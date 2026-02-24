# Layer 3: Analyzer & Enforcement Implementation Guide

Adds the compile-time `CallScopeAnalyzer` pass that populates `RealtimeSafetyInfo` on inline functions and callbacks, the `getRealtimeSafetyReport()` override on `InlineFunction::Object`, and upgrades all call sites to use the `check()` helper.

**Prerequisites:** Layer 1 (data pipeline) and Layer 2 (lookup API, types, settings) are complete.

**Build guard:** All new code is `#if USE_BACKEND` only.

**Deliverables:**
1. `Holder` inheritance on `InlineFunction::Object` and `Callback`
2. `getRealtimeSafetyReport()` override on `InlineFunction::Object`
3. `CallScopeAnalyzer` optimization pass
4. Wiring: `setCurrentRealtimeInfoHolder()` in `optimiseFunction()` and callback loop
5. Post-compilation MIDI callback check
6. Call site upgrades (5 existing + 6 new)

---

## 1. `Holder` Inheritance

### 1a. `InlineFunction::Object`

**File:** `hi_scripting/scripting/engine/JavascriptEngineCustom.cpp`

**Current declaration (lines 409-413):**

```cpp
struct Object : public DynamicObject,
                public DebugableObjectBase,
                public WeakCallbackHolder::CallableObject,
                public CyclicReferenceCheckBase,
                public LocalScopeCreator
{
```

**Add `Holder` base class:**

```cpp
struct Object : public DynamicObject,
                public DebugableObjectBase,
                public WeakCallbackHolder::CallableObject,
                public CyclicReferenceCheckBase,
                public LocalScopeCreator
#if USE_BACKEND
                , public RealtimeSafetyInfo::Holder
#endif
{
```

**Add member and overrides** (after `isRealtimeSafe()` at line 600):

```cpp
        bool isRealtimeSafe() const override { return true; }  // existing, line 600

#if USE_BACKEND
        RealtimeSafetyInfo realtimeSafetyInfoData;

        RealtimeSafetyInfo* getRealtimeSafetyInfo() override
        {
            return &realtimeSafetyInfoData;
        }

        juce::Result getRealtimeSafetyReport(StrictnessLevel strictness) const override
        {
            if (strictness == StrictnessLevel::Relaxed)
                return Result::ok();

            if (realtimeSafetyInfoData.isEmpty())
                return Result::ok();

            auto msg = realtimeSafetyInfoData.toString(strictness);

            if (msg.isEmpty())
                return Result::ok();

            return Result::fail(msg);
        }
#endif
```

### 1b. `Callback`

**File:** `hi_scripting/scripting/engine/HiseJavascriptEngine.h`

**Current declaration (lines 744-746):**

```cpp
        class Callback:  public DynamicObject,
                         public DebugableObject,
                         public LocalScopeCreator
        {
```

**Add `Holder` base class:**

```cpp
        class Callback:  public DynamicObject,
                         public DebugableObject,
                         public LocalScopeCreator
#if USE_BACKEND
                         , public RealtimeSafetyInfo::Holder
#endif
        {
```

**Add member and override** (after `cleanLocalProperties()` at line 786, before `Identifier parameters[4]` at line 788):

```cpp
            void cleanLocalProperties();  // existing, line 786

#if USE_BACKEND
            RealtimeSafetyInfo realtimeSafetyInfoData;
            RealtimeSafetyInfo* getRealtimeSafetyInfo() override { return &realtimeSafetyInfoData; }
#endif

            Identifier parameters[4];  // existing, line 788
```

`Callback` does not override `getRealtimeSafetyReport()` — it's not a `CallableObject`. The post-compilation check (Section 5) accesses `realtimeSafetyInfoData` directly via the `Holder` interface.

---

## 2. `CallScopeAnalyzer` Pass

### File: `hi_scripting/scripting/engine/JavascriptEngineCustom.cpp`

### Location: After `FunctionInliner` (ends at line ~1186), before `registerOptimisationPasses()` (line 1188)

### AST Node Types

The analyzer visits four AST node types via `dynamic_cast`:

| AST Type | File | Key Members | Resolution |
|----------|------|-------------|------------|
| `ApiCall` | `JavascriptEngineCustom.cpp:115` | `apiClass` (ApiClass*), `functionName` (String, set under `ENABLE_SCRIPTING_BREAKPOINTS`) | Exact: `apiClass->getObjectName()` + `functionName` |
| `ConstObjectApiCall` | `JavascriptEngineCustom.cpp:278` | `objectPointer` (var*), `functionName` (Identifier) | Exact: resolve `objectPointer` → `ConstScriptingObject::getObjectName()` + `functionName` |
| `FunctionCall` | `JavascriptEngineExpressions.cpp:444` | `object` (ExpPtr) | Check if `object` is a `DotOperator`: greedy lookup on `dot->child` |
| `InlineFunction::FunctionCall` | `JavascriptEngineCustom.cpp:744` | `f` (Object* — the callee) | Inherit callee's warnings (already analyzed due to definition order) |

