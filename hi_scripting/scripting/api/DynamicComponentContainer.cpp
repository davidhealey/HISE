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
namespace dyncomp
{

namespace dcid
{
const Array<Identifier>& Helpers::getProperties()
{
	static const Array<Identifier> all({
		id,
		type,
		text,
		enabled,
		visible,
		tooltip,
		class_,
		defaultValue,
		useUndoManager,
		elementStyle,
		parentComponent,
		x,
		y,
		width,
		height,
		isMomentary,
		radioGroupId,
		setValueOnClick,
		useCustomPopup,
		items,
		editable,
		multiline,
		updateEachKey,
		min,
		max,
		middlePosition,
		stepSize,
		mode,
		suffix,
		style,
		showValuePopup,
		processorId,
		parameterId,
		filmstripImage,
		numStrips,
		isVertical,
		scaleFactor,
		animationSpeed,
		dragMargin,
		bgColour,
		itemColour,
		itemColour2,
		textColour
	});

	return all;
}

var Helpers::getDefaultValue(const Identifier& p)
{
	jassert(isValidProperty(p));

	static NamedValueSet defaultValues;

	defaultValues.set(id, var(""));
	defaultValues.set(type, var(""));
	defaultValues.set(text, var(""));
	defaultValues.set(enabled, var(true));
	defaultValues.set(visible, var(true));
	defaultValues.set(tooltip, var(""));
	defaultValues.set(class_, var(""));
	defaultValues.set(defaultValue, var(0.0));
	defaultValues.set(useUndoManager, var(false));
	defaultValues.set(elementStyle, var(""));
	defaultValues.set(parentComponent, var(""));
	defaultValues.set(x, var(0));
	defaultValues.set(y, var(0));
	defaultValues.set(width, var(128));
	defaultValues.set(height, var(50));
	defaultValues.set(isMomentary, var(false));
	defaultValues.set(radioGroupId, var(0));
	defaultValues.set(setValueOnClick, var(false));
	defaultValues.set(useCustomPopup, var(false));
	defaultValues.set(items, var(""));
	defaultValues.set(editable, var(true));
	defaultValues.set(multiline, var(false));
	defaultValues.set(updateEachKey, var(false));
	defaultValues.set(min, var(0.0));
	defaultValues.set(max, var(1.0));
	defaultValues.set(middlePosition, var(-10));
	defaultValues.set(stepSize, var(0.01));
	defaultValues.set(mode, var(""));
	defaultValues.set(suffix, var(""));
	defaultValues.set(style, var("Knob"));
	defaultValues.set(showValuePopup, var(false));
	defaultValues.set(processorId, var(""));
	defaultValues.set(parameterId, var(""));
	defaultValues.set(filmstripImage, var(""));
	defaultValues.set(numStrips, var(64));
	defaultValues.set(isVertical, var(true));
	defaultValues.set(scaleFactor, var(1.0));
	defaultValues.set(animationSpeed, var());
	defaultValues.set(dragMargin, var());
	defaultValues.set(bgColour, Colours::black.withAlpha(0.5f).getARGB());
	defaultValues.set(itemColour, Colours::white.withAlpha(0.2f).getARGB());
	defaultValues.set(itemColour2, Colours::white.withAlpha(0.2f).getARGB());
	defaultValues.set(textColour, Colours::white.withAlpha(0.8f).getARGB());

	return defaultValues[p];
}
}

struct Factory
{
	Factory();

	Base::Ptr create(Data::Ptr d, const ValueTree& v) const
	{
		Identifier id(v[dcid::type].toString());

		for(const auto& i: items)
		{
			if(i.first == id)
				return i.second(d, v);
		}

		jassertfalse;
		return nullptr;
	}

private:

	using CreateFunction = std::function<Base*(Data::Ptr, const ValueTree& v)>;

	Array<std::pair<Identifier, CreateFunction>> items;

	template <typename T> void registerItem()
	{
		items.add({T::getStaticId(), T::createComponent});
	}
};

template <typename ComponentType> struct WrapperBase: public Base
{
	WrapperBase(Data::Ptr d, const ValueTree& v):
	  Base(d, v),
	  component(v[dcid::id.toString()])
	{
		addAndMakeVisible(component);

		

		if constexpr(std::is_base_of<SettableTooltipClient, ComponentType>())
		{
			auto tooltip = this->dataTree[dcid::tooltip].toString();
			component.setTooltip(tooltip);
		}
	};

	virtual ~WrapperBase() {};

	void resized() override
	{
		component.setBounds(getLocalBounds());
	}

	void initSpecialProperties(const Array<Identifier>& ids)
	{
		specialProperties.setCallback(this->dataTree, ids, valuetree::AsyncMode::Asynchronously, BIND_MEMBER_FUNCTION_2(WrapperBase::updateSpecialProperties));
	}

protected:

	virtual void updateSpecialProperties(const Identifier& id, const var& newValue) = 0;

	bool useUndoManager() const
	{
		return (bool)dataTree[dcid::useUndoManager];
	}

	ComponentType component;

	valuetree::PropertyListener specialProperties;
};

struct Button: public WrapperBase<juce::ToggleButton>
{
	SN_NODE_ID("Button");

