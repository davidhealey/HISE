# HISE REST API for AI Agent Development

This document describes the REST API available in HISE for enabling AI agents to interact with script processors, enabling automated development workflows like editing scripts, compiling, and fixing errors in a tight feedback loop.

## Overview

- **Base URL**: `http://localhost:1900`
- **Response Format**: JSON with consistent structure
- **Common Fields**: All responses include `success` (boolean), `logs` (array), and `errors` (array)

### Response Structure

```json
{
  "success": true,
  "result": "...",
  "logs": ["console output line 1", "console output line 2"],
  "errors": [
    {
      "errorMessage": "Description of the error",
      "callstack": [
        "functionName() at Scripts/file.js:10:5",
        "onInit() at Interface.js:3:1"
      ]
    }
  ]
}
```

## Quick Start

1. **Discover available API methods** (optional but useful):
   ```bash
   curl "http://localhost:1900/"
   ```

2. **Check if HISE is running** and discover the project structure:
   ```bash
   curl "http://localhost:1900/api/status"
   ```

3. **Get current script** from a processor:
   ```bash
   curl "http://localhost:1900/api/get_script?moduleId=Interface&callback=onInit"
   ```

4. **Modify and compile**:
   ```bash
   curl -X POST "http://localhost:1900/api/set_script" \
     -H "Content-Type: application/json" \
     -d '{"moduleId": "Interface", "callback": "onInit", "script": "Console.print(\"Hello\");", "compile": true}'
   ```

5. **Check for errors** in the response's `errors` array, fix, and repeat.

---

## API Reference

### GET /

List all available API methods with their parameters and descriptions. This is the self-documenting discovery endpoint.

**Parameters**: None

**Example Request**:
```bash
curl "http://localhost:1900/"
```

**Example Response**:
```json
{
  "success": true,
  "methods": [
    {
      "path": "/api/get_script",
      "method": "GET",
      "category": "scripting",
      "description": "Read script content from a processor",
      "returns": "Script content for the specified callback (or all callbacks merged)",
      "queryParameters": [
        { "name": "moduleId", "description": "The script processor's module ID", "required": true },
        { "name": "callback", "description": "Specific callback name (e.g., onInit). If omitted, returns all callbacks merged.", "required": false }
      ]
    },
    {
      "path": "/api/set_script",
      "method": "POST",
      "category": "scripting",
      "description": "Update script content and optionally compile",
      "returns": "Compilation result with success status, logs, and errors",
      "bodyParameters": [
        { "name": "moduleId", "description": "The script processor's module ID", "required": true },
        { "name": "callback", "description": "Specific callback to update. If omitted, script is treated as merged content.", "required": false },
        { "name": "script", "description": "The script content", "required": true },
        { "name": "compile", "description": "Whether to compile after setting", "required": false, "defaultValue": "true" }
      ]
    }
  ],
  "logs": [],
  "errors": []
}
```

**Key Fields**:
| Field | Description |
|-------|-------------|
| `methods[].path` | The endpoint path (e.g., `/api/status`) |
| `methods[].method` | HTTP method (`GET` or `POST`) |
| `methods[].category` | Category: `status`, `scripting`, or `ui` |
| `methods[].description` | One-line description of the endpoint |
| `methods[].returns` | One-line description of what the endpoint returns |
| `methods[].queryParameters` | For GET requests: array of query parameter definitions |
| `methods[].bodyParameters` | For POST requests: array of JSON body parameter definitions |

**Use cases**:
- Discover all available endpoints programmatically
- Generate MCP tool definitions automatically
- Keep agent instructions in sync with the API

---

### GET /api/status

Discover the project structure, available script processors, and their state.

**Parameters**: None

**Example Request**:
```bash
curl "http://localhost:1900/api/status"
```

**Example Response**:
```json
{
  "success": true,
  "server": {
    "version": "1.0.0"
  },
  "project": {
    "name": "My Project",
    "projectFolder": "D:/Projects/MyPlugin",
    "scriptsFolder": "D:/Projects/MyPlugin/Scripts"
  },
  "scriptProcessors": [
    {
      "moduleId": "Interface",
      "isMainInterface": true,
      "externalFiles": ["utils.js", "ui/components.js"],
      "callbacks": [
        { "id": "onInit", "empty": false },
        { "id": "onNoteOn", "empty": true },
        { "id": "onNoteOff", "empty": true },
        { "id": "onController", "empty": true },
        { "id": "onTimer", "empty": true },
        { "id": "onControl", "empty": false }
      ]
    },
    {
      "moduleId": "Script FX1",
      "isMainInterface": false,
      "externalFiles": [],
      "callbacks": [
        { "id": "onInit", "empty": true },
        { "id": "prepareToPlay", "empty": true },
        { "id": "processBlock", "empty": false },
        { "id": "onControl", "empty": true }
      ]
    }
  ],
  "logs": [],
  "errors": []
}
```

