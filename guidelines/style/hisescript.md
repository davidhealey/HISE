# HISEScript Coding Style & Quirks

A living document of HISEScript language quirks and best practices discovered during development.

**Related Documentation:**
- [../api/rest-api.md](../api/rest-api.md) - REST API for scripting automation (references this guide)

---

## Variable Declarations in Inline Functions

**Issue:** Cannot use `var` inside inline functions.

**Error message:**
```
Can't declare var statement in inline function
```

**Solution:** Use `local` instead of `var`:

```javascript
// WRONG - causes compile error
inline function onKnobChanged(component, value)
{
    var newY = 50 + (value * 250);  // Error!
    Knob3.set("y", newY);
}

// CORRECT - use 'local' for variables inside inline functions
inline function onKnobChanged(component, value)
{
    local newY = 50 + (value * 250);  // Works
    Knob3.set("y", newY);
}
```

**Why:** Inline functions have stricter scoping rules for performance reasons (they can be called in the audio thread). The `local` keyword ensures the variable is stack-allocated and doesn't escape the function scope.

---

## Triggering Callbacks Programmatically

**Issue:** Calling `setValue()` on a component does NOT automatically trigger its control callback.

**Solution:** Call `changed()` after `setValue()` to trigger the callback:

```javascript
// This sets the value but does NOT trigger Knob2's callback
Knob2.setValue(0.5);

// This sets the value AND triggers Knob2's callback
Knob2.setValue(0.5);
Knob2.changed();
```

**Note:** During `onInit`, HISE may intentionally skip `changed()` callbacks with a console message:
```
Skipping changed() callback during onInit for Knob2
```

This is a safety feature to prevent callback chains during initialization.

---

## Variable Declaration Keywords

HISEScript has several variable declaration keywords with different behaviors:

| Keyword | Scope | Use Case |
|---------|-------|----------|
| `var` | Function/global scope | General variables in regular functions or top-level |
| `local` | Block scope (stack) | Variables inside inline functions |
| `const var` | Immutable reference | Component references, constants |
| `reg` | Register variable | Performance-critical audio code |

**Best practice for component references:**
```javascript
// Use const var for component references (they shouldn't change)
const var Knob1 = Content.addKnob("Knob1", 50, 50);
const var Knob2 = Content.addKnob("Knob2", 200, 50);

// Use const var when getting existing components too
const var MyButton = Content.getComponent("MyButton");
```

---

## Inline Functions for Callbacks

Control callbacks **must** be inline functions due to realtime performance constraints:

```javascript
// CORRECT - inline function for callback
inline function onKnobChanged(component, value)
{
    Console.print(component.get("id") + " changed to: " + value);
}

Knob1.setControlCallback(onKnobChanged);

// WRONG - regular function (may cause audio glitches)
function onKnobChanged(component, value)
{
    Console.print(component.get("id") + " changed to: " + value);
}
```

**Why:** Inline functions are optimized for the audio thread and won't cause priority inversion or blocking issues.

---

## Callback Parameters

Control callbacks receive two parameters:

```javascript
inline function onMyCallback(component, value)
{
    // component - reference to the component that triggered the callback
    // value - the new value of the component
    
    // You can get the component's ID
    local id = component.get("id");
    
    // The value parameter equals component.getValue()
    Console.print(value == component.getValue());  // true
}
```

---

*This document will be expanded as more quirks are discovered during development.*
