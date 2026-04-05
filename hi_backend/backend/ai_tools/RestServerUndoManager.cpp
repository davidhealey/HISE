/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licensing:
*
*   http://www.hise.audio/
*
*   HISE is based on the JUCE library,
*   which must be separately licensed for closed source applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

namespace hise {
using namespace juce;

RestServerUndoManager::ActionBase::Error::Error(const ActionBase& om)
{
	message = om.getDescription();
	ok = false;
}



RestServerUndoManager::ActionBase::Error RestServerUndoManager::ActionBase::Error::withHint(const String& hint) const
{
	auto copy = *this;
	copy.message << hint;
	return copy;
}

RestServerUndoManager::ActionBase::Error RestServerUndoManager::ActionBase::Error::withError(const String& error) const
{
	auto copy = *this;
	copy.ok = false;
	copy.message << error;
	return copy;
}

RestServerUndoManager::ActionBase::Error RestServerUndoManager::Factory::prevalidate(Domain d, const var& obj) const
{
	if (!obj.isObject())
		return ActionBase::Error().withError("must be an object");

	auto op = obj[RestApiIds::op].toString();

	if (op.isEmpty())
		return ActionBase::Error().withError("op field is required");

	StringArray supportedOps;

	if (registeredFunctions.find(d) != registeredFunctions.end())
	{
		auto& r = registeredFunctions.at(d);

		if (r.find(op) != r.end())
			return r.at(op).first(const_cast<MainController*>(getMainController()), obj);

		for (const auto& f : r)
			supportedOps.add(f.first);
	}

	return ActionBase::Error()
		.withError("unsupported op at domain " + getDomains()[(int)d] + ": " + op)
		.withHint("supported ops: " + supportedOps.joinIntoString(","));
}

RestServerUndoManager::ActionBase::Ptr RestServerUndoManager::Factory::create(Domain d, const var& obj) const
{
	if (registeredFunctions.find(d) != registeredFunctions.end())
	{
		auto& r = registeredFunctions.at(d);

		auto op = obj[RestApiIds::op].toString();

		if (r.find(op) != r.end())
		{
			auto ad = r.at(op).second(const_cast<MainController*>(getMainController()), obj);
			ad->d = d;
			return ad;
		}
	}

	return nullptr;
}

void RestServerUndoManager::Factory::registerAllFunctions()
{
	registerCreatorFunctionT<rest_undo::builder::add>(Domain::Builder);
	registerCreatorFunctionT<rest_undo::builder::remove>(Domain::Builder);
	registerCreatorFunctionT<rest_undo::builder::clone>(Domain::Builder);
	registerCreatorFunctionT<rest_undo::builder::set_attributes>(Domain::Builder);
	registerCreatorFunctionT<rest_undo::builder::set_id>(Domain::Builder);
	registerCreatorFunctionT<rest_undo::builder::set_bypassed>(Domain::Builder);
	registerCreatorFunctionT<rest_undo::builder::set_effect>(Domain::Builder);
	registerCreatorFunctionT<rest_undo::builder::set_complex_data>(Domain::Builder);
}

hise::RestServerUndoManager::Instance* RestServerUndoManager::Instance::getOrCreate(MainController* mc, RestHelpers::ApiRoute endpoint)
{
	auto bp = dynamic_cast<BackendProcessor*>(mc);
	auto um = dynamic_cast<Instance*>(bp->getOrCreateRestServerBuildUndoManager());
	um->setCurrentEndpoint(endpoint);
	um->setValidationErrors({});
	return um;
}

void RestServerUndoManager::Instance::flushUI(Processor* p)
{
	SafeAsyncCall::callAsyncIfNotOnMessageThread<Processor>(*p, [](Processor& p)
	{
		p.sendRebuildMessage(true);
		p.getMainController()->getProcessorChangeHandler().sendProcessorChangeMessage(&p,
			MainController::ProcessorChangeHandler::EventType::RebuildModuleList,
			false);
	});
}

RestServerUndoManager::Instance::Instance(MainController* mc) :
	ControlledObject(mc),
	f(mc)
{
	clearUndoHistory();
}

hise::RestServer::Response RestServerUndoManager::Instance::getResponse(const std::vector<CallStack>& callstacks, var r)
{
	DynamicObject::Ptr result = new DynamicObject();
	result->setProperty(RestApiIds::success, callstacks.empty());
	result->setProperty(RestApiIds::result, r);
	result->setProperty(RestApiIds::logs, Array<var>());
	result->setProperty(RestApiIds::errors, CallStack::toJSONList(callstacks));
	return RestServer::Response::ok(var(result.get()));
}

bool RestServerUndoManager::Instance::killVoicesAndPerform(AsyncRequest::Ptr req, ActionBase::Ptr a, bool shouldUndo /*= false*/)
{
	if (!a->needsKillVoice())
	{
		try
		{
			a->planValidation = nullptr;

			if (shouldUndo)
				a->undo();
			else
				a->perform();
		}
		catch (ActionBase::Error& e)
		{
			callStack.push_back(CallStack(e.getErrorMessage())
				.withGroup(getCurrentGroupId())
				.withPhase(CallStack::Phase::Runtime)
				.withEndpoint(currentEndpoint));
		}
		
		req->complete(getResponse(callStack, getDiffJSON(true, true)));

		if ((a->getRebuildLevel(Domain::Builder, shouldUndo) & RebuildLevel::UpdateUI) != 0)
			flushUI(getMainController()->getMainSynthChain());

		if ((a->getRebuildLevel(Domain::UI, shouldUndo) & RebuildLevel::Recompile) != 0)
		{
			// TODO: trigger compilation asynchronously...
			jassertfalse;
		}

		return true;
	}

	getMainController()->getKillStateHandler().killVoicesAndCall(
		getMainController()->getMainSynthChain(),
		[a, shouldUndo, req, this](Processor* p)
		{
			try
			{
				a->planValidation = nullptr;

				if (shouldUndo)
					a->undo();
				else
					a->perform();
			}
			catch (ActionBase::Error& e)
			{
				callStack.push_back(CallStack(e.getErrorMessage())
					.withGroup(getCurrentGroupId())
					.withPhase(CallStack::Phase::Runtime)
					.withEndpoint(currentEndpoint));
			}

			req->complete(getResponse(callStack, getDiffJSON(true, true)));

			if ((a->getRebuildLevel(Domain::Builder, shouldUndo) & RebuildLevel::UpdateUI) != 0)
				flushUI(p);

			if ((a->getRebuildLevel(Domain::UI, shouldUndo) & RebuildLevel::Recompile) != 0)
			{
				// TODO: trigger compilation synchronously...
				jassertfalse;
			}

			return SafeFunctionCall::OK;
		},
		MainController::KillStateHandler::TargetThread::SampleLoadingThread
	);

	return false;
}

void RestServerUndoManager::Instance::performAction(AsyncRequest::Ptr req, ActionBase::Ptr l)
{
	if (auto cs = getCurrentStack())
	{
		if (!cs->actions.isEmpty() && cs->currentAction != cs->actions.getLast())
			cs->actions.removeRange(cs->actions.indexOf(cs->currentAction) + 1, INT_MAX);

		cs->actions.add(l);
		cs->currentAction = cs->actions.getLast();

		if (!isTestRun(cs))
			killVoicesAndPerform(req, l);
		else
			req->complete(getResponse({}, getDiffJSON(true, true)));
	}
}

void RestServerUndoManager::Instance::performAction(AsyncRequest::Ptr req, ActionBase::List list, const PostOpCallback& postOp, const String& name /*= {}*/)
{
	ActionBase::Ptr set = new Set(getMainController(), list, postOp, name);
	performAction(req, set);
}

void RestServerUndoManager::Instance::clearUndoHistory()
{
	rootActions = new StackItem();
	rootActions->name = "root";
	planStack.clear();
}

juce::ValueTree RestServerUndoManager::Instance::getValidationPlan() const
{
	if (auto p = getCurrentStack())
	{
		if (p->planValidationState != nullptr)
		{
			return p->planValidationState->moduleTree;
		}
	}

	return {};
}

bool RestServerUndoManager::Instance::undo(AsyncRequest::Ptr req)
{
	if (auto cs = getCurrentStack())
	{
		if (cs->currentAction != nullptr)
		{
			auto actionToUndo = cs->currentAction;
			auto idx = cs->actions.indexOf(cs->currentAction) - 1;

			if (idx >= 0)
				cs->currentAction = cs->actions[idx];
			else
				cs->currentAction = nullptr;

			if (!isTestRun(cs))
				killVoicesAndPerform(req, actionToUndo, true);
			else
				req->complete(getResponse({}, getDiffJSON(true, true)));

			return true;
		}
	}

	return false;
}

bool RestServerUndoManager::Instance::redo(AsyncRequest::Ptr req)
{
	if (auto cs = getCurrentStack())
	{
		if (cs->currentAction == nullptr && !cs->actions.isEmpty())
		{
			cs->currentAction = cs->actions.getFirst();

			if (!isTestRun(cs))
				killVoicesAndPerform(req, cs->currentAction);
			else
				req->complete(getResponse(callStack, getDiffJSON(true, true)));

			return true;
		}

		if (cs->currentAction != nullptr && cs->currentAction != cs->actions.getLast())
		{
			auto idx = cs->actions.indexOf(cs->currentAction) + 1;
			cs->currentAction = cs->actions[idx];

			if (!isTestRun(cs))
				killVoicesAndPerform(req, cs->currentAction);
			else
				req->complete(getResponse(callStack, getDiffJSON(true, true)));

			return true;
		}
	}

	return false;
}

void RestServerUndoManager::Instance::pushPlan(const String& name)
{
	auto ns = new StackItem();
	ns->name = name;
	ns->planValidationState = new PlanValidationState(getMainController());

	planStack.add(ns);
}

bool RestServerUndoManager::Instance::popPlan(AsyncRequest::Ptr req)
{
	if (planStack.isEmpty())
		return false;

	auto shouldCancel = (bool)req->getRequest().getJsonBody().getProperty(RestApiIds::cancel, false);

	auto lastPlan = planStack.removeAndReturn(planStack.size() - 1);

	if (!shouldCancel)
	{
		ActionBase::List activeActions;
		for (int i = 0; i <= lastPlan->actions.indexOf(lastPlan->currentAction); i++)
			activeActions.add(lastPlan->actions[i]);
		performAction(req, activeActions, {}, lastPlan->name);
	}

	return true;
}

}
