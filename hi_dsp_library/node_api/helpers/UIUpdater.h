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

#pragma once

/** Timer and async updater classes for scriptnode nodes.

    Use NodeUITimer or NodeUIAsyncUpdater for any timer/async 
    functionality in your nodes. These classes:
    
    1. Provide message thread simulation in DLL builds (where juce::Timer won't work)
    2. Automatically wrap callbacks with ScopedAllVoiceSetter for safe PolyData access
    3. Support voice tracking to only update UI for a specific voice (avoids jumping displays)
    4. Support rate limiting for AsyncUpdater (avoids excessive UI updates)
    5. Support lock-free data exchange via WithData<T> subclasses
    
    IMPORTANT: Do not use juce::Timer or juce::AsyncUpdater directly in node code.
    In DLL builds, they will silently fail (no message thread).
    
    Usage:
    - Inherit from NodeUITimer or NodeUIAsyncUpdater
    - Call setupUIUpdater(specs) in your prepare() method
    - Override onUITimerCallback() or onUIAsyncUpdate()
    - Optionally call setActiveVoiceForUIUpdates() in handleHiseEvent() on note-on
    - For lock-free data transfer, use NodeUITimer::WithData<T> or 
      NodeUIAsyncUpdater::WithData<T>
*/

namespace hise {
using namespace juce;

// Forward declarations for WithData<T> aliases
template<typename T> struct NodeUITimerWithData;
template<typename T> struct NodeUIAsyncUpdaterWithData;

struct NodeUIUpdaterBase
{
	/** Call this in your node's prepare() method to initialize the UI updater. */
	void setupUIUpdater(PrepareSpecs specs)
	{
		prepareSpecs = specs;
	}

	/** Call this in handleHiseEvent() on note-on to track this voice for UI updates.
	    After calling this, only the tracked voice will trigger UI updates.
	    This prevents UI displays from jumping around when multiple voices are active. */
	void setActiveVoiceForUIUpdates()
	{
		if (prepareSpecs.voiceIndex != nullptr)
		{
			trackedVoiceIndex = prepareSpecs.voiceIndex->getVoiceIndex();
			voiceTrackingEnabled = true;
		}
	}

protected:
	PrepareSpecs prepareSpecs;

	int trackedVoiceIndex = -1;
	bool voiceTrackingEnabled = false;

	/** Returns true if we should proceed with UI update/data post.
	    Call this from audio thread methods (process, handleHiseEvent, etc.) */
	bool shouldUpdateForCurrentVoice() const
	{
		if (!voiceTrackingEnabled || prepareSpecs.voiceIndex == nullptr)
			return true;  // No tracking, allow all

		int currentVoice = prepareSpecs.voiceIndex->getVoiceIndex();

		// If voice tracking is enabled, we expect to be called from voice rendering context.
		// If currentVoice is -1 here, the user is calling from the wrong place.
		jassert(currentVoice >= 0 && "shouldUpdateForCurrentVoice() called outside voice rendering context. "
		                            "Only call triggerAsyncUpdate() / postData() from process() or handleHiseEvent().");

		if (currentVoice < 0)
			return false;  // Fail safe: don't update if we can't determine voice

		return currentVoice == trackedVoiceIndex;
	}

