# Layer 2: C++ Runtime Lookup API Implementation Guide

Adds the `CallScope` enum, `RealtimeSafetyInfo` struct, `getCallScope()` lookup function, `getRealtimeSafetyReport()` virtual on `CallableObject`, and the `CallScopeWarnings` HiseSettings entry.

**Prerequisite:** Layer 1 complete — `XmlApi.h`/`XmlApi.cpp` contain `callScope` and `callScopeNote` properties on method ValueTree children.

**Build guard:** All new code is `#if USE_BACKEND` only.

**Deliverables:**
1. `StrictnessLevel` enum + `getRealtimeSafetyReport()` virtual on `CallableObject` (`ScriptingBaseObjects.h`)
2. `RealtimeSafetyInfo` struct (with `CallScope`, `Warning`, `Holder`) in `HiseJavascriptEngine.h`
3. `setCurrentRealtimeInfoHolder()` virtual on `OptimizationPass` (`HiseJavascriptEngine.h`)
4. `CallScopeInfo` + `getCallScope()` in `ApiHelpers` (`ScriptingApiObjects.h/.cpp`)
5. `RealtimeSafetyInfo::toString()` + `RealtimeSafetyInfo::check()` implementations (`ScriptingApiObjects.cpp`)
6. `CallScopeWarnings` setting (`HiseSettings.h/.cpp`)

---

## 1. `StrictnessLevel` + `getRealtimeSafetyReport()` on `CallableObject`

### File: `hi_scripting/scripting/api/ScriptingBaseObjects.h`

### Location: Inside `WeakCallbackHolder::CallableObject` (line 220), after `isRealtimeSafe()` (line 229)

`ScriptingBaseObjects.h` is the first scripting header in the include chain (`hi_scripting.h` line 140), so types defined here are visible to everything downstream — `HiseJavascriptEngine.h`, `ScriptingApiObjects.h`, etc.

**Insert after line 229 (`virtual bool isRealtimeSafe() const = 0;`):**

```cpp
        virtual bool isRealtimeSafe() const = 0;

#if USE_BACKEND
		enum class StrictnessLevel { Relaxed, Warn, Error };

		/** Returns a safety report for this callable at the given strictness.
		 *  Relaxed: always returns Result::ok().
		 *  Warn/Error: returns Result::fail(message) if unsafe calls were found.
		 *  Default: returns ok (no analysis data). Override in InlineFunction::Object (Layer 3).
		 */
		virtual juce::Result getRealtimeSafetyReport(StrictnessLevel) const
		{
			return Result::ok();
		}
#endif
```

This is a non-pure virtual with a default no-op. All three existing `CallableObject` implementations (`InlineFunction::Object`, `FunctionObject`, `ScriptBroadcaster`) compile without changes — they inherit the default. Layer 3 adds the meaningful override on `InlineFunction::Object`.

---

## 2. `RealtimeSafetyInfo` in `HiseJavascriptEngine.h`

### File: `hi_scripting/scripting/engine/HiseJavascriptEngine.h`

### Location: Inside `RootObject` class, after `CallStackEntry` (line 538), before `struct Scope;` (line 540)

