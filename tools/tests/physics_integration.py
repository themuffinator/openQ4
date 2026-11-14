#!/usr/bin/env python3
"""Enhanced physics integration: saves, multiplayer, demos, dedicated (PHYS-05).

Opt-in runtime test built on tools/tests/physics_workloads.py; hidden,
silent, loopback-only and input-free, with all data under .tmp.

  saves      a single-player save made mid-motion (ragdolls and crates in
             flight) in one physics mode loads in the other and in its own,
             restores every body where it was saved, commits the loading
             session's mode and settles without errors or runaway speeds
  client     a client joining a listen server takes the server's committed
             mode from serverinfo (si_enhancedPhysics) whatever its own
             g_enhancedPhysics says, both ways round
  mvd        a multiview demo recorded on an enhanced server plays back in
             the enhanced mode on a client whose own setting is legacy
  dedicated  the dedicated server commits enhanced physics and runs a bot
             match without renderer or effect dependencies, and a legacy
             client that joins follows it
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import physics_workloads as pw  # noqa: E402

BODY = re.compile(r"^PHYSICS_BODY (\S+) class=\S+ bodies=\d+ mass=[\d.]+ centre=(\S+) (\S+) (\S+) momentum=.* peak=([\d.]+) rest=(\d)", re.M)
FAST = re.compile(r"^PHYSICS_FAST ", re.M)


def report_blocks(log: str) -> list[dict]:
    blocks: list[dict] = []
    for line in log.splitlines():
        if line.startswith("PHYSICS_REPORT "):
            blocks.append({})
            continue
        m = BODY.match(line)
        if m and blocks:
            blocks[-1][m.group(1)] = (tuple(map(float, m.group(2, 3, 4))), float(m.group(5)), int(m.group(6)))
    return blocks


def log_of(result: dict) -> str:
    path = Path(result["log"])
    return path.read_text(encoding="latin-1") if path.exists() else ""


def saves(args) -> list[dict]:
    out = []
    for save_mode, load_mode in (("legacy", "enhanced"), ("enhanced", "legacy"), ("enhanced", "enhanced")):
        slot = f"physics_{save_mode}"
        root = pw.task_path(args.save_root) / f"saves-{save_mode}-{load_mode}"
        pw.WORKLOADS["int-save"] = {
            "map": "game/airdefense1", "needs_stats": False,
            "steps": ["god", "notarget", "killMonsters", "@wait 30",
                      "physicsSpawn monster_strogg_marine 4 ring 104 16",
                      "physicsSpawn moveable_crate1_small 4 grid 48 64 56 nodrop 1",
                      "@wait 60",
                      "physicsDamage phys_ damage_rocketSplash 1",
                      "@wait 12",
                      "physicsReport phys_",
                      f"saveGame {slot}",
                      "@wait 30"],
        }
        pw.WORKLOADS["int-load"] = {
            "map": "game/airdefense1", "needs_stats": False,
            "steps": ["@wait 10", f"loadGame {slot}", "@wait 2", "physicsReport phys_",
                      "@wait 360", "physicsReport phys_", "physicsDigest"],
        }
        saved = pw.run_workload(args, "int-save", save_mode, 0, save_root=root)
        loaded = pw.run_workload(args, "int-load", load_mode, 0, save_root=root)
        save_blocks, load_blocks = report_blocks(log_of(saved)), report_blocks(log_of(loaded))
        notes = []
        save_file = root / "baseoq4" / "savegames" / f"{slot}.save"
        if not save_file.exists():
            notes.append("no save file")
        if not saved["passed"]:
            notes.append(f"save run failed: exit={saved['exit']} errors={saved['errors'][:1]}")
        if not loaded["passed"]:
            notes.append(f"load run failed: exit={loaded['exit']} modes={loaded.get('latched_modes')} errors={loaded['errors'][:1]}")
        if len(save_blocks) < 1 or len(load_blocks) < 2:
            notes.append(f"report blocks {len(save_blocks)}/{len(load_blocks)}")
        else:
            at_save, at_load, settled = save_blocks[0], load_blocks[0], load_blocks[1]
            missing = sorted(set(at_save) - set(at_load))
            if missing:
                notes.append(f"bodies missing after load: {missing}")
            # the save is taken 30 tics before quitting and reported in the same
            # frame as the save; the load reports two tics in, so allow two tics of flight
            for name in sorted(set(at_save) & set(at_load)):
                moved = math.dist(at_save[name][0], at_load[name][0])
                if moved > 32.0:
                    notes.append(f"{name} restored {moved:.1f} units from its saved place")
            if any(v[1] > 4000.0 for v in settled.values()):
                notes.append("runaway body speed after the load")
            if FAST.search(log_of(loaded)):
                notes.append("PHYSICS_FAST after the load")
        out.append({"check": f"saves {save_mode}->{load_mode}", "passed": not notes, "notes": notes,
                    "modes": loaded.get("latched_modes")})
    return out


def mp_server_workload(seconds: int, extra: list[str] | None = None) -> dict:
    return {
        "map": "mp/q4dm1", "multiplayer": True, "needs_stats": False,
        "steps": ["notarget", "god", "addbot Cortez 4 exact", "addbot Rhodes 4 exact", "addbot Sledge 4 exact",
                  "addbot Voss 4 exact", "@wait 120", *(extra or []), f"@wait {seconds * 60}",
                  *([s for s in ("stopMVD",) if extra and any(e.startswith("recordMVD") for e in extra)])],
    }


def run_pair(args, server_name: str, server_mode: str, client_name: str, client_mode: str,
             server_root: Path, client_root: Path, client_delay: float) -> tuple[dict, dict]:
    result: dict = {}

    def server():
        result["server"] = pw.run_workload(args, server_name, server_mode, 0, save_root=server_root)

    thread = threading.Thread(target=server)
    thread.start()
    time.sleep(client_delay)
    client = pw.run_workload(args, client_name, client_mode, 0, save_root=client_root)
    thread.join()
    return result["server"], client


def client_checks(args) -> list[dict]:
    out = []
    for server_mode, client_mode in (("enhanced", "legacy"), ("legacy", "enhanced")):
        pw.WORKLOADS["int-server"] = mp_server_workload(60)
        pw.WORKLOADS["int-client"] = {
            "map": "mp/q4dm1", "client": True, "needs_stats": False, "expect_mode": server_mode,
            "startup": ["+connect", f"127.0.0.1:{args.port}"],
            "steps": ["physicsStats start client", "@wait 1200", "physicsStats stop"],
        }
        root = pw.task_path(args.save_root) / f"client-{server_mode}-{client_mode}"
        srv, cli = run_pair(args, "int-server", server_mode, "int-client", client_mode,
                            root / "server", root / "client", args.client_delay)
        log = log_of(cli)
        notes = []
        if f"physics: {server_mode} simulation (from server)" not in log:
            notes.append(f"client did not follow the server: {cli.get('latched_modes')}")
        if not srv.get("latched_modes") or srv["latched_modes"][-1] != server_mode:
            notes.append(f"server committed {srv.get('latched_modes')}")
        if cli["errors"] or srv["errors"]:
            notes.append(f"errors: {(cli['errors'] + srv['errors'])[:2]}")
        out.append({"check": f"client {client_mode} on {server_mode} server", "passed": not notes, "notes": notes,
                    "client_stats": cli.get("stats")})
    return out


def mvd_check(args) -> list[dict]:
    root = pw.task_path(args.save_root) / "mvd"
    pw.WORKLOADS["int-record"] = mp_server_workload(40, ["recordMVD physics_enhanced"])
    recorded = pw.run_workload(args, "int-record", "enhanced", 0, save_root=root)
    pw.WORKLOADS["int-play"] = {
        "map": "mp/q4dm1", "client": True, "needs_stats": False, "expect_mode": "enhanced",
        "startup": ["+playMVD", "physics_enhanced"],
        "steps": ["physicsStats start mvd", "@wait 900", "physicsStats stop"],
    }
    played = pw.run_workload(args, "int-play", "legacy", 0, save_root=root)
    log = log_of(played)
    notes = []
    demos = list((root / "baseoq4" / "demos").glob("physics_enhanced*"))
    if not demos:
        notes.append("no demo recorded")
    if "physics: enhanced simulation (from server)" not in log:
        notes.append(f"playback did not use the recorded mode: {played.get('latched_modes')}")
    if played["errors"] or recorded["errors"]:
        notes.append(f"errors: {(played['errors'] + recorded['errors'])[:2]}")
    return [{"check": "mvd playback follows the recording", "passed": not notes, "notes": notes,
             "playback_stats": played.get("stats")}]


def dedicated_check(args) -> list[dict]:
    root = pw.task_path(args.save_root) / "dedicated"
    ded = args.exe.parent / ("openQ4-ded_x64.exe" if args.exe.suffix == ".exe" else "openQ4-ded_x64")
    server_args = argparse.Namespace(**vars(args))
    server_args.exe = ded
    # the dedicated server loads faster than the client boots its renderer: keep
    # it up long enough for the client to connect and run its own 15 seconds
    pw.WORKLOADS["int-ded"] = dict(mp_server_workload(120), expect_mode="enhanced")
    pw.WORKLOADS["int-ded"]["dedicated"] = True
    pw.WORKLOADS["int-client"] = {
        "map": "mp/q4dm1", "client": True, "needs_stats": False, "expect_mode": "enhanced",
        "startup": ["+connect", f"127.0.0.1:{args.port}"],
        "steps": ["@wait 900"],
    }
    result: dict = {}

    def server():
        result["server"] = pw.run_workload(server_args, "int-ded", "enhanced", 0, save_root=root / "server")

    thread = threading.Thread(target=server)
    thread.start()
    time.sleep(args.client_delay)
    client = pw.run_workload(args, "int-client", "legacy", 0, save_root=root / "client")
    thread.join()
    srv = result["server"]
    notes = []
    if not srv.get("latched_modes") or srv["latched_modes"][-1] != "enhanced":
        notes.append(f"dedicated committed {srv.get('latched_modes')}")
    if "physics: enhanced simulation (from server)" not in log_of(client):
        notes.append(f"client did not follow the dedicated server: {client.get('latched_modes')}")
    if srv["errors"]:
        notes.append(f"server errors: {srv['errors'][:2]}")
    return [{"check": "dedicated enhanced server", "passed": not notes, "notes": notes}]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=pw.ROOT / ".install/openQ4-client_x64.exe")
    parser.add_argument("--retail", type=Path)
    parser.add_argument("--save-root", type=Path, default=pw.ROOT / ".tmp/physics-integration")
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--port", type=int, default=28979)
    parser.add_argument("--client-delay", type=float, default=35.0, help="seconds before a client joins a server")
    parser.add_argument("--checks", default="saves,client,mvd,dedicated")
    parser.add_argument("--min-free-gb", type=float, default=5.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    args.set = []
    args.verbose_digest = False
    args.map = None
    wanted = args.checks.split(",")
    results = []
    for name, check in (("saves", saves), ("client", client_checks), ("mvd", mvd_check), ("dedicated", dedicated_check)):
        if name in wanted:
            results += check(args)
    for r in results:
        print(f"{r['check']:40s} {'PASS' if r['passed'] else 'FAIL'} {'; '.join(r['notes'])}")
    if args.output:
        out = pw.task_path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
