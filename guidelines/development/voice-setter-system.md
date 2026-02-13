# HISE UIUpdater & Voice Protection System

**Related Documentation:**
- [unit-testing.md](unit-testing.md) - Reusable testing patterns used in Part 10
- [cpp-architecture.md](cpp-architecture.md) - General C++ development guidelines

## Document Purpose

This is the **definitive guideline document** for HISE's UI synchronization system for scriptnode nodes. It covers:
- The NodeUIUpdater class hierarchy for audio-to-UI communication
- Voice protection for safe PolyData access from non-audio threads
- Global cable automatic protection
- Best practices and key insights

**Last Updated:** Session fixing Scenario #6 thread safety bug in setAttribute() paths (HardcodedSwappableEffect, FlexAhdsrEnvelope)

---

## Implementation Status: COMPLETE

### Files Modified

| File | Changes |
|------|---------|
| `hi_dsp_library/node_api/helpers/UIUpdater.h` | Complete rewrite with new class hierarchy |
| `hi_dsp_library/dsp_nodes/RoutingNodes.h` | Added PolyHandler storage and automatic callback wrapping |
| `hi_dsp_library/node_api/helpers/node_macros.h` | Added `SN_VOICE_SETTER_2_WITH_DATA_LOCK` macro; removed `forceAll` from all macros |
| `hi_dsp_library/dsp_nodes/CoreNodes.h` | Updated `file_player` with data lock macro and proper `reset()` locking |
| `hi_dsp_library/snex_basics/snex_ExternalData.h` | Fixed `data::updater<T>` incomplete type issue |
| `hi_dsp_library/snex_basics/snex_Types.h` | Added `PolyHandler::AccessType` enum with `ForceAllVoices`, `forEachCurrentVoice()` with `AccessType`, removed `forceAll` from `PolyData`/`ScopedVoiceIndexCacher` |
| `hi_dsp_library/node_api/nodes/Containers.h` | Removed `forceAll` from `base_wrapper`, `sub_tuple_helper_impl`, `DummyVoiceSetter`, container `VoiceSetter` |
| `hi_dsp_library/node_api/nodes/processors.h` | Removed `false` argument from `VoiceSetter` calls |
| `hi_dsp_library/dsp_nodes/MathNodes.h` | Updated `neural::clearFilterState()` to use `forEachCurrentVoice(..., ForceAllVoices)` |
| `hi_dsp_library/unit_test/uiupdater_tests.cpp` | Updated `forceAll` test, added `testForEachCurrentVoiceWithAccessType` |

---

## Part 1: The PolyData Thread Safety Problem

### How PolyData Works

`PolyData<T, NumVoices>` stores per-voice state. Iteration behavior depends on context:

| Context | voiceIndex | Iteration Behavior |
|---------|------------|-------------------|
| Audio thread inside `ScopedVoiceSetter` | >= 0 | Single voice only |
| Other threads (UI, scripting) | Should be -1 | All voices |

### The Race Condition

When the audio thread is mid-callback with `voiceIndex >= 0`, and another thread accesses the same `PolyData`:

1. UI thread reads `voiceIndex` (which is `>= 0` from audio thread)
2. UI thread accesses only one voice instead of all
3. **Silent bug** - wrong behavior, no crash, hard to debug

### The Solution: ScopedAllVoiceSetter

`PolyHandler::ScopedAllVoiceSetter` registers the current thread as the "all voices" thread. When active, `getVoiceIndex()` returns `-1` for that thread, regardless of what the audio thread has set.

**Performance:** Zero overhead on audio thread hot path. Thread ID lookup only happens during UI operations.

---

## Part 2: NodeUIUpdater Class Hierarchy

### Class Overview

```
NodeUIUpdaterBase                    (base: PrepareSpecs storage, voice protection)
    |
    +-- NodeUITimer                  (timer functionality)
    |       |
    |       +-- NodeUITimer::WithData<T>    (+ lock-free data exchange)
    |
    +-- NodeUIAsyncUpdater           (async update + rate limiting)
            |
            +-- NodeUIAsyncUpdater::WithData<T>  (+ lock-free data exchange)
```

### NodeUIUpdaterBase

Base class providing:
- `PrepareSpecs prepareSpecs` - stored for voice protection and rate limiting calculations
- `setupUIUpdater(PrepareSpecs)` - must be called in `prepare()`
- Voice tracking for multi-voice displays
- Automatic `ScopedAllVoiceSetter` wrapping of callbacks

### NodeUITimer

Timer-based UI updates. Best for:
- Continuous displays (meters, waveforms, spectrum analyzers)
- Fixed refresh rate requirements

```cpp
struct MyNode : public NodeUITimer
{
    void prepare(PrepareSpecs specs) {
        setupUIUpdater(specs);
        startTimerHz(30);  // 30 FPS refresh
    }
    
    void onUITimerCallback() override {
        // Called on message thread with ScopedAllVoiceSetter active
        // Safe to iterate PolyData here
    }
};
```

### NodeUIAsyncUpdater

Event-driven UI updates with rate limiting. Best for:
- Displays that only need updates when data changes
- High-frequency triggers that need coalescing (e.g., per-buffer triggers)

```cpp
struct MyNode : public NodeUIAsyncUpdater
{
    void prepare(PrepareSpecs specs) {
        setupUIUpdater(specs);
        setEnableRateLimiting(true, 30.0, 1);  // Coalesce to ~30 FPS
    }
    
    void process(ProcessDataDyn& data) {
        triggerAsyncUpdate();  // Can be called every buffer - rate limited
    }
    
    void onUIAsyncUpdate() override {
        // Called on message thread with ScopedAllVoiceSetter active
    }
};
```

### WithData<T> Variants

Add lock-free data exchange for passing data from audio to UI thread:

```cpp
struct DisplayData {
    float level;
    int state;
};

struct MyNode : public NodeUITimer::WithData<DisplayData>
{
    void process(ProcessDataDyn& data) {
        // Audio thread: post data
        postData({calculateLevel(), currentState});
    }
    
    void onUITimerCallback() override {
        // UI thread: consume data
        DisplayData d;
        if (consumeData(d)) {
            updateDisplay(d.level, d.state);
        }
    }
};
```

**Constraint:** `T` must be trivially copyable (enforced by `static_assert`).

---

## Part 3: Voice Tracking Feature

