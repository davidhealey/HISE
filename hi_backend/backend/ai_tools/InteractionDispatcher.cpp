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
// Helper functions
//==============================================================================

namespace {

/** Generate human-readable description for an interaction. */
String getInteractionDescription(const InteractionParser::MouseInteraction& mouse)
{
    using Type = InteractionParser::MouseInteraction::Type;
    
    switch (mouse.type)
    {
        case Type::MoveTo:
            return "Moving to \"" + mouse.targetComponentId + "\"";
        case Type::Click:
            return mouse.rightClick ? "Right-clicking" : "Clicking";
        case Type::Drag:
            return "Dragging (" + String(mouse.deltaPixels.x) + ", " + String(mouse.deltaPixels.y) + ")";
        case Type::Screenshot:
            return "Capturing screenshot \"" + mouse.screenshotId + "\"";
        case Type::SelectMenuItem:
            return "Selecting menu item \"" + mouse.menuItemText + "\"";
        default:
            return "Processing...";
    }
}

/** Estimate total duration for a sequence of interactions. */
int estimateTotalDuration(const Array<InteractionParser::Interaction>& interactions)
{
    int total = 0;
    for (const auto& i : interactions)
    {
        total += 150;  // Base overhead per interaction
        total += i.mouse.delayMs;
        total += i.mouse.durationMs;
    }
    return total;
}

/** Write interaction event to console if MainController available. */
void logInteractionToConsole(InteractionDispatcher::Executor& exec, const String& description)
{
    if (auto* mc = exec.getMainController())
    {
        debugToConsole(mc->getMainSynthChain(), "> " + description);
    }
}

} // anonymous namespace

//==============================================================================
// InteractionDispatcher implementation
//==============================================================================

InteractionDispatcher::ExecutionResult InteractionDispatcher::execute(
    const Array<InteractionParser::Interaction>& interactions,
    Executor& executor,
    Array<var>& executedLog,
    int timeout)
{
    jassert(MessageManager::getInstance()->isThisTheMessageThread());
    
    timeoutMs = timeout;
    lastError = {};
    lastSelectedMenuItem = {};
    
    int completedCount = 0;
    
    // Start synthetic input mode
    executor.executeSyntheticModeStart(0);
    
    // Wait until executor is ready to receive events
    int readyDelayMs = executor.waitUntilReady();
    if (readyDelayMs < 0)
    {
        executor.executeSyntheticModeEnd(0);
        if (progressListener != nullptr)
            progressListener->onSequenceCompleted(false, "UI failed to become ready within timeout");
        return ExecutionResult::fail("UI failed to become ready within timeout", 0, 0);
    }
    
    // Notify listener that sequence is starting
    if (progressListener != nullptr)
    {
        int estimatedMs = estimateTotalDuration(interactions);
        progressListener->onSequenceStarted(interactions.size(), estimatedMs);
    }
    
    // Start timing AFTER ready
    startTimeMs = Time::getMillisecondCounterHiRes();
    
    int interactionIndex = 0;
    for (const auto& interaction : interactions)
    {
        const auto& mouse = interaction.mouse;
        
        // Notify listener that interaction is starting
        String description = getInteractionDescription(mouse);
        if (progressListener != nullptr)
        {
            progressListener->onInteractionStarted(interactionIndex, description);
        }
        
        // Log to console for interleaved output with script Console.print() calls
        logInteractionToConsole(executor, description);
        
        // Apply delay before this interaction
        if (mouse.delayMs > 0)
        {
            waitForDuration(mouse.delayMs, executor);
        }
        
        // Check abort conditions
        String error = checkAbortConditions();
        if (error.isNotEmpty())
        {
            executor.executeSyntheticModeEnd(getElapsedMs());
            if (progressListener != nullptr)
                progressListener->onSequenceCompleted(false, error);
            return ExecutionResult::fail(error, getElapsedMs(), completedCount);
        }
        
        // Execute based on type
        using Type = InteractionParser::MouseInteraction::Type;
        
        switch (mouse.type)
        {
            case Type::MoveTo:
                executeMoveTo(mouse, executor, executedLog);
                break;
            case Type::Click:
                executeClick(mouse, executor, executedLog);
                break;
            case Type::Drag:
                executeDrag(mouse, executor, executedLog);
                break;
            case Type::Screenshot:
                executeScreenshot(mouse, executor, executedLog);
                if (progressListener != nullptr)
                    progressListener->onScreenshotCaptured();
                break;
            case Type::SelectMenuItem:
                executeSelectMenuItem(mouse, executor, executedLog);
                break;
        }
        
        // Check if resolution/execution failed
        if (lastError.isNotEmpty())
        {
            executor.executeSyntheticModeEnd(getElapsedMs());
            if (progressListener != nullptr)
                progressListener->onSequenceCompleted(false, lastError);
            return ExecutionResult::fail(lastError, getElapsedMs(), completedCount);
        }
        
        completedCount++;
        
        // Notify listener that interaction completed
        if (progressListener != nullptr)
            progressListener->onInteractionCompleted(interactionIndex);
        
        interactionIndex++;
        
        // Check again after execution
        error = checkAbortConditions();
        if (error.isNotEmpty())
        {
            executor.executeSyntheticModeEnd(getElapsedMs());
            if (progressListener != nullptr)
                progressListener->onSequenceCompleted(false, error);
            return ExecutionResult::fail(error, getElapsedMs(), completedCount);
        }
    }
    
    // End synthetic mode
    executor.executeSyntheticModeEnd(getElapsedMs());
    
    // Notify listener of successful completion
    if (progressListener != nullptr)
        progressListener->onSequenceCompleted(true, {});
    
    return ExecutionResult::ok(getElapsedMs(), completedCount);
}

