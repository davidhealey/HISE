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

namespace hise { using namespace juce;

//==============================================================================
// ScopedConsoleHandler implementation

RestHelpers::ScopedConsoleHandler::ScopedConsoleHandler(MainController* mc, RestServer::AsyncRequest::Ptr request_) :
	ControlledObject(mc),
	request(request_)
{
	debugToConsole(getMainController()->getMainSynthChain(), "> create console pipe");
	getMainController()->getConsoleHandler().setCustomCodeHandler(BIND_MEMBER_FUNCTION_3(ScopedConsoleHandler::onMessage));
}

RestHelpers::ScopedConsoleHandler::~ScopedConsoleHandler()
{
	getMainController()->getConsoleHandler().setCustomCodeHandler({});
	debugToConsole(getMainController()->getMainSynthChain(), "> close console pipe");
}

void RestHelpers::ScopedConsoleHandler::onMessage(const String& message, int warning, const Processor* p)
{
	if (warning == 0)
		request->appendLog(message);
	else
	{
		auto lines = StringArray::fromLines(message);

		auto scriptRoot = getMainController()->getSampleManager().getProjectHandler().getSubDirectory(FileHandlerBase::Scripts);
		auto moduleId = p->getId();

		auto error = parseError(lines[0], scriptRoot, moduleId);

		lines.remove(0);

		StringArray callstack;
		for (auto& entry : lines)
		{
			auto parsed = parseError(entry, scriptRoot, moduleId);
			if (parsed.location.isNotEmpty())
				callstack.add(parsed.toCallstackString());
		}

		request->appendError(error.message, callstack);
	}
}

String RestHelpers::ScopedConsoleHandler::ParsedError::toCallstackString() const
{
	if (functionName.isEmpty())
		return location;
	return functionName + "() at " + location;
}

RestHelpers::ScopedConsoleHandler::ParsedError RestHelpers::ScopedConsoleHandler::parseError(
	const String& errorString,
	const File& scriptRoot,
	const String& moduleId)
{
	ParsedError result;

	String working = errorString.trim();

	// Strip leading ":\t\t\t" from callstack entries
	if (working.startsWith(":"))
		working = working.fromFirstOccurrenceOf(":", false, false).trim();

	// Check if this is a callstack entry with function name: "funcName() - ..."
	if (working.contains("() - "))
	{
		result.functionName = working.upToFirstOccurrenceOf("()", false, false).trim();
		working = working.fromFirstOccurrenceOf("() - ", false, false);
	}

	// Extract message (everything before the encoded location)
	result.message = working.upToFirstOccurrenceOf("{{", false, false)
		.upToFirstOccurrenceOf("\t", false, false)
		.trim();

	// Extract and decode the Base64 location
	String encoded = working.fromFirstOccurrenceOf("{{", false, false)
		.upToFirstOccurrenceOf("}}", false, false);

	if (encoded.isNotEmpty())
	{
		MemoryOutputStream mos;
		if (Base64::convertFromBase64(mos, encoded))
		{
			String decoded(static_cast<const char*>(mos.getData()), mos.getDataSize());
			StringArray parts = StringArray::fromTokens(decoded, "|", "");

			if (parts.size() >= 5)
			{
				// Format: "processorId|relativePath|charIndex|line|col"
				String path = parts[1];
				int line = parts[3].getIntValue();
				int col = parts[4].getIntValue();

				// Build the path
				String fullPath;

				if (path.isEmpty() || path.contains("()"))
				{
					// Inline callback - use moduleId as filename
					fullPath = moduleId + ".js";
				}
				else
				{
					// External file
					File f = scriptRoot.getChildFile(path);
					if (f.existsAsFile())
						fullPath = f.getRelativePathFrom(scriptRoot.getParentDirectory()).replaceCharacter('\\', '/');
					else
						fullPath = path;
				}

				result.location = fullPath + ":" + String(line) + ":" + String(col);
			}
		}
	}

	return result;
}

//==============================================================================
// Static helper methods

JavascriptProcessor* RestHelpers::getScriptProcessor(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	String moduleId = req->getRequest()["moduleId"];

	if (moduleId.isEmpty())
		return nullptr;

	return dynamic_cast<JavascriptProcessor*>(ProcessorHelpers::getFirstProcessorWithName(mc->getMainSynthChain(), moduleId));
}

RestServer::Response RestHelpers::compile(MainController* mc, ScopedConsoleHandler& sch, RestServer::AsyncRequest::Ptr req)
{
	if (auto jp = getScriptProcessor(mc, req))
	{
		mc->getKillStateHandler().killVoicesAndCall(dynamic_cast<Processor*>(jp), [req, &sch](Processor* p)
		{
			JavascriptProcessor::ResultFunction rf = [req, &sch](const JavascriptProcessor::SnippetResult& result)
			{
				DynamicObject::Ptr r = new DynamicObject();
				r->setProperty("success", result.r.wasOk());
				r->setProperty("result", result.r.wasOk() ? "Recompiled OK" : "Compilation / Runtime Error");

				req->complete(RestServer::Response::ok(var(r.get())));
			};

			dynamic_cast<JavascriptProcessor*>(p)->compileScript(rf);
			return SafeFunctionCall::OK;
		}, MainController::KillStateHandler::TargetThread::ScriptingThread);
	}
	else
	{
		req->fail(404, "moduleId is not a valid script processor");
	}

	return req->waitForResponse();
}

DynamicObject::Ptr RestHelpers::createRecursivePropertyTree(ScriptComponent* sc)
{
	DynamicObject::Ptr obj = new DynamicObject();

	obj->setProperty("id", sc->getName().toString());
	obj->setProperty("type", sc->getObjectName().toString());
	obj->setProperty("visible", sc->getScriptObjectProperty(ScriptComponent::visible));
	obj->setProperty("enabled", sc->getScriptObjectProperty(ScriptComponent::enabled));
	obj->setProperty("x", sc->getScriptObjectProperty(ScriptComponent::x));
	obj->setProperty("y", sc->getScriptObjectProperty(ScriptComponent::y));
	obj->setProperty("width", sc->getScriptObjectProperty(ScriptComponent::width));
	obj->setProperty("height", sc->getScriptObjectProperty(ScriptComponent::height));

	Array<var> children;

	ScriptComponent::ChildIterator<ScriptComponent> ci(sc);

	auto v = sc->getPropertyValueTree();

	for (auto c : v)
	{
		if (auto child = sc->getScriptProcessor()->getScriptingContent()->getComponentWithName(c["id"].toString()))
		{
			auto cobj = createRecursivePropertyTree(child);
			children.add(var(cobj.get()));
		}
	}

	obj->setProperty("childComponents", var(children));

	return obj;
}

} // namespace hise
