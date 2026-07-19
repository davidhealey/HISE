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

#ifndef EXTERNALCONTROLLERASSIGNMENTHANDLER_H_INCLUDED
#define EXTERNALCONTROLLERASSIGNMENTHANDLER_H_INCLUDED

#if HISE_USE_EXTERNAL_ASSIGNMENT_FILE

namespace hise { using namespace juce;

/** Stores the MIDI controller, macro and MPE assignments of the GUI controls in an external
    JSON file instead of writing them into the preset.

    This class is only compiled if HISE_USE_EXTERNAL_ASSIGNMENT_FILE is enabled. It coordinates
    three things:

    1. On save, the assignment sections (MidiAutomation, MPEData and macro_controls) are excluded
       from the preset / plugin state as long as an external file exists.
    2. On load (preset change, DAW session, plugin or expansion load), the assignments are taken
       from the external file (if it exists) rather than the preset. If there is no file, the
       default behaviour of using the preset values is kept.
    3. Whenever the user changes an assignment, the external file is written (creating it if it
       did not exist yet).

    Each file handler (the main project or a full instrument expansion) gets its own assignment
    file, so switching the current expansion switches the assignment set as well.
*/
class ExternalControllerAssignmentHandler : public ControlledObject,
											 public ExpansionHandler::Listener
{
public:

	// The name of the assignment file that is placed in the root of the active file handler.
	static constexpr char assignmentFileName[] = "ControllerAssignments.json";

	ExternalControllerAssignmentHandler(MainController* mc);

	~ExternalControllerAssignmentHandler();

	/** RAII helper that suppresses file writes while a programmatic restore is running. */
	struct ScopedRestoreSuspender
	{
		ScopedRestoreSuspender(MainController* mc_);
		~ScopedRestoreSuspender();

	private:

		WeakReference<ExternalControllerAssignmentHandler> handler;
	};

	/** Enables or disables the entire feature at runtime. When disabled, HISE behaves exactly like
	    the default (the assignments are stored in the preset). */
	void setEnabled(bool shouldBeEnabled);

	bool isEnabled() const { return enabled; }

	/** The feature is active if it is enabled at runtime (the preprocessor already gates compilation). */
	bool isActive() const { return enabled; }

	/** Returns true while a programmatic restore is running (used to skip file writes). */
	bool isSuspended() const { return suspendCount > 0; }

	/** Returns the assignment file for the currently active file handler (project or expansion). */
	File getAssignmentFile() const;

	/** Returns true if an assignment file exists for the currently active file handler. */
	bool hasExternalFile() const;

	/** Returns the stored section (eg. MidiAutomation, MPEData or macro_controls) or an invalid tree. */
	ValueTree getExternalSection(const Identifier& sectionId) const;

	/** Used by the restore functions: if the feature is active and the external file contains the
	    section, the external tree is returned, otherwise the passed preset tree is returned. */
	ValueTree getTreeToRestore(const Identifier& sectionId, const ValueTree& presetFallback) const;

	/** Returns true if the assignment sections should be left out of the preset (feature active and
	    file exists). */
	bool shouldExcludeFromPreset() const;

	/** Restores the assignments from the external file into the live handlers. Used when an
	    expansion is loaded (or at initialisation) where no preset restore is triggered. */
	void applyExternalAssignments();

	/** Called by the assignment mutators when the user changes something. Writes the file
	    (coalesced to a single async write). */
	void notifyAssignmentChanged();

	/** @internal (ExpansionHandler::Listener) */
	void expansionPackLoaded(Expansion* currentExpansion) override;

	// The identifiers of the three stored sections.
	static Identifier getMacroSectionId() { static const Identifier id("macro_controls"); return id; }

private:

	friend struct ScopedRestoreSuspender;

	void writeFileNow();

	// Collects the three assignment sections from the live handlers into a single root tree.
	ValueTree gatherAssignments() const;

	// Lossless conversion between a ValueTree and a JSON friendly var (properties are numbers /
	// strings / bools, children are stored under "_children" with the type kept in "_type").
	static var valueTreeToJSON(const ValueTree& v);
	static ValueTree jsonToValueTree(const var& obj);

	bool enabled = true;
	int suspendCount = 0;
	bool savePending = false;

	JUCE_DECLARE_WEAK_REFERENCEABLE(ExternalControllerAssignmentHandler);
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExternalControllerAssignmentHandler);
};

} // namespace hise

#endif // HISE_USE_EXTERNAL_ASSIGNMENT_FILE

#endif // EXTERNALCONTROLLERASSIGNMENTHANDLER_H_INCLUDED
