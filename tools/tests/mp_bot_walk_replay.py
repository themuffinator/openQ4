#!/usr/bin/env python3
"""Replay DM8 WALK-rim falls and jump-approach controls without device input."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path

from mp_bot_impulse_replay import assess_trials, replay_session
from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--port", type=int, default=28973)
    parser.add_argument("--expect-unsafe", action="store_true", help="qualify the pre-fix two-fall control")
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    if output.exists() or not (args.basepath / "q4base").is_dir() or not 1024 <= args.port <= 65535:
        parser.error("use a new evidence directory, installed retail assets and an unprivileged port")
    executable = "openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"
    verify_runtime_is_current(runtime, executable)
    trials = []
    # These first two grounded poses precede the retained unhit match death.
    # Direct approaches survive; diagonal steering reproduces an unsafe fall.
    # It does not establish the original match's exact input sequence.
    for name, state, target in (
        ("early", "556.39 705.44 328.25 76.58 -5.78 0", "628 700 328.25"),
        ("late", "588.68 689.35 328.25 72.18 -26.23 0", "628 700 328.25"),
        ("diagonal", "588.68 689.35 328.25 72.18 -26.23 0", "652 660 328.25"),
    ):
        for approach in (False, True):
            trials.append({"name": name + ("Jump" if approach else "Walk"),
                "command": f"bot_probeWalk 1 {state} {target}" + (" 508 580 328.25" if approach else ""),
                "setup": ["bot_probeHealth 1 111", "set bot_debug 3", "navmesh probe 628 700 328.25"]})
    paths = [runtime / executable, *sorted((runtime / "baseoq4").glob("game_*")),
        *sorted(runtime.glob("renderer-*")), runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4",
        runtime / "baseoq4/mod.json"]
    paths = [p for p in paths if p.is_file() and p.suffix not in (".pdb", ".lib", ".exp")]
    def identity():
        return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    before = identity()
    complete, log, code = replay_session(runtime, executable, args.basepath.resolve(), output,
        "mp/q4dm8", "DM", trials, args.port, 180)
    outcomes = assess_trials(log, trials)
    failures = []
    if not complete or code != 0:
        failures.append("isolated replay did not finish cleanly")
    if identity() != before:
        failures.append("runtime changed during replay")
    for row in outcomes:
        unsafe = args.expect_unsafe and row["name"].startswith("diagonal")
        if not row["armed"] or (not row["died"] if unsafe else not row["survived"]):
            failures.append(f"{row['name']}: {row['reason']}")
    report = {"status": "fail" if failures else "pass", "expected_unsafe_control": args.expect_unsafe,
        "complete": complete, "exit": code, "identity": before, "trials": outcomes, "failures": failures}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "failures": failures, "report": str(output / "report.json")}, indent=2))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
