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
static bool Same(const TextEditState& a, const TextEditState& b) {
	return a.text == b.text && a.anchor == b.anchor && a.caret == b.caret;
}
static bool Same(const TextEditHistory& a, const TextEditHistory& b) {
	if (a.undo.size() != b.undo.size() || a.redo.size() != b.redo.size()) return false;
	for (std::size_t i=0;i<a.undo.size();++i) if (!Same(a.undo[i],b.undo[i])) return false;
	for (std::size_t i=0;i<a.redo.size();++i) if (!Same(a.redo[i],b.redo[i])) return false;
	return true;
}
struct Fixture {
	DocumentModel model; Interaction input; std::string error;
	std::map<std::string,ControlBounds> bounds;
	std::map<std::string,ControlReadback> readbacks;
	explicit Fixture(unsigned count=2, std::uint32_t maxBytes=64) {
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

static void History() {
	TextEditBuffer buffer; std::string error;
	Check(buffer.Reset("1",{64,false,false},error),"history starts");
	Check(buffer.SetSelection(0,1,error) && buffer.ReplaceSelection("1.25",error),"first history entry");
	Check(buffer.SetSelection(0,4,error) && buffer.ReplaceSelection("1.5",error),"second history entry");
	Check(buffer.Undo(error),"make redo endpoint");
	Check(buffer.SetSelection(4,0,error),"preserve reversed selection");
	const auto state=buffer.State(); const auto history=buffer.CaptureHistory();
	TextInputEvent preedit; Check(MakeTextInputPreedit("2",TextIndexUnit::Utf8Bytes,1,0,preedit,error),"preedit packet");
	Check(buffer.Apply(preedit,error),"live preedit");
	TextEditBuffer restored; Check(restored.RestoreHistory(state,history,buffer.Policy(),error),"restore exact bounded history");
	Check(Same(restored.State(),state) && Same(restored.CaptureHistory(),history) && !restored.Composition(),"history restore excludes composition and preserves endpoints");
	Check(restored.Redo(error) && restored.State().text=="1.5","restored redo order");
	Check(restored.Undo(error) && restored.State().text=="1.25","restored undo order");
	const auto beforeState=buffer.State(); const auto beforeHistory=buffer.CaptureHistory();
	std::vector<std::function<void(TextEditState&,TextEditHistory&)>> corrupt{
		[](auto& s,auto&) { s.text="bad\nline"; },
		[](auto& s,auto&) { s.text=std::string("x\0y",3); },
		[](auto& s,auto&) { s.text="\xc0\xaf"; },
		[](auto& s,auto&) { s.text="\xc3\xa9"; s.anchor=1; s.caret=2; },
		[](auto& s,auto&) { s.anchor=std::numeric_limits<std::size_t>::max(); },
		[](auto& s,auto&) { s.text=std::string(65,'1'); },
		[](auto&,auto& h) { h.undo[0].caret=999; },
		[](auto&,auto& h) { h.redo[0].text="\xe2\x80\xa8"; },
		[](auto&,auto& h) { h.undo.resize(TextEditBuffer::MaxHistoryEntries+1); },
		[](auto&,auto& h) { h.undo.resize(TextEditBuffer::MaxHistoryEntries); h.redo.resize(1); }
	};
	for (const auto& change:corrupt) {
		auto badState=state; auto badHistory=history; change(badState,badHistory);
		Check(!buffer.RestoreHistory(badState,badHistory,{64,false,false},error) && !error.empty(),"malformed history rejected");
		Check(Same(buffer.State(),beforeState) && Same(buffer.CaptureHistory(),beforeHistory) && buffer.Composition(),"history failure leaves text, selection, history and preedit unchanged");
	}
	TextEditHistory large; large.undo.resize(17,TextEditState{std::string(65536,'1'),0,65536});
	Check(!buffer.RestoreHistory({"1",0,1},large,{65536,false,false},error),"combined history byte budget enforced");
	large.undo.resize(16); Check(restored.RestoreHistory({"1",0,1},large,{65536,false,false},error),"inclusive history byte budget accepted");
	Check(restored.HistoryTextBytes()==TextEditBuffer::MaxHistoryTextBytes,"exact history byte count");
}

static void DetachAndResume() {
	Fixture f; f.Begin(); f.Text("1.25"); f.Text("1.5"); Check(f.input.UndoNumberEdit("n0",f.Id(),false,f.error),"create redo before focus loss");
	const auto old=f.Id(); const auto before=f.View();
	f.Begin("n1"); Check(!f.View().active && f.Id()==NumberEditIdentity{} && f.View().state.text=="1.25" && f.View().canRedo,"Tab/focus detaches and preserves draft and redo");
	Check(!f.input.ReplaceNumberSelection("n0",old,"2",f.error),"former focus lease cannot edit detached draft");
	f.Text("-","n1"); f.Begin(); Check(f.View().active && f.Id()!=old && f.View().state.text=="1.25" && Same(f.View().state,before.state),"Begin resumes exact old selection with new lease");
	Check(!f.View("n1").active && f.View("n1").state.text=="-","independent invalid draft stays visible");
	Check(f.input.UndoNumberEdit("n0",f.Id(),true,f.error) && f.View().state.text=="1.5","redo survives focus movement");
	const auto action=f.Commit();
	Check(f.input.PushModal("n1"),"open modal over a pending edit");
	Check(!f.View().active && f.View().state.text=="1.5" && !f.input.Widget("n0")->pending,"modal detaches text and cancels proposal");
	Check(!f.input.CanDispatchControlAction(action) && !f.input.AcknowledgeProposal("n0",action.proposalToken,true),"taken proposal cannot survive modal ownership change");
	Check(f.input.PopModal(),"close modal"); f.Begin();
	f.input.Cancel(); Check(!f.View().active && f.View().state.text=="1.5","input quarantine preserves local draft");
	f.Begin(); f.input.SetEnabled("n0",false); Check(!f.View().active && f.View().state.text=="1.5","disabled field retains local draft");
	f.input.SetEnabled("n0",true); f.Begin(); f.input.InvalidateLayout();
	Check(!f.View().active && f.View().state.text=="1.5","unavailable layout retires live edit but keeps text");
	f.input.SetBounds(f.bounds); Check(!f.View().active,"fresh bounds alone do not acquire text input ownership");
	f.Begin(); Check(f.input.CancelNumberEdit("n0",f.Id()) && !f.input.Widget("n0")->number,"explicit cancel discards local draft");
	Check(f.View("n1").state.text=="-","explicit cancel never discards another field");
}

static void RoundTrip() {
	Fixture source; source.Begin(); source.Text("1.25"); source.Text("1.5");
	Check(source.input.UndoNumberEdit("n0",source.Id(),false,source.error),"saved redo endpoint");
	Check(source.input.SetNumberSelection("n0",source.Id(),4,1,source.error),"saved nontrivial selection");
	TextInputEvent preedit; Check(MakeTextInputPreedit("7",TextIndexUnit::Utf8Bytes,0,1,preedit,source.error),"saved preedit source");
	Check(source.input.ApplyNumberInput("n0",source.Id(),preedit,source.error),"live preedit before save");
	const auto old=source.Id(); const auto saved=source.Capture();
	Check(saved.version==2 && saved.widgets.at("n0").number && !saved.widgets.at("n1").number,"v2 optional editor presence");
	Check(source.View().composition && source.Id()==old,"capture does not mutate live editor or composition");
	Fixture target; target.Begin("n1"); target.Text("bad","n1"); const auto obsolete=target.Id("n1");
	Check(target.input.Restore(source.input.Capture(),target.error),"restore semantic focus before widget data");
	Check(target.input.RestoreWidgets(saved,target.error),"source-matched widget snapshot");
	Check(!target.View().active && target.Id()==NumberEditIdentity{} && !target.View().composition && target.View().state.text=="1.25","restore retains local text but never native preedit or lease");
	Check(target.View().state.anchor==4 && target.View().state.caret==1 && target.View().canUndo && target.View().canRedo,"restore preserves selection and both histories");
	Check(!target.input.ReplaceNumberSelection("n0",old,"2",target.error) && !target.input.CancelNumberEdit("n1",obsolete),"sender and replaced-receiver identities stay invalid");
	target.input.SetBounds(target.bounds); Check(!target.View().active,"first rebuilt layout leaves editor detached");
	target.Begin(); Check(target.Id()!=old && target.Id()!=obsolete,"resuming restored editor allocates never-reused identity");
	Check(target.input.UndoNumberEdit("n0",target.Id(),true,target.error) && target.View().state.text=="1.5","roundtrip redo retains exact text");
	Fixture second; Check(second.input.RestoreWidgets(saved,second.error),"same snapshot can restore another independent instance"); second.Begin();
	Check(second.Id()!=target.Id() && !second.input.ReplaceNumberSelection("n0",target.Id(),"2",second.error),"restored instances never share edit ownership");
	const auto action=target.Commit(); const auto pending=target.Capture();
	Check(target.input.RestoreWidgets(pending,target.error) && !target.input.Widget("n0")->pending && target.View().state.text=="1.5","save of pending proposal preserves buffer without replaying action");
	Check(!target.input.CanDispatchControlAction(action) && !target.input.AcknowledgeProposal("n0",action.proposalToken,true),"all old proposal tokens retired by restore");
	Check(target.input.TakeActions().empty(),"restoration cannot enqueue a hidden commit");
	auto legacy=saved; legacy.version=1; for (auto& [id,widget]:legacy.widgets) widget.number.reset();
	Check(target.input.RestoreWidgets(legacy,target.error) && !target.input.Widget("n0")->number,"old v1 widget snapshots remain readable");
}

static void Baselines() {
	Fixture source; source.Begin(); source.Text("-"); const auto invalid=source.Capture();
	Fixture changed; changed.Read(1.5); Check(changed.input.RestoreWidgets(invalid,changed.error),"fresh baseline wins over saved baseline");
	Check(changed.View().state.text=="-" && changed.View().conflict && std::get<double>(changed.input.Widget("n0")->accepted)==1.5,"unfinished invalid text retained as conflict without changing authoritative value");
	Check(!changed.input.CommitNumberEdit("n0",changed.Id(),changed.error),"detached conflict cannot commit");
	const auto conflict=changed.Capture(); Check(conflict.widgets.at("n0").number->baselineValue==1.0 && conflict.widgets.at("n0").number->conflict,"conflict retains original baseline provenance");
	Fixture again; Check(again.input.RestoreWidgets(conflict,again.error) && again.View().conflict,"a previous conflict cannot disappear merely because external value returns");
	changed.Begin(); Check(changed.View().state.text=="-" && changed.View().conflict && changed.View().active,"Begin preserves conflicted text while acquiring fresh ownership");
	const auto unresolved=changed.Id();
	Check(changed.input.ResolveNumberConflict("n0",unresolved,false,changed.error) && changed.View().state.text=="1.5" && !changed.View().conflict && changed.Id()!=unresolved,"explicit Reload resolves from current authoritative value and retires revision");
	Fixture clean; clean.Begin(); auto saved=clean.Capture(); changed.Read(1.75);
	Check(changed.input.RestoreWidgets(saved,changed.error) && changed.View().state.text=="1.75" && !changed.View().conflict && !changed.View().active,"clean saved buffer follows changed authoritative readback while detached");
	Fixture custom; custom.Read(42); custom.Begin(); saved=custom.Capture();
	Fixture customTarget; customTarget.Read(42); Check(customTarget.input.RestoreWidgets(saved,customTarget.error),"custom out-of-editor-range baseline stays valid data");
	Check(customTarget.View().state.text=="42" && customTarget.View().status==TextNumberStatus::OutOfRange,"custom value never clamped");
	source.Begin("n1"); source.Text("bad","n1"); saved=source.Capture(); Check(saved.widgets.at("n0").number && saved.widgets.at("n1").number,"multiple inactive drafts persist independently");
	Fixture many; Check(many.input.RestoreWidgets(saved,many.error) && many.View().state.text=="-" && many.View("n1").state.text=="bad","multiple numeric drafts restore without hidden focus/commit");
}

static void ConflictDecisions() {
	Fixture f; f.Begin(); f.Text("1.25"); f.Text("1.75");
	Check(f.input.UndoNumberEdit("n0",f.Id(),false,f.error),"conflict starts with undo and redo");
	f.Read(1.5); const auto conflicted=f.Id(); const auto before=f.View();
	Check(before.conflict && before.state.text=="1.25" && before.canUndo && before.canRedo,"conflict preserves both histories");
	Check(!f.input.ResolveNumberConflict("n0",{},true,f.error) && f.Id()==conflicted,"conflict choice requires exact nonzero active identity");
	f.Begin("n1"); Check(!f.input.ResolveNumberConflict("n0",conflicted,true,f.error) && f.View().conflict,"detached conflict cannot be resolved by stale owner");
	f.Begin(); const auto resumed=f.Id(); Check(f.View().conflict && Same(f.View().state,before.state),"refocusing does not discard conflict");
	Check(f.input.ResolveNumberConflict("n0",resumed,true,f.error),"explicit Keep my edit adopts current baseline");
	Check(!f.View().conflict && f.View().state.text=="1.25" && f.View().canUndo && f.View().canRedo && f.Id()!=resumed && f.input.TakeActions().empty(),"Keep preserves local history, rotates revision and never commits");
	const auto saved=f.Capture(); Check(saved.widgets.at("n0").number->baselineValue==1.5 && saved.widgets.at("n0").number->baselineText=="1.5","Keep records current authoritative baseline");
	Check(!f.input.ResolveNumberConflict("n0",resumed,false,f.error),"prior decision token cannot perform another decision");
	Check(f.input.UndoNumberEdit("n0",f.Id(),true,f.error) && f.View().state.text=="1.75","Keep retains redo after conflict resolution");
	f.Read(1.8); const auto reload=f.Id();
	Check(f.input.ResolveNumberConflict("n0",reload,false,f.error) && f.View().state.text=="1.8" && !f.View().canUndo && !f.View().canRedo && !f.View().dirty,"explicit Reload resets local text/history to accepted value");
	Check(std::get<double>(f.input.Widget("n0")->accepted)==1.8 && f.input.TakeActions().empty(),"neither resolution path writes accepted state or queues an application action");
	Check(!f.input.ResolveNumberConflict("n0",f.Id(),true,f.error),"non-conflicted field has no conflict to resolve");
}

static void CorruptionAndBudgets() {
	Fixture source; source.Begin(); source.Text("1.25"); auto good=source.Capture();
	Fixture live; live.Begin(); live.Text("1.5"); auto action=live.Commit(); const auto identity=live.Id();
	std::vector<std::function<void(ValueWidgetSnapshot&)>> corrupt{
		[](auto& s){s.version=0;}, [](auto& s){s.version=3;}, [](auto& s){s.version=1;},
		[](auto& s){s.widgets.erase("n1");}, [](auto& s){s.widgets["unknown"]=s.widgets.at("n1");},
		[](auto& s){s.widgets.at("n0").role=ControlRole::Slider;}, [](auto& s){s.widgets.at("n0").firstVisible=1;},
		[](auto& s){s.widgets.at("n0").number->baselineValue=std::numeric_limits<double>::quiet_NaN();},
		[](auto& s){s.widgets.at("n0").number->baselineValue=1e13;},
		[](auto& s){s.widgets.at("n0").number->baselineText="1.0";},
		[](auto& s){s.widgets.at("n0").number->state.text="\xed\xa0\x80";},
		[](auto& s){s.widgets.at("n0").number->state.text="bad\nline";},
		[](auto& s){s.widgets.at("n0").number->state.caret=999;},
		[](auto& s){s.widgets.at("n0").number->state.text=std::string(65,'1');},
		[](auto& s){s.widgets.at("n0").number->undo[0].text=std::string("x\0y",3);},
		[](auto& s){s.widgets.at("n0").number->redo.resize(65);}
	};
	for (const auto& change:corrupt) {
		auto bad=good; change(bad); Check(!live.input.RestoreWidgets(bad,live.error) && !live.error.empty(),"malformed/source-policy snapshot rejected");
		Check(live.Id()==identity && live.View().active && live.View().state.text=="1.5" && live.input.CanDispatchControlAction(action),"failed widget restore is atomic for draft/lease/pending action");
	}
	Fixture count(257); for (unsigned i=0;i<257;++i) count.Begin("n"+std::to_string(i));
	ValueWidgetSnapshot unchanged; unchanged.version=99;
	Check(!count.input.CaptureWidgets(unchanged,count.error) && unchanged.version==99,"checked capture rejects too many drafts without replacing output");
	Check(count.View("n256").active && count.View().state.text=="1","capture budget refusal never discards drafts");
	Check(count.input.CancelNumberEdit("n256"),"remove explicit draft to meet count budget"); auto maximum=count.Capture();
	Check(maximum.widgets.size()==257,"count limit applies to saved editors, not total controls");
	maximum.widgets.at("n256").number=maximum.widgets.at("n0").number;
	Check(!count.input.RestoreWidgets(maximum,count.error),"restore aggregate editor count rejects before mutation");
	Fixture large(17,65536); ValueWidgetSnapshot huge;
	for (unsigned i=0;i<17;++i) {
		ValueWidgetSnapshot::Widget widget; widget.role=ControlRole::Number;
		NumberEditorSnapshot editor; editor.state={"1",0,1}; editor.baselineValue=1; editor.baselineText="1";
		editor.undo.resize(16,TextEditState{std::string(65536,'1'),0,65536}); widget.number=std::move(editor);
		huge.widgets.emplace("n"+std::to_string(i),std::move(widget));
	}
	Check(!large.input.RestoreWidgets(huge,large.error) && !large.input.Widget("n0")->number,"restore aggregate text budget rejects atomically");
	// 15 MiB of history plus small live buffers is valid; capture preserves all
	// bytes and never serializes active/native state.
	for (unsigned i=15;i<17;++i) huge.widgets.at("n"+std::to_string(i)).number.reset();
	Check(large.input.RestoreWidgets(huge,large.error),"individually and aggregately bounded histories accepted");
	auto largeSaved=large.Capture(); Check(largeSaved.widgets.at("n0").number->undo.size()==16,"capture does not truncate bounded history");
}
int main() {
	History(); DetachAndResume(); RoundTrip(); Baselines(); ConflictDecisions(); CorruptionAndBudgets();
	std::printf("UI number snapshots: %u checks passed\n",checks); return 0;
}
