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

// This is a helper tool to print out definitions for the extern const char* variables with the sizeof([]) operator
#if 0
#define PRINT_DATA(ns, x) DBG(juce::String("DECLARE_DATA(") + #x + ", " + juce::String(sizeof(ns::x)) + ");")

void printData()
{
	PRINT_DATA(HiBinaryData::SpecialSymbols, midiData);
	PRINT_DATA(HiBinaryData::SpecialSymbols, masterEffect);
	PRINT_DATA(HiBinaryData::SpecialSymbols, macros);
	PRINT_DATA(HiBinaryData::SpecialSymbols, globalCableIcon);
	PRINT_DATA(HiBinaryData::SpecialSymbols, scriptProcessor);
	PRINT_DATA(HiBinaryData::SpecialSymbols, routingIcon);
}

#undef PRINT_DATA
#endif

namespace hise { using namespace juce;


	

	void ExampleAssetManager::initialise()
	{
		if(!initialised)
		{
			initialised = true;

			setWorkingProject(mainProjectHandler.getRootFolder());

			auto snippetSettings = getAppDataDirectory(getMainController()).getChildFile("snippetBrowser.xml");

			if(auto xml = XmlDocument::parse(snippetSettings))
			{
				if(auto sd = xml->getChildByName("snippetDirectory"))
				{
					auto snippetDirectory = sd->getStringAttribute("value");

					if(File::isAbsolutePath(snippetDirectory))
					{
						auto assetDirectory = File(snippetDirectory).getChildFile("Assets");

						if(!assetDirectory.getChildFile("SampleMaps").isDirectory())
						{
							debugError(getMainController()->getMainSynthChain(), "Uninitialised assets, please download the assets and reload the snippet");
							initialised = false;
							return;
						}

						if(assetDirectory.isDirectory())
						{
							rootDirectory = assetDirectory;

							for(auto d: getSubDirectoryIds())
								rootDirectory.getChildFile(getIdentifier(d)).createDirectory();

							checkSubDirectories();

							pool->getAudioSampleBufferPool().loadAllFilesFromProjectFolder();
							pool->getImagePool().loadAllFilesFromProjectFolder();
							pool->getMidiFilePool().loadAllFilesFromProjectFolder();
							pool->getSampleMapPool().loadAllFilesFromProjectFolder();
							return;
						}
					}
				}
			}

			debugError(getMainController()->getMainSynthChain(), "You need to download the assets using the snippet browser");
		}
	}

	File ExampleAssetManager::getSubDirectory(SubDirectories dir) const
	{
		auto redirected = getSubDirectoryIds();

		if(redirected.contains(dir))
			return ProjectHandler::getSubDirectory(dir);
		else
			return mainProjectHandler.getSubDirectory(dir);
	}

	Array<FileHandlerBase::SubDirectories> ExampleAssetManager::getSubDirectoryIds() const
	{
		return {
			FileHandlerBase::SubDirectories::AudioFiles,
			FileHandlerBase::SubDirectories::SampleMaps,
			FileHandlerBase::SubDirectories::Samples,
			FileHandlerBase::SubDirectories::Images,
			FileHandlerBase::SubDirectories::MidiFiles
		};
	}

bool PluginParameterRamp::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages,
	const ProcessCallback& f)
{
	PluginParameterSimulatorInfo thisInfo, thisGesture;

	{
		SimpleReadWriteLock::ScopedReadLock sl(lock);
		thisInfo = currentInfo;
		thisGesture = gestureInfo;
	}

	auto threadMatches = thisInfo.sourceThread == PluginParameterSimulatorInfo::SourceThread::Audio;

	if(gestureInfo && gestureAtNextCallback && gestureInfo.sourceThread == PluginParameterSimulatorInfo::SourceThread::Audio)
	{
		thisGesture.performGesture();
		gestureAtNextCallback = false;

		if(thisGesture.eventType == PluginParameterSimulatorInfo::EventType::EndGesture)
		{
			currentInfo = {};
			return false;
		}
	}

	if(!thisInfo || !threadMatches)
		return false;

	

	if(!thisInfo.useRamp)
	{
		thisInfo.performChange();

		SimpleReadWriteLock::ScopedWriteLock sl(lock);
		currentInfo = {};
		return false;
	}

	auto numSamples = thisInfo.bufferSize != -1 ? thisInfo.bufferSize : buffer.getNumSamples();
	auto rampTime =  (double)numSamples / getMainController()->getMainSynthChain()->getSampleRate() * 1000.0;

	if(thisInfo.bufferSize == -1)
	{
		bump(thisInfo, rampTime);
		currentInfo.currentValue = thisInfo.currentValue;
		return false;
	}
	else
	{
		int numTodo = buffer.getNumSamples();
		int pos = 0;

		while (numTodo > 0)
		{
			bump(thisInfo, rampTime);
			float* channels[HISE_NUM_PLUGIN_CHANNELS];
			f(channels, buffer, midiMessages, pos, thisInfo.bufferSize);

			numTodo -= thisInfo.bufferSize;
			pos += thisInfo.bufferSize;
		}
	}

	currentInfo.currentValue = thisInfo.currentValue;
	return true;
}

