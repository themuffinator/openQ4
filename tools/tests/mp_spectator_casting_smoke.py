#!/usr/bin/env python3
"""Two windowed, input-free clients verify live spectating and casting.

A listen-server host plays a team match against three bots, which are left to
fight. A second client connects as a spectator and, through console commands
alone, follows by slot and by name, turns auto-follow on, changes the follow
view, takes the caster view and has it withdrawn. No input is synthesized.

Every checkpoint is read from both ends: the server's own record of the
spectator (`MP_SPECTATE`, `MP_CAMERA`) and what the remote client was sent and
draws (`MP_LOCAL`, `MP_VIEW`: the camera, and the team vitals in its accepted
match view). Both processes write the match event stream; each stream is held
to the fixture stream's schema, replayed through the sample overlay, and must
end on the server's final scores.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
BOTS = ("Cortez", "Rhodes", "Sledge")
ERRORS = r"unknown command|FATAL ERROR|ERROR:|Ignoring invalid .*reliable|could not import casual match rule"


def free_udp_port() -> int:
    """A loopback UDP port nothing holds now. Other sessions run game servers on
    this machine; a fixed port let one test's client join another test's server."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def fields(text: str, prefix: str) -> dict[str, str]:
    line = re.search(r"\n" + prefix + r" ([^\r\n]+)", text)
    return dict(re.findall(r'(\w+)=("[^"]*"|\S+)', line[1])) if line else {}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", help="runtime executable basename for an isolated test copy")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-spectator-casting-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--fight-seconds", type=int, default=30, help="how long the bots fight under killer auto-follow")
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    verify_runtime_is_current(runtime, getattr(args, "executable_name", ""))
    exe = runtime / (args.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"))
    if not exe.is_file() or not args.basepath.is_dir():
        parser.error("a staged runtime and installed Quake 4 assets are required")
    roles = ("host", "client")
    saves = {role: output / role for role in roles}
    games = {role: path / "baseoq4" for role, path in saves.items()}
    logs = {role: path / "logs/openq4.log" for role, path in games.items()}
    streams = {role: path / "match-events/live.ndjson" for role, path in games.items()}
    for role in roles:
        games[role].mkdir(parents=True, exist_ok=True)
        for stale in (logs[role], streams[role]):
            if stale.is_file():
                stale.unlink()

    # Each process idles in a config that re-executes itself. A step replaces
    # that file with its commands and a jump to a fresh idle file.
    pumps = {role: games[role] / "cast_pump_0.cfg" for role in roles}
    counters = {role: 0 for role in roles}
    for role in roles:
        write_script(pumps[role], ["waitMsec 200", f"exec {pumps[role].name}"])
    write_script(games["host"] / "cast_start.cfg", [
        *[f"addbot {name} 3 exact" for name in BOTS],
        "set g_casterAccess 0", "set g_matchEventStream 1", f"exec {pumps['host'].name}"])
    # The client joins like anyone else, then chooses to spectate.
    write_script(games["client"] / "cast_start.cfg", [
        "set g_spectatorFollowView 0", "set g_matchEventStream 1", "set ui_spectate Spectate", f"exec {pumps['client'].name}"])
    host_port, client_port = free_udp_port(), free_udp_port()

    common = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(args.basepath.resolve()), "fs_game": "baseoq4", "com_gameMode": "MP",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
        "r_borderlessDefaultMigrated": "1", "r_mode": "-1", "r_customWidth": "960",
        "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540",
        "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_renderApi": args.renderer, "ui_retained": "1", "com_maxfps": "60", "r_swapInterval": "0",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
        "ui_autoJoin": "1", "net_allowCheats": "1", "com_skipLoadingContinue": "1",
        "g_autoExecAfterMapLoad": "cast_start.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
    }

    def command(role: str) -> list[str]:
        cvars = dict(common, fs_savepath=str(saves[role]), fs_devpath=str(saves[role]))
        if role == "host":
            cvars.update(ui_name="Host", ui_spectate="Play", ui_team="Marine", net_port=str(host_port), net_ip="127.0.0.1",
                         net_enableIPv6="0", net_serverDedicated="0", net_LANServer="1", si_pure="0", si_maxPlayers="5",
                         si_gameType="Team DM", si_warmup="1", si_useReady="0", si_minPlayers="2", si_countDown="4",
                         si_timeLimit="0", si_fragLimit="999", si_autoBalance="1", bot_pause="0", bot_minPlayers="0",
                         g_matchProfile="casual")
        else:
            cvars.update(ui_name="Caster", ui_spectate="Play", ui_spectatorSpeed="200", net_port=str(client_port))
        result = [str(exe)]
        for key, value in cvars.items():
            result += ["+set", key, value]
        assert len(cvars) + 1 <= 64, "engine startup command capacity exceeded"
        result += ["+spawnServer", "mp/q4dm1"] if role == "host" else ["+connect", f"127.0.0.1:{host_port}"]
        (output / f"{role}-launch.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        return result

    processes: dict[str, subprocess.Popen] = {}
    consoles = []
    failures: list[str] = []
    started = time.time()
    deadline = started + args.timeout

    def need(condition: bool, message: str) -> bool:
        if not condition:
            failures.append(message)
        return condition

    def launch(role: str) -> None:
        console = (output / f"{role}-console.log").open("w", encoding="utf-8")
        consoles.append(console)
        processes[role] = subprocess.Popen(command(role), cwd=runtime, stdout=console, stderr=subprocess.STDOUT)

    class Stop(Exception):
        pass

    def wait_for(role: str, pattern: str, what: str, seconds: float = 40.0) -> str:
        limit = min(deadline, time.time() + seconds)
        while time.time() < limit:
            text = read_log(logs[role])
            if re.search(pattern, text):
                return text
            if any(process.poll() is not None for process in processes.values()):
                raise Stop(f"a process ended while waiting for {what}")
            time.sleep(0.2)
        raise Stop(f"timeout waiting for {what}")

    def step(role: str, name: str, commands: list[str], seconds: float = 40.0) -> str:
        """Runs commands in a process and returns its log text for that step."""
        marker = f"MP_{role.upper()}_{name.upper()}"
        counters[role] += 1
        idle = games[role] / f"cast_pump_{counters[role]}.cfg"
        write_script(idle, ["waitMsec 200", f"exec {idle.name}"])
        write_script(pumps[role], [f"echo {marker}_BEGIN", *commands, f"echo {marker}", f"exec {idle.name}"])
        pumps[role] = idle
        text = wait_for(role, r"\n" + marker + r"\s", f"{role} step {name}", seconds)
        return text[text.rindex("\n" + marker + "_BEGIN"):]

    def both(name: str, client_commands: list[str], shot: bool = False) -> tuple[dict, dict, dict, str]:
        """A client step, then the server's record of it."""
        client = step("client", name, [*client_commands, "openq4_reportMPState",
                                       *([f'screenshot "screenshots/{name}.tga"'] if shot else [])])
        host = step("host", name, ["openq4_reportMPState"])
        return fields(host, f"MP_SPECTATE slot={caster}"), fields(client, "MP_VIEW"), fields(client, "MP_LOCAL"), host

    caster = -1
    host_quit_seconds = None
    shots = [games["client"] / f"screenshots/{name}.tga" for name in ("caster", "chase", "third", "first")]
    final_host = ""
    try:
        launch("host")
        wait_for("host", r"MP match phase: 0 -> 1", "the host's map", 240)
        launch("client")
        # Start-up and the map load take as long as the machine allows; the
        # client's own start script is the signal that it is in the match.
        wait_for("client", r"execing cast_pump_0\.cfg", "the client's map", 240)
        # The spectator is in and the host's presence took the match live.
        for attempt in range(60):
            host = step("host", f"poll{attempt}", ["openq4_reportMPState"])
            watcher = re.search(r"MP_PLAYER slot=(\d+) bot=0 team=-?\d+ health=-?\d+ spectating=1 wantSpectate=1 ingame=1", host)
            if watcher and fields(host, "MP_STATE").get("phase") == "3" and len(re.findall(r"MP_PLAYER slot=\d+ bot=1 ", host)) == 3:
                caster = int(watcher[1])
                break
            time.sleep(1.0)
        else:
            raise Stop("the spectating client never appeared in a live match")
        players = {int(slot): int(team) for slot, team in
                   re.findall(r"MP_PLAYER slot=(\d+) bot=\d team=(-?\d+) health=-?\d+ spectating=0 ", host)}
        need(len(players) == 4 and caster not in players, f"expected the host and three bots playing: {players}")
        by_team = {team: sorted(slot for slot, value in players.items() if value == team and slot != 0) for team in (0, 1)}
        need(all(by_team.values()), f"bots are not on both teams: {by_team}")
        first, other = by_team[0][0], by_team[1][0]

        spectate, view, _, host = both("start", ["followlist"])
        need(spectate.get("autoFollow") == "0" and spectate.get("caster") == "0" and spectate.get("following") == "-1", f"start: {spectate}")
        need(spectate.get("speed") == "200", f"start: ui_spectatorSpeed 200 did not reach the server: {spectate}")
        need(view.get("vitals") == "0" and view.get("context") == "0", f"start: an ordinary spectator was sent vitals: {view}")
        # A casual spectator is neutral: every player on both teams may be followed.
        allowed = {int(target): flag for target, flag in re.findall(rf"MP_CAMERA observer={caster} target=(\d+) allowed=(\d)", host)}
        need(all(allowed.get(slot) == "1" for slot in players), f"start: a neutral spectator cannot follow everyone: {allowed}")
        listed = dict(re.findall(r"\n\s*(\d+)  (\S+?)(?:\^\d)?  ", read_log(logs["client"]).split("MP_CLIENT_START_BEGIN")[-1]))
        names = {int(slot): name for slot, name in listed.items()}
        need(set(names) == set(players) and sorted(names[slot] for slot in players if slot != 0) == sorted(BOTS),
             f"followlist does not list the four players by slot and name: {names}")

        spectate, view, _, _ = both("refused", ["caster on", "waitMsec 1500"])
        need(spectate.get("caster") == "0" and view.get("vitals") == "0", f"refused: caster view given with g_casterAccess 0: {spectate} {view}")

        step("host", "access_on", ["set g_casterAccess 1"])
        spectate, view, _, _ = both("caster", ["caster on", "waitMsec 2500"], shot=True)
        need(spectate.get("caster") == "1", f"caster: not granted with g_casterAccess 1: {spectate}")
        need(view.get("vitals") == "4" and view.get("context") == "1", f"caster: vitals not sent or not shown: {view}")
        sides = view.get("sideA", "") + view.get("sideB", "")
        need(all(name in sides for name in BOTS) and bool(re.search(r"\d+/\d+", sides)), f"caster: panel lacks names or vitals: {view}")

        spectate, view, local, _ = both("auto", ["follow auto leader", "waitMsec 1200"])
        need(spectate.get("autoFollow") == "3" and spectate.get("following", "").isdigit() and int(spectate["following"]) in players, f"auto: {spectate}")
        need(local.get("spectator") == spectate.get("following"), f"auto: the client was not moved to the server's choice: {local} {spectate}")
        need(spectate.get("caster") == "1", "auto: choosing a camera withdrew the caster view")
        for name, camera in (("chase", "2"), ("third", "1"), ("first", "0")):
            spectate, view, _, _ = both(name, [f"followview {name}", "waitMsec 900"], shot=True)
            need(view.get("camera") == camera and view.get("followView") == camera, f"{name}: camera {view}")
            need(spectate.get("autoFollow") == "3", f"{name}: a view change ended auto-follow: {spectate}")

        for name, slot in (("slot", other), ("otherteam", first)):
            spectate, _, local, _ = both(name, [f"follow {slot}", "waitMsec 900"])
            need(spectate.get("following") == str(slot) and spectate.get("autoFollow") == "0", f"{name}: follow {slot}: {spectate}")
            need(local.get("spectator") == str(slot), f"{name}: the client does not follow slot {slot}: {local}")
        target = names.get(other, "")
        spectate, _, _, _ = both("name", [f"follow {target[1:].lower()}", "waitMsec 900"])
        need(bool(target) and spectate.get("following") == str(other), f"name: part of {target!r} did not select slot {other}: {spectate}")
        spectate, _, _, _ = both("badname", ["follow nobodyhere", "follow 1x", "waitMsec 900"])
        need(spectate.get("following") == str(other), f"badname: an unknown name moved the camera: {spectate}")

        step("host", "access_off", ["set g_casterAccess 0", "waitMsec 1200"])
        spectate, view, _, _ = both("revoked", ["waitMsec 1500"])
        need(spectate.get("caster") == "0" and view.get("vitals") == "0" and view.get("context") == "0",
             f"revoked: the caster view outlived g_casterAccess: {spectate} {view}")

        both("killer", ["followauto killer", "waitMsec 900"])
        time.sleep(args.fight_seconds)
        spectate, _, local, host = both("fought", [])
        need(spectate.get("autoFollow") == "1" and spectate.get("following", "").isdigit() and int(spectate["following"]) in players, f"fought: {spectate}")
        # Recipient filtering: the camera the spectator holds is one the server allows it.
        held = re.findall(rf"MP_CAMERA observer={caster} target=(\d+) allowed=(\d) following=1", host)
        need(len(held) == 1 and held[0][1] == "1", f"fought: followed target is not an allowed one: {held}")

        # Hold the bots so the last report and both streams describe one moment.
        final_host = step("host", "end", ["set bot_pause 1", "waitMsec 3000", "openq4_reportMPState"])
        step("client", "end", ["waitMsec 500", "openq4_reportMPState"])
        # A listen server with bots takes a while to shut down on a busy machine.
        write_script(pumps["client"], ["quit"])
        processes["client"].wait(timeout=180)
        quit_started = time.time()
        write_script(pumps["host"], ["quit"])
        processes["host"].wait(timeout=180)
        host_quit_seconds = round(time.time() - quit_started, 1)
    except Stop as stop:
        failures.append(str(stop))
    except subprocess.TimeoutExpired:
        failures.append("a process did not quit")
    finally:
        codes = {}
        for role, process in processes.items():
            if process.poll() is None:
                process.kill()  # Only a process launched by this test.
                process.wait()
                codes[role] = -1
            else:
                codes[role] = process.returncode
        for console in consoles:
            console.close()

    need(codes == {"host": 0, "client": 0}, f"exit codes {codes}")
    for role in roles:
        need(not re.search(ERRORS, read_log(logs[role]), re.IGNORECASE), f"the {role} log contains an unknown command or error")
    need(all(path.is_file() and path.stat().st_size > 18 and path.stat().st_mtime >= started for path in shots),
         "missing fresh engine screenshots")

    # Each stream passes the fixture stream's validation and replays through the
    # sample overlay to the server's own final scores.
    summary = {}
    if final_host and not failures:
        contract = load("mp_live_event_stream_contract", ROOT / "tools/tests/mp_live_event_stream_contract.py")
        overlay = load("match_overlay", ROOT / "tools/overlay/match_overlay.py")
        server = {int(slot): (int(spectating), int(score)) for slot, spectating, score in
                  re.findall(r"MP_PLAYER slot=(\d+) bot=\d team=-?\d+ health=-?\d+ spectating=(\d) wantSpectate=\d ingame=1 score=(-?\d+)", final_host)}
        marine, strogg = (int(fields(final_host, "MP_STATE").get(team, "-999")) for team in ("marine", "strogg"))
        # The streams run on until each process quits, the client's first. They
        # are compared as they stood at the server's final report, a moment the
        # paused bots hold still around.
        end_time = int(fields(final_host, "MP_STATE").get("time", "0")) + 250
        kills = {}
        for role in roles:
            if not need(streams[role].is_file() and streams[role].stat().st_mtime >= started, f"{role}: g_matchEventStream 1 wrote no stream"):
                continue
            data = streams[role].read_bytes()
            need(data.endswith(b"\n") and b"\r" not in data, f"{role}: stream lines must end with a single newline")
            try:
                events = contract.check_stream(data.splitlines())
            except SystemExit as invalid:
                failures.append(f"{role}: {invalid}")
                continue
            shown = overlay.MatchState()
            need(all(shown.apply(event) for event in events if event["t"] <= end_time),
                 f"{role}: the overlay refused a line of the live stream")
            final = shown.snapshot()
            need(sum(event["type"] == "stream" for event in events) == 1, f"{role}: the stream restarted during the match")
            opening = events[0]
            need(opening["source"] == ("server" if role == "host" else "client") and opening["map"] == "mp/q4dm1"
                 and opening["gameType"] == "Team DM" and opening["teamGame"] is True, f"{role}: stream identity {opening}")
            need(final["gaps"] == 0 and final["rejected"] == 0 and final["streams"] == 1, f"{role}: stream bookkeeping {final['gaps']} gaps")
            streamed = {row["slot"]: (int(bool(row["spectator"])), row["score"]) for row in final["players"]}
            need(streamed == server and len(server) == 5, f"{role}: final roster differs: server {server}, stream {streamed}")
            need(final["teams"] == {"marine": marine, "strogg": strogg}, f"{role}: team scores {final['teams']} differ from {marine}-{strogg}")
            # a server knows who is a bot; a client is not told and must not guess
            need(all(("bot" in row) == (role == "host") for row in final["players"]), f"{role}: bot flags")
            kills[role] = [(event["killer"], event["victim"], event["weapon"], event["cause"]) for event in events if event["type"] == "kill"]
            need(bool(kills[role]), f"{role}: no kill reached the stream")
            need(any(event["type"] == "playerScore" for event in events), f"{role}: no score change reached the stream")
            need(any(event["type"] == "score" for event in events), f"{role}: no team score change reached the stream")
            for word in ("health", "armor", "origin", "ammo"):
                need(all(word not in json.dumps(event).lower().replace("armorregen", "").replace("ammoregen", "") for event in events),
                     f"{role}: the stream names {word}")
            whole = overlay.MatchState()
            for event in events:
                whole.apply(event)
            once = subprocess.run([sys.executable, str(ROOT / "tools/overlay/match_overlay.py"), "--file", str(streams[role]), "--once"],
                                  capture_output=True)
            need(once.returncode == 0 and json.loads(once.stdout.decode("utf-8")) == whole.snapshot(),
                 f"{role}: the overlay command line disagrees with its reducer")
            summary[role] = {"events": len(events), "kills": len(kills[role]), "types": sorted({event["type"] for event in events})}
        if set(kills) == set(roles):
            # The client joined later: what it was shown is the end of what the server scored.
            count = len(kills["client"])
            need(count <= len(kills["host"]) and kills["host"][len(kills["host"]) - count:] == kills["client"],
                 "the client's kill events are not the tail of the server's")

    report = {"status": "fail" if failures else "pass", "renderer": args.renderer, "casterSlot": caster, "hostQuitSeconds": host_quit_seconds, "streams": summary,
              "failures": failures, "logs": {role: str(path) for role, path in logs.items()}, "screenshots": [str(path) for path in shots]}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
