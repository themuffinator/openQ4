#!/usr/bin/env python3
"""Play multiplayer in VR against the openQ4 OpenXR test runtime (requires a
staged client built with OpenXR and retail assets).

The VR player hosts a listen server on a deathmatch map and joins its own
match. As in openxr_vr_smoke.py, the test runtime (selected for this process
only through XR_RUNTIME_JSON) simulates the headset and controllers, console
commands drive the game, and the runtime's frame script follows them through
marker files the engine writes with condump; no keyboard or mouse input is
synthesised.

Checks:
- the player joins the match (leaves the spectator body) and the session
  presents a stereo projection layer with the HUD;
- room scale: a server moves bodies only from usercmds, so half a metre's step
  forward walks the body after the head through the usercmd, and once it has
  coasted to a stop the head is within a few units of the tracking origin;
- while it walks, the head the eyes are drawn from stays where the step put
  it (the tracking space takes each tic's step whole while the eye is drawn
  between tic samples, so an eye that did not carry the rest shimmered by up
  to a tic's walk), and the comfort vignette stays off: the walk has the
  stick's velocity but is the player's own step. vr_debug 2 logs each drawn
  head and vignette; vr_headOffsetLimit is raised past the step for it;
- on OpenGL, a damage number staged straight ahead of the head shows on the
  HUD layer where that line meets it: the numbers project through the layer
  the headset hangs in front of the head, not through the last eye rendered;
- with vr_roomScale 0 the same step stays a lean of the whole distance;
- the log carries no errors.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess

from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]
STEP_UNITS = 0.5 * 39.37
DRAWN_HEAD = re.compile(r"VR drawn head (\d+) \(\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s*\) "
                        r"vignette (-?\d+(?:\.\d+)?)")
# The HUD layer (metres): a 1.2 m wide plane 1.4 m ahead of the head, hung
# 0.1 m low while the hand aims, at the 16:9 window's shape. A number straight
# ahead of the head meets it at the centre across and 0.1 / 1.4 of the height
# tangent above it: 240 - (0.1 / 1.4) * 240 / (0.6 / 1.4 * 9 / 16) = 168.9 on
# the 640x480 canvas. Through the right eye's off-axis frustum, as the numbers
# were placed, it landed near (268, 225).
HUD_DISTANCE, HUD_WIDTH, HUD_OFFSET = 1.4, 1.2, -0.1
NUMBER_AHEAD = (320.0, 240.0 + (HUD_OFFSET / HUD_DISTANCE) * 240.0 / (HUD_WIDTH / 2.0 / HUD_DISTANCE * 9.0 / 16.0))


def damage_number_centre(shots: Path) -> tuple[float, float] | None:
    """Where the staged number's red glyphs sit on the HUD layer, in 640x480 units."""
    from PIL import Image, ImageChops
    with Image.open(shots / "vr_mp_numbers_on.tga") as on, Image.open(shots / "vr_mp_numbers_off.tga") as off:
        on, off = on.convert("RGB"), off.convert("RGB")
        delta = ImageChops.difference(on, off).convert("L").load()
        pixels = on.load()
        xs, ys = [], []
        for y in range(on.height):
            for x in range(on.width):
                r, g, b = pixels[x, y]
                # a severe hit's red, and only where the frames differ
                if r > 150 and g < 90 and b < 90 and delta[x, y] > 24:
                    xs.append(x)
                    ys.append(y)
        if len(xs) < 40:
            return None
        return sum(xs) / len(xs) * 640.0 / on.width, sum(ys) / len(ys) * 480.0 / on.height