**Key Fields**:
| Field | Description |
|-------|-------------|
| `project.scriptsFolder` | Root folder for external `.js` files |
| `scriptProcessors[].moduleId` | Unique identifier for the processor (use in other API calls) |
| `scriptProcessors[].isMainInterface` | `true` if this processor renders the plugin UI |
| `scriptProcessors[].externalFiles` | List of `include()` files (relative to scriptsFolder) |
| `scriptProcessors[].callbacks[].id` | Callback name (e.g., `onInit`, `onNoteOn`) |
| `scriptProcessors[].callbacks[].empty` | `true` if callback has no content |

---

### GET /api/get_script

Read script content from a processor.

**Parameters**:
| Parameter | Required | Description |
|-----------|----------|-------------|
| `moduleId` | Yes | The processor's module ID |
| `callback` | No | Specific callback name. If omitted, returns all callbacks merged. |

**Example: Get single callback**
```bash
curl "http://localhost:1900/api/get_script?moduleId=Interface&callback=onInit"
```

**Response**:
```json
{
  "success": true,
  "moduleId": "Interface",
  "callback": "onInit",
  "script": "Content.makeFrontInterface(600, 500);\nConsole.print(\"Hello\");",
  "logs": [],
  "errors": []
}
```

**Example: Get all callbacks (merged)**
```bash
curl "http://localhost:1900/api/get_script?moduleId=Interface"
```

**Response**:
```json
{
  "success": true,
  "moduleId": "Interface",
  "script": "Content.makeFrontInterface(600, 500);\n\nfunction onNoteOn()\n{\n\t\n}\n\nfunction onNoteOff()\n{\n\t\n}\n...",
  "logs": [],
  "errors": []
}
```

**Important Notes**:
- `onInit` returns raw content (no function wrapper) since it's the top-level code
- Other callbacks return the full function signature: `function onNoteOn() { ... }`
- When getting merged script, all callbacks are concatenated with the standard function wrappers

---

### POST /api/set_script

Update script content in a processor.

**JSON Body** (all parameters in request body):
| Field | Required | Default | Description |
|-------|----------|---------|-------------|
| `moduleId` | Yes | - | The processor's module ID |
| `callback` | No | - | Specific callback to update. If omitted, `script` is treated as merged content. |
| `script` | Yes | - | The script content |
| `compile` | No | `true` | Whether to compile after setting |

**Example: Set single callback**
```bash
curl -X POST "http://localhost:1900/api/set_script" \
  -H "Content-Type: application/json" \
  -d '{
    "moduleId": "Interface",
    "callback": "onInit",
    "script": "Content.makeFrontInterface(600, 500);\nConsole.print(123);",
    "compile": true
  }'
```

**Example: Set all callbacks (merged script)**
```bash
curl -X POST "http://localhost:1900/api/set_script" \
  -H "Content-Type: application/json" \
  -d '{
    "moduleId": "Interface",
    "script": "Content.makeFrontInterface(600, 500);\n\nfunction onNoteOn()\n{\n\tMessage.makeArtificial();\n}\n\nfunction onNoteOff()\n{\n}\n\nfunction onController()\n{\n}\n\nfunction onTimer()\n{\n}\n\nfunction onControl(number, value)\n{\n}",
    "compile": true
  }'
```

**Example: Set without compiling**
```bash
curl -X POST "http://localhost:1900/api/set_script" \
  -H "Content-Type: application/json" \
  -d '{
    "moduleId": "Interface",
    "callback": "onNoteOn",
    "script": "function onNoteOn()\n{\n\tConsole.print(Message.getNoteNumber());\n}",
    "compile": false
  }'
```

**Response (on successful compile)**:
```json
{
  "success": true,
  "result": "Recompiled OK",
  "logs": ["123"],
  "errors": []
}
```

**Response (on compile error)**:
```json
{
  "success": false,
  "result": "Compilation / Runtime Error",
  "logs": [],
  "errors": [
    {
      "errorMessage": "API call with undefined parameter 0",
      "callstack": [
        "myFunction() at Scripts/utils.js:15:8",
        "onInit() at Interface.js:3:1"
      ]
    }
  ]
}
```

---

### GET /api/recompile

