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

class InteractionAnalyzerTests : public UnitTest
{
public:
    InteractionAnalyzerTests() : UnitTest("Interaction Analyzer Tests", "AI Tools") {}
    
    void runTest() override
    {
        // RawEvent parsing
        testRawEventFromVar();
        testRawEventFromVarWithModifiers();
        testParseRawEvents();
        
        // Click detection
        testDetectsClick();
        testDetectsClickWithTarget();
        testDetectsRightClick();
        testDetectsClickWithModifiers();
        
        // Double-click detection
        testDetectsDoubleClick();
        testTwoSlowClicksAreNotDoubleClick();
        testTwoClicksDifferentPositionAreNotDoubleClick();
        testTwoClicksDifferentComponentAreNotDoubleClick();
        
        // Drag detection
        testDetectsDrag();
        testDetectsDragWithCorrectDelta();
        testDetectsDragWithModifiers();
        testSmallMovementIsClick();
        
        // Subtarget handling
        testDetectsClickOnSubtarget();
        testDetectsDragOnSubtarget();
        testSubtargetFallbackPosition();
        
        // Position modes
        testAbsolutePositionMode();
        testNormalizedPositionMode();
        
        // Edge cases
        testEmptyEventsReturnsEmpty();
        testMouseUpWithoutDownWarns();
        testRecordingEndedWithMouseDown();
        testNoComponentAtPosition();
        
        // Multiple interactions
        testMultipleClicks();
        testClickThenDrag();
        testDragThenClick();
        
        // Real recorded data tests
        testRealRecordedTwoButtonClicks();
        testRealRecordedKnobDragsAndButtonClick();
    }
    
private:
    using RawEvent = InteractionAnalyzer::RawEvent;
    using Options = InteractionAnalyzer::Options;
    using PositionMode = InteractionAnalyzer::PositionMode;
    
    //==========================================================================
    // Helper methods
    //==========================================================================
    
    void setupDefaultMockComponents(TestExecutor& exec)
    {
        exec.addMockComponent("Button1", {100, 100, 80, 30}, true);
        exec.addMockComponent("Button2", {200, 100, 80, 30}, true);
        exec.addMockComponent("Knob1", {100, 150, 50, 50}, true);
        exec.addMockComponent("Panel1", {200, 150, 200, 200}, true);
    }
    
    RawEvent makeMouseDown(int x, int y, int64 timestamp, bool rightClick = false)
    {
        RawEvent e;
        e.type = InteractionIds::mouseDown;
        e.position.absolute = {x, y};
        e.timestamp = timestamp;
        e.rightClick = rightClick;
        return e;
    }
    
    RawEvent makeMouseUp(int x, int y, int64 timestamp, bool rightClick = false)
    {
        RawEvent e;
        e.type = InteractionIds::mouseUp;
        e.position.absolute = {x, y};
        e.timestamp = timestamp;
        e.rightClick = rightClick;
        return e;
    }
    
    RawEvent makeMouseMove(int x, int y, int64 timestamp)
    {
        RawEvent e;
        e.type = InteractionIds::mouseMove;
        e.position.absolute = {x, y};
        e.timestamp = timestamp;
        return e;
    }
    
    var createRawEventVar(const Identifier& type, int x, int y, int timestamp, 
                          bool rightClick = false)
    {
        DynamicObject::Ptr obj = new DynamicObject();
        obj->setProperty(RestApiIds::type, type.toString());
        obj->setProperty(RestApiIds::x, x);
        obj->setProperty(RestApiIds::y, y);
        obj->setProperty(InteractionIds::timestamp, timestamp);
        if (rightClick)
            obj->setProperty(InteractionIds::rightClick, true);
        return var(obj.get());
    }
    
    /** Helper that attaches target info and then analyzes.
     *  This is the standard pattern for unit tests - targets are batch-resolved.
     */
    InteractionAnalyzer::AnalyzeResult analyzeWithTargets(Array<RawEvent>& events,
                                                          TestExecutor& exec,
                                                          const Options& options = {})
    {
        InteractionAnalyzer::attachTargetInfo(events, exec);
        return InteractionAnalyzer::analyze(events, exec, options);
    }
    
