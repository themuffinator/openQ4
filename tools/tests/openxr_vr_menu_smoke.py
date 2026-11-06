#!/usr/bin/env python3
"""Change a VR setting from inside the headset, against the openQ4 OpenXR test
runtime (requires Pillow, a staged client built with OpenXR, and retail assets).

The runtime's scripted weapon hand opens the pause menu, then points and clicks
its way to Settings > Game Options, opens its Virtual Reality tab, and clicks
the Laser Sight value. The setting must change from the dot to the dot and
beam, which only happens if the tab opened and the pointer's click reached its
row. Nothing synthesises keyboard or mouse input: console commands write
marker files, and the runtime acts on them.

The pointer positions are fractions of the 16:9 virtual screen, measured on
its captures of the retained pages (ui_retained 1, the launch default).
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]
SCREEN_WIDTH_M = 3.0
SCREEN_ASPECT = 16.0 / 9.0


def canvas_to_screen(x: float, y: float) -> tuple[float, float]:
    """A point on a stock GUI's 640x480 canvas as 0..1 across the 16:9 virtual screen."""
    return (160.0 + 1.5 * x) / 1280.0, (1.5 * y) / 720.0


def hand_pointing_at(u: float, v: float) -> str:
    """A right-hand pose that points straight ahead at (u, v) on a screen hung 2.5 m ahead at eye height."""
    height = SCREEN_WIDTH_M / SCREEN_ASPECT
    return f"{(u - 0.5) * SCREEN_WIDTH_M:.3f} {1.6 + (0.5 - v) * height:.3f} 0 0 0 0"


