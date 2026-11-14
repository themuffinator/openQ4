#!/usr/bin/env python3
"""Run the PHYS reference workloads and report physics CPU distributions.

Opt-in runtime harness for the rigid-body and ragdoll physics track
(docs/dev/physics-reference.md). Each workload boots the staged client hidden
and silent with one game tic per frame (com_fixedTic 1), stages its scene from
console commands only (physicsSpawn, physicsRadiusDamage, physicsImpulse,
killMonsters, trigger, ...), records per-frame physics CPU time with
`physicsStats`, and prints `physicsDigest` checkpoints so two runs can be
compared exactly. It never injects input or captures the desktop, and all
writable data stays under .tmp/.

Examples:
  python tools/tests/physics_workloads.py --mode legacy --workloads sp-ragdolls
  python tools/tests/physics_workloads.py --mode both --repeat 3 --output .tmp/phys/baseline.json
"""

from __future__ import annotations

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ERROR = re.compile(r"(?mi)^(?:\^\d)?(?:ERROR:|FATAL ERROR:).*$")
STATS = re.compile(r"^PHYSICS_STATS (\{.*\})\s*$", re.M)
DIGEST = re.compile(r"^PHYSICS_DIGEST time=(\d+) mode=(\w+) bodies=(\d+) hash=([0-9a-f]+) all=([0-9a-f]+)"
                    r" workload=([0-9a-f]+) workloadBodies=(\d+)\s*$", re.M)
DONE = "PHYS_WORKLOAD_DONE"

# One frame is one 16 ms game tic under com_fixedTic 1.
SECOND = 60


def frames(seconds: float) -> int:
    return int(round(seconds * SECOND))


