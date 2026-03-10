# Processor Metadata Migration Guide

This guide describes how to migrate HISE processors from scattered parameter metadata (parameterNames, getDefaultValue, editor setup code) to the unified `ProcessorMetadata` system. It is designed to be followed mechanically for each processor.

Three processors have already been migrated and serve as templates:

- **LfoModulator** (TimeVariantModulator) - full example with tempo sync, value list, modulation chains
- **SimpleEnvelope** (EnvelopeModulator) - envelope with base class chaining
- **WaveSynth** (ModulatorSynth) - sound generator with base class chaining

## 1. Base Class Chaining Reference

Each processor's `createMetadata()` starts from a different base depending on its type:

| Processor base class | Chain from | Base parameters provided | Base modulation chains |
|---|---|---|---|
| VoiceStartModulator | `ProcessorMetadata(getClassType())` | None | None |
| TimeVariantModulator | `ProcessorMetadata(getClassType())` | None | None |
| EnvelopeModulator | `EnvelopeModulator::createBaseMetadata()` | Monophonic (toggle, default 0.0), Retrigger (toggle, default 1.0) | None |
| MidiProcessor | `ProcessorMetadata(getClassType())` | None | None |
| EffectProcessor (all subtypes) | `ProcessorMetadata(getClassType())` | None | None |
| ModulatorSynth | `ModulatorSynth::createBaseMetadata()` | Gain (Decibel, default 1.0), Balance (Pan, default 0.0), VoiceLimit (Discrete {1,256,1}, default 64.0), KillFadeTime (Time {0,20000,1}, default 20.0) | GainModulation (ScaleOnly), PitchModulation (Pitch) |

## 2. The Recipe (Standard Processors)

### Step 1: Header (.h)

**a) Clear the SET_PROCESSOR_NAME description.** The third argument becomes `""` since metadata now owns the description:

```cpp
// Before
SET_PROCESSOR_NAME("SimpleEnvelope", "Simple Envelope", "The most simple envelope (only attack and release).")

// After
SET_PROCESSOR_NAME("SimpleEnvelope", "Simple Envelope", "")
```

**b) Strip doxygen comments from parameter enum members.** Metadata owns descriptions now:

```cpp
// Before
enum SpecialParameters
{
    Attack = EnvelopeModulator::Parameters::numParameters, ///< the attack time in milliseconds
    Release, ///< the release time in milliseconds
    LinearMode, ///< toggles between linear and exponential mode
    numTotalParameters
};

// After
enum SpecialParameters
{
    Attack = EnvelopeModulator::Parameters::numParameters,
    Release,
    LinearMode,
    numTotalParameters
};
```

Also remove any parameter table from the class doxygen comment (the `ID | Parameter | Description` markdown tables).

**c) Add `const bool metadataInitialised;`** after the constructor declaration:

```cpp
SimpleEnvelope(MainController *mc, const String &id, int voiceAmount, Modulation::Mode m);
~SimpleEnvelope();

const bool metadataInitialised;
```

**d) Add `static ProcessorMetadata createMetadata();`** declaration.

**e) Remove `getDefaultValue()` override declaration** - unless the processor has runtime-dependent defaults (see Section 5).

### Step 2: Source (.cpp)

**a) Write `createMetadata()` at the top of the file** (before the constructor). Chain from the appropriate base (see Section 1):

```cpp
hise::ProcessorMetadata SimpleEnvelope::createMetadata()
{
    using Par = ProcessorMetadata::ParameterMetadata;
    using Mod = ProcessorMetadata::ModulationMetadata;

    return EnvelopeModulator::createBaseMetadata()
        .withId(getClassType())
        .withPrettyName("Simple Envelope")
        .withDescription("The most simple envelope (only attack and release).")
        .withType<hise::EnvelopeModulator>()
        .withParameter(Par(Attack)
            .withId("Attack")
            .withDescription("The attack time in milliseconds")
            .withSliderMode(HiSlider::Time)
            .withDefault(5.0f))
        .withParameter(Par(Release)
            .withId("Release")
            .withDescription("The release time in milliseconds")
            .withSliderMode(HiSlider::Time)
            .withDefault(10.0f))
        .withParameter(Par(LinearMode)
            .withId("LinearMode")
            .withDescription("Toggles between linear and exponential envelope curves")
            .asToggle()
            .withDefault(1.0f))
        .withModulation(Mod(AttackChain)
            .withId("AttackTimeModulation")
            .withDescription("Modulates the attack time per voice")
            .withMode(scriptnode::modulation::ParameterMode::ScaleOnly));
}
```

