# PolyData & UIUpdater System

**Related:**
- [voice-setter-history.md](voice-setter-history.md) — Archive of the failed VoiceSetter chain optimization
- [unit-testing.md](unit-testing.md) — Test patterns and framework
- [cpp-architecture.md](cpp-architecture.md) — General C++ guidelines

---

## 1. PolyData Voice System

### What is PolyData

`PolyData<T, NumVoices>` stores per-voice state for polyphonic scriptnode nodes. Its iteration behavior depends on voice context:

| Context | `voiceIndex` | Iteration |
|---------|-------------|-----------|
| Audio thread inside `ScopedVoiceSetter` | >= 0 | Single voice |
| Audio thread before voice rendering | -1 | All voices |
| Non-audio thread with `ScopedAllVoiceSetter` | -1 (forced) | All voices |

### Access Patterns

| Pattern | When to Use |
|---------|-------------|
| `for(auto& d : polyData)` | Standard — voice-context-aware (single voice or all) |
| `polyData.get()` | Get reference to current voice's data |
| `for(auto& d : polyData.all())` | Force all voices, ignoring voice context |

### The Race Condition

When the audio thread is mid-voice-rendering with `voiceIndex >= 0`, a UI thread reading the same `PolyData` will see that stale `voiceIndex` and access only one voice instead of all. This is a **silent bug** — wrong behavior, no crash, hard to debug.

`ScopedAllVoiceSetter` fixes this by registering the calling thread so that `getVoiceIndex()` returns -1 for that thread regardless of what the audio thread has set.

| Context | Need `ScopedAllVoiceSetter`? | Why |
|---------|------------------------------|-----|
| Audio thread (inside voice rendering) | No | Same thread writes `voiceIndex`, no race |
| Audio thread (before voice rendering) | No | `voiceIndex = -1` already, same thread |
| UI/message thread | **Yes** | Different thread, `voiceIndex` may be stale |
| Any other non-audio thread | **Yes** | Same reasoning as UI thread |

`ScopedAllVoiceSetter` nests safely (saves/restores previous thread), so wrapping unconditionally is always correct.

### Performance

`getVoiceIndex()` uses a fast-path: when `currentAllThread` is nullptr (99.9% of the time — only non-null during knob drags, preset loads, etc.), the expensive `Thread::getCurrentThreadId()` call is skipped entirely. Cost on the audio thread hot path: ~2-6 cycles (two atomic loads).

### The 7 Thread Safety Scenarios

These scenarios document every possible interaction between `ScopedVoiceSetter` (audio thread), `ScopedAllVoiceSetter` (non-audio threads), and `PolyData` access.

**Background:**
- `PolyHandler::voiceIndex` (atomic int): Set by `ScopedVoiceSetter` on audio thread (-1 = no voice)
- `PolyHandler::currentAllThread` (atomic void*): Set by `ScopedAllVoiceSetter` to store thread ID
- `getVoiceIndex()`: Returns -1 if `currentAllThread == currentThreadId`, else `voiceIndex * enabled`

#### Scenario #1: Nested ScopedAllVoiceSetter Inside ScopedVoiceSetter (Audio Thread)

**Setup:** Audio thread is inside `ScopedVoiceSetter(voiceIndex=5)`. Code then creates `ScopedAllVoiceSetter` on the same thread.

**What happens:** `ScopedAllVoiceSetter` sets `currentAllThread` to the audio thread's ID. Now `getVoiceIndex()` returns -1 on the audio thread, overriding the voice-specific index. Audio processing uses wrong voice data.

**Verdict:** **CRITICAL BUG if it occurs.** Architectural analysis shows this should never happen because `setAttribute()` paths (where `ScopedAllVoiceSetter` is used) are never called from within voice rendering context. Modulation during voice rendering uses `copyModulationValues()`, not `setAttribute()`.

#### Scenario #2: Missing ScopedAllVoiceSetter on UI Thread (voiceIndex happens to be -1)

**Setup:** UI thread iterates `PolyData` without `ScopedAllVoiceSetter`. Audio thread happens to be between voices (`voiceIndex = -1`).

