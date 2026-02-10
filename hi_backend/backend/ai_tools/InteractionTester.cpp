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
// InteractionTestWindow::RecordingListener implementation

void InteractionTestWindow::RecordingListener::setRecording(bool enabled, int64 startTime)
{
    recording = enabled;
    recordingStartTime = startTime;
}

void InteractionTestWindow::RecordingListener::addEvent(const String& type, const MouseEvent& e)
{
    if (!recording) return;
    
    // Get the source component from the event
    auto* sourceComponent = e.eventComponent;
    if (sourceComponent == nullptr) return;
    
    // Navigate up to find InteractionTestWindow
    auto* window = sourceComponent->findParentComponentOfClass<InteractionTestWindow>();
    if (window == nullptr) return;
    
    auto* topBar = window->getTopBar();
    if (topBar == nullptr) return;
    
    // Convert coordinates to ScriptContentComponent space
    auto* scc = window->getContent();
    if (scc == nullptr) return;
    
    Point<int> posInScc = scc->getLocalPoint(sourceComponent, e.getPosition());
    
    int64 timestamp = Time::getMillisecondCounter() - recordingStartTime;
    
    DynamicObject::Ptr event = new DynamicObject();
    event->setProperty("type", type);
    event->setProperty("x", posInScc.x);
    event->setProperty("y", posInScc.y);
    event->setProperty("timestamp", (int)timestamp);
    
    if (type == "mouseDown" || type == "mouseUp")
    {
        event->setProperty("rightClick", e.mods.isRightButtonDown());
    }
    
    // Store modifier state
    if (e.mods.isShiftDown() || e.mods.isCtrlDown() || e.mods.isAltDown() || e.mods.isCommandDown())
    {
        DynamicObject::Ptr mods = new DynamicObject();
        mods->setProperty("shift", e.mods.isShiftDown());
        mods->setProperty("ctrl", e.mods.isCtrlDown());
        mods->setProperty("alt", e.mods.isAltDown());
        mods->setProperty("cmd", e.mods.isCommandDown());
        event->setProperty("modifiers", var(mods.get()));
    }
    
    topBar->addRawEvent(var(event.get()));
}

void InteractionTestWindow::RecordingListener::mouseDown(const MouseEvent& e)
{
    addEvent("mouseDown", e);
}

void InteractionTestWindow::RecordingListener::mouseUp(const MouseEvent& e)
{
    addEvent("mouseUp", e);
}

void InteractionTestWindow::RecordingListener::mouseMove(const MouseEvent& e)
{
    addEvent("mouseMove", e);
}

