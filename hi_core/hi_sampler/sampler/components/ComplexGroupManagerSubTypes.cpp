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


namespace hise {
using namespace juce;

struct ComplexGroupManagerComponent::LayerComponent::KeyboardComponent: public Component,
																	    public ComponentWithGroupManagerConnection
{
	static constexpr int KeyboardHeight = 48;

	struct PopupMenuHandler
	{
		virtual ~PopupMenuHandler() {};

		virtual void fillPopupMenu(PopupMenu& m, int noteNumber) = 0;

		virtual void onPopupResult(int result, int noteNumber) = 0;

		JUCE_DECLARE_WEAK_REFERENCEABLE(PopupMenuHandler);
	};

	KeyboardComponent(LogicTypeComponent& parent):
	  ComponentWithGroupManagerConnection(parent.getSampler()),
	  layerIndex(ComplexGroupManager::Helpers::getLayerIndex(parent.data)),
	  data(parent.data),
	  mapColour(ComplexGroupManagerComponent::Helpers::getLayerColour(parent.data))
	{
		parent.getSampleEditHandler()->noteBroadcaster.addListener(*this, onNote);
		rebuildKeyboard();
		getComplexGroupManager()->addPlaystateListener(*this, layerIndex, onPlaystateChange);
	}

	void rebuildKeyboard()
	{
		mappedKeys = ComplexGroupManager::Helpers::getKeyRange(data, getSampler());
		groupNoteMap.clear();

		if(mappedKeys.isEmpty())
		{
			octaveRange = {};
			noteRange = {};
		}
		else
		{
			auto low = mappedKeys.getFirstSetBit();
			auto high = (int)mappedKeys.getHighestSetBit();

			auto lowOctave = low - (low % 12);
			auto highOctave = high - (high % 12) + 12;

			octaveRange = { lowOctave / 12, highOctave / 12};
			noteRange = { low, high };

			if(octaveRange.getLength() == 1)
				octaveRange.setLength(2);

			uint8 layerValue = 1;

			for(int i = 0; i < 128; i++)
			{
				if(mappedKeys[i])
					groupNoteMap[layerValue++] = i;
			}
		}

		repaint();
	}

	std::map<uint8, int> groupNoteMap;

	VoiceBitMap<128> playedNotes;
	bool learnActive = false;

	int getHeightToUse() const override { return KeyboardHeight; }

	static void onNote(KeyboardComponent& k, int note, int velo)
	{
		if(k.learnActive)
		{
			k.data.setProperty(SampleIds::LoKey, note, k.getUndoManager());
			k.learnActive = false;
			return;
		}

		k.playedNotes.setBit(note, velo != 0);
		k.repaint();
	}

	static void onPlaystateChange(KeyboardComponent& k, uint8 s)
	{
		k.selected = k.groupNoteMap[s];
		k.repaint();
	}

	int getNoteNumber(int position) const
	{
		auto numOctavesToShow = octaveRange.getLength();
		auto b = getLocalBounds().toFloat();
		auto keyWidth = b.getWidth() / (float)(numOctavesToShow * 12) + 1;

		return roundToInt((float)position / (float)keyWidth) + octaveRange.getStart() * 12;
	}

	void showPopupMenu(int position)
	{
		if(popupMenuHandler != nullptr)
		{
			PopupLookAndFeel plaf;
			PopupMenu m;

			auto noteNumber = getNoteNumber(position);

			popupMenuHandler->fillPopupMenu(m, noteNumber);

			if(auto r = m.show())
			{
				popupMenuHandler->onPopupResult(r, noteNumber);
			}
		}
	}

	void mouseDown(const MouseEvent& e) override
	{
		auto position = e.getPosition().x;

		if(e.mods.isRightButtonDown() || mappedKeys.isEmpty())
		{
			showPopupMenu(position);
		}
		else
		{
			auto noteNumber = getNoteNumber(position);

			if(auto cg = findParentComponentOfClass<ComponentWithGroupManagerConnection>())
			{
				currentlyPlayedNote = noteNumber;
				auto& ks = cg->getSampler()->getMainController()->getKeyboardState();
				ks.noteOn(1, currentlyPlayedNote, 1.0);
			}
		}
	}

	void mouseUp(const MouseEvent& e) override
	{
		if(currentlyPlayedNote != -1)
		{
			if(auto cg = findParentComponentOfClass<ComponentWithGroupManagerConnection>())
			{
				auto& ks = cg->getSampler()->getMainController()->getKeyboardState();
				ks.noteOff(1, currentlyPlayedNote, 0.0f);
				currentlyPlayedNote = -1;
			}
		}
	}

	int currentlyPlayedNote = -1;

	void paint(Graphics& g) override
	{
		auto b = getLocalBounds().toFloat();

		

		if(mappedKeys.isEmpty())
		{
			String m;
			m << "No key range assigned. Click to setup the MIDI note range for this layer";
			g.setColour(Colours::white.withAlpha(0.4f));
			g.setFont(GLOBAL_BOLD_FONT());
			g.drawText(m, b, Justification::centred);
		}

		auto top = b.removeFromTop(18);
		auto numOctavesToShow = octaveRange.getLength();

		auto keyWidth = b.getWidth() / (float)(numOctavesToShow * 12);

		static const bool black[12] = { false, true, false, true, false, false, true, false, true, false, true, false };

		constexpr int Gap = 1;

		for(int i = 0; i < numOctavesToShow; i++)
		{
			auto isLowerOctave = i == 0;
			auto isUpperOctave = i == (numOctavesToShow - 1);

			auto o = top.removeFromLeft(keyWidth * 12);
			g.setColour(Colours::white.withAlpha(0.3f));
			g.setFont(GLOBAL_BOLD_FONT());
			g.drawText("C" + String(i + octaveRange.getStart() - 2), o, Justification::left);

			for(int j = 0; j < 12; j++)
			{
				int noteNumber = (i + octaveRange.getStart()) * 12 + j;

				float alpha = 0.2f;

				if(mappedKeys[noteNumber])
					alpha += 0.5f;

				auto c = (black[j] ? Colour(0xFF555555) : Colours::white);

				if(playedNotes[noteNumber])
					c = Colour(SIGNAL_COLOUR);

				g.setColour(c.withAlpha(alpha));
				
				auto k = b.removeFromLeft(keyWidth - Gap);

				if(noteNumber == selected)
				{
					g.setColour(mapColour);
				}

				if(isLowerOctave && j == 0)
				{
					Path p;
					p.addRoundedRectangle(k.getX(), k.getY(), k.getWidth(), k.getHeight(), 4.0f, 4.0f, true, false, true, false);
					g.fillPath(p);
				}
				else if (isUpperOctave && j == 11)
				{
					Path p;
					p.addRoundedRectangle(k.getX(), k.getY(), k.getWidth(), k.getHeight(), 4.0f, 4.0f, false, true, false, true);
					g.fillPath(p);
				}
				else
				{
					g.fillRect(k);
				}

				

				b.removeFromLeft((float)Gap);
			}
		}
	}

	void setKeyboardProperties(const Array<Identifier>& idsToWatch)
	{
		rebuildListener.setCallback(data, idsToWatch, valuetree::AsyncMode::Asynchronously, [this](const Identifier& id, const var& nv)
		{
			rebuildKeyboard();
		});
	}

	valuetree::PropertyListener rebuildListener;

	Range<int> octaveRange;
	Range<int> noteRange;

	VoiceBitMap<128> mappedKeys;
	Colour mapColour = Colours::red;

	int selected = -1;
	const uint8 layerIndex;
	ValueTree data;

	WeakReference<PopupMenuHandler> popupMenuHandler;

	bool initialised = false;

	JUCE_DECLARE_WEAK_REFERENCEABLE(KeyboardComponent);
};

struct ComplexGroupManagerComponent::RRBody: public LogicTypeComponent::BodyBase
{
	static constexpr int CircleWidth = 24;
	static constexpr int CircleMargin = 24;