**`ApiCall::functionName`** (line 271) is a `String` populated at parse time in `JavascriptEngineParser.cpp:2040` under `#if ENABLE_SCRIPTING_BREAKPOINTS`. This is always `1` in HISE builds (`AppConfig.h:137`). No new field or parser changes needed.

### Complete Implementation

```cpp
#if USE_BACKEND

struct CallScopeAnalyzer : public HiseJavascriptEngine::RootObject::OptimizationPass
{
    using RealtimeSafetyInfo = HiseJavascriptEngine::RootObject::RealtimeSafetyInfo;
    using CallStackEntry = HiseJavascriptEngine::RootObject::CallStackEntry;

    String getPassName() const override { return "CallScope Analyzer"; }

    void setCurrentRealtimeInfoHolder(RealtimeSafetyInfo::Holder* h) override
    {
        currentHolder = h;
    }

    Statement* getOptimizedStatement(Statement* parent, Statement* statementToOptimize) override
    {
        if (currentHolder == nullptr)
            return statementToOptimize;

        auto* info = currentHolder->getRealtimeSafetyInfo();
        if (info == nullptr)
            return statementToOptimize;

        // --- ApiCall: global API class method (Console.print, Engine.getSampleRate, etc.) ---
        if (auto* apiCall = dynamic_cast<ApiCall*>(statementToOptimize))
        {
            auto className = apiCall->apiClass->getObjectName().toString();
            auto methodName = apiCall->functionName;

            if (methodName.isNotEmpty())
            {
                auto scopeInfo = ApiHelpers::getCallScope(className, methodName);
                addWarningIfNeeded(info, className + "." + methodName, scopeInfo,
                                   statementToOptimize->location);
            }
        }
        // --- ConstObjectApiCall: method on a const-declared scripting object ---
        else if (auto* constCall = dynamic_cast<ConstObjectApiCall*>(statementToOptimize))
        {
            auto methodName = constCall->functionName.toString();
            String className;

            if (constCall->objectPointer != nullptr)
            {
                if (auto* obj = dynamic_cast<ConstScriptingObject*>(constCall->objectPointer->getObject()))
                    className = obj->getObjectName().toString();
            }

            if (className.isNotEmpty() && methodName.isNotEmpty())
            {
                auto scopeInfo = ApiHelpers::getCallScope(className, methodName);
                addWarningIfNeeded(info, className + "." + methodName, scopeInfo,
                                   statementToOptimize->location);
            }
            else if (methodName.isNotEmpty())
            {
                // Object not resolved — fall back to greedy
                auto scopeInfo = ApiHelpers::getCallScope("*", methodName);
                addWarningIfNeeded(info, "*." + methodName, scopeInfo,
                                   statementToOptimize->location);
            }
        }
        // --- InlineFunction::FunctionCall: calling another inline function ---
        else if (auto* ilCall = dynamic_cast<InlineFunction::FunctionCall*>(statementToOptimize))
        {
            inheritCalleeWarnings(info, ilCall);
        }
        // --- FunctionCall: generic dot-operator or bare function call ---
        else if (auto* funcCall = dynamic_cast<FunctionCall*>(statementToOptimize))
        {
            if (auto* dot = dynamic_cast<DotOperator*>(funcCall->object.get()))
            {
                auto methodName = dot->child.toString();
                auto scopeInfo = ApiHelpers::getCallScope("*", methodName);
                addWarningIfNeeded(info, "*." + methodName, scopeInfo,
                                   statementToOptimize->location);
            }
            // Bare FunctionCall without DotOperator (var holding a function) is skipped.
            // These are caught at registration time via isRealtimeSafe().
        }

        // Never replace statements — this is a read-only analysis pass
        return statementToOptimize;
    }

private:

    void addWarningIfNeeded(RealtimeSafetyInfo* info, const String& apiCall,
                            const ApiHelpers::CallScopeInfo& scopeInfo,
                            const CodeLocation& location)
    {
        using CS = ApiHelpers::CallScope;

        if (scopeInfo.scope == CS::Safe || scopeInfo.scope == CS::Unknown)
            return;

        // Unsafe, Init, or Caution — add warning
        RealtimeSafetyInfo::Warning w;
        w.apiCall = apiCall;
        w.scope = scopeInfo.scope;
        w.note = scopeInfo.note;

        CallStackEntry entry;
        entry.location = location;
        w.callStack.add(entry);

        info->warnings.add(w);
    }

    void inheritCalleeWarnings(RealtimeSafetyInfo* info,
                               InlineFunction::FunctionCall* ilCall)
    {
        if (ilCall->f == nullptr)
            return;

        auto* calleeInfo = ilCall->f->getRealtimeSafetyInfo();
        if (calleeInfo == nullptr || calleeInfo->isEmpty())
            return;

        for (auto& w : calleeInfo->warnings)
        {
            RealtimeSafetyInfo::Warning inherited;
            inherited.apiCall = w.apiCall;
            inherited.scope = w.scope;
            inherited.note = w.note;

            // Prepend the call site (where the inline function is called from)
            CallStackEntry callerEntry;
            callerEntry.location = ilCall->location;
            callerEntry.functionName = ilCall->f->name;

            inherited.callStack.add(callerEntry);
            inherited.callStack.addArray(w.callStack);

            info->warnings.add(inherited);
        }
    }

    RealtimeSafetyInfo::Holder* currentHolder = nullptr;
};

#endif // USE_BACKEND
```

