#!/usr/bin/env python3
"""Windowed, input-free runs of the multiplayer modifiers and presets (roadmap MP-14).

Each scenario hosts a hidden listen server with three bots on a stock map, drives
it from a script run after the map loads, and reads the engine log: the latched
rules and each player's loadout (openq4_reportMPState), the entities the map was
populated with (listEntities) and the server's own diagnostics.

  quadhog   Deathmatch with Quad Hog: the quad spawns, the host takes it, the
            bots hunt, and only frags by or on the hog score.
  vampiric  Team Deathmatch with Vampiric Damage: hits heal the attacker, never
            across a team, and the map has no health pickups.
  presets   `preset rocketarena`, a hand change that turns it Custom, the
            modifier and weaponArena commands, `preset standard`, Weapons Frenzy.
  instactf  Capture the Flag with `preset instactf`: Instagib, and the flags stay.
  remote-arena, remote-quadhog
            A second client joins the host. It must latch the same rules from
            the server info, populate the same pickups, carry the same loadout
            and see the hog's quad, with no entity disagreement in either log.

This needs the installed Quake 4 assets and a staged runtime; it is a
qualification tool, not part of the asset-free validation list.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import socket
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
BOTS = ["addbot Cortez 4 exact", "addbot Rhodes 4 exact", "addbot Sledge 4 exact", "waitMsec 4000"]


def checkpoint(name: str, entities: bool = False) -> list[str]:
    return [f"echo MP_MOD_{name.upper()}", "openq4_reportMPState", *(["listEntities", f"echo MP_MOD_{name.upper()}_END"] if entities else [])]


SCENARIOS = {
    "quadhog": {
        "gametype": "DM", "map": "mp/q4dm1", "cvars": {"si_quadHog": "1"},
        "script": [*BOTS, "waitMsec 6000", *checkpoint("spawned", True),
                   "teleport openq4_quad_hog_1", "waitMsec 1500", *checkpoint("hog"),
                   "waitMsec 45000", *checkpoint("fought"),
                   "waitMsec 35000", *checkpoint("end", True)],
        "timeout": 240,
    },
    "vampiric": {
        "gametype": "Team DM", "map": "mp/q4dm1", "cvars": {"si_vampiric": "1"},
        "script": [*BOTS, *checkpoint("start", True), "waitMsec 60000", *checkpoint("end")],
        "timeout": 200,
    },
    "presets": {
        "gametype": "DM", "map": "mp/q4dm1", "cvars": {},
        "script": [*BOTS, *checkpoint("standard", True),
                   "preset rocketarena", "waitMsec 8000", *checkpoint("rocketarena", True),
                   "waitMsec 15000", *checkpoint("rocketfight"),
                   "set si_instagib 1", "rescanSI", "waitMsec 8000", *checkpoint("instagib"),
                   "modifier instagib 0", "waitMsec 7000", "weaponArena none", "waitMsec 7000", *checkpoint("nopowerups", True),
                   "preset standard", "waitMsec 8000", *checkpoint("reset", True),
                   "set bot_pause 1", "modifier frenzy 1", "waitMsec 9000", *checkpoint("frenzy_a"),
                   "waitMsec 6000", *checkpoint("frenzy_b"), "listPresets", "modifier",
                   # a server's own presets: one refused whole, one applied (it changes the game type, so the map reloads)
                   "preset locked", "waitMsec 2000", *checkpoint("refused"),
                   "preset friday", "waitMsec 20000", *checkpoint("friday")],
        "files": {"presets/friday.cfg": "// Friday night\nseta si_gameType \"Team DM\"\nsi_vampiric 1\nsi_weaponArena lightninggun\nsi_fragLimit 60\n",
                  "presets/locked.cfg": "si_vampiric 1\nnet_serverRemoteConsolePassword letmein\n"},
        "timeout": 340,
    },
    "instactf": {
        "gametype": "CTF", "map": "mp/q4ctf1", "cvars": {},
        "script": [*BOTS, *checkpoint("standard", True), "preset instactf", "waitMsec 9000", *checkpoint("instactf", True),
                   "waitMsec 15000", *checkpoint("end"), "si_captureLimit"],
        "timeout": 200,
    },
}


# Two-process scenarios: the host's script, the joining client's, and when the client is launched.
REMOTE = {
    "remote-arena": {
        "gametype": "Team DM", "map": "mp/q4dm1",
        "cvars": {"si_weaponArena": "rocketlauncher", "si_vampiric": "1", "si_powerups": "0", "si_selfDamage": "0"},
        "host": ["addbot Cortez 4 exact", "addbot Rhodes 4 exact"], "joined": [],
        "client": ["waitMsec 20000", *checkpoint("client", True), "waitMsec 2000"],
        "timeout": 420,
    },
    "remote-quadhog": {
        "gametype": "DM", "map": "mp/q4dm1", "cvars": {"si_quadHog": "1"},
        # The host takes the quad once the client is in. With the bots paused nobody else does, so the
        # quad is taken back and respawned under the next name every 25 s: go to whichever is current.
        "host": ["set bot_pause 1", "addbot Cortez 4 exact"], "joined": "quad",
        "client": ["waitMsec 20000", *checkpoint("client", True), "waitMsec 2000"],
        "timeout": 420,
    },
}


def free_port() -> int:
    """A loopback UDP port nobody is listening on, with the next one free for the joining client."""
    for _ in range(64):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as first:
            first.bind(("127.0.0.1", 0))
            port = first.getsockname()[1]
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as second:
                    second.bind(("127.0.0.1", port + 1))
                    return port
            except OSError:
                continue
    raise RuntimeError("no free loopback port pair")


def paced(lines: list, scale: float) -> list:
    """The script with every waitMsec stretched: the waits are wall-clock, and a loaded machine runs the game slower."""
    return [re.sub(r"^waitMsec (\d+)$", lambda match: "waitMsec %d" % round(int(match.group(1)) * scale), line) for line in lines]


def block(log: str, name: str, entities: bool = False) -> str:
    """The log text of one checkpoint."""
    # echo prints its argument and a space
    marks = list(re.finditer(r"\nMP_MOD_" + name.upper() + r" ?\r?\n", log))
    if not marks:
        raise AssertionError(f"checkpoint {name} is missing from the log")
    start = marks[-1].start()
    end = log.find("\nMP_MOD_", start + 1)
    if entities:
        end = log.find("\nMP_MOD_" + name.upper() + "_END", start + 1)
    return log[start:end if end > 0 else len(log)]


def rules(text: str) -> dict:
    match = re.search(r"MP_RULES modifiers='([^']*)' fixedWeapon=(\S+) quadHog=(-?\d+) preset='([^']*)' selfDamage=(\d+) "
                      r"knockback=(\d+) fallDamage=(\d+) weaponRespawn=(\d+) title='([^']*)'", text)
    if match is None:
        raise AssertionError("no MP_RULES line at the checkpoint")
    keys = ("modifiers", "fixedWeapon", "quadHog", "preset", "selfDamage", "knockback", "fallDamage", "weaponRespawn", "title")
    return dict(zip(keys, match.groups()))


def loadouts(text: str) -> dict:
    return {int(slot): {"weapons": int(weapons, 16), "current": int(current), "ammo": int(ammo), "powerups": int(powerups, 16)}
            for slot, weapons, current, ammo, powerups in
            re.findall(r"MP_LOADOUT slot=(\d+) weapons=0x([0-9a-f]+) current=(-?\d+) ammo=(-?\d+) powerups=0x([0-9a-f]+)", text)}


def players(text: str) -> dict:
    return {int(slot): {"team": int(team), "health": int(health), "score": int(score)}
            for slot, team, health, score in
            re.findall(r"MP_PLAYER slot=(\d+) bot=\d team=(-?\d+) health=(-?\d+) [^\n]*? score=(-?\d+)", text)}


def pickups(text: str) -> dict:
    """Map pickups by classname prefix, from a listEntities dump."""
    counts = {"weapon": 0, "ammo": 0, "health": 0, "armor": 0, "powerup": 0, "flag": 0, "names": []}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 3 or not fields[0].rstrip(":").isdigit():
            continue
        definition = fields[1]
        if definition.startswith("weapon_"):
            counts["weapon"] += 1
        elif definition.startswith("ammo_"):
            counts["ammo"] += 1
        elif definition.startswith("item_health"):
            counts["health"] += 1
        elif definition.startswith("item_armor"):
            counts["armor"] += 1
        elif definition.startswith("powerup_"):
            counts["powerup"] += 1
            counts["names"].append(definition)
        elif definition.startswith("mp_ctf_") and "flag" in definition:
            counts["flag"] += 1
    return counts


def expect(condition: bool, message: str, failures: list) -> None:
    if not condition:
        failures.append(message)


def check_quadhog(log: str, failures: list) -> dict:
    spawned = block(log, "spawned", True)
    state = rules(spawned)
    expect(state["modifiers"] == "quadhog", f"latched modifiers: {state['modifiers']!r}", failures)
    expect(state["title"] == "Quad Hog Deathmatch", f"match title: {state['title']!r}", failures)
    found = pickups(spawned)
    expect(found["weapon"] > 0 and found["health"] > 0, "Quad Hog must keep the map's weapons and health", failures)
    expect(all(name.startswith("powerup_quad_damage") for name in found["names"]) and found["powerup"] <= 1,
           f"only the hog's quad may be on the map: {found['names']}", failures)
    spawns = re.findall(r"Quad Hog: a quad spawned at ", log)
    takes = [int(client) for client in re.findall(r"Quad Hog: client (\d+) took the quad", log)]
    expect(bool(spawns), "the quad never spawned", failures)
    expect(bool(takes), "nobody took the quad", failures)
    hog = block(log, "hog")
    carrier = int(rules(hog)["quadHog"])
    if carrier >= 0:
        expect(loadouts(hog).get(carrier, {}).get("powerups", 0) & 1 == 1, "the hog does not carry Quad Damage", failures)
    kills = re.findall(r"Quad Hog: client (\d+) killed client (\d+), hog (-?\d+): (scores|no score)", log)
    for killer, dead, carrier_at, verdict in kills:
        involved = carrier_at in (killer, dead)
        if killer != dead:
            expect((verdict == "scores") == involved, f"frag {killer}->{dead} with hog {carrier_at} was '{verdict}'", failures)
    end = players(block(log, "end", True))
    # Nobody gains a point except through a frag that scored.
    scoring = {}
    for killer, dead, _, verdict in kills:
        if verdict == "scores" and killer != dead:
            scoring[int(killer)] = scoring.get(int(killer), 0) + 1
    for slot, player in end.items():
        expect(player["score"] <= scoring.get(slot, 0), f"slot {slot} has {player['score']} points from {scoring.get(slot, 0)} scoring frags", failures)
    return {"spawns": len(spawns), "takes": takes, "kills": len(kills), "scored": sum(1 for kill in kills if kill[3] == "scores"),
            "loose": len(re.findall(r"Quad Hog: the quad is loose", log)), "taken_back": len(re.findall(r"taking it back", log)),
            "scores": {slot: player["score"] for slot, player in end.items()}}


def check_vampiric(log: str, failures: list) -> dict:
    start = block(log, "start", True)
    state = rules(start)
    expect(state["modifiers"] == "vampiric", f"latched modifiers: {state['modifiers']!r}", failures)
    expect(state["title"] == "Vampiric Team Deathmatch", f"match title: {state['title']!r}", failures)
    found = pickups(start)
    expect(found["health"] == 0, f"{found['health']} health pickups are on the map", failures)
    expect(found["weapon"] > 0 and found["armor"] > 0, "Vampiric Damage must keep weapons and armour", failures)
    teams = {slot: player["team"] for slot, player in players(start).items()}
    drains = re.findall(r"vampiric: client (\d+) took (\d+) from client (\d+), gains (\d+), health (\d+) of (\d+)", log)
    expect(bool(drains), "no hit healed its attacker", failures)
    for attacker, taken, victim, gain, health, cap in drains:
        attacker, taken, victim, gain, health, cap = int(attacker), int(taken), int(victim), int(gain), int(health), int(cap)
        expect(attacker != victim and teams.get(attacker) != teams.get(victim), f"client {attacker} drained team mate or self {victim}", failures)
        expect(gain == max(1, (taken + 1) // 2), f"a hit that took {taken} gave {gain}", failures)
        expect(health <= cap == 200, f"health {health} over the cap {cap}", failures)
    return {"drains": len(drains), "health_gained": sum(int(drain[3]) for drain in drains),
            "largest_health": max((int(drain[4]) for drain in drains), default=0)}


def check_presets(log: str, failures: list) -> dict:
    standard = block(log, "standard", True)
    baseline = pickups(standard)
    expect(rules(standard)["modifiers"] == "" and rules(standard)["title"] == "Deathmatch", "the stock rules must latch nothing", failures)
    expect(min(baseline["weapon"], baseline["ammo"], baseline["health"], baseline["armor"], baseline["powerup"]) > 0,
           f"the stock map is missing pickups: {baseline}", failures)

    arena = block(log, "rocketarena", True)
    state = rules(arena)
    expect(state["modifiers"] == "arena:rocketlauncher nopowerups", f"rocketarena latched {state['modifiers']!r}", failures)
    expect(state["preset"] == "rocketarena" and state["selfDamage"] == "0" and state["fallDamage"] == "0", f"rocketarena settings: {state}", failures)
    expect(state["fixedWeapon"] == "weapon_rocketlauncher" and state["title"] == "Deathmatch (Rocket Launcher)", f"rocketarena presentation: {state}", failures)
    found = pickups(arena)
    expect(found["weapon"] == 0 and found["ammo"] == 0 and found["powerup"] == 0, f"rocketarena left pickups: {found}", failures)
    expect(found["health"] == baseline["health"] and found["armor"] == baseline["armor"], "a weapon arena keeps health and armour", failures)
    for slot, loadout in loadouts(arena).items():
        expect(bin(loadout["weapons"]).count("1") == 2, f"slot {slot} carries weapons 0x{loadout['weapons']:x}", failures)
    expect(any(loadout["ammo"] == -1 for loadout in loadouts(arena).values()), "nobody holds the unlimited arena weapon", failures)
    expect("preset 'rocketarena' applied" in log, "the preset command did not report", failures)

    instagib = rules(block(log, "instagib"))
    expect(instagib["modifiers"] == "instagib" and instagib["preset"] == "", f"a hand change must clear si_preset: {instagib}", failures)
    expect("The settings no longer match preset 'rocketarena'; si_preset cleared" in log, "the drift was not reported", failures)
    expect("The match modifiers changed to 'instagib': restarting the match" in log, "the modifier change did not restart the match", failures)

    left = block(log, "nopowerups", True)
    expect(rules(left)["modifiers"] == "nopowerups" and rules(left)["fixedWeapon"] == "none", f"after the commands: {rules(left)}", failures)
    found = pickups(left)
    expect(found["weapon"] > 0 and found["ammo"] == baseline["ammo"] and found["powerup"] == 0, f"si_powerups 0 on the stock map: {found}", failures)

    reset = block(log, "reset", True)
    expect(rules(reset)["modifiers"] == "" and rules(reset)["preset"] == "standard" and rules(reset)["selfDamage"] == "100",
           f"preset standard: {rules(reset)}", failures)
    restored = pickups(reset)
    # dropped weapons are listed too, so the weapon count only has to be there
    expect(restored["weapon"] > 0 and all(restored[key] == baseline[key] for key in ("ammo", "health", "armor", "powerup")),
           f"preset standard must bring the stock map back: {restored}", failures)

    first, second = block(log, "frenzy_a"), block(log, "frenzy_b")
    expect(rules(first)["modifiers"] == "frenzy" and rules(first)["title"] == "Frenzy Deathmatch", f"frenzy: {rules(first)}", failures)
    # The bots are paused and nobody fires: a fresh spawn's ammo under the stock rules (the reset
    # checkpoint, where the host has just spawned) against the same weapon a while into Weapons Frenzy.
    before, after = loadouts(reset), loadouts(second)
    gains = {slot: (before[slot]["ammo"], after[slot]["ammo"]) for slot in before
             if slot in after and before[slot]["current"] == after[slot]["current"] >= 0 and before[slot]["ammo"] >= 0
             and before[slot]["weapons"] == after[slot]["weapons"]}
    expect(bool(gains) and all(new > old for old, new in gains.values()), f"carried ammo did not regenerate under Weapons Frenzy: {gains}", failures)
    refused = rules(block(log, "refused"))
    expect("preset: presets/locked.cfg line 2: 'net_serverRemoteConsolePassword' is not a setting a preset may change" in log,
           "the preset with a forbidden setting was not refused with its reason", failures)
    expect(refused["modifiers"] == "frenzy" and refused["preset"] == "", f"a refused preset changed the match: {refused}", failures)
    friday = rules(block(log, "friday"))
    expect(friday["preset"] == "friday" and friday["modifiers"] == "vampiric arena:lightninggun", f"the file preset latched {friday}", failures)
    expect(friday["title"] == "Vampiric Team Deathmatch (Lightning Gun)", f"the file preset's title: {friday['title']!r}", failures)
    return {"baseline": {key: value for key, value in baseline.items() if key != "names"}, "frenzy_ammo": gains,
            "regenerated": any(new > old for old, new in gains.values()), "file_preset": friday["title"]}


def check_instactf(log: str, failures: list) -> dict:
    standard, modified = block(log, "standard", True), block(log, "instactf", True)
    expect(rules(standard)["title"] == "Capture the Flag", f"stock title: {rules(standard)['title']!r}", failures)
    state = rules(modified)
    expect(state["modifiers"] == "instagib" and state["preset"] == "instactf", f"instactf latched {state}", failures)
    expect(state["title"] == "Instagib Capture the Flag" and state["fixedWeapon"] == "weapon_railgun", f"instactf presentation: {state}", failures)
    found = pickups(modified)
    expect(found["weapon"] + found["ammo"] + found["health"] + found["armor"] + found["powerup"] == 0, f"Instagib left pickups: {found}", failures)
    expect(found["flag"] >= 2, f"the flags must stay: {found['flag']} (stock {pickups(standard)['flag']})", failures)
    # The preset's capture limit must survive the restart: a casual session mirrors its limits back at every map start.
    limit = re.search(r'"si_captureLimit" is:"(\d+)"', log[log.rfind("MP_MOD_END"):])
    expect(limit is not None and limit.group(1) == "8", f"instactf's capture limit after the restart: {limit.group(1) if limit else None}", failures)
    return {"flags": found["flag"], "capture_limit": limit.group(1) if limit else None}


CHECKS = {"quadhog": check_quadhog, "vampiric": check_vampiric, "presets": check_presets, "instactf": check_instactf}


def launch_cvars(args, savepath: Path, script: str) -> dict:
    return {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(savepath),
        "fs_devpath": str(savepath), "fs_game": "baseoq4", "com_gameMode": "MP",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
        "r_borderlessDefaultMigrated": "1", "r_mode": "-1", "r_customWidth": "960",
        "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540",
        "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_renderApi": args.render_api, "com_maxfps": "60", "r_swapInterval": "0",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
        "ui_autoJoin": "1", "ui_spectate": "Play", "com_skipLoadingContinue": "1", "net_allowCheats": "1",
        "g_autoExecAfterMapLoad": script, "g_autoExecAfterMapLoadDelayMs": "1000",
    }


def run_remote(name: str, args) -> dict:
    scenario = REMOTE[name]
    runtime, output = args.runtime_dir.resolve(), (args.output_dir / name).resolve()
    exe = runtime / (args.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"))
    saves = {role: output / role for role in ("host", "client")}
    logs = {role: path / "baseoq4/logs/openq4.log" for role, path in saves.items()}
    for role, path in saves.items():
        (path / "baseoq4").mkdir(parents=True, exist_ok=True)
        if logs[role].is_file():
            logs[role].unlink()
    # The client loads for as long as it loads, so the host does not run to a
    # clock: it idles on a script this harness rewrites as the client gets on.
    idle = ["waitMsec 500", "exec mp_modifiers_idle.cfg"]
    write_script(saves["host"] / "baseoq4/mp_modifiers_idle.cfg", idle)
    write_script(saves["host"] / "baseoq4/mp_modifiers_smoke.cfg", [*scenario["host"], "exec mp_modifiers_idle.cfg"])
    write_script(saves["client"] / "baseoq4/mp_modifiers_smoke.cfg",
                 [*paced(scenario["client"], args.time_scale), "echo MP_MOD_DONE_CLIENT", "quit"])
    host = launch_cvars(args, saves["host"], "mp_modifiers_smoke.cfg")
    host.update({
        "net_port": str(args.port), "net_ip": "127.0.0.1", "net_enableIPv6": "0", "net_serverDedicated": "0", "net_LANServer": "1",
        "si_pure": "0", "si_maxPlayers": "8", "si_gameType": scenario["gametype"], "si_warmup": "0", "si_useReady": "0",
        "si_minPlayers": "1", "si_timeLimit": "0", "si_fragLimit": "999", "bot_minPlayers": "0", "g_matchProfile": "casual",
        "si_autoBalance": "1", "ui_team": "Marine", "ui_name": "host",
        "si_ruleset": "0", "si_instagib": "0", "si_quadHog": "0", "si_vampiric": "0", "si_weaponsFrenzy": "0",
        "si_weaponArena": "none", "si_powerups": "1", "si_selfDamage": "100", "si_knockback": "100", "si_fallDamage": "100",
        "si_weaponRespawn": "0", "si_preset": "",
    })
    host.update(scenario["cvars"])
    client = launch_cvars(args, saves["client"], "mp_modifiers_smoke.cfg")
    client.update({"net_port": str(args.port + 1), "net_ip": "127.0.0.1", "net_enableIPv6": "0", "ui_team": "Strogg", "ui_name": "client",
                   "si_gameType": scenario["gametype"]})

    def command(cvars: dict, tail: list) -> list:
        result = [str(exe)]
        for key, value in cvars.items():
            result += ["+set", key, value]
        assert len(cvars) + 1 <= 64, "engine startup command capacity exceeded"
        return result + tail

    started = time.time()
    failures: list = []
    processes, streams, codes = {}, [], {}
    try:
        for role, line in (("host", command(host, ["+spawnServer", scenario["map"]])),
                           ("client", command(client, ["+connect", f"127.0.0.1:{args.port}"]))):
            if role == "client":
                # once the host's map is up and its script has started
                deadline = time.time() + 150 + args.extra_timeout
                while time.time() < deadline and "addbot" not in (logs["host"].read_text(encoding="utf-8", errors="replace")
                                                                 if logs["host"].is_file() else ""):
                    if processes["host"].poll() is not None:
                        break
                    time.sleep(1.0)
            (output / f"{role}-launch.json").write_text(json.dumps(line, indent=2), encoding="utf-8")
            stream = (output / f"{role}-console.log").open("w", encoding="utf-8")
            streams.append(stream)
            processes[role] = subprocess.Popen(line, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT)
        deadline = started + scenario["timeout"] * args.time_scale + args.extra_timeout
        stage, hog_step = 0, None
        while time.time() < deadline and any(process.poll() is None for process in processes.values()):
            time.sleep(1.0)
            seen = logs["client"].read_text(encoding="utf-8", errors="replace") if logs["client"].is_file() else ""
            if stage == 0 and "execing mp_modifiers_smoke.cfg" in seen:
                # the client's map is up: the host moves on to the script it idles on from here
                write_script(saves["host"] / "baseoq4/mp_modifiers_joined.cfg", ["waitMsec 500", "exec mp_modifiers_joined.cfg"])
                write_script(saves["host"] / "baseoq4/mp_modifiers_idle.cfg", ["exec mp_modifiers_joined.cfg"])
                stage = 1
            if stage == 1 and scenario["joined"] == "quad":
                served = logs["host"].read_text(encoding="utf-8", errors="replace") if logs["host"].is_file() else ""
                quad = served.count("Quad Hog: a quad spawned")
                step = [] if "took the quad" in served or quad == 0 else [f"teleport openq4_quad_hog_{quad}"]
                if step != hog_step:
                    hog_step = step
                    write_script(saves["host"] / "baseoq4/mp_modifiers_joined.cfg", [*step, "waitMsec 1000", "exec mp_modifiers_joined.cfg"])
            if stage == 1 and "MP_MOD_DONE_CLIENT" in seen:
                # the client has reported: the host reports the same moment's world and ends the run
                write_script(saves["host"] / "baseoq4/mp_modifiers_joined.cfg", [*checkpoint("host", True), "echo MP_MOD_DONE_HOST", "quit"])
                stage = 2
            elif stage == 0 and processes.get("client") is not None and processes["client"].poll() is not None:
                break	# the client left before its map was up
    finally:
        for role, process in processes.items():
            if process.poll() is None:
                process.kill()  # Only the processes launched by this test.
                process.wait()
                codes[role] = -1
            else:
                codes[role] = process.returncode
        for stream in streams:
            stream.close()
    text = {role: (path.read_text(encoding="utf-8", errors="replace") if path.is_file() else "") for role, path in logs.items()}
    summary: dict = {}
    for role in ("host", "client"):
        if f"MP_MOD_DONE_{role.upper()}" not in text[role]:
            failures.append(f"the {role}'s script did not finish (exit code {codes.get(role)})")
        for marker in ("ERROR:", "FATAL", "entity type mismatch", "Data not in sync"):
            if marker in text[role]:
                failures.append(f"the {role}'s log reports {marker!r}")
    if not failures:
        try:
            ours, theirs = block(text["host"], "host", True), block(text["client"], "client", True)
            served, joined = rules(ours), rules(theirs)
            for key in ("modifiers", "fixedWeapon", "title", "selfDamage", "knockback", "fallDamage"):
                expect(served[key] == joined[key], f"{key}: the host has {served[key]!r}, the client {joined[key]!r}", failures)
            host_pickups, client_pickups = pickups(ours), pickups(theirs)
            for key in ("weapon", "ammo", "health", "armor"):
                expect(host_pickups[key] == client_pickups[key], f"{key} pickups: host {host_pickups[key]}, client {client_pickups[key]}", failures)
            summary = {"modifiers": joined["modifiers"], "title": joined["title"],
                       "pickups": {key: value for key, value in client_pickups.items() if key != "names"}}
            carried = loadouts(theirs)
            if name == "remote-arena":
                expect(served["modifiers"] == "vampiric arena:rocketlauncher nopowerups", f"the host latched {served['modifiers']!r}", failures)
                expect(client_pickups["weapon"] == 0 and client_pickups["ammo"] == 0 and client_pickups["health"] == 0
                       and client_pickups["powerup"] == 0 and client_pickups["armor"] > 0, f"the client populated {client_pickups}", failures)
                expect(bool(carried) and all(bin(loadout["weapons"]).count("1") == 2 for loadout in carried.values()),
                       f"the client sees loadouts {carried}", failures)
                expect(any(loadout["ammo"] == -1 for loadout in carried.values()), "the client sees no unlimited arena weapon", failures)
                summary["loadouts"] = {slot: hex(loadout["weapons"]) for slot, loadout in carried.items()}
            else:
                expect(served["modifiers"] == "quadhog" and served["quadHog"] == "0", f"the host's hog: {served}", failures)
                expect(carried.get(0, {}).get("powerups", 0) & 1 == 1, f"the client does not see the hog's quad: {carried.get(0)}", failures)
                expect(client_pickups["powerup"] <= 1 and all(n.startswith("powerup_quad_damage") for n in client_pickups["names"]),
                       f"the client's powerups: {client_pickups['names']}", failures)
                summary["hog_powerups"] = hex(carried.get(0, {}).get("powerups", 0))
        except AssertionError as error:
            failures.append(str(error))
    report = {"scenario": name, "render_api": args.render_api, "exit_codes": codes, "seconds": round(time.time() - started, 1),
              "summary": summary, "failures": failures}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report


def run(name: str, args) -> dict:
    scenario = SCENARIOS[name]
    runtime, output = args.runtime_dir.resolve(), (args.output_dir / name).resolve()
    suffix = ".exe" if os.name == "nt" else ""
    exe = runtime / (args.executable_name or f"openQ4-client_x64{suffix}")
    savepath = output / "save"
    gamepath = savepath / "baseoq4"
    gamepath.mkdir(parents=True, exist_ok=True)
    (gamepath / "mp_modifiers_smoke.cfg").write_text(
        "\n".join([*paced(scenario["script"], args.time_scale), "echo MP_MOD_DONE", "quit"]) + "\n", encoding="utf-8")
    for relative, text in scenario.get("files", {}).items():
        (gamepath / relative).parent.mkdir(parents=True, exist_ok=True)
        (gamepath / relative).write_text(text, encoding="utf-8")
    cvars = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(savepath),
        "fs_devpath": str(savepath), "fs_game": "baseoq4", "com_gameMode": "MP",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
        "r_borderlessDefaultMigrated": "1", "r_mode": "-1", "r_customWidth": "960",
        "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540",
        "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_renderApi": args.render_api, "com_maxfps": "60", "r_swapInterval": "0",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
        "net_port": str(args.port), "net_ip": "127.0.0.1", "net_enableIPv6": "0",
        "net_serverDedicated": "0", "net_LANServer": "1",
        "si_pure": "0", "si_maxPlayers": "8", "si_gameType": scenario["gametype"],
        "si_warmup": "0", "si_useReady": "0", "si_minPlayers": "1", "si_countDown": "4",
        "si_timeLimit": "0", "si_fragLimit": "999", "si_captureLimit": "999", "bot_minPlayers": "0",
        "g_matchProfile": "casual", "ui_autoJoin": "1", "ui_spectate": "Play", "com_skipLoadingContinue": "1",
        "si_autoBalance": "1", "ui_team": "Marine", "net_allowCheats": "1", "g_debugDamage": "1",
        # every run starts from the stock composition, whatever the last one archived
        "si_ruleset": "0", "si_instagib": "0", "si_quadHog": "0", "si_vampiric": "0", "si_weaponsFrenzy": "0",
        "si_weaponArena": "none", "si_powerups": "1", "si_selfDamage": "100", "si_knockback": "100", "si_fallDamage": "100",
        "si_weaponRespawn": "0", "si_preset": "",
        "g_autoExecAfterMapLoad": "mp_modifiers_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
    }
    cvars.update(scenario["cvars"])
    if args.read_only:
        # judge the log a finished run left, without launching anything
        log = (gamepath / "logs/openq4.log").read_text(encoding="utf-8", errors="replace")
        failures, summary = [], {}
        try:
            summary = CHECKS[name](log, failures)
        except AssertionError as error:
            failures.append(str(error))
        return {"scenario": name, "render_api": args.render_api, "read_only": True, "summary": summary, "failures": failures}
    command = [str(exe)]
    for key, value in cvars.items():
        command += ["+set", key, value]
    command += ["+spawnServer", scenario["map"]]
    assert len(cvars) + 1 <= 64, "engine startup command capacity exceeded"
    (output / "launch.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    started = time.time()
    with (output / "console.log").open("w", encoding="utf-8") as console:
        process = subprocess.Popen(command, cwd=runtime, stdout=console, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=scenario["timeout"] * args.time_scale + args.extra_timeout)
        except subprocess.TimeoutExpired:
            process.kill()  # Only the process launched by this test.
            process.wait()
            code = -1
    log_path = gamepath / "logs/openq4.log"
    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    failures: list = []
    summary: dict = {}
    if not log_path.is_file() or log_path.stat().st_mtime < started:
        failures.append("missing fresh engine log")
    elif "MP_MOD_DONE" not in log:
        failures.append(f"the script did not finish (exit code {code})")
    for marker in ("ERROR:", "FATAL", "entity type mismatch"):
        if marker in log:
            failures.append(f"the log reports {marker!r}")
    if not failures:
        try:
            summary = CHECKS[name](log, failures)
        except AssertionError as error:
            failures.append(str(error))
    report = {"scenario": name, "render_api": args.render_api, "exit_code": code, "seconds": round(time.time() - started, 1),
              "summary": summary, "failures": failures}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", help="runtime executable basename for an isolated test copy")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-modifiers-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--render-api", choices=("gl", "vk"), default="gl")
    parser.add_argument("--port", type=int, default=0, help="server port; 0 picks a free loopback port for each scenario")
    parser.add_argument("--extra-timeout", type=int, default=0, help="seconds added to each scenario's timeout on a loaded machine")
    parser.add_argument("--time-scale", type=float, default=1.0,
                        help="stretch every scripted wait and timeout; a loaded machine runs the hidden game slower than the clock")
    parser.add_argument("--read-only", action="store_true", help="re-read the logs of the last run of each single-process scenario")
    parser.add_argument("--scenario", action="append", choices=(*SCENARIOS, *REMOTE), help="run only these; default all")
    args = parser.parse_args()
    runtime = args.runtime_dir.resolve()
    verify_runtime_is_current(runtime, getattr(args, "executable_name", "") or "")
    suffix = ".exe" if os.name == "nt" else ""
    if not (runtime / (args.executable_name or f"openQ4-client_x64{suffix}")).is_file() or not args.basepath.is_dir():
        parser.error("the runtime executable and installed Quake 4 assets are required")
    failed = False
    chosen = args.port
    for name in args.scenario or [*SCENARIOS, *REMOTE]:
        args.port = chosen or free_port()
        report = run_remote(name, args) if name in REMOTE else run(name, args)
        print(json.dumps(report, indent=2))
        failed = failed or bool(report["failures"])
    print("mp_modifiers_smoke: FAIL" if failed else "mp_modifiers_smoke: PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