	Button(Data::Ptr d, const ValueTree& v):
	  WrapperBase<juce::ToggleButton>(d, v)
	{
		this->component.onClick = [&]()
		{
			data->onValueChange(getId(), this->component.getToggleState(), useUndoManager());

			auto rid = this->component.getRadioGroupId();

			if(rid != 0)
			{
				if(auto r = findParentComponentOfClass<Root>())
				{
					Component::callRecursive<Button>(r, [&](Button* b)
					{
						if(this == b)
							return false;

						if(b->component.getRadioGroupId() == rid)
							b->data->onValueChange(b->getId(), false, b->useUndoManager());

						return false;
					});
				}
			}
		};

		component.setClickingTogglesState(!(bool)this->dataTree[dcid::isMomentary]);
		
		component.setTriggeredOnMouseDown((bool)this->dataTree[dcid::setValueOnClick]);
		
		initSpecialProperties({ dcid::text, dcid::radioGroupId });
		initCSSForChildComponent();
	};

	void onValue(const var& newValue) override
	{
		

		this->component.setToggleState((bool)newValue, dontSendNotification);
	}

	void updateSpecialProperties(const Identifier& id, const var& newValue) override
	{
		if(id == dcid::text)
		{
			this->component.setButtonText(newValue.toString());
		}
		if(id == dcid::radioGroupId)
		{
			this->component.setRadioGroupId((int)newValue, dontSendNotification);
		}
	}

	static Base* createComponent(Data::Ptr d, const ValueTree& v) { return new Button(d, v); }
};

struct ComboBox: public WrapperBase<hise::SubmenuComboBox>
{
	SN_NODE_ID("ComboBox");

	ComboBox(Data::Ptr d, const ValueTree& v):
	  WrapperBase<juce::SubmenuComboBox>(d, v)
	{
		component.setUseCustomPopup((bool)this->dataTree[dcid::useCustomPopup]);

		this->component.onChange = [&]()
		{
			data->onValueChange(getId(), this->component.getSelectedId(), useUndoManager());
		};

		initSpecialProperties({ dcid::items});

		// Call it once synchronously so that the init value works
		updateSpecialProperties(dcid::items, this->dataTree[dcid::items]);
		initCSSForChildComponent();
	}

	static Base* createComponent(Data::Ptr d, const ValueTree& v) { return new ComboBox(d, v); }

	void onValue(const var& newValue) override
	{
		this->component.setSelectedId((int)newValue, dontSendNotification);
	}

