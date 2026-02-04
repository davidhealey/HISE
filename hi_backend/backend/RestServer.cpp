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
String RestServer::Request::getParameter(const String& name, const String& defaultValue) const
{
    auto& names = url.getParameterNames();
    auto& values = url.getParameterValues();
    
    // Search from end to get last occurrence (request params override defaults)
    for (int i = names.size() - 1; i >= 0; --i)
    {
        if (names[i] == name)
            return values[i];
    }
    
    return defaultValue;
}

var RestServer::Request::getJsonBody() const
{
    var result;
    auto postData = url.getPostData();
    
    if (postData.isEmpty())
        return var::undefined();
    
    auto parseResult = JSON::parse(postData, result);
    
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
        String path;                    // Just the path (e.g., "/api/recompile")
        StringPairArray defaultParams;  // Default parameter values from route URL
        RouteHandler handler;
    };

    //==============================================================================
    void addRoute(Method method, const URL& routeUrl, RouteHandler handler)
    {
        RouteInfo info;
        
        // Extract path from URL, prepending "/" since getSubPath() doesn't include it
        String subPath = routeUrl.getSubPath(false);
        info.path = subPath.isEmpty() ? "/" : ("/" + subPath);
        
        info.handler = std::move(handler);
        
        // Extract default parameters from route URL
        auto& names = routeUrl.getParameterNames();
        auto& values = routeUrl.getParameterValues();
        for (int i = 0; i < names.size(); ++i)
            info.defaultParams.set(names[i], values[i]);
        
        DBG("RestServer::addRoute - method: " + String(method) + ", path: " + info.path);
        
        routes[{method, info.path}] = std::move(info);
        
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
            auto& routePath = pair.first.second;
            auto& routeInfo = pair.second;

            DBG("RestServer::startServer - registering route: " + routePath + " (method: " + String(method) + ")");

            auto wrappedHandler = [this, routeInfo](const httplib::Request& req, httplib::Response& res)
            {
                DBG("RestServer - handler invoked for path: " + String(req.path.c_str()));
                handleRequest(routeInfo, req, res);
            };

            std::string pathStr = routePath.toStdString();

            switch (method)
            {
                case GET:    server->Get(pathStr, wrappedHandler); break;
                case POST:   server->Post(pathStr, wrappedHandler); break;
                case PUT:    server->Put(pathStr, wrappedHandler); break;
                case DELETE: server->Delete(pathStr, wrappedHandler); break;
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
    void handleRequest(const RouteInfo& routeInfo, const httplib::Request& req, httplib::Response& res)
    {
        DBG("RestServer::handleRequest - path: " + String(req.path.c_str()) + ", route: " + routeInfo.path);
        
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

        // Build URL with path
        URL url("http://localhost:" + String(currentPort) + String(req.path.c_str()));
        
        // First add default parameters from route definition
        for (int i = 0; i < routeInfo.defaultParams.size(); ++i)
        {
            auto key = routeInfo.defaultParams.getAllKeys()[i];
            auto value = routeInfo.defaultParams.getAllValues()[i];
            url = url.withParameter(key, value);
        }
        
        // Then override with incoming request parameters
        for (auto& param : req.params)
            url = url.withParameter(String(param.first), String(param.second));
        
        // Add POST data if present
        if (!req.body.empty())
            url = url.withPOSTData(String(req.body));
        
        // Build Request object
        Request request;
        request.url = url;
        
        // Copy headers
        for (auto& header : req.headers)
            request.headers.set(String(header.first), String(header.second));

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

URL RestServer::getBaseURL() const
{
    int port = pimpl->getServerPort();  // Returns 0 if not running
    return URL("http://localhost:" + String(port));
}

void RestServer::addRoute(Method method, const URL& routeUrl, RouteHandler handler)
{
    pimpl->addRoute(method, routeUrl, std::move(handler));
}

void RestServer::addAsyncRoute(Method method, const URL& routeUrl, AsyncRouteHandler handler)
{
    addRoute(method, routeUrl, [handler](const Request& req) -> Response
    {
        AsyncRequest::Ptr asyncReq = new AsyncRequest(req);
        return handler(asyncReq);
    });
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
