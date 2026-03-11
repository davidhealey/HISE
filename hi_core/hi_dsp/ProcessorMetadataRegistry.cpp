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

namespace hise {
using namespace juce;

// ============================================================================
// ProcessorMetadataRegistry
// ============================================================================

ProcessorMetadataRegistry::ProcessorMetadataRegistry()
{
	registerAllMetadata();
}

const ProcessorMetadata* ProcessorMetadataRegistry::get(const Identifier& typeId) const
{
	if (entries.find(typeId) != entries.end())
		return &entries.at(typeId);

	return nullptr;
}

void ProcessorMetadataRegistry::add(const Identifier& typeId, const ProcessorMetadata& md)
{
	jassert(entries.find(typeId) == entries.end());
	entries[typeId] = md;
}

void ProcessorMetadataRegistry::add(const ProcessorMetadata& md)
{
	jassert(md.id.isValid());
	add(md.id, md);
}

Array<Identifier> ProcessorMetadataRegistry::getRegisteredTypes() const
{
	Array<Identifier> result;

	for (const auto& [key, pd] : entries)
		result.add(key);

	return result;
}

int ProcessorMetadataRegistry::getNumRegistered() const
{
	return entries.size();
}

var ProcessorMetadataRegistry::toJSON() const
{
	Array<var> result;

	for (const auto& [key, pd] : entries)
		result.add(pd.toJSON());
	
	return var(result);
}

// ============================================================================
// registerAllMetadata()
//
// This mirrors the fillTypeNameList() pattern. Each processor either provides
// full metadata via createMetadata(), or gets a fallback entry.
//
// As processors are migrated, replace createFallback<T>() with the real
// createMetadata() call.
// ============================================================================

void ProcessorMetadataRegistry::registerAllMetadata()
{
	// --- VoiceStartModulators ---

	add(ConstantModulator::createMetadata());
	add(VelocityModulator::createMetadata());
	add(KeyModulator::createMetadata());
	add(RandomModulator::createMetadata());
	add(GlobalVoiceStartModulator::createMetadata());
	add(GlobalStaticTimeVariantModulator::createMetadata());
	add(ArrayModulator::createMetadata());
	add(JavascriptVoiceStartModulator::createMetadata());
	add(EventDataModulator::createMetadata());

	// --- TimeVariantModulators ---

	add(ControlModulator::createMetadata());
	add(LfoModulator::createMetadata());
	add(PitchwheelModulator::createMetadata());
	add(MacroModulator::createMetadata());
	add(GlobalTimeVariantModulator::createMetadata());
	add(JavascriptTimeVariantModulator::createMetadata());
	add(HardcodedTimeVariantModulator::createMetadata());

	// --- EnvelopeModulators ---

	add(SimpleEnvelope::createMetadata());
	add(AhdsrEnvelope::createMetadata());
	add(TableEnvelope::createMetadata());
	add(JavascriptEnvelopeModulator::createMetadata());
	add(MPEModulator::createMetadata());
	add(ScriptnodeVoiceKiller::createMetadata());
	add(GlobalEnvelopeModulator::createMetadata());
	add(EventDataEnvelope::createMetadata());
	add(HardcodedEnvelopeModulator::createMetadata());
	add(MatrixModulator::createMetadata());
	add(FlexAhdsrEnvelope::createMetadata());

	// --- MidiProcessors (fallback) ---

	add(JavascriptMidiProcessor::createMetadata());
	add(ProcessorMetadata::createFallback<Transposer>());
	add(ProcessorMetadata::createFallback<MidiPlayer>());
	add(ProcessorMetadata::createFallback<ChokeGroupProcessor>());

	// Hardcoded script processors
	add(ProcessorMetadata::createFallback<LegatoProcessor>());
	add(ProcessorMetadata::createFallback<CCSwapper>());
	add(ProcessorMetadata::createFallback<ReleaseTriggerScriptProcessor>());
	add(ProcessorMetadata::createFallback<CCToNoteProcessor>());
	add(ProcessorMetadata::createFallback<ChannelFilterScriptProcessor>());
	add(ProcessorMetadata::createFallback<ChannelSetterScriptProcessor>());
	add(ProcessorMetadata::createFallback<MuteAllScriptProcessor>());
	add(ProcessorMetadata::createFallback<hise::Arpeggiator>());

	// --- Effects (fallback) ---

	add(ProcessorMetadata::createFallback<PolyFilterEffect>());
	add(ProcessorMetadata::createFallback<HarmonicFilter>());
	add(ProcessorMetadata::createFallback<HarmonicMonophonicFilter>());
	add(ProcessorMetadata::createFallback<CurveEq>());
	add(ProcessorMetadata::createFallback<StereoEffect>());
	add(ProcessorMetadata::createFallback<SimpleReverbEffect>());
	add(ProcessorMetadata::createFallback<GainEffect>());
	add(ProcessorMetadata::createFallback<ConvolutionEffect>());
	add(ProcessorMetadata::createFallback<DelayEffect>());
	add(ProcessorMetadata::createFallback<ChorusEffect>());
	add(ProcessorMetadata::createFallback<PhaseFX>());
	add(ProcessorMetadata::createFallback<RouteEffect>());
	add(ProcessorMetadata::createFallback<SendEffect>());
	add(ProcessorMetadata::createFallback<SaturatorEffect>());
	add(JavascriptMasterEffect::createMetadata());
	add(JavascriptPolyphonicEffect::createMetadata());
	add(SlotFX::createMetadata());
	add(ProcessorMetadata::createFallback<EmptyFX>());
	add(ProcessorMetadata::createFallback<DynamicsEffect>());
	add(ProcessorMetadata::createFallback<AnalyserEffect>());
	add(ProcessorMetadata::createFallback<ShapeFX>());
	add(ProcessorMetadata::createFallback<PolyshapeFX>());
	add(HardcodedMasterFX::createMetadata());
	add(HardcodedPolyphonicFX::createMetadata());
	add(ProcessorMetadata::createFallback<MidiMetronome>());
	add(ProcessorMetadata::createFallback<NoiseGrainPlayer>());

	// --- SoundGenerators (fallback) ---

	add(ProcessorMetadata::createFallback<ModulatorSampler>());
	add(ProcessorMetadata::createFallback<SineSynth>());
	add(ModulatorSynthChain::createMetadata());
	add(GlobalModulatorContainer::createMetadata());
	add(WaveSynth::createMetadata());
	add(ProcessorMetadata::createFallback<NoiseSynth>());
	add(ProcessorMetadata::createFallback<WavetableSynth>());
	add(ProcessorMetadata::createFallback<AudioLooper>());
	add(ModulatorSynthGroup::createMetadata());
	add(JavascriptSynthesiser::createMetadata());
	add(ProcessorMetadata::createFallback<MacroModulationSource>());
	add(SendContainer::createMetadata());
	add(ProcessorMetadata::createFallback<SilentSynth>());
	add(HardcodedSynthesiser::createMetadata());
	
	auto x = JSON::toString(toJSON());
	DBG(x);
}

} // namespace hise
