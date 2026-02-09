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
        return ExecutionResult::fail("UI failed to become ready within timeout", 0, 0);
    }
    
    // Start timing AFTER ready - user timestamps are relative to ready state
    startTimeMs = Time::getMillisecondCounterHiRes();
    
    for (const auto& interaction : interactions)
    {
        // Wait until this interaction's timestamp (no adjustment needed)
        if (!waitUntilTimestamp(interaction.getTimestamp(), executor))
        {
            String error = checkAbortConditions(executor);
            executor.executeSyntheticModeEnd(getElapsedMs());
            return ExecutionResult::fail(error, getElapsedMs(), completedCount);
        }
        
        // Check abort conditions before executing
        String error = checkAbortConditions(executor);
        if (error.isNotEmpty())
        {
            executor.executeSyntheticModeEnd(getElapsedMs());
            return ExecutionResult::fail(error, getElapsedMs(), completedCount);
        }
        
        // Execute the interaction
        if (interaction.isMidi)
        {
            // MIDI interactions - skip for now (MVP is mouse only)
            // TODO: Implement MIDI injection
        }
        else
        {
            using Type = InteractionParser::MouseInteraction::Type;
            
            switch (interaction.mouse.type)
            {
                case Type::Click:       executeClick(interaction.mouse, executor, executedLog); break;
                case Type::DoubleClick: executeDoubleClick(interaction.mouse, executor, executedLog); break;
                case Type::Drag:        executeDrag(interaction.mouse, executor, executedLog); break;
                case Type::Hover:       executeHover(interaction.mouse, executor, executedLog); break;
                case Type::Move:        executeMove(interaction.mouse, executor, executedLog); break;
                case Type::Exit:        executeExit(interaction.mouse, executor, executedLog); break;
                case Type::Screenshot:  executeScreenshot(interaction.mouse, executor, executedLog); break;
                case Type::SelectMenuItem: executeSelectMenuItem(interaction.mouse, executor, executedLog); break;
            }
        }
        
        // Check if resolution failed (critical error - hallucinated component ID)
        if (lastError.isNotEmpty())
        {
            executor.executeSyntheticModeEnd(getElapsedMs());
            return ExecutionResult::fail(lastError, getElapsedMs(), completedCount);
        }
        
        completedCount++;
        
        // Check again after execution
        error = checkAbortConditions(executor);
        if (error.isNotEmpty())
        {
            executor.executeSyntheticModeEnd(getElapsedMs());
            return ExecutionResult::fail(error, getElapsedMs(), completedCount);
        }
    }
    
    // End synthetic mode (RealExecutor handles its own cleanup delay internally)
    executor.executeSyntheticModeEnd(getElapsedMs());
    
    return ExecutionResult::ok(getElapsedMs(), completedCount);
}

int InteractionDispatcher::getElapsedMs() const
{
    return (int)(Time::getMillisecondCounterHiRes() - startTimeMs);
}

bool InteractionDispatcher::waitUntilTimestamp(int targetMs, Executor& executor)
{
    while (getElapsedMs() < targetMs)
    {
        // Check for abort conditions
        if (checkAbortConditions(executor).isNotEmpty())
            return false;
        
        // Pump message loop - this allows UI updates and mouse event processing
        MessageManager::getInstance()->runDispatchLoopUntil(5);
    }
    
    return true;
}

String InteractionDispatcher::checkAbortConditions(Executor& executor) const
{
    if (getElapsedMs() > timeoutMs)
    {
        return "Timeout after " + String(timeoutMs) + "ms";
    }
    
    if (executor.wasHumanInterventionDetected())
    {
        return "Illegal human intervention detected at " + 
               String(executor.getInterventionTimestamp()) + "ms";
    }
    
    return {};
}

//==============================================================================
// Individual interaction execution methods
//==============================================================================

