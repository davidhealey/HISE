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
// Main parsing entry point
//==============================================================================

InteractionParser::ParseResult InteractionParser::parseInteractions(
    const var& jsonArray,
    Array<Interaction>& result)
{
    ParseResult parseResult;
    result.clear();
    
    if (!jsonArray.isArray())
        return ParseResult::fail("Input must be a JSON array of interactions");
    
    auto* arr = jsonArray.getArray();
    
    if (arr->isEmpty())
        return ParseResult::fail("Interaction array cannot be empty");
    
    if (arr->size() > MaxInteractions)
        return ParseResult::fail("Too many interactions: " + String(arr->size()) + 
                                 " exceeds maximum of " + String(MaxInteractions));
    
    for (int i = 0; i < arr->size(); i++)
    {
        auto singleResult = parseSingleInteraction((*arr)[i], result, i);
        
        if (singleResult.failed())
            return singleResult;
        
        for (auto& warning : singleResult.warnings)
            parseResult.addWarning(warning);
    }
    
    // Validate total sequence duration
    int totalDuration = 0;
    for (const auto& interaction : result)
    {
        totalDuration += interaction.getDelay() + interaction.getDuration();
    }
    
    if (totalDuration > SessionTimeoutMs)
    {
        parseResult.addWarning("Sequence duration (" + String(totalDuration) + 
            "ms) may exceed session timeout of " + String(SessionTimeoutMs) + "ms");
    }
    
    return parseResult;
}

//==============================================================================
// Single interaction parsing
//==============================================================================

InteractionParser::ParseResult InteractionParser::parseSingleInteraction(
    const var& obj,
    Array<Interaction>& result,
    int index)
{
    if (!obj.isObject())
        return ParseResult::fail(formatError("Must be a JSON object", index));
    
    if (!obj.hasProperty("type"))
        return ParseResult::fail(formatError("Missing required field 'type'", index));
    
    auto typeVar = obj["type"];
    if (!typeVar.isString())
        return ParseResult::fail(formatError("Field 'type' must be a string", index, "type"));
    
    String type = typeVar.toString().toLowerCase();
    
    // Handle doubleClick expansion (expands to 2 clicks)
    if (type == "doubleclick")
        return parseDoubleClick(obj, result, index);
    
    // Parse single interaction
    Interaction interaction;
    auto parseRes = parseMouseInteraction(obj, interaction.mouse, type, index);
    if (parseRes.failed())
        return parseRes;
    
    result.add(interaction);
    return parseRes;
}

//==============================================================================
// DoubleClick expansion
//==============================================================================

InteractionParser::ParseResult InteractionParser::parseDoubleClick(
    const var& obj,
    Array<Interaction>& result,
    int index)
{
    // doubleClick expands to: click, click (with 20ms delay between)
    // The moveTo will be auto-inserted by the normalizer
    
    // Parse target (required)
    if (!obj.hasProperty("target"))
        return ParseResult::fail(formatError("Missing required field 'target'", index));
    
    auto targetVar = obj["target"];
    if (!targetVar.isString())
        return ParseResult::fail(formatError("Field 'target' must be a string", index, "target"));
    
    String target = targetVar.toString();
    if (target.isEmpty())
        return ParseResult::fail(formatError("Field 'target' cannot be empty", index, "target"));
    
    // Parse position
    auto position = parsePosition(obj);
    
    // Parse modifiers
    ModifierKeys mods = parseModifiers(obj);
    
    // Parse delay (applies to first click)
    int delay = static_cast<int>(obj.getProperty("delay", 0));
    if (delay < 0)
        return ParseResult::fail(formatError("Field 'delay' cannot be negative", index, "delay"));
    
    // First click
    Interaction click1;
    click1.mouse.type = MouseInteraction::Type::Click;
    click1.mouse.targetComponentId = target;
    click1.mouse.position = position;
    click1.mouse.modifiers = mods;
    click1.mouse.delayMs = delay;
    click1.mouse.durationMs = InteractionDefaults::CLICK_DURATION_MS;
    result.add(click1);
    
    // Second click with short delay
    Interaction click2;
    click2.mouse.type = MouseInteraction::Type::Click;
    click2.mouse.targetComponentId = target;
    click2.mouse.position = position;
    click2.mouse.modifiers = mods;
    click2.mouse.delayMs = InteractionDefaults::DOUBLE_CLICK_DELAY_MS;
    click2.mouse.durationMs = InteractionDefaults::CLICK_DURATION_MS;
    result.add(click2);
    
    return ParseResult::ok();
}

