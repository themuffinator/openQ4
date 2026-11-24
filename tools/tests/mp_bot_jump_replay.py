#!/usr/bin/env python3
"""Replay a jump, pad-impulse or dodge overshoot with one isolated bot.

Uses the staged runtime, an untargetable observer and engine console commands.
No human input or desktop capture is used. This is a bounded regression, not
the complete multiplayer safety or performance qualification.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path

from mp_bot_audit import file_sha256, strip_colours, world_deaths
from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]


def assess(log: str, exit_code: int, pad_impulse: bool = False, dodge_runout: bool = False) -> dict:
    # The engine's echo command appends a space before its newline.
    clean = "\n" + "\n".join(line.rstrip() for line in strip_colours(log).splitlines()) + "\n"
    _, begin, sample = clean.partition("\nMP_BOT_JUMP_REPLAY_BEGIN\n")
    sample, end, _ = sample.partition("\nMP_BOT_JUMP_REPLAY_END\n")
    failures = []
    if exit_code != 0 or not begin or not end:
        failures.append("Replay did not complete")
    failures.extend(re.findall(r"^.*(?:ERROR:|FATAL:|Assertion failed).*$", clean, re.M))
    probe = "DODGE" if dodge_runout else "PAD" if pad_impulse else "JUMP"
    command = "bot_probeDodge" if dodge_runout else "bot_probePad" if pad_impulse else "bot_probeJump"
    probes = re.findall(rf"^BOT_{probe}_PROBE client=(\d+).*$", sample, re.M)
    if probes != ["1"]:
        failures.append("Expected exactly one bot probe after the rejection controls")
    if sample.count(f"{command}: requires an isolated local cheat server") != 1:
        failures.append("Human-client rejection control failed")
    if sample.count(f"{command}: client must be an integer") != 3:
        failures.append("Invalid-client rejection controls failed")
    takeoffs = re.findall(r"^BOT_JUMP_TAKEOFF .*$", sample, re.M)
    travel = "jump pad" if pad_impulse else "jump(?! pad)"
    completed = re.findall(rf"^bot Rhodes: completed {travel} .*$", sample, re.M)
    deaths = world_deaths(clean, {"Rhodes"})
    dodge_decisions = re.findall(r"^NAV_DODGE_LANDING .*?supported=([01]) .*$", sample, re.M)
    if dodge_runout:
        completed = re.findall(r"^bot Rhodes: released dodge jump .*\(grounded\)$", sample, re.M)
        if not dodge_decisions or dodge_decisions[0] != "0" or "1" not in dodge_decisions or not completed:
            failures.append("The dodge must reject the unsafe momentum, then complete a supported hop")
    elif (not pad_impulse and not takeoffs) or not completed:
        failures.append("The bot did not complete the routed traversal")
    if deaths:
        failures.append("The bot died to the world")
    return {"failures": failures, "accepted_takeoffs": takeoffs,
            "completed_traversals": completed, "complete_run_world_deaths": deaths,
            "dodge_decisions": dodge_decisions,
            "prediction_frames": len(re.findall(r"^NAV_JUMP_FRAME ", sample, re.M)),
            "traversal_frames": len(re.findall(r"^BOT_TRAVEL ", sample, re.M))}


# q4ctf5's func_jumppad_14 (entity 34) lies beside the walkway its trigger
# shares with the jump landing at 604 -484 -184; its natural flight lands on
# func_jumppad_15, which throws the bot across the map. Each walking leg below
# avoids the trigger, while the starting momentum carries toward it. Before
# the transport guard, "westC" entered the pad and rode that chain.
UNPLANNED_PAD_TRIALS = {
    "westA": "680 -370 -183.75 130 290 0 560 -200 -184",
    "westB": "690 -380 -183.75 200 250 0 560 -200 -184",
    "westC": "700 -400 -183.75 250 250 0 600 -172 -184",
}


def assess_unplanned_pad(sample: str) -> dict:
    failures, trials = [], {}
    for name in UNPLANNED_PAD_TRIALS:
        begin, end = f"MP_BOT_TRIAL_BEGIN {name}", f"MP_BOT_TRIAL_END {name}"
        if begin not in sample or end not in sample:
            failures.append(f"{name}: trial did not run")
            continue
        body = sample.split(begin, 1)[1].split(end, 1)[0]
        trials[name] = {"armed": len(re.findall(r"^BOT_MOVE_PROBE client=1 ", body, re.M)) == 1,
                        "adopted": re.findall(r"^bot Rhodes: adopted pad .*$", body, re.M),
                        "guarded": len(re.findall(r"^BOT_TRANSPORT_GUARD client=1 ", body, re.M)),
                        "world_death": bool(re.search(r"^Rhodes (?:was killed by world|died)$", body, re.M))}
        if not trials[name]["armed"]:
            failures.append(f"{name}: probe did not arm")
        if trials[name]["adopted"]:
            failures.append(f"{name}: entered a pad the route did not choose")
        if trials[name]["world_death"]:
            failures.append(f"{name}: died to the world")
    if trials.get("westC", {}).get("guarded", 0) == 0:
        failures.append("westC: the transport guard never engaged")
    return {"failures": failures, "trials": trials}


def unplanned_pad_replay(runtime: Path, executable: str, basepath: Path, output: Path, port: int) -> int:
    from mp_bot_impulse_replay import replay_session
    trials = [{"name": name, "decisive": True, "command": f"bot_probeMove 1 {state}"}
              for name, state in UNPLANNED_PAD_TRIALS.items()]
    started = time.monotonic()
    completed, clean, code = replay_session(runtime, executable, basepath, output, "mp/q4ctf5", "CTF",
                                            trials, port, 200)
    result = assess_unplanned_pad(clean.partition("\nMP_BOT_REPLAY_BEGIN\n")[2])
    if not completed:
        result["failures"].insert(0, "Replay did not complete")
    result.update({"exit_code": code, "elapsed_seconds": round(time.monotonic() - started, 2),
                   "identity": {str(p): file_sha256(p) for p in
                                (runtime / executable, runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4",
                                 *[p for p in (runtime / "baseoq4").glob("game_*") if p.suffix in (".dll", ".so", ".dylib")])},
                   "scope": "One isolated q4ctf5 bot on walking legs clear of func_jumppad_14 with momentum toward it; "
                            "no enemies. Bounded regression, not match acceptance."})
    (output / "report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return int(bool(result["failures"]))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-bot-jump-replay")
    parser.add_argument("--port", type=int, default=28972)
    parser.add_argument("--timeout", type=int, default=120)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--pad-impulse", action="store_true", help="replay the recorded post-hit pad flight instead of the air-step jump")
    mode.add_argument("--dodge-runout", action="store_true", help="replay q4xdm13's high-momentum post-drop dodge")
    mode.add_argument("--unplanned-pad", action="store_true",
                      help="replay q4ctf5 momentum toward func_jumppad_14 beside a walking leg that avoids it")
    args = parser.parse_args()
    runtime, output, basepath = args.runtime_dir.resolve(), args.output_dir.resolve(), args.basepath.resolve()
    exe = runtime / args.executable_name
    if not exe.is_file() or not (basepath / "q4base").is_dir():
        parser.error("The staged client and retail q4base assets are required")
    if output.exists():
        parser.error("Use a new output directory to preserve earlier evidence")
    verify_runtime_is_current(runtime, args.executable_name)
    if args.unplanned_pad:
        return unplanned_pad_replay(runtime, args.executable_name, basepath, output, args.port)
    game = output / "save/baseoq4"
    game.mkdir(parents=True)
    command_name = "bot_probePad" if args.pad_impulse else "bot_probeJump"
    state = ("1538 108 434 822 -10 412 2452 76 256 func_jumppad_11" if args.pad_impulse else
             "-556 876 608.25 -130 -130 0 -572 844 632.25")
    neutral = ("1538 108 434 0 0 0 2452 76 256 func_jumppad_11" if args.pad_impulse else
               "-556 876 608.25 0 0 0 -572 844 632.25")
    observer = "2350 -300 600 45 70 0" if args.pad_impulse else "-420 960 740 45 225 0"
    if args.dodge_runout:
        command_name = "bot_probeDodge"
        state = "947 -390 -319.75 -575 81 0 847 -390 -319.75"
        neutral = "947 -390 -319.75 0 0 0 847 -390 -319.75"
        observer = "620 -620 -100 40 90 0"
    neutral_command = "bot_probeMove" if args.dodge_runout else command_name
    script = [
        "notarget", "god", "noclip", "set bot_debug 4", "set bot_chat 0",
        f"setviewpos {observer}", "addbot Rhodes 4 exact", "waitMsec 2000",
        "navmesh info", "set bot_pause 1",
        f"{neutral_command} 1 {neutral}",
        f"waitMsec {64 if args.pad_impulse or args.dodge_runout else 512}", "echo MP_BOT_JUMP_REPLAY_BEGIN",
        *[f"{command_name} {client} {state}" for client in ("0", "-1", "214748364800", "1.5")],
        f"{command_name} 1 {state}", "set bot_pause 0", "waitMsec 1500",
        'screenshot "screenshots/dm8-jump.tga"', "waitMsec 3500",
        "openq4_reportMPState", "echo MP_BOT_JUMP_REPLAY_END", "quit",
    ]
    (game / "jump_replay.cfg").write_text("\n".join(script) + "\n", encoding="utf-8")
    (output / "runner.py").write_bytes(Path(__file__).read_bytes())
    cvars = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "sys_consoleWindow": "0", "in_tty": "0", "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
        "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "s_noSound": "1",
        "r_mode": "-1", "r_customWidth": "960", "r_customHeight": "540",
        "r_windowWidth": "960", "r_windowHeight": "540", "r_renderApi": "gl",
        "r_swapInterval": "0", "com_maxfps": "60", "ui_retained": "0",
        "fs_basepath": str(basepath), "fs_savepath": str(game.parent), "fs_devpath": str(game.parent),
        "fs_game": "baseoq4", "com_gameMode": "MP", "logFile": "2", "logFileName": "logs/openq4.log",
        "developer": "1", "ui_autoJoin": "1", "ui_spectate": "Play", "ui_name": "BotAuditObserver",
        "net_serverDedicated": "0", "net_LANServer": "1", "net_ip": "127.0.0.1", "net_enableIPv6": "0",
        "net_allowCheats": "1", "sv_cheats": "1", "net_port": str(args.port), "si_pure": "0",
        "si_gameType": "DM", "si_maxPlayers": "4", "si_minPlayers": "2", "si_warmup": "0", "si_useReady": "0",
        "si_fragLimit": "999", "si_timeLimit": "0", "g_matchProfile": "casual",
        "bot_enable": "1", "bot_minPlayers": "0", "bot_pause": "0", "bot_characters": "1",
        "bot_skillVariance": "0", "com_skipLoadingContinue": "1",
        "g_autoExecAfterMapLoad": "jump_replay.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
    }
    command = [str(exe)]
    for key, value in cvars.items():
        command += ["+set", key, value]
    command += ["+spawnServer", "mp/q4xdm13" if args.dodge_runout else "mp/q4dm8"]
    (output / "launch.json").write_text(json.dumps(command, indent=2) + "\n", encoding="utf-8")
    paths = [exe, runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4",
             *[p for p in (runtime / "baseoq4").glob("game_*") if p.suffix in (".dll", ".so", ".dylib")],
             *[ROOT / p for p in ("src/game/bots/NavMesh.cpp", "src/game/bots/Bot.cpp",
                                 "src/game/bots/Bot.h", "src/game/gamesys/SysCmds.cpp", "src/game/Player.cpp")],
             Path(__file__).resolve()]
    identity = {str(path): file_sha256(path) for path in paths}
    started = time.monotonic()
    with (output / "console.log").open("w", encoding="utf-8") as console:
        process = subprocess.Popen(command, cwd=runtime, stdout=console, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            code = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            code = -1
    log_path = game / "logs/openq4.log"
    result = assess(log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else "", code, args.pad_impulse, args.dodge_runout)
    if any(file_sha256(path) != digest for path, digest in ((Path(p), h) for p, h in identity.items())):
        result["failures"].append("Source or runtime changed during the replay")
    capture = game / "screenshots/dm8-jump.tga"
    if not capture.is_file():
        result["failures"].append("Missing engine screenshot")
    result.update({"exit_code": code, "elapsed_seconds": round(time.monotonic() - started, 2),
                   "identity": identity, "engine_capture": str(capture),
                   "scope": ("One isolated q4xdm13 bot, rounded post-drop momentum and normal dodge controller; no enemy impulses."
                             if args.dodge_runout else
                             "One isolated DM8 bot, rounded recorded post-hit pad flight, real player physics; no subsequent hits."
                             if args.pad_impulse else "One isolated DM8 bot, exact recorded approach, real player physics; no enemy impulses.")})
    (output / "report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return int(bool(result["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
