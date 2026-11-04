#!/usr/bin/env python3
"""Replay the three recoverable long-match hit states recorded on 4 November.

These are positive recovery regressions, not full match snapshots or causal
exclusions. The weak-hit cases enter through the real knockback handler instead
of the ordinary MOVE probe's forced recovery timestamp.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from mp_bot_impulse_replay import assess_trials, replay_identity, replay_session

ROOT = Path(__file__).resolve().parents[2]
CASES = {
    "dm8": ("DM", [{
        "name": "damaged_jump", "probe_type": "JUMP",
        "setup": ["bot_probeHealth 1 2 50 1.514 34.961 -0.643"],
        "command": "bot_probeJump 1 -75.655 891.068 616.708 24.985 30.71 250.885 -44 868 328.25",
    }]),
    "ctf3": ("CTF", [{
        "name": "weak_nail_walk",
        "setup": ["bot_probeHealth 1 113 52 -49.744 57.825 105.27"],
        "command": "bot_probeWalk 1 2971.629 3232.276 -255.75 122.007 367.183 105.27 3108 3108 -255.75",
    }]),
    "ctf5": ("CTF", [{
        "name": "rail_flight",
        "setup": ["bot_probeHealth 1 10 200 19.531 -499.058 -23.655"],
        "command": "bot_probeWalk 1 568.824 74.316 -33.102 19.531 146.841 299.182 580 884 -183.75",
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
        done, log, code = replay_session(runtime, executable, args.basepath.resolve(), output / name,
                                        "mp/q4" + name, mode, cases, args.port, 180)
        complete = complete and done
        codes[name] = code
        outcomes += [{**trial, "map": name} for trial in assess_trials(log, cases)]
    unchanged = replay_identity(runtime, executable) == identity
    passed = complete and unchanged and all(trial["survived"] for trial in outcomes)
    report = dict(passed=passed, complete=complete, exit_codes=codes, runtime_identity=identity,
                  runtime_unchanged=unchanged, trials=outcomes,
                  scope="Positive hit recovery only; no causal exclusions.")
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return int(not passed)


if __name__ == "__main__":
    raise SystemExit(main())
