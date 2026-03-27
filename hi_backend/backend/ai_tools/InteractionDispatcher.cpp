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
            return "Moving to \"" + mouse.target.toString() + "\"";
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
void logInteractionToConsole(InteractionExecutorBase& exec, const String& description)
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
    InteractionExecutorBase& executor,
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

void InteractionDispatcher::waitForDuration(int ms, InteractionExecutorBase& executor)
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

void InteractionDispatcher::waitWithWiggle(int ms, InteractionExecutorBase& exec)
{
    if (ms <= 0) return;
    
    auto endTime = Time::getMillisecondCounterHiRes() + ms;
    
    while (Time::getMillisecondCounterHiRes() < endTime)
    {
        if (checkAbortConditions().isNotEmpty())
            return;
        
        // Wiggle mouse to keep message loop active
        auto pos = exec.getCurrentCursorPosition();
        exec.executeMouseMove(pos + Point<int>(1, 0), {}, getElapsedMs());
        exec.executeMouseMove(pos, {}, getElapsedMs());
        
        // Pump message loop
        MessageManager::getInstance()->runDispatchLoopUntil(5);
    }
}

void InteractionDispatcher::wiggleToUpdateComponent(Point<int> pos, InteractionExecutorBase& exec)
{
    // Small move to force JUCE to update componentUnderMouse before mouse events
    exec.executeMouseMove(pos + Point<int>(2, 0), {}, getElapsedMs());
    exec.executeMouseMove(pos, {}, getElapsedMs());
}

void InteractionDispatcher::interpolateMovement(Point<int> startPos, Point<int> endPos, 
                                                 int durationMs, ModifierKeys mods, 
                                                 InteractionExecutorBase& exec)
{
    int numSteps = jmax(1, durationMs / MOVE_STEP_INTERVAL_MS);
    int stepDuration = durationMs / numSteps;
    
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
    InteractionExecutorBase& exec,
    Array<var>& log)
{
    // Resolve target bounds (including subtarget if specified)
    // Retry loop: target may not be visible yet (e.g., after page transition)
    auto resolved = exec.resolveTarget(mouse.target);
    
    for (int attempt = 0; !resolved.success() && attempt < RESOLVE_MAX_RETRIES; ++attempt)
    {
        if (checkAbortConditions().isNotEmpty())
            break;
        
        waitWithWiggle(RESOLVE_RETRY_DELAY_MS, exec);
        resolved = exec.resolveTarget(mouse.target);
    }
    
    // Fallback for subtargets that don't exist yet (e.g., TableEditor point created by mouseDown)
    // Use normalized position within parent component instead
    if (!resolved.success() && mouse.target.hasSubtarget() && mouse.position.hasFallback())
    {
        InteractionParser::ComponentTargetPath parentOnly(mouse.target.componentId);
        auto parentResolved = exec.resolveTarget(parentOnly);
        
        if (parentResolved.success())
        {
            // Calculate pixel position from normalized coordinates within parent bounds
            Point<int> fallbackPos(
                parentResolved.componentBounds.getX() + 
                    roundToInt(mouse.position.normalizedInParent.x * parentResolved.componentBounds.getWidth()),
                parentResolved.componentBounds.getY() + 
                    roundToInt(mouse.position.normalizedInParent.y * parentResolved.componentBounds.getHeight())
            );
            
            // Move to fallback position - subsequent click/drag will create the subtarget
            Point<int> startPos = exec.getCurrentCursorPosition();
            int duration = mouse.durationMs;
            int numSteps = jmax(1, duration / MOVE_STEP_INTERVAL_MS);
            int stepDuration = duration / numSteps;
            
            for (int i = 1; i <= numSteps; ++i)
            {
                float t = static_cast<float>(i) / numSteps;
                auto delta = (fallbackPos - startPos).toFloat() * t;
                Point<int> pos = startPos + Point<int>(roundToInt(delta.x), roundToInt(delta.y));
                
                exec.executeMouseMove(pos, mouse.modifiers, getElapsedMs());
                exec.setCursorPosition(pos);
                
                if (i < numSteps)
                    waitForDuration(stepDuration, exec);
            }
            
            exec.setCursorPosition(fallbackPos);
            
            // Log with fallback indicator
            auto entry = createLogEntry(InteractionIds::moveTo.toString(), fallbackPos, getElapsedMs());
            mouse.target.toVar(entry.getDynamicObject());
            entry.getDynamicObject()->setProperty(InteractionIds::fallback, true);
            if (mouse.autoInserted)
                entry.getDynamicObject()->setProperty(InteractionIds::autoInserted, true);
            log.add(entry);
            
            return;  // Success via fallback
        }
    }
    
    if (!resolved.success())
    {
        lastError = resolved.error;
        return;
    }
    
    Point<int> startPos = exec.getCurrentCursorPosition();
    Point<int> endPos = mouse.position.getPixelPosition(resolved.componentBounds);
    
    interpolateMovement(startPos, endPos, mouse.durationMs, mouse.modifiers, exec);
    exec.setCursorPosition(endPos);
    
    // Log
    auto entry = createLogEntry(InteractionIds::moveTo.toString(), endPos, getElapsedMs());
    mouse.target.toVar(entry.getDynamicObject());
    if (mouse.autoInserted)
        entry.getDynamicObject()->setProperty(InteractionIds::autoInserted, true);
    log.add(entry);
}

