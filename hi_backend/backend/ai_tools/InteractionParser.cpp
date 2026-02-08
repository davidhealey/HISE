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
        case MouseInteraction::Type::Screenshot:
            return mouse.timestampMs + 50;  // Instant actions have short duration
            
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
    
    // Check if input is an array
    if (!jsonArray.isArray())
    {
        return ParseResult::fail("Input must be a JSON array of interactions");
    }
    
    auto* arr = jsonArray.getArray();
    
    // Check for empty array
    if (arr->isEmpty())
    {
        return ParseResult::fail("Interaction array cannot be empty");
    }
    
    // Check for too many interactions
    if (arr->size() > MaxInteractions)
    {
        return ParseResult::fail("Too many interactions: " + String(arr->size()) + 
                                 " exceeds maximum of " + String(MaxInteractions));
    }
    
    // Parse each interaction
    for (int i = 0; i < arr->size(); i++)
    {
        Interaction interaction;
        auto singleResult = parseSingleInteraction((*arr)[i], interaction, i);
        
        if (singleResult.failed())
            return singleResult;
        
        // Collect warnings
        for (auto& warning : singleResult.warnings)
            parseResult.addWarning(warning);
        
        result.add(interaction);
    }
    
    // Validate the sequence (sorting, overlaps, MIDI pairing)
    auto sequenceResult = validateSequence(result, strict);
    
    if (sequenceResult.failed())
        return sequenceResult;
    
    // Collect sequence warnings
    for (auto& warning : sequenceResult.warnings)
        parseResult.addWarning(warning);
    
    // In strict mode, warnings become errors
    if (strict && parseResult.warnings.size() > 0)
    {
        return ParseResult::fail("Strict mode: " + parseResult.warnings[0]);
    }
    
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
    // Check that it's an object
    if (!obj.isObject())
    {
        return ParseResult::fail(formatError("Must be a JSON object", index));
    }
    
    // Get type field
    if (!obj.hasProperty("type"))
    {
        return ParseResult::fail(formatError("Missing required field 'type'", index));
    }
    
    auto typeVar = obj["type"];
    if (!typeVar.isString())
    {
        return ParseResult::fail(formatError("Field 'type' must be a string", index));
    }
    
    String type = typeVar.toString().toLowerCase();
    
    // Determine if it's a MIDI or mouse interaction
    if (type == "midi")
    {
        interaction.isMidi = true;
        return parseMidiInteraction(obj, interaction.midi, index);
    }
    else
    {
        interaction.isMidi = false;
        return parseMouseInteraction(obj, interaction.mouse, type, index);
    }
}