void PluginParameterRamp::setCurrentInfo(const PluginParameterSimulatorInfo& newInfo)
{
	auto rampWasActive = gestureInfo.useRamp;
	auto prevValue = currentInfo.currentValue;

	auto gestureWasActive = gestureInfo.isActiveGesture();

	{
		SimpleReadWriteLock::ScopedWriteLock sl(lock);

		if(newInfo.isGestureEvent())
			gestureInfo = newInfo;
		else
			currentInfo = newInfo;
	}

	if(!newInfo)
		return;

	if(rampWasActive)
		currentInfo.currentValue = prevValue;

	auto gestureShouldBeActive = gestureInfo.isActiveGesture();

	if(gestureWasActive != gestureShouldBeActive)
	{
		if(gestureInfo.sourceThread != PluginParameterSimulatorInfo::SourceThread::UI)
		{
			gestureAtNextCallback = true;
		}
		else
		{
			gestureInfo.performGesture();
		}
	}

	auto useTimer = gestureInfo.useRamp && newInfo.sourceThread == PluginParameterSimulatorInfo::SourceThread::UI;
	auto useThread = (gestureInfo.useRamp || gestureShouldBeActive || gestureWasActive) && newInfo.sourceThread == PluginParameterSimulatorInfo::SourceThread::Custom;

	if(useTimer)
		start();
	else
		stop();

	if(useThread)
		ThreadStarters::startHigh(this);
	else
		stopThread(1000);

	if(currentInfo.sourceThread == PluginParameterSimulatorInfo::SourceThread::UI && !currentInfo.useRamp)
	{
		currentInfo.performChange();
		currentInfo = {};
	}
}

void PluginParameterRamp::bump(PluginParameterSimulatorInfo& info, double milliSeconds)
{
	auto delta = (float)milliSeconds * 0.001f;

	if(!sign)
		delta *= -1.0f;

	auto nv = info.currentValue + delta;

	if(nv >= 1.0f)
		sign = false;
	if(nv <= 0.0f)
		sign = true;

	

	info.currentValue = jlimit(0.0f, 1.0f, nv);
	info.performChange();
}

bool BackendProcessor::registerAsyncRestApi(const URL& url, RestServer::Method m)
{
	if (restServer.isRunning())
	{
		jassertfalse;
		return false;
	}

	restServer.addAsyncRoute(
		m,
		url,
		BIND_MEMBER_FUNCTION_1(BackendProcessor::onAsyncRequest));

	return true;
}


struct RestHelpers
{
	struct ScopedConsoleHandler : public ControlledObject
	{
		ScopedConsoleHandler(MainController* mc, RestServer::AsyncRequest::Ptr request_) :
			ControlledObject(mc),
			request(request_)
		{
			debugToConsole(getMainController()->getMainSynthChain(), "> create console pipe");
			getMainController()->getConsoleHandler().setCustomCodeHandler(BIND_MEMBER_FUNCTION_3(ScopedConsoleHandler::onMessage));
		}

		~ScopedConsoleHandler()
		{
			getMainController()->getConsoleHandler().setCustomCodeHandler({});
			debugToConsole(getMainController()->getMainSynthChain(), "> close console pipe");
		}

		void onMessage(const String& message, int warning, const Processor* p)
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
					auto p = parseError(entry, scriptRoot, moduleId);
					if (p.location.isNotEmpty())
						callstack.add(p.toCallstackString());
				}

				request->appendError(error.message, callstack);
			}
		}

	private:

		struct ParsedError
		{
			String message;      // "API call with undefined parameter 0"
			String location;     // "Scripts/funky.js:9:16"
			String functionName; // "dudel" (empty if not a callstack entry)

			/** Returns formatted callstack entry: "dudel() at Scripts/funky.js:9:16"
				or just location if no function name. */
			String toCallstackString() const
			{
				if (functionName.isEmpty())
					return location;
				return functionName + "() at " + location;
			}
		};
		/** Parses an error string and extracts message, location, and optional function name.

			Handles both formats:
			- Error message: "API call with undefined parameter 0 {{SW50ZXJm...}}"
			- Callstack entry: ":\t\t\tdudel() - funky.js (9)\t{{SW50ZXJm...}}"

			@param errorString   The full error/callstack string
			@param scriptRoot    The project's Scripts folder for resolving full paths
			@param moduleId      The script processor's module ID - used as fallback filename

			@returns ParsedError with message, location, and optional functionName
		*/
		static ParsedError parseError(const String& errorString,
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

		RestServer::AsyncRequest::Ptr request;
	};

	static JavascriptProcessor* getScriptProcessor(MainController* mc, RestServer::AsyncRequest::Ptr req)
	{
		String moduleId = req->getRequest()["moduleId"];

		if (moduleId.isEmpty())
			return nullptr;

		return dynamic_cast<JavascriptProcessor*>(ProcessorHelpers::getFirstProcessorWithName(mc->getMainSynthChain(), moduleId));
	}

	static RestServer::Response compile(MainController* mc, ScopedConsoleHandler& sch, RestServer::AsyncRequest::Ptr req)
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
};

static DynamicObject::Ptr createRecursivePropertyTree(ScriptComponent* sc)
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

