#!/usr/bin/env python3
"""Compile production managed clipboard/FIFO/Number notice boundaries.

Clipboard services are counted functions only: no SDL calls, native clipboard,
Session routing, window, input or GPU execution. Manager+Deferred and retained
adapter methods are extracted unchanged. TextEdit and Interaction are real;
recording adapters exercise lifecycle re-resolution independently of rendering.
"""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,tempfile
import ui_manager_lifecycle as manager
import ui_retained_adapter as adapter
import ui_text_owner as owner
from filesystem_case_segments import function_body
ROOT=Path(__file__).resolve().parents[2]

NATIVE=r'''
#include <deque>
#include "src/ui/retained/TextEdit.h"
static int reads=0,writes=0,backendStack=0;
static bool readOkay=true,writeOkay=true;
static std::string pasted="1.75",written;
static std::function<void()> nativeCallback;
static std::vector<std::string> order;
namespace openq4 {
bool SDL3_ReadTextClipboard(std::string& value,std::string& error) {
    assert(backendStack==0);++reads;order.push_back("read");auto callback=nativeCallback;if(callback)callback();
    if(!readOkay){error="native sensitive failure";return false;}value=pasted;return true;
}
bool SDL3_WriteTextClipboard(const std::string& value,std::string& error) {
    assert(backendStack==0);++writes;order.push_back("write");written=value;auto callback=nativeCallback;if(callback)callback();
    if(!writeOkay){error="native sensitive failure";return false;}return true;
}
}
'''
BACKEND=r'''
class idUserInterfaceRetained : public idUserInterfaceLocal {
public:
    using Notice=openq4::ui::NumberEditNotice;
    static inline std::uint64_t next=100;
    struct Stack { Stack(){++backendStack;}~Stack(){--backendStack;} };
    uiNumberEditorTarget_t target{++next,++next,++next,"number",{++next,++next}};
    openq4::ui::TextEditBuffer buffer;
    Notice notice=Notice::None;
    bool eligible=true,conflict=false,pending=false,interactiveOverride=false;
    int replacements=0,notices=0;
    struct Entry {std::optional<uiClipboardRequest_t> clipboard;std::string action;std::function<void()> effect;};
    std::deque<Entry> queue;
    explicit idUserInterfaceRetained(bool managed=true):idUserInterfaceLocal(managed) {
        interactive=true;active=true;std::string error;assert(buffer.Reset("1.25",{64,false,false},error));assert(buffer.SetSelection(4,0,error));
    }
    int Kind() const override{return 2;}
    void Request(uiClipboardOperation_t op){queue.push_back({uiClipboardRequest_t{op,target},{},{}});}
    void Action(std::string name,std::function<void()> effect={}){queue.push_back({{},std::move(name),std::move(effect)});}
    const char* PendingApplicationCommand()const override{return queue.empty()?"":"retained-pending";}
    bool DispatchApplicationActions(const char* command,bool& close)override{
        Stack stack;close=false;if(!command || std::string(command)!="retained-pending")return true;
        while(!queue.empty() && !queue.front().clipboard){auto entry=std::move(queue.front());queue.pop_front();order.push_back(entry.action);
            if(entry.action=="close"){close=true;queue.clear();return true;}if(entry.effect)entry.effect();}
        return true;
    }
    bool TakeClipboardRequest(const char* command,uiClipboardRequest_t& out)override{
        Stack stack;if(!command || std::string(command)!="retained-pending" || queue.empty() || !queue.front().clipboard)return false;
        out=*queue.front().clipboard;queue.pop_front();return true;
    }
    bool QueryClipboardEditor(uiNumberEditorSnapshot_t& out,std::string&)override{
        Stack stack;if(!eligible || !active || conflict || pending || buffer.Composition())return false;
        openq4::ui::NumberEditView view;view.active=true;view.identity=target.edit;view.state=buffer.State();view.notice=notice;
        view.canUndo=buffer.CanUndo();view.canRedo=buffer.CanRedo();out={target,view};return true;
    }
    bool ReplaceClipboardSelection(const uiNumberEditorTarget_t& expected,std::string_view text,std::string& error)override{
        Stack stack;if(expected!=target || !eligible || conflict || pending || buffer.Composition())return false;
        if(!buffer.ReplaceSelection(text,error))return false;++replacements;++target.edit.revision;notice=Notice::None;order.push_back("replace");return true;
    }
    bool SetClipboardNotice(const uiNumberEditorTarget_t& expected,Notice value,std::string&)override{
        Stack stack;if(expected!=target || !eligible || conflict || pending || buffer.Composition())return false;
        notice=value;++notices;return true;
    }
};
'''
MANAGER_MAIN=r'''
using namespace openq4::ui;
static void Drain(idUserInterface* gui,bool expectedClose=false){bool close=false;assert(UI_DispatchApplicationActions(gui,"retained-pending",close));assert(close==expectedClose);}
static void NativeReset(){reads=writes=0;readOkay=writeOkay=true;nativeCallback={};pasted="1.75";written.clear();order.clear();}
int main(){
    auto& m=uiManagerLocal;std::string error;NativeReset();
    auto* a=new idUserInterfaceRetained;const auto before=a->target;
    a->Request(uiClipboardOperation_t::Copy);Drain(a);assert(writes==1 && reads==0 && written=="1.25" && a->target==before && !a->buffer.CanUndo());
    NativeReset();writeOkay=false;a->Request(uiClipboardOperation_t::Cut);Drain(a);
    assert(writes==1 && a->buffer.State().text=="1.25" && a->target==before && !a->buffer.CanUndo() && a->notice==NumberEditNotice::ClipboardWriteFailed);
    NativeReset();a->Request(uiClipboardOperation_t::Cut);Drain(a);
    assert(writes==1 && a->buffer.State().text.empty() && a->buffer.CanUndo() && a->notice==NumberEditNotice::None);
    assert(a->buffer.Undo(error) && a->buffer.State().text=="1.25" && a->buffer.State().anchor==4 && a->buffer.State().caret==0);++a->target.edit.revision;
    assert(a->buffer.Redo(error) && a->buffer.State().text.empty());assert(a->buffer.Undo(error));++a->target.edit.revision;
    NativeReset();const auto emptyIdentity=a->target;pasted="";a->Request(uiClipboardOperation_t::Paste);Drain(a);
    assert(reads==1 && a->buffer.State().text=="1.25" && a->buffer.State().anchor==4 && a->target==emptyIdentity && a->buffer.CanRedo());
    NativeReset();readOkay=false;a->Request(uiClipboardOperation_t::Paste);Drain(a);
    assert(a->target==emptyIdentity && a->buffer.CanRedo() && a->notice==NumberEditNotice::ClipboardReadFailed);
    for(const auto& bad:std::vector<std::string>{"a\nb","a\rb","a\xc2\x85" "b","a\xe2\x80\xa8" "b","a\xe2\x80\xa9" "b",std::string(65,'a'),std::string(65537,'a'),std::string("a\0b",3),std::string("\xc0\xaf",2)}){
        NativeReset();pasted=bad;a->Request(uiClipboardOperation_t::Paste);Drain(a);
        assert(reads==1 && a->target==emptyIdentity && a->buffer.State().text=="1.25" && a->buffer.CanRedo() && a->notice==NumberEditNotice::ClipboardRejected);
    }
    NativeReset();pasted="invalid\xc3\xa9";a->Request(uiClipboardOperation_t::Paste);Drain(a);
    assert(a->buffer.State().text==pasted && a->notice==NumberEditNotice::None && !a->buffer.CanRedo());
    assert(a->buffer.Undo(error));++a->target.edit.revision;
    NativeReset();assert(a->buffer.SetSelection(0,0,error));++a->target.edit.revision;const auto noSelection=a->target;
    a->Request(uiClipboardOperation_t::Copy);a->Request(uiClipboardOperation_t::Cut);Drain(a);assert(reads==0 && writes==0 && a->target==noSelection);
    delete a;
    // Every identity component and eligibility is rechecked after native access.
    for(int change=0;change<11;++change){
        NativeReset();a=new idUserInterfaceRetained;
        nativeCallback=[&,change]{switch(change){case 0:++a->target.backend;break;case 1:++a->target.document;break;case 2:++a->target.modal;break;
            case 3:a->target.control="peer";break;case 4:++a->target.edit.session;break;case 5:++a->target.edit.revision;break;
            case 6:a->eligible=false;break;case 7:a->active=false;break;case 8:a->conflict=true;break;case 9:a->pending=true;break;
            case 10:TextInputEvent composition;assert(MakeTextInputPreedit("1",TextIndexUnit::Utf8Bytes,0,1,composition,error));assert(a->buffer.Apply(composition,error));break;}};
        a->Request(uiClipboardOperation_t::Cut);Drain(a);assert(writes==1 && a->replacements==0 && a->notices==0 && a->buffer.State().text=="1.25");delete a;
    }
    for(int unavailable=0;unavailable<4;++unavailable){
        NativeReset();a=new idUserInterfaceRetained;a->Request(uiClipboardOperation_t::Paste);
        if(unavailable==0)a->eligible=false;if(unavailable==1)a->pending=true;if(unavailable==2)a->conflict=true;
        if(unavailable==3){TextInputEvent pre;assert(MakeTextInputPreedit("1",TextIndexUnit::Utf8Bytes,0,1,pre,error));assert(a->buffer.Apply(pre,error));}
        Drain(a);assert(reads==0 && writes==0);delete a;
    }
    // Earlier committed actions and later actions retain precise FIFO order.
    NativeReset();a=new idUserInterfaceRetained;a->Action("before");a->Request(uiClipboardOperation_t::Paste);a->Action("after");Drain(a);
    assert((order==std::vector<std::string>{"before","read","replace","after"}));delete a;
    NativeReset();a=new idUserInterfaceRetained;a->Action("close");a->Request(uiClipboardOperation_t::Paste);Drain(a,true);assert(reads==0);delete a;
    NativeReset();a=new idUserInterfaceRetained;a->Action("change",[&]{++a->target.document;});a->Request(uiClipboardOperation_t::Paste);Drain(a);assert(reads==0);delete a;
    // A native callback can release this exact allocation and reuse its address.
    NativeReset();a=new idUserInterfaceRetained;auto* address=a;a->Request(uiClipboardOperation_t::Cut);
    const auto reusedTarget=a->target;
    nativeCallback=[&]{recycleAllocation=true;delete a;a=new idUserInterfaceRetained;recycleAllocation=false;assert(a==address);a->target=reusedTarget;};
    Drain(address);assert(writes==1 && a->replacements==0 && a->notices==0 && a->buffer.State().text=="1.25");delete a;
    NativeReset();a=new idUserInterfaceRetained;a->Request(uiClipboardOperation_t::Paste);address=a;
    nativeCallback=[&]{delete a;a=nullptr;};Drain(address);assert(reads==1 && a==nullptr);
    // The same registered Deferred survives replacement of its inner backend.
    NativeReset();auto* deferred=new idUserInterfaceDeferred;assert(deferred->InitFromFile("one.q4ui",true,true));
    auto* inner=static_cast<idUserInterfaceRetained*>(deferred->backend);inner->Request(uiClipboardOperation_t::Cut);
    nativeCallback=[&]{assert(deferred->InitFromFile("two.q4ui",true,true));};Drain(deferred);
    inner=static_cast<idUserInterfaceRetained*>(deferred->backend);assert(writes==1 && inner->replacements==0 && inner->notices==0);delete deferred;
    // Nested drains may not run another native service or authorize the first.
    NativeReset();a=new idUserInterfaceRetained;auto* b=new idUserInterfaceRetained;b->Request(uiClipboardOperation_t::Paste);
    nativeCallback=[&]{bool close=true;assert(UI_DispatchApplicationActions(b,"retained-pending",close) && !close);};a->Request(uiClipboardOperation_t::Cut);Drain(a);
    assert(writes==1 && reads==0 && a->replacements==0);nativeCallback={};Drain(b);assert(reads==1 && b->replacements==1);delete a;delete b;
    NativeReset();a=new idUserInterfaceRetained;nativeCallback=[] {throw std::runtime_error("clipboard callback");};a->Request(uiClipboardOperation_t::Cut);Drain(a);
    assert(a->buffer.State().text=="1.25" && a->notice==NumberEditNotice::ClipboardWriteFailed);delete a;
    assert(m.allocations.Num()==0 && !backendStack);std::puts("Clipboard manager: native access outside backend stacks; exact owner/Deferred/ABA/reentrancy/FIFO, write-before-cut, empty paste, failures, UTF-8/line/budgets and history passed");
}
'''

