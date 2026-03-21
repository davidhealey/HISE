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
namespace rest_undo {



using namespace juce;
using namespace scriptnode;

using ActionBase       = RestServerUndoManager::ActionBase;
using Error            = RestServerUndoManager::ActionBase::Error;
using RebuildLevel     = RestServerUndoManager::RebuildLevel;
using Domain           = RestServerUndoManager::Domain;
using Diff             = RestServerUndoManager::Diff;

#define BUILDER_ID(x) static String getActionIdStatic() { return #x; } \
String getActionId() const override { return getActionIdStatic(); } \
static ActionBase::Ptr create(MainController* mc, const var& obj) { return new x(mc, obj);}

namespace builder {

	struct ProcessorReference
	{
		ProcessorReference(const String& id_) :
			id(id_)
		{}

		Processor* get() const { return processor.get(); }

		String getId() const
		{
			if (planData.isValid())
				return planData["ID"].toString();

			if (processor != nullptr)
				return processor->getId();

			return id;
		}
		Identifier getType() const
		{
			if (planData.isValid())
				return planData.getType();

			if (processor != nullptr)
				return processor->getType();

			return Identifier();
		}

		bool initAtValidation(RestServerUndoManager::PlanValidationState::Ptr data, MainController* mc)
		{
			if (data != nullptr)
				return initPlanValidation(data);

			return initAtPerform(mc);
		}

		bool initPlanValidation(RestServerUndoManager::PlanValidationState::Ptr data)
		{
			if (data != nullptr)
			{
				processor = nullptr;
				planData = data->get(getId());
			}
			else
				planData = ValueTree();

			return existsInPlanValidation();
		}

		bool initAtPerform(MainController* mc)
		{
			planData = ValueTree();
			processor = ProcessorHelpers::getFirstProcessorWithName(mc->getMainSynthChain(), id);
			return existsInRuntime();
		}

		bool existsInRuntime() const { return processor != nullptr; }

		bool existsInPlanValidation() const
		{
			if (planData.isValid())
				return (bool)valuetree::Helpers::getRoot(planData)["Root"];

			return false;
		}

		bool isInPlanValidationMode() const
		{
			return planData.isValid();
		}

		bool isDeleted() const
		{
			if (isInPlanValidationMode())
				return !existsInPlanValidation();

			return !existsInRuntime();
		}

		void setProcessor(Processor* p)
		{
			processor = p;
		}

		void setId(String newId)
		{
			id = newId;

			if (processor != nullptr)
				processor->setId(newId, sendNotificationAsync);
		}

		String id;
		WeakReference<Processor> processor;
		ValueTree planData;
	};

struct Helpers
{
	static String getChainId(int index)
	{
		using namespace raw::IDs;

		if (index == Chains::Direct)
			return "direct";
		if (index == Chains::Midi)
			return "midi";
		if (index == Chains::Gain)
			return "gain";
		if (index == Chains::Pitch)
			return "pitch";
		if (index == Chains::FX)
			return "fx";

		return String(index);
	}

	static void throwProcessorDeleted(const String& moduleId)
	{
		throw Error().withError(moduleId + " was deleted");
	}

	static void throwRuntimeError(const String& errorMessage)
	{
		throw Error().withError(errorMessage);
	}

	static String getHintForUnknownString(const String& errorString, const StringArray& availableOptions)
	{
		auto correction = FuzzySearcher::suggestCorrection(errorString, availableOptions, 0.6);

		String hint;

		if (correction.isNotEmpty())
			hint << "Did you mean " << correction;
		else if (availableOptions < 6)
			hint << "Available parameters: " << availableOptions.joinIntoString(",");

		return hint;
	}

	static bool isChain(const Identifier& id)
	{
		return id == MidiProcessorChain::getClassType() ||
			   id == ModulatorChain::getClassType() ||
			   id == EffectProcessorChain::getClassType();
	}

