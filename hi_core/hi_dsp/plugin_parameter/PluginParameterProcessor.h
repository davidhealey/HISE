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
*   which must be separately licensed for closed source applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

#ifndef __PLUGINPROCESSOR_H_BF259E1F__
#define __PLUGINPROCESSOR_H_BF259E1F__

namespace hise { using namespace juce;

#pragma warning (push)
#pragma warning (disable: 4996)


//==============================================================================
/** @class PluginParameterAudioProcessor
	@brief This AudioProcessor subclass removes some annoying virtual methods and allows PluginParameter objects to 
		   communicate with the host.

	The following methods are overwritten:

	- All Parameter methods: getNumParameters(), getParameter(), setParameter(), getParameterName(), getParameterText()
	- All Channel methods: Stereo mode assumed
	- All Program methods: functionality disabled
	
	Also a CPU meter is implemented, which can be read with getCpuUsage()

	See PluginParameterTestAudioProcessor for an example implementation of this abstract class.
*/
class PluginParameterAudioProcessor  : public AudioProcessor
{
public:
    //==============================================================================
    
	/** You have to create and add all PluginParameters here in order to ensure compatibility with most hosts */
	PluginParameterAudioProcessor(const String &name_ = "Untitled");

#if HISE_MIDIFX_PLUGIN
	bool isMidiEffect() const override
	{
		return true;
	}
#endif

	BusesProperties getHiseBusProperties() const;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    virtual ~PluginParameterAudioProcessor();;

	void handleLatencyInPrepareToPlay(double samplerate);

	void handleLatencyWhenBypassed(AudioSampleBuffer& buffer, MidiBuffer& );

	void setScriptedPluginParameter(Identifier id, float newValue);
	void addScriptedParameters();

    //==============================================================================
	const String getName() const;;

	const String getInputChannelName (int channelIndex) const;;
    const String getOutputChannelName (int channelIndex) const;;
	bool isInputChannelStereoPair (int ) const;;
	bool isOutputChannelStereoPair (int ) const;;

	void setNonRealtime(bool isNonRealtime) noexcept override;


    //==============================================================================
	int getNumPrograms();;
	int getCurrentProgram();;
	void setCurrentProgram (int );;
	const String getProgramName (int );;
	void changeProgramName (int , const String& );;
    
	String name;

	OwnedArray<DelayLine<32768>> bypassedLatencyDelays;
	int lastLatencySamples = 0;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginParameterAudioProcessor)
};


#pragma warning (pop)

struct HisePluginParameterBase: public AsyncUpdater,
								public ControlledObject,
								public dispatch::ListenerOwner
{
	enum class Type
	{
		CustomAutomation,
		Macro,
		numTypes
	};

	HisePluginParameterBase(MainController* mc, int index):
	  ControlledObject(mc),
	  autoListener(getMainController()->getRootDispatcher(), *this, BIND_MEMBER_FUNCTION_2(HisePluginParameterBase::onUpdate)),
	  parameterIndex(index)
	{}

	virtual ~HisePluginParameterBase() = default;

	int getHiseParameterIndex() { return parameterIndex; }
	virtual Type getType() const = 0;
	virtual NormalisableRange<float> getNormalisableRange() const = 0;
	virtual bool matchesIndex(int slotIndex) const = 0;

	void onUpdate(int index, float v)
	{
		FloatSanitizers::sanitizeFloatNumber(v);
		v = getNormalisableRange().convertTo0to1(v);

		if(v != parameterValueToSend)
		{
			parameterValueToSend = v;
			refreshParameterValue();
		}
	}

	void refreshParameterValue()
	{
		auto mc = getMainController();
		auto t = mc->getKillStateHandler().getCurrentThread();
		auto defer = mc->getDeferNotifyHostFlag() || t != MainController::KillStateHandler::TargetThread::MessageThread;

		if(defer)
			triggerAsyncUpdate();
		else
			handleAsyncUpdate();
	}

	void handleAsyncUpdate() override
	{
		auto mc = getMainController();
		
		ScopedValueSetter<bool> svs(recursive, true);
		ScopedValueSetter<bool> setter(mc->getPluginParameterUpdateState(), false, true);

		auto parentProcessor = dynamic_cast<AudioProcessor*>(mc);
		auto pindex = parameterIndex;

		parentProcessor->beginParameterChangeGesture(pindex);
		parentProcessor->setParameterNotifyingHost(pindex, parameterValueToSend);
		parentProcessor->endParameterChangeGesture(pindex);
	}

    virtual void cleanup() = 0;
    
	const int parameterIndex;
	float parameterValueToSend = 0.0f;
	bool recursive = false;

	dispatch::library::CustomAutomationSource::Listener autoListener;

	JUCE_DECLARE_WEAK_REFERENCEABLE(HisePluginParameterBase);
};

} // namespace hise

#endif  // __PLUGINPROCESSOR_H_BF259E1F__
