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
- and the head the eyes are drawn from stays where the step put it while the
  body walks after it, with no comfort vignette. Single player walks the body
  the whole step in one tic and the tracking space takes it at once, while
  the eye is drawn interpolated between tic samples: an eye that did not
  carry the step's rest jumped back by up to a tic's step and forward again.
  vr_debug 2 logs each drawn head; vr_headOffsetLimit is raised past the
  step for it, so the lean limit holds nothing back;
then with OPENQ4_XR_TEST_SPACES=local_floor standing play uses LOCAL_FLOOR,
and with none it falls back to head-relative play with a warning.

A seat run boards a walker without a console script (testVehicle enter: a
save made after the console runs a script does not load), saves in it and
loads the save. A seat's facing is not saved and the vehicle's bind disturbs
the view angles a load restores, so a loaded seat once faced anywhere (about
104 degrees off here, with the head-steered turret swinging round after it);
it must face the seat's eye, as boarding does, and so where it was saved.
Before boarding, the same run
records a demo, which plays flat as the desktop mirror shows VR: the left
eye's views only (both eyes' views and 2D, the laser dot among it, were
written), and makes a checkpoint save with vr_mirror 0, whose preview must
show the left eye rather than the black desktop window.

The LOCAL_FLOOR run also recentres from the headset: the runtime announces
that the space will move under the head some seconds ahead, as the
specification has it. The VR origin must stay where it is until frames are
displayed in the moved space and follow it then, so the head reads its old
offset while the move is pending and the origin afterwards.

The no-floor run also checks where a level starts. The body's yaw is absolute
usercmd yaw and a new map spawns the player against a zeroed usercmd, so two
snap turns once turned the next level's start, and a loaded save's view, by
those turns: the map loaded again must start facing the way it first did, and
a save loaded after a further snap turn must show the view it was saved with.
The same run holds the aim's pitch to the weapon hand's. Shots follow the
game's view angles and the laser follows the controller, so an offset between
them, which centring the view or loading a save once left, sent every shot
off the laser: with the hand 20 degrees down the aim reads 20 before and
after `centerview`, and a save made aiming down loads level with a level hand.
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
        "vr_playSpace": "2", "vr_standingHeight": "0", "vr_headOffsetLimit": "16",
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


def head_position(text: str, tag: str) -> tuple[float, float, float]:
    """The tracked head (forward, left, up; units from the VR origin) that vr_status printed after tag."""
    head = re.search(r"head tracked \( (-?\d+\.\d) (-?\d+\.\d) (-?\d+\.\d) \)", after(text, tag))
    assert head, f"vr_status after {tag} printed no tracked head"
    return float(head.group(1)), float(head.group(2)), float(head.group(3))


def view(text: str, tag: str) -> tuple[float, float, float]:
    found = re.search(r"\(\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s*\)\s+-?\d", after(text, tag))
    assert found, f"getviewpos after {tag} printed no view position"
    return float(found.group(1)), float(found.group(2)), float(found.group(3))


def view_yaw(text: str, tag: str) -> float:
    found = re.search(r"\(\s*-?\d+(?:\.\d+)?\s+-?\d+(?:\.\d+)?\s+-?\d+(?:\.\d+)?\s*\)\s+(-?\d+(?:\.\d+)?)", after(text, tag))
    assert found, f"getviewpos after {tag} printed no view yaw"
    return float(found.group(1))


def aim_pitch(text: str, tag: str) -> float:
    """The pitch of the view angles (pitch yaw roll, positive down) that a script printed after tag."""
    found = re.search(r"^\s*\(?\s*(-?\d+(?:\.\d+)?)\s+-?\d+(?:\.\d+)?\s+-?\d+(?:\.\d+)?\s*\)?\s*$", after(text, tag), re.M)
    assert found, f"no view angles were printed after {tag}"
    return float(found.group(1))


def turned(before: float, after_yaw: float) -> float:
    """Degrees turned from one yaw to another, in -180..180."""
    return (after_yaw - before + 180.0) % 360.0 - 180.0


def near(value: float, expected: float, tolerance: float, what: str) -> None:
    assert abs(value - expected) <= tolerance, f"{what}: expected {expected:.1f} +/- {tolerance:.1f}, got {value:.1f}"


