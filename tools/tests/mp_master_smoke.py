#!/usr/bin/env python3
"""Qualify Internet discovery through an openQ4 master over loopback (MP-19).

tools/master/openq4_master.py listens on 127.0.0.1 (with --allow-private, as a
test or a LAN needs); a pure dedicated server with net_LANServer 0 and
net_master0 pointed at it starts a map; a hidden client that never connects
points its own net_master0 at the master, runs NETScan (the browser's Get New
List on the Internet source) and then listServers. The run must show:

* the master listing the server only once the server answered the master's
  challenge, and writing it to the JSON list;
* the client's list naming the server, once, with its name and a ping, so the
  master's reply, both list formats and the browser's own ping all worked.

Peers bind 127.0.0.1 only, so a runtime at a new path raises no firewall
prompt. Not covered: the HTTPS list (net_serverListUrl), which needs a
certificate the machine trusts; mp_server_discovery.py tests its reader.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

from mp_map_rotation_smoke import Role
from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""
MAP = "mp/q4dm1"
NAME = "MP19 master smoke"


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--master-port", type=int, default=28190)
    parser.add_argument("--port", type=int, default=28191)
    parser.add_argument("--client-port", type=int, default=28192)
    parser.add_argument("--timeout", type=int, default=90, help="maximum seconds for any one step")
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
    games = {name: output / name / "baseoq4" for name in ("server", "client")}
    for game in games.values():
        game.mkdir(parents=True, exist_ok=True)
    server = Role("server", games["server"])
    client_log = games["client"] / "logs/openq4.log"
    write_script(games["server"] / "rot_server_start.cfg", ["echo ROT_SERVER_READY", *server.start_lines()])
    # The browser's search, from the console: the request, time for the replies and
    # the pings, then the list. A second listing covers a slow first ping.
    write_script(games["client"] / "mp19_browser.cfg", [
        "waitMsec 2000", "echo MP19_SCAN", "NETScan", "waitMsec 6000",
        "echo MP19_LIST_BEGIN", "listServers", "echo MP19_LIST_END", "waitMsec 4000",
        "echo MP19_LIST2_BEGIN", "listServers", "echo MP19_LIST2_END", "quit"])
    master = f"127.0.0.1:{args.master_port}"
    address = f"127.0.0.1:{args.port}"
    # the engine prints a loopback address as localhost
    shown = rf"(?:127\.0\.0\.1|localhost):{args.port}"
    shown_master = rf"(?:127\.0\.0\.1|localhost):{args.master_port}"
    common = {
        "fs_basepath": str(assets), "fs_game": "baseoq4", "com_gameMode": "MP", "si_gameType": "DM",
        "logFile": "2", "developer": "1", "s_noSound": "1", "logFileName": "logs/openq4.log",
        "in_mouse": "0", "in_joystick": "0", "com_skipLoadingContinue": "1",
        "net_LANServer": "0", "net_clientDownload": "0", "net_enableIPv4": "1", "net_enableIPv6": "0",
        "net_ip": "127.0.0.1", "net_master0": master, "net_master1": "", "net_master2": "", "net_master3": "",
        "net_master4": "", "net_serverListUrl": "",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
    }
    window = {
        "r_renderApi": "gl", "r_fullscreen": "0", "r_fullscreenDesktop": "0", "r_borderless": "0",
        "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_mode": "-1", "r_customWidth": "640",
        "r_customHeight": "360", "r_windowWidth": "640", "r_windowHeight": "360", "com_maxfps": "60",
        "r_swapInterval": "0", "ui_retained": "0", "ui_autoJoin": "0",
    }
    processes, streams = {}, []

    def launch(role: str, command: list[str]) -> None:
        report["commands"][role] = command
        stream = (output / f"{role}-console.log").open("w", encoding="utf-8")
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=runtime if role != "master" else ROOT, stdout=stream,
                                           stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

    def engine(role: str, executable: Path, cvars: dict[str, str], tail: list[str]) -> None:
        launch(role, [str(executable)] + [part for key, value in cvars.items() for part in ("+set", key, value)] + tail)

    def texts() -> dict[str, str]:
        return {"server": read_log(server.log), "client": read_log(client_log),
                "master": read_log(output / "master-console.log")}

    def wait(label: str, predicate, seconds: int | None = None, allow_ended: tuple[str, ...] = ()) -> dict[str, str]:
        until = time.monotonic() + (seconds or args.timeout)
        while time.monotonic() < until:
            current = texts()
            if re.search(r"FATAL ERROR|Unknown command", current["server"] + current["client"]):
                raise RuntimeError("engine error during " + label)
            if predicate(current):
                return current
            ended = [name for name, process in processes.items() if process.poll() is not None and name not in allow_ended]
            if ended:
                raise RuntimeError(f"{', '.join(ended)} ended during {label}")
            time.sleep(0.25)
        raise RuntimeError("timeout during " + label)

    def check(name: str, passed: bool, detail: str = "") -> None:
        report["checks"].append({"name": name, "passed": bool(passed), "detail": detail[-600:]})
        print(("ok   " if passed else "FAIL ") + name, flush=True)
        if not passed:
            report["failures"].append(name)

    def section(text: str, begin: str, end: str) -> str:
        return text.rpartition("\n" + end)[0].rpartition("\n" + begin)[2]

    started = time.monotonic()
    listing = output / "servers.json"
    try:
        launch("master", [sys.executable, str(ROOT / "tools" / "master" / "openq4_master.py"),
                          "--port", str(args.master_port), "--bind", "127.0.0.1", "--allow-private",
                          "--json", str(listing), "--json-interval", "1", "--ready-file", str(output / "master.ready")])
        wait("master startup", lambda _t: (output / "master.ready").is_file(), 30)

        engine("server", paths["dedicated"],
               dict(common, fs_savepath=str(games["server"].parent), fs_devpath=str(games["server"].parent),
                    net_port=str(args.port), net_serverDedicated="1", si_pure="1", si_name=NAME, si_maxPlayers="8",
                    si_warmup="0", si_timeLimit="0", si_fragLimit="0", si_mapCycle="", g_mapRotation="0",
                    g_autoExecAfterMapLoadDelayMs="500", g_autoExecAfterMapLoad="rot_server_start.cfg"),
               ["+spawnServer", MAP])
        wait("server startup", lambda t: "\nROT_SERVER_READY" in t["server"], args.startup_timeout)
        heard = wait("the master lists the server", lambda t: f"listed {address} " in t["master"])
        check("the server sends its heartbeat to net_master0", re.search(rf"Sending heartbeat to {shown_master}\b", heard["server"]) is not None,
              heard["server"][-400:])
        line = re.search(rf"listed {re.escape(address)} protocol (\d+)\.(\d+) game '([^']*)' name '([^']*)'", heard["master"])
        check("the master lists it once it answers the challenge, with its game and name",
              line is not None and line.group(3) == "baseoq4" and line.group(4) == NAME, heard["master"][-400:])

        deadline = time.monotonic() + args.timeout
        entries = []
        while time.monotonic() < deadline and not entries:
            try:
                entries = [entry for entry in json.loads(listing.read_text(encoding="utf-8")).get("servers", [])
                           if entry.get("address") == address]
            except (OSError, ValueError):
                entries = []
            time.sleep(0.5)
        check("the JSON list for the HTTPS fallback names the server and its map",
              len(entries) == 1 and entries[0].get("name") == NAME and entries[0].get("map") == MAP, json.dumps(entries))

        engine("client", paths["engine"],
               dict(common | window, fs_savepath=str(games["client"].parent), fs_devpath=str(games["client"].parent),
                    net_port=str(args.client_port), ui_name="Browser"),
               ["+exec", "mp19_browser.cfg"])
        found = wait("the client lists servers", lambda t: "\nMP19_LIST2_END" in t["client"], args.startup_timeout,
                     allow_ended=("client",))["client"]
        first, second = section(found, "MP19_LIST_BEGIN", "MP19_LIST_END"), section(found, "MP19_LIST2_BEGIN", "MP19_LIST2_END")
        report["lists"] = [first.strip(), second.strip()]
        entry = re.compile(rf"^\s*\d+: {re.escape(NAME)} (\d+)ms \({shown}\)$", re.MULTILINE)
        check("NETScan asks the master and the browser lists the server by name, with a ping",
              entry.search(first) is not None or entry.search(second) is not None, first + "\n--\n" + second)
        check("the server is listed once, though both list formats name it",
              max(len(re.findall(rf"\({shown}\)", second)), len(re.findall(rf"\({shown}\)", first))) == 1, second)
        report["exit_codes"]["client"] = processes.pop("client").wait(timeout=60)
        server.inject(["quit"])
        report["exit_codes"]["server"] = processes.pop("server").wait(timeout=30)
    except Exception as error:  # noqa: BLE001 - the report records the failure
        report["failures"].append(f"exception: {error}")
        print(f"FAIL {error}", flush=True)
    finally:
        for name, process in list(processes.items()):
            if process.poll() is None:
                process.kill()  # Only the processes this test launched.
                process.wait()
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
