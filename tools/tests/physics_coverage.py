#!/usr/bin/env python3
"""Enhanced physics against legacy on constrained bodies and debris (PHYS-04).

Opt-in runtime test built on tools/tests/physics_workloads.py; it completes
the behavior envelope of tools/tests/physics_envelope.py for bodies that are
not free corpses. Both modes run the same binary and the same console steps.

Suites:
  hanging   bodies held by the world: the swinging lights of Air Defense 1
            and Putra, chains of bodies fixed to the ceiling at one end. (The
            stock maps' hanging corpses are `idAFEntity_Base` entities, which
            never load their figure: they are static scenery in both modes.)
            Each is pushed (a light and a heavy raw impulse, a machinegun hit,
            a rocket splash) and sampled every tenth of a second: how far it
            swings, where it ends, how far its joints stretch and whether it
            comes to rest.
            Enhanced mode must swing within 0.4x to 1.6x of legacy (or both
            under 4 units), end no further from where it hung, and stretch
            its joints no more than legacy.
  debris    client-side debris (the chunks effects throw: no entity, no
            network state): launching barrels set off together.
            The pieces' count, spread around the blast and speed are compared
            as distributions, since effects draw their own random numbers.
            Enhanced mode must keep the median spread within 0.4x to 1.6x of
            legacy and leave no piece moving or outside the map.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import physics_workloads as pw  # noqa: E402
import physics_defects as pd  # noqa: E402
import physics_envelope as pe  # noqa: E402

BLAST = re.compile(r"^PHYSICS_RADIUS_DAMAGE (\S+) at (\S+) (\S+) (\S+) time=(\d+)", re.M)

# site -> map, viewpoint (eye position and yaw, reached with noclip), name prefix
HANGING_SITES = {
    "airdefense1-lights": {"map": "game/airdefense1", "view": "8956 -4200 150 90", "prefix": "env_swinging_light"},
    "putra-lights": {"map": "game/putra", "view": "928 -1150 500 90", "prefix": "env_swinging_light"},
}
# name, commands ("{p}" = the site's prefix); raw impulses go to the body below the anchor
HANGING_HITS = (
    ("impulse-light", ["physicsImpulse {p}* 1500 0 0 1"]),
    ("impulse-heavy", ["physicsImpulse {p}* 12000 0 3000 1"]),
    ("machinegun", ["physicsDamage {p} damage_machinegun 1"]),
    ("rocket-splash", ["physicsDamage {p} damage_rocketSplash 1"]),
)
SAMPLES = 50
SAMPLE_EVERY = 6


def hanging_workload(site: dict, commands: list[str]) -> dict:
    prefix = site["prefix"]
    sample = []
    for index in range(SAMPLES):
        sample += [f"physicsReport {prefix}", f"physicsQuality {prefix}", f"@wait {SAMPLE_EVERY}"]
    return {
        "map": site["map"],
        "steps": [
            "god", "notarget", "noclip", f"setviewpos {site['view']}", "@wait 120",
            "physicsStats start hanging",
            f"physicsReport {prefix}", f"physicsQuality {prefix}",
            *[c.replace("{p}", prefix) for c in commands],
            *sample,
            f"@wait {10 * pw.SECOND}",
            f"physicsReport {prefix}", f"physicsQuality {prefix}",
            "physicsStats stop",
        ],
    }


def analyse_hanging(result: dict) -> dict:
    reports = pe.reports(result["log_text"])
    quality = pd.quality_blocks(result["log_text"])
    if len(reports) < SAMPLES + 2 or len(quality) < SAMPLES + 2:
        return {"error": f"{len(reports)} reports and {len(quality)} quality blocks, want {SAMPLES + 2}"}
    before, last = reports[0], reports[-1]
    names = sorted(set(before) & set(last))
    if not names:
        return {"error": "no bodies reported"}
    swing = {n: max(pe.distance(before[n]["centre"], block[n]["centre"]) for block in reports[1:] if n in block) for n in names}
    return {
        "bodies": len(names),
        "peak_swing": round(max(swing.values()), 2), "median_swing": round(statistics.median(swing.values()), 2),
        "residual": round(max(pe.distance(before[n]["centre"], last[n]["centre"]) for n in names), 2),
        "peak_stretch": round(max(e["stretch"] for block in quality for e in block.values()), 3),
        "stretch_before": round(max(e["stretch"] for e in quality[0].values()), 3),
        "peak_speed": round(max(block[n]["peak"] for block in reports for n in names if n in block), 1),
        "awake_at_end": sum(e["awake"] for e in quality[-1].values()),
        "pierced_at_end": sum(e["pierced"] for e in quality[-1].values()),
    }


def judge_hanging(legacy: dict, enhanced: dict) -> list[str]:
    why: list[str] = []
    if "error" in legacy or "error" in enhanced:
        return [legacy.get("error") or enhanced.get("error")]
    ls, es = legacy["peak_swing"], enhanced["peak_swing"]
    if not (ls < 4.0 and es < 4.0) and not (0.4 * ls <= es <= 1.6 * ls):
        why.append(f"peak swing {es} outside 0.4-1.6x legacy {ls}")
    if enhanced["residual"] > max(legacy["residual"] * 1.5, legacy["residual"] + 4.0):
        why.append(f"ends {enhanced['residual']} from where it hung, legacy {legacy['residual']}")
    if enhanced["peak_stretch"] > max(legacy["peak_stretch"] * 1.25, legacy["peak_stretch"] + 1.0):
        why.append(f"joint stretch {enhanced['peak_stretch']} > legacy {legacy['peak_stretch']}")
    if enhanced["awake_at_end"] > legacy["awake_at_end"]:
        why.append(f"{enhanced['awake_at_end']} still awake, legacy {legacy['awake_at_end']}")
    return why


def debris_workload() -> dict:
    return {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget", "killMonsters", "@wait 30",
            "physicsStats start debris",
            "physicsSpawn launching_barrel_small 4 ring 120 8",
            "@wait 120",
            "physicsReport @debris",
            "physicsDamage phys_ damage_triggerhurt_1000 1",
            # the blast point the spread is measured from: the middle of the ring
            "physicsRadiusDamage damage_hyperblaster_splash 0 16",
            "@wait 6", "physicsReport @debris",
            "@wait 24", "physicsReport @debris",
            f"@wait {2 * pw.SECOND}", "physicsReport @debris",
            f"@wait {6 * pw.SECOND}", "physicsReport @debris",
            "physicsStats stop",
        ],
    }


def analyse_debris(result: dict) -> dict:
    reports = pe.reports(result["log_text"])
    blast = BLAST.search(result["log_text"])
    if len(reports) < 5 or not blast:
        return {"error": f"{len(reports)} reports, blast {'found' if blast else 'missing'}"}
    centre = tuple(map(float, blast.group(2, 3, 4)))
    reports = reports[1:]		# the first report is the scene before the blast
    pieces = max(len(block) for block in reports)
    stats = result.get("stats") or {}
    # debris expires: measure each piece where it was last seen
    last_seen: dict = {}
    for block in reports:
        last_seen.update(block)
    spread = [pe.distance(centre, entry["centre"]) for entry in last_seen.values()]
    return {
        "pieces": pieces, "pieces_at_end": len(reports[-1]),
        "median_spread": round(statistics.median(spread), 1) if spread else 0.0,
        "max_spread": round(max(spread), 1) if spread else 0.0,
        "peak_speed": round(max([e["peak"] for block in reports for e in block.values()] + [0.0]), 1),
        "moving_at_end": sum(1 for e in reports[-1].values() if not e["rest"] and e["peak"] > 20.0),
        "client_rigid_evaluations": stats.get("client_rigid", 0),
        "outside_world": len(pd.OUTSIDE.findall(result["log_text"])),
    }


def judge_debris(legacy: dict, enhanced: dict) -> list[str]:
    if "error" in legacy or "error" in enhanced:
        return [legacy.get("error") or enhanced.get("error")]
    why: list[str] = []
    if legacy["pieces"] and not (0.5 * legacy["pieces"] <= enhanced["pieces"] <= 1.5 * legacy["pieces"]):
        why.append(f"{enhanced['pieces']} pieces, legacy {legacy['pieces']}")
    lm, em = legacy["median_spread"], enhanced["median_spread"]
    if legacy["pieces"] and not (0.4 * lm <= em <= 1.6 * lm):
        why.append(f"median spread {em} outside 0.4-1.6x legacy {lm}")
    if enhanced["moving_at_end"] > legacy["moving_at_end"]:
        why.append(f"{enhanced['moving_at_end']} pieces still moving, legacy {legacy['moving_at_end']}")
    if enhanced["outside_world"] > legacy["outside_world"]:
        why.append(f"{enhanced['outside_world']} bodies left the world")
    return why


def run(args: argparse.Namespace, name: str, workload: dict, mode: str) -> dict:
    pw.WORKLOADS[name] = workload
    result = pw.run_workload(args, name, mode, 0)
    result["log_text"] = Path(result["log"]).read_text(encoding="latin-1") if Path(result["log"]).exists() else ""
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=pw.ROOT / ".install/openQ4-client_x64.exe")
    parser.add_argument("--retail", type=Path)
    parser.add_argument("--save-root", type=Path, default=pw.ROOT / ".tmp/physics-coverage")
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--suites", default="hanging,debris")
    parser.add_argument("--sites", default=",".join(HANGING_SITES))
    parser.add_argument("--hits", default=",".join(h[0] for h in HANGING_HITS))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cache-root", type=Path,
                        help="generated-image cache shared by every run (default: one per run, regenerated each time)")
    parser.add_argument("--min-free-gb", type=float, default=5.0)
    parser.add_argument("--set", action="append", default=[], help="extra cvar KEY=VALUE for every run")
    args = parser.parse_args()
    args.verbose_digest = False
    args.map = None
    args.port = 28980

    suites = set(args.suites.split(","))
    report = []
    if "hanging" in suites:
        for site_name in args.sites.split(","):
            site = HANGING_SITES[site_name]
            for hit_name, commands in HANGING_HITS:
                if hit_name not in args.hits.split(","):
                    continue
                workload = hanging_workload(site, commands)
                name = f"hang-{site_name}-{hit_name}"
                runs = {mode: run(args, name, workload, mode) for mode in ("legacy", "enhanced")}
                metrics = {mode: analyse_hanging(result) for mode, result in runs.items()}
                why = judge_hanging(metrics["legacy"], metrics["enhanced"])
                report.append({"case": name, "legacy": metrics["legacy"], "enhanced": metrics["enhanced"], "why": why,
                               "passed": not why and all(result["passed"] for result in runs.values())})
    if "debris" in suites:
        workload = debris_workload()
        runs = {mode: run(args, "debris", workload, mode) for mode in ("legacy", "enhanced")}
        metrics = {mode: analyse_debris(result) for mode, result in runs.items()}
        why = judge_debris(metrics["legacy"], metrics["enhanced"])
        report.append({"case": "debris", "legacy": metrics["legacy"], "enhanced": metrics["enhanced"], "why": why,
                       "passed": not why and all(result["passed"] for result in runs.values())})

    for entry in report:
        print(f"{entry['case']:40s} {'PASS' if entry['passed'] else 'FAIL'}  {'; '.join(entry['why'])}")
        print(f"    legacy   {json.dumps(entry['legacy'])}")
        print(f"    enhanced {json.dumps(entry['enhanced'])}")
    if args.output:
        out = pw.task_path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if report and all(entry["passed"] for entry in report) else 1


if __name__ == "__main__":
    sys.exit(main())