	static std::pair<ProcessorReference, int> getParentSynthAndChainIndex(const ProcessorReference& p)
	{
		if (p.existsInRuntime())
		{
			auto parentChain = p.get()->getParentProcessor(false);
			auto realParent = parentChain;

			if (isChain(parentChain->getType()))
				realParent = parentChain->getParentProcessor(false);

			ProcessorReference parent("");

			if (realParent == nullptr)
				return { parent, -2};

			parent.setId(realParent->getId());
			parent.setProcessor(realParent);

			if (parentChain == realParent)
				return { parent, -1 };

			for (int i = 0; i < realParent->getNumInternalChains(); i++)
			{
				if (realParent->getChildProcessor(i) == parentChain)
					return { parent, i };
			}

			return { parent, -2 };
		}
		else
		{
			auto pData = p.planData;

			auto parentChain = pData.getParent();
			auto realParent = parentChain;

			if (isChain(parentChain.getType()))
				realParent = parentChain.getParent();

			ProcessorReference parent("");

			if (!realParent.isValid())
				return { parent, -2 };

			parent.setId(realParent["ID"]);
			parent.planData = realParent;

			if (parentChain == realParent)
				return { parent, -1 };

			auto chainIndex = realParent.indexOf(parentChain);

			if(chainIndex != -1)
				return { parent, chainIndex };

			return { parent, -2 };
		}
	}

	static void deleteProcessorAsync(Processor* pToDelete)
	{
		auto f = [](Processor* p)
		{
			auto mc = p->getMainController();

			if (auto c = dynamic_cast<Chain*>(p->getParentProcessor(false)))
			{
				c->getHandler()->remove(p, false);
				jassert(!p->isOnAir());
			}

			mc->getProcessorChangeHandler().sendProcessorChangeMessage(mc->getMainSynthChain(), 
				MainController::ProcessorChangeHandler::EventType::RebuildModuleList, 
				false);

			return SafeFunctionCall::OK;
		};

		pToDelete->getMainController()->getGlobalAsyncModuleHandler().removeAsync(pToDelete, f);
	}
};



struct add : public ActionBase
{
	BUILDER_ID(add);

	static Error prevalidate(MainController*, const var& op)
	{
		if (op[RestApiIds::type].toString().isEmpty())
			return Error().withError("add requires type");

		if (op[RestApiIds::parent].toString().isEmpty())
			return Error().withError("add requires a parent");

		auto chainVal = op[RestApiIds::chain];

		if (chainVal.isVoid() || chainVal.isUndefined())
			return Error().withError("add requires 'chain' (int: -1 = direct, 0 = midi, 1 = gain, 2 = pitch, 3 = fx)");

		return {};
	}

	add(MainController* mc, const var& obj) :
		ActionBase(mc),
		typeId(obj["type"].toString()),
		parentProcessor(obj["parent"].toString()),
		createdProcessor(obj["name"].toString()),
		chainIndex((int)obj["chain"])
	{}

	Identifier typeId;
	int chainIndex;

	ProcessorReference parentProcessor;
	ProcessorReference createdProcessor;

	int getRebuildLevel(Domain d, bool undo) const override
	{
		if (d != Domain::Builder)
			return 0;

		if (!undo)
		{
			// adding a module needs to set the unique ID afterwards
			return RebuildLevel::UpdateUI | RebuildLevel::UniqueId;
		}
		else
		{
			return RebuildLevel::UpdateUI;
		}
	}

	void addToDiffList(std::vector<Diff>& diffList, bool undo) override
	{
		Diff d;
		d.target = createdProcessor.getId();
		d.domain = Domain::Builder;
		d.type = undo ? Diff::Type::Remove : Diff::Type::Add;

		diffList.push_back(d);
	}