def manager_source():
    source=manager.production_source()
    stub='namespace openq4 { bool SDL3_ReadTextClipboard(std::string&,std::string&) { return false; } bool SDL3_WriteTextClipboard(const std::string&,std::string&) { return false; } }\n'
    assert stub in source;source=source.replace(stub,'')
    at=source.index('class idUserInterfaceManagerLocal {');source=source[:at]+NATIVE+source[at:]
    old=function_body(source,'class idUserInterfaceRetained : public idUserInterfaceLocal {')+';'
    source=source.replace(old,BACKEND)
    # Expose the deferred private pointer only to inspect counted backend results.
    source=source.replace('private:\n\tidUserInterfaceManaged *backend;','public:\n\tidUserInterfaceManaged *backend;')
    return source+MANAGER_MAIN

ADAPTER_MAIN=r"""
static void CheckClipboardKeys() {
    assert(views.empty());const auto original=modelTemplate;Expression operand;operand.type=0;operand.inputValue=true;
    modelTemplate.actions["number.set"]={"settings.brightness.set",{{"value",operand}},std::size_t(0)};
    consoleObject.open=false;windowFocused=true;
    {
        idUserInterfaceRetained gui;assert(gui.InitFromFile("test.q4ui"));gui.Activate(true,0);gui.Redraw(0);
        auto& runtime=Live();runtime.InstallNumber("root","number.set");runtime.InstallNumber("other","number.set");runtime.selected="root";
        std::string error;assert(runtime.BeginNumberEdit("root",error,0));
        const auto captured=[&](int key,bool down,openq4::KeyEventMetadata modifiers){
            auto bytes=openq4::EncodeKeyEventMetadata(modifiers);sysEvent_t event{SE_KEY,key,down};event.evPtr=bytes.data();event.evPtrLength=bytes.size();return std::string(gui.HandleEvent(&event,0,nullptr));};
        const auto request=[&](int key,openq4::KeyEventMetadata modifiers,uiClipboardOperation_t operation){
            const auto before=runtime.widgets.at("root").number->identity;
            assert(captured(key,true,modifiers)==ActionMarker);uiClipboardRequest_t out;
            assert(gui.TakeClipboardRequest(ActionMarker,out) && out.operation==operation && out.target.edit==before && out.target.control=="root");
            assert(!gui.TakeClipboardRequest(ActionMarker,out));modifiers.repeated=true;captured(key,true,modifiers);
            assert(!gui.TakeClipboardRequest(ActionMarker,out));captured(key,false,{});
            assert(runtime.widgets.at("root").number->identity==before);
        };
        for(const auto pair:{std::pair{'c',uiClipboardOperation_t::Copy},{'x',uiClipboardOperation_t::Cut},{'v',uiClipboardOperation_t::Paste},{K_INS,uiClipboardOperation_t::Copy}})
            request(pair.first,{true,false,false,false},pair.second);
        request(K_INS,{false,true,false,false},uiClipboardOperation_t::Paste);
        request(K_DEL,{false,true,false,false},uiClipboardOperation_t::Cut);
        uiClipboardRequest_t out;
        for(const int key:std::vector<int>{'c','x','v',K_INS,K_DEL}) {
            captured(key,true,{true,false,true,false});captured(key,false,{});assert(!gui.TakeClipboardRequest(ActionMarker,out));
            captured(key,true,{false,true,true,false});captured(key,false,{});assert(!gui.TakeClipboardRequest(ActionMarker,out));
        }
        // Queue only after fresh eligible readback; preedit/pending/conflict do not call a service.
        runtime.widgets.at("root").number->conflict=true;captured('v',true,{true,false,false,false});captured('v',false,{});assert(!gui.TakeClipboardRequest(ActionMarker,out));
        runtime.widgets.at("root").number->conflict=false;
        TextInputEvent pre;assert(MakeTextInputPreedit("1",TextIndexUnit::Utf8Bytes,0,1,pre,error));
        runtime.widgets.at("root").number->composition=pre;uiNumberEditorSnapshot_t composed;assert(!gui.QueryClipboardEditor(composed,error));captured('x',true,{true,false,false,false});captured('x',false,{});assert(!gui.TakeClipboardRequest(ActionMarker,out));
        runtime.widgets.at("root").number->composition.reset();runtime.widgets.at("root").pending=1.0;
        captured('c',true,{true,false,false,false});captured('c',false,{});assert(!gui.TakeClipboardRequest(ActionMarker,out));runtime.widgets.at("root").pending.reset();
        // Held command quarantines across focus replacement, including a repeat carrying a new modifier state.
        captured('v',true,{true,false,false,false});assert(gui.TakeClipboardRequest(ActionMarker,out));
        runtime.selected="other";assert(runtime.BeginNumberEdit("other",error,0));captured('v',true,{true,false,false,true});assert(!gui.TakeClipboardRequest(ActionMarker,out));
        captured('v',false,{});captured('v',true,{true,false,false,false});assert(gui.TakeClipboardRequest(ActionMarker,out) && out.target.control=="other");captured('v',false,{});
        runtime.selected="root";
        // Actual FIFO prefix stops before clipboard; dismiss suffix waits for the manager to take it.
        StubEvent prefix;prefix.invocations={{"","settings.brightness.set",{{"value",1.125}}}};eventPlans["beforeclipboard"]=prefix;
        StubEvent suffix;suffix.invocations={{"","ui.dismiss",{}}};eventPlans["afterclipboard"]=suffix;
        gui.HandleNamedEvent("beforeClipboard");captured('c',true,{true,false,false,false});captured('c',false,{});gui.HandleNamedEvent("afterClipboard");
        bool close=false;assert(gui.DispatchApplicationActions(ActionMarker,close) && !close && cvars.brightness==1.125f);
        assert(gui.TakeClipboardRequest(ActionMarker,out));assert(gui.DispatchApplicationActions(ActionMarker,close) && close);
        // A preceding dismiss skips clipboard while preserving the committed suffix.
        cvars.brightness=1;gui.HandleNamedEvent("afterClipboard");captured('c',true,{true,false,false,false});captured('c',false,{});
        gui.HandleNamedEvent("beforeClipboard");captured('v',true,{true,false,false,false});captured('v',false,{});
        assert(gui.DispatchApplicationActions(ActionMarker,close) && close && !gui.TakeClipboardRequest(ActionMarker,out) && cvars.brightness==1.125f);
        // Window/console loss before any ordinary event must discard queued physical commands.
        captured('v',true,{true,false,false,false});consoleObject.open=true;assert(gui.DispatchApplicationActions(ActionMarker,close));assert(!gui.TakeClipboardRequest(ActionMarker,out));
        consoleObject.open=false;captured('v',false,{});captured('v',true,{true,false,false,false});assert(gui.TakeClipboardRequest(ActionMarker,out));captured('v',false,{});
        uiNumberEditorSnapshot_t snapshot;assert(gui.QueryClipboardEditor(snapshot,error));const auto identity=snapshot.editor.identity;
        assert(gui.SetClipboardNotice(snapshot.target,NumberEditNotice::ClipboardReadFailed,error));
        assert(runtime.widgets.at("root").number->identity==identity && runtime.widgets.at("root").number->notice==NumberEditNotice::ClipboardReadFailed);
        auto stale=snapshot.target;++stale.modal;assert(!gui.ReplaceClipboardSelection(stale,"forged",error));
        assert(gui.ReplaceClipboardSelection(snapshot.target,"1.5",error));assert(!gui.SetClipboardNotice(snapshot.target,NumberEditNotice::ClipboardReadFailed,error));
    }
    assert(views.empty());modelTemplate=original;eventPlans.clear();std::puts("Clipboard adapter: exact shortcuts, press-only quarantine, AltGr, current Number eligibility, actual FIFO/dismiss/focus and checked notice/replacement passed");
}
"""

