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

namespace hise {
using namespace juce;

struct RestApiEndpoints
{
	using RouteParameter = RestHelpers::RouteParameter;
	using ParamType      = RestHelpers::ParamType;
	using RouteMetadata  = RestHelpers::RouteMetadata;
	using ApiRoute       = RestHelpers::ApiRoute;

	/* /api/builder/apply */
	static void applyBuilder(Array<RouteMetadata>& m)
	{
		auto opItem = RouteParameter(RestApiIds::op, "Operation object")
			.withType(ParamType::Object)
			.withDiscriminator("op")
			.withVariant("add", "Add a new module (type, parent, chain, name)")
			.withVariant("remove", "Remove a module (target)")
			.withVariant("clone", "Clone a module (source, count, template?)")
			.withVariant("set_attributes", "Set parameter values (target, attributes, mode?)")
			.withVariant("set_id", "Rename a module (target, name)")
			.withVariant("set_bypassed", "Set bypass state (target, bypassed)")
			.withVariant("set_effect", "Set effect/network (target, effect)")
			.withProperty(RouteParameter(RestApiIds::op, "Operation type")
				.withEnumValues({ "add","remove","clone","set_attributes","set_id","set_bypassed","set_effect" }))
			.withProperty(RouteParameter(RestApiIds::type, "Module type ID").asOptional())
			.withProperty(RouteParameter(RestApiIds::parent, "Parent module name").asOptional())
			.withProperty(RouteParameter(RestApiIds::chain, "Chain index (-1=direct, 0=midi, 1=gain, 2=pitch, 3=fx)")
				.withType(ParamType::Int).asOptional())
			.withProperty(RouteParameter(RestApiIds::name, "Module name").asOptional())
			.withProperty(RouteParameter(RestApiIds::target, "Target module name").asOptional())
			.withProperty(RouteParameter(RestApiIds::source, "Source module to clone").asOptional())
			.withProperty(RouteParameter(RestApiIds::count, "Number of clones")
				.withType(ParamType::Int).asOptional())
			.withProperty(RouteParameter(Identifier("template"), "Name template with {n} placeholder").asOptional())
			.withProperty(RouteParameter(RestApiIds::attributes, "Parameter values {paramName: value}")
				.withType(ParamType::Object).asOptional())
			.withProperty(RouteParameter(RestApiIds::mode, "Value interpretation")
				.withEnumValues({ "value", "normalized", "raw" }).withDefault("value"))
			.withProperty(RouteParameter(RestApiIds::bypassed, "Bypass state")
				.withType(ParamType::Bool).asOptional())
			.withProperty(RouteParameter(RestApiIds::effect, "Effect/network name").asOptional());

		auto diffEntry = RouteParameter(Identifier("entry"), "Diff entry")
			.withType(ParamType::Object)
			.withProperty(RouteParameter(RestApiIds::target, "Module ID affected"))
			.withProperty(RouteParameter(RestApiIds::action, "Net action symbol")
				.withEnumValues({ "+", "-", "*" }))
			.withProperty(RouteParameter(RestApiIds::domain, "Operation domain")
				.withExample("builder"));

		m.add(RouteMetadata(ApiRoute::BuilderApply, "api/builder/apply")
			.withMethod(RestServer::POST)
			.withCategory("builder")
			.withSummary("Apply a set of operations to the module tree")
			.withDescription("Apply one or more operations to the module tree in a single batch. "
				"When called inside an undo group (after push_group), the diff accumulates all operations in the group so far. "
				"The response contains a diff summary with the net effect - if a module was added and then modified, it shows as '+'. "
				"Operations are pre-validated before execution; invalid operations return 400 with detailed error info.")
			.withReturns("Diff summary showing the net effect of all operations")
			.withBodyParam(RouteParameter(RestApiIds::operations, "Non-empty array of operation objects")
				.withArrayItems(opItem))
			.withResponseField(RouteParameter(RestApiIds::scope, "Scope level")
				.withEnumValues({ "group", "root" }))
			.withResponseField(RouteParameter(RestApiIds::groupName, "Active group name"))
			.withResponseField(RouteParameter(RestApiIds::diff, "Array of diff entries")
				.withArrayItems(diffEntry))
			.withErrorCodes({ 400, 404, 409 })
			.withRequestExample(R"({"operations": [{"op": "add", "type": "SineSynth", "parent": "Master Chain", "chain": -1, "name": "MySine"}, {"op": "set_bypassed", "target": "MySine", "bypassed": true}]})")
			.withResponseExample(R"({"success": true, "result": {"scope": "group", "groupName": "root", "diff": [{"target": "MySine", "action": "+", "domain": "builder"}]}, "logs": [], "errors": []})"));
	}
};

const Array<RestHelpers::RouteMetadata>& RestHelpers::getRouteMetadata()
{
	static Array<RouteMetadata> metadata = []()
		{
			Array<RouteMetadata> m;

			// The order MUST match ApiRoute enum!

			// ApiRoute::ListMethods
			m.add(RouteMetadata(ApiRoute::ListMethods, "")
				.withCategory("status")
				.withDescription("List all available API methods with parameters and descriptions")
				.withReturns("Array of method definitions with path, parameters, and documentation"));

			// ApiRoute::Status
			m.add(RouteMetadata(ApiRoute::Status, "api/status")
				.withCategory("status")
				.withDescription("Get project status and discover available script processors")
				.withReturns("Server info (version, compileTimeout in seconds), project info, scriptsFolder path, and scriptProcessors with their callbacks"));

			// ApiRoute::GetScript
			m.add(RouteMetadata(ApiRoute::GetScript, "api/get_script")
				.withCategory("scripting")
				.withDescription("Read script content from a processor's callbacks")
				.withReturns("Callbacks object with script content for requested callback(s). Includes externalFiles array with name and full path.")
				.withModuleIdParam()
				.withQueryParam(RouteParameter(RestApiIds::callback, "Specific callback name (e.g., onInit). If omitted, returns all callbacks.").asOptional()));

			// ApiRoute::SetScript
			m.add(RouteMetadata(ApiRoute::SetScript, "api/set_script")
				.withMethod(RestServer::POST)
				.withCategory("scripting")
				.withDescription("Update one or more callbacks and optionally compile. Only specified callbacks are updated; others remain unchanged.")
				.withReturns("Compilation result with updatedCallbacks array, success status, logs, errors, and optional lafRenderWarning listing unrendered LAF components with reason (invisible or timeout)")
				.withBodyParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID"))
				.withBodyParam(RouteParameter(RestApiIds::callbacks, "Object with callback names as keys and script content as values (e.g., {onInit: \"...\", onNoteOn: \"...\"})"))
				.withBodyParam(RouteParameter(RestApiIds::compile, "Whether to compile after setting").withDefault("true"))
				.withBodyParam(RouteParameter(RestApiIds::forceSynchronousExecution, "Debug tool: Bypass threading model for synchronous execution. WARNING: May cause crashes due to race conditions - use only as last resort after saving.").withDefault("false")));

			m.add(RouteMetadata(ApiRoute::EvaluateREPL, "api/repl")
				.withMethod(RestServer::POST)
				.withCategory("scripting")
				.withDescription("Evaluates a script expression with the current script engine and returns the result")
				.withReturns("Evaluation result with success status, logs and error messages")
				.withBodyParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID"))
				.withBodyParam(RouteParameter(RestApiIds::expression, "The HiseScript expression that is evaluated. Note that any side effects of this evaluation might change the runtime state of HISE.")));

			// ApiRoute::Recompile
			m.add(RouteMetadata(ApiRoute::Recompile, "api/recompile")
				.withMethod(RestServer::POST)
				.withCategory("scripting")
				.withDescription("Recompile a processor (restores preset values, triggering callbacks for saveInPreset components)")
				.withReturns("Compilation result with success status, logs, errors, and optional lafRenderWarning listing unrendered LAF components with reason (invisible or timeout)")
				.withBodyParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID"))
				.withBodyParam(RouteParameter(RestApiIds::forceSynchronousExecution, "Debug tool: Bypass threading model for synchronous execution. WARNING: May cause crashes due to race conditions - use only as last resort after saving.").withDefault("false"))
				.withBodyParam(RouteParameter(RestApiIds::profile, "Start a profiling session alongside compilation. Retrieve results later via POST /api/profile with mode=\"get\".").withDefault("false"))
				.withBodyParam(RouteParameter(RestApiIds::durationMs, "Profiling duration in ms when profile=true (100-5000).").withDefault("2000")));

			// ApiRoute::ListComponents
			m.add(RouteMetadata(ApiRoute::ListComponents, "api/list_components")
				.withCategory("ui")
				.withDescription("List all UI components in a script processor")
				.withReturns("Array of components with id, type, and laf info (flat list or hierarchical tree with layout properties)")
				.withModuleIdParam()
				.withQueryParam(RouteParameter(RestApiIds::hierarchy, "If true, returns nested tree with layout properties").withDefault("false")));

			// ApiRoute::GetComponentProperties
			m.add(RouteMetadata(ApiRoute::GetComponentProperties, "api/get_component_properties")
				.withCategory("ui")
				.withDescription("Get all properties for a specific UI component")
				.withReturns("Component type and array of properties with id, value, isDefault, and options")
				.withModuleIdParam()
				.withQueryParam(RouteParameter(RestApiIds::id, "The component's ID (e.g., Button1, Panel1)")));

			// ApiRoute::GetComponentValue
			m.add(RouteMetadata(ApiRoute::GetComponentValue, "api/get_component_value")
				.withCategory("ui")
				.withDescription("Get the current runtime value of a UI component")
				.withReturns("Component type, current value, and min/max range")
				.withModuleIdParam()
				.withQueryParam(RouteParameter(RestApiIds::id, "The component's ID (e.g., GainKnob, BypassButton)")));

			// ApiRoute::SetComponentValue
			m.add(RouteMetadata(ApiRoute::SetComponentValue, "api/set_component_value")
				.withMethod(RestServer::POST)
				.withCategory("ui")
				.withDescription("Set the runtime value of a UI component (triggers control callback)")
				.withReturns("Success status")
				.withBodyParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID"))
				.withBodyParam(RouteParameter(RestApiIds::id, "The component's ID"))
				.withBodyParam(RouteParameter(RestApiIds::value, "The value to set"))
				.withBodyParam(RouteParameter(RestApiIds::validateRange, "If true, validates value is within component's min/max range").withDefault("false"))
				.withBodyParam(RouteParameter(RestApiIds::forceSynchronousExecution, "Debug tool: Bypass threading model for synchronous execution. WARNING: May cause crashes due to race conditions - use only as last resort after saving.").withDefault("false")));

			// ApiRoute::SetComponentProperties
			m.add(RouteMetadata(ApiRoute::SetComponentProperties, "api/set_component_properties")
				.withMethod(RestServer::POST)
				.withCategory("ui")
				.withDescription("Set properties on one or more UI components (like Interface Designer)")
				.withReturns("Success with applied changes (recompileRequired=true if parentComponent changed), or error with locked properties if any are script-controlled")
				.withBodyParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID"))
				.withBodyParam(RouteParameter(RestApiIds::changes, "Array of {id, properties: {...}} objects"))
				.withBodyParam(RouteParameter(RestApiIds::force, "If true, bypasses script-lock check and sets all properties").withDefault("false")));

			// ApiRoute::Screenshot
			m.add(RouteMetadata(ApiRoute::Screenshot, "api/screenshot")
				.withCategory("ui")
				.withDescription("Capture screenshot of interface or specific component")
				.withReturns("Base64-encoded PNG image data with dimensions, or file path if outputPath is specified")
				.withQueryParam(RouteParameter(RestApiIds::moduleId, "Script processor ID").withDefault("Interface"))
				.withQueryParam(RouteParameter(RestApiIds::id, "Component ID to capture (omit for full interface)").asOptional())
				.withQueryParam(RouteParameter(RestApiIds::scale, "Scale factor (0.5 or 1.0)").withDefault("1.0"))
				.withQueryParam(RouteParameter(RestApiIds::outputPath, "File path to save PNG (must end with .png). If provided, writes to file instead of returning Base64").asOptional()));

			// ApiRoute::GetSelectedComponents
			m.add(RouteMetadata(ApiRoute::GetSelectedComponents, "api/get_selected_components")
				.withCategory("ui")
				.withDescription("Get the currently selected UI components from the Interface Designer")
				.withReturns("Selection count and array of selected components with all properties")
				.withQueryParam(RouteParameter(RestApiIds::moduleId, "The script processor's module ID").withDefault("Interface")));

			// ApiRoute::SimulateInteractions
			m.add(RouteMetadata(ApiRoute::SimulateInteractions, "api/simulate_interactions")
				.withMethod(RestServer::POST)
				.withCategory("testing")
				.withDescription("Execute a sequence of UI interactions in a test window. Auto-inserts moveTo events as needed for proper mouse positioning.")
				.withReturns("Execution result with success status, completion count, timing, execution log, captured screenshots, and optionally mouseState when verbose=true")
				.withBodyParam(RouteParameter(RestApiIds::interactions,
					"Array of interaction objects. Types: 'moveTo' (explicit mouse positioning), 'click' (uses current position), "
					"'doubleClick' (expands to two clicks), 'drag' (uses pixel 'delta'), 'selectMenuItem' (click menu item by text), "
					"'screenshot' (capture interface). Fields: 'target' (component ID), 'delay' (ms before action), "
					"'normalizedPosition' (0-1, default center), 'pixelPosition' (absolute, takes precedence), 'delta' (for drag), "
					"'subtarget' (optional sub-component ID), 'rightClick' (boolean), 'shiftDown'/'ctrlDown'/'altDown'/'cmdDown' (modifiers), "
					"'menuItemText' (for selectMenuItem), 'id'/'scale' (for screenshot)"))
				.withBodyParam(RouteParameter(RestApiIds::verbose, "If true, include auto-insertion details and final mouseState in response").withDefault("false")));

			// ApiRoute::DiagnoseScript
			m.add(RouteMetadata(ApiRoute::DiagnoseScript, "api/diagnose_script")
				.withMethod(RestServer::POST)
				.withCategory("scripting")
				.withDescription("Run diagnostic-only shadow parse on a script file. Returns structured diagnostics "
					"without modifying runtime state. Requires at least one prior successful compile (F5).")
				.withReturns("Array of diagnostics with line, column, severity, source, message, and suggestions")
				.withBodyParam(RouteParameter(RestApiIds::moduleId,
					"The script processor's module ID. Required if filePath is not provided.").asOptional())
				.withBodyParam(RouteParameter(RestApiIds::filePath,
					"Path to the external .js file (absolute or relative to Scripts folder). "
					"Required if moduleId is not provided. When used alone, HISE resolves the owning processor.").asOptional())
				.withBodyParam(RouteParameter(RestApiIds::async,
					"If true, defer the shadow parse to the scripting thread (slower, blocks audio). "
					"Default is false: runs directly on the HTTP thread with a read lock.").withDefault("false")));

			// ApiRoute::GetIncludedFiles
			m.add(RouteMetadata(ApiRoute::GetIncludedFiles, "api/get_included_files")
				.withCategory("scripting")
				.withDescription("List all included (watched) external script files. "
					"Without moduleId, returns all files across all processors with owning processor names. "
					"With moduleId, returns files for that processor only.")
				.withReturns("Array of included files as full paths, optionally with owning processor ID")
				.withQueryParam(RouteParameter(RestApiIds::moduleId,
					"Filter by script processor. If omitted, returns files from all processors with processor names.").asOptional()));

			// ApiRoute::StartProfiling
			m.add(RouteMetadata(ApiRoute::StartProfiling, "api/profile")
				.withMethod(RestServer::POST)
				.withCategory("scripting")
				.withDescription("Start a profiling session or retrieve last result. "
					"mode=\"record\" starts a new session (non-blocking, returns immediately). "
					"mode=\"get\" returns last result with optional filtering/summary. "
					"Workflow: record once, then query with different filters.")
				.withReturns("Full tree (threads+flows), filtered results, or recording status")
				.withBodyParam(RouteParameter(RestApiIds::mode,
					"\"record\" = start new session (non-blocking), "
					"\"get\" = return last result (blocks if recording in progress)")
					.withDefault("record"))
				.withBodyParam(RouteParameter(RestApiIds::durationMs,
					"Recording duration in milliseconds (100-5000). Used in record mode.")
					.withDefault("1000"))
				.withBodyParam(RouteParameter(RestApiIds::threadFilter,
					"Array of thread names to include (default: all). In record mode, controls "
					"which threads are recorded. In get mode, filters which threads are queried. "
					"Valid: Audio Thread, Scripting Thread, UI Thread, Loading Thread, etc.")
					.asOptional())
				.withBodyParam(RouteParameter(RestApiIds::eventFilter,
					"Array of event source types to record (default: all). Used in record mode. "
					"Valid: DSP, Script, Lock, Callback, Trace, TimerCallback, Scriptnode, etc.")
					.asOptional())
				.withBodyParam(RouteParameter(RestApiIds::summary,
					"If true, aggregate repeated events with count/median/peak/min/total stats. "
					"Used in get mode.")
					.withDefault("false"))
				.withBodyParam(RouteParameter(RestApiIds::filter,
					"Wildcard pattern matched against event name (e.g. \"slow*\", \"*.processBlock*\"). "
					"Case-insensitive. Used in get mode.")
					.asOptional())
				.withBodyParam(RouteParameter(RestApiIds::minDuration,
					"Only include events with duration >= this value in ms. Used in get mode.")
					.asOptional())
				.withBodyParam(RouteParameter(RestApiIds::sourceTypeFilter,
					"Wildcard pattern matched against sourceType (e.g. \"Trace\", \"Script\"). "
					"Case-insensitive. Used in get mode.")
					.asOptional())
				.withBodyParam(RouteParameter(RestApiIds::nested,
					"When filtering, include children of matched events. Used in get mode.")
					.withDefault("false"))
				.withBodyParam(RouteParameter(RestApiIds::limit,
					"Max number of results in filtered/summary mode (1-100). Used in get mode.")
					.withDefault("15"))
				.withBodyParam(RouteParameter(RestApiIds::wait,
					"If false, return immediately when recording is in progress instead of "
					"blocking. Returns {recording: true}. Used in get mode.")
					.withDefault("true")));

			// ApiRoute::ParseCSS
			m.add(RouteMetadata(ApiRoute::ParseCSS, "api/parse_css")
				.withMethod(RestServer::POST)
				.withCategory("scripting")
				.withDescription("Parse CSS code and return structured diagnostics. "
					"Accepts either inline code or a file path to a .css file. "
					"Optionally resolves properties for a set of selectors using CSS specificity rules")
				.withReturns("Diagnostics array with line/column/severity/message, "
					"list of parsed selectors, "
					"and resolved properties when selectors are provided")
				.withBodyParam(RouteParameter(RestApiIds::code,
					"The CSS code to parse (provide this or filePath)").asOptional())
				.withBodyParam(RouteParameter(RestApiIds::filePath,
					"Path to a .css file. Relative paths resolve against the Scripts/ directory "
					"(provide this or code)").asOptional())
				.withBodyParam(RouteParameter(RestApiIds::selectors,
					"Array of selector strings representing a component's selectors "
					"(e.g. [\"button\", \".my-class\", \"#MyId\"]). "
					"Resolves properties using CSS specificity").asOptional())
				.withBodyParam(RouteParameter(RestApiIds::width,
					"Reference width in pixels for resolving percentage and relative units").asOptional())
				.withBodyParam(RouteParameter(RestApiIds::height,
					"Reference height in pixels for resolving percentage and relative units").asOptional()));

			// ApiRoute::Shutdown
			m.add(RouteMetadata(ApiRoute::Shutdown, "api/shutdown")
				.withMethod(RestServer::POST)
				.withCategory("status")
				.withDescription("Gracefully quit the HISE application")
				.withReturns("Success confirmation before shutdown begins"));

			// ApiRoute::BuilderTree
			m.add(RouteMetadata(ApiRoute::BuilderTree, "api/builder/tree")
				.withMethod(RestServer::GET)
				.withCategory("builder")
				.withDescription("Returns runtime module tree, or active validation tree when group=current")
				.withReturns("Nested JSON tree with metadata / modulation / children; 400 when no current group, 501 for unsupported group values")
				.withQueryParam(RouteParameter(RestApiIds::moduleId,
					"Optional root module ID to return a subtree").asOptional())
				.withQueryParam(RouteParameter(RestApiIds::group,
					"Optional group selector. Only 'current' is supported").asOptional())
				.withQueryParam(RouteParameter(RestApiIds::queryParameters,
					"If false, omit parameter lists from the tree").withDefault("true"))
				.withQueryParam(RouteParameter(RestApiIds::verbose,
					"If true, include verbose metadata in tree nodes").withDefault("false")));

			RestApiEndpoints::applyBuilder(m);

			// ApiRoute::BuilderReset
			m.add(RouteMetadata(ApiRoute::BuilderReset, "api/builder/reset")
				.withMethod(RestServer::POST)
				.withCategory("builder")
				.withDescription("Reset the module tree to an empty state (equivalent to File -> New). Clears all undo history.")
				.withReturns("Success confirmation"));

			// ApiRoute::UndoPushGroup
			m.add(RouteMetadata(ApiRoute::UndoPushGroup, "api/undo/push_group")
				.withMethod(RestServer::POST)
				.withCategory("undo")
				.withDescription("Start a new undo group. Actions are recorded but not executed until pop_group.")
				.withReturns("Current diff state for the new group")
				.withBodyParam(RouteParameter(RestApiIds::name, "Label for the group")));

			// ApiRoute::UndoPopGroup
			m.add(RouteMetadata(ApiRoute::UndoPopGroup, "api/undo/pop_group")
				.withMethod(RestServer::POST)
				.withCategory("undo")
				.withDescription("End the current undo group. Executes all actions as one undoable batch, or discards them.")
				.withReturns("Updated diff state after group is applied or discarded")
				.withBodyParam(RouteParameter(RestApiIds::cancel, "If true, discard the group without executing").withDefault("false")));

			// ApiRoute::UndoBack
			m.add(RouteMetadata(ApiRoute::UndoBack, "api/undo/back")
				.withMethod(RestServer::POST)
				.withCategory("undo")
				.withDescription("Undo the last action or group. Stops at group boundaries.")
				.withReturns("Updated diff state after undo; 400 when nothing to undo"));

			// ApiRoute::UndoForward
			m.add(RouteMetadata(ApiRoute::UndoForward, "api/undo/forward")
				.withMethod(RestServer::POST)
				.withCategory("undo")
				.withDescription("Redo the next action or group. Stops at group boundaries.")
				.withReturns("Updated diff state after redo; 400 when nothing to redo"));

			// ApiRoute::UndoDiff
			m.add(RouteMetadata(ApiRoute::UndoDiff, "api/undo/diff")
				.withCategory("undo")
				.withDescription("Returns the current diff state showing active changes")
				.withReturns("Diff array with scope, depth, groupName, and action entries")
				.withQueryParam(RouteParameter(RestApiIds::scope, "group = current group only, root = full stack").withDefault("group"))
				.withQueryParam(RouteParameter(RestApiIds::domain, "Filter by domain (e.g., builder, ui)").asOptional())
				.withQueryParam(RouteParameter(RestApiIds::flatten, "If true, compute net effect merging cancelling actions").withDefault("false")));

			// ApiRoute::UndoHistory
			m.add(RouteMetadata(ApiRoute::UndoHistory, "api/undo/history")
				.withCategory("undo")
				.withDescription("Returns the full undo history including redo buffer and cursor position")
				.withReturns("History array with cursor position, group nesting, and action entries")
				.withQueryParam(RouteParameter(RestApiIds::scope, "group = current group only, root = full stack with nested groups").withDefault("group"))
				.withQueryParam(RouteParameter(RestApiIds::domain, "Filter by domain (e.g., builder, ui)").asOptional())
				.withQueryParam(RouteParameter(RestApiIds::flatten, "If true, flatten group nesting to a single list").withDefault("false")));

			// ApiRoute::UndoClear
			m.add(RouteMetadata(ApiRoute::UndoClear, "api/undo/clear")
				.withMethod(RestServer::POST)
				.withCategory("undo")
				.withDescription("Clear the entire undo history and exit all groups")
				.withReturns("Empty diff state"));

			// ApiRoute::WizardInitialise
			m.add(RouteMetadata(ApiRoute::WizardInitialise, "api/wizard/initialise")
				.withMethod(RestServer::GET)
				.withCategory("wizard")
				.withDescription("Fetch pre-populated field defaults for a wizard form")
				.withReturns("Flat key/value object of field defaults, or error if wizard ID unknown")
				.withQueryParam(RouteParameter(RestApiIds::id, "Wizard ID string (new_project, recompile, plugin_export, compile_networks, audio_export, install_package_maker)")));

			// ApiRoute::WizardExecute
			m.add(RouteMetadata(ApiRoute::WizardExecute, "api/wizard/execute")
				.withMethod(RestServer::POST)
				.withCategory("wizard")
				.withDescription("Execute a wizard task. Sync tasks return immediately; async tasks return a jobId for polling")
				.withReturns("Sync: result string with logs. Async: {jobId, async: true}")
				.withBodyParam(RouteParameter(RestApiIds::wizardId, "Wizard ID string"))
				.withBodyParam(RouteParameter(RestApiIds::answers, "Key/value object of all form field answers"))
				.withBodyParam(RouteParameter(RestApiIds::tasks, "Array with exactly one task function name to execute")));

			// ApiRoute::WizardStatus
			m.add(RouteMetadata(ApiRoute::WizardStatus, "api/wizard/status")
				.withMethod(RestServer::GET)
				.withCategory("wizard")
				.withDescription("Poll progress of a long-running async wizard job")
				.withReturns("finished=true when job is done")
				.withQueryParam(RouteParameter(RestApiIds::jobId, "Job ID returned by wizard/execute")));

			// ApiRoute::UITree
			m.add(RouteMetadata(ApiRoute::UITree, "api/ui/tree")
				.withCategory("ui")
				.withDescription("Get UI component tree hierarchy for a script processor's interface")
				.withReturns("Recursive tree with id, type, visible, enabled, saveInPreset, x, y, width, height, childComponents")
				.withModuleIdParam()
				.withQueryParam(RouteParameter(RestApiIds::group,
					"Optional group selector. 'current' returns the active plan's validation tree").asOptional()));

			// ApiRoute::UIApply
			m.add(RouteMetadata(ApiRoute::UIApply, "api/ui/apply")
				.withMethod(RestServer::POST)
				.withCategory("ui")
				.withDescription("Apply batched UI component operations (add, remove, set, move, rename)")
				.withReturns("Diff of changes with domain='ui', action symbols (+/-/*), and target component IDs")
				.withBodyParam(RouteParameter(RestApiIds::moduleId, "Script processor ID"))
				.withBodyParam(RouteParameter(RestApiIds::operations,
					"Array of operations. Each requires 'op' field: "
					"add (componentType, id?, parentId?, x?, y?, width?, height?), "
					"remove (target), "
					"set (target, properties, force?), "
					"move (target, parent, index?, keepPosition?), "
					"rename (target, newId)")));

			// ApiRoute::InjectMidi
			m.add(RouteMetadata(ApiRoute::InjectMidi, "api/inject_midi")
				.withMethod(RestServer::POST)
				.withCategory("midi")
				.withDescription("Inject MIDI messages into the virtual keyboard. Non-blocking: queues messages and returns immediately. "
					"Notes auto-send note-off after duration. Multiple calls merge into the pending queue. "
					"Send allNotesOff to panic (clears queue). Send empty messages array to poll status.")
				.withReturns("Playback status: isPlaying, durationMs, activeNotes, eventsInSequence, playedEvents, progress")
				.withBodyParam(RouteParameter(RestApiIds::messages,
					"Array of MIDI messages. Each requires 'type' field: "
					"note (channel?, noteNumber, velocity?, duration?, timestamp?), "
					"cc (channel?, controller, value, timestamp?), "
					"pitchbend (channel?, value, timestamp?), "
					"allNotesOff (timestamp?), "
					"repl (expression, id?, moduleId?, timestamp?), "
					"set_attribute (parameterId, value, processorId?, timestamp?), "
					"testsignal (signal, duration?, frequency?, startFrequency?, endFrequency?, timestamp?)"))
				.withBodyParam(RouteParameter(RestApiIds::blocking,
					"If true, block until the sequence completes before returning (default: false)").asOptional())
				.withBodyParam(RouteParameter(RestApiIds::recordOutput,
					"File path to record audio output as WAV. Implies blocking mode. Duration is auto-computed from the sequence.").asOptional()));

			// Verify count matches enum
			jassert(m.size() == (int)ApiRoute::numRoutes);

			return m;
		}();

	return metadata;
}

}