# Each workload is a map plus console steps. "@wait N" waits N frames; "@digest"
# prints a digest checkpoint. Scenes use only stock content and entity defs.
WORKLOADS: dict[str, dict] = {
    # Several AI ragdolls: monsters spawned around the player, killed at once,
    # then left to fall, tumble and settle.
    "sp-ragdolls": {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget", "killMonsters",
            "@wait 30",
            "physicsStats start sp_ragdolls",
            "physicsSpawn monster_strogg_marine 8 ring 160 16",
            "@wait 90",
            # half the ring dies to machinegun-class hits, half to a rocket: each
            # with its def's push and death push, through the normal Damage path
            "physicsDamage phys_0 damage_machinegun 20",
            "physicsDamage phys_1 damage_machinegun 20",
            "physicsDamage phys_2 damage_machinegun 20",
            "physicsDamage phys_3 damage_machinegun 20",
            "physicsDamage phys_4 damage_rocketSplash 4",
            "physicsDamage phys_5 damage_rocketSplash 4",
            "physicsDamage phys_6 damage_rocketSplash 4",
            "physicsDamage phys_7 damage_rocketSplash 4",
            "@wait 30", "@digest",
            "@wait 270", "@digest",
            "@wait 600", "@digest",
            "physicsStats stop",
        ],
    },
    # An explosion clearing moveables: crates and barrels in front of the player,
    # then a rocket-sized splash in their middle; debris and items fly and settle.
    "sp-explosion": {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget", "killMonsters",
            "@wait 30",
            "physicsStats start sp_explosion",
            # crates in a 3 x 3 grid ahead, barrels in a ring, an exploding barrel
            # behind the player; a placement a wall blocks is skipped, never moved
            "physicsSpawn moveable_crate1_small 9 grid 48 8 56",
            "physicsSpawn moveable_barrel_medium1a 6 ring 190 8",
            "physicsSpawn moveable_explodingbarrel 1 line -80 8",
            "@wait 120", "@digest",
            # a rocket-sized splash in the grid; it also sets off the exploding barrel
            "physicsRadiusDamage damage_rocketSplash 104 16",
            "@wait 30", "@digest",
            "@wait 270", "@digest",
            "@wait 600", "@digest",
            "physicsStats stop",
        ],
    },
    # A scripted AF set piece: Air Defense 1's grunt wrestle. The relay runs the
    # map script, Kovitch's animation blasts the grunt, the script kills it and
    # applies two scripted impulses so it topples over (map_airdefense1::gruntBlasted).
    "sp-setpiece": {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget",
            "setviewpos 10256 -3180 68 90",
            "@wait 30",
            "physicsStats start sp_setpiece",
            # the relay starts the sequence; the blast itself is the map's own
            # function (Kovitch's animation calls it once he is in the scene)
            "trigger trigger_npc_kovitch_wrestle",
            "@wait 60",
            "script \"$mon_grunt_wrestle.show(); thread map_airdefense1::gruntBlasted();\"",
            "@wait 300", "@digest",
            "@wait 300", "@digest",
            "@wait 300", "@digest",
            "physicsStats stop",
        ],
    },
    # Visual check: corpses and debris photographed at fixed moments after death
    # and after an explosion, in each mode, for a side-by-side review.
    "sp-visual": {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget", "killMonsters", "g_showHud 0", "@wait 30",
            "physicsStats start sp_visual",
            "physicsSpawn monster_strogg_marine 3 line 96 16 40",
            "physicsSpawn moveable_crate1_small 4 grid 120 8 48",
            "@wait 90",
            "physicsDamage phys_0 damage_machinegun 20",
            "physicsDamage phys_1 damage_rocketSplash 4",
            "physicsDamage phys_2 damage_gauntlet 4",
            "@wait 12", "screenshot \"screenshots/physics_death_0200ms.tga\"",
            "@wait 48", "screenshot \"screenshots/physics_death_1000ms.tga\"",
            "@wait 240", "screenshot \"screenshots/physics_death_5000ms.tga\"",
            "physicsRadiusDamage damage_rocketSplash 140 16",
            "@wait 12", "screenshot \"screenshots/physics_blast_0200ms.tga\"",
            "@wait 108", "screenshot \"screenshots/physics_blast_2000ms.tga\"",
            "@wait 300", "screenshot \"screenshots/physics_blast_7000ms.tga\"",
            "@digest",
            "physicsStats stop",
        ],
    },
    # Stacks on a flat hangar floor: they must stand still after the nudge.
    "sp-flatstacks": {
        "map": "game/hangar1",
        "steps": [
            "god", "notarget", "killMonsters",
            "@wait 30",
            "physicsStats start sp_flatstacks",
            "physicsSpawn moveable_crate1_small 5 stack 72 16 34",
            "physicsSpawn moveable_barrel_medium1a 4 stack -72 16 50",
            "@wait 60",
            "physicsImpulse phys_0 0 0 2000",
            "physicsImpulse phys_5 0 0 6000",
            "@wait 60", "@digest",
            "@wait 480", "@digest",
            "@wait 600", "@digest",
            "physicsStats stop",
        ],
    },
    # Resting stacks and piles: a tower of crates and a pile of barrels that must
    # settle and stay still (resting jitter, sliding, endless twitching).
    "sp-stacks": {
        "map": "game/airdefense1",
        "steps": [
            "god", "notarget", "killMonsters",
            "@wait 30",
            "physicsStats start sp_stacks",
            # each body drops onto the one below (drop-to-floor placement), then a
            # nudge on the bottom crate and barrel wakes the stacks to settle again
            "physicsSpawn moveable_crate1_small 5 stack 64 16 34",
            "physicsSpawn moveable_barrel_medium1a 4 stack -64 16 50",
            "@wait 60",
            "physicsImpulse phys_0 0 0 2000",
            "physicsImpulse phys_5 0 0 6000",
            "@wait 60", "@digest",
            "@wait 480", "@digest",
            "@wait 600", "@digest",
            "physicsStats stop",
        ],
    },
    # A frag-heavy multiplayer round on a loopback listen server: seven bots on a
    # small map; deaths leave player corpses and gibs. Bots and networking make
    # this workload non-deterministic, so it is measured for cost only.
    "mp-frags": {
        "map": "mp/q4dm1",
        "multiplayer": True,
        "steps": [
            "notarget", "god",
            "addbot Cortez 4 exact", "addbot Rhodes 4 exact", "addbot Sledge 4 exact",
            "addbot Voss 4 exact", "addbot Strauss 4 exact", "addbot Kane 4 exact", "addbot Morris 4 exact",
            "@wait 300",
            "physicsStats start mp_frags",
            "@wait 3600",
            "physicsStats stop",
        ],
    },
}


def task_path(path: Path) -> Path:
    absolute = path.resolve()
    if not absolute.is_relative_to(ROOT / ".tmp"):
        raise ValueError(f"runtime output must be inside {ROOT / '.tmp'}")
    return absolute


def build_cfg(workload: dict, verbose_digest: bool = False) -> list[str]:
    lines: list[str] = []
    for step in workload["steps"]:
        if step.startswith("@wait "):
            count = int(step.split()[1])
            # long waits split so the console buffer never holds a huge count
            while count > 0:
                lines.append(f"wait {min(count, 120)}")
                count -= min(count, 120)
        elif step == "@digest":
            lines.append("physicsDigest verbose" if verbose_digest else "physicsDigest")
        else:
            lines.append(step)
    lines += [f"echo {DONE}", "quit"]
    return lines


def free_commit_gb() -> float | None:
    """Free commit charge (page file included), or None off Windows."""
    if os.name != "nt":
        return None
    import ctypes

    class MemoryStatus(ctypes.Structure):
        _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong), ("totalPhys", ctypes.c_ulonglong),
                    ("availPhys", ctypes.c_ulonglong), ("totalPageFile", ctypes.c_ulonglong),
                    ("availPageFile", ctypes.c_ulonglong), ("totalVirtual", ctypes.c_ulonglong),
                    ("availVirtual", ctypes.c_ulonglong), ("availExtended", ctypes.c_ulonglong)]
    status = MemoryStatus()
    status.length = ctypes.sizeof(MemoryStatus)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        return None
    return status.availPageFile / (1024 ** 3)


