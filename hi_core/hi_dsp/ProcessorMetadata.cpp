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
// ParameterMetadata::toJSON
// ============================================================================

var ProcessorMetadata::ParameterMetadata::toJSON() const
{
	auto obj = new DynamicObject();

	obj->setProperty("parameterIndex", parameterIndex);
	obj->setProperty("id", id.toString());
	obj->setProperty("description", description);
	obj->setProperty("defaultValue", defaultValue);

	// Type
	switch (type)
	{
	case Type::Toggle: obj->setProperty("type", "Toggle"); break;
	case Type::List:   obj->setProperty("type", "List"); break;
	case Type::Float:  obj->setProperty("type", "Float"); break;
	default:           obj->setProperty("type", "Unspecified"); break;
	}

	// Range
	{
		auto rangeObj = new DynamicObject();
		rangeObj->setProperty("min", range.rng.start);
		rangeObj->setProperty("max", range.rng.end);
		rangeObj->setProperty("interval", range.rng.interval);
		rangeObj->setProperty("skew", range.rng.skew);
		rangeObj->setProperty("inverted", range.inv);
		obj->setProperty("range", var(rangeObj));
	}

	// Value names (for List type, stored in vtc.itemList)
	if (!vtc.itemList.isEmpty())
	{
		Array<var> names;
		for (auto& n : vtc.itemList)
			names.add(n);
		obj->setProperty("valueNames", var(names));
	}

	// Slider mode (if set)
	if (sliderMode != HiSlider::numModes)
	{
		static const char* modeNames[] = {
			"Frequency", "Decibel", "Time", "TempoSync",
			"Linear", "Discrete", "Pan", "NormalizedPercentage"
		};

		if (isPositiveAndBelow(sliderMode, (int)HiSlider::numModes))
			obj->setProperty("sliderMode", modeNames[sliderMode]);
	}

	// Tempo sync dual-mode info (range/names are constant, derived from TempoSyncer)
	if (tempoSyncControllerIndex >= 0)
	{
		auto tsObj = new DynamicObject();
		tsObj->setProperty("controllerIndex", tempoSyncControllerIndex);

		Array<var> tsNames;
		for (auto& n : TempoSyncer::getTempoNames())
			tsNames.add(n);
		tsObj->setProperty("valueNames", var(tsNames));

		auto tsRange = new DynamicObject();
		tsRange->setProperty("min", 0.0);
		tsRange->setProperty("max", (double)(TempoSyncer::numTempos - 1));
		tsRange->setProperty("interval", 1.0);
		tsObj->setProperty("range", var(tsRange));

		obj->setProperty("tempoSync", var(tsObj));
	}

	return var(obj);
}

// ============================================================================
// ModulationMetadata::toJSON
// ============================================================================

var ProcessorMetadata::ModulationMetadata::toJSON() const
{
	auto obj = new DynamicObject();

	obj->setProperty("chainIndex", chainIndex);
	obj->setProperty("id", id.toString());
	obj->setProperty("description", description);

	static const char* modeNames[] = { "Disabled", "ScaleAdd", "ScaleOnly", "AddOnly", "Pan", "Pitch" };
	if (isPositiveAndBelow((int)modulationMode, (int)scriptnode::modulation::ParameterMode::numModulationModes))
		obj->setProperty("modulationMode", modeNames[(int)modulationMode]);

	obj->setProperty("colour", (int)colour);

	return var(obj);
}

// ============================================================================
// ProcessorMetadata::toJSON
// ============================================================================

var ProcessorMetadata::toJSON() const
{
	auto obj = new DynamicObject();

	obj->setProperty("id", id.toString());
	obj->setProperty("prettyName", prettyName);
	obj->setProperty("description", description);
	obj->setProperty("type", type.toString());
	obj->setProperty("subtype", subtype.toString());
	obj->setProperty("dataType", (int)dataType);

	if (!interfaceClasses.isEmpty())
	{
		Array<var> interfaces;
		for (auto& i : interfaceClasses)
			interfaces.add(i);
		obj->setProperty("interfaces", var(interfaces));
	}

	if (!parameters.isEmpty())
	{
		Array<var> params;
		for (auto& p : parameters)
			params.add(p.toJSON());
		obj->setProperty("parameters", var(params));
	}

	if (!modulation.isEmpty())
	{
		Array<var> mods;
		for (auto& m : modulation)
			mods.add(m.toJSON());
		obj->setProperty("modulation", var(mods));
	}

	return var(obj);
}

// ============================================================================
// ProcessorMetadata::getParameterById
// ============================================================================

const ProcessorMetadata::ParameterMetadata* ProcessorMetadata::getParameterById(int parameterIndex) const
{
	for (auto& p : parameters)
	{
		if (p.parameterIndex == parameterIndex)
			return &p;
	}

	return nullptr;
}

// ============================================================================
// ProcessorMetadata::setup (HiSlider)
// ============================================================================

bool ProcessorMetadata::setup(HiSlider& slider, Processor* p, int parameterIndex) const
{
	auto pd = getParameterById(parameterIndex);

	if (pd == nullptr)
		return false;

	slider.setup(p, parameterIndex, pd->id.toString());

	// Set mode with custom range if we have both
	if (pd->sliderMode != HiSlider::numModes)
	{
		auto nr = NormalisableRange<double>(pd->range.rng.start, pd->range.rng.end, pd->range.rng.interval);
		nr.skew = pd->range.rng.skew;
		slider.setMode(pd->sliderMode, nr);
	}

	if (!pd->description.isEmpty())
		slider.setTooltip(pd->description);

	return true;
}

// ============================================================================
// ProcessorMetadata::setup (HiComboBox)
// ============================================================================

bool ProcessorMetadata::setup(HiComboBox& comboBox, Processor* p, int parameterIndex) const
{
	auto pd = getParameterById(parameterIndex);

	if (pd == nullptr)
		return false;

	comboBox.setup(p, parameterIndex, pd->id.toString());

	// Populate combo box items with IDs matching the attribute value range
	comboBox.clear(dontSendNotification);
	int startIndex = (int)pd->range.rng.start;
	for (int i = 0; i < pd->vtc.itemList.size(); i++)
		comboBox.addItem(pd->vtc.itemList[i], startIndex + i);

	if (!pd->description.isEmpty())
		comboBox.setTooltip(pd->description);

	return true;
}

// ============================================================================
// ProcessorMetadata::setup (HiToggleButton)
// ============================================================================

bool ProcessorMetadata::setup(HiToggleButton& button, Processor* p, int parameterIndex) const
{
	auto pd = getParameterById(parameterIndex);

	if (pd == nullptr)
		return false;

	button.setup(p, parameterIndex, pd->id.toString());

	if (!pd->description.isEmpty())
		button.setTooltip(pd->description);

	return true;
}

bool ProcessorMetadata::updateTempoSync(HiSlider& slider) const
{
	auto p = slider.getProcessor();
	auto parameterIndex = slider.getParameter();

	if (auto pd = getParameterById(parameterIndex))
	{
		auto ts = pd->tempoSyncControllerIndex;

		if (ts == -1)
		{
			// Only call this with tempo-syncable parameters
			jassertfalse;
			return false;
		}

		if (p->getAttribute(ts) > 0.5f)
		{
			slider.setMode(HiSlider::Mode::TempoSync);
		}
		else
		{
			auto nr = NormalisableRange<double>(pd->range.rng.start, pd->range.rng.end, pd->range.rng.interval);
			nr.skew = pd->range.rng.skew;
			slider.setMode(pd->sliderMode, nr);
		}

		return true;
	}

	return false;
}

} // namespace hise
