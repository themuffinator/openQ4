#!/usr/bin/env python3
"""Replay CTF5's rail-displaced drop entry and its settled/aligned controls."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re

from mp_bot_impulse_replay import replay_session

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    if output.exists():
        parser.error("use a new output directory to preserve earlier evidence")
    trials = [
        dict(name="hit", command="bot_probeDrop 1 669.114 1108.080 .25 457.366 6.455 -27.629 652 1076 -183.75 676 1100 .25",
             setup=["set bot_debug 4", "bot_probeHealth 1 21 200"]),
        dict(name="settled", command="bot_probeDrop 1 695 1084 .25 0 0 0 652 1076 -183.75 676 1100 .25",
             setup=["set bot_debug 4", "bot_probeHealth 1 21"]),
        dict(name="aligned", command="bot_probeDrop 1 676 1100 .25 0 0 0 652 1076 -183.75 676 1100 .25",
             setup=["set bot_debug 4", "bot_probeHealth 1 21"]),
    ]
    exe = "openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"
    files = [runtime / exe, *[p for p in (runtime / "baseoq4").glob("game_*")
                             if p.suffix in (".dll", ".so", ".dylib")],
             ROOT / "src/game/bots/Bot.cpp", ROOT / "src/game/gamesys/SysCmds.cpp"]
    identity = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    completed, log, code = replay_session(runtime, exe, args.basepath.resolve(), output,
                                          "mp/q4ctf5", "CTF", trials, 28973, 180)
    outcomes = []
    for trial in trials:
        begin = f"MP_BOT_TRIAL_BEGIN {trial['name']}\n"
        end = f"MP_BOT_TRIAL_END {trial['name']}\n"
        sample = log.partition(begin)[2].partition(end)[0]
        outcomes.append(dict(name=trial["name"],
            armed=bool(sample and end in log and "BOT_DROP_PROBE client=1 " in sample),
            completed_drop=bool(re.search(r"^bot Rhodes: completed drop .* to 652 1076 -184 at ", sample, re.M)),
            stuck=bool(re.search(r"^bot Rhodes: (?:stuck at|giving up|.*traversal timed out)", sample, re.M)),
            died=bool(re.search(r"^Rhodes (?:was killed by world|died)$", sample, re.M))))
    stable = all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == h for p, h in identity.items())
    passed = completed and stable and all(t["armed"] and t["completed_drop"] and not t["stuck"] and not t["died"] for t in outcomes)
    report = dict(passed=passed, exit_code=code, identity=identity, trials=outcomes,
                  scope="Recorded post-rail state, rounded stuck pose and aligned entry; isolated bot, no later hits or peers.")
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return int(not passed)


if __name__ == "__main__":
    raise SystemExit(main())
