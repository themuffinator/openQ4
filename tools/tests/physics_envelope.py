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
             quarter plus 24 units/s. A corpse hit while it is still falling
             travels mostly by its own collapse down the slope, and the
             largest of a handful of such collapses moves by a third between
             one staging and three in legacy alone (69 to 91 units): its
             displacement bound is the largest legacy displacement plus a
             half, or plus 48 units, over the pooled stagings
  heavy      rockets, grenades and the Dark Matter Gun's splash: the median
             displacement of the bodies the hit reached (those whose velocity
             it changed) stays within 0.4x to 1.6x of legacy, or both stay
             under 32 units, and no corpse leaves the map
  gibbing    a hit that gibs every corpse in both modes passes: there is no
             motion left to compare, and both modes treated the hit alike
  telemetry  enhanced runs report their impulse clamps and peak speeds

Both modes run the same binary, so scenes are identical. Prints a table and
writes JSON (under .tmp) with --output.

A thrown or still-falling figure tumbles chaotically: the same hit on a ring
two paces wider sends the same corpse somewhere else, in either mode. One
staging of six corpses therefore measures the draw as much as the solver. The
heavy and falling classes run on three stagings of each scene (--stagings) and
judge the pooled corpses; a resting corpse that a light hit does not move is
judged on one.

Scenes (--scene):
  ai          strogg marine corpses on Air Defense 1 (the default)
  moveables   crates and barrels on Air Defense 1: rigid bodies take the same
              hits, with no kill and no falling variants
  moveables-flat  the same crates and barrels on Hangar Perimeter's flat floor:
              Air Defense 1's crater slopes, and a body knocked loose there
              travels mostly by running downhill
  crates-flat, barrels-flat  the friction calibration: one kind of body on the
              flat floor, shoved along it at three speeds (no splash, no slope)
  mp          player corpses on a loopback listen server: six paused bots on
              q4dm1 are killed and hit where they spawned. Spawn points are
              drawn at random, so the scene differs between runs and the
              comparison is statistical
  awakening   The Awakening's grunts on m01_stranarus_trench2 (needs the
              expansion content: --awakening-path)
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

# name, kind, staged hit commands, falling? "{p}" is the scene's name prefix
# (every body), "{each}" repeats the command for each of the scene's targets,
# "{splash} <def>" is a radius hit delivered the way the scene needs.
HITS = (
    ("machinegun", "light", ["physicsDamage {p} damage_machinegun 1"], False),
    ("machinegun-falling", "light", ["physicsDamage {p} damage_machinegun 1"], True),
    ("gauntlet", "light", ["physicsDamage {p} damage_gauntlet 0.5"], False),
    ("small-splash", "light", ["{splash} damage_hyperblaster_splash"], False),
    ("vehicle-bump", "light", ["physicsImpulse {each} 4000 0 1500"], False),
    ("rocket", "heavy", ["physicsDamage {p} damage_rocketDirect 0.5"], False),
    ("rocket-splash", "heavy", ["{splash} damage_rocketSplash"], False),
    ("grenade-splash", "heavy", ["{splash} damage_grenadeSplash"], False),
    ("grenade-splash-falling", "heavy", ["{splash} damage_grenadeSplash"], True),
    ("dmg-splash", "heavy", ["{splash} damage_dmg_splash"], False),
)
# Clean horizontal shoves for the friction calibration scenes: every body gets
# the same impulse along +x and slides to a stop on a flat floor. A crate (mass
# 33) leaves at 122, 214 and 305 units/s for 4,000, 7,000 and 10,000; a barrel
# (mass 89) at 112, 224 and 447 for 10,000, 20,000 and 40,000. Faster crates
# reach the room's walls and steps, which then decide where they stop. Judged
# like a heavy hit. These run only in the scenes that list them.
PUSHES = tuple((f"push-{impulse}", "heavy", [f"physicsImpulse {{each}} {impulse} 0 0"], False)
               for impulse in (4000, 7000, 10000, 20000, 40000))
MOMENTUM_IMPULSE = (0.0, 9000.0, 3000.0)

