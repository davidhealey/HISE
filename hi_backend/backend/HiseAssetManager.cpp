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

struct HiseAssetManager::ButtonLaf : public LookAndFeel_V4
{
	void drawButtonBackground(Graphics& g, Button& button, const Colour& backgroundColour, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
	{
		Rectangle<float> b = button.getLocalBounds().toFloat().reduced(1.0f);

		Path p;
		p.addRoundedRectangle(b.getX(), b.getY(), b.getWidth(), b.getHeight(), 3.0f, 3.0f,
			!button.isConnectedOnLeft(), !button.isConnectedOnRight(),
			!button.isConnectedOnLeft(), !button.isConnectedOnRight());

		float alpha = 0.8f;
		if (shouldDrawButtonAsDown)
			alpha += 0.1f;
		if (shouldDrawButtonAsHighlighted)
			alpha += 0.1f;

		g.setColour(backgroundColour.withAlpha(alpha));
		g.fillPath(p);
	}

	void drawButtonText(Graphics& g, TextButton& tb, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
	{
		float alpha = 0.8f;
		if (shouldDrawButtonAsHighlighted)
			alpha += 0.1f;
		if (shouldDrawButtonAsDown)
			alpha += 0.1f;

		g.setColour(Colours::white.withAlpha(alpha));

		String x = tb.getButtonText();

		Path p;

		auto b = tb.getLocalBounds().toFloat();

		if (x == "info")
		{
			static const unsigned char pathData[] = { 110,109,80,178,38,68,0,0,158,66,98,144,243,48,68,0,0,158,66,208,16,54,68,192,12,190,66,208,16,54,68,128,43,254,66,98,208,16,54,68,32,37,31,67,144,243,48,68,128,43,47,67,80,178,38,68,128,43,47,67,98,112,116,28,68,128,43,47,67,208,83,23,68,32,37,31,67,
208,83,23,68,128,43,254,66,98,208,83,23,68,192,12,190,66,112,116,28,68,0,0,158,66,80,178,38,68,0,0,158,66,99,109,160,173,24,68,48,159,154,67,98,160,173,24,68,144,91,146,67,0,208,23,68,64,173,140,67,144,17,22,68,160,147,137,67,98,0,83,20,68,16,122,134,
67,0,52,17,68,144,236,132,67,48,177,12,68,144,236,132,67,108,224,68,13,68,224,163,110,67,98,144,242,14,68,160,62,109,67,160,111,18,68,192,235,106,67,144,184,23,68,224,169,103,67,98,192,55,36,68,224,175,96,67,48,136,44,68,64,35,91,67,96,166,48,68,192,
3,87,67,108,0,241,51,68,224,50,93,67,108,0,241,51,68,64,212,253,67,98,208,42,55,68,16,23,1,68,240,72,59,68,224,162,2,68,192,78,64,68,56,141,3,68,108,0,140,63,68,0,64,10,68,108,0,220,13,68,0,64,10,68,108,0,162,14,68,64,53,3,68,98,176,225,18,68,32,104,
2,68,32,61,22,68,224,249,0,68,160,173,24,68,64,212,253,67,108,160,173,24,68,48,159,154,67,99,101,0,0 };

			p.loadPathFromData(pathData, sizeof(pathData));
			auto tr = b.withSizeKeepingCentre(14, 14);
			PathFactory::scalePath(p, tr);
		}
		if (x == "more")
		{
			auto tr = b.withSizeKeepingCentre(10, 8);
			p.addTriangle(tr.getTopLeft(), tr.getTopRight(), { tr.getCentreX(), tr.getBottom() });

		}

		if (!p.isEmpty())
		{
			g.fillPath(p);
			return;
		}

		g.setFont(GLOBAL_BOLD_FONT());
		g.drawText(x, b, Justification::centred);
	}
};


juce::CodeEditorComponent::ColourScheme DiffViewer::FileDiff::DiffTokeniser::getDefaultColourScheme()
{
	CodeEditorComponent::ColourScheme cs;
	cs.set("Header", Colours::white);
	cs.set("Context", Colours::grey);
	cs.set("Add", Colour(HISE_OK_COLOUR));
	cs.set("Remove", Colour(HISE_ERROR_COLOUR));
	return cs;
}

int DiffViewer::FileDiff::DiffTokeniser::readNextToken(CodeDocument::Iterator& source)
{
	auto c = source.nextChar();

	switch (c)
	{
	case '@':
		source.skipToEndOfLine();
		return (int)LineType::Header;
	case '+':
		source.skipToEndOfLine();
		return (int)LineType::Add;
	case '-':
		source.skipToEndOfLine();
		return (int)LineType::Remove;
	default:
		source.skipToEndOfLine();
		return(int)LineType::Context;
	}
}

void DiffViewer::addDiff(const File& f, const LineDiff::HashedDiff::Ptr diffReport)
{
	auto nd = new FileDiff(rootDir, f, diffReport);
	diffs.add(nd);
	content.addAndMakeVisible(nd);

	updateBounds();
}

DiffViewer::DiffViewer(const File& rootDir_):
  applyButton("Save"),
  cancelButton("Close"),
  rootDir(rootDir_),
  laf(new HiseAssetManager::ButtonLaf())
{
	jassert(rootDir.isDirectory());
	addAndMakeVisible(applyButton);
	addAndMakeVisible(cancelButton);

	applyButton.setLookAndFeel(laf);
	cancelButton.setLookAndFeel(laf);

	applyButton.setConnectedEdges(Button::ConnectedEdgeFlags::ConnectedOnLeft);
	cancelButton.setConnectedEdges(Button::ConnectedEdgeFlags::ConnectedOnRight);

	cancelButton.setTooltip("Close the diff viewer");
	applyButton.setTooltip("Save the changes to the edited file");

	applyButton.setEnabled(false);

	cancelButton.setColour(TextButton::ColourIds::buttonColourId, Colour(0xFF666666));
	applyButton.setColour(TextButton::ColourIds::buttonColourId, Colour(HiseAssetManager::ProductList::SignalColour));

	addAndMakeVisible(vp);
	vp.setViewedComponent(&content, false);
	sf.addScrollBarToAnimate(vp.getVerticalScrollBar());
	vp.setScrollBarThickness(12);

	applyButton.onClick = [this]()
	{
		if(currentCodeDoc != nullptr)
			updateReport(currentReport, currentCodeDoc->getAllContent());
	};

	cancelButton.onClick = [this]()
	{
		auto am = findParentComponentOfClass<HiseAssetManager>();
		
		am->showStatusMessage("");
		am->setState(HiseAssetManager::ErrorState::OK);
		am->setModalComponent(nullptr);
	};

	checkSaveButton();
}

void DiffViewer::paint(Graphics& g)
{
	g.fillAll(Colour(0xFF222222));

	if(editedFile.existsAsFile())
	{
		g.fillAll(Colour(0xFF222222));
		g.setColour(Colours::white.withAlpha(0.5f));
		auto b = getLocalBounds();
		
		auto rb = b.removeFromRight(b.getWidth() / 2);
		auto tb = rb.removeFromTop(TopHeight).toFloat().reduced(4.0f, 0.0f);

 		g.setFont(GLOBAL_BOLD_FONT());
		String text;
		text << editedFile.getRelativePathFrom(rootDir);

		if(isShowingOriginal)
			text << " (Original Version)";

		g.drawText(text, tb, Justification::left);
	}

	{
		auto b = getLocalBounds();

		auto lb = b.removeFromLeft(b.getWidth() / 2).toFloat();
		auto rb = b.toFloat();

		g.setFont(GLOBAL_FONT());
		g.setColour(Colours::white.withAlpha(0.2f));

		if(diffs.isEmpty())
		{
			g.drawText("No changes", lb, Justification::centred);
		}
		if(currentEditor == nullptr)
		{
			g.drawText("Select a change to edit the file", rb, Justification::centred);
		}
	}


}

void DiffViewer::resized()
{
	auto b = getLocalBounds();

	cancelButton.setVisible(true);
	applyButton.setVisible(true);
	auto rb = b.removeFromRight(b.getWidth() / 2);
	auto bottom = rb.removeFromTop(TopHeight);

	applyButton.setBounds(bottom.removeFromRight(100).reduced(0, 2));
	cancelButton.setBounds(bottom.removeFromRight(100).reduced(0, 2));

	if(currentEditor != nullptr)
		currentEditor->setBounds(rb);
	
	vp.setBounds(b);


	auto w = b.getWidth() - vp.getScrollBarThickness();
	content.setSize(w, content.getHeight());
	updateBounds();
}

void DiffViewer::updateBounds()
{
	if (content.getWidth() == 0 || diffs.isEmpty())
		return;

	int h = 0;

	for (auto d : diffs)
	{
		auto th = d->getHeight();
		d->setBounds(0, h, content.getWidth(), th);
		h += th;
	}

	content.setSize(content.getWidth(), h);
}

void DiffViewer::checkSaveButton()
{
	if (currentReport != nullptr)
	{
		auto currentFileContent = currentReport->getContent(false);
		auto currentEditorContent = currentCodeDoc->getAllContent();

		auto changes = currentFileContent.hashCode64() != currentEditorContent.hashCode64();

		if (changes)
		{
			applyButton.setColour(TextButton::ColourIds::buttonColourId, Colour(HiseAssetManager::ProductList::SignalColour));
			applyButton.setEnabled(true);
			return;
		}
	}

	applyButton.setColour(TextButton::ColourIds::buttonColourId, Colour(0xFF444444));
	applyButton.setEnabled(false);
}

void DiffViewer::setEditedFile(const File& f, const LineDiff::HashedDiff::Ptr p, bool showOld)
{
	if(currentCodeDoc != nullptr)
		currentCodeDoc->removeListener(this);

	isShowingOriginal = showOld;
	editedFile = f;
	currentReport = p;
	currentEditor = nullptr;
	currentTextDoc = nullptr;
	currentCodeDoc = nullptr;

	if(p != nullptr)
	{
		auto content = p->getContent(showOld);

		currentCodeDoc = new CodeDocument();

		currentCodeDoc->replaceAllContent(content);
		currentTextDoc = new mcl::TextDocument(*currentCodeDoc);
		currentEditor = new mcl::TextEditor(*currentTextDoc);

		if(showOld)
			currentEditor->setReadOnly(true);
		else
			setEditorChanges(p->createHunks());

		if (f.getFileExtension() == ".xml")
			currentEditor->setLanguageManager(new mcl::XmlLanguageManager());
		if (f.getFileExtension() == ".dsp")
			currentEditor->setLanguageManager(new mcl::FaustLanguageManager());
		if (f.getFileExtension() == ".md")
			currentEditor->setLanguageManager(new mcl::MarkdownLanguageManager());

		currentCodeDoc->addListener(this);

		addAndMakeVisible(currentEditor);
	}

	checkSaveButton();
	resized();
}


DiffViewer::FileDiff::FileDiff(const File& rootDir_, const File& file, const LineDiff::HashedDiff::Ptr diff_) :
	editedFile(file),
	revertButton("View Original"),
	editButton("Edit"),
	moreButton("more"),
    diff(diff_),
    rootDir(rootDir_)
{
	jassert(rootDir.isDirectory());
	laf = new HiseAssetManager::ButtonLaf();

	revertButton.setLookAndFeel(laf);
	editButton.setLookAndFeel(laf);
	moreButton.setLookAndFeel(laf);

	revertButton.setConnectedEdges(Button::ConnectedOnRight);
	editButton.setConnectedEdges(Button::ConnectedOnRight | Button::ConnectedOnLeft);
	moreButton.setConnectedEdges(Button::ConnectedOnLeft);

	revertButton.setColour(TextButton::ColourIds::buttonColourId, Colour(0xFF666666));
	editButton.setColour(TextButton::ColourIds::buttonColourId, Colour(0xFF666666));
	moreButton.setColour(TextButton::ColourIds::buttonColourId, Colour(HiseAssetManager::ProductList::SignalColour));

	int h = 0;

	auto report = diff->getPatchReport();

	for(const auto& c: diff->createHunks())
	{
		addAndMakeVisible(changes.add(new ChangeEditor(c)));
		h += changes.getLast()->getHeight();
	}

	addAndMakeVisible(revertButton);
	addAndMakeVisible(editButton);
	addAndMakeVisible(moreButton);

	editButton.onClick = [this]()
	{
		findParentComponentOfClass<DiffViewer>()->setEditedFile(this->editedFile, this->diff, false);
	};

	revertButton.onClick = [this]()
	{
		findParentComponentOfClass<DiffViewer>()->setEditedFile(this->editedFile, this->diff, true);
	};

	moreButton.onClick = [this]()
	{
		PopupLookAndFeel plaf;
		PopupMenu m;

		m.addItem(1, "Show file");
		m.addItem(2, "Revert file");

		auto r = m.show();
	};

	setSize(400, h);
}

void DiffViewer::FileDiff::paint(Graphics& g)
{
	g.fillAll(Colour(0xFF222222));
	g.setColour(Colours::white.withAlpha(0.5f));
	auto b = getLocalBounds().removeFromTop(TopHeight).toFloat().reduced(4.0f, 0.0f);

	g.setFont(GLOBAL_BOLD_FONT());
	g.drawText(editedFile.getRelativePathFrom(rootDir), b, Justification::left);
}

void DiffViewer::FileDiff::resized()
{
	auto b = getLocalBounds();
	auto top = b.removeFromTop(TopHeight).reduced(0, 2);

	moreButton.setBounds(top.removeFromRight(top.getHeight()));
	editButton.setBounds(top.removeFromRight(100));
	revertButton.setBounds(top.removeFromRight(100));

	for(auto c: changes)
	{
		c->setBounds(b.removeFromTop(c->getHeight()));
	}
}


DiffViewer::FileDiff::ChangeEditor::ChangeEditor(const LineDiff::Change& c) :
	editor(doc, &tokeniser),
	change(c),
	discardButton("Discard"),
	laf(new HiseAssetManager::ButtonLaf())
{

	addAndMakeVisible(editor);
	addAndMakeVisible(discardButton);
	discardButton.setLookAndFeel(laf);
	discardButton.setColour(TextButton::ColourIds::buttonColourId, Colour(0xFF555555));

	editor.setReadOnly(true);
	doc.setDisableUndo(true);
	doc.replaceAllContent(c.toUnifiedPatchFormat().joinIntoString("\n"));
	auto f = GLOBAL_MONOSPACE_FONT();
	editor.setFont(f);
	editor.setColourScheme(tokeniser.getDefaultColourScheme());

	editor.setColour(CodeEditorComponent::backgroundColourId, Colour(0xff262626));
	editor.setColour(CodeEditorComponent::ColourIds::defaultTextColourId, Colour(0xFFCCCCCC));
	editor.setColour(CodeEditorComponent::ColourIds::lineNumberTextId, Colour(0xFFCCCCCC));
	editor.setColour(CodeEditorComponent::ColourIds::lineNumberBackgroundId, Colour(0xff363636));
	editor.setColour(CodeEditorComponent::ColourIds::highlightColourId, Colour(0xff666666));
	editor.setColour(CaretComponent::ColourIds::caretColourId, Colour(0xFFDDDDDD));
	editor.setColour(ScrollBar::ColourIds::thumbColourId, Colour(0x3dffffff));
	editor.setLineNumberOffset(c.lines[0].newLine - 1);

	editor.addMouseListener(this, true);

	discardButton.onClick = [this]()
	{
		if (PresetHandler::showYesNoWindow("Discard change", "Press OK to discard the hunk"))
		{
			auto fd = findParentComponentOfClass<FileDiff>();

			auto newContent = StringArray::fromLines(fd->diff->getContent(false));

			std::vector<LineDiff::Change> invertChange;
			invertChange.push_back(change.invert());

			LineDiff::HashedDiff::Ptr revertPatch = new LineDiff::HashedDiff(newContent, invertChange);

			auto discardedContent = revertPatch->getContent(false);
			auto parent = findParentComponentOfClass<DiffViewer>();

			parent->updateReport(fd->diff, discardedContent);
		}
	};

	auto h = editor.getLineHeight() * (doc.getNumLines() + 3);
	setSize(400, h);
}

void DiffViewer::FileDiff::ChangeEditor::mouseDown(const MouseEvent& e)
{
	if (e.eventComponent == &editor)
	{
		auto fd = findParentComponentOfClass<FileDiff>();
		auto dv = findParentComponentOfClass<DiffViewer>();

		if (fd->editedFile == dv->editedFile)
		{
			if (auto currentEditor = dv->currentEditor.get())
			{
				auto firstLine = change.lines[0];
				auto lineNumber = dv->isShowingOriginal ? firstLine.oldLine : firstLine.newLine;
				currentEditor->scrollToLine(lineNumber, true);
			}
		}
	}
}

struct HiseAssetManager::LoginTask : public ActionBase
{
	String getMessage() const override { return "Login & fetch data"; }

	bool perform(HiseAssetManager* manager) override
	{
		manager->showStatusMessage("Fetch user data");

		rd = Helpers::post(BaseURL::Repository, "api/v1/user", true, {});

		if (rd.statusCode == 200)
		{
			manager->userName = "Logged in as " + rd.returnData["email"].toString();
			rd = Helpers::post(BaseURL::Repository, "api/v1/user/repos", true, {});
		}

		return true;
	}

	void onCompletion(HiseAssetManager* manager) override
	{
		if (rd.statusCode == 401)
		{
			manager->loginOk = false;
			Helpers::getCredentialFile().deleteFile();
			manager->setState(ErrorState::OK);
			manager->showStatusMessage("The authentication token was not valid. Please retry...");
			manager->productList->setProducts({}, manager);
		}

		if (rd.statusCode == 200)
		{
			manager->loginOk = true;
			manager->repaint();

			if (auto l = rd.returnData.getArray())
			{
				manager->productList->setProducts(*l, manager);
			}
		}
	}

	Helpers::RequestData rd;
};




juce::var HiseAssetManager::Helpers::RequestData::operator[](int index) const
{
	if (auto v = returnData.getArray())
	{
		if (isPositiveAndBelow(index, v->size()))
			return v->getUnchecked(index);
	}

	return var();
}

String HiseAssetManager::Helpers::RequestData::operator[](const String& d) const
{
	return returnData[Identifier(d)].toString();
}

void HiseAssetManager::Helpers::RequestData::dump()
{
	DBG(JSON::toString(returnData));
}

void HiseAssetManager::Helpers::RequestData::forEachListElement(const std::function<void(const var&)>& f)
{
	if (statusCode == 200)
	{
		if (auto v = returnData.getArray())
		{
			for (const auto& c : *v)
				f(c);
		}
	}
}

juce::File HiseAssetManager::Helpers::getCredentialFile()
{
	return ProjectHandler::getAppDataDirectory(nullptr).getChildFile("storeToken.dat");
}

juce::URL HiseAssetManager::Helpers::getSubURL(const String& subURL, BaseURL baseURL)
{
	auto storeAsBase = baseURL == BaseURL::Store;
	URL base(storeAsBase ? "https://store.hise.dev" : "https://git.hise.dev");
	return base.getChildURL(subURL);
}

juce::URL::DownloadTaskOptions HiseAssetManager::Helpers::getDownloadOptions(URL::DownloadTaskListener* l)
{
	URL::DownloadTaskOptions options;
	auto token = getCredentialFile().loadFileAsString();
	options.extraHeaders = "Authorization: Bearer " + token;
	options.listener = l;
	return options;
}

hise::HiseAssetManager::Helpers::RequestData HiseAssetManager::Helpers::post(BaseURL base, const String& subURL, bool getRequest, const StringPairArray& parameters)
{
	auto url = getSubURL(subURL, base).withParameters(parameters);

	URL::InputStreamOptions options(getRequest ? URL::ParameterHandling::inAddress :
		URL::ParameterHandling::inPostData);

	RequestData d;

	String headers;

	if(base == BaseURL::Repository)
	{
		auto token = getCredentialFile().loadFileAsString();

		if (token.isEmpty())
			return { 401, "no token provided" };

		headers = "Authorization: Bearer " + token;
	}

	auto stream = url.createInputStream(options.withStatusCode(&d.statusCode).withExtraHeaders(headers));

	if (stream != nullptr)
	{
		auto data = stream->readEntireStreamAsString();
		auto r = Result::ok();
		auto ok = JSON::parse(data, d.returnData);

		if (!ok.wasOk())
		{
			d.returnData = data;
		}
	}

	return d;
}

struct HiseAssetManager::ProductInfo::ThumbnailTask : public ActionBase
{
	ThumbnailTask(ProductInfo* info_, const String& url_) :
		url(url_),
		info(info_)
	{
	}

	String getMessage() const override { return "Fetch thumbnail..."; }

	bool perform(HiseAssetManager* manager) override
	{
		TemporaryFile tf;

		URL::DownloadTaskOptions options;
		auto task = url.downloadToFile(tf.getFile(), options);

		while (!task->isFinished())
		{
			manager->wait(100);
		}

		if (!task->hadError())
			downloadedImage = ImageFileFormat::loadFrom(tf.getFile());

		return true;
	}

	void onCompletion(HiseAssetManager* manager) override
	{
		manager->showStatusMessage("OK");
		if (downloadedImage.isValid())
		{
			info->image = downloadedImage;
		}
	}

	ProductInfo::Ptr info;
	URL url;
	Image downloadedImage;
};

HiseAssetManager::ProductInfo::ProductInfo(const var& obj, HiseAssetManager* manager) :
	prettyName(obj["product_name"].toString()),
	description(obj["product_short_description"].toString()),
	shopURL(obj["path"].toString())
{
	auto repoPath = URL(obj["repo_link"].toString()).getSubPath();
	auto tokens = StringArray::fromTokens(repoPath, "/", "");

	vendor = tokens[0].trim();
	repoId = tokens[1].trim();

	manager->addJob(new ThumbnailTask(this, obj["thumbnail"].toString()));
}

struct HiseAssetManager::ProductFetchTask : public ActionBase
{
	String getMessage() const override { return "Fetch products from store"; }

	bool perform(HiseAssetManager*) override
	{
		auto rd = Helpers::post(BaseURL::Store, "api/products/", true, {});

		if(rd.statusCode == 200)
			obj = rd.returnData;

		return true;

#if 0
		URL url("https://store.hise.dev/api/products/");
		auto x = url.readEntireTextStream(false);

		if (x.isNotEmpty())
		{
			auto ok = JSON::parse(x, obj);

			if (ok.wasOk())
			{
				return true;
			}
		}
#endif

		return false;
	}

	var obj;

	void onCompletion(HiseAssetManager* manager) override
	{
		manager->products.clear();

		if (auto ar = obj.getArray())
		{
			for (auto& v : *ar)
			{
				manager->products.add(new ProductInfo(v, manager));
			}
		}

		manager->initialiseLocalAssets();
		manager->tryToLogin();
	}
};



HiseAssetManager::ProductList::TagInfo::TagInfo(const var& obj) :
	name(obj["name"].toString()),
	isSemanticVersion(SemanticVersionChecker(name, name).oldVersionNumberIsValid()),
	date(Time::fromISO8601(obj["commit"]["created"].toString())),
	commitHash(obj["commit"]["sha"].toString()),
	downloadLink(obj["zipball_url"].toString())
{
	DBG(JSON::toString(obj));
}

bool HiseAssetManager::ProductList::TagInfo::operator<(const TagInfo& other) const
{
	if (isSemanticVersion && other.isSemanticVersion)
	{
		return !SemanticVersionChecker(other.name, name).isUpdate();
	}

	return other.name.compare(name) < 0;
}

bool HiseAssetManager::ProductList::TagInfo::operator>(const TagInfo& other) const
{
	return !(*this < other);
}

HiseAssetManager::ProductList::RowInfo::RowInfo(const HiseAssetInstaller::UninstallInfo& info) :
	installInfo(info)
{

}

HiseAssetManager::ProductList::RowInfo::RowInfo(const var& obj) :
	installInfo(obj),
	repoPath(obj["url"].toString().fromFirstOccurrenceOf("https://git.hise.dev/", false, false))
{

}

HiseAssetManager::ProductList::RowInfo::RowInfo(ProductInfo* info)
{
	setFromProductInfo(info);
	state = State::NotOwned;
}

void HiseAssetManager::ProductList::RowInfo::setFromProductInfo(ProductInfo* info)
{
	if (info != nullptr)
	{
		installInfo.packageName = info->repoId;
		installInfo.vendor = info->vendor;
		description = info->description;
		prettyName = info->prettyName;
		shopURL = URL("https://store.hise.dev").getChildURL(info->shopURL);
	}
}

bool HiseAssetManager::ProductList::RowInfo::matches(const String& searchTerm) const
{
	return searchTerm.isEmpty() || installInfo.packageName.toLowerCase().contains(searchTerm.toLowerCase());
}

bool HiseAssetManager::ProductList::RowInfo::matches(int at) const
{
	auto installed = installInfo.isInstalled();

	auto installTypeMatches = true;

	if (state == State::NotOwned)
	{
		installTypeMatches &= (at & AssetType::Online) != 0;
	}
	else
	{
		if (installed)
			installTypeMatches &= (at & AssetType::Installed) != 0;
		else
			installTypeMatches &= (at & AssetType::Uninstalled) != 0;
	}

	auto isLocal = installInfo.isLocalFolder();

	auto sourceMatches = ((at & AssetType::AllSources) == AssetType::AllSources) ||
		(isLocal && ((at & AssetType::Local) != 0)) ||
		(!isLocal && ((at & AssetType::Webstore) != 0));

	return installTypeMatches && sourceMatches;
}

String HiseAssetManager::ProductList::RowInfo::getPackageName() const
{
	return installInfo.packageName;
}

String HiseAssetManager::ProductList::RowInfo::getNameToDisplay() const
{
	if (prettyName.isNotEmpty())
		return prettyName;

	return getPackageName();
}

void HiseAssetManager::ProductList::RowInfo::setFromTagData(const TagInfo& info)
{
	if (!info.isSemanticVersion)
		return;

	installInfo.availableVersion = info.name;

	downloadLink = info.downloadLink;

	updateState();
}

void HiseAssetManager::ProductList::RowInfo::updateState()
{
	if (state == State::NotOwned)
		return;

	if (!installInfo.isInstalled())
	{
		state = State::Uninstalled;
	}
	else if (installInfo.isUpdateAvailable())
	{
		state = State::UpdateAvailable;
	}
	else
	{
		state = State::UpToDate;
	}
}

String HiseAssetManager::ProductList::RowInfo::getActionName() const
{
	switch (state)
	{
	case State::Uninstalled:
		return "Install " + installInfo.availableVersion;
	case State::UpdateAvailable:
		return "Update to " + installInfo.availableVersion;
	case State::UpToDate:
		return "Uninstall " + installInfo.installedVersion;
	case State::NotOwned:
		return "View in HISE store";
	case State::numStates:
		break;
	default:
		break;
	}

	jassertfalse;
	return "Undefined";
}

std::unique_ptr<juce::Drawable> HiseAssetManager::ProductList::RowBase::createSVGfromBase64(const String& b64)
{
	MemoryBlock mb;
	mb.fromBase64Encoding(b64);
	zstd::ZDefaultCompressor comp;
	String text;
	comp.expand(mb, text);

	if (auto xml = XmlDocument::parse(text))
		return juce::Drawable::createFromSVG(*xml);

	return nullptr;
}

bool HiseAssetManager::ProductList::RowBase::paintBase(Graphics& g, Drawable* icon, bool fullOpacity)
{
	g.setColour(Colours::white.withAlpha(fullOpacity ? 0.08f : 0.05f));
	g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 3.0f);

	if (icon != nullptr)
	{
		icon->drawWithin(g, imgBounds.toFloat(), RectanglePlacement::centred, 1.0f);
		return true;
	}

	return false;
}

HiseAssetManager::ProductList::SpecialRow::SpecialRow()
{
	setMouseCursor(MouseCursor::PointingHandCursor);
	setRepaintsOnMouseActivity(true);
}

void HiseAssetManager::ProductList::SpecialRow::mouseUp(const MouseEvent& e)
{
	if (onClick)
		onClick();
}

bool HiseAssetManager::ProductList::SpecialRow::matchesFlag(int filterFlag, const String& searchTerm) const
{
	if (filterFlag & AssetType::Online)
		return false;

	if (((filterFlag & AssetType::Installed) != 0) &&
		((filterFlag & AssetType::Uninstalled) == 0))
		return false;

	return searchTerm.isEmpty();
}

void HiseAssetManager::ProductList::SpecialRow::setIcon(const String& b64)
{
	icon = createSVGfromBase64(b64);
}

void HiseAssetManager::ProductList::SpecialRow::setText(const String& text)
{
	helpText.append(text, GLOBAL_FONT(), Colours::white.withAlpha(0.6f));
	helpText.setJustification(Justification::centred);
}

void HiseAssetManager::ProductList::SpecialRow::paint(Graphics& g)
{
	paintBase(g, icon.get(), isMouseOver());
	helpText.draw(g, textBounds);
}

void HiseAssetManager::ProductList::SpecialRow::resized()
{
	auto b = getLocalBounds();
	b.removeFromLeft(10);
	b.removeFromLeft(3);
	b.removeFromRight(10);

	b.removeFromRight(5);
	imgBounds = b.removeFromLeft(b.getHeight()).reduced(16);
	b.removeFromLeft(10);
	textBounds = b.toFloat();
}


struct HiseAssetManager::ProductList::Row::FetchVersion : public RowAction
{
	FetchVersion(Row* r) : RowAction(r, "Fetch version...") {};

