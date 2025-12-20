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


namespace control {

xfader_editor::xfader_editor(mothernode* f, PooledUIUpdater* updater) :
	ScriptnodeExtraComponent<mothernode>(f, updater),
	faderSelector("Linear"),
	numOutputs("2", PropertyIds::NumParameters)
{
	addAndMakeVisible(faderSelector);
	addAndMakeVisible(numOutputs);
	setSize(200, 120);
}

void xfader_editor::setInputValue(double v)
{
	if(v != inputValue)
	{
		inputValue = v;
		repaint();
	}
}

void xfader_editor::initialise(ObjectWithValueTree* o)
{
	MultiOutputBase::initialiseOutputs(o);

	faderSelector.initModes(obj.getFaderModes(), o);

	numOutputs.initModes({ "2", "3", "4", "5", "6", "7", "8" }, o);

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

	numOutputs.setBounds(top.removeFromRight(80));
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

	if (auto psource = findParentComponentOfClass<ParameterSourceObject>())
	{
		setInputValue(psource->getParameterValue(0));
	}
}

intensity_editor::intensity_editor(mothernode* b, PooledUIUpdater* u) :
	ScriptnodeExtraComponent(b, u)
{
	setSize(256, 256);
	setLayout(ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween);
	setWatchedParameters({ 0, 1 });
	start();
}

void intensity_editor::onLayoutChange()
{
	if (getPreferredLayout() == ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween)
		setSize(NewParameterHeight * 2, NewParameterHeight * 2);
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

	auto pb = pathArea.reduced(UIValues::NodeMargin/2);

	fullPath.scaleToFit(pb.getX(), pb.getY(), pb.getWidth(), pb.getHeight(), false);
	valuePath.scaleToFit(pb.getX(), pb.getY(), pb.getWidth(), pb.getHeight(), false);

	repaint();
}

void intensity_editor::resized()
{
	auto b = getLocalBounds();

	pathArea = b.toFloat();
	pathArea = pathArea.withSizeKeepingCentre(pathArea.getHeight(), pathArea.getHeight());
	
	if(getPreferredLayout() != ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween)
		pathArea = pathArea.reduced(10.0f);

	rebuildPaths();
}

void intensity_editor::timerCallback()
{
	if (checkWatchedParameters())
		rebuildPaths();
}

blend_editor::blend_editor(mothernode* b, PooledUIUpdater* u) :
	ScriptnodeExtraComponent<mothernode>(b, u)
{
	setWatchedParameters({ 0, 1, 2 });
	setLayout(ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween);

	setSize(256 - 32, 50);
}

void blend_editor::onLayoutChange()
{
	if (getPreferredLayout() == ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween)
		setSize(40, (32 + 5) * 3);
}


void blend_editor::paint(Graphics& g)
{
	auto vertical = getPreferredLayout() == ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween;

	multilogic::blend bl;
	bl.setParameter<0>(getWatchedParameterValue(0));
	bl.setParameter<1>(getWatchedParameterValue(1));
	bl.setParameter<2>(getWatchedParameterValue(2));

	auto alpha = bl.alpha * 2.0f - 1.0f;

	Rectangle<float> area;
	Rectangle<float> tb;

	if (vertical)
	{
		auto b = getLocalBounds().toFloat();
		area = b.removeFromBottom((getHeight() * 2) / 3);
		area = area.reduced(JUCE_LIVE_CONSTANT_OFF(12), 10);
		tb = b;
		ScriptnodeComboBoxLookAndFeel::drawScriptnodeDarkBackground(g, tb, false);
	}
	else
	{
		auto b = getLocalBounds().toFloat();
		b = b.removeFromRight((getWidth() * 2) / 3);
		area = b.reduced(40, 15);
	}

	ScriptnodeComboBoxLookAndFeel::drawScriptnodeDarkBackground(g, area, true);
	area = area.reduced(4);



	auto w = std::abs((area.getWidth() - area.getHeight())) * 0.5f;

	if (vertical)
	{
		area = area.withSizeKeepingCentre(area.getWidth(), area.getWidth());
		area = area.translated(0, alpha * w);
	}
	else
	{
		tb = area.translated(0, 20);
		area = area.withSizeKeepingCentre(area.getHeight(), area.getHeight());
		area = area.translated(alpha * w, 0);
	}

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
	setLayout(PreferredLayout::PreferBetween);
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


void converter_editor::initialise(ObjectWithValueTree* o)
{
	node = o;
	modeSelector.initModes(data.getConverterNames(), o);
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

void converter_editor::resized()
{
	auto b = getLocalBounds();
	modeSelector.setBounds(b.removeFromTop(24));
	textArea = b.toFloat();
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


void converter_editor::timerCallback()
{
	if (checkWatchedParameters())
		repaint();
}

void pma_editor::initialise(ObjectWithValueTree* o)
{
	node = dynamic_cast<ParameterSourceObject*>(o);
	jassert(node != nullptr);
}


pma_editor::pma_editor(mothernode* b, PooledUIUpdater* u) :
	ScriptnodeExtraComponent<mothernode>(b, u)
{
	setLayout(ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween);
	setWatchedParameters({ 0, 1, 2 });
	setSize(120, 120);
	setRepaintsOnMouseActivity(true);
}


void pma_editor::paint(Graphics& g)
{
	g.setFont(GLOBAL_BOLD_FONT());

	auto r = node->getParameterRange(0).rng;

	String start, mid, end;

	int numDigits = jmax<int>(1, -1 * roundToInt(log10(r.interval)));

	start = String(r.start, numDigits);

	mid = String(r.convertFrom0to1(0.5), numDigits);

	end = String(r.end, numDigits);

	auto b = getLocalBounds().toFloat();

	float w = JUCE_LIVE_CONSTANT_OFF(85.0f);

	auto midCircle = b.withSizeKeepingCentre(w, w).translated(0.0f, 5.0f);

	float r1 = JUCE_LIVE_CONSTANT_OFF(3.0f);
	float r2 = JUCE_LIVE_CONSTANT_OFF(5.0f);

	float startArc = JUCE_LIVE_CONSTANT_OFF(-2.5f);
	float endArc = JUCE_LIVE_CONSTANT_OFF(2.5f);

	Colour trackColour = JUCE_LIVE_CONSTANT_OFF(Colour(0xff4f4f4f));

	auto createArc = [startArc, endArc](Rectangle<float> b, float startNormalised, float endNormalised)
	{
		if (startNormalised > endNormalised)
			std::swap(startNormalised, endNormalised);

		Path p;

		auto s = startArc + jmin(startNormalised, endNormalised) * (endArc - startArc);
		auto e = startArc + jmax(startNormalised, endNormalised) * (endArc - startArc);

		s = jlimit(startArc, endArc, s);
		e = jlimit(startArc, endArc, e);

		p.addArc(b.getX(), b.getY(), b.getWidth(), b.getHeight(), s, e, true);

		return p;
	};

	auto oc = midCircle;
	auto mc = midCircle.reduced(5.0f);
	auto ic = midCircle.reduced(10.0f);

	auto outerTrack = createArc(oc, 0.0f, 1.0f);
	auto midTrack = createArc(mc, 0.0f, 1.0f);
	auto innerTrack = createArc(ic, 0.0f, 1.0f);

	if (isMouseOver())
		trackColour = trackColour.withMultipliedBrightness(1.1f);

	if (isMouseButtonDown())
		trackColour = trackColour.withMultipliedBrightness(1.1f);

	g.setColour(trackColour);

	g.strokePath(outerTrack, PathStrokeType(r1));
	g.strokePath(midTrack, PathStrokeType(r2));
	g.strokePath(innerTrack, PathStrokeType(r1));

	auto nrm = [r](double v)
	{
		return r.convertTo0to1(v);
	};

	auto value = nrm(node->getParameterValue(0));
	auto mulValue = node->getParameterValue(1);
	auto addValue = node->getParameterValue(2);

	auto totalValue = value * mulValue + addValue;

	auto outerRing = createArc(oc, mulValue * value, totalValue);
	auto midRing = createArc(mc, 0.0f, totalValue);
	auto innerRing = createArc(ic, 0.0f, mulValue * value);
	auto valueRing = createArc(ic, 0.0f, value);

	auto c1 = MultiOutputBase::getFadeColour(0, 2).withAlpha(0.8f);
	auto c2 = MultiOutputBase::getFadeColour(1, 2).withAlpha(0.8f);

	auto ab = getLocalBounds().toFloat();

	Rectangle<float> ar1, ar2;

	if (getPreferredLayout() == ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween)
	{
		ab = ab.removeFromLeft(5.0f);
		ab.removeFromTop(ab.getHeight() / 3.0f);
		ar2 = ab.removeFromTop(ab.getHeight() / 2.0f).withSizeKeepingCentre(5.0f, 5.0f);
		ar1 = ab.withSizeKeepingCentre(5.0f, 5.0f);

	}
	else
	{
		ab = ab.removeFromBottom(5.0f);
		ab.removeFromLeft(ab.getWidth() / 3.0f);
		ar2 = ab.removeFromLeft(ab.getWidth() / 2.0f).withSizeKeepingCentre(5.0f, 5.0f);
		ar1 = ab.withSizeKeepingCentre(5.0f, 5.0f);
	}

	g.setColour(Colour(c1));
	g.strokePath(outerRing, PathStrokeType(r1 - 1.0f));
	g.setColour(c1.withMultipliedAlpha(addValue == 0.0 ? 0.2f : 1.0f));
	g.fillEllipse(ar1);
	g.setColour(JUCE_LIVE_CONSTANT_OFF(Colour(0xffd7d7d7)));
	g.strokePath(midRing, PathStrokeType(r2 - 1.0f));

	g.setColour(c2.withMultipliedAlpha(JUCE_LIVE_CONSTANT_OFF(0.4f)));
	g.strokePath(valueRing, PathStrokeType(r1 - 1.0f));
	g.setColour(c2.withMultipliedAlpha(mulValue == 1.0 ? 0.2f : 1.0f));
	g.fillEllipse(ar2);
	g.setColour(c2);
	g.strokePath(innerRing, PathStrokeType(r1));

	b.removeFromTop(18.0f);

	g.setColour(Colours::white.withAlpha(0.3f));

	Rectangle<float> t((float)getWidth() / 2.0f - 35.0f, 0.0f, 70.0f, 15.0f);

	g.drawText(start, t.translated(-70.0f, 80.0f), Justification::centred);
	g.drawText(mid, t, Justification::centred);
	g.drawText(end, t.translated(70.0f, 80.0f), Justification::centred);
}

void pma_editor::onLayoutChange()
{
	if (getPreferredLayout() == ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween)
		setSize(NewParameterHeight * 3, NewParameterHeight * 3);
}


void pma_editor::timerCallback()
{
	if (checkWatchedParameters())
		repaint();
}

compare_editor::compare_editor(mothernode* b, PooledUIUpdater* u) :
	ScriptnodeExtraComponent<mothernode>(b, u)
{
	setLayout(ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween);
	setSize(256, 60);
	setWatchedParameters({ 0, 1, 2 });
}

juce::Array<int> compare_editor::getParameterOrder() const
{
	return { 2, 0, 1 };
}


void compare_editor::onLayoutChange()
{
	if (getPreferredLayout() == ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween)
		setSize(NewParameterHeight, NewParameterHeight * 3);
	else
		setSize(256, 60);
}


void compare_editor::paint(Graphics& g)
{
	auto b = getLocalBounds();

	auto circles = getCircles(3, 16.0f);

	auto l = circles.getRectangle(getParameterIndexForPreferredLayout(0));
	auto r = circles.getRectangle(getParameterIndexForPreferredLayout(1));
	auto m = circles.getRectangle(getParameterIndexForPreferredLayout(2));

	ScriptnodeComboBoxLookAndFeel::drawScriptnodeDarkBackground(g, l.getUnion(r).expanded(6.0f, 6.0f), true);

	g.setColour(Colours::white.withAlpha(0.9f));
	g.drawEllipse(l, 2.0f);
	g.drawEllipse(r, 2.0f);
	g.drawEllipse(m, 2.0f);

	g.setFont(GLOBAL_MONOSPACE_FONT().withHeight(l.getHeight() - 1.0f));

	using Comp = multilogic::compare::Comparator;

	multilogic::compare data;
	initObjectWithParameterValues<3>(data);

	String w;

	switch (data.comparator)
	{
	case Comp::EQ: w = "==";
		break;
	case Comp::NEQ: w = "!=";
		break;
	case Comp::GT: w = ">";
		break;
	case Comp::LT: w = "<";
		break;
	case Comp::GET: w = ">=";
		break;
	case Comp::LET: w = "<=";
		break;
	case Comp::MIN: w = "min";
		break;
	case Comp::MAX: w = "max";
		break;
	case Comp::numComparators:
		break;
	default:;
	}

	g.drawText(w, l.getUnion(r), Justification::centred);

	g.fillEllipse(l.reduced(3.0f + 5.0f * jlimit(0.0, 1.0, 1.0 - data.leftValue)));
	g.fillEllipse(r.reduced(3.0f + 5.0f * jlimit(0.0, 1.0, 1.0 - data.rightValue)));

	g.fillEllipse(m.reduced(3.0f + 5.0f * jlimit(0.0, 1.0, 1.0 - data.getValue())));
}

void compare_editor::timerCallback()
{
	if (checkWatchedParameters())
		repaint();
}

logic_op_editor::logic_op_editor(mothernode* b, PooledUIUpdater* u) :
	ScriptnodeExtraComponent<mothernode>(b, u)
{
	setLayout(ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween);
	setSize(256, 60);
	setWatchedParameters({ 0, 1, 2 });
}

Array<int> logic_op_editor::getParameterOrder() const
{
	return { 2, 0, 1 };
}


void logic_op_editor::paint(Graphics& g)
{
	auto b = getLocalBounds().toFloat();

	auto circles = getCircles(3, 16.0f);

	auto l = circles.getRectangle(getParameterIndexForPreferredLayout(0));
	auto r = circles.getRectangle(getParameterIndexForPreferredLayout(1));
	auto m = circles.getRectangle(getParameterIndexForPreferredLayout(2));

	ScriptnodeComboBoxLookAndFeel::drawScriptnodeDarkBackground(g, l.getUnion(r).expanded(6.0f, 6.0f), true);

	g.setColour(Colours::white.withAlpha(0.9f));
	g.drawEllipse(l, 2.0f);
	g.drawEllipse(r, 2.0f);
	g.drawEllipse(m, 2.0f);

	g.setFont(GLOBAL_MONOSPACE_FONT().withHeight(l.getHeight() - 1.0f));

	control::multilogic::logic_op lastData;

	initObjectWithParameterValues<3>(lastData);

	String w;

	switch (lastData.logicType)
	{
	case multilogic::logic_op::LogicType::AND: w = "AND"; break;
	case multilogic::logic_op::LogicType::OR: w = "OR"; break;
	case multilogic::logic_op::LogicType::XOR: w = "XOR"; break;
	default: jassertfalse; break;
	}

	g.drawText(w, l.getUnion(r), Justification::centred);

	if (lastData.leftValue == multilogic::logic_op::LogicState::True)
		g.fillEllipse(l.reduced(4.0f));

	if (lastData.rightValue == multilogic::logic_op::LogicState::True)
		g.fillEllipse(r.reduced(4.0f));

	if (lastData.getValue() > 0.5)
		g.fillEllipse(m.reduced(4.0f));
}

void logic_op_editor::timerCallback()
{
	if (checkWatchedParameters())
		repaint();
}

void logic_op_editor::onLayoutChange()
{
	if (getPreferredLayout() == ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween)
		setSize(NewParameterHeight, NewParameterHeight * 3);
}

bipolar_editor::bipolar_editor(mothernode* b, PooledUIUpdater* u) :
	ScriptnodeExtraComponent<mothernode>(b, u)
{
	setLayout(ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween);
	setWatchedParameters({ 0, 1, 2 });
	setSize(256, 256);
}

void bipolar_editor::onLayoutChange()
{
	if (getPreferredLayout() == ScriptnodeExtraComponentBase::PreferredLayout::PreferBetween)
		setSize(NewParameterHeight * 3, NewParameterHeight * 3);
}



void bipolar_editor::paint(Graphics& g)
{
	ScriptnodeComboBoxLookAndFeel::drawScriptnodeDarkBackground(g, pathArea, false);

	UnblurryGraphics ug(g, *this, true);

	g.setColour(Colours::white.withAlpha(0.1f));

	auto pb = pathArea.reduced(UIValues::NodeMargin / 2);

	ug.draw1PxHorizontalLine(pathArea.getCentreY(), pb.getX(), pb.getRight());
	ug.draw1PxVerticalLine(pathArea.getCentreX(), pb.getY(), pb.getBottom());
	ug.draw1PxRect(pb);

	auto c = getNodeColour();
	g.setColour(c);

	Path dst;

	auto ps = ug.getPixelSize();
	float l[2] = { 4.0f * ps, 4.0f * ps };

	PathStrokeType(2.0f * ps).createDashedStroke(dst, outlinePath, l, 2);

	g.fillPath(dst);
	g.strokePath(valuePath, PathStrokeType(4.0f * ug.getPixelSize()));
}



void bipolar_editor::resized()
{
	auto b = getLocalBounds();
	auto bSize = jmin(b.getWidth(), b.getHeight());
	pathArea = b.withSizeKeepingCentre(bSize, bSize).toFloat();
}


void bipolar_editor::timerCallback()
{
	if (checkWatchedParameters())
		rebuild();
}

void bipolar_editor::rebuild()
{
	outlinePath.clear();

	valuePath.clear();
	outlinePath.startNewSubPath(0.0f, 0.0f);
	outlinePath.startNewSubPath(1.0f, 1.0f);

	valuePath.startNewSubPath(0.0f, 0.0f);
	valuePath.startNewSubPath(1.0f, 1.0f);

	multilogic::bipolar lastData;
	initObjectWithParameterValues<3>(lastData);

	auto copy = lastData;

	auto numPixels = pathArea.getWidth();

	bool outlineEmpty = true;
	bool valueEmpty = true;

	bool valueBiggerThanHalf = copy.value > 0.5;
	auto v = lastData.value;

	for (float i = 0.0; i < numPixels; i++)
	{
		float x = i / numPixels;

		copy.value = x;
		float y = 1.0f - copy.getValue();

		if (outlineEmpty)
		{
			outlinePath.startNewSubPath(x, y);
			outlineEmpty = false;
		}
		else
			outlinePath.lineTo(x, y);

		bool drawBiggerValue = valueBiggerThanHalf && x > 0.5f && x < v;
		bool drawSmallerValue = !valueBiggerThanHalf && x < 0.5f && x > v;

		if (drawBiggerValue || drawSmallerValue)
		{
			if (valueEmpty)
			{
				valuePath.startNewSubPath(x, y);
				valueEmpty = false;
			}
			else
				valuePath.lineTo(x, y);
		}
	}

	PathFactory::scalePath(outlinePath, pathArea.reduced(UIValues::NodeMargin));
	PathFactory::scalePath(valuePath, pathArea.reduced(UIValues::NodeMargin));

	repaint();
}








minmax_editor::minmax_editor(mothernode* b, PooledUIUpdater* u) :
	ScriptnodeExtraComponent<mothernode>(b, u)
{
	addAndMakeVisible(rangePresets);
	rangePresets.setLookAndFeel(&slaf);
	rangePresets.setColour(ComboBox::ColourIds::textColourId, Colours::white.withAlpha(0.8f));

	setWatchedParameters({ 0, 1, 2, 3, 4, 5 });

	for (const auto& p : presets.presets)
		rangePresets.addItem(p.id, p.index + 1);

	rangePresets.onChange = [this]()
	{
		for (const auto& p : presets.presets)
		{
			if (p.id == rangePresets.getText())
			{
				setRange(p.nr, p.textConverter);
			}
				
		}
	};

	setSize(256, 100);
	start();
}

void minmax_editor::paint(Graphics& g)
{
	ScriptnodeComboBoxLookAndFeel::drawScriptnodeDarkBackground(g, pathArea, false);
	g.setFont(GLOBAL_BOLD_FONT());

	auto r = currentRange.rng;

	auto tc = node->getValueTree().getChildWithName(PropertyIds::Parameters).getChild(1)[PropertyIds::TextToValueConverter].toString();
	auto vtc = hise::ValueToTextConverter::fromString(tc);
	
	String startValue, endValue;

	if (vtc.active)
	{
		startValue = vtc.getTextForValue(r.start);
		endValue = vtc.getTextForValue(r.end);
	}
	else
	{
		startValue = String(r.start);
		endValue = String(r.end);
	}


	String s;
	s << "[" << startValue;
	s << " - " << endValue << "]";

	g.setColour(Colours::white);
	g.drawText(s, pathArea.reduced(UIValues::NodeMargin), r.skew < 1.0 ? Justification::centredTop : Justification::centredBottom);

	g.setColour(getNodeColour());

	Path dst;

	UnblurryGraphics ug(g, *this, true);
	auto ps = ug.getPixelSize();
	float l[2] = { 4.0f * ps, 4.0f * ps };

	PathStrokeType(2.0f * ps).createDashedStroke(dst, fullPath, l, 2);

	g.fillPath(dst);

	g.strokePath(valuePath, PathStrokeType(4.0f * ug.getPixelSize()));
}

void minmax_editor::timerCallback()
{
	if (checkWatchedParameters())
	{
		rebuildPaths();
	}
}

void minmax_editor::initialise(ObjectWithValueTree* o)
{
	node = o;
}

void minmax_editor::setRange(InvertableParameterRange newRange, const String& tc)
{
	if (node != nullptr)
	{
		auto pTree = node->getValueTree().getChildWithName(PropertyIds::Parameters);

        auto p0 = pTree.getChild(0);
        auto p1 = pTree.getChild(1);
        auto p2 = pTree.getChild(2);
        
		RangeHelpers::storeDoubleRange(p1, newRange, node->getUndoManager());
		RangeHelpers::storeDoubleRange(p2, newRange, node->getUndoManager());

        p0.setProperty(PropertyIds::TextToValueConverter, "NormalizedPercentage", node->getUndoManager());
        p1.setProperty(PropertyIds::TextToValueConverter, tc, node->getUndoManager());
        p2.setProperty(PropertyIds::TextToValueConverter, tc, node->getUndoManager());

        p1.setProperty(PropertyIds::Value, newRange.inv ? newRange.rng.end : newRange.rng.start, node->getUndoManager());
		p2.setProperty(PropertyIds::Value, newRange.inv ? newRange.rng.start : newRange.rng.end, node->getUndoManager());
		pTree.getChild(3).setProperty(PropertyIds::Value, newRange.rng.skew, node->getUndoManager());
		pTree.getChild(4).setProperty(PropertyIds::Value, newRange.rng.interval, node->getUndoManager());
		pTree.getChild(5).setProperty(PropertyIds::Value, newRange.inv ? 1.0 : 0.0, node->getUndoManager());
		rebuildPaths();
	}
}

void minmax_editor::rebuildPaths()
{
	fullPath.clear();
	valuePath.clear();

	if (getWidth() == 0)
		return;

	multilogic::minmax lastData;
	initObjectWithParameterValues<6>(lastData);

	currentRange = lastData.range;

	

	if (currentRange.rng.getRange().isEmpty())
		return;

	auto maxValue = (float)lastData.range.convertFrom0to1(1.0, false);
	auto minValue = (float)lastData.range.convertFrom0to1(0.0, false);

	auto vToY = [&](float v)
	{
		return -v;
	};

	fullPath.startNewSubPath(1.0f, vToY(maxValue));
	fullPath.startNewSubPath(1.0f, vToY(minValue));
	fullPath.startNewSubPath(0.0f, vToY(maxValue));
	fullPath.startNewSubPath(0.0f, vToY(minValue));

	valuePath.startNewSubPath(1.0f, vToY(maxValue));
	valuePath.startNewSubPath(1.0f, vToY(minValue));
	valuePath.startNewSubPath(0.0f, vToY(maxValue));
	valuePath.startNewSubPath(0.0f, vToY(minValue));

	for (int i = 0; i < getWidth(); i += 3)
	{
		float normX = (float)i / (float)getWidth();

		auto v = lastData.range.convertFrom0to1(normX, false);
		v = lastData.range.snapToLegalValue(v);

		auto y = vToY((float)v);

		fullPath.lineTo(normX, y);

		if (lastData.value > normX)
			valuePath.lineTo(normX, y);
	}

	fullPath.lineTo(1.0, vToY(maxValue));

	if (lastData.value == 1.0)
		valuePath.lineTo(1.0, vToY(maxValue));

	auto pb = pathArea.reduced((float)UIValues::NodeMargin);

	fullPath.scaleToFit(pb.getX(), pb.getY(), pb.getWidth(), pb.getHeight(), false);
	valuePath.scaleToFit(pb.getX(), pb.getY(), pb.getWidth(), pb.getHeight(), false);

	repaint();
}

void minmax_editor::resized()
{
	auto b = getLocalBounds();
	auto bottom = b.removeFromBottom(28);
	b.removeFromBottom(UIValues::NodeMargin);
	bottom.removeFromRight(UIValues::NodeMargin);
	rangePresets.setBounds(bottom);

	b.reduced(UIValues::NodeWidth, 0);

	pathArea = b.toFloat();

	rebuildPaths();
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
	registerComponent<control::pma_editor>("control.pma");
	registerComponent<control::pma_editor>("control.pma_unscaled");
	registerComponent<control::compare_editor>("control.compare");
	registerComponent<control::logic_op_editor>("control.logic_op");
	registerComponent<control::bipolar_editor>("control.bipolar");
	registerComponent<control::minmax_editor>("control.minmax");
	registerComponent<control::branch_editor>("control.branch_cable");
}



}