Recompile a processor without changing its script content.

**Parameters**:
| Parameter | Required | Description |
|-----------|----------|-------------|
| `moduleId` | Yes | The processor's module ID |

**Example Request**:
```bash
curl "http://localhost:1900/api/recompile?moduleId=Interface"
```

**Response**: Same format as `set_script` compile response.

**When to use**:
- After editing external `.js` files directly on disk
- To re-run initialization code without changes
- To check current compile state

---

### GET /api/list_components

List all UI components in a script processor.

**Parameters**:
| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `moduleId` | Yes | - | The processor's module ID |
| `hierarchy` | No | `false` | If `true`, returns nested tree with layout properties |

**Example: Flat list (default)**
```bash
curl "http://localhost:1900/api/list_components?moduleId=Interface"
```

**Response**:
```json
{
  "success": true,
  "moduleId": "Interface",
  "components": [
    { "id": "Button1", "type": "ScriptButton" },
    { "id": "Panel1", "type": "ScriptPanel" },
    { "id": "Button2", "type": "ScriptButton" },
    { "id": "Knob1", "type": "ScriptSlider" }
  ],
  "logs": [],
  "errors": []
}
```

**Example: Hierarchical tree**
```bash
curl "http://localhost:1900/api/list_components?moduleId=Interface&hierarchy=true"
```

**Response**:
```json
{
  "success": true,
  "moduleId": "Interface",
  "components": [
    { 
      "id": "Button1", 
      "type": "ScriptButton",
      "visible": true,
      "enabled": false,
      "x": 159.0,
      "y": 88.0,
      "width": 128,
      "height": 28,
      "childComponents": []
    },
    { 
      "id": "Panel1", 
      "type": "ScriptPanel",
      "visible": true,
      "enabled": true,
      "x": 162.0,
      "y": 197.0,
      "width": 360.0,
      "height": 280.0,
      "childComponents": [
        { 
          "id": "Button2", 
          "type": "ScriptButton",
          "visible": true,
          "enabled": true,
          "x": 74.0,
          "y": 45.0,
          "width": 128,
          "height": 28,
          "childComponents": []
        }
      ]
    }
  ],
  "logs": [],
  "errors": []
}
```

**Note**: Component positions (`x`, `y`) are relative to their parent component. Root components are positioned relative to the interface origin.

**Use cases**:
| Format | Best for |
|--------|----------|
| Flat list (default) | Finding component IDs, simple enumeration |
| Hierarchy (`hierarchy=true`) | Debugging visibility issues, understanding layout structure |

---

### GET /api/get_component_properties

Returns all properties for a specific UI component.

**Parameters**:
| Parameter | Required | Description |
|-----------|----------|-------------|
| `moduleId` | Yes | The processor's module ID |
| `componentId` | Yes | The component's ID (e.g., `"Button1"`, `"Panel1"`) |

**Example Request**:
```bash
curl "http://localhost:1900/api/get_component_properties?moduleId=Interface&componentId=Button1"
```

**Response**:
```json
{
  "success": true,
  "moduleId": "Interface",
  "componentId": "Button1",
  "type": "ScriptButton",
  "properties": [
    { "id": "x", "value": 159, "isDefault": true },
    { "id": "y", "value": 88, "isDefault": true },
    { "id": "width", "value": 128, "isDefault": true },
    { "id": "height", "value": 28, "isDefault": true },
    { "id": "visible", "value": true, "isDefault": true },
    { "id": "enabled", "value": false, "isDefault": false },
    { "id": "saveInPreset", "value": true, "isDefault": true },
    { "id": "radioGroup", "value": 0, "isDefault": true },
    { "id": "bgColour", "value": "0x55FFFFFF", "isDefault": true },
    { "id": "itemColour", "value": "0xFD81D427", "isDefault": false },
    { "id": "textColour", "value": "0xFFFFFFFF", "isDefault": true },
    { 
      "id": "macroControl", 
      "value": -1, 
      "isDefault": true,
      "options": ["No MacroControl", "Macro 1", "Macro 2", "..."]
    }
  ],
  "logs": [],
  "errors": []
}
```

**Property fields**:
| Field | Description |
|-------|-------------|
| `id` | Property name (use this with `component.set(id, value)`) |
| `value` | Current value (native type: int, bool, string, or hex color string) |
| `isDefault` | `true` if value hasn't been explicitly changed from default |
| `options` | (optional) Available choices for enum-type properties |

**Use cases**:
- Discover correct property names (e.g., `bgColour` vs `colour`)
- Query component dimensions for layout calculations
- Verify property values after setting them
- Find available options for enum properties

