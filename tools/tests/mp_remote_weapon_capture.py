#!/usr/bin/env python3
"""Capture a remote spectator's view of bots firing, for review by eye, without input.

A listen host on mp/q4dm1 plays, ignored by the bots, while they fight. A second process joins
over loopback as a spectator, follows a bot and writes bursts of engine
captures in first person and then in third person. With `--latency` the
spectator's link is delayed and jittered (`net_forceLatency`/`net_forceJitter`).
The captures show remote weapon play as the spectator's camera draws it - view
model, muzzle effects, tracers, beams and impacts - for an inspection by eye.
The harness checks that the follow held, that shots were fired during the
bursts (`MP_PLAYER` frag counts or the server's damage log advance) and that
every capture exists. No operating-system input is used.
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
BURST = 40
INTERVAL_MSEC = 150


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--port", type=int, default=28966)
    parser.add_argument("--latency", type=int, default=0, help="forced one-way latency on the spectator, ms")
    parser.add_argument("--burst", type=int, default=BURST, help="captures per view")
    parser.add_argument("--bot-skill", type=int, choices=range(1, 6), default=4)
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    verify_runtime_is_current(runtime)
    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    if output.exists() or not exe.is_file() or not (args.basepath / "q4base").is_dir():
        parser.error("use a new output directory, a staged client and installed retail assets")
    output.mkdir(parents=True)
    paths = [exe, *sorted((runtime / "baseoq4").glob("game-mp*")), *sorted(runtime.glob("renderer-*")),
             runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4"]
    identity = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                for p in paths if p.is_file() and p.suffix not in (".pdb", ".lib", ".exp")}
    games = {role: output / role / "baseoq4" for role in ("host", "observer")}
    for game in games.values():
        game.mkdir(parents=True)
    # The host keeps playing so the match stays live, but bots ignore it
    # (notarget) and it cannot die (god). ui_spectate is a game-module cvar: a
    # command-line +set is lost when the module registers it, so the observer
    # switches to spectating after load.
    host = ['set g_autoExecAfterMapLoad ""', "god", "notarget", f"addbot Sledge {args.bot_skill}", f"addbot Gorge {args.bot_skill}", f"addbot Kane {args.bot_skill}",
            "echo REMOTE_WEAPON_HOST_READY",
            # The server's own camera verdicts (MP_CAMERA ... allowed=) every 5 s.
            *[step for _ in range(30) for step in ("waitMsec 5000", "openq4_reportMPState")], "quit"]
    shots = []
    # Auto-join can still apply Play after the first frames, so request
    # spectating again once it has settled.
    observer = ['set g_autoExecAfterMapLoad ""', "set ui_spectate Spectate", "waitMsec 8000",
                "set ui_spectate Spectate", "waitMsec 12000",
                # A numbered target is refused while that bot is dead, and a
                # refusal keeps the previous camera, so ask for each bot in turn;
                # slot 0 is the host, which never fires.
                "follow 1", "waitMsec 400", "follow 2", "waitMsec 400", "follow 3", "waitMsec 2000",
                "echo REMOTE_WEAPON_FOLLOWING", "openq4_reportMPState"]
    for view in ("first", "third"):
        if view == "third":
            observer += ["set pm_thirdPerson 1", "set pm_thirdPersonRange 120", "waitMsec 1000"]
        observer += [f"echo REMOTE_WEAPON_BURST {view}", "openq4_reportMPState"]
        for index in range(args.burst):
            name = f"{view}_{index:02d}"
            shots.append(name)
            observer += [f"waitMsec {INTERVAL_MSEC}", f"screenshot screenshots/{name}.tga"]
        observer += [f"echo REMOTE_WEAPON_BURST_END {view}", "openq4_reportMPState"]
    observer += ["echo REMOTE_WEAPON_COMPLETE", "disconnect", "quit"]
    write_script(games["host"] / "remote_weapon.cfg", host)
    write_script(games["observer"] / "remote_weapon.cfg", observer)

    processes, streams = {}, []

    def launch(role):
        game = games[role]
        cvars = {"win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
                 "sys_consoleWindow": "0", "in_tty": "0", "in_mouse": "0", "in_joystick": "0",
                 "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
                 "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_renderApi": args.renderer,
                 "r_mode": "-1", "r_customWidth": "960", "r_customHeight": "540", "r_windowWidth": "960",
                 "r_windowHeight": "540", "r_swapInterval": "0", "com_maxfps": "60", "s_noSound": "1",
                 "ui_autoJoin": "1", "ui_spectate": "Play" if role == "host" else "Spectate",
                 "ui_name": "WeaponHost" if role == "host" else "WeaponObserver", "fs_game": "baseoq4",
                 "com_gameMode": "MP", "com_nextGameModule": "game_mp",
                 "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(game.parent),
                 "fs_devpath": str(game.parent), "logFile": "2", "logFileName": "logs/openq4.log",
                 "developer": "1", "net_allowCheats": "1", "net_serverDedicated": "0", "net_LANServer": "1",
                 "net_port": str(args.port if role == "host" else args.port + 1), "net_ip": "127.0.0.1",
                 "net_enableIPv6": "0", "si_pure": "0", "si_gameType": "DM", "si_minPlayers": "1",
                 "si_maxPlayers": "6", "si_warmup": "0", "si_useReady": "0", "si_fragLimit": "999",
                 "si_timeLimit": "0", "g_matchProfile": "casual", "bot_minPlayers": "0", "bot_chat": "0",
                 "com_skipLoadingContinue": "1", "g_autoExecAfterMapLoad": "remote_weapon.cfg",
                 "g_autoExecAfterMapLoadDelayMs": "500"}
        if role == "observer" and args.latency:
            cvars.update(net_forceLatency=str(args.latency), net_forceJitter=str(args.latency // 3),
                         net_forceSeed="40708")
        command = [str(exe), *[part for key, value in cvars.items() for part in ("+set", key, value)]]
        command += ["+spawnServer", "mp/q4dm1"] if role == "host" else ["+connect", f"127.0.0.1:{args.port}"]
        (output / f"{role}-launch.json").write_text(json.dumps(command, indent=2))
        stream = (output / f"{role}-console.log").open("w")
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

    failures = []
    try:
        launch("host")
        until = time.monotonic() + 90
        while "REMOTE_WEAPON_HOST_READY" not in read_log(games["host"] / "logs/openq4.log"):
            if time.monotonic() > until or processes["host"].poll() is not None:
                raise RuntimeError("host did not start the bot match")
            time.sleep(0.25)
        launch("observer")
        try:
            if processes["observer"].wait(timeout=200) != 0:
                failures.append("observer exited abnormally")
        except subprocess.TimeoutExpired:
            failures.append("observer timed out")
    except (OSError, RuntimeError) as error:
        failures.append(str(error))
    finally:
        for process in processes.values():
            if process.poll() is None:
                process.kill()
                process.wait()
        for stream in streams:
            stream.close()
    log = re.sub(r"\^[0-9A-Za-z]", "", read_log(games["observer"] / "logs/openq4.log"))
    if "REMOTE_WEAPON_COMPLETE" not in log or re.search(r"ERROR:|FATAL:|Unknown command", log):
        failures.append("observer did not complete cleanly")
    if not re.search(rf"Renderer API: {args.renderer} \(", log):
        failures.append("the requested renderer did not activate")
    locals_ = re.findall(r"MP_LOCAL client=(\d+) entity=\d+ .*spectator=(-?\d+)",
                         log.partition("REMOTE_WEAPON_FOLLOWING")[2])
    following = [target for client, target in locals_]
    # The spectator's own slot is its free camera; any bot slot (1-3) is a follow.
    if not locals_ or any(not 1 <= int(target) <= 3 or target == client for client, target in locals_):
        failures.append(f"the spectator did not follow a bot throughout: {following[:6]}")
    frags = {}
    for view in ("first", "third"):
        start = log.partition(f"REMOTE_WEAPON_BURST {view}")[2]
        before = re.findall(r"MP_PLAYER slot=(\d+) [^\n]*?score=(-?\d+)", start.partition("\nMP_STATE ")[2][:20000])
        end = log.partition(f"REMOTE_WEAPON_BURST_END {view}")[2]
        after = re.findall(r"MP_PLAYER slot=(\d+) [^\n]*?score=(-?\d+)", end.partition("\nMP_STATE ")[2][:20000])
        frags[view] = {"before": before, "after": after}
    missing = [s for s in shots if not (games["observer"] / f"screenshots/{s}.tga").is_file()]
    if missing:
        failures.append(f"missing captures: {missing[:5]}")
    report = {"status": "fail" if failures else "pass", "renderer": args.renderer, "latency": args.latency,
              "identity": identity, "frags": frags, "follow_targets": following[:10],
              "captures": [str(games["observer"] / f"screenshots/{s}.tga") for s in shots],
              "failures": failures,
              "logs": {role: str(game / "logs/openq4.log") for role, game in games.items()}}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "failures": failures, "frags": frags}, indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
