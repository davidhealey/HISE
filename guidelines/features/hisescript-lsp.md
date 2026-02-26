# HISEScript LSP — Implementation Plan

## Overview

A Language Server Protocol (LSP) implementation for HISEScript that provides real-time diagnostics to AI coding agents (Claude Code, OpenCode) and editors. The primary goal is **detecting API hallucinations** — methods that don't exist, wrong argument counts, invalid constants, type mismatches — with fuzzy "did you mean?" suggestions. This dramatically reduces the back-and-forth cycle when LLMs generate HISEScript code.

**Key insight:** The LSP's main value is NOT syntax checking (HISEScript is close enough to JS that LLMs rarely produce unparseable syntax). The value is catching HISEScript-specific semantic errors that LLMs constantly produce because they confuse HISEScript with vanilla JavaScript.

### Design Principles

1. **Don't duplicate existing features** — the MCP server already has API method lookup and in-memory script compilation. The LSP targets external `.js` file editing with zero-token-cost diagnostics.
2. **Don't mess with the existing parser** — syntax errors remain fail-fast. Multi-recovery is only for semantic errors (API hallucinations, language rule violations).
3. **Leverage live runtime state** — after the first successful compile, `constObjects` contains fully resolved API objects, enabling precise type-resolved validation without greedy guessing.
4. **Crash isolation** — the Python proxy and HISE are separate processes. Either can crash without bringing down the other.

---

## Architecture

```
Agent (Claude Code / OpenCode)
    ↕ stdio (LSP JSON-RPC, zero token cost for file content)
hise-lsp proxy (Python, ~200 lines, stdlib only)
    ↕ HTTP (localhost:1900)
HISE REST server
    → killVoicesAndCall (scripting thread, ~50ms)
    → ScopedDiagnosticParse (snapshot mutable state)
    → parse external file (fail-fast for syntax, multi-recovery for semantic)
    → run ApiValidationAnalyzer + CallScopeAnalyzer + type checking
    → ~ScopedDiagnosticParse (restore all state)
    → return multi-diagnostic array
```

**Token cost model:** The agent's framework handles the LSP communication internally. The agent edits a file using its native Edit tool (costs output tokens for the edit instruction). The framework notifies the LSP, which validates and returns diagnostics. The agent sees only the compact diagnostic messages (a few lines each). The full file content transfer to HISE is invisible to the agent's token budget.

**Why not HTTP for LSP transport?** Standard LSP uses JSON-RPC over stdio or sockets, not HTTP. Both Claude Code and OpenCode expect an LSP binary that speaks stdio. The Python proxy bridges stdio ↔ HTTP, keeping HISE's REST server as the backend.

---

## F7 — Manual Shadow Parse (Entry Point)

Before building the LSP proxy, the shadow parse pipeline gets a direct keyboard shortcut for hands-on testing with real projects. **F7** triggers a diagnostic-only parse of the current external `.js` file — no recompilation, no state changes, no execution.

**Why F7:** F6 is already taken (Focus BroadcasterMap in `BackendRootWindow.cpp:787`, Toggle Global Layout Mode in `FloatingTile.cpp:2428`). F7 is the only free F-key in HISE.

### Flow

```
F7 keypress in PopupIncludeEditor
  → save current file to disk
  → resolve which JavascriptProcessor owns this file (cached lookup)
  → killVoicesAndCall → ScriptingThread (~50ms, audio suspended)
    → ScopedDiagnosticParse (snapshot)
    → parse the single external .js file against live HiseSpecialData
    → run ApiValidationAnalyzer (method/constant/type validation)
    → run CallScopeAnalyzer (audio-thread safety warnings)
    → ~ScopedDiagnosticParse (restore)
  → display diagnostics: gutter annotations + console/bottom bar listing
```

### Implementation Points

**Entry:** `PopupIncludeEditor::keyPressed()` at `PopupEditors.cpp:~461` — add `else if` block right after the F5 handler. Also add to the `addKeyPressFunction` lambda at `PopupEditors.cpp:~810-843`.

**Shortcut registration:** `PopupIncludeEditor::initKeyPresses()` at `PopupEditors.cpp:582-601` — add via `TopLevelWindowWithKeyMappings::addShortcut()`. Requires `DECLARE_ID(shadow_parse)` in the `TextEditorShortcuts` namespace.

**Core method:** New `JavascriptProcessor::shadowParseFile(const File& file)` — mirrors `compileScript()` but:
- Does NOT call `setupApi()` or create a new engine
- Constructs `ScopedDiagnosticParse` around the parse
- Returns a `DiagnosticReport` (array of `ApiDiagnostic`) instead of `SnippetResult`

**Display:** Diagnostics rendered in both:
- **Gutter annotations** — via `mcl::TextEditor::addWarning()` / `setError()` for in-editor visibility
- **Console / bottom bar** — via `EditorBottomBar::setError()` for full detail, same location as compile errors

**Legacy editor:** Also add F7 handling to `ScriptingEditor::keyPressed()` at `ScriptingEditor.cpp:~386` for consistency.

### Purpose

This serves triple duty:
1. **Testing tool** — exercise the diagnostic pipeline with real non-trivial projects before the LSP layer exists
2. **Feel check** — tune diagnostic verbosity, threshold, and UX before committing to the LSP protocol
3. **Permanent feature** — quick error checking without a full recompile (useful even without the LSP)

---

## Components

### 1. `ScopedDiagnosticParse` — RAII Snapshot/Restore

**Location:** `hi_scripting/scripting/engine/` (near the parser)

**Purpose:** Enables a "shadow parse" of a single external `.js` file against a processor's live `HiseSpecialData` without modifying any runtime state. The parser runs normally (writes freely), and the destructor restores everything.

**Scope:** Parses only the target external `.js` file — not the full snippet set. Does NOT call `setupApi()` or create a new engine. The shadow parse runs against the existing `HiseSpecialData` from the last successful compile, which already contains all resolved API objects, inline functions, namespaces, and globals.

**Threading:** Runs inside `killVoicesAndCall` on the **scripting thread** with audio suspended (~50ms). The UI skips LAF access during suspension. No thread safety concerns.

#### Why This Is Needed

The parser mutates `HiseSpecialData` during `parseStatementList()` at approximately 7 specific sites relevant to external file parsing:

