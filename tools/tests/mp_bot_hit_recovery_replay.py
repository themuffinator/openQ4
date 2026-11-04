#!/usr/bin/env python3
"""Replay the recoverable long-match hit states recorded on 4 November.

These are positive recovery regressions, not full match snapshots or causal
exclusions. The weak-hit cases enter through the real knockback handler instead
of the ordinary MOVE probe's forced recovery timestamp.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from mp_bot_impulse_replay import replay_identity, replay_session
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
        "setup": ["bot_probeHealth 1 12 154 170.551 -246.454 241.656 0"],
        "command": "bot_probeMove 1 -24.912 -761.135 328.25 274.678 -581.521 241.656 -24.912 -761.135 328.25",
    }]),
    "ctf3": ("CTF", [{
        "name": "weak_nail_walk",
        "setup": ["bot_probeHealth 1 113 52 -49.744 57.825 105.27"],
        "command": "bot_probeWalk 1 2971.629 3232.276 -255.75 122.007 367.183 105.27 3108 3108 -255.75",
    }]),
    "ctf5": ("CTF", [{
        "name": "rail_flight",
        "observe_msec": 12000, "allow_resumed_navigation": True,
        "setup": ["bot_probeHealth 1 10 200 19.531 -499.058 -23.655"],
        "command": "bot_probeWalk 1 568.824 74.316 -33.102 19.531 146.841 299.182 580 884 -183.75",
    }, {
        "name": "diagonal_pad_catch",
        "setup": ["bot_probeHealth 1 95 50 21.272 -12.727 3.242"],
        "command": "bot_probeMove 1 721.956 854.658 -96.145 431.422 -398.978 654.039 721.956 854.658 -96.145",
    }]),
    "ctf7": ("CTF", [{
        "name": "armoured_drop", "probe_type": "DROP",
        "setup": ["bot_probeHealth 1 3 0 0 0 0 100"],
        "command": "bot_probeDrop 1 -206 -819 256.25 -222 221 0 -236 -788 -111.75 -212 -812 256.25",
    }]),
}


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
        done, log, code = replay_session(runtime, executable, args.basepath.resolve(), output / name,
                                        "mp/q4" + name, mode, cases, args.port, 180)
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
