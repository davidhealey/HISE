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
    
    int completedCount = 0;
    
    for (const auto& interaction : interactions)
    {
        // Wait until this interaction's timestamp
        if (!waitUntilTimestamp(interaction.getTimestamp(), executor))
        {
            // Aborted due to timeout or human intervention
            String error = checkAbortConditions(executor);
            return ExecutionResult::fail(error, getElapsedMs(), completedCount);
        }
        
        // Check abort conditions before executing
        String error = checkAbortConditions(executor);
        if (error.isNotEmpty())
        {
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
            }
        }
        
        completedCount++;
        
        // Check again after execution
        error = checkAbortConditions(executor);
        if (error.isNotEmpty())
        {
            return ExecutionResult::fail(error, getElapsedMs(), completedCount);
        }
    }
    
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
    auto mods = buildModifiers(mouse);
    auto pos = mouse.fromNormalized;
    int actualTime = getElapsedMs();
    
    // Click is: mouseDown, then mouseUp
    exec.executeMouseDown(pos, mouse.targetComponentId, mods, mouse.rightClick, mouse.timestampMs);
    log.add(createLogEntry("mouseDown", mouse.targetComponentId, pos, mouse.timestampMs, actualTime));
    
    // Small delay between down and up
    MessageManager::getInstance()->runDispatchLoopUntil(5);
    
    int upTime = getElapsedMs();
    exec.executeMouseUp(pos, mouse.targetComponentId, mods, mouse.rightClick, mouse.timestampMs);
    log.add(createLogEntry("mouseUp", mouse.targetComponentId, pos, mouse.timestampMs, upTime));
}

void InteractionDispatcher::executeDoubleClick(const InteractionParser::MouseInteraction& mouse, 
                                                Executor& exec, Array<var>& log)
{
    auto mods = buildModifiers(mouse);
    auto pos = mouse.fromNormalized;
    int actualTime = getElapsedMs();
    
    // Double click is: down, up, down, up (quickly)
    exec.executeMouseDown(pos, mouse.targetComponentId, mods, mouse.rightClick, mouse.timestampMs);
    log.add(createLogEntry("mouseDown", mouse.targetComponentId, pos, mouse.timestampMs, actualTime));
    
    MessageManager::getInstance()->runDispatchLoopUntil(2);
    
    exec.executeMouseUp(pos, mouse.targetComponentId, mods, mouse.rightClick, mouse.timestampMs);
    log.add(createLogEntry("mouseUp", mouse.targetComponentId, pos, mouse.timestampMs, getElapsedMs()));
    
    MessageManager::getInstance()->runDispatchLoopUntil(5);
    
    exec.executeMouseDown(pos, mouse.targetComponentId, mods, mouse.rightClick, mouse.timestampMs);
    log.add(createLogEntry("mouseDown", mouse.targetComponentId, pos, mouse.timestampMs, getElapsedMs()));
    
    MessageManager::getInstance()->runDispatchLoopUntil(2);
    
    exec.executeMouseUp(pos, mouse.targetComponentId, mods, mouse.rightClick, mouse.timestampMs);
    log.add(createLogEntry("mouseUp", mouse.targetComponentId, pos, mouse.timestampMs, getElapsedMs()));
}

void InteractionDispatcher::executeDrag(const InteractionParser::MouseInteraction& mouse, 
                                         Executor& exec, Array<var>& log)
{
    auto mods = buildModifiers(mouse);
    auto from = mouse.fromNormalized;
    auto to = mouse.toNormalized;
    int duration = mouse.durationMs;
    int startTimestamp = mouse.timestampMs;
    
    // Calculate number of steps
    int numSteps = jmax(1, duration / DRAG_STEP_INTERVAL_MS);
    
    // Mouse down at start position
    int actualTime = getElapsedMs();
    exec.executeMouseDown(from, mouse.targetComponentId, mods, mouse.rightClick, startTimestamp);
    log.add(createLogEntry("mouseDown", mouse.targetComponentId, from, startTimestamp, actualTime));
    
    // Interpolate mouse moves
    for (int i = 1; i < numSteps; i++)
    {
        float t = (float)i / (float)numSteps;
        Point<float> pos = from + (to - from) * t;
        int stepTimestamp = startTimestamp + (duration * i / numSteps);
        
        // Wait until this step's timestamp
        waitUntilTimestamp(stepTimestamp, exec);
        
        actualTime = getElapsedMs();
        exec.executeMouseMove(pos, mouse.targetComponentId, mods, stepTimestamp);
        log.add(createLogEntry("mouseMove", mouse.targetComponentId, pos, stepTimestamp, actualTime));
    }
    
    // Wait until end timestamp
    int endTimestamp = startTimestamp + duration;
    waitUntilTimestamp(endTimestamp, exec);
    
    // Mouse up at end position
    actualTime = getElapsedMs();
    exec.executeMouseUp(to, mouse.targetComponentId, mods, mouse.rightClick, endTimestamp);
    log.add(createLogEntry("mouseUp", mouse.targetComponentId, to, endTimestamp, actualTime));
}

