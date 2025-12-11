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

#pragma once

#define SN_CREATE_EXTRA_COMPONENT(ComponentType) static Component* createExtraComponent(void* obj, PooledUIUpdater* updater) { \
	return new ComponentType(static_cast<mothernode*>(obj), updater); }

namespace scriptnode {

using namespace hise;
using namespace juce;

namespace fx {

struct bitcrush_editor : simple_visualiser
{
	bitcrush_editor(PooledUIUpdater* u) :
		simple_visualiser(nullptr, u)
	{
		setSize(128, 100);
	};

	void rebuildPath(Path& p) override
	{
		span<float, 100> x;

		for (int i = 0; i < 100; i++)
			x[i] = (float)i / 100.0f - 50.0f;

		getBitcrushedValue(x, getParameter(0) / 2.5, getParameter(1));

		FloatSanitizers::sanitizeArray(x.begin(), x.size());

		p.startNewSubPath(0, 1.0f - x[0]);

		for (int i = 1; i < 100; i++)
			p.lineTo(i, 1.0f - x[i]);
	}

	static Component* createExtraComponent(void*, PooledUIUpdater* u)
	{
		return new bitcrush_editor(u);
	}
};

struct sampleandhold_editor : simple_visualiser
{
	sampleandhold_editor(PooledUIUpdater* u) :
		simple_visualiser(nullptr, u)
	{
		setSize(128, 100);
	};

	void rebuildPath(Path& p) override
	{
		span<float, 100> x;

		for (int i = 0; i < 100; i++)
			x[i] = hmath::sin(float_Pi * 2.0f * (float)i / 100.0f);

		auto delta = (int)(getParameter(0) / JUCE_LIVE_CONSTANT_OFF(10.0f));
		int counter = 0;
		float v = 0.0;

		for (int i = 0; i < 100; i++)
		{
			if (counter++ >= delta)
			{
				counter = 0;
				v = x[i];
			}

			x[i] = v;
		}

		p.startNewSubPath(0, 1.0f - x[0]);

		for (int i = 1; i < 100; i++)
			p.lineTo(i, 1.0f - x[i]);
	}

	static Component* createExtraComponent(void*, PooledUIUpdater* u)
	{
		return new sampleandhold_editor(u);
	}
};

struct phase_delay_editor : public simple_visualiser
{
	phase_delay_editor(PooledUIUpdater* u) :
		simple_visualiser(nullptr, u)
	{
		setSize(128, 100);
	};

	void rebuildPath(Path& p) override
	{
		span<float, 100> x;

		for (int i = 0; i < 100; i++)
			x[i] = hmath::sin(float_Pi * 2.0f * (float)i / 100.0f);

		auto v = getParameter(0);

		NormalisableRange<double> fr(20.0, 20000.0);
		fr.setSkewForCentre(500.0);

		auto nv = fr.convertTo0to1(v);

		original.startNewSubPath(0, 1.0f - x[0]);

		for (int i = 1; i < 100; i++)
			original.lineTo(i, 1.0f - x[i]);

		p.startNewSubPath(0, 1.0f - x[50 + roundToInt(nv * 49)]);

		for (int i = 1; i < 100; i++)
		{
			auto index = (i + 50 + roundToInt(nv * 49)) % 100;

			p.lineTo(i, 1.0f - x[index]);
		}
	}

	static Component* createExtraComponent(void*, PooledUIUpdater* u)
	{
		return new phase_delay_editor(u);
	}
};

struct reverb_editor : simple_visualiser
{
	reverb_editor(PooledUIUpdater* u) :
		simple_visualiser(nullptr, u)
	{
		setSize(256, 100);
	};

	void rebuildPath(Path& p) override
	{
		auto damp = getParameter(0);
		auto width = getParameter(1);
		auto size = getParameter(2);

		p.startNewSubPath(0.0f, 0.0f);
		p.startNewSubPath(1.0f, 1.0f);

		Rectangle<float> base(0.5f, 0.5f, 0.0f, 0.0f);

		for (int i = 0; i < 8; i++)
		{
			auto ni = (float)i / 8.0f;
			ni = hmath::pow(ni, 1.0f + (float)damp);

			auto a = base.withSizeKeepingCentre(width * ni * 2.0f, size * ni);
			p.addRectangle(a);
		}
	}