def wait_for_memory(minimum_gb: float, limit_seconds: float = 900.0) -> None:
    """The machine is shared: a map load near the commit limit dies with FATAL: Out of memory."""
    started = time.monotonic()
    while True:
        free = free_commit_gb()
        if free is None or free >= minimum_gb or time.monotonic() - started > limit_seconds:
            return
        time.sleep(10)


def run_workload(args: argparse.Namespace, name: str, mode: str, run_index: int, attempt: int = 0,
                 save_root: Path | None = None) -> dict:
    """Run one workload. save_root lets several runs share a profile (save in one, load in another)."""
    wait_for_memory(getattr(args, "min_free_gb", 5.0))
    workload = WORKLOADS[name]
    if save_root is None:
        save_root = task_path(args.save_root) / f"{name}-{mode}-{run_index}{'-' + args.map.replace('/', '_') if args.map else ''}"
    save_root = task_path(save_root)
    game_root = save_root / "baseoq4"
    game_root.mkdir(parents=True, exist_ok=True)
    cfg_name = "physics_workload.cfg"
    (game_root / cfg_name).write_text("\n".join(build_cfg(workload, args.verbose_digest)) + "\n", encoding="ascii")
    log_relative = "logs/physics.log"
    log_path = game_root / log_relative
    if log_path.exists():
        log_path.unlink()
    exe = args.exe.resolve(strict=True)
    settings = {
        "r_hiddenWindow": "1", "r_fullscreen": "0", "s_noSound": "1",
        "logFile": "2", "logFileName": log_relative,
        "in_mouse": "0", "in_joystick": "0",
        "developer": "1", "com_skipLoadingContinue": "1", "g_autoSkipCinematics": "1",
        "fs_savepath": str(save_root), "fs_devpath": str(save_root), "fs_cachepath": str(save_root / "cache"),
        "r_fullscreenDesktop": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "com_fixedTic": "1",
        "g_autoExecAfterMapLoad": cfg_name, "g_autoExecAfterMapLoadDelayMs": "0",
        "g_enhancedPhysics": "1" if mode == "enhanced" else "0",
        "r_renderApi": args.renderer,
    }
    multiplayer = workload.get("multiplayer", False)
    client = workload.get("client", False)		# joins a server or plays a demo: multiplayer settings, no server
    if multiplayer or client:
        # loopback-only listen server: no firewall prompt, no LAN exposure
        settings.update({
            "com_gameMode": "MP", "fs_game": "baseoq4", "net_serverDedicated": "0", "net_LANServer": "1",
            "net_ip": "127.0.0.1", "net_enableIPv6": "0", "net_port": str(args.port),
            "net_allowCheats": "1", "sv_cheats": "1", "ui_autoJoin": "1", "ui_spectate": "Play",
            "ui_name": "PhysicsObserver", "si_pure": "0", "si_gameType": "DM", "si_maxPlayers": "8",
            "si_minPlayers": "2", "si_warmup": "0", "si_useReady": "0", "si_fragLimit": "999",
            "si_timeLimit": "0", "g_matchProfile": "casual", "bot_enable": "1", "bot_minPlayers": "0",
            "bot_pause": "0", "bot_chat": "0", "bot_skillVariance": "0",
        })
    else:
        settings["si_gameType"] = "singleplayer"
    if workload.get("dedicated"):
        settings["net_serverDedicated"] = "1"
    if args.retail:
        settings["fs_basepath"] = str(args.retail.resolve(strict=True))
    settings["win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances"] = "1"
    command = [str(exe)]
    for key, value in settings.items():
        command += ["+set", key, value]
    for extra in args.set or []:
        key, _, value = extra.partition("=")
        command += ["+set", key, value]
    if workload.get("startup"):
        command += list(workload["startup"])		# e.g. +connect, +playMVD, or +map then a loadGame in the cfg
    else:
        command += ["+spawnServer" if multiplayer else "+map", args.map or workload["map"]]
    options: dict = {}
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    started = time.monotonic()
    (game_root / "logs").mkdir(exist_ok=True)
    with (game_root / "logs" / "stdout.txt").open("w", encoding="utf-8") as output:
        process = subprocess.Popen(command, cwd=exe.parent, stdin=subprocess.DEVNULL, stdout=output,
                                   stderr=subprocess.STDOUT, **options)
        try:
            code = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            code = "timeout"
    log = log_path.read_text(encoding="latin-1") if log_path.exists() else ""
    if "FATAL: Out of memory" in log and attempt == 0:
        # another process held the commit charge: wait for room and run once more
        time.sleep(30)
        return run_workload(args, name, mode, run_index, attempt + 1, save_root)
    stats = [json.loads(m.group(1)) for m in STATS.finditer(log)]
    digests = [{"time": int(m.group(1)), "mode": m.group(2), "bodies": int(m.group(3)),
                "hash": m.group(4), "all": m.group(5), "workload": m.group(6),
                "workload_bodies": int(m.group(7))} for m in DIGEST.finditer(log)]
    errors = ERROR.findall(log)
    latched = re.search(r"^physics: (\w+) simulation", log, re.M)
    result = {
        "workload": name, "mode": mode, "run": run_index, "exit": code,
        "seconds": round(time.monotonic() - started, 2),
        "done": DONE in log, "errors": errors[:5],
        "latched_mode": latched.group(1) if latched else None,
        "stats": stats[-1] if stats else None, "digests": digests,
        "spawns": len(re.findall(r"^PHYSICS_SPAWN ", log, re.M)),
        "skipped_spawns": len(re.findall(r"^PHYSICS_SPAWN_SKIPPED ", log, re.M)),
        "log": str(log_path),
    }
    latched_modes = re.findall(r"^physics: (\w+) simulation", log, re.M)
    result["latched_modes"] = latched_modes
    expected_mode = workload.get("expect_mode", mode)
    result["passed"] = bool(code == 0 and result["done"] and not errors
                            and (result["stats"] or not workload.get("needs_stats", True))
                            and latched_modes and latched_modes[-1] == expected_mode)
    s = result["stats"] or {}
    all_ms = s.get("all", {})
    print(f"{name} {mode} #{run_index}: {'PASS' if result['passed'] else 'FAIL'} "
          f"frames={s.get('frames')} total={all_ms.get('total_ms')}ms p95={all_ms.get('p95_ms')} "
          f"p99={all_ms.get('p99_ms')} max={all_ms.get('max_ms')} digests={[d['workload'][:8] for d in digests]} "
          f"({result['seconds']}s){' ' + errors[0] if errors else ''}", flush=True)
    return result


