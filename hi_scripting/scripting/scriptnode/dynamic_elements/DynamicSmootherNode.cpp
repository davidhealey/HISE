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

namespace scriptnode {
using namespace juce;
using namespace hise;

namespace control
{






}

namespace smoothers
{
	dynamic_base::editor::editor(dynamic_base* p, PooledUIUpdater* updater) :
		ScriptnodeExtraComponent<dynamic_base>(p, updater),
		plotter(updater),
		modeSelector("Linear Ramp")
	{
		addAndMakeVisible(modeSelector);
		addAndMakeVisible(plotter);
		setSize(200, 58);
	}

	void dynamic_base::editor::paint(Graphics& g)
	{
		float a = JUCE_LIVE_CONSTANT_OFF(0.4f);

		auto b = getLocalBounds();
		b.removeFromTop(modeSelector.getHeight());
		b.removeFromTop(UIValues::NodeMargin);
		g.setColour(currentColour.withAlpha(a));

		b = b.removeFromRight(b.getHeight()).reduced(5);

		g.fillEllipse(b.toFloat());
	}

	void dynamic_base::editor::timerCallback()
	{
		double v = 0.0;
		currentColour =  getObject()->lastValue.getChangedValue(v) ? Colour(SIGNAL_COLOUR) : Colours::grey;
		repaint();

		auto pn = plotter.getSourceNodeFromParent();
		modeSelector.initModes(smoothers::dynamic_base::getSmoothNames(), pn);
	}

}

}
