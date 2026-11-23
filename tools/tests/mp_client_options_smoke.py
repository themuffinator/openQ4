#!/usr/bin/env python3
"""Two clients and a bot exercise the MP-15 competitive client options with no input.

A listen server hosts Team DM with a second client on the host's team and a
paused bot on the other. Engine commands alone then drive: the weapon
preference order on pickups, chat tokens on both ends, landmark place names,
team info over the wire, the item-timer policy (spectators only, then
everyone, then back), crosshair ammunition and the time of day in the HUD
state, and hit tones on the remote client through the attack probe. Every
check reads openq4_reportClientOptions lines from the two engine logs.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import random
import re
import socket
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
ERRORS = r"ERROR:|FATAL ERROR|Ignoring invalid .*unreliable|Unknown unreliable message|Not allowed in multiplayer"


def free_port(taken: set[int]) -> int:
    """A loopback UDP port nothing on this machine is bound to.

    Other sessions run their own servers here, and a fixed port sends this
    test's client into somebody else's match."""
    for _ in range(200):
        port = random.randint(29000, 29900)
        if port in taken:
            continue
        try:
            for address in ("0.0.0.0", "127.0.0.1"):
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                    probe.bind((address, port))
        except OSError:
            continue
        taken.add(port)
        return port
    raise SystemExit("no free loopback UDP port found")


def done(log: str, marker: str) -> bool:
    """True once a marker has been echoed as a line of its own."""
    return re.search(r"\n" + marker + r"\s*\n", log) is not None


def after(log: str, marker: str) -> str:
    """The log from the last echo of a marker on."""
    parts = re.split(r"\n" + marker + r"\s*\n", log)
    return parts[-1] if len(parts) > 1 else ""


def plain(log: str) -> str:
    """A log without Quake colour escapes."""
    return re.sub(r"\^c\d\d\d|\^.", "", log)


def fact(text: str, kind: str) -> dict[str, str] | None:
    """The first CLIENTOPT line of a kind as a dictionary."""
    match = re.search(r"CLIENTOPT " + kind + r" (.*)", text)
    if match is None:
        return None
    return {key: value.strip('"') for key, value in re.findall(r'(\w+)=("[^"]*"|\S+)', match.group(1))}


def facts(text: str, kind: str) -> list[dict[str, str]]:
    return [{key: value.strip('"') for key, value in re.findall(r'(\w+)=("[^"]*"|\S+)', line)}
            for line in re.findall(r"CLIENTOPT " + kind + r" (.*)", text)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", help="runtime executable basename for an isolated test copy")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-client-options-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--render-api", choices=("gl", "vk"), default="gl")
    parser.add_argument("--timeout", type=int, default=420)
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    verify_runtime_is_current(runtime, getattr(args, "executable_name", ""))
    exe = runtime / (args.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"))
    if not exe.is_file() or not args.basepath.is_dir():
        parser.error("a staged runtime and installed Quake 4 assets are required")
    saves = {role: output / role for role in ("host", "client")}
    games = {role: path / "baseoq4" for role, path in saves.items()}
    for path in games.values():
        path.mkdir(parents=True, exist_ok=True)
    logs = {role: path / "logs/openq4.log" for role, path in games.items()}
    for log in logs.values():
        if log.is_file():
            log.unlink()

    # Each process loops one of two files; an action is published into the file
    # being looped and ends by moving the loop to the other one.
    names = ("options_a.cfg", "options_b.cfg")
    looping = {"host": 0, "client": 0}

    def idle(name: str) -> list[str]:
        return ["waitMsec 250", f"exec {name}"]

    for role in games:
        for name in names:
            write_script(games[role] / name, idle(name))
        write_script(games[role] / "options_start.cfg", ["exec options_a.cfg"])

    def dispatch(role: str, lines: list[str], marker: str) -> None:
        current, other = names[looping[role]], names[1 - looping[role]]
        write_script(games[role] / other, idle(other))
        write_script(games[role] / current, [*lines, f"echo {marker}", f"exec {other}"])
        looping[role] = 1 - looping[role]

    common = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(args.basepath.resolve()), "fs_game": "baseoq4", "com_gameMode": "MP",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
        "r_borderlessDefaultMigrated": "1", "r_mode": "-1", "r_customWidth": "1280",
        "r_customHeight": "720", "r_windowWidth": "1280", "r_windowHeight": "720",
        "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_renderApi": args.render_api, "com_maxfps": "60", "r_swapInterval": "0",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
        "ui_autoJoin": "1", "ui_spectate": "Play", "ui_ready": "Ready", "net_allowCheats": "1",
        "ui_retained": "1", "ui_hudLayout": "1", "ui_team": "Marine",
        "net_ip": "127.0.0.1", "net_enableIPv6": "0",
        "com_skipLoadingContinue": "1", "g_autoExecAfterMapLoad": "options_start.cfg",
        "g_autoExecAfterMapLoadDelayMs": "500",
    }

    taken: set[int] = set()
    ports = {role: free_port(taken) for role in ("host", "client")}

    def command(role: str) -> list[str]:
        cvars = dict(common, fs_savepath=str(saves[role]), fs_devpath=str(saves[role]), ui_name=role)
        if role == "host":
            cvars.update(net_port=str(ports["host"]), net_serverDedicated="0", net_LANServer="1", si_pure="0",
                         si_maxPlayers="4", si_gameType="Team DM", si_minPlayers="2", si_autoBalance="0",
                         si_warmup="1", si_useReady="0", si_countDown="4", si_timeLimit="0", si_fragLimit="999",
                         bot_minPlayers="0", bot_pause="1", g_matchProfile="casual", si_itemTimers="1")
        else:
            cvars.update(net_port=str(ports["client"]))
        result = [str(exe)]
        for key, value in cvars.items():
            result += ["+set", key, value]
        assert len(cvars) + 1 <= 64
        result += ["+spawnServer", "mp/q4dm1"] if role == "host" else ["+connect", f"127.0.0.1:{ports['host']}"]
        (output / f"{role}-launch.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        return result

    processes: dict[str, subprocess.Popen] = {}
    streams = []
    stage, failures, codes = 0, [], {}
    probes = 0
    started = time.time()
    report = "openq4_reportClientOptions"

    def launch(role: str) -> None:
        stream = (output / f"{role}-console.log").open("w", encoding="utf-8")
        streams.append(stream)
        processes[role] = subprocess.Popen(command(role), cwd=runtime, stdout=stream, stderr=subprocess.STDOUT)

    try:
        launch("host")
        while time.time() - started < args.timeout:
            host, client = read_log(logs["host"]), read_log(logs["client"])
            if re.search(ERRORS, host + client):
                failures.append("an engine reported an error")
                break
            if "client" not in processes and "MP match phase: 0 -> 1" in host:
                if f"UDP socket: 127.0.0.1:{ports['host']}" not in host:
                    failures.append(f"the host did not open 127.0.0.1:{ports['host']}")
                    break
                launch("client")
                probes = 1
                dispatch("client", ["waitMsec 3000", "echo OPT_PROBE_1", report], "OPT_PROBED_1")
            if stage == 0 and "client" in processes and done(client, f"OPT_PROBED_{probes}"):
                seen = fact(after(client, f"OPT_PROBE_{probes}"), "player")
                if seen is None or seen.get("spectating") != "0":
                    # not in the match yet: ask again
                    probes += 1
                    dispatch("client", ["waitMsec 1000", f"echo OPT_PROBE_{probes}", report], f"OPT_PROBED_{probes}")
                else:
                    dispatch("host", ["addbot Cortez 3 exact"], "OPT_DONE_HOST_0")
                    stage = 10
            # The bot gives the other team a player: the match counts down and goes
            # live with a restart that respawns everyone. Weapons are given after it.
            if stage == 10 and done(host, "OPT_DONE_HOST_0") and "MP match phase: 2 -> 3" in host:
                if stage == 10:
                    # Smart switching (ui_autoSwitch 2, the default) first: every weapon
                    # given is new, so weapons-stay never refuses one. _impulseN is the
                    # engine's own impulse command, not input. Both settings are archived,
                    # so a reused savepath still holds the last run's values: set them.
                    dispatch("host", ["waitMsec 2500", "set ui_autoSwitch 2", 'set ui_weaponOrder ""', "waitMsec 1000",
                                      "echo OPT_SMART_SPAWN", report,
                                      "give weapon_shotgun", "waitMsec 2000", "echo OPT_SMART_SG", report,
                                      "give weapon_rocketlauncher", "waitMsec 2000", "echo OPT_SMART_RL", report,
                                      "_impulse0", "waitMsec 2000", "echo OPT_SMART_GAUNTLET", report,
                                      "give weapon_hyperblaster", "waitMsec 2000", "echo OPT_SMART_HB", report,
                                      "_impulse51", "waitMsec 2000", "echo OPT_LAST_WEAPON", report,
                                      "set ui_autoSwitch 0", "waitMsec 1000",
                                      "give weapon_nailgun", "waitMsec 2000", "echo OPT_OFF_NG", report,
                                      "set ui_autoSwitch 1", 'set ui_weaponOrder "rg gl"', "waitMsec 1000",
                                      "give weapon_grenadelauncher", "waitMsec 2000", "echo OPT_AFTER_GL", report,
                                      "give weapon_railgun", "waitMsec 2000", "echo OPT_AFTER_RG", report,
                                      "give weapon_lightninggun", "waitMsec 2000", "echo OPT_AFTER_LG", report,
                                      "say status #h #a #w at #l with #n", 'say "quoted ##h #h"', "listLocations"],
                             "OPT_DONE_HOST_1")
                    stage = 1
            if stage == 1 and done(host, "OPT_DONE_HOST_1"):
                # si_itemTimers is still 1 here: a player must hold no timers
                dispatch("client", ["say_team here #l with #n hp #h", "set hud_crosshairAmmo 1", "set hud_localClock 1",
                                    "waitMsec 1500", "echo OPT_CLIENT_STATE", report], "OPT_DONE_CLIENT_1")
                stage = 2
            if stage == 2 and done(client, "OPT_DONE_CLIENT_1"):
                dispatch("host", ["set si_itemTimers 2", "rescanSI", "waitMsec 1500", "echo OPT_HOST_TIMERS", report],
                         "OPT_DONE_HOST_2")
                stage = 20
            if stage == 20 and done(host, "OPT_DONE_HOST_2"):
                rows = facts(after(host, "OPT_HOST_TIMERS"), "timer")
                if not rows:
                    failures.append("the host saw no item timers under si_itemTimers 2")
                    break
                dispatch("host", [f"teleport {rows[0]['entity']}", "waitMsec 1500", "echo OPT_HOST_PICKUP", report],
                         "OPT_DONE_HOST_3")
                stage = 3
            if stage == 3 and done(host, "OPT_DONE_HOST_3"):
                dispatch("client", ["waitMsec 1500", "echo OPT_CLIENT_TIMERS", report,
                                    'screenshot "screenshots/options_remastered.tga"',
                                    "set ui_hudLayout 3", "waitMsec 1200", "echo OPT_CLIENT_COMPETITIVE", report,
                                    'screenshot "screenshots/options_competitive.tga"',
                                    "set ui_hudLayout 2", "waitMsec 1200", 'screenshot "screenshots/options_minimal.tga"',
                                    "set ui_hudLayout 0", "waitMsec 1200", 'screenshot "screenshots/options_classic.tga"',
                                    "set ui_hudLayout 1"], "OPT_DONE_CLIENT_2")
                stage = 4
            if stage == 4 and done(client, "OPT_DONE_CLIENT_2"):
                dispatch("host", ["set si_itemTimers 1", "rescanSI"], "OPT_DONE_HOST_4")
                stage = 5
            if stage == 5 and done(host, "OPT_DONE_HOST_4"):
                dispatch("client", ["waitMsec 5000", "echo OPT_CLIENT_POLICY1", report], "OPT_DONE_CLIENT_3")
                stage = 6
            if stage == 6 and done(client, "OPT_DONE_CLIENT_3"):
                me = fact(after(client, "OPT_CLIENT_POLICY1"), "player")
                x, y, z = (float(v) for v in me["origin"].split(","))
                yaw = math.radians(float(me["yaw"]))
                # the bot is the third player in: stand it in the client's line of fire
                target = f"{x + 128 * math.cos(yaw):.0f} {y + 128 * math.sin(yaw):.0f} {z + 1:.0f}"
                dispatch("host", [f"script \"$player3.setOrigin('{target}')\"", "waitMsec 500"], "OPT_DONE_HOST_5")
                stage = 7
            if stage == 7 and done(host, "OPT_DONE_HOST_5"):
                dispatch("client", ["set hud_hitSound 2", "set in_presentationViewProbeAttack 1", "waitMsec 1500",
                                    "echo OPT_CLIENT_AIM", report,
                                    "set in_presentationViewProbeAttack 0", "waitMsec 600", "echo OPT_CLIENT_HITS", report,
                                    # a spectator hears about every player, ammunition included
                                    "set ui_spectate Spectate", "waitMsec 2500", "echo OPT_CLIENT_SPECTATE", report,
                                    "waitMsec 300", "quit"], "OPT_DONE_CLIENT_4")
                stage = 8
            if stage == 8 and done(client, "OPT_CLIENT_HITS") and processes["client"].poll() is not None:
                dispatch("host", ["quit"], "OPT_DONE_HOST_6")
                stage = 9
            if stage == 9 and processes["host"].poll() is not None:
                break
            if stage < 8 and (processes["host"].poll() is not None or
                              ("client" in processes and processes["client"].poll() is not None)):
                failures.append(f"a process ended before completing stage {stage}")
                break
            time.sleep(0.2)
        else:
            failures.append(f"timeout at stage {stage}")
    finally:
        for role, process in processes.items():
            try:
                codes[role] = process.wait(timeout=8 if stage == 9 else 1)
            except subprocess.TimeoutExpired:
                process.kill()  # Only a process launched by this test.
                process.wait()
                codes[role] = -1
        for stream in streams:
            stream.close()

    host, client = read_log(logs["host"]), read_log(logs["client"])
    results: dict[str, object] = {}

    def check(name: str, condition: bool, detail: object = None) -> None:
        results[name] = {"pass": bool(condition), "detail": detail}
        if not condition:
            failures.append(name)

    if stage == 9 and not any(codes.values()) and "client joined Marine team" not in plain(host):
        failures.append("the client never joined the host's match")
    elif stage == 9 and not any(codes.values()):
        # smart switching: the machine gun and the gauntlet give way, a real weapon stays
        for marker, expected in (("OPT_SMART_SPAWN", "weapon_machinegun"), ("OPT_SMART_SG", "weapon_shotgun"),
                                 ("OPT_SMART_RL", "weapon_shotgun"), ("OPT_SMART_GAUNTLET", "weapon_gauntlet"),
                                 ("OPT_SMART_HB", "weapon_hyperblaster"), ("OPT_LAST_WEAPON", "weapon_gauntlet"),
                                 ("OPT_OFF_NG", "weapon_gauntlet")):
            weapon = fact(after(host, marker), "weapon") or {}
            check(f"smart switch {marker}", weapon.get("def", "").startswith(expected) and weapon.get("order_listed") == "0", weapon)
        # weapon preference order: "rg gl" with the gauntlet in hand, under ui_autoSwitch 1
        for marker, expected in (("OPT_AFTER_GL", "weapon_grenadelauncher"), ("OPT_AFTER_RG", "weapon_railgun"),
                                 ("OPT_AFTER_LG", "weapon_railgun")):
            weapon = fact(after(host, marker), "weapon") or {}
            check(f"weapon order {marker}", weapon.get("def", "").startswith(expected) and weapon.get("order_listed") == "2", weapon)

        # chat tokens, expanded where the line was typed: as console words, quoted, and from the remote client
        said = re.search(r"host: status (\d+) (\d+) (.+) at (.+) with (.+)", plain(host))
        check("host chat tokens", said is not None and "#" not in said.group(0), said.group(0) if said else None)
        quoted = re.search(r"host: quoted #h (\d+)", plain(host))
        check("a doubled hash stays a hash", quoted is not None, quoted.group(0) if quoted else None)
        team = re.search(r"client \(Marine\): here (.+) with (.+) hp (\d+)", plain(host))
        check("client chat tokens reach the host expanded", team is not None and "#" not in team.group(0),
              team.group(0) if team else None)

        # landmarks
        place = fact(after(host, "OPT_AFTER_GL"), "location") or {}
        check("landmark place names", int(place.get("landmarks", "0")) > 0 and place.get("name", "") != "", place)

        # team info over the wire, and the HUD state
        state = after(client, "OPT_CLIENT_STATE").split("OPT_CLIENT_TIMERS")[0]
        mates = [m for m in facts(state, "mate") if m.get("slot") == "0"]
        check("team info record for the team mate", bool(mates) and mates[0].get("source") == "record"
              and int(mates[0].get("health", "0")) > 0 and mates[0].get("location", "") != "", mates)
        foes = [m for m in facts(state, "mate") if m.get("team") != (fact(state, "player") or {}).get("team")]
        check("no team info record for an opponent", all(m.get("source") == "entity" for m in foes), foes)
        check("the team mate's record carries the ammunition for the weapon in hand",
              bool(mates) and 0 <= int(mates[0].get("ammo", "255")) <= 250, mates)
        watching = facts(after(client, "OPT_CLIENT_SPECTATE"), "mate")
        check("a spectator holds every player's record with ammunition",
              len(watching) >= 2 and all(m.get("source") == "record" and 0 <= int(m.get("ammo", "255")) <= 250 for m in watching)
              and len({m.get("team") for m in watching}) >= 2, watching)
        hud = fact(state, "hud") or {}
        check("crosshair ammunition in the HUD state", hud.get("xhair_ammo", "").isdigit(), hud)
        check("time of day in the HUD state", re.fullmatch(r"\d\d:\d\d", hud.get("local_clock", "")) is not None, hud)

        # item timers: spectators only, then everyone, then spectators only again
        early = fact(state, "timers") or {}
        check("si_itemTimers 1 sends a player nothing", early.get("count") == "0", early)
        picked = facts(after(host, "OPT_HOST_PICKUP"), "timer")
        check("the host's pickup starts a timer", any(int(r["left_ms"]) > 0 for r in picked), picked[:3])
        sent = after(client, "OPT_CLIENT_TIMERS")
        rows = facts(sent.split("OPT_CLIENT_COMPETITIVE")[0], "timer")
        check("si_itemTimers 2 sends the list to a player", any(int(r["left_ms"]) > 0 for r in rows), rows[:3])
        counting = [r for r in picked + rows if int(r["left_ms"]) > 0]
        check("a counting timer knows its whole respawn (the HUD's progress)",
              bool(counting) and all(int(r.get("total_ms", "0")) >= int(r["left_ms"]) for r in counting), counting[:3])
        hud = fact(sent, "hud") or {}
        check("item timers in the HUD state", int(hud.get("it_count", "0")) > 0 and hud.get("it0", "/").split("/")[0] != "", hud)
        competitive = fact(after(client, "OPT_CLIENT_COMPETITIVE"), "hud") or {}
        check("team list names the team mate's location", competitive.get("ts0", "@").split("@")[-1] != "", competitive)
        late = fact(after(client, "OPT_CLIENT_POLICY1"), "timers") or {}
        check("back at si_itemTimers 1 the list stops", late.get("count") == "0", late)

        # hit tones and the crosshair name on the remote client
        aim = fact(after(client, "OPT_CLIENT_AIM"), "hud") or {}
        check("crosshair names the player aimed at", "Cortez" in aim.get("aim", ""), aim)
        tones = fact(after(client, "OPT_CLIENT_HITS"), "hitsound") or {}
        check("hit tones by damage on the remote client", int(tones.get("tones", "0")) > 0
              and tones.get("last_pitch") in ("1.30", "1.10", "0.92", "0.76"), tones)
    else:
        failures.append(f"incomplete two-client run: stage={stage}, exits={codes}")

    for role, log in (("host", host), ("client", client)):
        if not logs[role].is_file() or logs[role].stat().st_mtime < started:
            failures.append(f"missing fresh {role} engine log")
        if re.search(ERRORS, log):
            failures.append(f"{role} reported an engine error")
    shots = [games["client"] / f"screenshots/options_{name}.tga" for name in ("remastered", "competitive", "minimal", "classic")]
    if not all(p.is_file() and p.stat().st_size > 18 and p.stat().st_mtime >= started for p in shots):
        failures.append("missing fresh engine screenshots")
    summary = {"status": "fail" if failures else "pass", "render_api": args.render_api, "stage": stage, "failures": failures,
               "exits": codes, "checks": results, "logs": {k: str(v) for k, v in logs.items()},
               "screenshots": [str(p) for p in shots]}
    (output / "report.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
