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


/** When a node is loaded from the DLL into HISE we'll need to simulate the message thread
    because the process where the DLL runs cannot access the standard UI thread. 

	All you need to do is to subclass your node from either

	- hise::DllAsyncUpdater for a juce::AsycUpdater replacement or
	- hise::DllTimer for a juce::Timer replacement.

	Both classes are designed to be drop in replacements for the original classes.
	
	Note that when you compile the plugin, this all goes away and the node uses the actual JUCE classes.
*/
#if 1 || HI_EXPORT_AS_PROJECT_DLL
namespace hise {
	using namespace juce;


struct DllUpdaterBase
{
	virtual ~DllUpdaterBase() = default;

protected:

	virtual void execute() = 0;

	class TimerThread : public Thread,
		public DeletedAtShutdown
	{
	public:

		TimerThread() :
			Thread("DLL Timer thread")
		{
			timers.ensureStorageAllocated(64);
			asyncUpdaters.ensureStorageAllocated(128);

			startThread(5);
		}

		~TimerThread()
		{
			stopThread(1000);
		}

		void addUpdater(DllUpdaterBase* t)
		{
			asyncUpdaters.add(t);
			notify();
		}

		void addTimer(DllUpdaterBase* t)
		{
			hise::SimpleReadWriteLock::ScopedWriteLock sl(lock);
			timers.addIfNotAlreadyThere(t);
		}

		void removeTimer(DllUpdaterBase* t)
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
						Array<WeakReference<DllUpdaterBase>> pending;
						pending.ensureStorageAllocated(128);
						pending.swapWith(asyncUpdaters);

						for (auto& p : pending)
							p->execute();
					}
				}

				wait(15);
			}
		}

		hise::SimpleReadWriteLock lock;
		Array<WeakReference<DllUpdaterBase>> timers;
		Array<WeakReference<DllUpdaterBase>> asyncUpdaters;
	};

	SharedResourcePointer<TimerThread> thread;

	JUCE_DECLARE_WEAK_REFERENCEABLE(DllUpdaterBase);
};



struct DllAsyncUpdater : public DllUpdaterBase
{
	void triggerAsyncUpdate()
	{
		if (!dirty)
		{
			dirty = true;
			thread->addUpdater(this);
		}
	}

	virtual void handleAsyncUpdate() = 0;

	void execute() override
	{
		if (dirty)
		{
			handleAsyncUpdate();
			dirty = false;
		}
	}

	SharedResourcePointer<TimerThread> thread;

	bool dirty = false;
};

struct DllTimer : public DllUpdaterBase
{
	virtual ~DllTimer() {};

	void startTimerHz(int frequencyHz)
	{
		startTimer(1000 / frequencyHz);
	}

	void startTimer(int milliSeconds)
	{
		numSteps = milliSeconds / 15;
		counter = numSteps;

		if(running)
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

	virtual void timerCallback() = 0;

private:

	void execute()
	{
		if (--counter == 0)
		{
			counter = numSteps;
			timerCallback();
		}
	}

	int numSteps = 1;
	int counter = 1;
	bool running = false;
};

}
#else


namespace hise { using namespace juce;

// In a non-DLL build we'll use the proper JUCE classes that operate on the real message thread
using DllTimer = juce::Timer;
using DllAsyncUpdater = juce::AsyncUpdater;

}

#endif
