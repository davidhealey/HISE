# callScope Compile-Time Warning System

A compile-time analysis system that warns developers when audio-thread-unsafe API calls are made inside inline functions that will execute on the audio thread. The system collects warnings silently during script compilation and reports them only when a function actually enters an audio-thread context -- either via a MIDI callback body or via a registrar method that checks callback safety at registration time.

**Goal:** Not 100% safety enforcement, but a strong hinting system that guides developers toward writing audio-thread-safe code. Avoid noise that causes users to disable the system.

**Build guard:** All callScope warning code is `#if USE_BACKEND` only. Zero overhead in exported plugins -- no extra memory per inline function, no analysis pass, no warning infrastructure.

---

## Architecture

Three layers plus a runtime enforcement interface:

```
Layer 1: api_reference.json -> filtered JSON -> ApiValueTreeBuilder.exe -> XmlApi.h/.cpp
Layer 2: C++ runtime lookup API: CallScopeInfo getCallScope(className, methodName)
Layer 3: CallScopeAnalyzer pass -> silently collects warnings on InlineFunction::Object
Runtime: isRealtimeSafe() (unchanged) + getRealtimeSafetyInfo() (new) -> call site enforcement
```

---

## Layer 1: Binary Data Pipeline

### Current pipeline (deprecated):

```
C++ source -> Doxygen -> XML -> ApiExtractor.exe -> apivaluetree.dat -> BinaryBuilder.exe -> XmlApi.h/.cpp
```

### New pipeline:

```
api_reference.json -> api_enrich.py filter-binary -> filtered_api.json -> ApiValueTreeBuilder.exe -> XmlApi.h/.cpp
```

### `api_enrich.py filter-binary`

New command that reads `enrichment/output/api_reference.json` and writes a stripped-down JSON containing only the fields needed by the C++ binary.

**Per-method fields included:**

| Field | Source | Notes |
|-------|--------|-------|
| `name` | api_reference.json | Method name |
| `arguments` | Reconstructed from `parameters` array | Format: `"(Type1 param1, Type2 param2)"` matching current ValueTree consumer format |
| `returnType` | api_reference.json | |
| `description` | `brief` from enriched data, fallback to Phase 0 Doxygen `description` | Single-sentence summary |
| `callScope` | api_reference.json | `"safe"`, `"caution"`, `"unsafe"`, `"init"`, or absent (unknown) |
| `callScopeNote` | api_reference.json | Optional, present for caution tier |

**Excluded fields (too large for binary):** `userDocs`, `examples`, `pitfalls`, `diagrams`, `callbackProperties`, `valueDescriptions`.

**Size estimate:** ~400KB (current `apivaluetree.dat` is 341KB with 4 fields per method).

### `ApiValueTreeBuilder.exe`

Minimal JUCE console app (~200-300 lines). Reads the filtered JSON, builds a JUCE ValueTree, and writes `XmlApi.h` + `XmlApi.cpp` with the binary data embedded as C++ byte arrays.

**Location:** `tools/api generator/ApiValueTreeBuilder/` (new JUCER project)

**Invocation:**

```
ApiValueTreeBuilder.exe <input.json> <output_dir> <namespace>
```

Example:

```
ApiValueTreeBuilder.exe filtered_api.json "..\..\hi_scripting\scripting\api" XmlApi
```

**ValueTree schema (unchanged structure, new properties):**

- Root type: `"Api"`
- Children: one per class (type = class name)
- Grandchildren: one per method (type = `"method"`)
- Existing method properties: `name`, `arguments`, `returnType`, `description`
- New method properties: `callScope`, `callScopeNote`

The output `.h` and `.cpp` files use the same format as JUCE's BinaryBuilder -- `extern const char*` declarations and `static const unsigned char[]` definitions in the `XmlApi` namespace. This maintains backward compatibility with all existing `#include` references to `XmlApi.h`.

---

## Layer 2: C++ Runtime Lookup API

All Layer 2 code is `#if USE_BACKEND` only.

### New types

**Location:** Nested inside `ApiHelpers` in `hi_scripting/scripting/api/ScriptingApiObjects.h`