RestServer::Response BackendProcessor::onAsyncRequest(RestServer::AsyncRequest::Ptr req)
{
	debugToConsole(getMainSynthChain(), "\tincoming HTTP request: " + req->getRequest().url.toString(true));

	RestHelpers::ScopedConsoleHandler sch(this, req);

	auto subURL = req->getRequest().url.getSubPath(false);

	if (subURL == "api/recompile")
	{
		return RestHelpers::compile(this, sch, req);
	}
	
	if (subURL == "api/get_script")
	{
		if (auto jp = RestHelpers::getScriptProcessor(this, req))
		{
			auto callback = req->getRequest()["callback"];

			if (callback.isNotEmpty())
			{
				if (auto cb = jp->getSnippet(Identifier(callback)))
				{
					DynamicObject::Ptr result = new DynamicObject();
					result->setProperty("success", true);
					result->setProperty("moduleId", dynamic_cast<Processor*>(jp)->getId());
					result->setProperty("callback", callback);

					// onInit is special - it's raw content, not wrapped in function
					if (callback == "onInit")
						result->setProperty("script", cb->getAllContent());
					else
						result->setProperty("script", cb->getSnippetAsFunction());

					result->setProperty("logs", Array<var>());
					result->setProperty("errors", Array<var>());

					return RestServer::Response::ok(var(result.get()));
				}
					
				else
					return RestServer::Response::error(404, "callback " + callback + " not found");
			}
			else
			{
				String x;
				jp->mergeCallbacksToScript(x);
				DynamicObject::Ptr result = new DynamicObject();
				result->setProperty("success", true);
				result->setProperty("moduleId", dynamic_cast<Processor*>(jp)->getId());
				result->setProperty("script", x);
				result->setProperty("logs", Array<var>());
				result->setProperty("errors", Array<var>());
				return RestServer::Response::ok(var(result.get()));
			}
		}
	}

	if (subURL == "api/set_script")
	{
		if(auto jp = RestHelpers::getScriptProcessor(this, req))
		{ 
			
			auto obj = req->getRequest().getJsonBody();

			auto callback = obj["callback"].toString();
			auto script = obj["script"].toString();

			if (callback.isNotEmpty())
			{
				if (auto cb = jp->getSnippet(Identifier(callback)))
				{
					MessageManagerLock mm;
					cb->replaceContentAsync(script, false);
				}
				else
					return RestServer::Response::error(404, "callback not found: " + callback);
			}
			else
			{
				auto ok = jp->parseSnippetsFromString(script, false);

				if (!ok)
					return RestServer::Response::error(400, "Error at parsing script");
			}

			if (obj["compile"])
				return RestHelpers::compile(this, sch, req);

			return RestServer::Response::ok("updated script");
		}
		else
		{
			return RestServer::Response::error(404, "module not found");
		}
	}

	if (subURL == "api/status")
	{
		DynamicObject::Ptr result = new DynamicObject();
		result->setProperty("success", true);

		// Server info
		DynamicObject::Ptr server = new DynamicObject();
		server->setProperty("version", "1.0.0");
		result->setProperty("server", var(server.get()));

		// Project info
		DynamicObject::Ptr project = new DynamicObject();

		// TODO: Get project name from settings/project handler
		project->setProperty("name", GET_HISE_SETTING(getMainSynthChain(), HiseSettings::Project::Name));

		auto& projectHandler = getSampleManager().getProjectHandler();
		auto projectFolder = projectHandler.getWorkDirectory().getFullPathName().replace("\\", "/");
		auto scriptsFolder = projectHandler.getSubDirectory(FileHandlerBase::Scripts).getFullPathName().replace("\\", "/");

		project->setProperty("projectFolder", projectFolder);
		project->setProperty("scriptsFolder", scriptsFolder);
		result->setProperty("project", var(project.get()));

		// Script processors
		Array<var> processors;

		Processor::Iterator<JavascriptProcessor> iter(getMainSynthChain());

		while (auto jp = iter.getNextProcessor())
		{
			auto asP = dynamic_cast<Processor*>(jp);
			DynamicObject::Ptr proc = new DynamicObject();
			proc->setProperty("moduleId", dynamic_cast<Processor*>(jp)->getId());
		
			if (auto jmp = dynamic_cast<JavascriptMidiProcessor*>(jp))
				proc->setProperty("isMainInterface", jmp->isFront() ? true : false);
			else
				proc->setProperty("isMainInterface", false);
			
		    // External files
		    Array<var> externalFiles;

			for (int i = 0; i < jp->getNumWatchedFiles(); i++)
			{
				auto fp = jp->getWatchedFile(i);
				auto rp = fp.getRelativePathFrom(scriptsFolder).replaceCharacter('\\', '/');
				externalFiles.add(rp);
			}

		    proc->setProperty("externalFiles", externalFiles);
		
		    Array<var> callbacks;
		    for (int i = 0; i < jp->getNumSnippets(); i++)
		    {
		        if (auto snippet = jp->getSnippet(i))
		        {
		            DynamicObject::Ptr cb = new DynamicObject();
		            cb->setProperty("id", snippet->getCallbackName().toString());
		
		            // TODO: Check if callback is empty
		            cb->setProperty("empty", snippet->isSnippetEmpty());
		
		            callbacks.add(var(cb.get()));
		        }
		    }
		    proc->setProperty("callbacks", callbacks);
		
		    processors.add(var(proc.get()));
		}

		result->setProperty("scriptProcessors", processors);
		result->setProperty("logs", Array<var>());
		result->setProperty("errors", Array<var>());

		return RestServer::Response::ok(var(result.get()));
	}

	if (subURL == "api/list_components")
	{
		if (auto jp = RestHelpers::getScriptProcessor(this, req))
		{
			auto ps = dynamic_cast<ProcessorWithScriptingContent*>(jp);
			auto hierarchyParam = req->getRequest()["hierarchy"];
			auto useHierarchy = (hierarchyParam == "true" || hierarchyParam == "1");

			DynamicObject::Ptr result = new DynamicObject();
			result->setProperty("success", true);
			result->setProperty("moduleId", dynamic_cast<Processor*>(jp)->getId());

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
					comp->setProperty("id", sc->getName().toString());
					comp->setProperty("type", sc->getObjectName().toString());
					components.add(var(comp.get()));
				}
			}

			result->setProperty("components", components);
			result->setProperty("logs", Array<var>());
			result->setProperty("errors", Array<var>());

			return RestServer::Response::ok(var(result.get()));
		}
		else
		{
			return RestServer::Response::error(404, "moduleId is not a valid script processor");
		}
	}

	if (subURL == "api/get_component_properties")
	{
		if (auto jp = RestHelpers::getScriptProcessor(this, req))
		{
			auto componentId = req->getRequest()["componentId"];

			auto c = dynamic_cast<ProcessorWithScriptingContent*>(jp)->getScriptingContent();

			if (componentId.isEmpty())
				return RestServer::Response::error(400, "componentId parameter is required");

			if (auto sc = c->getComponentWithName(Identifier(componentId)))
			{
				DynamicObject::Ptr result = new DynamicObject();
				result->setProperty("success", true);
				result->setProperty("moduleId", dynamic_cast<Processor*>(jp)->getId());
				result->setProperty("componentId", componentId);

				result->setProperty("type", sc->getObjectName().toString() /* component type as string */);

				Array<var> properties;

				for (int i = 0; i < sc->getNumIds(); i++)
				{
					DynamicObject::Ptr po = new DynamicObject();

					auto id = sc->getIdFor(i);
					auto v = sc->getScriptObjectProperty(i);

					auto nonDefault = sc->getNonDefaultScriptObjectProperties();

					if (id.toString().toLowerCase().contains("colour"))
						v = "0x" + ApiHelpers::getColourFromVar(v).toDisplayString(true);

					auto options = sc->getOptionsFor(id);

					po->setProperty("id", id.toString());
					po->setProperty("value", v);
					po->setProperty("isDefault", !nonDefault.hasProperty(id));

					if (!options.isEmpty())
					{
						Array<var> ol;

						for (auto o : options)
							ol.add(var(o));

						po->setProperty("options", var(ol));
					}

					properties.add(po.get());
				}

				result->setProperty("properties", var(properties));
				result->setProperty("logs", Array<var>());
				result->setProperty("errors", Array<var>());

				return RestServer::Response::ok(var(result.get()));
			}
			else
				return RestServer::Response::error(404, "component not found: " + componentId);
		}
		else
		{
			return RestServer::Response::error(404, "moduleId is not a valid script processor");
		}
	}

	return req->fail(404, "missing API method " + subURL);
}