### Registration in `registerOptimisationPasses()`

**File:** `JavascriptEngineCustom.cpp`, line 1188

**Current code:**

```cpp
void HiseJavascriptEngine::RootObject::HiseSpecialData::registerOptimisationPasses()
{
    bool shouldOptimize = false;

#if USE_BACKEND
    auto enable = GET_HISE_SETTING(processor->mainController->getMainSynthChain(), HiseSettings::Scripting::EnableOptimizations).toString();
    shouldOptimize = enable == "1";
    optimizations.add(new LocationInjector());
#endif

    if (shouldOptimize)
    {
        optimizations.add(new ConstantFolding());
        optimizations.add(new BlockRemover());
        optimizations.add(new FunctionInliner());
    }
}
```

**Updated:**

```cpp
void HiseJavascriptEngine::RootObject::HiseSpecialData::registerOptimisationPasses()
{
    bool shouldOptimize = false;

#if USE_BACKEND
    auto enable = GET_HISE_SETTING(processor->mainController->getMainSynthChain(), HiseSettings::Scripting::EnableOptimizations).toString();
    shouldOptimize = enable == "1";
    optimizations.add(new LocationInjector());

    auto strictness = GET_HISE_SETTING(processor->mainController->getMainSynthChain(), HiseSettings::Scripting::CallScopeWarnings).toString();
    if (strictness != "Relaxed")
        optimizations.add(new CallScopeAnalyzer());
#endif

    if (shouldOptimize)
    {
        optimizations.add(new ConstantFolding());
        optimizations.add(new BlockRemover());
        optimizations.add(new FunctionInliner());
    }
}
```

The analyzer is only added when `CallScopeWarnings` is `Warn` or `Error`. When `Relaxed`, zero overhead.

---

## 3. Wiring: `setCurrentRealtimeInfoHolder()`

### File: `hi_scripting/scripting/engine/JavascriptEngineAdditionalMethods.cpp`

### 3a. In `optimiseFunction()` (line 168)

**Current code:**

```cpp
bool HiseJavascriptEngine::RootObject::JavascriptNamespace::optimiseFunction(
    OptimizationPass::OptimizationResult& r, var function, OptimizationPass* p)
{
    if (auto fo = dynamic_cast<InlineFunction::Object*>(function.getObject()))
    {
        if (fo->body != nullptr)
        {
            auto tr = p->executePass(fo->body);
            r.numOptimizedStatements += tr.numOptimizedStatements;
            return true;
        }
    }
    else if (auto fo = dynamic_cast<FunctionObject*>(function.getObject()))
    {
        auto tr = p->executePass(fo->body);
        r.numOptimizedStatements += tr.numOptimizedStatements;
        return true;
    }

    return false;
}
```

**Updated:**

```cpp
bool HiseJavascriptEngine::RootObject::JavascriptNamespace::optimiseFunction(
    OptimizationPass::OptimizationResult& r, var function, OptimizationPass* p)
{
    if (auto fo = dynamic_cast<InlineFunction::Object*>(function.getObject()))
    {
        if (fo->body != nullptr)
        {
#if USE_BACKEND
            p->setCurrentRealtimeInfoHolder(fo);
#endif
            auto tr = p->executePass(fo->body);
#if USE_BACKEND
            p->setCurrentRealtimeInfoHolder(nullptr);
#endif
            r.numOptimizedStatements += tr.numOptimizedStatements;
            return true;
        }
    }
    else if (auto fo = dynamic_cast<FunctionObject*>(function.getObject()))
    {
        auto tr = p->executePass(fo->body);
        r.numOptimizedStatements += tr.numOptimizedStatements;
        return true;
    }

    return false;
}
```

`FunctionObject` is NOT a `Holder` — no `setCurrentRealtimeInfoHolder()` call. Regular functions are rejected at registration time via `isRealtimeSafe() == false`.

### 3b. In the callback loop (line 241)

**Current code:**

```cpp
    for (auto c : callbackNEW)
    {
        if (c->statements != nullptr)
        {
            auto tr = p->executePass(c->statements);
            r.numOptimizedStatements += tr.numOptimizedStatements;
        }
    }
```

**Updated:**

```cpp
    for (auto c : callbackNEW)
    {
        if (c->statements != nullptr)
        {
#if USE_BACKEND
            p->setCurrentRealtimeInfoHolder(c);
#endif
            auto tr = p->executePass(c->statements);
#if USE_BACKEND
            p->setCurrentRealtimeInfoHolder(nullptr);
#endif
            r.numOptimizedStatements += tr.numOptimizedStatements;
        }
    }
```

### Processing Order Guarantee

`runOptimisation()` (line 218) processes in this order:

1. **Inline functions** (line 194 via `JavascriptNamespace::runOptimisation`): definition order
2. **Const objects** (line 199): optimizable functions from const API objects
3. **API classes** (line 222): optimizable functions from global API classes
4. **Root properties** (line 230): function objects on the root
5. **Sub-namespaces** (line 235): recursively
6. **Callbacks** (line 241): registration order

