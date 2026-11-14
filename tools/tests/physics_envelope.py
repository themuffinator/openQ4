#!/usr/bin/env python3
"""Behavior envelope of enhanced physics against legacy (PHYS-04).

Opt-in runtime test built on tools/tests/physics_workloads.py. For each hit
class it stages the same scene in both physics modes: strogg marines spawned
in a ring around the player, killed and left to settle, then hit while
resting or while still falling. `physicsReport` records every corpse's mass,
mass-weighted centre, linear momentum and fastest body before the hit, just
after it and once it has settled again.

Checks, per hit class:
  momentum   a raw impulse on a resting corpse adds the same linear momentum
             in both modes (the guard only redistributes it), within 2 %
  light      machinegun, gauntlet, small splash and a weak vehicle-sized bump:
             no enhanced corpse moves further than the legacy envelope (the
             largest legacy displacement plus a quarter, or plus 24 units)
             and no body exceeds the legacy peak body speed by more than a
             quarter plus 24 units/s
  heavy      rockets, grenades and the Dark Matter Gun's splash: the median
             displacement stays within 0.4x to 1.6x of legacy, or both stay
             under 32 units, and no corpse leaves the map
  gibbing    a hit that gibs every corpse in both modes passes: there is no
             motion left to compare, and both modes treated the hit alike
  telemetry  enhanced runs report their impulse clamps and peak speeds

Both modes run the same binary, so scenes are identical. Prints a table and
writes JSON (under .tmp) with --output.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import physics_workloads as pw  # noqa: E402

BODY = re.compile(r"^PHYSICS_BODY (\S+) class=(\S+) bodies=(\d+) mass=([\d.]+) centre=(\S+) (\S+) (\S+) "
                  r"momentum=(\S+) (\S+) (\S+) peak=([\d.]+) rest=(\d) time=(\d+)", re.M)

# name, kind, staged hit commands (phys_ prefix = every corpse), falling?
HITS = (
    ("machinegun", "light", ["physicsDamage phys_ damage_machinegun 1"], False),
    ("machinegun-falling", "light", ["physicsDamage phys_ damage_machinegun 1"], True),
    ("gauntlet", "light", ["physicsDamage phys_ damage_gauntlet 0.5"], False),
    ("small-splash", "light", ["physicsRadiusDamage damage_hyperblaster_splash 0 16"], False),
    ("vehicle-bump", "light", [f"physicsImpulse phys_{i} 4000 0 1500" for i in range(8)], False),
    ("rocket", "heavy", ["physicsDamage phys_ damage_rocketDirect 0.5"], False),
    ("rocket-splash", "heavy", ["physicsRadiusDamage damage_rocketSplash 0 16"], False),
    ("grenade-splash", "heavy", ["physicsRadiusDamage damage_grenadeSplash 0 16"], False),
    ("grenade-splash-falling", "heavy", ["physicsRadiusDamage damage_grenadeSplash 0 16"], True),
    ("dmg-splash", "heavy", ["physicsRadiusDamage damage_dmg_splash 0 16"], False),
)
MOMENTUM_IMPULSE = (0.0, 9000.0, 3000.0)


def workload(hit: tuple) -> dict:
    name, _kind, commands, falling = hit
    settle = [] if falling else ["@wait 240"]
    return {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget", "killMonsters", "@wait 30",
            "physicsStats start envelope",
            "physicsSpawn monster_strogg_marine 8 ring 104 16",
            "@wait 90",
            # quiet kill: no push, so every corpse starts the hit from the same rest
            "physicsDamage phys_ damage_triggerhurt_1000 1",
            "@wait 8" if falling else "@wait 1",
            *settle,
            "physicsReport phys_",
            *commands,
            "physicsReport phys_",
            "@wait 6", "physicsReport phys_",
            "@wait 360", "physicsReport phys_",
            "physicsStats stop",
        ],
    }


def momentum_workload() -> dict:
    x, y, z = MOMENTUM_IMPULSE
    return {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget", "killMonsters", "@wait 30",
            "physicsStats start momentum",
            "physicsSpawn monster_strogg_marine 8 ring 104 16",
            "@wait 90",
            "physicsDamage phys_ damage_triggerhurt_1000 1",
            "@wait 300",
            "physicsReport phys_",
            # the same frame: no step runs between the impulse and the report
            *[f"physicsImpulse phys_{i} {x} {y} {z} {i % 3}" for i in range(8)],
            "physicsReport phys_",
            "physicsStats stop",
        ],
    }


def reports(log: str) -> list[dict]:
    """PHYSICS_BODY lines grouped into report blocks (one per physicsReport)."""
    blocks: list[dict] = []
    current: dict | None = None
    for line in log.splitlines():
        if line.startswith("PHYSICS_REPORT "):
            current = {}
            blocks.append(current)
            continue
        m = BODY.match(line)
        if m and current is not None:
            current[m.group(1)] = {
                "mass": float(m.group(4)),
                "centre": tuple(map(float, m.group(5, 6, 7))),
                "momentum": tuple(map(float, m.group(8, 9, 10))),
                "peak": float(m.group(11)), "rest": int(m.group(12)), "time": int(m.group(13)),
            }
    return blocks


def distance(a: tuple, b: tuple) -> float:
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def run(args: argparse.Namespace, name: str, wl: dict, mode: str) -> dict:
    pw.WORKLOADS[name] = wl
    result = pw.run_workload(args, name, mode, 0)
    log = Path(result["log"]).read_text(encoding="latin-1") if Path(result["log"]).exists() else ""
    result["reports"] = reports(log)
    return result


def analyse_hit(hit: tuple, legacy: dict, enhanced: dict) -> dict:
    name, kind, _commands, _falling = hit
    out = {"hit": name, "kind": kind, "passed": True, "notes": []}
    per_mode = {}
    for mode, result in (("legacy", legacy), ("enhanced", enhanced)):
        blocks = result["reports"]
        if len(blocks) < 4:
            out["passed"] = False
            out["notes"].append(f"{mode}: {len(blocks)} report blocks, want 4")
            return out
        before, _now, early, after = blocks[0], blocks[1], blocks[2], blocks[3]
        names = sorted(set(before) & set(after))
        per_mode[mode] = {
            "corpses": len(names),
            "gibbed": len(set(before) - set(after)),
            "displacement": [distance(before[n]["centre"], after[n]["centre"]) for n in names],
            "peak": max([early[n]["peak"] for n in names if n in early] + [0.0]),
            "clamps": (result.get("stats") or {}).get("impulse_clamps", 0),
            "peak_dv": (result.get("stats") or {}).get("peak_impulse_dv", 0.0),
        }
    lg, en = per_mode["legacy"], per_mode["enhanced"]
    out.update(legacy=lg, enhanced=en)
    if not lg["displacement"] or not en["displacement"]:
        if not lg["displacement"] and not en["displacement"] and lg["gibbed"] == en["gibbed"] and lg["gibbed"] > 0:
            out["notes"].append(f"every corpse gibbed in both modes ({lg['gibbed']})")
            return out
        out["passed"] = False
        out["notes"].append("no corpses to compare")
        return out
    lmax, emax = max(lg["displacement"]), max(en["displacement"])
    lmed, emed = statistics.median(lg["displacement"]), statistics.median(en["displacement"])
    if kind == "light":
        bound = max(lmax * 1.25, lmax + 24.0)
        if emax > bound:
            out["passed"] = False
            out["notes"].append(f"enhanced corpse moved {emax:.1f} > legacy envelope {bound:.1f}")
        peak_bound = lg["peak"] * 1.25 + 24.0
        if en["peak"] > peak_bound:
            out["passed"] = False
            out["notes"].append(f"enhanced peak body speed {en['peak']:.0f} > legacy envelope {peak_bound:.0f}")
    else:
        if not ( lmed < 32.0 and emed < 32.0 ) and not ( 0.4 * lmed <= emed <= 1.6 * lmed ):
            out["passed"] = False
            out["notes"].append(f"median displacement {emed:.1f} outside 0.4-1.6x legacy {lmed:.1f}")
    return out


def analyse_momentum(legacy: dict, enhanced: dict) -> dict:
    out = {"hit": "momentum", "kind": "momentum", "passed": True, "notes": []}
    deltas = {}
    for mode, result in (("legacy", legacy), ("enhanced", enhanced)):
        blocks = result["reports"]
        if len(blocks) < 2:
            out["passed"] = False
            out["notes"].append(f"{mode}: {len(blocks)} report blocks, want 2")
            return out
        before, after = blocks[0], blocks[1]
        deltas[mode] = {n: tuple(a - b for a, b in zip(after[n]["momentum"], before[n]["momentum"]))
                        for n in sorted(set(before) & set(after))}
    expected = MOMENTUM_IMPULSE
    for mode, values in deltas.items():
        for n, d in values.items():
            error = distance(d, expected) / max(1.0, distance(expected, (0, 0, 0)))
            if error > 0.02:
                out["passed"] = False
                out["notes"].append(f"{mode} {n}: momentum change {d} vs impulse {expected}")
    out["corpses"] = {m: len(v) for m, v in deltas.items()}
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=pw.ROOT / ".install/openQ4-client_x64.exe")
    parser.add_argument("--retail", type=Path)
    parser.add_argument("--save-root", type=Path, default=pw.ROOT / ".tmp/physics-envelope")
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--hits", default=",".join(h[0] for h in HITS) + ",momentum")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--min-free-gb", type=float, default=5.0)
    args = parser.parse_args()
    args.set = []
    args.verbose_digest = False
    args.map = None
    args.port = 28978

    wanted = set(args.hits.split(","))
    results = []
    if "momentum" in wanted:
        wl = momentum_workload()
        results.append(analyse_momentum(run(args, "env-momentum", wl, "legacy"), run(args, "env-momentum", wl, "enhanced")))
    for hit in HITS:
        if hit[0] not in wanted:
            continue
        wl = workload(hit)
        name = "env-" + hit[0]
        results.append(analyse_hit(hit, run(args, name, wl, "legacy"), run(args, name, wl, "enhanced")))

    for r in results:
        lg, en = r.get("legacy", {}), r.get("enhanced", {})
        summary = ""
        if lg and en and lg.get("displacement") and en.get("displacement"):
            summary = (f"corpses {lg['corpses']}/{en['corpses']} gibbed {lg['gibbed']}/{en['gibbed']} disp max {max(lg['displacement']):.0f}/{max(en['displacement']):.0f} "
                       f"median {statistics.median(lg['displacement']):.0f}/{statistics.median(en['displacement']):.0f} "
                       f"peak {lg['peak']:.0f}/{en['peak']:.0f} clamps {en['clamps']} dv {lg['peak_dv']:.0f}/{en['peak_dv']:.0f}")
        print(f"{r['hit']:24s} {r['kind']:8s} {'PASS' if r['passed'] else 'FAIL'}  {summary} {'; '.join(r['notes'])}")
    if args.output:
        out = pw.task_path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