//==============================================================================
// Mouse interaction parsing
//==============================================================================

InteractionParser::ParseResult InteractionParser::parseMouseInteraction(
    const var& obj,
    MouseInteraction& mouse,
    const String& type,
    int index)
{
    // Parse type
    if (type == "moveto")
        mouse.type = MouseInteraction::Type::MoveTo;
    else if (type == "click")
        mouse.type = MouseInteraction::Type::Click;
    else if (type == "drag")
        mouse.type = MouseInteraction::Type::Drag;
    else if (type == "screenshot")
        mouse.type = MouseInteraction::Type::Screenshot;
    else if (type == "selectmenuitem")
        mouse.type = MouseInteraction::Type::SelectMenuItem;
    else
        return ParseResult::fail(formatError(
            "Unknown type '" + type + "'. Valid types: moveTo, click, doubleClick, drag, screenshot, selectMenuItem", 
            index, "type"));
    
    //==========================================================================
    // Screenshot handling
    //==========================================================================
    if (mouse.type == MouseInteraction::Type::Screenshot)
    {
        if (!obj.hasProperty("id"))
            return ParseResult::fail(formatError("Missing required field 'id' for screenshot", index));
        
        auto idVar = obj["id"];
        if (!idVar.isString())
            return ParseResult::fail(formatError("Field 'id' must be a string", index, "id"));
        
        mouse.screenshotId = idVar.toString();
        if (mouse.screenshotId.isEmpty())
            return ParseResult::fail(formatError("Field 'id' cannot be empty", index, "id"));
        
        // Parse scale (optional, defaults to 1.0)
        mouse.screenshotScale = static_cast<float>(obj.getProperty("scale", 1.0f));
        if (mouse.screenshotScale != 0.5f && mouse.screenshotScale != 1.0f)
            return ParseResult::fail(formatError("Field 'scale' must be 0.5 or 1.0", index, "scale"));
        
        // Screenshot has no other fields
        return ParseResult::ok();
    }
    
    //==========================================================================
    // SelectMenuItem handling
    //==========================================================================
    if (mouse.type == MouseInteraction::Type::SelectMenuItem)
    {
        if (!obj.hasProperty("text"))
            return ParseResult::fail(formatError("Missing required field 'text' for selectMenuItem", index));
        
        auto textVar = obj["text"];
        if (!textVar.isString())
            return ParseResult::fail(formatError("Field 'text' must be a string", index, "text"));
        
        mouse.menuItemText = textVar.toString();
        if (mouse.menuItemText.isEmpty())
            return ParseResult::fail(formatError("Field 'text' cannot be empty", index, "text"));
        
        // Parse delay (optional)
        mouse.delayMs = static_cast<int>(obj.getProperty("delay", 0));
        if (mouse.delayMs < 0)
            return ParseResult::fail(formatError("Field 'delay' cannot be negative", index, "delay"));
        
        // Parse duration (optional)
        mouse.durationMs = static_cast<int>(obj.getProperty("duration", InteractionDefaults::MENU_SELECT_DURATION_MS));
        if (mouse.durationMs < 0)
            return ParseResult::fail(formatError("Field 'duration' cannot be negative", index, "duration"));
        if (mouse.durationMs > MaxDurationMs)
            return ParseResult::fail(formatError("Field 'duration' exceeds maximum of " + 
                String(MaxDurationMs) + "ms", index, "duration"));
        
        // SelectMenuItem has no target or position
        return ParseResult::ok();
    }
    
    //==========================================================================
    // Target-based interactions (moveTo, click, drag)
    //==========================================================================
    
    // Parse target (required)
    if (!obj.hasProperty("target"))
        return ParseResult::fail(formatError("Missing required field 'target'", index));
    
    auto targetVar = obj["target"];
    if (!targetVar.isString())
        return ParseResult::fail(formatError("Field 'target' must be a string", index, "target"));
    
    mouse.targetComponentId = targetVar.toString();
    if (mouse.targetComponentId.isEmpty())
        return ParseResult::fail(formatError("Field 'target' cannot be empty", index, "target"));
    
    // Parse position (optional, defaults to center)
    mouse.position = parsePosition(obj);
    
    // Parse delay (optional)
    mouse.delayMs = static_cast<int>(obj.getProperty("delay", 0));
    if (mouse.delayMs < 0)
        return ParseResult::fail(formatError("Field 'delay' cannot be negative", index, "delay"));
    
    //==========================================================================
    // Type-specific parsing
    //==========================================================================
    switch (mouse.type)
    {
        case MouseInteraction::Type::MoveTo:
        {
            mouse.durationMs = static_cast<int>(obj.getProperty("duration", InteractionDefaults::MOVE_DURATION_MS));
            break;
        }
        
        case MouseInteraction::Type::Click:
        {
            mouse.durationMs = static_cast<int>(obj.getProperty("duration", InteractionDefaults::CLICK_DURATION_MS));
            mouse.rightClick = static_cast<bool>(obj.getProperty("rightClick", false));
            mouse.modifiers = parseModifiers(obj);
            break;
        }
        
        case MouseInteraction::Type::Drag:
        {
            mouse.durationMs = static_cast<int>(obj.getProperty("duration", InteractionDefaults::DRAG_DURATION_MS));
            mouse.modifiers = parseModifiers(obj);
            
            // Delta required for drag
            if (!obj.hasProperty("delta"))
                return ParseResult::fail(formatError("Missing required field 'delta' for drag", index));
            
            auto deltaVar = obj["delta"];
            if (!deltaVar.isObject())
                return ParseResult::fail(formatError("Field 'delta' must be an object with 'x' and 'y'", index, "delta"));
            
            if (!deltaVar.hasProperty("x") || !deltaVar.hasProperty("y"))
                return ParseResult::fail(formatError("Field 'delta' must have both 'x' and 'y' properties", index, "delta"));
            
            mouse.deltaPixels = {
                static_cast<int>(deltaVar["x"]),
                static_cast<int>(deltaVar["y"])
            };
            break;
        }
        
        default:
            break;
    }
    
    // Validate duration
    if (mouse.durationMs < 0)
        return ParseResult::fail(formatError("Field 'duration' cannot be negative", index, "duration"));
    if (mouse.durationMs > MaxDurationMs)
        return ParseResult::fail(formatError("Field 'duration' exceeds maximum of " + 
            String(MaxDurationMs) + "ms", index, "duration"));
    
    return ParseResult::ok();
}