	void updateSpecialProperties(const Identifier& id, const var& newValue) override
	{
		if(id == dcid::items)
		{
			auto currentId = this->component.getSelectedId();
			auto items = StringArray::fromLines(newValue.toString());
			items.removeEmptyStrings();

			this->component.clear(dontSendNotification);
			this->component.addItemList(items, 1);
			this->component.setSelectedId(currentId, dontSendNotification);
			this->component.rebuildPopupMenu();
		}
	}
};



struct Slider: public Base,
			   public ControlledObject,
			   public juce::Slider::Listener	
{
	SN_NODE_ID("Slider");

	struct UnconnectedSlider: public hise::SliderWithShiftTextBox,
							  juce::Slider
	{
		UnconnectedSlider(const String& name):
		  juce::Slider(name)
		{}

		void mouseDoubleClick(const MouseEvent& e) override
		{
			performModifierAction(e, true);
		}

		void mouseDown(const MouseEvent& e) override
		{
			if(performModifierAction(e, false))
				return;

			Slider::mouseDown(e);
		}
	};

	Slider(Data::Ptr d, const ValueTree& v):
	  Base(d, v),
	  ControlledObject(d->getMainController())
	{
		connectionListener.setCallback(v, getSliderIds(), valuetree::AsyncMode::Asynchronously, BIND_MEMBER_FUNCTION_2(Slider::updateSliderProperty));
		updateSliderProperty(dcid::processorId, dataTree[dcid::processorId]);
	}

	void sliderValueChanged(juce::Slider* slider) override
	{
		this->data->onValueChange (getId(), this->slider->getValue(), false);
	}

	static Array<Identifier> getSliderIds()
	{
		static const Array<Identifier> sliderIds({
		 dcid::min, 
		 dcid::max, 
		 dcid::middlePosition, 
		 dcid::stepSize, 
		 dcid::mode, 
		 dcid::suffix, 
		 dcid::style, 
		 dcid::showValuePopup,
         dcid::processorId,
		 dcid::parameterId
		});

		return sliderIds;
	}

	void lookAndFeelChanged() override
	{
		if(slider != nullptr)
			slider->setLookAndFeel(&getLookAndFeel());
	}

	std::pair<Processor*, int> getConnectedParameter()
	{
		auto pid = dataTree[dcid::processorId].toString();

		auto p = ProcessorHelpers::getFirstProcessorWithName(getMainController()->getMainSynthChain(), pid);

		if(p != nullptr)
		{
			auto parameter = dataTree[dcid::parameterId];
			int pIndex = -1;

			if(parameter.isString())
				pIndex = p->getParameterIndexForIdentifier(Identifier(parameter));
			else
				pIndex = (int)parameter;

			if(pIndex != -1)
				return { p, pIndex };
		}

		return { nullptr, -1 };
	}

	void setSlider(juce::Slider* s)
	{
		simple_css::FlexboxComponent::Helpers::writeClassSelectors(*s, { simple_css::Selector(".scriptslider") }, true);
		s->setDoubleClickReturnValue(true, (double)dataTree[dcid::defaultValue]);
		s->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

		s->addListener(this);

		GlobalHiseLookAndFeel::setDefaultColours(*s);

		slider = s;
		addAndMakeVisible(s);

		for(auto id: getSliderIds())
		{
			if(id == dcid::parameterId || id == dcid::processorId)
				continue;

			updateSliderProperty(id, getPropertyOrDefault(id));
		}

		initCSSForChildComponent();
		resized();
	}

	void onValue(const var& newValue) override
	{
		if(slider != nullptr)
		{
			auto isConnected = dynamic_cast<HiSlider*>(slider.get()) != nullptr;
			slider->setValue((double)newValue, isConnected ? sendNotificationSync : dontSendNotification);
		}
	}

	static bool isFilmStripId(const Identifier& id)
	{
		return id == dcid::filmstripImage || id == dcid::numStrips || id == dcid::isVertical || id == dcid::scaleFactor;
	}

	FilmstripLookAndFeel* createFilmstripLookAndFeel() const
	{
		auto ref = dataTree[dcid::filmstripImage].toString();

		if(ref.isNotEmpty())
		{
			Image img = data->getImage(ref);

			auto isVertical = (bool)dataTree.getProperty(dcid::isVertical, true);
			auto scaleFactor = (double)dataTree.getProperty(dcid::scaleFactor, 1.0);
			auto numStrips = (int)dataTree.getProperty(dcid::numStrips, 0);

			if(numStrips > 0 && img.isValid())
			{
				auto fs = new FilmstripLookAndFeel();
				fs->setFilmstripImage(img, numStrips, isVertical);
				fs->setScaleFactor(scaleFactor);

				return fs;
			}
		}

		return nullptr;
	}


	void updateSliderProperty(const Identifier& id, const var& newValue)
	{
		if(id == dcid::parameterId || id == dcid::processorId)
		{
			auto connection = getConnectedParameter();
			auto name = dataTree[dcid::text].toString();

			if(name.isEmpty())
				name = dataTree[dcid::id].toString();

			if(connection.first != nullptr)
			{
				auto s = new HiSlider(dataTree[dcid::id].toString());
				s->setup(connection.first, connection.second, name);
				setSlider(s);
				auto laf = &getLookAndFeel();
				s->setLookAndFeel(laf);
			}
			else
			{
				auto s = new UnconnectedSlider(name);
				setSlider(s);
			}
		}
		if(id == dcid::suffix)
		{
			this->slider->setTextValueSuffix(newValue.toString());
		}
		else if (id == dcid::style)
		{
			StringArray values({"Knob", "Horizontal", "Vertical"});

			std::array<juce::Slider::SliderStyle, 3> styles = { juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag,
			  juce::Slider::SliderStyle::LinearBar,
			  juce::Slider::SliderStyle::LinearHorizontal
			};

			auto idx = values.indexOf(newValue.toString());

			if(idx != -1)
				this->slider->setSliderStyle(styles[idx]);
		}
		else if (isFilmStripId(id))
		{
			if(auto laf = createFilmstripLookAndFeel())
			{
				fslaf = laf;
				slider->setLookAndFeel(laf);
			}
		}
		else if(id == dcid::mode)
		{
			auto vtc = hise::ValueToTextConverter::createForMode(newValue.toString());

			if(vtc.active)
			{
				this->slider->textFromValueFunction = vtc;
				this->slider->valueFromTextFunction = vtc;
			}
			else
			{
				this->slider->textFromValueFunction = {};
				this->slider->valueFromTextFunction = {};
			}
		}
		else if (id == dcid::showValuePopup)
		{
			
		}
		else
		{
			auto rng = scriptnode::RangeHelpers::getDoubleRange(this->dataTree, RangeHelpers::IdSet::ScriptComponents);
			this->slider->setRange(rng.rng.getRange(), rng.rng.interval);
			this->slider->setSkewFactor(rng.rng.skew);
		}
	}

	void resized() override
	{
		if(slider != nullptr)
			slider->setBounds(getLocalBounds());
	}

	static Base* createComponent(Data::Ptr d, const ValueTree& v) { return new Slider(d, v); }

private:

	valuetree::PropertyListener connectionListener;
	ScopedPointer<juce::Slider> slider;
	ScopedPointer<FilmstripLookAndFeel> fslaf;
};


struct Label: public WrapperBase<hise::MultilineLabel>,
			  public Timer,
	          public juce::Label::Listener,	
			  public juce::TextEditor::Listener	
{
	SN_NODE_ID("Label");

	Label(Data::Ptr d, const ValueTree& v):
	  WrapperBase<MultilineLabel>(d, v)
	{
		component.addListener(this);

		if((bool)dataTree[dcid::updateEachKey])
		{
			component.onTextChange = [this]()
			{
				this->startTimer(500);
			};
		}

		initSpecialProperties({ dcid::text, dcid::multiline, dcid::editable});

		initCSSForChildComponent();
	}

	void labelTextChanged (juce::Label* labelThatHasChanged) override
	{
		if(!component.isBeingEdited())
			this->data->onValueChange(getId(), component.getText(), useUndoManager());
	}

	void editorShown(juce::Label*, TextEditor& te) override
	{
		if(auto r = simple_css::CSSRootComponent::find(*this))
		{
			if(auto ss = r->css.getForComponent(&te))
			{
				ss->setupComponent(r, &te, 0);
			}
		}

		te.addListener(this);
	}

	void editorHidden(juce::Label*, TextEditor& te) override
	{
		te.removeListener(this);
	}

	String currentText;

	void textEditorTextChanged(TextEditor& te) override
	{
		if((bool)dataTree[dcid::updateEachKey])
		{
			currentText = te.getText();
			startTimer(500);
		}
	}

	void timerCallback() override
	{
		this->data->onValueChange(getId(), currentText, useUndoManager());
		stopTimer();
	}

	void onValue(const var& newValue) override
	{
		if(!component.isBeingEdited())
			component.setText(newValue.toString(), dontSendNotification);
	}

	static Base* createComponent(Data::Ptr d, const ValueTree& v) { return new Label(d, v); }

	void updateSpecialProperties(const Identifier& id, const var& newValue) override
	{
		if(id == dcid::text)
		{
			component.setText(newValue.toString(), dontSendNotification);
		}
		if(id == dcid::editable)
		{
			component.setEditable((bool)newValue);
		}
		if(id == dcid::multiline)
		{
			component.setMultiline((bool)newValue);
		}
	}
};	

struct FloatingTile: public Base
{
	SN_NODE_ID("FloatingTile");

