# RTNeural Static Export Plan

This document tracks the static neural export pipeline for HISE neural networks.

## Goals

- Keep scripting dynamic for import and authoring.
- Use `DspNetworks/NeuralNetworks/*.json` and supported `.nam` files as the source of truth for compiled neural models.
- Let users create compile-source JSON with `ScriptNeuralNetwork.writeCompiledModelJSON()`.
- Use the existing DspNetwork DLL compiler path so the IDE can verify the same compiled backend as exported plugins.
- Avoid requiring scripts to instantiate or load every neural network before compiling the DLL.
- Keep plugin export deterministic: project neural files are compile inputs, not runtime assets.
- Avoid cross-DLL heap and lifetime issues for DLL-backed generated neural models.

## Current User Workflow

Authoring / conversion:

```js
const nn = Engine.createNeuralNetwork("MyModel");

nn.loadTensorFlowModel(modelData);
// or nn.loadPytorchModel(modelData);
// or nn.loadNAMModel(modelData);

nn.writeCompiledModelJSON({});
```

HiseScript does not support overloaded methods with different argument counts, so `writeCompiledModelJSON()` should expect one dynamic object argument. Passing an empty object means default behaviour:

```js
nn.writeCompiledModelJSON({});
```

If no quality configuration entries are supplied, omit quality metadata from the JSON and generate one implicit default variant internally.

This writes or replaces:

```text
DspNetworks/NeuralNetworks/MyModel.json
```

Runtime after DLL compilation:

```js
const nn = Engine.createNeuralNetwork("MyModel");
Console.print(nn.getNetworkState()); // "compiled" if the DLL contains the generated model
Console.print(trace(nn.getNetworkInfo())); // includes available quality configurations
```

`DspNetworks/NeuralNetworks` is a compile-source folder. It is not an import folder and not a runtime asset folder.

## Runtime States

The scripting API exposes the actual active backend:

```js
nn.getNetworkState(); // "empty" | "dynamic" | "compiled"
```

| State | Meaning | Export-safe |
| --- | --- | --- |
| `empty` | No model is loaded and no compiled model is registered. | Only if unused. |
| `dynamic` | The script explicitly loaded model data through a loader such as `loadTensorFlowModel()`, `loadPytorchModel()`, `loadNAMModel()`, or `build()`. | Yes, if the script supplies the data in the exported plugin. |
| `compiled` | A generated static model is registered from the project DLL or exported plugin binary. | Yes. |

Internally, distinguish at least:

- `Empty`
- `Dynamic`
- `CompiledLinked`
- `CompiledDll`

There is intentionally no initial `file-based` runtime state. If a compiled model is not available and the script does not manually load model data, the network remains `empty`.

Quality configuration switching only applies to compiled models:

```js
nn.setQualityConfiguration("low");
```

Rules:

- If the current state is `compiled` and the named variant exists, swap the active compiled model and call `reset()`.
- If the current state is `empty`, fail because no compiled model is registered.
- If the current state is `dynamic`, fail because quality configurations only apply to compiled neural networks.
- If the named variant does not exist, fail with a list of available quality configurations.
- Swapping must use the existing audio-safe kill-state mechanism.

`getNetworkInfo()` should report the available and active quality settings. No separate quality-list getter is required.

## Canonical JSON Format

Files in `DspNetworks/NeuralNetworks` are compile inputs for statically compiled neural networks.

Supported compile inputs:

- HISE canonical compiled-model `.json` files for generic RTNeural models.
- HISE canonical compiled-model `.json` files for NAM models.
- Raw `.nam` files for the supported plain WaveNet NAM subset.

Unsupported compile inputs:

- Raw PyTorch import files.
- Raw TensorFlow import files.
- Unsupported NAM architectures or WaveNet feature variants.

The filename without extension is the neural network ID:

