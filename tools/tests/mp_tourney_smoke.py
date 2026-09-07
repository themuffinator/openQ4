#!/usr/bin/env python3
"""Finish a four-player stock Tourney bracket using windowed engine commands."""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import read_log, write_script

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", help="runtime executable basename for an isolated test copy")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-tourney-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--timeout", type=int, default=260)
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    exe = runtime / (args.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"))
    if not exe.is_file() or not args.basepath.is_dir():
        parser.error("a staged runtime and installed Quake 4 assets are required")
    save = output / "save"
    game = save / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    log_path, loop = game / "logs/openq4.log", game / "tourney_probe.cfg"
    idle = ["waitMsec 250", "openq4_reportMPState", "exec tourney_probe.cfg"]
    write_script(loop, idle)
    write_script(game / "tourney_start.cfg", ["addbot Cortez 3 exact", "addbot Rhodes 3 exact",
                 "addbot Sledge 3 exact", "exec tourney_probe.cfg"])
    cvars = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(save), "fs_devpath": str(save),
        "fs_game": "baseoq4", "com_gameMode": "MP", "r_fullscreen": "0", "r_borderless": "0",
        "r_fullscreenDesktop": "0", "r_borderlessDefaultMigrated": "1", "r_mode": "-1",
        "r_customWidth": "960", "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540",
        "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_renderApi": "gl", "com_maxfps": "60", "r_swapInterval": "0",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
        "net_port": "28851", "net_serverDedicated": "0", "net_LANServer": "1", "si_pure": "0",
        "si_maxPlayers": "8", "si_gameType": "Tourney", "si_minPlayers": "4",
        "si_warmup": "1", "si_useReady": "0", "si_countDown": "4", "si_timeLimit": "1",
        "si_fragLimit": "999", "si_tourneyLimit": "3", "bot_minPlayers": "0", "bot_pause": "1",
        "g_matchProfile": "casual", "g_gameReviewPause": "10", "com_skipLoadingContinue": "1",
        "ui_autoJoin": "1", "ui_spectate": "Play", "ui_name": "Player", "net_allowCheats": "1",
        "g_autoExecAfterMapLoad": "tourney_start.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
    }
    command = [str(exe)]
    for key, value in cvars.items():
        command += ["+set", key, value]
    command += ["+spawnServer", "mp/q4tourney1"]
    (output / "launch.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    assert len(cvars) + 1 <= 64
    started = time.time()
    stage, failures, semifinal_winners, finalists = 0, [], [], []
    first_round = -1
    with (output / "console.log").open("w", encoding="utf-8") as stream:
        process = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT)
        try:
            while time.time() - started < args.timeout and process.poll() is None:
                log = read_log(log_path)
                if re.search(r"ERROR:|FATAL ERROR|could not import casual match rule|Error:|rejected match.*transition", log):
                    failures.append("engine reported an error")
                    break
                current = log.rsplit("\nMP_STATE ", 1)[-1]
                arenas = [tuple(map(int, row)) for row in re.findall(
                    r"MP_TOURNEY round=(\d+) arena=(\d+) state=(\d+) first=(-?\d+) second=(-?\d+) winner=(-?\d+)", current)]
                if stage == 0 and len(arenas) == 2 and all(row[2] == 2 for row in arenas):
                    first_round = arenas[0][0]
                    semifinal_winners = [min(row[3:5]) for row in arenas]
                    losers = [max(row[3:5]) for row in arenas]
                    write_script(game / "tourney_idle.cfg", idle)
                    write_script(loop, ["echo MP_TOURNEY_SEMIFINALS", "openq4_assertMPClientActive",
                                 'screenshot "screenshots/semifinals.tga"', *[f"kill {slot}" for slot in losers],
                                 "exec tourney_idle.cfg"])
                    stage = 1
                elif stage == 1 and len(arenas) == 1 and arenas[0][0] > first_round and arenas[0][2] == 2:
                    finalists = list(arenas[0][3:5])
                    if sorted(finalists) != sorted(semifinal_winners):
                        failures.append("the semifinal winners did not advance into the final")
                        break
                    write_script(loop, ["echo MP_TOURNEY_FINAL", "openq4_reportMPState",
                                 "openq4_assertMPClientActive", 'screenshot "screenshots/final.tga"',
                                 f"kill {max(finalists)}", "exec tourney_idle.cfg"])
                    stage = 2
                elif stage == 2 and "phase=5 " in current:
                    write_script(loop, ["echo MP_TOURNEY_REVIEW", "openq4_reportMPState",
                                 'screenshot "screenshots/review.tga"', "waitMsec 18000",
                                 "echo MP_TOURNEY_ENDED", "openq4_reportMPState", "quit"])
                    stage = 3
                # Restore the polling file once each one-shot action has run.
                for marker in ("MP_TOURNEY_SEMIFINALS", "MP_TOURNEY_FINAL"):
                    if "\n" + marker + " " in log and marker in loop.read_text(encoding="utf-8"):
                        write_script(loop, idle)
                time.sleep(0.2)
            else:
                if process.poll() is None:
                    failures.append(f"timeout at stage {stage}")
        finally:
            try:
                code = process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()  # Only this test's process.
                code = process.wait()
    log = read_log(log_path)
    champion = re.search(r"Round \d+: player (\d+) \([^)]+\) has won the tourney!", log)
    if code != 0 or stage != 3:
        failures.append(f"incomplete bracket run: stage={stage}, exit={code}")
    if champion is None or not finalists or int(champion[1]) != min(finalists):
        failures.append("the expected finalist was not declared tournament champion")
    if log.count("has won this arena") != 3:
        failures.append("expected two semifinals and one final to finish")
    # With readiness disabled the next bracket leaves warmup on the very next
    # frame.  Check the completed lifecycle edge, not a later polling sample.
    review_tail = log.partition("\nMP_TOURNEY_REVIEW ")[2].partition("\nMP_TOURNEY_ENDED ")[0]
    if not re.search(r"MP match phase: 5 -> 6.*?MP match phase: 6 -> 1", review_tail, re.DOTALL):
        failures.append("the tournament did not return to warmup")
    shots = [game / f"screenshots/{name}.tga" for name in ("semifinals", "final", "review")]
    if not all(path.is_file() and path.stat().st_mtime >= started and path.stat().st_size > 18 for path in shots):
        failures.append("missing fresh engine screenshots")
    report = {"status": "fail" if failures else "pass", "stage": stage, "exit": code,
              "failures": failures, "semifinal_winners": semifinal_winners, "finalists": finalists,
              "champion": int(champion[1]) if champion else None, "log": str(log_path),
              "screenshots": [str(path) for path in shots]}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
