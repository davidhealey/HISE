# HISE REST API Development Reference

This document provides context for AI agents working on the HISE REST API. Read this at the start of each session to get up to speed quickly.

**Related Documentation:**
- [rest-api.md](rest-api.md) - Full API specification and user-facing documentation
- [mcp-laf-integration.md](mcp-laf-integration.md) - LAF integration details for MCP servers

## Important Workflow Rules

1. **Never build** - Christoph will build and test
2. **Never touch git** - Christoph handles all commits
3. **Focus on**: Route metadata, handler implementations, unit tests
4. **Christoph handles**: HISE internals plumbing, complex API integrations

---

## Architecture Overview

```
hi_backend/backend/ai_tools/
├── RestServer.h         # Generic HTTP server (HISE-agnostic)
├── RestServer.cpp       # Server implementation using cpp-httplib
├── RestHelpers.h        # HISE-specific: route metadata, handler declarations, identifiers
├── RestHelpers.cpp      # Handler implementations
├── ServerUnitTests.cpp  # Unit tests for all endpoints
└── httplib.h            # Third-party HTTP library (don't modify)

hi_backend/backend/
├── BackendProcessor.h   # Owns RestServer, implements Listener
├── BackendProcessor.cpp # Routes requests to handlers, console output
```

### Request Flow

```
HTTP Request → RestServer → BackendProcessor::onAsyncRequest() → RestHelpers::handle*()
                                     ↓
                          ScopedConsoleHandler captures Console.print/errors
                                     ↓
                          Response with logs/errors arrays
```

---

## Key Classes

### RestServer (RestServer.h)

Generic HTTP server, HISE-agnostic. Key types:

```cpp
RestServer::Method        // GET, POST, PUT, DELETE
RestServer::Request       // url, method, headers; operator[](Identifier) for query params
RestServer::Response      // statusCode, body; static factories: ok(), badRequest(), notFound()
RestServer::AsyncRequest  // Thread coordination: complete(), fail(), waitForResponse(), appendLog(), appendError()
RestServer::Listener      // Callbacks: serverStarted(), serverStopped(), requestReceived(), serverError()
```

### RestHelpers (RestHelpers.h)

HISE-specific utilities:

```cpp
RestHelpers::ApiRoute           // Enum identifying each endpoint
RestHelpers::RouteMetadata      // Path, method, category, description, parameters
RestHelpers::RouteParameter     // Parameter name, description, required, default
RestHelpers::ScopedConsoleHandler  // Captures Console.print() into AsyncRequest logs/errors
```

### RestApiIds Namespace (RestHelpers.h)

All JSON property names as `Identifier` constants. Add new ones here when needed:

```cpp
namespace RestApiIds {
    DECLARE_ID(moduleId);
    DECLARE_ID(componentId);
    DECLARE_ID(success);
    DECLARE_ID(logs);
    DECLARE_ID(errors);
    // ... etc
}
```

---

## Adding a New Endpoint

### Step 1: Add to ApiRoute enum (RestHelpers.h)

```cpp
enum class ApiRoute
{
    // ... existing routes ...
    MyNewEndpoint,      // Add before numRoutes
    numRoutes
};
```

### Step 2: Add Route Metadata (RestHelpers.cpp)

In `getRouteMetadata()`, add entry **in enum order**:

```cpp
// ApiRoute::MyNewEndpoint
m.add(RouteMetadata(ApiRoute::MyNewEndpoint, "api/my_endpoint")
    .withMethod(RestServer::POST)  // or GET (default)
    .withCategory("scripting")     // "status", "scripting", or "ui"
    .withDescription("Brief description of what this does")
    .withReturns("What the response contains")
    .withModuleIdParam()           // Adds standard moduleId parameter
    .withQueryParam(RouteParameter(RestApiIds::someParam, "Description").withDefault("value"))
    .withBodyParam(RouteParameter(RestApiIds::otherParam, "Description")));  // For POST
```

### Step 3: Declare Handler (RestHelpers.h)

```cpp
static RestServer::Response handleMyNewEndpoint(MainController* mc, 
                                                 RestServer::AsyncRequest::Ptr req);
```

### Step 4: Implement Handler (RestHelpers.cpp)