**What happens:** UI thread reads `voiceIndex = -1`, correctly iterates all voices.

**Verdict:** **Lucky, not safe.** Works by accident. If audio thread enters voice rendering between the check and iteration, behavior becomes incorrect.

#### Scenario #3: Stale voiceIndex Between Voices (Audio Thread)

**Setup:** Audio thread finishes voice 5, `ScopedVoiceSetter` destructs (`voiceIndex = -1`), hasn't started voice 6 yet.

**What happens:** Brief window where `voiceIndex = -1`. Another thread reading now sees -1 (correct for "all voices").

**Verdict:** **Design consideration.** Expected behavior, no bug.

#### Scenario #4: ScopedVoiceSetter on UI Thread

**Setup:** Someone accidentally uses `ScopedVoiceSetter` on the UI thread.

**What happens:** UI thread sets `voiceIndex = N`. Audio thread reads this stale value. Massive corruption — wrong voice data processed.

**Verdict:** **CRITICAL BUG if it occurs.** `ScopedVoiceSetter` must only be used on the audio thread.

#### Scenario #5: Concurrent Access During ScopedAllVoiceSetter

**Setup:** UI thread has `ScopedAllVoiceSetter` active. Audio thread is mid-voice rendering with `voiceIndex = 5`.

**What happens:** Audio thread's `getVoiceIndex()` returns 5 (thread ID != `currentAllThread`). UI thread's `getVoiceIndex()` returns -1 (thread ID matches). Each thread sees correct value.

**Verdict:** **Correct behavior (by design).** This is exactly what `ScopedAllVoiceSetter` is for.

#### Scenario #6: UI Thread Without ScopedAllVoiceSetter, Audio Thread Mid-Voice

**Setup:** UI thread calls parameter setter which iterates `PolyData`. No `ScopedAllVoiceSetter`. Audio thread is mid-voice with `voiceIndex = 5`.

**What happens:**
```
Audio thread:                          UI thread:
ScopedVoiceSetter(polyHandler, 5)
  voiceIndex = 5
                                       // No ScopedAllVoiceSetter!
                                       for(auto& s : polyState)
                                       // getVoiceIndex() returns 5
                                       // Only voice 5 updated!
```

Non-deterministic: works correctly when audio thread is between voices, fails when mid-voice.

**Verdict:** **CONFIRMED BUG — FIXED.** Fixed by adding `ScopedAllVoiceSetter` unconditionally in `HardcodedSwappableEffect::setHardcodedAttribute()` and `FlexAhdsrEnvelope::setInternalAttribute()`.

#### Scenario #7: Audio Thread Before Voice Rendering (MIDI CC, Automation)

**Setup:** Audio thread processes MIDI CC before entering voice rendering loop. `voiceIndex = -1`.

**What happens:** Parameter setter iterates all voices correctly. Same thread, no race.

**Verdict:** **Correct behavior, no protection needed.**

#### Summary

| # | Description | Verdict |
|---|-------------|---------|
| 1 | Nested `ScopedAllVoiceSetter` inside `ScopedVoiceSetter` | CRITICAL BUG if it occurs |
| 2 | Missing `ScopedAllVoiceSetter`, voiceIndex happens to be -1 | Lucky, not safe |
| 3 | Stale voiceIndex between voices | Expected, no bug |
| 4 | `ScopedVoiceSetter` on UI thread | CRITICAL BUG if it occurs |
| 5 | Concurrent access during `ScopedAllVoiceSetter` | Correct (by design) |
| 6 | UI thread without protection, audio mid-voice | **CONFIRMED BUG** (FIXED) |
| 7 | Audio thread before voice rendering | Correct, no protection needed |

---

## 2. External Data Locking

Nodes inheriting from `data::base` can have external data (audio files, tables, filter coefficients) that is read during audio processing and written from the message thread. Without protection, this is a data race.

### DataTryReadLock

`DataTryReadLock` is a **non-blocking** read lock for audio thread use:

```cpp
if (auto dt = DataTryReadLock(this))
{
    processAudioData();  // Lock acquired, safe to access external data
}
// Lock not acquired (writer active) — skip this buffer
```