def summarize(results: list[dict]) -> dict:
    summary: dict = {}
    for result in results:
        if not result.get("stats"):
            continue
        key = f"{result['workload']}/{result['mode']}"
        entry = summary.setdefault(key, {"runs": 0, "total_ms": [], "p95_ms": [], "p99_ms": [], "max_ms": [],
                                         "digest_sets": []})
        entry["runs"] += 1
        for field in ("total_ms", "p95_ms", "p99_ms", "max_ms"):
            entry[field].append(result["stats"]["all"][field])
        entry["digest_sets"].append([d["workload"] for d in result["digests"]])
    for entry in summary.values():
        for field in ("total_ms", "p95_ms", "p99_ms", "max_ms"):
            values = entry[field]
            entry[field + "_median"] = statistics.median(values) if values else None
        entry["deterministic"] = len({tuple(d) for d in entry["digest_sets"]}) == 1
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=ROOT / ".install/openQ4-client_x64.exe")
    parser.add_argument("--retail", type=Path, help="Quake 4 install (default: engine discovery)")
    parser.add_argument("--save-root", type=Path, default=ROOT / ".tmp/physics-workloads")
    parser.add_argument("--mode", choices=("legacy", "enhanced", "both"), default="legacy")
    parser.add_argument("--workloads", default=",".join(WORKLOADS), help="comma-separated workload names")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--port", type=int, default=28977, help="loopback port for multiplayer workloads")
    parser.add_argument("--map", help="run the workloads on this map instead of their own (probing)")
    parser.add_argument("--min-free-gb", type=float, default=5.0, help="wait for this much free commit before a launch")
    parser.add_argument("--set", action="append", help="extra cvar KEY=VALUE for every run")
    parser.add_argument("--verbose-digest", action="store_true", help="also print per-entity digests")
    parser.add_argument("--output", type=Path, help="write the JSON results here (under .tmp)")
    args = parser.parse_args()

    names = [n for n in args.workloads.split(",") if n]
    unknown = [n for n in names if n not in WORKLOADS]
    if unknown:
        parser.error(f"unknown workloads: {', '.join(unknown)}")
    modes = ("legacy", "enhanced") if args.mode == "both" else (args.mode,)
    results = []
    for run_index in range(args.repeat):
        for name in names:
            for mode in modes:
                results.append(run_workload(args, name, mode, run_index))
    report = {"results": results, "summary": summarize(results)}
    if args.output:
        out = task_path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for key, entry in report["summary"].items():
        print(f"{key}: runs={entry['runs']} total_ms~{entry['total_ms_median']} p99~{entry['p99_ms_median']} "
              f"max~{entry['max_ms_median']} deterministic={entry['deterministic']}")
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