	RRBody(LogicTypeComponent& parent):
	  BodyBase(parent)
	{
		uint8 idx = 0;

		for(auto t: tokens)
			addAndMakeVisible(buttons.add(new CircleButton(parent.data, idx++)));
	}

	int getHeightToUse() const override { return jmax(100, height + 10); }

	struct CircleButton: public ButtonBase
	{
		CircleButton(const ValueTree& layerData, uint8 rrIndex_):
		  ButtonBase(layerData, rrIndex_ + 1),
		  rrIndex(rrIndex_)
		{
			setSize(getWidthToUse(), CircleWidth);
		}

		int getWidthToUse() const override { return CircleWidth; }

		void paint(Graphics& g) override
		{
			g.setColour(getButtonColour(true));

			auto cb = getLocalBounds().toFloat().reduced(3.0f);

			g.drawEllipse(cb, 2.0f);

			if(active)
				g.fillEllipse(cb.reduced(4.0f));
		}

		const uint8 rrIndex = 0;
	};

	void resized() override
	{
		SimpleFlexbox fb;

		fb.padding = CircleMargin;
		fb.justification = Justification::centred;

		//for(auto b: buttons)
			//b->setSize(CircleWidth, CircleWidth);

		auto buttonList = getAllButtonsToShow();

		auto list = fb.createBoundsListFromComponents(buttonList);

		if(auto newHeight = fb.apply(list, getLocalBounds().removeFromTop(100).reduced(5)))
		{
			height = newHeight;

			fb.applyBoundsListToComponents(buttonList, list);
			
			if(getHeight() != getHeightToUse())
			{
				findParentComponentOfClass<Content>()->updateSize();
			}
		}
		
		BodyBase::resized();
	}

	int height = CircleWidth;