```cpp
struct ApiHelpers
{
    // ... existing members ...

#if USE_BACKEND

    enum class CallScope : uint8
    {
        Unknown = 0,   // no enrichment data
        Init,          // onInit only
        Unsafe,        // runtime OK, not audio thread
        Caution,       // audio thread OK with caveats
        Safe           // anywhere, anytime
    };

    struct CallScopeInfo
    {
        CallScope scope = CallScope::Unknown;
        String note;
    };

    static CallScopeInfo getCallScope(const String& className, const String& methodName);

#endif
};
```

### Unified lookup: `getCallScope`

**Exact mode** (`className` = specific class name): Finds the class child in the ValueTree by type name, finds the method child by `name` property, reads `callScope` string and converts to enum, reads `callScopeNote`.

**Greedy mode** (`className` = `"*"`): Iterates all class children, collects every `callScope` for methods matching `methodName`. Applies the forgiving greedy rule:

1. Collect all `callScope` values for `*.methodName` across all classes
2. If ALL are unsafe/caution/init (none safe): return worst-case scope -- warn
3. If ANY are safe: return `Safe` -- don't warn (ambiguous case, user can add type annotation for precision)
4. If NO matches found: return `Unsafe` (non-API dynamic function dispatch)

**Performance:** Pre-build a `HashMap<String, CallScopeInfo>` for greedy lookups at startup from a single pass over the ValueTree. Both exact and greedy lookups become O(1).

**Implementation location:** `hi_scripting/scripting/api/ScriptingApiObjects.cpp` (alongside existing `ApiHelpers::getApiTree()` at ~line 7012)

---

## Layer 3: Compile-Time Analysis

All Layer 3 code is `#if USE_BACKEND` only. The analyzer runs silently -- it collects data but never emits warnings directly. Reporting happens later through the runtime enforcement interface.

### 3a. InlineFunction::Object Extensions

**Location:** `hi_scripting/scripting/engine/JavascriptEngineCustom.cpp`, `InlineFunction::Object` struct (~line 409)

The `InlineFunction::Object` gains a `RealtimeSafetyInfo` member (see Runtime Enforcement Interface below for the struct definition). After the analyzer pass, this member holds all callScope warnings discovered in the function body. The `getRealtimeSafetyInfo()` override returns this stored data.

```cpp
#if USE_BACKEND
RealtimeSafetyInfo realtimeSafetyInfo;
bool callScopeAnalyzed = false;
#endif
```

### 3b. CallScopeAnalyzer Pass

**Location:** `hi_scripting/scripting/engine/JavascriptEngineCustom.cpp` (alongside existing passes: `ConstantFolding` at ~line 1066, `LocationInjector` at ~line 1088, `BlockRemover` at ~line 1130, `FunctionInliner` at ~line 1154)

**Critical constraint: Independent of HiseSettings::Compiler::Optimization.** The existing optimization passes are gated by the Optimization setting and registered in `hiseSpecialData.optimizations`. The `CallScopeAnalyzer` must NOT be in that list. It runs unconditionally after parsing completes, controlled only by the `StrictnessLevel` setting. When `StrictnessLevel` is effectively off, the pass is skipped entirely; otherwise it always runs regardless of the Optimization flag.

This requires a separate invocation point -- either a dedicated call after the optimization loop or a separate list of "always-run" analysis passes. The existing `JavascriptNamespace::runOptimisation()` pattern (`JavascriptEngineAdditionalMethods.cpp` ~line 189) can be reused with a different entry point.

**AST node resolution strategy:**

| AST Node | How class is resolved | Lookup call |
|----------|----------------------|-------------|
| `ApiCall` (~line 115, `JavascriptEngineCustom.cpp`) | Class known: `apiCall->apiClass->getObjectName()` | Exact: `getCallScope("Console", "print")` |
| `ConstObjectApiCall` (~line 278, `JavascriptEngineCustom.cpp`) | Class known from `objectPointer` -> `ConstScriptingObject::getObjectName()` | Exact: `getCallScope("Array", "push")` |
| `FunctionCall` with `DotOperator` (~line 444, `JavascriptEngineExpressions.cpp`) | Class unknown (parameter, local, dynamic) | Greedy: `getCallScope("*", "push")` |
| `FunctionCall` without `DotOperator` | Dynamic function call on a var | Always `Unsafe` -- non-inline dispatch |
| `InlineFunction::FunctionCall` (~line 744, `JavascriptEngineCustom.cpp`) | Callee already analyzed | Inherit callee's `realtimeSafetyInfo.warnings`, extend callstacks |