### The Multi-Voice Display Problem

In polyphonic instruments, when multiple voices are active, UI displays can "jump" between voices because each callback might read from a different voice's data.

### Solution: Active Voice Tracking

```cpp
struct MyNode : public NodeUIAsyncUpdater::WithData<DisplayData>
{
    void handleHiseEvent(HiseEvent& e) {
        if (e.isNoteOn()) {
            setActiveVoiceForUIUpdates();  // Capture this voice for UI
        }
    }
    
    void process(ProcessDataDyn& data) {
        postData({...});  // Only posts if this is the tracked voice
    }
};
```

### How It Works

1. First call to `setActiveVoiceForUIUpdates()` enables tracking and captures current voice index
2. `postData()` checks if current voice matches tracked voice before posting
3. UI always displays data from the same voice until a new note-on

### Design Decision: Note-On Only

We chose simple note-on tracking over auto-fallback (switching to another voice when tracked voice ends) because:
- Simpler implementation, fewer edge cases
- Most use cases only care about "latest note pressed"
- Auto-fallback would require integration with voice management (`hise::UnorderedStack`)

---

## Part 4: Rate Limiting

### The Problem

Many nodes call `triggerAsyncUpdate()` every audio buffer. At 44.1kHz/512 samples, that's ~86 triggers/second - wasteful when UI only needs ~30 FPS.

### The Solution

```cpp
setEnableRateLimiting(bool enabled, double targetFPS = 30.0, int triggersPerCallback = 1);
```

Calculates threshold from PrepareSpecs: `threshold = sampleRate / blockSize / targetFPS`

When enabled, `triggerAsyncUpdate()` increments a counter and only triggers the actual update when threshold is reached.

**Note:** `triggersPerCallback` parameter allows for nodes that call trigger multiple times per buffer (e.g., once per channel).

---

## Part 5: Global Cable Automatic Protection

### Implementation in RoutingNodes.h

Global cable data callbacks are automatically wrapped with `ScopedAllVoiceSetter`:

```cpp
// In global_cable_cpp_manager struct:
PolyHandler* polyHandler = nullptr;

void prepare(PrepareSpecs ps) {
    polyHandler = ps.voiceIndex;  // Store for later
    // ...
}

template <auto CableIndex> 
void registerDataCallback(const std::function<void(const var&)>& f) {
    // ...
    c.setDataCallback([this, f](const var& data) {
        if (polyHandler != nullptr) {
            PolyHandler::ScopedAllVoiceSetter avs(*polyHandler);
            f(data);
        } else {
            f(data);
        }
    });
}
```

**User Impact:** Zero. Existing code automatically gets protection.

---

## Part 6: Migration Guide

### Old API (Deprecated)

```cpp
// These are now stubs with deleted constructors:
class DllTimer { ... };           // DO NOT USE
class DllAsyncUpdater { ... };    // DO NOT USE
```

### Migration Steps

1. Change base class: `DllTimer` → `NodeUITimer` (or `NodeUIAsyncUpdater`)
2. Add in `prepare()`: `setupUIUpdater(specs);`
3. Rename callback: `timerCallback()` → `onUITimerCallback()` (or `handleAsyncUpdate()` → `onUIAsyncUpdate()`)

### Example Migration

**Before:**
```cpp
struct MyNode : public DllTimer {
    void prepare(PrepareSpecs specs) { /* ... */ }
    void timerCallback() override { /* ... */ }
};
```

**After:**
```cpp
struct MyNode : public NodeUITimer {
    void prepare(PrepareSpecs specs) {
        setupUIUpdater(specs);  // ADD THIS
        startTimerHz(30);
    }
    void onUITimerCallback() override { /* RENAMED */ }
};
```

---

## Part 7: Implementation Details

### Understanding `HI_EXPORT_AS_PROJECT_DLL`

This macro controls whether the code is being compiled as part of a **Project DLL** - a dynamically loaded library containing compiled scriptnode networks.

#### HISE Build Configurations

| Macro | Default | Description |
|-------|---------|-------------|
| `HI_EXPORT_AS_PROJECT_DLL` | 0 | Set to 1 when building Project DLL |
| `IS_STATIC_DSP_LIBRARY` | 1 | Set to 0 when loading networks from DLL |

#### The Project DLL Workflow

1. **Development:** Developer creates scriptnode networks in HISE
2. **Export:** HISE exports networks to a separate Projucer project (`DspNetworkCompileExporter`)
3. **Compilation:** The DLL project is compiled with `HI_EXPORT_AS_PROJECT_DLL=1`
4. **Runtime:** Host application loads the DLL via `DynamicLibraryHostFactory`

#### Why This Matters for UIUpdater

**When `HI_EXPORT_AS_PROJECT_DLL=0` (Normal Build):**
- Code runs in the same process as JUCE
- Direct access to JUCE's message thread
- Can use `juce::Timer` and `juce::AsyncUpdater` directly

**When `HI_EXPORT_AS_PROJECT_DLL=1` (Project DLL):**
- Code runs inside a separate DLL binary
- **No direct access to the host's JUCE message thread**
- The DLL has no knowledge of the host's event loop
- Must use custom implementations that work across the DLL boundary

#### DLL Timer/AsyncUpdater Implementation

In DLL builds, we provide:
- **Custom `TimerThread`:** A dedicated thread that sleeps and wakes to fire timer callbacks
- **Custom async updater:** Implementation that doesn't rely on JUCE's message manager

These are internal implementation details - the `NodeUITimer` and `NodeUIAsyncUpdater` APIs are identical regardless of build type.

### DLL vs Non-DLL Summary

| Build Type | Timer Implementation | AsyncUpdater Implementation |
|------------|---------------------|----------------------------|
| DLL (`HI_EXPORT_AS_PROJECT_DLL=1`) | Custom `TimerThread` | Custom implementation |
| Non-DLL (`HI_EXPORT_AS_PROJECT_DLL=0`) | Private inheritance from `juce::Timer` | Private inheritance from `juce::AsyncUpdater` |

**Private Inheritance (Non-DLL):** Hides original callback methods (`timerCallback`, `handleAsyncUpdate`) forcing users to use the new protected methods.

### Setup Assertions

All start/trigger methods include assertions:
```cpp
void startTimer(int ms) {
    jassert(prepareSpecs && "Call setupUIUpdater() in prepare() before starting timer");
    // ...
}
```

