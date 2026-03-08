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

/** TODO:

- add proper change log

*/

struct HiseAssetManager: public Component,
						 public QuasiModalComponent,
						 public PathFactory,
						 public Thread,
						 public AsyncUpdater,
						 public ProjectHandler::Listener,
						 public ControlledObject,
						 public Timer
{
	enum class ErrorState
	{
		Error = 0,
		Pending,
		OK
	};

	enum AssetType
	{
		Undefined   = 0,
		Installed   = 0x00000001,
		Uninstalled = 0x00000002,
		Online      = 0x00000004,
		All         = 0x00000003,
		Local       = 0x00000010,
		Webstore    = 0x00000020,
		AllSources  = 0x00000030
	};

	enum class BaseURL
	{
		Store,
		Repository
	};

	enum SpecialAction
	{
		ShowInStore = 1,
		Install,
		Uninstall,
		Cleanup,
		RemoveLocalFolder
	};

	struct Helpers
	{
		struct RequestData
		{
			String operator[](const String& d) const;
			var operator[](int index) const;

			void forEachListElement(const std::function<void(const var&)>& f);

			int statusCode = -1;
			var returnData;
		};

		static File getCredentialFile();
		static URL getSubURL(const String& subURL, BaseURL baseURL);
		static URL::DownloadTaskOptions getDownloadOptions(URL::DownloadTaskListener* l);
		static RequestData post(BaseURL base, const String& subURL, bool getRequest, const StringPairArray& parameters);
	};

	struct ActionBase: public ReferenceCountedObject
	{
		using List = ReferenceCountedArray<ActionBase>;
		using Ptr = ReferenceCountedObjectPtr<ActionBase>;

		virtual ~ActionBase() {};

		virtual bool perform(HiseAssetManager* manager) = 0;
		virtual void onCompletion(HiseAssetManager* manager) = 0;
		virtual String getMessage() const = 0;

		bool succeeded = false;

		JUCE_DECLARE_WEAK_REFERENCEABLE(ActionBase);
	};

	struct LoginTask; struct ProductFetchTask;

	struct ProductInfo: public ReferenceCountedObject
	{
		using List = ReferenceCountedArray<ProductInfo>;
		using Ptr = ReferenceCountedObjectPtr<ProductInfo>;
		struct ThumbnailTask;

		ProductInfo(const var& obj, HiseAssetManager* manager);

		String prettyName;
		String repoId;
		String vendor;
		Image image;
		String description;
		String shopURL;
	};

	ProductInfo::List products;

	struct ButtonLaf;
	struct LogoutButtonLaf;

	struct ProductList: public Component
	{
		static constexpr int NameWidth = 100;
		static constexpr int VendorWidth = 80;
		static constexpr int VersionWidth = 60;
		static constexpr int ButtonWidth = 60;
		static constexpr int RowHeight = 64 + 2*16;
		static constexpr int HeaderHeight = 24;
		static constexpr uint32 SignalColour = 0xFF39439D;

		struct TagInfo
		{
			TagInfo(const var& obj);

			TagInfo() = default;

			bool operator<(const TagInfo& other) const;
			bool operator>(const TagInfo& other) const;
			bool operator==(const TagInfo& other) const { return name == other.name; }

			String name;
			bool isSemanticVersion = false;
			Time date;
			String commitHash;
			String downloadLink;
		};

		struct RowInfo
		{
		enum class State
		{
			Uninstalled,
			UpdateAvailable,
			UpToDate,
			NeedsCleanup,
			NotOwned,
			numStates
		};

			RowInfo(const HiseAssetInstaller::UninstallInfo& info);
			RowInfo(const var& obj);
			RowInfo(ProductInfo* info);

			void setFromProductInfo(ProductInfo* info);

			bool matches(const String& searchTerm) const;
			bool matches(int at) const;

			String getPackageName() const;
			String getNameToDisplay() const;

			void setFromTagData(const TagInfo& info);
			void updateState();
			String getActionName() const;

			URL shopURL;
			HiseAssetInstaller::UninstallInfo installInfo;
			String downloadLink;

			State state = State::Uninstalled;
			bool needsCleanup = false;
			String repoPath;
			String prettyName;
			var installData;
			String description;
		};

		struct RowBase: public Component
		{
			virtual ~RowBase() = default;

			static std::unique_ptr<Drawable> createSVGfromBase64(const String& b64);

			bool paintBase(Graphics& g, Drawable* icon, bool fullOpacity);

			virtual bool matchesFlag(int filterFlag, const String& searchTerm) const = 0;
			
			Rectangle<int> imgBounds;
		};

	struct SpecialRow: public RowBase
		{
			SpecialRow();

			void mouseUp(const MouseEvent& e);

			bool matchesFlag(int filterFlag, const String& searchTerm) const override;
			void setIcon(const String& b64);
			void setText(const String& text);

			void paint(Graphics& g) override;
			void resized() override;

			std::unique_ptr<Drawable> icon;
			AttributedString helpText;
			std::function<void()> onClick;

			Rectangle<float> textBounds;
		};

		struct LoginRow: public RowBase,
						 public ButtonListener
		{
			LoginRow(HiseAssetManager* manager);

			bool matchesFlag(int filterFlag, const String& searchTerm) const override;

			void buttonClicked(Button* b) override;
			void paint(Graphics& g) override;
			void resized() override;

			HiseAssetManager* manager;
			std::unique_ptr<Drawable> icon;
			AttributedString helpText;

			TextEditor tokenEditor;
			TextButton loginButton;
			TextButton getTokenButton;
			ScopedPointer<LookAndFeel> blaf;

			Rectangle<float> textBounds;
		};

		struct Row: public RowBase,
					public ButtonListener
		{
		struct RowAction: public ActionBase
		{
			RowAction(Row* r, const String& message_) :
				rowComponent(r),
				repoPath(r->info.repoPath),
				installInfo(r->info.installInfo),
				message(message_)
			{}

			/** Returns the install info captured at construction time (safe for background threads). */
			HiseAssetInstaller::UninstallInfo getInfo() const 
			{ 
				return installInfo; 
			}

			String getMessage() const override { return message; }
			Helpers::RequestData rd;
			String repoPath;
			HiseAssetInstaller::UninstallInfo installInfo;
			Component::SafePointer<Row> rowComponent;
			const String message;
			};

		struct FetchVersion; struct FetchInfo; struct FetchChangeLog;
		struct UninstallAction; struct UpdateAction; struct CleanupAction;


			Row(const RowInfo& r, HiseAssetManager* manager);

			void buttonClicked(Button* b) override;
			bool matchesFlag(int filterFlag, const String& searchTerm) const override;

			void updateAfterTag(HiseAssetManager* m);

			void paint(Graphics& g) override;
			void resized() override;

			std::vector<TagInfo> tags;
			Path downloadIcon;
			ProductInfo::Ptr productInfo;
			RowInfo info;
			std::unique_ptr<juce::Drawable> localIcon;
			AttributedString title;
			AttributedString description;

			Rectangle<float> titleBounds;
			Rectangle<float> descriptionBounds;
			Rectangle<float> circleArea;

			TextButton infoButton;
			TextButton actionButton;
			TextButton moreButton;

		bool initialised = false;
		ScopedPointer<LookAndFeel> blaf;
		};

		struct Content: public Component
		{
			Row* addRow(const RowInfo& r, HiseAssetManager* manager);
			void updateFilter();
			void rebuild();
			void resized() override;

			String searchTerm;
			int currentAssetFilter = AssetType::All | AssetType::AllSources;
			bool anyVisible = false;
			OwnedArray<RowBase> rows;
			String title;
		};

		ProductList();

		void setProducts(const Array<var>& data, HiseAssetManager* manager);

		void paint(Graphics& g) override;
		void resized() override;

		Array<var> storeData;
		ScopedPointer<Content> content;
		Viewport vp;
		
		ScrollbarFader sf;
	};

	struct TopBar: public Component,
				   public ButtonListener
	{
		TopBar(ProductList* list_);;

		void updateFilters();
		int getAssetType() const;

		void buttonClicked(Button* b) override;
		void paint(Graphics& g) override;
		void resized() override;

		AssetType currentDisplayType = AssetType::All;
		ScopedPointer<LookAndFeel> blaf;

		Path searchIcon;
		TextButton allButton;
		TextButton installedButton;
		TextButton uninstalledButton;
		TextButton onlineButton;
		TextEditor searchBar;
		ProductList* list;
	};

	HiseAssetManager(BackendRootWindow* brw);
	~HiseAssetManager();
	
	void projectChanged(const File& newRootDirectory);
	void initialiseLocalAssets();
	void rebuildProductsAsync();
	void setProjectFolder(const File& f);
	Path createPath(const String& url) const override;

	void tryToLogin();
	void addNewAssetFolder();

	void performAction(SpecialAction a, ProductList::Row* r, bool silent=true);
	void addJob(ActionBase::Ptr newJob);
	void run() override;
	void handleAsyncUpdate() override;

	void setError(const HiseAssetInstaller::Error& e)
	{
		currentError = e;
	}

	void setState(ErrorState newState);
	void showStatusMessage(const String& newMessage);
	void showHelpPopup(Component* attachedComponent, const String& markdownText);

	void mouseDown(const MouseEvent& e) override;
	void mouseDrag(const MouseEvent& e) override;

	void timerCallback() override;
	void paint(Graphics& g) override;
	void resized() override;

	String userName = "Not logged in";
	File currentProjectRoot;
	String projectName;
	SharedResourcePointer<HiseAssetInstaller::UninstallInfo::LocalCollection> localAssets;

	ScopedPointer<TopBar> topBar;
	ScopedPointer<ProductList> productList;
	CriticalSection lock;
	ActionBase::List pendingTasks;
	ActionBase::List completedTasks;

	
	HiseAssetInstaller::Error currentError;
	ErrorState state = ErrorState::OK;
	std::array<Colour, 3> colours = { Colour(HISE_ERROR_COLOUR), Colour(HISE_WARNING_COLOUR), Colour(HISE_OK_COLOUR) };
	Rectangle<float> stateIcon;
	bool running = false;
	String currentMessage;

	bool loginOk = false;

	juce::ComponentBoundsConstrainer restrainer;
	juce::ComponentDragger dragger;
	juce::ResizableCornerComponent resizer;
	HiseShapeButton closeButton;
	TextButton logoutButton;
	ScopedPointer<LookAndFeel> logoutLaf;
	
	Rectangle<float> projectFolderBounds;
	AttributedString projectInfo;
};

}