```text
DspNetworks/NeuralNetworks/MyModel.json -> Engine.createNeuralNetwork("MyModel")
DspNetworks/NeuralNetworks/MyModel.nam  -> Engine.createNeuralNetwork("MyModel")
```

The filename stem must be a valid HISE identifier. Downloaded NAM files with spaces, punctuation, brackets, or leading numbers must be renamed by the project developer before export.

Generic RTNeural model:

```json
{
    "hise": {
        "format": "HiseCompiledNeuralNetwork",
        "version": 1,
        "id": "MyModel",
        "modelType": "rtneural",
        "sourceFormat": "TensorFlow",
        "createdBy": "ScriptNeuralNetwork.writeCompiledModelJSON",
        "numInputs": 1,
        "numOutputs": 1
    },
    "in_shape": [1],
    "layers": []
}
```

NAM model:

```json
{
    "hise": {
        "format": "HiseCompiledNeuralNetwork",
        "version": 1,
        "id": "MyNamModel",
        "modelType": "nam",
        "sourceFormat": "NAM",
        "createdBy": "ScriptNeuralNetwork.writeCompiledModelJSON",
        "numInputs": 1,
        "numOutputs": 1
    },
    "version": "0.5.4",
    "architecture": "WaveNet",
    "config": {
        "layers": [],
        "head": null,
        "head_scale": 0.02
    },
    "weights": []
}
```

The `hise` metadata object is HISE-specific. Generic RTNeural payloads keep `in_shape` and `layers` at the top level so the existing RTNeural JSON parser can consume the file.

Raw `.nam` files do not contain `hise` metadata. They are accepted directly as compile inputs if the filename stem is a valid HISE identifier and the NAM architecture is in the supported subset.

## Quality Configurations

Quality configurations allow one canonical JSON file to generate multiple statically compiled model variants with the same topology and weights but different compile-time RTNeural options.

Example authoring call:

```js
nn.writeCompiledModelJSON({
    "hi": {
        "mathProvider": "default",
        "sampleRateCorrection": "linear"
    },
    "low": {
        "mathProvider": "fastMath",
        "sampleRateCorrection": "none"
    }
});
```

This writes:

```json
"hise": {
    "qualityConfigurations": {
        "hi": {
            "mathProvider": "default",
            "sampleRateCorrection": "linear"
        },
        "low": {
            "mathProvider": "fastMath",
            "sampleRateCorrection": "none"
        }
    }
}
```

If `writeCompiledModelJSON({})` is called, or if the supplied object has no quality entries, do not write `hise.qualityConfigurations`. Missing quality metadata means the generator creates exactly one implicit default variant that matches the current behaviour.

If quality configurations are supplied, generate one variant per key. If a configuration named `default` exists, use it as the initial active compiled variant. Otherwise use a deterministic key order for registration and initial selection. Scripts can select a specific variant with `setQualityConfiguration()` after the network has resolved to `compiled`.

Initial supported options:

| JSON option | Values | Static meaning |
| --- | --- | --- |
| `mathProvider` | `default`, `fastMath` | Selects the RTNeural maths provider template argument. |
| `sampleRateCorrection` | `none`, `linear` | Selects the RTNeural sample-rate correction template argument. |

Default mapping:

| JSON value | C++ symbol |
| --- | --- |
| `mathProvider: "default"` | `RTNeural::DefaultMathsProvider` |
| `mathProvider: "fastMath"` | The configured fast RTNeural maths provider, once available. |
| `sampleRateCorrection: "none"` | `RTNeural::SampleRateCorrectionMode::None` |
| `sampleRateCorrection: "linear"` | `RTNeural::SampleRateCorrectionMode::LinInterp` |

The implicit default must exactly match the current generated/static path. Do not add quality metadata just to express this default.

Possible future options:

- `scalarType`: likely `float` first, maybe `double` later if the HISE ABI and buffers support it cleanly.
- More `mathProvider` values if RTNeural exposes additional provider policies.
- More `sampleRateCorrection` values only if RTNeural exposes matching static enum values.