This catches the common mistake of forgetting `setupUIUpdater()`.

---

## Part 8: Key Insights & Best Practices

### Insight 1: PolyData Race Conditions Are Silent

The bug doesn't crash - it just gives wrong results. A UI might show data from voice 3 when you expected all voices. This makes it extremely hard to debug without understanding the mechanism.

**Best Practice:** Always use NodeUITimer/NodeUIAsyncUpdater for any UI code that might touch PolyData, even if you're not sure.

### Insight 2: PrepareSpecs Contains Everything Needed

`PrepareSpecs` has:
- `voiceIndex` - the `PolyHandler*` for voice protection
- `sampleRate` and `blockSize` - needed for rate limiting calculations

**Best Practice:** Store `PrepareSpecs` directly rather than extracting individual members. Use `operator bool()` to check if setup was called.

### Insight 3: Private Inheritance Prevents Misuse

By privately inheriting from `juce::Timer`/`juce::AsyncUpdater`, we prevent users from:
- Overriding `timerCallback()` directly (bypassing protection)
- Calling base class methods that don't have protection

### Insight 4: Rate Limiting Should Be Opt-In

Not all async updates should be rate limited. Some legitimately need immediate updates (e.g., error displays). Making it opt-in with explicit `setEnableRateLimiting()` call is the right approach.

### Insight 5: Voice Tracking Complexity

Auto-fallback voice tracking (switching to another voice when tracked voice ends) was considered but rejected because:
- Requires hooking into voice end events
- Edge cases with voice stealing
- Most UI use cases only care about "latest note" anyway

**Future Consideration:** If needed, could integrate with `hise::SuspendableTimer::Manager` for suspend/resume support.

### Insight 6: Lock-Free Data Exchange Constraints

Using `std::atomic<T>` requires `T` to be trivially copyable. This is enforced at compile time:
```cpp
static_assert(std::is_trivially_copyable_v<T>, "...");
```

For complex data, users must design POD structs that capture the essential display state.

---

## Part 9: Testing Checklist

### Basic Functionality
- [ ] Timer starts and fires callbacks at expected rate
- [ ] Async updater triggers and coalesces updates
- [ ] `WithData<T>` correctly passes data from audio to UI thread

### Voice Protection
- [ ] PolyData iteration in callbacks covers all voices
- [ ] No race conditions under load (multiple voices + rapid UI updates)
- [ ] Voice tracking correctly filters `postData()` to tracked voice

### Rate Limiting
- [ ] High-frequency triggers coalesced to ~target FPS
- [ ] Counter resets correctly after callback
- [ ] Disabled rate limiting allows immediate updates

### Migration
- [ ] Old `DllTimer`/`DllAsyncUpdater` usage produces clear compiler errors
- [ ] Migration path is straightforward (base class + setup call + rename)

### Edge Cases
- [ ] Callback before `prepare()` called (should hit assertion in debug)
- [ ] `polyHandler` is nullptr (callbacks still work, just without protection)
- [ ] Nested `ScopedAllVoiceSetter` (harmless - stores/restores previous thread)

---

## Appendix: Quick Reference

### Class Selection Guide

| Use Case | Class |
|----------|-------|
| Fixed-rate UI refresh (meters, scopes) | `NodeUITimer` |
| Event-driven updates | `NodeUIAsyncUpdater` |
| High-frequency triggers needing coalescing | `NodeUIAsyncUpdater` + `setEnableRateLimiting()` |
| Need to pass data to UI | `NodeUITimer::WithData<T>` or `NodeUIAsyncUpdater::WithData<T>` |
| Multi-voice display stability | Any of above + `setActiveVoiceForUIUpdates()` |

### Method Reference

| Method | Class | Purpose |
|--------|-------|---------|
| `setupUIUpdater(PrepareSpecs)` | All | **Required** - call in `prepare()` |
| `startTimer(int ms)` | Timer | Start timer with interval in ms |
| `startTimerHz(double hz)` | Timer | Start timer with frequency |
| `stopTimer()` | Timer | Stop timer |
| `triggerAsyncUpdate()` | AsyncUpdater | Request async callback |
| `setEnableRateLimiting(...)` | AsyncUpdater | Enable/configure rate limiting |
| `setActiveVoiceForUIUpdates()` | All | Enable voice tracking, capture current voice |
| `postData(const T&)` | WithData | Audio thread: post data (checks voice tracking) |
| `consumeData(T&)` | WithData | UI thread: consume posted data |

### Callback Methods to Override

| Old Method | New Method |
|------------|------------|
| `timerCallback()` | `onUITimerCallback()` |
| `handleAsyncUpdate()` | `onUIAsyncUpdate()` |

---

## Part 10: Test Coverage Roadmap

### Implementation Status

The multi-threaded test framework is **implemented** in `hi_dsp_library/unit_test/uiupdater_tests.cpp`.

**File Location:** `hi_dsp_library/unit_test/uiupdater_tests.cpp`
**Included via:** `hi_dsp_library/hi_dsp_library_01.cpp` (inside `#if HI_RUN_UNIT_TESTS` block)

See `unit-testing.md` for the pattern documentation and reusable templates.

### Key Lessons Learned During Implementation

| Issue | Solution |
|-------|----------|
| **Tests running 15x** | Moved test code from header to `.cpp` file - headers compile once per translation unit |
| **MSVC CTAD error** | Class template argument deduction guides don't work in class scope on MSVC. Used helper functions instead: `template<typename F> static AudioThreadT<F> AudioThread(F&& f)` |
| **Static destruction crash** | Test nodes (Timer/AsyncUpdater) as class members crashed in `CriticalSection::enter()` during shutdown. Fixed by using `ScopedPointer<>` and explicitly destroying at end of `runTest()` |
| **PrepareSpecs assertion** | `PrepareSpecs::operator bool()` checks `numChannels`. Must set `specs.numChannels = 1` (or 2) in all test specs |
| **Test validation** | Used "stupid bug" technique - introduced obvious bug to verify test infrastructure catches failures |

### COMPLETED: Test Infrastructure

Implemented in `uiupdater_tests.cpp`:

