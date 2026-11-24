#!/usr/bin/env python3
"""Exercise Arena pause/resume and a result with engine commands.

Null OpenAL is the default. --audio-output physical uses the default device for
a listening check; automated results alone never establish audible correctness.
A shared isolated --profile also checks persisted result ledgers.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

from mp_round_remote_smoke import verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--profile", type=Path)
    parser.add_argument("--outcome", choices=("win", "loss", "draw"), default="win")
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--audio-output", choices=("null", "physical"), default="null")
    parser.add_argument("--listening-cue", action="store_true", help="keep the player alive and a retail lightning loop running across pauses")
    parser.add_argument("--port", type=int, default=28990)
    parser.add_argument("--timeout", type=int, default=220)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error("an unprivileged UDP port is required")
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    verify_runtime_is_current(runtime)
    output.mkdir(parents=True, exist_ok=False)
    profile = args.profile.resolve() if args.profile else output / "save"
    game = profile / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    config = game / "openQ4Config.cfg"
    before = config.read_text(encoding="utf-8", errors="replace") if config.is_file() else ""
    (output / "config-before.cfg").write_text(before, encoding="utf-8")
    log = game / "logs/openq4.log"
    # Preserve a previous run's log when deliberately sharing its isolated home.
    if log.is_file():
        (output / "previous-openq4.log").write_bytes(log.read_bytes())
        log.unlink()
    for name in ("arena-pause1", "arena-pause2", "arena-result"):
        (game / "screenshots" / (name + ".tga")).unlink(missing_ok=True)
    write_script(game / "arena_bootstrap.cfg", [
        "waitMsec 1800", "campaignSelect arena", "waitMsec 1500",
        "openq4_retainedGui focus arena_start", "openq4_retainedGui menu accept 1",
        "openq4_retainedGui menu accept 0",
    ])
    pauses = ((1, 8000), (2, 8000)) if args.audio_output == "physical" else ((1, 3500), (2, 1800))
    resume_hold = 6000 if args.audio_output == "physical" else 1000
    # Arena's countdown restarts the map and resets cheats. Set the observer's
    # protection after that restart, before starting the persistent audio cue.
    script = ["waitMsec 10000", "god", "waitMsec 10000"] if args.listening_cue else ["waitMsec 20000"]
    if args.listening_cue:
        script += ["testSound weapon_lightninggun_fireloop", "echo ARENA_LISTENING_LOOP_STARTED"]
    script += ["waitMsec 10000", "echo ARENA_SAMPLE_live", "openq4_arenaCeremony status", "s_soundWorlds", "listPlayingSounds"]
    for cycle, hold in pauses:
        script += ["openq4_assertMenuActivation 5000 game", "waitMsec 500",
                   f"echo ARENA_SAMPLE_pause{cycle}a", "openq4_arenaCeremony status", "s_soundWorlds",
                   f"waitMsec {hold}", f"echo ARENA_SAMPLE_pause{cycle}b", "openq4_arenaCeremony status",
                   "s_soundWorlds", f'screenshot "screenshots/arena-pause{cycle}.tga"',
                   "openq4_retainedGui event onBack", f"waitMsec {resume_hold}",
                   f"echo ARENA_SAMPLE_resume{cycle}", "openq4_arenaCeremony status", "s_soundWorlds", "listPlayingSounds",
                   "waitMsec 500", f"echo ARENA_SAMPLE_resume{cycle}b", "s_soundWorlds"]
    # Victory review (6 s), scoreboard (5 s), stats (6 s), fades and the
    # browser handoff must finish before observing the archived result.
    script += [f"openq4_arenaCeremony {args.outcome}", "waitMsec 30000", "echo ARENA_SAMPLE_result",
               "s_soundWorlds", "ui_arenaProgress", "ui_arenaBest", "openq4_retainedGui report",
               'screenshot "screenshots/arena-result.tga"', "echo ARENA_SOUND_COMPLETE", "wait 5", "quit"]
    write_script(game / "arena_probe.cfg", script)
    settings = {
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(profile), "fs_devpath": str(profile),
        # The campaign browser belongs to SP and its Start action switches to
        # MP. Starting in MP first would reload to SP and discard the remainder
        # of this config script before it can activate the match.
        "fs_game": "baseoq4", "com_gameMode": "SP", "si_gameType": "singleplayer", "ui_autoJoin": "1",
        "ui_retained": "1", "ui_arenaSelectedTier": "0", "ui_arenaSelectedMatch": "0",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "r_renderApi": args.renderer, "r_fullscreen": "0", "r_fullscreenDesktop": "0", "r_borderless": "0",
        "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_mode": "-1", "r_customWidth": "960",
        "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540", "com_maxfps": "60",
        "r_swapInterval": "0", "in_mouse": "0", "in_joystick": "0", "s_noSound": "0", "s_muteUnfocused": "0",
        "in_tty": "0", "sys_consoleWindow": "0", "s_deviceName": "",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1", "net_port": str(args.port),
        "net_ip": "127.0.0.1", "net_enableIPv6": "0", "net_allowCheats": "1", "com_skipLoadingContinue": "1",
        "g_autoExecAfterMapLoad": "arena_probe.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
    }
    if args.audio_output == "physical":
        settings["s_volume"] = ".25"
    executable = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    command = [str(executable), *(part for key, value in settings.items() for part in ("+set", key, value)),
               "+exec", "arena_bootstrap.cfg"]
    environment = dict(os.environ, ALSOFT_LOGFILE=str(output / "openal.log"), ALSOFT_LOGLEVEL="3")
    if args.audio_output == "null":
        environment["ALSOFT_DRIVERS"] = "null"
    else:
        environment.pop("ALSOFT_DRIVERS", None)
    report = {"status": "fail", "outcome": args.outcome, "renderer": args.renderer, "command": command,
              "audio_backend": "OpenAL Soft null (no physical output)" if args.audio_output == "null" else "default physical endpoint requested",
              "audible_result": "not applicable" if args.audio_output == "null" else "pending human listening check",
              "listening_cue": args.listening_cue, "harness_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "capture_method": "engine screenshot command",
              "source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "runtime_sha256": {}, "samples": {}, "world_samples": {}, "failures": []}
    for path in (executable, runtime / "baseoq4/game_x64.dll", runtime / "baseoq4/game_x64.so",
                 runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4"):
        if path.is_file():
            with path.open("rb") as source:
                report["runtime_sha256"][path.relative_to(runtime).as_posix()] = hashlib.file_digest(source, "sha256").hexdigest()
    launch_options = {}
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        launch_options["startupinfo"] = startup
    observed_markers = set()
    with (output / "console.log").open("w", encoding="utf-8") as stream:
        process = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT, env=environment,
                                   stdin=subprocess.DEVNULL, **launch_options)
        deadline = time.monotonic() + args.timeout
        try:
            while process.poll() is None:
                current = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
                if args.audio_output == "physical":
                    for marker in re.findall(r"^ARENA_SAMPLE_(\w+)\s*$", current, re.M):
                        if marker not in observed_markers:
                            observed_markers.add(marker)
                            print("Listening check phase: " + marker, flush=True)
                if re.search(r"RETAINED_GUI_OPERATION \w+ failed|Unknown command|FATAL ERROR|Unable to open server on port", current):
                    report["failures"].append("diagnostic action failed; terminated the isolated fixture")
                    process.terminate()
                    break
                if time.monotonic() >= deadline:
                    raise subprocess.TimeoutExpired(command, args.timeout)
                time.sleep(0.25)
            report["exit_code"] = process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            process.terminate(); process.wait(timeout=20)
            report["exit_code"] = process.returncode
            report["failures"].append("Arena runtime timed out")
    text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
    (output / "openq4.log").write_text(text, encoding="utf-8")
    after = config.read_text(encoding="utf-8", errors="replace") if config.is_file() else ""
    (output / "config-after.cfg").write_text(after, encoding="utf-8")
    if report["exit_code"] != 0 or "ARENA_SOUND_COMPLETE" not in text:
        report["failures"].append("runtime did not finish cleanly")
    if re.search(r"ERROR:|FATAL ERROR|Unknown command|usage: openq4_|requires a retained test|Unable to open server on port", text):
        report["failures"].append("engine or diagnostic command failed")
    if "OpenAL renderer: OpenAL Soft" not in text:
        report["failures"].append("OpenAL Soft was not initialized")
    if ("No Output" in text) != (args.audio_output == "null"):
        report["failures"].append("OpenAL output does not match the requested null/physical mode")
    if args.listening_cue and ("godmode ON" not in text or "Player was killed" in text or "ARENA_LISTENING_LOOP_STARTED" not in text):
        report["failures"].append("listening cue or living-player control did not start or persist")
    for marker, block in re.findall(r"ARENA_SAMPLE_(\w+)\s*\n(.*?)(?=ARENA_SAMPLE_|\Z)", text, re.S):
        rows = re.findall(r"SOUND_WORLD id=(\d+) paused=(\d+) time=(-?\d+) active=(\d+) channels=(\d+)"
                          r"(?: stamp=(-?\d+) now=(-?\d+))?", block)
        report["samples"][marker] = {world: {name: int(value) for name, value in
                                             zip(("paused", "time", "active", "channels", "stamp", "now"), values) if value}
                                     for world, *values in rows}
        state = re.search(r"arena campaign: ceremony phase (\d+) state (\d+) pending (\d+) reported (\d+) outcome (\d+) paused (\d+) clock (\S+)", block)
        if state:
            report["world_samples"][marker] = {"paused": int(state[6]), "clock": state[7]}
    samples = report["samples"]
    if args.listening_cue:
        blocks = dict(re.findall(r"ARENA_SAMPLE_(\w+)\s*\n(.*?)(?=ARENA_SAMPLE_|\Z)", text, re.S))
        report["cue_samples"] = {name: "sound/weapons/lightninggun/loop.wav" in blocks.get(name, "")
                                 for name in ("live", "resume1", "resume2")}
        if not all(report["cue_samples"].values()):
            report["failures"].append("persistent cue missing from the live or resumed voice list")
    report["resume_latency_ms"] = {}
    try:
        for cycle, hold in pauses:
            first, last, resumed, later = (samples[f"{name}{cycle}{suffix}"] for name, suffix in
                                           (("pause", "a"), ("pause", "b"), ("resume", ""), ("resume", "b")))
            assert first["1"]["paused"] == last["1"]["paused"] == 1
            assert first["1"]["time"] == last["1"]["time"]
            assert first["1"]["active"] == last["1"]["active"] == 0
            assert first["2"]["paused"] == last["2"]["paused"] == 0
            assert first["2"]["active"] == last["2"]["active"] == 1
            # Both world clocks derive from one stamp the sound system takes once
            # per frame, while waitMsec counts wall time. Compare each clock with
            # its stamp and the waits with wall time: comparing the two kinds lost
            # a whole frame at either end (~450 ms when a hidden GL window
            # presented with the display asleep).
            assert last["2"]["time"] - first["2"]["time"] == last["2"]["stamp"] - first["2"]["stamp"] > 0
            assert last["2"]["now"] - first["2"]["now"] >= hold - 100
            assert resumed["1"]["paused"] == 0 and resumed["1"]["active"] == 1
            advanced = resumed["1"]["time"] - last["1"]["time"]
            # Resuming never carries the paused interval into the game clock.
            assert 0 < advanced <= resumed["1"]["stamp"] - last["1"]["stamp"]
            # The game world unpaused at stamp (resume stamp - advanced). Keep the
            # old window: running for at least 200 ms of the resume hold.
            latency = resumed["1"]["stamp"] - advanced - last["1"]["now"]
            report["resume_latency_ms"][str(cycle)] = latency
            assert latency <= resume_hold - 200
            # Once resumed, the game clock runs with the live stamp.
            assert later["1"]["paused"] == 0
            assert later["1"]["time"] - resumed["1"]["time"] == later["1"]["stamp"] - resumed["1"]["stamp"] > 0
            old_world, new_world = (report["world_samples"][f"pause{cycle}{suffix}"] for suffix in ("a", "b"))
            assert old_world == new_world and old_world["paused"] == 1
            assert report["world_samples"][f"resume{cycle}"]["paused"] == 0
    except (KeyError, AssertionError):
        report["failures"].append("game/menu sound clocks or route did not satisfy pause/resume")
    if samples.get("result", {}).get("2", {}).get("active") != 1:
        report["failures"].append("result browser did not restore the menu sound world")
    if not re.search(rf"arena campaign: reporting result token \d+ \({args.outcome},", text):
        report["failures"].append("authoritative result handoff missing")
    for phase, cfg in (("before", before), ("after", after)):
        report["ledger_" + phase] = {name: value for name, value in re.findall(r'^seta (ui_arena(?:Best|Progress)) "([^"]*)"', cfg, re.M)}
    if not report["ledger_after"].get("ui_arenaBest"):
        report["failures"].append("no persisted best-result ledger")
    else:
        try:
            def records(ledger):
                return {int(row.split(":")[0]): tuple(map(int, row.split(":")[1:]))
                        for row in ledger.get("ui_arenaBest", "").split()}
            old = records(report["ledger_before"]).get(0)
            new = records(report["ledger_after"])[0]
            expected = {"loss": 0, "win": 1, "draw": 2}[args.outcome]
            rank = {0: 0, 2: 1, 1: 2}
            assert len(new) == 4
            if old is None or rank[expected] > rank[old[0]]:
                assert new[0] == expected
            elif rank[expected] < rank[old[0]]:
                assert new == old
            progress = int(report["ledger_after"]["ui_arenaProgress"])
            assert bool(progress & 1) == (new[0] == 1)
            assert (new[1] > new[2] if new[0] == 1 else new[1] == new[2] if new[0] == 2 else new[1] < new[2])
        except (ValueError, KeyError, AssertionError):
            report["failures"].append("persisted first-match result/progression or best-result ordering is incorrect")
    for name in ("arena-pause1", "arena-pause2", "arena-result"):
        capture = game / "screenshots" / (name + ".tga")
        if capture.is_file():
            (output / (name + ".tga")).write_bytes(capture.read_bytes())
        else:
            report["failures"].append("missing engine capture " + name)
    report["status"] = "fail" if report["failures"] else "pass"
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("status", "outcome", "exit_code", "samples", "ledger_before", "ledger_after", "failures")}, indent=2))
    return int(bool(report["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
