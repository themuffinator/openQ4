#!/usr/bin/env python3
"""Hold a full match over a simulated small path MTU (MP-08, checklist row 8.1).

One pure dedicated server fills `--map` with bots up to `--players`, and one
client joins it over IPv4 or IPv6 loopback and stays an active player for
`--seconds`. The client's port carries the simulated path (`net_forceMTU`), so
both directions of its link see it, as when one machine's interface MTU is
lowered:

* `--path drop` loses every datagram larger than the path whole, as a path that
  filters IP fragments or black-holes Packet Too Big does. Passing proves openQ4
  never relies on IP fragmentation at that MTU.
* `--path fragment` carries such a datagram in IP fragments, each lost on its
  own, as an ordinary host and path do. Passing proves the match survives the
  fragmentation the operating system would add.
* `--path none` sets no path, as a control for the same match.

A pass needs the client active at every sample, no reliable-message overflow or
dropped client on either side, no disconnect, and clean exits. The report keeps
the delay lines' counts: the largest datagram each way and how many exceeded the
path. This is a one-machine simulation; it does not replace the physical-MTU
run with two machines in docs/dev/mp-human-qualification.md.
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

from mp_lan_reconnect_smoke import busy_udp_ports
from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""
IMPAIR = re.compile(r"NET_IMPAIR port=(\d+) dir=(\w+) offered=(\d+) dropped=(\d+) duplicated=(\d+) released=(\d+) "
                    r"reordered=(\d+) overflowed=(\d+) discarded=(\d+) queued=\d+ maxQueued=\d+ bytes=(\d+) bursts=\d+ "
                    r"congested=(\d+) maxWait=\d+ oversized=(\d+) ipFragments=(\d+) maxSize=(\d+)")
IMPAIR_FIELDS = ("offered", "dropped", "duplicated", "released", "reordered", "overflowed", "discarded", "bytes",
                 "congested", "oversized", "ipFragments", "maxSize")
# A failure on either end: the channel refusing a reliable message, the server
# dropping the client for any reason, or the client leaving the server.
FAILURES = {
    "reliable_overflow": re.compile(r"reliable message[^\n]*overflow|SendReliableMessage: overflowed", re.I),
    "disconnect": re.compile(r"Disconnected from server|Server has shut down|Connection timed out|timed out", re.I),
    "snapshot_overflow": re.compile(r"snapshot[^\n]*overflow", re.I),
}


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def local_active(state: str) -> bool:
    """Whether the report's local player is in the match; remote players all report bot=0."""
    local = re.search(r"MP_LOCAL client=(\d+)", state)
    return bool(local) and re.search(rf"MP_PLAYER slot={local[1]} [^\n]*spectating=0 wantSpectate=0 ingame=1",
                                     state) is not None


def human_drops(server_log: str) -> list[str]:
    """Server drop lines for clients that are not bots (the bot manager kicks bots to balance)."""
    bots = set(re.findall(r"addbot: '[^']*' joined as client (\d+)", server_log))
    return [line for slot, line in re.findall(r"^client (\d+) ((?!connected\.)[^:\n].*)$", server_log, re.M)
            if slot not in bots]