InteractionParser::ParseResult InteractionParser::parseMouseInteraction(
    const var& obj,
    MouseInteraction& mouse,
    const String& type,
    int index)
{
    // Parse type
    if (type == "click")
        mouse.type = MouseInteraction::Type::Click;
    else if (type == "doubleclick")
        mouse.type = MouseInteraction::Type::DoubleClick;
    else if (type == "drag")
        mouse.type = MouseInteraction::Type::Drag;
    else if (type == "hover")
        mouse.type = MouseInteraction::Type::Hover;
    else if (type == "move")
        mouse.type = MouseInteraction::Type::Move;
    else if (type == "exit")
        mouse.type = MouseInteraction::Type::Exit;
    else if (type == "screenshot")
        mouse.type = MouseInteraction::Type::Screenshot;
    else
    {
        return ParseResult::fail(formatError("Unknown interaction type '" + type + 
            "'. Valid types: click, doubleClick, drag, hover, move, exit, screenshot, midi", index, "type"));
    }
    
    // Screenshot type has different required fields
    if (mouse.type == MouseInteraction::Type::Screenshot)
    {
        // Parse id (required for screenshot)
        if (!obj.hasProperty("id"))
        {
            return ParseResult::fail(formatError("Missing required field 'id' for screenshot", index));
        }
        
        auto idVar = obj["id"];
        if (!idVar.isString())
        {
            return ParseResult::fail(formatError("Field 'id' must be a string", index, "id"));
        }
        
        mouse.screenshotId = idVar.toString();
        
        if (mouse.screenshotId.isEmpty())
        {
            return ParseResult::fail(formatError("Field 'id' cannot be empty", index, "id"));
        }
        
        // Parse timestamp (required)
        if (!obj.hasProperty("timestamp"))
        {
            return ParseResult::fail(formatError("Missing required field 'timestamp'", index));
        }
        
        int timestamp = (int)obj["timestamp"];
        if (timestamp < 0)
        {
            return ParseResult::fail(formatError("Field 'timestamp' cannot be negative (got " + 
                String(timestamp) + ")", index, "timestamp"));
        }
        mouse.timestampMs = timestamp;
        
        // Parse scale (optional, defaults to 1.0)
        mouse.screenshotScale = 1.0f;
        if (obj.hasProperty("scale"))
        {
            float scale = (float)obj["scale"];
            if (scale != 0.5f && scale != 1.0f)
            {
                return ParseResult::fail(formatError("Field 'scale' must be 0.5 or 1.0 (got " + 
                    String(scale) + ")", index, "scale"));
            }
            mouse.screenshotScale = scale;
        }
        
        // Screenshot doesn't need target or position fields
        return ParseResult::ok();
    }
    
    // Parse target (required for all mouse events except screenshot)
    if (!obj.hasProperty("target"))
    {
        return ParseResult::fail(formatError("Missing required field 'target' for mouse interaction", index));
    }
    
    auto targetVar = obj["target"];
    if (!targetVar.isString())
    {
        return ParseResult::fail(formatError("Field 'target' must be a string", index, "target"));
    }
    
    mouse.targetComponentId = targetVar.toString();
    
    if (mouse.targetComponentId.isEmpty())
    {
        return ParseResult::fail(formatError("Field 'target' cannot be empty", index, "target"));
    }
    
    // Parse timestamp (required)
    if (!obj.hasProperty("timestamp"))
    {
        return ParseResult::fail(formatError("Missing required field 'timestamp'", index));
    }
    
    int timestamp = (int)obj["timestamp"];
    if (timestamp < 0)
    {
        return ParseResult::fail(formatError("Field 'timestamp' cannot be negative (got " + 
            String(timestamp) + ")", index, "timestamp"));
    }
    mouse.timestampMs = timestamp;
    
    // Parse position/from field (optional, defaults to center)
    mouse.fromNormalized = {0.5f, 0.5f};
    mouse.toNormalized = {0.5f, 0.5f};
    
    if (obj.hasProperty("position"))
    {
        auto posResult = parsePosition(obj["position"], mouse.fromNormalized, "position", index);
        if (posResult.failed())
            return posResult;
    }
    else if (obj.hasProperty("from"))
    {
        auto posResult = parsePosition(obj["from"], mouse.fromNormalized, "from", index);
        if (posResult.failed())
            return posResult;
    }
    
    // Parse 'to' field (for drag/move)
    if (obj.hasProperty("to"))
    {
        auto posResult = parsePosition(obj["to"], mouse.toNormalized, "to", index);
        if (posResult.failed())
            return posResult;
    }
    
    // Validate move interaction requires explicit from/to (not position)
    if (mouse.type == MouseInteraction::Type::Move)
    {
        // Check if path is provided - path takes precedence over from/to
        bool hasPath = obj.hasProperty("path");
        
        if (!hasPath)
        {
            // If using position instead of from, provide helpful error
            if (obj.hasProperty("position") && !obj.hasProperty("from"))
            {
                return ParseResult::fail(formatError(
                    "Field 'move' requires 'from' and 'to' positions (not 'position'). "
                    "Use 'from' for start position and 'to' for end position.", index));
            }
            
            // Require both from and to for move without path
            if (!obj.hasProperty("from") || !obj.hasProperty("to"))
            {
                return ParseResult::fail(formatError(
                    "Field 'move' requires both 'from' and 'to' positions, or a 'path' array", index));
            }
        }
    }
    
    // Parse path for move (optional)
    if (obj.hasProperty("path"))
    {
        auto pathVar = obj["path"];
        if (!pathVar.isArray())
        {
            return ParseResult::fail(formatError("Field 'path' must be an array", index, "path"));
        }
        
        auto* pathArray = pathVar.getArray();
        
        if (pathArray->isEmpty())
        {
            return ParseResult::fail(formatError("Field 'path' cannot be empty", index, "path"));
        }
        
        if (pathArray->size() > MaxPathPoints)
        {
            return ParseResult::fail(formatError("Field 'path' has too many points (" + 
                String(pathArray->size()) + "), maximum is " + String(MaxPathPoints), index, "path"));
        }
        
        for (int i = 0; i < pathArray->size(); i++)
        {
            Point<float> point;
            auto pointResult = parsePosition((*pathArray)[i], point, "path[" + String(i) + "]", index);
            if (pointResult.failed())
                return pointResult;
            mouse.pathNormalized.add(point);
        }
    }
    
    // Parse duration (optional, has default)
    mouse.durationMs = 100;  // Default duration
    
    if (obj.hasProperty("duration"))
    {
        int duration = (int)obj["duration"];
        
        if (duration <= 0)
        {
            return ParseResult::fail(formatError("Field 'duration' must be positive (got " + 
                String(duration) + ")", index, "duration"));
        }
        
        if (duration > MaxDurationMs)
        {
            return ParseResult::fail(formatError("Field 'duration' exceeds maximum of " + 
                String(MaxDurationMs) + "ms (got " + String(duration) + "ms)", index, "duration"));
        }
        
        mouse.durationMs = duration;
    }
    
    // Parse modifiers (optional)
    mouse.rightClick = obj.getProperty("rightClick", false);
    mouse.shiftDown = obj.getProperty("shiftDown", false);
    mouse.ctrlDown = obj.getProperty("ctrlDown", false);
    mouse.altDown = obj.getProperty("altDown", false);
    mouse.cmdDown = obj.getProperty("cmdDown", false);
    
    return ParseResult::ok();
}