void InteractionDispatcher::executeClick(const InteractionParser::MouseInteraction& mouse, 
                                          Executor& exec, Array<var>& log)
{
    // Resolve target to pixel coordinates
    auto resolved = exec.resolveTarget(mouse.targetComponentId, mouse.fromNormalized);
    if (!resolved.success())
    {
        lastError = resolved.error;
        return;
    }
    
    auto mods = buildModifiers(mouse);
    int actualTime = getElapsedMs();
    
    // Click is: mouseDown, then mouseUp
    exec.executeMouseDown(resolved.pixelPosition, mouse.targetComponentId, mods, mouse.rightClick, mouse.timestampMs);
    log.add(createLogEntry("mouseDown", mouse.targetComponentId, mouse.fromNormalized, mouse.timestampMs, actualTime));
    
    waitUntilTimestamp(mouse.timestampMs + 20, exec);

    int upTime = getElapsedMs();
    exec.executeMouseUp(resolved.pixelPosition, mouse.targetComponentId, mods, mouse.rightClick, mouse.timestampMs);
    log.add(createLogEntry("mouseUp", mouse.targetComponentId, mouse.fromNormalized, mouse.timestampMs, upTime));
}

void InteractionDispatcher::executeDoubleClick(const InteractionParser::MouseInteraction& mouse, 
                                                Executor& exec, Array<var>& log)
{
    // Resolve target to pixel coordinates
    auto resolved = exec.resolveTarget(mouse.targetComponentId, mouse.fromNormalized);
    if (!resolved.success())
    {
        lastError = resolved.error;
        return;
    }
    
    auto mods = buildModifiers(mouse);
    int actualTime = getElapsedMs();
    
    // Double click is: down, up, down, up (quickly)
    exec.executeMouseDown(resolved.pixelPosition, mouse.targetComponentId, mods, mouse.rightClick, mouse.timestampMs);
    log.add(createLogEntry("mouseDown", mouse.targetComponentId, mouse.fromNormalized, mouse.timestampMs, actualTime));
    
    MessageManager::getInstance()->runDispatchLoopUntil(2);
    
    exec.executeMouseUp(resolved.pixelPosition, mouse.targetComponentId, mods, mouse.rightClick, mouse.timestampMs);
    log.add(createLogEntry("mouseUp", mouse.targetComponentId, mouse.fromNormalized, mouse.timestampMs, getElapsedMs()));
    
    MessageManager::getInstance()->runDispatchLoopUntil(5);
    
    exec.executeMouseDown(resolved.pixelPosition, mouse.targetComponentId, mods, mouse.rightClick, mouse.timestampMs);
    log.add(createLogEntry("mouseDown", mouse.targetComponentId, mouse.fromNormalized, mouse.timestampMs, getElapsedMs()));
    
    MessageManager::getInstance()->runDispatchLoopUntil(2);
    
    exec.executeMouseUp(resolved.pixelPosition, mouse.targetComponentId, mods, mouse.rightClick, mouse.timestampMs);
    log.add(createLogEntry("mouseUp", mouse.targetComponentId, mouse.fromNormalized, mouse.timestampMs, getElapsedMs()));
}