int InteractionDispatcher::getElapsedMs() const
{
    return static_cast<int>(Time::getMillisecondCounterHiRes() - startTimeMs);
}

void InteractionDispatcher::waitForDuration(int ms, Executor& executor)
{
    if (ms <= 0) return;
    
    ignoreUnused(executor);
    
    auto endTime = Time::getMillisecondCounterHiRes() + ms;
    
    while (Time::getMillisecondCounterHiRes() < endTime)
    {
        if (checkAbortConditions().isNotEmpty())
            return;
        
        // Pump message loop
        MessageManager::getInstance()->runDispatchLoopUntil(5);
    }
}

String InteractionDispatcher::checkAbortConditions() const
{
    if (getElapsedMs() > timeoutMs)
    {
        return "Timeout after " + String(timeoutMs) + "ms";
    }
    
    return {};
}

//==============================================================================
// Execute MoveTo
//==============================================================================

void InteractionDispatcher::executeMoveTo(
    const InteractionParser::MouseInteraction& mouse,
    Executor& exec,
    Array<var>& log)
{
    // Resolve target bounds
    auto resolved = exec.resolveTarget(mouse.targetComponentId);
    if (!resolved.success())
    {
        lastError = resolved.error;
        return;
    }
    
    Point<int> startPos = exec.getCurrentCursorPosition();
    Point<int> endPos = mouse.position.getPixelPosition(resolved.componentBounds);
    
    // Interpolate movement
    int duration = mouse.durationMs;
    int numSteps = jmax(1, duration / MOVE_STEP_INTERVAL_MS);
    int stepDuration = duration / numSteps;
    
    for (int i = 1; i <= numSteps; ++i)
    {
        float t = static_cast<float>(i) / numSteps;
        auto delta = (endPos - startPos).toFloat() * t;
        Point<int> pos = startPos + Point<int>(roundToInt(delta.x), roundToInt(delta.y));
        
        exec.executeMouseMove(pos, mouse.modifiers, getElapsedMs());
        exec.setCursorPosition(pos);
        
        if (i < numSteps)
            waitForDuration(stepDuration, exec);
    }
    
    // Ensure we're exactly at end position
    exec.setCursorPosition(endPos);
    
    // Log
    auto entry = createLogEntry("moveTo", endPos, getElapsedMs());
    entry.getDynamicObject()->setProperty("target", mouse.targetComponentId);
    if (mouse.autoInserted)
        entry.getDynamicObject()->setProperty("autoInserted", true);
    log.add(entry);
}