```cpp
		};  // end of CallStackEntry (line 538)

#if USE_BACKEND

		struct RealtimeSafetyInfo
		{
			enum class CallScope : uint8
			{
				Unknown = 0,   // no enrichment data
				Init,          // onInit only
				Unsafe,        // runtime OK, not audio thread
				Caution,       // audio thread OK with caveats
				Safe           // anywhere, anytime
			};

			using StrictnessLevel = WeakCallbackHolder::CallableObject::StrictnessLevel;

			struct Warning
			{
				String apiCall;                    // "Console.print" or "*.push" (greedy)
				CallScope scope;                   // from the CallScopeInfo lookup
				String note;                       // callScopeNote, e.g. "allocates MemoryOutputStream"
				Array<CallStackEntry> callStack;   // full trace from caller to unsafe API call
			};

			struct Holder
			{
				virtual ~Holder() {}
				virtual RealtimeSafetyInfo* getRealtimeSafetyInfo() = 0;
			};

			Array<Warning> warnings;

			bool isEmpty() const { return warnings.isEmpty(); }

			bool hasUnsafe() const
			{
				for (auto& w : warnings)
					if (w.scope == CallScope::Unsafe || w.scope == CallScope::Init)
						return true;
				return false;
			}

			bool hasCaution() const
			{
				for (auto& w : warnings)
					if (w.scope == CallScope::Caution)
						return true;
				return false;
			}

			/** Returns a formatted report string for the console.
			 *  Filters based on strictness: Warn includes caution+unsafe, Error same.
			 *  Relaxed returns empty (caller should not call this with Relaxed).
			 */
			String toString(StrictnessLevel l) const;

			/** Encapsulates the full enforcement pattern at call sites.
			 *
			 *  1. Checks callable->isRealtimeSafe() — if false, returns true (structural rejection)
			 *  2. Queries StrictnessLevel from settings
			 *  3. Calls callable->getRealtimeSafetyReport(strictness)
			 *  4. If failed: logs to console, returns true if Error strictness
			 *  5. Otherwise returns false
			 */
			static bool check(WeakCallbackHolder::CallableObject* callable,
			                  ScriptingObject* caller,
			                  const String& context);
		};

#endif

		struct Scope;  // line 540 (existing)
```

**Include-order note:** `StrictnessLevel` is defined in `ScriptingBaseObjects.h` (line 140 in module header) which is included before `HiseJavascriptEngine.h` (line 163). The `using` alias makes it available under `RealtimeSafetyInfo::StrictnessLevel` for consistency with callers that work with `RealtimeSafetyInfo` directly.

---

## 3. `setCurrentRealtimeInfoHolder()` on `OptimizationPass`

### File: `hi_scripting/scripting/engine/HiseJavascriptEngine.h`

### Location: Inside `OptimizationPass` struct, after `executePass()` (line 616)

```cpp
		struct OptimizationPass
		{
			// ... existing members (lines 594-616) ...

			OptimizationResult executePass(Statement* rootStatementToOptimize);

#if USE_BACKEND
			virtual void setCurrentRealtimeInfoHolder(RealtimeSafetyInfo::Holder*) {}
#endif
		};
```

Default no-op. Layer 3's `CallScopeAnalyzer` overrides this to capture the current function/callback being analyzed.

---

## 4. `CallScopeInfo` + `getCallScope()` in `ApiHelpers`

### File: `hi_scripting/scripting/api/ScriptingApiObjects.h`

### Location: Inside `ApiHelpers` class, expand the existing `#if USE_BACKEND` block (lines 118-125)

```cpp
#if USE_BACKEND

	static String getValueType(const var& v);
	static ValueTree getApiTree();

	using CallScope = HiseJavascriptEngine::RootObject::RealtimeSafetyInfo::CallScope;

	struct CallScopeInfo
	{
		CallScope scope = CallScope::Unknown;
		String note;
	};

	/** Look up the callScope for a given class + method.
	 *
	 *  Exact mode (className = specific class name):
	 *    Finds the class child in the API ValueTree, finds the method by name,
	 *    reads callScope string and converts to enum.
	 *
	 *  Greedy mode (className = "*"):
	 *    Iterates all classes, collects every callScope for matching methodName.
	 *    - If ANY are Safe: return Safe (don't warn — ambiguous)
	 *    - If ALL are not-safe: return worst-case scope
	 *    - If NO matches: return Unsafe (non-API dynamic dispatch)
	 */
	static CallScopeInfo getCallScope(const String& className, const String& methodName);

#endif
```

### File: `hi_scripting/scripting/api/ScriptingApiObjects.cpp`

### Location: After `getApiTree()` (line 7020), inside the `#if USE_BACKEND` block (before the `ScriptBuffer` constructor at line 7022)

