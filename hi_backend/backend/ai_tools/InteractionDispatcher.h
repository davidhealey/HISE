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
        /** Result of resolving a component target to bounds. */
        struct ResolveResult
        {
            Rectangle<int> componentBounds;  // Absolute bounds in Interface coords
            bool visible = false;
            String error;
            
            bool success() const { return error.isEmpty(); }
        };
        
        //======================================================================
        /** Resolve a component ID to its bounds.
         *  @param componentId   Script component ID (e.g., "Button1")
         *  @returns             ResolveResult with bounds or error
         */
        virtual ResolveResult resolveTarget(const String& componentId) = 0;
        
        //======================================================================
        // Primitive mouse operations (pixel coordinates)
        virtual void executeMouseDown(Point<int> pixelPos, ModifierKeys mods, 
                                      bool rightClick, int elapsedMs) = 0;
        virtual void executeMouseUp(Point<int> pixelPos, ModifierKeys mods,
                                    bool rightClick, int elapsedMs) = 0;
        virtual void executeMouseMove(Point<int> pixelPos, ModifierKeys mods,
                                      int elapsedMs) = 0;
        
        // Screenshot capture
        virtual void executeScreenshot(const String& id, float scale, int elapsedMs) = 0;
        
        // Synthetic input mode control
        virtual void executeSyntheticModeStart(int elapsedMs) = 0;
        virtual void executeSyntheticModeEnd(int elapsedMs) = 0;
        
        //======================================================================
        // Cursor and menu state
        
        /** Get current cursor position in Interface coordinates. */
        virtual Point<int> getCurrentCursorPosition() const = 0;
        
        /** Set cursor position (for state tracking). */
        virtual void setCursorPosition(Point<int> pos) = 0;
        
        /** Get all currently visible menu items from open popup menus. */
        virtual Array<PopupMenu::VisibleMenuItem> getVisibleMenuItems() const = 0;
        
        /** Wait until the executor is ready to process events.
         *  @returns The number of milliseconds waited, or -1 if timed out.
         */
        virtual int waitUntilReady() = 0;
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
    /** Information about the selected menu item (from selectMenuItem interaction). */
    struct SelectedMenuItemInfo
    {
        String text;
        int itemId = 0;
        bool wasSelected = false;
    };
    
    //==============================================================================
    /** Execute normalized interactions synchronously. MUST be called on message thread.
     *
     *  Assumes moveTo events have been auto-inserted by normalizeSequence().
     *
     *  @param interactions  Parsed and normalized interactions to execute
     *  @param executor      Implementation that performs the actual operations
     *  @param executedLog   Output: log of executed events for response
     *  @param timeoutMs     Maximum execution time (default 20 seconds)
     *  @returns             ExecutionResult with success/failure and timing
     */
    ExecutionResult execute(const Array<InteractionParser::Interaction>& interactions,
                            Executor& executor,
                            Array<var>& executedLog,
                            int timeoutMs = 20000);
    
    /** Get info about the last selected menu item.
     *  Only valid after executing a selectMenuItem interaction.
     */
    SelectedMenuItemInfo getLastSelectedMenuItem() const { return lastSelectedMenuItem; }
    
    //==============================================================================
    static constexpr int MOVE_STEP_INTERVAL_MS = 10;
    static constexpr int DRAG_STEP_INTERVAL_MS = 10;
    
private:
    int64 startTimeMs = 0;
    int timeoutMs = 20000;
    String lastError;
    SelectedMenuItemInfo lastSelectedMenuItem;
    
    int getElapsedMs() const;
    void waitForDuration(int ms, Executor& executor);
    String checkAbortConditions() const;
    
    // Execute individual interaction types
    void executeMoveTo(const InteractionParser::MouseInteraction& mouse, Executor& exec, Array<var>& log);
    void executeClick(const InteractionParser::MouseInteraction& mouse, Executor& exec, Array<var>& log);
    void executeDrag(const InteractionParser::MouseInteraction& mouse, Executor& exec, Array<var>& log);
    void executeScreenshot(const InteractionParser::MouseInteraction& mouse, Executor& exec, Array<var>& log);
    void executeSelectMenuItem(const InteractionParser::MouseInteraction& mouse, Executor& exec, Array<var>& log);
    
    // Create log entry for response
    var createLogEntry(const String& type, Point<int> pixelPos, int elapsedMs) const;
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
        Point<int> pixelPos;
        ModifierKeys mods;
        bool rightClick = false;
        String screenshotId;
        float screenshotScale = 1.0f;
        int elapsedMs = 0;
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
    Point<int> cursorPosition{0, 0};
    Array<PopupMenu::VisibleMenuItem> mockMenuItems;
    
    //==========================================================================
    /** Reset all state for a new test. */
    void reset();
    
    /** Add a mock component for resolution testing. */
    void addMockComponent(const String& id, Rectangle<int> bounds, bool visible = true);
    
    /** Clear all mock components. */
    void clearMockComponents();
    
    /** Add a mock menu item for testing selectMenuItem. */
    void addMockMenuItem(const String& text, int itemId, Rectangle<int> bounds);
    
    /** Clear all mock menu items. */
    void clearMockMenuItems();
    
    //==========================================================================
    // Executor interface implementation
    
    ResolveResult resolveTarget(const String& componentId) override;
    
    void executeMouseDown(Point<int> pixelPos, ModifierKeys mods,
                          bool rightClick, int elapsedMs) override;
    
    void executeMouseUp(Point<int> pixelPos, ModifierKeys mods,
                        bool rightClick, int elapsedMs) override;
    
    void executeMouseMove(Point<int> pixelPos, ModifierKeys mods,
                          int elapsedMs) override;
    
    void executeScreenshot(const String& id, float scale, int elapsedMs) override;
    
    void executeSyntheticModeStart(int elapsedMs) override { ignoreUnused(elapsedMs); }
    void executeSyntheticModeEnd(int elapsedMs) override { ignoreUnused(elapsedMs); }
    
    Point<int> getCurrentCursorPosition() const override { return cursorPosition; }
    void setCursorPosition(Point<int> pos) override { cursorPosition = pos; }
    
    Array<PopupMenu::VisibleMenuItem> getVisibleMenuItems() const override { return mockMenuItems; }
    
    int waitUntilReady() override { return 0; }  // TestExecutor is always immediately ready
};

} // namespace hise
