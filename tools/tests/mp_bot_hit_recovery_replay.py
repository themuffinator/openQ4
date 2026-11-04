#!/usr/bin/env python3
"""Replay recoverable long-match hit and off-route states recorded on 4 November.

These are positive recovery regressions, not full match snapshots or causal
exclusions. The weak-hit cases enter through the real knockback handler instead
of the ordinary MOVE probe's forced recovery timestamp.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from mp_bot_impulse_replay import (
    TRIAL_OBSERVE_MSEC, TRIAL_REMOVE_MSEC, TRIAL_RESPAWN_MSEC,
    replay_identity, replay_session,
)
from mp_bot_air_recovery_replay import assess_recovery_trials

ROOT = Path(__file__).resolve().parents[2]
CASES = {
    "dm8": ("DM", [{
        "name": "damaged_jump", "probe_type": "JUMP",
        "observe_msec": 12000, "allow_resumed_navigation": True,
        "setup": ["bot_probeHealth 1 2 50 1.514 34.961 -0.643"],
        "command": "bot_probeJump 1 -75.655 891.068 616.708 24.985 30.71 250.885 -44 868 328.25",
    }, {
        "name": "bevel_descent",
        "setup": ["bot_probeHealth 1 7 50 -16.301 -22.592 21.187"],
        "command": "bot_probeMove 1 -170.137 169.962 860.787 273.625 226.057 -332.896 -170.137 169.962 860.787",
    }, {
        "name": "coasting_pad_catch",
        "setup": ["bot_probeHealth 1 74 50 0.685 -30.386 17.357"],
        "command": "bot_probeMove 1 -444.388 53.313 982.004 -306.296 -13.552 84.265 -444.388 53.313 982.004",
    }, {
        "name": "rocket_ground_slide",
        "setup": ["bot_probeHealth 1 67 200 510.996 195.932 -54.711"],
        "command": "bot_probeMove 1 828.721 -10.581 324.25 352.019 387.219 -54.711 828.721 -10.581 324.25",
    }, {
        "name": "rocket_rim_runout",
        "observe_msec": 12000, "allow_resumed_navigation": True,
        "setup": ["bot_probeHealth 1 12 154 170.551 -246.454 241.656 0"],
        "command": "bot_probeMove 1 -24.912 -761.135 328.25 274.678 -581.521 241.656 -24.912 -761.135 328.25",
    }, {
        "name": "rocket_player_deflection", "observe_msec": 12000,
        "allow_resumed_navigation": True,
        "setup": ["noclip", "setviewpos 700 60 68", "waitMsec 64",
                  "bot_probeHealth 1 66 190 259.792 -144.761 370.375 1"],
        "command": "bot_probeMove 1 544.251 70.468 0.25 601.687 -126.201 370.375 544.251 70.468 0.25",
    }, {
        "name": "fall_player_contact", "observe_msec": 12000,
        "setup": ["noclip", "setviewpos 475 342 68", "waitMsec 64",
                  "bot_probeHealth 1 31 50 5.182 12.799 32.161 0"],
        "command": "bot_probeWalk 1 446.359 355.924 134.854 171.735 -81.538 -465.89 532 364 0.25",
    }]),
    "ctf3": ("CTF", [{
        "name": "weak_nail_walk",
        "setup": ["bot_probeHealth 1 113 52 -49.744 57.825 105.27"],
        "command": "bot_probeWalk 1 2971.629 3232.276 -255.75 122.007 367.183 105.27 3108 3108 -255.75",
    }, {
        "name": "ramp_walk_ground", "observe_msec": 12000,
        "allow_resumed_navigation": True,
        "setup": ["bot_probeHealth 1 25 0 0 0 0 0"],
        "command": "bot_probeWalk 1 -521.9 -570.4 -287.75 -91.6 277 0 -540 -492 -277",
    }, {
        "name": "ramp_walk_air", "observe_msec": 18000,
        "allow_resumed_navigation": True,
        "setup": ["bot_probeHealth 1 25 0 0 0 0 0"],
        "command": "bot_probeWalk 1 -550.081 -516.115 -282.25 -62.339 261.318 111.758 -540 -492 -277",
    }, {
        "name": "ramp_pad_approach_air", "probe_type": "PAD", "observe_msec": 18000,
        "allow_resumed_navigation": True,
        # Prime airborne physics before restoring the pose. The common neutral
        # settle otherwise leaves a grounded flag from the ramp below it and
        # permits one artificial walking command before the flight controller.
        "neutral_command": "bot_probeAir 1 -550.081 -516.115 -218.25 0 0 0 0 0 0",
        "setup": ["bot_probeHealth 1 25 0 0 0 0 0"],
        "command": "bot_probePad 1 -550.081 -516.115 -282.25 -62.339 261.318 111.758 84 84 -31.75 func_jumppad_10 -540 -492 -277",
    }]),
    "ctf5": ("CTF", [{
        "name": "pending_climb", "observe_msec": 12000,
        # One frame changes the fresh-jump pulse: the old controller jumps
        # before its first grounded brake and falls off the CTF5 walkway.
        "setup": ["waitMsec 1", "bot_probeHealth 1 33 0 0 0 0 0"],
        "command": "bot_probeClimb 1 619.928 787.929 -182.842 348.87 -10.243 -92.217 556 764 -172.305",
    }, {
        "name": "pending_climb_braked", "observe_msec": 12000,
        "setup": ["bot_probeHealth 1 33 0 0 0 0 0"],
        "command": "bot_probeClimb 1 619.928 787.929 -182.842 348.87 -10.243 -92.217 556 764 -172.305",
    }, {
        "name": "rail_flight",
        "observe_msec": 12000, "allow_resumed_navigation": True,
        "setup": ["bot_probeHealth 1 10 200 19.531 -499.058 -23.655"],
        "command": "bot_probeWalk 1 568.824 74.316 -33.102 19.531 146.841 299.182 580 884 -183.75",
    }, {
        "name": "diagonal_pad_catch",
        "setup": ["bot_probeHealth 1 95 50 21.272 -12.727 3.242"],
        "command": "bot_probeMove 1 721.956 854.658 -96.145 431.422 -398.978 654.039 721.956 854.658 -96.145",
    }, {
        "name": "rocket_ramp_glance",
        "observe_msec": 12000,
        "setup": ["bot_probeHealth 1 60 124 56.315 -252.725 170.467 18"],
        "command": "bot_probeMove 1 618.9 983.089 -183.75 -117.871 -521.892 170.467 604 956 -183.75",
    }, {
        "name": "wall_runout_controller", "observe_msec": 12000,
        "release_input_after_msec": 2000,
        "setup": ["bot_probeHealth 1 45 50 13.023 21.29 -1.463 0"],
        "command": "bot_probeMove 1 583.431 120.247 -61.195 18.727 -441.232 -369.033 583.431 120.247 -61.195",
    }, {
        "name": "wall_runout_fixed_coast", "observe_msec": 12000,
        "setup": ["bot_probeHealth 1 45 50 13.023 21.29 -1.463 0"],
        "command": "bot_probeAir 1 583.431 120.247 -61.195 18.727 -441.232 -369.033 0 0 0",
    }, {
        "name": "pad_catch_approach", "observe_msec": 12000,
        "allow_resumed_navigation": True,
        "setup": ["bot_probeHealth 1 9 50 8.927 -23.186 2.781 0"],
        "command": "bot_probeMove 1 1090.429 109.865 154.281 410.068 368.839 -76.021 1090.429 109.865 154.281",
    }]),
    "ctf7": ("CTF", [{
        "name": "armoured_drop", "probe_type": "DROP",
        "setup": ["bot_probeHealth 1 3 0 0 0 0 100"],
        "command": "bot_probeDrop 1 -206 -819 256.25 -222 221 0 -236 -788 -111.75 -212 -812 256.25",
    }]),
}

# A normal WALK probe cannot exercise the no-route recovery that entered an
# unplanned CTF5 pad. The westward approaches die before the transport guard;
# all four must remain alive and grounded for twelve seconds after the fix.
for start_name, pose in (("slow", "771.99 -297.27 -183.75 18.71 18.61 0"),
                         ("moving", "765.95 -303.31 -183.75 -138.13 -138.19 0")):
    for direction, target in (("west", "652 -336 -183.75"), ("diagonal", "652 -420 -183.75")):
        CASES["ctf5"][1].append({
            "name": f"off_route_{start_name}_{direction}", "observe_msec": 12000,
            "setup": ["bot_probeHealth 1 9 0 0 0 0 0"],
            "command": f"bot_probeRecover 1 {pose} {target}",
        })


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--map", choices=("all", *CASES), default="all")
    parser.add_argument("--port", type=int, default=29320)
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    if output.exists():
        parser.error("use a new output directory to preserve earlier evidence")
    executable = "openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"
    identity = replay_identity(runtime, executable)
    complete, outcomes, codes = True, [], {}
    for name, (mode, cases) in CASES.items():
        if args.map not in ("all", name):
            continue
        cases = [{**trial, "expect_survival": True} for trial in cases]
        observation = sum(trial.get("observe_msec", TRIAL_OBSERVE_MSEC) +
                          TRIAL_REMOVE_MSEC + TRIAL_RESPAWN_MSEC + 1200 for trial in cases)
        timeout = 150 + observation * 3 // 1000
        done, log, code = replay_session(runtime, executable, args.basepath.resolve(), output / name,
                                        "mp/q4" + name, mode, cases, args.port, timeout)
        complete = complete and done
        codes[name] = code
        outcomes += [{**trial, "map": name} for trial in assess_recovery_trials(log, cases)]
    unchanged = replay_identity(runtime, executable) == identity
    passed = complete and unchanged and all(trial["armed"] and trial["recovery_passed"] for trial in outcomes)
    report = dict(passed=passed, complete=complete, exit_codes=codes, runtime_identity=identity,
                  runtime_unchanged=unchanged, trials=outcomes,
                  scope="Positive hit recovery only; no causal exclusions.")
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return int(not passed)


if __name__ == "__main__":
    raise SystemExit(main())