	FloatingTile(Data::Ptr d, const ValueTree& v):
	  Base(d, v)
	{
		onValue(d->getFloatingTileData(v[dcid::id].toString()));
	}

	void lookAndFeelChanged() override
	{
		if(ft != nullptr)
		{
			auto laf = &getLookAndFeel();

			Component::callRecursive<Component>(ft, [laf](Component* c)
			{
				c->setLookAndFeel(laf);
				return false;
			});
		}
	}

	void onValue(const var& newValue) override
	{
		if(newValue.getDynamicObject() != nullptr)
		{
			addAndMakeVisible(ft = new hise::FloatingTile(data->getMainController(), nullptr, newValue));
			simple_css::FlexboxComponent::Helpers::setIsOpaqueWrapper(*this, true);

			auto idSelector = String("#") + getId().toString();
			ft->getProperties().set(dcid::id, idSelector);
			ft->setLookAndFeel(&getLookAndFeel());
			resized();
		}
		else
		{
			ft = nullptr;
		}
	}

	static Base* createComponent(Data::Ptr d, const ValueTree& v)
	{
		return new FloatingTile(d, v);
	}

	void resized() override
	{
		if(ft != nullptr)
			ft->setBounds(getLocalBounds());
	}

	ScopedPointer<hise::FloatingTile> ft;
};

struct TextBox: public WrapperBase<SimpleMarkdownDisplay>
{
	SN_NODE_ID("TextBox");

	TextBox(Data::Ptr d, const ValueTree& v):
	  WrapperBase<hise::SimpleMarkdownDisplay>(d, v)
	{
		simple_css::FlexboxComponent::Helpers::setCustomType(component, simple_css::Selector(simple_css::ElementType::Paragraph));
		simple_css::FlexboxComponent::Helpers::setFallbackStyleSheet(component, "width: 100%; height: auto;");
		initSpecialProperties({dcid::text});
		component.setResizeToFit(true);
		simple_css::FlexboxComponent::Helpers::storeDefaultBounds(*this, {});

		initCSSForChildComponent();
	}

	void updateSpecialProperties(const Identifier& id, const var& newValue) override
	{
		if(id == dcid::text)
		{
			if(getLocalBounds().isEmpty())
			{
				waitForNotEmpty = true;
			}
			else
			{
				waitForNotEmpty = false;

				component.setText(newValue.toString());

				if(component.totalHeight > 0.0)
					dataTree.setProperty(dcid::height, component.totalHeight, nullptr);

				
			}
			
		}
	}

	void onValue(const var& newValue) override
	{
		
	}

	void resized() override
	{
		if(waitForNotEmpty && !getLocalBounds().isEmpty())
		{
			if(auto r = simple_css::CSSRootComponent::find(*this))
			{
				auto sd = r->css.getMarkdownStyleData(&component);

				ss = r->css.getForComponent(&component);
				component.r.setStyleData(sd);
			}

			component.setText(dataTree[dcid::text].toString());

			if(component.totalHeight > 0.0)
			{
				waitForNotEmpty = true;
				dataTree.setProperty(dcid::height, component.totalHeight, nullptr);
				updatePosition({},{});
			}
		}

		if(ss != nullptr)
		{
			auto area = getLocalBounds().toFloat();

			auto ma = ss->getArea(area, { "margin", 0});
			auto pa = ss->getArea(ma, { "padding", 0});

			

			this->component.setBounds(pa.toNearestInt());

			auto h = roundToInt(component.totalHeight + (area.getHeight() - pa.getHeight()));
			dataTree.setProperty(dcid::height, h, nullptr);
		}
		else
		{
			WrapperBase::resized();
		}
	}

	static Base* createComponent(Data::Ptr d, const ValueTree& v) { return new TextBox(d, v); }

	simple_css::StyleSheet::Ptr ss;

	bool waitForNotEmpty = false;
};

struct DragContainer: public Base
{
	SN_NODE_ID("DragContainer");

	DragContainer(Data::Ptr d, const ValueTree& v):
	  Base(d, v)
	{
		auto idSelector = String("#") + getId().toString();
		getProperties().set(dcid::id, idSelector);

		isVertical = v.getProperty(dcid::isVertical, true);
		dragMargin = v.getProperty(dcid::dragMargin, 3);
		animationSpeed = v.getProperty(dcid::animationSpeed, 150);
	}

	static Base* createComponent(Data::Ptr d, const ValueTree& v) { return new DragContainer(d, v); }