void InteractionDispatcher::executeDrag(const InteractionParser::MouseInteraction& mouse, 
                                         Executor& exec, Array<var>& log)
{
    // Resolve both from and to positions
    auto resolvedFrom = exec.resolveTarget(mouse.targetComponentId, mouse.fromNormalized);
    if (!resolvedFrom.success())
    {
        lastError = resolvedFrom.error;
        return;
    }
    
    auto resolvedTo = exec.resolveTarget(mouse.targetComponentId, mouse.toNormalized);
    if (!resolvedTo.success())
    {
        lastError = resolvedTo.error;
        return;
    }
    
    auto mods = buildModifiers(mouse);
    auto fromPixel = resolvedFrom.pixelPosition;
    auto toPixel = resolvedTo.pixelPosition;
    int duration = mouse.durationMs;
    int startTimestamp = mouse.timestampMs;
    
    // Calculate number of steps
    int numSteps = jmax(1, duration / DRAG_STEP_INTERVAL_MS);
    
    // Mouse down at start position
    int actualTime = getElapsedMs();
    exec.executeMouseDown(fromPixel, mouse.targetComponentId, mods, mouse.rightClick, startTimestamp);
    log.add(createLogEntry("mouseDown", mouse.targetComponentId, mouse.fromNormalized, startTimestamp, actualTime));
    
    // Interpolate mouse moves in pixel space
    for (int i = 1; i < numSteps; i++)
    {
        float t = (float)i / (float)numSteps;
        Point<int> pixelPos = fromPixel + ((toPixel - fromPixel).toFloat() * t).toInt();
        Point<float> normPos = mouse.fromNormalized + (mouse.toNormalized - mouse.fromNormalized) * t;
        int stepTimestamp = startTimestamp + (duration * i / numSteps);
        
        // Wait until this step's timestamp
        waitUntilTimestamp(stepTimestamp, exec);
        
        actualTime = getElapsedMs();
        exec.executeMouseMove(pixelPos, mouse.targetComponentId, mods, stepTimestamp);
        log.add(createLogEntry("mouseMove", mouse.targetComponentId, normPos, stepTimestamp, actualTime));
    }
    
    // Wait until end timestamp
    int endTimestamp = startTimestamp + duration;
    waitUntilTimestamp(endTimestamp, exec);
    
    // Mouse up at end position
    actualTime = getElapsedMs();
    exec.executeMouseUp(toPixel, mouse.targetComponentId, mods, mouse.rightClick, endTimestamp);
    log.add(createLogEntry("mouseUp", mouse.targetComponentId, mouse.toNormalized, endTimestamp, actualTime));
}

void InteractionDispatcher::executeHover(const InteractionParser::MouseInteraction& mouse, 
                                          Executor& exec, Array<var>& log)
{
    // Resolve target to pixel coordinates
    auto resolved = exec.resolveTarget(mouse.targetComponentId, mouse.fromNormalized);
    if (!resolved.success())
    {
        lastError = resolved.error;
        return;
    }
    
    auto mods = buildModifiers(mouse);
    int actualTime = getElapsedMs();
    
    // Hover is just a mouse move to position (no button down)
    exec.executeMouseMove(resolved.pixelPosition, mouse.targetComponentId, mods, mouse.timestampMs);
    log.add(createLogEntry("mouseMove", mouse.targetComponentId, mouse.fromNormalized, mouse.timestampMs, actualTime));
    
    // If duration specified, wait at that position
    if (mouse.durationMs > 0)
    {
        waitUntilTimestamp(mouse.timestampMs + mouse.durationMs, exec);
    }
}

void InteractionDispatcher::executeMove(const InteractionParser::MouseInteraction& mouse, 
                                         Executor& exec, Array<var>& log)
{
    auto mods = buildModifiers(mouse);
    int startTimestamp = mouse.timestampMs;
    int duration = mouse.durationMs;
    
    // If path is provided, use it; otherwise interpolate from->to
    if (mouse.pathNormalized.size() > 0)
    {
        int numPoints = mouse.pathNormalized.size();
        
        for (int i = 0; i < numPoints; i++)
        {
            // Resolve each path point
            auto resolved = exec.resolveTarget(mouse.targetComponentId, mouse.pathNormalized[i]);
            if (!resolved.success())
            {
                lastError = resolved.error;
                return;
            }
            
            int stepTimestamp = startTimestamp + (duration * i / jmax(1, numPoints - 1));
            
            if (i > 0)
                waitUntilTimestamp(stepTimestamp, exec);
            
            int actualTime = getElapsedMs();
            exec.executeMouseMove(resolved.pixelPosition, mouse.targetComponentId, mods, stepTimestamp);
            log.add(createLogEntry("mouseMove", mouse.targetComponentId, mouse.pathNormalized[i], 
                                   stepTimestamp, actualTime));
        }
    }
    else
    {
        // Resolve from and to positions
        auto resolvedFrom = exec.resolveTarget(mouse.targetComponentId, mouse.fromNormalized);
        if (!resolvedFrom.success())
        {
            lastError = resolvedFrom.error;
            return;
        }
        
        auto resolvedTo = exec.resolveTarget(mouse.targetComponentId, mouse.toNormalized);
        if (!resolvedTo.success())
        {
            lastError = resolvedTo.error;
            return;
        }
        
        // Interpolate from->to similar to drag but without button down
        auto fromPixel = resolvedFrom.pixelPosition;
        auto toPixel = resolvedTo.pixelPosition;
        int numSteps = jmax(1, duration / DRAG_STEP_INTERVAL_MS);
        
        for (int i = 0; i <= numSteps; i++)
        {
            float t = (float)i / (float)numSteps;
            Point<int> pixelPos = fromPixel + ((toPixel - fromPixel).toFloat() * t).toInt();
            Point<float> normPos = mouse.fromNormalized + (mouse.toNormalized - mouse.fromNormalized) * t;
            int stepTimestamp = startTimestamp + (duration * i / numSteps);
            
            if (i > 0)
                waitUntilTimestamp(stepTimestamp, exec);
            
            int actualTime = getElapsedMs();
            exec.executeMouseMove(pixelPos, mouse.targetComponentId, mods, stepTimestamp);
            log.add(createLogEntry("mouseMove", mouse.targetComponentId, normPos, stepTimestamp, actualTime));
        }
    }
}

