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

#pragma once

namespace scriptnode {
using namespace juce;
using namespace hise;

namespace file_analysers
{
struct dynamic
{
	using NodeType = wrap::data<control::file_analyser<parameter::dynamic_base_holder, dynamic>, data::dynamic::audiofile>;

	enum AnalyserMode
	{
		Peak,
		Pitch,
		Length,
		numAnalyserModes
	};

	static StringArray getAnalyserModes()
	{
		return { "Peak", "Pitch", "Length" };
	}

	dynamic() :
		mode(PropertyIds::Mode, "Peak")
	{};

	void initialise(NodeBase* n)
	{
		mode.initialise(n->getUndoManager(), n->getValueTree());
		mode.setAdditionalCallback(BIND_MEMBER_FUNCTION_2(dynamic::updateMode), true);

		parentNode = n;
	}

	void updateMode(Identifier, var newValue)
	{
		m = (AnalyserMode)getAnalyserModes().indexOf(newValue.toString());
		
		if (auto o = lastData.obj)
			o->getUpdater().sendContentChangeMessage(sendNotificationAsync, 90);
	}

	double getValue(const ExternalData& d)
	{
		lastData = d;

		switch (m)
		{
		case Length: lastValue = file_analysers::milliseconds().getValue(d); break;
		case Pitch: lastValue = file_analysers::pitch().getValue(d); break;
		case Peak: lastValue = file_analysers::peak().getValue(d); break;
        default: break;
		}

		return lastValue;
	}

	double lastValue = 0.0;
	ExternalData lastData;
	NodePropertyT<String> mode;
	AnalyserMode m;
	WeakReference<NodeBase> parentNode;

	struct editor : public ScriptnodeExtraComponent<NodeType>
	{
		editor(NodeType* n, PooledUIUpdater* u) :
			ScriptnodeExtraComponent<NodeType>(n, u),
			audioEditor(u, &n->i),
			modeSelector("Peak")
		{
			addAndMakeVisible(audioEditor);
			addAndMakeVisible(modeSelector);

			auto pn = getObject()->obj.analyser.parentNode.get();
			modeSelector.initModes(dynamic::getAnalyserModes(), pn->getUndoManager(), pn->getValueTree());

			setSize(500, 128);
			stop();
		};

		void resized() override
		{
			auto b = getLocalBounds();
			modeSelector.setBounds(b.removeFromTop(28));
			audioEditor.setBounds(b);
		}

		void timerCallback() override
		{}

		static Component* createExtraComponent(void* obj, PooledUIUpdater* updater)
		{
			auto typed = static_cast<NodeType*>(obj);
			return new editor(typed, updater);
		}

		ComboBoxWithModeProperty modeSelector;
		data::ui::audiofile_editor_with_mod audioEditor;
	};
};
}


namespace faders
{

using NodeType = control::xfader<parameter::dynamic_list, dynamic>;

struct editor : public ScriptnodeExtraComponent<NodeType>
{
	using FaderGraph = control::xfader_editor;

	editor(NodeType* v, PooledUIUpdater* updater_);;

	void timerCallback() override
	{
		jassertfalse;
	};

	void resized() override;

	static Component* createExtraComponent(void* obj, PooledUIUpdater* updater);

	parameter::ui::dynamic_list_editor dragRow;

	FaderGraph graph;
};

} // namespace faders

namespace control 
{

	using dynamic_branch_base = branch_base<parameter::dynamic_list>;

	struct branch_editor : public ScriptnodeExtraComponent<dynamic_branch_base>
	{
		branch_editor(dynamic_branch_base* t, PooledUIUpdater* u) :
			ScriptnodeExtraComponent(t, u),
			dragRow(&t->p, u)
		{
			addAndMakeVisible(dragRow);
			setSize(512, 100);
			start();
		}

		static Component* createExtraComponent(void* typed, PooledUIUpdater* updater)
		{
			auto t = static_cast<dynamic_branch_base*>(typed);
			return new branch_editor(t, updater);
		}

		void onNumParametersChange(const Identifier& id, const var& newValue)
		{
			if(auto h = findParentComponentOfClass<NodeComponent>())
			{
				auto numParameters = (int)newValue;

				if(numParameters > 1)
				{
					auto pn = h->node.get();
					auto indexParameter = pn->getParameterFromName("Index");
					indexParameter->setRangeProperty(PropertyIds::MaxValue.toString(), numParameters - 1);
				}
			}
		}

		bool updaterInitialised = false;
		valuetree::PropertyListener numParametersUpdater;

		void timerCallback() override 
		{
			if(auto n = getObject())
			{
				auto pn = findParentComponentOfClass<NodeComponent>()->node.get();


				if (pn->getValueTree().getChildWithName(PropertyIds::SwitchTargets).getNumChildren() == 0)
				{
					pn->setNodeProperty(PropertyIds::NumParameters, 8);
				}

				if(!updaterInitialised)
				{
					auto npt = pn->getPropertyTree().getChildWithProperty(PropertyIds::ID, PropertyIds::NumParameters.toString());
					numParametersUpdater.setCallback(npt, { PropertyIds::Value }, valuetree::AsyncMode::Asynchronously, VT_BIND_PROPERTY_LISTENER(onNumParametersChange));
					updaterInitialised = true;
				}
			}

			auto currentIndex = getObject()->getUIIndex();
			auto numParameters = getObject()->getParameter().getNumParameters();

			if(lastIndex != currentIndex || numParameters != numLastParameters)
			{
				lastIndex = currentIndex;
				numLastParameters = numParameters;
				repaint();
			}
		}

		void paint(Graphics& g) override
		{
			if(numLastParameters > 0)
			{
				auto w = top.getWidth() / numLastParameters;

				auto copy = top;

				auto nc = findParentComponentOfClass<NodeComponent>()->getHeaderColour();

				for(int i = 0; i < numLastParameters; i++)
				{
					auto s = copy.removeFromLeft(w).reduced(1).toFloat();

					auto active = i == lastIndex;
					
					g.setColour(nc.withAlpha(active ? 1.0f : 0.1f));
					g.fillRoundedRectangle(s, s.getHeight() / 2.0f);
				}
			}
		}

		int lastIndex = 0;
		int numLastParameters = 0;

		void resized() override
		{
			auto b = getLocalBounds();
			top = b.removeFromBottom(10);
			dragRow.setBounds(b);
		}

		Rectangle<int> top;

		parameter::ui::dynamic_list_editor dragRow;
	};

}

} // namespace scriptnode
