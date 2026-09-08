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
constexpr unsigned SDL_WINDOW_HIDDEN = 8;
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


def main():
    source = (ROOT / 'src/sys/sdl3/sdl3_backend.cpp').read_text(encoding='utf-8')
    functions = '\n'.join(function_body(source, signature) for signature in (
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


if __name__ == '__main__':
    main()
