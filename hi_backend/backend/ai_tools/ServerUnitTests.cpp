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

#if HI_RUN_UNIT_TESTS

namespace hise {

/** Unit tests for the REST Server API.
    
    These tests create a BackendProcessor instance with a REST server running
    on a test port (1901) and verify the API endpoints work correctly.
*/
class RestServerTest : public juce::UnitTest
{
public:
    RestServerTest() : UnitTest("REST Server Tests", "AI Tools") {}
    
    void runTest() override
    {
        ScopedValueSetter<bool> svs(MainController::unitTestMode, true);
        
        // Create context once for all tests, pass *this for expect() access
        ctx = std::make_unique<TestContext>(*this);
        
        testServerStartStop();
        testListMethods();
        testStatusEndpoint();
        testListComponentsFlat();
        testListComponentsHierarchy();
        testGetComponentProperties();
        testSetScriptSuccess();
        testSetScriptCompileError();
        testGetScript();
        testRecompile();
        testGetComponentValue();
        testSetComponentValue();
        testSetComponentValueTriggersCallback();
        testSetComponentValueCallbackRuntimeError();
        testNestedControlCallbacks();
        testFloatNormalizationUnit();
        testFloatNormalization();
        
        // Verify all endpoints were tested
        verifyAllEndpointsTested();
        
        // Clean up
        ctx.reset();
    }
    
private:
    static constexpr int TEST_PORT = RestServer::TestPort;
    
    /** Test context that manages a full HISE instance with REST server.
        
        This follows the same initialization pattern as CLIInstance in Main.cpp
        to ensure proper setup of the BackendProcessor and all its subsystems.
        
        Also tracks which endpoints have been called for coverage verification.
    */
    struct TestContext
    {
        StringArray touchedEndpoints;
        
        TestContext(UnitTest& parentTest) : test(parentTest)
        {
            // Skip audio driver initialization for headless testing
            CompileExporter::setSkipAudioDriverInitialisation();
            
            // Create the processor chain: StandaloneProcessor -> MainController -> BackendProcessor
            sp = new StandaloneProcessor();
            mc = dynamic_cast<MainController*>(sp->createProcessor());
            
            // Suspend background threads for safe operation during tests
            threadSuspender = new MainController::ScopedBadBabysitter(mc);
            
            bp = dynamic_cast<BackendProcessor*>(mc.get());
            
            // Initialize audio processing (required even without real audio)
            bp->prepareToPlay(44100.0, 512);
            AudioSampleBuffer ab(2, 512);
            MidiBuffer mb;
            bp->processBlock(ab, mb);
            
            // Start REST server on test port
            bp->getRestServer().start(TEST_PORT);
        }
        
        ~TestContext()
        {
            bp->getRestServer().stop();
            
            // Clean up in reverse order
            threadSuspender = nullptr;
            mc = nullptr;
            sp = nullptr;
        }
        
        /** Reset to clean state by clearing the preset. */
        void reset()
        {
            bp->clearPreset(dontSendNotification);
            bp->createInterface(600, 600, false);
            
            // Pump messages to allow any async operations to complete
            MessageManager::getInstance()->runDispatchLoopUntil(50);
        }
        
        /** Make a GET request to the test server (runs on background thread, pumps message loop). */
        String httpGet(const String& endpoint)
        {
            // Track the endpoint path (without query params) for coverage
            auto path = endpoint.upToFirstOccurrenceOf("?", false, false);
            touchedEndpoints.addIfNotAlreadyThere(path);
            
            return httpRequestWithMessagePump([endpoint]()
            {
                URL url("http://localhost:" + String(TEST_PORT) + endpoint);
                return url.readEntireTextStream(false);
            });
        }
        
        /** Make a POST request with JSON body to the test server (runs on background thread, pumps message loop). */
        String httpPost(const String& endpoint, const String& jsonBody)
        {
            // Track the endpoint path (without query params) for coverage
            auto path = endpoint.upToFirstOccurrenceOf("?", false, false);
            touchedEndpoints.addIfNotAlreadyThere(path);
            
            return httpRequestWithMessagePump([endpoint, jsonBody]()
            {
                URL url = URL("http://localhost:" + String(TEST_PORT) + endpoint)
                    .withPOSTData(jsonBody);
                
                // Use inAddress to keep query params in URL path, not in POST body
                URL::InputStreamOptions options(URL::ParameterHandling::inAddress);
                
                if (auto stream = url.createInputStream(options.withExtraHeaders("Content-Type: application/json")))
                    return stream->readEntireStreamAsString();
                return String();
            });
        }
        
