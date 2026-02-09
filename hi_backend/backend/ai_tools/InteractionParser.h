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

/** Parser and validator for interaction test sequences.
 *
 *  This class handles parsing JSON interaction arrays and validating them for:
 *  - Syntactical correctness (required fields, correct types)
 *  - Value ranges (normalized positions 0-1, MIDI values 0-127, etc.)
 *  - Physical plausibility (no overlapping mouse interactions, etc.)
 */
class InteractionParser
{
public:
    //==============================================================================
    /** Mouse interaction types */
    struct MouseInteraction
    {
        enum class Type
        {
            Hover,
            Move,
            Click,
            DoubleClick,
            Drag,
            Exit,
            Screenshot,
            SelectMenuItem
        };
        
        Type type = Type::Click;
        String targetComponentId;
        Point<float> fromNormalized{0.5f, 0.5f};
        Point<float> toNormalized{0.5f, 0.5f};
        Array<Point<float>> pathNormalized;  // For Move type with path
        int durationMs = 100;
        int timestampMs = 0;
        bool rightClick = false;
        bool shiftDown = false;
        bool ctrlDown = false;
        bool altDown = false;
        bool cmdDown = false;
        
        // Screenshot-specific fields
        String screenshotId;
        float screenshotScale = 1.0f;
        
        // SelectMenuItem-specific fields
        String menuItemText;  // Text to fuzzy-match for menu item selection
    };
    
    /** MIDI interaction types */
    struct MidiInteraction
    {
        enum class Type
        {
            NoteOn,
            NoteOff,
            Controller,
            PitchBend
        };
        
        Type type = Type::NoteOn;
        int channel = 1;
        int noteOrController = 60;
        int valueOrVelocity = 100;
        int timestampMs = 0;
    };
    
    /** Union of mouse and MIDI interactions */
    struct Interaction
    {
        bool isMidi = false;
        MouseInteraction mouse;
        MidiInteraction midi;
        
        int getTimestamp() const { return isMidi ? midi.timestampMs : mouse.timestampMs; }
        int getEndTime() const;
    };
    
    //==============================================================================
    /** Result of parsing, includes warnings */
    struct ParseResult
    {
        Result result = Result::ok();
        StringArray warnings;
        
        bool wasOk() const { return result.wasOk(); }
        bool failed() const { return result.failed(); }
        String getErrorMessage() const { return result.getErrorMessage(); }
        
        static ParseResult ok() { return {}; }
        static ParseResult fail(const String& message) { ParseResult r; r.result = Result::fail(message); return r; }
        void addWarning(const String& warning) { warnings.add(warning); }
    };
    
    //==============================================================================
    /** Parse JSON array of interactions.
     *
     *  @param jsonArray    The var containing the JSON array of interactions
     *  @param result       Output array to receive parsed interactions
     *  @param strict       If true, warnings are treated as errors
     *  @return             ParseResult containing success/failure and any warnings
     */
    static ParseResult parseInteractions(const var& jsonArray, 
                                         Array<Interaction>& result,
                                         bool strict = false);
    
    //==============================================================================
    // Validation limits
    static constexpr int MaxInteractions = 100;
    static constexpr int MaxPathPoints = 50;
    static constexpr int MaxDurationMs = 10000;      // 10 seconds
    static constexpr int SessionTimeoutMs = 20000;   // 20 seconds
    
private:
    //==============================================================================
    /** Parse a single interaction object */
    static ParseResult parseSingleInteraction(const var& obj, 
                                              Interaction& interaction,
                                              int index);
    
    /** Parse mouse interaction fields */
    static ParseResult parseMouseInteraction(const var& obj,
                                             MouseInteraction& mouse,
                                             const String& type,
                                             int index);
    
    /** Parse MIDI interaction fields */
    static ParseResult parseMidiInteraction(const var& obj,
                                            MidiInteraction& midi,
                                            int index);
    
    /** Parse a position object {x, y} */
    static ParseResult parsePosition(const var& obj,
                                     Point<float>& position,
                                     const String& fieldName,
                                     int index);
    
    /** Validate physical plausibility of the sequence */
    static ParseResult validateSequence(Array<Interaction>& interactions, bool strict);
    
    /** Check for mouse interaction overlaps */
    static ParseResult checkMouseOverlaps(const Array<Interaction>& interactions);
    
    /** Check for MIDI note pairing issues (returns warnings, not errors) */
    static void checkMidiPairing(const Array<Interaction>& interactions, 
                                 ParseResult& result);
    
    /** Sort interactions by timestamp */
    static bool sortByTimestamp(Array<Interaction>& interactions, ParseResult& result);
    
    /** Helper to format error messages with context */
    static String formatError(const String& message, int index, const String& field = {});
    
    /** Get string name for mouse interaction type */
    static String getTypeName(MouseInteraction::Type type);
    
    /** Check if mouse interaction requires button down */
    static bool requiresMouseButton(MouseInteraction::Type type);
    
    /** Get end time of an interaction */
    static int getInteractionEndTime(const Interaction& interaction);
};

} // namespace hise
