#!/usr/bin/env python3
"""The stock campaign with enhanced physics on: level chain and set pieces (PHYS-04).

Opt-in runtime test built on tools/tests/physics_workloads.py. Hidden client,
console commands only, all writable data under .tmp.

chain       One client walks the 31 stages of the stock campaign from
            game/airdefense1 to game/core2. On each map it lets the map's own
            bodies run for a few seconds while `physicsStats` records, reads
            `physicsQuality *` (every rigid body and figure in the map), saves,
            loads that save, and fires the map's own end-level target, so the
            session performs the real level change. Per map it reports the
            physics cost, peak body speed, bodies still awake, joints pierced
            by geometry, bodies that left the world, warnings and errors.
            Run in both modes, the table compares them map by map.
            Passes when every stage loads twice in order in the requested
            mode, the game ends at core2 and nothing errors; in enhanced mode
            no map may lose more bodies to the void than legacy does.

setpieces   Scripted physics the campaign's progression depends on, each run
            in both modes from its own map: Air Defense 1's blasted grunt,
            Building B's thrown marine and falling barrels, Process 1's
            knocked-down grate, Storage 2's falling panel and Hub 1's grate.
            The map's own trigger or script function drives it. Passes when
            both modes finish without errors and each driven body ends within
            the heavy-hit tolerance of legacy (0.4x to 1.6x of its travel, or
            both under 32 units, or the two end points within 48 units).

Vehicles, projectiles and flying AI on rigid-body physics keep the legacy
step in both modes by design, so the chain exercises them unchanged.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import physics_workloads as pw  # noqa: E402
import physics_defects as pd  # noqa: E402
import physics_envelope as pe  # noqa: E402

# map, the end-level target that leads on (names from the retail .map files)
CHAIN = (
    ("airdefense1", "target_endlevel_2"), ("airdefense2", "target_endlevel_2"),
    ("hangar1", "elevNorthPass1GoToHangar2"), ("hangar2", "endLevel"), ("mcc_landing", "endLevel"),
    ("mcc_1", "target_leveltrigger_1"), ("convoy1", "target_endlevel_3"), ("building_b", "target_endlevel_2"),
    ("convoy2", "target_endlevel_1"), ("convoy2b", "target_endlevel_3"), ("hub1", "target_endlevel_1"),
    ("hub2", "endlevel"), ("medlabs", "target_endlevel_3"), ("walker", "target_endlevel_1"),
    ("dispersal", "hrt_target_endlevel_2"), ("recomp", "target_endlevel_2"), ("putra", "target_endlevel_2"),
    ("waste", "endLevel"), ("mcc_2", "mcc2_endlevel"), ("storage1", "endLevel"), ("storage2", "target_endlevel_1"),
    ("storage1", "target_endlevel_2"), ("tram1", "target_endlevel_1"), ("tram1b", "target_endlevel_1"),
    ("process1", "target_endlevel_1"), ("process2", "target_endlevel_1"), ("process1", "target_endlevel_2"),
    ("network1", "levelChange"), ("network2", "target_endlevel_1"), ("core1", "target_endlevel_3"),
    ("core2", "endGame"),
)
STAGE = re.compile(r"^PHYS_STAGE (\d+) (\S+) (\w+)\s*$", re.M)
LATCH = re.compile(r"^physics: (\w+) simulation", re.M)
WARNING = re.compile(r"(?mi)^(?:\^\d)?WARNING:.*$")
CHAIN_DONE = "PHYS_CHAIN_DONE"

SETPIECES = {
    "airdefense1-blasted-grunt": {
        "map": "game/airdefense1", "view": "10256 -3180 68 90", "noclip": False,
        "drive": ["trigger trigger_npc_kovitch_wrestle", "@wait 60",
                  "script \"$mon_grunt_wrestle.show(); thread map_airdefense1::gruntBlasted();\""],
        "subjects": ["mon_grunt_wrestle"],
    },
    "building_b-thrown-marine": {
        "map": "game/building_b", "view": "-6800 4900 150 180", "noclip": True,
        "drive": ["trigger chuck_trigger"],
        "subjects": ["forced_marine_1"],
    },
    "building_b-falling-barrels": {
        "map": "game/building_b", "view": "-7800 4300 260 180", "noclip": True,
        "drive": ["script \"thread map_building_b::Weld_guard();\""],
        "subjects": ["falling_barrel_1", "falling_barrel_2"],
    },
    "process1-grate": {
        "map": "game/process1", "view": "-2496 -2200 200 270", "noclip": True,
        "drive": ["script \"thread map_process1::knockdownGrate();\""],
        "subjects": ["mov_protector_grate"],
    },
    "storage2-panel": {
        "map": "game/storage2", "view": "-1088 -160 -2560 90", "noclip": True,
        "drive": ["trigger trigger_once_65"],
        "subjects": ["panel_1"],
    },
    "hub1-grate": {
        "map": "game/hub1", "view": "3328 -6000 -2560 270", "noclip": True,
        "drive": ["script \"thread map_hub1::grate();\""],
        "subjects": ["grate"],
    },
}


def stage_cfgs(stages: tuple, settle_frames: int) -> dict[str, list[str]]:
    """Two cfgs per stage: `a` runs after the level change, `b` after loading the stage's save."""
    cfgs: dict[str, list[str]] = {}
    for index, (name, end) in enumerate(stages):
        last = index == len(stages) - 1
        # Each cfg opens with a short wait: the game may queue it a frame before
        # the session performs the load, and frames do not pass during a load,
        # so everything after the wait runs in the new map.
        cfgs[f"phys_chain_{index}_a.cfg"] = [
            "wait 4",
            f"echo PHYS_STAGE {index} {name} change",
            "god", "notarget", "wait 30",
            f"physicsStats start chain_{index}_{name}",
            *pw.build_cfg({"steps": [f"@wait {settle_frames}"]})[:-2],
            "physicsStats stop",
            "physicsQuality *",
            f"saveGame phys_chain_{index}",
            "wait 30",
            # armed in the same frame as the load, so it runs once the save is in
            f"set g_autoExecAfterMapLoad phys_chain_{index}_b.cfg",
            f"loadGame phys_chain_{index}",
        ]
        after_load = [
            "wait 4",
            f"echo PHYS_STAGE {index} {name} load",
            "wait 60",
            "physicsQuality *",
        ]
        if last:
            after_load += [f"echo {CHAIN_DONE}", f"trigger {end}", "wait 120", "quit"]
        else:
            after_load += [f"set g_autoExecAfterMapLoad phys_chain_{index + 1}_a.cfg", f"trigger {end}"]
        cfgs[f"phys_chain_{index}_b.cfg"] = after_load
    return cfgs


