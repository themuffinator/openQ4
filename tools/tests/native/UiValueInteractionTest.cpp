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
// Left and right step a closed choice (section 13.5): through its eligible
// options from the latest proposal, wrapping at either end, without opening
// its list; a custom value steps to the first or the last. With nothing to
// step to, they move focus as before.
static void ChoiceStepping() {
	Fixture f; Check(f.input.Focus("choice"),"focus the choice");
	f.Press(MenuInput::Right); auto a = f.Actions();
	Check(a.size() == 1 && std::get<std::string>(*a[0].proposal) == "value0" && !f.View("choice").popupOpen,"Right steps a custom value to the first option");
	f.Press(MenuInput::Right); a = f.Actions();
	Check(a.size() == 1 && std::get<std::string>(*a[0].proposal) == "value2","Right skips a disabled option and chains from the pending proposal");
	f.Press(MenuInput::Left); f.Press(MenuInput::Left); a = f.Actions();
	Check(a.size() == 2 && std::get<std::string>(*a[0].proposal) == "value0" && std::get<std::string>(*a[1].proposal) == "value7","Left steps back and wraps to the last option");
	Check(f.input.Focused() == "choice" && std::get<std::string>(f.View("choice").accepted) == "custom","stepping keeps focus and never rewrites the accepted value");
	f.Press(MenuInput::Right); a = f.Actions();
	Check(a.size() == 1 && std::get<std::string>(*a[0].proposal) == "value0","Right wraps from the last option to the first");
	Fixture custom; Check(custom.input.Focus("choice"),"focus a fresh choice");
	custom.Press(MenuInput::Left); a = custom.Actions();
	Check(a.size() == 1 && std::get<std::string>(*a[0].proposal) == "value7","Left steps a custom value to the last option");
	// An open list keeps its own navigation: left and right step nothing.
	custom.Press(MenuInput::Accept); custom.Press(MenuInput::Right); custom.Press(MenuInput::Left);
	Check(custom.Actions().empty() && custom.View("choice").popupOpen,"an open list takes no stepping");
	custom.Press(MenuInput::Back);
	// Nothing to step to: one eligible option already chosen, or none.
	Fixture single; single.values["choice"] = {std::string("value3"),false,{false,false,false,true,false,false,false,false}}; single.Refresh();
	Check(single.input.Focus("choice"),"focus a choice with one option");
	single.Press(MenuInput::Right); single.Press(MenuInput::Left);
	Check(single.Actions().empty(),"a choice already on its only option proposes nothing");
	single.values["choice"].enabledOptions.assign(8,false); single.Refresh();
	single.Press(MenuInput::Right); Check(single.Actions().empty(),"a choice without eligible options proposes nothing");
	single.input.SetEnabled("choice",false); single.Press(MenuInput::Right); Check(single.Actions().empty(),"a disabled choice never steps");
}
// The most recent device sets the presentation (section 13.4): under the
// pointer's lead only the control under it is lit, and the focus waits for
// the next navigation input. A focus whose control turns ineligible resumes
// from where it stood.
static void FocusPresentation() {
	Fixture f; Check(f.input.Focus("button") && f.input.State("button") == ControlState::Focus,"the keyboard's lead shows the focus");
	f.input.SetPointerPresentation(true); f.input.Hover("toggle");
	Check(f.input.State("button") == ControlState::Default && f.input.State("toggle") == ControlState::Hover && f.input.Focused() == "button",
		"the pointer's lead lights only the control under it and keeps the focus");
	f.input.Hover(""); Check(f.input.State("button") == ControlState::Default,"a focused button rests while the pointer leads");
	Check(f.input.Focus("slider") && f.input.State("slider") == ControlState::Focus,"a value control keeps showing its focus");
	f.input.Focus("button"); f.input.SetPointerPresentation(false);
	Check(f.input.State("button") == ControlState::Focus,"the keyboard's next lead shows the focus again");
	f.input.SetPointerPresentation(true); f.input.Hover("button"); f.input.Pointer(true);
	Check(f.input.State("button") == ControlState::Pressed,"a press still shows");
	f.input.Pointer(false); Check(f.input.State("button") == ControlState::Hover && f.Actions().size() == 1,"a click activates and leaves hover");
	f.input.SetPointerPresentation(false); f.input.Hover("");
}
static void LostFocusResumes() {
	Fixture f; f.input.Focus("toggle"); f.input.SetEnabled("toggle",false);
	Check(f.input.Focused().empty(),"focus never stays on a disabled control");
	f.Press(MenuInput::Down); Check(f.input.Focused() == "slider","Down moves on from the lost control, not from the first");
	f.input.SetEnabled("toggle",true); f.input.Focus("toggle"); f.input.SetEnabled("toggle",false);
	f.Press(MenuInput::Up); Check(f.input.Focused() == "button","Up moves on from the lost control");
	f.input.SetEnabled("toggle",true); f.input.Focus("toggle"); f.input.SetEnabled("toggle",false); f.input.SetEnabled("toggle",true);
	f.Press(MenuInput::Down); Check(f.input.Focused() == "toggle","a lost control that can take focus again resumes it");
	f.input.SetEnabled("toggle",false); f.Press(MenuInput::Next); Check(f.input.Focused() == "slider","tab order resumes after the lost control");
	f.input.SetEnabled("toggle",true); f.input.Focus("toggle"); f.input.SetEnabled("toggle",false); f.input.Focus("choice");
	f.input.SetEnabled("toggle",true); f.input.Focus(""); f.Press(MenuInput::Down);
	Check(f.input.Focused() == "button","a focus set since forgets the lost control");
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
	Check(snapshot.version == 2 && snapshot.widgets.size() == 3 && snapshot.widgets.at("choice").firstVisible == 5,"widget snapshot records exact roles and bounded scroll");
	f.Press(MenuInput::Accept); auto old = f.Actions()[0]; f.input.PointerPart("slider",.6); f.input.Pointer(true);
	for (unsigned mutation = 0; mutation < 5; ++mutation) {
		auto bad = snapshot;
		switch (mutation) {
			case 0: bad.version = 3; break;
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
static void QueuedReadbackOwnership() {
	Fixture f;
	f.Accept("toggle"); f.Accept("toggle"); f.Accept("toggle");
	auto actions = f.Actions();
	Check(f.input.CanDispatchControlAction(actions[0]) && !f.input.CanDispatchControlAction(actions[1]),
		"queued steps require their predecessor's actual readback");
	for (const auto& action : actions) {
		Check(f.input.CanDispatchControlAction(action),"ordered queued toggle remains dispatchable");
		f.values["toggle"].value = *action.proposal; f.Refresh();
		f.input.AcknowledgeProposal("toggle",action.proposalToken,true);
	}
	Check(!f.input.CanDispatchControlAction(actions[0]),"completed proposal cannot dispatch again");
	f.values["toggle"].value = false; f.Refresh(); f.Accept("toggle");
	Check(!f.input.CanDispatchControlAction(actions[0]),"new proposal cannot revive a previous chain");
	const auto fresh = f.Actions().front();
	Check(f.input.CanDispatchControlAction(fresh),"fresh proposal after previous chain remains usable");
	f.values["toggle"].mixed = true; f.Refresh();
	Check(!f.input.CanDispatchControlAction(fresh),"external mixed state invalidates old toggle intent");
	f.input.AcknowledgeProposal("toggle",fresh.proposalToken,false); f.Accept("toggle");
	Check(f.input.CanDispatchControlAction(f.Actions().front()),"fresh mixed toggle uses current mixed state");
	Fixture slider;
	slider.input.Focus("slider"); slider.Press(MenuInput::Right); slider.Press(MenuInput::Right);
	actions = slider.Actions();
	Check(slider.input.CanDispatchControlAction(actions[0]),"slider starts from actual custom value");
	slider.values["slider"].value = 1.8; slider.Refresh();
	Check(!slider.input.CanDispatchControlAction(actions[0]) && !slider.input.CanDispatchControlAction(actions[1]),
		"external slider value invalidates queued steps");
	Fixture choice;
	choice.input.Focus("choice"); choice.Press(MenuInput::Right);
	actions = choice.Actions();
	Check(choice.input.CanDispatchControlAction(actions[0]),"current choice is dispatchable");
	choice.values["choice"].enabledOptions[0] = false; choice.Refresh();
	Check(!choice.input.CanDispatchControlAction(actions[0]),"option disabled after queue cannot dispatch");
	choice.values["choice"].enabledOptions[0] = true; choice.values["choice"].optionCount = 0; choice.Refresh();
	Check(!choice.input.CanDispatchControlAction(actions[0]),"removed counted option cannot dispatch");
	choice.values["choice"].optionCount.reset(); choice.values["choice"].value = std::string("value5"); choice.Refresh();
	Check(!choice.input.CanDispatchControlAction(actions[0]),"external choice value invalidates old selection");
}
static void QueueBound() {
	Fixture f; f.input.Focus("toggle");
	for (unsigned i = 0; i < 256; ++i) f.Press(MenuInput::Accept);
	const auto token = f.View("toggle").proposalToken; f.Press(MenuInput::Accept);
	Check(f.input.Overflowed() && f.View("toggle").proposalToken == token,"overflow cannot install an undispatched pending proposal");
	auto a = f.Actions(); Check(a.size() == 256 && !f.input.Overflowed(),"bounded queue retains all frozen preceding proposals");
	Check(!std::get<bool>(*a.back().proposal),"overflow did not mutate queued values");
}
static void ButtonContextOwnership() {
	Fixture f;
	Expression context; context.type = 2; context.state = "subject";
	f.model.root.children[0].control->context = context;
	f.input.Reset(f.model); f.input.SetBounds(f.bounds);
	Check(!f.input.CanActivate("button"),"context button waits for authoritative subject");
	f.values["button"].value = std::string("connection-a"); f.Refresh();
	f.Accept("button"); auto queued = f.Actions().front();
	Check(queued.contextToken && f.input.CanDispatchControlAction(queued),"current button subject can dispatch");
	f.Refresh(); Check(f.input.CanDispatchControlAction(queued),"unchanged subject retains its activation");
	f.values["button"].value = std::string("connection-b"); f.Refresh();
	Check(!f.input.CanDispatchControlAction(queued),"queued action cannot follow a replacement subject");
	f.values["button"].value = std::string("connection-a"); f.Refresh();
	Check(!f.input.CanDispatchControlAction(queued),"returning subject cannot revive old work");
	f.input.Focus("button"); f.input.Input(MenuInput::Accept,true);
	f.values["button"].value = std::string("connection-b"); f.Refresh();
	f.input.Input(MenuInput::Accept,false);
	Check(f.Actions().empty(),"held semantic press is canceled when its subject changes");
	f.input.Hover("button"); f.input.Pointer(true);
	f.values["button"].value = std::string("connection-c"); f.Refresh();
	f.input.Pointer(false); Check(f.Actions().empty(),"held pointer press cannot select a replacement row");
	f.Accept("button"); auto fresh = f.Actions().front();
	Check(f.input.CanDispatchControlAction(fresh),"a fresh press can select the replacement subject");
	auto forged = fresh; forged.contextToken = 0;
	Check(!f.input.CanDispatchControlAction(forged),"missing context token fails closed");
	auto invalid = f.values; invalid["button"].value = 4.0;
	Check(!f.input.SetReadbacks(invalid,f.error) && f.input.CanDispatchControlAction(fresh),"invalid context readback is atomic");
	invalid = f.values; invalid.erase("button");
	Check(!f.input.SetReadbacks(invalid,f.error),"missing context readback is rejected");
	const auto snapshot = f.input.Capture();
	Check(f.input.Restore(snapshot,f.error) && !f.input.CanDispatchControlAction(fresh),"restore cannot carry queued context authority");
}

static void ChoiceContextOwnership() {
	Fixture f; Expression context; context.type=2; context.state="catalog";
	f.model.root.children[3].control->context=context;
	f.input.Reset(f.model); f.input.SetBounds(f.bounds);
	f.values["choice"].context="0,1:4097"; f.Refresh();
	f.input.Focus("choice"); f.Press(MenuInput::Right); const auto queued=f.Actions().front();
	Check(queued.contextToken && f.input.CanDispatchControlAction(queued),"choice proposal carries its catalog lifetime");
	f.Refresh(); Check(f.input.CanDispatchControlAction(queued),"unchanged catalog keeps queued choice");
	f.values["choice"].context="0,1:8193"; f.Refresh();
	Check(!f.input.CanDispatchControlAction(queued) && !f.View("choice").pending,"replacement cancels a queued choice despite identical labels and value");
	f.values["choice"].context="0,1:4097"; f.Refresh();
	Check(!f.input.CanDispatchControlAction(queued),"catalog ABA cannot revive an old choice");
	f.Accept("choice"); f.input.Input(MenuInput::Accept,true);
	f.values["choice"].context="0,1:12289"; f.Refresh(); f.input.Input(MenuInput::Accept,false);
	Check(f.Actions().empty() && !f.View("choice").popupOpen,"replaced catalog closes popup and cancels held semantic selection");
	f.Accept("choice"); f.input.PointerPart("choice",{},"o0"); f.input.Pointer(true);
	f.values["choice"].context="0,1:16385"; f.Refresh(); f.input.Pointer(false);
	Check(f.Actions().empty() && !f.View("choice").popupOpen,"replaced catalog cancels held pointer selection");
	f.input.Focus("choice"); f.Press(MenuInput::Right); const auto fresh=f.Actions().front();
	Check(f.input.CanDispatchControlAction(fresh),"fresh choice targets the current catalog");
	auto bad=f.values;bad["choice"].context.reset();
	Check(!f.input.SetReadbacks(bad,f.error) && f.input.CanDispatchControlAction(fresh),"missing catalog fails atomically");
	bad=f.values;bad["choice"].context=std::string("bad\0catalog",11);
	Check(!f.input.SetReadbacks(bad,f.error),"invalid catalog text fails atomically");
	const auto snapshot=f.input.Capture();
	Check(f.input.Restore(snapshot,f.error) && !f.input.CanDispatchControlAction(fresh),"restoring a view cannot restore choice authority");
}

int main() {
	ButtonAndToggle(); AtomicReadbacks(); SliderKeyboard(); SliderPointerAndCancel(); ChoiceNavigation(); ChoiceStepping(); FocusPresentation(); LostFocusResumes();
	ChoicePointerAndReadback(); PersistenceAndLifetime(); QueuedReadbackOwnership(); ButtonContextOwnership(); ChoiceContextOwnership(); QueueBound();
	std::printf("PASS: value interaction (%u assertions)\n",checks); return 0;
}
