#!/usr/bin/env python3
"""Swim, wade and climb out of every liquid-lab pool under player movement, without input.

A listen host on mp/liquid_lab walks into each pool from the central aisle and
moves the way a player holding keys would. The development cvar
`in_movementProbe` holds the named movement buttons inside the usercmd
generator, so each command reaches the server's player physics through the
same path as a held key; no operating-system input is synthesised.

Per pool the host walks in, dives to the floor, moves right, left, back and
forward there, swims up to the surface and swims forward against the far edge
until it climbs out. In the deep pool it also stays down until its air runs
out, takes drowning damage, and dives again after surfacing to show the air
refilled. In the 24-unit wading pool it runs across and jumps out.
`g_debugLiquid` logs every water-level change, drowning tick and wading
footstep; `getviewpos` samples the eye position after each move. Captures at
the main moments are kept for review by eye.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
EYE = 68  # pm_normalviewheight; setviewpos and getviewpos place and report the eye
SURFACE = -16  # every liquid-lab pool's surface; the walkways are at z = 0
# name, liquid, start x, start y, yaw, floor z, far-edge y, swims, god mode.
# See maps/mp/liquid_lab.map: the north row is deep water (304 units), shallow
# water (112) and wading water (24); the south row is lava and slime (64 deep
# over a raised floor). Starts are on the central aisle, 60 units from the edge.
POOLS = (
    ("deep", "water", -720, 120, 90, -320, 650, True, False),
    ("shallow", "water", 0, 120, 90, -128, 650, True, False),
    ("wading", "water", 720, 120, 90, -40, 650, False, False),
    ("slime", "slime", 545, -120, 270, -80, -650, True, True),
    ("lava", "lava", -545, -120, 270, -80, -650, True, True),
)
# pm_waterAir is 720 frames, 12 seconds of air, and about 8 of them are gone by
# the time the floor moves finish. Nine more seconds give several drowning
# ticks on the rising 2, 4, 6... ramp, well short of a fatal total.
DROWN_WAIT_MSEC = 9000
# Resting on the floor with crouch held must stay put. Before 12 November the
# underwater slope rule turned the downward push into a slide of about 40 units
# a second along any leftover drift, 60 units over this window.
FLOOR_REST_MSEC = 1500
FLOOR_REST_DRIFT = 20
DROWN_REST_DRIFT = 16


def probe(steps: list[str], buttons: str, msec: int) -> None:
    steps += [f'set in_movementProbe "{buttons}"', f"waitMsec {msec}", 'set in_movementProbe ""']


def sample(steps: list[str], pool: str, label: str) -> None:
    steps += [f"echo SWIM_POS {pool} {label}", "getviewpos"]


def capture(steps: list[str], shots: list[str], name: str) -> None:
    shots.append(name)
    steps.append(f"screenshot screenshots/{name}.tga")


def build_steps(shots: list[str]) -> list[str]:
    steps = ['set g_autoExecAfterMapLoad ""', "notarget", "set g_debugLiquid 1",
             "waitMsec 3000", "echo LIQUID_SWIM_READY"]
    god = False
    for pool, _, x, y, yaw, floor, _, swims, wants_god in POOLS:
        if wants_god != god:
            steps.append("god")
            god = wants_god
        steps += [f"echo LIQUID_SWIM_POOL {pool}", f"setviewpos {x} {y} {EYE} {yaw}", "waitMsec 1000"]
        sample(steps, pool, "start")
        if not swims:
            # Run in, across the wading pool and against its far wall, then jump out.
            steps += [f'set in_movementProbe "_forward"', "waitMsec 600"]
            capture(steps, shots, f"{pool}_running")
            steps += ["waitMsec 2400", 'set in_movementProbe ""', "waitMsec 300"]
            sample(steps, pool, "far_wall")
            probe(steps, "_forward _moveUp", 1200)
            steps.append("waitMsec 800")
            sample(steps, pool, "climbed")
            capture(steps, shots, f"{pool}_climbed")
            continue
        probe(steps, "_forward", 900)
        steps.append("waitMsec 300")
        sample(steps, pool, "entered")
        capture(steps, shots, f"{pool}_entered")
        steps += ['set in_movementProbe "_moveDown"', f"waitMsec {300 + (SURFACE - floor) * 9}"]
        sample(steps, pool, "floor_a")
        steps.append(f"waitMsec {FLOOR_REST_MSEC}")
        sample(steps, pool, "floor_b")
        capture(steps, shots, f"{pool}_floor")
        steps.append('set in_movementProbe ""')
        for buttons, label in (("_moveRight", "right"), ("_moveLeft", "left"),
                               ("_back", "back"), ("_forward", "forward")):
            probe(steps, f"{buttons} _moveDown", 700)
            steps.append("waitMsec 200")
            sample(steps, pool, label)
        if pool == "deep":
            # Stay down until the air runs out and drowning damage starts.
            # The last stroke coasts for a moment, so the rest is measured from 1.5 s in.
            steps += ["echo LIQUID_SWIM_DROWN", 'set in_movementProbe "_moveDown"', "waitMsec 1500"]
            sample(steps, pool, "rest_start")
            steps.append(f"waitMsec {DROWN_WAIT_MSEC - 1500}")
            sample(steps, pool, "drowned")
            steps.append('set in_movementProbe ""')
            capture(steps, shots, f"{pool}_drowning")
        probe(steps, "_moveUp", 400 + (SURFACE - floor) * 8)
        steps.append("waitMsec 300")
        sample(steps, pool, "surface")
        capture(steps, shots, f"{pool}_surface")
        if pool == "deep":
            # Breathe, then dive again: the entry transition reports the refilled air.
            # A swimmer with no input sinks, so holding up keeps the head out.
            probe(steps, "_moveUp", 3000)
            steps += ["echo LIQUID_SWIM_REDIVE"]
            probe(steps, "_moveDown", 700)
            probe(steps, "_moveUp", 1600)
            steps.append("waitMsec 300")
        steps += ["echo LIQUID_SWIM_CLIMB"]
        probe(steps, "_forward _moveUp", 4500)
        steps.append("waitMsec 800")
        sample(steps, pool, "climbed")
        capture(steps, shots, f"{pool}_climbed")
        steps += ["setviewpos 0 0 68 90", "waitMsec 1500"]
    steps += ["echo LIQUID_SWIM_COMPLETE", "quit"]
    return steps


def parse_positions(log: str) -> dict[str, dict[str, list[float]]]:
    positions: dict[str, dict[str, list[float]]] = {}
    pattern = r"SWIM_POS (\w+) (\w+)\s*\n\s*\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) (-?[\d.]+)"
    for pool, label, x, y, z, yaw in re.findall(pattern, log):
        positions.setdefault(pool, {})[label] = [float(x), float(y), float(z), float(yaw)]
    return positions


def flat_distance(a: list[float], b: list[float]) -> float:
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5


def section(log: str, pool: str) -> str:
    return log.partition(f"LIQUID_SWIM_POOL {pool}")[2].partition("LIQUID_SWIM_POOL ")[0]


def check_pool(pool: str, liquid: str, floor: int, edge: int, swims: bool, body: str,
               at: dict[str, list[float]]) -> list[str]:
    failures = []
    transitions = re.findall(r"liquid: level (\d) -> (\d), type 0x[0-9a-f]+ \(([^)]*)\), air (\d+)/", body)
    if not any(a == "0" and b != "0" and liquid in name.lower() for a, b, name, _ in transitions):
        failures.append(f"{pool}: never entered the {liquid}")
    if not any(a != "0" and b == "0" for a, b, _, _ in transitions):
        failures.append(f"{pool}: never left the {liquid}")
    labels = ["start", "climbed"] + (["entered", "floor_a", "floor_b", "right", "left", "back", "forward",
                                      "surface"] if swims else ["far_wall"])
    missing = [label for label in labels if label not in at]
    if missing:
        return failures + [f"{pool}: missing position samples {missing}"]
    north = edge > 0
    ahead = (lambda a, b: b - a) if north else (lambda a, b: a - b)
    climbed = at["climbed"]
    # Out of the pool means standing on the far walkway: beyond its edge and at deck height.
    if not (ahead(edge, climbed[1]) > 0 and abs(climbed[2] - EYE) < 6):
        failures.append(f"{pool}: did not climb out onto the far walkway, ended at {climbed[:3]}")
    if not swims:
        if any(b not in ("0", "1") for _, b, _, _ in transitions):
            failures.append(f"{pool}: wading went deeper than the feet {transitions}")
        if abs(at["far_wall"][2] - (floor + EYE)) > 6 or ahead(at["start"][1], at["far_wall"][1]) < 400:
            failures.append(f"{pool}: did not run across the pool floor, reached {at['far_wall'][:3]}")
        steps = re.findall(r"liquid: wade footstep (\S+), sound (\S+)", body)
        if len(steps) < 2:
            failures.append(f"{pool}: {len(steps)} wading footsteps logged, expected at least 2")
        return failures
    if not any(b == "3" for _, b, _, _ in transitions):
        failures.append(f"{pool}: head never went under")
    if ahead(at["start"][1], at["entered"][1]) < 60:
        failures.append(f"{pool}: walking forward did not carry the player into the pool")
    # Holding down reaches the floor: the eye is within a standing height of it and stops sinking.
    floor_a, floor_b = at["floor_a"][2], at["floor_b"][2]
    if floor_b - floor > EYE + 4 or abs(floor_a - floor_b) > 2:
        failures.append(f"{pool}: holding down did not reach and stay on the floor ({floor_a}, {floor_b})")
    rest = flat_distance(at["floor_a"], at["floor_b"])
    if rest > FLOOR_REST_DRIFT:
        failures.append(f"{pool}: resting on the floor with crouch held slid {rest:.1f} units")
    if pool == "deep":
        if "rest_start" not in at or "drowned" not in at:
            failures.append("deep: missing the position samples around the drowning rest")
        elif flat_distance(at["rest_start"], at["drowned"]) > DROWN_REST_DRIFT:
            failures.append(f"deep: drifted {flat_distance(at['rest_start'], at['drowned']):.1f} units "
                            "while resting on the floor")
    deltas = {
        "right": [at["right"][i] - at["floor_b"][i] for i in range(2)],
        "left": [at["left"][i] - at["right"][i] for i in range(2)],
        "back": [at["back"][i] - at["left"][i] for i in range(2)],
        "forward": [at["forward"][i] - at["back"][i] for i in range(2)],
    }
    sign = 1 if north else -1
    expected = {"right": (0, sign), "left": (0, -sign), "back": (1, -sign), "forward": (1, sign)}
    for label, (axis, direction) in expected.items():
        moved = deltas[label][axis] * direction
        drift = abs(deltas[label][1 - axis])
        if moved < 50 or drift > moved * 0.5:
            failures.append(f"{pool}: {label} moved {deltas[label]} instead of along axis {axis}")
    if at["surface"][2] < SURFACE - 40:
        failures.append(f"{pool}: holding up did not bring the head to the surface ({at['surface'][2]})")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--port", type=int, default=28975)
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    verify_runtime_is_current(runtime)
    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    if output.exists() or not exe.is_file() or not (args.basepath / "q4base").is_dir():
        parser.error("use a new output directory, a staged client and installed retail assets")
    output.mkdir(parents=True)
    paths = [exe, *sorted((runtime / "baseoq4").glob("game-mp*")), *sorted(runtime.glob("renderer-*")),
             runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4"]
    paths = [p for p in paths if p.is_file() and p.suffix not in (".pdb", ".lib", ".exp")]
    identity = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    game = output / "host" / "baseoq4"
    game.mkdir(parents=True)
    shots: list[str] = []
    write_script(game / "liquid_swim.cfg", build_steps(shots))

    cvars = {"win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
             "sys_consoleWindow": "0", "in_tty": "0", "in_mouse": "0", "in_joystick": "0",
             "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
             "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_renderApi": args.renderer,
             "r_mode": "-1", "r_customWidth": "960", "r_customHeight": "540", "r_windowWidth": "960",
             "r_windowHeight": "540", "r_swapInterval": "0", "com_maxfps": "60", "s_noSound": "1",
             "ui_autoJoin": "1", "ui_spectate": "Play", "ui_name": "LiquidSwim", "fs_game": "baseoq4",
             "com_gameMode": "MP", "com_nextGameModule": "game_mp", "in_alwaysRun": "1",
             "in_toggleCrouch": "0", "pm_thirdPerson": "0",
             "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(game.parent),
             "fs_devpath": str(game.parent), "logFile": "2", "logFileName": "logs/openq4.log",
             "developer": "1", "net_allowCheats": "1", "net_serverDedicated": "0", "net_LANServer": "1",
             "net_port": str(args.port), "net_ip": "127.0.0.1", "net_enableIPv6": "0", "si_pure": "0",
             "si_gameType": "DM", "si_minPlayers": "1", "si_maxPlayers": "4", "si_warmup": "0",
             "si_useReady": "0", "si_fragLimit": "999", "si_timeLimit": "0", "g_matchProfile": "casual",
             "bot_minPlayers": "0", "com_skipLoadingContinue": "1",
             "g_autoExecAfterMapLoad": "liquid_swim.cfg", "g_autoExecAfterMapLoadDelayMs": "500"}
    command = [str(exe), *[part for key, value in cvars.items() for part in ("+set", key, value)],
               "+spawnServer", "mp/liquid_lab"]
    (output / "host-launch.json").write_text(json.dumps(command, indent=2))
    failures = []
    with (output / "host-console.log").open("w") as stream:
        process = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            if process.wait(timeout=300) != 0:
                failures.append("host exited abnormally")
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            failures.append("host timed out")
    log = re.sub(r"\^[0-9A-Za-z]", "", read_log(game / "logs/openq4.log"))
    if "LIQUID_SWIM_COMPLETE" not in log or re.search(r"ERROR:|FATAL:|Unknown command|in_movementProbe:", log):
        failures.append("host did not complete cleanly")
    if not re.search(rf"Renderer API: {args.renderer} \(", log):
        failures.append("the requested renderer did not activate")
    positions = parse_positions(log)
    transitions = {}
    for pool, liquid, _, _, _, floor, edge, swims, _ in POOLS:
        body = section(log, pool)
        transitions[pool] = re.findall(r"liquid: level (\d -> \d), type 0x[0-9a-f]+ \(([^)]*)\), air (\d+)", body)
        failures += check_pool(pool, liquid, floor, edge, swims, body, positions.get(pool, {}))

    deep = section(log, "deep")
    drowning = deep.partition("LIQUID_SWIM_DROWN")[2].partition("LIQUID_SWIM_REDIVE")[0]
    drown_ticks = [(float(d), int(h)) for d, h in re.findall(r"liquid: drowning for ([\d.]+), health (\d+)", drowning)]
    if len(drown_ticks) < 2 or drown_ticks[-1][1] >= drown_ticks[0][1]:
        failures.append(f"deep: no rising drowning damage at zero air {drown_ticks}")
    if not re.search(r"liquid: submerged, air 0/", drowning):
        failures.append("deep: the air never reached zero while submerged")
    redive = deep.partition("LIQUID_SWIM_REDIVE")[2].partition("LIQUID_SWIM_CLIMB")[0]
    refilled = [int(a) for a in re.findall(r"liquid: level \d -> 3, type 0x[0-9a-f]+ \([^)]*\), air (\d+)/", redive)]
    if not refilled or refilled[0] <= 0:
        failures.append(f"deep: air did not refill after surfacing {refilled}")
    wading = [m for m in re.findall(r"liquid: wade footstep (\S+), sound (\S+)", section(log, "wading"))]

    missing = [s for s in shots if not (game / f"screenshots/{s}.tga").is_file() or
               (game / f"screenshots/{s}.tga").stat().st_size < 1024]
    if missing:
        failures.append(f"missing captures: {missing}")
    report = {"status": "fail" if failures else "pass", "renderer": args.renderer, "identity": identity,
              "positions": positions, "transitions": transitions, "drowning": drown_ticks,
              "refilled_air": refilled, "wading_footsteps": len(wading),
              "wade_sounds": sorted({sound for _, sound in wading}),
              "captures": [str(game / f"screenshots/{s}.tga") for s in shots],
              "failures": failures, "log": str(game / "logs/openq4.log")}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "failures": failures, "positions": positions,
                      "drowning": drown_ticks, "refilled_air": refilled,
                      "wading_footsteps": len(wading)}, indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