//==============================================================================
// Position parsing
//==============================================================================

InteractionParser::MouseInteraction::Position InteractionParser::parsePosition(const var& obj)
{
    // pixelPosition takes precedence over position
    if (obj.hasProperty("pixelPosition"))
    {
        auto px = obj["pixelPosition"];
        if (px.isObject() && px.hasProperty("x") && px.hasProperty("y"))
        {
            return MouseInteraction::Position::absolute(
                static_cast<int>(px["x"]),
                static_cast<int>(px["y"])
            );
        }
    }
    
    // Normalized position
    if (obj.hasProperty("position"))
    {
        auto pos = obj["position"];
        if (pos.isObject() && pos.hasProperty("x") && pos.hasProperty("y"))
        {
            return MouseInteraction::Position::normalized(
                static_cast<float>(pos["x"]),
                static_cast<float>(pos["y"])
            );
        }
    }
    
    // Default: center
    return MouseInteraction::Position::center();
}

//==============================================================================
// Modifier parsing
//==============================================================================

ModifierKeys InteractionParser::parseModifiers(const var& obj)
{
    int flags = 0;
    
    if (static_cast<bool>(obj.getProperty("shiftDown", false)))
        flags |= ModifierKeys::shiftModifier;
    if (static_cast<bool>(obj.getProperty("ctrlDown", false)))
        flags |= ModifierKeys::ctrlModifier;
    if (static_cast<bool>(obj.getProperty("altDown", false)))
        flags |= ModifierKeys::altModifier;
    if (static_cast<bool>(obj.getProperty("cmdDown", false)))
        flags |= ModifierKeys::commandModifier;
    
    return ModifierKeys(flags);
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
        case MouseInteraction::Type::MoveTo:        return "moveTo";
        case MouseInteraction::Type::Click:         return "click";
        case MouseInteraction::Type::Drag:          return "drag";
        case MouseInteraction::Type::Screenshot:    return "screenshot";
        case MouseInteraction::Type::SelectMenuItem: return "selectMenuItem";
    }
    return "unknown";
}

} // namespace hise
