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

namespace hise {
using namespace juce;



struct ComponentWithGroupManagerConnection: public ControlledObject
{
	/** A super simple flexbox implementation:
	 *
	 *  - only left to right
	 *	- Justification: center or left (both vertically and horizontally)
	 *	- optional odd row offset
	 *
	 */
	struct SimpleFlexbox
	{
		int padding = 10;

		int oddRowOffset = 0;

		template <typename ContainerType> void applyBoundsListToComponents(const ContainerType& t, const std::vector<Rectangle<int>> & bounds)
		{
			for(int i = 0; i < t.size(); i++)
				t[i]->setBounds(bounds[i]);
		}

		template <typename ContainerType> std::vector<Rectangle<int>> createBoundsListFromComponents(const ContainerType& t)
		{
			std::vector<Rectangle<int>> list;

			for(auto c: t)
				list.push_back(c->getLocalBounds());

			return list;
		}

		Justification justification = Justification::topLeft;

		int apply(std::vector<Rectangle<int>>& elements, Rectangle<int> availableSpace);
	};

	ComponentWithGroupManagerConnection(ModulatorSampler* s);;

	ModulatorSampler* getSampler() { return sampler.get(); }
	const ModulatorSampler* getSampler() const { return sampler.get(); }

	SampleEditHandler* getSampleEditHandler() { return sampler->getSampleEditHandler(); }
	const SampleEditHandler* getSampleEditHandler() const { return sampler->getSampleEditHandler(); }

	ComplexGroupManager* getComplexGroupManager() { return sampler->getComplexGroupManager(); }
	const ComplexGroupManager* getComplexGroupManager() const { return sampler->getComplexGroupManager(); }

	UndoManager* getUndoManager() { return sampler->getUndoManager(); }

	virtual int getHeightToUse() const = 0;

	void setBoundsWithoutRecursion(Rectangle<int> newBounds)
	{
		if(!recursive)
		{
			ScopedValueSetter<bool> svs(recursive, true);

			auto asComponent = dynamic_cast<Component*>(this);

			if(asComponent->getBoundsInParent() == newBounds)
				asComponent->resized();
			else
				dynamic_cast<Component*>(this)->setBounds(newBounds);
		}
	}

	void resizeWithoutRecursion()
	{
		if(!recursive)
		{
			ScopedValueSetter<bool> svs(recursive, true);
			dynamic_cast<Component*>(this)->resized();
		}
	}

protected:

	void clearRulersAndLabels()
	{
		rulers.clear();
		labels.clear();
	}

	void addRuler(Rectangle<int> r)
	{
		rulers.add(r);
	}

	void addLabel(Rectangle<int> b, const String& label)
	{
		labels.add({ b.toFloat() , label });
	}

	void drawLabelsAndRulers(Graphics& g);

private:

	bool recursive = false;

	Array<std::pair<Rectangle<float>, String>> labels;
	Array<Rectangle<int>> rulers;

