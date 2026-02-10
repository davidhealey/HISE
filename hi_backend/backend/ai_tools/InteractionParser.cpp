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
    click1.mouse.target = ComponentTargetPath::fromVar(obj);
    click1.mouse.position = position;
    click1.mouse.modifiers = mods;
    click1.mouse.delayMs = delay;
    click1.mouse.durationMs = InteractionDefaults::CLICK_DURATION_MS;
    result.add(click1);
    
    // Second click with short delay
    Interaction click2;
    click2.mouse.type = MouseInteraction::Type::Click;
    click2.mouse.target = ComponentTargetPath::fromVar(obj);
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
        if (!obj.hasProperty("menuItemText"))
            return ParseResult::fail(formatError("Missing required field 'menuItemText' for selectMenuItem", index));
        
        auto textVar = obj["menuItemText"];
        if (!textVar.isString())
            return ParseResult::fail(formatError("Field 'menuItemText' must be a string", index, "menuItemText"));
        
        mouse.menuItemText = textVar.toString();
        if (mouse.menuItemText.isEmpty())
            return ParseResult::fail(formatError("Field 'menuItemText' cannot be empty", index, "menuItemText"));
        
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
    
    // Parse target (required) and optional subtarget
    if (!obj.hasProperty("target"))
        return ParseResult::fail(formatError("Missing required field 'target'", index));
    
    auto targetVar = obj["target"];
    if (!targetVar.isString())
        return ParseResult::fail(formatError("Field 'target' must be a string", index, "target"));
    
    mouse.target = ComponentTargetPath::fromVar(obj);
    if (mouse.target.componentId.isEmpty())
        return ParseResult::fail(formatError("Field 'target' cannot be empty", index, "target"));
    
    // Parse position (optional, defaults to center)
    mouse.position = parsePosition(obj);
    
    // Parse normalizedPositionInTarget (optional, for subtarget fallback)
    if (obj.hasProperty("normalizedPositionInTarget"))
    {
        auto normPos = obj["normalizedPositionInTarget"];
        if (normPos.isObject() && normPos.hasProperty("x") && normPos.hasProperty("y"))
        {
            mouse.position.normalizedInParent = Point<float>(
                static_cast<float>(normPos["x"]),
                static_cast<float>(normPos["y"])
            );
        }
    }
    
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
    if (obj.hasProperty("normalizedPosition"))
    {
        auto pos = obj["normalizedPosition"];
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

//==============================================================================
// InteractionAnalyzer implementation
//==============================================================================

InteractionAnalyzer::RecordedPosition InteractionAnalyzer::RecordedPosition::fromVar(const var& obj)
{
    RecordedPosition pos;
    
    pos.absolute = Point<int>(
        static_cast<int>(obj.getProperty("x", 0)),
        static_cast<int>(obj.getProperty("y", 0))
    );
    
    if (obj.hasProperty("targetBounds"))
    {
        auto bounds = obj["targetBounds"];
        if (bounds.isArray() && bounds.size() >= 4)
        {
            pos.targetBounds = Rectangle<int>(
                static_cast<int>(bounds[0]),
                static_cast<int>(bounds[1]),
                static_cast<int>(bounds[2]),
                static_cast<int>(bounds[3])
            );
        }
    }
    
    if (obj.hasProperty("normalizedPositionInTarget"))
    {
        auto normPos = obj["normalizedPositionInTarget"];
        if (normPos.isObject())
        {
            pos.normalizedInParent = Point<float>(
                static_cast<float>(normPos.getProperty("x", -1.f)),
                static_cast<float>(normPos.getProperty("y", -1.f))
            );
        }
    }
    
    return pos;
}

void InteractionAnalyzer::RecordedPosition::toInteractionVar(DynamicObject* obj, PositionMode mode) const
{
    if (targetBounds.isEmpty())
    {
        // No component - use absolute coordinates
        DynamicObject::Ptr pos = new DynamicObject();
        pos->setProperty("x", absolute.x);
        pos->setProperty("y", absolute.y);
        obj->setProperty("pixelPosition", var(pos.get()));
    }
    else
    {
        // Component available - use relative coordinates
        Point<int> relPos = getRelativePosition();
        
        if (mode == PositionMode::Absolute)
        {
            DynamicObject::Ptr pos = new DynamicObject();
            pos->setProperty("x", relPos.x);
            pos->setProperty("y", relPos.y);
            obj->setProperty("pixelPosition", var(pos.get()));
        }
        else
        {
            // Normalized mode
            float normX = static_cast<float>(relPos.x) / targetBounds.getWidth();
            float normY = static_cast<float>(relPos.y) / targetBounds.getHeight();
            
            DynamicObject::Ptr pos = new DynamicObject();
            pos->setProperty("x", normX);
            pos->setProperty("y", normY);
            obj->setProperty("normalizedPosition", var(pos.get()));
        }
    }
    
    // Write fallback position if available
    if (hasFallback())
    {
        DynamicObject::Ptr normPos = new DynamicObject();
        normPos->setProperty("x", normalizedInParent.x);
        normPos->setProperty("y", normalizedInParent.y);
        obj->setProperty("normalizedPositionInTarget", var(normPos.get()));
    }
}

InteractionAnalyzer::RawEvent InteractionAnalyzer::RawEvent::fromVar(const var& obj)
{
    RawEvent event;
    
    event.type = obj.getProperty("type", "").toString();
    event.position = RecordedPosition::fromVar(obj);
    event.timestamp = static_cast<int64>(obj.getProperty("timestamp", 0));
    event.rightClick = static_cast<bool>(obj.getProperty("rightClick", false));
    
    // Parse modifiers if present
    if (obj.hasProperty("modifiers"))
    {
        auto mods = obj["modifiers"];
        int flags = 0;
        
        if (static_cast<bool>(mods.getProperty("shiftDown", false)))
            flags |= ModifierKeys::shiftModifier;
        if (static_cast<bool>(mods.getProperty("ctrlDown", false)))
            flags |= ModifierKeys::ctrlModifier;
        if (static_cast<bool>(mods.getProperty("altDown", false)))
            flags |= ModifierKeys::altModifier;
        if (static_cast<bool>(mods.getProperty("cmdDown", false)))
            flags |= ModifierKeys::commandModifier;
        
        event.modifiers = ModifierKeys(flags);
    }
    
    // Parse pre-resolved target info if present
    event.target = ComponentTargetPath::fromVar(obj);
    
    // Parse menu selection info if present (for "selectMenuItem" type)
    event.menuItemId = static_cast<int>(obj.getProperty("itemId", -1));
    event.menuItemText = obj.getProperty("menuItemText", "").toString();
    
    return event;
}

Array<InteractionAnalyzer::RawEvent> InteractionAnalyzer::parseRawEvents(const Array<var>& rawEventVars)
{
    Array<RawEvent> events;
    events.ensureStorageAllocated(rawEventVars.size());
    
    for (const auto& eventVar : rawEventVars)
    {
        events.add(RawEvent::fromVar(eventVar));
    }
    
    return events;
}

void InteractionAnalyzer::attachTargetInfo(Array<RawEvent>& events, InteractionExecutorBase& executor)
{
    for (auto& event : events)
    {
        auto resolveResult = executor.resolvePosition(event.position.absolute);
        
        if (resolveResult.target.isEmpty())
        {
            // No component at position - click on background
            event.target = ComponentTargetPath("content");
            event.position.targetBounds = {};
        }
        else
        {
            event.target = resolveResult.target;
            event.position.targetBounds = resolveResult.componentBounds;
            
            // Compute normalized fallback position if this is a subtarget
            if (resolveResult.target.hasSubtarget())
            {
                // Resolve parent-only to get parent bounds
                ComponentTargetPath parentOnly(resolveResult.target.componentId);
                auto parentResult = executor.resolveTarget(parentOnly);
                
                if (parentResult.success() && !parentResult.componentBounds.isEmpty())
                {
                    float normX = (event.position.absolute.x - parentResult.componentBounds.getX()) 
                                / static_cast<float>(parentResult.componentBounds.getWidth());
                    float normY = (event.position.absolute.y - parentResult.componentBounds.getY()) 
                                / static_cast<float>(parentResult.componentBounds.getHeight());
                    
                    event.position.normalizedInParent = {normX, normY};
                }
            }
        }
    }
}

InteractionAnalyzer::AnalyzeResult InteractionAnalyzer::analyze(
    const Array<RawEvent>& rawEvents,
    const InteractionExecutorBase& executor,
    const Options& options)
{
    ignoreUnused(executor);  // Targets are now pre-attached via attachTargetInfo()
    
    AnalyzeResult result;
    Array<var> interactions;
    GestureState state;
    
    for (const auto& event : rawEvents)
    {
        // Handle selectMenuItem events - pass through directly (already semantic)
        if (event.type == "selectMenuItem")
        {
            DynamicObject::Ptr obj = new DynamicObject();
            obj->setProperty("type", "selectMenuItem");
            obj->setProperty("itemId", event.menuItemId);
            if (event.menuItemText.isNotEmpty())
                obj->setProperty("menuItemText", event.menuItemText);
            interactions.add(var(obj.get()));
            continue;
        }
        
        if (event.type == "mouseDown")
        {
            // Validate that target info was attached (via recording or attachTargetInfo())
            if (event.target.isEmpty())
            {
                result.warnings.add("mouseDown at (" + String(event.position.absolute.x) + ", " + 
                                   String(event.position.absolute.y) + ") has no target info - call attachTargetInfo() first");
            }
            
            state.mouseDown = true;
            state.downPosition = event.position;
            state.downTimestamp = event.timestamp;
            state.downRightClick = event.rightClick;
            state.downModifiers = event.modifiers;
            state.downTarget = event.target;
        }
        else if (event.type == "mouseUp")
        {
            if (!state.mouseDown)
            {
                // mouseUp without mouseDown - skip
                result.warnings.add("mouseUp without matching mouseDown at timestamp " + 
                                   String(event.timestamp));
                continue;
            }
            
            // Calculate movement distance
            int dx = event.position.absolute.x - state.downPosition.absolute.x;
            int dy = event.position.absolute.y - state.downPosition.absolute.y;
            int distance = static_cast<int>(std::sqrt(dx * dx + dy * dy));
            
            if (distance < options.clickMaxMovementPx)
            {
                // It's a click - check for double-click
                bool isDoubleClick = false;
                
                if (state.lastClickTimestamp >= 0)
                {
                    int64 timeSinceLastClick = state.downTimestamp - state.lastClickTimestamp;
                    int lastClickDx = state.downPosition.absolute.x - state.lastClickPosition.x;
                    int lastClickDy = state.downPosition.absolute.y - state.lastClickPosition.y;
                    int lastClickDist = static_cast<int>(std::sqrt(lastClickDx * lastClickDx + lastClickDy * lastClickDy));
                    
                    if (timeSinceLastClick <= options.doubleClickThresholdMs &&
                        lastClickDist < options.clickMaxMovementPx &&
                        state.lastClickTarget == state.downTarget)
                    {
                        isDoubleClick = true;
                    }
                }
                
                if (isDoubleClick)
                {
                    // Remove the previous click and replace with doubleClick
                    if (interactions.size() > 0)
                    {
                        interactions.removeLast();
                    }
                    interactions.add(createDoubleClickInteraction(state, options));
                    
                    // Reset last click tracking (can't triple-click)
                    state.lastClickTimestamp = -1000;
                    state.lastClickPosition = {};
                    state.lastClickTarget.clear();
                }
                else
                {
                    // Single click
                    interactions.add(createClickInteraction(state, options));
                    
                    // Track for potential double-click
                    state.lastClickTimestamp = state.downTimestamp;
                    state.lastClickPosition = state.downPosition.absolute;
                    state.lastClickTarget = state.downTarget;
                }
            }
            else
            {
                // It's a drag
                interactions.add(createDragInteraction(state, event.position.absolute, options));
                
                // Reset last click tracking (drag breaks double-click)
                state.lastClickTimestamp = -1000;
                state.lastClickPosition = {};
                state.lastClickTarget.clear();
            }
            
            state.reset();
        }
        // mouseMove events are ignored for semantic analysis
    }
    
    // Handle case where recording ended with mouse still down
    if (state.mouseDown)
    {
        result.warnings.add("Recording ended with mouse still down");
    }
    
    result.interactions = interactions;
    return result;
}

var InteractionAnalyzer::createClickInteraction(const GestureState& state,
                                                 const Options& options)
{
    DynamicObject::Ptr obj = new DynamicObject();
    
    obj->setProperty("type", "click");
    state.downTarget.toVar(obj.get());
    state.downPosition.toInteractionVar(obj.get(), options.positionMode);
    
    if (state.downRightClick)
    {
        obj->setProperty("rightClick", true);
    }
    
    addModifiersToInteraction(obj.get(), state.downModifiers);
    
    return var(obj.get());
}

var InteractionAnalyzer::createDoubleClickInteraction(const GestureState& state,
                                                       const Options& options)
{
    DynamicObject::Ptr obj = new DynamicObject();
    
    obj->setProperty("type", "doubleClick");
    state.downTarget.toVar(obj.get());
    state.downPosition.toInteractionVar(obj.get(), options.positionMode);
    
    addModifiersToInteraction(obj.get(), state.downModifiers);
    
    return var(obj.get());
}

var InteractionAnalyzer::createDragInteraction(const GestureState& state,
                                                Point<int> endPosition,
                                                const Options& options)
{
    DynamicObject::Ptr obj = new DynamicObject();
    
    obj->setProperty("type", "drag");
    state.downTarget.toVar(obj.get());
    state.downPosition.toInteractionVar(obj.get(), options.positionMode);
    
    // Delta
    DynamicObject::Ptr delta = new DynamicObject();
    delta->setProperty("x", endPosition.x - state.downPosition.absolute.x);
    delta->setProperty("y", endPosition.y - state.downPosition.absolute.y);
    obj->setProperty("delta", var(delta.get()));
    
    addModifiersToInteraction(obj.get(), state.downModifiers);
    
    return var(obj.get());
}

void InteractionAnalyzer::addPositionToInteraction(DynamicObject* obj,
                                                    Point<int> position,
                                                    Rectangle<int> componentBounds,
                                                    PositionMode mode)
{
    if (componentBounds.isEmpty())
        return;
    
    // Calculate position relative to component
    int relX = position.x - componentBounds.getX();
    int relY = position.y - componentBounds.getY();
    
    if (mode == PositionMode::Absolute)
    {
        DynamicObject::Ptr pos = new DynamicObject();
        pos->setProperty("x", relX);
        pos->setProperty("y", relY);
        obj->setProperty("pixelPosition", var(pos.get()));
    }
    else
    {
        // Normalized
        float normX = static_cast<float>(relX) / componentBounds.getWidth();
        float normY = static_cast<float>(relY) / componentBounds.getHeight();
        
        DynamicObject::Ptr pos = new DynamicObject();
        pos->setProperty("x", normX);
        pos->setProperty("y", normY);
        obj->setProperty("normalizedPosition", var(pos.get()));
    }
}

void InteractionAnalyzer::addModifiersToInteraction(DynamicObject* obj,
                                                     ModifierKeys mods)
{
    // Only add modifiers that are active (non-default = non-noisy)
    if (mods.isShiftDown())
        obj->setProperty("shiftDown", true);
    if (mods.isCtrlDown())
        obj->setProperty("ctrlDown", true);
    if (mods.isAltDown())
        obj->setProperty("altDown", true);
    if (mods.isCommandDown())
        obj->setProperty("cmdDown", true);
}

} // namespace hise