Since HISEScript does not allow forward references to inline functions, callees are always analyzed before callers. Step 6 runs after all inline functions, so `inheritCalleeWarnings()` always has complete data.

---

## 4. Post-Compilation MIDI Callback Check

### File: `hi_scripting/scripting/engine/JavascriptEngineParser.cpp`

### Location: After the optimization report (line 2912), before the closing `}` of `execute()` (line 2914)

This check reports warnings for audio-thread MIDI callbacks (`onNoteOn`, `onNoteOff`, `onController`, `onTimer`) after all optimization passes have run. It uses the `Holder` interface directly — not `getRealtimeSafetyReport()` — because `Callback` is not a `CallableObject`.

```cpp
        hiseSpecialData.processor->setOptimisationReport(s);
    }

#if USE_BACKEND
    // Post-compilation: check MIDI callback bodies for callScope warnings
    {
        auto strictnessStr = GET_HISE_SETTING(hiseSpecialData.processor->getMainController_(),
                                               HiseSettings::Scripting::CallScopeWarnings).toString();

        using SL = RealtimeSafetyInfo::StrictnessLevel;
        SL strictness = SL::Relaxed;
        if (strictnessStr == "Warn")       strictness = SL::Warn;
        else if (strictnessStr == "Error") strictness = SL::Error;

        if (strictness != SL::Relaxed)
        {
            static const Identifier audioCallbacks[] = {
                Identifier("onNoteOn"),
                Identifier("onNoteOff"),
                Identifier("onController"),
                Identifier("onTimer")
            };

            for (auto c : hiseSpecialData.callbackNEW)
            {
                auto* info = c->getRealtimeSafetyInfo();
                if (info == nullptr || info->isEmpty())
                    continue;

                auto cbName = c->getName();
                bool isAudioThread = false;

                for (auto& id : audioCallbacks)
                {
                    if (cbName == id)
                    {
                        isAudioThread = true;
                        break;
                    }
                }

                if (!isAudioThread)
                    continue;

                auto report = info->toString(strictness);
                if (report.isNotEmpty())
                {
                    debugToConsole(hiseSpecialData.processor,
                        "[" + cbName.toString() + "] " + report);
                }

                if (strictness == SL::Error && info->hasUnsafe())
                {
                    auto& firstWarning = info->warnings.getFirst();
                    CodeLocation errorLoc;
                    if (!firstWarning.callStack.isEmpty())
                        errorLoc = firstWarning.callStack.getFirst().location;

                    errorLoc.throwError("Unsafe API call in audio-thread callback: "
                                        + firstWarning.apiCall);
                }
            }
        }
    }
#endif

}  // end of execute()
```

---

## 5. Call Site Upgrades

### Pattern

Every call site that registers a callback for potential audio-thread execution uses `RealtimeSafetyInfo::check()`:

```cpp
if (RealtimeSafetyInfo::check(co, this, "ClassName.methodName"))
    reportScriptError("unsafe callback for audio-thread execution");
```

`check()` handles both structural rejection (`isRealtimeSafe() == false`) and content-aware analysis (callScope warnings from the analyzer). It returns `true` when the call site should report an error.

For sync/async call sites, `check()` is only called in the sync path. Async callbacks run on the scripting thread and don't need callScope analysis.

### 5a. Existing Call Sites (Upgrade from `isRealtimeSafe()` to `check()`)

#### GlobalCable::Callback constructor

**File:** `hi_scripting/scripting/api/ScriptingApiObjects.cpp`, line 9148

**Current code (lines 9157-9183):**

```cpp
    auto ilf = dynamic_cast<WeakCallbackHolder::CallableObject*>(f.getObject());

    if (ilf != nullptr && (!synchronous || ilf->isRealtimeSafe()))
    {
        // ... connect callback ...
    }
    else
    {
        stop();  // Silent failure — no error, no warning
    }
```

**Issue:** When a non-inline function is passed with `synchronous=true`, the `else` branch silently calls `stop()`. The callback never fires but no error is reported. This is the high-severity silent-fail bug from `issues.md`.

**Updated (restructured to fix silent failure):**

```cpp
    auto ilf = dynamic_cast<WeakCallbackHolder::CallableObject*>(f.getObject());

    if (ilf == nullptr)
    {
        stop();
        return;
    }

    if (synchronous)
    {
#if USE_BACKEND
        if (RealtimeSafetyInfo::check(ilf, &p, "GlobalCable.registerCallback"))
        {
            p.reportScriptError("Callback is not safe for synchronous audio-thread execution");
            stop();
            return;
        }
#else
        if (!ilf->isRealtimeSafe())
        {
            stop();
            return;
        }
#endif
    }

    if (auto dobj = dynamic_cast<DebugableObjectBase*>(ilf))
    {
        id << dobj->getDebugName();
        funcLocation = dobj->getLocation();
    }

    callback.incRefCount();
    callback.setHighPriority();

    if (auto c = getCableFromVar(parent.cable))
    {
        c->addTarget(this);
    }

    if (!synchronous)
        start();
    else
        stop();
```

