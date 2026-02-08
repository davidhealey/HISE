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

class InteractionDispatcherTests : public UnitTest
{
public:
    InteractionDispatcherTests() : UnitTest("Interaction Dispatcher Tests", "AI Tools") {}
    
    void runTest() override
    {
        // Basic execution - single interactions
        testExecuteSingleClick();
        testExecuteSingleClickWithPosition();
        testExecuteSingleClickRightClick();
        testExecuteSingleClickWithModifiers();
        testExecuteDoubleClick();
        testExecuteHover();
        testExecuteMove();
        testExecuteMoveWithPath();
        testExecuteExit();
        testExecuteScreenshot();
        testExecuteScreenshotWithScale();
        
        // Drag decomposition
        testDragGeneratesDownMoveUpSequence();
        testDragMoveCountMatchesDuration();
        testDragInterpolatesPositions();
        testDragWithModifiers();
        testDragRightClick();
        testDragShortDuration();
        
        // Timing verification
        testSingleEventFiresAtCorrectTime();
        testMultipleEventsFireAtCorrectTimes();
        testZeroTimestampFiresImmediately();
        testEventsWithGapsFireAtCorrectTimes();
        
        // Sequence and ordering
        testEventsExecuteInTimestampOrder();
        testMixedMouseAndScreenshotOrder();
        
        // Human intervention
        testHumanInterventionAbortsExecution();
        testHumanInterventionReportsTimestamp();
        testHumanInterventionMidSequence();
        testHumanInterventionDuringDrag();
        testNoInterventionCompletesSuccessfully();
        
        // Timeout handling
        testTimeoutAbortsExecution();
        testTimeoutReportsCorrectError();
        
        // Edge cases
        testEmptyInteractionListSucceeds();
        testSingleInteractionSucceeds();
        
        // Execution log verification
        testExecutionLogContainsAllEvents();
        testExecutionLogHasCorrectFormat();
    }
    
private:
    // Timing tolerance for verification (milliseconds)
    static constexpr int TIMING_TOLERANCE_MS = 20;
    
    //==========================================================================
    // Helper methods
    //==========================================================================
    
    using Interaction = InteractionParser::Interaction;
    using MouseInteraction = InteractionParser::MouseInteraction;
    
    /** Create a click interaction. */
    Interaction makeClick(const String& target, int timestampMs, 
                          Point<float> pos = {0.5f, 0.5f})
    {
        Interaction i;
        i.isMidi = false;
        i.mouse.type = MouseInteraction::Type::Click;
        i.mouse.targetComponentId = target;
        i.mouse.timestampMs = timestampMs;
        i.mouse.fromNormalized = pos;
        return i;
    }
    
    /** Create a click with modifiers. */
    Interaction makeClickWithModifiers(const String& target, int timestampMs,
                                        bool shift = false, bool ctrl = false, 
                                        bool alt = false, bool cmd = false,
                                        bool rightClick = false)
    {
        Interaction i = makeClick(target, timestampMs);
        i.mouse.shiftDown = shift;
        i.mouse.ctrlDown = ctrl;
        i.mouse.altDown = alt;
        i.mouse.cmdDown = cmd;
        i.mouse.rightClick = rightClick;
        return i;
    }
    
    /** Create a double click interaction. */
    Interaction makeDoubleClick(const String& target, int timestampMs,
                                 Point<float> pos = {0.5f, 0.5f})
    {
        Interaction i;
        i.isMidi = false;
        i.mouse.type = MouseInteraction::Type::DoubleClick;
        i.mouse.targetComponentId = target;
        i.mouse.timestampMs = timestampMs;
        i.mouse.fromNormalized = pos;
        return i;
    }
    
    /** Create a drag interaction. */
    Interaction makeDrag(const String& target, int timestampMs,
                         Point<float> from, Point<float> to, int durationMs)
    {
        Interaction i;
        i.isMidi = false;
        i.mouse.type = MouseInteraction::Type::Drag;
        i.mouse.targetComponentId = target;
        i.mouse.timestampMs = timestampMs;
        i.mouse.fromNormalized = from;
        i.mouse.toNormalized = to;
        i.mouse.durationMs = durationMs;
        return i;
    }
    
