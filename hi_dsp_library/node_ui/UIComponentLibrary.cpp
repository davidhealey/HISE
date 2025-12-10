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

}

UIFactory::Data::Data()
{
	registerComponent<fx::bitcrush_editor>("fx.bitcrush");
	registerComponent<fx::sampleandhold_editor>("fx.sampleandhold");
	registerComponent<fx::phase_delay_editor>("fx.phasedelay");
	registerComponent<fx::reverb_editor>("fx.reverb");
	registerComponent<math::map_editor>("math.map");
	registerComponent<control::xfader_editor>("control.xfader");
}



}