def seat_yaws(text: str, tag: str) -> tuple[float, float]:
    """The view yaw getviewpos printed after tag, and the eye yaw testVehicle printed for the player's seat."""
    tail = after(text, tag)
    seat = re.search(r"testVehicleSeat: name=\S+ seat=\d+ eye=\([^)]*\) eyeYaw=(-?\d+(?:\.\d+)?)", tail)
    assert seat, f"testVehicle after {tag} printed no seat: is the player in the vehicle?"
    return view_yaw(text, tag), float(seat.group(1))


DRAWN_HEAD = re.compile(r"VR drawn head (\d+) \(\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s*\) "
                        r"vignette (-?\d+(?:\.\d+)?)")


def drawn_heads(text: str, start: str, end: str) -> list[tuple[float, float, float]]:
    """Each frame's drawn head (x, y) and vignette that vr_debug 2 logged between two markers."""
    section = after(text, start).partition(end)[0]
    return [(float(m.group(2)), float(m.group(3)), float(m.group(5))) for m in DRAWN_HEAD.finditer(section)]


def step_hold(heads: list[tuple[float, float, float]], step: float) -> tuple[int, float, float]:
    """The frame a head step shows in, how far a later drawn head strayed from
    where the step put it, and the strongest vignette across the trace."""
    assert len(heads) >= 20, f"vr_debug 2 logged {len(heads)} drawn heads around the step"
    first = heads[0][:2]
    stepped = next((i for i, h in enumerate(heads) if math.dist(h[:2], first) > step * 0.5), None)
    assert stepped is not None and len(heads) - stepped >= 10, \
        f"the drawn head never took the step: {[tuple(round(v, 1) for v in h[:2]) for h in heads[:12]]}"
    settled = heads[-1][:2]
    strayed = max(math.dist(h[:2], settled) for h in heads[stepped:])
    return stepped, strayed, max(h[2] for h in heads)


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
        # (a lean limit beyond the step, so it is the walk alone that moves the body)
        "vr_recenter", "waitMsec 300", "vr_roomScale 1", "vr_headOffsetLimit 24", "waitMsec 300",
        "echo VR_PS_WALK_FROM", "getviewpos",
        "vr_debug 2", "echo VR_PS_STEP_TRACE", "condump vr_marker_step.txt", "waitMsec 1500",
        "vr_debug 1", "echo VR_PS_STEP_TRACED", "waitMsec 1000", "vr_headOffsetLimit 16",
        "echo VR_PS_WALK_TO", "vr_status", "getviewpos",
        "vr_roomScale 0", "echo VR_PLAYSPACE_COMPLETE", "quit",
    ]
    text = run(options, staged, executable, manifest, profile, "stage", commands, script)

    assert "OpenXR: play space standing on STAGE" in text, "standing play did not take the STAGE space"
    assert "OpenXR: standing eye height learned as 1.60 m" in text, "the first recentre did not learn the standing height"
    space, start = status(text, "VR_PS_START")
    assert space.startswith("standing on STAGE, standing eye height 1.60 m") and "play area 2.00 x 2.00 m" in space, space
    near(start, 0.0, 1.0, "the standing head at the start")
    assert re.search(r"refresh: 90\.0 Hz", after(text, "VR_PS_START")), "vr_status did not report the headset's 90 Hz refresh"
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
    # the drawn head holds still once the head has stepped: the body's walk
    # after it moves the tracking space, never the eye, and darkens nothing
    step_frame, strayed, step_vignette = step_hold(drawn_heads(text, "VR_PS_STEP_TRACE", "VR_PS_STEP_TRACED"), drop)
    assert strayed < 0.5, \
        (f"room scale on STAGE: after the step (frame {step_frame} of the trace) the drawn head strayed {strayed:.2f} "
         f"units while the body walked after it: the eye lagged the tracking space")
    assert step_vignette < 0.01, f"room scale on STAGE: a real step darkened the view (vignette {step_vignette:.3f})"

    # --- LOCAL_FLOOR and no floor at all
    short = ["waitMsec 2500", "echo VR_PS_START", "vr_status", "echo VR_PLAYSPACE_COMPLETE", "quit"]
    # LOCAL_FLOOR, and the headset's own recentre: the head steps 0.3 m ahead and
    # 0.4 m to the right and turns 40 degrees, then the runtime announces that
    # LOCAL_FLOOR moves there 2.5 s later
    floor_profile = output / f"local-floor-{options.render_api}"
    floor_game = floor_profile / "baseoq4"
    floor_script = [
        f"when {floor_game / 'vr_marker_move.txt'} head 40 0 0 0.40 1.60 -0.30",
        f"when {floor_game / 'vr_marker_recenter.txt'} recenter 2500",
    ]
    floor_commands = [
        "waitMsec 2500", "echo VR_PS_START", "vr_status",
        "condump vr_marker_move.txt", "waitMsec 600", "echo VR_PS_MOVED", "vr_status",
        "condump vr_marker_recenter.txt", "waitMsec 400", "echo VR_PS_PENDING", "vr_status",
        "waitMsec 3500", "echo VR_PS_RUNTIME_RECENTRED", "vr_status",
        "echo VR_PLAYSPACE_COMPLETE", "quit",
    ]
    floor = run(options, staged, executable, manifest, floor_profile, "local_floor", floor_commands, floor_script)
    assert "OpenXR: play space standing on LOCAL_FLOOR" in floor, "without a STAGE, standing play should use LOCAL_FLOOR"
    space, _ = status(floor, "VR_PS_START")
    assert space.startswith("standing on LOCAL_FLOOR, standing eye height 1.60 m"), space
    step = (0.3 * UNITS_PER_METRE, -0.4 * UNITS_PER_METRE, 0.0)
    for tag, what in (("VR_PS_MOVED", "the head after its step"),
                      ("VR_PS_PENDING", "the head while the runtime's recentre is still pending")):
        moved = head_position(floor, tag)
        assert math.dist(moved, step) < 1.0, f"{what}: expected {step}, got {moved}"
    assert "the runtime moves reference space" in after(floor, "VR_PS_MOVED"), "the runtime's recentre was not announced"
    recentred = head_position(floor, "VR_PS_RUNTIME_RECENTRED")
    assert math.dist(recentred, (0.0, 0.0, 0.0)) < 1.0, f"the VR origin did not follow the runtime's recentre: the head reads {recentred}"
    space, _ = status(floor, "VR_PS_RUNTIME_RECENTRED")
    assert space.startswith("standing on LOCAL_FLOOR, standing eye height 1.60 m"), f"the runtime's recentre moved the floor: {space}"
    # no floor at all, and where levels start: two snap turns, the map again,
    # a save, another snap turn, and the save loaded
    none_profile = output / f"no-floor-{options.render_api}"
    none_game = none_profile / "baseoq4"
    none_game.mkdir(parents=True, exist_ok=True)

    def snap(name: str) -> list[str]:
        return [f"condump vr_marker_{name}.txt", "waitMsec 500", f"condump vr_marker_{name}_back.txt", "waitMsec 500"]

    none_script = []
    for name in ("snap1", "snap2", "snap3", "snap4"):
        none_script += [f"when {none_game / f'vr_marker_{name}.txt'} stick right 1 0",
                        f"when {none_game / f'vr_marker_{name}_back.txt'} stick right 0 0"]
    # the weapon hand, level and 20 degrees down (twice: a marker fires once)
    for name, pitch in (("aim_level", 0), ("aim_down", -20), ("aim_level2", 0), ("aim_down2", -20), ("aim_level3", 0)):
        none_script.append(f"when {none_game / f'vr_marker_{name}.txt'} hand right 0.2 1.25 -0.35 0 {pitch} 0")

    def aim(name: str) -> list[str]:
        return [f"condump vr_marker_{name}.txt", "waitMsec 500"]

    # the game's aim (pitch yaw roll), which shots follow
    angles = 'script "sys.println( $player1.getViewAngles() );"'
    none_commands = ["waitMsec 2500", *aim("aim_level"), "echo VR_PS_START", "vr_status", "getviewpos",
                     # the aim's pitch is the hand's: centring the view must not offset it
                     *aim("aim_down"), "echo VR_PS_AIM_DOWN", angles,
                     "centerview", "waitMsec 1000", "echo VR_PS_CENTRED", angles,
                     *aim("aim_level2"), "echo VR_PS_AIM_LEVEL", angles,
                     *snap("snap1"), *snap("snap2"), "echo VR_PS_SNAPPED", "getviewpos",
                     "set g_autoExecAfterMapLoad openxr_vr_playspace_reload.cfg", "map game/airdefense1"]
    reload_commands = ["waitMsec 2500", "echo VR_PS_RELOADED", "getviewpos", *snap("snap3"),
                       # saved aiming 20 degrees down, loaded aiming level. No script
                       # runs in this session: a save taken after the console ran
                       # one keeps its thread and does not load
                       *aim("aim_down2"), "echo VR_PS_PRESAVE", "getviewpos", "savegame vr_playspace_smoke",
                       "waitMsec 2500", *snap("snap4"), *aim("aim_level3"), "echo VR_PS_PRELOAD", "getviewpos",
                       "set g_autoExecAfterMapLoad openxr_vr_playspace_loaded.cfg", "loadgame vr_playspace_smoke"]
    loaded_commands = ["waitMsec 2500", "echo VR_PS_LOADED", "getviewpos", angles, "echo VR_PLAYSPACE_COMPLETE", "quit"]
    (none_game / "openxr_vr_playspace_reload.cfg").write_text("\n".join(frame_guarded(reload_commands)) + "\n", encoding="utf-8")
    (none_game / "openxr_vr_playspace_loaded.cfg").write_text("\n".join(frame_guarded(loaded_commands)) + "\n", encoding="utf-8")
    none = run(options, staged, executable, manifest, none_profile, "none", none_commands, none_script)
    level_start = view_yaw(none, "VR_PS_START")
    near(turned(level_start, view_yaw(none, "VR_PS_SNAPPED")), -90.0, 1.0, "two right snap turns")
    near(turned(level_start, view_yaw(none, "VR_PS_RELOADED")), 0.0, 1.0,
         "the level loaded again after two snap turns should start facing the way it first did; its start turned")
    saved_yaw = view_yaw(none, "VR_PS_PRESAVE")
    near(turned(saved_yaw, view_yaw(none, "VR_PS_PRELOAD")), -45.0, 1.0, "the snap turn after saving")
    near(turned(saved_yaw, view_yaw(none, "VR_PS_LOADED")), 0.0, 1.0,
         "a save loaded after a snap turn should show the view it was saved with; it turned")
    # shots follow the game's aim and the laser the controller: the aim's pitch
    # must be the hand's, whatever set the view since
    near(aim_pitch(none, "VR_PS_AIM_DOWN"), 20.0, 0.5, "the aim with the hand 20 degrees down")
    near(aim_pitch(none, "VR_PS_CENTRED"), 20.0, 0.5, "the aim after centring the view, the hand still 20 degrees down")
    near(aim_pitch(none, "VR_PS_AIM_LEVEL"), 0.0, 0.5, "the aim with the hand level again after centring the view")
    # the save was made with the hand 20 degrees down, as at VR_PS_AIM_DOWN
    near(aim_pitch(none, "VR_PS_LOADED"), 0.0, 0.5, "the aim, with the hand level, after loading a save made aiming 20 degrees down")
    assert "OpenXR: play space seated on LOCAL" in none and "offers no floor-level space" in none, \
        "without a floor-level space, standing play should fall back to head-relative play with a warning"
    space, _ = status(none, "VR_PS_START")
    assert "standing asked for; the runtime knows no floor" in space, space

    # a seat saved and loaded: boarded without a console script, so the save loads
    seat_profile = output / f"seat-{options.render_api}"
    seat_game = seat_profile / "baseoq4"
    seat_game.mkdir(parents=True, exist_ok=True)
    seat_commands = ["waitMsec 2500", "god", "notarget",
                     # a demo recorded in VR: the left eye's views and the virtual screen's 2D, as the mirror shows it
                     "r_showDemo 1", "echo VR_PS_DEMO", "recordDemo vr_playspace_demo", "waitMsec 1500", "stopRecording",
                     "r_showDemo 0", "echo VR_PS_DEMO_DONE",
                     # a checkpoint's preview shows the left eye, even with the desktop mirror black
                     "vr_mirror 0", "savegame checkPoint", "waitMsec 2500", "vr_mirror 1",
                     'spawn vehicle_walker name vr_ps_walker origin "10094 -6825 60" angle 150.7', "waitMsec 2500",
                     "testVehicle vr_ps_walker enter", "waitMsec 2000",
                     "echo VR_PS_SEAT_IN", "getviewpos", "testVehicle vr_ps_walker",
                     "savegame vr_playspace_seat", "waitMsec 2500",
                     "set g_autoExecAfterMapLoad openxr_vr_playspace_seat.cfg", "loadgame vr_playspace_seat"]
    seat_loaded = ["waitMsec 2500", "echo VR_PS_SEAT_LOADED", "getviewpos", "testVehicle vr_ps_walker",
                   "echo VR_PLAYSPACE_COMPLETE", "quit"]
    (seat_game / "openxr_vr_playspace_seat.cfg").write_text("\n".join(frame_guarded(seat_loaded)) + "\n", encoding="utf-8")
    for stale in (seat_game / "savegames").glob("*") if (seat_game / "savegames").is_dir() else []:
        stale.unlink()
    seat = run(options, staged, executable, manifest, seat_profile, "stage", seat_commands, [])
    # the demo: one eye's views a frame (airdefense1's portal sky, then the
    # view), never the eyes' 2D such as the laser's red dot
    recorded = after(seat, "VR_PS_DEMO").partition("VR_PS_DEMO_DONE")[0].split("write DC_END_FRAME")[1:-1]
    # (r_showDemo's report of every surface slows recording to a few frames a second)
    assert len(recorded) >= 5, f"the demo recorded {len(recorded)} complete frames"
    views = sorted({frame.count("write DC_RENDERVIEW") for frame in recorded})
    assert views == [2], f"a demo recorded in VR should write the left eye's portal sky and view each frame, not {views}"
    laser = re.findall(r"gui surface \d+: _white color=1\.00 0\.15 0\.10", after(seat, "VR_PS_DEMO").partition("VR_PS_DEMO_DONE")[0])
    assert not laser, f"a demo recorded in VR wrote {len(laser)} surfaces of the eyes' laser dot"
    # the checkpoint's preview, taken with vr_mirror 0 (a black desktop window)
    from PIL import Image, ImageStat
    previews = [p for p in (seat_game / "savegames").glob("*.tga") if p.stem != "vr_playspace_seat"]
    assert len(previews) == 1, f"the checkpoint wrote {[p.name for p in previews]}"
    with Image.open(previews[0]) as preview:
        preview_luma = ImageStat.Stat(preview.convert("L")).mean[0]
    assert preview_luma > 8.0, \
        f"the checkpoint's preview is black (mean {preview_luma:.1f}): it read the black desktop mirror, not the left eye"
    assert "testVehicle: action=enter name=vr_ps_walker success=1" in seat, "testVehicle could not board the walker"
    boarded_view, boarded_eye = seat_yaws(seat, "VR_PS_SEAT_IN")
    near(turned(boarded_eye, boarded_view), 0.0, 3.0, "boarding: the seat faces the walker's eye")
    loaded_view, loaded_eye = seat_yaws(seat, "VR_PS_SEAT_LOADED")
    # the walker's turret follows the head, so a seat loaded facing elsewhere
    # swings the turret round after it: the view must stay where it was saved
    near(turned(boarded_view, loaded_view), 0.0, 10.0,
         "a save made in the walker's seat should load facing where it was saved; the loaded seat turned")
    near(turned(loaded_eye, loaded_view), 0.0, 3.0, "the loaded seat faces the walker's eye")

    print(f"OpenXR VR play-space smoke ({options.render_api}): PASS (STAGE learned 1.60 m, crouched recentre kept the floor, "
          f"calibrated 1.75 m, seated and back without moving the body, room-scale step walked {walked:.1f} units "
          f"with the drawn head still after it (strayed {strayed:.2f}); "
          f"LOCAL_FLOOR, the headset's own recentre followed when the space moved, no-floor fallback, a level "
          f"and a save start facing as they should after snap turns, and a seat saved in a walker loads facing "
          f"where it was saved ({turned(boarded_view, loaded_view):+.1f}); evidence={output}")


if __name__ == "__main__":
    main()