InteractionParser::ParseResult InteractionParser::parseMidiInteraction(
    const var& obj,
    MidiInteraction& midi,
    int index)
{
    // Parse timestamp (required)
    if (!obj.hasProperty("timestamp"))
    {
        return ParseResult::fail(formatError("Missing required field 'timestamp'", index));
    }
    
    int timestamp = (int)obj["timestamp"];
    if (timestamp < 0)
    {
        return ParseResult::fail(formatError("Field 'timestamp' cannot be negative (got " + 
            String(timestamp) + ")", index, "timestamp"));
    }
    midi.timestampMs = timestamp;
    
    // Count how many MIDI event types are present
    int eventTypeCount = 0;
    if (obj.hasProperty("noteOn")) eventTypeCount++;
    if (obj.hasProperty("noteOff")) eventTypeCount++;
    if (obj.hasProperty("cc")) eventTypeCount++;
    if (obj.hasProperty("pitchBend")) eventTypeCount++;
    
    if (eventTypeCount == 0)
    {
        return ParseResult::fail(formatError(
            "MIDI interaction must have one of: noteOn, noteOff, cc, pitchBend", index));
    }
    
    if (eventTypeCount > 1)
    {
        return ParseResult::fail(formatError(
            "MIDI interaction cannot have multiple event types (found " + 
            String(eventTypeCount) + ")", index));
    }
    
    // Parse the specific MIDI event type
    if (obj.hasProperty("noteOn"))
    {
        midi.type = MidiInteraction::Type::NoteOn;
        auto noteOnVar = obj["noteOn"];
        
        if (!noteOnVar.isObject())
        {
            return ParseResult::fail(formatError("Field 'noteOn' must be an object", index, "noteOn"));
        }
        
        // Note is required
        if (!noteOnVar.hasProperty("note"))
        {
            return ParseResult::fail(formatError("Field 'noteOn' requires 'note'", index, "noteOn"));
        }
        
        int note = (int)noteOnVar["note"];
        if (note < 0 || note > 127)
        {
            return ParseResult::fail(formatError("Note value " + String(note) + 
                " out of range (0-127)", index, "noteOn.note"));
        }
        midi.noteOrController = note;
        
        // Velocity is optional, defaults to 100
        midi.valueOrVelocity = 100;
        if (noteOnVar.hasProperty("velocity"))
        {
            int velocity = (int)noteOnVar["velocity"];
            if (velocity < 0 || velocity > 127)
            {
                return ParseResult::fail(formatError("Velocity value " + String(velocity) + 
                    " out of range (0-127)", index, "noteOn.velocity"));
            }
            midi.valueOrVelocity = velocity;
        }
        
        // Channel is optional, defaults to 1
        midi.channel = 1;
        if (noteOnVar.hasProperty("channel"))
        {
            int channel = (int)noteOnVar["channel"];
            if (channel < 1 || channel > 16)
            {
                return ParseResult::fail(formatError("Channel value " + String(channel) + 
                    " out of range (1-16)", index, "noteOn.channel"));
            }
            midi.channel = channel;
        }
    }
    else if (obj.hasProperty("noteOff"))
    {
        midi.type = MidiInteraction::Type::NoteOff;
        auto noteOffVar = obj["noteOff"];
        
        if (!noteOffVar.isObject())
        {
            return ParseResult::fail(formatError("Field 'noteOff' must be an object", index, "noteOff"));
        }
        
        // Note is required
        if (!noteOffVar.hasProperty("note"))
        {
            return ParseResult::fail(formatError("Field 'noteOff' requires 'note'", index, "noteOff"));
        }
        
        int note = (int)noteOffVar["note"];
        if (note < 0 || note > 127)
        {
            return ParseResult::fail(formatError("Note value " + String(note) + 
                " out of range (0-127)", index, "noteOff.note"));
        }
        midi.noteOrController = note;
        
        // Velocity defaults to 0 for noteOff
        midi.valueOrVelocity = 0;
        if (noteOffVar.hasProperty("velocity"))
        {
            int velocity = (int)noteOffVar["velocity"];
            if (velocity < 0 || velocity > 127)
            {
                return ParseResult::fail(formatError("Velocity value " + String(velocity) + 
                    " out of range (0-127)", index, "noteOff.velocity"));
            }
            midi.valueOrVelocity = velocity;
        }
        
        // Channel is optional, defaults to 1
        midi.channel = 1;
        if (noteOffVar.hasProperty("channel"))
        {
            int channel = (int)noteOffVar["channel"];
            if (channel < 1 || channel > 16)
            {
                return ParseResult::fail(formatError("Channel value " + String(channel) + 
                    " out of range (1-16)", index, "noteOff.channel"));
            }
            midi.channel = channel;
        }
    }
    else if (obj.hasProperty("cc"))
    {
        midi.type = MidiInteraction::Type::Controller;
        auto ccVar = obj["cc"];
        
        if (!ccVar.isObject())
        {
            return ParseResult::fail(formatError("Field 'cc' must be an object", index, "cc"));
        }
        
        // Controller is required
        if (!ccVar.hasProperty("controller"))
        {
            return ParseResult::fail(formatError("Field 'cc' requires 'controller'", index, "cc"));
        }
        
        int controller = (int)ccVar["controller"];
        if (controller < 0 || controller > 127)
        {
            return ParseResult::fail(formatError("Controller number " + String(controller) + 
                " out of range (0-127)", index, "cc.controller"));
        }
        midi.noteOrController = controller;
        
        // Value is required
        if (!ccVar.hasProperty("value"))
        {
            return ParseResult::fail(formatError("Field 'cc' requires 'value'", index, "cc"));
        }
        
        int value = (int)ccVar["value"];
        if (value < 0 || value > 127)
        {
            return ParseResult::fail(formatError("CC value " + String(value) + 
                " out of range (0-127)", index, "cc.value"));
        }
        midi.valueOrVelocity = value;
        
        // Channel is optional, defaults to 1
        midi.channel = 1;
        if (ccVar.hasProperty("channel"))
        {
            int channel = (int)ccVar["channel"];
            if (channel < 1 || channel > 16)
            {
                return ParseResult::fail(formatError("Channel value " + String(channel) + 
                    " out of range (1-16)", index, "cc.channel"));
            }
            midi.channel = channel;
        }
    }
    else if (obj.hasProperty("pitchBend"))
    {
        midi.type = MidiInteraction::Type::PitchBend;
        auto pbVar = obj["pitchBend"];
        
        if (!pbVar.isObject())
        {
            return ParseResult::fail(formatError("Field 'pitchBend' must be an object", index, "pitchBend"));
        }
        
        // Value is required
        if (!pbVar.hasProperty("value"))
        {
            return ParseResult::fail(formatError("Field 'pitchBend' requires 'value'", index, "pitchBend"));
        }
        
        int value = (int)pbVar["value"];
        if (value < 0 || value > 16383)
        {
            return ParseResult::fail(formatError("Pitch bend value " + String(value) + 
                " out of range (0-16383)", index, "pitchBend.value"));
        }
        midi.valueOrVelocity = value;
        midi.noteOrController = 0;  // Not used for pitch bend
        
        // Channel is optional, defaults to 1
        midi.channel = 1;
        if (pbVar.hasProperty("channel"))
        {
            int channel = (int)pbVar["channel"];
            if (channel < 1 || channel > 16)
            {
                return ParseResult::fail(formatError("Channel value " + String(channel) + 
                    " out of range (1-16)", index, "pitchBend.channel"));
            }
            midi.channel = channel;
        }
    }
    
    return ParseResult::ok();
}

