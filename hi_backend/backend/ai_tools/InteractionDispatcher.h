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

#pragma once

namespace hise {
using namespace juce;

/** Executes parsed interactions with timing control.
 *  Must be called on the message thread. Pumps message loop between events.
 */
class InteractionDispatcher
{
public:
    //==============================================================================
    /** Interface for executing individual mouse/screenshot actions.
     *  Real implementation injects into JUCE.
     *  Test implementation logs for verification.
     */
    struct Executor
    {
        virtual ~Executor() = default;
        
        //======================================================================
        /** Result of resolving a component target to pixel coordinates. */
        struct ResolveResult
        {
            Rectangle<int> componentBounds;  // Absolute bounds in content coords
            Point<int> pixelPosition;        // Click position within content
            bool visible = false;
            String error;
            
            bool success() const { return error.isEmpty(); }
        };
        
        //======================================================================
        /** Resolve a component ID and normalized position to absolute pixel coords.
         *  @param componentId   Script component ID (e.g., "Button1")
         *  @param normalizedPos Position within component (0.5, 0.5 = center)
         *  @returns             ResolveResult with pixel position or error
         */
        virtual ResolveResult resolveTarget(const String& componentId, 
                                            Point<float> normalizedPos) = 0;
        
        //======================================================================
        // Primitive mouse operations (pixel coordinates)
        virtual void executeMouseDown(Point<int> pixelPos, const String& target, 
                                      ModifierKeys mods, bool rightClick, int timestampMs) = 0;
        virtual void executeMouseUp(Point<int> pixelPos, const String& target,
                                    ModifierKeys mods, bool rightClick, int timestampMs) = 0;
        virtual void executeMouseMove(Point<int> pixelPos, const String& target,
                                      ModifierKeys mods, int timestampMs) = 0;
        
        // Screenshot capture
        virtual void executeScreenshot(const String& id, float scale, int timestampMs) = 0;
        
        // Human intervention detection
        virtual bool wasHumanInterventionDetected() const = 0;
        virtual int getInterventionTimestamp() const = 0;
        
        // Test support - simulate human intervention at specific timestamp
        virtual void simulateHumanIntervention(int atTimestampMs) {}
    };
    
    //==============================================================================
    /** Result of execution with timing info. */
    struct ExecutionResult
    {
        Result result = Result::ok();
        int totalElapsedMs = 0;
        int interactionsCompleted = 0;
        
        static ExecutionResult ok(int elapsed, int completed) 
        { 
            ExecutionResult r;
            r.totalElapsedMs = elapsed;
            r.interactionsCompleted = completed;
            return r;
        }
        
        static ExecutionResult fail(const String& message, int elapsed, int completed)
        {
            ExecutionResult r;
            r.result = Result::fail(message);
            r.totalElapsedMs = elapsed;
            r.interactionsCompleted = completed;
            return r;
        }
    };
    
    //==============================================================================
    /** Execute interactions synchronously. MUST be called on message thread.
     *
     *  @param interactions  Parsed interactions to execute
     *  @param executor      Implementation that performs the actual operations
     *  @param executedLog   Output: log of executed events for response
     *  @param timeoutMs     Maximum execution time (default 20 seconds)
     *  @returns             ExecutionResult with success/failure and timing
     */
    ExecutionResult execute(const Array<InteractionParser::Interaction>& interactions,
                            Executor& executor,
                            Array<var>& executedLog,
                            int timeoutMs = 20000);
    
    //==============================================================================
    static constexpr int DRAG_STEP_INTERVAL_MS = 10;  // Mouse move every 10ms during drag
    
private:
    int64 startTimeMs = 0;
    int timeoutMs = 20000;
    String lastError;  // Set by execute methods if resolution fails
    
    int getElapsedMs() const;
    bool waitUntilTimestamp(int targetMs, Executor& executor);
    
    // Check for timeout or human intervention, returns error message or empty string
    String checkAbortConditions(Executor& executor) const;
    
    // Execute individual interaction types
    void executeClick(const InteractionParser::MouseInteraction& mouse, Executor& exec, Array<var>& log);
    void executeDoubleClick(const InteractionParser::MouseInteraction& mouse, Executor& exec, Array<var>& log);
    void executeDrag(const InteractionParser::MouseInteraction& mouse, Executor& exec, Array<var>& log);
    void executeHover(const InteractionParser::MouseInteraction& mouse, Executor& exec, Array<var>& log);
    void executeMove(const InteractionParser::MouseInteraction& mouse, Executor& exec, Array<var>& log);
    void executeExit(const InteractionParser::MouseInteraction& mouse, Executor& exec, Array<var>& log);
    void executeScreenshot(const InteractionParser::MouseInteraction& mouse, Executor& exec, Array<var>& log);
    
    // Build modifier keys from MouseInteraction
    ModifierKeys buildModifiers(const InteractionParser::MouseInteraction& mouse) const;
    
    // Create log entry for response
    var createLogEntry(const String& eventType, const String& target, Point<float> pos, 
                       int timestampMs, int actualTimestampMs) const;
    var createScreenshotLogEntry(const String& id, float scale, int timestampMs, int actualTimestampMs) const;
};

//==============================================================================
/** Test executor that logs events for verification without actual UI injection. */
class TestExecutor : public InteractionDispatcher::Executor
{
public:
    //==========================================================================
    /** Log entry for recorded mouse/screenshot events. */
    struct LogEntry
    {
        String type;           // "mouseDown", "mouseUp", "mouseMove", "screenshot"
        String target;
        Point<int> pixelPos;   // Resolved pixel position
        ModifierKeys mods;
        bool rightClick = false;
        String screenshotId;
        float screenshotScale = 1.0f;
        int expectedTimestampMs;  // When it should have fired
        int actualTimestampMs;    // When it actually fired
    };
    
    //==========================================================================
    /** Mock component for testing resolution. */
    struct MockComponent
    {
        Rectangle<int> absoluteBounds;
        bool visible = true;
    };
    
    //==========================================================================
    Array<LogEntry> log;
    std::map<String, MockComponent> mockComponents;
    int64 startTimeMs = 0;
    
    // Human intervention simulation
    int simulatedInterventionAtMs = -1;
    bool interventionTriggered = false;
    int interventionTimestampMs = 0;
    
    //==========================================================================
    /** Reset all state for a new test. */
    void reset();
    
    /** Start timing - call this right before dispatcher.execute(). */
    void startTiming();
    
    /** Get current elapsed time since startTiming(). */
    int getElapsedMs() const;
    
    /** Add a mock component for resolution testing. */
    void addMockComponent(const String& id, Rectangle<int> bounds, bool visible = true);
    
    /** Clear all mock components. */
    void clearMockComponents();
    
    //==========================================================================
    // Executor interface implementation
    
    ResolveResult resolveTarget(const String& componentId, 
                                Point<float> normalizedPos) override;
    
    void executeMouseDown(Point<int> pixelPos, const String& target,
                          ModifierKeys mods, bool rightClick, int timestampMs) override;
    
    void executeMouseUp(Point<int> pixelPos, const String& target,
                        ModifierKeys mods, bool rightClick, int timestampMs) override;
    
    void executeMouseMove(Point<int> pixelPos, const String& target,
                          ModifierKeys mods, int timestampMs) override;
    
    void executeScreenshot(const String& id, float scale, int timestampMs) override;
    
    bool wasHumanInterventionDetected() const override;
    int getInterventionTimestamp() const override;
    void simulateHumanIntervention(int atTimestampMs) override;
    
private:
    void checkIntervention(int currentTimestampMs);
};

} // namespace hise