For processors that don't chain from a base (VoiceStartModulators, TimeVariantModulators, MidiProcessors, Effects):

```cpp
hise::ProcessorMetadata LfoModulator::createMetadata()
{
    using Par = ProcessorMetadata::ParameterMetadata;
    using Mod = ProcessorMetadata::ModulationMetadata;
    using Range = scriptnode::InvertableParameterRange;

    return ProcessorMetadata(getClassType())
        .withPrettyName("LFO Modulator")
        .withDescription("A LFO Modulator modulates the signal with a low frequency")
        .withType<hise::TimeVariantModulator>()
        // ... parameters and modulations
}
```

**b) Add `metadataInitialised(updateParameterSlots())` to the initializer list.** It must appear before any member that calls `getDefaultValue()`:

```cpp
// Before
SimpleEnvelope::SimpleEnvelope(MainController *mc, const String &id, int voiceAmount, Modulation::Mode m):
    EnvelopeModulator(mc, id, voiceAmount, m),
    Modulation(m),
    attack(getDefaultValue(Attack)),
    ...

// After
SimpleEnvelope::SimpleEnvelope(MainController *mc, const String &id, int voiceAmount, Modulation::Mode m):
    EnvelopeModulator(mc, id, voiceAmount, m),
    Modulation(m),
    metadataInitialised(updateParameterSlots()),
    attack(getDefaultValue(Attack)),
    ...
```

If the constructor does NOT call `getDefaultValue()` in the initializer list, place `metadataInitialised(updateParameterSlots())` at the end of the initializer list (it still needs to run before the constructor body).

**c) Delete the `parameterNames.add(...)` block** from the constructor body.

**d) Delete the `updateParameterSlots()` call** from the constructor body (it now runs via the initializer list).

**e) Delete or simplify the `getDefaultValue()` override:**

- **If all defaults are static constants:** delete the entire override. The base class chain handles it:
  - `Processor::getDefaultValue()` reads from cached metadata
  - `ModulatorSynth::getDefaultValue()` checks metadata first, falls back to switch for base params
  - `EnvelopeModulator::getDefaultValue()` handles Monophonic (runtime-dependent) and Retrigger via switch, delegates to metadata for derived-class params

- **If some defaults are runtime-dependent:** keep only those cases, delegate the rest:

```cpp
float LfoModulator::getDefaultValue(int parameterIndex) const
{
    // Frequency has a dynamic default depending on tempoSync state
    if (parameterIndex == Parameters::Frequency)
        return tempoSync ? (float)TempoSyncer::Eighth : 3.0f;

    // All other parameters use the static default from metadata
    return Processor::getDefaultValue(parameterIndex);
}
```

### Step 3: Editor (.cpp)

**a) Add metadata retrieval** at the top of the setup section:

```cpp
auto md = getProcessor()->getMetadata();
```

**b) Replace manual setup calls** with `md.setup()`:

```cpp
// Before
attackSlider->setup(getProcessor(), SimpleEnvelope::Attack, "Attack Time");
attackSlider->setMode(HiSlider::Time);

// After
md.setup(*attackSlider, getProcessor(), SimpleEnvelope::Attack);
```

The `setup()` method handles:
- `HiSlider`: calls `setup()`, `setMode()`, sets tooltip from description
- `HiComboBox`: calls `setup()`, populates item list
- `HiToggleButton`: calls `setup()`, sets tooltip from description

**c) Keep cosmetic suffixes** that are display-only and not part of the parameter definition:

```cpp
md.setup(*detuneSlider, getProcessor(), WaveSynth::Detune1);
detuneSlider->setTextValueSuffix("ct");  // keep this
```

**d) Replace tempo-sync switching** in `updateGui()`:

```cpp
// Before
if (getProcessor()->getAttribute(LfoModulator::TempoSync) > 0.5f)
    frequencySlider->setMode(HiSlider::Mode::TempoSync);
else
    frequencySlider->setMode(HiSlider::Frequency, NormalisableRange(0.01, 40.0).withCentreSkew(10.0));

// After
auto md = getProcessor()->getMetadata();
md.updateTempoSync(*frequencySlider);
```

