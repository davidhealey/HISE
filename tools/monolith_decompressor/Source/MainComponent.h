/*
    MainComponent.h

    GUI for the monolith decompressor tool. See Readme.md in this folder for what
    the tool does and its limitations.
*/

#pragma once

#include <JuceHeader.h>
#include "HlacSlicer.h"
#include "SampleMapSource.h"

namespace MonolithTool
{
using namespace juce;

class MainComponent : public Component,
                       public Button::Listener,
                       private Thread,
                       private DecodeLogger
{
public:

    MainComponent();
    ~MainComponent() override;

    void paint(Graphics& g) override;
    void resized() override;

    void buttonClicked(Button* b) override;

private:

    // DecodeLogger
    void log(const String& message) override;
    bool shouldAbort() override { return threadShouldExit(); }

    // Thread
    void run() override;

    void appendToLog(const String& message);
    void setWorking(bool isWorking);

    // FilenameComponent(name, currentFile, canEditFilename, isDirectory, isForSaving, wildcard, enforcedSuffix, textWhenNothingSelected)
    FilenameComponent chFolderChooser { "chFolder", {}, true, true, false, {}, {}, "Choose the folder containing the .ch monolith files" };
    FilenameComponent mapOrPluginChooser { "mapOrPlugin", {}, true, false, false, "*.xml;*.vst3", {}, "Optional: samplemap XML or compiled plugin" };
    FilenameComponent outputFolderChooser { "outputFolder", {}, true, true, true, {}, {}, "Choose where the decompressed WAV files should go" };

    Label chFolderLabel { {}, "CH folder:" };
    Label mapOrPluginLabel { {}, "SampleMap / VST3:" };
    Label outputFolderLabel { {}, "Output folder:" };

    TextButton decompressButton { "Decompress" };
    TextEditor logBox;

    // Filled in on the message thread (in buttonClicked) before the worker thread
    // starts, which only ever does the (potentially slow) decode/write work -
    // discovery, matching and any user prompts happen synchronously beforehand so
    // the worker thread never needs to show a dialog and wait on it.
    File pendingChFolder, pendingOutputFolder;
    Array<ValueTree> pendingSampleMaps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

class MainWindow : public DocumentWindow
{
public:
    MainWindow(const String& name);
    void closeButtonPressed() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace MonolithTool