	WeakReference<ModulatorSampler> sampler;
};


class ComplexGroupManagerComponent: public Component,
								     public ComponentWithGroupManagerConnection,
									 public SamplerSubEditor,
									 public SampleMap::Listener
{
public:

	using LogicType = ComplexGroupManager::LogicType;
	using Bitmask = SynthSoundWithBitmask::Bitmask;

	struct Factory: public PathFactory
	{
		Path createPath(const String& url) const override;
	};

	int getHeightToUse() const override { return -1; }

	struct Helpers: public ComplexGroupManager::Helpers
	{
		static void drawSelection(Graphics& g, Rectangle<int> bounds);
		static void drawRuler(Graphics& g, Rectangle<int>& bounds);
		static void drawLabel(Graphics& g, Rectangle<float> bounds, const String& text, Justification j = Justification::left);

		static Path getPath(LogicType lt);

		static Colour getLayerColour(const ValueTree& v);
		static void createNewLayers(ValueTree& data, UndoManager* um)
		{
			auto tokens = data[groupIds::tokens].toString();

			if(tokens.isEmpty())
				tokens = "Layer groups";

			auto newTokens = PresetHandler::getCustomName(tokens, "Enter comma separated names to create new layer groups.");

			if(newTokens.isNotEmpty() && newTokens != tokens)
			{
				data.setProperty(groupIds::tokens, newTokens, um);
			}
		}
	};

	struct LayerColourSelector: public Component,
							    public ChangeListener
	{
		LayerColourSelector(ValueTree& v_, Component* toRepaint):
		  v(v_),
		  c(Helpers::getLayerColour(v_)),
		  attachedComponent(toRepaint),
		  selector(ColourSelector::ColourSelectorOptions::editableColour |
				   ColourSelector::ColourSelectorOptions::showColourAtTop |
				   ColourSelector::ColourSelectorOptions::showColourspace)
		{
			addAndMakeVisible(selector);
			selector.setCurrentColour(c);
			selector.addChangeListener(this);
			selector.setLookAndFeel(&laf);
			setSize(400, 400);
		}

		~LayerColourSelector()
		{
			selector.removeChangeListener(this);
		}

		void changeListenerCallback(ChangeBroadcaster* source) override
		{
			auto nc = selector.getCurrentColour();
			v.setProperty(groupIds::colour, nc.getARGB(), nullptr);

			if(auto ac = attachedComponent.getComponent())
				ac->repaint();
		}

		void resized() override
		{
			selector.setBounds(getLocalBounds());
		}

		ValueTree v;
		Colour c;

		Component::SafePointer<Component> attachedComponent;

		GlobalHiseLookAndFeel laf;
		juce::ColourSelector selector;
	};

	struct LayerComponent: public Component,
						   public ComponentWithGroupManagerConnection
	{
		static constexpr int DefaultHeight = 60;
		static constexpr int TopBarHeight = 30;
		static constexpr int BodyMargin = 20;
		static constexpr int PaddingLeft = 70;

		LayerComponent(ComponentWithGroupManagerConnection& parent, const ValueTree& t, LogicType lt);
		~LayerComponent() override;;

		void paint(Graphics& g) override;
		void resized() override;
		void updateHeight(int newHeight);

		LogicType type = LogicType::numLogicTypes;
		Factory f;
		HiseShapeButton clearButton;
		Path p;
		String typeName;
		ValueTree data;
		bool showHeaderText = true;

		Colour c;

		struct KeyboardComponent;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LayerComponent);
	};

	struct SelectorLookAndFeel: public PopupLookAndFeel
	{
		void drawComboBox(Graphics& g, int , int , bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, ComboBox& cb) override;

		void drawComboBoxTextWhenNothingSelected(Graphics& g, ComboBox& cb, Label&) override;

		void drawLabel(Graphics& g, Label& l) override;
	};

