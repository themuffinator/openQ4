#!/usr/bin/env python3
"""Qualify MVD storage refusal, bounded recording and recovery without user input."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    verify_runtime_is_current(runtime)
    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    if not exe.is_file() or not (args.basepath / "q4base").is_dir() or args.timeout <= 0:
        parser.error("a staged client, retail assets and a positive timeout are required")
    game = output / "save/baseoq4"
    keeper = game / "demos/kept/storage-occupancy.mvd"
    keeper.parent.mkdir(parents=True, exist_ok=False)
    # Synthetic occupancy, deliberately not a playable demo. Every existing
    # recording byte counts, including malformed/interrupted user files.
    keeper.write_bytes(bytes((2 << 20) - (64 << 10)))
    keeper_digest = hashlib.sha256(keeper.read_bytes()).hexdigest()
    paths = [exe, runtime / "baseoq4/game_x64.dll", runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4",
             *sorted(runtime.glob("renderer-*.dll" if os.name == "nt" else "renderer-*.so"))]
    def identity():
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths if path.is_file()}
    before = identity()
    write_script(game / "quota_smoke.cfg", [
        'set g_autoExecAfterMapLoad ""', "set developer 1", "notarget", "god",
        "addbot Rhodes 4 exact", "addbot Sledge 4 exact", "addbot Voss 4 exact", "waitMsec 2000",
        "set mvd_libraryQuotaMB 1", "echo MVD_QUOTA_REFUSAL", "mvdLibrary", "recordMVD blocked", "mvdStatus",
        "set mvd_libraryQuotaMB 2", "echo MVD_QUOTA_ADMISSION", "recordMVD admitted", "mvdStatus",
        "waitMsec 45000", "echo MVD_QUOTA_CAPPED", "mvdStatus", "mvdLibrary", "openq4_reportMPState",
        "echo MVD_QUOTA_RECOVERY", "mvdRecover admitted.mvd.part", "mvdInfo admitted_recovered",
        "echo MVD_QUOTA_PLAYBACK", "playMVD admitted_recovered", "waitMsec 2000", "mvdPause 1", "mvdStatus",
        'screenshot "screenshots/recovered.tga"', "waitMsec 250", "stopMVD", "echo MVD_QUOTA_COMPLETE", "quit",
    ])
    cvars = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "sys_consoleWindow": "0", "in_tty": "0", "in_mouse": "0", "in_joystick": "0",
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(game.parent), "fs_devpath": str(game.parent),
        "fs_game": "baseoq4", "com_gameMode": "MP", "com_nextGameModule": "game_mp", "ui_autoJoin": "1",
        "ui_spectate": "Play", "ui_retained": "1", "ui_name": "QuotaObserver",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0", "r_borderlessDefaultMigrated": "1",
        "r_hiddenWindow": "1", "r_renderApi": "gl", "r_mode": "-1", "r_customWidth": "640", "r_customHeight": "480",
        "r_swapInterval": "0", "com_maxfps": "60", "s_noSound": "1",
        "net_allowCheats": "1", "net_serverDedicated": "0", "net_LANServer": "1", "net_port": "28976",
        "si_pure": "0", "si_gameType": "DM", "si_maxPlayers": "6", "si_minPlayers": "1",
        "si_warmup": "0", "si_useReady": "0", "si_fragLimit": "999", "si_timeLimit": "0", "g_matchProfile": "casual",
        "bot_minPlayers": "0", "bot_chat": "0", "logFile": "2", "logFileName": "logs/openq4.log",
        "com_skipLoadingContinue": "1", "g_autoExecAfterMapLoad": "quota_smoke.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
    }
    assert len(cvars) + 1 <= 64
    command = [str(exe), *[part for key, value in cvars.items() for part in ("+set", key, value)], "+spawnServer", "mp/q4dm1"]
    (output / "launch.json").write_text(json.dumps(command, indent=2) + "\n", encoding="utf-8")
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
    log = read_log(log_path)
    failures = []
    if code != 0 or "\nMVD_QUOTA_COMPLETE" not in log:
        failures.append(f"quota workflow did not complete (exit {code})")
    if re.search(r"FATAL:|ERROR:|Unknown command", log):
        failures.append("engine reported an error")
    if identity() != before:
        failures.append("staged runtime changed during qualification")
    if hashlib.sha256(keeper.read_bytes()).hexdigest() != keeper_digest:
        failures.append("existing recording storage was modified")
    if "MVD library quota is full" not in log or (game / "demos/blocked.mvd.part").exists() or (game / "demos/blocked.mvd").exists():
        failures.append("a full library did not refuse the new recording before opening its file")
    if "MVD_RECORDING_BUDGET libraryBytes=65536" not in log:
        failures.append("the admitted recording did not freeze its 64 KiB remaining budget")
    capped = log.partition("\nMVD_QUOTA_CAPPED")[2].partition("\nMVD_QUOTA_RECOVERY")[0]
    sizes = re.findall(r"MVD_LIBRARY recordings=(\d+) bytes=(\d+) quotaMB=(\d+) automaticDeletion=0", capped)
    if len(sizes) != 1 or int(sizes[0][0]) != 2 or int(sizes[0][1]) > (2 << 20):
        failures.append("the recorder crossed the library quota or omitted its partial stream")
    if 'MVD_LAST_RESULT state=failed' not in capped or 'partial="demos/admitted.mvd.part"' not in capped:
        failures.append("the quota stop did not publish a recoverable partial-recording result")
    recovered = game / "demos/admitted_recovered.mvd"
    partial = game / "demos/admitted.mvd.part"
    if not recovered.is_file() or not partial.is_file() or "MVD_RECOVER recovered" not in log:
        failures.append("manual recovery failed or discarded its source")
    playback = log.partition("\nMVD_QUOTA_PLAYBACK")[2]
    if not re.search(r"MVD_STATUS playing=1 paused=1 seeking=0 time=\d+ duration=\d+ follow=-?\d+", playback):
        failures.append("the recovered capped recording did not play")
    shot = game / "screenshots/recovered.tga"
    if not shot.is_file() or shot.stat().st_size < 1024:
        failures.append("missing engine playback capture")
    report = {"status": "fail" if failures else "pass", "exit": code, "identity": before,
              "storage_occupancy": {"synthetic": True, "path": str(keeper), "sha256": keeper_digest},
              "capped_library": sizes, "partial": str(partial), "recovered": str(recovered),
              "failures": failures, "log": str(log_path), "capture": str(shot)}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "failures": failures, "report": str(output / "report.json")}, indent=2))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
