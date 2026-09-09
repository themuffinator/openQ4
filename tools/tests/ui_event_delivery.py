#!/usr/bin/env python3
"""Exercise production managed-action pump and Session lifecycle delivery.

The manager, managed/deferred interfaces and Session methods are extracted from
production. Counted backend/host doubles isolate queue ownership and callbacks;
the canonical event evaluator and renderer are covered by separate suites.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body
from ui_manager_lifecycle import production_source

ROOT = Path(__file__).resolve().parents[2]

SESSION = r'''
#include <cstring>
using HandleGuiCommand_t = bool (*)(const char*);
static idUserInterfaceManagerLocal* uiManager=&uiManagerLocal;
static int Sys_Milliseconds() { return 100; }
enum { SE_NONE=0, INHIBIT_SESSION=1 };
static bool previewOpen=false;
static int previewFrames=0;
static bool RetainedUI_IsOpen() { return previewOpen; }
static void RetainedUI_Close() { previewOpen=false; }
static void RetainedUI_FrameInput() { ++previewFrames; }
static void ClearMenuControllerRepeatState() {}
struct Console { bool open=false; bool Active() const { return open; } } consoleObject,*console=&consoleObject;
struct UserCommands { bool inhibited=false; void InhibitUsercmd(int,bool value) { inhibited=value; } } userCommands,*usercmdGen=&userCommands;
struct SoundWorld { bool paused=true; bool IsPaused() const { return paused; } void UnPause() { paused=false; } } sound;
struct Game { const char* HandleGuiCommands(const char*) { assert(false); return ""; } } *game=nullptr;
struct idSessionLocal {
    idUserInterface *guiTest=nullptr,*guiActive=nullptr,*guiMainMenu=nullptr,*guiRestartMenu=nullptr,
        *guiMsgRestore=nullptr,*guiMsg=nullptr,*guiDemoMenu=nullptr,*guiIntro=nullptr,*guiTakeNotes=nullptr;
    HandleGuiCommand_t guiHandle=nullptr;
    bool mapSpawned=false;
    int menuProfileSaveVarsMsec=0,menuProfileMainVarsMsec=0,legacyDispatches=0,soundSelections=0;
    SoundWorld* sw=&sound;
    struct Arena { bool IsGui(idUserInterface*) { return false; } void HandleGuiCommand(const char*) { assert(false); } } arenaCampaign;
    void TestGUI(const char*);
    void SetGUI(idUserInterface*,HandleGuiCommand_t);
    void ExitMenu();
    void GuiFrameEvents();
    void PumpApplicationActions(idUserInterface* = nullptr);
    void DispatchCommand(idUserInterface*,const char*,bool=true);
    void SetSaveGameGuiVars() {}
    void SetMainMenuGuiVars(bool) {}
    void SetPlayingSoundWorld(SoundWorld*) { ++soundSelections; }
    void HandleMainMenuCommands(const char*) { ++legacyDispatches; }
    void HandleDemoMenuCommand(const char*) { ++legacyDispatches; }
    void HandleIntroMenuCommands(const char*) { ++legacyDispatches; }
    void HandleMsgCommands(const char*) { ++legacyDispatches; }
    void HandleNoteCommands(const char*) { ++legacyDispatches; }
    void HandleRestartMenuCommands(const char*) { ++legacyDispatches; }
    void HandleInGameCommands(const char*) { ++legacyDispatches; }
    void StartMenu() { assert(false); }
};
static void PumpControllerMenuNavigation(idSessionLocal*) {}
static void SyncMainMenuSettingsScrollPages(idUserInterface* gui) {
    if(gui)assert(uiManagerLocal.allocations.Find(static_cast<idUserInterfaceManaged*>(gui)));
}
struct Scenario {
    idSessionLocal session;
    Scenario() { applicationActions.clear(); consoleObject.open=false; previewOpen=false; previewFrames=0; }
    ~Scenario() {
        while(uiManagerLocal.allocations.Num())delete uiManagerLocal.allocations[0];
        assert(!uiManagerLocal.applicationPumpDepth && !recycledAllocation);
    }
    idUserInterfaceRetained* Make(const char* name="view.q4ui") {
        auto* gui=new idUserInterfaceRetained; assert(gui->InitFromFile(name)); return gui;
    }
};
'''

MAIN = r'''
int main() {
    {
        Scenario s;
        auto* first=s.Make(); auto* second=s.Make("hidden.q4ui");
        auto* legacy=new idUserInterfaceLocal; legacy->InitFromFile("legacy.gui");
        first->pendingActions={"first"}; second->pendingActions={"hidden"};
        consoleObject.open=true; previewOpen=true;
        s.session.GuiFrameEvents();
        assert((applicationActions==std::vector<std::string>{"first","hidden"}));
        assert(previewFrames==1 && userCommands.inhibited && s.session.legacyDispatches==0);
        s.session.GuiFrameEvents(); assert(applicationActions.size()==2);
        previewOpen=false; consoleObject.open=false;
        second->pendingActions={"no active gui"}; s.session.GuiFrameEvents();
        assert(applicationActions.back()=="no active gui" && !userCommands.inhibited);
        idUserInterfaceRetained unknown(false); unknown.pendingActions={"unmanaged"};
        s.session.PumpApplicationActions(&unknown);
        assert(unknown.pendingActions.size()==1 && applicationActions.size()==3);
    }
    {
        Scenario s;
        auto* first=s.Make("first.q4ui"); auto* victim=s.Make("victim.q4ui");
        idUserInterfaceRetained* replacement=nullptr;
        first->pendingActions={"first"}; victim->pendingActions={"stale"};
        first->onDispatch=[&] {
            first->onDispatch={}; recycleAllocation=true; delete victim;
            replacement=s.Make("replacement.q4ui"); recycleAllocation=false;
            assert(replacement==victim); replacement->pendingActions={"replacement"};
        };
        s.session.PumpApplicationActions();
        assert(applicationActions==std::vector<std::string>{"first"});
        s.session.PumpApplicationActions();
        assert((applicationActions==std::vector<std::string>{"first","replacement"}));
    }
    {
        Scenario s;
        auto* first=s.Make(); auto* peer=s.Make("peer.q4ui");
        first->pendingActions={"self"}; peer->pendingActions={"peer"};
        first->onDispatch=[&] { delete peer; delete first; };
        const int before=destroyed; s.session.PumpApplicationActions();
        assert(destroyed==before+2 && applicationActions==std::vector<std::string>{"self"});
    }
    {
        Scenario s;
        auto* first=s.Make(); auto* idle=s.Make("idle.q4ui");
        first->pendingActions={"initial"};
        first->onDispatch=[&] {
            first->onDispatch={}; first->pendingActions={"next self"}; idle->pendingActions={"next peer"};
            auto* added=s.Make("added.q4ui"); added->pendingActions={"next new"};
            s.session.PumpApplicationActions(); // Nested global drain does nothing.
        };
        s.session.PumpApplicationActions();
        assert(applicationActions==std::vector<std::string>{"initial"});
        s.session.PumpApplicationActions();
        assert((applicationActions==std::vector<std::string>{"initial","next self","next peer","next new"}));
        s.session.PumpApplicationActions(); assert(applicationActions.size()==4);
    }
    {
        Scenario s;
        auto* gui=s.Make(); gui->pendingActions={"bounded"};
        gui->onDispatch=[&] { gui->pendingActions={"bounded"}; s.session.PumpApplicationActions(gui); };
        s.session.PumpApplicationActions();
        assert(applicationActions.size()==8 && gui->pendingActions.size()==1);
        gui->onDispatch={}; s.session.PumpApplicationActions();
        assert(applicationActions.size()==9 && gui->pendingActions.empty());
    }
    {
        Scenario s;
        for(int i=0;i<300;++i)s.Make()->pendingActions={std::to_string(i)};
        s.session.PumpApplicationActions(); assert(applicationActions.size()==256);
        s.session.PumpApplicationActions(); assert(applicationActions.size()==300);
        s.session.PumpApplicationActions(); assert(applicationActions.size()==300);
    }
    {
        Scenario s;
        auto* old=s.Make("old.q4ui"); auto* next=s.Make("next.q4ui");
        int deactivations=0,activations=0;
        old->active=true; s.session.guiActive=old;
        old->onActivate=[&](bool active) {
            assert(!active && !s.session.guiActive); ++deactivations;
            old->pendingActions={"dismiss","deactivation setting"};
        };
        next->onActivate=[&](bool active) { assert(active); ++activations; next->pendingActions={"activation setting"}; };
        s.session.SetGUI(next,nullptr);
        assert(s.session.guiActive==next && next->active && !old->active);
        assert(deactivations==1 && activations==1);
        assert((applicationActions==std::vector<std::string>{"deactivation setting","activation setting"}));
        next->onActivate=[&](bool active) { assert(!active && !s.session.guiActive); next->pendingActions={"dismiss","exit setting"}; };
        s.session.ExitMenu();
        assert(!s.session.guiActive && applicationActions.back()=="exit setting" && s.session.soundSelections==1);
        s.session.ExitMenu(); assert(applicationActions.size()==3);
    }
    {
        Scenario s;
        auto* active=s.Make("normal.q4ui"); active->active=true; s.session.guiActive=active;
        s.session.TestGUI("test.q4ui"); auto* old=static_cast<idUserInterfaceRetained*>(s.session.guiTest);
        int deactivations=0;
        old->onActivate=[&](bool value) {
            assert(!value && !s.session.guiTest); ++deactivations;
            old->pendingActions={"dismiss","before delete"};
        };
        // Closing an active test during the global pump nests a targeted
        // deactivation drain, which must execute before the old owner is freed.
        old->pendingActions={"dismiss"}; const int before=destroyed;
        s.session.PumpApplicationActions();
        assert(!s.session.guiTest && s.session.guiActive==active && active->active);
        assert(deactivations==1 && destroyed==before+1 && applicationActions==std::vector<std::string>{"before delete"});
        s.session.PumpApplicationActions(); assert(applicationActions.size()==1);
        s.session.TestGUI("replacement-source.q4ui");
        old=static_cast<idUserInterfaceRetained*>(s.session.guiTest);
        old->onActivate=[&](bool value) { assert(!value); old->pendingActions={"dismiss","replace cleanup"}; };
        s.session.TestGUI("replacement.q4ui");
        assert(s.session.guiTest && std::string(s.session.guiTest->Name())=="replacement.q4ui");
        assert(applicationActions.back()=="replace cleanup" && active->active);
    }
    {
        Scenario s;
        auto* next=s.Make();
        next->onActivate=[&](bool value) { next->pendingActions=value?std::vector<std::string>{"dismiss"}:std::vector<std::string>{"dismiss","cleanup"}; };
        s.session.SetGUI(next,nullptr);
        assert(!s.session.guiActive && !next->active && applicationActions==std::vector<std::string>{"cleanup"});
    }
    {
        Scenario s;
        auto* wrapper=uiManagerLocal.Alloc(); assert(wrapper->InitFromFile("deferred.q4ui"));
        assert(uiManagerLocal.allocations.Num()==1);
        wrapper->SetStateString("queuedAction","wrapped"); wrapper->HandleNamedEvent("queue");
        assert(std::string(static_cast<idUserInterfaceManaged*>(wrapper)->PendingApplicationCommand())=="retained-pending");
        s.session.PumpApplicationActions(wrapper);
        assert(applicationActions==std::vector<std::string>{"wrapped"});
        s.session.PumpApplicationActions(); assert(applicationActions.size()==1);
    }
    std::puts("UI event delivery: production manager/Session pump, identity reuse, callback deletion, nested bounds, lifecycle teardown, wrapper dispatch and suspended input passed");
}
'''


def main():
    source = (ROOT / 'src/framework/Session_menu.cpp').read_text(encoding='utf-8')
    signatures = (
        'static void Session_DispatchApplicationCommand(',
        'void idSessionLocal::PumpApplicationActions(',
        'void idSessionLocal::DispatchCommand(',
        'void idSessionLocal::SetGUI(',
        'void idSessionLocal::ExitMenu(',
        'void idSessionLocal::GuiFrameEvents(',
    )
    implementation = '\n'.join(function_body(source, signature) for signature in signatures)
    session_source = (ROOT / 'src/framework/Session.cpp').read_text(encoding='utf-8')
    implementation += '\n' + function_body(session_source, 'void idSessionLocal::TestGUI(')
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-event-delivery-', dir=ROOT / '.tmp') as directory:
        test = Path(directory) / 'delivery.cpp'
        binary = Path(directory) / 'delivery.exe'
        test.write_text(production_source() + SESSION + implementation + MAIN, encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', str(test), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
