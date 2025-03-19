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

#include "DebugProfileTools.h"

namespace hise { using namespace juce;

Component* DebugSession::createMultiViewer()
{
	return new ProfileDataSource::ViewComponents::MultiViewer(*this);
}

bool DebugSession::isRecordingMultithread() const
{
	return nextState.load() != RecordingState::Idle;
}

bool DebugSession::shouldProfileInitialisation() const
{
	return profileInitialisation;
}

void DebugSession::setProfileInitialisation(bool shouldProfileInit)
{
	profileInitialisation = shouldProfileInit;
}

struct DebugSession::ProfileDataSource::ViewComponents::Manager::SearchBar: public Component,
                                                                            public TextEditor::Listener
{
	SearchBar():
	  clearButton("clear", nullptr, f)
	{
		
		addAndMakeVisible(editor);
		addAndMakeVisible(clearButton);
		clearButton.setVisible(false);
		clearButton.onClick = [this](){ editor.setText({}, sendNotificationSync); };
		clearButton.setColours(Colours::black.withAlpha(0.3f), Colours::black.withAlpha(0.5f), Colours::black.withAlpha(0.7f));
		editor.setTextToShowWhenEmpty("Search timeline...", Colours::black.withAlpha(0.4f));
		editor.setEscapeAndReturnKeysConsumed(true);
		editor.setSelectAllWhenFocused(true);
		GlobalHiseLookAndFeel::setTextEditorColours(editor);
		searchIcon = f.createPath("search");
		editor.setSelectAllWhenFocused(true);
		editor.addListener(this);
	}

	void textEditorTextChanged(TextEditor& te) override
	{
		findParentComponentOfClass<Manager>()->setSearchTerm(te.getText(), false);
		clearButton.setVisible(!te.isEmpty());
	}

	void textEditorEscapeKeyPressed(TextEditor& te) override
	{
		te.setText({}, dontSendNotification);
		clearButton.setVisible(false);
	}

	void textEditorReturnKeyPressed(TextEditor& te) override
	{
		auto m = findParentComponentOfClass<Manager>();
		m->setSearchTerm(te.getText(), true);
		clearButton.setVisible(!te.isEmpty());
	}

	void paint(Graphics& g) override
	{
		g.setColour(Colours::white.withAlpha(0.4f));
		g.fillPath(searchIcon);
	}

	void resized()
	{
		auto b = getLocalBounds();
		PathFactory::scalePath(searchIcon, b.removeFromLeft(b.getHeight()).reduced(4).toFloat());
		editor.setBounds(b.reduced(1));
		clearButton.setBounds(b.removeFromRight(b.getHeight()).reduced(5));
	}

	Factory f;
	Path searchIcon;
	TextEditor editor;
	HiseShapeButton clearButton;
	
};

struct DebugSession::ProfileDataSource::ViewComponents::Manager::NavigationAction: public UndoableAction
{
	NavigationAction(Manager& v, BaseWithManagerConnection* source_, ViewItem::Ptr old, ViewItem::Ptr n):
	  parent(v),
	  source(source_),
	  oldRoot(old),
	  newRoot(n)
	{};

	bool perform() override
	{
		if(newRoot != nullptr)
			return parent.navigate(source, newRoot, false);

		return false;
	}

	bool undo() override
	{
		if(newRoot != nullptr)
			return parent.navigate(source, oldRoot, false);

		return false;
	}

	WeakReference<BaseWithManagerConnection> source;
	Manager& parent;
	ViewItem::Ptr oldRoot;
	ViewItem::Ptr newRoot;
};

struct DebugSession::ProfileDataSource::ViewComponents::Manager::UndoableTotalRootChange: public UndoableAction
{
	UndoableTotalRootChange(BaseWithManagerConnection* source_, Manager& p, ViewItem::Ptr newRoot_):
	  UndoableAction(),
	  parent(p),
	  source(source_),
	  oldRoot(p.totalRoot),
	  newRoot(newRoot_)
	{}

	bool perform() override
	{
		return parent.setNewRoot(source, newRoot.get(), false);
	}

	bool undo() override
	{
		return parent.setNewRoot(source, oldRoot.get(), false);
	}

