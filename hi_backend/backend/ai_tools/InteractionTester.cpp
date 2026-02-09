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
    
    bool wasVisible = overlayComponent != nullptr && overlayComponent->isVisible();
    if (overlayComponent != nullptr)
        overlayComponent->setVisible(false);
    
    auto bounds = contentComponent->getLocalBounds();
    auto image = contentComponent->createComponentSnapshot(bounds, true, scale);
    
    if (overlayComponent != nullptr)
        overlayComponent->setVisible(wasVisible);
    
    if (!image.isValid())
        return;
    
    MemoryBlock mb;
    {
        MemoryOutputStream mos(mb, false);
        PNGImageFormat pngFormat;
        if (!pngFormat.writeImageToStream(image, mos))
            return;
    }
    
    screenshots.set(id, "data:image/png;base64," + Base64::toBase64(mb.getData(), mb.getSize()));
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

        RestServer::forceRepaintWindow(peer->getNativeHandle());
        peer->performAnyPendingRepaintsNow();
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

//==============================================================================
// InteractionTestWindow implementation

InteractionTestWindow::InteractionTestWindow(ProcessorWithScriptingContent* processor)
    : PopupLookAndFeel::DocumentWindowWithEmbeddedPopupMenu("Interaction Test", 
                     Colours::darkgrey, 
                     DocumentWindow::closeButton,
                     true)
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
    
    overlay = std::make_unique<CursorOverlay>();
    
    container = std::make_unique<ContentContainer>();
    container->rootTile = rootTile.get();
    container->overlay = overlay.get();
    
    container->addAndMakeVisible(rootTile.get());
    container->addAndMakeVisible(overlay.get());
    overlay->setAlwaysOnTop(true);
    
    container->setSize(contentWidth, contentHeight);
    
    setContentNonOwned(container.get(), true);
    
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
    overlay = nullptr;
    rootTile = nullptr;
    container = nullptr;
}

void InteractionTestWindow::closeButtonPressed()
{
    setVisible(false);
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
    
    // Ensure window is open
    ensureWindowOpen();
    
    if (window == nullptr || !window->isVisible())
    {
        result.errorMessage = "Failed to create test window";
        return result;
    }
    
    // NORMALIZE: Auto-insert moveTo events
    normalizeSequence(interactions);
    
    // Execute with real executor
    auto* pwsc = dynamic_cast<ProcessorWithScriptingContent*>(processor);
    InteractionTestWindow::RealExecutor executor(window.get(), pwsc);
    
    // Sync executor cursor position with our mouse state
    executor.setCursorPosition(mouseState.pixelPosition);
    
    InteractionDispatcher dispatcher;
    
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
