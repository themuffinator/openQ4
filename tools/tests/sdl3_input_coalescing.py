#!/usr/bin/env python3
"""Run the production SDL3 input coalescing (HF-09) against a model SDL queue.

Compiles SDL3_JoinMouseMotionRun, SDL3_MouseMotionReport and
SDL3_ConsumeMouseDelta from src/sys/sdl3/sdl3_backend.cpp, with the join rule
in src/sys/MouseMotionRun.h, against a stand-in event queue that peeks and
takes the way SDL 3.4's SDL_PeepEvents does (the poll sentinel is skipped by a
peek and ends a poll). Floods of reports from an 8 kHz mouse, interleaved
presses, other windows and devices, non-finite reports, fractional motion and
the per-run bounds are each pumped twice: once joined, as the pump now runs,
and once report by report, as it ran before. Joined pumps must deliver the
same motion in the same order relative to every other event, in far fewer
entries. The per-poll-cycle controller sample guard is exercised as well, and
the pump's wiring of both is pinned. Mutants of the join rule and of the guard
must be rejected. No engine, window or device input is involved.
"""

from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body, require_order

ROOT = Path(__file__).resolve().parents[2]
BACKEND = ROOT / "src/sys/sdl3/sdl3_backend.cpp"
RULE = ROOT / "src/sys/MouseMotionRun.h"

SUPPORT = r'''
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>
#include "MouseMotionRun.h"
using Uint8 = std::uint8_t; using Uint32 = std::uint32_t; using Uint64 = std::uint64_t;
using SDL_WindowID = Uint32; using SDL_MouseID = Uint32; using SDL_JoystickID = Uint32;
using SDL_MouseButtonFlags = Uint32;
enum : Uint32 { SDL_EVENT_FIRST = 0, SDL_EVENT_KEY_DOWN = 0x300, SDL_EVENT_MOUSE_MOTION = 0x400,
    SDL_EVENT_MOUSE_BUTTON_DOWN = 0x401, SDL_EVENT_POLL_SENTINEL = 0x7F00, SDL_EVENT_LAST = 0xFFFF };
enum SDL_EventAction { SDL_ADDEVENT, SDL_PEEKEVENT, SDL_GETEVENT };
struct SDL_MouseMotionEvent { Uint32 type; Uint32 reserved; Uint64 timestamp; SDL_WindowID windowID; SDL_MouseID which;
    SDL_MouseButtonFlags state; float x, y, xrel, yrel; };
union SDL_Event { Uint32 type; SDL_MouseMotionEvent motion; Uint8 padding[128]; };
static unsigned checks = 0;
static void Check(bool value, const char *message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
// SDL 3.4's queue as SDL_PeepEvents and SDL_PollEvent see it: one consumer.
static std::deque<SDL_Event> sdlQueue;
static int SDL_PeepEvents(SDL_Event *events, int count, SDL_EventAction action, Uint32 minType, Uint32 maxType) {
    Check(count == 1 && events != nullptr, "one event at a time");
    for (auto entry = sdlQueue.begin(); entry != sdlQueue.end(); ++entry) {
        if (entry->type < minType || entry->type > maxType) continue;
        if (entry->type == SDL_EVENT_POLL_SENTINEL) continue; // the public call never returns it
        *events = *entry;
        if (action == SDL_GETEVENT) sdlQueue.erase(entry);
        return 1;
    }
    return 0;
}
// SDL_PollEvent: the next event; the poll sentinel ends the cycle.
static bool Poll(SDL_Event &event) {
    if (sdlQueue.empty()) return false;
    event = sdlQueue.front(); sdlQueue.pop_front();
    return event.type != SDL_EVENT_POLL_SENTINEL;
}
struct idMath { static float ClampFloat(float lo, float hi, float v) { return v < lo ? lo : (v > hi ? hi : v); } };
static const int SDL3_MAX_MOUSE_DELTA_PER_EVENT = 32767;
static std::uint64_t s_mouseMotionReports = 0, s_mouseMotionReportsJoined = 0;
static unsigned s_sdlPollCycle = 0, s_controllerAxesCycle = 0;
static SDL_JoystickID s_controllerAxesDevice = 0;
static std::uint64_t s_controllerAxisSamples = 0;
'''