1. [x] `TestData` struct (shared: `sequence`, `value`, `voiceIndex`)
2. [x] `TestAsyncUpdater` class (with `onCallback` hook, inherits `WithData<TestData>`)
3. [x] `TestTimer` class (with `onCallback` hook, inherits `WithData<TestData>`)
4. [x] `AudioThreadT<F>`, `UIThreadT<F>`, `StopWhenT<F>` wrapper types with helper functions (MSVC workaround)
5. [x] `ThreadedTestResult` struct
6. [x] `MockAudioThread` nested class using `juce::Thread`
7. [x] `NodeUIUpdaterTests` class with:
   - [x] Class members: `polyHandler`, `asyncNode`, `timerNode`, `activeCallback`
   - [x] `createDefaultSpecs()` method
   - [x] `setupAsyncTest(rateLimiting, fps, triggersPerCallback)` method
   - [x] `setupTimerTest(frequencyHz)` method
   - [x] `expectWithinRange(value, min, max, message)` method
   - [x] `runThreadedTest(AudioThread, UIThread, StopWhen)` method
   - [x] `runThreadedTestWithExistingCallback()` variant for timer tests
8. [x] `testThreadedTestFramework()` validation test (runs first)

### COMPLETED: Single-Threaded Tests

| Test | Status |
|------|--------|
| `testRateLimitingCalculation` | [x] Implemented |
| `testRateLimitingCounter` | [x] Implemented |
| `testVoiceTrackingDisabled` | [x] Implemented |
| `testVoiceTrackingEnabled` | [x] Implemented |
| `testWithDataPostConsume` | [x] Implemented |
| `testWithDataMultiplePosts` | [x] Implemented |
| `testWithDataNoData` | [x] Implemented |
| `testWithDataTimerSingleThreaded` | [x] Implemented |

### COMPLETED: Additional Single-Threaded Tests

| Test | Code Path Covered |
|------|-------------------|
| [x] `testSetActiveVoiceWithNullPolyHandler` | `setActiveVoiceForUIUpdates()` when `voiceIndex == nullptr` |
| [x] `testRateLimitingWithInvalidSpecs` | `setEnableRateLimiting(true)` with sampleRate=0 or blockSize=0 |
| [x] `testTriggerAsyncUpdateNoRateLimiting` | `triggerAsyncUpdate()` without rate limiting |
| [x] `testCallWithVoiceProtectionNullHandler` | `callWithVoiceProtection()` when `voiceIndex == nullptr` |
| [x] `testTimerStartStopRunning` | `startTimer()` -> `isTimerRunning()` -> `stopTimer()` |

### COMPLETED: Multi-Threaded Tests

| Test | Code Path Covered |
|------|-------------------|
| [x] `testAsyncUpdateUnderLoad` | Basic async update flow |
| [x] `testAsyncUpdateRateLimitedUnderLoad` | Rate limiting reduces UI callbacks |
| [x] `testAsyncUpdateNoRateLimitingUnderLoad` | Non-rate-limited path |
| [x] `testTimerUnderLoad` | Timer callbacks at expected rate |
| [x] `testVoiceProtectionUnderLoad` | `ScopedAllVoiceSetter` active during callback |
| [x] `testVoiceTrackingUnderLoad` | Voice filtering under concurrency |
| [x] `testWithDataTimerUnderLoad` | Timer data integrity |
| [x] `testWithDataAsyncUnderLoad` | AsyncUpdater data integrity |
| [x] `testVoiceTrackingBlocksPostData` | `postData()` filtered by voice tracking |
| [x] `testVoiceTrackingBlocksTrigger` | `triggerAsyncUpdate()` filtered by voice tracking |

### Code Paths NOT Tested (DLL-only)

DLL-specific paths (`HI_EXPORT_AS_PROJECT_DLL=1`) covered by code review:
- `TimerThread` class
- DLL versions of `execute()`, `startTimer()`, `stopTimer()`

---

## Part 11: PolyData Voice Caching (Performance Optimization)

### The Two "ScopedVoiceSetter" Classes

There are two similarly-named but very different classes. Understanding the distinction is critical:

| Class | Location | Purpose |
|-------|----------|---------|
| `PolyHandler::ScopedVoiceSetter` | `snex_Types.h` | Sets which voice is active in the audio thread |
| `PolyData::ScopedVoiceIndexCacher` | `snex_Types.h` | Caches voice pointer for zero-overhead `get()` calls |

**Note:** `PolyData::ScopedVoiceSetter` is a deprecated alias for `ScopedVoiceIndexCacher`, kept for backwards compatibility.

### Why This Optimization Exists

`PolyData::get()` returns a reference to the current voice's data. Without caching, each call to `get()` must:

1. Call `begin()` which calls `voicePtr->getVoiceIndex()`
2. `getVoiceIndex()` performs:
   - Thread ID check: `Thread::getCurrentThreadId() == currentAllThread`
   - Atomic load: `voiceIndex.load()`
   - Multiplication: `* enabled`

In tight per-sample loops where `get()` is called hundreds of times per audio buffer, this overhead is significant - **up to 40% additional CPU usage**.

### How ScopedVoiceIndexCacher Works

```cpp
// Without cacher - get() calls begin() every time
for (int i = 0; i < numSamples; i++)
{
    auto& data = polyData.get();  // Calls begin() -> getVoiceIndex() each iteration
    data.process(samples[i]);
}

// With cacher - get() returns cached pointer
{
    PolyData<T, NV>::ScopedVoiceIndexCacher cacher(polyData, false);
    
    for (int i = 0; i < numSamples; i++)
    {
        auto& data = polyData.get();  // Returns cached pointer directly
        data.process(samples[i]);
    }
}
```

The cacher:
1. At construction: calls `begin()` once, stores result in `currentRenderVoice`
2. During scope: `get()` returns `currentRenderVoice` directly (no computation)
3. At destruction: resets `currentRenderVoice` to nullptr

### The SN_VOICE_SETTER Macro System

Nodes don't manually create `ScopedVoiceIndexCacher`. Instead, they declare their PolyData member:

```cpp
struct oscillator : public polyphonic_base
{
    PolyData<OscData, NumVoices> voiceData;
    SN_VOICE_SETTER(oscillator, voiceData);  // Generates VoiceSetter type
    
    // ...
};
```

The `SN_VOICE_SETTER` macro generates a `VoiceSetter` struct that creates a `ScopedVoiceIndexCacher`:

