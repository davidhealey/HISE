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

// Prevent Windows.h min/max macros from interfering
#define NOMINMAX

#include "httplib.h"

// Remove Windows GDI Rectangle to avoid conflict with juce::Rectangle
#ifdef _WIN32
#undef Rectangle
#endif

namespace hise { using namespace juce;

//==============================================================================
// Helper to convert var to JSON string
static String varToJsonString(const var& v)
{
    return JSON::toString(v, false);
}

// Helper to create JSON error response body
static String createErrorJson(const String& message)
{
    DynamicObject::Ptr obj = new DynamicObject();
    obj->setProperty("error", true);
    obj->setProperty("message", message);
    return varToJsonString(obj.get());
}

//==============================================================================
// Request implementation
var RestServer::Request::getJsonBody() const
{
    var result;
    auto parseResult = JSON::parse(body, result);
    
    if (parseResult.failed())
        return var::undefined();
    
    return result;
}

//==============================================================================
// Response factory methods
RestServer::Response RestServer::Response::ok(const var& json)
{
    return { 200, "application/json", varToJsonString(json) };
}

RestServer::Response RestServer::Response::ok(const String& body, const String& contentType)
{
    return { 200, contentType, body };
}

RestServer::Response RestServer::Response::badRequest(const String& message)
{
    return { 400, "application/json", createErrorJson(message) };
}

RestServer::Response RestServer::Response::notFound(const String& message)
{
    return { 404, "application/json", createErrorJson(message) };
}

RestServer::Response RestServer::Response::internalError(const String& message)
{
    return { 500, "application/json", createErrorJson(message) };
}

RestServer::Response RestServer::Response::error(int statusCode, const String& message)
{
    return { statusCode, "application/json", createErrorJson(message) };
}

//==============================================================================
// Internal implementation class
class RestServer::Impl : public Thread
{
public:
    Impl() : Thread("REST API Server") {}

    ~Impl()
    {
        stopServer();
    }

    //==============================================================================
    struct RouteInfo
    {
        String originalPath;
        String regexPattern;
        StringArray paramNames;
        RouteHandler handler;
    };

    //==============================================================================
    void addRoute(Method method, const String& path, RouteHandler handler)
    {
        RouteInfo info;
        info.originalPath = path;
        info.handler = std::move(handler);
        
        // Convert :param to regex capture groups and extract param names
        parsePathPattern(path, info.regexPattern, info.paramNames);
        
        DBG("RestServer::addRoute - method: " + String(method) + ", path: " + path + ", regex: " + info.regexPattern);
        
        routes[{method, info.regexPattern}] = std::move(info);
        
        DBG("RestServer::addRoute - routes count: " + String(routes.size()));
    }

    //==============================================================================
    bool startServer(int port, const String& bindAddress)
    {
        DBG("RestServer::startServer - port: " + String(port) + ", bindAddress: " + bindAddress);
        DBG("RestServer::startServer - routes count before registration: " + String(routes.size()));
        
        if (isThreadRunning())
        {
            DBG("RestServer::startServer - already running, returning false");
            return false;
        }

        currentPort = port;
        currentBindAddress = bindAddress;

        server = std::make_unique<httplib::Server>();

        // Register all routes with httplib
        for (auto& pair : routes)
        {
            auto method = pair.first.first;
            auto& regexPattern = pair.first.second;
            auto& routeInfo = pair.second;

            DBG("RestServer::startServer - registering route: " + regexPattern + " (method: " + String(method) + ")");

            auto wrappedHandler = [this, routeInfo](const httplib::Request& req, httplib::Response& res)
            {
                DBG("RestServer - handler invoked for path: " + String(req.path.c_str()));
                handleRequest(routeInfo, req, res);
            };

            std::string pattern = regexPattern.toStdString();

            switch (method)
            {
                case GET:    server->Get(pattern, wrappedHandler); break;
                case POST:   server->Post(pattern, wrappedHandler); break;
                case PUT:    server->Put(pattern, wrappedHandler); break;
                case DELETE: server->Delete(pattern, wrappedHandler); break;
            }
        }

        DBG("RestServer::startServer - starting server thread");
        
        // Start the server thread
        startThread();

        // Wait briefly to check if server started successfully
        // The thread will set running to true once listen() is active
        for (int i = 0; i < 50; ++i)  // Wait up to 500ms
        {
            if (running.load())
            {
                listeners.call(&RestServer::Listener::serverStarted, currentPort);
                return true;
            }
            Thread::sleep(10);
        }

        // Server failed to start
        stopServer();
        return false;
    }

    //==============================================================================
    void stopServer()
    {
        if (server)
            server->stop();

        if (isThreadRunning())
            stopThread(5000);

        server.reset();
        running.store(false);
    }