	int getCentreXPositionForToken(uint8 tokenIndex) const override
	{
		if(auto b = buttons[tokenIndex])
			return b->getBoundsInParent().getCentreX();

		return 0;
	}
};

struct ComplexGroupManagerComponent::KeyswitchBody: public LogicTypeComponent::BodyBase,
													public LayerComponent::KeyboardComponent::PopupMenuHandler
{
	KeyswitchBody(LogicTypeComponent& parent):
	  BodyBase(parent),
	  keyboard(parent),
	  editButton("settings", nullptr, f)
	{
		keyboard.mapColour = ComplexGroupManagerComponent::Helpers::getLayerColour(parent.data);
		keyboard.popupMenuHandler = this;

		keyboard.setKeyboardProperties({ SampleIds::LoKey, groupIds::isChromatic, groupIds::tokens });

		addAndMakeVisible(keyboard);
		addAndMakeVisible(editButton);

		editButton.onClick = [this]()
		{
			keyboard.showPopupMenu(0);
		};

		uint8 layerValue = 1;

		for(auto t: tokens)
			addAndMakeVisible(buttons.add(new KeyswitchButton(parent.data, layerValue++, t, parent.data[groupIds::purgable])));

		keyboard.rebuildKeyboard();
	}

	int getHeightToUse() const override
	{
		int h = 0;
		h += 2 * LayerComponent::BodyMargin;
		h += jmax(100, height);
		h += LayerComponent::KeyboardComponent::KeyboardHeight;

		return h;
	}

	int getCentreXPositionForToken(uint8 tokenIndex) const override
	{
		if(auto p = buttons[tokenIndex])
			return p->getBoundsInParent().getCentreX();

		return 0;
	}

	int height = LayerComponent::TopBarHeight;

	void fillPopupMenu(PopupMenu& m, int noteNumber) override
	{
		PopupMenu sub;

		auto range = Helpers::getKeyRange(data, getSampler());
		auto firstBit = range.getFirstSetBit();

		for(int i = 0; i < 128; i++)
		{
			sub.addItem(i+1000, MidiMessage::getMidiNoteName(i, true, true, 3), true, i == firstBit);
		}

		m.addSubMenu("Set keyswitch start note", sub);

		m.addItem(2, "Set keyswitch start note to next played MIDI note", true, keyboard.learnActive);
		m.addItem(1, "Use white keys", true, !(bool)data[groupIds::isChromatic]);
	}

	void onPopupResult(int result, int noteNumber) override
	{
		if(result == 1)
		{
			data.setProperty(groupIds::isChromatic, !(bool)data[groupIds::isChromatic], getUndoManager());
		}
		else if (result == 2)
		{
			keyboard.learnActive = !keyboard.learnActive;
		}
		else
		{
			auto startNote = result - 1000;
			data.setProperty(SampleIds::LoKey, startNote, getUndoManager());
		}
	}

	void resized() override
	{
		auto b = getLocalBounds().reduced(LayerComponent::BodyMargin);

		for(auto b: buttons)
			b->setSize(b->getWidthToUse(), LayerComponent::TopBarHeight);

		SimpleFlexbox fb;
		fb.justification = Justification::centred;

		auto buttonList = getAllButtonsToShow();

		auto list = fb.createBoundsListFromComponents(buttonList);

		if(auto h = fb.apply(list, b.removeFromTop(100)))
		{
			height = h;

			fb.applyBoundsListToComponents(buttonList, list);

			if(getHeight() != getHeightToUse())
			{
				findParentComponentOfClass<Content>()->updateSize();
			}
		}

		keyboard.setBounds(b.removeFromBottom(LayerComponent::KeyboardComponent::KeyboardHeight));

		auto tb = keyboard.getBounds();

		tb = tb.removeFromRight(tb.getHeight()).translated(tb.getHeight(), 0).reduced(3).getIntersection(getLocalBounds());
		editButton.setBounds(tb);

		BodyBase::resized();
	}

	LayerComponent::KeyboardComponent keyboard;

	ComplexGroupManagerComponent::Factory f;

	HiseShapeButton editButton;
};

struct ComplexGroupManagerComponent::TableFadeBody: public LogicTypeComponent::BodyBase,
													public ComboBox::Listener
{
	TableFadeBody(LogicTypeComponent& parent):
	  BodyBase(parent),
	  popupButton("edit", nullptr, f)
	{
		addAndMakeVisible(table);
		addAndMakeVisible(tableSelector);
		addAndMakeVisible(popupButton);

		tableSelector.addItemList(tokens, 1);

		tableSelector.addListener(this);

		popupButton.onClick = [this]()
		{
			auto t = new RRDisplayComponent::XFadeEditor(getSampler());
			auto root = findParentComponentOfClass<FloatingTile>()->getRootFloatingTile();
			root->showComponentInRootPopup(t, &popupButton, { getLocalBounds().getCentreX(), 15 }, false);
		};

		tableSelector.setSelectedId(1, sendNotificationSync);

		tableSelector.setLookAndFeel(&laf);

		uint8 layerValue = 1;

		for(auto t: tokens)
			addAndMakeVisible(buttons.add(new KeyswitchButton(parent.data, layerValue++, t, false)));
	}

	int getHeightToUse() const override { return 220 + LogicTypeComponent::TopBarHeight; }

	void comboBoxChanged(ComboBox* comboBoxThatHasChanged) override
	{
		if(comboBoxThatHasChanged == &tableSelector)
		{
			auto id = tableSelector.getSelectedId() - 1;

			if(isPositiveAndBelow(id, getSampler()->getNumDataObjects(ExternalData::DataType::Table)))
			{
				auto t = getSampler()->getTable(id);
				t->setGlobalUIUpdater(getMainController()->getGlobalUIUpdater());
				t->setUndoManager(getMainController()->getControlUndoManager());
				table.setComplexDataUIBase(t);
			}
		}
	}

	virtual int getCentreXPositionForToken(uint8 tokenIndex) const
	{
		auto tb = table.getBoundsInParent();

		auto normX = (float)tokenIndex / (float)(tokens.size() - 1);

		return tb.getX() + roundToInt(normX * (float)tb.getWidth());
	}

	void resized() override
	{
		auto b = getLocalBounds().reduced(LogicTypeComponent::BodyMargin);

		b.removeFromRight(LogicTypeComponent::PaddingLeft + 3 * LogicTypeComponent::BodyMargin);
		b.removeFromLeft(3 * LogicTypeComponent::BodyMargin);
		
		auto cb = b.removeFromBottom(28);

		tableSelector.setBounds(cb.removeFromLeft(128));
		popupButton.setBounds(cb.removeFromRight(cb.getHeight()).reduced(4));

		b.removeFromBottom(LogicTypeComponent::BodyMargin);

		b.removeFromLeft(LogicTypeComponent::BodyMargin);

		auto buttonRow = b.removeFromTop(LogicTypeComponent::TopBarHeight);

		b.removeFromTop(LogicTypeComponent::BodyMargin);

		table.setBounds(b);

		Array<int> positions;

		calculateXPositionsForItems(positions);

		auto buttonList = getAllButtonsToShow();

		if(unassignedButton->isVisible())
		{
			jassertfalse;
		}
		if(ignoreButton->isVisible())
		{
			jassertfalse;
		}

		for(int i = 0; i < buttons.size(); i++)
		{
			auto b = buttons[i];
			b->setSize(b->getWidthToUse(), LogicTypeComponent::TopBarHeight - 4);
			b->setCentrePosition(positions[i] - LogicTypeComponent::PaddingLeft, buttonRow.getCentreY());
		}

		BodyBase::resized();
	}

	Factory f;

	TableEditor table;
	ComboBox tableSelector;
	HiseShapeButton popupButton;

	SelectorLookAndFeel laf;

};

struct LayerPropertySelector: public ComboBox,
							  public ComponentWithGroupManagerConnection
{
	enum DataStorageType
	{
		Index,
		Text,
		numDataStorageTypes
	};

	LayerPropertySelector(ComponentWithGroupManagerConnection& parent, DataStorageType type_):
	  ComponentWithGroupManagerConnection(parent.getSampler()),
	  type(type_)
	{
		setLookAndFeel(&laf);
		onChange = [this]()
		{
			auto s = getText();

			if(type == DataStorageType::Text)
				layerData.setProperty(id, s, getUndoManager());
			else
				layerData.setProperty(id, items.indexOf(s), getUndoManager());

			if(additionalCallback)
				additionalCallback();
		};
	}

	int getHeightToUse() const override { return 28; }

	void setup(const ValueTree& v, const Identifier& id_, const StringArray& items_)
	{
		clear(dontSendNotification);
		addItemList(items_, 1);

		id = id_;
		jassert(v.getType() == groupIds::Layer);
		
		items = items_;

		if(!layerData.isValid())
		{
			layerData = v;
			listener.setCallback(layerData, { id }, valuetree::AsyncMode::Asynchronously, BIND_MEMBER_FUNCTION_2(LayerPropertySelector::update));
		}
		
		if(layerData.hasProperty(id))
			update(id, layerData[id]);
	}

	void update(const Identifier& id, const var& newValue)
	{
		if(type == DataStorageType::Text)
			setText(newValue.toString(), dontSendNotification);
		else
			setSelectedItemIndex((int)newValue, dontSendNotification);
	}

	std::function<void()> additionalCallback;

	Identifier id;
	UndoManager* um;
	StringArray items;
	const DataStorageType type;
	ValueTree layerData;
	valuetree::PropertyListener listener;
	ComplexGroupManagerComponent::SelectorLookAndFeel laf;
};

struct ComplexGroupManagerComponent::XFadeBody: public LogicTypeComponent::BodyBase
{
	XFadeBody(LogicTypeComponent& parent):
	  BodyBase(parent),
	  slotSelector(*this, LayerPropertySelector::DataStorageType::Index),
	  modeSelector(*this, LayerPropertySelector::DataStorageType::Text),
	  sourceSelector(*this, LayerPropertySelector::DataStorageType::Text),
	  fadeTime(parent, groupIds::fadeTime, { 0.0, 1000.0, 1.0 })
	{
		uint8 layerValue = 1;

		for(auto t: tokens)
			addAndMakeVisible(buttons.add(new KeyswitchButton(parent.data, layerValue++, t, false)));

		addAndMakeVisible(slotSelector);
		addAndMakeVisible(modeSelector);
		addAndMakeVisible(sourceSelector);
		
		addAndMakeVisible(fadeTime);
		fadeTime.setTextConverter("Time");
		fadeTime.setSkewFactorFromMidPoint(100.0);
		fadeTime.setTooltip("The fade time when using CC messages or event data");

		auto idx = Helpers::getLayerIndex(parent.data);

		if(auto l = dynamic_cast<ComplexGroupManager::XFadeLayer*>(getComplexGroupManager()->layers[idx]))
		{
			l->valueUpdater.addListener(*this, updateValue, true);
		}

		modeSelector.additionalCallback = [this]()
		{
			resized();
		};

		StringArray slots;

		modeSelector.setup(parent.data, groupIds::fader, ComplexGroupManager::XFadeLayer::getFaderNames());
		sourceSelector.setup(parent.data, { groupIds::sourceType }, ComplexGroupManager::XFadeLayer::getSourceNames() );

		sourceSelector.additionalCallback = BIND_MEMBER_FUNCTION_0(XFadeBody::rebuildSlotItems);
		rebuildSlotItems();
	};