	template<typename F>
	void callWithVoiceProtection(F&& f)
	{
		if (prepareSpecs.voiceIndex != nullptr)
		{
			PolyHandler::ScopedAllVoiceSetter avs(*prepareSpecs.voiceIndex);
			f();
		}
		else
		{
			f();
		}
	}

#if HI_EXPORT_AS_PROJECT_DLL
	class TimerThread : public Thread,
		public DeletedAtShutdown
	{
	public:

		TimerThread() :
			Thread("DLL Timer thread")
		{
			timers.ensureStorageAllocated(64);
			asyncUpdaters.ensureStorageAllocated(128);

			ThreadStarters::startNormal(this);
		}

		~TimerThread()
		{
			stopThread(1000);
		}

		void addUpdater(NodeUIUpdaterBase* t)
		{
			asyncUpdaters.add(t);
			notify();
		}

		void addTimer(NodeUIUpdaterBase* t)
		{
			hise::SimpleReadWriteLock::ScopedWriteLock sl(lock);
			timers.addIfNotAlreadyThere(t);
		}

		void removeTimer(NodeUIUpdaterBase* t)
		{
			hise::SimpleReadWriteLock::ScopedWriteLock sl(lock);
			timers.removeAllInstancesOf(t);
		}

		void run() override
		{
			while (!threadShouldExit())
			{
				if (auto sl = hise::SimpleReadWriteLock::ScopedTryReadLock(lock))
				{
					for (auto& t : timers)
					{
						if (threadShouldExit())
							break;

						if (t != nullptr)
							t->execute();
					}

					if (!asyncUpdaters.isEmpty())
					{
						Array<WeakReference<NodeUIUpdaterBase>> pending;
						pending.ensureStorageAllocated(128);
						pending.swapWith(asyncUpdaters);

						for (auto& p : pending)
						{
							if (p != nullptr)
								p->execute();
						}
					}
				}

				wait(15);
			}
		}

		hise::SimpleReadWriteLock lock;
		Array<WeakReference<NodeUIUpdaterBase>> timers;
		Array<WeakReference<NodeUIUpdaterBase>> asyncUpdaters;
	};

	virtual void execute() = 0;

	SharedResourcePointer<TimerThread> thread;

	JUCE_DECLARE_WEAK_REFERENCEABLE(NodeUIUpdaterBase);
#endif
};

#if HI_EXPORT_AS_PROJECT_DLL

struct NodeUITimer : public NodeUIUpdaterBase
{
	virtual ~NodeUITimer()
	{
		stopTimer();
	}

	void startTimerHz(int frequencyHz)
	{
		jassert(prepareSpecs && "startTimerHz() called before setupUIUpdater(). "
		                        "Call setupUIUpdater(specs) in your prepare() method first.");
		startTimer(1000 / frequencyHz);
	}

	void startTimer(int milliSeconds)
	{
		jassert(prepareSpecs && "startTimer() called before setupUIUpdater(). "
		                        "Call setupUIUpdater(specs) in your prepare() method first.");

		numSteps = jmax(1, milliSeconds / 15);
		counter = numSteps;

		if (running)
			return;

		running = true;
		thread->addTimer(this);
	}

	bool isTimerRunning() const { return running; }

	void stopTimer()
	{
		running = false;
		thread->removeTimer(this);
	}

	virtual void onUITimerCallback() = 0;

	template<typename T>
	using WithData = NodeUITimerWithData<T>;

private:
	void execute() override
	{
		if (--counter == 0)
		{
			counter = numSteps;
			callWithVoiceProtection([this]() { onUITimerCallback(); });
		}
	}

	int numSteps = 1;
	int counter = 1;
	bool running = false;
};

struct NodeUIAsyncUpdater : public NodeUIUpdaterBase
{
	virtual ~NodeUIAsyncUpdater() = default;

	/** Enable rate-limited async updates.
	    @param enabled              Whether to enable rate limiting
	    @param targetFPS            Target UI update rate (default 30)
	    @param triggersPerCallback  How many times you call triggerAsyncUpdate() per audio callback (default 1)
	*/
	void setEnableRateLimiting(bool enabled, double targetFPS = 30.0, int triggersPerCallback = 1)
	{
		rateLimitingEnabled = enabled;

		if (enabled && prepareSpecs.sampleRate > 0 && prepareSpecs.blockSize > 0)
		{
			double callbacksPerSec = prepareSpecs.sampleRate / (double)prepareSpecs.blockSize;
			double triggersPerSec = callbacksPerSec * (double)triggersPerCallback;
			updateThreshold = jmax(1, (int)(triggersPerSec / targetFPS));
		}
		else
		{
			updateThreshold = 1;
		}

		rateLimitCounter = 0;
	}

	void triggerAsyncUpdate()
	{
		jassert(prepareSpecs && "triggerAsyncUpdate() called before setupUIUpdater(). "
		                        "Call setupUIUpdater(specs) in your prepare() method first.");

		if (!shouldUpdateForCurrentVoice())
			return;

		if (rateLimitingEnabled)
		{
			if (++rateLimitCounter >= updateThreshold)
			{
				rateLimitCounter = 0;
				if (!dirty)
				{
					dirty = true;
					thread->addUpdater(this);
				}
			}
		}
		else
		{
			if (!dirty)
			{
				dirty = true;
				thread->addUpdater(this);
			}
		}
	}

