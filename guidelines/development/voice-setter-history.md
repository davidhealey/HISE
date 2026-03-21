# VoiceSetter Chain Optimization — Historical Archive

**Status:** REMOVED. All code described in this document has been deleted from the codebase.

This document preserves the design, implementation, and failure analysis of the recursive
VoiceSetter chain approach for `PolyData` voice caching. This optimization was attempted
and failed at runtime. The code was removed in the Phase 2 cleanup.

**See:** `voice-setter-system.md` for the current working system.

---

## The Performance Problem (Motivation)

`PolyData::get()` returns a reference to the current voice's data. Each call to `get()` must:

1. Call `begin()` which calls `voicePtr->getVoiceIndex()`
2. `getVoiceIndex()` performs:
   - Atomic load of `currentAllThread`
   - Conditional `Thread::getCurrentThreadId()` check (~15-65 cycles)
   - Atomic load: `voiceIndex.load()`
   - Multiplication: `* enabled`

In tight per-sample loops where `get()` is called hundreds of times per audio buffer, this
overhead was estimated at **~40% additional CPU usage** for trivial DSP operations.

**Resolution:** The `getVoiceIndex()` fast-path optimization (checking `currentAllThread`
for nullptr before calling `Thread::getCurrentThreadId()`) reduced the overhead to ~2-6
cycles per call (two atomic loads). This made the complex VoiceSetter chain unnecessary.

---

## Part 11: PolyData Voice Caching (The Attempted Optimization)

### The Two "ScopedVoiceSetter" Classes (REMOVED)

There were two similarly-named but very different classes:

| Class | Location | Purpose |
|-------|----------|---------|
| `PolyHandler::ScopedVoiceSetter` | `snex_Types.h` | Sets which voice is active in the audio thread |
| `PolyData::ScopedVoiceIndexCacher` | `snex_Types.h` | Cached voice pointer for zero-overhead `get()` calls |

`PolyData::ScopedVoiceSetter` was a deprecated alias for `ScopedVoiceIndexCacher`.

**Both `ScopedVoiceIndexCacher` and the `PolyData::ScopedVoiceSetter` alias have been removed.**
Only `PolyHandler::ScopedVoiceSetter` remains.

### How ScopedVoiceIndexCacher Worked

```cpp
// Without cacher - get() calls begin() every time
for (int i = 0; i < numSamples; i++)
{
    auto& data = polyData.get();  // Calls begin() -> getVoiceIndex() each iteration
    data.process(samples[i]);
}

// With cacher - get() returned cached pointer
{
    PolyData<T, NV>::ScopedVoiceIndexCacher cacher(polyData, false);
    
    for (int i = 0; i < numSamples; i++)
    {
        auto& data = polyData.get();  // Returned cached pointer directly
        data.process(samples[i]);
    }
}
```

The cacher:
1. At construction: called `begin()` once, stored result in `currentRenderVoice`
2. During scope: `get()` returned `currentRenderVoice` directly (no computation)
3. At destruction: reset `currentRenderVoice` to nullptr

### The SN_VOICE_SETTER Macro System (REMOVED)

Nodes declared their PolyData member for automatic caching:

```cpp
struct oscillator : public polyphonic_base
{
    PolyData<OscData, NumVoices> voiceData;
    SN_VOICE_SETTER(oscillator, voiceData);  // Generated VoiceSetter type
};
```

The macro generated a `VoiceSetter` struct that created a `ScopedVoiceIndexCacher`:

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

Container wrappers automatically created `VoiceSetter` around all processing calls:

```cpp
// In processors.h - wrapped every process() call
void process(ProcessDataDyn& data) noexcept
{
    if (auto sv = typename T::ObjectType::VoiceSetter(obj.getObject()))
        obj.process(fd);
}
```

### Macro Variants (ALL REMOVED)