	Manager& parent;
	WeakReference<BaseWithManagerConnection> source;
	ViewItem::Ptr newRoot;
	ViewItem::Ptr oldRoot;
};

bool DebugSession::ProfileDataSource::ViewComponents::Manager::setNewRoot(BaseWithManagerConnection* source,
	ViewItem::Ptr newRoot, bool useUndoManager)
{
	if(newRoot != totalRoot)
	{
		if(useUndoManager)
		{
			um.beginNewTransaction();
			um.perform(new UndoableTotalRootChange(source, *this, newRoot));
		}
		else
		{
			totalRoot = newRoot;
			currentRoot = totalRoot;
			sendRootChangeMessage(source);
		}
	}

	return true;
}

bool DebugSession::ProfileDataSource::ViewComponents::Manager::navigate(BaseWithManagerConnection* source,
                                                                        ViewItem::Ptr newTarget, bool useUndoManager)
{
	if(newTarget != currentRoot)
	{
		if(useUndoManager)
		{
			um.beginNewTransaction();
			um.perform(new NavigationAction(*this, source, currentRoot, newTarget));
		}
		else
		{
			currentRoot = newTarget;
			sendRootChangeMessage(source);
				
		}
	}

	return true;
}

void DebugSession::ProfileDataSource::ViewComponents::Manager::sendRootChangeMessage(BaseWithManagerConnection* source)
{
	for(int i = 0; i < connectedComponents.size(); i++)
	{
		auto b = connectedComponents[i];
		if(b != nullptr)
			b->onNavigation(source, totalRoot, currentRoot);
	}
}

void DebugSession::ProfileDataSource::ViewComponents::Manager::setActiveViewer(Viewer* newViewer)
{
	if(newViewer != currentViewer)
	{
		currentViewer = newViewer;
		totalRoot = currentViewer->originalRoot;
		currentRoot = currentViewer->rootItem;

		sendRootChangeMessage(currentViewer.get());
	}
}

String DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::getColumnName(int columnId)
{
	static const StringArray names({
		"Index",
		"Depth",
		"Name",
		"#",
		"Thread",
		"Type",
		"Exclusive",
		"Total",
		"Average",
		"Peak"
	});

	return names[columnId-1];
}

DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::RowData::RowData(ViewItem& i, int index_)
{
	auto thisLength = currentRunIndex == -1 ? i.avg.length : i.runs[currentRunIndex].length;;
	auto n = i.name.upToFirstOccurrenceOf("[", false, false);

	auto childLength = 0.0;

	for(auto c: i.children)
	{
		auto cLength = currentRunIndex == -1 ? c->avg.length : c->runs[currentRunIndex].length;;
		childLength += cLength;
	}

	auto exclusiveLength = thisLength - childLength;

	currentRunIndex = currentRunIndex;
	exclusiveTime = exclusiveLength;
	name = n;
	firstItem = &i;
	occurrences = 1;
	totalTime = thisLength;
	peak = thisLength;
	index = index_;
	depth = i.depth;
	threadType = i.threadType;
	sourceType = i.sourceType;
	c = i.c;
}

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::RowData::draw(Graphics& g, int columnId,
	Rectangle<float> rectangle, TimeDomain d, double domainContext, bool rowIsSelected)
{
	g.setColour(Colours::white.withAlpha(rowIsSelected ? 1.0f : 0.6f));
	g.setFont(rowIsSelected ? GLOBAL_BOLD_FONT() : GLOBAL_FONT());

	String t;
	if(columnId == Index)
		t << "#" << String(index+1);
	if(columnId == Name)
	{
		t << name;

		auto b = rectangle.removeFromLeft(rectangle.getHeight()).reduced(3.0f);

		g.setColour(c);
		g.fillRoundedRectangle(b, 4.0f);
		g.setColour(Colours::white.withAlpha(0.6f));
		g.drawRoundedRectangle(b, 4.0f, 1.0f);
		g.setFont(GLOBAL_MONOSPACE_FONT());
	}

	if(columnId == Depth)
		t << String(depth);
	if(columnId == Thread)
		t << ThreadIdentifier::getThreadName(threadType).replace("Thread", "");
	if(columnId == Num)
		t << String(occurrences) << "x";
	if(columnId == Source)
		t << ProfileDataSource::getSourceTypeName(sourceType);
	if(columnId == Duration)
		t << Helpers::getDuration((float)totalTime, d, domainContext);
	if(columnId == Exclusive)
		t << Helpers::getDuration((float)exclusiveTime, d, domainContext);
	if(columnId == Average)
		t << Helpers::getDuration((float)totalTime / (float)occurrences, d, domainContext);
	if(columnId == Peak)
		t << Helpers::getDuration((float)peak, d, domainContext);
			
	g.setColour(Colours::white.withAlpha(rowIsSelected ? 1.0f : 0.6f));
	g.drawText(t, rectangle.reduced(5.0f, 0.0f), Justification::left);
}

DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::StatisticsComponent(DebugSession& s):
  session(s)
{
	addAndMakeVisible(table);

	table.setClickingTogglesRowSelection(true);
	table.setMouseMoveSelectsRows(true);

	//table.setMultipleSelectionEnabled(true);
	table.setModel(this);
	table.setColour(TableListBox::ColourIds::backgroundColourId, Colour(0xFF242424));
	table.getHeader().setLookAndFeel(&tlaf);
	table.getHeader().setStretchToFitActive(true);
	table.getHeader().addColumn(getColumnName(ColumnId::Index), ColumnId::Index, 50, 50, 50);
	table.getHeader().addColumn(getColumnName(ColumnId::Depth), ColumnId::Depth, 50, 50, 50);
	table.getHeader().addColumn(getColumnName(ColumnId::Name), ColumnId::Name, 100, 100, -1);
	table.getHeader().addColumn(getColumnName(ColumnId::Thread), ColumnId::Thread, 100, 50, 100);
	table.getHeader().addColumn(getColumnName(ColumnId::Source), ColumnId::Source, 120, 50, 120);
	table.getHeader().addColumn(getColumnName(ColumnId::Num), ColumnId::Num, 40, 40, 40);
	table.getHeader().addColumn(getColumnName(ColumnId::Duration), ColumnId::Duration, 80, 40, -1);
	table.getHeader().addColumn(getColumnName(ColumnId::Exclusive), ColumnId::Exclusive, 80, 40, -1);
	table.getHeader().addColumn(getColumnName(ColumnId::Average), ColumnId::Average, 80, 40,  -1);
	table.getHeader().addColumn(getColumnName(ColumnId::Peak), ColumnId::Peak, 80, 40, -1);

	table.getViewport()->setScrollBarThickness(12.0f);
	sf.addScrollBarToAnimate(table.getViewport()->getVerticalScrollBar());
}

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::selectedRowsChanged(int lastSelected)
{
	currentSelection = lastSelected;

	if(auto m = getManager())
	{
		String s;

		if(auto r = sorted[currentSelection])
			s = r->name;

		ScopedValueSetter<bool> svs(ignoreSearch, true);
		m->setSearchTerm(s, false);
	}
}

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::cellClicked(int rowNumber,
                                                                                       int columnId, const MouseEvent& e)
{
	if(auto r = sorted[rowNumber])
	{
		if(auto m = getManager())
		{
			if(e.mods.isRightButtonDown() && m->currentViewer != nullptr)
			{
				std::unique_ptr<Component> c;
				c.reset(new ItemPopup(m->currentViewer, r->firstItem));

				if(auto t = TopLevelWindowWithOptionalOpenGL::findRoot(this))
				{
					auto tBounds = table.getRowPosition(rowNumber, true);
					auto b = t->getLocalArea(this, getLocalBounds().getIntersection(tBounds.toNearestInt()));
					CallOutBox::launchAsynchronously(std::move(c), b, t);
					return;
				}
			}
			else
			{
				m->zoomToFirstMatch(r->firstItem);
			}
		}

		

		
	}
}

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::cellDoubleClicked(int rowNumber,
	int columnId, const MouseEvent& mouseEvent)
{
	if(!isConnected())
		return;

	if(auto r = sorted[rowNumber])
	{
		getManager()->navigate(this, r->firstItem, true);

		//parent->navigate(r->firstItem, true);
	}
}

int DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::compareElements(RowData* r1,
                                                                                          RowData* r2) const
{
	int result;

	switch(currentSortId)
	{
	case Name:
		result = r1->name.compareNatural(r2->name);
		break;
	case Num:
		result = r1->occurrences < r2->occurrences ? 1 : (r1->occurrences > r2->occurrences ? -1 : 0);
		break;
	case Thread:
		result = (int)(r1->threadType) < (int)(r2->threadType) ? 1 : ((int)(r1->threadType) > (int)(r2->threadType) ? -1 : 0);
		break;
	case Duration:
		result = r1->totalTime < r2->totalTime ? 1 : (r1->totalTime > r2->totalTime ? -1 : 0);
		break;
	case Depth:
		result = r1->depth < r2->depth ? 1 : (r1->depth > r2->depth ? -1 : 0);
		break;
	case Exclusive:
		result = r1->exclusiveTime < r2->exclusiveTime ? 1 : (r1->exclusiveTime > r2->exclusiveTime ? -1 : 0);
		break;
	case Source:
		result = (int)(r1->sourceType) < (int)(r2->sourceType) ? 1 : ((int)(r1->sourceType) > (int)(r2->sourceType) ? -1 : 0);
		break;
	case Average:
		result = (r1->totalTime / (double)r1->occurrences) < (r2->totalTime / (double)r2->occurrences) ? 1 : ((r1->totalTime / (double)r1->occurrences > r2->totalTime / (double)r2->occurrences) ? -1 : 0);
		break;
	case Peak:
		result = r1->peak < r2->peak ? 1 : (r1->peak > r2->peak ? -1 : 0);
		break;
	case Index:
		result = r1->index < r2->index ? 1 : (r1->index > r2->index ? -1 : 0);
		break;
	default: ;
	}

	if(!sortForward)
		result *= -1;

	return result;
}

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::updateSearchTerm()
{
	sorted.clear();

	if(isConnected())
	{
		auto m = getManager();

		for(auto r: rows)
		{
			auto showType = m->showSourceType(r->sourceType);

			if(showType && (searchTerm.isEmpty() || r->name.toLowerCase().contains(searchTerm)))
				sorted.add(r);
		}
	}

	sorted.sort(*this);
	table.updateContent();
	resized();
}

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::onSearchUpdate(const String& s, bool unused)
{
	if(ignoreSearch)
		return;

	searchTerm = s.toLowerCase();
	updateSearchTerm();
}

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::sortOrderChanged(int newSortColumnId,
	bool isForwards)
{
	currentSortId = (ColumnId)newSortColumnId;
	sortForward = isForwards;

	sorted.sort(*this);
}

int DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::getNumRows()
{ return sorted.size(); }

bool DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::isConnected() const
{ return currentRoot != nullptr; }

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::setRootItem(ViewItem::Ptr newRoot,
	int currentRunIndex)
{
	if(currentRoot != newRoot)
	{
		currentRoot = newRoot;

		sorted.clear();
		rows.clear();
		int idx = 0;

		newRoot->callAllRecursive([&](ViewItem& i)
		{
			auto skip = i.threadType == ThreadIdentifier::Type::Undefined;
			skip |= i.depth == 1 && i.multiThreadMode;

			if(skip)
				return false;

			RowData::Ptr nr = new RowData(i, idx);
				
			for(auto& ro: rows)
			{
				if(ro->name == nr->name)
				{
					ro->occurrences++;
					ro->totalTime += nr->totalTime;
					ro->exclusiveTime += nr->exclusiveTime;
					ro->currentRunIndex = nr->currentRunIndex;
					ro->peak = jmax(ro->peak, nr->peak);
					return false;
				}
			}
				
			rows.add(nr);
			idx++;
			return false;
		});

		updateSearchTerm();
	}
}

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::paintRowBackground(Graphics& g,
	int rowNumber, int width, int height, bool rowIsSelected)
{
	g.fillAll(Colour(rowNumber % 2 == 0 ? 0xFF242424 : 0xFF303030));
}

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::paintCell(Graphics& g, int rowNumber,
	int columnId, int width, int height, bool rowIsSelected)
{
	if(!isConnected())
		return;

	if(auto r = sorted[rowNumber])
		r->draw(g, columnId, Rectangle<int>(0, 0, width, height).toFloat(), domain, contextValue, rowIsSelected);
}

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::resized()
{
	initManager();
	table.getHeader().resizeAllColumnsToFit(getWidth());
	table.setBounds(getLocalBounds());
}

DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::RowData::Ptr DebugSession::ProfileDataSource::
ViewComponents::StatisticsComponent::getRowDataForName(const String& n) const
{
	for(auto d: rows)
	{
		if(d->name == n)
			return d;
	}

	return nullptr;
}

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::onNavigation(
	BaseWithManagerConnection* navigationSource, ViewItem::Ptr totalRoot, ViewItem::Ptr currentRoot)
{
	setRootItem(currentRoot, -1);
}

void DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent::onTimeDomainUpdate(TimeDomain newDomain,
                                                                                              double context)
{
	contextValue = context;
	domain = newDomain;
	repaint();
}


DebugSession::ProfileDataSource::ViewComponents::ItemPopup::ItemPopup(Viewer* p, ViewItem::Ptr item_):
	parent(p),
	item(item_),
	rootButton("root", nullptr, f),
	combineButton("combine", nullptr, f),
	gotoButton("goto", nullptr, f)
{
	if(auto di = dynamic_cast<ProfileInfo*>(item->infoItem.get()))
	{
		if(auto first = di->data.attachedData.getFirst())
		{
			attachedData = first;
		}
	}

	addAndMakeVisible(rootButton);
	addAndMakeVisible(combineButton);
	addAndMakeVisible(gotoButton);

	rootButton.setTooltip("Set as root");
	combineButton.setTooltip("Set as root & combine calls into timeline");
	gotoButton.setTooltip("Show event source in HISE");

	rootButton.onClick = [this]()
	{
		parent->navigate(item, true);
		findParentComponentOfClass<CallOutBox>()->dismiss();
	};

	combineButton.onClick = [this]()
	{
		parent->combineRuns(parent->originalRoot->infoItem, item->infoItem);
		findParentComponentOfClass<CallOutBox>()->dismiss();
	};

	gotoButton.onClick = [this]()
	{
		item->infoItem->gotoLocation(this);
		findParentComponentOfClass<CallOutBox>()->dismiss();
	};

	auto n = item->name.upToFirstOccurrenceOf("[", false, false);

	if(auto s = p->getStatistics())
		rd = s->getRowDataForName(n);

	auto vg = new DebugSession::Session::ValueGroup();

	attachedData = vg;

	auto internalTracks = item->createTracks();

	item->callAllRecursive([&](ViewItem& vi)
	{
		if(auto pi = dynamic_cast<ProfileInfo*>(vi.infoItem.get()))
		{
			if(pi->data.trackSource != -1 && item != &vi)
			{
				bool found = false;

				for(auto v: internalTracks)
				{
					if(v.first == &vi)
					{
						found = true;
						break;
					}
				}

				if(!found)
					sources.add(&vi);
			}
			if(pi->data.trackTarget != -1 && item != &vi)
			{
				bool found = false;

				for(auto v: internalTracks)
				{
					if(v.second == &vi)
					{
						found = true;
						break;
					}
				}

				if(!found)
					targets.add(&vi);
			}

			for(auto l: pi->data.attachedData)
			{
				vg->dataItems.add(l);
			}
		}

		return false;
	});

	if(vg->dataItems.isEmpty())
		attachedData = nullptr;
	else if (vg->dataItems.size() == 1)
		attachedData = vg->dataItems[0];

	if(attachedData != nullptr)
	{
		dataComponent = attachedData->createComponent(false);
		addAndMakeVisible(dataComponent);
	}

	auto numItemsToShow = 1 + sources.size() + targets.size();

	setSize(600, numItemsToShow * (ItemHeight + ItemMargin) + LabelMargin + 2 * RowHeight + 20 + MenuBarHeight + (dataComponent != nullptr ? dataComponent->getHeight() : 0));
}

Path DebugSession::ProfileDataSource::ViewComponents::ItemPopup::Factory::createPath(const String& url) const
{
	Path p;

	LOAD_EPATH_IF_URL("goto", ColumnIcons::openWorkspaceIcon);
	LOAD_EPATH_IF_URL("combine", EditorIcons::peakIcon);
	LOAD_EPATH_IF_URL("root", MainToolbarIcons::home);

	if(url == "combine")
		p.applyTransform(AffineTransform::scale(1.0f, 2.0f));

	return p;
}

void DebugSession::ProfileDataSource::ViewComponents::ItemPopup::paint(Graphics& g)
{
	g.fillAll(Colour(0xFF242424));
	auto b = getLocalBounds().toFloat().reduced(10);

	if(!isConnected())
		return;

	ViewItem::List allItems;
	allItems.add(item);
	allItems.addArray(sources);
	allItems.addArray(targets);

	Path fw;
	fw.loadPathFromData(EditorIcons::forwardIcon, EditorIcons::forwardIcon_Size);
	Path bk;
	bk.loadPathFromData(EditorIcons::backIcon, EditorIcons::backIcon_Size);

	for(auto v: allItems)
	{
		ViewItem::DrawContext ctx;
		ctx.scaleFactor = 1.0;
		ctx.transpose = 0.0;
		ctx.currentIndex = parent->currentIndex;
		ctx.currentlyHoveredItem = parent->currentlyHoveredItem;
		ctx.currentRoot = v;

		auto tb = b.removeFromTop((float)ItemHeight);

		g.setColour(Colours::white.withAlpha(0.7f));

		if(sources.contains(v))
		{
			PathFactory::scalePath(fw, tb.removeFromRight(tb.getHeight()).reduced(3.0f));
			g.fillPath(fw);
		}
			
		if(targets.contains(v))
		{
			PathFactory::scalePath(bk, tb.removeFromLeft(tb.getHeight()).reduced(3.0f));
			g.fillPath(bk);
		}


		ctx.fullBounds = tb;
		ctx.totalWidth = getLocalBounds().getWidth();
		ctx.componentToDrawOn = this;
		ctx.displayRange = { 0.0, 1.0 };

		b.removeFromTop((float)ItemMargin);
		v->paintItem(g, ctx.fullBounds, ctx);
	}

	Helpers::drawLabel(g, { b.removeFromTop(LabelMargin), "Event Properties" });

	if(rd != nullptr)
	{
		auto w = (float)getWidth() / 3.0f;
		auto r1 = b.removeFromTop((float)RowHeight);
		auto r2 = b.removeFromTop((float)RowHeight);

		drawStatistics(g, StatisticsComponent::Source, r1.removeFromLeft(w));
		drawStatistics(g, StatisticsComponent::Num, r2.removeFromLeft(w));
		drawStatistics(g, StatisticsComponent::Duration, r2.removeFromLeft(w));
		drawStatistics(g, StatisticsComponent::Thread, r1.removeFromLeft(w));
		drawStatistics(g, StatisticsComponent::Depth, r1.removeFromLeft(w));
		drawStatistics(g, StatisticsComponent::Exclusive, r2.removeFromLeft(w));
	}
}

void DebugSession::ProfileDataSource::ViewComponents::ItemPopup::resized()
{
	int ButtonMargin = 4;

	auto b = getLocalBounds();

	if(dataComponent != nullptr)
		dataComponent->setBounds(b.removeFromBottom(dataComponent->getHeight()));

	b = b.removeFromBottom(MenuBarHeight);
	rootButton.setBounds(b.removeFromLeft(MenuBarHeight).reduced(ButtonMargin));
	combineButton.setBounds(b.removeFromLeft(MenuBarHeight).reduced(ButtonMargin));
	gotoButton.setBounds(b.removeFromLeft(MenuBarHeight).reduced(ButtonMargin));
}

void DebugSession::ProfileDataSource::ViewComponents::ItemPopup::drawStatistics(Graphics& g,
	StatisticsComponent::ColumnId c, Rectangle<float> bounds)
{
	if(rd != nullptr && isConnected())
	{
		auto title = StatisticsComponent::getColumnName((int)c) + ": ";

		auto bf = GLOBAL_BOLD_FONT();

		g.setFont(bf);
		g.setColour(Colours::white.withAlpha(0.8f));

		auto w = bf.getStringWidthFloat(title);

		auto tb = bounds.removeFromLeft(w);
		g.drawText(title, tb, Justification::left);

		rd->draw(g, (int)c, bounds, parent->currentDomain, parent->domainContext, false);
	}
}

DebugSession::ProfileDataSource::ViewComponents::BaseWithManagerConnection::~BaseWithManagerConnection()
{
	if(auto m = currentManager.get())
		m->connectedComponents.removeAllInstancesOf(this);
}

DebugSession::ProfileDataSource::ViewComponents::Manager* DebugSession::ProfileDataSource::ViewComponents::
BaseWithManagerConnection::getManager()
{
	initManager();
	return currentManager.get();
}

DebugSession::ProfileDataSource::ViewComponents::Manager* DebugSession::ProfileDataSource::ViewComponents::
BaseWithManagerConnection::initManager()
{
	if(currentManager == nullptr)
	{
		currentManager = Manager::findManager(dynamic_cast<Component*>(this));

		if(currentManager != nullptr)
			currentManager->connectedComponents.addIfNotAlreadyThere(this);

		return currentManager;
	}

	return nullptr;
}

Path DebugSession::ProfileDataSource::ViewComponents::Manager::Factory::createPath(const String& url) const
{
	Path p;

	LOAD_EPATH_IF_URL("open", EditorIcons::openFile);
	LOAD_EPATH_IF_URL("save", EditorIcons::saveFile);
	LOAD_EPATH_IF_URL("delete", SampleMapIcons::deleteSamples);
	LOAD_EPATH_IF_URL("init", HnodeIcons::jit);
	LOAD_EPATH_IF_URL("trim", EditorIcons::scissorIcon);
	LOAD_EPATH_IF_URL("undo", EditorIcons::backIcon);
	LOAD_EPATH_IF_URL("redo", EditorIcons::forwardIcon);
	LOAD_EPATH_IF_URL("home", MainToolbarIcons::home);
	LOAD_EPATH_IF_URL("peak", EditorIcons::peakIcon);
	LOAD_EPATH_IF_URL("favorite", EditorIcons::favoriteIcon);
	LOAD_EPATH_IF_URL("search", EditorIcons::searchIcon);
	LOAD_EPATH_IF_URL("clear", EditorIcons::clearTextEditorIcon);

	if(url == "search")
		p.applyTransform(AffineTransform::rotation(float_Pi));

	if(url == "peak")
		p.applyTransform(AffineTransform::scale(1.0f, 2.0f));

	if (url == "record")
		p.addEllipse({ 0.0f, 0.0f, 1.0f, 1.0f });

	return p;
}

DebugSession::ProfileDataSource::ViewComponents::Manager* DebugSession::ProfileDataSource::ViewComponents::Manager::
findManager(Component* c)
{
	Manager* m = nullptr;

	while(c != nullptr)
	{
		Component::callRecursive<Manager>(c, [&](Manager* cm)
		{
			m = cm;
			return true;
		});

		if(m != nullptr)
			return m;

		c = c->getParentComponent();
	}

	return m;
}

DebugSession::ProfileDataSource::ViewComponents::Manager::Manager(DebugSession& s):
	session(s),
	openButton("open", nullptr, f),
	saveButton("save", nullptr, f),
	recordButton("record", nullptr, f),
	trimButton("trim", nullptr, f),
	initButton("init", nullptr, f),
	undoButton("undo", nullptr, f),
	redoButton("redo", nullptr, f),
	searchBar(new SearchBar()) 
{
	addAndMakeVisible(openButton);
	addAndMakeVisible(saveButton);
	addAndMakeVisible(recordButton);
	addAndMakeVisible(initButton);
	addAndMakeVisible(trimButton);
	addAndMakeVisible(undoButton);
	addAndMakeVisible(redoButton);
	addAndMakeVisible(searchBar);

    openButton.setTooltip("Import a profile session from the clipboard");
    saveButton.setTooltip("Copy the current profiling session to the clipboard");
    recordButton.setTooltip("Toggle the profiling recording session");
    initButton.setTooltip("Enable automatic profiling of compilation / project load");
    undoButton.setTooltip("Navigate back");
    redoButton.setTooltip("Navigate forward");
    trimButton.setTooltip("Copy the currently displayed range into a new profiling session");
    
	recordButton.onColour = Colour(HISE_ERROR_COLOUR);
	recordButton.setToggleModeWithColourChange(true);

	initButton.setToggleModeWithColourChange(true);

	undoButton.onClick = [this](){ um.undo(); };
	redoButton.onClick = [this](){ um.redo(); };

	initButton.onClick = [this]()
	{
		this->session.setProfileInitialisation(initButton.getToggleState());
	};

	trimButton.onClick = [this]()
	{
		if(auto r = currentRoot)
		{
			if(currentViewer != nullptr)
				currentViewer->trimToCurrentRange();
		}
	};

	recordButton.onClick = [this]()
	{
		//new ProfiledComponent::RecordListener(session, this);

		if(recordButton.getToggleState())
			this->session.startRecording(20000, &this->session);
		else
			this->session.stopRecording();
	};

	saveButton.onClick = [this]()
	{
		if(auto r = currentRoot->infoItem)
		{
			SystemClipboard::copyTextToClipboard(r->toBase64());
		}
	};

	openButton.onClick = [this]()
	{
		auto b64 = SystemClipboard::getTextFromClipboard();

		if(b64.startsWith("HiseProfile"))
		{
			auto n = ProfileDataSource::MultiThreadSession::fromBase64(b64, session);
			ProfileInfoBase::Ptr s = n.get();
			session.recordingFlushBroadcaster.sendMessage(sendNotificationSync, s);
		}
	};

	session.syncRecordingBroadcaster.addListener(*this, onRec, false);
}

void DebugSession::ProfileDataSource::ViewComponents::Manager::resized()
{
	auto menuBar = getLocalBounds();

	searchBar->setBounds(menuBar.removeFromRight(300).reduced(5));

	int ButtonMargin = 10;
	undoButton.setBounds(menuBar.removeFromLeft(MultiViewerMenuBarHeight).reduced(ButtonMargin));
	redoButton.setBounds(menuBar.removeFromLeft(MultiViewerMenuBarHeight).reduced(ButtonMargin));
    menuBar.removeFromLeft(ButtonMargin);
    initButton.setBounds(menuBar.removeFromLeft(MultiViewerMenuBarHeight).reduced(ButtonMargin));
    recordButton.setBounds(menuBar.removeFromLeft(MultiViewerMenuBarHeight).reduced(ButtonMargin));
	menuBar.removeFromLeft(ButtonMargin);
	openButton.setBounds(menuBar.removeFromLeft(MultiViewerMenuBarHeight).reduced(ButtonMargin));
	saveButton.setBounds(menuBar.removeFromLeft(MultiViewerMenuBarHeight).reduced(ButtonMargin));
    trimButton.setBounds(menuBar.removeFromLeft(MultiViewerMenuBarHeight).reduced(ButtonMargin));
}

void DebugSession::ProfileDataSource::ViewComponents::Manager::setRecording(bool isRecording)
{
	recordButton.setToggleStateAndUpdateIcon(isRecording, true);
}

void DebugSession::ProfileDataSource::ViewComponents::Manager::setTimeDomain(TimeDomain newDomain,
                                                                             double newDomainContext)
{
	if(currentDomain != newDomain)
	{
		currentDomain = newDomain;
		domainContext = newDomainContext;

		for(auto b: connectedComponents)
		{
			b->onTimeDomainUpdate(newDomain, newDomainContext);
		}
	}
}

void DebugSession::ProfileDataSource::ViewComponents::Manager::toggleTypeFilter(SourceType source)
{
	typeFilters.setBit((int)source, !typeFilters[(int)source]);

	for(auto c: connectedComponents)
	{
		if(c != nullptr)
			c->onSearchUpdate(lastSearch, false);
	}
}

bool DebugSession::ProfileDataSource::ViewComponents::Manager::showSourceType(SourceType source) const
{
	return !typeFilters[(int)source];
}

void DebugSession::ProfileDataSource::ViewComponents::Manager::setPopupMode(bool usePopupMode)
{
	recordButton.setVisible(!usePopupMode);
	trimButton.setVisible(!usePopupMode);
	initButton.setVisible(!usePopupMode);
}

void DebugSession::ProfileDataSource::ViewComponents::Manager::onRec(Manager& m, bool isEnabled)
{
	SafeAsyncCall::call<Manager>(m, [isEnabled](Manager& m)
	{
		m.setRecording(isEnabled);
	});
}

void DebugSession::ProfileDataSource::ViewComponents::Manager::setSearchTerm(const String& searchTerm, bool zoomToFirst)
{
	lastSearch = searchTerm;

	for(auto b: connectedComponents)
	{
		if(b != nullptr)
			b->onSearchUpdate(searchTerm, zoomToFirst);
	}
}

void DebugSession::ProfileDataSource::ViewComponents::Manager::zoomToFirstMatch(ViewItem::Ptr itemToZoomTo)
{
	if(currentViewer != nullptr)
	{
		currentViewer->zoomToItem(itemToZoomTo);
	}
}
} // namespace hise
