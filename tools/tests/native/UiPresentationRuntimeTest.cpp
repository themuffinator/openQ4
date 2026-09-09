// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include <json/json.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>

using namespace openq4::ui;

static void Check(bool condition, const char* message) {
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Near(double a, double b) { return std::abs(a-b) < .00001; }
static std::string JsonText(const Json::Value& value) {
	Json::StreamWriterBuilder writer; writer["indentation"] = ""; writer["precision"] = 17;
	return Json::writeString(writer,value);
}
static Json::Value JsonData(const std::string& source) {
	Json::CharReaderBuilder builder; std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	Json::Value result; std::string error;
	Check(reader->parse(source.data(),source.data()+source.size(),&result,&error),"test snapshot is valid JSON");
	return result;
}
static Json::Value Typed(const char* type, const Json::Value& data, const char* unit = nullptr) {
	Json::Value value; value["type"] = type; value["value"] = data;
	if (unit) value["unit"] = unit;
	return value;
}
static Json::Value Tuple(std::initializer_list<double> values) {
	Json::Value result(Json::arrayValue); for (double value : values) result.append(value); return result;
}
static Json::Value StateRef(const char* name) { Json::Value value; value["state"] = name; return value; }
static Json::Value Divide(double numerator, const char* denominator) {
	Json::Value value; value["op"] = "/"; value["args"] = Json::Value(Json::arrayValue);
	value["args"].append(numerator); value["args"].append(StateRef(denominator)); return value;
}
static Json::Value Box(const char* id, double x, double y, double width, double height) {
	Json::Value node; node["id"] = id; node["type"] = "group";
	auto& properties = node["properties"];
	properties["position"] = Typed("keyword","absolute");
	properties["left"] = Typed("length",x,"dp"); properties["top"] = Typed("length",y,"dp");
	properties["width"] = Typed("length",width,"dp"); properties["height"] = Typed("length",height,"dp");
	return node;
}
static Json::Value JsonTrack(const char* node, const char* property, const Json::Value& from,
	const Json::Value& to, double duration) {
	Json::Value track; track["node"] = node; track["property"] = property;
	Json::Value first; first["atMs"] = 0; first["value"] = from;
	Json::Value last; last["atMs"] = duration; last["value"] = to;
	track["keys"] = Json::Value(Json::arrayValue); track["keys"].append(first); track["keys"].append(last);
	return track;
}

// The fixture is authored through the public canonical schema and compiled by
// the real document parser. Only engine services and the final drawing sink
// are doubled; State, Motion, RmlUi layout/input and persistence stay production.
static std::string Source(bool legacy = false) {
	Json::Value source; source["format"] = "openq4-ui"; source["version"] = 1; source["id"] = "presentation-runtime";
	for (const auto& [name,initial] : std::map<std::string,double>{{"anchor",10},{"divisor",2},{"otherDivisor",2},{"metaDivisor",2},{"page",22}}) {
		source["state"][name]["type"] = "number"; source["state"][name]["initial"] = initial;
	}
	source["state"]["allowed"]["type"] = "boolean"; source["state"]["allowed"]["initial"] = true;
	source["state"]["title"]["type"] = "string"; source["state"]["title"]["initial"] = "#str_test";
	auto& variables = source["presentationVariables"];
	auto variable = [&](const char* id, const char* type, const Json::Value& initial) -> Json::Value& {
		auto& value = variables[id]; value["type"] = type; value["initial"] = initial; return value;
	};
	variable("page","number",22)["value"] = StateRef("page");
	variable("flag","boolean",true)["value"] = StateRef("allowed");
	variable("metadata","string","metadata")["value"] = StateRef("title");
	variable("danger","number",50)["value"] = Divide(100,"metaDivisor");
	variable("pair","vector2",Tuple({1,2})); variable("triple","vector3",Tuple({-1,2,3}));
	variable("quad","vector4",Tuple({10,2,3,4}))["value"] = Tuple({0,2,3,4});
	variables["quad"]["value"][0] = StateRef("anchor");
	variable("raw","string","plain metadata");
	auto& aliases = source["aliases"];
	for (const auto& [alias,id] : std::map<std::string,std::string>{{"curr","page"},{"desktop::curr","page"},
		{"dpadGUI","flag"},{"metadata","metadata"},{"danger","danger"},{"pair","pair"},{"triple","triple"},{"quad","quad"},{"raw","raw"}})
		aliases[alias]["variable"] = id;
	auto alias = [&](const char* name, const char* node, const char* property) {
		aliases[name]["node"] = node; aliases[name]["property"] = property;
	};
	alias("panel::rect","panel","rect"); alias("panel::left","panel","left");
	alias("panel::width","panel","width"); alias("panel::backColor","panel","background-color");
	alias("label::text","label","text"); alias("label::foreColor","label","color");
	alias("menu::noevents","menu","noevents"); alias("menu::visible","menu","visible");
	aliases["menu::visible"]["shown"] = "flex";
	alias("moving::left","moving","left"); alias("moving::top","moving","top");
	auto& root = source["root"]; root = Box("root",0,0,960,640);
	root["children"] = Json::Value(Json::arrayValue);
	auto panel = Box("panel",10,20,100,80);
	panel["properties"]["background-color"] = Typed("color",Tuple({.125,.25,.5,1}));
	root["children"].append(panel);
	root["children"].append(Box("other",200,20,50,80));
	auto label = Box("label",320,20,600,100); label["type"] = "text";
	label["properties"]["text"] = Typed("text","#str_test");
	label["properties"]["color"] = Typed("color",Tuple({1,1,1,1}));
	label["properties"]["white-space"] = Typed("keyword","pre");
	root["children"].append(label);
	auto moving = Box("moving",0,130,20,20); root["children"].append(moving);
	source["timelines"] = Json::Value(Json::arrayValue);
	Json::Value slide; slide["id"] = "slide"; slide["durationMs"] = 1000;
	slide["tracks"] = Json::Value(Json::arrayValue);
	slide["tracks"].append(JsonTrack("moving","left",Typed("length",0,"dp"),Typed("length",100,"dp"),1000));
	slide["tracks"].append(JsonTrack("moving","top",Typed("length",130,"dp"),Typed("length",170,"dp"),1000));
	source["timelines"].append(slide);
	auto button = [&](const char* id, double x) {
		auto node = Box(id,x,0,80,40); auto& control = node["control"];
		control["role"] = "button"; control["action"] = std::string(id)+".activate"; control["label"] = "#str_test";
		const std::string timeline = std::string(id)+".feedback";
		for (const auto* state : {"default","hover","focus","pressed","disabled"}) control["states"][state] = timeline;
		node["properties"]["background-color"] = Typed("color",Tuple({.2,.3,.4,1}));
		Json::Value feedback; feedback["id"] = timeline; feedback["durationMs"] = 10;
		feedback["tracks"] = Json::Value(Json::arrayValue);
		feedback["tracks"].append(JsonTrack(id,"background-color",node["properties"]["background-color"],node["properties"]["background-color"],10));
		source["timelines"].append(feedback); return node;
	};
	auto menu = Box("menu",20,200,220,100);
	menu["properties"]["display"] = Typed("keyword","flex");
	menu["properties"]["pointer-events"] = Typed("keyword","auto");
	menu["children"] = Json::Value(Json::arrayValue);
	menu["children"].append(button("first",0)); menu["children"].append(button("second",100));
	root["children"].append(menu);
	auto outside = button("outside",400); outside["properties"]["top"] = Typed("length",200,"dp");
	root["children"].append(outside);
	source["bindings"] = Json::Value(Json::arrayValue);
	auto binding = [&](const char* id, const char* node, const char* property, const Json::Value& expression) {
		Json::Value value; value["id"] = id; value["node"] = node; value["property"] = property; value["value"] = expression;
		source["bindings"].append(value);
	};
	binding("anchor","panel","left",StateRef("anchor")); binding("width","panel","width",Divide(200,"divisor"));
	binding("other-width","other","width",Divide(100,"otherDivisor")); binding("title","label","text",StateRef("title"));
	if (legacy) { source.removeMember("presentationVariables"); source.removeMember("aliases"); }
	return JsonText(source);
}

struct TestHost final : Host {
	unsigned errors = 0, draws = 0;
	std::uint64_t frame = 1;
	std::vector<Vertex> vertices;
	std::set<std::string> materials;
	std::set<std::uint32_t> glyphs;
	double eventHost = 1;
	bool eventHostAvailable = true;
	bool ReadFile(const std::string&, std::string&) override { return false; }
	bool ReadCVar(const std::string& name, size_t type, StateValue& value) override {
		if (name != "r_eventHost" || type != 0 || !eventHostAvailable) return false;
		value = eventHost; return true;
	}
	std::string Translate(const std::string& value) override { return value == "#str_test" ? "Localised" : value; }
	void Log(bool error, const std::string& message) override {
		if (error) { ++errors; std::fprintf(stderr,"Runtime: %s\n",message.c_str()); }
	}
	std::uintptr_t LoadMaterial(const std::string& name, int& width, int& height) override {
		materials.insert(name); width = height = 256; return 1;
	}
	void Draw(const std::vector<Vertex>& values, const std::vector<int>& indices, std::uintptr_t) override {
		++draws; Check(indices.size()%3 == 0,"real runtime submits triangle indices");
		for (int index : indices) Check(index >= 0 && static_cast<size_t>(index) < values.size(),"runtime geometry indices stay in bounds");
		for (const auto& value : values) Check(std::isfinite(value.x) && std::isfinite(value.y),"runtime geometry stays finite");
		vertices.insert(vertices.end(),values.begin(),values.end());
	}
	std::uint64_t RenderFrame() const override { return frame; }
	bool BeginLayer(std::uint32_t, int, int) override { return true; }
	void CompositeLayer(std::uint32_t source, std::uint32_t destination, float, const Bounds&) override {
		Check(source != destination,"layer composition does not sample its destination");
	}
	void MaskLayer(std::uint32_t, std::uint32_t, const Bounds&) override {}
	void EndLayer(std::uint32_t) override {}
	FontMetrics GetFontMetrics(const std::string&, int size) override { return {size*.8f,size*.2f,size*1.2f,size*.5f}; }
	Glyph GetGlyph(const std::string&, int size, std::uint32_t codepoint) override {
		glyphs.insert(codepoint); return {size*.6f,0,-size*.8f,size*.6f,static_cast<float>(size),0,0,1,1,"test-font"};
	}
};

static void Load(Runtime& runtime, const std::string& source = Source()) {
	std::vector<Diagnostic> diagnostics;
	const bool loaded = runtime.LoadDocument(source,"presentation-runtime.q4ui",diagnostics);
	for (const auto& problem : diagnostics) std::fprintf(stderr,"%s: %s\n",problem.pointer.c_str(),problem.message.c_str());
	Check(loaded && diagnostics.empty(),"compile and load real canonical presentation fixture");
}
static void Frame(TestHost& host, Runtime& runtime, double time) { ++host.frame; runtime.Frame({},time); }
static std::string Read(const Runtime& runtime, const std::string& name) {
	std::string value = "untouched"; Check(runtime.GetPresentationAlias(name,value),"registered presentation alias resolves"); return value;
}
static void Write(Runtime& runtime, const std::string& name, const std::string& value, bool overrideExpression = true) {
	std::string error = "previous error";
	Check(runtime.SetPresentationAlias(name,value,overrideExpression,error) && error.empty(),"valid alias write succeeds and clears its diagnostic");
}
static void StateChanged(Runtime& runtime, const StateValues& values = {}, double time = 1) {
	std::string error; Check(runtime.SetState(values,error,time) && error.empty(),"application state commits together with derived presentation");
}
static std::string Snapshot(Runtime& runtime, double time) {
	std::string value, error; Check(runtime.SaveSnapshot(value,error,time) && error.empty(),"save complete runtime instance"); return value;
}
static void Restore(Runtime& runtime, const std::string& snapshot, double time) {
	std::string error; Check(runtime.RestoreSnapshot(snapshot,error,time) && error.empty(),"restore complete runtime instance atomically");
}

static void CheckAliasesAndOwnership(TestHost& host) {
	Runtime first(host), second(host); Load(first); Load(second);
	Check(Read(first,"CURR") == "22" && Read(first,"Desktop::CuRR") == "22","root and qualified names address one presentation cell");
	Write(first,"desktop::curr","99",false); Check(Read(first,"curr") == "99","qualified writes update root aliases immediately");
	StateChanged(first,first.GetState()); Check(Read(first,"curr") == "22","an identical state batch evaluates transient metadata writes");
	Write(first,"curr","23"); StateChanged(first,{{"page",24.0}}); Check(Read(first,"desktop::curr") == "23","explicit metadata override disables expression ownership");
	Write(first,"curr","25",false); StateChanged(first,{{"page",26.0}});
	Check(Read(first,"curr") == "25" && Read(second,"curr") == "22","later transient writes do not revive expressions or affect other instances");
	Check(Read(first,"dpadGUI") == "1","boolean metadata is numeric for atoi consumers");
	Write(first,"dpadGUI","false",false); Check(Read(first,"dpadGUI") == "0","transient boolean uses numeric readback");
	StateChanged(first); Check(Read(first,"dpadGUI") == "1","unchanged empty batch restores boolean expression");

	Write(first,"panel::rect","-24,-88,640,958",false);
	Check(Read(first,"panel::rect") == "-24 -88 640 958","comma tuples write bound and unbound rectangle components together");
	const auto revision = first.StateRevision(); const auto before = Snapshot(first,1);
	for (const auto& rect : {"9 8 -1 4","9 8 5 -1","1 2 3","1,2 3,4","1 2 3 nan"}) {
		std::string error;
		Check(!first.SetPresentationAlias("panel::rect",rect,true,error) && !error.empty(),"invalid bound or unbound rectangle component rejects the entire write");
		Check(first.StateRevision() == revision && Snapshot(first,1) == before,"failed write preserves state, motion, values and expression ownership");
	}
	for (const auto& name : {"missing","panel::missing","gui::anchor","panel::rect::x"}) {
		std::string value = "unchanged", error;
		Check(!first.GetPresentationAlias(name,value) && value == "unchanged","unknown and malformed reads preserve their output");
		Check(!first.SetPresentationAlias(name,"1",true,error) && !error.empty(),"unknown and malformed writes do not allocate targets");
	}
	StateChanged(first,first.GetState());
	Check(Read(first,"panel::rect") == "10 -88 100 958","unchanged state restores only expression-owned rectangle components");
	Write(first,"panel::rect","5 6 7 8"); StateChanged(first,{{"anchor",20.0},{"divisor",4.0}});
	Check(Read(first,"panel::rect") == "5 6 7 8","explicit rectangle write disables both bound owners and retains unbound values");
	Write(first,"panel::left","33",false); StateChanged(first,{{"anchor",25.0}});
	Check(Read(first,"panel::rect") == "33 6 7 8","transient component write preserves existing override provenance");
	Check(Read(second,"panel::rect") == "10 20 100 80","separate runtime retains independent default geometry");
	Check(first.TakeActions().empty(),"presentation get/set and state operations dispatch no application actions");
}

static void CheckDisabledExpressions(TestHost& host) {
	Runtime runtime(host); Load(runtime); std::string error;
	const auto original = runtime.GetState(); const auto revision = runtime.StateRevision();
	Check(!runtime.SetState({{"divisor",0.0}},error,1) && runtime.GetState() == original && runtime.StateRevision() == revision,
		"active invalid binding rejects the complete application batch");
	Write(runtime,"panel::width","73"); StateChanged(runtime,{{"divisor",0.0}});
	Check(Read(runtime,"panel::width") == "73","overridden expression is not evaluated and cannot raise division errors");
	Write(runtime,"panel::width","74",false); StateChanged(runtime,{{"anchor",31.0}});
	Check(Read(runtime,"panel::width") == "74","transient write after override cannot reintroduce a disabled binding error");
	const auto safe = Snapshot(runtime,1);
	Check(!runtime.SetState({{"anchor",32.0},{"otherDivisor",0.0}},error,1) && !error.empty() && Snapshot(runtime,1) == safe,
		"unrelated invalid binding still rolls back every member of a batch");
	Check(!runtime.SetState({{"metaDivisor",0.0}},error,1),"active metadata expression diagnoses invalid arithmetic");
	Write(runtime,"danger","12"); StateChanged(runtime,{{"metaDivisor",0.0}});
	Write(runtime,"danger","13",false); StateChanged(runtime,{{"page",27.0}});
	Check(Read(runtime,"danger") == "13","metadata expression overrides suppress evaluation errors with the same ownership policy");
	Frame(host,runtime,2);
	Check(Read(runtime,"panel::width") == "74" && Read(runtime,"danger") == "13","host refresh does not revive disabled invalid expressions");
}

static void CheckTextAndColour(TestHost& host) {
	Runtime runtime(host); Load(runtime);
	const std::string text = "Player <img id='injected' src='trap'> & \"quote\"";
	Write(runtime,"label::text",text); Write(runtime,"panel::backColor","0.25,0.5,0.75,1");
	Write(runtime,"label::foreColor","1 0.5 0.25 1");
	Check(Read(runtime,"label::text") == text && Read(runtime,"panel::BACKCOLOR") == "0.25 0.5 0.75 1","text stays literal and color tuples remain straight numeric RGBA");
	host.vertices.clear(); Frame(host,runtime,1);
	Bounds bounds;
	Check(!runtime.GetBounds("injected",bounds) && !host.materials.contains("trap"),"presentation text cannot create markup nodes or load an injected material");
	Check(host.glyphs.contains('<') && host.glyphs.contains('>'),"literal angle brackets reach the real escaped text renderer");
	const bool tintDrawn = std::any_of(host.vertices.begin(),host.vertices.end(),[](const Vertex& v) {
		return std::abs(v.r-.25f) < .005f && std::abs(v.g-.5f) < .005f && std::abs(v.b-.75f) < .005f && Near(v.a,1);
	});
	Check(tintDrawn,"real retained geometry carries the externally written background color");
	const auto before = Snapshot(runtime,1); std::string error;
	Check(!runtime.SetPresentationAlias("label::foreColor","1 0 0 1.1",true,error) && Snapshot(runtime,1) == before,"out-of-range paint cannot partially change presentation");
}

static void CheckSnapshots(TestHost& host) {
	Runtime first(host), second(host); Load(first); Load(second);
	StateChanged(first,{{"anchor",40.0},{"divisor",4.0},{"page",23.0},{"allowed",false},{"title",std::string("#str_second")}});
	Write(first,"danger","12"); StateChanged(first,{{"metaDivisor",0.0}});
	Write(first,"panel::left","55"); Write(first,"panel::rect","55 66 77 88",false);
	Write(first,"curr","99",false); Write(first,"dpadGUI","1"); Write(first,"metadata","pending metadata",false);
	Write(first,"pair","11 12",false); Write(first,"triple","13 14 15"); Write(first,"quad","9 8 7 6",false);
	const std::string raw = "metadata <xml>\n\"quoted\""; Write(first,"raw",raw,false);
	Write(first,"label::text","pending <literal>",false); Write(first,"panel::backColor","0.5 0.25 0.125 1");
	const auto saved = Snapshot(first,1); const auto data = JsonData(saved);
	Check(data["version"].asUInt() == 2 && data["presentationState"]["variables"]["page"]["pending"].asBool(),"v2 stores transient presentation provenance before state evaluation");
	Check(data["presentationState"]["variables"]["flag"]["expressionDisabled"].asBool(),"v2 stores disabled metadata expression ownership");
	Write(second,"curr","123"); Write(second,"panel::width","321");
	Restore(second,saved,100);
	for (const char* alias : {"curr","dpadGUI","metadata","danger","pair","triple","quad","raw","panel::rect","panel::backColor","label::text"})
		Check(Read(first,alias) == Read(second,alias),"v2 restores scalar, boolean, vector, string, property and pending values exactly");
	Check(second.GetState() == first.GetState(),"presentation restore retains the associated committed application snapshot");
	StateChanged(second,second.GetState(),100);
	Check(Read(second,"curr") == "23" && Read(second,"quad") == "40 2 3 4" && Read(second,"metadata") == "#str_second",
		"restored pending metadata expressions resume on an identical application batch");
	Check(Read(second,"label::text") == "#str_second" && Read(second,"panel::rect") == "55 66 50 88",
		"restored transient bound properties resume while explicit and unanimated components remain");
	Check(Read(second,"dpadGUI") == "1" && Read(second,"danger") == "12" && Read(second,"raw") == raw && Read(second,"pair") == "11 12" && Read(second,"triple") == "13 14 15",
		"restored explicit metadata and unbound vector/string writes survive subsequent evaluation");
	Check(Read(first,"curr") == "99" && Read(first,"panel::rect") == "55 66 77 88","restoring and evaluating another instance never consumes the source instance pending writes");
	Frame(host,second,101); Bounds rect;
	Check(second.GetBounds("panel",rect) && Near(rect.x,55) && Near(rect.y,66) && Near(rect.width,50) && Near(rect.height,88),"restored unanimated values reach real layout");

	// Corrupt complete tables, individual cells and late semantic fields. Every
	// rejection must preserve the live state, motion, overrides and input state.
	const auto baseline = Snapshot(second,101); const auto live = second.GetState(); const auto revision = second.StateRevision();
	auto reject = [&](const std::function<void(Json::Value&)>& mutate) {
		auto changed = data; mutate(changed); std::string error;
		Check(!second.RestoreSnapshot(JsonText(changed),error,500) && !error.empty(),"corrupt v2 snapshot is rejected with a diagnostic");
		Check(second.GetState() == live && second.StateRevision() == revision && Snapshot(second,101) == baseline,"failed restore rolls back all values, ownership, clocks and semantic state");
	};
	reject([](auto& d) { d["presentationState"]["variables"].removeMember("page"); });
	reject([](auto& d) { const auto cell = d["presentationState"]["variables"]["page"]; d["presentationState"]["variables"].removeMember("page"); d["presentationState"]["variables"]["missing"] = cell; });
	reject([](auto& d) { d["presentationState"]["variables"]["page"]["value"]["type"] = 999; });
	reject([](auto& d) { d["presentationState"]["variables"]["page"]["value"]["data"][3] = 1; });
	reject([](auto& d) { d["presentationState"]["variables"]["page"]["value"]["text"] = "hidden"; });
	reject([](auto& d) { d["presentationState"]["variables"]["flag"]["value"]["data"][0] = 2; });
	reject([](auto& d) { d["presentationState"]["variables"]["page"]["expressionDisabled"] = "true"; });
	reject([](auto& d) { d["presentationState"]["variables"]["page"]["expressionDisabled"] = true; });
	reject([](auto& d) { d["presentationState"]["variables"]["pair"]["pending"] = true; });
	reject([](auto& d) { d["presentationState"]["variables"]["quad"]["value"]["data"][2] = 1e13; });
	reject([](auto& d) { d["presentationState"]["variables"]["raw"]["value"]["text"] = std::string("bad\0value",9); });
	reject([](auto& d) { d["presentationState"]["properties"].append(d["presentationState"]["properties"][0]); });
	reject([](auto& d) { d["presentationState"]["properties"][0]["node"] = "missing"; });
	reject([](auto& d) { d["presentationState"]["properties"][0]["value"]["unit"] = "percent"; });
	reject([](auto& d) { d["presentationState"]["properties"] = Json::Value(Json::objectValue); });
	reject([](auto& d) { d["presentation"]["values"][0]["node"] = "missing"; });
	reject([](auto& d) { d["interaction"]["focus"] = "missing"; });
}

static void CheckVersionOne(TestHost& host) {
	Runtime first(host), second(host); const auto source = Source(true); Load(first,source); Load(second,source);
	StateChanged(first,{{"anchor",25.0}}); Check(first.PlayTimeline("slide",1),"start timeline in a document authored before aliases");
	Frame(host,first,1.25);
	auto previousFormat = JsonData(Snapshot(first,1.25)); previousFormat["version"] = 1; previousFormat.removeMember("presentationState");
	Restore(second,JsonText(previousFormat),100);
	Check(std::get<double>(second.GetState().at("anchor")) == 25 && Near(second.PresentedValue("moving","left")->data[0],25),"old-source v1 snapshots restore application values and timeline progress");
	Frame(host,second,100.25);
	Check(Near(second.PresentedValue("moving","left")->data[0],50),"v1 restored playback advances from the reanchored clock");
	const auto baseline = Snapshot(second,100.25); std::string error;
	auto item = previousFormat["presentation"]["values"][0]; item["node"] = "panel"; item["property"] = "height";
	item["value"]["data"][0] = 99; previousFormat["presentation"]["values"].append(item);
	Check(!second.RestoreSnapshot(JsonText(previousFormat),error,500) && Snapshot(second,100.25) == baseline,
		"v1 retains its original restriction against unanimated property overrides");
}

static void CheckTimelineWrites(TestHost& host) {
	Runtime runtime(host); Load(runtime); Check(runtime.PlayTimeline("slide",1),"start two-track timeline");
	Frame(host,runtime,1.25);
	Check(Read(runtime,"moving::left") == "25" && Read(runtime,"moving::top") == "140","timeline reaches quarter progress");
	Write(runtime,"moving::left","777"); Check(Read(runtime,"moving::left") == "777","external unbound write is immediately observable");
	Frame(host,runtime,1.5);
	Check(Read(runtime,"moving::left") == "50" && Read(runtime,"moving::top") == "150","next active timeline sample replaces external write without cancelling its other tracks");
	Frame(host,runtime,2); Write(runtime,"moving::left","888",false); Frame(host,runtime,3);
	Check(Read(runtime,"moving::left") == "888" && Read(runtime,"moving::top") == "170","completed timelines do not overwrite later unbound writes");
}

static void CheckInputSuppression(TestHost& host) {
	Runtime runtime(host); Load(runtime); Frame(host,runtime,1);
	Check(runtime.FocusControl("first",1),"visible descendant initially accepts focus");
	runtime.MenuAction(MenuInput::Accept,true,1);
	Write(runtime,"menu::noevents","1");
	Check(!runtime.FocusControl("second",1) && runtime.FocusedControl().empty(),"parent noevents blocks direct child focus immediately before another frame");
	runtime.MenuAction(MenuInput::Accept,false,1);
	Check(runtime.TakeActions().empty(),"blocking an armed subtree cancels its pending activation before redraw");
	runtime.MenuAction(MenuInput::Next,true,1); runtime.MenuAction(MenuInput::Next,false,1);
	Check(runtime.FocusedControl() == "outside","keyboard navigation skips every noevents descendant");
	runtime.MenuAction(MenuInput::Accept,true,1); runtime.MenuAction(MenuInput::Accept,false,1);
	const auto outside = runtime.TakeActions();
	Check(outside.size() == 1 && outside.front().node == "outside","unrelated control remains independently actionable");
	runtime.PointerMove(40,220,1); runtime.PointerButton(true,1); runtime.PointerButton(false,1);
	Check(runtime.TakeActions().empty(),"blocked descendants cannot activate through pointer input with cached geometry");
	Write(runtime,"menu::noevents","0",false); Frame(host,runtime,2);
	Check(runtime.FocusControl("first",2),"clearing noevents restores descendant eligibility");
	runtime.PointerMove(40,220,2); runtime.PointerButton(true,2);
	Write(runtime,"menu::noevents","1"); runtime.PointerButton(false,2);
	Check(runtime.TakeActions().empty(),"parent noevents also cancels an armed pointer release without another frame");
	Write(runtime,"menu::noevents","0"); Write(runtime,"menu::visible","0");
	Check(Read(runtime,"menu::visible") == "0" && !runtime.FocusControl("first",2),"visibility aliases return numeric booleans and suppress descendant focus immediately");
	Frame(host,runtime,3); Write(runtime,"menu::visible","1"); Frame(host,runtime,4);
	Check(Read(runtime,"menu::visible") == "1" && runtime.PresentedValue("menu","display")->text == "flex" && runtime.FocusControl("second",4),
		"showing a group restores its authored flex layout and descendant input");
}

static void CheckEventRuntime(TestHost& host) {
	Json::Value source = JsonData(Source());
	source["state"]["pair"]["type"] = "number"; source["state"]["pair"]["initial"] = 1;
	source["state"]["host"]["type"] = "number"; source["state"]["host"]["initial"] = 1;
	source["state"]["host"]["cvar"] = "r_eventHost";
	auto op = [](const char* name, std::initializer_list<Json::Value> args) {
		Json::Value result; result["op"] = name; result["args"] = Json::Value(Json::arrayValue);
		for (const auto& value : args) result["args"].append(value); return result;
	};
	// Setting pending pair=2 before reading fresh host=2 would divide by zero.
	for (auto& binding : source["bindings"]) if (binding["id"] == "width")
		binding["value"] = op("/",{100,op("+",{op("-",{StateRef("host"),StateRef("pair")}),1})});
	Json::Value page; page["presentation"] = "desktop::curr";
	source["actions"]["capture"]["operation"] = "test.capture";
	source["actions"]["capture"]["arguments"]["value"] = page;
	Json::Value write; write["op"] = "setState"; write["values"]["page"] = op("+",{page,1});
	Json::Value block; block["op"] = "setPresentation"; block["alias"] = "menu::noevents";
	block["value"] = true; block["overrideExpression"] = true;
	Json::Value action; action["op"] = "action"; action["action"] = "capture";
	source["events"]["onTrigger"] = Json::Value(Json::arrayValue);
	for (const auto& step : {write,block,action}) source["events"]["onTrigger"].append(step);
	// Event controls report their program distinctly from direct actions.
	auto& control = source["root"]["children"][4]["children"][0]["control"];
	control.removeMember("action"); control["event"] = "ONTRIGGER";
	Runtime runtime(host), peer(host);
	Check(runtime.Initialize() && peer.Initialize(),"event contexts initialize");
	Load(runtime,JsonText(source)); Load(peer,JsonText(source)); Frame(host,runtime,1);
	Check(runtime.HasEvent("ONTRIGGER") && !runtime.HasEvent("absent"),"runtime event lookup is explicit and case folded");
	Check(runtime.FocusControl("first",1),"event control focuses");
	runtime.MenuAction(MenuInput::Accept,true,1); runtime.MenuAction(MenuInput::Accept,false,1);
	const auto requests = runtime.TakeActions();
	Check(requests.size() == 1 && requests[0].event == "ontrigger" && requests[0].action.empty(),"button activation carries its compiled event target");
	Check(runtime.CanActivateControl("first",1) && !runtime.CanActivateControl("missing",1),"queued activation eligibility validates a current control");
	host.eventHost = 2;
	Runtime::EventEffects effects; effects.stateChanges["sentinel"] = true;
	std::string error;
	const StateValues pending{{"pair",2.0},{"page",44.0}};
	Check(runtime.RunEvent("ONTRIGGER",2,effects,error,pending),"pending application and fresh host enter program atomically");
	Check(Read(runtime,"curr") == "45" && std::get<double>(runtime.GetState().at("host")) == 2 &&
		Near(runtime.PresentedValue("panel","width")->data[0],100),"program sees pending dictionary and current host values");
	Check(effects.stateChanges.size() == 1 && std::get<double>(effects.stateChanges.at("page")) == 45 &&
		effects.actions.size() == 1 && std::get<double>(effects.actions[0].arguments.at("value")) == 45,
		"effects publish only program-written keys and resolved presentation action arguments");
	Check(runtime.FocusedControl().empty() && !runtime.FocusControl("first",2),"event visibility/input writes take effect before another frame");
	Check(!runtime.CanActivateControl("first",2) && runtime.CanActivateControl("outside",2),"earlier program invalidates queued subtree activations while unrelated controls remain eligible");
	Check(Read(peer,"curr") == "22","event does not mutate independent instance");
	const auto saved = Snapshot(runtime,2);
	const auto revision = runtime.StateRevision();
	Runtime::EventEffects rejected; rejected.stateChanges["sentinel"] = true;
	unsigned validations = 0;
	Check(!runtime.RunEvent("onTrigger",3,rejected,error,{{"page",70.0}},[&](const ActionInvocation&,std::string& why) {
		++validations; why = "rejected host operation"; return false;
	}),"host validation failure rejects entire runtime event");
	Check(validations == 1 && runtime.StateRevision() == revision && Snapshot(runtime,2) == saved &&
		rejected.stateChanges.size() == 1 && rejected.stateChanges.contains("sentinel"),"rejected event preserves input state, clock and prior output");
	host.eventHostAvailable = false;
	Check(!runtime.RunEvent("onTrigger",4,rejected,error,pending) && runtime.StateRevision() == revision,"unavailable host source rejects before commit");
	host.eventHostAvailable = true;
	Check(!runtime.RunEvent("absent",4,rejected,error) && runtime.StateRevision() == revision,"unknown direct runtime program cannot mutate state");
	Check(!runtime.RunEvent("onTrigger",4,rejected,error,{{"page",std::string("bad")}}),"invalid pending type rejects event entry");
	ActionInvocation invocation;
	Check(runtime.ResolveAction("capture",invocation,error) && std::get<double>(invocation.arguments.at("value")) == 45,
		"direct control action resolution also supports typed presentation lookup");
	Restore(runtime,saved,5);
	Check(Read(runtime,"curr") == "45" && runtime.TakeActions().empty(),"snapshot restore retains event effects without producing new activations");
	host.eventHost = 1;
}

int main() {
	TestHost host;
	CheckAliasesAndOwnership(host); CheckDisabledExpressions(host); CheckTextAndColour(host); CheckSnapshots(host);
	CheckVersionOne(host); CheckTimelineWrites(host); CheckInputSuppression(host);
	CheckEventRuntime(host);
	Check(host.draws > 0 && host.errors == 0,"real runtime integration completes with geometry and no host diagnostics");
	std::puts("PASS: runtime presentation aliases, binding ownership, snapshots, atomic rollback and subtree input suppression");
	return 0;
}
