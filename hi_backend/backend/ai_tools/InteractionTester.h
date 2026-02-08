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
class InteractionTestWindow : public PopupLookAndFeel::DocumentWindowWithEmbeddedPopupMenu,
                              public Timer
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
        
        void paint(Graphics& g) override;
        
    private:
        State state = State::Idle;
        Point<int> position{0, 0};
        
        Colour getColourForState() const;
        
        static constexpr float cursorDiameter = 16.0f;
        static constexpr float borderWidth = 2.0f;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CursorOverlay)
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
        
        ResolveResult resolveTarget(const String& componentId, 
                                    Point<float> normalizedPos) override;
        
        void executeMouseDown(Point<int> pixelPos, const String& target,
                              ModifierKeys mods, bool rightClick, int timestampMs) override;
        
        void executeMouseUp(Point<int> pixelPos, const String& target,
                            ModifierKeys mods, bool rightClick, int timestampMs) override;
        
        void executeMouseMove(Point<int> pixelPos, const String& target,
                              ModifierKeys mods, int timestampMs) override;
        
        void executeScreenshot(const String& id, float scale, int timestampMs) override;
        
        // Human intervention detection (disabled for MVP)
        bool wasHumanInterventionDetected() const override { return false; }
        int getInterventionTimestamp() const override { return -1; }
        
        //======================================================================
        /** Get all captured screenshots (id -> base64 PNG with data URI prefix). */
        const StringPairArray& getScreenshots() const { return screenshots; }
        
        /** Enable/disable real cursor mode.
         *  When enabled, the actual OS mouse cursor is moved to match synthetic events.
         *  This is required for modal components (PopupMenu, ComboBox) that poll the
         *  real cursor position via MouseInputSource::getScreenPosition().
         *  Default: true (enabled).
         */
        void setRealCursorMode(bool enabled) { realCursorMode = enabled; }
        bool isRealCursorModeEnabled() const { return realCursorMode; }
        
    private:
        InteractionTestWindow* window;
        ProcessorWithScriptingContent* processor;
        StringPairArray screenshots;  // id -> "data:image/png;base64,..."
        bool mouseCurrentlyDown = false;
        bool realCursorMode = true;  // Move real cursor to match synthetic events
        
        /** Inject a mouse event into the window's ComponentPeer. */
        void injectMouseEvent(Point<float> pixelPos, ModifierKeys mods);
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RealExecutor)
    };
    
    //==========================================================================
    /** Create window for the given script processor's interface. */
    InteractionTestWindow(ProcessorWithScriptingContent* processor);
    ~InteractionTestWindow() override;
    
    void timerCallback() override
    {
        Component::callRecursive<Component>(this, [](Component* c)
        {
            c->repaint();
            return false;
        });
    }

    void closeButtonPressed() override;
    
    /** Get the ScriptContentComponent displaying the interface.
     *  Navigates through FloatingTile -> InterfaceContentPanel -> ScriptContentComponent.
     */
    ScriptContentComponent* getContent();
    
    /** Get the cursor overlay component. */
    CursorOverlay* getOverlay() { return overlay.get(); }
    
    /** Convert normalized position (0-1) to pixel coordinates within content. */
    Point<int> normalizedToPixels(Point<float> norm) const;
    
    /** Get the content size in pixels. */
    Rectangle<int> getContentBounds() const;
    
private:

    /** Container component that holds FloatingTile and overlay. */
    class ContentContainer : public Component
    {
    public:
        ContentContainer() = default;
        void resized() override;
        
        FloatingTile* rootTile = nullptr;
        CursorOverlay* overlay = nullptr;
    };
    
    juce::OpenGLContext context;

    std::unique_ptr<ContentContainer> container;
    std::unique_ptr<FloatingTile> rootTile;
    std::unique_ptr<CursorOverlay> overlay;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InteractionTestWindow)
};



//==============================================================================
/** Main coordinator for interaction testing.
 *
 *  Manages a single implicit test session:
 *  - Creates test window on first interaction request
 *  - Re-creates window if it was closed externally
 *  - Orchestrates parsing, dispatching, and result collection
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
        StringPairArray screenshots;  // id -> base64 PNG
        StringArray parseWarnings;
    };
    
    //==========================================================================
    /** Execute a sequence of interactions.
     *
     *  Creates the test window if needed. Must be called on the message thread
     *  (use SafeAsyncCall from REST handler).
     *
     *  @param interactionsJson  JSON array of interaction objects
     *  @return                  TestResult with success/failure and captured data
     */
    TestResult executeInteractions(const var& interactionsJson);
    
    /** Check if the test window is currently open. */
    bool isWindowOpen() const;
    
    /** Close the test window if open. */
    void closeWindow();
    
private:
    BackendProcessor* backendProcessor;
    std::unique_ptr<InteractionTestWindow> window;
    
    /** Ensure test window is open, creating if needed. */
    void ensureWindowOpen();
    
    /** Get the main interface script processor. */
    JavascriptMidiProcessor* getInterfaceProcessor() const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InteractionTester)
};

} // namespace hise
