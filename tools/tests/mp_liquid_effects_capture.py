#!/usr/bin/env python3
"""Capture multiplayer liquid splash and bubble presentation for review, without input.

A listen host on mp/liquid_lab plays in third person with god mode. It is
dropped from above each pool - deep water, slime and lava - and the engine
writes a burst of captures while it falls through the surface (entry splash),
then holds it submerged (head bubble trail) and lifts it back out (exit
splash). `g_debugLiquid` logs every liquid transition, so each burst is tied to
the server's entry, submersion and exit events. The captures are evidence for
an inspection by eye; the harness itself checks the logged transitions, the
capture files and a clean exit. No operating-system input is used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
# Liquid lab surfaces are at z = -16. Pool centres: deep water (north-west,
# 304 units deep), slime (south-east) and lava (south-west), both 64 units deep
# over a raised floor, so only the water pool can submerge a standing player;
# see maps/mp/liquid_lab.map. setviewpos places the eye.
POOLS = (("water", -720, 415, True), ("slime", 545, -415, False), ("lava", -545, -415, False))
DROP_HEIGHT = 150
SUBMERGED_Z = -200
BURST = 10
BURST_INTERVAL_MSEC = 70


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--port", type=int, default=28974)
    parser.add_argument("--tree-def", action="store_true",
                        help="load the working tree's def/liquids_openq4.def as a loose override "
                             "instead of the staged pak0 copy")
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    verify_runtime_is_current(runtime)
    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    if output.exists() or not exe.is_file() or not (args.basepath / "q4base").is_dir():
        parser.error("use a new output directory, a staged client and installed retail assets")
    output.mkdir(parents=True)
    paths = [exe, *sorted((runtime / "baseoq4").glob("game-mp*")), *sorted(runtime.glob("renderer-*")),
             runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4"]
    paths = [p for p in paths if p.is_file() and p.suffix not in (".pdb", ".lib", ".exp")]
    identity = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    game = output / "host" / "baseoq4"
    game.mkdir(parents=True)
    if args.tree_def:
        source = ROOT / "content/baseoq4/pak0/def/liquids_openq4.def"
        (game / "def").mkdir()
        (game / "def/liquids_openq4.def").write_bytes(source.read_bytes())

    steps = ['set g_autoExecAfterMapLoad ""', "god", "notarget", "set g_debugLiquid 1",
             "set pm_thirdPerson 1", "set pm_thirdPersonRange 170", "set pm_thirdPersonAngle 60",
             "set pm_thirdPersonHeight 40", "set bse_debug 1", "waitMsec 3000", "echo LIQUID_FX_READY"]
    shots = []
    for liquid, x, y, deep in POOLS:
        steps += [f"echo LIQUID_FX_DROP {liquid}", f"setviewpos {x} {y} {DROP_HEIGHT} 90"]
        for index in range(BURST):
            name = f"{liquid}_entry_{index:02d}"
            shots.append(name)
            steps += [f"waitMsec {BURST_INTERVAL_MSEC}", f"screenshot screenshots/{name}.tga"]
        steps += ["waitMsec 1500"]
        if deep:
            steps += [f"echo LIQUID_FX_SUBMERGE {liquid}", f"setviewpos {x} {y} {SUBMERGED_Z} 90",
                      "waitMsec 1200"]
        for index in range(3 if deep else 0):
            name = f"{liquid}_submerged_{index:02d}"
            shots.append(name)
            steps += ["waitMsec 250", f"screenshot screenshots/{name}.tga"]
        if deep:
            # Control: the same submerged view with effect rendering off. The head
            # bubble trail (stock recomp_bubble.fx, a 200-unit sphere of bubbles)
            # is the only effect here, so its particles must vanish.
            name = f"{liquid}_submerged_no_effects"
            shots.append(name)
            steps += ["set bse_render 0", "waitMsec 250", f"screenshot screenshots/{name}.tga",
                      "set bse_render 1", "waitMsec 250"]
        steps += [f"echo LIQUID_FX_EXIT {liquid}", f"setviewpos {x} {y} 40 90"]
        for index in range(4):
            name = f"{liquid}_exit_{index:02d}"
            shots.append(name)
            steps += [f"waitMsec {BURST_INTERVAL_MSEC}", f"screenshot screenshots/{name}.tga"]
        steps += ["setviewpos 0 0 128 90", "waitMsec 1500"]
    steps += ["echo LIQUID_FX_COMPLETE", "quit"]
    write_script(game / "liquid_fx.cfg", steps)

    cvars = {"win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
             "sys_consoleWindow": "0", "in_tty": "0", "in_mouse": "0", "in_joystick": "0",
             "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
             "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_renderApi": args.renderer,
             "r_mode": "-1", "r_customWidth": "960", "r_customHeight": "540", "r_windowWidth": "960",
             "r_windowHeight": "540", "r_swapInterval": "0", "com_maxfps": "60", "s_noSound": "1",
             "ui_autoJoin": "1", "ui_spectate": "Play", "ui_name": "LiquidFx", "fs_game": "baseoq4",
             "com_gameMode": "MP", "com_nextGameModule": "game_mp",
             "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(game.parent),
             "fs_devpath": str(game.parent), "logFile": "2", "logFileName": "logs/openq4.log",
             "developer": "1", "net_allowCheats": "1", "net_serverDedicated": "0", "net_LANServer": "1",
             "net_port": str(args.port), "net_ip": "127.0.0.1", "net_enableIPv6": "0", "si_pure": "0",
             "si_gameType": "DM", "si_minPlayers": "1", "si_maxPlayers": "4", "si_warmup": "0",
             "si_useReady": "0", "si_fragLimit": "999", "si_timeLimit": "0", "g_matchProfile": "casual",
             "bot_minPlayers": "0", "com_skipLoadingContinue": "1",
             "g_autoExecAfterMapLoad": "liquid_fx.cfg", "g_autoExecAfterMapLoadDelayMs": "500"}
    command = [str(exe), *[part for key, value in cvars.items() for part in ("+set", key, value)],
               "+spawnServer", "mp/liquid_lab"]
    (output / "host-launch.json").write_text(json.dumps(command, indent=2))
    failures = []
    with (output / "host-console.log").open("w") as stream:
        process = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            if process.wait(timeout=240) != 0:
                failures.append("host exited abnormally")
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            failures.append("host timed out")
    log = re.sub(r"\^[0-9A-Za-z]", "", read_log(game / "logs/openq4.log"))
    if "LIQUID_FX_COMPLETE" not in log or re.search(r"ERROR:|FATAL:|Unknown command", log):
        failures.append("host did not complete cleanly")
    if not re.search(rf"Renderer API: {args.renderer} \(", log):
        failures.append("the requested renderer did not activate")
    events = {}
    for liquid, _, _, deep in POOLS:
        body = log.partition(f"LIQUID_FX_DROP {liquid}")[2].partition("LIQUID_FX_DROP ")[0]
        transitions = re.findall(r"liquid: level (\d) -> (\d), type 0x[0-9a-f]+ \(([^)]*)\)", body)
        events[liquid] = transitions
        entered = any(a == "0" and b != "0" and liquid in name.lower() for a, b, name in transitions)
        submerged = not deep or any(b == "3" and liquid in name.lower() for _, b, name in transitions)
        left = any(a != "0" and b == "0" for a, b, _ in transitions)
        if not (entered and submerged and left):
            failures.append(f"{liquid}: missing entry/submersion/exit transitions {transitions}")
    # bse_debug 1 logs every effect the client plays: each pool's splash and the
    # deep pool's head bubble trail must actually be played, not only captured.
    played = {}
    for liquid, _, _, deep in POOLS:
        body = log.partition(f"LIQUID_FX_DROP {liquid}")[2].partition("LIQUID_FX_DROP ")[0]
        names = re.findall(r"Playing effect: (\S+)", body)
        played[liquid] = sorted(set(names))
        splash = {"water": "splash_lrg", "slime": "pipesplash", "lava": "wastesplash"}[liquid]
        if not any(splash in name for name in names):
            failures.append(f"{liquid}: the splash effect was not played")
        if deep and not any("recomp_bubble" in name for name in names):
            failures.append(f"{liquid}: the head bubble trail was not played")
    missing = [s for s in shots if not (game / f"screenshots/{s}.tga").is_file() or
               (game / f"screenshots/{s}.tga").stat().st_size < 1024]
    if missing:
        failures.append(f"missing captures: {missing}")
    report = {"status": "fail" if failures else "pass", "renderer": args.renderer, "identity": identity,
              "tree_def": args.tree_def, "transitions": events, "played_effects": played, "captures": [str(game / f"screenshots/{s}.tga") for s in shots],
              "failures": failures, "log": str(game / "logs/openq4.log")}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "failures": failures, "transitions": events}, indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
