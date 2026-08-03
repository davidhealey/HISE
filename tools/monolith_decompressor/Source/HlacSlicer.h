/*
    HlacSlicer.h

    Decodes HISE ".ch" monolith files using the real hlac::HiseLosslessAudioFormatReader
    from the hi_lac module (so decoding is bit-exact with what HISE itself would do),
    then either:

    - dumps a whole ".ch" file as one continuous WAV (used when no samplemap could be
      found or matched), or
    - slices it into the original individual samples using the MonolithOffset /
      MonolithLength / SampleRate / FileName metadata from a samplemap ValueTree,
      the same way hise::HlacMonolithInfo does internally.
*/

#pragma once

#include <map>
#include <memory>
#include <string>

#include <JuceHeader.h>
#include "MonolithFileReference.h"

namespace MonolithTool
{
using namespace juce;

struct DecodeLogger
{
    virtual ~DecodeLogger() {}
    virtual void log(const String& message) = 0;
    virtual bool shouldAbort() = 0;
};

class HlacSlicer
{
public:

    explicit HlacSlicer(DecodeLogger& logger_) : logger(logger_) {}

    /** Returns true if every monolith file this samplemap refers to actually exists
        in chFolder. Used to decide whether a recovered/given samplemap actually
        belongs to the .ch files the user pointed at.
    */
    static bool sampleMapMatchesFolder(const ValueTree& sampleMap, const File& chFolder);

    /** Slices every <sample> in the samplemap out of the monolith files in chFolder
        and writes one WAV per original sample (per mic channel) into outputFolder,
        recreating the FileName's relative subfolders. Returns the number of samples
        written.
    */
    int decodeSampleMap(const ValueTree& sampleMap, const File& chFolder, const File& outputFolder);

    /** Decodes every ".ch"-style file found directly in chFolder into one continuous
        WAV each. This is the fallback used when no samplemap is available/matched.
        Returns the number of files written.
    */
    int decodeRawFolder(const File& chFolder, const File& outputFolder);

private:

    hlac::HiseLosslessAudioFormatReader* getReaderForFile(const File& f);

    File resolveOutputFile(const File& outputFolder, const String& fileNameProperty, int fallbackIndex, int fallbackChannel);

    std::map<std::string, std::unique_ptr<hlac::HiseLosslessAudioFormatReader>> readerCache;

    DecodeLogger& logger;
};

} // namespace MonolithTool