    /** Create a hover interaction. */
    Interaction makeHover(const String& target, int timestampMs,
                          Point<float> pos = {0.5f, 0.5f}, int durationMs = 100)
    {
        Interaction i;
        i.isMidi = false;
        i.mouse.type = MouseInteraction::Type::Hover;
        i.mouse.targetComponentId = target;
        i.mouse.timestampMs = timestampMs;
        i.mouse.fromNormalized = pos;
        i.mouse.durationMs = durationMs;
        return i;
    }
    
    /** Create a move interaction. */
    Interaction makeMove(const String& target, int timestampMs,
                         Point<float> from, Point<float> to, int durationMs)
    {
        Interaction i;
        i.isMidi = false;
        i.mouse.type = MouseInteraction::Type::Move;
        i.mouse.targetComponentId = target;
        i.mouse.timestampMs = timestampMs;
        i.mouse.fromNormalized = from;
        i.mouse.toNormalized = to;
        i.mouse.durationMs = durationMs;
        return i;
    }
    
    /** Create a move interaction with path. */
    Interaction makeMoveWithPath(const String& target, int timestampMs,
                                  const Array<Point<float>>& path, int durationMs)
    {
        Interaction i;
        i.isMidi = false;
        i.mouse.type = MouseInteraction::Type::Move;
        i.mouse.targetComponentId = target;
        i.mouse.timestampMs = timestampMs;
        i.mouse.pathNormalized = path;
        i.mouse.durationMs = durationMs;
        return i;
    }
    
    /** Create an exit interaction. */
    Interaction makeExit(const String& target, int timestampMs)
    {
        Interaction i;
        i.isMidi = false;
        i.mouse.type = MouseInteraction::Type::Exit;
        i.mouse.targetComponentId = target;
        i.mouse.timestampMs = timestampMs;
        return i;
    }
    
    /** Create a screenshot interaction. */
    Interaction makeScreenshot(const String& id, int timestampMs, float scale = 1.0f)
    {
        Interaction i;
        i.isMidi = false;
        i.mouse.type = MouseInteraction::Type::Screenshot;
        i.mouse.screenshotId = id;
        i.mouse.screenshotScale = scale;
        i.mouse.timestampMs = timestampMs;
        return i;
    }
    
    /** Check timing is within tolerance. */
    void expectTimingWithinTolerance(int expected, int actual, const String& context)
    {
        int diff = std::abs(actual - expected);
        expect(diff <= TIMING_TOLERANCE_MS, 
               context + ": expected ~" + String(expected) + "ms, got " + String(actual) + 
               "ms (diff: " + String(diff) + "ms, tolerance: " + String(TIMING_TOLERANCE_MS) + "ms)");
    }
    
    /** Count events of a specific type in the log. */
    int countEventsOfType(const TestExecutor& exec, const String& type)
    {
        int count = 0;
        for (const auto& entry : exec.log)
            if (entry.type == type)
                count++;
        return count;
    }
    
    /** Find first event of a specific type. */
    const TestExecutor::LogEntry* findFirstEventOfType(const TestExecutor& exec, const String& type)
    {
        for (const auto& entry : exec.log)
            if (entry.type == type)
                return &entry;
        return nullptr;
    }
    
    //==========================================================================
    // Basic execution - single interactions
    //==========================================================================
    
    void testExecuteSingleClick()
    {
        beginTest("Execute single click");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Should succeed");
        expect(result.interactionsCompleted == 1, "Should complete 1 interaction");
        
        // Click generates mouseDown + mouseUp
        expect(countEventsOfType(exec, "mouseDown") == 1, "Should have 1 mouseDown");
        expect(countEventsOfType(exec, "mouseUp") == 1, "Should have 1 mouseUp");
        
        auto* down = findFirstEventOfType(exec, "mouseDown");
        expect(down != nullptr && down->target == "Button1", "Target should be Button1");
    }
    