	bool perform(HiseAssetManager*) override
	{
		rd = Helpers::post(BaseURL::Repository, repoPath + "/tags", true, {});
		return true;
	}

	void onCompletion(HiseAssetManager* manager) override
	{
		if (rowComponent.getComponent() != nullptr)
		{
			rd.forEachListElement([&](const var& obj)
				{
					rowComponent->tags.push_back({ obj });
				});

			std::sort(rowComponent->tags.begin(), rowComponent->tags.end());

			rowComponent->updateAfterTag(manager);
		}
	}
};

struct HiseAssetManager::ProductList::Row::FetchInfo : public RowAction
{
	FetchInfo(Row* r) : RowAction(r, "Fetch readme...") {};

	bool perform(HiseAssetManager*) override
	{
		rd = Helpers::post(BaseURL::Repository, repoPath + "/raw/Readme.md", true, {});
		return true;
	}

	void onCompletion(HiseAssetManager* manager) override
	{
		manager->showHelpPopup(&rowComponent.getComponent()->infoButton, rd.returnData.toString());
	}

	String getMessage() const override { return "Fetch README.md"; }
};

struct HiseAssetManager::ProductList::Row::FetchChangeLog : public RowAction
{
	struct Commit
	{
		Commit(const var& obj) :
			date(Time::fromISO8601(obj["created"].toString())),
			message(obj["commit"]["message"].toString().trim()),
			sha(obj["sha"].toString())
		{}

