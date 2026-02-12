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
// BaseScopedConsoleHandler implementation

RestHelpers::BaseScopedConsoleHandler::BaseScopedConsoleHandler(MainController* mc, bool enabled) :
	ControlledObject(mc),
	capturing(false)
{
	if (enabled && !mc->getConsoleHandler().hasCustomLogger())
	{
		capturing = true;
		mc->getConsoleHandler().setCustomCodeHandler(
			BIND_MEMBER_FUNCTION_3(BaseScopedConsoleHandler::onMessage));
	}
}

RestHelpers::BaseScopedConsoleHandler::~BaseScopedConsoleHandler()
{
	if (capturing)
		getMainController()->getConsoleHandler().setCustomCodeHandler({});
}

void RestHelpers::BaseScopedConsoleHandler::onMessage(const String& message, int warning, const Processor* p)
{
	if (warning == 0)
	{
		handleMessage(message);
	}
	else
	{
		auto lines = StringArray::fromLines(message);
		
		auto scriptRoot = getMainController()->getSampleManager().getProjectHandler()
			.getSubDirectory(FileHandlerBase::Scripts);
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
		
		handleError(error.message, callstack);
	}
}

String RestHelpers::BaseScopedConsoleHandler::ParsedError::toCallstackString() const
{
	if (functionName.isEmpty())
		return location;
	return functionName + "() at " + location;
}

