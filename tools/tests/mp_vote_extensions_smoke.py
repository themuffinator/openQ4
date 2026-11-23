#!/usr/bin/env python3
"""Hidden, input-free modifier, arena, preset and spectator chat votes (MP-12, MP-14, MP-16).

One human hosts a casual Deathmatch listen server with two paused bots, so the
host's own yes is the whole electorate and every vote it calls passes. The
script first has three calls refused without starting a vote (a preset the
server does not have, and spectator chat and an arena already at the value
asked for), then passes, a cool-off apart: Quad Hog on, a rocket arena,
spectator chat on, and the standard preset, which puts the modifiers back.
Each checkpoint prints the switches the votes change, and the log must show
the match restarting for each modifier change.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]
SWITCHES = ("si_quadHog", "si_vampiric", "si_weaponsFrenzy", "si_weaponArena", "si_preset", "g_spectatorChat")
# A caller may call again 30 s after its vote's 30 s window closes, in game time.
COOL_OFF = "waitMsec 62000"


def checkpoint(name: str) -> list[str]:
    return [f"echo MP_VOTES_{name.upper()}", *SWITCHES, "openq4_reportMPState"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", help="runtime executable basename for an isolated test copy")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-vote-extensions-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--port", type=int, default=28839)
    parser.add_argument("--renderer", default="gl")
    parser.add_argument("--timeout", type=int, default=480)
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    verify_runtime_is_current(runtime, getattr(args, "executable_name", ""))
    suffix = ".exe" if os.name == "nt" else ""
    exe = runtime / (args.executable_name or f"openQ4-client_x64{suffix}")
    if not exe.is_file() or not args.basepath.is_dir():
        parser.error("the runtime executable and installed Quake 4 assets are required")
    savepath = output / "save"
    gamepath = savepath / "baseoq4"
    gamepath.mkdir(parents=True, exist_ok=True)
    log_path = gamepath / "logs/openq4.log"
    if log_path.is_file():
        log_path.unlink()
    script = [
        "addbot Cortez 3 exact", "addbot Rhodes 3 exact", "waitMsec 7000",
        *checkpoint("live"),
        # refused before a vote starts, so they cost no cool-off
        "callvote preset nosuchpreset", "waitMsec 1500",
        "callvote spectatorchat 0", "waitMsec 1500",
        "callvote arena none", "waitMsec 1500",
        *checkpoint("refused"),
        "callvote quadhog 1", "waitMsec 6000", *checkpoint("quadhog"),
        COOL_OFF, "callvote arena rocketlauncher", "waitMsec 6000", *checkpoint("arena"),
        COOL_OFF, "callvote spectatorchat 1", "waitMsec 4000", *checkpoint("spectatorchat"),
        COOL_OFF, "callvote preset standard", "waitMsec 6000", *checkpoint("preset"),
        "quit",
    ]
    (gamepath / "mp_vote_extensions_smoke.cfg").write_text("\n".join(script) + "\n", encoding="utf-8")
    cvars = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(savepath),
        "fs_devpath": str(savepath), "fs_game": "baseoq4", "com_gameMode": "MP",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
        "r_borderlessDefaultMigrated": "1", "r_mode": "-1", "r_customWidth": "960",
        "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540",
        "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_renderApi": args.renderer, "com_maxfps": "60", "r_swapInterval": "0",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
        "net_port": str(args.port), "net_serverDedicated": "0", "net_LANServer": "1",
        "net_ip": "127.0.0.1", "net_enableIPv6": "0",
        "si_pure": "0", "si_maxPlayers": "8", "si_gameType": "DM",
        "si_warmup": "0", "si_useReady": "0", "si_minPlayers": "2", "si_countDown": "4",
        "si_timeLimit": "0", "si_fragLimit": "999", "bot_pause": "1",
        "bot_minPlayers": "0", "g_matchProfile": "casual",
        "ui_autoJoin": "1", "ui_spectate": "Play", "g_inactivity": "0",
        "si_quadHog": "0", "si_vampiric": "0", "si_weaponsFrenzy": "0", "si_weaponArena": "none", "si_instagib": "0",
        "si_preset": "", "g_spectatorChat": "0",
        "net_allowCheats": "1", "com_skipLoadingContinue": "1", "g_gameReviewPause": "10",
        "si_allowVoting": "1", "si_voteFlags": "0", "g_votePercent": "51", "g_voteLimit": "0",
        "g_autoExecAfterMapLoad": "mp_vote_extensions_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
    }
    command = [str(exe)]
    for name, value in cvars.items():
        command += ["+set", name, value]
    command += ["+spawnServer", "mp/q4dm1"]
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
    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    failures: list[str] = []
    if not log_path.is_file() or log_path.stat().st_mtime < started:
        failures.append("missing fresh engine log")

    def section(name: str) -> str:
        marker = log.find("\nMP_VOTES_" + name.upper())
        if marker < 0:
            failures.append(f"{name}: checkpoint missing")
            return ""
        rest = log[marker:]
        end = rest.find("\nMP_VOTES_", 1)
        return rest if end < 0 else rest[:end]

    def switches(text: str) -> dict[str, str]:
        return {name: value for name, value in re.findall(r'"(\w+)" is:"([^"]*)"', text) if name in SWITCHES}

    names = ("live", "refused", "quadhog", "arena", "spectatorchat", "preset")
    sections = {name: section(name) for name in names}
    states = {name: switches(text) for name, text in sections.items()}
    # What a step printed comes before its checkpoint: from the previous marker to this one.
    leads, previous = {}, 0
    for name in names:
        marker = log.find("\nMP_VOTES_" + name.upper())
        leads[name] = log[previous:marker] if marker >= 0 else ""
        previous = marker if marker >= 0 else previous
    expected = {
        "live": {"si_quadHog": "0", "si_weaponArena": "none", "g_spectatorChat": "0"},
        "refused": {"si_quadHog": "0", "si_weaponArena": "none", "g_spectatorChat": "0", "si_preset": ""},
        "quadhog": {"si_quadHog": "1", "si_weaponArena": "none"},
        "arena": {"si_quadHog": "1", "si_weaponArena": "rocketlauncher"},
        "spectatorchat": {"g_spectatorChat": "1", "si_weaponArena": "rocketlauncher"},
        "preset": {"si_preset": "standard", "si_quadHog": "0", "si_weaponArena": "none", "g_spectatorChat": "1"},
    }
    for name, wanted in expected.items():
        for key, value in wanted.items():
            if states[name].get(key) != value:
                failures.append(f"{name}: expected {key}={value!r}, got {states[name].get(key)!r}")
    refused_text = leads["refused"]
    if "The server has no such preset" not in refused_text:
        failures.append("refused: no refusal for the unknown preset")
    if refused_text.count("That setting already has that value") != 2:
        failures.append("refused: spectator chat and the arena, already at the values asked for, were not both refused")
    if "called a vote" in refused_text or re.search(r"vote passed", refused_text, re.IGNORECASE):
        failures.append("refused: a refused call started a vote")
    for name, change in (("quadhog", "'quadhog'"), ("arena", "'quadhog arena:rocketlauncher'")):
        if f"The match modifiers changed to {change}" not in leads[name]:
            failures.append(f"{name}: the match did not restart for the new modifiers")
    if "preset 'standard' applied by vote" not in leads["preset"]:
        failures.append("preset: the passed preset vote did not apply")
    for name in ("quadhog", "arena", "spectatorchat", "preset"):
        if not re.search(r"vote passed|#str_104277", leads[name], re.IGNORECASE):
            failures.append(f"{name}: the vote did not pass")
    if re.search(r"unknown command|FATAL ERROR|ERROR:|malformed modifier, preset or arena field|not applied", log, re.IGNORECASE):
        failures.append("engine log contains an unknown command, an error or a malformed vote field")
    if code != 0:
        failures.append(f"engine exit code {code}")
    report = {"status": "fail" if failures else "pass", "renderer": args.renderer, "states": states,
              "failures": failures, "log": str(log_path)}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
