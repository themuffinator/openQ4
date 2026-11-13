#!/usr/bin/env python3
"""Qualify a networked player's own weapon visuals while it turns and fires, without input.

A listen host on mp/q4dm1 holds the match in warmup (`si_minPlayers` above the
player count), so every player carries the map's weapons, and adds three bots,
held still with `bot_pause`. A second process joins over loopback as the
shooter. No operating-system input is used: the shooter selects weapons with
the `testWeaponWheel` developer command, turns with `in_presentationViewProbe`
(synthetic mouse counts inside the usercmd generator) and fires with
`in_presentationViewProbeAttack`; the host is placed with `setviewpos`.

The steps follow checklist MP-07 in docs/dev/mp-human-qualification.md (the
target steps run first, while the shooter faces into the room from its spawn
point):

- sweep (7.1): machinegun, railgun, shotgun and lightning gun, each fired for
  four seconds while turning at a steady rate;
- target (7.2, and impacts on a body): the harness reads the shooter's
  position and stands the host in front of it, still in the line of fire for
  the railgun and the machinegun (the step that counts body hits: a rail
  passes through bodies and reports the wall behind) and still inside the
  gauntlet's reach; the shooter holds still and fires. A moving target (the
  rest of 7.2) stays with the by-eye checklist;
- respawn (7.4): the shooter fires the machinegun while turning, kills
  itself, and keeps firing through the respawn. The client's weapon for the
  new life raises from the frame the server's began its raise, so the burst
  after the respawn is held to same-frame matching like every other step. The
  report also gives each new life's raise on both sides and where the first
  shots after the death started;
- rejoin: the shooter spectates, joins again (new weapon entities, as on any
  join from spectating) and fires the machinegun while turning; its weapon
  for the new life must raise with the server's too.

The harness feeds the host a control file it executes every 100 ms, which is
how the bots are paused and the host is placed and moved on cue. Both
processes log every hit-scan shot (`net_mpHitscanDebug`): the server's shots,
the shots the client predicted, and the server's copies the client received,
with how far each started from the gun and aim the client held at that moment
and what it hit. The report pairs server shots with the client's predictions
by game time, compares directions and hits, counts the server's copies drawn
in full (a round trip late) and those that only added the confirmed impacts on
a body, and summarises the gaps per step. Engine captures of every step are
kept for review by eye.

With `--predict-hitscan 0` the client keeps the old behaviour (every shot drawn
from the server's copy), the baseline the gaps are measured against.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import socket
import statistics
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
# player_marine_mp def_weaponN slots.
SLOTS = {"gauntlet": 0, "machinegun": 1, "shotgun": 2, "railgun": 7, "lightninggun": 8}
SWEEPS = ("machinegun", "railgun", "shotgun", "lightninggun")
HITSCAN_SWEEPS = ("machinegun", "railgun", "shotgun")
# How far in front of the shooter the host stands, still. The rail passes through
# bodies and reports the wall behind, so the machinegun is the step that counts hits on
# the body. The gauntlet target stands inside the 50 units of multiplayer reach: at 36
# units the players' boxes overlap and push the host aside, and a strafe steered through
# the 100 ms control file drifts out of reach, so a moving target stays with the by-eye
# checklist.
TARGETS = (("railgun", 96), ("machinegun", 96), ("gauntlet", 44))
BODY_STEP = "target_machinegun"
MEASURED_STEPS = (*HITSCAN_SWEEPS, "target_railgun", BODY_STEP, "respawn", "rejoin")
BURST = 12
INTERVAL_MSEC = 150
FIRE_MSEC = 4000
# sensitivity 5 and m_yaw 0.022 make one count 0.11 degrees: 2000 counts a second turn 220 degrees a second.
SENSITIVITY = 5
M_YAW = 0.022
LINE = re.compile(r"MP_HITSCAN kind=(\w+) client=(\d) owner=(-?\d+) local=(\d) def=(-?\d+) time=(-?\d+) "
                  r"newFrame=(\d) seed=(-?\d+) fx=(\S+) dir=(\S+) gunGap=(\S+) aimGap=(\S+) hit=(-?\d+) actor=(\d)")
WEAPON = re.compile(r"MP_WEAPON (\w+) client=(\d) owner=(-?\d+) local=(\d) time=(-?\d+)(?:.* start=(-?\d+) done=(-?\d+))?")
VIEWPOS = re.compile(r"\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) (-?[\d.]+)")


def parse(log: str) -> list[dict]:
    shots, segment = [], None
    for line in log.splitlines():
        marker = re.search(r"FIRING_VIEW_SEGMENT(_END)? (\w+)", line)
        if marker:
            segment = None if marker.group(1) else marker.group(2)
            continue
        match = LINE.search(line)
        if not match:
            continue
        kind, client, owner, local, definition, game_time, _, seed, fx, direction, gun, aim, hit, actor = match.groups()
        shots.append({"kind": kind, "client": int(client), "owner": int(owner), "local": int(local),
                      "def": int(definition), "time": int(game_time), "seed": int(seed),
                      "fx": [float(v) for v in fx.split(",")], "dir": [float(v) for v in direction.split(",")],
                      "gunGap": float(gun), "aimGap": float(aim), "hit": int(hit), "actor": int(actor),
                      "segment": segment})
    return shots


def weapon_events(log: str) -> list[dict]:
    """The MP_WEAPON lines in log order: new lives (respawn, entities) and raises."""
    events = []
    for match in WEAPON.finditer(log):
        event, client, owner, local, game_time, start, done = match.groups()
        events.append({"event": event, "client": int(client), "owner": int(owner), "local": int(local),
                       "time": int(game_time), "start": int(start) if start else None,
                       "done": int(done) if done else None})
    return events


def new_life_raises(host_events: list[dict], client_events: list[dict], slot: int, scripted_from: int | None) -> list[dict]:
    """Pair the raise of each of the shooter's new lives on the server with the client's.

    The server raises the weapon for a life on the frame it makes it, right after the
    respawn line. The client's raise for it starts from that frame, so its start and end
    equal the server's; a client that caught its weapon up to ready logs no raise at all.
    Lives from scripted_from on (the respawn and the rejoin) are judged. The first join is
    reported only: the shooter turns net_mpHitscanDebug on from its script after the map
    loads, possibly after that join, and it can land in the gaps of a connection starting.
    """
    pairs = []
    for index, event in enumerate(host_events):
        if event["event"] != "respawn" or event["owner"] != slot:
            continue
        server = next((e for e in host_events[index + 1:] if e["event"] == "raise" and e["owner"] == slot), None)
        if server is None:
            continue
        client = min((e for e in client_events if e["event"] == "raise" and e["local"] and
                      abs(e["start"] - server["start"]) <= 500),
                     key=lambda e: abs(e["start"] - server["start"]), default=None)
        pairs.append({"respawn_time": event["time"],
                      "judged": scripted_from is not None and event["time"] >= scripted_from,
                      "server_start": server["start"], "server_done": server["done"],
                      "client_start": client["start"] if client else None,
                      "client_done": client["done"] if client else None,
                      "client_raised_at": client["time"] if client else None})
    return pairs


def angle(a: list[float], b: list[float]) -> float:
    # atan2 of the cross and dot products: acos of a dot product near 1 turns the log's
    # five-decimal rounding into a quarter of a degree.
    cross = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
    return math.degrees(math.atan2(math.sqrt(sum(c * c for c in cross)), sum(x * y for x, y in zip(a, b))))


def summary(values: list[float]) -> dict:
    if not values:
        return {"count": 0}
    ordered = sorted(values)
    return {"count": len(ordered), "median": round(statistics.median(ordered), 3),
            "p95": round(ordered[min(len(ordered) - 1, int(0.95 * len(ordered)))], 3), "max": round(ordered[-1], 3)}


def burst(lines: list[str], shots: list[str], name: str, count: int = BURST) -> None:
    for index in range(count):
        shot = f"{name}_{index:02d}"
        shots.append(shot)
        lines += [f"waitMsec {INTERVAL_MSEC}", f"screenshot screenshots/{shot}.tga"]


def select(weapon: str) -> list[str]:
    return ["testWeaponWheel open", f"testWeaponWheel slot {SLOTS[weapon]}", "waitMsec 100",
            "testWeaponWheel release", "waitMsec 1600"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--port", type=int, default=28968)
    parser.add_argument("--predict-hitscan", type=int, choices=(0, 1), default=1,
                        help="the shooter's net_mpPredictHitscan; 0 is the old behaviour")
    parser.add_argument("--enhanced", action="store_true",
                        help="net_mpPredictMode 1 on the shooter and net_mpLagCompensation 1 on the host")
    parser.add_argument("--latency", type=int, default=0, help="forced one-way latency on the shooter, ms")
    parser.add_argument("--jitter", type=int, default=0, help="forced latency jitter on the shooter, ms")
    parser.add_argument("--turn-rate", type=float, default=220.0, help="degrees per second while firing")
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    verify_runtime_is_current(runtime)
    # A second run on the same ports would start a host that cannot bind, and its shooter
    # could join the other run's host: refuse instead of measuring a mix of the two.
    for port in (args.port, args.port + 1):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            try:
                probe.bind(("127.0.0.1", port))
            except OSError:
                parser.error(f"UDP port {port} is in use: another run is active, or pass --port")
    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    if output.exists() or not exe.is_file() or not (args.basepath / "q4base").is_dir():
        parser.error("use a new output directory, a staged client and installed retail assets")
    output.mkdir(parents=True)
    paths = [exe, *sorted((runtime / "baseoq4").glob("game-mp*")), *sorted(runtime.glob("renderer-*")),
             runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4"]
    identity = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                for p in paths if p.is_file() and p.suffix not in (".pdb", ".lib", ".exp", ".map")}
    games = {role: output / role / "baseoq4" for role in ("host", "shooter")}
    for game in games.values():
        game.mkdir(parents=True)
    control = games["host"] / "firing_control.cfg"

    def steer(*lines: str) -> None:
        write_script(control, ["bot_pause 1", *lines])

    steer()
    counts = args.turn_rate / (SENSITIVITY * M_YAW)
    # Game-module cvars are set from the scripts: a command-line +set is lost when
    # the module registers them. god keeps the host standing as a target.
    host = ['set g_autoExecAfterMapLoad ""', "set net_mpHitscanDebug 1",
            f"set net_mpLagCompensation {int(args.enhanced)}", "god", "notarget",
            *[f"addbot {name} 3" for name in ("Sledge", "Gorge", "Kane")],
            "echo FIRING_VIEW_HOST_READY", "exec firing_loop.cfg"]
    # The loop re-executes itself instead of being unrolled: the console's command buffer
    # cannot hold a few minutes of unrolled steps. The harness stops the host at the end.
    write_script(games["host"] / "firing_loop.cfg", ["waitMsec 100", "exec firing_control.cfg", "exec firing_loop.cfg"])
    shooter = ['set g_autoExecAfterMapLoad ""', "set net_mpHitscanDebug 1",
               f"set net_mpPredictHitscan {args.predict_hitscan}", f"set net_mpPredictMode {int(args.enhanced)}",
               f"set sensitivity {SENSITIVITY}", f"set m_yaw {M_YAW}", "set ui_retained 1",
               "waitMsec 8000", "openq4_reportMPState", "echo FIRING_VIEW_JOINED"]
    shots = []
    hold = max(0, FIRE_MSEC - 300 - BURST * INTERVAL_MSEC)
    # The target steps come first, while the shooter still faces the way its spawn point
    # does, into the room; after the sweeps it can face a wall at any range.
    for weapon, distance in TARGETS:
        # getviewpos prints the eye and yaw the harness places the host from.
        shooter += [*select(weapon), "getviewpos", f"echo FIRING_VIEW_TARGET {weapon} {distance}", "waitMsec 1200",
                    f"echo FIRING_VIEW_SEGMENT target_{weapon}", "in_presentationViewProbeAttack 1", "waitMsec 300"]
        burst(shooter, shots, f"target_{weapon}")
        shooter += [f"waitMsec {hold}", "in_presentationViewProbeAttack 0", "waitMsec 1200",
                    f"echo FIRING_VIEW_SEGMENT_END target_{weapon}", "echo FIRING_VIEW_TARGET_END"]
    for weapon in SWEEPS:
        shooter += [*select(weapon), f"echo FIRING_VIEW_SEGMENT {weapon}",
                    f"in_presentationViewProbe {counts:g}", "in_presentationViewProbeAttack 1", "waitMsec 300"]
        burst(shooter, shots, weapon)
        shooter += [f"waitMsec {hold}", "in_presentationViewProbeAttack 0", "in_presentationViewProbe 0",
                    "waitMsec 1200", f"echo FIRING_VIEW_SEGMENT_END {weapon}"]
    shooter += [*select("machinegun"), "echo FIRING_VIEW_SEGMENT respawn",
                f"in_presentationViewProbe {counts:g}", "in_presentationViewProbeAttack 1", "waitMsec 300"]
    burst(shooter, shots, "respawn_before", 4)
    # The held attack respawns the shooter once the death camera's minimum time has passed.
    shooter += ["echo FIRING_VIEW_RESPAWN", "kill", "waitMsec 3300"]
    burst(shooter, shots, "respawn_after", 16)
    shooter += ["in_presentationViewProbeAttack 0", "in_presentationViewProbe 0", "waitMsec 1200",
                "echo FIRING_VIEW_SEGMENT_END respawn"]
    # Spectating removes the weapon entities, so joining again makes new ones. Fire starts
    # once the join has gone through: a held attack while spectating cycles the camera. The
    # client refuses a second ui_spectate change within 8 seconds (ThrottleUserInfo).
    shooter += ["set ui_spectate Spectate", "waitMsec 9000", "echo FIRING_VIEW_SEGMENT rejoin", "set ui_spectate Play",
                "waitMsec 400", f"in_presentationViewProbe {counts:g}", "in_presentationViewProbeAttack 1"]
    burst(shooter, shots, "rejoin", 16)
    shooter += ["in_presentationViewProbeAttack 0", "in_presentationViewProbe 0", "waitMsec 1200",
                "echo FIRING_VIEW_SEGMENT_END rejoin", "openq4_reportMPState", "echo FIRING_VIEW_COMPLETE",
                "waitMsec 1000", "disconnect", "quit"]
    write_script(games["host"] / "firing_view.cfg", host)
    write_script(games["shooter"] / "firing_view.cfg", shooter)

    processes, streams = {}, []

    def launch(role):
        game = games[role]
        cvars = {"win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
                 "sys_consoleWindow": "0", "in_tty": "0", "in_mouse": "0", "in_joystick": "0",
                 "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
                 "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_renderApi": args.renderer,
                 "r_mode": "-1", "r_customWidth": "960", "r_customHeight": "540", "r_windowWidth": "960",
                 "r_windowHeight": "540", "r_swapInterval": "0", "com_maxfps": "60", "s_noSound": "1",
                 "ui_autoJoin": "1", "ui_spectate": "Play", "ui_retained": "1",
                 "ui_name": "FiringHost" if role == "host" else "Shooter", "fs_game": "baseoq4",
                 "com_gameMode": "MP", "com_nextGameModule": "game_mp",
                 "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(game.parent),
                 "fs_devpath": str(game.parent), "logFile": "2", "logFileName": "logs/openq4.log",
                 "developer": "1", "net_allowCheats": "1", "net_serverDedicated": "0", "net_LANServer": "1",
                 "net_port": str(args.port if role == "host" else args.port + 1), "net_ip": "127.0.0.1",
                 "net_enableIPv6": "0", "si_pure": "0", "si_gameType": "DM", "si_maxPlayers": "8",
                 # Warmup that never ends: the match waits for 16 players, so the
                 # warmup arsenal (si_warmupWeapons) stays in every hand.
                 "si_warmup": "1", "si_minPlayers": "16", "si_warmupWeapons": "1", "si_useReady": "0", "si_spectators": "1",
                 "si_fragLimit": "999", "si_timeLimit": "0", "g_matchProfile": "casual", "bot_minPlayers": "0",
                 "bot_chat": "0", "com_skipLoadingContinue": "1", "g_autoExecAfterMapLoad": "firing_view.cfg",
                 "g_autoExecAfterMapLoadDelayMs": "500"}
        if role == "shooter" and (args.latency or args.jitter):
            cvars.update(net_forceLatency=str(args.latency), net_forceJitter=str(args.jitter), net_forceSeed="40708")
        command = [str(exe), *[part for key, value in cvars.items() for part in ("+set", key, value)]]
        command += ["+spawnServer", "mp/q4dm1"] if role == "host" else ["+connect", f"127.0.0.1:{args.port}"]
        (output / f"{role}-launch.json").write_text(json.dumps(command, indent=2))
        stream = (output / f"{role}-console.log").open("w")
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

    failures, placements = [], []
    shooter_log = games["shooter"] / "logs/openq4.log"
    try:
        launch("host")
        until = time.monotonic() + 90
        while "FIRING_VIEW_HOST_READY" not in read_log(games["host"] / "logs/openq4.log"):
            if time.monotonic() > until or processes["host"].poll() is not None:
                raise RuntimeError("host did not start the bot match")
            time.sleep(0.25)
        launch("shooter")
        until, handled, placed = time.monotonic() + 300, 0, False
        while processes["shooter"].poll() is None:
            if time.monotonic() > until:
                failures.append("shooter timed out")
                break
            text = read_log(shooter_log)
            targets = list(re.finditer(r"FIRING_VIEW_TARGET (\w+) (\d+)", text))
            if len(targets) > handled:
                marker = targets[handled]
                handled += 1
                eye = list(VIEWPOS.finditer(text[:marker.start()]))
                if eye:
                    x, y, z, yaw = (float(v) for v in eye[-1].groups())
                    distance = float(marker.group(2))
                    spot = (x + distance * math.cos(math.radians(yaw)), y + distance * math.sin(math.radians(yaw)), z)
                    placements.append({"weapon": marker.group(1), "shooter_eye": [x, y, z], "shooter_yaw": yaw,
                                       "host_eye": [round(v, 1) for v in spot]})
                    # The host re-runs this every 100 ms, which also holds it on the spot.
                    steer(f"setviewpos {spot[0]:.1f} {spot[1]:.1f} {spot[2]:.1f} {yaw + 180:.1f}")
                    placed = True
                else:
                    failures.append(f"no getviewpos before the {marker.group(1)} target step")
            if placed and text.count("FIRING_VIEW_TARGET_END") >= handled:
                steer()
                placed = False
            time.sleep(0.1)
        if processes["shooter"].poll() not in (None, 0):
            failures.append("shooter exited abnormally")
    except (OSError, RuntimeError) as error:
        failures.append(str(error))
    finally:
        for process in processes.values():
            if process.poll() is None:
                process.kill()
                process.wait()
        for stream in streams:
            stream.close()

    strip = lambda text: re.sub(r"\^[0-9A-Za-z]", "", text)
    host_log = strip(read_log(games["host"] / "logs/openq4.log"))
    log = strip(read_log(shooter_log))
    consoles = "".join(read_log(output / f"{role}-console.log") for role in games)
    if re.search(r"out of memory", consoles + log + host_log, re.IGNORECASE):
        failures.append("a client ran out of memory: environmental on a shared machine, rerun with more free commit")
    if "FIRING_VIEW_COMPLETE" not in log or re.search(r"ERROR:|FATAL:|Unknown command", log):
        failures.append("shooter did not complete cleanly")
    if re.search(r"ERROR:|FATAL:", host_log):
        failures.append("host reported an error")
    if not re.search(rf"Renderer API: {args.renderer} \(", log):
        failures.append("the requested renderer did not activate")
    missing = [s for s in shots if not (games["shooter"] / f"screenshots/{s}.tga").is_file()]
    if missing:
        failures.append(f"missing captures: {missing[:5]}")
    respawns = log.partition("FIRING_VIEW_RESPAWN")[2]
    if not re.search(r"Shooter was killed by Shooter|Shooter killed", respawns):
        failures.append("the shooter did not die in the respawn step")

    client_shots = parse(log)
    local = [s for s in client_shots if s["local"]]
    slot = local[0]["owner"] if local else None
    host_shots = parse(host_log)
    server = [s for s in host_shots if s["kind"] == "server" and s["owner"] == slot]
    predicted = [s for s in local if s["kind"] == "predicted"]
    copies = [s for s in local if s["kind"] in ("echo", "confirmed")]
    by_time = {(s["time"], s["def"]): s for s in predicted}
    # The host log has no step markers: a server shot belongs to the step of the client
    # shot logged nearest to it in game time (its prediction, or its copy).
    for shot in server:
        nearest = min(local, key=lambda s: abs(s["time"] - shot["time"]), default=None)
        shot["segment"] = nearest["segment"] if nearest else None
    matched = [(s, by_time[(s["time"], s["def"])]) for s in server if (s["time"], s["def"]) in by_time]
    server_times = {(s["time"], s["def"]) for s in server}

    def step(name: str) -> dict:
        mine = [s for s in local if s["segment"] == name]
        theirs = [s for s in server if s["segment"] == name]
        pairs = [(a, by_time[(a["time"], a["def"])]) for a in theirs if (a["time"], a["def"]) in by_time]
        full = [s for s in mine if s["kind"] == "echo"]
        result = {
            "defs": sorted({s["def"] for s in mine}),
            "server_shots": len(theirs),
            "predicted": sum(s["kind"] == "predicted" for s in mine),
            "server_shots_predicted_same_frame": len(pairs),
            "predictions_without_server_shot": sum(s["kind"] == "predicted" and (s["time"], s["def"]) not in server_times
                                                   for s in mine),
            "confirmed_copies": sum(s["kind"] == "confirmed" for s in mine),
            "full_copies": len(full),
            "full_copy_gun_gap": summary([s["gunGap"] for s in full]),
            "full_copy_aim_gap_deg": summary([s["aimGap"] for s in full]),
            "copy_aim_gap_deg": summary([s["aimGap"] for s in mine if s["kind"] in ("echo", "confirmed")]),
            "predicted_gun_gap": summary([s["gunGap"] for s in mine if s["kind"] == "predicted"]),
            "predicted_aim_gap_deg": summary([s["aimGap"] for s in mine if s["kind"] == "predicted"]),
            "predicted_body_hits": sum(s["actor"] for s in mine if s["kind"] == "predicted"),
            "server_body_hits": sum(s["actor"] for s in theirs),
            "confirmed_body_impacts": sum(s["actor"] for s in mine if s["kind"] == "confirmed"),
            "hit_agreement": f"{sum(a['actor'] == b['actor'] for a, b in pairs)}/{len(pairs)}",
            "predicted_vs_server_direction_deg": summary([angle(b["dir"], a["dir"]) for a, b in pairs]),
        }
        own = sorted((s for s in mine if s["kind"] == "predicted"), key=lambda s: s["time"])
        if name == "respawn" and own:
            # How far each server shot the client did not predict on its own frame was from
            # the nearest prediction, and where the first shots after the death started
            # relative to the last shot before it (the death is the longest gap).
            offsets = [min(abs(p["time"] - a["time"]) for p in own if p["def"] == a["def"])
                       for a in theirs if (a["time"], a["def"]) not in by_time and any(p["def"] == a["def"] for p in own)]
            result["unpredicted_server_shot_offset_ms"] = summary(offsets)
            gaps = [(own[i + 1]["time"] - own[i]["time"], i) for i in range(len(own) - 1)]
            if gaps:
                _, death = max(gaps)
                result["first_shot_after_death_from_death_spot"] = round(math.dist(own[death]["fx"], own[death + 1]["fx"]), 1)
        return result

    steps = {name: step(name) for name in MEASURED_STEPS}
    # The scripted lives follow the shooter's first logged shot (its first target step).
    scripted_from = min((s["time"] for s in local), default=None)
    lives = new_life_raises(weapon_events(host_log), weapon_events(log), slot, scripted_from) if slot is not None else []
    judged = [life for life in lives if life["judged"]]
    # A held burst keeps predicting shots after the server has killed the shooter until the
    # client hears of the death, a round trip on; only predictions after the respawn count
    # against it.
    respawn_time = judged[0]["respawn_time"] if judged else None
    extras = [s for s in predicted if s["segment"] == "respawn" and (s["time"], s["def"]) not in server_times]
    steps["respawn"]["predictions_at_death_without_server_shot"] = sum(
        respawn_time is None or s["time"] < respawn_time for s in extras)
    steps["respawn"]["predictions_after_respawn_without_server_shot"] = sum(
        respawn_time is not None and s["time"] >= respawn_time for s in extras)
    outside = [s for s in server if s["segment"] != "respawn"]
    outside_matched = sum((s["time"], s["def"]) in by_time for s in outside)
    metrics = {
        "shooter_slot": slot,
        "server_shots": len(server),
        "predicted_shots": len(predicted),
        "server_shots_predicted_same_frame": len(matched),
        "server_shots_outside_respawn": len(outside),
        "server_shots_outside_respawn_predicted_same_frame": outside_matched,
        "predictions_without_server_shot": sum((s["time"], s["def"]) not in server_times for s in predicted),
        "predicted_vs_server_direction_deg": summary([angle(a["dir"], b["dir"]) for a, b in matched]),
        "copies_confirmed": sum(s["kind"] == "confirmed" for s in copies),
        "copies_drawn_in_full": sum(s["kind"] == "echo" for s in copies),
        "full_copy_gun_gap": summary([s["gunGap"] for s in copies if s["kind"] == "echo"]),
        "full_copy_aim_gap_deg": summary([s["aimGap"] for s in copies if s["kind"] == "echo"]),
        "copy_aim_gap_deg": summary([s["aimGap"] for s in copies]),
        "steps": steps,
        "new_life_raises": lives,
        "host_placements": placements,
        "gauntlet_target_captures": sum(1 for s in shots if s.startswith("target_gauntlet")),
    }
    if slot is None or not server:
        failures.append("no hit-scan shots from the shooter were logged")
    for name in MEASURED_STEPS:
        if not steps[name]["confirmed_copies"] + steps[name]["full_copies"]:
            failures.append(f"no server copies of shots in the {name} step arrived")
        if args.predict_hitscan and not steps[name]["predicted"]:
            failures.append(f"no predicted shots in the {name} step")
    # The weapon for each scripted new life raises with the server's, prediction on or off.
    if len(judged) < 2:
        failures.append(f"expected the respawn and the rejoin among the shooter's new lives, found {len(judged)}")
    for life in judged:
        if life["client_start"] is None:
            failures.append(f"the client caught its weapon for the life at {life['respawn_time']} up to ready "
                            "instead of raising it")
        elif (life["client_start"], life["client_done"]) != (life["server_start"], life["server_done"]):
            failures.append(f"the client raised its weapon for the life at {life['respawn_time']} over "
                            f"{life['client_start']}-{life['client_done']}, the server over "
                            f"{life['server_start']}-{life['server_done']}")
    if len(placements) != len(TARGETS):
        failures.append("the host was not placed for every target step")
    if not steps[BODY_STEP]["server_body_hits"]:
        failures.append(f"no shot in the {BODY_STEP} step hit the host, so no impact on a body was drawn")
    if args.predict_hitscan and server:
        if len(matched) < 0.9 * len(server):
            failures.append(f"only {len(matched)} of {len(server)} server shots were predicted on the same frame")
        # The burst held through the respawn is held to the same frames as the server.
        respawn = steps["respawn"]
        if respawn["server_shots_predicted_same_frame"] < 0.9 * respawn["server_shots"]:
            failures.append(f"only {respawn['server_shots_predicted_same_frame']} of {respawn['server_shots']} server "
                            "shots around the respawn were predicted on the same frame")
        if respawn["predictions_after_respawn_without_server_shot"] > 1:
            failures.append(f"the client predicted {respawn['predictions_after_respawn_without_server_shot']} shots "
                            "after the respawn that the server never fired")
        rejoin = steps["rejoin"]
        if rejoin["server_shots_predicted_same_frame"] < 0.9 * rejoin["server_shots"]:
            failures.append(f"only {rejoin['server_shots_predicted_same_frame']} of {rejoin['server_shots']} server "
                            "shots after the rejoin were predicted on the same frame")
        if rejoin["predictions_without_server_shot"] > 1:
            failures.append(f"the client predicted {rejoin['predictions_without_server_shot']} shots after the "
                            "rejoin that the server never fired")
        # Under simulated latency the server's aim can drift from the client's a second into
        # the burst after a join from spectating (MP-07, open): the rejoin's directions are
        # then reported, and held only without it.
        impaired = bool(args.latency or args.jitter)
        held = [(a, b) for a, b in matched if not (impaired and a["segment"] == "rejoin")]
        worst = summary([angle(a["dir"], b["dir"]) for a, b in held]).get("max", 0.0)
        if worst > 0.1:
            failures.append(f"predicted and server shot directions differ by up to {worst} degrees")
        if metrics["copies_drawn_in_full"] > 0.1 * max(1, len(copies)):
            failures.append(f"{metrics['copies_drawn_in_full']} of {len(copies)} server copies were drawn in full")
        if steps["respawn"]["full_copies"]:
            failures.append("a server copy was drawn in full around the respawn")
    if not args.predict_hitscan and predicted:
        failures.append("the client predicted shots with net_mpPredictHitscan 0")

    report = {"status": "fail" if failures else "pass", "renderer": args.renderer,
              "predict_hitscan": args.predict_hitscan, "enhanced": args.enhanced, "latency": args.latency,
              "jitter": args.jitter, "turn_rate": args.turn_rate, "identity": identity, "metrics": metrics,
              "captures": [str(games["shooter"] / f"screenshots/{s}.tga") for s in shots],
              "failures": failures,
              "logs": {role: str(game / "logs/openq4.log") for role, game in games.items()}}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "failures": failures,
                      "metrics": {k: v for k, v in metrics.items() if k not in ("steps", "host_placements")}},
                     indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