def impairment(text: str) -> dict[str, dict[str, int]]:
    """The last report of each direction."""
    found = {}
    for match in IMPAIR.finditer(text):
        found[match[2]] = dict(zip(IMPAIR_FIELDS, map(int, match.groups()[2:])))
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--family", choices=("ipv4", "ipv6"), required=True)
    parser.add_argument("--path", choices=("drop", "fragment", "none"), required=True)
    parser.add_argument("--mtu", type=int, default=1280)
    parser.add_argument("--map", default="mp/q4ctf1")
    parser.add_argument("--gametype", default="CTF")
    parser.add_argument("--players", type=int, default=12, help="the human plus bots")
    parser.add_argument("--seconds", type=int, default=300, help="time the client holds the match")
    parser.add_argument("--sample-seconds", type=int, default=30)
    parser.add_argument("--loss", type=int, default=0, help="also lose this percentage of IP packets, seeded")
    parser.add_argument("--port", type=int, default=28171)
    parser.add_argument("--client-port", type=int, default=28993)
    parser.add_argument("--timeout", type=int, default=180, help="maximum seconds for any one step")
    parser.add_argument("--startup-timeout", type=int, default=300)
    args = parser.parse_args()
    if not 576 <= args.mtu <= 65535 or args.players < 2 or args.seconds < 1:
        parser.error("the MTU is 576-65535 bytes, at least two players, a positive duration")
    address = "127.0.0.1" if args.family == "ipv4" else "::1"
    server_endpoint = f"127.0.0.1:{args.port}" if args.family == "ipv4" else f"[::1]:{args.port}"
    runtime, output, assets = args.runtime_dir.resolve(), args.output_dir.resolve(), args.basepath.resolve()
    verify_runtime_is_current(runtime)
    paths = {
        "engine": runtime / f"openQ4-client_x64{SUFFIX}",
        "dedicated": runtime / f"openQ4-ded_x64{SUFFIX}",
        "game_mp": runtime / "baseoq4" / ("game-mp_x64" + (".dll" if os.name == "nt" else ".so")),
        "pak0": runtime / "baseoq4/pak0.pk4", "pak1": runtime / "baseoq4/pak1.pk4",
    }
    if any(not path.is_file() for path in paths.values()) or not (assets / "q4base").is_dir():
        parser.error("a complete staged runtime and the retail PK4 assets are required")
    busy = busy_udp_ports()
    if args.port in busy or args.client_port in busy:
        parser.error("the server or client port is in use")
    output.mkdir(parents=True, exist_ok=False)
    path_cvars = {} if args.path == "none" else {
        "net_forceMTU": str(args.mtu), "net_forceMTUDrop": "1" if args.path == "drop" else "0", "net_forceSeed": "80801"}
    if args.loss:
        path_cvars["net_forceDrop"] = str(args.loss)
    report = {
        "status": "fail", "family": args.family, "path": args.path, "mtu": args.mtu, "map": args.map,
        "gametype": args.gametype, "players": args.players, "seconds": args.seconds, "client_impairment": path_cvars,
        "source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"],
                                                     cwd=ROOT, text=True)),
        "runtime_dir": str(runtime), "runtime_sha256": {key: digest(path) for key, path in paths.items()},
        "samples": [], "commands": {}, "exit_codes": {}, "failures": [],
    }
    games = {role: output / role / "baseoq4" for role in ("server", "client")}
    logs = {role: game / "logs/openq4.log" for role, game in games.items()}
    for game in games.values():
        game.mkdir(parents=True)
    family = {"net_enableIPv4": "1", "net_enableIPv6": "0", "net_ip": address} if args.family == "ipv4" else \
             {"net_enableIPv4": "0", "net_enableIPv6": "1", "net_ip6": address}
    common = {
        "fs_basepath": str(assets), "fs_game": "baseoq4", "com_gameMode": "MP", "si_gameType": args.gametype,
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1", "s_noSound": "1",
        "in_mouse": "0", "in_joystick": "0", "com_skipLoadingContinue": "1", "net_allowCheats": "1",
        "net_LANServer": "1", "net_clientDownload": "0",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1", **family,
    }
    processes, streams = {}, []
    sequence = 0
    loops = {"client": games["client"] / "mtu_c0.cfg"}
    write_script(loops["client"], ["waitMsec 250", "exec mtu_c0.cfg"])
    server_commands = games["server"] / "mtu_server_cmd.cfg"
    write_script(server_commands, ["// idle"])
    write_script(games["server"] / "mtu_server_pump.cfg", ["exec mtu_server_cmd.cfg", "waitMsec 500", "exec mtu_server_pump.cfg"])
    write_script(games["server"] / "mtu_server_start.cfg", ["echo MTU_SERVER_READY", "exec mtu_server_pump.cfg"])

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

    def check_failures(texts: dict[str, str]) -> list[str]:
        found = []
        for role, text in texts.items():
            for label, pattern in FAILURES.items():
                if pattern.search(text):
                    found.append(f"{role}: {label}: {pattern.search(text)[0]!r}")
        found += [f"server dropped the client: {line!r}" for line in human_drops(texts["server"])]
        return found

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
        nonlocal sequence
        sequence += 1
        marker = f"MTU_STEP_DONE_{sequence}"
        following = games["client"] / f"mtu_c{sequence}.cfg"
        write_script(following, ["waitMsec 250", f"exec {following.name}"])
        if finite:
            # A pure connect may reload the engine and appends `reconnect`; an
            # endless loop at the head of the buffer would starve it, so the
            # one-shot map-load hook resumes the loop.
            write_script(games["client"] / "mtu_loaded.cfg", ["echo MTU_MAP_LOADED", f"exec {following.name}"])
            write_script(loops["client"], ["set g_autoExecAfterMapLoad mtu_loaded.cfg", *actions, "echo " + marker])
        else:
            write_script(loops["client"], [*actions, "echo " + marker, f"exec {following.name}"])
        loops["client"] = following
        return marker

    def step(label: str, actions: list[str]) -> str:
        begin = f"MTU_STEP_BEGIN_{sequence + 1}"
        marker = inject(["echo " + begin, *actions])
        text = wait(label, lambda t: "\n" + marker in t["client"])["client"]
        return text.rpartition("\n" + marker)[0].rpartition("\n" + begin)[2]

    def server_step(label: str, actions: list[str]) -> str:
        marker = f"MTU_SERVER_DONE_{label}"
        write_script(server_commands, [*actions, "echo " + marker])
        text = wait("server " + label, lambda t: "\n" + marker in t["server"])["server"]
        write_script(server_commands, ["// idle"])
        return text.rpartition("\n" + marker)[0]

    started = time.monotonic()
    try:
        server = dict(common, fs_savepath=str(games["server"].parent), fs_devpath=str(games["server"].parent),
                      net_port=str(args.port), net_serverDedicated="1", si_pure="1", si_name="MP08 path MTU",
                      # The bot manager keeps one public slot open and removes a bot when it fills.
                      si_maxPlayers=str(args.players + 1), bot_minPlayers=str(args.players), si_warmup="0",
                      si_timeLimit="0", si_fragLimit="999", si_captureLimit="999", si_autoBalance="1",
                      g_autoExecAfterMapLoad="mtu_server_start.cfg", g_autoExecAfterMapLoadDelayMs="500")
        launch("server", server, ["+spawnServer", args.map])
        wait("server startup", lambda t: "\nMTU_SERVER_READY" in t["server"], args.startup_timeout)
        if f"Server spawned on port {args.port}." not in read_log(logs["server"]):
            raise RuntimeError(f"the server did not take port {args.port}")
        client = dict(common, **path_cvars, fs_savepath=str(games["client"].parent),
                      fs_devpath=str(games["client"].parent), net_port=str(args.client_port), ui_name="MtuPlayer",
                      ui_autoJoin="1", ui_spectate="Play", r_renderApi="gl", r_fullscreen="0",
                      r_fullscreenDesktop="0", r_borderless="0", r_borderlessDefaultMigrated="1", r_hiddenWindow="1",
                      r_mode="-1", r_customWidth="640", r_customHeight="360", r_windowWidth="640",
                      r_windowHeight="360", com_maxfps="60", r_swapInterval="0",
                      g_autoExecAfterMapLoad="mtu_loaded.cfg", g_autoExecAfterMapLoadDelayMs="500")
        launch("client", client, ["+exec", "mtu_c0.cfg"])
        wait("client startup", lambda t: "mtu_c0.cfg" in t["client"], args.startup_timeout)

        join_started = time.monotonic()
        inject([f'connect "{server_endpoint}"'], finite=True)
        wait("map load", lambda t: "\nMTU_MAP_LOADED" in t["client"])
        deadline = time.monotonic() + args.timeout
        while True:
            section = step("join state", ["openq4_reportMPState"]).rsplit("\nMP_STATE ", 1)[-1]
            if local_active(section):
                break
            if time.monotonic() >= deadline:
                raise RuntimeError("the client never became an active player")
            time.sleep(0.75)
        report["join_seconds"] = round(time.monotonic() - join_started, 2)
        print(f"joined after {report['join_seconds']} s; holding for {args.seconds} s", flush=True)

        hold_started = time.monotonic()
        while True:
            elapsed = time.monotonic() - hold_started
            remaining = args.seconds - elapsed
            if remaining > 0:
                time.sleep(min(args.sample_seconds, remaining))
            section = step("sample", ["openq4_reportMPState", "netImpairment"])
            state = section.rsplit("\nMP_STATE ", 1)[-1]
            players = len(re.findall(r"MP_PLAYER slot=\d+ ", state))
            sample = {"seconds": round(time.monotonic() - hold_started, 1), "active": local_active(state),
                      "players": players, "client": impairment(section)}
            report["samples"].append(sample)
            print(json.dumps(sample), flush=True)
            if not sample["active"]:
                raise RuntimeError(f"the client stopped being an active player after {sample['seconds']} s")
            failures = check_failures({role: read_log(path) for role, path in logs.items()})
            if failures:
                raise RuntimeError("; ".join(failures))
            if remaining <= 0:
                break
        report["server_impairment"] = impairment(server_step("final", ["netImpairment"]))
        report["client_impairment_final"] = report["samples"][-1]["client"]
        report["max_players_seen"] = max(sample["players"] for sample in report["samples"])
        if report["max_players_seen"] < args.players:
            report["failures"].append(f"the match never reached {args.players} players")
        inject(["quit"])
        report["exit_codes"]["client"] = processes["client"].wait(timeout=60)
        write_script(server_commands, ["quit"])
        report["exit_codes"]["server"] = processes["server"].wait(timeout=60)
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
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    report["failures"].append(role + " required forced termination")
            report["exit_codes"].setdefault(role, process.returncode)
        for stream in streams:
            stream.close()
    report["elapsed_seconds"] = round(time.monotonic() - started, 2)
    # Each sample checked both logs during the hold; the server logs the client's
    # final quit as a drop, so the logs are not checked again after it.
    if {key: digest(path) for key, path in paths.items()} != report["runtime_sha256"]:
        report["failures"].append("runtime changed during qualification")
    report["status"] = "fail" if report["failures"] else "pass"
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report.get(key) for key in ("status", "family", "path", "mtu", "join_seconds",
                                                       "max_players_seen", "client_impairment_final",
                                                       "server_impairment", "exit_codes", "failures")}, indent=2))
    return int(bool(report["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
