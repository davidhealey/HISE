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
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licensing:
*
*   http://www.hise.audio/
*
*   HISE is based on the JUCE library,
*   which must be separately licensed for closed source applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

#pragma once

namespace hise { using namespace juce;

// Forward declaration for ReplayConsoleHandler
class InteractionTestWindow;



/** Helper utilities for REST API request handling.
    
    This struct contains utilities for handling REST API requests that interact
    with HISE's scripting system, including console output capture, error parsing,
    and script compilation. It also provides the central registry of all API routes
    and their handler implementations.
*/
struct RestHelpers
{
    //==========================================================================
    // Route types (nested to avoid polluting hise namespace)
    
    /** Identifies REST API endpoints. The enum value is the index into getRouteMetadata(). */
    enum class ApiRoute
    {
        ListMethods,            ///< GET  /           - List all available API methods
        Status,                 ///< GET  /api/status - Get project status
        GetScript,              ///< GET  /api/get_script - Read script content
        SetScript,              ///< POST /api/set_script - Update script content
        EvaluateREPL,           ///< POST /api/repl - Evaluate an expression and get the result
        Recompile,              ///< POST /api/recompile - Recompile a processor
        ListComponents,         ///< GET  /api/list_components - List UI components
        GetComponentProperties, ///< GET  /api/get_component_properties - Get component properties
        GetComponentValue,      ///< GET  /api/get_component_value - Get component runtime value
        SetComponentValue,      ///< POST /api/set_component_value - Set component runtime value
        SetComponentProperties, ///< POST /api/set_component_properties - Set component properties
        Screenshot,             ///< GET  /api/screenshot - Capture UI screenshot
        GetSelectedComponents,  ///< GET  /api/get_selected_components - Get selected UI components
        SimulateInteractions,   ///< POST /api/simulate_interactions - Execute UI interaction sequence
        DiagnoseScript,         ///< POST /api/diagnose_script - Run diagnostic shadow parse
        GetIncludedFiles,       ///< GET  /api/get_included_files - List included script files
        StartProfiling,         ///< POST /api/profile - Run profiling session or retrieve last result
        ParseCSS,               ///< POST /api/parse_css - Parse CSS and return diagnostics
        Shutdown,               ///< POST /api/shutdown - Gracefully quit HISE
        BuilderTree,            ///< GET  /api/builder/tree - Get module tree hierarchy
        BuilderApply,           ///< POST /api/builder/apply - Apply operations to module tree
        UndoPushGroup,          ///< POST /api/undo/push_group - Start a new undo group
        UndoPopGroup,           ///< POST /api/undo/pop_group - End group, execute or discard
        UndoBack,               ///< POST /api/undo/back - Undo last action
        UndoForward,            ///< POST /api/undo/forward - Redo next action
        UndoDiff,               ///< GET  /api/undo/diff - Current diff state
        UndoHistory,            ///< GET  /api/undo/history - Full undo history
        UndoClear,              ///< POST /api/undo/clear - Clear undo history
        numRoutes
    };
    
    /** Metadata for a REST API route parameter. Uses fluent builder pattern. */
    struct RouteParameter
    {
        Identifier name;
        String description;
        String defaultValue;  ///< Empty = no default
        bool required = true;
        
        /** Constructor with required fields. */
        RouteParameter(const Identifier& name_, const String& desc_)
            : name(name_), description(desc_) {}
        
        /** Set a default value (implies optional). */
        RouteParameter withDefault(const String& def) const
        {
            auto copy = *this;
            copy.defaultValue = def;
            copy.required = false;
            return copy;
        }
        
        /** Mark as optional without a default value. */
        RouteParameter asOptional() const
        {
            auto copy = *this;
            copy.required = false;
            return copy;
        }
    };
    
    /** Metadata for a registered REST API route. Uses fluent builder pattern. */
    struct RouteMetadata
    {
        ApiRoute id = ApiRoute::numRoutes;
        String path;              ///< e.g., "api/status" (without leading /)
        RestServer::Method method = RestServer::GET;
        String category;          ///< "status", "scripting", "ui"
        String description;       ///< One-liner description
        String returns;           ///< One-liner describing response
        Array<RouteParameter> queryParameters;   ///< For GET requests
        Array<RouteParameter> bodyParameters;    ///< For POST requests (JSON body)
        