BackendProcessor::BackendProcessor(AudioDeviceManager *deviceManager_/*=nullptr*/, AudioProcessorPlayer *callback_/*=nullptr*/) :
  MainController(),
  AudioProcessorDriver(deviceManager_, callback_),
  scriptUnlocker(this),
  autosaver(this),
  pluginParameterRamp(this)
{
	auto url = restServer.getBaseURL();

	registerAsyncRestApi(url.getChildURL("api/recompile").withParameter("moduleId", ""));
	registerAsyncRestApi(url.getChildURL("api/get_script").withParameter("moduleId", "").withParameter("callback", ""));

	registerAsyncRestApi(url.getChildURL("api/set_script")
							.withParameter("moduleId", "")
							.withParameter("callback", "")
							.withParameter("script", "")
							.withParameter("compile", "false"),
						 RestServer::Method::POST);

	registerAsyncRestApi(url.getChildURL("api/status"));

	registerAsyncRestApi(url.getChildURL("api/list_components")
							.withParameter("moduleId", "")
							.withParameter("hierarchy", "false"));

	registerAsyncRestApi(url.getChildURL("api/get_component_properties")
							.withParameter("moduleId", "")
							.withParameter("componentId", ""));

	auto ok = restServer.start(1900);
	ExtendedApiDocumentation::init();

    synthChain = new ModulatorSynthChain(this, "Master Chain", NUM_POLYPHONIC_VOICES);
    
	synthChain->addProcessorsWhenEmpty();

#if HISE_INCLUDE_PROFILING_TOOLKIT
	getDebugSession().syncRecordingBroadcaster.addListener(*synthChain, [](ModulatorSynthChain& c, bool isEnabled)
	{
		c.setEnableProfiling(isEnabled, &c.getMainController()->getDebugSession(), 0);
	}, false);
#endif

	getSampleManager().getModulatorSamplerSoundPool()->setDebugProcessor(synthChain);
	getMacroManager().setMacroChain(synthChain);

	getExpansionHandler().addListener(this);

	if (!inUnitTestMode())
	{
		handleEditorData(false);
		restoreGlobalSettings(this);
	}

	if (CompileExporter::isUsingWorkingDirectoryAsProjectFolder())
	{
		try
		{
			GET_PROJECT_HANDLER(synthChain).setWorkingProject(CompileExporter::getCurrentWorkDirectory());
		}
		catch (Result& r)
		{
			GET_PROJECT_HANDLER(synthChain).restoreWorkingProjects();
			jassertfalse;
		}
		
	}
	else
	{
		GET_PROJECT_HANDLER(synthChain).restoreWorkingProjects();
	}
	

	initData(this);

	getFontSizeChangeBroadcaster().sendMessage(sendNotification, getGlobalCodeFontSize());

	GET_PROJECT_HANDLER(synthChain).checkSubDirectories();

	dllManager = new BackendDllManager(this);

	if(getCurrentFileHandler().getRootFolder().isDirectory())
		refreshExpansionType();

	//getExpansionHandler().createAvailableExpansions();


	if (!inUnitTestMode())
	{
		getAutoSaver().initialise();
		
	}
	
	clearPreset(dontSendNotification);
	getSampleManager().getProjectHandler().addListener(this);

	createInterface(600, 500);

	if (!inUnitTestMode())
	{
		auto tmp = getCurrentSampleMapPool();
		auto tmp2 = getCurrentMidiFilePool();

		auto f = [tmp, tmp2](Processor*)
		{
			tmp->loadAllFilesFromProjectFolder();
			tmp2->loadAllFilesFromProjectFolder();
			return SafeFunctionCall::OK;
		};

		getKillStateHandler().killVoicesAndCall(getMainSynthChain(), f, MainController::KillStateHandler::TargetThread::SampleLoadingThread);
	}
    
    externalClockSim.bpm = dynamic_cast<GlobalSettingManager*>(this)->globalBPM;

#if HISE_INCLUDE_LORIS && !HISE_USE_LORIS_DLL

	lorisManager = new LorisManager(File(), [this](String message)
    {
        this->getConsoleHandler().writeToConsole(message, 1, getMainSynthChain(), Colour(HISE_ERROR_COLOUR));
    });

#else

    if(GET_HISE_SETTING(getMainSynthChain(), HiseSettings::Compiler::EnableLoris))
    {
#if HISE_USE_LORIS_DLL
        auto f = ProjectHandler::getAppDataDirectory(nullptr).getChildFile("loris_library");
        
        if(f.isDirectory())
        {
            lorisManager = new LorisManager(f, [this](String message)
            {
                this->getConsoleHandler().writeToConsole(message, 1, getMainSynthChain(), Colour(HISE_ERROR_COLOUR));
            });
        }
        else
        {
            if(PresetHandler::showYesNoWindow("Install Loris library", "In order to use Loris, you need to install the dll libraries in the app data directory of HISE. Press OK to create the folder, then download the precompiled dlls and put it in this directory"))
            {
                f.createDirectory();
                f.revealToUser();
            }
        }
#endif
    }
    else
    {
        auto f = ProjectHandler::getAppDataDirectory(this).getChildFile("loris_library");
        
        if(f.isDirectory())
            debugToConsole(getMainSynthChain(), "You seem to have installed the loris library, but you need to enable the setting `EnableLoris` in the HISE preferences");
    }

#endif

	AudioProcessor::addListener(&getUserPresetHandler());

}


