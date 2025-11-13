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


/** CHANGE:
 *
 * - remove icon on the left
 * - add folding
 * - move add bar to top
 */

namespace hise {
using namespace juce;

int ComponentWithGroupManagerConnection::SimpleFlexbox::apply(std::vector<Rectangle<int>>& elements,
	Rectangle<int> availableSpace)
{
	if(availableSpace.getWidth() == 0 || elements.empty())
		return 0;

	auto xPos = availableSpace.getX();
	auto yPos = availableSpace.getY();

	auto rowHeight = 0;

	int lastRowStart = 0;
	int idx = 0;

	bool isOddRow = false;

	auto centerPreviousRow = [&]()
	{
		if(idx <= 0)
			return;

		if(justification.getFlags() & (int)Justification::horizontallyCentred)
		{
			auto left = elements[lastRowStart].getX();

			if(isOddRow)
				left -= oddRowOffset;

			auto right = elements[idx - 1].getRight();
			auto rowWidth = right - left;

			auto deltaX = (availableSpace.getWidth() - rowWidth) / 2;

			for(int i = lastRowStart; i < idx; i++)
				elements[i].translate(deltaX, 0);
		}
	};

	for(auto& e: elements)
	{
		auto w = e.getWidth();

		if(xPos + w > availableSpace.getRight())
		{
			xPos = availableSpace.getX() + (isOddRow ? oddRowOffset : 0);

			rowHeight = jmax(rowHeight, e.getHeight());

			yPos += rowHeight + padding;

			centerPreviousRow();

			e.setPosition(xPos, yPos);
			
			isOddRow = !isOddRow;

			rowHeight = 0;
			lastRowStart = idx;
		}
		
		e.setPosition(xPos, yPos);

		rowHeight = jmax(rowHeight, e.getHeight());
		xPos += w + padding;
		idx++;
	}

	centerPreviousRow();

	yPos = elements.back().getBottom();

	auto thisHeight = yPos - availableSpace.getY();

	if(justification.getFlags() & (int)Justification::verticallyCentred)
	{
		auto deltaY = (availableSpace.getHeight() - thisHeight) / 2;

		if(deltaY > 0)
		{
			for(auto& e: elements)
				e.translate(0, deltaY);

			return availableSpace.getHeight();
		}
	}

	return thisHeight;
}

ComponentWithGroupManagerConnection::ComponentWithGroupManagerConnection(ModulatorSampler* s):
	ControlledObject(s->getMainController()),
	sampler(s)
{}




void ComplexGroupManagerComponent::Helpers::drawSelection(Graphics& g, Rectangle<int> bounds)
{
	g.setColour(Colours::white.withAlpha(0.25f));
	Path p;
	p.addRoundedRectangle(bounds.toFloat(), LayerComponent::TopBarHeight / 2.0f);
	Path dp;
	float l[2] = { 4.0f, 4.0f };
	PathStrokeType(1.0f).createDashedStroke(dp, p, l, 2);
	g.fillPath(dp);

	g.setColour(Colours::white.withAlpha(0.05f));

	g.fillRoundedRectangle(bounds.toFloat().reduced(2.0f), LayerComponent::TopBarHeight / 2.0f);
}

void ComplexGroupManagerComponent::Helpers::drawRuler(Graphics& g, Rectangle<int>& bounds)
{
	bounds.removeFromTop(LayerComponent::BodyMargin/2-1);
	g.setColour(Colours::white.withAlpha(0.1f));
	g.fillRect(bounds.removeFromTop(1));
	bounds.removeFromTop(LayerComponent::BodyMargin/2);
}

void ComplexGroupManagerComponent::Helpers::drawLabel(Graphics& g, Rectangle<float> bounds, const String& text, Justification j)
{
	g.setFont(GLOBAL_BOLD_FONT());
	g.setColour(Colours::white.withAlpha(0.5f));
	g.drawText(text, bounds, j);
}

Path ComplexGroupManagerComponent::Helpers::getPath(LogicType lt)
{
	auto n = getLogicTypeName(lt).toLowerCase();
	Factory f;
	return f.createPath(n);
}

Colour ComplexGroupManagerComponent::Helpers::getLayerColour(const ValueTree& v)
{
	if(v.getType() == groupIds::Layers)
		return Colours::transparentBlack;

	if(v.hasProperty(groupIds::colour))
	{
		return ApiHelpers::getColourFromVar(v[groupIds::colour]);
	}

	auto lt = getLogicType(v);
	return MultiOutputDragSource::getFadeColour((int)(lt) - 1, (int)LogicType::numLogicTypes - 1);
}



ComplexGroupManagerComponent::LayerComponent::LayerComponent(ComponentWithGroupManagerConnection& parent, const ValueTree& d_, LogicType lt):
	ComponentWithGroupManagerConnection(parent.getSampler()),
	type(lt),
	clearButton("more", nullptr, f),
	data(d_)
{
	p = Helpers::getPath(type);
	typeName = Helpers::getLogicTypeName(type);
	setSize(0, DefaultHeight + TopBarHeight);

	clearButton.setTooltip("Remove this layer.");

	clearButton.onClick = [this]()
	{
		PopupLookAndFeel plaf;
		PopupMenu m;

		
		m.addItem(1, "Change layer ID");
		m.addItem(2, "Change layer groups");
		m.addItem(8, "Change layer colour");
		m.addSeparator();
		m.addItem(3, "Purgable", true, this->data[groupIds::purgable]);
		m.addItem(4, "Bypassable", true, this->data[groupIds::ignorable]);
		m.addItem(5, "Cacheable", true, this->data[groupIds::cached]);
		m.addSeparator();
		m.addItem(6, "Reset all sample group values");
		m.addItem(7, "Delete layer");

		auto before = data.createCopy();

		auto r = m.show();

		bool rebuild = r == 2 || r == 4;
		
		// deleting / modifying the last layer will not invalidate the bitmasks so this should be fine
		rebuild &= data.getParent().indexOf(data) != (data.getParent().getNumChildren() - 1);
		
		try
		{
			if(r == 1)
			{
				auto id = Helpers::getId(data).toString();
				auto newId = PresetHandler::getCustomName(id, "Enter a new name for this layer.  \n> This ID is used to access the layer through the scripting API so changing this might break the connection!");

				if(newId != id)
					data.setProperty(groupIds::id, newId, getUndoManager());

				repaint();
			}
			if(r == 8)
			{
				auto nc = new LayerColourSelector(data, this);
				auto ft = findParentComponentOfClass<FloatingTile>();
				ft->getRootFloatingTile()->showComponentInRootPopup(nc, this, {getWidth() / 2, 30});
			}
			if(r == 6)
			{
				ModulatorSampler::SoundIterator iter(getSampler());

				Array<Identifier> ids = { Helpers::getId(data) };

				while(auto s = iter.getNextSound())
				{
					getComplexGroupManager()->clearSampleId(s, ids, true);
				}
			}
			if(r == 7)
			{
				findParentComponentOfClass<Content>()->removeLayer(data);
			}
			if(r == 2)
			{
				ComplexGroupManager::ScopedMigrator::Collection c(*getComplexGroupManager(), rebuild);
				Helpers::createNewLayers(this->data, getUndoManager());
			}
			if(r == 3)
			{
				this->data.setProperty(groupIds::purgable, !(bool)data[groupIds::purgable], getUndoManager());
			}
			if(r == 4)
			{
				ComplexGroupManager::ScopedMigrator::Collection c(*getComplexGroupManager(), rebuild);
				this->data.setProperty(groupIds::ignorable, !(bool)data[groupIds::ignorable], getUndoManager());
			}
			if(r == 5)
			{
				this->data.setProperty(groupIds::cached, !(bool)data[groupIds::cached], getUndoManager());
			}
		}
		catch(Result& r)
		{
			PresetHandler::showMessageWindow("Error at changing property", r.getErrorMessage(), PresetHandler::IconType::Error);
			data.copyPropertiesAndChildrenFrom(before, getUndoManager());

		}
	};

	addAndMakeVisible(clearButton);
}

ComplexGroupManagerComponent::LayerComponent::~LayerComponent()
{}

void ComplexGroupManagerComponent::LayerComponent::paint(Graphics& g)
{
	auto b = getLocalBounds().toFloat();

	g.setColour(Colour(0xFF1D1D1D));
	g.fillRoundedRectangle(b.reduced(1.0f), 3.0f);

	g.setColour(Colour(0xFF444444));
	g.drawRoundedRectangle(b.reduced(1.0f), 3.0f, 1.0f);

	auto c = Helpers::getLayerColour(data);

	if(c.isTransparent())
	{
		GlobalHiseLookAndFeel::drawFake3D(g, b.toNearestInt());
	}
	else
	{
		g.setColour(c);
	}

	g.setColour(c.withAlpha(0.3f));
	g.fillPath(p);

	auto topBar = b.removeFromTop(TopBarHeight);
		
	g.fillRect(topBar.reduced(3));

	auto textToShow = typeName;

	if(data.getType() == groupIds::Layer)
	{
		auto id = Helpers::getId(data);

		if(!id.isNull())
			textToShow = id.toString();
	}

	if(textToShow.isEmpty())
		textToShow = getName();

	if(!showHeaderText)
		textToShow = {};

	if(textToShow.isNotEmpty())
	{
		g.setColour(Colours::white);
		g.setFont(GLOBAL_BOLD_FONT());
		g.drawText(textToShow, topBar, Justification::centred);
	}

	drawLabelsAndRulers(g);
}

void ComplexGroupManagerComponent::LayerComponent::resized()
{
	clearRulersAndLabels();

	auto b = getLocalBounds();

	auto top = b.removeFromTop(TopBarHeight);
	clearButton.setBounds(top.removeFromRight(top.getHeight()).reduced(3));
		
}

void ComplexGroupManagerComponent::LayerComponent::updateHeight(int newHeight)
{
	if(getHeight() != newHeight)
	{
		setSize(getWidth(), newHeight);
		findParentComponentOfClass<Content>()->updateSize();
	}
}

ComplexGroupManagerComponent::LogicTypeComponent::LogicTypeComponent(ComponentWithGroupManagerConnection& parent, const ValueTree& d):
	LayerComponent(parent, d, Helpers::getLogicType(d)),
	items(Helpers::getTokens(d, getSampler())),
	lockButton("lock", this, f),
	midiButton("midi", this, f)
{
	addAndMakeVisible(lockButton);
	addAndMakeVisible(midiButton);
	
	lockButton.setTooltip("Lock the current state of this layer regardless of the MIDI input.");
	midiButton.setTooltip("Make the UI display follow the last active state of this layer");

	lockButton.setToggleModeWithColourChange(true);
	midiButton.setToggleModeWithColourChange(true);
	
	typeName << " (" << String(items.size()) << " " << Helpers::getItemName(type, items) << ")";

	addAndMakeVisible(body = new BodyBase(*this));

	setSize(10, getHeightToUse());

	auto layerIndex = Helpers::getLayerIndex(data);
	getComplexGroupManager()->addPlaystateListener(*this, layerIndex, onPlaystateUpdate);

	foldListener.setCallback(data, { groupIds::folded}, valuetree::AsyncMode::Asynchronously, VT_BIND_PROPERTY_LISTENER(onFolded));

	showSelectors(false);
}

void ComplexGroupManagerComponent::LogicTypeComponent::buttonClicked(Button* b)
{
	
	if(b == &lockButton)
	{
		auto idx = Helpers::getLayerIndex(data);
		getComplexGroupManager()->setLockLayer(idx, lockButton.getToggleState(), body->currentIndex);
	}
	
}

ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::ButtonBase::ButtonBase(const ValueTree& v, uint8 layerIndex_):
  layerIndex(layerIndex_)
{
	setRepaintsOnMouseActivity(true);

	if(layerIndex == 0)
		setTooltip("Click to show all samples that are not assigned to this group");
	else if(layerIndex == ComplexGroupManager::IgnoreFlag)
		setTooltip("Click to show all samples that are ignored by this group");
	else
	{
		auto groupName = Helpers::getTokens(v)[layerIndex-1];
		setTooltip("Click to show only samples from the group " + groupName);
	}
		
}

bool ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::ButtonBase::isDisplayed() const
{
	auto base = findParentComponentOfClass<BodyBase>();

	if(isPositiveAndBelow(base->currentIndex - 1, base->buttons.size()))
	{
		return base->buttons[base->currentIndex-1] == this;
	}

	return true;
}

Colour ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::ButtonBase::getButtonColour(bool on) const
{
	auto alpha = isDisplayed() ? 0.8f : 0.3f;

	if(isMouseOver())
		alpha += 0.1f;

	if(isMouseButtonDown())
		alpha += 0.1f;

	auto c = on ? Colour(0xFFAAAAAA) : Colour(0xFF222222);

	return c.withAlpha(alpha);
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::ButtonBase::mouseDown(const MouseEvent& e)
{
	auto body = findParentComponentOfClass<BodyBase>();

	auto idx = body->buttons.indexOf(this) + 1;

	if(body->currentIndex == idx)
		body->currentIndex = 0;
	else
		body->currentIndex = idx;

	body->repaint();

	auto p = findParentComponentOfClass<ComplexGroupManagerComponent>();

	auto layerIndex = Helpers::getLayerIndex(body->data);

	auto lp = findParentComponentOfClass<LogicTypeComponent>();

	if(lp->lockButton.getToggleState())
		lp->getComplexGroupManager()->setLockLayer(layerIndex, true, body->currentIndex);

	p->setDisplayFilter(layerIndex, body->currentIndex);
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::ButtonBase::setActive(bool shouldBeActive)
{
	active = shouldBeActive;
	repaint();
}

ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::KeyswitchButton::KeyswitchButton(const ValueTree& layerData, uint8 layerIndex_, const String& n,
                                                                                             bool addPowerButton):
	ButtonBase(layerData, layerIndex_),
	name(n)
{
	auto idx = Helpers::getTokens(layerData).indexOf(n);

	if(idx != -1)
		layerValue = (uint8)idx+1;

	if(addPowerButton)
	{
		addAndMakeVisible(purgeButton = new HiseShapeButton("purge", nullptr, f));
		purgeButton->setToggleModeWithColourChange(true);
		purgeButton->setTooltip("Click to toggle the purge state of this group");

		purgeButton->onClick = [this, layerData]()
		{
			auto gmc = findParentComponentOfClass<ComponentWithGroupManagerConnection>();

			auto gm = gmc->getComplexGroupManager();

			auto shouldBePurged = !purgeButton->getToggleState();
			auto id = ComplexGroupManagerComponent::Helpers::getId(layerData);
			gm->setPurged(id, name, shouldBePurged);
		};
	}

	setTooltip("Click to toggle the visible sample selection to this layer group");
}

int ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::KeyswitchButton::getWidthToUse() const
{
	auto w = GLOBAL_BOLD_FONT().getStringWidthFloat(name) + LayerComponent::TopBarHeight;

	if(purgeButton != nullptr)
		w += LayerComponent::TopBarHeight / 2;

	return w;
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::KeyswitchButton::resized()
{
	if(purgeButton != nullptr)
	{
		purgeButton->setBounds(getLocalBounds().removeFromLeft(getHeight()).reduced(4));

		if(auto gmc = findParentComponentOfClass<ComponentWithGroupManagerConnection>())
		{
			auto gm = gmc->getComplexGroupManager();

			if(layerValue != 0)
			{
				auto sel = gm->getFilteredSelection(layerIndex, layerValue, true);

				auto purged = false;

				for(const auto& s: sel)
					purged |= s->isPurged();

				purgeButton->setToggleStateAndUpdateIcon(!purged, true);
			}
		}
	}
		
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::KeyswitchButton::paint(Graphics& g)
{
	g.setFont(GLOBAL_BOLD_FONT());

	auto on = getButtonColour(true);
	auto off = getButtonColour(false);

	auto b = getLocalBounds().toFloat().reduced(3.0f);

	g.setColour(active ? on : off);
	g.fillRoundedRectangle(b, b.getHeight() / 2.0f);

	g.setColour((active ? off : on).withAlpha(0.3f));
					
	g.drawRoundedRectangle(b, b.getHeight() / 2.0f, 1.0f);

	g.setColour((active ? off : on).withAlpha(1.0f));

	if(purgeButton != nullptr)
		b.removeFromLeft(LayerComponent::TopBarHeight / 2);

	g.drawText(name, getTextBounds(), Justification::centred);
}

ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::SelectionTag::SelectionTag(ButtonBase* attachedButton,
                                                                                       uint8 layerIndex_, uint8 layerValue_):
	target(attachedButton),
	layerIndex(layerIndex_),
	layerValue(layerValue_)
{
	setMouseCursor(MouseCursor::PointingHandCursor);
	attachedButton->addComponentListener(this);
	refreshNumSamples();
	setRepaintsOnMouseActivity(true);

	auto t = target->findParentComponentOfClass<ComplexGroupManagerComponent>();

	jassert(t != nullptr);

	auto h = t->getSampleEditHandler();
	auto gm = t->getSampler()->getComplexGroupManager();

	h->allSelectionBroadcaster.addListener(*this, [h, gm](SelectionTag& t, int numSelected)
	{
		t.refreshActiveState(numSelected);
	}, true);

	String msg;

	if(layerValue == 0)
	{
		msg << "Select all samples that are not assigned to any group within the " ;
		msg << t->getComplexGroupManager()->getLayerId(layerIndex);
		msg << " layer";
	}
	else if (layerValue == ComplexGroupManager::IgnoreFlag)
	{
		msg << "Select all samples that are ignored by the ";
		msg << t->getComplexGroupManager()->getLayerId(layerIndex);
		msg << " layer";
	}
	else
	{
		msg << "Select all samples that are assigned to ";
		msg << t->getComplexGroupManager()->getLayerValueAsToken(layerIndex, layerValue);
	}

	setTooltip(msg);
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::SelectionTag::mouseDown(const MouseEvent& e)
{
	//active = true;
	findParentComponentOfClass<ComplexGroupManagerComponent>()->addSelectionFilter({layerIndex, layerValue}, e.mods);
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::SelectionTag::refreshNumSamples()
{
	if(auto t = target->findParentComponentOfClass<ComponentWithGroupManagerConnection>())
	{
		if(layerValue == 0 || layerValue == ComplexGroupManager::IgnoreFlag)
		{
			auto s = t->getComplexGroupManager()->getNumUnassignedAndIgnored(layerIndex);

			if(layerValue == 0)
				numSamples = s.first;
			else
				numSamples = s.second;
		}
		else
		{
			numSamples = t->getComplexGroupManager()->getNumFiltered(layerIndex, layerValue, false);
		}

		

		refreshSize();
	}
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::SelectionTag::refreshSize()
{
	auto b = target->getBoundsInParent();
	auto p = b.getTopRight();
	auto f = GLOBAL_FONT();
	auto w = f.getStringWidth(String(numSamples)) + SelectionSize;

	auto nb = Rectangle<int>(p, p).withSizeKeepingCentre(w, SelectionSize);
	setBounds(nb);
	repaint();
}

ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::SelectionTag::~SelectionTag()
{
	if(target != nullptr)
		target->removeComponentListener(this);
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::SelectionTag::paint(Graphics& g)
{
	//auto active = findParentComponentOfClass<ComplexGroupManagerComponent>()->isFilterSelected({layerIndex, layerValue});

	auto b = getLocalBounds().toFloat().reduced(1.0f);

	auto alpha = 0.8f;

	if(isMouseButtonDown() || isMouseOver())
		alpha = 1.0f;

	if(active)
	{
		g.setColour(Colour(SIGNAL_COLOUR));
		g.fillRoundedRectangle(b, b.getHeight() / 2.0f);
		g.setColour(Colours::black.withAlpha(alpha));

	}
	else
	{
		g.setColour(Colours::black.withAlpha(0.7f));	
		g.fillRoundedRectangle(b, b.getHeight() / 2.0f);
		g.setColour(Colour(SIGNAL_COLOUR).withAlpha(alpha));
		g.drawRoundedRectangle(b, b.getHeight() / 2.0f, 1.0f);
	}
	
	g.setFont(GLOBAL_FONT());
	g.drawText(String(numSamples), b, Justification::centred);

}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::SelectionTag::componentMovedOrResized(
	Component& component, bool wasMoved, bool wasResized)
{
	refreshSize();
}

ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::BodyBase(LogicTypeComponent& parent):
	ComponentWithGroupManagerConnection(parent.getSampler()),
	lt(parent.type),
	tokens(parent.items),
	data(parent.data),
	unassignedButton(new UnassignedButton(parent.data)),
	ignoreButton(new IgnoreButton(parent.data)),
	addGroupButton("Add Layer Groups"),
	updater(*this)
{
	parent.getSampler()->getSampleMap()->addListener(this);

	addChildComponent(unassignedButton);
	addChildComponent(ignoreButton);
	addChildComponent(addGroupButton);

	addGroupButton.onClick = [this]()
	{
		Helpers::createNewLayers(this->data, getUndoManager());
	};

	getComplexGroupManager()->lockBroadcaster.addListener(*this, onLockUpdate);
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::showSelectors(bool shouldShow)
{
	auto showSelectors = selectors.size() > 0;



	if(shouldShow != showSelectors)
	{
		showSelectors = shouldShow;

		selectors.clear();

		if(shouldShow)
		{
			auto layerIndex = Helpers::getLayerIndex(data);

			if(unassignedButton->isVisible())
				addAndMakeVisible(selectors.add(new SelectionTag(unassignedButton, layerIndex, 0)));

			uint8 layerValue = 1;

			for(auto b: buttons)
				addAndMakeVisible(selectors.add(new SelectionTag(b, layerIndex, layerValue++)));

			if(ignoreButton->isVisible())
				addAndMakeVisible(selectors.add(new SelectionTag(ignoreButton, layerIndex, ComplexGroupManager::IgnoreFlag)));
		}
	}
	else
	{
		for(auto s: selectors)
			s->refreshNumSamples();
	}
}

int ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::getCentreXPositionForToken(uint8 tokenIndex) const
{
	auto b = getLocalBounds();

	if(tokens.size() == 0)
		return b.getCentreX();

	auto normX = (float)tokenIndex / (float)(tokens.size() - 1);
	return roundToInt(normX * (float)b.getWidth()) + b.getX();
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::onPlaystateUpdate(uint8 value)
{
	currentLayerValue = value;

	for(int i = 0; i < buttons.size(); i++)
		buttons[i]->setActive(i == (value - 1));

	if(auto p = findParentComponentOfClass<LogicTypeComponent>())
	{
		if(p->midiButton.getToggleState())
		{
			auto idx = Helpers::getLayerIndex(p->data);
			p->findParentComponentOfClass<ComplexGroupManagerComponent>()->setDisplayFilter(idx, value);
			currentIndex = value;
			repaint();
		}
	}

	if(lockEnabled)
	{
		refreshLock();
	}
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::calculateXPositionsForItems(
	Array<int>& positions) const
{
	uint8 idx = 0;

	for(auto& t: tokens)
	{
		ignoreUnused(t);
		positions.add(getCentreXPositionForToken(idx++) + PaddingLeft);
	}
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::resized()
{
	RectangleList<int> buttonBounds;

	for(auto b: buttons)
		buttonBounds.add(b->getBoundsInParent());

	allBounds = buttonBounds.getBounds();

	addGroupButton.setVisible(buttons.isEmpty());

	if(addGroupButton.isVisible())
	{
		addGroupButton.setBounds(getLocalBounds().reduced(5).removeFromTop(20));
	}

	refreshLock();
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::paint(Graphics& g)
{
	Rectangle<int> b;

	if(currentIndex == 0)
		b = allBounds;
	else if(isPositiveAndBelow((int)currentIndex-1, buttons.size()))
	{
		b = buttons[(int)currentIndex-1]->getBoundsInParent();
	}
	
	Helpers::drawSelection(g, b.expanded(2));

	if(!currentLock.isEmpty())
	{
		auto pb = currentLock.withSizeKeepingCentre(10, 10).translated(0, LayerComponent::TopBarHeight / 2 + 5).toFloat();

		Factory f;
		auto p = f.createPath("lock");
		f.scalePath(p, pb);
		g.setColour(Colours::white.withAlpha(0.4f));
		g.fillPath(p);
	}

	
}

Array<hise::ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::ButtonBase*> ComplexGroupManagerComponent::LogicTypeComponent::BodyBase::getAllButtonsToShow()
{
	Array<ButtonBase*> list;

	auto idx = Helpers::getLayerIndex(data);

	auto showUnassigned = getComplexGroupManager()->getNumUnassignedAndIgnored(idx).first > 0;

	unassignedButton->setVisible(showUnassigned);

	if (showUnassigned)
		list.add(unassignedButton.get());

	for (auto b : buttons)
		list.add(b);

	auto showIgnorable = (bool)data[groupIds::ignorable];

	ignoreButton->setVisible(showIgnorable);

	if (showIgnorable)
		list.add(ignoreButton.get());

	return list;
}

void ComplexGroupManagerComponent::LogicTypeComponent::setBody(BodyBase* b)
{
	addAndMakeVisible(body = b);
	setSize(10, getHeightToUse());

	body->onPlaystateUpdate(lastLayerValue);

}

void ComplexGroupManagerComponent::SelectorLookAndFeel::drawComboBox(Graphics& g, int i, int i1, bool isButtonDown,
                                                                     int buttonX, int buttonY, int buttonW, int buttonH, ComboBox& cb)
{
	auto b = cb.getLocalBounds();

	if(cb.isEnabled())
	{
		g.setColour(Colours::white.withAlpha(0.05f));
		g.fillRoundedRectangle(b.reduced(2).toFloat(), 3.0f);
	}
			
	Path p;
	g.setColour(Colours::white.withAlpha(0.5f));
	p.addTriangle({0.0f, 0.0f}, {1.0f, 0.0f}, { 0.5f, 1.0f} );
	PathFactory::scalePath(p, b.removeFromRight(b.getHeight()).withSizeKeepingCentre(8, 6).toFloat());

	if(cb.isEnabled())
		g.fillPath(p);
}

void ComplexGroupManagerComponent::SelectorLookAndFeel::drawComboBoxTextWhenNothingSelected(Graphics& g, ComboBox& cb,
	Label& label)
{
	Helpers::drawLabel(g, cb.getLocalBounds().toFloat().reduced(10, 0), cb.getTextWhenNothingSelected());
}

void ComplexGroupManagerComponent::SelectorLookAndFeel::drawLabel(Graphics& g, Label& l)
{
	auto b = l.getLocalBounds();
	Helpers::drawLabel(g, b.toFloat(), l.getText());
}


int ComplexGroupManagerComponent::LogicTypeComponent::getHeightToUse() const
{
	auto h = TopBarHeight;

	if(data[groupIds::folded])
		return h;

	if(body != nullptr)
		h += body->getHeightToUse();

	return h;
}

void ComplexGroupManagerComponent::LogicTypeComponent::refreshHeight()
{
	updateHeight(getHeightToUse());
}

void ComplexGroupManagerComponent::LogicTypeComponent::resized()
{
	LayerComponent::resized();

	auto b = getLocalBounds();

	auto topBar = b.removeFromTop(TopBarHeight);

	clearButton.setBounds(topBar.removeFromRight(topBar.getHeight()).reduced(4));
	lockButton.setBounds(topBar.removeFromRight(topBar.getHeight()).reduced(4));
	midiButton.setBounds(topBar.removeFromRight(topBar.getHeight()).reduced(4));

	b = b.reduced(BodyMargin, 0);

	auto bb = b.removeFromTop(body->getHeightToUse());

	//ignoreLabel.setBounds(leftArea.removeFromBottom(AddComponent::LabelHeight));
	//unassignedLabel.setBounds(leftArea.removeFromTop(AddComponent::LabelHeight));

	PathFactory::scalePath(p, topBar.removeFromLeft(topBar.getHeight()).reduced(6.0f).toFloat());

	body->setBounds(bb);
	body->resized();

	updateSampleCounters();
}

void ComplexGroupManagerComponent::LogicTypeComponent::updateSampleCounters()
{
	callRecursive<BodyBase::SelectionTag>(this, [](BodyBase::SelectionTag* s)
	{
		s->refreshNumSamples();
		return false;
	});
}

void ComplexGroupManagerComponent::LogicTypeComponent::showSelectors(bool shouldShow)
{
	selectors.clear();

	//ignoreLabel.setVisible(shouldShow && data[groupIds::ignorable]);
	//unassignedLabel.setVisible(shouldShow);

	if(shouldShow)
	{
		//if(ignoreLabel.isVisible())
			//selectors.add(new BodyBase::SelectionTag(*this, &ignoreLabel, ComplexGroupManager::IgnoreFlag));

				
		//selectors.add(new BodyBase::SelectionTag(*this, &unassignedLabel, 0));

		
				
		for(auto s: selectors)
			addAndMakeVisible(s);
	}

	body->showSelectors(shouldShow);
}

void ComplexGroupManagerComponent::LogicTypeComponent::onPlaystateUpdate(LogicTypeComponent& l, uint8 value)
{
	l.lastLayerValue = value;
	l.body->onPlaystateUpdate(value);
}

void ComplexGroupManagerComponent::LogicTypeComponent::paint(Graphics& g)
{
	LayerComponent::paint(g);

	g.setColour(Colours::white.withAlpha(0.7f));
	g.fillPath(p);
}



struct ComplexGroupManagerComponent::AddComponent::LogicTypeSelector: public Component,
																      public ComponentWithGroupManagerConnection
{
	static constexpr int IconWidth = 50;
	static constexpr int LogicTypeSelectorHeight = 2 * IconWidth + 2 * BodyMargin + 2 * TopBarHeight;
	
	void paint(Graphics& g) override
	{
		auto pos = getMouseXYRelative().toFloat();

		for(int i = 0; i < areas.size(); i++)
		{
			auto lt = (LogicType)(i + 1);

			auto n = Helpers::getLogicTypeName(lt);
			auto p = Helpers::getPath(lt);

			auto a = areas[i].toFloat().expanded(4, 0);

			a.removeFromBottom(-BodyMargin);

			float alpha = 0.0f;

			auto active = currentType == lt;

			if(a.contains(pos))
			{
				alpha += 0.05f;

				if(isMouseButtonDown())
				{
					alpha += 0.05f;
					a = a.reduced(1.0f);
				}
			}

			if(active)
				alpha += 0.1f;

			g.setColour(Colours::white.withAlpha(alpha));
			g.fillRoundedRectangle(a.reduced(2.0f), 3.0f);
			g.drawRoundedRectangle(a.reduced(2.0f), 3.0f, 1.0f);

			PathFactory::scalePath(p, a.removeFromTop(IconWidth).reduced(15.0f));
			
			auto colour = scriptnode::MultiOutputDragSource::getFadeColour((int)lt, (int)LogicType::numLogicTypes);

			g.setColour(colour.withMultipliedSaturation(1.3f));
			g.fillPath(p);

			Helpers::drawLabel(g, a, n, Justification::centred);
		}
	}

	LogicTypeSelector(ModulatorSampler* s):
	  ComponentWithGroupManagerConnection(s)
	{
		setRepaintsOnMouseActivity(true);
	}

	void mouseMove(const MouseEvent& event) override { repaint(); }

	void mouseDown(const MouseEvent& e) override
	{
		for(int i = 0; i < areas.size(); i++)
		{
			if(areas[i].contains(e.getPosition()))
			{
				auto newType = (LogicType)(i + 1);

				if(currentType != newType)
				{
					currentType = newType;

					if(onTypeSelection)
						onTypeSelection(currentType);

					repaint();
				}

				break;
			}
		}
	}

	int getHeightToUse() const override { return height + 2 * BodyMargin; }

	void resized() override
	{
		SimpleFlexbox fb;

		fb.justification = Justification::centredTop;
		fb.padding = 10;

		areas.clear();

		auto numTypes = (int)(LogicType::numLogicTypes) - 1;

		for(int i = 0; i < numTypes; i++)
			areas.push_back({ 0, 0, IconWidth, IconWidth + 10 });

		if(auto h = fb.apply(areas, getLocalBounds().reduced(BodyMargin, BodyMargin)))
		{
			height = h;

			if(getHeight() != getHeightToUse())
			{
				if(auto c = findParentComponentOfClass<Content>())
					c->updateSize();
			}
				
		}
	}

	

	std::function<void(LogicType)> onTypeSelection;

	LogicType currentType = LogicType::RoundRobin;
	std::vector<Rectangle<int>> areas;
	int height = IconWidth;
};

ComplexGroupManagerComponent::AddComponent::AddComponent(ComponentWithGroupManagerConnection& parent, const ValueTree& d):
	LayerComponent(parent, d, LogicType::numLogicTypes),
	//addButton("add", this, f),
	//moreButton("more", this, f),
	okButton("Add new layer")
{
	setName("Add group layer");

	typeName = {};

	//addAndMakeVisible(addButton);

	//addButton.setToggleModeWithColourChange(true);

	//addAndMakeVisible(moreButton);

	addAndMakeVisible(okButton);
	okButton.setLookAndFeel(&alaf);
	okButton.addListener(this);

	addEditor(groupIds::type, "The layer mode. These layers come with a predefined behaviour and can be combined in order to create a complex group logic.", "LogicTypeSelector");
	
	auto tokenEditor = dynamic_cast<TextEditor*>(addEditor(groupIds::tokens, "A comma separated list of tokens that will be used to determine the amount of groups in this layer.  \n> If you select some samples, you can choose the file name token and it will automatically fill out the tokens based on the sample selection.", "TextEditor"));

	auto idEditor = dynamic_cast<TextEditor*>(addEditor(groupIds::id, "This sets the ID of the layer. This is used for organisational purposes like the table column name as well as addressing them through the scripting API.", "TextEditor"));
	
	idEditor->setTextToShowWhenEmpty("Enter ID", Colours::black.withAlpha(0.3f));
	tokenEditor->setTextToShowWhenEmpty("Token1, Token2, Token3", Colours::black.withAlpha(0.3f));

	addEditor(groupIds::purgable, "Enable purging of different groups of this layer.  \n> This allows you to load/unload all samples of a particular layer group at once.", "HiseShapeButton");
	addEditor(groupIds::ignorable, "Allow the bypass flag to be set for this layer.  \n> If you have any samples that should not be affected by this layer at all (eg. release samples when creating a xfade layer for sustain samples), enable this flag and assign the Bypass state to those samples." , "HiseShapeButton");
	addEditor(groupIds::cached, "Prefilters the groups of this layer for the voice start calculation.  \n> This will create a list of samples that is prefiltered when the voice start logic needs to iterate over all samples to find the ones that should be started.", "HiseShapeButton");
	
	setSize(200, TopBarHeight);

	//addButton.setTooltip("Add a new layer");
	clearButton.setTooltip("Remove all layers");
	//moreButton.setTooltip("Manage layer setup");

	layerListener.setCallback(data, valuetree::AsyncMode::Asynchronously, BIND_MEMBER_FUNCTION_2(AddComponent::layerAddOrRemoved));

	groupRebuildListener.setCallback(data, { groupIds::tokens, groupIds::ignorable, groupIds::purgable }, valuetree::AsyncMode::Asynchronously, BIND_MEMBER_FUNCTION_2(AddComponent::rebuildLayers));

	
}

ComplexGroupManagerComponent::AddComponent::~AddComponent()
{
	
}

ComplexGroupManagerComponent::LogicTypeComponent* ComplexGroupManagerComponent::AddComponent::create(const ValueTree& l)
{
	auto nc = new LogicTypeComponent(*this, l);

	switch(nc->type)
	{
	case LogicType::Undefined: break;
	case LogicType::Custom: 
		nc->setBody(new CustomBody(*nc));
		break;
	case LogicType::RoundRobin: 
		nc->setBody(new RRBody(*nc));
		break;
	case LogicType::Keyswitch:
		nc->setBody(new KeyswitchBody(*nc));
		break;
	case LogicType::TableFade:
		nc->setBody(new TableFadeBody(*nc));
		break;
	case LogicType::XFade:
		nc->setBody(new XFadeBody(*nc));
		break;
	case LogicType::LegatoInterval:
		nc->setBody(new LegatoBody(*nc));
		break;
	case LogicType::ReleaseTrigger:
		nc->setBody(new ReleaseBody(*nc));
		break;
	case LogicType::Choke:
		nc->setBody(new ChokeBody(*nc));
		break;
	case LogicType::numLogicTypes: break;
	default: ;
	}
			
	return nc;
}

void ComplexGroupManagerComponent::AddComponent::addNewLayer()
{
	auto tokens = StringArray::fromTokens(getEditorValue(groupIds::tokens).toString(), ",", "");
	tokens.trim();
	tokens.removeDuplicates(false);
	tokens.removeEmptyStrings(true);

	dynamic_cast<TextEditor*>(getEditor(groupIds::tokens))->setText(tokens.joinIntoString(","), dontSendNotification);

	ValueTree l(groupIds::Layer);

	Array<Identifier> ids = {
		groupIds::id,
		groupIds::type,
		groupIds::tokens,
		groupIds::ignorable,
		groupIds::cached,
		groupIds::purgable
	};

	ids.addArray(getSpecialIds());

	for(auto id: ids)
	{
		if(getEditor(id) != nullptr)
			l.setProperty(id, getEditorValue(id), nullptr);
	}

	if(l[groupIds::id].toString().isEmpty())
	{
		auto lt = Helpers::getLogicType(l);
		l.setProperty(groupIds::id, Helpers::getNextFreeId(lt, data).toString(), nullptr);
	}

	

	try
	{
		tokens = Helpers::getTokens(l, getSampler());
		getUndoManager()->beginNewTransaction();
		data.addChild(l, -1, getUndoManager());
	}
	catch(Result& r)
	{
		PresetHandler::showMessageWindow("Error at creating layer", r.getErrorMessage(), PresetHandler::IconType::Error);
		data.removeChild(l, getUndoManager());
	}
}

void ComplexGroupManagerComponent::AddComponent::buttonClicked(Button* b)
{
#if 0
	if(b == &addButton)
	{
		auto shouldShow = b->getToggleState();
		auto show = getHeight() != TopBarHeight;

		if(shouldShow != show)
			updateHeight(getHeightToUse());
	}

	if(b == &moreButton)
	{
		PopupLookAndFeel plaf;
		PopupMenu m;
		m.setLookAndFeel(&plaf);

		m.addItem(1, "Export as Base64");
		m.addItem(2, "Import from Base64");

		m.addSeparator();

		m.addItem(3, "Export as JSON");
		m.addItem(4, "Import from JSON");

		m.addSeparator();

		m.addItem(5, "Reset all samples");
		m.addItem(6, "Rebuild cache");

		if(auto r = m.show())
		{
			if(r == 1)
			{
				zstd::ZDefaultCompressor comp;
				MemoryBlock mb;
				comp.compress(data, mb);
				auto b64 = mb.toBase64Encoding();
				SystemClipboard::copyTextToClipboard(b64);
			}
			if(r == 2)
			{
				
				MemoryBlock mb;
				auto b64 = SystemClipboard::getTextFromClipboard();
				if(mb.fromBase64Encoding(b64))
				{
					ValueTree v;
					zstd::ZDefaultCompressor comp;
					comp.expand(mb, v);

					ComplexGroupManager::ScopedUpdateDelayer sds(*getComplexGroupManager());

					getUndoManager()->beginNewTransaction();
					data.removeAllChildren(getUndoManager());

					for(auto c: v)
						data.addChild(c.createCopy(), -1, getUndoManager());
				}
			}
		}

		
	}
#endif
	if(b == &okButton)
	{
		if(b == &okButton)
		{
			addNewLayer();
		}
	}
}


void ComplexGroupManagerComponent::AddComponent::paint(Graphics& g)
{
	LayerComponent::paint(g);
}

void ComplexGroupManagerComponent::AddComponent::rebuildLayers(const ValueTree& v, const Identifier& id)
{
	auto p = findParentComponentOfClass<Content>();
	p->layerComponents.clear();

	for(auto c: data)
		layerAddOrRemoved(c, true);
	
}

void ComplexGroupManagerComponent::AddComponent::layerAddOrRemoved(const ValueTree& v, bool wasAdded)
{
	auto c = findParentComponentOfClass<Content>();

	if(c == nullptr)
		return;

	if(wasAdded)
	{
		auto c = findParentComponentOfClass<Content>();
		auto nc = create(v);
		c->layerComponents.add(nc);
		c->addAndMakeVisible(nc);

		dynamic_cast<TextEditor*>(getEditor(groupIds::tokens))->setText("", dontSendNotification);
		dynamic_cast<TextEditor*>(getEditor(groupIds::id))->setText("", dontSendNotification);
		
		//if(addButton.getToggleState())
//			addButton.triggerClick(sendNotificationSync);

		c->updateSize();
	}
	else
	{
		for(auto l: c->layerComponents)
		{
			if(l->data == v)
			{
				c->layerComponents.removeObject(l);
				break;
			}
		}
		
		c->updateSize();
	}

	if(auto parent = findParentComponentOfClass<ComplexGroupManagerComponent>())
		parent->rebuildSampleCounters();
}



void ComplexGroupManagerComponent::AddComponent::resized()
{
	LayerComponent::resized();

	auto b = getLocalBounds();

	auto top = b.removeFromTop(TopBarHeight);

	constexpr int ButtonMargin = 5;

	b.removeFromBottom(BodyMargin);
	b.removeFromLeft(BodyMargin);
	b.removeFromRight(BodyMargin);

	clearRulersAndLabels();

	if(b.isEmpty())
	{
		return;
	}
		

	//moreButton.setBounds(top.removeFromLeft(top.getHeight()).reduced(ButtonMargin));
	//addButton.setBounds(top.withSizeKeepingCentre(top.getHeight(), top.getHeight()).reduced(ButtonMargin));
	clearButton.setVisible(false);
	clearButton.setBounds(top.removeFromRight(top.getHeight()).reduced(ButtonMargin));
	top.removeFromRight(2 * top.getHeight());

	auto showBody = true;

	if(showBody)
	{
		// set the ok padding before the left padding
		okButton.setBounds(b.removeFromBottom(TopBarHeight));
		addRuler(b.removeFromBottom(BodyMargin).reduced(BodyMargin, 0));

		

		addLabel(b.removeFromTop(LabelHeight), "Layer Type:");
		getEditor(groupIds::type)->setBounds(b.removeFromTop(getGroupComponent(groupIds::type)->getHeightToUse()));
		b.removeFromTop(BodyMargin*2);

		auto textEditorArea = b.removeFromTop(LabelHeight + 28);

		auto idArea = textEditorArea.removeFromLeft(150);

		textEditorArea.removeFromLeft(BodyMargin);

		auto flagArea = textEditorArea;

		addLabel(idArea.removeFromTop(LabelHeight), "Layer ID:");

		getEditor(groupIds::id)->setBounds(idArea.removeFromTop(28));

		{
			addLabel(flagArea.removeFromTop(LabelHeight), "Layer flags:");

			std::vector<Rectangle<int>> flagButtons;

			flagButtons.push_back({ 60 + TopBarHeight, TopBarHeight });
			flagButtons.push_back({ 60 + TopBarHeight, TopBarHeight });
			flagButtons.push_back({ 60 + TopBarHeight, TopBarHeight });

			SimpleFlexbox fb;

			if(auto h = fb.apply(flagButtons, flagArea))
			{
				flagHeight = h;

				getEditor(groupIds::purgable)->setBounds(flagButtons[0].removeFromLeft(TopBarHeight).reduced(ButtonMargin));
				addLabel(flagButtons[0], "Purge");
				getEditor(groupIds::ignorable)->setBounds(flagButtons[1].removeFromLeft(TopBarHeight).reduced(ButtonMargin));
				addLabel(flagButtons[1].removeFromLeft(100), "Bypass" );
				getEditor(groupIds::cached)->setBounds(flagButtons[2].removeFromLeft(TopBarHeight).reduced(ButtonMargin));
				addLabel(flagButtons[2].removeFromLeft(100), "Cache" );
			}
		}

		auto delta = flagHeight - 28;

		if(delta > 0)
		{
			b.removeFromTop(delta);
		}

		//auto tokenArea = b.removeFromTop(LabelHeight + 28);
		//addLabel(tokenArea.removeFromTop(LabelHeight), "Layer Groups:");
		getEditor(groupIds::tokens)->setBounds({});
	}

	for(auto e: editors)
		e->setVisible(showBody);

	for(auto b: helpButtons)
		b->setVisible(showBody);

	okButton.setVisible(showBody);
}

int ComplexGroupManagerComponent::AddComponent::getHeightToUse() const
{
	int bodyHeight = TopBarHeight;

	if(getWidth() == 0)
		return 0;

	auto lt = getGroupComponent(groupIds::type);

	lt->setBoundsWithoutRecursion(getLocalBounds().reduced(BodyMargin));

	bodyHeight += BodyMargin;
	bodyHeight += LabelHeight + getGroupComponent(groupIds::type)->getHeightToUse() + BodyMargin;

#if 0
	bodyHeight += LabelHeight + getEditor(groupIds::tokens)->getHeight() + BodyMargin;
#endif

	bodyHeight += LabelHeight + jmax(flagHeight, 28) + BodyMargin;

	if(specialHeight != 0)
		bodyHeight += 2 * BodyMargin + LabelHeight + specialHeight;

	bodyHeight += TopBarHeight;
	bodyHeight += BodyMargin;

	return bodyHeight;
}

void ComplexGroupManagerComponent::AddComponent::addSpecialComponents(LogicType nt)
{
	auto newId = Helpers::getNextFreeId(nt, getComplexGroupManager()->getDataTree()).toString();
	dynamic_cast<TextEditor*>(getEditor(groupIds::id))->setText(newId, dontSendNotification);

	auto defaultCacheValue = ComplexGroupManager::Helpers::getDefaultValue(nt, ComplexGroupManager::Flags::FlagCached);
	auto defaultPurgeValue = ComplexGroupManager::Helpers::getDefaultValue(nt, ComplexGroupManager::Flags::FlagPurgable);
	auto defaultIgnoreValue = ComplexGroupManager::Helpers::getDefaultValue(nt, ComplexGroupManager::Flags::FlagIgnorable);

	if(defaultPurgeValue != -1)
		dynamic_cast<HiseShapeButton*>(getEditor(groupIds::purgable))->setToggleStateAndUpdateIcon((bool)defaultPurgeValue, true);

	if(defaultIgnoreValue != -1)
		dynamic_cast<HiseShapeButton*>(getEditor(groupIds::ignorable))->setToggleStateAndUpdateIcon((bool)defaultIgnoreValue, true);

	if(defaultCacheValue != -1)
		dynamic_cast<HiseShapeButton*>(getEditor(groupIds::cached))->setToggleStateAndUpdateIcon((bool)defaultCacheValue, true);

	specialHeight = 0; // fb.apply(pos, getLocalBounds());

	resized();
}

Array<Component::SafePointer<Component>> ComplexGroupManagerComponent::AddComponent::getSpecialComponents() const
{
	return {};

#if 0
	auto specialIds = getSpecialIds();
	Array<Component::SafePointer<Component>> list;

	for(int i = 0; i < editors.size(); i++)
	{
		if(specialIds.contains(Identifier(editors[i]->getName())))
			list.add(editors[i]);
	}

	return list;
#endif
}

Component* ComplexGroupManagerComponent::AddComponent::addEditor(const Identifier& propertyId, const String& helpText,
                                                                 const String& type)
{
	Component* nc = nullptr;

	auto at = MarkdownHelpButton::AttachmentType::TopRight;

	if(type == "LogicTypeSelector")
	{
		auto t = new LogicTypeSelector(getSampler());
		t->onTypeSelection = BIND_MEMBER_FUNCTION_1(AddComponent::addSpecialComponents); 
		nc = t;
	}
	else if (type == "ComboBox")
	{
		nc = new ComboBox();
		nc->setSize(128, TopBarHeight);
	}
	else if(type == "TextEditor")
	{
		auto te = new TextEditor();
		GlobalHiseLookAndFeel::setTextEditorColours(*te);
		te->setSelectAllWhenFocused(true);
		nc = te;
	}
	else if (type == "HiseShapeButton")
	{
		auto hb = new HiseShapeButton(propertyId.toString(), this, f);
		hb->setToggleModeWithColourChange(true);
		hb->getProperties().set("helpOffsetX", 60);
		at = MarkdownHelpButton::AttachmentType::PropertyHelpOffsetXY;
		nc = hb;
	}
	else
		return nullptr;

	nc->setName(propertyId.toString());
	editors.add(nc);
	addAndMakeVisible(nc);

	if(helpText.isNotEmpty())
	{
		auto md = new MarkdownHelpButton();
		md->setHelpText(helpText);
		md->attachTo(nc, at);
		addAndMakeVisible(helpButtons.add(md));
	}

	return nc;
}

ComponentWithGroupManagerConnection* ComplexGroupManagerComponent::AddComponent::getGroupComponent(
	const Identifier& id) const
{
	return dynamic_cast<ComponentWithGroupManagerConnection*>(getEditor(id));
}

Component* ComplexGroupManagerComponent::AddComponent::getEditor(const Identifier& id) const
{
	for(auto e: editors)
	{
		if(e->getName() == id.toString())
			return e;
	}
	
	jassert(getSpecialIds().contains(id));

	return nullptr;
}

var ComplexGroupManagerComponent::AddComponent::getEditorValue(const Identifier& id) const
{
	if(getSpecialIds().contains(id))
	{
		if(auto cb = dynamic_cast<ComboBox*>(getEditor(id)))
		{
			return cb->getSelectedItemIndex();
		}

		jassertfalse;
		return var();
	}

	if(id == groupIds::type)
	{
		auto lt = dynamic_cast<LogicTypeSelector*>(getEditor(id))->currentType;
		return Helpers::getLogicTypeName(lt);
	}
	if(id == groupIds::purgable || id == groupIds::cached || id == groupIds::ignorable)
	{
		return dynamic_cast<HiseShapeButton*>(getEditor(id))->getToggleState();
	}
	if(id == groupIds::id || id == groupIds::tokens)
	{
		return dynamic_cast<TextEditor*>(getEditor(id))->getText();
	}

	jassertfalse;
	return var();
}

ComplexGroupManagerComponent::Content::Content(ComponentWithGroupManagerConnection& parent, const ValueTree& d) :
	ComponentWithGroupManagerConnection(parent.getSampler()),
	data(d),
	addComponent(parent, d)
{
	addChildComponent(addComponent);
	updateSize();
}

void ComplexGroupManagerComponent::Content::updateSize()
{
	height = 0;

	Array<LogicTypeComponent*> lp;

	for(auto l: layerComponents)
	{
		height += l->getHeightToUse() + ItemMargin;
	}

	if (addComponent.isVisible())
		height += addComponent.getHeightToUse();

	setSize(getWidth(), height);
	resized();

	if(auto p = findParentComponentOfClass<ComplexGroupManagerComponent>())
	{
		if(recursion)
			return;

		p->resized();
		p->repaint();
	}
		
}

void ComplexGroupManagerComponent::Content::removeLayer(const ValueTree& d)
{
	auto layerIndex = d == data ? -1 : data.indexOf(d);

	if(layerIndex == -1 && !PresetHandler::showYesNoWindow("Clear all layers", "Do you want to clear all layers"))
		return ;

	getUndoManager()->beginNewTransaction();

	if(layerIndex == -1)
	{
		ComplexGroupManager::ScopedUpdateDelayer sds(*getComplexGroupManager());
		data.removeAllChildren(getUndoManager());
	}
	else
		data.removeChild(layerIndex, getUndoManager());
}

void ComplexGroupManagerComponent::Content::resized()
{
	auto b = getLocalBounds();

	if(b.isEmpty())
		return;

	for(auto l: layerComponents)
	{
		l->setBounds(b.removeFromTop(l->getHeightToUse()));
		b.removeFromTop(ItemMargin);
	}

	addComponent.setBounds(b.removeFromTop(addComponent.getHeightToUse()));
	addComponent.resized();
}

ComplexGroupManagerComponent::ComplexGroupManagerComponent(ModulatorSampler* s):
	ComponentWithGroupManagerConnection(s),
	SamplerSubEditor(s->getSampleEditHandler()),
	content(*this, getComplexGroupManager()->getDataTree()),
	editButton("edit", nullptr, f),
	moreButton("more", nullptr, f),
	tagButton("count", nullptr, f),
	layerRoot(getComplexGroupManager()->getDataTree())
{
	editButton.onClick = [this]()
	{
		content.addComponent.setVisible(editButton.getToggleState());
		content.updateSize();
		this->repaint();
	};

	moreButton.onClick = [this]()
	{
		PopupLookAndFeel plaf;
		PopupMenu m;
		m.setLookAndFeel(&plaf);

		m.addItem(1, "Export as Base64");
		m.addItem(2, "Import from Base64");

		m.addSeparator();

		m.addItem(3, "Export as JSON");
		m.addItem(4, "Import from JSON");

		m.addSeparator();

		m.addItem(5, "Reset all samples");
		m.addItem(6, "Rebuild cache");
		m.addItem(7, "Remove all layers");
		m.addItem(9, "Store layout in samplemap", true, getSampler()->getSampleMap()->storeComplexLayers);
		m.addItem(8, "Switch back to simple RR system");

		if(auto r = m.show())
		{
			if(r == 1)
			{
				zstd::ZDefaultCompressor comp;
				MemoryBlock mb;
				comp.compress(content.data, mb);
				auto b64 = mb.toBase64Encoding();
				SystemClipboard::copyTextToClipboard(b64);
			}
			if(r == 2)
			{
				
				MemoryBlock mb;
				auto b64 = SystemClipboard::getTextFromClipboard();
				if(mb.fromBase64Encoding(b64))
				{
					ValueTree v;
					zstd::ZDefaultCompressor comp;
					comp.expand(mb, v);

					ComplexGroupManager::ScopedUpdateDelayer sds(*getComplexGroupManager());

					getUndoManager()->beginNewTransaction();
					content.data.removeAllChildren(getUndoManager());

					for(auto c: v)
						content.data.addChild(c.createCopy(), -1, getUndoManager());
				}
			}
			if(r == 5)
			{
				if(PresetHandler::showYesNoWindow("Confirm reset", "Do you want to clear the bitmask for all samples?"))
				{
					ModulatorSampler::SoundIterator iter(getSampler());

					while(auto s = iter.getNextSound())
						s->setBitmask(0);
				}
			}
			if(r == 6)
			{
				getComplexGroupManager()->rebuildGroups();
			}
			if(r == 7)
			{
				if(PresetHandler::showYesNoWindow("Confirm reset", "Do you want to remove all layers?"))
				{
					ComplexGroupManager::ScopedUpdateDelayer sds(*getComplexGroupManager());

					ModulatorSampler::SoundIterator iter(getSampler());

					while(auto s = iter.getNextSound())
						s->setBitmask(0);

					getUndoManager()->beginNewTransaction();
					content.data.removeAllChildren(getUndoManager());
				}
			}
			if(r == 8)
			{
				if(PresetHandler::showYesNoWindow("Deactivate complex group manager", "Press OK to switch back to the default RR system for this sampler."))
				{
					auto s = getSampler();
					s->setUseComplexGroupManager(false);
				}
			}
			if(r == 9)
			{
				auto on = !getSampler()->getSampleMap()->storeComplexLayers;
				getSampler()->getSampleMap()->setStoreComplexLayers(on);
			}
		}
	};

	tagButton.onClick = [this]()
	{
		auto on = tagButton.getToggleState();
		Component::callRecursive<LogicTypeComponent>(&content, [on](LogicTypeComponent* lt)
		{
			lt->showSelectors(on);
			return false;
		});
	};

	
	
	editButton.setTooltip("Show / hide the layer creation dialog.");
	tagButton.setTooltip("Show / hide the number of samples that are assigned / unassigned to each layer group.");
	moreButton.setTooltip("Show a context menu with advanced tools / functions for group management");
	addAndMakeVisible(helpStateButton);

	addAndMakeVisible(editButton);
	addAndMakeVisible(moreButton);
	addAndMakeVisible(tagButton);

	editButton.setToggleModeWithColourChange(true);
	tagButton.setToggleModeWithColourChange(true);

	s->getSampleMap()->addListener(this);

	addAndMakeVisible(viewport);
	viewport.setViewedComponent(&content, false);
	viewport.setScrollBarThickness(12);
	sf.addScrollBarToAnimate(viewport.getVerticalScrollBar());

	layerListener.setCallback(layerRoot, valuetree::AsyncMode::Asynchronously, VT_BIND_CHILD_LISTENER(onLayerAddRemove));
	onLayerAddRemove({}, false);

	Component::SafePointer<ComplexGroupManagerComponent> safeThis(this);

	Timer::callAfterDelay(60, [safeThis]()
	{
		if(safeThis.getComponent() != nullptr)
			safeThis->content.updateSize();
	});
}

ComplexGroupManagerComponent::~ComplexGroupManagerComponent()
{
	if(getSampler() != nullptr)
		getSampler()->getSampleMap()->removeListener(this);
}

void ComplexGroupManagerComponent::resized()
{
	auto b = getLocalBounds();

	auto topBar = b.removeFromTop(24);

	b.removeFromTop(viewport.getScrollBarThickness());

	editButton.setBounds(topBar.removeFromLeft(topBar.getHeight()).reduced(3));
	tagButton.setBounds(topBar.removeFromLeft(topBar.getHeight()).reduced(3));

	if(helpStateButton.isVisible())
	{
		helpStateButton.setBounds(topBar.removeFromRight(topBar.getHeight()).reduced(3));
	}

	moreButton.setBounds(topBar.removeFromRight(topBar.getHeight()).reduced(3));

	b.removeFromLeft(viewport.getScrollBarThickness());
	viewport.setBounds(b);
	content.setSize(viewport.getWidth() - viewport.getScrollBarThickness(), content.getHeight());
}

void ComplexGroupManagerComponent::soundsSelected(int numSelected)
{}

void ComplexGroupManagerComponent::samplePropertyWasChanged(ModulatorSamplerSound* s, const Identifier& id,
	const var& newValue)
{
	if(id == SampleIds::RRGroup)
		rebuildSampleCounters();

	ignoreUnused(s, id, newValue);
}

void ComplexGroupManagerComponent::paint(Graphics& g)
{
	g.fillAll(Colour(0xFF262626));

	auto b = getLocalBounds();

	

	if(editButton.getToggleState())
	{
		GlobalHiseLookAndFeel::draw1PixelGrid(g, this, b);
	}

	g.setColour(Colour(0xFF262626));
	auto tb = b.removeFromTop(24);
	g.fillRect(tb);
	GlobalHiseLookAndFeel::drawFake3D(g, tb);

	if(content.layerComponents.isEmpty())
	{
		g.setColour(Colour(0x33FFFFFF));
		g.setFont(GLOBAL_FONT());
		g.drawText("Click on the pen button to add logic layers", getLocalBounds().toFloat(), Justification::centred);
	}
}



void ComplexGroupManagerComponent::setDisplayFilter(uint8 layerIndex, uint8 value)
{
	auto id = getComplexGroupManager()->getLayerId(layerIndex);

	if(value == 0)
	{
		for(int i = 0; i < displayFilters.size(); i++)
		{
			if(displayFilters[i].first == id)
			{
				displayFilters.remove(i);
				break;
			}
		}
	}
	else
	{
		bool found = false;

		for(int i = 0; i < displayFilters.size(); i++)
		{
			if(displayFilters[i].first == id)
			{
				displayFilters.getReference(i).second = value;
				found = true;
				break;
			}
		}

		if(!found)
			displayFilters.add({ id, value });
	}

	if(displayFilters.isEmpty())
	{
		getSampleEditHandler()->complexGroupBroadcaster.sendMessage(sendNotificationSync, {});
	}
	else
	{
		auto f = getComplexGroupManager()->getDisplayFilter(displayFilters);
		getSampleEditHandler()->complexGroupBroadcaster.sendMessage(sendNotificationSync, f);
	}
}

void ComplexGroupManagerComponent::onLayerAddRemove(const ValueTree& v, bool added)
{
	auto hasChildren = layerRoot.getNumChildren() > 0 || added;

	tagButton.setEnabled(hasChildren);
	helpStateButton.setVisible(false);

	if(!hasChildren && !editButton.getToggleState())
	{
		editButton.setToggleState(true, sendNotificationSync);
		editButton.setToggleStateAndUpdateIcon(true, true);

		tagButton.setToggleStateAndUpdateIcon(false, true);

		String m;

		m << "Create at least one logic layer in order to use the complex group system:\n";
		m << "1. Select the layer logic type\n";
		m << "2. Give it a name / ID (optional)\n";
		m << "3. Define the number of groups for this layer. Either type in a comma separated list of strings into the Layer text field or select some samples and then choose one of the available tokens that are extracted from the filenames.\n";
		m << "> Note that if you use the file tokens the samples will automatically be assigned to the groups so you can skip the next step of assigning the samples";

		helpStateButton.setHelpText(m);
		helpStateButton.setVisible(true);
	}

	if(added)
	{
		auto idx = layerRoot.indexOf(v);

		auto unassignedList = getComplexGroupManager()->getUnassignedSamples(idx);

		if(!unassignedList.isEmpty())
		{
			tagButton.setToggleState(true, sendNotificationSync);
			tagButton.setToggleStateAndUpdateIcon(true, true);

			String m;
			m << "Assign all samples to a valid layer group:\n";
			m << "1. Click on the green tag selector next to the Unassigned button. This will select all samples that are not assigned to any layer.\n";
			m << "2. In the drop down menu choose one of the options for assigning\n";
			m << "3. Click on the assign button on the bottom right to assign the currently selected samples.\n";
			m << "4. Repeat that process until all samples are assigned and the warning button goes away.";
			helpStateButton.setVisible(true);
			helpStateButton.setHelpText(m);
		}
	}

	resized();
}

void ComplexGroupManagerComponent::rebuildSampleCounters()
{
	callRecursive<LogicTypeComponent>(this, [this](LogicTypeComponent* cb)
	{
		cb->showSelectors(tagButton.getToggleState());
		return false;
	});
}

void ComplexGroupManagerComponent::addSelectionFilter(const std::pair<uint8, uint8>& filter,
	ModifierKeys mods)
{
	auto add = mods.isShiftDown() || mods.isCommandDown();


	if(!add)
	{
		auto isSelected = isFilterSelected(filter);
		activeCountButtons.clear();

		if(!isSelected)
			activeCountButtons.add(filter);
	}
	else
	{
		auto isZero = filter.second == 0;

		for(int i = 0; i < activeCountButtons.size(); i++)
		{
			if(activeCountButtons[i].first == filter.first)
			{
				auto thisZero = activeCountButtons[i].second == 0;

				if(thisZero != isZero)
					activeCountButtons.remove(i--);
			}
		}

		activeCountButtons.addIfNotAlreadyThere(filter);
	}

	rebuildSelection();
}

bool ComplexGroupManagerComponent::isFilterSelected(const std::pair<uint8, uint8>& f) const
{
	return activeCountButtons.contains(f);
}

void ComplexGroupManagerComponent::rebuildSelection()
{
	auto& selection = getSampleEditHandler()->getSelectionReference();

	selection.deselectAll();

	if(!activeCountButtons.isEmpty())
	{
		std::map<uint8, std::vector<uint8>> filtersPerLayer;

		for(const auto& p: activeCountButtons)
		{
			filtersPerLayer[p.first].push_back(p.second);
		}

		std::map<Identifier, Array<WeakReference<SynthSoundWithBitmask>>> samplesPerLayer;

		for(const auto& p: filtersPerLayer)
		{
			auto isUnassigned = p.second.size() == 1 && p.second[0] == 0;

			auto id = getComplexGroupManager()->getLayerId(p.first);

			if(isUnassigned)
			{
				samplesPerLayer[id] = getComplexGroupManager()->getUnassignedSamples(p.first);
			}
			else
			{
				for(auto v: p.second)
					samplesPerLayer[id].addArray(getComplexGroupManager()->getFilteredSelection(p.first, v, v == ComplexGroupManager::IgnoreFlag));
			}
		}

		Array<SynthSoundWithBitmask*> newSelection;

		for(auto s: samplesPerLayer.begin()->second)
		{
			newSelection.add(s);
		}

		if(samplesPerLayer.size() >= 1)
		{
			bool first = true;

			for(const auto& sl: samplesPerLayer)
			{
				if(first)
				{
					first = false;
					continue;
				}

				for(int i = 0; i < newSelection.size(); i++)
				{
					if(!sl.second.contains(newSelection[i]))
					{
						newSelection.remove(i--);
					}
				}
			}
		}

		for(auto s: newSelection)
			selection.addToSelection(dynamic_cast<ModulatorSamplerSound*>(s));

		getSampleEditHandler()->setMainSelectionToLast();
	}

	using Count = LogicTypeComponent::BodyBase::SelectionTag;

	callRecursive<Count>(this, [](Count* c)
	{
		c->repaint();
		return false;
	});
}

ComplexGroupManagerFloatingTile::ComplexGroupActivator::ComplexGroupActivator(ModulatorSampler* s):
	b("purge", nullptr, f),
	sampler(s)
{
	addAndMakeVisible(b);
	b.setTooltip("Activate the complex group manager");

	b.onClick = [this]()
	{
		sampler->setUseComplexGroupManager(true);
		auto pc = findParentComponentOfClass<PanelWithProcessorConnection>();

		MessageManager::callAsync([pc]()
		{
			pc->refreshContent();
		});
	};
}

void ComplexGroupManagerFloatingTile::ComplexGroupActivator::paint(Graphics& g)
{
	g.fillAll(Colour(0xFF262626));

	g.setColour(Colours::white.withAlpha(0.5f));
	g.setFont(GLOBAL_FONT());

	String m;

	m << "1. Click to enable the complex group manager for this sampler\n";
	m << "2. Create one or more logic layers with multiple groups that define a voice start logic\n";
	m << "3. Assign samples to each layer group using the file tokens or by manual selection.\n";

	g.drawMultiLineText(m, 10, 10 + 30, getWidth()-20, Justification::centred);
}

void ComplexGroupManagerFloatingTile::ComplexGroupActivator::resized()
{
	b.setBounds(getLocalBounds().withSizeKeepingCentre(48, 48));
}

Component* ComplexGroupManagerFloatingTile::createContentComponent(int)
{
	

	if(auto s = dynamic_cast<ModulatorSampler*>(getProcessor()))
	{
		if (s != lastSampler)
		{
			if(lastSampler != nullptr)
				lastSampler->getSampleEditHandler()->complexGroupEventBroadcaster.removeListener(*this);

			lastSampler = s;

			s->getSampleEditHandler()->complexGroupEventBroadcaster.addListener(*this, [](ComplexGroupManagerFloatingTile& ft, SampleEditHandler::ComplexGroupEvent e)
			{
				if (e == SampleEditHandler::ComplexGroupEvent::ComplexManagerDisabled ||
					e == SampleEditHandler::ComplexGroupEvent::ComplexManagerEnabled)
					ft.refreshContent();
			}, false);
		}

		if (auto g = s->getComplexGroupManager())
			return new ComplexGroupManagerComponent(s);
		else
			return new ComplexGroupActivator(s);
	}

	return nullptr;
}

ComplexGroupManagerComponent::LogicTypeComponent::BodyKnob::BodyKnob(LogicTypeComponent& parent, const Identifier& propertyId, scriptnode::InvertableParameterRange rng) :
	v(parent.data),
	id(propertyId),
	um(parent.getUndoManager())
{
	setName(propertyId.toString());
	setSliderStyle(Slider::SliderStyle::LinearHorizontal);
	setRange(rng.rng.getRange(), rng.rng.interval);
	setSkewFactor(rng.rng.skew);
	setTextBoxStyle(TextEntryBoxPosition::NoTextBox, false, 0, 0);

	valueListener.setCallback(v, { id }, valuetree::AsyncMode::Asynchronously, VT_BIND_PROPERTY_LISTENER(onValue));
	addListener(this);
	setLookAndFeel(&laf);
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyKnob::sliderValueChanged(Slider*)
{
	v.setProperty(id, getValue(), um);
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyKnob::mouseDown(const MouseEvent& e)
{
	if (SliderWithShiftTextBox::performModifierAction(e, false, true))
		return;

	Slider::mouseDown(e);
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyKnob::onValue(const Identifier&, const var& newValue)
{
	auto d = (double)newValue;
	FloatSanitizers::sanitizeDoubleNumber(d);
	setValue(d, dontSendNotification);
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyKnob::setTextConverter(const String& s)
{
	auto vtc = ValueToTextConverter::createForMode(s);
	this->textFromValueFunction = vtc;
	this->valueFromTextFunction = vtc;
}

void ComplexGroupManagerComponent::LogicTypeComponent::BodyKnob::Laf::drawLinearSlider(Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, const Slider::SliderStyle, Slider& s)
{
	Rectangle<float> b((float)x, (float)y, (float)width, (float)height);

	float alpha = 0.4f;

	if (s.isMouseOver(true))
		alpha += 0.1f;

	if (s.isMouseButtonDown(true))
		alpha += 0.2f;

	g.setColour(Colours::white.withAlpha(alpha));

	auto tv = s.getTextFromValue(s.getValue());

	g.setFont(GLOBAL_MONOSPACE_FONT());
	g.drawText(s.getName(), b.reduced(4.0f), Justification::centredTop);
	g.setFont(GLOBAL_FONT());
	g.drawText(tv, b.reduced(4.0f), Justification::centredBottom);

	NormalisableRange<double> nr(s.getRange());
	nr.skew = s.getSkewFactor();

	auto w = nr.convertTo0to1(s.getValue()) * b.getWidth();

	g.setColour(Colour(0x10FFFFFF));
	g.fillRect(b);
	g.fillRect(b.removeFromLeft(w));
}

}