This fixes the silent-fail bug: non-inline functions with `synchronous=true` now get a `reportScriptError()` in backend builds and a clean early return in frontend builds (instead of silently registering a dead callback).

#### ScriptedMidiPlayer::setRecordEventCallback

**File:** `hi_scripting/scripting/api/ScriptingApiObjects.cpp`, line 6460

**Current code:**

```cpp
void ScriptingObjects::ScriptedMidiPlayer::setRecordEventCallback(var recordEventCallback)
{
    if (auto co = dynamic_cast<WeakCallbackHolder::CallableObject*>(recordEventCallback.getObject()))
    {
        if (!co->isRealtimeSafe())
            reportScriptError("This callable object is not realtime safe!");

        recordEventProcessor = nullptr;
        recordEventProcessor = new ScriptEventRecordProcessor(*this, recordEventCallback);
    }
    else
    {
        reportScriptError("You need to pass in an inline function");
    }
}
```

**Updated:**

```cpp
void ScriptingObjects::ScriptedMidiPlayer::setRecordEventCallback(var recordEventCallback)
{
    if (auto co = dynamic_cast<WeakCallbackHolder::CallableObject*>(recordEventCallback.getObject()))
    {
        if (RealtimeSafetyInfo::check(co, this, "MidiPlayer.setRecordEventCallback"))
            reportScriptError("This callable object is not safe for audio-thread execution");

        recordEventProcessor = nullptr;
        recordEventProcessor = new ScriptEventRecordProcessor(*this, recordEventCallback);
    }
    else
    {
        reportScriptError("You need to pass in an inline function");
    }
}
```

`check()` replaces `!co->isRealtimeSafe()`. It handles structural rejection (returns `true` for non-inline) plus content-aware analysis (returns `true` at Error strictness with warnings).

#### ComplexGroupManager::registerGroupStartCallback

**File:** `hi_scripting/scripting/api/ScriptingApiObjects.cpp`, line 10687

**Current code:**

```cpp
void ScriptingObjects::ScriptingComplexGroupManager::registerGroupStartCallback(var layerIdOrIndex, var callback)
{
    auto idx = getLayerIndexInternal(layerIdOrIndex);

    if(auto obj = dynamic_cast<WeakCallbackHolder::CallableObject*>(callback.getObject()))
    {
        if(!obj->isRealtimeSafe())
        {
            reportScriptError("This function only works with callable objects that are realtime safe");
        }

        auto nc = new GroupCallback(*this, callback);
        getManager()->setVoiceStartCallback(idx, nc);
        groupCallbacks.add(nc);
    }
}
```

**Updated:**

```cpp
void ScriptingObjects::ScriptingComplexGroupManager::registerGroupStartCallback(var layerIdOrIndex, var callback)
{
    auto idx = getLayerIndexInternal(layerIdOrIndex);

    if(auto obj = dynamic_cast<WeakCallbackHolder::CallableObject*>(callback.getObject()))
    {
        if(RealtimeSafetyInfo::check(obj, this, "ComplexGroupManager.registerGroupStartCallback"))
            reportScriptError("This function only works with callable objects that are safe for audio-thread execution");

        auto nc = new GroupCallback(*this, callback);
        getManager()->setVoiceStartCallback(idx, nc);
        groupCallbacks.add(nc);
    }
}
```

#### ScriptBroadcaster::addListener

**File:** `hi_scripting/scripting/api/ScriptBroadcaster.cpp`, line 3427

**Current code:**

```cpp
bool ScriptBroadcaster::addListener(var object, var metadata, var function)
{
    if(isRealtimeSafe())
    {
        if(auto c = dynamic_cast<WeakCallbackHolder::CallableObject*>(function.getObject()))
        {
            if(!c->isRealtimeSafe())
                reportScriptError("You need to use inline functions in order to ensure realtime safe execution");
        }
    }
    // ... rest of method
}
```

**Updated:**

```cpp
bool ScriptBroadcaster::addListener(var object, var metadata, var function)
{
    if(isRealtimeSafe())
    {
        if(auto c = dynamic_cast<WeakCallbackHolder::CallableObject*>(function.getObject()))
        {
            if(RealtimeSafetyInfo::check(c, this, "Broadcaster.addListener"))
                reportScriptError("Listener callback is not safe for audio-thread execution");
        }
    }
    // ... rest of method unchanged
}
```

#### ScriptBroadcaster::sendMessage — Keep as-is

`sendMessage` checks the broadcaster's own `isRealtimeSafe()` flag at runtime dispatch, not a callback at registration time. The `check()` helper is for registration-time analysis. No change needed.

### 5b. New Call Sites (Add `check()`)

These call sites currently have NO `isRealtimeSafe()` check. Adding `check()` fixes the missing-validation issues from `issues.md`.

#### TransportHandler::Callback constructor

**File:** `hi_scripting/scripting/api/ScriptingApi.cpp`, line 8238

**Current code (lines 8247-8258):**

```cpp
    if (synchronous)
    {
        auto fObj = dynamic_cast<HiseJavascriptEngine::RootObject::InlineFunction::Object*>(f.getObject());

        if (fObj == nullptr)
            throw String("Must use inline functions for synchronous callback");

        if (fObj->parameterNames.size() != numArgs)
        {
            throw String("Parameter amount mismatch for callback. Expected " + String(numArgs));
        }
    }
```