MAIN = r'''
static SDL_Event Motion(float xrel, float yrel, Uint32 window = 7, Uint32 device = 1, Uint64 time = 0) {
    static Uint64 clock = 1000;
    SDL_Event event{}; event.motion.type = SDL_EVENT_MOUSE_MOTION; event.motion.timestamp = time ? time : (clock += 125000);
    event.motion.windowID = window; event.motion.which = device;
    static float x = 400, y = 300; x += xrel; y += yrel;
    event.motion.x = x; event.motion.y = y; event.motion.xrel = xrel; event.motion.yrel = yrel;
    return event;
}
static SDL_Event Other(Uint32 type) { SDL_Event event{}; event.type = type; return event; }
static SDL_Event Sentinel() { return Other(SDL_EVENT_POLL_SENTINEL); }
// What one pump delivers: every event in order, a motion entry as its deltas.
struct Delivery { Uint32 type; int dx, dy; Uint32 window; float x, y; };
struct Pumped { std::vector<Delivery> out; int reports = 0; };
static Pumped Pump(const std::vector<SDL_Event> &events, bool join) {
    sdlQueue.assign(events.begin(), events.end());
    Pumped pumped; float remainderX = 0, remainderY = 0;
    SDL_Event event;
    while (Poll(event)) {
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            pumped.reports += join ? SDL3_JoinMouseMotionRun(event.motion) : 1;
            const int dx = SDL3_ConsumeMouseDelta(event.motion.xrel, remainderX);
            const int dy = SDL3_ConsumeMouseDelta(event.motion.yrel, remainderY);
            pumped.out.push_back({event.type, dx, dy, event.motion.windowID, event.motion.x, event.motion.y});
        } else {
            pumped.out.push_back({event.type, 0, 0, 0, 0, 0});
        }
    }
    return pumped;
}
// Folds per-report deliveries into what the joined pump should show: one
// entry per run of motion of one window, other events in place.
static void SameOrderAndMotion(const Pumped &joined, const Pumped &single, int slack, const char *label) {
    size_t j = 0;
    for (size_t s = 0; s < single.out.size();) {
        Check(j < joined.out.size(), label);
        const Delivery &want = single.out[s];
        if (want.type != SDL_EVENT_MOUSE_MOTION) {
            Check(joined.out[j].type == want.type, label); ++j; ++s; continue;
        }
        // a stretch of motion from one window: the joined entries cover it exactly
        long long wantX = 0, wantY = 0, gotX = 0, gotY = 0;
        size_t end = s;
        while (end < single.out.size() && single.out[end].type == SDL_EVENT_MOUSE_MOTION && single.out[end].window == want.window) {
            wantX += single.out[end].dx; wantY += single.out[end].dy; ++end;
        }
        const float lastX = single.out[end - 1].x, lastY = single.out[end - 1].y;
        size_t runs = 0;
        while (j < joined.out.size() && joined.out[j].type == SDL_EVENT_MOUSE_MOTION && joined.out[j].window == want.window) {
            gotX += joined.out[j].dx; gotY += joined.out[j].dy; ++j; ++runs;
            Check(std::abs(joined.out[j - 1].dx) <= 32767 && std::abs(joined.out[j - 1].dy) <= 32767, "a run never meets the per-event clamp");
        }
        Check(runs >= 1 && runs <= end - s, label);
        Check(std::llabs(gotX - wantX) <= slack && std::llabs(gotY - wantY) <= slack, label);
        Check(joined.out[j - 1].x == lastX && joined.out[j - 1].y == lastY, "a run ends at its last report's position");
        s = end;
    }
    Check(j == joined.out.size(), label);
}
static std::vector<SDL_Event> Flood(int reports, float xrel, float yrel) {
    std::vector<SDL_Event> events;
    for (int i = 0; i < reports; ++i) events.push_back(Motion(xrel, yrel));
    events.push_back(Sentinel());
    return events;
}
int main() {
    // An 8 kHz mouse over a 125 ms frame: 1000 reports become one entry.
    {
        const auto events = Flood(1000, 3.0f, -2.0f);
        const Pumped joined = Pump(events, true), single = Pump(events, false);
        Check(joined.reports == 1000 && joined.out.size() == 1, "one frame's flood is one run");
        Check(joined.out[0].dx == 3000 && joined.out[0].dy == -2000, "the run carries every count");
        SameOrderAndMotion(joined, single, 0, "flood");
    }
    // The per-run bound keeps each entry inside the per-event clamp: a fast
    // flick through a stalled frame loses nothing. 409 reports of 40 counts
    // fit 16384, so 2000 make five runs.
    {
        const auto events = Flood(2000, 40.0f, 25.0f);
        const Pumped joined = Pump(events, true), single = Pump(events, false);
        Check(joined.out.size() == 5, "80000 counts split below the clamp");
        SameOrderAndMotion(joined, single, 0, "bounded flood");
        long long totalX = 0, totalY = 0;
        for (const auto &d : joined.out) { totalX += d.dx; totalY += d.dy; }
        Check(totalX == 80000 && totalY == 50000, "the bounded runs add up to the whole motion");
    }
    // A run holds at most MOUSE_MOTION_RUN_MAX_REPORTS reports.
    {
        const auto events = Flood(5000, 1.0f, 0.0f);
        const Pumped joined = Pump(events, true), single = Pump(events, false);
        Check(joined.out.size() == 2, "the report bound splits a long run");
        SameOrderAndMotion(joined, single, 0, "report bound");
    }
    // Presses, keys and wheel steps keep their place among the motion.
    {
        std::vector<SDL_Event> events;
        for (int i = 0; i < 100; ++i) events.push_back(Motion(1, 1));
        events.push_back(Other(SDL_EVENT_MOUSE_BUTTON_DOWN));
        for (int i = 0; i < 100; ++i) events.push_back(Motion(-2, 0));
        events.push_back(Other(SDL_EVENT_KEY_DOWN));
        for (int i = 0; i < 50; ++i) events.push_back(Motion(0, 4));
        events.push_back(Sentinel());
        const Pumped joined = Pump(events, true), single = Pump(events, false);
        Check(joined.out.size() == 5, "three runs between two other events");
        Check(joined.out[1].type == SDL_EVENT_MOUSE_BUTTON_DOWN && joined.out[3].type == SDL_EVENT_KEY_DOWN, "other events in place");
        Check(joined.out[0].dx == 100 && joined.out[2].dx == -200 && joined.out[4].dy == 200, "each run's motion before its event");
        SameOrderAndMotion(joined, single, 0, "interleaved");
    }
    // Another window or device ends a run; so does a report that is not finite.
    {
        std::vector<SDL_Event> events;
        for (int i = 0; i < 10; ++i) events.push_back(Motion(1, 0, 7, 1));
        for (int i = 0; i < 10; ++i) events.push_back(Motion(1, 0, 9, 1));
        for (int i = 0; i < 10; ++i) events.push_back(Motion(1, 0, 9, 2));
        // finite motion at a position that is not: the pump drops such a report alone
        SDL_Event broken = Motion(1, 0, 9, 2); broken.motion.x = std::nanf(""); events.push_back(broken);
        for (int i = 0; i < 10; ++i) events.push_back(Motion(1, 0, 9, 2));
        events.push_back(Sentinel());
        const Pumped joined = Pump(events, true);
        Check(joined.out.size() == 5, "window, device and non-finite reports each break the run");
        Check(joined.out[0].window == 7 && joined.out[1].window == 9, "window runs in order");
        Check(joined.reports == 41, "every report was seen once");
        sdlQueue.clear();
    }
    // Fractional motion (scaled or high-resolution pointers) joins too.
    {
        const auto events = Flood(1000, 0.3f, -0.7f);
        const Pumped joined = Pump(events, true), single = Pump(events, false);
        Check(joined.out.size() == 1, "fractional reports join");
        SameOrderAndMotion(joined, single, 1, "fractional flood");
    }
    // A report the raw input thread queued behind this cycle's sentinel joins
    // and is handled a frame sooner; nothing else crosses the sentinel.
    {
        std::vector<SDL_Event> events = { Motion(5, 0), Sentinel(), Motion(6, 0), Other(SDL_EVENT_KEY_DOWN), Motion(1, 0) };
        sdlQueue.assign(events.begin(), events.end());
        SDL_Event event; Check(Poll(event) && event.type == SDL_EVENT_MOUSE_MOTION, "first report");
        Check(SDL3_JoinMouseMotionRun(event.motion) == 2 && event.motion.xrel == 11.0f, "the report behind the sentinel joins");
        Check(!Poll(event), "the cycle still ends at its sentinel");
        Check(Poll(event) && event.type == SDL_EVENT_KEY_DOWN, "the key waits for the next cycle, in order");
        Check(Poll(event) && event.type == SDL_EVENT_MOUSE_MOTION && SDL3_JoinMouseMotionRun(event.motion) == 1, "then its motion");
        sdlQueue.clear();
    }
    // The controller guard: one sample per device per poll cycle, again after a clear.
    {
        ++s_sdlPollCycle;
        Check(!SDL3_ControllerAxesCurrent(3), "a new cycle samples");
        SDL3_NoteControllerAxesSampled(3);
        Check(SDL3_ControllerAxesCurrent(3), "the rest of the cycle reads the same state");
        Check(!SDL3_ControllerAxesCurrent(4), "another active device samples");
        Check(!SDL3_ControllerAxesCurrent(0), "no device is never current");
        s_controllerAxesDevice = 0; // SDL3_ClearJoystickAxisStateUnlocked
        Check(!SDL3_ControllerAxesCurrent(3), "cleared axes sample again");
        SDL3_NoteControllerAxesSampled(3);
        ++s_sdlPollCycle;
        Check(!SDL3_ControllerAxesCurrent(3), "the next cycle samples again");
        Check(s_controllerAxisSamples == 2, "samples are counted");
    }
    std::printf("PASS %u coalescing checks; %llu reports, %llu joined\n", checks,
        static_cast<unsigned long long>(s_mouseMotionReports), static_cast<unsigned long long>(s_mouseMotionReportsJoined));
}
'''


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def production() -> str:
    source = BACKEND.read_text(encoding="utf-8")
    parts = [
        function_body(source, "static int SDL3_ConsumeMouseDelta(float delta, float &remainder) {"),
        function_body(source, "static openq4::MouseMotionReport SDL3_MouseMotionReport(const SDL_MouseMotionEvent &motion) {"),
        function_body(source, "static int SDL3_JoinMouseMotionRun(SDL_MouseMotionEvent &motion) {"),
        function_body(source, "static bool SDL3_ControllerAxesCurrent(SDL_JoystickID device) {"),
        function_body(source, "static void SDL3_NoteControllerAxesSampled(SDL_JoystickID device) {"),
    ]
    return SUPPORT + "\n".join(parts) + "\n" + MAIN


