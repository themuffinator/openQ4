// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Interaction.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>

using namespace openq4::ui;
static unsigned checks;
static void Check(bool value, const char* message) {
	++checks; if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
struct Fixture {
	DocumentModel model; Interaction input; std::string error;
	std::map<std::string,ControlBounds> bounds;
	std::map<std::string,ControlReadback> readbacks;
	explicit Fixture(unsigned count=2, std::size_t maxBytes=64) {
		model.id="numbers"; model.root.id="root";
		for (unsigned i=0;i<count;++i) {
			const std::string id="n"+std::to_string(i);
			Node node; node.id=id; Control control; control.role=ControlRole::Number; control.action="edit";
			Expression expression; expression.type=0; control.value=expression;
			NumberSpec spec; spec.minimum=.5; spec.maximum=2; spec.maxBytes=maxBytes; control.widget=spec;
			for (const auto state : {ControlState::Default,ControlState::Hover,ControlState::Focus,ControlState::Pressed,ControlState::Disabled}) control.states[state]="feedback";
			node.control=control; model.root.children.push_back(node); bounds[id]={float(i*130),0,120,30,true}; readbacks[id]={1.0,false,{}};
		}
		input.Reset(model); Check(input.SetReadbacks(readbacks,error),"initial readbacks"); input.SetBounds(bounds);
	}
	NumberEditView View(const std::string& id="n0") const {
		const auto view=input.Widget(id); Check(view && view->number,"local number draft exists"); return *view->number;
	}
	NumberEditIdentity Id(const std::string& id="n0") const { return View(id).identity; }
	void Begin(const std::string& id="n0") { Check(input.Focus(id) && input.BeginNumberEdit(id,error),"begin or resume focused number"); }
	void Text(std::string_view value, const std::string& id="n0") {
		Check(input.SetNumberSelection(id,Id(id),0,View(id).state.text.size(),error),"select local buffer");
		Check(input.ReplaceNumberSelection(id,Id(id),value,error),"replace local buffer");
	}
	void Read(double value, const std::string& id="n0") { readbacks[id].value=value; Check(input.SetReadbacks(readbacks,error),"fresh authoritative readback"); }
	ValueWidgetSnapshot Capture() const { ValueWidgetSnapshot out; std::string error; Check(input.CaptureWidgets(out,error),"checked snapshot capture"); return out; }
	ControlAction Commit() { Check(input.CommitNumberEdit("n0",Id(),error),"explicit number proposal"); auto actions=input.TakeActions(); Check(actions.size()==1,"one queued proposal"); return actions[0]; }
};

static NumberDraftSummary Query(Fixture& f) {
	NumberDraftSummary out; Check(f.input.QueryNumberDrafts(out,f.error),"exact local draft query"); return out;
}
static void Stale(Fixture& f,const NumberDraftBarrier& old) {
	const auto current=Query(f); const auto focus=f.input.Focused();
	Check(current.barrier!=old,"relevant mutation changes barrier");
	Check(!f.input.DiscardNumberDrafts(old,f.error) && !f.error.empty(),"stale discard rejected");
	Check(Query(f).barrier==current.barrier && f.input.Focused()==focus,"stale discard has no partial effects");
	Check(!f.input.FocusNumberDraft(old,"n0",f.error),"stale focus rejected");
	Check(Query(f).barrier==current.barrier && f.input.Focused()==focus,"stale focus has no partial effects");
}
static void InventoryAndInactive() {
	Fixture f; auto empty=Query(f); Check(empty.blocking.empty() && empty.barrier.editors.empty(),"no local buffers means no blockers");
	f.Begin(); auto clean=Query(f);
	Check(clean.blocking.empty() && clean.barrier.editors.size()==1,"clean active editor inventoried but not blocking");
	f.Text("-"); auto first=Query(f);
	Check(first.blocking.size()==1 && first.blocking[0].control=="n0" && first.blocking[0].dirty &&
		first.blocking[0].status==TextNumberStatus::Incomplete && first.blocking[0].active && !first.blocking[0].nativeUnsettled,"invalid active buffer is a local blocker only");
	f.Begin("n1"); Stale(f,first.barrier); f.Text("1.75","n1");
	const auto active=Query(f).barrier; f.input.Cancel(); Stale(f,active); auto detached=Query(f);
	Check(detached.blocking.size()==2 && detached.blocking[0].control=="n0" && detached.blocking[1].control=="n1" &&
		!detached.blocking[0].active && !detached.blocking[1].active,"source-ordered detached editors both block");
	for(unsigned i=0;i<100;++i) {
		f.input.Cancel(); f.input.SetBounds(f.bounds); f.Read(1.0); f.input.TakeFeedback();
		Check(Query(f).barrier==detached.barrier,"unchanged inactive drafts have stable barriers across refresh");
	}
	const auto saved=f.Capture(); Check(Query(f).barrier==detached.barrier,"capture alone preserves barrier");
	Check(f.input.FocusNumberDraft(detached.barrier,"n0",f.error) && f.View().active && f.View().state.text=="-","focus resumes invalid text without commit");
	Check(f.View("n1").state.text=="1.75" && f.input.TakeActions().empty(),"focus leaves peer draft and host actions untouched");
	Stale(f,detached.barrier); f.input.Cancel(); auto all=Query(f);
	Check(f.input.DiscardNumberDrafts(all.barrier,f.error),"one exact barrier discards all local editors");
	Check(!f.input.Widget("n0")->number && !f.input.Widget("n1")->number && Query(f).blocking.empty(),"discard is complete");
	Check(std::get<double>(f.input.Widget("n0")->accepted)==1 && std::get<double>(f.input.Widget("n1")->accepted)==1,"discard never writes accepted readback");
	Stale(f,empty.barrier); Check(f.input.RestoreWidgets(saved,f.error),"restore saved inactive editors");
	Stale(f,detached.barrier);
}
static void MutationBarriers() {
	Fixture f; f.Begin(); f.Text("1.25");
	auto stamp=Query(f).barrier;
	Check(f.input.SetNumberSelection("n0",f.Id(),4,1,f.error),"selection change"); Stale(f,stamp);
	stamp=Query(f).barrier; f.Text("1.5"); Stale(f,stamp);
	stamp=Query(f).barrier; Check(f.input.UndoNumberEdit("n0",f.Id(),false,f.error),"undo draft"); Stale(f,stamp);
	stamp=Query(f).barrier; Check(f.input.UndoNumberEdit("n0",f.Id(),true,f.error),"redo draft"); Stale(f,stamp);
	stamp=Query(f).barrier; TextInputEvent pre;
	Check(MakeTextInputPreedit("",TextIndexUnit::Utf8Bytes,0,0,pre,f.error) && f.input.ApplyNumberInput("n0",f.Id(),pre,f.error),"successful empty-preedit cancellation"); Stale(f,stamp);
	stamp=Query(f).barrier;
	Check(MakeTextInputPreedit("2",TextIndexUnit::Utf8Bytes,0,1,pre,f.error) && f.input.ApplyNumberInput("n0",f.Id(),pre,f.error),"composition visible");
	Check(Query(f).blocking[0].composing,"local composition blocks apply"); Stale(f,stamp);
	stamp=Query(f).barrier; f.input.Input(MenuInput::Back,true); f.input.Input(MenuInput::Back,false); Stale(f,stamp);
	Check(!f.View().composition && f.View().state.text=="1.5","Back cancels only local preedit");
	stamp=Query(f).barrier;
	Check(f.input.SetNumberNotice("n0",f.Id(),NumberEditNotice::ClipboardWriteFailed,f.error),"presentational notice");
	Check(Query(f).barrier==stamp,"notice does not retire unchanged text guard");
	const auto state=f.View().state;
	TextEditOperation noOp; noOp.kind=TextEditOperation::Kind::Selection; noOp.anchor=state.anchor; noOp.caret=state.caret;
	Check(f.input.ApplyNumberOperation("n0",f.Id(),noOp,f.error),"verified command no-op");
	Check(Query(f).barrier==stamp,"no-op and notice clearing preserve barrier");
	TextEditOperation move; move.kind=TextEditOperation::Kind::Selection; move.anchor=move.caret=0; move.changed=true;
	Check(f.input.ApplyNumberOperation("n0",f.Id(),move,f.error),"actual command selection move"); Stale(f,stamp);
	stamp=Query(f).barrier; const auto proposal=f.Commit(); Stale(f,stamp);
	const auto pending=Query(f); Check(pending.blocking.size()==1 && pending.blocking[0].pending,"pending typed commit blocks transaction");
	Check(!f.input.FocusNumberDraft(pending.barrier,"n0",f.error) && Query(f).barrier==pending.barrier,"pending focus failure is atomic");
	f.Read(1.5); Stale(f,pending.barrier); const auto readback=Query(f);
	Check(readback.blocking.size()==1 && readback.blocking[0].pending,"matching authoritative readback still waits for acknowledgement");
	Check(f.input.AcknowledgeProposal("n0",proposal.proposalToken,true),"acknowledge observed value"); Stale(f,readback.barrier);
	Check(Query(f).blocking.empty() && f.View().state.text=="1.5","accepted explicit commit rebases clean editor");
	f.Text("1.75"); const auto rejected=f.Commit(); stamp=Query(f).barrier;
	Check(f.input.AcknowledgeProposal("n0",rejected.proposalToken,false),"reject typed proposal"); Stale(f,stamp);
	Check(Query(f).blocking.size()==1 && !Query(f).blocking[0].pending && f.View().state.text=="1.75","rejected text remains blocking/editable");
	stamp=Query(f).barrier; f.Read(1.25); Stale(f,stamp); Check(Query(f).blocking[0].conflict,"external readback conflict blocks");
	f.input.Cancel(); stamp=Query(f).barrier; f.Read(1.0); Stale(f,stamp);
	stamp=Query(f).barrier; Check(f.input.FocusNumberDraft(stamp,"n0",f.error),"focus conflicted inactive draft");
	Check(f.View().conflict && f.View().state.text=="1.75","focus never implicitly rebases conflict");
	stamp=Query(f).barrier; Check(f.input.ResolveNumberConflict("n0",f.Id(),true,f.error),"explicit keep local draft"); Stale(f,stamp);
	Check(!f.View().conflict && f.View().state.text=="1.75" && Query(f).blocking.size()==1,"keep remains uncommitted");
	f.Read(1.5); stamp=Query(f).barrier; Check(f.input.ResolveNumberConflict("n0",f.Id(),false,f.error),"explicit reload authoritative text"); Stale(f,stamp);
	Check(Query(f).blocking.empty(),"reload clears unfinished draft");
}
static void RestoreAndRejection() {
	Fixture f; f.Begin(); f.Text("-"); f.Begin("n1"); f.Text("1.75","n1"); f.input.Cancel();
	const auto before=Query(f); auto saved=f.Capture();
	auto bad=saved; bad.widgets.at("n1").number->state.caret=999;
	Check(!f.input.RestoreWidgets(bad,f.error) && Query(f).barrier==before.barrier,"late malformed restore preserves all draft stamps");
	Check(f.input.RestoreWidgets(saved,f.error),"same-value restore succeeds"); Stale(f,before.barrier);
	const auto restored=Query(f); Check(!restored.blocking[0].active && !restored.blocking[1].active,"restore grants no native editor ownership");
	Fixture peer; Check(peer.input.RestoreWidgets(saved,peer.error),"same data can recreate a peer");
	Check(!peer.input.DiscardNumberDrafts(restored.barrier,peer.error) && Query(peer).blocking.size()==2,"cross-instance discard cannot erase same text");
	const auto semantic=f.input.Capture(); const auto stamp=Query(f).barrier;
	Check(f.input.Restore(semantic,f.error),"semantic restore resets local inventory"); Stale(f,stamp);
	const auto empty=Query(f); f.Begin(); Check(f.input.CancelNumberEdit("n0",f.Id()),"create/delete empty-inventory ABA"); Stale(f,empty.barrier);
	auto emptyRestore=Query(f).barrier;
	Check(f.input.Restore(f.input.Capture(),f.error),"empty semantic restore"); Stale(f,emptyRestore);
	emptyRestore=Query(f).barrier; Check(f.input.RestoreWidgets(f.Capture(),f.error),"empty widget restore"); Stale(f,emptyRestore);
	const auto reset=Query(f); f.input.Reset(f.model); f.input.SetReadbacks(f.readbacks,f.error); f.input.SetBounds(f.bounds); Stale(f,reset.barrier);
	Check(f.input.RestoreWidgets(saved,f.error),"restore multiple local drafts again"); auto barrier=Query(f).barrier;
	auto forged=barrier; std::swap(forged.editors[0],forged.editors[1]);
	Check(!f.input.DiscardNumberDrafts(forged,f.error) && Query(f).barrier==barrier,"reordered inventory rejected atomically");
	forged=barrier; forged.editors.pop_back(); Check(!f.input.DiscardNumberDrafts(forged,f.error),"omitted peer draft rejected");
	forged=barrier; ++forged.editors.back().lifetime; Check(!f.input.DiscardNumberDrafts(forged,f.error),"same-value different lifetime rejected");
	f.input.SetEnabled("n0",false); const auto disabled=Query(f); const auto focus=f.input.Focused();
	Check(!f.input.FocusNumberDraft(disabled.barrier,"n0",f.error) && Query(f).barrier==disabled.barrier && f.input.Focused()==focus,"disabled focus leaves peers untouched");
	f.input.SetEnabled("n0",true); Check(f.input.PushModal("n1"),"modal blocks outside field"); barrier=Query(f).barrier;
	Check(!f.input.FocusNumberDraft(barrier,"n0",f.error) && Query(f).barrier==barrier,"modal-ineligible draft cannot steal focus");
	Check(f.input.PopModal(),"modal closes"); f.input.InvalidateLayout(); barrier=Query(f).barrier;
	Check(!f.input.FocusNumberDraft(barrier,"n0",f.error) && Query(f).barrier==barrier,"stale layout cannot begin a text lease");
	f.input.SetBounds(f.bounds); barrier=Query(f).barrier;
	Check(f.input.DiscardNumberDrafts(barrier,f.error) && Query(f).blocking.empty(),"final exact discard succeeds");
	Interaction uninitialized; NumberDraftSummary sentinel=before;
	Check(!uninitialized.QueryNumberDrafts(sentinel,f.error) && sentinel.barrier==before.barrier && sentinel.blocking.size()==2,"unavailable query preserves output");
}
static void BudgetsAndPendingDiscard() {
	Fixture f(257);
	for(unsigned n=0;n<256;++n) { const auto id="n"+std::to_string(n); f.Begin(id); f.Text("-",id); }
	f.input.Cancel(); const auto maximum=Query(f); Check(maximum.blocking.size()==256,"inclusive bounded inventory");
	f.Begin("n256"); NumberDraftSummary unchanged=maximum;
	Check(!f.input.QueryNumberDrafts(unchanged,f.error) && unchanged.barrier==maximum.barrier,"over-budget query fails without truncation");
	Check(!f.input.DiscardNumberDrafts(maximum.barrier,f.error) && f.input.Widget("n0")->number,"over-budget inventory cannot partially discard");
	Fixture pending; pending.Begin(); pending.Text("1.25"); const auto action=pending.Commit(); auto barrier=Query(pending).barrier;
	Check(pending.input.DiscardNumberDrafts(barrier,pending.error),"discard can retire a taken pending commit");
	Check(!pending.input.CanDispatchControlAction(action) && !pending.input.AcknowledgeProposal("n0",action.proposalToken,true),"discarded typed action cannot publish later");
	pending.Begin(); pending.Text("1.5"); Check(pending.input.CommitNumberEdit("n0",pending.Id(),pending.error),"queued commit"); barrier=Query(pending).barrier;
	Check(pending.input.DiscardNumberDrafts(barrier,pending.error) && pending.input.TakeActions().empty(),"discard removes queued local commit");
}
int main() {
	InventoryAndInactive(); MutationBarriers(); RestoreAndRejection(); BudgetsAndPendingDiscard();
	std::printf("UiNumberDraftTest: %u checks passed (real local Interaction/TextEdit, no native document or device).\n",checks);
}
