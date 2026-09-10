#!/usr/bin/env python3
"""Exercise production GUI-manager ownership without renderer, input or a game.

The managed declaration and manager method bodies are compiled unchanged against
counted GUI/resource stand-ins. This covers lifecycle and cache behavior only;
the deferred adapter and path factory are also compiled unchanged. Backend
stand-ins do not establish retained document rendering or demo replay.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <functional>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
using ID_TIME_T = long long;
struct idStr : std::string {
    using std::string::string;
    using std::string::operator=;
    operator const char*() const { return c_str(); }
    int Length() const { return static_cast<int>(size()); }
    bool IsEmpty() const { return empty(); }
    static int Cmp(const char* a,const char* b) { return std::string(a).compare(b); }
    void StripLeading(char value) { erase(0,find_first_not_of(value)); }
    static int Icmpn(const char* a,const char* b,int length) { return Icmp(std::string(a,length).c_str(),std::string(b,length).c_str()); }
    static int Length(const char* value) { return static_cast<int>(std::char_traits<char>::length(value)); }
    static bool CheckExtension(const char* name,const char* extension) {
        const size_t n=std::char_traits<char>::length(name), e=std::char_traits<char>::length(extension);
        return n>=e && Icmp(name+n-e,extension)==0;
    }
    static int Icmp(const char* a, const char* b) {
        while (*a && *b) {
            const int delta = std::tolower(static_cast<unsigned char>(*a++)) -
                              std::tolower(static_cast<unsigned char>(*b++));
            if (delta) return delta;
        }
        return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
    }
};
template<class T> struct idList {
    std::vector<T> values;
    int Num() const { return static_cast<int>(values.size()); }
    T& operator[](int i) { return values.at(i); }
    const T& operator[](int i) const { return values.at(i); }
    void Append(const T& value) { values.push_back(value); }
    T* Find(const T& value) {
        auto it=std::find(values.begin(),values.end(),value);
        return it==values.end() ? nullptr : &*it;
    }
    void AddUnique(const T& value) { if (!Find(value)) Append(value); }
    void Remove(const T& value) {
        auto it=std::find(values.begin(),values.end(),value);
        if(it!=values.end()) values.erase(it);
    }
};
struct idVec4 { void Zero() {} } vec4_origin;
struct idRectangle {};
struct sysEvent_t { int evType=0,evValue=0,evValue2=0; };
struct idMath { static float ClampFloat(float low,float high,float value) { return std::clamp(value,low,high); } };
struct wrapInfo_t {};
struct idKeyValue {
    idStr key,value;
    const idStr& GetKey() const { return key; }
    const idStr& GetValue() const { return value; }
};
struct idDict {
    std::vector<idKeyValue> items;
    void Set(const char* key,const char* value) {
        for(auto& item:items) if(!idStr::Icmp(item.key,key)) { item.value=value; return; }
        items.push_back({key,value});
    }
    const char* GetString(const char* key,const char* fallback="") const {
        for(const auto& item:items) if(!idStr::Icmp(item.key,key)) return item.value;
        return fallback;
    }
    void SetBool(const char* key,bool value) { Set(key,value?"1":"0"); }
    void SetInt(const char* key,int value) { Set(key,std::to_string(value).c_str()); }
    void SetFloat(const char* key,float value) { Set(key,std::to_string(value).c_str()); }
    void SetVec4(const char* key,const idVec4&) { Set(key,"1 2 3 4"); }
    bool GetBool(const char* key,const char* fallback="0") const { return std::atoi(GetString(key,fallback))!=0; }
    int GetInt(const char* key,const char* fallback="0") const { return std::atoi(GetString(key,fallback)); }
    float GetFloat(const char* key,const char* fallback="0") const { return std::strtof(GetString(key,fallback),nullptr); }
    int GetNumKeyVals() const { return static_cast<int>(items.size()); }
    const idKeyValue* GetKeyVal(int index) const { return &items.at(index); }
    const idKeyValue* FindKey(const char* key) const {
        for(const auto& item:items) if(!idStr::Icmp(item.key,key)) return &item;
        return nullptr;
    }
    void Clear() { items.clear(); }
    void Delete(const char* name) { items.erase(std::remove_if(items.begin(),items.end(),[&](auto& item){return !idStr::Icmp(item.key,name);}),items.end()); }
    size_t Allocated() const { return items.size()*sizeof(idKeyValue); }
};
struct idFile {
    idDict state; int kind=0,writes=0,reads=0;
    bool active=false,interactive=false,unique=false,interactiveOverride=false;
};
class idDemoFile;
'''

MANAGER = r'''
class idUserInterfaceManagerLocal {
public:
    void Shutdown();
    void Touch(const char*);
    void BeginLevelLoad();
    void EndLevelLoad();
    void Reload(bool);
    void ListGuis() const;
    idUserInterface* Alloc() const;
    void DeAlloc(idUserInterface*);
    idUserInterface* FindGui(const char*,bool=false,bool=false,bool=false);
    idUserInterface* FindDemoGui(const char*);
    void RegisterAllocation(idUserInterfaceManaged*);
    void RegisterGui(idUserInterfaceManaged*);
    void RegisterDemoGui(idUserInterfaceManaged*);
    void UnregisterGui(idUserInterfaceManaged*);
    void UpdateAlwaysThinkGui(idUserInterfaceManaged*);
    void RemoveAlwaysThinkGui(idUserInterfaceManaged*);
    void RunAlwaysThinkGUIs(int);
    bool DispatchApplicationActions(idUserInterface*,const char*,bool&);
    void PumpApplicationActions(UI_ApplicationCommandCallback,void*,idUserInterface*);
    idList<idUserInterfaceManaged*> allocations,guis,alwaysThinkGUIs,demoGuis;
    unsigned long long nextAllocationId=0;
    int applicationPumpDepth=0,applicationPumpBudget=0;
    struct Context { void SizeIcons() {} void Shutdown() {} } dc;
} uiManagerLocal;
static int destroyed=0,retainedShutdowns=0;
static bool recycleAllocation=false;
static void* recycledAllocation=nullptr;
static std::vector<std::string> applicationActions;
class idUserInterfaceLocal : public idUserInterfaceManaged {
public:
    explicit idUserInterfaceLocal(bool managed=true) : idUserInterfaceManaged(managed) {}
    idStr source;
    idDict state;
    ID_TIME_T timestamp=1;
    bool menu=false,think=false,interactive=false,unique=false,active=false;
    float x=0,y=0;
    virtual int Kind() const { return 1; }
    int ticks=0,loads=0,lastTime=-1;
    std::function<void()> onThink,onLoad;
    std::function<void(bool)> onActivate;
    ~idUserInterfaceLocal() override { ++destroyed; }
    static void* operator new(size_t bytes) {
        if(recycledAllocation) {
            void* result=recycledAllocation; recycledAllocation=nullptr; return result;
        }
        return ::operator new(bytes);
    }
    static void operator delete(void* memory) {
        if(recycleAllocation && !recycledAllocation) recycledAllocation=memory;
        else ::operator delete(memory);
    }
    const char* Name() const override { return source.c_str(); }
    const char* Comment() const override { return Kind()==1?"legacy":"retained"; }
    const char* GetSourceFile() const override { return source.c_str(); }
    ID_TIME_T GetTimeStamp() const override { return timestamp; }
    bool Active() const override { return active; }
    bool IsMenuGui() const override { return menu; }
    bool AlwaysThink() const override { return think; }
    bool IsInteractive() const override { return interactive; }
    void SetInteractive(bool value) override { interactive=value; }
    bool IsUniqued() const override { return unique; }
    void SetUniqued(bool value) override { unique=value; }
    const idDict& State() const override { return state; }
    void DeleteStateVar(const char* name) override { state.Delete(name); }
    void SetStateString(const char* name,const char* value) override { state.Set(name,value); }
    void SetStateBool(const char* name,bool value) override { state.SetBool(name,value); }
    void SetStateInt(const char* name,int value) override { state.SetInt(name,value); }
    void SetStateFloat(const char* name,float value) override { state.SetFloat(name,value); }
    const char* GetStateString(const char* name,const char* fallback="") const override { return state.GetString(name,fallback); }
    bool GetStateBool(const char* name,const char* fallback="0") const override { return state.GetBool(name,fallback); }
    int GetStateInt(const char* name,const char* fallback="0") const override { return state.GetInt(name,fallback); }
    float GetStateFloat(const char* name,const char* fallback="0") const override { return state.GetFloat(name,fallback); }
    bool GetPresentationValue(const char*,idStr& value) const override { value="presentation"; return true; }
    bool SetPresentationValue(const char*,const char*,bool) override { return true; }
    bool GetTextInputState(idRectangle&,float& offset) const override { offset=5; return true; }
    void StateChanged(int time,bool) override { lastTime=time; }
    const char* Activate(bool value,int time) override { active=value; lastTime=time; if(onActivate)onActivate(value); return "activate"; }
    void Trigger(int time) override { lastTime=time; }
    const char* HandleEvent(const sysEvent_t*,int time,bool* visuals) override { lastTime=time; if(visuals)*visuals=true; return "event"; }
    void HandleNamedEvent(const char* name) override { ChangeThinking(!idStr::Icmp(name,"think")); }
    void Redraw(int time,bool) override { lastTime=time; }
    void DrawCursor() override {}
    bool WriteToSaveGame(idFile* file) const override {
        if(!file)return false; file->kind=Kind(); file->state=state;
        file->active=active; file->interactive=interactive; file->unique=unique;
        ++file->writes; return true;
    }
    bool ReadFromSaveGame(idFile* file) override {
        if(!file || file->kind!=Kind())return false; state=file->state;
        active=file->active; interactive=file->interactive; unique=file->unique;
        ++file->reads; return true;
    }
    void SetKeyBindingNames() override { state.Set("binding","bound"); }
    void SetCursor(float px,float py) override { x=px; y=py; }
    float CursorX() override { return x; }
    float CursorY() override { return y; }
    bool GetMaxTextIndex(const char*,const char*,wrapInfo_t&) const override { return true; }
    size_t Size() override { return sizeof(*this); }
    int NumTransitions() override { return 3; }
    void RunTimeEvents(int time) override {
        ++ticks; lastTime=time; auto callback=onThink; if(callback) callback();
    }
    bool InitFromFile(const char* path,bool=true,bool=true) override {
        if(!path || !*path || !idStr::Icmp(path,"missing.gui") || !idStr::Icmp(path,"missing.q4ui")) return false;
        // A production reload must not pass a pointer owned by this instance.
        assert(path!=source.c_str());
        source=path; state.Set("name",path); state.Set("text","parser default");
        ++loads; RegisterLoaded();
        auto callback=onLoad; if(callback) callback();
        return true;
    }
    void PublishDemo() { RegisterDemo(); }
    void ChangeThinking(bool value) { think=value; RefreshThinking(); }
};
class idUserInterfaceRetained : public idUserInterfaceLocal {
public:
    std::vector<std::string> pendingActions;
    std::function<void()> onDispatch;
    bool interactiveOverride=false;
    explicit idUserInterfaceRetained(bool managed=true) : idUserInterfaceLocal(managed) { interactive=true; }
    int Kind() const override { return 2; }
    bool HasInteractiveOverride() const override { return interactiveOverride; }
    void SetInteractive(bool value) override { interactiveOverride=true; interactive=value; }
    void StateChanged(int time,bool redraw) override {
        idUserInterfaceLocal::StateChanged(time,redraw);
        if(!interactiveOverride)interactive=!state.GetBool("noninteractive");
    }
    bool WriteToSaveGame(idFile* file) const override {
        if(!idUserInterfaceLocal::WriteToSaveGame(file))return false;
        file->interactiveOverride=interactiveOverride; return true;
    }
    bool ReadFromSaveGame(idFile* file) override {
        if(!idUserInterfaceLocal::ReadFromSaveGame(file))return false;
        interactiveOverride=file->interactiveOverride; return true;
    }
    bool DispatchApplicationActions(const char* command,bool& close) override {
        close=command && !idStr::Icmp(command,"retained-back");
        if(command && !idStr::Cmp(command,"retained-pending")) {
            auto actions=std::move(pendingActions); pendingActions.clear();
            for(const auto& action:actions) {
                if(action=="dismiss")close=true;
                else applicationActions.push_back(action);
            }
            auto callback=onDispatch; if(callback)callback();
        }
        return true;
    }
    const char* PendingApplicationCommand() const override { return pendingActions.empty()?"":"retained-pending"; }
    void HandleNamedEvent(const char* name) override {
        if(!idStr::Cmp(name,"queue"))pendingActions.push_back(state.GetString("queuedAction","named"));
        else idUserInterfaceLocal::HandleNamedEvent(name);
    }
};
struct Commands { void RemoveCommand(const char*) {} } commands;
Commands* cmdSystem=&commands;
struct idChatWindow { static void Reset() {} };
void RetainedUI_Shutdown() {
    assert(uiManagerLocal.allocations.Num()==0);
    ++retainedShutdowns;
}
struct Common {
    void FatalError(const char*) { throw std::runtime_error("GUI identity exhausted"); }
    void Printf(const char*,...) {}
    void DPrintf(const char*,...) {}
    int GetPresentationTime() const { return 1234; }
    void Error(const char*,...) { throw std::runtime_error("missing save GUI"); }
} commonObject;
Common* common=&commonObject;
struct Session { bool saving=false; bool IsLoadingSaveGame() const {return saving;} } sessionObject;
Session* session=&sessionObject;
struct FileSystem {
    ID_TIME_T timestamp=1;
    int ReadFile(const char*,void*,ID_TIME_T* stamp) { *stamp=timestamp; return 0; }
} files;
FileSystem* fileSystem=&files;
struct idMaterial {
    idUserInterface* gui=nullptr;
    idUserInterface* GlobalGui() const { return gui; }
};
constexpr int DECL_MATERIAL=0;
struct DeclManager {
    std::vector<idMaterial> materials;
    int GetNumDecls(int) const { return static_cast<int>(materials.size()); }
    const idMaterial* DeclByIndex(int,int index,bool) const { return &materials.at(index); }
} declarations;
DeclManager* declManager=&declarations;
'''

MAIN = r'''
static void Empty() {
    assert(uiManagerLocal.allocations.Num()==0 && uiManagerLocal.guis.Num()==0 &&
           uiManagerLocal.alwaysThinkGUIs.Num()==0 && uiManagerLocal.demoGuis.Num()==0);
}
static idUserInterfaceLocal* Load(const char* name) {
    return static_cast<idUserInterfaceLocal*>(uiManagerLocal.FindGui(name,true));
}
static void RestoredReplacement() {
    auto& manager=uiManagerLocal;
    auto* legacy=static_cast<idUserInterfaceManaged*>(manager.Alloc());
    legacy->SetInteractive(true); legacy->Activate(true,10);
    assert(legacy->InitFromFile("restore-legacy-a.gui"));
    // The pre-init override applies to the first parsed legacy desktop.
    assert(legacy->IsInteractive() && legacy->Active() && !legacy->HasInteractiveOverride());
    idFile legacySave; assert(legacy->WriteToSaveGame(&legacySave));
    legacy->SetInteractive(false); legacy->Activate(false,20);
    assert(legacy->ReadFromSaveGame(&legacySave));
    assert(legacy->IsInteractive() && legacy->Active());
    assert(legacy->InitFromFile("restore-legacy-b.gui"));
    // A legacy file has no persistent override provenance: replacement uses
    // the next desktop's parsed interactivity, preserving restored activation.
    assert(!legacy->IsInteractive() && legacy->Active() && !legacy->HasInteractiveOverride());
    delete legacy; Empty();

#ifndef ID_DEDICATED
    auto* explicitView=static_cast<idUserInterfaceManaged*>(manager.Alloc());
    assert(explicitView->InitFromFile("restore-explicit-a.q4ui"));
    explicitView->SetInteractive(false); explicitView->Activate(true,10);
    idFile explicitSave; assert(explicitView->WriteToSaveGame(&explicitSave));
    explicitView->SetInteractive(true); explicitView->Activate(false,20);
    assert(explicitView->ReadFromSaveGame(&explicitSave));
    assert(explicitView->Active() && !explicitView->IsInteractive() && explicitView->HasInteractiveOverride());
    assert(explicitView->InitFromFile("restore-explicit-b.q4ui"));
    explicitView->StateChanged(30,false);
    assert(explicitView->Active() && !explicitView->IsInteractive() && explicitView->HasInteractiveOverride());
    // The same restored policy continues through another replacement.
    assert(explicitView->InitFromFile("restore-explicit-c.q4ui"));
    assert(explicitView->Active() && !explicitView->IsInteractive() && explicitView->HasInteractiveOverride());
    delete explicitView; Empty();

    auto* defaultView=static_cast<idUserInterfaceManaged*>(manager.Alloc());
    assert(defaultView->InitFromFile("restore-default-a.q4ui"));
    idFile defaultSave; assert(defaultView->WriteToSaveGame(&defaultSave));
    defaultView->SetInteractive(false); defaultView->Activate(true,20);
    assert(defaultView->ReadFromSaveGame(&defaultSave));
    assert(!defaultView->Active() && defaultView->IsInteractive() && !defaultView->HasInteractiveOverride());
    assert(defaultView->InitFromFile("restore-default-b.q4ui"));
    assert(!defaultView->Active() && defaultView->IsInteractive() && !defaultView->HasInteractiveOverride());
    defaultView->SetStateBool("noninteractive",true); defaultView->StateChanged(30,false);
    assert(!defaultView->IsInteractive());
    defaultView->SetStateBool("noninteractive",false); defaultView->StateChanged(40,false);
    assert(defaultView->IsInteractive() && !defaultView->HasInteractiveOverride());
    delete defaultView; Empty();
#endif
}
int main() {
    auto& manager=uiManagerLocal;
    assert(UI_IsRetainedPath("menu.q4ui") && UI_IsRetainedPath("MENU.Q4UI"));
    assert(!UI_IsRetainedPath(nullptr) && !UI_IsRetainedPath("") && !UI_IsRetainedPath("ui"));
    assert(!UI_IsRetainedPath("menuXq4ui") && !UI_IsRetainedPath("menu.x4ui") && !UI_IsRetainedPath("menu.q4ui.guied"));
    RestoredReplacement();
    // An owned child never appears in any manager registry, even when it
    // publishes its parsed desktop, demo metadata and changing thinker flag.
    auto child=new idUserInterfaceLocal(false);
    assert(child->InitFromFile("child.gui"));
    child->PublishDemo(); child->ChangeThinking(true); Empty();
    delete child; Empty();

    auto deferred=manager.Alloc();
    assert(dynamic_cast<idUserInterfaceDeferred*>(deferred)!=nullptr);
    deferred->SetStateString("text","application text");
    deferred->SetStateInt("score",42); deferred->SetStateBool("enabled",true);
    deferred->SetStateFloat("opacity",0.5f); deferred->SetInteractive(true);
    deferred->SetUniqued(true); deferred->SetCursor(31,47);
    assert(deferred->CursorX()==31 && deferred->CursorY()==47);
    idFile save;
    assert(!deferred->ReadFromSaveGame(&save) && !deferred->WriteToSaveGame(&save));
    assert(!deferred->InitFromFile("missing.gui") && manager.allocations.Num()==1 && manager.guis.Num()==0);
    assert(deferred->GetStateInt("score")==42 && deferred->IsUniqued() && deferred->IsInteractive());
    assert(deferred->InitFromFile("deferred.gui"));
    assert(manager.allocations.Num()==1 && manager.guis.Num()==1 && manager.guis[0]==deferred);
    assert(deferred->GetStateInt("score")==42 && deferred->GetStateBool("enabled"));
    assert(deferred->GetStateFloat("opacity")==0.5f && !idStr::Icmp(deferred->GetStateString("text"),"application text"));
    assert(deferred->IsUniqued() && deferred->IsInteractive() && deferred->CursorX()==31 && deferred->CursorY()==47);
    assert(manager.FindGui("deferred.gui",true,false,true)==deferred);
    assert(manager.guis[0]->GetRefs()==2);
    assert(!deferred->InitFromFile("missing.gui") && !idStr::Icmp(deferred->Name(),"deferred.gui"));
    assert(deferred->GetStateInt("score")==42 && manager.allocations.Num()==1);
    // Source may alias the backend name; the wrapper owns the copy on reload.
    assert(deferred->InitFromFile(deferred->Name(),false));
    assert(deferred->IsUniqued() && deferred->CursorX()==31 && manager.guis[0]->GetRefs()==2);
    deferred->HandleNamedEvent("think");
    assert(manager.alwaysThinkGUIs.Num()==1 && manager.alwaysThinkGUIs[0]==deferred);
    manager.RunAlwaysThinkGUIs(70);
    deferred->HandleNamedEvent("stop");
    assert(manager.alwaysThinkGUIs.Num()==0);
    assert(deferred->WriteToSaveGame(&save) && save.kind==1 && save.writes==1);
    deferred->SetStateInt("score",0);
    assert(deferred->ReadFromSaveGame(&save) && save.reads==1 && deferred->GetStateInt("score")==42);
    bool close=true;
    assert(!UI_DispatchApplicationActions(deferred,"legacy",close) && !close);
    idUserInterfaceLocal unknown(false);
    assert(!UI_DispatchApplicationActions(&unknown,"legacy",close) && !close);

#ifndef ID_DEDICATED
    // A different explicit path switches the unregistered implementation,
    // preserving the wrapper identity, refs, state, cursor and save contract.
    assert(deferred->InitFromFile("deferred.Q4UI"));
    assert(!idStr::Icmp(deferred->Comment(),"retained") && manager.allocations.Num()==1 && manager.guis.Num()==1);
    assert(manager.guis[0]->GetRefs()==2 && deferred->GetStateInt("score")==42);
    assert(!idStr::Icmp(deferred->GetStateString("name"),"deferred.Q4UI"));
    assert(!deferred->InitFromFile("missing.q4ui") && !idStr::Icmp(deferred->Name(),"deferred.Q4UI"));
    assert(deferred->WriteToSaveGame(&save) && save.kind==2 && save.writes==2);
    assert(UI_DispatchApplicationActions(deferred,"retained-back",close) && close);
    assert(UI_DispatchApplicationActions(deferred,"untrusted text",close) && !close);
    // Only the wrapper is registered; its unregistered child still exposes
    // pending requests through the private managed virtual.
    assert(std::string(static_cast<idUserInterfaceManaged*>(deferred)->PendingApplicationCommand()).empty());
    auto retained=manager.FindGui("direct.q4ui",true);
    assert(dynamic_cast<idUserInterfaceRetained*>(retained)!=nullptr);
    assert(manager.FindGui("DIRECT.Q4UI",false,false,true)==retained);
    delete retained;
#else
    assert(manager.FindGui("direct.q4ui",true)==nullptr);
    assert(!deferred->InitFromFile("deferred.q4ui"));
    assert(!idStr::Icmp(deferred->Name(),"deferred.gui") && deferred->GetStateInt("score")==42);
#endif
    // The wrapper remains the material owner; its child must not be purged.
    declarations.materials.push_back({deferred});
    deferred->SetInteractive(false);
    manager.BeginLevelLoad(); manager.EndLevelLoad();
    assert(manager.allocations.Num()==1 && manager.guis[0]==deferred);
    declarations.materials.clear();
    delete deferred; Empty();

    // Allocations are owned before loading and remain safe through level scans.
    auto blank=manager.Alloc();
    assert(manager.allocations.Num()==1 && manager.guis.Num()==0);
    manager.BeginLevelLoad(); manager.EndLevelLoad(); manager.ListGuis();
    assert(manager.allocations.Num()==1);
    manager.DeAlloc(blank); Empty();
    const int afterBlank=destroyed;
    manager.DeAlloc(blank); Empty();
    assert(destroyed==afterBlank);
    manager.DeAlloc(nullptr);
    assert(manager.FindGui(nullptr,true)==nullptr && manager.FindGui("",true)==nullptr);
    assert(manager.FindDemoGui(nullptr)==nullptr && manager.FindDemoGui("")==nullptr);
    const int beforeFailure=destroyed;
    manager.Touch("missing.gui");
    assert(manager.FindGui("missing.gui",true)==nullptr);
    sessionObject.saving=true;
    bool rejected=false;
    try { manager.FindGui("missing.gui",true); } catch(const std::runtime_error&) { rejected=true; }
    sessionObject.saving=false;
    assert(rejected && destroyed==beforeFailure+3); Empty();

    // Direct legacy/editor deletion removes all non-owning registries once.
    auto editor=new idUserInterfaceLocal;
    editor->think=true;
    assert(editor->InitFromFile("guis/temp.guied"));
    editor->PublishDemo(); editor->PublishDemo();
    assert(manager.guis.Num()==1 && manager.demoGuis.Num()==1 && manager.alwaysThinkGUIs.Num()==1);
    assert(manager.FindDemoGui("GUIS/TEMP.GUIED")==editor);
    delete editor; Empty();
    auto reopened=manager.FindGui("guis/temp.guied",true,true);
    assert(dynamic_cast<idUserInterfaceLocal*>(reopened)!=nullptr);
    delete reopened; Empty();

    // Shared, interactive and explicitly unique caches preserve retail rules.
    auto shared=Load("shared.gui");
    assert(shared->GetRefs()==1 && manager.FindGui("SHARED.GUI")==shared && shared->GetRefs()==2);
    shared->interactive=true;
    auto interactiveCopy=Load("shared.gui");
    assert(interactiveCopy!=shared);
    assert(manager.FindGui("shared.gui",true,false,true)==shared);
    auto unique=static_cast<idUserInterfaceLocal*>(manager.FindGui("unique.gui",true,true));
    assert(unique->IsUniqued() && !unique->IsInteractive());
    assert(manager.FindGui("unique.gui",true)!=unique);
    manager.Shutdown(); Empty();

    // Menu refs persist; touched world views and material-held views survive.
    auto menu=Load("menu.gui"); menu->menu=true;
    auto orphan=Load("orphan.gui"); orphan->ChangeThinking(true);
    auto referenced=Load("referenced.gui");
    auto material=Load("material.gui");
    declarations.materials.push_back({material});
    manager.BeginLevelLoad();
    assert(menu->GetRefs()==1 && material->GetRefs()==0 && referenced->GetRefs()==0);
    assert(manager.FindGui("referenced.gui")==referenced);
    const int beforePurge=destroyed;
    manager.EndLevelLoad();
    assert(destroyed==beforePurge+1 && manager.FindGui("orphan.gui")==nullptr);
    assert(manager.alwaysThinkGUIs.Num()==0 && manager.guis.Num()==3);
    declarations.materials.clear(); manager.Shutdown(); Empty();

    // A thinker can delete a later view, including reusing its exact address.
    auto first=Load("first.gui"); first->ChangeThinking(true);
    auto victim=Load("victim.gui"); victim->ChangeThinking(true);
    idUserInterfaceLocal* replacement=nullptr;
    first->onThink=[&] {
        if(replacement) return;
        recycleAllocation=true;
        delete victim;
        replacement=new idUserInterfaceLocal;
        assert(replacement==victim); // Deliberately exercise address reuse.
        recycleAllocation=false;
        replacement->think=true;
        replacement->InitFromFile("replacement.gui");
    };
    manager.RunAlwaysThinkGUIs(10);
    assert(first->ticks==1 && replacement && replacement->ticks==0);
    manager.RunAlwaysThinkGUIs(20);
    assert(first->ticks==2 && replacement->ticks==1 && replacement->lastTime==20);
    first->ChangeThinking(false);
    manager.RunAlwaysThinkGUIs(30);
    assert(first->ticks==2 && replacement->ticks==2);
    auto self=Load("self-delete.gui"); self->ChangeThinking(true);
    bool selfCalled=false;
    self->onThink=[&] { selfCalled=true; delete self; };
    const int beforeSelf=destroyed;
    manager.RunAlwaysThinkGUIs(40);
    assert(selfCalled && destroyed==beforeSelf+1 && replacement->ticks==3);
    assert(manager.FindGui("self-delete.gui")==nullptr);
    manager.Shutdown(); Empty();

    // Reload retains path ownership, skips unchanged files and tolerates a
    // callback removing another entry without reloading its replacement.
    first=Load("reload-first.gui"); victim=Load("reload-victim.gui"); replacement=nullptr;
    files.timestamp=1; manager.Reload(false);
    assert(first->loads==1 && victim->loads==1);
    first->onLoad=[&] {
        if(replacement) return;
        recycleAllocation=true; delete victim;
        replacement=new idUserInterfaceLocal;
        assert(replacement==victim); recycleAllocation=false;
        replacement->InitFromFile("reload-replacement.gui");
    };
    files.timestamp=2; manager.Reload(false);
    assert(first->loads==2 && replacement && replacement->loads==1);
    manager.Reload(true);
    assert(first->loads==3 && replacement->loads==2);
    manager.Shutdown(); Empty();

    // One object can occur in loaded/demo subsets; a blank demo allocation is
    // still owned. Shutdown destroys both exactly once and is repeatable.
    auto dual=Load("dual.gui"); dual->PublishDemo(); dual->ChangeThinking(true);
    manager.Alloc();
    const int beforeShutdown=destroyed;
    manager.Shutdown();
    assert(destroyed==beforeShutdown+1); Empty(); // blank deferred has no child
    manager.Shutdown(); Empty();
    assert(recycledAllocation==nullptr && retainedShutdowns==6);
    std::puts("GUI manager lifecycle: deferred routing/state/save/actions, ownership, direct deletion, refs, uniqueness, material retention, reload and callback address reuse passed");
}
'''


def production_source():
    source = (ROOT / 'src/ui/UserInterface.cpp').read_text(encoding='utf-8')
    header = (ROOT / 'src/ui/UserInterfaceManaged.h').read_text(encoding='utf-8')
    public_header = (ROOT / 'src/ui/UserInterface.h').read_text(encoding='utf-8')
    deferred_header = (ROOT / 'src/ui/UserInterfaceDeferred.h').read_text(encoding='utf-8')
    deferred_source = (ROOT / 'src/ui/UserInterfaceDeferred.cpp').read_text(encoding='utf-8')
    public = function_body(public_header, 'class idUserInterface {') + ';\n'
    managed = function_body(header, 'class idUserInterfaceManaged :') + ';\n'
    declarations = header[header.index('idUserInterfaceManaged *UI_CreateForPath'):]
    deferred = function_body(deferred_header, 'class idUserInterfaceDeferred final :') + ';\n'
    signatures = (
        'idUserInterfaceManaged::idUserInterfaceManaged(',
        'idUserInterfaceManaged::~idUserInterfaceManaged()',
        'void idUserInterfaceManaged::RegisterLoaded()',
        'void idUserInterfaceManaged::RegisterDemo()',
        'void idUserInterfaceManaged::RefreshThinking()',
        'idUserInterfaceManaged *UI_CreateForPath(',
        'bool UI_IsRetainedPath(',
        'bool UI_DispatchApplicationActions(',
        'bool idUserInterfaceManagerLocal::DispatchApplicationActions(',
        'void UI_PumpApplicationActions(',
        'void idUserInterfaceManagerLocal::PumpApplicationActions(',
        'void idUserInterfaceManagerLocal::Shutdown()',
        'void idUserInterfaceManagerLocal::Touch(',
        'void idUserInterfaceManagerLocal::BeginLevelLoad()',
        'void idUserInterfaceManagerLocal::EndLevelLoad()',
        'void idUserInterfaceManagerLocal::Reload(',
        'void idUserInterfaceManagerLocal::ListGuis()',
        'idUserInterface *idUserInterfaceManagerLocal::Alloc(',
        'void idUserInterfaceManagerLocal::DeAlloc(',
        'idUserInterface *idUserInterfaceManagerLocal::FindGui(',
        'idUserInterface *idUserInterfaceManagerLocal::FindDemoGui(',
        'void idUserInterfaceManagerLocal::RegisterAllocation(',
        'void idUserInterfaceManagerLocal::RegisterGui(',
        'void idUserInterfaceManagerLocal::RegisterDemoGui(',
        'void idUserInterfaceManagerLocal::UnregisterGui(',
        'void idUserInterfaceManagerLocal::UpdateAlwaysThinkGui(',
        'void idUserInterfaceManagerLocal::RemoveAlwaysThinkGui(',
        'void idUserInterfaceManagerLocal::RunAlwaysThinkGUIs(',
    )
    return ('#include "src/ui/UserInterfaceText.h"\n#include <limits>\n' + SUPPORT + public + managed + declarations + deferred + MANAGER +
            '\n'.join(function_body(source, signature) for signature in signatures) +
            deferred_source[deferred_source.index('idUserInterfaceDeferred::idUserInterfaceDeferred()'):])


def main():
    code = production_source() + MAIN
    compiler = next((found for name in ('clang++', 'g++', 'c++') if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-manager-', dir=ROOT / '.tmp') as temp:
        test_source = Path(temp) / 'manager.cpp'
        test_source.write_text(code, encoding='utf-8')
        for dedicated in (False, True):
            binary = Path(temp) / ('dedicated.exe' if dedicated else 'client.exe')
            defines = ['-DID_DEDICATED'] if dedicated else []
            subprocess.run([compiler, '-std=c++20', '-I', str(ROOT), *defines, str(test_source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
