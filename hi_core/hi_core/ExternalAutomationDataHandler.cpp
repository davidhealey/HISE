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

#if HISE_USE_EXTERNAL_AUTOMATION_DATA

namespace hise { using namespace juce;

constexpr char ExternalAutomationDataHandler::automationDataFileName[];

ExternalAutomationDataHandler::ExternalAutomationDataHandler(MainController* mc_) :
	ControlledObject(mc_)
{
	getMainController()->getExpansionHandler().addListener(this);
}

ExternalAutomationDataHandler::~ExternalAutomationDataHandler()
{
	getMainController()->getExpansionHandler().removeListener(this);
}

ExternalAutomationDataHandler::ScopedRestoreSuspender::ScopedRestoreSuspender(MainController* mc_)
{
	if (mc_ != nullptr)
	{
		if (auto* h = mc_->getExternalAutomationDataHandler())
		{
			handler = h;
			h->suspendCount++;
		}
	}
}

ExternalAutomationDataHandler::ScopedRestoreSuspender::~ScopedRestoreSuspender()
{
	if (handler != nullptr)
		handler->suspendCount--;
}

void ExternalAutomationDataHandler::setEnabled(bool shouldBeEnabled)
{
	if (enabled == shouldBeEnabled)
		return;

	enabled = shouldBeEnabled;

	if (enabled)
		applyExternalAutomationData();
}

File ExternalAutomationDataHandler::getAutomationDataFile() const
{
	auto* fh = getMainController()->getActiveFileHandler();

	if (fh == nullptr)
		return {};

	auto root = fh->getRootFolder();

	if (root == File())
		return {};

	return root.getChildFile(automationDataFileName);
}

bool ExternalAutomationDataHandler::hasExternalFile() const
{
	return getAutomationDataFile().existsAsFile();
}

ValueTree ExternalAutomationDataHandler::getExternalSection(const Identifier& sectionId) const
{
	auto f = getAutomationDataFile();

	if (!f.existsAsFile())
		return {};

	var parsed;

	auto r = JSON::parse(f.loadFileAsString(), parsed);

	if (r.failed() || !parsed.isObject())
		return {};

	auto root = jsonToValueTree(parsed);

	return root.getChildWithName(sectionId);
}

ValueTree ExternalAutomationDataHandler::getTreeToRestore(const Identifier& sectionId, const ValueTree& presetFallback) const
{
	if (isActive())
	{
		auto ext = getExternalSection(sectionId);

		if (ext.isValid())
			return ext;
	}

	return presetFallback;
}

bool ExternalAutomationDataHandler::shouldExcludeFromPreset() const
{
	return isActive() && hasExternalFile();
}

void ExternalAutomationDataHandler::applyExternalAutomationData()
{
	if (!isActive() || !hasExternalFile())
		return;

	ScopedRestoreSuspender ss(getMainController());

	auto* mc = getMainController();

	if (auto* mah = mc->getMacroManager().getMidiControlAutomationHandler())
	{
		auto midi = getExternalSection(UserPresetIds::MidiAutomation);

		if (midi.isValid())
			mah->restoreFromValueTree(midi);

		auto mpe = getExternalSection(UserPresetIds::MPEData);

		if (mpe.isValid())
			mah->getMPEData().restoreFromValueTree(mpe);
	}

	auto macros = getExternalSection(getMacroSectionId());

	if (macros.isValid())
	{
		ValueTree tmp("Preset");
		tmp.addChild(macros, -1, nullptr);
		mc->getMainSynthChain()->loadMacrosFromValueTree(tmp, false);
	}
}

void ExternalAutomationDataHandler::notifyAutomationDataChanged()
{
	if (!isActive() || isSuspended())
		return;

	// coalesce a burst of changes into a single write
	if (savePending)
		return;

	savePending = true;

	WeakReference<ExternalAutomationDataHandler> safeThis(this);

	MessageManager::callAsync([safeThis]()
	{
		if (safeThis != nullptr)
		{
			safeThis->savePending = false;
			safeThis->writeFileNow();
		}
	});
}

void ExternalAutomationDataHandler::expansionPackLoaded(Expansion* /*currentExpansion*/)
{
	// restore the newly active expansion's own assignments
	applyExternalAutomationData();
}

ValueTree ExternalAutomationDataHandler::gatherAutomationData() const
{
	ValueTree root("AutomationData");

	auto* mc = getMainController();

	if (auto* mah = mc->getMacroManager().getMidiControlAutomationHandler())
	{
		auto midi = mah->exportAsValueTree();

		if (midi.isValid())
			root.addChild(midi.createCopy(), -1, nullptr);

		auto mpe = mah->getMPEData().exportAsValueTree();

		if (mpe.isValid())
			root.addChild(mpe.createCopy(), -1, nullptr);
	}

	ValueTree tmp("tmp");
	mc->getMainSynthChain()->saveMacrosToValueTree(tmp);
	auto macros = tmp.getChildWithName(getMacroSectionId());

	if (macros.isValid())
		root.addChild(macros.createCopy(), -1, nullptr);

	return root;
}

void ExternalAutomationDataHandler::writeFileNow()
{
	if (!isActive())
		return;

	auto f = getAutomationDataFile();

	if (f == File())
		return;

	auto root = gatherAutomationData();

	auto json = JSON::toString(valueTreeToJSON(root), false);

	f.getParentDirectory().createDirectory();
	f.replaceWithText(json);
}

var ExternalAutomationDataHandler::valueTreeToJSON(const ValueTree& v)
{
	DynamicObject::Ptr obj = new DynamicObject();

	obj->setProperty("_type", v.getType().toString());

	for (int i = 0; i < v.getNumProperties(); i++)
	{
		auto id = v.getPropertyName(i);
		obj->setProperty(id, v.getProperty(id));
	}

	if (v.getNumChildren() > 0)
	{
		Array<var> children;

		for (const auto& c : v)
			children.add(valueTreeToJSON(c));

		obj->setProperty("_children", var(children));
	}

	return var(obj.get());
}

ValueTree ExternalAutomationDataHandler::jsonToValueTree(const var& value)
{
	auto obj = value.getDynamicObject();

	if (obj == nullptr)
		return {};

	static const Identifier typeId("_type");
	static const Identifier childrenId("_children");

	ValueTree v(Identifier(obj->getProperty(typeId).toString()));

	for (const auto& p : obj->getProperties())
	{
		if (p.name == typeId || p.name == childrenId)
			continue;

		v.setProperty(p.name, p.value, nullptr);
	}

	// keep the var alive in a local so the array pointer does not dangle
	auto childrenVar = obj->getProperty(childrenId);

	if (auto children = childrenVar.getArray())
	{
		for (const auto& c : *children)
			v.addChild(jsonToValueTree(c), -1, nullptr);
	}

	return v;
}

} // namespace hise

#endif // HISE_USE_EXTERNAL_AUTOMATION_DATA