    private:
        /** Helper that runs an HTTP request on a background thread while pumping the message loop. */
        template<typename Func>
        String httpRequestWithMessagePump(Func&& requestFunc)
        {
            String result;
            std::atomic<bool> done{false};
            
            // Run the HTTP request on a background thread
            Thread::launch([&result, &done, func = std::forward<Func>(requestFunc)]()
            {
                result = func();
                done.store(true);
            });
            
            // Pump the message loop while waiting for the request to complete
            while (!done.load())
            {
                MessageManager::getInstance()->runDispatchLoopUntil(10);
            }
            
            return result;
        }
        
    public:
        
        /** Parse JSON response and return as var. */
        var parseJson(const String& response)
        {
            var result;
            JSON::parse(response, result);
            return result;
        }
        
        /** Compile a script via the REST API.
            @param code          The script code to compile
            @param moduleId      Target script processor (default: "Interface")
            @param callback      Target callback (default: "onInit")
            @param expectSuccess If true, calls expect() to verify compilation succeeded (default: true)
            @returns             Parsed JSON response
        */
        var compile(const String& code, 
                    const String& moduleId = "Interface", 
                    const String& callback = "onInit",
                    bool expectSuccess = true)
        {
            // All POST parameters go in the JSON body (unified approach)
            DynamicObject::Ptr bodyObj = new DynamicObject();
            bodyObj->setProperty("moduleId", moduleId);
            bodyObj->setProperty("callback", callback);
            bodyObj->setProperty("script", code);
            bodyObj->setProperty("compile", true);
            
            auto response = httpPost("/api/set_script", 
                                     JSON::toString(var(bodyObj.get())));
            var json = parseJson(response);
            
            if (expectSuccess)
            {
                test.expect((bool)json["success"], 
                            "Compilation failed: " + json["result"].toString());
            }
            
            return json;
        }
        
        UnitTest& test;
        ScopedPointer<StandaloneProcessor> sp;
        ScopedPointer<MainController> mc;
        ScopedPointer<MainController::ScopedBadBabysitter> threadSuspender;
        BackendProcessor* bp = nullptr;  // Non-owning, managed by mc
    };
    
    std::unique_ptr<TestContext> ctx;
    
    //==========================================================================
    void testServerStartStop()
    {
        beginTest("Server start/stop");
        
        expect(ctx->bp->getRestServer().isRunning(), "Server should be running");
        expect(ctx->bp->getRestServer().getPort() == TEST_PORT, "Should use test port");
    }
    
