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

namespace control
{
	

	using logic_op_editor_wrapped = extra_drag_wrapper<logic_op_editor, Justification::top>;
	using compare_editor_wrapped = extra_drag_wrapper<compare_editor, Justification::top>;
	using blend_editor_wrapped = extra_drag_wrapper<blend_editor, Justification::left>;
	using intensity_editor_wrapped = extra_drag_wrapper<intensity_editor, Justification::bottom>;

	

	using minmax_editor_wrapped = extra_drag_wrapper<minmax_editor, Justification::bottom>;

	struct midi_cc_editor : public ScriptnodeExtraComponent<midi_cc<parameter::dynamic_base_holder>>
	{
		using ObjectType = midi_cc<parameter::dynamic_base_holder>;

		midi_cc_editor(ObjectType* obj, PooledUIUpdater* u) :
			ScriptnodeExtraComponent<ObjectType>(obj, u),
			dragger(u)
		{
			addAndMakeVisible(dragger);
			setSize(256, 32);
		};

		void checkValidContext()
		{
			if (contextChecked)
				return;

			if (auto nc = findParentComponentOfClass<NodeComponent>())
			{
				NodeBase* n = nc->node.get();
				
				try
				{
					ScriptnodeExceptionHandler::validateMidiProcessingContext(n);
					n->getRootNetwork()->getExceptionHandler().removeError(n);
				}
				catch (Error& e)
				{
					n->getRootNetwork()->getExceptionHandler().addError(n, e);
				}

				contextChecked = true;
			}
		}

		void paint(Graphics& g) override
		{
			auto b = getLocalBounds().toFloat();
			b.removeFromBottom((float)dragger.getHeight());

			g.setColour(Colours::white.withAlpha(alpha));
			g.drawRoundedRectangle(b, b.getHeight() / 2.0f, 1.0f);
			
			b = b.reduced(2.0f);
			b.setWidth(jmax<float>(b.getHeight(), lastValue.getModValue() * b.getWidth()));
			g.fillRoundedRectangle(b, b.getHeight() / 2.0f);
		}

		void resized() override
		{
			dragger.setBounds(getLocalBounds().removeFromBottom(24));
		}

		void timerCallback() override
		{
			checkValidContext();

			if (ObjectType* o = getObject())
			{
				auto lv = o->getParameter().getDisplayValue();

				if (lastValue.setModValueIfChanged(lv))
					alpha = 1.0f;
				else
					alpha = jmax(0.5f, alpha * 0.9f);

				repaint();
			}
		}

		static Component* createExtraComponent(void* o, PooledUIUpdater* u)
		{
			auto mn = static_cast<mothernode*>(o);
			auto typed = dynamic_cast<ObjectType*>(mn);

			return new midi_cc_editor(typed, u);
		}

		float alpha = 0.5f;
		ModValue lastValue;

		ModulationSourceBaseComponent dragger;
		bool contextChecked = false;
	};

	

	using bipolar_editor_wrapped = extra_drag_wrapper<bipolar_editor, Justification::bottom>;	

	struct sliderbank_pack : public data::dynamic::sliderpack
	{
		sliderbank_pack(data::base& t, int index=0) :
			data::dynamic::sliderpack(t, index)
		{};

		void initialise(ObjectWithValueTree* n) override
		{
			sliderpack::initialise(n);

			outputListener.setCallback(n->getValueTree().getChildWithName(PropertyIds::SwitchTargets), 
									   valuetree::AsyncMode::Synchronously,
								       BIND_MEMBER_FUNCTION_2(sliderbank_pack::updateNumSliders));

			updateNumSliders({}, false);
		}

		void updateNumSliders(ValueTree v, bool wasAdded)
		{
			if (auto sp = dynamic_cast<SliderPackData*>(currentlyUsedData))
				sp->setNumSliders((int)outputListener.getParentTree().getNumChildren());
		}

		valuetree::ChildListener outputListener;
	};

	using dynamic_sliderbank = wrap::data<sliderbank<parameter::dynamic_list>, sliderbank_pack>;

	struct sliderbank_editor : public ScriptnodeExtraComponent<dynamic_sliderbank>
	{
		using NodeType = dynamic_sliderbank;