def validate_wiring() -> None:
    source = BACKEND.read_text(encoding="utf-8")
    pump = function_body(source, "bool Sys_SDL_PumpEvents(void) {")
    loop = pump.index("++s_sdlPollCycle;")
    if pump.find("while (SDL_PollEvent(&event))", loop) == -1:
        raise AssertionError("the poll cycle must advance before the main SDL event loop")
    motion = pump[pump.index("case SDL_EVENT_MOUSE_MOTION:"):pump.index("case SDL_EVENT_MOUSE_BUTTON_DOWN:")]
    require_order(motion, "SDL3_JoinMouseMotionRun(event.motion);", "SDL3_ConsumeMouseDelta(event.motion.xrel", "joined before consumed")
    require_order(motion, "SDL3_JoinMouseMotionRun(event.motion);", "SDL3_QueueRetainedPointer(event.motion.x", "joined before the retained pointer")
    gamepad = pump[pump.index("case SDL_EVENT_GAMEPAD_AXIS_MOTION:"):pump.index("case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:")]
    require_order(gamepad, "!SDL3_ControllerAxesCurrent(s_sdlGamepadId)", "SDL3_UpdateGamepadAxes(eventTime);", "gamepad axes sampled once a cycle")
    require_order(gamepad, "SDL3_UpdateGamepadAxes(eventTime);", "SDL3_GamepadAxisIsUse(source->pad, event.gaxis.axis)", "use is still judged per event")
    joystick = pump[pump.index("case SDL_EVENT_JOYSTICK_AXIS_MOTION:"):pump.index("case SDL_EVENT_FINGER_DOWN:")]
    require_order(joystick, "!SDL3_ControllerAxesCurrent(s_sdlJoystickId)", "SDL3_UpdateJoystickAxes();", "joystick axes sampled once a cycle")
    if "s_controllerAxesDevice = 0;" not in function_body(source, "static void SDL3_ClearJoystickAxisStateUnlocked(void) {"):
        raise AssertionError("clearing the axes must invalidate the cycle's sample")
    if "SDL3_NoteControllerAxesSampled(s_sdlGamepadId);" not in function_body(source, "static void SDL3_UpdateGamepadAxes(int eventTime) {"):
        raise AssertionError("a gamepad sample must be recorded")
    if "SDL3_NoteControllerAxesSampled(s_sdlJoystickId);" not in function_body(source, "static void SDL3_UpdateJoystickAxes(void) {"):
        raise AssertionError("a joystick sample must be recorded")
    area = function_body(source, "static void SDL3_UpdateTextInputArea(void) {")
    require_order(area, "SDL_memcmp(&s_textInputArea, &textArea, sizeof(textArea)) == 0", "SDL_SetTextInputArea(s_sdlWindow", "IME area set only on change")
    if "s_textInputAreaWindow = NULL;" not in function_body(source, "static void SDL3_UpdateTextInputState(void) {"):
        raise AssertionError("a new text input session must be given its area")


