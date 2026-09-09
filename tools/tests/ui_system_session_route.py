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
#include <memory>
#include <string>
#include <vector>
#include <cctype>

struct idStr : std::string {
    using std::string::string;
    operator const char*() const {return c_str();}
    static int Icmp(const char* a,const char* b) {
        while(*a && *b && std::tolower(*a)==std::tolower(*b)){++a;++b;}
        return std::tolower(*a)-std::tolower(*b);
    }
};
struct idCmdArgs {
    std::vector<std::string> values;
    int Argc() const{return static_cast<int>(values.size());}
    const char* Argv(int index) const{return values.at(index).c_str();}
};
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
} commonObject,*common=&commonObject;
struct CVar {bool value=false;bool GetBool() const{return value;}} ui_retainedSystem;
enum {SE_NONE=0,INHIBIT_SESSION=1};
struct sysEvent_t {int evType=SE_NONE;};
using HandleGuiCommand_t=bool (*)(const char*);
struct Sound {
    bool paused=true;
    int unpauses=0;
    bool IsPaused() const{return paused;}
    void UnPause(){paused=false;++unpauses;}
};
struct idUserInterface {
    std::string source;
    bool active=false,retired=false,valid=true,canReturn=true,close=false;
    int activates=0,deactivates=0,drains=0,backs=0,frames=0;
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
        CHECK(!retired && std::string(event)=="onBack");++backs;if(onBack)onBack();
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
    void SetGUI(idUserInterface*,HandleGuiCommand_t);void ExitMenu();void GuiFrameEvents();
    void SetPlayingSoundWorld();void SetPlayingSoundWorld(Sound* sound){requestedSoundWorld=sound;}
    bool IsMultiplayer(){return multiplayer;}
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
static void SyncMainMenuSettingsScrollPages(idUserInterface* gui){CHECK(!gui || !gui->retired);}
static bool UI_DispatchApplicationActions(idUserInterface*,const char* command,bool& close){close=std::string(command)=="typed";return true;}
static bool ParentHandler(const char*){return true;}
struct Scenario {
    Manager manager;Sound gameSound,menuSound;
    idSessionLocal& session=sessLocal;
    idUserInterface* parent;
    Scenario(){sessLocal=idSessionLocal{};uiManager=&manager;preview=false;ui_retainedSystem.value=true;common->output.clear();events.clear();userCommands.inhibited=false;
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
        ui_retainedSystem.value=true;s.session.guiTest=s.parent;CHECK(!s.session.OpenSystemSettings());s.session.guiTest=nullptr;
        preview=true;CHECK(!s.session.OpenSystemSettings());preview=false;
        s.session.guiActive=nullptr;CHECK(!s.session.OpenSystemSettings());s.session.guiActive=s.parent;
        s.session.guiMsgRestore=s.parent;CHECK(!s.session.OpenSystemSettings());s.session.guiMsgRestore=nullptr;
        s.manager.missing=true;CHECK(!s.session.OpenSystemSettings());CHECK(s.session.guiActive==s.parent && s.parent->deactivates==0);
        s.manager.missing=false;s.manager.valid=false;CHECK(!s.session.OpenSystemSettings());CHECK(s.manager.frees==1 && s.parent->deactivates==0 && s.session.guiSystem==nullptr);
    }
    {
        Scenario s;CHECK(s.session.OpenSystemSettings());auto* child=s.session.guiSystem;
        CHECK(child && child->active && !s.parent->active && s.parent->deactivates==1 && s.session.guiTest==nullptr);
        CHECK(s.session.guiSystemParent==s.parent && s.session.guiSystemParentHandle==ParentHandler && s.session.guiHandle==nullptr);
        CHECK(events[0]=="load" && events[1]=="deactivate:guis/mainmenu.gui" && events[2]=="drain:guis/mainmenu.gui");
        CHECK(s.session.OpenSystemSettings() && s.manager.loads==1);
        s.session.GuiFrameEvents();CHECK(userCommands.inhibited && child->frames==2);
        s.session.ReportSystemSettings();CHECK(common->output.find("child=1 guiTest=0 menu=1 map=1 multiplayer=0 menuSound=1 canReturn=1")!=std::string::npos);
        child->onDeactivate=[&]{CHECK(!s.session.guiSystem && !s.session.guiSystemParent && s.session.guiActive!=child);CHECK(!s.session.OpenSystemSettings());};
        s.manager.onFree=[&](auto* gui){CHECK(gui==child && child->deactivates==1 && child->drains==2);};
        const auto refreshes=s.session.mainRefresh;
        CHECK(s.session.ReturnSystemSettings());CHECK(child->retired && s.manager.frees==1 && s.parent->active && s.session.guiHandle==ParentHandler);
        CHECK(s.session.mainRefresh==refreshes); // Returning must preserve the parent's section/selection/catalog state.
        CHECK(s.session.requestedSoundWorld==&s.menuSound && s.gameSound.paused && s.gameSound.unpauses==0);
        CHECK(!s.session.ReturnSystemSettings());s.session.CloseSystemSettings();CHECK(s.manager.frees==1);
        s.session.ExitMenu();CHECK(s.session.guiActive==nullptr && s.gameSound.unpauses==1 && s.session.requestedSoundWorld==&s.gameSound);
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
    std::printf("SYSTEM Session route: %d checks passed\n",checks);
}
'''


def main():
    menu_path = ROOT / 'src/framework/Session_menu.cpp'
    session_path = ROOT / 'src/framework/Session.cpp'
    header_path = ROOT / 'src/framework/Session_local.h'
    menu = menu_path.read_text(encoding='utf-8')
    session = session_path.read_text(encoding='utf-8')
    header = header_path.read_text(encoding='utf-8')
    bodies = [function_body(menu, signature) for signature in (
        'bool idSessionLocal::OpenSystemSettings(', 'bool idSessionLocal::ReturnSystemSettings(',
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
    assert 'if ( !idStr::Icmp( cmd, "openRetainedSystem" ) ) {\n\t\t\tOpenSystemSettings();\n\t\t\treturn;' in main_menu
    assert 'ui_retainedSystem( "ui_retainedSystem", "0", CVAR_GUI | CVAR_BOOL' in menu
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
        'sources': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (menu_path, session_path, header_path, Path(__file__))},
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