```cpp
#define SN_VOICE_SETTER(className, polyDataMember) struct VoiceSetter { \
    using FT = decltype(polyDataMember); \
    template <typename WT> VoiceSetter(WT& obj) : \
        svs(obj.getWrappedObject().polyDataMember), \
        updater(obj.getWrappedObject()) {}; \
    data::updater<className> updater; \
    operator bool() const { return true; } \
    typename FT::ScopedVoiceIndexCacher svs; }
```

Container wrappers automatically create `VoiceSetter` around all processing calls:

```cpp
// In processors.h - wraps every process() call
void process(ProcessDataDyn& data) noexcept
{
    if (auto sv = typename T::ObjectType::VoiceSetter(obj.getObject()))
        obj.process(fd);
}
```

### AccessType::ForceAllVoices

`forEachCurrentVoice()` accepts an `AccessType` parameter that controls iteration:

| AccessType | Effect on forEachCurrentVoice() |
|------------|--------------------------------|
| `RequireCached` (default) | Single-voice branch uses cached `get()` |
| `AllowUncached` | Single-voice branch uses uncached `get()` without assertions |
| `ForceAllVoices` | Iterates ALL voices regardless of current context |

Use `ForceAllVoices` for operations that must affect all voices unconditionally:

```cpp
void clearFilterState()
{
    filterState.forEachCurrentVoice([](FilterStateArray& voiceState)
    {
        voiceState.fill(0.0f);
    }, PolyHandler::AccessType::ForceAllVoices);
}
```

### Summary: Voice Context Flow

```
Audio Thread                          UI Thread
============                          =========

PolyHandler::ScopedVoiceSetter(5)     PolyHandler::ScopedAllVoiceSetter
    |                                     |
    v                                     v
voiceIndex = 5                        isAllThread() = true
    |                                     |
    v                                     v
ScopedVoiceIndexCacher                for(auto& d : polyData)
    |                                   -> iterates all 256 voices
    v
currentRenderVoice = &data[5]
    |
    v
polyData.get() -> returns cached ptr
polyData.begin()/end() -> voice 5 only
```

### Test Coverage

PolyData tests are included in `uiupdater_tests.cpp`:

| Category | Tests |
|----------|-------|
| Construction & Setup | `testPolyDataDefaultConstruction`, `testPolyDataValueConstruction`, `testPolyDataPrepare`, `testPolyDataSetAll` |
| Iteration | `testPolyDataBeginEndSingleVoice`, `testPolyDataBeginEndAllVoices`, `testPolyDataRangeForSingleVoice`, `testPolyDataRangeForAllVoices` |
| get() | `testPolyDataGetReturnsCurrentVoice`, `testPolyDataGetWithCacher`, `testPolyDataGetWithoutCacher` |
| forEachCurrentVoice | `testPolyDataForEachSingleVoice`, `testPolyDataForEachAllVoices`, `testPolyDataForEachForceAll`, `testForEachCurrentVoiceWithAccessType` |
| Utility Methods | `testPolyDataIsFirst`, `testPolyDataGetFirst`, `testPolyDataGetVoiceIndexForData`, `testPolyDataGetWithIndex`, `testPolyDataIsMonophonicOrInsideVoiceRendering`, `testPolyDataIsVoiceRenderingActive`, `testPolyDataIsPolyHandlerEnabled` |
| WithData Integration | `testWithDataPostsFromPolyDataGet`, `testWithDataAggregatesPolyDataVoices` |
| Multi-Threaded | `testPolyDataCacherUnderLoad`, `testPolyDataUIIterationUnderLoad` |
| Thread Safety Scenarios | `testScenario6RaceWithoutProtection`, `testScenario6FixWithScopedAllVoiceSetter`, `testScopedAllVoiceSetterNestingSafe`, `testScenario7AudioThreadNoProtectionNeeded` |
| Debug Counter (debug builds only) | `testPolyDataAllowUncachedNoAssertion`, `testPolyDataCounterResetOnCachedAccess`, `testPolyDataCachedAccessNoAssertion` |

---

## Part 12: Enforcing ScopedVoiceIndexCacher Usage

### The Problem: Uncached Access in Hot Paths

Without `ScopedVoiceIndexCacher`, every call to `PolyData::get()` performs expensive operations (atomic loads, thread ID checks). In per-sample loops, this adds ~40% CPU overhead. However, there was no mechanism to guide developers toward using `SN_VOICE_SETTER`.

### Solution: Debug Counter + AccessType Enum

#### PolyHandler::AccessType Enum

```cpp
enum class AccessType
{
    /** Default. Requires ScopedVoiceIndexCacher for optimal performance.
     *  Triggers jassertfalse in debug builds if called >64 times without caching. */
    RequireCached,

    /** Explicitly allows uncached access without debug assertions.
     *  Use for control-rate code, event handlers, or heavy processing. */
    AllowUncached
};
```

#### Updated get() Signature

```cpp
T& get(PolyHandler::AccessType accessType = PolyHandler::AccessType::RequireCached) const;
```

#### Debug Counter Behavior

In debug builds, `get()` tracks uncached calls:

1. If `currentRenderVoice == nullptr` (no cacher active) and `accessType == RequireCached`:
   - Increment counter
   - If counter > 64, fire `jassertfalse` with explanatory comment
   - Reset counter to avoid spam
2. If cacher is active, reset counter to 0
3. `ScopedVoiceIndexCacher` destructor also resets counter

### When to Use Each AccessType

| Scenario | AccessType | Reasoning |
|----------|------------|-----------|
| Per-sample processing loops | `RequireCached` (default) | 40% overhead is unacceptable |
| Parameter setters | `AllowUncached` | Called infrequently |
| `handleHiseEvent()` | `AllowUncached` | Per-event, not per-sample |
| `handleModulation()` | `AllowUncached` | Per-block, not per-sample |
| `reset()`, `prepare()` | `AllowUncached` | Initialization only |
| Neural network / FFT / Convolution | `AllowUncached` | Heavy payload dwarfs lookup cost |

### Cost-Benefit Analysis

When deciding whether to add `SN_VOICE_SETTER` to a node:

**Evaluate:** What percentage of the processing time is the `get()` lookup vs. actual computation?

