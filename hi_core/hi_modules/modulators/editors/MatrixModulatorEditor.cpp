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


namespace hise { using namespace juce;




MatrixModulatorBody::RangeEditor::Editor::Editor(ValueTree v, const Identifier& id_, UndoManager* um_):
	rangeTree(v),
	id(id_.toString()),
	f(GLOBAL_BOLD_FONT()),
	um(um_) 
{
	if(id == MatrixIds::TextConverter.toString())
	{
		selector.addItemList(ValueToTextConverter::getAvailableTextConverterModes(), 1);
		selector.setLookAndFeel(&laf);
		laf.setDefaultColours(selector);
		addAndMakeVisible(selector);
		selector.onChange = [this]() { update(selector.getText()); };
		selector.setTextWhenNothingSelected("Select mode");
	}
	else if (id == MatrixIds::UseMidPositionAsZero.toString())
	{
		addAndMakeVisible(zeroToggle);
		zeroToggle.setLookAndFeel(&laf);
		zeroToggle.onClick = [this]() { update(zeroToggle.getToggleState()); };
		laf.setDefaultColours(zeroToggle);
	}
	else
	{
		editor.setFont(GLOBAL_FONT());
		editor.onReturnKey = [this](){ update(editor.getText()); };
		editor.onFocusLost = [this](){ update(editor.getText()); };

		addAndMakeVisible(editor);
		GlobalHiseLookAndFeel::setTextEditorColours(editor);
	}

	listener.setCallback(v, { id}, valuetree::AsyncMode::Asynchronously, BIND_MEMBER_FUNCTION_2(Editor::onUpdate));
}

void MatrixModulatorBody::RangeEditor::Editor::paint(Graphics& g)
{
	g.setColour(Colours::white.withAlpha(0.7f));
	g.setFont(f);
	g.drawText(id, tb, Justification::centred);
}

void MatrixModulatorBody::RangeEditor::Editor::resized()
{
	auto b = getLocalBounds();
	tb = b.removeFromLeft(f.getStringWidthFloat(id) + 10).toFloat();
	getChildComponent(0)->setBounds(b);
}

void MatrixModulatorBody::RangeEditor::Editor::update(const var& newValue)
{
	jassert(rangeTree.hasProperty(id));
	auto v = newValue.toString();
	rangeTree.setProperty(id, newValue, um);
}

MatrixModulatorBody::RangeEditor::RangeEditor(ValueTree rangeTree, UndoManager* um):
	data(rangeTree)
{
	for(const auto& id: MatrixModulator::getRangeIds(rangeTree.getType() == MatrixIds::InputRange))
	{
		auto ne = new Editor(rangeTree, id, um);
		addAndMakeVisible(ne);
		editors.add(ne);
	}
}

void MatrixModulatorBody::RangeEditor::paint(Graphics& g)
{
	auto t = data.getType().toString();
	g.setFont(GLOBAL_BOLD_FONT());
	g.setColour(Colours::white.withAlpha(0.9f));
	g.drawText(t, getLocalBounds().removeFromLeft(80).toFloat(), Justification::left);
}

void MatrixModulatorBody::RangeEditor::resized()
{
	auto b = getLocalBounds();
	b.removeFromLeft(80);
	auto w = b.getWidth() / editors.size();

	for(auto e: editors)
	{
		e->calculateWidth(getHeight());

		auto tw = (int)e->tb.getWidth() + 90;
		e->setBounds(b.removeFromLeft(tw));
	}
}

ValueTree MatrixModulatorBody::createValueTreeFromRange(Processor* p, bool isInputData)
{
	auto mm = static_cast<MatrixModulator*>(p);
	return mm->getRangeData(isInputData);
}

MatrixModulatorBody::MatrixModulatorBody(ProcessorEditor* parent):
	ProcessorEditorBody(parent),
	ControlledObject(parent->getProcessor()->getMainController()),
	valueSlider("Value"),
	smoothingSlider("SmoothingTime"),
	content(getMainController(), parent->getProcessor()->getId(), false),
	controller(getMainController(), *this),
	inputRangeEditor(createValueTreeFromRange(getProcessor(), true), getMainController()->getControlUndoManager()),
	outputRangeEditor(createValueTreeFromRange(getProcessor(), false), getMainController()->getControlUndoManager()),
    editRangeButton("Edit value range"),
	autofixButton("Presets")
{
	auto p = dynamic_cast<MatrixModulator*>(getProcessor());
	valueSlider.setup(p, MatrixModulator::Value, "Value");
	valueSlider.setSliderStyle(Slider::SliderStyle::RotaryHorizontalVerticalDrag);

	auto obj = new DynamicObject();

	obj->setProperty("ContextMenu", 0);
	obj->setProperty("ScaleModulation", (int)ModifierKeys::rightButtonModifier);

	valueSlider.setModifierObject(var(obj));
		
	smoothingSlider.setup(p, MatrixModulator::SmoothingTime, "Smoothing");
	smoothingSlider.setMode(HiSlider::Mode::Time, { 0.0, 200.0, 1.0 });
	smoothingSlider.setSliderStyle(Slider::SliderStyle::RotaryHorizontalVerticalDrag);

	controller.setValueTree(dynamic_cast<MatrixModulator*>(parent->getProcessor())->globalMatrixData, 
	                        parent->getProcessor()->getId(), 
	                        getMainController()->getControlUndoManager());

	addAndMakeVisible(smoothingSlider);
	addAndMakeVisible(valueSlider);
	addAndMakeVisible(content);
	addAndMakeVisible(controller);

	addChildComponent(inputRangeEditor);
	addChildComponent(outputRangeEditor);
	addChildComponent(autofixButton);
	addAndMakeVisible(editRangeButton);
	laf.setDefaultColours(editRangeButton);
	

	autofixButton.onClick = [this]()
	{
		PopupMenu m;
		m.setLookAndFeel(&laf);

		m.addSectionHeader("Load range preset");

		enum class Presets
		{
			NormalizedPercentage,
			Gain0dB,
			Gain6dB,
			Pitch1Octave,
			Pitch2Octaves,
			Pitch1Semitone,
			FilterFreq,
			FilterFreqLog,
			Stereo,
			numPresets
		};

		static const StringArray names = {
			"NormalizedPercentage",
			"Gain +0dB",
			"Gain +6dB",
			"Pitch +-1 Octave",
			"Pitch +-2 Octaves",
			"Pitch +-1 Semitone",
			"FilterFreq",
			"FilterFreq Logarithmic",
			"Stereo"
		};

		std::array<bool, (int)Presets::numPresets> allowed = {
			true,
			true,
			true,
			true,
			true,
			true,
			true,
			true,
			true
		};

		auto mod = dynamic_cast<MatrixModulator*>(this->getProcessor());

		if(mod->getMode() == Modulation::PitchMode)
		{
			for(auto& m: allowed)
				m = false;

			allowed[(int)Presets::Pitch1Octave] = true;
			allowed[(int)Presets::Pitch2Octaves] = true;
			allowed[(int)Presets::Pitch1Semitone] = true;
		}
		else
		{
			allowed[(int)Presets::Pitch1Octave] = false;
			allowed[(int)Presets::Pitch2Octaves] = false;
			allowed[(int)Presets::Pitch1Semitone] = false;
		}
		

		for(int i = 0; i < (int)Presets::numPresets; i++)
		{
			m.addItem(1 + i, names[i], allowed[i], false);
		}

		if(auto r = m.show())
		{
			auto p = (Presets)(r - 1);

			InvertableParameterRange inp, out;
			String converter;
			bool useMidPositionAsZero = false;

			switch(p)
			{
			case Presets::NormalizedPercentage:
				inp = { 0.0, 1.0};
				out = { 0.0, 1.0};
				converter = "NormalizedPercentage";
				break;
			case Presets::Gain0dB:
				inp = { -100.0, 0.0};
				inp.setSkewForCentre(-6.0);
				out = { 0.0, 1.0};
				converter = "Decibel";
				break;
			case Presets::Gain6dB:
				inp = { -100.0, 6.0};
				inp.setSkewForCentre(0.0);
				out = { 0.0, 2.0};
				converter = "Decibel";
				break;
			case Presets::Pitch1Octave:
				inp = { -12.0, 12.0};
				inp.rng.interval = 1.0;
				out = { -1.0, 1.0};
				converter = "Semitones";
				useMidPositionAsZero = true;
				break;
			case Presets::Pitch2Octaves:
				inp = { -24.0, 24.0};
				inp.rng.interval = 1.0;
				out = { -2.0, 2.0};
				converter = "Semitones";
				useMidPositionAsZero = true;
				break;
			case Presets::Pitch1Semitone:
				inp = { -1.0, 1.0};
				inp.rng.interval = 0.01;
				out = { -1.0 / 12.0, 1.0 / 12.0};
				converter = "Semitones";
				useMidPositionAsZero = true;
				break;
			case Presets::FilterFreq:
				inp = { 20.0, 20000.0};
				out = { 0.0, 1.0};
				converter = "Frequency";
				break;
			case Presets::FilterFreqLog:
				inp = { 20.0, 20000.0};
				inp.rng.skew = 0.2998514912584123; // use 2khz as center point
				out = { 0.0, 1.0};
				out.rng.skew = 0.2998514912584123;
				converter = "Frequency";
				break;
			case Presets::Stereo:
				inp = { -100.0, 100.0};
				out = { 0.0, 1.0 };
				converter = "Pan";
				useMidPositionAsZero = true;
			case Presets::numPresets:
				break;
			default: ;
			}

			

			auto um = getMainController()->getControlUndoManager();
			auto set = RangeHelpers::IdSet::ScriptComponents;

			auto ir = mod->getRangeData(true);
			auto outr = mod->getRangeData(false);

			RangeHelpers::storeDoubleRange(ir, inp, um, set);
			RangeHelpers::storeDoubleRange(outr, out, um, set);
			ir.setProperty(MatrixIds::TextConverter, converter, um);
			outr.setProperty(MatrixIds::UseMidPositionAsZero, useMidPositionAsZero, um);

			if(mod->getMode() == Modulation::PitchMode)
			{
				mod->setIsBipolar(false);
				mod->setIntensity(1.0);
			}

			if(mod->getMode() == Modulation::PanMode)
			{
				mod->setIsBipolar(true);
				mod->setIntensity(1.0);
			}
		}
	};

	autofixButton.setLookAndFeel(&laf);

	editRangeButton.setLookAndFeel(&laf);

	editRangeButton.onClick = [this]()
	{
		auto on = editRangeButton.getToggleState();
		content.setVisible(!on);
		inputRangeEditor.setVisible(on);
		outputRangeEditor.setVisible(on);
		autofixButton.setVisible(on);
	};

	content.setLookAndFeel(&laf);
	content.resizeFunction = [this]()
	{
		refreshBodySize();
	};

	controller.setCSS(css);

	inputRangeListener.setCallback(inputRangeEditor.data, MatrixModulator::getRangeIds(true), valuetree::AsyncMode::Synchronously,
	                               [this](const Identifier& id, const var& newValue)
	                               {
		                               updateRangeProperty(true, id.toString(), newValue);
	                               });

	outputRangeListener.setCallback(outputRangeEditor.data, MatrixModulator::getRangeIds(false), valuetree::AsyncMode::Synchronously,
	                                [this](const Identifier& id, const var& newValue)
	                                {
		                                updateRangeProperty(false, id.toString(), newValue);
	                                });
}

void MatrixModulatorBody::updateRangeProperty(bool isInputRange, const Identifier& id, const var& newValue)
{
	updateValueSlider();
	return;

#if 0
	auto mm = dynamic_cast<MatrixModulator*>(getProcessor());

	if(id == MatrixIds::TextConverter)
	{
		mm->vtcMode = newValue.toString();
		updateValueSlider();
		return;
	}
		
	auto value = (double)newValue;
	FloatSanitizers::sanitizeDoubleNumber(value);

	auto r = isInputRange ? mm->inputRange : mm->outputRange;
	auto intervalToUse = r.rng.interval;

	if(intervalToUse == 0.0)
		intervalToUse = 0.001;

	if(id.toString() == "min")
	{
		r.rng.start = jmin(r.rng.end - intervalToUse, value);
	}
	if(id.toString() == "max")
	{
		r.rng.end = jmax(r.rng.start + intervalToUse, value);
	}
	if(id.toString() == "middlePosition")
	{
		if(r.rng.getRange().contains(value) && r.rng.start != value)
			r.rng.setSkewForCentre(value);
	}
	if(id.toString() == "stepSize")
	{
		r.rng.interval = value;
	}

	mm->setValueRange(isInputRange, r);
	updateValueSlider();
#endif
}

void MatrixModulatorBody::updateValueSlider()
{
	auto mm = dynamic_cast<MatrixModulator*>(getProcessor());

	auto vtc = ValueToTextConverter::createForMode(mm->vtcMode);

	valueSlider.setNormalisableRange(mm->inputRange.rng);
	valueSlider.valueFromTextFunction = vtc;
	valueSlider.textFromValueFunction = vtc;
	valueSlider.updateText();

	for(auto r: content.rows)
	{
		r->setIntensityConverter(mm);
	}
}

void MatrixModulatorBody::updateGui()
{
	valueSlider.updateValue();
	smoothingSlider.updateValue();
}

void MatrixModulatorBody::paint(Graphics& g)
{
	auto tb = getLocalBounds().reduced(Margin).removeFromTop(TopRowHeight);
	g.setColour (Colour (0xAAFFFFFF));
	g.setFont (GLOBAL_BOLD_FONT().withHeight(26.0f));
	g.drawText(getProcessor()->getName().toLowerCase(), tb.toFloat(), Justification::centredRight);
}

void MatrixModulatorBody::resized()
{
	auto b = getLocalBounds().reduced(2 * Margin, Margin);
	auto top = b.removeFromTop(TopRowHeight);
	auto bottom = b.removeFromBottom(controller.getHeightToUse(getWidth()));

	top.removeFromBottom(16);
	
	valueSlider.setBounds(top.removeFromLeft(128));
	top.removeFromLeft(2*Margin);
	smoothingSlider.setBounds(top.removeFromLeft(128));
	top.removeFromLeft(2*Margin);
	editRangeButton.setBounds(top.removeFromLeft(128).reduced(0, 10));

	content.setBounds(b);

	b.removeFromTop(Margin);
	inputRangeEditor.setBounds(b.removeFromTop(28));
	b.removeFromTop(Margin);

	auto outputRow = b.removeFromTop(28);
	autofixButton.setBounds(outputRow.removeFromRight(100));
	outputRangeEditor.setBounds(outputRow);
	

	controller.setBounds(bottom);
}

int MatrixModulatorBody::getBodyHeight() const
{
	int h = 0;
	h += Margin;
	h += TopRowHeight;
	h += Margin;
	h += content.getHeightToUse(-1);
	h += Margin;
	h += HeaderHeight;
	h += const_cast<MatrixContent::Controller*>(&controller)->getHeightToUse(getWidth());
	h += Margin;
	return h;
}



} // namespace hise