    //==============================================================================
    void run() override
    {
        running.store(true);
        
        // This blocks until server->stop() is called
        server->listen(currentBindAddress.toStdString(), currentPort);
        
        running.store(false);
        listeners.call(&RestServer::Listener::serverStopped);
    }

    //==============================================================================
    bool isServerRunning() const
    {
        return running.load();
    }

    int getServerPort() const
    {
        return running.load() ? currentPort : 0;
    }

    //==============================================================================
    ListenerList<RestServer::Listener> listeners;

private:
    //==============================================================================
    void parsePathPattern(const String& path, String& regexPattern, StringArray& paramNames)
    {
        // Convert "/api/script/:id/compile" to "/api/script/([^/]+)/compile"
        // and extract ["id"] as param names
        
        StringArray parts;
        parts.addTokens(path, "/", "");
        parts.removeEmptyStrings();

        String result;
        
        for (auto& part : parts)
        {
            result += "/";
            
            if (part.startsWith(":"))
            {
                // This is a parameter
                paramNames.add(part.substring(1));
                result += "([^/]+)";
            }
            else
            {
                // Escape any regex special characters in literal parts
                result += escapeRegex(part);
            }
        }

        // If original path was just "/", result would be empty
        if (result.isEmpty())
            result = "/";

        regexPattern = result;
    }

    //==============================================================================
    String escapeRegex(const String& text)
    {
        String result;
        
        for (int i = 0; i < text.length(); ++i)
        {
            auto c = text[i];
            
            // Escape regex special characters
            if (String("[]{}()^$.|*+?\\").containsChar(c))
                result += "\\";
            
            result += c;
        }
        
        return result;
    }

    //==============================================================================
    void handleRequest(const RouteInfo& routeInfo, const httplib::Request& req, httplib::Response& res)
    {
        DBG("RestServer::handleRequest - path: " + String(req.path.c_str()) + ", route: " + routeInfo.originalPath);
        
        // Notify listeners
        String methodStr;
        switch (routes.begin()->first.first)  // Bit of a hack to get method
        {
            case GET:    methodStr = "GET"; break;
            case POST:   methodStr = "POST"; break;
            case PUT:    methodStr = "PUT"; break;
            case DELETE: methodStr = "DELETE"; break;
        }
        
        listeners.call(&RestServer::Listener::requestReceived, methodStr, String(req.path));

        // Build our Request object
        Request request;
        request.path = String(req.path);
        request.body = String(req.body);

        // Copy headers
        for (auto& header : req.headers)
            request.headers.set(String(header.first), String(header.second));

        // Copy query params
        for (auto& param : req.params)
            request.queryParams.set(String(param.first), String(param.second));

        // Extract path params from regex matches
        for (int i = 0; i < routeInfo.paramNames.size(); ++i)
        {
            if (i < (int)req.matches.size() - 1)  // matches[0] is full match
            {
                request.pathParams.set(routeInfo.paramNames[i], 
                                       String(req.matches[i + 1].str()));
            }
        }

        // Call handler with exception safety
        Response response;
        
        try
        {
            response = routeInfo.handler(request);
        }
        catch (const std::exception& e)
        {
            String errorMsg = String("Exception in handler: ") + e.what();
            listeners.call(&RestServer::Listener::serverError, errorMsg);
            response = Response::internalError(errorMsg);
        }
        catch (...)
        {
            String errorMsg = "Unknown exception in handler";
            listeners.call(&RestServer::Listener::serverError, errorMsg);
            response = Response::internalError(errorMsg);
        }

        // Send response
        res.status = response.statusCode;
        res.set_content(response.body.toStdString(), response.contentType.toStdString());
    }

    //==============================================================================
    std::unique_ptr<httplib::Server> server;
    std::map<std::pair<Method, String>, RouteInfo> routes;
    
    int currentPort = 0;
    String currentBindAddress;
    std::atomic<bool> running{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Impl)
};

//==============================================================================
// RestServer public interface implementation

RestServer::RestServer() : pimpl(std::make_unique<Impl>()) {}

RestServer::~RestServer()
{
    stop();
}

void RestServer::addRoute(Method method, const String& path, RouteHandler handler)
{
    pimpl->addRoute(method, path, std::move(handler));
}

bool RestServer::start(int port, const String& bindAddress)
{
    return pimpl->startServer(port, bindAddress);
}

void RestServer::stop()
{
    pimpl->stopServer();
}

bool RestServer::isRunning() const
{
    return pimpl->isServerRunning();
}

int RestServer::getPort() const
{
    return pimpl->getServerPort();
}

void RestServer::addListener(Listener* listener)
{
    pimpl->listeners.add(listener);
}

void RestServer::removeListener(Listener* listener)
{
    pimpl->listeners.remove(listener);
}

} // namespace hise