InteractionParser::ParseResult InteractionParser::parsePosition(
    const var& obj,
    Point<float>& position,
    const String& fieldName,
    int index)
{
    if (!obj.isObject())
    {
        return ParseResult::fail(formatError("Field '" + fieldName + 
            "' must be an object with 'x' and 'y' properties", index, fieldName));
    }
    
    if (!obj.hasProperty("x") || !obj.hasProperty("y"))
    {
        return ParseResult::fail(formatError("Field '" + fieldName + 
            "' must have both 'x' and 'y' properties", index, fieldName));
    }
    
    float x = (float)obj["x"];
    float y = (float)obj["y"];
    
    // Note: We allow values outside [0,1] range to support drag operations
    // that extend beyond component bounds (e.g., dragging a slider thumb
    // past its visible range)
    
    position.x = x;
    position.y = y;
    
    return ParseResult::ok();
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
    
    // Check total sequence duration
    if (!interactions.isEmpty())
    {
        int lastEndTime = 0;
        for (auto& interaction : interactions)
        {
            int endTime = interaction.getEndTime();
            if (endTime > lastEndTime)
                lastEndTime = endTime;
        }
        
        if (lastEndTime > SessionTimeoutMs)
        {
            result.addWarning("Sequence duration (" + String(lastEndTime) + 
                "ms) exceeds session timeout of " + String(SessionTimeoutMs) + "ms");
        }
    }
    
    // In strict mode, warnings become errors (handled by caller)
    return result;
}

