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
/**
    A simple REST API server with a JUCE-style interface.
    
    Wraps cpp-httplib to provide a thread-safe HTTP server that can be used
    to control HISE via a REST API.
    
    @note Route handlers are called on the server thread, NOT the message thread.
          Use MessageManager::callAsync() or your own synchronization if you need
          to interact with JUCE components or HISE internals.
    
    Routes are defined using juce::URL objects, which handle query parameters.
    Use getBaseURL() to get a starting URL, then chain getChildURL() and
    withParameter() to define your route.
    
    @code
    RestServer server;
    
    // Simple route
    server.addRoute(RestServer::GET, 
        server.getBaseURL().getChildURL("api/status"),
        [](const RestServer::Request& req) {
            DynamicObject::Ptr json = new DynamicObject();
            json->setProperty("status", "running");
            return RestServer::Response::ok(json.get());
        });
    
    // Route with parameters (accessed via query string: ?moduleId=value)
    server.addRoute(RestServer::GET, 
        server.getBaseURL()
            .getChildURL("api/recompile")
            .withParameter("moduleId", ""),  // empty default = required
        [](const RestServer::Request& req) {
            auto moduleId = req.getParameter("moduleId");
            if (moduleId.isEmpty())
                return RestServer::Response::badRequest("moduleId is required");
            return RestServer::Response::ok("Recompiling " + moduleId);
        });
    
    server.start(5000);
    @endcode
*/
class RestServer
{
public:
    //==============================================================================
    enum Method { GET, POST, PUT, DELETE };

    //==============================================================================
    /** Represents an incoming HTTP request. */
    struct Request
    {
        URL url;                        //< Full URL with path, query params, and POST data
        StringPairArray headers;        //< HTTP headers

        /** Get a query parameter value, with optional default.
            @param name         Parameter name
            @param defaultValue Value to return if parameter not present
            @returns            Parameter value or defaultValue
        */
        String getParameter(const String& name, const String& defaultValue = {}) const;

        /** Parse POST body as JSON. Returns undefined var on parse failure. */
        var getJsonBody() const;
    };

    //==============================================================================
    /** Represents an HTTP response to send back to the client. */
    struct Response
    {
        int statusCode = 200;
        String contentType = "application/json";
        String body;

        //==============================================================================
        // Factory methods for common responses

        /** 200 OK with JSON body. */
        static Response ok(const var& json);

        /** 200 OK with raw string body. */
        static Response ok(const String& body, const String& contentType);

        /** 400 Bad Request. */
        static Response badRequest(const String& message);

        /** 404 Not Found. */
        static Response notFound(const String& message = "Not found");

        /** 500 Internal Server Error. */
        static Response internalError(const String& message);

        /** Custom error with any status code. */
        static Response error(int statusCode, const String& message);
    };

    //==============================================================================
    /** Callback type for route handlers. */
    using RouteHandler = std::function<Response(const Request&)>;

    //==============================================================================
    /** A reference-counted async request object for coordinating work across threads.
        
        Use this with addAsyncRoute() when you need to dispatch work to another thread
        (e.g., via killVoicesAndCall) and block the HTTP response until complete.
        
        @code
        server.addAsyncRoute(RestServer::GET, "/api/recompile", [this](RestServer::AsyncRequest::Ptr asyncReq)
        {
            ProcessorFunction pf = [asyncReq](Processor* proc)
            {
                // Do work on the target thread...
                asyncReq->complete(RestServer::Response::ok(result));
                return SafeFunctionCall::OK;
            };
            
            getKillStateHandler().killVoicesAndCall(getMainSynthChain(), pf, 
                KillStateHandler::TargetThread::SampleLoadingThread);
            
            return asyncReq->waitForResponse();
        });
        @endcode
    */
    class AsyncRequest : public juce::ReferenceCountedObject
    {
    public:
        using Ptr = ReferenceCountedObjectPtr<AsyncRequest>;

        /** Creates an AsyncRequest wrapping the given HTTP request. */
        AsyncRequest(const Request& r) : request(r) {}

        /** Returns the original HTTP request. */
        const Request& getRequest() const { return request; }

        /** Call this from the target thread when work is complete.
            @param r The response to send back to the HTTP client.
        */
        void complete(const Response& r)
        {
            response = r;
            completed.store(true);
        }

