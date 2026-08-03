# Monolith decompressor

A small standalone GUI tool that decompresses HISE ".ch" monolith files back into
individual WAV files.

## What it does

Give it:

- a folder containing the ".ch" (HLAC) monolith files for one instrument, and
- either a samplemap ".xml" file, the original compiled plugin (".vst3"), or nothing,
- plus an output folder,

and it will:

1. If a samplemap XML was given: parse it directly.
2. If a compiled plugin was given: HISE always embeds every samplemap it uses into
   the exported binary (see `hi_backend/backend/CompileExporter.cpp`, search for
   `DEFINE_EMBEDDED_DATA(hise::FileHandlerBase::SampleMaps ...)`). This tool scans
   the plugin binary for that embedded data and recovers the samplemap(s) from it.
   See "How VST3 recovery works" below for the details and its limitations.
3. If a samplemap could be found and it matches the ".ch" files in the given folder
   (i.e. every monolith file it references actually exists there), it slices out
   each original sample using the samplemap's `MonolithOffset` / `MonolithLength` /
   `SampleRate` / `FileName` metadata, recreating the original file names and
   folder structure under the output folder.
4. If no samplemap/plugin was given, or none of the samplemaps found could be
   matched to the chosen folder, you get a warning explaining that the individual
   samples cannot be identified or named without one. If you continue anyway, each
   ".ch" file is decompressed as a single continuous WAV (this is exactly the
   decompressed monolith stream with all its original samples still concatenated
   back to back, but with no idea where one sample ends and the next begins).

## Why a samplemap is needed to get individual samples back

A ".ch" file is just every sample assigned to a sampler concatenated together and
losslessly compressed with HLAC (see `hi_lac/hlac/HiseLosslessAudioFormat.cpp`).
The compressed stream itself does not store where one original sample ends and the
next begins, what it was called, or (for very old monolith files, format version
1) even its real sample rate - all of that lives only in the samplemap that was
exported alongside it (`MonolithOffset`, `MonolithLength`, `SampleRate`, `FileName`
per `<sample>`, see `hi_streaming/hi_streaming/MonolithAudioFormat.cpp`). Without
that metadata, decompression can only recover the raw, continuous audio.

## How VST3 recovery works (and its limits)

Every samplemap gets embedded into the exported plugin as a small custom container
(see `PoolBase::DataProvider::writePool` / `restorePool` in
`hi_core/hi_core/ExternalFilePool.cpp`):

```
int64   size of the metadata block
bytes   a zstd frame (no dictionary) containing a ValueTree named "PoolData" that
        lists every embedded samplemap's ID and its byte range in the data below
bytes   the samplemaps themselves, each its own zstd frame, compressed with a
        fixed dictionary (SampleMapDictionaryProvider, vendored in
        Source/SampleMapDictionary.h so this tool doesn't need to link hi_tools)
```

There is no guaranteed fixed offset for this container inside a compiled plugin
binary, so this tool finds it by scanning the binary for zstd frame headers and
attempting to parse + validate each candidate (see `Source/SampleMapSource.cpp`).
This works well against a normal HISE export because:

- the container format and its dictionary are not encrypted or obfuscated, and
- a plausible-looking zstd frame that isn't actually a HISE pool container will
  almost always fail to decompress into a valid "PoolData" ValueTree, so false
  positives are filtered out automatically.

It will *not* find anything if:

- the plugin was built with a custom fork that encrypts/obfuscates embedded
  resources, or
- the samplemap data was stripped from the binary after compilation, or
- the plugin ships as a Windows/Linux ".vst3" *folder* bundle and you only used
  the Browse button (native file choosers on Windows/Linux can't select a folder
  as a "file") - in that case, type or paste the folder path directly into the
  "SampleMap / VST3" field instead of using Browse.

## Building

This is a normal Projucer project (like the other tools in this folder), not part
of the main HISE build. Open `MonolithDecompressor.jucer` in Projucer, point the
module paths at your local JUCE checkout if needed, and build the generated
Xcode/Visual Studio project. It only depends on `hi_lac` and `hi_zstd` (both are
lightweight, self-contained HISE modules) plus standard JUCE modules - it does not
link `hi_core`, `hi_streaming` or `hi_tools`, so it builds quickly on its own.

## Limitations

- Only handles the standard HLAC monolith format HISE itself produces. Custom
  forks that change the compression or embedding scheme are not supported.
- VST3 recovery is best-effort/heuristic, as described above.
- A samplemap that references a monolith split across multiple ".ch" parts or
  multiple mic positions is supported (the file naming logic mirrors
  `hise::MonolithFileReference` exactly), but every part must be present in the
  chosen folder for that samplemap to be used.
