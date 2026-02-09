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

namespace hise {
using namespace juce;

//==============================================================================
// Debug flag for synthetic mouse event tracing
// Set to true before injecting events, false after




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
    // Position is already in pixels
    auto pixelPos = position.toFloat();
    
    // Draw cursor circle
    auto cursorBounds = Rectangle<float>(cursorDiameter, cursorDiameter)
        .withCentre(pixelPos);
    
    // Fill with state color
    g.setColour(getColourForState());
    g.fillEllipse(cursorBounds);
    
    // White border
    g.setColour(Colours::white);
    g.drawEllipse(cursorBounds.reduced(borderWidth / 2.0f), borderWidth);
    
    // Small crosshair in center for precision
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
// InteractionTestWindow::ContentContainer implementation

void InteractionTestWindow::ContentContainer::resized()
{
    auto bounds = getLocalBounds();
    
    if (rootTile != nullptr)
        rootTile->setBounds(bounds);
    
    if (overlay != nullptr)
        overlay->setBounds(bounds);
}

//==============================================================================
// InteractionTestWindow::RealExecutor implementation

// Static member for callback access
InteractionTestWindow::RealExecutor* InteractionTestWindow::RealExecutor::activeExecutor = nullptr;

InteractionTestWindow::RealExecutor::RealExecutor(InteractionTestWindow* w, ProcessorWithScriptingContent* p)
    : window(w), processor(p)
{
    jassert(window != nullptr);
    jassert(processor != nullptr);
}

InteractionTestWindow::RealExecutor::~RealExecutor()
{
    // Ensure synthetic mode is cleaned up if still active (safety net)
    if (syntheticModeActive)
        endSyntheticInputMode();
}

//==============================================================================

void InteractionTestWindow::RealExecutor::executeSyntheticModeStart(int timestampMs)
{
    ignoreUnused(timestampMs);
    beginSyntheticInputMode();
    syntheticModeActive = true;
}

void InteractionTestWindow::RealExecutor::executeSyntheticModeEnd(int timestampMs)
{
    ignoreUnused(timestampMs);
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
    
    // Grab keyboard focus on the test window to prevent PopupMenu from dismissing
    // due to "no JUCE comp has focus" check in doesAnyJuceCompHaveFocus()
    if (window != nullptr)
        window->grabKeyboardFocus();
    
    // Enable synthetic input mode in PopupMenu to skip focus and inputAttemptWhenModal checks
    PopupMenu::setSyntheticInputMode(true);
    
    // Enable synthetic position mode on ALL mouse sources, not just the main one
    // PopupMenu and other components may use different mouse sources
    auto& desktop = Desktop::getInstance();
    for (int i = 0; i < desktop.getNumMouseSources(); ++i)
    {
        if (auto* source = desktop.getMouseSource(i))
            source->setSyntheticPositionMode(true);
    }
    
    // Hook the realtime modifiers callback
    ComponentPeer::getNativeRealtimeModifiers = &RealExecutor::getSyntheticModifiersCallback;
}

void InteractionTestWindow::RealExecutor::endSyntheticInputMode()
{
    // Disable synthetic input mode in PopupMenu
    PopupMenu::setSyntheticInputMode(false);
    
    // Restore normal position mode on ALL mouse sources
    auto& desktop = Desktop::getInstance();
    for (int i = 0; i < desktop.getNumMouseSources(); ++i)
    {
        if (auto* source = desktop.getMouseSource(i))
            source->setSyntheticPositionMode(false);
    }
    
    // Restore default realtime modifiers callback
    ComponentPeer::getNativeRealtimeModifiers = nullptr;
    
    // Note: We don't reset ModifierKeys::currentModifiers here
    // JUCE's internal state is managed by handleMouseEvent() and native windowing code
    
    activeExecutor = nullptr;
}

ModifierKeys InteractionTestWindow::RealExecutor::getSyntheticModifiersCallback()
{
    if (activeExecutor != nullptr)
    {
        auto mods = activeExecutor->syntheticModifiers;
        DBG("getSyntheticModifiersCallback: returning synthetic mods, leftButton=" 
            << (mods.isLeftButtonDown() ? "true" : "false")
            << ", rightButton=" << (mods.isRightButtonDown() ? "true" : "false"));
        return mods;
    }
    
    DBG("getSyntheticModifiersCallback: no active executor, returning currentModifiers");
    return ModifierKeys::currentModifiers;
}

InteractionDispatcher::Executor::ResolveResult InteractionTestWindow::RealExecutor::resolveTarget(
    const String& componentId, Point<float> normalizedPos)
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
    