        /** Completes the request with an error and returns the response.
            
            Use this for preflight checks before dispatching to another thread,
            or for errors that occur during async execution.
            
            @param statusCode HTTP status code (e.g., 400, 404, 500)
            @param message    Error message to include in the response.
            @returns          The error response (for immediate return from handler).
            
            @code
            // Preflight check - no need to kill voices
            if (module == nullptr)
                return asyncReq->fail(404, "Module not found");
            
            // Otherwise dispatch to another thread...
            @endcode
        */
        Response fail(int statusCode, const String& message)
        {
            complete(Response::error(statusCode, message));
            return waitForResponse();
        }

        /** Blocks until complete() is called or timeout expires.
            @param timeoutMs Maximum time to wait in milliseconds (default 30 seconds).
            @returns The response set via complete(), or a 504 timeout error.
        */
        Response waitForResponse(int timeoutMs = 30000)
        {
            auto startTime = Time::getMillisecondCounter();

            while (!completed.load())
            {
                if (Time::getMillisecondCounter() - startTime > (uint32)timeoutMs)
                    return Response::error(504, "Operation timed out");

                Thread::sleep(50);
            }

            return response;
        }

    private:
        Request request;
        std::atomic<bool> completed{false};
        Response response;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsyncRequest)
    };

    //==============================================================================
    /** Callback type for async route handlers. */
    using AsyncRouteHandler = std::function<Response(AsyncRequest::Ptr)>;

    //==============================================================================
    /** Listener interface for server events.
        
        @note Listeners are called on the server thread, not the message thread.
              Use MessageManager::callAsync() if you need to update UI.
        
        @note Events are not queued. If you register a listener after the server
              has started, check isRunning() and getPort() to catch up on state.
    */
    class Listener
    {
    public:
        virtual ~Listener() = default;

        /** Called when the server starts successfully. */
        virtual void serverStarted(int port) {}

        /** Called when the server stops. */
        virtual void serverStopped() {}

        /** Called when a request is received (for logging/debugging). */
        virtual void requestReceived(const String& method, const String& path) {}

        /** Called when an error occurs (e.g., exception in handler). */
        virtual void serverError(const String& message) {}
    };

    //==============================================================================
    RestServer();

    /** Destructor. Calls stop() automatically (RAII). */
    ~RestServer();

    //==============================================================================
    /** Returns a base URL for building route definitions.
        
        Use this with getChildURL() and withParameter() to define routes.
        
        @code
        server.addRoute(RestServer::GET, 
            server.getBaseURL()
                .getChildURL("api/recompile")
                .withParameter("moduleId", ""),
            handler);
        @endcode
        
        @returns URL with "http://localhost:PORT" where PORT is the server port,
                 or 0 if not yet started.
    */
    URL getBaseURL() const;

    /** Register a route handler.
        
        @param method   The HTTP method (GET, POST, PUT, DELETE)
        @param routeUrl URL defining the path and default parameter values.
                        Use getBaseURL().getChildURL().withParameter() to build.
        @param handler  Function called when route matches. Called on server thread!
        
        @note Call this before start(). Adding routes while running is not supported.
    */
    void addRoute(Method method, const URL& routeUrl, RouteHandler handler);

    /** Register an async route handler for operations that dispatch to other threads.
        
        This is a convenience method that wraps the handler with AsyncRequest creation.
        Use this when you need to dispatch work via killVoicesAndCall() or similar
        and block until the operation completes.
        
        @param method   The HTTP method (GET, POST, PUT, DELETE)
        @param routeUrl URL defining the path and default parameter values.
                        Use getBaseURL().getChildURL().withParameter() to build.
        @param handler  Function that receives an AsyncRequest::Ptr. Must call
                        asyncReq->complete() and return asyncReq->waitForResponse().
        
        @note Call this before start(). Adding routes while running is not supported.
        
        @see AsyncRequest
    */
    void addAsyncRoute(Method method, const URL& routeUrl, AsyncRouteHandler handler);

    //==============================================================================
    /** Start listening for connections.
        
        @param port         Port number to listen on
        @param bindAddress  Address to bind to. Default "127.0.0.1" for localhost only.
        @returns            true if server started successfully
    */
    bool start(int port, const String& bindAddress = "127.0.0.1");

    /** Stop the server. 
        
        Idempotent (safe to call multiple times). 
        Blocks until server thread has terminated.
    */
    void stop();

    /** Returns true if the server is currently running. */
    bool isRunning() const;

    /** Returns the port the server is listening on, or 0 if not running. */
    int getPort() const;

    //==============================================================================
    void addListener(Listener* listener);
    void removeListener(Listener* listener);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RestServer)
};

} // namespace hise