//==============================================================================
// Execute Click
//==============================================================================

void InteractionDispatcher::executeClick(
    const InteractionParser::MouseInteraction& mouse,
    Executor& exec,
    Array<var>& log)
{
    // Get current position (moveTo should have positioned us)
    Point<int> pixelPos = exec.getCurrentCursorPosition();
    
    // Build modifiers with button
    auto mods = mouse.modifiers;
    if (mouse.rightClick)
        mods = mods.withFlags(ModifierKeys::rightButtonModifier);
    else
        mods = mods.withFlags(ModifierKeys::leftButtonModifier);
    
    // Wiggle: small move to force JUCE to update componentUnderMouse
    exec.executeMouseMove(pixelPos + Point<int>(2, 0), {}, getElapsedMs());
    exec.executeMouseMove(pixelPos, {}, getElapsedMs());
    
    // MouseDown
    exec.executeMouseDown(pixelPos, mods, mouse.rightClick, getElapsedMs());
    
    auto downEntry = createLogEntry("mouseDown", pixelPos, getElapsedMs());
    if (mouse.rightClick)
        downEntry.getDynamicObject()->setProperty("rightClick", true);
    log.add(downEntry);
    
    // Brief pause (click duration)
    waitForDuration(mouse.durationMs, exec);
    
    // MouseUp
    exec.executeMouseUp(pixelPos, mods, mouse.rightClick, getElapsedMs());
    
    auto upEntry = createLogEntry("mouseUp", pixelPos, getElapsedMs());
    if (mouse.rightClick)
        upEntry.getDynamicObject()->setProperty("rightClick", true);
    log.add(upEntry);
    
    // Let UI settle after click (e.g., popup menus appearing, button state changes)
    waitForDuration(UI_SETTLE_DELAY_MS, exec);
}

//==============================================================================
// Execute Drag
//==============================================================================

void InteractionDispatcher::executeDrag(
    const InteractionParser::MouseInteraction& mouse,
    Executor& exec,
    Array<var>& log)
{
    Point<int> startPos = exec.getCurrentCursorPosition();
    Point<int> endPos = startPos + mouse.deltaPixels;
    
    // Build modifiers with left button
    auto mods = mouse.modifiers;
    mods = mods.withFlags(ModifierKeys::leftButtonModifier);
    
    // Wiggle: small move to force JUCE to update componentUnderMouse
    exec.executeMouseMove(startPos + Point<int>(2, 0), {}, getElapsedMs());
    exec.executeMouseMove(startPos, {}, getElapsedMs());
    
    // MouseDown at start
    exec.executeMouseDown(startPos, mods, false, getElapsedMs());
    log.add(createLogEntry("mouseDown", startPos, getElapsedMs()));
    
    // Interpolate drag
    int duration = mouse.durationMs;
    int numSteps = jmax(1, duration / DRAG_STEP_INTERVAL_MS);
    int stepDuration = duration / numSteps;
    
    for (int i = 1; i <= numSteps; ++i)
    {
        float t = static_cast<float>(i) / numSteps;
        auto delta = (endPos - startPos).toFloat() * t;
        Point<int> pos = startPos + Point<int>(roundToInt(delta.x), roundToInt(delta.y));
        
        exec.executeMouseMove(pos, mods, getElapsedMs());
        exec.setCursorPosition(pos);
        
        if (i < numSteps)
            waitForDuration(stepDuration, exec);
    }
    
    // MouseUp at end
    exec.executeMouseUp(endPos, mods, false, getElapsedMs());
    exec.setCursorPosition(endPos);
    
    auto upEntry = createLogEntry("mouseUp", endPos, getElapsedMs());
    upEntry.getDynamicObject()->setProperty("dragDelta", 
        var(new DynamicObject()));
    upEntry.getDynamicObject()->getProperty("dragDelta").getDynamicObject()->setProperty("x", mouse.deltaPixels.x);
    upEntry.getDynamicObject()->getProperty("dragDelta").getDynamicObject()->setProperty("y", mouse.deltaPixels.y);
    log.add(upEntry);
    
    // Let UI settle after drag
    waitForDuration(UI_SETTLE_DELAY_MS, exec);
}

