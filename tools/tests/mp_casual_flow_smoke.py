#!/usr/bin/env python3
"""Hidden, input-free casual match flow: callvote, team timeouts, inactivity and the spectator lock (MP-12).

One human hosts a casual Team DM listen server with two paused bots. The script
passes a coin-toss vote alone, calls a team timeout and watches it expire into
the resume countdown and back to play, ends a second timeout early with timein,
is refused a third, passes a roundlimit vote after the cooldown, is held in
play by si_spectatorLock 2 when it asks to spectate, and is finally moved to
spectators by g_inactivity (it never sends input), which ends the match as
abandoned because no human is left in it. Every checkpoint reads the
authoritative state through openq4_reportMPState. si_ settings are latched at
map load, so the lock is set on the command line, not mid-match.
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


def checkpoint(name: str) -> list[str]:
    return [f"echo MP_CASUAL_{name.upper()}", "openq4_reportMPState"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", help="runtime executable basename for an isolated test copy")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-casual-flow-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--port", type=int, default=28837)
    parser.add_argument("--renderer", default="gl")
    parser.add_argument("--timeout", type=int, default=240)
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
        "addbot Cortez 3 exact", "addbot Rhodes 3 exact", "waitMsec 7000", "botlist",
        *checkpoint("live"),
        # one human alone is the whole electorate: the coin toss passes at once
        "callvote cointoss", "waitMsec 3500", *checkpoint("cointoss"),
        # a team timeout: pending -> paused -> expiry -> resume countdown -> running
        "timeout", "waitMsec 2500", *checkpoint("timeout"),
        "waitMsec 9000", *checkpoint("resume_countdown"),
        "waitMsec 6000", *checkpoint("resumed"),
        # the second budget, ended early by its own side
        "timeout", "waitMsec 2500", *checkpoint("second_timeout"),
        "timein", "waitMsec 7000", *checkpoint("timein"),
        # no budget left
        "timeout", "waitMsec 1500", *checkpoint("exhausted"),
        # a caller may vote again 30 s after its vote's 30 s window closes, in game
        # time, which the two pauses above held still for about 13 s
        "waitMsec 48000", "callvote roundlimit 5", "waitMsec 3500", *checkpoint("roundlimit"),
        # si_spectatorLock 2 keeps a player in live play
        "set ui_spectate Spectate", "waitMsec 3000", *checkpoint("locked"),
        # the host sends no input: g_inactivity warns at half time and moves it at the limit;
        # with no human left the match ends as abandoned
        "set g_inactivity 12", "waitMsec 15000", *checkpoint("inactive"),
        "quit",
    ]
    (gamepath / "mp_casual_flow_smoke.cfg").write_text("\n".join(script) + "\n", encoding="utf-8")
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
        "si_pure": "0", "si_maxPlayers": "8", "si_gameType": "Team DM",
        "si_warmup": "0", "si_useReady": "0", "si_minPlayers": "2", "si_countDown": "4",
        "si_timeLimit": "0", "si_fragLimit": "999", "si_overtime": "0", "bot_pause": "1",
        "bot_minPlayers": "0", "g_matchProfile": "casual",
        "ui_autoJoin": "1", "ui_spectate": "Play", "ui_team": "Marine", "si_autoBalance": "1",
        "net_allowCheats": "1", "com_skipLoadingContinue": "1", "g_gameReviewPause": "10",
        "si_allowVoting": "1", "si_voteFlags": "0", "g_votePercent": "51", "g_voteLimit": "3",
        "si_timeouts": "2", "si_timeoutLength": "10", "si_spectatorLock": "2", "g_inactivity": "0",
        "g_autoExecAfterMapLoad": "mp_casual_flow_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
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

    def snapshot(name: str) -> tuple[dict[str, int], dict[int, dict[str, int]]]:
        marker = log.find("\nMP_CASUAL_" + name.upper())
        if marker < 0:
            failures.append(f"{name}: checkpoint missing")
            return {}, {}
        section = log[marker:]
        end = section.find("\nMP_CASUAL_", 1)
        section = section if end < 0 else section[:end]
        state = re.search(r"\nMP_STATE ([^\r\n]+)", section)
        if not state:
            failures.append(f"{name}: missing authoritative state report")
            return {}, {}
        values = {key: int(value) for key, value in re.findall(r"(\w+)=(-?\d+)", state[1])}
        players = {}
        for line in re.findall(r"\nMP_PLAYER ([^\r\n]+)", section):
            fields = {key: int(value) for key, value in re.findall(r"(\w+)=(-?\d+)", line)}
            players[fields.get("slot", -1)] = fields
        return values, players

    states = {name: snapshot(name) for name in ("live", "cointoss", "timeout", "resume_countdown", "resumed",
                                                "second_timeout", "timein", "exhausted", "roundlimit",
                                                "locked", "inactive")}
    live = states["live"][0]
    if live.get("phase") != 3 or live.get("pause") != 0 or live.get("timeouts_marine") != 2:
        failures.append(f"live: expected GAMEON with two Marine timeouts, got {live}")
    if not re.search(r"coin shows (heads|tails)|#str_4276[89]", log):
        failures.append("the coin toss vote did not announce a result")
    if states["timeout"][0].get("pause") != 2 or states["timeout"][0].get("pause_kind") != 1 or states["timeout"][0].get("timeouts_marine") != 1:
        failures.append(f"timeout: expected a charged, paused team timeout, got {states['timeout'][0]}")
    if states["resume_countdown"][0].get("pause") != 3:
        failures.append(f"resume_countdown: expected the resume countdown, got {states['resume_countdown'][0]}")
    if states["resumed"][0].get("pause") != 0:
        failures.append(f"resumed: expected play to run again, got {states['resumed'][0]}")
    if states["second_timeout"][0].get("pause") != 2 or states["second_timeout"][0].get("timeouts_marine") != 0:
        failures.append(f"second_timeout: expected the last Marine timeout in use, got {states['second_timeout'][0]}")
    if states["timein"][0].get("pause") != 0:
        failures.append(f"timein: expected an early resume, got {states['timein'][0]}")
    if states["exhausted"][0].get("pause") != 0 or "timeout: " not in log:
        failures.append(f"exhausted: a third timeout was not refused, got {states['exhausted'][0]}")
    if states["roundlimit"][0].get("roundlimit") != 5:
        failures.append(f"roundlimit: the vote did not apply, got {states['roundlimit'][0]}")
    for name in ("live", "roundlimit", "locked"):
        human = states[name][1].get(0, {})
        if human.get("spectating") != 0:
            failures.append(f"{name}: the host should still be playing: {human}")
    if states["locked"][0].get("phase") != 3:
        failures.append("locked: the match should still be live")
    human = states["inactive"][1].get(0, {})
    if human.get("spectating") != 1:
        failures.append(f"inactive: the host should be spectating: {human}")
    if states["inactive"][0].get("phase") not in (5, 6):
        failures.append(f"inactive: the match should have ended without a human, got {states['inactive'][0]}")
    if re.search(r"rejected match (?:round )?transition|unknown command|could not import casual match rule|competitive rules rejected|FATAL ERROR|ERROR:", log, re.IGNORECASE):
        failures.append("engine log contains a rejected transition, unknown command or error")
    if code != 0:
        failures.append(f"engine exit code {code}")
    report = {"status": "fail" if failures else "pass", "renderer": args.renderer,
              "states": {name: value[0] for name, value in states.items()},
              "humans": {name: value[1].get(0, {}) for name, value in states.items()},
              "failures": failures, "log": str(log_path)}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