Insert between line 7020 (`}` closing `getApiTree()`) and line 7022 (`ScriptingObjects::ScriptBuffer::ScriptBuffer`):

```cpp
static ApiHelpers::CallScope parseCallScopeString(const String& s)
{
	using CS = ApiHelpers::CallScope;

	if (s == "safe")    return CS::Safe;
	if (s == "caution") return CS::Caution;
	if (s == "unsafe")  return CS::Unsafe;
	if (s == "init")    return CS::Init;
	return CS::Unknown;
}

static int callScopeSeverity(ApiHelpers::CallScope s)
{
	using CS = ApiHelpers::CallScope;
	switch (s)
	{
		case CS::Unsafe:  return 3;
		case CS::Init:    return 2;
		case CS::Caution: return 1;
		case CS::Safe:    return 0;
		case CS::Unknown: return 0;
		default:          return 0;
	}
}

/** Greedy lookup HashMap — built once on first greedy query. */
struct GreedyCallScopeMap
{
	/** For each method name, stores the resolved greedy CallScopeInfo.
	 *
	 *  Resolution rule:
	 *  - Collect all CallScope values for *.methodName across all classes
	 *  - If ANY are Safe: store Safe (ambiguous — don't warn)
	 *  - If ALL are not-safe: store the worst-case scope
	 *  - Severity: Unsafe (3) > Init (2) > Caution (1) > Unknown/Safe (0)
	 */
	HashMap<String, ApiHelpers::CallScopeInfo> map;
	bool built = false;

	void buildFromTree(const ValueTree& apiTree)
	{
		using CS = ApiHelpers::CallScope;

		// Temporary: collect all scopes per method name
		HashMap<String, Array<ApiHelpers::CallScopeInfo>> collector;

		for (int i = 0; i < apiTree.getNumChildren(); i++)
		{
			auto classNode = apiTree.getChild(i);

			for (int j = 0; j < classNode.getNumChildren(); j++)
			{
				auto methodNode = classNode.getChild(j);
				auto methodName = methodNode.getProperty("name").toString();
				auto scopeStr = methodNode.getProperty("callScope").toString();

				if (methodName.isEmpty())
					continue;

				ApiHelpers::CallScopeInfo info;
				info.scope = parseCallScopeString(scopeStr);
				info.note = methodNode.getProperty("callScopeNote").toString();

				if (!collector.contains(methodName))
					collector.set(methodName, {});

				collector.getReference(methodName).add(info);
			}
		}

		// Resolve each method name using the forgiving greedy rule
		HashMap<String, Array<ApiHelpers::CallScopeInfo>>::Iterator it(collector);

		while (it.next())
		{
			auto& methodName = it.getKey();
			auto& infos = it.getValue();

			bool anySafe = false;
			CS worstScope = CS::Unknown;
			String worstNote;

			for (auto& info : infos)
			{
				if (info.scope == CS::Safe)
				{
					anySafe = true;
					break;
				}

				if (callScopeSeverity(info.scope) > callScopeSeverity(worstScope))
				{
					worstScope = info.scope;
					worstNote = info.note;
				}
			}

			ApiHelpers::CallScopeInfo resolved;

			if (anySafe)
			{
				resolved.scope = CS::Safe;
			}
			else
			{
				resolved.scope = worstScope;
				resolved.note = worstNote;
			}

			map.set(methodName, resolved);
		}

		built = true;
	}
};

static GreedyCallScopeMap& getGreedyMap()
{
	static GreedyCallScopeMap instance;

	if (!instance.built)
		instance.buildFromTree(ApiHelpers::getApiTree());

	return instance;
}

ApiHelpers::CallScopeInfo ApiHelpers::getCallScope(const String& className, const String& methodName)
{
	using CS = CallScope;

	if (className == "*")
	{
		// Greedy mode: use pre-built HashMap
		auto& greedyMap = getGreedyMap();

		if (greedyMap.map.contains(methodName))
			return greedyMap.map[methodName];

		// No match at all: non-API dynamic function dispatch
		return { CS::Unsafe, {} };
	}

	// Exact mode: find class, then method
	auto apiTree = getApiTree();
	auto classNode = apiTree.getChildWithName(Identifier(className));

	if (!classNode.isValid())
		return { CS::Unknown, {} };

	for (int i = 0; i < classNode.getNumChildren(); i++)
	{
		auto methodNode = classNode.getChild(i);

		if (methodNode.getProperty("name").toString() == methodName)
		{
			auto scopeStr = methodNode.getProperty("callScope").toString();

			CallScopeInfo info;
			info.scope = parseCallScopeString(scopeStr);
			info.note = methodNode.getProperty("callScopeNote").toString();
			return info;
		}
	}

	return { CS::Unknown, {} };
}
```