Missing one audio buffer (~1-5ms) is imperceptible. Blocking the audio thread is never acceptable. The lock is reentrant — nested acquisitions from the same thread are safe.

Write locks are acquired by higher-level code before calling `setExternalData()`, always from the message thread where blocking is acceptable.

### Thread Context for Node Methods

| Method | Thread | Lock-Free? | Notes |
|--------|--------|------------|-------|
| `process()` / `processFrame()` | Audio | **Yes** | Hot path |
| `handleHiseEvent()` | Audio | **Yes** | Per-event |
| `reset()` | Audio | **Yes** | Called during `startVoice()` |
| `prepare()` | Various | No | Audio always suspended first |
| `setExternalData()` | Message | No | Write lock held by caller |
| `setXxx()` (parameters) | Both | **Yes** | UI or modulation |

**Key guarantees:**
- `prepare()` is always called with audio suspended (`killVoicesAndCall()`). Safe to iterate all voices directly.
- `reset()` is called from the audio thread during `startVoice()`. Any external data access needs manual `DataTryReadLock`.
- Parameter setters can be called from any thread. When from the message thread, `ScopedAllVoiceSetter` must be active for correct `PolyData` iteration.

---

## 3. NodeUIUpdater Classes

### Class Hierarchy

```
NodeUIUpdaterBase                    (PrepareSpecs storage, voice protection)
├── NodeUITimer                      (timer-based updates)
│   └── NodeUITimer::WithData<T>     (+ lock-free data exchange)
└── NodeUIAsyncUpdater               (event-driven updates + rate limiting)
    └── NodeUIAsyncUpdater::WithData<T>
```

All callbacks are automatically wrapped with `ScopedAllVoiceSetter` — safe to access `PolyData` without manual protection.

### When to Use Which

| Use Case | Class |
|----------|-------|
| Fixed-rate refresh (meters, scopes) | `NodeUITimer` |
| Event-driven, needs coalescing | `NodeUIAsyncUpdater` |
| Need to pass data audio -> UI | `WithData<T>` variant |
| Multi-voice display stability | Any + `setActiveVoiceForUIUpdates()` |

### NodeUITimer

```cpp
struct MyNode : public NodeUITimer
{
    void prepare(PrepareSpecs specs) {
        setupUIUpdater(specs);  // Required — stores PolyHandler for voice protection
        startTimerHz(30);
    }

    void onUITimerCallback() override {
        // Message thread, ScopedAllVoiceSetter active — safe to iterate PolyData
    }
};
```

### NodeUIAsyncUpdater

```cpp
struct MyNode : public NodeUIAsyncUpdater
{
    void prepare(PrepareSpecs specs) {
        setupUIUpdater(specs);
        setEnableRateLimiting(true, 30.0, 1);  // Coalesce to ~30 FPS
    }

    void process(ProcessDataDyn& data) {
        triggerAsyncUpdate();  // Called every buffer, rate-limited internally
    }

    void onUIAsyncUpdate() override { /* Message thread, voice-protected */ }
};
```

Rate limiting calculates threshold from PrepareSpecs: `sampleRate / blockSize / targetFPS`. The `triggersPerCallback` parameter handles nodes that trigger multiple times per buffer (e.g., once per channel).

### WithData\<T\>

Lock-free data exchange from audio to UI thread. `T` must be trivially copyable (`static_assert` enforced).

```cpp
struct DisplayData { float level; int state; };

struct MyNode : public NodeUITimer::WithData<DisplayData>
{
    void process(ProcessDataDyn& data) {
        postData({calculateLevel(), currentState});  // Audio thread
    }

    void onUITimerCallback() override {
        DisplayData d;
        if (consumeData(d))  // UI thread
            updateDisplay(d.level, d.state);
    }
};
```

### Voice Tracking

In polyphonic instruments, UI displays can "jump" between voices. Voice tracking pins the display to one voice:

```cpp
void handleHiseEvent(HiseEvent& e) {
    if (e.isNoteOn())
        setActiveVoiceForUIUpdates();  // Capture this voice
}

void process(ProcessDataDyn& data) {
    postData({...});  // Only posts if this is the tracked voice
}
```

