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
        testExecuteMoveDirectionCorrect();
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
        
        // Component resolution tests
        testResolutionFindsComponentAtCenter();
        testResolutionFindsComponentAtCorners();
        testResolutionAllowsOutOfBoundsForDrag();
        testResolutionFailsForUnknownComponent();
        testResolutionFailsForHiddenComponent();
        testResolutionFailsForZeroSizeComponent();
        testDispatcherAbortsOnUnknownComponent();
        testDispatcherAbortsOnHiddenComponent();
        testDispatcherLogsCorrectPixelPosition();
        testDispatcherClickNotAtWindowCenter();
        
        // SelectMenuItem tests
        testSelectMenuItemFindsMatch();
        testSelectMenuItemFuzzyMatch();
        testSelectMenuItemNoMenuOpen();
        testSelectMenuItemNoMatch();
        testSelectMenuItemUpdatesLastSelected();
        testSelectMenuItemAnimatesMove();
    }
    
private:
    // Timing tolerance for verification (milliseconds)
    static constexpr int TIMING_TOLERANCE_MS = 20;
    
    //==========================================================================
    // Helper methods
    //==========================================================================
    
    using Interaction = InteractionParser::Interaction;
    using MouseInteraction = InteractionParser::MouseInteraction;
    
    /** Set up common mock components for tests. */
    void setupDefaultMockComponents(TestExecutor& exec)
    {
        // Standard test components with known positions
        exec.addMockComponent("Button1", {100, 50, 200, 100}, true);
        exec.addMockComponent("Button2", {100, 160, 200, 100}, true);
        exec.addMockComponent("Button3", {100, 270, 200, 100}, true);
        exec.addMockComponent("Slider1", {350, 50, 50, 300}, true);
        exec.addMockComponent("Panel1", {450, 50, 200, 200}, true);
    }
    
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0, {0.25f, 0.75f}));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        auto* down = findFirstEventOfType(exec, "mouseDown");
        expect(down != nullptr, "Should have mouseDown");
        // Button1 is at (100, 50) with size (200, 100)
        // Position (0.25, 0.75) should map to pixel (100 + 200*0.25, 50 + 100*0.75) = (150, 125)
        expect(down->pixelPos.x == 150, "X pixel should be 150, got " + String(down->pixelPos.x));
        expect(down->pixelPos.y == 125, "Y pixel should be 125, got " + String(down->pixelPos.y));
    }
    
    void testExecuteSingleClickRightClick()
    {
        beginTest("Execute right click");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
    
    void testExecuteMoveDirectionCorrect()
    {
        beginTest("Execute move direction: from -> to (not reversed)");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        // Move from top-left (0.0, 0.0) to bottom-right (1.0, 1.0)
        // Panel1 is 200x200 at position (450, 50) per setupDefaultMockComponents
        // So from = (450, 50) and to = (650, 250) in pixels
        Array<Interaction> interactions;
        interactions.add(makeMove("Panel1", 0, {0.0f, 0.0f}, {1.0f, 1.0f}, 100));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Get all mouseMove events
        Array<TestExecutor::LogEntry*> moves;
        for (auto& entry : exec.log)
        {
            if (entry.type == "mouseMove")
                moves.add(&entry);
        }
        
        expect(moves.size() >= 2, "Should have at least 2 moves");
        
        if (moves.size() >= 2)
        {
            auto* firstMove = moves.getFirst();
            auto* lastMove = moves.getLast();
            
            // First move should be near the 'from' position (450, 50)
            // Last move should be near the 'to' position (650, 250)
            expect(firstMove->pixelPos.x < lastMove->pixelPos.x,
                   "Move should progress left-to-right: first.x=" + String(firstMove->pixelPos.x) +
                   " should be < last.x=" + String(lastMove->pixelPos.x));
            
            expect(firstMove->pixelPos.y < lastMove->pixelPos.y,
                   "Move should progress top-to-bottom: first.y=" + String(firstMove->pixelPos.y) +
                   " should be < last.y=" + String(lastMove->pixelPos.y));
            
            // Verify approximate positions (Panel1 is at 450,50 with size 200x200)
            // from (0,0) -> pixel (450, 50), to (1,1) -> pixel (650, 250)
            expect(firstMove->pixelPos.x >= 450 && firstMove->pixelPos.x <= 470,
                   "First move X should be near 450, got " + String(firstMove->pixelPos.x));
            expect(lastMove->pixelPos.x >= 630 && lastMove->pixelPos.x <= 650,
                   "Last move X should be near 650, got " + String(lastMove->pixelPos.x));
        }
    }
    
    void testExecuteExit()
    {
        beginTest("Execute exit");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeExit("Panel1", 0));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Exit generates mouseMove to offscreen
        expect(countEventsOfType(exec, "mouseMove") == 1, "Should have 1 mouseMove");
        
        auto* move = findFirstEventOfType(exec, "mouseMove");
        expect(move != nullptr && move->pixelPos.x < 0, "Should move to offscreen position");
    }
    
    void testExecuteScreenshot()
    {
        beginTest("Execute screenshot");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        // Slider1 is at (350, 50) with size (50, 300)
        // Drag from (0.0, 0.0) to (1.0, 0.0) horizontally
        // Start pixel: (350 + 50*0.0, 50 + 300*0.0) = (350, 50)
        // End pixel: (350 + 50*1.0, 50 + 300*0.0) = (400, 50)
        
        Array<Interaction> interactions;
        interactions.add(makeDrag("Slider1", 0, {0.0f, 0.0f}, {1.0f, 0.0f}, 100));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Check that mouseDown is at start pixel position
        auto* down = findFirstEventOfType(exec, "mouseDown");
        expect(down != nullptr && down->pixelPos.x == 350, 
               "mouseDown should be at start pixel X=350, got " + String(down->pixelPos.x));
        
        // Find mouseUp (last event)
        const auto& up = exec.log[exec.log.size() - 1];
        expect(up.type == "mouseUp", "Last event should be mouseUp");
        expect(up.pixelPos.x == 400, 
               "mouseUp should be at end pixel X=400, got " + String(up.pixelPos.x));
        
        // Check that intermediate moves are between start and end pixels
        for (int i = 1; i < exec.log.size() - 1; i++)
        {
            expect(exec.log[i].pixelPos.x >= 350 && exec.log[i].pixelPos.x <= 400,
                   "Intermediate move X should be between 350 and 400");
        }
    }
    
    void testDragWithModifiers()
    {
        beginTest("Drag with modifiers");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
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
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 50));
        
        exec.addMockComponent("Button1", {100, 50, 200, 100}, true);
        
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
    
    //==========================================================================
    // Component Resolution Tests
    //==========================================================================
    
    void testResolutionFindsComponentAtCenter()
    {
        beginTest("Resolution: finds component at center");
        
        TestExecutor exec;
        exec.addMockComponent("Button1", {100, 50, 200, 100}, true);
        
        auto result = exec.resolveTarget("Button1", {0.5f, 0.5f});
        
        expect(result.success(), "Should succeed");
        expect(result.pixelPosition.x == 200, "X should be 100 + 200*0.5 = 200, got " + String(result.pixelPosition.x));
        expect(result.pixelPosition.y == 100, "Y should be 50 + 100*0.5 = 100, got " + String(result.pixelPosition.y));
        expect(result.componentBounds == Rectangle<int>(100, 50, 200, 100), "Bounds should match");
    }
    
    void testResolutionFindsComponentAtCorners()
    {
        beginTest("Resolution: finds component at corners");
        
        TestExecutor exec;
        exec.addMockComponent("Button1", {100, 50, 200, 100}, true);
        
        // Top-left
        auto tl = exec.resolveTarget("Button1", {0.0f, 0.0f});
        expect(tl.success() && tl.pixelPosition == Point<int>(100, 50), 
               "Top-left should be (100, 50)");
        
        // Bottom-right
        auto br = exec.resolveTarget("Button1", {1.0f, 1.0f});
        expect(br.success() && br.pixelPosition == Point<int>(300, 150), 
               "Bottom-right should be (300, 150)");
    }
    
    void testResolutionAllowsOutOfBoundsForDrag()
    {
        beginTest("Resolution: allows out-of-bounds position for drag support");
        
        TestExecutor exec;
        exec.addMockComponent("Slider1", {100, 50, 50, 200}, true);
        
        // Drag to 150% of height (below component)
        auto result = exec.resolveTarget("Slider1", {0.5f, 1.5f});
        
        expect(result.success(), "Should succeed for out-of-bounds");
        expect(result.pixelPosition.x == 125, "X should be 100 + 50*0.5 = 125");
        expect(result.pixelPosition.y == 350, "Y should be 50 + 200*1.5 = 350");
    }
    
    void testResolutionFailsForUnknownComponent()
    {
        beginTest("Resolution: fails for unknown component");
        
        TestExecutor exec;
        // No components registered
        
        auto result = exec.resolveTarget("NonExistent", {0.5f, 0.5f});
        
        expect(!result.success(), "Should fail");
        expect(result.error.containsIgnoreCase("not found"), 
               "Error should mention 'not found': " + result.error);
    }
    
    void testResolutionFailsForHiddenComponent()
    {
        beginTest("Resolution: fails for hidden component");
        
        TestExecutor exec;
        exec.addMockComponent("Button1", {100, 50, 200, 100}, false);  // Not visible
        
        auto result = exec.resolveTarget("Button1", {0.5f, 0.5f});
        
        expect(!result.success(), "Should fail for hidden component");
        expect(result.error.containsIgnoreCase("not visible"), 
               "Error should mention 'not visible': " + result.error);
    }
    
    void testResolutionFailsForZeroSizeComponent()
    {
        beginTest("Resolution: fails for zero-size component");
        
        TestExecutor exec;
        exec.addMockComponent("Button1", {100, 50, 0, 0}, true);  // Zero size
        
        auto result = exec.resolveTarget("Button1", {0.5f, 0.5f});
        
        expect(!result.success(), "Should fail for zero-size");
        expect(result.error.containsIgnoreCase("zero size"), 
               "Error should mention 'zero size': " + result.error);
    }
    
    void testDispatcherAbortsOnUnknownComponent()
    {
        beginTest("Dispatcher: aborts on unknown component");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        // No mock components - Button1 doesn't exist
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Unknown", 0));
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.failed(), "Should fail");
        expect(result.result.getErrorMessage().containsIgnoreCase("not found"), 
               "Error should mention 'not found'");
        expect(result.interactionsCompleted == 0, "Should complete 0 interactions");
    }
    
    void testDispatcherAbortsOnHiddenComponent()
    {
        beginTest("Dispatcher: aborts on hidden component");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        exec.addMockComponent("Button1", {100, 50, 200, 100}, false);  // Hidden
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.failed(), "Should fail for hidden component");
        expect(result.result.getErrorMessage().containsIgnoreCase("not visible"), 
               "Error should mention 'not visible'");
    }
    
    void testDispatcherLogsCorrectPixelPosition()
    {
        beginTest("Dispatcher: logs correct pixel position");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        // Button at (100, 50) with size (200, 100)
        // Click at center (0.5, 0.5) should go to pixel (200, 100)
        exec.addMockComponent("Button1", {100, 50, 200, 100}, true);
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Should succeed");
        expect(exec.log.size() >= 2, "Should have mouseDown and mouseUp");
        
        // Check that the logged pixel position is correct
        expect(exec.log[0].pixelPos == Point<int>(200, 100), 
               "mouseDown pixelPos should be (200, 100), got (" + 
               String(exec.log[0].pixelPos.x) + ", " + String(exec.log[0].pixelPos.y) + ")");
    }
    
    void testDispatcherClickNotAtWindowCenter()
    {
        beginTest("Dispatcher: click goes to component position, not window center");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        // This test verifies the bug fix: clicks should go to the component's
        // position, not to the center of the interface window.
        
        // Button is in top-left area, NOT at window center
        exec.addMockComponent("Button1", {50, 30, 100, 40}, true);
        
        Array<Interaction> interactions;
        interactions.add(makeClick("Button1", 0));  // Default position (0.5, 0.5) = center of Button1
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Should succeed");
        
        // Click should be at center of Button1: (50 + 100*0.5, 30 + 40*0.5) = (100, 50)
        // NOT at some arbitrary window center like (300, 200)
        expect(exec.log[0].pixelPos.x == 100, 
               "X should be 100 (center of button), not window center. Got " + 
               String(exec.log[0].pixelPos.x));
        expect(exec.log[0].pixelPos.y == 50, 
               "Y should be 50 (center of button), not window center. Got " + 
               String(exec.log[0].pixelPos.y));
    }
    
    //==========================================================================
    // SelectMenuItem tests
    //==========================================================================
    
    void testSelectMenuItemFindsMatch()
    {
        beginTest("SelectMenuItem: finds exact match");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        // Set up mock menu items
        exec.addMockMenuItem("Option A", 1, {100, 100, 150, 25});
        exec.addMockMenuItem("Option B", 2, {100, 125, 150, 25});
        exec.addMockMenuItem("Option C", 3, {100, 150, 150, 25});
        
        // Set cursor position (simulates where we are after clicking a combo box)
        exec.cursorPosition = {50, 50};
        
        Array<Interaction> interactions;
        interactions.add(makeSelectMenuItem("Option B", 0, 100));
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Should succeed");
        
        auto selected = dispatcher.getLastSelectedMenuItem();
        expect(selected.wasSelected, "Should have selected an item");
        expect(selected.text == "Option B", "Should match 'Option B', got: " + selected.text);
        expect(selected.itemId == 2, "Item ID should be 2");
    }
    
    void testSelectMenuItemFuzzyMatch()
    {
        beginTest("SelectMenuItem: fuzzy matching works");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        exec.addMockMenuItem("Save File", 1, {100, 100, 150, 25});
        exec.addMockMenuItem("Save As...", 2, {100, 125, 150, 25});
        exec.addMockMenuItem("Export", 3, {100, 150, 150, 25});
        
        exec.cursorPosition = {50, 50};
        
        Array<Interaction> interactions;
        // Typo in "Save As" - should still match via fuzzy search
        interactions.add(makeSelectMenuItem("Save As", 0, 100));
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Should succeed with fuzzy match");
        
        auto selected = dispatcher.getLastSelectedMenuItem();
        expect(selected.wasSelected, "Should have selected an item");
        expect(selected.text == "Save As...", "Should fuzzy match 'Save As...'");
    }
    
    void testSelectMenuItemNoMenuOpen()
    {
        beginTest("SelectMenuItem: fails when no menu open");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        // No mock menu items added - simulates no popup menu open
        
        Array<Interaction> interactions;
        interactions.add(makeSelectMenuItem("Option B", 0, 100));
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.failed(), "Should fail when no menu is open");
        expect(result.result.getErrorMessage().containsIgnoreCase("no popup menu"),
               "Error should mention no popup menu: " + result.result.getErrorMessage());
    }
    
    void testSelectMenuItemNoMatch()
    {
        beginTest("SelectMenuItem: fails with helpful error when no match");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        exec.addMockMenuItem("Cut", 1, {100, 100, 150, 25});
        exec.addMockMenuItem("Copy", 2, {100, 125, 150, 25});
        exec.addMockMenuItem("Paste", 3, {100, 150, 150, 25});
        
        Array<Interaction> interactions;
        interactions.add(makeSelectMenuItem("Delete", 0, 100));
        
        exec.startTiming();
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.failed(), "Should fail when no match found");
        expect(result.result.getErrorMessage().containsIgnoreCase("Delete"),
               "Error should mention the search term");
        expect(result.result.getErrorMessage().containsIgnoreCase("not found"),
               "Error should mention 'not found'");
    }
    
    void testSelectMenuItemUpdatesLastSelected()
    {
        beginTest("SelectMenuItem: updates lastSelectedMenuItem info");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        exec.addMockMenuItem("First Item", 10, {100, 100, 150, 25});
        exec.addMockMenuItem("Second Item", 20, {100, 125, 150, 25});
        
        exec.cursorPosition = {50, 50};
        
        Array<Interaction> interactions;
        interactions.add(makeSelectMenuItem("Second Item", 0, 100));
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        auto selected = dispatcher.getLastSelectedMenuItem();
        expectEquals(selected.text, String("Second Item"));
        expectEquals(selected.itemId, 20);
        expect(selected.wasSelected, "wasSelected should be true");
    }
    
    void testSelectMenuItemAnimatesMove()
    {
        beginTest("SelectMenuItem: generates mouse move events");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        InteractionDispatcher dispatcher;
        Array<var> log;
        
        exec.addMockMenuItem("Target Item", 1, {100, 100, 150, 25});
        exec.cursorPosition = {50, 50};
        
        Array<Interaction> interactions;
        interactions.add(makeSelectMenuItem("Target Item", 0, 200));  // 200ms duration
        
        exec.startTiming();
        dispatcher.execute(interactions, exec, log);
        
        // Should have multiple mouse moves followed by down/up
        int moveCount = 0;
        bool hasDown = false;
        bool hasUp = false;
        
        for (const auto& entry : exec.log)
        {
            if (entry.type == "mouseMove") moveCount++;
            if (entry.type == "mouseDown") hasDown = true;
            if (entry.type == "mouseUp") hasUp = true;
        }
        
        expect(moveCount > 1, "Should have multiple mouse move events, got " + String(moveCount));
        expect(hasDown, "Should have mouseDown");
        expect(hasUp, "Should have mouseUp");
    }
    
    //==========================================================================
    // Helper to create SelectMenuItem interaction
    //==========================================================================
    
    Interaction makeSelectMenuItem(const String& text, int timestamp, int duration = 500)
    {
        Interaction i;
        i.isMidi = false;
        i.mouse.type = MouseInteraction::Type::SelectMenuItem;
        i.mouse.menuItemText = text;
        i.mouse.timestampMs = timestamp;
        i.mouse.durationMs = duration;
        return i;
    }
};

static InteractionDispatcherTests interactionDispatcherTests;

} // namespace hise