	void rearrangeChildren(const Array<var>& sortedIndexes, bool force)
	{
		bool same = sortedIndexes.size() == currentIndexes.size();

		for(int i = 0; i < currentIndexes.size(); i++)
			same &= currentIndexes[i] == sortedIndexes[i];

		if(!same || force)
		{
			currentIndexes = sortedIndexes;
			rebuildPositions(force);
		}
	}

	void rebuildPositions(bool writePositionsInValueTree)
	{
		auto b = getLocalBounds().reduced(dragMargin);

		for(auto& idx: currentIndexes)
		{
			Rectangle<int> tb;

			if(auto c = getChildComponent(idx))
			{
				if(isVertical)
					tb = b.removeFromTop(c->getHeight());
				else
					tb = b.removeFromLeft(c->getWidth());

				if(c == currentlyDraggedComponent)
					continue;

				if(writePositionsInValueTree)
					dynamic_cast<Base*>(c)->writePositionInValueTree(tb, false);

				if(animationSpeed == 0)
					c->setBounds (tb);
				else
                    Desktop::getInstance().getAnimator().animateComponent(c, tb, 1.0, 200, false, 1.0, 0.3);
			}
		}
	}

	void resized() override
	{
		rebuildPositions(false);
	}

	void onValue(const var& newValue)
	{
		if(auto a = newValue.getArray())
		{
			Array<var> copy;
			copy.addArray(*a);

			if(copy.size() == getNumChildComponents())
			{
				rearrangeChildren(copy, true);
			}
		}
	}

	Component* currentlyDraggedComponent = nullptr;

	void snap()
	{
		rebuildIndexArrayFromPosition(false);
	}

	struct Dragger: public MouseListener,
				    public ComponentBoundsConstrainer
	{
		Dragger(DragContainer& parent_, Component* c):
		  child(dynamic_cast<Base*>(c)),
		  parent(parent_) 
		{
			child->addMouseListener(this, false);
		};

		~Dragger()
		{
			if(child != nullptr)
				child->removeMouseListener(this);
		}

		void mouseDown(const MouseEvent& e) override
		{
			parent.currentlyDraggedComponent = child.get();
			child->toFront(false);
			d.startDraggingComponent(child, e);
			child->repaint();
		}

		void mouseDrag(const MouseEvent& e) override
		{
			d.dragComponent(child, e, this);
			parent.snap();
		}

		void mouseUp(const MouseEvent& e) override
		{
			parent.currentlyDraggedComponent = nullptr;
			parent.rebuildIndexArrayFromPosition(true);
			parent.data->onValueChange(parent.getId(), var(parent.currentIndexes), (bool)parent.dataTree[dcid::useUndoManager]);

			child->repaint();
		}

		void checkBounds (Rectangle<int>& bounds, const Rectangle<int>& previousBounds, const Rectangle<int>& limits, bool ,
                               bool, bool, bool) override
		{
			auto x = parent.isVertical ? parent.dragMargin : 0;
			auto y = parent.isVertical ? 0 : parent.dragMargin;

			auto parentArea = parent.getLocalBounds().reduced(x, y);
			bounds = bounds.constrainedWithin(parentArea);
		}

		DragContainer& parent;
		Base::WeakPtr child;
		ComponentDragger d;
	};

	void paint(Graphics& g) override
	{
		auto slaf = dynamic_cast<simple_css::StyleSheetLookAndFeel*>(&getLookAndFeel());
		slaf->drawComponentBackground(g, this);
	}

	void updateChild(const ValueTree& v, bool wasAdded) override
	{
		Base::updateChild(v, wasAdded);

		if(wasAdded)
		{
			auto c = getChildComponent(getNumChildComponents()-1);
			auto nd = new Dragger(*this, c);
			draggers.add(nd);
		}
		else
		{
			for(auto d: draggers)
			{
				if(*d->child == v)
				{
					draggers.removeObject(d);
				}
			}
		}

		rebuildIndexArrayFromPosition(true);
	}

	void rebuildIndexArrayFromPosition(bool force)
	{
		Array<Component*> list;
		for(int i = 0; i<  getNumChildComponents(); i++)
			list.add(getChildComponent(i));

		struct Sorter
		{
			Sorter(bool isVertical_, int maxValue_):
			  isVertical(isVertical_),
			  maxValue(maxValue_) 
			{};

			int compareElements(Component* c1, Component* c2) const
			{
				int pos1, pos2, b1, b2, l1, l2;

				if(isVertical)
				{
					pos1 = c1->getY();
					pos2 = c2->getY();
					b1 = c1->getBottom();
					b2 = c2->getBottom();
					l1 = c1->getHeight();
					l2 = c2->getHeight();
				}
				else
				{
					pos1 = c1->getX();
					pos2 = c2->getX();
					b1 = c1->getRight();
					b2 = c2->getRight();
					l1 = c1->getWidth();
					l2 = c2->getWidth();
				}

				auto h1 = pos1 + l1/2;
				auto h2 = pos2 + l2/2;

				if(l1 == l2)
				{
				    if(pos1 < pos2)
				        return -1;
			        if(pos1 > pos2)
				        return 1;
				}
				if(l1 < l2)
				{
				    if(h1 < h2)
						return -1;
                    if(h1 > h2)
						return 1;
				}
				if(l1 > l2)
				{
					if(pos1 == 0 && pos2 != 0)
						return -1;
					if(pos2 == 0 && pos1 != 0)
						return 1;

					if(b1 == maxValue && b2 != maxValue)
						return 1;
					if(b1 == maxValue && b2 != maxValue)
						return -1;

				    if(h1 < h2)
				        return -1;
			        if(h1 > h2)
				        return 1;
				}

				return 0;
			}

			const bool isVertical;
			const int maxValue;
		};

		Sorter s(isVertical, isVertical ? getHeight() : getWidth());

		Array<Component*> sorted;
		sorted.addArray(list);
		sorted.sort(s);

		Array<var> newIndexes;

		for(auto& s: sorted)
		{
			newIndexes.add(list.indexOf(s));
		}
		
		rearrangeChildren(newIndexes, force);
	}