| Write Site | What It Overwrites |
|------------|-------------------|
| `ns->constObjects.set(name, "uninitialised")` (line 1152) | Live const var values (API objects) |
| `ifo->body = body.release()` (line 1638) | Inline function body pointers (audio thread reads these) |
| `hiseSpecialData->includedFiles.add(...)` (lines 978-984) | Included file tracking |
| `hiseSpecialData->globals->setProperty(...)` (line 1221) | Global variables |
| `ifo->localProperties` (line 1247) | Inline function locals (name registration during parse) |

**Not snapshotted (external files only):**
- Callback statements/parameters/locals — callbacks never sit in external files, only in snippet editors.
- Namespace comments — purely syntactic sugar for autocomplete, regenerated at next full compilation.

Without snapshot/restore, a shadow parse would corrupt the live runtime state.

#### Snapshot/Restore Specification

```cpp
struct ScopedDiagnosticParse
{
    ScopedDiagnosticParse(HiseSpecialData& data);  // snapshots everything
    ~ScopedDiagnosticParse();                        // restores everything

    bool isDiagnosticMode() const { return true; }

    // Per-namespace snapshots (hiseSpecialData + all sub-namespaces):
    struct NamespaceSnapshot
    {
        JavascriptNamespace* ns;
        NamedValueSet savedConstObjects;       // copyable
        struct InlineFunctionSnapshot
        {
            InlineFunction::Object* obj;
            BlockStatement* savedBody;          // raw pointer, detached via swap
            NamedValueSet savedLocalProperties; // parser registers names here during parse
        };
        Array<InlineFunctionSnapshot> inlineFnSnapshots;
    };
    Array<NamespaceSnapshot> nsSnapshots;

    // Globals:
    NamedValueSet savedGlobalProperties;

    // Included files:
    int savedIncludedFilesSize;
};
```

#### Key Behaviors in Diagnostic Mode

The `ScopedDiagnosticParse` also sets a flag (accessible to the parser) that enables two critical behaviors:

1. **Skip `constObjects` sentinel overwrite** — When the flag is active, the parser skips the `constObjects.set(name, "uninitialised")` write at line 1152. This preserves the live resolved API objects in `constObjects`, enabling precise type resolution throughout the shadow parse and analysis passes.

2. **Enable soft-fail at semantic error sites** — Instead of `throwError()`, the parser records diagnostics and returns `DiagnosticPlaceholder` nodes (see Component 2).

#### Known Gotchas

