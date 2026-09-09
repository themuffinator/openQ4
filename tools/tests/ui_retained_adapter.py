#!/usr/bin/env python3
"""Exercise production retained GUI adapter state, event queues, saves and input.

The adapter/public headers and Input.cpp are real. Engine I/O, canonical
document loading and runtime rendering are bounded stand-ins; this does not
qualify alias expression ownership, parsing, GPU output or the complete
application action vocabulary. Event effects are prescribed boundary data;
UiBehaviorTest covers actual ordered evaluation and rollback.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

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
#include "src/ui/retained/Input.h"
#include "src/ui/RetainedUI.h"
struct idCmdArgs {
    std::vector<std::string> values;
    int Argc() const {return static_cast<int>(values.size());}
    const char* Argv(int index) const {return values.at(index).c_str();}
};
template<class T> T Min(T a,T b) { return (std::min)(a,b); }
struct idVec2 { idVec2(float=0,float=0) {} } vec2_origin;
enum { SE_KEY=1,SE_MOUSE,K_TAB=10,K_SHIFT,K_UPARROW,K_DOWNARROW,K_LEFTARROW,K_RIGHTARROW,
       K_ENTER,K_KP_ENTER,K_SPACE,K_ESCAPE,K_MOUSE1,K_JOY3,K_JOY4,K_JOY7,K_JOY8,K_JOY9,K_JOY10,K_JOY11,K_JOY12,K_LAST_KEY=512 };
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
    void Warning(const char*,...) { ++warnings; }
    void Printf(const char*,...) {}
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
                                 const PresentationLookup& lookup) const {
    auto found=actions.find(name); if(found==actions.end())return false;
    ActionInvocation candidate; candidate.action=name; candidate.operation=found->second.operation;
    for(auto& [key,expression]:found->second.arguments) {
        if(!expression.presentation.empty()) {
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
};
static std::map<std::string,StubEvent> eventPlans;
static std::vector<std::string> eventHistory;
class Runtime {
public:
    using ActionValidator=std::function<bool(const ActionInvocation&,std::string&)>;
    struct EventEffects { StateValues stateChanges; std::vector<ActionInvocation> actions; };
    struct EventCall { std::string name; StateValues application; size_t maxActions; };
    std::vector<EventCall> eventCalls;
    mutable std::vector<std::string> eventQueries,resolvedActions;
    std::vector<std::string> timelines;
    std::vector<std::pair<std::string,double>> eligibilityQueries;
    std::set<std::string> disabledControls;
    int validations=0;
    std::string FocusedControl() const {return selected;}
    unsigned long long StateRevision() const {return 0;}
    struct Stats {unsigned long long activeContexts=1;};
    Stats Statistics() const {return {};}
    bool FocusControl(const std::string& name,double) {selected=name; return true;}
    static constexpr size_t MaxSnapshotBytes=128u*1024u*1024u;
    static inline std::map<std::string,StateValues> snapshots;
    bool loaded=true,failSave=false,failRestore=false,accept=false,pointer=false;
    int cancels=0,releases=0,frames=0,modals=0;
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
    bool SetState(const StateValues& values,std::string&,double) { state=values; return true; }
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
        return true;
    }
    bool CanActivateControl(const std::string& node,double seconds) {
        eligibilityQueries.emplace_back(node,seconds); return !disabledControls.contains(node);
    }
    bool ResolveAction(const std::string& id,ActionInvocation& result,std::string& error) const {
        resolvedActions.push_back(id);
        return modelTemplate.ResolveAction(id,state,result,error,[&](const std::string& alias,int component,StateValue& value,std::string& why) {
            auto found=aliases.find(alias);
            if(found==aliases.end() || component!=-1) {why="stub presentation lookup failed"; return false;}
            value=std::stod(found->second); return true;
        });
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
    std::vector<ControlAction> TakeActions() { std::vector<ControlAction> result; result.swap(actions); return result; }
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
        if(action==MenuInput::Back && down)actions.push_back({ControlAction::Kind::Back,modelTemplate.id,"button",""});
    }
    void PointerMove(float x,float y,double) { pointerX=x; pointerY=y; }
    bool PlayTimeline(const std::string& name,double) { timelines.push_back(name); return name=="slide"; }
    bool PopModal(double) { if(!modals)return false; --modals; return true; }
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
static bool rejectLoad=false;
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
bool RetainedUI_DrawViewRoot(retainedUIView_t* view,const openq4::ui::Viewport&) { ++view->runtime.frames; return true; }
'''

BASE = r'''
idUserInterfaceManaged::idUserInterfaceManaged(bool enabled):refs(1),allocationId(0),managed(enabled) {}
idUserInterfaceManaged::~idUserInterfaceManaged()=default;
void idUserInterfaceManaged::RegisterLoaded() {}
void idUserInterfaceManaged::RegisterDemo() {}
void idUserInterfaceManaged::RefreshThinking() {}
'''

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
                        {ControlAction::Kind::Back,modelTemplate.id,"later","",""}};
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
                        {ControlAction::Kind::Back,modelTemplate.id,"later","",""}};
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
    std::puts("Retained adapter: ordered event/FIFO publication, lifecycle/restore suppression, pending dictionary, presentation delegation, framed save atomicity, actions, suspension and cursor mapping passed");
}
'''


def main():
    source = (ROOT / 'src/ui/UserInterfaceRetained.cpp').read_text(encoding='utf-8')
    public = (ROOT / 'src/ui/UserInterface.h').read_text(encoding='utf-8')
    managed = (ROOT / 'src/ui/UserInterfaceManaged.h').read_text(encoding='utf-8')
    header = (ROOT / 'src/ui/UserInterfaceRetained.h').read_text(encoding='utf-8')
    factory = (ROOT / 'src/ui/UserInterface.cpp').read_text(encoding='utf-8')
    code = DICTIONARY_SUPPORT[:DICTIONARY_SUPPORT.index('struct idFile {')] + ENGINE + RUNTIME
    code += function_body(public, 'class idUserInterface {') + ';\n'
    code += function_body(managed, 'class idUserInterfaceManaged :') + ';\n'
    code += function_body(header, 'class idUserInterfaceRetained final :') + ';\n' + BASE
    code += function_body(factory, 'bool UI_IsRetainedPath(')
    code += source[source.index('namespace {'):source.index('bool UI_RetainedDiagnostic(')]
    code += function_body(source, 'bool UI_RetainedDiagnostic(') + MAIN
    compiler = next((found for name in ('clang++', 'g++', 'c++') if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='retained-adapter-', dir=ROOT / '.tmp') as temp:
        test_source = Path(temp) / 'adapter.cpp'
        binary = Path(temp) / 'adapter.exe'
        test_source.write_text(code, encoding='utf-8')
        subprocess.run([compiler, '-std=c++20', '-DUSE_SDL3', '-I', str(ROOT), str(test_source),
                        str(ROOT / 'src/ui/retained/Input.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