| Macro | PolyData Count | Data Lock | Use Case |
|-------|----------------|-----------|----------|
| `SN_VOICE_SETTER` | 1 | No | No external data access in processing |
| `SN_VOICE_SETTER_WITH_DATA_LOCK` | 1 | Yes | External data accessed in processing |
| `SN_VOICE_SETTER_2` | 2 | No | Two PolyData, no external data |
| `SN_VOICE_SETTER_2_WITH_DATA_LOCK` | 2 | Yes | Two PolyData + external data (e.g., `file_player`) |
| `SN_EMPTY_VOICE_SETTER` | 0 | No | Wrappers that skip caching |
| `SN_FORWARD_VOICE_SETTER_T` | N/A | N/A | Forwarded VoiceSetter type from wrapped node |
| `SN_ACCESS_TYPE_FOR_PARAM` | N/A | N/A | Per-parameter AccessType selection |

### Nodes That Had SN_VOICE_SETTER (ALL REMOVED)

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

---

## Part 12: Enforcing ScopedVoiceIndexCacher Usage (REMOVED)

### The Enforcement Mechanism (REMOVED)

A debug counter system was designed to guide developers toward using `SN_VOICE_SETTER`:

#### PolyHandler::AccessType Enum (REMOVED)

```cpp
enum class AccessType
{
    RequireCached,         // Default — fires debug assertion if called >64 times uncached
    AllowUncached,         // Explicitly allows uncached access
    ForceAllVoices,        // Iterates ALL voices regardless of context
    TrustAudioThreadContext // Skips thread ID check for known audio-thread calls
};
```

#### Debug Counter Behavior (REMOVED)

In debug builds, `get()` tracked uncached calls:

1. If `currentRenderVoice == nullptr` (no cacher active) and `accessType == RequireCached`:
   - Increment counter
   - If counter > 64, fire `jassertfalse`
   - Reset counter to avoid spam
2. If cacher was active, reset counter to 0

#### Nodes Using AllowUncached (REMOVED — no longer needed)

| Node | Method | Reasoning |
|------|--------|-----------|
| `event_data_reader` | `handleModulation()`, `handleHiseEvent()` | Per-event/block |
| `event_data_writer` | `handleHiseEvent()` | Per-event |
| `timer` | `handleModulation()` | Per-block |
| `neural<NV>` | `process()` | Heavy neural network computation |

### Cost-Benefit Analysis (Historical)

| Processing Payload | Lookup Overhead | Decision |
|-------------------|-----------------|----------|
| Trivial (single multiply, simple state) | ~40% of total | Use `SN_VOICE_SETTER` |
| Light (basic DSP, filters) | ~10-20% of total | Use `SN_VOICE_SETTER` |
| Heavy (FFT, convolution, neural net) | <1% of total | Use `AllowUncached` |

**Note:** With the `getVoiceIndex()` fast-path optimization now in place, the overhead
is ~2-6 cycles per `get()` call (two atomic loads when `currentAllThread` is nullptr),
making this cost-benefit analysis moot. Even trivial DSP operations no longer suffer
significant overhead from uncached `get()`.

---

## Part 14: Failed VoiceSetter Chain Approach — Full Analysis

### The Two Problems Being Solved

**Problem A: Thread Safety (SOLVED — kept in main document)**
When the UI thread calls parameter setters, it can read a stale `voiceIndex` set by the
audio thread, causing only one voice to be updated instead of all. Fixed with
`ScopedAllVoiceSetter` in `setHardcodedAttribute()` and `setInternalAttribute()`.

**Problem B: Performance Optimization (SOLVED differently)**
Originally we tried the VoiceSetter chain for this. Instead, the `getVoiceIndex()` fast-path
(nullptr check on `currentAllThread` before `Thread::getCurrentThreadId()`) eliminated
the expensive thread ID lookup for 99.9% of calls.

### What We Tried: The VoiceSetter Chain Approach

#### The Idea

Create a recursive VoiceSetter system where `wrap::node::process()` constructs a single
VoiceSetter object that recursively creates `ScopedVoiceIndexCacher` instances for ALL
nested polyphonic nodes' `PolyData` members. This would cache voice pointers once at the
top of the processing chain, eliminating repeated `getVoiceIndex()` calls in hot paths.

#### The Implementation (5 Phases)

**Phase 1: Thread Safety Fix (COMPLETED — KEPT)**
Added `ScopedAllVoiceSetter` before parameter callbacks in:
- `HardcodedModuleBase.cpp` — `setHardcodedAttribute()`
- `FlexAhdsrEnvelope.cpp` — `setInternalAttribute()`

