#!/usr/bin/env python3
"""Qualify autoaction on a cheats-off dedicated server (MP-22).

The server sets si_autoAction "demo stats" and plays a one-minute Deathmatch
with two bots; a hidden client with ui_autoAction "demo ss stats" joins and
does nothing else. When the match ends the run must show:

* the server recording the match as a multi-view demo and writing its stats,
  both under one MPBuildMatchBasename name with no player in it;
* the client recording its own view, taking a screenshot of the result and
  writing its stats, all under one name that ends with the player's name;
* both demos finished (an openQ4 MVD header, no staged .part left), and both
  stats files JSON with the players and their scores.

Peers bind 127.0.0.1 only, so a runtime at a new path raises no firewall
prompt.
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

from mp_map_rotation_smoke import Role
from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""
MAP = "mp/q4dm1"
NAME = re.compile(r"\d{8}-\d{6}_[a-z0-9_.-]+")


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--port", type=int, default=28211)
    parser.add_argument("--client-port", type=int, default=28212)
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--startup-timeout", type=int, default=300)
    args = parser.parse_args()
    runtime, output, assets = args.runtime_dir.resolve(), args.output_dir.resolve(), args.basepath.resolve()
    verify_runtime_is_current(runtime)
    paths = {
        "engine": runtime / f"openQ4-client_x64{SUFFIX}",
        "dedicated": runtime / f"openQ4-ded_x64{SUFFIX}",
        "game_mp": runtime / "baseoq4" / ("game-mp_x64" + (".dll" if os.name == "nt" else ".so")),
    }
    if any(not path.is_file() for path in paths.values()) or not (assets / "q4base").is_dir():
        parser.error("a complete staged runtime and the retail PK4 assets are required")
    output.mkdir(parents=True, exist_ok=False)
    report = {
        "status": "fail",
        "source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "runtime_dir": str(runtime), "runtime_sha256": {key: digest(path) for key, path in paths.items()},
        "checks": [], "commands": {}, "exit_codes": {}, "failures": [],
    }
    games = {"server": output / "server" / "baseoq4", "player": output / "player" / "baseoq4"}
    for game in games.values():
        game.mkdir(parents=True, exist_ok=True)
    server = Role("server", games["server"])
    logs_of = {"server": server.log, "player": games["player"] / "logs/openq4.log"}
    write_script(games["server"] / "rot_server_start.cfg", [
        "set bot_minPlayers 0", "set bot_skill 3", "addbot", "addbot", "echo ROT_SERVER_READY", *server.start_lines()])
    common = {
        "fs_basepath": str(assets), "fs_game": "baseoq4", "com_gameMode": "MP", "si_gameType": "DM",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1", "s_noSound": "1",
        "in_mouse": "0", "in_joystick": "0", "com_skipLoadingContinue": "1", "net_allowCheats": "0",
        "net_LANServer": "1", "net_clientDownload": "0", "net_enableIPv4": "1", "net_enableIPv6": "0",
        "net_ip": "127.0.0.1", "g_autoExecAfterMapLoadDelayMs": "500",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
    }
    processes, streams = {}, []

    def launch(role: str, executable: Path, cvars: dict[str, str], tail: list[str]) -> None:
        command = [str(executable)] + [part for key, value in cvars.items() for part in ("+set", key, value)] + tail
        report["commands"][role] = command
        stream = (output / f"{role}-console.log").open("w", encoding="utf-8")
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

    def wait(label: str, predicate, seconds: int | None = None) -> dict[str, str]:
        until = time.monotonic() + (seconds or args.timeout)
        while time.monotonic() < until:
            texts = {name: read_log(path) for name, path in logs_of.items()}
            if re.search(r"FATAL ERROR|Unknown command", "\n".join(texts.values())):
                raise RuntimeError("engine error during " + label)
            if predicate(texts):
                return texts
            ended = [name for name, process in processes.items() if process.poll() is not None]
            if ended:
                raise RuntimeError(f"{', '.join(ended)} ended during {label}")
            time.sleep(0.5)
        raise RuntimeError("timeout during " + label)

    def check(name: str, passed: bool, detail: str = "") -> None:
        report["checks"].append({"name": name, "passed": bool(passed), "detail": detail[-600:]})
        print(("ok   " if passed else "FAIL ") + name, flush=True)
        if not passed:
            report["failures"].append(name)

    def stats(game: Path, name: str) -> dict | None:
        try:
            return json.loads((game / "stats" / f"{name}.json").read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return None

    def finished_demo(game: Path, name: str) -> bool:
        demo = game / "demos" / f"{name}.mvd"
        return demo.is_file() and demo.read_bytes()[:6] == b"OQ4MVD" and not (game / "demos" / f"{name}.mvd.part").exists()

    started = time.monotonic()
    try:
        launch("server", paths["dedicated"],
               dict(common, fs_savepath=str(games["server"].parent), fs_devpath=str(games["server"].parent),
                    net_port=str(args.port), net_serverDedicated="1", si_pure="1", si_name="MP22 autoaction",
                    si_maxPlayers="8", si_warmup="0", si_timeLimit="1", si_fragLimit="0", si_mapCycle="",
                    g_mapRotation="0", g_gameReviewPause="10", si_autoAction="demo stats",
                    g_autoExecAfterMapLoad="rot_server_start.cfg"),
               ["+spawnServer", MAP])
        wait("server startup", lambda t: "\nROT_SERVER_READY" in t["server"], args.startup_timeout)
        launch("player", paths["engine"],
               dict(common, fs_savepath=str(games["player"].parent), fs_devpath=str(games["player"].parent),
                    net_port=str(args.client_port), ui_name="Auto Player", ui_autoJoin="1", ui_spectate="Play",
                    ui_autoAction="demo ss stats", r_renderApi="gl", r_fullscreen="0", r_fullscreenDesktop="0",
                    r_borderless="0", r_borderlessDefaultMigrated="1", r_hiddenWindow="1", r_mode="-1",
                    r_customWidth="640", r_customHeight="360", r_windowWidth="640", r_windowHeight="360",
                    com_maxfps="60", r_swapInterval="0", ui_retained="0"),
               ["+connect", f"127.0.0.1:{args.port}"])
        texts = wait("the player's match starts", lambda t: "autoaction: this player's match " in t["player"],
                     args.startup_timeout)
        player_name = re.search(r"autoaction: this player's match (\S+)", texts["player"])[1]
        server_name = re.search(r"autoaction: the server's match (\S+)", texts["server"])
        report["names"] = {"player": player_name, "server": server_name[1] if server_name else None}
        check("the server's match is named after the match alone",
              server_name is not None and NAME.fullmatch(server_name[1]) is not None and
              server_name[1].endswith("_dm_q4dm1"), json.dumps(report["names"]))
        check("the player's match is named after the match and the player",
              NAME.fullmatch(player_name) is not None and player_name.endswith("_dm_q4dm1_auto_player"),
              json.dumps(report["names"]))
        try:  # the record command runs a frame after the edge
            texts = wait("the player's demo starts",
                         lambda t: f"Recording your view to 'demos/{player_name}.mvd'" in t["player"], 30)
        except RuntimeError:
            texts = {name: read_log(path) for name, path in logs_of.items()}
        check("the player's demo starts with live play", f"Recording your view to 'demos/{player_name}.mvd'" in texts["player"],
              texts["player"][-500:])

        texts = wait("the match ends and the results are written",
                     lambda t: f"autoaction: wrote stats/{player_name}.json" in t["player"] and
                     server_name is not None and f"autoaction: wrote stats/{server_name[1]}.json" in t["server"],
                     args.timeout)
        time.sleep(3)  # the screenshot and the demos' last writes
        texts = {name: read_log(path) for name, path in logs_of.items()}
        check("the server records the match and stops at the result",
              finished_demo(games["server"], server_name[1]) and
              f"Stopped MVD recording (match ended): 'demos/{server_name[1]}.mvd'" in texts["server"], texts["server"][-500:])
        check("the player's demo stops at the result",
              finished_demo(games["player"], player_name) and
              f"Stopped MVD recording (console command): 'demos/{player_name}.mvd'" in texts["player"], texts["player"][-500:])
        shot = games["player"] / "screenshots" / f"{player_name}.tga"
        check("the player gets a screenshot of the result", shot.is_file() and shot.stat().st_size > 100000, str(shot))
        for role, name in (("server", server_name[1]), ("player", player_name)):
            data = stats(games[role], name)
            players = data.get("players", []) if data else []
            report.setdefault("stats", {})[role] = data
            check(f"the {role}'s stats name the match and every player with a score",
                  data is not None and data.get("format") == "openq4-match-stats" and data.get("match") == name and
                  len(players) >= 3 and all("score" in p and "name" in p for p in players) and
                  any(p["name"] == "Auto Player" for p in players), json.dumps(data)[:500])

        # The server's recording, watched: its players by name
        for name in list(processes):
            processes[name].kill()
            processes.pop(name).wait()
        server_demo = games["server"] / "demos" / f"{server_name[1]}.mvd"
        (games["player"] / "demos" / server_demo.name).write_bytes(server_demo.read_bytes())
        logs_of["viewer"] = games["player"] / "logs/openq4-viewer.log"
        write_script(games["player"] / "mp22_viewer_boot.cfg", ["waitMsec 2000", f'playMVD "{server_name[1]}"'])
        write_script(games["player"] / "mp22_viewer.cfg", [
            "waitMsec 3000", "echo MP22_PLAYERS_BEGIN", "demoFind auto player", "demoPlayers", 'demoFollow "Auto Player"', "waitMsec 1000",
            "mvdStatus", "echo MP22_PLAYERS_END", "waitMsec 1000", "quit"])
        launch("viewer", paths["engine"],
               dict(common, fs_savepath=str(games["player"].parent), fs_devpath=str(games["player"].parent),
                    logFileName="logs/openq4-viewer.log", g_autoExecAfterMapLoad="mp22_viewer.cfg",
                    net_port=str(args.client_port), r_renderApi="gl", r_fullscreen="0", r_fullscreenDesktop="0",
                    r_borderless="0", r_borderlessDefaultMigrated="1", r_hiddenWindow="1", r_mode="-1",
                    r_customWidth="640", r_customHeight="360", r_windowWidth="640", r_windowHeight="360",
                    com_maxfps="60", r_swapInterval="0", ui_retained="0"),
               ["+exec", "mp22_viewer_boot.cfg"])
        watched = wait("the viewer lists the players", lambda t: "\nMP22_PLAYERS_END" in t["viewer"],
                       args.startup_timeout)["viewer"]
        listed = watched.rpartition("\nMP22_PLAYERS_END")[0].rpartition("\nMP22_PLAYERS_BEGIN")[2]
        slot = re.search(r"^[ >]\s*(\d+)\s+Auto Player$", listed, re.MULTILINE)
        followed = re.search(r"MVD_STATUS playing=1 [^\n]*follow=(\d+)", listed)
        check("demoFind finds the server's recording by a player who was in it",
              re.search(rf"DEMO_FIND demos/{re.escape(server_name[1])}\.mvd\s+mp/q4dm1\s+DM\s+[^\n]*Auto Player", listed)
              is not None, listed[-600:])
        check("demoPlayers lists the recorded players and demoFollow follows one by name",
              slot is not None and re.search(r"(\d+) players; demoFollow", listed) is not None and
              followed is not None and followed[1] == slot[1], listed[-600:])
    except Exception as error:  # noqa: BLE001 - the report records the failure
        report["failures"].append(f"exception: {error}")
        print(f"FAIL {error}", flush=True)
    finally:
        for name, process in list(processes.items()):
            if process.poll() is None:
                process.kill()  # Only the processes this test launched.
                process.wait()
            report["exit_codes"][name] = process.returncode
        for stream in streams:
            stream.close()
    if {key: digest(path) for key, path in paths.items()} != report["runtime_sha256"]:
        report["failures"].append("runtime changed during qualification")
    report["seconds"] = round(time.monotonic() - started, 1)
    report["status"] = "pass" if not report["failures"] else "fail"
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"status": report["status"], "failures": report["failures"], "seconds": report["seconds"]}, indent=2), flush=True)
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