BackendProcessor::~BackendProcessor()
{
#if IS_STANDALONE_APP
	for(auto p: getParameters())
    {
        if(auto typed = dynamic_cast<HisePluginParameterBase*>(p))
            typed->cleanup();
    }

	setParameterTree({});
#endif

	AudioProcessor::removeListener(&getUserPresetHandler());

	getRootDispatcher().setState(dispatch::HashedPath(dispatch::CharPtr::Type::Wildcard), dispatch::State::Shutdown);

	docWindow = nullptr;
	docProcessor = nullptr;
	getDatabase().clear();

#if JUCE_ENABLE_AUDIO_GUARD
	AudioThreadGuard::setHandler(nullptr);
#endif

    getJavascriptThreadPool().stopThread(1000);
	getJavascriptThreadPool().getGlobalServer()->cleanup();


	getSampleManager().cancelAllJobs();

	getSampleManager().getProjectHandler().removeListener(this);
	getExpansionHandler().removeListener(this);

	deletePendingFlag = true;

	clearPreset(dontSendNotification);

	synthChain = nullptr;

	dllManager->unloadDll();

	handleEditorData(true);
}



void BackendProcessor::projectChanged(const File& /*newRootDirectory*/)
{
	getExpansionHandler().setCurrentExpansion("");

	clearExtraDefinitionCache();

	auto tmp = getCurrentSampleMapPool();
	auto tmp2 = getCurrentMidiFilePool();

	auto f = [tmp, tmp2](Processor*)
	{
		tmp->loadAllFilesFromProjectFolder();
		tmp2->loadAllFilesFromProjectFolder();
		return SafeFunctionCall::OK;
	};

	getKillStateHandler().killVoicesAndCall(getMainSynthChain(), f, MainController::KillStateHandler::TargetThread::SampleLoadingThread);

	refreshExpansionType();
	
    dllManager->loadDll(true);
}

void BackendProcessor::refreshExpansionType()
{
	getSettingsObject().refreshProjectData();
	auto expType = dynamic_cast<GlobalSettingManager*>(this)->getSettingsObject().getSetting(HiseSettings::Project::ExpansionType).toString();

	if (expType == "Disabled")
	{
		getExpansionHandler().setExpansionType<ExpansionHandler::Disabled>();
	}
	else if (expType == "FilesOnly" || expType == "Custom")
	{
		getExpansionHandler().setExpansionType<Expansion>();
		getExpansionHandler().setEncryptionKey({}, dontSendNotification);
	}
	else if (expType == "Full")
	{
		if(HISE_GET_PREPROCESSOR(this, HISE_USE_UNLOCKER_FOR_EXPANSIONS))
		{
			getExpansionHandler().setExpansionType<FullInstrumentExpansion>();
		}
		else
		{
			auto key = dynamic_cast<GlobalSettingManager*>(this)->getSettingsObject().getSetting(HiseSettings::Project::EncryptionKey).toString();

			if (key.isNotEmpty())
			{
				getExpansionHandler().setEncryptionKey(key);
				getExpansionHandler().setExpansionType<FullInstrumentExpansion>();
			}

			else
			{
				PresetHandler::showMessageWindow("Can't initialise full expansions", "You need to specify the encryption key in the Project settings in order to use **Full** expansions", PresetHandler::IconType::Error);
				getExpansionHandler().setExpansionType<ExpansionHandler::Disabled>();
			}
		}
	}
	else if (expType == "Encrypted")
	{
		auto key = dynamic_cast<GlobalSettingManager*>(this)->getSettingsObject().getSetting(HiseSettings::Project::EncryptionKey).toString();
		
		getExpansionHandler().setExpansionType<ScriptEncryptedExpansion>();
		getExpansionHandler().setEncryptionKey(key, dontSendNotification);
	}

	getExpansionHandler().resetAfterProjectSwitch();
}

