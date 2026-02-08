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

void InteractionTestWindow::CursorOverlay::setPosition(Point<float> normalizedPos)
{
    position = normalizedPos;
    repaint();
}

void InteractionTestWindow::CursorOverlay::paint(Graphics& g)
{
    // Convert normalized position to pixel position within our bounds
    auto bounds = getLocalBounds().toFloat();
    auto pixelPos = Point<float>(
        position.x * bounds.getWidth(),
        position.y * bounds.getHeight()
    );
    
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
    
    if (content != nullptr)
        content->setBounds(bounds);
    
    if (overlay != nullptr)
        overlay->setBounds(bounds);
}

//==============================================================================
// InteractionTestWindow::RealExecutor implementation

InteractionTestWindow::RealExecutor::RealExecutor(InteractionTestWindow* w)
    : window(w)
{
    jassert(window != nullptr);
}

void InteractionTestWindow::RealExecutor::executeMouseDown(Point<float> normPos, const String& target,
                                                           ModifierKeys mods, bool rightClick, int timestampMs)
{
    ignoreUnused(target, timestampMs);
    
    mouseCurrentlyDown = true;
    lastPosition = normPos;
    
    auto pixelPos = window->normalizedToPixels(normPos).toFloat();
    
    // Add appropriate button modifier
    if (rightClick)
        mods = mods.withFlags(ModifierKeys::rightButtonModifier);
    else
        mods = mods.withFlags(ModifierKeys::leftButtonModifier);
    
    injectMouseEvent(pixelPos, mods);
    
    // Update overlay
    window->getOverlay()->setPosition(normPos);
    window->getOverlay()->setState(CursorOverlay::State::Clicking);
}

void InteractionTestWindow::RealExecutor::executeMouseUp(Point<float> normPos, const String& target,
                                                         ModifierKeys mods, bool rightClick, int timestampMs)
{
    ignoreUnused(target, timestampMs, rightClick);
    
    mouseCurrentlyDown = false;
    lastPosition = normPos;
    
    auto pixelPos = window->normalizedToPixels(normPos).toFloat();
    
    // No button flags for mouseUp - just modifiers
    // (The lack of button flags signals mouseUp)
    injectMouseEvent(pixelPos, mods);
    
    // Update overlay
    window->getOverlay()->setPosition(normPos);
    window->getOverlay()->setState(CursorOverlay::State::Idle);
}

void InteractionTestWindow::RealExecutor::executeMouseMove(Point<float> normPos, const String& target,
                                                           ModifierKeys mods, int timestampMs)
{
    ignoreUnused(target, timestampMs);
    
    lastPosition = normPos;
    
    auto pixelPos = window->normalizedToPixels(normPos).toFloat();
    
    // If mouse is down, this is a drag move - include button
    if (mouseCurrentlyDown)
        mods = mods.withFlags(ModifierKeys::leftButtonModifier);
    
    injectMouseEvent(pixelPos, mods);
    
    // Update overlay
    window->getOverlay()->setPosition(normPos);
    
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
        
        peer->handleMouseEvent(
            MouseInputSource::InputSourceType::touch,
            windowPos,
            mods,
            MouseInputSource::invalidPressure,
            MouseInputSource::invalidOrientation,
            Time::currentTimeMillis(),
            {},
            syntheticTouchIndex
        );
    }
}

//==============================================================================
// InteractionTestWindow implementation

InteractionTestWindow::InteractionTestWindow(ProcessorWithScriptingContent* processor)
    : DocumentWindow("Interaction Test", 
                     Colours::darkgrey, 
                     DocumentWindow::closeButton,
                     true)
{
    // Create the script content component
    content = std::make_unique<ScriptContentComponent>(processor);
    
    auto contentWidth = content->getContentWidth();
    auto contentHeight = content->getContentHeight();
    
    // Create overlay
    overlay = std::make_unique<CursorOverlay>();
    
    // Create container to hold both
    container = std::make_unique<ContentContainer>();
    container->content = content.get();
    container->overlay = overlay.get();
    
    container->addAndMakeVisible(content.get());
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
    // Clean up in correct order
    overlay = nullptr;
    content = nullptr;
    container = nullptr;
}

void InteractionTestWindow::closeButtonPressed()
{
    // Just hide - the owner (InteractionTester) will detect this
    setVisible(false);
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
    if (content != nullptr)
        return { 0, 0, content->getContentWidth(), content->getContentHeight() };
    
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
    InteractionTestWindow::RealExecutor executor(window.get());
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