//==============================================================================
// Execute Screenshot
//==============================================================================

void InteractionDispatcher::executeScreenshot(
    const InteractionParser::MouseInteraction& mouse,
    Executor& exec,
    Array<var>& log)
{
    exec.executeScreenshot(mouse.screenshotId, mouse.screenshotScale, getElapsedMs());
    
    auto entry = createLogEntry("screenshot", {}, getElapsedMs());
    entry.getDynamicObject()->setProperty("id", mouse.screenshotId);
    entry.getDynamicObject()->setProperty("scale", mouse.screenshotScale);
    log.add(entry);
}

//==============================================================================
// Execute SelectMenuItem
//==============================================================================

void InteractionDispatcher::executeSelectMenuItem(
    const InteractionParser::MouseInteraction& mouse,
    Executor& exec,
    Array<var>& log)
{
    lastSelectedMenuItem = {};
    
    auto menuItems = exec.getVisibleMenuItems();
    
    if (menuItems.isEmpty())
    {
        lastError = "No popup menu is currently open";
        return;
    }
    
    // Build StringArray of item texts for fuzzy matching
    StringArray itemTexts;
    for (const auto& item : menuItems)
        itemTexts.add(item.text);
    
    // Find best match (sorted by Levenshtein distance, first element is best)
    auto matches = FuzzySearcher::searchForIndexes(mouse.menuItemText, itemTexts, 0.4, true);
    int matchedIndex = matches.isEmpty() ? -1 : matches.getFirst();
    
    if (matchedIndex < 0)
    {
        // No match found - provide helpful error with suggestion
        String availableItems;
        for (int i = 0; i < jmin(5, itemTexts.size()); i++)
        {
            if (i > 0) availableItems += ", ";
            availableItems += "'" + itemTexts[i] + "'";
        }
        if (itemTexts.size() > 5)
            availableItems += ", ... (" + String(itemTexts.size() - 5) + " more)";
        
        String suggestion = FuzzySearcher::suggestCorrection(mouse.menuItemText, itemTexts, 0.3);
        
        lastError = "Menu item '" + mouse.menuItemText + "' not found.";
        if (suggestion.isNotEmpty())
            lastError += " Did you mean '" + suggestion + "'?";
        lastError += " Available: " + availableItems;
        return;
    }
    
    auto& matchedItem = menuItems.getReference(matchedIndex);
    
    // Store matched item info
    lastSelectedMenuItem.text = matchedItem.text;
    lastSelectedMenuItem.itemId = matchedItem.itemId;
    lastSelectedMenuItem.wasSelected = true;
    
    // Move to menu item
    Point<int> startPos = exec.getCurrentCursorPosition();
    Point<int> targetPos = matchedItem.screenBounds.getCentre();
    
    int duration = mouse.durationMs;
    int numSteps = jmax(1, duration / MOVE_STEP_INTERVAL_MS);
    int stepDuration = duration / numSteps;
    
    for (int i = 1; i <= numSteps; ++i)
    {
        float t = static_cast<float>(i) / numSteps;
        auto delta = (targetPos - startPos).toFloat() * t;
        Point<int> pos = startPos + Point<int>(roundToInt(delta.x), roundToInt(delta.y));
        
        exec.executeMouseMove(pos, {}, getElapsedMs());
        exec.setCursorPosition(pos);
        
        if (i < numSteps)
            waitForDuration(stepDuration, exec);
    }
    
    // Wiggle: small move to force JUCE to update componentUnderMouse
    exec.executeMouseMove(targetPos + Point<int>(2, 0), {}, getElapsedMs());
    exec.executeMouseMove(targetPos, {}, getElapsedMs());
    
    // Click on menu item
    auto mods = ModifierKeys(ModifierKeys::leftButtonModifier);
    exec.executeMouseDown(targetPos, mods, false, getElapsedMs());
    waitForDuration(20, exec);
    exec.executeMouseUp(targetPos, mods, false, getElapsedMs());
    
    // Log
    auto entry = createLogEntry("selectMenuItem", targetPos, getElapsedMs());
    entry.getDynamicObject()->setProperty("menuItem", matchedItem.text);
    entry.getDynamicObject()->setProperty("menuItemId", matchedItem.itemId);
    log.add(entry);
    
    // Let UI settle after menu selection (menu closes, value updates)
    waitForDuration(UI_SETTLE_DELAY_MS, exec);
}

