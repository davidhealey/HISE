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

class InteractionParserTests : public UnitTest
{
public:
    InteractionParserTests() : UnitTest("Interaction Parser Tests", "AI Tools") {}
    
    void runTest() override
    {
        // Syntax Validation
        testParseNotArray();
        testParseEmptyArray();
        testParseMissingType();
        testParseInvalidType();
        testParseMissingTarget();
        testParseMissingTimestamp();
        testParseInvalidPositionFormat();
        testParsePositionNotObject();
        testParseMidiMissingEventData();
        testParseMidiMultipleEventTypes();
        testParseMovePathNotArray();
        testParseInteractionNotObject();
        testParseTypeNotString();
        testParseTargetNotString();
        
        // Value Range Validation
        testParseNegativeTimestamp();
        testParsePositionOutOfRangeAllowed();
        testParseNegativeDuration();
        testParseZeroDuration();
        testParseDurationExceedsLimit();
        testParseMidiNoteOutOfRange();
        testParseMidiNoteTooLow();
        testParseMidiVelocityOutOfRange();
        testParseMidiChannelTooLow();
        testParseMidiChannelTooHigh();
        testParseMidiCCControllerOutOfRange();
        testParseMidiCCValueOutOfRange();
        testParseMidiPitchBendOutOfRange();
        testParseEmptyTargetString();
        testParseTooManyInteractions();
        testParseMovePathTooManyPoints();
        testParseMoveEmptyPath();
        testParseMoveRequiresFromTo();
        testParseMoveWithPositionRejected();
        testParseMoveWithPathAccepted();
        
        // Physical Plausibility - Mouse Overlap Detection
        testOverlappingDrags();
        testClickDuringDrag();
        testDoubleClickDuringDrag();
        testDragDuringDrag();
        testHoverDuringDrag();
        testMoveDuringDrag();
        testExitDuringDrag();
        testRightClickDuringDrag();
        
        // Physical Plausibility - Valid Sequential Events
        testClickAfterDragEnds();
        testClickAtExactDragEnd();
        testDragAfterClick();
        testHoverAfterDrag();
        testMidiDuringDrag();
        testMultipleMidiEvents();
        
        // Physical Plausibility - MIDI Warnings
        testNoteOffWithoutNoteOn();
        testNoteOnWithoutNoteOff();
        testDuplicateNoteOn();
        testNoteOffWrongNote();
        testMidiWarningsInStrictMode();
        
        // Physical Plausibility - Sequence Duration
        testSequenceDurationExceedsTimeout();
        testSequenceDurationExceedsTimeoutStrict();
        
        // Valid Input - Click Variants
        testParseValidClick();
        testParseValidClickWithPosition();
        testParseValidClickWithFromField();
        testParseValidClickRightClick();
        testParseValidClickAllModifiers();
        testParseValidClickSingleModifier();
        
        // Valid Input - Double Click
        testParseValidDoubleClick();
        testParseValidDoubleClickWithModifiers();
        
        // Valid Input - Drag
        testParseValidDrag();
        testParseValidDragWithModifiers();
        testParseValidDragMinimalFields();
        
        // Valid Input - Hover
        testParseValidHover();
        testParseValidHoverWithDuration();
        
        // Valid Input - Move
        testParseValidMove();
        testParseValidMoveWithPath();
        testParseValidMoveSinglePathPoint();
        testParseValidMovePathTakesPrecedence();
        
        // Valid Input - Exit
        testParseValidExit();
        
        // Valid Input - MIDI
        testParseValidMidiNoteOn();
        testParseValidMidiNoteOnAllFields();
        testParseValidMidiNoteOff();
        testParseValidMidiCC();
        testParseValidMidiPitchBend();
        
        // Default Values
        testParseClickDefaults();
        testParseDragDefaults();
        testParseHoverDefaults();
        testParseMidiNoteOnDefaults();
        testParseMidiCCDefaults();
        
        // Sequence Handling
        testParseMultipleInteractions();
        testParseTimestampsAutoSort();
        testParseTimestampsAlreadySorted();
        testParseTimestampsAutoSortWarning();
        
        // Error Message Quality
        testErrorMessageIncludesIndex();
        testErrorMessageIncludesFieldName();
        testErrorMessageIncludesActualValue();
        testErrorMessageIncludesValidRange();
    }
    
private:
    
    //==============================================================================
    // Type aliases for convenience
    using Interaction = InteractionParser::Interaction;
    using MouseInteraction = InteractionParser::MouseInteraction;
    using MidiInteraction = InteractionParser::MidiInteraction;
    using ParseResult = InteractionParser::ParseResult;
    
    //==============================================================================
    // Helper Methods
    //==============================================================================
    
    /** Parse JSON and expect success, return interactions */
    Array<Interaction> parseOrFail(const String& json, bool strict = false)
    {
        var input = JSON::parse(json);
        Array<Interaction> result;
        auto parseResult = InteractionParser::parseInteractions(input, result, strict);
        
        expect(parseResult.wasOk(), 
               "Parse should succeed: " + parseResult.getErrorMessage());
        
        // Log any warnings for visibility
        for (auto& warning : parseResult.warnings)
            logMessage("Warning: " + warning);
        
        return result;
    }
    
    /** Parse JSON and expect failure, return the result for inspection */
    ParseResult parseExpectingFailure(const String& json, bool strict = false)
    {
        var input = JSON::parse(json);
        Array<Interaction> result;
        return InteractionParser::parseInteractions(input, result, strict);
    }
    
    /** Parse JSON and expect success with warnings */
    ParseResult parseExpectingWarnings(const String& json, bool strict = false)
    {
        var input = JSON::parse(json);
        Array<Interaction> result;
        auto parseResult = InteractionParser::parseInteractions(input, result, strict);
        
        expect(parseResult.wasOk(), 
               "Parse should succeed (with warnings): " + parseResult.getErrorMessage());
        
        return parseResult;
    }
    
