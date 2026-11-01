#!/usr/bin/env python3
"""Measure how much a higher bot skill level actually wins by, with no input.

A hidden listen server runs a free-for-all between two groups of bots that
differ only in `bot_skill`: characters are off (`bot_characters 0`), so every
bot is the plain skill curve and nothing but skill separates the groups. The
engine log is reduced to what decides a fight:

* the cross kills - how often the high group killed the low group and the
  other way round - whose ratio is the headline number;
* each group's accuracy and damage, read from the match statistics `botlist`
  prints (hits, shots and damage given since the map started), differenced
  over the measured window.

Run both groups at one level (`--high-skill 5 --low-skill 5`) for an absolute
accuracy figure. The local client is an untargetable, invulnerable observer,
as in mp_bot_audit.py.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path

from mp_bot_audit import OBITUARY_KILLED_BY, OBITUARY_POSSESSIVE, log_has_fatal, strip_colours
from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]
# Plain names, so nothing in a report reads as one of the shipped characters.
NAMES = ("Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel",
         "India", "Juliet", "Kilo", "Lima")

BOTLIST_ROW = re.compile(
    r"^\s*(\d+)\s+(\S+)\s+\S+\s+\S+\s+([\d.]+)\s+(\d+)\s+(\d+)\s+(\d+)/(\d+)\s+\d+%\s+(\d+)\s*$", re.M)


def botlist_rows(block: str) -> dict[str, dict[str, float]]:
    rows = {}
    for client, name, skill, kills, deaths, hits, shots, damage in BOTLIST_ROW.findall(block):
        rows[name] = {"client": int(client), "skill": float(skill), "kills": int(kills),
                      "deaths": int(deaths), "hits": int(hits), "shots": int(shots), "damage": int(damage)}
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-bot-skill-ladder")
    parser.add_argument("--map", dest="map_name", default="mp/q4dm1")
    parser.add_argument("--high-skill", type=int, choices=range(1, 6), default=5)
    parser.add_argument("--low-skill", type=int, choices=range(1, 6), default=1)
    parser.add_argument("--per-group", type=int, default=3, help="bots in each group")
    parser.add_argument("--seconds", type=int, default=180)
    parser.add_argument("--port", type=int, default=28976)
    parser.add_argument("--timeout", type=int, default=0, help="defaults to seconds + 150")
    parser.add_argument("--min-cross-ratio", type=float, default=None,
                        help="fail unless the high group's kills on the low group outnumber the reverse by this factor")
    parser.add_argument("--set", action="append", default=[], metavar="CVAR=VALUE",
                        help="extra server cvar; repeatable")
    args = parser.parse_args()
    if not 1 <= args.per_group <= len(NAMES) // 2:
        parser.error(f"use 1 to {len(NAMES) // 2} bots per group")
    timeout = args.timeout or args.seconds + 150
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    exe = runtime / args.executable_name
    if not exe.is_file() or not (args.basepath / "q4base").is_dir():
        parser.error("the staged client and retail q4base assets are required")
    verify_runtime_is_current(runtime, args.executable_name)

    # Interleaved, so neither group gets the earlier client slots or spawns.
    roster = []
    for i in range(args.per_group):
        roster.append((NAMES[2 * i], args.high_skill, "high"))
        roster.append((NAMES[2 * i + 1], args.low_skill, "low"))
    group_of = {name: group for name, _, group in roster}

    save = output / "save"
    game = save / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    log_path = game / "logs/openq4.log"
    if log_path.exists():
        log_path.unlink()

    script = ["notarget", "god", *[f"addbot {name} {skill}" for name, skill, _ in roster],
              "waitMsec 5000", "botlist", "echo MP_BOT_LADDER_BEGIN"]
    script += ["waitMsec 5000"] * (args.seconds // 5)
    script += ["botlist", "echo MP_BOT_LADDER_END", "quit"]
    (game / "bot_ladder.cfg").write_text("\n".join(script) + "\n", encoding="utf-8")

    cvars = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "sys_consoleWindow": "0", "in_tty": "0", "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
        "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "s_noSound": "1",
        "r_mode": "-1", "r_customWidth": "640", "r_customHeight": "480", "r_renderApi": "gl",
        "r_swapInterval": "0", "com_maxfps": "60", "ui_retained": "0",
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(save), "fs_devpath": str(save),
        "fs_game": "baseoq4", "com_gameMode": "MP", "logFile": "2", "logFileName": "logs/openq4.log",
        "developer": "1", "ui_autoJoin": "1", "ui_spectate": "Play", "ui_name": "BotLadderObserver",
        "net_serverDedicated": "0", "net_LANServer": "1", "net_ip": "127.0.0.1", "net_enableIPv6": "0",
        "net_allowCheats": "1", "sv_cheats": "1",
        "net_port": str(args.port), "si_pure": "0", "si_gameType": "DM",
        "si_maxPlayers": str(len(roster) + 2), "si_minPlayers": "2", "si_warmup": "0", "si_useReady": "0",
        "si_fragLimit": "999", "si_timeLimit": "0",
        "g_matchProfile": "casual", "bot_enable": "1", "bot_minPlayers": "0", "bot_pause": "0",
        "bot_characters": "0", "bot_skillVariance": "0", "bot_chat": "0", "bot_debug": "0",
        "com_skipLoadingContinue": "1", "g_autoExecAfterMapLoad": "bot_ladder.cfg",
        "g_autoExecAfterMapLoadDelayMs": "500",
    }
    for extra in args.set:
        name, _, value = extra.partition("=")
        if not name or not _:
            parser.error(f"--set expects CVAR=VALUE, not {extra!r}")
        cvars[name] = value
    command = [str(exe)]
    for name, value in cvars.items():
        command += ["+set", name, value]
    command += ["+spawnServer", args.map_name]
    (output / "launch.json").write_text(json.dumps(command, indent=2), encoding="utf-8")

    started = time.time()
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    with (output / "console.log").open("w", encoding="utf-8") as console:
        process = subprocess.Popen(command, cwd=runtime, stdout=console, stderr=subprocess.STDOUT,
                                   creationflags=creationflags)
        code = None
        deadline = started + timeout
        while code is None:
            try:
                code = process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                if log_has_fatal(log_path) or time.time() > deadline:
                    process.kill()
                    process.wait()
                    code = -1

    clean = strip_colours(log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else "")
    before, _, measured = clean.partition("\nMP_BOT_LADDER_BEGIN")
    sample, _, after = measured.partition("\nMP_BOT_LADDER_END")
    failures = []
    if code != 0 or "\nMP_BOT_LADDER_END" not in clean:
        failures.append(f"server failed to complete (exit {code})")
    failures.extend(re.findall(r"^.*(?:ERROR:|FATAL:|Assertion failed).*$", clean, re.M))

    start_rows = botlist_rows(before)
    end_rows = botlist_rows(sample)
    obituaries = re.findall(OBITUARY_KILLED_BY, sample, re.M) + re.findall(OBITUARY_POSSESSIVE, sample, re.M)
    cross = {"high_on_low": 0, "low_on_high": 0, "high_on_high": 0, "low_on_low": 0}
    for victim, killer in obituaries:
        if victim in group_of and killer in group_of and victim != killer:
            cross[f"{group_of[killer]}_on_{group_of[victim]}"] += 1

    minutes = max(args.seconds / 60.0, 1e-6)
    groups = {}
    for group in ("high", "low"):
        names = [name for name, _, g in roster if g == group]
        totals = {key: sum(end_rows.get(n, {}).get(key, 0) - start_rows.get(n, {}).get(key, 0) for n in names)
                  for key in ("kills", "deaths", "hits", "shots", "damage")}
        groups[group] = {
            "skill": args.high_skill if group == "high" else args.low_skill,
            "bots": names,
            **totals,
            "accuracy": round(totals["hits"] / totals["shots"], 3) if totals["shots"] else None,
            "damage_per_bot_minute": round(totals["damage"] / (len(names) * minutes), 1),
        }
    if len(end_rows) < len(roster):
        failures.append(f"botlist reported {len(end_rows)} of {len(roster)} bots with combat statistics")
    ratio = (cross["high_on_low"] / cross["low_on_high"]) if cross["low_on_high"] else None
    if args.min_cross_ratio is not None:
        if cross["high_on_low"] == 0 or (ratio is not None and ratio < args.min_cross_ratio):
            failures.append(f"cross-kill ratio {ratio} is below the required {args.min_cross_ratio}")

    report = {
        "map": args.map_name, "seconds": args.seconds, "per_group": args.per_group,
        "exit_code": code, "elapsed_seconds": round(time.time() - started, 1),
        "cross_kills": cross, "cross_kill_ratio": round(ratio, 2) if ratio is not None else None,
        "groups": groups, "bots": end_rows, "failures": failures,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("cross_kills", "cross_kill_ratio", "groups", "failures")},
                     indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
