#!/usr/bin/env python3
"""Exercise production retained GUI adapter state, event queues, saves and input.

The adapter/public headers and Input.cpp are real. Engine I/O, canonical
document loading and runtime rendering are bounded stand-ins; this does not
qualify alias expression ownership, parsing, GPU output or the complete
application action vocabulary. Event effects are prescribed boundary data;
UiBehaviorTest covers actual ordered evaluation and rollback. The settings
service below is a small ownership/dispatch stand-in, not the production
catalog, transaction, persistence, or device restart implementation.
Wheel regressions verify real adapter move/wheel handoff against a recording
Runtime double; the separate UiValueRuntimeTest owns popup selection/geometry.
Number diagnostics use the actual TextEdit/TextInput models behind counted
Runtime methods; native ownership and rendering remain separate qualifications.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import os
import hashlib
import json

from filesystem_case_segments import function_body
from ui_manager_lifecycle import SUPPORT as DICTIONARY_SUPPORT

ROOT = Path(__file__).resolve().parents[2]

ENGINE = r'''
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <iomanip>
#include <sstream>
#include <cstdarg>
#include <charconv>
#include "src/ui/retained/Input.h"
#include "src/ui/UserInterfaceText.h"
#include "src/ui/UserInterfaceClipboard.h"
#include "src/ui/UserInterfaceNativeText.h"
#include "src/ui/retained/TextEditCommand.h"
#include "src/sys/KeyEventMetadata.h"
#include "src/ui/RetainedUI.h"
#include "src/ui/SettingsService.h"
#include "src/ui/application/SettingsTransaction.h"
struct idCmdArgs {
    std::vector<std::string> values;
    int Argc() const {return static_cast<int>(values.size());}
    const char* Argv(int index) const {return values.at(index).c_str();}
};
template<class T> T Min(T a,T b) { return (std::min)(a,b); }
struct idVec2 { idVec2(float=0,float=0) {} } vec2_origin;
enum { SE_KEY=1,SE_MOUSE,K_TAB=10,K_SHIFT,K_UPARROW,K_DOWNARROW,K_LEFTARROW,K_RIGHTARROW,
       K_ENTER,K_KP_ENTER,K_SPACE,K_ESCAPE,K_MOUSE1,K_JOY3,K_JOY4,K_JOY7,K_JOY8,K_JOY9,K_JOY10,K_JOY11,K_JOY12,
       K_HOME,K_END,K_PGUP,K_PGDN,K_MWHEELUP,K_MWHEELDOWN,K_CTRL,K_ALT,K_RIGHT_ALT,K_BACKSPACE,K_DEL,K_INS,K_LAST_KEY=512 };
struct idKeyInput { static inline bool shift=false; static bool IsDown(int key) { return key==K_SHIFT && shift; } };
class idFile {
public:
    std::string bytes;
    int position=0,writeLimit=(std::numeric_limits<int>::max)();
    virtual ~idFile()=default;
    int Length() const { return static_cast<int>(bytes.size()); }
    int Tell() const { return position; }
    int Write(const void* source,int count) {
        count=(std::min)(count,(std::max)(0,writeLimit-position));
        if(position+count>Length())bytes.resize(position+count);
        if(count)std::memcpy(bytes.data()+position,source,count);
        position+=count; return count;
    }
    int Read(void* output,int count) {
        count=(std::min)(count,Length()-position);
        if(count)std::memcpy(output,bytes.data()+position,count);
        position+=count; return count;
    }
    int WriteUnsignedInt(unsigned value) {
        unsigned char data[4]; for(int i=0;i<4;++i)data[i]=static_cast<unsigned char>(value>>(i*8));
        return Write(data,4);
    }
    int ReadUnsignedInt(unsigned& value) {
        unsigned char data[4]{}; int count=Read(data,4); value=0;
        for(int i=0;i<count;++i)value|=static_cast<unsigned>(data[i])<<(i*8);
        return count;
    }
    int WriteInt(int value) { return WriteUnsignedInt(static_cast<unsigned>(value)); }
    int ReadInt(int& value) { unsigned word=0; int count=ReadUnsignedInt(word); value=static_cast<int>(word); return count; }
    int WriteFloat(float value) { unsigned word; std::memcpy(&word,&value,4); return WriteUnsignedInt(word); }
    int ReadFloat(float& value) { unsigned word; int count=ReadUnsignedInt(word); std::memcpy(&value,&word,4); return count; }
    const char* GetDataPtr() const { return bytes.data(); }
};
class idFile_Memory : public idFile {
public:
    idFile_Memory()=default;
    idFile_Memory(const char*,const char* data,int length) { bytes.assign(data,length); }
};
struct Files {
    std::map<std::string,std::string> sources;
    int ReadFile(const char* path,void** output,ID_TIME_T* stamp=nullptr) {
        auto found=sources.find(path); if(found==sources.end()) { if(output)*output=nullptr; return -1; }
        if(output) { *output=std::malloc(found->second.size()); std::memcpy(*output,found->second.data(),found->second.size()); }
        if(stamp)*stamp=91; return static_cast<int>(found->second.size());
    }
    void FreeFile(void* data) { std::free(data); }
} files,*fileSystem=&files;
struct Common {
    int warnings=0;
    std::vector<std::string> prints;
    void Warning(const char*,...) { ++warnings; }
    void Printf(const char* format,...) {
        char text[4096]; va_list args; va_start(args,format); std::vsnprintf(text,sizeof(text),format,args); va_end(args);
        prints.emplace_back(text);
    }
    int GetPresentationTime() const {return 0;}
} commonObject,*common=&commonObject;
struct CVars {
    bool aspect=true,shadows=true;
    float brightness=1;
    int writes=0;
    std::vector<std::pair<std::string,double>> history;
    bool GetCVarBool(const char* name) const {
        if(!std::strcmp(name,"ui_aspectCorrection"))return aspect;
        return !std::strcmp(name,"r_shadows") && shadows;
    }
    float GetCVarFloat(const char*) const { return brightness; }
    void SetCVarFloat(const char* name,float value) { assert(!std::strcmp(name,"r_brightness")); brightness=value; ++writes; history.emplace_back(name,value); }
    void SetCVarBool(const char* name,bool value) { assert(!std::strcmp(name,"r_shadows")); shadows=value; ++writes; history.emplace_back(name,value?1:0); }
} cvars,*cvarSystem=&cvars;
struct Console { bool open=false; bool Active() const { return open; } } consoleObject,*console=&consoleObject;
static bool windowFocused=true;
bool Sys_SDL_IsGameWindowFocused() { return windowFocused; }
struct Renderer {
    bool viewport=false;
    bool GetUseUIViewportFor2D() const { return viewport; }
    void SetUseUIViewportFor2D(bool value) { viewport=value; }
    void SetColor4(float,float,float,float) {}
    void DrawStretchTri(idVec2,idVec2,idVec2,idVec2,idVec2,idVec2,const void*) {}
    void FlushGui() {}
} renderer,*renderSystem=&renderer;
struct Decls { const void* FindMaterial(const char*) { return nullptr; } } decls,*declManager=&decls;
'''

RUNTIME = r'''
namespace openq4::ui {
// This ordinary-input fixture has no native owner/provider. Abort if a native
// endpoint is unexpectedly reached; ui_managed_native_owner compiles the real
// Runtime/Interaction settlement path including allocation-free publication.
struct Interaction::NativeSettlement::Impl {};
Interaction::NativeSettlement::~NativeSettlement() { std::abort(); }
const NativeTextEditorReceipt& Interaction::NativeSettlement::Receipt() const { std::abort(); }
struct Bounds { float x=0,y=0,width=0,height=0; };
struct Viewport {
    int width=1920,height=1080;
    float displayScale=1,userScale=1,pixelDensityX=2,pixelDensityY=2,originX=80,originY=40;
    float DpRatio() const { return displayScale*userScale; }
};
static DocumentModel modelTemplate;
struct Document::Impl { std::string source; DocumentModel model; };
Document::Document():impl(std::make_unique<Impl>()) {}
Document::~Document()=default;
Document::Document(Document&&) noexcept=default;
Document& Document::operator=(Document&&) noexcept=default;
bool Document::Load(const std::string& source,std::vector<Diagnostic>&) {
    if(source=="invalid")return false;
    impl->source=source; impl->model=modelTemplate; return true;
}
const std::string& Document::Source() const { return impl->source; }
const DocumentModel& Document::Model() const { return impl->model; }
const Node* DocumentModel::FindNode(const std::string& id) const {
    std::vector<const Node*> pending{&root};
    while(!pending.empty()) { const auto* node=pending.back(); pending.pop_back();
        if(node->id==id)return node;
        for(const auto& child:node->children)pending.push_back(&child);
    }
    return nullptr;
}
bool ValidStateValue(const StateValue& value) {
    return !std::holds_alternative<double>(value) || std::isfinite(std::get<double>(value));
}
std::string Value::Css() const { return text; }
std::string PresentationAliasKey(const std::string& name) {
    std::string folded=name;
    std::transform(folded.begin(),folded.end(),folded.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return folded;
}
std::string FormatPresentationValue(const PresentationValue& value) {
    if(value.type==PresentationType::String)return value.text;
    std::ostringstream text; text<<std::setprecision(17)<<value.data[0]; return text.str();
}
bool DocumentModel::ResolveAction(const std::string& name,const StateValues& variables,ActionInvocation& result,std::string& error,
                                 const PresentationLookup& lookup,const StateValue* input) const {
    auto found=actions.find(name); if(found==actions.end())return false;
    if(found->second.inputType && (!input || input->index()!=*found->second.inputType)) {error="stub input type rejected"; return false;}
    ActionInvocation candidate; candidate.action=name; candidate.operation=found->second.operation;
    for(auto& [key,expression]:found->second.arguments) {
        if(expression.inputValue) {
            if(!input) {error="stub missing input value"; return false;} candidate.arguments[key]=*input;
        } else if(!expression.presentation.empty()) {
            if(!lookup || !lookup(expression.presentation,expression.component,candidate.arguments[key],error))return false;
        } else candidate.arguments[key]=expression.state.empty()?expression.literal:variables.at(expression.state);
    }
    result=std::move(candidate);
    return true;
}
// Event effects are prescribed boundary data. Core evaluation/rollback/math is
// exercised by UiBehaviorTest; this harness checks adapter calls and publication.
struct StubEvent {
    StateValues changes;
    std::vector<ActionInvocation> invocations;
    bool pendingBrightness=false,fail=false;
    std::string disableControl;
    bool advanceModal=false;
};
static std::map<std::string,StubEvent> eventPlans;
static std::vector<std::string> eventHistory;
struct NumberEditorContext {std::string control;NumberEditView editor;std::uint64_t modalToken=0;};
class Runtime {
public:
    bool AttachNumberNative(const TextEditorIdentity&,NativeTextIdentity,NativeTextEditorBarrier&,std::string&,double) { std::abort(); }
    bool RefreshNumberNative(const NativeTextEditorBarrier&,NativeTextEditorView&,std::string&,double) { std::abort(); }
    bool IsNumberNativeCurrent(const NativeTextEditorBarrier&) const noexcept { std::abort(); }
    bool BeginNumberNativeCollection(const NativeTextEditorBarrier&,const NativeTextCollection&,NativeTextEditorBarrier&,std::string&) { std::abort(); }
    bool ApplyNumberNative(const NativeTextEditorBarrier&,const NativeTextOffer&,NativeTextEditorReceipt&,std::string&) { std::abort(); }
    bool CompleteNumberNativeCollection(const NativeTextEditorBarrier&,const NativeTextCollection&,NativeTextEditorBarrier&,std::string&) { std::abort(); }
    std::unique_ptr<Interaction::NativeSettlement> PrepareNumberNativeSettlement(const NativeTextEditorBarrier&,std::string&) { std::abort(); }
    bool PublishNumberNativeSettlement(Interaction::NativeSettlement&,NativeTextEditorReceipt&) noexcept { std::abort(); }
    bool RetireNumberNativeExact(NativeTextIdentity,const TextEditorIdentity&) noexcept { std::abort(); }
    NativeTextPresence QueryNumberNativePresence(NativeTextIdentity,const TextEditorIdentity&) const noexcept { return NativeTextPresence::BusyOrUnknown; }
    bool draftQueryAvailable=true,failDraftDiscard=false;
    std::vector<std::string> draftFocusCalls;
    unsigned draftDiscardCalls=0;
    bool QueryNumberDrafts(NumberDraftSummary& out,std::string& error,double) {
        error.clear();if(!loaded || !draftQueryAvailable) {error="stub draft query unavailable";return false;}
        NumberDraftSummary result;result.barrier.instance=numberToken;
        for(const auto& [id,widget]:widgets) if(widget.number) {
            const auto& editor=*widget.number;
            if(editor.dirty || editor.conflict || widget.pending || editor.composition) {
                NumberDraftStatus draft;draft.control=id;draft.status=editor.status;draft.dirty=editor.dirty;
                draft.conflict=editor.conflict;draft.pending=widget.pending.has_value();
                draft.composing=editor.composition.has_value();draft.active=editor.active;
                result.blocking.push_back(std::move(draft));
            }
        }
        out=std::move(result);return true;
    }
    bool DiscardNumberDrafts(const NumberDraftBarrier& expected,std::string& error,double seconds) {
        ++draftDiscardCalls;NumberDraftSummary current;
        if(!QueryNumberDrafts(current,error,seconds) || current.barrier!=expected || failDraftDiscard) {error="stub stale discard";return false;}
        for(auto& [id,widget]:widgets) {widget.number.reset();widget.pending.reset();}
        numberBuffers.clear();latestProposal.clear();++numberToken;return true;
    }
    bool FocusNumberDraft(const NumberDraftBarrier& expected,const std::string& id,std::string& error,double seconds) {
        draftFocusCalls.push_back(id);NumberDraftSummary current;
        if(!QueryNumberDrafts(current,error,seconds) || current.barrier!=expected ||
           std::none_of(current.blocking.begin(),current.blocking.end(),[&](const auto& draft){return draft.control==id;}))return false;
        if(!FocusControl(id,seconds))return false;
        return BeginNumberEdit(id,error,seconds);
    }
    std::optional<NumberEditorContext> QueryNumberEditor(std::string& error,double) {
        error.clear();const auto found=widgets.find(selected);
        if(!loaded || found==widgets.end() || disabledControls.contains(selected) || !found->second.number ||
           !found->second.number->active || found->second.number->conflict || found->second.pending)return {};
        return NumberEditorContext{selected,*found->second.number,modalIdentity};
    }

    using ActionValidator=std::function<bool(const ActionInvocation&,std::string&)>;
    struct EventEffects { StateValues stateChanges; std::vector<ActionInvocation> actions; };
    struct EventCall { std::string name; StateValues application; size_t maxActions; };
    std::vector<EventCall> eventCalls;
    mutable std::vector<std::string> eventQueries,resolvedActions;
    mutable std::vector<std::optional<StateValue>> resolvedInputs;
    struct Acknowledgement {std::string control; std::uint64_t token; bool accepted,matched; StateValues readback;};
    std::vector<Acknowledgement> acknowledgements;
    std::map<std::string,std::uint64_t> latestProposal;
    std::map<std::string,WidgetViewState> widgets;
    // These methods record the actual adapter boundary and use the production
    // UTF-8/edit buffer. They are not a replacement for Interaction/Runtime
    // ownership tests or a claim that a native text lease exists.
    struct NumberCall {
        std::string operation,id,text;
        NumberEditIdentity expected;
        size_t anchor=0,caret=0;
        bool option=false;
        double seconds=0;
        std::optional<TextInputEvent> input;
    };
    std::vector<NumberCall> numberCalls;
    mutable std::vector<std::string> numberGeometryReads;
    std::map<std::string,TextEditBuffer> numberBuffers;
    std::map<std::string,std::string> numberActions,numberBaselines;
    static inline std::uint64_t numberToken=10000;
    void InstallNumber(const std::string& id,const std::string& action) {
        WidgetViewState widget;widget.role=ControlRole::Number;widget.accepted=1.0;
        widgets[id]=widget;numberActions[id]=action;expectedControlDescriptors[id]={action,{}};
    }
    void UpdateNumber(const std::string& id) {
        auto& view=*widgets.at(id).number;const auto& buffer=numberBuffers.at(id);
        view.state=buffer.State();view.composition=buffer.Composition();
        double value=0;view.status=ParseTextNumber(view.state.text,{.5,2,false},value);
        view.dirty=view.state.text!=numberBaselines.at(id) || view.composition.has_value();
        view.canUndo=buffer.CanUndo();view.canRedo=buffer.CanRedo();
    }
    bool NumberOwner(const std::string& id,NumberEditIdentity expected,std::string& error) {
        const auto found=widgets.find(id);
        if(found==widgets.end() || !found->second.number || !found->second.number->active ||
           !expected.session || !expected.revision || expected!=found->second.number->identity ||
           selected!=id || disabledControls.contains(id)) {error="stub stale numeric owner";return false;}
        return true;
    }
    bool BeginNumberEdit(const std::string& id,std::string& error,double seconds) {
        numberCalls.push_back({"begin",id,"",{},0,0,false,seconds,{}});
        const auto found=widgets.find(id);
        if(found==widgets.end() || found->second.role!=ControlRole::Number || selected!=id || disabledControls.contains(id)) {error="stub number unavailable";return false;}
        auto& widget=found->second;
        if(!widget.number) {
            std::string text;if(!FormatTextNumber(std::get<double>(widget.accepted),{.5,2,false},text,error))return false;
            TextEditBuffer buffer;if(!buffer.Reset(text,{64,false,false},error) || !buffer.SetSelection(0,text.size(),error))return false;
            numberBuffers[id]=std::move(buffer);numberBaselines[id]=text;widget.number=NumberEditView{};
        }
        if(!widget.number->active) {widget.number->active=true;widget.number->identity={++numberToken,++numberToken};}
        UpdateNumber(id);return true;
    }
    bool ChangeNumber(const std::string& id,NumberEditIdentity expected,std::string& error,
                      const std::function<bool(TextEditBuffer&,std::string&)>& change) {
        if(!NumberOwner(id,expected,error) || widgets.at(id).number->conflict || latestProposal.contains(id))return false;
        auto candidate=numberBuffers.at(id);if(!change(candidate,error))return false;
        numberBuffers[id]=std::move(candidate);widgets.at(id).number->identity.revision=++numberToken;widgets.at(id).number->notice=NumberEditNotice::None;UpdateNumber(id);return true;
    }
    bool SetNumberNotice(const std::string& id,NumberEditIdentity expected,NumberEditNotice notice,std::string& error,double) {
        if(!NumberOwner(id,expected,error) || widgets.at(id).number->composition || widgets.at(id).number->conflict || widgets.at(id).pending)return false;
        widgets.at(id).number->notice=notice;return true;
    }
    bool ReplaceNumberSelection(const std::string& id,NumberEditIdentity expected,std::string_view text,std::string& error,double seconds) {
        numberCalls.push_back({"replace",id,std::string(text),expected,0,0,false,seconds,{}});
        return ChangeNumber(id,expected,error,[&](auto& buffer,auto& why){return buffer.ReplaceSelection(text,why);});
    }
    bool SetNumberSelection(const std::string& id,NumberEditIdentity expected,size_t anchor,size_t caret,std::string& error,double seconds) {
        numberCalls.push_back({"select",id,"",expected,anchor,caret,false,seconds,{}});
        return ChangeNumber(id,expected,error,[&](auto& buffer,auto& why){return buffer.SetSelection(anchor,caret,why);});
    }
    bool NumberCommand(const std::string& id,NumberEditIdentity expected,TextEditCommand command,bool extend,std::string& error,double seconds) {
        numberCalls.push_back({"command",id,"",expected,static_cast<size_t>(command),0,extend,seconds,{}});
        return ChangeNumber(id,expected,error,[&](auto& buffer,auto& why){
            if(buffer.Composition())return false;
            TextEditBoundaryMap map;map.text=buffer.State().text;
            for(size_t i=0;i<=map.text.size();++i)
                if(i==map.text.size() || (static_cast<unsigned char>(map.text[i])&0xc0)!=0x80)map.visualCarets.push_back(i);
            map.deletionStops=map.visualCarets;
            TextEditOperation operation;
            if(!EvaluateTextEditCommand(buffer.State(),map,command,extend,operation,why))return false;
            return operation.kind==TextEditOperation::Kind::Selection ? buffer.SetSelection(operation.anchor,operation.caret,why) :
                buffer.ReplaceRange(operation.anchor,operation.caret,operation.replacement,why);
        });
    }
    bool ApplyNumberInput(const std::string& id,NumberEditIdentity expected,const TextInputEvent& input,std::string& error,double seconds) {
        numberCalls.push_back({"input",id,input.text,expected,0,0,false,seconds,input});
        return ChangeNumber(id,expected,error,[&](auto& buffer,auto& why){return buffer.Apply(input,why);});
    }
    bool UndoNumberEdit(const std::string& id,NumberEditIdentity expected,bool redo,std::string& error,double seconds) {
        numberCalls.push_back({"history",id,"",expected,0,0,redo,seconds,{}});
        return ChangeNumber(id,expected,error,[&](auto& buffer,auto& why){return redo?buffer.Redo(why):buffer.Undo(why);});
    }
    bool CommitNumberEdit(const std::string& id,NumberEditIdentity expected,std::string& error,double seconds) {
        numberCalls.push_back({"commit",id,"",expected,0,0,false,seconds,{}});
        if(!NumberOwner(id,expected,error) || widgets.at(id).number->conflict || latestProposal.contains(id))return false;
        const auto& buffer=numberBuffers.at(id);double value=0;
        if(buffer.Composition() || ParseTextNumber(buffer.State().text,{.5,2,false},value)!=TextNumberStatus::Valid) {error="stub numeric text cannot commit";return false;}
        const auto token=++numberToken;latestProposal[id]=token;widgets.at(id).pending=value;widgets.at(id).proposalToken=token;
        actions.push_back({ControlAction::Kind::Activate,modelTemplate.id,id,numberActions.at(id),{},value,token,modalIdentity,expected.session,expected.revision});return true;
    }
    bool ResolveNumberConflict(const std::string& id,NumberEditIdentity expected,bool keepDraft,std::string& error,double seconds) {
        numberCalls.push_back({"resolve",id,"",expected,0,0,keepDraft,seconds,{}});
        if(!NumberOwner(id,expected,error) || !widgets.at(id).number->conflict)return false;
        std::string text;if(!FormatTextNumber(std::get<double>(widgets.at(id).accepted),{.5,2,false},text,error))return false;
        auto candidate=numberBuffers.at(id);
        if(keepDraft)candidate.CancelComposition();else if(!candidate.Reset(text,{64,false,false},error))return false;
        numberBuffers[id]=std::move(candidate);numberBaselines[id]=text;widgets.at(id).number->conflict=false;
        widgets.at(id).number->identity.revision=++numberToken;UpdateNumber(id);return true;
    }
    bool CancelNumberEdit(const std::string& id,NumberEditIdentity expected,double seconds) {
        numberCalls.push_back({"cancel",id,"",expected,0,0,false,seconds,{}});std::string error;
        if(!NumberOwner(id,expected,error))return false;
        widgets.at(id).number.reset();widgets.at(id).pending.reset();numberBuffers.erase(id);latestProposal.erase(id);return true;
    }
    std::optional<int> GetNumberGeometry(const std::string& id) const {
        numberGeometryReads.push_back(id);const auto found=widgets.find(id);
        return found!=widgets.end() && found->second.number && found->second.number->active ? std::optional<int>(1) : std::nullopt;
    }
    std::optional<WidgetViewState> GetWidgetState(const std::string& id) const {
        const auto found=widgets.find(id); return found==widgets.end()?std::nullopt:std::optional(found->second);
    }
    struct ChoiceCall {std::string operation,id;std::uint64_t token;ScrollStep step;double seconds;};
    std::vector<ChoiceCall> choiceCalls;
    bool choiceResult=true;
    bool OpenChoicePopup(const std::string& id,double seconds) {
        choiceCalls.push_back({"open",id,0,ScrollStep::Start,seconds});return choiceResult;
    }
    bool CloseChoicePopup(const std::string& id,std::uint64_t token,double seconds) {
        choiceCalls.push_back({"close",id,token,ScrollStep::Start,seconds});return choiceResult;
    }
    bool ScrollChoicePopup(const std::string& id,std::uint64_t token,ScrollStep step,double seconds) {
        choiceCalls.push_back({"scroll",id,token,step,seconds});return choiceResult;
    }
    std::vector<std::string> timelines;
    std::vector<std::pair<std::string,double>> eligibilityQueries;
    std::set<std::string> disabledControls;
    int validations=0;
    std::string FocusedControl() const {return selected;}
    unsigned long long StateRevision() const {return 0;}
    struct Stats {unsigned long long activeContexts=1,vectorElements=0,vectorPathsCompiled=0,vectorUploads=0,vectorCacheHits=0,submittedVertices=0,submittedIndices=0;};
    Stats stats;
    Stats Statistics() const {return stats;}
    Bounds bounds{1,2,3,4};
    bool boundsAvailable=true;
    mutable std::vector<std::string> boundReads;
    bool GetBounds(const std::string& name,Bounds& result) const {
        boundReads.push_back(name); if(!boundsAvailable)return false; result=bounds; return true;
    }
    bool FocusControl(const std::string& name,double) {selected=name; return true;}
    static constexpr size_t MaxSnapshotBytes=128u*1024u*1024u;
    static inline std::map<std::string,StateValues> snapshots;
    bool loaded=true,failSave=false,failRestore=false,accept=false,pointer=false;
    int cancels=0,releases=0,frames=0,modals=0;
    std::string modalRoot="dialog",modalBack="modalback";
    std::uint64_t modalIdentity=1;
    bool authoredModal=false;
    std::vector<ControlAction> modalBackQueries;
    std::vector<ControlAction> controlActionQueries;
    std::map<std::string,std::pair<std::string,std::string>> expectedControlDescriptors;
    float pointerX=0,pointerY=0;
    StateValues state;
    struct AliasWrite { std::string name,value; bool overrideExpression; };
    std::map<std::string,std::string> aliases;
    std::map<std::pair<std::string,std::string>,Value> properties;
    mutable std::vector<std::string> aliasReads;
    mutable std::vector<std::pair<std::string,std::string>> propertyReads;
    std::vector<AliasWrite> aliasWrites;
    bool failAliasWrite=false;
    std::string selected="brightness";
    std::vector<ControlAction> actions;
    std::vector<std::pair<MenuInput,bool>> menu;
    std::vector<int> wheels;
    std::vector<std::pair<float,float>> pointerMoves;
    std::vector<std::string> pointerTransport;
    std::vector<StateValues> stateCalls;
    bool failState=false;
    bool SetState(const StateValues& values,std::string& error,double) {
        stateCalls.push_back(values);
        if(failState) {error="stub state publication failed";return false;}
        for(const auto& [key,value]:values)state[key]=value;
        if(values.contains("settings.draft.r_brightness")) for(auto& [id,widget]:widgets)
            if(widget.role==ControlRole::Number)widget.accepted=values.at("settings.draft.r_brightness");
        return true;
    }
    StateValues GetState(bool=true) const { return state; }
    bool HasEvent(const std::string& name) const { eventQueries.push_back(name); return eventPlans.contains(PresentationAliasKey(name)); }
    bool RunEvent(const std::string& name,double,EventEffects& effects,std::string& error,
                  const StateValues& application={},const ActionValidator& validate={},size_t maxActions=256) {
        const auto key=PresentationAliasKey(name);
        eventCalls.push_back({name,application,maxActions}); eventHistory.push_back(key);
        auto found=eventPlans.find(key);
        if(found==eventPlans.end() || found->second.fail) {error="stub event rejected"; return false;}
        const auto& plan=found->second;
        if(maxActions>256 || plan.invocations.size()>maxActions) {error="stub action budget exceeded"; return false;}
        EventEffects candidate{plan.changes,plan.invocations};
        StateValues next=state;
        for(const auto& [id,value]:application)next[id]=value;
        for(const auto& [id,value]:candidate.stateChanges)next[id]=value;
        if(plan.pendingBrightness)candidate.actions.at(0).arguments["value"]=application.at("number");
        if(validate)for(const auto& action:candidate.actions) {++validations; if(!validate(action,error))return false;}
        state=std::move(next); effects=std::move(candidate);
        if(!plan.disableControl.empty())disabledControls.insert(plan.disableControl);
        if(plan.advanceModal)++modalIdentity;
        return true;
    }
    bool CanActivateControl(const std::string& node,double seconds) {
        eligibilityQueries.emplace_back(node,seconds); return !disabledControls.contains(node);
    }
    bool CanDispatchControlAction(const ControlAction& action,double seconds) {
        controlActionQueries.push_back(action);
        const auto descriptor=expectedControlDescriptors.find(action.node);
        if(descriptor!=expectedControlDescriptors.end() && descriptor->second!=std::make_pair(action.action,action.event))return false;
        if(action.editSession) {
            const auto found=widgets.find(action.node);
            if(found==widgets.end() || !found->second.number || !found->second.number->active || found->second.number->conflict ||
               found->second.number->identity!=NumberEditIdentity{action.editSession,action.editRevision} ||
               found->second.pending!=action.proposal || !latestProposal.contains(action.node) ||
               latestProposal.at(action.node)!=action.proposalToken || selected!=action.node)return false;
        }
        return action.modalToken && action.modalToken==modalIdentity && CanActivateControl(action.node,seconds);
    }
    bool ResolveAction(const std::string& id,ActionInvocation& result,std::string& error,const StateValue* input=nullptr) const {
        resolvedActions.push_back(id);
        resolvedInputs.push_back(input?std::optional<StateValue>(*input):std::nullopt);
        return modelTemplate.ResolveAction(id,state,result,error,[&](const std::string& alias,int component,StateValue& value,std::string& why) {
            auto found=aliases.find(alias);
            if(found==aliases.end() || component!=-1) {why="stub presentation lookup failed"; return false;}
            value=std::stod(found->second); return true;
        },input);
    }
    bool AcknowledgeControlProposal(const std::string& id,std::uint64_t token,bool accepted) {
        const auto found=latestProposal.find(id);
        const bool matched=token && found!=latestProposal.end() && found->second==token;
        acknowledgements.push_back({id,token,accepted,matched,state});
        if(matched) {
            latestProposal.erase(found);
            if(widgets.contains(id))widgets.at(id).pending.reset();
        }
        return matched;
    }
    bool SaveSnapshot(std::string& output,std::string&,double) const {
        if(failSave)return false;
        output="snapshot-"+std::to_string(snapshots.size()); snapshots.emplace(output,state); return true;
    }
    bool RestoreSnapshot(const std::string& input,std::string&,double) {
        if(failRestore || !snapshots.contains(input))return false;
        state=snapshots.at(input); return true;
    }
    void CancelInput(double) { ++cancels; accept=pointer=false; actions.clear(); }
    void ReleaseInputSources() { ++releases; accept=pointer=false; }
    std::vector<ControlAction> TakeActions() {
        std::vector<ControlAction> result; result.swap(actions);
        // Fixture-created activations stand in for current-scope Queue output.
        // Stamp the whole batch before lowering can replace scope ownership;
        // explicitly supplied identities exercise stale records independently.
        for(auto& action:result) if(action.kind==ControlAction::Kind::Activate && !action.modalToken)
            action.modalToken=modalIdentity;
        return result;
    }
    void PointerButton(bool down,double) {
        if(down)pointer=true;
        else if(pointer) { pointer=false; actions.push_back({ControlAction::Kind::Activate,modelTemplate.id,"button",selected}); }
    }
    void MenuAction(MenuInput action,bool down,double) {
        menu.emplace_back(action,down);
        if(action==MenuInput::Accept) {
            if(down)accept=true;
            else if(accept) { accept=false; actions.push_back({ControlAction::Kind::Activate,modelTemplate.id,"button",selected}); }
        }
        if(action==MenuInput::Back && down) {
            ControlAction back{ControlAction::Kind::Back,modelTemplate.id,"button",""};
            back.modalToken=modalIdentity; actions.push_back(back);
        }
    }
    void PointerMove(float x,float y,double) { pointerX=x; pointerY=y; pointerMoves.emplace_back(x,y); pointerTransport.push_back("move"); }
    void PointerWheel(int rows,double) { wheels.push_back(rows); pointerTransport.push_back(rows>0?"wheel-down":"wheel-up"); }
    bool PlayTimeline(const std::string& name,double) { timelines.push_back(name); return name=="slide"; }
    bool PopModal(double) { if(!modals)return false; --modals; return true; }
    bool CanDispatchModalBack(const ControlAction& action,double) {
        modalBackQueries.push_back(action);
        if(!action.modalToken || action.modalToken!=modalIdentity)return false;
        if(action.event.empty())return !authoredModal;
        return authoredModal && modals>0 && action.node==modalRoot && action.event==modalBack;
    }
    bool GetPresentationAlias(const std::string& name,std::string& output) const {
        aliasReads.push_back(name);
        auto found=aliases.find(name); if(found==aliases.end())return false;
        output=found->second; return true;
    }
    bool SetPresentationAlias(const std::string& name,const std::string& value,bool overrideExpression,std::string& error) {
        aliasWrites.push_back({name,value,overrideExpression});
        auto found=aliases.find(name);
        if(failAliasWrite || found==aliases.end()) { error="Presentation alias write rejected"; return false; }
        found->second=value; return true;
    }
    std::optional<Value> PresentedValue(const std::string& node,const std::string& property) const {
        propertyReads.emplace_back(node,property);
        auto found=properties.find({node,property});
        return found==properties.end()?std::nullopt:std::optional<Value>(found->second);
    }
};
}
struct retainedUIView_t {
    openq4::ui::Runtime runtime;
    retainedUIViewCallback_t callback;
    void* owner;
};
static std::vector<retainedUIView_t*> views;
static bool rejectLoad=false,rejectDraw=false;
static double presentationTime=1;
static openq4::ui::Viewport viewport;
retainedUIView_t* RetainedUI_CreateView(retainedUIViewCallback_t callback,void* owner) {
    auto* view=new retainedUIView_t; view->callback=callback; view->owner=owner; views.push_back(view); return view;
}
void RetainedUI_DestroyView(retainedUIView_t* view) {
    if(!view)return;
    views.erase(std::find(views.begin(),views.end(),view)); delete view;
}
openq4::ui::Runtime* RetainedUI_ViewRuntime(retainedUIView_t* view) { return view?&view->runtime:nullptr; }
bool RetainedUI_PrepareView(retainedUIView_t* view) { return view && view->runtime.loaded; }
bool RetainedUI_LoadView(retainedUIView_t*,const std::string&,const std::string&,std::vector<openq4::ui::Diagnostic>&) { return !rejectLoad; }
bool RetainedUI_DefaultViewport(openq4::ui::Viewport& result) { result=viewport; return viewport.width>0 && viewport.height>0; }
double RetainedUI_PresentationTime() { return presentationTime; }
bool RetainedUI_DrawViewRoot(retainedUIView_t* view,const openq4::ui::Viewport&) {
    ++view->runtime.frames; view->runtime.pointerTransport.push_back("frame"); return !rejectDraw;
}
'''

SETTINGS = r'''
// Deliberately small service boundary: real adapter code must validate reserved
// declarations, forward owner tokens, and publish only the fields it receives.
// Transaction validation, conflict/rollback and the actual catalog have their
// own production tests; prescribed service values here establish none of those.
namespace SettingsBoundary {
using namespace openq4::ui;
struct Dispatch { std::uint64_t owner; ActionInvocation invocation; bool accepted=false; };
struct Draw { std::uint64_t owner; std::string request; int frames; };
struct Service {
    std::uint64_t next=1,active=0;
    std::set<std::uint64_t> owners;
    std::set<std::uint64_t> exitReceipts;
    std::vector<std::uint64_t> exitConsumed;
    std::vector<std::uint64_t> created,released,closed,reads;
    std::vector<Dispatch> dispatches;
    std::vector<std::string> order;
    std::vector<Action> descriptors;
    std::vector<ActionInvocation> invocations;
    std::vector<std::pair<std::uint64_t,DocumentModel>> registrations;
    std::vector<Draw> draws;
    std::map<std::uint64_t,std::string> requests;
    StateValues live{{"r_brightness",1.0},{"r_shadows",true}},baseline,draft;
    bool readAvailable=true,rejectDispatch=false,cancelQueuesRestore=false;
    std::function<void()> afterDispatch;
    StateValues readOverrides;
} service;
const std::map<std::string,std::size_t> fields{{"r_brightness",0},{"r_shadows",1}};
}
void UI_SettingsConfirmationDocument(std::uint64_t owner,const openq4::ui::DocumentModel& document) {
    auto& service=SettingsBoundary::service;
    assert(service.owners.contains(owner));
    service.registrations.emplace_back(owner,document);
}
void UI_SettingsOwnerDrawn(std::uint64_t owner,const std::string& request) {
    auto& service=SettingsBoundary::service;
    assert(service.owners.contains(owner));
    // This records only the adapter's draw intent. SettingsService tests own
    // EndFrame receipt qualification and the controller's later presentation.
    assert(!views.empty());
    service.draws.push_back({owner,request,views.back()->runtime.frames});
}
std::uint64_t UI_SettingsCreateOwner() {
    auto& service=SettingsBoundary::service;
    const auto owner=service.next++; service.owners.insert(owner); service.created.push_back(owner); return owner;
}
void UI_SettingsCloseOwner(std::uint64_t owner) {
    auto& service=SettingsBoundary::service;
    service.exitReceipts.erase(owner);
    service.closed.push_back(owner); service.order.push_back("close:"+std::to_string(owner));
    if(service.active==owner) {service.active=0; service.baseline.clear(); service.draft.clear();}
}
void UI_SettingsReleaseOwner(std::uint64_t owner) {
    auto& service=SettingsBoundary::service;
    service.released.push_back(owner); UI_SettingsCloseOwner(owner); service.owners.erase(owner);
}
bool UI_SettingsExitReady(std::uint64_t owner) {
    const auto& service=SettingsBoundary::service;
    return !service.active && service.owners.contains(owner) && service.exitReceipts.contains(owner);
}
bool UI_SettingsConsumeExit(std::uint64_t owner) {
    if(!UI_SettingsExitReady(owner))return false;
    auto& service=SettingsBoundary::service;
    service.exitReceipts.erase(owner); service.exitConsumed.push_back(owner);
    service.order.push_back("exit-consumed:"+std::to_string(owner)); return true;
}
const std::map<std::string,std::size_t>& UI_SettingsStateSchema() {
    static const auto schema=[] {
        std::map<std::string,std::size_t> result{{"settings.open",1},{"settings.dirty",1},
            {"settings.busy",1},{"settings.canApply",1},{"settings.phase",0},{"settings.message",2},
            {"settings.request",2},{"settings.canConfirm",1},{"settings.canRevert",1},{"settings.canRetry",1},
            {"settings.remaining",0},{"settings.confirmationVisible",1}};
        for(const auto& [key,type]:SettingsBoundary::fields) {
            result.emplace("settings.draft."+key,type); result.emplace("settings.baseline."+key,type);
        }
        return result;
    }(); return schema;
}
bool UI_SettingsOperation(const openq4::ui::Action& action,std::string& error) {
    auto& service=SettingsBoundary::service; service.descriptors.push_back(action);
    const auto& op=action.operation;
    if((op=="settings.system.begin" || op=="settings.system.defaults" || op=="settings.system.cancel" ||
        op=="settings.system.apply" || op=="settings.system.applyExit" ||
        op=="settings.system.confirm" || op=="settings.system.revert") && action.arguments.empty())return true;
    if(op=="settings.system.edit" && !action.arguments.empty()) {
        for(const auto& [key,value]:action.arguments) {
            const auto field=SettingsBoundary::fields.find(key);
            if(field==SettingsBoundary::fields.end() || field->second!=value.type) {error="stub field/type rejected"; return false;}
        }
        return true;
    }
    error="stub operation shape rejected"; return false;
}
bool UI_SettingsInvocation(const openq4::ui::ActionInvocation& invocation,std::string& error) {
    using namespace openq4::ui;
    SettingsBoundary::service.invocations.push_back(invocation);
    Action action; action.operation=invocation.operation;
    for(const auto& [key,value]:invocation.arguments) {
        if(!ValidStateValue(value)) {error="stub value rejected"; return false;}
        Expression expression; expression.type=value.index(); action.arguments.emplace(key,expression);
    }
    return UI_SettingsOperation(action,error);
}
bool UI_SettingsDispatch(std::uint64_t owner,const openq4::ui::ActionInvocation& invocation,std::string& error) {
    auto& service=SettingsBoundary::service;
    service.order.push_back("dispatch:"+invocation.operation);
    service.dispatches.push_back({owner,invocation});
    // The actual service invalidates a prior receipt for every operation by
    // that owner, including rejected operations. Preserve that boundary rule
    // without duplicating transaction validation or the display coordinator.
    if(service.exitReceipts.erase(owner))service.order.push_back("exit-canceled:"+std::to_string(owner));
    if(!service.owners.contains(owner) || !UI_SettingsInvocation(invocation,error))return false;
    if(service.rejectDispatch) {error="stub dispatch rejected"; return false;}
    const auto& op=invocation.operation;
    if(op=="settings.system.begin") {
        if(service.active && service.active!=owner) {error="stub owner busy"; return false;}
        service.exitReceipts.clear();
        if(!service.active) {service.active=owner; service.baseline=service.live; service.draft=service.live;}
    } else if(op=="settings.system.cancel" && !service.active) {
        // Production cancel permits explicit completion of local-only discard
        // after an earlier service cancel already closed its transaction.
    } else {
        if(service.active!=owner) {error="stub owner mismatch"; return false;}
        if(op=="settings.system.edit")for(const auto& [key,value]:invocation.arguments)service.draft.at(key)=value;
        else if(op=="settings.system.apply" || op=="settings.system.applyExit") {
            service.live=service.draft; service.baseline=service.live;
            if(op=="settings.system.applyExit") {
                service.active=0; service.baseline.clear(); service.draft.clear();
                service.exitReceipts.insert(owner); service.order.push_back("exit-ready:"+std::to_string(owner));
            }
        }
        else if(op=="settings.system.defaults")service.draft={{"r_brightness",1.0},{"r_shadows",true}};
        else if(op=="settings.system.revert")service.draft=service.baseline;
        else if(op=="settings.system.cancel") {
            if(service.cancelQueuesRestore) {
                service.readOverrides["settings.busy"]=true;
                service.readOverrides["settings.phase"]=static_cast<double>(openq4::ui::SettingsPhase::Restoring);
            } else UI_SettingsCloseOwner(owner);
        }
    }
    service.dispatches.back().accepted=true; if(service.afterDispatch)service.afterDispatch(); return true;
}
bool UI_SettingsRead(std::uint64_t owner,openq4::ui::StateValues& values) {
    auto& service=SettingsBoundary::service; service.reads.push_back(owner);
    if(!service.readAvailable || !service.owners.contains(owner))return false;
    const bool own=service.active==owner,dirty=own && service.draft!=service.baseline;
    values={{"settings.open",own},{"settings.dirty",dirty},{"settings.busy",service.active!=0 && !own},
        {"settings.canApply",dirty},{"settings.phase",own?1.0:0.0},{"settings.message",std::string("#str_stub_settings")},
        {"settings.request",service.requests[owner]},{"settings.canConfirm",false},{"settings.canRevert",false},
        {"settings.canRetry",false},{"settings.remaining",0.0},{"settings.confirmationVisible",false}};
    if(own) {
        for(const auto& [key,value]:service.draft)values.emplace("settings.draft."+key,value);
        for(const auto& [key,value]:service.baseline)values.emplace("settings.baseline."+key,value);
    }
    for(const auto& [key,value]:service.readOverrides)values[key]=value;
    return true;
}
'''

BASE = r'''
idUserInterfaceManaged::idUserInterfaceManaged(bool enabled):refs(1),allocationId(0),managed(enabled) {}
idUserInterfaceManaged::~idUserInterfaceManaged()=default;
void idUserInterfaceManaged::RegisterLoaded() {}
void idUserInterfaceManaged::RegisterDemo() {}
void idUserInterfaceManaged::RefreshThinking() {}
'''
_native_lifetime_source = (ROOT / 'src/ui/UserInterface.cpp').read_text(encoding='utf-8')
BASE += function_body(_native_lifetime_source, 'void idUserInterfaceManaged::MarkNativeInputClosing(')
BASE += function_body(_native_lifetime_source, 'void idUserInterfaceManaged::SetNativeInputChanging(')

MAIN = r'''
using namespace openq4::ui;
static Runtime& Live() { assert(views.size()==1); return views.front()->runtime; }
static std::vector<std::pair<std::string,std::string>> Dictionary(const idUserInterfaceRetained& gui) {
    std::vector<std::pair<std::string,std::string>> result;
    for(int i=0;i<gui.State().GetNumKeyVals();++i) {
        const auto* entry=gui.State().GetKeyVal(i);
        result.emplace_back(entry->GetKey(),entry->GetValue());
    }
    return result;
}
static void CheckPresentationBridge() {
    assert(views.empty());
    idUserInterfaceRetained gui;
    idStr output="unchanged";
    assert(!gui.GetPresentationValue(nullptr,output) && output=="unchanged");
    assert(!gui.GetPresentationValue("curr",output) && output=="unchanged");
    assert(!gui.SetPresentationValue(nullptr,"1",true));
    assert(!gui.SetPresentationValue("curr",nullptr,false));
    assert(!gui.SetPresentationValue("curr","1",true));
    assert(views.empty());
    gui.SetStateFloat("number",1.25f);
    gui.SetStateString("curr","dictionary page");
    gui.SetStateString("desktop::curr","dictionary qualified page");
    gui.SetStateString("dictionaryOnly","not an exported presentation variable");
    assert(gui.InitFromFile("test.q4ui"));
    auto& runtime=Live();
    runtime.aliases={{"CuRR","22"},{"desktop::curr","23"},{"number","24"},{"metadata",""}};
    Value numeric; numeric.type=ValueType::Number; numeric.text="0.35";
    Value text; text.type=ValueType::Text; text.text="literal <span> ; quit";
    runtime.properties={{{"desktop","curr"},numeric},{{"root","opacity"},numeric},{{"label","text"},text}};
    const auto dictionary=Dictionary(gui);
    const auto state=runtime.state;
    const int cvarWrites=cvars.writes;

    // The adapter forwards authored names unchanged. Runtime owns case folding,
    // typing and expressions; the caller dictionary is a separate namespace.
    assert(gui.GetPresentationValue("CuRR",output) && output=="22");
    assert(runtime.aliasReads.back()=="CuRR" && runtime.propertyReads.empty());
    assert(gui.GetPresentationValue("number",output) && output=="24");
    assert(gui.GetPresentationValue("desktop::curr",output) && output=="23");
    assert(runtime.aliasReads.back()=="desktop::curr" && runtime.propertyReads.empty());
    assert(gui.GetPresentationValue("metadata",output) && output.empty());

    // Qualified property diagnostics remain readable only after alias lookup
    // fails. A qualified export wins even when a property has that same name.
    assert(gui.GetPresentationValue("root::opacity",output) && output=="0.35");
    assert(runtime.aliasReads.back()=="root::opacity");
    assert(runtime.propertyReads.back()==std::make_pair(std::string("root"),std::string("opacity")));
    assert(gui.GetPresentationValue("label::text",output) && output==text.text);
    const auto readCount=runtime.aliasReads.size(),propertyCount=runtime.propertyReads.size();
    output="unchanged";
    assert(!gui.GetPresentationValue(nullptr,output) && output=="unchanged");
    assert(runtime.aliasReads.size()==readCount && runtime.propertyReads.size()==propertyCount);
    for(const char* missing:{"","missing","dictionaryOnly","missing::text"}) {
        assert(!gui.GetPresentationValue(missing,output) && output=="unchanged");
        assert(runtime.aliasReads.back()==missing);
    }
    assert(Dictionary(gui)==dictionary && runtime.state==state);

    const auto writeCount=runtime.aliasWrites.size();
    assert(!gui.SetPresentationValue(nullptr,"1",true));
    assert(!gui.SetPresentationValue("CuRR",nullptr,false));
    assert(runtime.aliasWrites.size()==writeCount);
    assert(gui.SetPresentationValue("CuRR"," 27 ",true));
    assert(runtime.aliasWrites.back().name=="CuRR" && runtime.aliasWrites.back().value==" 27 ");
    assert(runtime.aliasWrites.back().overrideExpression && runtime.aliases.at("CuRR")==" 27 ");
    assert(gui.SetPresentationValue("desktop::curr","28",false));
    assert(!runtime.aliasWrites.back().overrideExpression && runtime.aliasWrites.back().name=="desktop::curr");
    const char* literal="literal ; quit\n<span>data</span>";
    assert(gui.SetPresentationValue("metadata",literal,false));
    assert(runtime.aliasWrites.back().value==literal && !runtime.aliasWrites.back().overrideExpression);
    assert(gui.GetPresentationValue("metadata",output) && output==literal);
    assert(gui.SetPresentationValue("metadata","",true));
    assert(gui.GetPresentationValue("metadata",output) && output.empty());

    const auto aliases=runtime.aliases;
    const auto fallbackCount=runtime.propertyReads.size();
    for(const char* missing:{"","missing","dictionaryOnly","root::opacity"}) {
        assert(!gui.SetPresentationValue(missing,"99",true));
        assert(runtime.aliasWrites.back().name==missing && runtime.aliasWrites.back().value=="99");
        assert(runtime.aliases==aliases && runtime.propertyReads.size()==fallbackCount);
    }
    // A successful delegation clears the adapter error, so a subsequent
    // runtime validation failure is reported once and preserves the value.
    assert(gui.SetPresentationValue("CuRR"," 27 ",false));
    const int warnings=common->warnings;
    runtime.failAliasWrite=true;
    assert(!gui.SetPresentationValue("CuRR","invalid",true) && common->warnings==warnings+1);
    assert(!gui.SetPresentationValue("CuRR","invalid",false) && common->warnings==warnings+1);
    assert(runtime.aliases==aliases);
    runtime.failAliasWrite=false;
    assert(gui.SetPresentationValue("CuRR"," 27 ",false));
    runtime.failAliasWrite=true;
    assert(!gui.SetPresentationValue("CuRR","invalid",false) && common->warnings==warnings+2);
    runtime.failAliasWrite=false;

    const auto finalReads=runtime.aliasReads.size(),finalWrites=runtime.aliasWrites.size();
    runtime.loaded=false;
    output="unavailable";
    assert(!gui.GetPresentationValue("CuRR",output) && output=="unavailable");
    assert(!gui.SetPresentationValue("CuRR","99",true));
    assert(runtime.aliasReads.size()==finalReads && runtime.aliasWrites.size()==finalWrites);
    runtime.loaded=true;
    assert(gui.GetPresentationValue("CuRR",output) && output==" 27 ");
    assert(Dictionary(gui)==dictionary && runtime.state==state && cvars.writes==cvarWrites);
}
static const char* Key(idUserInterfaceRetained& gui,int key,bool down) {
    sysEvent_t event{SE_KEY,key,down?1:0}; return gui.HandleEvent(&event,0,nullptr);
}
static void PatchWord(std::string& bytes,size_t offset,unsigned value) {
    assert(offset+4<=bytes.size()); for(int i=0;i<4;++i)bytes[offset+i]=static_cast<char>(value>>(i*8));
}
static void RejectFrame(idUserInterfaceRetained& gui,const std::string& bytes) {
    idFile_Memory file("mutated",bytes.data(),static_cast<int>(bytes.size()));
    const auto before=Live().state; const std::string dictionary=gui.GetStateString("number");
    const float x=gui.CursorX(),y=gui.CursorY(); const bool interactive=gui.IsInteractive(),unique=gui.IsUniqued();
    assert(!gui.ReadFromSaveGame(&file));
    assert(Live().state==before && dictionary==gui.GetStateString("number"));
    assert(x==gui.CursorX() && y==gui.CursorY() && interactive==gui.IsInteractive() && unique==gui.IsUniqued());
}
static std::string Frame(const std::vector<std::pair<std::string,std::string>>& dictionary,
                         int flags=7,float x=31,float y=47,bool trailing=false) {
    std::string snapshot,error; assert(Live().SaveSnapshot(snapshot,error,presentationTime));
    idFile_Memory payload,frame;
    payload.WriteInt(static_cast<int>(dictionary.size()));
    for(const auto& [key,value]:dictionary) { assert(WriteString(payload,key)); assert(WriteString(payload,value)); }
    payload.WriteInt(flags); payload.WriteFloat(x); payload.WriteFloat(y); assert(WriteString(payload,snapshot));
    if(trailing)payload.WriteInt(0);
    frame.WriteUnsignedInt(SaveTag); frame.WriteInt(1); frame.WriteInt(payload.Length()); frame.Write(payload.GetDataPtr(),payload.Length());
    return frame.bytes;
}
static ActionInvocation Brightness(double value) {return {"program.brightness","settings.brightness.set",{{"value",value}}};}
static ActionInvocation Shadows(bool value) {return {"program.shadows","settings.shadows.set",{{"value",value}}};}
static void Drain(idUserInterfaceRetained& gui,const std::vector<std::pair<std::string,double>>& expected,bool expectedClose=false) {
    const auto before=cvars.history.size(); bool close=false;
    assert(gui.DispatchApplicationActions(gui.PendingApplicationCommand(),close) && close==expectedClose);
    assert(cvars.history.size()==before+expected.size());
    for(size_t i=0;i<expected.size();++i) {
        const auto& actual=cvars.history[before+i];
        assert(actual.first==expected[i].first && std::abs(actual.second-expected[i].second)<.00001);
    }
    assert(!*gui.PendingApplicationCommand());
    gui.DispatchApplicationActions(ActionMarker,close);
    assert(!close && cvars.history.size()==before+expected.size());
}
static bool Semantic(idUserInterfaceRetained& gui,const char* input,bool down) {
    return UI_RetainedDiagnostic(&gui,idCmdArgs{{"openq4_retainedGui","menu",input,down?"1":"0"}});
}
static void CheckEventBridge() {
    assert(views.empty()); cvars=CVars{}; consoleObject.open=false; windowFocused=true;
    eventPlans={{"batch",{{{"flag",false}},{Brightness(0),Shadows(false),Brightness(1.4)},true}},
                {"tail",{{},{Brightness(.9)}}}};
    eventHistory.clear();
    {
        idUserInterfaceRetained gui;
        assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0);
        gui.SetStateString("number","1.7500"); gui.SetStateString("text","pending ; data");
        gui.SetStateString("unrelated","unmodified"); gui.SetStateString("host","caller cannot own this");
        assert(std::get<double>(Live().state.at("number"))==1);
        gui.HandleNamedEvent("BaTcH");
        assert(Live().eventCalls.size()==1 && Live().eventCalls.back().name=="BaTcH");
        assert(Live().eventCalls.back().maxActions==256 && Live().validations==3);
        const auto& application=Live().eventCalls.back().application;
        assert(std::get<double>(application.at("number"))==1.75 && !application.contains("host"));
        assert(std::get<std::string>(application.at("text"))=="pending ; data");
        assert(std::get<double>(Live().state.at("number"))==1.75 && !gui.GetStateBool("flag"));
        assert(std::string(gui.GetStateString("number"))=="1.7500" &&
               std::string(gui.GetStateString("unrelated"))=="unmodified" &&
               std::string(gui.GetStateString("host"))=="caller cannot own this");
        assert(cvars.writes==0 && !std::strcmp(gui.PendingApplicationCommand(),ActionMarker));
        // Captured invocations remain immutable if the caller changes state before draining.
        gui.SetStateFloat("number",1.6f); gui.StateChanged(0);
        bool close=false; gui.DispatchApplicationActions("quit; set r_gamma 99",close);
        assert(cvars.writes==0 && *gui.PendingApplicationCommand());
        Drain(gui,{{"r_brightness",1.75},{"r_shadows",0},{"r_brightness",1.4}});

        // One collected sequence can mix physical actions and a completed program.
        Live().actions={{ControlAction::Kind::Activate,modelTemplate.id,"button","brightness",""},
                        {ControlAction::Kind::Activate,modelTemplate.id,"button","","batch"},
                        {ControlAction::Kind::Activate,modelTemplate.id,"button","shadows",""}};
        sysEvent_t tick{}; gui.HandleEvent(&tick,0,nullptr); gui.HandleNamedEvent("tail");
        assert(Live().resolvedActions.size()==2 && Live().resolvedActions[0]=="brightness" && Live().resolvedActions[1]=="shadows");
        assert(Live().eventCalls[1].maxActions==255 && Live().eventCalls[2].maxActions==251);
        Drain(gui,{{"r_brightness",1.6},{"r_brightness",1.6},{"r_shadows",0},
                   {"r_brightness",1.4},{"r_shadows",0},{"r_brightness",.9}});

        // The global pump may run before another input event or redraw. It must
        // discard a stale physical click while retaining program and diagnostic effects.
        for(bool focusLoss:{false,true}) {
            gui.HandleNamedEvent("tail"); Key(gui,K_ENTER,true); Key(gui,K_ENTER,false);
            assert(Semantic(gui,"accept",true) && Semantic(gui,"accept",false));
            if(focusLoss)windowFocused=false; else consoleObject.open=true;
            assert(*gui.PendingApplicationCommand());
            Drain(gui,{{"r_brightness",.9},{"r_brightness",1.6}});
            assert(Semantic(gui,"back",true) && Semantic(gui,"back",false));
            Drain(gui,{},true);
            windowFocused=true; consoleObject.open=false; gui.HandleEvent(&tick,0,nullptr);
        }

        // Complete programs are discarded at resource replacement and never replayed.
        for(auto event:{retainedUIViewEvent_t::BeforeResourceReset,retainedUIViewEvent_t::Failed}) {
            gui.HandleNamedEvent("tail"); const auto before=eventHistory.size();
            views.front()->callback(views.front()->owner,event);
            views.front()->callback(views.front()->owner,retainedUIViewEvent_t::Restored);
            assert(!*gui.PendingApplicationCommand() && eventHistory.size()==before); Drain(gui,{});
        }
        gui.HandleNamedEvent("tail"); idFile_Memory saved; assert(gui.WriteToSaveGame(&saved));
        idFile_Memory restore("programs",saved.GetDataPtr(),saved.Length());
        assert(gui.ReadFromSaveGame(&restore)); Drain(gui,{});
        gui.HandleNamedEvent("tail"); assert(gui.InitFromFile("test.q4ui")); Drain(gui,{});
        gui.HandleNamedEvent("tail"); assert(gui.InitFromFile("next.q4ui")); Drain(gui,{});

        // Validation is real adapter code; late rejection leaves runtime, public
        // dictionary and FIFO unchanged. No partial CVar write can escape.
        eventPlans["invalid"]={{{"number",.8}},{Brightness(1.1),Brightness(9)}};
        gui.HandleNamedEvent("tail");
        const auto oldState=Live().state; const auto oldDictionary=Dictionary(gui);
        const int oldWrites=cvars.writes,oldValidations=Live().validations;
        gui.HandleNamedEvent("invalid");
        assert(Live().validations==oldValidations+2 && Live().state==oldState && Dictionary(gui)==oldDictionary);
        assert(*gui.PendingApplicationCommand() && cvars.writes==oldWrites);
        Drain(gui,{{"r_brightness",.9}});
        gui.SetStateString("number","invalid pending number");
        const auto calls=Live().eventCalls.size(); gui.HandleNamedEvent("tail");
        assert(Live().eventCalls.size()==calls && !*gui.PendingApplicationCommand());
        gui.SetStateString("number","1.6000");
        const auto beforeUnknown=Dictionary(gui); const auto beforeQueries=Live().eventQueries.size();
        gui.HandleNamedEvent(nullptr); assert(Live().eventQueries.size()==beforeQueries);
        gui.HandleNamedEvent("missing");
        assert(Live().timelines.back()=="missing" && Live().eventCalls.size()==calls && Dictionary(gui)==beforeUnknown);
        Live().loaded=false; gui.HandleNamedEvent("tail");
        assert(Live().eventCalls.size()==calls); Live().loaded=true;

        // Capacity is passed to Runtime before publication; committed programs
        // remain intact when a later whole program cannot fit.
        eventPlans["full"]={{},std::vector<ActionInvocation>(256,{"dismiss","ui.dismiss",{}})};
        gui.HandleNamedEvent("full"); gui.HandleNamedEvent("tail");
        assert(Live().eventCalls.back().maxActions==0 && *gui.PendingApplicationCommand()); Drain(gui,{},true);
    }
    eventPlans.clear(); eventHistory.clear();
    {
        // ResolveAction delegates through Runtime's presentation-aware lookup.
        auto saved=modelTemplate.actions.at("brightness");
        auto& expression=modelTemplate.actions["brightness"].arguments["value"];
        expression.state.clear(); expression.presentation="setting";
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0);
        Live().aliases["setting"]="1.3"; Key(gui,K_ENTER,true); Key(gui,K_ENTER,false);
        assert(Live().resolvedActions==std::vector<std::string>{"brightness"}); Drain(gui,{{"r_brightness",1.3}});
        modelTemplate.actions["brightness"]=std::move(saved);
    }
    {
        eventPlans={{"oninit",{{{"number",1.125}},{Brightness(1.125)}}},
                    {"onactivate",{{},{Shadows(false)}}}, {"ondeactivate",{{},{Brightness(.9)}}},
                    {"ontrigger",{{},{Brightness(1.7)}}}};
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui") && eventHistory.empty());
        assert(*gui.Activate(true,0)); gui.Redraw(0); gui.Redraw(0);
        assert(eventHistory==std::vector<std::string>({"onactivate","oninit"}));
        assert(gui.GetStateFloat("number")==1.125f); Drain(gui,{{"r_shadows",0},{"r_brightness",1.125}});
        gui.Trigger(0); assert(*gui.Activate(false,0)); Drain(gui,{{"r_brightness",1.7},{"r_brightness",.9}});
        const auto beforeReload=eventHistory.size();
        assert(gui.InitFromFile("test.q4ui")); gui.Redraw(0); assert(eventHistory.size()==beforeReload);
        idFile_Memory save; assert(gui.WriteToSaveGame(&save));
        idFile_Memory restored("lifecycle",save.GetDataPtr(),save.Length());
        assert(gui.ReadFromSaveGame(&restored)); gui.Redraw(0); assert(eventHistory.size()==beforeReload);
        views.front()->callback(views.front()->owner,retainedUIViewEvent_t::BeforeResourceReset);
        views.front()->callback(views.front()->owner,retainedUIViewEvent_t::Restored);
        gui.Redraw(0); assert(eventHistory.size()==beforeReload);
        assert(gui.InitFromFile("next.q4ui")); eventPlans["oninit"].fail=true;
        gui.Redraw(0); assert(eventHistory.size()==beforeReload+1 && !*gui.PendingApplicationCommand());
        eventPlans["oninit"].fail=false; gui.Redraw(0); gui.Redraw(0);
        assert(eventHistory.size()==beforeReload+2); Drain(gui,{{"r_brightness",1.125}});
    }
    eventHistory.clear();
    {
        // Restoring a GUI that has never drawn must still suppress automatic init.
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui"));
        idFile_Memory save; assert(gui.WriteToSaveGame(&save));
        idFile_Memory restored("before-init",save.GetDataPtr(),save.Length());
        assert(gui.ReadFromSaveGame(&restored)); gui.Redraw(0);
        assert(eventHistory.empty() && !*gui.PendingApplicationCommand());
    }
    eventPlans.clear(); eventHistory.clear();
    std::string error;
    assert(ValidInvocation(Brightness(.5),error) && ValidInvocation(Brightness(2),error));
    assert(!ValidInvocation(Brightness(std::numeric_limits<double>::infinity()),error));
    assert(!ValidInvocation({"x","settings.brightness.set",{{"value",true}}},error));
    assert(!ValidInvocation({"x","ui.dismiss",{{"value",1.0}}},error));
    assert(!ValidInvocation({"x","exec",{}},error));
    auto eventControl=modelTemplate; eventControl.events["selected"]={"selected",{}};
    eventControl.root.control->action.clear(); eventControl.root.control->event="SELECTED";
    assert(ValidateApplication(eventControl,error)); eventControl.events.clear(); assert(!ValidateApplication(eventControl,error));
    // Exercise the engine adapter's real load boundary for local scroll controls.
    auto scrollModel=modelTemplate;
    scrollModel.root.control=Control{};
    scrollModel.root.control->role=ControlRole::Scrollbar;
    scrollModel.root.control->widget=ScrollSpec{"viewport","track","thumb"};
    assert(ValidateApplication(scrollModel,error));
    auto invalidScroll=scrollModel; invalidScroll.root.control->action="brightness";
    assert(!ValidateApplication(invalidScroll,error));
    invalidScroll=scrollModel; invalidScroll.root.control->event="selected";
    invalidScroll.events["selected"]={"selected",{}};
    assert(!ValidateApplication(invalidScroll,error));
    invalidScroll=scrollModel; invalidScroll.root.control->value=Expression{};
    assert(!ValidateApplication(invalidScroll,error));
    invalidScroll=scrollModel; invalidScroll.root.control->widget=std::monostate{};
    assert(!ValidateApplication(invalidScroll,error));
    auto nestedScroll=modelTemplate; nestedScroll.root.children.push_back(scrollModel.root);
    assert(ValidateApplication(nestedScroll,error));
    auto missingAction=modelTemplate; missingAction.root.control->action.clear();
    assert(!ValidateApplication(missingAction,error));
    const auto applicationModel=modelTemplate;
    modelTemplate=scrollModel;
    {
        idUserInterfaceRetained gui;
        assert(gui.InitFromFile("test.q4ui") && gui.IsInteractive());
        assert(!*gui.PendingApplicationCommand());
    }
    modelTemplate=applicationModel;
    assert(views.empty());
}
static void CheckEventEligibility() {
    assert(views.empty());
    const auto originalModel=modelTemplate;
    modelTemplate.state["noninteractive"]={false,""};
    eventPlans={{"tail",{{},{Brightness(.9)}}},
                {"disable",{{{"noninteractive",true}},{Brightness(1.25)}}},
                {"invalidate",{{},{Shadows(false)}}}};
    eventPlans["invalidate"].disableControl="later";
    sysEvent_t tick{};
    {
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0);
        gui.HandleNamedEvent("tail"); Live().modals=1;
        Live().actions={{ControlAction::Kind::Activate,"stale-document","button","brightness",""},
                        {ControlAction::Kind::Back,"stale-document","button","",""},
                        {ControlAction::Kind::Activate,modelTemplate.id,"button","","invalidate"},
                        {ControlAction::Kind::Activate,modelTemplate.id,"later","brightness",""},
                        {ControlAction::Kind::Activate,modelTemplate.id,"later","","tail"},
                        {ControlAction::Kind::Back,modelTemplate.id,"later","","",{},0,1}};
        gui.HandleEvent(&tick,0,nullptr);
        assert((Live().eligibilityQueries==std::vector<std::pair<std::string,double>>({
            {"button",presentationTime},{"later",presentationTime},{"later",presentationTime}})));
        assert(Live().eventCalls.size()==2 && Live().resolvedActions.empty() && Live().modals==0);
        // A committed program survives; stale document records never reach
        // eligibility/modal checks, and current Back does not require a control.
        Drain(gui,{{"r_brightness",.9},{"r_shadows",0}});
    }
    {
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0);
        Key(gui,K_ENTER,true); Key(gui,K_ENTER,false); // Pending physical invocation.
        gui.SetStateString("noninteractive","true");
        assert(gui.IsInteractive() && !std::get<bool>(Live().state.at("noninteractive")));
        gui.HandleNamedEvent("tail");
        assert(!gui.IsInteractive() && !gui.HasInteractiveOverride());
        assert(std::get<bool>(Live().eventCalls.back().application.at("noninteractive")) &&
               std::get<bool>(Live().state.at("noninteractive")));
        assert(std::string(gui.GetStateString("noninteractive"))=="true");
        Drain(gui,{{"r_brightness",.9}}); // Quarantine removed the physical action only.
        gui.SetStateString("noninteractive","false"); gui.HandleNamedEvent("tail");
        assert(gui.IsInteractive() && !gui.HasInteractiveOverride()); Drain(gui,{{"r_brightness",.9}});
    }
    {
        idUserInterfaceRetained gui; gui.SetStateString("noninteractive","true");
        assert(gui.InitFromFile("test.q4ui") && !gui.IsInteractive());
        gui.SetStateString("noninteractive","false"); gui.StateChanged(0); assert(gui.IsInteractive());
        gui.SetStateString("noninteractive","1"); gui.StateChanged(0); assert(!gui.IsInteractive());
        gui.SetStateString("noninteractive","0"); gui.StateChanged(0); assert(gui.IsInteractive());
    }
    {
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0);
        Key(gui,K_ENTER,true); Key(gui,K_ENTER,false);
        gui.HandleNamedEvent("tail"); Live().modals=1;
        const auto checked=Live().eligibilityQueries.size();
        Live().actions={{ControlAction::Kind::Activate,modelTemplate.id,"button","","disable"},
                        {ControlAction::Kind::Activate,modelTemplate.id,"later","brightness",""},
                        {ControlAction::Kind::Activate,modelTemplate.id,"later","","tail"},
                        {ControlAction::Kind::Back,modelTemplate.id,"later","","",{},0,1}};
        gui.HandleEvent(&tick,0,nullptr);
        assert(!gui.IsInteractive() && gui.GetStateBool("noninteractive") && !gui.HasInteractiveOverride());
        assert(Live().eligibilityQueries.size()==checked+1 && Live().eventCalls.size()==2 && Live().modals==1);
        // The first program disables interactivity. Later activation, program
        // and Back records from the already-collected batch are all skipped.
        Drain(gui,{{"r_brightness",.9},{"r_brightness",1.25}});
    }
    for(bool interactive:{false,true}) {
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0);
        gui.SetInteractive(interactive); gui.SetStateBool("noninteractive",true);
        gui.HandleNamedEvent("disable");
        assert(gui.HasInteractiveOverride() && gui.IsInteractive()==interactive && gui.GetStateBool("noninteractive"));
        const auto queries=Live().eligibilityQueries.size();
        assert(Semantic(gui,"accept",true) && Semantic(gui,"accept",false));
        assert(Live().eligibilityQueries.size()==queries+(interactive?1:0));
        if(interactive)Drain(gui,{{"r_brightness",1.25},{"r_brightness",1}});
        else Drain(gui,{{"r_brightness",1.25}});
    }
    eventPlans.clear(); eventHistory.clear(); modelTemplate=originalModel;
    // Undeclared legacy numeric flags retain the dictionary's nonzero behavior.
    for(const char* value:{"2","-1"}) {
        idUserInterfaceRetained gui; gui.SetStateString("noninteractive",value);
        assert(gui.InitFromFile("test.q4ui") && !gui.IsInteractive());
        gui.SetStateString("noninteractive","0"); gui.StateChanged(0); assert(gui.IsInteractive());
    }
    assert(views.empty());
}
static ActionInvocation SettingsAction(const std::string& operation,StateValues values={}) {
    return {"stub.settings", "settings.system."+operation, std::move(values)};
}
static void SettingsEvent(idUserInterfaceRetained& gui,const std::vector<ActionInvocation>& actions) {
    eventPlans["settingsboundary"]={{},actions};
    gui.HandleNamedEvent("settingsboundary");
}
static void CheckSettingsBoundary() {
    assert(views.empty() && SettingsBoundary::service.owners.empty());
    const auto originalModel=modelTemplate;
    auto& service=SettingsBoundary::service;
    service=SettingsBoundary::Service{};
    eventPlans.clear(); eventHistory.clear();
    for(const auto& [key,type]:UI_SettingsStateSchema()) {
        StateValue value=type==0?StateValue(0.0):type==1?StateValue(false):StateValue(std::string("#str_authored_fallback"));
        modelTemplate.state.emplace(key,StateDeclaration{value,""});
    }
    modelTemplate.actions.emplace("settings.begin",Action{"settings.system.begin",{}});
    Expression setting; setting.type=0; setting.state="settings.draft.r_brightness";
    modelTemplate.actions.emplace("settings.edit",Action{"settings.system.edit",{{"r_brightness",setting}}});
    std::string error;
    assert(ValidateApplication(modelTemplate,error));
    assert(!service.descriptors.empty());
    // These are actual production ValidateApplication/ValidInvocation methods.
    // JSON parsing and the complete catalog remain outside this harness.
    for(const auto& key:{"settings.unknown","Settings.open","settings.OPEN"}) {
        auto bad=modelTemplate; bad.state[key]={false,""}; assert(!ValidateApplication(bad,error));
    }
    for(const auto& [key,type]:UI_SettingsStateSchema()) {
        auto bad=modelTemplate; bad.state[key].cvar="r_brightness"; assert(!ValidateApplication(bad,error));
        bad=modelTemplate; bad.state[key].initial=type==0?StateValue(false):StateValue(0.0);
        assert(!ValidateApplication(bad,error));
    }
    for(int branch=0;branch<3;++branch) {
        auto bad=modelTemplate; EventStep write; write.op=EventOp::SetState; write.values["settings.open"]=Expression{};
        EventStep nested; nested.op=EventOp::If;
        if(branch==0)bad.events["reserved"].steps.push_back(write);
        else {
            (branch==1?nested.thenSteps:nested.elseSteps).push_back(write);
            bad.events["reserved"].steps.push_back(nested);
        }
        assert(!ValidateApplication(bad,error));
    }
    auto bad=modelTemplate; bad.events["reserved"].steps.push_back(EventStep{});
    bad.events["reserved"].steps[0].values["SETTINGS.open"]=Expression{};
    assert(!ValidateApplication(bad,error));
    bad=modelTemplate; bad.actions["settings.edit"].arguments["r_brightness"].type=1;
    assert(!ValidateApplication(bad,error));
    bad=modelTemplate; bad.actions["settings.begin"].arguments["extra"]=Expression{};
    assert(!ValidateApplication(bad,error));
    bad=modelTemplate; bad.actions["settings.begin"].operation="settings.system.exec";
    assert(!ValidateApplication(bad,error));
    assert(!ValidInvocation(SettingsAction("edit",{{"r_brightness",true}}),error));
    assert(!ValidInvocation(SettingsAction("edit",{{"r_brightness",std::numeric_limits<double>::infinity()}}),error));
    assert(ValidInvocation(SettingsAction("edit",{{"r_brightness",1.6}}),error));
    std::uint64_t oldOwner=0;
    {
        idUserInterfaceRetained gui;
        gui.SetStateFloat("number",1.25f);
        gui.SetStateString("settings.draft.r_brightness","caller cannot initialize service values");
        assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0); gui.Redraw(0);
        assert(gui.GetStateBool("settings.open")==false);
        SettingsEvent(gui,{SettingsAction("begin")}); Drain(gui,{});
        oldOwner=service.active;
        assert(oldOwner && service.owners.contains(oldOwner) && service.dispatches.back().owner==oldOwner);
        assert(gui.GetStateBool("settings.open") && gui.GetStateFloat("settings.draft.r_brightness")==1);
        gui.SetStateString("number","1.7500"); gui.SetStateString("text","unrelated pending text");
        gui.SetStateString("unrelated","preserve this key");
        gui.SetStateString("settings.draft.r_brightness","not a number"); gui.SetStateBool("SETTINGS.OPEN",false);
        service.draft["r_brightness"]=1.6;
        const auto calls=Live().stateCalls.size();
        gui.Redraw(0);
        assert(Live().stateCalls.size()==calls+1);
        for(const auto& [key,value]:Live().stateCalls.back())assert(key.starts_with("settings."));
        assert(Live().state.at("number")==StateValue(1.25));
        assert(std::string(gui.GetStateString("number"))=="1.7500" && std::string(gui.GetStateString("text"))=="unrelated pending text");
        assert(std::string(gui.GetStateString("unrelated"))=="preserve this key");
        assert(gui.GetStateBool("settings.open") && std::abs(gui.GetStateFloat("settings.draft.r_brightness")-1.6f)<.0001f);
        assert(Live().state.at("settings.draft.r_brightness")==StateValue(1.6));
        assert(std::string(gui.GetStateString("settings.message"))=="#str_stub_settings");
        const auto dispatches=service.dispatches.size();
        gui.SetStateString("settings.phase","nan"); gui.SetStateFloat("settings.draft.r_brightness",99);
        SettingsEvent(gui,{});
        assert(service.dispatches.size()==dispatches && Live().eventCalls.back().application.at("number")==StateValue(1.75));
        for(const auto& [key,value]:Live().eventCalls.back().application)assert(!key.starts_with("settings."));
        assert(Live().state.at("settings.draft.r_brightness")==StateValue(1.6));
        gui.SetStateFloat("settings.draft.r_brightness",99); gui.StateChanged(0);
        assert(Live().state.at("settings.draft.r_brightness")==StateValue(1.6));
        for(const auto& [key,value]:Live().stateCalls.back())assert(!key.starts_with("settings."));
        // Same source replacement and renderer callbacks retain the service
        // owner even though the adapter's Runtime resource may be recreated.
        const auto creates=service.created.size(),releases=service.released.size();
        assert(gui.InitFromFile("test.q4ui")); gui.Redraw(0);
        assert(service.created.size()==creates && service.released.size()==releases && service.reads.back()==oldOwner && service.active==oldOwner);
        views.front()->callback(views.front()->owner,retainedUIViewEvent_t::BeforeResourceReset);
        views.front()->callback(views.front()->owner,retainedUIViewEvent_t::Restored); gui.Redraw(0);
        assert(service.reads.back()==oldOwner && service.released.size()==releases);
        rejectLoad=true; assert(!gui.InitFromFile("next.q4ui")); rejectLoad=false;
        assert(service.active==oldOwner && service.released.size()==releases);
        SettingsEvent(gui,{SettingsAction("edit",{{"r_brightness",1.9}})});
        const auto delivered=service.dispatches.size();
        assert(gui.InitFromFile("next.q4ui")); gui.Redraw(0); Drain(gui,{});
        assert(service.dispatches.size()==delivered && service.released.back()==oldOwner && !service.owners.contains(oldOwner));
        assert(service.reads.back()!=oldOwner && service.active==0 && !gui.GetStateBool("settings.open"));
        SettingsEvent(gui,{SettingsAction("begin")}); Drain(gui,{});
        oldOwner=service.active;
        const auto previous=oldOwner;
        const auto oldSource=files.sources.at("next.q4ui"); files.sources["next.q4ui"]="edited body at the same path";
        assert(gui.InitFromFile("next.q4ui")); gui.Redraw(0);
        assert(service.released.back()==previous && !service.owners.contains(previous) && service.active==0);
        SettingsEvent(gui,{SettingsAction("begin")}); Drain(gui,{});
        oldOwner=service.active; assert(oldOwner!=previous);
        files.sources["next.q4ui"]=oldSource;
    }
    assert(views.empty() && service.owners.empty() && service.active==0 && service.released.back()==oldOwner);
    // Two adapters forward distinct stable owners; a busy response is scoped
    // to the requester and cannot change the first adapter's draft.
    {
        idUserInterfaceRetained first,second;
        assert(first.InitFromFile("test.q4ui") && second.InitFromFile("test.q4ui"));
        first.Activate(true,0); second.Activate(true,0);
        SettingsEvent(first,{SettingsAction("begin"),SettingsAction("edit",{{"r_brightness",1.4}})}); Drain(first,{});
        const auto firstOwner=service.active;
        SettingsEvent(second,{SettingsAction("begin"),SettingsAction("edit",{{"r_brightness",1.9}})}); Drain(second,{});
        const auto secondOwner=service.dispatches.back().owner;
        assert(secondOwner!=firstOwner && !service.dispatches.back().accepted);
        assert(service.active==firstOwner && service.draft.at("r_brightness")==StateValue(1.4));
        assert(second.GetStateBool("settings.busy") && !second.GetStateBool("settings.open"));
        first.Redraw(0); assert(first.GetStateBool("settings.open") && !first.GetStateBool("settings.busy"));
        assert(!*first.Activate(false,0));
        assert(service.active==0 && service.owners.contains(firstOwner));
        SettingsEvent(second,{SettingsAction("begin")}); Drain(second,{});
        assert(service.active==secondOwner);
    }
    assert(views.empty() && service.owners.empty());
    // A cached outgoing GUI must finish committed lifecycle actions before it
    // closes settings. The token remains available for a later activation.
    {
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0);
        SettingsEvent(gui,{SettingsAction("begin")}); Drain(gui,{});
        const auto owner=service.active;
        SettingsEvent(gui,{SettingsAction("edit",{{"r_brightness",1.6}})});
        eventPlans["ondeactivate"]={{},{SettingsAction("edit",{{"r_brightness",1.7}}),SettingsAction("apply")}};
        service.order.clear(); const auto closed=service.closed.size();
        assert(*gui.Activate(false,0) && service.active==owner && service.closed.size()==closed);
        Drain(gui,{});
        assert(service.order==std::vector<std::string>({"dispatch:settings.system.edit","dispatch:settings.system.edit",
            "dispatch:settings.system.apply","close:"+std::to_string(owner)}));
        assert(service.live.at("r_brightness")==StateValue(1.7) && !gui.GetStateBool("settings.open") && service.owners.contains(owner));
        gui.Activate(true,0); SettingsEvent(gui,{SettingsAction("begin")}); Drain(gui,{});
        assert(service.active==owner);
        assert(*gui.Activate(false,0));
        gui.Activate(true,0); service.order.clear(); Drain(gui,{});
        assert(service.active==owner && std::none_of(service.order.begin(),service.order.end(),[](const auto& item){return item.starts_with("close:");}));
        // If resources disappear before the outgoing queue is dispatched, its
        // actions are discarded but the pending settings close still happens.
        assert(*gui.Activate(false,0)); const auto dispatched=service.dispatches.size();
        Live().loaded=false; Drain(gui,{});
        assert(service.dispatches.size()==dispatched && service.active==0 && service.owners.contains(owner));
        Live().loaded=true; gui.Redraw(0); assert(!gui.GetStateBool("settings.open"));
        for(const auto event:{retainedUIViewEvent_t::Restored,retainedUIViewEvent_t::Failed}) {
            gui.Activate(true,0); SettingsEvent(gui,{SettingsAction("begin")}); Drain(gui,{});
            assert(service.active==owner && *gui.Activate(false,0));
            const auto before=service.dispatches.size();
            views.front()->callback(views.front()->owner,retainedUIViewEvent_t::BeforeResourceReset);
            if(event==retainedUIViewEvent_t::Failed)Live().loaded=false;
            views.front()->callback(views.front()->owner,event);
            // The resource callback discards every program, but pending owner
            // closure must itself keep the manager's dispatch marker alive.
            assert(*gui.PendingApplicationCommand()); Drain(gui,{});
            assert(service.dispatches.size()==before && service.active==0 && service.owners.contains(owner));
            Live().loaded=true;
        }
        eventPlans.erase("ondeactivate");
        gui.Activate(true,0); SettingsEvent(gui,{SettingsAction("begin")}); Drain(gui,{});
        assert(!*gui.Activate(false,0) && service.active==0 && !gui.GetStateBool("settings.open"));
    }
    assert(views.empty() && service.owners.empty());
    {
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui"));
        idFile_Memory inactive; assert(gui.WriteToSaveGame(&inactive));
        gui.Activate(true,0);
        SettingsEvent(gui,{SettingsAction("begin"),SettingsAction("edit",{{"r_brightness",1.4}})}); Drain(gui,{});
        const auto owner=service.active;
        gui.SetStateString("number","1.2500");
        idFile_Memory active; assert(gui.WriteToSaveGame(&active));
        SettingsEvent(gui,{SettingsAction("edit",{{"r_brightness",1.8}})}); Drain(gui,{});
        gui.SetStateString("number","1.9000");
        eventPlans["ondeactivate"]={{},{SettingsAction("edit",{{"r_brightness",1.9}})}};
        assert(*gui.Activate(false,0));
        const auto dispatched=service.dispatches.size(),closed=service.closed.size();
        Live().failRestore=true; active.position=0;
        assert(!gui.ReadFromSaveGame(&active)); Live().failRestore=false;
        assert(service.active==owner && service.draft.at("r_brightness")==StateValue(1.8) &&
            service.closed.size()==closed && *gui.PendingApplicationCommand());
        active.position=0; assert(gui.ReadFromSaveGame(&active));
        assert(gui.Active() && !*gui.PendingApplicationCommand() && service.dispatches.size()==dispatched);
        assert(service.active==owner && service.draft.at("r_brightness")==StateValue(1.8) && service.closed.size()==closed);
        // The engine draft is not serialized in the GUI frame. The next
        // preparation republishes it over saved service presentation fields,
        // while retaining the saved ordinary pending/committed distinction.
        gui.Redraw(0);
        assert(std::abs(gui.GetStateFloat("settings.draft.r_brightness")-1.8f)<.0001f &&
            Live().state.at("settings.draft.r_brightness")==StateValue(1.8));
        assert(std::string(gui.GetStateString("number"))=="1.2500" && Live().state.at("number")==StateValue(1.0));
        assert(*gui.Activate(false,0));
        inactive.position=0; assert(gui.ReadFromSaveGame(&inactive));
        assert(!gui.Active() && !*gui.PendingApplicationCommand() && service.active==0 && service.owners.contains(owner));
        assert(service.dispatches.size()==dispatched && service.closed.size()==closed+1);
        gui.Redraw(0); assert(!gui.GetStateBool("settings.open"));
        // Same-source recreation keeps the token, but cannot lose an already
        // requested inactive close when it discards the outgoing action queue.
        gui.Activate(true,0); SettingsEvent(gui,{SettingsAction("begin")}); Drain(gui,{});
        assert(service.active==owner && *gui.Activate(false,0));
        const auto released=service.released.size();
        rejectLoad=true; assert(!gui.InitFromFile("test.q4ui")); rejectLoad=false;
        assert(service.active==owner && *gui.PendingApplicationCommand() && service.released.size()==released);
        assert(gui.InitFromFile("test.q4ui"));
        assert(!*gui.PendingApplicationCommand() && service.active==0 && service.owners.contains(owner) && service.released.size()==released);
        gui.Activate(true,0); SettingsEvent(gui,{SettingsAction("begin")}); Drain(gui,{});
        assert(*gui.Activate(false,0));
        assert(gui.InitFromFile("next.q4ui") && !*gui.PendingApplicationCommand());
        assert(!service.owners.contains(owner) && service.active==0);
        SettingsEvent(gui,{SettingsAction("begin")}); Drain(gui,{});
        assert(service.active!=0 && service.active!=owner);
        eventPlans.erase("ondeactivate");
    }
    assert(views.empty() && service.owners.empty());
    modelTemplate=originalModel; eventPlans.clear(); eventHistory.clear();
}
static void CheckNodeInspection() {
    assert(views.empty());
    idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui"));
    auto inspect=[&](const std::string& id) {return UI_RetainedDiagnostic(&gui,idCmdArgs{{"openq4_retainedGui","inspect",id}});};
    auto& runtime=Live();
    runtime.bounds={32,32,864,462}; runtime.stats={1,13,4,4,9,300,450};
    Value opacity; opacity.type=ValueType::Number; opacity.data[0]=.2;
    Value display; display.type=ValueType::Keyword; display.text="block";
    runtime.properties[{"root","opacity"}]=opacity; runtime.properties[{"root","display"}]=display;
    const auto state=Dictionary(gui); const auto calls=runtime.stateCalls;
    const auto events=eventHistory; const auto frames=runtime.frames;
    assert(inspect("root"));
    assert(commonObject.prints.back()=="RETAINED_GUI_NODE id=root bounds=32,32,864,462 opacity=0.2 display=block authoredPaths=0 statistics=view vectorElements=13 pathsCompiled=4 uploads=4 cacheHits=9 vertices=300 triangles=150\n");
    assert(Dictionary(gui)==state && runtime.stateCalls==calls && runtime.frames==frames && eventHistory==events);
    assert(runtime.boundReads.back()=="root");
    for(const auto& id:std::vector<std::string>{"missing","ROOT","","root;quit","root\nquit","root::other",std::string(129,'x')}) {
        const auto prints=commonObject.prints.size(); assert(!inspect(id)); assert(commonObject.prints.size()==prints);
    }
    runtime.boundsAvailable=false; assert(!inspect("root")); runtime.boundsAvailable=true;
    runtime.bounds.width=std::numeric_limits<float>::infinity(); assert(!inspect("root")); runtime.bounds.width=864;
    runtime.properties[{"root","display"}].text="block\nFAKE_RECORD";
    assert(inspect("root") && commonObject.prints.back().find("display=invalid ")!=std::string::npos);
    assert(commonObject.prints.back().find("FAKE_RECORD")==std::string::npos);
    runtime.properties.clear(); assert(inspect("root"));
    assert(commonObject.prints.back().find("opacity=1 display=default ")!=std::string::npos);
}
static void CheckSettingsDrawBoundary() {
    assert(views.empty() && SettingsBoundary::service.owners.empty());
    const auto originalModel=modelTemplate;
    auto& service=SettingsBoundary::service;
    service=SettingsBoundary::Service{}; eventPlans.clear();
    consoleObject.open=false; windowFocused=true;
    for(const auto& [key,type]:UI_SettingsStateSchema())
        modelTemplate.state[key]={type==0?StateValue(0.0):type==1?StateValue(false):StateValue(std::string()),""};
    modelTemplate.root.children.push_back(Node{});
    modelTemplate.root.children.back().id="settings_revert";
    modelTemplate.root.children.back().control=Control{};
    modelTemplate.root.children.back().control->label="#str_229989";
    modelTemplate.root.children.back().control->action="dismiss";
    {
        idUserInterfaceRetained gui;
        const auto registrations=service.registrations.size();
        assert(!gui.InitFromFile("bad.q4ui") && service.registrations.size()==registrations);
        rejectLoad=true; assert(!gui.InitFromFile("test.q4ui")); rejectLoad=false;
        assert(service.registrations.size()==registrations);
        assert(gui.InitFromFile("test.q4ui"));
        const auto owner=service.created.back();
        assert(service.registrations.size()==registrations+1 && service.registrations.back().first==owner);
        const auto& registered=service.registrations.back().second;
        assert(registered.id==modelTemplate.id && registered.state.size()==modelTemplate.state.size());
        assert(registered.state.at("settings.request").initial==StateValue(std::string()));
        assert(registered.root.children.back().id=="settings_revert" &&
               registered.root.children.back().control->label=="#str_229989");
        service.requests[owner]="18446744073709551614";
        gui.Redraw(0); assert(service.draws.empty()); // Inactive owners cannot acknowledge a frame.
        gui.Activate(true,0);
        gui.SetStateString("settings.request","forged pending caller value");
        const int frames=Live().frames;
        gui.Redraw(0);
        assert(service.draws.size()==1 && service.draws.back().frames==frames+1);
        assert(service.draws.back().owner==owner && service.draws.back().request==service.requests.at(owner));
        assert(Live().eligibilityQueries.back().first=="settings_revert");
        const auto draws=service.draws.size();
        rejectDraw=true; gui.Redraw(0); rejectDraw=false;
        Live().disabledControls.insert("settings_revert"); gui.Redraw(0); Live().disabledControls.clear();
        gui.SetInteractive(false); gui.Redraw(0); gui.SetInteractive(true);
        Live().loaded=false; gui.Redraw(0); Live().loaded=true;
        service.readAvailable=false; gui.Redraw(0); service.readAvailable=true;
        const auto savedViewport=viewport; viewport.width=0; gui.Redraw(0); viewport=savedViewport;
        gui.Redraw(0,false);
        assert(service.draws.size()==draws);
        // A draw intent does not dispatch programs. Repeated redraws preserve
        // committed work, and the normal pending pump delivers it once.
        eventPlans["ondrawtest"]={{},{SettingsAction("begin")}};
        gui.HandleNamedEvent("ondrawtest");
        const auto dispatched=service.dispatches.size();
        gui.Redraw(0); gui.Redraw(0);
        assert(service.dispatches.size()==dispatched && *gui.PendingApplicationCommand());
        Drain(gui,{}); Drain(gui,{});
        assert(service.dispatches.size()==dispatched+1);
        const auto count=service.registrations.size();
        idFile_Memory saved; assert(gui.WriteToSaveGame(&saved));
        saved.position=0; assert(gui.ReadFromSaveGame(&saved));
        views.front()->callback(views.front()->owner,retainedUIViewEvent_t::BeforeResourceReset);
        views.front()->callback(views.front()->owner,retainedUIViewEvent_t::Restored);
        gui.Redraw(0);
        assert(service.registrations.size()==count && service.draws.back().owner==owner);
        assert(service.dispatches.size()==dispatched+1);
        assert(gui.InitFromFile("test.q4ui"));
        assert(service.registrations.size()==count+1 && service.registrations.back().first==owner);
        rejectLoad=true; assert(!gui.InitFromFile("next.q4ui")); rejectLoad=false;
        assert(service.registrations.size()==count+1 && service.owners.contains(owner));
        modelTemplate.id="replacement-document";
        assert(gui.InitFromFile("next.q4ui"));
        const auto replacement=service.created.back();
        assert(replacement!=owner && !service.owners.contains(owner));
        assert(service.registrations.size()==count+2 && service.registrations.back().first==replacement &&
               service.registrations.back().second.id=="replacement-document");
        service.requests[replacement]="27";
        gui.Redraw(0);
        assert(service.draws.back().owner==replacement && service.draws.back().request=="27");
        assert(service.dispatches.size()==dispatched+1);
    }
    assert(views.empty() && service.owners.empty());
    modelTemplate=originalModel; eventPlans.clear();
    const auto draws=service.draws.size();
    {
        idUserInterfaceRetained plain; assert(plain.InitFromFile("test.q4ui")); plain.Activate(true,0); plain.Redraw(0);
        assert(service.draws.size()==draws); // Unrelated retained pages make no settings draw claim.
    }
    assert(views.empty() && service.owners.empty());
}
static void CheckValueProposalBoundary() {
    assert(views.empty() && SettingsBoundary::service.owners.empty());
    const auto original=modelTemplate; auto& service=SettingsBoundary::service;
    service=SettingsBoundary::Service{}; eventPlans.clear(); eventHistory.clear();
    consoleObject.open=false; windowFocused=true;
    Expression input; input.type=0; input.inputValue=true;
    modelTemplate.actions["value.brightness"]={"settings.brightness.set",{{"value",input}},std::size_t(0)};
    modelTemplate.actions["value.settings"]={"settings.system.edit",{{"r_brightness",input}},std::size_t(0)};
    modelTemplate.actions["value.dismiss"]={"ui.dismiss",{},std::size_t(0)};
    for(const auto& [key,type]:UI_SettingsStateSchema())
        modelTemplate.state[key]={type==0?StateValue(0.0):type==1?StateValue(false):StateValue(std::string()),""};
    {
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0); gui.Redraw(0);
        auto& runtime=Live();
        const auto proposal=[&](std::uint64_t token,double value,const char* action="value.brightness",const char* document=nullptr) {
            runtime.latestProposal["root"]=token;
            runtime.actions.push_back({ControlAction::Kind::Activate,document?document:modelTemplate.id,"root",action,"",value,token});
        };
        const auto collect=[&] {sysEvent_t tick{}; gui.HandleEvent(&tick,0,nullptr);};
        proposal(101,1.25); proposal(102,1.5); collect();
        assert(runtime.resolvedInputs.size()==2 && runtime.resolvedInputs[0]==StateValue(1.25) && runtime.resolvedInputs[1]==StateValue(1.5));
        // Mutating both source readbacks and the resolver's model after queue
        // collection cannot rewrite the already-resolved application requests.
        gui.SetStateFloat("number",.6f); gui.StateChanged(0);
        modelTemplate.actions["value.brightness"].arguments["value"].inputValue=false;
        modelTemplate.actions["value.brightness"].arguments["value"].literal=1.9;
        Drain(gui,{{"r_brightness",1.25},{"r_brightness",1.5}});
        assert(runtime.acknowledgements.size()==2);
        assert(runtime.acknowledgements[0].token==101 && runtime.acknowledgements[0].accepted && !runtime.acknowledgements[0].matched);
        assert(runtime.acknowledgements[1].token==102 && runtime.acknowledgements[1].accepted && runtime.acknowledgements[1].matched);
        assert(runtime.latestProposal.empty());
        modelTemplate.actions["value.brightness"].arguments["value"]=input;
        proposal(103,99); collect(); Drain(gui,{});
        assert(runtime.acknowledgements.back().token==103 && !runtime.acknowledgements.back().accepted && runtime.acknowledgements.back().matched);
        proposal(104,1.3,"value.brightness","old-document"); collect(); Drain(gui,{});
        assert(runtime.acknowledgements.back().token==104 && !runtime.acknowledgements.back().accepted);
        runtime.disabledControls.insert("root"); proposal(105,1.3); collect(); Drain(gui,{}); runtime.disabledControls.clear();
        assert(runtime.acknowledgements.back().token==105 && !runtime.acknowledgements.back().accepted);
        // A physical proposal quarantined before dispatch is rejected. A newer
        // token remains pending even when the older rejection is delivered.
        proposal(106,1.4); collect(); runtime.latestProposal["root"]=107;
        consoleObject.open=true; Drain(gui,{}); consoleObject.open=false; gui.Redraw(0);
        assert(runtime.acknowledgements.back().token==106 && !runtime.acknowledgements.back().accepted && !runtime.acknowledgements.back().matched);
        assert(runtime.latestProposal.at("root")==107);
        runtime.latestProposal.clear();
        SettingsEvent(gui,{SettingsAction("begin")}); Drain(gui,{});
        proposal(108,1.6,"value.settings"); collect(); service.rejectDispatch=true; Drain(gui,{}); service.rejectDispatch=false;
        assert(service.draft.at("r_brightness")==StateValue(1.0));
        assert(runtime.acknowledgements.back().token==108 && !runtime.acknowledgements.back().accepted && runtime.acknowledgements.back().matched);
        proposal(109,1.7,"value.settings"); collect(); Drain(gui,{});
        assert(service.draft.at("r_brightness")==StateValue(1.7));
        assert(runtime.state.at("settings.draft.r_brightness")==StateValue(1.7));
        assert(runtime.acknowledgements.back().token==109 && runtime.acknowledgements.back().accepted && runtime.acknowledgements.back().matched);
        // Legacy buttons pass a null input payload and require no acknowledgement.
        const auto count=runtime.acknowledgements.size(); gui.SetStateFloat("number",1.1f); gui.StateChanged(0);
        runtime.selected="brightness"; Key(gui,K_ENTER,true); Key(gui,K_ENTER,false); Drain(gui,{{"r_brightness",static_cast<double>(1.1f)}});
        assert(!runtime.resolvedInputs.back() && runtime.acknowledgements.size()==count);
        for(const auto& [key,action]:std::vector<std::pair<int,MenuInput>>{{K_HOME,MenuInput::Home},{K_END,MenuInput::End},{K_PGUP,MenuInput::PageUp},{K_PGDN,MenuInput::PageDown}}) {
            const auto before=runtime.menu.size(); Key(gui,key,true); Key(gui,key,false);
            assert(runtime.menu.size()==before+2 && runtime.menu[before]==std::make_pair(action,true) && runtime.menu.back()==std::make_pair(action,false));
        }
        Key(gui,K_MWHEELUP,true); Key(gui,K_MWHEELUP,true); Key(gui,K_MWHEELUP,false); Key(gui,K_MWHEELDOWN,true); Key(gui,K_MWHEELDOWN,false);
        assert((runtime.wheels==std::vector<int>{-1,1}));
        proposal(110,1.2,"value.dismiss"); collect(); Drain(gui,{},true);
        assert(runtime.acknowledgements.back().token==110 && runtime.acknowledgements.back().accepted && runtime.acknowledgements.back().matched);
        assert(runtime.latestProposal.empty());
        WidgetViewState widget; widget.role=ControlRole::Slider; widget.accepted=1.05; widget.pending=1.2;
        widget.rejected=1.1; widget.proposalToken=999; widget.firstVisible=0; runtime.widgets["root"]=widget;
        idCmdArgs inspect; inspect.values={"retained","widget","root"};
        assert(UI_RetainedDiagnostic(&gui,inspect));
        const auto record=commonObject.prints.back();
        assert(record.find("RETAINED_GUI_WIDGET id=root role=2 type=0 accepted=1.05 ")!=std::string::npos &&
               record.find("pending=1 proposed=1.2 rejected=1 token=999 popup=0 firstVisible=0")!=std::string::npos);
        inspect.values.back()="missing"; assert(!UI_RetainedDiagnostic(&gui,inspect));
    }
    assert(views.empty() && service.owners.empty()); modelTemplate=original; eventPlans.clear();
}
static void CheckStationaryWheelHandoff() {
    assert(views.empty() && SettingsBoundary::service.owners.empty());
    consoleObject.open=false; windowFocused=true;
    const auto savedViewport=viewport;
    {
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0); gui.Redraw(0);
        auto& runtime=Live(); runtime.pointerTransport.clear();
        const auto wheel=[&] {Key(gui,K_MWHEELDOWN,true); Key(gui,K_MWHEELDOWN,false);};
        gui.SetCursor(100,100);
        wheel(); gui.Redraw(0); wheel(); gui.Redraw(0);
        // A popup's wheel selection survives only if the adapter avoids
        // synthesizing another PointerMove at the stationary row. This tests
        // actual production transport; Runtime/RmlUi selection is tested by
        // UiValueRuntimeTest, not reimplemented in this recording double.
        assert((runtime.pointerTransport==std::vector<std::string>{"move","wheel-down","frame","wheel-down","frame"}));
        assert(runtime.pointerMoves.size()==1 && (runtime.wheels==std::vector<int>{1,1}));
        const auto initial=runtime.pointerMoves.back();
        gui.SetCursor(120,100); wheel();
        assert(runtime.pointerMoves.size()==2 && runtime.pointerMoves.back()!=initial);
        wheel(); assert(runtime.pointerMoves.size()==2);
        const auto current=runtime.pointerMoves.back();
        viewport.originX+=32; wheel();
        assert(runtime.pointerMoves.size()==3 && runtime.pointerMoves.back().first==current.first+32/viewport.pixelDensityX);
        viewport=savedViewport; wheel();
        assert(runtime.pointerMoves.size()==4 && runtime.pointerMoves.back()==current);
        // Actual motion/button events reclaim pointer navigation even when
        // their coordinates equal the last routed location.
        sysEvent_t motion{SE_MOUSE,0,0}; gui.HandleEvent(&motion,0,nullptr);
        assert(runtime.pointerMoves.size()==5 && runtime.pointerMoves.back()==current);
        Key(gui,K_MOUSE1,true); Key(gui,K_MOUSE1,false);
        assert(runtime.pointerMoves.size()==7 && runtime.pointerMoves.back()==current);
        Drain(gui,{{"r_brightness",std::get<double>(runtime.state.at("number"))}});
        gui.Activate(false,0); gui.Activate(true,0); gui.Redraw(0); wheel();
        assert(runtime.pointerMoves.size()==8 && runtime.pointerMoves.back()==current);
        views.front()->callback(views.front()->owner,retainedUIViewEvent_t::BeforeResourceReset);
        views.front()->callback(views.front()->owner,retainedUIViewEvent_t::Restored);
        gui.Redraw(0); wheel();
        assert(runtime.pointerMoves.size()==9 && runtime.pointerMoves.back()==current);
    }
    viewport=savedViewport;
    assert(views.empty() && SettingsBoundary::service.owners.empty());
}
static void CheckSettingsExitReceiptBoundary() {
    assert(views.empty() && SettingsBoundary::service.owners.empty());
    const auto original=modelTemplate; auto& service=SettingsBoundary::service;
    for(const auto& [key,type]:UI_SettingsStateSchema())
        modelTemplate.state[key]={type==0?StateValue(0.0):type==1?StateValue(false):StateValue(std::string()),""};
    for(unsigned scenario=0;scenario<5;++scenario) {
        service=SettingsBoundary::Service{}; eventPlans.clear(); eventHistory.clear();
        {
            idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0);
            const auto owner=service.created.back();
            // This boundary supplies a finished service receipt. The separate
            // production service tests prove which Apply/Keep outcomes create it.
            gui.SetStateBool("settings.exitReady",true); gui.SetStateBool("settings.exitPending",true);
            assert(!*gui.PendingApplicationCommand());
            if(scenario==4) {
                const auto other=UI_SettingsCreateOwner(); service.exitReceipts.insert(other);
                assert(!*gui.PendingApplicationCommand());
                bool close=true; assert(gui.DispatchApplicationActions(ActionMarker,close) && !close);
                assert(service.exitReceipts.contains(other) && service.exitConsumed.empty());
                UI_SettingsReleaseOwner(other); continue;
            }
            service.exitReceipts.insert(owner);
            assert(!std::strcmp(gui.PendingApplicationCommand(),ActionMarker));
            assert(!std::strcmp(gui.PendingApplicationCommand(),ActionMarker) && service.exitConsumed.empty());
            bool close=false;
            if(scenario==1) {
                views.front()->callback(views.front()->owner,retainedUIViewEvent_t::BeforeResourceReset);
                Live().loaded=false;
                assert(gui.DispatchApplicationActions(ActionMarker,close) && !close);
                assert(service.exitReceipts.contains(owner) && service.exitConsumed.empty());
                Live().loaded=true;
                views.front()->callback(views.front()->owner,retainedUIViewEvent_t::Restored);
                assert(!std::strcmp(gui.PendingApplicationCommand(),ActionMarker));
            } else if(scenario==2) gui.Activate(false,0);
            else if(scenario==3) SettingsEvent(gui,{SettingsAction("begin")});
            assert(gui.DispatchApplicationActions(ActionMarker,close));
            if(scenario<2) {
                assert(close && service.exitConsumed==std::vector<std::uint64_t>{owner});
                assert(!*gui.PendingApplicationCommand());
                assert(gui.DispatchApplicationActions(ActionMarker,close) && !close && service.exitConsumed.size()==1);
            } else {
                assert(!close && service.exitConsumed.empty() && !service.exitReceipts.contains(owner));
                if(scenario==3)assert(service.active==owner);
            }
        }
        assert(views.empty() && service.owners.empty());
    }
    modelTemplate=original; eventPlans.clear();
    assert(views.empty() && service.owners.empty());
}
static void CheckSettingsExitBatchBoundary() {
    assert(views.empty() && SettingsBoundary::service.owners.empty());
    const auto original=modelTemplate; auto& service=SettingsBoundary::service;
    for(const auto& [key,type]:UI_SettingsStateSchema())
        modelTemplate.state[key]={type==0?StateValue(0.0):type==1?StateValue(false):StateValue(std::string()),""};
    for(unsigned scenario=0;scenario<4;++scenario) {
        service=SettingsBoundary::Service{}; eventPlans.clear(); eventHistory.clear();
        consoleObject.open=false; windowFocused=true;
        {
            idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0);
            const auto owner=service.created.back();
            SettingsEvent(gui,{SettingsAction("begin"),SettingsAction("edit",{{"r_brightness",1.6}})}); Drain(gui,{});
            assert(service.active==owner && service.exitReceipts.empty());
            std::vector<ActionInvocation> batch{SettingsAction("applyExit")};
            if(scenario==1)batch.push_back(SettingsAction("begin"));
            if(scenario==2)batch.push_back(SettingsAction("cancel"));
            if(scenario==3)batch.push_back(SettingsAction("edit",{{"r_brightness",1.9}}));
            const auto dispatched=service.dispatches.size(); service.order.clear();
            SettingsEvent(gui,batch);
            assert(*gui.PendingApplicationCommand() && service.exitReceipts.empty() && service.dispatches.size()==dispatched);
            // Real adapter code delivers the entire immutable queue before
            // spending the service receipt. The stand-in's immediate closure
            // models only the already-tested service boundary, not device I/O.
            bool close=false; assert(gui.DispatchApplicationActions(gui.PendingApplicationCommand(),close));
            assert(close==(scenario==0) && service.dispatches.size()==dispatched+batch.size());
            assert(service.dispatches.at(dispatched).accepted && service.live.at("r_brightness")==StateValue(1.6));
            std::vector<std::string> order{"dispatch:settings.system.applyExit","exit-ready:"+std::to_string(owner)};
            if(scenario==0) {
                order.push_back("exit-consumed:"+std::to_string(owner));
                assert(service.exitConsumed==std::vector<std::uint64_t>{owner});
            } else {
                order.push_back("dispatch:"+batch.back().operation);order.push_back("exit-canceled:"+std::to_string(owner));
                assert(service.exitConsumed.empty() && service.dispatches.back().accepted==(scenario==1 || scenario==2));
            }
            assert(service.order==order && service.exitReceipts.empty());
            assert(service.active==(scenario==1?owner:0) && gui.GetStateBool("settings.open")==bool(scenario==1));
            if(scenario==1)assert(service.draft.at("r_brightness")==StateValue(1.6));
            assert(!*gui.PendingApplicationCommand());
            close=true; assert(gui.DispatchApplicationActions(ActionMarker,close) && !close);
            assert(service.dispatches.size()==dispatched+batch.size() && service.order==order);
        }
        assert(views.empty() && service.owners.empty());
    }
    modelTemplate=original; eventPlans.clear();
}
static void CheckAuthoredModalBackBoundary() {
    assert(views.empty());
    eventPlans.clear(); eventHistory.clear();
    for(int scenario=0;scenario<9;++scenario) {
        eventPlans["modalback"]={{{"text",std::string("modal back accepted")}}, {Brightness(1.3)}};
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0);
        const std::string priorText=gui.GetStateString("text");
        auto& runtime=Live(); runtime.modals=1; runtime.authoredModal=true;
        runtime.modalIdentity=71;
        ControlAction back{ControlAction::Kind::Back,modelTemplate.id,"dialog","","modalback"};
        back.modalToken=71;
        bool accepted=false;
        switch(scenario) {
            case 0: accepted=true; break;
            case 1: back.document="previous-document"; break;
            case 2: back.node="previous-dialog"; break;
            case 3: back.event="differentback"; break;
            case 4: back.modalToken=70; break;
            case 5: back.modalToken=0; break;
            case 6: eventPlans["modalback"].fail=true; break;
            case 7: accepted=true; eventPlans["modalback"].advanceModal=true; break;
            case 8: back.event.clear(); break; // Old background/manual Back cannot bypass this scope.
        }
        runtime.actions={back};
        if(scenario==7)runtime.actions.push_back(back); // First event replaces modal ownership.
        const auto history=eventHistory.size();
        sysEvent_t tick{99,0,0}; gui.HandleEvent(&tick,0,nullptr);
        assert(runtime.modals==1); // Authored lifetime is never popped by the adapter.
        assert(runtime.modalBackQueries.size()==(scenario==1?0:scenario==7?2:1));
        assert(eventHistory.size()==history+(accepted || scenario==6?1:0));
        assert(std::string(gui.GetStateString("text"))==(accepted?"modal back accepted":priorText));
        bool close=true; const auto writes=cvars.writes;
        assert(gui.DispatchApplicationActions(ActionMarker,close) && !close);
        assert(cvars.writes==writes+(accepted?1:0));
        assert(gui.DispatchApplicationActions(ActionMarker,close) && !close);
        assert(cvars.writes==writes+(accepted?1:0));
    }
    eventPlans.clear(); eventHistory.clear(); assert(views.empty());
}
static void CheckQueuedControlScopeBoundary() {
    assert(views.empty()); eventPlans.clear(); eventHistory.clear();
    eventPlans["replace"]={{}, {Brightness(1.2)}, false, false, "", true};
    eventPlans["stale"]={{}, {Brightness(1.8)}};
    {
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0);
        auto& runtime=Live(); runtime.modalIdentity=81;
        ControlAction replace{ControlAction::Kind::Activate,modelTemplate.id,"button","","replace"};
        ControlAction staleEvent{ControlAction::Kind::Activate,modelTemplate.id,"same-control","","stale"};
        ControlAction staleValue{ControlAction::Kind::Activate,modelTemplate.id,"same-control","brightness","",1.4,901};
        replace.modalToken=staleEvent.modalToken=staleValue.modalToken=81;
        runtime.latestProposal["same-control"]=901;
        runtime.actions={replace,staleEvent,staleValue};
        sysEvent_t tick{99,0,0}; gui.HandleEvent(&tick,0,nullptr);
        assert(runtime.modalIdentity==82 && runtime.controlActionQueries.size()==3);
        assert((eventHistory==std::vector<std::string>{"replace"}));
        assert(runtime.resolvedActions.empty());
        assert(runtime.acknowledgements.size()==1 && runtime.acknowledgements[0].matched && !runtime.acknowledgements[0].accepted);
        // The first program was committed; later raw records cannot reuse the
        // same control ID after that program replaced scope ownership.
        Drain(gui,{{"r_brightness",1.2}});
        staleEvent.modalToken=82; runtime.actions={staleEvent};
        gui.HandleEvent(&tick,0,nullptr);
        assert((eventHistory==std::vector<std::string>{"replace","stale"}));
        Drain(gui,{{"r_brightness",1.8}});
    }
    eventPlans.clear(); eventHistory.clear(); assert(views.empty());
}
static void CheckPendingControlScopeBoundary() {
    assert(views.empty()); eventPlans.clear(); eventHistory.clear();
    eventPlans["committed"]={{}, {Brightness(1.6)}};
    for(int scenario=0;scenario<3;++scenario) {
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0);
        auto& runtime=Live(); runtime.modalIdentity=91; runtime.latestProposal["button"]=902;
        runtime.expectedControlDescriptors["button"]={"brightness",""};
        ControlAction physical{ControlAction::Kind::Activate,modelTemplate.id,"button","brightness","",1.4,902};
        physical.modalToken=91; runtime.actions={physical};
        sysEvent_t tick{99,0,0}; gui.HandleEvent(&tick,0,nullptr);
        assert(runtime.resolvedActions.size()==1 && runtime.acknowledgements.empty());
        if(scenario==0)++runtime.modalIdentity;
        if(scenario==1)runtime.disabledControls.insert("button");
        // A service/async transition may occur after lowering but before the
        // session drains. Reject only that physical record, preserving programs.
        gui.HandleNamedEvent("committed");
        if(scenario==2)Drain(gui,{{"r_brightness",1.0},{"r_brightness",1.6}});
        else Drain(gui,{{"r_brightness",1.6}});
        assert(runtime.acknowledgements.size()==1 && runtime.acknowledgements[0].matched && runtime.acknowledgements[0].accepted==(scenario==2));
        assert(runtime.controlActionQueries.back().action=="brightness" && runtime.controlActionQueries.back().event.empty());
        runtime.disabledControls.clear();
        assert(Semantic(gui,"accept",true) && Semantic(gui,"accept",false));
        ++runtime.modalIdentity;
        Drain(gui,{{"r_brightness",1.0}}); // Explicit semantic diagnostics are committed by contract.
    }
    eventPlans.clear(); eventHistory.clear(); assert(views.empty());
}
static void CheckSettingsReturnBoundary() {
    assert(views.empty() && SettingsBoundary::service.owners.empty());
    const auto original=modelTemplate; auto& service=SettingsBoundary::service;
    service=SettingsBoundary::Service{}; eventPlans.clear(); eventHistory.clear();
    modelTemplate.id="openq4.system";
    for(const auto& [key,type]:UI_SettingsStateSchema())
        modelTemplate.state[key]={type==0?StateValue(0.0):type==1?StateValue(false):StateValue(std::string()),""};
    modelTemplate.events["onactivate"]={"onActivate",{}}; modelTemplate.events["onback"]={"onBack",{}};
    modelTemplate.actions["begin"]={"settings.system.begin",{}};
    Expression value; value.type=0; value.literal=1.25;
    modelTemplate.actions["edit"]={"settings.system.edit",{{"r_brightness",value}}};
    modelTemplate.actions["apply"]={"settings.system.apply",{}};
    modelTemplate.actions["cancel"]={"settings.system.cancel",{}};
    const auto qualified=modelTemplate;
    assert(!UI_RetainedSettingsDocument(nullptr) && !UI_RetainedSettingsCanReturn(nullptr));
    {
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui")); gui.Activate(true,0); gui.Redraw(0);
        assert(UI_RetainedSettingsDocument(&gui) && UI_RetainedSettingsCanReturn(&gui));
        SettingsEvent(gui,{SettingsAction("begin")}); Drain(gui,{});
        assert(UI_RetainedSettingsCanReturn(&gui));
        // Caller State() is a pending dictionary. Only a fresh service read may
        // decide whether the owner has unapplied work or a device transition.
        const auto forge=[&] {gui.SetStateBool("settings.open",false); gui.SetStateBool("settings.dirty",false); gui.SetStateBool("settings.busy",false); gui.SetStateInt("settings.phase",1);};
        service.draft["r_brightness"]=1.6; forge(); auto reads=service.reads.size();
        assert(!UI_RetainedSettingsCanReturn(&gui) && service.reads.size()==reads+1);
        assert(!gui.GetStateBool("settings.open")); // Read-only guard does not silently publish over the caller dictionary.
        service.draft=service.baseline;
        service.readOverrides["settings.busy"]=true; forge(); assert(!UI_RetainedSettingsCanReturn(&gui));
        service.readOverrides.clear();
        for(const auto phase:{SettingsPhase::Confirming,SettingsPhase::RecoveryRequired,SettingsPhase::Applying,SettingsPhase::Restoring}) {
            service.readOverrides["settings.phase"]=static_cast<double>(phase); forge(); assert(!UI_RetainedSettingsCanReturn(&gui));
        }
        service.readOverrides.clear();
        gui.SetStateBool("settings.dirty",true); gui.SetStateBool("settings.busy",true); gui.SetStateInt("settings.phase",99);
        assert(UI_RetainedSettingsCanReturn(&gui));
        service.readAvailable=false; assert(!UI_RetainedSettingsCanReturn(&gui) && !UI_RetainedSettingsDocument(&gui)); service.readAvailable=true;
        // Authored Back handles the user's dirty-exit flow; default dismissal
        // must not pre-empt it. A nested semantic modal still gets first refusal.
        eventPlans["onback"]={{{"text",std::string("authored back result")}}, {Brightness(1.4)}};
        Key(gui,K_ESCAPE,true); Key(gui,K_ESCAPE,false); Drain(gui,{{"r_brightness",1.4}});
        assert(std::string(gui.GetStateString("text"))=="authored back result" && eventHistory.back()=="onback");
        const auto history=eventHistory.size(); Live().modals=1;
        Key(gui,K_ESCAPE,true); Key(gui,K_ESCAPE,false); Drain(gui,{}); assert(eventHistory.size()==history && Live().modals==0);
        eventPlans["onback"].fail=true;
        Key(gui,K_ESCAPE,true); Key(gui,K_ESCAPE,false); Drain(gui,{}); // Failed authored event does not fall through to dismiss.
        eventPlans.clear();
    }
    // The production helper inspects document identity, lifecycle contract,
    // complete reserved typed schema and required catalog operations.
    for(unsigned mutation=0;mutation<5;++mutation) {
        modelTemplate=qualified;
        switch(mutation) {
            case 0: modelTemplate.id="other.page"; break;
            case 1: modelTemplate.events.erase("onactivate"); break;
            case 2: modelTemplate.events.erase("onback"); break;
            case 3: modelTemplate.state.erase("settings.canRetry"); break;
            case 4: modelTemplate.actions.erase("apply"); break;
        }
        idUserInterfaceRetained gui; assert(gui.InitFromFile("test.q4ui"));
        assert(!UI_RetainedSettingsDocument(&gui));
    }
    assert(views.empty() && service.owners.empty()); modelTemplate=original; eventPlans.clear();
}
static void CheckNumberDraftBoundary() {
    assert(views.empty() && SettingsBoundary::service.owners.empty());
    const auto original=modelTemplate;auto& service=SettingsBoundary::service;
    service=SettingsBoundary::Service{};eventPlans.clear();consoleObject.open=false;windowFocused=true;
    for(const auto& [key,type]:UI_SettingsStateSchema())
        modelTemplate.state[key]={type==0?StateValue(0.0):type==1?StateValue(false):StateValue(std::string()),""};
    modelTemplate.state["ui.numberDraftsPending"]={false,""};
    modelTemplate.state["ui.numberDraftMessage"]={std::string(),""};
    Expression operand;operand.type=0;operand.inputValue=true;
    modelTemplate.actions["number.settings"]={"settings.system.edit",{{"r_brightness",operand}},std::size_t(0)};
    modelTemplate.root.control->action="number.settings";
    std::string error;
    for(unsigned variant=0;variant<4;++variant) {
        auto bad=modelTemplate;
        if(variant==0)bad.state["ui.numberDraftsPending"].cvar="r_shadows";
        if(variant==1)bad.state["ui.numberDraftsPending"].initial=1.0;
        if(variant==2) {bad.state.erase("ui.numberDraftsPending");bad.state["UI.NUMBERDRAFTSPENDING"]={false,""};}
        if(variant==3) {EventStep step;step.values["UI.NUMBERDRAFTSPENDING"]=Expression{};bad.events["forge"].steps.push_back(step);}
        assert(!ValidateApplication(bad,error));
    }
    {
        idUserInterfaceRetained gui;assert(gui.InitFromFile("test.q4ui"));gui.Activate(true,0);gui.Redraw(0);
        SettingsEvent(gui,{SettingsAction("begin")});Drain(gui,{});
        auto& runtime=Live();runtime.InstallNumber("root","number.settings");runtime.selected="root";
        assert(runtime.BeginNumberEdit("root",error,0));
        assert(runtime.ReplaceNumberSelection("root",runtime.widgets.at("root").number->identity,"1.375",error,0));
        runtime.widgets.at("root").number->active=false;runtime.selected="other";
        gui.Redraw(0);
        assert(gui.GetStateBool("ui.numberDraftsPending") && !gui.GetStateBool("settings.dirty"));
        assert(std::string(gui.GetStateString("ui.numberDraftMessage"))=="#str_230006");
        assert(!UI_RetainedSettingsCanReturn(&gui));
        const auto text=runtime.numberBuffers.at("root").State().text;
        gui.SetStateBool("UI.NUMBERDRAFTSPENDING",false);gui.DeleteStateVar("ui.numberDraftMessage");
        assert(gui.GetStateBool("ui.numberDraftsPending") && std::string(gui.GetStateString("ui.numberDraftMessage"))=="#str_230006");
        StateValues caller;assert(ApplicationState(modelTemplate,gui.State(),caller,error));
        assert(!caller.contains("ui.numberDraftsPending") && !caller.contains("ui.numberDraftMessage"));
        assert(!UI_RetainedDiagnostic(&gui,idCmdArgs{{"retained","state","ui.numberDraftsPending","0"}}));
        assert(!UI_RetainedDiagnostic(&gui,idCmdArgs{{"retained","pending","UI.NUMBERDRAFTMESSAGE","fake"}}));
        for(const auto& action:std::vector<ActionInvocation>{SettingsAction("apply"),SettingsAction("applyExit"),SettingsAction("defaults"),
                SettingsAction("edit",{{"r_brightness",1.75}}),{"dismiss","ui.dismiss",{}}}) {
            const auto dispatched=service.dispatches.size();SettingsEvent(gui,{action});Drain(gui,{});
            assert(service.dispatches.size()==dispatched && runtime.numberBuffers.at("root").State().text==text);
        }
        // Independent setting edits stay available, with the local field intact.
        SettingsEvent(gui,{SettingsAction("edit",{{"r_shadows",false}})});Drain(gui,{});
        assert(service.draft.at("r_shadows")==StateValue(false) && runtime.numberBuffers.at("root").State().text==text);
        SettingsEvent(gui,{{"focus-draft","ui.numberDrafts.focus",{}}});Drain(gui,{});
        assert(runtime.selected=="root" && runtime.widgets.at("root").number->active && runtime.draftFocusCalls.back()=="root");
        assert(runtime.numberBuffers.at("root").State().text==text);
        runtime.draftQueryAvailable=false;assert(!UI_RetainedSettingsCanReturn(&gui));runtime.draftQueryAvailable=true;
        // Explicit discard does not lose text if service rollback fails.
        service.rejectDispatch=true;const auto discards=runtime.draftDiscardCalls;
        SettingsEvent(gui,{SettingsAction("cancel"),{"dismiss","ui.dismiss",{}}});Drain(gui,{});
        assert(runtime.draftDiscardCalls==discards && runtime.numberBuffers.at("root").State().text==text);
        service.rejectDispatch=false;
        // An accepted display cancel is only a queued rollback. A failed or
        // unfinished restore must retain field text and cannot authorize close.
        service.cancelQueuesRestore=true;
        SettingsEvent(gui,{SettingsAction("cancel"),{"dismiss","ui.dismiss",{}}});Drain(gui,{});
        assert(runtime.draftDiscardCalls==discards && runtime.numberBuffers.at("root").State().text==text);
        assert(!UI_RetainedSettingsCanReturn(&gui));
        service.cancelQueuesRestore=false;service.readOverrides.clear();
        // A callback changing the captured draft rejects the whole local discard.
        service.afterDispatch=[&] {++Runtime::numberToken;};
        SettingsEvent(gui,{SettingsAction("cancel"),{"dismiss","ui.dismiss",{}}});Drain(gui,{});
        service.afterDispatch={};
        assert(runtime.draftDiscardCalls==discards+1 && runtime.numberBuffers.at("root").State().text==text);
        assert(!UI_RetainedSettingsCanReturn(&gui)); // Service is closed; local text still blocks return.
        service.rejectDispatch=true;
        SettingsEvent(gui,{SettingsAction("cancel")});Drain(gui,{});
        assert(runtime.draftDiscardCalls==discards+1 && runtime.numberBuffers.at("root").State().text==text);
        service.rejectDispatch=false;
        // A rejected earlier Dismiss cannot become valid retroactively when a
        // later explicit discard removes the draft in the same action batch.
        SettingsEvent(gui,{{"dismiss","ui.dismiss",{}},SettingsAction("cancel")});Drain(gui,{});
        assert(!runtime.widgets.at("root").number && UI_RetainedSettingsCanReturn(&gui));
        SettingsEvent(gui,{{"dismiss","ui.dismiss",{}}});Drain(gui,{},true);
    }
    {
        // A completed Apply-and-exit callback can introduce a new local draft.
        // Consume the service receipt once, then refuse the stale close result.
        idUserInterfaceRetained gui;assert(gui.InitFromFile("test.q4ui"));gui.Activate(true,0);gui.Redraw(0);
        SettingsEvent(gui,{SettingsAction("begin")});Drain(gui,{});
        auto& runtime=Live();runtime.InstallNumber("root","number.settings");runtime.selected="root";
        service.afterDispatch=[&] {
            assert(runtime.BeginNumberEdit("root",error,0));
            assert(runtime.ReplaceNumberSelection("root",runtime.widgets.at("root").number->identity,"1.625",error,0));
        };
        const auto consumed=service.exitConsumed.size();
        SettingsEvent(gui,{SettingsAction("applyExit")});Drain(gui,{});service.afterDispatch={};
        assert(service.exitConsumed.size()==consumed+1 && service.exitReceipts.empty());
        assert(!UI_RetainedSettingsCanReturn(&gui) && runtime.numberBuffers.at("root").State().text=="1.625");
        SettingsEvent(gui,{SettingsAction("cancel"),{"dismiss","ui.dismiss",{}}});Drain(gui,{},true);
    }
    modelTemplate=original;eventPlans.clear();assert(views.empty() && service.owners.empty());
}
static void CheckChoiceDiagnosticBoundary() {
    assert(views.empty());
    idUserInterfaceRetained gui;assert(gui.InitFromFile("test.q4ui"));gui.Activate(true,0);gui.Redraw(0);
    auto& runtime=Live();WidgetViewState widget;widget.role=ControlRole::Choice;widget.accepted=2.0;
    widget.popupOpen=true;widget.popupToken=731;widget.popupRevision=734;widget.popupOffsetDp=42;
    ScrollReadback scroll;scroll.geometryToken=901;scroll.available=true;scroll.dpRatio=1.25;
    scroll.geometry={100,300,52.5,100,25,13.125,75,true};widget.scroll=scroll;
    runtime.widgets["root"]=widget;
    const auto diagnostic=[&](std::vector<std::string> args) {
        args.insert(args.begin(),{"retained","choice"});return UI_RetainedDiagnostic(&gui,idCmdArgs{std::move(args)});
    };
    assert(diagnostic({"open","root"}));assert(runtime.choiceCalls.back().operation=="open"&&runtime.choiceCalls.back().seconds==presentationTime);
    assert(diagnostic({"close","root"}));assert(runtime.choiceCalls.back().token==731);
    for(const auto& [name,step]:std::map<std::string,ScrollStep>{{"line-back",ScrollStep::LineBackward},{"line-forward",ScrollStep::LineForward},
        {"page-back",ScrollStep::PageBackward},{"page-forward",ScrollStep::PageForward},{"start",ScrollStep::Start},{"end",ScrollStep::End}}) {
        assert(diagnostic({"scroll","root",name}));const auto& call=runtime.choiceCalls.back();
        assert(call.operation=="scroll"&&call.id=="root"&&call.token==731&&call.step==step&&call.seconds==presentationTime);
    }
    const auto calls=runtime.choiceCalls.size();
    for(const auto& args:std::vector<std::vector<std::string>>{{"open"},{"open","root","extra"},{"close","root","extra"},
        {"scroll","root"},{"scroll","root","end","extra"},{"scroll","root","bogus"},{"accept","root"},{"open","missing"}})
        assert(!diagnostic(args)&&runtime.choiceCalls.size()==calls);
    gui.Activate(false,0);assert(!diagnostic({"open","root"})&&runtime.choiceCalls.size()==calls);gui.Activate(true,0);
    gui.SetInteractive(false);assert(!diagnostic({"open","root"})&&runtime.choiceCalls.size()==calls);gui.SetInteractive(true);
    runtime.widgets.at("root").role=ControlRole::Toggle;assert(!diagnostic({"open","root"})&&runtime.choiceCalls.size()==calls);
    runtime.widgets.at("root").role=ControlRole::Choice;runtime.choiceResult=false;
    assert(!diagnostic({"open","root"})&&runtime.choiceCalls.size()==calls+1);
    commonObject.prints.clear();assert(UI_RetainedDiagnostic(&gui,idCmdArgs{{"retained","widget","root"}}));
    assert(commonObject.prints.size()==2&&commonObject.prints.front().find("accepted=2 pending=0")!=std::string::npos);
    assert(commonObject.prints.back()=="RETAINED_GUI_CHOICE_SCROLL id=root open=1 opening=731 revision=734 offsetDp=42 available=1 usable=1 density=1.25 viewport=100 range=300 offset=52.5 track=100 thumb=25 position=13.125 travel=75 geometry=901\n");
    assert(runtime.widgets.at("root").accepted==widget.accepted&&!runtime.widgets.at("root").pending&&runtime.menu.empty());
}
static void CheckNumberDiagnosticBoundary() {
    assert(views.empty() && SettingsBoundary::service.owners.empty());
    const auto original=modelTemplate;auto& service=SettingsBoundary::service;
    service=SettingsBoundary::Service{};eventPlans.clear();consoleObject.open=false;windowFocused=true;
    Expression operand;operand.type=0;operand.inputValue=true;
    modelTemplate.actions["number.settings"]={"settings.system.edit",{{"r_brightness",operand}},std::size_t(0)};
    for(const auto& [key,type]:UI_SettingsStateSchema())
        modelTemplate.state[key]={type==0?StateValue(0.0):type==1?StateValue(false):StateValue(std::string()),""};
    {
        idUserInterfaceRetained gui;assert(gui.InitFromFile("test.q4ui"));gui.Activate(true,0);gui.Redraw(0);
        SettingsEvent(gui,{SettingsAction("begin")});Drain(gui,{});
        auto& runtime=Live();runtime.InstallNumber("root","number.settings");
        assert(UI_RetainedDiagnostic(&gui,idCmdArgs{{"retained","focus","root"}}));
        const auto diagnostic=[&](std::vector<std::string> args) {
            args.insert(args.begin(),{"retained","number"});return UI_RetainedDiagnostic(&gui,idCmdArgs{std::move(args)});
        };
        const auto identity=[&]{return runtime.widgets.at("root").number->identity;};
        assert(diagnostic({"begin","root"}));assert(runtime.numberCalls.back().operation=="begin" && runtime.numberCalls.back().seconds==presentationTime);
        for(const auto& args:std::vector<std::vector<std::string>>{
            {"select","root","-1","0"},{"select","root","0","65537"},{"select","root","0","1junk"},
            {"select","root","99999999999999999999999","0"},{"select","root","+1","0"},
            {"select","root","","0"},{"begin","root","extra"},{"unknown","root"},
            {"preedit","root","\xc3\xa9","1","0"},{"preedit","root","x","-1","1"},
            {"preedit","root","x","0junk","1"},{"input","root","\xc0\xaf"},
            {"replace","root"},{"undo","root","extra"}}) {
            const auto calls=runtime.numberCalls.size();const auto before=identity();
            assert(!diagnostic(args) && runtime.numberCalls.size()==calls && identity()==before);
        }
        auto expected=identity();assert(diagnostic({"replace","root","1.25"}));
        assert(runtime.numberCalls.back().expected==expected && runtime.numberCalls.back().text=="1.25");
        expected=identity();assert(diagnostic({"command","root","left","extend"}));
        assert(runtime.numberCalls.back().operation=="command" && runtime.numberCalls.back().expected==expected && runtime.numberCalls.back().option &&
            runtime.numberBuffers.at("root").State().anchor==4 && runtime.numberBuffers.at("root").State().caret==3);
        const auto commandCalls=runtime.numberCalls.size();
        assert(!diagnostic({"command","root","unknown"}) && !diagnostic({"command","root","left","bad"}) && runtime.numberCalls.size()==commandCalls);
        expected=identity();assert(diagnostic({"select","root","4","1"}));
        assert(runtime.numberCalls.back().expected==expected && runtime.numberCalls.back().anchor==4 && runtime.numberCalls.back().caret==1);
        for(const auto& [name,notice]:std::map<std::string,NumberEditNotice>{{"read",NumberEditNotice::ClipboardReadFailed},
                {"write",NumberEditNotice::ClipboardWriteFailed},{"rejected",NumberEditNotice::ClipboardRejected},{"none",NumberEditNotice::None}}) {
            const auto before=identity();assert(diagnostic({"notice","root",name}));
            assert(identity()==before && runtime.widgets.at("root").number->notice==notice);
        }
        assert(!diagnostic({"notice","root","raw-native-error"}));
        assert(diagnostic({"select","root","0","4"}));expected=identity();
        assert(diagnostic({"preedit","root","\xc3\xa9","0","2"}));
        const auto packet=runtime.numberCalls.back();assert(packet.expected==expected && packet.input && packet.input->kind==TextInputKind::Preedit &&
            packet.input->text=="\xc3\xa9" && packet.input->selectionStart==0 && packet.input->selectionLength==2);
        expected=identity();assert(diagnostic({"input","root","1.75"}));
        assert(runtime.numberCalls.back().expected==expected && runtime.numberCalls.back().input->kind==TextInputKind::Commit &&
            runtime.numberBuffers.at("root").State().text=="1.75");
        expected=identity();assert(diagnostic({"undo","root"}));
        assert(runtime.numberCalls.back().expected==expected && !runtime.numberCalls.back().option && runtime.numberBuffers.at("root").State().text=="1.25");
        expected=identity();assert(diagnostic({"redo","root"}));
        assert(runtime.numberCalls.back().expected==expected && runtime.numberCalls.back().option && runtime.numberBuffers.at("root").State().text=="1.75");
        runtime.widgets.at("root").number->conflict=true;expected=identity();assert(diagnostic({"keep","root"}));
        assert(runtime.numberCalls.back().expected==expected && runtime.numberCalls.back().option && runtime.numberBuffers.at("root").State().text=="1.75");
        runtime.widgets.at("root").number->conflict=true;expected=identity();assert(diagnostic({"reload","root"}));
        assert(runtime.numberCalls.back().expected==expected && !runtime.numberCalls.back().option && runtime.numberBuffers.at("root").State().text=="1");
        assert(diagnostic({"select","root","0","1"}) && diagnostic({"replace","root","private;quit"}));
        const auto writes=service.dispatches.size();assert(!diagnostic({"commit","root"}) && service.dispatches.size()==writes && !*gui.PendingApplicationCommand());
        commonObject.prints.clear();assert(UI_RetainedDiagnostic(&gui,idCmdArgs{{"retained","widget","root"}}));
        assert(runtime.numberGeometryReads.back()=="root");
        assert(commonObject.prints.back().find("RETAINED_GUI_NUMBER id=root active=1 bytes=12 ")!=std::string::npos);
        for(const auto& line:commonObject.prints)assert(line.find("private;quit")==std::string::npos);
        expected=identity();assert(diagnostic({"cancel","root"}));
        assert(runtime.numberCalls.back().expected==expected && !runtime.widgets.at("root").number);
        assert(!diagnostic({"replace","root","2"}) && !diagnostic({"begin","missing"}));
        // Every valid semantic proposal must still retain and recheck its edit
        // descriptor at actual dispatch, even though ordinary semantic actions
        // are not physical-input-cancellable.
        for(unsigned stale=0;stale<3;++stale) {
            assert(diagnostic({"begin","root"}));assert(diagnostic({"replace","root","1.25"}));
            const auto source=identity();assert(diagnostic({"commit","root"}));
            const auto token=runtime.latestProposal.at("root");const auto dispatches=service.dispatches.size();
            if(stale==0)++runtime.widgets.at("root").number->identity.revision;
            if(stale==1)++runtime.widgets.at("root").number->identity.session;
            if(stale==2)++runtime.modalIdentity;
            Drain(gui,{});
            const auto& query=runtime.controlActionQueries.back();
            assert(query.editSession==source.session && query.editRevision==source.revision && query.proposalToken==token && query.action=="number.settings" && query.proposal==StateValue(1.25));
            assert(service.dispatches.size()==dispatches && !runtime.acknowledgements.back().accepted && runtime.acknowledgements.back().token==token);
            assert(diagnostic({"cancel","root"}));
        }
        assert(diagnostic({"begin","root"}));assert(diagnostic({"replace","root","1.375"}));
        const auto source=identity();assert(diagnostic({"commit","root"}));const auto token=runtime.latestProposal.at("root");
        Drain(gui,{});
        const auto& accepted=runtime.acknowledgements.back();
        assert(accepted.token==token && accepted.accepted && accepted.matched && accepted.readback.at("settings.draft.r_brightness")==StateValue(1.375));
        assert(runtime.controlActionQueries.back().editSession==source.session && runtime.controlActionQueries.back().editRevision==source.revision);
        // A successful service write is insufficient when publishing its fresh
        // authoritative state into Runtime fails. The Ack must remain false.
        assert(diagnostic({"cancel","root"}) && diagnostic({"begin","root"}));
        assert(diagnostic({"replace","root","1.625"}) && diagnostic({"commit","root"}));
        const auto failedToken=runtime.latestProposal.at("root");
        service.afterDispatch=[&]{runtime.failState=true;};
        bool close=false;assert(gui.DispatchApplicationActions(ActionMarker,close) && !close);
        const auto& failed=runtime.acknowledgements.back();
        assert(failed.token==failedToken && !failed.accepted && failed.matched && failed.readback.at("settings.draft.r_brightness")==StateValue(1.375));
        assert(service.draft.at("r_brightness")==StateValue(1.625));
        service.afterDispatch={};runtime.failState=false;
        const auto acknowledgements=runtime.acknowledgements.size();assert(gui.DispatchApplicationActions(ActionMarker,close));
        assert(runtime.acknowledgements.size()==acknowledgements);
    }
    assert(views.empty() && service.owners.empty());modelTemplate=original;eventPlans.clear();
}
static void CheckNumberKeys() {
    assert(views.empty());const auto original=modelTemplate;
    Expression operand;operand.type=0;operand.inputValue=true;
    modelTemplate.actions["number.set"]={"settings.brightness.set",{{"value",operand}},std::size_t(0)};
    consoleObject.open=false;windowFocused=true;
    {
        idUserInterfaceRetained gui;assert(gui.InitFromFile("test.q4ui"));gui.Activate(true,0);gui.Redraw(0);
        auto& runtime=Live();runtime.InstallNumber("root","number.set");runtime.InstallNumber("other","number.set");runtime.selected="root";
        std::string error;assert(runtime.BeginNumberEdit("root",error,0));
        const auto identity=[&]{return runtime.widgets.at(runtime.selected).number->identity;};
        const auto state=[&]{return runtime.numberBuffers.at(runtime.selected).State();};
        const auto unchanged=[&](const TextEditState& before){const auto current=state();return current.text==before.text && current.anchor==before.anchor && current.caret==before.caret;};
        const auto replace=[&](const std::string& text){assert(runtime.ReplaceNumberSelection(runtime.selected,identity(),text,error,0));};
        const auto pulse=[&](int key){Key(gui,key,true);Key(gui,key,false);};
        replace("1.25");runtime.menu.clear();
        pulse(K_LEFTARROW);assert(state().anchor==3 && state().caret==3);
        Key(gui,K_SHIFT,true);pulse(K_LEFTARROW);Key(gui,K_SHIFT,false);
        assert(state().anchor==3 && state().caret==2);
        pulse(K_BACKSPACE);assert(state().text=="1.5" && state().caret==2);
        Key(gui,K_CTRL,true);pulse('z');Key(gui,K_CTRL,false);
        assert(state().text=="1.25" && state().anchor==3 && state().caret==2);
        Key(gui,K_CTRL,true);Key(gui,K_SHIFT,true);pulse('z');Key(gui,K_SHIFT,false);Key(gui,K_CTRL,false);
        assert(state().text=="1.5");
        Key(gui,K_CTRL,true);pulse('a');Key(gui,K_CTRL,false);
        assert(state().anchor==0 && state().caret==3);replace("1.25");
        pulse(K_HOME);assert(state().caret==0);pulse(K_DEL);assert(state().text==".25");
        Key(gui,K_CTRL,true);pulse('z');Key(gui,K_CTRL,false);assert(state().text=="1.25");
        pulse(K_END);Key(gui,K_LEFTARROW,true);assert(state().caret==3);
        Key(gui,K_LEFTARROW,true);assert(state().caret==2); // Native repeat, same owner.
        runtime.selected="other";assert(runtime.BeginNumberEdit("other",error,0));replace("1.75");
        const auto other=state();const auto calls=runtime.numberCalls.size();
        Key(gui,K_LEFTARROW,true);assert(unchanged(other) && runtime.numberCalls.size()==calls);
        runtime.selected="root";Key(gui,K_LEFTARROW,true);assert(runtime.numberCalls.size()==calls);
        Key(gui,K_LEFTARROW,false);pulse(K_LEFTARROW);assert(state().caret==1);
        assert(runtime.menu.empty()); // Text commands never become menu navigation.
        // A menu-held source cannot be reclassified as a text key mid-press.
        Key(gui,K_HOME,true);consoleObject.open=true;gui.Redraw(0);consoleObject.open=false;gui.Redraw(0);
        const auto suspended=state();const auto afterSuspend=runtime.numberCalls.size();
        Key(gui,K_HOME,true);assert(unchanged(suspended) && runtime.numberCalls.size()==afterSuspend);
        Key(gui,K_HOME,false);pulse(K_END);assert(state().caret==4);
        runtime.menu.clear();pulse(K_SPACE);assert(runtime.menu.empty() && state().text=="1.25");
        // SDL omits Ctrl/Alt key events. Captured per-event modifiers remain
        // correct after the physical modifier has already been released.
        const auto captured=[&](int key,bool down,openq4::KeyEventMetadata metadata){
            auto bytes=openq4::EncodeKeyEventMetadata(metadata);sysEvent_t event{SE_KEY,key,down?1:0};
            event.evPtr=bytes.data();event.evPtrLength=static_cast<int>(bytes.size());return gui.HandleEvent(&event,0,nullptr);
        };
        captured('a',true,{true,false,false,false});captured('a',false,{});
        assert(state().anchor==0 && state().caret==4);
        captured(K_END,true,{});captured(K_END,false,{});
        captured(K_LEFTARROW,true,{false,true,false,false});captured(K_LEFTARROW,false,{});
        assert(state().anchor==4 && state().caret==3);
        const auto beforeAlt=state();captured('a',true,{true,false,true,false});captured('a',false,{});assert(unchanged(beforeAlt));
        for(const auto key:{K_BACKSPACE,K_DEL}) {captured(key,true,{true,false,false,false});captured(key,false,{});assert(unchanged(beforeAlt));}
        captured(K_DEL,true,{false,true,false,false});captured(K_DEL,false,{});assert(unchanged(beforeAlt));
        uiClipboardRequest_t cut;assert(gui.TakeClipboardRequest(ActionMarker,cut) && cut.operation==uiClipboardOperation_t::Cut);
        const auto beforeOrphan=runtime.numberCalls.size();captured(K_LEFTARROW,true,{false,false,false,true});captured(K_LEFTARROW,false,{});
        assert(runtime.numberCalls.size()==beforeOrphan);
        captured(K_END,true,{});captured(K_END,false,{});
        const auto writes=cvars.writes;
        const auto* command=Key(gui,K_ENTER,true);assert(!std::strcmp(command,ActionMarker));
        const auto beforeRepeat=runtime.numberCalls.size();Key(gui,K_ENTER,true);Key(gui,K_ENTER,false);
        assert(runtime.numberCalls.size()==beforeRepeat && runtime.menu.empty());
        bool close=false;assert(gui.DispatchApplicationActions(ActionMarker,close) && !close && cvars.writes==writes+1 && cvars.brightness==1.25f);
        // Invalid local text and active preedit refuse Enter without closing.
        Key(gui,K_CTRL,true);pulse('a');Key(gui,K_CTRL,false);replace("1e");
        assert(!*Key(gui,K_ENTER,true));Key(gui,K_ENTER,false);assert(cvars.writes==writes+1 && state().text=="1e");
        TextInputEvent preedit;assert(MakeTextInputPreedit("2",TextIndexUnit::Utf8Bytes,0,1,preedit,error));
        assert(runtime.ApplyNumberInput("root",identity(),preedit,error,0));const auto composing=state();
        pulse(K_BACKSPACE);assert(unchanged(composing) && runtime.numberBuffers.at("root").Composition());
    }
    modelTemplate=original;
    // Shared source bookkeeping cannot let a held text key mask an unrelated
    // menu Accept, emit a synthetic menu release, or survive an owner change.
    Input input;using T=Input::TextKey;
    assert(input.ClaimTextKey(1,41,true,false)==T::Press);
    input.Menu(2,MenuInput::Accept,true,false,0);auto events=input.Take();assert(events.size()==1 && events[0].down);
    input.Menu(2,MenuInput::Accept,false,false,0);events=input.Take();assert(events.size()==1 && !events[0].down);
    assert(input.ClaimTextKey(1,42,true,true)==T::Consumed);
    assert(input.ClaimTextKey(1,41,true,true)==T::Consumed);
    assert(input.ClaimTextKey(1,0,false,false)==T::Consumed && input.Take().empty());
    assert(input.ClaimTextKey(1,42,true,true)==T::Consumed);
    assert(input.ClaimTextKey(1,42,true,false)==T::Press);
    input.Cancel();input.Take();assert(input.ClaimTextKey(1,42,true,false)==T::Consumed);
    input.ReleaseQuarantined(1);assert(input.ClaimTextKey(1,42,true,false)==T::Press);
    input.Cancel(true);input.Take();assert(input.ClaimTextKey(1,42,true,true)==T::Consumed);
}
int main() {
    modelTemplate.id="adapter-document";
    modelTemplate.state={{"number",{1.0,""}},{"flag",{true,""}},{"text",{std::string("default"),""}},{"host",{1.0,"host_cvar"}}};
    Expression number; number.type=0; number.state="number";
    Expression flag; flag.type=1; flag.state="flag";
    modelTemplate.actions={{"brightness",{"settings.brightness.set",{{"value",number}}}},
                           {"shadows",{"settings.shadows.set",{{"value",flag}}}}, {"dismiss",{"ui.dismiss",{}}}};
    modelTemplate.root.id="root";
    modelTemplate.root.control=Control{}; modelTemplate.root.control->action="brightness";
    files.sources={{"test.q4ui","valid"},{"bad.q4ui","invalid"},{"next.q4ui","next"},{"menuXq4ui","valid"}};
    CheckPresentationBridge();
    {
        idUserInterfaceRetained gui;
        gui.SetStateFloat("number",1.25f); gui.SetStateBool("flag",true);
        gui.SetStateString("text","literal ; quit"); gui.SetStateString("host","not a number");
        assert(gui.InitFromFile("test.q4ui") && std::get<double>(Live().state.at("number"))==1.25);
        assert(gui.IsInteractive() && !gui.Active() && !gui.HasInteractiveOverride());
        gui.SetStateBool("noninteractive",true); gui.StateChanged(0); assert(!gui.IsInteractive());
        gui.SetStateBool("noninteractive",false); gui.StateChanged(0); assert(gui.IsInteractive());
        assert(!Live().state.contains("host") && gui.GetTimeStamp()==91);
        const auto* first=views.front();
        assert(!gui.InitFromFile("bad.q4ui") && views.front()==first);
        assert(!gui.InitFromFile("menuXq4ui") && views.front()==first);
        rejectLoad=true; assert(!gui.InitFromFile("next.q4ui") && views.front()==first); rejectLoad=false;
        gui.SetStateString("number","nan"); gui.StateChanged(0);
        assert(std::get<double>(Live().state.at("number"))==1.25);
        gui.SetStateFloat("number",1.5f); gui.StateChanged(0);
        assert(std::get<double>(Live().state.at("number"))==1.5);
        gui.SetStateString("flag","false"); gui.StateChanged(0);
        assert(!std::get<bool>(Live().state.at("flag")));
        gui.SetStateString("flag","2"); gui.StateChanged(0);
        assert(!std::get<bool>(Live().state.at("flag")));
        gui.SetStateBool("flag",true); gui.StateChanged(0);

        // Saving preserves pending application edits separately from the last
        // StateChanged commit that currently drives visible runtime bindings.
        gui.SetStateFloat("number",1.75f); gui.SetCursor(31,47); gui.SetUniqued(true); gui.SetInteractive(true); gui.Activate(true,0);
        idFile_Memory save;
        assert(gui.WriteToSaveGame(&save)); const int frameSize=save.Length();
        assert(save.WriteUnsignedInt(0x53454e54)==4);
        gui.SetStateFloat("number",.75f); gui.StateChanged(0); gui.SetCursor(10,20); gui.SetUniqued(false); gui.SetInteractive(false);
        idFile_Memory restore("save",save.GetDataPtr(),save.Length());
        assert(gui.ReadFromSaveGame(&restore) && restore.Tell()==frameSize);
        unsigned sentinel=0; assert(restore.ReadUnsignedInt(sentinel)==4 && sentinel==0x53454e54 && restore.Tell()==restore.Length());
        assert(gui.GetStateFloat("number")==1.75f && std::get<double>(Live().state.at("number"))==1.5);
        assert(gui.CursorX()==31 && gui.CursorY()==47 && gui.IsUniqued());
        assert(gui.Active() && gui.HasInteractiveOverride());
        gui.StateChanged(0); assert(std::get<double>(Live().state.at("number"))==1.75);
        gui.SetStateBool("noninteractive",true); gui.StateChanged(0); assert(gui.IsInteractive());
        gui.SetStateBool("noninteractive",false); gui.StateChanged(0);

        const std::string valid=save.bytes.substr(0,frameSize);
        for(size_t size=0;size<valid.size();++size)RejectFrame(gui,valid.substr(0,size));
        for(const auto& [offset,value]:std::vector<std::pair<size_t,unsigned>>{{0,0},{4,99},{8,0xffffffffu},{8,0x7fffffffu},{12,4097},{16,65537}}) {
            std::string invalid=valid; PatchWord(invalid,offset,value); RejectFrame(gui,invalid);
        }
        Live().failRestore=true; RejectFrame(gui,valid); Live().failRestore=false;
        RejectFrame(gui,Frame({{"number","1"},{"NUMBER","2"}}));
        RejectFrame(gui,Frame({{"","empty key"}}));
        RejectFrame(gui,Frame({{"number",std::string("1\0junk",6)}}));
        RejectFrame(gui,Frame({{"number","1"}},16));
        RejectFrame(gui,Frame({{"number","1"}},7,std::numeric_limits<float>::quiet_NaN()));
        RejectFrame(gui,Frame({{"number","1"}},7,31,47,true));
        idFile_Memory shortWrite; shortWrite.writeLimit=9; assert(!gui.WriteToSaveGame(&shortWrite));
        Live().failSave=true; idFile_Memory failedSave; assert(!gui.WriteToSaveGame(&failedSave) && failedSave.Length()==0); Live().failSave=false;
        gui.SetStateString("","empty key"); idFile_Memory emptyKey;
        assert(!gui.WriteToSaveGame(&emptyKey) && emptyKey.Length()==0); gui.DeleteStateVar("");
        gui.SetStateString("number","not a numeric value");
        idFile_Memory invalidPending; assert(gui.WriteToSaveGame(&invalidPending));
        gui.SetStateFloat("number",.75f); gui.StateChanged(0);
        idFile_Memory pendingRestore("pending",invalidPending.GetDataPtr(),invalidPending.Length());
        assert(gui.ReadFromSaveGame(&pendingRestore));
        assert(!std::strcmp(gui.GetStateString("number"),"not a numeric value"));
        assert(std::get<double>(Live().state.at("number"))==1.75);
        gui.StateChanged(0); assert(std::get<double>(Live().state.at("number"))==1.75);
        gui.SetStateFloat("number",1.75f); gui.StateChanged(0);

        // The event marker is only a signal to drain this instance's typed
        // queue. Arbitrary supplied text never writes a CVar or closes a menu.
        bool close=true;
        assert(gui.DispatchApplicationActions("quit; set r_brightness 99",close) && !close && cvars.writes==0);
        Key(gui,K_ENTER,true); Key(gui,K_ENTER,true); // OS repeat cannot rearm.
        assert(!std::strcmp(Key(gui,K_ENTER,false),ActionMarker));
        assert(gui.DispatchApplicationActions(ActionMarker,close) && !close && cvars.brightness==1.75f && cvars.writes==1);
        assert(gui.DispatchApplicationActions(ActionMarker,close) && cvars.writes==1);
        const int sourceWrites=cvars.writes;
        Key(gui,K_ENTER,true); Key(gui,K_SPACE,true); Key(gui,K_ENTER,false);
        gui.DispatchApplicationActions(ActionMarker,close); assert(cvars.writes==sourceWrites);
        Key(gui,K_SPACE,false); gui.DispatchApplicationActions(ActionMarker,close); assert(cvars.writes==sourceWrites+1);
        cvars.writes=1; // Remaining assertions count only their own operations.
        gui.SetStateFloat("number",99); gui.StateChanged(0); Key(gui,K_ENTER,true); Key(gui,K_ENTER,false);
        gui.DispatchApplicationActions(ActionMarker,close); assert(cvars.writes==1);
        Live().selected="shadows"; gui.SetStateBool("flag",false); gui.StateChanged(0);
        Key(gui,K_ENTER,true); Key(gui,K_ENTER,false); gui.DispatchApplicationActions(ActionMarker,close);
        assert(cvars.writes==2 && !cvars.shadows);
        Live().modals=1;
        Key(gui,K_ESCAPE,true); gui.DispatchApplicationActions(ActionMarker,close); assert(!close && Live().modals==0); Key(gui,K_ESCAPE,false);
        Key(gui,K_ESCAPE,true); assert(gui.DispatchApplicationActions(ActionMarker,close) && close); Key(gui,K_ESCAPE,false);

        // Suspension cancels arms and ignores their releases. A release while
        // suspended retires the source so the next deliberate press works.
        Live().selected="brightness"; gui.SetStateFloat("number",1.25f); gui.StateChanged(0);
        Key(gui,K_ENTER,true); consoleObject.open=true; gui.Redraw(0); Key(gui,K_ENTER,false);
        consoleObject.open=false; gui.Redraw(0); gui.DispatchApplicationActions(ActionMarker,close);
        assert(cvars.writes==2);
        Key(gui,K_ENTER,true); Key(gui,K_ENTER,false); gui.DispatchApplicationActions(ActionMarker,close);
        assert(cvars.writes==3 && cvars.brightness==1.25f);
        Key(gui,K_ENTER,true); gui.SetInteractive(false); Key(gui,K_ENTER,false); gui.SetInteractive(true);
        Key(gui,K_ENTER,true); Key(gui,K_ENTER,false); gui.DispatchApplicationActions(ActionMarker,close); assert(cvars.writes==4);
        Key(gui,K_ENTER,true); Live().loaded=false; Key(gui,K_ENTER,false);
        Live().loaded=true; Key(gui,K_ENTER,true); Key(gui,K_ENTER,false);
        gui.DispatchApplicationActions(ActionMarker,close); assert(cvars.writes==5);
        Live().loaded=false;
        assert(!std::strcmp(Key(gui,K_ESCAPE,true),ActionMarker));
        assert(gui.DispatchApplicationActions(ActionMarker,close) && close);
        Key(gui,K_ESCAPE,false); Live().loaded=true;
        const int navigation=static_cast<int>(Live().menu.size());
        Key(gui,K_DOWNARROW,true); presentationTime+=.4; sysEvent_t tick{}; gui.HandleEvent(&tick,0,nullptr);
        assert(Live().menu.size()==static_cast<size_t>(navigation+2)); Key(gui,K_DOWNARROW,false);
        idKeyInput::shift=true; Key(gui,K_TAB,true); idKeyInput::shift=false; Key(gui,K_TAB,false);
        assert(Live().menu[Live().menu.size()-2]==std::make_pair(MenuInput::Previous,true));
        assert(Live().menu.back()==std::make_pair(MenuInput::Previous,false));
        gui.SetCursor(320,240); sysEvent_t mouse{SE_MOUSE,0,0}; gui.HandleEvent(&mouse,0,nullptr);
        assert(std::abs(Live().pointerX-520)<.001f && std::abs(Live().pointerY-290)<.001f);
        gui.SetCursor(-2000,-2000); assert(std::abs(gui.CursorX()+106.666667f)<.001f && gui.CursorY()==0);
        cvars.aspect=false; gui.SetCursor(640,480); gui.HandleEvent(&mouse,0,nullptr);
        assert(Live().pointerX==1000 && Live().pointerY==560);
        gui.SetCursor(std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN());
        assert(gui.CursorX()==0 && gui.CursorY()==0);
    }
    assert(views.empty());
    std::string error;
    auto unsafe=modelTemplate; unsafe.actions["brightness"].operation="exec"; assert(!ValidateApplication(unsafe,error));
    unsafe=modelTemplate; unsafe.actions["brightness"].arguments["extra"]=Expression{}; assert(!ValidateApplication(unsafe,error));
    unsafe=modelTemplate; unsafe.state["NUMBER"]={1.0,""}; assert(!ValidateApplication(unsafe,error));
    unsafe=modelTemplate; unsafe.state["NaMe"]={1.0,""}; assert(!ValidateApplication(unsafe,error));
    CheckEventBridge();
    CheckEventEligibility();
    CheckNodeInspection();
    CheckSettingsBoundary();
    CheckSettingsDrawBoundary();
    CheckValueProposalBoundary();
    CheckStationaryWheelHandoff();
    CheckSettingsExitReceiptBoundary();
    CheckAuthoredModalBackBoundary();
    CheckQueuedControlScopeBoundary();
    CheckPendingControlScopeBoundary();
    CheckSettingsExitBatchBoundary();
    CheckSettingsReturnBoundary();
    CheckNumberDraftBoundary();
    CheckNumberDiagnosticBoundary();
    CheckChoiceDiagnosticBoundary();
    CheckNumberKeys();
    std::puts("Retained adapter: Number diagnostic transport, delayed numeric identity and readback-before-acknowledgement; exactly-once Apply-and-exit receipts after complete queued batches, stationary wheel/pointer handoff, immutable value proposals/acknowledgements, authoritative settings return and authored Back, settings capability/draw ownership and state/lifecycle boundaries, ordered event/FIFO publication, restore suppression, pending dictionary, presentation delegation, framed saves, input suspension and cursor mapping passed");
}
'''


def main():
    dependencies = [
        'src/ui/UserInterfaceRetained.cpp', 'src/ui/UserInterface.h', 'src/ui/UserInterfaceManaged.h', 'src/ui/UserInterfaceText.h', 'src/ui/UserInterfaceNativeText.h',
        'src/ui/UserInterfaceRetained.h', 'src/ui/UserInterface.cpp', 'src/ui/RetainedUI.h',
        'src/ui/SettingsService.h', 'src/ui/application/SettingsTransaction.h',
        'src/ui/retained/Document.h', 'src/ui/retained/Interaction.h',
        'src/ui/retained/Input.h', 'src/ui/retained/Input.cpp',
        'src/ui/retained/TextInput.h', 'src/ui/retained/TextInput.cpp',
        'src/ui/retained/TextEdit.h', 'src/ui/retained/TextEdit.cpp',
        'src/ui/retained/TextEditCommand.h', 'src/ui/retained/TextEditCommand.cpp',
        'src/sys/KeyEventMetadata.h',
        'tools/tests/ui_manager_lifecycle.py', 'tools/tests/filesystem_case_segments.py',
        'tools/tests/ui_retained_adapter.py',
    ]
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    before = {path: digest(ROOT / path) for path in dependencies}
    source = (ROOT / 'src/ui/UserInterfaceRetained.cpp').read_text(encoding='utf-8')
    public = (ROOT / 'src/ui/UserInterface.h').read_text(encoding='utf-8')
    managed = (ROOT / 'src/ui/UserInterfaceManaged.h').read_text(encoding='utf-8')
    header = (ROOT / 'src/ui/UserInterfaceRetained.h').read_text(encoding='utf-8')
    factory = (ROOT / 'src/ui/UserInterface.cpp').read_text(encoding='utf-8')
    support = DICTIONARY_SUPPORT[:DICTIONARY_SUPPORT.index('struct idFile {')]
    support = support.replace('int evType=0,evValue=0,evValue2=0;', 'int evType=0,evValue=0,evValue2=0,evPtrLength=0; void* evPtr=nullptr;')
    # The imported stand-in constructs length bytes even after NUL. Settings
    # prefix checks now exercise short IDs; match real idStr's bounded scan.
    old_icmpn = 'static int Icmpn(const char* a,const char* b,int length) { return Icmp(std::string(a,length).c_str(),std::string(b,length).c_str()); }'
    assert old_icmpn in support
    support = support.replace(old_icmpn, '''static int Icmpn(const char* a,const char* b,int length) {
        for(int i=0;i<length;++i) {
            const int x=std::tolower(static_cast<unsigned char>(a[i])),y=std::tolower(static_cast<unsigned char>(b[i]));
            if(x!=y || !x || !y)return x-y;
        }
        return 0;
    }''')
    code = support + ENGINE + RUNTIME + SETTINGS
    code += function_body(public, 'class idUserInterface {') + ';\n'
    code += function_body(managed, 'class idUserInterfaceManaged :') + ';\n'
    code += function_body(header, 'class idUserInterfaceRetained final :') + ';\n' + BASE
    code += function_body(factory, 'std::uint64_t UI_NextTextLifetime(')
    code += function_body(factory, 'bool UI_IsRetainedPath(')
    code += source[source.index('namespace {'):source.index('bool UI_RetainedDiagnostic(')]
    code += function_body(source, 'bool UI_RetainedDiagnostic(') + MAIN
    compiler = next((found for name in ('clang++', 'g++', 'c++') if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    temp = Path(tempfile.mkdtemp(prefix='retained-adapter-', dir=ROOT / '.tmp'))
    environment = {**os.environ, 'TEMP': str(temp), 'TMP': str(temp), 'TMPDIR': str(temp)}
    mutations = [
        ('scrollbar-load-rejected',
         'node->control && node->control->role == ControlRole::Scrollbar', 'false'),
        ('scrollbar-output-accepted',
         'if (!control.action.empty() || !control.event.empty() || control.value ||', 'if (false ||'),
        ('ordinary-action-check-skipped',
         '} else if (node->control && (node->control->event.empty() ?', '} else if (false && (node->control->event.empty() ?'),
        ('draft-caller-authority', 'if (!NumberDraftState(name)) return true;', 'return true;'),
        ('draft-application-authority', ' || NumberDraftState(name.c_str())) continue;', ') continue;'),
        ('draft-settings-dispatch', 'impl->ConflictsWithNumberDraft(pending,drafts)', 'false'),
        ('draft-dismiss',
         'if (!impl->QueryNumberDrafts(drafts) || !drafts.blocking.empty()) {',
         'if (!impl->QueryNumberDrafts(drafts)) {'),
        ('draft-final-close',
         'if (!impl->QueryNumberDrafts(drafts) || !drafts.blocking.empty()) closeRequested = false;',
         'if (!impl->QueryNumberDrafts(drafts)) closeRequested = false;'),
        ('draft-parent-return',
         'if (!(*found)->impl->QueryNumberDrafts(drafts) || !drafts.blocking.empty()) return false;',
         'if (!(*found)->impl->QueryNumberDrafts(drafts)) return false;'),
        ('draft-failed-cancel', 'if (accepted && invocation.operation == "settings.system.cancel")',
         'if (invocation.operation == "settings.system.cancel")'),
        ('draft-queued-rollback',
         '!std::get<bool>(completed.at("settings.open")) && !std::get<bool>(completed.at("settings.busy")) &&\n\t\t\t\t\tstd::get<double>(completed.at("settings.phase")) == static_cast<double>(SettingsPhase::Closed)',
         'true'),
        ('text-key-owner-rebound', 'mapped ? identity.session : 0', 'mapped ? 1 : 0'),
        ('text-key-repeat-commit', 'else if (claim == Input::TextKey::Press)', 'else if (true)'),
        ('text-key-selection-lost', 'movement && shift,error,now', 'false,error,now'),
        ('stale-semantic-number', 'if (pending.cancellable || pending.source.editSession)', 'if (pending.cancellable)'),
        ('ack-without-publication', 'accepted && synchronized', 'accepted'),
        ('ack-before-readback',
         'const bool synchronized = impl->SyncSettings();\n\t\t\tif (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,accepted && synchronized);',
         'if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,accepted);\n\t\t\timpl->SyncSettings();'),
    ]
    cases = [('production', code)]
    for name, old, new in mutations:
        if code.count(old) != 1:
            raise RuntimeError(f'Production mutation anchor is not unique: {name}')
        cases.append((name, code.replace(old, new)))
    report = {'passed': False, 'sources': before, 'scope': __doc__.strip(), 'cases': []}
    try:
        for name, body in cases:
            test_source = temp / (name + '.cpp')
            binary = temp / (name + ('.exe' if os.name == 'nt' else '-test'))
            test_source.write_text(body, encoding='utf-8', newline='\n')
            command = [compiler, '-std=c++20', '-DUSE_SDL3', '-I', str(ROOT), str(test_source),
                       str(ROOT / 'src/ui/retained/Input.cpp'), str(ROOT / 'src/ui/retained/TextInput.cpp'),
                       str(ROOT / 'src/ui/retained/TextEdit.cpp'), str(ROOT / 'src/ui/retained/TextEditCommand.cpp'), '-o', str(binary)]
            compiled = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=120)
            compile_log = temp / (name + '-compile.log')
            compile_log.write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
            record = {'name': name, 'command': command, 'compile_exit': compiled.returncode,
                      'compile_log_sha256': digest(compile_log), 'extracted_source_sha256': digest(test_source)}
            report['cases'].append(record)
            if compiled.returncode:
                raise RuntimeError(compiled.stdout + compiled.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, env=environment, timeout=120)
            run_log = temp / (name + '-run.log')
            run_log.write_text(run.stdout + run.stderr, encoding='utf-8')
            record.update(exit_code=run.returncode, output=run.stdout + run.stderr,
                          run_log_sha256=digest(run_log), binary_sha256=digest(binary))
            if name == 'production':
                if run.returncode:
                    raise RuntimeError(run.stdout + run.stderr)
                print(run.stdout.strip())
            elif not run.returncode or 'assertion' not in (run.stdout + run.stderr).lower():
                raise RuntimeError(f'Compiled mutation did not fail a behavioral assertion: {name}')
            else:
                print(f'Rejected compiled adapter mutation: {name}')
        report['passed'] = True
    except Exception as error:
        report['failure'] = str(error)
        print(error)
    report['sources_unchanged'] = before == {path: digest(ROOT / path) for path in dependencies}
    report['passed'] &= report['sources_unchanged']
    (temp / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'Retained adapter evidence: {temp / "result.json"}')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
