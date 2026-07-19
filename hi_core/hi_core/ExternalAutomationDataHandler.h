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

#ifndef EXTERNALAUTOMATIONDATAHANDLER_H_INCLUDED
#define EXTERNALAUTOMATIONDATAHANDLER_H_INCLUDED

namespace hise { using namespace juce;

#if HISE_USE_EXTERNAL_AUTOMATION_DATA

/** Stores the MIDI controller, macro and MPE assignments in an external JSON file instead of the
    preset. Each file handler (main project or expansion) gets its own file in its root folder.
    Only compiled if HISE_USE_EXTERNAL_AUTOMATION_DATA is enabled.
*/
class ExternalAutomationDataHandler : public ControlledObject,
											 public ExpansionHandler::Listener
{
public:

	static constexpr char automationDataFileName[] = "AutomationData.json";

	ExternalAutomationDataHandler(MainController* mc);

	~ExternalAutomationDataHandler();

	/** RAII helper that suppresses file writes while a programmatic restore is running. */
	struct ScopedRestoreSuspender
	{
		ScopedRestoreSuspender(MainController* mc_);
		~ScopedRestoreSuspender();

	private:

		WeakReference<ExternalAutomationDataHandler> handler;
	};

	/** Enables or disables the feature at runtime. When off, the assignments are stored in the preset. */
	void setEnabled(bool shouldBeEnabled);

	bool isEnabled() const { return enabled; }
	bool isActive() const { return enabled; }
	bool isSuspended() const { return suspendCount > 0; }

	/** Returns the assignment file for the currently active file handler (project or expansion). */
	File getAutomationDataFile() const;

	bool hasExternalFile() const;

	/** Returns the stored section (MidiAutomation, MPEData or macro_controls) or an invalid tree. */
	ValueTree getExternalSection(const Identifier& sectionId) const;

	/** Returns the external tree for the section if the file has it, otherwise the preset fallback. */
	ValueTree getTreeToRestore(const Identifier& sectionId, const ValueTree& presetFallback) const;

	/** Returns true if the assignment sections should be left out of the preset. */
	bool shouldExcludeFromPreset() const;

	/** Restores the assignments from the file into the live handlers (used on expansion / init load). */
	void applyExternalAutomationData();

	/** Called by the assignment mutators on a user change. Writes the file (coalesced). */
	void notifyAutomationDataChanged();

	/** @internal (ExpansionHandler::Listener) */
	void expansionPackLoaded(Expansion* currentExpansion) override;

	static Identifier getMacroSectionId() { static const Identifier id("macro_controls"); return id; }

private:

	friend struct ScopedRestoreSuspender;

	void writeFileNow();
	ValueTree gatherAutomationData() const;

	// lossless ValueTree <-> JSON conversion (type in "_type", children in "_children")
	static var valueTreeToJSON(const ValueTree& v);
	static ValueTree jsonToValueTree(const var& obj);

	bool enabled = true;
	int suspendCount = 0;
	bool savePending = false;

	JUCE_DECLARE_WEAK_REFERENCEABLE(ExternalAutomationDataHandler);
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExternalAutomationDataHandler);
};

/** Writes the external assignment file if the feature is active. Called by the assignment mutators. */
inline void notifyExternalAutomationDataChange(MainController* mc)
{
	if (auto* h = mc->getExternalAutomationDataHandler())
		h->notifyAutomationDataChanged();
}

#else

inline void notifyExternalAutomationDataChange(MainController*) {}

#endif // HISE_USE_EXTERNAL_AUTOMATION_DATA

} // namespace hise

#endif // EXTERNALAUTOMATIONDATAHANDLER_H_INCLUDED