        /** Default constructor for juce::Array compatibility. */
        RouteMetadata() = default;
        
        /** Constructor with required fields. */
        RouteMetadata(ApiRoute id_, const String& path_)
            : id(id_), path(path_) {}
        
        RouteMetadata withMethod(RestServer::Method m) const
        {
            auto copy = *this;
            copy.method = m;
            return copy;
        }
        
        RouteMetadata withCategory(const String& cat) const
        {
            auto copy = *this;
            copy.category = cat;
            return copy;
        }
        
        RouteMetadata withDescription(const String& desc) const
        {
            auto copy = *this;
            copy.description = desc;
            return copy;
        }
        
        RouteMetadata withReturns(const String& ret) const
        {
            auto copy = *this;
            copy.returns = ret;
            return copy;
        }
        
        RouteMetadata withQueryParam(const RouteParameter& p) const
        {
            auto copy = *this;
            copy.queryParameters.add(p);
            return copy;
        }
        
        RouteMetadata withBodyParam(const RouteParameter& p) const
        {
            auto copy = *this;
            copy.bodyParameters.add(p);
            return copy;
        }
        
        /** Convenience: adds standard moduleId query parameter. */
        RouteMetadata withModuleIdParam() const
        {
            return withQueryParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID"));
        }
    };
    
    //==========================================================================
    
    /** Base class for scoped console capture.
     *  
     *  Captures console output during its lifetime by setting a custom logger
     *  on the MainController's CodeHandler. Only captures if enabled AND no
     *  existing custom logger is set.
     *
     *  Subclasses implement handleMessage() and handleError() to process captured output.
     */
    struct BaseScopedConsoleHandler : public ControlledObject
    {
        BaseScopedConsoleHandler(MainController* mc, bool enabled);
        virtual ~BaseScopedConsoleHandler();
        
        bool isCapturing() const { return capturing; }
        
    protected:
        virtual void handleMessage(const String& message) = 0;
        virtual void handleError(const String& message, const StringArray& callstack) = 0;
        
        // Error parsing utilities
        struct ParsedError
        {
            String message;      // "API call with undefined parameter 0"
            String location;     // "Scripts/funky.js:9:16"
            String functionName; // "dudel" (empty if not a callstack entry)
            
            /** Returns formatted callstack entry: "dudel() at Scripts/funky.js:9:16"
                or just location if no function name. */
            String toCallstackString() const;
        };
        
        /** Parses an error string and extracts message, location, and optional function name.
            
            Handles both formats:
            - Error message: "API call with undefined parameter 0 {{SW50ZXJm...}}"
            - Callstack entry: ":\t\t\tdudel() - funky.js (9)\t{{SW50ZXJm...}}"
            
            @param errorString   The full error/callstack string
            @param scriptRoot    The project's Scripts folder for resolving full paths
            @param moduleId      The script processor's module ID - used as fallback filename
            
            @returns ParsedError with message, location, and optional functionName
        */
        ParsedError parseError(const String& errorString,
                               const File& scriptRoot,
                               const String& moduleId);
        
    private:
        void onMessage(const String& message, int warning, const Processor* p);
        bool capturing = false;
    };
    
    //==========================================================================
    
    /** Scoped handler that captures console output during request processing.
        
        While this object exists, any Console.print() calls or error messages
        will be captured and added to the AsyncRequest's logs/errors arrays,
        which are then merged into the JSON response.
        
        Inherits from IConsoleCapture so it can be attached to AsyncRequest
        with proper lifetime management (destroyed when request completes).
    */
    struct ScopedConsoleHandler : public BaseScopedConsoleHandler,
                                  public RestServer::AsyncRequest::IConsoleCapture
    {
        ScopedConsoleHandler(MainController* mc, RestServer::AsyncRequest::Ptr request_);
        
    protected:
        void handleMessage(const String& message) override;
        void handleError(const String& message, const StringArray& callstack) override;
        
    private:
        // Reference - safe because AsyncRequest owns this ScopedConsoleHandler,
        // so AsyncRequest always outlives this object
        RestServer::AsyncRequest& request;
    };
    
    //==========================================================================
    