		bool isInternal() const { return message.contains("[internal]"); }

		Time date;
		String message;
		String sha;
	};

	FetchChangeLog(Row* r) : RowAction(r, "Fetch changelog") {};

	bool perform(HiseAssetManager*) override
	{
		rd = Helpers::post(BaseURL::Repository, repoPath + "/commits", true, {});
		return true;
	}

	void onCompletion(HiseAssetManager* manager) override
	{
		std::vector<Commit> commits;

		rd.forEachListElement([&](const var& obj)
			{
				commits.push_back({ obj });
			});

		auto info = rowComponent->info.installInfo;

		String msg;

		msg << "### Changelog for " << info.packageName << " " << info.availableVersion;
		msg << "\n\n";

		for (const auto& c : commits)
		{
			msg << "- " << c.message << "\n";
		}

		manager->showHelpPopup(&rowComponent.getComponent()->infoButton, msg);
	}
};

struct HiseAssetManager::ProductList::Row::UninstallAction : public RowAction
{
	UninstallAction(Row* r, bool silent_) :
		RowAction(r, "Uninstall " + r->info.getPackageName() + " " + r->info.installInfo.installedVersion),
		silent(silent_)
	{};

	bool perform(HiseAssetManager* manager) override
	{
		auto info = rowComponent.getComponent()->info.installInfo;

		try
		{
			HiseAssetInstaller installer(manager->getMainController(), info);

			if (manager->currentError.info.packageName == info.packageName)
			{
			}

			return installer.uninstall();
		}
		catch(HiseAssetInstaller::Error& e)
		{
			e.info = info;
			manager->setError(e);
		}
	}