---

## 5. `toString()` and `check()` Implementations

### File: `hi_scripting/scripting/api/ScriptingApiObjects.cpp`

### Location: After the `getCallScope()` implementation, still inside the `#if USE_BACKEND` block

### `toString()`

```cpp
String HiseJavascriptEngine::RootObject::RealtimeSafetyInfo::toString(StrictnessLevel l) const
{
	if (l == StrictnessLevel::Relaxed || warnings.isEmpty())
		return {};

	String s;

	for (auto& w : warnings)
	{
		String scopeLabel;

		switch (w.scope)
		{
			case CallScope::Unsafe:  scopeLabel = "unsafe"; break;
			case CallScope::Init:    scopeLabel = "init-only"; break;
			case CallScope::Caution: scopeLabel = "caution"; break;
			default: scopeLabel = "unknown"; break;
		}

		s << "[CallScope] " << w.apiCall << " (" << scopeLabel;

		if (w.note.isNotEmpty())
			s << ": " << w.note;

		s << ")\n";

		for (auto& entry : w.callStack)
		{
			s << "  at ";

			if (entry.functionName.isValid())
				s << entry.functionName;

			if (entry.location.location != nullptr)
			{
				auto lineCol = entry.location.getLineAndColumn();
				auto file = entry.location.getCallbackName(true);

				if (file.isNotEmpty())
					s << " (" << file << ":" << String(lineCol.x) << ")";
			}

			s << "\n";
		}
	}

	return s;
}
```

### `check()`

This is the static helper called at every call site that registers audio-thread callbacks (Layer 3 wires these up).

```cpp
bool HiseJavascriptEngine::RootObject::RealtimeSafetyInfo::check(
	WeakCallbackHolder::CallableObject* callable,
	ScriptingObject* caller,
	const String& context)
{
	if (callable == nullptr)
		return true;

	// Step 1: structural gate — rejects regular function objects and non-RT broadcasters
	if (!callable->isRealtimeSafe())
		return true;

	// Step 2: query StrictnessLevel from settings
	auto* mc = dynamic_cast<Processor*>(caller)->getMainController();
	auto settingValue = GET_HISE_SETTING(mc, HiseSettings::Scripting::CallScopeWarnings).toString();

	StrictnessLevel strictness = StrictnessLevel::Relaxed;
	if (settingValue == "Warn")       strictness = StrictnessLevel::Warn;
	else if (settingValue == "Error") strictness = StrictnessLevel::Error;

	if (strictness == StrictnessLevel::Relaxed)
		return false;

	// Step 3: get the content-aware safety report
	auto result = callable->getRealtimeSafetyReport(strictness);

	if (result.failed())
	{
		debugToConsole(dynamic_cast<Processor*>(caller),
		               "[" + context + "] " + result.getErrorMessage());

		return strictness == StrictnessLevel::Error;
	}

	return false;
}
```

**Runtime behavior at Layer 2 (before Layer 3 wires up overrides):**
- `getRealtimeSafetyReport()` always returns `Result::ok()` (default implementation)
- `check()` effectively just does the `isRealtimeSafe()` structural gate
- Layer 3 adds the override on `InlineFunction::Object` that formats `RealtimeSafetyInfo` into `Result::fail(message)`