### Step 4: Registry (.cpp)

In `ProcessorMetadataRegistry.cpp`, replace the fallback entry:

```cpp
// Before
add(ProcessorMetadata::createFallback<SimpleEnvelope>());

// After
add(SimpleEnvelope::createMetadata());
```

## 3. Variant: Hardcoded Script Processors

Hardcoded script processors (Arpeggiator, LegatoProcessor, CCSwapper, etc.) define parameters through the scripting Content API in their `onInit()` override, which is called from the constructor. The `flushContentParameters()` helper then auto-populates `parameterNames` from Content component names and calls `updateParameterSlots()`.

Their migration is different because:
- Parameters are defined via `Content.addKnob()`, `Content.addButton()`, etc. - not C++ enums
- There is no `getDefaultValue()` override
- There is no separate editor file (the Content IS the editor)
- The `onInit()` code must be preserved (it creates the UI widgets)

### What to do

**a) Header:** Add `static ProcessorMetadata createMetadata();` declaration. Clear the `SET_PROCESSOR_NAME` description to `""`. No `metadataInitialised` member needed (these processors don't call `getDefaultValue()` in the initializer list).

**b) Source:** Write a `createMetadata()` that extracts parameter names, types, and defaults from the `onInit()` code. The parameter IDs come from the `parameterNames.add()` calls (Arpeggiator pattern) or from the Content component names (flushContentParameters pattern). The parameter types come from the widget type (addKnob = Float/Discrete, addButton = Toggle, addComboBox = List). Defaults come from `setValue()` calls.

For example, for a processor that does:

```cpp
void onInit() override
{
    channelNumber = Content.addKnob("channelNumber", 0, 0);
    channelNumber->set("text", "MIDI Channel");
    channelNumber->setRange(1, 16, 1);
}
```

The metadata parameter would be:

```cpp
.withParameter(Par(0)  // index by position in Content
    .withId("channelNumber")
    .withDescription("MIDI Channel")
    .withSliderMode(HiSlider::Discrete, Range(1.0, 16.0, 1.0)))
```

Note: hardcoded script processor parameters don't have C++ enum indices. Use sequential integers starting from 0.

**c) Registry:** Replace `createFallback<T>()` with `T::createMetadata()`.

**d) `onInit()` stays unchanged.** The Content widget creation code is preserved. The `parameterNames.add()` / `flushContentParameters()` / `updateParameterSlots()` calls also stay - these are needed for the scripting content system to function. The metadata exists alongside them for the registry/documentation/factory consumers.

## 4. Parameter Extraction Checklist

For each processor, gather data from these sources:

| Data | Where to find it |
|---|---|
| Parameter names/order | `parameterNames.add()` in constructor (or `onInit()` for hardcoded) |
| Parameter enum indices | The `SpecialParameters` or `Parameters` enum in the header |
| Default values | `getDefaultValue()` switch statement |
| Slider mode | Editor `.setMode()` calls |
| Range | Editor `.setMode(mode, range)` or `.setRange()` calls |
| Value list (combo boxes) | Editor combo box item population, or `withValueList()` in onInit |
| Toggle parameters | `asToggle()` in editor `.setup()` or `HiToggleButton` usage |
| Tempo sync | Editor tempo-sync switching logic in `updateGui()` |
| Modulation chains | `InternalChains` enum + `new ModulatorChain(...)` calls in constructor |
| Modulation modes | The mode argument to `ModulatorChain` constructor (GainMode, PitchMode, etc.) |
| Description | Doxygen comments on enum members, `SET_PROCESSOR_NAME` third argument |
| Type/subtype | The C++ base class (passed to `withType<T>()`) |
| Identity interfaces | Concrete type checks: `ModulatorSampler`, `MidiPlayer`, `WavetableSynth`, `HotswappableProcessor`, `RoutableProcessor` |
| Complex data interfaces | `ProcessorWithStaticExternalData` constructor args or `getNumDataObjects()` overrides |

### Description quality

Descriptions should be concise but informative - explain *what the parameter does* to the signal or behavior, not just restate the parameter name. Aim for one sentence that would help a user who doesn't know what the parameter is.

