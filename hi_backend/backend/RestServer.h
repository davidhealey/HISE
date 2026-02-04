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
    
    Path Parameters:
        Supports :param syntax for path parameters.
        Example: "/api/script/:scriptId/compile"
        Access via request.pathParams["scriptId"]
    
    @code
    RestServer server;
    
    server.addRoute(RestServer::GET, "/api/status", [](const RestServer::Request& req) {
        DynamicObject::Ptr json = new DynamicObject();
        json->setProperty("status", "running");
        return RestServer::Response::ok(json.get());
    });
    
    server.addRoute(RestServer::POST, "/api/script/:id/compile", 
        [](const RestServer::Request& req) {
            auto scriptId = req.pathParams["id"];
            return RestServer::Response::ok({{"compiled", scriptId}});
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
        String path;                    //< The matched path
        String body;                    //< Request body (for POST/PUT)
        StringPairArray headers;        //< HTTP headers
        StringPairArray queryParams;    //< ?key=value query parameters
        StringPairArray pathParams;     //< :param extracted values

        /** Parse body as JSON. Returns undefined var on parse failure. */
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
    /** Register a route handler.
        
        @param method   The HTTP method (GET, POST, PUT, DELETE)
        @param path     Path pattern, may include :param placeholders.
                        Examples: "/api/status", "/api/script/:id/compile"
        @param handler  Function called when route matches. Called on server thread!
        
        @note Call this before start(). Adding routes while running is not supported.
    */
    void addRoute(Method method, const String& path, RouteHandler handler);

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