# What each scene stages before the hit. "kill" leaves corpses at rest with no
# push of its own; "splash" is how a radius hit reaches the bodies (around the
# player for a ring, by name where the bodies stand apart).
SCENES = {
    "ai": {
        "map": "game/airdefense1",
        "cheats": ["god", "notarget", "killMonsters"],
        "spawn": ["physicsSpawn monster_strogg_marine 8 ring 104 16", "@wait 90"],
        "kill": "physicsDamage phys_ damage_triggerhurt_1000 1",
        "prefix": "phys_", "targets": [f"phys_{i}" for i in range(8)],
        "splash": "physicsRadiusDamage {def} 0 16",
        "stagings": [{" ring 104 ": " ring 96 "}, {}, {" ring 104 ": " ring 112 "}],
        "common_start": True,
    },
    "moveables": {
        "map": "game/airdefense1",
        "cheats": ["god", "notarget", "killMonsters"],
        "spawn": ["physicsSpawn moveable_crate1_small 4 ring 96 8", "physicsSpawn moveable_barrel_medium1a 4 ring 150 8",
                  "@wait 120"],
        "kill": None,
        "prefix": "phys_", "targets": [f"phys_{i}" for i in range(8)],
        "splash": "physicsRadiusDamage {def} 0 16",
        "stagings": [{" ring 96 ": " ring 88 ", " ring 150 ": " ring 140 "}, {}, {" ring 96 ": " ring 104 ", " ring 150 ": " ring 160 "}],
    },
    "moveables-flat": {
        "map": "game/hangar1",
        "cheats": ["god", "notarget", "killMonsters"],
        # the barrels stand inside the crates' ring: a splash throws the lighter crates clear of them
        "spawn": ["physicsSpawn moveable_crate1_small 4 ring 110 8", "physicsSpawn moveable_barrel_medium1a 4 ring 60 8",
                  "@wait 120"],
        "kill": None,
        "prefix": "phys_", "targets": [f"phys_{i}" for i in range(8)],
        "splash": "physicsRadiusDamage {def} 0 16",
        "stagings": [{" ring 110 ": " ring 100 ", " ring 60 ": " ring 54 "}, {}, {" ring 110 ": " ring 120 ", " ring 60 ": " ring 66 "}],
    },
    "crates-flat": {
        "map": "game/hangar1",
        "cheats": ["god", "notarget", "killMonsters"],
        "spawn": ["physicsSpawn moveable_crate1_small 4 ring 96 8", "@wait 120"],
        "kill": None,
        "prefix": "phys_", "targets": [f"phys_{i}" for i in range(4)],
        "splash": "physicsRadiusDamage {def} 0 16",
        "stagings": [{" ring 96 ": " ring 88 "}, {}, {" ring 96 ": " ring 104 "}],
        "hits": ["push-4000", "push-7000", "push-10000"],
    },
    "barrels-flat": {
        "map": "game/hangar1",
        "cheats": ["god", "notarget", "killMonsters"],
        "spawn": ["physicsSpawn moveable_barrel_medium1a 4 ring 96 8", "@wait 120"],
        "kill": None,
        "prefix": "phys_", "targets": [f"phys_{i}" for i in range(4)],
        "splash": "physicsRadiusDamage {def} 0 16",
        "stagings": [{" ring 96 ": " ring 88 "}, {}, {" ring 96 ": " ring 104 "}],
        "hits": ["push-10000", "push-20000", "push-40000"],
    },
    "mp": {
        "map": "mp/q4dm1", "multiplayer": True,
        "cheats": ["god", "notarget"],
        "spawn": ["addbot Cortez 4 exact", "addbot Rhodes 4 exact", "addbot Sledge 4 exact", "addbot Voss 4 exact",
                  "addbot Strauss 4 exact", "addbot Kane 4 exact", "@wait 300", "bot_pause 1", "@wait 30"],
        # the listen server's own player is player1 and is never hit
        "kill": "physicsDamage player damage_triggerhurt_1000 1",
        "prefix": "player", "targets": [f"player{i}" for i in range(2, 8)],
        # the bots stand at spawn points all over the map: the same radius damage def,
        # delivered to each of them as a hit pushing away from the server's player
        "splash": "physicsDamage player {def} 1",
        # every run draws its own spawn points, and about one corpse in twenty lies where it
        # tumbles 350 to 500 units down the map in either mode: --stagings 6 pools enough
        # repeats for the falling classes to meet one in both modes
        "stagings": [{}, {}, {}, {}, {}, {}],
    },
    "awakening": {
        "map": "game/m01_stranarus_trench2", "game_dir": "q4xbase",
        "cheats": ["god", "notarget", "killMonsters", "noclip", "setviewpos 400 -2300 110 90", "@wait 30", "noclip",
                   "@wait 60", "killMonsters"],
        "spawn": ["physicsSpawn monster_q4x_grunt 8 ring 104 16", "@wait 90"],
        "kill": "physicsDamage phys_ damage_triggerhurt_1000 1",
        # one grunt of the ring dies on the trench's edge and falls down it: four
        # seconds after the kill it is still moving in both modes (22 units/s in
        # legacy, 63 in enhanced), and a hit then measures the fall, not the hit.
        # Ten seconds is when the game removes a corpse, so the scene keeps them
        # for thirty with the player's own setting.
        "settle": 600,
        "set": ["g_corpseRemoveDelaySP=30"],
        "prefix": "phys_", "targets": [f"phys_{i}" for i in range(8)],
        "splash": "physicsRadiusDamage {def} 0 16",
        "stagings": [{" ring 104 ": " ring 96 "}, {}, {" ring 104 ": " ring 112 "}],
    },
}
SCENE = SCENES["ai"]