//==============================================================================
// Execute Click
//==============================================================================

void InteractionDispatcher::executeClick(
    const InteractionParser::MouseInteraction& mouse,
    InteractionExecutorBase& exec,
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
    
    wiggleToUpdateComponent(pixelPos, exec);
    
    // MouseDown
    exec.executeMouseDown(pixelPos, mods, mouse.rightClick, getElapsedMs());
    
    auto downEntry = createLogEntry(InteractionIds::mouseDown.toString(), pixelPos, getElapsedMs());
    if (mouse.rightClick)
        downEntry.getDynamicObject()->setProperty(InteractionIds::rightClick, true);
    log.add(downEntry);
    
    // Brief pause (click duration)
    waitForDuration(mouse.durationMs, exec);
    
    // MouseUp
    exec.executeMouseUp(pixelPos, mods, mouse.rightClick, getElapsedMs());
    
    auto upEntry = createLogEntry(InteractionIds::mouseUp.toString(), pixelPos, getElapsedMs());
    if (mouse.rightClick)
        upEntry.getDynamicObject()->setProperty(InteractionIds::rightClick, true);
    log.add(upEntry);
    
    // Let UI settle after click (e.g., popup menus appearing, button state changes)
    waitWithWiggle(UI_SETTLE_DELAY_MS, exec);
}

//==============================================================================
// Execute Drag
//==============================================================================

void InteractionDispatcher::executeDrag(
    const InteractionParser::MouseInteraction& mouse,
    InteractionExecutorBase& exec,
    Array<var>& log)
{
    Point<int> startPos = exec.getCurrentCursorPosition();
    Point<int> endPos = startPos + mouse.deltaPixels;
    
    // Build modifiers with left button
    auto mods = mouse.modifiers;
    mods = mods.withFlags(ModifierKeys::leftButtonModifier);
    
    wiggleToUpdateComponent(startPos, exec);
    
    // MouseDown at start
    exec.executeMouseDown(startPos, mods, false, getElapsedMs());
    log.add(createLogEntry(InteractionIds::mouseDown.toString(), startPos, getElapsedMs()));
    
    interpolateMovement(startPos, endPos, mouse.durationMs, mods, exec);
    
    // MouseUp at end
    exec.executeMouseUp(endPos, mods, false, getElapsedMs());
    exec.setCursorPosition(endPos);
    
    auto upEntry = createLogEntry(InteractionIds::mouseUp.toString(), endPos, getElapsedMs());
    upEntry.getDynamicObject()->setProperty(InteractionIds::delta, 
        var(new DynamicObject()));
    upEntry.getDynamicObject()->getProperty(InteractionIds::delta).getDynamicObject()->setProperty(RestApiIds::x, mouse.deltaPixels.x);
    upEntry.getDynamicObject()->getProperty(InteractionIds::delta).getDynamicObject()->setProperty(RestApiIds::y, mouse.deltaPixels.y);
    log.add(upEntry);
    
    // Let UI settle after drag
    waitWithWiggle(UI_SETTLE_DELAY_MS, exec);
}

//==============================================================================
// Execute Screenshot
//==============================================================================

void InteractionDispatcher::executeScreenshot(
    const InteractionParser::MouseInteraction& mouse,
    InteractionExecutorBase& exec,
    Array<var>& log)
{
    exec.executeScreenshot(mouse.screenshotId, mouse.screenshotScale, getElapsedMs());
    
    auto entry = createLogEntry(InteractionIds::screenshot.toString(), {}, getElapsedMs());
    entry.getDynamicObject()->setProperty(RestApiIds::id, mouse.screenshotId);
    entry.getDynamicObject()->setProperty(RestApiIds::scale, mouse.screenshotScale);
    log.add(entry);
}

//==============================================================================
// Execute SelectMenuItem
//==============================================================================

