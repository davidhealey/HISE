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
*   ===========================================================================
*/


/** TODO:

- filter console log. error ? only show error list, otherwise: just show log list
- set the green indicator to match the error state (so if a script error occurs, make it red)
- append the initial input list. so log window only shows: input list -> console log or error per run.
- test popupment properly - panels, right click, etc!
- add a simple replay button
- make a comprehensive guide for the MCP server 
- add get screenshot with downscaling & cropping. remove entire old screenshot endpoint
- add autoscroll
- add subcomponent resolving to allow more complex components to work (eg. Preset browser or slider packs).

## big feature: project-based testing system! 

- record (!) user interaction with event filtering & thinning
- record audio output too! make spectrogram with downsampling endpoint
- use profiling toolkit
- add MIDI event logic!
- record audio
- store in projectfolder/Tests
- check with scriptnode test system
- add autodump for screenshots, use projectfolder/Images/screenshots as default...
- add embedded tool in HISE that replicates my current debug script.

*/

namespace hise {
using namespace juce;

//==============================================================================
// InteractionTestWindow::CursorOverlay implementation

InteractionTestWindow::CursorOverlay::CursorOverlay()
{
    setInterceptsMouseClicks(false, false);
}

void InteractionTestWindow::CursorOverlay::setState(State newState)
{
    if (state != newState)
    {
        state = newState;
        repaint();
    }
}

void InteractionTestWindow::CursorOverlay::setPosition(Point<int> pixelPos)
{
    position = pixelPos;
    repaint();
}

void InteractionTestWindow::CursorOverlay::paint(Graphics& g)
{
    painted = true;
    
    auto pixelPos = position.toFloat();
    
    auto cursorBounds = Rectangle<float>(cursorDiameter, cursorDiameter)
        .withCentre(pixelPos);
    
    g.setColour(getColourForState());
    g.fillEllipse(cursorBounds);
    
    g.setColour(Colours::white);
    g.drawEllipse(cursorBounds.reduced(borderWidth / 2.0f), borderWidth);
    
    g.setColour(Colours::white.withAlpha(0.7f));
    g.drawLine(pixelPos.x - 3, pixelPos.y, pixelPos.x + 3, pixelPos.y, 1.0f);
    g.drawLine(pixelPos.x, pixelPos.y - 3, pixelPos.x, pixelPos.y + 3, 1.0f);
}

Colour InteractionTestWindow::CursorOverlay::getColourForState() const
{
    switch (state)
    {
        case State::Idle:     return Colours::grey;
        case State::Hovering: return Colours::yellow;
        case State::Clicking: return Colours::green;
        case State::Dragging: return Colours::orange;
        default:              return Colours::grey;
    }
}

//==============================================================================
// InteractionTestWindow::TopBar implementation

Path InteractionTestWindow::TopBar::ButtonPathFactory::createPath(const String& url) const
{
    Path p;
    
    if (url == "record")
    {
        // Simple filled circle for record button
        p.addEllipse(0.0f, 0.0f, 1.0f, 1.0f);
        return p;
    }
    
    LOAD_EPATH_IF_URL("replay", EditorIcons::swapIcon);
    LOAD_EPATH_IF_URL("new", SampleMapIcons::newSampleMap);
    LOAD_EPATH_IF_URL("save", SampleMapIcons::saveSampleMap);
    LOAD_EPATH_IF_URL("load", SampleMapIcons::loadSampleMap);
    
    return p;
}

InteractionTestWindow::TopBar::TopBar()
{
    // Group 1: Record, Replay
    recordButton = new HiseShapeButton("record", this, pathFactory, "record");
    recordButton->setTooltip("Record mouse interactions");
    addAndMakeVisible(recordButton);
    
    replayButton = new HiseShapeButton("replay", this, pathFactory, "replay");
    replayButton->setTooltip("Replay recorded interactions");
    replayButton->setEnabled(false);
    addAndMakeVisible(replayButton);
    
    // Group 2: New, Open, Save
    clearButton = new HiseShapeButton("new", this, pathFactory, "new");
    clearButton->setTooltip("New session");
    addAndMakeVisible(clearButton);
    
    loadButton = new HiseShapeButton("load", this, pathFactory, "load");
    loadButton->setTooltip("Load session from Tests folder");
    addAndMakeVisible(loadButton);
    
    saveButton = new HiseShapeButton("save", this, pathFactory, "save");
    saveButton->setTooltip("Save session to Tests folder");
    saveButton->setEnabled(false);
    addAndMakeVisible(saveButton);
    
    // Session name editor
    sessionNameEditor = new TextEditor("sessionName");
    sessionNameEditor->setText(sessionName, dontSendNotification);
    sessionNameEditor->setFont(GLOBAL_BOLD_FONT());
    GlobalHiseLookAndFeel::setTextEditorColours(*sessionNameEditor);
    addAndMakeVisible(sessionNameEditor);
}

void InteractionTestWindow::TopBar::paint(Graphics& g)
{
    auto b = getLocalBounds();
    
    // Background (same as StatusBar)
    g.setColour(Colour(0xFF333333));
    g.fillRect(b);
    
    // Bottom border line
    g.setColour(Colours::black.withAlpha(0.5f));
    g.drawHorizontalLine(getHeight() - 1, 0.0f, (float)getWidth());
}

void InteractionTestWindow::TopBar::resized()
{
    auto b = getLocalBounds();
    
    int buttonSize = 32;
    int padding = 4;
    int groupMargin = 12;  // Larger margin between button groups
    
    b.removeFromLeft(padding);
    
    // Group 1: Record, Replay
    recordButton->setBounds(b.removeFromLeft(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize).reduced(4));
    b.removeFromLeft(padding);
    replayButton->setBounds(b.removeFromLeft(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize).reduced(4));
    
    b.removeFromLeft(groupMargin);
    
    // Group 2: New, Open, Save
    clearButton->setBounds(b.removeFromLeft(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize).reduced(4));
    b.removeFromLeft(padding);
    loadButton->setBounds(b.removeFromLeft(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize).reduced(4));
    b.removeFromLeft(padding);
    saveButton->setBounds(b.removeFromLeft(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize).reduced(4));
    b.removeFromLeft(padding);
    
    // Session name editor fills remaining space
    sessionNameEditor->setBounds(b.reduced(4, 6));
}

void InteractionTestWindow::TopBar::buttonClicked(Button* b)
{
    if (b == recordButton.get())
    {
        // Toggle recording state (actual recording logic in Phase 2)
        setRecording(!recording);
    }
    else if (b == replayButton.get())
    {
        // Replay the recorded/loaded interactions
        if (auto* window = findParentComponentOfClass<InteractionTestWindow>())
        {
            if (auto* t = window->getInteractionTester())
            {
                t->executeInteractions(recordedInteractions, false);
            }
        }
    }
    else if (b == clearButton.get())
    {
        // Clear current session
        recordedInteractions = var();
        setHasRecording(false);
        
        // Bump session name if folder exists
        if (auto* window = findParentComponentOfClass<InteractionTestWindow>())
        {
            if (auto* t = window->getInteractionTester())
            {
                setSessionName(generateSessionName(t->getTestsFolder()));
            }
        }
    }
    else if (b == saveButton.get())
    {
        // Save logic (Phase 3)
    }
    else if (b == loadButton.get())
    {
        // Load logic (Phase 3)
    }
}

void InteractionTestWindow::TopBar::setRecording(bool isRecording)
{
    recording = isRecording;
    
    // Update button appearance
    if (recordButton != nullptr)
    {
        if (recording)
        {
            // Red color when recording
            recordButton->setColours(Colour(0xFFFF4444), Colour(0xFFFF6666), Colour(0xFFFF2222));
        }
        else
        {
            // Default colors
            recordButton->setColours(Colours::white.withAlpha(0.7f), 
                                     Colours::white, 
                                     Colours::white.withAlpha(0.5f));
        }
        recordButton->repaint();
    }
    
    // Disable other buttons during recording
    replayButton->setEnabled(!recording && hasRecordingData);
    saveButton->setEnabled(!recording && hasRecordingData);
    loadButton->setEnabled(!recording);
    
    // Update status bar state
    if (auto* window = findParentComponentOfClass<InteractionTestWindow>())
    {
        if (auto* statusBar = window->getStatusBar())
        {
            // StatusBar will handle the state change via its own mechanism
            // For now, we just update the visual appearance
        }
    }
    
    repaint();
}

void InteractionTestWindow::TopBar::setHasRecording(bool hasData)
{
    hasRecordingData = hasData;
    replayButton->setEnabled(hasData && !recording);
    saveButton->setEnabled(hasData && !recording);
}

void InteractionTestWindow::TopBar::setSessionName(const String& name)
{
    sessionName = name;
    if (sessionNameEditor != nullptr)
        sessionNameEditor->setText(name, dontSendNotification);
    repaint();
}

String InteractionTestWindow::TopBar::getSessionName() const
{
    if (sessionNameEditor != nullptr)
        return sessionNameEditor->getText();
    return sessionName;
}

String InteractionTestWindow::TopBar::generateSessionName(const File& testsFolder)
{
    int counter = 1;
    String baseName = "new_session_";
    
    while (testsFolder.getChildFile(baseName + String(counter)).exists())
        counter++;
    
    return baseName + String(counter);
}

//==============================================================================
// InteractionTestWindow::StatusBar implementation

Path InteractionTestWindow::StatusBar::ButtonPathFactory::createPath(const String& url) const
{
    Path p;
    LOAD_EPATH_IF_URL("save", SampleMapIcons::saveSampleMap);
    LOAD_EPATH_IF_URL("log", ColumnIcons::scriptWorkspaceIcon);
    return p;
}

InteractionTestWindow::StatusBar::StatusBar()
{
    dumpButton = new HiseShapeButton("dump", this, pathFactory, "save");
    dumpButton->setTooltip("Save screenshots to folder");
    addAndMakeVisible(dumpButton);
    
    logButton = new HiseShapeButton("log", this, pathFactory, "log");
    logButton->setTooltip("Show verbose response log");
    logButton->setToggleModeWithColourChange(true);
    addAndMakeVisible(logButton);
}

InteractionTestWindow::StatusBar::~StatusBar()
{
    stopTimer();
}

void InteractionTestWindow::StatusBar::paint(Graphics& g)
{
    auto b = getLocalBounds();
    
    // Background
    g.setColour(Colour(0xFF333333));
    g.fillRect(b);
    
    if (flashAlpha > 0.0f)
    {
        g.setColour(Colours::white.withAlpha(flashAlpha));
        g.fillRect(b);
    }

    // Top border line
    g.setColour(Colours::black.withAlpha(0.5f));
    g.drawHorizontalLine(0, 0.0f, (float)getWidth());
    
    // Layout areas (right to left for fixed-width elements)
    auto dumpArea = b.removeFromRight(28);        // dump button handled by child
    auto screenshotArea = b.removeFromRight(90);  // screenshot counter
    auto stateArea = b.removeFromLeft(90);        // state indicator
    auto logArea = b.removeFromLeft(28);          // log button handled by child
    auto progressArea = b.reduced(4, 4);          // remaining = progress bar with text inside
    
    // State indicator (colored dot)
    auto dotArea = stateArea.removeFromLeft(20);
    auto dotBounds = dotArea.withSizeKeepingCentre(10, 10).toFloat();
    
    Colour stateColour = getStateColour();
    if (state == State::Running)
        stateColour = stateColour.withAlpha(pulseAlpha);
    
    g.setColour(stateColour);
    g.fillEllipse(dotBounds);
    
    // State text
    g.setFont(GLOBAL_BOLD_FONT());
    g.setColour(Colours::white.withAlpha(0.8f));
    g.drawText(getStateText(), stateArea, Justification::centredLeft);
    
    // Progress bar background
    g.setColour(Colours::black.withAlpha(0.3f));
    g.fillRoundedRectangle(progressArea.toFloat(), 2.0f);
    
    

    // Progress bar fill (white with 20% opacity)
    if (progress > 0.0)
    {
        auto fillWidth = (int)(progressArea.getWidth() * jlimit(0.0, 1.0, progress));
        if (fillWidth > 0)
        {
            auto fillBounds = progressArea.withWidth(fillWidth);
            g.setColour(Colours::white.withAlpha(0.2f));
            g.fillRoundedRectangle(fillBounds.toFloat(), 2.0f);
        }
    }
    
    // Centered text inside progress bar: "Action (completed/total)"
    g.setFont(GLOBAL_FONT());
    g.setColour(Colours::white.withAlpha(0.8f));
    
    String progressText = currentAction;
    if (totalInteractions > 0)
        progressText += " (" + String(completedInteractions) + "/" + String(totalInteractions) + ")";
    
    g.drawText(progressText, progressArea, Justification::centred, true);
    
    // Screenshot counter
    g.setColour(Colours::white.withAlpha(0.7f));
    String ssText = String(screenshotCount) + (screenshotCount == 1 ? " screenshot" : " screenshots");
    g.drawText(ssText, screenshotArea.reduced(4, 0), Justification::centredRight);
}

void InteractionTestWindow::StatusBar::resized()
{
    auto b = getLocalBounds();
    dumpButton->setBounds(b.removeFromRight(28).reduced(4));
    b.removeFromRight(90);  // screenshot area
    b.removeFromLeft(90);   // state area
    logButton->setBounds(b.removeFromLeft(28).reduced(4));
}

//==============================================================================
// StatusBar ProgressListener implementation

void InteractionTestWindow::StatusBar::onSequenceStarted(int total, int estimatedMs)
{
    state = State::Running;
    totalInteractions = total;
    completedInteractions = 0;
    screenshotCount = 0;
    estimatedDurationMs = estimatedMs;
    sequenceStartTime = Time::currentTimeMillis();
    progress = 0.0;
    currentAction = "Starting...";
    clearScreenshots();
    startTimerHz(30);
    
    // Disable log button during sequence to prevent accidental clicks
    if (logButton != nullptr)
        logButton->setEnabled(false);
    
    repaint();
}

void InteractionTestWindow::StatusBar::onInteractionStarted(int index, const String& description)
{
    ignoreUnused(index);
    currentAction = description;
    repaint();
}

void InteractionTestWindow::StatusBar::onInteractionCompleted(int index)
{
    completedInteractions = index + 1;
    repaint();
}

void InteractionTestWindow::StatusBar::onScreenshotCaptured()
{
    flashAlpha = 1.0f;
    screenshotCount++;
    repaint();
}

void InteractionTestWindow::StatusBar::onSequenceCompleted(bool success, const String& error)
{
    state = success ? State::Success : State::Failed;
    currentAction = success ? "Complete" : error;
    progress = 1.0;
    stopTimer();
    
    // Re-enable log button after sequence completes
    if (logButton != nullptr)
        logButton->setEnabled(true);
    
    repaint();
}

//==============================================================================
// StatusBar Timer implementation

void InteractionTestWindow::StatusBar::timerCallback()
{
    // Update progress based on elapsed time
    if (estimatedDurationMs > 0)
    {
        auto elapsed = Time::currentTimeMillis() - sequenceStartTime;
        progress = jlimit(0.0, 0.95, (double)elapsed / (double)estimatedDurationMs);
    }
    
    flashAlpha *= 0.9f;

    if (flashAlpha < 0.01f)
        flashAlpha = 0.0f;

    // Pulse animation for running state
    if (pulseIncreasing)
    {
        pulseAlpha += 0.05f;
        if (pulseAlpha >= 1.0f)
        {
            pulseAlpha = 1.0f;
            pulseIncreasing = false;
        }
    }
    else
    {
        pulseAlpha -= 0.05f;
        if (pulseAlpha <= 0.6f)
        {
            pulseAlpha = 0.6f;
            pulseIncreasing = true;
        }
    }
    
    repaint();
}

//==============================================================================
// StatusBar Button::Listener implementation

void InteractionTestWindow::StatusBar::buttonClicked(Button* b)
{
    if (b == dumpButton.get())
    {
        // If no folder selected yet, prompt for one
        if (!dumpFolder.exists())
        {
            FileChooser chooser("Select folder for screenshots",
                               File::getSpecialLocation(File::userDesktopDirectory));
            
            if (chooser.browseForDirectory())
                dumpFolder = chooser.getResult();
        }
        
        // If we have a folder, save the screenshots
        if (dumpFolder.exists() && !capturedScreenshots.empty())
        {
            for (const auto& screenshot : capturedScreenshots)
            {
                auto file = dumpFolder.getChildFile(screenshot.first + ".png");
                file.replaceWithData(screenshot.second.getData(), screenshot.second.getSize());
            }
        }
    }
    else if (b == logButton.get())
    {
        if (auto* window = findParentComponentOfClass<InteractionTestWindow>())
        {
            window->setLogVisible(logButton->getToggleState());
            window->overlay->setVisible(!logButton->getToggleState());
        }
            
    }
}

//==============================================================================
// StatusBar screenshot storage

void InteractionTestWindow::StatusBar::storeScreenshot(const String& id, const MemoryBlock& pngData)
{
    capturedScreenshots.push_back({id, pngData});
}

void InteractionTestWindow::StatusBar::clearScreenshots()
{
    capturedScreenshots.clear();
}

void InteractionTestWindow::StatusBar::appendToLog(const var& inputInteractions, const String& jsonResponse, bool success)
{
    String text;
    
    // Header with timestamp and status
    text << "\n// === " << Time::getCurrentTime().toString(true, true)
         << (success ? " [SUCCESS]" : " [FAILED]") << " ===\n\n";
    
    // Input section - compact JSON format
    text << "// Input:\n";
    text << JSON::toString(inputInteractions, false) << "\n\n";
    
    // Parse response to extract logs and errors
    var parsed = JSON::parse(jsonResponse);
    auto* obj = parsed.getDynamicObject();
    
    if (obj != nullptr)
    {
        // Console section (always show if not empty)
        auto logsVar = obj->getProperty(RestApiIds::logs);
        if (logsVar.isArray() && logsVar.size() > 0)
        {
            text << "// Console:\n";
            for (int i = 0; i < logsVar.size(); i++)
                text << logsVar[i].toString() << "\n";
            text << "\n";
        }
        
        // Errors section (only show if errors exist)
        auto errorsVar = obj->getProperty(RestApiIds::errors);
        if (errorsVar.isArray() && errorsVar.size() > 0)
        {
            text << "// Errors:\n";
            for (int i = 0; i < errorsVar.size(); i++)
            {
                auto* errObj = errorsVar[i].getDynamicObject();
                if (errObj == nullptr) continue;
                
                text << "[" << (i + 1) << "] " << errObj->getProperty(RestApiIds::errorMessage).toString() << "\n";
                
                // Show location if available
                auto callstack = errObj->getProperty(RestApiIds::callstack);
                if (callstack.isArray() && callstack.size() > 0)
                {
                    text << "    Callstack:\n";
                    for (int j = 0; j < callstack.size(); j++)
                        text << "      " << callstack[j].toString() << "\n";
                }
            }
            text << "\n";
        }
    }
    
    logDocument.insertText(logDocument.getNumCharacters(), text);
}

void InteractionTestWindow::StatusBar::setLogToggleState(bool shouldBeOn)
{
    if (logButton != nullptr)
        logButton->setToggleStateAndUpdateIcon(shouldBeOn, true);
}

void InteractionTestWindow::StatusBar::updateFinalState(bool hasScriptErrors)
{
    if (hasScriptErrors && state == State::Success)
    {
        state = State::Failed;
        currentAction = "Script error(s)";

        SafeAsyncCall::repaint(this);
    }
}

//==============================================================================
// StatusBar helpers

Colour InteractionTestWindow::StatusBar::getStateColour() const
{
    switch (state)
    {
        case State::Idle:      return Colours::white.withAlpha(0.5f);
        case State::Running:   return Colour(SIGNAL_COLOUR);
        case State::Recording: return Colour(0xFFFF4444);  // Red for recording
        case State::Success:   return Colour(HISE_OK_COLOUR);
        case State::Failed:    return Colour(HISE_ERROR_COLOUR);
        default:               return Colours::white;
    }
}

String InteractionTestWindow::StatusBar::getStateText() const
{
    switch (state)
    {
        case State::Idle:      return "IDLE";
        case State::Running:   return "RUNNING";
        case State::Recording: return "RECORDING";
        case State::Success:   return "SUCCESS";
        case State::Failed:    return "FAILED";
        default:               return "";
    }
}

//==============================================================================
// InteractionTestWindow::ContentContainer implementation

void InteractionTestWindow::ContentContainer::resized()
{
    auto bounds = getLocalBounds();
    
    // TopBar at top
    if (topBar != nullptr)
        topBar->setBounds(bounds.removeFromTop(TopBar::HEIGHT));
    
    // StatusBar at bottom
    if (statusBar != nullptr)
        statusBar->setBounds(bounds.removeFromBottom(StatusBar::HEIGHT));
    
    // Root tile gets remaining space
    if (rootTile != nullptr)
        rootTile->setBounds(bounds);
}

//==============================================================================
// InteractionTestWindow::RealExecutor implementation

InteractionTestWindow::RealExecutor* InteractionTestWindow::RealExecutor::activeExecutor = nullptr;

InteractionTestWindow::RealExecutor::RealExecutor(InteractionTestWindow* w, ProcessorWithScriptingContent* p)
    : window(w), processor(p)
{
    jassert(window != nullptr);
    jassert(processor != nullptr);
}

InteractionTestWindow::RealExecutor::~RealExecutor()
{
    if (syntheticModeActive)
        endSyntheticInputMode();
}

//==============================================================================

void InteractionTestWindow::RealExecutor::executeSyntheticModeStart(int elapsedMs)
{
    ignoreUnused(elapsedMs);
    beginSyntheticInputMode();
    syntheticModeActive = true;
}

void InteractionTestWindow::RealExecutor::executeSyntheticModeEnd(int elapsedMs)
{
    ignoreUnused(elapsedMs);
    
    // Force a final repaint to ensure last UI change is visible
    if (auto* peer = window->getPeer())
    {
        RestServer::forceRepaintWindow(peer->getNativeHandle());
        peer->performAnyPendingRepaintsNow();
    }
    
    MessageManager::getInstance()->runDispatchLoopUntil(200);
    
    if (syntheticModeActive)
    {
        endSyntheticInputMode();
        syntheticModeActive = false;
    }
}

//==============================================================================

void InteractionTestWindow::RealExecutor::beginSyntheticInputMode()
{
    activeExecutor = this;
    syntheticModifiers = ModifierKeys();
    
    if (window != nullptr)
        window->grabKeyboardFocus();
    
    PopupMenu::setSyntheticInputMode(true);
    
    auto& desktop = Desktop::getInstance();
    for (int i = 0; i < desktop.getNumMouseSources(); ++i)
    {
        if (auto* source = desktop.getMouseSource(i))
            source->setSyntheticPositionMode(true);
    }
    
    ComponentPeer::getNativeRealtimeModifiers = &RealExecutor::getSyntheticModifiersCallback;
}

void InteractionTestWindow::RealExecutor::endSyntheticInputMode()
{
    PopupMenu::setSyntheticInputMode(false);
    
    auto& desktop = Desktop::getInstance();
    for (int i = 0; i < desktop.getNumMouseSources(); ++i)
    {
        if (auto* source = desktop.getMouseSource(i))
            source->setSyntheticPositionMode(false);
    }
    
    ComponentPeer::getNativeRealtimeModifiers = nullptr;
    activeExecutor = nullptr;
}

ModifierKeys InteractionTestWindow::RealExecutor::getSyntheticModifiersCallback()
{
    if (activeExecutor != nullptr)
        return activeExecutor->syntheticModifiers;
    
    return ModifierKeys::currentModifiers;
}

InteractionDispatcher::Executor::ResolveResult 
InteractionTestWindow::RealExecutor::resolveTarget(const String& componentId)
{
    ResolveResult result;
    
    if (processor == nullptr)
    {
        result.error = "No processor available";
        return result;
    }
    
    auto* content = processor->getScriptingContent();
    if (content == nullptr)
    {
        result.error = "No scripting content available";
        return result;
    }
    
    auto* sc = content->getComponentWithName(Identifier(componentId));
    if (sc == nullptr)
    {
        result.error = "Component '" + componentId + "' not found";
        return result;
    }
    
    result.visible = sc->isShowing(true);
    if (!result.visible)
    {
        result.error = "Component '" + componentId + "' is not visible";
        return result;
    }
    
    auto localBounds = sc->getPosition();
    int globalX = sc->getGlobalPositionX();
    int globalY = sc->getGlobalPositionY();
    
    result.componentBounds = Rectangle<int>(
        globalX, globalY,
        localBounds.getWidth(), localBounds.getHeight()
    );
    
    if (result.componentBounds.isEmpty())
    {
        result.error = "Component '" + componentId + "' has zero size";
        return result;
    }
    
    return result;
}

void InteractionTestWindow::RealExecutor::executeMouseDown(Point<int> pixelPos, ModifierKeys mods,
                                                           bool rightClick, int elapsedMs)
{
    ignoreUnused(elapsedMs);
    
    mouseCurrentlyDown = true;
    cursorPosition = pixelPos;
    
    syntheticModifiers = mods;
    
    injectMouseEvent(pixelPos.toFloat(), mods);
    
    window->getOverlay()->setPosition(pixelPos);
    window->getOverlay()->setState(CursorOverlay::State::Clicking);
}

void InteractionTestWindow::RealExecutor::executeMouseUp(Point<int> pixelPos, ModifierKeys mods,
                                                         bool rightClick, int elapsedMs)
{
    ignoreUnused(elapsedMs);
    
    mouseCurrentlyDown = false;
    cursorPosition = pixelPos;
    
    injectMouseEvent(pixelPos.toFloat(), mods);
    
    if (rightClick)
        syntheticModifiers = syntheticModifiers.withoutFlags(ModifierKeys::rightButtonModifier);
    else
        syntheticModifiers = syntheticModifiers.withoutFlags(ModifierKeys::leftButtonModifier);
    
    window->getOverlay()->setPosition(pixelPos);
    window->getOverlay()->setState(CursorOverlay::State::Idle);
}

void InteractionTestWindow::RealExecutor::executeMouseMove(Point<int> pixelPos, ModifierKeys mods,
                                                           int elapsedMs)
{
    ignoreUnused(elapsedMs);
    
    cursorPosition = pixelPos;
    
    if (mouseCurrentlyDown && !mods.isAnyMouseButtonDown())
        mods = mods.withFlags(ModifierKeys::leftButtonModifier);
    
    syntheticModifiers = mods;
    
    injectMouseEvent(pixelPos.toFloat(), mods);
    
    window->getOverlay()->setPosition(pixelPos);
    
    if (mouseCurrentlyDown)
        window->getOverlay()->setState(CursorOverlay::State::Dragging);
    else
        window->getOverlay()->setState(CursorOverlay::State::Hovering);
}

void InteractionTestWindow::RealExecutor::executeScreenshot(const String& id, float scale, int elapsedMs)
{
    ignoreUnused(elapsedMs);
    
    auto* contentComponent = window->getContent();
    auto* overlayComponent = window->getOverlay();
    
    if (contentComponent == nullptr)
        return;
    
    // Hide overlay for screenshot
    bool wasVisible = overlayComponent != nullptr && overlayComponent->isVisible();
    if (overlayComponent != nullptr)
        overlayComponent->setVisible(false);
    
    // Capture the full window (includes embedded popup menus)
    auto windowImage = window->createComponentSnapshot(window->getLocalBounds(), true, scale);
    
    if (overlayComponent != nullptr)
        overlayComponent->setVisible(wasVisible);
    
    if (!windowImage.isValid())
        return;
    
    // Calculate crop region to exclude status bar (content area only)
    // Note: Native title bar is already excluded from window->getLocalBounds()
    auto contentBoundsInWindow = window->getLocalArea(contentComponent, contentComponent->getLocalBounds());
    
    // Scale the crop region
    auto scaledCropBounds = Rectangle<int>(
        roundToInt(contentBoundsInWindow.getX() * scale),
        roundToInt(contentBoundsInWindow.getY() * scale),
        roundToInt(contentBoundsInWindow.getWidth() * scale),
        roundToInt(contentBoundsInWindow.getHeight() * scale)
    );
    
    // Crop to just the content area (excludes status bar)
    auto image = windowImage.getClippedImage(scaledCropBounds);
    
    if (!image.isValid())
        return;
    
    MemoryBlock mb;
    {
        MemoryOutputStream mos(mb, false);
        PNGImageFormat pngFormat;
        if (!pngFormat.writeImageToStream(image, mos))
            return;
    }
    
    // Store PNG data in StatusBar for dump feature
    if (auto* sb = window->getStatusBar())
        sb->storeScreenshot(id, mb);
    
    // Store screenshot info (metadata + raw PNG data)
    ScreenshotInfo info;
    info.id = id;
    info.sizeKB = static_cast<float>(mb.getSize()) / 1024.0f;
    info.width = image.getWidth();
    info.height = image.getHeight();
    info.pngData = std::move(mb);
    screenshots[id] = std::move(info);
}

void InteractionTestWindow::RealExecutor::injectMouseEvent(Point<float> pixelPos, ModifierKeys mods)
{
    auto* contentComponent = window->getContent();
    if (contentComponent == nullptr)
        return;
    
    if (auto* peer = window->getPeer())
    {
        auto contentTopLeft = contentComponent->getScreenPosition() - window->getScreenPosition();
        auto windowPos = pixelPos + contentTopLeft.toFloat();
        
        peer->handleMouseEvent(
            MouseInputSource::InputSourceType::mouse,
            windowPos,
            mods,
            MouseInputSource::invalidPressure,
            MouseInputSource::invalidOrientation,
            Time::currentTimeMillis(),
            {},
            0
        );

        //RestServer::forceRepaintWindow(peer->getNativeHandle());
        //peer->performAnyPendingRepaintsNow();
    }
}

Point<int> InteractionTestWindow::RealExecutor::getCurrentCursorPosition() const
{
    return cursorPosition;
}

void InteractionTestWindow::RealExecutor::setCursorPosition(Point<int> pos)
{
    cursorPosition = pos;
}

Array<PopupMenu::VisibleMenuItem> InteractionTestWindow::RealExecutor::getVisibleMenuItems() const
{
    auto screenItems = PopupMenu::getVisibleMenuItems();
    
    Array<PopupMenu::VisibleMenuItem> localItems;
    
    if (window != nullptr)
    {
        for (auto& item : screenItems)
        {
            auto localBounds = window->getLocalArea(nullptr, item.screenBounds);
            localBounds.translate(0, -TopBar::HEIGHT);  // Offset for TopBar
            
            PopupMenu::VisibleMenuItem localItem;
            localItem.text = item.text;
            localItem.itemId = item.itemId;
            localItem.screenBounds = localBounds;
            localItems.add(localItem);
        }
    }
    
    return localItems;
}

int InteractionTestWindow::RealExecutor::waitUntilReady()
{
    constexpr int MAX_WAIT_MS = 5000;
    constexpr int POLL_INTERVAL_MS = 10;
    constexpr int BUFFER_MS = 100;
    
    // Hide log editor before sequence starts to prevent accidental clicks
    if (window->isLogVisible())
        window->setLogVisible(false);
    
    auto* overlay = window->getOverlay();
    if (overlay == nullptr)
        return -1;
    
    if (overlay->hasBeenPainted())
        return 0;
    
    overlay->repaint();
    
    auto startTime = Time::getMillisecondCounterHiRes();
    
    while (!overlay->hasBeenPainted())
    {
        auto elapsed = Time::getMillisecondCounterHiRes() - startTime;
        if (elapsed > MAX_WAIT_MS)
            return -1;
        
        MessageManager::getInstance()->runDispatchLoopUntil(POLL_INTERVAL_MS);
    }
    
    MessageManager::getInstance()->runDispatchLoopUntil(BUFFER_MS);
    
    return static_cast<int>(Time::getMillisecondCounterHiRes() - startTime);
}

MainController* InteractionTestWindow::RealExecutor::getMainController() const
{
    return processor != nullptr ? processor->getMainController_() : nullptr;
}

//==============================================================================
// InteractionTestWindow implementation

InteractionTestWindow::InteractionTestWindow(ProcessorWithScriptingContent* processor, InteractionTester* t)
    : PopupLookAndFeel::DocumentWindowWithEmbeddedPopupMenu("Interaction Test", 
                     Colours::darkgrey, 
                     DocumentWindow::closeButton,
                     true),
      tester(t)
{
    setUsingNativeTitleBar(true);
    
    rootTile = std::make_unique<FloatingTile>(processor->getMainController_(), nullptr);
    rootTile->setNewContent("InterfacePanel");
    
    int contentWidth = 600;
    int contentHeight = 400;
    
    if (auto* scc = getContent())
    {
        contentWidth = scc->getContentWidth();
        contentHeight = scc->getContentHeight();
    }
    
    // Add TopBar and StatusBar height to content height
    contentHeight += TopBar::HEIGHT;
    contentHeight += StatusBar::HEIGHT;
    
    overlay = std::make_unique<CursorOverlay>();
    topBar = std::make_unique<TopBar>();
    statusBar = std::make_unique<StatusBar>();
    
    container = std::make_unique<ContentContainer>();
    container->rootTile = rootTile.get();
    container->topBar = topBar.get();
    container->statusBar = statusBar.get();
    
    container->addAndMakeVisible(topBar.get());
    container->addAndMakeVisible(rootTile.get());
    container->addAndMakeVisible(statusBar.get());
    
    container->setSize(contentWidth, contentHeight);
    
    setContentNonOwned(container.get(), true);
    
    // Add overlay as direct child of window (same level as popup menus)
    Component::addAndMakeVisible(overlay.get());
    overlay->setAlwaysOnTop(true);
    
    if (auto* display = Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        auto displayArea = display->userArea;
        setCentrePosition(displayArea.getCentre().translated(1000, 0));
    }
    
    setVisible(true);
    setAlwaysOnTop(true);
    toFront(true);
}

InteractionTestWindow::~InteractionTestWindow()
{
    logEditor = nullptr;  // Destroy before StatusBar (which owns CodeDocument)
    overlay = nullptr;
    topBar = nullptr;
    statusBar = nullptr;
    rootTile = nullptr;
    container = nullptr;
}

void InteractionTestWindow::closeButtonPressed()
{
    setVisible(false);
}

void InteractionTestWindow::childrenChanged()
{
    // Ensure overlay stays on top when children change (e.g., popup menu added)
    // Only reorder if overlay exists and isn't already the frontmost child
    if (overlay != nullptr && getChildren().getLast() != overlay.get())
        overlay->toFront(false);
}

void InteractionTestWindow::resized()
{
    // Call base class to handle content component
    DocumentWindowWithEmbeddedPopupMenu::resized();
    
    // Position overlay to cover the content area (excluding TopBar)
    if (overlay != nullptr && container != nullptr)
    {
        auto overlayBounds = container->getBounds();
        overlayBounds.removeFromTop(TopBar::HEIGHT);
        overlay->setBounds(overlayBounds);
    }
    
    // Position log editor over content area (excluding TopBar and StatusBar)
    if (logEditor != nullptr && container != nullptr)
    {
        auto logBounds = container->getBounds();
        logBounds.removeFromTop(TopBar::HEIGHT);
        logBounds.removeFromBottom(StatusBar::HEIGHT);
        logEditor->setBounds(logBounds);
        logEditor->toFront(false);
    }
}

void InteractionTestWindow::setLogVisible(bool visible)
{
    // Sync button toggle state
    if (auto* sb = getStatusBar())
    {
        sb->setLogToggleState(visible);
        overlay->setVisible(!visible);
    }
        
    
    if (visible && logEditor == nullptr)
    {
        auto* sb = getStatusBar();
        if (sb == nullptr) return;
        
        logEditor = std::make_unique<CodeEditorComponent>(sb->getLogDocument(), sb->getLogTokeniser());
        
        // Style to match JSONEditor / HISE dark theme
        logEditor->setColour(CodeEditorComponent::backgroundColourId, Colour(0xff262626));
        logEditor->setColour(CodeEditorComponent::defaultTextColourId, Colour(0xFFCCCCCC));
        logEditor->setColour(CodeEditorComponent::lineNumberTextId, Colour(0xFFCCCCCC));
        logEditor->setColour(CodeEditorComponent::lineNumberBackgroundId, Colour(0xff363636));
        logEditor->setColour(CodeEditorComponent::highlightColourId, Colour(0xff666666));
        logEditor->setColour(CaretComponent::caretColourId, Colour(0xFFDDDDDD));
        logEditor->setColour(ScrollBar::thumbColourId, Colour(0x3dffffff));
        logEditor->setReadOnly(true);
        logEditor->setFont(GLOBAL_MONOSPACE_FONT().withHeight(14.0f));
        
        Component::addAndMakeVisible(logEditor.get());
        resized();
        logEditor->moveCaretToEnd(false);
    }
    else if (!visible && logEditor != nullptr)
    {
        logEditor = nullptr;
        resized();
    }
}

ScriptContentComponent* InteractionTestWindow::getContent()
{
    if (rootTile == nullptr)
        return nullptr;
    
    if (auto* panel = dynamic_cast<InterfaceContentPanel*>(rootTile->getCurrentFloatingPanel()))
        return panel->getScriptContentComponent();
    
    return nullptr;
}

Point<int> InteractionTestWindow::normalizedToPixels(Point<float> norm) const
{
    auto bounds = getContentBounds();
    return {
        roundToInt(norm.x * bounds.getWidth()),
        roundToInt(norm.y * bounds.getHeight())
    };
}

Rectangle<int> InteractionTestWindow::getContentBounds() const
{
    auto* self = const_cast<InteractionTestWindow*>(this);
    if (auto* scc = self->getContent())
        return { 0, 0, scc->getContentWidth(), scc->getContentHeight() };
    
    return {};
}

//==============================================================================
// InteractionTester implementation

InteractionTester::InteractionTester(BackendProcessor* bp)
    : backendProcessor(bp)
{
    jassert(backendProcessor != nullptr);
}

InteractionTester::~InteractionTester()
{
    closeWindow();
}

void InteractionTester::resetMouseState()
{
    mouseState.reset();
}

void InteractionTester::logResponse(const var& inputInteractions, const String& jsonResponse, bool success)
{
    if (window == nullptr) return;
    
    auto* sb = window->getStatusBar();
    if (sb == nullptr) return;
    
    // Append formatted log entry
    sb->appendToLog(inputInteractions, jsonResponse, success);
    
    // Update indicator if script errors occurred
    var parsed = JSON::parse(jsonResponse);
    if (auto* obj = parsed.getDynamicObject())
    {
        auto errorsVar = obj->getProperty(RestApiIds::errors);
        bool hasScriptErrors = errorsVar.isArray() && errorsVar.size() > 0;
        sb->updateFinalState(hasScriptErrors);
    }
}

InteractionTester::TestResult InteractionTester::executeInteractions(const var& interactionsJson, bool verbose)
{
    TestResult result;
    
    jassert(MessageManager::getInstance()->isThisTheMessageThread());
    
    // Parse interactions
    Array<InteractionParser::Interaction> interactions;
    auto parseResult = InteractionParser::parseInteractions(interactionsJson, interactions);
    
    if (parseResult.failed())
    {
        result.errorMessage = parseResult.getErrorMessage();
        return result;
    }
    
    result.parseWarnings = parseResult.warnings;
    
    if (interactions.isEmpty())
    {
        result.success = true;
        return result;
    }
    
    // Get interface processor
    auto* processor = getInterfaceProcessor();
    if (processor == nullptr)
    {
        result.errorMessage = "moduleId 'Interface' not found";
        return result;
    }
    
    // Make a local copy of interactions to avoid dangling references during replay
    // (when replaying, interactionsJson may reference TopBar::recordedInteractions which gets overwritten)
    var interactionsCopy = interactionsJson;
    
    // Ensure window is open
    ensureWindowOpen();
    
    if (window == nullptr || !window->isVisible())
    {
        result.errorMessage = "Failed to create test window";
        return result;
    }
    
    // Capture console if not already being captured by REST handler
    RestHelpers::ReplayConsoleHandler consoleHandler(backendProcessor, window.get(),
        !backendProcessor->getConsoleHandler().hasCustomLogger());
    
    // NORMALIZE: Auto-insert moveTo events
    normalizeSequence(interactions);
    
    // Execute with real executor
    auto* pwsc = dynamic_cast<ProcessorWithScriptingContent*>(processor);
    InteractionTestWindow::RealExecutor executor(window.get(), pwsc);
    
    // Sync executor cursor position with our mouse state
    executor.setCursorPosition(mouseState.pixelPosition);
    
    InteractionDispatcher dispatcher;
    
    // Wire up status bar as progress listener
    if (auto* statusBar = window->getStatusBar())
        dispatcher.setProgressListener(statusBar);
    
    auto execResult = dispatcher.execute(interactions, executor, result.executionLog);
    
    // Update our mouse state from executor
    mouseState.pixelPosition = executor.getCurrentCursorPosition();
    
    result.success = execResult.result.wasOk();
    result.errorMessage = execResult.result.getErrorMessage();
    result.interactionsCompleted = execResult.interactionsCompleted;
    result.totalElapsedMs = execResult.totalElapsedMs;
    result.screenshots = executor.getScreenshots();
    result.selectedMenuItem = dispatcher.getLastSelectedMenuItem();
    
    if (verbose)
    {
        result.finalMouseState = mouseState;
    }
    
    // Store for replay & enable replay button
    if (window != nullptr && window->getTopBar() != nullptr)
    {
        window->getTopBar()->setRecordedInteractions(interactionsCopy);
        window->getTopBar()->setHasRecording(true);
    }
    
    // Finalize console capture and log to StatusBar
    if (consoleHandler.isCapturing())
    {
        consoleHandler.finalize(interactionsCopy, result.success, 
                                result.interactionsCompleted, result.totalElapsedMs);
    }
    
    return result;
}

bool InteractionTester::isWindowOpen() const
{
    return window != nullptr && window->isVisible();
}

void InteractionTester::closeWindow()
{
    window = nullptr;
}

void InteractionTester::ensureWindowOpen()
{
    if (window != nullptr && !window->isVisible())
    {
        window = nullptr;
    }
    
    if (window == nullptr)
    {
        // Reset mouse state when creating new window
        resetMouseState();
        
        auto* processor = getInterfaceProcessor();
        if (processor != nullptr)
        {
            auto* pwsc = dynamic_cast<ProcessorWithScriptingContent*>(processor);
            if (pwsc != nullptr)
            {
                window = std::make_unique<InteractionTestWindow>(pwsc, this);
            }
        }
    }
}

JavascriptMidiProcessor* InteractionTester::getInterfaceProcessor() const
{
    if (backendProcessor == nullptr)
        return nullptr;
    
    return JavascriptMidiProcessor::getFirstInterfaceScriptProcessor(backendProcessor);
}

File InteractionTester::getTestsFolder() const
{
    if (backendProcessor == nullptr)
        return File();
    
    return GET_PROJECT_HANDLER(backendProcessor->getMainSynthChain()).getWorkDirectory().getChildFile("Tests");
}

//==============================================================================
// Normalization - Auto-insert moveTo events

void InteractionTester::normalizeSequence(Array<InteractionParser::Interaction>& interactions)
{
    if (interactions.isEmpty())
        return;
    
    Array<InteractionParser::Interaction> normalized;
    
    for (auto& interaction : interactions)
    {
        auto& mouse = interaction.mouse;
        
        // Check if moveTo needed before this interaction
        auto moveToResult = createMoveToIfNeeded(mouse);
        
        if (moveToResult.needed)
        {
            InteractionParser::Interaction moveInteraction;
            moveInteraction.mouse = moveToResult.moveTo;
            
            // Transfer delay from original interaction to the moveTo
            moveInteraction.mouse.delayMs = mouse.delayMs;
            mouse.delayMs = 0;
            
            normalized.add(moveInteraction);
            
            // Update state after moveTo
            updateMouseStateForNormalization(moveInteraction.mouse);
        }
        
        // Add original interaction
        normalized.add(interaction);
        
        // Update state after original interaction
        updateMouseStateForNormalization(mouse);
    }
    
    interactions = std::move(normalized);
}

InteractionTester::MoveToResult 
InteractionTester::createMoveToIfNeeded(const InteractionParser::MouseInteraction& next)
{
    using Type = InteractionParser::MouseInteraction::Type;
    
    switch (next.type)
    {
        case Type::Click:
        case Type::Drag:
        {
            // Need moveTo if:
            // 1. Different target, OR
            // 2. Same target but explicit non-center position
            
            bool differentTarget = (mouseState.currentTarget != next.targetComponentId);
            bool explicitPosition = !next.position.isCenter();
            
            if (differentTarget || explicitPosition)
            {
                MoveToResult result;
                result.needed = true;
                result.moveTo.type = Type::MoveTo;
                result.moveTo.targetComponentId = next.targetComponentId;
                result.moveTo.position = next.position;
                result.moveTo.durationMs = InteractionDefaults::MOVE_DURATION_MS;
                result.moveTo.delayMs = 0;  // Will be set by caller
                result.moveTo.autoInserted = true;
                return result;
            }
            break;
        }
        
        case Type::MoveTo:
        case Type::SelectMenuItem:
        case Type::Screenshot:
            // No auto-insertion needed
            break;
    }
    
    return MoveToResult();  // needed = false by default
}

void InteractionTester::updateMouseStateForNormalization(const InteractionParser::MouseInteraction& interaction)
{
    using Type = InteractionParser::MouseInteraction::Type;
    
    switch (interaction.type)
    {
        case Type::MoveTo:
            mouseState.currentTarget = interaction.targetComponentId;
            // Note: pixelPosition will be updated during actual execution
            // when we have access to resolved component bounds
            break;
            
        case Type::Click:
            // State unchanged (mouse stays where it is)
            break;
            
        case Type::Drag:
            // Position changes by delta - actual update happens during execution
            // Target stays the same
            break;
            
        case Type::SelectMenuItem:
            // Menu closes, reset to unknown state
            mouseState.currentTarget = "";
            break;
            
        case Type::Screenshot:
            // No state change
            break;
    }
}

} // namespace hise
