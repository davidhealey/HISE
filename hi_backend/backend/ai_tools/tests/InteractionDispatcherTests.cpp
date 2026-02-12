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
        // MoveTo execution
        testExecuteMoveTo();
        testExecuteMoveToInterpolates();
        testExecuteMoveToWithPixelPosition();
        
        // Click execution
        testExecuteClick();
        testExecuteClickRightClick();
        testExecuteClickWithModifiers();
        
        // Drag execution
        testExecuteDrag();
        testExecuteDragWithModifiers();
        testExecuteDragUpdatesPosition();
        
        // Screenshot
        testExecuteScreenshot();
        
        // SelectMenuItem
        testExecuteSelectMenuItem();
        testExecuteSelectMenuItemNoMenu();
        
        // Timing
        testDelayWorks();
        
        // Component resolution
        testResolutionSuccess();
        testResolutionFailsForUnknownComponent();
        testResolutionFailsForHiddenComponent();
        
        // Execution log
        testExecutionLogContainsAllEvents();
    }
    
private:
    
    using Interaction = InteractionParser::Interaction;
    using MouseInteraction = InteractionParser::MouseInteraction;
    using Position = InteractionParser::MouseInteraction::Position;
    using ComponentTargetPath = InteractionParser::ComponentTargetPath;
    
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
    
    Interaction makeMoveTo(const String& target, int delayMs = 0, int durationMs = 100)
    {
        Interaction i;
        i.mouse.type = MouseInteraction::Type::MoveTo;
        i.mouse.target = ComponentTargetPath(target);
        i.mouse.position = Position::center();
        i.mouse.delayMs = delayMs;
        i.mouse.durationMs = durationMs;
        return i;
    }
    
    Interaction makeMoveToPixel(const String& target, int x, int y, int durationMs = 100)
    {
        Interaction i;
        i.mouse.type = MouseInteraction::Type::MoveTo;
        i.mouse.target = ComponentTargetPath(target);
        i.mouse.position = Position::absolute(x, y);
        i.mouse.durationMs = durationMs;
        return i;
    }
    
    Interaction makeClick(int delayMs = 0)
    {
        Interaction i;
        i.mouse.type = MouseInteraction::Type::Click;
        i.mouse.delayMs = delayMs;
        i.mouse.durationMs = InteractionConstants::DefaultClickDurationMs;
        return i;
    }
    
    Interaction makeClickOnTarget(const String& target)
    {
        Interaction i;
        i.mouse.type = MouseInteraction::Type::Click;
        i.mouse.target = ComponentTargetPath(target);
        i.mouse.position = Position::center();
        i.mouse.durationMs = InteractionConstants::DefaultClickDurationMs;
        return i;
    }
    
    Interaction makeDrag(const String& target, int dx, int dy, int durationMs = 100)
    {
        Interaction i;
        i.mouse.type = MouseInteraction::Type::Drag;
        i.mouse.target = ComponentTargetPath(target);
        i.mouse.position = Position::center();
        i.mouse.deltaPixels = {dx, dy};
        i.mouse.durationMs = durationMs;
        return i;
    }
    
    Interaction makeScreenshot(const String& id)
    {
        Interaction i;
        i.mouse.type = MouseInteraction::Type::Screenshot;
        i.mouse.screenshotId = id;
        return i;
    }
    
    Interaction makeSelectMenuItem(const String& text, int durationMs = 100)
    {
        Interaction i;
        i.mouse.type = MouseInteraction::Type::SelectMenuItem;
        i.mouse.menuItemText = text;
        i.mouse.durationMs = durationMs;
        return i;
    }
    
    //==========================================================================
    // MoveTo Tests
    //==========================================================================
    
    void testExecuteMoveTo()
    {
        beginTest("MoveTo: Basic execution");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        exec.cursorPosition = {0, 0};
        
        Array<Interaction> interactions;
        interactions.add(makeMoveTo("Button1"));
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Execution should succeed");
        
        // Button1 is at (100, 100) with size 80x30
        // Center is (140, 115)
        expect(exec.cursorPosition.x == 140, "Cursor X should be at Button1 center: " + String(exec.cursorPosition.x));
        expect(exec.cursorPosition.y == 115, "Cursor Y should be at Button1 center: " + String(exec.cursorPosition.y));
    }
    
    void testExecuteMoveToInterpolates()
    {
        beginTest("MoveTo: Interpolates movement");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        exec.cursorPosition = {0, 0};
        
        Array<Interaction> interactions;
        interactions.add(makeMoveTo("Button1", 0, 100));  // 100ms duration
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        dispatcher.execute(interactions, exec, log);
        
        // Should have multiple mouseMove events
        int moveCount = 0;
        for (const auto& entry : exec.log)
        {
            if (entry.type == InteractionIds::mouseMove)
                moveCount++;
        }
        
        expect(moveCount > 1, "Should have multiple moves for interpolation, got: " + String(moveCount));
    }
    
    void testExecuteMoveToWithPixelPosition()
    {
        beginTest("MoveTo: With pixel position");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        exec.cursorPosition = {0, 0};
        
        Array<Interaction> interactions;
        interactions.add(makeMoveToPixel("Panel1", 50, 75));  // Panel at (200, 150)
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        dispatcher.execute(interactions, exec, log);
        
        // Panel1 at (200, 150) + pixel (50, 75) = (250, 225)
        expect(exec.cursorPosition.x == 250, "Cursor X should be 200 + 50 = 250: " + String(exec.cursorPosition.x));
        expect(exec.cursorPosition.y == 225, "Cursor Y should be 150 + 75 = 225: " + String(exec.cursorPosition.y));
    }
    
    //==========================================================================
    // Click Tests
    //==========================================================================
    
    void testExecuteClick()
    {
        beginTest("Click: Basic execution");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        exec.cursorPosition = {140, 115};  // At Button1 center
        
        Array<Interaction> interactions;
        interactions.add(makeClick());
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Execution should succeed");
        
        // Should have mouseDown and mouseUp at cursor position
        bool hasDown = false, hasUp = false;
        for (const auto& entry : exec.log)
        {
            if (entry.type == InteractionIds::mouseDown)
            {
                hasDown = true;
                expect(entry.pixelPos.x == 140, "MouseDown X should be 140");
                expect(entry.pixelPos.y == 115, "MouseDown Y should be 115");
            }
            if (entry.type == InteractionIds::mouseUp)
            {
                hasUp = true;
                expect(entry.pixelPos.x == 140, "MouseUp X should be 140");
                expect(entry.pixelPos.y == 115, "MouseUp Y should be 115");
            }
        }
        
        expect(hasDown, "Should have mouseDown");
        expect(hasUp, "Should have mouseUp");
    }
    
    void testExecuteClickRightClick()
    {
        beginTest("Click: Right click");
        
        TestExecutor exec;
        exec.cursorPosition = {100, 100};
        
        Array<Interaction> interactions;
        Interaction click = makeClick();
        click.mouse.rightClick = true;
        interactions.add(click);
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        dispatcher.execute(interactions, exec, log);
        
        bool hasRightDown = false;
        for (const auto& entry : exec.log)
        {
            if (entry.type == InteractionIds::mouseDown && entry.rightClick)
                hasRightDown = true;
        }
        
        expect(hasRightDown, "Should have right mouseDown");
    }
    
    void testExecuteClickWithModifiers()
    {
        beginTest("Click: With modifiers");
        
        TestExecutor exec;
        exec.cursorPosition = {100, 100};
        
        Array<Interaction> interactions;
        Interaction click = makeClick();
        click.mouse.modifiers = ModifierKeys(ModifierKeys::shiftModifier | ModifierKeys::ctrlModifier);
        interactions.add(click);
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        dispatcher.execute(interactions, exec, log);
        
        bool hasModifiers = false;
        for (const auto& entry : exec.log)
        {
            if (entry.type == InteractionIds::mouseDown)
            {
                hasModifiers = entry.mods.isShiftDown() && entry.mods.isCtrlDown();
            }
        }
        
        expect(hasModifiers, "Should have shift and ctrl modifiers");
    }
    
    //==========================================================================
    // Drag Tests
    //==========================================================================
    
    void testExecuteDrag()
    {
        beginTest("Drag: Basic execution");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        exec.cursorPosition = {125, 175};  // At Knob1 center
        
        Array<Interaction> interactions;
        interactions.add(makeDrag("Knob1", 0, -50));  // Drag up 50px
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Execution should succeed");
        
        // Final position should be start + delta
        expect(exec.cursorPosition.y == 125, "Cursor Y should be 175 - 50 = 125: " + String(exec.cursorPosition.y));
    }
    
    void testExecuteDragWithModifiers()
    {
        beginTest("Drag: With shift modifier");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        exec.cursorPosition = {125, 175};
        
        Array<Interaction> interactions;
        Interaction drag = makeDrag("Knob1", 0, -50);
        drag.mouse.modifiers = ModifierKeys(ModifierKeys::shiftModifier);
        interactions.add(drag);
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        dispatcher.execute(interactions, exec, log);
        
        bool hasShift = false;
        for (const auto& entry : exec.log)
        {
            if (entry.type == InteractionIds::mouseDown && entry.mods.isShiftDown())
                hasShift = true;
        }
        
        expect(hasShift, "Should have shift modifier during drag");
    }
    
    void testExecuteDragUpdatesPosition()
    {
        beginTest("Drag: Updates cursor position");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        exec.cursorPosition = {100, 200};
        
        Array<Interaction> interactions;
        interactions.add(makeDrag("Panel1", 30, -40));
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        dispatcher.execute(interactions, exec, log);
        
        expect(exec.cursorPosition.x == 130, "Cursor X should be 100 + 30 = 130");
        expect(exec.cursorPosition.y == 160, "Cursor Y should be 200 - 40 = 160");
    }
    
    //==========================================================================
    // Screenshot Tests
    //==========================================================================
    
    void testExecuteScreenshot()
    {
        beginTest("Screenshot: Basic execution");
        
        TestExecutor exec;
        
        Array<Interaction> interactions;
        interactions.add(makeScreenshot("test_capture"));
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Execution should succeed");
        
        bool hasScreenshot = false;
        for (const auto& entry : exec.log)
        {
            if (entry.type == InteractionIds::screenshot && entry.screenshotId == "test_capture")
                hasScreenshot = true;
        }
        
        expect(hasScreenshot, "Should have screenshot event");
    }
    
    //==========================================================================
    // SelectMenuItem Tests
    //==========================================================================
    
    void testExecuteSelectMenuItem()
    {
        beginTest("SelectMenuItem: Basic execution");
        
        TestExecutor exec;
        exec.cursorPosition = {100, 100};
        exec.addMockMenuItem("Option 1", 1, {50, 200, 150, 25});
        exec.addMockMenuItem("Option 2", 2, {50, 225, 150, 25});
        
        Array<Interaction> interactions;
        interactions.add(makeSelectMenuItem("Option 2"));
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.wasOk(), "Execution should succeed");
        
        auto selected = dispatcher.getLastSelectedMenuItem();
        expect(selected.wasSelected, "Should have selected item");
        expect(selected.text == "Option 2", "Selected text should match");
        expect(selected.itemId == 2, "Selected ID should be 2");
    }
    
    void testExecuteSelectMenuItemNoMenu()
    {
        beginTest("SelectMenuItem: Fails when no menu open");
        
        TestExecutor exec;
        // No mock menu items added
        
        Array<Interaction> interactions;
        interactions.add(makeSelectMenuItem("Option 1"));
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        auto result = dispatcher.execute(interactions, exec, log);
        
        expect(result.result.failed(), "Should fail when no menu open");
        expect(result.result.getErrorMessage().containsIgnoreCase("menu"), 
               "Error should mention menu");
    }
    
    //==========================================================================
    // Timing Tests
    //==========================================================================
    
    void testDelayWorks()
    {
        beginTest("Timing: Delay is applied");
        
        TestExecutor exec;
        exec.cursorPosition = {100, 100};
        
        Array<Interaction> interactions;
        interactions.add(makeClick(50));  // 50ms delay
        
        auto startTime = Time::getMillisecondCounterHiRes();
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        dispatcher.execute(interactions, exec, log);
        
        auto elapsed = Time::getMillisecondCounterHiRes() - startTime;
        
        // Should have waited at least 50ms (with some tolerance)
        expect(elapsed >= 40, "Should wait for delay, elapsed: " + String(elapsed));
    }
    
    //==========================================================================
    // Component Resolution Tests
    //==========================================================================
    
    void testResolutionSuccess()
    {
        beginTest("Resolution: Finds component");
        
        TestExecutor exec;
        exec.addMockComponent("TestBtn", {50, 50, 100, 50}, true);
        
        auto result = exec.resolveTarget(ComponentTargetPath("TestBtn"));
        
        expect(result.success(), "Resolution should succeed");
        expect(result.componentBounds == Rectangle<int>(50, 50, 100, 50), "Bounds should match");
    }
    
    void testResolutionFailsForUnknownComponent()
    {
        beginTest("Resolution: Fails for unknown component");
        
        TestExecutor exec;
        
        auto result = exec.resolveTarget(ComponentTargetPath("NonExistent"));
        
        expect(!result.success(), "Resolution should fail");
        expect(result.error.containsIgnoreCase("not found"), "Error should mention not found");
    }
    
    void testResolutionFailsForHiddenComponent()
    {
        beginTest("Resolution: Fails for hidden component");
        
        TestExecutor exec;
        exec.addMockComponent("HiddenBtn", {50, 50, 100, 50}, false);  // visible = false
        
        auto result = exec.resolveTarget(ComponentTargetPath("HiddenBtn"));
        
        expect(!result.success(), "Resolution should fail");
        expect(result.error.containsIgnoreCase("not visible"), "Error should mention not visible");
    }
    
    //==========================================================================
    // Execution Log Tests
    //==========================================================================
    
    void testExecutionLogContainsAllEvents()
    {
        beginTest("Log: Contains all events");
        
        TestExecutor exec;
        setupDefaultMockComponents(exec);
        exec.cursorPosition = {140, 115};
        
        Array<Interaction> interactions;
        interactions.add(makeClick());
        interactions.add(makeScreenshot("snap"));
        
        InteractionDispatcher dispatcher;
        Array<var> log;
        dispatcher.execute(interactions, exec, log);
        
        // Should have: mouseDown, mouseUp, screenshot
        expect(log.size() >= 3, "Log should have at least 3 entries: " + String(log.size()));
        
        bool hasDown = false, hasUp = false, hasScreenshot = false;
        for (const auto& entry : log)
        {
            if (entry.isObject())
            {
                String type = entry.getProperty(RestApiIds::type, "").toString();
                if (type == InteractionIds::mouseDown.toString()) hasDown = true;
                if (type == InteractionIds::mouseUp.toString()) hasUp = true;
                if (type == InteractionIds::screenshot.toString()) hasScreenshot = true;
            }
        }
        
        expect(hasDown, "Log should contain mouseDown");
        expect(hasUp, "Log should contain mouseUp");
        expect(hasScreenshot, "Log should contain screenshot");
    }
};

static InteractionDispatcherTests interactionDispatcherTests;

} // namespace hise