	void onCompletion(HiseAssetManager* manager) override
	{
		if (!silent)
		{
			manager->showStatusMessage("Deinstalled " + getInfo().packageName);
		}

		manager->rebuildProductsAsync();
	}

	bool silent = false;
};

struct HiseAssetManager::ProductList::Row::UpdateAction : public RowAction,
														  public URL::DownloadTaskListener
{
	enum class State
	{
		Idle,
		Downloading,
		Success,
		Fail
	};

	std::atomic<State> currentState { State::Idle };

	UpdateAction(Row* r, TagInfo ti) :
		RowAction(r, "Update " + r->info.getPackageName()),
		tf("hise_store", 0),
		tag(ti)
	{
		if (getInfo().isLocalFolder())
			tag = TagInfo();
	};

	bool perform(HiseAssetManager* manager) override
	{
		currentManager = manager;

		if (!getInfo().isLocalFolder())
		{
			auto link = tag.downloadLink;
			URL u(link);

			manager->showStatusMessage("Downloading...");

			currentTask = u.downloadToFile(tf.getFile(), Helpers::getDownloadOptions(this));

			currentState.store(State::Downloading);

			while (currentState.load() == State::Downloading)
			{
				Thread::getCurrentThread()->wait(500);
			}

			if (currentState == State::Success)
			{
				manager->showStatusMessage("Extracting...");

				ScopedPointer<ZipFile> zf = new ZipFile(tf.getFile());
				HiseAssetInstaller installer(currentManager->getMainController(), zf.get());
				return installer.install(getInfo());
			}
		}
		else
		{
			HiseAssetInstaller installer(currentManager->getMainController(), getInfo());
			auto ok = installer.install(getInfo());

			if(!ok)
			{
				manager->setState(ErrorState::Error);

			}

			return ok;
		}

		return true;
	}

	void finished(URL::DownloadTask* task, bool success) override
	{
		if (currentTask.get() == task)
		{

			int x = 5;
		}

		currentState = success ? State::Success : State::Fail;
	}

	void progress(URL::DownloadTask* task, int64 bytesDownloaded, int64 totalLength) override
	{
		if (currentTask.get() == task)
		{
			int x = 5;
		}
	}

	void onCompletion(HiseAssetManager* manager) override
	{
		manager->showStatusMessage("Installed " + getInfo().packageName);

		manager->rebuildProductsAsync();
	}

	bool isDone = false;
	TemporaryFile tf;
	TagInfo tag;
	HiseAssetManager* currentManager = nullptr;
	std::unique_ptr<URL::DownloadTask> currentTask;
};


HiseAssetManager::ProductList::Row::Row(const RowInfo& r, HiseAssetManager* manager) :
	info(r),
	infoButton("info"),
	actionButton("action"),
	moreButton("more"),
    blaf(new ButtonLaf())
{
	downloadIcon = manager->createPath("download");

	infoButton.setConnectedEdges(Button::ConnectedOnRight);
	actionButton.setConnectedEdges(Button::ConnectedOnRight | Button::ConnectedOnLeft);
	moreButton.setConnectedEdges(Button::ConnectedOnLeft);

	infoButton.setColour(TextButton::ColourIds::buttonColourId, Colour(0xFF666666));
	moreButton.setColour(TextButton::ColourIds::buttonColourId, Colour(SignalColour));
	actionButton.setColour(TextButton::ColourIds::buttonColourId, Colour(0xFF666666));

	addAndMakeVisible(infoButton);
	addAndMakeVisible(actionButton);
	addAndMakeVisible(moreButton);

	infoButton.addListener(this);
	actionButton.addListener(this);
	moreButton.addListener(this);

	infoButton.setLookAndFeel(blaf);
	actionButton.setLookAndFeel(blaf);
	moreButton.setLookAndFeel(blaf);

	manager->addJob(new FetchVersion(this));
}

void HiseAssetManager::ProductList::Row::buttonClicked(Button* b)
{
	auto manager = findParentComponentOfClass<HiseAssetManager>();

	if (b == &infoButton)
	{
		manager->addJob(new FetchChangeLog(this));
	}
	if(b == &actionButton)
	{
		if (info.state == RowInfo::State::NotOwned)
		{
			manager->performAction(SpecialAction::ShowInStore, this, true);
			return;
		}
			

		if(info.installInfo.isInstalled())
		{ 
			manager->performAction(SpecialAction::Uninstall, this, false);
			return;
		}
		else
		{
			manager->performAction(SpecialAction::Install, this, false);
		}
	}

	if (b == &moreButton)
	{
		auto localFolder = info.installInfo.isLocalFolder();

		PopupLookAndFeel laf;
		PopupMenu m;
		m.setLookAndFeel(&laf);
		m.addItem(1 + SpecialAction::Install, "Install latest version", info.installInfo.isUpdateAvailable(), false);
		m.addItem(1 + SpecialAction::Uninstall, "Uninstall from project", info.installInfo.isInstalled(), false);
		m.addItem(1 + SpecialAction::CheckLocalChanges, "Check local changes", hasLocalChanges, false);

		m.addItem(1 + SpecialAction::ShowInStore, localFolder ? "Show folder" : "Show in store", true, false);
		m.addItem(1 + SpecialAction::RemoveLocalFolder, "Remove local asset folder", localFolder, false);

		PopupMenu subMenu;
		int idx = 0;
		for(const auto& t: tags)
		{
			subMenu.addItem(1000 + idx++, t.name, t.name != info.installInfo.installedVersion, false);
		}

		m.addSubMenu("Revert to version", subMenu);
		
		auto result = m.show();

		if(result == 0)
			return;

		if (result >= 1000)
		{
			auto idx = result - 1000;

			if (info.installInfo.isInstalled())
				manager->addJob(new UninstallAction(this, true));

			manager->addJob(new UpdateAction(this, tags[idx]));
		}
		else
		{
			manager->performAction((SpecialAction)(result-1), this, true);
		}
	}
}

bool HiseAssetManager::ProductList::Row::matchesFlag(int filterFlag, const String& searchTerm) const
{
	return info.matches(filterFlag) && info.matches(searchTerm);
}

void HiseAssetManager::ProductList::Row::updateAfterTag(HiseAssetManager* m)
{
	HiseAssetInstaller installer(m->getMainController(), info.installInfo);

	hasLocalChanges = installer.hasLocalChanges();

	info.installInfo.installedVersion = installer.getInstallInfoFromLog({})[HiseSettings::Project::Version].toString();

	if (!tags.empty())
		info.setFromTagData(tags.back());
	else
		info.updateState();

	title.clear();

	float alpha = 0.7f;

	title.append(info.getNameToDisplay(), GLOBAL_BOLD_FONT().withHeight(18.0f), Colours::white.withAlpha(0.9f));
	title.append(" by " + info.installInfo.vendor, GLOBAL_BOLD_FONT().withHeight(18.0f), Colours::white.withAlpha(0.6f));

	title.setJustification(Justification::centredLeft);
	description.setJustification(Justification::centredLeft);

	if (info.installInfo.isLocalFolder())
	{
		description.clear();
		description.append(info.installInfo.localFolder.getFullPathName(), GLOBAL_FONT(), Colours::white.withAlpha(0.6f));

		if (localIcon == nullptr)
		{
			auto local_asset = "767.nT6K8ClmEz5E.XLZ5YB7Ppd.6by7JTpv8E0ZuMVdX4.6zQi.FEgPRZCY48ltnlHahrwyGzG.4A.Y.fYp5xFEppIJtm.wX+dnrIUgARjmzlD2MDB0lTQwhjrEfTYXrigwfACD9vx1Fkm64PBPXysaAJhvgfDH.gPdEPnyyAwPzAS.gi5hlKAHrPBnGPH.HQL3gc8dltvrjHcIYSy8rNJHwDuGpqJIrIWStWj62yEVkV1zEmUkLF.IynUJ4HAUYydb5zbuw3yTmlN1zCjoJaSRk2SkLrLztjjs4nfD4Uf7QkU1zVUkKNvxdbUgowjyzFXYaZVprlTfjD10zkoGSVbj4XYUkgUGvJXS3LpltjMoMkY48LMQhEsGv681abUYk4QLFkOZWtW+ruymtnK9dxfcPzSnYjWeVZc1VqUIKud2P+lsVmBBIB7U.A1u48d.Lnxdbga+lox24exsSi7ERR6npk8V5rK4dNaW0cdBMkejrFYI046O6FdORNJaRrzrz6twsbs9cKqt6wJ2SayzX2bemsSoiM..QntnEYo0Xs9rG+4LR42k26yzHqekV1oSd9y57o9yuWkbUd+tgKrS1kdG8cb1bYoa+leZa+H8YYikduMOcZnxXK46WYpsajtDJpo8sxWp69YEJ5iteNaHYka5jZiNWeJKcAXfnFPoigjFZ..B..HnA.kB.kYz.HrnVFEoDTvXDRjB5+mAeHz8ju.dpFczLDT394eBOnGOh02an2XlmV5jUi7BREE3Ra.CM+SH0r7MyPpuvgfypwm8fNWIpTN8wQMJLi5+os52UAEPaBLPjl1aZeS4NqfF7zAQR4T8FFsl3fsxLuT8zRPBkFxxIf.hKF3AR.ajGTOuGN+HZIzThUyRhvKYC9ouEs52Xlu0RbLS8v0.NSRi5FM0CufvtVTVnGHf0rB2Ljpyt6y6eg6h.RNJATCbFrUVEJ765aUVmRzSG1jig+yOruLCkAlLCZiaKBVvw.Wyvb0w3G2VfLbRa7AENG6PmXnMvAX.DzA";
			localIcon = createSVGfromBase64(local_asset);
		}
	}
	else
	{
		description.clear();
		description.append(info.description, GLOBAL_BOLD_FONT(), Colours::white.withAlpha(0.6f));
		localIcon = nullptr;
	}

	actionButton.setButtonText(info.getActionName());
	repaint();
	initialised = true;
}

void HiseAssetManager::ProductList::Row::paint(Graphics& g)
{
	auto iconPainted = paintBase(g, localIcon.get(), false);

	if (!initialised)
	{
		g.setColour(Colours::white.withAlpha(0.1f));
		g.setFont(GLOBAL_BOLD_FONT());
		g.drawText("Loading data...", getLocalBounds().toFloat(), Justification::centred);
		return;
	}

	if (iconPainted)
	{
		// done...
	}
	else if (productInfo != nullptr && productInfo->image.isValid())
	{
		g.setColour(Colours::white);
		g.drawImageWithin(productInfo->image, imgBounds.getX(), imgBounds.getY(), imgBounds.getWidth(), imgBounds.getHeight(), RectanglePlacement::centred);
	}
	else
	{
		g.setColour(Colours::white);
		g.drawRoundedRectangle(imgBounds.toFloat(), 3.0f, 2.0f);
	}

	bool updateDrawn = false;

	if (info.installInfo.isInstalled() && info.installInfo.isUpdateAvailable())
	{
		g.setColour(Colour(SignalColour));
		g.fillEllipse(circleArea.toFloat().translated(10.0f, 10.0f));
		updateDrawn = true;
	}
	if(hasLocalChanges)
	{
		auto thisCircle = circleArea.toFloat().translated(10.0f, 10.0f);

		if(updateDrawn)
			thisCircle = thisCircle.translated(0.0, 20.0f);

		g.setColour(Colour(HISE_WARNING_COLOUR));
		g.fillEllipse(thisCircle);
	}

	if (!info.installInfo.isInstalled() && info.state != RowInfo::State::NotOwned)
	{
		auto copy = titleBounds;

		PathFactory::scalePath(downloadIcon, copy.removeFromLeft(16.0f));
		copy.removeFromLeft(3.0f);
		g.setColour(Colours::white.withAlpha(0.7f));
		g.fillPath(downloadIcon);
		title.draw(g, copy);
	}
	else
	{
		title.draw(g, titleBounds);
	}

	description.draw(g, descriptionBounds);
}

void HiseAssetManager::ProductList::Row::resized()
{
	auto b = getLocalBounds();
	circleArea = b.removeFromLeft(10).removeFromTop(10).toFloat();
	b.removeFromLeft(3);

	b.removeFromRight(10);
	auto bb = b.removeFromRight(180).withSizeKeepingCentre(180, 20);

	infoButton.setBounds(bb.removeFromLeft(20));
	moreButton.setBounds(bb.removeFromRight(20));
	actionButton.setBounds(bb);

	b.removeFromRight(5);
	imgBounds = b.removeFromLeft(b.getHeight()).reduced(16);
	b.removeFromLeft(5);

	b = b.reduced(0, 5 + 8);

	titleBounds = b.removeFromTop(b.getHeight() / 2).toFloat();
	descriptionBounds = b.toFloat();
}

hise::HiseAssetManager::ProductList::Row* HiseAssetManager::ProductList::Content::addRow(const RowInfo& r, HiseAssetManager* manager)
{
	auto nr = new Row(r, manager);
	rows.add(nr);
	addAndMakeVisible(rows.getLast());
	return nr;
}



void HiseAssetManager::ProductList::Content::updateFilter()
{
	int h = 0;

	anyVisible = false;

	for (auto rb : rows)
	{
		auto match = rb->matchesFlag(currentAssetFilter, searchTerm.toLowerCase().trim());
		rb->setVisible(match);

		anyVisible |= match;

		if (match)
			h += RowHeight;
	}

	setSize(getWidth(), h);
	resized();
}

void HiseAssetManager::ProductList::Content::rebuild()
{
	updateFilter();
	resized();
}

void HiseAssetManager::ProductList::Content::resized()
{
	if (getLocalBounds().isEmpty())
		return;

	auto b = getLocalBounds();

	for (auto r : rows)
	{
		if (r->isVisible())
			r->setBounds(b.removeFromTop(RowHeight));
	}
}

HiseAssetManager::HiseAssetManager(BackendRootWindow* brw) :
	ControlledObject(brw->getMainController()),
	Thread("HISE Asset Manager operations"),
	resizer(this, &restrainer),
	closeButton("close", nullptr, *this)
{
	getMainController()->getSampleManager().getProjectHandler().addListener(this);

	productList = new ProductList();

	addAndMakeVisible(topBar = new TopBar(productList.get()));
	addAndMakeVisible(productList);

	addJob(new ProductFetchTask());
	projectChanged(getMainController()->getSampleManager().getProjectHandler().getRootFolder());


	addAndMakeVisible(resizer);

	setSize(900, 700);

	restrainer.setSizeLimits(600, 600, 1500, 1500);
	restrainer.setMinimumOnscreenAmounts(0xffffff, 0xffffff, 0xffffff, 0xffffff);

	addAndMakeVisible(closeButton);

	closeButton.onClick = [this]()
	{
		this->stopThread(1000);
		this->destroy();
	};
}

HiseAssetManager::~HiseAssetManager()
{
	getMainController()->getSampleManager().getProjectHandler().removeListener(this);
}

void HiseAssetManager::projectChanged(const File& newRootDirectory)
{
	currentProjectRoot = newRootDirectory;
	projectName = GET_HISE_SETTING(getMainController()->getMainSynthChain(), HiseSettings::Project::Name);
	initialiseLocalAssets();
	setProjectFolder(newRootDirectory);
}

void HiseAssetManager::initialiseLocalAssets()
{
	productList->setProducts({}, this);
}

void HiseAssetManager::rebuildProductsAsync()
{
	MessageManager::callAsync([this]()
	{
		productList->setProducts({}, this);
	});
}

void HiseAssetManager::setProjectFolder(const File& f)
{
	projectInfo.clear();
	projectInfo.append("Current HISE project: ", GLOBAL_FONT(), Colours::grey);
	projectInfo.append(f.getFullPathName(), GLOBAL_BOLD_FONT(), Colours::white);
	projectInfo.setJustification(Justification::centredLeft);
	repaint();
}

juce::Path HiseAssetManager::createPath(const String& url) const
{
	Path p;
	LOAD_EPATH_IF_URL("close", HiBinaryData::ProcessorEditorHeaderIcons::closeIcon);

	if (url == "download")
	{
		static const unsigned char downloadIcon[] = { 110,109,0,128,38,68,0,0,64,67,98,189,155,55,68,0,0,64,67,0,128,69,68,240,142,119,67,0,128,69,68,0,0,158,67,98,0,128,69,68,112,55,192,67,189,155,55,68,248,255,219,67,0,128,38,68,248,255,219,67,98,191,99,21,68,248,255,219,67,0,128,7,68,112,55,192,67,0,
128,7,68,0,0,158,67,98,0,128,7,68,240,142,119,67,191,99,21,68,0,0,64,67,0,128,38,68,0,0,64,67,99,109,0,128,38,68,160,244,87,67,98,20,178,24,68,160,244,87,67,43,125,13,68,32,100,130,67,43,125,13,68,0,0,158,67,98,43,125,13,68,200,154,185,67,20,178,24,68,
152,4,208,67,0,128,38,68,152,4,208,67,98,103,77,52,68,152,4,208,67,80,130,63,68,200,154,185,67,80,130,63,68,0,0,158,67,98,80,130,63,68,32,100,130,67,103,77,52,68,160,244,87,67,0,128,38,68,160,244,87,67,99,109,127,190,43,68,216,148,160,67,108,35,213,53,
68,216,148,160,67,108,0,128,38,68,112,55,192,67,108,88,42,23,68,216,148,160,67,108,252,64,33,68,216,148,160,67,108,252,64,33,68,16,145,119,67,108,127,190,43,68,16,145,119,67,108,127,190,43,68,216,148,160,67,99,101,0,0 };

		p.loadPathFromData(downloadIcon, sizeof(downloadIcon));
	}

	return p;
}

void HiseAssetManager::tryToLogin()
{
	auto token = Helpers::getCredentialFile().loadFileAsString();

	if (token.isNotEmpty())
		addJob(new LoginTask());
}

void HiseAssetManager::showLoginPrompt()
{
	auto token = Helpers::getCredentialFile().loadFileAsString();

	String message;
	message << "Please enter the Auth Token that you can find in the Settings of your HISE store account.";
	message << "\n> Click Open HISE Store to open the account in your web browser.";

	ScopedPointer<MessageWithIcon> comp = new MessageWithIcon(PresetHandler::IconType::Question, &getLookAndFeel(), message);
	ScopedPointer<AlertWindow> nameWindow = new AlertWindow("Enter HISE Store Auth Token", "", AlertWindow::AlertIconType::NoIcon);

	ScopedPointer<ToggleButton> saveButton = new ToggleButton("");

	saveButton->setSize(128, 32);
	saveButton->setToggleState(true, dontSendNotification);

	nameWindow->setLookAndFeel(&getLookAndFeel());
	nameWindow->addCustomComponent(comp);
	nameWindow->addTextEditor("Name", token);
	nameWindow->addCustomComponent(saveButton);

	nameWindow->getTextEditor("Name")->setPasswordCharacter('*');

	GlobalHiseLookAndFeel::setDefaultColours(*saveButton);

	saveButton->setButtonText("Save credentials");

	nameWindow->addButton("OK", 1, KeyPress(KeyPress::returnKey));
	nameWindow->addButton("Cancel", 0, KeyPress(KeyPress::escapeKey));
	nameWindow->addButton("Open HISE Store", 2);

	nameWindow->getTextEditor("Name")->setSelectAllWhenFocused(true);
	nameWindow->getTextEditor("Name")->grabKeyboardFocusAsync();

	auto result = nameWindow->runModalLoop();

	if (result == 2)
	{
		URL("https://store.hise.dev/account/settings/").launchInDefaultBrowser();
		return;
	}

	if (result == 1)
	{
		token = nameWindow->getTextEditorContents("Name");

		if (saveButton->getToggleState())
		{
			Helpers::getCredentialFile().replaceWithText(token.trim());
		}
	}

	if (token.isNotEmpty())
	{
		setState(ErrorState::OK);
		addJob(new LoginTask());
	}
}

void HiseAssetManager::addNewAssetFolder()
{
	FileChooser fc("Add local HISE project as asset", File(), "package_install.json");

	if (fc.browseForFileToOpen())
	{
		auto f = fc.getResult().getParentDirectory();

		if (auto info = HiseAssetInstaller::UninstallInfo(f))
		{
			localAssets->assets.push_back(info);
			productList->setProducts({}, this);
			localAssets->resave();
		}
	}
}

void HiseAssetManager::performAction(SpecialAction a, ProductList::Row* r, bool silent/*=true*/)
{
	String msg;

	if (a == SpecialAction::RemoveLocalFolder)
	{
		silent = false;
		msg << "Remove the folder\n> `" + r->info.installInfo.localFolder.getFullPathName() << "`";
	}
	else
	{
		msg << "Do you want to " + r->info.getActionName();
	}

	if (!silent && !PresetHandler::showYesNoWindow("Confirm", msg))
		return;

	switch (a)
	{
	case ShowInStore:

		if (r->info.installInfo.isLocalFolder())
		{
			r->info.installInfo.localFolder.getChildFile("package_install.json").revealToUser();
		}
		else
		{
			r->info.shopURL.launchInDefaultBrowser();
		}

		break;
	case RemoveLocalFolder:
	{
		auto folderToRemove = r->info.installInfo.localFolder;

		for (const auto& a : localAssets->assets)
		{
			if (a.localFolder == folderToRemove)
			{
				auto& v = localAssets->assets;

				v.erase(std::remove(v.begin(), v.end(), a),
					v.end());

				localAssets->resave();
				break;
			}
		}

		rebuildProductsAsync();
		break;
	}
	case CheckLocalChanges:
	{
		HiseAssetInstaller installer(getMainController(), r->info.installInfo);

		auto list = installer.checkLocalChanges();

		if(!list.isEmpty())
		{
			auto rf = getMainController()->getCurrentFileHandler().getRootFolder();
			auto df = new DiffViewer(rf);
			
			showStatusMessage("Editing local changes for package " + r->info.getPackageName());
			setState(ErrorState::Pending);

			for(const auto& l: list)
			{
				df->addDiff(l.first, l.second);
			}

			df->setEditedFile(list[0].first, list[0].second, false);

			setModalComponent(df);
		}

		break;
	}
	case Install:
	{
		if (r->info.installInfo.isInstalled())
			addJob(new ProductList::Row::UninstallAction(r, true));

		if (r->info.installInfo.isLocalFolder())
			addJob(new ProductList::Row::UpdateAction(r, {}));
		else
			addJob(new ProductList::Row::UpdateAction(r, r->tags.back()));

		break;
	}
	case Uninstall:
		addJob(new ProductList::Row::UninstallAction(r, false));
		break;
	default:
		break;
	}
}


void HiseAssetManager::addJob(ActionBase::Ptr newJob)
{
	{
		ScopedLock sl(lock);
		pendingTasks.add(newJob);
	}

	startTimer(100);
}

void HiseAssetManager::run()
{
	if (state == ErrorState::Error)
		return;

	ScopedValueSetter<bool> svs(running, true);

	setState(ErrorState::Pending);

	ActionBase::List thisTime;

	{
		ScopedLock sl(lock);
		thisTime.swapWith(pendingTasks);
	}

	for (auto t : thisTime)
	{
		auto msg = t->getMessage();

		if (msg.isNotEmpty())
			showStatusMessage(msg);

		if (t->perform(this))
			completedTasks.add(t);
	}

	if (state != ErrorState::Error)
		setState(ErrorState::OK);

	triggerAsyncUpdate();
}

void HiseAssetManager::handleAsyncUpdate()
{
	for (auto t : completedTasks)
		t->onCompletion(this);

	completedTasks.clear();

	if (!pendingTasks.isEmpty() && !running)
		this->startThread();
}

void HiseAssetManager::setState(ErrorState newState)
{
	state = newState;
	SafeAsyncCall::repaint(this);
}

void HiseAssetManager::showStatusMessage(const String& newMessage)
{
	currentMessage = newMessage;
	SafeAsyncCall::repaint(this);
}

void HiseAssetManager::showHelpPopup(Component* attachedComponent, const String& markdownText)
{
	auto md = new SimpleMarkdownDisplay("");

	md->margin = 20;
	md->backgroundColour = Colour(0xFF333333);
	md->setOpaque(true);
	md->setSize(700, 500);
	md->setText(markdownText);

	std::unique_ptr<Component> n;
	n.reset(md);

	auto brw = GET_BACKEND_ROOT_WINDOW(attachedComponent);

	auto localArea = brw->getLocalArea(attachedComponent, attachedComponent->getLocalBounds());
	auto& cb = juce::CallOutBox::launchAsynchronously(std::move(n), localArea, brw);
}

void HiseAssetManager::mouseDown(const MouseEvent& e)
{
	dragger.startDraggingComponent(this, e);
}

void HiseAssetManager::mouseDrag(const MouseEvent& e)
{
	dragger.dragComponent(this, e, nullptr);
}

void HiseAssetManager::timerCallback()
{
	if (!running)
		this->startThread();

	stopTimer();
}

void HiseAssetManager::paint(Graphics& g)
{
	g.fillAll(Colour(0xFF222222));
	auto b = getLocalBounds();
	auto tb = b.removeFromTop(24);
	GlobalHiseLookAndFeel::drawFake3D(g, tb);
	g.setFont(GLOBAL_BOLD_FONT());
	g.setColour(Colours::white);
	g.drawText("HISE Asset Manager", tb.toFloat(), Justification::centred);

	g.setColour(Colours::white.withAlpha(0.05f));
	g.fillRect(getLocalBounds().removeFromBottom(20));

	if (state != ErrorState::OK && currentMessage.isNotEmpty())
	{
		g.setFont(GLOBAL_BOLD_FONT());
		g.setColour(Colours::white.withAlpha(0.5f));
		g.drawText(currentMessage, projectFolderBounds.reduced(5.0f, 0.0f), Justification::left);
	}
	else
	{
		projectInfo.draw(g, projectFolderBounds.reduced(5.0f, 0.0f));
	}

	g.setColour(colours[(int)state]);
	g.drawEllipse(stateIcon, 2.0f);
	g.fillEllipse(stateIcon.reduced(3.0f));

	g.setColour(Colour(0xFF444444));
	g.drawRect(getLocalBounds(), 1);

	g.setFont(GLOBAL_FONT());

	g.setColour(Colours::white.withAlpha(0.6f));
	g.drawText(userName, projectFolderBounds.reduced(20.0, 0.0f), Justification::centredRight);
}

void HiseAssetManager::resized()
{
	auto b = getLocalBounds();
	auto header = b.removeFromTop(24);

	closeButton.setBounds(header.removeFromRight(header.getHeight()).reduced(3));


	b = b.reduced(90, 25);

	topBar->setBounds(b.removeFromTop(32));

	b.setWidth(b.getWidth() + productList->vp.getScrollBarThickness());

	b.removeFromTop(20);

	b.removeFromBottom(40);

	productList->setBounds(b);
	resizer.setBounds(getLocalBounds().removeFromBottom(15).removeFromRight(15));

	projectFolderBounds = getLocalBounds().removeFromBottom(20).toFloat();

	stateIcon = projectFolderBounds.removeFromLeft(projectFolderBounds.getHeight()).reduced(4.0f);

	if (getParentComponent() != nullptr)
	{
		centreWithSize(getWidth(), getHeight());
	}

	if(modalComponent != nullptr)
	{
		auto mb = getLocalBounds();
		mb.removeFromTop(24);
		mb.removeFromBottom(22);
		modalComponent->setBounds(mb);
	}
}

HiseAssetManager::ProductList::ProductList()
{
	addAndMakeVisible(vp);
	vp.setViewedComponent(content = new Content(), false);
	vp.setScrollBarThickness(12);
	vp.setScrollOnDragEnabled(true);

	setSize(600, 300);

	sf.addScrollBarToAnimate(vp.getVerticalScrollBar());
}

void HiseAssetManager::ProductList::setProducts(const Array<var>& data, HiseAssetManager* manager)
{
	if (!data.isEmpty())
	{
		storeData.clear();
		storeData.addArray(data);
	}

	content->rows.clear();

	StringArray ownedProducts;

	for (auto& pd : storeData)
	{
		ProductInfo::Ptr info;

		RowInfo ri(pd);

		for (auto pi : manager->products)
		{
			if (pi->repoId == ri.getPackageName())
			{
				info = pi;
				break;
			}
		}

		ri.setFromProductInfo(info.get());
		auto nr = content->addRow(ri, manager);
		nr->productInfo = info;
		ownedProducts.add(ri.getPackageName());
	}

	for (auto pi : manager->products)
	{
		if (ownedProducts.contains(pi->repoId))
			continue;

		auto nr = content->addRow(RowInfo(pi), manager);
		nr->productInfo = pi;
	}

	for (const auto& la : manager->localAssets.get())
	{
		content->addRow(RowInfo(la), manager);
	}

	auto token = Helpers::getCredentialFile().loadFileAsString();

	if (!manager->loginOk)
	{
		auto connectRow = new SpecialRow();

		connectRow->setText("Login with your HISE Store account to sync this list with your purchased assets.");
		connectRow->setIcon("1094.nT6K8C1lFTdH.XYOEaBvp791HdIOsrUiWPPbDBwThRP7x0G2EraxVB.vGLtIAAAoDD55A3K.pBPt.HNVhzjwHHoC+EeRjlnmkfghLnqib7a5y3GVxbQXHnW3IKQRQUOvuIy+42rrB6Khq7jgk26ujFACgfg0ZwXmiTUWcEF5nOv+HpJHJy6JrQncfoUcnc060xx5TD3nRNyHVelj0bZMMM0xhQIJ5nJpJKPrrt0KUDNZndr4gkUVtik0hJpLMJN1tP8Y5jBxkOaxX3g.GD.BITQNVg8GIC45F4POop5KTXyVE12HXGonwOUksdqvFIAOM+lnBEgHgCxtpu28091qb+sBnbWW6c7ktZ2a9Vm4wVLkNOWu5qFuqVKti80K+M650U78p48d05uTYeFmytx1Zu68Udtmu1Uu9xW2dNul455Muq69kR1xqd7VA91qW8akJrNpQnbkaqs5u2EJ5W70UMuP66Xb8yx5DUksB6+wIp0v.eyQLn9mkrKJPiupV6dgB8FW4V7BEWqhKAP.ZhxIVgIZR5cNxYhR8c.kAXFfgleTvHV4rTihFFJhThjpa3s8fLaAMVHcFwBIIhvjs4hDyzRoYk9V3i+A28nU3DFLYZLZb4+GYFNXfDB4pSmvIRCHX.BuhmNZN7jEJ6jgOeZjYt83aCxnMMVZP9EYhMMCCN3.pPe8O4BYv2XoECNTttJrNPJvSuemAEPPnOSFfnn2Tt74a9h40KkdMaqdd9h62459duZdkiu9KE1iwYZ+Niw4r+twW7aluaqX7deU4aNtt67dV2lq4qsUq85..fvmoSqCrpvzz9jsmjgANaZBY8xtnozl16Dz75jDSIF4cwh6x+cfiz3iymHYXDwlrD5F1HAdyVmRbe8HV13gNH87vD9jRsKMKHO1t34CHKFKHQSMQRiPyRgKjtjoKQj7Kld5gRsHp3oPkOSb1jgTlDR5P58v2IxQ5Hpr4VYzXlDS3z7HuHxCsCGPYbPmPijSmRw7kcUdcVwywTxxonMwG3TB5ZeRJdwiitExLAhenXluQiDTr.R6SHKM5.g7+.tu88YR4aFC.BIpgSJcJHp.PDAD.PC.PoHSUUUAAVJUlCxHADHDs1.7r.3KKAUnDcHpe4gAUCQBGnnQ50rBfPAs84NQMeSMHN4123YBLIeoWjIkX9LhA4fmhpwdixVIAoONuKIPmyqpzNksarXe.1elGF3LdQTG8xVCRX3sFC9MlbaYilRunhwLRLUPwyaj1AANMNxbiADFkqAKIgdQC.NjQBhnHTLTuJLtXC.6d1QUOXeEMblZgarphEBfv0PhzQs9utzuv0V7hGJGGdjhs3sHX5Wzc2Ty6FhV.NMUmFEUW2R4Zxaq+1bNXJXhOZy8O.AwcWIj8nSX.T.A0JWPk5ldVpFC9lZQ5hL2Dv2xP+bgJXFf4MLNvln6p7I8P2RZE7oqIlEg5EG67JfAjsC");

		connectRow->onClick = std::bind(&HiseAssetManager::showLoginPrompt, manager);

		content->rows.add(connectRow);
		content->addAndMakeVisible(connectRow);

	}
	{
		auto localManager = new SpecialRow();

		localManager->onClick = std::bind(&HiseAssetManager::addNewAssetFolder, manager);

		localManager->setText("Add local folders to this list to create assets");
		localManager->setIcon("1130.nT6K8ClaGTvH.XkOHaBvL79+jCCjrG.Kuvje38C2AlNry6f5RjhBmPfhZ2h2lhhhkEEPN3K.wB.u.j9GICUd1f7WBgmgOhxyTTyu3GC99JI8h7M7hgNYHJ9AEKQMV9wB98hN8n9SyJl+imIZCpduiif7KRpw8dqvPImpXtthkBJ76AMUIYl2ULPoe+Q0uzOKFSQQg94Cxz6DBU3YoMOxNZzHJpFloofpS0JaTTXKdBhlLSOZ3AEkVdCEEi5jVDNGcWlBEgNscoSCZVdHvAAfPB0RxJlmrTz6izKEcpxYnPCcUrPx+W4GR+T8ZwULxxO5oWzj3HoDNHftpaMguxZ8L2xs3v498xdcks5L5WKvWWarshwcqthUZpdqX9PuwHNLPECzeBpwjGMzB7MjhpZPAb8hLFuPmwD7P3YCOYtFaspzYqdawYKMs0pxBycwsYbU1lqr38FOcky1s1thauzbUWWakE110aLkJ12DnTOSqc2ZsyK34Ka0cag0Ws8x2ZNOauEjwW1cWyXQuRes8NiE36BLAj9modiUrwyRPTRuyzovClLnpwW7zGvd64YgN4SnXm8voPCjHVn5jgxaotvOMLhFxTBWGHk5UJYHaBPaf3ONwjxDLbtcchxJ.4PZ.SGGVLT5k8gkXzvvl3MqGMQCjLRV1tnvYSDGbzdPC0RmNYZmcOB61.0koRVYHjI2QXr3Pyeg1xi58fAgjlTM7R89pXe+oOJn+aQNyowgsUScMWyRqs36Vqw06WSXsV0ta6bt90h43Ksrtxd4b580kles7qo8kaqz52JalywsWr1tusXt99lsB..BghPoNfphgEFFJIhyscGFINxlzITJIIhHoZg7HKtK6Pnj3.04N4nmHwPKaYg7H5IijzE3HFrDUbfFZ6w+gBQt7QyTgFMKK+rR+I8MZW5TDmXaiWEE7S1bh.gpozEEYZlXyasmP5.85UBTwCOPnPmU5FnBISFkfdeXBuaBNVdz4LBPtBViWY6bFLcLjk7uLB4wWBGQhjACELY5HghGJyacOHmOMuOsPOabLofPhmkrRQxn0qP1MAW1lGTRKK+YifjMhCoPon5..lIpgjxXHFwl..B...M.PgHhUF4AwCRAFmDLiLAjFRDDjT..xC.AapyRVeTYn+JEXTwR5F.MMcUwmBoInyEzLEfaBPLLEb5ijiSjdPSkFTCUpuBgmNHzyvfR41CJ0vJrGF8QLUebRV+5lYh8FFkHLYE6vWUgNqc.iRaXvju+O48+9RxNb9BFQttvOPEz8f+WHf2OvFBsICwv2ADPTjurO6JMuCu+sxHG7LjXnHRnPXivqJHTXvhlgW1r522OJbBRUO3DFXscxxSqD+JiFTeUg7rxFfJEH9X3uwvy7YWiEmjTlY1vzCESCJG07TwZFFcg.Q.fPQiogJwP2g9VkcWzoDEoNzDxvhSALwFhlSPo46omkxwgA3s.u5DQO3sLXoKsCIAhE9lT3VKzJiIer2fWC+cd1rMHlwGvxBXtB5.");
		content->rows.add(localManager);
		content->addAndMakeVisible(localManager);
	}

	content->rebuild();
}

void HiseAssetManager::ProductList::paint(Graphics& g)
{
	if (!content->anyVisible)
	{
		g.setColour(Colours::white.withAlpha(0.4f));
		g.setFont(GLOBAL_FONT());
		g.drawText("No matches :(", getLocalBounds().toFloat(), Justification::centred);
	}
}

void HiseAssetManager::ProductList::resized()
{
	vp.setBounds(getLocalBounds());
	content->setSize(vp.getWidth() - vp.getScrollBarThickness(), content->getHeight());
}

HiseAssetManager::TopBar::TopBar(ProductList* list_) :
	list(list_),
	allButton("My Assets"),
	installedButton("Installed"),
	uninstalledButton("Uninstalled"),
	onlineButton("Browse Store"),
	sortButton("Sort by"),
	blaf(new ButtonLaf())
{
	/* TODO:

	- implement project root folder display OK
	- add product info from fetch server OK
	- add sorting & filtering
	- fix text layout
	- fix circle when update available OK
	- set all as default OK
	- add show package file & show install log
	- add show local folder & switch project context option
	- add open in store as context option
	- implement action button functionality with confirmation
	- fix product info not being detected correctly.

	*/
	addAndMakeVisible(allButton);
	addAndMakeVisible(installedButton);
	addAndMakeVisible(uninstalledButton);
	addAndMakeVisible(onlineButton);
	addAndMakeVisible(sortButton);

	allButton.setLookAndFeel(blaf);
	installedButton.setLookAndFeel(blaf);
	uninstalledButton.setLookAndFeel(blaf);
	sortButton.setLookAndFeel(blaf);
	onlineButton.setLookAndFeel(blaf);

	sortButton.setConnectedEdges(Button::ConnectedOnLeft);

	allButton.addListener(this);
	installedButton.addListener(this);
	uninstalledButton.addListener(this);
	sortButton.addListener(this);
	onlineButton.addListener(this);

	allButton.setClickingTogglesState(true);
	installedButton.setClickingTogglesState(true);
	uninstalledButton.setClickingTogglesState(true);
	onlineButton.setClickingTogglesState(true);

	allButton.setRadioGroupId(12251);
	installedButton.setRadioGroupId(12251);
	uninstalledButton.setRadioGroupId(12251);
	onlineButton.setRadioGroupId(12251);

	allButton.setToggleState(true, dontSendNotification);

	sortButton.setColour(TextButton::ColourIds::buttonColourId, Colour(0xFF555555));
	allButton.setColour(TextButton::ColourIds::buttonColourId, Colour(0xFF555555));
	allButton.setColour(TextButton::ColourIds::buttonOnColourId, Colour(ProductList::SignalColour));
	installedButton.setColour(TextButton::ColourIds::buttonColourId, Colour(0xFF555555));
	installedButton.setColour(TextButton::ColourIds::buttonOnColourId, Colour(ProductList::SignalColour));
	uninstalledButton.setColour(TextButton::ColourIds::buttonColourId, Colour(0xFF555555));
	uninstalledButton.setColour(TextButton::ColourIds::buttonOnColourId, Colour(ProductList::SignalColour));
	onlineButton.setColour(TextButton::ColourIds::buttonColourId, Colour(0xFF555555));
	onlineButton.setColour(TextButton::ColourIds::buttonOnColourId, Colour(ProductList::SignalColour));

	allButton.setConnectedEdges(Button::ConnectedOnRight);
	installedButton.setConnectedEdges(Button::ConnectedOnRight | Button::ConnectedOnLeft);
	uninstalledButton.setConnectedEdges(Button::ConnectedOnLeft);

	searchIcon.loadPathFromData(EditorIcons::searchIcon, SIZE_OF_PATH(EditorIcons::searchIcon));
	searchIcon.applyTransform(AffineTransform::rotation(float_Pi));
	addAndMakeVisible(searchBar);
	GlobalHiseLookAndFeel::setTextEditorColours(searchBar);
	searchBar.setTextToShowWhenEmpty("Search assets...", Colours::grey);
	searchBar.setJustification(Justification::centredLeft);
	setSize(600, 36);

	searchBar.onTextChange = [this]()
	{
		updateFilters();
	};
}

void HiseAssetManager::TopBar::updateFilters()
{
	list->content->currentAssetFilter = getAssetType();
	list->content->searchTerm = searchBar.getText().trim().toLowerCase();
	list->content->updateFilter();
	list->repaint();
}

int HiseAssetManager::TopBar::getAssetType() const
{
	int flags = currentDisplayType;
	flags |= AssetType::AllSources;

	return flags;
}

void HiseAssetManager::TopBar::buttonClicked(Button* b)
{
	if (b == &allButton)
		currentDisplayType = AssetType::All;
	if (b == &installedButton)
		currentDisplayType = AssetType::Installed;
	if (b == &uninstalledButton)
		currentDisplayType = AssetType::Uninstalled;
	if (b == &onlineButton)
		currentDisplayType = AssetType::Online;

	updateFilters();
}

void HiseAssetManager::TopBar::paint(Graphics& g)
{
	g.setColour(Colours::white.withAlpha(0.4f));
	g.fillPath(searchIcon);
}

void HiseAssetManager::TopBar::resized()
{
	auto b = getLocalBounds();
	//projectFolderBounds = b.removeFromTop(32).toFloat().reduced(2.0f);



	onlineButton.setBounds(b.removeFromRight(90).removeFromTop(24));
	b.removeFromRight(10);
	uninstalledButton.setBounds(b.removeFromRight(70).removeFromTop(24));
	installedButton.setBounds(b.removeFromRight(70).removeFromTop(24));
	allButton.setBounds(b.removeFromRight(70).removeFromTop(24));

	auto searchIconArea = b.removeFromLeft(b.getHeight()).toFloat().reduced(3.0f);

	b = b.removeFromLeft(jmin(b.getWidth(), 350));

	b.removeFromRight(30);
	sortButton.setBounds(b.removeFromRight(60));

	searchBar.setBounds(b);



	PathFactory::scalePath(searchIcon, searchIconArea);
}



}