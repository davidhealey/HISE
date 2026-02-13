# HISE Threading System

**Related Documentation:**
- [voice-setter-system.md](voice-setter-system.md) - UIUpdater and voice protection (scriptnode-specific threading)
- [cpp-architecture.md](cpp-architecture.md) - Architecture guidelines including threading precision

---

## Overview

HISE is a real-time audio application that must never block or stall on the audio thread. The codebase has a sophisticated threading system centered around `MainController::KillStateHandler` that coordinates safe cross-thread operations while maintaining glitch-free audio.

**Core principle:** Instead of locking (which causes audio dropouts), HISE uses a **suspend-and-resume** pattern. When a non-audio thread needs exclusive access to shared data, the audio thread gracefully kills all voices, suspends processing (outputs silence), and resumes when the operation completes.

---

## Thread Types

HISE defines the following thread types in `KillStateHandler::TargetThread`:

| Thread | Purpose | Can Block? | Can Allocate? |
|--------|---------|------------|---------------|
| **AudioThread** | Real-time DSP processing | **NO** | **NO** |
| **MessageThread** | UI updates, user interaction (JUCE message loop) | Yes | Yes |
| **SampleLoadingThread** | Background sample/preset loading | Yes | Yes |
| **ScriptingThread** | HISEScript compilation and execution | Yes | Yes |
| **AudioExportThread** | Offline bounce/render | Yes | Yes |

### Thread Detection

```cpp
auto thread = mc->getKillStateHandler().getCurrentThread();
bool running = mc->getKillStateHandler().isAudioRunning();
```

---

## KillStateHandler: The Core Mechanism

`KillStateHandler` implements a ticket-based suspend-and-resume system for operations that require exclusive access to shared data (loading samples, adding/removing processors, compiling scripts, modifying DSP graphs):

1. A non-audio thread **requests a ticket** indicating it needs safe access
2. The audio thread detects pending tickets and **gracefully kills all voices** (fade out)
3. Audio processing **suspends** (outputs silence)
4. The requesting thread performs its work safely
5. The ticket is **invalidated** when done
6. Audio processing **resumes** when all tickets are cleared

This is transparent to the caller -- the `killVoicesAndCall()` method handles the entire sequence.

---

## killVoicesAndCall() -- The Primary Cross-Thread Pattern

This is the main mechanism for safely executing operations that modify audio-thread-accessible data from a non-audio thread.

### Signature

```cpp
bool killVoicesAndCall(
    Processor* p,
    const ProcessorFunction& functionToExecuteWhenKilled,
    TargetThread targetThread
);
```

### Usage

```cpp
auto f = [](Processor* p) -> SafeFunctionCall::Status
{
	// Audio is guaranteed to be suspended here.
	// Safe to modify shared data structures.
	p->doSomething();
	return SafeFunctionCall::OK;
};

mc->getKillStateHandler().killVoicesAndCall(
    processor, f,
    MainController::KillStateHandler::TargetThread::SampleLoadingThread
);
```

### Return Value

- `true`: Function was executed synchronously (already on target thread and audio not running)
- `false`: Function was deferred to the target thread's queue

### When to Use

- Loading/unloading samples
- Adding/removing processors from the DSP graph
- Modifying DSP routing
- Any operation that would be unsafe during audio processing

### When NOT to Use

- Never call from the audio thread
- Not needed for simple parameter changes that use atomic values

---

## Lock Types and Priority

HISE uses a priority-based locking system to prevent deadlocks:

```cpp
enum class Type
{
    MessageLock = 0,   // Lowest priority
    ScriptLock,
    SampleLock,
    IteratorLock,      // Same effective priority as SampleLock
    AudioLock,         // Highest priority
    numLockTypes
};
```

### Lock Ordering Rules

1. **Cannot acquire a lower-priority lock while holding a higher-priority lock** (prevents priority inversion)
2. `IteratorLock` and `SampleLock` are mutually exclusive (same effective priority level)
3. `MessageLock` should only be held briefly from non-message threads

### SafeLock

Always use `SafeLock` instead of raw `ScopedLock` for any lock the audio thread might need:

```cpp
LockHelpers::SafeLock sl(mc, LockHelpers::Type::AudioLock, useRealLock);
```

`SafeLock` tracks which thread holds which lock and throws `BadLockException` on lock-order violations during development.

---

## SimpleReadWriteLock -- Non-Blocking Audio-Safe Pattern

For data that needs frequent reads from the audio thread and occasional writes from other threads:

```cpp
// Audio thread -- non-blocking, skips processing if write lock is held
if (auto sl = SimpleReadWriteLock::ScopedTryReadLock(dataLock))
{
    processData();  // Safe to read
}
// If lock not acquired, skip this buffer (acceptable trade-off)

// Message thread -- blocking write with exclusive access
{
    SimpleReadWriteLock::ScopedWriteLock sl(dataLock);
    updateData();
}
```

The key insight: audio thread reads use `ScopedTryReadLock` which returns immediately if the lock is held. Skipping one buffer (a few milliseconds of silence) is always preferable to blocking.

---

## AudioThreadGuard -- Debug-Time Thread Safety

Detects illegal operations on the audio thread during development. Create a guard in `processBlock` and the system will assert if any of the following occur on the audio thread:

- Heap allocation/deallocation
- String creation
- MessageManager locking
- Processor insertion/destruction
- ValueTree operations
- Global locking

```cpp
// Temporarily suspend checks for intentional operations
AudioThreadGuard::Suspender suspender;
```

### Warning Macro

```cpp
WARN_IF_AUDIO_THREAD(condition, MainController::KillStateHandler::IllegalOps::ProcessorInsertion);
```

---

## Thread Safety Assertions

Use these throughout the codebase to document and enforce thread requirements:

```cpp
jassert_message_thread;                  // Assert message thread
jassert_locked_script_thread(mc);        // Assert script lock held
jassert_sample_loading_thread(mc);       // Assert sample loading thread or lock
jassert_processor_idle;                  // Assert processor not processing audio
jassert_dispatched_message_thread(mc);   // Assert dispatched message thread context
```

---

## ScopedTicket -- Manual Suspension Control

For operations that need explicit control over when audio resumes:

```cpp
{
    SuspendHelpers::ScopedTicket ticket(mc);  // Audio suspends

    doSomethingThatNeedsNoAudio();

}  // Ticket invalidated, audio resumes
```

---

## LockFreeDispatcher -- Async Message Thread Operations

For operations triggered from non-message threads that need to run on the message thread:

```cpp
mc->getLockFreeDispatcher().callOnMessageThreadAfterSuspension(
    processor,
    [](Dispatchable* obj) -> Dispatchable::Status
    {
        auto p = static_cast<Processor*>(obj);
        p->sendRebuildMessage(true);
        return Dispatchable::Status::OK;
    }
);
```

---

## Guidelines for AI Coding Agents

### DO

- Use `killVoicesAndCall()` for any operation that modifies audio-thread-accessible data
- Use `SafeLock` instead of raw `ScopedLock` for locks the audio thread might need
- Use `ScopedTryReadLock` for audio-thread reads (never blocking reads)
- Return `SafeFunctionCall::OK` or `SafeFunctionCall::cancelled` from lambdas
- Document which thread calls each method
- Use `PolyHandler::ScopedAllVoiceSetter` when accessing `PolyData` from non-audio threads (see below)

### DON'T

- Never block on the audio thread (no locks, no allocations, no I/O)
- Never call `killVoicesAndCall()` from the audio thread
- Never acquire `AudioLock` while holding a lower-priority lock
- Never assume thread context without checking
- Never use raw `new`/`delete` in audio-thread code
- Never use `ScopedAllVoiceSetter` inside voice rendering context (would override `ScopedVoiceSetter`)

### PolyData and ScopedAllVoiceSetter

When `setInternalAttribute()` or any parameter setter touches `PolyData` (via scriptnode
parameter callbacks or `forEachCurrentVoice()`), it **must** be wrapped with 
`PolyHandler::ScopedAllVoiceSetter` if it can be called from a non-audio thread.

Without this protection, the UI thread may read a stale `voiceIndex` set by the audio
thread's `ScopedVoiceSetter`, causing only one voice to be updated instead of all.

```cpp
// WRONG: UI thread reads racy voiceIndex
void setInternalAttribute(int index, float newValue) override
{
    obj.template setParameter<0>(newValue);  // May only update one voice!
}

// CORRECT: ScopedAllVoiceSetter ensures all voices updated
void setInternalAttribute(int index, float newValue) override
{
    PolyHandler::ScopedAllVoiceSetter avs(polyHandler);
    obj.template setParameter<0>(newValue);  // Always updates all voices
}
```

**Key rule:** `ScopedAllVoiceSetter` is NOT needed on the audio thread (even outside
voice rendering) because there's no race condition on the same thread. It's only needed
for cross-thread access to `PolyData`.

See `voice-setter-system.md` for the full scenario analysis and fix details.

### Thread Context Comments

Document thread requirements on methods:

```cpp
// Called from: MessageThread only
void loadPreset(const File& f);

// Called from: AudioThread (must be lock-free)
void processBlock(AudioBuffer<float>& buffer);

// Called from: Any thread (uses killVoicesAndCall internally)
void setParameter(int index, float value);
```