InteractionParser::ParseResult InteractionParser::checkMouseOverlaps(
    const Array<Interaction>& interactions)
{
    // Find all mouse interactions and check for overlaps
    // We need to check if any mouse interaction starts before another ends
    
    for (int i = 0; i < interactions.size(); i++)
    {
        if (interactions[i].isMidi)
            continue;  // MIDI events don't conflict with mouse
        
        int startTimeI = interactions[i].getTimestamp();
        int endTimeI = interactions[i].getEndTime();
        
        for (int j = i + 1; j < interactions.size(); j++)
        {
            if (interactions[j].isMidi)
                continue;  // MIDI events don't conflict
            
            int startTimeJ = interactions[j].getTimestamp();
            
            // If j starts before i ends, there's an overlap
            if (startTimeJ < endTimeI)
            {
                String typeI = getTypeName(interactions[i].mouse.type);
                String typeJ = getTypeName(interactions[j].mouse.type);
                
                return ParseResult::fail(
                    "Mouse interaction overlap detected: '" + typeJ + "' on '" + 
                    interactions[j].mouse.targetComponentId + "' at " + String(startTimeJ) + 
                    "ms starts during '" + typeI + "' on '" + 
                    interactions[i].mouse.targetComponentId + "' (ends at " + String(endTimeI) + 
                    "ms). Only one mouse interaction can be active at a time.");
            }
        }
    }
    
    return ParseResult::ok();
}

void InteractionParser::checkMidiPairing(
    const Array<Interaction>& interactions,
    ParseResult& result)
{
    // Track active notes per channel
    // Key: (channel << 8) | note
    std::set<int> activeNotes;
    
    for (auto& interaction : interactions)
    {
        if (!interaction.isMidi)
            continue;
        
        int channel = interaction.midi.channel;
        int note = interaction.midi.noteOrController;
        int key = (channel << 8) | note;
        
        if (interaction.midi.type == MidiInteraction::Type::NoteOn)
        {
            activeNotes.insert(key);
        }
        else if (interaction.midi.type == MidiInteraction::Type::NoteOff)
        {
            if (activeNotes.find(key) == activeNotes.end())
            {
                result.addWarning("NoteOff for note " + String(note) + " on channel " + 
                    String(channel) + " without matching NoteOn");
            }
            else
            {
                activeNotes.erase(key);
            }
        }
    }
    
    // Warn about hanging notes
    for (int key : activeNotes)
    {
        int channel = key >> 8;
        int note = key & 0xFF;
        result.addWarning("NoteOn for note " + String(note) + " on channel " + 
            String(channel) + " has no matching NoteOff (hanging note)");
    }
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
        case MouseInteraction::Type::Screenshot:  return "screenshot";
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
        case MouseInteraction::Type::Screenshot:
            return false;
    }
    return false;
}

int InteractionParser::getInteractionEndTime(const Interaction& interaction)
{
    return interaction.getEndTime();
}

} // namespace hise
