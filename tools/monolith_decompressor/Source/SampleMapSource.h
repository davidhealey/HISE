/*
    SampleMapSource.h

    Loads samplemap ValueTree(s) either straight from a .xml samplemap file, or by
    recovering them from a compiled HISE plugin (.vst3).

    Background: HISE always embeds every samplemap into the exported plugin binary
    (hi_backend/backend/CompileExporter.cpp, DEFINE_EMBEDDED_DATA(SampleMaps, ...)).
    The container is a small custom format (see PoolBase::DataProvider::writePool/
    restorePool in hi_core/hi_core/ExternalFilePool.cpp):

        int64   metadataSize
        bytes   zstd frame (no dictionary) of a ValueTree named "PoolData" that lists
                each embedded samplemap's ID / ChunkStart / ChunkEnd
        bytes   the samplemaps themselves, each a zstd frame (compressed with the
                dictionary from SampleMapDictionaryProvider, see SampleMapDictionary.h)
                back to back, sliced according to the metadata above

    Since a compiled plugin is not required to keep this at a fixed, discoverable
    offset (and may be stripped of symbols), this class finds it heuristically: it
    scans the binary for zstd frame headers and tries to parse+validate each
    candidate. This is best-effort - see the Readme for details/limitations.
*/

#pragma once

#include <JuceHeader.h>
#include "HlacSlicer.h"

namespace MonolithTool
{
using namespace juce;

struct SampleMapSource
{
    /** Loads every recoverable samplemap from a .xml file (returns just that one
        samplemap) or a .vst3 plugin (bundle folder or flat binary).
    */
    static Array<ValueTree> loadFrom(const File& xmlOrVst3, DecodeLogger& logger);

private:

    static Array<ValueTree> extractFromVst3(const File& vst3Path, DecodeLogger& logger);
    static Array<File> collectBinaryFiles(const File& vst3Path);
    static Array<ValueTree> scanBufferForSampleMaps(const MemoryBlock& data, DecodeLogger& logger);
};

} // namespace MonolithTool
