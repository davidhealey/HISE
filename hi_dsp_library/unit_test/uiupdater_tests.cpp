/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licencing:
*
*   http://www.hartinstruments.net/hise/
*
*   HISE is based on the JUCE library,
*   which also must be licenced for commercial applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

namespace hise
{

using namespace juce;

struct NodeUIUpdaterTests : public juce::UnitTest
{
    NodeUIUpdaterTests()
        : UnitTest ("NodeUIUpdater Tests", "UI")
    {
    }

    // =========================================
    // Configuration
    // =========================================

    static constexpr double kSampleRate = 44100.0;
    static constexpr int kBlockSize = 512;
    static constexpr int kTimeoutMs = 5000;

    // =========================================
    // Shared Test Data
    // =========================================

    struct TestData
    {
        int sequence = 0;
        float value = 0.0f;
        int voiceIndex = -1;
    };

    // =========================================
    // Test Nodes with Callback Hooks
    // =========================================

    struct TestAsyncUpdater : public NodeUIAsyncUpdater::WithData<TestData>
    {
        std::function<void()> onCallback;

        void onUIAsyncUpdate() override
        {
            if (onCallback)
                onCallback();
        }
    };

    struct TestTimer : public NodeUITimer::WithData<TestData>
    {
        std::function<void()> onCallback;

        void onUITimerCallback() override
        {
            if (onCallback)
                onCallback();
        }
    };

    // =========================================
    // Wrapper Types for Readable Test Syntax
    // =========================================

    template <typename F>
    struct AudioThreadT
    {
        F callback;
    };

    template <typename F>
    struct UIThreadT
    {
        F callback;
    };

    template <typename F>
    struct StopWhenT
    {
        F condition;
    };

    // Helper functions to create wrapper types (MSVC doesn't support in-class CTAD)
    template <typename F>
    static AudioThreadT<F> AudioThread (F&& f)
    {
        return { std::forward<F> (f) };
    }

    template <typename F>
    static UIThreadT<F> UIThread (F&& f)
    {
        return { std::forward<F> (f) };
    }

    template <typename F>
    static StopWhenT<F> StopWhen (F&& f)
    {
        return { std::forward<F> (f) };
    }

    // =========================================
    // Threaded Test Result
    // =========================================

    struct ThreadedTestResult
    {
        bool completed = false;
        int audioCallbacksExecuted = 0;
        int uiIterationsExecuted = 0;
        double averageAudioIntervalMs = 0.0;
        double totalTimeMs = 0.0;
    };

    // =========================================
    // Mock Audio Thread
    // =========================================

    class MockAudioThread : private juce::Thread
    {
    public:
        MockAudioThread (double sampleRate, int blockSize, int timeoutMs)
            : Thread ("Mock Audio Thread")
            , sampleRate (sampleRate)
            , blockSize (blockSize)
            , timeoutMs (timeoutMs)
        {
        }

        ~MockAudioThread() { stopThread (1000); }

        template <typename AudioF, typename StopF>
        ThreadedTestResult run (AudioF&& audioFunc, StopF&& stopFunc)
        {
            audioCallback = std::forward<AudioF> (audioFunc);
            stopCondition = std::forward<StopF> (stopFunc);

            result = {};
            shouldStop = false;

            startThread();

            auto startTime = juce::Time::getMillisecondCounterHiRes();

            while (! stopCondition())
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
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
            stopThread (1000);

            if (result.audioCallbacksExecuted > 1)
                result.averageAudioIntervalMs = result.totalTimeMs / (result.audioCallbacksExecuted - 1);

            return result;
        }