| Processing Payload | Lookup Overhead | Decision |
|-------------------|-----------------|----------|
| Trivial (single multiply, simple state) | ~40% of total | Use `SN_VOICE_SETTER` |
| Light (basic DSP, filters) | ~10-20% of total | Use `SN_VOICE_SETTER` |
| Heavy (FFT, convolution, neural net) | <1% of total | Use `AllowUncached` |

**Rule of Thumb:** If your `processFrame()` does more than a few multiplies/adds, the lookup overhead becomes negligible. If your processing is trivial (like `OpNode` which just multiplies by a value), `SN_VOICE_SETTER` is critical.

### SN_VOICE_SETTER_2 for Multiple PolyData Members

Some nodes have two PolyData members accessed in hot paths (e.g., `fm` with `oscData` and `modGain`). Use `SN_VOICE_SETTER_2`:

```cpp
struct fm : public HiseDspBase
{
    PolyData<OscData, NUM_POLYPHONIC_VOICES> oscData;
    PolyData<double, NUM_POLYPHONIC_VOICES> modGain;

    SN_VOICE_SETTER_2(fm, oscData, modGain);  // Caches both members
    
    void processFrame(FrameDataType& d)
    {
        auto& od = oscData.get();  // Fast - cached by VoiceSetter
        auto& mg = modGain.get();  // Fast - also cached
        // ...
    }
};
```

### Nodes Updated with SN_VOICE_SETTER

| Node | Member(s) | Macro |
|------|-----------|-------|
| `oscillator<NV>` | `voiceData` | `SN_VOICE_SETTER` |
| `FilterNodeBase<NV>` | `filter` | `SN_VOICE_SETTER` |
| `mod_base<NV>` | `state` | `SN_VOICE_SETTER` |
| `jwrapper<NV>` | `objects` | `SN_VOICE_SETTER` |
| `mod_voice_checker_base<NV>` | `state` | `SN_VOICE_SETTER` |
| `gain<V>` | `gainer` | `SN_VOICE_SETTER` |
| `OpNode<V>` | `value` | `SN_VOICE_SETTER` |
| `simple_ar<NV>` | `states` | `SN_VOICE_SETTER` |
| `ahdsr<NV>` | `states` | `SN_VOICE_SETTER` |
| `ramp<NV>` | `state` | `SN_VOICE_SETTER` |
| `file_player<NV>` | `state`, `currentXYZSample` | `SN_VOICE_SETTER_2_WITH_DATA_LOCK` |
| `fm` | `oscData`, `modGain` | `SN_VOICE_SETTER_2` |

### Nodes Using AllowUncached

| Node | Method | Reasoning |
|------|--------|-----------|
| `event_data_reader` | `handleModulation()`, `handleHiseEvent()` | Per-event/block |
| `event_data_writer` | `handleHiseEvent()` | Per-event |
| `timer` | `handleModulation()` | Per-block |
| `neural<NV>` | `process()` | Heavy neural network computation |

### Debug Counter Test Coverage

Tests in `uiupdater_tests.cpp` (debug builds only):

| Test | Scenario |
|------|----------|
| `testPolyDataAllowUncachedNoAssertion` | 100+ calls with `AllowUncached` - no assertion |
| `testPolyDataCounterResetOnCachedAccess` | Counter resets when cacher used mid-sequence |
| `testPolyDataCachedAccessNoAssertion` | 1000+ calls with cacher active - no assertion |

---

## Part 13: External Data Locking and Thread Safety

### Thread Context for Node Methods

Understanding which thread calls each method is critical for writing safe, lock-free audio code:

| Method | Thread | Must Be Lock-Free | Notes |
|--------|--------|-------------------|-------|
| `process()` / `processFrame()` | Audio | **YES** | Hot path - per-sample/block |
| `handleHiseEvent()` | Audio | **YES** | Per-event |
| `handleModulation()` | Audio | **YES** | Per-block |
| `reset()` | Audio | **YES** | Called during `startVoice()` |
| `prepare()` | Various | No | **Never concurrent with audio**; called with audio suspended during setup/reconfiguration |
| `setExternalData()` | Message | No | Called under write lock by caller |
| `createParameters()` | Message | No | Setup only |
| `setXxx()` (parameters) | Both | **YES** | Called from UI or via modulation |

**Key guarantee:** `prepare()` is always called with audio processing suspended. The HISE framework ensures this via `killVoicesAndCall()` or equivalent suspension mechanisms before invoking `prepare()`. This means:
- No race conditions with `process()`, `handleHiseEvent()`, or other audio-thread methods
- No need for `ScopedAllVoiceSetter` protection in `prepare()`
- Safe to iterate all voices directly (e.g., to initialize state)

**Critical:** `reset()` is called from the audio thread during voice start via `startVoice()` in `snex_Types.h`. Any external data access in `reset()` requires manual locking.

### External Data Protection

Nodes inheriting from `data::base` can have external data (audio files, tables, filter coefficients) that is:
- **Read** during audio processing
- **Written** from the message thread (e.g., loading a new audio file)

Without protection, this is a data race.

### DataTryReadLock: Non-Blocking Protection

`DataTryReadLock` is a **non-blocking** read lock for audio thread use:

```cpp
if (auto dt = DataTryReadLock(this))
{
    // Lock acquired - safe to access external data
    processAudioData();
}
// Lock not acquired - skip processing this buffer
```

| Lock Acquired? | Behavior |
|----------------|----------|
| Yes | Process normally with data access |
| No (writer active) | Skip processing this buffer |

**Why skipping is acceptable:** Missing one audio buffer (~1-5ms) is imperceptible. Blocking the audio thread causes audible dropouts and is never acceptable.

**Reentrant behavior:** The lock uses a counter-based shared lock implementation. Multiple nested `DataTryReadLock` acquisitions from the same thread are safe.

### Write Lock Acquisition

The write lock is acquired by **higher-level code** (not the node) before calling `setExternalData()`:

```cpp
// Called from message thread when loading new audio file
SimpleReadWriteLock::ScopedWriteLock sl(data->getDataLock());
node.setExternalData(newData, index);
```

The node's `setExternalData()` asserts that the write lock is held:

```cpp
virtual void setExternalData(const snex::ExternalData& d, int index)
{
    jassert(d.obj == nullptr || d.obj->getDataLock().writeAccessIsLocked() 
            || d.obj->getDataLock().writeAccessIsSkipped());
    externalData = d;
}
```

**Important:** Write locks are only acquired from the message thread, where blocking is acceptable. The audio thread only ever uses non-blocking `DataTryReadLock`.

