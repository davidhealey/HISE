/*
    SampleMapSource.cpp

    See SampleMapSource.h for the container format this parses.
*/

#include "SampleMapSource.h"
#include "SampleMapDictionary.h"

namespace MonolithTool
{

// Matches hise::SampleMapDictionaryProvider (hi_tools/hi_binary_data/BinaryDataDictionaries.h)
// but only needs the dictionary bytes, which are vendored in SampleMapDictionary.h.
struct LocalSampleMapDictionaryProvider : public zstd::DictionaryProviderBase<void>
{
    LocalSampleMapDictionaryProvider(InputStream* = nullptr) : zstd::DictionaryProviderBase<void>(nullptr) {}

    MemoryBlock createDictionaryData() override
    {
        return MemoryBlock(kSampleMapZstdDictionary, kSampleMapZstdDictionarySize);
    }
};

static int64 readInt64LE(const uint8* p)
{
    int64 v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | (int64)p[i];
    return v;
}

Array<ValueTree> SampleMapSource::loadFrom(const File& xmlOrVst3, DecodeLogger& logger)
{
    Array<ValueTree> result;

    if (xmlOrVst3.hasFileExtension(".xml"))
    {
        if (auto xml = XmlDocument::parse(xmlOrVst3))
        {
            auto v = ValueTree::fromXml(*xml);

            if (v.isValid())
                result.add(v);
            else
                logger.log("Could not parse " + xmlOrVst3.getFullPathName() + " as a samplemap.");
        }
        else
        {
            logger.log("Could not parse XML file: " + xmlOrVst3.getFullPathName());
        }

        return result;
    }

    return extractFromVst3(xmlOrVst3, logger);
}

Array<File> SampleMapSource::collectBinaryFiles(const File& vst3Path)
{
    Array<File> files;

    if (vst3Path.isDirectory())
    {
        // A .vst3 bundle: scan every file inside Contents/ rather than assuming a
        // fixed per-platform layout, and skip tiny plist/resource files up front.
        for (const auto& entry : RangedDirectoryIterator(vst3Path, true, "*", File::findFiles))
        {
            auto f = entry.getFile();

            if (f.getSize() > 1024 * 64)
                files.add(f);
        }
    }
    else if (vst3Path.existsAsFile())
    {
        files.add(vst3Path);
    }

    return files;
}

Array<ValueTree> SampleMapSource::scanBufferForSampleMaps(const MemoryBlock& data, DecodeLogger& logger)
{
    Array<ValueTree> found;

    const auto* bytes = static_cast<const uint8*>(data.getData());
    const int64 total = (int64)data.getSize();

    // ZSTD_MAGICNUMBER (0xFD2FB528) as it appears in a little-endian byte stream.
    const uint8 zstdMagic[4] = { 0x28, 0xB5, 0x2F, 0xFD };

    for (int64 i = 8; i < total - 4; i++)
    {
        if (bytes[i] != zstdMagic[0] || bytes[i + 1] != zstdMagic[1] ||
            bytes[i + 2] != zstdMagic[2] || bytes[i + 3] != zstdMagic[3])
            continue;

        auto metadataSize = readInt64LE(bytes + i - 8);

        if (metadataSize <= 4 || metadataSize > (total - i) || metadataSize > (int64)200 * 1024 * 1024)
            continue;

        MemoryBlock metadataBlock(bytes + i, (size_t)metadataSize);

        ValueTree poolMetadata;
        zstd::ZDefaultCompressor mDecomp;

        if (!mDecomp.expand(metadataBlock, poolMetadata).wasOk() || !poolMetadata.isValid())
            continue;

        if (poolMetadata.getType() != Identifier("PoolData"))
            continue;

        const int64 itemDataStart = i + metadataSize;

        zstd::ZCompressor<LocalSampleMapDictionaryProvider> itemDecompressor;

        int numRecovered = 0;

        for (const auto& item : poolMetadata)
        {
            auto chunkStart = (int64)item.getProperty("ChunkStart");
            auto chunkEnd = (int64)item.getProperty("ChunkEnd");

            if (chunkStart < 0 || chunkEnd <= chunkStart)
                continue;

            auto absStart = itemDataStart + chunkStart;
            auto absEnd = itemDataStart + chunkEnd;

            if (absStart < 0 || absEnd > total)
                continue;

            MemoryBlock itemBlock(bytes + absStart, (size_t)(absEnd - absStart));

            ValueTree sampleMap;

            if (!itemDecompressor.expand(itemBlock, sampleMap).wasOk() || !sampleMap.isValid())
                continue;

            bool looksLikeSampleMap = sampleMap.getType() == Identifier("samplemap") ||
                (sampleMap.getNumChildren() > 0 && sampleMap.getChild(0).hasProperty(MonolithIds::MonolithOffset));

            if (!looksLikeSampleMap)
                continue;

            found.add(sampleMap);
            numRecovered++;
        }

        if (numRecovered > 0)
        {
            logger.log("Found an embedded SampleMapPool at offset " + String(i - 8) +
                " with " + String(numRecovered) + " samplemap(s).");
        }
    }

    return found;
}

Array<ValueTree> SampleMapSource::extractFromVst3(const File& vst3Path, DecodeLogger& logger)
{
    Array<ValueTree> result;

    auto binaries = collectBinaryFiles(vst3Path);

    if (binaries.isEmpty())
    {
        logger.log("Could not find any binary content in " + vst3Path.getFullPathName());
        return result;
    }

    for (const auto& bin : binaries)
    {
        if (logger.shouldAbort())
            break;

        logger.log("Scanning " + bin.getFileName() + " (" + String(bin.getSize() / 1024) + " kB)...");

        MemoryBlock data;

        if (!bin.loadFileAsData(data))
        {
            logger.log("Could not read " + bin.getFullPathName());
            continue;
        }

        result.addArray(scanBufferForSampleMaps(data, logger));
    }

    if (result.isEmpty())
        logger.log("No embedded samplemaps could be recovered from " + vst3Path.getFullPathName());

    return result;
}

} // namespace MonolithTool
