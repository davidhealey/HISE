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

namespace scriptnode {

using namespace hise;
using namespace juce;

simple_visualiser::simple_visualiser(mothernode*, PooledUIUpdater* u) :
	ScriptnodeExtraComponent<mothernode>(nullptr, u)
{
	setSize(100, 60);
}

double simple_visualiser::getParameter(int index)
{
	if (auto nc = findParentComponentOfClass<ParameterSourceObject>())
		return nc->getParameterValue(index);

	return 0.0;
}

InvertableParameterRange simple_visualiser::getParameterRange(int index)
{
	if (auto nc = findParentComponentOfClass<ParameterSourceObject>())
		return nc->getParameterRange(index);

	return {};
}



void simple_visualiser::timerCallback()
{
	original.clear();
	p.clear();
	gridPath.clear();
	rebuildPath(p);

	auto b = getLocalBounds().toFloat().reduced(4.0f);

	if (!p.getBounds().isEmpty())
		p.scaleToFit(b.getX(), b.getY(), b.getWidth(), b.getHeight(), false);

	if (!original.getBounds().isEmpty())
	{
		original.scaleToFit(b.getX(), b.getY(), b.getWidth(), b.getHeight(), false);

		auto cp = original;

		float l[2] = { 2.0f, 2.0f };
		PathStrokeType(1.5f).createDashedStroke(original, cp, l, 2);
	}

	if (!gridPath.getBounds().isEmpty())
	{
		gridPath.scaleToFit(b.getX(), b.getY(), b.getWidth(), b.getHeight(), false);
	}

	repaint();
}

void simple_visualiser::paint(Graphics& g)
{
	if (drawBackground)
		ScriptnodeComboBoxLookAndFeel::drawScriptnodeDarkBackground(g, getLocalBounds().toFloat(), false);

	if (!gridPath.isEmpty())
	{
		UnblurryGraphics ug(g, *this);

		g.setColour(Colours::black.withAlpha(0.5f));
		g.strokePath(gridPath, PathStrokeType(ug.getPixelSize()));
	}

	g.setColour(getNodeColour());

	if (!original.isEmpty())
		g.fillPath(original);

	g.strokePath(p, PathStrokeType(thickness));
}

namespace control {

xfader_editor::xfader_editor(mothernode* f, PooledUIUpdater* updater) :
	ScriptnodeExtraComponent<mothernode>(f, updater),
	faderSelector("Linear")
{
	addAndMakeVisible(faderSelector);
	setSize(256, 120);
}

void xfader_editor::initialise(ObjectWithValueTree* o)
{
	MultiOutputBase::initialiseOutputs(o);

	faderSelector.initModes(obj.getFaderModes(), o);
	obj.initialise(o);

	graphRebuilder.setCallback(o->getValueTree(), { PropertyIds::NumParameters, PropertyIds::Value }, valuetree::AsyncMode::Asynchronously, [this](ValueTree, Identifier)
		{
			this->rebuildFaderCurves();
		});

	rebuildFaderCurves();
}

void xfader_editor::paint(Graphics& g)
{
	auto b = getLocalBounds().toFloat();
	b.removeFromTop(34.0f);

	ScriptnodeComboBoxLookAndFeel::drawScriptnodeDarkBackground(g, b, false);

	auto xPos = (float)b.getWidth() * inputValue;

	Line<float> posLine(xPos, b.getY() + 2.0f, xPos, (float)b.getBottom() - 2.0f);
	Line<float> scanLine(xPos, b.getY() + 5.0f, xPos, (float)b.getBottom() - 5.0f);

	int index = 0;

	g.setColour(Colours::white.withAlpha(0.3f));
	g.drawLine(posLine);

	for (auto& p : faderCurves)
	{
		auto c = getFadeColour(index++, faderCurves.size());
		g.setColour(c.withMultipliedAlpha(0.7f));

		g.fillPath(p);
		g.setColour(c);
		g.strokePath(p, PathStrokeType(1.0f));

	}

	index = 0;

	for (auto& p : faderCurves)
	{
		auto c = getFadeColour(index++, faderCurves.size());
		auto clippedLine = p.getClippedLine(scanLine, false);

		Point<float> circleCenter;

		if (clippedLine.getStartY() > clippedLine.getEndY())
			circleCenter = clippedLine.getEnd();
		else
			circleCenter = clippedLine.getStart();

		if (!circleCenter.isOrigin())
		{
			Rectangle<float> circle(circleCenter, circleCenter);
			g.setColour(c.withAlpha(1.0f));
			g.fillEllipse(circle.withSizeKeepingCentre(5.0f, 5.0f));
		}
	}
}

void xfader_editor::setInputValue(double v)
{
	if(v != inputValue)
	{
		inputValue = v;
		repaint();
	}
}

void xfader_editor::rebuildFaderCurves()
{
	int numPaths = getNumOutputs();

	faderCurves.clear();

	if (numPaths > 0)
		faderCurves.add(createPath<0>());
	if (numPaths > 1)
		faderCurves.add(createPath<1>());
	if (numPaths > 2)
		faderCurves.add(createPath<2>());
	if (numPaths > 3)
		faderCurves.add(createPath<3>());
	if (numPaths > 4)
		faderCurves.add(createPath<4>());
	if (numPaths > 5)
		faderCurves.add(createPath<5>());
	if (numPaths > 6)
		faderCurves.add(createPath<6>());
	if (numPaths > 7)
		faderCurves.add(createPath<7>());

	resized();
}

void xfader_editor::resized()
{
	auto b = getLocalBounds().reduced(2).toFloat();

	auto top = b.removeFromTop(24).toNearestInt();

	b.removeFromTop(10.0f);

	faderSelector.setBounds(top);

	float maxHeight = 0.0f;

	for (const auto& p : faderCurves)
		maxHeight = jmax(maxHeight, (float)p.getBounds().getHeight());

	if (!b.isEmpty())
	{
		for (auto& p : faderCurves)
		{
			auto sf = p.getBounds().getHeight() / maxHeight;

			if (!p.getBounds().isEmpty())
			{
				auto thisMaxHeight = b.getHeight() * sf;
				auto offset = b.getY() + b.getHeight() - thisMaxHeight;
				p.scaleToFit(b.getX(), offset, b.getWidth(), b.getHeight() * sf, false);
			}

		}

		repaint();
	}
}

void xfader_editor::timerCallback()
{
	double v = 0.0;

	if(auto psource = findParentComponentOfClass<ParameterSourceObject>())
	{
		setInputValue(psource->getParameterValue(0));
	}	
}

intensity_editor::intensity_editor(mothernode* b, PooledUIUpdater* u) :
	ScriptnodeExtraComponent(b, u)
{
	setSize(256, 256);
	setWatchedParameters({ 0, 1 });
	start();
}

void intensity_editor::paint(Graphics& g)
{
	ScriptnodeComboBoxLookAndFeel::drawScriptnodeDarkBackground(g, pathArea, false);

	UnblurryGraphics ug(g, *this, true);

	g.setColour(Colours::white.withAlpha(0.1f));

	auto pb = pathArea.reduced(UIValues::NodeMargin / 2);

	ug.draw1PxHorizontalLine(pathArea.getCentreY(), pb.getX(), pb.getRight());
	ug.draw1PxVerticalLine(pathArea.getCentreX(), pb.getY(), pb.getBottom());
	ug.draw1PxRect(pb);

	g.setColour(getNodeColour());

	Path dst;

	auto ps = ug.getPixelSize();
	float l[2] = { 4.0f * ps, 4.0f * ps };

	PathStrokeType(2.0f * ps).createDashedStroke(dst, fullPath, l, 2);


	g.fillPath(dst);
	g.strokePath(valuePath, PathStrokeType(4.0f * ug.getPixelSize()));
}

void intensity_editor::rebuildPaths()
{
	fullPath.clear();
	valuePath.clear();

	fullPath.startNewSubPath(1.0f, 0.0f);
	valuePath.startNewSubPath(1.0f, 0.0f);

	fullPath.startNewSubPath(0.0f, 1.0f);
	valuePath.startNewSubPath(0.0f, 1.0f);

	auto v = getWatchedParameterValue(0);
	auto iv = getWatchedParameterValue(1);

	fullPath.lineTo(0.0f, iv);
	valuePath.lineTo(0.0f, iv);

	fullPath.lineTo(1.0, 0.0f);

	auto valuePos1 = iv;
	auto valuePos2 = 0.0;

	valuePath.lineTo(v, (float)Interpolator::interpolateLinear(valuePos1, valuePos2, v));

	auto pb = pathArea.reduced(UIValues::NodeMargin);

	fullPath.scaleToFit(pb.getX(), pb.getY(), pb.getWidth(), pb.getHeight(), false);
	valuePath.scaleToFit(pb.getX(), pb.getY(), pb.getWidth(), pb.getHeight(), false);

	repaint();
}

void intensity_editor::timerCallback()
{
	if (checkWatchedParameters())
		rebuildPaths();
}

void intensity_editor::resized()
{
	auto b = getLocalBounds();

	pathArea = b.toFloat();
	pathArea = pathArea.withSizeKeepingCentre(pathArea.getHeight(), pathArea.getHeight()).reduced(10.0f);
	rebuildPaths();
}

blend_editor::blend_editor(mothernode* b, PooledUIUpdater* u) :
	ScriptnodeExtraComponent<mothernode>(b, u)
{
	setWatchedParameters({ 0, 1, 2 });

	setSize(256 - 32, 50);
}

void blend_editor::paint(Graphics& g)
{
	multilogic::blend bl;
	bl.setParameter<0>(getWatchedParameterValue(0));
	bl.setParameter<1>(getWatchedParameterValue(1));
	bl.setParameter<2>(getWatchedParameterValue(2));

	auto alpha = bl.alpha * 2.0f - 1.0f;

	auto b = getLocalBounds().removeFromRight((getWidth() * 2) / 3).toFloat();

	auto area = b.reduced(JUCE_LIVE_CONSTANT(40), 15).toFloat();

	ScriptnodeComboBoxLookAndFeel::drawScriptnodeDarkBackground(g, area, true);

	area = area.reduced(4);

	auto w = (area.getWidth() - area.getHeight()) * 0.5f;

	auto tb = area.translated(0, 20);

	area = area.withSizeKeepingCentre(area.getHeight(), area.getHeight());

	area = area.translated(alpha * w, 0);

	g.setColour(getNodeColour());

	g.fillEllipse(area);

	g.setFont(GLOBAL_BOLD_FONT());

	g.drawText(String(bl.getValue(), 2), tb, Justification::centred);
}

void blend_editor::timerCallback()
{
	if (checkWatchedParameters())
	{
		repaint();
	}
}

converter_editor::converter_editor(mothernode* p, PooledUIUpdater* updater) :
	ScriptnodeExtraComponent<mothernode>(p, updater),
	modeSelector(data.getConverterNames()[0])
{
	addAndMakeVisible(modeSelector);
	setSize(128, 24 + 30);

	setWatchedParameters({ 0 });

	modeSelector.addListener(this);
}

void converter_editor::setRange(NormalisableRange<double> nr, double center /*= -90.0*/)
{
	auto p = node->getValueTree().getChildWithName(PropertyIds::Parameters).getChild(0);

	if (center != -90.0)
		nr.setSkewForCentre(center);

	InvertableParameterRange r;
	r.rng = nr;

	RangeHelpers::storeDoubleRange(p, r, node->getUndoManager());
	repaint();
}

void converter_editor::paint(Graphics& g)
{
	g.setColour(Colours::white.withAlpha(0.5f));
	g.setFont(GLOBAL_BOLD_FONT());

	auto v = getWatchedParameterValue(0);
	data.prepare(node->getLastPrepareSpecs());
	auto m = (Mode)data.getConverterNames().indexOf(modeSelector.getText());
	data.currentMode = m;
	auto output = data.getValue(v);
	

	String inputDomain, outputDomain;

	switch (m)
	{
	case Mode::Ms2Freq: inputDomain = "ms"; outputDomain = "Hz"; break;
	case Mode::Freq2Ms: inputDomain = "Hz"; outputDomain = "ms"; break;
	case Mode::Freq2Samples: inputDomain = "Hz"; outputDomain = "smp"; break;
	case Mode::Ms2Samples:  inputDomain = "ms"; outputDomain = " smp"; break;
	case Mode::Samples2Ms:  inputDomain = "smp"; outputDomain = "ms"; break;
	case Mode::Ms2BPM:    inputDomain = "ms"; outputDomain = "BPM"; break;
	case Mode::Pitch2St:  inputDomain = ""; outputDomain = "st"; break;
	case Mode::St2Pitch:  inputDomain = "st"; outputDomain = ""; break;
	case Mode::Cent2Pitch: inputDomain = "ct"; outputDomain = ""; break;
	case Mode::Pitch2Cent: inputDomain = ""; outputDomain = "ct"; break;
	case Mode::Midi2Freq:  inputDomain = ""; outputDomain = "Hz"; break;
	case Mode::Freq2Norm:  inputDomain = "Hz"; outputDomain = ""; break;
	case Mode::Gain2dB: inputDomain = ""; outputDomain = "dB"; break;
	case Mode::dB2Gain: inputDomain = "dB"; outputDomain = ""; break;
	default: break;
	}

	String s;
	s << snex::Types::Helpers::getCppValueString(v);
	s << inputDomain << " -> ";
	s << snex::Types::Helpers::getCppValueString(output) << outputDomain;
	g.drawText(s, textArea, Justification::centred);
}

void converter_editor::comboBoxChanged(ComboBox* b)
{
	auto m = (Mode)data.getConverterNames().indexOf(b->getText());

	switch (m)
	{
	case Mode::Ms2Freq: setRange({ 0.0, 1000.0, 1.0 }); break;
	case Mode::Freq2Ms: setRange({ 20.0, 20000.0, 0.1 }, 1000.0); break;
	case Mode::Freq2Samples: setRange({ 20.0, 20000.0, 0.1 }, 1000.0); break;
	case Mode::Ms2Samples:  setRange({ 0.0, 1000.0, 1.0 }); break;
	case Mode::Samples2Ms:  setRange({ 0.0, 44100.0, 1.0 }); break;
	case Mode::Pitch2St:  setRange({ 0.5, 2.0 }, 1.0); break;
	case Mode::Ms2BPM:    setRange({ 0.0, 2000.0, 1.0 }); break;
	case Mode::St2Pitch:  setRange({ -12.0, 12.0, 1.0 }); break;
	case Mode::Pitch2Cent: setRange({ 0.5, 2.0 }, 1.0); break;
	case Mode::Cent2Pitch: setRange({ -100.0, 100.0, 0.0 }); break;
	case Mode::Midi2Freq:  setRange({ 0, 127.0, 1.0 }); break;
	case Mode::Freq2Norm:  setRange({ 0.0, 20000.0 }); break;
	case Mode::Gain2dB: setRange({ 0.0, 1.0, 0.0 }); break;
	case Mode::dB2Gain: setRange({ -100.0, 0.0, 0.1 }, -12.0); break;
	default: break;
	}

	repaint();
}

void converter_editor::initialise(ObjectWithValueTree* o)
{
	node = o;
	modeSelector.initModes(data.getConverterNames(), o);
}

void converter_editor::timerCallback()
{
	if (checkWatchedParameters())
		repaint();
}

void converter_editor::resized()
{
	auto b = getLocalBounds();
	modeSelector.setBounds(b.removeFromTop(24));
	textArea = b.toFloat();
}

}

UIFactory::Data::Data()
{
	registerComponent<fx::bitcrush_editor>("fx.bitcrush");
	registerComponent<fx::sampleandhold_editor>("fx.sampleandhold");
	registerComponent<fx::phase_delay_editor>("fx.phasedelay");
	registerComponent<fx::reverb_editor>("fx.reverb");
	registerComponent<math::map_editor>("math.map");
	registerComponent<control::intensity_editor>("control.intensity");
	registerComponent<control::xfader_editor>("control.xfader");
	registerComponent<control::blend_editor>("control.blend");
	registerComponent<control::converter_editor>("control.converter");
}



}