```cpp
RestServer::Response RestHelpers::handleMyNewEndpoint(MainController* mc, 
                                                       RestServer::AsyncRequest::Ptr req)
{
    // For GET: read from query params
    auto param = req->getRequest()[RestApiIds::someParam];
    
    // For POST: read from JSON body
    auto obj = req->getRequest().getJsonBody();
    auto param = obj[RestApiIds::someParam].toString();
    
    // Validation
    if (param.isEmpty())
        return req->fail(400, "someParam is required");
    
    // Get script processor (common pattern)
    if (auto jp = getScriptProcessor(mc, req))
    {
        // Do work...
        
        // Build response
        DynamicObject::Ptr result = new DynamicObject();
        result->setProperty(RestApiIds::success, true);
        result->setProperty(RestApiIds::myData, someValue);
        result->setProperty(RestApiIds::logs, Array<var>());
        result->setProperty(RestApiIds::errors, Array<var>());
        
        req->complete(RestServer::Response::ok(var(result.get())));
        return req->waitForResponse();
    }
    
    return req->fail(404, "moduleId is not a valid script processor");
}
```

### Step 5: Add Switch Case (BackendProcessor.cpp)

In `onAsyncRequest()`:

```cpp
case RestHelpers::ApiRoute::MyNewEndpoint:
    return RestHelpers::handleMyNewEndpoint(this, req);
```

### Step 6: Add Unit Test (ServerUnitTests.cpp)

```cpp
void testMyNewEndpoint()
{
    beginTest("POST /api/my_endpoint");
    
    ctx->reset();
    
    // Setup: compile a script if needed
    ctx->compile("Content.makeFrontInterface(600, 400);");
    
    // Make request
    DynamicObject::Ptr bodyObj = new DynamicObject();
    bodyObj->setProperty("moduleId", "Interface");
    bodyObj->setProperty("someParam", "value");
    
    auto response = ctx->httpPost("/api/my_endpoint",
                                  JSON::toString(var(bodyObj.get())));
    var json = ctx->parseJson(response);
    
    // Assertions
    expect((bool)json["success"], "Should succeed");
    expect(json["myData"].toString() == "expected", "Should return correct data");
}
```

Don't forget to:
1. Call the test from `runTest()`
2. The endpoint will be auto-verified by `verifyAllEndpointsTested()`

---

## Common Patterns

### Getting a Script Processor

```cpp
// Handles both GET (query param) and POST (JSON body) automatically
if (auto jp = getScriptProcessor(mc, req))
{
    // jp is JavascriptProcessor*
    auto processor = dynamic_cast<Processor*>(jp);
    auto content = dynamic_cast<ProcessorWithScriptingContent*>(jp)->getScriptingContent();
}
```

### Getting a Component

```cpp
auto componentId = req->getRequest()[RestApiIds::componentId];  // GET
// or
auto componentId = obj[RestApiIds::componentId].toString();     // POST

auto c = dynamic_cast<ProcessorWithScriptingContent*>(jp)->getScriptingContent();
if (auto sc = c->getComponentWithName(Identifier(componentId)))
{
    // sc is ScriptComponent*
}
```

### Triggering Callbacks and Waiting

```cpp
sc->setValue(newValue);
sc->changed();  // Triggers control callback

// Wait for async callback to complete
waitForPendingCallbacks(sc);
```

### Operations Requiring Thread Safety

For operations that need to run on specific threads (like recompilation):

```cpp
mc->getKillStateHandler().killVoicesAndCall(
    dynamic_cast<Processor*>(jp),
    [req](Processor* p) {
        // This runs on the scripting thread
        // Call req->complete() when done
        return SafeFunctionCall::OK;
    },
    MainController::KillStateHandler::TargetThread::ScriptingThread
);

return req->waitForResponse();
```

### forceSynchronousExecution Debug Mode

For endpoints that support it (set_script, recompile, set_component_value):

```cpp
bool forceSync = (bool)obj.getProperty(RestApiIds::forceSynchronousExecution, false);

std::unique_ptr<MainController::ScopedBadBabysitter> syncMode;
if (forceSync)
    syncMode = std::make_unique<MainController::ScopedBadBabysitter>(mc);

// ... do work ...

// Include in response
result->setProperty(RestApiIds::forceSynchronousExecution, forceSync);
if (forceSync)
    result->setProperty(RestApiIds::warning, "Executed in unsafe synchronous mode - threading checks bypassed");
```

---

## Response Format

All responses follow this structure:

```json
{
  "success": true,
  "logs": ["Console.print output line 1", "line 2"],
  "errors": [
    {
      "errorMessage": "API call with undefined parameter 0",
      "callstack": ["myFunction() at Scripts/main.js:15:8"]
    }
  ],
  // ... endpoint-specific fields ...
}
```

The `logs` and `errors` arrays are populated by `ScopedConsoleHandler` which captures all `Console.print()` calls and error messages during request processing.

