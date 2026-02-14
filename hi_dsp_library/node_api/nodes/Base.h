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
 *   information about commercial licensing:
 *
 *   http://www.hise.audio/
 *
 *   HISE is based on the JUCE library,
 *   which also must be licenced for commercial applications:
 *
 *   http://www.juce.com
 *
 *   ===========================================================================
 */

#pragma once

namespace scriptnode
{
using namespace juce;
using namespace hise;
using namespace snex::Types;

class ParameterHolder
{
public:

	virtual ~ParameterHolder() {};

	virtual void createParameters(ParameterDataList& data) {};

};

struct polyphonic_base
{
	virtual ~polyphonic_base() {};

	polyphonic_base(const Identifier& id, bool addProcessEventFlag=true)
	{
		cppgen::CustomNodeProperties::addNodeIdManually(id, PropertyIds::IsPolyphonic);

		if(addProcessEventFlag)
			cppgen::CustomNodeProperties::addNodeIdManually(id, PropertyIds::IsProcessingHiseEvent);
	}

	/** Mark parameter P as internally modulated (connected via wrap::mod / parameter::plain).
	 *  Called at connection time by parameter::single_base::connect(). Parameters marked this
	 *  way can use TrustAudioThreadContext to skip thread-ID checks in forEachCurrentVoice(). */
	template <int P> void setInternallyModulated()
	{
		static_assert(P < 64, "Parameter index must be less than 64");
		internallyModulatedParams |= (1ULL << P);
	}

	/** Returns true if parameter p is internally modulated AND we're not in a prepare() context.
	 *  During prepare(), all parameters must iterate all voices regardless of modulation state,
	 *  so this returns false when ScopedPrepareContext is active. */
	bool isInternallyModulated(int p) const
	{
		if (forceAllVoiceIteration)
			return false;

		return (internallyModulatedParams & (1ULL << p)) != 0;
	}

	/** RAII guard for prepare() methods. While active, isInternallyModulated() returns false
	 *  for all parameters, causing SN_ACCESS_TYPE_FOR_PARAM to return AllowUncached instead
	 *  of TrustAudioThreadContext. This ensures parameter setters called from prepare()
	 *  iterate all voices (since there's no active voice context during prepare). */
	struct ScopedPrepareContext
	{
		ScopedPrepareContext(polyphonic_base& p_):
		  p(p_),
		  prev(p_.forceAllVoiceIteration)
		{
			p.forceAllVoiceIteration = true;
		}

		~ScopedPrepareContext()
		{
			p.forceAllVoiceIteration = prev;
		}

	private:

		polyphonic_base& p;
		bool prev;
	};

private:

	bool forceAllVoiceIteration = false;
	uint64_t internallyModulatedParams = 0;
};

class HiseDspBase: public ParameterHolder
{
public:

	HiseDspBase() {};

	virtual ~HiseDspBase() {};

	bool isPolyphonic() const { return false; }

	virtual void initialise(ObjectWithValueTree* n)
	{
		ignoreUnused(n);
	}
};

template <class T> class SingleWrapper : public HiseDspBase
{
public:

    inline void initialise(ObjectWithValueTree* n) override
	{
		obj.initialise(n);
	}

	void handleHiseEvent(HiseEvent& e)
	{
		obj.handleHiseEvent(e);
	}

	T obj;
};


}
