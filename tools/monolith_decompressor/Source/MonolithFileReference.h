/*
    MonolithFileReference.h

    This is a trimmed copy of the hise::MonolithFileReference struct from
    hise/hi_streaming/hi_streaming/MonolithAudioFormat.h. It reproduces the exact
    naming/lookup rules HISE uses to turn a samplemap ID plus mic-channel/split-part
    index into a ".ch" monolith file name, so this tool can find the right files on
    disk without pulling in the rest of the hi_streaming module (which drags in the
    sampler voice/engine classes that this tool has no use for).

    Only the file-naming logic is copied here, not HlacMonolithInfo (which owns the
    HLAC readers) - that part is reimplemented directly against hi_lac in
    HlacSlicer.cpp using the same offsets this struct resolves.
*/

#pragma once

#include <JuceHeader.h>

namespace MonolithTool
{
using namespace juce;

namespace MonolithIds
{
    static const Identifier MonolithSplitAmount("MonolithSplitAmount");
    static const Identifier MonolithSplitIndex("MonolithSplitIndex");
    static const Identifier MonolithReference("MonolithReference");
    static const Identifier MonolithLength("MonolithLength");
    static const Identifier MonolithOffset("MonolithOffset");
    static const Identifier FileName("FileName");
    static const Identifier SampleRate("SampleRate");
}

/** Resolves the ".ch" / ".ch1" / ".ch2a" style monolith file names for a given
    samplemap, mirroring hise::MonolithFileReference exactly.
*/
struct MonolithFileReference
{
    MonolithFileReference(const ValueTree& v);
    MonolithFileReference(int numChannels_, int numParts_);

    bool isMultimic() const noexcept { return numChannels > 1; }
    bool useSplitIndex() const noexcept { return numParts > 0; }
    int getNumMicPositions() const { return numChannels; }
    int getNumSplitParts() const { return numParts; }

    static juce_wchar getCharForSplitPart(int partIndex);
    static int getSplitPartFromChar(juce_wchar splitChar);
    static String getFileExtensionPrefix();

    static String getIdFromValueTree(const ValueTree& v);

    /** Returns the expected file (channelIndex / partIndex must be set beforehand). */
    File getFile(const File& sampleRoot) const;

    /** Returns every monolith file referenced by this samplemap (in channel-major,
        part-minor order), regardless of whether they exist on disk.
    */
    Array<File> getAllFiles(const File& sampleRoot) const;

    String referenceString;
    mutable int channelIndex = 0;
    mutable int partIndex = 0;

private:

    bool bumpToNextMonolith(bool allowChannelBump) const;

    int numParts = 0;
    int numChannels = 1;
};

} // namespace MonolithTool