### SN_VOICE_SETTER Macro Variants

| Macro | PolyData Count | Data Lock | Use Case |
|-------|----------------|-----------|----------|
| `SN_VOICE_SETTER` | 1 | No | No external data access in processing |
| `SN_VOICE_SETTER_WITH_DATA_LOCK` | 1 | Yes | External data accessed in processing |
| `SN_VOICE_SETTER_2` | 2 | No | Two PolyData, no external data |
| `SN_VOICE_SETTER_2_WITH_DATA_LOCK` | 2 | Yes | Two PolyData + external data (e.g., `file_player`) |

### How the Data Lock Works in VoiceSetter

The `_WITH_DATA_LOCK` variants add `DataTryReadLock` and change `operator bool()`:

```cpp
#define SN_VOICE_SETTER_2_WITH_DATA_LOCK(className, poly1, poly2) struct VoiceSetter { \
    // ... voice cachers ...
    DataTryReadLock sl; \
    operator bool() const { return (bool)sl; }  // Returns lock status
    // ...
}
```

The wrapper skips processing if the lock fails:

```cpp
// In wrap::node::process()
if (auto sv = typename T::ObjectType::VoiceSetter(obj.getObject()))
    obj.process(d);  // Only runs if lock acquired (and other conditions met)
```

### When Manual Locking is Still Required

Methods called **directly** (not through the wrapper) need manual locking if they access external data:

- `reset()` - called from `prepare()`, `startVoice()`, parameter setters
- Parameter setters that access external data
- Any helper methods that read external data

**Example pattern from `file_player::reset()`:**

```cpp
void reset()
{
    for (auto& s : state)
    {
        if (mode != PlaybackModes::MidiFreq)
        {
            // Manual lock required here because reset() is called directly (not through
            // the wrapper's VoiceSetter) from prepare(), startVoice(), and setPlaybackMode().
            // This lock is reentrant for read access, so it's safe even if the VoiceSetter
            // already holds a read lock in some call paths.
            if (auto dt = DataTryReadLock(this))
            {
                auto& cd = currentXYZSample.get(PolyHandler::AccessType::AllowUncached);
                HiseEvent e(HiseEvent::Type::NoteOn, 64, 1, 1);

                if (this->externalData.getStereoSample(cd, e))
                    s.uptimeDelta = cd.getPitchFactor();
            }

            // Always reset playback position, regardless of lock status
            s.uptime = 0.0;
        }
    }
}
```

### Parameter Setters and Voice Context

Parameter setters (`setFrequency()`, `setGain()`, etc.) are called from:
- **Audio thread** via `wrap::mod` modulation (per-frame in some setups)
- **Message thread** via UI controls

When called via modulation during `process()`, the outer `wrap::node` VoiceSetter IS active. However, parameter setters using `for(auto& s : polyState)` **bypass the cached voice pointer** because `begin()/end()` calls `getVoiceIndex()` directly.

**Recommendation:** Use `forEachCurrentVoice()` in parameter setters:

```cpp
// Slow - bypasses voice cache:
void setFrequency(double f)
{
    for(auto& s : voiceData)  // Calls begin() -> getVoiceIndex() each time
        s.frequency = f;
}

// Fast - uses cached pointer when VoiceSetter active:
void setFrequency(double f)
{
    voiceData.forEachCurrentVoice([f](OscData& d)
    {
        d.frequency = f;
    });
}
```

See **CRITICAL TODO** below for the full scope of this issue.

---

## COMPLETED: forEachCurrentVoice() API Consistency and forceAll Removal

### What Was Done

1. **Added `AccessType` parameter to `forEachCurrentVoice()`** for API consistency with `get()`:

   ```cpp
   template <typename F> 
   void forEachCurrentVoice(F&& f, 
       PolyHandler::AccessType accessType = PolyHandler::AccessType::RequireCached);
   ```

2. **Added `ForceAllVoices` to `PolyHandler::AccessType` enum** - replaces the old 
   `forceAll` flag mechanism. Callers now express intent directly at the call site:

   ```cpp
   filterState.forEachCurrentVoice([](FilterStateArray& voiceState)
   {
       voiceState.fill(0.0f);
   }, PolyHandler::AccessType::ForceAllVoices);
   ```

3. **Removed `forceAll` flag entirely:**
   - Removed `bool forceAll` member from `PolyData`
   - Removed `forceAll` parameter from `ScopedVoiceIndexCacher` constructor
   - Simplified all `SN_VOICE_SETTER*` macros (removed `forceAll` parameter)
   - Simplified `VoiceSetter` constructors in Containers.h and processors.h

4. **Updated `neural::clearFilterState()`** to use `forEachCurrentVoice(..., ForceAllVoices)`.
   This also fixed a latent bug: the old code used `ScopedVoiceIndexCacher(filterState, true)` 
   with `for(auto& : filterState)`, but `forceAll` never affected `begin()/end()` - it only 
   affected `forEachCurrentVoice()`. The old code worked by accident (always called from 
   all-voices context). The new code works correctly in all contexts.

### Files Modified

| File | Changes |
|------|---------|
| `snex_Types.h` | Added `ForceAllVoices` to enum, added `AccessType` param to `forEachCurrentVoice()`, removed `forceAll` from `ScopedVoiceIndexCacher` and `PolyData` |
| `node_macros.h` | Removed `forceAll` parameter from all `SN_VOICE_SETTER*` macros |
| `Containers.h` | Removed `forceAll` from `base_wrapper`, `sub_tuple_helper_impl`, `DummyVoiceSetter`, container `VoiceSetter` |
| `processors.h` | Removed `false` argument from 4 `VoiceSetter` calls |
| `MathNodes.h` | Updated `neural::clearFilterState()` to use `forEachCurrentVoice(..., ForceAllVoices)` |
| `uiupdater_tests.cpp` | Updated `testPolyDataForEachForceAll`, added `testForEachCurrentVoiceWithAccessType` |

### Remaining TODO: Parameter Setter Migration

**Update all parameter setters** to use `forEachCurrentVoice()` instead of 
`for(auto& s : polyState)`. Found 36 instances across:
- CoreNodes.h (12 instances)
- EnvelopeNodes.h (6 instances)
- CableNodes.h (2 instances)
- ModulationNodes.h (4 instances)
- DynamicsNode.cpp (2 instances)
- StretchNode.h (10 instances)