//==============================================================================
// Helper methods
//==============================================================================

var InteractionDispatcher::createLogEntry(const String& type, Point<int> pixelPos, int elapsedMs) const
{
    DynamicObject::Ptr obj = new DynamicObject();
    obj->setProperty("type", type);
    obj->setProperty("x", pixelPos.x);
    obj->setProperty("y", pixelPos.y);
    obj->setProperty("elapsedMs", elapsedMs);
    return var(obj.get());
}

//==============================================================================
// TestExecutor implementation
//==============================================================================

void TestExecutor::reset()
{
    log.clear();
    mockComponents.clear();
    mockMenuItems.clear();
    cursorPosition = {0, 0};
}

void TestExecutor::addMockComponent(const String& id, Rectangle<int> bounds, bool visible)
{
    mockComponents[id.toStdString()] = { bounds, visible };
}

void TestExecutor::clearMockComponents()
{
    mockComponents.clear();
}

void TestExecutor::addMockMenuItem(const String& text, int itemId, Rectangle<int> bounds)
{
    PopupMenu::VisibleMenuItem item;
    item.text = text;
    item.itemId = itemId;
    item.screenBounds = bounds;
    mockMenuItems.add(item);
}

void TestExecutor::clearMockMenuItems()
{
    mockMenuItems.clear();
}

InteractionDispatcher::Executor::ResolveResult TestExecutor::resolveTarget(const String& componentId)
{
    ResolveResult result;
    
    auto it = mockComponents.find(componentId.toStdString());
    if (it == mockComponents.end())
    {
        result.error = "Component '" + componentId + "' not found";
        return result;
    }
    
    auto& mock = it->second;
    result.componentBounds = mock.absoluteBounds;
    result.visible = mock.visible;
    
    if (!mock.visible)
    {
        result.error = "Component '" + componentId + "' is not visible";
        return result;
    }
    
    if (mock.absoluteBounds.isEmpty())
    {
        result.error = "Component '" + componentId + "' has zero size";
        return result;
    }
    
    return result;
}

void TestExecutor::executeMouseDown(Point<int> pixelPos, ModifierKeys mods,
                                     bool rightClick, int elapsedMs)
{
    cursorPosition = pixelPos;
    
    LogEntry entry;
    entry.type = "mouseDown";
    entry.pixelPos = pixelPos;
    entry.mods = mods;
    entry.rightClick = rightClick;
    entry.elapsedMs = elapsedMs;
    log.add(entry);
}

void TestExecutor::executeMouseUp(Point<int> pixelPos, ModifierKeys mods,
                                   bool rightClick, int elapsedMs)
{
    cursorPosition = pixelPos;
    
    LogEntry entry;
    entry.type = "mouseUp";
    entry.pixelPos = pixelPos;
    entry.mods = mods;
    entry.rightClick = rightClick;
    entry.elapsedMs = elapsedMs;
    log.add(entry);
}

void TestExecutor::executeMouseMove(Point<int> pixelPos, ModifierKeys mods, int elapsedMs)
{
    cursorPosition = pixelPos;
    
    LogEntry entry;
    entry.type = "mouseMove";
    entry.pixelPos = pixelPos;
    entry.mods = mods;
    entry.elapsedMs = elapsedMs;
    log.add(entry);
}

void TestExecutor::executeScreenshot(const String& id, float scale, int elapsedMs)
{
    LogEntry entry;
    entry.type = "screenshot";
    entry.screenshotId = id;
    entry.screenshotScale = scale;
    entry.elapsedMs = elapsedMs;
    log.add(entry);
}

} // namespace hise