    // Find component by ID
    auto* sc = content->getComponentWithName(Identifier(componentId));
    if (sc == nullptr)
    {
        result.error = "Component '" + componentId + "' not found";
        return result;
    }
    
    // Check visibility (includes parent visibility)
    result.visible = sc->isShowing(true);
    if (!result.visible)
    {
        result.error = "Component '" + componentId + "' is not visible";
        return result;
    }
    
    // Get absolute bounds using existing ScriptComponent methods
    auto localBounds = sc->getPosition();
    int globalX = sc->getGlobalPositionX();
    int globalY = sc->getGlobalPositionY();
    
    result.componentBounds = Rectangle<int>(
        globalX, globalY,
        localBounds.getWidth(), localBounds.getHeight()
    );
    
    // Check for zero-size
    if (result.componentBounds.isEmpty())
    {
        result.error = "Component '" + componentId + "' has zero size";
        return result;
    }
    
    // Calculate pixel position from normalized coords
    result.pixelPosition = {
        globalX + roundToInt(normalizedPos.x * localBounds.getWidth()),
        globalY + roundToInt(normalizedPos.y * localBounds.getHeight())
    };
    
    return result;
}

void InteractionTestWindow::RealExecutor::executeMouseDown(Point<int> pixelPos, const String& target,
                                                           ModifierKeys mods, bool rightClick, int timestampMs)
{
    ignoreUnused(target, timestampMs);
    
    mouseCurrentlyDown = true;
    
    // Prime the component under mouse by sending mouse moves first
    // This ensures JUCE's MouseInputSourceInternal properly tracks:
    // 1. The peer (via setPeer)
    // 2. The component under mouse (via setComponentUnderMouse)
    // Without this, JUCE doesn't know which component should receive the click.
    // We use empty modifiers for the priming moves (no buttons down).
    // Note: We don't update syntheticModifiers or currentModifiers during priming
    // to avoid interfering with JUCE's internal state tracking.
    injectMouseEvent(pixelPos.toFloat().translated(1.0f, 0.0f), ModifierKeys());
    injectMouseEvent(pixelPos.toFloat(), ModifierKeys());
    MessageManager::getInstance()->runDispatchLoopUntil(1);
    
    // Now update synthetic modifier state to reflect this mouseDown
    // mods already contains the button flag from buildModifiers()
    // Note: We only update syntheticModifiers, NOT ModifierKeys::currentModifiers
    // Our getNativeRealtimeModifiers hook handles PopupMenu's realtime checks
    // JUCE's internal buttonState is updated automatically by handleMouseEvent()
    syntheticModifiers = mods;
    
    // Inject the mouseDown event
    injectMouseEvent(pixelPos.toFloat(), mods);
    
    // Update overlay
    window->getOverlay()->setPosition(pixelPos);
    window->getOverlay()->setState(CursorOverlay::State::Clicking);
}

void InteractionTestWindow::RealExecutor::executeMouseUp(Point<int> pixelPos, const String& target,
                                                         ModifierKeys mods, bool rightClick, int timestampMs)
{
    ignoreUnused(target, timestampMs);
    
    mouseCurrentlyDown = false;
    
    // Inject the mouseUp event
    // JUCE expects mods to still contain the button being released
    injectMouseEvent(pixelPos.toFloat(), mods);
    
    // After the event, clear the button from syntheticModifiers
    // Keep keyboard modifiers (shift/ctrl/alt/cmd) intact
    // Note: We only update syntheticModifiers, NOT ModifierKeys::currentModifiers
    if (rightClick)
        syntheticModifiers = syntheticModifiers.withoutFlags(ModifierKeys::rightButtonModifier);
    else
        syntheticModifiers = syntheticModifiers.withoutFlags(ModifierKeys::leftButtonModifier);
    
    // Update overlay
    window->getOverlay()->setPosition(pixelPos);
    window->getOverlay()->setState(CursorOverlay::State::Idle);
}

