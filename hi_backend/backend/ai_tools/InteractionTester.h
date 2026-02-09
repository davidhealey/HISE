/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   ===========================================================================
*/

#pragma once



namespace hise {
using namespace juce;



// Forward declarations
class BackendProcessor;

//==============================================================================
/** Screenshot metadata (replaces base64 data in API response). */
struct ScreenshotInfo
{
    String id;
    float sizeKB = 0.0f;
    int width = 0;
    int height = 0;
    MemoryBlock pngData;  // Raw PNG data for dump feature (not serialized to JSON)
};

//==============================================================================
/** Window hosting the test interface with cursor overlay.
 *
 *  Creates a DocumentWindow containing:
 *  - FloatingTile with InterfaceContentPanel (mirrors FrontendProcessorEditor structure)
 *  - CursorOverlay on top showing injected mouse events
 *
 *  The FloatingTile hierarchy ensures ScriptContentComponent can find its
 *  expected parent components (required for proper slider/button callbacks).
 *
 *  Window is centered on the primary display and set to always-on-top.
 */
class InteractionTestWindow : public PopupLookAndFeel::DocumentWindowWithEmbeddedPopupMenu
{
public:
    //==========================================================================
    /** Visual cursor overlay showing injected mouse events.
     *
     *  Displays a colored circle at the current mouse position:
     *  - Grey: Idle (between interactions)
     *  - Yellow: Hovering (mouse present, no button pressed)
     *  - Green: Clicking (mouseDown/mouseUp)
     *  - Orange: Dragging (mouseDown while moving)
     */
    class CursorOverlay : public Component
    {
    public:
        enum class State
        {
            Idle,
            Hovering,
            Clicking,
            Dragging
        };
        
        CursorOverlay();
        
        /** Set the current cursor state (affects color). */
        void setState(State newState);
        
        /** Set cursor position in pixel coordinates. */
        void setPosition(Point<int> pixelPos);
        
        /** Get current position in pixels. */
        Point<int> getPosition() const { return position; }
        
        /** Returns true if paint() has been called at least once. */
        bool hasBeenPainted() const { return painted; }
        
        void paint(Graphics& g) override;
        
    private:
        State state = State::Idle;
        Point<int> position{0, 0};
        bool painted = false;
        
        Colour getColourForState() const;
        
        static constexpr float cursorDiameter = 16.0f;
        static constexpr float borderWidth = 2.0f;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CursorOverlay)
    };
    
    //==========================================================================
    /** Status bar showing execution progress at bottom of test window.
     *
     *  Displays:
     *  - State indicator (colored dot + text: IDLE/RUNNING/SUCCESS/FAILED)
     *  - Current action description
     *  - Time-based progress bar
     *  - Step counter (completed/total)
     *  - Screenshot counter with flash effect
     *  - Dump button to save screenshots to folder
     */
    class StatusBar : public Component,
                      public ProgressListener,
                      public Timer,
                      public Button::Listener
    {
    public:
        enum class State { Idle, Running, Success, Failed };
        
        StatusBar();
        ~StatusBar() override;
        
        // Component
        void paint(Graphics& g) override;
        void resized() override;
        
        // ProgressListener
        void onSequenceStarted(int totalInteractions, int estimatedDurationMs) override;
        void onInteractionStarted(int index, const String& description) override;
        void onInteractionCompleted(int index) override;
        void onScreenshotCaptured() override;
        void onSequenceCompleted(bool success, const String& error) override;
        
        // Timer (for animations)
        void timerCallback() override;
        
        // Button::Listener
        void buttonClicked(Button* b) override;
        
        /** Store a screenshot for later dumping.
         *  @param id      Screenshot identifier (used as filename)
         *  @param pngData Raw PNG data
         */
        void storeScreenshot(const String& id, const MemoryBlock& pngData);
        
        /** Clear all stored screenshots. */
        void clearScreenshots();
        
        /** Append JSON response to the log document. */
        void appendToLog(const String& json, bool success);
        
        /** Get the log document for the editor. */
        CodeDocument& getLogDocument() { return logDocument; }
        
        /** Get the tokeniser for syntax highlighting. */
        CodeTokeniser* getLogTokeniser() { return &logTokeniser; }
        
        /** Set the log button toggle state (used when hiding log programmatically). */
        void setLogToggleState(bool state);
        
        static constexpr int HEIGHT = 28;
        
    private:

        float flashAlpha = 0.0f;

        State state = State::Idle;
        String currentAction = "Ready";
        int totalInteractions = 0;
        int completedInteractions = 0;
        int screenshotCount = 0;
        int estimatedDurationMs = 0;
        int64 sequenceStartTime = 0;
        
        // Animation state
        float pulseAlpha = 1.0f;
        bool pulseIncreasing = false;
        
        // Progress (0.0 - 1.0)
        double progress = 0.0;
        
        // Components
        ScopedPointer<HiseShapeButton> dumpButton;
        ScopedPointer<HiseShapeButton> logButton;
        
        // Dump state
        File dumpFolder;
        std::vector<std::pair<String, MemoryBlock>> capturedScreenshots;  // id -> PNG data
        
        // Log console
        CodeDocument logDocument;
        JavascriptTokeniser logTokeniser;
        
        // LAF
        GlobalHiseLookAndFeel laf;
        
        // Path factory for button icons
        struct ButtonPathFactory : public PathFactory
        {
            Path createPath(const String& id) const override;
        };
        
        ButtonPathFactory pathFactory;
        
        // Helpers
        Colour getStateColour() const;
        String getStateText() const;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
    };
    
