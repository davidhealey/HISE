# Layer 1: Data Pipeline Implementation Guide

Produces new `XmlApi.h`/`XmlApi.cpp` files with `callScope` and `callScopeNote` properties embedded in the `apivaluetree_dat` binary blob. The 91 per-class `*_xml` Doxygen blobs previously bundled in these files are removed (they were dead code — never referenced at runtime).

**Prerequisite:** `api_enrich.py merge` must have been run to produce `enrichment/output/api_reference.json`.

**Deliverables:**
1. New `filter-binary` subcommand in `api_enrich.py`
2. New `ApiValueTreeBuilder.exe` JUCE console app
3. Updated `XmlApi.h` / `XmlApi.cpp` (ValueTree blob only, no XML blobs)
4. Updated `batchCreate.bat` (remove `ApiExtractor.exe` and `BinaryBuilder.exe`)

---

## Pipeline Overview

### Before

```
1. Clean xml/selection/
2. Doxygen → raw XML from C++ headers
3. Copy + rename → 91 class XMLs into xml/selection/
4. ApiExtractor.exe → simplified XML + apivaluetree.dat
5. BinaryBuilder.exe → XmlApi.h + XmlApi.cpp (1 ValueTree blob + 91 XML blobs)
```

### After

```
1. Clean xml/selection/
2. Doxygen → raw XML from C++ headers
3. Copy + rename → 91 class XMLs into xml/selection/
── batchCreate.bat ends here ──
4. python api_enrich.py phase0          (already exists — parses Doxygen XML)
   ... phases 1-3, merge ...            (already exist — enrichment pipeline)
5. python api_enrich.py filter-binary   (NEW — produces filtered_api.json)
6. ApiValueTreeBuilder.exe              (NEW — produces XmlApi.h + XmlApi.cpp)
```

Steps 4-6 are run manually, separate from `batchCreate.bat`. Steps 5-6 always use whatever `api_reference.json` currently exists — they only need re-running when the enrichment data changes or when the API surface changes (new Doxygen run).

### What's Removed

- **`ApiExtractor.exe`** — `api_enrich.py phase0` already parses the raw Doxygen XML
- **`BinaryBuilder.exe`** — `ApiValueTreeBuilder.exe` produces `XmlApi.h/.cpp` directly
- **91 per-class `*_xml` blobs** from `XmlApi.h/.cpp` (~6.5MB of dead code — declared but never referenced by any C++ consumer)

### Runtime Consumers (unchanged)

All three consumers use only `apivaluetree_dat` and require no code changes:

| Consumer | File | Line |
|----------|------|------|
| `ApiHelpers::getApiTree()` | `hi_scripting/scripting/api/ScriptingApiObjects.cpp` | 7017 |
| `ScriptingApiDatabase::Data` | `hi_backend/backend/doc_generators/ApiMarkdownGenerator.cpp` | 38 |
| `ApiCollection` | `hi_backend/backend/debug_components/ApiBrowser.cpp` | 341 |

---

## 1. `api_enrich.py filter-binary` Command

### File: `tools/api generator/api_enrich.py`

Add a new subcommand `filter-binary` that reads the full merged JSON and writes a stripped-down JSON containing only the fields needed by the C++ binary data.

### Integration Points

**Function definition** — insert before `run_preview()` (before line 2381):

```python
def run_filter_binary(output_path=None):
    ...

def _extract_brief(description: str, max_len: int = 200) -> str:
    ...
```

**Subparser registration** — insert before `args = parser.parse_args()` (before line 2498):

```python
filter_binary_parser = subparsers.add_parser(
    "filter-binary",
    help="Filter merged JSON -> minimal JSON for C++ binary embedding",
)
filter_binary_parser.add_argument(
    "output", nargs="?", default=None,
    help="Output path (default: enrichment/output/filtered_api.json)",
)
```

**Dispatch** — insert before the `else` branch (before line 2508):