    void testExecuteSingleClickWithPosition()
    {
        beginTest("Execute click with custom position");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0, {0.25f, 0.75f}));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        auto* down = findFirstEventOfType(exec, "mouseDown");
        expect(down != nullptr, "Should have mouseDown");
        expect(std::abs(down->position.x - 0.25f) < 0.01f, "X position should be 0.25");
        expect(std::abs(down->position.y - 0.75f) < 0.01f, "Y position should be 0.75");
    }
    
    void testExecuteSingleClickRightClick()
    {
        beginTest("Execute right click");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClickWithModifiers("Button1", 0, false, false, false, false, true));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        auto* down = findFirstEventOfType(exec, "mouseDown");
        expect(down != nullptr && down->rightClick, "Should be right click");
    }
    
    void testExecuteSingleClickWithModifiers()
    {
        beginTest("Execute click with modifiers");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClickWithModifiers("Button1", 0, true, true, false, false, false));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        auto* down = findFirstEventOfType(exec, "mouseDown");
        expect(down != nullptr, "Should have mouseDown");
        expect(down->mods.isShiftDown(), "Shift should be down");
        expect(down->mods.isCtrlDown(), "Ctrl should be down");
        expect(!down->mods.isAltDown(), "Alt should not be down");
    }
    
    void testExecuteDoubleClick()
    {
        beginTest("Execute double click");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeDoubleClick("Button1", 0));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Double click generates 2x (mouseDown + mouseUp)
        expect(countEventsOfType(exec, "mouseDown") == 2, "Should have 2 mouseDowns");
        expect(countEventsOfType(exec, "mouseUp") == 2, "Should have 2 mouseUps");
    }
    
    void testExecuteHover()
    {
        beginTest("Execute hover");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeHover("Panel1", 0, {0.5f, 0.5f}, 50));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Hover generates mouseMove (no button down)
        expect(countEventsOfType(exec, "mouseMove") >= 1, "Should have mouseMove");
        expect(countEventsOfType(exec, "mouseDown") == 0, "Should have no mouseDown");
    }
    
    void testExecuteMove()
    {
        beginTest("Execute move");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeMove("Panel1", 0, {0.0f, 0.0f}, {1.0f, 1.0f}, 100));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Move generates multiple mouseMoves
        int moveCount = countEventsOfType(exec, "mouseMove");
        expect(moveCount >= 2, "Should have multiple mouseMoves, got " + String(moveCount));
        expect(countEventsOfType(exec, "mouseDown") == 0, "Should have no mouseDown");
    }
    
    void testExecuteMoveWithPath()
    {
        beginTest("Execute move with path");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Point<float>> path;
        path.add({0.0f, 0.0f});
        path.add({0.5f, 0.5f});
        path.add({1.0f, 1.0f});
        
        Array<Interaction> interactions;
        interactions.add(makeMoveWithPath("Panel1", 0, path, 100));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Should have exactly 3 moves (one per path point)
        expect(countEventsOfType(exec, "mouseMove") == 3, "Should have 3 mouseMoves for 3 path points");
    }
    
    void testExecuteExit()
    {
        beginTest("Execute exit");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeExit("Panel1", 0));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Exit generates mouseMove to offscreen
        expect(countEventsOfType(exec, "mouseMove") == 1, "Should have 1 mouseMove");
        
        auto* move = findFirstEventOfType(exec, "mouseMove");
        expect(move != nullptr && move->position.x < 0, "Should move to offscreen position");
    }
    
    void testExecuteScreenshot()
    {
        beginTest("Execute screenshot");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeScreenshot("test_shot", 0));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        expect(countEventsOfType(exec, "screenshot") == 1, "Should have 1 screenshot");
        
        auto* shot = findFirstEventOfType(exec, "screenshot");
        expect(shot != nullptr && shot->screenshotId == "test_shot", "Screenshot ID should match");
        expect(shot != nullptr && shot->screenshotScale == 1.0f, "Default scale should be 1.0");
    }
    
    void testExecuteScreenshotWithScale()
    {
        beginTest("Execute screenshot with scale");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeScreenshot("half_size", 0, 0.5f));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        auto* shot = findFirstEventOfType(exec, "screenshot");
        expect(shot != nullptr && shot->screenshotScale == 0.5f, "Scale should be 0.5");
    }
    
    //==========================================================================
    // Drag decomposition
    //==========================================================================
    
    void testDragGeneratesDownMoveUpSequence()
    {
        beginTest("Drag generates down/move/up sequence");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeDrag("Slider1", 0, {0.5f, 0.8f}, {0.5f, 0.2f}, 100));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Should have: 1 mouseDown + N mouseMoves + 1 mouseUp
        expect(countEventsOfType(exec, "mouseDown") == 1, "Should have 1 mouseDown");
        expect(countEventsOfType(exec, "mouseUp") == 1, "Should have 1 mouseUp");
        expect(countEventsOfType(exec, "mouseMove") >= 1, "Should have mouseMoves");
        
        // Verify order: first event is mouseDown
        expect(exec.log[0].type == "mouseDown", "First event should be mouseDown");
        
        // Last event is mouseUp
        expect(exec.log[exec.log.size() - 1].type == "mouseUp", "Last event should be mouseUp");
        
        // Middle events are mouseMove
        for (int i = 1; i < exec.log.size() - 1; i++)
        {
            expect(exec.log[i].type == "mouseMove", 
                   "Event " + String(i) + " should be mouseMove");
        }
    }
    
    void testDragMoveCountMatchesDuration()
    {
        beginTest("Drag move count matches duration");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        // 100ms drag with 10ms step = ~10 steps
        Array<Interaction> interactions;
        interactions.add(makeDrag("Slider1", 0, {0.0f, 0.0f}, {1.0f, 1.0f}, 100));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        int moveCount = countEventsOfType(exec, "mouseMove");
        
        // 100ms / 10ms = 10 steps, minus 1 for the up, so 9 moves expected
        // Allow some tolerance
        expect(moveCount >= 5 && moveCount <= 15, 
               "Expected ~9 moves for 100ms drag, got " + String(moveCount));
    }
    
    void testDragInterpolatesPositions()
    {
        beginTest("Drag interpolates positions");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeDrag("Slider1", 0, {0.0f, 0.0f}, {1.0f, 0.0f}, 100));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Check that mouseDown is at start and mouseUp is at end
        auto* down = findFirstEventOfType(exec, "mouseDown");
        expect(down != nullptr && std::abs(down->position.x - 0.0f) < 0.01f, 
               "mouseDown should be at start (0.0)");
        
        // Find mouseUp (last event)
        auto& up = exec.log[exec.log.size() - 1];
        expect(up.type == "mouseUp", "Last event should be mouseUp");
        expect(std::abs(up.position.x - 1.0f) < 0.01f, 
               "mouseUp should be at end (1.0), got " + String(up.position.x));
        
        // Check that intermediate moves are between 0 and 1
        for (int i = 1; i < exec.log.size() - 1; i++)
        {
            expect(exec.log[i].position.x >= 0.0f && exec.log[i].position.x <= 1.0f,
                   "Intermediate move should be between 0 and 1");
        }
    }
    
    void testDragWithModifiers()
    {
        beginTest("Drag with modifiers");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Interaction drag = makeDrag("Slider1", 0, {0.0f, 0.0f}, {1.0f, 0.0f}, 50);
        drag.mouse.shiftDown = true;
        
        Array<Interaction> interactions;
        interactions.add(drag);
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        auto* down = findFirstEventOfType(exec, "mouseDown");
        expect(down != nullptr && down->mods.isShiftDown(), "Shift should be down during drag");
    }
    
    void testDragRightClick()
    {
        beginTest("Drag with right click");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Interaction drag = makeDrag("Slider1", 0, {0.0f, 0.0f}, {1.0f, 0.0f}, 50);
        drag.mouse.rightClick = true;
        
        Array<Interaction> interactions;
        interactions.add(drag);
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        auto* down = findFirstEventOfType(exec, "mouseDown");
        expect(down != nullptr && down->rightClick, "Should be right-click drag");
    }
    
    void testDragShortDuration()
    {
        beginTest("Drag with very short duration");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        // 5ms drag - shorter than step interval
        Array<Interaction> interactions;
        interactions.add(makeDrag("Slider1", 0, {0.0f, 0.0f}, {1.0f, 0.0f}, 5));
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Should succeed even with short duration");
        
        // Should still have at least down and up
        expect(countEventsOfType(exec, "mouseDown") == 1, "Should have mouseDown");
        expect(countEventsOfType(exec, "mouseUp") == 1, "Should have mouseUp");
    }
    
    //==========================================================================
    // Timing verification
    //==========================================================================
    
    void testSingleEventFiresAtCorrectTime()
    {
        beginTest("Single event fires at correct time");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 100));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        auto* down = findFirstEventOfType(exec, "mouseDown");
        expect(down != nullptr, "Should have mouseDown");
        expectTimingWithinTolerance(100, down->actualTimestampMs, "Click timing");
    }
    
    void testMultipleEventsFireAtCorrectTimes()
    {
        beginTest("Multiple events fire at correct times");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 50));
        interactions.add(makeClick("Button2", 150));
        interactions.add(makeClick("Button3", 250));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Find the three mouseDown events
        int clickIndex = 0;
        int expectedTimes[] = {50, 150, 250};
        
        for (const auto& entry : exec.log)
        {
            if (entry.type == "mouseDown" && clickIndex < 3)
            {
                expectTimingWithinTolerance(expectedTimes[clickIndex], entry.actualTimestampMs,
                                            "Click " + String(clickIndex + 1));
                clickIndex++;
            }
        }
        
        expect(clickIndex == 3, "Should have found 3 clicks");
    }
    
    void testZeroTimestampFiresImmediately()
    {
        beginTest("Zero timestamp fires immediately");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        auto* down = findFirstEventOfType(exec, "mouseDown");
        expect(down != nullptr, "Should have mouseDown");
        expectTimingWithinTolerance(0, down->actualTimestampMs, "Zero timestamp click");
    }
    
    void testEventsWithGapsFireAtCorrectTimes()
    {
        beginTest("Events with gaps fire at correct times");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        interactions.add(makeClick("Button2", 200));  // 200ms gap
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        int clickIndex = 0;
        int expectedTimes[] = {0, 200};
        
        for (const auto& entry : exec.log)
        {
            if (entry.type == "mouseDown" && clickIndex < 2)
            {
                expectTimingWithinTolerance(expectedTimes[clickIndex], entry.actualTimestampMs,
                                            "Click " + String(clickIndex + 1));
                clickIndex++;
            }
        }
    }
    
    //==========================================================================
    // Sequence and ordering
    //==========================================================================
    
    void testEventsExecuteInTimestampOrder()
    {
        beginTest("Events execute in timestamp order");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 100));
        interactions.add(makeScreenshot("shot1", 200));
        interactions.add(makeClick("Button2", 300));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Verify events are in increasing timestamp order
        int lastTime = -1;
        for (const auto& entry : exec.log)
        {
            expect(entry.actualTimestampMs >= lastTime,
                   "Events should be in order: got " + String(entry.actualTimestampMs) + 
                   " after " + String(lastTime));
            lastTime = entry.actualTimestampMs;
        }
    }
    
    void testMixedMouseAndScreenshotOrder()
    {
        beginTest("Mixed mouse and screenshot maintain order");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        interactions.add(makeScreenshot("before_drag", 100));
        interactions.add(makeDrag("Slider1", 150, {0.0f, 0.5f}, {1.0f, 0.5f}, 100));
        interactions.add(makeScreenshot("after_drag", 300));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Find the screenshot events and verify they're in correct positions
        bool foundFirstShot = false;
        bool foundDragStart = false;
        bool foundSecondShot = false;
        
        for (const auto& entry : exec.log)
        {
            if (entry.type == "screenshot" && entry.screenshotId == "before_drag")
            {
                expect(!foundDragStart, "before_drag screenshot should come before drag");
                foundFirstShot = true;
            }
            else if (entry.type == "mouseDown" && entry.target == "Slider1")
            {
                expect(foundFirstShot, "Drag should come after before_drag screenshot");
                foundDragStart = true;
            }
            else if (entry.type == "screenshot" && entry.screenshotId == "after_drag")
            {
                expect(foundDragStart, "after_drag screenshot should come after drag start");
                foundSecondShot = true;
            }
        }
        
        expect(foundFirstShot && foundDragStart && foundSecondShot, 
               "Should find all expected events");
    }
    
    //==========================================================================
    // Human intervention
    //==========================================================================
    
    void testHumanInterventionAbortsExecution()
    {
        beginTest("Human intervention aborts execution");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        interactions.add(makeClick("Button2", 100));
        interactions.add(makeClick("Button3", 200));
        interactions.add(makeClick("Button4", 300));
        
        // Simulate intervention at 150ms (between Button2 and Button3)
        exec.simulateHumanIntervention(150);
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.failed(), "Should fail due to intervention");
        expect(result.result.getErrorMessage().containsIgnoreCase("intervention"),
               "Error should mention intervention");
    }
    
    void testHumanInterventionReportsTimestamp()
    {
        beginTest("Human intervention reports timestamp");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        interactions.add(makeClick("Button2", 200));
        
        exec.simulateHumanIntervention(100);
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.failed(), "Should fail");
        // The error message should contain the intervention timestamp
        expect(result.result.getErrorMessage().contains("100") ||
               result.result.getErrorMessage().contains(String(exec.getInterventionTimestamp())),
               "Error should mention intervention timestamp");
    }
    
    void testHumanInterventionMidSequence()
    {
        beginTest("Human intervention mid-sequence");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        interactions.add(makeClick("Button2", 50));
        interactions.add(makeClick("Button3", 150));  // After intervention
        interactions.add(makeClick("Button4", 200));  // After intervention
        
        exec.simulateHumanIntervention(100);
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.failed(), "Should fail");
        
        // Should have completed first 2 clicks (Button1 at 0, Button2 at 50)
        // Intervention happens at 100, before Button3 at 150
        expect(result.interactionsCompleted == 2, 
               "Should have completed 2 interactions, got " + String(result.interactionsCompleted));
    }
    
    void testHumanInterventionDuringDrag()
    {
        beginTest("Human intervention during drag");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeDrag("Slider1", 0, {0.0f, 0.5f}, {1.0f, 0.5f}, 200));
        interactions.add(makeClick("Button1", 250));
        
        // Intervention at 100ms - during the drag
        exec.simulateHumanIntervention(100);
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.failed(), "Should fail due to intervention during drag");
    }
    
    void testNoInterventionCompletesSuccessfully()
    {
        beginTest("No intervention completes successfully");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        interactions.add(makeClick("Button2", 50));
        interactions.add(makeClick("Button3", 100));
        
        // No intervention simulated
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Should succeed without intervention");
        expect(result.interactionsCompleted == 3, "Should complete all 3 interactions");
    }
    
    //==========================================================================
    // Timeout handling
    //==========================================================================
    
    void testTimeoutAbortsExecution()
    {
        beginTest("Timeout aborts execution");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        interactions.add(makeClick("Button2", 500));  // Way past timeout
        
        exec.startTiming();
        // Very short timeout of 100ms
        auto result = dispatcher.execute(interactions, exec, log, 100);
        
        expect(result.result.failed(), "Should fail due to timeout");
        expect(result.result.getErrorMessage().containsIgnoreCase("timeout"),
               "Error should mention timeout");
    }
    
    void testTimeoutReportsCorrectError()
    {
        beginTest("Timeout reports correct error");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 200));
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log, 50);  // 50ms timeout
        
        expect(result.result.failed(), "Should fail");
        expect(result.result.getErrorMessage().contains("50"),
               "Error should mention timeout value");
    }
    
    //==========================================================================
    // Edge cases
    //==========================================================================
    
    void testEmptyInteractionListSucceeds()
    {
        beginTest("Empty interaction list succeeds");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        // Empty list
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Should succeed with empty list");
        expect(result.interactionsCompleted == 0, "Should complete 0 interactions");
        expect(exec.log.isEmpty(), "Log should be empty");
    }
    
    void testSingleInteractionSucceeds()
    {
        beginTest("Single interaction succeeds");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Should succeed");
        expect(result.interactionsCompleted == 1, "Should complete 1 interaction");
    }
    
    //==========================================================================
    // Execution log verification
    //==========================================================================
    
    void testExecutionLogContainsAllEvents()
    {
        beginTest("Execution log contains all events");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        interactions.add(makeScreenshot("shot1", 50));
        interactions.add(makeClick("Button2", 100));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // log (the var array) should have entries for all events
        expect(log.size() >= 5, "Should have log entries (2 clicks = 4 events + 1 screenshot)");
    }
    
    void testExecutionLogHasCorrectFormat()
    {
        beginTest("Execution log has correct format");
        
        TestExecutor exec;
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 50));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        expect(log.size() >= 2, "Should have at least 2 log entries");
        
        // Check first entry (mouseDown)
        auto* entry = log[0].getDynamicObject();
        expect(entry != nullptr, "Log entry should be object");
        expect(entry->hasProperty("type"), "Should have type");
        expect(entry->hasProperty("target"), "Should have target");
        expect(entry->hasProperty("timestamp"), "Should have timestamp");
        expect(entry->hasProperty("actualTimestamp"), "Should have actualTimestamp");
        
        expect(entry->getProperty("type").toString() == "mouseDown", "Type should be mouseDown");
        expect(entry->getProperty("target").toString() == "Button1", "Target should be Button1");
    }
};

static InteractionDispatcherTests interactionDispatcherTests;

} // namespace hise