def main() -> None:
    validate_wiring()
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    scratch = ROOT / ".tmp"
    scratch.mkdir(exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="sdl3-input-coalescing-", dir=scratch))
    env = dict(os.environ, TEMP=str(directory), TMP=str(directory), TMPDIR=str(directory))
    rule = RULE.read_text(encoding="utf-8")
    program = production()
    mutants = {
        "production": (rule, program),
        "join-ignores-window": (rule.replace("next.window != run.window || ", ""), program),
        "join-ignores-device": (rule.replace(" || next.device != run.device", ""), program),
        "join-unbounded-counts": (rule.replace("<= MOUSE_MOTION_RUN_MAX_COUNTS &&", "<= 1.0e9f &&").replace(
            "<= MOUSE_MOTION_RUN_MAX_COUNTS;", "<= 1.0e9f;"), program),
        "join-unbounded-reports": (rule.replace("reports >= MOUSE_MOTION_RUN_MAX_REPORTS", "false"), program),
        "join-drops-motion": (rule.replace("run.xrel += next.xrel;", ""), program),
        "join-keeps-first-position": (rule.replace("run.x = next.x;", ""), program),
        "join-accepts-nonfinite": (rule.replace("!MouseMotionReportFinite( run ) || !MouseMotionReportFinite( next )", "false"), program),
        "guard-ignores-cycle": (rule, program.replace("&& s_controllerAxesCycle == s_sdlPollCycle", "")),
        "guard-ignores-device": (rule, program.replace("s_controllerAxesDevice == device &&", "")),
    }
    results = {"scope": "production SDL3 coalescing functions against a model SDL 3.4 queue; no engine, window or input",
               "files": {path.relative_to(ROOT).as_posix(): digest(path) for path in (BACKEND, RULE, Path(__file__))},
               "cases": {}}
    for name, (header, source) in mutants.items():
        if name != "production" and header == rule and source == program:
            raise AssertionError(f"mutant {name} changed nothing")
        case = directory / name
        case.mkdir()
        (case / "MouseMotionRun.h").write_text(header, encoding="utf-8", newline="\n")
        cpp, exe = case / "coalescing.cpp", case / "coalescing.exe"
        cpp.write_text(source, encoding="utf-8", newline="\n")
        command = [compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-I", str(case), str(cpp), "-o", str(exe)]
        compiled = subprocess.run(command, text=True, capture_output=True, env=env)
        if compiled.returncode:
            raise AssertionError(f"{name} compilation failed: {compiled.stdout}{compiled.stderr}")
        ran = subprocess.run([str(exe)], text=True, capture_output=True, env=env, timeout=120)
        if (ran.returncode == 0) != (name == "production"):
            raise AssertionError(f"{name} unexpected exit {ran.returncode}: {ran.stdout}{ran.stderr}")
        results["cases"][name] = {"run_exit": ran.returncode, "output": (ran.stdout + ran.stderr).strip(),
                                  "source_sha256": digest(cpp)}
    (directory / "result.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(results["cases"]["production"]["output"])
    print(f"PASS: {len(mutants) - 1} coalescing mutants rejected; pump wiring pinned")
    print("Evidence: " + str(directory / "result.json"))


if __name__ == "__main__":
    main()