	void rebuildSlotItems()
	{
		auto idx = ComplexGroupManager::XFadeLayer::getSourceNames().indexOf(sourceSelector.getText());

		StringArray sa;

		if(idx != -1)
		{
			auto sourceType = (ComplexGroupManager::XFadeLayer::SourceTypes)idx;

			fadeTime.setEnabled(sourceType != ComplexGroupManager::XFadeLayer::SourceTypes::GlobalMod);

			switch (sourceType)
			{
			case ComplexGroupManager::XFadeLayer::SourceTypes::EventData:
				for(int i = 0; i < AdditionalEventStorage::NumDataSlots; i++)
					sa.add("Event Data Slot #" + String((i+1)));
				break;
			case ComplexGroupManager::XFadeLayer::SourceTypes::MidiCC:
				sa.add("PitchBend");
				for(int i = 1; i < 128; i++)
					sa.add("CC #" + String(i));
				break;
			case ComplexGroupManager::XFadeLayer::SourceTypes::GlobalMod: 
			{
				auto chain = getSampler()->getMainController()->getMainSynthChain();
				if(auto container = ProcessorHelpers::getFirstProcessorWithType<GlobalModulatorContainer>(chain))
				{
					auto c = container->getChildProcessor(ModulatorSynth::GainModulation);

					for(int i = 0; i < c->getNumChildProcessors(); i++)
						sa.add(c->getChildProcessor(i)->getId());
				}

				break;
			}
			case ComplexGroupManager::XFadeLayer::SourceTypes::numSourceTypes: break;
			default: ;
			}
			
		}
		
		slotSelector.setup(data, groupIds::slotIndex, sa);
	}

	static void updateValue(XFadeBody& b, int voiceIndex, double value)
	{
		b.values[voiceIndex] = value;
		b.repaint();
	}

	template <int Index> Path createPath(int numPaths, ComplexGroupManager::XFadeLayer::FaderTypes type) const
	{
		int numPixels = 256;

		Path p;
		p.startNewSubPath(0.0f, 0.0f);

		if (numPixels > 0)
		{
			for (int i = 0; i < numPixels; i++)
			{
				double inputValue = (double)i / (double)numPixels;
				auto output = ComplexGroupManager::XFadeLayer::getFaderValueInternal<Index>(inputValue, numPaths, type);

				p.lineTo((float)i, -1.0f * output);
			}
		}

		p.lineTo(numPixels - 1, 0.0f);
		p.closeSubPath();

		return p;
	}

	int getHeightToUse() const override { return 5 * LayerComponent::BodyMargin + 15 + 80 + LayerComponent::TopBarHeight + slotSelector.getHeightToUse(); }

	void resized() override
	{
		BodyBase::resized();

		paths.clear();
		auto numPaths = tokens.size();

		if(numPaths >= 2)
		{
			using FaderType = ComplexGroupManager::XFadeLayer::FaderTypes;

			auto faderNames = ComplexGroupManager::XFadeLayer::getFaderNames();

			auto idx = faderNames.indexOf(data[groupIds::fader].toString());

			if(isPositiveAndBelow(idx, faderNames.size()))
			{
				auto type = (FaderType)idx;

				paths.add(createPath<0>(numPaths, type));
				paths.add(createPath<1>(numPaths, type));

				if(numPaths > 2)
					paths.add(createPath<2>(numPaths, type));
				if(numPaths > 3)
					paths.add(createPath<3>(numPaths, type));
				if(numPaths > 4)
					paths.add(createPath<4>(numPaths, type));
				if(numPaths > 5)
					paths.add(createPath<5>(numPaths, type));
				if(numPaths > 6)
					paths.add(createPath<6>(numPaths, type));
				if(numPaths > 7)
					paths.add(createPath<7>(numPaths, type));
			}

			
		}

		auto b = getLocalBounds();

		b.removeFromTop(LayerComponent::BodyMargin);
		b.removeFromTop(LayerComponent::BodyMargin);

		auto top = b.removeFromTop(LayerComponent::TopBarHeight);

		b.removeFromTop(LayerComponent::BodyMargin);

		auto fb = b.removeFromTop(80).withSizeKeepingCentre(256, 80).toFloat();

		pathArea = fb;

		if(fb.getX() > 0)
		{
			for(auto& p: paths)
				p.scaleToFit(fb.getX(), fb.getY(), fb.getWidth(), fb.getHeight(), false);
		}

		auto buttonList = getAllButtonsToShow();

		if(unassignedButton->isVisible())
		{
			unassignedButton->setBounds(top.removeFromLeft(unassignedButton->getWidthToUse()));
		}

		if(ignoreButton->isVisible())
		{
			ignoreButton->setBounds(top.removeFromRight(unassignedButton->getWidthToUse()));
		}
		
		if(tokens.size() >= 2)
		{
			auto w = 256 / (tokens.size() - 1);
			auto x = fb.getX();

			for(auto kb: buttons)
			{
				kb->setSize(kb->getWidthToUse(), LayerComponent::TopBarHeight);
				kb->setCentrePosition(x, top.getY());
				x += w;
			}
		}

		b.removeFromTop(LayerComponent::BodyMargin);

		auto bottom = b;

		bottom.removeFromBottom(LayerComponent::BodyMargin);

		modeSelector.setBounds(bottom.removeFromLeft(100).reduced(0, 7));
		bottom.removeFromLeft(LayerComponent::BodyMargin);
		slotSelector.setBounds(bottom.removeFromLeft(100).reduced(0, 7));
		bottom.removeFromLeft(LayerComponent::BodyMargin);
		sourceSelector.setBounds(bottom.removeFromLeft(100).reduced(0, 7));
		fadeTime.setBounds(bottom.removeFromRight(100).withSizeKeepingCentre(100, 35));

		BodyBase::resized();
	}

	int getCentreXPositionForToken(uint8 tokenIndex) const override
	{
		if(auto b = buttons[tokenIndex])
			return b->getBoundsInParent().getCentreX();

		return 0;
	}

	void paint(Graphics& g) override
	{
		BodyBase::paint(g);

		if(paths.isEmpty())
		{
			g.setColour(Colours::white.withAlpha(0.2f));
			g.setFont(GLOBAL_BOLD_FONT());
			g.drawText("No tokens in this layer", getLocalBounds().toFloat(), Justification::centred);
		}
		else
		{
			g.setColour(Colours::white.withAlpha(0.2f));

			for(const auto& p: paths)
			{
				g.fillPath(p);
				g.strokePath(p, PathStrokeType(1.0f));
			}

			auto x = pathArea.getX() + values[0] * pathArea.getWidth();

			g.setColour(Colours::white);
			g.drawVerticalLine(x, pathArea.getY(), pathArea.getBottom());
		}
	}

	span<float, NUM_POLYPHONIC_VOICES> values;

	Rectangle<float> pathArea;

	faders::cosine_half lf;

	Array<Path> paths;

	LayerPropertySelector slotSelector;
	LayerPropertySelector sourceSelector;
	LayerPropertySelector modeSelector;
	LogicTypeComponent::BodyKnob fadeTime;