```python
elif args.command == "filter-binary":
    run_filter_binary(args.output)
```

### Output Format

```json
{
  "classes": {
    "Console": {
      "methods": {
        "print": {
          "name": "print",
          "arguments": "(NotUndefined x)",
          "returnType": "undefined",
          "description": "Prints a value to the HISE console.",
          "callScope": "caution",
          "callScopeNote": "Allocates in HISE IDE; no-op in exported plugins"
        },
        "startBenchmark": {
          "name": "startBenchmark",
          "arguments": "()",
          "returnType": "undefined",
          "description": "Starts a high-resolution benchmark timer.",
          "callScope": "safe"
        }
      }
    }
  }
}
```

### Per-Method Field Mapping

| Output Field | Source in api_reference.json | Notes |
|-------------|--------|-------|
| `name` | method key | Verbatim |
| `arguments` | Reconstructed from `parameters` array | `"(Type1 param1, Type2 param2)"` or `"()"` |
| `returnType` | `returnType` field | Verbatim. May be empty string. |
| `description` | `description` field | First sentence only, max ~200 chars |
| `callScope` | `callScope` field | `"safe"`, `"caution"`, `"unsafe"`, `"init"`. Omit key entirely if absent/null. |
| `callScopeNote` | `callScopeNote` field | Omit if absent/null/empty. |

### Full Implementation

```python
def run_filter_binary(output_path=None):
    """Filter merged JSON -> minimal JSON for C++ binary embedding."""
    input_path = OUTPUT_DIR / "api_reference.json"
    if not input_path.is_file():
        print("ERROR: No merged JSON found. Run 'merge' first.")
        sys.exit(1)

    if output_path is None:
        output_path = OUTPUT_DIR / "filtered_api.json"
    else:
        output_path = Path(output_path)

    with open(input_path, "r", encoding="utf-8") as f:
        full_api = json.load(f)

    filtered = {"classes": {}}

    for class_name, class_data in full_api.get("classes", {}).items():
        methods_out = {}

        for method_name, method_data in class_data.get("methods", {}).items():
            # Skip disabled methods
            if method_data.get("disabled"):
                continue

            # Reconstruct arguments string
            params = method_data.get("parameters", [])
            if params:
                param_strs = [f"{p.get('type', 'var')} {p['name']}" for p in params]
                arguments = "(" + ", ".join(param_strs) + ")"
            else:
                arguments = "()"

            # Extract brief description (first sentence, max ~200 chars)
            desc = method_data.get("description", "")
            brief = _extract_brief(desc)

            entry = {
                "name": method_name,
                "arguments": arguments,
                "returnType": method_data.get("returnType", ""),
                "description": brief,
            }

            # Only include callScope if present and valid
            call_scope = method_data.get("callScope")
            if call_scope in ("safe", "caution", "unsafe", "init"):
                entry["callScope"] = call_scope

            call_scope_note = method_data.get("callScopeNote")
            if call_scope_note:
                entry["callScopeNote"] = call_scope_note

            methods_out[method_name] = entry

        if methods_out:
            filtered["classes"][class_name] = {"methods": methods_out}

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(filtered, f, indent=2, ensure_ascii=False)

    class_count = len(filtered["classes"])
    method_count = sum(len(c["methods"]) for c in filtered["classes"].values())
    print(f"Filter-binary complete:")
    print(f"  Classes: {class_count}")
    print(f"  Methods: {method_count}")
    print(f"  Output: {output_path}")


def _extract_brief(description: str, max_len: int = 200) -> str:
    """Extract first sentence from a description, capped at max_len."""
    if not description:
        return ""
    # Find first sentence boundary
    for sep in (". ", ".\n"):
        idx = description.find(sep)
        if idx != -1:
            brief = description[:idx + 1]
            if len(brief) <= max_len:
                return brief
            break
    # No sentence boundary or first sentence too long: truncate
    if len(description) <= max_len:
        return description
    # Truncate at last space before max_len
    trunc = description[:max_len]
    last_space = trunc.rfind(" ")
    if last_space > max_len // 2:
        return trunc[:last_space]
    return trunc
```

