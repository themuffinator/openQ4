#!/usr/bin/env python3
"""Validate a saved match report and replay its linked MVD without device input."""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import read_log, write_script

ROOT = Path(__file__).resolve().parents[2]
STATUS = re.compile(r"MVD_STATUS playing=(\d+) paused=(\d+) seeking=(\d+) time=(\d+) duration=(\d+) follow=(-?\d+)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--match-report", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-mvd-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--timeout", type=int, default=150)
    parser.add_argument("--startup-timeout", type=int, default=90,
                        help="cold engine/map initialization and initial seek budget in seconds")
    parser.add_argument("--early-seconds", type=int, default=30)
    parser.add_argument("--later-seconds", type=int, default=70)
    parser.add_argument("--boundary-seconds", type=int,
                        help="optionally seek onto a restart gap before seeking into active gameplay")
    parser.add_argument("--cancel-boundary-follow", action="store_true",
                        help="explicitly choose free camera at the boundary and verify later seeks retain it")
    parser.add_argument("--standing-camera-height", type=float,
                        help="check the followed idle player1 eye/render height at each seek (for standing-player fixtures)")
    parser.add_argument("--play-to-later", action="store_true",
                        help="reach the later checkpoint by continuous playback, for comparison with a forward seek")
    parser.add_argument("--allow-development-view-mismatch", action="store_true",
                        help="allow rejected match views only for camera diagnosis of older development recordings")
    parser.add_argument("--library-play", action="store_true", help="start through the selected Demos library Play action")
    args = parser.parse_args()
    if args.startup_timeout <= 0 or args.timeout <= 0:
        parser.error("startup-timeout and timeout must be positive")
    if args.early_seconds < 1 or args.later_seconds <= args.early_seconds + 1:
        parser.error("seek times require 1 <= early < later - 1")
    if args.boundary_seconds is not None and not args.early_seconds < args.boundary_seconds < args.later_seconds:
        parser.error("boundary-seconds must lie between the early and later seek times")
    if args.cancel_boundary_follow and args.boundary_seconds is None:
        parser.error("cancel-boundary-follow requires boundary-seconds")
    if args.standing_camera_height is not None and (not math.isfinite(args.standing_camera_height) or
                                                   args.standing_camera_height <= 0 or args.cancel_boundary_follow):
        parser.error("standing-camera-height must be positive and requires player following")
    if args.play_to_later and args.boundary_seconds is not None:
        parser.error("play-to-later cannot be combined with an intermediate seek boundary")
    early, later_time = args.early_seconds * 1000, args.later_seconds * 1000
    report_path = args.match_report.resolve()
    evidence = json.loads(report_path.read_text(encoding="utf-8"))
    assert evidence["schema"] == 2 and evidence["sessionId"] > 0
    events = evidence["journal"]["events"]
    assert evidence["journal"]["accepted"] == len(events)
    assert evidence["journal"]["dropped"] == 0
    assert [e["sequence"] for e in events] == list(range(1, len(events) + 1))
    for field in ("sessionRevision", "matchTimeMsec", "hostTimeUtcMsec"):
        assert all(a[field] <= b[field] for a, b in zip(events, events[1:])), field
    results = [e for e in events if e["kind"] == "result"]
    assert len(results) == 1 and results[0]["data"]["outcome"] in ("decided", "forfeit")
    artifacts = [a for a in evidence["artifacts"] if a["kind"] == "mvd"]
    assert len(artifacts) == 1
    source_game = report_path.parent.parent
    source = (source_game / artifacts[0]["qpath"]).resolve()
    assert source.is_relative_to(source_game) and source.suffix == ".mvd" and source.is_file()
    assert not source.with_suffix(".mvd.part").exists()

    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    exe = runtime / (args.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"))
    if not exe.is_file():
        parser.error("a staged runtime is required")
    save, failures = output / "save", []
    game = save / "baseoq4"
    (game / "demos").mkdir(parents=True, exist_ok=True)
    if (runtime / "manifest.json").is_file():
        shutil.copy2(runtime / "manifest.json", output / "runtime-manifest.json")
    shutil.copy2(source, game / "demos" / source.name)
    shots = ("library", "next", "free", "playing", "later", "rewound", "controls")
    if args.boundary_seconds is not None:
        shots += ("boundary",)
    loop = game / "mvd_wait_0.cfg"
    write_script(loop, ["waitMsec 200", "echo MP_MVD_POLL_INITIAL", "mvdStatus", f"exec {loop.name}"])
    write_script(game / "mvd_probe.cfg", [
        f'mvdInfo "{source.name}"', "demoMenu", "waitMsec 1000",
        *(["openq4_demoLibrary report"] if args.library_play else []),
        'screenshot "screenshots/library.tga"',
        "openq4_demoLibrary play" if args.library_play else f'playMVD "{source.name}"',
        "waitMsec 1500", "mvdPause 1", f"mvdSeek {args.early_seconds}", f"exec {loop.name}",
    ])
    cvars = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(save), "fs_devpath": str(save),
        "fs_game": "baseoq4", "com_gameMode": "MP", "com_nextGameModule": "game_mp",
        "r_fullscreen": "0", "r_borderless": "0",
        "r_fullscreenDesktop": "0", "r_borderlessDefaultMigrated": "1", "r_mode": "-1",
        "r_customWidth": "960", "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540",
        "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_renderApi": "gl", "com_maxfps": "60", "r_swapInterval": "0", "logFile": "2",
        "logFileName": "logs/openq4.log", "developer": "1", "com_skipLoadingContinue": "1",
        "ui_autoJoin": "1", "net_allowCheats": "1", "g_autoExecAfterMapLoad": "",
    }
    command = [str(exe)]
    for key, value in cvars.items():
        command += ["+set", key, value]
    command += ["+exec", "mvd_probe.cfg"]
    (output / "launch.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    started = time.time()
    deadline = time.monotonic() + args.timeout
    log_path = game / "logs/openq4.log"
    phase_number = 0
    camera_states = {}
    startup_seconds = None
    expected_follow = None

    def capture(name: str) -> list[str]:
        return ["echo MP_MVD_" + name.upper(), "mvdStatus", "openq4_reportMPState",
                "openq4_reportMPWorld player1",
                f'screenshot "screenshots/{name}.tga"']

    def wait_state(label: str, predicate, timeout: int = 45) -> tuple[int, ...]:
        phase_deadline = min(deadline, time.monotonic() + timeout)
        latest = None
        while time.monotonic() < phase_deadline:
            log = read_log(log_path)
            _, marker, section = log.rpartition("\nMP_MVD_POLL_" + label + " ")
            found = STATUS.search(section) if marker else None
            if found:
                latest = tuple(map(int, found.groups()))
                if predicate(latest):
                    return latest
            if process.poll() is not None:
                raise RuntimeError(f"engine ended during {label}: {process.returncode}")
            if re.search(r"ERROR:|FATAL(?: ERROR|:)|MVD playback failed|Unknown command", log):
                raise RuntimeError(f"engine error during {label}")
            time.sleep(0.2)
        raise RuntimeError(f"timeout during {label}: last status {latest}")

    def advance(label: str, actions: list[str], predicate, timeout: int = 45) -> tuple[int, ...]:
        nonlocal loop, phase_number
        phase_number += 1
        following = game / f"mvd_wait_{phase_number}.cfg"
        write_script(following, ["waitMsec 200", "echo MP_MVD_POLL_" + label, "mvdStatus", f"exec {following.name}"])
        write_script(loop, [*actions, f"exec {following.name}"])
        loop = following
        return wait_state(label, predicate, timeout)

    def reached(state: tuple[int, ...], target: int) -> bool:
        return state[:3] == (1, 1, 0) and target <= state[3] <= target + 100

    with (output / "console.log").open("w", encoding="utf-8") as stream:
        process = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT)
        try:
            initial = wait_state("INITIAL", lambda s: reached(s, early), timeout=args.startup_timeout)
            startup_seconds = round(time.time() - started, 3)
            if initial[4] <= later_time + 1000:
                raise RuntimeError("recording is too short for the requested forward seek")
            selected = advance("FOLLOW", ["mvdFollowNext"],
                               lambda s: reached(s, early) and s[5] >= 0, timeout=10)[5]
            camera_states["first"] = selected
            camera_states["next"] = advance("NEXT", ["mvdFollowNext"],
                lambda s: reached(s, early) and s[5] >= 0 and s[5] != selected, timeout=10)[5]
            camera_states["free"] = advance("FREE", [*capture("next"), "mvdFreeRoam"],
                lambda s: reached(s, early) and s[5] == -1, timeout=10)[5]
            selected = advance("REFOLLOW", [*capture("free"), "mvdFollowNext"],
                lambda s: reached(s, early) and s[5] >= 0, timeout=10)[5]
            camera_states["refollow"] = selected
            if args.boundary_seconds is not None:
                boundary = args.boundary_seconds * 1000
                camera_states["boundary"] = advance("BOUNDARY", [*capture("playing"), f"mvdSeek {args.boundary_seconds}"],
                    lambda s: reached(s, boundary))[5]
                later_actions = [*capture("boundary"), *(["mvdFreeRoam"] if args.cancel_boundary_follow else []),
                                 f"mvdSeek {args.later_seconds}"]
            else:
                later_actions = [*capture("playing"), f"mvdSeek {args.later_seconds}"]
            expected_follow = -1 if args.cancel_boundary_follow else selected
            if args.play_to_later:
                advance("LATER_PLAY", [*capture("playing"), "mvdPause 0"],
                        lambda s: s[0] == 1 and s[2] == 0 and s[3] >= later_time,
                        timeout=args.later_seconds - args.early_seconds + 30)
                later = advance("LATER", ["mvdPause 1"],
                                lambda s: s[:3] == (1, 1, 0) and s[3] >= later_time)
            else:
                later = advance("LATER", later_actions, lambda s: reached(s, later_time))
            if later[5] != expected_follow:
                failures.append(("continuous playback" if args.play_to_later else "forward seek") + " lost the selected camera")
            rewind = advance("REWIND", [*capture("later"), f"mvdSeek {args.early_seconds}"], lambda s: reached(s, early))
            if rewind[5] != expected_follow:
                failures.append("rewind lost the selected camera")
            advance("STEP", ["mvdStep 2"], lambda s: reached(s, early + 16))
            advance("END", [*capture("rewound"), "demoMenu", "waitMsec 1000",
                    'screenshot "screenshots/controls.tga"', "mvdSpeed 4", "mvdPause 0"],
                    lambda s: s[0] == 0, timeout=60)
            write_script(loop, ["echo MP_MVD_COMPLETE", "quit"])
            code = process.wait(timeout=max(1, deadline - time.monotonic()))
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            failures.append(str(error))
            write_script(loop, ["openq4_reportMPState", "mvdStatus", "quit"])
            try:
                code = process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()  # Only this test's process.
                code = process.wait()
    log = read_log(log_path)
    if args.library_play:
        library = re.search(r"DEMO_LIBRARY count=(\d+) visible=(\d+) selected=(-?\d+) canPlay=(\d+) truncated=(\d+)", log)
        entry = re.search(r"DEMO_LIBRARY_ENTRY index=0 type=\d+ playable=1 capabilities=\d+ path=([^\r\n]+)", log)
        if (not library or tuple(map(int, library.groups())) != (1, 1, 0, 1, 0) or
                not entry or entry[1] != "demos/" + source.name):
            failures.append("the Demos library did not select the linked playable recording")
    if code != 0:
        failures.append(f"engine exit {code}")
    if "clean end: yes" not in log:
        failures.append("MVD inspection did not find a clean end")
    if not re.search(r"Stopped MVD playback of '[^']+' \(complete\)", log):
        failures.append("playback did not consume the linked recording's end record")
    if re.search(r"ERROR:|FATAL(?: ERROR|:)|Invalid MVD|MVD playback failed|No MVD is playing|Unknown command", log):
        failures.append("engine reported a playback error")
    match_view_rejections = log.count("ignored malformed competitive match view")
    if match_view_rejections and not args.allow_development_view_mismatch:
        failures.append("recording contains incompatible or malformed competitive match views")
    for marker in ("PLAYING", "LATER", "REWOUND", "COMPLETE"):
        if "\nMP_MVD_" + marker + " " not in log:
            failures.append("missing playback marker " + marker)
    states = []
    for marker in ("PLAYING", "LATER", "REWOUND"):
        found = STATUS.search(log.partition("\nMP_MVD_" + marker + " ")[2])
        if found:
            states.append(tuple(map(int, found.groups())))
    if (len(states) != 3 or any(s[0] != 1 or s[2] != 0 for s in states) or
            states[0][5] < 0 or any(s[5] != expected_follow for s in states[1:])):
        failures.append("playback did not retain the selected camera through seek/rewind")
    elif not (later_time <= int(states[1][3]) <= later_time + (1000 if args.play_to_later else 100) and
              early <= int(states[2][3]) <= early + 500):
        failures.append("seek or rewind did not reach the requested playback time")
    camera_poses = {}
    for marker in ("PLAYING", "LATER", "REWOUND"):
        section = log.partition("\nMP_MVD_" + marker + " ")[2].partition("\nMP_MVD_")[0]
        state = STATUS.search(section)
        world = re.search(r'MP_WORLD entity=(\d+) [^\r\n]* health=(-?\d+) origin="([^"]+)"', section)
        view = re.search(r'MP_WORLD_VIEW entity=(\d+) think=\d+ eye="([^"]+)" render="([^"]+)"', section)
        if state and world and view and view[3] != "unavailable":
            origin, eye, rendered = (list(map(float, value.split())) for value in (world[3], view[2], view[3]))
            camera_poses[marker.lower()] = {
                "follow": int(state[6]), "entity": int(world[1]), "view_entity": int(view[1]),
                "health": int(world[2]), "origin": origin, "eye": eye, "render": rendered,
                "eye_height": round(eye[2] - origin[2], 3),
                "render_height": round(rendered[2] - origin[2], 3),
            }
        if args.standing_camera_height is not None:
            pose = camera_poses.get(marker.lower())
            if (pose is None or pose["follow"] != 0 or pose["entity"] != 0 or pose["view_entity"] != 0 or
                    pose["health"] <= 0 or any(abs(pose[key] - args.standing_camera_height) > 0.1
                                             for key in ("eye_height", "render_height"))):
                failures.append(f"{marker.lower()} did not present the followed player's standing eye height")
    images = [game / f"screenshots/{name}.tga" for name in shots]
    if not all(p.is_file() and p.stat().st_size > 18 and p.stat().st_mtime >= started for p in images):
        failures.append("missing fresh engine screenshots")
    report = {"status": "fail" if failures else "pass", "failures": failures, "exit": code,
              "startup_seconds": startup_seconds, "startup_timeout": args.startup_timeout,
              "library_play": args.library_play,
              "source_report": str(report_path), "source_mvd": str(source),
              "session_id": evidence["sessionId"], "journal_events": len(events), "camera_states": camera_states,
              "seek_msec": {"early": early, "later": later_time},
              "boundary_seconds": args.boundary_seconds,
              "cancel_boundary_follow": args.cancel_boundary_follow,
              "standing_camera_height": args.standing_camera_height, "camera_poses": camera_poses,
              "play_to_later": args.play_to_later,
              "match_view_rejections": match_view_rejections,
              "allow_development_view_mismatch": args.allow_development_view_mismatch,
              "result": results[0]["data"], "log": str(log_path),
              "screenshots": [str(p) for p in images]}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