---

## Development Workflows

### Recommended: API-Only Workflow

Use the API exclusively for script modifications. This approach ensures the script content in HISE is always in sync with what you're working on.

**Advantages**:
- Atomic operations (get, modify, set in one flow)
- No file sync issues
- Immediate feedback on compile errors
- Works for inline callbacks that aren't stored in external files

**Workflow Pseudocode**:
```
1. GET /api/status
   -> Store scriptProcessors list
   -> Identify target processor (usually isMainInterface: true)

2. GET /api/get_script?moduleId={target}&callback={callback}
   -> Get current script content

3. Analyze/modify the script content

4. POST /api/set_script?moduleId={target}
   body: { callback, script, compile: true }
   
5. Check response:
   IF success == true AND errors is empty:
      -> Done, or continue with next change
   ELSE:
      -> Parse errors[].errorMessage and errors[].callstack
      -> Fix the issue in script
      -> Go to step 4

REPEAT steps 2-5 for each change needed
```

**Example: Fix a bug in onInit**
```bash
# 1. Get current script
SCRIPT=$(curl -s "http://localhost:1900/api/get_script?moduleId=Interface&callback=onInit" | jq -r '.script')

# 2. Modify script (example: agent modifies $SCRIPT here)
MODIFIED_SCRIPT="..."

# 3. Set and compile (all params in JSON body)
curl -X POST "http://localhost:1900/api/set_script" \
  -H "Content-Type: application/json" \
  -d "{\"moduleId\": \"Interface\", \"callback\": \"onInit\", \"script\": \"$MODIFIED_SCRIPT\", \"compile\": true}"

# 4. Check response for errors, fix if needed, repeat
```

---

### Alternative: Direct File Editing

Edit `.js` files directly in the `scriptsFolder`, then trigger recompilation via the API.

**When to use**:
- Large refactoring across multiple files
- Working with external include files (listed in `externalFiles`)
- Using external tools (linters, formatters) on the files
- Version control workflows (git)

**Important**: External files are stored in the `scriptsFolder` path from `/api/status`. Inline callbacks (the code inside the processor itself) cannot be edited this way.

**Workflow Pseudocode**:
```
1. GET /api/status
   -> Get scriptsFolder path
   -> Get list of externalFiles for target processor

2. Read/modify files directly:
   {scriptsFolder}/{externalFile}

3. GET /api/recompile?moduleId={target}
   -> Triggers recompilation with updated file contents

4. Check response for errors:
   IF errors is not empty:
      -> Parse errors[].callstack for file:line:col
      -> Fix the file
      -> Go to step 3

REPEAT until no errors
```

**Example: Edit an external file**
```bash
# 1. Get project info
STATUS=$(curl -s "http://localhost:1900/api/status")
SCRIPTS_FOLDER=$(echo $STATUS | jq -r '.project.scriptsFolder')
# -> "D:/Projects/MyPlugin/Scripts"

# 2. Edit external file directly
# File: D:/Projects/MyPlugin/Scripts/utils.js
# (make changes with your preferred method)

# 3. Recompile
curl "http://localhost:1900/api/recompile?moduleId=Interface"

# 4. Check for errors in response
```

---

## Error Handling

### Error Response Structure

When compilation fails or a runtime error occurs, the `errors` array contains detailed information:

```json
{
  "errors": [
    {
      "errorMessage": "API call with undefined parameter 0",
      "callstack": [
        "helperFunction() at Scripts/utils.js:42:12",
        "initUI() at Scripts/ui.js:15:5",
        "onInit() at Interface.js:8:1"
      ]
    }
  ]
}
```

### Callstack Format

Each callstack entry follows the format:
```
functionName() at path:line:column
```

- `functionName` - The function where the error occurred (or was called from)
- `path` - Relative path from project root, or `ModuleId.js` for inline callbacks
- `line` - Line number (1-based)
- `column` - Column number (1-based)

### Error-Fixing Loop

```
WHILE errors is not empty:
    error = errors[0]
    location = parse callstack[0]  # Top of stack = error location
    
    IF location.path ends with ".js" AND exists in scriptsFolder:
        # External file - edit directly or via include
        Edit file at {scriptsFolder}/{location.path}
        GET /api/recompile?moduleId={moduleId}
    ELSE:
        # Inline callback
        callback = extract callback name from location.path (e.g., "onInit")
        GET /api/get_script?moduleId={moduleId}&callback={callback}
        Fix the error in returned script
        POST /api/set_script with fixed script
    
    Check new response for errors
```

