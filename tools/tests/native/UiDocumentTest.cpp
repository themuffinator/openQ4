// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Document.h"
#include "src/ui/retained/Motion.h"
#include "src/ui/retained/State.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace openq4::ui;
static void Check(bool condition, const char* message) {
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Near(double a, double b) { return std::abs(a-b) < .0001; }
static const char* Source = R"json(
// Retain this authored comment and spacing exactly.
{
 "format": "openq4-ui", "version": 1, "id": "test-document",
 "tokens": {"size": {"type":"length", "value":100, "unit":"dp"}},
 "root": {"id":"root", "type":"group", "children":[
  {"id":"panel", "type":"group", "properties":{
   "left":{"type":"length", "value":10, "unit":"dp"},
   "width":{"type":"token", "value":"size"},
   "opacity":{"type":"number", "value":0},
   "transform":{"type":"transform", "unit":"dp", "value":[0,0,1,1,0]}
  }},
  {"id":"label", "type":"text", "properties":{"text":{"type":"text", "value":"#str_test"}}}
 ]},
 "timelines":[
  {"id":"enter", "durationMs":1000, "iterations":1, "tracks":[
   {"node":"panel", "property":"left", "keys":[
    {"atMs":0, "value":{"type":"length", "value":10, "unit":"dp"}},
    {"atMs":1000, "value":{"type":"length", "value":110, "unit":"dp"}}]},
   {"node":"panel", "property":"opacity", "keys":[
    {"atMs":0, "value":{"type":"number", "value":0}},
    {"atMs":1000, "value":{"type":"number", "value":1}}]},
   {"node":"panel", "property":"transform", "keys":[
    {"atMs":0, "value":{"type":"transform", "unit":"dp", "value":[0,0,1,1,0]}},
    {"atMs":1000, "value":{"type":"transform", "unit":"dp", "value":[100,0,1,1,0]}}]}
  ]},
  {"id":"fade", "durationMs":1000, "tracks":[
   {"node":"panel", "property":"opacity", "keys":[
    {"atMs":0, "value":{"type":"number", "value":1}},
    {"atMs":1000, "value":{"type":"number", "value":0}}]}
  ]}
 ],
 "editor":{"zoom":1.25},
 "extensions": {"vendor/example~1": {"raw":"<script>never markup</script>", "unicode":"\u00e9\ud83d\ude80", "empty":[ ]}}
}
// Keep this trailer too.
)json";
static void CheckPresentationSchema() {
	const std::string source = R"json(
// Public metadata is typed data, independent of rendered properties.
{
 "format":"openq4-ui", "version":1, "id":"presentation-schema",
 "state":{"source":{"type":"number","initial":3},"allowed":{"type":"boolean","initial":true}},
 "presentationVariables":{
  "page":{"type":"number","initial":22,"value":{"state":"source"}},
  "flag":{"type":"boolean","initial":true,"value":{"state":"allowed"}},
  "metadata":{"type":"string","initial":"metadata <script>data</script>","value":"unlocalized metadata"},
  "pair":{"type":"vector2","initial":[1,2],"value":[{"state":"source"},4]},
  "triple":{"type":"vector3","initial":[-1,2,3]},
  "quad":{"type":"vector4","initial":[-1,2,3,4],"value":[1,2,3,{"op":"+","args":[{"state":"source"},1]}]}
 },
 "aliases":{
  "CuRR":{"variable":"page","extensions":{"keep":"alias metadata"}},
  "desktop::curr":{"variable":"page"},
  "dpadGUI":{"variable":"flag"},
  "metadata":{"variable":"metadata"},
  "point":{"variable":"pair"},
  "volume":{"variable":"triple"},
  "bounds":{"variable":"quad"},
  "PANEL::rect":{"node":"panel","property":"rect"},
  "panel::visible":{"node":"panel","property":"visible","shown":"flex"},
  "panel::noevents":{"node":"panel","property":"noevents"},
  "panel::opacity":{"node":"panel","property":"opacity"},
  "panel::min-width":{"node":"panel","property":"min-width"},
  "panel::color":{"node":"panel","property":"color"},
  "panel::font":{"node":"panel","property":"font-family"},
  "panel::display":{"node":"panel","property":"display"},
  "label::text":{"node":"label","property":"text"}
 },
 "root":{"id":"panel","type":"group","properties":{
  "left":{"type":"length","value":10,"unit":"dp"},
  "top":{"type":"length","value":20,"unit":"dp"},
  "width":{"type":"length","value":300,"unit":"dp"},
  "height":{"type":"length","value":120,"unit":"dp"},
  "min-width":{"type":"length","value":1,"unit":"px"},
  "right":{"type":"length","value":0,"unit":"%"},
  "display":{"type":"keyword","value":"flex"},
  "pointer-events":{"type":"keyword","value":"auto"},
  "position":{"type":"keyword","value":"absolute"},
  "opacity":{"type":"number","value":0.5},
  "color":{"type":"color","value":[0.1,0.2,0.3,1]},
  "font-family":{"type":"font","value":"marine"},
  "transform":{"type":"transform","value":[0,0,1,1,0],"unit":"dp"}
 },"children":[{"id":"label","type":"text","properties":{"text":{"type":"text","value":"#str_test"}}}]},
 "extensions":{"keep":"source-preserving sentinel"}
}
// Keep this presentation trailer.
)json";
	Document document; std::vector<Diagnostic> errors;
	Check(document.Load(source,errors),"parse explicit presentation metadata, aliases and fixed-unit rendered targets");
	Check(document.Source() == source && document.Model().presentationVariables.size() == 6 && document.Model().aliases.size() == 16,
		"presentation declarations preserve source and compile without creating rendered nodes");
	const auto& variables = document.Model().presentationVariables;
	Check(variables.at("page").initial.type == PresentationType::Number && variables.at("page").initial.data[0] == 22 &&
		variables.at("page").expressions.front().type == 0,"number metadata retains authored initial and compiled expression");
	Check(variables.at("flag").initial.type == PresentationType::Boolean && variables.at("flag").initial.data[0] == 1 &&
		variables.at("flag").expressions.front().type == 1,"boolean presentation declaration is distinct from a CSS value");
	Check(variables.at("metadata").initial.text == "metadata <script>data</script>" && variables.at("metadata").expressions.front().type == 2,
		"non-rendered strings are bounded data and do not require display localization keys");
	Check(variables.at("pair").initial.type == PresentationType::Vector2 && variables.at("pair").expressions.size() == 2 &&
		variables.at("triple").initial.type == PresentationType::Vector3 && variables.at("quad").expressions.size() == 4,
		"vector metadata keeps declared dimensions and component expression types");
	Check(document.Model().aliases.at("curr").variable == "page" && document.Model().aliases.at("desktop::curr").variable == "page" &&
		document.Model().aliases.at("dpadgui").variable == "flag","public alias names case-fold and qualified/root synonyms share exact targets");
	Check(document.Model().aliases.at("panel::rect").node == "panel" && document.Model().aliases.at("panel::visible").shown == "flex",
		"rect and visibility targets compile independently of their public spelling");
	Check(document.BuildMarkup().find("<script>") == std::string::npos && document.BuildMarkup().find("unlocalized metadata") == std::string::npos,
		"presentation metadata never becomes markup");
	Check(document.ReplaceValue("/presentationVariables/page/initial","23",errors),"edit presentation metadata through canonical source spans");
	std::string expected = source;
	expected.replace(expected.find("\"initial\":22"),12,"\"initial\":23");
	Check(document.Source() == expected && document.Model().presentationVariables.at("page").initial.data[0] == 23,
		"presentation metadata edit preserves all other bytes and comments");
	const auto beforeInvalid = document.Source();
	auto reject = [&](const std::string& pointer, const std::string& value, const char* message) {
		Check(!document.ReplaceValue(pointer,value,errors),message);
		Check(!errors.empty() && document.Source() == beforeInvalid && document.Model().aliases.at("curr").variable == "page",
			"invalid presentation edits preserve source/model and report a diagnostic");
	};
	reject("/presentationVariables","[]","presentation variable declarations require an object");
	reject("/presentationVariables/page",R"({"type":"integer","initial":1})","reject unsupported presentation types");
	reject("/presentationVariables/page",R"({"type":"number"})","presentation variables require initial values");
	reject("/presentationVariables/page",R"({"type":"number","initial":1,"cvar":"r_gamma"})","metadata cannot introduce an implicit CVar owner");
	reject("/presentationVariables/page/initial","true","numbers cannot silently consume booleans");
	reject("/presentationVariables/page/initial","1000000000001","reject out-of-range scalar metadata");
	reject("/presentationVariables/flag/initial","1","booleans require exact boolean JSON");
	reject("/presentationVariables/metadata/initial","\""+std::string(65537,'a')+"\"","metadata strings retain the 64 KiB bound");
	reject("/presentationVariables/metadata/initial",R"("bad\u0000text")","metadata strings reject embedded NUL");
	reject("/presentationVariables/pair/initial","[1]","vectors require exact initial component counts");
	reject("/presentationVariables/pair/initial","[1,true]","vector components are exactly numeric");
	reject("/presentationVariables/quad/initial","[1,2,3,-1000000000001]","all vector components are bounded");
	reject("/presentationVariables/page/value","[1]","scalar expressions cannot use vector encoding");
	reject("/presentationVariables/page/value","true","presentation expressions must match the declared scalar type");
	reject("/presentationVariables/pair/value","[1]","presentation vector expression counts are exact");
	reject("/presentationVariables/pair/value","[1,false]","presentation vector expression types are numeric");
	reject("/presentationVariables/page/value",R"({"state":"page"})","presentation variables cannot masquerade as state references");
	reject("/presentationVariables/page/value",R"({"op":"/","args":[1,0]})","invalid initial expressions fail document compilation");
	reject("/presentationVariables/page/value",R"({"op":"*","args":[1000000000000,2]})","initial expression results obey presentation bounds");
	reject("/presentationVariables/metadata/value","false","string expression types cannot silently convert booleans");
	reject("/aliases","[]","presentation aliases require an object");
	reject("/aliases",R"({"curr":{"variable":"page"},"CURR":{"variable":"page"}})","case-fold collisions are rejected even for the same target");
	reject("/aliases",R"({"GUI::source":{"variable":"page"}})","application gui namespace is forbidden case-insensitively");
	reject("/aliases",R"({"a::b::c":{"variable":"page"}})","aliases allow at most one qualification separator");
	reject("/aliases",R"({"::x":{"variable":"page"}})","aliases cannot have an empty node qualifier");
	reject("/aliases",R"({"x::":{"variable":"page"}})","aliases cannot have an empty variable qualifier");
	reject("/aliases",R"({"bad alias":{"variable":"page"}})","alias components use bounded stable ID syntax");
	reject("/aliases",R"({"a":{"variable":"Page"}})","presentation variable target IDs retain exact case");
	reject("/aliases",R"({"a":{"variable":"page","node":"panel"}})","metadata aliases cannot also project a node");
	reject("/aliases",R"({"a":{"variable":"page","shown":"block"}})","metadata aliases reject rendered-only fields");
	reject("/aliases",R"({"a":{"node":"Panel","property":"opacity"}})","rendered target IDs retain exact case");
	reject("/aliases",R"({"a":{"node":"panel","property":"absent"}})","aliases cannot create missing canonical properties");
	reject("/aliases",R"({"a":{"node":"panel","property":"transform"}})","five-component transforms are not silently truncated to vectors");
	reject("/aliases",R"({"a":{"node":"panel","property":"right"}})","single property aliases reject percentage lengths");
	reject("/aliases",R"({"a":{"node":"panel","property":"visible"}})","visible aliases require a declared shown display mode");
	reject("/aliases",R"({"a":{"node":"panel","property":"visible","shown":"none"}})","visible true cannot mean none");
	reject("/aliases",R"({"a":{"node":"panel","property":"visible","shown":"grid"}})","shown values use the supported canonical display registry");
	reject("/aliases",R"({"a":{"node":"panel","property":"noevents","shown":"block"}})","noevents aliases reject unrelated shown fields");
	reject("/aliases",R"({"a":{"node":"label","property":"visible","shown":"block"}})","visible aliases need an explicit display base");
	reject("/aliases",R"({"a":{"node":"label","property":"noevents"}})","noevents aliases need an explicit pointer-events base");
	reject("/aliases",R"({"a":{"node":"panel","property":"opacity","css":"raw"}})","unknown alias fields fail closed");
	reject("/root/properties/pointer-events/value",R"("all")","pointer events accept only auto or none");
	reject("/root/properties/left/unit",R"("px")","rect aliases cannot mix px and dp");
	reject("/root/properties/top",R"({"type":"keyword","value":"auto"})","rect aliases require fixed numeric lengths");
	Check(document.ReplaceValue("/presentationVariables/page/value",R"({"op":"select","args":[true,4,{"op":"/","args":[1,0]}]})",errors),
		"initial presentation evaluation preserves lazy unused expression branches");
	Check(document.ReplaceValue("/root/properties/pointer-events/value",R"("none")",errors),"pointer-events none is a canonical keyword");

	const std::string prefix = R"({"format":"openq4-ui","version":1,"id":"presentation-limits","root":{"id":"root","type":"group"},"presentationVariables":)";
	auto declarations = [](unsigned count,bool aliases) {
		std::string result="{";
		for (unsigned i=0;i<count;++i) {
			if(i)result+=',';
			result+='"'+std::string("v")+std::to_string(i)+"\":";
			result+=aliases ? R"({"variable":"page"})" : R"({"type":"number","initial":0})";
		}
		return result+'}';
	};
	Document bounded;
	Check(bounded.Load(prefix+declarations(4096,false)+'}',errors),"maximum presentation variable count is supported");
	const auto boundedSource = bounded.Source();
	Check(!bounded.Load(prefix+declarations(4097,false)+'}',errors) && errors.front().pointer == "/presentationVariables" && bounded.Source()==boundedSource,
		"presentation variable limit fails atomically before expensive declaration work");
	const std::string aliasPrefix = prefix+R"({"page":{"type":"number","initial":0}},"aliases":)";
	Check(bounded.Load(aliasPrefix+declarations(8192,true)+'}',errors),"maximum alias count is supported");
	Check(!bounded.Load(aliasPrefix+declarations(8193,true)+'}',errors) && errors.front().pointer == "/aliases",
		"presentation alias declarations have an explicit count budget");
}
static void CheckEventSchema() {
	const std::string source = R"json(
// Ordered authored programs, including forward and recursive named calls.
{"format":"openq4-ui","version":1,"id":"event-schema",
 "state":{"count":{"type":"number","initial":2},"gate":{"type":"boolean","initial":true},
          "host":{"type":"number","initial":1,"cvar":"host_value"}},
 "presentationVariables":{"page":{"type":"number","initial":0,"value":{"state":"count"}},
  "flag":{"type":"boolean","initial":true},"caption":{"type":"string","initial":"metadata data"},
  "pair":{"type":"vector2","initial":[1,2]},"triple":{"type":"vector3","initial":[1,2,3]}},
 "aliases":{"curr":{"variable":"page"},"desktop::curr":{"variable":"page"},
  "allowed":{"variable":"flag"},"caption":{"variable":"caption"},"pair":{"variable":"pair"},"triple":{"variable":"triple"},
  "panel::rect":{"node":"root","property":"rect"},"panel::visible":{"node":"root","property":"visible","shown":"block"},
  "panel::noevents":{"node":"root","property":"noevents"},"panel::tint":{"node":"root","property":"color"},
  "panel::pixels":{"node":"root","property":"min-width"},"label::text":{"node":"label","property":"text"}},
 "root":{"id":"root","type":"group","properties":{
  "left":{"type":"length","value":0,"unit":"dp"},"top":{"type":"length","value":0,"unit":"dp"},
  "width":{"type":"length","value":100,"unit":"dp"},"height":{"type":"length","value":50,"unit":"dp"},
  "min-width":{"type":"length","value":5,"unit":"px"},"opacity":{"type":"number","value":1},
  "display":{"type":"keyword","value":"block"},"pointer-events":{"type":"keyword","value":"auto"},
  "color":{"type":"color","value":[1,1,1,1]}},"children":[
   {"id":"label","type":"text","properties":{"text":{"type":"text","value":"#str_test"}}},
   {"id":"button","type":"group","properties":{"opacity":{"type":"number","value":1}},
    "control":{"role":"button","event":"ROOT::Later","label":"#str_test",
     "states":{"default":"pulse","hover":"pulse","focus":"pulse","pressed":"pulse","disabled":"pulse"}}}]},
 "bindings":[{"id":"opacity","node":"root","property":"opacity","value":{"state":"host"}}],
 "timelines":[{"id":"pulse","durationMs":100,"tracks":[{"node":"button","property":"opacity","keys":[
  {"atMs":0,"value":{"type":"number","value":1}},{"atMs":100,"value":{"type":"number","value":0.5}}]}]}],
 "actions":{"inspect":{"operation":"test.inspect","arguments":{
  "number":{"presentation":"CuRR"},"flag":{"presentation":"allowed"},"text":{"presentation":"caption"},
  "width":{"presentation":"panel::rect","component":2},"pair":{"presentation":"pair","component":1},
  "triple":{"presentation":"triple","component":2},"tint":{"presentation":"panel::tint","component":3}}}},
 "events":{"OnActivate":[
  {"op":"setState","values":{"count":{"op":"+","args":[{"presentation":"CuRR"},1]},"gate":{"presentation":"allowed"}}},
  {"op":"setPresentation","alias":"desktop::curr","value":{"state":"count"},"overrideExpression":true},
  {"op":"setPresentation","alias":"panel::rect","value":[1,2,{"presentation":"panel::rect","component":2},50],"overrideExpression":false},
  {"op":"setPresentation","alias":"panel::visible","value":{"presentation":"allowed"},"overrideExpression":true},
  {"op":"setPresentation","alias":"caption","value":"arbitrary metadata remains data","overrideExpression":true},
  {"op":"setPresentation","alias":"label::text","value":"#str_test","overrideExpression":false},
  {"op":"if","condition":{"op":"&&","args":[{"state":"gate"},{"presentation":"allowed"}]},
   "then":[{"op":"action","action":"inspect"},{"op":"call","event":"ROOT::Later"}],"else":[{"op":"setState","values":{}}]},
  {"op":"playTimeline","timeline":"pulse"},{"op":"pauseTimeline","timeline":"pulse"},
  {"op":"resumeTimeline","timeline":"pulse"},{"op":"cancelTimeline","timeline":"pulse","policy":"hold"},
  {"op":"cancelTimeline","timeline":"pulse","policy":"base","extensions":{"note":"preserve"}}],
  "root::Later":[{"op":"call","event":"ONACTIVATE"}]},
 "extensions":{"event-test":"source bytes remain authored"}}
// Keep this trailer.
)json";
	Document document; std::vector<Diagnostic> errors;
	if (!document.Load(source,errors)) for (const auto& error : errors) std::fprintf(stderr,"%s: %s\n",error.pointer.c_str(),error.message.c_str());
	Check(errors.empty() && document.Source()==source,"ordered event source compiles without executing its recursive programs");
	const auto& model = document.Model();
	Check(model.events.size()==2 && model.events.at("onactivate").name=="OnActivate","event keys fold case while diagnostic names stay authored");
	Check(model.FindNode("button")->control->event=="root::later" && model.FindNode("button")->control->action.empty(),"event control uses forward-declared qualified event");
	const auto& steps = model.events.at("onactivate").steps;
	Check(steps.size()==12 && steps[0].op==EventOp::SetState && steps[0].values.size()==2,"event ordering and atomic state batches are retained");
	Check(steps[2].op==EventOp::SetPresentation && steps[2].presentation.size()==4 && !steps[2].overrideExpression,"vector event write retains expressions and explicit ownership flag");
	Check(steps[6].condition.type==1 && steps[6].thenSteps[1].target=="root::later" && steps[6].elseSteps.size()==1,"conditional branches and forward calls compile");
	Check(steps[7].op==EventOp::PlayTimeline && steps[8].op==EventOp::PauseTimeline && steps[9].op==EventOp::ResumeTimeline &&
		steps[10].op==EventOp::CancelTimeline && !steps[10].restoreBase && steps[11].restoreBase,"all timeline commands and cancellation policies remain distinct");
	for (const auto& [name,type] : std::map<std::string,PresentationType>{{"CURR",PresentationType::Number},{"caption",PresentationType::String},
		{"allowed",PresentationType::Boolean},{"pair",PresentationType::Vector2},{"triple",PresentationType::Vector3},
		{"panel::rect",PresentationType::Vector4},{"panel::tint",PresentationType::Vector4},{"panel::visible",PresentationType::Boolean},
		{"panel::noevents",PresentationType::Boolean},{"panel::pixels",PresentationType::Number},{"label::text",PresentationType::String}})
		Check(PresentationAliasType(model,name)==type,"shared alias typing covers metadata and rendered forms");
	Check(!PresentationAliasType(model,"missing") && !PresentationAliasType(model,"gui::curr"),"unknown and application namespace aliases have no presentation type");
	auto broken = model; broken.root.properties.erase("height");
	Check(!PresentationAliasType(broken,"panel::rect"),"alias type query fails safely for missing rectangle parts");
	broken = model; broken.aliases.at("panel::visible").shown="none";
	Check(!PresentationAliasType(broken,"panel::visible"),"alias type query rejects an invalid shown mode");
	std::vector<std::pair<std::string,int>> reads;
	const PresentationLookup lookup = [&](const std::string& name, int component, StateValue& value, std::string&) {
		reads.emplace_back(name,component);
		if (name=="CuRR") value=9.0;
		else if (name=="allowed") value=true;
		else if (name=="caption") value=std::string("literal ; quit <span>data</span>");
		else if (name=="panel::rect" && component==2) value=100.0;
		else if (name=="pair" && component==1) value=2.0;
		else if (name=="triple" && component==2) value=3.0;
		else if (name=="panel::tint" && component==3) value=1.0;
		else return false;
		return true;
	};
	StateValue value=std::string("previous"); std::string error;
	Check(EvaluateStateExpression(steps[0].values.at("count"),{},value,error,lookup) && std::get<double>(value)==10,
		"nested expressions forward presentation lookup and preserve scalar component sentinel");
	Check(reads.back()==std::make_pair(std::string("CuRR"),-1),"scalar alias read forwards original name and absent component");
	ActionInvocation invocation;
	Check(model.ResolveAction("inspect",{},invocation,error,lookup) && invocation.arguments.size()==7 && std::get<double>(invocation.arguments.at("width"))==100,
		"action arguments resolve typed live presentation values including vector components");
	Check(std::get<std::string>(invocation.arguments.at("text"))=="literal ; quit <span>data</span>","presentation strings remain action data");
	const auto before = invocation;
	Check(!model.ResolveAction("inspect",{},invocation,error) && invocation.arguments==before.arguments && invocation.operation==before.operation,
		"missing presentation callback rejects the action without a partial invocation");
	const Expression expression = model.actions.at("inspect").arguments.at("number");
	value=std::string("previous");
	for (const auto& bad : std::vector<PresentationLookup>{
		[](const std::string&,int,StateValue& result,std::string& message) { result=4.0; message="unavailable target"; return false; },
		[](const std::string&,int,StateValue& result,std::string&) { result=true; return true; },
		[](const std::string&,int,StateValue& result,std::string&) { result=std::numeric_limits<double>::infinity(); return true; }})
		Check(!EvaluateStateExpression(expression,{},value,error,bad) && !error.empty() && std::get<std::string>(value)=="previous",
			"failed, wrong-type and invalid-value lookups preserve the previous expression result");
	Check(document.ReplaceValue("/actions/inspect/arguments/number",R"({"op":"select","args":[true,5,{"presentation":"curr"}]})",errors),"compile lazy presentation read");
	Check(EvaluateStateExpression(document.Model().actions.at("inspect").arguments.at("number"),{},value,error) && std::get<double>(value)==5,
		"unused presentation branches require no callback");
	Check(document.Load(source,errors),"restore event source for transactional edits");
	Check(document.ReplaceValue("/events/OnActivate/0/values/count/args/1","3",errors),"edit an actual event expression through canonical source offsets");
	std::string expected=source; const auto position=expected.find("\"CuRR\"},1]")+std::string("\"CuRR\"},").size(); expected.replace(position,1,"3");
	Check(document.Source()==expected,"event edit preserves all comments, extensions and unrelated source bytes");
	Check(document.Load(source,errors),"restore event source for negative cases");
	auto reject = [&](const char* pointer,const char* replacement) {
		Check(!document.ReplaceValue(pointer,replacement,errors) && !errors.empty() && document.Source()==source,"invalid event edit preserves the prior complete document");
	};
	for (const auto* bad : {R"({"presentation":"missing"})",R"({"presentation":"curr","component":0})",
		R"({"presentation":"pair"})",R"({"presentation":"pair","component":2})",R"({"presentation":"pair","component":-1})",
		R"({"presentation":"pair","component":1.0})",R"({"presentation":"pair","component":true})",R"({"presentation":"pair","component":"1"})",
		R"({"presentation":"panel::rect","component":4})",R"({"presentation":"curr","state":"count"})",
		R"({"presentation":"curr","op":"+","args":[1,2]})",R"({"presentation":1})"})
		reject("/actions/inspect/arguments/number",bad);
	reject("/bindings/0/value",R"({"presentation":"curr"})");
	reject("/bindings/0/value",R"({"op":"select","args":[true,1,{"presentation":"curr"}]})");
	reject("/presentationVariables/page/value",R"({"presentation":"curr"})");
	reject("/presentationVariables/page/value",R"({"op":"+","args":[1,{"presentation":"curr"}]})");
	for (const auto* bad : {R"([])",R"({"":[]})",R"({"GUI::event":[]})",R"({"a::b::c":[]})",R"({"OnActivate":[],"onactivate":[]})",
		R"({"OnActivate":{}})",R"({"root::Later":[{"op":"call","event":"missing"}]})"}) reject("/events",bad);
	for (const auto* bad : {R"({"op":"setState","values":{"host":2}})",R"({"op":"setState","values":{"missing":2}})",
		R"({"op":"setState","values":{"count":true}})",R"({"op":"setState","values":[]})",
		R"({"op":"setState","values":{},"action":"inspect"})",R"({"op":"setPresentation","alias":"missing","value":1,"overrideExpression":true})",
		R"({"op":"setPresentation","alias":"curr","value":1})",R"({"op":"setPresentation","alias":"curr","value":1,"overrideExpression":1})",
		R"({"op":"setPresentation","alias":"curr","value":true,"overrideExpression":true})",
		R"({"op":"setPresentation","alias":"panel::visible","value":1,"overrideExpression":true})",
		R"({"op":"setPresentation","alias":"panel::rect","value":[1,2,3],"overrideExpression":true})",
		R"({"op":"setPresentation","alias":"panel::rect","value":[1,2,true,4],"overrideExpression":true})",
		R"({"op":"setPresentation","alias":"curr","value":[1],"overrideExpression":false})",
		R"({"op":"setPresentation","alias":"label::text","value":"hardcoded label","overrideExpression":false})",
		R"({"op":"action","action":"missing"})",R"({"op":"call","event":"GUI::OnActivate"})",R"({"op":"call","event":"missing"})",
		R"({"op":"if","condition":1,"then":[]})",R"({"op":"if","condition":true})",R"({"op":"if","condition":true,"then":[],"else":{}})",
		R"({"op":"playTimeline","timeline":"missing"})",R"({"op":"pauseTimeline","timeline":"PULSE"})",
		R"({"op":"resumeTimeline","timeline":"pulse","policy":"hold"})",R"({"op":"cancelTimeline","timeline":"pulse"})",
		R"({"op":"cancelTimeline","timeline":"pulse","policy":"unknown"})",R"({"op":"exec","command":"quit"})",R"({"op":"action","action":"inspect","extensions":[]})"})
		reject("/events/OnActivate/0",bad);
	reject("/root/children/1/control/event",R"("missing")");
	reject("/root/children/1/control",R"({"role":"button","event":"OnActivate","action":"inspect","label":"#str_test","states":{}})");
	reject("/root/children/1/control",R"({"role":"button","label":"#str_test","states":{}})");
	const auto bounded = [](const std::string& events) {
		return "{\"format\":\"openq4-ui\",\"version\":1,\"id\":\"limits\",\"root\":{\"id\":\"root\",\"type\":\"group\"},\"events\":"+events+"}";
	};
	for (const unsigned count : {1024u,1025u}) {
		std::string events="{";
		for (unsigned i=0;i<count;++i) events+=(i?",":"")+std::string("\"event")+std::to_string(i)+"\":[]";
		events+="}";
		Check(document.Load(bounded(events),errors)==(count==1024),"named event budget includes its exact boundary");
	}
	for (const unsigned count : {8192u,8193u}) {
		std::string events="{\"event\":[";
		for (unsigned i=0;i<count;++i) events+=(i?",":"")+std::string("{\"op\":\"setState\",\"values\":{}}");
		events+="]}";
		Check(document.Load(bounded(events),errors)==(count==8192),"aggregate event step budget includes its exact boundary");
	}
	for (const unsigned depth : {32u,33u}) {
		std::string step="{\"op\":\"setState\",\"values\":{}}";
		for (unsigned i=0;i<depth;++i) step="{\"op\":\"if\",\"condition\":true,\"then\":["+step+"]}";
		Check(document.Load(bounded("{\"event\":["+step+"]}"),errors)==(depth==32),"event structural depth includes its exact boundary");
	}
}
int main() {
	CheckEventSchema();
	CheckPresentationSchema();
	Document document;
	std::vector<Diagnostic> errors;
	Check(document.Load(Source,errors),"parse typed commented document");
	Check(errors.empty() && document.Source() == Source,"unchanged source round trip is byte exact");
	Check(document.Model().FindNode("panel") != nullptr,"stable node lookup");
	Check(Near(document.Model().FindNode("panel")->properties.at("width").data[0],100),"resolve design token");
	Check(document.BuildMarkup().find("<script>") == std::string::npos,"extensions cannot inject markup");
	Check(document.BuildMarkup().find("#str_test") == std::string::npos,"localization goes through escaped text API");
	Check(document.ReplaceValue("/tokens/size/value","160 // supplied trailing comment",errors),"edit scalar by exact source offset");
	std::string expected = Source;
	expected.replace(expected.find("\"value\":100"),11,"\"value\":160");
	Check(document.Source() == expected,"scalar edit preserves all surrounding source bytes");
	Check(Near(document.Model().FindNode("panel")->properties.at("width").data[0],160),"token edits update resolved model");
	Check(document.ReplaceValue("/extensions/vendor~1example~01/empty","[1, /* inside replacement */ 2]",errors),"JSON pointer escaping and empty-container source spans");
	Check(document.Source().find("[1, /* inside replacement */ 2]") != std::string::npos,"replacement interior comments retained");
	Check(document.ReplaceValue("/extensions/vendor~1example~01/raw","\"changed \\\"value\\\"\"",errors),"string source span includes quotes");
	const auto beforeInvalid = document.Source();
	auto reject = [&](const std::string& pointer, const std::string& value, const char* message) {
		Check(!document.ReplaceValue(pointer,value,errors),message);
		Check(!errors.empty() && document.Source() == beforeInvalid,"failed edit preserves prior source/model and diagnoses");
	};
	reject("/version","2","reject unknown schema version");
	reject("/root/children/1/id","\"panel\"","reject duplicate stable IDs");
	reject("/root/children/0/properties/width/value","\"missing\"","reject dangling token");
	reject("/root/children/0/properties/opacity/value","2","reject invalid opacity");
	reject("/root/children/1/properties/text/value","\"Hardcoded\"","reject unlocalized display string");
	reject("/timelines/0/tracks/0/node","\"absent\"","reject dangling animation target");
	reject("/timelines/0/tracks/0/keys/1/atMs","0","reject duplicate key times");
	reject("/timelines/0/tracks/0/keys/1/value/unit","\"px\"","reject ambiguous mixed-unit interpolation");
	reject("/editor/zoom","1e999","reject infinite numbers in opaque metadata");
	reject("/editor/zoom","NaN","reject non-JSON numeric spellings");
	reject("/editor/zoom","01","reject leading-zero numbers");
	reject("/editor/zoom","1.","reject incomplete decimals");
	reject("/editor/zoom","1e+","reject incomplete exponents");
	reject("/editor","{\"zoom\":1,\"zoom\":2}","reject duplicate object keys");
	reject("/editor/zoom","1,\"escape\":2","fragment cannot escape its source span");
	reject("/root/children/01/id","\"other\"","array pointers reject leading zeros");
	reject("/extensions/vendor~2example","null","reject malformed pointer escape");
	reject("/editor","{\"bad\":\"\\ud800\\u0041\"}","reject unpaired high surrogate");
	reject("/editor","{\"bad\":\"\\udc00\"}","reject lone low surrogate");
	reject("/editor","{\"bad\":\"\\u0000\"}","reject escaped embedded NUL");
	reject("/editor","{\"bad\":\"raw\nnewline\"}","reject unescaped string control characters");
	reject("/editor","{\"bad\":\""+std::string("\xc0\x80",2)+"\"}","reject overlong UTF-8");
	Check(!document.Load("{\n  \"format\": \n}",errors),"syntax diagnostics");
	Check(errors[0].line == 3 && errors[0].byte > 0,"syntax diagnostic source position");
	Check(document.Source() == beforeInvalid,"failed whole-document load is transactional");
	Check(document.Load(Source,errors),"reload authored model");
	Check(!document.ReplaceValue("/root/type","\"image\"",errors),"unsupported features fail instead of silently disappearing");
	Check(errors[0].pointer == "/root/type" && errors[0].line > 1 && errors[0].column > 1,"semantic diagnostic pointer and source location");
	std::string windowsSource = "\xef\xbb\xbf";
	for (char c : std::string(Source)) windowsSource += c == '\n' ? "\r\n" : std::string(1,c);
	Check(document.Load(windowsSource,errors),"load UTF-8 BOM and Windows line endings");
	Check(document.ReplaceValue("/editor/zoom","2.5",errors),"BOM does not shift source edit offsets");
	windowsSource.replace(windowsSource.find("1.25"),4,"2.5");
	Check(document.Source() == windowsSource,"BOM and CRLF are byte exact after a value edit");
	Check(document.Load(Source,errors),"restore model after source encoding checks");
	const Easing ease{.16,1,.3,1};
	Check(ease.Evaluate(0) == 0 && ease.Evaluate(1) == 1 && ease.Evaluate(.5) > .9,"specified page easing and exact endpoints");
	Check(Near(Easing{0,0,0,1}.Evaluate(.125),.5),"Bezier solves x, including flat derivative");
	Value red, transparentBlue;
	red.type = transparentBlue.type = ValueType::Colour;
	red.data = {1,0,0,1}; transparentBlue.data = {0,0,1,0};
	const auto mixed = red.Interpolate(transparentBlue,.5);
	Check(Near(mixed.data[0],1) && Near(mixed.data[2],0) && Near(mixed.data[3],.5),"colour interpolation uses premultiplied alpha to avoid dark/coloured fringes");
	Motion motion;
	auto opacity = [&]() { return motion.Values().at({"panel","opacity"}).data[0]; };
	auto left = [&]() { return motion.Values().at({"panel","left"}).data[0]; };
	motion.Reset(document.Model());
	Check(motion.Play("enter",10),"play by stable timeline ID");
	motion.Advance(10.25);
	Check(Near(opacity(),.25) && Near(left(),35),"250 ms frame stall samples absolute elapsed time");
	motion.Advance(10.25+1.0/240);
	Check(Near(opacity(),.25+1.0/240),"240 Hz presentation independent of game ticks");
	motion.Advance(9); motion.Advance(std::numeric_limits<double>::quiet_NaN());
	Check(Near(opacity(),.25+1.0/240),"backwards and invalid clock samples do not reverse motion");
	motion.Advance(11);
	Check(Near(opacity(),1) && !motion.IsPlaying("enter"),"long gap reaches exact final values");
	motion.Reset(document.Model()); motion.Play("enter",0); motion.Play("fade",.25);
	Check(Near(opacity(),.25),"interruption starts at sampled current value even without intervening frame");
	motion.Advance(.5);
	Check(Near(opacity(),.1875) && Near(left(),60),"new owner reverses opacity while other properties continue");
	motion.Cancel("enter",CancelPolicy::RestoreBase,.5);
	Check(Near(left(),10) && Near(opacity(),.1875),"cancelling older owner cannot overwrite newer owner");
	motion.Cancel("fade",CancelPolicy::Hold,.75); const double held = opacity();
	motion.Advance(100);
	Check(Near(opacity(),held),"hold cancellation stops motion at sampled value");
	motion.Reset(document.Model()); motion.Play("enter",0); motion.Pause("enter",.25);
	motion.Advance(5); Check(Near(opacity(),.25),"pause freezes presentation value");
	motion.Resume("enter",5); motion.Advance(5.25);
	Check(Near(opacity(),.5),"resume excludes paused time");
	const auto scrub = motion.Scrub("enter",750);
	Check(Near(scrub.at({"panel","opacity"}).data[0],.75) && Near(opacity(),.5),"editor scrub is a pure authored-time sample");
	motion.Reset(document.Model()); motion.SetReducedMotion(true,0); motion.Play("enter",0);
	Check(Near(left(),110),"reduced motion applies spatial endpoint immediately");
	motion.Advance(.04); Check(Near(opacity(),.5),"reduced opacity uses bounded 80 ms interpolation");
	motion.Advance(.081); Check(Near(opacity(),1) && !motion.IsPlaying("enter"),"reduced transition finishes without loops");
	motion.SetReducedMotion(false,.1); motion.Reset(document.Model()); motion.Play("enter",0);
	motion.SetReducedMotion(true,.25); motion.Advance(.29);
	Check(Near(opacity(),.625) && Near(left(),110),"live reduced-motion change retargets without an opacity jump");
	motion.Advance(.34); Check(Near(opacity(),1),"live preference completes opacity within 80 ms");
	Check(document.ReplaceValue("/timelines/0/iterations","0",errors),"author repeating timeline");
	motion.SetReducedMotion(false,1); motion.Reset(document.Model()); motion.Play("enter",0); motion.Advance(7.25);
	Check(Near(opacity(),.25) && motion.IsPlaying("enter"),"repeat phase derives from elapsed time across multiple skipped cycles");
	motion.SetReducedMotion(true,7.25); motion.Advance(8);
	Check(Near(opacity(),1) && !motion.IsPlaying("enter"),"reduced motion terminates an existing decorative loop");
	Check(!motion.Play("unknown",9),"unknown animation leaves state intact");
	std::string shorthandSource = Source;
	shorthandSource.insert(shorthandSource.find("\"left\":{\"type\""),
		"\"padding\":{\"type\":\"length\",\"value\":10,\"unit\":\"dp\"},\"padding-left\":{\"type\":\"length\",\"value\":20,\"unit\":\"dp\"},");
	Check(document.Load(shorthandSource,errors),"load overlapping shorthand and side properties");
	const auto& padded = document.Model().FindNode("panel")->properties;
	Check(!padded.contains("padding") && Near(padded.at("padding-left").data[0],20) && Near(padded.at("padding-top").data[0],10),"explicit sides override shorthand independent of source/cascade order");
	Check(document.ReplaceValue("/timelines/0/tracks/0/property","\"padding\"",errors),"expand animated shorthand into effective property tracks");
	Check(document.ReplaceValue("/timelines/1/tracks/0",R"json({"node":"panel","property":"padding-left","keys":[
	 {"atMs":0,"value":{"type":"length","value":20,"unit":"dp"}},
	 {"atMs":1000,"value":{"type":"length","value":0,"unit":"dp"}}
	]})json",errors),"author a competing side transition");
	motion.SetReducedMotion(false,9); motion.Reset(document.Model()); motion.Play("enter",0); motion.Play("fade",.25); motion.Advance(.5);
	Check(Near(motion.Values().at({"panel","padding-left"}).data[0],31.875) && Near(motion.Values().at({"panel","padding-top"}).data[0],60),"new side owner interrupts only its effective part of a shorthand track");
	motion.Cancel("enter",CancelPolicy::RestoreBase,.5);
	Check(Near(motion.Values().at({"panel","padding-left"}).data[0],31.875),"shorthand cancellation cannot overwrite a newer side owner");
	std::puts("UI document: typed JSONC, lossless value edits, diagnostics, tokens, timeline ownership, stalls, pause, scrub and reduced motion passed");
}