    //==============================================================================
    // Syntax Validation Tests
    //==============================================================================
    
    void testParseNotArray()
    {
        beginTest("Syntax: Input must be array");
        
        auto result = parseExpectingFailure(R"({"type": "click", "target": "Btn", "timestamp": 0})");
        
        expect(result.failed(), "Should reject non-array input");
        expect(result.getErrorMessage().containsIgnoreCase("array"),
               "Error should mention 'array': " + result.getErrorMessage());
    }
    
    void testParseEmptyArray()
    {
        beginTest("Syntax: Empty array rejected");
        
        auto result = parseExpectingFailure(R"([])");
        
        expect(result.failed(), "Should reject empty array");
        expect(result.getErrorMessage().containsIgnoreCase("empty"),
               "Error should mention 'empty': " + result.getErrorMessage());
    }
    
    void testParseMissingType()
    {
        beginTest("Syntax: Missing 'type' field");
        
        auto result = parseExpectingFailure(R"([
            {"target": "Button1", "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject missing type");
        expect(result.getErrorMessage().containsIgnoreCase("type"),
               "Error should mention 'type': " + result.getErrorMessage());
    }
    
    void testParseInvalidType()
    {
        beginTest("Syntax: Invalid type value");
        
        auto result = parseExpectingFailure(R"([
            {"type": "invalidType", "target": "Button1", "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject invalid type");
        expect(result.getErrorMessage().contains("invalidType") ||
               result.getErrorMessage().containsIgnoreCase("type"),
               "Error should mention the invalid type: " + result.getErrorMessage());
    }
    
    void testParseMissingTarget()
    {
        beginTest("Syntax: Missing 'target' field for mouse event");
        
        auto result = parseExpectingFailure(R"([
            {"type": "click", "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject missing target");
        expect(result.getErrorMessage().containsIgnoreCase("target"),
               "Error should mention 'target': " + result.getErrorMessage());
    }
    
    void testParseMissingTimestamp()
    {
        beginTest("Syntax: Missing 'timestamp' field");
        
        auto result = parseExpectingFailure(R"([
            {"type": "click", "target": "Button1"}
        ])");
        
        expect(result.failed(), "Should reject missing timestamp");
        expect(result.getErrorMessage().containsIgnoreCase("timestamp"),
               "Error should mention 'timestamp': " + result.getErrorMessage());
    }
    
    void testParseInvalidPositionFormat()
    {
        beginTest("Syntax: Position must have x and y");
        
        auto result = parseExpectingFailure(R"([
            {"type": "click", "target": "Button1", "timestamp": 0,
             "position": {"x": 0.5}}
        ])");
        
        expect(result.failed(), "Should reject position missing y");
        expect(result.getErrorMessage().containsIgnoreCase("position") ||
               result.getErrorMessage().containsIgnoreCase("y"),
               "Error should mention position issue: " + result.getErrorMessage());
    }
    
    void testParsePositionNotObject()
    {
        beginTest("Syntax: Position must be object");
        
        auto result = parseExpectingFailure(R"([
            {"type": "click", "target": "Button1", "timestamp": 0,
             "position": [0.5, 0.5]}
        ])");
        
        expect(result.failed(), "Should reject position as array");
        expect(result.getErrorMessage().containsIgnoreCase("position"),
               "Error should mention 'position': " + result.getErrorMessage());
    }
    
    void testParseMidiMissingEventData()
    {
        beginTest("Syntax: MIDI must have noteOn/noteOff/cc/pitchBend");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject MIDI without event data");
        expect(result.getErrorMessage().containsIgnoreCase("noteOn") ||
               result.getErrorMessage().containsIgnoreCase("midi"),
               "Error should mention missing MIDI data: " + result.getErrorMessage());
    }
    
