/*
    Main.cpp

    Application entry point for the HISE monolith decompressor tool.
*/

#include <JuceHeader.h>
#include "MainComponent.h"

class MonolithDecompressorApplication : public juce::JUCEApplication
{
public:

    MonolithDecompressorApplication() = default;

    const juce::String getApplicationName() override { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow.reset(new MonolithTool::MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:

    std::unique_ptr<MonolithTool::MainWindow> mainWindow;
};

START_JUCE_APPLICATION(MonolithDecompressorApplication)
