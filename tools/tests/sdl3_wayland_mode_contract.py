#!/usr/bin/env python3
"""Pin the SDL 3.4.18 behaviour the strict display path and the shared
exclusive mode rule rely on.

SDL's Wayland driver emulates exclusive modes per window and never switches
the output. openQ4's strict window query therefore reads an exclusive window's
own fullscreen mode on native Wayland, and SYSTEM display descriptors keep the
desktop size while an exclusive mode resizes a display's bounds. This test
reads the pinned SDL source (the provisioned tree, or only these members of the
hashed archive) and fails when an SDL update changes a fact those rules rest
on. It compiles and runs nothing; no compositor is qualified.
"""
from __future__ import annotations

import argparse
import configparser
from pathlib import Path
import shutil
import tempfile

from sdl3_clipboard_status import body, provision_source

ROOT = Path(__file__).resolve().parents[2]
WRAP = ROOT / "subprojects/sdl3.wrap"
FILES = ["src/video/SDL_video.c", "src/video/wayland/SDL_waylandvideo.c", "src/video/wayland/SDL_waylandwindow.c"]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sdl_facts(video: str, wayland: str, window: str) -> None:
    # Every Wayland device emulates mode switching.
    create = body(wayland, "static SDL_VideoDevice *Wayland_CreateDevice(")
    require("device->device_caps = VIDEO_DEVICE_CAPS_MODE_SWITCHING_EMULATED |" in create,
            "Wayland devices no longer emulate mode switching unconditionally")
    # Setting a display mode under emulation (other than x11) never changes the
    # display's current mode.
    setter = body(video, "bool SDL_SetDisplayModeForDisplay(")
    early = setter.find('if (SDL_ModeSwitchingEmulated(_this) && SDL_strcmp(_this->name, "x11") != 0) {\n        return true;')
    require(early >= 0 and early < setter.find("SDL_SetCurrentDisplayMode(display, mode);"),
            "SDL_SetDisplayModeForDisplay no longer returns before switching an emulated display")
    # A fullscreen window reports the mode it uses.
    fullscreen = body(video, "const SDL_DisplayMode *SDL_GetWindowFullscreenMode(")
    require("if (window->flags & SDL_WINDOW_FULLSCREEN) {\n        return SDL_GetFullscreenModeMatch(&window->current_fullscreen_mode);" in fullscreen,
            "SDL_GetWindowFullscreenMode no longer reports the fullscreen window's current mode")
    # An exclusive window's buffer is that mode's size.
    buffer = body(window, "static void GetBufferSize(")
    require("if (data->is_fullscreen && window->fullscreen_exclusive) {\n        buf_width = window->current_fullscreen_mode.w;" in buffer,
            "GetBufferSize no longer sizes an exclusive window from its fullscreen mode")
    # A focused exclusive window's mode stands in for its display's bounds.
    bounds = body(wayland, "static bool Wayland_GetDisplayBounds(SDL_VideoDevice *_this, SDL_VideoDisplay *display, SDL_Rect *rect)\n{")
    require("rect->w = display->fullscreen_window->current_fullscreen_mode.w;" in bounds,
            "Wayland_GetDisplayBounds no longer reports a focused exclusive window's mode")
    # SDL's closest mode aims refresh 0 at the desktop rate, and the desktop
    # mode stays put while a fullscreen window is active: Auto's target.
    closest = body(video, "bool SDL_GetClosestFullscreenDisplayMode(")
    require("if (refresh_rate == 0.0f) {\n        refresh_rate = display->desktop_mode.refresh_rate;" in closest,
            "SDL_GetClosestFullscreenDisplayMode no longer aims refresh 0 at the desktop rate")
    desktop = body(video, "void SDL_SetDesktopDisplayMode(")
    require(desktop.find("if (display->fullscreen_active) {") >= 0 and
            desktop.find("if (display->fullscreen_active) {") < desktop.find("SDL_copyp(&display->desktop_mode") ,
            "SDL_SetDesktopDisplayMode no longer keeps the desktop mode while a fullscreen window is active")
    # Native Wayland is preferred over XWayland only with the fifo protocol.
    require('SDL_strcmp(interface, "wp_fifo_manager_v1") == 0' in wayland and "static bool Wayland_IsPreferred(" in wayland,
            "SDL's Wayland preference no longer depends on wp_fifo_manager_v1")


def openq4_rules() -> None:
    backend = (ROOT / "src/sys/sdl3/sdl3_backend.cpp").read_text(encoding="utf-8")
    observed = body(backend, "static const SDL_DisplayMode *SDL3_StrictObservedMode(")
    require("if (SDL3_IsNativeWaylandVideoDriver() && state.fullscreen && !state.fullscreenDesktop) {" in observed and
            "SDL_GetWindowFullscreenMode(s_sdlWindow)" in observed and "return SDL_GetCurrentDisplayMode(state.displayId);" in observed,
            "the strict query no longer reads the exclusive window's mode on native Wayland only")
    query = body(backend, "static bool SDL3_WindowServices_QueryWindowState(")
    require("const SDL_DisplayMode *mode = SDL3_StrictObservedMode(state);" in query,
            "the strict query no longer observes its mode through SDL3_StrictObservedMode")
    display = (ROOT / "src/ui/application/SystemDisplay.cpp").read_text(encoding="utf-8")
    # The client implementation follows the dedicated server's stubs.
    capture = body(display[display.index("#else"):], "bool CaptureDisplayTopology(")
    require("display.width=desktopMode->w; display.height=desktopMode->h;" in capture,
            "display descriptors no longer keep the desktop size")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdl-source", type=Path, help="Existing SDL source tree; an explicit missing tree is an error")
    args = parser.parse_args()
    wrap = configparser.ConfigParser()
    wrap.read(WRAP, encoding="utf-8")
    if wrap["wrap-file"]["directory"] != "SDL3-3.4.18":
        raise RuntimeError("Review these SDL facts for the new SDL version, then update the pinned directory here")
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="sdl3-wayland-mode-", dir=temporary))
    try:
        source, provenance, _ = provision_source(args.sdl_source, dict(wrap["wrap-file"]), scratch, files=FILES)
        video, wayland, window = [(source / name).read_text(encoding="utf-8") for name in FILES]
        sdl_facts(video, wayland, window)
        openq4_rules()
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    print(f"SDL Wayland mode contract: emulated exclusive modes, window mode, buffer and bounds facts pinned ({provenance['kind']})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
