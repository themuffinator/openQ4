#!/usr/bin/env python3
"""Replay DM8/CTF5 recovery failures and surviving controls from 3 November.

The later Rhodes pose is the rounded BOT_AIR_RECOVER observation, not a full
match snapshot. Voss's rocket state is the recorded BOT_IMPULSE observation.
These use real player physics and isolated bot commands, with no OS input.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from mp_bot_impulse_replay import assess_trials, replay_identity, replay_session

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--port", type=int, default=29080)
    parser.add_argument("--map", choices=("all", "dm8", "ctf5"), default="all")
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    if output.exists():
        parser.error("use a new output directory to preserve earlier evidence")
    trials = []
    pose = "420.913 742.652 328.250 695.264 -339.153 -79.632"
    for name, target in (("southwest", "300 600 328.25"),
                         ("northwest", "300 900 328.25"), ("south", "420 500 328.25")):
        trials.append({"name": name, "decisive": True, "expect_survival": True,
                       "setup": ["bot_probeHealth 1 5 200"],
                       "command": f"bot_probeMove 1 {pose} {target}"})
    # A later rounded pose is already beyond recovery: all sixteen headings,
    # coasting and the controller died in the recorded envelope. Keep one
    # negative control so respawn/first-touch cannot masquerade as survival.
    trials.append({"name": "later_lost", "decisive": True, "expect_survival": False,
                   "setup": ["bot_probeHealth 1 5 24"],
                   "command": "bot_probeMove 1 528.1 686 328.2 645 -364.5 0 528.1 686 328.2"})
    pose = "383.691 -782.427 328.250 200.367 -591.175 189.748"
    trials += [
        {"name": "rocket_controller", "decisive": True, "expect_survival": True,
         "setup": ["bot_probeHealth 1 53 134"],
         "command": f"bot_probeMove 1 {pose} 383.691 -782.427 328.250"},
        {"name": "rocket_north", "decisive": True, "expect_survival": True,
         "setup": ["bot_probeHealth 1 53 134"],
         "command": f"bot_probeAir 1 {pose} 0 1 0"},
    ]
    pose = "103.247 -656.602 328.250 -172.655 -590.704 333.543"
    trials += [
        {"name": "raised_lip_controller", "decisive": True, "expect_survival": True,
         "setup": ["bot_probeHealth 1 18 180"],
         "command": f"bot_probeMove 1 {pose} 103.247 -656.602 328.250"},
        {"name": "raised_lip_northeast", "decisive": True, "expect_survival": True,
         "setup": ["bot_probeHealth 1 18 180"],
         "command": f"bot_probeAir 1 {pose} 0.7071 0.7071 0"},
    ]
    executable = "openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"
    pose = "350.388 1160.896 0.250 184.511 -712.429 -33.655"
    ctf_trials = [
        {"name": "rim_controller", "decisive": True, "expect_survival": True,
         "setup": ["bot_probeHealth 1 14 200"],
         "command": f"bot_probeMove 1 {pose} 350.388 1160.896 0.250"},
        {"name": "rim_northeast", "decisive": True, "expect_survival": True,
         "setup": ["bot_probeHealth 1 14 200"],
         "command": f"bot_probeAir 1 {pose} 0.3827 0.9239 0"},
    ]
    identity = replay_identity(runtime, executable)
    complete, outcomes, codes = True, [], {}
    for name, game_map, mode, cases in (("dm8", "mp/q4dm8", "DM", trials),
                                        ("ctf5", "mp/q4ctf5", "CTF", ctf_trials)):
        if args.map not in ("all", name):
            continue
        done, log, code = replay_session(runtime, executable, args.basepath.resolve(), output / name,
                                         game_map, mode, cases, args.port, 350)
        complete = complete and done
        codes[name] = code
        outcomes += [{**trial, "map": game_map} for trial in assess_trials(log, cases)]
    unchanged = replay_identity(runtime, executable) == identity
    passed = complete and unchanged and all(
        trial["armed"] and (trial["survived"] if trial["expect_survival"] else trial["died"])
        for trial in outcomes)
    report = {"passed": passed, "complete": complete, "exit_codes": codes,
              "runtime_identity": identity, "runtime_unchanged": unchanged, "trials": outcomes}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": passed, "trials": [
        {key: trial[key] for key in ("name", "armed", "survived", "reason")} for trial in outcomes]}, indent=2))
    return int(not passed)


if __name__ == "__main__":
    raise SystemExit(main())
