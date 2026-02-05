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

//==============================================================================
/** Identifiers for REST API parameter names and JSON response keys.
    Using Identifiers instead of string literals provides compile-time safety
    and better performance for property lookups.
*/
#define DECLARE_ID(x) const Identifier x(#x);

namespace RestApiIds
{
    // Request/body parameters
    DECLARE_ID(moduleId);
    DECLARE_ID(callback);
    DECLARE_ID(script);
    DECLARE_ID(compile);
    DECLARE_ID(hierarchy);
    
    // Common response fields
    DECLARE_ID(success);
    DECLARE_ID(result);
    DECLARE_ID(logs);
    DECLARE_ID(errors);
    DECLARE_ID(errorMessage);
    DECLARE_ID(callstack);
    
    // list_methods response
    DECLARE_ID(methods);
    DECLARE_ID(path);
    DECLARE_ID(method);
    DECLARE_ID(category);
    DECLARE_ID(description);
    DECLARE_ID(returns);
    DECLARE_ID(queryParameters);
    DECLARE_ID(bodyParameters);
    DECLARE_ID(name);
    DECLARE_ID(required);
    DECLARE_ID(defaultValue);
    
    // status response
    DECLARE_ID(server);
    DECLARE_ID(version);
    DECLARE_ID(project);
    DECLARE_ID(projectFolder);
    DECLARE_ID(scriptsFolder);
    DECLARE_ID(scriptProcessors);
    DECLARE_ID(isMainInterface);
    DECLARE_ID(externalFiles);
    DECLARE_ID(callbacks);
    DECLARE_ID(id);
    DECLARE_ID(type);
    DECLARE_ID(empty);
    
    // list_components response
    DECLARE_ID(components);
    DECLARE_ID(childComponents);
    DECLARE_ID(visible);
    DECLARE_ID(enabled);
    DECLARE_ID(x);
    DECLARE_ID(y);
    DECLARE_ID(width);
    DECLARE_ID(height);
    
    // get_component_properties response
    DECLARE_ID(properties);
    DECLARE_ID(value);
    DECLARE_ID(isDefault);
    DECLARE_ID(options);
    
    // get/set_component_value
    DECLARE_ID(min);
    DECLARE_ID(max);
    DECLARE_ID(validateRange);
    
    // Debug parameters
    DECLARE_ID(forceSynchronousExecution);
    DECLARE_ID(warning);
    
    // set_component_property
    DECLARE_ID(changes);
    DECLARE_ID(force);
    DECLARE_ID(applied);
    DECLARE_ID(locked);
    DECLARE_ID(property);
    DECLARE_ID(recompileRequired);
}

#undef DECLARE_ID

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
        Recompile,              ///< POST /api/recompile - Recompile a processor
        ListComponents,         ///< GET  /api/list_components - List UI components
        GetComponentProperties, ///< GET  /api/get_component_properties - Get component properties
        GetComponentValue,      ///< GET  /api/get_component_value - Get component runtime value
        SetComponentValue,      ///< POST /api/set_component_value - Set component runtime value
        SetComponentProperties, ///< POST /api/set_component_properties - Set component properties
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
    
    /** Scoped handler that captures console output during request processing.
        
        While this object exists, any Console.print() calls or error messages
        will be captured and added to the AsyncRequest's logs/errors arrays,
        which are then merged into the JSON response.
        
        Inherits from IConsoleCapture so it can be attached to AsyncRequest
        with proper lifetime management (destroyed when request completes).
    */
    struct ScopedConsoleHandler : public ControlledObject,
                                  public RestServer::AsyncRequest::IConsoleCapture
    {
        ScopedConsoleHandler(MainController* mc, RestServer::AsyncRequest::Ptr request_);
        ~ScopedConsoleHandler();
        
        void onMessage(const String& message, int warning, const Processor* p);
        
    private:
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
        static ParsedError parseError(const String& errorString,
                                      const File& scriptRoot,
                                      const String& moduleId);
        
        // Reference - safe because AsyncRequest owns this ScopedConsoleHandler,
        // so AsyncRequest always outlives this object
        RestServer::AsyncRequest& request;
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
};

} // namespace hise