Do not expose implementation selectors such as scalar / xsimd / eigen as per-model quality settings unless RTNeural supports them as clean per-model template choices. They currently look like build-level choices, not model-level quality options.

`getNetworkInfo()` should include quality data for compiled models:

```json
{
    "state": "compiled",
    "id": "MyModel",
    "activeQualityConfiguration": "low",
    "qualityConfigurations": ["hi", "low"]
}
```

If the JSON had no quality metadata, report the implicit internal default:

```json
{
    "state": "compiled",
    "id": "MyModel",
    "activeQualityConfiguration": "default",
    "qualityConfigurations": ["default"]
}
```

## Source Of Truth

Use `DspNetworks/NeuralNetworks/*.json` and supported `.nam` files as the primary source for compiled neural generation.

`MainController::getNeuralNetworks().getIdList()` is not the compile source of truth. It should only be used for diagnostics, for example:

- Warn if a script-created neural network has no matching compiled `.json` or supported `.nam` file and remains empty.
- Warn if a `math.neural` node references an unresolved network ID.
- Optionally warn if a compiled JSON file appears unused.

Compilation intent is implicit:

```text
DspNetworks/NeuralNetworks/Foo.json exists -> Foo is a compiled neural model candidate.
DspNetworks/NeuralNetworks/Foo.nam exists  -> Foo is a compiled neural model candidate.
No compile-source file exists -> Foo must be script-managed or remain unused.
```

Do not add `setExportMode()` or `getExportMode()`.

## Export Policy

Plugin export must not depend on `DspNetworks/NeuralNetworks/*.json` or `.nam` files at runtime. These files are not embedded into the plugin.

Plugin export must fail if a compile-source neural file cannot be generated into C++.

Example error:

```text
Could not compile NAM model MyModel.nam: Unsupported NAM field `config.condition_dsp`
```

Script-loaded dynamic models remain supported for IDE authoring/import workflows and exported plugins, as long as the script supplies the model data. A script-created network without a compile-source file is not an export error by itself.

## Development Diary

Completed foundation work:

- Added `DspNetworks/NeuralNetworks` folder support.
- Added `BackendDllManager::getNeuralNetworkFiles()`.
- Added `ScriptNeuralNetwork.writeCompiledModelJSON()`.
- Added `ScriptNeuralNetwork.getNetworkState()`.
- Added `ScriptNeuralNetwork.getNetworkInfo()`.
- Added backend states for `empty`, `dynamic`, and `compiled` reporting.
- Added canonical JSON retention and writing for TensorFlow / RTNeural JSON.
- Added canonical JSON retention and writing for NAM JSON.
- Made the DspNetwork compiler detect neural `.json` and supported `.nam` files as compile inputs.
- Allowed neural-only DLL compile setup to proceed without an active scriptnode network.
- Added static C++ generation and linked registration for RTNeural and the supported NAM subset.
- Added export validation for unresolved neural network IDs.

Known current limitations:

- PyTorch import does not yet emit canonical compiled-model JSON.
- DLL neural registration / ABI is not implemented yet.

## Static Metadata Design

The RTNeural compiled generator should reuse RTNeural's existing JSON parser for topology discovery instead of cloning its layer dispatch logic.

RTNeural-side addition:

```cpp
struct StaticWeightInfo
{
    std::string name;
    std::vector<int> shape;
    size_t flatNumElements = 0;
};

struct StaticTypeOptions
{
    std::string qualityId = "default";
    std::string scalarType = "float";
    std::string mathProvider = "default";
    std::string sampleRateCorrection = "none";
};

struct StaticLayerInfo
{
    std::string typeName;
    std::vector<StaticWeightInfo> weights;
    bool isActivation = false;
    bool supported = false;
    std::string error;
};

struct StaticModelInfo
{
    int inputSize = 0;
    int outputSize = 0;
    std::vector<StaticLayerInfo> layers;
    bool supported = false;
    std::string error;
};
```