    /** Console handler for replay that captures to StatusBar log.
     *  
     *  Used when executeInteractions() is called directly (not through REST API)
     *  to capture console output and display it in the test window's log panel.
     */
    struct ReplayConsoleHandler : public BaseScopedConsoleHandler
    {
        ReplayConsoleHandler(MainController* mc, InteractionTestWindow* window, bool enabled);
        
        /** Build result JSON and log to StatusBar. Call after execution completes. */
        void finalize(const var& inputInteractions, bool success, 
                      int interactionsCompleted, int totalElapsedMs);
        
    protected:
        void handleMessage(const String& message) override;
        void handleError(const String& message, const StringArray& callstack) override;
        
    private:
        InteractionTestWindow* window;
        StringArray logs;
        Array<var> errors;
    };
    
    /** Gets a JavascriptProcessor from the request's moduleId parameter.
        
        @param mc   The MainController
        @param req  The async request containing the moduleId parameter
        @returns    The JavascriptProcessor, or nullptr if not found
    */
    static JavascriptProcessor* getScriptProcessor(MainController* mc, 
                                                   RestServer::AsyncRequest::Ptr req);
    
    /** Waits for any pending control callbacks to complete by pumping the message loop.
        
        @param sc        The component to wait for
        @param timeoutMs Maximum time to wait (default 200ms)
    */
    static void waitForPendingCallbacks(ScriptComponent* sc, int timeoutMs = 200);
    
    /** Builds a hierarchical property tree for a UI component.
        
        Creates a DynamicObject with the component's properties and recursively
        includes all child components in a "childComponents" array.
        
        Used by the /api/list_components?hierarchy=true endpoint.
        
        @param sc   The script component to build the tree for
        @returns    DynamicObject with id, type, position, size, and childComponents
    */
    static DynamicObject::Ptr createRecursivePropertyTree(ScriptComponent* sc);
    
    //==========================================================================
    // Route metadata registry
    
    /** Returns metadata for all registered API routes.
        The array index corresponds to the ApiRoute enum value.
    */
    static const Array<RouteMetadata>& getRouteMetadata();
    
    /** Get the path string for a route. */
    static String getRoutePath(ApiRoute route);
    
    /** Find route by subURL path. Returns ApiRoute::numRoutes if not found. */
    static ApiRoute findRoute(const String& subURL);
    
    //==========================================================================
    // Route handlers (static methods called from BackendProcessor::onAsyncRequest)
    
    /** Handler for GET / - List all available API methods. */
    static RestServer::Response handleListMethods(MainController* mc, 
                                                  RestServer::AsyncRequest::Ptr req);
    
    /** Handler for GET /api/status - Get project status. */
    static RestServer::Response handleStatus(MainController* mc, 
                                             RestServer::AsyncRequest::Ptr req);
    
