#!/usr/bin/env python3
"""Qualify a staged Linux WSL server with two successive native Windows clients.

Both binaries stay at stable package paths. Commands, logs and engine screenshots
are isolated in a new output directory. This is a real UDP/pure-content peer
check, not a cross-platform save or public-network qualification.
"""
from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import subprocess
import time

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def wsl_path(path: Path) -> str:
    value = path.resolve().as_posix()
    if not re.match(r"^[A-Za-z]:/", value):
        raise ValueError(f"WSL smoke requires a local drive path: {path}")
    return "/mnt/" + value[0].lower() + value[2:]


def cvar_arguments(values: dict[str, str]) -> list[str]:
    return [part for key, value in values.items() for part in ("+set", key, value)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--linux-runtime", type=Path, default=ROOT / ".tmp/runtime/linux")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--address", required=True, help="Linux numeric address; include Windows scope ID for link-local IPv6")
    parser.add_argument("--discover-address", action="store_true",
                        help="resolve WSL eth0 after startup; --address selects its family and Windows scope")
    parser.add_argument("--wsl-case-insensitive", action="store_true",
                        help="verify case-insensitive mounted paths and use the matching engine lookup policy")
    parser.add_argument("--port", type=int, default=28994)
    parser.add_argument("--timeout", type=int, default=180, help="maximum seconds per client")
    parser.add_argument("--startup-timeout", type=int, default=300, help="maximum seconds for WSL map startup")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("run this fixture on Windows with WSL available")
    address = ipaddress.ip_address(args.address)
    if not 1024 <= args.port <= 65532 or min(args.timeout, args.startup_timeout) <= 0:
        parser.error("reserve three unprivileged ports and a positive timeout")
    runtime, linux = args.runtime_dir.resolve(), args.linux_runtime.resolve()
    output, assets = args.output_dir.resolve(), args.basepath.resolve()
    verify_runtime_is_current(runtime)
    paths = {
        "windows/engine": runtime / "openQ4-client_x64.exe",
        "windows/game": runtime / "baseoq4/game_x64.dll",
        "linux/engine": linux / "openQ4-ded_x64",
        "linux/game": linux / "baseoq4/game_x64.so",
    }
    for package, root in (("windows", runtime), ("linux", linux)):
        for name in ("pak0.pk4", "pak1.pk4", "mod.json"):
            paths[package + "/" + name] = root / "baseoq4" / name
    if any(not path.is_file() for path in paths.values()) or not (assets / "q4base").is_dir():
        parser.error("both complete staged runtimes and retail PK4 assets are required")
    output.mkdir(parents=True, exist_ok=False)
    report = {
        "status": "fail", "family": address.version, "address": str(address),
        "transport": "native Windows to WSL Linux UDP", "source": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True)),
        "runtime_sha256": {key: digest(path) for key, path in paths.items()},
        "assets_sha256": {path.name: digest(path) for path in sorted((assets / "q4base").glob("*.pk4"))},
        "commands": {}, "exit_codes": {}, "captures": {}, "failures": [],
    }
    for name in ("pak0.pk4", "pak1.pk4", "mod.json"):
        if report["runtime_sha256"]["windows/" + name] != report["runtime_sha256"]["linux/" + name]:
            report["failures"].append("runtime content differs: " + name)
    processes, streams = {}, []
    games = {role: output / role / "baseoq4" for role in ("server", "client1", "client2")}
    logs = {role: path / "logs/openq4.log" for role, path in games.items()}
    for game in games.values():
        game.mkdir(parents=True)
    server_selector = games["server"] / "peer_commands.cfg"
    server_pid = output / "server.pid"
    server_runner = output / "server_runner.py"
    server_runner.write_text(
        "import os, pathlib, sys\n"
        "pathlib.Path(sys.argv[1]).write_text(str(os.getpid()))\n"
        "os.execv(sys.argv[2], sys.argv[2:])\n", encoding="utf-8")
    write_script(server_selector, ["// Awaiting client completion."])
    write_script(games["server"] / "peer_pump.cfg", ["exec peer_commands.cfg", "waitMsec 500", "exec peer_pump.cfg"])
    write_script(games["server"] / "peer_start.cfg", ["set developer 1", "echo CROSS_PEER_SERVER_READY", "exec peer_pump.cfg"])
    if args.wsl_case_insensitive:
        probes = [linux / "baseoq4/mod.json", games["server"] / "peer_start.cfg",
                  next(iter(sorted((assets / "q4base").glob("*.pk4"))))]
        probe = "import pathlib,sys,json; print(json.dumps([p.with_name(p.name.swapcase()).samefile(p) for p in map(pathlib.Path,sys.argv[1:])]))"
        report["wsl_case_insensitive_probes"] = json.loads(subprocess.check_output(
            ["wsl.exe", "--exec", "python3", "-c", probe, *map(wsl_path, probes)], text=True, timeout=20))
        if not all(report["wsl_case_insensitive_probes"]):
            raise RuntimeError("case-insensitive lookup requested on a case-sensitive test path")
    common = {
        "fs_game": "baseoq4", "com_gameMode": "MP", "si_gameType": "DM", "logFile": "2",
        "logFileName": "logs/openq4.log", "developer": "0", "s_noSound": "1",
        "in_mouse": "0", "in_joystick": "0", "com_skipLoadingContinue": "1",
        "net_enableIPv4": str(int(address.version == 4)), "net_enableIPv6": str(int(address.version == 6)),
        "net_ip": "0.0.0.0", "net_ip6": "::", "net_allowCheats": "1",
        "g_autoExecAfterMapLoad": "peer_start.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
    }

    def launch(role: str, command: list[str], cwd: Path) -> None:
        report["commands"][role] = command
        stream = (output / (role + "-console.log")).open("w", encoding="utf-8")
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT)

    def wait_for(role: str, marker: str) -> None:
        deadline = time.monotonic() + (args.startup_timeout if role == "server" else args.timeout)
        while time.monotonic() < deadline:
            text = read_log(logs[role])
            if re.search(r"FATAL ERROR|ERROR:|Unknown command", text):
                raise RuntimeError(role + " reported an engine error")
            if marker in text:
                return
            if processes[role].poll() is not None:
                raise RuntimeError(role + " ended before " + marker)
            time.sleep(0.2)
        raise RuntimeError(role + " timed out before " + marker)

    started = time.monotonic()
    try:
        if report["failures"]:
            raise RuntimeError("matching content is required before pure qualification")
        server_cvars = dict(common, fs_basepath=wsl_path(assets), fs_savepath=wsl_path(games["server"].parent),
                            fs_devpath=wsl_path(games["server"].parent), sys_allowMultipleInstances="1",
                            net_port=str(args.port), net_serverDedicated="1", net_LANServer="1", si_pure="1",
                            si_maxPlayers="4", bot_minPlayers="0", si_warmup="0", si_timeLimit="0", si_fragLimit="0")
        if args.wsl_case_insensitive:
            server_cvars["fs_caseSensitiveOS"] = "0"
        command = ["wsl.exe", "--cd", str(linux), "--exec", "python3", wsl_path(server_runner),
                   wsl_path(server_pid), wsl_path(paths["linux/engine"]),
                   *cvar_arguments(server_cvars), "+spawnServer", "mp/q4dm1"]
        launch("server", command, linux)
        wait_for("server", "CROSS_PEER_SERVER_READY")
        if args.discover_address:
            # WSL can assign a new link-local address when its VM restarts.
            # Discover while our server keeps that VM alive, retaining the
            # caller's Windows interface scope (the Linux index is different).
            interfaces = json.loads(subprocess.check_output(
                ["wsl.exe", "--exec", "ip", "-j", "address", "show", "eth0"], text=True, timeout=15))
            candidates = [ipaddress.ip_address(info["local"])
                          for interface in interfaces for info in interface.get("addr_info", [])]
            candidates = [item for item in candidates if item.version == address.version
                          and item.is_link_local == address.is_link_local]
            if len(candidates) != 1:
                raise RuntimeError("WSL address discovery did not produce one matching address")
            report["requested_address"] = str(address)
            scope = getattr(address, "scope_id", None)
            address = ipaddress.ip_address(str(candidates[0]) + ("%" + scope if scope else ""))
            report["address"] = str(address)
        endpoint = f"[{address}]:{args.port}" if address.version == 6 else f"{address}:{args.port}"
        for number in (1, 2):
            role = f"client{number}"
            write_script(games[role] / "peer_start.cfg", [
                "set developer 1", "waitMsec 7000", "openq4_assertMPClientActive", "openq4_assertMPGameplayView", "openq4_reportMPState",
                'screenshot "screenshots/cross_peer.tga"', "waitMsec 1500", "openq4_assertMPClientActive",
                "openq4_assertMPGameplayView", "echo CROSS_PEER_CLIENT_COMPLETE", "wait 5", "quit",
            ])
            client_cvars = dict(common, fs_basepath=str(assets), fs_savepath=str(games[role].parent),
                                fs_devpath=str(games[role].parent), win_allowMultipleInstances="1",
                                net_port=str(args.port + number), ui_name="CrossPeer" + str(number),
                                ui_autoJoin="1", ui_spectate="Play", ui_retained="1", r_renderApi="gl",
                                r_fullscreen="0", r_fullscreenDesktop="0", r_borderless="0", r_borderlessDefaultMigrated="1",
                                r_hiddenWindow="1", r_mode="-1", r_customWidth="640", r_customHeight="480",
                                r_windowWidth="640", r_windowHeight="480", com_maxfps="60", r_swapInterval="0")
            launch(role, [str(paths["windows/engine"]), *cvar_arguments(client_cvars), "+connect", endpoint], runtime)
            wait_for(role, "CROSS_PEER_CLIENT_COMPLETE")
            report["exit_codes"][role] = processes[role].wait(timeout=20)
            if report["exit_codes"][role] != 0:
                raise RuntimeError(role + " exited unsuccessfully")
            capture = games[role] / "screenshots/cross_peer.tga"
            if not capture.is_file():
                raise RuntimeError(role + " did not write an engine screenshot")
            report["captures"][role] = {"path": str(capture), "sha256": digest(capture)}
            print(role + " reached pure-server gameplay and exited cleanly", flush=True)
        write_script(server_selector, ["echo CROSS_PEER_SERVER_COMPLETE", "quit"])
        report["exit_codes"]["server"] = processes["server"].wait(timeout=20)
        if report["exit_codes"]["server"] != 0:
            raise RuntimeError("server exited unsuccessfully")
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        report["failures"].append(str(error))
    finally:
        # Engine commands end the Linux process. Killing only wsl.exe can leave
        # its server running, so retain the pump's quit even after a failed client.
        write_script(server_selector, ["quit"])
        for role, process in processes.items():
            try:
                if process.poll() is None:
                    try:
                        process.wait(timeout=15 if role == "server" else 2)
                    except subprocess.TimeoutExpired:
                        if role == "server" and server_pid.is_file():
                            # Verify ownership again for each signal, including
                            # the fallback if graceful termination cannot finish.
                            cleanup = (
                                "import os,pathlib,signal,sys,json; p=int(sys.argv[1]); "
                                "f=pathlib.Path('/proc')/str(p)/'cmdline'; "
                                "a=f.read_bytes().decode().split(chr(0)) if f.exists() else []; "
                                "owned=bool(a) and a[0]==sys.argv[2] and sys.argv[3] in a; "
                                "os.kill(p,int(sys.argv[4])) if owned else None; "
                                "print(json.dumps({'pid':p,'owned':owned,'signal':int(sys.argv[4])}))"
                            )
                            for signal_number in (15, 9):
                                result = subprocess.run([
                                    "wsl.exe", "--exec", "python3", "-c", cleanup,
                                    server_pid.read_text().strip(), wsl_path(paths["linux/engine"]),
                                    wsl_path(games["server"].parent), str(signal_number)
                                ], check=True, timeout=15, capture_output=True, text=True, cwd=output)
                                report.setdefault("forced_server_cleanup", []).append(json.loads(result.stdout))
                                try:
                                    process.wait(timeout=15)
                                    break
                                except subprocess.TimeoutExpired:
                                    if signal_number == 9:
                                        raise
                        else:
                            process.terminate()
                            process.wait(timeout=15)
                        report["failures"].append(role + " required forced termination")
            except (OSError, subprocess.SubprocessError, ValueError) as error:
                report["failures"].append(role + " cleanup failed: " + str(error))
            report["exit_codes"][role] = process.poll()
        for stream in streams:
            stream.close()
    server_log = read_log(logs["server"])
    report["pure_list_sends"] = server_log.count("sending pure pak list")
    if report["pure_list_sends"] < 2:
        report["failures"].append("server did not send pure lists to both connections")
    for name in ("CrossPeer1", "CrossPeer2"):
        if name not in server_log:
            report["failures"].append("server did not report " + name)
    for role in ("client1", "client2"):
        text = read_log(logs[role])
        if len(re.findall(r"OPENQ4_STOCK_BASELINE_MP_CLIENT_VIEW gui=0", text)) != 2:
            report["failures"].append(role + " did not preserve gameplay view")
    report["elapsed_seconds"] = round(time.monotonic() - started, 2)
    report["status"] = "fail" if report["failures"] else "pass"
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("status", "family", "exit_codes", "failures")}, indent=2))
    return int(bool(report["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
