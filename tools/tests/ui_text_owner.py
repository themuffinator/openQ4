#!/usr/bin/env python3
"""Compile actual manager, deferred and retained text-owner boundaries.

Manager registry/deferred code and retained adapter bodies are unchanged;
recording engine/runtime stand-ins exercise lifetime, freshness and receipt
ordering. The runtime query/prepare bodies are separately compiled against
counted host/interaction boundaries. Actual TextInput/TextEdit and broker code
validate payloads and receipts. No SDL, Session, Rml rendering or IME claim.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile

import ui_manager_lifecycle as manager
import ui_retained_adapter as adapter
from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

MANAGER_EDITOR = r'''
    std::uint64_t textBackend=UI_NextTextLifetime(),textDocument=UI_NextTextLifetime();
    std::uint64_t textModal=11,textSession=12,textRevision=13;
    int textQueries=0,textWrites=0;
    bool textAvailable=true,textReject=false,textNoChange=false;
    std::string text;
    std::function<void()> onTextQuery,onTextApply;
    openq4::ui::TextBrokerContext QueryTextContext(std::uint64_t allocation,std::uint64_t window,std::uint64_t session) override {
        ++textQueries;
        openq4::ui::TextBrokerContext result{openq4::ui::TextBrokerRoute::Retained,window,session,{}};
        if(textAvailable)result.editor=openq4::ui::TextEditorIdentity{allocation,textBackend,textDocument,textModal,window,textSession,textRevision,"number"};
        auto callback=onTextQuery;if(callback)callback();return result;
    }
    bool ApplyTextInput(const openq4::ui::TextBrokerContext&,const openq4::ui::TextInputEvent& input,std::string&) override {
        ++textWrites;
        if(textReject)return false;
        if(!textNoChange) {text=input.text;++textRevision;}
        auto callback=onTextApply;if(callback)callback();return true;
    }
'''

MANAGER_MAIN = r'''
using namespace openq4::ui;
int main() {
    auto& m=uiManagerLocal;
    assert(!UI_QueryTextContext(nullptr,5,7).editor);
    assert(!UI_QueryTextContext(reinterpret_cast<idUserInterface*>(0x1234),5,7).editor);
    auto* a=new idUserInterfaceRetained;
    auto* b=new idUserInterfaceRetained;
    auto context=UI_QueryTextContext(a,5,7);assert(context.editor);
    assert(context.editor->allocation!=UI_QueryTextContext(b,5,7).editor->allocation);
    TextInputEvent input;std::string error;assert(MakeTextInputCommit("1.25",input,error));
    TextBrokerDelivery delivery{1,2,*context.editor,input};
    const auto changed=UI_DeliverTextInput(a,5,7,context,delivery);
    assert(changed.outcome==TextDeliveryOutcome::AppliedChanged && changed.context.editor->revision>context.editor->revision);
    assert(a->textWrites==1 && a->text=="1.25");
    assert(UI_DeliverTextInput(a,5,7,context,delivery).outcome==TextDeliveryOutcome::Rejected && a->textWrites==1);
    assert(UI_DeliverTextInput(b,5,7,context,delivery).outcome==TextDeliveryOutcome::Rejected && b->textWrites==0);
    context=UI_QueryTextContext(a,5,7);delivery.target=*context.editor;
    for(int field=0;field<9;++field) {
        auto stale=delivery;
        switch(field) {
        case 0:++stale.target.allocation;break;case 1:++stale.target.backend;break;
        case 2:++stale.target.document;break;case 3:++stale.target.modal;break;
        case 4:++stale.target.window;break;case 5:++stale.target.session;break;
        case 6:++stale.target.revision;break;case 7:stale.target.control="other";break;
        case 8:stale.token=0;break;
        }
        auto authorization=context;authorization.editor=stale.target;
        assert(UI_DeliverTextInput(a,5,7,authorization,stale).outcome==TextDeliveryOutcome::Rejected && a->textWrites==1);
    }
    assert(UI_DeliverTextInput(a,6,7,context,delivery).outcome==TextDeliveryOutcome::Rejected && a->textWrites==1);
    assert(UI_DeliverTextInput(a,5,8,context,delivery).outcome==TextDeliveryOutcome::Rejected && a->textWrites==1);
    auto mismatch=delivery;++mismatch.target.backend;
    assert(UI_DeliverTextInput(a,5,7,context,mismatch).outcome==TextDeliveryOutcome::Rejected && a->textWrites==1);
    auto invalid=delivery;invalid.input.text=std::string("1\0x",3);
    assert(UI_DeliverTextInput(a,5,7,context,invalid).outcome==TextDeliveryOutcome::Rejected && a->textWrites==1);
    a->textNoChange=true;
    auto noChange=UI_DeliverTextInput(a,5,7,context,delivery);
    assert(noChange.outcome==TextDeliveryOutcome::AppliedNoChange && noChange.context==context);
    a->textReject=true;
    auto rejected=UI_DeliverTextInput(a,5,7,context,delivery);
    assert(rejected.outcome==TextDeliveryOutcome::Rejected && rejected.context==context);
    a->textReject=false;a->textNoChange=false;
    auto immutable=delivery;auto authorized=context;
    a->onTextQuery=[&]{immutable.input.text="forged";authorized.nativeSession=999;};
    auto stable=UI_DeliverTextInput(a,5,7,authorized,immutable);a->onTextQuery={};
    assert(stable.outcome==TextDeliveryOutcome::AppliedChanged && a->text=="1.25");
    context=UI_QueryTextContext(a,5,7);delivery.target=*context.editor;
    // Nested synchronization must invalidate the outer query/delivery as well.
    a->onTextQuery=[&]{assert(!UI_QueryTextContext(b,5,7).editor);};
    assert(!UI_QueryTextContext(a,5,7).editor);
    const int writes=a->textWrites;
    assert(UI_DeliverTextInput(a,5,7,context,delivery).outcome==TextDeliveryOutcome::Rejected && a->textWrites==writes);
    a->onTextQuery={};
    a->onTextApply=[&]{UI_QueryTextContext(b,5,7);};
    assert(!UI_DeliverTextInput(a,5,7,context,delivery).context.editor);
    a->onTextApply={};
    a->onTextQuery=[] {throw std::runtime_error("host query");};
    assert(!UI_QueryTextContext(a,5,7).editor);a->onTextQuery={};
    // Actual broker owns the one-use receipt; the manager reports observed data.
    TextInputBroker broker;context=UI_QueryTextContext(a,5,7);
    assert(broker.Synchronize(context,error));
    TextNativeRecord native;native.kind=TextNativeKind::SessionBegin;native.sequence=1;native.window=5;native.session=7;
    assert(broker.Process(native).disposition==TextBrokerDisposition::Ignored);
    native.kind=TextNativeKind::Commit;native.origin=TextNativeOrigin::ProvenDirect;native.sequence=2;native.input=input;
    auto dispatched=broker.Process(native);assert(dispatched.delivery);
    auto receipt=UI_DeliverTextInput(a,5,7,context,*dispatched.delivery);
    assert(broker.Complete(dispatched.delivery->token,receipt.outcome,receipt.context,error));
    assert(!broker.Complete(dispatched.delivery->token,receipt.outcome,receipt.context,error));
    // Backend replacement after a write is reported honestly and poisons receipt.
    TextInputBroker changedOwner;context=UI_QueryTextContext(a,5,7);
    assert(changedOwner.Synchronize(context,error));native.kind=TextNativeKind::SessionBegin;native.origin=TextNativeOrigin::Unknown;
    native.input=TextInputEvent{};native.sequence=1;changedOwner.Process(native);
    native.kind=TextNativeKind::Commit;native.origin=TextNativeOrigin::ProvenDirect;native.sequence=2;native.input=input;
    dispatched=changedOwner.Process(native);assert(dispatched.delivery);
    a->onTextApply=[&]{a->textDocument=UI_NextTextLifetime();};
    receipt=UI_DeliverTextInput(a,5,7,context,*dispatched.delivery);a->onTextApply={};
    assert(!changedOwner.Complete(dispatched.delivery->token,receipt.outcome,receipt.context,error));
    // Current allocation address can be reused; a stale token never follows it.
    context=UI_QueryTextContext(a,5,7);delivery.target=*context.editor;
    auto* old=a;recycleAllocation=true;delete a;a=new idUserInterfaceRetained;
    assert(a==old && UI_QueryTextContext(a,5,7).editor->allocation!=delivery.target.allocation);
    assert(UI_DeliverTextInput(a,5,7,context,delivery).outcome==TextDeliveryOutcome::Rejected && a->textWrites==0);
    // Invalidate during a query without retaining a registry iterator/reference.
    a->onTextQuery=[&] {m.UnregisterGui(a);};
    assert(!UI_QueryTextContext(a,5,7).editor);a->onTextQuery={};
    delete a;recycleAllocation=false;if(recycledAllocation){::operator delete(recycledAllocation);recycledAllocation=nullptr;}
    delete b;
    auto* deferred=new idUserInterfaceDeferred;assert(deferred->InitFromFile("one.q4ui",true,true));
    auto first=UI_QueryTextContext(deferred,5,7);assert(first.editor);
    assert(deferred->InitFromFile("two.q4ui",true,true));auto second=UI_QueryTextContext(deferred,5,7);
    assert(second.editor && second.editor->allocation==first.editor->allocation && second.editor->backend!=first.editor->backend);
    delivery.target=*first.editor;assert(UI_DeliverTextInput(deferred,5,7,first,delivery).outcome==TextDeliveryOutcome::Rejected);
    assert(deferred->InitFromFile("parent.gui",true,true));deferred->Activate(true,0);
    assert(UI_QueryTextContext(deferred,5,7).route==TextBrokerRoute::Legacy);delete deferred;
    assert(m.allocations.Num()==0);
    m.nextAllocationId=(std::numeric_limits<unsigned long long>::max)();
    bool threw=false;try{new idUserInterfaceRetained;}catch(const std::runtime_error&){threw=true;}
    assert(threw && m.allocations.Num()==0 && m.nextAllocationId==(std::numeric_limits<unsigned long long>::max)());
    std::puts("Managed text owner: current registry, all identity fields, address reuse, deferred backend replacement, reentry, exceptions, exhaustion and actual broker receipts passed");
}
'''

RUNTIME_QUERY_STUB = r'''
    int textQueries=0;
    bool textHostAvailable=true;
    std::function<void()> beforeTextQuery,beforeTextApply;
    std::optional<NumberEditorContext> QueryNumberEditor(std::string& error,double) {
        ++textQueries;if(beforeTextQuery)beforeTextQuery();error.clear();
        if(!textHostAvailable){error="host unavailable";return {};}
        const auto found=widgets.find(selected);
        if(!loaded || found==widgets.end() || disabledControls.contains(selected) || !found->second.number ||
           !found->second.number->active || found->second.number->conflict || found->second.pending)return {};
        return NumberEditorContext{selected,*found->second.number,modalIdentity};
    }
'''

ADAPTER_MAIN = r'''
using namespace openq4::ui;
static Runtime& Live() {assert(views.size()==1);return views.front()->runtime;}
static void Install() {
    auto& r=Live();r.InstallNumber("number","brightness");r.FocusControl("number",1);
    std::string error;assert(r.BeginNumberEdit("number",error,1));
}
int main() {
    files.sources["test.q4ui"]="source";files.sources["next.q4ui"]="next";
    modelTemplate.id="number-test";modelTemplate.root.id="root";
    modelTemplate.root.control=Control{};
    modelTemplate.root.control->action="brightness";
    Expression number;number.state="number";number.type=0;
    modelTemplate.actions["brightness"]={"settings.brightness.set",{{"value",number}}};
    modelTemplate.state["number"]={1.0,""};
    std::string error;TextInputEvent input;assert(MakeTextInputCommit("1.25",input,error));
    idUserInterfaceRetained gui;
    assert(!gui.QueryTextContext(77,5,7).editor);
    assert(gui.InitFromFile("test.q4ui"));gui.Activate(true,0);
    assert(!gui.QueryTextContext(77,5,7).editor);
    Install();auto context=gui.QueryTextContext(77,5,7);assert(context.editor);
    assert(context.editor->allocation==77 && context.editor->window==5 && context.nativeSession==7);
    const auto beforeCalls=Live().numberCalls.size();
    assert(gui.ApplyTextInput(context,input,error));
    auto after=gui.QueryTextContext(77,5,7);assert(after.editor && after.editor->revision>context.editor->revision);
    assert(Live().numberBuffers.at("number").State().text=="1.25" && Live().numberCalls.size()==beforeCalls+1);
    assert(Live().actions.empty() && cvars.writes==0);
    assert(!gui.ApplyTextInput(context,input,error));
    for(int field=0;field<6;++field) {
        auto stale=after;
        switch(field){case 0:++stale.editor->backend;break;case 1:++stale.editor->document;break;case 2:++stale.editor->modal;break;
        case 3:++stale.editor->session;break;case 4:++stale.editor->revision;break;case 5:stale.editor->control="other";break;}
        assert(!gui.ApplyTextInput(stale,input,error));
    }
    // A native clear changes local presentation only; there is no settings commit.
    auto preedit=input;assert(MakeTextInputPreedit("2",TextIndexUnit::UnicodeScalars,0,1,preedit,error));
    assert(gui.ApplyTextInput(after,preedit,error));after=gui.QueryTextContext(77,5,7);
    assert(Live().numberBuffers.at("number").Composition());
    assert(MakeTextInputPreedit("",TextIndexUnit::UnicodeScalars,0,0,preedit,error));
    assert(gui.ApplyTextInput(after,preedit,error));after=gui.QueryTextContext(77,5,7);
    assert(!Live().numberBuffers.at("number").Composition() && Live().actions.empty());
    // Fresh readback can retire an edit between query and mutation.
    Live().beforeTextQuery=[&]{++Live().widgets.at("number").number->identity.revision;};
    assert(!gui.ApplyTextInput(after,input,error));Live().beforeTextQuery={};
    after=gui.QueryTextContext(77,5,7);const auto text=Live().numberBuffers.at("number").State().text;
    Live().beforeTextApply=[&]{++Live().widgets.at("number").number->identity.revision;};
    assert(!gui.ApplyTextInput(after,input,error) && Live().numberBuffers.at("number").State().text==text);
    Live().beforeTextApply={};
    Live().textHostAvailable=false;assert(!gui.QueryTextContext(77,5,7).editor);Live().textHostAvailable=true;
    Live().disabledControls.insert("number");assert(!gui.QueryTextContext(77,5,7).editor);Live().disabledControls.clear();
    Live().widgets.at("number").number->active=false;const auto count=Live().numberCalls.size();
    assert(!gui.QueryTextContext(77,5,7).editor && Live().numberCalls.size()==count);
    Install();after=gui.QueryTextContext(77,5,7);
    consoleObject.open=true;assert(!gui.QueryTextContext(77,5,7).editor);consoleObject.open=false;
    Install();assert(gui.QueryTextContext(77,5,7).editor);
    windowFocused=false;assert(!gui.QueryTextContext(77,5,7).editor);windowFocused=true;
    Install();after=gui.QueryTextContext(77,5,7);
    idFile_Memory save;assert(gui.WriteToSaveGame(&save));
    assert(gui.QueryTextContext(77,5,7).editor->document==after.editor->document);
    Live().failSave=true;idFile_Memory failed;assert(!gui.WriteToSaveGame(&failed));Live().failSave=false;
    assert(gui.QueryTextContext(77,5,7).editor->document==after.editor->document);
    Live().failRestore=true;save.position=0;assert(!gui.ReadFromSaveGame(&save));Live().failRestore=false;
    assert(gui.QueryTextContext(77,5,7).editor->document==after.editor->document);
    save.position=0;assert(gui.ReadFromSaveGame(&save));Install();auto restored=gui.QueryTextContext(77,5,7);
    assert(restored.editor->document!=after.editor->document && restored.editor->backend==after.editor->backend);
    assert(!gui.ApplyTextInput(after,input,error));
    after=restored;auto* view=views.front();view->callback(view->owner,retainedUIViewEvent_t::BeforeResourceReset);
    Live().loaded=false;assert(!gui.QueryTextContext(77,5,7).editor);
    Live().loaded=true;view->callback(view->owner,retainedUIViewEvent_t::Restored);Install();restored=gui.QueryTextContext(77,5,7);
    assert(restored.editor->document!=after.editor->document && restored.editor->backend==after.editor->backend);
    rejectLoad=true;assert(!gui.InitFromFile("next.q4ui"));rejectLoad=false;
    assert(gui.QueryTextContext(77,5,7).editor->document==restored.editor->document);
    assert(gui.InitFromFile("next.q4ui"));Install();after=gui.QueryTextContext(77,5,7);
    assert(after.editor->document!=restored.editor->document && after.editor->backend==restored.editor->backend);
    gui.Activate(false,0);assert(!gui.QueryTextContext(77,5,7).editor);
    std::puts("Retained text owner: active Number freshness, stale dispatch, no auto-begin/settings write, focus/console/eligibility, save/restore/source/resource lifetimes passed");
}
'''


def manager_source(main=MANAGER_MAIN):
    source = manager.production_source()
    source = '#include "src/ui/UserInterfaceText.h"\n#include <limits>\n' + source
    declarations = '''
    openq4::ui::TextBrokerContext QueryTextContext(idUserInterface*,std::uint64_t,std::uint64_t);
    uiTextDeliveryResult_t DeliverTextInput(idUserInterface*,std::uint64_t,std::uint64_t,const openq4::ui::TextBrokerContext&,const openq4::ui::TextBrokerDelivery&);
    bool textBoundaryActive=false,textBoundaryFailed=false;
'''
    source = source.replace('class idUserInterfaceManagerLocal {\npublic:', 'class idUserInterfaceManagerLocal {\npublic:' + declarations)
    source = source.replace('class idUserInterfaceRetained : public idUserInterfaceLocal {\npublic:',
                            'class idUserInterfaceRetained : public idUserInterfaceLocal {\npublic:' + MANAGER_EDITOR)
    
    production = (ROOT / 'src/ui/UserInterface.cpp').read_text(encoding='utf-8')
    for signature in ('std::uint64_t UI_NextTextLifetime(', 'openq4::ui::TextBrokerContext UI_QueryTextContext(',
                      'uiTextDeliveryResult_t UI_DeliverTextInput(', 'openq4::ui::TextBrokerContext idUserInterfaceManagerLocal::QueryTextContext(',
                      'uiTextDeliveryResult_t idUserInterfaceManagerLocal::DeliverTextInput('):
        source += function_body(production, signature)
    return source + main


def dedicated_source():
    # Link without the retained runtime/models: dedicated has no native field
    # consumer and must not acquire a client rendering/text dependency.
    return '#define ID_DEDICATED 1\n' + manager_source(r'''
using namespace openq4::ui;
int main() {
    auto* gui=new idUserInterfaceRetained;
    TextEditorIdentity target{1,2,3,4,5,6,7,"number"};
    TextBrokerContext authorization{TextBrokerRoute::Retained,5,8,target};
    TextBrokerDelivery delivery{1,2,target,{TextInputKind::Commit,"1.25",{}, {}}};
    for(auto* current:{static_cast<idUserInterface*>(gui),reinterpret_cast<idUserInterface*>(0x1234)}) {
        assert(UI_QueryTextContext(current,5,8).route==TextBrokerRoute::Unavailable);
        const auto result=UI_DeliverTextInput(current,5,8,authorization,delivery);
        assert(result.outcome==TextDeliveryOutcome::Rejected && result.context.route==TextBrokerRoute::Unavailable);
        assert(!result.diagnostic.empty());
    }
    assert(gui->textQueries==0 && gui->textWrites==0);
    delete gui;
    std::puts("Dedicated text boundary: no owner dereference/delivery or retained model link dependency passed");
}
''')


def adapter_source():
    support = adapter.DICTIONARY_SUPPORT[:adapter.DICTIONARY_SUPPORT.index('struct idFile {')]
    support = support.replace('int evType=0,evValue=0,evValue2=0;', 'int evType=0,evValue=0,evValue2=0,evPtrLength=0; void* evPtr=nullptr;')
    support = support.replace('static int Icmpn(const char* a,const char* b,int length) { return Icmp(std::string(a,length).c_str(),std::string(b,length).c_str()); }', '''static int Icmpn(const char* a,const char* b,int length) {
        for(int i=0;i<length;++i) {
            const int x=std::tolower(static_cast<unsigned char>(a[i])),y=std::tolower(static_cast<unsigned char>(b[i]));
            if(x!=y || !x || !y)return x-y;
        }
        return 0;
    }''')
    runtime = adapter.RUNTIME
    runtime = runtime.replace(function_body(runtime, 'std::optional<NumberEditorContext> QueryNumberEditor('), '')
    runtime = runtime.replace('class Runtime {\npublic:', 'class Runtime {\npublic:' + RUNTIME_QUERY_STUB)
    runtime = runtime.replace('numberCalls.push_back({"input",', 'if(beforeTextApply)beforeTextApply();\n        numberCalls.push_back({"input",')
    source = '#include "src/ui/UserInterfaceText.h"\n' + support + adapter.ENGINE + runtime + adapter.SETTINGS
    for path, signature in (('UserInterface.h', 'class idUserInterface {'),
                            ('UserInterfaceManaged.h', 'class idUserInterfaceManaged :'),
                            ('UserInterfaceRetained.h', 'class idUserInterfaceRetained final :')):
        source += function_body((ROOT / 'src/ui' / path).read_text(encoding='utf-8'), signature) + ';\n'
    source += adapter.BASE
    factory = (ROOT / 'src/ui/UserInterface.cpp').read_text(encoding='utf-8')
    source += function_body(factory, 'std::uint64_t UI_NextTextLifetime(')
    source += function_body(factory, 'bool UI_IsRetainedPath(')
    production = (ROOT / 'src/ui/UserInterfaceRetained.cpp').read_text(encoding='utf-8')
    source += production[production.index('namespace {'):production.index('bool UI_RetainedDiagnostic(')]
    return source + ADAPTER_MAIN


def runtime_source():
    source = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include "src/ui/retained/Interaction.h"
using namespace openq4::ui;
'''
    header = (ROOT / 'src/ui/retained/Runtime.h').read_text(encoding='utf-8')
    production = (ROOT / 'src/ui/retained/Runtime.cpp').read_text(encoding='utf-8')
    interaction = (ROOT / 'src/ui/retained/Interaction.h').read_text(encoding='utf-8')
    source += function_body(header, 'struct NumberEditorContext {') + ';\n'
    source += '''
struct RecordedInteraction {
    std::uint64_t modalToken=21;bool modalBlocked=false,eligible=true;
    std::string focused="number";std::optional<WidgetViewState> widget;
    std::string Focused() const {return focused;}
    std::optional<WidgetViewState> Widget(const std::string&) const {return widget;}
    bool CanActivate(const std::string&) const {return eligible;}
'''
    source += function_body(interaction, 'std::uint64_t ModalToken() const') + '\n};\n'
    source += '''
struct Runtime {
    struct Impl {
        bool canonical=true,document=true;RecordedInteraction interaction;
        std::string stateError;int reads=0,updates=0;std::function<void()> onRead;
        void ReadStateSources(){++reads;if(onRead)onRead();}
        void UpdateInteraction(double){++updates;}
'''
    source += function_body(production, 'bool PrepareNumberEdit(') + '''
    } storage,*impl=&storage;
    std::optional<NumberEditorContext> QueryNumberEditor(std::string&,double);
};
'''
    source += function_body(production, 'std::optional<NumberEditorContext> Runtime::QueryNumberEditor(')
    source += r'''
int main() {
    Runtime runtime;auto& impl=runtime.storage;auto& interaction=impl.interaction;std::string error;
    WidgetViewState widget;widget.role=ControlRole::Number;widget.number=NumberEditView{};
    widget.number->active=true;widget.number->identity={10,11};widget.number->state.text="1.25";
    interaction.widget=widget;
    auto view=runtime.QueryNumberEditor(error,1);assert(view && error.empty() && impl.reads==1 && impl.updates==1);
    assert(view->control=="number" && view->modalToken==21 && view->editor.identity==widget.number->identity);
    view->editor.state.text="copy";assert(interaction.widget->number->state.text=="1.25");
    impl.onRead=[&]{interaction.widget->number->identity.revision=33;};
    view=runtime.QueryNumberEditor(error,1);assert(view && view->editor.identity.revision==33);impl.onRead={};
    for(int unavailable=0;unavailable<13;++unavailable) {
        interaction.widget=widget;interaction.eligible=true;interaction.modalBlocked=false;interaction.modalToken=21;
        switch(unavailable) {
        case 0:interaction.widget.reset();break;
        case 1:interaction.widget->role=ControlRole::Button;break;
        case 2:interaction.widget->number.reset();break;
        case 3:interaction.widget->number->active=false;break;
        case 4:interaction.widget->pending=1.0;break;
        case 5:interaction.widget->number->identity.session=0;break;
        case 6:interaction.widget->number->identity.revision=0;break;
        case 7:interaction.eligible=false;break;
        case 8:interaction.modalToken=0;break;
        case 9:interaction.modalBlocked=true;break;
        case 10:interaction.widget->number->conflict=true;break;
        case 11:interaction.widget->number->nativePresentation=NativeTextSnapshot{};break;
        case 12:interaction.widget->number->nativeUnsettled=true;break;
        }
        assert(!runtime.QueryNumberEditor(error,1) && error.empty());
    }
    interaction.widget=widget;interaction.modalBlocked=false;
    impl.stateError="fresh source error";const auto updates=impl.updates;
    assert(!runtime.QueryNumberEditor(error,1) && error==impl.stateError && impl.updates==updates);
    impl.stateError.clear();
    for(double time:{-1.,std::nan(""),static_cast<double>(INFINITY)}) {
        const auto reads=impl.reads;assert(!runtime.QueryNumberEditor(error,time) && !error.empty() && reads==impl.reads);
    }
    impl.canonical=false;assert(!runtime.QueryNumberEditor(error,1));impl.canonical=true;
    impl.document=false;assert(!runtime.QueryNumberEditor(error,1));
    std::puts("Runtime editor query: actual host refresh/eligibility bodies, copied state, modal lease, inactive/pending/invalid owners and time failures passed");
}
'''
    return source


def main():
    compiler = next((x for name in ('clang++', 'g++', 'c++') if (x := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++20 compiler required')
    paths = ['src/ui/UserInterface.cpp', 'src/ui/UserInterfaceText.h', 'src/ui/UserInterfaceManaged.h', 'src/ui/UserInterfaceNativeText.h',
             'src/ui/UserInterfaceLocal.h', 'src/ui/UserInterfaceRetained.cpp', 'src/ui/UserInterfaceRetained.h',
             'src/ui/UserInterfaceDeferred.cpp', 'src/ui/UserInterfaceDeferred.h',
             'src/ui/retained/Runtime.cpp', 'src/ui/retained/Runtime.h', 'src/ui/retained/Interaction.h',
             'src/ui/retained/TextInputBroker.cpp', 'src/ui/retained/TextInputBroker.h',
             'src/ui/retained/TextInput.cpp', 'src/ui/retained/TextEdit.cpp', 'src/ui/retained/TextEditCommand.cpp',
             'tools/tests/ui_text_owner.py', 'tools/tests/ui_manager_lifecycle.py', 'tools/tests/ui_retained_adapter.py']
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    before = {path: digest(ROOT / path) for path in paths}
    folder = Path(tempfile.mkdtemp(prefix='ui-text-owner-', dir=ROOT / '.tmp'))
    env = {**os.environ, 'TEMP': str(folder), 'TMP': str(folder), 'TMPDIR': str(folder)}
    report = {'passed': False, 'scope': __doc__.strip(), 'sources': before, 'cases': []}
    try:
        sources = {'manager': manager_source(), 'adapter': adapter_source(), 'runtime': runtime_source(), 'dedicated': dedicated_source()}
        mutations = [
            ('manager', 'allocation', 'allocations[i]->allocationId == request.target.allocation', 'true'),
            ('manager', 'before-context', 'if (result.context != expected)', 'if (false)'),
            ('manager', 'native-session', 'const TextBrokerContext expected = authorizedContext;',
             'const TextBrokerContext expected{TextBrokerRoute::Retained,nativeWindow,nativeSession,delivery.target};'),
            ('manager', 'context-target', '*expected.editor != delivery.target', 'false'),
            ('manager', 'immutable-request', 'const TextBrokerDelivery request = delivery;', 'const TextBrokerDelivery& request = delivery;'),
            ('manager', 'immutable-context', 'const TextBrokerContext expected = authorizedContext;', 'const TextBrokerContext& expected = authorizedContext;'),
            ('manager', 'payload', 'if (!ValidateTextInputEvent(delivery.input,result.diagnostic)) return result;', '/* omitted validation */'),
            ('manager', 'reentry', 'if (textBoundaryFailed) return {};', 'if (false) return {};'),
            ('manager', 'deferred', 'backend->QueryTextContext(allocation,window,session)', 'idUserInterfaceManaged::QueryTextContext(allocation,window,session)'),
            ('adapter', 'fresh-apply', 'QueryTextContext(expected.editor->allocation,expected.nativeWindow,expected.nativeSession) != expected', 'false'),
            ('adapter', 'restore-lifetime', 'impl->textDocument = textDocument;\n\timpl->Quarantine(false,false,true);', 'impl->Quarantine(false,false,true);'),
            ('adapter', 'resource-lifetime', 'self.textDocument = UI_NextTextLifetime();', '/* old lease incorrectly retained */'),
            ('runtime', 'fresh-host', 'ReadStateSources();', '/* omitted fresh read */'),
            ('runtime', 'active', '!view->number->active', 'false'),
            ('runtime', 'pending', 'view->pending ||', 'false ||'),
            ('runtime', 'modal', 'return modalBlocked ? 0 : modalToken;', 'return modalToken;'),
            ('runtime', 'conflict', 'view->number->conflict ||', 'false ||'),
            ('runtime', 'native-presentation', 'view->number->nativePresentation ||', 'false ||'),
            ('runtime', 'native-unsettled', 'view->number->nativeUnsettled ||', 'false ||'),
        ]
        cases = [(name, source, False) for name, source in sources.items()]
        for unit, name, old, new in mutations:
            if sources[unit].count(old) != 1:
                raise RuntimeError('Nonunique production mutation anchor: ' + name)
            cases.append((unit + '-' + name, sources[unit].replace(old, new), True))
        for name, source, mutation in cases:
            path = folder / (name + '.cpp'); path.write_text(source, encoding='utf-8', newline='\n')
            binary = folder / (name + ('.exe' if os.name == 'nt' else '-test'))
            models = [] if name == 'dedicated' else [str(ROOT / ('src/ui/retained/' + x + '.cpp')) for x in ('Input', 'TextInput', 'TextEdit', 'TextEditCommand', 'TextInputBroker')]
            command = [compiler, '-std=c++20', '-DUSE_SDL3', '-I', str(ROOT), str(path), *models, '-o', str(binary)]
            if os.name != 'nt':
                command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=180)
            log = folder / (name + '-compile.log'); log.write_text(result.stdout + result.stderr, encoding='utf-8')
            entry = {'name': name, 'command': command, 'compile_exit': result.returncode,
                     'compile_log': str(log), 'compile_sha256': digest(log), 'extracted_sha256': digest(path)}
            report['cases'].append(entry)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, env=env, timeout=60)
            log = folder / (name + '-run.log'); log.write_text(result.stdout + result.stderr, encoding='utf-8')
            entry.update(exit_code=result.returncode, run_log=str(log), run_sha256=digest(log), binary_sha256=digest(binary))
            if mutation:
                if not result.returncode or 'assert' not in (result.stdout + result.stderr).lower():
                    raise RuntimeError('Compiled mutation did not fail a behavioral assertion: ' + name)
                print('Rejected compiled text-owner mutation:', name)
            else:
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout.strip())
        report['passed'] = True
    except Exception as error:
        report['failure'] = str(error); print(error)
    report['sources_unchanged'] = before == {path: digest(ROOT / path) for path in paths}
    report['passed'] &= report['sources_unchanged']
    path = folder / 'result.json'; path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Text owner evidence:', path)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