# where to click
PAUSE_SETTINGS = (0.231, 0.723)                 # the retained pause menu's Settings row
NAV_GAME_OPTIONS = canvas_to_screen(105, 227)   # Settings' left-hand Game Options button
VR_TAB = (0.746, 0.116)                         # Game Options' VIRTUAL REALITY tab
LASER_VALUE = (0.707, 0.256)                    # the Laser Sight row's value on that tab
LASER_BEAM = (0.707, 0.356)                     # "Dot and Beam", third in the list it unfolds
SECTION_CLICKS = 1                              # the tab
LIST_CLICKS = 2                                 # the list, then its option


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 assets")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir",
                        help="build directory holding openq4_xr_test_runtime.json")
    parser.add_argument("--executable-name")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--language", default="english", help="sys_lang, to check that translations fit")
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    options = parser.parse_args(argv)
    from PIL import Image

    staged = options.runtime_dir.resolve()
    executable = options.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    manifest = (options.build_dir / "openq4_xr_test_runtime.json").resolve()
    if not (staged / executable).is_file() or not options.basepath.is_dir():
        parser.error("a staged client and installed Quake 4 assets are required")
    if not manifest.is_file():
        parser.error(f"the OpenXR test runtime is not built: {manifest}")
    verify_runtime_is_current(staged, executable)

    output = (options.output_dir or ROOT / ".tmp/openxr-vr-menu-smoke").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")
    profile = output / ( "sp-vulkan" if options.render_api == "vulkan" else "sp" )
    game = profile / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    for stale in list(game.glob("vr_marker_*.txt")) + list(profile.glob("xr_*.tga")) + list(profile.glob("xr_*.png")):
        stale.unlink()
    xr_log = profile / "xr.jsonl"
    xr_log.unlink(missing_ok=True)

    def marker(name: str) -> str:
        return str(game / f"vr_marker_{name}.txt")

    script = [
        f"when {marker('menu')} button left menu 1",
        f"when {marker('menu_up')} button left menu 0",
        f"when {marker('to_settings')} hand right {hand_pointing_at(*PAUSE_SETTINGS)}",
        f"when {marker('to_game')} hand right {hand_pointing_at(*NAV_GAME_OPTIONS)}",
        f"when {marker('to_section')} hand right {hand_pointing_at(*VR_TAB)}",
        f"when {marker('vr_section')} capture {profile / 'xr_vr_section'}",
        f"when {marker('to_laser')} hand right {hand_pointing_at(*LASER_VALUE)}",
        f"when {marker('laser_open')} capture {profile / 'xr_laser_open'}",
        f"when {marker('to_beam')} hand right {hand_pointing_at(*LASER_BEAM)}",
        f"when {marker('laser_set')} capture {profile / 'xr_laser_set'}",
    ]
    commands = [
        "waitMsec 3000", "echo VR_MENU_SMOKE_START",
        # the profile persists between runs: start from the default
        "vr_aimLaser 1",
        "condump vr_marker_menu.txt", "waitMsec 300", "condump vr_marker_menu_up.txt", "waitMsec 1500",
    ]

    def click(name: str) -> None:
        script.append(f"when {marker(name + '_down')} button right trigger 1")
        script.append(f"when {marker(name + '_up')} button right trigger 0")
        commands.extend([f"condump vr_marker_{name}_down.txt", "waitMsec 250", f"condump vr_marker_{name}_up.txt", "waitMsec 900"])

    commands += ["condump vr_marker_to_settings.txt", "waitMsec 900"]
    click("settings")
    commands += ["waitMsec 800", "condump vr_marker_to_game.txt", "waitMsec 900"]
    click("game")
    commands += ["waitMsec 800", "condump vr_marker_to_section.txt", "waitMsec 900"]
    for step in range(SECTION_CLICKS):
        click(f"section{step}")
    commands += ["condump vr_marker_vr_section.txt", "waitMsec 600", "condump vr_marker_to_laser.txt", "waitMsec 900"]
    click("laser")
    commands += ["condump vr_marker_laser_open.txt", "waitMsec 600", "condump vr_marker_to_beam.txt", "waitMsec 900"]
    click("beam")
    commands += ["condump vr_marker_laser_set.txt", "waitMsec 600",
                 "echo VR_MENU_LASER", "vr_aimLaser", "echo VR_MENU_SMOKE_COMPLETE", "quit"]
    (profile / "xr_script.txt").write_text("\n".join(script) + "\n", encoding="utf-8")
    (game / "openxr_vr_menu_smoke.cfg").write_text("\n".join(commands) + "\n", encoding="utf-8")

    launches = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
    launch = next(c for c in launches["configurations"] if c["name"] == "(SP) Main menu — GL")
    args = [a.replace("${workspaceFolder}", str(ROOT)) for a in launch["args"]]
    settings = {
        "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "r_hiddenWindow": "1", "r_windowWidth": "1280", "r_windowHeight": "720",
        "r_mode": "-1", "r_customWidth": "1280", "r_customHeight": "720",
        "r_swapInterval": "0", "com_maxfps": "60",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(options.basepath.resolve()),
        "fs_savepath": str(profile), "fs_devpath": str(profile),
        "g_autoExecAfterMapLoad": "openxr_vr_menu_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "g_autoSkipCinematics": "1", "com_skipLoadingContinue": "1",
        "com_loadingContinueAutoAdvance": "1", "com_allowConsole": "1", "sys_lang": options.language,
        "vr_enable": "1", "vr_debug": "1", "vr_aimMode": "1", "vr_screenDistance": "2.5",
        "vr_screenWidth": str(SCREEN_WIDTH_M), "vr_leftHanded": "0",
        # after the launch configuration's own r_renderApi, so this one wins
        "r_renderApi": options.render_api,
    }
    for key, value in settings.items():
        args += ["+set", key, value]
    args += ["+map", "game/airdefense1"]
    command = [str(staged / executable), *args]
    (profile / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")

    environment = dict(os.environ)
    environment.update({
        "XR_RUNTIME_JSON": str(manifest),
        "OPENQ4_XR_TEST_LOG": str(xr_log),
        "OPENQ4_XR_TEST_SCRIPT": str(profile / "xr_script.txt"),
        "OPENQ4_XR_TEST_EYE_SIZE": "1200x1320",
        "OPENQ4_XR_TEST_RATE": "90",
    })
    startup = None
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    with (profile / "process.log").open("w", encoding="utf-8") as log:
        try:
            result = subprocess.run(command, cwd=staged, stdout=log, stderr=subprocess.STDOUT,
                                    env=environment, startupinfo=startup, timeout=options.timeout)
        except subprocess.TimeoutExpired:
            raise AssertionError(f"the client did not finish within {options.timeout} s")

    text = (game / "logs/openq4.log").read_text(encoding="utf-8", errors="replace")
    assert "OpenXR: presenting to the headset" in text, "the session never began"
    assert result.returncode == 0, result.returncode
    assert "VR_MENU_SMOKE_COMPLETE" in text, "script did not complete"
    assert not re.search(r"FATAL|ERROR:|OpenXR: .* failed|Unknown command", text), "errors in the log"
    clicks = text.count("OpenXR: MOUSE1 down")
    expected = 2 + SECTION_CLICKS + LIST_CLICKS
    assert clicks == expected, f"expected {expected} pointer clicks on menus, saw {clicks}"
    laser = re.search(r'"vr_aimLaser" is:"(\d)"', text.partition("VR_MENU_LASER")[2])
    assert laser, "vr_aimLaser was not reported"
    assert laser.group(1) == "2", \
        f"clicking Laser Sight in the Virtual Reality section should step it from 1 to 2, it is {laser.group(1)}"

    # settings pages are opaque, so the world stops rendering and the screen is the only layer
    for name in ("xr_vr_section", "xr_laser_open", "xr_laser_set"):
        quads = sorted(profile.glob(f"{name}_quad*.tga"))
        assert quads, f"no virtual-screen capture for {name}"
        with Image.open(quads[-1]) as screen:
            screen.convert("RGB").save(profile / f"{name}_screen.png")
    print(f"OpenXR VR menu smoke ({options.render_api}): PASS (Laser Sight stepped to dot and beam through the pointer); evidence={profile}")


if __name__ == "__main__":
    main()
