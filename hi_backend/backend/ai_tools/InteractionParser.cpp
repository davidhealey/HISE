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
// Interaction struct methods
//==============================================================================

int InteractionParser::Interaction::getEndTime() const
{
    if (isMidi)
        return midi.timestampMs;
    
    // For mouse interactions, end time depends on duration
    switch (mouse.type)
    {
        case MouseInteraction::Type::Click:
        case MouseInteraction::Type::DoubleClick:
        case MouseInteraction::Type::Exit:
            return mouse.timestampMs + 50;  // Click has short duration
            
        case MouseInteraction::Type::Hover:
        case MouseInteraction::Type::Move:
        case MouseInteraction::Type::Drag:
            return mouse.timestampMs + mouse.durationMs;
    }
    
    return mouse.timestampMs;
}

//==============================================================================
// Main parsing entry point
//==============================================================================

InteractionParser::ParseResult InteractionParser::parseInteractions(
    const var& jsonArray,
    Array<Interaction>& result,
    bool strict)
{
    ParseResult parseResult;
    result.clear();
    
    // TODO: Implement parsing
    // For now, return failure to make tests fail initially
    parseResult.result = Result::fail("Parser not implemented yet");
    
    return parseResult;
}

//==============================================================================
// Single interaction parsing
//==============================================================================

InteractionParser::ParseResult InteractionParser::parseSingleInteraction(
    const var& obj,
    Interaction& interaction,
    int index)
{
    // TODO: Implement
    return ParseResult::fail(formatError("Not implemented", index));
}

InteractionParser::ParseResult InteractionParser::parseMouseInteraction(
    const var& obj,
    MouseInteraction& mouse,
    const String& type,
    int index)
{
    // TODO: Implement
    return ParseResult::fail(formatError("Not implemented", index));
}

InteractionParser::ParseResult InteractionParser::parseMidiInteraction(
    const var& obj,
    MidiInteraction& midi,
    int index)
{
    // TODO: Implement
    return ParseResult::fail(formatError("Not implemented", index));
}

InteractionParser::ParseResult InteractionParser::parsePosition(
    const var& obj,
    Point<float>& position,
    const String& fieldName,
    int index)
{
    // TODO: Implement
    return ParseResult::fail(formatError("Not implemented", index, fieldName));
}

//==============================================================================
// Sequence validation
//==============================================================================

InteractionParser::ParseResult InteractionParser::validateSequence(
    Array<Interaction>& interactions,
    bool strict)
{
    ParseResult result;
    
    // Sort by timestamp first
    sortByTimestamp(interactions, result);
    
    // Check for mouse overlaps
    auto overlapResult = checkMouseOverlaps(interactions);
    if (overlapResult.failed())
        return overlapResult;
    
    // Check MIDI pairing (adds warnings)
    checkMidiPairing(interactions, result);
    
    // In strict mode, warnings become errors
    if (strict && result.warnings.size() > 0)
    {
        return ParseResult::fail("Strict mode: " + result.warnings[0]);
    }
    
    return result;
}

InteractionParser::ParseResult InteractionParser::checkMouseOverlaps(
    const Array<Interaction>& interactions)
{
    // TODO: Implement overlap detection
    return ParseResult::ok();
}

void InteractionParser::checkMidiPairing(
    const Array<Interaction>& interactions,
    ParseResult& result)
{
    // TODO: Implement MIDI pairing checks
}

bool InteractionParser::sortByTimestamp(
    Array<Interaction>& interactions,
    ParseResult& result)
{
    // Check if already sorted
    bool wasSorted = true;
    for (int i = 1; i < interactions.size(); i++)
    {
        if (interactions[i].getTimestamp() < interactions[i-1].getTimestamp())
        {
            wasSorted = false;
            break;
        }
    }
    
    if (!wasSorted)
    {
        result.addWarning("Interactions were not in timestamp order and have been auto-sorted");
        
        // Sort by timestamp
        std::sort(interactions.begin(), interactions.end(),
            [](const Interaction& a, const Interaction& b)
            {
                return a.getTimestamp() < b.getTimestamp();
            });
    }
    
    return wasSorted;
}

//==============================================================================
// Helper methods
//==============================================================================

String InteractionParser::formatError(const String& message, int index, const String& field)
{
    String error = "Interaction [" + String(index) + "]";
    
    if (field.isNotEmpty())
        error += " field '" + field + "'";
    
    error += ": " + message;
    
    return error;
}

String InteractionParser::getTypeName(MouseInteraction::Type type)
{
    switch (type)
    {
        case MouseInteraction::Type::Hover:       return "hover";
        case MouseInteraction::Type::Move:        return "move";
        case MouseInteraction::Type::Click:       return "click";
        case MouseInteraction::Type::DoubleClick: return "doubleClick";
        case MouseInteraction::Type::Drag:        return "drag";
        case MouseInteraction::Type::Exit:        return "exit";
    }
    return "unknown";
}

bool InteractionParser::requiresMouseButton(MouseInteraction::Type type)
{
    switch (type)
    {
        case MouseInteraction::Type::Click:
        case MouseInteraction::Type::DoubleClick:
        case MouseInteraction::Type::Drag:
            return true;
            
        case MouseInteraction::Type::Hover:
        case MouseInteraction::Type::Move:
        case MouseInteraction::Type::Exit:
            return false;
    }
    return false;
}

int InteractionParser::getInteractionEndTime(const Interaction& interaction)
{
    return interaction.getEndTime();
}

} // namespace hise