def run_chain(args: argparse.Namespace, mode: str) -> dict:
    stages = CHAIN[:args.stages] if args.stages else CHAIN
    save_root = pw.task_path(args.save_root) / f"chain-{mode}"
    game_root = save_root / "baseoq4"
    game_root.mkdir(parents=True, exist_ok=True)
    for name, lines in stage_cfgs(stages, int(args.settle_seconds * pw.SECOND)).items():
        (game_root / name).write_text("\n".join(lines) + "\n", encoding="ascii")
    # the first stage's cfg runs as the workload's own after-load cfg
    pw.WORKLOADS["campaign-chain"] = {"map": f"game/{stages[0][0]}", "cfg": ["exec phys_chain_0_a.cfg"], "steps": [],
                                      "needs_stats": False}
    chain_args = argparse.Namespace(**vars(args))
    chain_args.timeout = args.chain_timeout
    result = pw.run_workload(chain_args, "campaign-chain", mode, 0, save_root=save_root)
    log = Path(result["log"]).read_text(encoding="latin-1") if Path(result["log"]).exists() else ""

    marks = list(STAGE.finditer(log))
    rows: list[dict] = []
    for position, mark in enumerate(marks):
        index, name, how = int(mark.group(1)), mark.group(2), mark.group(3)
        section = log[mark.end():marks[position + 1].start() if position + 1 < len(marks) else len(log)]
        quality = pd.quality_blocks(section)
        block = quality[0] if quality else {}
        stats = [json.loads(m.group(1)) for m in pw.STATS.finditer(section)]
        s = stats[-1] if stats else {}
        row = next((r for r in rows if r["stage"] == index), None)
        if row is None:
            row = {"stage": index, "map": name}
            rows.append(row)
        row[how] = {
            "bodies": len(block), "awake": pd.total(block, "awake"), "pierced": pd.total(block, "pierced"),
            "embedded": pd.total(block, "embedded"),
            "stretch": round(max([e["stretch"] for e in block.values()] + [0.0]), 2),
            "outside_world": len(pd.OUTSIDE.findall(section)),
            "slow": len(re.findall(r"^PHYSICS_SLOW ", section, re.M)),
            "errors": pw.ERROR.findall(section)[:3],
        }
        if s:
            row[how].update(total_ms=s["all"]["total_ms"], max_ms=s["all"]["max_ms"], peak_speed=s["peak_speed"],
                            energy_clamps=s["energy_clamps"], fallbacks=s["solver_fallbacks"], sleeps=s["sleeps"],
                            wakes=s["wakes"])
    latched = LATCH.findall(log)
    order = [(int(m.group(1)), m.group(3)) for m in marks]
    expected = [(i, how) for i in range(len(stages)) for how in ("change", "load")]
    out = {
        "mode": mode, "stages": len(stages), "rows": rows, "exit": result["exit"], "seconds": result["seconds"],
        "loads": len(latched), "wrong_mode_loads": sum(1 for m in latched if m != mode),
        "order_ok": order == expected, "done": CHAIN_DONE in log,
        "errors": pw.ERROR.findall(log)[:8], "warnings": len(WARNING.findall(log)),
        "outside_world": len(pd.OUTSIDE.findall(log)), "log": result["log"],
    }
    # a partial walk's last end-level target still loads the next map
    out["expected_loads"] = 2 * len(stages) + (0 if len(stages) == len(CHAIN) else 1)
    out["passed"] = bool(out["order_ok"] and out["done"] and not out["errors"] and out["loads"] == out["expected_loads"]
                         and not out["wrong_mode_loads"] and result["exit"] == 0)
    return out


