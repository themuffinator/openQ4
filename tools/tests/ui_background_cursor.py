#!/usr/bin/env python3
"""Run production SDL menu routing/synchronization with counted OS calls."""

from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <cassert>
#include <cstdio>
#include <cstddef>
struct CVar { bool value = true; bool GetBool() const { return value; } };
struct { CVar in_mouse; bool activeApp = true; } win32;
using SDL_WindowFlags = unsigned;
constexpr unsigned SDL_WINDOW_HIDDEN = 8, SDL_WINDOW_MINIMIZED = 16;
struct SDL_Window { unsigned flags = 0; } window;
SDL_Window* s_sdlWindow = &window;
unsigned SDL_GetWindowFlags(SDL_Window* w) { return w->flags; }
unsigned reads = 0, warps = 0;
void SDL_GetMouseState(float* x, float* y) { ++reads; *x = 20; *y = 30; }
void SDL_WarpMouseInWindow(SDL_Window*, float, float) { ++warps; }
bool retainedOpen = false;
bool RetainedUI_IsOpen() { return retainedOpen; }
struct Session { bool active = true; bool IsGUIActive() { return active || retainedOpen; } } sessionObject;
Session* session = &sessionObject;
struct Console {
    bool active = false;
    bool Active() { return active; }
    void SetMousePosition(float, float) {}
} consoleObject;
Console* console = &consoleObject;
struct idUserInterface {
    float CursorX() { return 320; } float CursorY() { return 240; }
    void SetCursor(float, float) {}
} gui;
idUserInterface* SDL3_GetActiveMenuGui() { return &gui; }
struct sdl3GuiMouseTransform_t {
    float xScale = 1, yScale = 1, xOffset = 0, yOffset = 0;
    float drawAreaX = 0, drawAreaY = 0, drawAreaWidth = 640, drawAreaHeight = 480;
    float guiWidth = 640, guiHeight = 480, pixelToWindowX = 1, pixelToWindowY = 1;
};
bool SDL3_BuildGuiMouseTransform(sdl3GuiMouseTransform_t&) { return true; }
bool SDL3_MapWindowMouseToConsoleCursor(float x, float y, float& a, float& b) { a=x; b=y; return true; }
bool SDL3_MapWindowMouseToGuiCursor(float x, float y, float& a, float& b) { a=x; b=y; return true; }
void SDL3_SetMenuMouseTrackingPosition(float, float) {}
bool s_ignoreNextMenuWarpMotion = false, s_menuMouseInsideWindow = false;
float s_menuWarpWindowX = 0, s_menuWarpWindowY = 0;
'''

MAIN = r'''
int main() {
    // All combinations of focus, input enablement, hidden state and GUI/console
    // activation must avoid OS calls when the application does not own input.
    for (int focused=0; focused<2; ++focused)
    for (int enabled=0; enabled<2; ++enabled)
    for (int hidden=0; hidden<2; ++hidden)
    for (int menu=0; menu<2; ++menu)
    for (int consoleActive=0; consoleActive<2; ++consoleActive)
    for (int retained=0; retained<2; ++retained) {
        win32.activeApp = focused; win32.in_mouse.value = enabled;
        window.flags = hidden ? SDL_WINDOW_HIDDEN : 0;
        sessionObject.active = menu; consoleObject.active = consoleActive;
        retainedOpen = retained;
        reads=warps=0;
        SDL3_SyncSystemMouseToActiveCursor();
        const bool ownsInput = focused && enabled && !hidden && (consoleActive || (menu && !retained));
        assert((reads+warps) == (ownsInput ? 1u : 0u));
    }
    s_sdlWindow = nullptr;
    reads=warps=0;
    SDL3_SyncSystemMouseToActiveCursor();
    assert(reads == 0 && warps == 0);
    std::puts("background cursor: production routing passed 64 state combinations + no window; retained routing never warps or polls the cursor");
}
'''


TRACKING_SUPPORT = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
struct idMath { static float ClampFloat(float a, float b, float v) { return v < a ? a : v > b ? b : v; } };
static const int SDL3_MAX_MOUSE_DELTA_PER_EVENT = 32767;
// A menu GUI as the retained and stock ones behave: it adds each delta it
// accepts and clamps its cursor to the 640x480 screen it was given.
struct idUserInterface {
    float x = 320, y = 240; bool accepting = true;
    float CursorX() { return x; } float CursorY() { return y; }
    void Step(int dx, int dy) {
        if (!accepting) return;
        x = idMath::ClampFloat(0, 640, x + dx); y = idMath::ClampFloat(0, 480, y + dy);
    }
} gui;
idUserInterface* SDL3_GetActiveMenuGui() { return &gui; }
struct Console { bool active = false; bool Active() { return active; } } consoleObject;
Console* console = &consoleObject;
struct Delta { int dx, dy; };
std::vector<Delta> queue;
bool Sys_HasQueuedEvents() { return !queue.empty(); }
static bool s_haveAbsoluteMousePosition = false;
static bool s_haveMenuMousePosition = false;
static idUserInterface *s_menuMouseGui = NULL;
static float s_menuMouseX = 0, s_menuMouseY = 0, s_menuMouseRemainderX = 0, s_menuMouseRemainderY = 0;
'''