void BackendProcessor::handleEditorData(bool save)
{
#if IS_STANDALONE_APP
	File jsonFile = NativeFileHandler::getAppDataDirectory(nullptr).getChildFile("editorData.json");

	if (save)
	{
		if (editorInformation.isObject())
			jsonFile.replaceWithText(JSON::toString(editorInformation));
		else
			jsonFile.deleteFile();
	}
	else
	{
		editorInformation = JSON::parse(jsonFile);
	}
#else
		ignoreUnused(save);
#endif

		
}

void BackendProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
    

#if HISE_INCLUDE_PROFILING_TOOLKIT
	if(getDebugSession().isMidiTriggerEnabled())
	{
		if(!midiMessages.isEmpty())
		{
			MidiBuffer::Iterator iter(midiMessages);

			MidiMessage m;
			int pos;

			auto before = numPressedKeys;

			while(iter.getNextEvent(m, pos))
			{
				if(m.isNoteOn())
					++numPressedKeys;

				if(m.isNoteOff())
					numPressedKeys = jmax(0, numPressedKeys - 1);
			}

			if(before == 0 && numPressedKeys > 0)
			{
				// I know what I'm doing here...
				MainController::ScopedBadBabysitter sbs(this);

				// Cause the recording to start synchronously, let's live with the CPU peak
				MessageManagerLock mm;
				

				getDebugSession().startRecording(-1.0, &getDebugSession());
			}
			else if (before != 0 && numPressedKeys == 0)
			{
				getDebugSession().stopRecording();
			}
		}
	}
#endif

	TRACE_DSP();

	if(externalClockSim.bypassed)
	{
		processBlockBypassed(buffer, midiMessages);
		return;
	}
	
#if !HISE_BACKEND_AS_FX
	buffer.clear();
#endif


	handleLatencyCheck(buffer);

    auto processChunk = [this](float** channels, AudioSampleBuffer& original, MidiBuffer& mb, int offset, int numThisTime)
    {
        for (int i = 0; i < original.getNumChannels(); i++)
            channels[i] = original.getWritePointer(i, offset);

        MidiBuffer chunkMidiBuffer;
        chunkMidiBuffer.addEvents(mb, offset, numThisTime, -offset);

        AudioSampleBuffer chunk(channels, original.getNumChannels(), numThisTime);

#if IS_STANDALONE_APP
		externalClockSim.addTimelineData(chunk, chunkMidiBuffer);
#endif

        getDelayedRenderer().processWrapped(chunk, chunkMidiBuffer);
    };
    
#if IS_STANDALONE_APP
    setPlayHead(&externalClockSim);
    
    auto numBeforeWrap = externalClockSim.getLoopBeforeWrap(buffer.getNumSamples());
    
	// we need to align the loop points to the raster 
	numBeforeWrap -= numBeforeWrap % HISE_EVENT_RASTER;

    if(numBeforeWrap != 0)
    {
        auto numAfterWrap = buffer.getNumSamples() - numBeforeWrap;
        float* channels[HISE_NUM_PLUGIN_CHANNELS];

        processChunk(channels, buffer, midiMessages, 0, numBeforeWrap);
        
        externalClockSim.process(numBeforeWrap);
        
        if(numAfterWrap > 0)
            processChunk(channels, buffer, midiMessages, numBeforeWrap, numAfterWrap);
        
        externalClockSim.process(numAfterWrap);
        
		externalClockSim.sendLoopMessage();

        return;
    }
    
#endif
    
    
    
	if (isUsingDynamicBufferSize())
	{
		int numTodo = buffer.getNumSamples();
		int pos = 0;

		while (numTodo > 0)
		{
			// I'm sure that's how it looks inside there...
			int fruityLoopsBufferSize = Random::getSystemRandom().nextInt({ numTodo / 3, numTodo + 1 });
			
			if (fruityLoopsBufferSize == 0)
				continue;

			if (numTodo < 8)
				fruityLoopsBufferSize = numTodo;

			fruityLoopsBufferSize = jlimit(0, numTodo, fruityLoopsBufferSize);

			

			float* channels[HISE_NUM_PLUGIN_CHANNELS];

            
            
            processChunk(channels, buffer, midiMessages, pos, fruityLoopsBufferSize);

			numTodo -= fruityLoopsBufferSize;
			pos += fruityLoopsBufferSize;
		}
	}
	else
	{
#if IS_STANDALONE_APP
		externalClockSim.addTimelineData(buffer, midiMessages);
#endif

		ScopedAnalyser sa(this, nullptr, buffer, buffer.getNumSamples());

#if IS_STANDALONE_APP
		if(!pluginParameterRamp.processBlock(buffer, midiMessages, processChunk))
			getDelayedRenderer().processWrapped(buffer, midiMessages);
#else
		getDelayedRenderer().processWrapped(buffer, midiMessages);
#endif
			
		
#if IS_STANDALONE_APP
		externalClockSim.addPostTimelineData(buffer, midiMessages);
#endif
	}