Add a virtual API to the dynamic RTNeural layer base class:

```cpp
virtual StaticLayerInfo getStaticTypeInfo(const StaticTypeOptions& options) const;
```

Add a model helper:

```cpp
StaticModelInfo getStaticTypeInfo(const StaticTypeOptions& options) const;
```

`StaticTypeOptions` stores normalized option values, not raw C++ symbols. The HISE generator maps the normalized values to the exact C++ template arguments when emitting code.

The metadata should include expected source JSON weight tensor shapes, not actual float values. HISE uses the original canonical JSON as the source of exact weight data.

## RTNeural Codegen Flow

For `modelType: "rtneural"`:

1. Read `DspNetworks/NeuralNetworks/Foo.json` into `nlohmann::json`.
2. Validate HISE metadata and filename ID.
3. Resolve quality configurations from `hise.qualityConfigurations` or create one implicit `default` configuration.
4. Call `RTNeural::json_parser::parseJson<float>(json)` to create a dynamic reference model.
5. For each quality configuration, build `StaticTypeOptions` and call `model->getStaticTypeInfo(options)`.
6. Validate original JSON layer weights against `StaticModelInfo`.
7. Emit one `RTNeural::ModelT<float, input, output, Layers...>` typedef per quality configuration.
8. Emit baked byte arrays from the original JSON weights.
9. Generate `NeuralNetwork::ModelBase` compatible wrappers that load baked weights without runtime JSON parsing.
10. Register generated variants with the neural factory under one logical network ID.

The dynamic reference model is used only during code generation. Generated compiled models must not parse JSON at runtime.

Quality variants must have identical logical topology, input size, and output size. If a static option changes the shape or requires a different layer mapping, fail code generation.

## RTNeural Layer Support

Implement `getStaticTypeInfo()` for dynamic layers with static equivalents:

- Dense
- Conv1D
- Conv2D where a safe static mapping exists
- LSTM
- GRU
- PReLU
- BatchNorm
- BatchNorm2D where a safe static mapping exists
- Tanh activation
- ReLU activation
- Sigmoid activation
- Softmax / ELU if matching static template layers exist

Unsupported layers must return `supported = false` with a precise error.

Generation must stay conservative:

- Dynamic script loading can support any model accepted by the existing dynamic parser.
- Compiled generation only supports models that can be represented exactly with static template types.
- Unsupported static mappings must fail generation with a clear error.

## NAM Static Generation

NAM uses a dedicated compiled path, not the generic sequential `RTNeural::ModelT` path.

The runtime and compiled NAM paths support a plain WaveNet subset:

- `architecture: "WaveNet"`.
- Top-level `config.head` is `null` or absent.
- No `condition_dsp`.
- Each layer-array uses mono conditioning: `condition_size == 1`.
- Each layer-array uses `activation: "Tanh"`.
- Each layer-array uses `gated: false`.
- No packed, slimmable, FiLM, grouped convolution, grouped input mixin, explicit bottleneck, `head_1x1`, or disabled/grouped `layer_1x1` features.
- Weight count must exactly match the detected layer-array configuration.

Runtime NAM loading currently dispatches two known plain WaveNet signatures:

```cpp
wavenet::Wavenet_Model<float,
                       1,
                       wavenet::Layer_Array<float, 1, 1, 8, 16, 3, Dilations, false, NAMMathsProvider>,
                       wavenet::Layer_Array<float, 16, 1, 1, 8, 3, Dilations, true, NAMMathsProvider>>
```

and:

```cpp
wavenet::Wavenet_Model<float,
                       1,
                       wavenet::Layer_Array<float, 1, 1, 8, 8, 6, Dilations7, false, NAMMathsProvider>,
                       wavenet::Layer_Array<float, 8, 1, 8, 8, 6, Dilations7, false, NAMMathsProvider>,
                       wavenet::Layer_Array<float, 8, 1, 8, 8, 15, Dilations2, false, NAMMathsProvider>,
                       wavenet::Layer_Array<float, 8, 1, 1, 8, 6, Dilations7, true, NAMMathsProvider>>
```

Compiled NAM generation is more flexible within the same plain WaveNet subset. It emits the exact `wavenet::Layer_Array` template list from `config.layers`, then bakes the flat NAM `weights` array as bytes.

The compiled NAM generator should:

- Accept canonical `modelType: "nam"` JSON and raw `.nam` compile inputs.
- Validate metadata if it exists.
- Validate the flat `weights` array against `config.layers`.
- Emit the detected Wavenet topology.
- Bake weights into generated C++ byte arrays.
- Load the baked weights into the Wavenet model using the same order as `Wavenet_Model::load_weights()`.

Unsupported NAM files must fail with a clear diagnostic instead of falling back to dynamic loading or reading past the weight array.

## Baked Weights

Compiled models should bake all weights into generated C++.

Use raw byte arrays, not JSON strings and not decimal float literals.

Example generated data:

```cpp
alignas(16) static const unsigned char MyModel_lstm_kernel[] = { ... };
static constexpr int MyModel_lstm_kernelSize = 1234;
```

Generated loaders should use `static_assert(sizeof(float) == 4)` and copy byte data into aligned local float buffers before passing it to RTNeural setters.

Avoid direct aliasing of `unsigned char*` as `float*` unless alignment and aliasing are explicitly handled.

First implementation should prioritise correctness and readable generated code. Temporary vectors and existing RTNeural setters are acceptable during model construction. Optimise generated code only after correctness is verified.

## Export Pipeline Integration

Generate neural model headers into the generated source folder used by compiled DspNetworks.

Include generated neural model headers through the generated `includes.h` file so DLL and exported-plugin builds compile the same model classes.

Generate neural registration code beside the generated `project::Factory` so both generated DLL `Main.cpp` and static exported `factory.cpp` use the same neural model list.

Export must validate every compile-source neural file by generating the matching C++ header. A frontend/export build must not depend on raw files from `DspNetworks/NeuralNetworks` at runtime. Script-managed dynamic neural models are still allowed if the script supplies their model data through the dynamic loaders.

## Linked Registration

In exported plugin builds there is no DLL boundary, so generated code can directly register generated classes with the neural factory.

Example shape:

```cpp
void registerCompiledNeuralNetworks(MainController* mc)
{
    auto f = mc->getNeuralNetworks().getFactory();
    f->registerModel<project::MyModel_default>("MyModel", "default");
    f->registerModel<project::MyModel_low>("MyModel", "low");
}
```

Registration must happen before scripts create or load `ScriptNeuralNetwork` objects.

If no quality metadata exists in the JSON, only register the implicit `default` variant. If quality metadata exists, register one variant per named quality configuration.

## DLL Registration

For IDE DLL builds, do not register raw template factory functions from the DLL into the host `NeuralNetwork::Factory`.

Raw DLL callbacks can dangle after reload, and raw DLL-allocated model objects must not be deleted by host-side `OwnedArray<ModelBase>`.

Use a lightweight host-side wrapper:

```cpp
struct DllCompiledNeuralModel: public NeuralNetwork::ModelBase
{
    void* dllModel = nullptr;
    ProjectDll::Ptr dll;
};
```

The wrapper forwards `process()`, `reset()`, `clone()`, input/output queries, state queries, and destruction through DLL-exported C ABI functions.

The generated DLL owns creation and destruction of the actual generated model object.

Required DLL exports:

```cpp
getNumNeuralModels();
getNeuralModelId(index, char* target);
getNeuralModelQualityId(index, char* target);
getNeuralModelState(index);
createNeuralModel(index);
destroyNeuralModel(void* model);
cloneNeuralModel(void* model);
resetNeuralModel(void* model);
processNeuralModel(void* model, const float* input, float* output);
getNeuralModelNumInputs(void* model);
getNeuralModelNumOutputs(void* model);
```