- **`getFileContent()` duplicate-include guard** — When re-parsing a file already in `includedFiles`, this function returns empty content. The shadow parse must bypass this check (e.g., temporarily remove the file's entry, or add a flag to skip the guard).
- **Sub-namespaces** — The snapshot must iterate ALL namespaces (`hiseSpecialData` + all entries in `hiseSpecialData->namespaces`), not just the root.
- **Inline function `ThreadLocalValue<NamedValueSet> localProperties`** — The parser registers local variable names (with empty values) during parse for identifier resolution. Since the shadow parse only parses and never executes inline function bodies, and `ThreadLocalValue` provides per-thread isolation, this is benign. The snapshot saves and restores these names regardless, for cleanliness.

---

### 2. `DiagnosticPlaceholder` — Expression Node for Soft-Failed Lookups

**Location:** `hi_scripting/scripting/engine/JavascriptEngineCustom.cpp` (alongside `ApiCall`, `ApiConstant`)

**Purpose:** When the parser encounters a semantic error in diagnostic mode (e.g., unknown API method), it records the diagnostic and returns this placeholder node instead of throwing. This allows parsing to continue and collect more errors.

```cpp
struct DiagnosticPlaceholder : public Expression
{
    DiagnosticPlaceholder(const CodeLocation& l, const String& errorMessage, 
                          const StringArray& suggestions = {})
        : Expression(l), message(errorMessage), suggestions(suggestions) {}

    var getResult(const Scope& s) const override { return var::undefined(); }

    String message;
    StringArray suggestions;  // from FuzzySearcher
};
```

The `ApiValidationAnalyzer` (Component 5) collects all `DiagnosticPlaceholder` nodes from the AST during its walk and includes them in the diagnostic output alongside its own findings.

**Why not just `LiteralValue(undefined)`?** The `DiagnosticPlaceholder` carries the error message and fuzzy suggestions, which the analyzer can extract without needing a separate lookup table keyed by source location.

---

### 3. Diagnostic Mode Parser Behavior — 11 Recovery Sites

These are the specific `throwError` sites that switch from fail-fast to diagnostic-recovery when `ScopedDiagnosticParse` is active. Syntax errors remain fail-fast.

#### Tier A — API Hallucination Detection (Expression-Level, `DiagnosticPlaceholder`)

| # | Line | Error | Recovery Strategy |
|---|------|-------|-------------------|
| 1 | 2053 | "Function / constant not found: `Console.foo()`" | Use `FuzzySearcher::suggestCorrection()` against `apiClass->getAllFunctionNames()` + `apiClass->getAllConstants()`. Skip argument list (advance to matching `)`). Return `DiagnosticPlaceholder`. |
| 2 | 2076 | "Too many arguments: `Console.print(a, b, c)`" | Skip remaining arguments to `)`. Return `DiagnosticPlaceholder` with expected count in message. |
| 3 | 2079 | "Too few arguments: `Console.print()`" | Args already consumed through `)`. Return `DiagnosticPlaceholder` with expected count in message. |
| 4 | 1548 | "Inline function argument mismatch: `myFunc(a, b, c)` expects 2" | Args already parsed. Return `DiagnosticPlaceholder` with expected count. |

#### Tier A — Language Rules (`const var` Scope)

| # | Line | Error | Recovery Strategy |
|---|------|-------|-------------------|
| 5 | 1130 | "`const var` inside function body" | Record diagnostic suggesting `local` or `reg` instead. Downgrade to `local` declaration and continue parsing. |

#### Tier B — Duplicate/Conflict

| # | Line | Error | Recovery Strategy |
|---|------|-------|-------------------|
| 6 | 994 | "Identifier already defined as [type]" | Record diagnostic as warning. Continue with the new declaration (existing behavior of the storage system will handle the overlap). |

#### Tier B — Language Constructs LLMs Think Are Valid

| # | Line | Error | Recovery Strategy |
|---|------|-------|-------------------|
| 7 | 2410 | "Named function in expression: `var f = function myName(){}`" | Function is already fully parsed. Ignore the name, return the function object as a `LiteralValue`. Record diagnostic: "HISEScript does not support named function expressions. The name will be ignored." |
| 8 | 1393 | "Anonymous function at statement level: `function(){}`" | Generate a synthetic name. Record diagnostic: "Functions at statement level must have a name in HISEScript." |
| 9 | 2236 | "Referencing outer inline function parameters in nested body" | Consume identifier. Return `DiagnosticPlaceholder`. Record diagnostic: "Cannot reference inline function parameter '[name]' in nested function body. Use a capture: `function [name](params){}`." |
| 10 | 2239 | "Referencing outer inline function locals in nested body" | Same as #9 but for local variables. |
| 11 | 1602 | "Nested inline function definition" | Record diagnostic: "Nested inline functions are not allowed in HISEScript. Define it at the outer scope." Skip the inner inline function body (advance to matching `}`). Return a no-op statement. |

#### Implementation Pattern

All 11 sites share the same guard pattern:

```cpp
// Before (fail-fast):
location.throwError("Function / constant not found: " + prettyName);

// After (diagnostic-aware):
if (diagnosticMode)
{
    auto suggestions = FuzzySearcher::searchForResults(memberName, candidates, 0.6);
    recordDiagnostic(location, "Function / constant not found: " + prettyName, suggestions);
    skipToMatchingParen();  // consume the argument list
    return new DiagnosticPlaceholder(location, errorMessage, suggestions);
}
location.throwError("Function / constant not found: " + prettyName);
```

The `diagnosticMode` flag is readable from the `ExpressionTreeBuilder` — set by `ScopedDiagnosticParse` before the shadow parse begins.

---

### 4. Three-Tier Type Resolution

When validating method calls on objects, the system uses three tiers of resolution, from most precise to most approximate:

#### Tier 1: Direct API Calls (Exact)

Calls like `Console.print()`, `Engine.getSampleRate()`, `Math.random()` where the API class is directly referenced.

- **Already resolved at parse time** — the parser matches against `hiseSpecialData->apiIds` and `apiClasses`.
- **In diagnostic mode:** soft-fails return `DiagnosticPlaceholder` with fuzzy suggestions instead of throwing.
- **Type info:** Full parameter types available from `ApiClass` method signatures.

#### Tier 2: Resolved `const var` Objects (Exact)

Calls like `knob.setValue()`, `cable.sendData()` where the `const var` was resolved in a previous successful compile.

- **Leverages the diagnostic mode's key behavior:** `constObjects` sentinel overwrite is skipped, so the live resolved API objects remain accessible throughout the shadow parse.
- **Resolution path:** Look up the variable name in `constObjects` → get the live `ConstScriptingObject*` → call `getIndexAndNumArgsForFunction()` directly.
- **Type info:** Full parameter types available from the live object's `ApiClass` interface.
- **Accuracy:** Exact — we know the concrete type.

**Example of what this catches:**
```javascript
const var knob = Content.addKnob("MyKnob");    // ScriptSlider
const var panel = Content.addPanel("MyPanel");  // ScriptPanel

knob.setTimerCallback(fn);   // ERROR: ScriptSlider has no 'setTimerCallback'
panel.setTimerCallback(fn);  // OK: ScriptPanel has 'setTimerCallback'
```

Without Tier 2, a greedy lookup would find `setTimerCallback` exists on ScriptPanel and allow both calls. With Tier 2, we know `knob` is a `ScriptSlider` and correctly reject it.

**Prerequisite:** At least one successful compile must have occurred so that `constObjects` contains live objects. On a fresh project with no prior compile, Tier 2 falls back to Tier 3.

#### Tier 3: Unresolved `const var` Objects (Greedy + Fuzzy)

Calls on objects whose concrete type is unknown (first compile, conditional creation, etc.).

- **Greedy API method lookup:** Check the method name against ALL known API classes. If the method name is unique (exists in only one class) or all classes agree on parameter count, we can validate.
- **`FuzzySearcher::suggestCorrection()`:** If the method name doesn't exist anywhere, find the closest matches across all API classes and suggest them.
- **Ambiguous methods:** If the method exists in multiple classes with different signatures, skip validation (no false positives).

**Data source:** Built from `hiseSpecialData->apiClasses` (live global API classes) + `ApiHelpers::getApiTree()` (the enriched ValueTree with all 89 classes including `ConstScriptingObject` subclasses).

**Pattern:** Extend the existing `GreedyCallScopeMap` or build a parallel `GreedyApiValidationMap` with the same architecture — `HashMap<String, ...>` keyed by method name, built lazily, singleton pattern.

---

### 5. `ApiValidationAnalyzer` — Optimization Pass

**Location:** `hi_scripting/scripting/engine/JavascriptEngineCustom.cpp` (alongside `CallScopeAnalyzer`)

**Purpose:** Walks the successfully-parsed AST and validates method calls on `const var` objects (the `FunctionCall` + `DotOperator` path) that are NOT validated at parse time. Also collects `DiagnosticPlaceholder` nodes left by the parser's soft-fail recovery. Integrates static type checking for literal arguments.

**Registration:** Added via `optimizations.add(new ApiValidationAnalyzer())` in `registerOptimisationPasses()`, alongside `CallScopeAnalyzer`.

#### What It Validates

1. **`FunctionCall` + `DotOperator` nodes** (the active code path for `constVar.method()` calls):
   - Extract method name from `dot->child` (Identifier)
   - Resolve object via `dot->parent` → `ConstReference` → `constObjects` lookup (Tier 2) or greedy map (Tier 3)
   - Check: does the method exist? Is the argument count correct?
   - On failure: use `FuzzySearcher::suggestCorrection()` against the object's (or all known) method names
   - **Quality guard for suggestions:** Use threshold `0.6` (determined by Phase 0 testing). For Tier 3 cross-namespace lookups with dot-qualified candidates, require that the namespace prefix of the suggestion matches the input's namespace prefix — the FuzzySearcher's dot-qualified 50/50 blend can produce nonsensical cross-namespace matches (e.g., `"Console.log"` → `"Console.stop"` via namespace boost, `"Engine.noteToFreq"` → `"Engine.allNotesOff"` via partial token overlap). For Tier 1/2 lookups within a single known API class, no additional guard is needed — the candidate list is already scoped to one class.

2. **`DiagnosticPlaceholder` nodes** (left by parser soft-fail at the 11 recovery sites):
   - Collect the stored error message and suggestions
   - Include in the diagnostic output

3. **Static type mismatches on literal arguments** (see Component 6)

#### AST Walking Pattern

Follows the `CallScopeAnalyzer` pattern exactly:
- Subclass of `OptimizationPass`
- `getOptimizedStatement()` — uses `dynamic_cast` to check each node type
- **Read-only pass** — never replaces statements, always returns `statementToOptimize` unchanged
- Uses `callForEach()` for recursive depth-first traversal

#### Diagnostic Output

Collects diagnostics into an array (analogous to `RealtimeSafetyInfo::Warning`):

```cpp
struct ApiDiagnostic
{
    CodeLocation location;
    String message;
    StringArray suggestions;
    enum Severity { Error, Warning, Info } severity;
};
```

The REST endpoint (Component 7) collects these plus `RealtimeSafetyInfo::Warning` entries into a unified diagnostic response.

---

### 6. Static Type Checking for Literal Arguments

**Integrated into the `ApiValidationAnalyzer` pass (Component 5).**

**Scope:** Validates argument types ONLY when the argument is a **literal value** or a **resolved `const var`** with a known value. Dynamic expressions, function return values, and unresolved variables are NOT type-checked.

#### What It Catches

```javascript
// Literal mismatches (always detectable):
Message.setNoteNumber("24");              // ERROR: param 1 requires int, got String
Message.setNoteNumber(24);                // OK
reg x:int = "hello";                      // ERROR: int reg assigned String literal
inline function myFunc(a:int) {}
myFunc("a");                              // ERROR: param 1 requires int, got String

// Resolved const var mismatches (detectable after first compile):
const var NOTE = "C3";                    // constObjects["NOTE"] = "C3" (String)
Message.setNoteNumber(NOTE);              // ERROR: param 1 requires int, got String
```

#### Type Information Sources

**For API methods (Tier 1 — direct API calls):**
- The enriched API data in `XmlApi.h` includes parameter type information
- Types are either **forced** (via `ADD_TYPED_API_METHOD()` in C++ source) or **inferred** (by the enrichment pipeline's source analysis)
- Forced types produce `Error` severity; inferred types produce `Warning` severity

**For API methods on const var objects (Tier 2):**
- The live `ConstScriptingObject*` has typed method signatures accessible via its `ApiClass` interface

**For inline functions:**
- Parameter types declared via `:TypeName` syntax (e.g., `function myFunc(a:int)`) are available on the `InlineFunction::Object`

**For `reg` variables:**
- Type declared via `reg x:TypeName = value` syntax, stored in `VarRegister`

#### Detection Logic

For each validated method call (after confirming the method exists and arg count matches):

1. Get the expected parameter types from the `ApiClass` or inline function signature
2. For each argument expression in the AST:
   - If it's a `LiteralValue` → type is trivially known (`int`, `double`, `String`, `bool`, `Array`, `Object`)
   - If it's a `ConstReference` → look up value in `constObjects`, derive type from the `var` value
   - Otherwise → skip type check for this argument (dynamic, unknown)
3. Compare detected type against expected type
4. On mismatch → record diagnostic with expected vs actual type, severity based on forced vs inferred

---

### 7. REST Endpoint — `POST /v1/script/diagnose`

**Location:** `hi_backend/backend/ai_tools/RestHelpers.h/.cpp` and `BackendProcessor.cpp`

**Route:** Add `DiagnoseScript` to the `ApiRoute` enum, following the existing REST API development pattern (see `guidelines/api/rest-api-development.md`).

#### Request

```json
{
  "filePath": "Scripts/MainInterface.js"
}
```

Only the file path is required. HISE resolves:
- Which processor includes this file (via **cached lookup** — see below)
- The file content (read from disk)

#### Processor Cache

A `HashMap<String, JavascriptProcessor*>` maps filenames to their owning processor. Even if multiple processors include the same file, a single mapping suffices — the shadow parse only needs one processor's `HiseSpecialData` context.

- **Built lazily** on first `diagnose` request (walk module tree once, index all `includedFiles`)
- **Invalidated on compile** — register as a `GlobalScriptCompileListener` to clear/rebuild on any compilation
- **Cache miss** — if a file isn't in the cache (new file, never compiled), return an LSP error (see "New-file parsing" below)

#### Processing Pipeline

1. **Resolve processor** — look up the owning `JavascriptProcessor` via the processor cache
2. **`killVoicesAndCall`** — dispatch to scripting thread (~50ms)
3. **Construct `ScopedDiagnosticParse`** — snapshots all mutable `HiseSpecialData` fields, sets diagnostic mode flag
4. **Parse the file** — using the existing parser against the processor's live `HiseSpecialData`
   - Syntax errors: fail-fast, return single diagnostic, destructor restores state
   - Semantic errors: soft-fail via `DiagnosticPlaceholder`, continue parsing
5. **Run `ApiValidationAnalyzer`** — walks the AST, validates `DotOperator` calls, collects `DiagnosticPlaceholder` nodes, checks literal argument types
6. **Run `CallScopeAnalyzer`** — collects CallScope warnings (audio-thread safety)
7. **Discard AST** — no `perform()`, no execution
8. **`~ScopedDiagnosticParse`** — restores all snapshotted state
9. **Return diagnostics**

#### Response

```json
{
  "success": true,
  "diagnostics": [
    {
      "line": 15,
      "column": 5,
      "severity": "error",
      "source": "api-validation",
      "message": "Unknown method 'sendDataAsync' on GlobalCable",
      "suggestions": ["sendData"]
    },
    {
      "line": 23,
      "column": 12,
      "severity": "error",
      "source": "api-validation",
      "message": "FileSystem.UserDesktop is not a valid constant",
      "suggestions": ["FileSystem.Desktop"]
    },
    {
      "line": 31,
      "column": 8,
      "severity": "error",
      "source": "type-check",
      "message": "Message.setNoteNumber() parameter 1 requires int, got String"
    },
    {
      "line": 40,
      "column": 3,
      "severity": "warning",
      "source": "language",
      "message": "const var cannot be declared inside function body. Use 'local' or 'reg' instead."
    },
    {
      "line": 47,
      "column": 8,
      "severity": "warning",
      "source": "callscope",
      "message": "[CallScope] cable.sendData (unsafe) in audio-thread context"
    }
  ]
}
```

The `source` field distinguishes diagnostic categories: `syntax` (fail-fast), `api-validation` (method/constant/argcount), `type-check` (literal type mismatches), `language` (HISEScript-specific rule violations), `callscope` (audio-thread safety).

---

### 8. `hise-lsp` Python Proxy

**Location:** `tools/hise-lsp/hise-lsp.py`

**Purpose:** Thin bridge between the LSP stdio protocol (expected by Claude Code / OpenCode) and HISE's REST API on `localhost:1900`. Single file, stdlib only (`json`, `sys`, `http.client`), zero dependencies.

#### LSP Protocol Support (v1 Minimum)

| Direction | Message | Action |
|-----------|---------|--------|
| Agent → Proxy | `initialize` | Respond with capabilities: `textDocumentSync: Full`, `diagnosticProvider: true` |
| Proxy → Agent | `initialized` | Ready |
| Agent → Proxy | `textDocument/didOpen` | POST file path to `http://localhost:1900/v1/script/diagnose` |
| Agent → Proxy | `textDocument/didChange` | POST file path to `http://localhost:1900/v1/script/diagnose` |
| Proxy → Agent | `textDocument/publishDiagnostics` | Map HISE diagnostic array to LSP diagnostic format |
| Agent → Proxy | `textDocument/didClose` | No-op |
| Agent → Proxy | `shutdown` / `exit` | Clean exit |

#### Diagnostic Severity Mapping

| HISE Severity | LSP Severity |
|---------------|-------------|
| `error` | `DiagnosticSeverity.Error` (1) |
| `warning` | `DiagnosticSeverity.Warning` (2) |
| `info` | `DiagnosticSeverity.Information` (3) |

#### Error Handling

- **HISE not running:** Return a single diagnostic: "HISE IDE is not running on port 1900. Start HISE to enable HISEScript diagnostics."
- **HISE returns HTTP error:** Log to stderr, return empty diagnostics
- **Connection timeout:** Configurable via `HISE_LSP_TIMEOUT` environment variable (default: 5000ms)
- **Rapid edits:** Debounce with configurable delay via `HISE_LSP_DEBOUNCE` environment variable (default: tune during prototype testing)

#### Configuration

Port and host configurable via environment variables:
- `HISE_LSP_PORT` — default `1900`
- `HISE_LSP_HOST` — default `localhost`

---

## Agent Integration

### Claude Code

Plugin file `.lsp.json` (placed in project root or `~/.claude/plugins/`):

```json
{
  "hisescript": {
    "command": "python",
    "args": ["path/to/hise-lsp.py"],
    "extensionToLanguage": {
      ".js": "hisescript"
    }
  }
}
```

### OpenCode

Config in `opencode.json`:

```json
{
  "lsp": {
    "hisescript": {
      "command": ["python", "path/to/hise-lsp.py"],
      "extensions": [".js"]
    }
  }
}
```

### VS Code (Side Effect)

A minimal `package.json` extension could wrap the same proxy for VS Code users, but this is out of scope for v1.

---

## Enrichment Pipeline Impact

The enriched API data in `XmlApi.h` already provides:

- **Method signatures** — names and parameter strings for all 89 classes (from doxygen stubs)
- **CallScope data** — for the 3 enriched classes (Console, GlobalCable, ScriptedViewport), extensible
- **Parameter types** — forced (via `ADD_TYPED_API_METHOD()`) and inferred (from source analysis)
- **Type confidence flag** — distinguishes forced vs inferred types for diagnostic severity

### Additions Needed for LSP

| Data | Status | Impact |
|------|--------|--------|
| Method names + param counts (all classes) | **Already available** | Method existence + arg count validation works from day one |
| Parameter types (forced) | **Available for typed API methods** | High-confidence type mismatch errors |
| Parameter types (inferred) | **Available from enrichment analysis** | Lower-confidence type mismatch warnings |
| API constants (namespaces + names) | **Not yet in blob** | Next enrichment iteration — add one class with constants to establish the pattern |
| Deprecation flags | **Not yet** | See `guidelines/features/enrichment-v2-ideas.md` — 6 known deprecated methods identified |
| `backendOnly` flag | **Not yet** | 33+ `#if USE_BACKEND` guarded methods — see enrichment v2 ideas |

### Graceful Degradation

- If constants aren't enriched yet → skip constant validation (method validation still works)
- If parameter types aren't available for a method → skip type checking for that call (arg count validation still works)
- If CallScope data isn't enriched for a class → skip CallScope warnings for those methods (no false positives)

---

## Implementation Order

### Phase 0: FuzzySearcher Tuning — COMPLETE

**Test file:** `hi_tools/hi_tools/FuzzySearcherTests.cpp` (included in `hi_tools_01.cpp` under `#if HI_RUN_UNIT_TESTS`)
**Category:** `"AI Tools"` — runs with the existing CI test runner.
**Tests:** 8 sections, ~35 test cases — Levenshtein basics, score formula, unqualified API correction (Console 8 methods, Synth 54 methods), dot-qualified API correction (~90 methods from 6 namespaces), threshold sweep, ranking verification, case insensitivity, minimum quality guard.

#### Threshold Sweep Results

| Threshold | TP | FP | Borderline | Precision | Recall |
|-----------|-----|-----|------------|-----------|--------|
| 0.3 | 20/20 | 2/4 | 7/7 | 0.91 | 1.00 |
| 0.4 | 20/20 | 0/4 | 7/7 | 1.00 | 1.00 |
| 0.5 | 20/20 | 0/4 | 6/7 | 1.00 | 1.00 |
| **0.6** | **20/20** | **0/4** | **4/7** | **1.00** | **1.00** |

**Recommended threshold: `0.6`** — used throughout the LSP pipeline.

#### Key Findings

1. **100% recall at all thresholds** — every single-edit hallucination (missing char, transposition, wrong suffix) is caught from 0.3 to 0.6. The algorithm is robust for the typo cases LLMs commonly produce.
2. **`contains()` shortcut** — `FuzzySearcher::fitsSearch()` returns `true` if the search term is a substring of any candidate, bypassing the distance check entirely. This means short prefixes like `"play"` always match `"playNote"`. For API correction this is acceptable — suggesting `playNote` when someone writes `play(...)` is useful.
3. **Dot-qualified 50/50 blend causes false matches** — `suggestCorrection()` blends last-token and full-string scores for dot-qualified names. At 0.6, two false matches persist: `"Console.log"` → `"Console.stop"` (namespace boost inflates score) and `"Engine.noteToFreq"` → `"Engine.allNotesOff"` (partial token overlap). **Mitigation: require namespace prefix match for dot-qualified suggestions in the `ApiValidationAnalyzer` (see Component 5 quality guard note).**
4. **No quality guard needed for unqualified lookups** — when searching within a single API class (parser context), the candidate list is already scoped. Garbage inputs like `"foo"`, `"xyz"` correctly return empty at 0.6.
5. **No code changes to `FuzzySearcher` needed** — the existing implementation works correctly for API correction at threshold 0.6. The quality guard is applied at the consumer level (`ApiValidationAnalyzer`), not in the searcher itself.

### Phase 1: Foundation (`hi_scripting`) — COMPLETE

1. **`ScopedDiagnosticParse`** — RAII snapshot/restore struct
   - Implement snapshot in constructor, restore in destructor
   - Handle all namespace iteration (root + sub-namespaces)
   - Add diagnostic mode flag accessible to `ExpressionTreeBuilder`
   - Add `constObjects` sentinel-overwrite skip behavior
   - Scope: single-file parse only (no callbacks, no comments)
   - **Test:** Wrap a normal compile, verify state unchanged after

2. **`DiagnosticPlaceholder`** — expression node
   - Simple struct: location, message, suggestions, `getResult()` returns `undefined`
   - **Test:** Manually construct one, verify it evaluates to `undefined`

3. **Diagnostic mode at the 11 recovery sites** — parser modifications
   - Guard each site with `if (diagnosticMode)` check
   - Implement recovery strategy per site (skip args, return placeholder, downgrade, etc.)
   - Use `FuzzySearcher::suggestCorrection()` for "did you mean?" at API lookup failures (with tuned threshold from Phase 0)
   - **Test:** Compile scripts with known errors in diagnostic mode, verify multiple diagnostics collected

#### Phase 1 Implementation Notes

All Phase 1 code is `#if USE_BACKEND` guarded (stripped from exported plugins).

**Files modified:**

| File | Changes |
|------|---------|
| `HiseJavascriptEngine.h` | Added `diagnosticMode` flag to `HiseSpecialData`, `ApiDiagnostic` struct, `ScopedDiagnosticParse` struct declaration |
| `JavascriptEngineCustom.cpp` | Added `DiagnosticPlaceholder` expression node (after `ApiConstant`, before `ApiCall`) |
| `JavascriptEngineParser.cpp` | Added `isDiagnosticMode()`, `recordDiagnostic()`, `skipToMatchingParen()` to `ExpressionTreeBuilder`; 11 recovery sites; `constObjects` sentinel skip; `getFileContent()` duplicate-include bypass |
| `JavascriptEngineAdditionalMethods.cpp` | `ScopedDiagnosticParse` constructor/destructor implementation; `throwExistingDefinition` diagnostic-mode guard (site #6) |

**Recovery sites implemented:**

| # | Location | Error | Recovery |
|---|----------|-------|----------|
| 1 | `parseApiCall` | Function/constant not found | FuzzySearcher suggestion → DiagnosticPlaceholder |
| 2 | `parseApiCall` | Too many arguments | Skip remaining args → DiagnosticPlaceholder |
| 3 | `parseApiCall` | Too few arguments | Record + DiagnosticPlaceholder |
| 4 | `parseInlineFunctionCall` | Inline function arg mismatch | Record + DiagnosticPlaceholder |
| 5 | `parseConstVar` | `const var` inside function body | Record warning → downgrade to local |
| 6 | `throwExistingDefinition` | Identifier already defined | Silently continue (warning) |
| 7 | `parseFactor` (function expr) | Named function expression | Record warning → ignore name, continue |
| 8 | `parseFunction` | Anonymous function at statement level | Record → synthetic name → continue |
| 9 | `parseFactor` (outer inline) | Reference outer inline parameter | Record → DiagnosticPlaceholder |
| 10 | `parseFactor` (outer inline) | Reference outer inline local | Record → DiagnosticPlaceholder |
| 11 | `parseInlineFunction` | Nested inline function | Record → skip body block → no-op statement |

**ScopedDiagnosticParse snapshots:**

- Per-namespace (root + all sub-namespaces): `constObjects`, inline function bodies + `localProperties`
- Global: `globals` properties
- `includedFiles` size (trimmed on restore)
- Sets/clears `diagnosticMode` flag

**Key behaviors in diagnostic mode:**

- `constObjects` sentinel overwrite skipped (preserves live API objects for Tier 2)
- `getFileContent()` duplicate-include guard bypassed
- `throwExistingDefinition` returns instead of throwing

### Phase 1.5: F7 Shortcut (`hi_scripting` + `hi_backend`)

4. **F7 keyboard shortcut** — manual shadow parse entry point
   - Add F7 handling to `PopupIncludeEditor::keyPressed()` and `ScriptingEditor::keyPressed()`
   - Register shortcut via `TopLevelWindowWithKeyMappings::addShortcut()`
   - Implement `JavascriptProcessor::shadowParseFile()` — single-file parse with `ScopedDiagnosticParse`
   - Display diagnostics: gutter annotations via `mcl::TextEditor` + console/bottom bar
   - Processor cache: `HashMap<String, JavascriptProcessor*>` with `GlobalScriptCompileListener` invalidation
   - **Test:** Open a non-trivial project, press F7 on files with known API errors, verify gutter + console output
   - **Purpose:** Hands-on testing with real projects to tune diagnostic verbosity before building the LSP layer

### Phase 2: Analysis (`hi_scripting`)

5. **`ApiValidationAnalyzer`** — optimization pass
   - Follow `CallScopeAnalyzer` pattern exactly
   - Walk AST for `FunctionCall` + `DotOperator` nodes
   - Implement three-tier resolution (direct API → resolved const var → greedy)
   - Collect `DiagnosticPlaceholder` nodes
   - Use `FuzzySearcher` for suggestions on unknown methods
   - **Test:** Compile scripts with hallucinated method calls, verify diagnostics

6. **Static type checking** — integrated into `ApiValidationAnalyzer`
   - Check literal arguments against expected parameter types
   - Check resolved const var arguments
   - Respect forced vs inferred type confidence
   - **Test:** Compile scripts with type mismatches, verify diagnostics with correct severity

### Phase 3: Integration (`hi_backend`) — COMPLETE

7. **REST endpoint `POST /api/diagnose_script`** — new route
   - Added `DiagnoseScript` to `ApiRoute` enum, route metadata, handler, switch case
   - Accepts `moduleId` and/or `filePath` (absolute or relative to Scripts folder)
   - When `moduleId` only: auto-selects first external file (or 400 if none)
   - When `filePath` only: walks all `JavascriptProcessor`s to find the owning processor
   - Reads file from disk, calls `shadowParseFile()` (reuses F7 pipeline)
   - Response: `{ success, moduleId, filePath, diagnostics: [{line, column, severity, source, message, suggestions}], logs, errors }`
   - Severity strings: `"error"`, `"warning"`, `"info"`, `"hint"`
   - No processor cache — per-request module tree walk (<1ms)
   - 6 unit tests: missing params, unknown module, no external files, clean file success, API error with diagnostics, filePath-only resolution
   - **Tested:** via unit tests (`verifyAllEndpointsTested()` coverage)

### Phase 4: Native LSP Proxy — COMPLETE

Separate repo: `https://github.com/christoph-hart/hise_lsp_server.git`
Submodule path: `tools/hise_lsp_server`

- **Native C++ binary** (JUCE consoleapp, ~350 lines, single `Main.cpp`)
- JSON-RPC stdio framing → POST /api/diagnose_script → publishDiagnostics
- Stateless: no file tracking, no caching, diagnose on every didOpen/didSave
- Precompiled binaries in `bin/` (Windows, macOS, Linux)
- Agent configs: Claude Code plugin (`plugin/`), OpenCode + Crush examples (`config/`)
- Zero runtime dependencies (native binary)
- Uses `MemoryOutputStream` for all string building (JSON framing, header parsing) — avoids `String` concatenation performance trap
- **Detailed implementation plan: `guidelines/features/hisescript-lsp-phase4.md`**

### Phase 5: Agent Integration Testing

9. **Integration testing** — connect to Claude Code / OpenCode
   - Install Claude Code plugin from `tools/hise_lsp_server/plugin/`
   - Configure OpenCode via `opencode.json` LSP entry
   - Edit `.js` files, verify diagnostics appear automatically after save
   - Test hallucination scenarios: wrong methods, wrong args, wrong types, language violations
   - Verify error cases: HISE not running, file not included

---

## Out of Scope (v1)

- **Hover / autocomplete / go-to-definition** — diagnostics only
- **Multi-file validation** — one file at a time
- **Incremental parsing** — full file every time (internal to framework, no token cost)
- **Statement-level syntax error recovery** — syntax errors remain fail-fast
- **Type inference for dynamic expressions** — only literals and resolved const vars
- **VS Code extension packaging** — the proxy works but no extension wrapper
- **Constant validation** — deferred until enrichment pipeline covers constants
- **New-file parsing** (files never compiled before) — requires `preprocessCode` reversal which is significantly harder. v1 requires at least one prior successful compile. When a file has never been compiled, the LSP returns an explicit error diagnostic: *"This file has not been compiled yet. Run a full compile first (F5 in editor, or use the `hise_runtime_set_script` / `hise_runtime_recompile` MCP tool)."* This is preferable to silently degrading to Tier 3 — the agent needs to know it must compile first for accurate diagnostics.

---

## Key File Locations (Reference)

| File | Relevance |
|------|-----------|
| `hi_scripting/scripting/engine/JavascriptEngineParser.cpp` | Parser — 11 recovery sites, `parseApiCall()`, `parseFactor()` |
| `hi_scripting/scripting/engine/JavascriptEngineCustom.cpp` | `ApiCall`, `CallScopeAnalyzer`, `InlineFunction::Object`, optimization passes |
| `hi_scripting/scripting/engine/HiseJavascriptEngine.h` | `HiseSpecialData`, `OptimizationPass`, `Callback`, `JavascriptNamespace` |
| `hi_scripting/scripting/engine/HiseJavascriptEngine.cpp` | `execute()`, `callForEach()`, `executePass()` |
| `hi_scripting/scripting/engine/JavascriptEngineStatements.cpp` | `ScopedSuppress`, statement types |
| `hi_scripting/scripting/engine/JavascriptEngineAdditionalMethods.cpp` | `preprocessCode()`, `Scope::findFunctionCall()` |
| `hi_scripting/scripting/engine/JavascriptApiClass.h` | `ApiClass` — `getAllFunctionNames()`, `getIndexAndNumArgsForFunction()`, `getAllConstants()` |
| `hi_scripting/scripting/api/ScriptingApiObjects.cpp` | `GreedyCallScopeMap`, `ApiHelpers::getApiTree()`, `check()` |
| `hi_scripting/scripting/api/XmlApi.h` | Enriched API binary blob (89 classes, method signatures, types, callScope) |
| `hi_tools/hi_tools/MiscToolClasses.h` | `FuzzySearcher` — `suggestCorrection()`, `searchForResults()`, Levenshtein |
| `hi_tools/hi_tools/FuzzySearcherTests.cpp` | Phase 0: FuzzySearcher unit tests (threshold tuning, API correction validation) |
| `hi_backend/backend/ai_tools/RestHelpers.h/.cpp` | REST API routes, handlers |
| `hi_backend/backend/BackendProcessor.cpp` | `onAsyncRequest()` route dispatch |
| `hi_scripting/scripting/components/PopupEditors.cpp` | F7 shortcut entry point, `PopupIncludeEditor::keyPressed()` |
| `hi_scripting/scripting/components/ScriptingEditor.cpp` | F7 in legacy editor |
| `tools/hise_lsp_server/` | Git submodule: native C++ LSP proxy (separate repo) |
| `tools/hise_lsp_server/cpp/Source/Main.cpp` | LSP proxy implementation (~350 lines) |
| `tools/hise_lsp_server/bin/` | Precompiled LSP binaries (Windows, macOS, Linux) |
| `tools/hise_lsp_server/plugin/` | Claude Code plugin wrapper (.lsp.json) |
| `guidelines/features/hisescript-lsp-phase4.md` | Phase 4 detailed implementation plan |

---

## v2 Feature Ideas (Out of Scope for v1)

These are explicitly NOT part of the v1 plan. Captured here so they don't get forgotten.

### Runtime Data Validation

The v1 LSP validates **API method signatures** (does this method exist? right arg count? right types?). v2 should validate **runtime data** — the actual values and identifiers passed to API methods.

**Examples:**

```javascript
// Component property validation
const var slider = Content.addKnob("MyKnob");
slider.set("value", 5);
// → ERROR: "value" is not a property of ScriptSlider.
//   Did you mean: use slider.setValue(5) instead?

// Module attribute validation
const var proc = Synth.getChildSynth("MySynth");
proc.setAttribute(proc.Intensity, 0.5);
// → ERROR: "Intensity" is not an attribute of ChildSynth.
//   Did you mean: proc.setIntensity(0.5)?

// Module ID validation
const var fx = Synth.getEffect("MyReverbb");
// → WARNING: No module with ID "MyReverbb" found.
//   Did you mean: "MyReverb"?
```

This requires access to live runtime data (component property lists, module tree, attribute names) which goes beyond static API signature checking. The exact mechanism for accessing this data is TBD — options include querying via the existing REST API, enriching the binary blob with property/attribute metadata, or reading from the live `HiseSpecialData` during the shadow parse.

### CSS Validation

The shadow parser should detect inline CSS style sheet definitions and CSS file references in HISEScript, then validate them using the existing `simple_css` parser.

**What to detect:**

```javascript
// Inline style sheet definitions (string literal passed to setStyleSheet)
panel.setStyleSheet("button { background: red; colr: blue; }");
// → WARNING: Unknown CSS property 'colr'. Did you mean: 'color'?

// CSS file references loaded via File API or include patterns
const var css = FileSystem.loadFile("Style.css");
panel.setStyleSheet(css);
// → Parse the referenced .css file content through simple_css
```

**Implementation approach:**

1. In the `ApiValidationAnalyzer`, detect calls to `setStyleSheet()` where the argument is a string literal
2. Extract the CSS string and run it through the `simple_css` parser
3. Collect any CSS parse errors/warnings and convert them to `ApiDiagnostic` entries with `source = "css"`
4. For file references, resolve the file path and parse the CSS file content
5. Use `FuzzySearcher` for property name suggestions on unknown CSS properties

**Depends on:** Understanding the `simple_css` parser's error reporting API and what CSS properties/selectors HISE's `simple_css` subset supports.

### Runtime State Inspection via DebugInformation

The v1 LSP is purely static — it validates code structure but has zero visibility into runtime state. v2 should expose HISE's `DebugInformation` pipeline through the LSP, giving both agents and human developers live variable inspection.

**What DebugInformation already provides:** HISE's script debugger maintains a live tree of all script variables with their current types, values, and available members. This is what powers the variable watchpoint panel in the HISE IDE. The data is already there — it just needs an API surface.

**Value for LLM agents:**

The agent currently flies blind between "compile succeeded" and "the user says it doesn't work." Runtime inspection closes that gap:

1. **Verify its own work after compile** — "I created `const var slider = Content.addKnob("MySlider")` — is `slider` actually a `ScriptSlider`? What's its current value?" The agent can confirm its code did what it intended without asking the user.

2. **Debug user-reported issues** — User says "my knob doesn't do anything." Agent inspects the variable: sees the callback connection is missing, or the value range is wrong, or the module ID in `Synth.getEffect()` resolved to `undefined`. Targeted diagnosis instead of guessing.

3. **Understand existing code it didn't write** — Agent opens a new project. Instead of guessing what `p` is from context clues, it queries: "`p` is a `ScriptedMidiPlayer` with 3 sequences loaded, currently at position 0.5." This is especially valuable for `const var` objects where the concrete type isn't written in the code.

4. **Type-aware code generation** — If the agent can query "what methods/properties does this variable have?", it can generate correct API calls without hallucinating. The DebugInformation already stores the object type and its available members. This turns runtime state into a live autocomplete source.

**LSP integration approach:**

The natural LSP capability is `textDocument/hover`. The agent's framework sends a hover request at a variable's position, the LSP proxy calls a new HISE REST endpoint (e.g., `GET /api/inspect_variable?moduleId=Interface&name=slider&line=15`), and HISE queries the DebugInformation to return:

```json
{
  "name": "slider",
  "type": "ScriptSlider",
  "value": "0.5",
  "members": ["setValue", "getValue", "set", "get", "setRange", ...],
  "properties": {"text": "My Slider", "min": 0.0, "max": 1.0, "stepSize": 0.01}
}
```

The LSP formats this as a hover response (markdown), and the agent sees it inline.

**Alternative approaches:**
- Custom LSP request (`hisescript/inspect`) instead of hover — avoids conflicting with other hover providers
- Pull diagnostics model: on each `didSave`, include a summary of key variable states alongside the diagnostics
- Dedicated REST endpoint only (no LSP integration) — agents call it via MCP when they need inspection, rather than passively through the LSP

**Depends on:** Understanding the `DebugInformation` class hierarchy, how it's populated (per-compile vs per-execution), thread safety for reading it from the REST handler thread, and what subset of data is meaningful to expose (full object dumps vs summary).

### Additional Enrichment Flags

See `guidelines/features/enrichment-v2-ideas.md` for a comprehensive list of enrichment metadata that would improve diagnostic quality (constants in blob, `backendOnly` flag, `callbackSignature` for Function params, `sideEffects` annotations, etc.).
