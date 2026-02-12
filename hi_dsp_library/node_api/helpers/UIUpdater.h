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

    Use VoiceProtectedTimer or VoiceProtectedAsyncUpdater for any timer/async 
    functionality in your nodes. These classes:
    
    1. Provide message thread simulation in DLL builds (where juce::Timer won't work)
    2. Automatically wrap callbacks with ScopedAllVoiceSetter for safe PolyData access
    
    IMPORTANT: Do not use juce::Timer or juce::AsyncUpdater directly in node code.
    In DLL builds, they will silently fail (no message thread).
    
    Usage:
    - Inherit from VoiceProtectedTimer or VoiceProtectedAsyncUpdater
    - Call setPolyHandler(specs.voiceIndex) in your prepare() method
    - Override onTimerWithVoiceProtection() or onAsyncUpdateWithVoiceProtection()
*/

namespace hise {
using namespace juce;

struct VoiceProtectionBase
{
	void setPolyHandler(PolyHandler* ph) { polyHandler = ph; }

protected:
	PolyHandler* polyHandler = nullptr;

	template<typename F>
	void callWithVoiceProtection(F&& f)
	{
		if (polyHandler != nullptr)
		{
			PolyHandler::ScopedAllVoiceSetter avs(*polyHandler);
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

		void addUpdater(VoiceProtectionBase* t)
		{
			asyncUpdaters.add(t);
			notify();
		}

		void addTimer(VoiceProtectionBase* t)
		{
			hise::SimpleReadWriteLock::ScopedWriteLock sl(lock);
			timers.addIfNotAlreadyThere(t);
		}

		void removeTimer(VoiceProtectionBase* t)
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
						Array<WeakReference<VoiceProtectionBase>> pending;
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
		Array<WeakReference<VoiceProtectionBase>> timers;
		Array<WeakReference<VoiceProtectionBase>> asyncUpdaters;
	};

	virtual void execute() = 0;

	SharedResourcePointer<TimerThread> thread;

	JUCE_DECLARE_WEAK_REFERENCEABLE(VoiceProtectionBase);
#endif
};

#if HI_EXPORT_AS_PROJECT_DLL

struct VoiceProtectedTimer : public VoiceProtectionBase
{
	virtual ~VoiceProtectedTimer()
	{
		stopTimer();
	}

	void startTimerHz(int frequencyHz)
	{
		startTimer(1000 / frequencyHz);
	}

	void startTimer(int milliSeconds)
	{
		numSteps = milliSeconds / 15;
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

	virtual void onTimerWithVoiceProtection() = 0;

private:
	void execute() override
	{
		if (--counter == 0)
		{
			counter = numSteps;
			callWithVoiceProtection([this]() { onTimerWithVoiceProtection(); });
		}
	}

	int numSteps = 1;
	int counter = 1;
	bool running = false;
};

struct VoiceProtectedAsyncUpdater : public VoiceProtectionBase
{
	virtual ~VoiceProtectedAsyncUpdater() = default;

	void triggerAsyncUpdate()
	{
		if (!dirty)
		{
			dirty = true;
			thread->addUpdater(this);
		}
	}

	virtual void onAsyncUpdateWithVoiceProtection() = 0;

private:
	void execute() override
	{
		if (dirty)
		{
			callWithVoiceProtection([this]() { onAsyncUpdateWithVoiceProtection(); });
			dirty = false;
		}
	}

	bool dirty = false;
};

#else // Non-DLL build

struct VoiceProtectedTimer : private juce::Timer, public VoiceProtectionBase
{
	virtual ~VoiceProtectedTimer() = default;

	using juce::Timer::startTimer;
	using juce::Timer::startTimerHz;
	using juce::Timer::stopTimer;
	using juce::Timer::isTimerRunning;

	virtual void onTimerWithVoiceProtection() = 0;

private:
	void timerCallback() override
	{
		callWithVoiceProtection([this]() { onTimerWithVoiceProtection(); });
	}
};

struct VoiceProtectedAsyncUpdater : private juce::AsyncUpdater, public VoiceProtectionBase
{
	virtual ~VoiceProtectedAsyncUpdater() = default;

	using juce::AsyncUpdater::triggerAsyncUpdate;
	using juce::AsyncUpdater::cancelPendingUpdate;
	using juce::AsyncUpdater::handleUpdateNowIfNeeded;
	using juce::AsyncUpdater::isUpdatePending;

	virtual void onAsyncUpdateWithVoiceProtection() = 0;

private:
	void handleAsyncUpdate() override
	{
		callWithVoiceProtection([this]() { onAsyncUpdateWithVoiceProtection(); });
	}
};

#endif

// Legacy stubs - force compile error with migration message
struct DllTimer
{
	// DllTimer has been removed. Migration steps:
	// 1. Change base class to VoiceProtectedTimer
	// 2. Rename timerCallback() to onTimerWithVoiceProtection()
	// 3. Add setPolyHandler(specs.voiceIndex) in prepare()
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
	// 1. Change base class to VoiceProtectedAsyncUpdater
	// 2. Rename handleAsyncUpdate() to onAsyncUpdateWithVoiceProtection()
	// 3. Add setPolyHandler(specs.voiceIndex) in prepare()
	DllAsyncUpdater() = delete;
	virtual ~DllAsyncUpdater() = default;
	virtual void handleAsyncUpdate() = 0;

	void triggerAsyncUpdate() {}
	void cancelPendingUpdate() {}
	void handleUpdateNowIfNeeded() {}
	bool isUpdatePending() const { return false; }
};

} // namespace hise