	struct LogicTypeComponent: public LayerComponent,
							   public ButtonListener
	{
		struct BodyKnob : public Slider,
						  public SliderWithShiftTextBox,
						  public Slider::Listener
		{
			struct Laf : public GlobalHiseLookAndFeel
			{
				void drawLinearSlider(Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, const Slider::SliderStyle, Slider& s) override;
			};

			BodyKnob(LogicTypeComponent& parent, const Identifier& propertyId, scriptnode::InvertableParameterRange rng);

			void sliderValueChanged(Slider* ) override;
			void mouseDown(const MouseEvent& e) override;
			void onValue(const Identifier&, const var& newValue);
			void setTextConverter(const String& s);

			Laf laf;

			ValueTree v;
			Identifier id;
			UndoManager* um;
			valuetree::PropertyListener valueListener;
		};

		struct BodyBase: public Component,
						 public ComponentWithGroupManagerConnection,
						 public SampleMap::Listener
		{
			static constexpr int ButtonHeight = 24;

			struct ButtonBase: public Component,
							   public SettableTooltipClient
			{
				ButtonBase(const ValueTree& layerData, uint8 layerIndex_);

				virtual ~ButtonBase() {};
				virtual int getWidthToUse() const = 0;

				bool isDisplayed() const;
				
				Colour getButtonColour(bool on) const;
				void mouseDown(const MouseEvent& e) override;
				void setActive(bool shouldBeActive);

				bool active = false;
				bool empty = false;
				const uint8 layerIndex;
			};

			struct UnassignedButton: public ButtonBase
			{
				UnassignedButton(const ValueTree& layerData):
				  ButtonBase(layerData, 0),
				  f(GLOBAL_BOLD_FONT())
				{
					setMouseCursor(MouseCursor::PointingHandCursor);
					p = fa.createPath("unassigned");
					setSize(getWidthToUse(), ButtonHeight);
					setInterceptsMouseClicks(true, true);
					setRepaintsOnMouseActivity(true);
					setTooltip("There are unassigned samples which will not be played back when a note is pressed. Click to open the parser tool");
				}

				int getWidthToUse() const override { return ButtonHeight + f.getStringWidth("Unassigned") + 10; }

				void mouseDown(const MouseEvent& e) override
				{
					PresetHandler::showMessageWindow("Unassigned samples", "Any sample that is not assigned to a group (or has the ignore flag set for this layer) will be skipped when starting a voice at a note on.  \n> Press OK, then assign any unassigned sample to any of the groups in this layer. Note that you can use the green tag selectors to quickly select all samples of a layer.");

					if(auto c = findParentComponentOfClass<ComplexGroupManagerComponent>())
					{
						c->tagButton.setToggleState(true, sendNotificationSync);
						c->tagButton.setToggleStateAndUpdateIcon(true, true);
					}
				}

				void paint(Graphics& g) override
				{
					float alpha = 0.5f;

					if(isMouseOverOrDragging())
						alpha += 0.1f;

					if(isMouseButtonDown())
						alpha += 0.3f;

					g.setColour(Colours::white.withAlpha(alpha));

					g.setFont(f);
					auto tb = getLocalBounds().toFloat();
					g.fillPath(p);
					tb.removeFromLeft(tb.getHeight());
					g.drawText("Unassigned", tb, Justification::centred);
				}

				void resized() override
				{
					fa.scalePath(p, getLocalBounds().removeFromLeft(getHeight()).toFloat().reduced(3.0f));
				}

				Factory fa;
				Path p;
				Font f;
			};

			struct IgnoreButton: public ButtonBase
			{
				IgnoreButton(const ValueTree& layerData):
				  ButtonBase(layerData, ComplexGroupManager::IgnoreFlag)
				{
					icon = f.createPath("ignorable");

					setSize(getWidthToUse(), ButtonHeight);
				}

				int getWidthToUse() const override { return ButtonHeight; }

				void resized() override
				{
					PathFactory::scalePath(icon, this, 3);
				}

				void paint(Graphics& g) override
				{
					g.setColour(Colours::white.withAlpha(0.5f));
					g.fillPath(icon);
				}

				Factory f;
				Path icon;
			};

			struct KeyswitchButton: public ButtonBase
			{
				KeyswitchButton(const ValueTree& layerData, uint8 layerIndex, const String& n, bool addPowerButton=true);

				virtual Rectangle<float> getTextBounds() const
				{
					auto b = getLocalBounds().toFloat();
					if(purgeButton != nullptr)
						b.removeFromLeft(b.getHeight());

					return b;
				}

				int getWidthToUse() const override;

				void resized() override;
				void paint(Graphics& g) override;

				Justification justification = Justification::centred;
				String name;
				uint8 layerValue = 0;
				Factory f;
				ScopedPointer<HiseShapeButton> purgeButton;
			};

			struct SelectionTag: public Component,
								 public ComponentListener,
								 public SettableTooltipClient
			{
				static constexpr int SelectionSize = 17;

				SelectionTag(ButtonBase* attachedButton, uint8 layerIndex_, uint8 layerValue_);

				void mouseDown(const MouseEvent& e) override;

				void mouseUp(const MouseEvent& e) override
				{
					//active = false;
				}

				void refreshActiveState(int numSelected)
				{
					if(!isVisible())
						return;

					active = numSelected > 0 && numSelected == numSamples;

					auto gmc = findParentComponentOfClass<ComponentWithGroupManagerConnection>();

					if(gmc != nullptr && active)
					{
						auto h = gmc->getSampleEditHandler();
						auto gm = gmc->getSampler()->getComplexGroupManager();

						for(auto s: *h)
						{
							auto bm = s->getBitmask();
							auto v = gm->getLayerValue(bm, layerIndex);
							
							if(v != layerValue)
							{
								active = false;
								break;
							}
						}
					}

					repaint();
				}

				void refreshNumSamples();

				void refreshSize();

				~SelectionTag();

				void paint(Graphics& g) override;

				void componentMovedOrResized (Component& component,
				                              bool wasMoved,
				                              bool wasResized) override;

				Component::SafePointer<Component> target;

				int numSamples = 0;

				bool active = false;

				uint8 layerIndex;
				uint8 layerValue;

				JUCE_DECLARE_WEAK_REFERENCEABLE(SelectionTag);
			};

			BodyBase(LogicTypeComponent& parent);

			virtual ~BodyBase() override
			{
				if(getSampler() != nullptr)
					getSampler()->getSampleMap()->removeListener(this);
			}

			void showSelectors(bool shouldShow);

			int getHeightToUse() const override { return 100; }

			/** Overwrite this method and return the center X position for the token. */
			virtual int getCentreXPositionForToken(uint8 tokenIndex) const;

			/** Overwrite this and react to play state changes. The default behaviour just sets the button active. */
			virtual void onPlaystateUpdate(uint8 value);

			void calculateXPositionsForItems(Array<int>& positions) const;

			void resized() override;

			void paint(Graphics& g) override;

			Array<ButtonBase*> getAllButtonsToShow();

			static void onLockUpdate(BodyBase& l, uint8 layerIndex, bool active)
			{
				if(Helpers::getLayerIndex(l.data) == layerIndex)
				{
					l.lockEnabled = active;
					l.refreshLock();
				}
			}

			void refreshLock()
			{
				if(lockEnabled == 0)
					currentLock = {};
				else
				{
					if(isPositiveAndBelow(currentLayerValue-1, buttons.size()))
						currentLock = buttons[currentLayerValue-1]->getBoundsInParent();
				}

				repaint();
			}

			struct Updater: public AsyncUpdater
			{
				Updater(BodyBase& parent_):
				  parent(parent_)
				{}

				void handleAsyncUpdate() override
				{
					parent.refreshButtons(sendNotificationSync);
				}

				BodyBase& parent;
			} updater;

			void refreshButtons(NotificationType n=sendNotificationAsync)
			{
				if(n != sendNotificationSync)
					updater.triggerAsyncUpdate();
				else
				{
					resized();

					auto shouldShow = !selectors.isEmpty();
					selectors.clear();
					showSelectors(shouldShow);
				}
			}

			void sampleMapWasChanged(PoolReference) override { refreshButtons(); }

			void samplePropertyWasChanged(ModulatorSamplerSound*, const Identifier& id, const var&) override
			{
				if(id == SampleIds::RRGroup) refreshButtons();
			};

			void sampleAmountChanged() override { refreshButtons(); }
			void sampleMapCleared() override { refreshButtons(); };

			uint8 currentLayerValue = 0;
			bool lockEnabled = false;
			Rectangle<int> currentLock;

			LogicType lt;
			StringArray tokens;

			ScopedPointer<ButtonBase> ignoreButton;
			ScopedPointer<ButtonBase> unassignedButton;

			OwnedArray<ButtonBase> buttons;
			ValueTree data;

			uint8 currentIndex = 0;

			Rectangle<int> allBounds;

			OwnedArray<SelectionTag> selectors;
			TextButton addGroupButton;

			JUCE_DECLARE_WEAK_REFERENCEABLE(BodyBase);
		};

		

		LogicTypeComponent(ComponentWithGroupManagerConnection& parent, const ValueTree& d);

		void setBody(BodyBase* b);
		int getHeightToUse() const override;

		void refreshHeight();

		void buttonClicked(Button* b) override;
		void paint(Graphics& g) override;
		void resized() override;

		void updateSampleCounters();

		void mouseDown(const MouseEvent& e) override
		{
			if(e.getMouseDownY() < TopBarHeight)
			{
				if (e.mods.isShiftDown())
					rename();
				else
					toggleFold();
			}
		}

		void toggleFold()
		{
			data.setProperty(groupIds::folded, !(bool)data[groupIds::folded], getUndoManager());
		}

		bool folded = false;

		void onFolded(const Identifier&, const var& newValue)
		{
			folded = (bool)newValue;
			
			if(auto pc = findParentComponentOfClass<Content>())
			{
				refreshHeight();
			}
		}

		void mouseDoubleClick(const MouseEvent& e) override
		{
			if(e.getMouseDownY() < TopBarHeight)
				rename();
		}

		void rename()
		{
			addAndMakeVisible(inputLabel = new TextEditor());

			this->showHeaderText = false;
			inputLabel->setBounds(getLocalBounds().removeFromTop(TopBarHeight).reduced(3));

			inputLabel->onReturnKey = [this]()
			{
				data.setProperty(groupIds::id, inputLabel->getText(), getUndoManager());

				MessageManager::callAsync([this]()
				{
					this->showHeaderText = true;
					this->inputLabel = nullptr;
				});
			};

			inputLabel->onFocusLost = [this]()
			{
				MessageManager::callAsync([this]()
				{
					this->showHeaderText = true;
					this->inputLabel = nullptr;
				});
			};

			auto c = Helpers::getLayerColour(data);

			inputLabel->setColour(TextEditor::ColourIds::backgroundColourId, c);
			inputLabel->setColour(TextEditor::ColourIds::textColourId, Colours::white.withAlpha(0.8f));
			inputLabel->setColour(TextEditor::ColourIds::highlightedTextColourId, Colours::black);
			inputLabel->setColour(TextEditor::ColourIds::highlightColourId, Colours::white.withAlpha(0.5f));
			inputLabel->setColour(TextEditor::ColourIds::focusedOutlineColourId, Colours::transparentBlack);
			inputLabel->setColour(CaretComponent::ColourIds::caretColourId, Colours::white);

			inputLabel->setFont(GLOBAL_BOLD_FONT());
			inputLabel->setBorder(BorderSize<int>());
			inputLabel->setJustification(Justification::centred);

			inputLabel->setText(Helpers::getId(data).toString(), dontSendNotification);
			inputLabel->selectAll();
			inputLabel->grabKeyboardFocus();
		}

		ScopedPointer<TextEditor> inputLabel;

		void showSelectors(bool shouldShow);

		static void onPlaystateUpdate(LogicTypeComponent& l, uint8 value);

		StringArray items;
		ScopedPointer<BodyBase> body;
		HiseShapeButton lockButton, midiButton;

		SelectorLookAndFeel laf;

		//Label unassignedLabel, ignoreLabel;
		OwnedArray<BodyBase::SelectionTag> selectors;

		valuetree::PropertyListener foldListener;

		uint8 lastLayerValue = 0;

		JUCE_DECLARE_WEAK_REFERENCEABLE(LogicTypeComponent);
	};

