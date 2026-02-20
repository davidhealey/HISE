# MCP Server LAF Integration Guide

This document explains how the HISE REST API integrates with LookAndFeel (LAF) objects and how an MCP server should use this information.

**Related Documentation:**
- [rest-api.md](rest-api.md) - Full REST API specification
- [laf-extraction.md](../../tools/api generator/doc_builders/laf-extraction.md) - Technical procedure for extracting LAF function documentation (in api_generator submodule)

## Overview

The LAF registry system tracks custom LookAndFeel objects and their component assignments. This enables an LLM-based coding assistant to:

1. Know which components have custom LAF styling
2. Wait for LAF rendering to complete after compilation to catch runtime errors in paint routines
3. Get LAF metadata for understanding the styling setup
4. Navigate to LAF source code locations

## LAF Info in `list_components` Response

Each component in the `/api/list_components` response includes a `laf` property:

```json
{
  "id": "FilterKnob",
  "type": "ScriptSlider",
  "laf": {
    "id": "LafNamespace.knobLaf",
    "renderStyle": "script",
    "location": "Scripts/Interface.js:15:12",
    "cssLocation": ""
  }
}
```

### LAF Properties

| Property | Description |
|----------|-------------|
| `id` | Variable name of the LAF object (e.g., `"laf"` or `"MyNamespace.buttonLaf"`) |
| `renderStyle` | One of: `"script"`, `"css"`, `"css_inline"`, `"mixed"` |
| `location` | Source location of `createLocalLookAndFeel()` call in `file:line:col` format |
| `cssLocation` | Full path to external CSS file (empty for inline CSS or script-only) |

### Render Styles

| Style | Description |
|-------|-------------|
| `script` | Uses `laf.registerFunction()` only - paint routines are JavaScript |
| `css` | Uses external CSS file only |
| `css_inline` | Uses `laf.setInlineStyleSheet()` only |
| `mixed` | Combination of script functions and CSS |

If no LAF is assigned to a component: `"laf": false`

## Location String Format

LAF location strings use the same format as error callstacks:

```
Scripts/MyScript.js:42:8
Interface.js:15:12
```

Format: `relativePath:lineNumber:columnNumber`

- For inline callbacks (onInit, onControl, etc.): Uses `moduleId.js` as the filename
- For external script files: Uses the relative path from the Scripts folder

This format is consistent with error locations in the `errors` array, allowing unified code navigation.

## LAF Render Warning

When compiling via `/api/set_script` or `/api/recompile`, the server waits up to 1 second for visible script-based LAF components to render. If any components haven't rendered, a warning is included:

```json
{
  "success": true,
  "result": "Compiled OK",
  "lafRenderWarning": {
    "errorMessage": "Some LAF components did not render",
    "unrenderedComponents": [
      {"id": "FilterKnob", "reason": "timeout"},
      {"id": "HiddenButton", "reason": "invisible"}
    ],
    "timeoutMs": 1000
  }
}
```

### Unrendered Component Reasons

| Reason | Description |
|--------|-------------|
| `timeout` | Component is visible but didn't render within timeout |
| `invisible` | Component or its parent is not showing (won't block polling) |

### Why This Matters

Script-based LAF paint routines execute lazily when the UI renders. Runtime errors in paint code (like accessing undefined variables or invalid API calls) won't be caught during compilation - they only surface when the component tries to paint.

The render warning helps detect:
- Paint routines that never executed (possibly broken or component hidden)
- Runtime errors that prevented painting

**Note**: The warning is skipped on the test port (1901) to avoid delays during unit tests.

## Using LAF Info for Code Navigation

When you need to modify a component's LAF styling:

1. Query `/api/list_components?moduleId=Interface` to find components with LAF
2. Check the `laf.location` property to find the source file and line
3. If `laf.renderStyle` is `"css"` or `"css_inline"`, look at `laf.cssLocation` for CSS source
4. Navigate to the location to view/edit the LAF definition

### Example Workflow

```javascript
// 1. Get component list
const response = await fetch('/api/list_components?moduleId=Interface');
const data = await response.json();

// 2. Find components with script-based LAF
const scriptLafComponents = data.components.filter(c => 
  c.laf && c.laf.renderStyle === 'script'
);

// 3. Get locations for code navigation
for (const comp of scriptLafComponents) {
  console.log(`${comp.id}: LAF defined at ${comp.laf.location}`);
  // Output: "FilterKnob: LAF defined at Scripts/Interface.js:42:8"
}
```

## Relationship to /api/status

The `/api/status` endpoint returns project information including the Scripts folder path:

```json
{
  "projectName": "MyPlugin",
  "scriptsFolder": "C:/Projects/MyPlugin/Scripts"
}
```

Use this to resolve relative LAF locations to absolute paths when needed for file operations.
