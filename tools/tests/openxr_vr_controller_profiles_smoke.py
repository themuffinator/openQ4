#!/usr/bin/env python3
"""Every game action on every controller profile, against the openQ4 OpenXR
test runtime (requires a staged client built with OpenXR and retail assets).

The test runtime reports the interaction profile it is told to
(OPENQ4_XR_TEST_PROFILE) and presses that controller's inputs from its script
when marker files appear, so nothing synthesises input. With vr_debug 1 the
engine logs each gamepad key a controller drives, and each press must reach
exactly the key it is meant to (the keys are bound to console echoes so that
none of them acts on the game, the menu key included):

- Valve Index: pressing hard on the left trackpad is the left controller's
  second button (the flashlight), which Index lacked.
- HTC Vive wand: the trackpad pressed by region. Right: up crouches, down
  jumps, the centre is the stick's click, left and right turn; a thumb resting
  on it, unpressed, neither pages weapons nor turns. Left: up toggles the
  flashlight, down reloads, the centre runs.
- Windows Mixed Reality: the trackpad's upper half is the secondary button,
  the rest the primary; the thumbstick's click is still its own.
- Oculus Touch, unchanged: A jumps, B crouches, X reloads, Y toggles the
  flashlight.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from mp_round_remote_smoke import verify_runtime_is_current
from openxr_vr_playspace_smoke import frame_guarded

ROOT = Path(__file__).resolve().parents[2]
PROFILES = {
    "touch": "/interaction_profiles/oculus/touch_controller",
    "index": "/interaction_profiles/valve/index_controller",
    "vive": "/interaction_profiles/htc/vive_controller",
    "wmr": "/interaction_profiles/microsoft/motion_controller",
}
# gamepad keys (content/baseoq4/pak0/default.cfg): JOY3 jump, JOY4 crouch,
# JOY5 flashlight, JOY6 reload, JOY7 menu, JOY11/12 weapon paging, JOY13 run,
# JOY14 the weapon stick's click
KEYS = ("JOY3", "JOY4", "JOY5", "JOY6", "JOY7", "JOY11", "JOY12", "JOY13", "JOY14")

# per profile: (step name, runtime commands that press, runtime commands that
# release, the keys the press must reach, whether it must turn the player)
STEPS = {
    "touch": [
        ("right_a", ["button right a 1"], ["button right a 0"], {"JOY3"}, False),
        ("right_b", ["button right b 1"], ["button right b 0"], {"JOY4"}, False),
        ("left_x", ["button left x 1"], ["button left x 0"], {"JOY6"}, False),
        ("left_y", ["button left y 1"], ["button left y 0"], {"JOY5"}, False),
    ],
    "index": [
        ("left_pad_force", ["button left padforce 1"], ["button left padforce 0"], {"JOY5"}, False),
        ("right_a", ["button right a 1"], ["button right a 0"], {"JOY3"}, False),
        ("right_b", ["button right b 1"], ["button right b 0"], {"JOY4"}, False),
        ("left_a", ["button left a 1"], ["button left a 0"], {"JOY6"}, False),
        ("left_b", ["button left b 1"], ["button left b 0"], {"JOY7"}, False),
    ],
    "vive": [
        ("right_rest_up", ["pad right 0 0.95"], ["pad right 0 0"], set(), False),
        ("right_up", ["pad right 0 0.9", "button right padclick 1"], ["button right padclick 0", "pad right 0 0"], {"JOY4"}, False),
        ("right_down", ["pad right 0 -0.9", "button right padclick 1"], ["button right padclick 0", "pad right 0 0"], {"JOY3"}, False),
        ("right_centre", ["pad right 0.1 0.1", "button right padclick 1"], ["button right padclick 0", "pad right 0 0"], {"JOY14"}, False),
        ("right_right", ["pad right 0.9 0", "button right padclick 1"], ["button right padclick 0", "pad right 0 0"], set(), True),
        ("left_up", ["pad left 0 0.9", "button left padclick 1"], ["button left padclick 0", "pad left 0 0"], {"JOY5"}, False),
        ("left_down", ["pad left 0 -0.9", "button left padclick 1"], ["button left padclick 0", "pad left 0 0"], {"JOY6"}, False),
        ("left_centre", ["pad left 0 0", "button left padclick 1"], ["button left padclick 0"], {"JOY13"}, False),
        ("left_menu", ["button left menu 1"], ["button left menu 0"], {"JOY7"}, False),
    ],
    "wmr": [
        ("right_up", ["pad right 0 0.9", "button right padclick 1"], ["button right padclick 0", "pad right 0 0"], {"JOY4"}, False),
        ("right_down", ["pad right 0 -0.9", "button right padclick 1"], ["button right padclick 0", "pad right 0 0"], {"JOY3"}, False),
        ("right_side", ["pad right 0.9 0", "button right padclick 1"], ["button right padclick 0", "pad right 0 0"], {"JOY3"}, False),
        ("left_up", ["pad left 0 0.9", "button left padclick 1"], ["button left padclick 0", "pad left 0 0"], {"JOY5"}, False),
        ("left_down", ["pad left 0 -0.9", "button left padclick 1"], ["button left padclick 0", "pad left 0 0"], {"JOY6"}, False),
        ("right_stick_click", ["button right thumbclick 1"], ["button right thumbclick 0"], {"JOY14"}, False),
    ],
}


def run_profile(options, staged: Path, executable: str, manifest: Path, output: Path, name: str) -> list[str]:
    profile = output / name
    game = profile / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    for stale in game.glob("vr_marker_*.txt"):
        stale.unlink()
    log_path = game / "logs/openq4.log"
    log_path.unlink(missing_ok=True)

    def marker(tag: str) -> str:
        return str(game / f"vr_marker_{tag}.txt")

    # the profile persists between runs: every binding the check reads is set here
    commands = ["waitMsec 2500", "god", "notarget", "vr_debug 1", "vr_status"]
    commands += [f'bind {key} "echo VRKEY_{key}"' for key in KEYS]
    script = []
    for index, (step, press, release, _keys, _turns) in enumerate(STEPS[name]):
        commands += ["getviewpos", f"echo VRSTEP_{step}_BEGIN", f"condump vr_marker_{index}_press.txt", "waitMsec 500",
                     f"condump vr_marker_{index}_release.txt", "waitMsec 400", f"echo VRSTEP_{step}_END", "getviewpos"]
        script += [f"when {marker(f'{index}_press')} {command}" for command in press]
        script += [f"when {marker(f'{index}_release')} {command}" for command in release]
    commands += ["echo VR_PROFILES_COMPLETE", "quit"]
    (profile / "xr_script.txt").write_text("\n".join(script) + "\n", encoding="utf-8")
    (game / "openxr_vr_controller_profiles_smoke.cfg").write_text("\n".join(frame_guarded(commands)) + "\n", encoding="utf-8")

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
        "g_autoExecAfterMapLoad": "openxr_vr_controller_profiles_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "g_autoSkipCinematics": "1", "com_skipLoadingContinue": "1",
        "com_loadingContinueAutoAdvance": "1", "com_allowConsole": "1",
        "vr_enable": "1", "vr_debug": "1", "vr_aimMode": "0", "vr_roomScale": "0", "vr_physicalCrouch": "0",
        "vr_leftHanded": "0", "vr_turnMode": "0", "vr_snapTurnAngle": "45", "vr_hapticStrength": "0",
        "r_renderApi": options.render_api,
    }
    for key, value in settings.items():
        args += ["+set", key, value]
    args += ["+map", "game/airdefense1"]
    command = [str(staged / executable), *args]
    environment = {k: v for k, v in os.environ.items() if not k.startswith("OPENQ4_XR_TEST_")}
    environment.update({
        "XR_RUNTIME_JSON": str(manifest),
        "OPENQ4_XR_TEST_LOG": str(profile / "xr.jsonl"),
        "OPENQ4_XR_TEST_SCRIPT": str(profile / "xr_script.txt"),
        "OPENQ4_XR_TEST_EYE_SIZE": "480x528",
        "OPENQ4_XR_TEST_RATE": "90",
        "OPENQ4_XR_TEST_PROFILE": PROFILES[name],
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
            return [f"{name}: the client did not finish within {options.timeout} s"]
    text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    failures = []
    if f"interaction profile: {PROFILES[name]}" not in text:
        failures.append(f"{name}: the runtime did not report {PROFILES[name]}")
    if "OpenXR: presenting to the headset" not in text:
        failures.append(f"{name}: the session never began")
    if result.returncode != 0 or "VR_PROFILES_COMPLETE" not in text:
        failures.append(f"{name}: the script did not complete (exit {result.returncode})")
    if re.search(r"FATAL|ERROR:|OpenXR: .* failed|Unknown command", text):
        failures.append(f"{name}: errors in the log")
    for step, _press, _release, keys, turns in STEPS[name]:
        begin, end = f"VRSTEP_{step}_BEGIN", f"VRSTEP_{step}_END"
        if begin not in text or end not in text:
            failures.append(f"{name}/{step}: markers missing")
            continue
        before, _, rest = text.partition(begin)
        segment, _, after = rest.partition(end)
        reached = set(re.findall(r"OpenXR: (JOY\d+) down", segment))
        if reached != keys:
            failures.append(f"{name}/{step}: reached {sorted(reached)}, expected {sorted(keys)}")
        yaws = [float(m) for m in re.findall(r"\(\s*-?\d+(?:\.\d+)?\s+-?\d+(?:\.\d+)?\s+-?\d+(?:\.\d+)?\s*\)\s+(-?\d+(?:\.\d+)?)", before)[-1:]]
        yaws += [float(m) for m in re.findall(r"\(\s*-?\d+(?:\.\d+)?\s+-?\d+(?:\.\d+)?\s+-?\d+(?:\.\d+)?\s*\)\s+(-?\d+(?:\.\d+)?)", after)[:1]]
        if len(yaws) == 2:
            turned = abs((yaws[1] - yaws[0] + 180.0) % 360.0 - 180.0)
            if turns and turned < 30.0:
                failures.append(f"{name}/{step}: the press did not turn the player ({turned:.1f} degrees)")
            if not turns and turned > 5.0:
                failures.append(f"{name}/{step}: the player turned {turned:.1f} degrees")
        else:
            failures.append(f"{name}/{step}: getviewpos printed no yaw around the step")
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True, help="installed retail Quake 4 assets")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "builddir",
                        help="build directory holding openq4_xr_test_runtime.json")
    parser.add_argument("--executable-name")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--profiles", nargs="*", choices=sorted(PROFILES), default=sorted(PROFILES))
    options = parser.parse_args(argv)

    staged = options.runtime_dir.resolve()
    executable = options.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    manifest = (options.build_dir / "openq4_xr_test_runtime.json").resolve()
    if not (staged / executable).is_file() or not options.basepath.is_dir():
        parser.error("a staged client and installed Quake 4 assets are required")
    if not manifest.is_file():
        parser.error(f"the OpenXR test runtime is not built: {manifest}")
    verify_runtime_is_current(staged, executable)
    output = (options.output_dir or ROOT / ".tmp/openxr-vr-controller-profiles-smoke").resolve()
    if output == staged or staged in output.parents or output in staged.parents:
        parser.error("output and staged runtime directories must not overlap")

    failures = []
    for name in options.profiles:
        failures += run_profile(options, staged, executable, manifest, output, name)
    report = {"status": "fail" if failures else "pass", "profiles": options.profiles, "failures": failures}
    output.mkdir(parents=True, exist_ok=True)
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