	static Component* createExtraComponent(void*, PooledUIUpdater* u)
	{
		return new reverb_editor(u);
	}
};

} // fx

namespace math {

struct map_editor : public simple_visualiser
{
	map_editor(void* p, PooledUIUpdater* u) :
		simple_visualiser(nullptr, u)
	{
		setSize(256, 100);
	};

	void rebuildPath(Path& p) override
	{

		auto p0 = getParameterRange(0).convertTo0to1(getParameter(0), true);
		auto p1 = getParameterRange(1).convertTo0to1(getParameter(1), true);
		auto p2 = getParameterRange(2).convertTo0to1(getParameter(2), true);
		auto p3 = getParameterRange(3).convertTo0to1(getParameter(3), true);

		p.startNewSubPath(0.0f, 0.0f);
		p.startNewSubPath(1.0f, 1.0f);

		original.startNewSubPath(0.0f, 0.0f);
		original.startNewSubPath(1.0f, 1.0f);

		p.startNewSubPath(0.0f, 1.0f - p0);
		p.lineTo(1.0f, 1.0f - p2);
		p.startNewSubPath(0.0f, 1.0f - p1);
		p.lineTo(1.0f, 1.0f - p3);

		original.startNewSubPath(0.0f, 1.0f - (p0 + p1) / 2.0f);
		original.lineTo(1.0f, 1.0f - (p2 + p3) / 2.0f);
	}

	SN_CREATE_EXTRA_COMPONENT(map_editor);

};

} // math

namespace control {

struct xfader_editor : public ScriptnodeExtraComponent<mothernode>,
					   public MultiOutputBase
{
	xfader_editor(mothernode* f, PooledUIUpdater* updater);

	void initialise(ObjectWithValueTree* o) override;

	valuetree::RecursivePropertyListener graphRebuilder;

	void paint(Graphics& g) override;

	void setInputValue(double v);

	template <int Index> Path createPath() const
	{
		int numPaths = getNumOutputs();
		int numPixels = 256;

		Path p;
		p.startNewSubPath(0.0f, 0.0f);

		if (numPixels > 0)
		{
			for (int i = 0; i < numPixels; i++)
			{
				double inputValue = (double)i / (double)numPixels;
				auto output = (float)obj.getFadeValue<Index>(numPaths, inputValue);

				p.lineTo((float)i, -1.0f * output);
			}
		}

		p.lineTo(numPixels - 1, 0.0f);
		p.closeSubPath();

		return p;
	}

	SN_CREATE_EXTRA_COMPONENT(xfader_editor);

	void rebuildFaderCurves();

	void resized() override;

	void timerCallback() override;

	double inputValue = 0.0;

	faders::dynamic obj;

	ComboBoxWithModeProperty faderSelector;

	Array<Path> faderCurves;
};



struct intensity_editor : public ScriptnodeExtraComponent<mothernode>,
						  public ParameterWatcherBase
{

	intensity_editor(mothernode* b, PooledUIUpdater* u);

	void paint(Graphics& g) override;

	void rebuildPaths();

	void timerCallback() override;

	void resized() override;

	SN_CREATE_EXTRA_COMPONENT(intensity_editor);

	Rectangle<float> pathArea;

	Path fullPath, valuePath;
};

struct blend_editor : public ScriptnodeExtraComponent<mothernode>,
	public ParameterWatcherBase
{
	blend_editor(mothernode* b, PooledUIUpdater* u);;

	void paint(Graphics& g) override;

	void timerCallback() override;

	SN_CREATE_EXTRA_COMPONENT(blend_editor);
};

struct converter_editor : public ScriptnodeExtraComponent<mothernode>,
						  public ParameterWatcherBase,
						  public ComboBox::Listener
{
	using Mode = conversion_logic::dynamic::Mode;

	converter_editor(mothernode* p, PooledUIUpdater* updater);

	void setRange(NormalisableRange<double> nr, double center = -90.0);
	void paint(Graphics& g) override;
	void comboBoxChanged(ComboBox* b);
	void initialise(ObjectWithValueTree* o);
	void timerCallback() override;;
	void resized() override;

	SN_CREATE_EXTRA_COMPONENT(converter_editor);

private:	

	ObjectWithValueTree* node;
	conversion_logic::dynamic data;
	Rectangle<float> textArea;
	ComboBoxWithModeProperty modeSelector;
	Colour currentColour;
};

} // control

} // scriptnode