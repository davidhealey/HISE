# HISE Unit Testing Guidelines

**Related Documentation:**
- [voice-setter-system.md](voice-setter-system.md) - Uses these testing patterns extensively (Part 10)

## Overview

This document describes patterns and best practices for unit testing HISE components. It covers both single-threaded and multi-threaded testing scenarios.

**Reference Implementation:** `hi_dsp_library/unit_test/uiupdater_tests.cpp`

---

## Scenario-Based Bug Analysis

A methodology for investigating complex bugs (threading, race conditions, state machines) through systematic scenario enumeration before diving into code.

### The Process

| Phase | Activity | Output |
|-------|----------|--------|
| 1. Define Failure Modes | List abstract ways things could go wrong | 2-4 categories |
| 2. Enumerate Scenarios | Hypothetical code patterns triggering each failure | 5-10 numbered scenarios |
| 3. Classify Scenarios | Bug? Correct behavior? Performance concern? | Severity table |
| 4. Write Scenario Tests | Test abstract patterns, not specific code paths | Test coverage |
| 5. Search Codebase | Find real occurrences of problematic patterns | Fix list |

### Example: Threading Bug Analysis

**Phase 1 - Failure Modes:**
- Wrong single element accessed when all intended
- All elements accessed when single intended
- Expensive lookup in hot path

**Phase 3 - Classification Table:**

| Scenario | Description | Verdict |
|----------|-------------|---------|
| #1 | Nested scope overrides outer | CRITICAL if occurs |
| #6 | Cross-thread race on shared index | CONFIRMED BUG |
| #7 | Same-thread access before loop | Correct behavior |

**Phase 4 - Test Naming:**
```cpp
void testScenario6RaceWithoutProtection()     // Demonstrates the bug
void testScenario6FixWithProtection()         // Proves the fix
void testScenario7NoProtectionNeeded()        // Documents correct behavior
```

### Key Insight

Test the **category of bug**, not specific code paths. A test for "UI thread accessing shared data without protection" catches:
- The current bug
- Future bugs matching the same pattern
- Serves as documentation of the failure mode

---

## When to Use Multi-Threaded Testing

Use the multi-threaded test pattern when testing code that:
- Has callbacks triggered from both audio and UI threads
- Uses lock-free data exchange between threads
- Has timing-dependent behavior (rate limiting, throttling)
- Requires verifying thread safety under realistic conditions

For simple synchronous code, standard single-threaded tests are sufficient.

---

## The Problem with Single-Threaded Tests

Standard single-threaded unit tests cannot verify:
- Race conditions between threads
- Timing-dependent behavior under realistic callback rates
- Thread-safety wrappers are correctly applied during callbacks
- Data integrity under concurrent read/write access

---

## Key Insights for Lean Test Syntax

Lessons learned during design iteration:

| Insight | What We Learned |
|---------|-----------------|
| **Parallel structure reveals intent** | When testing two threads, the test code should visually show both sides in parallel: `AudioThread([&](){...}), UIThread([&](){...})` |
| **Move counting out of test nodes** | Don't put `std::atomic<int> callbackCount` in the node class - put it in the test lambda. Keeps the node pure. |
| **Use callback hooks, not inheritance** | Instead of overriding methods per-test, use `std::function<void()> onCallback` that the test sets. Test logic stays in test. |
| **Default stop conditions** | Most tests stop after N audio callbacks. Don't force every test to define this - make it configurable with a default. |
| **Class members reduce boilerplate** | If `PolyHandler`, test nodes, and specs don't have persistent state issues, make them class members. Reset between tests. |
| **Setup methods encode common patterns** | `setupAsyncTest(rateLimiting, fps, triggers)` is better than 5 lines of manual setup per test. |
| **Name things for the reader** | `sim.run([&](){})` - what does it run? Better: `AudioThread([&](){})` makes it obvious. |
| **Question every lambda** | For each lambda in the API, ask "what concrete thing would go here?" If no good answer, remove it. |
| **Shared data types** | If 90% of tests use similar data, define one `TestData` struct with all fields. Unused fields don't matter. |
| **Internal wiring is invisible** | `UIThread` lambda gets wired to `node.onCallback` internally. Test doesn't need to see this. |

---

## Design Process Checklist

When creating a new test system:

- [ ] Analyze code for coverage - list every method/branch
- [ ] Categorize: single-threaded vs multi-threaded tests needed
- [ ] Identify shared state that can be class members
- [ ] Design setup methods for common configurations
- [ ] Create wrapper types for readability (`AudioThread`, `UIThread`, `StopWhen`)
- [ ] Add callback hooks to test nodes (not counters)
- [ ] Define shared test data struct if testing data exchange
- [ ] Write one validation test first to confirm framework works
- [ ] Document coverage roadmap for remaining tests

---

## Pattern: Threaded Test Framework

### Wrapper Types for Readable Syntax

```cpp
template<typename F> struct AudioThread { F callback; };
template<typename F> struct UIThread { F callback; };
template<typename F> struct StopWhen { F condition; };

// Deduction guides (C++17)
template<typename F> AudioThread(F) -> AudioThread<F>;
template<typename F> UIThread(F) -> UIThread<F>;
template<typename F> StopWhen(F) -> StopWhen<F>;
```

### Result Struct

```cpp
struct ThreadedTestResult
{
    bool completed = false;
    int audioCallbacksExecuted = 0;
    int uiIterationsExecuted = 0;
    double averageAudioIntervalMs = 0.0;
    double totalTimeMs = 0.0;
};
```

### Mock Audio Thread (Nested Class)

```cpp
class MockAudioThread : private juce::Thread
{
public:
    MockAudioThread(double sampleRate, int blockSize, int timeoutMs)
        : Thread("Mock Audio Thread"), 
          sampleRate(sampleRate), 
          blockSize(blockSize),
          timeoutMs(timeoutMs) {}
    
    ~MockAudioThread() { stopThread(1000); }
    
    template<typename AudioF, typename StopF>
    ThreadedTestResult run(AudioF&& audioCallback, StopF&& stopCondition)
    {
        this->audioCallback = std::forward<AudioF>(audioCallback);
        this->stopCondition = std::forward<StopF>(stopCondition);
        
        result = {};
        shouldStop = false;
        
        startThread();
        
        auto startTime = juce::Time::getMillisecondCounterHiRes();
        
        while (!stopCondition())
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            result.uiIterationsExecuted++;
            
            auto elapsed = juce::Time::getMillisecondCounterHiRes() - startTime;
            if (elapsed > timeoutMs)
            {
                shouldStop = true;
                break;
            }
        }
        
        result.completed = stopCondition();
        result.totalTimeMs = juce::Time::getMillisecondCounterHiRes() - startTime;
        
        shouldStop = true;
        stopThread(1000);
        
        if (result.audioCallbacksExecuted > 1)
            result.averageAudioIntervalMs = result.totalTimeMs / (result.audioCallbacksExecuted - 1);
        
        return result;
    }
    
private:
    void run() override
    {
        int sleepMs = juce::jmax(1, juce::roundToInt((blockSize / sampleRate) * 1000.0));
        
        while (!threadShouldExit() && !shouldStop && !stopCondition())
        {
            audioCallback();
            result.audioCallbacksExecuted++;
            Thread::sleep(sleepMs);
        }
    }
    
    double sampleRate;
    int blockSize;
    int timeoutMs;
    std::atomic<bool> shouldStop { false };
    std::function<void()> audioCallback;
    std::function<bool()> stopCondition;
    ThreadedTestResult result;
};
```

### Runner Method in Test Class

```cpp
template<typename AudioF, typename UIF, typename StopF>
ThreadedTestResult runThreadedTest(
    AudioThread<AudioF> audio,
    UIThread<UIF> ui,
    StopWhen<StopF> stop)
{
    // Wire UI callback to active node
    *activeCallback = ui.callback;
    
    // Create and run mock audio thread
    MockAudioThread mockAudio(kSampleRate, kBlockSize, kTimeoutMs);
    return mockAudio.run(audio.callback, stop.condition);
}
```

---

## Pattern: Test Node with Callback Hook

Instead of creating custom test node subclasses per test, create one reusable node with a callback hook:

```cpp
// Example: Wrap your component with a callback hook
struct TestableComponent : YourComponent
{
    std::function<void()> onCallback;
    void onSomeEvent() override { if (onCallback) onCallback(); }
};
```

This allows test logic to remain in the test:

```cpp
std::atomic<int> callbackCount { 0 };
testNode.onCallback = [&]() { callbackCount++; };  // Test-specific logic
```

---

## Pattern: Shared Test Data

