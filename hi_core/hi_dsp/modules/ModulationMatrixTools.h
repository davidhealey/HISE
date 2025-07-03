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

namespace hise { using namespace juce;


#define DECLARE_ID(x) static const Identifier x(#x);
namespace MatrixIds
{
DECLARE_ID(Connection);
DECLARE_ID(TargetId);
DECLARE_ID(SourceIndex);
DECLARE_ID(Intensity);
DECLARE_ID(Mode);
DECLARE_ID(AuxIndex);
DECLARE_ID(AuxIntensity);
DECLARE_ID(Inverted);
DECLARE_ID(MatrixData);

DECLARE_ID(ValueRange);
DECLARE_ID(IsModulator);
DECLARE_ID(InputRange);
DECLARE_ID(OutputRange);
DECLARE_ID(TextConverter);
DECLARE_ID(UseMidPositionAsZero);

struct Helpers
{
	enum class TargetType
	{
		All,
		Modulators,
		Parameters
	};

	enum class DragTargetType
	{
		Inactive = 0,
		Dragging,
		Hover
	};

	static Array<Identifier> getWatchableIds() { return { SourceIndex, TargetId, Mode, Inverted, Intensity, AuxIndex, AuxIntensity }; };
	static ValueTree addConnection(ValueTree& v, MainController* um, const String& targetId, int sourceIndex=-1);
	static ValueTree getMatrixDataFromGlobalContainer(MainController* mc);
	static bool removeConnection(ValueTree& data, UndoManager* um, const String& targetId, int sourceIndex);
	static bool removeLastConnection(ValueTree& data, UndoManager* um, const String& targetId);
	static bool removeAllConnections(ValueTree& data, UndoManager* um, const String& targetId);
	static bool matchesTarget(const ValueTree& data, const String& targetId);

	static ValueTree getConnection(const ValueTree& matrixData, int sourceIndex, const String& targetId)
	{
		jassert(matrixData.getType() == MatrixData);

		for(auto c: matrixData)
		{
			if(matchesTarget(c, targetId) && (int)c[SourceIndex] == sourceIndex)
				return c;
		}

		return {};
	}

	static void fillModSourceList(const MainController* mc, StringArray& items);
	static void fillModTargetList(const MainController* mc, StringArray& items, TargetType t);

	static int getModulationSourceDragIndex(const var& data)
	{
		if(data["Type"].toString() == "ModulationDrag")
			return (int)data[MatrixIds::SourceIndex];

		return -1;
	}

	static void updateDragState(HiSlider* s, DragTargetType state)
	{
		if(s->isUsingModulatedRing())
		{
			s->getProperties().set("modulationDragState", (int)state);
			s->repaint();
		}
	}

	static void repaintMatrixSlidersOnDrag(Component* c, const var& data, DragTargetType state)
	{
		auto idx = getModulationSourceDragIndex(data);

		if(idx != -1)
		{
			Component::callRecursive<HiSlider>(c, [&](HiSlider* s)
			{
				updateDragState(s, state);
				return false;
			});
		}
	}

	static void startDragging(Component* c, int sourceIndex);
	static void stopDragging(Component* c);

	static TargetType getTargetType(const MainController* mc, const String& targetId);

	struct IntensityTextConverter: public ValueToTextConverter::CustomConverter,
	                               public dispatch::Processor::AttributeListener
	{
		struct ConstructData
		{
			WeakReference<Processor> p;
			int parameterIndex;
			String prettifierMode;
			scriptnode::modulation::TargetMode tm;
			NormalisableRange<double> inputRange;
			Component::SafePointer<Slider> connectedSlider;
		};

		IntensityTextConverter(const ConstructData& cd);
		~IntensityTextConverter() override;

		void onAttributeUpdate(Processor* , uint16 index) override;
		double getValue(const String& text) const override;
		String getText(double v) const override;

		bool showInactive = false;
		ConstructData data;
		ValueToTextConverter prettifier;
	};
};

}
#undef DECLARE_ID


} // namespace hise
