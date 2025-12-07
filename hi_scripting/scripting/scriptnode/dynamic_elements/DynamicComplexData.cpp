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

namespace data
{
namespace pimpl
{

using namespace snex;

dynamic_base::dynamic_base(data::base& b, ExternalData::DataType t, int index_) :
	dt(t),
	index(index_),
	dataNode(&b)
{

}

dynamic_base::~dynamic_base()
{
	if (forcedUpdateSource != nullptr)
		forcedUpdateSource->removeForcedUpdateListener(this);
}

void dynamic_base::initialise(NodeBase* p)
{
	parentNode = p;

	auto h = parentNode->getRootNetwork()->getExternalDataHolder();

	if ((forcedUpdateSource = dynamic_cast<ExternalDataHolderWithForcedUpdate*>(h)))
	{
		forcedUpdateSource->addForcedUpdateListener(this);
	}

	auto dataTree = parentNode->getValueTree().getOrCreateChildWithName(PropertyIds::ComplexData, parentNode->getUndoManager());
	auto dataName = ExternalData::getDataTypeName(dt);
	auto typeTree = dataTree.getOrCreateChildWithName(Identifier(dataName + "s"), parentNode->getUndoManager());

	if (typeTree.getNumChildren() <= index)
	{
		for (int i = 0; i <= index; i++)
		{
			ValueTree newChild(dataName);
			newChild.setProperty(PropertyIds::Index, -1, nullptr);
			newChild.setProperty(PropertyIds::EmbeddedData, -1, nullptr);
			typeTree.addChild(newChild, -1, parentNode->getUndoManager());
		}
	}

	cTree = typeTree.getChild(index);
	jassert(cTree.isValid());

	dataUpdater.setCallback(cTree, { PropertyIds::Index, PropertyIds::EmbeddedData }, valuetree::AsyncMode::Synchronously, BIND_MEMBER_FUNCTION_2(dynamic_base::updateData));

	getInternalData()->setGlobalUIUpdater(parentNode->getScriptProcessor()->getMainController_()->getGlobalUIUpdater());
	getInternalData()->getUpdater().addEventListener(this);

	setIndex(getIndex(), true);
}

void dynamic_base::onComplexDataEvent(ComplexDataUIUpdaterBase::EventType d, var data)
{
	if (d == ComplexDataUIUpdaterBase::EventType::ContentChange ||
		d == ComplexDataUIUpdaterBase::EventType::ContentRedirected)
	{
		if (currentlyUsedData == getInternalData() && parentNode != nullptr)
		{
			auto s = getInternalData()->toBase64String();
			cTree.setProperty(PropertyIds::EmbeddedData, s, parentNode->getUndoManager());
		}

		updateExternalData();
	}
}

void dynamic_base::forceRebuild(ExternalData::DataType dt_, int index)
{
	auto indexToUse = getIndex();

	if(indexToUse != -1 && dt_ == dt)
		setIndex(indexToUse, false);
}

void dynamic_base::updateExternalData()
{
	if (currentlyUsedData != nullptr)
	{
		auto updater = parentNode != nullptr ? parentNode->getScriptProcessor()->getMainController_()->getGlobalUIUpdater() : nullptr;
		auto um = parentNode != nullptr ? parentNode->getScriptProcessor()->getMainController_()->getControlUndoManager() : nullptr;

		currentlyUsedData->setGlobalUIUpdater(updater);
		currentlyUsedData->setUndoManager(um);

		ExternalData ed(currentlyUsedData, 0);

		{
#if 0
			if (currentlyUsedData->getDataLock().writeAccessIsLocked())
			{
				if (dataNode->externalData.data == ed.data)
				{
					sourceWatcher.setNewSource(currentlyUsedData);
					return;
				}
			}
#endif

			SimpleReadWriteLock::ScopedWriteLock sl(currentlyUsedData->getDataLock());
			setExternalData(*dataNode, ed, index);
		}
		
		sourceWatcher.setNewSource(currentlyUsedData);
	}
}

void dynamic_base::updateData(Identifier id, var newValue)
{
	if (id == PropertyIds::Index)
	{
		setIndex((int)newValue, false);
	}
	if (id == PropertyIds::EmbeddedData)
	{
		auto b64 = newValue.toString();

		if (b64 == "-1")
			b64 = "";

		if (getIndex() == -1)
		{
			auto thisString = getInternalData()->toBase64String();
            
            if(thisString == "-1")
                thisString = "";

			if (thisString.compare(b64) != 0)
				getInternalData()->fromBase64String(b64);
		}
	}
	
	if (forcedUpdateSource != nullptr)
	{
		forcedUpdateSource->sendForceUpdateMessage(this, dt, getIndex());
	}
}

int dynamic_base::getNumDataObjects(ExternalData::DataType t) const
{
	return (int)(t == dt);
}

void dynamic_base::setExternalData(data::base& n, const ExternalData& b, int index)
{
	SimpleRingBuffer::ScopedPropertyCreator sps(b.obj);
	n.setExternalData(b, index);
}

void dynamic_base::setIndex(int index, bool forceUpdate)
{
	ComplexDataUIBase* newData = nullptr;

	if (index != -1 && parentNode != nullptr)
	{
		if (auto h = parentNode->getRootNetwork()->getExternalDataHolder())
			newData = h->getComplexBaseType(dt, index);
	}
		
	if (newData == nullptr)
		newData = getInternalData();
	
	if (currentlyUsedData != newData || forceUpdate)
	{
		if (currentlyUsedData != nullptr)
			currentlyUsedData->getUpdater().removeEventListener(this);

		currentlyUsedData = newData;

		if (currentlyUsedData != nullptr)
			currentlyUsedData->getUpdater().addEventListener(this);

		updateExternalData();
	}
}

}

dynamic::sliderpack::sliderpack(data::base& t, int index):
	dynamicT<hise::SliderPackData>(t, index)
{
	this->internalData->setNumSliders(16);
}

void dynamic::sliderpack::initialise(NodeBase* p)
{
	dynamicT<hise::SliderPackData>::initialise(p);

	numParameterSyncer.setCallback(getValueTree(), { PropertyIds::NumParameters }, valuetree::AsyncMode::Synchronously, BIND_MEMBER_FUNCTION_2(sliderpack::updateNumParameters));
}

void dynamic::sliderpack::updateNumParameters(Identifier id, var newValue)
{
	if (auto sp = dynamic_cast<SliderPackData*>(currentlyUsedData))
	{
		sp->setNumSliders((int)newValue);
	}
}


void dynamic::audiofile::onComplexDataEvent(ComplexDataUIUpdaterBase::EventType d, var data)
{
	dynamicT<hise::MultiChannelAudioBuffer>::onComplexDataEvent(d, data);

	if (d != ComplexDataUIUpdaterBase::EventType::DisplayIndex)
	{
		sourceHasChanged(nullptr, currentlyUsedData);
	}
}

void dynamic::audiofile::initialise(NodeBase* n)
{
	auto mc = n->getScriptProcessor()->getMainController_();

	auto fileProvider = new PooledAudioFileDataProvider(mc);

	internalData->setProvider(fileProvider);

	internalData->registerXYZProvider("SampleMap", 
		[mc]() { return static_cast<MultiChannelAudioBuffer::XYZProviderBase*>(new hise::XYZSampleMapProvider(mc)); });

	internalData->registerXYZProvider("SFZ",
		[mc]() { return static_cast<MultiChannelAudioBuffer::XYZProviderBase*>(new hise::XYZSFZProvider(mc)); });

	dynamicT<hise::MultiChannelAudioBuffer>::initialise(n);

	allowRangeChange = true;

	rangeSyncer.setCallback(getValueTree(), { PropertyIds::MinValue, PropertyIds::MaxValue }, valuetree::AsyncMode::Synchronously, BIND_MEMBER_FUNCTION_2(audiofile::updateRange));
}

void dynamic::audiofile::sourceHasChanged(ComplexDataUIBase* oldSource, ComplexDataUIBase* newSource)
{
	if (allowRangeChange)
	{
		if (auto af = dynamic_cast<MultiChannelAudioBuffer*>(newSource))
		{
			auto r = af->getCurrentRange();
			getValueTree().setProperty(PropertyIds::MinValue, r.getStart(), newSource->getUndoManager());
			getValueTree().setProperty(PropertyIds::MaxValue, r.getEnd(), newSource->getUndoManager());
		}
	}
}

void dynamic::audiofile::updateRange(Identifier id, var newValue)
{
	if (auto af = dynamic_cast<MultiChannelAudioBuffer*>(currentlyUsedData))
	{
		auto min = (int)getValueTree()[PropertyIds::MinValue];
		auto max = (int)getValueTree()[PropertyIds::MaxValue];

		Range<int> nr(min, max);

		if (!nr.isEmpty())
			af->setRange({ min, max });
	}
}


dynamic::displaybuffer::displaybuffer(data::base& t, int index /*= 0*/) :
	dynamicT<SimpleRingBuffer>(t, index)
{

}

void dynamic::displaybuffer::initialise(NodeBase* n)
{
	dynamicT<hise::SimpleRingBuffer>::initialise(n);

	propertyListener.setCallback(getValueTree(), getCurrentRingBuffer()->getIdentifiers(), valuetree::AsyncMode::Synchronously, BIND_MEMBER_FUNCTION_2(displaybuffer::updateProperty));
}

void dynamic::displaybuffer::updateProperty(Identifier id, const var& newValue)
{
	if(!newValue.isVoid())
		getCurrentRingBuffer()->setProperty(id, newValue);
}

namespace ui
{
namespace pimpl
{