def setpiece_workload(piece: dict, settle_seconds: float) -> dict:
    report = [f"physicsReport {name}" for name in piece["subjects"]]
    quality = [f"physicsQuality {name}" for name in piece["subjects"]]
    view = ["noclip"] if piece["noclip"] else []
    return {
        "map": piece["map"],
        "steps": [
            "god", "notarget", *view, f"setviewpos {piece['view']}", "@wait 60",
            "physicsStats start setpiece",
            *report,
            *piece["drive"],
            # a body the drive itself turns into a ragdoll or unhides is first read here
            "@wait 30", *report,
            f"@wait {int(settle_seconds * pw.SECOND)}",
            *report, *quality,
            "physicsStats stop",
        ],
    }


def analyse_setpiece(piece: dict, result: dict) -> dict:
    blocks = pe.reports(result["log_text"])
    count = len(piece["subjects"])
    if len(blocks) < 3 * count:
        return {"error": f"{len(blocks)} report blocks, want {3 * count}"}
    before: dict = {}
    after: dict = {}
    # where each body was before the drive, or half a second into it when the
    # drive is what gives it physics
    for block in blocks[count:2 * count]:
        before.update(block)
    for block in blocks[:count]:
        before.update(block)
    for block in blocks[2 * count:3 * count]:
        after.update(block)
    quality: dict = {}
    for block in pd.quality_blocks(result["log_text"]):
        quality.update(block)
    subjects = {}
    for name in piece["subjects"]:
        if name not in before or name not in after:
            subjects[name] = {"error": "not reported (no rigid-body or articulated physics under that name)"}
            continue
        subjects[name] = {
            "travel": round(pe.distance(before[name]["centre"], after[name]["centre"]), 1),
            "drop": round(before[name]["centre"][2] - after[name]["centre"][2], 1),
            "end": after[name]["centre"], "at_rest": after[name]["rest"],
            "stretch": quality.get(name, {}).get("stretch", 0.0), "pierced": quality.get(name, {}).get("pierced", 0),
        }
    stats = result.get("stats") or {}
    return {"subjects": subjects, "peak_speed": stats.get("peak_speed"), "impulse_clamps": stats.get("impulse_clamps"),
            "outside_world": len(pd.OUTSIDE.findall(result["log_text"])), "errors": result["errors"]}


