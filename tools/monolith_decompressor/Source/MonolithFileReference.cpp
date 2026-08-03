/*
    MonolithFileReference.cpp

    See MonolithFileReference.h for provenance. Logic mirrors
    hise::MonolithFileReference in hise/hi_streaming/hi_streaming/MonolithAudioFormat.cpp.
*/

#include "MonolithFileReference.h"

namespace MonolithTool
{

String MonolithFileReference::getFileExtensionPrefix()
{
    return String("ch");
}

String MonolithFileReference::getIdFromValueTree(const ValueTree& v)
{
    if (v.hasProperty(MonolithIds::MonolithReference))
        return v[MonolithIds::MonolithReference].toString();

    return v["ID"].toString();
}

juce_wchar MonolithFileReference::getCharForSplitPart(int partIndex)
{
    if (partIndex == -1)
        return 0;

    return (juce_wchar)jlimit(0, 26, partIndex) + 'a';
}

int MonolithFileReference::getSplitPartFromChar(juce_wchar splitChar)
{
    return (int)splitChar - 'a';
}

File MonolithFileReference::getFile(const File& sampleRoot) const
{
    jassert(referenceString.isNotEmpty());

    auto path = referenceString.replace("/", "_");

    auto extension = getFileExtensionPrefix();

    if (isMultimic())
    {
        extension << String(channelIndex + 1);

        if (useSplitIndex())
            extension << getCharForSplitPart(partIndex);
    }
    else if (useSplitIndex())
    {
        extension << String(partIndex + 1);
    }
    else
        extension << String(1);

    path << "." << extension;

    return sampleRoot.getChildFile(path);
}

Array<File> MonolithFileReference::getAllFiles(const File& sampleRoot) const
{
    channelIndex = 0;
    partIndex = 0;

    Array<File> filesToLoad;

    filesToLoad.addIfNotAlreadyThere(getFile(sampleRoot));

    while (bumpToNextMonolith(true))
        filesToLoad.addIfNotAlreadyThere(getFile(sampleRoot));

    return filesToLoad;
}

bool MonolithFileReference::bumpToNextMonolith(bool allowChannelBump) const
{
    if (useSplitIndex())
    {
        if (isPositiveAndBelow(partIndex, numParts - 1))
        {
            partIndex++;
            return true;
        }

        if (!isMultimic() || !allowChannelBump)
            return false;

        partIndex = 0;
    }

    if (!allowChannelBump)
        return false;

    if (isPositiveAndBelow(channelIndex, numChannels - 1))
    {
        channelIndex++;
        return true;
    }

    return false;
}

MonolithFileReference::MonolithFileReference(int numChannels_, int numParts_) :
    numChannels(jmax(1, numChannels_)),
    numParts(numParts_)
{
}

MonolithFileReference::MonolithFileReference(const ValueTree& v)
{
    numChannels = jmax(1, v.getChild(0).getNumChildren());
    numParts = v.getProperty(MonolithIds::MonolithSplitAmount, 0);
    referenceString = getIdFromValueTree(v);
}

} // namespace MonolithTool