---

## 2. `ApiValueTreeBuilder.exe` — JUCE Console App

### New Project: `tools/api generator/ApiValueTreeBuilder/`

**Files to create:**
- `ApiValueTreeBuilder.jucer` — Projucer project file
- `Source/Main.cpp` — Tool implementation

### Projucer Setup

Create a JUCE Console Application project:
- **Project name:** `ApiValueTreeBuilder`
- **Project type:** Console Application
- **JUCE modules needed:** `juce_core` only
- **C++ standard:** C++17
- **Build configurations:** Release only (this is a build tool, not shipped)
- **Output:** Copy built executable to `tools/api generator/` alongside existing tools

### Invocation

```
ApiValueTreeBuilder.exe <input.json> <output_dir> <namespace>
```

Example:
```
ApiValueTreeBuilder.exe enrichment\output\filtered_api.json "..\..\hi_scripting\scripting\api" XmlApi
```

### What It Does

1. Parse the filtered JSON using `juce::JSON::parse()`
2. Build a JUCE `ValueTree` with this schema:
   - Root: type `"Api"`
   - Children: one per class, type = class name (e.g., `"Console"`)
   - Grandchildren: one per method, type = `"method"`
   - Method properties: `name`, `arguments`, `returnType`, `description`, plus optional `callScope`, `callScopeNote`
3. Serialize the ValueTree to binary using `ValueTree::writeToStream()`
4. Write two output files matching the JUCE BinaryBuilder format:
   - `<namespace>.h` — Header with `extern const char*` declaration and `const int` size
   - `<namespace>.cpp` — Implementation with `static const unsigned char[]` array

The output contains a single entry: `apivaluetree_dat`. No per-class XML blobs.

### Output File Format

**`XmlApi.h`:**
```cpp
/* (Auto-generated binary data file). */

#ifndef BINARY_XMLAPI_H
#define BINARY_XMLAPI_H

namespace XmlApi
{
    extern const char*  apivaluetree_dat;
    const int           apivaluetree_datSize = <size>;
}

#endif
```

**`XmlApi.cpp`:**
```cpp
/* (Auto-generated binary data file). */

#include "XmlApi.h"

static const unsigned char temp1[] = {<comma-separated byte values>};
const char* XmlApi::apivaluetree_dat = (const char*) temp1;
```

The byte array formatting matches the existing BinaryBuilder convention: 40 values per line, comma-separated, with 2-space indentation on continuation lines.

### Source/Main.cpp

