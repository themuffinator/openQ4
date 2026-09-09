// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/State.h"
#include <json/json.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>

using namespace openq4::ui;
static unsigned checks = 0;
static void Check(bool okay, const char* message) {
	++checks;
	if (!okay) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static Json::Value Typed(const char* type, Json::Value value, const char* unit = nullptr) {
	Json::Value result; result["type"] = type; result["value"] = std::move(value);
	if (unit) result["unit"] = unit;
	return result;
}
static Json::Value Ref(const char* state) { Json::Value result; result["state"] = state; return result; }
static Json::Value Input() { Json::Value result; result["input"] = "value"; return result; }
static Json::Value Op(const char* op, std::initializer_list<Json::Value> args) {
	Json::Value result; result["op"] = op; result["args"] = Json::Value(Json::arrayValue);
	for (const auto& arg : args) result["args"].append(arg);
	return result;
}
static Json::Value MakeNode(const std::string& id, const char* type = "group") {
	Json::Value result; result["id"] = id; result["type"] = type;
	result["properties"]["display"] = Typed("keyword","block");
	result["properties"]["opacity"] = Typed("number",1);
	result["properties"]["position"] = Typed("keyword",id=="thumb" || id=="content" ? "absolute" : "relative");
	for (const auto* name : {"left","top","width","height"}) result["properties"][name] = Typed("length",20,"dp");
	if (id=="content") result["properties"]["left"] = Typed("length",0,"dp");
	if (std::string(type) == "text") result["properties"]["text"] = Typed("text","#str_label");
	else result["children"] = Json::Value(Json::arrayValue);
	if (std::string(type) == "vector") result["paths"] = Json::Value(Json::arrayValue);
	return result;
}
static Json::Value MakeControl(const char* role, const char* action, Json::Value value, const char* timeline) {
	Json::Value result; result["role"] = role; result["action"] = action; result["label"] = "#str_label";
	if (std::string(role) != "button") result["value"] = std::move(value);
	for (const auto* name : {"default","hover","focus","pressed","disabled"}) result["states"][name] = timeline;
	return result;
}
static Json::Value MakeTimeline(const char* id, const char* node, const char* property = "opacity", Json::Value value = Typed("number",1)) {
	Json::Value result, track; result["id"] = id; result["durationMs"] = 100;
	track["node"] = node; track["property"] = property;
	for (unsigned at : {0u,100u}) { Json::Value key; key["atMs"] = at; key["value"] = value; track["keys"].append(key); }
	result["tracks"].append(track); return result;
}
static void StateDecl(Json::Value& root, const char* id, const char* type, Json::Value value) {
	root["state"][id]["type"] = type; root["state"][id]["initial"] = std::move(value);
}
static Json::Value Fixture() {
	Json::Value root; root["format"] = "openq4-ui"; root["version"] = 1; root["id"] = "value-controls";
	StateDecl(root,"flag","boolean",false); StateDecl(root,"mixed","boolean",true);
	StateDecl(root,"brightness","number",1.25); StateDecl(root,"divisor","number",1);
	StateDecl(root,"choice","number",3); StateDecl(root,"available","boolean",true);
	StateDecl(root,"host","number",1); root["state"]["host"]["cvar"] = "r_example";
	root["root"] = MakeNode("root");
	for (const auto& [id,type] : {std::pair{"set-toggle","boolean"},{"set-slider","number"},{"set-choice","number"}}) {
		root["actions"][id]["input"] = type; root["actions"][id]["operation"] = "application.edit";
		root["actions"][id]["arguments"]["value"] = Input();
	}
	root["actions"]["ordinary"]["operation"] = "application.ordinary";
	root["actions"]["ordinary"]["arguments"] = Json::Value(Json::objectValue);
	Json::Value toggle = MakeNode("toggle"); toggle["control"] = MakeControl("toggle","set-toggle",Ref("flag"),"toggle-feedback");
	toggle["control"]["mixed"] = Ref("mixed"); toggle["control"]["parts"]["checked"] = "checked";
	toggle["control"]["parts"]["mixed"] = "mixed-part";
	toggle["children"].append(MakeNode("checked","vector")); toggle["children"].append(MakeNode("mixed-part"));
	root["root"]["children"].append(toggle);
	Json::Value slider = MakeNode("slider"); slider["control"] = MakeControl("slider","set-slider",Op("/",{Ref("brightness"),Ref("divisor")}),"slider-feedback");
	slider["control"]["minimum"] = .5; slider["control"]["maximum"] = 2; slider["control"]["step"] = .1;
	slider["control"]["decimals"] = 2; slider["control"]["orientation"] = "horizontal";
	for (const auto* name : {"track","fill","thumb"}) slider["control"]["parts"][name] = name;
	slider["control"]["parts"]["value"] = "slider-value";
	Json::Value track = MakeNode("track"); track["children"].append(MakeNode("fill")); track["children"].append(MakeNode("thumb"));
	slider["children"].append(track); slider["children"].append(MakeNode("slider-value","text")); root["root"]["children"].append(slider);
	Json::Value choice = MakeNode("choice"); choice["control"] = MakeControl("choice","set-choice",Ref("choice"),"choice-feedback");
	for (const auto* name : {"popup","viewport","content"}) choice["control"]["parts"][name] = name;
	choice["control"]["parts"]["value"] = "choice-value"; choice["control"]["visibleRows"] = 2;
	Json::Value popup = MakeNode("popup"), viewport = MakeNode("viewport"), content = MakeNode("content");
	for (unsigned i = 0; i < 2; ++i) {
		const auto id = std::string("option-")+std::to_string(i);
		Json::Value option, row = MakeNode(id); option["id"] = id; option["node"] = id; option["label"] = "#str_options";
		option["labelIndex"] = i; option["value"] = i == 0 ? 0 : 4;
		if (i == 1) option["enabled"] = Op(">",{Op("/",{Ref("host"),Ref("divisor")}),0});
		for (const auto* part : {"label","selected","highlight"}) {
			option["parts"][part] = id+"-"+part;
			row["children"].append(MakeNode(id+"-"+part,std::string(part)=="label" ? "text" : "group"));
		}
		choice["control"]["options"].append(option); content["children"].append(row);
	}
	viewport["children"].append(content); popup["children"].append(viewport);
	choice["children"].append(popup); choice["children"].append(MakeNode("choice-value","text")); root["root"]["children"].append(choice);
	Json::Value button = MakeNode("button"); button["control"] = MakeControl("button","legacy-opaque-action",{},"button-feedback");
	root["root"]["children"].append(button); root["root"]["children"].append(MakeNode("ordinary-text","text"));
	for (const auto* id : {"toggle","slider","choice","button"}) root["timelines"].append(MakeTimeline((std::string(id)+"-feedback").c_str(),id));
	Json::Value binding; binding["id"] = "ordinary-reading"; binding["node"] = "ordinary-text"; binding["property"] = "text";
	binding["value"] = Op("numberText",{Ref("brightness")}); root["bindings"].append(binding);
	binding["id"] = "available-toggle"; binding["node"] = "toggle"; binding["property"] = "enabled"; binding["value"] = Ref("available"); root["bindings"].append(binding);
	root["presentationVariables"]["metadata"]["type"] = "number"; root["presentationVariables"]["metadata"]["initial"] = 1;
	root["presentationVariables"]["metadata"]["value"] = Ref("brightness");
	return root;
}
static std::string Text(const Json::Value& root) { Json::StreamWriterBuilder writer; writer["indentation"] = ""; writer["precision"] = 17; return Json::writeString(writer,root); }
static void Load(Document& document, const Json::Value& root) {
	std::vector<Diagnostic> diagnostics;
	if (!document.Load(Text(root),diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s: %s\n",d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty(),"valid value-control document compiles");
}
static void Schema() {
	const auto source = Fixture(); Document document; Load(document,source);
	Check(document.Model().FindNode("button")->control->role == ControlRole::Button,"legacy button and opaque action retain default semantics");
	Check(document.Model().actions.at("set-toggle").inputType == 1 && document.Model().actions.at("set-slider").arguments.at("value").inputValue,"typed action input is distinct from application state");
	const auto& choice = std::get<ChoiceSpec>(document.Model().FindNode("choice")->control->widget);
	Check(choice.options.size()==2 && choice.options[1].labelIndex==1 && choice.options[0].enabled.type==1,"authored option identity, translated list index and default availability compile");
	const auto saved = document.Source();
	auto reject = [&](const std::function<void(Json::Value&)>& change) {
		auto invalid = source; change(invalid); std::vector<Diagnostic> diagnostics;
		Check(!document.Load(Text(invalid),diagnostics) && !diagnostics.empty(),"invalid schema rejects with a located diagnostic");
		Check(document.Source()==saved && document.Model().FindNode("slider")->control->role==ControlRole::Slider,"failed load preserves exact source and prior compiled controls");
	};
	reject([](auto& r){r["root"]["children"][0]["control"]["role"]="checkbox-like";});
	reject([](auto& r){r["root"]["children"][0]["control"]["value"]=1;});
	reject([](auto& r){r["root"]["children"][0]["control"]["mixed"]=1;});
	reject([](auto& r){r["root"]["children"][0]["control"]["parts"].removeMember("mixed");});
	reject([](auto& r){r["root"]["children"][0]["control"]["value"]=Input();});
	reject([](auto& r){r["bindings"][0]["value"]=Input();});
	reject([](auto& r){r["presentationVariables"]["metadata"]["value"]=Input();});
	reject([](auto& r){r["actions"]["set-slider"].removeMember("input");});
	reject([](auto& r){r["actions"]["set-slider"]["input"]="float";});
	reject([](auto& r){r["actions"]["set-slider"]["input"]="boolean";});
	reject([](auto& r){r["actions"]["set-slider"]["arguments"]["value"]["input"]="state";});
	reject([](auto& r){r["root"]["children"][1]["control"]["action"]="unknown";});
	reject([](auto& r){r["root"]["children"][3]["control"]["action"]="set-slider";});
	reject([](auto& r){Json::Value step;step["op"]="action";step["action"]="set-slider";r["events"]["bad"].append(step);});
	reject([](auto& r){r["root"]["children"][1]["control"]["event"]="event";});
	for (const auto& [field,value] : {std::pair{"minimum",2.0},{"maximum",.5},{"step",0.0},{"step",-.1},{"step",.4},{"step",1e-20}})
		reject([&](auto& r){r["root"]["children"][1]["control"][field]=value;});
	reject([](auto& r){r["root"]["children"][1]["control"]["decimals"]=7;});
	reject([](auto& r){auto& c=r["root"]["children"][1]["control"];c["minimum"]=0;c["maximum"]=999999.9999;c["step"]=1;});
	reject([](auto& r){r["root"]["children"][1]["control"]["orientation"]="diagonal";});
	reject([](auto& r){r["root"]["children"][1]["control"]["parts"]["track"]="root";});
	reject([](auto& r){r["root"]["children"][1]["control"]["parts"]["fill"]="checked";});
	reject([](auto& r){r["root"]["children"][1]["control"]["parts"]["value"]="track";});
	reject([](auto& r){r["root"]["children"][1]["control"]["parts"]["thumb"]="fill";});
	reject([](auto& r){r["root"]["children"][1]["children"][0]["children"][0]["properties"].removeMember("width");});
	reject([](auto& r){r["root"]["children"][1]["children"][0]["children"][1]["properties"]["position"]=Typed("keyword","relative");});
	reject([](auto& r){r["root"]["children"][2]["children"][0]["children"][0]["children"][0]["properties"]["position"]=Typed("keyword","relative");});
	reject([](auto& r){r["root"]["children"][2]["children"][0]["children"][0]["children"][0]["properties"]["left"]=Typed("length",4,"dp");});
	reject([](auto& r){r["root"]["children"][1]["control"]["orientation"]="vertical";});
	reject([](auto& r){r["root"]["children"][2]["control"]["options"]=Json::Value(Json::arrayValue);});
	reject([](auto& r){auto& options=r["root"]["children"][2]["control"]["options"];options.resize(257);});
	reject([](auto& r){r["root"]["children"][2]["control"]["visibleRows"]=0;});
	reject([](auto& r){r["root"]["children"][2]["control"]["visibleRows"]=33;});
	for (const auto& [field,value] : {std::pair{"id",Json::Value("option-0")},{"value",Json::Value(0)},{"value",Json::Value(false)},
		{ "label",Json::Value("Raw English")},{"labelIndex",Json::Value(256)},{"enabled",Json::Value(1)}})
		reject([&](auto& r){r["root"]["children"][2]["control"]["options"][1][field]=value;});
	reject([](auto& r){r["root"]["children"][2]["control"]["options"][0]["node"]="track";});
	reject([](auto& r){r["root"]["children"][2]["control"]["options"][0]["parts"]["selected"]="option-1-selected";});
	reject([](auto& r){r["root"]["children"][2]["control"]["options"][0]["parts"]["highlight"]="option-0-selected";});
	reject([](auto& r){r["root"]["children"][2]["children"][0]["children"][0]["children"][0]["children"][0]["control"]=r["root"]["children"][3]["control"];});
	// Runtime ownership applies independently to ordinary bindings, motion and
	// both direct and semantic aggregate public aliases.
	reject([](auto& r){Json::Value b;b["id"]="collision";b["node"]="fill";b["property"]="width";b["value"]=1;r["bindings"].append(b);});
	reject([](auto& r){r["timelines"].append(MakeTimeline("collision","thumb","left",Typed("length",0,"dp")));});
	reject([](auto& r){r["timelines"].append(MakeTimeline("collision","checked","display",Typed("keyword","block")));});
	reject([](auto& r){Json::Value b;b["id"]="collision";b["node"]="popup";b["property"]="position";b["value"]="relative";r["bindings"].append(b);});
	reject([](auto& r){r["timelines"].append(MakeTimeline("collision","popup","position",Typed("keyword","absolute")));});
	reject([](auto& r){r["root"]["children"][2]["children"][0]["children"][0]["properties"]["overflow"]=Typed("keyword","visible");
		Json::Value b;b["id"]="collision";b["node"]="viewport";b["property"]="overflow";b["value"]="hidden";r["bindings"].append(b);});
	reject([](auto& r){r["root"]["children"][2]["children"][0]["children"][0]["properties"]["overflow"]=Typed("keyword","visible");
		r["timelines"].append(MakeTimeline("collision","viewport","overflow",Typed("keyword","hidden")));});
	reject([](auto& r){r["aliases"]["collision"]["node"]="slider-value";r["aliases"]["collision"]["property"]="text";});
	reject([](auto& r){r["aliases"]["collision"]["node"]="popup";r["aliases"]["collision"]["property"]="rect";});
	reject([](auto& r){r["aliases"]["collision"]["node"]="checked";r["aliases"]["collision"]["property"]="visible";r["aliases"]["collision"]["shown"]="block";});
	std::vector<Diagnostic> diagnostics;
	Check(!document.ReplaceValue("/root/children/1/control/step","0",diagnostics) && document.Source()==saved,"failed editor step edit preserves exact canonical document");
	Check(document.ReplaceValue("/root/children/1/control/step","0.025",diagnostics),"editor can update a valid decimal step without lowering widget semantics");
	auto vertical = source; vertical["root"]["children"][1]["control"]["orientation"]="vertical";
	vertical["root"]["children"][1]["children"][0]["children"][0]["properties"]["position"]=Typed("keyword","absolute");
	vertical["root"]["children"][1]["children"][0]["children"][0]["properties"]["top"]=Typed("keyword","auto");
	vertical["root"]["children"][1]["children"][0]["children"][0]["properties"]["bottom"]=Typed("length",0,"dp"); Load(document,vertical);
	Check(std::get<SliderSpec>(document.Model().FindNode("slider")->control->widget).vertical,"vertical orientation uses the same typed model");
	auto strings=source; strings["state"]["choice"]["type"]="string";strings["state"]["choice"]["initial"]="#str_custom";
	strings["actions"]["set-choice"]["input"]="string";strings["root"]["children"][2]["control"]["options"][0]["value"]="best";
	strings["root"]["children"][2]["control"]["options"][1]["value"]="arb2"; Load(document,strings);
	Check(std::get<ChoiceSpec>(document.Model().FindNode("choice")->control->widget).options[1].value==StateValue(std::string("arb2")),"machine choice values remain opaque strings independent of localized labels");
}
static void Inputs() {
	auto fixture=Fixture(); fixture["actions"]["set-slider"]["arguments"]["derived"]=Op("+",{Input(),Ref("brightness")});
	Document document; Load(document,fixture); State state; std::string error; Check(state.Reset(document.Model(),error),"reset input test state");
	ActionInvocation result{"before","before",{{"old",true}}}; const auto original=result;
	StateValue value=1.5; Check(document.Model().ResolveAction("set-slider",state.Variables(),result,error,{},&value),"typed proposal resolves action arguments");
	Check(result.arguments.at("value")==value && result.arguments.at("derived")==StateValue(2.75),"one immutable input participates in bounded expression arithmetic");
	value=2.0; Check(result.arguments.at("value")==StateValue(1.5),"resolved invocation owns captured data after the caller changes its operand");
	auto reject=[&](const char* action,const StateValue* input) {
		result=original; Check(!document.Model().ResolveAction(action,state.Variables(),result,error,{},input) && !error.empty(),"missing extra or malformed operands reject");
		Check(result.action==original.action && result.operation==original.operation && result.arguments==original.arguments,"failed resolution leaves prior invocation unchanged");
	};
	reject("set-slider",nullptr); reject("ordinary",&value); StateValue wrong=true;reject("set-slider",&wrong);
	wrong=std::numeric_limits<double>::infinity();reject("set-slider",&wrong);wrong=1e13;reject("set-slider",&wrong);
	const auto expression=document.Model().actions.at("set-slider").arguments.at("value"); StateValue output=std::string("preserve");
	Check(!EvaluateStateExpression(expression,state.Variables(),output,error) && output==StateValue(std::string("preserve")),"unavailable invocation operand is never read from global state");
	Check(EvaluateStateExpression(expression,state.Variables(),output,error,{},&value) && output==value,"expression accepts an explicit valid operand");
	fixture["actions"]["set-slider"]["arguments"]["last"]=Op("/",{Input(),0}); Load(document,fixture); reject("set-slider",&value);
	Check(document.Model().ResolveAction("ordinary",state.Variables(),result,error),"existing operand-free action resolution is unchanged");
}
static void AtomicReadbacks() {
	Document document; Load(document,Fixture()); State first,second;std::string error;
	Check(first.Reset(document.Model(),error) && second.Reset(document.Model(),error),"independent states initialize all readbacks");
	Check(first.ControlValues().size()==3 && !first.ControlValues().contains("button"),"button semantics do not invent a value cell");
	Check(first.ControlValues().at("toggle").mixed && first.ControlValues().at("choice").enabledOptions==std::vector<bool>({true,true}),"mixed state and ordered option availability evaluate together");
	Check(first.ControlValues().at("choice").value==StateValue(3.0),"unmatched authoritative choice remains unchanged at load");
	Check(first.Set({{"brightness",8.0},{"flag",true},{"mixed",false},{"choice",7.0}},error),"valid custom values are accepted without slider or option coercion");
	Check(first.ControlValues().at("slider").value==StateValue(8.0) && first.ControlValues().at("choice").value==StateValue(7.0),"out-of-range and unmatched readbacks preserve raw typed values");
	Check(second.ControlValues().at("slider").value==StateValue(1.25),"value controls are instance-local");
	const auto revision=first.Revision(); const auto variables=first.Variables(); const auto text=first.Properties().at({"ordinary-text","text"}).text;
	Check(!first.SetCombined({{"divisor",0.0},{"brightness",1.5},{"available",false},{"flag",false}},{{"host",2.0}},error),"one invalid value expression rejects combined application and host update");
	Check(first.Revision()==revision && first.Variables()==variables && first.Properties().at({"ordinary-text","text"}).text==text && first.Enabled().at("toggle"),"failed readback evaluation preserves state, revision, properties and enabled controls");
	Check(first.ControlValues().at("toggle").value==StateValue(true) && first.ControlValues().at("slider").value==StateValue(8.0) && first.Presentation().variables.at("metadata").value.data[0]==8,"failure preserves prior widget and metadata outputs");
	Check(first.Set({{"host",-1.0}},error,true) && first.ControlValues().at("choice").enabledOptions==std::vector<bool>({true,false}),"fresh host snapshot updates option availability without altering selection");
	StateValues application; for (const auto& [id,value]:first.Variables()) if (document.Model().state.at(id).cvar.empty()) application[id]=value;
	Check(second.Restore(application,{{"host",1.0}},error) && second.ControlValues().at("slider").value==StateValue(8.0) && second.ControlValues().at("choice").enabledOptions[1],"restore rebuilds raw values against the receiving host availability");
	const auto restored=second.Variables(); const auto restoredRevision=second.Revision();application["divisor"]=0.0;
	Check(!second.Restore(application,{{"host",1.0}},error) && second.Variables()==restored && second.Revision()==restoredRevision,"invalid restored readbacks leave the receiving instance unchanged");
	// Remove the slider's divide so the failure is specifically an option
	// availability expression after other derived outputs have been staged.
	auto fixture=Fixture();fixture["root"]["children"][1]["control"]["value"]=Ref("brightness");Load(document,fixture);
	Check(second.Reset(document.Model(),error),"initialize option-failure state"); const auto before=second.Variables();
	Check(!second.Set({{"divisor",0.0},{"brightness",2.0}},error) && error.find("Control 'choice'")!=std::string::npos && second.Variables()==before && second.ControlValues().at("slider").value==StateValue(1.25),"invalid option availability rolls back previously staged control values");
	fixture["root"]["children"][2]["control"]["options"][1]["enabled"]=true;
	fixture["root"]["children"][0]["control"]["mixed"]=Op(">",{Op("/",{1,Ref("divisor")}),0});Load(document,fixture);
	Check(second.Reset(document.Model(),error),"initialize mixed-expression failure state");
	Check(!second.Set({{"divisor",0.0}},error) && error.find("Control 'toggle'")!=std::string::npos && second.ControlValues().at("toggle").mixed,"invalid mixed state cannot partially publish");
}
int main() {
	Schema(); Inputs(); AtomicReadbacks();
	std::printf("UI value-control schema and atomic readbacks passed: %u checks\n",checks);
}