	editor_base::editor_base(ObjectType* b, PooledUIUpdater* updater) :
		ScriptnodeExtraComponent<ObjectType>(b, updater)
	{
		b->sourceWatcher.addSourceListener(this);
	}

	editor_base::~editor_base()
	{
		if (getObject() != nullptr)
			getObject()->sourceWatcher.removeSourceListener(this);
	}

	void editor_base::showProperties(SimpleRingBuffer* obj, Component* c)
	{
		XmlElement xml("Funky");

		auto pObj = obj->getPropertyObject();

		DynamicObject::Ptr dynObj = new DynamicObject();

		for (auto& nv : pObj->properties)
			dynObj->setProperty(nv.first, nv.second);

		

		auto ed = new JSONEditor(var(dynObj.get()));

		ed->setSize(500, 400);
		ed->setEditable(true);

		ed->setCallback([pObj](const var& o)
		{
			if (auto d = o.getDynamicObject())
			{
				for (auto& nv : d->getProperties())
					pObj->setProperty(nv.name, nv.value);
			}
		});

		c->findParentComponentOfClass<FloatingTile>()->showComponentInRootPopup(ed, c, {}, false);
	}

	Colour editor_base::getColourFromNodeComponent(NodeComponent* nc)
	{
		return nc->header.colour;
	}

	

	RingBufferPropertyEditor::Item::Item(dynamic::displaybuffer* b_, Identifier id_, const StringArray& entries, const String& value) :
		f(GLOBAL_FONT()),
		id(id_),
		db(b_)
	{
		addAndMakeVisible(cb);
		cb.addListener(this);
		cb.setLookAndFeel(&laf);
		cb.addItemList(entries, 1);
		cb.setText(value, dontSendNotification);
		cb.setColour(ComboBox::ColourIds::textColourId, Colour(0xFFDADADA));

		auto w = f.getStringWidthFloat(id.toString()) + 10.0f;

		setSize(w + 70, 28);
	}

	RingBufferPropertyEditor::RingBufferPropertyEditor(dynamic::displaybuffer* b, RingBufferComponentBase* e) :
		buffer(b),
		editor(e)
	{
		jassert(buffer != nullptr);
		jassert(editor != nullptr);

		if (auto rb = b->getCurrentRingBuffer())
		{
			for (auto& id : rb->getIdentifiers())
				addItem(id, { "1", "2" });
		}
	}

}

}
}

}