def adapter_source():
    source=owner.adapter_source();old=owner.ADAPTER_MAIN;assert source.endswith(old);source=source[:-len(old)]
    init=adapter.MAIN[adapter.MAIN.index('int main() {'):adapter.MAIN.index('    CheckPresentationBridge();')]
    return source+'using namespace openq4::ui;\nstatic Runtime& Live(){assert(views.size()==1);return views.front()->runtime;}\n'+ADAPTER_MAIN+init+'\n    CheckClipboardKeys();\n}\n'

INTERACTION_MAIN=r"""
using namespace openq4::ui;
int main(){
    DocumentModel model;model.id="notice";model.root.id="root";Node number;number.id="number";
    number.control=Control{};number.control->role=ControlRole::Number;number.control->action="edit";
    number.control->value=Expression{};number.control->value->type=0;NumberSpec spec;spec.minimum=.5;spec.maximum=2;spec.maxBytes=32;number.control->widget=spec;
    for(auto state:{ControlState::Default,ControlState::Hover,ControlState::Focus,ControlState::Pressed,ControlState::Disabled})number.control->states[state]="feedback";
    model.root.children.push_back(number);Interaction input;input.Reset(model);std::string error;
    assert(input.SetReadbacks({{"number",{1.0,false,{}}}},error));input.SetBounds({{"number",{0,0,100,30,true}}});assert(input.Focus("number"));assert(input.BeginNumberEdit("number",error));
    const auto view=[&]{return *input.Widget("number")->number;};const auto id=[&]{return view().identity;};
    auto original=view();assert(input.SetNumberNotice("number",id(),NumberEditNotice::ClipboardWriteFailed,error));
    assert(id()==original.identity && view().state.text==original.state.text && !view().canUndo && !view().canRedo);
    assert(input.SetReadbacks({{"number",{1.0,false,{}}}},error));assert(view().notice==NumberEditNotice::ClipboardWriteFailed);
    assert(!input.SetNumberNotice("number",{id().session,id().revision+1},NumberEditNotice::None,error));
    assert(!input.SetNumberNotice("number",id(),static_cast<NumberEditNotice>(99),error));assert(view().notice==NumberEditNotice::ClipboardWriteFailed);
    ValueWidgetSnapshot saved;assert(input.CaptureWidgets(saved,error));
    assert(input.ReplaceNumberSelection("number",id(),"1.25",error));assert(view().notice==NumberEditNotice::None && view().canUndo && input.TakeActions().empty());
    assert(input.SetNumberNotice("number",id(),NumberEditNotice::ClipboardRejected,error));auto before=view();
    assert(!input.ReplaceNumberSelection("number",id(),"x\ny",error));assert(id()==before.identity && view().state.text==before.state.text && view().notice==before.notice && view().canUndo);
    TextEditOperation noop;noop.kind=TextEditOperation::Kind::Selection;noop.anchor=before.state.anchor;noop.caret=before.state.caret;noop.changed=false;
    assert(input.ApplyNumberOperation("number",id(),noop,error));assert(id()==before.identity && view().notice==NumberEditNotice::None);
    assert(input.SetNumberNotice("number",id(),NumberEditNotice::ClipboardReadFailed,error));assert(input.UndoNumberEdit("number",id(),false,error));assert(view().notice==NumberEditNotice::None && view().canRedo);
    TextInputEvent pre;assert(MakeTextInputPreedit("a",TextIndexUnit::Utf8Bytes,0,1,pre,error));assert(input.ApplyNumberInput("number",id(),pre,error));
    assert(!input.SetNumberNotice("number",id(),NumberEditNotice::ClipboardReadFailed,error));
    TextInputEvent cancel;MakeTextInputCancel(cancel);assert(input.ApplyNumberInput("number",id(),cancel,error));
    assert(input.SetNumberNotice("number",id(),NumberEditNotice::ClipboardReadFailed,error));input.Cancel();assert(!view().active && view().notice==NumberEditNotice::None);
    input.SetBounds({{"number",{0,0,100,30,true}}});assert(input.Focus("number"));assert(input.BeginNumberEdit("number",error));
    assert(input.SetNumberNotice("number",id(),NumberEditNotice::ClipboardRejected,error));assert(input.RestoreWidgets(saved,error));
    assert(!view().active && view().notice==NumberEditNotice::None); // durable buffer contains no transient service notice
    std::puts("Actual Interaction: transient localized notices preserve identity/history, survive unchanged readback, clear on edit/undo/detach and never restore from snapshots passed");
}
"""