---

## Debugging Techniques

### Querying Component Properties (Recommended)

Use `/api/get_component_properties` to discover property names and values without modifying scripts:

```bash
curl "http://localhost:1900/api/get_component_properties?moduleId=Interface&componentId=Button1"
```

This returns all properties with their correct names, current values, and available options - eliminating guesswork about property names like `bgColour` vs `colour`.

### Debugging Visibility Issues

Use `/api/list_components?hierarchy=true` to understand the component tree and check visibility:

```bash
curl "http://localhost:1900/api/list_components?moduleId=Interface&hierarchy=true"
```

This shows each component's `visible` and `enabled` state along with positions. If a component isn't visible, check:
1. Its own `visible` property
2. Parent component's `visible` property (traverse up the tree)
3. Position - is it within the parent's bounds?

### Querying Runtime Values

For dynamic values that can't be queried via the API, use `Console.print()`. The output appears in the `logs` array of the response:

```javascript
// In onInit - query dynamic values
Console.print(myArray.length);
Console.print(calculatedValue);
```

Response:
```json
{
  "success": true,
  "logs": ["5", "42"],
  "errors": []
}
```

### Asserting Conditions

Use `Console.assertWithMessage(condition, errorMessage)` to validate assumptions. If the condition is false, compilation fails with your custom message:

```javascript
// Validate expected state
Console.assertWithMessage(buttons.length == 5, "Expected 5 buttons but got " + buttons.length);
Console.assertWithMessage(panel.get("width") > 0, "Panel width must be positive");

// Verify calculations
var totalWidth = buttonCount * (buttonWidth + gap);
Console.assertWithMessage(totalWidth <= 600, "Buttons exceed interface width: " + totalWidth);
```

This is useful for:
- Catching configuration errors early
- Validating calculations before they cause layout issues
- Providing clear error messages instead of debugging unexpected behavior

---

## Best Practices

### 1. Always Start with /api/status

Before any operation, call `/api/status` to:
- Verify HISE is running and the API is available
- Discover all available script processors and their IDs
- Get the `scriptsFolder` path for direct file access
- Check which callbacks have content (`empty: false`)

### 2. Target the Right Processor

- Use `isMainInterface: true` for UI-related changes
- Check `externalFiles` to understand dependencies
- Different processor types have different callbacks (e.g., Script FX has `processBlock`)

### 3. Check Callback State Before Reading

Use the `empty` field from `/api/status` to know which callbacks have content:
- `empty: true` - Callback exists but has no code
- `empty: false` - Callback has code worth reading

### 4. Compile Frequently

Compile after each logical change rather than batching many changes:
- Easier to identify which change caused an error
- Faster feedback loop
- Prevents cascading errors

### 5. Use Appropriate Workflow

| Scenario | Recommended Approach |
|----------|---------------------|
| Fixing a bug in a callback | API-only workflow |
| Adding a new feature to onInit | API-only workflow |
| Refactoring shared utility functions | Direct file editing |
| Working with multiple include files | Direct file editing |
| Quick iteration on UI code | API-only workflow |

### 6. Handle the onInit Special Case

Remember that `onInit` content is returned raw (no function wrapper), while other callbacks are wrapped:
- `onInit`: `Console.print("hello");`
- `onNoteOn`: `function onNoteOn() { Console.print("hello"); }`

When setting `onInit`, provide raw code. When setting other callbacks, include the function wrapper.

### 7. Prefer Action Over Confirmation

When working iteratively:
- Try changes directly rather than asking for confirmation
- The error feedback loop is robust - mistakes are recoverable
- Use `Console.print()` to discover unknown values (check `logs` in response)
- Use `Console.assertWithMessage()` to validate assumptions

The compile → check errors → fix cycle is fast enough that trial-and-error is often more efficient than extensive upfront planning.

---

## Troubleshooting

### API Not Responding

- Ensure HISE is running
- Check that the REST server started (look for console message on HISE startup)
- Verify port 1900 is not blocked by firewall

### Module Not Found (404)

- Call `/api/status` to get the exact `moduleId` values
- Module IDs are case-sensitive
- The processor must exist in the current project

### Script Changes Not Taking Effect

- Ensure `compile: true` in `set_script` request (or call `recompile` after)
- Check the response for errors that may have prevented compilation
- For external files, make sure you're editing the right path from `scriptsFolder`

### Errors Keep Returning

- Check the full callstack, not just the error message
- The actual bug may be in a different file/function than where it manifests
- Look at `externalFiles` to understand the include chain
