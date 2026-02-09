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

//==============================================================================
/** Default timing values for interactions */
struct InteractionDefaults
{
    static constexpr int MOVE_DURATION_MS = 700;
    static constexpr int CLICK_DURATION_MS = 30;
    static constexpr int DOUBLE_CLICK_DELAY_MS = 20;
    static constexpr int DRAG_DURATION_MS = 500;
    static constexpr int MENU_SELECT_DURATION_MS = 700;
};

//==============================================================================
/** Parser and validator for interaction test sequences.
 *
 *  Parses JSON interaction arrays and validates them.
 *  Expands doubleClick to two clicks.
 *  Does NOT perform moveTo auto-insertion (that's done by InteractionTester).
 */
class InteractionParser
{
public:
    //==============================================================================
    /** Mouse interaction data */
    struct MouseInteraction
    {
        //==========================================================================
        /** Position within a component - normalized (0-1) or absolute pixels.
         *  Both modes are relative to the target component's bounds.
         */
        struct Position
        {
            enum class Mode { Normalized, Absolute };
            
            /** Get pixel position in Interface coordinates.
             *  @param componentBounds  Absolute bounds of target component
             */
            Point<int> getPixelPosition(Rectangle<int> componentBounds) const
            {
                if (mode == Mode::Absolute)
                {
                    return {
                        componentBounds.getX() + roundToInt(value.x),
                        componentBounds.getY() + roundToInt(value.y)
                    };
                }
                
                return {
                    componentBounds.getX() + roundToInt(value.x * componentBounds.getWidth()),
                    componentBounds.getY() + roundToInt(value.y * componentBounds.getHeight())
                };
            }
            
            /** Create normalized position (0-1 range relative to component). */
            static Position normalized(float x, float y) 
            { 
                Position p;
                p.mode = Mode::Normalized;
                p.value = {x, y};
                return p;
            }
            
            /** Create absolute pixel position (relative to component origin). */
            static Position absolute(int x, int y) 
            { 
                Position p;
                p.mode = Mode::Absolute;
                p.value = {static_cast<float>(x), static_cast<float>(y)};
                return p;
            }
            
            /** Create normalized position at center (0.5, 0.5). */
            static Position center() { return normalized(0.5f, 0.5f); }
            
            bool isAbsolute() const { return mode == Mode::Absolute; }
            
            bool isCenter() const 
            { 
                return mode == Mode::Normalized && 
                       value.x == 0.5f && value.y == 0.5f; 
            }
            
            bool operator==(const Position& other) const
            {
                return mode == other.mode && value == other.value;
            }
            
            bool operator!=(const Position& other) const { return !(*this == other); }
            
            Mode getMode() const { return mode; }
            Point<float> getValue() const { return value; }
            
        private:
            Mode mode = Mode::Normalized;
            Point<float> value{0.5f, 0.5f};
        };
        
        //==========================================================================
        /** Internal primitive types (dispatcher executes these).
         *  Note: doubleClick is expanded to two Click events at parse time.
         */
        enum class Type
        {
            MoveTo,
            Click,
            Drag,
            Screenshot,
            SelectMenuItem
        };
        
        Type type = Type::Click;
        
        /** Target component ID (for moveTo, click, drag). */
        String targetComponentId;
        
        /** Position within target (for moveTo, click, drag start). */
        Position position = Position::center();
        
        /** Drag-specific: pixel delta from current position. */
        Point<int> deltaPixels{0, 0};
        
        /** Delay before this interaction starts (ms). */
        int delayMs = 0;
        
        /** Duration of animated movement (ms). */
        int durationMs = 0;
        
        /** Modifier keys (shift, ctrl, alt, cmd). */
        ModifierKeys modifiers;
        
        /** Right-click flag (for click only). */
        bool rightClick = false;
        
        /** Screenshot ID (for screenshot only). */
        String screenshotId;
        
        /** Screenshot scale factor (for screenshot only). */
        float screenshotScale = 1.0f;
        
        /** Menu item text to match (for selectMenuItem only). */
        String menuItemText;
        
        /** True if this event was auto-inserted by normalization. */
        bool autoInserted = false;
    };
    
    //==============================================================================
    /** Parsed interaction (mouse only - MIDI support deferred). */
    struct Interaction
    {
        MouseInteraction mouse;
        
        int getDelay() const { return mouse.delayMs; }
        int getDuration() const { return mouse.durationMs; }
    };
    
    //==============================================================================
    /** Result of parsing, includes warnings. */
    struct ParseResult
    {
        Result result = Result::ok();
        StringArray warnings;
        
        bool wasOk() const { return result.wasOk(); }
        bool failed() const { return result.failed(); }
        String getErrorMessage() const { return result.getErrorMessage(); }
        
        static ParseResult ok() { return {}; }
        static ParseResult fail(const String& message) 
        { 
            ParseResult r; 
            r.result = Result::fail(message); 
            return r; 
        }
        void addWarning(const String& warning) { warnings.add(warning); }
    };
    
    //==============================================================================
    /** Parse JSON array of interactions.
     *
     *  Expands doubleClick to two clicks.
     *  Does NOT perform moveTo auto-insertion (that's done by InteractionTester).
     *
     *  @param jsonArray    The var containing the JSON array of interactions
     *  @param result       Output array to receive parsed interactions
     *  @return             ParseResult containing success/failure and any warnings
     */
    static ParseResult parseInteractions(const var& jsonArray, 
                                         Array<Interaction>& result);
    
    //==============================================================================
    // Validation limits
    static constexpr int MaxInteractions = 100;
    static constexpr int MaxDurationMs = 10000;      // 10 seconds
    static constexpr int SessionTimeoutMs = 20000;   // 20 seconds
    
private:
    //==============================================================================
    /** Parse a single interaction object (may add multiple to result for doubleClick). */
    static ParseResult parseSingleInteraction(const var& obj, 
                                              Array<Interaction>& result,
                                              int index);
    
    /** Expand doubleClick to two click interactions. */
    static ParseResult parseDoubleClick(const var& obj,
                                        Array<Interaction>& result,
                                        int index);
    
    /** Parse mouse interaction fields. */
    static ParseResult parseMouseInteraction(const var& obj,
                                             MouseInteraction& mouse,
                                             const String& type,
                                             int index);
    
    /** Parse position (normalized or absolute pixel). */
    static MouseInteraction::Position parsePosition(const var& obj);
    
    /** Parse modifier keys from JSON object. */
    static ModifierKeys parseModifiers(const var& obj);
    
    /** Helper to format error messages with context. */
    static String formatError(const String& message, int index, const String& field = {});
    
    /** Get string name for mouse interaction type. */
    static String getTypeName(MouseInteraction::Type type);
};

} // namespace hise