    //==========================================================================
    /** Executor that injects real mouse events via ComponentPeer.
     *
     *  Implements the InteractionDispatcher::Executor interface to:
     *  - Convert normalized coordinates to pixel positions
     *  - Inject mouse events via ComponentPeer::handleMouseEvent()
     *  - Capture screenshots with overlay temporarily hidden
     *  - Update cursor overlay state for visual feedback
     *
     *  Uses InputSourceType::mouse with index 0 (primary mouse).
     */
    class RealExecutor : public InteractionDispatcher::Executor
    {
    public:
        RealExecutor(InteractionTestWindow* window, ProcessorWithScriptingContent* processor);
        
        //======================================================================
        // Executor interface implementation
        
        ResolveResult resolveTarget(const String& componentId) override;
        
        void executeMouseDown(Point<int> pixelPos, ModifierKeys mods,
                              bool rightClick, int elapsedMs) override;
        
        void executeMouseUp(Point<int> pixelPos, ModifierKeys mods,
                            bool rightClick, int elapsedMs) override;
        
        void executeMouseMove(Point<int> pixelPos, ModifierKeys mods,
                              int elapsedMs) override;
        
        void executeScreenshot(const String& id, float scale, int elapsedMs) override;
        
        void executeSyntheticModeStart(int elapsedMs) override;
        void executeSyntheticModeEnd(int elapsedMs) override;
        
        Point<int> getCurrentCursorPosition() const override;
        void setCursorPosition(Point<int> pos) override;
        
        Array<PopupMenu::VisibleMenuItem> getVisibleMenuItems() const override;
        
        int waitUntilReady() override;
        
        //======================================================================
        /** Get all captured screenshots. */
        const std::map<String, ScreenshotInfo>& getScreenshots() const { return screenshots; }
        
        /** Get MainController for console logging. */
        MainController* getMainController() const override;
        
        //======================================================================
        /** Destructor ensures synthetic mode is cleaned up if still active. */
        ~RealExecutor();
        
    private:
        
        InteractionTestWindow* window;
        ProcessorWithScriptingContent* processor;
        std::map<String, ScreenshotInfo> screenshots;
        bool mouseCurrentlyDown = false;
        Point<int> cursorPosition{0, 0};
        
        // Synthetic input mode state
        bool syntheticModeActive = false;
        ModifierKeys syntheticModifiers;
        
        void beginSyntheticInputMode();
        void endSyntheticInputMode();
        static ModifierKeys getSyntheticModifiersCallback();
        static RealExecutor* activeExecutor;
        
        /** Inject a mouse event into the window's ComponentPeer. */
        void injectMouseEvent(Point<float> pixelPos, ModifierKeys mods);
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RealExecutor)
    };
    
    //==========================================================================
    /** Create window for the given script processor's interface. */
    InteractionTestWindow(ProcessorWithScriptingContent* processor);
    ~InteractionTestWindow() override;
    
    void closeButtonPressed() override;
    void childrenChanged() override;
    void resized() override;
    
    /** Get the ScriptContentComponent displaying the interface.
     *  Navigates through FloatingTile -> InterfaceContentPanel -> ScriptContentComponent.
     */
    ScriptContentComponent* getContent();
    
    /** Get the cursor overlay component. */
    CursorOverlay* getOverlay() { return overlay.get(); }
    
    /** Get the status bar component. */
    StatusBar* getStatusBar() { return statusBar.get(); }
    
    /** Show/hide the log editor overlay. */
    void setLogVisible(bool visible);
    
    /** Check if log is currently visible. */
    bool isLogVisible() const { return logEditor != nullptr; }
    
    /** Convert normalized position (0-1) to pixel coordinates within content. */
    Point<int> normalizedToPixels(Point<float> norm) const;
    
    /** Get the content size in pixels. */
    Rectangle<int> getContentBounds() const;
    