void InteractionDispatcher::executeExit(const InteractionParser::MouseInteraction& mouse, 
                                         Executor& exec, Array<var>& log)
{
    // For exit, we need to resolve the target first to verify it exists,
    // but then move to an off-screen position
    auto resolved = exec.resolveTarget(mouse.targetComponentId, mouse.fromNormalized);
    if (!resolved.success())
    {
        lastError = resolved.error;
        return;
    }
    
    auto mods = buildModifiers(mouse);
    int actualTime = getElapsedMs();
    
    // Exit is a mouse move to off-screen position (-1, -1)
    Point<int> offscreen(-1, -1);
    Point<float> offscreenNorm(-1.0f, -1.0f);
    exec.executeMouseMove(offscreen, mouse.targetComponentId, mods, mouse.timestampMs);
    log.add(createLogEntry("mouseExit", mouse.targetComponentId, offscreenNorm, mouse.timestampMs, actualTime));
}

void InteractionDispatcher::executeScreenshot(const InteractionParser::MouseInteraction& mouse, 
                                               Executor& exec, Array<var>& log)
{
    int actualTime = getElapsedMs();
    exec.executeScreenshot(mouse.screenshotId, mouse.screenshotScale, mouse.timestampMs);
    log.add(createScreenshotLogEntry(mouse.screenshotId, mouse.screenshotScale, 
                                     mouse.timestampMs, actualTime));
}