	bool isVertical = true;
	double animationSpeed = 150.0;
	int dragMargin = 3;
	Array<var> currentIndexes;

	OwnedArray<Dragger> draggers;
};

struct Panel: public Base
{
	SN_NODE_ID("Panel");

	Panel(Data::Ptr d, const ValueTree& v):
	  Base(d, v),
	  bp(getDrawHandler())
	{
		addAndMakeVisible(bp);

		bp.isUsingCustomImage = getDrawHandler() != &fallback;

		auto idSelector = String("#") + getId().toString();
		simple_css::FlexboxComponent::Helpers::setCustomType(bp, simple_css::Selector(simple_css::ElementType::Panel));
		bp.getProperties().set(dcid::id, idSelector);
		initCSSForChildComponent();
	}

	DrawActions::Handler* getDrawHandler()
	{
		auto dh = data->getDrawHandler(dataTree);
		return dh != nullptr ? dh : &fallback;
	}

	void onValue(const var& newValue) override
	{
		
	}

	void paint(Graphics& g) override
	{
#if 0
		if(auto slaf = dynamic_cast<simple_css::StyleSheetLookAndFeel*>(&getLookAndFeel()))
		{
			slaf->drawComponentBackground(g, this);

			auto text = dataTree[dcid::text].toString();

			if(text.isNotEmpty())
			{
				slaf->drawGenericComponentText(g, text, this);
			}
		}
#endif
	}

	void resized() override
	{
		bp.setBounds(getLocalBounds());
	}

	static Base* createComponent(Data::Ptr d, const ValueTree& v)
	{
		return new Panel(d, v);
	}