	struct RRBody; struct KeyswitchBody; struct XFadeBody; struct LegatoBody;
	struct ChokeBody; struct TableFadeBody; struct ReleaseBody; struct CustomBody;

	struct AddComponent: public LayerComponent,
						 public ButtonListener
	{
		static constexpr int LabelHeight = 28;
		static constexpr int BottomBarHeight = TopBarHeight + 2 * BodyMargin;

		struct LogicTypeSelector;

		AddComponent(ComponentWithGroupManagerConnection& parent, const ValueTree& t);
		~AddComponent();

		LogicTypeComponent* create(const ValueTree& l);
		void addNewLayer();

		void buttonClicked(Button* b) override;
		void resized() override;
		void paint(Graphics& g) override;

		void rebuildLayers(const ValueTree& v, const Identifier& id);

		void layerAddOrRemoved(const ValueTree& v, bool wasAdded);
		

		int getHeightToUse() const override;

	private:


		void addSpecialComponents(LogicType nt);

		Array<Component::SafePointer<Component>> getSpecialComponents() const;


		int specialHeight = 0;

		static Array<Identifier> getSpecialIds()
		{
			return { SampleIds::LoKey, SampleIds::HiKey, groupIds::isChromatic };
		}

		Component* addEditor(const Identifier& propertyId, const String& helpText, const String& type);;
		ComponentWithGroupManagerConnection* getGroupComponent(const Identifier& id) const;
		Component* getEditor(const Identifier& id) const;
		var getEditorValue(const Identifier& id) const;

		AlertWindowLookAndFeel alaf;
		TextButton okButton;
		
		valuetree::ChildListener layerListener;
		valuetree::RecursivePropertyListener groupRebuildListener;

		OwnedArray<Component> editors;
		OwnedArray<MarkdownHelpButton> helpButtons;

		int flagHeight = TopBarHeight;

		JUCE_DECLARE_WEAK_REFERENCEABLE(AddComponent);
	};