### Global Cable Protection

Global cable data callbacks in `RoutingNodes.h` are automatically wrapped with `ScopedAllVoiceSetter`. No user action required.

---

## 4. Migration Guide

### Old API -> New API

| Old | New |
|-----|-----|
| `DllTimer` | `NodeUITimer` |
| `DllAsyncUpdater` | `NodeUIAsyncUpdater` |
| `timerCallback()` | `onUITimerCallback()` |
| `handleAsyncUpdate()` | `onUIAsyncUpdate()` |

### Steps

1. Change base class
2. Add `setupUIUpdater(specs)` in `prepare()`
3. Rename callback method

### SN_VOICE_SETTER Removal

Client code that used `SN_VOICE_SETTER` macros (any variant) should remove them entirely — these macros and the VoiceSetter chain infrastructure no longer exist.

If the node accesses external data (`data::base` subclass) in `process()`, `processFrame()`, `handleHiseEvent()`, or `reset()`, add manual `DataTryReadLock` to protect against concurrent writes:

```cpp
// Before (automatic locking via macro):
SN_VOICE_SETTER_2_WITH_DATA_LOCK(my_node, state, audioData);

// After (manual locking in each method that accesses external data):
template <typename T> void process(T& data)
{
    if (auto dt = DataTryReadLock(this))
    {
        // Safe to access external data
    }
    // else: skip this buffer
}
```

Nodes that only have `PolyData` members without external data access (the `SN_VOICE_SETTER` non-data-lock variants) need no replacement — just remove the macro.

---

## 5. DLL Build Considerations

When `HI_EXPORT_AS_PROJECT_DLL=1`, code runs inside a separate DLL binary with no access to the host's JUCE message thread. The `NodeUITimer` and `NodeUIAsyncUpdater` APIs are identical regardless of build type, but the implementation differs:

| Build | Timer | AsyncUpdater |
|-------|-------|--------------|
| Normal (`HI_EXPORT_AS_PROJECT_DLL=0`) | Private inheritance from `juce::Timer` | Private inheritance from `juce::AsyncUpdater` |
| DLL (`HI_EXPORT_AS_PROJECT_DLL=1`) | Custom `TimerThread` | Custom implementation |

Private inheritance hides `timerCallback()` and `handleAsyncUpdate()`, forcing use of the voice-protected `onUITimerCallback()` and `onUIAsyncUpdate()` overrides.

---

## 6. Quick Reference

### PolyData Access Patterns

| Pattern | When to Use |
|---------|-------------|
| `for(auto& d : polyData)` | Standard — voice-context-aware |
| `polyData.get()` | Current voice reference |
| `for(auto& d : polyData.all())` | Force all voices |

### UIUpdater Methods

| Method | Class | Purpose |
|--------|-------|---------|
| `setupUIUpdater(PrepareSpecs)` | All | **Required** in `prepare()` |
| `startTimer(int ms)` / `startTimerHz(double)` | Timer | Start timer |
| `stopTimer()` | Timer | Stop timer |
| `triggerAsyncUpdate()` | AsyncUpdater | Request callback |
| `setEnableRateLimiting(bool, fps, triggers)` | AsyncUpdater | Configure rate limiting |
| `setActiveVoiceForUIUpdates()` | All | Pin UI to current voice |
| `postData(const T&)` | WithData | Audio thread: post data |
| `consumeData(T&)` | WithData | UI thread: consume data |

### Voice Context Flow

```
Audio Thread                          UI Thread
============                          =========

ScopedVoiceSetter(polyHandler, 5)     ScopedAllVoiceSetter(polyHandler)
  voiceIndex = 5                        currentAllThread = thisThreadId

polyData.get()                        for(auto& d : polyData)
  -> getVoiceIndex()                    -> getVoiceIndex()
     currentAllThread == nullptr?          currentAllThread == thisThreadId?
     YES -> voiceIndex.load() = 5          YES -> return -1
  -> returns &data[5]                   -> iterates all voices
```