| Avoid | Prefer |
|---|---|
| "The smoothing time" | "Applies smoothing to reduce hard edges in the oscillator output" |
| "The attack time in milliseconds" | "Controls how long the envelope takes to reach full level after a note-on" |
| "The most simple envelope (only attack and release)." | "A minimal envelope with attack and release stages, using linear or exponential curves." |
| "Smoothing factor for the oscillator" | "Applies low-pass smoothing to the LFO output to reduce discontinuities at waveform edges" |

If the existing doxygen comment or `SET_PROCESSOR_NAME` description is already clear, use it directly. If it merely restates the parameter name (e.g., "the frequency"), rewrite it. If improving a description requires domain knowledge you are uncertain about, keep the existing text and mark it with a `TODO_MODULE:` comment for manual review later.

### ParameterMetadata builder methods reference

| Method | When to use |
|---|---|
| `.withId("Name")` | Always - the parameter's string identifier |
| `.withDescription("...")` | Always - brief description |
| `.withDefault(value)` | Always - the default value as float |
| `.withSliderMode(HiSlider::Mode)` | For slider parameters |
| `.withSliderMode(HiSlider::Mode, Range(...))` | For slider parameters with explicit range |
| `.asToggle()` | For on/off parameters (sets range to {0,1,1}) |
| `.withValueList({"item1", "item2", ...})` | For combo box / list parameters (1-indexed by default) |
| `.withValueList(items, startIndex)` | For list parameters with non-default start index |
| `.withTempoSyncMode(toggleParamIndex)` | For parameters that switch between free and tempo-sync modes |
| `.withRange(Range(...))` | For parameters with explicit range but no slider mode |

### ModulationMetadata builder methods reference

| Method | When to use |
|---|---|
| `.withId("Name")` | Always - the chain's string identifier |
| `.withDescription("...")` | Always - what the chain modulates |
| `.withMode(scriptnode::modulation::ParameterMode::X)` | Always - ScaleOnly, ScaleAdd, or Pitch |

### Modulation mode mapping

The `ModulatorChain` constructor mode maps to `scriptnode::modulation::ParameterMode`:

| ModulatorChain mode | ParameterMode |
|---|---|
| `ModulatorChain::GainMode` | `ScaleOnly` |
| `ModulatorChain::PitchMode` | `Pitch` |