void InteractionTestWindow::RealExecutor::executeMouseMove(Point<int> pixelPos, const String& target,
                                                           ModifierKeys mods, int timestampMs)
{
    ignoreUnused(target, timestampMs);
    
    // If mouse is down and no button modifier is already set, add leftButtonModifier
    // (The caller may have already set rightButtonModifier for right-click moves)
    if (mouseCurrentlyDown && !mods.isAnyMouseButtonDown())
        mods = mods.withFlags(ModifierKeys::leftButtonModifier);
    
    // Update synthetic modifier state (NOT ModifierKeys::currentModifiers)
    syntheticModifiers = mods;
    
    injectMouseEvent(pixelPos.toFloat(), mods);
    
    // Update overlay
    window->getOverlay()->setPosition(pixelPos);
    
    if (mouseCurrentlyDown)
        window->getOverlay()->setState(CursorOverlay::State::Dragging);
    else
        window->getOverlay()->setState(CursorOverlay::State::Hovering);
}

void InteractionTestWindow::RealExecutor::executeScreenshot(const String& id, float scale, int timestampMs)
{
    ignoreUnused(timestampMs);
    
    auto* contentComponent = window->getContent();
    auto* overlayComponent = window->getOverlay();
    
    if (contentComponent == nullptr)
        return;
    
    // Hide overlay during capture
    bool wasVisible = overlayComponent != nullptr && overlayComponent->isVisible();
    if (overlayComponent != nullptr)
        overlayComponent->setVisible(false);
    
    // Capture screenshot
    auto bounds = contentComponent->getLocalBounds();
    auto image = contentComponent->createComponentSnapshot(bounds, true, scale);
    
    // Restore overlay visibility
    if (overlayComponent != nullptr)
        overlayComponent->setVisible(wasVisible);
    
    if (!image.isValid())
        return;
    
    // Encode to base64 PNG
    MemoryBlock mb;
    {
        MemoryOutputStream mos(mb, false);
        PNGImageFormat pngFormat;
        if (!pngFormat.writeImageToStream(image, mos))
            return;
    }
    
    // Store with data URI prefix
    screenshots.set(id, "data:image/png;base64," + Base64::toBase64(mb.getData(), mb.getSize()));
}

void InteractionTestWindow::RealExecutor::injectMouseEvent(Point<float> pixelPos, ModifierKeys mods)
{
    auto* contentComponent = window->getContent();
    if (contentComponent == nullptr)
        return;
    
    // Get the peer for the top-level window
    if (auto* peer = window->getPeer())
    {
        // Convert position to be relative to the peer (window coordinates)
        // The content component is inside the window, so we need to offset
        auto contentTopLeft = contentComponent->getScreenPosition() - window->getScreenPosition();
        auto windowPos = pixelPos + contentTopLeft.toFloat();
        
        // Inject the synthetic mouse event
        peer->handleMouseEvent(
            MouseInputSource::InputSourceType::mouse,
            windowPos,
            mods,
            MouseInputSource::invalidPressure,
            MouseInputSource::invalidOrientation,
            Time::currentTimeMillis(),
            {},
            0  // Primary mouse input source
        );

        // Force immediate repaint (Windows: calls UpdateWindow via native handle)
        RestServer::forceRepaintWindow(peer->getNativeHandle());
        peer->performAnyPendingRepaintsNow();
    }
}

//==============================================================================
// InteractionTestWindow implementation

