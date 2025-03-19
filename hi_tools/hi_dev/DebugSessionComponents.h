#pragma once

namespace hise
{
using namespace juce;

struct DebugSession::ProfileDataSource::ViewComponents::BaseWithManagerConnection
{
	virtual ~BaseWithManagerConnection();;

	virtual void onNavigation(BaseWithManagerConnection* navigationSource, ViewItem::Ptr totalRoot, ViewItem::Ptr currentRoot) {};

	virtual void onSearchUpdate(const String& searchTerm, bool zoomToFirst) {};

	virtual void onTimeDomainUpdate(TimeDomain newDomain, double context) {};

protected:

	Manager* getManager();

	Manager* initManager();

	

private:

	friend struct SingleThreadPopup;
	WeakReference<Manager> currentManager;

	JUCE_DECLARE_WEAK_REFERENCEABLE(BaseWithManagerConnection);
};

struct DebugSession::ProfileDataSource::ViewComponents::Manager: public Component
{
	struct SearchBar;
	struct NavigationAction;
	struct UndoableTotalRootChange;

	struct Factory: public PathFactory
	{
		Path createPath(const String& url) const override;
	};

	/** Recursively goes up the parent component hierarchy to find the first manager component. */
	static Manager* findManager(Component* c);

	SET_GENERIC_PANEL_ID("ProfilerManager");

	Manager(DebugSession& s);

	static void onRec(Manager& m, bool isEnabled);

	void resized() override;
	void setRecording(bool isRecording);
	void setTimeDomain(TimeDomain newDomain, double newDomainContext);
	void toggleTypeFilter(SourceType source);
	bool showSourceType(SourceType source) const;
	void setPopupMode(bool usePopupMode);
	void setSearchTerm(const String& searchTerm, bool zoomToFirst);
	void zoomToFirstMatch(ViewItem::Ptr itemToZoomTo);
	bool setNewRoot(BaseWithManagerConnection* source, ViewItem::Ptr newRoot, bool useUndoManager);
	bool navigate(BaseWithManagerConnection* source, ViewItem::Ptr newTarget, bool useUndoManager);
	void sendRootChangeMessage(BaseWithManagerConnection* source);
	void setActiveViewer(Viewer* newViewer);

    void paint(Graphics& g) override
    {
        session.initUIThread();
    }
    
	VoiceBitMap<32> typeFilters;

	TimeDomain currentDomain;
	double domainContext;
	String lastSearch;

	WeakReference<Viewer> currentViewer;

	Array<WeakReference<BaseWithManagerConnection>> connectedComponents;

	ViewItem::Ptr totalRoot;
	ViewItem::Ptr currentRoot;
	
	DebugSession& session;
	Factory f;
	HiseShapeButton openButton, saveButton, recordButton, initButton, trimButton;

	HiseShapeButton undoButton, redoButton;
	ScopedPointer<Component> searchBar;

	UndoManager um;

	JUCE_DECLARE_WEAK_REFERENCEABLE(Manager);
};



struct DebugSession::ProfileDataSource::ViewComponents::StatisticsComponent: public Component,
																			 public BaseWithManagerConnection,
																			 public TableListBoxModel
{
	SET_GENERIC_PANEL_ID("ProfilerStatistics");

	static constexpr int Height = 200;

	enum ColumnId
	{
		Index = 1,
		Depth,
		Name,
		Num,
		Thread,
		Source,
		Exclusive,
		Duration,
		Average,
		Peak,
		numColumns
	};

	static String getColumnName(int columnId);

	struct RowData: public ReferenceCountedObject
	{
		using Ptr = ReferenceCountedObjectPtr<RowData>;

		RowData(ViewItem& i, int index_);

		void draw(Graphics& g, int columnId, Rectangle<float> rectangle, TimeDomain d, double domainContext, bool rowIsSelected);

		String name;
		int depth = 0;
		int occurrences = 0;
		double totalTime = 0.0;
		double exclusiveTime = 0.0;
		double peak = 0.0;
		ViewItem::Ptr firstItem;
		SourceType sourceType;
		ThreadIdentifier::Type threadType;
		Colour c;
		int currentRunIndex = -1;
		int index = 0;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RowData);
	};

	StatisticsComponent(DebugSession& session);

	void selectedRowsChanged(int lastSelected) override;
	void cellClicked (int rowNumber, int columnId, const MouseEvent& e) override;
	void cellDoubleClicked(int rowNumber, int columnId, const MouseEvent&) override;
	int compareElements(RowData* r1, RowData* r2) const;
	void updateSearchTerm();
	void onSearchUpdate(const String& s, bool unused) override;
	void sortOrderChanged (int newSortColumnId, bool isForwards);

	int getNumRows() override;
	bool isConnected() const;
	void setRootItem(ViewItem::Ptr newRoot, int currentRunIndex);

	void paintRowBackground (Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
	void paintCell (Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
	void resized() override;

	RowData::Ptr getRowDataForName(const String& n) const;

	void onNavigation(BaseWithManagerConnection* navigationSource, ViewItem::Ptr totalRoot, ViewItem::Ptr currentRoot) override;
	void onTimeDomainUpdate(TimeDomain newDomain, double context) override;

private:

	int currentSelection = -1;

	TimeDomain domain = TimeDomain::Absolute;
	double contextValue = 0.0;

	DebugSession& session;
	
	String searchTerm;
	ColumnId currentSortId = ColumnId::Name;
	bool sortForward = false;

	ReferenceCountedArray<RowData> rows;
	ReferenceCountedArray<RowData> sorted;

	TableHeaderLookAndFeel tlaf;
	ScrollbarFader sf;

	ViewItem::Ptr currentRoot;
	TableListBox table;

	bool ignoreSearch = false;

	JUCE_DECLARE_WEAK_REFERENCEABLE(StatisticsComponent);
};


struct DebugSession::ProfileDataSource::ViewComponents::ItemPopup: public Component
{
	ItemPopup(Viewer* p, ViewItem::Ptr item_);

	struct Factory: public PathFactory
	{
		Path createPath(const String& url) const override;
	} f;

	bool isConnected() const { return parent.get() != nullptr; }

	void paint(Graphics& g) override;
	void resized() override;
	void drawStatistics(Graphics& g, StatisticsComponent::ColumnId c, Rectangle<float> bounds);

	ReferenceCountedObjectPtr<ItemBase> attachedData;
	ScopedPointer<Component> dataComponent;

	WeakReference<Viewer> parent;
	ViewItem::Ptr item;

	ViewItem::List sources;
	ViewItem::List targets;

	StatisticsComponent::RowData::Ptr rd;
	HiseShapeButton rootButton, combineButton, gotoButton;

	friend struct ItemPopup;
	friend struct Viewer;
};






}
