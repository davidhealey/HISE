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

namespace hise { using namespace juce;

struct LineDiff
{
	enum class ChangeType
	{
		Unchanged = 0,
		Added = 1,
		Removed = 2,
		Modified,
		numChangeTypes
	};

	static constexpr int NumContextLines = 3;

	struct Line
	{
		ChangeType type;
		juce::String text;
		int oldLine = -1;
		int newLine = -1;
	};

	struct MergeError
	{
		Line mine, theirs;
	};

	struct Change
	{
		/** Creates a string representation using the unified patch format. */
		StringArray toUnifiedPatchFormat() const;

		/** Creates a change object that inverts the changes performed by this object. */
		Change invert() const;

		static std::map<int, ChangeType> createChangeMap(const std::vector<Change>& changes)
		{
			std::map<int, ChangeType> diffLines;

			for (const auto& c : changes)
			{
				auto it = c.lines.begin();
				auto end = c.lines.end();

				int currentLine = -1;

				for (const auto& l : c.lines)
				{
					if (l.type == LineDiff::ChangeType::Unchanged)
						currentLine = l.newLine + 1;

					if (l.type == LineDiff::ChangeType::Added)
					{
						currentLine = l.newLine;

						if (diffLines.find(currentLine) != diffLines.end())
							diffLines[currentLine] = LineDiff::ChangeType::Modified;
						else
							diffLines[currentLine] = l.type;
					}
					if (l.type == LineDiff::ChangeType::Removed)
					{
						diffLines[currentLine] = l.type;
						currentLine++;
					}
				}
			}

			bool lastWasDelete = false;
			int lastDeleteIndex = -1;

			std::map<int, ChangeType> cleaned;

			for (const auto& a : diffLines)
			{
				if (lastWasDelete && a.second == LineDiff::ChangeType::Removed && a.first == lastDeleteIndex + 1)
				{
					lastDeleteIndex = a.first;
					continue;
				}

				cleaned[a.first] = a.second;
				lastWasDelete = a.second == LineDiff::ChangeType::Removed;
				lastDeleteIndex = a.first;
			}

			return cleaned;
		}

		std::vector<Line> lines;
	};

	static bool isTextFile(const File& f);

	static File getPatchFile(const File& patchFile);

	static bool isChangedTextFile(const File& f, int64 prevHash);

	struct HashedDiff : public ReferenceCountedObject
	{
		using Ptr = ReferenceCountedObjectPtr<HashedDiff>;

		/** Creates a HashedDiff object from two string arrays. */
		HashedDiff(const StringArray& A, const StringArray& B);

		/** Creates a HashedDiff object from a patch file. */
		HashedDiff(const String& patch);

		/** Creates a HashedDiff object from a list of lines and a given list of changes. */
		HashedDiff(const StringArray& A, const std::vector<Change>& changes);

		/** Creates a list of changes from A to B in the unified patch format. 
			It also attaches the hashes from A at the end so that it can be 
			used to create a new HashedDiff object.
		*/
		StringArray getPatchReport() const;

		/** Performs a three-way merge with a new set of lines. If there is a merge conflict, 
			the function will throw a MergeError object. 
		*/
		void merge(StringArray& C);

		/** Creates a set of changes from A to B. */
		std::vector<Change> createHunks() const;

		/** Returns the hash map of one of the two string arrays. */
		std::vector<size_t> getHashMap(bool getOld) const;

		/** Creates the content from the hashmap from the string arrays. */
		String getContent(bool getOld) const;

	private:

		std::vector<size_t> toHash(const StringArray& sa);
		std::map<size_t, String> hashmap;
		std::vector<size_t> ah, bh;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HashedDiff);
	};
};

} // namespace hise

