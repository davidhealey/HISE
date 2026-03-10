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

#ifndef PROCESSOR_METADATA_H
#define PROCESSOR_METADATA_H

namespace hise {
using namespace juce;

class Processor;

/** Unified parameter and module metadata for HISE processors.
*
*	Replaces the scattered metadata across editor constructors, parameterNames,
*	getDefaultValue(), and ProcessorDocumentation with a single declaration per
*	processor type using a fluent builder pattern.
*
*	Each processor class declares a static createMetadata() method that returns
*	a fully populated ProcessorMetadata. This feeds three consumers:
*	1. Editor widget setup (md.setup(slider, processor, paramIndex))
*	2. Factory/registry queries (JSON dump without processor instantiation)
*	3. Documentation extraction (docs.hise.dev enrichment pipeline)
*/
struct ProcessorMetadata
{
	enum class DataType
	{
		Undefined = 0,
		Fallback,
		StaticInitialised,
		Dynamic
	};

	/** Metadata for a single processor parameter. */
	struct ParameterMetadata
	{
		enum class Type
		{
			Unspecified,
			Toggle,
			List,
			Float
		};

		ParameterMetadata():
			parameterIndex(-1)
		{ }

		/** Construct with the parameter's enum index. */
		ParameterMetadata(int parameterIndex_)
			: parameterIndex(parameterIndex_)
		{}

		// --- Fluent builder methods (return by value for chaining) ---

		ParameterMetadata withId(const String& name) const
		{
			auto copy = *this;
			copy.id = Identifier(name);
			return copy;
		}

		ParameterMetadata withDescription(const String& desc) const
		{
			auto copy = *this;
			copy.description = desc;
			return copy;
		}

		ParameterMetadata withRange(scriptnode::InvertableParameterRange r) const
		{
			auto copy = *this;
			copy.range = r;
			if (copy.type == Type::Unspecified)
				copy.type = Type::Float;
			return copy;
		}

		/** Set the HiSlider display mode for editor setup. */
		ParameterMetadata withSliderMode(HiSlider::Mode m) const
		{
			auto copy = *this;
			copy.sliderMode = m;
			if (copy.type == Type::Unspecified)
				copy.type = Type::Float;
			return copy;
		}

		/** Convenience: set slider mode and custom range in one call.
		*	This mirrors the common editor pattern: setMode(Mode, NormalisableRange).
		*/
		ParameterMetadata withSliderMode(HiSlider::Mode m, scriptnode::InvertableParameterRange r) const
		{
			auto copy = *this;
			copy.sliderMode = m;
			copy.range = r;
			if (copy.type == Type::Unspecified)
				copy.type = Type::Float;
			return copy;
		}

		ParameterMetadata withDefault(float value) const
		{
			auto copy = *this;
			copy.defaultValue = value;
			return copy;
		}

		ParameterMetadata withValueToTextConverter(const String& mode) const
		{
			auto copy = *this;
			copy.vtc = ValueToTextConverter::createForMode(mode);
			return copy;
		}

		/** For combo-box / list parameters. Sets type to List and populates valueNames.
		*	startIndex defaults to 1 since HiComboBox passes attribute values straight
		*	through as JUCE ComboBox item IDs (which must be >= 1).
		*/
		ParameterMetadata withValueList(const StringArray& items, int startIndex = 1) const
		{
			auto copy = *this;
			copy.type = Type::List;
			copy.range = { (double)startIndex, (double)(startIndex + items.size() - 1), 1.0 };
			copy.vtc = ValueToTextConverter::createForOptions(items);
			return copy;
		}

		/** Marks this parameter as a toggle. Sets range to {0,1,1}. */
		ParameterMetadata asToggle() const
		{
			auto copy = *this;
			copy.type = Type::Toggle;
			copy.range = { 0.0, 1.0, 1.0 };
			return copy;
		}

		/** Marks this parameter as tempo-syncable.
		*
		*	Stores the index of the toggle parameter that controls whether this
		*	parameter operates in tempo-sync or free-running mode. The tempo-sync
		*	range, value names, and VTC are constant data derived at point of use
		*	(from TempoSyncer) rather than stored per-parameter.
		*/
		ParameterMetadata withTempoSyncMode(int tempoSyncParameterIndex) const
		{
			auto copy = *this;
			copy.tempoSyncControllerIndex = tempoSyncParameterIndex;
			return copy;
		}

