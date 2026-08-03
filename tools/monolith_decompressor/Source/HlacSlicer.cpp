/*
    HlacSlicer.cpp

    See HlacSlicer.h. The per-sample slicing mirrors
    hise::HlacMonolithInfo::fillMetadataInfo / createFallbackReader from
    hise/hi_streaming/hi_streaming/MonolithAudioFormat.cpp, but is written directly
    against hi_lac (HiseLosslessAudioFormatReader / HlacSubSectionReader) so this tool
    does not need to link the rest of hi_streaming.
*/

#include "HlacSlicer.h"

namespace MonolithTool
{

static String sanitizeRelativePath(String fileNameProperty)
{
    // Samplemaps store paths like "{PROJECT_FOLDER}Electric Guitars/Note_A3.wav" or
    // absolute paths from the machine that imported them. We only care about a
    // relative path we can safely recreate under the output folder.
    auto path = fileNameProperty;

    path = path.replace("{PROJECT_FOLDER}", "");
    path = path.replace("{EXP_FOLDER}", "");
    path = path.replace("\\", "/");

    // Strip a leading drive letter or absolute root so File::getChildFile() can't
    // escape the output folder.
    if (path.substring(1, 3) == ":/")
        path = path.substring(3);

    while (path.startsWith("/"))
        path = path.substring(1);

    StringArray parts;
    parts.addTokens(path, "/", "");
    parts.removeString("..");
    parts.removeEmptyStrings();

    return parts.joinIntoString("/");
}

File HlacSlicer::resolveOutputFile(const File& outputFolder, const String& fileNameProperty, int fallbackIndex, int fallbackChannel)
{
    auto relative = sanitizeRelativePath(fileNameProperty);

    File target;

    if (relative.isNotEmpty())
    {
        target = outputFolder.getChildFile(relative);

        if (target.getFileExtension().isEmpty())
            target = target.withFileExtension(".wav");
    }
    else
    {
        target = outputFolder.getChildFile("Sample_" + String(fallbackIndex).paddedLeft('0', 5) +
            "_ch" + String(fallbackChannel + 1) + ".wav");
    }

    target.getParentDirectory().createDirectory();

    return target;
}

hlac::HiseLosslessAudioFormatReader* HlacSlicer::getReaderForFile(const File& f)
{
    auto key = f.getFullPathName().toStdString();

    auto it = readerCache.find(key);

    if (it != readerCache.end())
        return it->second.get();

    if (!f.existsAsFile())
    {
        logger.log("Missing monolith file: " + f.getFullPathName());
        return nullptr;
    }

    auto* fis = new FileInputStream(f);

    if (!fis->openedOk())
    {
        delete fis;
        logger.log("Could not open: " + f.getFullPathName());
        return nullptr;
    }

    auto reader = std::make_unique<hlac::HiseLosslessAudioFormatReader>(fis);
    auto* rawPtr = reader.get();
    readerCache[key] = std::move(reader);
    return rawPtr;
}

// static
bool HlacSlicer::sampleMapMatchesFolder(const ValueTree& sampleMap, const File& chFolder)
{
    MonolithFileReference ref(sampleMap);

    if (ref.referenceString.isEmpty())
        return false;

    auto files = ref.getAllFiles(chFolder);

    if (files.isEmpty())
        return false;

    for (auto& f : files)
        if (!f.existsAsFile())
            return false;

    return true;
}

int HlacSlicer::decodeSampleMap(const ValueTree& sampleMap, const File& chFolder, const File& outputFolder)
{
    MonolithFileReference ref(sampleMap);

    WavAudioFormat wavFormat;

    int numWritten = 0;

    for (int i = 0; i < sampleMap.getNumChildren(); i++)
    {
        if (logger.shouldAbort())
            break;

        auto sample = sampleMap.getChild(i);

        if (!sample.hasProperty(MonolithIds::MonolithOffset) || !sample.hasProperty(MonolithIds::MonolithLength))
        {
            logger.log("Sample " + String(i) + " has no monolith metadata, skipping.");
            continue;
        }

        int64 offset = (int64)sample.getProperty(MonolithIds::MonolithOffset);
        int64 length = (int64)sample.getProperty(MonolithIds::MonolithLength);
        int splitIndex = sample.getProperty(MonolithIds::MonolithSplitIndex, 0);
        double sampleRate = (double)sample.getProperty(MonolithIds::SampleRate, 0.0);

        auto numChannels = ref.getNumMicPositions();

        for (int ch = 0; ch < numChannels; ch++)
        {
            ref.channelIndex = ch;
            ref.partIndex = splitIndex;

            auto monolithFile = ref.getFile(chFolder);
            auto* reader = getReaderForFile(monolithFile);

            if (reader == nullptr)
                continue;

            if (sampleRate > 0.0)
                reader->sampleRate = sampleRate;

            hlac::HlacSubSectionReader subReader(reader, offset, length);

            String fileNameProperty = numChannels == 1 ? sample.getProperty(MonolithIds::FileName).toString()
                                                        : sample.getChild(ch).getProperty(MonolithIds::FileName).toString();

            auto targetFile = resolveOutputFile(outputFolder, fileNameProperty, i, ch);

            targetFile.deleteFile();

            // WavAudioFormat::createWriterFor() takes ownership of the stream it is
            // given (on success or failure alike, matching every other AudioFormatWriter
            // use in this codebase, e.g. CompressionHelpers::dump), so this is not leaked.
            auto* fos = new FileOutputStream(targetFile);

            std::unique_ptr<AudioFormatWriter> writer(wavFormat.createWriterFor(fos, subReader.sampleRate,
                (unsigned int)subReader.numChannels, (int)subReader.bitsPerSample, {}, 0));

            if (writer == nullptr)
            {
                logger.log("Could not create WAV writer for " + targetFile.getFullPathName());
                continue;
            }

            if (writer->writeFromAudioReader(subReader, 0, subReader.lengthInSamples))
            {
                numWritten++;
                logger.log("Wrote " + targetFile.getRelativePathFrom(outputFolder));
            }
            else
            {
                logger.log("Failed to write " + targetFile.getFullPathName());
            }
        }
    }

    return numWritten;
}

int HlacSlicer::decodeRawFolder(const File& chFolder, const File& outputFolder)
{
    auto chFiles = chFolder.findChildFiles(File::findFiles, false, "*.ch*;*.hlac");

    if (chFiles.isEmpty())
    {
        logger.log("No .ch/.hlac files found in " + chFolder.getFullPathName());
        return 0;
    }

    logger.log("No samplemap available: decompressing each monolith file as a single "
                "continuous WAV. Sample boundaries, names and (for pre-v2 monoliths) the "
                "sample rate cannot be recovered without a samplemap.");

    WavAudioFormat wavFormat;
    int numWritten = 0;

    for (auto& f : chFiles)
    {
        if (logger.shouldAbort())
            break;

        auto* fis = new FileInputStream(f);

        if (!fis->openedOk())
        {
            delete fis;
            logger.log("Could not open: " + f.getFullPathName());
            continue;
        }

        hlac::HiseLosslessAudioFormatReader reader(fis);

        auto targetFile = outputFolder.getChildFile(f.getFileNameWithoutExtension() + "_" + f.getFileExtension().substring(1) + ".wav");
        targetFile.getParentDirectory().createDirectory();
        targetFile.deleteFile();

        auto* fos = new FileOutputStream(targetFile);

        std::unique_ptr<AudioFormatWriter> writer(wavFormat.createWriterFor(fos, reader.sampleRate,
            (unsigned int)reader.numChannels, (int)reader.bitsPerSample, {}, 0));

        if (writer == nullptr)
        {
            logger.log("Could not create WAV writer for " + targetFile.getFullPathName());
            continue;
        }

        logger.log("Decompressing " + f.getFileName() + " (" + String(reader.lengthInSamples) + " samples, "
                    + String(reader.sampleRate, 0) + " Hz, " + String((int)reader.numChannels) + "ch)...");

        if (writer->writeFromAudioReader(reader, 0, reader.lengthInSamples))
        {
            numWritten++;
            logger.log("Wrote " + targetFile.getFileName());
        }
        else
        {
            logger.log("Failed to write " + targetFile.getFullPathName());
        }
    }

    return numWritten;
}

} // namespace MonolithTool