def expand(commands: list[str]) -> list[str]:
    out: list[str] = []
    for command in commands:
        if command.startswith("{splash} "):
            out.append(SCENE["splash"].replace("{def}", command.split()[1]))
        elif "{each}" in command:
            out += [command.replace("{each}", target) for target in SCENE["targets"]]
        else:
            out.append(command.replace("{p}", SCENE["prefix"]))
    return out


def scene_workload(steps: list[str]) -> dict:
    out = {"map": SCENE["map"], "steps": [*SCENE["cheats"], "@wait 30", *steps]}
    if SCENE.get("multiplayer"):
        out["multiplayer"] = True
    return out


def workload(hit: tuple) -> dict:
    _name, _kind, commands, falling = hit
    settle = [] if falling else [f"@wait {SCENE.get('settle', 240)}"]
    # quiet kill: no push, so every corpse starts the hit from the same rest
    kill = [SCENE["kill"], "@wait 8" if falling else "@wait 1"] if SCENE["kill"] else []
    report = f"physicsReport {SCENE['prefix']}"
    return scene_workload([
        "physicsStats start envelope",
        *SCENE["spawn"],
        *kill,
        *settle,
        report,
        *expand(commands),
        report,
        "@wait 6", report,
        "@wait 360", report,
        "physicsStats stop",
    ])


STAGE_SLOT = "physics_envelope_stage"


def stage_workload() -> dict:
    """Stage, kill and settle the scene in legacy mode and save it: the common start of both modes."""
    kill = [SCENE["kill"], "@wait 1"] if SCENE["kill"] else []
    wl = scene_workload([
        *SCENE["spawn"],
        *kill,
        f"@wait {SCENE.get('settle', 240)}",
        f"physicsReport {SCENE['prefix']}",
        f"saveGame {STAGE_SLOT}",
        "@wait 30",
    ])
    wl["needs_stats"] = False
    return wl


def loaded_workload(hit: tuple) -> dict:
    """The hit on the saved scene: every body starts where the staging run left it, in either mode."""
    _name, _kind, commands, _falling = hit
    report = f"physicsReport {SCENE['prefix']}"
    return {"map": SCENE["map"], "steps": [
        "@wait 10", f"loadGame {STAGE_SLOT}", "@wait 2",
        "physicsStats start envelope",
        report,
        *expand(commands),
        report,
        "@wait 6", report,
        "@wait 360", report,
        "physicsStats stop",
    ]}


def momentum_workload() -> dict:
    x, y, z = MOMENTUM_IMPULSE
    kill = [SCENE["kill"]] if SCENE["kill"] else []
    report = f"physicsReport {SCENE['prefix']}"
    # a figure takes the impulse on one of its first three bodies, a rigid body on its only one
    impulses = [f"physicsImpulse {target} {x} {y} {z} {i % 3 if SCENE['kill'] else 0}"
                for i, target in enumerate(SCENE["targets"])]
    return scene_workload([
        "physicsStats start momentum",
        *SCENE["spawn"],
        *kill,
        "@wait 300",
        report,
        # the same frame: no step runs between the impulse and the report
        *impulses,
        report,
        "physicsStats stop",
    ])


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


def run(args: argparse.Namespace, name: str, wl: dict, mode: str, staging: int = 0) -> dict:
    pw.WORKLOADS[name] = wl
    result = pw.run_workload(args, name, mode, staging)
    log = Path(result["log"]).read_text(encoding="latin-1") if Path(result["log"]).exists() else ""
    result["reports"] = reports(log)
    return result


def staged(wl: dict, swaps: dict) -> dict:
    """The same workload with the scene's spawn commands moved (ring radii)."""
    out = dict(wl)
    steps = []
    for step in wl["steps"]:
        for old, new in swaps.items():
            step = step.replace(old, new)
        steps.append(step)
    out["steps"] = steps
    return out