def judge_setpiece(legacy: dict, enhanced: dict) -> list[str]:
    if "error" in legacy or "error" in enhanced:
        return [legacy.get("error") or enhanced.get("error")]
    why: list[str] = []
    for name, lg in legacy["subjects"].items():
        en = enhanced["subjects"].get(name, {"error": "missing"})
        if "error" in lg or "error" in en:
            why.append(f"{name}: {lg.get('error') or en.get('error')}")
            continue
        lt, et = lg["travel"], en["travel"]
        apart = pe.distance(lg["end"], en["end"])
        if not (lt < 32.0 and et < 32.0) and not (0.4 * lt <= et <= 1.6 * lt) and apart > 48.0:
            why.append(f"{name}: travelled {et} against legacy {lt}, ending {apart:.0f} units apart")
        if en["at_rest"] < lg["at_rest"]:
            why.append(f"{name}: still moving where legacy came to rest")
    if enhanced["outside_world"] > legacy["outside_world"]:
        why.append(f"{enhanced['outside_world']} bodies left the world")
    return why


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=pw.ROOT / ".install/openQ4-client_x64.exe")
    parser.add_argument("--retail", type=Path)
    parser.add_argument("--save-root", type=Path, default=pw.ROOT / ".tmp/physics-campaign")
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--parts", default="chain,setpieces")
    parser.add_argument("--mode", choices=("legacy", "enhanced", "both"), default="both", help="modes the chain runs in")
    parser.add_argument("--stages", type=int, help="walk only the first N stages (probing)")
    parser.add_argument("--settle-seconds", type=float, default=8.0, help="how long each map's bodies run before the save")
    parser.add_argument("--setpieces", default=",".join(SETPIECES))
    parser.add_argument("--timeout", type=int, default=600, help="per set-piece run")
    parser.add_argument("--chain-timeout", type=int, default=7200)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cache-root", type=Path,
                        help="generated-image cache shared by every run (default: one per run, regenerated each time)")
    parser.add_argument("--min-free-gb", type=float, default=5.0)
    parser.add_argument("--set", action="append", default=[], help="extra cvar KEY=VALUE for every run")
    args = parser.parse_args()
    args.verbose_digest = False
    args.map = None
    args.port = 28981

    parts = set(args.parts.split(","))
    report: dict = {}
    passed = True
    if "chain" in parts:
        modes = ("legacy", "enhanced") if args.mode == "both" else (args.mode,)
        chains = {mode: run_chain(args, mode) for mode in modes}
        report["chain"] = chains
        for mode, chain in chains.items():
            print(f"chain {mode}: {'PASS' if chain['passed'] else 'FAIL'} loads={chain['loads']}/{chain['expected_loads']} "
                  f"order_ok={chain['order_ok']} done={chain['done']} errors={len(chain['errors'])} warnings={chain['warnings']} "
                  f"outside_world={chain['outside_world']} ({chain['seconds']}s) {chain['errors'][:1]}")
            passed &= chain["passed"]
        if len(chains) == 2:
            legacy = {row["stage"]: row for row in chains["legacy"]["rows"]}
            for row in chains["enhanced"]["rows"]:
                lg = legacy.get(row["stage"], {}).get("change", {})
                en = row.get("change", {})
                if en.get("outside_world", 0) > lg.get("outside_world", 0):
                    passed = False
                    print(f"  {row['map']}: {en['outside_world']} bodies left the world, legacy {lg.get('outside_world', 0)}")
                print(f"  {row['stage']:2d} {row['map']:12s} bodies {lg.get('bodies')}/{en.get('bodies')} "
                      f"awake {lg.get('awake')}/{en.get('awake')} ms {lg.get('total_ms')}/{en.get('total_ms')} "
                      f"worst {lg.get('max_ms')}/{en.get('max_ms')} peak {lg.get('peak_speed')}/{en.get('peak_speed')} "
                      f"pierced {lg.get('pierced')}/{en.get('pierced')} stretch {lg.get('stretch')}/{en.get('stretch')}")
    if "setpieces" in parts:
        report["setpieces"] = []
        for name in [n for n in args.setpieces.split(",") if n]:
            piece = SETPIECES[name]
            workload = setpiece_workload(piece, 10.0)
            metrics = {}
            ran = True
            for mode in ("legacy", "enhanced"):
                pw.WORKLOADS["setpiece-" + name] = workload
                result = pw.run_workload(args, "setpiece-" + name, mode, 0)
                result["log_text"] = Path(result["log"]).read_text(encoding="latin-1") if Path(result["log"]).exists() else ""
                metrics[mode] = analyse_setpiece(piece, result)
                ran &= result["passed"]
            why = judge_setpiece(metrics["legacy"], metrics["enhanced"])
            entry = {"setpiece": name, "legacy": metrics["legacy"], "enhanced": metrics["enhanced"], "why": why,
                     "passed": bool(ran and not why)}
            report["setpieces"].append(entry)
            passed &= entry["passed"]
            print(f"{name:28s} {'PASS' if entry['passed'] else 'FAIL'}  {'; '.join(why)}")
            print(f"    legacy   {json.dumps(metrics['legacy'])}")
            print(f"    enhanced {json.dumps(metrics['enhanced'])}")
    if args.output:
        out = pw.task_path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
