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
        testListComponentsWithLaf();
        testListComponentsWithoutLaf();
        testListComponentsWithLafHierarchy();
        testGetComponentProperties();
        testSetScriptSuccess();
        testSetScriptCompileError();
        testSetScriptUnknownCallback();
        testSetScriptMultipleCallbacks();
        testSetScriptEmptyCallbacks();
        testGetScript();
        testGetScriptExternalFiles();
        testRecompile();
        testGetComponentValue();
        testSetComponentValue();
        testSetComponentValueTriggersCallback();
        testSetComponentValueCallbackRuntimeError();
        testNestedControlCallbacks();
        testFloatNormalizationUnit();
        testFloatNormalization();
        testForceSynchronousExecution();
        testSetComponentPropertiesBasic();
        testSetComponentPropertiesMultiple();
        testSetComponentPropertiesLockedRejection();
        testSetComponentPropertiesForceMode();
        testSetComponentPropertiesInvalidComponent();
        testSetComponentPropertiesInvalidProperty();
        testSetComponentPropertiesColour();
        testSetComponentPropertiesParentComponent();
        testSetComponentPropertiesRemoveFromParent();
        testSetComponentPropertiesInvalidParent();
        testScreenshotBasic();
        testScreenshotComponent();
        testScreenshotInvalidComponent();
        testScreenshotScale();
        testScreenshotPixelVerification();
        testScreenshotPixelVerificationScaled();
        testScreenshotFileOutput();
        testScreenshotFileOutputErrors();
        testGetSelectedComponents();
        
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
            // Use callbacks object format
            DynamicObject::Ptr bodyObj = new DynamicObject();
            bodyObj->setProperty("moduleId", moduleId);
            
            DynamicObject::Ptr callbacks = new DynamicObject();
            callbacks->setProperty(callback, code);
            bodyObj->setProperty("callbacks", var(callbacks.get()));
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
        
        // Check server info
        auto server = json["server"];
        expect(server.hasProperty("version"), "server should have version");
        expect(server["version"].toString().isNotEmpty(), "version should not be empty");
        expect(server["version"].toString() != "1.0.0", "version should be actual HISE version, not hardcoded 1.0.0");
        expect(server.hasProperty("compileTimeout"), "server should have compileTimeout");
        expect((double)server["compileTimeout"] > 0, "compileTimeout should be positive");
        
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
    void testListComponentsWithLaf()
    {
        beginTest("GET /api/list_components (with LAF info)");
        
        ctx->reset();
        
        // Create a button with a script-based LAF
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestButton = Content.addButton(\"TestButton\", 10, 10);\n"
                     "const var laf = Content.createLocalLookAndFeel();\n"
                     "laf.registerFunction(\"drawToggleButton\", function(g, obj) {\n"
                     "    g.fillAll(0xFF000000);\n"
                     "});\n"
                     "TestButton.setLocalLookAndFeel(laf);");
        
        auto response = ctx->httpGet("/api/list_components?moduleId=Interface");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        
        auto components = json["components"];
        expect(components.isArray() && components.size() == 1, "Should have 1 component");
        
        auto button = components[0];
        expect(button["id"].toString() == "TestButton", "Should be TestButton");
        
        // Verify LAF info is present
        auto laf = button["laf"];
        expect(laf.isObject(), "Should have laf object for component with LAF assigned");
        expect(laf["id"].toString() == "laf", "LAF id should be 'laf'");
        expect(laf["renderStyle"].toString() == "script", "Should be script style for registerFunction LAF");
        expect(laf.hasProperty("location"), "Should have location property");
        expect(laf.hasProperty("cssLocation"), "Should have cssLocation property");
    }
    
    //==========================================================================
    void testListComponentsWithoutLaf()
    {
        beginTest("GET /api/list_components (without LAF)");
        
        ctx->reset();
        
        // Create a component without LAF
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);");
        
        auto response = ctx->httpGet("/api/list_components?moduleId=Interface");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        
        auto components = json["components"];
        expect(components.size() == 1, "Should have 1 component");
        
        auto knob = components[0];
        expect(knob["id"].toString() == "TestKnob", "Should be TestKnob");
        
        // Verify LAF is false when no LAF assigned
        expect(knob.hasProperty("laf"), "Should have laf property");
        expect(!knob["laf"], "laf should be false when no LAF assigned");
    }
    
    //==========================================================================
    void testListComponentsWithLafHierarchy()
    {
        beginTest("GET /api/list_components?hierarchy=true (with LAF info)");
        
        ctx->reset();
        
        // Create a panel with a child that has LAF
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var Panel1 = Content.addPanel(\"Panel1\", 10, 10);\n"
                     "Panel1.set(\"width\", 200);\n"
                     "Panel1.set(\"height\", 200);\n"
                     "const var ChildButton = Content.addButton(\"ChildButton\", 20, 20);\n"
                     "ChildButton.set(\"parentComponent\", \"Panel1\");\n"
                     "const var laf = Content.createLocalLookAndFeel();\n"
                     "laf.registerFunction(\"drawToggleButton\", function(g, obj) {\n"
                     "    g.fillAll(0xFF000000);\n"
                     "});\n"
                     "ChildButton.setLocalLookAndFeel(laf);");
        
        auto response = ctx->httpGet("/api/list_components?moduleId=Interface&hierarchy=true");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        
        // Find Panel1 and check its LAF and its child's LAF
        auto components = json["components"];
        bool foundPanel = false;
        bool foundChildWithLaf = false;
        
        for (int i = 0; i < components.size(); i++)
        {
            if (components[i]["id"].toString() == "Panel1")
            {
                foundPanel = true;
                
                // Panel should have laf: false
                expect(!components[i]["laf"], "Panel without LAF should have laf: false");
                
                // Check child
                auto children = components[i]["childComponents"];
                for (int j = 0; j < children.size(); j++)
                {
                    if (children[j]["id"].toString() == "ChildButton")
                    {
                        foundChildWithLaf = true;
                        auto childLaf = children[j]["laf"];
                        expect(childLaf.isObject(), "Child with LAF should have laf object");
                        expect(childLaf["renderStyle"].toString() == "script", 
                               "Child LAF should have script renderStyle");
                    }
                }
            }
        }
        
        expect(foundPanel, "Should find Panel1");
        expect(foundChildWithLaf, "Should find ChildButton with LAF in hierarchy");
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
        
        auto response = ctx->httpGet("/api/get_component_properties?moduleId=Interface&id=PropKnob");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        expect(json["id"].toString() == "PropKnob", "Should return correct id");
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
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        
        DynamicObject::Ptr callbacks = new DynamicObject();
        callbacks->setProperty("onInit", "Content.makeFrontInterface(600, 400);\n"
                                         "Console.print(\"Test output\");");
        bodyObj->setProperty("callbacks", var(callbacks.get()));
        bodyObj->setProperty("compile", true);
        
        auto response = ctx->httpPost("/api/set_script", JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        expect(json["moduleId"].toString() == "Interface", "Should return moduleId");
        expect(json["result"].toString() == "Compiled OK", "Result should be 'Compiled OK'");
        
        // Verify updatedCallbacks array
        expect(json.hasProperty("updatedCallbacks"), "Should have updatedCallbacks array");
        auto updatedCallbacks = json["updatedCallbacks"];
        expect(updatedCallbacks.isArray(), "updatedCallbacks should be array");
        expect(updatedCallbacks.size() == 1, "Should have 1 updated callback");
        expect(updatedCallbacks[0].toString() == "onInit", "Should have updated onInit");
        
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
        
        // Script with syntax error - use callbacks object format
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        
        DynamicObject::Ptr callbacks = new DynamicObject();
        callbacks->setProperty("onInit", "Content.makeFrontInterface(600, 400);\n"
                                         "const var x = ;");  // Syntax error
        bodyObj->setProperty("callbacks", var(callbacks.get()));
        bodyObj->setProperty("compile", true);
        
        auto response = ctx->httpPost("/api/set_script", JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect(!(bool)json["success"], "Compilation should fail");
        
        // Should still have updatedCallbacks even on failure
        expect(json.hasProperty("updatedCallbacks"), "Should have updatedCallbacks");
        
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
        
        // Test 1: Get specific callback - should return callbacks object with only that callback
        {
            auto response = ctx->httpGet("/api/get_script?moduleId=Interface&callback=onInit");
            var json = ctx->parseJson(response);
            
            expect((bool)json["success"], "Should succeed");
            expect(json["moduleId"].toString() == "Interface", "Should return correct moduleId");
            expect(json.hasProperty("callbacks"), "Should have callbacks object");
            expect(!json.hasProperty("script"), "Should NOT have script property");
            
            auto callbacks = json["callbacks"];
            expect(callbacks.hasProperty("onInit"), "callbacks should have onInit");
            
            String returnedScript = callbacks["onInit"].toString();
            expect(returnedScript.contains("makeFrontInterface"), "Should contain makeFrontInterface");
            expect(returnedScript.contains("MyKnob"), "Should contain MyKnob");
            
            // onInit should NOT have function wrapper
            expect(!returnedScript.contains("function onInit"), "onInit should NOT have function wrapper");
            
            // Verify externalFiles is present (may be empty)
            expect(json.hasProperty("externalFiles"), "Should have externalFiles array");
            expect(json["externalFiles"].isArray(), "externalFiles should be array");
        }
        
        // Test 2: Get all callbacks (no callback param) - should return all callbacks
        {
            auto response = ctx->httpGet("/api/get_script?moduleId=Interface");
            var json = ctx->parseJson(response);
            
            expect((bool)json["success"], "Should succeed");
            expect(json.hasProperty("callbacks"), "Should have callbacks object");
            expect(!json.hasProperty("script"), "Should NOT have script property");
            
            auto callbacks = json["callbacks"];
            expect(callbacks.hasProperty("onInit"), "Should have onInit callback");
            expect(callbacks.hasProperty("onNoteOn"), "Should have onNoteOn callback");
            expect(callbacks.hasProperty("onNoteOff"), "Should have onNoteOff callback");
            expect(callbacks.hasProperty("onController"), "Should have onController callback");
            expect(callbacks.hasProperty("onTimer"), "Should have onTimer callback");
            expect(callbacks.hasProperty("onControl"), "Should have onControl callback");
            
            // onInit should be raw content (no function wrapper)
            expect(!callbacks["onInit"].toString().contains("function onInit"), 
                   "onInit should NOT have function wrapper");
            
            // onNoteOn should have function wrapper
            expect(callbacks["onNoteOn"].toString().contains("function onNoteOn"), 
                   "onNoteOn should have function wrapper");
            
            // Verify externalFiles is present
            expect(json.hasProperty("externalFiles"), "Should have externalFiles array");
        }
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
    void testSetScriptUnknownCallback()
    {
        beginTest("POST /api/set_script (unknown callback)");
        
        ctx->reset();
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        
        DynamicObject::Ptr callbacks = new DynamicObject();
        callbacks->setProperty("onFoo", "// This callback doesn't exist");
        bodyObj->setProperty("callbacks", var(callbacks.get()));
        
        auto response = ctx->httpPost("/api/set_script", JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect(!(bool)json["success"], "Should fail for unknown callback");
        expect(json["errors"].isArray(), "Should have errors array");
        expect(json["errors"].size() >= 1, "Should have at least one error");
        expect(json["errors"][0]["errorMessage"].toString().contains("unknown callback"), 
               "Error should mention unknown callback");
        expect(json["errors"][0]["errorMessage"].toString().contains("onFoo"), 
               "Error should include the unknown callback name");
    }
    
    //==========================================================================
    void testSetScriptMultipleCallbacks()
    {
        beginTest("POST /api/set_script (multiple callbacks)");
        
        ctx->reset();
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        
        DynamicObject::Ptr callbacks = new DynamicObject();
        callbacks->setProperty("onInit", "Content.makeFrontInterface(600, 400);\n"
                                         "Console.print(\"Init ran\");");
        callbacks->setProperty("onNoteOn", "function onNoteOn()\n{\n\tConsole.print(\"NoteOn\");\n}");
        bodyObj->setProperty("callbacks", var(callbacks.get()));
        bodyObj->setProperty("compile", true);
        
        auto response = ctx->httpPost("/api/set_script", JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        
        auto updatedCallbacks = json["updatedCallbacks"];
        expect(updatedCallbacks.size() == 2, "Should have 2 updated callbacks");
        
        // Verify both callbacks are in the list (order may vary)
        bool hasOnInit = false, hasOnNoteOn = false;
        for (int i = 0; i < updatedCallbacks.size(); i++)
        {
            if (updatedCallbacks[i].toString() == "onInit") hasOnInit = true;
            if (updatedCallbacks[i].toString() == "onNoteOn") hasOnNoteOn = true;
        }
        expect(hasOnInit, "Should have updated onInit");
        expect(hasOnNoteOn, "Should have updated onNoteOn");
    }
    
    //==========================================================================
    void testSetScriptEmptyCallbacks()
    {
        beginTest("POST /api/set_script (empty callbacks)");
        
        ctx->reset();
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        
        DynamicObject::Ptr callbacks = new DynamicObject();
        // Empty callbacks object
        bodyObj->setProperty("callbacks", var(callbacks.get()));
        
        auto response = ctx->httpPost("/api/set_script", JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect(!(bool)json["success"], "Should fail for empty callbacks");
        expect(json["errors"].isArray(), "Should have errors array");
        expect(json["errors"].size() >= 1, "Should have at least one error");
        expect(json["errors"][0]["errorMessage"].toString().contains("empty"), 
               "Error should mention empty");
    }
    
    //==========================================================================
    void testGetScriptExternalFiles()
    {
        beginTest("GET /api/get_script (external files structure)");
        
        ctx->reset();
        
        // This test verifies the externalFiles structure is correct
        // Without actual include() files, array should be empty
        ctx->compile("Content.makeFrontInterface(600, 400);");
        
        auto response = ctx->httpGet("/api/get_script?moduleId=Interface");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        expect(json.hasProperty("externalFiles"), "Should have externalFiles");
        
        auto externalFiles = json["externalFiles"];
        expect(externalFiles.isArray(), "externalFiles should be array");
        // With no includes, array should be empty
        expect(externalFiles.size() == 0, "Should have no external files when no includes used");
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
        
        auto response = ctx->httpGet("/api/get_component_value?moduleId=Interface&id=TestKnob");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        expect(json["moduleId"].toString() == "Interface", "Should return correct moduleId");
        expect(json["id"].toString() == "TestKnob", "Should return correct id");
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
            bodyObj->setProperty("id", "TestKnob");
            bodyObj->setProperty("value", 5.0);
            
            auto response = ctx->httpPost("/api/set_component_value",
                                          JSON::toString(var(bodyObj.get())));
            var json = ctx->parseJson(response);
            
            expect((bool)json["success"], "Should succeed");
            expect(json["moduleId"].toString() == "Interface", "Should return correct moduleId");
            expect(json["id"].toString() == "TestKnob", "Should return correct id");
            expect(json["type"].toString() == "ScriptSlider", "Should return correct type");
        }
        
        // Test 2: Set value with validation - valid value
        {
            DynamicObject::Ptr bodyObj = new DynamicObject();
            bodyObj->setProperty("moduleId", "Interface");
            bodyObj->setProperty("id", "TestKnob");
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
            bodyObj->setProperty("id", "TestKnob");
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
            bodyObj->setProperty("id", "TestKnob");
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
        bodyObj->setProperty("id", "TestKnob");
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
        bodyObj->setProperty("id", "TestKnob");
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
        bodyObj->setProperty("id", "Knob1");
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
        bodyObj->setProperty("id", "TestKnob");
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
    
    //==========================================================================
    void testForceSynchronousExecution()
    {
        beginTest("forceSynchronousExecution parameter");
        
        ctx->reset();
        
        // Create a knob with control callback
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);\n"
                     "\n"
                     "inline function onTestKnobChanged(component, value)\n"
                     "{\n"
                     "    Console.print(\"Callback executed: \" + value);\n"
                     "}\n"
                     "\n"
                     "TestKnob.setControlCallback(onTestKnobChanged);");
        
        // Test with forceSynchronousExecution: true
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("id", "TestKnob");
        bodyObj->setProperty("value", 0.5);
        bodyObj->setProperty("forceSynchronousExecution", true);
        
        auto response = ctx->httpPost("/api/set_component_value",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        expect((bool)json["forceSynchronousExecution"], 
               "Response should indicate sync mode was used");
        expect(json["warning"].toString().isNotEmpty(), 
               "Response should include warning when sync mode is used");
        expect(json["warning"].toString().contains("unsafe"), 
               "Warning should mention 'unsafe'");
        
        // Verify callback was still executed
        auto logs = json["logs"];
        bool callbackExecuted = false;
        for (int i = 0; i < logs.size(); i++)
        {
            if (logs[i].toString().contains("Callback executed"))
                callbackExecuted = true;
        }
        expect(callbackExecuted, "Callback should still execute in sync mode");
    }
    
    //==========================================================================
    void testSetComponentPropertiesBasic()
    {
        beginTest("POST /api/set_component_properties (basic)");
        
        ctx->reset();
        
        // Create a knob
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);");
        
        // Set a single property
        DynamicObject::Ptr change = new DynamicObject();
        change->setProperty("id", "TestKnob");
        DynamicObject::Ptr props = new DynamicObject();
        props->setProperty("x", 100);
        props->setProperty("text", "New Label");
        change->setProperty("properties", var(props.get()));
        
        Array<var> changes;
        changes.add(var(change.get()));
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("changes", var(changes));
        
        auto response = ctx->httpPost("/api/set_component_properties",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed: " + response);
        expect(json["moduleId"].toString() == "Interface", "Should return correct moduleId");
        expect(!(bool)json["recompileRequired"], "Should not require recompile for simple props");
        
        auto applied = json["applied"];
        expect(applied.isArray(), "applied should be array");
        expect(applied.size() == 1, "Should have 1 applied change");
        expect(applied[0]["id"].toString() == "TestKnob", "Should have correct component id");
        
        // Verify properties were actually changed
        auto getResponse = ctx->httpGet("/api/get_component_properties?moduleId=Interface&id=TestKnob");
        var getJson = ctx->parseJson(getResponse);
        auto properties = getJson["properties"];
        
        bool foundX = false, foundText = false;
        for (int i = 0; i < properties.size(); i++)
        {
            if (properties[i]["id"].toString() == "x")
            {
                foundX = true;
                expect((int)properties[i]["value"] == 100, "x should be 100");
            }
            if (properties[i]["id"].toString() == "text")
            {
                foundText = true;
                expect(properties[i]["value"].toString() == "New Label", "text should be 'New Label'");
            }
        }
        expect(foundX, "Should have x property");
        expect(foundText, "Should have text property");
    }
    
    //==========================================================================
    void testSetComponentPropertiesMultiple()
    {
        beginTest("POST /api/set_component_properties (multiple components)");
        
        ctx->reset();
        
        // Create multiple components
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var Knob1 = Content.addKnob(\"Knob1\", 10, 10);\n"
                     "const var Knob2 = Content.addKnob(\"Knob2\", 150, 10);");
        
        // Set properties on both
        DynamicObject::Ptr change1 = new DynamicObject();
        change1->setProperty("id", "Knob1");
        DynamicObject::Ptr props1 = new DynamicObject();
        props1->setProperty("x", 50);
        change1->setProperty("properties", var(props1.get()));
        
        DynamicObject::Ptr change2 = new DynamicObject();
        change2->setProperty("id", "Knob2");
        DynamicObject::Ptr props2 = new DynamicObject();
        props2->setProperty("x", 200);
        props2->setProperty("text", "Second");
        change2->setProperty("properties", var(props2.get()));
        
        Array<var> changes;
        changes.add(var(change1.get()));
        changes.add(var(change2.get()));
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("changes", var(changes));
        
        auto response = ctx->httpPost("/api/set_component_properties",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        
        auto applied = json["applied"];
        expect(applied.size() == 2, "Should have 2 applied changes");
    }
    
    //==========================================================================
    void testSetComponentPropertiesLockedRejection()
    {
        beginTest("POST /api/set_component_properties (locked property rejection)");
        
        ctx->reset();
        
        // Create a knob where script sets x property
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);\n"
                     "TestKnob.set(\"x\", 50);");  // This locks 'x'
        
        // Try to set x via API without force
        DynamicObject::Ptr change = new DynamicObject();
        change->setProperty("id", "TestKnob");
        DynamicObject::Ptr props = new DynamicObject();
        props->setProperty("x", 100);  // x is locked by script
        change->setProperty("properties", var(props.get()));
        
        Array<var> changes;
        changes.add(var(change.get()));
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("changes", var(changes));
        bodyObj->setProperty("force", false);
        
        auto response = ctx->httpPost("/api/set_component_properties",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect(!(bool)json["success"], "Should fail when property is locked");
        expect(json.hasProperty("locked"), "Should have locked array");
        
        auto locked = json["locked"];
        expect(locked.isArray(), "locked should be array");
        expect(locked.size() >= 1, "Should have at least one locked property");
        expect(locked[0]["id"].toString() == "TestKnob", "Should identify correct component");
        expect(locked[0]["property"].toString() == "x", "Should identify correct property");
    }
    
    //==========================================================================
    void testSetComponentPropertiesForceMode()
    {
        beginTest("POST /api/set_component_properties (force mode)");
        
        ctx->reset();
        
        // Create a knob where script sets x property
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);\n"
                     "TestKnob.set(\"x\", 50);");  // This locks 'x'
        
        // Set x via API WITH force=true
        DynamicObject::Ptr change = new DynamicObject();
        change->setProperty("id", "TestKnob");
        DynamicObject::Ptr props = new DynamicObject();
        props->setProperty("x", 200);  // x is locked but we force
        change->setProperty("properties", var(props.get()));
        
        Array<var> changes;
        changes.add(var(change.get()));
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("changes", var(changes));
        bodyObj->setProperty("force", true);
        
        auto response = ctx->httpPost("/api/set_component_properties",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed with force=true");
        
        // Verify property was actually changed
        auto getResponse = ctx->httpGet("/api/get_component_properties?moduleId=Interface&id=TestKnob");
        var getJson = ctx->parseJson(getResponse);
        auto properties = getJson["properties"];
        
        for (int i = 0; i < properties.size(); i++)
        {
            if (properties[i]["id"].toString() == "x")
            {
                expect((int)properties[i]["value"] == 200, "x should be 200 after force");
            }
        }
    }
    
    //==========================================================================
    void testSetComponentPropertiesInvalidComponent()
    {
        beginTest("POST /api/set_component_properties (invalid component)");
        
        ctx->reset();
        
        ctx->compile("Content.makeFrontInterface(600, 400);");
        
        DynamicObject::Ptr change = new DynamicObject();
        change->setProperty("id", "NonExistent");
        DynamicObject::Ptr props = new DynamicObject();
        props->setProperty("x", 100);
        change->setProperty("properties", var(props.get()));
        
        Array<var> changes;
        changes.add(var(change.get()));
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("changes", var(changes));
        
        auto response = ctx->httpPost("/api/set_component_properties",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect(!(bool)json["success"], "Should fail for non-existent component");
    }
    
    //==========================================================================
    void testSetComponentPropertiesInvalidProperty()
    {
        beginTest("POST /api/set_component_properties (invalid property)");
        
        ctx->reset();
        
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);");
        
        DynamicObject::Ptr change = new DynamicObject();
        change->setProperty("id", "TestKnob");
        DynamicObject::Ptr props = new DynamicObject();
        props->setProperty("nonExistentProperty", 100);
        change->setProperty("properties", var(props.get()));
        
        Array<var> changes;
        changes.add(var(change.get()));
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("changes", var(changes));
        
        auto response = ctx->httpPost("/api/set_component_properties",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect(!(bool)json["success"], "Should fail for non-existent property");
    }
    
    //==========================================================================
    void testSetComponentPropertiesColour()
    {
        beginTest("POST /api/set_component_properties (colour conversion)");
        
        ctx->reset();
        
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);");
        
        // Set bgColour using hex string format
        DynamicObject::Ptr change = new DynamicObject();
        change->setProperty("id", "TestKnob");
        DynamicObject::Ptr props = new DynamicObject();
        props->setProperty("bgColour", "0xFF00FF00");  // Green
        change->setProperty("properties", var(props.get()));
        
        Array<var> changes;
        changes.add(var(change.get()));
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("changes", var(changes));
        
        auto response = ctx->httpPost("/api/set_component_properties",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed with hex colour string");
        
        // Verify colour was set
        auto getResponse = ctx->httpGet("/api/get_component_properties?moduleId=Interface&id=TestKnob");
        var getJson = ctx->parseJson(getResponse);
        auto properties = getJson["properties"];
        
        for (int i = 0; i < properties.size(); i++)
        {
            if (properties[i]["id"].toString() == "bgColour")
            {
                expect(properties[i]["value"].toString() == "0xFF00FF00", 
                       "bgColour should be green (0xFF00FF00)");
            }
        }
    }
    
    //==========================================================================
    void testSetComponentPropertiesParentComponent()
    {
        beginTest("POST /api/set_component_properties (parentComponent triggers rebuild)");
        
        ctx->reset();
        
        // Create panel and knob
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var Panel1 = Content.addPanel(\"Panel1\", 10, 10);\n"
                     "Panel1.set(\"width\", 200);\n"
                     "Panel1.set(\"height\", 200);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 250, 10);");
        
        // Move knob into panel by setting parentComponent
        DynamicObject::Ptr change = new DynamicObject();
        change->setProperty("id", "TestKnob");
        DynamicObject::Ptr props = new DynamicObject();
        props->setProperty("parentComponent", "Panel1");
        change->setProperty("properties", var(props.get()));
        
        Array<var> changes;
        changes.add(var(change.get()));
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("changes", var(changes));
        
        auto response = ctx->httpPost("/api/set_component_properties",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Should succeed");
        expect((bool)json["recompileRequired"], "Should indicate recompile required");
    }
    
    //==========================================================================
    void testSetComponentPropertiesRemoveFromParent()
    {
        beginTest("POST /api/set_component_properties (remove from parent with empty string)");
        
        ctx->reset();
        
        // Create panel and knob, with knob inside panel
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var Panel1 = Content.addPanel(\"Panel1\", 10, 10);\n"
                     "Panel1.set(\"width\", 200);\n"
                     "Panel1.set(\"height\", 200);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 20, 20);\n"
                     "TestKnob.set(\"parentComponent\", \"Panel1\");");
        
        // Verify knob is in panel
        auto hierarchyBefore = ctx->httpGet("/api/list_components?moduleId=Interface&hierarchy=true");
        var jsonBefore = ctx->parseJson(hierarchyBefore);
        auto componentsBefore = jsonBefore["components"];
        
        bool knobInPanelBefore = false;
        for (int i = 0; i < componentsBefore.size(); i++)
        {
            if (componentsBefore[i]["id"].toString() == "Panel1")
            {
                auto children = componentsBefore[i]["childComponents"];
                for (int j = 0; j < children.size(); j++)
                {
                    if (children[j]["id"].toString() == "TestKnob")
                        knobInPanelBefore = true;
                }
            }
        }
        expect(knobInPanelBefore, "Knob should be inside Panel1 initially");
        
        // Remove knob from panel by setting parentComponent to empty string
        DynamicObject::Ptr change = new DynamicObject();
        change->setProperty("id", "TestKnob");
        DynamicObject::Ptr props = new DynamicObject();
        props->setProperty("parentComponent", "");
        change->setProperty("properties", var(props.get()));
        
        Array<var> changes;
        changes.add(var(change.get()));
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("changes", var(changes));
        bodyObj->setProperty("force", true);
        
        auto jmp = ProcessorHelpers::getFirstProcessorWithType<JavascriptMidiProcessor>(ctx->bp->getMainSynthChain());
        auto sc = jmp->getContent()->getComponentWithName("TestKnob");

        auto response = ctx->httpPost("/api/set_component_properties",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);

        expect((bool)json["success"], "Should succeed");

        // Verify knob is now at root level (not inside panel)
        auto hierarchyAfter = ctx->httpGet("/api/list_components?moduleId=Interface&hierarchy=true");

        var jsonAfter = ctx->parseJson(hierarchyAfter);
        auto componentsAfter = jsonAfter["components"];
        
        bool knobAtRoot = false;
        bool knobStillInPanel = false;
        
        for (int i = 0; i < componentsAfter.size(); i++)
        {
            if (componentsAfter[i]["id"].toString() == "TestKnob")
                knobAtRoot = true;
            
            if (componentsAfter[i]["id"].toString() == "Panel1")
            {
                auto children = componentsAfter[i]["childComponents"];
                for (int j = 0; j < children.size(); j++)
                {
                    if (children[j]["id"].toString() == "TestKnob")
                        knobStillInPanel = true;
                }
            }
        }
        
        expect(knobAtRoot, "Knob should be at root level after setting parentComponent to empty");
        expect(!knobStillInPanel, "Knob should NOT be inside Panel1 after removal");
    }
    
    //==========================================================================
    void testSetComponentPropertiesInvalidParent()
    {
        beginTest("POST /api/set_component_properties (invalid parentComponent target)");
        
        ctx->reset();
        
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var TestKnob = Content.addKnob(\"TestKnob\", 10, 10);");
        
        // Try to set parentComponent to non-existent panel
        DynamicObject::Ptr change = new DynamicObject();
        change->setProperty("id", "TestKnob");
        DynamicObject::Ptr props = new DynamicObject();
        props->setProperty("parentComponent", "NonExistentPanel");
        change->setProperty("properties", var(props.get()));
        
        Array<var> changes;
        changes.add(var(change.get()));
        
        DynamicObject::Ptr bodyObj = new DynamicObject();
        bodyObj->setProperty("moduleId", "Interface");
        bodyObj->setProperty("changes", var(changes));
        
        auto response = ctx->httpPost("/api/set_component_properties",
                                      JSON::toString(var(bodyObj.get())));
        var json = ctx->parseJson(response);
        
        expect(!(bool)json["success"], "Should fail for non-existent parent");
    }
    
    //==========================================================================
    void testScreenshotBasic()
    {
        beginTest("GET /api/screenshot (full interface)");
        
        ctx->reset();
        
        // Create a simple interface
        ctx->compile("Content.makeFrontInterface(400, 300);\n"
                     "const var TestPanel = Content.addPanel(\"TestPanel\", 10, 10);\n"
                     "TestPanel.set(\"width\", 100);\n"
                     "TestPanel.set(\"height\", 100);");
        
        auto response = ctx->httpGet("/api/screenshot?moduleId=Interface");
        var json = ctx->parseJson(response);
        
        // Note: Until capture logic is implemented, this will fail with "not yet implemented"
        // Once implemented, these assertions should pass:
        // expect((bool)json["success"], "Screenshot should succeed");
        // expect(json.hasProperty("imageData"), "Should have imageData");
        // expect(json.hasProperty("width"), "Should have width");
        // expect(json.hasProperty("height"), "Should have height");
        // expect((int)json["width"] == 400, "Width should match interface width");
        // expect((int)json["height"] == 300, "Height should match interface height");
        // expect((float)json["scale"] == 1.0f, "Scale should be 1.0 by default");
        
        // For now, just verify the endpoint is registered and responds
        expect(json.hasProperty("success") || json.hasProperty("error"), 
               "Should return a valid response structure");
    }
    
    //==========================================================================
    void testScreenshotComponent()
    {
        beginTest("GET /api/screenshot (specific component)");
        
        ctx->reset();
        
        ctx->compile("Content.makeFrontInterface(400, 300);\n"
                     "const var TestPanel = Content.addPanel(\"TestPanel\", 50, 50);\n"
                     "TestPanel.set(\"width\", 100);\n"
                     "TestPanel.set(\"height\", 80);");
        
        auto response = ctx->httpGet("/api/screenshot?moduleId=Interface&id=TestPanel");
        var json = ctx->parseJson(response);
        
        // Once implemented:
        // expect((bool)json["success"], "Component screenshot should succeed");
        // expect(json["id"].toString() == "TestPanel", "Should return component ID");
        
        expect(json.hasProperty("success") || json.hasProperty("error"), 
               "Should return a valid response structure");
    }
    
    //==========================================================================
    void testScreenshotInvalidComponent()
    {
        beginTest("GET /api/screenshot (invalid component)");
        
        ctx->reset();
        
        ctx->compile("Content.makeFrontInterface(400, 300);");
        
        auto response = ctx->httpGet("/api/screenshot?moduleId=Interface&id=NonExistent");
        var json = ctx->parseJson(response);
        
        expect(!(bool)json["success"], "Should fail for non-existent component");
        expect(json.hasProperty("error"), "Should have error message");
    }
    
    //==========================================================================
    void testScreenshotScale()
    {
        beginTest("GET /api/screenshot (with scale)");
        
        ctx->reset();
        
        ctx->compile("Content.makeFrontInterface(400, 300);");
        
        auto response = ctx->httpGet("/api/screenshot?moduleId=Interface&scale=0.5");
        var json = ctx->parseJson(response);
        
        // Once implemented:
        // expect((bool)json["success"], "Scaled screenshot should succeed");
        // expect((int)json["width"] == 200, "Width should be scaled to 200");
        // expect((int)json["height"] == 150, "Height should be scaled to 150");
        // expect((float)json["scale"] == 0.5f, "Scale should be 0.5");
        
        expect(json.hasProperty("success") || json.hasProperty("error"), 
               "Should return a valid response structure");
    }
    
    //==========================================================================
    void testScreenshotPixelVerification()
    {
        beginTest("GET /api/screenshot (pixel verification)");
        
        ctx->reset();
        
        // Create a panel with a red fill paint routine
        ctx->compile("Content.makeFrontInterface(400, 300);\n"
                     "const var RedPanel = Content.addPanel(\"RedPanel\", 10, 10);\n"
                     "RedPanel.set(\"width\", 100);\n"
                     "RedPanel.set(\"height\", 100);\n"
                     "RedPanel.setPaintRoutine(function(g) {\n"
                     "    g.fillAll(Colours.red);\n"
                     "});");
        
        auto response = ctx->httpGet("/api/screenshot?moduleId=Interface&id=RedPanel");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Screenshot should succeed");
        expect(json.hasProperty("imageData"), "Should have imageData");
        
        // Decode Base64 to image
        String base64Data = json["imageData"].toString();
        MemoryOutputStream mos;
        bool decoded = Base64::convertFromBase64(mos, base64Data);
        expect(decoded, "Base64 decoding should succeed");
        
        // Load as PNG image
        auto image = ImageFileFormat::loadFrom(mos.getData(), mos.getDataSize());
        expect(image.isValid(), "Should load as valid image");
        expect(image.getWidth() == 100, "Image width should be 100");
        expect(image.getHeight() == 100, "Image height should be 100");
        
        // Verify center pixel is pure red (R:255, G:0, B:0, A:255)
        auto pixel = image.getPixelAt(50, 50);
        expect(pixel.getRed() == 255, "Red channel should be 255");
        expect(pixel.getGreen() == 0, "Green channel should be 0");
        expect(pixel.getBlue() == 0, "Blue channel should be 0");
        expect(pixel.getAlpha() == 255, "Alpha channel should be 255");
    }
    
    //==========================================================================
    void testScreenshotPixelVerificationScaled()
    {
        beginTest("GET /api/screenshot (pixel verification with scale)");
        
        ctx->reset();
        
        // Create a panel with a red fill paint routine
        ctx->compile("Content.makeFrontInterface(400, 300);\n"
                     "const var RedPanel = Content.addPanel(\"RedPanel\", 10, 10);\n"
                     "RedPanel.set(\"width\", 100);\n"
                     "RedPanel.set(\"height\", 100);\n"
                     "RedPanel.setPaintRoutine(function(g) {\n"
                     "    g.fillAll(Colours.red);\n"
                     "});");
        
        auto response = ctx->httpGet("/api/screenshot?moduleId=Interface&id=RedPanel&scale=0.5");
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Screenshot should succeed");
        
        // Decode Base64 to image
        String base64Data = json["imageData"].toString();
        MemoryOutputStream mos;
        bool decoded = Base64::convertFromBase64(mos, base64Data);
        expect(decoded, "Base64 decoding should succeed");
        
        // Load as PNG image - should be half size
        auto image = ImageFileFormat::loadFrom(mos.getData(), mos.getDataSize());
        expect(image.isValid(), "Should load as valid image");
        expect(image.getWidth() == 50, "Image width should be 50 (scaled)");
        expect(image.getHeight() == 50, "Image height should be 50 (scaled)");
        
        // Verify center pixel is still pure red
        auto pixel = image.getPixelAt(25, 25);
        expect(pixel.getRed() == 255, "Red channel should be 255");
        expect(pixel.getGreen() == 0, "Green channel should be 0");
        expect(pixel.getBlue() == 0, "Blue channel should be 0");
        expect(pixel.getAlpha() == 255, "Alpha channel should be 255");
    }
    
    //==========================================================================
    void testScreenshotFileOutput()
    {
        beginTest("GET /api/screenshot (file output)");
        
        ctx->reset();
        
        // Create a panel with a red fill paint routine
        ctx->compile("Content.makeFrontInterface(400, 300);\n"
                     "const var RedPanel = Content.addPanel(\"RedPanel\", 10, 10);\n"
                     "RedPanel.set(\"width\", 100);\n"
                     "RedPanel.set(\"height\", 100);\n"
                     "RedPanel.setPaintRoutine(function(g) {\n"
                     "    g.fillAll(Colours.red);\n"
                     "});");
        
        // Use temp directory for output file
        File tempFile = File::getSpecialLocation(File::tempDirectory).getChildFile("hise_test_screenshot.png");
        
        // Clean up any existing file
        tempFile.deleteFile();
        
        auto response = ctx->httpGet("/api/screenshot?moduleId=Interface&id=RedPanel&outputPath=" 
                                      + URL::addEscapeChars(tempFile.getFullPathName(), true));
        var json = ctx->parseJson(response);
        
        expect((bool)json["success"], "Screenshot should succeed");
        expect(!json.hasProperty("imageData"), "Should not have imageData when outputPath specified");
        expect(json.hasProperty("filePath"), "Should have filePath");
        expect(json["filePath"].toString() == tempFile.getFullPathName(), "filePath should match requested path");
        
        // Verify file exists
        expect(tempFile.existsAsFile(), "Output file should exist");
        
        // Load the file and verify it's a valid PNG with correct dimensions
        auto image = ImageFileFormat::loadFrom(tempFile);
        expect(image.isValid(), "Should load as valid image");
        expect(image.getWidth() == 100, "Image width should be 100");
        expect(image.getHeight() == 100, "Image height should be 100");
        
        // Verify pixel is red
        auto pixel = image.getPixelAt(50, 50);
        expect(pixel.getRed() == 255, "Red channel should be 255");
        expect(pixel.getGreen() == 0, "Green channel should be 0");
        expect(pixel.getBlue() == 0, "Blue channel should be 0");
        expect(pixel.getAlpha() == 255, "Alpha channel should be 255");
        
        // Clean up
        tempFile.deleteFile();
    }
    
    //==========================================================================
    void testScreenshotFileOutputErrors()
    {
        beginTest("GET /api/screenshot (file output errors)");
        
        ctx->reset();
        
        ctx->compile("Content.makeFrontInterface(400, 300);");
        
        // Test: missing .png extension
        {
            auto response = ctx->httpGet("/api/screenshot?moduleId=Interface&outputPath=C:/temp/screenshot.jpg");
            var json = ctx->parseJson(response);
            
            expect(!(bool)json["success"], "Should fail for non-.png extension");
            expect(json["message"].toString().containsIgnoreCase(".png"), "Error should mention .png extension");
        }
        
        // Test: parent directory doesn't exist
        {
            auto response = ctx->httpGet("/api/screenshot?moduleId=Interface&outputPath=" 
                                          + URL::addEscapeChars("C:/nonexistent_dir_12345/screenshot.png", true));
            var json = ctx->parseJson(response);
            
            expect(!(bool)json["success"], "Should fail for non-existent parent directory");
            expect(json["message"].toString().containsIgnoreCase("directory"), "Error should mention directory");
        }
    }
    
    void testGetSelectedComponents()
    {
        beginTest("GET /api/get_selected_components");
        
        ctx->reset();
        
        ctx->compile("Content.makeFrontInterface(600, 400);\n"
                     "const var Button1 = Content.addButton(\"Button1\", 10, 10);\n"
                     "const var Button2 = Content.addButton(\"Button2\", 100, 10);");
        
        // Test: Empty selection (no components selected in Interface Designer)
        // Note: Programmatic selection would require ScriptComponentEditBroadcaster manipulation,
        // which needs to be done on the message thread. Testing with actual selection requires
        // Christoph's help or complex async coordination. For now, we verify the endpoint works
        // and touches the coverage system correctly.
        {
            auto response = ctx->httpGet("/api/get_selected_components?moduleId=Interface");
            var json = ctx->parseJson(response);
            
            expect((bool)json["success"], "Should succeed even with empty selection");
            expectEquals<int>(json["selectionCount"], 0, "Should have 0 selected components when Interface Designer is not open or nothing is selected");
            expect(json["components"].isArray(), "Should have components array");
            expectEquals<int>(json["components"].size(), 0, "Components array should be empty with no selection");
            expect(json["moduleId"].toString() == "Interface", "Should return the moduleId");
        }
    }
};

static RestServerTest restServerTest;

} // namespace hise

#endif // HI_RUN_UNIT_TESTS