When testing data exchange, define one struct that covers all test scenarios:

```cpp
struct TestData
{
    int sequence = 0;       // For ordering/integrity checks
    float value = 0.0f;     // For value checks
    // Add fields relevant to your component...
};
```

Tests that don't need all fields simply ignore them.

---

## Pattern: Setup Methods

Encode common configurations in setup methods. Adapt to your component's needs:

```cpp
void setupTest(bool someOption = false, int someParameter = 30)
{
    // 1. Stop any running activity from previous tests
    testNode.stop();
    
    // 2. Clear callbacks
    testNode.onCallback = nullptr;
    
    // 3. Configure the component
    testNode.setup(createDefaultSpecs());
    if (someOption)
        testNode.enableFeature(someParameter);
    
    // 4. Wire up for runThreadedTest if needed
    activeCallback = &testNode.onCallback;
}
```

**Key points:**
- Reset state from previous tests
- Clear callback hooks
- Apply test-specific configuration
- Wire callbacks for the test framework

---

## Anti-Patterns to Avoid

| Don't | Do Instead |
|-------|------------|
| Put test counters in node classes | Define counters in test, use callback hook |
| Create local test node structs per test | Use class member nodes with reset/setup methods |
| Require empty lambdas for unused features | Make parameters optional or remove them |
| Name things generically (`run`, `sim`, `callback`) | Name for intent (`AudioThread`, `UIThread`, `onCallback`) |
| Hardcode timing/message loop pumping | Encapsulate in `runThreadedTest` |
| Let nodes persist state between tests | Call setup method at start of each test |
| Put test code in header files | Put complete test class in `.cpp` file (see below) |
| Use class member test objects with JUCE dependencies | Use `ScopedPointer<>` and destroy at end of `runTest()` |

---

## Test File Organization

Never place test code in header files - the `static MyTests myTests;` registration compiles once per translation unit, causing tests to run multiple times.

| Component | Location |
|-----------|----------|
| Test class + static registration | `.cpp` file |
| Include in build | `#include "unit_test/foo_tests.cpp"` under `#if HI_RUN_UNIT_TESTS` |

---

## Static Destruction Pitfalls

Test objects with JUCE dependencies (Timer, AsyncUpdater) as class members crash during static destruction (after JUCE teardown). Use `ScopedPointer<>` and destroy explicitly:

```cpp
juce::ScopedPointer<TestTimer> timerNode;

void runTest() override
{
    // ... tests ...
    timerNode = nullptr;  // Destroy while JUCE alive
}
```

---

## Test Documentation Format

Every test should have a doc comment with three sections:

```cpp
/** Setup: [One line describing test configuration]
 *
 *  Scenario: [What operations are performed, how threads interact if multi-threaded]
 *
 *  Expected: [High-level description of verified behavior, not just restating expect() calls]
 */
void testSomething()
```

### Example (Single-Threaded)

```cpp
/** Setup: Component with caching enabled, empty cache.
 *
 *  Scenario: Call getData() three times with same key, inspect cache hit counter.
 *
 *  Expected: First call misses cache (counter=0), subsequent calls hit cache
 *  (counter increments), proving caching logic works correctly.
 */
void testCacheHitBehavior()
```

### Example (Multi-Threaded)

```cpp
/** Setup: Lock-free queue connecting producer and consumer threads.
 *
 *  Scenario: Producer thread pushes 100 items, consumer thread pops and
 *  verifies sequence numbers never decrease.
 *
 *  Expected: All items received in order, no data corruption, proving
 *  thread-safe handoff between threads.
 */
void testLockFreeQueueIntegrity()
```

### Why This Format

- **Setup** establishes context without repeating boilerplate
- **Scenario** describes the test logic at a higher level than code
- **Expected** explains *why* the assertions matter, not just *what* they check

When reviewing tests or updating them after code changes, compare the Expected
section against the actual expect() calls to catch deprecation or drift.

---

## Test Runtime Optimization

Keep test durations minimal while maintaining robust coverage. Fast tests enable quick CI feedback.

**Guidelines:**
- Use the minimum number of iterations needed to verify behavior (e.g., 20-40 callbacks instead of 100)
- Multi-threaded tests: run long enough to observe thread interactions, but no longer
- Avoid arbitrary sleep/wait times; use condition-based stopping (`StopWhen`)
- If a test takes >500ms, consider whether fewer iterations would still catch failures