---

## Unit Test Helpers

### TestContext Methods

```cpp
ctx->reset();                           // Clear preset, create fresh interface
ctx->httpGet("/api/endpoint?param=x");  // GET request, returns response string
ctx->httpPost("/api/endpoint", json);   // POST request with JSON body
ctx->parseJson(response);               // Parse response to var
ctx->compile(code);                     // Compile script via set_script API
ctx->compile(code, "Interface", "onInit", false);  // Don't expect success
```

### Assertions

```cpp
expect(condition, "message");
expectEquals(actual, expected, "message");
expectEquals<int>(json["count"], 5, "Should have 5 items");
```

---

## Current Endpoints

| Route | Method | Category | Description |
|-------|--------|----------|-------------|
| `/` | GET | status | List all API methods |
| `/api/status` | GET | status | Project info, script processors |
| `/api/get_script` | GET | scripting | Read script content |
| `/api/set_script` | POST | scripting | Update script, optionally compile |
| `/api/recompile` | POST | scripting | Recompile processor |
| `/api/list_components` | GET | ui | List UI components (flat or hierarchy) |
| `/api/get_component_properties` | GET | ui | Get all properties of a component |
| `/api/get_component_value` | GET | ui | Get runtime value + min/max |
| `/api/set_component_value` | POST | ui | Set value, triggers callback |
| `/api/set_component_properties` | POST | ui | Set properties on UI components |
| `/api/screenshot` | GET | ui | Capture UI screenshot |
| `/api/get_selected_components` | GET | ui | Get selected components from Interface Designer |
| `/api/diagnose_script` | POST | scripting | Diagnostic shadow parse (no recompile) |
| `/api/get_included_files` | GET | scripting | List included external script files |

**Note:** The `diagnose_script` endpoint is consumed by the native LSP proxy binary at `tools/hise_lsp_server/`. The proxy forwards LSP `didSave` notifications to this endpoint and maps the response to standard LSP `publishDiagnostics`. See `tools/hise_lsp_server/README.md` for details.

---

## LAF (LookAndFeel) Integration

The REST API integrates with HISE's LafRegistry to track custom LookAndFeel objects and their component assignments. This enables an LLM-based coding assistant to:

1. Know which components have custom LAF styling
2. Wait for LAF rendering to complete after compilation to catch runtime errors
3. Get LAF metadata for understanding the styling setup

### LAF Info in list_components Response

Each component in the `/api/list_components` response includes a `laf` property:

```json
{
  "id": "FilterKnob",
  "type": "ScriptSlider",
  "laf": {
    "id": "LafNamespace.knobLaf",
    "renderStyle": "script",
    "location": "Interface.js:15:12",
    "cssLocation": ""
  }
}
```

- **`id`**: Variable name of the LAF object (e.g., `"laf"` or `"MyNamespace.buttonLaf"`)
- **`renderStyle`**: One of:
  - `"script"` - Uses `registerFunction()` only
  - `"css"` - Uses external CSS file only
  - `"css_inline"` - Uses `setInlineStyleSheet()` only
  - `"mixed"` - Combination of script functions and CSS
- **`location`**: Source location of `createLocalLookAndFeel()` call
- **`cssLocation`**: Full path to external CSS file (empty for inline CSS or script-only)

If no LAF is assigned: `"laf": false`

### LAF Render Warning

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

Each unrendered component includes:
- **`id`**: Component identifier
- **`reason`**: Either `"timeout"` (visible but didn't render in time) or `"invisible"` (component or parent not showing)

Invisible components are reported immediately without waiting, as they cannot render while hidden.

**Note**: LAF render waiting is skipped on the test port (1901) to avoid delays during unit tests.

---

## Settings

The REST API port is configurable in HISE Settings:

- **Location**: Settings > Scripting > Rest Api Port
- **Default**: 1900
- **Valid range**: 1024-65535

The server is started/stopped via: **Tools > Toggle REST API Server**

---

## Files Quick Reference

| File | What You'll Edit |
|------|------------------|
| `RestHelpers.h` | ApiRoute enum, handler declarations, RestApiIds |
| `RestHelpers.cpp` | Route metadata, handler implementations |
| `ServerUnitTests.cpp` | Unit tests |
| `BackendProcessor.cpp` | Switch case for new routes (usually just one line) |

Files you typically **don't** need to edit:
- `RestServer.h/cpp` - Generic server infrastructure
- `BackendProcessor.h` - Already set up with listener
- `httplib.h` - Third-party library