	virtual void onUIAsyncUpdate() = 0;

	template<typename T>
	using WithData = NodeUIAsyncUpdaterWithData<T>;

private:
	void execute() override
	{
		if (dirty)
		{
			callWithVoiceProtection([this]() { onUIAsyncUpdate(); });
			dirty = false;
		}
	}

	bool dirty = false;
	bool rateLimitingEnabled = false;
	int rateLimitCounter = 0;
	int updateThreshold = 1;
};

#else // Non-DLL build

struct NodeUITimer : private juce::Timer, public NodeUIUpdaterBase
{
	virtual ~NodeUITimer() = default;

	void startTimer(int milliSeconds)
	{
		jassert(prepareSpecs && "startTimer() called before setupUIUpdater(). "
		                        "Call setupUIUpdater(specs) in your prepare() method first.");
		juce::Timer::startTimer(milliSeconds);
	}

	void startTimerHz(int frequencyHz)
	{
		jassert(prepareSpecs && "startTimerHz() called before setupUIUpdater(). "
		                        "Call setupUIUpdater(specs) in your prepare() method first.");
		juce::Timer::startTimerHz(frequencyHz);
	}

	void stopTimer()
	{
		juce::Timer::stopTimer();
	}

	bool isTimerRunning() const
	{
		return juce::Timer::isTimerRunning();
	}

	virtual void onUITimerCallback() = 0;

	template<typename T>
	using WithData = NodeUITimerWithData<T>;

private:
	void timerCallback() override
	{
		callWithVoiceProtection([this]() { onUITimerCallback(); });
	}
};

struct NodeUIAsyncUpdater : private juce::AsyncUpdater, public NodeUIUpdaterBase
{
	virtual ~NodeUIAsyncUpdater() = default;

	/** Enable rate-limited async updates.
	    @param enabled              Whether to enable rate limiting
	    @param targetFPS            Target UI update rate (default 30)
	    @param triggersPerCallback  How many times you call triggerAsyncUpdate() per audio callback (default 1)
	*/
	void setEnableRateLimiting(bool enabled, double targetFPS = 30.0, int triggersPerCallback = 1)
	{
		rateLimitingEnabled = enabled;

		if (enabled && prepareSpecs.sampleRate > 0 && prepareSpecs.blockSize > 0)
		{
			double callbacksPerSec = prepareSpecs.sampleRate / (double)prepareSpecs.blockSize;
			double triggersPerSec = callbacksPerSec * (double)triggersPerCallback;
			updateThreshold = jmax(1, (int)(triggersPerSec / targetFPS));
		}
		else
		{
			updateThreshold = 1;
		}

		rateLimitCounter = 0;
	}

	void triggerAsyncUpdate()
	{
		jassert(prepareSpecs && "triggerAsyncUpdate() called before setupUIUpdater(). "
		                        "Call setupUIUpdater(specs) in your prepare() method first.");

		if (!shouldUpdateForCurrentVoice())
			return;

		if (rateLimitingEnabled)
		{
			if (++rateLimitCounter >= updateThreshold)
			{
				rateLimitCounter = 0;
				juce::AsyncUpdater::triggerAsyncUpdate();
			}
		}
		else
		{
			juce::AsyncUpdater::triggerAsyncUpdate();
		}
	}

	void cancelPendingUpdate()
	{
		juce::AsyncUpdater::cancelPendingUpdate();
	}

	void handleUpdateNowIfNeeded()
	{
		juce::AsyncUpdater::handleUpdateNowIfNeeded();
	}

	bool isUpdatePending() const
	{
		return juce::AsyncUpdater::isUpdatePending();
	}

	virtual void onUIAsyncUpdate() = 0;

	template<typename T>
	using WithData = NodeUIAsyncUpdaterWithData<T>;

private:
	void handleAsyncUpdate() override
	{
		callWithVoiceProtection([this]() { onUIAsyncUpdate(); });
	}

