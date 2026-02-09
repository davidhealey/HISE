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
    
    startTimeMs = Time::getMillisecondCounterHiRes();
    timeoutMs = timeout;
    lastError = {};  // Reset error state
    
    int completedCount = 0;
    
    // Start synthetic input mode immediately (no delay)
    executor.executeSyntheticModeStart(0);
    
    // Calculate the last event timestamp (after adding window init delay)
    int lastEventTimestamp = 0;
    for (const auto& interaction : interactions)
    {
        int adjustedTimestamp = interaction.getTimestamp() + WINDOW_INIT_DELAY_MS;
        
        // For drag/hover/move, also consider duration
        if (!interaction.isMidi)
        {
            adjustedTimestamp += interaction.mouse.durationMs;
        }
        
        lastEventTimestamp = jmax(lastEventTimestamp, adjustedTimestamp);
    }
    
    for (const auto& interaction : interactions)
    {
        // Add window init delay to all user event timestamps
        int adjustedTimestamp = interaction.getTimestamp() + WINDOW_INIT_DELAY_MS;
        
        // Wait until this interaction's adjusted timestamp
        if (!waitUntilTimestamp(adjustedTimestamp, executor))
        {
            // Aborted due to timeout or human intervention
            String error = checkAbortConditions(executor);
            executor.executeSyntheticModeEnd(getElapsedMs());  // Cleanup on abort
            return ExecutionResult::fail(error, getElapsedMs(), completedCount);
        }
        
        // Check abort conditions before executing
        String error = checkAbortConditions(executor);
        if (error.isNotEmpty())
        {
            executor.executeSyntheticModeEnd(getElapsedMs());  // Cleanup on abort
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
            
            // Create a copy with adjusted timestamp for execution
            auto adjustedMouse = interaction.mouse;
            adjustedMouse.timestampMs += WINDOW_INIT_DELAY_MS;
            
            switch (interaction.mouse.type)
            {
                case Type::Click:       executeClick(adjustedMouse, executor, executedLog); break;
                case Type::DoubleClick: executeDoubleClick(adjustedMouse, executor, executedLog); break;
                case Type::Drag:        executeDrag(adjustedMouse, executor, executedLog); break;
                case Type::Hover:       executeHover(adjustedMouse, executor, executedLog); break;
                case Type::Move:        executeMove(adjustedMouse, executor, executedLog); break;
                case Type::Exit:        executeExit(adjustedMouse, executor, executedLog); break;
                case Type::Screenshot:  executeScreenshot(adjustedMouse, executor, executedLog); break;
            }
        }
        
        // Check if resolution failed (critical error - hallucinated component ID)
        if (lastError.isNotEmpty())
        {
            executor.executeSyntheticModeEnd(getElapsedMs());  // Cleanup on error
            return ExecutionResult::fail(lastError, getElapsedMs(), completedCount);
        }
        
        completedCount++;
        
        // Check again after execution
        error = checkAbortConditions(executor);
        if (error.isNotEmpty())
        {
            executor.executeSyntheticModeEnd(getElapsedMs());  // Cleanup on abort
            return ExecutionResult::fail(error, getElapsedMs(), completedCount);
        }
    }
    
    // Wait for synthetic mode end delay, then end synthetic mode
    // BUT: If a modal component is open (e.g., user right-clicked and opened a context menu),
    // don't wait - the modal event loop would trap us. Just end synthetic mode immediately.
    int syntheticModeEndTimestamp = lastEventTimestamp + SYNTHETIC_MODE_END_DELAY_MS;
    if (Component::getNumCurrentlyModalComponents() == 0)
    {
        waitUntilTimestamp(syntheticModeEndTimestamp, executor);
    }
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
