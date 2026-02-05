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

namespace hise {

/** Helper utilities for REST API request handling.
    
    This struct contains utilities for handling REST API requests that interact
    with HISE's scripting system, including console output capture, error parsing,
    and script compilation.
*/
struct RestHelpers
{
    /** Scoped handler that captures console output during request processing.
        
        While this object exists, any Console.print() calls or error messages
        will be captured and added to the AsyncRequest's logs/errors arrays,
        which are then merged into the JSON response.
    */
    struct ScopedConsoleHandler : public ControlledObject
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
        
        RestServer::AsyncRequest::Ptr request;
    };
    
    /** Gets a JavascriptProcessor from the request's moduleId parameter.
        
        @param mc   The MainController
        @param req  The async request containing the moduleId parameter
        @returns    The JavascriptProcessor, or nullptr if not found
    */
    static JavascriptProcessor* getScriptProcessor(MainController* mc, 
                                                   RestServer::AsyncRequest::Ptr req);
    
    /** Compiles a script processor and returns the response.
        
        This triggers an asynchronous compilation and waits for the result.
        The ScopedConsoleHandler captures any console output and errors.
        
        @param mc   The MainController
        @param sch  The scoped console handler for capturing output
        @param req  The async request
        @returns    Response with compilation result, logs, and errors
    */
    static RestServer::Response compile(MainController* mc, 
                                        ScopedConsoleHandler& sch, 
                                        RestServer::AsyncRequest::Ptr req);
    
    /** Builds a hierarchical property tree for a UI component.
        
        Creates a DynamicObject with the component's properties and recursively
        includes all child components in a "childComponents" array.
        
        Used by the /api/list_components?hierarchy=true endpoint.
        
        @param sc   The script component to build the tree for
        @returns    DynamicObject with id, type, position, size, and childComponents
    */
    static DynamicObject::Ptr createRecursivePropertyTree(ScriptComponent* sc);
};

} // namespace hise