void InteractionDispatcher::executeSelectMenuItem(const InteractionParser::MouseInteraction& mouse, 
                                                   Executor& exec, Array<var>& log)
{
    // Reset last selected menu item
    lastSelectedMenuItem = {};
    
    // Get current cursor position (should be at the component that opened the menu)
    auto startPosition = exec.getCurrentCursorPosition();
    
    // Get all visible menu items from open popup menus
    auto menuItems = exec.getVisibleMenuItems();
    
    if (menuItems.isEmpty())
    {
        lastError = "No popup menu is currently open";
        return;
    }
    
    // Build StringArray of item texts for matching
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
    
    // Store the matched item info for response
    lastSelectedMenuItem.text = matchedItem.text;
    lastSelectedMenuItem.itemId = matchedItem.itemId;
    lastSelectedMenuItem.wasSelected = true;
    
    // Calculate target position (center of menu item)
    auto targetPosition = matchedItem.screenBounds.getCentre();
    
    // Animate mouse move from current position to menu item
    int duration = mouse.durationMs;
    int numSteps = jmax(1, duration / DRAG_STEP_INTERVAL_MS);
    int startTimestamp = mouse.timestampMs;
    
    auto mods = buildModifiers(mouse);
    
    for (int i = 0; i <= numSteps; i++)
    {
        float t = (float)i / (float)numSteps;
        Point<int> pixelPos = startPosition + ((targetPosition - startPosition).toFloat() * t).toInt();
        int stepTimestamp = startTimestamp + (duration * i / numSteps);
        
        if (i > 0)
            waitUntilTimestamp(stepTimestamp, exec);
        
        int actualTime = getElapsedMs();
        exec.executeMouseMove(pixelPos, "MenuItem:" + matchedItem.text, mods, stepTimestamp);
        
        // Log the move
        DynamicObject::Ptr moveObj = new DynamicObject();
        moveObj->setProperty("type", "mouseMove");
        moveObj->setProperty("menuItem", matchedItem.text);
        moveObj->setProperty("x", pixelPos.x);
        moveObj->setProperty("y", pixelPos.y);
        moveObj->setProperty("timestamp", stepTimestamp);
        moveObj->setProperty("actualTimestamp", actualTime);
        log.add(var(moveObj.get()));
    }
    
    // Click on the menu item
    int clickTimestamp = startTimestamp + duration;
    waitUntilTimestamp(clickTimestamp, exec);
    
    int actualTime = getElapsedMs();
    exec.executeMouseDown(targetPosition, "MenuItem:" + matchedItem.text, mods, false, clickTimestamp);
    
    // Log mouseDown
    DynamicObject::Ptr downObj = new DynamicObject();
    downObj->setProperty("type", "mouseDown");
    downObj->setProperty("menuItem", matchedItem.text);
    downObj->setProperty("menuItemId", matchedItem.itemId);
    downObj->setProperty("x", targetPosition.x);
    downObj->setProperty("y", targetPosition.y);
    downObj->setProperty("timestamp", clickTimestamp);
    downObj->setProperty("actualTimestamp", actualTime);
    log.add(var(downObj.get()));
    
    // Small delay before mouseUp
    waitUntilTimestamp(clickTimestamp + 20, exec);
    
    int upTime = getElapsedMs();
    exec.executeMouseUp(targetPosition, "MenuItem:" + matchedItem.text, mods, false, clickTimestamp);
    
    // Log mouseUp
    DynamicObject::Ptr upObj = new DynamicObject();
    upObj->setProperty("type", "mouseUp");
    upObj->setProperty("menuItem", matchedItem.text);
    upObj->setProperty("menuItemId", matchedItem.itemId);
    upObj->setProperty("x", targetPosition.x);
    upObj->setProperty("y", targetPosition.y);
    upObj->setProperty("timestamp", clickTimestamp);
    upObj->setProperty("actualTimestamp", upTime);
    log.add(var(upObj.get()));
}

//==============================================================================
// Helper methods
//==============================================================================

ModifierKeys InteractionDispatcher::buildModifiers(const InteractionParser::MouseInteraction& mouse) const
{
    int flags = 0;
    
    if (mouse.rightClick)
        flags |= ModifierKeys::rightButtonModifier;
    else
        flags |= ModifierKeys::leftButtonModifier;
    
    if (mouse.shiftDown)
        flags |= ModifierKeys::shiftModifier;
    if (mouse.ctrlDown)
        flags |= ModifierKeys::ctrlModifier;
    if (mouse.altDown)
        flags |= ModifierKeys::altModifier;
    if (mouse.cmdDown)
        flags |= ModifierKeys::commandModifier;
    
    return ModifierKeys(flags);
}

var InteractionDispatcher::createLogEntry(const String& eventType, const String& target, 
                                           Point<float> pos, int timestampMs, int actualTimestampMs) const
{
    DynamicObject::Ptr obj = new DynamicObject();
    obj->setProperty("type", eventType);
    obj->setProperty("target", target);
    obj->setProperty("x", pos.x);
    obj->setProperty("y", pos.y);
    obj->setProperty("timestamp", timestampMs);
    obj->setProperty("actualTimestamp", actualTimestampMs);
    return var(obj.get());
}

