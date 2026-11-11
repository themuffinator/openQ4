#!/usr/bin/env python3
"""Execute production Arena pause ownership, sound clocks and session routing."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#define SOUNDWORLD_GAME 1
#define SOUNDWORLD_MENU 2
template<class T> struct List : std::vector<T> {
    using std::vector<T>::operator=;
    int Num() const { return static_cast<int>(this->size()); }
};
struct Voice {
    int pauses=0, resumes=0;
    bool paused=false;
    void Pause() { ++pauses; paused=true; }
    void UnPause() { ++resumes; paused=false; }
};
struct idSoundChannel {
    bool mutableVoice=false;
    Voice* hardwareVoice=nullptr;
    bool CanMute() const { return mutableVoice; }
};
struct Emitter { List<idSoundChannel*> channels; };
struct SoundFade {
    void SetVolume(float) {}
    void Fade(float,int,int) {}
};
struct Cvar { int GetInteger() const { return 250; } } s_unpauseFadeInTime;
struct idSoundWorldLocal {
    bool isPaused=false;
    int pausedTime=0, accumulatedPauseTime=0;
    SoundFade pauseFade;
    List<Emitter*> emitters;
    void Pause(); void UnPause(); int GetSoundTime();
    bool IsPaused() const { return isPaused; }
};
using idSoundWorld=idSoundWorldLocal;
struct SoundSystem {
    int now=100;
    idSoundWorld* world=nullptr;
    int SoundTime() const { return now; }
    int RefreshSoundTime() { return now; }
    idSoundWorld* GetSoundWorldFromId(int id) { assert(id==SOUNDWORLD_GAME); return world; }
} soundSystemLocal;
SoundSystem* soundSystem=&soundSystemLocal;
struct idMultiplayerGame {
    bool arenaSoundPauseOwned=false;
    void UpdateArenaSoundPause(bool);
};
struct GUI {
    const char* name="ordinary";
    const char* Name() const { return name; }
    void Activate(bool,int) {}
};
using idUserInterface=GUI;
struct Common { int GetPresentationTime() const { return 0; } } commonLocal;
Common* common=&commonLocal;
namespace openq4 { void NativeInputBeforeSessionChange() {} }
struct idStr { static int Icmp(const char* a,const char* b) { return std::strcmp(a,b); } };
bool UI_IsRetainedPath(const char* name) { return std::strstr(name,".rui")!=nullptr; }
struct idSessionLocal {
    GUI *guiSystem=nullptr,*guiActive=nullptr,*guiMsg=nullptr,*guiMsgRestore=nullptr;
    GUI *guiMainMenu=nullptr,*guiIntro=nullptr,*guiLoading=nullptr;
    bool mapSpawned=true,retainedPage=false;
    idSoundWorld *sw=nullptr,*menuSoundWorld=nullptr,*selected=nullptr;
    bool RetainedPageActive() const { return retainedPage; }
    void SetPlayingSoundWorld(idSoundWorld* world) { selected=world; }
    void SetPlayingSoundWorld();
    void ExitMenu();
    void ReturnSystemSettings() {}
    void PumpApplicationActions(GUI*) {}
};
'''
MAIN = r'''
int main() {
    idSoundWorld gameWorld,menuWorld;
    Voice localVoice,entityVoice,mutableVoice;
    idSoundChannel local{false,&localVoice},entity{false,&entityVoice},ambient{true,&mutableVoice},missing;
    Emitter localEmitter,entityEmitter;
    localEmitter.channels={&local}; entityEmitter.channels={&entity,&ambient,&missing};
    gameWorld.emitters={&localEmitter,&entityEmitter};
    soundSystemLocal.world=&gameWorld;
    idMultiplayerGame game;
    const int initial=gameWorld.GetSoundTime();
    for(int cycle=0;cycle<1000;++cycle) {
        game.UpdateArenaSoundPause(true);
        assert(game.arenaSoundPauseOwned && gameWorld.IsPaused());
        const int frozen=gameWorld.GetSoundTime();
        assert(localVoice.paused && entityVoice.paused && !mutableVoice.paused);
        soundSystemLocal.now+=5000;
        for(int repeat=0;repeat<4;++repeat) game.UpdateArenaSoundPause(true);
        assert(gameWorld.GetSoundTime()==frozen);
        assert(menuWorld.GetSoundTime()==soundSystemLocal.now);
        game.UpdateArenaSoundPause(false);
        game.UpdateArenaSoundPause(false);
        assert(!game.arenaSoundPauseOwned && !gameWorld.IsPaused());
        assert(gameWorld.GetSoundTime()==frozen);
        assert(!localVoice.paused && !entityVoice.paused);
        assert(localVoice.pauses==cycle+1 && localVoice.resumes==cycle+1);
        assert(entityVoice.pauses==cycle+1 && entityVoice.resumes==cycle+1);
        soundSystemLocal.now+=16;
        assert(gameWorld.GetSoundTime()==initial+(cycle+1)*16);
    }
    // An unrelated pause must never be acquired or released by the Arena menu.
    gameWorld.Pause();
    game.UpdateArenaSoundPause(true); game.UpdateArenaSoundPause(false);
    assert(!game.arenaSoundPauseOwned && gameWorld.IsPaused());
    gameWorld.UnPause();
    // Missing/recreated backend state cannot retain stale ownership.
    game.UpdateArenaSoundPause(true); soundSystemLocal.world=nullptr;
    game.UpdateArenaSoundPause(false); assert(!game.arenaSoundPauseOwned);
    soundSystemLocal.world=&gameWorld; gameWorld.UnPause();
    soundSystem=nullptr; game.UpdateArenaSoundPause(true); assert(!game.arenaSoundPauseOwned);
    soundSystem=&soundSystemLocal;
    idSessionLocal session; session.sw=&gameWorld; session.menuSoundWorld=&menuWorld;
    GUI arena{"guis/arena_pause.gui"},mp{"guis/mpmenu.gui"},settings{"guis/menu/settings/system.rui"};
    session.guiActive=&arena; session.SetPlayingSoundWorld(); assert(session.selected==&menuWorld);
    session.guiActive=&mp; session.SetPlayingSoundWorld(); assert(session.selected==&gameWorld);
    session.guiActive=nullptr; session.SetPlayingSoundWorld(); assert(session.selected==&gameWorld);
    session.guiActive=&settings; session.guiSystem=&settings;
    session.SetPlayingSoundWorld(); assert(session.selected==&menuWorld);
    session.guiSystem=nullptr; session.mapSpawned=false;
    session.SetPlayingSoundWorld(); assert(session.selected==&menuWorld);
    // Exercise the actual session exit too: its ordinary SP unpause must not
    // take ownership away from Arena or release an externally borrowed pause.
    session.mapSpawned=true; session.guiActive=&arena;
    gameWorld.Pause(); game.UpdateArenaSoundPause(true);
    session.ExitMenu();
    assert(session.guiActive==nullptr && session.selected==&gameWorld);
    game.UpdateArenaSoundPause(false); assert(gameWorld.IsPaused());
    gameWorld.UnPause();
    session.guiActive=&arena; game.UpdateArenaSoundPause(true);
    session.ExitMenu(); assert(gameWorld.IsPaused());
    game.UpdateArenaSoundPause(false); assert(!gameWorld.IsPaused());
    session.guiActive=&mp; gameWorld.Pause(); session.ExitMenu();
    assert(!gameWorld.IsPaused());
    std::puts("Arena sound: 1000 pause/resume cycles, frozen clock, slot-zero and entity VO, independent menu clock, borrowed pause and menu routing passed");
}
'''


def source() -> str:
    multiplayer = (ROOT / "src/mpgame/MultiplayerGame.cpp").read_text(encoding="utf-8")
    sound = (ROOT / "src/sound/snd_world.cpp").read_text(encoding="utf-8")
    session = (ROOT / "src/framework/Session.cpp").read_text(encoding="utf-8")
    menu = (ROOT / "src/framework/Session_menu.cpp").read_text(encoding="utf-8")
    assert "if ( arenaSoundRoute )" in function_body(menu, "void idSessionLocal::SetGUI(")
    assert "SetPlayingSoundWorld();" in function_body(menu, "void idSessionLocal::SetGUI(")
    assert "ExitMenu();" in function_body(menu, "void idSessionLocal::HandleGameMenuReturn(")
    for signature in ("void idMultiplayerGame::Clear()", "void idMultiplayerGame::ClearMap("):
        assert "UpdateArenaSoundPause( false );" in function_body(multiplayer, signature)
    assert "UpdateArenaSoundPause( arenaPaused );" in function_body(multiplayer, "void idMultiplayerGame::Run(")
    assert "IsArenaPauseMenuOpen() ? SOUNDWORLD_MENU : SOUNDWORLD_GAME" in multiplayer
    result = SUPPORT + function_body(multiplayer, "void idMultiplayerGame::UpdateArenaSoundPause(")
    for signature in ("void idSoundWorldLocal::Pause()", "void idSoundWorldLocal::UnPause()", "int idSoundWorldLocal::GetSoundTime()"):
        result += "\n" + function_body(sound, signature)
    return result + "\n" + function_body(session, "void idSessionLocal::SetPlayingSoundWorld()") + \
        "\n" + function_body(menu, "void idSessionLocal::ExitMenu(") + MAIN


def main() -> None:
    if len(sys.argv) == 3 and sys.argv[1] == "--generate-native":
        Path(sys.argv[2]).write_text(source(), encoding="utf-8")
        return
    if len(sys.argv) != 1:
        raise SystemExit("usage: arena_sound_pause.py [--generate-native OUTPUT]")
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("C++17 compiler required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="arena-sound-", dir=ROOT / ".tmp") as directory:
        cpp, executable = Path(directory) / "test.cpp", Path(directory) / "test.exe"
        cpp.write_text(source(), encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