void InteractionDispatcher::executeSelectMenuItem(
    const InteractionParser::MouseInteraction& mouse,
    InteractionExecutorBase& exec,
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
    
    interpolateMovement(startPos, targetPos, mouse.durationMs, {}, exec);
    wiggleToUpdateComponent(targetPos, exec);
    
    // Click on menu item
    auto mods = ModifierKeys(ModifierKeys::leftButtonModifier);
    exec.executeMouseDown(targetPos, mods, false, getElapsedMs());
    waitForDuration(20, exec);
    exec.executeMouseUp(targetPos, mods, false, getElapsedMs());
    
    // Log
    auto entry = createLogEntry(InteractionIds::selectMenuItem.toString(), targetPos, getElapsedMs());
    entry.getDynamicObject()->setProperty(InteractionIds::menuItemText, matchedItem.text);
    entry.getDynamicObject()->setProperty(RestApiIds::itemId, matchedItem.itemId);
    log.add(entry);
    
    // Let UI settle after menu selection (menu closes, value updates)
    waitWithWiggle(UI_SETTLE_DELAY_MS, exec);
}

//==============================================================================
// Helper methods
//==============================================================================

var InteractionDispatcher::createLogEntry(const String& type, Point<int> pixelPos, int elapsedMs) const
{
    DynamicObject::Ptr obj = new DynamicObject();
    obj->setProperty(RestApiIds::type, type);
    obj->setProperty(RestApiIds::x, pixelPos.x);
    obj->setProperty(RestApiIds::y, pixelPos.y);
    obj->setProperty(InteractionIds::timestamp, elapsedMs);
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
    InteractionExecutorBase::VisibleMenuItem item;
    item.text = text;
    item.itemId = itemId;
    item.screenBounds = bounds;
    mockMenuItems.add(item);
}

void TestExecutor::clearMockMenuItems()
{
    mockMenuItems.clear();
}

void TestExecutor::addMockSubtarget(const String& parentId, const String& subtargetId, Rectangle<int> bounds)
{
    auto it = mockComponents.find(parentId.toStdString());
    if (it != mockComponents.end())
    {
        it->second.subtargets[subtargetId.toStdString()] = bounds;
    }
}

InteractionExecutorBase::ResolveResult TestExecutor::resolveTarget(
    const InteractionParser::ComponentTargetPath& target)
{
    InteractionExecutorBase::ResolveResult result;
    
    auto it = mockComponents.find(target.componentId.toStdString());
    if (it == mockComponents.end())
    {
        result.error = "Component '" + target.componentId + "' not found";
        return result;
    }
    
    auto& mock = it->second;
    result.componentBounds = mock.absoluteBounds;
    result.target = target;
    result.visible = mock.visible;
    
    // If subtarget specified, try to find its bounds
    if (target.hasSubtarget())
    {
        auto subIt = mock.subtargets.find(target.subtargetId.toStdString());
        if (subIt != mock.subtargets.end())
        {
            result.componentBounds = subIt->second;
        }
    }
    
    if (!mock.visible)
    {
        result.error = "Component '" + target.componentId + "' is not visible";
        return result;
    }
    
    if (mock.absoluteBounds.isEmpty())
    {
        result.error = "Component '" + target.componentId + "' has zero size";
        return result;
    }
    
    return result;
}

InteractionExecutorBase::ResolveResult TestExecutor::resolvePosition(Point<int> absolutePos) const
{
    InteractionExecutorBase::ResolveResult result;
    
    // Check subtargets first (they're on top)
    for (const auto& [id, comp] : mockComponents)
    {
        if (!comp.visible) continue;
        
        for (const auto& [subtargetId, bounds] : comp.subtargets)
        {
            if (bounds.contains(absolutePos))
            {
                result.target = InteractionParser::ComponentTargetPath(id, subtargetId);
                result.componentBounds = comp.absoluteBounds;
                result.visible = true;
                return result;
            }
        }
    }
    
    // Then check main components
    for (const auto& [id, comp] : mockComponents)
    {
        if (!comp.visible) continue;
        
        if (comp.absoluteBounds.contains(absolutePos))
        {
            result.target = InteractionParser::ComponentTargetPath(id);
            result.componentBounds = comp.absoluteBounds;
            result.visible = true;
            return result;
        }
    }
    
    result.error = "No component at position (" + String(absolutePos.x) + ", " + String(absolutePos.y) + ")";
    return result;
}

void TestExecutor::executeMouseDown(Point<int> pixelPos, ModifierKeys mods,
                                     bool rightClick, int elapsedMs)
{
    cursorPosition = pixelPos;
    
    LogEntry entry;
    entry.type = InteractionIds::mouseDown;
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
    entry.type = InteractionIds::mouseUp;
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
    entry.type = InteractionIds::mouseMove;
    entry.pixelPos = pixelPos;
    entry.mods = mods;
    entry.elapsedMs = elapsedMs;
    log.add(entry);
}

void TestExecutor::executeScreenshot(const String& id, float scale, int elapsedMs)
{
    LogEntry entry;
    entry.type = InteractionIds::screenshot;
    entry.screenshotId = id;
    entry.screenshotScale = scale;
    entry.elapsedMs = elapsedMs;
    log.add(entry);
}

} // namespace hise