**Updated (add `check()` after validation):**

```cpp
    if (synchronous)
    {
        auto fObj = dynamic_cast<HiseJavascriptEngine::RootObject::InlineFunction::Object*>(f.getObject());

        if (fObj == nullptr)
            throw String("Must use inline functions for synchronous callback");

        if (fObj->parameterNames.size() != numArgs)
        {
            throw String("Parameter amount mismatch for callback. Expected " + String(numArgs));
        }

#if USE_BACKEND
        if (auto co = dynamic_cast<WeakCallbackHolder::CallableObject*>(f.getObject()))
        {
            if (RealtimeSafetyInfo::check(co, p, "TransportHandler." + name))
                throw String("Callback contains unsafe API calls for audio-thread execution");
        }
#endif
    }
```

This constructor throws `String` exceptions (existing pattern). Covers all five TransportHandler setters (`setOnTempoChange`, `setOnTransportChange`, `setOnSignatureChange`, `setOnBeatChange`, `setOnGridChange`) since they all create `Callback` through this single constructor.

#### ScriptedMidiPlayer::setPlaybackCallback

**File:** `hi_scripting/scripting/api/ScriptingApiObjects.cpp`, line 6807

**Current code:**

```cpp
void ScriptingObjects::ScriptedMidiPlayer::setPlaybackCallback(var newPlaybackCallback, var synchronous)
{
    playbackUpdater = nullptr;
    bool isSync = ApiHelpers::isSynchronous(synchronous);
    if (HiseJavascriptEngine::isJavascriptFunction(newPlaybackCallback))
    {
        playbackUpdater = new PlaybackUpdater(*this, newPlaybackCallback, isSync);
    }
}
```

**Updated:**

```cpp
void ScriptingObjects::ScriptedMidiPlayer::setPlaybackCallback(var newPlaybackCallback, var synchronous)
{
    playbackUpdater = nullptr;
    bool isSync = ApiHelpers::isSynchronous(synchronous);
    if (HiseJavascriptEngine::isJavascriptFunction(newPlaybackCallback))
    {
#if USE_BACKEND
        if (isSync)
        {
            if (auto co = dynamic_cast<WeakCallbackHolder::CallableObject*>(newPlaybackCallback.getObject()))
            {
                if (RealtimeSafetyInfo::check(co, this, "MidiPlayer.setPlaybackCallback"))
                    reportScriptError("Callback is not safe for synchronous audio-thread execution");
            }
        }
#endif
        playbackUpdater = new PlaybackUpdater(*this, newPlaybackCallback, isSync);
    }
}
```

#### Message::setAllNotesOffCallback

**File:** `hi_scripting/scripting/api/ScriptingApi.cpp`, line 1091

**Current code:**

```cpp
void ScriptingApi::Message::setAllNotesOffCallback(var onAllNotesOffCallback)
{
    allNotesOffCallback = WeakCallbackHolder(getScriptProcessor(), this, onAllNotesOffCallback, 0);
    allNotesOffCallback.incRefCount();
}
```

**Updated (always audio-thread — no sync parameter):**

```cpp
void ScriptingApi::Message::setAllNotesOffCallback(var onAllNotesOffCallback)
{
#if USE_BACKEND
    if (auto co = dynamic_cast<WeakCallbackHolder::CallableObject*>(onAllNotesOffCallback.getObject()))
    {
        if (RealtimeSafetyInfo::check(co, this, "Message.setAllNotesOffCallback"))
            reportScriptError("Callback is not safe for audio-thread execution");
    }
#endif
    allNotesOffCallback = WeakCallbackHolder(getScriptProcessor(), this, onAllNotesOffCallback, 0);
    allNotesOffCallback.incRefCount();
}
```

#### ScriptFFT::setMagnitudeFunction

**File:** `hi_scripting/scripting/api/ScriptingApiObjects.cpp`, line 8100

**Current code:**

```cpp
void ScriptingObjects::ScriptFFT::setMagnitudeFunction(var newMagnitudeFunction, bool convertDb)
{
    SimpleReadWriteLock::ScopedWriteLock sl(lock);
    if (HiseJavascriptEngine::isJavascriptFunction(newMagnitudeFunction))
    {
        convertMagnitudesToDecibel = convertDb;
        magnitudeFunction = WeakCallbackHolder(getScriptProcessor(), this, newMagnitudeFunction, 2);
        magnitudeFunction.incRefCount();
        reinitialise();
    }
}
```

**Updated:**

```cpp
void ScriptingObjects::ScriptFFT::setMagnitudeFunction(var newMagnitudeFunction, bool convertDb)
{
    SimpleReadWriteLock::ScopedWriteLock sl(lock);
    if (HiseJavascriptEngine::isJavascriptFunction(newMagnitudeFunction))
    {
#if USE_BACKEND
        if (auto co = dynamic_cast<WeakCallbackHolder::CallableObject*>(newMagnitudeFunction.getObject()))
        {
            if (RealtimeSafetyInfo::check(co, this, "FFT.setMagnitudeFunction"))
                reportScriptError("Callback is not safe for audio-thread execution");
        }
#endif
        convertMagnitudesToDecibel = convertDb;
        magnitudeFunction = WeakCallbackHolder(getScriptProcessor(), this, newMagnitudeFunction, 2);
        magnitudeFunction.incRefCount();
        reinitialise();
    }
}
```

