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
*   which also must be licenced for commercial applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

namespace hise { using namespace juce;

ProcessorWithScriptingContent::~ProcessorWithScriptingContent()
{
	if (content != nullptr)
	{
		content->removeAllScriptComponents();
	}
	
}

void ProcessorWithScriptingContent::suspendStateChanged(bool shouldBeSuspended)
{
	getScriptingContent()->suspendPanelTimers(shouldBeSuspended);
}

void ProcessorWithScriptingContent::setControlValue(int index, float newValue)
{
	jassert(content.get() != nullptr);

	if (content != nullptr && index < content->getNumComponents())
	{
		ScriptingApi::Content::ScriptComponent *c = content->getComponent(index);

		if (auto lc = c->getLinkedComponent())
		{
			c = lc;
		}

		if (c != nullptr)
		{
			c->setValue(newValue);

			if (auto b = dynamic_cast<ScriptingApi::Content::ScriptButton*>(c))
			{
				if (int group = b->getScriptObjectProperty(ScriptingApi::Content::ScriptButton::Properties::radioGroup))
				{
					if (newValue > 0.5f)
					{
						for (int i = 0; i < content->getNumComponents(); i++)
						{
							if (i == index) continue;

							if (auto other = dynamic_cast<ScriptingApi::Content::ScriptButton*>(content->getComponent(i)))
							{
								if ((int)other->getScriptObjectProperty(ScriptingApi::Content::ScriptButton::Properties::radioGroup) == group)
									other->setValue(0);
							}
						}
					}
				}
			}

#if USE_FRONTEND

			if (c->isAutomatable() &&
				c->getScriptObjectProperty(ScriptingApi::Content::ScriptComponent::Properties::isPluginParameter) &&
				getMainController_()->getPluginParameterUpdateState())
			{
				dynamic_cast<PluginParameterAudioProcessor*>(getMainController_())->setScriptedPluginParameter(c->getName(), newValue);
			}

#endif

			controlCallback(c, newValue);
		}
	}
}

float ProcessorWithScriptingContent::getControlValue(int index) const
{
	if (content != nullptr && index < content->getNumComponents())
		return content->getComponent(index)->getValue();

	else return 1.0f;
}

void ProcessorWithScriptingContent::controlCallback(ScriptingApi::Content::ScriptComponent *component, var controllerValue)
{
	if (thisAsJavascriptProcessor == nullptr)
		thisAsJavascriptProcessor = dynamic_cast<JavascriptProcessor*>(this);

	Processor* thisAsProcessor = dynamic_cast<Processor*>(this);

	

    if (component->isConnectedToMacroControll())
    {
        
        float v = jlimit<float>(0.0, 127.0, (float)component->getValue());
        
        component->setMacroRecursionProtection(true);
        
        getMainController_()->getMainSynthChain()->setMacroControl(component->getMacroControlIndex(), v, sendNotification);
        
        component->setMacroRecursionProtection(false);
    }
	else if (component->isConnectedToProcessor())
	{
		float v = (float)controllerValue;
		FloatSanitizers::sanitizeFloatNumber(v);
		
		auto index = component->getConnectedParameterIndex();

		if (index == -2) // intensity
		{
			if (auto mod = dynamic_cast<Modulation*>(component->getConnectedProcessor()))
			{
				mod->setIntensity(v);
				BACKEND_ONLY(component->getConnectedProcessor()->sendChangeMessage());
			}
		}
		else if (index == -3) // bypassed
		{
			component->getConnectedProcessor()->setBypassed(v > 0.5f, sendNotification);
			BACKEND_ONLY(component->getConnectedProcessor()->sendChangeMessage());
		}
		else if (index == -4) // enabled
		{
			component->getConnectedProcessor()->setBypassed(v < 0.5f, sendNotification);
			BACKEND_ONLY(component->getConnectedProcessor()->sendChangeMessage());

		}
		else
		{
			component->getConnectedProcessor()->setAttribute(index, v, sendNotification);
		}

		if (auto sp = dynamic_cast<ScriptingApi::Content::ScriptPanel*>(component))
		{
			sp->repaintWrapped();
		}
	}
	else if (auto callback = component->getCustomControlCallback())
	{
		if (MessageManager::getInstance()->isThisTheMessageThread())
		{
			auto f = [component, controllerValue](JavascriptProcessor* p)
			{
				Result& r = p->lastResult;
				dynamic_cast<ProcessorWithScriptingContent*>(p)->customControlCallbackIdle(component, controllerValue, r);
				return r;
			};

			getMainController_()->getJavascriptThreadPool().addJob(JavascriptThreadPool::Task::HiPriorityCallbackExecution,
				dynamic_cast<JavascriptProcessor*>(this),
				f);
		}
		else
		{
			customControlCallbackIdle(component, controllerValue, thisAsJavascriptProcessor->lastResult);
		}

	}
	else
	{
		int callbackIndex = getControlCallbackIndex();

		getMainController_()->getDebugLogger().logParameterChange(thisAsJavascriptProcessor, component, controllerValue);

		JavascriptProcessor::SnippetDocument* onControlCallback = thisAsJavascriptProcessor->getSnippet(callbackIndex);

		if (!onControlCallback->isSnippetEmpty())
		{
			if (MessageManager::getInstance()->isThisTheMessageThread())
			{
				auto f = [component, controllerValue](JavascriptProcessor* p)
				{
					Result& r = p->lastResult;
					dynamic_cast<ProcessorWithScriptingContent*>(p)->defaultControlCallbackIdle(component, controllerValue, r);
					return r;
				};

				getMainController_()->getJavascriptThreadPool().addJob(JavascriptThreadPool::Task::HiPriorityCallbackExecution, 
																	   dynamic_cast<JavascriptProcessor*>(this), 
																	   f);
			}
			else
			{
				defaultControlCallbackIdle(component, controllerValue, thisAsJavascriptProcessor->lastResult);
			}
		}
	}

	if (MessageManager::getInstance()->isThisTheMessageThread())
		thisAsProcessor->sendSynchronousChangeMessage();
	else
		thisAsProcessor->sendChangeMessage();
}

void ProcessorWithScriptingContent::defaultControlCallbackIdle(ScriptingApi::Content::ScriptComponent *component, const var& controllerValue, Result& r)
{
	ScopedValueSetter<bool> objectConstructorSetter(allowObjectConstructors, true);

	int callbackIndex = getControlCallbackIndex();

	if (auto scriptEngine = thisAsJavascriptProcessor->getScriptEngine())
	{
		LockHelpers::SafeLock sl(getMainController_(), LockHelpers::ScriptLock);

		scriptEngine->maximumExecutionTime = RelativeTime(3.0);

#if ENABLE_SCRIPTING_BREAKPOINTS
		thisAsJavascriptProcessor->breakpointWasHit(-1);
#endif

		scriptEngine->setCallbackParameter(callbackIndex, 0, component);
		scriptEngine->setCallbackParameter(callbackIndex, 1, controllerValue);
		scriptEngine->executeCallback(callbackIndex, &r);

	}

	BACKEND_ONLY(if (!r.wasOk()) debugError(dynamic_cast<Processor*>(this), r.getErrorMessage()));
}



void ProcessorWithScriptingContent::customControlCallbackIdle(ScriptingApi::Content::ScriptComponent *component, const var& controllerValue, Result& r)
{
	ScopedValueSetter<bool> objectConstructorSetter(allowObjectConstructors, true);
	

	getMainController_()->getDebugLogger().logParameterChange(thisAsJavascriptProcessor, component, controllerValue);

	auto callback = component->getCustomControlCallback();

	var fVar(callback);
	var args[2] = { var(component), controllerValue };

	if (auto scriptEngine = thisAsJavascriptProcessor->getScriptEngine())
	{
		LockHelpers::SafeLock sl(getMainController_(), LockHelpers::ScriptLock);

		scriptEngine->maximumExecutionTime = RelativeTime(3.0);

#if ENABLE_SCRIPTING_BREAKPOINTS
		thisAsJavascriptProcessor->breakpointWasHit(-1);
#endif

		scriptEngine->executeInlineFunction(fVar, args, &r);
	}

#if USE_BACKEND
	if (!r.wasOk())
	{
		auto inlineF = dynamic_cast<HiseJavascriptEngine::RootObject::InlineFunction::Object*>(fVar.getObject());

		if (inlineF != nullptr)
		{
			debugError(dynamic_cast<Processor*>(this), r.getErrorMessage());
		}
	}
#endif
}

void countChildren(const ValueTree& t, int& numChildren)
{
	numChildren++;

	for (auto child : t)
	{
		countChildren(child, numChildren);
	}
}


int ProcessorWithScriptingContent::getNumScriptParameters() const
{
	if (content != nullptr)
	{
		auto c = content->getContentProperties();
		int numChildren = -1; // the root tree doesn't count

		countChildren(c, numChildren);

		return numChildren;
	}
	else
		return 0;
}

void ProcessorWithScriptingContent::restoreContent(const ValueTree &restoredState)
{
	restoredContentValues = restoredState.getChildWithName("Content");

	if (content.get() != nullptr)
	{
		content->restoreFromValueTree(restoredContentValues);
	}
}

void ProcessorWithScriptingContent::saveContent(ValueTree &savedState) const
{
	if (content.get() != nullptr)
		savedState.addChild(content->exportAsValueTree(), -1, nullptr);
}

FileChangeListener::~FileChangeListener()
{
	watchers.clear();
	masterReference.clear();
}

void FileChangeListener::addFileWatcher(const File &file)
{
	auto p = dynamic_cast<Processor*>(this)->getMainController()->getExternalScriptFile(file);

	watchers.add(p);
}

void FileChangeListener::setFileResult(const File &file, Result r)
{
	for (int i = 0; i < watchers.size(); i++)
	{
		if (file == watchers[i]->getFile())
		{
			watchers[i]->setResult(r);
		}
	}
}

Result FileChangeListener::getWatchedResult(int index)
{
	return watchers[index]->getResult();
}

File FileChangeListener::getWatchedFile(int index) const
{
	if (index < watchers.size())
	{
		return watchers[index]->getFile();

	}
	else return File();
}

CodeDocument& FileChangeListener::getWatchedFileDocument(int index)
{
	if(index < watchers.size())
		return watchers[index]->getFileDocument();

	jassertfalse;
	return emptyDoc;
}

void FileChangeListener::showPopupForFile(int index, int charNumberToDisplay/*=0*/, int /*lineNumberToDisplay=-1*/)
{
#if USE_BACKEND

	auto mc = dynamic_cast<Processor*>(this)->getMainController();
	mc->getCommandManager()->invoke(BackendCommandTarget::MenuViewAddFloatingWindow, false);

	auto rw = GET_BACKEND_ROOT_WINDOW(mc->getConsoleHandler().getMainConsole());

	auto window = rw->getLastPopup();

	window->setName(watchers[index]->getFile().getFileName());
	window->centreWithSize(1000, 800);
	

	auto root = window->getRootFloatingTile();
	
	root->setNewContent(GET_PANEL_NAME(CodeEditorPanel));

	auto editor = dynamic_cast<CodeEditorPanel*>(root->getCurrentFloatingPanel());

	editor->gotoLocation(dynamic_cast<Processor*>(this), watchers[index]->getFile().getFullPathName(), charNumberToDisplay);

#else
	ignoreUnused(index, charNumberToDisplay);
#endif
}

void FileChangeListener::showPopupForFile(const File& f, int charNumberToDisplay /*= 0*/, int lineNumberToDisplay /*= -1*/)
{
	for (int i = 0; i < watchers.size(); i++)
	{
		if (watchers[i]->getFile() == f)
			showPopupForFile(i, charNumberToDisplay, lineNumberToDisplay);
	}
}

void JavascriptProcessor::showPopupForCallback(const Identifier& callback, int /*charNumberToDisplay*/, int /*lineNumberToDisplay*/)
{
#if USE_BACKEND

	auto mc = dynamic_cast<Processor*>(this)->getMainController();
	mc->getCommandManager()->invoke(BackendCommandTarget::MenuViewAddFloatingWindow, false);

	auto rw = GET_BACKEND_ROOT_WINDOW(mc->getConsoleHandler().getMainConsole());
	auto root = rw->getLastPopup()->getRootFloatingTile();

	root->setNewContent(GET_PANEL_NAME(CodeEditorPanel));

	auto editor = dynamic_cast<CodeEditorPanel*>(root->getCurrentFloatingPanel());

	auto jp = dynamic_cast<JavascriptProcessor*>(this);
	
	for (int i = 0; i < jp->getNumSnippets(); i++)
	{
		if (jp->getSnippet(i)->getCallbackName() == callback)
		{
			editor->setContentWithUndo(dynamic_cast<Processor*>(this), i);
			break;
		}
	}

#else
	ignoreUnused(callback);

#endif
}


void JavascriptProcessor::cleanupEngine()
{
	mainController->getScriptComponentEditBroadcaster()->clearSelection(sendNotification);
	scriptEngine = nullptr;
	dynamic_cast<ProcessorWithScriptingContent*>(this)->content = nullptr;
}

void JavascriptProcessor::setCallStackEnabled(bool shouldBeEnabled)
{
	callStackEnabled = shouldBeEnabled;
	
	if (scriptEngine != nullptr)
		scriptEngine->setCallStackEnabled(shouldBeEnabled);
}

ValueTree FileChangeListener::collectAllScriptFiles(ModulatorSynthChain *chainToExport)
{
	Processor::Iterator<JavascriptProcessor> iter(chainToExport);

	ValueTree externalScriptFiles = ValueTree("ExternalScripts");

	while (JavascriptProcessor *sp = iter.getNextProcessor())
	{
		if (sp->isConnectedToExternalFile())
		{
			const String fileNameReference = sp->getConnectedFileReference();
			
			bool exists = false;

			for (int j = 0; j < externalScriptFiles.getNumChildren(); j++)
			{
				if (externalScriptFiles.getChild(j).getProperty("FileName").toString() == fileNameReference)
				{
					exists = true;
					break;
				}
			}

			if (!exists)
			{
				String code;

				sp->mergeCallbacksToScript(code);

				ValueTree script("Script");

				script.setProperty("FileName", fileNameReference, nullptr);
				script.setProperty("Content", code, nullptr);

				externalScriptFiles.addChild(script, -1, nullptr);
			}
		}

        for (int i = 0; i < sp->getNumWatchedFiles(); i++)
		{
			File scriptFile = sp->getWatchedFile(i);

			addFileContentToValueTree(externalScriptFiles, scriptFile, chainToExport);
		}

		Array<File> allScriptFiles;

		auto scriptDirectory = GET_PROJECT_HANDLER(chainToExport).getSubDirectory(ProjectHandler::SubDirectories::Scripts);

		scriptDirectory.findChildFiles(allScriptFiles, File::findFiles, true, "*.js");

		for (auto f : allScriptFiles)
		{
			if (HiseDeviceSimulator::fileNameContainsDeviceWildcard(f))
			{
				addFileContentToValueTree(externalScriptFiles, f, chainToExport);
			}
		}
	}

	return externalScriptFiles;
}

void FileChangeListener::addFileContentToValueTree(ValueTree externalScriptFiles, File scriptFile, ModulatorSynthChain* chainToExport)
{
	String fileName = scriptFile.getRelativePathFrom(GET_PROJECT_HANDLER(chainToExport).getSubDirectory(ProjectHandler::SubDirectories::Scripts));

	// Wow, much cross-platform, very OSX, totally Windows
	fileName = fileName.replace("\\", "/");

	File globalScriptFolder = PresetHandler::getGlobalScriptFolder(chainToExport);

	if (globalScriptFolder.isDirectory() && scriptFile.isAChildOf(globalScriptFolder))
	{
		fileName = "{GLOBAL_SCRIPT_FOLDER}" + scriptFile.getRelativePathFrom(globalScriptFolder);
	}

	for (int j = 0; j < externalScriptFiles.getNumChildren(); j++)
	{
		if (externalScriptFiles.getChild(j).getProperty("FileName").toString() == fileName)
		{
			return;
		}
	}

	String fileContent = scriptFile.loadFileAsString();
	ValueTree script("Script");

	script.setProperty("FileName", fileName, nullptr);
	script.setProperty("Content", fileContent, nullptr);

	externalScriptFiles.addChild(script, -1, nullptr);
}

JavascriptProcessor::JavascriptProcessor(MainController *mc) :
	mainController(mc),
	scriptEngine(new HiseJavascriptEngine(this)),
	lastCompileWasOK(false),
	currentCompileThread(nullptr),
	lastResult(Result::ok()),
	callStackEnabled(mc->isCallStackEnabled()),
	repaintDispatcher(mc),
	repaintUpdater(repaintDispatcher)
{
	allInterfaceData = ValueTree("UIData");
	auto defaultContent = ValueTree("ContentProperties");
	defaultContent.setProperty("DeviceType", "Desktop", nullptr);
	allInterfaceData.addChild(defaultContent, -1, nullptr);

}



JavascriptProcessor::~JavascriptProcessor()
{
	deleteAllPopups();
	
	scriptEngine = nullptr;
}


void JavascriptProcessor::fileChanged()
{
	compileScript();
}




void JavascriptProcessor::clearExternalWindows()
{
	if (callbackPopups.size() != 0)
	{
		for (int i = 0; i < callbackPopups.size(); i++)
		{
			if (callbackPopups[i].getComponent() != nullptr)
			{
				callbackPopups[i]->closeButtonPressed();
			}
		}

		callbackPopups.clear();
	}
}

JavascriptProcessor::SnippetResult JavascriptProcessor::compileInternal()
{
	LockHelpers::freeToGo(dynamic_cast<Processor*>(this)->getMainController());

	ProcessorWithScriptingContent* thisAsScriptBaseProcessor = dynamic_cast<ProcessorWithScriptingContent*>(this);



	ScriptingApi::Content* content = thisAsScriptBaseProcessor->getScriptingContent();

	const bool saveThisContent = lastCompileWasOK && content != nullptr && !useStoredContentData;

	if (saveThisContent) 
		thisAsScriptBaseProcessor->restoredContentValues = content->exportAsValueTree();

	auto thisAsProcessor = dynamic_cast<Processor*>(this);

	

	scriptEngine->clearDebugInformation();

	content->beginInitialization();

	setupApi();

	content = thisAsScriptBaseProcessor->getScriptingContent();

	scriptEngine->setIsInitialising(true);

	if (cycleReferenceCheckEnabled)
		scriptEngine->setUseCycleReferenceCheckForNextCompilation();

	thisAsScriptBaseProcessor->allowObjectConstructors = true;

	const static Identifier onInit("onInit");

	for (int i = 0; i < getNumSnippets(); i++)
	{
		getSnippet(i)->checkIfScriptActive();

		if (!getSnippet(i)->isSnippetEmpty())
		{
			const Identifier callbackId = getSnippet(i)->getCallbackName();

#if ENABLE_SCRIPTING_BREAKPOINTS
			Array<HiseJavascriptEngine::Breakpoint> breakpointsForCallback;

			for (int k = 0; k < breakpoints.size(); k++)
			{
				if (breakpoints[k].snippetId == callbackId || breakpoints[k].snippetId.toString().startsWith("File_"))
					breakpointsForCallback.add(breakpoints[k]);
			}

			if (!breakpointsForCallback.isEmpty())
				scriptEngine->setBreakpoints(breakpointsForCallback);


#endif

			lastResult = scriptEngine->execute(getSnippet(i)->getSnippetAsFunction(), callbackId == onInit);

			if (!lastResult.wasOk())
			{
				debugError(thisAsProcessor, lastResult.getErrorMessage());

				content->endInitialization();
				scriptEngine->setIsInitialising(false);
				thisAsScriptBaseProcessor->allowObjectConstructors = false;

				// Check the rest of the snippets or they will be deleted on failed compile...
				for (int j = i; j < getNumSnippets(); j++)
				{
					getSnippet(j)->checkIfScriptActive();
				}

				lastCompileWasOK = false;

				scriptEngine->rebuildDebugInformation();
				return SnippetResult(lastResult, i);

			}
		}
	}

	scriptEngine->rebuildDebugInformation();

	try
	{
		content->restoreAllControlsFromPreset(thisAsScriptBaseProcessor->restoredContentValues);
	}
	catch (String& s)
	{
		debugError(thisAsProcessor, "Error at content restoring: " + s);
	}

	useStoredContentData = false; // From now on it's normal;

	content->endInitialization();

	scriptEngine->setIsInitialising(false);

	thisAsScriptBaseProcessor->allowObjectConstructors = false;

	lastCompileWasOK = true;

	if (thisAsProcessor->getMainController()->getScriptComponentEditBroadcaster()->isBeingEdited(thisAsProcessor))
	{
		debugToConsole(thisAsProcessor, "Compiled OK");
	}

	postCompileCallback();

	return SnippetResult(Result::ok(), getNumSnippets());
}

void JavascriptProcessor::compileScript(const ResultFunction& rf /*= ResultFunction()*/)
{
	auto f = [rf](Processor* p)
	{
		auto jp = dynamic_cast<JavascriptProcessor*>(p);

		auto result = jp->compileInternal();

		auto postCompile = [result, rf](Dispatchable* obj)
		{
			

			auto jp = static_cast<JavascriptProcessor*>(obj);
			jp->stuffAfterCompilation(result);
			
			if(rf)
				rf(result);

			return Dispatchable::Status::OK;
		};

		jp->mainController->getLockFreeDispatcher().callOnMessageThreadAfterSuspension(jp, postCompile);

		return SafeFunctionCall::OK;
	};

	

	mainController->getKillStateHandler().killVoicesAndCall(dynamic_cast<Processor*>(this), f, MainController::KillStateHandler::ScriptingThread);
}


void JavascriptProcessor::setupApi()
{
	clearFileWatchers();

	dynamic_cast<ProcessorWithScriptingContent*>(this)->getScriptingContent()->cleanJavascriptObjects();

	scriptEngine = new HiseJavascriptEngine(this);

	scriptEngine->addBreakpointListener(this);

	scriptEngine->setCallStackEnabled(callStackEnabled);

	scriptEngine->maximumExecutionTime = RelativeTime(mainController->getCompileTimeOut());

	registerApiClasses();
	
	scriptEngine->registerNativeObject("Globals", mainController->getGlobalVariableObject());
	scriptEngine->registerGlobalStorge(mainController->getGlobalVariableObject());

	registerCallbacks();
}


void JavascriptProcessor::registerCallbacks()
{
	const Processor* p = dynamic_cast<Processor*>(this);

	const double bufferTime = (double)p->getLargestBlockSize() / p->getSampleRate() * 1000.0;

	for (int i = 0; i < getNumSnippets(); i++)
	{
		scriptEngine->registerCallbackName(getSnippet(i)->getCallbackName(), getSnippet(i)->getNumArgs(), bufferTime);
	}
}


JavascriptProcessor::SnippetDocument * JavascriptProcessor::getSnippet(const Identifier& id)
{
	for (int i = 0; i < getNumSnippets(); i++)
	{
		if (getSnippet(i)->getCallbackName() == id) return getSnippet(i);
	}

	return nullptr;
}

const JavascriptProcessor::SnippetDocument * JavascriptProcessor::getSnippet(const Identifier& id) const
{
	for (int i = 0; i < getNumSnippets(); i++)
	{
		if (getSnippet(i)->getCallbackName() == id) return getSnippet(i);
	}

	return nullptr;
}

#if 0
void JavascriptProcessor::DelayedPositionUpdater::scriptComponentChanged(ReferenceCountedObject *componentThatWasChanged, Identifier idThatWasChanged)
{
	if (currentScriptComponent != nullptr)
	{
		JavascriptCodeEditor::Helpers::changePositionOfComponent(currentScriptComponent, newPosition.x, newPosition.y);
	}

	auto sc = dynamic_cast<ScriptComponent*>(componentThatWasChanged);

	if (sc != nullptr)
	{
		JavascriptCodeEditor::Helpers::gotoAndReturnDocumentWithDefinition(dynamic_cast<Processor*>(p), sc);
	}

	currentScriptComponent = sc;
}

void JavascriptProcessor::DelayedPositionUpdater::scriptComponentUpdated(Identifier idThatWasChanged, var newValue)
{
	if (currentScriptComponent != nullptr)
	{
		static Identifier xId("x");
		static const Identifier yId("y");

		if (idThatWasChanged == xId)
		{
			int y = currentScriptComponent->getScriptObjectProperty(ScriptComponent::Properties::y);
			newPosition = Point<int>((int)newValue, y);
		}
		else if (idThatWasChanged == yId)
		{
			int x = currentScriptComponent->getScriptObjectProperty(ScriptComponent::Properties::x);
			newPosition = Point<int>(x, (int)newValue);
		}
	}
}
#endif



void JavascriptProcessor::saveScript(ValueTree &v) const
{
	saveComplexDataTypeAmounts(v);

	String x;

	if (isConnectedToExternalFile())
	{
		x = "{EXTERNAL_SCRIPT}" + connectedFileReference;
	}
	else
	{
		mergeCallbacksToScript(x);
	}

	v.addChild(allInterfaceData, -1, nullptr);

	v.setProperty("Script", x, nullptr);
}

void JavascriptProcessor::restoreScript(const ValueTree &v)
{
	restoreComplexDataTypes(v);

	String x = v.getProperty("Script", String());

	auto contentPropertyChild = v.getChildWithName("ContentProperties");
	auto uiData = v.getChildWithName("UIData");

	static const Identifier deviceType("DeviceType");

	if (contentPropertyChild.isValid())
	{
		allInterfaceData = ValueTree("UIData");

		auto deviceName = HiseDeviceSimulator::getDeviceName((int)HiseDeviceSimulator::DeviceType::Desktop);

		auto copy = contentPropertyChild.createCopy();

		ScriptingApi::Content::Helpers::sanitizeNumberProperties(copy);

		copy.setProperty(deviceType, deviceName, nullptr);

		allInterfaceData.addChild(copy, -1, nullptr);

		restoreInterfaceData(copy);
	}
	if (uiData.isValid())
	{
		allInterfaceData = uiData;

		ScriptingApi::Content::Helpers::sanitizeNumberProperties(allInterfaceData);

		auto deviceIndex = (int)HiseDeviceSimulator::getDeviceType();

		setDeviceTypeForInterface(deviceIndex);
	}

	if (x.startsWith("{EXTERNAL_SCRIPT}"))
	{
		String fileReference = x.fromFirstOccurrenceOf("{EXTERNAL_SCRIPT}", false, false);

		setConnectedFile(fileReference, false);
	}
	else
	{
		parseSnippetsFromString(x, true);
	}

	if (Processor* parent = ProcessorHelpers::findParentProcessor(dynamic_cast<Processor*>(this), true))
	{
		if (parent->getMainController()->shouldSkipCompiling())
		{
			dynamic_cast<ProcessorWithScriptingContent*>(this)->restoredContentValues = v.getChildWithName("Content");
			useStoredContentData = true;
		}
		else
		{
			compileScript();
		}
	}
}

void JavascriptProcessor::setDeviceTypeForInterface(int deviceIndex)
{
	static const Identifier deviceType("DeviceType");

	auto deviceName = HiseDeviceSimulator::getDeviceName(deviceIndex);

	auto ct = allInterfaceData.getChildWithProperty(deviceType, deviceName);

	if (!ct.isValid())
		ct = allInterfaceData.getChild(0);

	jassert(ct.isValid());

	restoreInterfaceData(ct);
}



ValueTree JavascriptProcessor::getContentPropertiesForDevice(int deviceIndex)
{
	static const Identifier deviceType("DeviceType");

	auto desktopName = HiseDeviceSimulator::getDeviceName((int)HiseDeviceSimulator::DeviceType::Desktop);
	auto deviceName = HiseDeviceSimulator::getDeviceName(deviceIndex);

	auto ct = allInterfaceData.getChildWithProperty(deviceType, deviceName);

	if (!ct.isValid())
	{
		ct = allInterfaceData.getChildWithProperty(deviceType, desktopName);
	}

	jassert(ct.isValid());

	return ct;
}

bool JavascriptProcessor::hasUIDataForDeviceType(int type) const
{
	static const Identifier deviceType("DeviceType");

	auto deviceName = HiseDeviceSimulator::getDeviceName(type);

	auto ct = allInterfaceData.getChildWithProperty(deviceType, deviceName);

	return ct.isValid();
}

void JavascriptProcessor::createUICopyFromDesktop()
{
	static const Identifier deviceType("DeviceType");

	auto desktopName = HiseDeviceSimulator::getDeviceName((int)HiseDeviceSimulator::DeviceType::Desktop);
	auto deviceName = HiseDeviceSimulator::getDeviceName();

	if (desktopName == deviceName)
	{
		jassertfalse;
		return;
	}

	auto ct = allInterfaceData.getChildWithProperty(deviceType, deviceName);

	if (ct.isValid())
	{
		if (!PresetHandler::showYesNoWindow("Overwrite existing data", "There is already a UI model for this device type.\nThe current data will be merciless overwritten", PresetHandler::IconType::Warning))
		{
			return;
		}
	}
	
	ValueTree copy = allInterfaceData.getChildWithProperty(deviceType, desktopName).createCopy();

	jassert(copy.isValid());

	copy.setProperty(deviceType, deviceName, nullptr);

	allInterfaceData.addChild(copy, -1, nullptr);

	restoreInterfaceData(copy);
}



void JavascriptProcessor::restoreInterfaceData(ValueTree propertyData)
{
	auto buildComponents = !mainController->shouldSkipCompiling();

	auto r = dynamic_cast<ProcessorWithScriptingContent*>(this)->getScriptingContent()->createComponentsFromValueTree(propertyData, buildComponents);

	if (r.failed())
	{
		debugError(dynamic_cast<Processor*>(this), r.getErrorMessage());
	}
}

String JavascriptProcessor::Helpers::resolveIncludeStatements(String& x, Array<File>& includedFiles, const JavascriptProcessor* p)
{
	String regex("include\\(\"([\\w\\s]+\\.\\w+)\"\\);");
	StringArray results = RegexFunctions::search(regex, x, 0);
	StringArray fileNames = RegexFunctions::search(regex, x, 1);
	StringArray includedContents;

	for (int i = 0; i < fileNames.size(); i++)
	{
		File f = File::isAbsolutePath(fileNames[i]) ? File(fileNames[i]) :
			GET_PROJECT_HANDLER(dynamic_cast<const Processor*>(p)).getSubDirectory(ProjectHandler::SubDirectories::Scripts).getChildFile(fileNames[i]);

		if (includedFiles.contains(f)) // skip multiple inclusions...
		{
			includedContents.add("");
			continue;
		}

		includedFiles.add(f);
		String content = f.loadFileAsString();
		includedContents.add(resolveIncludeStatements(content, includedFiles, p));
	}

	for (int i = 0; i < results.size(); i++)
	{
		x = x.replace(results[i], includedContents[i]);
	}

	return x;
}

String JavascriptProcessor::Helpers::stripUnusedNamespaces(const String &code, int& counter)
{
	HiseJavascriptEngine::RootObject::ExpressionTreeBuilder it(code, "");

	try
	{
		String returnString = it.removeUnneededNamespaces(counter);
		return returnString;
	}
	catch (String &e)
	{
		Logger::getCurrentLogger()->writeToLog(e);
		return code;
	}
}

String JavascriptProcessor::Helpers::uglify(const String& prettyCode)
{
	HiseJavascriptEngine::RootObject::ExpressionTreeBuilder it(prettyCode, "");

	try
	{
		String returnString = it.uglify();
		return returnString;
	}
	catch (String &e)
	{
		Logger::getCurrentLogger()->writeToLog(e);
		return prettyCode;
	}
}

String JavascriptProcessor::getBase64CompressedScript() const
{
	String x;
	mergeCallbacksToScript(x, NewLine::getDefault());

	int counter = 0;

	Array<File> includedFiles;
	String everything = Helpers::resolveIncludeStatements(x, includedFiles, this);
	String stripped = Helpers::stripUnusedNamespaces(everything, counter);

	if (counter != 0) PresetHandler::showMessageWindow("Unneeded namespaces detected", String(counter) + " namespaces will be removed before exporting");

	if(PresetHandler::showYesNoWindow("Uglify Script", "Do you want to strip comments & whitespace before compressing?"))
		stripped = Helpers::uglify(stripped);

	MemoryOutputStream mos;
	GZIPCompressorOutputStream gos(&mos, 9);
	gos.writeString(stripped);
	gos.flush();

	return mos.getMemoryBlock().toBase64Encoding();
}

bool JavascriptProcessor::restoreBase64CompressedScript(const String &base64compressedScript)
{
	MemoryBlock mb;

	mb.fromBase64Encoding(base64compressedScript);

	MemoryInputStream mis(mb, false);

	GZIPDecompressorInputStream gis(mis);

	String script = gis.readEntireStreamAsString();

	return parseSnippetsFromString(script);
}

void JavascriptProcessor::setConnectedFile(const String& fileReference, bool compileScriptAfterLoad/*=true*/)
{
	if (fileReference.isNotEmpty())
	{
		connectedFileReference = fileReference;

#if USE_BACKEND
		const File f = GET_PROJECT_HANDLER(dynamic_cast<const Processor*>(this)).getFilePath(fileReference, ProjectHandler::SubDirectories::Scripts);
		const String code = f.loadFileAsString();
#else
		const String code = dynamic_cast<Processor*>(this)->getMainController()->getExternalScriptFromCollection(fileReference);

#endif

		if (fileReference.endsWith(".cjs"))
		{
			restoreBase64CompressedScript(code);
		}
		else
		{
			parseSnippetsFromString(code, true);
		}

		if(compileScriptAfterLoad)
			compileScript();

		dynamic_cast<Processor*>(this)->sendChangeMessage();
	}
}

void JavascriptProcessor::disconnectFromFile()
{
	connectedFileReference = String();

	dynamic_cast<Processor*>(this)->sendChangeMessage();
}

void JavascriptProcessor::reloadFromFile()
{
	if (isConnectedToExternalFile())
	{
		setConnectedFile(connectedFileReference);
	}
}

void JavascriptProcessor::mergeCallbacksToScript(String &x, const String& sepString/*=String()*/) const
{
	for (int i = 0; i < getNumSnippets(); i++)
	{
		const SnippetDocument *s = getSnippet(i);

		x << s->getSnippetAsFunction() << sepString;
	}
}

bool JavascriptProcessor::parseSnippetsFromString(const String &x, bool clearUndoHistory /*= false*/)
{
	String codeToCut = String(x);

	for (int i = getNumSnippets(); i > 1; i--)
	{
		SnippetDocument *s = getSnippet(i-1);
		
		const String filter = "function " + s->getCallbackName().toString() + "(";

		if (!x.contains(filter))
		{
            if(MessageManager::getInstance()->isThisTheMessageThread())
            {
                PresetHandler::showMessageWindow("Invalid script", "The script you are trying to load is not a valid HISE script file.\nThe callback " + filter + " is not defined.", PresetHandler::IconType::Error);
            }
            
            debugError(dynamic_cast<Processor*>(this), s->getCallbackName().toString() + " could not be parsed!");
			
			return false;
		}

		String code = codeToCut.fromLastOccurrenceOf(filter, true, false);

		s->replaceContentAsync(code);

		codeToCut = codeToCut.upToLastOccurrenceOf(filter, false, false);
        
        if(clearUndoHistory)
        {
            s->getUndoManager().clearUndoHistory();
        }
	}

	getSnippet(0)->replaceContentAsync(codeToCut);

	debugToConsole(dynamic_cast<Processor*>(this), "All callbacks sucessfuly parsed");

	return true;
}



void JavascriptProcessor::setCompileProgress(double progress)
{
	if (currentCompileThread != nullptr && mainController->isUsingBackgroundThreadForCompiling())
	{
		currentCompileThread->setProgress(progress);
	}
}



void JavascriptProcessor::compileScriptWithCycleReferenceCheckEnabled()
{
	ScopedValueSetter<bool> ss(cycleReferenceCheckEnabled, true);
	compileScript();
}

void JavascriptProcessor::stuffAfterCompilation(const SnippetResult& result)
{
	mainController->getScriptComponentEditBroadcaster()->clearSelection(sendNotification);

	if (lastCompileWasOK)
	{
		String x;
		mergeCallbacksToScript(x);
	}

	mainController->checkAndAbortMessageThreadOperation();

	clearFileWatchers();

	const int numFiles = scriptEngine->getNumIncludedFiles();

	for (int i = 0; i < numFiles; i++)
	{
		mainController->checkAndAbortMessageThreadOperation();

		addFileWatcher(scriptEngine->getIncludedFile(i));
		setFileResult(scriptEngine->getIncludedFile(i), scriptEngine->getIncludedFileResult(i));
	}

	const String fileName = ApiHelpers::getFileNameFromErrorMessage(result.r.getErrorMessage());

	if (fileName.isNotEmpty())
	{
		for (int i = 0; i < getNumWatchedFiles(); i++)
		{
			mainController->checkAndAbortMessageThreadOperation();

			if (getWatchedFile(i).getFileName() == fileName)
			{
				setFileResult(getWatchedFile(i), result.r);
			}
		}
	}

	mainController->sendScriptCompileMessage(this);
}

JavascriptProcessor::SnippetDocument::SnippetDocument(const Identifier &callbackName_, const String &parameters_) :
CodeDocument(),
isActive(false),
callbackName(callbackName_),
notifier(*this)
{
	parameters = StringArray::fromTokens(parameters_, " ", "");
	numArgs = parameters.size();
	
	if (callbackName != Identifier("onInit"))
	{
		emptyText << "function " << callbackName_.toString() << "(";
		
		for (int i = 0; i < numArgs; i++)
		{
			emptyText << parameters[i];
			if (i != numArgs - 1) emptyText << ", ";
		}
		
		emptyText << ")\n";
		emptyText << "{\n";
		emptyText << "\t\n";
		emptyText << "}\n";
	};

	emptyText << " ";
	
	setDisableUndo(true);

	replaceAllContent(emptyText);
    
	setDisableUndo(false);
}

void JavascriptProcessor::SnippetDocument::checkIfScriptActive()
{
	isActive = true;
    auto text = getSnippetAsFunction();
    
	if (!text.containsNonWhitespaceChars()) isActive = false;

	String trimmedText = text.removeCharacters(" \t\n\r");

	String trimmedEmptyText = emptyText.removeCharacters(" \t\n\r");

	if (trimmedEmptyText == trimmedText)	isActive = false;
}

String JavascriptProcessor::SnippetDocument::getSnippetAsFunction() const
{
	SpinLock::ScopedLockType sl(pendingLock);

	if (isSnippetEmpty()) return emptyText;
	else if (pendingNewContent.isNotEmpty()) return pendingNewContent;
	else				  return getAllContent();
}

JavascriptProcessor::CompileThread::CompileThread(JavascriptProcessor *processor) :
ThreadWithProgressWindow("Compiling", true, false),
sp(processor),
result(SnippetResult(Result::ok(), 0))
{
	getAlertWindow()->setLookAndFeel(&alaf);
}

void JavascriptProcessor::CompileThread::run()
{
	result = sp->compileInternal();
}


float ScriptBaseMidiProcessor::getDefaultValue(int index) const
{
	if(auto c = getScriptingContent()->getComponent(index))
		return c->getScriptObjectProperty(ScriptingApi::Content::ScriptComponent::defaultValue);

	return 0.0f;
}

void JavascriptThreadPool::addJob(Task::Type t, JavascriptProcessor* p, const Task::Function& f)
{
	WARN_IF_AUDIO_THREAD(true, IllegalAudioThreadOps::StringCreation);

	auto currentThread = getMainController()->getKillStateHandler().getCurrentThread();

	switch (currentThread)
	{
	case MainController::KillStateHandler::SampleLoadingThread:
	{
		jassert(!getMainController()->getKillStateHandler().isAudioRunning());
		
		if (t == Task::Type::LowPriorityCallbackExecution)
		{
			pushToQueue(t, p, f);
		}
		else
		{

			// We're calling a high-priority task from the sample loading thread.
			// This can be executed synchronously

			Result r = executeNow(t, p, f);

			if (r.failed())
				getMainController()->getConsoleHandler().writeToConsole(r.getErrorMessage(), 1, dynamic_cast<Processor*>(p), Colours::blue);
		}

		break;
	}
	case MainController::KillStateHandler::ScriptingThread:
	{
		jassert(isBusy());
		
		if (t == currentType)
		{
			// Same priority, just run it
			executeNow(t, p, f);
		}
		else
		{
			if (t == Task::Type::LowPriorityCallbackExecution)
			{
				// We're calling a low priority task from a high priority task
				// In this case, we defer the task to later (it's just a timer
				// callback or a repaint function).
				pushToQueue(t, p, f);
			}
			else
			{
				// We're calling a high priority task from a low priority
				// callback, so we need to execute it synchronously.
				executeNow(t, p, f);
			}
		}

		break;
	}
	case MainController::KillStateHandler::MessageThread:
	{
		if (getMainController()->isInitialised())
		{
			pushToQueue(t, p, f);
			notify();
		}
		else
		{
			executeNow(t, p, f);
		}
		break;
	}
	case MainController::KillStateHandler::AudioThread:
	{
		jassertfalse;
		break;
	}
    default:
        jassertfalse;
        break;
	};
}


Result JavascriptThreadPool::executeQueue(const Task::Type& t, PendingCompilationList& pendingCompilations)
{
	Result r = Result::ok();

	auto alreadyCompiled = [pendingCompilations](const CallbackTask& t)
	{
		return pendingCompilations.contains(t.getFunction().getProcessor());
	};

	switch (t)
	{
	case Task::Type::Compilation:
	{
		CompilationTask ct;

		while (compilationQueue.pop(ct))
		{
			SuspendHelpers::ScopedTicket ticket;

			killVoicesAndExtendTimeOut(ct.getFunction().getProcessor());

			r = ct.call();
			pendingCompilations.addIfNotAlreadyThere(ct.getFunction().getProcessor());
		}

		return r;
	}
	case Task::HiPriorityCallbackExecution:
	{
		r = executeQueue(Task::Compilation, pendingCompilations);

		CallbackTask hpt;

		while (highPriorityQueue.pop(hpt))
		{
			jassert(hpt.getFunction().isHiPriority());

			if (alreadyCompiled(hpt))
				continue;

			
			r = hpt.call();
		}

		return r;
	}
	case Task::LowPriorityCallbackExecution:
	{
		r = executeQueue(Task::HiPriorityCallbackExecution, pendingCompilations);

		CallbackTask lpt;

		while (lowPriorityQueue.pop(lpt))
		{
			jassert(!lpt.getFunction().isHiPriority());

			if (alreadyCompiled(lpt))
				continue;

			r = lpt.call();
		}

		return r;
	}
	default:
		jassertfalse;
	}

	return r;
}

void JavascriptThreadPool::run()
{
	while (!threadShouldExit())
	{
		Array<WeakReference<JavascriptProcessor>> compiledProcessors;
		compiledProcessors.ensureStorageAllocated(16);

		executeQueue(Task::LowPriorityCallbackExecution, compiledProcessors);
		
		wait(500);
	}
}

void JavascriptThreadPool::killVoicesAndExtendTimeOut(JavascriptProcessor* jp, int milliseconds)
{
	if (!getMainController()->isInitialised())
		return;

	getMainController()->getKillStateHandler().killVoicesAndWait(&milliseconds);

	if (auto engine = jp->getScriptEngine())
	{
		engine->extendTimeout(milliseconds);
	}
}

void JavascriptThreadPool::pushToQueue(const Task::Type& t, JavascriptProcessor* p, const Task::Function& f)
{
	switch (t)
	{
	case Task::LowPriorityCallbackExecution:
	{
		lowPriorityQueue.push({ Task(t, p, f), getMainController() });
		break;
	}
	case Task::HiPriorityCallbackExecution:
	{
		highPriorityQueue.push({ Task(t, p, f), getMainController() });
		break;
	}
	case Task::Compilation:
	{
		compilationQueue.push({ Task(t, p, f), getMainController() });
		break;
	}
	default:
		jassertfalse;
		return;
	}

	notify();
}

Result JavascriptThreadPool::executeNow(const Task::Type& t, JavascriptProcessor* p, const Task::Function& f)
{
	return Task(t, p, f).callWithResult();
}


Result JavascriptThreadPool::Task::callWithResult()
{
	if (getProcessor() == nullptr)
		return Result::fail("Processor deleted");

	auto& parent = dynamic_cast<Processor*>(getProcessor())->getMainController()->getJavascriptThreadPool();

	if (parent.threadShouldExit())
		return Result::fail("Aborted");

	if (jp != nullptr && f)
	{
		jassert(type != Free);

		if(type == Compilation)
			LockHelpers::freeToGo(parent.getMainController());

		LockHelpers::SafeLock sl(parent.getMainController(), LockHelpers::ScriptLock);

		ScopedValueSetter<bool> svs(parent.busy, true);
		ScopedValueSetter<Task::Type> svs2(parent.currentType, type);

		try
		{
			return f(jp.get());
		}
		catch (Result& r)
		{
			jassertfalse;
			return Result(r);
		}
		catch (String& errorMessage)
		{
			jassertfalse;
			return Result::fail(errorMessage);
		}
	};

	return Result::fail("invalid function");
}

} // namespace hise
