#!/usr/bin/env python3
"""Replay CTF5's recoverable rail hit with the controller and fixed-input controls."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path

from mp_bot_impulse_replay import assess_trials, replay_session, trials_for

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
    impulse = dict(origin=[614.047, 215.628, 19.222], velocity=[36.745, -168.359, 31.18],
                   kick=[36.745, 477.541, 143.544], health=7, damage=63, knock_msec=200)
    trials = [t for t in trials_for(impulse) if t["name"] in ("air11", "controller", "preController")]
    exe = "openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"
    files = [runtime / exe, *[p for p in (runtime / "baseoq4").glob("game_*")
                             if p.suffix in (".dll", ".so", ".dylib")],
             ROOT / "src/game/bots/Bot.cpp", ROOT / "src/game/bots/NavMesh.cpp"]
    identity = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    completed, log, code = replay_session(runtime, exe, args.basepath.resolve(), output,
                                           "mp/q4ctf5", "CTF", trials, 28973, 180)
    outcomes = assess_trials(log, trials)
    stable = all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == h for p, h in identity.items())
    passed = completed and stable and all(t["survived"] for t in outcomes)
    report = dict(passed=passed, exit_code=code, identity=identity, trials=outcomes)
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return int(not passed)


if __name__ == "__main__":
    raise SystemExit(main())
