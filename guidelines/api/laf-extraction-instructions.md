# LAF Style Guide Extraction Instructions

**Related Documentation:**
- [mcp-laf-integration.md](mcp-laf-integration.md) - How MCP servers use the extracted LAF data

## Overview

This document describes the procedure to extract LookAndFeel (LAF) function documentation from the HISE source code for use by an MCP server.

**Output Location:** `hi_scripting/scripting/api/laf_style_guide.json`

---

## Step 1: Extract LAF Function Names

**File:** `hi_scripting/scripting/api/ScriptingGraphics.cpp`

**Method:** `ScriptingObjects::ScriptedLookAndFeel::getAllFunctionNames()`

Extract the complete `Array<Identifier>` list. This is the authoritative source for all LAF function names.

---

## Step 2: Extract ScriptComponent Types

**File:** `hi_scripting/scripting/api/ScriptingApiContent.h`

**Pattern:** Search for classes inheriting from `ScriptComponent` with `getStaticObjectName()` methods.

**Components with LAF support:**
- `ScriptSlider`
- `ScriptButton`
- `ScriptComboBox`
- `ScriptTable`
- `ScriptSliderPack`
- `ScriptAudioWaveform`

---

## Step 3: Extract FloatingTile ContentTypes (Optimized)

### 3a. Known ContentTypes (Hardcoded Reference)

| Class Name | ContentType String |
|------------|-------------------|
| `EmptyComponent` | `Empty` |
| `PresetBrowserPanel` | `PresetBrowser` |
| `AboutPagePanel` | `AboutPagePanel` |
| `MidiKeyboardPanel` | `Keyboard` |
| `PerformanceLabelPanel` | `PerformanceLabel` |
| `MidiOverlayPanel` | `MidiOverlayPanel` |
| `ActivityLedPanel` | `ActivityLed` |
| `CustomSettingsWindowPanel` | `CustomSettings` |
| `MidiSourcePanel` | `MidiSources` |
| `MidiChannelPanel` | `MidiChannelList` |
| `TooltipPanel` | `TooltipPanel` |
| `MidiLearnPanel` | `MidiLearnPanel` |
| `FrontendMacroPanel` | `FrontendMacroPanel` |
| `PlotterPanel` | `Plotter` |
| `AudioAnalyserComponent::Panel` | `AudioAnalyser` |
| `WaveformComponent::Panel` | `Waveform` |
| `FilterGraph::Panel` | `FilterDisplay` |
| `FilterDragOverlay::Panel` | `DraggableFilterPanel` |
| `WaterfallComponent::Panel` | `WavetableWaterfall` |
| `MPEPanel` | `MPEPanel` |
| `ModulationMatrixPanel` | `ModulationMatrix` |
| `ModulationMatrixControlPanel` | `ModulationMatrixController` |
| `AhdsrEnvelope::Panel` | `AHDSRGraph` |
| `FlexAhdsrEnvelope::Panel` | `FlexAHDSRGraph` |
| `MarkdownPreviewPanel` | `MarkdownPanel` |
| `MatrixPeakMeter` | `MatrixPeakMeter` |

### 3b. Quick Validation

**File:** `hi_core/hi_components/floating_layout/FloatingTileFactoryMethods.cpp`

**Method:** `registerFrontendPanelTypes()`

1. Read the method body
2. Extract all class names from `registerType<ClassName>()` calls
3. Compare against the class names in the table above
4. **Match** → use the hardcoded ContentType strings, done
5. **New class found** → do full research (Step 3c) for that class only

### 3c. Full Research (Only for unknown classes)

For each new class not in the hardcoded list:

1. Grep for `SET_PANEL_NAME` in `.h` files containing that class
2. Extract the ContentType string
3. **Update the hardcoded table in 3a** for future runs

---

## Step 4: Extract LAF Function Callback Properties

**File:** `hi_scripting/scripting/api/ScriptingGraphics.cpp`

For each LAF function, find its implementation and extract the callback object properties from `obj->setProperty()` calls.

### 4a. Common Helper Functions (Hardcoded Reference)

These helper functions are used across multiple LAF implementations. When you encounter them, use the hardcoded property lists below instead of tracing into their implementations.

**`writeId(obj, component)`** adds:
| Property | Type | Description |
|----------|------|-------------|
| `id` | String | The component's ID from `getComponentID()` |

**`ModulationDisplayValue::storeToJSON(obj)` / `mv.storeToJSON(obj)`** adds:
| Property | Type | Description |
|----------|------|-------------|
| `valueNormalized` | double | Value normalized to 0.0-1.0 range |
| `scaledValue` | double | Modulation scale factor |
| `addValue` | double | Modulation additive offset |
| `modulationActive` | bool | Whether modulation is active |
| `modMinValue` | double | Minimum of modulation range (0.0-1.0) |
| `modMaxValue` | double | Maximum of modulation range (0.0-1.0) |
| `lastModValue` | double | Last modulation value |

**`addParentFloatingTile(component, obj)`** adds:
| Property | Type | Description |
|----------|------|-------------|
| `parentType` | String | ContentType of parent FloatingTile (if any) |

**`setColourOrBlack(obj, propertyName, component, colourId)`** adds:
| Property | Type | Description |
|----------|------|-------------|
| *(propertyName)* | int (ARGB) | The colour value as 32-bit ARGB integer, or 0 if not specified |