---

## 6. `CallScopeWarnings` Setting

### File: `hi_core/hi_core/HiseSettings.h`

### Location: In `namespace Scripting` (line 151), after `EnableOptimizations` (line 157)

```cpp
DECLARE_ID(EnableOptimizations);
DECLARE_ID(CallScopeWarnings);       // NEW
DECLARE_ID(EnableDebugMode);
```

### File: `hi_core/hi_core/HiseSettings.cpp`

Five insertion points:

### 6a. `getAllIds()` — line 166

After `ids.add(EnableOptimizations);`:

```cpp
	ids.add(EnableOptimizations);
	ids.add(CallScopeWarnings);          // NEW
```

### 6b. Description registration — after line 602

After the `EnableOptimizations` description block (which ends with `P_();` at line 602):

```cpp
		P(HiseSettings::Scripting::CallScopeWarnings);
		D("Controls compile-time analysis of audio-thread safety for inline functions and MIDI callbacks.");
		D("- **Relaxed**: No analysis (current behavior). No overhead.");
		D("- **Warn**: Analyzes API calls inside inline functions and MIDI callbacks. Logs warnings to the console when potentially unsafe calls are detected.");
		D("- **Error**: Same analysis as Warn, but also prevents compilation when unsafe calls are found in audio-thread contexts.");
		D("> This only affects the HISE IDE. Exported plugins have zero overhead regardless of this setting.");
		P_();
```

### 6c. Options list — after line 1001

The boolean settings block returns `{ "Yes", "No" }` at line 1001. Insert after it, before the `VisualStudioVersion` check at line 1003:

```cpp
	    return { "Yes", "No" };

	if (id == Scripting::CallScopeWarnings)           // NEW
		return { "Relaxed", "Warn", "Error" };        // NEW

	if (id == Compiler::VisualStudioVersion)
```

### 6d. Default value — after line 1240

After `EnableOptimizations` default:

```cpp
	else if (id == Scripting::EnableOptimizations)	return "No";
	else if (id == Scripting::CallScopeWarnings)	return "Relaxed";    // NEW
```

### 6e. On-change handler — after line 1422

After the `EnableOptimizations` handler:

```cpp
	else if (id == Scripting::EnableOptimizations)
		mc->compileAllScripts();
	else if (id == Scripting::CallScopeWarnings)    // NEW
		mc->compileAllScripts();                    // NEW
```

---

## Verification Checklist

After implementing Layer 2:

- [ ] `CallableObject::StrictnessLevel` enum compiles in `ScriptingBaseObjects.h`
- [ ] `CallableObject::getRealtimeSafetyReport()` default returns `Result::ok()`
- [ ] `RealtimeSafetyInfo` compiles with all nested types (`CallScope`, `Warning`, `Holder`)
- [ ] `RealtimeSafetyInfo::StrictnessLevel` is a `using` alias to `CallableObject::StrictnessLevel`
- [ ] `OptimizationPass::setCurrentRealtimeInfoHolder()` has a default no-op
- [ ] `ApiHelpers::CallScope` is a `using` alias to `RealtimeSafetyInfo::CallScope`
- [ ] `ApiHelpers::getCallScope("Console", "print")` returns `{Caution, "Allocates..."}`
- [ ] `ApiHelpers::getCallScope("Console", "startBenchmark")` returns `{Safe, ""}`
- [ ] `ApiHelpers::getCallScope("Array", "push")` returns `{Unknown, ""}` (no enrichment data yet)
- [ ] `ApiHelpers::getCallScope("*", "print")` returns greedy resolution for "print"
- [ ] `ApiHelpers::getCallScope("*", "nonexistentMethod")` returns `{Unsafe, ""}`
- [ ] `CallScopeWarnings` setting appears in HISE Settings > Scripting with options Relaxed / Warn / Error
- [ ] Changing `CallScopeWarnings` triggers recompilation of all scripts
- [ ] Default value is `Relaxed`
- [ ] `RealtimeSafetyInfo::check()` returns `true` for non-inline functions (structural rejection)
- [ ] `RealtimeSafetyInfo::check()` returns `false` for inline functions when `Relaxed`
- [ ] All new code is inside `#if USE_BACKEND` guards
- [ ] HISE compiles in all configurations (Debug, Release, CI, Minimal Build)