    //==========================================================================
    void testListMethods()
    {
        beginTest("GET / (list_methods)");
        
        auto response = ctx->httpGet("/");
        expect(response.isNotEmpty(), "Should return response");
        
        var json = ctx->parseJson(response);
        expect((bool)json["success"], "success should be true");
        
        auto methods = json["methods"];
        expect(methods.isArray(), "methods should be array");
        expect(methods.size() == (int)RestHelpers::ApiRoute::numRoutes, 
               "Should have all " + String((int)RestHelpers::ApiRoute::numRoutes) + " routes");
        
        // Verify sorted by category then path
        String lastCategory;
        String lastPath;
        for (int i = 0; i < methods.size(); i++)
        {
            auto cat = methods[i]["category"].toString();
            auto path = methods[i]["path"].toString();
            
            if (cat != lastCategory)
            {
                // New category - should be alphabetically >= last category
                expect(lastCategory.isEmpty() || cat.compare(lastCategory) >= 0,
                       "Categories should be sorted: " + lastCategory + " -> " + cat);
                lastCategory = cat;
                lastPath = "";
            }
            
            // Within same category, paths should be sorted
            expect(lastPath.isEmpty() || path.compare(lastPath) >= 0,
                   "Paths should be sorted within category: " + lastPath + " -> " + path);
            lastPath = path;
        }
        
        // Verify status endpoint structure
        bool foundStatus = false;
        for (int i = 0; i < methods.size(); i++)
        {
            if (methods[i]["path"].toString() == "/api/status")
            {
                foundStatus = true;
                expect(methods[i]["method"].toString() == "GET", "status should be GET");
                expect(methods[i]["category"].toString() == "status", "status should have 'status' category");
                expect(methods[i]["description"].toString().isNotEmpty(), "status should have description");
                expect(methods[i]["returns"].toString().isNotEmpty(), "status should have returns");
                break;
            }
        }
        expect(foundStatus, "Should include /api/status endpoint");
        
        // Verify POST endpoint has bodyParameters
        for (int i = 0; i < methods.size(); i++)
        {
            if (methods[i]["path"].toString() == "/api/set_script")
            {
                expect(methods[i]["method"].toString() == "POST", "set_script should be POST");
                auto bodyParams = methods[i]["bodyParameters"];
                expect(bodyParams.isArray(), "set_script should have bodyParameters");
                expect(bodyParams.size() >= 3, "set_script should have at least 3 body params");
                
                // Verify moduleId is in body params
                bool hasModuleId = false;
                for (int j = 0; j < bodyParams.size(); j++)
                {
                    if (bodyParams[j]["name"].toString() == "moduleId")
                    {
                        hasModuleId = true;
                        expect((bool)bodyParams[j]["required"], "moduleId should be required");
                    }
                }
                expect(hasModuleId, "set_script should have moduleId in bodyParameters");
                break;
            }
        }
        
        // Verify GET endpoint has queryParameters
        for (int i = 0; i < methods.size(); i++)
        {
            if (methods[i]["path"].toString() == "/api/get_script")
            {
                auto queryParams = methods[i]["queryParameters"];
                expect(queryParams.isArray(), "get_script should have queryParameters");
                expect(queryParams.size() >= 1, "get_script should have at least 1 query param");
                break;
            }
        }
    }
    
    //==========================================================================
    void verifyAllEndpointsTested()
    {
        beginTest("All endpoints tested");
        
        // Verify metadata count matches enum
        const auto& metadata = RestHelpers::getRouteMetadata();
        expect(metadata.size() == (int)RestHelpers::ApiRoute::numRoutes,
               "Metadata count must match RestHelpers::ApiRoute::numRoutes");
        
        // Verify all endpoints were touched during tests
        for (const auto& route : metadata)
        {
            auto fullPath = "/" + route.path;
            expect(ctx->touchedEndpoints.contains(fullPath),
                   "Endpoint not tested: " + fullPath);
        }
    }
    
    //==========================================================================
    void testStatusEndpoint()
    {
        beginTest("GET /api/status");
        
        auto response = ctx->httpGet("/api/status");
        expect(response.isNotEmpty(), "Should return response");
        
        var json = ctx->parseJson(response);
        expect((bool)json["success"], "success should be true");
        expect(json.hasProperty("project"), "Should have project info");
        expect(json.hasProperty("scriptProcessors"), "Should have processors");
        expect(json.hasProperty("server"), "Should have server info");
        
        // Check that Interface processor exists
        auto processors = json["scriptProcessors"];
        expect(processors.isArray(), "scriptProcessors should be array");
        
        bool hasInterface = false;
        for (int i = 0; i < processors.size(); i++)
        {
            if (processors[i]["moduleId"].toString() == "Interface")
            {
                hasInterface = true;
                expect((bool)processors[i]["isMainInterface"], 
                       "Interface should be main interface");
            }
        }
        expect(hasInterface, "Should have Interface processor");
    }
    
    //==========================================================================
    void testListComponentsFlat()
    {
        beginTest("GET /api/list_components (flat)");
        
        ctx->reset();
        
        // First create some components
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);\n"
                     "const var TestButton = Content.addButton(\"TestButton\", 150, 10);");
        
        // Now list components
        auto response = ctx->httpGet("/api/list_components?moduleId=Interface");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed: " + response);
        
        auto components = json["components"];
        expect(components.isArray(), "components should be array");
        expectEquals<int>(components.size(), 2, "Should have 2 components");
        