	bool rateLimitingEnabled = false;
	int rateLimitCounter = 0;
	int updateThreshold = 1;
};

#endif

// ============================================================================
// WithData<T> template subclasses for lock-free data exchange
// ============================================================================

/** A NodeUITimer subclass that supports lock-free data exchange between
    audio and UI threads.
    
    Usage:
    @code
    struct DisplayData { float level; int state; };
    
    struct MyNode : public NodeUITimer::WithData<DisplayData>
    {
        void prepare(PrepareSpecs specs)
        {
            setupUIUpdater(specs);
            startTimerHz(30);
        }
        
        void process(ProcessData& data)
        {
            postData({ currentLevel, currentState });
        }
        
        void onUITimerCallback() override
        {
            DisplayData d;
            if (consumeData(d))
                updateUI(d);
        }
    };
    @endcode
*/
template<typename T>
struct NodeUITimerWithData : public NodeUITimer
{
	static_assert(std::is_trivially_copyable<T>::value,
		"T must be trivially copyable for lock-free exchange");

	/** Post data from the audio thread. Only posts if voice tracking allows it. */
	void postData(const T& data)
	{
		if (!shouldUpdateForCurrentVoice())
			return;

		pendingData.store(data);
		hasNewData.store(true);
	}

	/** Consume data on the UI thread. Returns true if new data was available. */
	bool consumeData(T& out)
	{
		if (hasNewData.exchange(false))
		{
			out = pendingData.load();
			return true;
		}
		return false;
	}

private:
	std::atomic<T> pendingData;
	std::atomic<bool> hasNewData { false };
};

/** A NodeUIAsyncUpdater subclass that supports lock-free data exchange between
    audio and UI threads.
    
    Usage:
    @code
    struct DisplayData { float level; int state; };
    
    struct MyNode : public NodeUIAsyncUpdater::WithData<DisplayData>
    {
        void prepare(PrepareSpecs specs)
        {
            setupUIUpdater(specs);
            setEnableRateLimiting(true, 30.0, 1);
        }
        
        void handleHiseEvent(HiseEvent& e)
        {
            if (e.isNoteOn())
                setActiveVoiceForUIUpdates();
        }
        
        void process(ProcessData& data)
        {
            postData({ currentLevel, currentState });
            triggerAsyncUpdate();
        }
        
        void onUIAsyncUpdate() override
        {
            DisplayData d;
            if (consumeData(d))
                updateUI(d);
        }
    };
    @endcode
*/
template<typename T>
struct NodeUIAsyncUpdaterWithData : public NodeUIAsyncUpdater
{
	static_assert(std::is_trivially_copyable<T>::value,
		"T must be trivially copyable for lock-free exchange");

	/** Post data from the audio thread. Only posts if voice tracking allows it. */
	void postData(const T& data)
	{
		if (!shouldUpdateForCurrentVoice())
			return;

		pendingData.store(data);
		hasNewData.store(true);
	}

	/** Consume data on the UI thread. Returns true if new data was available. */
	bool consumeData(T& out)
	{
		if (hasNewData.exchange(false))
		{
			out = pendingData.load();
			return true;
		}
		return false;
	}

private:
	std::atomic<T> pendingData;
	std::atomic<bool> hasNewData { false };
};

// ============================================================================
// Legacy stubs - force compile error with migration message
// ============================================================================

struct DllTimer
{
	// DllTimer has been removed. Migration steps:
	// 1. Change base class to NodeUITimer
	// 2. Rename timerCallback() to onUITimerCallback()
	// 3. Add setupUIUpdater(specs) in your prepare() method
	DllTimer() = delete;
	virtual ~DllTimer() = default;
	virtual void timerCallback() = 0;

	void startTimer(int) {}
	void startTimerHz(int) {}
	void stopTimer() {}
	bool isTimerRunning() const { return false; }
	int getTimerInterval() const { return 0; }
};

struct DllAsyncUpdater
{
	// DllAsyncUpdater has been removed. Migration steps:
	// 1. Change base class to NodeUIAsyncUpdater
	// 2. Rename handleAsyncUpdate() to onUIAsyncUpdate()
	// 3. Add setupUIUpdater(specs) in your prepare() method
	DllAsyncUpdater() = delete;
	virtual ~DllAsyncUpdater() = default;
	virtual void handleAsyncUpdate() = 0;

	void triggerAsyncUpdate() {}
	void cancelPendingUpdate() {}
	void handleUpdateNowIfNeeded() {}
	bool isUpdatePending() const { return false; }
};

} // namespace hise