**Phase 2: Removed `forceAll` Parameter (COMPLETED — SUPERSEDED by full cleanup)**
Replaced the `forceAll` bool with `AccessType::ForceAllVoices`. Later, the entire
`AccessType` enum and `forEachCurrentVoice()` method were removed in the full cleanup.
`ForceAllVoices` was replaced by `polyData.all()`.

**Phase 3: Performance Optimization for Internal Modulation (FAILED — REMOVED)**
- Added `TrustAudioThreadContext` to `AccessType` enum
- Added `internallyModulatedParams` uint64 bitmask to `polyphonic_base`
- `parameter::single_base::connect()` called `setInternallyModulated<P>()` at connection time
- `SN_ACCESS_TYPE_FOR_PARAM` macro returned `TrustAudioThreadContext` for modulated params
- `ScopedPrepareContext` RAII guard forced all-voice iteration during `prepare()`

**Phase 4: Interpreted/Dynamic Path Fix (PARTIALLY WORKING — REMOVED)**
- Added `defaultAccessType` to `PolyHandler`
- `DspNetwork::VoiceSetter` passed `AllowUncached` to `ScopedVoiceSetter`
- `ScopedVoiceIndexCacher` skipped caching when `defaultAccessType == AllowUncached`

**Phase 5: VoiceSetter Chain Forwarding (FAILED — REMOVED)**
The container's `sub_tuple` / `base_wrapper` system created VoiceSetters recursively, but
wrappers needed to forward their VoiceSetter type:
- Added `SN_FORWARD_VOICE_SETTER_T` macro to wrapper templates
- Refactored VoiceSetter resolution in `Containers.h`:
  - `has_direct_voice_setter<T>` detected `T::VoiceSetter`
  - `has_objecttype_voice_setter<T>` detected `T::ObjectType::VoiceSetter`
  - `get_voice_setter<T>` preferred direct over indirect (ObjectType) path
  - `base_wrapper` used `get_voice_setter<T>::type`

#### Why It Failed: The Assertion

The assertion fired in `ramp::processFrame()` -> `state.get()` because
`currentRenderVoice` was null (no `ScopedVoiceIndexCacher` active).

**Failing callstack (simplified):**
```
wrap::node<instance<256>>::process()         [VoiceSetter created HERE]
  outer chain.process()
    wrap::fix::process()
      wrap::fix::process()
        wrap::frame_x::process()
          FrameConverters::processFix()      [per-sample loop, NO VoiceSetter]
            inner chain.processFrame()       [NO VoiceSetter for inner elements]
              wrap::fix::processFrame()
                wrap::mod::processFrame()
                  wrap::no_data::processFrame()
                    ramp::processFrame()
                      state.get()            [ASSERTION - currentRenderVoice is null]
```

#### Analysis of the Failure

The VoiceSetter at `wrap::node::process()` should recursively descend through the
container tree and create `ScopedVoiceIndexCacher` for every polyphonic node. We traced
the entire chain and it **should** work in theory, but the assertion still fired.

**Possible failure points identified but never confirmed:**
- `voicePtr` could be null in `ramp.state` (prepare not called correctly?)
- `ScopedVoiceIndexCacher` skipped caching when `defaultAccessType != RequireCached`
- The recursive `get_voice_setter` resolution may not actually work as designed
  (compile-time type resolution succeeds but runtime construction fails)
- `base_wrapper` passes `obj.getObject()` which returns different things depending on
  `SN_OPAQUE_WRAPPER` vs `SN_SELF_AWARE_WRAPPER`
- Possible MSVC template instantiation issue with deeply nested types

**We ran out of time debugging before confirming the exact failure point.**

### Key Technical Concepts (Historical)

#### SN_SELF_AWARE_WRAPPER vs SN_OPAQUE_WRAPPER

```cpp
// SN_SELF_AWARE_WRAPPER(x, T):
//   getObject()        returns *this          (the wrapper itself)
//   getWrappedObject() returns this->obj.getWrappedObject()  (chains to inner)
//   ObjectType         = x (the wrapper class)

// SN_OPAQUE_WRAPPER(x, T):
//   getObject()        returns this->obj.getObject()  (skips to inner's getObject)
//   getWrappedObject() returns this->obj.getWrappedObject()  (chains to inner)
//   ObjectType         = T::ObjectType (inner type's ObjectType)
```