private:

    /** Container component that holds FloatingTile and status bar. */
    class ContentContainer : public Component
    {
    public:
        ContentContainer() = default;
        void resized() override;
        
        FloatingTile* rootTile = nullptr;
        StatusBar* statusBar = nullptr;
    };
    
    juce::OpenGLContext context;

    std::unique_ptr<ContentContainer> container;
    std::unique_ptr<FloatingTile> rootTile;
    std::unique_ptr<CursorOverlay> overlay;
    std::unique_ptr<StatusBar> statusBar;
    std::unique_ptr<CodeEditorComponent> logEditor;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InteractionTestWindow)
};


//==============================================================================
/** Tracks virtual mouse position across API calls. */
struct MouseState
{
    String currentTarget;           // Component ID mouse is over (empty = root/none)
    Point<int> pixelPosition{0, 0}; // Absolute pixel position in Interface coords
    
    void reset() 
    { 
        currentTarget = ""; 
        pixelPosition = {0, 0}; 
    }
};


//==============================================================================
/** Main coordinator for interaction testing.
 *
 *  Manages a single implicit test session:
 *  - Creates test window on first interaction request
 *  - Re-creates window if it was closed externally
 *  - Orchestrates parsing, normalization, dispatching, and result collection
 *  - Maintains mouse state across API calls
 *
 *  Lifetime is tied to the REST server - created when server starts,
 *  destroyed when server stops.
 */
class InteractionTester
{
public:
    explicit InteractionTester(BackendProcessor* bp);
    ~InteractionTester();
    
    //==========================================================================
    /** Result of executing an interaction sequence. */
    struct TestResult
    {
        bool success = false;
        String errorMessage;
        int interactionsCompleted = 0;
        int totalElapsedMs = 0;
        Array<var> executionLog;
        std::map<String, ScreenshotInfo> screenshots;  // id -> metadata + PNG data
        StringArray parseWarnings;
        
        // Menu item selection result
        InteractionDispatcher::SelectedMenuItemInfo selectedMenuItem;
        
        // Final mouse state (when verbose=true)
        MouseState finalMouseState;
    };
    
    //==========================================================================
    /** Execute a sequence of interactions.
     *
     *  Creates the test window if needed. Must be called on the message thread.
     *  Auto-inserts moveTo events as needed for proper mouse positioning.
     *
     *  @param interactionsJson  JSON array of interaction objects
     *  @param verbose           If true, include auto-insertion details in response
     *  @return                  TestResult with success/failure and captured data
     */
    TestResult executeInteractions(const var& interactionsJson, bool verbose = false);
    
    /** Check if the test window is currently open. */
    bool isWindowOpen() const;
    
    /** Close the test window if open. */
    void closeWindow();
    
    /** Get current mouse state (for debugging/verbose output). */
    const MouseState& getMouseState() const { return mouseState; }
    
    /** Reset mouse state to origin. */
    void resetMouseState();
    
    /** Log the JSON response for display in the status bar console. */
    void logResponse(const String& jsonResponse, bool success);
    
    /** Get the test window (for internal use). */
    InteractionTestWindow* getTestWindow() { return window.get(); }
    
private:
    BackendProcessor* backendProcessor;
    std::unique_ptr<InteractionTestWindow> window;
    MouseState mouseState;  // Persists across API calls!
    
    /** Ensure test window is open, creating if needed. */
    void ensureWindowOpen();
    
    /** Get the main interface script processor. */
    JavascriptMidiProcessor* getInterfaceProcessor() const;
    
    /** Normalize sequence by auto-inserting moveTo events.
     *  Modifies interactions array in place.
     *  Uses and updates mouseState.
     */
    void normalizeSequence(Array<InteractionParser::Interaction>& interactions);
    
    /** Result of createMoveToIfNeeded - either contains a moveTo or indicates none needed. */
    struct MoveToResult
    {
        bool needed = false;
        InteractionParser::MouseInteraction moveTo;
    };
    
    /** Check if moveTo needed before interaction, create if so. */
    MoveToResult createMoveToIfNeeded(const InteractionParser::MouseInteraction& next);
    
    /** Update mouse state after interaction. Called during normalization. */
    void updateMouseStateForNormalization(const InteractionParser::MouseInteraction& interaction);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InteractionTester)
};

} // namespace hise