#if IS_STANDALONE_APP
    externalClockSim.process(buffer.getNumSamples());
#endif

	handlePostLatencyCheck(buffer);
};

void BackendProcessor::processBlockBypassed(AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
	buffer.clear();
	midiMessages.clear();
}

void BackendProcessor::handleControllersForMacroKnobs(const MidiBuffer &/*midiMessages*/)
{
	
}


void BackendProcessor::prepareToPlay(double newSampleRate, int samplesPerBlock)
{
    externalClockSim.prepareToPlay(newSampleRate);
    
	setRateAndBufferSizeDetails(newSampleRate, samplesPerBlock);
 
	handleLatencyInPrepareToPlay(newSampleRate);

	getDelayedRenderer().prepareToPlayWrapped(newSampleRate, samplesPerBlock);
}

void BackendProcessor::releaseResources()
{
		
};

void BackendProcessor::checkLatency()
{
	getKillStateHandler().killVoicesAndCall(getMainSynthChain(), [](Processor* p)
	{
		auto bp = dynamic_cast<BackendProcessor*>(p->getMainController());

		bp->latencyCheckState = LatencyCheckState::WaitingForKillCounter;
		bp->killCounter = (int)bp->getMainSynthChain()->getSampleRate() * 0.5;

		return SafeFunctionCall::OK;
	}, KillStateHandler::TargetThread::SampleLoadingThread);
}

void BackendProcessor::getStateInformation(MemoryBlock &destData)
{
	if(forceSaveAsPluginState)
	{
		MainController::savePluginState(destData, 0);
	}
	else
	{
		MemoryOutputStream output(destData, false);

		ValueTree v = synthChain->exportAsValueTree();

		v.setProperty("ProjectRootFolder", GET_PROJECT_HANDLER(synthChain).getWorkDirectory().getFullPathName(), nullptr);

		if (auto root = dynamic_cast<BackendRootWindow*>(getActiveEditor()))
			root->saveInterfaceData();

		v.setProperty("InterfaceData", JSON::toString(editorInformation, true, DOUBLE_TO_STRING_DIGITS), nullptr);
		v.writeToStream(output);
	}
}

void BackendProcessor::handleLatencyCheck(AudioSampleBuffer& buffer)
{
	if(latencyCheckState == LatencyCheckState::WaitingForKillCounter)
	{
		killCounter -= buffer.getNumSamples();

		if(killCounter < 0)
		{
			killCounter = 0;
			latencyCheckState = LatencyCheckState::WaitingForProcessBlock;
		}
	}

	if(latencyCheckState == LatencyCheckState::WaitingForProcessBlock)
	{
		reportedLatency = 0.0;
		buffer.setSample(0, 0, 1.0f);
		buffer.setSample(0, 1, 1.0f);
	}
}

void BackendProcessor::handlePostLatencyCheck(AudioSampleBuffer& buffer)
{
	if(latencyCheckState == LatencyCheckState::WaitingForProcessBlock)
	{
		latencyCheckState = LatencyCheckState::WaitingForImpulse;
		reportedLatency = 0.0;
	}

	if(latencyCheckState == LatencyCheckState::WaitingForImpulse)
	{
		if(buffer.getMagnitude(0, 0, buffer.getNumSamples()) > 0.01f)
		{
			float maxPeak = 0.0f;
			float indexOfPeak = 0.0f;

			for(int i = 0; i < buffer.getNumSamples(); i++)
			{
				auto value = buffer.getSample(0, i);
				if(value > maxPeak)
				{
					maxPeak = value;
					indexOfPeak = i;
				}
			}

			reportedLatency += (double)indexOfPeak;

			latencyCheckState = LatencyCheckState::Done;

			MessageManager::callAsync([this]()
			{
				PresetHandler::showMessageWindow("Latency detected", "The latency of the processing chain is:  \n>`" + String((int)reportedLatency) + "` samples.", PresetHandler::IconType::Info);
				latencyCheckState = LatencyCheckState::Idle;
				reportedLatency = 0;
			});
		}
		else
		{
			reportedLatency += buffer.getNumSamples();
		}

		buffer.clear();
	}
}

void BackendProcessor::logMessage(const String& message, bool isCritical)
{
	if (isCritical)
	{
		debugError(getMainSynthChain(), message);
	}
	else
		debugToConsole(getMainSynthChain(), message);
}

void BackendProcessor::setStateInformation(const void *data, int sizeInBytes)
{
	tempLoadingData.setSize(sizeInBytes);

	tempLoadingData.copyFrom(data, 0, sizeInBytes);

	

	auto f = [](Processor* p)
	{
		auto bp = dynamic_cast<BackendProcessor*>(p->getMainController());

		auto& tmp = bp->tempLoadingData;

		ValueTree v = ValueTree::readFromData(tmp.getData(), tmp.getSize());

		String fileName = v.getProperty("ProjectRootFolder", String());

		if (fileName.isNotEmpty())
		{
			File root(fileName);
			if (root.exists() && root.isDirectory())
			{
				GET_PROJECT_HANDLER(p).setWorkingProject(root);

				bp->getSettingsObject().refreshProjectData();

			}
		}

		p->getMainController()->loadPresetFromValueTree(v);

		

		bp->editorInformation = JSON::parse(v.getProperty("InterfaceData", ""));

		tmp.reset();

		return SafeFunctionCall::OK;
	};

	getKillStateHandler().killVoicesAndCall(getMainSynthChain(), f, MainController::KillStateHandler::TargetThread::SampleLoadingThread);
}