        // Verify flat structure (no childComponents in flat mode)
        bool hasKnob = false, hasButton = false;
        for (int i = 0; i < components.size(); i++)
        {
            if (components[i]["id"].toString() == "TestKnob")
            {
                hasKnob = true;
                expect(components[i]["type"].toString() == "ScriptSlider", "TestKnob should be ScriptSlider");
                expect(!components[i].hasProperty("childComponents"), "Flat mode should not have childComponents");
            }
            if (components[i]["id"].toString() == "TestButton")
            {
                hasButton = true;
                expect(components[i]["type"].toString() == "ScriptButton", "TestButton should be ScriptButton");
            }
        }
        expect(hasKnob, "Should have TestKnob");
        expect(hasButton, "Should have TestButton");
    }
    
    //==========================================================================
    void testListComponentsHierarchy()
    {
        beginTest("GET /api/list_components?hierarchy=true");
        
        ctx->reset();
        
        // Create parent panel with child knob
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var Panel1 = Content.addPanel(\"Panel1\", 10, 10);\n"
                     "Panel1.set(\"width\", 200);\n"
                     "Panel1.set(\"height\", 200);\n"
                     "const var ChildKnob = Content.addKnob(\"ChildKnob\", 20, 20);\n"
                     "ChildKnob.set(\"parentComponent\", \"Panel1\");");
        
        auto response = ctx->httpGet("/api/list_components?moduleId=Interface&hierarchy=true");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        
        auto components = json["components"];
        
        // In hierarchy mode, ChildKnob should NOT be at root level
        bool panelAtRoot = false;
        bool childAtRoot = false;
        
        for (int i = 0; i < components.size(); i++)
        {
            if (components[i]["id"].toString() == "Panel1")
                panelAtRoot = true;
            if (components[i]["id"].toString() == "ChildKnob")
                childAtRoot = true;
        }
        
        expect(panelAtRoot, "Panel1 should be at root");
        expect(!childAtRoot, "ChildKnob should NOT be at root in hierarchy mode");
        
        // Find Panel1 and verify child is nested
        bool foundNestedChild = false;
        for (int i = 0; i < components.size(); i++)
        {
            if (components[i]["id"].toString() == "Panel1")
            {
                auto children = components[i]["childComponents"];
                expect(children.isArray(), "Should have childComponents array");
                
                for (int j = 0; j < children.size(); j++)
                {
                    if (children[j]["id"].toString() == "ChildKnob")
                    {
                        foundNestedChild = true;
                        expect(children[j]["type"].toString() == "ScriptSlider", "ChildKnob should be ScriptSlider");
                        expect(children[j].hasProperty("width"), "Hierarchy should include width");
                        expect(children[j].hasProperty("height"), "Hierarchy should include height");
                    }
                }
            }
        }
        
        expect(foundNestedChild, "ChildKnob should be nested under Panel1");
    }
    
    //==========================================================================
    void testGetComponentProperties()
    {
        beginTest("GET /api/get_component_properties");
        
        ctx->reset();
        
        // Create a knob with some custom properties
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var PropKnob = Content.addKnob(\"PropKnob\", 50, 50);\n"
                     "PropKnob.set(\"text\", \"My Knob\");\n"
                     "PropKnob.set(\"min\", -24);\n"
                     "PropKnob.set(\"max\", 24);\n"
                     "PropKnob.set(\"mode\", \"Decibel\");");
        
        auto response = ctx->httpGet("/api/get_component_properties?moduleId=Interface&componentId=PropKnob");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        expect(json["componentId"].toString() == "PropKnob", "Should return correct componentId");
        expect(json["type"].toString() == "ScriptSlider", "Should return correct type");
        
        auto properties = json["properties"];
        expect(properties.isArray(), "properties should be array");
        
        // Check specific properties
        bool foundText = false, foundMin = false, foundMode = false;
        for (int i = 0; i < properties.size(); i++)
        {
            auto prop = properties[i];
            String propId = prop["id"].toString();
            
            if (propId == "text")
            {
                foundText = true;
                expect(prop["value"].toString() == "My Knob", "text should be 'My Knob'");
                expect(!(bool)prop["isDefault"], "text should not be default");
            }
            if (propId == "min")
            {
                foundMin = true;
                expect((double)prop["value"] == -24.0, "min should be -24");
            }
            if (propId == "mode")
            {
                foundMode = true;
                expect(prop["value"].toString() == "Decibel", "mode should be 'Decibel'");
                expect(prop.hasProperty("options"), "mode should have options");
            }
        }
        
        expect(foundText, "Should have text property");
        expect(foundMin, "Should have min property");
        expect(foundMode, "Should have mode property");
    }
    
    //==========================================================================
    void testSetScriptSuccess()
    {
        beginTest("POST /api/set_script (success)");
        
        ctx->reset();
        
        var json = ctx->compile("Content.makeFrontInterface(600, 400);\n"
                                "Console.print(\"Test output\");");
        
        expect(json["result"].toString() == "Recompiled OK", "Result should be 'Recompiled OK'");
        
        auto logs = json["logs"];
        expect(logs.isArray(), "logs should be array");
        expect(logs.size() >= 1, "Should have at least one log entry");
        
        bool foundTestOutput = false;
        for (int i = 0; i < logs.size(); i++)
        {
            if (logs[i].toString().contains("Test output"))
                foundTestOutput = true;
        }
        expect(foundTestOutput, "Should capture Console.print output");
        
        auto errors = json["errors"];
        expect(errors.isArray(), "errors should be array");
        expect(errors.size() == 0, "Should have no errors");
    }
    
    //==========================================================================
    void testSetScriptCompileError()
    {
        beginTest("POST /api/set_script (compile error)");
        
        ctx->reset();
        
        // Script with syntax error - don't expect success
        var json = ctx->compile("Content.makeFrontInterface(600, 400);\n"
                                "const var x = ;",  // Syntax error
                                "Interface", "onInit", false);
        
        expect(!(bool)json["success"], "Compilation should fail");
        
        auto errors = json["errors"];
        expect(errors.isArray(), "errors should be array");
        expect(errors.size() >= 1, "Should have at least one error");
        
        auto firstError = errors[0];
        expect(firstError.hasProperty("errorMessage"), "Error should have errorMessage");
        expect(firstError.hasProperty("callstack"), "Error should have callstack");
        
        auto callstack = firstError["callstack"];
        expect(callstack.isArray(), "callstack should be array");
    }
    
    //==========================================================================
    void testGetScript()
    {
        beginTest("GET /api/get_script");
        
        ctx->reset();
        
        // Set a known script first
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var MyKnob = Content.addKnob(\"MyKnob\", 10, 10);");
        
        // Get the script back
        auto response = ctx->httpGet("/api/get_script?moduleId=Interface&callback=onInit");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        expect(json["moduleId"].toString() == "Interface", "Should return correct moduleId");
        expect(json["callback"].toString() == "onInit", "Should return correct callback");
        
        String returnedScript = json["script"].toString();
        expect(returnedScript.contains("makeFrontInterface"), "Should contain makeFrontInterface");
        expect(returnedScript.contains("MyKnob"), "Should contain MyKnob");
    }
    
    //==========================================================================
    void testRecompile()
    {
        beginTest("POST /api/recompile");
        
        ctx->reset();
        
        // Set initial script
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "Console.print(\"Recompile test\");");
        
        // Recompile without changes - now POST with JSON body
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        
        auto response = ctx->httpPost("/api/recompile", 
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Recompile should succeed");
        expect(json["result"].toString() == "Recompiled OK", "Result should be 'Recompiled OK'");
        
        // Console.print should run again
        auto logs = json["logs"];
        bool foundOutput = false;
        for (int i = 0; i < logs.size(); i++)
        {
            if (logs[i].toString().contains("Recompile test"))
                foundOutput = true;
        }
        expect(foundOutput, "Recompile should execute onInit again");
    }
    
    //==========================================================================
    void testGetComponentValue()
    {
        beginTest("GET /api/get_component_value");
        
        ctx->reset();
        
        // Create a knob with specific range
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);\n"
                     "TestKnob.set(\"min\", -12);\n"
                     "TestKnob.set(\"max\", 12);\n"
                     "TestKnob.setValue(0.5);");
        
        auto response = ctx->httpGet("/api/get_component_value?moduleId=Interface&componentId=TestKnob");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        expect(json["moduleId"].toString() == "Interface", "Should return correct moduleId");
        expect(json["componentId"].toString() == "TestKnob", "Should return correct componentId");
        expect(json["type"].toString() == "ScriptSlider", "Should return correct type");
        expect(json.hasProperty("value"), "Should have value property");
        expect(json.hasProperty("min"), "Should have min property");
        expect(json.hasProperty("max"), "Should have max property");
        expect((double)json["min"] == -12.0, "min should be -12");
        expect((double)json["max"] == 12.0, "max should be 12");
    }
    
    //==========================================================================
    void testSetComponentValue()
    {
        beginTest("POST /api/set_component_value");
        
        ctx->reset();
        
        // Create a knob with specific range
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);\n"
                     "TestKnob.set(\"min\", 0);\n"
                     "TestKnob.set(\"max\", 10);");
        
        // Test 1: Set value without validation
        {
            DynamicObject::Ptr bodyObj = new DynamicObject();
            bodyObj->setProperty("moduleId", "Interface");
            bodyObj->setProperty("componentId", "TestKnob");
            bodyObj->setProperty("value", 5.0);
            
            auto response = ctx->httpPost("/api/set_component_value",
                                          JSON::toString(var(bodyObj.get())));
            var json = ctx->parseJson(response);
            
            expect((bool)json["success"], "Should succeed");
            expect(json["moduleId"].toString() == "Interface", "Should return correct moduleId");
            expect(json["componentId"].toString() == "TestKnob", "Should return correct componentId");
            expect(json["type"].toString() == "ScriptSlider", "Should return correct type");
        }
        
        // Test 2: Set value with validation - valid value
        {
            DynamicObject::Ptr bodyObj = new DynamicObject();
            bodyObj->setProperty("moduleId", "Interface");
            bodyObj->setProperty("componentId", "TestKnob");
            bodyObj->setProperty("value", 7.5);
            bodyObj->setProperty("validateRange", true);
            
            auto response = ctx->httpPost("/api/set_component_value",
                                          JSON::toString(var(bodyObj.get())));
            var json = ctx->parseJson(response);
            
            expect((bool)json["success"], "Should succeed with valid value");
        }
        
        // Test 3: Set value with validation - out of range (should fail)
        {
            DynamicObject::Ptr bodyObj = new DynamicObject();
            bodyObj->setProperty("moduleId", "Interface");
            bodyObj->setProperty("componentId", "TestKnob");
            bodyObj->setProperty("value", 15.0);  // Out of range [0, 10]
            bodyObj->setProperty("validateRange", true);
            
            auto response = ctx->httpPost("/api/set_component_value",
                                          JSON::toString(var(bodyObj.get())));
            var json = ctx->parseJson(response);
            
            expect(!(bool)json["success"], "Should fail with out-of-range value when validateRange is true");
        }
        
        // Test 4: Out of range without validation (should succeed - no validation)
        {
            DynamicObject::Ptr bodyObj = new DynamicObject();
            bodyObj->setProperty("moduleId", "Interface");
            bodyObj->setProperty("componentId", "TestKnob");
            bodyObj->setProperty("value", 15.0);  // Out of range but validateRange is false
            bodyObj->setProperty("validateRange", false);
            
            auto response = ctx->httpPost("/api/set_component_value",
                                          JSON::toString(var(bodyObj.get())));
            var json = ctx->parseJson(response);
            
            expect((bool)json["success"], "Should succeed without validation even if out of range");
        }
    }
    
    //==========================================================================
    void testSetComponentValueTriggersCallback()
    {
        beginTest("POST /api/set_component_value triggers control callback");
        
        ctx->reset();
        
        // Create a knob with a dedicated control callback using setControlCallback
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);\n"
                     "\n"
                     "inline function onTestKnobChanged(component, value)\n"
                     "{\n"
                     "    Console.print(\"CALLBACK_TRIGGERED:\" + value);\n"
                     "}\n"
                     "\n"
                     "TestKnob.setControlCallback(onTestKnobChanged);");
        
        // Set value via API
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("componentId", "TestKnob");
        bodyObj->setProperty("value", 0.75);
        
        auto response = ctx->httpPost("/api/set_component_value",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        
        // Verify callback was triggered with correct value
        auto logs = json["logs"];
        bool callbackTriggeredWithCorrectValue = false;
        for (int i = 0; i < logs.size(); i++)
        {
            if (logs[i].toString().contains("CALLBACK_TRIGGERED:0.75"))
                callbackTriggeredWithCorrectValue = true;
        }
        expect(callbackTriggeredWithCorrectValue, 
               "Control callback should be triggered with value 0.75 when setting via API");
    }
    
    //==========================================================================
    void testSetComponentValueCallbackRuntimeError()
    {
        beginTest("POST /api/set_component_value reports callback runtime errors");
        
        ctx->reset();
        
        // Create a knob with a callback that will cause a runtime error
        // Console.print(undefined) compiles fine but fails at runtime
        auto compileResult = ctx->compile(
            "Content.makeFrontInterface(600, 400);\n"
            "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);\n"
            "TestKnob.set(\"saveInPreset\", false);\n"
            "\n"
            "inline function onTestKnobChanged(component, value)\n"
            "{\n"
            "    Console.print(undefined);\n"
            "}\n"
            "\n"
            "TestKnob.setControlCallback(onTestKnobChanged);");
        
        // Compilation should succeed (runtime error hasn't happened yet)
        expect((bool)compileResult["success"], "Compilation should succeed");
        auto compileErrors = compileResult["errors"];
        expect(compileErrors.size() == 0, "Should have no compile errors");
        
        // Now trigger the callback via set_component_value
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("componentId", "TestKnob");
        bodyObj->setProperty("value", 0.5);
        
        auto response = ctx->httpPost("/api/set_component_value",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        // The API call itself should still succeed (value was set)
        // but the errors array should contain the runtime error from the callback
        auto errors = json["errors"];
        expect(errors.isArray(), "errors should be array");
        expect(errors.size() >= 1, "Should have at least one runtime error from callback");
        
        if (errors.size() > 0)
        {
            auto firstError = errors[0];
            expect(firstError.hasProperty("errorMessage"), "Error should have errorMessage");
            // The error should mention something about undefined
            expect(firstError["errorMessage"].toString().containsIgnoreCase("undefined"),
                   "Error message should mention 'undefined'");
        }
    }
    
    //==========================================================================
    void testNestedControlCallbacks()
    {
        beginTest("POST /api/set_component_value captures nested callback logs");
        
        ctx->reset();
        
        // Create two knobs where Knob1's callback triggers Knob2's callback
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var Knob1 = Content.addKnob(\"Knob1\", 10, 10);\n"
                     "const var Knob2 = Content.addKnob(\"Knob2\", 150, 10);\n"
                     "\n"
                     "inline function onKnob1Changed(component, value)\n"
                     "{\n"
                     "    Console.print(\"Knob1 callback: \" + value);\n"
                     "    Knob2.setValue(1.0 - value);\n"
                     "    Knob2.changed();\n"
                     "}\n"
                     "\n"
                     "inline function onKnob2Changed(component, value)\n"
                     "{\n"
                     "    Console.print(\"Knob2 callback: \" + value);\n"
                     "}\n"
                     "\n"
                     "Knob1.setControlCallback(onKnob1Changed);\n"
                     "Knob2.setControlCallback(onKnob2Changed);");
        
        // Set value on Knob1 - this should trigger Knob1's callback,
        // which then triggers Knob2's callback
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("componentId", "Knob1");
        bodyObj->setProperty("value", 0.3);
        
        auto response = ctx->httpPost("/api/set_component_value",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        
        auto logs = json["logs"];
        
        // Verify BOTH callbacks' output appears in logs
        // Float normalization should clean up values (0.300000011920929 -> 0.3)
        bool hasKnob1Log = false;
        bool hasKnob2Log = false;
        
        for (int i = 0; i < logs.size(); i++)
        {
            String logEntry = logs[i].toString();
            if (logEntry.contains("Knob1 callback: 0.3"))
                hasKnob1Log = true;
            if (logEntry.contains("Knob2 callback: 0.7"))
                hasKnob2Log = true;
        }
        
        expect(hasKnob1Log, "Should capture Knob1's direct callback log");
        expect(hasKnob2Log, "Should capture Knob2's chained callback log (triggered via .changed())");
    }
    
    //==========================================================================
    void testFloatNormalizationUnit()
    {
        beginTest("Float normalization utility");
        
        // Test 1: Double with FP noise
        {
            var v = 0.300000011920929;
            RestServer::normalizeFloatsInVar(v);
            expectEquals((double)v, 0.3, "Should round 0.300000011920929 to 0.3");
        }
        
        // Test 2: Clean double
        {
            var v = 0.5;
            RestServer::normalizeFloatsInVar(v);
            expectEquals((double)v, 0.5, "Should preserve clean 0.5");
        }
        
        // Test 3: Integer-valued double
        {
            var v = 42.0;
            RestServer::normalizeFloatsInVar(v);
            expectEquals((double)v, 42.0, "Should preserve 42.0");
        }
        
        // Test 4: String with embedded FP noise
        {
            var v = "Value: 0.300000011920929";
            RestServer::normalizeFloatsInVar(v);
            expectEquals(v.toString(), String("Value: 0.3"), "Should clean FP noise in string");
        }
        
        // Test 5: String with multiple numbers
        {
            var v = "From 0.100000001 to 0.899999999";
            RestServer::normalizeFloatsInVar(v);
            expectEquals(v.toString(), String("From 0.1 to 0.9"), "Should clean multiple numbers");
        }
        
        // Test 6: String with short decimals (< 5) - unchanged
        {
            var v = "Value: 0.123";
            RestServer::normalizeFloatsInVar(v);
            expectEquals(v.toString(), String("Value: 0.123"), "Should preserve < 5 decimals");
        }
        
        // Test 7: Array of mixed values
        {
            Array<var> arr;
            arr.add(0.300000011920929);
            arr.add("Text: 0.700000001");
            arr.add(42);
            var v = arr;
            RestServer::normalizeFloatsInVar(v);
            
            expectEquals((double)v[0], 0.3, "Array double normalized");
            expectEquals(v[1].toString(), String("Text: 0.7"), "Array string normalized");
            expectEquals((int)v[2], 42, "Array int unchanged");
        }
        
        // Test 8: Nested object
        {
            DynamicObject::Ptr obj = new DynamicObject();
            obj->setProperty("value", 0.300000011920929);
            obj->setProperty("message", "Got 0.999999999");
            obj->setProperty("count", 5);
            var v = var(obj.get());
            RestServer::normalizeFloatsInVar(v);
            
            auto* result = v.getDynamicObject();
            expectEquals((double)result->getProperty("value"), 0.3, "Object double normalized");
            expectEquals(result->getProperty("message").toString(), String("Got 1.0"), "Object string normalized");
            expectEquals((int)result->getProperty("count"), 5, "Object int unchanged");
        }
        
        // Test 9: Negative value
        {
            var v = -0.300000011920929;
            RestServer::normalizeFloatsInVar(v);
            expectEquals((double)v, -0.3, "Should handle negative values");
        }
        
        // Test 10: Very small value
        {
            var v = 0.00009999999;
            RestServer::normalizeFloatsInVar(v);
            expectEquals((double)v, 0.0001, "Should round very small values");
        }
    }
    
    //==========================================================================
    void testFloatNormalization()
    {
        beginTest("REST API normalizes floating point values");
        
        ctx->reset();
        
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);\n"
                     "\n"
                     "inline function onTestKnobChanged(component, value)\n"
                     "{\n"
                     "    Console.print(\"Value: \" + value);\n"
                     "}\n"
                     "\n"
                     "TestKnob.setControlCallback(onTestKnobChanged);");
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("componentId", "TestKnob");
        bodyObj->setProperty("value", 0.1);
        
        auto response = ctx->httpPost("/api/set_component_value",
                                      JSON::toString(var(bodyObj.get())));
        
        expect(!response.contains("0.10000000"), 
               "Response should not contain FP noise");
        expect(!response.contains("0.0999999"),
               "Response should not contain FP noise");
        
        var json = ctx->parseJson(response);
        auto logs = json["logs"];
        
        bool hasCleanValue = false;
        for (int i = 0; i < logs.size(); i++)
        {
            if (logs[i].toString() == "Value: 0.1")
                hasCleanValue = true;
        }
        expect(hasCleanValue, "Log should contain clean 'Value: 0.1'");
    }
};

static RestServerTest restServerTest;

} // namespace hise

#endif // HI_RUN_UNIT_TESTS
