// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Input.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace openq4::ui;
static unsigned checks = 0;
static void Check(bool condition, const char* message) {
	++checks;
	if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static Node Button(const std::string& id) {
	Node node; node.id = id; node.type = "group";
	Control control; control.label = "#str_test"; control.action = id + ".activate";
	for (auto state : {ControlState::Default,ControlState::Hover,ControlState::Focus,ControlState::Pressed,ControlState::Disabled})
		control.states[state] = id + ".feedback";
	node.control = std::move(control); return node;
}
static Node Modal(const std::string& id, const std::string& initial, const std::string& back,
	std::vector<Node> children) {
	Node node; node.id = id; node.type = "group"; node.children = std::move(children);
	node.modal = ModalSpec{initial,back}; return node;
}
struct Fixture {
	DocumentModel model;
	Interaction interaction;
	std::string error;
	Fixture() {
		model.id = "modal-interaction"; model.root.id = "root"; model.root.type = "group";
		Node manual; manual.id = "manual"; manual.type = "group"; manual.children = {Button("manual.first")};
		model.root.children = {
			Button("page.first"), Button("page.opener"),
			Modal("a","a.safe","close-a",{Button("a.other"),Button("a.safe"),
				Modal("c","c.safe","close-c",{Button("c.other"),Button("c.safe")})}),
			Modal("b","b.safe","",{Button("b.other"),Button("b.safe")}), manual};
		interaction.Reset(model);
		Check(interaction.HasAuthoredModals(),"authored scopes are identified from the canonical model");
		Sync({});
		Check(interaction.Focus("page.opener"),"focus the actual page opener");
		interaction.TakeFeedback();
	}
	std::map<std::string,ControlBounds> Bounds(const std::vector<std::string>& roots, bool page = true, bool manual = false) const {
		const auto visible = [&](const char* id) { return std::find(roots.begin(),roots.end(),id) != roots.end(); };
		std::map<std::string,ControlBounds> bounds;
		unsigned row = 0;
		for (const auto& [id,show] : std::vector<std::pair<std::string,bool>>{
			{"page.first",page},{"page.opener",page},{"a.other",visible("a")},{"a.safe",visible("a")},
			{"c.other",visible("a") && visible("c")},{"c.safe",visible("a") && visible("c")},
			{"b.other",visible("b")},{"b.safe",visible("b")},{"manual.first",manual}})
			bounds[id] = {0,float(row++ * 40),160,32,show};
		return bounds;
	}
	void Sync(std::initializer_list<std::string> roots, bool fresh = true, bool page = true) {
		std::vector<std::string> chain(roots);
		Check(interaction.SyncAuthoredModals(chain,error),"valid authored chain synchronizes");
		interaction.SetBounds(Bounds(chain,page),fresh);
	}
	void Key(MenuInput key) { interaction.Input(key,true); interaction.Input(key,false); }
	std::vector<ControlAction> Actions() { return interaction.TakeActions(); }
	ControlAction Back() {
		Key(MenuInput::Back); auto actions = Actions();
		Check(actions.size() == 1 && actions.front().kind == ControlAction::Kind::Back,"one paired Back creates one scoped record");
		return actions.front();
	}
};
static void PreviousFocusAndFreshLayout() {
	Fixture f;
	Check(f.interaction.SyncAuthoredModals({"a"},f.error),"open captures previous focus before bindings invalidate it");
	auto opening = f.interaction.Capture();
	Check(opening.modals.size() == 1 && opening.modals.front().authored && opening.modals.front().restore == "page.opener",
		"modal records exact opener rather than the first page control");
	Check(opening.focusPending && opening.pendingFocus == "a.safe","opening retains authored initial target until fresh layout");
	f.interaction.SetEnabled("page.opener",false);
	f.interaction.SetBounds(f.Bounds({},false),false);
	Check(f.interaction.Capture().focusPending && f.interaction.Capture().pendingFocus == "a.safe",
		"stale hidden bounds cannot spend the initial focus target");
	f.interaction.SetBounds(f.Bounds({"a"},false));
	Check(f.interaction.Focused() == "a.safe" && f.interaction.Modal() == "a","fresh layout resolves authored default before input or rendering");
	Check(!f.interaction.CanActivate("page.first") && !f.interaction.AllowsNode("page.first"),"modal contains both control and scrolling eligibility");
	Check(f.interaction.AllowsNode("a") && f.interaction.AllowsNode("a.safe"),"active panel and descendants remain permitted");
	Check(f.interaction.SyncAuthoredModals({},f.error),"close begins restoration before incoming eligibility changes");
	f.interaction.SetBounds(f.Bounds({"a"},false),false);
	Check(f.interaction.Capture().focusPending && f.interaction.Capture().pendingFocus == "page.opener",
		"close preserves the exact restore target through stale outgoing geometry");
	f.interaction.SetEnabled("page.opener",true);
	f.interaction.SetBounds(f.Bounds({}));
	Check(f.interaction.Focused() == "page.opener" && f.interaction.Modal().empty(),"close restores exact now-eligible opener");
	Check(f.Actions().empty(),"focus transfer never dispatches application actions");
}
static void DefaultEligibilityAndAsyncChanges() {
	Fixture f;
	f.interaction.SetEnabled("a.safe",false); f.interaction.SetEnabled("a.other",false);
	f.Sync({"a"});
	Check(f.interaction.Modal() == "a" && f.interaction.Focused().empty(),"all-disabled pending modal contains focus without leaking to page");
	Check(!f.interaction.CanActivate("page.first"),"empty modal focus still excludes background activation");
	f.interaction.SetEnabled("a.safe",true); f.interaction.SetBounds(f.Bounds({"a"}));
	Check(f.interaction.Focused() == "a.safe","later eligible default receives focus without another opening event");
	f.interaction.SetEnabled("a.other",true); f.interaction.SetBounds(f.Bounds({"a"}));
	Check(f.interaction.Focused() == "a.safe","newly enabled Keep-like alternative does not steal Revert-like focus");
	f.interaction.SetEnabled("a.safe",false); f.interaction.SetBounds(f.Bounds({"a"}));
	Check(f.interaction.Focused() == "a.other","disabled current action falls back inside the modal");
	f.interaction.SetEnabled("a.safe",true); f.interaction.SetBounds(f.Bounds({"a"}));
	Check(f.interaction.Focused() == "a.other","eligible current focus survives default re-enablement");
	f.interaction.SetEnabled("page.opener",false); f.Sync({});
	Check(f.interaction.Focused() == "page.first","ineligible saved opener falls back to eligible page control");
}
static void NestedAndDisjointScopes() {
	Fixture f; f.Sync({"a"}); Check(f.interaction.Focus("a.other"),"select nested modal opener");
	f.Sync({"a","c"});
	Check(f.interaction.Focused() == "c.safe" && f.interaction.Modal() == "c","nested modal owns default focus");
	Check(!f.interaction.CanActivate("a.other") && !f.interaction.AllowsNode("a"),"nested scope excludes its still-visible parent controls and scrolling");
	auto nested = f.interaction.Capture();
	Check(nested.modals.size() == 2 && nested.modals[0].restore == "page.opener" && nested.modals[1].restore == "a.other",
		"nested chain preserves each distinct return focus");
	f.Sync({"a"}); Check(f.interaction.Focused() == "a.other","inner close restores outer selection");
	f.Sync({}); Check(f.interaction.Focused() == "page.opener","outer close restores page selection");
	f.Sync({"a"}); f.Sync({"a","c"}); f.Sync({});
	Check(f.interaction.Focused() == "page.opener","hiding the entire nested chain restores the outermost page opener");
	for (const auto& bad : std::vector<std::vector<std::string>>{{"a","b"},{"a","a"},{"c","a"},{"missing"},{"page.first"}}) {
		Check(!f.interaction.SyncAuthoredModals(bad,f.error) && !f.error.empty(),"disjoint, duplicate, reversed or unknown scopes fail closed");
		f.interaction.SetBounds(f.Bounds({"a","b","c"}));
		Check(!f.interaction.AllowsNode("root") && !f.interaction.AllowsNode("a.safe") && !f.interaction.CanActivate("page.first"),
			"invalid chain blocks every background and modal target");
		f.Key(MenuInput::Next); f.Key(MenuInput::Accept); f.Key(MenuInput::Back);
		f.interaction.PointerPart("a.safe"); f.interaction.Pointer(true); f.interaction.Pointer(false);
		Check(f.Actions().empty(),"invalid chain cannot enqueue navigation-triggered, Back or pointer operations");
		f.Sync({});
		Check(f.interaction.CanActivate("page.first"),"valid synchronization explicitly recovers fail-closed input");
		Check(f.interaction.Focus("page.opener"),"restore opener after invalid-chain recovery");
	}
}
static void HeldSourcesAndRepeats() {
	for (const auto key : {MenuInput::Accept,MenuInput::Back,MenuInput::Next,MenuInput::Previous,
		MenuInput::Up,MenuInput::Down,MenuInput::Left,MenuInput::Right,MenuInput::Home,MenuInput::End,MenuInput::PageUp,MenuInput::PageDown}) {
		Fixture f; f.interaction.Input(key,true); f.Actions(); f.Sync({"a"});
		const auto focused = f.interaction.Focused();
		f.interaction.Input(key,true); f.interaction.Input(key,true);
		Check(f.interaction.Focused() == focused && f.Actions().empty(),"held menu action cannot repeat or rearm in new modal");
		f.interaction.Input(key,false);
		Check(f.Actions().empty(),"old menu release cannot activate or Back the new modal");
		f.Key(MenuInput::Accept);
		auto actions = f.Actions();
		Check(actions.size() == 1 && actions.front().node == "a.safe","fresh matched Accept works after old source release");
	}
	Fixture pointer; pointer.interaction.PointerPart("page.opener"); pointer.interaction.Pointer(true); pointer.Sync({"a"});
	pointer.interaction.PointerPart("a.safe"); pointer.interaction.Pointer(true); pointer.interaction.Pointer(false);
	Check(pointer.Actions().empty(),"old pointer hold/release cannot click the new modal");
	pointer.interaction.Pointer(true); pointer.interaction.Pointer(false);
	Check(pointer.Actions().size() == 1,"fresh pointer click after release activates once");
	Fixture repeated; Input sources;
	auto route = [&] { for (const auto& event : sources.Take()) {
		if (event.kind == RoutedInput::Kind::Menu) repeated.interaction.Input(event.menu,event.down);
		else if (event.kind == RoutedInput::Kind::PointerButton) repeated.interaction.Pointer(event.down);
		else repeated.interaction.Cancel();
	} };
	sources.Menu(11,MenuInput::Down,true,false,0); sources.Menu(12,MenuInput::Down,true,false,.01); route();
	repeated.Sync({"a"});
	sources.Advance(.34); route();
	Check(repeated.interaction.Focused() == "a.safe","real repeat aggregator cannot move modal focus from an old held source");
	sources.Menu(11,MenuInput::Down,false,false,.35); route(); sources.Advance(.5); route();
	Check(repeated.interaction.Focused() == "a.safe","release of one physical navigation source does not release quarantine");
	sources.Menu(12,MenuInput::Down,false,false,.51); route();
	sources.Menu(13,MenuInput::Previous,true,false,.52); route();
	Check(repeated.interaction.Focused() == "a.other","fresh physical navigation source can act after all old sources release");
	sources.Menu(13,MenuInput::Previous,false,false,.53); route();
	Check(repeated.Actions().empty(),"navigation handoff produces no application action");
}
static void BackIdentityAndNoImplicitDismiss() {
	Fixture f; auto pageBack = f.Back();
	Check(f.interaction.CanDispatchModalBack(pageBack),"current page Back is valid");
	f.Sync({"a"}); Check(!f.interaction.CanDispatchModalBack(pageBack),"queued page Back cannot target a newly opened modal");
	auto first = f.Back();
	Check(first.event == "close-a" && first.node == "a" && first.modalToken && f.interaction.CanDispatchModalBack(first),
		"authored Back captures named program, exact scope and generation");
	Check(!f.interaction.PopModal() && f.interaction.Modal() == "a","manual Pop cannot desynchronize authored modal visibility");
	f.Sync({}); f.Sync({"a"});
	Check(!f.interaction.CanDispatchModalBack(first),"same-root reopen never reuses old Back identity");
	auto second = f.Back(); Check(second.modalToken != first.modalToken,"reopened modal has fresh identity");
	auto snapshot = f.interaction.Capture();
	Check(f.interaction.Restore(snapshot,f.error),"restore valid authored modal snapshot");
	f.interaction.SetBounds(f.Bounds({"a"}));
	Check(!f.interaction.CanDispatchModalBack(second),"snapshot restore invalidates in-flight Back even with unchanged modal root");
	auto restored = f.Back(); Check(f.interaction.CanDispatchModalBack(restored),"restored scope accepts newly captured Back");
	auto wrong = restored; wrong.document = "other";
	Check(!f.interaction.CanDispatchModalBack(wrong),"Back from another document is rejected");
	wrong = restored; wrong.node = "c";
	Check(!f.interaction.CanDispatchModalBack(wrong),"Back cannot forge another scope");
	wrong = restored; wrong.event = "different-program";
	Check(!f.interaction.CanDispatchModalBack(wrong),"Back cannot forge another authored program");
	f.Sync({}); f.Sync({"b"}); f.Key(MenuInput::Back);
	Check(f.Actions().empty() && f.interaction.Modal() == "b","authored modal without Back program consumes Back without implicit dismissal");
}
static void IndependentNavigationPulse() {
	Fixture f;
	Check(f.interaction.Focus("page.first"),"begin a real held navigation source before a wheel pulse");
	f.interaction.Input(MenuInput::Down,true);
	f.interaction.NavigationPulse(MenuInput::Down);
	f.Sync({"a"});
	Check(f.interaction.Focus("a.other"),"position modal focus where a stale Down repeat would move it");
	f.interaction.Input(MenuInput::Down,true);
	Check(f.interaction.Focused() == "a.other","pre-modal wheel pulse does not erase the real held direction quarantine");
	f.interaction.NavigationPulse(MenuInput::Down);
	Check(f.interaction.Focused() == "a.safe","independent wheel pulse can navigate while old physical Down remains quarantined");
	Check(f.interaction.Focus("a.other"),"recheck physical hold after independent wheel navigation");
	f.interaction.Input(MenuInput::Down,true);
	Check(f.interaction.Focused() == "a.other","wheel pulse preserves the blocked real source after it finishes");
	f.interaction.Input(MenuInput::Down,false); f.interaction.Input(MenuInput::Down,true);
	Check(f.interaction.Focused() == "a.safe","fresh physical Down acts after the old source releases");
	f.interaction.Input(MenuInput::Down,false);
	f.interaction.NavigationPulse(MenuInput::Accept); f.interaction.NavigationPulse(MenuInput::Back);
	Check(f.Actions().empty(),"navigation pulses cannot synthesize activation or Back");
}
static void PendingSnapshotRestoration() {
	Fixture f;
	Check(f.interaction.SyncAuthoredModals({"a"},f.error),"open before first layout for snapshot");
	auto saved = f.interaction.Capture();
	Check(saved.focusPending && saved.pendingFocus == "a.safe" && saved.modals.front().restore == "page.opener",
		"snapshot retains unresolved default and return target");
	Fixture restored;
	restored.interaction.Input(MenuInput::Accept,true);
	Check(restored.interaction.Restore(saved,restored.error),"fresh instance restores pending modal snapshot");
	restored.interaction.SetBounds(restored.Bounds({}),false);
	Check(restored.interaction.Capture().pendingFocus == "a.safe","stale resource bounds preserve saved pending default");
	restored.interaction.SetBounds(restored.Bounds({"a"}));
	Check(restored.interaction.Focused() == "a.safe","first fresh restored layout selects correct default");
	restored.interaction.Input(MenuInput::Accept,false);
	Check(restored.Actions().empty(),"receiving-instance held Accept cannot activate restored modal");
	restored.Sync({}); Check(restored.interaction.Focused() == "page.opener","restored modal close returns to saved opener");
	Fixture heldNavigation;
	heldNavigation.interaction.Input(MenuInput::Next,true);
	Check(heldNavigation.interaction.Restore(saved,heldNavigation.error),"restore while receiving instance holds navigation");
	heldNavigation.interaction.SetBounds(heldNavigation.Bounds({"a"}));
	heldNavigation.interaction.Input(MenuInput::Next,true);
	Check(heldNavigation.interaction.Focused() == "a.safe","restore quarantines old navigation repeat through first layout");
	heldNavigation.interaction.Input(MenuInput::Next,false);
	f.interaction.SetBounds(f.Bounds({"a"}));
	Check(f.interaction.SyncAuthoredModals({},f.error),"capture close before new page layout");
	saved = f.interaction.Capture();
	Check(saved.focusPending && saved.pendingFocus == "page.opener" && saved.modals.empty(),"closing snapshot retains pending exact page focus");
	Check(restored.interaction.Restore(saved,restored.error),"restore pending modal close snapshot");
	restored.interaction.SetBounds(restored.Bounds({"a"},false),false);
	restored.interaction.SetBounds(restored.Bounds({}));
	Check(restored.interaction.Focused() == "page.opener" && restored.Actions().empty(),"restored close cannot spend target on stale geometry or replay actions");
	Fixture rapid;
	rapid.Sync({"a"},false); rapid.Sync({},false);
	Check(rapid.interaction.Capture().focusPending && rapid.interaction.Capture().pendingFocus == "page.opener",
		"open and close before fresh layout retain original return focus");
	rapid.interaction.SetBounds(rapid.Bounds({}));
	Check(rapid.interaction.Focused() == "page.opener","rapid invisible modal reversal restores exact opener without activation");
}
static void ActivationOwnership() {
	Fixture f;
	f.Key(MenuInput::Accept); auto page = f.Actions();
	Check(page.size() == 1 && f.interaction.CanDispatchControlAction(page.front()),"current page activation can be dispatched");
	f.Sync({"a"});
	Check(!f.interaction.CanDispatchControlAction(page.front()),"taken page activation cannot cross into new modal ownership");
	f.Key(MenuInput::Accept); f.Key(MenuInput::Accept); auto taken = f.Actions();
	Check(taken.size() == 2 && f.interaction.CanDispatchControlAction(taken[0]) && f.interaction.CanDispatchControlAction(taken[1]),
		"separately queued current modal activations carry dispatchable identities");
	f.Sync({}); f.Sync({"a"});
	Check(f.interaction.CanActivate("a.safe"),"reopened control intentionally has the same eligible stable ID");
	Check(!f.interaction.CanDispatchControlAction(taken[0]) && !f.interaction.CanDispatchControlAction(taken[1]),
		"old adapter-local activation vector cannot dispatch after same-root reopen");
	f.Key(MenuInput::Accept); auto fresh = f.Actions();
	Check(fresh.size() == 1 && f.interaction.CanDispatchControlAction(fresh.front()),"new modal lifetime dispatches fresh activation");
	Check(fresh.front().modalToken != taken.front().modalToken,"activation scope identity is never reused across reopen");
	const auto snapshot = f.interaction.Capture();
	Check(f.interaction.Restore(snapshot,f.error),"restore while adapter holds a completed raw control activation");
	f.interaction.SetBounds(f.Bounds({"a"}));
	Check(!f.interaction.CanDispatchControlAction(fresh.front()),"restore invalidates taken raw activation despite unchanged document/control");
	f.Key(MenuInput::Accept); f.Sync({}); f.Sync({"a"});
	for (const auto& queued : f.Actions())
		Check(!f.interaction.CanDispatchControlAction(queued),"core-queued activation is canceled or rejects after a modal lifetime change");
	Check(f.Actions().empty(),"ownership checks never create replacement actions");
}
static void InvalidLayoutAndEmptyRestoration() {
	Fixture f; f.Sync({"a"});
	f.Key(MenuInput::Accept); const auto taken = f.Actions();
	Check(taken.size() == 1,"capture an activation before the viewport disappears");
	f.interaction.Input(MenuInput::Down,true);
	Check(f.interaction.Focus("a.safe"),"retain the safe non-first modal selection before invalid geometry");
	f.interaction.InvalidateLayout();
	auto pending = f.interaction.Capture();
	Check(pending.focus.empty() && pending.focusPending && pending.pendingFocus == "a.safe",
		"invalid viewport preserves exact selection as pending rather than choosing the first action");
	Check(!f.interaction.CanActivate("a.safe") && !f.interaction.CanDispatchControlAction(taken.front()),
		"invalid layout prevents activation and invalidates previously taken action identity");
	Check(!f.interaction.Focus("a.other"),"stale geometry cannot replace the pending safe target");
	f.interaction.SetBounds(f.Bounds({"a"}),false);
	f.interaction.PointerPart("a.other"); f.interaction.Pointer(true); f.interaction.Pointer(false);
	f.Key(MenuInput::Accept);
	Check(f.Actions().empty() && f.interaction.Focused().empty(),
		"stale projected bounds cannot enable pointer or Accept before a fresh layout");
	f.interaction.InvalidateLayout();
	Check(f.interaction.Capture().pendingFocus == "a.safe","repeated invalid frames retain the original exact target");
	f.interaction.SetBounds(f.Bounds({"a"}));
	Check(f.interaction.Focused() == "a.safe","first valid layout restores safe selection instead of earlier Keep-like control");
	f.interaction.Input(MenuInput::Down,true);
	Check(f.interaction.Focused() == "a.safe","old held navigation remains quarantined after geometry recovers");
	f.interaction.Input(MenuInput::Down,false);
	f.Key(MenuInput::Previous);
	Check(f.interaction.Focused() == "a.other","fresh navigation works after old direction releases");
	Check(!f.interaction.CanDispatchControlAction(taken.front()),"recovered geometry cannot revive an old activation token");
	Check(f.interaction.Focus("a.safe"),"select safe action before disabling it during an unavailable viewport");
	f.interaction.InvalidateLayout(); f.interaction.SetEnabled("a.safe",false);
	f.interaction.SetBounds(f.Bounds({"a"}),false);
	Check(f.interaction.Capture().pendingFocus == "a.safe","disabled pending selection is not spent on stale geometry");
	f.interaction.SetBounds(f.Bounds({"a"}));
	Check(f.interaction.Focused() == "a.other","fresh geometry falls back within the modal when saved selection is truly disabled");
	Fixture opening; opening.Sync({"a"},false); opening.interaction.InvalidateLayout();
	Check(opening.interaction.Capture().pendingFocus == "a.safe","invalid viewport also preserves an unresolved initial focus target");
	opening.interaction.SetBounds(opening.Bounds({"a"}));
	Check(opening.interaction.Focused() == "a.safe","initial target resolves after the first available viewport");
	Fixture empty;
	Check(empty.interaction.Focus(""),"intentionally leave a page without keyboard focus");
	const auto snapshot = empty.interaction.Capture();
	Check(!snapshot.focusPending && snapshot.focus.empty() && snapshot.modals.empty(),"empty page focus is a valid persistent state");
	Fixture restored;
	Check(restored.interaction.Restore(snapshot,restored.error),"restore intentionally empty focus into an instance with existing focus");
	Check(!restored.interaction.Capture().focusPending && restored.interaction.Focused().empty(),
		"authored modal capability alone does not invent a pending page focus");
	restored.interaction.SetBounds(restored.Bounds({}));
	Check(restored.interaction.Focused().empty() && restored.Actions().empty(),"fresh restored page preserves intentionally empty focus without actions");
	restored.interaction.InvalidateLayout(); restored.interaction.SetBounds(restored.Bounds({}));
	Check(restored.interaction.Focused().empty() && !restored.interaction.Capture().focusPending,
		"invalid and recovered page geometry do not invent a keyboard focus selection");
}
static void ManualScopeCompatibility() {
	Fixture f;
	f.interaction.SetBounds(f.Bounds({},true,true));
	Check(f.interaction.PushModal("manual"),"manual scope still opens");
	Check(f.interaction.Focused() == "manual.first","manual scope retains first-control behavior");
	auto back = f.Back();
	Check(back.event.empty() && f.interaction.CanDispatchModalBack(back),"manual Back remains a scoped empty-event operation");
	Check(f.interaction.PopModal(),"manual scope still pops explicitly");
	Check(f.interaction.Focused() == "page.opener" && !f.interaction.CanDispatchModalBack(back),"manual Pop restores focus and invalidates old Back");
}
int main() {
	PreviousFocusAndFreshLayout(); DefaultEligibilityAndAsyncChanges(); NestedAndDisjointScopes();
	HeldSourcesAndRepeats(); IndependentNavigationPulse(); BackIdentityAndNoImplicitDismiss(); PendingSnapshotRestoration(); ActivationOwnership();
	InvalidLayoutAndEmptyRestoration(); ManualScopeCompatibility();
	std::printf("UiModalInteractionTest: %u checks passed (production Interaction/Input, no devices or rendering).\n",checks);
}