**`ApiHelpers::getVarRectangle(useRectangleClass, rect)`** returns:
| Property | Type | Description |
|----------|------|-------------|
| *(result)* | Array[x, y, w, h] | Rectangle as [x, y, width, height] array |

**`ApiHelpers::getVarFromPoint(point)`** returns:
| Property | Type | Description |
|----------|------|-------------|
| *(result)* | Array[x, y] | Point as [x, y] array |

### 4b. Validation Check for Helpers

If you encounter a helper function call not listed in 4a:

1. Trace into its implementation
2. Document the properties it adds
3. **Update the hardcoded table in 4a** for future runs

---

## Step 5: Map LAF Functions to Components/ContentTypes

### 5a. Hardcoded Mappings (Reference)

**ScriptComponents:**

| Component | LAF Functions |
|-----------|--------------|
| `ScriptSlider` | `drawRotarySlider`, `drawLinearSlider` |
| `ScriptButton` | `drawToggleButton` |
| `ScriptComboBox` | `drawComboBox` |
| `ScriptTable` | `drawTableBackground`, `drawTablePath`, `drawTablePoint`, `drawTableMidPoint`, `drawTableRuler` |
| `ScriptSliderPack` | `drawSliderPackBackground`, `drawSliderPackFlashOverlay`, `drawSliderPackRightClickLine`, `drawSliderPackTextPopup` |
| `ScriptAudioWaveform` | `drawThumbnailBackground`, `drawThumbnailText`, `drawThumbnailPath`, `drawThumbnailRange`, `drawThumbnailRuler`, `getThumbnailRenderOptions`, `drawMidiDropper` |

**FloatingTile ContentTypes:**

| ContentType | LAF Functions |
|-------------|--------------|
| `PresetBrowser` | `createPresetBrowserIcons`, `drawPresetBrowserBackground`, `drawPresetBrowserDialog`, `drawPresetBrowserColumnBackground`, `drawPresetBrowserListItem`, `drawPresetBrowserSearchBar`, `drawPresetBrowserTag` |
| `Keyboard` | `drawKeyboardBackground`, `drawWhiteNote`, `drawBlackNote` |
| `AHDSRGraph` | `drawAhdsrBackground`, `drawAhdsrBall`, `drawAhdsrPath` |
| `FlexAHDSRGraph` | `drawFlexAhdsrBackground`, `drawFlexAhdsrCurvePoint`, `drawFlexAhdsrFullPath`, `drawFlexAhdsrPosition`, `drawFlexAhdsrSegment`, `drawFlexAhdsrText` |
| `FilterDisplay` | `drawFilterBackground`, `drawFilterPath`, `drawFilterGridLines` |
| `DraggableFilterPanel` | `drawFilterDragHandle` |
| `AudioAnalyser` | `drawAnalyserBackground`, `drawAnalyserPath`, `drawAnalyserGrid` |
| `Waveform` | `drawWavetableBackground`, `drawWavetablePath` |
| `MatrixPeakMeter` | `drawMatrixPeakMeter` |
| `ModulationMatrix` | `getModulatorDragData`, `drawModulationDragBackground`, `drawModulationDragger` |

**Global (System UI):**

| Category | LAF Functions |
|----------|--------------|
| AlertWindow | `drawAlertWindow`, `getAlertWindowMarkdownStyleData`, `drawAlertWindowIcon` |
| PopupMenu | `drawPopupMenuBackground`, `drawPopupMenuItem`, `getIdealPopupMenuItemSize` |
| Scrollbar | `drawScrollbar` |
| Dialog | `drawDialogButton` |
| NumberTag | `drawNumberTag` |
| TableListBox | `drawTableRowBackground`, `drawTableCell`, `drawTableHeaderBackground`, `drawTableHeaderColumn` |

### 5b. Validation Check

After extracting LAF functions in Step 1, compare against the hardcoded function lists in Step 5a:

1. Collect all function names from the hardcoded tables
2. Compare against the extracted list from `getAllFunctionNames()`
3. **All functions accounted for** → use hardcoded mappings, done
4. **New function(s) found** → proceed to 5c

### 5c. Research Unknown Functions

For each new LAF function not in the hardcoded mappings:

1. Find its implementation in `ScriptingGraphics.cpp`
2. Look at the class hierarchy - which `LookAndFeelMethods` class contains it
3. Determine the target component/ContentType from:
   - Function name prefix (e.g., `drawNewComponent*` → likely a "NewComponent" type)
   - The `obj->setProperty()` calls that reference component-specific data
4. Add to appropriate category (ScriptComponent, FloatingTile ContentType, or Global)
5. **Update the hardcoded tables in 5a** for future runs

---

## Output Format

```json
{
  "version": "1.0",
  "generated": "<date>",
  "categories": {
    "ScriptComponents": {
      "description": "UI components created via Content.addXxx() methods",
      "components": {
        "<ComponentName>": {
          "lafFunctions": {
            "<functionName>": {
              "description": "...",
              "callbackProperties": {
                "<propertyName>": {
                  "type": "...",
                  "description": "..."
                }
              }
            }
          }
        }
      }
    },
    "FloatingTileContentTypes": {
      "description": "ContentType values for ScriptFloatingTile components",
      "contentTypes": {
        "<ContentType>": {
          "lafFunctions": { ... }
        }
      }
    },
    "Global": {
      "description": "LAF functions that apply globally or to system UI elements",
      "categories": {
        "<CategoryName>": {
          "lafFunctions": { ... }
        }
      }
    }
  }
}
```