#### ScriptFFT::setPhaseFunction

**File:** `hi_scripting/scripting/api/ScriptingApiObjects.cpp`, line 8113

Same pattern as `setMagnitudeFunction`:

```cpp
void ScriptingObjects::ScriptFFT::setPhaseFunction(var newPhaseFunction)
{
    SimpleReadWriteLock::ScopedWriteLock sl(lock);
    if (HiseJavascriptEngine::isJavascriptFunction(newPhaseFunction))
    {
#if USE_BACKEND
        if (auto co = dynamic_cast<WeakCallbackHolder::CallableObject*>(newPhaseFunction.getObject()))
        {
            if (RealtimeSafetyInfo::check(co, this, "FFT.setPhaseFunction"))
                reportScriptError("Callback is not safe for audio-thread execution");
        }
#endif
        phaseFunction = WeakCallbackHolder(getScriptProcessor(), this, newPhaseFunction, 2);
        phaseFunction.incRefCount();
        reinitialise();
    }
}
```

#### ScriptUserPresetHandler::attachAutomationCallback

**File:** `hi_scripting/scripting/api/ScriptExpansion.cpp`, line 388

**Current code:**

```cpp
void ScriptUserPresetHandler::attachAutomationCallback(String automationId, var updateCallback, var isSynchronous)
{
    auto n = ApiHelpers::getDispatchType(isSynchronous, false);
    if (auto cData = getMainController()->getUserPresetHandler().getCustomAutomationData(Identifier(automationId)))
    {
        // ... remove existing callback ...
        if (HiseJavascriptEngine::isJavascriptFunction(updateCallback))
            attachedCallbacks.add(new AttachedCallback(this, cData, updateCallback, n));
        return;
    }
    else
        reportScriptError(automationId + " not found");
}
```

**Updated (check for sync dispatch only):**

```cpp
void ScriptUserPresetHandler::attachAutomationCallback(String automationId, var updateCallback, var isSynchronous)
{
    auto n = ApiHelpers::getDispatchType(isSynchronous, false);
    if (auto cData = getMainController()->getUserPresetHandler().getCustomAutomationData(Identifier(automationId)))
    {
        // ... remove existing callback (existing code) ...

#if USE_BACKEND
        if (n == dispatch::DispatchType::sendNotificationSync)
        {
            if (auto co = dynamic_cast<WeakCallbackHolder::CallableObject*>(updateCallback.getObject()))
            {
                if (RealtimeSafetyInfo::check(co, this, "UserPresetHandler.attachAutomationCallback"))
                    reportScriptError("Callback is not safe for synchronous audio-thread execution");
            }
        }
#endif
        if (HiseJavascriptEngine::isJavascriptFunction(updateCallback))
            attachedCallbacks.add(new AttachedCallback(this, cData, updateCallback, n));
        return;
    }
    else
        reportScriptError(automationId + " not found");
}
```

#### ScriptMultipageDialog::bindCallback

**File:** `hi_scripting/scripting/api/ScriptingApiContent.cpp`, line 7241

**Current code:**

```cpp
String ScriptingApi::Content::ScriptMultipageDialog::bindCallback(String id, var callback, var notificationType)
{
    auto n = ApiHelpers::getDispatchType(notificationType, false);
    auto nc = new ValueCallback(this, id, callback, n);
    valueCallbacks.add(nc);
    String callCode;
    callCode << "{BIND::" + id + "}";
    return callCode;
}
```

**Updated (check for sync dispatch only):**

```cpp
String ScriptingApi::Content::ScriptMultipageDialog::bindCallback(String id, var callback, var notificationType)
{
    auto n = ApiHelpers::getDispatchType(notificationType, false);

#if USE_BACKEND
    if (n == dispatch::DispatchType::sendNotificationSync)
    {
        if (auto co = dynamic_cast<WeakCallbackHolder::CallableObject*>(callback.getObject()))
        {
            if (RealtimeSafetyInfo::check(co, this, "ScriptMultipageDialog.bindCallback"))
                reportScriptError("Callback is not safe for synchronous audio-thread execution");
        }
    }
#endif

    auto nc = new ValueCallback(this, id, callback, n);
    valueCallbacks.add(nc);
    String callCode;
    callCode << "{BIND::" + id + "}";
    return callCode;
}
```

---

## 6. Files Changed Summary