### Unit Test Sketch

```cpp
// Exact lookups
auto printInfo = ApiHelpers::getCallScope("Console", "print");
jassert(printInfo.scope == ApiHelpers::CallScope::Caution);
jassert(printInfo.note.contains("Allocates"));

auto benchInfo = ApiHelpers::getCallScope("Console", "startBenchmark");
jassert(benchInfo.scope == ApiHelpers::CallScope::Safe);

// Unknown class/method
auto unknownInfo = ApiHelpers::getCallScope("Console", "nonExistentMethod");
jassert(unknownInfo.scope == ApiHelpers::CallScope::Unknown);

// Greedy: no match at all
auto noMatch = ApiHelpers::getCallScope("*", "totallyFakeMethod123");
jassert(noMatch.scope == ApiHelpers::CallScope::Unsafe);

// check() structural gate
// FunctionObject::isRealtimeSafe() returns false → check() returns true
// InlineFunction::Object::isRealtimeSafe() returns true → proceeds to report
```

---

## Files Changed Summary

| File | Change |
|------|--------|
| `hi_scripting/scripting/api/ScriptingBaseObjects.h` | `StrictnessLevel` enum + `getRealtimeSafetyReport()` virtual on `CallableObject` |
| `hi_scripting/scripting/engine/HiseJavascriptEngine.h` | `RealtimeSafetyInfo` struct (with `CallScope`, `Warning`, `Holder`, `using StrictnessLevel`) inside `RootObject`; `setCurrentRealtimeInfoHolder()` on `OptimizationPass` |
| `hi_scripting/scripting/api/ScriptingApiObjects.h` | `using CallScope`, `CallScopeInfo`, `getCallScope()` declaration inside `ApiHelpers` |
| `hi_scripting/scripting/api/ScriptingApiObjects.cpp` | `getCallScope()` (exact + greedy), `GreedyCallScopeMap`, `toString()`, `check()` |
| `hi_core/hi_core/HiseSettings.h` | `DECLARE_ID(CallScopeWarnings)` in `Scripting` namespace |
| `hi_core/hi_core/HiseSettings.cpp` | `getAllIds()`, description, options `{"Relaxed","Warn","Error"}`, default `"Relaxed"`, on-change `compileAllScripts()` |

---

## Dependencies on Layer 3

Layer 2 declares but does not fully use:

- `RealtimeSafetyInfo::Holder` — not inherited by anyone yet (Layer 3 adds it to `InlineFunction::Object` and `Callback`)
- `setCurrentRealtimeInfoHolder()` — no pass overrides it yet (Layer 3 adds `CallScopeAnalyzer`)
- `getRealtimeSafetyReport()` — default returns `Result::ok()` (Layer 3 adds the override on `InlineFunction::Object` that formats analysis results)
- `check()` — effectively just the `isRealtimeSafe()` structural gate until Layer 3 provides real analysis data

---

## Type Alias Chain

The `StrictnessLevel` and `CallScope` enums flow through the codebase via `using` aliases:

```
CallableObject::StrictnessLevel          (defined in ScriptingBaseObjects.h)
  └─ RealtimeSafetyInfo::StrictnessLevel (using alias in HiseJavascriptEngine.h)

RealtimeSafetyInfo::CallScope            (defined in HiseJavascriptEngine.h)
  └─ ApiHelpers::CallScope              (using alias in ScriptingApiObjects.h)
```

This follows the existing codebase pattern and keeps each type accessible at the abstraction level where it's most used, without creating include-order issues.