	JUCE_DECLARE_WEAK_REFERENCEABLE(XFadeBody);
};



struct ComplexGroupManagerComponent::LegatoBody: public LogicTypeComponent::BodyBase
{
	LegatoBody(LogicTypeComponent& parent):
	  BodyBase(parent),
	  keyboard(parent),
	  transition(parent)
	{
		uint8 layerValue = 1;

		for(auto t: tokens)
		{
			auto n = MidiMessage::getMidiNoteName(t.getIntValue(), true, true, 3);
			addAndMakeVisible(buttons.add(new KeyswitchButton(parent.data, layerValue++, n, false)));
			buttons.getLast()->setSize(buttons.getLast()->getWidthToUse(), ButtonHeight);
		}
			
		transition.startOffset = keyboard.noteRange.getStart() - 1;

		addAndMakeVisible(keyboard);
		addAndMakeVisible(transition);

		
		
	}

	

	void paint(Graphics& g) override
	{
		drawLabelsAndRulers(g);
		BodyBase::paint(g);
	}

	int getHeightToUse() const override
	{
		return LayerComponent::BodyMargin + AddComponent::LabelHeight +
			   height + LayerComponent::BodyMargin +
			   keyboard.getHeightToUse() + LayerComponent::BodyMargin +
			   transition.getHeightToUse() + LayerComponent::BodyMargin;
	}

	void resized() override
	{
		clearRulersAndLabels();

		auto b = getLocalBounds().reduced(LayerComponent::BodyMargin);

		addLabel(b.removeFromTop(AddComponent::LabelHeight), "Start note groups:");

		SimpleFlexbox fb;


		//for(auto b: buttons)
		//	b->setSize(b->getWidthToUse(), LayerComponent::TopBarHeight);

		auto buttonList = getAllButtonsToShow();

		auto pos = fb.createBoundsListFromComponents(buttonList);

		if(auto h = fb.apply(pos, b.removeFromTop(height)))
		{
			height = h;

			fb.applyBoundsListToComponents(buttonList, pos);

			if(getHeight() != getHeightToUse())
				findParentComponentOfClass<Content>()->updateSize();
		}

		transition.setBounds(b.removeFromBottom(transition.getHeightToUse()));

		

		b.removeFromBottom(LayerComponent::BodyMargin);
		keyboard.setBounds(b.removeFromBottom(keyboard.getHeightToUse()));
		addRuler(b.removeFromBottom(LayerComponent::BodyMargin));

		BodyBase::resized();
	}

	struct TransitionDisplay: public Component,
							  public ComponentWithGroupManagerConnection
	{
		TransitionDisplay(LogicTypeComponent& parent):
		  ComponentWithGroupManagerConnection(parent.getSampler())
		{
			parent.getComplexGroupManager()->addPlaystateListener(*this, Helpers::getLayerIndex(parent.data), onPlaystate);
			parent.getSampleEditHandler()->noteBroadcaster.addListener(*this, onNote);
		}

		void paint(Graphics& g) override
		{
			auto b = getLocalBounds().toFloat();
			g.setColour(Colours::white.withAlpha(0.05f));
			g.fillRect(b.reduced(4.0f));
			g.setColour(Colours::white.withAlpha(0.3f));
			g.drawRoundedRectangle(b.reduced(1.0f), 4.0f, 1.0f);

			String msg;

			if(currentStart != ComplexGroupManager::IgnoreFlag)
			{
				auto s = MidiMessage::getMidiNoteName(currentStart + startOffset, true, true, 3);
				auto e = MidiMessage::getMidiNoteName(currentNote, true, true, 3);

				msg << s << " -> " << e;
			}
			else
				msg << "no transition";

			Helpers::drawLabel(g, b, msg, Justification::centred);
		}

		static void onNote(TransitionDisplay& t, int note, int velo)
		{
			if(velo != 0)
			{
				t.currentNote = note;
				t.repaint();
			}
		}

		uint8 currentStart = ComplexGroupManager::IgnoreFlag;
		int currentNote = -1;
		int startOffset = 0;

		static void onPlaystate(TransitionDisplay& t, uint8 value)
		{
			t.currentStart = value;
			t.repaint();
		}

		int getHeightToUse() const override { return 20; }

		JUCE_DECLARE_WEAK_REFERENCEABLE(TransitionDisplay);
	} transition;

	LayerComponent::KeyboardComponent keyboard;