The host must never directly delete or call C++ methods on a DLL-owned generated model object.

## DLL Reload Lifecycle

On DLL reload:

1. Suspend audio safely through the existing kill-state mechanism.
2. Clear or detach existing DLL-backed neural model instances.
3. Deregister old DLL-backed neural factory entries.
4. Release the old DLL.
5. Load the new DLL.
6. Register new DLL-backed neural factory entries.
7. Let scripts recreate or reconnect neural networks as needed.

Existing DLL-backed neural models must not outlive the DLL that owns their implementation.

## Implementation Steps

1. Add RTNeural static metadata structs and virtual layer API.
2. Implement `getStaticTypeInfo()` for RTNeural dynamic layers with static equivalents.
3. Add `RTNeural::Model<T>::getStaticTypeInfo()` to aggregate model-level metadata.
4. Add HISE compile-source loader / validator for `DspNetworks/NeuralNetworks/*.json` and supported `.nam` files.
5. Extend `writeCompiledModelJSON({})` to accept optional quality configuration entries.
6. Omit quality metadata for an empty object and treat it as the implicit default.
7. Validate quality configuration names and option values.
8. For `modelType: "rtneural"`, build a dynamic reference model with `parseJson<float>()` and extract `StaticModelInfo` for every quality configuration.
9. Validate original JSON weights against `StaticModelInfo`.
10. Refactor `NeuralNetwork::CppBuilder` to consume canonical compile-source data, quality configs, and `StaticModelInfo`.
11. Emit one `RTNeural::ModelT` typedef and `NeuralNetwork::ModelBase` wrapper per quality configuration.
12. Emit baked byte arrays and readable weight-loading code using existing RTNeural setters.
13. Register generated variants under one logical neural network ID.
14. Add `ScriptNeuralNetwork.setQualityConfiguration()` with compiled-only reset-on-swap behaviour.
15. Extend `getNetworkInfo()` with `activeQualityConfiguration` and `qualityConfigurations`.
16. Add NAM compile-source validation and dedicated Wavenet generator for the supported plain WaveNet subset.
17. Generate neural headers from `DspNetworkCompileExporter::runStatic()`.
18. Include generated neural headers through `includes.h`.
19. Add linked registration for exported plugin builds.
20. Add DLL ABI functions and host-side `DllCompiledNeuralModel` wrapper.
21. Add owner-aware registration and deregistration to `NeuralNetwork::Factory`.
22. Wire DLL load and reload lifecycle to register and deregister DLL-backed neural models.
23. Add compile diagnostics and IDE-visible `getNetworkState()` verification.
24. Add plugin export validation that fails if used compile-source networks are not compiled.

## Immediate Milestone

Use the generated test file:

```text
DspNetworks/NeuralNetworks/TensorFlowNetwork.json
```

Expected topology:

```text
input: 1
layer 0: LSTM 1 -> 20
layer 1: Dense 20 -> 1
output: 1
```

First success criterion:

```text
TensorFlowNetwork.json -> StaticModelInfo -> generated TensorFlowNetwork.h -> registered compiled model -> getNetworkState() == "compiled"
```

Second success criterion:

```text
TensorFlowNetwork.json with quality configurations -> multiple generated variants -> setQualityConfiguration("low") swaps and resets the compiled model
```

## Open Questions

- Whether generated compiled models should keep compact source metadata for diagnostics.
- Whether `writeCompiledModelJSON()` should accept an optional ID / filename argument.
- Whether a later IDE-only `file-based` runtime fallback is worth the extra lifecycle complexity.
- Whether DLL-backed compiled neural models should be refreshed automatically after DLL reload or require scripts to recreate / reconnect model objects.