RestHelpers::BaseScopedConsoleHandler::ParsedError RestHelpers::BaseScopedConsoleHandler::parseError(
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
// ScopedConsoleHandler implementation

RestHelpers::ScopedConsoleHandler::ScopedConsoleHandler(MainController* mc, RestServer::AsyncRequest::Ptr request_) :
	BaseScopedConsoleHandler(mc, true),  // Always enabled for REST
	request(*request_)
{
}

void RestHelpers::ScopedConsoleHandler::handleMessage(const String& message)
{
	request.appendLog(message);
}

void RestHelpers::ScopedConsoleHandler::handleError(const String& message, const StringArray& callstack)
{
	request.appendError(message, callstack);
}

//==============================================================================
// ReplayConsoleHandler implementation

RestHelpers::ReplayConsoleHandler::ReplayConsoleHandler(MainController* mc, InteractionTestWindow* window_, bool enabled) :
	BaseScopedConsoleHandler(mc, enabled),
	window(window_)
{
}

void RestHelpers::ReplayConsoleHandler::handleMessage(const String& message)
{
	logs.add(message);
}

void RestHelpers::ReplayConsoleHandler::handleError(const String& message, const StringArray& callstack)
{
	DynamicObject::Ptr errorObj = new DynamicObject();
	errorObj->setProperty("message", message);
	
	Array<var> callstackArray;
	for (auto& entry : callstack)
		callstackArray.add(entry);
	errorObj->setProperty("callstack", var(callstackArray));
	
	errors.add(var(errorObj.get()));
}

void RestHelpers::ReplayConsoleHandler::finalize(const var& inputInteractions, bool success,
                                                  int interactionsCompleted, int totalElapsedMs)
{
	if (window == nullptr) return;
	
	// Build JSON response similar to REST API format
	DynamicObject::Ptr result = new DynamicObject();
	result->setProperty(RestApiIds::success, success && errors.isEmpty());
	result->setProperty(RestApiIds::interactionsCompleted, interactionsCompleted);
	result->setProperty(RestApiIds::totalElapsedMs, totalElapsedMs);
	
	Array<var> logsArray;
	for (auto& log : logs)
		logsArray.add(log);
	result->setProperty(RestApiIds::logs, var(logsArray));
	
	if (!errors.isEmpty())
		result->setProperty(RestApiIds::errors, var(errors));
	
	String jsonResponse = JSON::toString(var(result.get()), false);
	
	// Log to StatusBar
	if (auto* tester = window->getInteractionTester())
		tester->logResponse(inputInteractions, jsonResponse, success && errors.isEmpty());
}

//==============================================================================
// Static helper methods

JavascriptProcessor* RestHelpers::getScriptProcessor(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	String moduleId;
	
	if (req->getRequest().method == RestServer::POST)
	{
		// For POST requests, read moduleId from JSON body
		auto body = req->getRequest().getJsonBody();
		moduleId = body[RestApiIds::moduleId].toString();
	}
	else
	{
		// For GET requests, read moduleId from query parameters
		moduleId = req->getRequest()[RestApiIds::moduleId];
	}

	if (moduleId.isEmpty())
		return nullptr;

	return dynamic_cast<JavascriptProcessor*>(ProcessorHelpers::getFirstProcessorWithName(mc->getMainSynthChain(), moduleId));
}

void RestHelpers::waitForPendingCallbacks(ScriptComponent* sc, int timeoutMs)
{
	const int intervalMs = 5;
	int waited = 0;
	
	while (sc->isControlCallbackPending() && waited < timeoutMs)
	{
		Thread::sleep(intervalMs);
		waited += intervalMs;
	}
}

//==============================================================================
// LAF (LookAndFeel) integration helpers

/** Converts LafRegistry::LafInfo::RenderStyle enum to JSON string. */
static String renderStyleToString(ScriptingApi::Content::LafRegistry::LafInfo::RenderStyle style)
{
	using RenderStyle = ScriptingApi::Content::LafRegistry::LafInfo::RenderStyle;
	
	switch (style)
	{
		case RenderStyle::Script:    return "script";
		case RenderStyle::Css:       return "css";
		case RenderStyle::CssInline: return "css_inline";
		case RenderStyle::Mixed:     return "mixed";
		default:                     return "unknown";
	}
}

/** Decodes a Base64-encoded location string to file:line:col format.
    Input: "{{Base64(processorId|path|charIndex|line|col)}}"
    Output: "path:line:col" or "moduleId.js:line:col" for inline callbacks */
static String decodeLocationString(const String& encodedLocation, 
                                    const File& scriptRoot, 
                                    const String& moduleId)
{
	String encoded = encodedLocation.fromFirstOccurrenceOf("{{", false, false)
	                                 .upToFirstOccurrenceOf("}}", false, false);
	
	if (encoded.isEmpty())
		return encodedLocation;  // Return as-is if not encoded
	
	MemoryOutputStream mos;
	if (!Base64::convertFromBase64(mos, encoded))
		return encodedLocation;
	
	String decoded(static_cast<const char*>(mos.getData()), mos.getDataSize());
	StringArray parts = StringArray::fromTokens(decoded, "|", "");
	
	if (parts.size() < 5)
		return encodedLocation;
	
	// Format: "processorId|relativePath|charIndex|line|col"
	String path = parts[1];
	int line = parts[3].getIntValue();
	int col = parts[4].getIntValue();
	
	String fullPath;
	if (path.isEmpty() || path.contains("()"))
	{
		fullPath = moduleId + ".js";
	}
	else
	{
		File f = scriptRoot.getChildFile(path);
		if (f.existsAsFile())
			fullPath = f.getRelativePathFrom(scriptRoot.getParentDirectory()).replaceCharacter('\\', '/');
		else
			fullPath = path;
	}
	
	return fullPath + ":" + String(line) + ":" + String(col);
}

/** Builds LAF info object for a component, or returns false if no LAF assigned.
    Computes location lazily - by this point watched files are populated. */
static var getLafInfoForComponent(ScriptingApi::Content::LafRegistry* registry, 
                                   ScriptComponent* sc,
                                   const File& scriptRoot,
                                   const String& moduleId)
{
	if (registry == nullptr || sc == nullptr)
		return var(false);
	
	if (auto lafInfo = registry->getLafInfoForComponent(sc->getName()))
	{
		DynamicObject::Ptr laf = new DynamicObject();
		laf->setProperty(RestApiIds::id, lafInfo->variableName);
		laf->setProperty(RestApiIds::renderStyle, renderStyleToString(lafInfo->renderStyle));
		
		// Compute location lazily - watched files are now populated
		String locationStr;
		if (auto jp = dynamic_cast<JavascriptProcessor*>(sc->getScriptProcessor()))
		{
			if (auto codeDoc = jp->getSnippet(lafInfo->location))
			{
				CodeDocument::Position pos(*codeDoc, lafInfo->location.charNumber);
				auto lineNumber = pos.getLineNumber() + 1;  // 1-based
				auto columnNumber = pos.getIndexInLine();
				
				// Build location string directly (same format as error callstacks)
				auto fileName = lafInfo->location.fileName;
				String filePath;
				
				if (fileName.isEmpty() || fileName == "onInit" || fileName.contains("()"))
				{
					// Inline callback - use moduleId.js
					filePath = moduleId + ".js";
				}
				else
				{
					// External file - use relative path from Scripts folder
					filePath = "Scripts/" + fileName;
				}
				
				locationStr = filePath + ":" + String(lineNumber) + ":" + String(columnNumber);
			}
			else
			{
				// Fallback if document not found
				locationStr = lafInfo->location.fileName.isNotEmpty() ? lafInfo->location.fileName : "onInit";
			}
		}
		else
		{
			// Fallback if no JavascriptProcessor
			locationStr = lafInfo->location.fileName.isNotEmpty() ? lafInfo->location.fileName : "onInit";
		}
		
		laf->setProperty(RestApiIds::location, locationStr);
		laf->setProperty(RestApiIds::cssLocation, lafInfo->cssLocation);
		return var(laf.get());
	}
	
	return var(false);
}

/** Checks for script-based LAF recipients and waits for them to render.
    If timeout occurs, adds lafRenderWarning to the result object.
    Skipped on test port (1901) to avoid test delays. */
static void addLafRenderWarningIfNeeded(MainController* mc, 
                                         ScriptingApi::Content* content, 
                                         DynamicObject* result)
{
	// Skip on test port to avoid test delays
	if (auto bp = dynamic_cast<BackendProcessor*>(mc))
	{
		if (bp->getRestServer().isTestMode())
			return;
	}
	
	auto registry = content->getLafRegistry();
	if (registry == nullptr || !registry->hasScriptBasedRecipients())
		return;
	
	// Poll for render completion - pump message loop to allow UI thread to paint
	constexpr int timeoutMs = 1000;
	constexpr int pollIntervalMs = 50;
	int elapsed = 0;
	
	while (elapsed < timeoutMs && !registry->allRecipientsRendered())
	{
		// Pump the message loop to allow UI thread to process paint events
		if (auto* mm = MessageManager::getInstanceWithoutCreating())
			mm->runDispatchLoopUntil(pollIntervalMs);
		else
			Thread::sleep(pollIntervalMs);
		
		elapsed += pollIntervalMs;
	}
	
	auto unrendered = registry->getUnrenderedComponents();
	
	if (!unrendered.isEmpty())
	{
		DynamicObject::Ptr warning = new DynamicObject();
		warning->setProperty(RestApiIds::errorMessage, 
			"Some LAF components did not render");
		
		Array<var> componentList;
		for (const auto& info : unrendered)
		{
			DynamicObject::Ptr comp = new DynamicObject();
			comp->setProperty(RestApiIds::id, info.id.toString());
			comp->setProperty(RestApiIds::reason, info.isInvisible ? "invisible" : "timeout");
			componentList.add(var(comp.get()));
		}
		warning->setProperty(RestApiIds::unrenderedComponents, componentList);
		warning->setProperty(RestApiIds::timeoutMs, timeoutMs);
		
		result->setProperty(RestApiIds::lafRenderWarning, var(warning.get()));
	}
}

RestServer::Response RestHelpers::handleRecompile(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	auto obj = req->getRequest().getJsonBody();
	bool forceSync = (bool)obj.getProperty(RestApiIds::forceSynchronousExecution, false);
	
	// Create ScopedBadBabysitter if forceSynchronousExecution is requested
	// This bypasses all threading checks and executes everything synchronously
	std::unique_ptr<MainController::ScopedBadBabysitter> syncMode;
	if (forceSync)
		syncMode = std::make_unique<MainController::ScopedBadBabysitter>(mc);

	if (auto jp = getScriptProcessor(mc, req))
	{
		mc->getKillStateHandler().killVoicesAndCall(dynamic_cast<Processor*>(jp), [req, forceSync, mc](Processor* p)
		{
			JavascriptProcessor::ResultFunction rf = [req, forceSync, mc, p](const JavascriptProcessor::SnippetResult& result)
			{
				DynamicObject::Ptr r = new DynamicObject();
				r->setProperty(RestApiIds::success, result.r.wasOk());
				r->setProperty(RestApiIds::result, result.r.wasOk() ? "Recompiled OK" : "Compilation / Runtime Error");
				r->setProperty(RestApiIds::forceSynchronousExecution, forceSync);
				
				if (forceSync)
					r->setProperty(RestApiIds::warning, "Executed in unsafe synchronous mode - threading checks bypassed");
				
				// Check for LAF render warnings on successful compilation
				if (result.r.wasOk())
				{
					if (auto ps = dynamic_cast<ProcessorWithScriptingContent*>(p))
					{
						if (auto content = ps->getScriptingContent())
							addLafRenderWarningIfNeeded(mc, content, r.get());
					}
				}

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

/** Internal recursive helper that includes LAF info when registry is provided. */
static DynamicObject::Ptr createRecursivePropertyTreeWithLaf(ScriptComponent* sc, 
                                                              ScriptingApi::Content::LafRegistry* registry,
                                                              const File& scriptRoot,
                                                              const String& moduleId)
{
	DynamicObject::Ptr obj = new DynamicObject();

	obj->setProperty(RestApiIds::id, sc->getName().toString());
	obj->setProperty(RestApiIds::type, sc->getObjectName().toString());
	obj->setProperty(RestApiIds::visible, sc->getScriptObjectProperty(ScriptComponent::visible));
	obj->setProperty(RestApiIds::enabled, sc->getScriptObjectProperty(ScriptComponent::enabled));
	obj->setProperty(RestApiIds::x, sc->getScriptObjectProperty(ScriptComponent::x));
	obj->setProperty(RestApiIds::y, sc->getScriptObjectProperty(ScriptComponent::y));
	obj->setProperty(RestApiIds::width, sc->getScriptObjectProperty(ScriptComponent::width));
	obj->setProperty(RestApiIds::height, sc->getScriptObjectProperty(ScriptComponent::height));
	
	// Add LAF info
	obj->setProperty(RestApiIds::laf, getLafInfoForComponent(registry, sc, scriptRoot, moduleId));

	Array<var> children;

	ScriptComponent::ChildIterator<ScriptComponent> ci(sc);

	auto v = sc->getPropertyValueTree();

	for (auto c : v)
	{
		if (auto child = sc->getScriptProcessor()->getScriptingContent()->getComponentWithName(c[RestApiIds::id].toString()))
		{
			auto cobj = createRecursivePropertyTreeWithLaf(child, registry, scriptRoot, moduleId);
			children.add(var(cobj.get()));
		}
	}

	obj->setProperty(RestApiIds::childComponents, var(children));

	return obj;
}

DynamicObject::Ptr RestHelpers::createRecursivePropertyTree(ScriptComponent* sc)
{
	// Public API without LAF info (for backward compatibility)
	// Pass empty File and String since LAF info won't be used with nullptr registry
	return createRecursivePropertyTreeWithLaf(sc, nullptr, File(), String());
}

//==============================================================================
// Route metadata registry

const Array<RestHelpers::RouteMetadata>& RestHelpers::getRouteMetadata()
{
	static Array<RouteMetadata> metadata = []()
	{
		Array<RouteMetadata> m;
		
		// The order MUST match ApiRoute enum!
		
		// ApiRoute::ListMethods
		m.add(RouteMetadata(ApiRoute::ListMethods, "")
			.withCategory("status")
			.withDescription("List all available API methods with parameters and descriptions")
			.withReturns("Array of method definitions with path, parameters, and documentation"));
		
		// ApiRoute::Status
		m.add(RouteMetadata(ApiRoute::Status, "api/status")
			.withCategory("status")
			.withDescription("Get project status and discover available script processors")
			.withReturns("Server info (version, compileTimeout in seconds), project info, scriptsFolder path, and scriptProcessors with their callbacks"));
		
		// ApiRoute::GetScript
		m.add(RouteMetadata(ApiRoute::GetScript, "api/get_script")
			.withCategory("scripting")
			.withDescription("Read script content from a processor's callbacks")
			.withReturns("Callbacks object with script content for requested callback(s). Includes externalFiles array with name and full path.")
			.withModuleIdParam()
			.withQueryParam(RouteParameter(RestApiIds::callback, "Specific callback name (e.g., onInit). If omitted, returns all callbacks.").asOptional()));
		
		// ApiRoute::SetScript
		m.add(RouteMetadata(ApiRoute::SetScript, "api/set_script")
			.withMethod(RestServer::POST)
			.withCategory("scripting")
			.withDescription("Update one or more callbacks and optionally compile. Only specified callbacks are updated; others remain unchanged.")
			.withReturns("Compilation result with updatedCallbacks array, success status, logs, errors, and optional lafRenderWarning listing unrendered LAF components with reason (invisible or timeout)")
			.withBodyParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID"))
			.withBodyParam(RouteParameter(RestApiIds::callbacks, "Object with callback names as keys and script content as values (e.g., {onInit: \"...\", onNoteOn: \"...\"})"))
			.withBodyParam(RouteParameter(RestApiIds::compile, "Whether to compile after setting").withDefault("true"))
			.withBodyParam(RouteParameter(RestApiIds::forceSynchronousExecution, "Debug tool: Bypass threading model for synchronous execution. WARNING: May cause crashes due to race conditions - use only as last resort after saving.").withDefault("false")));
		
		// ApiRoute::Recompile
		m.add(RouteMetadata(ApiRoute::Recompile, "api/recompile")
			.withMethod(RestServer::POST)
			.withCategory("scripting")
			.withDescription("Recompile a processor (restores preset values, triggering callbacks for saveInPreset components)")
			.withReturns("Compilation result with success status, logs, errors, and optional lafRenderWarning listing unrendered LAF components with reason (invisible or timeout)")
			.withBodyParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID"))
			.withBodyParam(RouteParameter(RestApiIds::forceSynchronousExecution, "Debug tool: Bypass threading model for synchronous execution. WARNING: May cause crashes due to race conditions - use only as last resort after saving.").withDefault("false")));
		
		// ApiRoute::ListComponents
		m.add(RouteMetadata(ApiRoute::ListComponents, "api/list_components")
			.withCategory("ui")
			.withDescription("List all UI components in a script processor")
			.withReturns("Array of components with id, type, and laf info (flat list or hierarchical tree with layout properties)")
			.withModuleIdParam()
			.withQueryParam(RouteParameter(RestApiIds::hierarchy, "If true, returns nested tree with layout properties").withDefault("false")));
		
		// ApiRoute::GetComponentProperties
		m.add(RouteMetadata(ApiRoute::GetComponentProperties, "api/get_component_properties")
			.withCategory("ui")
			.withDescription("Get all properties for a specific UI component")
			.withReturns("Component type and array of properties with id, value, isDefault, and options")
			.withModuleIdParam()
			.withQueryParam(RouteParameter(RestApiIds::id, "The component's ID (e.g., Button1, Panel1)")));
		
		// ApiRoute::GetComponentValue
		m.add(RouteMetadata(ApiRoute::GetComponentValue, "api/get_component_value")
			.withCategory("ui")
			.withDescription("Get the current runtime value of a UI component")
			.withReturns("Component type, current value, and min/max range")
			.withModuleIdParam()
			.withQueryParam(RouteParameter(RestApiIds::id, "The component's ID (e.g., GainKnob, BypassButton)")));
		
		// ApiRoute::SetComponentValue
		m.add(RouteMetadata(ApiRoute::SetComponentValue, "api/set_component_value")
			.withMethod(RestServer::POST)
			.withCategory("ui")
			.withDescription("Set the runtime value of a UI component (triggers control callback)")
			.withReturns("Success status")
			.withBodyParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID"))
			.withBodyParam(RouteParameter(RestApiIds::id, "The component's ID"))
			.withBodyParam(RouteParameter(RestApiIds::value, "The value to set"))
			.withBodyParam(RouteParameter(RestApiIds::validateRange, "If true, validates value is within component's min/max range").withDefault("false"))
			.withBodyParam(RouteParameter(RestApiIds::forceSynchronousExecution, "Debug tool: Bypass threading model for synchronous execution. WARNING: May cause crashes due to race conditions - use only as last resort after saving.").withDefault("false")));
		
		// ApiRoute::SetComponentProperties
		m.add(RouteMetadata(ApiRoute::SetComponentProperties, "api/set_component_properties")
			.withMethod(RestServer::POST)
			.withCategory("ui")
			.withDescription("Set properties on one or more UI components (like Interface Designer)")
			.withReturns("Success with applied changes (recompileRequired=true if parentComponent changed), or error with locked properties if any are script-controlled")
			.withBodyParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID"))
			.withBodyParam(RouteParameter(RestApiIds::changes, "Array of {id, properties: {...}} objects"))
			.withBodyParam(RouteParameter(RestApiIds::force, "If true, bypasses script-lock check and sets all properties").withDefault("false")));
		
		// ApiRoute::Screenshot
		m.add(RouteMetadata(ApiRoute::Screenshot, "api/screenshot")
			.withCategory("ui")
			.withDescription("Capture screenshot of interface or specific component")
			.withReturns("Base64-encoded PNG image data with dimensions, or file path if outputPath is specified")
			.withQueryParam(RouteParameter(RestApiIds::moduleId, "Script processor ID").withDefault("Interface"))
			.withQueryParam(RouteParameter(RestApiIds::id, "Component ID to capture (omit for full interface)").asOptional())
			.withQueryParam(RouteParameter(RestApiIds::scale, "Scale factor (0.5 or 1.0)").withDefault("1.0"))
			.withQueryParam(RouteParameter(RestApiIds::outputPath, "File path to save PNG (must end with .png). If provided, writes to file instead of returning Base64").asOptional()));
		
		// ApiRoute::GetSelectedComponents
		m.add(RouteMetadata(ApiRoute::GetSelectedComponents, "api/get_selected_components")
			.withCategory("ui")
			.withDescription("Get the currently selected UI components from the Interface Designer")
			.withReturns("Selection count and array of selected components with all properties")
			.withQueryParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID").withDefault("Interface")));
		
		// ApiRoute::SimulateInteractions
		m.add(RouteMetadata(ApiRoute::SimulateInteractions, "api/simulate_interactions")
			.withMethod(RestServer::POST)
			.withCategory("testing")
			.withDescription("Execute a sequence of UI interactions in a test window. Auto-inserts moveTo events as needed for proper mouse positioning.")
			.withReturns("Execution result with success status, completion count, timing, execution log, captured screenshots, and optionally mouseState when verbose=true")
			.withBodyParam(RouteParameter(RestApiIds::interactions, 
				"Array of interaction objects. Types: 'moveTo' (explicit mouse positioning), 'click' (uses current position), "
				"'doubleClick' (expands to two clicks), 'drag' (uses pixel 'delta'), 'selectMenuItem' (click menu item by text), "
				"'screenshot' (capture interface). Fields: 'target' (component ID), 'delay' (ms before action), "
				"'normalizedPosition' (0-1, default center), 'pixelPosition' (absolute, takes precedence), 'delta' (for drag), "
				"'subtarget' (optional sub-component ID), 'rightClick' (boolean), 'shiftDown'/'ctrlDown'/'altDown'/'cmdDown' (modifiers), "
				"'menuItemText' (for selectMenuItem), 'id'/'scale' (for screenshot)"))
			.withBodyParam(RouteParameter(RestApiIds::verbose, "If true, include auto-insertion details and final mouseState in response").withDefault("false")));
		
		// Verify count matches enum
		jassert(m.size() == (int)ApiRoute::numRoutes);
		
		return m;
	}();
	
	return metadata;
}

String RestHelpers::getRoutePath(RestHelpers::ApiRoute route)
{
	return getRouteMetadata()[(int)route].path;
}

RestHelpers::ApiRoute RestHelpers::findRoute(const String& subURL)
{
	const auto& metadata = getRouteMetadata();
	for (int i = 0; i < metadata.size(); i++)
	{
		if (metadata[i].path == subURL)
			return (ApiRoute)i;
	}
	return ApiRoute::numRoutes;
}

//==============================================================================
// Route handlers

RestServer::Response RestHelpers::handleListMethods(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	ignoreUnused(mc);
	
	const auto& metadata = getRouteMetadata();
	
	// Sort by category, then alphabetically by path
	Array<int> sortedIndices;
	for (int i = 0; i < metadata.size(); i++)
		sortedIndices.add(i);
	
	// JUCE ElementComparator for sorting indices by category then path
	struct RouteIndexComparator
	{
		const Array<RouteMetadata>& routes;
		
		RouteIndexComparator(const Array<RouteMetadata>& r) : routes(r) {}
		
		int compareElements(int a, int b) const
		{
			const auto& ra = routes[a];
			const auto& rb = routes[b];
			
			int catCompare = ra.category.compare(rb.category);
			if (catCompare != 0)
				return catCompare;
			
			return ra.path.compare(rb.path);
		}
	};
	
	RouteIndexComparator comparator(metadata);
	sortedIndices.sort(comparator);
	
	// Build response
	DynamicObject::Ptr result = new DynamicObject();
	result->setProperty(RestApiIds::success, true);
	
	Array<var> methods;
	for (int idx : sortedIndices)
	{
		const auto& route = metadata[idx];
		
		DynamicObject::Ptr m = new DynamicObject();
		m->setProperty(RestApiIds::path, "/" + route.path);
		m->setProperty(RestApiIds::method, route.method == RestServer::GET ? "GET" : "POST");
		m->setProperty(RestApiIds::category, route.category);
		m->setProperty(RestApiIds::description, route.description);
		m->setProperty(RestApiIds::returns, route.returns);
		
		// Query parameters (for GET)
		if (!route.queryParameters.isEmpty())
		{
			Array<var> params;
			for (const auto& p : route.queryParameters)
			{
				DynamicObject::Ptr param = new DynamicObject();
				param->setProperty(RestApiIds::name, p.name.toString());
				param->setProperty(RestApiIds::description, p.description);
				param->setProperty(RestApiIds::required, p.required);
				if (p.defaultValue.isNotEmpty())
					param->setProperty(RestApiIds::defaultValue, p.defaultValue);
				params.add(param.get());
			}
			m->setProperty(RestApiIds::queryParameters, params);
		}
		
		// Body parameters (for POST)
		if (!route.bodyParameters.isEmpty())
		{
			Array<var> params;
			for (const auto& p : route.bodyParameters)
			{
				DynamicObject::Ptr param = new DynamicObject();
				param->setProperty(RestApiIds::name, p.name.toString());
				param->setProperty(RestApiIds::description, p.description);
				param->setProperty(RestApiIds::required, p.required);
				if (p.defaultValue.isNotEmpty())
					param->setProperty(RestApiIds::defaultValue, p.defaultValue);
				params.add(param.get());
			}
			m->setProperty(RestApiIds::bodyParameters, params);
		}
		
		methods.add(m.get());
	}
	
	result->setProperty(RestApiIds::methods, methods);
	result->setProperty(RestApiIds::logs, Array<var>());
	result->setProperty(RestApiIds::errors, Array<var>());
	
	req->complete(RestServer::Response::ok(var(result.get())));
	return req->waitForResponse();
}

RestServer::Response RestHelpers::handleStatus(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	DynamicObject::Ptr result = new DynamicObject();
	result->setProperty(RestApiIds::success, true);

	// Server info
	DynamicObject::Ptr server = new DynamicObject();
	server->setProperty(RestApiIds::version, PresetHandler::getVersionString());
	server->setProperty(RestApiIds::compileTimeout, GET_HISE_SETTING(mc->getMainSynthChain(), HiseSettings::Scripting::CompileTimeout));
	result->setProperty(RestApiIds::server, var(server.get()));

	// Project info
	DynamicObject::Ptr project = new DynamicObject();
	project->setProperty(RestApiIds::name, GET_HISE_SETTING(mc->getMainSynthChain(), HiseSettings::Project::Name));

	auto& projectHandler = mc->getSampleManager().getProjectHandler();
	auto projectFolder = projectHandler.getWorkDirectory().getFullPathName().replace("\\", "/");
	auto scriptsFolder = projectHandler.getSubDirectory(FileHandlerBase::Scripts).getFullPathName().replace("\\", "/");

	project->setProperty(RestApiIds::projectFolder, projectFolder);
	project->setProperty(RestApiIds::scriptsFolder, scriptsFolder);
	result->setProperty(RestApiIds::project, var(project.get()));

	// Script processors
	Array<var> processors;
	Processor::Iterator<JavascriptProcessor> iter(mc->getMainSynthChain());

	while (auto jp = iter.getNextProcessor())
	{
		DynamicObject::Ptr proc = new DynamicObject();
		proc->setProperty(RestApiIds::moduleId, dynamic_cast<Processor*>(jp)->getId());
	
		if (auto jmp = dynamic_cast<JavascriptMidiProcessor*>(jp))
			proc->setProperty(RestApiIds::isMainInterface, jmp->isFront() ? true : false);
		else
			proc->setProperty(RestApiIds::isMainInterface, false);
		
		// External files
		Array<var> externalFilesArr;
		for (int i = 0; i < jp->getNumWatchedFiles(); i++)
		{
			auto fp = jp->getWatchedFile(i);
			auto rp = fp.getRelativePathFrom(scriptsFolder).replaceCharacter('\\', '/');
			externalFilesArr.add(rp);
		}
		proc->setProperty(RestApiIds::externalFiles, externalFilesArr);
	
		Array<var> callbacksArr;
		for (int i = 0; i < jp->getNumSnippets(); i++)
		{
			if (auto snippet = jp->getSnippet(i))
			{
				DynamicObject::Ptr cb = new DynamicObject();
				cb->setProperty(RestApiIds::id, snippet->getCallbackName().toString());
				cb->setProperty(RestApiIds::empty, snippet->isSnippetEmpty());
				callbacksArr.add(var(cb.get()));
			}
		}
		proc->setProperty(RestApiIds::callbacks, callbacksArr);
		processors.add(var(proc.get()));
	}

	result->setProperty(RestApiIds::scriptProcessors, processors);
	result->setProperty(RestApiIds::logs, Array<var>());
	result->setProperty(RestApiIds::errors, Array<var>());

	req->complete(RestServer::Response::ok(var(result.get())));
	return req->waitForResponse();
}

RestServer::Response RestHelpers::handleGetScript(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	if (auto jp = getScriptProcessor(mc, req))
	{
		auto callbackParam = req->getRequest()[RestApiIds::callback];
		auto processor = dynamic_cast<Processor*>(jp);
		
		// Build externalFiles array (common to all response types)
		Array<var> externalFilesArr;
		for (int i = 0; i < jp->getNumWatchedFiles(); i++)
		{
			auto fp = jp->getWatchedFile(i);
			DynamicObject::Ptr fileObj = new DynamicObject();
			fileObj->setProperty(RestApiIds::name, fp.getFileName());
			fileObj->setProperty(RestApiIds::path, fp.getFullPathName().replace("\\", "/"));
			externalFilesArr.add(var(fileObj.get()));
		}

		DynamicObject::Ptr result = new DynamicObject();
		result->setProperty(RestApiIds::success, true);
		result->setProperty(RestApiIds::moduleId, processor->getId());

		// Always use callbacks object for consistent response structure
		DynamicObject::Ptr callbacksObj = new DynamicObject();

		if (callbackParam.isNotEmpty())
		{
			// Single callback mode - return only the requested callback
			if (auto cb = jp->getSnippet(Identifier(callbackParam)))
			{
				// onInit is raw content, others include function wrapper
				if (callbackParam == "onInit")
					callbacksObj->setProperty(callbackParam, cb->getAllContent());
				else
					callbacksObj->setProperty(callbackParam, cb->getSnippetAsFunction());
			}
			else
				return req->fail(404, "callback " + callbackParam + " not found");
		}
		else
		{
			// All callbacks mode - return all callbacks
			for (int i = 0; i < jp->getNumSnippets(); i++)
			{
				if (auto snippet = jp->getSnippet(i))
				{
					auto cbName = snippet->getCallbackName().toString();
					
					// onInit is raw content, others include function wrapper
					if (cbName == "onInit")
						callbacksObj->setProperty(cbName, snippet->getAllContent());
					else
						callbacksObj->setProperty(cbName, snippet->getSnippetAsFunction());
				}
			}
		}

		result->setProperty(RestApiIds::callbacks, var(callbacksObj.get()));
		result->setProperty(RestApiIds::externalFiles, var(externalFilesArr));
		result->setProperty(RestApiIds::logs, Array<var>());
		result->setProperty(RestApiIds::errors, Array<var>());

		req->complete(RestServer::Response::ok(var(result.get())));
		return req->waitForResponse();
	}
	
	return req->fail(404, "moduleId is not a valid script processor");
}

RestServer::Response RestHelpers::handleSetScript(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	auto obj = req->getRequest().getJsonBody();
	
	// Check for forceSynchronousExecution debug mode
	bool forceSync = (bool)obj.getProperty(RestApiIds::forceSynchronousExecution, false);
	
	std::unique_ptr<MainController::ScopedBadBabysitter> syncMode;
	if (forceSync)
		syncMode = std::make_unique<MainController::ScopedBadBabysitter>(mc);
	
	// Read moduleId from JSON body
	auto moduleId = obj[RestApiIds::moduleId].toString();
	
	// Helper for structured validation errors (consistent with compilation errors)
	auto validationError = [&](int statusCode, const String& message) -> RestServer::Response
	{
		DynamicObject::Ptr result = new DynamicObject();
		result->setProperty(RestApiIds::success, false);
		result->setProperty(RestApiIds::moduleId, moduleId);
		// Note: logs and errors are merged by AsyncRequest::mergeLogsIntoResponse()
		
		RestServer::Response response;
		response.statusCode = statusCode;
		response.contentType = "application/json";
		response.body = JSON::toString(var(result.get()));
		
		// Add error via appendError so it gets properly merged
		req->appendError(message);
		req->complete(response);
		return req->waitForResponse();
	};
	
	if (moduleId.isEmpty())
		return validationError(400, "moduleId is required in request body");
	
	auto jp = dynamic_cast<JavascriptProcessor*>(
		ProcessorHelpers::getFirstProcessorWithName(mc->getMainSynthChain(), moduleId));
	
	if (jp == nullptr)
		return validationError(404, "module not found: " + moduleId);
	
	// Read callbacks object
	auto callbacksVar = obj[RestApiIds::callbacks];
	if (!callbacksVar.getDynamicObject())
		return validationError(400, "callbacks must be an object");
	
	auto* callbacksObj = callbacksVar.getDynamicObject();
	auto& props = callbacksObj->getProperties();
	
	if (props.isEmpty())
		return validationError(400, "callbacks object cannot be empty");
	
	Array<var> updatedCallbackNames;
	
	// Validate all callback names first (reject unknown callbacks)
	for (auto& nv: props)
	{
		auto cbName = nv.name.toString();
		auto content = nv.value.toString();

		if (auto snippet = jp->getSnippet(Identifier(cbName)))
		{
			snippet->replaceContentAsync(content);
			updatedCallbackNames.add(cbName);
		}
		else
			return validationError(400, "unknown callback: " + cbName);
	}
	
	// Check if compile is requested (default true)
	if (obj.getProperty(RestApiIds::compile, true))
	{
		// Recompile and return result with updatedCallbacks in response
		mc->getKillStateHandler().killVoicesAndCall(dynamic_cast<Processor*>(jp), 
			[req, forceSync, updatedCallbackNames, moduleId, mc](Processor* p)
		{
			JavascriptProcessor::ResultFunction rf = 
				[req, forceSync, updatedCallbackNames, moduleId, mc, p](const JavascriptProcessor::SnippetResult& result)
			{
				DynamicObject::Ptr r = new DynamicObject();
				r->setProperty(RestApiIds::success, result.r.wasOk());
				r->setProperty(RestApiIds::moduleId, moduleId);
				r->setProperty(RestApiIds::updatedCallbacks, var(updatedCallbackNames));
				r->setProperty(RestApiIds::result, result.r.wasOk() ? "Compiled OK" : "Compilation / Runtime Error");
				r->setProperty(RestApiIds::forceSynchronousExecution, forceSync);
				
				if (forceSync)
					r->setProperty(RestApiIds::warning, "Executed in unsafe synchronous mode - threading checks bypassed");
				
				// Check for LAF render warnings on successful compilation
				if (result.r.wasOk())
				{
					if (auto ps = dynamic_cast<ProcessorWithScriptingContent*>(p))
					{
						if (auto content = ps->getScriptingContent())
							addLafRenderWarningIfNeeded(mc, content, r.get());
					}
				}

				req->complete(RestServer::Response::ok(var(r.get())));
			};

			dynamic_cast<JavascriptProcessor*>(p)->compileScript(rf);
			return SafeFunctionCall::OK;
		}, MainController::KillStateHandler::TargetThread::ScriptingThread);
		
		return req->waitForResponse();
	}

	// No compile - return success immediately
	DynamicObject::Ptr result = new DynamicObject();
	result->setProperty(RestApiIds::success, true);
	result->setProperty(RestApiIds::moduleId, moduleId);
	result->setProperty(RestApiIds::updatedCallbacks, var(updatedCallbackNames));
	result->setProperty(RestApiIds::result, "Updated without compilation");
	result->setProperty(RestApiIds::logs, Array<var>());
	result->setProperty(RestApiIds::errors, Array<var>());
	
	req->complete(RestServer::Response::ok(var(result.get())));
	return req->waitForResponse();
}

RestServer::Response RestHelpers::handleListComponents(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	if (auto jp = getScriptProcessor(mc, req))
	{
		auto ps = dynamic_cast<ProcessorWithScriptingContent*>(jp);
		auto hierarchyParam = req->getRequest()[RestApiIds::hierarchy];
		auto useHierarchy = (hierarchyParam == "true" || hierarchyParam == "1");

		auto p = dynamic_cast<Processor*>(jp);
		auto moduleId = p->getId();
		auto scriptRoot = mc->getSampleManager().getProjectHandler().getSubDirectory(FileHandlerBase::Scripts);

		DynamicObject::Ptr result = new DynamicObject();
		result->setProperty(RestApiIds::success, true);
		result->setProperty(RestApiIds::moduleId, moduleId);

		Array<var> components;

		auto c = ps->getScriptingContent();
		auto registry = c->getLafRegistry();

		if (useHierarchy)
		{
			for (int i = 0; i < c->getNumComponents(); i++)
			{
				auto sc = c->getComponent(i);
				if (sc->getParentScriptComponent() == nullptr)
				{
					auto obj = createRecursivePropertyTreeWithLaf(sc, registry.get(), scriptRoot, moduleId);
					components.add(obj.get());
				}
			}
		}
		else
		{
			// Flat list: id, type, and laf info
			for(int i = 0; i < c->getNumComponents(); i++)
			{
				auto sc = c->getComponent(i);
				DynamicObject::Ptr comp = new DynamicObject();
				comp->setProperty(RestApiIds::id, sc->getName().toString());
				comp->setProperty(RestApiIds::type, sc->getObjectName().toString());
				comp->setProperty(RestApiIds::laf, getLafInfoForComponent(registry.get(), sc, scriptRoot, moduleId));
				components.add(var(comp.get()));
			}
		}

		result->setProperty(RestApiIds::components, components);
		result->setProperty(RestApiIds::logs, Array<var>());
		result->setProperty(RestApiIds::errors, Array<var>());

		req->complete(RestServer::Response::ok(var(result.get())));
		return req->waitForResponse();
	}
	else
	{
		return req->fail(404, "moduleId is not a valid script processor");
	}
}

RestServer::Response RestHelpers::handleGetComponentProperties(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	if (auto jp = getScriptProcessor(mc, req))
	{
		auto componentId = req->getRequest()[RestApiIds::id];
		auto c = dynamic_cast<ProcessorWithScriptingContent*>(jp)->getScriptingContent();

		if (componentId.isEmpty())
			return req->fail(400, "id parameter is required");

		if (auto sc = c->getComponentWithName(Identifier(componentId)))
		{
			DynamicObject::Ptr result = new DynamicObject();
			result->setProperty(RestApiIds::success, true);
			result->setProperty(RestApiIds::moduleId, dynamic_cast<Processor*>(jp)->getId());
			result->setProperty(RestApiIds::id, componentId);
			result->setProperty(RestApiIds::type, sc->getObjectName().toString());

			Array<var> properties;

			for (int i = 0; i < sc->getNumIds(); i++)
			{
				DynamicObject::Ptr po = new DynamicObject();

				auto propId = sc->getIdFor(i);
				auto v = sc->getScriptObjectProperty(i);
				auto nonDefault = sc->getNonDefaultScriptObjectProperties();

				if (propId.toString().toLowerCase().contains("colour"))
					v = "0x" + ApiHelpers::getColourFromVar(v).toDisplayString(true);

				auto opts = sc->getOptionsFor(propId);

				po->setProperty(RestApiIds::id, propId.toString());
				po->setProperty(RestApiIds::value, v);
				po->setProperty(RestApiIds::isDefault, !nonDefault.hasProperty(propId));

				if (!opts.isEmpty())
				{
					Array<var> ol;
					for (auto o : opts)
						ol.add(var(o));
					po->setProperty(RestApiIds::options, var(ol));
				}

				properties.add(po.get());
			}

			result->setProperty(RestApiIds::properties, var(properties));
			result->setProperty(RestApiIds::logs, Array<var>());
			result->setProperty(RestApiIds::errors, Array<var>());

			req->complete(RestServer::Response::ok(var(result.get())));
			return req->waitForResponse();
		}
		else
			return req->fail(404, "component not found: " + componentId);
	}
	else
	{
		return req->fail(404, "moduleId is not a valid script processor");
	}
}

RestServer::Response RestHelpers::handleGetComponentValue(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	if (auto jp = getScriptProcessor(mc, req))
	{
		auto componentId = req->getRequest()[RestApiIds::id];
		auto c = dynamic_cast<ProcessorWithScriptingContent*>(jp)->getScriptingContent();

		if (componentId.isEmpty())
			return req->fail(400, "id parameter is required");

		if (auto sc = c->getComponentWithName(Identifier(componentId)))
		{
			DynamicObject::Ptr result = new DynamicObject();
			result->setProperty(RestApiIds::success, true);
			result->setProperty(RestApiIds::moduleId, dynamic_cast<Processor*>(jp)->getId());
			result->setProperty(RestApiIds::id, componentId);
			result->setProperty(RestApiIds::type, sc->getObjectName().toString());
			
			result->setProperty(RestApiIds::value, sc->getValue());
			
			// Include min/max range
			result->setProperty(RestApiIds::min, sc->getScriptObjectProperty(ScriptComponent::min));
			result->setProperty(RestApiIds::max, sc->getScriptObjectProperty(ScriptComponent::max));
			
			result->setProperty(RestApiIds::logs, Array<var>());
			result->setProperty(RestApiIds::errors, Array<var>());

			req->complete(RestServer::Response::ok(var(result.get())));
			return req->waitForResponse();
		}
		else
			return req->fail(404, "component not found: " + componentId);
	}
	else
	{
		return req->fail(404, "moduleId is not a valid script processor");
	}
}

RestServer::Response RestHelpers::handleSetComponentValue(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	auto obj = req->getRequest().getJsonBody();
	
	// Check for forceSynchronousExecution debug mode
	bool forceSync = (bool)obj.getProperty(RestApiIds::forceSynchronousExecution, false);
	
	std::unique_ptr<MainController::ScopedBadBabysitter> syncMode;
	if (forceSync)
		syncMode = std::make_unique<MainController::ScopedBadBabysitter>(mc);
	
	auto moduleId = obj[RestApiIds::moduleId].toString();
	if (moduleId.isEmpty())
		return req->fail(400, "moduleId is required in request body");
	
	auto jp = dynamic_cast<JavascriptProcessor*>(
		ProcessorHelpers::getFirstProcessorWithName(mc->getMainSynthChain(), moduleId));
	
	if (jp == nullptr)
		return req->fail(404, "module not found: " + moduleId);
	
	auto componentId = obj[RestApiIds::id].toString();
	if (componentId.isEmpty())
		return req->fail(400, "id is required in request body");
	
	if (!obj.hasProperty(RestApiIds::value))
		return req->fail(400, "value is required in request body");
	
	auto c = dynamic_cast<ProcessorWithScriptingContent*>(jp)->getScriptingContent();
	
	if (auto sc = c->getComponentWithName(Identifier(componentId)))
	{
		auto newValue = obj[RestApiIds::value];
		auto validateRange = obj.getProperty(RestApiIds::validateRange, false);
		
		if ((bool)validateRange)
		{
			double minVal = (double)sc->getScriptObjectProperty(ScriptComponent::min);
			double maxVal = (double)sc->getScriptObjectProperty(ScriptComponent::max);
			double val = (double)newValue;
			
			if (val < minVal || val > maxVal)
				return req->fail(400, "Value " + String(val) + " is out of range [" + 
								 String(minVal) + ", " + String(maxVal) + "] for component " + componentId);
		}
		
		sc->setValue(newValue);
		sc->changed();
		
		// Wait for callback to complete (pumps message loop)
		waitForPendingCallbacks(sc);

		DynamicObject::Ptr result = new DynamicObject();
		result->setProperty(RestApiIds::success, true);
		result->setProperty(RestApiIds::moduleId, moduleId);
		result->setProperty(RestApiIds::id, componentId);
		result->setProperty(RestApiIds::type, sc->getObjectName().toString());
		result->setProperty(RestApiIds::forceSynchronousExecution, forceSync);
		
		if (forceSync)
			result->setProperty(RestApiIds::warning, "Executed in unsafe synchronous mode - threading checks bypassed");

		req->complete(RestServer::Response::ok(var(result.get())));
		return req->waitForResponse();
	}
	else
		return req->fail(404, "component not found: " + componentId);
}

RestServer::Response RestHelpers::handleSetComponentProperties(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	auto obj = req->getRequest().getJsonBody();
	
	// Get moduleId
	auto moduleId = obj[RestApiIds::moduleId].toString();
	if (moduleId.isEmpty())
		return req->fail(400, "moduleId is required in request body");
	
	auto jp = dynamic_cast<JavascriptProcessor*>(
		ProcessorHelpers::getFirstProcessorWithName(mc->getMainSynthChain(), moduleId));
	
	if (jp == nullptr)
		return req->fail(404, "module not found: " + moduleId);
	
	auto content = dynamic_cast<ProcessorWithScriptingContent*>(jp)->getScriptingContent();
	
	// Parse changes array
	auto changesVar = obj[RestApiIds::changes];
	if (!changesVar.isArray() || changesVar.size() == 0)
		return req->fail(400, "changes must be a non-empty array");
	
	bool force = (bool)obj.getProperty(RestApiIds::force, false);
	
	// Phase 1: Validation - collect all locked properties if force=false
	Array<var> lockedProperties;
	static const Identifier parentComponentId("parentComponent");
	
	for (int i = 0; i < changesVar.size(); i++)
	{
		auto change = changesVar[i];
		auto componentId = change[RestApiIds::id].toString();
		
		if (componentId.isEmpty())
			return req->fail(400, "changes[" + String(i) + "].id is required");
		
		auto sc = content->getComponentWithName(Identifier(componentId));
		if (sc == nullptr)
			return req->fail(404, "component not found: " + componentId);
		
		auto propsVar = change[RestApiIds::properties];
		if (!propsVar.getDynamicObject())
			return req->fail(400, "changes[" + String(i) + "].properties must be an object");
		
		auto props = propsVar.getDynamicObject()->getProperties();
		
		for (int j = 0; j < props.size(); j++)
		{
			auto propId = props.getName(j);
			
			// Validate property exists
			if (!sc->hasProperty(propId))
				return req->fail(400, "property '" + propId.toString() + 
								 "' does not exist on component '" + componentId + "'");
			
			// Check for script-lock (unless force=true)
			if (!force && sc->isPropertyOverwrittenByScript(propId))
			{
				DynamicObject::Ptr lockedEntry = new DynamicObject();
				lockedEntry->setProperty(RestApiIds::id, componentId);
				lockedEntry->setProperty(RestApiIds::property, propId.toString());
				lockedProperties.add(var(lockedEntry.get()));
			}
			
			// Validate parentComponent target exists
			if (propId == parentComponentId)
			{
				auto targetParent = props.getValueAt(j).toString();
				if (targetParent.isNotEmpty())
				{
					if (content->getComponentWithName(Identifier(targetParent)) == nullptr)
					{
						return req->fail(400, "parentComponent target '" + targetParent + 
										 "' does not exist for component '" + componentId + "'");
					}
				}
			}
		}
	}
	
	// If any properties are locked, fail with details
	if (lockedProperties.size() > 0)
	{
		DynamicObject::Ptr result = new DynamicObject();
		result->setProperty(RestApiIds::success, false);
		result->setProperty(RestApiIds::errorMessage, "Properties are locked by script (use force=true to override)");
		result->setProperty(RestApiIds::locked, var(lockedProperties));
		
		RestServer::Response response;
		response.statusCode = 400;
		response.contentType = "application/json";
		response.body = JSON::toString(var(result.get()));
		
		req->complete(response);
		return req->waitForResponse();
	}
	
	// Phase 2: Apply all changes
	Array<var> appliedChanges;
	bool recompileRequired = false;
	
	ValueTreeUpdateWatcher::ScopedDelayer sd(content->getUpdateWatcher(), true);

	for (int i = 0; i < changesVar.size(); i++)
	{
		auto change = changesVar[i];
		auto componentId = change[RestApiIds::id].toString();
		auto sc = content->getComponentWithName(Identifier(componentId));
		auto props = change[RestApiIds::properties].getDynamicObject()->getProperties();
		
		// Use ScopedPropertyEnabler to bypass script tracking (like paste action)
		ScriptComponent::ScopedPropertyEnabler spe(sc);
		
		Array<var> appliedProps;
		
		for (int j = 0; j < props.size(); j++)
		{
			auto propId = props.getName(j);
			auto newValue = props.getValueAt(j);
			
			// Track parentComponent changes
			if (propId == parentComponentId)
				recompileRequired = true;
			
			// Handle colour conversion if needed
			if (propId.toString().toLowerCase().contains("colour"))
			{
				auto colour = ApiHelpers::getColourFromVar(newValue);
				newValue = (int64)colour.getARGB();
			}
			
			sc->setScriptObjectPropertyWithChangeMessage(propId, newValue, sendNotificationAsync);
			appliedProps.add(propId.toString());
		}
		
		DynamicObject::Ptr appliedEntry = new DynamicObject();
		appliedEntry->setProperty(RestApiIds::id, componentId);
		appliedEntry->setProperty(RestApiIds::properties, var(appliedProps));
		appliedChanges.add(var(appliedEntry.get()));
	}
	
	// Build success response
	DynamicObject::Ptr result = new DynamicObject();
	result->setProperty(RestApiIds::success, true);
	result->setProperty(RestApiIds::moduleId, moduleId);
	result->setProperty(RestApiIds::applied, var(appliedChanges));
	result->setProperty(RestApiIds::recompileRequired, recompileRequired);
	result->setProperty(RestApiIds::logs, Array<var>());
	result->setProperty(RestApiIds::errors, Array<var>());
	
	req->complete(RestServer::Response::ok(var(result.get())));
	return req->waitForResponse();
}

RestServer::Response RestHelpers::handleScreenshot(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	// Parse parameters
	auto moduleId = req->getRequest()[RestApiIds::moduleId];
	if (moduleId.isEmpty())
		moduleId = "Interface";
	
	auto componentId = req->getRequest()[RestApiIds::id];
	
	auto scaleStr = req->getRequest()[RestApiIds::scale];
	float scale = scaleStr.isNotEmpty() ? scaleStr.getFloatValue() : 1.0f;
	
	// Validate scale (only 0.5 or 1.0 allowed)
	if (scale != 0.5f && scale != 1.0f)
		scale = 1.0f;
	
	// Parse optional outputPath for file output mode
	auto outputPath = req->getRequest()[RestApiIds::outputPath];
	
	if (outputPath.isNotEmpty())
	{
		// Validate .png extension (case-insensitive)
		if (!outputPath.endsWithIgnoreCase(".png"))
			return req->fail(400, "outputPath must end with .png extension");
		
		// Validate parent directory exists
		File outputFile(outputPath);
		File parentDir = outputFile.getParentDirectory();
		
		if (!parentDir.isDirectory())
			return req->fail(400, "Parent directory does not exist: " + parentDir.getFullPathName());
	}
	
	// Find the script processor
	auto jp = dynamic_cast<JavascriptProcessor*>(
		ProcessorHelpers::getFirstProcessorWithName(mc->getMainSynthChain(), moduleId));
	
	if (jp == nullptr)
		return req->fail(404, "module not found: " + moduleId);
	
	auto content = dynamic_cast<ProcessorWithScriptingContent*>(jp)->getScriptingContent();
	
	// If component ID specified, validate it exists
	Rectangle<int> cropBounds;
	Rectangle<int> fullBounds = { 0, 0, content->getContentWidth(), content->getContentHeight() };
	bool cropToComponent = false;
	
	if (componentId.isNotEmpty())
	{
		auto sc = content->getComponentWithName(Identifier(componentId));

		if (sc == nullptr)
			return req->fail(404, "component not found: " + componentId);
		
		auto x = sc->getGlobalPositionX();
		auto y = sc->getGlobalPositionY();

		cropBounds = { x, y, (int)sc->getScriptObjectProperty("width"), sc->getScriptObjectProperty("height") };
		cropToComponent = true;
	}
	else
	{
		cropBounds = fullBounds;
	}
	
#if 0
	 ============================================
	 TODO: Christoph implements capture logic here
	 ============================================
	 
	 Requirements:
	 - Find the ScriptContentComponent (the actual UI component on screen)
	 - Marshal to message thread for capture
	 - Use Component::createComponentSnapshot(bounds, true, scale)
	 - If componentId specified, crop to that component's bounds
	 - 1 second timeout - return error if exceeded
	 - Handle case where UI is mid-compilation gracefully
	
	 Suggested structure:
#endif
	
	Image capturedImage;
	bool captureSuccess = false;
	WaitableEvent captureComplete;
	
	auto sp = dynamic_cast<ProcessorWithScriptingContent*>(jp);

	SafeAsyncCall::callAsyncIfNotOnMessageThread<ProcessorWithScriptingContent>(*sp, [&](ProcessorWithScriptingContent& spp)
	{
		hise::ScriptContentComponent component(&spp);
		component.setBounds(fullBounds);
		capturedImage = component.createComponentSnapshot(cropBounds, true, scale);
		captureSuccess = capturedImage.isValid();
		captureComplete.signal();
	});
	
	if (!captureComplete.wait(1000))
	    return req->fail(500, "screenshot capture timed out");
	
	if (!captureSuccess)
	    return req->fail(500, "failed to capture screenshot");
	
	// ============================================
	// End of Christoph's section
	// ============================================
	
	if (!captureSuccess)
		return req->fail(500, "screenshot capture not yet implemented");
	
	if (!capturedImage.isValid())
		return req->fail(500, "failed to capture screenshot");
	
	// Build response
	DynamicObject::Ptr result = new DynamicObject();
	result->setProperty(RestApiIds::success, true);
	result->setProperty(RestApiIds::moduleId, moduleId);
	
	if (componentId.isNotEmpty())
		result->setProperty(RestApiIds::id, componentId);
	
	result->setProperty(RestApiIds::width, capturedImage.getWidth());
	result->setProperty(RestApiIds::height, capturedImage.getHeight());
	result->setProperty(RestApiIds::scale, scale);
	
	if (outputPath.isNotEmpty())
	{
		// File output mode: write PNG to file
		File outputFile(outputPath);
		
		// Delete existing file to ensure clean overwrite (FileOutputStream doesn't truncate)
		if (outputFile.existsAsFile())
			outputFile.deleteFile();
		
		FileOutputStream fos(outputFile);
		
		if (fos.failedToOpen())
			return req->fail(500, "failed to open output file: " + outputPath);
		
		PNGImageFormat pngFormat;
		if (!pngFormat.writeImageToStream(capturedImage, fos))
			return req->fail(500, "failed to write PNG to file");
		
		result->setProperty(RestApiIds::filePath, outputFile.getFullPathName());
	}
	else
	{
		// Base64 output mode: encode to base64 PNG
		MemoryBlock mb;
		{
			MemoryOutputStream mos(mb, false);
			PNGImageFormat pngFormat;
			if (!pngFormat.writeImageToStream(capturedImage, mos))
				return req->fail(500, "failed to encode PNG");
		}
		
		auto base64 = Base64::toBase64(mb.getData(), mb.getSize());
		result->setProperty(RestApiIds::imageData, base64);
	}
	
	result->setProperty(RestApiIds::logs, Array<var>());
	result->setProperty(RestApiIds::errors, Array<var>());
	
	req->complete(RestServer::Response::ok(var(result.get())));
	return req->waitForResponse();
}

RestServer::Response RestHelpers::handleGetSelectedComponents(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	// Get moduleId (default to "Interface")
	auto moduleId = req->getRequest()[RestApiIds::moduleId];
	if (moduleId.isEmpty())
		moduleId = "Interface";
	
	auto jp = dynamic_cast<JavascriptProcessor*>(
		ProcessorHelpers::getFirstProcessorWithName(mc->getMainSynthChain(), moduleId));
	
	if (jp == nullptr)
		return req->fail(404, "module not found: " + moduleId);
	
	// Get the selection from the broadcaster
	auto broadcaster = mc->getScriptComponentEditBroadcaster();
	auto selection = broadcaster->getSelection();
	
	// Build response
	DynamicObject::Ptr result = new DynamicObject();
	result->setProperty(RestApiIds::success, true);
	result->setProperty(RestApiIds::moduleId, moduleId);
	result->setProperty(RestApiIds::selectionCount, selection.size());
	
	Array<var> components;
	
	for (auto sc : selection)
	{
		if (sc == nullptr)
			continue;
		
		DynamicObject::Ptr comp = new DynamicObject();
		comp->setProperty(RestApiIds::id, sc->getName().toString());
		comp->setProperty(RestApiIds::type, sc->getObjectName().toString());
		
		// Include all properties (same format as get_component_properties)
		Array<var> properties;
		
		for (int i = 0; i < sc->getNumIds(); i++)
		{
			DynamicObject::Ptr po = new DynamicObject();
			
			auto propId = sc->getIdFor(i);
			auto v = sc->getScriptObjectProperty(i);
			auto nonDefault = sc->getNonDefaultScriptObjectProperties();
			
			if (propId.toString().toLowerCase().contains("colour"))
				v = "0x" + ApiHelpers::getColourFromVar(v).toDisplayString(true);
			
			auto opts = sc->getOptionsFor(propId);
			
			po->setProperty(RestApiIds::id, propId.toString());
			po->setProperty(RestApiIds::value, v);
			po->setProperty(RestApiIds::isDefault, !nonDefault.hasProperty(propId));
			
			if (!opts.isEmpty())
			{
				Array<var> ol;
				for (auto o : opts)
					ol.add(var(o));
				po->setProperty(RestApiIds::options, var(ol));
			}
			
			properties.add(po.get());
		}
		
		comp->setProperty(RestApiIds::properties, var(properties));
		components.add(var(comp.get()));
	}
	
	result->setProperty(RestApiIds::components, components);
	result->setProperty(RestApiIds::logs, Array<var>());
	result->setProperty(RestApiIds::errors, Array<var>());
	
	req->complete(RestServer::Response::ok(var(result.get())));
	return req->waitForResponse();
}

RestServer::Response RestHelpers::handleSimulateInteractions(BackendProcessor* bp, RestServer::AsyncRequest::Ptr req)
{
	// Capture console output during interaction execution
	ScopedConsoleHandler consoleHandler(bp, req);
	
	// Parse the request body to get interactions array
	auto body = req->getRequest().getJsonBody();
	
	if (body.isUndefined() || body.isVoid())
		return req->fail(400, "Request body is required");
	
	auto interactionsVar = body.getProperty(RestApiIds::interactions, var());
	
	if (!interactionsVar.isArray())
		return req->fail(400, "'interactions' must be an array");
	
	// Parse verbose flag (default false)
	bool verbose = (bool)body.getProperty(RestApiIds::verbose, false);
	
	// Get the interaction tester (only available when REST server is running)
	auto* tester = bp->getInteractionTester();
	
	if (tester == nullptr)
		return req->fail(503, "InteractionTester not available - REST server may have been stopped");
	
	// Execute on message thread
	InteractionTester::TestResult testResult;
	bool executed = false;
	WaitableEvent completed;
	
	SafeAsyncCall::callAsyncIfNotOnMessageThread<BackendProcessor>(*bp, [&, verbose](BackendProcessor& processor)
	{
		auto* t = processor.getInteractionTester();
		if (t != nullptr)
		{
			testResult = t->executeInteractions(interactionsVar, verbose);
			executed = true;
		}
		completed.signal();
	});
	
	// Wait for execution with timeout (30 seconds should be plenty for any interaction sequence)
	if (!completed.wait(30000))
		return req->fail(500, "Interaction execution timed out");
	
	if (!executed)
		return req->fail(500, "Failed to execute interactions");
	
	// Build response
	DynamicObject::Ptr result = new DynamicObject();
	
	// If any script errors were captured during execution, mark as failed
	bool success = testResult.success && !req->hasErrors();
	result->setProperty(RestApiIds::success, success);
	
	if (!success)
	{
		if (testResult.errorMessage.isNotEmpty())
			result->setProperty(RestApiIds::errorMessage, testResult.errorMessage);
		else if (req->hasErrors())
			result->setProperty(RestApiIds::errorMessage, "Script error(s) occurred during execution");
	}
	
	result->setProperty(RestApiIds::interactionsCompleted, testResult.interactionsCompleted);
	result->setProperty(RestApiIds::totalElapsedMs, testResult.totalElapsedMs);
	result->setProperty(RestApiIds::executionLog, testResult.executionLog);
	
	// Convert screenshots to JSON object with metadata (no base64 data)
	DynamicObject::Ptr screenshotsObj = new DynamicObject();
	for (const auto& [id, info] : testResult.screenshots)
	{
		DynamicObject::Ptr ssInfo = new DynamicObject();
		ssInfo->setProperty(RestApiIds::id, info.id);
		ssInfo->setProperty("sizeKB", info.sizeKB);
		ssInfo->setProperty("width", info.width);
		ssInfo->setProperty("height", info.height);
		screenshotsObj->setProperty(Identifier(id), var(ssInfo.get()));
	}
	result->setProperty(RestApiIds::screenshots, var(screenshotsObj.get()));
	
	// Add parse warnings if any
	Array<var> warnings;
	for (auto& w : testResult.parseWarnings)
		warnings.add(w);
	result->setProperty(RestApiIds::parseWarnings, var(warnings));
	
	// Add selected menu item info if a menu item was selected
	if (testResult.selectedMenuItem.wasSelected)
	{
		DynamicObject::Ptr menuItemObj = new DynamicObject();
		menuItemObj->setProperty(RestApiIds::text, testResult.selectedMenuItem.text);
		menuItemObj->setProperty(RestApiIds::itemId, testResult.selectedMenuItem.itemId);
		result->setProperty(RestApiIds::selectedMenuItem, var(menuItemObj.get()));
	}
	
	// Add mouse state when verbose=true
	if (verbose)
	{
		DynamicObject::Ptr mouseStateObj = new DynamicObject();
		mouseStateObj->setProperty(RestApiIds::currentTarget, testResult.finalMouseState.currentTarget.toString());
		
		DynamicObject::Ptr pixelPosObj = new DynamicObject();
		pixelPosObj->setProperty(RestApiIds::x, testResult.finalMouseState.pixelPosition.x);
		pixelPosObj->setProperty(RestApiIds::y, testResult.finalMouseState.pixelPosition.y);
		mouseStateObj->setProperty(RestApiIds::pixelPosition, var(pixelPosObj.get()));
		
		result->setProperty(RestApiIds::mouseState, var(mouseStateObj.get()));
	}
	
	// Complete the request - this merges logs/errors via mergeLogsIntoResponse()
	req->complete(RestServer::Response::ok(var(result.get())));
	auto finalResponse = req->waitForResponse();
	
	// Log the final response (which now includes logs/errors) to the interaction tester's console
	if (auto* t = bp->getInteractionTester())
		t->logResponse(interactionsVar, finalResponse.body, success);
	
	return finalResponse;
}

} // namespace hise
