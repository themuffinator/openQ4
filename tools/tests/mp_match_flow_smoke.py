#!/usr/bin/env python3
"""Windowed, input-free MP readiness, votes, round progression and match endings."""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAMETYPES = ("DM", "Tourney", "Team DM", "CTF", "One Flag CTF", "Arena CTF",
             "Arena One Flag CTF", "DeadZone", "Duel", "Clan Arena", "Freeze Tag", "Red Rover")
ROUND_MODES = ("Clan Arena", "Freeze Tag", "Red Rover")


def checkpoint(name: str, shot: bool = True) -> list[str]:
    return [f"echo MP_FLOW_{name.upper()}", "openq4_reportMPState",
            *([f'screenshot "screenshots/{name.lower()}.tga"'] if shot else [])]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", help="runtime executable basename for an isolated test copy")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-match-flow-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--exit-population", choices=("opponents", "human"), default="opponents")
    parser.add_argument("--gametype", choices=GAMETYPES, default="DM")
    parser.add_argument("--map", dest="map_name", help="stock map override")
    parser.add_argument("--finish-by-time", action="store_true", help="finish a one-minute match and capture the review scoreboard")
    parser.add_argument("--finish-by-round", action="store_true", help="win two rounds through deaths and inspect the result")
    parser.add_argument("--finish-by-capture", action="store_true", help="capture twice through stock flag touch triggers")
    parser.add_argument("--finish-by-control", action="store_true", help="carry a DeadZone token into the stock control volume")
    parser.add_argument("--exercise-rescue", action="store_true", help="rescue a frozen teammate before finishing the rounds")
    parser.add_argument("--exercise-vote", action="store_true", help="one human among bots passes a timelimit vote in warmup")
    args = parser.parse_args()
    if sum((args.finish_by_time, args.finish_by_round, args.finish_by_capture, args.finish_by_control)) > 1:
        parser.error("choose one match-ending method")
    if args.finish_by_round and (args.gametype not in ROUND_MODES or args.finish_by_time):
        parser.error("--finish-by-round requires a round mode and excludes --finish-by-time")
    map_name = args.map_name or ("mp/q4ctf1" if "CTF" in args.gametype else
                                "mp/q4tourney1" if args.gametype == "Tourney" else
                                "mp/q4dz1" if args.gametype == "DeadZone" else "mp/q4dm1")
    if args.finish_by_capture and ("CTF" not in args.gametype or map_name != "mp/q4ctf1"):
        parser.error("the capture fixture requires a CTF mode on stock mp/q4ctf1")
    if args.finish_by_control and (args.gametype != "DeadZone" or map_name != "mp/q4dz1"):
        parser.error("the control fixture requires DeadZone on stock mp/q4dz1")
    if args.exercise_rescue and (args.gametype != "Freeze Tag" or not args.finish_by_round):
        parser.error("the rescue fixture requires Freeze Tag with --finish-by-round")
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    suffix = ".exe" if os.name == "nt" else ""
    exe = runtime / (args.executable_name or f"openQ4-client_x64{suffix}")
    if not exe.is_file() or not args.basepath.is_dir():
        parser.error("the runtime executable and installed Quake 4 assets are required")
    savepath = output / "save"
    gamepath = savepath / "baseoq4"
    gamepath.mkdir(parents=True, exist_ok=True)
    take_flag = "teleport " + ("openq4_neutral_flag" if "One Flag" in args.gametype else "mp_ctf_strogg_flag_1")
    score_flag = "teleport " + ("mp_ctf_strogg_flag_1" if "One Flag" in args.gametype else "mp_ctf_marine_flag_1")
    ending = (["teleport powerup_deadzone_3", "waitMsec 1000", "teleport trigger_controlzone",
               "waitMsec 7500", *checkpoint("review"), "waitMsec 12000"]
              if args.finish_by_control else
              [take_flag, "waitMsec 1000", score_flag, "waitMsec 1000", *checkpoint("capture_one"),
               take_flag, "waitMsec 1000", score_flag, "waitMsec 3500", *checkpoint("capture_two"),
               "waitMsec 1000", *checkpoint("review"), "waitMsec 12000"]
              if args.finish_by_capture else
              ["kill 0", "waitMsec 65000", *checkpoint("review"), "waitMsec 12000"]
              if args.finish_by_time else
              ["kill 1", "waitMsec 1000", *checkpoint("round_one"),
               "waitMsec 6500", *checkpoint("round_two"),
               "kill 1", "waitMsec 1000", *checkpoint("round_won"),
               "waitMsec 3500", *checkpoint("review"), "waitMsec 12000"]
              if args.finish_by_round else
              [*(["kick 1", "kick 2", *(["kick 3"] if args.gametype == "Tourney" else [])] if args.exit_population == "opponents" else ["set ui_spectate Spectate"]),
               "waitMsec 13000" if args.gametype == "Tourney" else "waitMsec 7000"])
    script = [
        "addbot Cortez 3 exact", "addbot Rhodes 3 exact",
        *(["addbot Sledge 3 exact"] if args.gametype == "Tourney" else []),
        "waitMsec 3000", "botlist",
        *checkpoint("unready"),
        *(["clientCallVote timelimit 1", "waitMsec 4500", *checkpoint("vote", False)] if args.exercise_vote else []),
        "ready", "waitMsec 1500",
        *checkpoint("countdown"),
        "notready", "waitMsec 5000",
        *checkpoint("cancelled"),
        "ready", "waitMsec 9000",
        "openq4_assertMPClientActive", *checkpoint("live"),
        *(["script \"$player3.setOrigin('-4480 5392 8')\"", "wait 30", "kill 2", "wait 90",
           *checkpoint("frozen"), "setviewpos -4470 5355 72 180",
           "wait 240", *checkpoint("thawed")] if args.exercise_rescue else []),
        *ending,
        *checkpoint("ended", False), "quit",
    ]
    (gamepath / "mp_flow_smoke.cfg").write_text("\n".join(script) + "\n", encoding="utf-8")
    cvars = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(savepath),
        "fs_devpath": str(savepath), "fs_game": "baseoq4", "com_gameMode": "MP",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
        "r_borderlessDefaultMigrated": "1", "r_mode": "-1", "r_customWidth": "960",
        "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540",
        "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_renderApi": "gl", "com_maxfps": "60", "r_swapInterval": "0",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
        "net_port": "28831", "net_serverDedicated": "0", "net_LANServer": "1",
        "si_pure": "0", "si_maxPlayers": "8", "si_gameType": args.gametype,
        "si_warmup": "1", "si_useReady": "1", "si_minPlayers": "4" if args.gametype == "Tourney" else "3",
        "si_countDown": "4", "si_timeLimit": "1" if args.finish_by_time and not args.exercise_vote else "0", "si_fragLimit": "999",
        "si_overtime": "0", "bot_pause": "1",
        "si_warmupReadyPercentage": "0.51", "bot_minPlayers": "0", "g_matchProfile": "casual",
        "ui_autoJoin": "1", "ui_spectate": "Play", "ui_ready": "Not Ready",
        "g_gameReviewPause": "10" if args.finish_by_time or args.finish_by_round or args.finish_by_capture or args.finish_by_control else "2", "com_skipLoadingContinue": "1",
        "si_autoBalance": "1", "ui_team": "Marine", "net_allowCheats": "1",
        "si_roundLimit": "2", "si_roundWarmupDelay": "2", "si_roundEndDelay": "3",
        "si_captureLimit": "2" if args.finish_by_capture else "999",
        "g_autoExecAfterMapLoad": "mp_flow_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
    }
    if args.exercise_vote:
        cvars.update(si_allowVoting="1", si_voteFlags="0")
    if args.finish_by_control:
        cvars.update(si_controlTime="5")
    command = [str(exe)]
    for name, value in cvars.items():
        command += ["+set", name, value]
    command += ["+spawnServer", map_name]
    assert len(cvars) + 1 <= 64, "engine startup command capacity exceeded"
    (output / "launch.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    with (output / "console.log").open("w", encoding="utf-8") as console:
        started = time.time()
        process = subprocess.Popen(command, cwd=runtime, stdout=console, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.kill()  # Only the process launched by this test.
            process.wait()
            code = -1
    log_path = gamepath / "logs/openq4.log"
    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    phases = [(int(a), int(b), int(t)) for a, b, t in re.findall(r"MP match phase: (\d+) -> (\d+) at (\d+)", log)]
    expected = [(0, 1), (1, 2), (2, 1), (1, 2), (2, 3), (3, 5), (5, 6), (6, 1)]
    failures = []
    if not log_path.is_file() or log_path.stat().st_mtime < started:
        failures.append("missing fresh engine log")
    if [(a, b) for a, b, _ in phases] != expected:
        failures.append(f"phase sequence differs: {phases}")
    checkpoints = [("UNREADY", 1), ("COUNTDOWN", 2), ("CANCELLED", 1), ("LIVE", 3), ("ENDED", 1)]
    if args.finish_by_time or args.finish_by_round or args.finish_by_capture or args.finish_by_control:
        checkpoints.append(("REVIEW", 5))
    if args.exercise_rescue:
        checkpoints += [("FROZEN", 3), ("THAWED", 3)]
    if args.finish_by_round:
        checkpoints += [("ROUND_ONE", 3), ("ROUND_TWO", 3), ("ROUND_WON", 3)]
    if args.finish_by_capture:
        checkpoints += [("CAPTURE_ONE", 3), ("CAPTURE_TWO", 5)]
    snapshots = {}
    for name, phase in checkpoints:
        marker = log.find("\nMP_FLOW_" + name)
        before = re.findall(r"MP match phase: (\d+) -> (\d+) at (\d+)", log[:marker]) if marker >= 0 else []
        if not before or int(before[-1][1]) != phase:
            failures.append(f"{name}: expected phase {phase} at checkpoint")
        snapshot = re.search(r"\nMP_STATE ([^\r\n]+)", log[marker:]) if marker >= 0 else None
        if snapshot:
            state = {key: int(value) for key, value in re.findall(r"(\w+)=(-?\d+)", snapshot[1])}
            snapshots[name] = state
            if state.get("phase") != phase or state.get("session_phase") != phase:
                failures.append(f"{name}: gameplay and session phase mismatch: {state}")
        else:
            failures.append(f"{name}: missing authoritative state report")
    if args.exercise_vote:
        vote = re.search(r"\nMP_FLOW_VOTE.*?\nMP_STATE ([^\r\n]+)", log, re.DOTALL)
        if not vote or "timelimit=1" not in vote[1]:
            failures.append("one human among bots did not pass the warmup vote")
    if args.finish_by_capture:
        for name, score in (("CAPTURE_ONE", 1), ("CAPTURE_TWO", 2), ("REVIEW", 2)):
            if snapshots.get(name, {}).get("marine") != score or snapshots.get(name, {}).get("strogg") != 0:
                failures.append(f"{name}: expected capture score {score}-0")
    if args.finish_by_control and (snapshots.get("REVIEW", {}).get("marine") != 5 or snapshots.get("REVIEW", {}).get("strogg") != 0):
        failures.append("DeadZone did not finish at five seconds of Marine control")
    if args.exercise_rescue:
        for name, alive in (("FROZEN", False), ("THAWED", True)):
            section = re.search(r"\nMP_FLOW_" + name + r"\s+.*?(?=\nMP_FLOW_|\Z)", log, re.DOTALL)
            player = re.search(r"MP_PLAYER slot=2 .*?health=(-?\d+) spectating=(\d+)", section[0]) if section else None
            if not player or (int(player[1]) > 0) != alive or player[2] != "0":
                failures.append(f"{name}: teammate did not have the expected frozen/thawed body")
        thawed = re.search(r"\nMP_FLOW_THAWED.*?MP_PLAYER slot=0 [^\r\n]*score=1 ", log, re.DOTALL)
        if not thawed:
            failures.append("the rescuer did not earn a point")
    if args.finish_by_round:
        rounds = [(int(a), int(b)) for a, b in re.findall(r"MP match round: (\d+) -> (\d+)", log)]
        # The parent review transition itself clears the session round; the
        # legacy round adapter subsequently acknowledges that same state.
        if rounds != [(0, 1), (1, 2), (2, 3), (3, 1), (1, 2), (2, 3)]:
            failures.append(f"round progression differs: {rounds}")
        for name, number, phase in (("LIVE", 1, 2), ("ROUND_ONE", 1, 3),
                                    ("ROUND_TWO", 2, 2), ("ROUND_WON", 2, 3)):
            state = snapshots.get(name, {})
            if state.get("round_number") != number or state.get("round") != phase:
                failures.append(f"{name}: expected round {number} phase {phase}")
        if snapshots.get("REVIEW", {}).get("marine") != 2 or snapshots.get("REVIEW", {}).get("strogg") != 0:
            failures.append("round match did not finish with Marine 2, Strogg 0")
        if snapshots.get("REVIEW", {}).get("round") != 0:
            failures.append("review did not clear the active round")
    if re.search(r"rejected match (?:round )?transition|unknown command|entity not found|Not allowed in multiplayer|could not import casual match rule|competitive rules rejected|FATAL ERROR|ERROR:", log, re.IGNORECASE):
        failures.append("engine log contains a rejected transition, unknown command or error")
    if code != 0:
        failures.append(f"engine exit code {code}")
    if args.finish_by_time and args.gametype == "Duel" and "MP summary: 2 ranked players, 1 unranked" not in log:
        failures.append("Duel review did not retain two finalists and one waiting player")
    shots = [gamepath / f"screenshots/{name}.tga" for name in ("unready", "countdown", "cancelled", "live")]
    if args.finish_by_time or args.finish_by_round or args.finish_by_capture or args.finish_by_control:
        shots.append(gamepath / "screenshots/review.tga")
    if args.exercise_rescue:
        shots += [gamepath / f"screenshots/{name}.tga" for name in ("frozen", "thawed")]
    if args.finish_by_round:
        shots += [gamepath / f"screenshots/{name}.tga" for name in ("round_one", "round_two", "round_won")]
    if args.finish_by_capture:
        shots += [gamepath / f"screenshots/{name}.tga" for name in ("capture_one", "capture_two")]
    if not all(path.is_file() and path.stat().st_size > 18 and path.stat().st_mtime >= started for path in shots):
        failures.append("missing fresh engine screenshots")
    report = {"status": "fail" if failures else "pass", "gametype": args.gametype,
              "map": map_name, "phases": phases, "snapshots": snapshots,
              "failures": failures, "log": str(log_path), "screenshots": [str(path) for path in shots]}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