	struct Content: public Component,
					public ComponentWithGroupManagerConnection
	{
		static constexpr int ItemMargin = 5;

		Content(ComponentWithGroupManagerConnection& parent, const ValueTree& d);

		void updateSize();
		void removeLayer(const ValueTree& d);
		void resized() override;

		int getHeightToUse() const override
		{
			return height;
		}

		OwnedArray<LogicTypeComponent> layerComponents;
		AddComponent addComponent;

		ValueTree data;

		int height = 0;

		bool recursion = false;

		JUCE_DECLARE_WEAK_REFERENCEABLE(Content);
	};

	ComplexGroupManagerComponent(ModulatorSampler* s);
	~ComplexGroupManagerComponent() override;

	void paint(Graphics& g) override;
	void resized() override;

	void updateInterface() override
	{

	}

	void soundsSelected(int numSelected) override;

	void sampleMapWasChanged(PoolReference newSampleMap) override { rebuildSampleCounters(); }

	void samplePropertyWasChanged(ModulatorSamplerSound* s, const Identifier& id, const var& newValue)  override;;

	void sampleAmountChanged() override { rebuildSampleCounters(); };

	void sampleMapCleared() override { rebuildSampleCounters(); };

	

	void setDisplayFilter(uint8 layerIndex, uint8 value);

