#!/usr/bin/env python3
"""Stage the legacy physics defects in stock maps, in both physics modes (PHYS-01).

Opt-in runtime test built on tools/tests/physics_workloads.py. Each repro
stages a scene from console commands only and reads `physicsQuality`, which
reports, per body or figure: the largest distance between a joint's two
anchors (stretch), the joints that have static geometry between a body's
centre and the joint (pierced: a limb through a wall or floor), the bodies
whose centre is inside solid geometry (embedded), whether the object is awake
and how fast its fastest body moves.

Repros:
  resting-jitter    crate and barrel stacks on Air Defense 1's sloped crater
                    floor: sleep/wake churn, bodies still awake and drift
                    after the stacks should have settled
  twitching         Air Defense 1's placed marine ragdolls, woken by a nudge:
                    figures that never come back to rest
  sliding-corpses   fresh corpses on the crater slope, read before the game
                    removes them: drift once they should have settled
  stuck-limbs       marines killed with their backs to a wall, and corpses
                    thrown into the surrounding geometry by a splash: pierced
                    joints and embedded bodies once everything has settled
  joint-stretch     corpses thrown by a Dark Matter splash: the largest joint
                    separation while they fly and tumble, and how long any
                    joint stays more than 8 units apart
  tunnelling        a small body fired into a surface at rising speeds, from
                    straight on to 30 degrees off: bodies that are behind the
                    surface and out of sight of the launch point a sixth of a
                    second after the launch (a body that ricochets away and
                    comes down behind a hill has not tunnelled)
  placed-ragdolls   Air Defense 1's placed corpses settling from the pose the
                    map gives them: joints pierced by what they lie on, and
                    how far each sinks while it settles

Each repro reports whether legacy shows the defect and whether enhanced mode
is free of it. The exit code is non-zero when enhanced mode is worse than
legacy on any repro, or shows a defect the gates forbid outright (a tunnelled
body, a body outside the world).
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import physics_workloads as pw  # noqa: E402

QUALITY = re.compile(r"^PHYSICS_QUALITY (\S+) class=(\S+) bodies=(\d+) joints=(\d+) stretch=([\d.]+) pierced=(\d+) "
                     r"embedded=(\d+) awake=(\d) speed=([\d.]+) origin=(\S+) (\S+) (\S+) lowest=(\S+) (\S+) (\S+)"
                     r"(?: hidden=(\d))? time=(\d+)", re.M)
LAUNCH = re.compile(r"^PHYSICS_LAUNCH (\S+) (\S+) origin=(\S+) (\S+) (\S+) dir=(\S+) (\S+) (\S+) speed=(\d+) static=(\d) "
                    r"wall=([\d.]+) thickness=(\d+) normal=(\S+) (\S+) (\S+) time=(\d+)", re.M)
WALL = re.compile(r"^PHYSICS_WALL yaw=(-?[\d.]+) pitch=(-?[\d.]+) static=(\d) distance=([\d.]+) thickness=(\d+)"
                  r" incidence=(\d+)", re.M)
OUTSIDE = re.compile(r"moved outside world bounds", re.I)

SECOND = pw.SECOND
LAUNCH_SPEEDS = (600, 1200, 2400, 4000, 6000, 9000)
STRETCH_VISIBLE = 8.0		# units: a joint this far apart shows as a stretched limb


def quality_blocks(log: str) -> list[dict]:
    """PHYSICS_QUALITY lines grouped per physicsQuality call."""
    blocks: list[dict] = []
    current: dict | None = None
    for line in log.splitlines():
        if line.startswith("PHYSICS_QUALITY_REPORT "):
            current = {}
            blocks.append(current)
            continue
        m = QUALITY.match(line)
        if m and current is not None:
            current[m.group(1)] = {
                "class": m.group(2), "bodies": int(m.group(3)), "joints": int(m.group(4)),
                "stretch": float(m.group(5)), "pierced": int(m.group(6)), "embedded": int(m.group(7)),
                "awake": int(m.group(8)), "speed": float(m.group(9)),
                "origin": tuple(map(float, m.group(10, 11, 12))), "lowest": tuple(map(float, m.group(13, 14, 15))),
                "hidden": int(m.group(16) or 0), "time": int(m.group(17)),
            }
    return blocks


def distance(a: tuple, b: tuple) -> float:
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def drift(first: dict, second: dict) -> float:
    names = set(first) & set(second)
    return max([distance(first[n]["origin"], second[n]["origin"]) for n in names] + [0.0])


def total(block: dict, field: str) -> int:
    return sum(entry[field] for entry in block.values())


def samples(prefix: str, count: int, every: int) -> list[str]:
    steps: list[str] = []
    for _ in range(count):
        steps += [f"physicsQuality {prefix}", f"@wait {every}"]
    return steps


def scenario_resting_jitter(args: argparse.Namespace) -> dict:
    return {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget", "killMonsters", "@wait 30",
            "physicsStats start resting_jitter",
            "physicsSpawn moveable_crate1_small 5 stack 64 16 34",
            "physicsSpawn moveable_barrel_medium1a 4 stack -64 16 50",
            "@wait 60",
            "physicsImpulse phys_0 0 0 2000", "physicsImpulse phys_5 0 0 6000",
            f"@wait {12 * SECOND}", "physicsQuality phys_",
            f"@wait {4 * SECOND}", "physicsQuality phys_",
            f"@wait {4 * SECOND}", "physicsQuality phys_",
            "physicsStats stop",
        ],
    }


def analyse_resting(result: dict) -> dict:
    blocks = result["quality"]
    stats = result.get("stats") or {}
    if len(blocks) < 3:
        return {"error": f"{len(blocks)} quality blocks, want 3"}
    return {
        "bodies": len(blocks[-1]), "awake_at_end": total(blocks[-1], "awake"),
        "drift_last_8s": round(max(drift(blocks[0], blocks[1]), drift(blocks[1], blocks[2])), 2),
        "sleeps": stats.get("sleeps", 0), "wakes": stats.get("wakes", 0),
    }


def scenario_twitching(args: argparse.Namespace) -> dict:
    return {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget", "killMonsters", "@wait 30",
            "physicsStats start twitching",
            # placed ragdolls start asleep: a nudge on the chest wakes each one
            "physicsQuality env_ragdoll",
            "physicsImpulse env_ragdoll* 0 0 1500 1",
            f"@wait {10 * SECOND}", "physicsQuality env_ragdoll",
            f"@wait {10 * SECOND}", "physicsQuality env_ragdoll",
            f"@wait {10 * SECOND}", "physicsQuality env_ragdoll",
            "physicsStats stop",
        ],
    }


def analyse_twitching(result: dict) -> dict:
    blocks = result["quality"]
    if len(blocks) < 4:
        return {"error": f"{len(blocks)} quality blocks, want 4"}
    last = blocks[-1]
    return {
        "figures": len(last), "awake_after_10s": total(blocks[1], "awake"), "awake_at_end": total(last, "awake"),
        "awake_names": sorted(n for n, e in last.items() if e["awake"]),
        "moved_by_nudge": round(drift(blocks[0], blocks[1]), 2),
        "drift_last_10s": round(drift(blocks[2], blocks[3]), 2),
        "peak_speed_at_end": round(max([e["speed"] for e in last.values()] + [0.0]), 1),
    }


def scenario_sliding(args: argparse.Namespace) -> dict:
    return {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget", "killMonsters", "@wait 30",
            "physicsStats start sliding",
            "physicsSpawn monster_strogg_marine 8 ring 104 16",
            "@wait 90",
            "physicsDamage phys_ damage_triggerhurt_1000 1",
            # the game removes an untouched corpse some ten seconds after death
            f"@wait {4 * SECOND}", "physicsQuality phys_",
            f"@wait {2 * SECOND}", "physicsQuality phys_",
            f"@wait {2 * SECOND}", "physicsQuality phys_",
            "physicsStats stop",
        ],
    }


def analyse_sliding(result: dict) -> dict:
    blocks = result["quality"]
    if len(blocks) < 3:
        return {"error": f"{len(blocks)} quality blocks, want 3"}
    return {
        "corpses": len(blocks[-1]), "awake_at_end": total(blocks[-1], "awake"),
        "drift_4_to_6s": round(drift(blocks[0], blocks[1]), 2),
        "drift_6_to_8s": round(drift(blocks[1], blocks[2]), 2),
    }


def scenario_stuck(args: argparse.Namespace) -> dict:
    return {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget", "killMonsters", "@wait 30",
            "physicsStats start stuck_limbs",
            # marines with their backs 20 units from whatever wall lies that way
            "physicsSpawn monster_strogg_marine 6 wall 20 16 60",
            "@wait 90",
            # the death push drives each one into its wall
            "physicsDamage phys_ damage_machinegun 20",
            f"@wait {4 * SECOND}", "physicsQuality phys_",
            # then a splash throws the corpses into the surrounding geometry
            "physicsRadiusDamage damage_dmg_splash 0 16",
            f"@wait {5 * SECOND}", "physicsQuality phys_",
            "physicsStats stop",
        ],
    }


def analyse_stuck(result: dict) -> dict:
    blocks = result["quality"]
    if len(blocks) < 2:
        return {"error": f"{len(blocks)} quality blocks, want 2"}
    return {
        "corpses": len(blocks[0]),
        "pierced_after_death": total(blocks[0], "pierced"), "embedded_after_death": total(blocks[0], "embedded"),
        "corpses_after_splash": len(blocks[1]),
        "pierced_after_splash": total(blocks[1], "pierced"), "embedded_after_splash": total(blocks[1], "embedded"),
        "stretch_at_rest": round(max([e["stretch"] for b in blocks for e in b.values()] + [0.0]), 2),
        "outside_world": result["outside_world"],
    }


def scenario_stretch(args: argparse.Namespace) -> dict:
    return {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget", "killMonsters", "@wait 30",
            "physicsStats start joint_stretch",
            "physicsSpawn monster_strogg_marine 8 ring 104 16",
            "@wait 90",
            "physicsDamage phys_ damage_triggerhurt_1000 1",
            "@wait 240",
            "physicsRadiusDamage damage_dmg_splash 0 16",
            # every second frame while the corpses fly and tumble
            *samples("phys_", 60, 2),
            f"@wait {5 * SECOND}", "physicsQuality phys_",
            "physicsStats stop",
        ],
    }


def analyse_stretch(result: dict) -> dict:
    blocks = result["quality"]
    if len(blocks) < 10:
        return {"error": f"{len(blocks)} quality blocks, want 61"}
    flight = blocks[:-1]
    series = [max([e["stretch"] for e in b.values()] + [0.0]) for b in flight]
    return {
        "corpses": len(blocks[0]),
        "peak_stretch_in_flight": round(max(series + [0.0]), 2),
        # samples are two frames apart
        "frames_stretched": 2 * sum(1 for value in series if value > STRETCH_VISIBLE),
        "stretch_at_rest": round(max([e["stretch"] for e in blocks[-1].values()] + [0.0]), 2),
        "pierced_at_rest": total(blocks[-1], "pierced"), "embedded_at_rest": total(blocks[-1], "embedded"),
        "outside_world": result["outside_world"],
    }


def scenario_tunnelling(args: argparse.Namespace) -> dict:
    steps = ["god", "notarget", "killMonsters"]
    if args.tunnel_view:
        x, y, z, yaw = args.tunnel_view
        steps += ["noclip", f"setviewpos {x} {y} {z} {yaw}"]
    steps += ["@wait 30", "physicsStats start tunnelling"]
    for speed in LAUNCH_SPEEDS:
        for pitch in args.tunnel_pitches:
            # read while the body is still where the impact left it
            steps += [f"physicsLaunch {args.launch_def} {speed} {args.tunnel_yaw} {pitch}", "@wait 10", "physicsQuality phys_ eye",
                      "@wait 35"]
    steps += [f"@wait {4 * SECOND}", "physicsQuality phys_ eye", "physicsStats stop"]
    return {"map": args.tunnel_map, "steps": steps}


def analyse_tunnelling(result: dict) -> dict:
    blocks = result["quality"]
    launches = result["launches"]
    if not blocks or not launches:
        return {"error": f"{len(blocks)} quality blocks, {len(launches)} launches"}
    last = blocks[-1]
    beyond = []
    lost = []
    thin = [launch for launch in launches if launch["static"] and launch["thickness"] <= 24]
    for index, launch in enumerate(launches):
        if launch["name"] not in last:
            lost.append(launch["name"])
        # the sample taken ten frames after this launch
        final = blocks[index].get(launch["name"]) if index < len(blocks) - 1 else None
        if final is None:
            if launch["name"] not in lost:
                lost.append(launch["name"])
            continue
        if not launch["static"]:
            continue
        # how far behind the surface the body ended: measured along the surface
        # normal from the point the launch line met it
        hit = tuple(o + launch["wall"] * d for o, d in zip(launch["origin"], launch["dir"]))
        behind = -sum((f - h) * n for f, h, n in zip(final["origin"], hit, launch["normal"]))
        # a body sliding away over uneven ground can sit below the plane and
        # still be in the open, so it must also be out of sight of the launch point
        if behind > 8.0 and final["hidden"]:
            beyond.append({"name": launch["name"], "speed": launch["speed"], "thickness": launch["thickness"],
                           "behind": round(behind, 1)})
    return {
        "launched": len(launches), "at_thin_surfaces": len(thin),
        "thinnest": min([launch["thickness"] for launch in launches if launch["static"]] + [999]),
        "beyond_wall": len(beyond), "beyond": beyond[:6], "lost": lost,
        "embedded_at_rest": total(last, "embedded"), "outside_world": result["outside_world"],
    }


def scenario_placed(args: argparse.Namespace) -> dict:
    return {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget",
            "physicsStats start placed_ragdolls",
            "physicsQuality env_ragdoll",
            f"@wait {10 * SECOND}", "physicsQuality env_ragdoll",
            "physicsStats stop",
        ],
    }


def analyse_placed(result: dict) -> dict:
    blocks = result["quality"]
    if len(blocks) < 2:
        return {"error": f"{len(blocks)} quality blocks, want 2"}
    first, last = blocks[0], blocks[-1]
    names = sorted(set(first) & set(last))
    sink = {n: first[n]["origin"][2] - last[n]["origin"][2] for n in names}
    return {
        "figures": len(names), "pierced_at_rest": total(last, "pierced"), "embedded_at_rest": total(last, "embedded"),
        "pierced_names": sorted(n for n, e in last.items() if e["pierced"]),
        "largest_sink": round(max(sink.values(), default=0.0), 2),
        "awake_at_end": total(last, "awake"),
    }


SCENARIOS = {
    "resting-jitter": (scenario_resting_jitter, analyse_resting),
    "twitching": (scenario_twitching, analyse_twitching),
    "sliding-corpses": (scenario_sliding, analyse_sliding),
    "stuck-limbs": (scenario_stuck, analyse_stuck),
    "joint-stretch": (scenario_stretch, analyse_stretch),
    "tunnelling": (scenario_tunnelling, analyse_tunnelling),
    "placed-ragdolls": (scenario_placed, analyse_placed),
}


def verdict(name: str, legacy: dict, enhanced: dict) -> dict:
    """Does legacy show the defect, and is enhanced free of it (or at least no worse)?"""
    if "error" in legacy or "error" in enhanced:
        return {"legacy_shows_defect": None, "enhanced_ok": False, "why": legacy.get("error") or enhanced.get("error")}

    def worse(field: str, slack: float = 0.0, ratio: float = 1.0) -> bool:
        return enhanced[field] > max(legacy[field] * ratio, legacy[field] + slack)

    why: list[str] = []
    if name == "resting-jitter":
        shows = legacy["wakes"] > 50 or legacy["awake_at_end"] > 0 or legacy["drift_last_8s"] > 1.0
        for field, slack in (("wakes", 20), ("awake_at_end", 0), ("drift_last_8s", 1.0)):
            if worse(field, slack):
                why.append(f"{field} {enhanced[field]} > legacy {legacy[field]}")
    elif name == "twitching":
        shows = legacy["awake_at_end"] > 0
        if worse("awake_at_end"):
            why.append(f"awake figures {enhanced['awake_at_end']} > legacy {legacy['awake_at_end']}")
    elif name == "sliding-corpses":
        shows = legacy["drift_6_to_8s"] > 1.0 or legacy["awake_at_end"] > 0
        if not legacy["corpses"] or not enhanced["corpses"]:
            why.append("no corpses left to read")
        for field, slack in (("drift_6_to_8s", 1.0), ("awake_at_end", 0)):
            if worse(field, slack):
                why.append(f"{field} {enhanced[field]} > legacy {legacy[field]}")
    elif name == "stuck-limbs":
        shows = legacy["pierced_after_death"] + legacy["pierced_after_splash"] + legacy["embedded_after_death"] \
            + legacy["embedded_after_splash"] > 0
        for field in ("pierced_after_death", "embedded_after_death", "pierced_after_splash", "embedded_after_splash"):
            if worse(field):
                why.append(f"{field} {enhanced[field]} > legacy {legacy[field]}")
        if enhanced["outside_world"]:
            why.append(f"{enhanced['outside_world']} bodies left the world")
    elif name == "joint-stretch":
        shows = legacy["peak_stretch_in_flight"] > STRETCH_VISIBLE or legacy["stretch_at_rest"] > 2.0
        # one caught limb sets the peak; how long joints stay visibly apart is the defect
        if worse("peak_stretch_in_flight", 8.0, 1.5):
            why.append(f"peak stretch {enhanced['peak_stretch_in_flight']} > legacy {legacy['peak_stretch_in_flight']}")
        if worse("frames_stretched", 4):
            why.append(f"stretched for {enhanced['frames_stretched']} frames, legacy {legacy['frames_stretched']}")
        if worse("stretch_at_rest", 1.0):
            why.append(f"stretch at rest {enhanced['stretch_at_rest']} > legacy {legacy['stretch_at_rest']}")
        if enhanced["outside_world"]:
            why.append(f"{enhanced['outside_world']} bodies left the world")
    elif name == "placed-ragdolls":
        shows = legacy["pierced_at_rest"] + legacy["embedded_at_rest"] > 0
        for field, slack in (("pierced_at_rest", 0), ("embedded_at_rest", 0), ("largest_sink", 2.0), ("awake_at_end", 0)):
            if worse(field, slack):
                why.append(f"{field} {enhanced[field]} > legacy {legacy[field]}")
    else:
        shows = legacy["beyond_wall"] > 0 or bool(legacy["lost"]) or legacy["outside_world"] > 0
        if enhanced["beyond_wall"] or enhanced["lost"] or enhanced["outside_world"]:
            why.append(f"{enhanced['beyond_wall']} bodies beyond the wall, {len(enhanced['lost'])} lost, "
                       f"{enhanced['outside_world']} outside the world")
    return {"legacy_shows_defect": shows, "enhanced_ok": not why, "why": "; ".join(why)}


# A corpse thrown into geometry tumbles chaotically, and a count of limbs left
# inside walls over six corpses is a draw as much as a measure. These repros run
# on several stagings (the same scene with the spawn moved) and judge the sums.
STAGINGS = {
    "stuck-limbs": [{" wall 20 ": " wall 16 "}, {}, {" wall 20 ": " wall 24 "}],
}


def staged(workload: dict, swaps: dict) -> dict:
    out = dict(workload)
    steps = []
    for step in workload["steps"]:
        for old, new in swaps.items():
            step = step.replace(old, new)
        steps.append(step)
    out["steps"] = steps
    return out


def pooled(metrics: list[dict]) -> dict:
    """Counts add up over stagings; a measured largest value stays the largest."""
    if any("error" in m for m in metrics):
        return next(m for m in metrics if "error" in m)
    out: dict = {}
    for m in metrics:
        for key, value in m.items():
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                out.setdefault(key, value)
            elif isinstance(value, float):
                out[key] = max(out.get(key, 0.0), value)
            else:
                out[key] = out.get(key, 0) + value
    return out


def run(args: argparse.Namespace, name: str, workload: dict, mode: str, staging: int = 0) -> dict:
    pw.WORKLOADS["defect-" + name] = workload
    result = pw.run_workload(args, "defect-" + name, mode, staging)
    log = Path(result["log"]).read_text(encoding="latin-1") if Path(result["log"]).exists() else ""
    result["quality"] = quality_blocks(log)
    result["outside_world"] = len(OUTSIDE.findall(log))
    result["launches"] = [{
        "name": m.group(1), "origin": tuple(map(float, m.group(3, 4, 5))), "dir": tuple(map(float, m.group(6, 7, 8))),
        "speed": int(m.group(9)), "static": int(m.group(10)), "wall": float(m.group(11)), "thickness": int(m.group(12)),
        "normal": tuple(map(float, m.group(13, 14, 15))),
    } for m in LAUNCH.finditer(log)]
    result["walls"] = [{"yaw": float(m.group(1)), "pitch": float(m.group(2)), "static": int(m.group(3)),
                        "distance": float(m.group(4)), "thickness": int(m.group(5)), "incidence": int(m.group(6))}
                       for m in WALL.finditer(log)]
    return result


def probe(args: argparse.Namespace) -> int:
    """List the thin walls around a viewpoint, to aim the tunnelling repro."""
    steps = ["god", "notarget", "noclip"]
    if args.tunnel_view:
        x, y, z, yaw = args.tunnel_view
        steps.append(f"setviewpos {x} {y} {z} {yaw}")
    steps += ["@wait 30", "physicsStats start probe"]
    for pitch in (0, 30, 60, 90):
        steps.append(f"physicsWallProbe -180 180 10 {pitch}")
    steps += ["physicsStats stop"]
    result = run(args, "probe", {"map": args.tunnel_map, "steps": steps, "needs_stats": False}, "legacy")
    walls = sorted((w for w in result["walls"] if w["static"] and w["thickness"] <= 24 and w["incidence"] <= 30),
                   key=lambda w: (w["thickness"], w["distance"]))
    for w in walls[:40]:
        print(f"yaw {w['yaw']:7.1f} pitch {w['pitch']:5.1f} distance {w['distance']:7.1f} thickness {w['thickness']} "
              f"incidence {w['incidence']}")
    print(f"{len(walls)} thin walls of {len(result['walls'])} probes")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=pw.ROOT / ".install/openQ4-client_x64.exe")
    parser.add_argument("--retail", type=Path)
    parser.add_argument("--save-root", type=Path, default=pw.ROOT / ".tmp/physics-defects")
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--repros", default=",".join(SCENARIOS))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cache-root", type=Path,
                        help="generated-image cache shared by every run (default: one per run, regenerated each time)")
    parser.add_argument("--min-free-gb", type=float, default=5.0)
    parser.add_argument("--set", action="append", help="extra cvar KEY=VALUE for every run")
    parser.add_argument("--stagings", type=int, default=3, help="stagings pooled for the repros that have them (1 to 3)")
    parser.add_argument("--probe", action="store_true", help="list thin walls around --tunnel-view and exit")
    parser.add_argument("--tunnel-map", default="game/airdefense1")
    parser.add_argument("--tunnel-view", type=float, nargs=4, metavar=("X", "Y", "Z", "YAW"),
                        help="eye position and yaw for the tunnelling repro (default: the map's player start)")
    parser.add_argument("--tunnel-yaw", type=float, default=0.0, help="yaw offset of the launches")
    parser.add_argument("--tunnel-pitches", type=float, nargs="+", default=[90.0, 75.0, 60.0],
                        help="pitches of the launches (90 is straight down into the ground)")
    parser.add_argument("--launch-def", default="moveable_marine_helmet", help="small body fired at the wall")
    args = parser.parse_args()
    args.verbose_digest = False
    args.map = None
    args.port = 28979

    if args.probe:
        return probe(args)

    names = [n for n in args.repros.split(",") if n]
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        parser.error(f"unknown repros: {', '.join(unknown)}")
    report = []
    for name in names:
        build, analyse = SCENARIOS[name]
        workload = build(args)
        stagings = STAGINGS.get(name, [{}])[:max(1, args.stagings)] if args.stagings > 1 else [{}]
        metrics, ran = {}, {}
        for mode in ("legacy", "enhanced"):
            runs = [run(args, name, staged(workload, swaps), mode, index) for index, swaps in enumerate(stagings)]
            metrics[mode] = pooled([analyse(result) for result in runs])
            ran[mode] = all(result["passed"] for result in runs)
        entry = {"repro": name, "stagings": len(stagings), "legacy": metrics["legacy"], "enhanced": metrics["enhanced"], "ran": ran,
                 **verdict(name, metrics["legacy"], metrics["enhanced"])}
        entry["passed"] = bool(entry["enhanced_ok"] and all(entry["ran"].values()))
        report.append(entry)

    for entry in report:
        shows = {True: "legacy shows it", False: "legacy does not show it", None: "legacy unknown"}[entry["legacy_shows_defect"]]
        print(f"{entry['repro']:16s} {'PASS' if entry['passed'] else 'FAIL'}  {shows}; "
              f"enhanced {'ok' if entry['enhanced_ok'] else 'WORSE: ' + entry['why']}")
        print(f"    legacy   {json.dumps(entry['legacy'])}")
        print(f"    enhanced {json.dumps(entry['enhanced'])}")
    if args.output:
        out = pw.task_path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if all(entry["passed"] for entry in report) else 1


if __name__ == "__main__":
    sys.exit(main())
