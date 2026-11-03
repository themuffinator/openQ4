#!/usr/bin/env python3
"""Qualify LAN discovery and successive reconnects to a pure dedicated server.

One dedicated server and one client share a single address family and bind
one local address: IPv4 loopback, IPv6 loopback, or this host's own scoped
link-local IPv6 address (which the operating system delivers internally). The
client finds the server with the console LAN scan, joins it through the scan
result, disconnects, joins it again by endpoint, and finally uses `reconnect`.
Every join must reach active gameplay on the pure server, and the server must
admit and release the same slot each time.

IPv6 loopback has no link to multicast on, so its LAN scan is expected to find
nothing; that run records the discovery result and joins by endpoint. Windows
only binds a scoped link-local address for a runtime the firewall already
knows, so run that family from a firewall-registered stable runtime path.
"""
from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import os
import re
import secrets
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""
LAN_PORTS = range(28004, 28012)  # PORT_SERVER and the MAX_SERVER_PORTS the scan probes
ACTIVE = re.compile(r"MP_PLAYER slot=\d+ bot=0 [^\n]*spectating=0 wantSpectate=0 ingame=1")


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def busy_udp_ports() -> set[int]:
    """Ports any local UDP socket holds, on any address, so a scan never reaches a stranger."""
    command = ["netstat", "-ano", "-p", "UDP"] if os.name == "nt" else ["ss", "-uan"]
    try:
        text = subprocess.run(command, capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return set()
    ports = set()
    for line in text.splitlines():
        fields = line.split()
        for field in fields[1:3]:
            found = re.search(r":(\d+)$", field)
            if found:
                ports.add(int(found[1]))
                break
    if os.name == "nt":
        ports |= busy_udp_ports_v6()
    return ports


def busy_udp_ports_v6() -> set[int]:
    try:
        text = subprocess.run(["netstat", "-ano", "-p", "UDPv6"], capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return set()
    return {int(m[1]) for m in re.finditer(r"\]:(\d+)\s", text)}


def endpoint(address: ipaddress._BaseAddress, port: int) -> str:
    return f"[{address}]:{port}" if address.version == 6 else f"{address}:{port}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--family", choices=("ipv4", "ipv6-loopback", "ipv6-scoped"), required=True)
    parser.add_argument("--address", help="local address to bind; required for ipv6-scoped (fe80::...%%N)")
    parser.add_argument("--client-port", type=int, default=28991)
    parser.add_argument("--timeout", type=int, default=120, help="maximum seconds for any one step")
    parser.add_argument("--startup-timeout", type=int, default=300)
    parser.add_argument("--impaired", action="store_true",
                        help="seeded 30 ms delay, 0-60 ms jitter, 3%% loss and 20%% duplication on each peer")
    args = parser.parse_args()
    default = {"ipv4": "127.0.0.1", "ipv6-loopback": "::1"}.get(args.family)
    address = ipaddress.ip_address(args.address or default or "::")
    if args.family == "ipv6-scoped" and not (address.version == 6 and address.is_link_local and address.scope_id):
        parser.error("ipv6-scoped needs this host's link-local address with its scope, e.g. fe80::1%5")
    if args.family == "ipv4" and (address.version != 4 or not address.is_loopback):
        parser.error("ipv4 binds a loopback address")
    if args.family == "ipv6-loopback" and address != ipaddress.ip_address("::1"):
        parser.error("ipv6-loopback binds ::1")
    runtime, output, assets = args.runtime_dir.resolve(), args.output_dir.resolve(), args.basepath.resolve()
    verify_runtime_is_current(runtime)
    paths = {
        "engine": runtime / f"openQ4-client_x64{SUFFIX}",
        "dedicated": runtime / f"openQ4-ded_x64{SUFFIX}",
        "game_mp": runtime / "baseoq4" / ("game-mp_x64" + (".dll" if os.name == "nt" else ".so")),
        "pak0": runtime / "baseoq4/pak0.pk4", "pak1": runtime / "baseoq4/pak1.pk4",
        "mod.json": runtime / "baseoq4/mod.json",
    }
    if any(not path.is_file() for path in paths.values()) or not (assets / "q4base").is_dir():
        parser.error("a complete staged runtime and the retail PK4 assets are required")
    busy = busy_udp_ports()
    free = [port for port in LAN_PORTS if port not in busy]
    if not free:
        parser.error("every LAN discovery port is in use")
    port = free[0]
    if args.client_port in busy or args.client_port in LAN_PORTS:
        parser.error("the client port is in use or inside the discovery range")
    output.mkdir(parents=True, exist_ok=False)
    nonce = secrets.token_hex(3)
    server_name = f"MP08 LAN {args.family} {nonce}"
    expected = endpoint(address, port)
    report = {
        "status": "fail", "family": args.family, "address": str(address), "server_endpoint": expected,
        "server_name": server_name,
        "source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"],
                                                     cwd=ROOT, text=True)),
        "runtime_dir": str(runtime), "runtime_sha256": {key: digest(path) for key, path in paths.items()},
        "assets_sha256": {path.name: digest(path) for path in sorted((assets / "q4base").glob("*.pk4"))},
        "discovery": {}, "joins": [], "commands": {}, "exit_codes": {}, "failures": [],
    }
    games = {role: output / role / "baseoq4" for role in ("server", "client")}
    logs = {role: game / "logs/openq4.log" for role, game in games.items()}
    for game in games.values():
        game.mkdir(parents=True)
    family = {"net_enableIPv4": "1", "net_enableIPv6": "0", "net_ip": str(address)} if address.version == 4 else \
             {"net_enableIPv4": "0", "net_enableIPv6": "1", "net_ip6": str(address)}
    common = {
        "fs_basepath": str(assets), "fs_game": "baseoq4", "com_gameMode": "MP", "si_gameType": "DM",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1", "s_noSound": "1",
        "in_mouse": "0", "in_joystick": "0", "com_skipLoadingContinue": "1", "net_allowCheats": "1",
        "net_LANServer": "1", "net_clientDownload": "0",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1", **family,
    }
    impairment = {"net_forceLatency": "30", "net_forceJitter": "60", "net_forceDrop": "3",
                  "net_forceDuplicate": "20", "net_forceSeed": "40708"} if args.impaired else {}
    report["impairment"] = impairment
    common.update(impairment)
    if args.impaired:
        common["net_channelShowPackets"] = "1"
    processes, streams = {}, []
    sequence = 0
    loops = {"client": games["client"] / "lan_c0.cfg"}
    write_script(loops["client"], ["waitMsec 250", "exec lan_c0.cfg"])
    server_commands = games["server"] / "lan_server_cmd.cfg"
    write_script(server_commands, ["// idle"])
    write_script(games["server"] / "lan_server_pump.cfg", ["exec lan_server_cmd.cfg", "waitMsec 500", "exec lan_server_pump.cfg"])
    write_script(games["server"] / "lan_server_start.cfg", ["echo LAN_SERVER_READY", "exec lan_server_pump.cfg"])

    def launch(role: str, cvars: dict[str, str], tail: list[str]) -> None:
        if len(cvars) + 1 > 64:
            raise RuntimeError("launch exceeds the engine's startup command capacity")
        executable = paths["dedicated"] if role == "server" else paths["engine"]
        command = [str(executable)] + [part for key, value in cvars.items() for part in ("+set", key, value)] + tail
        report["commands"][role] = command
        stream = (output / f"{role}-console.log").open("w", encoding="utf-8")
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

    def wait(label: str, predicate, seconds: int | None = None) -> dict[str, str]:
        until = time.monotonic() + (seconds or args.timeout)
        while time.monotonic() < until:
            texts = {role: read_log(path) for role, path in logs.items()}
            if re.search(r"FATAL ERROR|Unknown command", "\n".join(texts.values())):
                raise RuntimeError("engine error during " + label)
            ended = [role for role, process in processes.items() if process.poll() is not None]
            if ended:
                raise RuntimeError(f"{', '.join(ended)} ended during {label}")
            if predicate(texts):
                return texts
            time.sleep(0.25)
        raise RuntimeError("timeout during " + label)

    def inject(actions: list[str], finite: bool = False) -> str:
        """Queue client console actions; a finite step lets a connect's appended reload run."""
        nonlocal sequence
        sequence += 1
        marker = f"LAN_STEP_DONE_{sequence}"
        following = games["client"] / f"lan_c{sequence}.cfg"
        write_script(following, ["waitMsec 250", f"exec {following.name}"])
        if finite:
            # A connect from the menu may reload the game module or, on a pure
            # server, the engine, and appends `reconnect`; an endless loop at
            # the head of the buffer would starve it. The map-load hook resumes.
            # The hook is one-shot (it clears itself), so arm it for each join.
            write_script(games["client"] / "lan_loaded.cfg", ["echo LAN_MAP_LOADED", f"exec {following.name}"])
            write_script(loops["client"], ["set g_autoExecAfterMapLoad lan_loaded.cfg", *actions, "echo " + marker])
        else:
            write_script(loops["client"], [*actions, "echo " + marker, f"exec {following.name}"])
        loops["client"] = following
        return marker

    def step(label: str, actions: list[str]) -> str:
        begin = f"LAN_STEP_BEGIN_{sequence + 1}"
        marker = inject(["echo " + begin, *actions])
        text = wait(label, lambda t: "\n" + marker in t["client"])["client"]
        return text.rpartition("\n" + marker)[0].rpartition("\n" + begin)[2]

    def join(label: str, actions: list[str]) -> None:
        loads = read_log(logs["client"]).count("\nLAN_MAP_LOADED")
        connects = len(re.findall(r"client \d+ connected\.", read_log(logs["server"])))
        started = time.monotonic()
        inject(actions, finite=True)
        wait(label + ": map load", lambda t: t["client"].count("\nLAN_MAP_LOADED") > loads)
        deadline = time.monotonic() + args.timeout
        while True:
            section = step(label + ": state", ["openq4_reportMPState"]).rsplit("\nMP_STATE ", 1)[-1]
            if ACTIVE.search(section):
                break
            if time.monotonic() >= deadline:
                raise RuntimeError(label + ": the client never became an active player")
            time.sleep(0.75)
        server_log = read_log(logs["server"])
        slots = re.findall(r"client (\d+) connected\.", server_log)
        if len(slots) <= connects:
            raise RuntimeError(label + ": the server never admitted the client")
        report["joins"].append({"label": label, "slot": int(slots[-1]), "seconds": round(time.monotonic() - started, 2),
                                "server_connects": len(slots)})
        print(f"{label}: active in slot {slots[-1]}", flush=True)

    started = time.monotonic()
    try:
        server = dict(common, fs_savepath=str(games["server"].parent), fs_devpath=str(games["server"].parent),
                      net_port=str(port), net_serverDedicated="1", si_pure="1", si_name=server_name,
                      si_maxPlayers="4", bot_minPlayers="0", si_warmup="0", si_timeLimit="0", si_fragLimit="0",
                      g_autoExecAfterMapLoad="lan_server_start.cfg", g_autoExecAfterMapLoadDelayMs="500")
        launch("server", server, ["+spawnServer", "mp/q4dm1"])
        wait("server startup", lambda t: "\nLAN_SERVER_READY" in t["server"], args.startup_timeout)
        if f"Server spawned on port {port}." not in read_log(logs["server"]):
            raise RuntimeError(f"the server did not take port {port}")
        client = dict(common, fs_savepath=str(games["client"].parent), fs_devpath=str(games["client"].parent),
                      net_port=str(args.client_port), ui_name="LanPlayer", ui_autoJoin="1", ui_spectate="Play",
                      r_renderApi="gl", r_fullscreen="0", r_fullscreenDesktop="0", r_borderless="0",
                      r_borderlessDefaultMigrated="1", r_hiddenWindow="1", r_mode="-1", r_customWidth="640",
                      r_customHeight="360", r_windowWidth="640", r_windowHeight="360", com_maxfps="60",
                      r_swapInterval="0", g_autoExecAfterMapLoad="lan_loaded.cfg",
                      g_autoExecAfterMapLoadDelayMs="500")
        launch("client", client, ["+exec", "lan_c0.cfg"])
        wait("client startup", lambda t: "\nLAN_STEP" in t["client"] or "lan_c0.cfg" in t["client"], args.startup_timeout)

        # Discovery: the console scan sends the real broadcast (IPv4) or link
        # multicast (IPv6) sweep over the eight legacy ports and lists answers.
        index, listed = None, ""
        for attempt in range(3):
            listed = step(f"LAN scan {attempt + 1}", ["LANScan", "waitMsec 3000", "listServers"])
            rows = re.findall(r"^\s*(\d+): (.*) (-?\d+)ms \((.+)\)\s*$", listed, re.MULTILINE)
            mine = [row for row in rows if nonce in row[1]]
            report["discovery"] = {"attempts": attempt + 1, "rows": [list(row) for row in rows],
                                   "found": bool(mine), "listed_endpoint": mine[0][3] if mine else None}
            if mine:
                index = int(mine[0][0])
                break
        expected_discovery = args.family != "ipv6-loopback"
        report["discovery"]["expected"] = expected_discovery
        if expected_discovery and index is None:
            raise RuntimeError("the LAN scan never listed the server")
        # The engine names loopback endpoints "localhost:<port>".
        accepted = {expected} | ({f"localhost:{port}"} if address.is_loopback else set())
        if index is not None and report["discovery"]["listed_endpoint"] not in accepted:
            raise RuntimeError(f"the scan listed {report['discovery']['listed_endpoint']}, not {expected}")
        if not expected_discovery and index is not None:
            raise RuntimeError("IPv6 loopback discovery found the server; update the expectation")

        if index is not None:
            join("join from LAN scan", [f"connect {index}"])
        else:
            join("join by endpoint", [f'connect "{expected}"'])
        before = len(re.findall(r"^client \d+ (?!connected\.)[^:\n]", read_log(logs["server"]), re.MULTILINE))
        step("disconnect", ["disconnect", "waitMsec 1500"])
        wait("server releases the slot", lambda t: len(re.findall(
            r"^client \d+ (?!connected\.)[^:\n]", t["server"], re.MULTILINE)) > before, 30)
        join("connect again by endpoint", [f'connect "{expected}"'])
        join("reconnect command", ["reconnect"])
        slots = {entry["slot"] for entry in report["joins"]}
        if len(slots) != 1:
            report["failures"].append(f"successive joins used several slots: {sorted(slots)}")
        report["server_drops"] = re.findall(r"^client (\d+) (?!connected\.)([^:\n].*)$", read_log(logs["server"]), re.MULTILINE)
        report["pure_list_sends"] = read_log(logs["server"]).count("sending pure pak list")
        inject(["quit"])
        report["exit_codes"]["client"] = processes["client"].wait(timeout=30)
        write_script(server_commands, ["quit"])
        report["exit_codes"]["server"] = processes["server"].wait(timeout=30)
        if any(report["exit_codes"].values()):
            report["failures"].append(f"unclean exits: {report['exit_codes']}")
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        report["failures"].append(str(error))
    finally:
        write_script(server_commands, ["quit"])
        try:
            inject(["quit"])
        except OSError:
            pass
        for role, process in processes.items():
            if process.poll() is None:
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    report["failures"].append(role + " required forced termination")
            report["exit_codes"].setdefault(role, process.returncode)
        for stream in streams:
            stream.close()
    report["elapsed_seconds"] = round(time.monotonic() - started, 2)
    if args.impaired:
        report["received_fragments"] = {role: len(re.findall(r"recv .*fragment =", read_log(path)))
                                        for role, path in logs.items()}
        if not report["received_fragments"].get("client", 0):
            report["failures"].append("impaired run did not exercise received fragments")
    if {key: digest(path) for key, path in paths.items()} != report["runtime_sha256"]:
        report["failures"].append("runtime changed during qualification")
    report["status"] = "fail" if report["failures"] else "pass"
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("status", "family", "server_endpoint", "discovery", "joins",
                                                   "exit_codes", "failures")}, indent=2))
    return int(bool(report["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