    void testParseMidiMultipleEventTypes()
    {
        beginTest("Syntax: MIDI cannot have multiple event types");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "timestamp": 0,
             "noteOn": {"note": 60},
             "noteOff": {"note": 60}}
        ])");
        
        expect(result.failed(), "Should reject multiple MIDI event types");
    }
    
    void testParseMovePathNotArray()
    {
        beginTest("Syntax: Move path must be array");
        
        auto result = parseExpectingFailure(R"([
            {"type": "move", "target": "Panel1", "timestamp": 0, "duration": 100,
             "path": {"x": 0.5, "y": 0.5}}
        ])");
        
        expect(result.failed(), "Should reject path as object");
    }
    
    void testParseInteractionNotObject()
    {
        beginTest("Syntax: Each interaction must be object");
        
        auto result = parseExpectingFailure(R"([
            "click"
        ])");
        
        expect(result.failed(), "Should reject non-object interaction");
    }
    
    void testParseTypeNotString()
    {
        beginTest("Syntax: Type must be string");
        
        auto result = parseExpectingFailure(R"([
            {"type": 123, "target": "Button1", "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject non-string type");
    }
    
    void testParseTargetNotString()
    {
        beginTest("Syntax: Target must be string");
        
        auto result = parseExpectingFailure(R"([
            {"type": "click", "target": 123, "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject non-string target");
    }
    
    //==============================================================================
    // Value Range Validation Tests
    //==============================================================================
    
    void testParseNegativeTimestamp()
    {
        beginTest("Range: Negative timestamp rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "click", "target": "Button1", "timestamp": -100}
        ])");
        
        expect(result.failed(), "Should reject negative timestamp");
        expect(result.getErrorMessage().containsIgnoreCase("timestamp"),
               "Error should mention 'timestamp': " + result.getErrorMessage());
    }
    
    void testParsePositionOutOfRangeAllowed()
    {
        beginTest("Range: Position outside [0,1] allowed for drag support");
        
        // Positions outside [0,1] are now allowed to support drag operations
        // that extend beyond component bounds
        
        // X > 1.0 allowed
        {
            auto interactions = parseOrFail(R"([
                {"type": "click", "target": "Button1", "timestamp": 0,
                 "position": {"x": 1.5, "y": 0.5}}
            ])");
            expect(interactions.size() == 1, "Should have 1 interaction");
            expect(interactions[0].mouse.fromNormalized.x == 1.5f, "X should be 1.5");
        }
        
        // Y > 1.0 allowed
        {
            auto interactions = parseOrFail(R"([
                {"type": "click", "target": "Button1", "timestamp": 0,
                 "position": {"x": 0.5, "y": 1.5}}
            ])");
            expect(interactions.size() == 1, "Should have 1 interaction");
            expect(interactions[0].mouse.fromNormalized.y == 1.5f, "Y should be 1.5");
        }
        
        // Negative values allowed
        {
            auto interactions = parseOrFail(R"([
                {"type": "click", "target": "Button1", "timestamp": 0,
                 "position": {"x": -0.5, "y": -0.2}}
            ])");
            expect(interactions.size() == 1, "Should have 1 interaction");
            expect(interactions[0].mouse.fromNormalized.x == -0.5f, "X should be -0.5");
            expect(interactions[0].mouse.fromNormalized.y == -0.2f, "Y should be -0.2");
        }
    }
    
    void testParseNegativeDuration()
    {
        beginTest("Range: Negative duration rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": -50}
        ])");
        
        expect(result.failed(), "Should reject negative duration");
    }
    
    void testParseZeroDuration()
    {
        beginTest("Range: Zero duration rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 0}
        ])");
        
        expect(result.failed(), "Should reject zero duration");
    }
    
    void testParseDurationExceedsLimit()
    {
        beginTest("Range: Duration > 10s rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 15000}
        ])");
        
        expect(result.failed(), "Should reject duration > 10 seconds");
    }
    
    void testParseMidiNoteOutOfRange()
    {
        beginTest("Range: MIDI note > 127 rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "noteOn": {"note": 128}, "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject note > 127");
    }
    
    void testParseMidiNoteTooLow()
    {
        beginTest("Range: MIDI note < 0 rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "noteOn": {"note": -1}, "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject note < 0");
    }
    
    void testParseMidiVelocityOutOfRange()
    {
        beginTest("Range: MIDI velocity > 127 rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "noteOn": {"note": 60, "velocity": 200}, "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject velocity > 127");
    }
    
    void testParseMidiChannelTooLow()
    {
        beginTest("Range: MIDI channel < 1 rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "noteOn": {"channel": 0, "note": 60}, "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject channel < 1");
    }
    
    void testParseMidiChannelTooHigh()
    {
        beginTest("Range: MIDI channel > 16 rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "noteOn": {"channel": 17, "note": 60}, "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject channel > 16");
    }
    
    void testParseMidiCCControllerOutOfRange()
    {
        beginTest("Range: MIDI CC controller > 127 rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "cc": {"controller": 128, "value": 64}, "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject controller > 127");
    }
    
    void testParseMidiCCValueOutOfRange()
    {
        beginTest("Range: MIDI CC value > 127 rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "cc": {"controller": 1, "value": 128}, "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject CC value > 127");
    }
    
    void testParseMidiPitchBendOutOfRange()
    {
        beginTest("Range: MIDI pitch bend out of range rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "pitchBend": {"value": 20000}, "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject pitch bend > 16383");
    }
    
    void testParseEmptyTargetString()
    {
        beginTest("Range: Empty target string rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "click", "target": "", "timestamp": 0}
        ])");
        
        expect(result.failed(), "Should reject empty target");
    }
    
    void testParseTooManyInteractions()
    {
        beginTest("Range: More than 100 interactions rejected");
        
        String json = "[";
        for (int i = 0; i < 101; i++) {
            if (i > 0) json += ",";
            json += R"({"type": "click", "target": "Btn", "timestamp": )" + String(i * 100) + "}";
        }
        json += "]";
        
        auto result = parseExpectingFailure(json);
        
        expect(result.failed(), "Should reject > 100 interactions");
    }
    
    void testParseMovePathTooManyPoints()
    {
        beginTest("Range: Move path > 50 points rejected");
        
        String path = "[";
        for (int i = 0; i < 51; i++) {
            if (i > 0) path += ",";
            path += R"({"x": 0.5, "y": 0.5})";
        }
        path += "]";
        
        auto result = parseExpectingFailure(
            R"([{"type": "move", "target": "Panel1", "timestamp": 0, "duration": 100, "path": )" + path + "}]");
        
        expect(result.failed(), "Should reject path > 50 points");
    }
    
    void testParseMoveEmptyPath()
    {
        beginTest("Range: Move with empty path rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "move", "target": "Panel1", "timestamp": 0, "duration": 100, "path": []}
        ])");
        
        expect(result.failed(), "Should reject empty path");
    }
    
    void testParseMoveRequiresFromTo()
    {
        beginTest("Syntax: Move without from/to rejected");
        
        // Move with no position info at all
        {
            auto result = parseExpectingFailure(R"([
                {"type": "move", "target": "Panel1", "timestamp": 0, "duration": 100}
            ])");
            
            expect(result.failed(), "Should reject move without from/to");
            expect(result.getErrorMessage().containsIgnoreCase("from") &&
                   result.getErrorMessage().containsIgnoreCase("to"),
                   "Error should mention from/to: " + result.getErrorMessage());
        }
        
        // Move with only 'from' (missing 'to')
        {
            auto result = parseExpectingFailure(R"([
                {"type": "move", "target": "Panel1", "timestamp": 0, "duration": 100,
                 "from": {"x": 0.2, "y": 0.2}}
            ])");
            
            expect(result.failed(), "Should reject move with only 'from'");
        }
        
        // Move with only 'to' (missing 'from')
        {
            auto result = parseExpectingFailure(R"([
                {"type": "move", "target": "Panel1", "timestamp": 0, "duration": 100,
                 "to": {"x": 0.8, "y": 0.8}}
            ])");
            
            expect(result.failed(), "Should reject move with only 'to'");
        }
    }
    
    void testParseMoveWithPositionRejected()
    {
        beginTest("Syntax: Move with 'position' instead of from/to rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "move", "target": "Panel1", "timestamp": 0, "duration": 100,
             "position": {"x": 0.5, "y": 0.5}}
        ])");
        
        expect(result.failed(), "Should reject move with 'position'");
        expect(result.getErrorMessage().containsIgnoreCase("from") &&
               result.getErrorMessage().containsIgnoreCase("to"),
               "Error should guide user to use from/to: " + result.getErrorMessage());
    }
    
    void testParseMoveWithPathAccepted()
    {
        beginTest("Syntax: Move with path (no from/to) accepted");
        
        // Path alone is sufficient - no need for from/to
        auto interactions = parseOrFail(R"([
            {"type": "move", "target": "Panel1", "timestamp": 0, "duration": 100,
             "path": [{"x": 0.2, "y": 0.2}, {"x": 0.5, "y": 0.5}, {"x": 0.8, "y": 0.8}]}
        ])");
        
        expect(interactions.size() == 1, "Should have 1 interaction");
        expectEquals(interactions[0].mouse.pathNormalized.size(), 3);
    }
    
    //==============================================================================
    // Physical Plausibility - Mouse Overlap Detection
    //==============================================================================
    
    void testOverlappingDrags()
    {
        beginTest("Plausibility: Overlapping drags rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 300},
            {"type": "drag", "target": "Slider2", "timestamp": 100, "duration": 200}
        ])");
        
        expect(result.failed(), "Should reject overlapping drags");
        expect(result.getErrorMessage().containsIgnoreCase("overlap") ||
               result.getErrorMessage().containsIgnoreCase("simultaneous") ||
               result.getErrorMessage().containsIgnoreCase("during"),
               "Error should mention overlap: " + result.getErrorMessage());
    }
    
    void testClickDuringDrag()
    {
        beginTest("Plausibility: Click during drag rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 300},
            {"type": "click", "target": "Button1", "timestamp": 150}
        ])");
        
        expect(result.failed(), "Should reject click during drag");
    }
    
    void testDoubleClickDuringDrag()
    {
        beginTest("Plausibility: Double-click during drag rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 300},
            {"type": "doubleClick", "target": "Button1", "timestamp": 150}
        ])");
        
        expect(result.failed(), "Should reject double-click during drag");
    }
    
    void testDragDuringDrag()
    {
        beginTest("Plausibility: Drag during drag rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 300},
            {"type": "drag", "target": "Slider2", "timestamp": 50, "duration": 100}
        ])");
        
        expect(result.failed(), "Should reject drag during drag");
    }
    
    void testHoverDuringDrag()
    {
        beginTest("Plausibility: Hover during drag rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 300},
            {"type": "hover", "target": "Panel1", "timestamp": 150, "duration": 50}
        ])");
        
        expect(result.failed(), "Should reject hover during drag");
    }
    
    void testMoveDuringDrag()
    {
        beginTest("Plausibility: Move during drag rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 300},
            {"type": "move", "target": "Panel1", "timestamp": 150, "duration": 50,
             "path": [{"x": 0.2, "y": 0.2}, {"x": 0.8, "y": 0.8}]}
        ])");
        
        expect(result.failed(), "Should reject move during drag");
    }
    
    void testExitDuringDrag()
    {
        beginTest("Plausibility: Exit during drag rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 300},
            {"type": "exit", "target": "Panel1", "timestamp": 150}
        ])");
        
        expect(result.failed(), "Should reject exit during drag");
    }
    
    void testRightClickDuringDrag()
    {
        beginTest("Plausibility: Right-click during drag rejected");
        
        auto result = parseExpectingFailure(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 300},
            {"type": "click", "target": "Button1", "timestamp": 150, "rightClick": true}
        ])");
        
        expect(result.failed(), "Should reject right-click during drag");
    }
    
    //==============================================================================
    // Physical Plausibility - Valid Sequential Events
    //==============================================================================
    
    void testClickAfterDragEnds()
    {
        beginTest("Plausibility: Click after drag ends is valid");
        
        auto interactions = parseOrFail(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 300},
            {"type": "click", "target": "Button1", "timestamp": 350}
        ])");
        
        expectEquals(interactions.size(), 2, "Should have 2 interactions");
    }
    
    void testClickAtExactDragEnd()
    {
        beginTest("Plausibility: Click at exact drag end is valid");
        
        auto interactions = parseOrFail(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 300},
            {"type": "click", "target": "Button1", "timestamp": 300}
        ])");
        
        expectEquals(interactions.size(), 2, "Should have 2 interactions");
    }
    
    void testDragAfterClick()
    {
        beginTest("Plausibility: Drag after click is valid");
        
        auto interactions = parseOrFail(R"([
            {"type": "click", "target": "Button1", "timestamp": 0},
            {"type": "drag", "target": "Slider1", "timestamp": 100, "duration": 200}
        ])");
        
        expectEquals(interactions.size(), 2, "Should have 2 interactions");
    }
    
    void testHoverAfterDrag()
    {
        beginTest("Plausibility: Hover after drag is valid");
        
        auto interactions = parseOrFail(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 300},
            {"type": "hover", "target": "Panel1", "timestamp": 350, "duration": 100}
        ])");
        
        expectEquals(interactions.size(), 2, "Should have 2 interactions");
    }
    
    void testMidiDuringDrag()
    {
        beginTest("Plausibility: MIDI during drag is valid");
        
        auto interactions = parseOrFail(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0, "duration": 300},
            {"type": "midi", "noteOn": {"note": 60}, "timestamp": 50},
            {"type": "midi", "noteOff": {"note": 60}, "timestamp": 250}
        ])");
        
        expectEquals(interactions.size(), 3, "Should have 3 interactions");
    }
    
    void testMultipleMidiEvents()
    {
        beginTest("Plausibility: Multiple simultaneous MIDI events valid");
        
        auto interactions = parseOrFail(R"([
            {"type": "midi", "noteOn": {"note": 60}, "timestamp": 0},
            {"type": "midi", "noteOn": {"note": 64}, "timestamp": 0},
            {"type": "midi", "noteOn": {"note": 67}, "timestamp": 0}
        ])");
        
        expectEquals(interactions.size(), 3, "Should have 3 MIDI events");
    }
    
    //==============================================================================
    // Physical Plausibility - MIDI Warnings
    //==============================================================================
    
    void testNoteOffWithoutNoteOn()
    {
        beginTest("Plausibility: NoteOff without NoteOn (warning, not error)");
        
        auto result = parseExpectingWarnings(R"([
            {"type": "midi", "noteOff": {"note": 60}, "timestamp": 0}
        ])");
        
        expect(result.wasOk(), "Should parse successfully");
        expect(result.warnings.size() > 0, "Should have warning about missing NoteOn");
    }
    
    void testNoteOnWithoutNoteOff()
    {
        beginTest("Plausibility: NoteOn without NoteOff (warning, not error)");
        
        auto result = parseExpectingWarnings(R"([
            {"type": "midi", "noteOn": {"note": 60}, "timestamp": 0}
        ])");
        
        expect(result.wasOk(), "Should parse successfully");
        expect(result.warnings.size() > 0, "Should have warning about hanging note");
    }
    
    void testDuplicateNoteOn()
    {
        beginTest("Plausibility: Duplicate NoteOn is valid (retrigger)");
        
        auto interactions = parseOrFail(R"([
            {"type": "midi", "noteOn": {"note": 60}, "timestamp": 0},
            {"type": "midi", "noteOn": {"note": 60}, "timestamp": 100}
        ])");
        
        expectEquals(interactions.size(), 2, "Should allow retriggering");
    }
    
    void testNoteOffWrongNote()
    {
        beginTest("Plausibility: NoteOff for different note than NoteOn (warning)");
        
        auto result = parseExpectingWarnings(R"([
            {"type": "midi", "noteOn": {"note": 60}, "timestamp": 0},
            {"type": "midi", "noteOff": {"note": 62}, "timestamp": 100}
        ])");
        
        expect(result.wasOk(), "Should parse but warn");
        // Two warnings: note 60 has no NoteOff, note 62 has no NoteOn
    }
    
    void testMidiWarningsInStrictMode()
    {
        beginTest("Plausibility: MIDI warnings become errors in strict mode");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "noteOn": {"note": 60}, "timestamp": 0}
        ])", true);  // strict = true
        
        expect(result.failed(), "Should fail in strict mode due to hanging note");
    }
    
    //==============================================================================
    // Physical Plausibility - Sequence Duration
    //==============================================================================
    
    void testSequenceDurationExceedsTimeout()
    {
        beginTest("Plausibility: Sequence > 20s warns but parses");
        
        // Total duration: 25 seconds
        auto result = parseExpectingWarnings(R"([
            {"type": "drag", "target": "S1", "timestamp": 0, "duration": 5000},
            {"type": "drag", "target": "S2", "timestamp": 6000, "duration": 5000},
            {"type": "drag", "target": "S3", "timestamp": 12000, "duration": 5000},
            {"type": "drag", "target": "S4", "timestamp": 18000, "duration": 7000}
        ])");
        
        expect(result.wasOk(), "Should parse with warning");
        expect(result.warnings.size() > 0, "Should warn about exceeding session timeout");
    }
    
    void testSequenceDurationExceedsTimeoutStrict()
    {
        beginTest("Plausibility: Sequence > 20s fails in strict mode");
        
        auto result = parseExpectingFailure(R"([
            {"type": "drag", "target": "S1", "timestamp": 0, "duration": 5000},
            {"type": "drag", "target": "S2", "timestamp": 6000, "duration": 5000},
            {"type": "drag", "target": "S3", "timestamp": 12000, "duration": 5000},
            {"type": "drag", "target": "S4", "timestamp": 18000, "duration": 7000}
        ])", true);  // strict = true
        
        expect(result.failed(), "Should fail in strict mode");
    }
    
    //==============================================================================
    // Valid Input - Click Variants
    //==============================================================================
    
    void testParseValidClick()
    {
        beginTest("Valid: Basic click");
        
        auto interactions = parseOrFail(R"([
            {"type": "click", "target": "Button1", "timestamp": 0}
        ])");
        
        expectEquals(interactions.size(), 1);
        expect(!interactions[0].isMidi);
        expectEquals((int)interactions[0].mouse.type, (int)MouseInteraction::Type::Click);
        expectEquals(interactions[0].mouse.targetComponentId, String("Button1"));
        expectEquals(interactions[0].mouse.timestampMs, 0);
    }
    
    void testParseValidClickWithPosition()
    {
        beginTest("Valid: Click with position field");
        
        auto interactions = parseOrFail(R"([
            {"type": "click", "target": "Button1", "timestamp": 0,
             "position": {"x": 0.25, "y": 0.75}}
        ])");
        
        expectEquals(interactions[0].mouse.fromNormalized.x, 0.25f, "X should be 0.25");
        expectEquals(interactions[0].mouse.fromNormalized.y, 0.75f, "Y should be 0.75");
    }
    
    void testParseValidClickWithFromField()
    {
        beginTest("Valid: Click with 'from' field (alternative to position)");
        
        auto interactions = parseOrFail(R"([
            {"type": "click", "target": "Button1", "timestamp": 0,
             "from": {"x": 0.3, "y": 0.7}}
        ])");
        
        expectEquals(interactions[0].mouse.fromNormalized.x, 0.3f);
        expectEquals(interactions[0].mouse.fromNormalized.y, 0.7f);
    }
    
    void testParseValidClickRightClick()
    {
        beginTest("Valid: Right-click");
        
        auto interactions = parseOrFail(R"([
            {"type": "click", "target": "Button1", "timestamp": 0, "rightClick": true}
        ])");
        
        expect(interactions[0].mouse.rightClick, "Should be right-click");
    }
    
    void testParseValidClickAllModifiers()
    {
        beginTest("Valid: Click with all modifiers");
        
        auto interactions = parseOrFail(R"([
            {"type": "click", "target": "Button1", "timestamp": 0,
             "shiftDown": true, "ctrlDown": true, "altDown": true, "cmdDown": true}
        ])");
        
        expect(interactions[0].mouse.shiftDown, "Shift should be down");
        expect(interactions[0].mouse.ctrlDown, "Ctrl should be down");
        expect(interactions[0].mouse.altDown, "Alt should be down");
        expect(interactions[0].mouse.cmdDown, "Cmd should be down");
    }
    
    void testParseValidClickSingleModifier()
    {
        beginTest("Valid: Click with single modifier");
        
        auto interactions = parseOrFail(R"([
            {"type": "click", "target": "Button1", "timestamp": 0, "shiftDown": true}
        ])");
        
        expect(interactions[0].mouse.shiftDown, "Shift should be down");
        expect(!interactions[0].mouse.ctrlDown, "Ctrl should be up");
        expect(!interactions[0].mouse.altDown, "Alt should be up");
        expect(!interactions[0].mouse.cmdDown, "Cmd should be up");
    }
    
    //==============================================================================
    // Valid Input - Double Click
    //==============================================================================
    
    void testParseValidDoubleClick()
    {
        beginTest("Valid: Double-click");
        
        auto interactions = parseOrFail(R"([
            {"type": "doubleClick", "target": "Label1", "timestamp": 0}
        ])");
        
        expectEquals((int)interactions[0].mouse.type, (int)MouseInteraction::Type::DoubleClick);
    }
    
    void testParseValidDoubleClickWithModifiers()
    {
        beginTest("Valid: Double-click with modifiers");
        
        auto interactions = parseOrFail(R"([
            {"type": "doubleClick", "target": "Label1", "timestamp": 0, "ctrlDown": true}
        ])");
        
        expect(interactions[0].mouse.ctrlDown);
    }
    
    //==============================================================================
    // Valid Input - Drag
    //==============================================================================
    
    void testParseValidDrag()
    {
        beginTest("Valid: Drag with all fields");
        
        auto interactions = parseOrFail(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 100,
             "from": {"x": 0.5, "y": 0.8},
             "to": {"x": 0.5, "y": 0.2},
             "duration": 300}
        ])");
        
        expectEquals((int)interactions[0].mouse.type, (int)MouseInteraction::Type::Drag);
        expectEquals(interactions[0].mouse.fromNormalized.x, 0.5f);
        expectEquals(interactions[0].mouse.fromNormalized.y, 0.8f);
        expectEquals(interactions[0].mouse.toNormalized.x, 0.5f);
        expectEquals(interactions[0].mouse.toNormalized.y, 0.2f);
        expectEquals(interactions[0].mouse.durationMs, 300);
        expectEquals(interactions[0].mouse.timestampMs, 100);
    }
    
    void testParseValidDragWithModifiers()
    {
        beginTest("Valid: Drag with shift modifier");
        
        auto interactions = parseOrFail(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0,
             "from": {"x": 0.5, "y": 0.8}, "to": {"x": 0.5, "y": 0.2},
             "duration": 300, "shiftDown": true}
        ])");
        
        expect(interactions[0].mouse.shiftDown, "Shift should be down for fine control");
    }
    
    void testParseValidDragMinimalFields()
    {
        beginTest("Valid: Drag with minimal fields uses defaults");
        
        auto interactions = parseOrFail(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0}
        ])");
        
        // Should use default from/to/duration
        expectEquals(interactions[0].mouse.durationMs, 100, "Default duration should be 100ms");
    }
    
    //==============================================================================
    // Valid Input - Hover
    //==============================================================================
    
    void testParseValidHover()
    {
        beginTest("Valid: Hover");
        
        auto interactions = parseOrFail(R"([
            {"type": "hover", "target": "InfoPanel", "timestamp": 0}
        ])");
        
        expectEquals((int)interactions[0].mouse.type, (int)MouseInteraction::Type::Hover);
    }
    
    void testParseValidHoverWithDuration()
    {
        beginTest("Valid: Hover with duration");
        
        auto interactions = parseOrFail(R"([
            {"type": "hover", "target": "InfoPanel", "timestamp": 0, "duration": 500}
        ])");
        
        expectEquals(interactions[0].mouse.durationMs, 500);
    }
    
    //==============================================================================
    // Valid Input - Move
    //==============================================================================
    
    void testParseValidMove()
    {
        beginTest("Valid: Move with from/to");
        
        auto interactions = parseOrFail(R"([
            {"type": "move", "target": "XYPad", "timestamp": 0,
             "from": {"x": 0.2, "y": 0.2}, "to": {"x": 0.8, "y": 0.8}, "duration": 200}
        ])");
        
        expectEquals((int)interactions[0].mouse.type, (int)MouseInteraction::Type::Move);
    }
    
    void testParseValidMoveWithPath()
    {
        beginTest("Valid: Move with path");
        
        auto interactions = parseOrFail(R"([
            {"type": "move", "target": "XYPad", "timestamp": 0, "duration": 300,
             "path": [{"x": 0.2, "y": 0.2}, {"x": 0.8, "y": 0.5}, {"x": 0.5, "y": 0.8}]}
        ])");
        
        expectEquals(interactions[0].mouse.pathNormalized.size(), 3);
    }
    
    void testParseValidMoveSinglePathPoint()
    {
        beginTest("Valid: Move with single path point");
        
        auto interactions = parseOrFail(R"([
            {"type": "move", "target": "XYPad", "timestamp": 0, "duration": 100,
             "path": [{"x": 0.5, "y": 0.5}]}
        ])");
        
        expectEquals(interactions[0].mouse.pathNormalized.size(), 1);
    }
    
    void testParseValidMovePathTakesPrecedence()
    {
        beginTest("Valid: Move path takes precedence over from/to");
        
        auto interactions = parseOrFail(R"([
            {"type": "move", "target": "XYPad", "timestamp": 0, "duration": 200,
             "from": {"x": 0.1, "y": 0.1}, "to": {"x": 0.9, "y": 0.9},
             "path": [{"x": 0.3, "y": 0.3}, {"x": 0.7, "y": 0.7}]}
        ])");
        
        // Path should be used, from/to ignored
        expectEquals(interactions[0].mouse.pathNormalized.size(), 2);
        expectEquals(interactions[0].mouse.pathNormalized[0].x, 0.3f);
    }
    
    //==============================================================================
    // Valid Input - Exit
    //==============================================================================
    
    void testParseValidExit()
    {
        beginTest("Valid: Exit");
        
        auto interactions = parseOrFail(R"([
            {"type": "exit", "target": "HoverPanel", "timestamp": 0}
        ])");
        
        expectEquals((int)interactions[0].mouse.type, (int)MouseInteraction::Type::Exit);
    }
    
    //==============================================================================
    // Valid Input - MIDI
    //==============================================================================
    
    void testParseValidMidiNoteOn()
    {
        beginTest("Valid: MIDI NoteOn");
        
        auto interactions = parseOrFail(R"([
            {"type": "midi", "noteOn": {"note": 60}, "timestamp": 0}
        ])");
        
        expect(interactions[0].isMidi);
        expectEquals((int)interactions[0].midi.type, (int)MidiInteraction::Type::NoteOn);
        expectEquals(interactions[0].midi.noteOrController, 60);
    }
    
    void testParseValidMidiNoteOnAllFields()
    {
        beginTest("Valid: MIDI NoteOn with all fields");
        
        auto interactions = parseOrFail(R"([
            {"type": "midi", "noteOn": {"channel": 2, "note": 64, "velocity": 100}, "timestamp": 50}
        ])");
        
        expectEquals(interactions[0].midi.channel, 2);
        expectEquals(interactions[0].midi.noteOrController, 64);
        expectEquals(interactions[0].midi.valueOrVelocity, 100);
        expectEquals(interactions[0].midi.timestampMs, 50);
    }
    
    void testParseValidMidiNoteOff()
    {
        beginTest("Valid: MIDI NoteOff");
        
        auto interactions = parseOrFail(R"([
            {"type": "midi", "noteOff": {"note": 60}, "timestamp": 0}
        ])");
        
        expect(interactions[0].isMidi);
        expectEquals((int)interactions[0].midi.type, (int)MidiInteraction::Type::NoteOff);
    }
    
    void testParseValidMidiCC()
    {
        beginTest("Valid: MIDI CC");
        
        auto interactions = parseOrFail(R"([
            {"type": "midi", "cc": {"controller": 1, "value": 64}, "timestamp": 0}
        ])");
        
        expectEquals((int)interactions[0].midi.type, (int)MidiInteraction::Type::Controller);
        expectEquals(interactions[0].midi.noteOrController, 1);
        expectEquals(interactions[0].midi.valueOrVelocity, 64);
    }
    
    void testParseValidMidiPitchBend()
    {
        beginTest("Valid: MIDI PitchBend");
        
        auto interactions = parseOrFail(R"([
            {"type": "midi", "pitchBend": {"value": 8192}, "timestamp": 0}
        ])");
        
        expectEquals((int)interactions[0].midi.type, (int)MidiInteraction::Type::PitchBend);
        expectEquals(interactions[0].midi.valueOrVelocity, 8192);
    }
    
    //==============================================================================
    // Default Values
    //==============================================================================
    
    void testParseClickDefaults()
    {
        beginTest("Defaults: Click uses center position");
        
        auto interactions = parseOrFail(R"([
            {"type": "click", "target": "Button1", "timestamp": 0}
        ])");
        
        expectEquals(interactions[0].mouse.fromNormalized.x, 0.5f, "Default X should be 0.5");
        expectEquals(interactions[0].mouse.fromNormalized.y, 0.5f, "Default Y should be 0.5");
        expect(!interactions[0].mouse.rightClick, "Default should be left-click");
        expect(!interactions[0].mouse.shiftDown, "Default shift should be false");
        expect(!interactions[0].mouse.ctrlDown, "Default ctrl should be false");
        expect(!interactions[0].mouse.altDown, "Default alt should be false");
        expect(!interactions[0].mouse.cmdDown, "Default cmd should be false");
    }
    
    void testParseDragDefaults()
    {
        beginTest("Defaults: Drag uses sensible defaults");
        
        auto interactions = parseOrFail(R"([
            {"type": "drag", "target": "Slider1", "timestamp": 0}
        ])");
        
        expectEquals(interactions[0].mouse.durationMs, 100, "Default duration should be 100ms");
        expectEquals(interactions[0].mouse.fromNormalized.x, 0.5f);
        expectEquals(interactions[0].mouse.fromNormalized.y, 0.5f);
        expectEquals(interactions[0].mouse.toNormalized.x, 0.5f);
        expectEquals(interactions[0].mouse.toNormalized.y, 0.5f);
    }
    
    void testParseHoverDefaults()
    {
        beginTest("Defaults: Hover uses sensible defaults");
        
        auto interactions = parseOrFail(R"([
            {"type": "hover", "target": "Panel1", "timestamp": 0}
        ])");
        
        expectEquals(interactions[0].mouse.durationMs, 100, "Default hover duration");
    }
    
    void testParseMidiNoteOnDefaults()
    {
        beginTest("Defaults: MIDI NoteOn defaults");
        
        auto interactions = parseOrFail(R"([
            {"type": "midi", "noteOn": {"note": 60}, "timestamp": 0}
        ])");
        
        expectEquals(interactions[0].midi.channel, 1, "Default channel should be 1");
        expectEquals(interactions[0].midi.valueOrVelocity, 100, "Default velocity should be 100");
    }
    
    void testParseMidiCCDefaults()
    {
        beginTest("Defaults: MIDI CC defaults");
        
        auto interactions = parseOrFail(R"([
            {"type": "midi", "cc": {"controller": 1, "value": 64}, "timestamp": 0}
        ])");
        
        expectEquals(interactions[0].midi.channel, 1, "Default channel should be 1");
    }
    
    //==============================================================================
    // Sequence Handling
    //==============================================================================
    
    void testParseMultipleInteractions()
    {
        beginTest("Sequence: Multiple interactions parsed correctly");
        
        auto interactions = parseOrFail(R"([
            {"type": "hover", "target": "Knob1", "timestamp": 0, "duration": 50},
            {"type": "drag", "target": "Knob1", "from": {"x": 0.5, "y": 0.5}, 
             "to": {"x": 0.5, "y": 0.1}, "timestamp": 100, "duration": 200},
            {"type": "click", "target": "Button1", "timestamp": 350},
            {"type": "midi", "noteOn": {"note": 60}, "timestamp": 400},
            {"type": "midi", "noteOff": {"note": 60}, "timestamp": 900}
        ])");
        
        expectEquals(interactions.size(), 5, "Should have 5 interactions");
    }
    
    void testParseTimestampsAutoSort()
    {
        beginTest("Sequence: Out-of-order timestamps are auto-sorted");
        
        auto interactions = parseOrFail(R"([
            {"type": "click", "target": "A", "timestamp": 200},
            {"type": "click", "target": "B", "timestamp": 100},
            {"type": "click", "target": "C", "timestamp": 300}
        ])");
        
        // After sorting, order should be B, A, C
        expectEquals(interactions[0].mouse.targetComponentId, String("B"));
        expectEquals(interactions[1].mouse.targetComponentId, String("A"));
        expectEquals(interactions[2].mouse.targetComponentId, String("C"));
    }
    
    void testParseTimestampsAlreadySorted()
    {
        beginTest("Sequence: Already sorted timestamps work correctly");
        
        auto interactions = parseOrFail(R"([
            {"type": "click", "target": "A", "timestamp": 0},
            {"type": "click", "target": "B", "timestamp": 100},
            {"type": "click", "target": "C", "timestamp": 200}
        ])");
        
        expectEquals(interactions[0].mouse.targetComponentId, String("A"));
        expectEquals(interactions[1].mouse.targetComponentId, String("B"));
        expectEquals(interactions[2].mouse.targetComponentId, String("C"));
    }
    
    void testParseTimestampsAutoSortWarning()
    {
        beginTest("Sequence: Auto-sort generates warning");
        
        auto result = parseExpectingWarnings(R"([
            {"type": "click", "target": "A", "timestamp": 200},
            {"type": "click", "target": "B", "timestamp": 100}
        ])");
        
        expect(result.wasOk(), "Should parse successfully");
        expect(result.warnings.size() > 0, "Should warn about auto-sorting");
        
        bool hasOrderWarning = false;
        for (auto& w : result.warnings)
        {
            if (w.containsIgnoreCase("order") || w.containsIgnoreCase("sort"))
                hasOrderWarning = true;
        }
        expect(hasOrderWarning, "Should have warning about timestamp order");
    }
    
    //==============================================================================
    // Error Message Quality
    //==============================================================================
    
    void testErrorMessageIncludesIndex()
    {
        beginTest("Error Quality: Message includes interaction index");
        
        auto result = parseExpectingFailure(R"([
            {"type": "click", "target": "A", "timestamp": 0},
            {"type": "click", "target": "B", "timestamp": 100},
            {"type": "invalidType", "target": "C", "timestamp": 200}
        ])");
        
        expect(result.failed());
        expect(result.getErrorMessage().contains("2") ||
               result.getErrorMessage().contains("[2]") ||
               result.getErrorMessage().containsIgnoreCase("third"),
               "Error should indicate which interaction failed: " + result.getErrorMessage());
    }
    
    void testErrorMessageIncludesFieldName()
    {
        beginTest("Error Quality: Message includes field name");
        
        auto result = parseExpectingFailure(R"([
            {"type": "click", "target": "Button1", "timestamp": -5}
        ])");
        
        expect(result.failed());
        expect(result.getErrorMessage().containsIgnoreCase("timestamp"),
               "Error should mention the problematic field: " + result.getErrorMessage());
    }
    
    void testErrorMessageIncludesActualValue()
    {
        beginTest("Error Quality: Message includes actual value");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "noteOn": {"note": 999}, "timestamp": 0}
        ])");
        
        expect(result.failed());
        expect(result.getErrorMessage().contains("999") ||
               result.getErrorMessage().containsIgnoreCase("note"),
               "Error should mention the invalid value: " + result.getErrorMessage());
    }
    
    void testErrorMessageIncludesValidRange()
    {
        beginTest("Error Quality: Message includes valid range");
        
        auto result = parseExpectingFailure(R"([
            {"type": "midi", "noteOn": {"note": 200}, "timestamp": 0}
        ])");
        
        expect(result.failed());
        expect(result.getErrorMessage().contains("127") ||
               result.getErrorMessage().contains("0-127") ||
               result.getErrorMessage().containsIgnoreCase("range"),
               "Error should mention valid range: " + result.getErrorMessage());
    }
};

static InteractionParserTests interactionParserTests;

} // namespace hise