    /** Handler for GET /api/get_script - Read script content. */
    static RestServer::Response handleGetScript(MainController* mc, 
                                                RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/set_script - Update script content. */
    static RestServer::Response handleSetScript(MainController* mc, 
                                                RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/recompile - Recompile a script processor. */
    static RestServer::Response handleRecompile(MainController* mc, 
                                                RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/repl - Evaluate a REPL expression. */
	static RestServer::Response handleEvaluateREPL(MainController* mc,
		                                           RestServer::AsyncRequest::Ptr req);

    /** Handler for GET /api/list_components - List UI components. */
    static RestServer::Response handleListComponents(MainController* mc, 
                                                     RestServer::AsyncRequest::Ptr req);
    
    /** Handler for GET /api/get_component_properties - Get component properties. */
    static RestServer::Response handleGetComponentProperties(MainController* mc, 
                                                             RestServer::AsyncRequest::Ptr req);
    
    /** Handler for GET /api/get_component_value - Get component runtime value. */
    static RestServer::Response handleGetComponentValue(MainController* mc, 
                                                        RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/set_component_value - Set component runtime value. */
    static RestServer::Response handleSetComponentValue(MainController* mc, 
                                                        RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/set_component_properties - Set component properties. */
    static RestServer::Response handleSetComponentProperties(MainController* mc, 
                                                             RestServer::AsyncRequest::Ptr req);
    
    /** Handler for GET /api/screenshot - Capture UI screenshot. */
    static RestServer::Response handleScreenshot(MainController* mc, 
                                                 RestServer::AsyncRequest::Ptr req);
    
    /** Handler for GET /api/get_selected_components - Get selected UI components. */
    static RestServer::Response handleGetSelectedComponents(MainController* mc, 
                                                            RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/simulate_interactions - Execute UI interaction sequence.
     *  Note: Takes BackendProcessor* to access InteractionTester.
     */
    static RestServer::Response handleSimulateInteractions(BackendProcessor* bp, 
                                                           RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/diagnose_script - Run diagnostic shadow parse.
     *  Accepts moduleId and/or filePath. Reads file from disk, runs shadow parse,
     *  returns structured diagnostics without modifying runtime state.
     */
    static RestServer::Response handleDiagnoseScript(MainController* mc, 
                                                     RestServer::AsyncRequest::Ptr req);
    
    /** Handler for GET /api/get_included_files - List included script files.
     *  Without moduleId: returns all included files across all processors (with owning processor).
     *  With moduleId: returns files for that specific processor only.
     */
    static RestServer::Response handleGetIncludedFiles(MainController* mc, 
                                                       RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/profile - Start profiling or retrieve last result.
     *  mode="record": starts a new session (non-blocking, returns immediately).
     *  mode="get": returns last result, with optional filter/summary parameters.
     *  When recording is in progress, "get" blocks until done (unless wait=false).
     */
    static RestServer::Response handleStartProfiling(MainController* mc, 
                                                      RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/parse_css - Parse CSS code and return diagnostics.
     *  HISE-agnostic: does not require a script processor.
     *  Accepts either inline code or a file path to a .css file.
     *  Optionally resolves properties for a set of selectors using CSS specificity.
     */
    static RestServer::Response handleParseCSS(MainController* mc, 
                                               RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/shutdown - Gracefully quit HISE.
     *  Sends the HTTP 200 response before scheduling the quit on the message thread.
     */
    static RestServer::Response handleShutdown(MainController* mc, 
                                                RestServer::AsyncRequest::Ptr req);
    
    /** Handler for GET /api/builder/tree - Get module tree hierarchy */
    static RestServer::Response handleBuilderTree(MainController* mc, 
                                                   RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/builder/apply - Apply operations to module tree */
    static RestServer::Response handleBuilderApply(MainController* mc,
                                                    RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/undo/push_group - Start a new undo group */
    static RestServer::Response handleUndoPushGroup(MainController* mc,
                                                     RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/undo/pop_group - End group, execute or discard */
    static RestServer::Response handleUndoPopGroup(MainController* mc,
                                                    RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/undo/back - Undo last action */
    static RestServer::Response handleUndoBack(MainController* mc,
                                                RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/undo/forward - Redo next action */
    static RestServer::Response handleUndoForward(MainController* mc,
                                                   RestServer::AsyncRequest::Ptr req);
    
    /** Handler for GET /api/undo/diff - Current diff state */
    static RestServer::Response handleUndoDiff(MainController* mc,
                                                RestServer::AsyncRequest::Ptr req);
    
    /** Handler for GET /api/undo/history - Full undo history */
    static RestServer::Response handleUndoHistory(MainController* mc,
                                                   RestServer::AsyncRequest::Ptr req);
    
    /** Handler for POST /api/undo/clear - Clear undo history */
    static RestServer::Response handleUndoClear(MainController* mc,
                                                 RestServer::AsyncRequest::Ptr req);

#if HISE_INCLUDE_PROFILING_TOOLKIT
    /** Query options for filtering and summarizing profiling results. */
    struct ProfileQueryOptions
    {
        bool summary = false;
        bool nested = false;
        String filter;              // wildcard pattern (e.g., "slow*"), empty = no filter
        String sourceTypeFilter;    // wildcard pattern (e.g., "Trace"), empty = no filter
        double minDuration = 0.0;
        int limit = 15;
        StringArray threadFilter;   // thread names to include (empty = all threads)

        bool hasFilters() const
        {
            return summary || filter.isNotEmpty() || sourceTypeFilter.isNotEmpty()
                   || minDuration > 0.0 || threadFilter.size() > 0;
        }

        static ProfileQueryOptions fromJson(const var& json)
        {
            ProfileQueryOptions opts;
            opts.summary = (bool)json.getProperty(RestApiIds::summary, false);
            opts.nested = (bool)json.getProperty(RestApiIds::nested, false);
            opts.filter = json.getProperty(RestApiIds::filter, "").toString();
            opts.sourceTypeFilter = json.getProperty(RestApiIds::sourceTypeFilter, "").toString();
            opts.minDuration = (double)json.getProperty(RestApiIds::minDuration, 0.0);
            opts.limit = jlimit(1, 100, (int)json.getProperty(RestApiIds::limit, 15));

            auto tf = json.getProperty(RestApiIds::threadFilter, var());

            if (tf.isArray())
            {
                for (int i = 0; i < tf.size(); i++)
                    opts.threadFilter.add(tf[i].toString());
            }

            return opts;
        }
    };

    /** Starts a fire-and-forget profiling session from body JSON params.
     *  Reads durationMs, threadFilter, eventFilter from bodyJson.
     *  Returns true if recording started, false if already recording.
     */
    static bool startProfilingSession(MainController* mc, const var& bodyJson, 
                                       double defaultDurationMs = 1000.0);

    /** Walks a ProfileInfoBase tree and returns a JSON-ready DynamicObject 
        with "threads" array and "flows" array (cross-thread causal connections). */
    static var profilingResultToJson(
        DebugSession::ProfileDataSource::ProfileInfoBase* root);

    /** Walks a ProfileInfoBase tree with filtering/aggregation and returns a 
        JSON-ready DynamicObject with "results" array and "flows" array. */
    static var profilingResultToSummary(
        DebugSession::ProfileDataSource::ProfileInfoBase* root,
        const ProfileQueryOptions& options);
#endif

private:
    // Builder helpers
    
    /** Resolve chain name string to raw::IDs::Chains constant.
     *  Returns -2 if invalid. Case-insensitive.
     */
    static int resolveChainIndex(const String& chainName);
    
    /** Get chain name from index constant. */
    static String getChainName(int chainIndex);
    
    /** Validate module type for chain using ProcessorMetadata.
     *  Returns true if valid, sets errorHints on failure.
     */
    static bool validateTypeForChain(const Identifier& typeId, 
                                      int chainIndex, 
                                      DynamicObject::Ptr errorHints);
    
    /** Find processor by name in tree. */
    static Processor* findProcessorByName(MainController* mc, const String& name);
    
    struct TreeOptions
    {
        bool includeParameters = false;
        bool verbose = false;
    };

    struct ProcessorOrValueTree
    {
        ProcessorMetadata getMetadata() const
        {
            if (p != nullptr)
                return p->getMetadata();
            else
            {
                ProcessorMetadataRegistry rd;

                if (auto pmd = rd.get(v.getType()))
                    return *pmd;
            }

            return {};
        }

        bool isBypassed() const
        {
            if (p != nullptr)
                return p->isBypassed();

            return v[scriptnode::PropertyIds::Bypassed];
        }

        String getId() const
        {
            if (p != nullptr)
                return p->getId();

            return v[PropertyIds::ID].toString();
        }

        String getColour() const
        {
            if (p != nullptr)
                return "#" + p->getColour().toDisplayString(false);

            return v[PropertyIds::ModColour].toString();
        }

        Identifier getType() const
        {
            if (p != nullptr)
                return p->getType();

            return v.getType();
        }

        bool isRuntimeData() const { return p != nullptr; }

        int getNumChildren() const
        {
            if (p != nullptr)
                return p->getNumChildProcessors();

            return v.getNumChildren();
        }

        ProcessorOrValueTree getChild(int i) const
        {
            Processor* c = (p != nullptr && p->getNumChildProcessors() < i) ? p->getChildProcessor(i) : nullptr;
            ValueTree cv = v.isValid() ? v.getChild(i) : ValueTree();
            return { c, cv };
        }

        float getAttribute(int parameterIndex) const
        {
            if (p != nullptr)
                return p->getAttribute(parameterIndex);

            return 0.0f;
        }

        Processor* p = nullptr;
        ValueTree v;
    };

    /** Build JSON tree representation of module hierarchy. */
    static var buildModuleTree(const ProcessorOrValueTree& root, const TreeOptions& options);
    
    /** Build JSON array for a specific chain. */
    static Array<var> buildChainArray(Processor* parent, int chainIndex);
};

} // namespace hise