```cpp
#include <JuceHeader.h>

using namespace juce;

ValueTree buildApiValueTree(const var& jsonData)
{
    ValueTree root("Api");

    auto* classesObj = jsonData.getProperty("classes", {}).getDynamicObject();
    if (classesObj == nullptr)
        return root;

    for (auto& classPair : classesObj->getProperties())
    {
        auto className = classPair.name.toString();
        auto* classData = classPair.value.getDynamicObject();
        if (classData == nullptr) continue;

        ValueTree classNode(className);

        auto* methodsObj = classData->getProperty("methods").getDynamicObject();
        if (methodsObj == nullptr)
        {
            root.addChild(classNode, -1, nullptr);
            continue;
        }

        for (auto& methodPair : methodsObj->getProperties())
        {
            auto* methodData = methodPair.value.getDynamicObject();
            if (methodData == nullptr) continue;

            ValueTree methodNode("method");
            methodNode.setProperty("name",        methodData->getProperty("name"),        nullptr);
            methodNode.setProperty("arguments",   methodData->getProperty("arguments"),   nullptr);
            methodNode.setProperty("returnType",  methodData->getProperty("returnType"),  nullptr);
            methodNode.setProperty("description", methodData->getProperty("description"), nullptr);

            // Optional callScope properties — only add if present
            auto callScope = methodData->getProperty("callScope");
            if (!callScope.isVoid() && callScope.toString().isNotEmpty())
                methodNode.setProperty("callScope", callScope, nullptr);

            auto callScopeNote = methodData->getProperty("callScopeNote");
            if (!callScopeNote.isVoid() && callScopeNote.toString().isNotEmpty())
                methodNode.setProperty("callScopeNote", callScopeNote, nullptr);

            classNode.addChild(methodNode, -1, nullptr);
        }

        root.addChild(classNode, -1, nullptr);
    }

    return root;
}

void writeBinaryHeader(const File& outputFile, const String& namespaceName,
                       const String& varName, int dataSize)
{
    String guard = "BINARY_" + namespaceName.toUpperCase() + "_H";

    String h;
    h << "/* (Auto-generated binary data file). */\n\n";
    h << "#ifndef " << guard << "\n";
    h << "#define " << guard << "\n\n";
    h << "namespace " << namespaceName << "\n{\n";
    h << "    extern const char*  " << varName << ";\n";
    h << "    const int           " << varName << "Size = " << dataSize << ";\n";
    h << "}\n\n";
    h << "#endif\n";

    outputFile.replaceWithText(h);
}

void writeBinarySource(const File& outputFile, const String& namespaceName,
                       const String& headerName, const String& varName,
                       const void* data, int dataSize)
{
    String cpp;
    cpp << "/* (Auto-generated binary data file). */\n\n";
    cpp << "#include \"" << headerName << "\"\n\n";
    cpp << "static const unsigned char temp1[] = {";

    auto* bytes = static_cast<const unsigned char*>(data);

    for (int i = 0; i < dataSize; i++)
    {
        if (i > 0) cpp << ",";
        if (i % 40 == 0) cpp << "\n  ";
        cpp << (int)bytes[i];
    }

    cpp << "};\n";
    cpp << "const char* " << namespaceName << "::" << varName
        << " = (const char*) temp1;\n";

    outputFile.replaceWithText(cpp);
}

int main(int argc, char* argv[])
{
    if (argc < 4)
    {
        std::cout << "Usage: ApiValueTreeBuilder <input.json> <output_dir> <namespace>"
                  << std::endl;
        std::cout << "Example: ApiValueTreeBuilder filtered_api.json "
                  << "\"../../hi_scripting/scripting/api\" XmlApi" << std::endl;
        return 1;
    }

    File inputFile(argv[1]);
    File outputDir(argv[2]);
    String namespaceName(argv[3]);

    if (!inputFile.existsAsFile())
    {
        std::cerr << "ERROR: Input file not found: "
                  << inputFile.getFullPathName() << std::endl;
        return 1;
    }

    if (!outputDir.isDirectory())
    {
        std::cerr << "ERROR: Output directory not found: "
                  << outputDir.getFullPathName() << std::endl;
        return 1;
    }

    // Parse JSON
    auto jsonText = inputFile.loadFileAsString();
    auto jsonData = JSON::parse(jsonText);

    if (!jsonData.isObject())
    {
        std::cerr << "ERROR: Failed to parse JSON from "
                  << inputFile.getFullPathName() << std::endl;
        return 1;
    }

    // Build ValueTree
    auto tree = buildApiValueTree(jsonData);

    // Count stats
    int classCount = tree.getNumChildren();
    int methodCount = 0;
    int callScopeCount = 0;

    for (int i = 0; i < classCount; i++)
    {
        auto classNode = tree.getChild(i);
        for (int j = 0; j < classNode.getNumChildren(); j++)
        {
            methodCount++;
            if (classNode.getChild(j).hasProperty("callScope"))
                callScopeCount++;
        }
    }

    // Serialize to binary
    MemoryOutputStream mos;
    tree.writeToStream(mos);

    int dataSize = (int)mos.getDataSize();
    String varName = "apivaluetree_dat";

    // Write output files
    File headerFile = outputDir.getChildFile(namespaceName + ".h");
    File sourceFile = outputDir.getChildFile(namespaceName + ".cpp");

    writeBinaryHeader(headerFile, namespaceName, varName, dataSize);
    writeBinarySource(sourceFile, namespaceName, namespaceName + ".h",
                      varName, mos.getData(), dataSize);

    std::cout << "ApiValueTreeBuilder complete:" << std::endl;
    std::cout << "  Classes: " << classCount << std::endl;
    std::cout << "  Methods: " << methodCount << std::endl;
    std::cout << "  With callScope: " << callScopeCount << std::endl;
    std::cout << "  Binary size: " << dataSize << " bytes" << std::endl;
    std::cout << "  Header: " << headerFile.getFullPathName() << std::endl;
    std::cout << "  Source: " << sourceFile.getFullPathName() << std::endl;

    return 0;
}
```

