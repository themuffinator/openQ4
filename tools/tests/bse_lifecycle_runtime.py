#!/usr/bin/env python3
"""Opt-in, input-free stock BSE lifecycle probe (Q4-03); needs retail assets.

Checks telemetry, not visual or audible parity. Runs the staged client windowed
and hidden with isolated savepaths. The effect is stock content, never copied or
changed. The regular cross-load harness also checks every save reference.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys

import savegame_cross_load as saves

EFFECT = "effects/levels/convoy/weld_sparks"
COUNTERS = re.compile(r"bse_active: (\d+) particles: (\d+) traces: (\d+) segments: (\d+)")
MARKERS = ["START", "STOP", "RESTART", "DELETE", "END"]


def samples(text: str) -> list[tuple[int, ...]]:
    return [tuple(map(int, match)) for match in COUNTERS.findall(text)]


def active(text: str) -> bool:
    frames = samples(text)
    return bool(frames) and all(max(frame[i] for frame in frames) > 0 for i in range(4))


def quiet(text: str) -> bool:
    frames = samples(text)
    # Allow dying particles time to finish, then require sustained inactivity.
    return len(frames) >= 20 and all(frame[:3] == (0, 0, 0) for frame in frames[-20:])


def check(write_log: str, load_log: str) -> dict[str, bool]:
    phases = {}
    for start, end in zip(MARKERS, MARKERS[1:]):
        begin = f"OPENQ4_BSE_{start} "
        finish = f"OPENQ4_BSE_{end} "
        _, seen_begin, tail = write_log.partition(begin)
        phase, seen_end, _ = tail.partition(finish)
        phases[start] = phase if seen_begin and seen_end else ""
    return {
        "started": active(phases["START"]),
        "stopped": quiet(phases["STOP"]),
        "restarted": active(phases["RESTART"]),
        "deleted": quiet(phases["DELETE"]),
        "restored": active(load_log.partition("OPENQ4_BSE_RESTORED ")[2]),
    }


def self_test() -> None:
    live = "bse_active: 1 particles: 20 traces: 5 segments: 4\n"
    dead = "bse_active: 0 particles: 0 traces: 0 segments: 0\n" * 20
    text = "".join(f"OPENQ4_BSE_{mark} \n{value}" for mark, value in
                   zip(MARKERS, (live, dead, live, dead, "")))
    restored = "OPENQ4_BSE_RESTORED \n" + live
    assert all(check(text, restored).values())
    assert not check(text.replace("traces: 5", "traces: 0"), restored)["started"]
    assert not check(text.replace(dead, live), restored)["stopped"]
    assert not check(text.replace("OPENQ4_BSE_END", "MISSING_END"), restored)["deleted"]
    assert not check(text, "")["restored"]
    assert not any(check("", "").values())
    print("bse_lifecycle_runtime: self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--install-root", type=Path)
    parser.add_argument("--basepath", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--timeout", type=float, default=300)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.install_root, args.basepath, args.output)):
        parser.error("runtime requires --install-root, --basepath and --output")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    common = ["--install-root", str(args.install_root.resolve()), "--basepath", str(args.basepath.resolve()),
              "--timeout", str(args.timeout), "--set", "r_renderApi", args.renderer,
              "--set", "bse_speeds", "1", "--set", "bse_singleEffect", EFFECT]
    commands = ["echo OPENQ4_BSE_START",
                f"spawn func_fx name q4_bse_probe fx {EFFECT} loop 1 start_on 1", "wait 90",
                "echo OPENQ4_BSE_STOP", "trigger q4_bse_probe", "wait 240",
                "echo OPENQ4_BSE_RESTART", "trigger q4_bse_probe", "wait 90",
                "saveGame bse_live", "wait 5", "echo OPENQ4_BSE_DELETE",
                "remove q4_bse_probe", "wait 240", "echo OPENQ4_BSE_END"]
    write_args = saves.parse_args(["write", *common, "--home", str(output / "write"),
                                  "--tag", "finished", "--map", "game/airdefense1", "--settle-frames", "5",
                                  *[part for command in commands for part in ("--after-load", command)]])
    if saves.run_leg(write_args):
        return 1
    write_log = output / "write/baseoq4/logs" / saves.LOG_NAME
    if "Saved 'bse_live'" not in write_log.read_text(encoding="utf-8", errors="replace"):
        print("bse_lifecycle_runtime: live-effect save did not commit", file=sys.stderr)
        return 1
    load_args = saves.parse_args(["load", *common, "--home", str(output / "load"),
                                 "--tag", "restored", "--play-frames", "120",
                                 "--input", str(output / "write/baseoq4/savegames/bse_live.save"),
                                 "--after-load", "echo OPENQ4_BSE_RESTORED"])
    if saves.run_leg(load_args):
        return 1
    logs = [output / leg / "baseoq4/logs" / saves.LOG_NAME for leg in ("write", "load")]
    checks = check(*(path.read_text(encoding="utf-8", errors="replace") for path in logs))
    report = {"effect": EFFECT, "renderer": args.renderer, "checks": checks,
              "passed": all(checks.values()), "logs": [str(path) for path in logs],
              "scope": "telemetry and save lifecycle; no visual, audio or retail-parity acceptance"}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
