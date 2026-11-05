#!/usr/bin/env python3
"""Qualify the refusal of retail Quake 4 1.4.x (protocol 2.85) peers on loopback.

Two scenarios, each on IPv4 loopback with hidden windows and no input:

* retail server: the installed `Quake4Ded.exe` hosts q4dm1 under `fs_game q4mp`,
  bound to 127.0.0.1 with its writable files redirected into the output
  directory. An openQ4 client LAN-scans (the retail server must be ignored for
  its protocol, not listed) and then connects to it directly. The client must
  refuse the server for its protocol, and must not follow it into q4mp,
  restart its engine or send a connect request;
* retail client: an openQ4 dedicated server receives the challenge request a
  1.4.2 `Quake4.exe` sends (its layout read from that executable: the client id
  and one byte), and must answer with the protocol print that client shows.
  An openQ4 challenge on the same server must still be answered normally.

By default the retail client itself is not run, because it opens a visible
game window; its packet is reproduced byte for byte instead.
`--retail-client-binary` additionally starts the installed `Quake4.exe`
windowed, bound to loopback with its writable files redirected into the output
directory, connects it to the openQ4 server and requires the protocol refusal
in its own console log. The harness stops it afterwards; no input is sent.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import secrets
import socket
import struct
import subprocess
import time
from pathlib import Path

from mp_lan_reconnect_smoke import LAN_PORTS, busy_udp_ports, digest
from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""
OPENQ4_PROTOCOL = (2, 41)
RETAIL_PROTOCOL = (2, 85)


def wstr(text: str) -> bytes:
    return text.encode("latin-1") + b"\0"


def rstr(data: bytes, offset: int) -> tuple[str, int]:
    end = data.index(b"\0", offset)
    return data[offset:end].decode("latin-1"), end + 1


class Probe:
    """A loopback UDP peer that sends connectionless packets and keeps the replies it wants."""

    def __init__(self, port: int):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.destination = ("127.0.0.1", port)

    def drain(self) -> None:
        self.socket.settimeout(0.05)
        try:
            while True:
                self.socket.recvfrom(65536)
        except (socket.timeout, ConnectionResetError):
            pass

    def ask(self, payload: bytes, want: str, tries: int, alive=lambda: True) -> bytes | None:
        self.drain()
        for _ in range(tries):
            if not alive():
                return None
            self.socket.sendto(payload, self.destination)
            deadline = time.monotonic() + 1.0
            self.socket.settimeout(1.0)
            while time.monotonic() < deadline:
                try:
                    data, _ = self.socket.recvfrom(65536)
                except (socket.timeout, ConnectionResetError):
                    time.sleep(0.25)
                    break
                if data[:2] == b"\xff\xff" and data[2:].startswith(want.encode() + b"\0"):
                    return data
        return None

    def close(self) -> None:
        self.socket.close()


def parse_print(data: bytes) -> tuple[int, str]:
    _, offset = rstr(data, 2)
    opcode, = struct.unpack_from("<i", data, offset)
    text, _ = rstr(data, offset + 4)
    return opcode, text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True, help="the retail Quake 4 1.4.2 installation")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--client-port", type=int, default=28992)
    parser.add_argument("--startup-timeout", type=int, default=420)
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--retail-client-binary", action="store_true",
                        help="also run the real Quake4.exe (a visible window) against the openQ4 server")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("the retail binaries are Windows executables")
    runtime, output, retail = args.runtime_dir.resolve(), args.output_dir.resolve(), args.basepath.resolve()
    verify_runtime_is_current(runtime)
    paths = {
        "engine": runtime / f"openQ4-client_x64{SUFFIX}", "dedicated": runtime / f"openQ4-ded_x64{SUFFIX}",
        "game_mp": runtime / "baseoq4/game-mp_x64.dll", "pak0": runtime / "baseoq4/pak0.pk4",
        "pak1": runtime / "baseoq4/pak1.pk4",
    }
    retail_paths = {"Quake4Ded.exe": retail / "Quake4Ded.exe", "Quake4.exe": retail / "Quake4.exe",
                    **{f"q4mp/{p.name}": p for p in sorted((retail / "q4mp").glob("game*.pk4"))}}
    if any(not path.is_file() for path in [*paths.values(), *retail_paths.values()]):
        parser.error("a complete staged runtime and a retail 1.4.2 installation with q4mp are required")
    busy = busy_udp_ports()
    free = [port for port in LAN_PORTS if port not in busy]
    if len(free) < 2 or args.client_port in busy:
        parser.error("two free LAN discovery ports and a free client port are required")
    retail_port, openq4_port = free[0], free[1]
    output.mkdir(parents=True, exist_ok=False)
    report = {
        "status": "fail", "transport": "IPv4 loopback",
        "source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"],
                                                     cwd=ROOT, text=True)),
        "runtime_dir": str(runtime), "runtime_sha256": {key: digest(path) for key, path in paths.items()},
        "retail_sha256": {key: digest(path) for key, path in retail_paths.items()},
        "retail_server": {"port": retail_port}, "retail_client": {"port": openq4_port},
        "commands": {}, "failures": [],
    }
    processes, streams = {}, []

    def launch(role: str, command: list[str], cwd: Path, hidden_console: bool = False) -> None:
        report["commands"][role] = command
        stream = (output / f"{role}-console.log").open("w", encoding="utf-8")
        streams.append(stream)
        startup = None
        if hidden_console:
            # Quake4Ded opens its own console window; start it hidden.
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
        processes[role] = subprocess.Popen(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                                           startupinfo=startup,
                                           creationflags=0 if hidden_console else subprocess.CREATE_NO_WINDOW)

    def alive(role: str):
        return lambda: processes[role].poll() is None

    def stop(role: str, graceful) -> None:
        process = processes.get(role)
        if process is None or process.poll() is not None:
            return
        try:
            graceful()
            process.wait(timeout=30)
        except (OSError, subprocess.TimeoutExpired):
            process.kill()
            process.wait()
            report["failures"].append(f"{role} required forced termination")

    started = time.monotonic()
    rcon = secrets.token_hex(8)
    retail_save = output / "retail-server"
    client_game = output / "client" / "baseoq4"
    client_log = client_game / "logs/openq4.log"
    server_game = output / "openq4-server" / "baseoq4"
    server_log = server_game / "logs/openq4.log"
    probe = None
    try:
        # Scenario 1: the real retail dedicated server.
        (retail_save / "q4mp").mkdir(parents=True)
        # Lower-case paths: the retail filesystem warns about every upper-case
        # character in a search path, thousands of times per start.
        launch("retail_server", [
            str(retail_paths["Quake4Ded.exe"]), "+set", "fs_basepath", str(retail).lower(),
            "+set", "fs_savepath", str(retail_save).lower(), "+set", "fs_game", "q4mp",
            "+set", "net_ip", "127.0.0.1", "+set", "net_port", str(retail_port), "+set", "net_LANServer", "1",
            "+set", "si_gameType", "DM", "+set", "si_pure", "1", "+set", "si_maxPlayers", "4",
            "+set", "si_name", "MP08 retail " + rcon[:6], "+set", "logFile", "2", "+set", "developer", "1",
            "+set", "net_serverPunkbusterEnabled", "0", "+set", "net_serverRemoteConsolePassword", rcon,
            "+spawnServer", "mp/q4dm1"], retail, hidden_console=True)
        probe = Probe(retail_port)
        info = probe.ask(b"\xff\xff" + wstr("getInfo") + struct.pack("<i", 0x5151), "infoResponse",
                         args.startup_timeout, alive("retail_server"))
        if info is None:
            raise RuntimeError("the retail server never answered getInfo")
        _, offset = rstr(info, 2)
        _, protocol = struct.unpack_from("<ii", info, offset)
        version = re.search(rb"si_version\0([^\0]*)", info)
        game = re.search(rb"fs_game\0([^\0]*)", info)
        report["retail_server"].update(protocol=[protocol >> 16, protocol & 0xFFFF],
                                       si_version=version[1].decode() if version else None,
                                       fs_game=game[1].decode() if game else None)
        if (protocol >> 16, protocol & 0xFFFF) != RETAIL_PROTOCOL:
            raise RuntimeError(f"the retail server reports protocol {protocol >> 16}.{protocol & 0xFFFF}")

        client_game.mkdir(parents=True)
        endpoint = f"127.0.0.1:{retail_port}"
        # One finite script: a refused client loads no map, so no hook resumes
        # a command pump, and an endless pump would starve the appended
        # disconnect or module reload that the connection itself may queue.
        write_script(client_game / "retail_client.cfg", [
            "LANScan", "waitMsec 3000", "listServers", "echo RETAIL_SCAN_DONE",
            f'connect "{endpoint}"', "echo RETAIL_CONNECT_ISSUED"])
        cvars = {
            "fs_basepath": str(retail), "fs_savepath": str(client_game.parent), "fs_devpath": str(client_game.parent),
            "fs_game": "baseoq4", "com_gameMode": "MP", "si_gameType": "DM", "logFile": "2",
            "logFileName": "logs/openq4.log", "developer": "1", "s_noSound": "1", "in_mouse": "0",
            "in_joystick": "0", "win_allowMultipleInstances": "1", "net_enableIPv4": "1", "net_enableIPv6": "0",
            "net_ip": "127.0.0.1", "net_port": str(args.client_port), "net_LANServer": "1",
            "net_clientDownload": "0", "ui_name": "RetailProbe", "r_renderApi": "gl", "r_fullscreen": "0",
            "r_fullscreenDesktop": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
            "r_hiddenWindow": "1", "r_mode": "-1", "r_customWidth": "640", "r_customHeight": "360",
            "r_windowWidth": "640", "r_windowHeight": "360", "com_maxfps": "60", "r_swapInterval": "0",
        }
        launch("client", [str(paths["engine"]), *[part for key, value in cvars.items() for part in ("+set", key, value)],
                          "+exec", "retail_client.cfg"], runtime)
        refusal = re.compile(r"server (?:127\.0\.0\.1|localhost):%d uses protocol 2\.85, expected 2\.41 - connection refused" % retail_port)
        mod_refusal = re.compile(r"The server requires mod 'q4mp'[^\n]*")
        deadline = time.monotonic() + args.startup_timeout
        outcome_at = None
        while time.monotonic() < deadline:
            text = read_log(client_log)
            if processes["client"].poll() is not None:
                raise RuntimeError("the openQ4 client exited during the retail connection")
            if "RETAIL_CONNECT_ISSUED" in text and (refusal.search(text) or mod_refusal.search(text) or "Restarting.." in text):
                outcome_at = outcome_at or time.monotonic()
                # Watch a little longer for retries after the outcome.
                if time.monotonic() - outcome_at > 6:
                    break
            time.sleep(0.25)
        text = read_log(client_log)
        scan = text.partition("RETAIL_SCAN_DONE")[0]
        after = text.partition("RETAIL_CONNECT_ISSUED")[2]
        ignored = re.search(r"server (?:127\.0\.0\.1|localhost):%d ignored - protocol 2\.85, expected 2\.41" % retail_port, scan)
        listed = re.search(r"^\s*\d+: .* \((?:127\.0\.0\.1|localhost):%d\)\s*$" % retail_port, scan, re.MULTILINE)
        refused = refusal.search(after)
        tail = after[refused.end():] if refused else ""
        # The dialog the player sees (Session's MessageBox log line).
        dialog = re.search(r"MessageBox: [^\n]*uses network protocol 2\.85[^\n]*", tail)
        report["retail_server"]["dialog"] = dialog[0] if dialog else None
        if refused and not dialog:
            report["failures"].append("the protocol refusal showed the player no dialog")
        report["retail_server"].update(
            scan_ignored_for_protocol=bool(ignored), scan_listed=bool(listed),
            protocol_refusal=refused[0] if refused else None,
            mod_refusal=(mod_refusal.search(after) or [None])[0],
            followed_game_directory="Restarting.." in after or "reloadEngine" in after,
            connect_requests=after.count("sending authenticated connect request"),
            challenges_after_refusal=tail.count("sending challenge to"),
            challenges=after.count("sending challenge to"))
        outcome = report["retail_server"]
        if not ignored or listed:
            report["failures"].append("the LAN scan did not ignore the retail server for its protocol")
        if not refused:
            reason = outcome["mod_refusal"] or "no refusal"
            report["failures"].append(f"the client did not refuse the retail server for its protocol ({reason})")
        if outcome["followed_game_directory"] or outcome["connect_requests"]:
            report["failures"].append("the client followed the retail server or sent it a connect request")
        if refused and outcome["challenges_after_refusal"]:
            report["failures"].append("the client kept challenging after refusing the server")
        report["retail_server"]["client_stopped_by_harness"] = processes["client"].poll() is None
        stop("client", processes["client"].kill)
        probe.socket.sendto(b"\xff\xff" + wstr("rcon") + wstr(rcon) + wstr("quit"), probe.destination)
        processes["retail_server"].wait(timeout=30)
        report["retail_server"]["exit_code"] = processes["retail_server"].returncode
        probe.close()
        probe = None

        # Scenario 2: a 1.4.2 client's challenge against an openQ4 server.
        server_game.mkdir(parents=True)
        commands = server_game / "retail_server_cmd.cfg"
        write_script(commands, ["// idle"])
        write_script(server_game / "retail_server_pump.cfg", ["exec retail_server_cmd.cfg", "waitMsec 500", "exec retail_server_pump.cfg"])
        write_script(server_game / "retail_server_start.cfg", ["echo RETAIL_OPENQ4_SERVER_READY", "exec retail_server_pump.cfg"])
        server = {
            "fs_basepath": str(retail), "fs_savepath": str(server_game.parent), "fs_devpath": str(server_game.parent),
            "fs_game": "baseoq4", "com_gameMode": "MP", "si_gameType": "DM", "logFile": "2",
            "logFileName": "logs/openq4.log", "developer": "1", "win_allowMultipleInstances": "1",
            "net_enableIPv4": "1", "net_enableIPv6": "0", "net_ip": "127.0.0.1", "net_port": str(openq4_port),
            "net_serverDedicated": "1", "net_LANServer": "1", "si_pure": "1", "si_maxPlayers": "4",
            "bot_minPlayers": "0", "g_autoExecAfterMapLoad": "retail_server_start.cfg",
            "g_autoExecAfterMapLoadDelayMs": "500",
        }
        launch("openq4_server", [str(paths["dedicated"]), *[part for key, value in server.items() for part in ("+set", key, value)],
                                 "+spawnServer", "mp/q4dm1"], runtime)
        deadline = time.monotonic() + args.startup_timeout
        while "RETAIL_OPENQ4_SERVER_READY" not in read_log(server_log):
            if processes["openq4_server"].poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("the openQ4 server did not start")
            time.sleep(0.5)
        probe = Probe(openq4_port)
        client_id = 0x1234
        # Quake4.exe 1.4.2 idAsyncClient::SetupConnection: WriteShort(-1),
        # WriteString("challenge"), WriteLong(clientId), WriteByte(net_serverDedicated == 1).
        retail_challenge = b"\xff\xff" + wstr("challenge") + struct.pack("<iB", client_id, 0)
        answer = probe.ask(retail_challenge, "print", 5, alive("openq4_server"))
        if answer is None:
            report["retail_client"]["retail_challenge"] = "no answer"
            report["failures"].append("the openQ4 server did not answer a 1.4.2 client's challenge")
        else:
            opcode, text = parse_print(answer)
            report["retail_client"]["retail_challenge"] = {"opcode": opcode, "text": text}
            if opcode != 1 or text.strip() != "server uses protocol %d.%d" % OPENQ4_PROTOCOL:
                report["failures"].append(f"unexpected answer to a 1.4.2 challenge: {opcode} {text!r}")
        normal = probe.ask(b"\xff\xff" + wstr("challenge") + struct.pack("<i", client_id + 1), "challengeResponse", 5,
                           alive("openq4_server"))
        report["retail_client"]["openq4_challenge_answered"] = normal is not None
        if normal is None:
            report["failures"].append("the openQ4 server stopped answering openQ4 challenges")
        info = probe.ask(b"\xff\xff" + wstr("getInfo") + struct.pack("<i", 7), "infoResponse", 5, alive("openq4_server"))
        if info is not None:
            _, offset = rstr(info, 2)
            _, protocol = struct.unpack_from("<ii", info, offset)
            report["retail_client"]["info_protocol"] = [protocol >> 16, protocol & 0xFFFF]
        binary_started = False
        if args.retail_client_binary:
            # The real 1.4.2 client: windowed, loopback-bound, its files kept in
            # the output directory, and stopped by the harness afterwards.
            client_home = output / "retail-client"
            (client_home / "q4base").mkdir(parents=True)
            binary = report["retail_client"]["binary"] = {"refusal_logged": False}
            try:
                launch("retail_client_binary", [
                    str(retail_paths["Quake4.exe"]), "+set", "fs_basepath", str(retail).lower(),
                    "+set", "fs_savepath", str(client_home).lower(), "+set", "r_fullscreen", "0",
                    "+set", "r_mode", "3", "+set", "s_noSound", "1", "+set", "logFile", "2",
                    "+set", "net_ip", "127.0.0.1", "+set", "net_port", str(args.client_port),
                    "+set", "com_allowConsole", "1", "+connect", f"127.0.0.1:{openq4_port}"], retail)
                binary_started = True
            except OSError as error:
                # The Steam 1.4.2 Quake4.exe demands elevation (WinError 740).
                # The harness never elevates; run it from an elevated prompt.
                binary["launch_error"] = str(error)
                report["failures"].append("the real Quake4.exe could not start: " + (
                    "it requires elevation; run this check from an elevated prompt"
                    if getattr(error, "winerror", None) == 740 else str(error)))
        if binary_started:
            refusal = "server uses protocol %d.%d" % OPENQ4_PROTOCOL
            deadline = time.monotonic() + args.timeout
            retail_client_log = None
            while time.monotonic() < deadline and processes["retail_client_binary"].poll() is None:
                retail_client_log = next(iter(sorted(client_home.glob("*/qconsole.log"))), None)
                if retail_client_log is not None and refusal in read_log(retail_client_log):
                    binary["refusal_logged"] = True
                    break
                time.sleep(0.5)
            binary["exited_by_itself"] = processes["retail_client_binary"].poll() is not None
            stop("retail_client_binary", processes["retail_client_binary"].kill)
            if retail_client_log is not None:
                text = read_log(retail_client_log)
                binary["log"] = str(retail_client_log)
                binary["version"] = next(iter(re.findall(r"Quake4 [^\n]*1\.4\.2[^\n]*", text)), None)
                binary["protocol_lines"] = [line for line in text.splitlines() if "protocol" in line][:8]
            if not binary["refusal_logged"]:
                report["failures"].append("the real Quake4.exe did not log the openQ4 protocol refusal")
        write_script(commands, ["quit"])
        processes["openq4_server"].wait(timeout=30)
        report["retail_client"]["server_exit_code"] = processes["openq4_server"].returncode
        if processes["openq4_server"].returncode:
            report["failures"].append("the openQ4 server exited unsuccessfully")
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        report["failures"].append(str(error))
    finally:
        if probe is not None:
            probe.close()
        if "openq4_server" in processes:
            write_script(server_game / "retail_server_cmd.cfg", ["quit"])
        for role, process in processes.items():
            if process.poll() is None:
                if role == "retail_server":
                    try:
                        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as quit_socket:
                            quit_socket.sendto(b"\xff\xff" + wstr("rcon") + wstr(rcon) + wstr("quit"),
                                               ("127.0.0.1", retail_port))
                    except OSError:
                        pass
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    if role not in ("client", "retail_client_binary"):
                        report["failures"].append(f"{role} required forced termination")
        for stream in streams:
            stream.close()
    retail_log = next(iter(sorted(retail_save.glob("*/qconsole.log"))), None)
    if retail_log is not None:
        text = read_log(retail_log)
        report["retail_server"]["log"] = str(retail_log)
        report["retail_server"]["bound"] = bool(re.search(r"Opening IP socket: 127\.0\.0\.1:%d" % retail_port, text))
        if not report["retail_server"]["bound"]:
            report["failures"].append("the retail server did not bind loopback")
    report["elapsed_seconds"] = round(time.monotonic() - started, 2)
    report["status"] = "fail" if report["failures"] else "pass"
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("status", "retail_server", "retail_client", "failures")}, indent=2))
    return int(bool(report["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
