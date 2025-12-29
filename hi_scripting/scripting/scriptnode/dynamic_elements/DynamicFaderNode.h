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

	void initialise(ObjectWithValueTree* n)
	{
		mode.initialise(n);
		mode.setAdditionalCallback(BIND_MEMBER_FUNCTION_2(dynamic::updateMode), true);

		parentNode = dynamic_cast<NodeBase*>(n);
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
			modeSelector.initModes(dynamic::getAnalyserModes(), pn);

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

using Editor = parameter::ui::multi_output_wrapper<faders::NodeType, control::xfader_editor>;

} // namespace faders

namespace control 
{

	using dynamic_branch_base = branch_base<parameter::dynamic_list>;

	

	using branch_editor_wrapped = parameter::ui::multi_output_wrapper<dynamic_branch_base, branch_editor>;



}

} // namespace scriptnode
