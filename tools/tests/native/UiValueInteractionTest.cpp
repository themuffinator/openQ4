// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Interaction.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace openq4::ui;
static unsigned checks = 0;
static void Check(bool condition, const char* message) {
	++checks; if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Near(double a, double b) { return std::abs(a-b) < 1e-10; }
static Control MakeControl(ControlRole role) {
	Control c; c.role = role; c.action = "edit";
	for (auto state : {ControlState::Default,ControlState::Hover,ControlState::Focus,ControlState::Pressed,ControlState::Disabled}) c.states[state] = "feedback";
	if (role != ControlRole::Button) { Expression value; value.type = role == ControlRole::Toggle ? 1 : role == ControlRole::Choice ? 2 : 0; c.value = value; }
	if (role == ControlRole::Toggle) c.widget = ToggleSpec{};
	if (role == ControlRole::Slider) { SliderSpec spec; spec.minimum = .5; spec.maximum = 2; spec.step = .1; c.widget = spec; }
	if (role == ControlRole::Choice) {
		ChoiceSpec spec; spec.visibleRows = 3;
		for (unsigned i = 0; i < 8; ++i) { ChoiceOption option; option.id = "o"+std::to_string(i); option.value = std::string("value")+std::to_string(i); spec.options.push_back(option); }
		c.widget = spec;
	}
	return c;
}
struct Fixture {
	DocumentModel model;
	Interaction input;
	std::map<std::string,ControlBounds> bounds;
	std::map<std::string,ControlReadback> values;
	std::string error;
	Fixture() {
		model.id = "value-controls"; model.root.id = "root";
		unsigned n = 0;
		for (const auto& [id,role] : std::vector<std::pair<std::string,ControlRole>>{{"button",ControlRole::Button},{"toggle",ControlRole::Toggle},{"slider",ControlRole::Slider},{"choice",ControlRole::Choice}}) {
			Node node; node.id = id; node.control = MakeControl(role); model.root.children.push_back(node);
			bounds[id] = {0,static_cast<float>(n++*40),120,30,true};
		}
		values["toggle"] = {false,false,{}}; values["slider"] = {1.05,false,{}};
		values["choice"] = {std::string("custom"),false,{true,false,true,true,false,true,true,true}};
		input.Reset(model); Check(input.SetReadbacks(values,error),"install complete typed readbacks"); input.SetBounds(bounds); input.TakeFeedback();
	}
	void Press(MenuInput key) { input.Input(key,true); input.Input(key,false); }
	void Accept(const std::string& id) { Check(input.Focus(id),"focus control"); Press(MenuInput::Accept); }
	std::vector<ControlAction> Actions() { return input.TakeActions(); }
	WidgetViewState View(const std::string& id) { return *input.Widget(id); }
	void Refresh() { Check(input.SetReadbacks(values,error),"refresh valid readbacks"); }
};
static void ButtonAndToggle() {
	Fixture f;
	f.input.Focus("button"); f.input.Input(MenuInput::Accept,true); f.input.Input(MenuInput::Accept,true);
	Check(f.Actions().empty(),"button waits for matched release"); f.input.Input(MenuInput::Accept,false);
	auto a = f.Actions(); Check(a.size() == 1 && !a[0].proposal && !a[0].proposalToken,"button action compatibility");
	f.input.Hover("button"); f.input.Pointer(true); f.input.Hover("toggle"); f.input.Pointer(false);
	Check(f.Actions().empty(),"button drag off cannot click another control");
	f.input.Input(MenuInput::Accept,false); Check(f.Actions().empty(),"orphan accept release ignored");
	f.Accept("toggle"); f.Accept("toggle"); f.Accept("toggle"); a = f.Actions();
	Check(a.size() == 3 && std::get<bool>(*a[0].proposal) && !std::get<bool>(*a[1].proposal) && std::get<bool>(*a[2].proposal),"queued toggle proposals chain absolute pending values");
	Check(a[0].proposalToken < a[1].proposalToken && a[1].proposalToken < a[2].proposalToken,"tokens strictly increase");
	Check(!std::get<bool>(f.View("toggle").accepted),"proposals never change accepted value");
	Check(!f.input.AcknowledgeProposal("toggle",a[0].proposalToken,true) && f.View("toggle").proposalToken == a[2].proposalToken,"stale acknowledgement cannot clear newer proposal");
	Check(f.input.AcknowledgeProposal("toggle",a[2].proposalToken,false),"latest rejection acknowledged");
	Check(!f.View("toggle").pending && std::get<bool>(*f.View("toggle").rejected) && !std::get<bool>(f.View("toggle").accepted),"rejected candidate remains separate from accepted data");
	f.Accept("toggle"); auto next = f.Actions()[0]; Check(!f.View("toggle").rejected,"new candidate clears rejection presentation");
	f.values["toggle"].value = true; f.Refresh();
	Check(f.input.AcknowledgeProposal("toggle",next.proposalToken,true) && std::get<bool>(f.View("toggle").accepted) && !f.View("toggle").pending,"successful host readback and acknowledgement remain separate");
	f.values["toggle"].mixed = true; f.Refresh(); f.Accept("toggle");
	Check(std::get<bool>(*f.Actions()[0].proposal),"mixed toggle proposes checked on first activation");
	f.input.PointerPart("toggle"); f.input.Pointer(true); f.input.Cancel(); f.input.Pointer(false);
	Check(f.Actions().empty(),"cancelled toggle release cannot commit");
}
static void AtomicReadbacks() {
	Fixture f; f.Accept("toggle"); auto token = f.View("toggle").proposalToken;
	for (unsigned mutation = 0; mutation < 8; ++mutation) {
		auto bad = f.values;
		switch (mutation) {
			case 0: bad.erase("choice"); break;
			case 1: bad["unknown"] = bad["toggle"]; break;
			case 2: bad["slider"].value = true; break;
			case 3: bad["slider"].value = std::numeric_limits<double>::infinity(); break;
			case 4: bad["choice"].enabledOptions.pop_back(); break;
			case 5: bad["slider"].mixed = true; break;
			case 6: bad["toggle"].enabledOptions = {true}; break;
			case 7: bad["choice"].value = std::string("bad\0data",8); break;
		}
		bad["toggle"].value = true;
		Check(!f.input.SetReadbacks(bad,f.error) && !f.error.empty(),"malformed readback batch rejected");
		Check(!std::get<bool>(f.View("toggle").accepted) && f.View("toggle").proposalToken == token,"late invalid readback leaves all accepted/pending state unchanged");
	}
	f.values["slider"].value = 3.0; f.Refresh();
	Check(std::get<double>(f.View("slider").accepted) == 3 && std::get<std::string>(f.View("choice").accepted) == "custom","out-of-range and unmatched actual baselines preserved");
	f.input.SetEnabled("toggle",false); f.input.Focus("button"); f.input.PointerPart("toggle"); f.input.Pointer(true); f.input.Pointer(false);
	Check(f.Actions().size() == 1,"disabled interaction cannot append proposal");
}
static void SliderKeyboard() {
	Fixture f; f.input.Focus("slider"); f.Press(MenuInput::Right); f.Press(MenuInput::Right);
	auto a = f.Actions(); Check(a.size() == 2 && Near(std::get<double>(*a[0].proposal),1.1) && Near(std::get<double>(*a[1].proposal),1.2),"keyboard moves custom value to authored tick then accumulates pending steps");
	f.Press(MenuInput::Left); Check(Near(std::get<double>(*f.Actions()[0].proposal),1.1),"decrement remains minimum-anchored");
	f.Press(MenuInput::Home); f.Press(MenuInput::End); f.Press(MenuInput::PageDown); a = f.Actions();
	Check(a.size() == 3 && Near(std::get<double>(*a[0].proposal),.5) && Near(std::get<double>(*a[1].proposal),2) && Near(std::get<double>(*a[2].proposal),1),"Home End and page steps clamp to endpoints");
	f.Press(MenuInput::End); f.Actions(); f.Press(MenuInput::Right); Check(f.Actions().empty(),"endpoint repeat does not enqueue unchanged value");
	Check(Near(std::get<double>(f.View("slider").accepted),1.05),"keyboard editing never quantizes accepted baseline");
	auto snapshot = f.input.CaptureWidgets(); Check(f.input.RestoreWidgets(snapshot,f.error),"discard transient proposals by restore");
	f.values["slider"].value = -100.0; f.Refresh(); f.Press(MenuInput::Right);
	Check(Near(std::get<double>(*f.Actions()[0].proposal),.6) && Near(std::get<double>(f.View("slider").accepted),-100),"keyboard clamps editing position only");
	std::get<SliderSpec>(f.model.root.children[2].control->widget).vertical = true;
	f.input.Reset(f.model); f.Refresh(); f.input.SetBounds(f.bounds); f.input.Focus("slider"); f.Press(MenuInput::Up);
	Check(Near(std::get<double>(*f.Actions()[0].proposal),.6),"vertical Up increases normalized slider value");
}
static void SliderPointerAndCancel() {
	Fixture f;
	f.input.PointerPart("slider",.2); f.input.Pointer(true); f.input.PointerPart("slider",.51);
	Check(f.Actions().empty() && f.input.CapturedPointerControl() == "slider" && Near(std::get<double>(*f.View("slider").preview),1.3),"captured drag previews nearest authored tick without proposal");
	f.input.PointerPart("slider",1.8); f.input.Pointer(false); auto a = f.Actions();
	Check(a.size() == 1 && Near(std::get<double>(*a[0].proposal),2) && !f.View("slider").preview,"release outside track commits one bounded captured preview");
	f.input.Pointer(false); Check(f.Actions().empty(),"duplicate pointer release cannot commit twice");
	for (unsigned cancel = 0; cancel < 6; ++cancel) {
		Fixture g; g.input.PointerPart("slider",.5); g.input.Pointer(true);
		switch (cancel) {
			case 0: g.Press(MenuInput::Back); break;
			case 1: g.input.Focus("button"); break;
			case 2: g.input.Cancel(); break;
			case 3: g.input.PushModal("toggle"); break;
			case 4: g.input.SetEnabled("slider",false); break;
			case 5: g.bounds["slider"].visible = false; g.input.SetBounds(g.bounds); break;
		}
		g.input.PointerPart("slider",1); g.input.Pointer(false);
		Check(g.Actions().empty() && !g.View("slider").preview,"Escape focus owner modal disable and clipping loss cancel without proposal or global Back");
	}
	Fixture h; h.input.PointerPart("slider",.2); h.input.Pointer(true); h.Press(MenuInput::End);
	Check(h.Actions().empty(),"captured drag excludes simultaneous keyboard value edits"); h.input.Pointer(false); Check(h.Actions().size() == 1,"captured pointer remains sole edit owner");
	for (const auto fraction : {std::optional<double>{},std::optional<double>{std::numeric_limits<double>::quiet_NaN()}}) {
		Fixture invalid; invalid.input.PointerPart("slider",.2); invalid.input.Pointer(true); invalid.input.PointerPart("slider",fraction); invalid.input.Pointer(false);
		Check(invalid.Actions().empty() && !invalid.View("slider").preview,"lost or nonfinite track projection cancels stale drag preview");
	}
}
static void ChoiceNavigation() {
	Fixture f; f.Accept("choice");
	Check(f.Actions().empty() && f.View("choice").popupOpen && f.View("choice").highlight == "o0" && f.input.Focused() == "choice","paired activation opens anchored popup without changing custom baseline");
	f.Press(MenuInput::Down); Check(f.View("choice").highlight == "o2","choice navigation skips disabled rows");
	f.Press(MenuInput::PageDown); Check(f.View("choice").highlight == "o6" && f.View("choice").firstVisible == 4,"page navigation keeps tentative row visible in bounded viewport");
	f.Press(MenuInput::End); Check(f.View("choice").highlight == "o7" && f.View("choice").firstVisible == 5,"End reaches last eligible bounded row");
	f.Press(MenuInput::Next); Check(f.input.Focused() == "choice" && f.View("choice").popupOpen,"popup consumes tab navigation without leaving anchor");
	f.Press(MenuInput::Accept); auto a = f.Actions();
	Check(a.size() == 1 && std::get<std::string>(*a[0].proposal) == "value7" && !f.View("choice").popupOpen,"paired popup Accept commits tentative choice once");
	Check(std::get<std::string>(f.View("choice").accepted) == "custom","choice proposal never rewrites custom actual baseline");
	f.Accept("choice"); Check(f.View("choice").highlight == "o7","new popup starts at latest pending absolute proposal");
	f.Press(MenuInput::Home); Check(f.View("choice").highlight == "o0" && f.View("choice").firstVisible == 0,"Home restores first eligible viewport row");
	f.Press(MenuInput::Back); Check(f.Actions().empty() && !f.View("choice").popupOpen,"popup Back consumes cancel without global menu Back");
	f.Press(MenuInput::Back); a = f.Actions(); Check(a.size() == 1 && a[0].kind == ControlAction::Kind::Back,"fresh Back after cancellation routes normally");
}
static void ChoicePointerAndReadback() {
	Fixture f; f.Accept("choice"); f.input.PointerPart("choice",{},"o1"); f.input.Pointer(true); f.input.Pointer(false);
	Check(f.Actions().empty() && f.View("choice").popupOpen,"disabled row pointer press and release cannot commit");
	f.input.PointerPart("choice",{},"o3"); f.input.Pointer(true); f.input.PointerPart("choice",{},"o5"); f.input.Pointer(false);
	Check(f.Actions().empty(),"choice release must match its pressed row");
	f.input.PointerPart("choice",{},"o5"); f.input.Pointer(true); f.input.Pointer(false);
	auto a = f.Actions(); Check(a.size() == 1 && std::get<std::string>(*a[0].proposal) == "value5","matched enabled row commits frozen typed option");
	f.Accept("choice"); f.input.PointerPart("button"); f.input.Pointer(true);
	Check(!f.View("choice").popupOpen && f.input.Focused() == "choice","outside popup press cancels and does not focus underlying button");
	f.input.PointerPart("button"); f.input.Pointer(false); Check(f.Actions().empty(),"outside popup release cannot click through");
	f.Accept("choice"); f.Press(MenuInput::Home); f.input.Input(MenuInput::Accept,true);
	f.values["choice"].enabledOptions[0] = false; f.Refresh(); f.input.Input(MenuInput::Accept,false);
	Check(f.Actions().empty() && f.View("choice").popupOpen && f.View("choice").highlight == "o2","readback disabling armed tentative option prevents release commit");
	f.values["choice"].enabledOptions.assign(8,false); f.Refresh(); f.Press(MenuInput::End); f.Press(MenuInput::Accept);
	Check(f.Actions().empty() && f.View("choice").highlight.empty(),"no eligible options is safe and cannot propose");
	f.Press(MenuInput::Back);
	f.values["choice"].enabledOptions.assign(8,true); f.Refresh(); f.Accept("choice");
	f.input.Input(MenuInput::Accept,true); f.Press(MenuInput::Down); f.input.Input(MenuInput::Accept,false);
	Check(f.Actions().empty(),"moving tentative choice cancels an already-armed Accept");
	f.Press(MenuInput::Home); f.input.Input(MenuInput::Accept,true); f.input.PointerPart("choice",{},"o0"); f.input.Pointer(true); f.input.Pointer(false);
	Check(f.Actions().empty(),"popup pointer cannot steal an armed keyboard release"); f.input.Input(MenuInput::Accept,false);
	Check(f.Actions().size() == 1,"original popup input owner commits once");
}
static void PersistenceAndLifetime() {
	Fixture f; f.Accept("choice"); f.Press(MenuInput::End); auto snapshot = f.input.CaptureWidgets();
	Check(snapshot.version == 1 && snapshot.widgets.size() == 3 && snapshot.widgets.at("choice").firstVisible == 5,"widget snapshot records exact roles and bounded scroll only");
	f.Press(MenuInput::Accept); auto old = f.Actions()[0]; f.input.PointerPart("slider",.6); f.input.Pointer(true);
	for (unsigned mutation = 0; mutation < 5; ++mutation) {
		auto bad = snapshot;
		switch (mutation) {
			case 0: bad.version = 2; break;
			case 1: bad.widgets.erase("toggle"); break;
			case 2: bad.widgets["choice"].role = ControlRole::Toggle; break;
			case 3: bad.widgets["choice"].firstVisible = 6; break;
			case 4: bad.widgets["slider"].firstVisible = 1; break;
		}
		Check(!f.input.RestoreWidgets(bad,f.error) && f.View("slider").preview.has_value() && f.View("choice").proposalToken == old.proposalToken,"invalid widget snapshot preserves live drag and proposal atomically");
	}
	Check(f.input.RestoreWidgets(snapshot,f.error),"valid widget snapshot restores");
	Check(f.Actions().empty() && !f.View("slider").preview && !f.View("choice").popupOpen && !f.View("choice").pending && f.View("choice").firstVisible == 5,"snapshot excludes gestures popup proposals and queues");
	f.input.Pointer(false); Check(f.Actions().empty(),"snapshot preserves release quarantine");
	Check(!f.input.AcknowledgeProposal("choice",old.proposalToken,true),"restored instance rejects stale proposal acknowledgement");
	f.Accept("toggle"); auto first = f.Actions()[0];
	f.input.Reset(f.model); f.Refresh(); f.input.SetBounds(f.bounds); f.Accept("toggle"); auto second = f.Actions()[0];
	Check(second.proposalToken > first.proposalToken && !f.input.AcknowledgeProposal("toggle",first.proposalToken,true),"Reset cannot recycle tokens or let old acknowledgement clear a fresh proposal");
	Fixture other; other.Accept("toggle"); Check(other.Actions()[0].proposalToken > second.proposalToken,"tokens never alias across independent GUI owners");
	f.Press(MenuInput::Back); f.Actions();
	f.input.Focus("toggle"); auto semantics = f.input.Capture(); Check(f.input.Restore(semantics,f.error),"legacy interaction snapshot remains valid for persistent focus");
	Check(!f.View("toggle").pending,"legacy semantic restore clears transient value ownership");
}
static void QueueBound() {
	Fixture f; f.input.Focus("toggle");
	for (unsigned i = 0; i < 256; ++i) f.Press(MenuInput::Accept);
	const auto token = f.View("toggle").proposalToken; f.Press(MenuInput::Accept);
	Check(f.input.Overflowed() && f.View("toggle").proposalToken == token,"overflow cannot install an undispatched pending proposal");
	auto a = f.Actions(); Check(a.size() == 256 && !f.input.Overflowed(),"bounded queue retains all frozen preceding proposals");
	Check(!std::get<bool>(*a.back().proposal),"overflow did not mutate queued values");
}
int main() {
	ButtonAndToggle(); AtomicReadbacks(); SliderKeyboard(); SliderPointerAndCancel(); ChoiceNavigation(); ChoicePointerAndReadback(); PersistenceAndLifetime(); QueueBound();
	std::printf("PASS: value interaction (%u assertions)\n",checks); return 0;
}