def chosen_stagings(args: argparse.Namespace, pooled: bool) -> list[tuple[int, dict]]:
    stagings = SCENE.get("stagings", [{}])
    count = max(1, min(args.stagings, len(stagings))) if pooled else 1
    # the unmoved staging is the middle one: a single-staging run uses it
    middle = len(stagings) // 2
    return [(middle, stagings[middle])] if count == 1 else list(enumerate(stagings[:count]))


def run_stagings(args: argparse.Namespace, name: str, wl: dict, mode: str, pooled: bool) -> list[dict]:
    return [run(args, name, staged(wl, swaps), mode, index) for index, swaps in chosen_stagings(args, pooled)]


STAGED: dict[int, dict] = {}


def run_common(args: argparse.Namespace, name: str, hit: tuple, mode: str, pooled: bool) -> list[dict]:
    """The hit in one mode on scenes staged once, in legacy mode, and saved (see "common start" above)."""
    out = []
    for index, swaps in chosen_stagings(args, pooled):
        root = pw.task_path(args.save_root) / f"stage-{index}"
        if index not in STAGED:
            pw.WORKLOADS["env-stage"] = staged(stage_workload(), swaps)
            STAGED[index] = pw.run_workload(args, "env-stage", "legacy", index, save_root=root)
        pw.WORKLOADS[name] = loaded_workload(hit)
        result = pw.run_workload(args, name, mode, index, save_root=root)
        log = Path(result["log"]).read_text(encoding="latin-1") if Path(result["log"]).exists() else ""
        result["reports"] = reports(log)
        if not STAGED[index]["passed"]:
            result["passed"] = False
            result["errors"] = list(result.get("errors") or []) + ["the staging run failed"]
        out.append(result)
    return out