    //==========================================================================
    // RawEvent Parsing Tests
    //==========================================================================
    
    void testRawEventFromVar()
    {
        beginTest("RawEvent: fromVar basic parsing");
        
        DynamicObject::Ptr obj = new DynamicObject();
        obj->setProperty(RestApiIds::type, InteractionIds::mouseDown.toString());
        obj->setProperty(RestApiIds::x, 150);
        obj->setProperty(RestApiIds::y, 200);
        obj->setProperty(InteractionIds::timestamp, 1000);
        obj->setProperty(InteractionIds::rightClick, true);
        
        auto event = RawEvent::fromVar(var(obj.get()));
        
        expect(event.type == InteractionIds::mouseDown, "Type should be mouseDown");
        expect(event.position.absolute.x == 150, "X should be 150");
        expect(event.position.absolute.y == 200, "Y should be 200");
        expect(event.timestamp == 1000, "Timestamp should be 1000");
        expect(event.rightClick, "Should be right click");
    }
    
    void testRawEventFromVarWithModifiers()
    {
        beginTest("RawEvent: fromVar with modifiers");
        
        DynamicObject::Ptr mods = new DynamicObject();
        mods->setProperty(InteractionIds::shiftDown, true);
        mods->setProperty(InteractionIds::ctrlDown, true);
        mods->setProperty(InteractionIds::altDown, false);
        
        DynamicObject::Ptr obj = new DynamicObject();
        obj->setProperty(RestApiIds::type, InteractionIds::mouseDown.toString());
        obj->setProperty(RestApiIds::x, 100);
        obj->setProperty(RestApiIds::y, 100);
        obj->setProperty(InteractionIds::timestamp, 0);
        obj->setProperty(InteractionIds::modifiers, var(mods.get()));
        
        auto event = RawEvent::fromVar(var(obj.get()));
        
        expect(event.modifiers.isShiftDown(), "Shift should be down");
        expect(event.modifiers.isCtrlDown(), "Ctrl should be down");
        expect(!event.modifiers.isAltDown(), "Alt should not be down");
    }
    
    void testParseRawEvents()
    {
        beginTest("RawEvent: parseRawEvents array");
        
        Array<var> rawVars;
        rawVars.add(createRawEventVar(InteractionIds::mouseDown, 100, 100, 0));
        rawVars.add(createRawEventVar(InteractionIds::mouseUp, 100, 100, 50));
        
        auto events = InteractionAnalyzer::parseRawEvents(rawVars);
        
        expect(events.size() == 2, "Should have 2 events");
        expect(events[0].type == InteractionIds::mouseDown, "First should be mouseDown");
        expect(events[1].type == InteractionIds::mouseUp, "Second should be mouseUp");
    }
    
    //==========================================================================
    // Click Detection Tests
    //==========================================================================
    
    void testDetectsClick()
    {
        beginTest("Click: Basic detection");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        events.add(makeMouseDown(140, 115, 0));   // Button1 center
        events.add(makeMouseUp(140, 115, 50));
        
        auto result = analyzeWithTargets(events, exec);
        
        expect(result.interactions.isArray(), "Should return array");
        auto* arr = result.interactions.getArray();
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        
        auto click = (*arr)[0];
        expect(click[RestApiIds::type].toString() == InteractionIds::click.toString(), "Should be click");
    }
    
    void testDetectsClickWithTarget()
    {
        beginTest("Click: Detects target component");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        events.add(makeMouseDown(140, 115, 0));   // Button1 center
        events.add(makeMouseUp(140, 115, 50));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        expect((*arr)[0][InteractionIds::target].toString() == "Button1", "Target should be Button1");
    }
    
    void testDetectsRightClick()
    {
        beginTest("Click: Right click detection");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        auto down = makeMouseDown(140, 115, 0);
        down.rightClick = true;
        events.add(down);
        
        auto up = makeMouseUp(140, 115, 50);
        up.rightClick = true;
        events.add(up);
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        expect(static_cast<bool>((*arr)[0][InteractionIds::rightClick]), "Should be right click");
    }
    