---

## 3. `batchCreate.bat` Changes

### File: `tools/api generator/batchCreate.bat`

Remove the `ApiExtractor.exe` (line 247) and `BinaryBuilder.exe` (line 251) calls. The bat file now only runs Doxygen + copy/rename.

**Remove these lines (247-251):**

```batch
ApiExtractor.exe xml\selection xml\selection

REM del xml\selection\*.xml /Q

 BinaryBuilder.exe xml\selection "..\..\hi_scripting\scripting\api" XmlApi
```

**Replace with a comment:**

```batch
REM ApiExtractor.exe and BinaryBuilder.exe removed.
REM XmlApi.h/.cpp is now generated by the enrichment pipeline:
REM   python api_enrich.py filter-binary
REM   ApiValueTreeBuilder.exe enrichment\output\filtered_api.json "..\..\hi_scripting\scripting\api" XmlApi
```

The enrichment steps are run manually, not from `batchCreate.bat`.

---

## 4. Verification Checklist

After running the full pipeline:

- [ ] `enrichment/output/filtered_api.json` exists and contains all classes
- [ ] Each method entry has `name`, `arguments`, `returnType`, `description`
- [ ] Enriched methods (Console, GlobalCable, ScriptedViewport) have `callScope` values
- [ ] Non-enriched methods have no `callScope` key (not `null`, completely absent)
- [ ] `XmlApi.h` declares only `apivaluetree_dat` and `apivaluetree_datSize` (no `*_xml` entries)
- [ ] `XmlApi.cpp` contains only one `static const unsigned char temp1[]` array (no temp2-temp92)
- [ ] HISE compiles and `ApiHelpers::getApiTree()` returns a valid ValueTree
- [ ] Console class has `print` with `callScope` = `"caution"` and `startBenchmark` with `callScope` = `"safe"`
- [ ] Methods without enrichment data have no `callScope` property (returns empty `var`)
- [ ] Autocomplete still works (all methods still have `name`, `arguments`, `returnType`, `description`)
- [ ] Binary size is ~400KB (vs old 341KB without callScope, or ~6.8MB with the XML blobs)

### Quick Verification Script (Python)

```python
import json
with open("enrichment/output/filtered_api.json") as f:
    data = json.load(f)

# Check enriched classes have callScope
for cls in ("Console", "GlobalCable", "ScriptedViewport"):
    methods = data["classes"].get(cls, {}).get("methods", {})
    scoped = [m for m, d in methods.items() if "callScope" in d]
    print(f"{cls}: {len(methods)} methods, {len(scoped)} with callScope")

# Check a non-enriched class has no callScope
arr = data["classes"].get("Array", {}).get("methods", {})
scoped_arr = [m for m, d in arr.items() if "callScope" in d]
print(f"Array: {len(arr)} methods, {len(scoped_arr)} with callScope (should be 0)")
```