InteractionTestWindow::InteractionTestWindow(ProcessorWithScriptingContent* processor)
    : PopupLookAndFeel::DocumentWindowWithEmbeddedPopupMenu("Interaction Test", 
                     Colours::darkgrey, 
                     DocumentWindow::closeButton,
                     true)
{
    context.attachTo(*this);

    setUsingNativeTitleBar(true);
    
    setRepaintsOnMouseActivity(true);
    startTimer(30);

    // Create FloatingTile with InterfacePanel content
    // This mirrors FrontendProcessorEditor structure and ensures proper parent hierarchy
    rootTile = std::make_unique<FloatingTile>(processor->getMainController_(), nullptr);
    rootTile->setNewContent("InterfacePanel");
    
    // Get content dimensions from the ScriptContentComponent inside InterfaceContentPanel
    int contentWidth = 600;  // Default fallback
    int contentHeight = 400;
    
    if (auto* scc = getContent())
    {
        contentWidth = scc->getContentWidth();
        contentHeight = scc->getContentHeight();
    }
    
    // Create overlay
    overlay = std::make_unique<CursorOverlay>();
    
    // Create container to hold both
    container = std::make_unique<ContentContainer>();
    container->rootTile = rootTile.get();
    container->overlay = overlay.get();
    
    container->addAndMakeVisible(rootTile.get());
    container->addAndMakeVisible(overlay.get());
    overlay->setAlwaysOnTop(true);
    
    container->setSize(contentWidth, contentHeight);
    
    // Set as content (non-owned, we manage lifetime)
    setContentNonOwned(container.get(), true);
    
    // Center on primary display
    if (auto* display = Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        auto displayArea = display->userArea;
        setCentrePosition(displayArea.getCentre());
    }
    
    setVisible(true);
    setAlwaysOnTop(true);
    
    // Ensure window is on top and focused
    toFront(true);
}

InteractionTestWindow::~InteractionTestWindow()
{
    context.detach();

    // Clean up in correct order
    overlay = nullptr;
    rootTile = nullptr;
    container = nullptr;
}

void InteractionTestWindow::closeButtonPressed()
{
    // Just hide - the owner (InteractionTester) will detect this
    setVisible(false);
}

ScriptContentComponent* InteractionTestWindow::getContent()
{
    if (rootTile == nullptr)
        return nullptr;
    
    // Navigate: FloatingTile -> InterfaceContentPanel -> ScriptContentComponent
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
    // Need non-const access to call getContent()
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

InteractionTester::TestResult InteractionTester::executeInteractions(const var& interactionsJson)
{
    TestResult result;
    
    // Must be on message thread
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
    
    // Check for empty array
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
    
    // Ensure window is open
    ensureWindowOpen();
    
    if (window == nullptr || !window->isVisible())
    {
        result.errorMessage = "Failed to create test window";
        return result;
    }
    
    // Execute with real executor
    // The dispatcher handles synthetic input mode internally via executeSyntheticModeStart/End events
    // with proper timing (500ms delay before first event, 200ms after last event)
    auto* pwsc = dynamic_cast<ProcessorWithScriptingContent*>(processor);
    InteractionTestWindow::RealExecutor executor(window.get(), pwsc);
    InteractionDispatcher dispatcher;
    
    auto execResult = dispatcher.execute(interactions, executor, result.executionLog);
    
    result.success = execResult.result.wasOk();
    result.errorMessage = execResult.result.getErrorMessage();
    result.interactionsCompleted = execResult.interactionsCompleted;
    result.totalElapsedMs = execResult.totalElapsedMs;
    result.screenshots = executor.getScreenshots();
    
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
    // If window exists but is hidden/closed, destroy it
    if (window != nullptr && !window->isVisible())
    {
        window = nullptr;
    }
    
    // Create new window if needed
    if (window == nullptr)
    {
        auto* processor = getInterfaceProcessor();
        if (processor != nullptr)
        {
            auto* pwsc = dynamic_cast<ProcessorWithScriptingContent*>(processor);
            if (pwsc != nullptr)
            {
                window = std::make_unique<InteractionTestWindow>(pwsc);
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

} // namespace hise
