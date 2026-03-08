# HISE Build Configurations

**Related Documentation:**
- [cpp-architecture.md](cpp-architecture.md) - Architecture guidelines
- [threading-system.md](threading-system.md) - Threading system (also affected by build target)

---

## Build Target Architecture

HISE has three primary build targets. Both the **Backend** and **Frontend** derive from JUCE's `AudioProcessor` and compose HISE's `MainController`, but use completely different processor implementations:

```
                    ┌─────────────────┐
                    │  AudioProcessor │  (JUCE plugin base class)
                    │  MainController │  (HISE core engine)
                    └────────┬────────┘
                             │
            ┌────────────────┴────────────────┐
            │                                 │
            ▼                                 ▼
┌───────────────────────┐       ┌───────────────────────┐
│   BackendProcessor    │       │   FrontendProcessor   │
│   (hi_backend module) │       │   (hi_frontend module)│
│                       │       │                       │
│ - Full IDE            │       │ - Exported plugins    │
│ - Script editing      │       │ - Embedded presets    │
│ - Console output      │       │ - Copy protection     │
│ - Export tools        │       │ - Minimal footprint   │
│ - REST API server     │       │                       │
│ - Loads Project DLLs  │       │                       │
└───────────────────────┘       └───────────────────────┘
```

Shared modules (`hi_core`, `hi_scripting`, `hi_dsp_library`, etc.) are always included. `hi_backend` code **never ships** in exported plugins. `hi_frontend` code **never runs** in the HISE IDE.

---

## Primary Macros

### `USE_BACKEND`

Enables the HISE IDE/development environment. When `1`, includes:
- Full IDE functionality (code editing, debugging, console output)
- Development tools (script console, processor editors, floating panels)
- Export functionality (`CompileExporter`, project export tooling)
- REST API server for AI agent integration

### `USE_FRONTEND`

Enables the exported plugin runtime. When `1`, includes:
- Embedded preset/sample loading
- Copy protection support (`USE_COPY_PROTECTION`)
- Stripped-down UI (no IDE, no script editing)

### Mutual Exclusivity

`USE_BACKEND` and `USE_FRONTEND` are **mutually exclusive**, enforced in `LibConfig.h`:

```cpp
#if USE_BACKEND
#undef USE_FRONTEND
#define USE_FRONTEND 0
#endif
```

Never assume both can be true simultaneously.

### `HI_EXPORT_AS_PROJECT_DLL`

