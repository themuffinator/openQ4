// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Interaction.h"
#include "src/ui/retained/State.h"
#include <json/json.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>

using namespace openq4::ui;
static unsigned checks = 0;
static void Check(bool value, const char* message) {
	++checks; if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static Json::Value Typed(const char* type, Json::Value value, const char* unit = nullptr) {
	Json::Value result; result["type"] = type; result["value"] = std::move(value);
	if (unit) result["unit"] = unit;
	return result;
}
static Json::Value NodeJson(const char* id, const char* type = "group") {
	Json::Value node; node["id"] = id; node["type"] = type;
	node["properties"]["display"] = Typed("keyword","block");
	node["properties"]["position"] = Typed("keyword","absolute");
	node["properties"]["opacity"] = Typed("number",1);
	for (auto* property : {"left","top","width","height"}) node["properties"][property] = Typed("length",20,"dp");
	if (std::string(type) == "text") node["properties"]["text"] = Typed("text","#str_label");
	else node["children"] = Json::Value(Json::arrayValue);
	return node;
}
static Json::Value Source() {
	Json::Value source; source["format"] = "openq4-ui"; source["version"] = 1; source["id"] = "number-controls";
	source["state"]["brightness"]["type"] = "number"; source["state"]["brightness"]["initial"] = 1.0;
	source["actions"]["edit"]["input"] = "number"; source["actions"]["edit"]["operation"] = "settings.system.edit";
	source["actions"]["edit"]["arguments"]["r_brightness"]["input"] = "value";
	auto number = NodeJson("number"); auto& control = number["control"];
	control["role"] = "number"; control["action"] = "edit"; control["label"] = "#str_label";
	control["value"]["state"] = "brightness"; control["minimum"] = .5; control["maximum"] = 2;
	control["exponent"] = false; control["maxBytes"] = 32;
	for (const auto* state : {"default","hover","focus","pressed","disabled"}) control["states"][state] = "feedback";
	for (const auto* part : {"viewport","text","selection","caret","composition","validation"}) control["parts"][part] = part;
	auto viewport = NodeJson("viewport"); viewport["properties"]["overflow"] = Typed("keyword","hidden");
	auto text = NodeJson("text","text"); text["properties"]["white-space"] = Typed("keyword","pre");
	text["properties"]["text-align"] = Typed("keyword","left"); viewport["children"].append(text);
	for (const auto* part : {"selection","caret","composition"}) viewport["children"].append(NodeJson(part));
	number["children"].append(viewport); number["children"].append(NodeJson("validation","text"));
	source["root"] = NodeJson("root"); source["root"]["children"].append(number);
	Json::Value timeline, track; timeline["id"] = "feedback"; timeline["durationMs"] = 100;
	track["node"] = "number"; track["property"] = "opacity";
	for (unsigned t : {0u,100u}) { Json::Value key; key["atMs"] = t; key["value"] = Typed("number",1); track["keys"].append(key); }
	timeline["tracks"].append(track); source["timelines"].append(timeline); return source;
}
static std::string Encode(const Json::Value& source) { Json::StreamWriterBuilder writer; return Json::writeString(writer,source); }
static Json::Value& Number(Json::Value& source) { return source["root"]["children"][0]; }
static void Schema() {
	Document document; std::vector<Diagnostic> diagnostics; auto source = Source();
	if (!document.Load(Encode(source),diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s\n",d.message.c_str());
	Check(document.Load(Encode(source),diagnostics),"authored number with six distinct parts compiles");
	const auto& control = *document.Model().FindNode("number")->control;
	Check(control.role == ControlRole::Number && control.value->type == 0,"number compiles numeric state expression");
	const auto& spec = std::get<NumberSpec>(control.widget);
	Check(spec.minimum == .5 && spec.maximum == 2 && !spec.exponent && spec.maxBytes == 32,"compiled number preserves syntax/range/size policy");
	std::vector<std::function<void(Json::Value&)>> bad{
		[](auto& s) { Number(s)["control"]["value"] = true; },
		[](auto& s) { Number(s)["control"]["minimum"] = 3; },
		[](auto& s) { Number(s)["control"]["maximum"] = "2"; },
		[](auto& s) { Number(s)["control"]["exponent"] = 1; },
		[](auto& s) { Number(s)["control"]["maxBytes"] = 0; },
		[](auto& s) { Number(s)["control"]["maxBytes"] = 65537; },
		[](auto& s) { Number(s)["control"]["step"] = .1; },
		[](auto& s) { Number(s)["control"]["parts"].removeMember("composition"); },
		[](auto& s) { Number(s)["control"]["parts"]["caret"] = "selection"; },
		[](auto& s) { Number(s)["control"]["parts"]["text"] = "validation"; },
		[](auto& s) { Number(s)["children"][0]["properties"]["overflow"] = Typed("keyword","visible"); },
		[](auto& s) { Number(s)["children"][0]["properties"]["position"] = Typed("keyword","static"); },
		[](auto& s) { auto& parts = Number(s)["children"][0]["children"]; auto wrapper = NodeJson("nested");
			wrapper["children"].append(parts[0]); parts[0] = wrapper; },
		[](auto& s) { auto& parts = Number(s)["children"][0]["children"]; auto wrapper = NodeJson("nested");
			wrapper["children"].append(parts[2]); parts[2] = wrapper; },
		[](auto& s) { Json::Value transform(Json::arrayValue); for (auto v : {0,0,1,1,10}) transform.append(v);
			Number(s)["children"][0]["children"][0]["properties"]["transform"] = Typed("transform",transform,"dp"); },
		[](auto& s) { Json::Value transform(Json::arrayValue); for (auto v : {0,0,1,1,0}) transform.append(v);
			Number(s)["children"][0]["children"][2]["properties"]["transform"] = Typed("transform",transform,"dp"); },
		[](auto& s) { Number(s)["children"][0]["children"][0]["properties"]["white-space"] = Typed("keyword","normal"); },
		[](auto& s) { Number(s)["children"][0]["children"][0]["properties"]["text-align"] = Typed("keyword","right"); },
		[](auto& s) { Number(s)["children"][0]["children"][2]["properties"].removeMember("width"); },
		[](auto& s) { Number(s)["children"][0]["children"][2]["properties"].removeMember("opacity"); },
		[](auto& s) { s["timelines"][0]["tracks"][0]["node"] = "caret"; },
		[](auto& s) { Number(s)["children"][0]["children"][2]["properties"]["position"] = Typed("keyword","relative"); },
		[](auto& s) { Number(s)["children"][1]["properties"].removeMember("display"); },
		[](auto& s) { auto validation = Number(s)["children"][1]; Number(s)["children"].resize(1); Number(s)["children"][0]["children"].append(validation); },
		[](auto& s) { s["actions"]["edit"]["input"] = "boolean"; },
		[](auto& s) { Number(s)["children"][0]["control"] = Number(s)["control"]; },
		[](auto& s) { s["timelines"][0]["tracks"][0]["node"] = "caret"; s["timelines"][0]["tracks"][0]["property"] = "left";
			for (auto& k : s["timelines"][0]["tracks"][0]["keys"]) k["value"] = Typed("length",20,"dp"); }
	};
	for (const auto& mutate : bad) {
		auto candidate = source; mutate(candidate);
		Check(!document.Load(Encode(candidate),diagnostics) && !diagnostics.empty(),"invalid number schema rejected");
		Check(document.Model().id == "number-controls" && std::get<NumberSpec>(document.Model().FindNode("number")->control->widget).maxBytes == 32,"failed schema load preserves previous document");
	}
	State state; std::string error;
	Check(state.Reset(document.Model(),error),"state installs numeric readback");
	Check(state.Set({{"brightness",1.05}},error) && std::get<double>(state.ControlValues().at("number").value) == 1.05,"off-step state remains exact");
	Check(state.Set({{"brightness",42.0}},error) && std::get<double>(state.ControlValues().at("number").value) == 42,"custom out-of-policy actual readback remains visible");
	Check(state.Set({{"brightness",1.0}},error),"install ordinary brightness state");
	Interaction input; input.Reset(document.Model()); Check(input.SetReadbacks(state.ControlValues(),error),"compiled readbacks feed interaction");
	input.SetBounds({{"number",{0,0,120,30,true}}});
	Check(input.Focus("number") && input.BeginNumberEdit("number",error),"compiled number editor begins");
	Check(input.ReplaceNumberSelection("number",input.Widget("number")->number->identity,"1.05",error),"compiled number accepts off-step text");
	Check(input.CommitNumberEdit("number",input.Widget("number")->number->identity,error),"compiled number submits exact proposal");
	const auto invocationSource = input.TakeActions().at(0); ActionInvocation invocation;
	Check(document.Model().ResolveAction(invocationSource.action,state.Variables(),invocation,error,{},&*invocationSource.proposal),"real typed action resolves numeric operand");
	Check(invocation.operation == "settings.system.edit" && std::get<double>(invocation.arguments.at("r_brightness")) == 1.05 &&
		std::get<double>(state.Variables().at("brightness")) == 1.0,"immutable invocation retains exact proposal without changing application state");
}
static Control Make(ControlRole role) {
	Control c; c.role = role; c.action = "edit";
	for (const auto state : {ControlState::Default,ControlState::Hover,ControlState::Focus,ControlState::Pressed,ControlState::Disabled}) c.states[state] = "feedback";
	if (role != ControlRole::Button) { Expression expr; expr.type = 0; c.value = expr; }
	if (role == ControlRole::Number) { NumberSpec spec; spec.minimum = .5; spec.maximum = 2; spec.maxBytes = 32; c.widget = spec; }
	if (role == ControlRole::Slider) { SliderSpec spec; spec.minimum = .5; spec.maximum = 2; spec.step = .1; c.widget = spec; }
	return c;
}
struct Fixture {
	DocumentModel model; Interaction input; std::string error;
	std::map<std::string,ControlBounds> bounds;
	std::map<std::string,ControlReadback> values;
	Fixture() {
		model.id = "number-controls"; model.root.id = "root";
		for (const auto& [id,role] : std::vector<std::pair<std::string,ControlRole>>{{"number",ControlRole::Number},{"peer",ControlRole::Number},{"slider",ControlRole::Slider},{"button",ControlRole::Button}}) {
			Node node; node.id = id; node.control = Make(role); model.root.children.push_back(node); bounds[id] = {0,0,120,30,true};
			if (role != ControlRole::Button) values[id] = {1.0,false,{}};
		}
		Reset();
	}
	void Reset() { input.Reset(model); Check(input.SetReadbacks(values,error),"install valid full readbacks"); input.SetBounds(bounds); }
	void Read(double value) { values["number"].value = value; Check(input.SetReadbacks(values,error),"refresh authoritative number readback"); }
	WidgetViewState View() const { return *input.Widget("number"); }
	NumberEditIdentity Identity() const { Check(View().number.has_value(),"editor identity exists"); return View().number->identity; }
	void Begin() { Check(input.Focus("number") && input.BeginNumberEdit("number",error),"focused number begins local editor"); }
	void Select() { Check(input.SetNumberSelection("number",Identity(),0,View().number->state.text.size(),error),"select local text"); }
	void Text(std::string_view text) { Select(); Check(input.ReplaceNumberSelection("number",Identity(),text,error),"local replacement succeeds"); }
	void Press(MenuInput key) { input.Input(key,true); input.Input(key,false); }
	ControlAction Commit() { Check(input.CommitNumberEdit("number",Identity(),error),"explicit valid number commit"); auto actions = input.TakeActions(); Check(actions.size() == 1,"commit emits exactly one invocation"); return actions[0]; }
};
static void CommitAndReadback() {
	Fixture f; f.Begin(); const auto initial = f.Identity();
	Check(f.View().number->state.text == "1" && !f.View().number->dirty,"begin paints accepted value and selects all");
	Check(f.input.BeginNumberEdit("number",f.error) && f.Identity() == initial,"repeated begin preserves edit session");
	f.Text("1.05"); Check(f.View().number->dirty && std::get<double>(f.View().accepted) == 1.0 && f.input.TakeActions().empty(),"typing changes local buffer only");
	auto action = f.Commit();
	Check(action.proposal && std::get<double>(*action.proposal) == 1.05 && action.editSession == initial.session && action.editRevision == f.Identity().revision,"exact off-step proposal carries edit identity");
	Check(f.input.CanDispatchControlAction(action),"current proposal can dispatch");
	for (unsigned mutation = 0; mutation < 7; ++mutation) {
		auto stale = action;
		if (mutation == 0) ++stale.editSession;
		if (mutation == 1) ++stale.editRevision;
		if (mutation == 2) ++stale.proposalToken;
		if (mutation == 3) stale.proposal = 1.15;
		if (mutation == 4) stale.document = "other";
		if (mutation == 5) stale.modalToken = 0;
		if (mutation == 6) stale.action = "other";
		Check(!f.input.CanDispatchControlAction(stale),"altered/deferred number invocation cannot dispatch");
	}
	Check(!f.input.CommitNumberEdit("number",f.Identity(),f.error) && f.input.TakeActions().empty(),"outstanding commit cannot replay");
	Check(!f.input.ReplaceNumberSelection("number",f.Identity(),"2",f.error),"queued proposal is immutable while outstanding");
	f.Read(1.05); Check(f.input.CanDispatchControlAction(action) && f.View().number->state.text == "1.05","matching readback awaits explicit acknowledgement");
	Check(f.input.AcknowledgeProposal("number",action.proposalToken,true),"authoritative success acknowledged");
	Check(!f.View().pending && !f.View().number->dirty && f.Identity().session != initial.session && std::get<double>(f.View().accepted) == 1.05,"accepted readback rebases local text and retires old session");
	Check(!f.input.CanDispatchControlAction(action) && !f.input.AcknowledgeProposal("number",action.proposalToken,true),"delivered action and acknowledgement cannot replay");
	f.Text("1.15"); action = f.Commit(); auto revision = f.Identity();
	Check(f.input.AcknowledgeProposal("number",action.proposalToken,false),"host rejection acknowledged");
	Check(f.View().number->state.text == "1.15" && f.View().number->dirty && f.View().rejected && !f.View().number->conflict && f.Identity() != revision,"rejected text remains editable with a fresh revision");
	f.Text("1.25"); Check(!f.View().rejected,"editing clears old rejection"); action = f.Commit();
	Check(f.input.AcknowledgeProposal("number",action.proposalToken,true) && std::get<double>(f.View().accepted) == 1.05 && f.View().number->conflict,"ack without matching readback cannot invent an accepted value");
	Check(!f.input.CommitNumberEdit("number",f.Identity(),f.error),"unbacked acknowledgement requires explicit rebase");
	f.Begin(); Check(f.View().number->state.text == "1.25" && f.View().number->conflict,"begin preserves conflicted text");
	Check(f.input.ResolveNumberConflict("number",f.Identity(),false,f.error) && f.View().number->state.text == "1.05" && !f.View().number->conflict,"explicit reload resolves conflict from actual readback");
}
static void EditingAndComposition() {
	Fixture f; f.Begin();
	for (const auto* text : {"","-",".","1e","1e-","abc","NaN","inf","0.49","2.01","1,2"," 1"}) {
		f.Text(text); auto before = f.View(); auto identity = f.Identity();
		Check(!f.input.CommitNumberEdit("number",identity,f.error) && !f.error.empty(),"invalid/intermediate/out-of-range number cannot commit");
		Check(f.View().number->state.text == text && f.Identity() == identity && f.input.TakeActions().empty() && std::get<double>(f.View().accepted) == 1,"failed commit preserves local edit and accepted data");
	}
	f.Text("1.25"); auto identity = f.Identity(); auto before = f.View().number->state;
	for (const auto& invalid : {std::string(33,'x'),std::string("bad\0text",8),std::string("\xc0\xaf",2),std::string("\n")}) {
		Check(!f.input.ReplaceNumberSelection("number",identity,invalid,f.error),"invalid text edit is atomic");
		Check(f.Identity() == identity && f.View().number->state.text == before.text,"invalid input cannot advance revision or mutate text");
	}
	TextInputEvent preedit; Check(MakeTextInputPreedit(".5",TextIndexUnit::UnicodeScalars,1,1,preedit,f.error),"build actual preedit event");
	f.Select(); Check(f.input.ApplyNumberInput("number",f.Identity(),preedit,f.error),"preedit has local presentation");
	Check(f.View().number->composition && f.View().number->state.text == "1.25" && f.input.TakeActions().empty(),"composition is distinct from committed local buffer");
	Check(!f.input.CommitNumberEdit("number",f.Identity(),f.error),"valid underlying buffer cannot commit during composition");
	f.Press(MenuInput::Back); Check(!f.View().number->composition && f.View().number->state.text == "1.25" && f.input.TakeActions().empty(),"first Back cancels preedit only");
	Check(f.input.ApplyNumberInput("number",f.Identity(),preedit,f.error),"new preedit starts");
	TextInputEvent empty; Check(MakeTextInputPreedit("",TextIndexUnit::Utf8Bytes,0,0,empty,f.error),"build clear preedit");
	Check(f.input.ApplyNumberInput("number",f.Identity(),empty,f.error),"clear composition presentation");
	TextInputEvent commit; Check(MakeTextInputCommit("1.75",commit,f.error),"build commit");
	Check(f.input.ApplyNumberInput("number",f.Identity(),commit,f.error) && f.View().number->state.text == "1.75" && f.input.TakeActions().empty(),"IME commit edits text but does not submit a setting");
	Check(f.input.UndoNumberEdit("number",f.Identity(),false,f.error) && f.View().number->state.text == "1.25","clear-preedit then commit is one undo operation");
	Check(f.input.UndoNumberEdit("number",f.Identity(),true,f.error) && f.View().number->state.text == "1.75","redo restores text without action");
	f.Press(MenuInput::Accept); Check(f.input.TakeActions().empty(),"generic Accept cannot submit or double-activate the editor");
	f.Press(MenuInput::Back); Check(!f.View().number && f.input.TakeActions().empty(),"Back exits local editing without parent dismissal");
	f.Press(MenuInput::Back); Check(f.input.TakeActions().size() == 1,"fresh subsequent Back reaches normal owner routing");
}
static void LifetimesAndConflicts() {
	Fixture f; f.Begin(); f.Text("1.05"); const auto stale = f.Identity(); const auto action = f.Commit();
	Check(f.input.Focus("button") && f.View().number && !f.View().number->active && !f.View().pending,"focus loss detaches editor and cancels outstanding proposal");
	Check(!f.input.CanDispatchControlAction(action) && !f.input.AcknowledgeProposal("number",action.proposalToken,true),"taken action becomes stale after focus loss");
	f.Begin(); Check(f.Identity().session != stale.session,"rebegin never recycles edit identity");
	Check(!f.input.ReplaceNumberSelection("number",stale,"2",f.error) && !f.input.CancelNumberEdit("number",stale),"stale input/cancel cannot reach new editor");
	Fixture peer; peer.Begin(); Check(!peer.input.ReplaceNumberSelection("number",f.Identity(),"2",peer.error),"distinct instances reject each other's identity");
	f.Text("1.15"); auto identity = f.Identity(); f.Read(1.5);
	Check(f.View().number->conflict && f.View().number->state.text == "1.15" && std::get<double>(f.View().accepted) == 1.5 && f.Identity() != identity,"conflicting readback preserves local text but invalidates revision");
	Check(!f.input.ReplaceNumberSelection("number",identity,"2",f.error) && !f.input.CommitNumberEdit("number",f.Identity(),f.error),"conflict cannot overwrite external accepted value");
	f.Begin(); Check(f.input.ResolveNumberConflict("number",f.Identity(),false,f.error) && f.View().number->state.text == "1.5" && !f.View().number->dirty,"explicit reload rebases external data");
	identity = f.Identity(); f.Read(1.75); Check(f.View().number->state.text == "1.75" && !f.View().number->conflict && f.Identity() != identity,"clean local editor follows authoritative changes");
	f.Text("1.6"); identity = f.Identity(); auto bad = f.values; bad["peer"].value = true; bad["number"].value = 1.2;
	Check(!f.input.SetReadbacks(bad,f.error) && f.Identity() == identity && std::get<double>(f.View().accepted) == 1.75,"late invalid readback leaves all local/accepted state untouched");
	f.values["peer"].value = 1.1; Check(f.input.SetReadbacks(f.values,f.error) && f.Identity() == identity,"unrelated peer readback preserves local history and revision");
	Check(f.input.CommitNumberEdit("number",f.Identity(),f.error),"queue local proposal before cancellation"); f.input.Cancel();
	Check(f.input.TakeActions().empty() && f.View().number && !f.View().number->active,"cancellation removes still-queued number actions and detaches local draft");
	f.Begin(); f.Text("1.4"); Check(f.input.PushModal("number") && f.View().number && !f.View().number->active,"modal scope change detaches even surviving focused node's editor");
	Check(f.input.PopModal(),"close manual scope"); f.Begin(); f.Text("1.3");
	const auto snapshot = f.input.Capture(); const auto widgets = f.input.CaptureWidgets(); identity = f.Identity();
	Check(widgets.widgets.at("number").role == ControlRole::Number && f.input.RestoreWidgets(widgets,f.error),"snapshot preserves number role and local draft");
	Check(f.View().number && !f.View().number->active && !f.input.CommitNumberEdit("number",identity,f.error),"widget restore cannot retain live native/edit tokens");
	f.Begin(); Check(f.input.Restore(snapshot,f.error) && !f.View().number,"interaction restore also clears local transient editor");
	f.input.SetBounds(f.bounds); f.Begin(); f.bounds["number"].visible = false; f.input.SetBounds(f.bounds);
	Check(f.View().number && !f.View().number->active && !f.input.BeginNumberEdit("number",f.error),"hidden field retains draft without a live editor");
}
static void PolicyAndSliderCompatibility() {
	Fixture f; auto& spec = std::get<NumberSpec>(f.model.root.children[0].control->widget); spec.exponent = false; f.Reset(); f.Begin(); f.Text("1e0");
	Check(f.View().number->status == TextNumberStatus::Invalid && !f.input.CommitNumberEdit("number",f.Identity(),f.error),"authored exponent policy enforced");
	f.Text("1.05"); auto a = f.Commit(); f.Read(1.05); Check(f.input.AcknowledgeProposal("number",a.proposalToken,true),"exact off-step value accepted");
	f.values["slider"].value = 1.05; Check(f.input.SetReadbacks(f.values,f.error),"sibling slider accepts off-step readback");
	Check(f.input.Focus("slider"),"focus sibling slider"); f.Press(MenuInput::Right); a = f.input.TakeActions()[0];
	Check(std::abs(std::get<double>(*a.proposal)-1.1) < 1e-12 && a.editSession == 0 && a.editRevision == 0,"sibling slider retains its independent tick semantics");
	f.Read(42); f.Begin(); Check(f.View().number->state.text == "42" && f.View().number->status == TextNumberStatus::OutOfRange,"custom original value displays honestly");
	f.Text("1.3333333333333333"); a = f.Commit(); Check(std::get<double>(*a.proposal) == 1.3333333333333333,"number does not round to slider decimals");
	for (unsigned mutation = 0; mutation < 4; ++mutation) {
		Fixture invalid;
		auto& policy = std::get<NumberSpec>(invalid.model.root.children[0].control->widget);
		if (mutation == 0) policy.minimum = std::numeric_limits<double>::quiet_NaN();
		if (mutation == 1) policy.maximum = -1;
		if (mutation == 2) policy.maxBytes = 0;
		if (mutation == 3) invalid.model.root.children[0].control->widget = SliderSpec{};
		invalid.input.Reset(invalid.model); Check(!invalid.input.SetReadbacks(invalid.values,invalid.error),"manual malformed number policies are rejected before interaction");
	}
}
static void PendingAndFailureBoundaries() {
	Fixture f; f.Begin(); auto action = f.Commit(); // Explicit unchanged value still owns a proposal.
	f.Read(1.5);
	Check(!f.View().pending && !f.input.CanDispatchControlAction(action) && !f.input.AcknowledgeProposal("number",action.proposalToken,true) &&
		f.View().number->state.text == "1.5" && !f.View().number->conflict,"clean external readback retires an equal-value queued proposal");
	f.Text("1.25"); action = f.Commit(); f.Read(1.75);
	Check(!f.View().pending && f.View().number->conflict && f.View().number->state.text == "1.25" && !f.input.CanDispatchControlAction(action),"external readback invalidates outstanding dirty proposal");
	f.Begin(); Check(f.input.ResolveNumberConflict("number",f.Identity(),false,f.error),"explicit reload resolves prior external conflict");
	f.Text("1.2"); auto identity = f.Identity(); auto snapshot = f.input.CaptureWidgets();
	snapshot.widgets.at("number").firstVisible = 1;
	Check(!f.input.RestoreWidgets(snapshot,f.error) && f.Identity() == identity && f.View().number->state.text == "1.2","invalid snapshot leaves transient editor untouched atomically");
	f.input.Input(MenuInput::Back,true); f.input.Input(MenuInput::Back,true);
	Check(!f.View().number && f.input.TakeActions().empty(),"held Back cancels local editor once without also dismissing");
	f.input.Input(MenuInput::Back,false); f.Press(MenuInput::Back); Check(f.input.TakeActions().size() == 1,"fresh Back after release has normal semantics");
	f.input.Hover("number"); f.input.Pointer(true); f.input.Pointer(false);
	Check(f.View().number && f.input.TakeActions().empty(),"matched pointer activation begins editing without application action");
	identity = f.Identity(); f.input.InvalidateLayout(); f.input.SetBounds(f.bounds);
	Check(f.View().number && !f.View().number->active && !f.input.ReplaceNumberSelection("number",identity,"1",f.error),"layout invalidation retires native edit revision before fresh layout");
	f.Begin(); f.input.SetEnabled("number",false); Check(f.View().number && !f.View().number->active,"disable detaches local editor");
	f.input.SetEnabled("number",true); f.Begin(); f.input.Focus("button");
	for (unsigned i = 0; i < 256; ++i) f.Press(MenuInput::Accept);
	f.Begin(); f.Text("1.1"); identity = f.Identity();
	Check(!f.input.CommitNumberEdit("number",identity,f.error) && !f.View().pending && f.Identity() == identity && f.input.Overflowed(),"full action queue cannot claim a committed number proposal");
	Check(f.input.TakeActions().size() == 256 && f.View().number->state.text == "1.1","queue refusal preserves old records and editable candidate");
	Fixture shortField; std::get<NumberSpec>(shortField.model.root.children[0].control->widget).maxBytes = 1;
	shortField.values["number"].value = 1.25; shortField.Reset(); shortField.input.Focus("number");
	Check(!shortField.input.BeginNumberEdit("number",shortField.error) && !shortField.View().number && std::get<double>(shortField.View().accepted) == 1.25,"too-small local budget reports failure without truncating actual readback");
}
int main() {
	Schema(); CommitAndReadback(); EditingAndComposition(); LifetimesAndConflicts(); PolicyAndSliderCompatibility(); PendingAndFailureBoundaries();
	std::printf("UI number controls: %u checks passed\n",checks); return 0;
}