	void perform() override
	{
		// should be caught before
		jassert(validate());

		if (!parentProcessor.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(parentProcessor.getId());

		raw::Builder b(getMainController());
		createdProcessor.setProcessor(b.create(parentProcessor.get(), typeId, chainIndex));

		createdProcessor.setId(createdProcessor.id);
	}

	void undo() override
	{
		if (!createdProcessor.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(createdProcessor.getId());

		auto mc = getMainController();
		Helpers::deleteProcessorAsync(createdProcessor.get());
	}

	bool needsKillVoice() const override { return true; }

	String getHistoryMessage(bool undo) const override
	{
		return (undo ? "Remove " : "Add ") + createdProcessor.getId();
	}

	String getDescription() const override
	{
		String s;
		s << "add " << typeId << " to " << parentProcessor.getId() << "." << Helpers::getChainId(chainIndex) << ": ";
		return s;
	}

	Error expectChainIndex(const Identifier& typeId, int expectedIndex) const
	{
		if (chainIndex != expectedIndex)
		{
			String msg;
			msg << "Use chain index " << String(expectedIndex) << "(" << Helpers::getChainId(expectedIndex) << ")";
			return Error(*this).withHint(msg);
		}

		return {};
	}

	Error expectWildcardMatch(const Identifier& subtype, const String& wildcard) const
	{
		if (!subtype.toString().matchesWildcard(wildcard, false))
		{
			return Error(*this)
				.withError("Illegal subtype " + subtype.toString())
				.withHint("Allowed types: " + wildcard);
		}

		return {};
	}

	Error validate() override
	{
		using namespace raw::IDs;

		if (!parentProcessor.initAtValidation(planValidation, getMainController()))
			return Error().withError("Can't find parent with ID " + parentProcessor.getId());

		auto parentId = parentProcessor.getType();

		ProcessorMetadataRegistry rd;
		const ProcessorMetadata* pm = rd.get(parentId);
		const ProcessorMetadata* md = rd.get(typeId);

		if (md == nullptr)
		{
			StringArray sa;

			for (auto& id : rd.getRegisteredTypes())
				sa.add(id.toString());

			auto e = Error().withError("unknown module type " + typeId.toString());

			auto hint = FuzzySearcher::suggestCorrection(typeId.toString(), sa);

			if (hint.isNotEmpty())
				e = e.withHint("Did you mean: " + hint);

			return e;
		}

		Error e;

		// check type match
		if (md->type == ProcessorMetadataIds::MidiProcessor)
		{
			e = expectChainIndex(md->type, Chains::Midi);
			if (!e)
				return e;
		}
		else if (md->type == ProcessorMetadataIds::Effect)
		{
			e = expectChainIndex(md->type, Chains::FX);
			if (!e)
				return e;

			return expectWildcardMatch(md->subtype, pm->fxConstrainerWildcard);
		}
		else if (md->type == ProcessorMetadataIds::SoundGenerator)
		{
			e = expectChainIndex(md->type, Chains::Direct);
			if (!e)
				return e;

			e = expectWildcardMatch(md->subtype, pm->constrainerWildcard);

			if (!e)
				return e;
		}
		else
		{
			jassert(md->type == ProcessorMetadataIds::Modulator);

			if (chainIndex == Chains::Direct || chainIndex == Chains::Midi || chainIndex == Chains::FX)
			{
				e = expectChainIndex(md->type, Chains::Gain);

				if (!e)
					return e;
			}
			
			String hint;

			bool found = false;

			for (const auto& mod : pm->modulation)
			{
				if (mod.chainIndex == chainIndex)
				{

					if (mod.disabled)
						return Error(*this).withError(mod.id.toString() + " is disabled");

					
					e = expectWildcardMatch(md->subtype, mod.constrainerWildcard);

					if (!e)
						return e;

					found = true;
					break;
				}

				if (!mod.disabled)
					hint << String(mod.chainIndex) << "(" << mod.id.toString() << "), ";
			}

			if (!found)
			{
				return Error(*this).withError("Illegal modulation chain index " + String(chainIndex))
					.withHint("Available modulation chains: " + hint);
			}
		}

		if (planValidation != nullptr)
		{
			if (!planValidation->add(parentProcessor.getId(), typeId, createdProcessor.getId(), chainIndex))
				return Error().withError("Plan model update failed");
		}

		return {};
	}
};

struct remove : public ActionBase
{
	BUILDER_ID(remove);

	static Error prevalidate(MainController*, const var& op)
	{
		if (op[RestApiIds::target].toString().isEmpty())
			return Error().withError("remove requires 'target'");
		return {};
	}

	remove(MainController* mc, const var& obj) :
		ActionBase(mc),
		targetProcessor(obj["target"].toString()),
		parentProcessor({ ProcessorReference(""), -2 })
	{}

	ProcessorReference targetProcessor;
	std::pair<ProcessorReference, int> parentProcessor;

	ValueTree savedState;

	int getRebuildLevel(Domain d, bool undo) const override
	{
		if (d != Domain::Builder) return 0;
		return RebuildLevel::UpdateUI;
	}

	void addToDiffList(std::vector<Diff>& diffList, bool undo) override
	{
		Diff d;
		d.target = targetProcessor.getId();
		d.domain = Domain::Builder;
		d.type = undo ? Diff::Type::Add : Diff::Type::Remove;
		diffList.push_back(d);
	}

	void perform() override 
	{ 
		if (!targetProcessor.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(targetProcessor.getId());

		savedState = targetProcessor.get()->exportAsValueTree();

		Helpers::deleteProcessorAsync(targetProcessor.get());
	}

	void undo() override 
	{ 
		if(!parentProcessor.first.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(parentProcessor.first.getId());

		raw::Builder b(getMainController());

		targetProcessor.setProcessor(b.create(parentProcessor.first.get(), targetProcessor.getType(), parentProcessor.second));
		targetProcessor.setId(savedState["ID"].toString());
		targetProcessor.get()->restoreFromValueTree(savedState);
	}

	bool needsKillVoice() const override { return true; }

	String getHistoryMessage(bool undo) const override
	{
		return (undo ? "Add " : "Remove ") + targetProcessor.getId();
	}

	String getDescription() const override
	{
		return "remove " + targetProcessor.getId();
	}

	Error validate() override
	{
		if (!targetProcessor.initAtValidation(planValidation, getMainController()))
			return Error().withError("Can't find module with ID " + targetProcessor.getId());

		parentProcessor = Helpers::getParentSynthAndChainIndex(targetProcessor);

		if (planValidation != nullptr)
		{
			if(!planValidation->remove(targetProcessor.getId()))
				return Error().withError("Plan model update failed");
		}

		return {};
	}
};

struct clone : public ActionBase
{
	BUILDER_ID(clone);

	static Error prevalidate(MainController*, const var& op)
	{
		if (op[RestApiIds::source].toString().isEmpty())
			return Error().withError("clone requires 'source'");
		int c = (int)op.getProperty(RestApiIds::count, 0);
		if (c < 1)
			return Error().withError("clone requires count >= 1");
		if (c > 100)
			return Error().withError("clone count must be <= 100");
		return {};
	}

	clone(MainController* mc, const var& obj) :
		ActionBase(mc),
		source(obj["source"].toString()),
		insertPosition({ ProcessorReference(""), -2 }),
		cloneCount((int)obj["count"])
	{}

	ProcessorReference source;
	std::pair<ProcessorReference, int> insertPosition;
	Array<ProcessorReference> createdClones;

	const int cloneCount;

	int getRebuildLevel(Domain d, bool undo) const override
	{
		if (d != Domain::Builder) 
			return 0;
		return RebuildLevel::UpdateUI | (undo ? 0 : RebuildLevel::UniqueId);
	}

	void addToDiffList(std::vector<Diff>& diffList, bool undo) override
	{
		for (const auto& name : createdClones)
		{
			Diff d;
			d.target = name.getId();
			d.domain = Domain::Builder;
			d.type = undo ? Diff::Type::Remove : Diff::Type::Add;
			diffList.push_back(d);
		}
	}

	void perform() override 
	{ 
		if (!source.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(source.getId());

		if(!insertPosition.first.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(insertPosition.first.getId());

		auto copy = source.get()->exportAsValueTree();

		raw::Builder b(getMainController());

		auto t = source.getType();

		for (int i = 0; i < cloneCount; i++)
		{
			auto cloned = b.create(insertPosition.first.get(), t, insertPosition.second);

			cloned->setId(source.getId(), dontSendNotification);
			auto thisId = FactoryType::getUniqueName(cloned, source.getId());
			cloned->setId(thisId, dontSendNotification);
			copy.setProperty("ID", thisId, nullptr);
			cloned->restoreFromValueTree(copy);

			ProcessorReference nd(thisId);
			nd.setProcessor(cloned);
			createdClones.add(nd);
		}
	}

	void undo() override 
	{ 
		if(!insertPosition.first.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(insertPosition.first.getId());

		for (auto b : createdClones)
		{
			if (b.initAtPerform(getMainController()))
				Helpers::deleteProcessorAsync(b.get());
			else
				Helpers::throwProcessorDeleted(b.getId());
		}
	}

	bool needsKillVoice() const override { return true; }

	String getHistoryMessage(bool undo) const override
	{
		return (undo ? "Remove " : "Clone ") + String(cloneCount) + "x " + source.getId();
	}

	String getDescription() const override
	{
		String msg;
		msg << "clone " << source.getId() << " -> ";

		for (auto cn : createdClones)
			msg << cn.getId() << " ";

		return msg;
	}

	Error validate() override
	{
		if (!source.initAtValidation(planValidation, getMainController()))
			return Error().withError("Can't find module with ID " + source.getId());

		insertPosition = Helpers::getParentSynthAndChainIndex(source);

		if(!insertPosition.first.initAtValidation(planValidation, getMainController()))
			return Error().withError("Can't find module with ID " + insertPosition.first.getId());

		if (planValidation != nullptr)
		{
			for (int i = 0; i < cloneCount; i++)
			{
				auto clone = source.planData.createCopy();
				
				if (!planValidation->add(insertPosition.first.getId(), clone, insertPosition.second))
					return Error().withError("Plan model update failed");
			}
		}

		return {};
	}
};

struct set_attributes : public ActionBase
{
	BUILDER_ID(set_attributes);

	static Error prevalidate(MainController*, const var& op)
	{
		if (op[RestApiIds::target].toString().isEmpty())
			return Error().withError("set_attributes requires 'target'");
		if (!op[RestApiIds::attributes].isObject())
			return Error().withError("set_attributes requires 'attributes' object");
		auto modeStr = op.getProperty(RestApiIds::mode, "value").toString();
		if (modeStr != "value" && modeStr != "normalized" && modeStr != "raw")
			return Error().withError("set_attributes mode must be 'value', 'normalized', or 'raw'");
		return {};
	}

	set_attributes(MainController* mc, const var& obj) :
		ActionBase(mc),
		target(obj["target"].toString()),
		attributes(obj["attributes"]),
		mode(obj.getProperty("mode", "value").toString())
	{}

	var attributes;
	String mode;

	ProcessorReference target;

	mutable std::map<Identifier, float> oldValues;
	mutable std::map<Identifier, float> newValues;

	var previousAttributes; // saved on perform for undo

	int getRebuildLevel(Domain d, bool undo) const override
	{
		// no rebuild necessary - the attributes send their update message
		return 0;
	}

	void addToDiffList(std::vector<Diff>& diffList, bool undo) override
	{
		Diff d;
		d.target = target.getId();
		d.domain = Domain::Builder;
		d.type = Diff::Type::Modify;
		diffList.push_back(d);
	}

	void perform() override 
	{ 
		if (!target.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(target.getId());

		static const Identifier intensity("Intensity");

		auto targetProcessor = target.get();

		for (const auto& p : newValues)
		{
			if (p.first == intensity)
			{
				if (auto mod = dynamic_cast<Modulation*>(targetProcessor))
				{
					oldValues[p.first] = mod->getIntensity();
					mod->setIntensity(p.second);
					continue;
				}
			}

			auto idx = targetProcessor->getParameterIndexForIdentifier(p.first);

			if (idx == -1)
				Helpers::throwRuntimeError("Illegal parameter " + p.first);

			oldValues[p.first] = targetProcessor->getAttribute(idx);
			targetProcessor->setAttribute(idx, p.second, sendNotificationAsync);
		}
	}

	void undo() override 
	{ 
		if (!target.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(target.getId());

		auto targetProcessor = target.get();

		static const Identifier intensity("Intensity");

		for (const auto& p : oldValues)
		{
			if (p.first == intensity)
			{
				if (auto mod = dynamic_cast<Modulation*>(targetProcessor))
				{
					mod->setIntensity(p.second);
					continue;
				}
			}

			auto idx = targetProcessor->getParameterIndexForIdentifier(p.first);

			if (idx == -1)
				Helpers::throwRuntimeError("Illegal parameter " + p.first);

			targetProcessor->setAttribute(idx, p.second, sendNotificationAsync);
		}
	}

	bool needsKillVoice() const override { return false; }

	String getHistoryMessage(bool undo) const override
	{
		return (undo ? "Restore " : "Set ") + String("attributes on ") + target.getId();
	}

	String getDescription() const override
	{
		return "set_attributes on " + target.getId();
	}

	Error validate() override
	{
		if (!target.initAtValidation(planValidation, getMainController()))
			return Error().withError("Can't find module with ID " + target.getId());

		ProcessorMetadata md;

		bool skipDynamicParameters = false;

		// validate attribute names and ranges using ProcessorMetadata
		if (auto tp = target.get())
			md = tp->getMetadata();
		else
		{
			ProcessorMetadataRegistry rd;

			if (auto pmd = rd.get(target.getType()))
				md = *pmd;
			else
				return Error().withError("illegal type" + target.getType().toString());

			skipDynamicParameters = md.dataType == ProcessorMetadata::DataType::Dynamic;
		}

		for (auto& jsonParameter : attributes.getDynamicObject()->getProperties())
		{
			bool found = false;

			for (const ProcessorMetadata::ParameterMetadata& p : md.parameters)
			{
				if (jsonParameter.name.toString().toLowerCase() == "intensity")
				{
					if (md.type == ProcessorMetadataIds::Modulator)
					{
						oldValues["Intensity"] = 0.0f; // tbd
						newValues["Intensity"] = (float)jsonParameter.value;
						found = true;
						break;
					}
				}

				if (jsonParameter.name == p.id)
				{
					double value;

					if (!p.vtc.itemList.isEmpty())
					{
						auto v = jsonParameter.value.toString().toLowerCase();

						int idx = 1;

						bool valueFound = false;

						for (auto& it : p.vtc.itemList)
						{
							if (it.toLowerCase() == v)
							{
								value = (double)idx + 1;
								valueFound = true;
								break;
							}
	
							idx++;
						}

						if (!valueFound)
							return Error().withError(p.id.toString() + ": illegal value. ")
							.withHint(Helpers::getHintForUnknownString(v, p.vtc.itemList));
					}
					else
						value = (double)jsonParameter.value;

					if (p.type == ProcessorMetadata::ParameterMetadata::Type::List)
						value += 1.0;

					if (p.range.getRange().contains(value - 0.001))
					{
						newValues[p.id] = (float)value;
						oldValues[p.id] = 0.0f; // tbd
					}
					else
					{
						String e;
						e << "parameter " << p.id.toString() << ": " << String(value) << " is outside range [";
						e << String(p.range.rng.start) << " - " << String(p.range.rng.end) << "]";

						return Error().withError(e);
					}

					found = true;
					break;
				}
			}

			if (!found)
			{
				// In validation mode, dynamic parameters might not be visible to the dynamic metadata
				// so we need to let it slip through.
				if (skipDynamicParameters)
				{
					debugToConsole(getMainController()->getMainSynthChain(),
						"Skipping dynamic parameter validation for " + jsonParameter.name);

					continue;
				}

				StringArray parameterNames;
				for (auto& p : md.parameters)
					parameterNames.add(p.id.toString());

				auto errorName = jsonParameter.name.toString();
				auto hint = Helpers::getHintForUnknownString(errorName, parameterNames);

				return Error().withError("Unknown parameter " + errorName)
					.withHint(hint);
			}
		}

		return {};
	}
};

struct set_id : public ActionBase
{
	BUILDER_ID(set_id);

	static Error prevalidate(MainController*, const var& op)
	{
		if (op[RestApiIds::target].toString().isEmpty())
			return Error().withError("set_id requires 'target'");
		if (op[RestApiIds::name].toString().isEmpty())
			return Error().withError("set_id requires 'name'");
		return {};
	}

	set_id(MainController* mc, const var& obj) :
		ActionBase(mc),
		previousName(obj["target"].toString()),
		newName(obj["name"].toString()),
		targetProcessor(previousName)
	{}

	String newName;
	String previousName;
	ProcessorReference targetProcessor;

	int getRebuildLevel(Domain d, bool undo) const override
	{
		return 0;
	}

	void addToDiffList(std::vector<Diff>& diffList, bool undo) override
	{
		Diff d;
		d.target = undo ? newName : previousName;
		d.domain = Domain::Builder;
		d.type = Diff::Type::Modify;
		diffList.push_back(d);
	}

	void perform() override 
	{ 
		if (!targetProcessor.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(targetProcessor.getId());

		previousName = targetProcessor.getId();
		targetProcessor.setId(newName);
	}

	void undo() override 
	{ 
		if (!targetProcessor.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(targetProcessor.getId());

		targetProcessor.setId(previousName);
	}

	bool needsKillVoice() const override { return false; }

	String getHistoryMessage(bool undo) const override
	{
		return undo ? "Rename " + newName + " back to " + previousName
		            : "Rename " + previousName + " to " + newName;
	}

	String getDescription() const override
	{
		return "set_id " + previousName + " -> " + newName;
	}

	Error validate() override
	{
		if (!targetProcessor.initAtValidation(planValidation, getMainController()) || targetProcessor.isDeleted())
			return Error().withError("Can't find module with ID " + targetProcessor.getId());
		
		if (previousName == newName)
			return {};

		if (planValidation != nullptr)
		{
			if(planValidation->get(newName).isValid())
				return Error().withError("A module with the name " + newName + " already exists");

			if (!planValidation->setId(previousName, newName))
				return Error().withError("Plan model update failed");
		}
		else
		{
			if (auto p = ProcessorHelpers::getFirstProcessorWithName(getMainController()->getMainSynthChain(), newName))
				return Error().withError("A module with the name " + newName + " already exists");
		}

		return {};
	}
};

struct set_bypassed : public ActionBase
{
	BUILDER_ID(set_bypassed);

	static Error prevalidate(MainController*, const var& op)
	{
		if (op[RestApiIds::target].toString().isEmpty())
			return Error().withError("set_bypassed requires 'target'");
		auto bypassVal = op[RestApiIds::bypassed];
		if (bypassVal.isVoid() || bypassVal.isUndefined())
			return Error().withError("set_bypassed requires 'bypassed'");
		return {};
	}

	set_bypassed(MainController* mc, const var& obj) :
		ActionBase(mc),
		target(obj["target"].toString()),
		bypassed((bool)obj["bypassed"])
	{}

	ProcessorReference target;

	bool bypassed;
	bool previousBypassed = false;

	int getRebuildLevel(Domain d, bool undo) const override
	{
		return 0;
	}

	void addToDiffList(std::vector<Diff>& diffList, bool undo) override
	{
		Diff d;
		d.target = target.getId();
		d.domain = Domain::Builder;
		d.type = Diff::Type::Modify;
		diffList.push_back(d);
	}

	void perform() override 
	{ 
		if (!target.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(target.getId());
		
		auto targetProcessor = target.get();
		previousBypassed = targetProcessor->isBypassed();
		targetProcessor->setBypassed(bypassed, sendNotificationAsync);
	}

	void undo() override 
	{ 
		if (!target.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(target.getId());

		auto targetProcessor = target.get();

		targetProcessor->setBypassed(previousBypassed, sendNotificationAsync);
	}

	bool needsKillVoice() const override { return false; }

	String getHistoryMessage(bool undo) const override
	{
		return (undo ? "Restore " : (bypassed ? "Bypass " : "Unbypass ")) + target.getId();
	}

	String getDescription() const override
	{
		return String(bypassed ? "bypass " : "unbypass ") + target.getId();
	}

	Error validate() override
	{
		if(!target.initAtValidation(planValidation, getMainController()))
			return Error().withError("Can't find module with ID " + target.getId());

		return {};
	}
};

struct set_effect : public ActionBase
{
	BUILDER_ID(set_effect);

	static Error prevalidate(MainController*, const var& op)
	{
		if (op[RestApiIds::target].toString().isEmpty())
			return Error().withError("set_effect requires 'target'");
		if (op[RestApiIds::effect].toString().isEmpty())
			return Error().withError("set_effect requires 'effect'");
		return {};
	}

	set_effect(MainController* mc, const var& obj) :
		ActionBase(mc),
		target(obj["target"].toString()),
		effectName(obj["effect"].toString())
	{}

	ProcessorReference target;

	String effectName;
	String previousEffectName;

	int getRebuildLevel(Domain d, bool undo) const override
	{
		return 0;
	}

	void addToDiffList(std::vector<Diff>& diffList, bool undo) override
	{
		Diff d;
		d.target = target.getId();
		d.domain = Domain::Builder;
		d.type = Diff::Type::Modify;
		diffList.push_back(d);
	}

	Error checkRuntimeEffectSlot()
	{
		auto slotFX = dynamic_cast<HotswappableProcessor*>(target.get());

		if (slotFX == nullptr)
			return Error().withError("not a hotswappable processor");

		auto list = slotFX->getModuleList();

		if (!list.contains(effectName))
			return Error().withError("unknown effect")
			.withHint(Helpers::getHintForUnknownString(effectName, list));

		return {};
	}

	void perform() override 
	{ 
		if (!target.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(target.getId());

		auto ok = checkRuntimeEffectSlot();

		if (!ok)
			throw ok;

		auto slotFX = dynamic_cast<HotswappableProcessor*>(target.get());

		previousEffectName = slotFX->getCurrentEffectId();
		LockHelpers::SafeLock sl(getMainController(), LockHelpers::Type::AudioLock);
		slotFX->setEffect(effectName, false);
	}

	void undo() override 
	{ 
		if (!target.initAtPerform(getMainController()))
			Helpers::throwProcessorDeleted(target.getId());

		auto ok = checkRuntimeEffectSlot();

		if (!ok)
			throw ok;

		auto slotFX = dynamic_cast<HotswappableProcessor*>(target.get());

		LockHelpers::SafeLock sl(getMainController(), LockHelpers::Type::AudioLock);
		slotFX->setEffect(previousEffectName, false);
	}

	bool needsKillVoice() const override { return true; }

	String getHistoryMessage(bool undo) const override
	{
		return (undo ? "Restore effect on " : "Set effect '" + effectName + "' on ") + target.getId();
	}

	String getDescription() const override
	{
		return "set_effect " + effectName + " on " + target.getId();
	}

	Error validate() override
	{
		if(!target.initAtValidation(planValidation, getMainController()))
			return Error().withError("Can't find module with ID " + target.getId());

		if (target.existsInRuntime())
			return checkRuntimeEffectSlot();

		return {};
	}
};

struct set_complex_data : public ActionBase
{
	BUILDER_ID(set_complex_data);

	static Error prevalidate(MainController*, const var& op)
	{
		if (op[RestApiIds::target].toString().isEmpty())
			return Error().withError("set_complex_data requires 'target'");
		return {};
	}

	set_complex_data(MainController* mc, const var& obj) :
		ActionBase(mc),
		target(obj["target"].toString()),
		targetProcessor(ProcessorHelpers::getFirstProcessorWithName(mc->getMainSynthChain(), target))
	{}

	String target;
	ExternalData::DataType dt;

	var data;
	var previousData;
	WeakReference<Processor> targetProcessor;

	int getRebuildLevel(Domain d, bool undo) const override
	{
		if (d != Domain::Builder) return 0;
		return RebuildLevel::UpdateUI;
	}

	void addToDiffList(std::vector<Diff>& diffList, bool undo) override
	{
		Diff d;
		d.target = target;
		d.domain = Domain::Builder;
		d.type = Diff::Type::Modify;
		diffList.push_back(d);
	}

	void perform() override { /* TODO */ }
	void undo() override { /* TODO */ }

	bool needsKillVoice() const override { return false; }

	String getHistoryMessage(bool undo) const override
	{
		return (undo ? "Restore " : "Set ") + String("complex data on ") + target;
	}

	String getDescription() const override
	{
		return "set_complex_data on " + target;
	}

	Error validate() override
	{
		return Error().withError("not implemented yet");

		if (targetProcessor == nullptr)
			return Error().withError("Can't find module with ID " + target);
		return {};
	}
};

} // builder
} // rest_undo
} // hise