def interaction_source():
    document=(ROOT/'src/ui/retained/Document.cpp').read_text(encoding='utf-8')
    return '#include "src/ui/retained/Interaction.h"\n#include <cassert>\n#include <cmath>\n#include <cstdio>\nnamespace openq4::ui {\n'+function_body(document,'bool Utf8(')+function_body(document,'bool ValidStateValue(')+'}\n'+INTERACTION_MAIN


def validation_source():
    production=(ROOT/'src/ui/retained/NumberControlView.cpp').read_text(encoding='utf-8')
    start=production.index('\t\tconst bool invalid=');end=production.index('\t\tconst bool textChanged=',start)
    body=production[start:end]
    return r"""
#include "src/ui/retained/Interaction.h"
#include <cassert>
#include <cstdio>
using namespace openq4::ui;
struct Mock {
    struct String {std::string text;};
    std::map<std::string,std::map<std::string,String>> authored{{"validation",{{"text",{"#str_authored"}}}}};
    std::function<std::string(const NumberSpec&,const NumberEditView&,bool)> validation;
    std::function<std::string(const std::string&)> translate=[](const auto& key){return "translated:"+key;};
    std::string text;bool visible=false;
    bool Text(const std::string&,const std::string& next){bool changed=text!=next;text=next;return changed;}
    bool Display(const std::string&,bool next){bool changed=visible!=next;visible=next;return changed;}
} mock;
static bool Paint(const WidgetViewState& widget){
    const auto* view=&widget;const auto* edit=view->number ? &*view->number : nullptr;Mock* impl=&mock;
    NumberSpec spec;spec.validation="validation";bool changed=false;
"""+body+r"""
    return changed;
}
int main(){
    WidgetViewState widget;widget.number=NumberEditView{};widget.number->status=TextNumberStatus::Valid;
    assert(!Paint(widget) && !mock.visible);
    const std::vector<std::pair<NumberEditNotice,std::string>> notices={{NumberEditNotice::ClipboardReadFailed,"#str_230001"},{NumberEditNotice::ClipboardWriteFailed,"#str_230002"},{NumberEditNotice::ClipboardRejected,"#str_230003"}};
    for(const auto& [notice,key]:notices){widget.number->notice=notice;assert(Paint(widget) && mock.visible && mock.text=="translated:"+key);assert(!Paint(widget));}
    widget.number->notice=NumberEditNotice::None;widget.number->status=TextNumberStatus::Invalid;
    assert(Paint(widget) && mock.text=="translated:#str_authored" && mock.visible);
    widget.number->status=TextNumberStatus::Valid;assert(Paint(widget) && !mock.visible && mock.text.empty());
    std::puts("Actual Number view feedback block: fixed localized clipboard categories, stable repeated paint, authored numeric fallback and clear passed");
}
"""