    void testDetectsClickWithModifiers()
    {
        beginTest("Click: Modifier detection");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        auto down = makeMouseDown(140, 115, 0);
        down.modifiers = ModifierKeys(ModifierKeys::shiftModifier | ModifierKeys::ctrlModifier);
        events.add(down);
        events.add(makeMouseUp(140, 115, 50));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        expect(static_cast<bool>((*arr)[0][InteractionIds::shiftDown]), "Should have shift");
        expect(static_cast<bool>((*arr)[0][InteractionIds::ctrlDown]), "Should have ctrl");
        expect(!(*arr)[0].hasProperty(InteractionIds::altDown), "Should not have alt property");
    }
    
    //==========================================================================
    // Double-click Detection Tests
    //==========================================================================
    
    void testDetectsDoubleClick()
    {
        beginTest("DoubleClick: Basic detection");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        // First click
        events.add(makeMouseDown(140, 115, 0));
        events.add(makeMouseUp(140, 115, 50));
        // Second click within 400ms
        events.add(makeMouseDown(140, 115, 200));
        events.add(makeMouseUp(140, 115, 250));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction (double-click replaces single)");
        expect((*arr)[0][RestApiIds::type].toString() == InteractionIds::doubleClick.toString(), "Should be doubleClick");
        expect((*arr)[0][InteractionIds::target].toString() == "Button1", "Target should be Button1");
    }
    
