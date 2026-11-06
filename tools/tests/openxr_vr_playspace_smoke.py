#!/usr/bin/env python3
"""Standing and seated VR play spaces against the openQ4 OpenXR test runtime
(requires a staged client built with OpenXR and retail assets).

vr_playSpace picks the reference space heights are measured in. Standing play
measures the head from the floor of STAGE (or XR_EXT_local_floor's
LOCAL_FLOOR) against the standing eye height, vr_standingHeight, which is
learned at the first recentre or measured by vr_calibrateHeight. Seated play
measures it from where it was recentred. A runtime without a floor-level space
leaves standing play head-relative and says so. The test runtime offers STAGE
by default and is told which spaces to offer per run (OPENQ4_XR_TEST_SPACES).
Nothing synthesises input: console commands write marker files and the
runtime's script moves the head when they appear.

Checks, on STAGE:
- the session stands on STAGE, learns a 1.60 m standing eye height from the
  head at the first recentre and reports the 2 x 2 m play area;
- a head 0.5 m down crouches the body, and a recentre while crouched keeps
  the floor: the head still reads 0.5 m below the standing eye, and standing
  up again stands the body at the same height, so nothing drifts;
- vr_calibrateHeight with the head at 1.75 m makes 1.75 m the standing eye,
  after which a 1.60 m head reads 0.15 m low;
- switching to seated play mid-session recreates the base space on LOCAL and
  recentres the head without moving the body; a recentre while crouched then
  makes the crouched head the standing eye, as seated play should; switching
  back stands on STAGE with the calibrated height, again without moving the
  body;
- room scale on STAGE: a half-metre step walks the body about half a metre,
  once, and leaves the head near the origin, never the step twice;
then with OPENQ4_XR_TEST_SPACES=local_floor standing play uses LOCAL_FLOOR,
and with none it falls back to head-relative play with a warning.
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
UNITS_PER_METRE = 39.37
# a level spot near airdefense1's start with room to walk ahead (x y z yaw)
POSE = "10310.89 -6950.34 6.96 150.7"


def frame_guarded(commands: list[str]) -> list[str]:
    """Lets frames run after each real-time wait and before each marker (see openxr_vr_smoke.py)."""
    guarded = []
    for command in commands:
        if command.startswith("condump vr_marker_"):
            guarded.append("wait 2")
        guarded.append(command)
        if command.startswith("waitMsec "):
            guarded.append("wait 3")
    return guarded


def run(options, staged: Path, executable: str, manifest: Path, profile: Path, spaces: str,
        commands: list[str], script: list[str]) -> str:
    game = profile / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    for stale in game.glob("vr_marker_*.txt"):
        stale.unlink()
    log_path = game / "logs/openq4.log"
    log_path.unlink(missing_ok=True)
    (profile / "xr_script.txt").write_text("\n".join(script) + "\n", encoding="utf-8")
    (game / "openxr_vr_playspace_smoke.cfg").write_text("\n".join(frame_guarded(commands)) + "\n", encoding="utf-8")

    launches = json.loads((ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
    launch = next(c for c in launches["configurations"] if c["name"] == "(SP) Main menu — GL")
    args = [a.replace("${workspaceFolder}", str(ROOT)) for a in launch["args"]]
    settings = {
        "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "r_hiddenWindow": "1", "r_windowWidth": "960", "r_windowHeight": "540",
        "r_mode": "-1", "r_customWidth": "960", "r_customHeight": "540",
        "r_swapInterval": "0", "com_maxfps": "60",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(options.basepath.resolve()),
        "fs_savepath": str(profile), "fs_devpath": str(profile),
        "g_autoExecAfterMapLoad": "openxr_vr_playspace_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "g_autoSkipCinematics": "1", "com_skipLoadingContinue": "1",
        "com_loadingContinueAutoAdvance": "1", "com_allowConsole": "1",
        # the profile persists between runs: every setting this smoke reads starts fresh
        "vr_enable": "1", "vr_debug": "1", "vr_aimMode": "1", "vr_roomScale": "0", "vr_physicalCrouch": "1",
        "vr_playSpace": "2", "vr_standingHeight": "0",
        # after the launch configuration's own r_renderApi, so this one wins
        "r_renderApi": options.render_api,
    }
    for key, value in settings.items():
        args += ["+set", key, value]
    args += ["+map", "game/airdefense1"]
    command = [str(staged / executable), *args]
    (profile / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    environment = {k: v for k, v in os.environ.items() if not k.startswith("OPENQ4_XR_TEST_")}
    environment.update({
        "XR_RUNTIME_JSON": str(manifest),
        "OPENQ4_XR_TEST_LOG": str(profile / "xr.jsonl"),
        "OPENQ4_XR_TEST_SCRIPT": str(profile / "xr_script.txt"),
        "OPENQ4_XR_TEST_EYE_SIZE": "640x704",
        "OPENQ4_XR_TEST_RATE": "90",
        "OPENQ4_XR_TEST_SPACES": spaces,
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
            raise AssertionError(f"the client did not finish within {options.timeout} s ({profile.name})")
    text = log_path.read_text(encoding="utf-8", errors="replace")
    assert "OpenXR: presenting to the headset" in text, f"the session never began ({profile.name})"
    assert result.returncode == 0, (profile.name, result.returncode)
    assert "VR_PLAYSPACE_COMPLETE" in text, f"script did not complete ({profile.name})"
    assert not re.search(r"FATAL|ERROR:|OpenXR: .* failed|Unknown command", text), f"errors in the log ({profile.name})"
    return text


def after(text: str, tag: str) -> str:
    assert f"{tag} \n" in text or f"{tag}\n" in text, f"marker {tag} missing"
    return text.partition(tag)[2]


def status(text: str, tag: str) -> tuple[str, float]:
    """The play-space line and the head's height offset (units) that vr_status printed after tag."""
    tail = after(text, tag)
    space = re.search(r"play space: ([^\n]*)", tail)
    head = re.search(r"head tracked \( (-?\d+\.\d) (-?\d+\.\d) (-?\d+\.\d) \)", tail)
    assert space and head, f"vr_status after {tag} printed no play space or head"
    return space.group(1).strip(), float(head.group(3))