| File | Change |
|------|--------|
| `hi_scripting/scripting/engine/HiseJavascriptEngine.h` | `Holder` base on `Callback`; member + override |
| `hi_scripting/scripting/engine/JavascriptEngineCustom.cpp` | `Holder` base on `InlineFunction::Object`; member + `getRealtimeSafetyInfo()` + `getRealtimeSafetyReport()` overrides; `CallScopeAnalyzer` pass; registration in `registerOptimisationPasses()` |
| `hi_scripting/scripting/engine/JavascriptEngineAdditionalMethods.cpp` | `setCurrentRealtimeInfoHolder()` calls in `optimiseFunction()` + callback loop |
| `hi_scripting/scripting/engine/JavascriptEngineParser.cpp` | Post-compilation MIDI callback check |
| `hi_scripting/scripting/api/ScriptingApiObjects.cpp` | Restructured GlobalCable callback (fixes silent-fail bug); upgrade MidiPlayer::setRecordEventCallback, ComplexGroupManager; add check() to MidiPlayer::setPlaybackCallback, ScriptFFT::setMagnitudeFunction, ScriptFFT::setPhaseFunction |
| `hi_scripting/scripting/api/ScriptBroadcaster.cpp` | Upgrade `addListener` |
| `hi_scripting/scripting/api/ScriptingApi.cpp` | Add check() in TransportHandler::Callback constructor; add check() in Message::setAllNotesOffCallback |
| `hi_scripting/scripting/api/ScriptExpansion.cpp` | Add check() in attachAutomationCallback (sync only) |
| `hi_scripting/scripting/api/ScriptingApiContent.cpp` | Add check() in bindCallback (sync only) |

---

## 7. Issues Resolved

All items from `enrichment/issues.md` are fixed by this layer:

| Issue | Severity | Fix |
|-------|----------|-----|
| GlobalCable.registerCallback silent failure | High | Restructured constructor: `reportScriptError()` replaces silent `stop()` (Section 5a) |
| TransportHandler.setOn* no isRealtimeSafe() | Medium | `check()` in Callback constructor (Section 5b) |
| MidiPlayer.setPlaybackCallback no isRealtimeSafe() | Medium | `check()` for sync mode (Section 5b) |
| Message.setAllNotesOffCallback no isRealtimeSafe() | Medium | `check()` always (Section 5b) |
| ScriptFFT.setMagnitudeFunction/setPhaseFunction no isRealtimeSafe() | Medium | `check()` always (Section 5b) |

---

## 8. Verification Checklist

### Basic Compilation
- [ ] HISE compiles in Debug, Release, CI, Minimal Build configurations
- [ ] All `#if USE_BACKEND` guards are correct — zero overhead in exported plugins

### Analyzer Behavior
- [ ] With `CallScopeWarnings = Relaxed`: no analyzer pass runs, no warnings, identical behavior to before
- [ ] With `CallScopeWarnings = Warn`: analyzer runs, warnings logged for unsafe calls in MIDI callbacks
- [ ] With `CallScopeWarnings = Error`: analyzer runs, compilation fails for unsafe calls in audio-thread MIDI callbacks

### Test Script (Warn mode)

```javascript
// This should produce a warning for Console.print in onNoteOn
inline function unsafeHelper()
{
    Console.print("hello");  // caution: allocates in IDE
}

function onNoteOn()
{
    unsafeHelper();  // transitive warning inherited from unsafeHelper
}
```

Expected console output (Warn mode):
```
[onNoteOn] [CallScope] Console.print (caution: Allocates in HISE IDE; no-op in exported plugins)
  at unsafeHelper (Script.js:3)
  at onNoteOn (Script.js:8)
```

### Test Script (Registration site)

```javascript
const var cable = Engine.getGlobalRoutingManager().getCable("test");

inline function myCableCallback(value)
{
    Console.print(value);  // caution
}

cable.registerCallback(myCableCallback, true);  // sync = true → check() runs
```

Expected: With `Warn`, console warning logged at `registerCallback` but callback registers normally. With `Error`, `reportScriptError` thrown.

### Call Site Coverage
- [ ] GlobalCable.registerCallback: `check()` runs; silent failure bug fixed
- [ ] MidiPlayer.setRecordEventCallback: `check()` replaces manual `isRealtimeSafe()`
- [ ] ComplexGroupManager.registerGroupStartCallback: `check()` replaces manual `isRealtimeSafe()`
- [ ] Broadcaster.addListener: `check()` replaces manual `isRealtimeSafe()` (when RT-safe)
- [ ] TransportHandler (all 5 setters): `check()` in Callback constructor for sync mode
- [ ] MidiPlayer.setPlaybackCallback: `check()` for sync mode
- [ ] Message.setAllNotesOffCallback: `check()` always (always sync)
- [ ] FFT.setMagnitudeFunction: `check()` always (always audio-thread)
- [ ] FFT.setPhaseFunction: `check()` always (always audio-thread)
- [ ] UserPresetHandler.attachAutomationCallback: `check()` for sync mode
- [ ] ScriptMultipageDialog.bindCallback: `check()` for sync mode

### Edge Cases
- [ ] Empty inline function body: no warnings, no crash
- [ ] Inline function with only safe calls: no warnings
- [ ] Deeply nested transitive calls (A calls B calls C which has unsafe): full callstack in warning
- [ ] Non-enriched API methods (no callScope data): Unknown scope, no warning
- [ ] Greedy resolution: method name shared across multiple classes with mixed scopes
- [ ] `onInit` callback body: analyzed but no warnings emitted (not an audio-thread callback)
- [ ] `onControl` callback body: analyzed but no warnings emitted (not an audio-thread callback)
