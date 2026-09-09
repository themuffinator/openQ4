// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Behavior.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace openq4::ui;
namespace {
void Check(bool condition, const char* message) {
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
bool Near(double a, double b) { return std::abs(a-b) < .0001; }

const char* Source = R"json(
{
 "format":"openq4-ui", "version":1, "id":"ordered-behavior",
 "state":{
  "a":{"type":"number","initial":1}, "b":{"type":"number","initial":10},
  "level":{"type":"number","initial":0.25}, "extent":{"type":"number","initial":100},
  "flag":{"type":"boolean","initial":true},
  "host":{"type":"number","initial":0.75,"cvar":"r_gamma"}
 },
 "presentationVariables":{
  "page":{"type":"number","initial":0,"value":{"state":"a"}},
  "point":{"type":"vector2","initial":[3,4]},
  "metadata":{"type":"string","initial":"opaque ; data"},
  "toggle":{"type":"boolean","initial":true,"value":{"state":"flag"}}
 },
 "root":{"id":"panel","type":"group","properties":{
  "left":{"type":"length","value":10,"unit":"dp"},
  "top":{"type":"length","value":20,"unit":"dp"},
  "width":{"type":"length","value":100,"unit":"dp"},
  "height":{"type":"length","value":50,"unit":"dp"},
  "opacity":{"type":"number","value":0.25},
  "color":{"type":"color","value":[0.1,0.2,0.3,1]},
  "display":{"type":"keyword","value":"block"},
  "pointer-events":{"type":"keyword","value":"auto"}
 },"children":[{"id":"label","type":"text","properties":{
  "text":{"type":"text","value":"#str_test"}
 }}]},
 "bindings":[
  {"id":"opacity","node":"panel","property":"opacity","value":{"state":"level"}},
  {"id":"width","node":"panel","property":"width","value":{"state":"extent"}}
 ],
 "aliases":{
  "CuRR":{"variable":"page"}, "desktop::curr":{"variable":"page"},
  "point":{"variable":"point"}, "metadata":{"variable":"metadata"},
  "toggle":{"variable":"toggle"},
  "panel::rect":{"node":"panel","property":"rect"},
  "panel::left":{"node":"panel","property":"left"},
  "panel::opacity":{"node":"panel","property":"opacity"},
  "panel::color":{"node":"panel","property":"color"},
  "panel::visible":{"node":"panel","property":"visible","shown":"block"},
  "panel::noevents":{"node":"panel","property":"noevents"},
  "label::text":{"node":"label","property":"text"}
 },
 "timelines":[{"id":"slide","durationMs":1000,"tracks":[
  {"node":"panel","property":"left","keys":[
   {"atMs":0,"value":{"type":"length","value":10,"unit":"dp"}},
   {"atMs":1000,"value":{"type":"length","value":110,"unit":"dp"}}
  ]}
 ]}],
 "actions":{
  "record":{"operation":"test.record","arguments":{
   "count":{"state":"a"}, "prior":{"state":"b"},
   "page":{"presentation":"curr"}, "pointX":{"presentation":"point","component":0},
   "width":{"presentation":"panel::rect","component":2},
   "left":{"presentation":"panel::left"}, "shown":{"presentation":"panel::visible"},
   "title":{"presentation":"label::text"}, "metadata":{"presentation":"metadata"},
   "toggle":{"presentation":"toggle"}, "host":{"state":"host"}
  }}
 },
 "events":{
  "Ordered":[
   {"op":"setState","values":{"a":{"op":"+","args":[{"state":"a"},1]},"b":{"state":"a"}}},
   {"op":"action","action":"record"},
   {"op":"setPresentation","alias":"CuRR","value":7,"overrideExpression":false},
   {"op":"action","action":"record"},
   {"op":"call","event":"Nested"},
   {"op":"action","action":"record"},
   {"op":"if","condition":{"state":"flag"},
    "then":[{"op":"setState","values":{"b":55}}],
    "else":[{"op":"setState","values":{"a":{"op":"/","args":[1,0]}}}]},
   {"op":"action","action":"record"},
   {"op":"setState","values":{"a":4}},
   {"op":"action","action":"record"}
  ],
  "Nested":[
   {"op":"setState","values":{"a":3}},
   {"op":"setPresentation","alias":"desktop::curr","value":9,"overrideExpression":true},
   {"op":"setPresentation","alias":"point","value":[
    {"presentation":"point","component":1},{"presentation":"point","component":0}
   ],"overrideExpression":true},
   {"op":"action","action":"record"}
  ],
  "recurse":[{"op":"if","condition":{"op":">","args":[{"state":"a"},0]},"then":[
   {"op":"action","action":"record"},
   {"op":"setState","values":{"a":{"op":"-","args":[{"state":"a"},1]}}},
   {"op":"call","event":"recurse"}
  ],"else":[]}],
  "empty":[], "record":[{"op":"action","action":"record"}],
  "captureAliases":[{"op":"setState","values":{
   "a":{"presentation":"curr"}, "b":{"presentation":"panel::rect","component":2}
  }}],
  "whenVisible":[{"op":"if","condition":{"presentation":"panel::visible"},
   "then":[{"op":"action","action":"record"}],"else":[]}],
  "play":[{"op":"playTimeline","timeline":"slide"}],
  "pause":[{"op":"pauseTimeline","timeline":"slide"}],
  "resume":[{"op":"resumeTimeline","timeline":"slide"}],
  "hold":[{"op":"cancelTimeline","timeline":"slide","policy":"hold"}],
  "base":[{"op":"cancelTimeline","timeline":"slide","policy":"base"}],
  "moving":[
   {"op":"setPresentation","alias":"panel::left","value":77,"overrideExpression":false},
   {"op":"action","action":"record"}
  ],
  "static":[
   {"op":"setPresentation","alias":"panel::color","value":[0.4,0.5,0.6,1],"overrideExpression":true},
   {"op":"setPresentation","alias":"panel::visible","value":false,"overrideExpression":true},
   {"op":"setPresentation","alias":"panel::noevents","value":true,"overrideExpression":true},
   {"op":"setPresentation","alias":"label::text","value":"#str_next","overrideExpression":true},
   {"op":"action","action":"record"}
  ]
 }
}
)json";

Document Compile() {
	Document document; std::vector<Diagnostic> diagnostics;
	const bool good = document.Load(Source,diagnostics);
	for (const auto& item : diagnostics) std::fprintf(stderr,"%s: %s\n",item.pointer.c_str(),item.message.c_str());
	Check(good,"compile the complete event/state/presentation/action/timeline fixture");
	Check(document.Source() == Source,"event compilation preserves authored source");
	return document;
}
void Reset(const DocumentModel& model, State& state, Motion& motion) {
	std::string error;
	Check(state.Reset(model,error),"initialize typed state"); motion.Reset(model);
}
PresentationValue Read(const DocumentModel& model, const State& state, const Motion& motion, const char* alias) {
	PresentationValue value;
	Check(ReadPresentationAlias(model,state,motion,alias,value),"read public typed presentation alias");
	return value;
}
PresentationValue Number(double value) {
	PresentationValue result; result.data[0] = value; return result;
}
Expression Literal(StateValue value) {
	Expression result; result.type = value.index(); result.literal = std::move(value); return result;
}
EventStep Step(EventOp op, const std::string& target) {
	EventStep step; step.op = op; step.target = target; return step;
}
EventStep Set(const std::string& key, StateValue value) {
	EventStep step; step.values.emplace(key,Literal(std::move(value))); return step;
}
void Commit(EventResult result, State& state, Motion& motion) {
	state = std::move(result.state); motion = std::move(result.motion);
}
EventResult Run(const DocumentModel& model, const State& state, const Motion& motion, const char* event, double seconds) {
	EventResult result; std::string error;
	const bool good = EvaluateEvent(model,state,motion,event,seconds,result,error);
	if (!good) std::fprintf(stderr,"event %s: %s\n",event,error.c_str());
	Check(good,"evaluate valid event transaction"); return result;
}
double Argument(const ActionInvocation& action, const char* name) {
	return std::get<double>(action.arguments.at(name));
}

// Include ownership flags, revisions, playback phase and queued invocations in
// failure comparisons. Capture(0) reads the current monotonic clock unchanged.
void Dump(std::ostream& stream, const StateValues& values) {
	for (const auto& [name,value] : values) {
		stream << std::quoted(name) << ':' << value.index() << ':';
		std::visit([&](const auto& item) { stream << item; },value); stream << ';';
	}
}
void Dump(std::ostream& stream, const Value& value) {
	stream << static_cast<int>(value.type) << ':' << std::quoted(value.unit) << ':' << std::quoted(value.text);
	for (double component : value.data) stream << ':' << component;
}
void Dump(std::ostream& stream, const PropertyValues& properties) {
	for (const auto& [key,value] : properties) { stream << std::quoted(key.first) << std::quoted(key.second); Dump(stream,value); }
}
std::string Fingerprint(const State& state, const Motion& motion) {
	std::ostringstream stream; stream << std::setprecision(17) << state.Revision() << '|';
	Dump(stream,state.Variables()); Dump(stream,state.Properties());
	for (const auto& [name,enabled] : state.Enabled()) stream << std::quoted(name) << enabled;
	for (const auto& [name,cell] : state.Presentation().variables) {
		stream << std::quoted(name) << static_cast<int>(cell.value.type) << std::quoted(cell.value.text);
		for (double value : cell.value.data) stream << ':' << value;
		stream << cell.expressionDisabled << cell.pending;
	}
	for (const auto& [key,property] : state.Presentation().properties) {
		stream << std::quoted(key.first) << std::quoted(key.second) << property.expressionDisabled; Dump(stream,property.value);
	}
	Dump(stream,motion.Values());
	const auto snapshot = motion.Capture(0); stream << snapshot.reducedMotion;
	Dump(stream,snapshot.values);
	for (const auto& item : snapshot.playing) {
		stream << std::quoted(item.owner) << std::quoted(item.node) << std::quoted(item.property);
		Dump(stream,item.from); stream << ':' << item.elapsedMs << ':' << item.durationMs << ':' << item.paused << item.reduced;
	}
	return stream.str();
}
std::string Fingerprint(const EventResult& result) {
	std::ostringstream stream; stream << std::setprecision(17) << Fingerprint(result.state,result.motion);
	Dump(stream,result.stateChanges);
	for (const auto& action : result.actions) {
		stream << std::quoted(action.action) << std::quoted(action.operation); Dump(stream,action.arguments);
	}
	return stream.str();
}
void Reject(const DocumentModel& model, const State& state, const Motion& motion, const char* event,
	double seconds, const ActionValidator& validator = {}, size_t maxActions = 256) {
	EventResult previous; Reset(model,previous.state,previous.motion); std::string error;
	Check(previous.state.Set({{"a",99.0}},error),"prepare distinct previous output");
	Check(previous.motion.Play("slide",10),"prepare distinct previous playback"); previous.motion.Advance(10.1);
	previous.stateChanges = {{"sentinel",std::string("previous batch")}};
	previous.actions = {{"previous","previous.operation",{{"keep",true}}}};
	const auto beforeInput = Fingerprint(state,motion), beforeOutput = Fingerprint(previous);
	Check(!EvaluateEvent(model,state,motion,event,seconds,previous,error,validator,maxActions),"reject invalid event transaction");
	Check(!error.empty(),"event failure includes diagnostic");
	Check(Fingerprint(state,motion) == beforeInput,"failed event preserves all live state and motion");
	Check(Fingerprint(previous) == beforeOutput,"failed event preserves caller's previous complete output");
}

void Ordered(const DocumentModel& model) {
	State state; Motion motion; Reset(model,state,motion);
	const auto before = Fingerprint(state,motion);
	int validations = 0;
	EventResult result; std::string error;
	Check(EvaluateEvent(model,state,motion,"ORDERED",0,result,error,
		[&](const ActionInvocation& invocation, std::string&) {
			++validations; Check(invocation.operation == "test.record","validate typed operation data only"); return true;
		}),"case-folded event executes");
	Check(validations == 6 && result.actions.size() == 6,"nested events expand inline and validate every final invocation");
	const double counts[] = {2,2,3,3,3,4}, pages[] = {2,7,9,9,9,9}, priors[] = {1,1,1,1,55,55};
	for (size_t i = 0; i < result.actions.size(); ++i) {
		const auto& action = result.actions[i];
		Check(Argument(action,"count") == counts[i] && Argument(action,"page") == pages[i] &&
			Argument(action,"prior") == priors[i],"capture action arguments at their exact ordered instruction snapshot");
		Check(Argument(action,"pointX") == (i < 2 ? 3 : 4),"vector expression RHS components share one pre-write snapshot");
		Check(Argument(action,"width") == 100 && Argument(action,"host") == .75 &&
			std::get<bool>(action.arguments.at("shown")) && std::get<bool>(action.arguments.at("toggle")) &&
			std::get<std::string>(action.arguments.at("title")) == "#str_test" &&
			std::get<std::string>(action.arguments.at("metadata")) == "opaque ; data","action arguments retain exact scalar, boolean and string types");
	}
	Check(result.stateChanges == StateValues({{"a",4.0},{"b",55.0}}),"final touched application values exclude untouched and host sources");
	Check(std::get<double>(result.state.Variables().at("a")) == 4 &&
		Read(model,result.state,result.motion,"DESKTOP::CURR").data[0] == 9,"explicit shared alias ownership survives later state steps");
	Check(Fingerprint(state,motion) == before,"successful evaluation is staged until the owner commits it");
	Check(result.state.Presentation().variables.at("page").expressionDisabled,"ordered explicit override owns expression");
	Check(state.Set({{"flag",false}},error),"select else branch from actual state");
	Reject(model,state,motion,"ordered",0); // Previously lazy division by zero is now reached.
	Reset(model,state,motion); Check(state.Set({{"a",3.0}},error),"seed bounded conditional recursion");
	auto recursive = Run(model,state,motion,"recurse",0);
	Check(recursive.actions.size() == 3 && Argument(recursive.actions[0],"count") == 3 &&
		Argument(recursive.actions[1],"count") == 2 && Argument(recursive.actions[2],"count") == 1 &&
		std::get<double>(recursive.state.Variables().at("a")) == 0,"conditional recursion terminates on updated local state");
}

void AliasOwnership(const DocumentModel& model) {
	State state; Motion motion; Reset(model,state,motion); std::string error;
	Check(WritePresentationAlias(model,state,motion,"cUrR",Number(7),false,error),"transient scalar presentation write");
	auto result = Run(model,state,motion,"record",0);
	Check(Argument(result.actions.front(),"page") == 7 && result.stateChanges.empty(),"event entry preserves pending transient values");
	Commit(std::move(result),state,motion);
	Check(state.Set({},error) && Read(model,state,motion,"curr").data[0] == 1,"next state evaluation restores an enabled expression");
	Check(WritePresentationAlias(model,state,motion,"curr",Number(9),true,error) &&
		state.Set({{"a",2.0}},error) && Read(model,state,motion,"desktop::curr").data[0] == 9,"root and qualified aliases share explicit ownership");
	Check(WritePresentationAlias(model,state,motion,"desktop::curr",Number(11),false,error) &&
		state.Set({},error) && Read(model,state,motion,"curr").data[0] == 11,"later transient write cannot revive a disabled expression");
	Check(WritePresentationAlias(model,state,motion,"panel::opacity",Number(.8),false,error) &&
		Near(Read(model,state,motion,"panel::opacity").data[0],.8),"bound property accepts transient presentation");
	Check(state.Set({},error) && Near(Read(model,state,motion,"panel::opacity").data[0],.25),"bound property reevaluates on explicit state evaluation");
	Check(WritePresentationAlias(model,state,motion,"panel::opacity",Number(.6),true,error) &&
		WritePresentationAlias(model,state,motion,"panel::opacity",Number(.7),false,error) &&
		state.Set({{"level",.4}},error) && Near(Read(model,state,motion,"panel::opacity").data[0],.7),"disabled bound property remains presentation-owned");

	PresentationValue rect; rect.type = PresentationType::Vector4; rect.data = {50,60,150,80};
	Check(WritePresentationAlias(model,state,motion,"panel::rect",rect,true,error),"mixed bound/static/moving rect writes atomically");
	Check(Read(model,state,motion,"panel::rect").data == rect.data,"rect tuple preserves all components");
	const auto captured = Run(model,state,motion,"captureAliases",0);
	Check(captured.stateChanges == StateValues({{"a",11.0},{"b",150.0}}),"state batches consume scalar and vector-component aliases as typed expressions");
	const auto before = Fingerprint(state,motion);
	rect.data = {70,80,-1,100};
	Check(!WritePresentationAlias(model,state,motion,"panel::rect",rect,true,error) && !error.empty(),"invalid bound width rejects complete rect tuple");
	Check(Fingerprint(state,motion) == before,"invalid tuple cannot partially write static position or size");
	PresentationValue untouched; untouched.type = PresentationType::String; untouched.text = "previous";
	Check(!ReadPresentationAlias(model,state,motion,"missing",untouched) && untouched.type == PresentationType::String &&
		untouched.text == "previous","failed alias read preserves output");
	Check(!WritePresentationAlias(model,state,motion,"missing",Number(1),true,error) &&
		Fingerprint(state,motion) == before,"unknown alias writes are transactional");

	const auto fixed = Run(model,state,motion,"static",0);
	Check(Read(model,fixed.state,fixed.motion,"panel::color").data == std::array<double,4>{.4,.5,.6,1} &&
		Read(model,fixed.state,fixed.motion,"panel::noevents").data[0] == 1,"static vector and semantic boolean aliases are applied");
	Check(!std::get<bool>(fixed.actions.front().arguments.at("shown")) &&
		std::get<std::string>(fixed.actions.front().arguments.at("title")) == "#str_next","following action reads static string/visibility changes");
	Check(Run(model,state,motion,"whenVisible",0).actions.size() == 1 &&
		Run(model,fixed.state,fixed.motion,"whenVisible",0).actions.empty(),"Boolean presentation aliases drive lazy event branches");
	State peerState; Motion peerMotion; Reset(model,peerState,peerMotion);
	Check(Read(model,peerState,peerMotion,"curr").data[0] == 1 && Read(model,peerState,peerMotion,"panel::visible").data[0] == 1,
		"presentation transactions do not leak into another instance");
}

void Timelines(const DocumentModel& model) {
	State state; Motion motion; Reset(model,state,motion);
	Commit(Run(model,state,motion,"play",10),state,motion);
	Check(motion.IsPlaying("slide"),"play step acquires presentation timeline");
	auto advanced = Run(model,state,motion,"record",10.25);
	Check(Near(Argument(advanced.actions.front(),"left"),35) && Near(motion.Values().at({"panel","left"}).data[0],10),
		"event entry advances only staged motion and action sees the exact presentation time");
	Commit(std::move(advanced),state,motion);
	Commit(Run(model,state,motion,"pause",10.25),state,motion);
	Commit(Run(model,state,motion,"empty",20),state,motion);
	Check(Near(Read(model,state,motion,"panel::left").data[0],35) && motion.Capture(20).playing.front().paused,
		"pause holds phase across presentation-time gaps");
	Commit(Run(model,state,motion,"resume",20),state,motion);
	Commit(Run(model,state,motion,"empty",20.25),state,motion);
	Check(Near(Read(model,state,motion,"panel::left").data[0],60),"resume preserves remaining timeline progression");
	State heldState = state; Motion heldMotion = motion;
	Commit(Run(model,heldState,heldMotion,"hold",20.25),heldState,heldMotion);
	Check(!heldMotion.IsPlaying("slide") && Near(Read(model,heldState,heldMotion,"panel::left").data[0],60),"hold cancellation keeps sampled position");
	Commit(Run(model,state,motion,"base",20.25),state,motion);
	Check(!motion.IsPlaying("slide") && Near(Read(model,state,motion,"panel::left").data[0],10),"base cancellation restores authored position");
	Reset(model,state,motion); Commit(Run(model,state,motion,"play",10),state,motion);
	auto external = Run(model,state,motion,"moving",10.25);
	Check(Argument(external.actions.front(),"left") == 77 && external.motion.IsPlaying("slide"),"external moving-property write is immediately readable without cancelling playback");
	external.motion.Advance(10.5);
	Check(Near(Read(model,external.state,external.motion,"panel::left").data[0],60),"next animation sample reacquires its moving property");
}

void FailuresAndBudgets(const DocumentModel& compiled) {
	DocumentModel model = compiled;
	State state; Motion motion; Reset(model,state,motion); std::string error;
	Check(motion.Play("slide",10),"prepare running source for rollback");
	Check(WritePresentationAlias(model,state,motion,"curr",Number(7),false,error),"prepare pending source for rollback");
	int validations = 0;
	const ActionValidator count = [&](const ActionInvocation&, std::string&) { ++validations; return true; };
	EventStep property = Step(EventOp::SetPresentation,"panel::color");
	property.presentation = {Literal(.4),Literal(.5),Literal(.6),Literal(1.0)};
	model.events["late"] = {"late",{Set("a",8.0),property,Step(EventOp::PlayTimeline,"slide"),
		Step(EventOp::Action,"record"),Set("unknown",9.0)}};
	Reject(model,state,motion,"late",10.5,count);
	Check(validations == 0,"no action validators or host-facing dispatch run before all event instructions succeed");
	Reject(model,state,motion,"ordered",10.5,[&](const ActionInvocation& action, std::string& why) {
		++validations; Check(Argument(action,"count") == 2,"validator receives captured historical arguments");
		if (validations == 2) { why = "host contract rejected"; return false; } return true;
	});
	Check(validations == 2,"late host validation rejects the transaction without publishing earlier actions");
	Reject(model,state,motion,"ordered",10.5,[](const ActionInvocation&, std::string&) -> bool {
		throw std::runtime_error("validator exception");
	});
	Reject(model,state,motion,"missing",10.5);
	for (double time : {-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
		Reject(model,state,motion,"empty",time);
	Reject(model,state,motion,"empty",10.5,{},257);
	for (const auto& step : {Set("host",1.0),Set("a",true),
		Step(EventOp::Call,"missing"),Step(EventOp::Action,"missing"),
		Step(EventOp::PlayTimeline,"missing"),Step(EventOp::PauseTimeline,"missing"),
		Step(EventOp::ResumeTimeline,"missing"),Step(EventOp::CancelTimeline,"missing")}) {
		model.events["invalid"] = {"invalid",{step}}; Reject(model,state,motion,"invalid",10.5);
	}
	Expression broken; broken.presentation = "point"; broken.type = 0; // Missing vector component bypasses compiler.
	model.actions["broken"] = {"test.broken",{{"value",broken}}};
	model.events["invalid"] = {"invalid",{Step(EventOp::Action,"broken")}};
	Reject(model,state,motion,"invalid",10.5);
	broken.component = 2; model.actions["broken"].arguments["value"] = broken;
	Reject(model,state,motion,"invalid",10.5);
	EventStep wrongCondition; wrongCondition.op = EventOp::If; wrongCondition.condition = Literal(1.0);
	model.events["invalid"] = {"invalid",{wrongCondition}}; Reject(model,state,motion,"invalid",10.5);

	// A hand-built model exercises runtime defenses independently of compiler limits.
	model.events["queue"] = {"queue",std::vector<EventStep>(256,Step(EventOp::Action,"record"))};
	Check(Run(model,state,motion,"queue",10.5).actions.size() == 256,"runtime accepts exactly the maximum invocation count");
	model.events["queue"].steps.push_back(Step(EventOp::Action,"record"));
	Reject(model,state,motion,"queue",10.5);
	Reject(model,state,motion,"record",10.5,{},0);
	EventResult empty;
	Check(EvaluateEvent(model,state,motion,"empty",10.5,empty,error,{},0) && empty.actions.empty(),"zero action capacity still permits an event without actions");
	model.events["budget"] = {"budget",std::vector<EventStep>(16384,Set("a",1.0))};
	auto boundary = Run(model,state,motion,"budget",10.5);
	Check(boundary.stateChanges == StateValues({{"a",1.0}}),"step boundary preserves explicitly touched values equal to initial values");
	model.events["budget"].steps.push_back(Set("a",1.0)); Reject(model,state,motion,"budget",10.5);
	model.events["leaf"] = {"leaf",std::vector<EventStep>(8191,Set("a",1.0))};
	model.events["sharedbudget"] = {"sharedbudget",{Step(EventOp::Call,"leaf"),Step(EventOp::Call,"leaf")}};
	Check(Run(model,state,motion,"sharedbudget",10.5).stateChanges == StateValues({{"a",1.0}}),"nested calls share the exact 16384-instruction boundary");
	model.events["leaf"].steps.push_back(Set("a",1.0)); Reject(model,state,motion,"sharedbudget",10.5);
	model.events["invalid"] = {"invalid",{Step(static_cast<EventOp>(999),"")}}; Reject(model,state,motion,"invalid",10.5);
	for (int i = 0; i < 32; ++i) {
		const std::string name = "chain"+std::to_string(i);
		model.events[name] = {name,i == 31 ? std::vector<EventStep>{} : std::vector<EventStep>{Step(EventOp::Call,"chain"+std::to_string(i+1))}};
	}
	Check(Run(model,state,motion,"chain0",10.5).actions.empty(),"maximum call depth accepts a terminating program");
	model.events["chain31"].steps = {Step(EventOp::Call,"chain32")}; model.events["chain32"] = {"chain32",{}};
	Reject(model,state,motion,"chain0",10.5);
	EventStep nested; nested.op = EventOp::If; nested.condition = Literal(true);
	for (int i = 0; i < 33; ++i) {
		EventStep parent; parent.op = EventOp::If; parent.condition = Literal(true); parent.thenSteps.push_back(std::move(nested)); nested = std::move(parent);
	}
	model.events["nested"] = {"nested",{std::move(nested)}}; Reject(model,state,motion,"nested",10.5);
	Check(state.Set({{"a",40.0}},error),"seed recursion above shared stack bound");
	Reject(model,state,motion,"recurse",10.5);
}
} // namespace

int main() {
	auto document = Compile();
	Ordered(document.Model());
	AliasOwnership(document.Model());
	Timelines(document.Model());
	FailuresAndBudgets(document.Model());
	std::puts("UI behavior: ordered transactions, typed aliases/actions, ownership, motion, rollback and runtime budgets passed");
}