void InteractionTestWindow::RecordingListener::mouseDrag(const MouseEvent& e)
{
    // Treat drag as mouseMove (the down state is already tracked)
    addEvent("mouseMove", e);
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
    // Mode toggle button
    modeToggle = new TextButton("Semantic");
    modeToggle->setButtonText("Semantic");
    modeToggle->setTooltip("Toggle recording mode: Semantic (detect clicks/drags) or Raw (exact mouse replay)");
    modeToggle->addListener(this);
    modeToggle->setColour(TextButton::buttonColourId, Colour(0xFF555555));
    modeToggle->setColour(TextButton::textColourOffId, Colours::white);
    addAndMakeVisible(modeToggle);
    
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
    int toggleWidth = 70;
    int padding = 4;
    int groupMargin = 12;  // Larger margin between button groups
    
    b.removeFromLeft(padding);
    
    // Mode toggle (before record button)
    modeToggle->setBounds(b.removeFromLeft(toggleWidth).reduced(0, 8));
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
    if (b == modeToggle.get())
    {
        // Toggle between Semantic and Raw modes
        if (recordingMode == RecordingMode::Semantic)
        {
            recordingMode = RecordingMode::Raw;
            modeToggle->setButtonText("Raw");
        }
        else
        {
            recordingMode = RecordingMode::Semantic;
            modeToggle->setButtonText("Semantic");
        }
    }
    else if (b == recordButton.get())
    {
        if (!recording && !inCountdown)
        {
            // Start countdown sequence
            startRecordingCountdown();
        }
        else if (recording)
        {
            // Stop recording early (user clicked during recording)
            stopRecording();
        }
        // If in countdown, ignore clicks
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
    
    updateButtonStates();
    repaint();
}

void InteractionTestWindow::TopBar::updateButtonStates()
{
    bool busy = recording || inCountdown;
    
    // Disable all buttons during recording or countdown
    modeToggle->setEnabled(!busy);
    replayButton->setEnabled(!busy && hasRecordingData);
    saveButton->setEnabled(!busy && hasRecordingData);
    loadButton->setEnabled(!busy);
    clearButton->setEnabled(!busy);
}

void InteractionTestWindow::TopBar::timerCallback()
{
    if (inCountdown)
    {
        countdownValue--;
        
        if (auto* window = findParentComponentOfClass<InteractionTestWindow>())
        {
            if (auto* statusBar = window->getStatusBar())
            {
                if (countdownValue > 0)
                {
                    statusBar->showCountdown(countdownValue);
                }
                else
                {
                    // Countdown finished, start actual recording
                    inCountdown = false;
                    startRecording();
                }
            }
        }
    }
    else if (recording)
    {
        // Check if recording duration has expired
        auto elapsed = Time::getMillisecondCounter() - recordingStartTime;
        auto remaining = RECORDING_DURATION_SECONDS - (int)(elapsed / 1000);
        
        if (remaining <= 0)
        {
            stopRecording();
        }
        else
        {
            // Update status bar with remaining time
            if (auto* window = findParentComponentOfClass<InteractionTestWindow>())
            {
                if (auto* statusBar = window->getStatusBar())
                {
                    statusBar->showRecordingActive(remaining);
                }
            }
        }
    }
}

void InteractionTestWindow::TopBar::startRecordingCountdown()
{
    inCountdown = true;
    countdownValue = COUNTDOWN_SECONDS;
    rawRecordedEvents.clear();
    
    updateButtonStates();
    
    // Show initial countdown
    if (auto* window = findParentComponentOfClass<InteractionTestWindow>())
    {
        if (auto* statusBar = window->getStatusBar())
        {
            statusBar->showCountdown(countdownValue);
        }
        
        // Hide cursor overlay during countdown and recording
        if (auto* overlay = window->getOverlay())
        {
            overlay->setVisible(false);
        }
    }
    
    // Start timer for countdown (1 second intervals)
    startTimer(1000);
}

void InteractionTestWindow::TopBar::startRecording()
{
    recording = true;
    recordingStartTime = Time::getMillisecondCounter();
    
    // Update button appearance
    if (recordButton != nullptr)
    {
        recordButton->setColours(Colour(0xFFFF4444), Colour(0xFFFF6666), Colour(0xFFFF2222));
        recordButton->repaint();
    }
    
    // Show recording indicator and enable mouse capture
    if (auto* window = findParentComponentOfClass<InteractionTestWindow>())
    {
        if (auto* statusBar = window->getStatusBar())
        {
            statusBar->showRecordingActive(RECORDING_DURATION_SECONDS);
        }
        
        // Enable mouse capture on recording overlay
        window->setRecordingMouseCapture(true);
    }
    
    // Timer continues at 1 second intervals
}

void InteractionTestWindow::TopBar::stopRecording()
{
    stopTimer();
    recording = false;
    inCountdown = false;
    
    // Reset button appearance
    if (recordButton != nullptr)
    {
        recordButton->setColours(Colours::white.withAlpha(0.7f), 
                                 Colours::white, 
                                 Colours::white.withAlpha(0.5f));
        recordButton->repaint();
    }
    
    // Hide countdown/recording indicator, disable mouse capture, show cursor overlay again
    if (auto* window = findParentComponentOfClass<InteractionTestWindow>())
    {
        // Disable mouse capture first
        window->setRecordingMouseCapture(false);
        
        if (auto* statusBar = window->getStatusBar())
        {
            statusBar->hideCountdown();
        }
        
        if (auto* overlay = window->getOverlay())
        {
            overlay->setVisible(true);
        }
    }
    
    // Process the recorded events
    processRecordedEvents();
    
    updateButtonStates();
    repaint();
}

void InteractionTestWindow::TopBar::addRawEvent(const var& event)
{
    if (recording)
    {
        rawRecordedEvents.add(event);
    }
}

void InteractionTestWindow::TopBar::processRecordedEvents()
{
    int eventCount = rawRecordedEvents.size();
    
    if (eventCount == 0)
    {
        // No events recorded
        if (auto* window = findParentComponentOfClass<InteractionTestWindow>())
        {
            if (auto* statusBar = window->getStatusBar())
            {
                statusBar->hideCountdown();
                // Could show a message in log
            }
        }
        return;
    }
    
    Array<var> processedEvents;
    
    if (recordingMode == RecordingMode::Raw)
    {
        // Thin mouse move events to ~100ms intervals
        int64 lastMoveTime = -100;  // Ensure first move is included
        
        for (int i = 0; i < rawRecordedEvents.size(); i++)
        {
            auto& event = rawRecordedEvents.getReference(i);
            String type = event.getProperty("type", "").toString();
            int64 timestamp = (int64)event.getProperty("timestamp", 0);
            
            if (type == "mouseMove")
            {
                // Only include if 100ms has passed since last move
                if (timestamp - lastMoveTime >= 100)
                {
                    processedEvents.add(event);
                    lastMoveTime = timestamp;
                }
            }
            else
            {
                // Always include mouseDown/mouseUp
                processedEvents.add(event);
            }
        }
        
        // Create wrapper with mode
        DynamicObject::Ptr wrapper = new DynamicObject();
        wrapper->setProperty("mode", "raw");
        
        Array<var> eventsArray;
        for (auto& e : processedEvents)
            eventsArray.add(e);
        wrapper->setProperty("events", eventsArray);
        
        recordedInteractions = var(wrapper.get());
        setHasRecording(true);
    }
    else
    {
        // Semantic mode - for now just store raw, semantic parsing in Phase 2 Step 6
        // TODO: Implement semantic analysis
        DynamicObject::Ptr wrapper = new DynamicObject();
        wrapper->setProperty("mode", "semantic");
        wrapper->setProperty("interactions", Array<var>());  // Empty for now
        
        recordedInteractions = var(wrapper.get());
    }
    
    // Log recording result to StatusBar
    if (auto* window = findParentComponentOfClass<InteractionTestWindow>())
    {
        if (auto* statusBar = window->getStatusBar())
        {
            statusBar->logRecordingResult(recordingMode, processedEvents, eventCount);
        }
    }
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
    if (state == State::Recording)
        stateColour = stateColour.withAlpha(0.7f + 0.3f * pulseAlpha);  // Pulsing red
    
    g.setColour(stateColour);
    g.fillEllipse(dotBounds);
    
    // State text
    g.setFont(GLOBAL_BOLD_FONT());
    
    // Use red text for countdown/recording, white otherwise
    if (countdownActive)
        g.setColour(Colour(0xFFFF4444));
    else
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
    if (estimatedDurationMs > 0 && state == State::Running)
    {
        auto elapsed = Time::currentTimeMillis() - sequenceStartTime;
        progress = jlimit(0.0, 0.95, (double)elapsed / (double)estimatedDurationMs);
    }
    
    flashAlpha *= 0.9f;

    if (flashAlpha < 0.01f)
        flashAlpha = 0.0f;

    // Pulse animation for running/recording state
    if (state == State::Running || state == State::Recording)
    {
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

void InteractionTestWindow::StatusBar::logRecordingResult(RecordingMode mode, 
                                                          const Array<var>& events, 
                                                          int originalCount)
{
    String text;
    
    // Header with timestamp
    text << "\n// === " << Time::getCurrentTime().toString(true, true)
         << " [RECORDING] ===\n\n";
    
    if (mode == RecordingMode::Raw)
    {
        text << "// Raw mode: " << originalCount << " events captured, "
             << events.size() << " after thinning\n\n";
    }
    else
    {
        text << "// Semantic mode: " << originalCount << " events captured\n\n";
    }
    
    // Output all events as JSON array
    Array<var> eventsArray;
    for (const auto& e : events)
        eventsArray.add(e);
    
    text << JSON::toString(var(eventsArray), true) << "\n";
    
    logDocument.insertText(logDocument.getNumCharacters(), text);
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

void InteractionTestWindow::StatusBar::showCountdown(int seconds)
{
    countdownActive = true;
    countdownValue = seconds;
    recordingIndicator = false;
    state = State::Recording;
    currentAction = String(seconds);
    startTimerHz(30);  // Start pulse animation
    repaint();
}

void InteractionTestWindow::StatusBar::showRecordingActive(int remainingSeconds)
{
    countdownActive = true;
    recordingIndicator = true;
    recordingRemaining = remainingSeconds;
    state = State::Recording;
    currentAction = "REC " + String(remainingSeconds) + "s";
    repaint();
}

void InteractionTestWindow::StatusBar::hideCountdown()
{
    countdownActive = false;
    recordingIndicator = false;
    countdownValue = 0;
    recordingRemaining = 0;
    state = State::Idle;
    currentAction = "Ready";
    stopTimer();
    repaint();
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
    recordingListener = std::make_unique<RecordingListener>();
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
    // Remove the listener before destroying
    if (recordingListener != nullptr)
    {
        if (auto* scc = getContent())
            scc->removeMouseListener(recordingListener.get());
    }
    
    logEditor = nullptr;  // Destroy before StatusBar (which owns CodeDocument)
    recordingListener = nullptr;
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

void InteractionTestWindow::setRecordingMouseCapture(bool enabled)
{
    if (recordingListener == nullptr)
        return;
    
    auto* scc = getContent();
    if (scc == nullptr)
        return;
    
    if (enabled)
    {
        int64 startTime = Time::getMillisecondCounter();
        recordingListener->setRecording(true, startTime);
        scc->addMouseListener(recordingListener.get(), true);  // true = wantsEventsForAllNestedChildComponents
    }
    else
    {
        scc->removeMouseListener(recordingListener.get());
        recordingListener->setRecording(false, 0);
    }
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
    
    DBG(JSON::toString(interactionsJson, false));

    // Check for wrapper format with mode indicator
    var actualInteractions = interactionsJson;
    bool isRawMode = false;
    
    if (auto* obj = interactionsJson.getDynamicObject())
    {
        String mode = obj->getProperty("mode").toString();
        if (mode == "raw")
        {
            isRawMode = true;
            actualInteractions = obj->getProperty("events");
        }
        else if (mode == "semantic")
        {
            actualInteractions = obj->getProperty("interactions");
        }
        // If no mode property, treat as legacy format (semantic)
    }
    
    // Make a local copy to avoid dangling references during replay
    var interactionsCopy = interactionsJson;
    
    // Get interface processor
    auto* processor = getInterfaceProcessor();
    if (processor == nullptr)
    {
        result.errorMessage = "moduleId 'Interface' not found";
        return result;
    }
    
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
    
    // Execute with real executor
    auto* pwsc = dynamic_cast<ProcessorWithScriptingContent*>(processor);
    InteractionTestWindow::RealExecutor executor(window.get(), pwsc);
    
    // Sync executor cursor position with our mouse state
    executor.setCursorPosition(mouseState.pixelPosition);
    
    InteractionDispatcher dispatcher;
    
    // Wire up status bar as progress listener
    if (auto* statusBar = window->getStatusBar())
        dispatcher.setProgressListener(statusBar);
    
    InteractionDispatcher::ExecutionResult execResult;
    
    if (isRawMode)
    {
        // Raw mode: execute events directly without parsing/normalization
        execResult = executeRawEvents(actualInteractions, executor, result.executionLog);
    }
    else
    {
        // Semantic mode: parse, normalize, and execute
        Array<InteractionParser::Interaction> interactions;
        auto parseResult = InteractionParser::parseInteractions(actualInteractions, interactions);
        
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
        
        // NORMALIZE: Auto-insert moveTo events
        normalizeSequence(interactions);
        
        execResult = dispatcher.execute(interactions, executor, result.executionLog);
    }
    
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

//==============================================================================
// Raw event execution (for recorded raw mode)

InteractionDispatcher::ExecutionResult InteractionTester::executeRawEvents(
    const var& eventsArray,
    InteractionDispatcher::Executor& executor,
    Array<var>& executionLog)
{
    if (!eventsArray.isArray())
    {
        return InteractionDispatcher::ExecutionResult::fail("Raw events must be an array", 0, 0);
    }
    
    int64 startTime = Time::getMillisecondCounter();
    int eventsCompleted = 0;
    
    // Notify progress listener if available
    if (auto* statusBar = window->getStatusBar())
    {
        int totalEvents = eventsArray.size();
        int estimatedMs = 0;
        if (totalEvents > 0)
        {
            // Estimate duration from last event's timestamp
            auto lastEvent = eventsArray[totalEvents - 1];
            estimatedMs = (int)lastEvent.getProperty("timestamp", 0);
        }
        statusBar->onSequenceStarted(totalEvents, estimatedMs);
    }
    
    for (int i = 0; i < eventsArray.size(); i++)
    {
        auto& event = eventsArray[i];
        String type = event.getProperty("type", "").toString();
        int x = (int)event.getProperty("x", 0);
        int y = (int)event.getProperty("y", 0);
        int timestamp = (int)event.getProperty("timestamp", 0);
        bool rightClick = (bool)event.getProperty("rightClick", false);
        
        // Parse modifiers
        ModifierKeys mods;
        var modsVar = event.getProperty("modifiers", var());
        if (auto* modsObj = modsVar.getDynamicObject())
        {
            int modFlags = 0;
            if ((bool)modsObj->getProperty("shift"))
                modFlags |= ModifierKeys::shiftModifier;
            if ((bool)modsObj->getProperty("ctrl"))
                modFlags |= ModifierKeys::ctrlModifier;
            if ((bool)modsObj->getProperty("alt"))
                modFlags |= ModifierKeys::altModifier;
            if ((bool)modsObj->getProperty("cmd"))
                modFlags |= ModifierKeys::commandModifier;
            mods = ModifierKeys(modFlags);
        }
        
        Point<int> pos(x, y);
        
        // Wait until the correct timestamp
        int64 targetTime = startTime + timestamp;
        int64 now = Time::getMillisecondCounter();
        if (targetTime > now)
        {
            int waitMs = (int)(targetTime - now);
            // Pump message loop while waiting
            while (Time::getMillisecondCounter() < targetTime)
            {
                MessageManager::getInstance()->runDispatchLoopUntil(jmin(10, waitMs));
            }
        }
        
        int elapsedMs = (int)(Time::getMillisecondCounter() - startTime);
        
        // Execute the event
        if (type == "mouseDown")
        {
            // Add button modifier flag
            if (rightClick)
                mods = mods.withFlags(ModifierKeys::rightButtonModifier);
            else
                mods = mods.withFlags(ModifierKeys::leftButtonModifier);

            // Wiggle to update componentUnderMouse
            executor.executeMouseMove(pos + Point<int>(2, 0), {}, elapsedMs);
            executor.executeMouseMove(pos, {}, elapsedMs);

            executor.executeMouseDown(pos, mods, rightClick, elapsedMs);
        }
        else if (type == "mouseUp")
        {
            // Add button modifier flag (still needed for mouseUp)
            if (rightClick)
                mods = mods.withFlags(ModifierKeys::rightButtonModifier);
            else
                mods = mods.withFlags(ModifierKeys::leftButtonModifier);

            executor.executeMouseUp(pos, mods, rightClick, elapsedMs);

            // UI settle time
            MessageManager::getInstance()->runDispatchLoopUntil(50);
        }
        else if (type == "mouseMove")
        {
            executor.executeMouseMove(pos, mods, elapsedMs);
        }
        
        // Add to execution log
        DynamicObject::Ptr logEntry = new DynamicObject();
        logEntry->setProperty("type", type);
        logEntry->setProperty("x", x);
        logEntry->setProperty("y", y);
        logEntry->setProperty("elapsedMs", elapsedMs);
        executionLog.add(var(logEntry.get()));
        
        eventsCompleted++;
        
        // Update progress
        if (auto* statusBar = window->getStatusBar())
        {
            statusBar->onInteractionCompleted(i);
        }
    }
    
    int totalElapsedMs = (int)(Time::getMillisecondCounter() - startTime);
    
    // Notify completion
    if (auto* statusBar = window->getStatusBar())
    {
        statusBar->onSequenceCompleted(true, "");
    }
    
    return InteractionDispatcher::ExecutionResult::ok(totalElapsedMs, eventsCompleted);
}

} // namespace hise