	void onLayerAddRemove(const ValueTree& v, bool added);

private:

	Factory f;

	Array<std::pair<Identifier, uint8>> displayFilters;

	Array<std::pair<uint8, uint8>> activeCountButtons;

	void rebuildSampleCounters();
	void addSelectionFilter(const std::pair<uint8, uint8>& filter, ModifierKeys mods);
	bool isFilterSelected(const std::pair<uint8, uint8>& f) const;
	void rebuildSelection();

	ValueTree layerRoot;
	valuetree::ChildListener layerListener;

	ScrollbarFader sf;
	Viewport viewport;
	Content content;
	ScopedPointer<Logger> logger;

	HiseShapeButton editButton, moreButton, tagButton;
	MarkdownHelpButton helpStateButton;

	JUCE_DECLARE_WEAK_REFERENCEABLE(ComplexGroupManagerComponent);
};

struct ComplexGroupManagerFloatingTile: public SamplerBasePanel
{;
	ComplexGroupManagerFloatingTile(FloatingTile* parent):
	  SamplerBasePanel(parent)
	{};

	SET_PANEL_NAME("ComplexGroupEditor");

	struct ComplexGroupActivator: public Component
	{
		ComplexGroupActivator(ModulatorSampler* s);

		void paint(Graphics& g) override;

		void resized() override;

		ComplexGroupManagerComponent::Factory f;
		HiseShapeButton b;
		WeakReference<ModulatorSampler> sampler;
	};

	Component* createContentComponent(int /*index*/) override;;

	JUCE_DECLARE_WEAK_REFERENCEABLE(ComplexGroupManagerFloatingTile);
	
	WeakReference<ModulatorSampler> lastSampler;
};

}