def read_events(path: Path) -> list[dict]:
    events = []
    if path.is_file():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return events


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 assets")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir",
                        help="build directory holding openq4_xr_test_runtime.json")
    parser.add_argument("--executable-name")
    parser.add_argument("--map", default="mp/q4dm1")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    options = parser.parse_args(argv)

    staged = options.runtime_dir.resolve()
    executable = options.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    manifest = (options.build_dir / "openq4_xr_test_runtime.json").resolve()
    if not (staged / executable).is_file() or not options.basepath.is_dir():
        parser.error("a staged client and installed Quake 4 assets are required")
    if not manifest.is_file():
        parser.error(f"the OpenXR test runtime is not built: {manifest}")
    verify_runtime_is_current(staged, executable)

    output = (options.output_dir or ROOT / ".tmp/openxr-vr-mp-smoke").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")
    profile = output / ( "mp-vulkan" if options.render_api == "vulkan" else "mp" )
    game = profile / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    for stale in game.glob("vr_marker_*.txt"):
        stale.unlink()
    xr_log = profile / "xr.jsonl"
    xr_log.unlink(missing_ok=True)

    def marker(name: str) -> str:
        return str(game / f"vr_marker_{name}.txt")

    script = [
        f"when {marker('walk')} head 0 0 0 0 1.6 -0.5",
        f"when {marker('lean')} head 0 0 0 0 1.6 -1.0",
        f"when {marker('back')} head 0 0 0 0 1.6 0",
        f"when {marker('exit')} exit",
    ]
    (profile / "xr_script.txt").write_text("\n".join(script) + "\n", encoding="utf-8")
    numbers = [] if options.render_api == "vulkan" else [
        # a damage number staged 128 units along the aim, on the HUD layer
        # (OpenGL names that target; Vulkan keeps its targets to itself)
        'screenshot image _vrScreenColor "screenshots/vr_mp_numbers_off.tga"',
        "developer 1", "testHudOverlays 75 2000", "developer 0", "waitMsec 60",
        'screenshot image _vrScreenColor "screenshots/vr_mp_numbers_on.tga"', "waitMsec 1500",
    ]
    for stale in (game / "screenshots").glob("vr_mp_numbers_*.tga") if (game / "screenshots").is_dir() else []:
        stale.unlink()
    commands = [
        "waitMsec 4000", "echo VR_MP_ACTIVE", "vr_status", *numbers,
        # room scale: the step walks the body through the usercmd, then coasts to a stop
        "vr_roomScale 1", "vr_headOffsetLimit 24", "vr_debug 2", "echo VR_MP_STEP_TRACE",
        "condump vr_marker_walk.txt", "waitMsec 2500", "vr_debug 1", "echo VR_MP_WALKED", "vr_status",
        "vr_headOffsetLimit 16",
        "vr_roomScale 0", "condump vr_marker_lean.txt", "waitMsec 2000", "echo VR_MP_LEANED", "vr_status",
        "condump vr_marker_back.txt", "waitMsec 600", "vr_recenter", "vr_roomScale 1", "waitMsec 600",
        "condump vr_marker_exit.txt", "waitMsec 1500",
        "echo VR_MP_SMOKE_COMPLETE", "quit",
    ]
    (game / "openxr_vr_mp_smoke.cfg").write_text("\n".join(commands) + "\n", encoding="utf-8")

    launches = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
    launch = next(c for c in launches["configurations"] if c["name"].startswith("(MP) Main menu"))
    args = [a.replace("${workspaceFolder}", str(ROOT)) for a in launch["args"]]
    # the launch starts at the join screen; this player joins its own match
    for i in range(len(args) - 2):
        if args[i] == "+set" and args[i + 1] == "ui_autoJoin":
            args[i + 2] = "1"
    settings = {
        "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "r_hiddenWindow": "1", "r_windowWidth": "1280", "r_windowHeight": "720",
        "r_mode": "-1", "r_customWidth": "1280", "r_customHeight": "720",
        "r_swapInterval": "0", "com_maxfps": "60",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(options.basepath.resolve()),
        "fs_savepath": str(profile), "fs_devpath": str(profile),
        "g_autoExecAfterMapLoad": "openxr_vr_mp_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "com_skipLoadingContinue": "1", "com_loadingContinueAutoAdvance": "1", "com_allowConsole": "1",
        "vr_enable": "1", "vr_debug": "1", "vr_aimMode": "1", "vr_roomScale": "1",
        "vr_headOffsetLimit": "16", "vr_comfortVignette": "0.5",
        # the HUD layer the damage number is placed on (HUD_* below)
        "vr_hudDistance": str(HUD_DISTANCE), "vr_hudWidth": str(HUD_WIDTH), "vr_hudHeightOffset": str(HUD_OFFSET),
        "hud_damageNumbers": "1", "hud_damageNumberScale": "1",
        "net_serverDedicated": "0", "si_gameType": "DM", "sv_cheats": "1", "net_allowCheats": "1",
        "net_serverAllowServerMod": "0",
        # the listen server and its one client stay on loopback, without a
        # master-server heartbeat
        "net_ip": "127.0.0.1", "net_LANServer": "1",
        # after the launch configuration's own r_renderApi, so this one wins
        "r_renderApi": options.render_api,
    }
    for key, value in settings.items():
        args += ["+set", key, value]
    args += ["+spawnServer", options.map]
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
    events = read_events(xr_log)
    assert "VR_MP_ACTIVE" in text, "the match was not reached"
    assert "OpenXR: presenting to the headset" in text, "the session never began"
    assert result.returncode == 0, result.returncode
    assert "VR_MP_SMOKE_COMPLETE" in text, "script did not complete"
    assert not re.search(r"FATAL|ERROR:|OpenXR: .* failed|Unknown command", text), "errors in the log"
    assert "spectate = 0" in text, "the player never joined the match"
    frames = [e for e in events if e.get("event") == "end_frame"]
    assert any([layer["type"] for layer in f["layers"]] == ["projection", "quad"] for f in frames), \
        "the match never presented a stereo projection layer with the HUD"

    def head_forward(after: str) -> float:
        found = re.search(r"head tracked \( (-?\d+(?:\.\d+)?) (-?\d+(?:\.\d+)?) (-?\d+(?:\.\d+)?) \)", text.partition(after)[2])
        assert found, f"vr_status after {after} did not report a tracked head"
        return float(found.group(1))

    walked = head_forward("VR_MP_WALKED")
    leaned = head_forward("VR_MP_LEANED")
    assert abs(walked) <= 6.0, \
        f"a 0.5 m step should walk the body after the head through the usercmd, leaving it within 6 units, not {walked:.1f}"
    assert abs(leaned - walked - STEP_UNITS) < 1.5, \
        f"with vr_roomScale 0 another 0.5 m step should stay a {STEP_UNITS:.1f}-unit lean, moved {leaned - walked:.1f}"

    # the drawn head holds still while the body walks after it, undarkened
    trace = text.partition("VR_MP_STEP_TRACE")[2].partition("VR_MP_WALKED")[0]
    heads = [(float(m.group(2)), float(m.group(3)), float(m.group(5))) for m in DRAWN_HEAD.finditer(trace)]
    assert len(heads) >= 20, f"vr_debug 2 logged {len(heads)} drawn heads around the step"
    stepped = next((i for i, h in enumerate(heads) if math.dist(h[:2], heads[0][:2]) > STEP_UNITS * 0.5), None)
    assert stepped is not None and len(heads) - stepped >= 10, "the drawn head never took the step"
    strayed = max(math.dist(h[:2], heads[-1][:2]) for h in heads[stepped:])
    assert strayed < 1.0, \
        f"after the step the drawn head strayed {strayed:.2f} units while the body walked after it: the eye lagged the tracking space"
    darkest = max(h[2] for h in heads)
    assert darkest < 0.01, f"a room-scale step darkened the view: vignette {darkest:.3f}"

    # a damage number straight ahead of the head sits where the HUD layer meets
    # that line (it rises a few pixels in the frames before the capture)
    number = ""
    if numbers:
        centre = damage_number_centre(game / "screenshots")
        assert centre is not None, "the staged damage number never reached the HUD layer"
        assert abs(centre[0] - NUMBER_AHEAD[0]) < 15.0 and -25.0 < centre[1] - NUMBER_AHEAD[1] < 10.0, \
            (f"a damage number straight ahead should sit near ({NUMBER_AHEAD[0]:.0f}, {NUMBER_AHEAD[1]:.0f}) on the HUD "
             f"layer, not ({centre[0]:.0f}, {centre[1]:.0f}): it was not projected through the HUD")
        number = f", damage number at ({centre[0]:.0f}, {centre[1]:.0f})"
    print(f"OpenXR VR multiplayer smoke ({options.render_api}): PASS (room-scale walk left the head {walked:.1f} units out, "
          f"the drawn head strayed {strayed:.2f} with no vignette, lean {leaned - walked:.1f}{number}); evidence={profile}")


if __name__ == "__main__":
    main()