**Processing order:** Inline functions are processed in definition order (top-to-bottom in the script). Since HISEScript does not allow forward references to inline functions, callees are always defined before callers. This guarantees transitive warnings are available when the caller is analyzed.

**Callstack construction:** Each `Warning` in the `RealtimeSafetyInfo` carries an `Array<CallStackEntry>`. For direct API calls, the callstack has a single entry (the call site). For transitive warnings inherited from a called inline function, the callstack is the callee's warning callstack with the caller's call site prepended. This gives the user a complete trace from the top-level function down to the actual unsafe API call.

**The AST walker:** Uses the existing `callForEach()` utility (`HiseJavascriptEngine.cpp` ~line 2106) which recursively visits all child statements in an AST subtree.

### 3c. MIDI Callback Body Analysis

The MIDI callbacks (`onNoteOn`, `onNoteOff`, `onController`, `onTimer`) always execute on the audio thread. After parsing completes, the analyzer walks these callback `BlockStatement` bodies with the same AST analysis logic used for inline functions. Any unsafe/caution calls found (directly or transitively through inline function calls) are collected and reported based on the `StrictnessLevel` setting:

- **Relaxed:** No warnings emitted for MIDI callbacks
- **Warn:** Warnings logged to the console with callstacks
- **Error:** Warnings logged to the console, then a `reportScriptError` for the first unsafe call

Unlike the registrar call sites (which use the `RealtimeSafetyInfo::check()` helper), MIDI callback warnings are emitted at compile time because there is no registration point -- these callbacks are always audio-thread by definition. The reporting uses the same `RealtimeSafetyInfo::toString(StrictnessLevel)` formatting.

---

## Runtime Enforcement Interface

### RealtimeSafetyInfo Struct

**Location:** `hi_scripting/scripting/engine/HiseJavascriptEngine.h` (scoped inside `HiseJavascriptEngine::RootObject`, alongside `CallStackEntry`)

```cpp
#if USE_BACKEND

struct RealtimeSafetyInfo
{
    enum class StrictnessLevel
    {
        Relaxed,
        Warn,
        Error,
        numStrictnessLevels
    };

    struct Warning
    {
        String apiCall;                    // "Console.print" or "*.push" (greedy)
        CallScope scope;                   // from the CallScopeInfo lookup
        String note;                       // callScopeNote, e.g. "allocates MemoryOutputStream"
        Array<CallStackEntry> callStack;   // full trace from caller to unsafe API call
    };

    Array<Warning> warnings;

    bool isEmpty() const;
    bool hasUnsafe() const;
    bool hasCaution() const;

    /** Returns a formatted report string for the console.
     *  Relaxed: returns empty string (nothing to report).
     *  Warn/Error: returns formatted multi-line string with callstacks.
     */
    String toString(StrictnessLevel l) const;

    /** Helper that encapsulates the full enforcement pattern.
     *  1. Checks callable->isRealtimeSafe() -- if false, returns true immediately
     *     (structural rejection: regular function, not inline).
     *  2. Queries StrictnessLevel from MainController settings.
     *  3. Calls callable->getRealtimeSafetyInfo(), formats with toString().
     *  4. If non-empty: logs report to console.
     *  5. Returns true if the call site should reportScriptError().
     *
     *  NOTE: The exact signature and location of this helper depends on what
     *  is visible at the call sites (ScriptingObject, MainController, etc.).
     *  This will be resolved during implementation. The intent is a single
     *  static method that reduces every call site to two lines:
     *
     *      if (RealtimeSafetyInfo::check(co, this, "GlobalCable.registerCallback"))
     *          reportScriptError("unsafe callback");
     */
    static bool check(WeakCallbackHolder::CallableObject* callable,
                      ScriptingObject* caller,
                      const String& context);
};

#endif
```

### CallableObject Interface Extension