Chains constructed with a `scaleFunction` that maps 0..1 to -1..1 (like WaveSynth's MixModulation) use `ScaleAdd`.

### withType mapping

| C++ base class | `withType<T>()` argument |
|---|---|
| VoiceStartModulator subclass | `hise::VoiceStartModulator` |
| TimeVariantModulator subclass | `hise::TimeVariantModulator` |
| EnvelopeModulator subclass | `hise::EnvelopeModulator` |
| MasterEffectProcessor subclass | `hise::MasterEffectProcessor` |
| MonophonicEffectProcessor subclass | `hise::MonophonicEffectProcessor` |
| VoiceEffectProcessor subclass | `hise::VoiceEffectProcessor` |
| MidiProcessor subclass | `hise::MidiProcessor` |
| ModulatorSynth subclass | `hise::ModulatorSynth` |

## 5. Special Cases

### Runtime-dependent defaults

If a parameter's default depends on runtime state (not a compile-time constant), you must keep a `getDefaultValue()` override for that parameter only. Known cases:

- **EnvelopeModulator::Monophonic** - default depends on `getVoiceAmount() == 1` (already handled by `EnvelopeModulator::getDefaultValue()`, not a concern for derived classes)
- **LfoModulator::Frequency** - default depends on `tempoSync` state

The pattern is: handle the dynamic parameter(s) explicitly, delegate everything else to `Processor::getDefaultValue()`:

```cpp
float MyProcessor::getDefaultValue(int parameterIndex) const
{
    if (parameterIndex == DynamicParam)
        return computeRuntimeDefault();

    return Processor::getDefaultValue(parameterIndex);
}
```

Even for dynamic defaults, still declare a static default in `createMetadata()` - this is used by the registry/documentation consumers which don't have a processor instance.

### Tempo-sync parameters

Use `.withTempoSyncMode(toggleParamIndex)` on the target parameter (the one whose range/mode switches), pointing to the toggle parameter that controls the mode:

```cpp
.withParameter(Par(Frequency)
    .withId("Frequency")
    .withSliderMode(HiSlider::Frequency, Range(0.01, 40.0, 0.0).withCentreSkew(10.0))
    .withDefault(3.0f)
    .withTempoSyncMode(TempoSync))  // <-- index of the toggle param
```

### Zero-parameter processors

Processors with no own parameters still benefit from metadata for type/description/registry. For ModulatorSynth subclasses, chaining from `createBaseMetadata()` provides the 4 base parameters. For other types with truly zero parameters, the metadata just has type info:

```cpp
static ProcessorMetadata createMetadata()
{
    return ProcessorMetadata(getClassType())
        .withPrettyName("Route Effect")
        .withDescription("Routes audio to other channels.")
        .withType<hise::MasterEffectProcessor>();
}
```

### Processors without editors

Some processors have no dedicated editor file (they use `EmptyProcessorEditorBody` or a generic scripting editor). Skip Step 3 (editor migration) for these. They still get Steps 1, 2, and 4.

### Interfaces (Synth.getXXX() correlation)

There are two methods for declaring interfaces, corresponding to different categories of the `Synth.getXXX()` scripting API:

**`withInterface<T>()`** - for identity-based interfaces (the processor IS this type):

| `withInterface<T>()` argument | Scripting API method | String stored |
|---|---|---|
| `ModulatorSampler` | `Synth.getSampler()` | `"Sampler"` |
| `MidiPlayer` | `Synth.getMidiPlayer()` | `"MidiPlayer"` |
| `WavetableSynth` | `Synth.getWavetableController()` | `"WavetableController"` |
| `HotswappableProcessor` (or subclass) | `Synth.getSlotFX()` | `"SlotFX"` |
| `RoutableProcessor` (or subclass) | `Synth.getRoutingMatrix()` | `"RoutingMatrix"` |

Usage: call on the processor's own class or a base it inherits from:

```cpp
.withType<hise::ModulatorSynth>()
.withInterface<ModulatorSampler>()     // this IS a sampler
.withInterface<RoutableProcessor>()    // this HAS a routing matrix
```

**`withComplexDataInterface(ExternalData::DataType)`** - for data-based interfaces (the processor HOLDS this data type):

| `ExternalData::DataType` | Scripting API method | String stored |
|---|---|---|
| `Table` | `Synth.getTableProcessor()` | `"TableProcessor"` |
| `SliderPack` | `Synth.getSliderPackProcessor()` | `"SliderPackProcessor"` |
| `AudioFile` | `Synth.getAudioSampleProcessor()` | `"AudioSampleProcessor"` |
| `DisplayBuffer` | `Synth.getDisplayBufferSource()` | `"DisplayBufferSource"` |

Usage: call once per data type the processor holds. The counts come from the `ProcessorWithStaticExternalData` constructor arguments or the `ExternalDataHolder` implementation:

```cpp
// LFO has 1 table, 1 slider pack, 0 audio files, 1 display buffer
.withComplexDataInterface(ExternalData::DataType::Table)
.withComplexDataInterface(ExternalData::DataType::SliderPack)
.withComplexDataInterface(ExternalData::DataType::DisplayBuffer)
```

To determine which data types a processor holds, check:
- The `ProcessorWithStaticExternalData(mc, numTables, numSliderPacks, numAudioFiles, numDisplayBuffers)` constructor call
- Or `getNumDataObjects()` overrides for dynamic data holders

### Range construction

Use `scriptnode::InvertableParameterRange` (aliased as `Range` in the using declaration):

```cpp
using Range = scriptnode::InvertableParameterRange;

// Simple range: {min, max}
Range(-100.0, 100.0)

// Discrete range: {min, max, interval}
Range(-5.0, 5.0, 1.0)

// Range with skew: {min, max, interval}.withCentreSkew(centre)
Range(0.01, 40.0, 0.0).withCentreSkew(10.0)
```

## 6. Processor Inventory

### Already migrated

| Processor | Base class | Params |
|---|---|---|
| LfoModulator | TimeVariantModulator | 11 |
| SimpleEnvelope | EnvelopeModulator | 5 (2 base + 3 own) |
| WaveSynth | ModulatorSynth | 19 (4 base + 15 own) |

### VoiceStartModulators

| Processor | Params | Has getDefaultValue | InitList calls getDefaultValue | Has editor | Notes |
|---|---|---|---|---|---|
| ConstantModulator | 0 | No | No | Yes | Zero params |
| VelocityModulator | 3 | No | No | Yes | Inverted, UseTable, DecibelMode |
| KeyModulator | 0 | No | No | Yes | Table-only |
| RandomModulator | 1 | No | No | Yes | UseTable |
| GlobalVoiceStartModulator | 2 | No | No | Yes | UseTable, Inverted (from GlobalModulator) |
| GlobalStaticTimeVariantModulator | 2 | No | No | Yes | UseTable, Inverted (from GlobalModulator) |
| ArrayModulator | 0 | No | No | Yes | SliderPack-based |
| EventDataModulator | 2 | No | No | Yes | SlotIndex, DefaultValue |

### TimeVariantModulators

| Processor | Params | Has getDefaultValue | InitList calls getDefaultValue | Has editor | Notes |
|---|---|---|---|---|---|
| ControlModulator | 5 | No | No | Yes | Inverted, UseTable, ControllerNumber, SmoothTime, DefaultValue |
| PitchwheelModulator | 3 | No | No | Yes | Inverted, UseTable, SmoothTime |
| MacroModulator | 4 | No | No | Yes | MacroIndex, SmoothTime, UseTable, MacroValue |
| GlobalTimeVariantModulator | 2 | No | No | Yes | UseTable, Inverted (from GlobalModulator) |

### EnvelopeModulators

| Processor | Params | Has getDefaultValue | InitList calls getDefaultValue | Has editor | Notes |
|---|---|---|---|---|---|
| AhdsrEnvelope | 11 (2 base + 9 own) | Yes | No | Yes | Has internal chains |
| TableEnvelope | 4 (2 base + 2 own) | Yes | No | Yes | Has internal chains |
| MPEModulator | 6 (2 base + 4 own) | Yes | Yes | Yes | Needs metadataInitialised pattern |
| ScriptnodeVoiceKiller | 2 (2 base + 0 own) | Yes | No | No | Base params only |
| GlobalEnvelopeModulator | 4 (2 base + 2 own) | No | No | Yes | Uses GlobalModulator mix-in |
| EventDataEnvelope | 5 (2 base + 3 own) | Yes | No | Yes | |
| MatrixModulator | 4 (2 base + 2 own) | Yes | No | Yes | |
| FlexAhdsrEnvelope | varies | Yes | No | No | Params from scriptnode base |

### MidiProcessors

| Processor | Params | Has getDefaultValue | InitList calls getDefaultValue | Has editor | Notes |
|---|---|---|---|---|---|
| Transposer | 1 | No | No | Yes | TransposeAmount |
| MidiPlayer | 7 | No | No | Yes | |
| ChokeGroupProcessor | 4 | Yes | No | No | |

### Effects

| Processor | Base | Params | Has getDefaultValue | InitList calls getDefaultValue | Has editor | Notes |
|---|---|---|---|---|---|---|
| PolyFilterEffect | VoiceEffect | 6 | Yes | Yes | Yes | 4 internal chains, needs metadataInitialised |
| HarmonicFilter | VoiceEffect | 4 | No | No | Yes | |
| HarmonicMonophonicFilter | MonophonicEffect | 4 | No | No | Yes | |
| CurveEq | MasterEffect | 0 | No | No | Yes | Dynamic bands |
| StereoEffect | VoiceEffect | 2 | Yes | Yes | Yes | Needs metadataInitialised |
| SimpleReverbEffect | MasterEffect | 6 | No | No | Yes | |
| GainEffect | MasterEffect | 5 | Yes | No | Yes | 4 internal chains |
| ConvolutionEffect | MasterEffect | 10 | Yes | No | Yes | |
| DelayEffect | MasterEffect | 8 | Yes | No | Yes | Has tempo sync |
| ChorusEffect | MasterEffect | 4 | No | No | Yes | |
| PhaseFX | MasterEffect | 4 | No | No | Yes | 1 internal chain |
| RouteEffect | MasterEffect | 0 | No | No | Yes | Zero params |
| SendEffect | MasterEffect | 4 | Yes | No | Yes | 1 internal chain |
| SaturatorEffect | MasterEffect | 4 | Yes | No | Yes | 1 internal chain |
| SlotFX | MasterEffect | 0 | No | No | Yes | Container |
| EmptyFX | MasterEffect | 0 | No | No | Yes | Does nothing |
| DynamicsEffect | MasterEffect | 18 | Yes | No | Yes | Gate/Compressor/Limiter |
| AnalyserEffect | MasterEffect | 2 | No | No | Yes | |
| ShapeFX | MasterEffect | 13 | Yes | Yes | Yes | Needs metadataInitialised |
| PolyshapeFX | VoiceEffect | 4 | Yes | No | Yes | 1 internal chain |
| MidiMetronome | MasterEffect | 3 | Yes | No | Yes | |
| NoiseGrainPlayer | VoiceEffect | 4 | Yes | No | Yes | AudioSampleProcessor |

### SoundGenerators

| Processor | Params | Has getDefaultValue | InitList calls getDefaultValue | Has editor | Notes |
|---|---|---|---|---|---|
| ModulatorSampler | 16 (4 base + 12 own) | No | No | Yes | Complex sampler |
| SineSynth | 10 (4 base + 6 own) | Yes | Yes | Yes | Needs metadataInitialised |
| ModulatorSynthChain | 4 (4 base + 0 own) | No | No | Yes | Container, base params only |
| GlobalModulatorContainer | 4 (4 base + 0 own) | No | No | No | Container |
| NoiseSynth | 4 (4 base + 0 own) | No | No | Yes | Base params only |
| WavetableSynth | 8 (4 base + 4 own) | Yes | No | Yes | Extra internal chains |
| AudioLooper | 10 (4 base + 6 own) | Yes | No | Yes | |
| ModulatorSynthGroup | 12 (4 base + 8 own) | No | No | Yes | FM synthesis |
| MacroModulationSource | 4 (4 base + 0 own) | No | No | No | Container |
| SendContainer | 4 (4 base + 0 own) | No | No | No | Container |
| SilentSynth | 4 (4 base + 0 own) | No | No | Yes | Produces silence |

### Hardcoded Script Processors

These use the variant pattern described in Section 3. Parameters come from `onInit()` Content widget creation.

| Processor | Params | Notes |
|---|---|---|
| Arpeggiator | 15 | Manual parameterNames.add() pattern |
| LegatoProcessor | 0 | No Content widgets |
| CCSwapper | 2 | FirstCC, SecondCC |
| ReleaseTriggerScriptProcessor | 3 | TimeAttenuate, Time, TimeTable |
| CCToNoteProcessor | 2 | Bypass, ccSelector |
| ChannelFilterScriptProcessor | 3 | channelNumber, mpeStart, mpeEnd |
| ChannelSetterScriptProcessor | 1 | channelNumber |
| MuteAllScriptProcessor | 2 | ignoreButton, fixStuckNotes |

### Excluded from migration (dynamic parameters)

These processors have parameters defined at runtime by scripts or opaque nodes. They remain as fallback entries in the registry.

| Processor | Reason |
|---|---|
| JavascriptVoiceStartModulator | Scripting processor |
| JavascriptTimeVariantModulator | Scripting processor |
| JavascriptEnvelopeModulator | Scripting processor |
| JavascriptMidiProcessor | Scripting processor |
| JavascriptMasterEffect | Scripting processor |
| JavascriptPolyphonicEffect | Scripting processor |
| JavascriptSynthesiser | Scripting processor |
| HardcodedTimeVariantModulator | DLL wrapper, params from opaque node |
| HardcodedEnvelopeModulator | DLL wrapper, params from opaque node |
| HardcodedMasterFX | DLL wrapper, params from opaque node |
| HardcodedPolyphonicFX | DLL wrapper, params from opaque node |
| HardcodedSynthesiser | DLL wrapper, params from opaque node |

## 7. Validation

After migrating a processor, verify:

1. **Parameter count matches** - `createMetadata().parameters.size()` equals the old `parameterNames` count
2. **Parameter order preserved** - IDs appear in the same order as the old `parameterNames.add()` calls
3. **Default values match** - each parameter's `.withDefault()` matches the old `getDefaultValue()` return
4. **Ranges match** - compare against editor `.setMode()` / `.setRange()` calls
5. **Modulation chains match** - count and order match the `InternalChains` enum
6. **No leftover parameterNames.add()** - the constructor body should not add parameter names (metadata handles this via `updateParameterSlots()`)
7. **No leftover updateParameterSlots()** in constructor body - it runs via `metadataInitialised` in the initializer list
8. **Editor compiles** - `md.setup()` calls compile and reference the correct enum values
9. **Build succeeds** - full build with no errors
10. **Unit tests pass** - run the ProcessorMetadata tests in CI configuration