This mattered because `base_wrapper` passed `obj.getObject()` to the VoiceSetter
constructor, and the two wrapper types returned different things.

#### Two Entry Paths Into Scriptnode Processing

1. **Compiled C++ path** (`wrap::node<T>`): Container's VoiceSetter recursively creates
   `ScopedVoiceIndexCacher` for all PolyData members (was supposed to work, didn't)
2. **Interpreted/DspNetwork path**: `DspNetwork::VoiceSetter` only sets
   `PolyHandler::voiceIndex` via `ScopedVoiceSetter` (type-erased, can't set up per-node caching)

### Removed Infrastructure (Complete List)

| Category | Items |
|----------|-------|
| **Enums** | `PolyHandler::AccessType` entirely (RequireCached, AllowUncached, ForceAllVoices, TrustAudioThreadContext) |
| **Classes/Structs** | `ScopedVoiceIndexCacher`, `ScopedPrepareContext`, `DummyVoiceSetter`, `base_wrapper` (both specializations), `sub_tuple_helper_impl`, `container_base::VoiceSetter` |
| **Traits** | `has_voice_setter`, `has_direct_voice_setter`, `has_objecttype_voice_setter`, `get_voice_setter`, `get_voice_setter_impl` |
| **Macros** | `SN_VOICE_SETTER`, `SN_VOICE_SETTER_2`, `SN_VOICE_SETTER_WITH_DATA_LOCK`, `SN_VOICE_SETTER_2_WITH_DATA_LOCK`, `SN_ACCESS_TYPE_FOR_PARAM`, `SN_FORWARD_VOICE_SETTER_T`, `SN_EMPTY_VOICE_SETTER` |
| **Methods** | `forEachCurrentVoice()`, `getDefaultAccessType()`, `isAllThreadActive()`, `setInternallyModulated()`, `isInternallyModulated()` |
| **Members** | `currentRenderVoice`, `uncachedGetCallCount`, `UNCACHED_GET_THRESHOLD`, `defaultAccessType`, `prevAccessType`, `forceAllVoiceIteration`, `internallyModulatedParams` |
| **Aliases** | `PolyData::ScopedVoiceSetter` (alias for `ScopedVoiceIndexCacher`), `sub_tuple` |

### What Replaced It

| Removed | Replacement |
|---------|-------------|
| `ScopedVoiceIndexCacher` + caching in `get()` | `get()` always calls `begin()` which uses fast-path `getVoiceIndex()` |
| `forEachCurrentVoice(lambda, AccessType)` | `for(auto& d : polyData)` for voice-context-aware iteration |
| `forEachCurrentVoice(lambda, ForceAllVoices)` | `for(auto& d : polyData.all())` for all-voices iteration |
| `AccessType::AllowUncached` argument to `get()` | `get()` takes no arguments — always uses fast path |
| `SN_VOICE_SETTER` macros on nodes | Nothing — nodes don't need VoiceSetter declarations |
| `SN_FORWARD_VOICE_SETTER_T` on wrappers | Nothing — wrappers call processing directly |
| `container_base::VoiceSetter` / `sub_tuple` | Nothing — containers don't wrap processing in VoiceSetters |
| `wrap::node` VoiceSetter wrapping | Direct call: `obj.process(d)` |
| `polyphonic_base` optimization members | `polyphonic_base` is now just constructor + virtual destructor |

### Questions That Were Considered

1. **Is recursive VoiceSetter construction the right approach?** — No. Removed entirely.
2. **Should we separate thread safety from performance?** — Yes. Thread safety uses
   `ScopedAllVoiceSetter`. Performance uses the `getVoiceIndex()` fast-path.
3. **Can we use a simpler per-node approach?** — Not needed. Fast-path makes per-call
   overhead negligible.
4. **What about the interpreted path?** — Uses the same `getVoiceIndex()` fast-path.
   No special handling needed.
5. **Alternative optimization strategies?** — The nullptr check on `currentAllThread`
   was chosen (Question #8 from the original document). This eliminates the
   `Thread::getCurrentThreadId()` call 99.9% of the time.