Enables compilation as a standalone DSP library (DLL). See [Project DLL Architecture](#project-dll-architecture) below.

### Secondary Macros

| Macro | Purpose |
|-------|---------|
| `IS_STANDALONE_APP` | Standalone app features (audio device settings, window management) |
| `IS_STANDALONE_FRONTEND` | Exported standalone application (not plugin) |
| `FRONTEND_IS_PLUGIN` | Exported plugin is an FX effect (stereo in/out) |
| `HISE_BACKEND_AS_FX` | Simulates FX plugin routing in the backend for development |
| `USE_RAW_FRONTEND` | Pure C++ project bypassing HISEScript/scripted interfaces |

---

## Build Target Matrix

| Macro | HISE IDE | Exported Plugin | Exported Standalone | Project DLL |
|-------|----------|-----------------|---------------------|-------------|
| `USE_BACKEND` | **1** | 0 | 0 | 0 |
| `USE_FRONTEND` | 0 | **1** | **1** | 0 |
| `IS_STANDALONE_APP` | 1 | 0 | **1** | 0 |
| `HI_EXPORT_AS_PROJECT_DLL` | 0 | 0 | 0 | **1** |
| `HISE_NO_GUI_TOOLS` | 0 | 0 | 0 | **1** |

---

## Code Separation Guidelines

### Principle

IDE-specific code that would cause **binary bloat** or **runtime overhead** in exported plugins must be guarded with `#if USE_BACKEND`. The exported plugin should contain only what the end user needs.

### What Must Be Guarded

| Code Type | Guard with `#if USE_BACKEND`? |
|-----------|-------------------------------|
| Editor/UI components for the IDE | **Yes** |
| Console/debug output (`debugToConsole`) | **Yes** |
| Development tools (exporters, analyzers, script editors) | **Yes** |
| Validation with string formatting (debug-only) | **Yes** (or use `jassert` which compiles out) |
| Core DSP processing | No |
| Runtime parameter handling | No |
| Audio file loading/streaming | No |
| Scripted UI components (user-facing) | No |

**Rule of thumb:** If the end user of the exported plugin will never see or interact with it, it should be guarded with `#if USE_BACKEND`.

### Examples

```cpp
// GOOD: IDE-only debug logging is guarded
#if USE_BACKEND
    debugToConsole(processor, "Parameter changed: " + String(value));
#endif

// GOOD: Editor class only exists in backend
#if USE_BACKEND
class MyProcessorEditor : public ProcessorEditorBody
{
    // Full editor with knobs, graphs, debug views
};
#endif

// BAD: Debug code compiled into exported plugins
void processBlock(AudioBuffer<float>& buffer)
{
    debugToConsole(this, "Processing...");  // Binary bloat + runtime overhead
}
```

### Expansion Editing

Some features are needed in both the IDE and during expansion development:

```cpp
#if USE_BACKEND || HI_ENABLE_EXPANSION_EDITING
    // Sample map editing, wavetable tools
#endif
```

---

## Project DLL Architecture

The Project DLL allows users to run custom C++ DSP code (user-written or auto-generated from scriptnode's code generator) inside the HISE IDE **without recompiling the entire HISE application**. The HISE IDE loads this DLL at runtime.

**Critical:** When exporting a project as a plugin (VST3/AU/AAX), the DLL's C++ code gets **compiled directly into the exported plugin binary**. The DLL concept is completely irrelevant in the exported plugin -- it exists solely as a development convenience for the HISE IDE.

### `HISE_NO_GUI_TOOLS`

The Project DLL sets `HISE_NO_GUI_TOOLS=1` because it is a **pure DSP library** with no UI. This disables:
- JUCE GUI modules (`juce_gui_extra`, `juce_opengl`)
- Markdown system, code editor, CSS system
- Image effects, OpenGL, Lottie animations
- Embedded fonts (falls back to generic `Font()`)
- ScriptNode UI (`HISE_INCLUDE_SCRIPTNODE_UI=0`)

This results in faster compilation and smaller binary size.

### DLL-Specific Constraints

Code inside `#if HI_EXPORT_AS_PROJECT_DLL` handles special constraints of running as a loaded DLL:

| Constraint | Reason |
|------------|--------|
| Custom `TimerThread` | DLL can't use the host's JUCE message manager |
| No buffer reallocation | DLL runs in a different memory space; cross-heap operations cause corruption |
| Custom UI timer implementation | Uses `SharedResourcePointer<TimerThread>` instead of JUCE `Timer` |

---

## Guidelines for AI Coding Agents

1. **Check guard context:** When modifying code inside `#if USE_BACKEND`, remember it won't exist in exported plugins. If functionality is needed in both, place it outside the guard or in a shared module.

2. **Guard new IDE features:** When adding editor panels, debug output, or development tools, always wrap in `#if USE_BACKEND`.

3. **Don't break frontend builds:** Changes to shared modules (`hi_core`, `hi_scripting`, `hi_dsp_library`) must compile with both `USE_BACKEND=1` and `USE_FRONTEND=1`.

4. **Tests run in backend:** Unit tests run with `HI_RUN_UNIT_TESTS=1` + `USE_BACKEND=1`. Frontend-specific code paths may need separate verification.

5. **DLL memory rules:** When working on code that runs under `HI_EXPORT_AS_PROJECT_DLL`, don't reallocate buffers from the host application and use the custom `TimerThread` instead of JUCE's `Timer`.