def main():
    compiler=shutil.which('clang++') or shutil.which('g++');assert compiler
    temp=Path(tempfile.mkdtemp(prefix='ui-number-clipboard-',dir=ROOT/'.tmp'))
    env={**os.environ,'TEMP':str(temp),'TMP':str(temp),'TMPDIR':str(temp)}
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    paths=[p for p in (ROOT/'src/ui').rglob('*') if p.is_file()]+[ROOT/'tools/tests/ui_number_clipboard.py',ROOT/'tools/tests/ui_manager_lifecycle.py',ROOT/'tools/tests/ui_retained_adapter.py',ROOT/'tools/tests/ui_text_owner.py']
    paths += list((ROOT/'content/baseoq4/pak0/strings').glob('*_openq4.lang'))
    for language in paths:
        if language.suffix=='.lang':
            text=language.read_text(encoding='utf-8')
            for key in ('#str_230001','#str_230002','#str_230003'):assert text.count('"'+key+'"')==1,(language,key)
    sources={p.relative_to(ROOT).as_posix():digest(p) for p in paths}
    manager_code,adapter_code=manager_source(),adapter_source()
    cases=[('manager',manager_code,False),('adapter',adapter_code,False),('interaction',interaction_source(),False),('validation',validation_source(),False)]
    mutations=[
        ('manager-cut-after-refused-write',manager_code,'succeeded = openq4::SDL3_WriteTextClipboard(selection,error);','openq4::SDL3_WriteTextClipboard(selection,error); succeeded = true;'),
        ('manager-empty-paste-deletes',manager_code,'request.operation == uiClipboardOperation_t::Paste && !paste.empty()','request.operation == uiClipboardOperation_t::Paste'),
        ('manager-allocation-reused',manager_code,'allocations[i] == gui && allocations[i]->allocationId == allocation','allocations[i] == gui'),
        ('manager-owner-not-checked',manager_code,'out.target == request.target &&','true &&'),
        ('manager-postquery-skipped',manager_code,'if (!query(after) || after.editor.state.text != state.text ||','after = before; if (false || after.editor.state.text != state.text ||'),
        ('manager-native-error-is-success',manager_code,'succeeded = openq4::SDL3_ReadTextClipboard(paste,error);','openq4::SDL3_ReadTextClipboard(paste,error); succeeded = true;'),
        ('manager-reentry-not-retired',manager_code,'if (clipboardBoundaryActive) { clipboardBoundaryFailed = true; return true; }','if (clipboardBoundaryActive) { return true; }'),
        ('adapter-repeat-transfers',adapter_code,'else if (claim == Input::TextKey::Press)','else if (true)'),
        ('adapter-altgr-transfers',adapter_code,' || !editing || alt',' || !editing'),
        ('adapter-fifo-boundary-lost',adapter_code,'if (pending.clipboard) {','if (false) {'),
        ('adapter-close-loses-committed-suffix',adapter_code,'if (closeRequested) continue;','if (closeRequested) break;'),
        ('adapter-composition-query-admitted',adapter_code,'!current || !current->modalToken || current->editor.composition','!current || !current->modalToken'),
    ]
    for name,body,old,new in mutations:
        assert body.count(old)==1,(name,body.count(old));cases.append((name,body.replace(old,new),True))
    report={'passed':False,'scope':__doc__,'sources':sources,'cases':[]}
    try:
        for name,body,mutant in cases:
            source=temp/(name+'.cpp');source.write_text(body,encoding='utf-8',newline='\n');binary=temp/(name+('.exe' if os.name=='nt' else '-test'))
            command=[compiler,'-std=c++20','-I',str(ROOT),str(source),str(ROOT/'src/ui/retained/TextInput.cpp'),str(ROOT/'src/ui/retained/TextEdit.cpp'),'-o',str(binary)]
            if name.startswith('adapter'):command[1:1]=['-DUSE_SDL3'];command[-2:-2]=[str(ROOT/'src/ui/retained/Input.cpp'),str(ROOT/'src/ui/retained/TextEditCommand.cpp')]
            if name.startswith('interaction'):command[-2:-2]=[str(ROOT/'src/ui/retained/Interaction.cpp')]
            if os.name!='nt':command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            built=subprocess.run(command,env=env,capture_output=True,text=True,timeout=120);log=temp/(name+'-compile.log');log.write_text(built.stdout+built.stderr,encoding='utf-8')
            record={'name':name,'command':command,'compile_exit':built.returncode,'compile_log':str(log),'compile_log_sha256':digest(log),'source_sha256':digest(source)};report['cases'].append(record)
            if built.returncode:raise RuntimeError(built.stdout+built.stderr)
            run=subprocess.run([str(binary)],env=env,capture_output=True,text=True,timeout=90);log=temp/(name+'-run.log');log.write_text(run.stdout+run.stderr,encoding='utf-8');record.update(exit=run.returncode,run_log=str(log),run_log_sha256=digest(log),binary_sha256=digest(binary))
            if mutant:
                if not run.returncode or 'assertion' not in (run.stdout+run.stderr).lower():raise RuntimeError('Mutation did not fail a behavioral assertion: '+name+' '+run.stdout+run.stderr)
                print('Rejected compiled clipboard mutation: '+name)
            else:
                if run.returncode:raise RuntimeError(run.stdout+run.stderr or f'{name} exited {run.returncode}')
                print(run.stdout.strip())
        report['passed']=True
    except Exception as e:report['failure']=str(e);print(e)
    report['sources_unchanged']=sources=={p.relative_to(ROOT).as_posix():digest(p) for p in paths};report['passed'] &= report['sources_unchanged']
    result=temp/'result.json';result.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(result);return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())