    void testTwoSlowClicksAreNotDoubleClick()
    {
        beginTest("DoubleClick: Slow clicks are two separate clicks");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        // First click
        events.add(makeMouseDown(140, 115, 0));
        events.add(makeMouseUp(140, 115, 50));
        // Second click after 500ms (> 400ms threshold)
        events.add(makeMouseDown(140, 115, 500));
        events.add(makeMouseUp(140, 115, 550));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 2, "Should have 2 interactions");
        expect((*arr)[0][RestApiIds::type].toString() == InteractionIds::click.toString(), "First should be click");
        expect((*arr)[1][RestApiIds::type].toString() == InteractionIds::click.toString(), "Second should be click");
    }
    
    void testTwoClicksDifferentPositionAreNotDoubleClick()
    {
        beginTest("DoubleClick: Different positions are two separate clicks");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        // First click on Button1
        events.add(makeMouseDown(140, 115, 0));
        events.add(makeMouseUp(140, 115, 50));
        // Second click at different position (still Button1 but different spot)
        events.add(makeMouseDown(110, 115, 200));  // > 5px away
        events.add(makeMouseUp(110, 115, 250));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 2, "Should have 2 interactions");
        expect((*arr)[0][RestApiIds::type].toString() == InteractionIds::click.toString(), "First should be click");
        expect((*arr)[1][RestApiIds::type].toString() == InteractionIds::click.toString(), "Second should be click");
    }
    
    void testTwoClicksDifferentComponentAreNotDoubleClick()
    {
        beginTest("DoubleClick: Different components are two separate clicks");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        // First click on Button1
        events.add(makeMouseDown(140, 115, 0));
        events.add(makeMouseUp(140, 115, 50));
        // Second click on Button2
        events.add(makeMouseDown(240, 115, 200));
        events.add(makeMouseUp(240, 115, 250));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 2, "Should have 2 interactions");
        expect((*arr)[0][InteractionIds::target].toString() == "Button1", "First target should be Button1");
        expect((*arr)[1][InteractionIds::target].toString() == "Button2", "Second target should be Button2");
    }
    
    //==========================================================================
    // Drag Detection Tests
    //==========================================================================
    
    void testDetectsDrag()
    {
        beginTest("Drag: Basic detection");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        events.add(makeMouseDown(125, 175, 0));   // Knob1 center
        events.add(makeMouseMove(125, 150, 100));
        events.add(makeMouseMove(125, 125, 200));
        events.add(makeMouseUp(125, 125, 300));   // 50px up
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        expect((*arr)[0][RestApiIds::type].toString() == InteractionIds::drag.toString(), "Should be drag");
        expect((*arr)[0][InteractionIds::target].toString() == "Knob1", "Target should be Knob1");
    }
    
    void testDetectsDragWithCorrectDelta()
    {
        beginTest("Drag: Correct delta calculation");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        events.add(makeMouseDown(125, 175, 0));
        events.add(makeMouseUp(125, 125, 300));   // 50px up
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        
        auto delta = (*arr)[0][InteractionIds::delta];
        expect(static_cast<int>(delta[RestApiIds::x]) == 0, "Delta X should be 0");
        expect(static_cast<int>(delta[RestApiIds::y]) == -50, "Delta Y should be -50");
    }
    
    void testDetectsDragWithModifiers()
    {
        beginTest("Drag: With modifiers");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        auto down = makeMouseDown(125, 175, 0);
        down.modifiers = ModifierKeys(ModifierKeys::shiftModifier);
        events.add(down);
        events.add(makeMouseUp(125, 125, 300));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        expect(static_cast<bool>((*arr)[0][InteractionIds::shiftDown]), "Should have shift modifier");
    }
    
    void testSmallMovementIsClick()
    {
        beginTest("Drag: Small movement (< 5px) is click not drag");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        events.add(makeMouseDown(140, 115, 0));
        events.add(makeMouseUp(142, 117, 50));    // 2px movement
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        expect((*arr)[0][RestApiIds::type].toString() == InteractionIds::click.toString(), "Should be click not drag");
    }
    
    //==========================================================================
    // Subtarget Tests
    //==========================================================================
    
    void testDetectsClickOnSubtarget()
    {
        beginTest("Subtarget: Click on subtarget");
        
        TestExecutor exec;
        exec.addMockComponent("SliderPack1", {100, 100, 200, 50}, true);
        exec.addMockSubtarget("SliderPack1", "3", {160, 100, 20, 50});  // Slot 3
        
        Array<RawEvent> events;
        events.add(makeMouseDown(170, 125, 0));   // Inside slot 3
        events.add(makeMouseUp(170, 125, 50));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        expect((*arr)[0][InteractionIds::target].toString() == "SliderPack1", "Target should be SliderPack1");
        expect((*arr)[0][InteractionIds::subtarget].toString() == "3", "Subtarget should be 3");
    }
    
    void testDetectsDragOnSubtarget()
    {
        beginTest("Subtarget: Drag on subtarget");
        
        TestExecutor exec;
        exec.addMockComponent("SliderPack1", {100, 100, 200, 50}, true);
        exec.addMockSubtarget("SliderPack1", "3", {160, 100, 20, 50});
        
        Array<RawEvent> events;
        events.add(makeMouseDown(170, 140, 0));
        events.add(makeMouseUp(170, 110, 300));   // 30px up
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        expect((*arr)[0][RestApiIds::type].toString() == InteractionIds::drag.toString(), "Should be drag");
        expect((*arr)[0][InteractionIds::target].toString() == "SliderPack1", "Target should be SliderPack1");
        expect((*arr)[0][InteractionIds::subtarget].toString() == "3", "Subtarget should be 3");
    }
    
    void testSubtargetFallbackPosition()
    {
        beginTest("Subtarget: Fallback position (normalizedInParent) stored for replay");
        
        TestExecutor exec;
        // TableEditor at (100, 100), size 200x100
        exec.addMockComponent("Table1", {100, 100, 200, 100}, true);
        // Subtarget point_0 at center of table
        exec.addMockSubtarget("Table1", "point_0", {190, 140, 20, 20});
        
        Array<RawEvent> events;
        // Click at center of point_0 (200, 150) - center of table
        events.add(makeMouseDown(200, 150, 0));
        events.add(makeMouseUp(200, 150, 50));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        expect((*arr)[0][InteractionIds::target].toString() == "Table1", "Target should be Table1");
        expect((*arr)[0][InteractionIds::subtarget].toString() == "point_0", "Subtarget should be point_0");
        
        // Verify normalizedPositionInTarget is present for fallback during replay
        // The position (200, 150) relative to Table1 (100, 100, 200, 100) = (100, 50)
        // Normalized: (100/200, 50/100) = (0.5, 0.5)
        expect((*arr)[0].hasProperty(InteractionIds::normalizedPositionInTarget), 
               "Should have normalizedPositionInTarget for fallback");
        
        auto normPos = (*arr)[0][InteractionIds::normalizedPositionInTarget];
        expectWithinAbsoluteError(static_cast<float>(normPos[RestApiIds::x]), 0.5f, 0.01f, 
                                  "Normalized X should be 0.5");
        expectWithinAbsoluteError(static_cast<float>(normPos[RestApiIds::y]), 0.5f, 0.01f, 
                                  "Normalized Y should be 0.5");
    }
    
    //==========================================================================
    // Position Mode Tests
    //==========================================================================
    
    void testAbsolutePositionMode()
    {
        beginTest("PositionMode: Absolute");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        events.add(makeMouseDown(120, 110, 0));   // Button1 at (100,100) size 80x30
        events.add(makeMouseUp(120, 110, 50));
        
        Options opts;
        opts.positionMode = PositionMode::Absolute;
        
        auto result = analyzeWithTargets(events, exec, opts);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        
        auto pixelPos = (*arr)[0][RestApiIds::pixelPosition];
        expect(static_cast<int>(pixelPos[RestApiIds::x]) == 20, "Pixel X should be 20 (120 - 100)");
        expect(static_cast<int>(pixelPos[RestApiIds::y]) == 10, "Pixel Y should be 10 (110 - 100)");
    }
    
    void testNormalizedPositionMode()
    {
        beginTest("PositionMode: Normalized");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        events.add(makeMouseDown(140, 115, 0));   // Button1 center (100,100,80,30)
        events.add(makeMouseUp(140, 115, 50));
        
        Options opts;
        opts.positionMode = PositionMode::Normalized;
        
        auto result = analyzeWithTargets(events, exec, opts);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        
        auto pos = (*arr)[0][InteractionIds::normalizedPosition];
        expectWithinAbsoluteError(static_cast<float>(pos[RestApiIds::x]), 0.5f, 0.01f, "Normalized X should be 0.5");
        expectWithinAbsoluteError(static_cast<float>(pos[RestApiIds::y]), 0.5f, 0.01f, "Normalized Y should be 0.5");
    }
    
    //==========================================================================
    // Edge Case Tests
    //==========================================================================
    
    void testEmptyEventsReturnsEmpty()
    {
        beginTest("EdgeCase: Empty events returns empty array");
        
        TestExecutor exec;
        Array<RawEvent> events;
        
        auto result = analyzeWithTargets(events, exec);
        
        expect(result.interactions.isArray(), "Should return array");
        expect(result.interactions.getArray()->isEmpty(), "Array should be empty");
        expect(!result.hasWarnings(), "Should have no warnings");
    }
    
    void testMouseUpWithoutDownWarns()
    {
        beginTest("EdgeCase: mouseUp without mouseDown warns");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        events.add(makeMouseUp(140, 115, 50));
        
        auto result = analyzeWithTargets(events, exec);
        
        expect(result.interactions.getArray()->isEmpty(), "Should have no interactions");
        expect(result.hasWarnings(), "Should have warning");
        expect(result.warnings[0].containsIgnoreCase("mouseUp without"), 
               "Warning should mention mouseUp without mouseDown");
    }
    
    void testRecordingEndedWithMouseDown()
    {
        beginTest("EdgeCase: Recording ended with mouse down");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        events.add(makeMouseDown(140, 115, 0));
        // No mouseUp
        
        auto result = analyzeWithTargets(events, exec);
        
        expect(result.interactions.getArray()->isEmpty(), "Should have no complete interactions");
        expect(result.hasWarnings(), "Should have warning");
        expect(result.warnings[0].containsIgnoreCase("mouse still down"), 
               "Warning should mention mouse still down");
    }
    
    void testNoComponentAtPosition()
    {
        beginTest("EdgeCase: No component at position");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        events.add(makeMouseDown(500, 500, 0));   // Outside all components
        events.add(makeMouseUp(500, 500, 50));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        // Should still create interaction, no warning (background click is valid)
        expect(arr != nullptr && arr->size() == 1, "Should have 1 interaction");
        expect(!result.hasWarnings(), "Should have no warnings");
        
        // Should have pixelPosition and target = "content" for background click
        expect((*arr)[0][InteractionIds::target].toString() == "content", "Target should be 'content' for background click");
        expect((*arr)[0].hasProperty(RestApiIds::pixelPosition), "Should have pixelPosition");
    }
    
    //==========================================================================
    // Multiple Interaction Tests
    //==========================================================================
    
    void testMultipleClicks()
    {
        beginTest("Multiple: Three separate clicks");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        // Click 1 on Button1
        events.add(makeMouseDown(140, 115, 0));
        events.add(makeMouseUp(140, 115, 50));
        // Click 2 on Button2 (after 500ms - not double click)
        events.add(makeMouseDown(240, 115, 600));
        events.add(makeMouseUp(240, 115, 650));
        // Click 3 on Knob1
        events.add(makeMouseDown(125, 175, 1200));
        events.add(makeMouseUp(125, 175, 1250));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 3, "Should have 3 interactions");
        expect((*arr)[0][InteractionIds::target].toString() == "Button1", "First target should be Button1");
        expect((*arr)[1][InteractionIds::target].toString() == "Button2", "Second target should be Button2");
        expect((*arr)[2][InteractionIds::target].toString() == "Knob1", "Third target should be Knob1");
    }
    
    void testClickThenDrag()
    {
        beginTest("Multiple: Click then drag");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        // Click on Button1
        events.add(makeMouseDown(140, 115, 0));
        events.add(makeMouseUp(140, 115, 50));
        // Drag on Knob1
        events.add(makeMouseDown(125, 175, 500));
        events.add(makeMouseUp(125, 125, 800));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 2, "Should have 2 interactions");
        expect((*arr)[0][RestApiIds::type].toString() == InteractionIds::click.toString(), "First should be click");
        expect((*arr)[1][RestApiIds::type].toString() == InteractionIds::drag.toString(), "Second should be drag");
    }
    
    void testDragThenClick()
    {
        beginTest("Multiple: Drag then click");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        
        Array<RawEvent> events;
        // Drag on Knob1
        events.add(makeMouseDown(125, 175, 0));
        events.add(makeMouseUp(125, 125, 300));
        // Click on Button1
        events.add(makeMouseDown(140, 115, 500));
        events.add(makeMouseUp(140, 115, 550));
        
        auto result = analyzeWithTargets(events, exec);
        auto* arr = result.interactions.getArray();
        
        expect(arr != nullptr && arr->size() == 2, "Should have 2 interactions");
        expect((*arr)[0][RestApiIds::type].toString() == InteractionIds::drag.toString(), "First should be drag");
        expect((*arr)[1][RestApiIds::type].toString() == InteractionIds::click.toString(), "Second should be click");
    }
    
    //==========================================================================
    // Real Recorded Data Tests
    //==========================================================================
    
    void testRealRecordedTwoButtonClicks()
    {
        beginTest("RealData: Two button clicks with mouse movement");
        
        TestExecutor exec;
        exec.addMockComponent("Button1", {209, 136, 128, 28}, true);
        exec.addMockComponent("Button2", {306, 331, 128, 28}, true);
        
        // Recorded raw events (mouse moves thinned for brevity)
        auto rawJson = JSON::parse(R"([
            {"type": "mouseMove", "x": 248, "y": 84, "timestamp": 620},
            {"type": "mouseMove", "x": 266, "y": 130, "timestamp": 854},
            {"type": "mouseDown", "x": 270, "y": 146, "timestamp": 1161, "rightClick": false},
            {"type": "mouseUp", "x": 270, "y": 146, "timestamp": 1296, "rightClick": false},
            {"type": "mouseMove", "x": 270, "y": 148, "timestamp": 1741},
            {"type": "mouseMove", "x": 358, "y": 330, "timestamp": 2247},
            {"type": "mouseDown", "x": 362, "y": 338, "timestamp": 2908, "rightClick": false},
            {"type": "mouseUp", "x": 362, "y": 338, "timestamp": 3054, "rightClick": false},
            {"type": "mouseDown", "x": 362, "y": 338, "timestamp": 4202, "rightClick": false},
            {"type": "mouseUp", "x": 362, "y": 338, "timestamp": 4332, "rightClick": false}
        ])");
        
        auto events = InteractionAnalyzer::parseRawEvents(*rawJson.getArray());
        auto result = analyzeWithTargets(events, exec);
        
        // Verify: Should produce 3 clicks (no double-clicks since timing > 400ms)
        auto* arr = result.interactions.getArray();
        expect(arr != nullptr && arr->size() == 3, "Should have 3 interactions");
        expect(!result.hasWarnings(), "Should have no warnings");
        
        // Click 1: Button1 at relative position (61, 10)
        expect((*arr)[0][RestApiIds::type].toString() == InteractionIds::click.toString(), "First should be click");
        expect((*arr)[0][InteractionIds::target].toString() == "Button1", "First target should be Button1");
        auto pos1 = (*arr)[0][RestApiIds::pixelPosition];
        expect(static_cast<int>(pos1[RestApiIds::x]) == 61, "Button1 click X should be 61");
        expect(static_cast<int>(pos1[RestApiIds::y]) == 10, "Button1 click Y should be 10");
        
        // Click 2: Button2 at relative position (56, 7)
        expect((*arr)[1][RestApiIds::type].toString() == InteractionIds::click.toString(), "Second should be click");
        expect((*arr)[1][InteractionIds::target].toString() == "Button2", "Second target should be Button2");
        auto pos2 = (*arr)[1][RestApiIds::pixelPosition];
        expect(static_cast<int>(pos2[RestApiIds::x]) == 56, "Button2 click X should be 56");
        expect(static_cast<int>(pos2[RestApiIds::y]) == 7, "Button2 click Y should be 7");
        
        // Click 3: Button2 again (same position, but > 400ms later so not double-click)
        expect((*arr)[2][RestApiIds::type].toString() == InteractionIds::click.toString(), "Third should be click (not doubleClick)");
        expect((*arr)[2][InteractionIds::target].toString() == "Button2", "Third target should be Button2");
    }
    
    void testRealRecordedKnobDragsAndButtonClick()
    {
        beginTest("RealData: Two knob drags and button click");
        
        TestExecutor exec;
        exec.addMockComponent("Knob1", {235, 186, 128, 48}, true);
        exec.addMockComponent("Knob2", {154, 290, 128, 48}, true);
        exec.addMockComponent("Button1", {139, 115, 128, 28}, true);
        
        // Recorded raw events (mouse moves thinned for brevity)
        // Original recording: drag Knob2 up, drag Knob1 down, click Button1
        auto rawJson = JSON::parse(R"([
            {"type": "mouseMove", "x": 166, "y": 303, "timestamp": 526},
            {"type": "mouseMove", "x": 176, "y": 315, "timestamp": 646},
            {"type": "mouseDown", "x": 176, "y": 315, "timestamp": 695, "rightClick": false},
            {"type": "mouseMove", "x": 177, "y": 300, "timestamp": 854},
            {"type": "mouseMove", "x": 177, "y": 270, "timestamp": 952},
            {"type": "mouseMove", "x": 178, "y": 246, "timestamp": 1096},
            {"type": "mouseMove", "x": 179, "y": 266, "timestamp": 1234},
            {"type": "mouseMove", "x": 179, "y": 279, "timestamp": 1290},
            {"type": "mouseUp", "x": 179, "y": 279, "timestamp": 1379, "rightClick": false},
            {"type": "mouseMove", "x": 265, "y": 206, "timestamp": 1896},
            {"type": "mouseDown", "x": 265, "y": 206, "timestamp": 2165, "rightClick": false},
            {"type": "mouseMove", "x": 264, "y": 237, "timestamp": 2236},
            {"type": "mouseMove", "x": 264, "y": 265, "timestamp": 2366},
            {"type": "mouseMove", "x": 264, "y": 277, "timestamp": 2478},
            {"type": "mouseMove", "x": 264, "y": 261, "timestamp": 2656},
            {"type": "mouseUp", "x": 264, "y": 261, "timestamp": 2656, "rightClick": false},
            {"type": "mouseMove", "x": 192, "y": 132, "timestamp": 3897},
            {"type": "mouseDown", "x": 192, "y": 132, "timestamp": 4004, "rightClick": false},
            {"type": "mouseUp", "x": 192, "y": 132, "timestamp": 4138, "rightClick": false}
        ])");
        
        auto events = InteractionAnalyzer::parseRawEvents(*rawJson.getArray());
        auto result = analyzeWithTargets(events, exec);
        
        // Verify: Should produce 2 drags and 1 click
        auto* arr = result.interactions.getArray();
        expect(arr != nullptr && arr->size() == 3, "Should have 3 interactions");
        expect(!result.hasWarnings(), "Should have no warnings");
        
        // Drag 1: Knob2 (at 154,290 size 128x48)
        // Start: (176, 315) relative to Knob2 = (176-154, 315-290) = (22, 25)
        // End: (179, 279) - delta = (3, -36)
        expect((*arr)[0][RestApiIds::type].toString() == InteractionIds::drag.toString(), "First should be drag");
        expect((*arr)[0][InteractionIds::target].toString() == "Knob2", "First target should be Knob2");
        auto pos1 = (*arr)[0][RestApiIds::pixelPosition];
        expect(static_cast<int>(pos1[RestApiIds::x]) == 22, "Knob2 drag start X should be 22");
        expect(static_cast<int>(pos1[RestApiIds::y]) == 25, "Knob2 drag start Y should be 25");
        auto delta1 = (*arr)[0][InteractionIds::delta];
        expect(static_cast<int>(delta1[RestApiIds::x]) == 3, "Knob2 drag delta X should be 3");
        expect(static_cast<int>(delta1[RestApiIds::y]) == -36, "Knob2 drag delta Y should be -36");
        
        // Drag 2: Knob1 (at 235,186 size 128x48)
        // Start: (265, 206) relative to Knob1 = (265-235, 206-186) = (30, 20)
        // End: (264, 261) - delta = (-1, 55)
        expect((*arr)[1][RestApiIds::type].toString() == InteractionIds::drag.toString(), "Second should be drag");
        expect((*arr)[1][InteractionIds::target].toString() == "Knob1", "Second target should be Knob1");
        auto pos2 = (*arr)[1][RestApiIds::pixelPosition];
        expect(static_cast<int>(pos2[RestApiIds::x]) == 30, "Knob1 drag start X should be 30");
        expect(static_cast<int>(pos2[RestApiIds::y]) == 20, "Knob1 drag start Y should be 20");
        auto delta2 = (*arr)[1][InteractionIds::delta];
        expect(static_cast<int>(delta2[RestApiIds::x]) == -1, "Knob1 drag delta X should be -1");
        expect(static_cast<int>(delta2[RestApiIds::y]) == 55, "Knob1 drag delta Y should be 55");
        
        // Click 3: Button1 (at 139,115 size 128x28)
        // Position: (192, 132) relative to Button1 = (192-139, 132-115) = (53, 17)
        expect((*arr)[2][RestApiIds::type].toString() == InteractionIds::click.toString(), "Third should be click");
        expect((*arr)[2][InteractionIds::target].toString() == "Button1", "Third target should be Button1");
        auto pos3 = (*arr)[2][RestApiIds::pixelPosition];
        expect(static_cast<int>(pos3[RestApiIds::x]) == 53, "Button1 click X should be 53");
        expect(static_cast<int>(pos3[RestApiIds::y]) == 17, "Button1 click Y should be 17");
    }
};

static InteractionAnalyzerTests interactionAnalyzerTests;

} // namespace hise