		sliderbank_editor(ObjectType* b, PooledUIUpdater* updater) :
			ScriptnodeExtraComponent<ObjectType>(b, updater),
			p(updater, &b->i),
			r(&b->getWrappedObject().p, updater)
		{
			addAndMakeVisible(p);
			addAndMakeVisible(r);

			setSize(256, 200);
			stop();
		};

		void resized() override
		{
			auto b = getLocalBounds();

			p.setBounds(b.removeFromTop(130));
			r.setBounds(b);
		}

		void timerCallback() override
		{
			jassertfalse;
		}

		static Component* createExtraComponent(void* obj, PooledUIUpdater* updater)
		{
			auto v = static_cast<NodeType*>(obj);
			return new sliderbank_editor(v, updater);
		}

		scriptnode::data::ui::sliderpack_editor p;
		parameter::ui::dynamic_list_editor r;
	};

}

namespace conversion_logic
{

using DynamicNodeType = control::converter<parameter::dynamic_base_holder, dynamic>;
using DynamicEditor = extra_drag_wrapper<control::converter_editor, Justification::bottom>;

}

namespace smoothers
{

struct dynamic_base : public base
{
	using NodeType = control::smoothed_parameter_base;

	enum class SmoothingType
	{
		NoSmoothing,
		LinearRamp,
		LowPass,
		numSmoothingTypes
	};

	static StringArray getSmoothNames() { return { "NoSmoothing", "Linear Ramp", "Low Pass" }; }

	dynamic_base() :
		mode(PropertyIds::Mode, "Linear Ramp")
	{};

	void initialise(ObjectWithValueTree* n) override
	{
		mode.initialise(n);
		mode.setAdditionalCallback(BIND_MEMBER_FUNCTION_2(dynamic_base::setMode), true);
	}

	virtual void setMode(Identifier id, var newValue) {};
	
	virtual ~dynamic_base() {};

	struct editor : public ScriptnodeExtraComponent<dynamic_base>
	{
		editor(dynamic_base* p, PooledUIUpdater* updater);

		void paint(Graphics& g) override;

		void timerCallback();

		static Component* createExtraComponent(void* obj, PooledUIUpdater* updater)
		{
			auto v = static_cast<NodeType*>(obj);

			auto o = v->getSmootherObject();

			return new editor(dynamic_cast<dynamic_base*>(o), updater);
		}

		void resized() override
		{
			auto b = getLocalBounds();

			modeSelector.setBounds(b.removeFromTop(24));
			b.removeFromTop(UIValues::NodeMargin);
			plotter.setBounds(b);
		}

		ModulationSourceBaseComponent plotter;
		ComboBoxWithModeProperty modeSelector;

		Colour currentColour;
	};

	float get() const final override
	{
		return (float)lastValue.getModValue();
	}

protected:

	double value = 0.0;
	NodePropertyT<String> mode;
	ModValue lastValue;
	smoothers::base* b = nullptr;

	JUCE_DECLARE_WEAK_REFERENCEABLE(dynamic_base);
};

template <int NV> struct dynamic : public dynamic_base
{
	static constexpr int NumVoices = NV;
	
	dynamic()	
	{
		b = &r;
	}

	void reset() final override
	{
		b->reset();
	};

	void set(double nv) final override
	{
		value = nv;
		b->set(nv);
	}

	float advance() final override
	{
		if (enabled)
			lastValue.setModValueIfChanged(b->advance());
		
		return get();
	}

	void prepare(PrepareSpecs ps) final override
	{
		l.prepare(ps);
		r.prepare(ps);
		n.prepare(ps);
	}

	void refreshSmoothingTime() final override
	{
		b->setSmoothingTime(smoothingTimeMs);
	};

	float v = 0.0f;

	void setMode(Identifier id, var newValue) override
	{
		auto m = (SmoothingType)getSmoothNames().indexOf(newValue.toString());

		switch (m)
		{
		case SmoothingType::NoSmoothing: b = &n; break;
		case SmoothingType::LinearRamp: b = &r; break;
		case SmoothingType::LowPass: b = &l; break;
		default: b = &r; break;
		}

		refreshSmoothingTime();
		b->set(value);
		b->reset();
	}

	smoothers::no<NumVoices> n;
	smoothers::linear_ramp<NumVoices> r;
	smoothers::low_pass<NumVoices> l;
};
}

}
