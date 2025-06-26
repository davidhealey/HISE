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



namespace hise { using namespace juce;

namespace MatrixIds
{
ValueTree Helpers::addConnection(ValueTree& v, MainController* mc, const String& targetId, int sourceIndex)
{
	jassert(v.getType() == MatrixData);

	if(sourceIndex != -1)
	{
		auto existing = getConnection(v, sourceIndex, targetId);

		if(existing.isValid())
			return existing;
	}

	auto um = mc->getControlUndoManager();

	float defaultValue = 0.0f;
	int defaultMode = 1;

	if(auto mod = dynamic_cast<Modulation*>(ProcessorHelpers::getFirstProcessorWithName(mc->getMainSynthChain(), targetId)))
	{
		auto m = mod->getMode();

		switch(m)
		{
		case Modulation::GainMode:
		case Modulation::GlobalMode:
			defaultValue = 1.0f;
			defaultMode = 0;
			break;
		case Modulation::PitchMode:
		case Modulation::PanMode:
		case Modulation::OffsetMode:
		case Modulation::CombinedMode:
			defaultValue = 0.0f;
			defaultMode = 2;
			break;
		case Modulation::numModes:
			break;
		default: ;
		}
	}


	if(sourceIndex != -1)
	{
		for(auto c: v)
		{
			if(matchesTarget(c, targetId) && (int)c[SourceIndex] == -1)
			{
				c.setProperty(Mode, defaultMode, um);
				c.setProperty(SourceIndex, sourceIndex, um);
				c.setProperty(Intensity, defaultValue, um);
				c.setProperty(AuxIndex, -1.0, um);
				c.setProperty(AuxIntensity, 0.0, um);
				c.setProperty(Inverted, false, um);

				return c;
			}
		}
	}
	
	ValueTree nd(Connection);

	nd.setProperty(TargetId, targetId, nullptr);
	nd.setProperty(Mode, defaultMode, nullptr);
	nd.setProperty(SourceIndex, sourceIndex, nullptr);
	nd.setProperty(Intensity, defaultValue, nullptr);
	nd.setProperty(AuxIndex, -1.0, nullptr);
	nd.setProperty(AuxIntensity, 0.0, nullptr);
	nd.setProperty(Inverted, false, nullptr);
	v.addChild(nd, -1, um);
	return nd;
}

ValueTree Helpers::getMatrixDataFromGlobalContainer(MainController* mc)
{
	if(auto gc = ProcessorHelpers::getFirstProcessorWithType<GlobalModulatorContainer>(mc->getMainSynthChain()))
	{
		return gc->getMatrixModulatorData();
	}

	return {};
}

bool Helpers::removeConnection(ValueTree& data, UndoManager* um, const String& targetId, int sourceIndex)
{
	for(auto c: data)
	{
		if(c[TargetId].toString() == targetId && (int)c[SourceIndex] == sourceIndex)
		{
			data.removeChild(c, um);
			return true;
		}
	}

	return false;
}

bool Helpers::removeLastConnection(ValueTree& data, UndoManager* um, const String& targetId)
{
	for(int i = data.getNumChildren(); i >= 0; i--)
	{
		if(data.getChild(i)[TargetId].toString() == targetId)
		{
			data.removeChild(i, um);
			return true;
		}
	}

	return false;
}

bool Helpers::removeAllConnections(ValueTree& data, UndoManager* um, const String& targetId)
{
	auto ok = false;

	for(int i = 0; i < data.getNumChildren(); i++)
	{
		if(matchesTarget(data.getChild(i), targetId))
		{
			ok = true;
			data.removeChild(i--, um);
		}
	}

	return ok;
}

bool Helpers::matchesTarget(const ValueTree& data, const String& targetId)
{
	jassert(data.getType() == Connection);

	if(targetId.isEmpty())
		return true;

	return data[TargetId].toString() == targetId;
}

void Helpers::fillModSourceList(const MainController* mc, StringArray& items)
{
	if(auto container = ProcessorHelpers::getFirstProcessorWithType<GlobalModulatorContainer>(mc->getMainSynthChain()))
	{
		auto mc = container->getChildProcessor(ModulatorSynth::InternalChains::GainModulation);
		for(int i = 0; i < mc->getNumChildProcessors(); i++)
			items.add(mc->getChildProcessor(i)->getId());
	}
}

void Helpers::fillModTargetList(const MainController* mc, StringArray& items, TargetType t)
{
	if(t == TargetType::All || t == TargetType::Modulators)
		items.addArray(ProcessorHelpers::getAllIdsForType<MatrixModulator>(mc->getMainSynthChain()));

	if(t == TargetType::All || t == TargetType::Parameters)
	{
		if(auto jmp = JavascriptMidiProcessor::getFirstInterfaceScriptProcessor(mc))
		{
			auto content = jmp->getScriptingContent();

			valuetree::Helpers::forEach(content->getContentProperties(), [&](ValueTree& c)
			{
				static const Identifier id("matrixTargetId");
				if(c.hasProperty(id))
				{
					auto tid = c[id].toString();

					if(tid.isNotEmpty())
						items.add(tid);
				}

				return false;
			});
		}
	}

	items.sortNatural();

	auto noConnectionIdx = items.indexOf("No connection");

	if(noConnectionIdx != -1)
	{
		items.remove(noConnectionIdx);
		items.insert(0, "No connection");
	}
}

void Helpers::startDragging(Component* c, int sourceIndex)
{
	if(auto tc = DragAndDropContainer::findParentDragContainerFor(c))
	{
		auto rc = dynamic_cast<Component*>(tc);

		DynamicObject::Ptr dragInfo = new DynamicObject();
		dragInfo->setProperty("Type", "ModulationDrag");
		dragInfo->setProperty(MatrixIds::SourceIndex, sourceIndex);

		var di(dragInfo.get());
		repaintMatrixSlidersOnDrag(rc, di, DragTargetType::Dragging);
		tc->startDragging(di, c);
	}
}

void Helpers::stopDragging(Component* c)
{
	if(auto tc = DragAndDropContainer::findParentDragContainerFor(c))
	{
		auto rc = dynamic_cast<Component*>(tc);

		DynamicObject::Ptr dragInfo = new DynamicObject();
		dragInfo->setProperty("Type", "ModulationDrag");
		dragInfo->setProperty(MatrixIds::SourceIndex, 0);
		var di(dragInfo.get());
		repaintMatrixSlidersOnDrag(rc, di, DragTargetType::Inactive);
	}
}

Helpers::TargetType Helpers::getTargetType(const MainController* mc, const String& targetId)
{
	if(targetId.isEmpty())
		return TargetType::All;

	StringArray items;
	fillModTargetList(mc, items, TargetType::Modulators);

	if(items.contains(targetId))
		return TargetType::Modulators;

	return TargetType::Parameters;
}


Helpers::IntensityTextConverter::IntensityTextConverter(const MatrixIds::Helpers::IntensityTextConverter::ConstructData& cd):
	AttributeListener(cd.p->getMainController()->getRootDispatcher()),
	data(cd),
	prettifier(ValueToTextConverter::createForMode(data.prettifierMode))
{
	if(cd.connectedSlider != nullptr)
	{
		uint16 pi = cd.parameterIndex;
		addToProcessor(cd.p, &pi, 1, dispatch::DispatchType::sendNotificationAsync);
	}
}

Helpers::IntensityTextConverter::~IntensityTextConverter()
{
	if(data.connectedSlider != nullptr)
		removeFromProcessor();
}

void Helpers::IntensityTextConverter::onAttributeUpdate(Processor*, uint16 index)
{
	if(data.connectedSlider != nullptr)
	{
		data.connectedSlider->updateText();
		data.connectedSlider->repaint();
	}
		
}

double Helpers::IntensityTextConverter::getValue(const String& text) const
{
	if(data.tm == modulation::TargetMode::Gain)
		return ValueToTextConverter::InverterFunctions::NormalizedPercentage(text);

	if(data.p != nullptr)
	{
		auto v = prettifier(text);
		FloatSanitizers::sanitizeDoubleNumber(v);

		auto inputRange = data.inputRange;
		// remove the interval to be able to set different values.
		inputRange.interval = 0;
		v = inputRange.convertTo0to1(v);

		if(data.tm != scriptnode::modulation::TargetMode::Gain)
		{
			v -= 0.5;
			v *= 2.0;
		}
		
		return v;
	}

	return 0.0;
}

String Helpers::IntensityTextConverter::getText(double v) const
{
	if(v == 0.0 && showInactive)
		return "Inactive";

	if(data.tm == modulation::TargetMode::Gain)
		return ValueToTextConverter::ConverterFunctions::NormalizedPercentage(v);

	if(data.p != nullptr)
	{
		auto ir = data.inputRange;
		ir.interval = 0;

		if(ir.skew == 1.0)
		{
			v = v * 0.5 + 0.5;
			v = ir.convertFrom0to1(v);

			if(data.tm == modulation::TargetMode::Unipolar)
				return prettifier(v);
			else
				return "+-" + prettifier(v).removeCharacters("+-");
		}
		else
		{
			auto value = data.p->getAttribute(data.parameterIndex);

			auto norm = ir.convertTo0to1(value);
			auto target = ir.convertFrom0to1(jlimit(0.0, 1.0, norm + v));
			auto diff = target - value;

			if(data.tm == modulation::TargetMode::Unipolar)
				return prettifier(diff);
			else
				return "+-" + prettifier(diff).removeCharacters("+-");
		}
	}
	
	return "disconnected";
}

}

} // namespace hise