    private:
        void run() override
        {
            int sleepMs = juce::jmax (1, juce::roundToInt ((blockSize / sampleRate) * 1000.0));

            while (! threadShouldExit() && ! shouldStop && ! stopCondition())
            {
                audioCallback();
                result.audioCallbacksExecuted++;
                sleep (sleepMs);
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

    // =========================================
    // Shared Members
    // =========================================

    PolyHandler polyHandler { true };
    juce::ScopedPointer<TestAsyncUpdater> asyncNode;
    juce::ScopedPointer<TestTimer> timerNode;
    std::function<void()>* activeCallback = nullptr;

    // =========================================
    // Helper Methods
    // =========================================

    PrepareSpecs createDefaultSpecs()
    {
        PrepareSpecs specs;
        specs.sampleRate = kSampleRate;
        specs.blockSize = kBlockSize;
        specs.numChannels = 2;
        specs.voiceIndex = &polyHandler;
        return specs;
    }

    void setupAsyncTest (bool rateLimiting = false, double targetFPS = 30.0, int triggersPerCallback = 1)
    {
        // Destroy old nodes and create fresh ones
        timerNode = nullptr;

        // Cancel any pending juce::AsyncUpdater message before destroying the old node
        // to avoid use-after-free if a message was dequeued but not yet dispatched.
        if (asyncNode != nullptr)
            asyncNode->cancelPendingUpdate();

        asyncNode = new TestAsyncUpdater();

        // Setup async node
        asyncNode->setupUIUpdater (createDefaultSpecs());
        if (rateLimiting)
            asyncNode->setEnableRateLimiting (true, targetFPS, triggersPerCallback);

        // Set active callback pointer for runThreadedTest
        activeCallback = &asyncNode->onCallback;
    }

    void setupTimerTest (int frequencyHz = 30)
    {
        // Destroy old nodes and create fresh ones
        if (asyncNode != nullptr)
            asyncNode->cancelPendingUpdate();

        asyncNode = nullptr;
        timerNode = new TestTimer();

        // Setup timer node
        timerNode->setupUIUpdater (createDefaultSpecs());
        timerNode->startTimerHz (frequencyHz);

        // Set active callback pointer for runThreadedTest
        activeCallback = &timerNode->onCallback;
    }

    void expectWithinRange (int value, int minVal, int maxVal, const juce::String& message)
    {
        expect (value >= minVal && value <= maxVal,
                message + " (got " + juce::String (value) + ", expected " + juce::String (minVal) + "-" + juce::String (maxVal) + ")");
    }

    template <typename AudioF, typename UIF, typename StopF>
    ThreadedTestResult runThreadedTest (AudioThreadT<AudioF> audio, UIThreadT<UIF> ui, StopWhenT<StopF> stop)
    {
        // Wire UI callback to active node
        *activeCallback = ui.callback;

        // Create and run mock audio thread
        MockAudioThread mockAudio (kSampleRate, kBlockSize, kTimeoutMs);
        return mockAudio.run (audio.callback, stop.condition);
    }

    // Variant that doesn't overwrite the callback (for timer tests where callback is set beforehand)
    template <typename AudioF, typename StopF>
    ThreadedTestResult runThreadedTestWithExistingCallback (AudioThreadT<AudioF> audio, StopWhenT<StopF> stop)
    {
        // Don't overwrite the callback - it was set before calling this method
        MockAudioThread mockAudio (kSampleRate, kBlockSize, kTimeoutMs);
        return mockAudio.run (audio.callback, stop.condition);
    }

    void runTest() override
    {
        // Framework validation (run first)
        testThreadedTestFramework();

// skip all tests on macOS for now - something with the mock audio thread is causing the test suite to crash...
#if JUCE_WINDOWS
        

        // Single-threaded tests - basic functionality
        testRateLimitingCalculation();
        testRateLimitingCounter();
        testVoiceTrackingDisabled();
        testVoiceTrackingEnabled();
        testWithDataPostConsume();
        testWithDataMultiplePosts();
        testWithDataNoData();
        testWithDataTimerSingleThreaded();

        // Single-threaded tests - edge cases and additional coverage
        testSetActiveVoiceWithNullPolyHandler();
        testRateLimitingWithInvalidSpecs();
        testTriggerAsyncUpdateNoRateLimiting();
        testCallWithVoiceProtectionNullHandler();
        testTimerStartStopRunning();

        // Multi-threaded tests
        testAsyncUpdateUnderLoad();
        testAsyncUpdateRateLimitedUnderLoad();
        testAsyncUpdateNoRateLimitingUnderLoad();
        testTimerUnderLoad();
        testVoiceProtectionUnderLoad();
        testVoiceTrackingUnderLoad();
        testWithDataTimerUnderLoad();
        testWithDataAsyncUnderLoad();
        testVoiceTrackingBlocksPostData();
        testVoiceTrackingBlocksTrigger();

        // PolyData single-threaded tests - construction & setup
        testPolyDataDefaultConstruction();
        testPolyDataValueConstruction();
        testPolyDataPrepare();
        testPolyDataSetAll();

        // PolyData single-threaded tests - iteration
        testPolyDataBeginEndSingleVoice();
        testPolyDataBeginEndAllVoices();
        testPolyDataRangeForSingleVoice();
        testPolyDataRangeForAllVoices();

        // PolyData single-threaded tests - get()
        testPolyDataGetReturnsCurrentVoice();

        // PolyData single-threaded tests - iteration with voice context
        testPolyDataIterateSingleVoice();
        testPolyDataIterateAllVoices();

        // PolyData single-threaded tests - all()
        testPolyDataAllReturnsAllVoices();
        testPolyDataAllIgnoresVoiceContext();

        // PolyData single-threaded tests - utility methods
        testPolyDataIsFirst();
        testPolyDataGetFirst();
        testPolyDataGetVoiceIndexForData();
        testPolyDataGetWithIndex();
        testPolyDataIsMonophonicOrInsideVoiceRendering();
        testPolyDataIsVoiceRenderingActive();
        testPolyDataIsPolyHandlerEnabled();

        // PolyData + WithData integration tests
        testWithDataPostsFromPolyDataGet();
        testWithDataAggregatesPolyDataVoices();

        // PolyData multi-threaded tests
        testPolyDataUIIterationUnderLoad();

        // PolyData thread safety scenario tests
        testScenario6RaceWithoutProtection();
        testScenario6FixWithScopedAllVoiceSetter();
        testScopedAllVoiceSetterNestingSafe();
        testScenario7AudioThreadNoProtectionNeeded();

        // Cleanup: destroy nodes while JUCE infrastructure is still alive
        // (static destruction order would otherwise cause crash in CriticalSection)
        timerNode = nullptr;

        if (asyncNode != nullptr)
            asyncNode->cancelPendingUpdate();

        asyncNode = nullptr;
#endif
    }

    // =========================================
    // Framework Validation Test
    // =========================================

    /** Setup: AsyncUpdater with PolyHandler, no rate limiting.
	 *
	 *  Scenario: Audio thread triggers async updates 20 times while UI thread pumps
	 *  the message loop. Both threads increment their own atomic counters.
	 *
	 *  Expected: Both threads execute, UI receives callbacks, test completes without
	 *  timeout - validates the multi-threaded test infrastructure itself works.
	 */
    void testThreadedTestFramework()
    {
        beginTest ("Threaded test framework validation");

        setupAsyncTest();

        std::atomic<int> audioCallbacks { 0 };
        std::atomic<int> uiCallbacks { 0 };

        auto result = runThreadedTest (
            AudioThread ([&]()
                         {
                             PolyHandler::ScopedVoiceSetter svs (polyHandler, 0);
                             asyncNode->triggerAsyncUpdate();
                             audioCallbacks++;
                         }),
            UIThread ([&]()
                      {
                          uiCallbacks++;
                      }),
            StopWhen ([&]()
                      {
                          return audioCallbacks >= 20;
                      }));

        expect (result.completed, "Should complete without timeout");
        expectEquals (audioCallbacks.load(), 20, "Should execute 20 audio callbacks");
        expect (uiCallbacks.load() > 0, "Should have received UI callbacks");
    }

    // =========================================
    // Single-Threaded Basic Tests
    // =========================================

    /** Setup: Timer::WithData<T> with no voice tracking.
	 *
	 *  Scenario: Post sequence 1, 2, 3, consume, then consume again.
	 *
	 *  Expected: First consume returns sequence=3, second returns false. Single-threaded
	 *  baseline before testing under concurrent load.
	 */
    void testWithDataTimerSingleThreaded()
    {
        beginTest ("WithData Timer single-threaded (no race condition)");

        struct LocalTestTimer : public NodeUITimer::WithData<TestData>
        {
            void onUITimerCallback() override {}
        };

        LocalTestTimer timer;
        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = nullptr;
        timer.setupUIUpdater (specs);

        // Post incrementing sequences
        timer.postData ({ 1, 1.0f, 0 });
        timer.postData ({ 2, 2.0f, 0 });
        timer.postData ({ 3, 3.0f, 0 });

        // Consume - should get latest (3)
        TestData received;
        bool hasData = timer.consumeData (received);

        expect (hasData, "Should have data after posting");
        expectEquals (received.sequence, 3, "Should receive latest sequence (3)");

        // Second consume - no new data
        hasData = timer.consumeData (received);
        expect (! hasData, "Should not have data after consuming");
    }

    /** Setup: AsyncUpdater with valid PrepareSpecs (44.1kHz/512), no voice tracking.
	 *
	 *  Scenario: Call setEnableRateLimiting() with different sampleRate, blockSize,
	 *  targetFPS, and triggersPerCallback values, then inspect getUpdateThreshold().
	 *
	 *  Expected: Threshold matches formula: (sampleRate / blockSize / targetFPS) * triggersPerCallback,
	 *  with jmax(1, ...) ensuring minimum of 1. Disabling rate limiting resets threshold to 1.
	 */
    void testRateLimitingCalculation()
    {
        beginTest ("Rate limiting threshold calculation");

        struct TestAsyncUpdater : public NodeUIAsyncUpdater
        {
            void onUIAsyncUpdate() override {}

            using NodeUIAsyncUpdater::getUpdateThreshold;
        };

        TestAsyncUpdater updater;
        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = nullptr;

        updater.setupUIUpdater (specs);
        updater.setEnableRateLimiting (true, 30.0, 1);

        // Expected: 44100 / 512 / 30 = 2.87 -> jmax(1, 2) = 2
        int threshold = updater.getUpdateThreshold();
        expectEquals (threshold, 2, "Threshold should be 2 for 44.1kHz/512/30fps");

        // Test with different settings: 48000 / 128 / 60 = 6.25 -> 6
        specs.sampleRate = 48000.0;
        specs.blockSize = 128;
        updater.setupUIUpdater (specs);
        updater.setEnableRateLimiting (true, 60.0, 1);

        threshold = updater.getUpdateThreshold();
        expectEquals (threshold, 6, "Threshold should be 6 for 48kHz/128/60fps");

        // Test with triggersPerCallback = 2: 44100 / 512 / 30 * 2 = 5.74 -> 5
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        updater.setupUIUpdater (specs);
        updater.setEnableRateLimiting (true, 30.0, 2);

        threshold = updater.getUpdateThreshold();
        expectEquals (threshold, 5, "Threshold should be 5 for 44.1kHz/512/30fps with 2 triggers per callback");

        // Test disabling rate limiting sets threshold to 1
        updater.setEnableRateLimiting (false);
        threshold = updater.getUpdateThreshold();
        expectEquals (threshold, 1, "Threshold should be 1 when rate limiting disabled");
    }

    /** Setup: AsyncUpdater with rate limiting threshold=2, no voice tracking.
	 *
	 *  Scenario: Call triggerAsyncUpdate() three times sequentially, inspect
	 *  counter after each call.
	 *
	 *  Expected: Counter cycles 0 -> 1 -> 0 (reset at threshold) -> 1, proving
	 *  rate limiting only queues an actual update every N triggers.
	 */
    void testRateLimitingCounter()
    {
        beginTest ("Rate limiting counter behavior");

        struct TestAsyncUpdater : public NodeUIAsyncUpdater
        {
            void onUIAsyncUpdate() override {}

            using NodeUIAsyncUpdater::getRateLimitCounter;
            using NodeUIAsyncUpdater::getUpdateThreshold;
        };

        TestAsyncUpdater updater;
        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = nullptr;

        updater.setupUIUpdater (specs);
        updater.setEnableRateLimiting (true, 30.0, 1);

        int threshold = updater.getUpdateThreshold();
        expectEquals (threshold, 2, "Threshold should be 2");

        // Counter should start at 0
        expectEquals (updater.getRateLimitCounter(), 0, "Counter should start at 0");

        // First trigger increments counter to 1
        updater.triggerAsyncUpdate();
        expectEquals (updater.getRateLimitCounter(), 1, "Counter should be 1 after first trigger");

        // Second trigger reaches threshold, counter resets to 0
        updater.triggerAsyncUpdate();
        expectEquals (updater.getRateLimitCounter(), 0, "Counter should reset to 0 after reaching threshold");

        // Third trigger increments to 1 again
        updater.triggerAsyncUpdate();
        expectEquals (updater.getRateLimitCounter(), 1, "Counter should be 1 after third trigger");

        updater.cancelPendingUpdate();
    }

    /** Setup: Timer with PolyHandler, voice tracking NOT enabled (default state).
	 *
	 *  Scenario: Set different voice indices via ScopedVoiceSetter (0, 5, 15),
	 *  call shouldUpdateForCurrentVoice() for each.
	 *
	 *  Expected: All voices return true, proving that without explicit voice tracking
	 *  enabled, all voices are allowed to trigger UI updates.
	 */
    void testVoiceTrackingDisabled()
    {
        beginTest ("Voice tracking disabled - all voices update");

        struct TestTimer : public NodeUITimer
        {
            void onUITimerCallback() override {}

            using NodeUIUpdaterBase::shouldUpdateForCurrentVoice;
        };

        PolyHandler localPolyHandler (true);

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = &localPolyHandler;

        TestTimer timer;
        timer.setupUIUpdater (specs);

        // Without calling setActiveVoiceForUIUpdates(), all voices should update
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 0);
            expect (timer.shouldUpdateForCurrentVoice(), "Voice 0 should update when tracking disabled");
        }
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);
            expect (timer.shouldUpdateForCurrentVoice(), "Voice 5 should update when tracking disabled");
        }
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 15);
            expect (timer.shouldUpdateForCurrentVoice(), "Voice 15 should update when tracking disabled");
        }
    }

    /** Setup: Timer with PolyHandler, voice tracking enabled for voice 3.
	 *
	 *  Scenario: Call shouldUpdateForCurrentVoice() from different voice contexts
	 *  (voice 0, 3, 7). Then change tracked voice to 7 and retest.
	 *
	 *  Expected: Only the tracked voice returns true. After changing to voice 7,
	 *  voice 3 returns false and voice 7 returns true.
	 */
    void testVoiceTrackingEnabled()
    {
        beginTest ("Voice tracking enabled - only tracked voice updates");

        struct TestTimer : public NodeUITimer
        {
            void onUITimerCallback() override {}

            using NodeUIUpdaterBase::shouldUpdateForCurrentVoice;
        };

        PolyHandler localPolyHandler (true);

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = &localPolyHandler;

        TestTimer timer;
        timer.setupUIUpdater (specs);

        // Set voice 3 as the tracked voice
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 3);
            timer.setActiveVoiceForUIUpdates();
        }

        // Now only voice 3 should return true
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 3);
            expect (timer.shouldUpdateForCurrentVoice(), "Tracked voice 3 should update");
        }
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 0);
            expect (! timer.shouldUpdateForCurrentVoice(), "Voice 0 should NOT update when voice 3 is tracked");
        }
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 7);
            expect (! timer.shouldUpdateForCurrentVoice(), "Voice 7 should NOT update when voice 3 is tracked");
        }

        // Change tracked voice to 7
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 7);
            timer.setActiveVoiceForUIUpdates();
        }

        // Now voice 7 should update, voice 3 should not
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 7);
            expect (timer.shouldUpdateForCurrentVoice(), "Newly tracked voice 7 should update");
        }
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 3);
            expect (! timer.shouldUpdateForCurrentVoice(), "Previously tracked voice 3 should NOT update");
        }
    }

    /** Setup: Timer::WithData<T> with no voice tracking.
	 *
	 *  Scenario: Post data {0.75f, 42}, then consume it.
	 *
	 *  Expected: consumeData() returns true with correct values, proving basic
	 *  lock-free data exchange works.
	 */
    void testWithDataPostConsume()
    {
        beginTest ("WithData basic post and consume");

        struct LocalTestData
        {
            float value;
            int id;
        };

        struct LocalTestTimer : public NodeUITimer::WithData<LocalTestData>
        {
            void onUITimerCallback() override {}
        };

        LocalTestTimer timer;
        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = nullptr;
        timer.setupUIUpdater (specs);

        // Post data
        timer.postData ({ 0.75f, 42 });

        // Consume data
        LocalTestData received = { 0.0f, 0 };
        bool hasData = timer.consumeData (received);

        expect (hasData, "Should have data after post");
        expectEquals (received.value, 0.75f, "Value should match posted value");
        expectEquals (received.id, 42, "ID should match posted ID");
    }

    /** Setup: Timer::WithData<T> with no voice tracking.
	 *
	 *  Scenario: Post three values rapidly (sequence 1, 2, 3), then consume once.
	 *
	 *  Expected: consumeData() returns sequence=3 (latest value wins), second
	 *  consume returns false (data already consumed).
	 */
    void testWithDataMultiplePosts()
    {
        beginTest ("WithData multiple posts - only latest consumed");

        struct LocalTestData
        {
            int sequence;
        };

        struct LocalTestTimer : public NodeUITimer::WithData<LocalTestData>
        {
            void onUITimerCallback() override {}
        };

        LocalTestTimer timer;
        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = nullptr;
        timer.setupUIUpdater (specs);

        // Post multiple values rapidly (simulating audio thread posting each buffer)
        timer.postData ({ 1 });
        timer.postData ({ 2 });
        timer.postData ({ 3 });

        // Consume should get the latest
        LocalTestData received = { 0 };
        bool hasData = timer.consumeData (received);

        expect (hasData, "Should have data after multiple posts");
        expectEquals (received.sequence, 3, "Should receive latest value (3)");

        // Second consume should return false (data already consumed)
        hasData = timer.consumeData (received);
        expect (! hasData, "Second consume should return false - no new data");
    }

    /** Setup: Timer::WithData<T> with no voice tracking, no data posted.
	 *
	 *  Scenario: Call consumeData() without posting anything first.
	 *
	 *  Expected: consumeData() returns false, proving no spurious data appears.
	 */
    void testWithDataNoData()
    {
        beginTest ("WithData consume with no data posted");

        struct LocalTestData
        {
            float x;
        };

        struct LocalTestTimer : public NodeUITimer::WithData<LocalTestData>
        {
            void onUITimerCallback() override {}
        };

        LocalTestTimer timer;
        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = nullptr;
        timer.setupUIUpdater (specs);

        // Try to consume without posting anything
        LocalTestData received = { 999.0f };
        bool hasData = timer.consumeData (received);

        expect (! hasData, "Should return false when no data has been posted");
        // Note: received value is undefined when hasData is false, so we don't check it
    }

    // =========================================
    // Single-Threaded Edge Case Tests
    // =========================================

    /** Setup: Timer with voiceIndex=nullptr (no PolyHandler).
	 *
	 *  Scenario: Call setActiveVoiceForUIUpdates(), then shouldUpdateForCurrentVoice().
	 *
	 *  Expected: No crash, shouldUpdateForCurrentVoice() returns true. Voice tracking
	 *  is silently disabled when no PolyHandler is available.
	 */
    void testSetActiveVoiceWithNullPolyHandler()
    {
        beginTest ("setActiveVoiceForUIUpdates with null PolyHandler");

        struct TestTimer : public NodeUITimer
        {
            void onUITimerCallback() override {}

            using NodeUIUpdaterBase::shouldUpdateForCurrentVoice;
        };

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = nullptr; // No PolyHandler

        TestTimer timer;
        timer.setupUIUpdater (specs);

        // Should not crash when voiceIndex is nullptr
        timer.setActiveVoiceForUIUpdates();

        // shouldUpdateForCurrentVoice should return true (no tracking possible)
        expect (timer.shouldUpdateForCurrentVoice(), "Should always update when no PolyHandler");
    }

    /** Setup: AsyncUpdater with invalid PrepareSpecs (sampleRate=0, blockSize=0).
	 *
	 *  Scenario: Call setEnableRateLimiting(true, 30.0, 1), inspect threshold.
	 *
	 *  Expected: Threshold falls back to 1 (no rate limiting), avoiding division
	 *  by zero in the threshold calculation.
	 */
    void testRateLimitingWithInvalidSpecs()
    {
        beginTest ("Rate limiting with invalid specs (zero sampleRate/blockSize)");

        struct TestAsyncUpdater : public NodeUIAsyncUpdater
        {
            void onUIAsyncUpdate() override {}

            using NodeUIAsyncUpdater::getUpdateThreshold;
        };

        TestAsyncUpdater updater;
        PrepareSpecs specs;
        specs.sampleRate = 0.0; // Invalid
        specs.blockSize = 0;    // Invalid
        specs.numChannels = 1;
        specs.voiceIndex = nullptr;

        updater.setupUIUpdater (specs);
        updater.setEnableRateLimiting (true, 30.0, 1);

        // Should fall back to threshold of 1 (no rate limiting effectively)
        int threshold = updater.getUpdateThreshold();
        expectEquals (threshold, 1, "Threshold should be 1 with invalid specs");
    }

    /** Setup: AsyncUpdater with valid specs, rate limiting NOT enabled (default).
	 *
	 *  Scenario: Call triggerAsyncUpdate() three times, inspect threshold and counter.
	 *
	 *  Expected: Threshold=1, counter stays at 0 (not used when rate limiting disabled),
	 *  every trigger immediately queues an update.
	 */
    void testTriggerAsyncUpdateNoRateLimiting()
    {
        beginTest ("triggerAsyncUpdate without rate limiting");

        struct TestAsyncUpdater : public NodeUIAsyncUpdater
        {
            void onUIAsyncUpdate() override {}

            using NodeUIAsyncUpdater::getRateLimitCounter;
            using NodeUIAsyncUpdater::getUpdateThreshold;
        };

        TestAsyncUpdater updater;
        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = nullptr;

        updater.setupUIUpdater (specs);
        // Don't call setEnableRateLimiting - rate limiting is disabled by default

        // Threshold should be 1 (every trigger causes update)
        expectEquals (updater.getUpdateThreshold(), 1, "Threshold should be 1 when rate limiting disabled");

        // Counter should stay at 0 (not used when rate limiting disabled)
        updater.triggerAsyncUpdate();
        updater.triggerAsyncUpdate();
        updater.triggerAsyncUpdate();

        // Rate limit counter is not incremented when rate limiting is disabled
        expectEquals (updater.getRateLimitCounter(), 0, "Counter should be 0 when rate limiting disabled");

        updater.cancelPendingUpdate();
    }

    /** Setup: Timer with voiceIndex=nullptr (no PolyHandler).
	 *
	 *  Scenario: Call callWithVoiceProtection() with a lambda that sets a flag.
	 *
	 *  Expected: Lambda executes (flag is set), no crash. When PolyHandler is null,
	 *  the callback runs without ScopedAllVoiceSetter wrapping.
	 */
    void testCallWithVoiceProtectionNullHandler()
    {
        beginTest ("callWithVoiceProtection with null PolyHandler");

        struct TestTimer : public NodeUITimer
        {
            void onUITimerCallback() override {}

            // Expose callWithVoiceProtection for testing (non-template for MSVC local class)
            void testCallWithVoiceProtection (std::function<void()> f)
            {
                callWithVoiceProtection (f);
            }
        };

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = nullptr; // No PolyHandler

        TestTimer timer;
        timer.setupUIUpdater (specs);

        // Should not crash and should still call the callback
        bool callbackCalled = false;
        timer.testCallWithVoiceProtection ([&]()
                                           {
                                               callbackCalled = true;
                                           });

        expect (callbackCalled, "Callback should be called even with null PolyHandler");
    }

    /** Setup: Timer with valid PrepareSpecs, not started.
	 *
	 *  Scenario: Check isTimerRunning(), call startTimerHz(30), check again,
	 *  call stopTimer(), check again, call startTimer(100), check, stop.
	 *
	 *  Expected: isTimerRunning() correctly reflects timer state through all transitions.
	 */
    void testTimerStartStopRunning()
    {
        beginTest ("Timer start/stop/isTimerRunning");

        struct TestTimer : public NodeUITimer
        {
            void onUITimerCallback() override {}
        };

        TestTimer timer;
        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = nullptr;

        timer.setupUIUpdater (specs);

        // Initially not running
        expect (! timer.isTimerRunning(), "Timer should not be running initially");

        // Start timer
        timer.startTimerHz (30);
        expect (timer.isTimerRunning(), "Timer should be running after startTimerHz");

        // Stop timer
        timer.stopTimer();
        expect (! timer.isTimerRunning(), "Timer should not be running after stopTimer");

        // Start with ms
        timer.startTimer (100);
        expect (timer.isTimerRunning(), "Timer should be running after startTimer");

        // Stop again
        timer.stopTimer();
        expect (! timer.isTimerRunning(), "Timer should not be running after second stopTimer");
    }

    // =========================================
    // Multi-Threaded Tests
    // =========================================

    /** Setup: AsyncUpdater with PolyHandler, no rate limiting.
	 *
	 *  Scenario: Audio thread triggers 30 async updates, UI thread counts callbacks.
	 *
	 *  Expected: Test completes, UI receives callbacks. Baseline test proving
	 *  async updates flow from audio thread to UI thread.
	 */
    void testAsyncUpdateUnderLoad()
    {
        beginTest ("AsyncUpdater basic flow under load");

        setupAsyncTest();

        std::atomic<int> audioCallbacks { 0 };
        std::atomic<int> uiCallbacks { 0 };

        auto result = runThreadedTest (
            AudioThread ([&]()
                         {
                             PolyHandler::ScopedVoiceSetter svs (polyHandler, 0);
                             asyncNode->triggerAsyncUpdate();
                             audioCallbacks++;
                         }),
            UIThread ([&]()
                      {
                          uiCallbacks++;
                      }),
            StopWhen ([&]()
                      {
                          return audioCallbacks >= 30;
                      }));

        expect (result.completed, "Should complete without timeout");
        expectEquals (audioCallbacks.load(), 30, "Should execute 30 audio callbacks");
        expect (uiCallbacks.load() > 0, "Should have received UI callbacks");
    }

    /** Setup: AsyncUpdater with PolyHandler, rate limiting at 30fps (threshold=2).
	 *
	 *  Scenario: Audio thread triggers 50 async updates, UI thread counts callbacks.
	 *
	 *  Expected: UI callbacks significantly fewer than 50 (~25 with threshold=2),
	 *  proving rate limiting reduces callback frequency.
	 */
    void testAsyncUpdateRateLimitedUnderLoad()
    {
        beginTest ("AsyncUpdater rate-limited under load");

        setupAsyncTest (true, 30.0, 1); // Enable rate limiting at 30fps

        std::atomic<int> audioCallbacks { 0 };
        std::atomic<int> uiCallbacks { 0 };

        auto result = runThreadedTest (
            AudioThread ([&]()
                         {
                             PolyHandler::ScopedVoiceSetter svs (polyHandler, 0);
                             asyncNode->triggerAsyncUpdate();
                             audioCallbacks++;
                         }),
            UIThread ([&]()
                      {
                          uiCallbacks++;
                      }),
            StopWhen ([&]()
                      {
                          return audioCallbacks >= 50;
                      }));

        expect (result.completed, "Should complete without timeout");
        expectEquals (audioCallbacks.load(), 50, "Should execute 50 audio callbacks");
        // With rate limiting, UI callbacks should be significantly fewer than audio callbacks
        // Threshold is 2, so expect ~25 UI callbacks (50 / 2), with some tolerance
        expectWithinRange (uiCallbacks.load(), 10, 40, "UI callbacks should be rate-limited");
    }

    /** Setup: AsyncUpdater with PolyHandler, rate limiting disabled.
	 *
	 *  Scenario: Audio thread triggers 30 async updates, UI thread counts callbacks.
	 *
	 *  Expected: UI receives callbacks (may be coalesced by juce::AsyncUpdater).
	 *  Verifies the non-rate-limited code path works under load.
	 */
    void testAsyncUpdateNoRateLimitingUnderLoad()
    {
        beginTest ("AsyncUpdater without rate limiting under load");

        setupAsyncTest (false); // No rate limiting

        std::atomic<int> audioCallbacks { 0 };
        std::atomic<int> uiCallbacks { 0 };

        auto result = runThreadedTest (
            AudioThread ([&]()
                         {
                             PolyHandler::ScopedVoiceSetter svs (polyHandler, 0);
                             asyncNode->triggerAsyncUpdate();
                             audioCallbacks++;
                         }),
            UIThread ([&]()
                      {
                          uiCallbacks++;
                      }),
            StopWhen ([&]()
                      {
                          return audioCallbacks >= 30;
                      }));

        expect (result.completed, "Should complete without timeout");
        expectEquals (audioCallbacks.load(), 30, "Should execute 30 audio callbacks");
        // Without rate limiting, every trigger should cause a callback (async coalescing may reduce)
        expect (uiCallbacks.load() > 0, "Should have UI callbacks");
    }

    /** Setup: Timer at 30Hz with PolyHandler.
	 *
	 *  Scenario: Audio thread runs ~20 callbacks (~230ms), timer fires independently.
	 *
	 *  Expected: Timer callbacks fire (count > 0) while audio thread runs concurrently,
	 *  proving timer operates independently of audio thread activity.
	 */
    void testTimerUnderLoad()
    {
        beginTest ("Timer callbacks under load");

        setupTimerTest (30); // 30 Hz

        std::atomic<int> audioCallbacks { 0 };
        std::atomic<int> uiCallbacks { 0 };

        timerNode->onCallback = [&]()
        {
            uiCallbacks++;
        };

        auto result = runThreadedTestWithExistingCallback (
            AudioThread ([&]()
                         {
                             // Audio thread just runs, timer fires independently
                             audioCallbacks++;
                         }),
            StopWhen ([&]()
                      {
                          // Run for at least 200ms to get ~6 timer callbacks at 30Hz
                          return audioCallbacks >= 20;
                      }));

        expect (result.completed, "Should complete without timeout");
        expect (uiCallbacks.load() > 0, "Should have received timer callbacks");
    }

    /** Setup: AsyncUpdater with PolyHandler, no rate limiting.
	 *
	 *  Scenario: Audio thread sets voice index to 0 via ScopedVoiceSetter and triggers
	 *  updates. UI thread checks polyHandler.isAllThread() inside the callback to verify
	 *  that ScopedAllVoiceSetter was applied by callWithVoiceProtection().
	 *
	 *  Expected: isAllThread() returns true during UI callback, proving that PolyData
	 *  iteration would cover all voices (not just voice 0 that the audio thread set).
	 */
    void testVoiceProtectionUnderLoad()
    {
        beginTest ("Voice protection active during UI callbacks");

        setupAsyncTest();

        std::atomic<int> audioCallbacks { 0 };
        std::atomic<int> uiCallbacks { 0 };
        std::atomic<bool> voiceProtectionActive { false };

        auto result = runThreadedTest (
            AudioThread ([&]()
                         {
                             PolyHandler::ScopedVoiceSetter svs (polyHandler, 0);
                             asyncNode->triggerAsyncUpdate();
                             audioCallbacks++;
                         }),
            UIThread ([&]()
                      {
                          // Check if voice protection is active (isAllThread should be true)
                          if (polyHandler.isAllThread())
                              voiceProtectionActive = true;
                          uiCallbacks++;
                      }),
            StopWhen ([&]()
                      {
                          return audioCallbacks >= 20;
                      }));

        expect (result.completed, "Should complete without timeout");
        expect (voiceProtectionActive.load(), "ScopedAllVoiceSetter should be active during UI callback");
    }

    /** Setup: AsyncUpdater with PolyHandler, voice tracking enabled for voice 0.
	 *
	 *  Scenario: Audio thread alternates between voice 0 and voice 1, triggering
	 *  async updates from both. UI thread counts callbacks.
	 *
	 *  Expected: UI callbacks fire (from voice 0 triggers), proving voice tracking
	 *  filters correctly under concurrent access. Voice 1 triggers are blocked.
	 */
    void testVoiceTrackingUnderLoad()
    {
        beginTest ("Voice tracking filters updates under load");

        setupAsyncTest();

        // Track voice 0
        {
            PolyHandler::ScopedVoiceSetter svs (polyHandler, 0);
            asyncNode->setActiveVoiceForUIUpdates();
        }

        std::atomic<int> voice0Triggers { 0 };
        std::atomic<int> voice1Triggers { 0 };
        std::atomic<int> uiCallbacks { 0 };

        auto result = runThreadedTest (
            AudioThread ([&]()
                         {
                             // Alternate between voice 0 and voice 1
                             int currentCount = voice0Triggers.load() + voice1Triggers.load();
                             if (currentCount % 2 == 0)
                             {
                                 PolyHandler::ScopedVoiceSetter svs (polyHandler, 0);
                                 asyncNode->triggerAsyncUpdate(); // Should trigger (tracked voice)
                                 voice0Triggers++;
                             }
                             else
                             {
                                 PolyHandler::ScopedVoiceSetter svs (polyHandler, 1);
                                 asyncNode->triggerAsyncUpdate(); // Should be filtered (not tracked)
                                 voice1Triggers++;
                             }
                         }),
            UIThread ([&]()
                      {
                          uiCallbacks++;
                      }),
            StopWhen ([&]()
                      {
                          return voice0Triggers.load() + voice1Triggers.load() >= 40;
                      }));

        expect (result.completed, "Should complete without timeout");
        // Voice 1 triggers should be filtered, but voice 0 should still cause callbacks
        expect (uiCallbacks.load() > 0, "Should have UI callbacks from voice 0");
    }

    /** Setup: Timer::WithData at 60Hz with PolyHandler.
	 *
	 *  Scenario: Audio thread posts increasing sequence numbers (1, 2, 3...).
	 *  UI thread consumes and verifies sequence never decreases.
	 *
	 *  Expected: No sequence regression detected, proving lock-free data exchange
	 *  maintains ordering under concurrent audio/UI access.
	 */
    void testWithDataTimerUnderLoad()
    {
        beginTest ("WithData Timer data integrity under load");

        setupTimerTest (60); // 60 Hz for more callbacks

        std::atomic<int> audioCallbacks { 0 };
        std::atomic<int> lastReceivedSequence { 0 };
        std::atomic<bool> dataCorrupted { false };

        timerNode->onCallback = [&]()
        {
            TestData data;
            if (timerNode->consumeData (data))
            {
                int lastSeq = lastReceivedSequence.load();
                // Sequence should only increase (or be same if no new data)
                if (data.sequence < lastSeq)
                    dataCorrupted = true;
                lastReceivedSequence = data.sequence;
            }
        };

        auto result = runThreadedTestWithExistingCallback (
            AudioThread ([&]()
                         {
                             int seq = ++audioCallbacks;
                             timerNode->postData ({ seq, (float) seq * 0.1f, 0 });
                         }),
            StopWhen ([&]()
                      {
                          return audioCallbacks >= 40;
                      }));

        expect (result.completed, "Should complete without timeout");
        expect (! dataCorrupted.load(), "Data sequence should never go backwards");
        expect (lastReceivedSequence.load() > 0, "Should have received some data");
    }

    /** Setup: AsyncUpdater::WithData with PolyHandler.
	 *
	 *  Scenario: Audio thread posts increasing sequence numbers and triggers updates.
	 *  UI thread consumes and verifies sequence never decreases.
	 *
	 *  Expected: No sequence regression, same as Timer variant but using async
	 *  updates instead of timer callbacks.
	 */
    void testWithDataAsyncUnderLoad()
    {

        beginTest ("WithData AsyncUpdater data integrity under load");
        
#if JUCE_WINDOWS

        // Create a fresh async node with data
        struct TestAsyncWithData : public NodeUIAsyncUpdater::WithData<TestData>
        {
            std::function<void()> onCallback;

            void onUIAsyncUpdate() override
            {
                if (onCallback)
                    onCallback();
            }
        };

        juce::ScopedPointer<TestAsyncWithData> asyncDataNode = new TestAsyncWithData();
        asyncDataNode->setupUIUpdater (createDefaultSpecs());

        std::atomic<int> audioCallbacks { 0 };
        std::atomic<int> lastReceivedSequence { 0 };
        std::atomic<bool> dataCorrupted { false };

        asyncDataNode->onCallback = [&]()
        {
            TestData data;
            if (asyncDataNode->consumeData (data))
            {
                int lastSeq = lastReceivedSequence.load();
                if (data.sequence < lastSeq)
                    dataCorrupted = true;
                lastReceivedSequence = data.sequence;
            }
        };

        activeCallback = &asyncDataNode->onCallback;

        MockAudioThread mockAudio (kSampleRate, kBlockSize, kTimeoutMs);
        auto result = mockAudio.run (
            [&]()
            {
                PolyHandler::ScopedVoiceSetter svs (polyHandler, 0);
                int seq = ++audioCallbacks;
                asyncDataNode->postData ({ seq, (float) seq * 0.1f, 0 });
                asyncDataNode->triggerAsyncUpdate();
            },
            [&]()
            {
                return audioCallbacks >= 40;
            });

        expect (result.completed, "Should complete without timeout");
        expect (! dataCorrupted.load(), "Data sequence should never go backwards");
        expect (lastReceivedSequence.load() > 0, "Should have received some data");

        // Prevent use-after-free: the audio thread may have posted a juce::AsyncUpdater
        // message on its last iteration that hasn't been dispatched yet. Null the callback
        // first (so any straggling dispatch is a no-op), cancel the pending message, then
        // drain the message queue before destroying the object.
        asyncDataNode->onCallback = nullptr;
        asyncDataNode->cancelPendingUpdate();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        asyncDataNode = nullptr;
#else
        expect(true, "Skip this on macOS");
#endif
    }

    /** Setup: Timer::WithData with PolyHandler, voice tracking enabled for voice 0.
	 *
	 *  Scenario: Post data from voice 1 (non-tracked), then from voice 0 (tracked).
	 *  Consume after each post.
	 *
	 *  Expected: Voice 1 post blocked (consumeData returns false), voice 0 post
	 *  succeeds (consumeData returns true with correct data).
	 */
    void testVoiceTrackingBlocksPostData()
    {
        beginTest ("Voice tracking blocks postData for non-tracked voices");

        struct TestTimerWithData : public NodeUITimer::WithData<TestData>
        {
            void onUITimerCallback() override {}
        };

        PolyHandler localPolyHandler (true);

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = &localPolyHandler;

        TestTimerWithData timer;
        timer.setupUIUpdater (specs);

        // Track voice 0
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 0);
            timer.setActiveVoiceForUIUpdates();
        }

        // Post from voice 1 (should be blocked)
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 1);
            timer.postData ({ 100, 1.0f, 1 });
        }

        // Consume - should have no data
        TestData received;
        bool hasData = timer.consumeData (received);
        expect (! hasData, "postData from non-tracked voice should be blocked");

        // Post from voice 0 (should succeed)
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 0);
            timer.postData ({ 200, 2.0f, 0 });
        }

        // Consume - should have data
        hasData = timer.consumeData (received);
        expect (hasData, "postData from tracked voice should succeed");
        expectEquals (received.sequence, 200, "Should receive data from tracked voice");
    }

    /** Setup: AsyncUpdater with PolyHandler, voice tracking enabled for voice 0.
	 *
	 *  Scenario: Trigger async update from voice 1 (non-tracked), pump message loop,
	 *  check callback count. Then trigger from voice 0 (tracked) and check again.
	 *
	 *  Expected: Voice 1 trigger produces no callback, voice 0 trigger produces
	 *  callback, proving triggerAsyncUpdate() respects voice tracking.
	 */
    void testVoiceTrackingBlocksTrigger()
    {
        beginTest ("Voice tracking blocks triggerAsyncUpdate for non-tracked voices");

        struct TestAsyncUpdater : public NodeUIAsyncUpdater
        {
            std::atomic<int> callbackCount { 0 };

            void onUIAsyncUpdate() override { callbackCount++; }
        };

        PolyHandler localPolyHandler (true);

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 1;
        specs.voiceIndex = &localPolyHandler;

        TestAsyncUpdater updater;
        updater.setupUIUpdater (specs);

        // Track voice 0
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 0);
            updater.setActiveVoiceForUIUpdates();
        }

        // Trigger from voice 1 (should be blocked)
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 1);
            updater.triggerAsyncUpdate();
        }

        // Pump message loop
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        expectEquals (updater.callbackCount.load(), 0, "triggerAsyncUpdate from non-tracked voice should be blocked");

        // Trigger from voice 0 (should succeed)
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 0);
            updater.triggerAsyncUpdate();
        }

        // Pump message loop
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        expect (updater.callbackCount.load() > 0, "triggerAsyncUpdate from tracked voice should succeed");
    }

    // =========================================
    // PolyData Tests - Construction & Setup
    // =========================================

    /** Setup: Default-construct PolyData<int, NUM_POLYPHONIC_VOICES>.
	 *
	 *  Scenario: Check that arithmetic types are zero-initialized.
	 *
	 *  Expected: All voice slots contain 0.
	 */
    void testPolyDataDefaultConstruction()
    {
        beginTest ("PolyData default construction zeros arithmetic types");

        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        // Access raw data via getFirst() and getWithIndex()
        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
        {
            expectEquals (polyData.getWithIndex (i), 0, "Voice " + juce::String (i) + " should be zero");
        }
    }

    /** Setup: Construct PolyData<float, NUM_POLYPHONIC_VOICES> with initial value 3.14f.
	 *
	 *  Scenario: Check all voice slots.
	 *
	 *  Expected: All voice slots contain 3.14f.
	 */
    void testPolyDataValueConstruction()
    {
        beginTest ("PolyData value construction sets all voices");

        PolyData<float, NUM_POLYPHONIC_VOICES> polyData (3.14f);

        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
        {
            expectEquals (polyData.getWithIndex (i), 3.14f, "Voice " + juce::String (i) + " should be 3.14f");
        }
    }

    /** Setup: Create PolyData, call prepare() with valid PrepareSpecs.
	 *
	 *  Scenario: Check that isPolyHandlerEnabled() returns true after prepare.
	 *
	 *  Expected: isPolyHandlerEnabled() == true when voiceIndex != nullptr and enabled.
	 */
    void testPolyDataPrepare()
    {
        beginTest ("PolyData prepare stores voicePtr correctly");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        // Before prepare, handler not connected
        expect (! polyData.isPolyHandlerEnabled(), "Should not be enabled before prepare");

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;

        polyData.prepare (specs);

        expect (polyData.isPolyHandlerEnabled(), "Should be enabled after prepare with valid PolyHandler");
    }

    /** Setup: PolyData with PolyHandler, call setAll() in different contexts.
	 *
	 *  Scenario: 
	 *  1. With voiceIndex=-1 (no ScopedVoiceSetter), setAll() affects all voices
	 *  2. With ScopedVoiceSetter(5), setAll() only affects voice 5
	 *
	 *  Expected: setAll() respects the current voice context.
	 */
    void testPolyDataSetAll()
    {
        beginTest ("PolyData setAll respects voice context");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        // Set all voices to 100 (no voice context, voiceIndex = -1)
        polyData.setAll (100);

        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
        {
            expectEquals (polyData.getWithIndex (i), 100, "All voices should be 100");
        }

        // Now set only voice 5 to 200
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);
            polyData.setAll (200);
        }

        // Voice 5 should be 200, others should still be 100
        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
        {
            if (i == 5)
                expectEquals (polyData.getWithIndex (i), 200, "Voice 5 should be 200");
            else
                expectEquals (polyData.getWithIndex (i), 100, "Voice " + juce::String (i) + " should still be 100");
        }
    }

    // =========================================
    // PolyData Tests - Iteration
    // =========================================

    /** Setup: PolyData with PolyHandler, ScopedVoiceSetter(5) active.
	 *
	 *  Scenario: Check begin() and end() pointers.
	 *
	 *  Expected: begin() points to voice 5, end() - begin() == 1.
	 */
    void testPolyDataBeginEndSingleVoice()
    {
        beginTest ("PolyData begin/end span single voice when ScopedVoiceSetter active");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        // Initialize each voice with its index
        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i) = i;

        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);

            auto* b = polyData.begin();
            auto* e = polyData.end();

            expectEquals ((int) (e - b), 1, "Should iterate exactly 1 element");
            expectEquals (*b, 5, "begin() should point to voice 5 data");
        }
    }

    /** Setup: PolyData with PolyHandler, no ScopedVoiceSetter (voiceIndex = -1).
	 *
	 *  Scenario: Check begin() and end() pointers.
	 *
	 *  Expected: end() - begin() == NUM_POLYPHONIC_VOICES.
	 */
    void testPolyDataBeginEndAllVoices()
    {
        beginTest ("PolyData begin/end span all voices when voiceIndex=-1");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        // No ScopedVoiceSetter, voiceIndex defaults to -1
        auto* b = polyData.begin();
        auto* e = polyData.end();

        expectEquals ((int) (e - b), NUM_POLYPHONIC_VOICES, "Should iterate all voices");
    }

    /** Setup: PolyData with PolyHandler, ScopedVoiceSetter(7) active.
	 *
	 *  Scenario: Use range-based for loop to count iterations and sum values.
	 *
	 *  Expected: Loop executes exactly once, accessing voice 7.
	 */
    void testPolyDataRangeForSingleVoice()
    {
        beginTest ("PolyData range-for iterates single voice when ScopedVoiceSetter active");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        // Initialize each voice with its index
        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i) = i;

        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 7);

            int count = 0;
            int sum = 0;
            for (auto& v : polyData)
            {
                count++;
                sum += v;
            }

            expectEquals (count, 1, "Should iterate exactly once");
            expectEquals (sum, 7, "Should access voice 7 (value=7)");
        }
    }

    /** Setup: PolyData with PolyHandler, no ScopedVoiceSetter.
	 *
	 *  Scenario: Use range-based for loop to count iterations.
	 *
	 *  Expected: Loop executes NUM_POLYPHONIC_VOICES times.
	 */
    void testPolyDataRangeForAllVoices()
    {
        beginTest ("PolyData range-for iterates all voices when voiceIndex=-1");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        // Initialize each voice with its index
        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i) = i;

        // No ScopedVoiceSetter
        int count = 0;
        int sum = 0;
        for (auto& v : polyData)
        {
            count++;
            sum += v;
        }

        expectEquals (count, NUM_POLYPHONIC_VOICES, "Should iterate all voices");
        // Sum of 0..255 = 255*256/2 = 32640
        int expectedSum = (NUM_POLYPHONIC_VOICES - 1) * NUM_POLYPHONIC_VOICES / 2;
        expectEquals (sum, expectedSum, "Sum should be 0+1+2+...+255");
    }

    // =========================================
    // PolyData Tests - get()
    // =========================================

    /** Setup: PolyData with PolyHandler, ScopedVoiceSetter(10) active.
	 *
	 *  Scenario: Call get() and verify it returns voice 10's data.
	 *
	 *  Expected: get() returns reference to voice 10 slot.
	 */
    void testPolyDataGetReturnsCurrentVoice()
    {
        beginTest ("PolyData get() returns current voice data");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        // Initialize each voice with its index * 10
        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i) = i * 10;

        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 10);
            expectEquals (polyData.get(), 100, "get() should return voice 10 data (100)");
        }

        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 0);
            expectEquals (polyData.get(), 0, "get() should return voice 0 data (0)");
        }
    }

    // =========================================
    // PolyData Tests - Iteration with Voice Context
    // =========================================

    /** Setup: PolyData with PolyHandler, ScopedVoiceSetter(5) active.
	 *
	 *  Scenario: Use range-based for loop to iterate and count.
	 *
	 *  Expected: Loop executes once, processing voice 5.
	 */
    void testPolyDataIterateSingleVoice()
    {
        beginTest ("PolyData range-for processes single voice with ScopedVoiceSetter");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i) = i;

        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);

            int count = 0;
            int sum = 0;
            for (auto& v : polyData)
            {
                count++;
                sum += v;
            }

            expectEquals (count, 1, "Should iterate once");
            expectEquals (sum, 5, "Should process voice 5 (value=5)");
        }
    }

    /** Setup: PolyData with PolyHandler, ScopedAllVoiceSetter active.
	 *
	 *  Scenario: Use range-based for loop to iterate all voices.
	 *
	 *  Expected: Loop executes NUM_POLYPHONIC_VOICES times.
	 */
    void testPolyDataIterateAllVoices()
    {
        beginTest ("PolyData range-for iterates all voices with ScopedAllVoiceSetter");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i) = 1;

        {
            PolyHandler::ScopedAllVoiceSetter avs (localPolyHandler);

            int count = 0;
            for (auto& v : polyData)
            {
                count++;
                ignoreUnused (v);
            }

            expectEquals (count, NUM_POLYPHONIC_VOICES, "Should iterate all voices");
        }
    }

    // =========================================
    // PolyData Tests - all()
    // =========================================

    /** Setup: PolyData with PolyHandler, ScopedVoiceSetter(5) active.
	 *
	 *  Scenario: Call all() and iterate, counting elements.
	 *
	 *  Expected: all() always iterates ALL voices regardless of voice context.
	 */
    void testPolyDataAllReturnsAllVoices()
    {
        beginTest ("PolyData all() always iterates all voices");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i) = i;

        // With ScopedVoiceSetter active, all() should STILL iterate all voices
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);

            int count = 0;
            int sum = 0;
            for (auto& v : polyData.all())
            {
                count++;
                sum += v;
            }

            expectEquals (count, NUM_POLYPHONIC_VOICES, "all() should iterate all voices");
            int expectedSum = (NUM_POLYPHONIC_VOICES - 1) * NUM_POLYPHONIC_VOICES / 2;
            expectEquals (sum, expectedSum, "all() should access all voice data");
        }
    }

    /** Setup: PolyData with PolyHandler, all() used for clearing state.
	 *
	 *  Scenario: Use all() to set all voices to 0 while inside a single voice context.
	 *
	 *  Expected: All voices are cleared regardless of voice context.
	 */
    void testPolyDataAllIgnoresVoiceContext()
    {
        beginTest ("PolyData all() ignores voice context for operations like clearing");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        // Initialize all voices to non-zero
        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i) = 100 + i;

        // Clear all voices while inside voice 5 context
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);

            for (auto& v : polyData.all())
                v = 0;
        }

        // Verify ALL voices were cleared
        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
        {
            expectEquals (polyData.getWithIndex (i), 0, "Voice " + juce::String (i) + " should be cleared by all()");
        }
    }

    // =========================================
    // PolyData Tests - Utility Methods
    // =========================================

    /** Setup: PolyData with PolyHandler, check isFirst() for different voices.
	 *
	 *  Scenario: Set voice 0, check isFirst(). Set voice 5, check isFirst().
	 *
	 *  Expected: isFirst() returns true only for voice 0.
	 */
    void testPolyDataIsFirst()
    {
        beginTest ("PolyData isFirst() returns true only for voice 0");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 0);
            expect (polyData.isFirst(), "isFirst() should return true for voice 0");
        }

        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);
            expect (! polyData.isFirst(), "isFirst() should return false for voice 5");
        }

        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, NUM_POLYPHONIC_VOICES - 1);
            expect (! polyData.isFirst(), "isFirst() should return false for last voice");
        }
    }

    /** Setup: PolyData with values, check getFirst().
	 *
	 *  Scenario: Set voice 0 to 999, other voices to different values.
	 *
	 *  Expected: getFirst() always returns voice 0 data regardless of context.
	 */
    void testPolyDataGetFirst()
    {
        beginTest ("PolyData getFirst() always returns voice 0 data");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        polyData.getWithIndex (0) = 999;
        polyData.getWithIndex (5) = 555;

        // getFirst() should return voice 0 regardless of current voice context
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);
            expectEquals (polyData.getFirst(), 999, "getFirst() should return voice 0 data even when voice 5 active");
        }

        // Also works without any voice context
        expectEquals (polyData.getFirst(), 999, "getFirst() should return voice 0 data");
    }

    /** Setup: PolyData, get reference via getWithIndex(), then call getVoiceIndexForData().
	 *
	 *  Scenario: For several voice indices, verify round-trip works.
	 *
	 *  Expected: getVoiceIndexForData() returns the original voice index.
	 */
    void testPolyDataGetVoiceIndexForData()
    {
        beginTest ("PolyData getVoiceIndexForData returns correct index");

        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        // Test several indices
        int testIndices[] = { 0, 1, 5, 10, 100, NUM_POLYPHONIC_VOICES - 1 };
        for (int idx : testIndices)
        {
            int& ref = polyData.getWithIndex (idx);
            int computed = polyData.getVoiceIndexForData (ref);
            expectEquals (computed, idx, "getVoiceIndexForData should return " + juce::String (idx));
        }
    }

    /** Setup: PolyData with values, call getWithIndex() for different indices.
	 *
	 *  Scenario: Initialize each voice with unique value, retrieve via getWithIndex().
	 *
	 *  Expected: getWithIndex(i) returns correct slot regardless of voice context.
	 */
    void testPolyDataGetWithIndex()
    {
        beginTest ("PolyData getWithIndex returns correct slot");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        // Initialize
        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i) = i * 11;

        // Verify - even with different voice context
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);

            expectEquals (polyData.getWithIndex (0), 0, "getWithIndex(0) should return 0");
            expectEquals (polyData.getWithIndex (5), 55, "getWithIndex(5) should return 55");
            expectEquals (polyData.getWithIndex (10), 110, "getWithIndex(10) should return 110");
        }
    }

    /** Setup: PolyData with/without PolyHandler, check isMonophonicOrInsideVoiceRendering().
	 *
	 *  Scenario: Test various configurations.
	 *
	 *  Expected: Returns true when monophonic, voicePtr null, or voiceIndex >= 0.
	 */
    void testPolyDataIsMonophonicOrInsideVoiceRendering()
    {
        beginTest ("PolyData isMonophonicOrInsideVoiceRendering");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        // Before prepare (voicePtr null) - should return true
        expect (polyData.isMonophonicOrInsideVoiceRendering(), "Should return true when voicePtr is null");

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        // After prepare, no voice context (voiceIndex = -1) - should return false
        expect (! polyData.isMonophonicOrInsideVoiceRendering(), "Should return false when voiceIndex=-1");

        // With voice context - should return true
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);
            expect (polyData.isMonophonicOrInsideVoiceRendering(), "Should return true when inside voice rendering");
        }
    }

    /** Setup: PolyData with PolyHandler, check isVoiceRenderingActive().
	 *
	 *  Scenario: Check before and after ScopedVoiceSetter.
	 *
	 *  Expected: Returns true only when voiceIndex >= 0.
	 */
    void testPolyDataIsVoiceRenderingActive()
    {
        beginTest ("PolyData isVoiceRenderingActive");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        // No voice context - should return false
        expect (! polyData.isVoiceRenderingActive(), "Should return false when voiceIndex=-1");

        // With voice context - should return true
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);
            expect (polyData.isVoiceRenderingActive(), "Should return true when voiceIndex=5");
        }

        // After ScopedVoiceSetter destroyed - should return false again
        expect (! polyData.isVoiceRenderingActive(), "Should return false after ScopedVoiceSetter destroyed");
    }

    /** Setup: PolyData with various configurations, check isPolyHandlerEnabled().
	 *
	 *  Scenario: Test null handler, disabled handler, enabled handler.
	 *
	 *  Expected: Returns true only when NumVoices > 1, voicePtr != null, and handler enabled.
	 */
    void testPolyDataIsPolyHandlerEnabled()
    {
        beginTest ("PolyData isPolyHandlerEnabled");

        PolyHandler enabledHandler (true);
        PolyHandler disabledHandler (false);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        // Before prepare - should return false
        expect (! polyData.isPolyHandlerEnabled(), "Should return false when voicePtr is null");

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;

        // With enabled handler
        specs.voiceIndex = &enabledHandler;
        polyData.prepare (specs);
        expect (polyData.isPolyHandlerEnabled(), "Should return true with enabled handler");

        // With disabled handler
        specs.voiceIndex = &disabledHandler;
        polyData.prepare (specs);
        expect (! polyData.isPolyHandlerEnabled(), "Should return false with disabled handler");
    }

    // =========================================
    // PolyData + WithData Integration Tests
    // =========================================

    /** Setup: Timer::WithData using PolyData to store per-voice values.
	 *
	 *  Scenario: Audio thread uses get() to read current voice, posts via postData().
	 *  UI thread consumes.
	 *
	 *  Expected: UI receives the value from the active voice.
	 */
    void testWithDataPostsFromPolyDataGet()
    {
        beginTest ("WithData posts value from PolyData.get()");

        struct TestTimerWithPolyData : public NodeUITimer::WithData<TestData>
        {
            PolyData<int, NUM_POLYPHONIC_VOICES> voiceValues;

            void onUITimerCallback() override {}
        };

        PolyHandler localPolyHandler (true);
        TestTimerWithPolyData timer;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;

        timer.setupUIUpdater (specs);
        timer.voiceValues.prepare (specs);

        // Initialize each voice with unique value
        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            timer.voiceValues.getWithIndex (i) = i * 100;

        // "Audio thread" posts value from voice 7
        {
            PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 7);
            int val = timer.voiceValues.get();
            timer.postData ({ 0, (float) val, 7 });
        }

        // UI thread consumes
        TestData received;
        bool hasData = timer.consumeData (received);

        expect (hasData, "Should have data");
        expectEquals (received.value, 700.0f, "Should receive voice 7's value (700)");
        expectEquals (received.voiceIndex, 7, "voiceIndex should be 7");
    }

    /** Setup: Timer::WithData with PolyData, aggregate all voices.
	 *
	 *  Scenario: With ScopedAllVoiceSetter, iterate all voices, compute sum, post.
	 *
	 *  Expected: UI receives the sum of all voice values.
	 */
    void testWithDataAggregatesPolyDataVoices()
    {
        beginTest ("WithData aggregates all PolyData voices");

        struct TestTimerWithPolyData : public NodeUITimer::WithData<TestData>
        {
            PolyData<int, NUM_POLYPHONIC_VOICES> voiceValues;

            void onUITimerCallback() override {}
        };

        PolyHandler localPolyHandler (true);
        TestTimerWithPolyData timer;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;

        timer.setupUIUpdater (specs);
        timer.voiceValues.prepare (specs);

        // Initialize each voice with 1
        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            timer.voiceValues.getWithIndex (i) = 1;

        // Aggregate with ScopedAllVoiceSetter
        {
            PolyHandler::ScopedAllVoiceSetter avs (localPolyHandler);

            int sum = 0;
            for (auto& v : timer.voiceValues)
                sum += v;

            timer.postData ({ sum, (float) sum, -1 });
        }

        // UI thread consumes
        TestData received;
        bool hasData = timer.consumeData (received);

        expect (hasData, "Should have data");
        expectEquals (received.sequence, NUM_POLYPHONIC_VOICES, "Sum should equal NUM_POLYPHONIC_VOICES");
    }

    // =========================================
    // PolyData Multi-Threaded Tests
    // =========================================

    /** Setup: Timer with PolyData, audio writes per-voice, UI reads all.
	 *
	 *  Scenario: Audio thread writes to current voice slot. UI thread iterates
	 *  all voices with ScopedAllVoiceSetter and counts non-zero entries.
	 *
	 *  Expected: UI sees writes from audio thread, iteration covers all voices.
	 */
    void testPolyDataUIIterationUnderLoad()
    {
        beginTest ("PolyData UI iteration with ScopedAllVoiceSetter under load");

        setupTimerTest (60);

        PolyData<std::atomic<int>, NUM_POLYPHONIC_VOICES> polyData;
        polyData.prepare (createDefaultSpecs());

        // Initialize all to 0
        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i).store (0);

        std::atomic<int> audioCallbacks { 0 };
        std::atomic<int> voicesWritten { 0 };
        std::atomic<int> maxVoicesSeenByUI { 0 };

        timerNode->onCallback = [&]()
        {
            // UI thread iterates all voices with ScopedAllVoiceSetter
            PolyHandler::ScopedAllVoiceSetter avs (polyHandler);

            int count = 0;
            for (auto& v : polyData)
            {
                if (v.load() > 0)
                    count++;
            }

            // Track max voices ever seen
            int current = maxVoicesSeenByUI.load();
            while (count > current && ! maxVoicesSeenByUI.compare_exchange_weak (current, count))
                ;
        };

        auto result = runThreadedTestWithExistingCallback (
            AudioThread ([&]()
                         {
                             int voiceIdx = audioCallbacks % NUM_POLYPHONIC_VOICES;
                             PolyHandler::ScopedVoiceSetter svs (polyHandler, voiceIdx);

                             // Write to current voice
                             auto& slot = polyData.get();
                             if (slot.load() == 0)
                             {
                                 slot.store (1);
                                 voicesWritten++;
                             }

                             audioCallbacks++;
                         }),
            StopWhen ([&]()
                      {
                          // Run until we've written to at least 16 voices
                          return voicesWritten >= 16 && audioCallbacks >= 50;
                      }));

        expect (result.completed, "Should complete without timeout");
        expect (voicesWritten.load() >= 16, "Should have written to at least 16 voices");
        expect (maxVoicesSeenByUI.load() > 0, "UI should have seen some written voices");
    }

    // =========================================
    // PolyData Thread Safety Scenario Tests
    // =========================================

    /** Setup: PolyData with PolyHandler. ScopedVoiceSetter(5) active.
	 *
	 *  Scenario: Use range-based for loop WITHOUT ScopedAllVoiceSetter 
	 *  while voiceIndex is 5. This demonstrates Scenario #6 bug.
	 *
	 *  Expected: Only voice 5 updated (the bug without protection).
	 */
    void testScenario6RaceWithoutProtection()
    {
        beginTest ("Scenario #6: range-for without ScopedAllVoiceSetter reads stale voiceIndex");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i) = 0;

        // Simulate audio thread setting voiceIndex to 5
        PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);

        // Without ScopedAllVoiceSetter, range-for only processes voice 5
        int voicesUpdated = 0;
        for (auto& v : polyData)
        {
            v = 42;
            voicesUpdated++;
        }

        expectEquals (voicesUpdated, 1, "Without ScopedAllVoiceSetter, only 1 voice updated (the bug)");
        expectEquals (polyData.getWithIndex (5), 42, "Voice 5 should be updated");
        expectEquals (polyData.getWithIndex (0), 0, "Voice 0 should NOT be updated (demonstrates the bug)");
    }

    /** Setup: PolyData with PolyHandler. ScopedVoiceSetter(5) active.
	 *
	 *  Scenario: Use range-based for loop WITH ScopedAllVoiceSetter (the fix).
	 *
	 *  Expected: All voices updated.
	 */
    void testScenario6FixWithScopedAllVoiceSetter()
    {
        beginTest ("Scenario #6 fix: ScopedAllVoiceSetter ensures all voices updated");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i) = 0;

        PolyHandler::ScopedVoiceSetter svs (localPolyHandler, 5);

        {
            PolyHandler::ScopedAllVoiceSetter avs (localPolyHandler);

            int voicesUpdated = 0;
            for (auto& v : polyData)
            {
                v = 42;
                voicesUpdated++;
            }

            expectEquals (voicesUpdated, NUM_POLYPHONIC_VOICES, "With ScopedAllVoiceSetter, ALL voices should be updated");
        }

        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
        {
            expectEquals (polyData.getWithIndex (i), 42, "Voice " + String (i) + " should be updated to 42");
        }
    }

    /** Setup: PolyData with PolyHandler.
	 *
	 *  Scenario: Nest two ScopedAllVoiceSetter instances. Verify getVoiceIndex()
	 *  returns -1 during both, and correct state is restored after each destructor.
	 *
	 *  Expected: ScopedAllVoiceSetter nesting is safe. Inner instance saves/restores
	 *  outer's state. After all destructors, state returns to normal.
	 */
    void testScopedAllVoiceSetterNestingSafe()
    {
        beginTest ("ScopedAllVoiceSetter nesting is safe");

        PolyHandler localPolyHandler (true);

        // Before any ScopedAllVoiceSetter: isAllThread() should be false
        expect (! localPolyHandler.isAllThread(), "Should not be allThread before any wrapper");

        {
            PolyHandler::ScopedAllVoiceSetter outer (localPolyHandler);
            expect (localPolyHandler.isAllThread(), "Should be allThread with outer wrapper");
            expectEquals (localPolyHandler.getVoiceIndex(), -1, "Should return -1 with outer wrapper");

            {
                PolyHandler::ScopedAllVoiceSetter inner (localPolyHandler);
                expect (localPolyHandler.isAllThread(), "Should still be allThread with inner wrapper");
                expectEquals (localPolyHandler.getVoiceIndex(), -1, "Should return -1 with inner wrapper");
            }

            // After inner destructor, outer should still be active
            expect (localPolyHandler.isAllThread(), "Should still be allThread after inner destroyed");
            expectEquals (localPolyHandler.getVoiceIndex(), -1, "Should return -1 after inner destroyed");
        }

        // After all destructors, should be back to normal
        expect (! localPolyHandler.isAllThread(), "Should not be allThread after all wrappers destroyed");
    }

    /** Setup: PolyData with PolyHandler, voiceIndex = -1 (no ScopedVoiceSetter).
	 *
	 *  Scenario: Audio thread before voice rendering uses range-for loop.
	 *
	 *  Expected: All voices updated (voiceIndex = -1 means iterate all).
	 */
    void testScenario7AudioThreadNoProtectionNeeded()
    {
        beginTest ("Scenario #7: Audio thread before voice rendering needs no ScopedAllVoiceSetter");

        PolyHandler localPolyHandler (true);
        PolyData<int, NUM_POLYPHONIC_VOICES> polyData;

        PrepareSpecs specs;
        specs.sampleRate = 44100.0;
        specs.blockSize = 512;
        specs.numChannels = 2;
        specs.voiceIndex = &localPolyHandler;
        polyData.prepare (specs);

        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
            polyData.getWithIndex (i) = 0;

        // No ScopedVoiceSetter active (voiceIndex = -1)
        int voicesUpdated = 0;
        for (auto& v : polyData)
        {
            v = 99;
            voicesUpdated++;
        }

        expectEquals (voicesUpdated, NUM_POLYPHONIC_VOICES, "All voices should be updated when voiceIndex = -1");

        for (int i = 0; i < NUM_POLYPHONIC_VOICES; i++)
        {
            expectEquals (polyData.getWithIndex (i), 99, "Voice " + String (i) + " should be updated to 99");
        }
    }
};

static NodeUIUpdaterTests nodeUIUpdaterTests;

} // namespace hise