def view(text: str, tag: str) -> tuple[float, float, float]:
    found = re.search(r"\(\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s*\)\s+-?\d", after(text, tag))
    assert found, f"getviewpos after {tag} printed no view position"
    return float(found.group(1)), float(found.group(2)), float(found.group(3))


def near(value: float, expected: float, tolerance: float, what: str) -> None:
    assert abs(value - expected) <= tolerance, f"{what}: expected {expected:.1f} +/- {tolerance:.1f}, got {value:.1f}"


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 assets")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir",
                        help="build directory holding openq4_xr_test_runtime.json")
    parser.add_argument("--executable-name")
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
    output = (options.output_dir or ROOT / ".tmp/openxr-vr-playspace-smoke").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")

    # --- STAGE: the full sequence
    profile = output / f"stage-{options.render_api}"
    game = profile / "baseoq4"

    def marker(name: str) -> str:
        return str(game / f"vr_marker_{name}.txt")

    def head(y: float, z: float = 0.0) -> str:
        return f"head 0 0 0 0 {y:.2f} {z:.2f}"

    script = [
        f"when {marker('crouch')} {head(1.10)}",
        f"when {marker('stand')} {head(1.60)}",
        f"when {marker('tall')} {head(1.75)}",
        f"when {marker('normal')} {head(1.60)}",
        f"when {marker('seat_crouch')} {head(1.10)}",
        f"when {marker('seat_stand')} {head(1.60)}",
        f"when {marker('step')} {head(1.60, -0.5)}",
    ]
    commands = [
        "waitMsec 2500", "god", "notarget", f"setviewpos {POSE}", "waitMsec 800",
        "echo VR_PS_START", "vr_status", "getviewpos",
        # crouched, then recentred while crouched: standing play keeps the floor
        "condump vr_marker_crouch.txt", "waitMsec 1500", "echo VR_PS_CROUCHED", "vr_status", "getviewpos",
        "vr_recenter", "waitMsec 400", "echo VR_PS_RECENTRED_LOW", "vr_status",
        "condump vr_marker_stand.txt", "waitMsec 1500", "echo VR_PS_STOOD", "vr_status", "getviewpos",
        # calibration at a taller head
        "condump vr_marker_tall.txt", "waitMsec 500", "vr_calibrateHeight", "waitMsec 400",
        "echo VR_PS_CALIBRATED", "vr_status", "vr_standingHeight",
        "condump vr_marker_normal.txt", "waitMsec 500", "echo VR_PS_NORMAL", "vr_status", "getviewpos",
        # seated play mid-session, then a crouched recentre redefines the standing eye
        "vr_playSpace 1", "waitMsec 600", "echo VR_PS_SEATED", "vr_status", "getviewpos",
        "condump vr_marker_seat_crouch.txt", "waitMsec 600", "vr_recenter", "waitMsec 400",
        "echo VR_PS_SEATED_LOW", "vr_status",
        "condump vr_marker_seat_stand.txt", "waitMsec 600", "echo VR_PS_SEATED_UP", "vr_status",
        # standing again: the calibrated height, the body where it was
        "vr_playSpace 2", "waitMsec 600", "echo VR_PS_STANDING_AGAIN", "vr_status", "getviewpos",
        # room scale on STAGE: one half-metre step, walked once
        "vr_recenter", "waitMsec 300", "vr_roomScale 1", "waitMsec 300", "echo VR_PS_WALK_FROM", "getviewpos",
        "condump vr_marker_step.txt", "waitMsec 2500", "echo VR_PS_WALK_TO", "vr_status", "getviewpos",
        "vr_roomScale 0", "echo VR_PLAYSPACE_COMPLETE", "quit",
    ]
    text = run(options, staged, executable, manifest, profile, "stage", commands, script)

    assert "OpenXR: play space standing on STAGE" in text, "standing play did not take the STAGE space"
    assert "OpenXR: standing eye height learned as 1.60 m" in text, "the first recentre did not learn the standing height"
    space, start = status(text, "VR_PS_START")
    assert space.startswith("standing on STAGE, standing eye height 1.60 m") and "play area 2.00 x 2.00 m" in space, space
    near(start, 0.0, 1.0, "the standing head at the start")
    drop = 0.5 * UNITS_PER_METRE
    _, crouched = status(text, "VR_PS_CROUCHED")
    near(crouched, -drop, 1.5, "the head 0.5 m down")
    eye_start, eye_crouched = view(text, "VR_PS_START")[2], view(text, "VR_PS_CROUCHED")[2]
    assert 30.0 < eye_start - eye_crouched < 42.0, f"the head 0.5 m down should crouch the body ({eye_start:.1f} to {eye_crouched:.1f})"
    space, low = status(text, "VR_PS_RECENTRED_LOW")
    assert "standing eye height 1.60 m" in space, f"a crouched recentre moved the floor: {space}"
    near(low, -drop, 1.5, "the head after a crouched recentre (standing keeps the floor)")
    _, stood = status(text, "VR_PS_STOOD")
    near(stood, 0.0, 1.0, "the head standing again after the crouched recentre")
    near(view(text, "VR_PS_STOOD")[2], eye_start, 1.0, "the body standing again")

    assert "OpenXR: standing eye height calibrated to 1.75 m" in text, "vr_calibrateHeight did not measure the head"
    assert re.search(r'"vr_standingHeight" is:"1\.75', after(text, "VR_PS_CALIBRATED")), "vr_standingHeight did not become 1.75"
    space, calibrated = status(text, "VR_PS_CALIBRATED")
    assert "standing eye height 1.75 m" in space, space
    near(calibrated, 0.0, 1.0, "the head at the calibrated height")
    _, normal = status(text, "VR_PS_NORMAL")
    near(normal, -0.15 * UNITS_PER_METRE, 1.0, "a 1.60 m head under a 1.75 m standing eye")
    body_before = view(text, "VR_PS_NORMAL")

    assert "OpenXR: play space seated on LOCAL" in text, "seated play did not take the LOCAL space"
    space, seated = status(text, "VR_PS_SEATED")
    assert space.startswith("seated on LOCAL"), space
    near(seated, 0.0, 1.0, "the head recentred by the switch to seated play")
    body_seated = view(text, "VR_PS_SEATED")
    assert math.dist(body_before[:2], body_seated[:2]) < 1.0, f"the switch to seated play moved the body {body_before} -> {body_seated}"
    _, seated_low = status(text, "VR_PS_SEATED_LOW")
    near(seated_low, 0.0, 1.0, "seated play: a crouched recentre makes the crouched head the standing eye")
    _, seated_up = status(text, "VR_PS_SEATED_UP")
    near(seated_up, drop, 1.5, "seated play: rising above the recentre lifts the head")

    space, again = status(text, "VR_PS_STANDING_AGAIN")
    assert space.startswith("standing on STAGE, standing eye height 1.75 m"), space
    near(again, -0.15 * UNITS_PER_METRE, 1.0, "standing again: the calibrated height, not the seated recentre")
    body_again = view(text, "VR_PS_STANDING_AGAIN")
    assert math.dist(body_seated[:2], body_again[:2]) < 1.0, f"the switch back to standing moved the body {body_seated} -> {body_again}"

    walk_from, walk_to = view(text, "VR_PS_WALK_FROM"), view(text, "VR_PS_WALK_TO")
    walked = math.dist(walk_from[:2], walk_to[:2])
    near(walked, drop, 6.0, "room scale on STAGE: the body walks the half-metre step once")
    tail = after(text, "VR_PS_WALK_TO")
    head_xy = re.search(r"head tracked \( (-?\d+\.\d) (-?\d+\.\d) (-?\d+\.\d) \)", tail)
    assert head_xy and math.hypot(float(head_xy.group(1)), float(head_xy.group(2))) < 6.0, \
        "room scale on STAGE: the head should end near the tracking origin after the walk"

    # --- LOCAL_FLOOR and no floor at all
    short = ["waitMsec 2500", "echo VR_PS_START", "vr_status", "echo VR_PLAYSPACE_COMPLETE", "quit"]
    floor = run(options, staged, executable, manifest, output / f"local-floor-{options.render_api}", "local_floor", short, [])
    assert "OpenXR: play space standing on LOCAL_FLOOR" in floor, "without a STAGE, standing play should use LOCAL_FLOOR"
    space, _ = status(floor, "VR_PS_START")
    assert space.startswith("standing on LOCAL_FLOOR, standing eye height 1.60 m"), space
    none = run(options, staged, executable, manifest, output / f"no-floor-{options.render_api}", "none", short, [])
    assert "OpenXR: play space seated on LOCAL" in none and "offers no floor-level space" in none, \
        "without a floor-level space, standing play should fall back to head-relative play with a warning"
    space, _ = status(none, "VR_PS_START")
    assert "standing asked for; the runtime knows no floor" in space, space

    print(f"OpenXR VR play-space smoke ({options.render_api}): PASS (STAGE learned 1.60 m, crouched recentre kept the floor, "
          f"calibrated 1.75 m, seated and back without moving the body, room-scale step walked {walked:.1f} units; "
          f"LOCAL_FLOOR and no-floor fallback); evidence={output}")


if __name__ == "__main__":
    main()