		/** Convert to JSON for serialisation. */
		var toJSON() const;

		// --- Fields ---

		int parameterIndex = -1;
		Type type = Type::Unspecified;
		Identifier id;
		String description;
		scriptnode::InvertableParameterRange range;
		float defaultValue = 0.0f;
		ValueToTextConverter vtc;
		HiSlider::Mode sliderMode = HiSlider::numModes;

		// Index of the toggle parameter controlling tempo-sync mode (-1 = not syncable)
		int tempoSyncControllerIndex = -1;
	};

	/** Metadata for a modulation chain (internal child processor slot). */
	struct ModulationMetadata
	{
		ModulationMetadata() = default;

		/** Construct with the chain's enum index. */
		ModulationMetadata(int chainIndex_)
			: chainIndex(chainIndex_)
		{}

		ModulationMetadata withId(const String& name) const
		{
			auto copy = *this;
			copy.id = Identifier(name);
			return copy;
		}

		ModulationMetadata withDescription(const String& desc) const
		{
			auto copy = *this;
			copy.description = desc;
			return copy;
		}

		ModulationMetadata withColour(HiseModulationColours::ColourId colourId) const
		{
			auto copy = *this;
			copy.colour = colourId;
			return copy;
		}

		ModulationMetadata withMode(scriptnode::modulation::ParameterMode mode) const
		{
			auto copy = *this;
			copy.modulationMode = mode;
			return copy;
		}

		/** Convert to JSON for serialisation. */
		var toJSON() const;

		// --- Fields ---

		int chainIndex = -1;
		Identifier id;
		String description;
		HiseModulationColours::ColourId colour = HiseModulationColours::ColourId::Gain;
		scriptnode::modulation::ParameterMode modulationMode = scriptnode::modulation::ParameterMode::ScaleOnly;
	};

	ProcessorMetadata() = default;
	ProcessorMetadata(const Identifier& id_, DataType dt=DataType::StaticInitialised):
		id(id_),
		dataType(dt)
	{};

	// --- Fluent builder methods for the top-level struct ---

	ProcessorMetadata withId(const Identifier& typeId) const
	{
		auto copy = *this;
		copy.id = typeId;
		return copy;
	}

	ProcessorMetadata withPrettyName(const String& name) const
	{
		auto copy = *this;
		copy.prettyName = name;
		return copy;
	}

	ProcessorMetadata withDescription(const String& desc) const
	{
		auto copy = *this;
		copy.description = desc;
		return copy;
	}

	ProcessorMetadata withParameter(const ParameterMetadata& pd) const
	{
		auto copy = *this;
		copy.parameters.add(pd);
		return copy;
	}

	ProcessorMetadata withModulation(const ModulationMetadata& mod) const
	{
		auto copy = *this;
		copy.modulation.add(mod);
		return copy;
	}

	/** Set the processor type category from the C++ base class.
	*	Uses if-constexpr branching to resolve the type and subtype strings.
	*/
	template <typename T> ProcessorMetadata withType() const
	{
		auto copy = *this;

		if constexpr (std::is_base_of<VoiceStartModulator, T>())
		{
			copy.type = "Modulator";
			copy.subtype = "VoiceStartModulator";
		}
		else if constexpr (std::is_base_of<TimeVariantModulator, T>())
		{
			copy.type = "Modulator";
			copy.subtype = "TimeVariantModulator";
		}
		else if constexpr (std::is_base_of<EnvelopeModulator, T>())
		{
			copy.type = "Modulator";
			copy.subtype = "EnvelopeModulator";
		}
		else if constexpr (std::is_base_of<MasterEffectProcessor, T>())
		{
			copy.type = "Effect";
			copy.subtype = "MasterEffect";
		}
		else if constexpr (std::is_base_of<MonophonicEffectProcessor, T>())
		{
			copy.type = "Effect";
			copy.subtype = "MonophonicEffect";
		}
		else if constexpr (std::is_base_of<VoiceEffectProcessor, T>())
		{
			copy.type = "Effect";
			copy.subtype = "VoiceEffect";
		}
		else if constexpr (std::is_base_of<EffectProcessor, T>())
		{
			copy.type = "Effect";
			copy.subtype = "Effect";
		}
		else if constexpr (std::is_base_of<MidiProcessor, T>())
		{
			copy.type = "MidiProcessor";
			copy.subtype = "MidiProcessor";
		}
		else if constexpr (std::is_base_of<ModulatorSynth, T>())
		{
			copy.type = "SoundGenerator";
			copy.subtype = "SoundGenerator";
		}

		return copy;
	}