	DrawActions::Handler fallback;
	BorderPanel bp;
};

struct FlexBox: public Base,
				public AsyncUpdater
{
	SN_NODE_ID("Viewport");

	FlexBox(Data::Ptr d, const ValueTree& v):
	  Base(d, v),
	  content(simple_css::Selector("#" + v[dcid::id].toString()))
	{
		simple_css::FlexboxComponent::Helpers::writeSelectorsToProperties(content, {".scriptviewport"});
		addAndMakeVisible(content);
	}

	void onValue(const var& newValue) override {}

	void handleAsyncUpdate() override
	{
		content.rebuildLayout();
	}

	void baseChildChanged(Base::Ptr c, bool wasAdded) override
	{
		if(wasAdded)
			content.addFlexItem(*c);
		else
			content.removeFlexItem(*c);
		
		triggerAsyncUpdate();
	}

	void hideChild(Base::Ptr b, bool shouldBeVisible)
	{
		content.content.setFlexChildVisibility(b.get(), shouldBeVisible, !shouldBeVisible);
		triggerAsyncUpdate();
	}

	void resizeChild(Base::Ptr b, Rectangle<int> newBounds) override
	{
		simple_css::FlexboxComponent::Helpers::storeDefaultBounds(*b, newBounds);
		triggerAsyncUpdate();
	}

	void resized() override
	{
		if(!initialised)
		{
			if(auto root = simple_css::CSSRootComponent::find(*this))
			{
				content.setCSS(root->css);
				initialised = true;
			}
		}

		if(content.getLocalBounds() != getLocalBounds())
			content.setBounds(getLocalBounds());
		else
			content.resized();
	}

	static Base* createComponent(Data::Ptr d, const ValueTree& v)
	{
		return new FlexBox(d, v);
	}

	bool initialised = false;

	simple_css::FlexboxViewport content;
};


Data::RefreshType Data::getRefreshType(const var& t)
{
	if(t.isString())
	{
		auto refreshMode_ = t.toString();

		if (refreshMode_ == "repaint")
			return RefreshType::repaint;
		else if (refreshMode_ == "changed")
			return RefreshType::changed;
		else if (refreshMode_ == "updateValueFromProcessorConnection")
			return RefreshType::updateValueFromProcessorConnection;
		else if (refreshMode_ == "loseFocus")
			return RefreshType::loseFocus;
		else if (refreshMode_ == "resetValueToDefault")
			return RefreshType::resetValueToDefault;
		else
		{
			jassertfalse;
			return RefreshType::numRefreshTypes;
		}
			
	}
	else
		return (RefreshType)(int)t;
}

Data::Data(MainController* mc, const var& obj, Rectangle<int> position):
	ControlledObject(mc),
	data(fromJSON(obj, position)),
	values(ValueTree("Values")),
	factory(new Factory()),
	floatingTileData(obj[dcid::FloatingTileData].getDynamicObject()),
	um(mc->getControlUndoManager())
{
	refreshBroadcaster.enableLockFreeUpdate(mc->getGlobalUIUpdater());
	refreshBroadcaster.setEnableQueue(true);
}

void Data::setValues(const var& valueObject)
{
	if(auto obj = valueObject.getDynamicObject())
	{
		for(auto& nv: obj->getProperties())
		{
			values.setProperty(nv.name, nv.value, um);
		}
	}
}

Image Data::getImage(const String& ref)
{
	if(ip)
	{
		return ip(ref);
	}

	return {};
}

simple_css::StyleSheet::Collection::DataProvider* Data::createDataProvider() const
{
	struct DP: public simple_css::StyleSheet::Collection::DataProvider
	{
		DP() = default;

		Font loadFont(const String& fontName, const String& url) override
		{
			if(fp)
				return fp(fontName, url);

			return Font(fontName, 13.0f, Font::plain);
		}

		String importStyleSheet(const String& url) override
		{
			return {};
		}

		Image loadImage(const String& imageURL) override
		{
			if(ip)
				return ip(imageURL);

			return {};
		}

		ImageProvider ip;
		FontProvider fp;
	};

	auto np = new DP();
	np->ip = ip;
	np->fp = fp;
	return np;
}

ValueTree Data::fromJSON(const var& obj, Rectangle<int> position)
{
	var ft;

	ValueTree v;

	if(obj.hasProperty(dcid::ContentProperties))
	{
		v = ValueTreeConverters::convertDynamicObjectToContentProperties(obj[dcid::ContentProperties]);
		ft = obj[dcid::FloatingTileData];
	}
	else
	{
		v = ValueTreeConverters::convertDynamicObjectToContentProperties(obj);
	}

	v.setProperty(dcid::x, position.getX(), nullptr);
	v.setProperty(dcid::y, position.getY(), nullptr);
	v.setProperty(dcid::width, position.getWidth(), nullptr);
	v.setProperty(dcid::height, position.getHeight(), nullptr);

	std::map<String, String> typesToConvert = {
		{ "ScriptButton", "Button" },
		{ "ScriptSlider", "Slider" },
		{ "ScriptComboBox", "ComboBox" },
		{ "ScriptLabel", "Label" },
		{ "ScriptPanel", "Panel" },
		{ "ScriptViewport", "Viewport"}
	};

	valuetree::Helpers::forEach(v, [&](ValueTree& c)
	{
		auto type = c[dcid::type].toString();

		if(typesToConvert.find(type) != typesToConvert.end())
			c.setProperty(dcid::type, typesToConvert[type], nullptr);

		if(!c.hasProperty(dcid::width))
			c.setProperty(dcid::width, 128, nullptr);
		if(!c.hasProperty(dcid::height))
			c.setProperty(dcid::height, 50, nullptr);

		auto pid = c.getParent()[dcid::id];
		c.setProperty(dcid::parentComponent, pid, nullptr);
			
		return false;
	});

	return v;
}

ReferenceCountedObjectPtr<Base> Data::create(const ValueTree& v)
{
	auto id = v[dcid::id].toString();

	if(id.isNotEmpty() && v.hasProperty(dcid::defaultValue) && !values.hasProperty(Identifier(id)))
		values.setProperty(Identifier(id), v[dcid::defaultValue], nullptr);
	
	return factory->create(this, v);
}

void Data::onValueChange(const Identifier& id, const var& newValue, bool useUndoManager)
{
	values.setProperty(id, newValue, useUndoManager ? um : nullptr);
	if(valueCallback)
		valueCallback(id, newValue);
}

const ValueTree& Data::getValueTree(TreeType t) const
{
	return t == TreeType::Data ? data : values;
}

void Data::setValueCallback(const ValueCallback& v)
{
	valueCallback = v;
}

void Data::setDataProviderCallbacks(const ImageProvider& ip_, const FontProvider& fp_)
{
	ip = ip_;
	fp = fp_;
}

Base::Base(Data::Ptr d, const ValueTree& v):
	data(d),
	dataTree(v),
	valueReference(data->getValueTree(Data::TreeType::Values))
{
	auto basicProperties = dcid::Helpers::getBasicProperties();

	basicPropertyListener.setCallback(dataTree, basicProperties, valuetree::AsyncMode::Asynchronously, BIND_MEMBER_FUNCTION_2(Base::updateBasicProperties));

	positionListener.setCallback(dataTree, { dcid::x, dcid::y, dcid::width, dcid::height}, valuetree::AsyncMode::Coallescated, BIND_MEMBER_FUNCTION_2(Base::updatePosition));

	childListener.setCallback(dataTree, valuetree::AsyncMode::Asynchronously, BIND_MEMBER_FUNCTION_2(Base::updateChild));

	d->refreshBroadcaster.addListener(*this, onRefreshStatic, false);

	if(getId().isValid())
	{
		valueListener.setCallback(valueReference, { getId() }, valuetree::AsyncMode::Asynchronously, [&](const Identifier& id, const var& newValue)
		{
			onValue(newValue);
		});
	}
}

Base::~Base()
{
	data->refreshBroadcaster.removeListener(*this);
}

Identifier Base::getId() const
{
	auto s = dataTree[dcid::id].toString();
	return s.isNotEmpty() ? Identifier(s) : Identifier();
}

void Base::updateChild(const ValueTree& v, bool wasAdded)
{
	if(wasAdded)
	{
		auto newChild = data->create(v);
		addChild(newChild);
	}
	else
	{
		for(auto c: children)
		{
			if(c->dataTree == v)
			{
				baseChildChanged(c, false);
				children.removeObject(c);
				break;
			}
		}
	}
}

void Base::updateBasicProperties(const Identifier& id, const var& newValue)
{
    if(id == dcid::class_)
    {
		auto classes = StringArray::fromTokens(newValue.toString(), " ", "");

		auto c = getCSSTarget();

		auto existingClasses = simple_css::FlexboxComponent::Helpers::getClassSelectorFromComponentClass(c);

		for(auto cl: existingClasses)
			classes.add(cl.toString());

		classes.removeDuplicates(false);
		classes.removeEmptyStrings();
		
		simple_css::FlexboxComponent::Helpers::writeSelectorsToProperties(*c, classes);
		simple_css::FlexboxComponent::Helpers::invalidateCache(*c);
    }
	if(id == dcid::elementStyle)
	{
	    simple_css::FlexboxComponent::Helpers::writeInlineStyle (*getCSSTarget(), newValue.toString());
	}
	if(id == dcid::bgColour || id == dcid::itemColour || id == dcid::itemColour2 || id == dcid::textColour)
	{
		if(auto root = simple_css::CSSRootComponent::find(*this))
		{
			if(auto ptr = root->css.getForComponent(this))
			{
				ptr->setPropertyVariable(id, newValue);
				repaint();
			}
		}
	}
	if(id == dcid::enabled)
		setEnabled((bool)newValue);
	if(id == dcid::visible)
	{
		if(auto p = findParentComponentOfClass<Base>())
			p->hideChild(this, (newValue));
		else
			setVisible((bool)newValue);
	}
}

void Base::updatePosition(const Identifier&, const var&)
{
	Rectangle<int> b((int)dataTree[dcid::x], (int)dataTree[dcid::y], (int)dataTree[dcid::width], (int)dataTree[dcid::height]);

	if(auto parent = findParentComponentOfClass<Base>())
	{
		parent->resizeChild(this, b);
	}
}

void Base::baseChildChanged(Base::Ptr c, bool wasAdded)
{
	if(wasAdded)
	{
		c->updatePosition({}, {});
		addAndMakeVisible(c.get());
	}
	else
	{
		removeChildComponent(c.get());
	}
		
}

void Base::resizeChild(Base::Ptr b, Rectangle<int> newBounds)
{
	b->setBounds(newBounds);
	b->resized();
}

void Base::initCSSForChildComponent()
{
	jassert(getNumChildComponents() == 1);

	simple_css::FlexboxComponent::Helpers::setIsOpaqueWrapper(*this, true);

	auto idSelector = String("#") + getId().toString();
	getChildComponent(0)->getProperties().set(dcid::id, idSelector);

	if(dataTree.hasProperty(dcid::class_))
		updateBasicProperties(dcid::class_, dataTree[dcid::class_]);
	if(dataTree.hasProperty(dcid::elementStyle))
		updateBasicProperties(dcid::elementStyle, dataTree[dcid::elementStyle]);
}

void Base::onRefresh(Data::RefreshType rt, bool recursive)
{
	switch(rt)
	{
	case Data::RefreshType::repaint:
		getCSSTarget()->repaint();
		break;
	case Data::RefreshType::changed:
		onValue(getValueOrDefault());
		break;
	case Data::RefreshType::updateValueFromProcessorConnection:
		jassertfalse;
		break;
	case Data::RefreshType::loseFocus:
		unfocusAllComponents();
		return; // no recursion needed
	case Data::RefreshType::resetValueToDefault:
		onValue(dataTree[dcid::defaultValue]);
	default: ;
	}

	if(recursive)
	{
		for(auto c: children)
			c->onRefresh(rt, true);
	}
}

void Base::addChild(Base::Ptr c)
{
	if(c != nullptr)
	{
		children.add(c);
		c->setLookAndFeel(&getLookAndFeel());
		baseChildChanged(c, true);
	}
}

void Base::writePositionInValueTree(Rectangle<int> tb, bool useUndoManager)
{
	dataTree.setProperty(dcid::x, tb.getX(), nullptr);
	dataTree.setProperty(dcid::y, tb.getY(), nullptr);
	dataTree.setProperty(dcid::width, tb.getWidth(), nullptr);
	dataTree.setProperty(dcid::height, tb.getHeight(), nullptr);
}

var Base::getValueOrDefault() const
{
	Identifier id_(dataTree[dcid::id].toString());
	auto vt = data->getValueTree(Data::TreeType::Values);

	if(vt.hasProperty(id_))
		return vt[id_];

	return dataTree[dcid::defaultValue];
}

var Base::getPropertyOrDefault(const Identifier& id) const
{
	if(dataTree.hasProperty(id))
		return dataTree[id];

	return dcid::Helpers::getDefaultValue(id);
}

Factory::Factory()
{
	registerItem<Button>();
	registerItem<Slider>();
	registerItem<ComboBox>();
	registerItem<Panel>();
	registerItem<Label>();
	registerItem<FloatingTile>();
	registerItem<DragContainer>();
	registerItem<FlexBox>();
	registerItem<TextBox>();
}

Root::Root(Data::Ptr d):
  Base(d, d->getValueTree(Data::TreeType::Data))
{
	simple_css::FlexboxComponent::Helpers::setCustomType(*this, simple_css::Selector(simple_css::ElementType::Body));
	simple_css::FlexboxComponent::Helpers::setFallbackStyleSheet(*this, "background-color:transparent;");
}

} // namespace dyncomp
} // namespace hise