	int height = LayerComponent::TopBarHeight;

};

struct ComplexGroupManagerComponent::ChokeBody: public LogicTypeComponent::BodyBase,
												public ComboBox::Listener
{
	ChokeBody(LogicTypeComponent& parent):
	  BodyBase(parent),
	  fadeTime(parent, groupIds::fadeTime, { 0.0, 1000.0, 1.0 })
	{
		addAndMakeVisible(fadeTime);
		fadeTime.setTextConverter("Time");
		fadeTime.setSkewFactorFromMidPoint(100.0);
		fadeTime.setTooltip("The fade time when fading out choked notes");

		uint8 layerValue = 1;

		for (auto t : tokens)
		{
			auto kw = new KeyswitchButton(parent.data, layerValue++, t, parent.data[groupIds::purgable]);
			addAndMakeVisible(buttons.add(kw));
			groupButtons.add(kw);

			auto cb = new ComboBox();
			cb->addItem("Off", ComplexGroupManager::IgnoreFlag);
			cb->addItem("A", 1);
			cb->addItem("B", 2);
			cb->addItem("C", 3);
			cb->addItem("D", 4);
			cb->setTooltip("Set the choke group for the layer above");
			cb->setLookAndFeel(&slaf);

			cb->addListener(this);
			chokeMatrix.add(cb);

			addAndMakeVisible(cb);
		}

		matrixListener.setCallback(parent.data, { groupIds::matrixString }, valuetree::AsyncMode::Asynchronously, VT_BIND_PROPERTY_LISTENER(updateMatrix));
		updateMatrix(groupIds::matrixString, parent.data[groupIds::matrixString]);
	}

	void comboBoxChanged(ComboBox* cb) override
	{
		std::vector<uint8> m;

		for(auto c: chokeMatrix)
			m.push_back((uint8)c->getSelectedId());

		auto newString = ComplexGroupManager::Helpers::getStringFromMatrix(m);
		data.setProperty(groupIds::matrixString, newString, getUndoManager());
	}

	void updateMatrix(const Identifier& id, const var& newValue)
	{
		if(newValue.isString() && newValue.toString().length() == chokeMatrix.size())
		{
			auto s = newValue.toString();
			auto m = ComplexGroupManager::Helpers::getMatrixFromString(newValue.toString());

			if (m.size() == chokeMatrix.size())
			{
				for (int i = 0; i < m.size(); i++)
					chokeMatrix[i]->setSelectedId((int)m[i], dontSendNotification);
			}
		}
		else
		{
			for(auto c: chokeMatrix)
				c->setSelectedId(ComplexGroupManager::IgnoreFlag, dontSendNotification);
		}
	}

	void resized() override
	{
		SimpleFlexbox fb;

		fb.padding = 10;
		fb.justification = Justification::centred;

		for (auto b : buttons)
			b->setSize(b->getWidthToUse(), LayerComponent::TopBarHeight);

		auto buttonList = getAllButtonsToShow();

		auto list = fb.createBoundsListFromComponents(buttonList);

		auto b = getLocalBounds().removeFromTop(130).reduced(5);

		auto lb = b.removeFromRight(60);

		fadeTime.setBounds(lb.withSizeKeepingCentre(60, 35));

		if (auto newHeight = fb.apply(list, b))
		{
			height = newHeight;

			fb.applyBoundsListToComponents(buttonList, list);

			for (int i = 0; i < groupButtons.size(); i++)
			{
				auto gb = groupButtons[i]->getBoundsInParent();
				auto c = chokeMatrix[i];
				c->setBounds(gb.translated(0.0, 40));
			}

			if (getHeight() != getHeightToUse())
			{
				findParentComponentOfClass<Content>()->updateSize();
			}
		}

		BodyBase::resized();
	}

	int getHeightToUse() const override
	{
		return jmax(100, height + 50);
	}

	int height = 10;
	LogicTypeComponent::BodyKnob fadeTime;

	SelectorLookAndFeel slaf;
	Array<Component::SafePointer<KeyswitchButton>> groupButtons;
	OwnedArray<ComboBox> chokeMatrix;
	valuetree::PropertyListener matrixListener;
};




struct ComplexGroupManagerComponent::ReleaseBody: public LogicTypeComponent::BodyBase,
												  public PathFactory
{
	

	

	ReleaseBody(LogicTypeComponent& parent):
	  BodyBase(parent),
	  releaseStartOptions("release", nullptr, *this),
	  fadeTime(parent, groupIds::fadeTime, { 0.0, 1000.0, 1.0} ),
	  accuracy(parent, groupIds::accuracy, { 0.0, 1.0} )
	{
		fadeTime.setTextConverter("Time");
		accuracy.setTextConverter("NormalizedPercentage");

		fadeTime.setTooltip("The fade time between sustain and release sample");
		accuracy.setTooltip("If gain matching is enabled, this will control how much the release sample is attenuated based on the current level of the sustain sample");

		fadeTime.setSkewFactorFromMidPoint(100.0);

		addAndMakeVisible(releaseStartOptions);
		addAndMakeVisible(fadeTime);
		addAndMakeVisible(accuracy);
		releaseStartOptions.setTooltip("Enable gain matching between sustain and release group");

		releaseStartOptions.setToggleModeWithColourChange(true);
		releaseStartOptions.getToggleStateValue().referTo(parent.data.getPropertyAsValue(groupIds::matchGain, parent.getUndoManager(), true));
		releaseStartOptions.setToggleStateAndUpdateIcon((bool)releaseStartOptions.getToggleStateValue().getValue(), true);

		uint8 layerValue = 1;

		for(auto t: tokens)
			addAndMakeVisible(buttons.add(new KeyswitchButton(parent.data, layerValue++, t, parent.data[groupIds::purgable])));
	}

	Path createPath(const String& url) const override
	{
		Path p;
		LOAD_EPATH_IF_URL("release", SampleToolbarIcons::releaseStartOptions);
		return p;
	}

	int getHeightToUse() const override
	{
		return jmax(140, height + 20);
	}

	int height = 10;

	void resized() override
	{
		SimpleFlexbox fb;

		fb.padding = 10;
		fb.justification = Justification::centred;

		for(auto b: buttons)
			b->setSize(b->getWidthToUse(), LayerComponent::TopBarHeight);

		auto buttonList = getAllButtonsToShow();

		auto list = fb.createBoundsListFromComponents(buttonList);

		auto b = getLocalBounds().removeFromTop(130).reduced(5);

		auto lb = b.removeFromRight(60);

		releaseStartOptions.setBounds(lb.removeFromTop(40).reduced(10));
		accuracy.setBounds(lb.removeFromTop(35));
		lb.removeFromTop(5);
		fadeTime.setBounds(lb.removeFromTop(35));
		
		if(auto newHeight = fb.apply(list, b))
		{
			height = newHeight;

			fb.applyBoundsListToComponents(buttonList, list);
			
			if(getHeight() != getHeightToUse())
			{
				findParentComponentOfClass<Content>()->updateSize();
			}
		}
		
		BodyBase::resized();
	}

	HiseShapeButton releaseStartOptions;
	LogicTypeComponent::BodyKnob fadeTime;
	LogicTypeComponent::BodyKnob accuracy;

};

struct ComplexGroupManagerComponent::CustomBody: public LogicTypeComponent::BodyBase,
												 public PathFactory
{
	struct CustomButton: public KeyswitchButton,
						 public PooledUIUpdater::SimpleTimer
	{
		CustomButton(PooledUIUpdater* updater, const ValueTree& ld, uint8 gi, const String& n, bool addPower):
		  KeyswitchButton(ld, gi, n, addPower),
		  SimpleTimer(updater, false),
		  realLayerIndex(ld.getParent().indexOf(ld)),
		  data(ld)
		{}

		void setUseGain(bool shouldUseGain)
		{
			if(shouldUseGain != useGain)
			{
				useGain = shouldUseGain;

				if(useGain)
					start();
				else
					stop();

				repaint();
			}
		}

		void timerCallback() override { repaint(); }

		bool useGain = false;
		ValueTree data;
		uint8 realLayerIndex;

		int getWidthToUse() const override
		{
			return KeyswitchButton::getWidthToUse() + (useGain ? getHeight() : 0);
		}

		Rectangle<float> getTextBounds() const override
		{
			auto tb = KeyswitchButton::getTextBounds();
			if(useGain)
				tb.removeFromRight(getHeight());
			return tb;
		}

		void paint(Graphics& g) override
		{
			KeyswitchButton::paint(g);

			if(data[groupIds::gainMod])
			{
				auto b = getLocalBounds().removeFromRight(getHeight()).reduced(7).toFloat();

				if (auto gc = findParentComponentOfClass<ComponentWithGroupManagerConnection>())
				{
					auto gain = gc->getComplexGroupManager()->getGroupVolume(realLayerIndex, layerIndex);

					g.setColour(getButtonColour(true));
					g.drawEllipse(b, 2.0f);
					auto w = jlimit(3.0f, b.getWidth() - 3.0f, b.getWidth() * gain);
					g.fillEllipse(b.withSizeKeepingCentre(w, w));
				}
			}
		}
	};

	CustomBody(LogicTypeComponent& parent):
	  BodyBase(parent),
	  fadeTime(parent, groupIds::fadeTime, { 0.0, 1000.0, 1.0 }),
	  groupButton("group", nullptr, *this),
	  gainButton("gain", nullptr, *this),
	  dbgButton("dbg", nullptr, *this),
	  debugPanel(*this)
	{
		auto updater = parent.getSampler()->getMainController()->getGlobalUIUpdater();

		addAndMakeVisible(fadeTime);
		fadeTime.setTextConverter("Time");
		fadeTime.setSkewFactorFromMidPoint(100.0);

		addAndMakeVisible(groupButton);
		addAndMakeVisible(gainButton);
		addAndMakeVisible(dbgButton);
		addChildComponent(debugPanel);

		groupButton.setToggleModeWithColourChange(true);
		gainButton.setToggleModeWithColourChange(true);
		dbgButton.setToggleModeWithColourChange(true);

		dbgButton.onClick = [this]()
		{
			debugPanel.setVisible(dbgButton.getToggleState());

			if(auto c = findParentComponentOfClass<Content>())
				c->updateSize();
		};

		groupButton.setTooltip("Enable callbacks when voices are started");
		gainButton.setTooltip("Enable gain modulation for this layer");
		dbgButton.setTooltip("Show debugging info");

		groupButton.onClick = [this]()
		{
			this->data.setProperty(groupIds::groupStart, groupButton.getToggleState(), getUndoManager());
		};

		gainButton.onClick = [this]()
		{
			this->data.setProperty(groupIds::gainMod, gainButton.getToggleState(), getUndoManager());
		};

		uint8 layerValue = 1;

		for (auto t : tokens)
			addAndMakeVisible(buttons.add(new CustomButton(updater, parent.data, layerValue++, t, parent.data[groupIds::purgable])));

		flagButtonListener.setCallback(parent.data, { groupIds::gainMod, groupIds::groupStart }, valuetree::AsyncMode::Asynchronously, VT_BIND_PROPERTY_LISTENER(onFlagProperty));
	}

	void onFlagProperty(const Identifier& id, const var& newValue)
	{
		if(id == groupIds::gainMod)
		{
			gainButton.setToggleStateAndUpdateIcon((bool)newValue);
			fadeTime.setEnabled((bool)newValue);

			for (auto b : buttons)
			{
				if (auto t = dynamic_cast<CustomButton*>(b))
					t->setUseGain((bool)newValue);
			}

			resized();
		}
		if(id == groupIds::groupStart)
			groupButton.setToggleStateAndUpdateIcon((bool)newValue);
	}

	valuetree::PropertyListener flagButtonListener;

	Path createPath(const String& url) const override
	{
		Path p;

		LOAD_EPATH_IF_URL("gain", HiBinaryData::ProcessorIcons::gainModulation);
		LOAD_EPATH_IF_URL("group", HiBinaryData::SpecialSymbols::midiData);
		LOAD_EPATH_IF_URL("dbg", BackendBinaryData::ToolbarIcons::debugPanel);

		return p;
	}

	struct DebugPanel : public Component,
						public Timer
	{
		enum ColumnType
		{
			GroupVolume,
			CurrentPeak,
			EventIdList,
			StartProperties
		};

		struct ColumnInfo
		{
			ColumnType type;
			String name;
			float scale;
			bool visible;
		};

		std::vector<ColumnInfo> columns;

		void forEachVisibleColumn(Rectangle<float> rowArea, const std::function<void(Rectangle<float>, const ColumnInfo&)>& f)
		{
			float sum = 0.0f;
			float numVisible = 0.0f;

			for(const auto& c: columns)
			{
				if(c.visible)
				{
					sum += c.scale;
					numVisible += 1.0f;
				}
			}

			float fullWidth = rowArea.getWidth();

			for(const auto& c: columns)
			{
				if(c.visible)
				{
					auto w = fullWidth * c.scale / sum;
					auto cell = rowArea.removeFromLeft(w);
					f(cell, c);
				}
			}
		}

		void timerCallback() override
		{
			for(auto r: rows)
				r->repaint();
		}

		struct Row: public Component
		{
			static constexpr int RowHeight = 70;
			static constexpr int LayerLabelWidth = 80;
			
			Row(DebugPanel& parent_, uint8 groupIndex_):
			  parent(parent_),
			  groupIndex(groupIndex_)
			{
				auto v = parent.parent.data;
				layerIndex = v.getParent().indexOf(v);
			}

			struct StaleValue
			{
				StaleValue(const var& v):
				  lastValue(v),
				  staleCounter(0)
				{};

				StaleValue() = default;

				bool isValid() 
				{
					return !lastValue.isUndefined() && ++staleCounter < 30;
				}

				var lastValue;
				int staleCounter = 0;
			};

			std::map<ColumnType, float> lastGains;
			
			std::map<ColumnType, StaleValue> staleValues;

			void drawGainValue(bool stale, ColumnType c, Graphics& g, Rectangle<float> area, float gv, String topLabel={}, String bottomLabel={})
			{
				float gainToUse;


				if(stale)
					gainToUse = gv;
				else
				{
					gainToUse = gv; 
					lastGains[c] = gainToUse;
				}

				auto staleAlpha = stale ? 0.5f : 1.0f;

				g.setColour(Colours::white.withAlpha(0.3f * staleAlpha));

				if(!topLabel.isEmpty())
					g.drawText(topLabel, area, Justification::centredTop);

				if(bottomLabel.isEmpty())
					bottomLabel << String(Decibels::gainToDecibels(gainToUse), 1) << " dB";

				g.drawText(bottomLabel, area, Justification::centredBottom);

				area = area.withSizeKeepingCentre(area.getWidth(), 20.0f);

				g.setColour(Colour(0xFF1D1D1D).withAlpha(staleAlpha));
				
				g.fillRoundedRectangle(area, area.getHeight() * 0.5f);

				auto wScale = hmath::pow(jlimit(0.0f, 1.0f, gainToUse), 0.3f);

				area = area.reduced(3.0f);
				area = area.removeFromLeft(jmax(area.getHeight(), wScale * area.getWidth()));
				g.setColour(Colour(0xFF8E8E8E).withAlpha(staleAlpha));
				g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
			}

			void paint(Graphics& g) override
			{
				g.setFont(GLOBAL_BOLD_FONT());
				g.setColour(Colours::white.withAlpha(0.4f));

				auto b = getLocalBounds().toFloat().reduced(3.0f);
				
				g.drawText(parent.parent.tokens[groupIndex-1], b.removeFromLeft((float)LayerLabelWidth), Justification::right);
				b.removeFromLeft(LogicTypeComponent::BodyMargin);
				g.setColour(Colours::white.withAlpha(0.05f));
				g.fillRoundedRectangle(b, 3.0f);

				parent.forEachVisibleColumn(b, [&](Rectangle<float> cell, ColumnInfo info)
				{
					if(info.type == ColumnType::GroupVolume)
					{
						auto gain = getGroupManager()->getGroupVolume(layerIndex, groupIndex);
						drawGainValue(gain == 1.0f, info.type, g, cell.reduced(3.0f), gain);
					}
					if(info.type == ColumnType::CurrentPeak)
					{
						auto ok = forEachActiveVoiceMatch([&](ModulatorSynthVoice* v)
						{
							auto s = static_cast<ModulatorSamplerSound*>(v->getCurrentlyPlayingSound().get());
							auto watchesPeak = s->getReferenceToSound()->getReleaseStartBuffer() != nullptr;

							if (watchesPeak)
							{
								auto gain = (double)s->getReferenceToSound()->getCurrentReleasePeak();
								String eventId;
								eventId << String(v->getCurrentHiseEvent().getEventId());
								drawGainValue(false, info.type, g, cell.reduced(3.0f), gain, eventId);
								staleValues[info.type] = { var(gain) };
							}
							
							return watchesPeak;
						});

						if(ok)
							return;

						if(staleValues[info.type].isValid())
						{
							auto v = (float)staleValues[info.type].lastValue;
							drawGainValue(true, info.type, g, cell.reduced(3.0f), v);
							return;
						}

						g.setColour(Colours::white.withAlpha(0.2f));
						g.drawText("n.a.", cell, Justification::centred);
					}
					if(info.type == ColumnType::EventIdList)
					{
						forEachActiveVoiceMatch([&](ModulatorSynthVoice* v)
						{
							auto e = v->getCurrentHiseEvent();
							g.setColour(Colours::white.withAlpha(0.6f));
							g.setFont(GLOBAL_MONOSPACE_FONT());
							g.drawText(e.toDebugString(), cell.removeFromTop(20.0f), Justification::left);
							return false;
						});
					}
					if(info.type == ColumnType::StartProperties)
					{
						auto gm = getGroupManager();

						auto ok = forEachActiveVoiceMatch([&](ModulatorSynthVoice* v)
						{
							auto e = v->getCurrentHiseEvent();
							auto sound = static_cast<ModulatorSynthSound*>(v->getCurrentlyPlayingSound().get());
							
							if(auto ss = gm->getSpecialSoundStart(e, sound))
							{
								if(ss.fadeInTimeSeconds != 0.0)
								{
									String text;

									text << "Fade in " << String(roundToInt(ss.fadeInTimeSeconds * 0.001)) << "ms";
									drawGainValue(false, info.type, g, cell.reduced(3.0f), ss.targetVolume, text);
									staleValues[info.type] = { var(ss.targetVolume) };

									return true;
								}
								
							}

							return false;
						});

						if(ok)
							return;

						if (staleValues[info.type].isValid())
						{
							auto v = (float)staleValues[info.type].lastValue;
							drawGainValue(true, info.type, g, cell.reduced(3.0f), v);
							return;
						}

						g.setColour(Colours::white.withAlpha(0.2f));
						g.drawText("n.a.", cell, Justification::centred);
					}
				});

			}

			bool forEachActiveVoiceMatch(const std::function<bool(ModulatorSynthVoice*)>& f)
			{
				auto gm = getGroupManager();

				std::vector<ModulatorSynthVoice*> activeVoices;

				for (auto av : getSampler()->activeVoices)
				{
					if (av != nullptr)
						activeVoices.push_back(av);
				}

				for (auto av : activeVoices)
				{
					auto sustainSample = static_cast<ModulatorSamplerSound*>(av->getCurrentlyPlayingSound().get());

					if(sustainSample != nullptr)
					{
						auto bm = sustainSample->getBitmask();
						auto lv = gm->getLayerValue(bm, layerIndex);

						if (lv == groupIndex)
						{
							if (f(av))
								return true;
						}
					}
				}

				return false;
			}

			ModulatorSampler* getSampler()
			{
				if (auto c = findParentComponentOfClass<ComponentWithGroupManagerConnection>())
					return c->getSampler();

				jassertfalse;
				return nullptr;
			}

			ComplexGroupManager* getGroupManager()
			{
				if(auto c = findParentComponentOfClass<ComponentWithGroupManagerConnection>())
					return c->getComplexGroupManager();

				jassertfalse;
				return nullptr;
			}

			uint8 layerIndex;
			uint8 groupIndex;
			DebugPanel& parent;

			JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Row);
		};

		OwnedArray<Row> rows;

		DebugPanel(CustomBody& parent_):
		  parent(parent_)
		{
			columns.push_back({ ColumnType::GroupVolume, "Group Volume",     1.0f, true });
			columns.push_back({ ColumnType::CurrentPeak, "Current Peak",     1.0f, true });
			columns.push_back({ ColumnType::EventIdList, "Event Ids",        1.5f, true });
			columns.push_back({ ColumnType::StartProperties, "Start Properties", 1.0f, true });

			for(int i = 0; i < parent.tokens.size(); i++)
			{
				rows.add(new Row(*this, i+1));
				addAndMakeVisible(rows.getLast());
			}

			startTimer(30);
		};

		int getHeightToUse() const 
		{
			if(isVisible())
			{
				return LogicTypeComponent::TopBarHeight + rows.size() * Row::RowHeight;
			}

			return 0;
		}

		void paint(Graphics& g) override
		{
			auto b = getLocalBounds().removeFromTop(LogicTypeComponent::TopBarHeight).reduced(3, 0);
			b.removeFromLeft(Row::LayerLabelWidth + LogicTypeComponent::BodyMargin);

			g.setFont(GLOBAL_BOLD_FONT());
			
			forEachVisibleColumn(b.toFloat(), [&](Rectangle<float> cell, const ColumnInfo& info)
			{
				g.setColour(Colours::white.withAlpha(0.05f));
				g.fillRoundedRectangle(cell.reduced(1.0f), 3.0f);
				g.setColour(Colours::white.withAlpha(0.7f));
				g.drawText(info.name, cell, Justification::centred);
			});
		}

		void resized() override
		{
			auto b = getLocalBounds();
			b.removeFromTop(LogicTypeComponent::TopBarHeight);

			for(auto r: rows)
				r->setBounds(b.removeFromTop(Row::RowHeight));
		}

		CustomBody& parent;

	} debugPanel;