	/** Record which mix-in interfaces this processor implements. */
	template <typename T> ProcessorMetadata withInterface() const
	{
		auto copy = *this;

		if constexpr (std::is_base_of<RoutableProcessor, T>())
			copy.interfaceClasses.add("RoutableProcessor");
		if constexpr (std::is_base_of<AudioSampleProcessor, T>())
			copy.interfaceClasses.add("AudioSampleProcessor");
		if constexpr (std::is_base_of<LookupTableProcessor, T>())
			copy.interfaceClasses.add("LookupTableProcessor");
		if constexpr (std::is_base_of<SliderPackProcessor, T>())
			copy.interfaceClasses.add("SliderPackProcessor");

		return copy;
	}

	/** Creates a minimal fallback metadata from SET_PROCESSOR_NAME statics.
	*	Used for unmigrated processors in the registry.
	*/
	template <typename P> static ProcessorMetadata createFallback()
	{
		ProcessorMetadata md;
		md.id = P::getClassType();
		md.dataType = DataType::Fallback;
		md.prettyName = P::getClassName();
		return md;
	}

	// --- Editor setup methods ---

	/** Configure a HiSlider from the metadata for the given parameter index.
	*	Calls setup(), setMode(), and sets the tooltip from the description.
	*	Returns false if parameterIndex is out of range.
	*/
	bool setup(HiSlider& slider, Processor* p, int parameterIndex) const;

	/** Updates the slider mode based on tempo-sync state.
	*	Reads the processor and parameter index from the slider (set by setup()).
	*	Asserts if called on a parameter without tempoSyncControllerIndex.
	*/
	bool updateTempoSync(HiSlider& slider) const;

	/** Configure a HiComboBox from the metadata for the given parameter index.
	*	Calls setup() and populates the item list from valueNames.
	*	Returns false if parameterIndex is out of range.
	*/
	bool setup(HiComboBox& comboBox, Processor* p, int parameterIndex) const;

	/** Configure a HiToggleButton from the metadata for the given parameter index.
	*	Calls setup() and sets the tooltip from the description.
	*	Returns false if parameterIndex is out of range.
	*/
	bool setup(HiToggleButton& button, Processor* p, int parameterIndex) const;

	/** Convert the full metadata to JSON for serialisation. */
	var toJSON() const;

	/** Look up a ParameterMetadata by its parameter index.
	*	Returns nullptr if not found.
	*/
	const ParameterMetadata* getParameterById(int parameterIndex) const;

	/** Checks if the metadata is defined properly. */
	bool isValidAndNoFallback() const { return dataType > DataType::Fallback; }

	ParameterMetadata operator[](int index) const
	{
		if(isPositiveAndBelow(index, parameters.size()))
			return parameters[index];

		return {};
	}

	// --- Fields ---

	Identifier id;                        // e.g. "LFO" (from SET_PROCESSOR_NAME type)
	String prettyName;                    // e.g. "LFO Modulator" (display name)
	String description;                   // brief description
	Identifier type;                      // "Modulator", "Effect", "SoundGenerator", "MidiProcessor"
	Identifier subtype;                   // "TimeVariantModulator", "MasterEffect", etc.

	StringArray interfaceClasses;         // ["RoutableProcessor", "AudioSampleProcessor"]

	Array<ParameterMetadata> parameters;
	Array<ModulationMetadata> modulation;

	DataType dataType = DataType::Undefined;
};



} // namespace hise

#endif // PROCESSOR_METADATA_H
