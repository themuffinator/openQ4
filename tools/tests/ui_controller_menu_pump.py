#!/usr/bin/env python3
"""Compile the session's controller menu pump against counted doubles.

The pump turns a held D-pad or face button, or a deflected analog stick, into
repeated menu key presses. Retained views pair every press with its release
and repeat a press until it is released, and the stick never sends a release,
so the pump completes each step on any retained input target: the home screen
over the legacy menu, the multiplayer card over the game's menu, or an active
retained document such as the SYSTEM page. Legacy GUIs keep receiving presses
alone, as before.

The right stick's pump hands its deflection past a rest zone to the retained
view that takes the input, timed from the previous frame of the same push, so
the view scrolls its detail area and turns its model preview at a rate that
follows the deflection. Inverted look does not invert the menus, and nothing
reaches a legacy GUI or a page while a change between levels holds the input.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
enum { SE_KEY = 1 };
struct sysEvent_t { int evType = 0; int evValue = 0; int evValue2 = 0; int evValue3 = 0; int evPtrLength = 0; void* evPtr = nullptr; int inputDevice = 0; };
enum { K_JOY1 = 200, K_JOY2, K_JOY3, K_JOY4, K_JOY5, K_JOY6, K_JOY7, K_JOY8, K_JOY9, K_JOY10, K_JOY11, K_JOY12 };
enum { AXIS_SIDE, AXIS_FORWARD, AXIS_UP, AXIS_ROLL, AXIS_YAW, AXIS_PITCH };
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)
static int now = 1000;
struct Common { int GetPresentationTime() const { return now; } } commonObject, *common = &commonObject;
static bool held[512];
struct idKeyInput { static bool IsDown(int key) { return held[key]; } };
static int stickYaw = 0, stickPitch = 0, lookSide = 0, lookForward = 0;
static bool Sys_GetJoystickAxisState(int axis, int& value) {
    value = axis == AXIS_YAW ? stickYaw : axis == AXIS_PITCH ? stickPitch : axis == AXIS_SIDE ? lookSide : axis == AXIS_FORWARD ? lookForward : 0;
    return true;
}
struct idMath { static float ClampFloat(float lo, float hi, float v) { return v < lo ? lo : v > hi ? hi : v; } };
template <typename T> static T Min(T a, T b) { return a < b ? a : b; }
static bool invertLook = false;
struct CVarSystem { bool GetCVarBool(const char* name) const { return std::strcmp(name, "in_joystickInvertLook") == 0 && invertLook; } } cvarObject, *cvarSystem = &cvarObject;
static bool UI_IsRetainedPath(const char* qpath) { const std::string p = qpath ? qpath : ""; return p.size() >= 5 && p.compare(p.size() - 5, 5, ".q4ui") == 0; }
struct idUserInterface { std::string name; explicit idUserInterface(const char* n) : name(n) {} const char* Name() const { return name.c_str(); } };
struct Delivered { int key, down; };
struct Stick { idUserInterface* gui; float x, y, seconds; };
static std::vector<Stick> sticks;
struct idSessionLocal {
    idUserInterface *guiActive = nullptr, *guiMainMenu = nullptr, *guiRetainedHome = nullptr;
    idUserInterface *guiRetainedMultiplayer = nullptr, *retainedSubpageFrom = nullptr;
    // Whether the retained multiplayer card covers the game's menu (guiActive).
    bool cardCovers = false, homeBlocked = false;
    std::vector<Delivered> events;
    void MenuEvent(const sysEvent_t* e) { events.push_back({e->evValue, e->evValue2}); }
    bool RetainedHomePresenting() const { return guiRetainedHome != nullptr; }
    bool RetainedMultiplayerCovers() const { return cardCovers; }
    bool RetainedHomeInputBlocked() const { return homeBlocked; }
    bool RetainedDetailStick(idUserInterface* gui, float x, float y, float seconds) { sticks.push_back({gui, x, y, seconds}); return true; }
    bool RetainedInputTarget() const;
    idUserInterface* RetainedStickTarget() const;
};
'''

MAIN = r'''
static int Downs(const idSessionLocal& s) { int n = 0; for (const auto& e : s.events) n += e.down; return n; }
int main() {
    idUserInterface legacy("guis/mainmenu.gui"), page("guis/menu/settings/system.q4ui"), home("guis/menu/title.q4ui");
    auto step = [](idSessionLocal& s, int ms) { now += ms; PumpControllerMenuNavigation(&s); };
    {   // The stick on a retained page: every step pairs its press and its release,
        // and centring the stick ends the steps with nothing left held.
        idSessionLocal s; s.guiActive = &page; s.guiMainMenu = &legacy; CHECK(s.RetainedInputTarget());
        stickPitch = 100; step(s, 0);
        CHECK(s.events.size() == 2 && s.events[0].down == 1 && s.events[1].down == 0 && s.events[0].key == s.events[1].key);
        step(s, 50); CHECK(s.events.size() == 2);
        step(s, 60); CHECK(s.events.size() == 4 && s.events[3].down == 0);
        stickPitch = 0; step(s, 200); step(s, 200);
        CHECK(s.events.size() == 4 && Downs(s) * 2 == int(s.events.size()));
    }
    {   // A held D-pad direction on a retained page: the pump waits out the initial
        // delay, as its own key event steps first, then repeats released steps.
        idSessionLocal s; s.guiActive = &page; s.guiMainMenu = &legacy;
        held[K_JOY10] = true; step(s, 0); CHECK(s.events.empty());
        step(s, 320); CHECK(s.events.size() == 2 && s.events[0].key == K_JOY10 && s.events[1].down == 0);
        held[K_JOY10] = false; step(s, 200); CHECK(s.events.size() == 2);
    }
    {   // The retained home screen over the legacy menu pairs as before.
        idSessionLocal s; s.guiActive = s.guiMainMenu = &legacy; s.guiRetainedHome = &home; CHECK(s.RetainedInputTarget());
        stickYaw = 100; step(s, 0); CHECK(s.events.size() == 2 && s.events[1].down == 0);
        stickYaw = 0; step(s, 0);
    }
    {   // A legacy page receives the press alone, as before.
        idSessionLocal s; s.guiActive = s.guiMainMenu = &legacy; CHECK(!s.RetainedInputTarget());
        stickPitch = -100; step(s, 0); CHECK(s.events.size() == 1 && s.events[0].down == 1);
        stickPitch = 0; step(s, 0);
    }
    {   // A retained page over a legacy menu with a retained home that does not cover it.
        idSessionLocal s; s.guiActive = &page; s.guiMainMenu = &legacy; s.guiRetainedHome = &home; CHECK(s.RetainedInputTarget());
        idSessionLocal none; CHECK(!none.RetainedInputTarget());
    }
    {   // Another legacy menu, such as the in-game one, while the home screen still
        // covers the main menu receives the press alone.
        idUserInterface game("guis/mpmain.gui");
        idSessionLocal s; s.guiActive = &game; s.guiMainMenu = &legacy; s.guiRetainedHome = &home; CHECK(!s.RetainedInputTarget());
        stickYaw = -100; step(s, 0); CHECK(s.events.size() == 1 && s.events[0].down == 1);
        stickYaw = 0; step(s, 0);
    }
    {   // The multiplayer card over the game's menu takes its input: the stick's
        // steps pair, so the card never holds a press the stick cannot release,
        // and centring the stick ends them.
        idUserInterface game("guis/mpmain.gui");
        idSessionLocal s; s.guiActive = &game; s.guiMainMenu = &legacy; s.cardCovers = true; CHECK(s.RetainedInputTarget());
        stickPitch = 100; step(s, 0);
        CHECK(s.events.size() == 2 && s.events[0].down == 1 && s.events[1].down == 0 && s.events[0].key == s.events[1].key);
        stickPitch = 0; step(s, 200); step(s, 200);
        CHECK(s.events.size() == 2 && Downs(s) * 2 == int(s.events.size()));
    }
    auto pump = [](idSessionLocal& s, int ms) { now += ms; PumpControllerDetailStick(&s); };
    auto near = [](float a, float b) { return a > b - 1e-4f && a < b + 1e-4f; };
    {   // The right stick on a retained page: the first frame of a push only
        // times it; later frames hand on the deflection and the frame's time.
        idSessionLocal s; s.guiActive = &page; s.guiMainMenu = &legacy; CHECK(s.RetainedStickTarget() == &page);
        sticks.clear(); lookForward = 127; pump(s, 0); CHECK(sticks.empty());
        pump(s, 16); CHECK(sticks.size() == 1 && sticks[0].gui == &page && near(sticks[0].y, 1) && sticks[0].x == 0 && near(sticks[0].seconds, .016f));
        // A stalled frame hands on at most 100 ms; a longer gap starts a new push.
        pump(s, 200); CHECK(sticks.size() == 2 && near(sticks[1].seconds, .1f));
        pump(s, 400); CHECK(sticks.size() == 2);
        pump(s, 10); CHECK(sticks.size() == 3 && near(sticks[2].seconds, .01f));
        // Inside the rest zone nothing moves, and the next push starts afresh.
        lookForward = 16; pump(s, 10); CHECK(sticks.size() == 3);
        lookForward = -127; pump(s, 10); CHECK(sticks.size() == 3);
        pump(s, 10); CHECK(sticks.size() == 4 && near(sticks[3].y, -1));
        // The rate follows the deflection past the rest zone, on each axis.
        lookForward = 0; lookSide = 16 + 111 / 2; pump(s, 10);
        CHECK(sticks.size() == 5 && near(sticks[4].x, 55.0f / 111) && sticks[4].y == 0);
        // Inverted look leaves the menus' sense alone.
        invertLook = true; lookSide = 0; lookForward = 127; pump(s, 300); pump(s, 10);
        CHECK(sticks.size() == 6 && near(sticks[5].y, -1));
        invertLook = false; lookForward = 0; pump(s, 10);
    }
    {   // Nothing reaches a legacy page, a page while the change between menu
        // levels holds the input, or a home screen whose hand-off holds it.
        sticks.clear(); lookForward = 127;
        idSessionLocal legacyOnly; legacyOnly.guiActive = legacyOnly.guiMainMenu = &legacy; CHECK(!legacyOnly.RetainedStickTarget());
        pump(legacyOnly, 300); pump(legacyOnly, 10);
        idSessionLocal changing; changing.guiActive = changing.retainedSubpageFrom = &page; CHECK(!changing.RetainedStickTarget());
        pump(changing, 300); pump(changing, 10);
        idSessionLocal blocked; blocked.guiActive = blocked.guiMainMenu = &legacy; blocked.guiRetainedHome = &home; blocked.homeBlocked = true;
        CHECK(!blocked.RetainedStickTarget()); pump(blocked, 300); pump(blocked, 10);
        CHECK(sticks.empty());
        // The home screen over the legacy menu and the multiplayer card over
        // the game's menu take it, as they take the menu input.
        blocked.homeBlocked = false; CHECK(blocked.RetainedStickTarget() == &home);
        pump(blocked, 300); pump(blocked, 10); CHECK(sticks.size() == 1 && sticks[0].gui == &home);
        idUserInterface game("guis/mpmain.gui"), card("guis/menu/mp_escape.q4ui");
        idSessionLocal covered; covered.guiActive = &game; covered.guiMainMenu = &legacy; covered.cardCovers = true; covered.guiRetainedMultiplayer = &card;
        CHECK(covered.RetainedStickTarget() == &card);
        pump(covered, 300); pump(covered, 10); CHECK(sticks.size() == 2 && sticks[1].gui == &card);
        lookForward = 0; pump(covered, 10);
    }
    std::printf("Controller menu pump: %d checks passed\n", checks);
}
'''


def main():
    menu_path = ROOT / 'src/framework/Session_menu.cpp'
    menu = menu_path.read_text(encoding='utf-8')
    constants = '\n'.join(line for line in menu.splitlines() if line.startswith('static const int MENU_CONTROLLER_'))
    repeat = re.search(r'typedef struct menuControllerRepeat_s \{.*?\} menuControllerRepeat_t;', menu, re.S).group(0)
    state = 'static menuControllerRepeat_t menuControllerRepeat = { 0, false, 0 };'
    detail_state = 'static int menuDetailStickTime = -1;'
    assert state in menu and detail_state in menu
    state += '\n' + detail_state
    bodies = [function_body(menu, signature) for signature in (
        'static int MenuControllerAbs(', 'static void ClearMenuControllerRepeatState(', 'static int GetHeldControllerMenuKey(',
        'static int GetAnalogControllerMenuKey(', 'static int GetControllerMenuNavigationKey(', 'static void PumpControllerMenuNavigation(',
        'bool idSessionLocal::RetainedInputTarget(', 'static float MenuDetailStickAxis(', 'static void PumpControllerDetailStick(',
        'idUserInterface *idSessionLocal::RetainedStickTarget(')]
    # Every retained input target pairs the pump's steps, not only the home screen.
    assert 'if ( session->RetainedInputTarget() ) {' in bodies[5]
    # The frame pumps the right stick beside the navigation.
    frame = function_body(menu, 'void idSessionLocal::GuiFrameEvents(')
    assert 'PumpControllerMenuNavigation( this );\n\t\tPumpControllerDetailStick( this );' in frame
    compiler = next((p for name in ('clang++', 'g++', 'c++') if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='ui-controller-menu-pump-', dir=ROOT / '.tmp'))
    source = output / 'pump.cpp'
    binary = output / 'pump.exe'
    source.write_text(SUPPORT + '\n'.join([constants, repeat, state] + bodies) + MAIN, encoding='utf-8')
    environment = dict(os.environ, TEMP=str(output), TMP=str(output))
    command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Wno-unused-function', str(source), '-o', str(binary)]
    compile_result = subprocess.run(command, env=environment, capture_output=True, text=True)
    run_result = subprocess.run([str(binary)], env=environment, capture_output=True, text=True) if compile_result.returncode == 0 else None
    result = {
        'passed': run_result is not None and run_result.returncode == 0,
        'sources': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (menu_path, Path(__file__))},
        'command': command,
        'compile': {'exit': compile_result.returncode, 'stdout': compile_result.stdout, 'stderr': compile_result.stderr},
        'run': None if run_result is None else {'exit': run_result.returncode, 'stdout': run_result.stdout, 'stderr': run_result.stderr},
    }
    (output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result['run'] or result['compile'], indent=2))
    if not result['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