	void resized() override
	{
		auto b = getLocalBounds().reduced(LayerComponent::BodyMargin);

		for (auto b : buttons)
			b->setSize(b->getWidthToUse(), LayerComponent::TopBarHeight);

		SimpleFlexbox fb;
		fb.justification = Justification::centred;

		auto buttonList = getAllButtonsToShow();

		auto rb = b.removeFromRight(100);
		auto tb = rb.removeFromTop(24);

		fadeTime.setBounds(rb.removeFromTop(35));

		gainButton.setBounds(tb.removeFromRight(24).reduced(3));
		groupButton.setBounds(tb.removeFromRight(24).reduced(3));
		dbgButton.setBounds(tb.removeFromRight(24).reduced(3));

		auto list = fb.createBoundsListFromComponents(buttonList);

		if (auto h = fb.apply(list, b.removeFromTop(100 - 2 * LayerComponent::BodyMargin)))
		{
			height = h;

			fb.applyBoundsListToComponents(buttonList, list);

			if (getHeight() != getHeightToUse())
			{
				findParentComponentOfClass<Content>()->updateSize();
			}
		}

		if(debugPanel.isVisible())
		{
			b = getLocalBounds().reduced(LogicTypeComponent::BodyMargin);
			debugPanel.setBounds(b.removeFromBottom(debugPanel.getHeightToUse()));
		}
		
		BodyBase::resized();
	}

	int height = 10;

	

	int getHeightToUse() const override
	{
		return jmax(100, height) + (debugPanel.isVisible() ? (debugPanel.getHeightToUse() + LogicTypeComponent::BodyMargin) : 0);
	}

	LogicTypeComponent::BodyKnob fadeTime;
	HiseShapeButton gainButton;
	HiseShapeButton groupButton;
	HiseShapeButton dbgButton;
};

}

