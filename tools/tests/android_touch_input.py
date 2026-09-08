#!/usr/bin/env python3
"""Exercise SigmaTouch's actual input body without Android or device input.

Only the Android-dependent startup block is omitted. The Portable callbacks,
mutex, queue, overflow handling and analog conversion come directly from the
production translation unit; the sinks record events in memory.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cxx",
        default=os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++") or shutil.which("cl"),
    )
    args = parser.parse_args()
    if not args.cxx:
        parser.error("a native C++ compiler is required (--cxx, CXX, or a developer shell)")
    root = Path(__file__).resolve().parents[2]
    source = (root / "mobile/game_interface.cpp").read_text(encoding="utf-8")
    body = source[source.index("enum\n{\n    EV_KEY") :]
    bridge_source = (root / "src/sys/android/android_sdl3.cpp").read_text(encoding="utf-8")
    localization_start = bridge_source.index('extern "C" const char *Quake4_LocalizeString(')
    localization = bridge_source[localization_start : bridge_source.index("\n}", localization_start) + 2]
    layout_source = (root / "mobile/touch_interface_quake4.cpp").read_text(encoding="utf-8")
    labels_start = layout_source.index("void TouchInterface::updateControlLabels()")
    labels = layout_source[labels_start : layout_source.index("\nvoid TouchInterface::newGLContext()", labels_start)]
    actions = sorted(set(re.findall(r"\bPORT_ACT_[A-Z0-9_]+\b", body)) - {"PORT_ACT_WEAP0", "PORT_ACT_WEAP10"})
    declarations = "enum { " + ", ".join(actions) + ", PORT_ACT_WEAP0=200, PORT_ACT_WEAP10=210 };\n"
    scancodes = sorted(set(re.findall(r"\bSDL_SCANCODE_[A-Z_]+\b", body)))
    declarations += "enum { " + ", ".join(scancodes) + " };\n"
    harness = r'''
#include <cassert>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "quake4_bridge.h"
enum { BUTTON_PRIMARY=1, LOOK_MODE_JOYSTICK=1 };
enum touchscreemode_t { TS_MENU=1, TS_GAME=2, TS_CONSOLE=4 };
static int axes[6], resets, mouseX, mouseY;
static bool buttons[QUAKE4_BTN_COUNT];
static std::vector<std::string> commands;
static std::vector<int> impulses;
extern "C" void Quake4_PostKey(int, int) {}
extern "C" void Quake4_PostChar(int) {}
extern "C" void Quake4_PostMouseDelta(int x, int y) { mouseX += x; mouseY += y; }
extern "C" void Quake4_PostMouseButton(int, int) {}
extern "C" void Quake4_PostJoystickAxis(int axis, int value) { axes[axis] = value; }
extern "C" void Quake4_PostCommand(const char *command) { commands.emplace_back(command); }
extern "C" void Quake4_TriggerImpulse(int impulse) { impulses.push_back(impulse); }
extern "C" void Quake4_PostButton(int button, int down) { buttons[button] = down != 0; }
extern "C" int Quake4_GetScreenMode() { return TS_CONSOLE; }
extern "C" void Quake4_ResetTouchState() { ++resets; memset(buttons, 0, sizeof(buttons)); }
void PortableMoveFwd(float);
void PortableMoveSide(float);
void MouseButton(int, int) {}
struct TestDictionary {
    int lookups = 0;
    std::string result;
    const char *GetString(const char *id) { ++lookups; result = std::string("localized:") + id; return result.c_str(); }
};
struct TestCommon {
    bool initialized = false;
    TestDictionary dictionary;
    bool IsInitialized() { return initialized; }
    TestDictionary *GetLanguageDict() { return &dictionary; }
};
static TestCommon *common = nullptr;
namespace touchcontrols {
    struct ControlSuper { std::string description; };
    struct TouchControls {
        std::vector<ControlSuper *> controls;
        std::vector<ControlSuper *> *getControls() { return &controls; }
    };
}
class TouchInterface {
public:
    touchcontrols::TouchControls *tcGameMain, *tcMouse;
    bool labelsLocalized = false;
    void updateControlLabels();
};
'''
    checks = r'''
int main() {
    // The external host creates controls before any engine initialization.
    touchcontrols::ControlSuper attack{"#str_200114"}, mouse{"#str_42911"}, custom{"custom"};
    touchcontrols::TouchControls gameControls{{&attack, &custom}}, mouseControls{{&mouse}};
    TouchInterface layout{&gameControls, &mouseControls};
    layout.updateControlLabels();
    assert(!layout.labelsLocalized && attack.description == "#str_200114");
    TestCommon engine;
    common = &engine;
    layout.updateControlLabels();
    assert(engine.dictionary.lookups == 0 && !layout.labelsLocalized);
    engine.initialized = true;
    layout.updateControlLabels();
    assert(layout.labelsLocalized && attack.description == "localized:#str_200114");
    assert(mouse.description == "localized:#str_42911" && custom.description == "custom");
    const int resolvedLookups = engine.dictionary.lookups;
    layout.updateControlLabels();
    assert(engine.dictionary.lookups == resolvedLookups);

    // More than the old eight-slot command ring must retain each command.
    for (int i=0; i<20; ++i) PortableCommand(("echo " + std::to_string(i)).c_str());
    assert(commands.empty());
    Quake4_DrainTouchInput();
    for (int i=0; i<20; ++i) assert(commands[i] == "echo " + std::to_string(i));

    const auto commandCount = commands.size();
    PortableCommand(std::string(COMMAND_MAX_LEN, 'x').c_str());
    Quake4_DrainTouchInput();
    assert(commands.size() == commandCount);

    PortableAction(1, PORT_ACT_ATTACK);
    assert(!buttons[QUAKE4_BTN_ATTACK]);
    Quake4_DrainTouchInput();
    assert(buttons[QUAKE4_BTN_ATTACK]);
    for (int i=0; i<EVENT_QUEUE_SIZE; ++i) PortableCommand("echo full");
    Quake4_DrainTouchInput();
    assert(resets == 1 && !buttons[QUAKE4_BTN_ATTACK]);

    PortableAction(1, PORT_ACT_FWD);
    PortableAction(1, PORT_ACT_BACK);
    PortableAction(0, PORT_ACT_BACK);
    Quake4_DrainTouchInput();
    assert(axes[QUAKE4_AXIS_PITCH] == 127);
    PortableAction(0, PORT_ACT_FWD);
    PortableMoveSide(NAN);
    PortableMoveFwd(20.0f);
    Quake4_DrainTouchInput();
    assert(axes[QUAKE4_AXIS_PITCH] == 127 && axes[QUAKE4_AXIS_YAW] == 0);
    PortableMoveFwd(-20.0f);
    Quake4_DrainTouchInput();
    assert(axes[QUAKE4_AXIS_PITCH] == -127);

    Quake4_ClearTouchInput();
    Quake4_DrainTouchInput();
    assert(axes[QUAKE4_AXIS_PITCH] == 0);
    for (int i=0; i<4; ++i) {
        PortableLookYaw(0, 0.25f / LOOK_MOUSE_YAW_SCALE);
        Quake4_DrainTouchInput();
    }
    assert(mouseX == -1);
    PortableLookYaw(0, NAN);
    PortableLookPitch(0, INFINITY);
    Quake4_DrainTouchInput();
    assert(mouseX == -1 && mouseY == 0);

    // Concurrent producers and a draining engine cannot lose fractional look.
    mouseX = 0;
    std::vector<std::thread> producers;
    for (int i=0; i<4; ++i) producers.emplace_back([] {
        for (int j=0; j<1000; ++j) PortableLookYaw(0, 0.25f / LOOK_MOUSE_YAW_SCALE);
    });
    for (int i=0; i<100; ++i) Quake4_DrainTouchInput();
    for (auto &thread : producers) thread.join();
    Quake4_DrainTouchInput();
    assert(mouseX == -1000);

    PortableAction(1, PORT_ACT_RELOAD);
    PortableAction(0, PORT_ACT_RELOAD);
    Quake4_DrainTouchInput();
    assert(impulses.size() == 1 && impulses[0] == 13);
    // Aliased controls must retain the action until its last source releases.
    PortableAction(1, PORT_ACT_ALT_ATTACK);
    PortableAction(1, PORT_ACT_ZOOM_IN);
    PortableAction(0, PORT_ACT_ALT_ATTACK);
    Quake4_DrainTouchInput();
    assert(buttons[QUAKE4_BTN_ZOOM]);
    PortableAction(0, PORT_ACT_ZOOM_IN);
    Quake4_DrainTouchInput();
    assert(!buttons[QUAKE4_BTN_ZOOM]);
    PortableAction(1, PORT_ACT_SPRINT);
    PortableAction(1, PORT_ACT_SPEED);
    PortableAction(1, PORT_ACT_SPEED); // repeat is not another held source
    PortableAction(0, PORT_ACT_SPRINT);
    Quake4_DrainTouchInput();
    assert(buttons[QUAKE4_BTN_SPEED]);
    PortableAction(0, PORT_ACT_SPEED);
    Quake4_DrainTouchInput();
    assert(!buttons[QUAKE4_BTN_SPEED]);
    PortableAction(1, PORT_ACT_JUMP);
    Quake4_ClearTouchInput();
    PortableAction(1, PORT_ACT_JUMP);
    Quake4_DrainTouchInput();
    assert(buttons[QUAKE4_BTN_MOVE_UP]);
    assert(PortableShowKeyboard());
    PortableSetAlwaysRun(true);
    Quake4_DrainTouchInput();
    assert(commands.back() == "set in_alwaysRun 1");
}
'''
    scratch = root / ".tmp/android-touch-input"
    scratch.mkdir(parents=True, exist_ok=True)
    unit = scratch / "touch_input_test.cpp"
    unit.write_text(harness + declarations + body + localization + labels + checks, encoding="utf-8")
    binary = scratch / ("touch_input_test.exe" if os.name == "nt" else "touch_input_test")
    if Path(args.cxx).name.lower() in ("cl", "cl.exe"):
        command = [args.cxx, "/nologo", "/std:c++17", "/EHsc", "/D_CRT_SECURE_NO_WARNINGS",
                   "/I" + str(root / "mobile"), str(unit), "/Fe:" + str(binary)]
    else:
        command = [args.cxx, "-std=c++17", "-D_CRT_SECURE_NO_WARNINGS", "-I" + str(root / "mobile"), str(unit), "-o", str(binary)]
        if os.name != "nt":
            command.append("-pthread")
    subprocess.run(command, cwd=scratch, check=True)
    subprocess.run([str(binary)], cwd=scratch, check=True)
    print("PASS: Android touch queue, overflow releases, concurrent look, aliased actions, analog bounds and deferred localization")


if __name__ == "__main__":
    main()