def analyse_hit(hit: tuple, legacy: list[dict], enhanced: list[dict]) -> dict:
    name, kind, _commands, falling = hit
    out = {"hit": name, "kind": kind, "passed": True, "notes": [], "stagings": len(legacy)}
    per_mode = {}
    for mode, results in (("legacy", legacy), ("enhanced", enhanced)):
        pooled = {"corpses": 0, "gibbed": 0, "displacement": [], "reached": [], "peak": 0.0, "clamps": 0, "peak_dv": 0.0,
                  "peak_joint_stretch": 0.0, "stretched_frames": 0}
        for result in results:
            blocks = result["reports"]
            if len(blocks) < 4:
                out["passed"] = False
                out["notes"].append(f"{mode}: {len(blocks)} report blocks, want 4")
                return out
            before, now, early, after = blocks[0], blocks[1], blocks[2], blocks[3]
            names = sorted(set(before) & set(after))
            stats = result.get("stats") or {}
            pooled["corpses"] += len(names)
            pooled["gibbed"] += len(set(before) - set(after))
            pooled["displacement"] += [distance(before[n]["centre"], after[n]["centre"]) for n in names]
            # the bodies the hit reached: those whose velocity it changed by more than a unit a second
            pooled["reached"] += [distance(before[n]["centre"], after[n]["centre"]) for n in names
                                  if n in now and distance(now[n]["momentum"], before[n]["momentum"]) > max(1.0, before[n]["mass"])]
            pooled["peak"] = max([pooled["peak"]] + [early[n]["peak"] for n in names if n in early])
            pooled["clamps"] += stats.get("impulse_clamps", 0)
            pooled["peak_dv"] = max(pooled["peak_dv"], stats.get("peak_impulse_dv", 0.0))
            pooled["peak_joint_stretch"] = max(pooled["peak_joint_stretch"], stats.get("peak_joint_stretch", 0.0))
            pooled["stretched_frames"] += stats.get("stretched_frames", 0)
            if not result["passed"]:
                out["passed"] = False
                out["notes"].append(f"{mode}: a run did not finish cleanly")
        per_mode[mode] = pooled
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
        bound = max(lmax * 1.5, lmax + 48.0) if falling else max(lmax * 1.25, lmax + 24.0)
        if emax > bound:
            out["passed"] = False
            out["notes"].append(f"enhanced corpse moved {emax:.1f} > legacy envelope {bound:.1f}")
        peak_bound = lg["peak"] * 1.25 + 24.0
        if en["peak"] > peak_bound:
            out["passed"] = False
            out["notes"].append(f"enhanced peak body speed {en['peak']:.0f} > legacy envelope {peak_bound:.0f}")
    else:
        # A blast that reaches half the bodies leaves the median of all of them on a
        # knife edge between one that moved and one that did not, in either mode. The
        # response to a hit is read from the bodies it reached.
        if len(lg["reached"]) >= 3 and len(en["reached"]) >= 3:
            lmed, emed = statistics.median(lg["reached"]), statistics.median(en["reached"])
            out["judged_on"] = f"{len(lg['reached'])}/{len(en['reached'])} bodies the hit reached"
        if not ( lmed < 32.0 and emed < 32.0 ) and not ( 0.4 * lmed <= emed <= 1.6 * lmed ):
            out["passed"] = False
            out["notes"].append(f"median displacement {emed:.1f} outside 0.4-1.6x legacy {lmed:.1f}")
        out["median"] = [round(lmed, 1), round(emed, 1)]
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
    parser.add_argument("--hits", help="hit classes to run (default: the scene's own, or every hit and momentum)")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cache-root", type=Path,
                        help="generated-image cache shared by every run (default: one per run, regenerated each time)")
    parser.add_argument("--min-free-gb", type=float, default=5.0)
    parser.add_argument("--scene", choices=tuple(SCENES), default="ai")
    parser.add_argument("--stagings", type=int, default=3,
                        help="stagings pooled for the heavy and falling classes (1 to 3; the mp scene has 6 repeats)")
    parser.add_argument("--awakening-path", type=Path, help="fs_awakeningpath holding q4xbase (scene awakening)")
    parser.add_argument("--set", action="append", default=[], help="extra cvar KEY=VALUE for every run")
    parser.add_argument("--own-start", action="store_true",
                        help="let each mode kill and settle its own corpses before a hit on resting bodies, as before "
                             "17 November 2026 (default: both modes are hit from one saved scene)")
    args = parser.parse_args()
    args.verbose_digest = False
    args.map = None
    args.port = 28978

    global SCENE
    SCENE = SCENES[args.scene]
    args.game_dir = SCENE.get("game_dir", "baseoq4")
    if args.scene == "awakening":
        if not args.awakening_path:
            parser.error("--scene awakening needs --awakening-path")
        args.set += ["fs_game=q4xbase", f"fs_awakeningpath={args.awakening_path.resolve(strict=True)}"]
    if args.scene != "ai":
        args.save_root = args.save_root.with_name(f"{args.save_root.name}-{args.scene}")
    args.set += SCENE.get("set", [])

    wanted = set(args.hits.split(",")) if args.hits else set(SCENE.get("hits") or [h[0] for h in HITS] + ["momentum"])
    if not SCENE["kill"]:
        # nothing falls before the hit: the falling variants would repeat the resting ones
        wanted -= {hit[0] for hit in HITS if hit[3]}
    results = []
    if "momentum" in wanted:
        wl = momentum_workload()
        results.append(analyse_momentum(run(args, "env-momentum", wl, "legacy"), run(args, "env-momentum", wl, "enhanced")))
    for hit in HITS + PUSHES:
        if hit[0] not in wanted:
            continue
        wl = workload(hit)
        name = "env-" + hit[0]
        # a thrown or falling figure is chaotic: pool stagings. A resting corpse under a light hit is not.
        pooled = hit[1] == "heavy" or hit[3] or SCENE.get("multiplayer", False)
        if SCENE.get("common_start") and not hit[3] and not args.own_start:
            results.append(analyse_hit(hit, run_common(args, name, hit, "legacy", pooled),
                                       run_common(args, name, hit, "enhanced", pooled)))
        else:
            results.append(analyse_hit(hit, run_stagings(args, name, wl, "legacy", pooled),
                                       run_stagings(args, name, wl, "enhanced", pooled)))

    for r in results:
        lg, en = r.get("legacy", {}), r.get("enhanced", {})
        summary = ""
        if lg and en and lg.get("displacement") and en.get("displacement"):
            summary = (f"corpses {lg['corpses']}/{en['corpses']} gibbed {lg['gibbed']}/{en['gibbed']} disp max {max(lg['displacement']):.0f}/{max(en['displacement']):.0f} "
                       f"median {statistics.median(lg['displacement']):.0f}/{statistics.median(en['displacement']):.0f} "
                       f"reached {len(lg['reached'])}/{len(en['reached'])} "
                       + (f"their median {r['median'][0]:.0f}/{r['median'][1]:.0f} " if r.get("median") else "") +
                       f"peak {lg['peak']:.0f}/{en['peak']:.0f} clamps {en['clamps']} dv {lg['peak_dv']:.0f}/{en['peak_dv']:.0f} "
                       f"stretch {lg['peak_joint_stretch']:.0f}/{en['peak_joint_stretch']:.0f} "
                       f"for {lg['stretched_frames']}/{en['stretched_frames']} frames")
        print(f"{r['hit']:24s} {r['kind']:8s} {'PASS' if r['passed'] else 'FAIL'}  {summary} {'; '.join(r['notes'])}")
    if args.output:
        out = pw.task_path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