AudioProcessorEditor* BackendProcessor::createEditor()
{
#if USE_WORKBENCH_EDITOR
	return new SnexWorkbenchEditor(this);
#else
	auto d = new BackendRootWindow(this, editorInformation);
    docWindow = d;
    return d;
#endif
}





juce::File BackendProcessor::getDatabaseRootDirectory() const
{
	if (databaseRoot.isDirectory())
		return databaseRoot;

	auto docRepo = getSettingsObject().getSetting(HiseSettings::Documentation::DocRepository).toString();

	File root;

	if (File::isAbsolutePath(docRepo))
	{
		auto f = File(docRepo);

		if (f.isDirectory())
			root = f;
	}

	return root;
}

hise::BackendProcessor* BackendProcessor::getDocProcessor()
{
    return this;
}

hise::BackendRootWindow* BackendProcessor::getDocWindow()
{
    return docWindow;
    
}

juce::Component* BackendProcessor::getRootComponent()
{
	return dynamic_cast<Component*>(getDocWindow());
}

hise::JavascriptProcessor* BackendProcessor::createInterface(int width, int height)
{
	auto midiChain = dynamic_cast<MidiProcessorChain*>(getMainSynthChain()->getChildProcessor(ModulatorSynthChain::MidiProcessor));
	auto s = getMainSynthChain()->getMainController()->createProcessor(midiChain->getFactoryType(), "ScriptProcessor", "Interface");
	auto jsp = dynamic_cast<JavascriptProcessor*>(s);

	String code = "Content.makeFrontInterface(" + String(width) + ", " + String(width) + ");";

	jsp->getSnippet(0)->replaceContentAsync(code, false);
	jsp->compileScript();

	midiChain->getHandler()->add(s, nullptr);

	midiChain->setEditorState(Processor::EditorState::Visible, true);
	s->setEditorState(Processor::EditorState::Folded, true);

	return jsp;
}

void BackendProcessor::setEditorData(var editorState)
{
	editorInformation = editorState;
}





void BackendProcessor::pushToAnalyserBuffer(AnalyserInfo::Ptr info, bool post, const AudioSampleBuffer& buffer, int numSamples)
{
	jassert(info != nullptr);

	if(auto sl = SimpleReadWriteLock::ScopedTryReadLock(postAnalyserLock))
	{
		auto rb = info->ringBuffer[(int)post];

		if(rb != nullptr)
		{
			if(!post)
			{
				for(int i = 0; i < 128; i++)
				{
					if(getKeyboardState().isNoteOn(1, i))
					{
						currentNoteNumber = i;
						break;
					}
				}
			}

			if(!post && (bool)rb->getPropertyObject()->getProperty(scriptnode::PropertyIds::IsProcessingHiseEvent))
			{
				if(currentNoteNumber != -1 && currentNoteNumber != info->lastNoteNumber)
				{
					info->lastNoteNumber = currentNoteNumber;
					auto midiFreq = MidiMessage::getMidiNoteInHertz(info->lastNoteNumber);
					auto cycleLength = getMainSynthChain()->getSampleRate() / midiFreq;
					info->ringBuffer[0]->setMaxLength(cycleLength);
					info->ringBuffer[1]->setMaxLength(cycleLength);
				}
			}

			if(rb->getPropertyObject()->getProperty("ShowCpuUsage"))
			{
				auto numMsForBuffer = ((double)buffer.getNumSamples() / getMainSynthChain()->getSampleRate()) * 1000.0;
				auto usage = jlimit(0.0, 1.0, info->duration / numMsForBuffer);

				rb->write(post ? usage : (getCpuUsage() * 0.01), numSamples);
			}
			else
			{
				jassert(isPositiveAndBelow(info->currentlyAnalysedProcessor.second * 2, buffer.getNumChannels()+1));
				const float* data[2] = { buffer.getReadPointer(info->currentlyAnalysedProcessor.second * 2), buffer.getReadPointer(info->currentlyAnalysedProcessor.second * 2 +1) };
				rb->write(data, 2, buffer.getNumSamples());
			}
		}
	}
}

AnalyserInfo::Ptr BackendProcessor::getAnalyserInfoForProcessor(Processor* p)
{
	if(auto sl = SimpleReadWriteLock::ScopedTryReadLock(postAnalyserLock))
	{
		for(auto i: currentAnalysers)
		{
			if(i->currentlyAnalysedProcessor.first == p)
				return i;
		}
	}

	return nullptr;
}

Result BackendProcessor::setAnalysedProcessor(AnalyserInfo::Ptr newInfo, bool add)
{
	SimpleReadWriteLock::ScopedWriteLock sl(postAnalyserLock);

	if(add)
	{
		for(auto i: currentAnalysers)
		{
			if(i->currentlyAnalysedProcessor.first == newInfo->currentlyAnalysedProcessor.first)
				return Result::fail("Another analyser is already assigned to the module " + i->currentlyAnalysedProcessor.first->getId());
		}
		currentAnalysers.add(newInfo);
	}
			
	else
		currentAnalysers.removeObject(newInfo);

	return Result::ok();
}
} // namespace hise


