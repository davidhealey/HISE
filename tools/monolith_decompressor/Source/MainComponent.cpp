/*
    MainComponent.cpp

    See MainComponent.h / Readme.md.
*/

#include "MainComponent.h"

namespace MonolithTool
{

MainComponent::MainComponent() :
    Thread("MonolithDecompressorWorker")
{
    addAndMakeVisible(chFolderLabel);
    addAndMakeVisible(chFolderChooser);
    addAndMakeVisible(mapOrPluginLabel);
    addAndMakeVisible(mapOrPluginChooser);
    addAndMakeVisible(outputFolderLabel);
    addAndMakeVisible(outputFolderChooser);
    addAndMakeVisible(decompressButton);
    addAndMakeVisible(logBox);

    decompressButton.addListener(this);

    logBox.setMultiLine(true);
    logBox.setReadOnly(true);
    logBox.setScrollbarsShown(true);
    logBox.setCaretVisible(false);

    appendToLog("Pick a folder of .ch files, optionally a samplemap (.xml) or the "
                "original compiled plugin (.vst3), and an output folder, then press Decompress.");

    setSize(720, 480);
}

MainComponent::~MainComponent()
{
    stopThread(10000);
}

void MainComponent::paint(Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(10);

    auto row = [&area](int height) { return area.removeFromTop(height); };

    {
        auto r = row(24);
        chFolderLabel.setBounds(r.removeFromLeft(160));
        chFolderChooser.setBounds(r);
    }

    area.removeFromTop(6);

    {
        auto r = row(24);
        mapOrPluginLabel.setBounds(r.removeFromLeft(160));
        mapOrPluginChooser.setBounds(r);
    }

    area.removeFromTop(6);

    {
        auto r = row(24);
        outputFolderLabel.setBounds(r.removeFromLeft(160));
        outputFolderChooser.setBounds(r);
    }

    area.removeFromTop(10);

    decompressButton.setBounds(row(32).removeFromLeft(160));

    area.removeFromTop(10);

    logBox.setBounds(area);
}

void MainComponent::setWorking(bool isWorking)
{
    decompressButton.setEnabled(!isWorking);
    decompressButton.setButtonText(isWorking ? "Working..." : "Decompress");
    chFolderChooser.setEnabled(!isWorking);
    mapOrPluginChooser.setEnabled(!isWorking);
    outputFolderChooser.setEnabled(!isWorking);
}

void MainComponent::appendToLog(const String& message)
{
    logBox.moveCaretToEnd();
    logBox.insertTextAtCaret(message + "\n");
}

void MainComponent::log(const String& message)
{
    Component::SafePointer<MainComponent> safeThis(this);

    MessageManager::callAsync([safeThis, message]
    {
        if (safeThis != nullptr)
            safeThis->appendToLog(message);
    });
}

void MainComponent::buttonClicked(Button* b)
{
    if (b != &decompressButton)
        return;

    auto chFolder = chFolderChooser.getCurrentFile();
    auto mapOrPlugin = mapOrPluginChooser.getCurrentFile();
    auto outputFolder = outputFolderChooser.getCurrentFile();

    if (!chFolder.isDirectory())
    {
        AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Missing input",
            "Please choose a folder that contains the .ch monolith files.");
        return;
    }

    if (outputFolder == File())
    {
        AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Missing output folder",
            "Please choose an output folder.");
        return;
    }

    outputFolder.createDirectory();

    bool haveMapSource = mapOrPlugin != File() && (mapOrPlugin.existsAsFile() || mapOrPlugin.isDirectory());

    // Discovery/matching (and any prompt that depends on it) happens here, on the
    // message thread, before the worker thread starts. That way the worker thread
    // only ever does the slow decode/write work and never needs to show a dialog
    // and block on it (which could deadlock against stopThread() on shutdown).
    pendingSampleMaps.clear();

    if (haveMapSource)
    {
        appendToLog("Looking for a samplemap in " + mapOrPlugin.getFileName() + "...");

        auto sampleMaps = SampleMapSource::loadFrom(mapOrPlugin, *this);

        for (auto& sm : sampleMaps)
            if (HlacSlicer::sampleMapMatchesFolder(sm, chFolder))
                pendingSampleMaps.add(sm);

        if (pendingSampleMaps.isEmpty())
        {
            appendToLog("Could not match any samplemap from " + mapOrPlugin.getFileName() +
                " to the .ch files in " + chFolder.getFullPathName() + ".");

            bool proceed = AlertWindow::showOkCancelBox(AlertWindow::WarningIcon,
                "No matching samplemap found",
                "None of the samplemap(s) found matched the .ch files in the chosen folder.\n\n"
                "Continue and just decompress the raw monolith(s)?",
                "Continue", "Cancel");

            if (!proceed)
            {
                appendToLog("Cancelled.");
                return;
            }
        }
    }
    else
    {
        bool proceed = AlertWindow::showOkCancelBox(AlertWindow::WarningIcon,
            "No samplemap or plugin provided",
            "Without a samplemap (.xml) or the original compiled plugin (.vst3), the individual "
            "samples inside the monolith can't be identified, sliced or named - this will only "
            "produce the raw, continuous decompressed audio for each .ch file.\n\n"
            "Continue and just decompress the raw monolith(s)?",
            "Continue", "Cancel");

        if (!proceed)
        {
            appendToLog("Cancelled.");
            return;
        }
    }

    pendingChFolder = chFolder;
    pendingOutputFolder = outputFolder;

    setWorking(true);
    startThread();
}

void MainComponent::run()
{
    HlacSlicer slicer(*this);

    int numWritten = 0;

    if (!pendingSampleMaps.isEmpty())
    {
        for (auto& sm : pendingSampleMaps)
        {
            if (threadShouldExit())
                break;

            numWritten += slicer.decodeSampleMap(sm, pendingChFolder, pendingOutputFolder);
        }
    }
    else
    {
        numWritten += slicer.decodeRawFolder(pendingChFolder, pendingOutputFolder);
    }

    log("Done. Wrote " + String(numWritten) + " file(s) to " + pendingOutputFolder.getFullPathName());

    Component::SafePointer<MainComponent> safeThis(this);

    MessageManager::callAsync([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->setWorking(false);
    });
}

//==============================================================================

MainWindow::MainWindow(const String& name) :
    DocumentWindow(name, Desktop::getInstance().getDefaultLookAndFeel().findColour(ResizableWindow::backgroundColourId),
                   DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new MainComponent(), true);

    centreWithSize(getWidth(), getHeight());
    setResizable(true, true);
    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
    JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace MonolithTool
