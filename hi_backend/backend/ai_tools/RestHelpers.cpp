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

RestServer::Response RestHelpers::compile(MainController* mc, ScopedConsoleHandler& sch, RestServer::AsyncRequest::Ptr req)
{
	if (auto jp = getScriptProcessor(mc, req))
	{
		mc->getKillStateHandler().killVoicesAndCall(dynamic_cast<Processor*>(jp), [req, &sch](Processor* p)
		{
			JavascriptProcessor::ResultFunction rf = [req, &sch](const JavascriptProcessor::SnippetResult& result)
			{
				DynamicObject::Ptr r = new DynamicObject();
				r->setProperty(RestApiIds::success, result.r.wasOk());
				r->setProperty(RestApiIds::result, result.r.wasOk() ? "Recompiled OK" : "Compilation / Runtime Error");

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

	obj->setProperty(RestApiIds::id, sc->getName().toString());
	obj->setProperty(RestApiIds::type, sc->getObjectName().toString());
	obj->setProperty(RestApiIds::visible, sc->getScriptObjectProperty(ScriptComponent::visible));
	obj->setProperty(RestApiIds::enabled, sc->getScriptObjectProperty(ScriptComponent::enabled));
	obj->setProperty(RestApiIds::x, sc->getScriptObjectProperty(ScriptComponent::x));
	obj->setProperty(RestApiIds::y, sc->getScriptObjectProperty(ScriptComponent::y));
	obj->setProperty(RestApiIds::width, sc->getScriptObjectProperty(ScriptComponent::width));
	obj->setProperty(RestApiIds::height, sc->getScriptObjectProperty(ScriptComponent::height));

	Array<var> children;

	ScriptComponent::ChildIterator<ScriptComponent> ci(sc);

	auto v = sc->getPropertyValueTree();

	for (auto c : v)
	{
		if (auto child = sc->getScriptProcessor()->getScriptingContent()->getComponentWithName(c[RestApiIds::id].toString()))
		{
			auto cobj = createRecursivePropertyTree(child);
			children.add(var(cobj.get()));
		}
	}

	obj->setProperty(RestApiIds::childComponents, var(children));

	return obj;
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
			.withReturns("Project info, scriptsFolder path, and scriptProcessors with their callbacks"));
		
		// ApiRoute::GetScript
		m.add(RouteMetadata(ApiRoute::GetScript, "api/get_script")
			.withCategory("scripting")
			.withDescription("Read script content from a processor")
			.withReturns("Script content for the specified callback (or all callbacks merged)")
			.withModuleIdParam()
			.withQueryParam(RouteParameter(RestApiIds::callback, "Specific callback name (e.g., onInit). If omitted, returns all callbacks merged.").asOptional()));
		
		// ApiRoute::SetScript
		m.add(RouteMetadata(ApiRoute::SetScript, "api/set_script")
			.withMethod(RestServer::POST)
			.withCategory("scripting")
			.withDescription("Update script content and optionally compile")
			.withReturns("Compilation result with success status, logs, and errors")
			.withBodyParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID"))
			.withBodyParam(RouteParameter(RestApiIds::callback, "Specific callback to update. If omitted, script is treated as merged content.").asOptional())
			.withBodyParam(RouteParameter(RestApiIds::script, "The script content"))
			.withBodyParam(RouteParameter(RestApiIds::compile, "Whether to compile after setting").withDefault("true")));
		
		// ApiRoute::Recompile
		m.add(RouteMetadata(ApiRoute::Recompile, "api/recompile")
			.withCategory("scripting")
			.withDescription("Recompile a processor without changing its script content")
			.withReturns("Compilation result with success status, logs, and errors")
			.withModuleIdParam());
		
		// ApiRoute::ListComponents
		m.add(RouteMetadata(ApiRoute::ListComponents, "api/list_components")
			.withCategory("ui")
			.withDescription("List all UI components in a script processor")
			.withReturns("Array of components with id and type (flat list or hierarchical tree)")
			.withModuleIdParam()
			.withQueryParam(RouteParameter(RestApiIds::hierarchy, "If true, returns nested tree with layout properties").withDefault("false")));
		
		// ApiRoute::GetComponentProperties
		m.add(RouteMetadata(ApiRoute::GetComponentProperties, "api/get_component_properties")
			.withCategory("ui")
			.withDescription("Get all properties for a specific UI component")
			.withReturns("Component type and array of properties with id, value, isDefault, and options")
			.withModuleIdParam()
			.withQueryParam(RouteParameter(RestApiIds::componentId, "The component's ID (e.g., Button1, Panel1)")));
		
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
	server->setProperty(RestApiIds::version, "1.0.0");
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

RestServer::Response RestHelpers::handleGetScript(MainController* mc, RestServer::AsyncRequest::Ptr req, ScopedConsoleHandler& sch)
{
	ignoreUnused(sch);
	
	if (auto jp = getScriptProcessor(mc, req))
	{
		auto callbackParam = req->getRequest()[RestApiIds::callback];

		if (callbackParam.isNotEmpty())
		{
			if (auto cb = jp->getSnippet(Identifier(callbackParam)))
			{
				DynamicObject::Ptr result = new DynamicObject();
				result->setProperty(RestApiIds::success, true);
				result->setProperty(RestApiIds::moduleId, dynamic_cast<Processor*>(jp)->getId());
				result->setProperty(RestApiIds::callback, callbackParam);

				// onInit is special - it's raw content, not wrapped in function
				if (callbackParam == "onInit")
					result->setProperty(RestApiIds::script, cb->getAllContent());
				else
					result->setProperty(RestApiIds::script, cb->getSnippetAsFunction());

				result->setProperty(RestApiIds::logs, Array<var>());
				result->setProperty(RestApiIds::errors, Array<var>());

				req->complete(RestServer::Response::ok(var(result.get())));
				return req->waitForResponse();
			}
			else
				return req->fail(404, "callback " + callbackParam + " not found");
		}
		else
		{
			String x;
			jp->mergeCallbacksToScript(x);
			DynamicObject::Ptr result = new DynamicObject();
			result->setProperty(RestApiIds::success, true);
			result->setProperty(RestApiIds::moduleId, dynamic_cast<Processor*>(jp)->getId());
			result->setProperty(RestApiIds::script, x);
			result->setProperty(RestApiIds::logs, Array<var>());
			result->setProperty(RestApiIds::errors, Array<var>());
			
			req->complete(RestServer::Response::ok(var(result.get())));
			return req->waitForResponse();
		}
	}
	
	return req->fail(404, "moduleId is not a valid script processor");
}

RestServer::Response RestHelpers::handleSetScript(MainController* mc, RestServer::AsyncRequest::Ptr req, ScopedConsoleHandler& sch)
{
	auto obj = req->getRequest().getJsonBody();
	
	// Read moduleId from JSON body (unified POST parameter handling)
	auto moduleId = obj[RestApiIds::moduleId].toString();
	
	if (moduleId.isEmpty())
		return req->fail(400, "moduleId is required in request body");
	
	auto jp = dynamic_cast<JavascriptProcessor*>(
		ProcessorHelpers::getFirstProcessorWithName(mc->getMainSynthChain(), moduleId));
	
	if (jp == nullptr)
		return req->fail(404, "module not found: " + moduleId);
	
	auto callbackParam = obj[RestApiIds::callback].toString();
	auto scriptContent = obj[RestApiIds::script].toString();

	if (callbackParam.isNotEmpty())
	{
		if (auto cb = jp->getSnippet(Identifier(callbackParam)))
		{
			cb->replaceContentAsync(scriptContent);
		}
		else
			return req->fail(404, "callback not found: " + callbackParam);
	}
	else
	{
		auto ok = jp->parseSnippetsFromString(scriptContent, false);

		if (!ok)
			return req->fail(400, "Error at parsing script");
	}

	// Check if compile is requested (default true)
	bool shouldCompile = true;
	if (obj.hasProperty(RestApiIds::compile))
		shouldCompile = (bool)obj[RestApiIds::compile];

	if (shouldCompile)
		return compile(mc, sch, req);

	req->complete(RestServer::Response::ok("updated script"));
	return req->waitForResponse();
}

RestServer::Response RestHelpers::handleListComponents(MainController* mc, RestServer::AsyncRequest::Ptr req)
{
	if (auto jp = getScriptProcessor(mc, req))
	{
		auto ps = dynamic_cast<ProcessorWithScriptingContent*>(jp);
		auto hierarchyParam = req->getRequest()[RestApiIds::hierarchy];
		auto useHierarchy = (hierarchyParam == "true" || hierarchyParam == "1");

		DynamicObject::Ptr result = new DynamicObject();
		result->setProperty(RestApiIds::success, true);
		result->setProperty(RestApiIds::moduleId, dynamic_cast<Processor*>(jp)->getId());

		Array<var> components;

		auto c = ps->getScriptingContent();

		if (useHierarchy)
		{
			for (int i = 0; i < c->getNumComponents(); i++)
			{
				auto sc = c->getComponent(i);
				if (sc->getParentScriptComponent() == nullptr)
				{
					auto obj = createRecursivePropertyTree(sc);
					components.add(obj.get());
				}
			}
		}
		else
		{
			// Flat list: id and type only
			for(int i = 0; i < c->getNumComponents(); i++)
			{
				auto sc = c->getComponent(i);
				DynamicObject::Ptr comp = new DynamicObject();
				comp->setProperty(RestApiIds::id, sc->getName().toString());
				comp->setProperty(RestApiIds::type, sc->getObjectName().toString());
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
		auto componentId = req->getRequest()[RestApiIds::componentId];
		auto c = dynamic_cast<ProcessorWithScriptingContent*>(jp)->getScriptingContent();

		if (componentId.isEmpty())
			return req->fail(400, "componentId parameter is required");

		if (auto sc = c->getComponentWithName(Identifier(componentId)))
		{
			DynamicObject::Ptr result = new DynamicObject();
			result->setProperty(RestApiIds::success, true);
			result->setProperty(RestApiIds::moduleId, dynamic_cast<Processor*>(jp)->getId());
			result->setProperty(RestApiIds::componentId, componentId);
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

} // namespace hise