TRACKING_MAIN = r'''
// The pointer moves to (x, y) of the GUI's screen: one SDL motion event.
void Motion(float x, float y) {
    int dx = 0, dy = 0;
    SDL3_UpdateRoutedMouseDelta(x, y, dx, dy);
    if (dx || dy) queue.push_back({dx, dy});
}
// The event loop delivers everything queued.
void Drain() { for (const auto& d : queue) gui.Step(d.dx, d.dy); queue.clear(); }
bool At(float x, float y) { return std::fabs(gui.x - x) <= 1 && std::fabs(gui.y - y) <= 1; }
void Settle(float x, float y) { Motion(x, y); Drain(); }

int main() {
    // A screen opens centred: the first step lands the cursor on the pointer.
    Settle(100, 100); Settle(102, 101); assert(At(102, 101));
    // Several motion events before the GUI applies any count once each.
    Motion(110, 105); Motion(120, 110); Motion(130, 115); Drain(); assert(At(130, 115));
    // Steps the GUI ignored (its view loading, a hand-off) are not lost for
    // good: the next step after delivery lands on the pointer.
    gui.accepting = false;
    Settle(200, 150); Settle(260, 190);
    gui.accepting = true;
    Settle(262, 191); assert(At(262, 191));
    // The pointer leaves the screen and comes back: the clamp is not kept as
    // an offset short of the far edge.
    Settle(600, 191); Settle(700, 191); assert(At(640, 191));
    Settle(630, 191); assert(At(630, 191));
    Settle(630, 470); Settle(630, 560); Settle(630, 475); assert(At(630, 475));
    // Sub-unit motion never builds drift or jitters the cursor.
    for (int i = 0; i < 400; ++i) { const float t = 300 + i * 0.37f; Settle(t, t * 0.5f + 0.2f); assert(At(t, t * 0.5f + 0.2f)); }
    // The console keeps its own tracking: the GUI cursor never takes it over.
    consoleObject.active = true; gui.accepting = false; gui.x = 10;
    const float trackedX = s_menuMouseX;
    Settle(trackedX + 5, s_menuMouseY); assert(std::fabs(s_menuMouseX - (trackedX + 5)) < 0.01f);
    std::puts("background cursor: routed menu steps the GUI ignored or clamped are recovered on the next step");
}
'''


def main():
    source = (ROOT / 'src/sys/sdl3/sdl3_backend.cpp').read_text(encoding='utf-8')
    functions = '\n'.join(function_body(source, signature) for signature in (
        'static bool SDL3_OwnsSystemPointer(',
        'static bool SDL3_ShouldRouteMenuMouse(',
        'static void SDL3_SyncSystemMouseToActiveCursor(',
    ))
    compiler = next((found for name in ('clang++', 'g++', 'c++') if (found := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-cursor-', dir=ROOT / '.tmp') as temp:
        test_source = Path(temp) / 'cursor.cpp'
        test_source.write_text(SUPPORT + functions + MAIN, encoding='utf-8')
        for host in ('windows', 'posix'):
            binary = Path(temp) / f'cursor-{host}.exe'
            flags = ['-DOPENQ4_SDL3_POSIX_HOST'] if host == 'posix' else []
            subprocess.run([compiler, '-std=c++17', *flags, str(test_source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

        tracking = '\n'.join(function_body(source, signature) for signature in (
            'static int SDL3_ConsumeMouseDelta(',
            'static void SDL3_SetMenuMouseTrackingPosition(',
            'static bool SDL3_MenuCursorMissedSteps(',
            'static bool SDL3_UpdateRoutedMouseDelta(',
        ))
        resync = 'SDL3_MenuCursorMissedSteps(activeGui)'
        if resync not in tracking:
            raise AssertionError('routed menu steps must check the GUI cursor for missed steps')
        # The scenarios must catch tracking that keeps a missed step as an offset.
        for name, body, passes in (('tracking', tracking, True), ('tracking-kept-offset', tracking.replace(resync, 'false'), False)):
            test_source = Path(temp) / f'{name}.cpp'
            binary = Path(temp) / f'{name}.exe'
            test_source.write_text(TRACKING_SUPPORT + body + TRACKING_MAIN, encoding='utf-8')
            subprocess.run([compiler, '-std=c++17', str(test_source), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            if (result.returncode == 0) != passes:
                raise AssertionError(f'{name}: expected {"pass" if passes else "failure"}, got exit {result.returncode}\n{result.stdout}{result.stderr}')
            if passes:
                print(result.stdout.strip())


if __name__ == '__main__':
    main()