var InteractionDispatcher::createScreenshotLogEntry(const String& id, float scale, 
                                                     int timestampMs, int actualTimestampMs) const
{
    DynamicObject::Ptr obj = new DynamicObject();
    obj->setProperty("type", "screenshot");
    obj->setProperty("id", id);
    obj->setProperty("scale", scale);
    obj->setProperty("timestamp", timestampMs);
    obj->setProperty("actualTimestamp", actualTimestampMs);
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
    startTimeMs = 0;
    simulatedInterventionAtMs = -1;
    interventionTriggered = false;
    interventionTimestampMs = 0;
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

InteractionDispatcher::Executor::ResolveResult TestExecutor::resolveTarget(
    const String& componentId, Point<float> normalizedPos)
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
    
    result.pixelPosition = {
        mock.absoluteBounds.getX() + roundToInt(normalizedPos.x * mock.absoluteBounds.getWidth()),
        mock.absoluteBounds.getY() + roundToInt(normalizedPos.y * mock.absoluteBounds.getHeight())
    };
    
    return result;
}

void TestExecutor::startTiming()
{
    startTimeMs = Time::getMillisecondCounterHiRes();
}

int TestExecutor::getElapsedMs() const
{
    return (int)(Time::getMillisecondCounterHiRes() - startTimeMs);
}

void TestExecutor::executeMouseDown(Point<int> pixelPos, const String& target,
                                     ModifierKeys mods, bool rightClick, int timestampMs)
{
    checkIntervention(getElapsedMs());
    cursorPosition = pixelPos;  // Track cursor position
    
    LogEntry entry;
    entry.type = "mouseDown";
    entry.target = target;
    entry.pixelPos = pixelPos;
    entry.mods = mods;
    entry.rightClick = rightClick;
    entry.expectedTimestampMs = timestampMs;
    entry.actualTimestampMs = getElapsedMs();
    log.add(entry);
}

void TestExecutor::executeMouseUp(Point<int> pixelPos, const String& target,
                                   ModifierKeys mods, bool rightClick, int timestampMs)
{
    checkIntervention(getElapsedMs());
    cursorPosition = pixelPos;  // Track cursor position
    
    LogEntry entry;
    entry.type = "mouseUp";
    entry.target = target;
    entry.pixelPos = pixelPos;
    entry.mods = mods;
    entry.rightClick = rightClick;
    entry.expectedTimestampMs = timestampMs;
    entry.actualTimestampMs = getElapsedMs();
    log.add(entry);
}

void TestExecutor::executeMouseMove(Point<int> pixelPos, const String& target,
                                     ModifierKeys mods, int timestampMs)
{
    checkIntervention(getElapsedMs());
    cursorPosition = pixelPos;  // Track cursor position
    
    LogEntry entry;
    entry.type = "mouseMove";
    entry.target = target;
    entry.pixelPos = pixelPos;
    entry.mods = mods;
    entry.expectedTimestampMs = timestampMs;
    entry.actualTimestampMs = getElapsedMs();
    log.add(entry);
}

void TestExecutor::executeScreenshot(const String& id, float scale, int timestampMs)
{
    checkIntervention(getElapsedMs());
    
    LogEntry entry;
    entry.type = "screenshot";
    entry.screenshotId = id;
    entry.screenshotScale = scale;
    entry.expectedTimestampMs = timestampMs;
    entry.actualTimestampMs = getElapsedMs();
    log.add(entry);
}

bool TestExecutor::wasHumanInterventionDetected() const
{
    // Check if we've passed the simulated intervention time
    if (simulatedInterventionAtMs >= 0 && getElapsedMs() >= simulatedInterventionAtMs)
    {
        // Use const_cast to update mutable state (simulating real-time detection)
        auto* mutableThis = const_cast<TestExecutor*>(this);
        if (!mutableThis->interventionTriggered)
        {
            mutableThis->interventionTriggered = true;
            mutableThis->interventionTimestampMs = getElapsedMs();
        }
    }
    return interventionTriggered;
}

int TestExecutor::getInterventionTimestamp() const
{
    return interventionTimestampMs;
}

void TestExecutor::simulateHumanIntervention(int atTimestampMs)
{
    simulatedInterventionAtMs = atTimestampMs;
}

void TestExecutor::checkIntervention(int currentTimestampMs)
{
    if (simulatedInterventionAtMs >= 0 && currentTimestampMs >= simulatedInterventionAtMs && !interventionTriggered)
    {
        interventionTriggered = true;
        interventionTimestampMs = currentTimestampMs;
    }
}

} // namespace hise