This should be done BEFORE widespread adoption of the debug counter system, as the
counter will fire on all the parameter setters using the slow path.

**Consider deprecating `for(auto& s : polyState)` syntax** entirely for PolyData,
as it provides no control over AccessType. This is a larger API change requiring
careful consideration.

---

## COMPLETED: Thread Safety Fix for setAttribute() Paths

### The Problem: Scenario #6 Race Condition

When `setAttribute()` / `setInternalAttribute()` is called from the UI/message thread,
the code eventually reaches scriptnode parameter setters that iterate `PolyData` via
`forEachCurrentVoice()` or `for(auto& s : polyState)`. Without `ScopedAllVoiceSetter`,
the UI thread reads the raw `voiceIndex` atomic, which may contain a stale value set
by the audio thread's `ScopedVoiceSetter`.

**The race condition:**

```
Audio thread:                          UI thread:
ScopedVoiceSetter(polyHandler, 5)      
  voiceIndex = 5                       
                                       // No ScopedAllVoiceSetter!
                                       forEachCurrentVoice(...)
                                       // getVoiceIndex() returns 5
                                       // Only voice 5 updated!
```

This is non-deterministic: if the audio thread happens to be between voices
(`voiceIndex = -1`), all voices are updated correctly. If mid-voice rendering,
only that voice is updated.

### Why ScopedAllVoiceSetter Exists

`ScopedAllVoiceSetter` is fundamentally a **thread safety mechanism**, not an
"all voices" semantic. It forces `getVoiceIndex()` to return -1 for the calling
thread by checking thread ID, bypassing the potentially racy `voiceIndex` atomic.

| Context | ScopedAllVoiceSetter needed? | Why |
|---------|------------------------------|-----|
| Audio thread (inside voice rendering) | NO | Same thread as `voiceIndex` writer, no race |
| Audio thread (before voice rendering, e.g., MIDI CC) | NO | `voiceIndex = -1` (correct), same thread, no race |
| UI/message thread | **YES** | Different thread, `voiceIndex` may be stale |
| Any other non-audio thread | **YES** | Same reasoning as UI thread |

### Key Architectural Insight

`setAttribute()` / `setInternalAttribute()` is **never** called from within voice
rendering context (while `ScopedVoiceSetter` is active). All calls originate from:

- **UI/message thread:** Parameter changes, preset loads, drag handlers
- **Audio thread before voice rendering:** MIDI automation, macro controls, voice start events

Modulation during voice rendering uses a different path (`copyModulationValues()`) 
that operates on float buffers, not `setAttribute()`.

This means `ScopedAllVoiceSetter` can be added unconditionally to `setInternalAttribute()`
implementations without risk of Scenario #1 (nested `ScopedAllVoiceSetter` inside
`ScopedVoiceSetter`).

### Scenario Analysis

During investigation, we analyzed 7 failure scenarios:

| Scenario | Description | Verdict |
|----------|-------------|---------|
| #1 | Nested `ScopedAllVoiceSetter` inside `ScopedVoiceSetter` (audio thread) | CRITICAL BUG if it occurs |
| #2 | Missing `ScopedAllVoiceSetter` on UI thread (voiceIndex happens to be -1) | Lucky, not safe |
| #3 | Stale voiceIndex between voices (audio thread) | Design consideration |
| #4 | `ScopedVoiceSetter` on UI thread | CRITICAL BUG if it occurs |
| #5 | Concurrent access during `ScopedAllVoiceSetter` | Correct behavior (by design) |
| #6 | UI thread without `ScopedAllVoiceSetter`, audio thread mid-voice | **CONFIRMED BUG** |
| #7 | Audio thread before voice rendering (MIDI CC) | Correct behavior, no protection needed |

### The Fix

Added `ScopedAllVoiceSetter` unconditionally in two locations:

**1. `HardcodedSwappableEffect::setHardcodedAttribute()`**
File: `hi_core/hi_modules/hardcoded/HardcodedModuleBase.cpp`

```cpp
else
{
    PolyHandler::ScopedAllVoiceSetter avs(polyHandler);
    p->callback.call((double)newValue);
}
```

**2. `FlexAhdsrEnvelope::setInternalAttribute()`**
File: `hi_core/hi_modules/modulators/mods/FlexAhdsrEnvelope.cpp`

```cpp
parameterIndex -= getParameterOffset();
parameters[parameterIndex] = newValue;

PolyHandler::ScopedAllVoiceSetter avs(polyHandler);

switch(parameterIndex)
{
    case 0: obj.template setParameter<0>(newValue); break;
    // ... cases 1-9
}
```

### Why No Branch / No `isAllThread()` Check

`ScopedAllVoiceSetter` safely nests (saves/restores `prevThread`), so:
- If already wrapped: harmless, overwrites with same thread ID, restores correctly
- If on audio thread before voice rendering: `voiceIndex = -1` anyway, wrapper is harmless
- If on UI thread without wrapper: **fixes the bug**

No conditional logic needed. No code duplication.

### Checklist: Other Classes That May Need This Fix

Any class with a `PolyHandler` member whose `setInternalAttribute()` calls scriptnode
parameter setters needs the same fix. Known classes:

| Class | Has `polyHandler`? | `setInternalAttribute()` touches PolyData? | Fixed? |
|-------|-------------------|-------------------------------------------|--------|
| `HardcodedSwappableEffect` | Yes | Yes (via `p->callback.call()`) | **YES** |
| `FlexAhdsrEnvelope` | Yes | Yes (via `obj.template setParameter<N>()`) | **YES** |
| `MatrixModulator` | Yes | No (uses `setValueInternal()` / `baseValue.prepare()`) | N/A |

### Test Coverage

Tests added to `uiupdater_tests.cpp`:

| Test | Scenario Covered |
|------|-----------------|
| `testScenario6RaceWithoutProtection` | Demonstrates the bug: UI thread reads stale voiceIndex |
| `testScenario6FixWithScopedAllVoiceSetter` | Confirms fix: ScopedAllVoiceSetter prevents race |
| `testScopedAllVoiceSetterNestingSafe` | Confirms nesting is safe (no branch needed) |
| `testScenario7AudioThreadNoProtectionNeeded` | Confirms audio thread before voice rendering is correct |