void InteractionDispatcher::executeHover(const InteractionParser::MouseInteraction& mouse, 
                                          Executor& exec, Array<var>& log)
{
    auto mods = buildModifiers(mouse);
    auto pos = mouse.fromNormalized;
    int actualTime = getElapsedMs();
    
    // Hover is just a mouse move to position (no button down)
    exec.executeMouseMove(pos, mouse.targetComponentId, mods, mouse.timestampMs);
    log.add(createLogEntry("mouseMove", mouse.targetComponentId, pos, mouse.timestampMs, actualTime));
    
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
            int stepTimestamp = startTimestamp + (duration * i / jmax(1, numPoints - 1));
            
            if (i > 0)
                waitUntilTimestamp(stepTimestamp, exec);
            
            int actualTime = getElapsedMs();
            exec.executeMouseMove(mouse.pathNormalized[i], mouse.targetComponentId, mods, stepTimestamp);
            log.add(createLogEntry("mouseMove", mouse.targetComponentId, mouse.pathNormalized[i], 
                                   stepTimestamp, actualTime));
        }
    }
    else
    {
        // Interpolate from->to similar to drag but without button down
        auto from = mouse.fromNormalized;
        auto to = mouse.toNormalized;
        int numSteps = jmax(1, duration / DRAG_STEP_INTERVAL_MS);
        
        for (int i = 0; i <= numSteps; i++)
        {
            float t = (float)i / (float)numSteps;
            Point<float> pos = from + (to - from) * t;
            int stepTimestamp = startTimestamp + (duration * i / numSteps);
            
            if (i > 0)
                waitUntilTimestamp(stepTimestamp, exec);
            
            int actualTime = getElapsedMs();
            exec.executeMouseMove(pos, mouse.targetComponentId, mods, stepTimestamp);
            log.add(createLogEntry("mouseMove", mouse.targetComponentId, pos, stepTimestamp, actualTime));
        }
    }
}

void InteractionDispatcher::executeExit(const InteractionParser::MouseInteraction& mouse, 
                                         Executor& exec, Array<var>& log)
{
    auto mods = buildModifiers(mouse);
    int actualTime = getElapsedMs();
    
    // Exit is a mouse move to off-screen position (-1, -1)
    Point<float> offscreen(-1.0f, -1.0f);
    exec.executeMouseMove(offscreen, mouse.targetComponentId, mods, mouse.timestampMs);
    log.add(createLogEntry("mouseExit", mouse.targetComponentId, offscreen, mouse.timestampMs, actualTime));
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
    startTimeMs = 0;
    simulatedInterventionAtMs = -1;
    interventionTriggered = false;
    interventionTimestampMs = 0;
}

void TestExecutor::startTiming()
{
    startTimeMs = Time::getMillisecondCounterHiRes();
}

int TestExecutor::getElapsedMs() const
{
    return (int)(Time::getMillisecondCounterHiRes() - startTimeMs);
}

void TestExecutor::executeMouseDown(Point<float> normPos, const String& target,
                                     ModifierKeys mods, bool rightClick, int timestampMs)
{
    checkIntervention(getElapsedMs());
    
    LogEntry entry;
    entry.type = "mouseDown";
    entry.target = target;
    entry.position = normPos;
    entry.mods = mods;
    entry.rightClick = rightClick;
    entry.expectedTimestampMs = timestampMs;
    entry.actualTimestampMs = getElapsedMs();
    log.add(entry);
}

void TestExecutor::executeMouseUp(Point<float> normPos, const String& target,
                                   ModifierKeys mods, bool rightClick, int timestampMs)
{
    checkIntervention(getElapsedMs());
    
    LogEntry entry;
    entry.type = "mouseUp";
    entry.target = target;
    entry.position = normPos;
    entry.mods = mods;
    entry.rightClick = rightClick;
    entry.expectedTimestampMs = timestampMs;
    entry.actualTimestampMs = getElapsedMs();
    log.add(entry);
}

void TestExecutor::executeMouseMove(Point<float> normPos, const String& target,
                                     ModifierKeys mods, int timestampMs)
{
    checkIntervention(getElapsedMs());
    
    LogEntry entry;
    entry.type = "mouseMove";
    entry.target = target;
    entry.position = normPos;
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
