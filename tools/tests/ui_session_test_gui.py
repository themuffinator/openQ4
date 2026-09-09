#!/usr/bin/env python3
"""Exercise production TestGUI ownership and frame dispatch without a game or input.

The unchanged session functions are compiled against counted GUI/manager doubles.
Retired GUI source buffers are poisoned but kept allocated, exposing borrowed
name lifetimes deterministically without making the test itself use freed memory.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct idStr : std::string {
    using std::string::string;
    operator const char*() const { return c_str(); }
    bool IsEmpty() const { return empty(); }
};
static std::vector<std::string> events;
struct Common {
    int GetPresentationTime() const { return 1234; }
} commonObject, *common=&commonObject;
enum { SE_NONE=0, INHIBIT_SESSION=1 };
struct sysEvent_t { int evType; };

struct idUserInterface {
    int id, activated=0, deactivated=0, frees=0;
    bool active=false, retired=false;
    std::array<char,1024> source{};
    std::string command;
    std::function<void()> onDeactivate;
    idUserInterface(int number,const char* path):id(number) {
        assert(std::strlen(path)<source.size());
        std::copy_n(path,std::strlen(path)+1,source.data());
    }
    const char* Name() const { assert(!retired); return source.data(); }
    const char* HandleEvent(const sysEvent_t* event,int time) {
        assert(!retired && event && event->evType==SE_NONE && time==1234);
        return command.c_str();
    }
    const char* Activate(bool value,int time) {
        assert(!retired && time==1234);
        events.push_back(std::string(value?"activate:":"deactivate:")+std::to_string(id));
        active=value;
        if(value)++activated;
        else { ++deactivated; if(onDeactivate)onDeactivate(); }
        return "";
    }
};

struct Manager {
    struct Request { std::string path; bool autoLoad,needUnique,forceUnique; };
    std::vector<Request> requests;
    std::vector<std::unique_ptr<idUserInterface>> storage;
    std::function<void()> onFree;
    idUserInterface* Make(const char* path) {
        storage.emplace_back(std::make_unique<idUserInterface>(static_cast<int>(storage.size())+1,path));
        return storage.back().get();
    }
    idUserInterface* FindGui(const char* path,bool autoLoad,bool needUnique,bool forceUnique) {
        requests.push_back({path,autoLoad,needUnique,forceUnique});
        events.push_back(std::string("load:")+path);
        if(!std::strcmp(path,"missing.q4ui")) return nullptr;
        // Model the manager's shared cache when uniqueness was not requested.
        if(!needUnique)for(auto& gui:storage)
            if(!gui->retired && !std::strcmp(gui->Name(),path))return gui.get();
        return Make(path);
    }
    void DeAlloc(idUserInterface* gui) {
        assert(gui && !gui->retired && !gui->active && gui->deactivated==1);
        events.push_back("free:"+std::to_string(gui->id));
        if(onFree)onFree();
        ++gui->frees; gui->retired=true;
        std::fill(gui->source.begin(),gui->source.end(),'!');
        gui->source.back()='\0';
    }
    void CheckRequests() const {
        for(const auto& request:requests)
            assert(request.autoLoad && request.needUnique && !request.forceUnique);
    }
} *uiManager=nullptr;

struct idSessionLocal {
    idUserInterface *guiTest=nullptr,*guiActive=nullptr;
    std::function<void(idUserInterface*,const char*)> onDispatch;
    int dispatches=0;
    void TestGUI(const char* guiName);
    void GuiFrameEvents();
    void DispatchCommand(idUserInterface* gui,const char* command) {
        assert(gui && !gui->retired && command && *command);
        ++dispatches;
        if(onDispatch)onDispatch(gui,command);
    }
};
struct Console { bool Active() const { return false; } } consoleObject,*console=&consoleObject;
struct UserCommands {
    bool inhibited=false;
    void InhibitUsercmd(int reason,bool value) { assert(reason==INHIBIT_SESSION); inhibited=value; }
} userCommands,*usercmdGen=&userCommands;
static bool RetainedUI_IsOpen() { return false; }
static void RetainedUI_FrameInput() { assert(false); }
static void ClearMenuControllerRepeatState() {}
static void PumpControllerMenuNavigation(idSessionLocal*) {}
static std::vector<idUserInterface*> synchronized;
static void SyncMainMenuSettingsScrollPages(idUserInterface* gui) {
    assert(!gui || !gui->retired);
    synchronized.push_back(gui);
}

struct Scenario {
    Manager manager;
    idSessionLocal session;
    Scenario() { uiManager=&manager; events.clear(); synchronized.clear(); userCommands.inhibited=false; }
    ~Scenario() { manager.CheckRequests(); }
    idUserInterface* Active(const char* name) {
        auto* gui=manager.Make(name); gui->Activate(true,1234);
        session.guiActive=gui; events.clear(); return gui;
    }
};
'''

MAIN = r'''
int main() {
    {
        Scenario s;
        auto* active=s.Active("same.q4ui");
        s.session.TestGUI(active->Name());
        auto* test=s.session.guiTest;
        assert(test && test!=active && test->active && test->activated==1);
        assert(std::string(test->Name())=="same.q4ui");
        assert(s.session.guiActive==active && active->active && active->activated==1);
        assert(active->deactivated==0 && active->frees==0);
        s.session.TestGUI(nullptr);
        assert(!s.session.guiTest && test->retired && test->frees==1 && test->deactivated==1);
        assert(s.session.guiActive==active && active->active && active->deactivated==0 && active->frees==0);
        const auto before=events;
        s.session.TestGUI(""); s.session.TestGUI(nullptr);
        assert(events==before); // Empty/null close is idempotent.
    }
    {
        Scenario s;
        s.session.TestGUI("first.q4ui"); auto* first=s.session.guiTest;
        events.clear();
        first->onDeactivate=[&] { assert(!s.session.guiTest); };
        s.manager.onFree=[&] { assert(!s.session.guiTest); };
        s.session.TestGUI("second.q4ui"); auto* second=s.session.guiTest;
        assert(second && second!=first && second->active && !second->retired);
        assert(first->retired && first->frees==1 && first->deactivated==1);
        assert((events==std::vector<std::string>{"load:second.q4ui","deactivate:1","free:1","activate:2"}));
        s.session.TestGUI("");
        assert(!s.session.guiTest && second->frees==1 && second->deactivated==1);
    }
    {
        Scenario s;
        // A caller may pass Name() from the very instance being replaced.
        const std::string longName="guis/"+std::string(240,'a')+".q4ui";
        s.session.TestGUI(longName.c_str()); auto* first=s.session.guiTest;
        const char* borrowed=first->Name(); events.clear();
        s.session.TestGUI(borrowed); auto* replacement=s.session.guiTest;
        assert(replacement && replacement!=first && replacement->active);
        assert(replacement->Name()==longName && s.manager.requests.back().path==longName);
        assert(first->retired && first->frees==1 && borrowed[0]=='!');
        assert(events.front()=="load:"+longName && events.back()=="activate:2");
        s.session.TestGUI(nullptr);
    }
    {
        Scenario s;
        auto* active=s.Active("active.q4ui");
        s.session.TestGUI("working.q4ui"); auto* working=s.session.guiTest;
        const auto storage=s.manager.storage.size(); events.clear();
        s.session.TestGUI("missing.q4ui");
        assert(s.session.guiTest==working && working->active && !working->retired);
        assert(working->activated==1 && working->deactivated==0 && working->frees==0);
        assert(active->active && active->deactivated==0 && active->frees==0);
        assert(s.manager.storage.size()==storage && events==std::vector<std::string>{"load:missing.q4ui"});
        s.session.TestGUI(nullptr); events.clear();
        s.session.TestGUI("missing.q4ui");
        assert(!s.session.guiTest && s.session.guiActive==active && active->active);
        assert(events==std::vector<std::string>{"load:missing.q4ui"});
    }
    {
        Scenario s;
        auto* active=s.Active("shared-before-fix.gui");
        // Defensive cleanup of an alias created by the old TestGUI path must
        // neither deactivate nor release the normal session owner.
        s.session.guiTest=active;
        s.session.TestGUI(nullptr);
        assert(!s.session.guiTest && active->active && active->deactivated==0 && active->frees==0);
        assert(events.empty());
        s.session.guiTest=active;
        s.session.TestGUI("missing.q4ui");
        assert(s.session.guiTest==active && active->active && active->deactivated==0);
        events.clear();
        s.session.TestGUI(active->Name()); auto* test=s.session.guiTest;
        assert(test && test!=active && test->active && s.session.guiActive==active);
        assert(active->active && active->deactivated==0 && active->frees==0);
        assert((events==std::vector<std::string>{"load:shared-before-fix.gui","activate:2"}));
        s.session.TestGUI("");
        assert(test->frees==1 && active->active && active->frees==0);
    }
    {
        Scenario s;
        s.session.TestGUI("frame-close.q4ui"); auto* test=s.session.guiTest;
        test->command="close";
        s.session.onDispatch=[&](idUserInterface* gui,const char* command) {
            assert(gui==test && std::string(command)=="close");
            s.session.TestGUI(nullptr);
        };
        s.session.GuiFrameEvents();
        assert(test->retired && !s.session.guiTest && !s.session.guiActive && s.session.dispatches==1);
        assert(userCommands.inhibited && synchronized==std::vector<idUserInterface*>{nullptr});
        s.session.GuiFrameEvents();
        assert(!userCommands.inhibited && synchronized.size()==1 && s.session.dispatches==1);
    }
    {
        Scenario s;
        auto* active=s.Active("normal-menu.gui");
        s.session.TestGUI("frame-over-menu.q4ui"); auto* test=s.session.guiTest;
        test->command="close";
        s.session.onDispatch=[&](idUserInterface* gui,const char*) {
            assert(gui==test); s.session.TestGUI(nullptr);
        };
        s.session.GuiFrameEvents();
        assert(test->retired && !s.session.guiTest && active->active && !active->retired);
        assert(synchronized==std::vector<idUserInterface*>{active});
    }
    {
        Scenario s;
        s.session.TestGUI("frame-first.q4ui"); auto* first=s.session.guiTest;
        first->command="replace";
        s.session.onDispatch=[&](idUserInterface* gui,const char* command) {
            assert(gui==first && std::string(command)=="replace");
            s.session.TestGUI("frame-replacement.q4ui");
        };
        s.session.GuiFrameEvents();
        auto* replacement=s.session.guiTest;
        assert(first->retired && replacement && replacement!=first && replacement->active);
        assert(synchronized==std::vector<idUserInterface*>{replacement});
        s.session.TestGUI(nullptr);
    }
    {
        Scenario s;
        auto* first=s.Active("first-menu.gui");
        first->command="replace-menu";
        s.session.onDispatch=[&](idUserInterface* gui,const char*) {
            assert(gui==first);
            first->Activate(false,1234); s.manager.DeAlloc(first);
            s.session.guiActive=s.manager.Make("replacement-menu.gui");
            s.session.guiActive->Activate(true,1234);
        };
        s.session.GuiFrameEvents();
        assert(first->retired && s.session.guiActive!=first && s.session.guiActive->active);
        assert(synchronized==std::vector<idUserInterface*>{s.session.guiActive});
    }
    std::puts("Session GUI lifecycle: unique test ownership, teardown ordering, borrowed names, rollback, alias-safe close and post-dispatch synchronization passed");
}
'''


def main():
    source = (ROOT / 'src/framework/Session.cpp').read_text(encoding='utf-8')
    implementation = function_body(source, 'void idSessionLocal::TestGUI(')
    menu_source = (ROOT / 'src/framework/Session_menu.cpp').read_text(encoding='utf-8')
    implementation += '\n' + function_body(menu_source, 'void idSessionLocal::GuiFrameEvents(')
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='session-test-gui-', dir=ROOT / '.tmp') as directory:
        test = Path(directory) / 'session.cpp'
        binary = Path(directory) / 'session.exe'
        test.write_text(SUPPORT + implementation + MAIN, encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', str(test), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