**Location:** `hi_scripting/scripting/api/ScriptingBaseObjects.h`, `WeakCallbackHolder::CallableObject` (~line 229)

New pure virtual method alongside the existing `isRealtimeSafe()`:

```cpp
struct CallableObject
{
    virtual bool isRealtimeSafe() const = 0;          // unchanged
    
#if USE_BACKEND
    virtual RealtimeSafetyInfo getRealtimeSafetyInfo() const = 0;
#endif
};
```

### Three Implementations

| Class | `isRealtimeSafe()` | `getRealtimeSafetyInfo()` |
|-------|-------------------|--------------------------|
| `FunctionObject` | `return false;` (unchanged) | Returns single structural warning: "non-inline function dispatch" with scope `Unsafe` |
| `InlineFunction::Object` | `return true;` (unchanged -- always structurally capable of audio-thread execution) | Returns the `realtimeSafetyInfo` member populated by the analyzer pass |
| `ScriptBroadcaster` | `return realtimeSafe;` (unchanged) | When `!realtimeSafe`: returns structural warning. When `realtimeSafe`: returns empty (could aggregate listener warnings in a future iteration) |

**Key design point:** `isRealtimeSafe()` on `InlineFunction::Object` always returns `true`. This preserves current behavior -- inline functions are always *allowed* to register as audio-thread callbacks. The `getRealtimeSafetyInfo()` provides the content-aware analysis, and the `StrictnessLevel` setting determines whether warnings are silent, logged, or fatal. Existing projects are unaffected unless the user opts into a stricter setting.

### Call Site Pattern

Every call site that registers a callback for potential audio-thread execution follows this two-line pattern:

```cpp
if (RealtimeSafetyInfo::check(co, this, "GlobalCable.registerCallback"))
    reportScriptError("unsafe callback");
```

The `check()` helper encapsulates:
1. The `isRealtimeSafe()` structural gate (rejects regular `function` objects)
2. `StrictnessLevel` query from `MainController` settings
3. `getRealtimeSafetyInfo()` call and `toString()` formatting
4. Console logging of the report
5. Returns `true` only when `StrictnessLevel == Error` and warnings exist

This means:
- **Relaxed:** `isRealtimeSafe()` gate only. Current behavior exactly. No further analysis.
- **Warn:** Gate passes for inline functions. Warnings logged to console. Callback registers normally.
- **Error:** Gate passes for inline functions. Warnings logged to console. `check()` returns `true`, call site calls `reportScriptError()`. Hard stop.

### StrictnessLevel Setting

**Location:** `HiseSettings::Scripting` (alongside the existing `Optimisation` flag)

**Setting name:** `CallScopeWarnings`

**Values:** `Relaxed` (default -- current behavior), `Warn`, `Error`

---

## Greedy Resolution: Design Rationale

When the analyzer sees `x.push(v)` where `x` is a parameter or local variable (type unknown at compile time), it cannot do an exact class lookup. Instead of giving up, it uses the **greedy strategy**:

1. Query `getCallScope("*", "push")` -- finds all API classes that have a `push` method
2. If every match is unsafe/caution (e.g., only `Array.push` exists, and it's `unsafe`): warn
3. If any match is safe: stay quiet -- the ambiguity means the user might be calling the safe version
4. If no matches found: return `Unsafe` (non-API dynamic function dispatch)

This is **forgiving by design.** For method names unique to one class (like `push`, `sendData`, `print`), the greedy lookup is exact. For common names shared across many classes (like `setValue`, `getValue`), the greedy lookup stays quiet to avoid noise. The user can always add a type annotation to the parameter for precise analysis.

**Empirical validation (across 3 enriched classes, 68 methods with callScope data):**
- 50 method names are unambiguously not-safe -- greedy correctly flags all of them
- 1 method name is ambiguous (`setValue`: safe on GlobalCable, caution on ScriptedViewport) -- greedy correctly stays quiet
- 15 method names are unambiguously safe -- no false warnings

The "no match" case (method name not found in any API class) returns `Unsafe`. This catches dynamic function calls like `obj.customProperty()` which invoke a non-inline function stored as an object property -- inherently not RT-safe.

---

## Call Site Inventory

Methods that register callbacks for potential audio-thread execution. Each needs the `RealtimeSafetyInfo::check()` two-liner.

### Already checking `isRealtimeSafe()` (need `check()` upgrade)

These call sites currently check `isRealtimeSafe()` and reject regular functions. They need the `check()` helper to also inspect inline function contents when `StrictnessLevel >= Warn`.

| Class | Method | Current behavior | C++ source |
|-------|--------|-----------------|------------|
| GlobalCable | registerCallback | Silently rejects if `!isRealtimeSafe() && synchronous` | `ScriptingApiObjects.cpp` ~line 9157 |
| ScriptedMidiPlayer | setRecordEventCallback | `reportScriptError` if `!isRealtimeSafe()` | `ScriptingApiObjects.cpp` ~line 6462 |
| ComplexGroupManager | registerGroupStartCallback | `reportScriptError` if `!isRealtimeSafe()` | `ScriptingApiObjects.cpp` ~line 10691 |
| ScriptBroadcaster | addListener | `reportScriptError` if broadcaster is RT-safe and listener is not | `ScriptBroadcaster.cpp` ~line 3429 |
| ScriptBroadcaster | sendMessage | `reportScriptError` if sync on audio thread and not RT-safe | `ScriptBroadcaster.cpp` ~line 3660 |

### Missing `isRealtimeSafe()` check (need both gate + `check()`)

These methods register audio-thread callbacks but have no safety check at all. They need the full `check()` two-liner added.

| Class | Method | Audio-thread when | C++ source |
|-------|--------|------------------|------------|
| TransportHandler | setOnTempoChange | sync = `SyncNotification` | `ScriptingApi.cpp` ~line 8366 |
| TransportHandler | setOnTransportChange | sync = `SyncNotification` | `ScriptingApi.cpp` ~line 8406 |
| TransportHandler | setOnSignatureChange | sync = `SyncNotification` | `ScriptingApi.cpp` ~line 8456 |
| TransportHandler | setOnBeatChange | sync = `SyncNotification` | `ScriptingApi.cpp` ~line 8496 |
| TransportHandler | setOnGridChange | sync = `SyncNotification` | `ScriptingApi.cpp` ~line 8536 |
| ScriptedMidiPlayer | setPlaybackCallback | sync = `SyncNotification` | `ScriptingApiObjects.cpp` ~line 6807 |
| Message | setAllNotesOffCallback | always | `ScriptingApi.cpp` ~line 1091 |
| ScriptFFT | setMagnitudeFunction | always | `ScriptingApiObjects.cpp` ~line 8100 |
| ScriptFFT | setPhaseFunction | always | `ScriptingApiObjects.cpp` ~line 8113 |
| ScriptUserPresetHandler | attachAutomationCallback | sync = `SyncNotification` | `ScriptExpansion.cpp` ~line 388 |
| ScriptMultipageDialog | bindCallback | sync = `SyncNotification` | `ScriptingApiContent.cpp` ~line 7241 |

### Not registrars

~35 methods accept callbacks but always invoke them asynchronously (UI thread, scripting thread, background thread). These get normal `callScope` values (`unsafe` or `init`), not any special handling. Examples: `ScriptPanel.setPaintRoutine`, `ScriptPanel.setTimerCallback`, `ScriptComponent.setControlCallback`, `TimerObject.setTimerCallback`, `Server.setServerCallback`, `Engine.showYesNoWindow`, etc.

---

## Implementation Order

### Phase 1: Data pipeline (no C++ runtime changes)

1. Implement `api_enrich.py filter-binary` command
2. Build `ApiValueTreeBuilder.exe` JUCE console app (`tools/api generator/ApiValueTreeBuilder/`)
3. Verify end-to-end: JSON -> filtered JSON -> ValueTree -> XmlApi.h/.cpp
4. Update `batchCreate.bat` (or its replacement) with the new pipeline flow

### Phase 2: Lookup API (minimal C++ changes, `#if USE_BACKEND`)

1. Add `CallScope` enum and `CallScopeInfo` to `ScriptingApiObjects.h`
2. Implement `ApiHelpers::getCallScope()` with exact + greedy (`"*"`) modes
3. Build greedy lookup HashMap at startup (single pass over ValueTree in `getApiTree()`)
4. Unit test: verify correct callScope lookups (exact and greedy)

### Phase 3: Analyzer + Enforcement (`#if USE_BACKEND`)

1. Add `RealtimeSafetyInfo` struct to `HiseJavascriptEngine.h` (inside `RootObject`)
2. Add `getRealtimeSafetyInfo()` pure virtual to `WeakCallbackHolder::CallableObject`
3. Implement on `FunctionObject` (structural warning), `InlineFunction::Object` (analyzer results), `ScriptBroadcaster` (member flag)
4. Add `realtimeSafetyInfo` member and `callScopeAnalyzed` flag to `InlineFunction::Object`
5. Implement `CallScopeAnalyzer` pass (independent of Optimization setting)
6. Add MIDI callback body analysis (`onNoteOn`, `onNoteOff`, `onController`, `onTimer`)
7. Implement `RealtimeSafetyInfo::check()` static helper
8. Add `StrictnessLevel` / `CallScopeWarnings` setting to `HiseSettings::Scripting`
9. Upgrade existing `isRealtimeSafe()` call sites to use the `check()` two-liner
10. Add missing `check()` calls to the call sites listed in the inventory
11. Integration test: compile scripts with known unsafe patterns, verify warnings at each strictness level

---

## Files Expected to Change

### New files

| File | Purpose |
|------|---------|
| `tools/api generator/ApiValueTreeBuilder/` | New JUCE console app project |
| `tools/api generator/ApiValueTreeBuilder/Source/Main.cpp` | Tool implementation (~200-300 lines) |

### Modified files -- Pipeline

| File | Change |
|------|--------|
| `tools/api generator/api_enrich.py` | New `filter-binary` command |

### Modified files -- C++ (`#if USE_BACKEND`)

| File | Change |
|------|--------|
| `hi_scripting/scripting/engine/HiseJavascriptEngine.h` | `RealtimeSafetyInfo` struct inside `RootObject` |
| `hi_scripting/scripting/api/ScriptingBaseObjects.h` | `getRealtimeSafetyInfo()` pure virtual on `CallableObject` |
| `hi_scripting/scripting/api/ScriptingApiObjects.h` | `CallScope` enum, `CallScopeInfo`, `getCallScope()` nested inside `ApiHelpers` |
| `hi_scripting/scripting/api/ScriptingApiObjects.cpp` | `getCallScope()` implementation, greedy HashMap, `check()` helper |
| `hi_scripting/scripting/engine/JavascriptEngineCustom.cpp` | `realtimeSafetyInfo` on `InlineFunction::Object`, `getRealtimeSafetyInfo()` impl, `CallScopeAnalyzer` pass |
| `hi_scripting/scripting/engine/JavascriptEngineExpressions.cpp` | `getRealtimeSafetyInfo()` impl on `FunctionObject` |
| `hi_scripting/scripting/engine/JavascriptEngineParser.cpp` | `CallScopeAnalyzer` invocation (separate from optimization passes) |
| `hi_scripting/scripting/engine/HiseJavascriptEngine.cpp` | MIDI callback analysis hook |
| `hi_scripting/scripting/api/ScriptBroadcaster.cpp` | `getRealtimeSafetyInfo()` impl on `ScriptBroadcaster`, upgrade `addListener`/`sendMessage` call sites |
| `hi_scripting/scripting/api/ScriptingApi.cpp` | Add missing `check()` calls (TransportHandler, Message) |
| `hi_scripting/scripting/api/ScriptingApiObjects.cpp` | Upgrade existing call sites + add missing ones (ScriptedMidiPlayer, ScriptFFT, GlobalCable, ComplexGroupManager) |
| `hi_scripting/scripting/api/ScriptExpansion.cpp` | Add missing `check()` call (ScriptUserPresetHandler) |
| `hi_scripting/scripting/api/ScriptingApiContent.cpp` | Add missing `check()` call (ScriptMultipageDialog) |
| HiseSettings (location TBD) | `CallScopeWarnings` / `StrictnessLevel` setting in `HiseSettings::Scripting` |
