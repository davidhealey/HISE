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

namespace faders
{

	editor::editor(NodeType* v, PooledUIUpdater* updater_) :
		ScriptnodeExtraComponent(v, updater_),
		graph(nullptr, updater_),
		dragRow(&v->p, updater_)
	{
		graph.initialise(v->p.parentNode);
		addAndMakeVisible(dragRow);

		addAndMakeVisible(graph);

		setSize(256, 24 + 10 + parameter::ui::UIConstants::ButtonHeight + parameter::ui::UIConstants::DragHeight + parameter::ui::UIConstants::GraphHeight + UIValues::NodeMargin);

		setRepaintsOnMouseActivity(true);

		stop();
	}

	void editor::resized()
	{
		auto b = getLocalBounds();
		graph.setBounds(b.removeFromTop(parameter::ui::UIConstants::GraphHeight + 24 + UIValues::NodeMargin));
		dragRow.setBounds(b);
	}

	juce::Component* editor::createExtraComponent(void* obj, PooledUIUpdater* updater)
	{
		auto v = static_cast<NodeType*>(obj);
		return new editor(v, updater);
	}

}


}