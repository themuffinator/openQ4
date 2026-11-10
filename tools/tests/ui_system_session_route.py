#!/usr/bin/env python3
"""Compile real Session SYSTEM routing against counted engine/UI boundaries.

The route, SetGUI/ExitMenu, sound classification, GUI frame pump and diagnostic
command bodies are production code. Document/service eligibility and GUI action
delivery are boundary doubles; renderer output, physical input, and complete
settings-page functionality are not qualified by this test.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cctype>
namespace openq4 { void NativeInputBeforeSessionChange() noexcept {} }

struct idStr : std::string {
    using std::string::string;
    operator const char*() const {return c_str();}
    int Length() const {return static_cast<int>(size());}
    static int Icmp(const char* a,const char* b) {
        while(*a && *b && std::tolower(*a)==std::tolower(*b)){++a;++b;}
        return std::tolower(*a)-std::tolower(*b);
    }
    static int Icmpn(const char* a,const char* b,int n) {
        for(int i=0;i<n;++i,++a,++b){if(std::tolower(*a)!=std::tolower(*b)||!*a)return std::tolower(*a)-std::tolower(*b);}
        return 0;
    }
};
// The multiplayer card names the keys bound to its Vote page's ballots.
struct idKeyInput {
    static const char* KeysFromBinding(const char*) {return "";}
    static const char* KeyNumToString(int,bool) {return "";}
};
struct idCmdArgs {
    std::vector<std::string> values;
    int Argc() const{return static_cast<int>(values.size());}
    const char* Argv(int index) const{return values.at(index).c_str();}
};
static const int MAX_ASYNC_CLIENTS = 32;
static std::vector<std::string> events;
static int checks=0;
#define CHECK(x) do {++checks; if(!(x)){std::fprintf(stderr,"check failed line %d: %s\n",__LINE__,#x);std::abort();}} while(false)
struct Common {
    std::string output;
    int GetPresentationTime() const{return 100;}
    void Printf(const char* fmt,...) {
        char text[2048];va_list args;va_start(args,fmt);std::vsnprintf(text,sizeof(text),fmt,args);va_end(args);output+=text;
    }
    void DPrintf(const char*,...) {}
    std::vector<std::string> warnings,devWarnings;
    void Warning(const char* fmt,...) {
        char text[2048];va_list args;va_start(args,fmt);std::vsnprintf(text,sizeof(text),fmt,args);va_end(args);warnings.push_back(text);
    }
    void DWarning(const char* fmt,...) {
        char text[2048];va_list args;va_start(args,fmt);std::vsnprintf(text,sizeof(text),fmt,args);va_end(args);devWarnings.push_back(text);
    }
} commonObject,*common=&commonObject;
struct StrList : std::vector<std::string> {
    void Append(const char* value){push_back(value);}
    int FindIndex(const char* value) const{auto found=std::find(begin(),end(),value);return found==end()?-1:static_cast<int>(found-begin());}
};
struct CVar {bool value=false;bool GetBool() const{return value;}} ui_retainedSystem,ui_retained,ui_retainedMultiplayer;
enum {SE_NONE=0,INHIBIT_SESSION=1};
struct sysEvent_t {int evType=SE_NONE;};
using HandleGuiCommand_t=bool (*)(const char*);
typedef enum {RETAINED_SUBPAGE_CAMPAIGN,RETAINED_SUBPAGE_ARENA,RETAINED_SUBPAGE_CREATE_SERVER,RETAINED_SUBPAGE_JOIN_GAME,
    RETAINED_SUBPAGE_SETTINGS_CONTROLS,RETAINED_SUBPAGE_SETTINGS_GAME,RETAINED_SUBPAGE_SETTINGS_AUDIO,RETAINED_SUBPAGE_SETTINGS_SYSTEM} retainedSubpage_t;
struct Sound {
    bool paused=true;
    int unpauses=0;
    bool IsPaused() const{return paused;}
    void UnPause(){paused=false;++unpauses;}
};
struct idUserInterface {
    std::string source;
    bool active=false,retired=false,valid=true,canReturn=true,close=false;
    int activates=0,deactivates=0,drains=0,backs=0,frames=0,stateChanges=0;
    // The state dictionary, and the window variables a GUI script reads,
    // which only StateChanged brings up to date.
    std::map<std::string,bool> stateBools,scriptBools;std::vector<std::string> named;
    void SetStateBool(const char* key,bool value){CHECK(!retired);stateBools[key]=value;}
    bool staleScript=false; // A GUI whose window variables miss the update.
    void StateChanged(int time){CHECK(!retired && time==100);++stateChanges;if(!staleScript)scriptBools=stateBools;}
    std::function<void()> onBack,onActivate,onDeactivate,onDrain;
    explicit idUserInterface(const char* name):source(name){}
    const char* Name() const{CHECK(!retired);return source.c_str();}
    const char* Activate(bool value,int time) {
        CHECK(!retired && time==100);active=value;
        events.push_back(std::string(value?"activate:":"deactivate:")+source);
        if(value){++activates;if(onActivate)onActivate();}
        else{++deactivates;if(onDeactivate)onDeactivate();}
        return "";
    }
    const char* HandleEvent(const sysEvent_t* event,int time) {
        CHECK(!retired && event->evType==SE_NONE && time==100);++frames;return close?"typed":"";
    }
    void HandleNamedEvent(const char* event) {
        CHECK(!retired);
        // The main menu's stock SYSTEM route; every other event is the page's Back.
        if(std::string(event)=="fromMp_toSystemStock"){named.push_back(event);return;}
        CHECK(std::string(event)=="onBack");++backs;if(onBack)onBack();
    }
};
struct Manager {
    std::vector<std::unique_ptr<idUserInterface>> storage;
    int loads=0,frees=0;
    bool missing=false,valid=true;
    std::function<void(idUserInterface*)> onLoad,onFree;
    idUserInterface* Make(const char* path){storage.push_back(std::make_unique<idUserInterface>(path));return storage.back().get();}
    idUserInterface* FindGui(const char* path,bool load,bool unique,bool forceShared) {
        CHECK(std::string(path)=="guis/menu/settings/system.q4ui" && load && unique && !forceShared);++loads;
        events.push_back("load");if(missing)return nullptr;
        auto* gui=Make(path);gui->valid=valid;if(onLoad)onLoad(gui);return gui;
    }
    void DeAlloc(idUserInterface* gui) {
        CHECK(gui && !gui->active && !gui->retired);if(onFree)onFree(gui);
        events.push_back("free:"+gui->source);gui->retired=true;++frees;
    }
} *uiManager=nullptr;
static bool preview=false;
static bool RetainedUI_IsOpen(){return preview;}
static void RetainedUI_Close(){preview=false;}
static void RetainedUI_FrameInput(){CHECK(false);}
static bool UI_RetainedSettingsDocument(idUserInterface* gui){CHECK(gui && !gui->active);return gui->valid;}
static bool UI_IsRetainedPath(const char* qpath){const std::string p=qpath?qpath:"";return p.size()>=5&&p.compare(p.size()-5,5,".q4ui")==0;}
static bool UI_RetainedSettingsCanReturn(idUserInterface* gui){CHECK(gui && !gui->retired);return gui->canReturn;}
static int menuProfileSaveVarsMsec=0,menuProfileMainVarsMsec=0;
static int Sys_Milliseconds(){return 10;}
struct Console {bool Active() const{return false;}} consoleObject,*console=&consoleObject;
struct UserCommands {bool inhibited=false;void InhibitUsercmd(int kind,bool value){CHECK(kind==INHIBIT_SESSION);inhibited=value;}} userCommands,*usercmdGen=&userCommands;
static void ClearMenuControllerRepeatState(){}
struct idSessionLocal {
    idUserInterface *guiActive=nullptr,*guiMainMenu=nullptr,*guiSystem=nullptr,*guiSystemParent=nullptr,*guiTest=nullptr,*guiMsg=nullptr,*guiMsgRestore=nullptr,*guiRestartMenu=nullptr,*guiIntro=nullptr,*guiLoading=nullptr;
    HandleGuiCommand_t guiHandle=nullptr,guiSystemParentHandle=nullptr;
    bool systemGuiTransition=false,systemGuiBackEvent=false,mapSpawned=true,multiplayer=false;
    Sound *sw=nullptr,*menuSoundWorld=nullptr,*requestedSoundWorld=nullptr;
    int mainRefresh=0;
    bool OpenSystemSettings();bool ReturnSystemSettings();void CloseSystemSettings();void ReportSystemSettings();
    bool RetainedSystemAvailable() const;void OpenSystemSettingsRoute(bool fromMultiplayer);
    void SetGUI(idUserInterface*,HandleGuiCommand_t);void ExitMenu();void GuiFrameEvents();
    // idSessionLocal::GuiFrameEvents consumes an Android app suspension first; no desktop harness has one.
    void HandleAppResume(bool) {}
    void SetPlayingSoundWorld();void SetPlayingSoundWorld(Sound* sound){requestedSoundWorld=sound;}
    bool IsMultiplayer(){return multiplayer;}
    // The session's stock fallback (qualified by ui_retained_gate.py): a page that
    // could not load is listed and never loaded again this session.
    StrList retainedStock;
    idUserInterface* FindRetainedGui(const char* path,bool shared,bool home) {
        CHECK(!shared && !home);if(retainedStock.FindIndex(path)>=0)return nullptr;
        auto* gui=uiManager->FindGui(path,true,true,false);if(!gui)retainedStock.Append(path);return gui;
    }
    // Retained home screens are outside this route: they never present in these scenarios.
    idUserInterface* guiRetainedHome=nullptr;int retainedHomeUpdates=0;
    void UpdateRetainedHome(){++retainedHomeUpdates;}void RetainedHomeFrameEvent(){CHECK(guiRetainedHome==nullptr);}
    void UpdateRetainedSubpage(){}void UpdateRetainedPage(){}void UpdateRetainedMultiplayerPage(){}void UpdateRetainedJoinGame(){}void UpdateRetainedNotices(){}void UpdateRetainedSettingsPage(){}void UpdateBrowserTooltip(){}void PrecacheRetainedLevelshots(bool){}idUserInterface* guiRetainedLoad=nullptr,*guiRetainedSave=nullptr;
    // The SETTINGS page, whose DISPLAY the page is a sub-page of, and the change
    // of level its Back climbs out by (qualified by ui_retained_gate.py): each
    // change is recorded, then begun (the leaving page stays until the
    // hand-over), or with presentsAtOnce the SETTINGS page presents at once, as
    // for the stock screen or reduced motion.
    idUserInterface* guiRetainedSettingsPage=nullptr,*retainedSubpageFrom=nullptr;bool presentsAtOnce=false;
    struct Subpage{idUserInterface* gui;bool deeper;retainedSubpage_t kind;};std::vector<Subpage> subpages;
    void BeginRetainedSubpage(idUserInterface* gui,bool deeper,retainedSubpage_t kind) {
        subpages.push_back({gui,deeper,kind});
        if(presentsAtOnce)SetGUI(guiRetainedSettingsPage,nullptr);else retainedSubpageFrom=gui;
    }
    bool RetainedSettingsParent() const{return guiActive!=nullptr&&(guiActive==guiMainMenu||(guiRetainedSettingsPage!=nullptr&&guiActive==guiRetainedSettingsPage));}
    bool RetainedPageActive() const{return guiActive!=nullptr&&(guiActive==guiRetainedLoad||guiActive==guiRetainedSave);}
    // Nor does the multiplayer card, or a loading screen held over a join: these scenarios have no match.
    void UpdateRetainedMultiplayer(){}void RetainedMultiplayerFrameEvent(){}
    idUserInterface* guiLoadingHold=nullptr;void UpdateRetainedLoadingHold(){}
    idUserInterface* guiRetainedMultiplayer=nullptr,*guiRetainedRestartMenu=nullptr;
    void HandleRetainedMultiplayerRequest(idUserInterface*,const char*){CHECK(false);}void HandleGameMenuReturn(const char*){CHECK(false);}
    void HandleRetainedSessionRequest(idUserInterface*,const char*){CHECK(false);}
    void SetSaveGameGuiVars(){}void SetMainMenuGuiVars(bool){++mainRefresh;}
    void PumpApplicationActions(idUserInterface* only=nullptr) {
        std::vector<idUserInterface*> work;
        if(only)work.push_back(only);else for(auto& gui:uiManager->storage)if(!gui->retired && gui->close)work.push_back(gui.get());
        for(auto* gui:work){CHECK(!gui->retired);++gui->drains;events.push_back("drain:"+gui->source);if(gui->onDrain)gui->onDrain();if(!gui->retired && gui->close){gui->close=false;DispatchCommand(gui,"typed");}}
    }
    void DispatchCommand(idUserInterface* gui,const char* command);
    void TestGUI(const char*){CHECK(false);}
} sessLocal;
static void PumpControllerMenuNavigation(idSessionLocal*){}
static void PumpControllerDetailStick(idSessionLocal*){}
static void SyncMainMenuSettingsScrollPages(idUserInterface* gui){CHECK(!gui || !gui->retired);}
static std::vector<std::string> dispatched;
// The main menu's command handler, where a scenario asks for it: the button's
// own request routes again.
static idSessionLocal* reenter=nullptr;static int reentries=0;
static bool UI_DispatchApplicationActions(idUserInterface*,const char* command,bool& close) {
    dispatched.push_back(command);close=std::string(command)=="typed";
    if(reenter && std::string(command)=="openRetainedSystem"){++reentries;reenter->OpenSystemSettingsRoute(false);}
    return true;
}
// The SYSTEM button's stock action reads gui::retainedSystem when it runs,
// through the window variables, so a stale variable asks for the retained page.
static int legacyActions=0;static bool legacyRetained=true;
static bool UI_RunLegacyWindowAction(idUserInterface* gui,const char* window,bool back,idStr& command) {
    CHECK(gui && std::string(window)=="set_b_system" && !back);++legacyActions;
    legacyRetained=gui->scriptBools["retainedSystem"];command.assign(legacyRetained?"openRetainedSystem":"refreshSystemSettings");return true;
}
static bool UI_TakeSessionRequest(idUserInterface*,const char*,idStr&){return false;}
static bool ParentHandler(const char*){return true;}
struct Scenario {
    Manager manager;Sound gameSound,menuSound;
    idSessionLocal& session=sessLocal;
    idUserInterface* parent;
    Scenario(){sessLocal=idSessionLocal{};uiManager=&manager;preview=false;ui_retainedSystem.value=true;ui_retained.value=false;common->output.clear();common->warnings.clear();events.clear();userCommands.inhibited=false;
        common->devWarnings.clear();dispatched.clear();legacyActions=0;legacyRetained=true;reenter=nullptr;reentries=0;
        parent=manager.Make("guis/mainmenu.gui");parent->active=true;
        session.guiActive=session.guiMainMenu=parent;session.guiHandle=ParentHandler;
        session.guiMsg=manager.Make("guis/msg.gui");session.sw=&gameSound;session.menuSoundWorld=&menuSound;session.requestedSoundWorld=&menuSound;
    }
    ~Scenario(){session.CloseSystemSettings();}
};
'''

MAIN = r'''
int main(){
    {
        Scenario s;ui_retainedSystem.value=false;CHECK(!s.session.OpenSystemSettings());CHECK(s.manager.loads==0 && s.parent->deactivates==0);
        // The gate includes the SYSTEM page while it offers every stock setting and each one works there:
        // with both lists empty ui_retained alone opens it, and with a setting in either only the opt-in does.
        const bool complete=RETAINED_SYSTEM_MISSING_SETTINGS[0]==NULL && RETAINED_SYSTEM_INCOMPLETE_SETTINGS[0]==NULL;
        ui_retained.value=true;CHECK(s.session.OpenSystemSettings()==complete && s.manager.loads==(complete?1:0));
        if(complete)CHECK(s.session.ReturnSystemSettings());
        ui_retainedSystem.value=true;CHECK(s.session.OpenSystemSettings() && s.manager.loads==(complete?2:1));CHECK(s.session.ReturnSystemSettings());ui_retained.value=false;
        s.manager.loads=0;s.manager.frees=0;s.parent->deactivates=0;
        ui_retainedSystem.value=true;s.session.guiTest=s.parent;CHECK(!s.session.OpenSystemSettings());s.session.guiTest=nullptr;
        preview=true;CHECK(!s.session.OpenSystemSettings());preview=false;
        s.session.guiActive=nullptr;CHECK(!s.session.OpenSystemSettings());s.session.guiActive=s.parent;
        s.session.guiMsgRestore=s.parent;CHECK(!s.session.OpenSystemSettings());s.session.guiMsgRestore=nullptr;
        s.manager.missing=true;CHECK(!s.session.OpenSystemSettings());CHECK(s.session.guiActive==s.parent && s.parent->deactivates==0);
        // A missing page falls back to the stock one for the session: no reload is attempted.
        s.manager.missing=false;CHECK(s.session.retainedStock.FindIndex(RETAINED_SYSTEM_GUI)==0);
        const int loads=s.manager.loads;CHECK(!s.session.OpenSystemSettings() && s.manager.loads==loads);
        s.session.retainedStock.clear();s.manager.valid=false;CHECK(!s.session.OpenSystemSettings());CHECK(s.manager.frees==1 && s.parent->deactivates==0 && s.session.guiSystem==nullptr);
        // So does a page that is not a SYSTEM settings document, and it is reported.
        CHECK(s.session.retainedStock.FindIndex(RETAINED_SYSTEM_GUI)==0 && common->warnings.size()==1);
        CHECK(!s.session.OpenSystemSettings() && s.manager.frees==1);
    }
    {
        Scenario s;CHECK(s.session.OpenSystemSettings());auto* child=s.session.guiSystem;
        CHECK(child && child->active && !s.parent->active && s.parent->deactivates==1 && s.session.guiTest==nullptr);
        CHECK(s.session.guiSystemParent==s.parent && s.session.guiSystemParentHandle==ParentHandler && s.session.guiHandle==nullptr);
        CHECK(events[0]=="load" && events[1]=="deactivate:guis/mainmenu.gui" && events[2]=="drain:guis/mainmenu.gui");
        CHECK(s.session.OpenSystemSettings() && s.manager.loads==1);
        s.session.GuiFrameEvents();CHECK(userCommands.inhibited && child->frames==2 && s.session.retainedHomeUpdates==1);
        s.session.ReportSystemSettings();CHECK(common->output.find("child=1 guiTest=0 menu=1 map=1 multiplayer=0 menuSound=1 canReturn=1")!=std::string::npos);
        child->onDeactivate=[&]{CHECK(!s.session.guiSystem && !s.session.guiSystemParent && s.session.guiActive!=child);CHECK(!s.session.OpenSystemSettings());};
        s.manager.onFree=[&](auto* gui){CHECK(gui==child && child->deactivates==1 && child->drains==2);};
        const auto refreshes=s.session.mainRefresh;
        CHECK(s.session.ReturnSystemSettings());CHECK(child->retired && s.manager.frees==1 && s.parent->active && s.session.guiHandle==ParentHandler);
        CHECK(s.session.mainRefresh==refreshes); // Returning must preserve the parent's section/selection/catalog state.
        CHECK(s.session.requestedSoundWorld==&s.menuSound && s.gameSound.paused && s.gameSound.unpauses==0);
        CHECK(!s.session.ReturnSystemSettings());s.session.CloseSystemSettings();CHECK(s.manager.frees==1);
        s.session.ExitMenu();CHECK(s.session.guiActive==nullptr && s.gameSound.unpauses==1 && s.session.requestedSoundWorld==&s.gameSound);
        CHECK(s.session.subpages.empty()); // The stock settings screen's page returns at once.
    }
    {
        // From the SETTINGS page's DISPLAY the page is a sub-page: Back goes one
        // level up as the other settings pages' does. The change begins and the
        // page stays until the hand-over, whose SetGUI of the SETTINGS page
        // closes it.
        Scenario s;auto* hub=s.manager.Make("guis/menu/settings.q4ui");s.parent->active=false;hub->active=true;
        s.session.guiActive=s.session.guiRetainedSettingsPage=hub;s.session.guiHandle=nullptr;
        CHECK(s.session.OpenSystemSettings());auto* child=s.session.guiSystem;
        CHECK(child && child->active && !hub->active && s.session.guiSystemParent==hub && s.manager.loads==1 && s.session.subpages.empty());
        CHECK(s.session.ReturnSystemSettings() && s.session.subpages.size()==1);
        CHECK(s.session.subpages[0].gui==child && !s.session.subpages[0].deeper && s.session.subpages[0].kind==RETAINED_SUBPAGE_SETTINGS_SYSTEM);
        CHECK(s.session.retainedSubpageFrom==child && s.session.guiActive==child && child->active && !child->retired && s.manager.frees==0 && !hub->active);
        s.session.retainedSubpageFrom=nullptr;s.session.SetGUI(hub,nullptr);
        CHECK(child->retired && s.manager.frees==1 && hub->active && s.session.guiActive==hub && !s.session.guiSystem && !s.session.guiSystemParent);
        CHECK(!s.session.ReturnSystemSettings() && s.session.subpages.size()==1);
        // Escape from the page climbs the same way.
        CHECK(s.session.OpenSystemSettings());child=s.session.guiSystem;s.session.ExitMenu();
        CHECK(s.session.subpages.size()==2 && s.session.subpages[1].gui==child && s.session.retainedSubpageFrom==child && child->active);
    }
    {
        // Where the SETTINGS page presents at once (the stock screen, or reduced
        // motion), Back's change closes the page there and then.
        Scenario s;auto* hub=s.manager.Make("guis/menu/settings.q4ui");s.parent->active=false;hub->active=true;
        s.session.guiActive=s.session.guiRetainedSettingsPage=hub;s.session.guiHandle=nullptr;s.session.presentsAtOnce=true;
        CHECK(s.session.OpenSystemSettings());auto* child=s.session.guiSystem;
        CHECK(s.session.ReturnSystemSettings() && s.session.subpages.size()==1 && !s.session.retainedSubpageFrom);
        CHECK(child->retired && s.manager.frees==1 && hub->active && s.session.guiActive==hub && !s.session.guiSystem);
    }
    {
        // A draft not applied asks first, from the SETTINGS page as from the
        // stock screen; once settled, the authored dismiss begins the change.
        Scenario s;auto* hub=s.manager.Make("guis/menu/settings.q4ui");s.parent->active=false;hub->active=true;
        s.session.guiActive=s.session.guiRetainedSettingsPage=hub;s.session.guiHandle=nullptr;
        CHECK(s.session.OpenSystemSettings());auto* child=s.session.guiSystem;child->canReturn=false;
        child->onBack=[&]{child->close=true;};
        CHECK(!s.session.ReturnSystemSettings() && child->backs==1 && s.session.subpages.empty() && child->active);
        child->onBack=[&]{child->canReturn=true;child->close=true;};
        s.session.ReturnSystemSettings();
        CHECK(child->backs==2 && s.session.subpages.size()==1 && s.session.retainedSubpageFrom==child && !child->retired && !s.session.systemGuiBackEvent);
    }
    {
        // Opened over the stock settings screen, Back returns there at once,
        // the SETTINGS page loaded or not.
        Scenario s;s.session.guiRetainedSettingsPage=s.manager.Make("guis/menu/settings.q4ui");
        CHECK(s.session.OpenSystemSettings());auto* child=s.session.guiSystem;
        CHECK(s.session.ReturnSystemSettings() && child->retired && s.parent->active && s.session.subpages.empty() && !s.session.retainedSubpageFrom);
    }
    {
        Scenario s;s.session.multiplayer=true;CHECK(s.session.OpenSystemSettings());auto* child=s.session.guiSystem;child->canReturn=false;
        child->onBack=[&]{child->close=true;}; // Wrong/recursive authored dismiss cannot bypass dirty/busy gate.
        CHECK(!s.session.ReturnSystemSettings());CHECK(child->backs==1 && child->active && s.manager.frees==0 && !s.session.systemGuiBackEvent);
        s.session.ExitMenu();CHECK(child->backs==2 && child->active && s.gameSound.unpauses==0);
        s.session.SetPlayingSoundWorld();CHECK(s.session.requestedSoundWorld==&s.menuSound);
        s.session.ReportSystemSettings();CHECK(common->output.find("multiplayer=1 menuSound=1 canReturn=0")!=std::string::npos);
        child->onBack=[&]{child->canReturn=true;child->close=true;}; // Explicit discard then semantic dismiss.
        CHECK(s.session.ReturnSystemSettings());CHECK(child->retired && s.parent->active && child->backs==3);
    }
    {
        Scenario s;CHECK(s.session.OpenSystemSettings());auto* child=s.session.guiSystem;child->canReturn=false;
        auto* replacement=s.manager.Make("guis/other.gui");
        child->onDeactivate=[&]{child->close=true;};
        s.session.SetGUI(replacement,nullptr);CHECK(child->retired && replacement->active && s.manager.frees==1);
        CHECK(!s.session.guiSystem && !s.session.guiSystemParent && !s.session.OpenSystemSettings());
    }
    {
        Scenario s;CHECK(s.session.OpenSystemSettings());auto* child=s.session.guiSystem;
        s.session.guiMsgRestore=child;s.session.guiActive=s.session.guiMsg;
        s.session.SetPlayingSoundWorld();CHECK(s.session.requestedSoundWorld==&s.menuSound);
        CHECK(!s.session.ReturnSystemSettings() && !child->retired);
        {   // A retained front-end screen without a level plays the menu's sounds;
            // a retained screen over a level, such as the death screen, the game's.
            idUserInterface* active=s.session.guiActive;idUserInterface front("guis/menu/singleplayer.q4ui");
            s.session.guiActive=&front;s.session.mapSpawned=false;s.session.SetPlayingSoundWorld();
            CHECK(s.session.requestedSoundWorld==&s.menuSound);
            s.session.mapSpawned=true;s.session.SetPlayingSoundWorld();CHECK(s.session.requestedSoundWorld==&s.gameSound);
            s.session.guiActive=active;
        }
        s.session.ExitMenu();CHECK(s.session.guiActive==s.session.guiMsg && s.session.guiMsgRestore==child && !child->retired && s.gameSound.unpauses==0);
        s.session.CloseSystemSettings();CHECK(child->retired && !s.session.guiMsgRestore && !s.session.guiSystem);
        s.session.CloseSystemSettings();CHECK(s.manager.frees==1);
    }
    {
        Scenario s;Session_SystemSettings_f({{"openq4_system","open"}});CHECK(s.session.guiSystem && s.manager.loads==1);
        auto* child=s.session.guiSystem;Session_SystemSettings_f({{"openq4_system","report"}});CHECK(s.manager.loads==1);
        child->canReturn=false;Session_SystemSettings_f({{"openq4_system","back"}});CHECK(child->active && child->backs==1);
        CHECK(common->output.find("operation=back result=0")!=std::string::npos);
        child->canReturn=true;Session_SystemSettings_f({{"openq4_system","back"}});CHECK(child->retired && s.parent->active);
        Session_SystemSettings_f({{"openq4_system","open","extra"}});CHECK(s.manager.loads==1);
        Session_SystemSettings_f({{"openq4_system","open"}});CHECK(s.session.guiSystem && s.session.guiSystem!=child && s.manager.loads==2);
        auto* fresh=s.session.guiSystem;s.session.DispatchCommand(fresh,"typed");CHECK(fresh->retired && s.parent->active);
    }
    {
        // The SYSTEM button opens the page; nothing else runs.
        Scenario s;s.parent->stateBools["retainedSystem"]=s.parent->scriptBools["retainedSystem"]=true;s.session.OpenSystemSettingsRoute(false);
        CHECK(s.session.guiSystem && legacyActions==0 && s.parent->named.empty() && common->devWarnings.empty());
    }
    {
        // Refused while still available. No session state does this today;
        // a held message box stands in for any refusal. The stock page opens
        // for this click alone, and the button keeps the page.
        Scenario s;s.parent->stateBools["retainedSystem"]=s.parent->scriptBools["retainedSystem"]=true;s.session.guiMsgRestore=s.parent;
        s.session.OpenSystemSettingsRoute(false);
        CHECK(!s.session.guiSystem && legacyActions==1 && !legacyRetained && s.parent->stateBools["retainedSystem"] && s.parent->scriptBools["retainedSystem"]);
        CHECK(dispatched==std::vector<std::string>{"refreshSystemSettings"} && common->devWarnings.size()==1 && s.session.retainedStock.empty());
        // The multiplayer route falls back the same way, through its stock event.
        s.session.OpenSystemSettingsRoute(true);
        CHECK(legacyActions==1 && s.parent->named==std::vector<std::string>{"fromMp_toSystemStock"});
        CHECK(s.parent->stateBools["retainedSystem"] && common->devWarnings.size()==2 && s.manager.loads==0);
        // Once nothing holds the menu, both routes open the page again.
        s.session.guiMsgRestore=nullptr;s.session.OpenSystemSettingsRoute(false);CHECK(s.session.guiSystem && legacyActions==1);
    }
    {
        // A page that cannot load falls back for the session: the stock page
        // opens, and the button opens it directly from then on.
        Scenario s;s.parent->stateBools["retainedSystem"]=s.parent->scriptBools["retainedSystem"]=true;s.manager.missing=true;
        s.session.OpenSystemSettingsRoute(false);
        CHECK(legacyActions==1 && !legacyRetained && !s.parent->stateBools["retainedSystem"] && !s.parent->scriptBools["retainedSystem"] && common->devWarnings.empty());
        CHECK(s.session.retainedStock.FindIndex(RETAINED_SYSTEM_GUI)==0 && dispatched==std::vector<std::string>{"refreshSystemSettings"});
        s.parent->stateBools["retainedSystem"]=s.parent->scriptBools["retainedSystem"]=true;s.session.OpenSystemSettingsRoute(true);
        CHECK(s.parent->named==std::vector<std::string>{"fromMp_toSystemStock"} && !s.parent->stateBools["retainedSystem"] && !s.parent->scriptBools["retainedSystem"] && s.manager.loads==1);
    }
    {
        // Should the stock action read a stale gui::retainedSystem, it asks for
        // the retained page again; the nested request does nothing, where it
        // would otherwise recurse without end.
        Scenario s;s.parent->stateBools["retainedSystem"]=s.parent->scriptBools["retainedSystem"]=true;
        s.parent->staleScript=true;s.session.guiMsgRestore=s.parent;reenter=&s.session;
        s.session.OpenSystemSettingsRoute(false);
        CHECK(legacyActions==1 && legacyRetained && reentries==1 && dispatched==std::vector<std::string>{"openRetainedSystem"});
        // The guard is released: the next request routes again.
        s.parent->staleScript=false;reenter=nullptr;s.session.OpenSystemSettingsRoute(false);
        CHECK(legacyActions==2 && !legacyRetained && dispatched.back()=="refreshSystemSettings");
    }
    {
        // From multiplayer the page opens over the main menu.
        Scenario s;s.session.OpenSystemSettingsRoute(true);CHECK(s.session.guiSystem && s.parent->named.empty() && legacyActions==0);
        CHECK(s.session.ReturnSystemSettings() && s.parent->active);
    }
    std::printf("SYSTEM Session route: %d checks passed\n",checks);
}
'''


def main():
    menu_path = ROOT / 'src/framework/Session_menu.cpp'
    session_path = ROOT / 'src/framework/Session.cpp'
    header_path = ROOT / 'src/framework/Session_local.h'
    gui_path = ROOT / 'content/baseoq4/pak0/guis/mainmenu.gui'
    menu = menu_path.read_text(encoding='utf-8')
    session = session_path.read_text(encoding='utf-8')
    header = header_path.read_text(encoding='utf-8')
    # The production gate functions, the SYSTEM page path and its missing-settings list.
    gate_start = menu.index('static bool Session_RetainedScreensEnabled( void ) {')
    bodies = [menu[gate_start:menu.index('static const int MENU_CONTROLLER_AXIS_THRESHOLD', gate_start)]]
    bodies += [function_body(menu, signature) for signature in (
        'bool idSessionLocal::OpenSystemSettings(', 'bool idSessionLocal::ReturnSystemSettings(',
        'bool idSessionLocal::RetainedSystemAvailable(', 'void idSessionLocal::OpenSystemSettingsRoute(',
        'void idSessionLocal::CloseSystemSettings(', 'void idSessionLocal::ReportSystemSettings(',
        'void idSessionLocal::SetGUI(', 'void idSessionLocal::ExitMenu(', 'void idSessionLocal::GuiFrameEvents(')]
    bodies += [function_body(session, 'void idSessionLocal::SetPlayingSoundWorld()'),
               function_body(session, 'static void Session_SystemSettings_f(')]
    # Compile the unchanged retained-command branch; the rest of DispatchCommand
    # is legacy game/menu dispatch, outside this route's boundary.
    dispatch = function_body(menu, 'void idSessionLocal::DispatchCommand(')
    start = dispatch.index('bool closeRequested = false;')
    end = dispatch.index('\n\tif ( gui == guiMainMenu )', start)
    bodies.append('void idSessionLocal::DispatchCommand(idUserInterface* gui,const char* menuCommand) {\n' + dispatch[start:end] + '\n}')
    main_menu = function_body(menu, 'void idSessionLocal::HandleMainMenuCommands(')
    for command, multiplayer in (('openRetainedSystem', 'false'), ('openRetainedSystemFromMp', 'true')):
        assert f'if ( !idStr::Icmp( cmd, "{command}" ) ) {{\n\t\t\tOpenSystemSettingsRoute( {multiplayer} );\n\t\t\treturn;' in main_menu
    # The multiplayer menu's SYSTEM route: the retained page over the main
    # page, else the stock route, which a refused open also takes.
    gui = gui_path.read_text(encoding='utf-8')
    to_system = gui[gui.index('onNamedEvent fromMp_toSystem {'):]
    to_system = to_system[:to_system.index('onNamedEvent fromMp_toSystemStock {')]
    assert ('onNamedEvent fromMp_toSystem {\n\t\tif ("gui::retainedSystem" == 1) {\n\t\t\tset "desktop::dest" "0" ;\n'
            '\t\t\tresettime "anim_retainedSystemFromMp" "0" ;\n\t\t} else {\n\t\t\tset "desktop::active" "1" ;\n'
            '\t\t\tresettime "anim_in" "0" ;\n\t\t\tset "desktop::dest" "22" ;\n\t\t}\n\t}') in to_system
    # Only a page transition lowers desktop::active again, and the retained
    # branch runs none, so it must never raise it.
    retained_branch = to_system[to_system.index('if ("gui::retainedSystem" == 1) {'):to_system.index('} else {')]
    assert 'desktop::active' not in retained_branch and 'anim_in' not in retained_branch
    # The session fires the event, so the request comes from a timeline: a
    # command set in a named event is never collected. It follows anim_in, so
    # anim_in's own frame command runs first.
    asker = gui[gui.index('windowDef anim_retainedSystemFromMp\n'):]
    asker = asker[:asker.index('\n\t\t}\n')]
    assert 'notime\t1' in asker and 'onTime 0 {\n\t\t\t\tset "cmd" "openRetainedSystemFromMp" ;\n\t\t\t\tset "notime" "1" ;' in asker
    anims, at = gui.index('\n\twindowDef p_anims\n'), gui.index('windowDef anim_retainedSystemFromMp\n')
    assert anims < gui.index('windowDef anim_in\n') < at and '\n\twindowDef ' not in gui[anims + 1:at]
    stock = gui[gui.index('onNamedEvent fromMp_toSystemStock {'):]
    assert stock[:stock.index('}')].count('set "desktop::dest" "22" ;') == 1
    assert 'ui_retainedSystem( "ui_retainedSystem", "0", CVAR_GUI | CVAR_BOOL' in menu
    assert 'ui_retained( "ui_retained", "1", CVAR_GUI | CVAR_BOOL | CVAR_ARCHIVE,' in menu
    assert ('return ui_retainedSystem.GetBool() || ( Session_RetainedScreensEnabled() &&\n'
            '\t\tRETAINED_SYSTEM_MISSING_SETTINGS[0] == NULL && RETAINED_SYSTEM_INCOMPLETE_SETTINGS[0] == NULL );') in menu
    assert 'ReturnSystemSettings();' in function_body(menu, 'void idSessionLocal::StartMenu(')
    unload = function_body(session, 'void idSessionLocal::UnloadMap(')
    assert unload.index('CloseSystemSettings();') < unload.index('game->MapShutdown();')
    assert 'SetGUI( NULL, NULL );' in function_body(session, 'void idSessionLocal::StopInternal(')
    assert 'guiSystem = guiSystemParent = NULL;' in function_body(session, 'void idSessionLocal::Clear(')
    assert 'guiActive == guiSystem || guiActive->State().GetBool( "gameDraw" )' in session
    assert '"openq4_system", Session_SystemSettings_f' in session
    assert 'HandleGuiCommand_t\tguiSystemParentHandle;' in header
    compiler = next((p for name in ('clang++', 'g++', 'c++') if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='ui-system-session-route-', dir=ROOT / '.tmp'))
    source = output / 'route.cpp'
    binary = output / 'route.exe'
    source.write_text(SUPPORT + '\n'.join(bodies) + MAIN, encoding='utf-8')
    environment = dict(os.environ, TEMP=str(output), TMP=str(output))
    command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Wno-unused-variable', '-Wno-unused-but-set-variable', str(source), '-o', str(binary)]
    compile_result = subprocess.run(command, env=environment, capture_output=True, text=True)
    run_result = subprocess.run([str(binary)], env=environment, capture_output=True, text=True) if compile_result.returncode == 0 else None
    result = {
        'passed': run_result is not None and run_result.returncode == 0,
        'sources': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (menu_path, session_path, header_path, gui_path, Path(__file__))},
        'extracted_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'command': command,
        'compile': {'exit': compile_result.returncode, 'stdout': compile_result.stdout, 'stderr': compile_result.stderr},
        'run': None if run_result is None else {'exit': run_result.returncode, 'stdout': run_result.stdout, 'stderr': run_result.stderr},
    }
    (output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result['run'] or result['compile'], indent=2))
    print(output / 'result.json')
    if not result['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
