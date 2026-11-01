#!/usr/bin/env python3
"""Measure what each bot skill level is worth in a fight, with no input.

A hidden listen server runs a free-for-all between groups of bots that differ
only in `bot_skill`: characters are off (`bot_characters 0`), so every bot is
the plain skill curve and nothing but skill separates the groups. `--skills`
names one group per entry, so `--skills 1,2,3,4,5` puts every level in the
same match and each one fights all the others under the same conditions.
Every `--map` is a separate match; they run at the same time and pool.

The engine log is reduced to what decides a fight:

* the kill matrix between groups, and from it a Bradley-Terry rating per
  group on the Elo scale (400 points is ten to one): the chance a kill between
  two groups goes to the first is r1 / (r1 + r2). A properly levelled curve
  gives every level a rating above the one below it, by a similar step;
* each group's accuracy and damage, read from the match statistics `botlist`
  prints (hits, shots and damage given since the map started), differenced
  over the measured window. Damage is the median per bot and match: the
  statistics count it before it is clamped to the victim's health, so one
  telefrag would otherwise swamp a group.

One skill on every group gives an absolute figure for that level. The local
client is an untargetable, invulnerable observer, as in mp_bot_audit.py.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import random
import re
import statistics
import subprocess
import time
from pathlib import Path

from mp_bot_audit import OBITUARY_KILLED_BY, OBITUARY_POSSESSIVE, log_has_fatal, strip_colours
from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]
# Plain names, so nothing in a report reads as one of the shipped characters.
NAMES = ("Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel",
         "India", "Juliet", "Kilo", "Lima", "Mike", "November", "Oscar", "Papa",
         "Quebec", "Romeo", "Sierra", "Tango")

BOTLIST_ROW = re.compile(
    r"^\s*(\d+)\s+(\S+)\s+\S+\s+\S+\s+([\d.]+)\s+(\d+)\s+(\d+)\s+(\d+)/(\d+)\s+\d+%\s+(\d+)\s*$", re.M)
WEAPON_ROW = re.compile(r"^\s+weapon (\S+)\s+(\d+)/(\d+)\s+\d+%\s+(\d+) kills\s*$")
STAT_KEYS = ("kills", "deaths", "hits", "shots", "damage")


def botlist_rows(block: str) -> dict[str, dict]:
    """Each bot's `botlist weapons` row, with its per-weapon lines under "weapons"."""
    rows: dict[str, dict] = {}
    current = None
    for line in block.splitlines():
        row = BOTLIST_ROW.match(line)
        if row:
            client, name, skill, kills, deaths, hits, shots, damage = row.groups()
            current = rows[name] = {"client": int(client), "skill": float(skill), "kills": int(kills),
                                    "deaths": int(deaths), "hits": int(hits), "shots": int(shots),
                                    "damage": int(damage), "weapons": {}}
            continue
        weapon = WEAPON_ROW.match(line)
        if weapon and current is not None:
            name, hits, shots, kills = weapon.groups()
            current["weapons"][name] = {"hits": int(hits), "shots": int(shots), "kills": int(kills)}
        elif not line.startswith(" "):
            current = None
    return rows


def bradley_terry(wins: list[list[float]], prior: float = 0.5, iterations: int = 2000) -> list[float]:
    """Elo-scale ratings from wins[i][j] (kills by group i on group j).

    `prior` kills each way between every pair keep a group that never scored
    finite. Ratings are centred on their mean.
    """
    n = len(wins)
    w = [[(wins[i][j] + prior) if i != j else 0.0 for j in range(n)] for i in range(n)]
    strength = [1.0] * n
    for _ in range(iterations):
        updated = []
        for i in range(n):
            total = sum(w[i])
            denominator = sum((w[i][j] + w[j][i]) / (strength[i] + strength[j]) for j in range(n) if j != i)
            updated.append(total / denominator if denominator > 0 else strength[i])
        mean_log = sum(math.log(s) for s in updated) / n
        strength = [s / math.exp(mean_log) for s in updated]
    return [400.0 * math.log10(s) for s in strength]


def write_launch(game: Path, save: Path, args, roster, map_name: str, port: int, output: Path) -> list[str]:
    script = ["notarget", "god", *[f"addbot {name} {skill}" for name, skill, _ in roster],
              "waitMsec 5000", "botlist weapons", "echo MP_BOT_LADDER_BEGIN"]
    script += ["waitMsec 5000"] * (args.seconds // 5)
    script += ["botlist weapons", "echo MP_BOT_LADDER_END", "quit"]
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
        "net_port": str(port), "si_pure": "0", "si_gameType": "DM",
        "si_maxPlayers": str(len(roster) + 2), "si_minPlayers": "2", "si_warmup": "0", "si_useReady": "0",
        "si_fragLimit": "999", "si_timeLimit": "0",
        "g_matchProfile": "casual", "bot_enable": "1", "bot_minPlayers": "0", "bot_pause": "0",
        "bot_characters": "0", "bot_skillVariance": "0", "bot_chat": "0", "bot_debug": "0",
        "com_skipLoadingContinue": "1", "g_autoExecAfterMapLoad": "bot_ladder.cfg",
        "g_autoExecAfterMapLoadDelayMs": "500",
    }
    for extra in args.set:
        name, _, value = extra.partition("=")
        cvars[name] = value
    command = [str(args.runtime_dir.resolve() / args.executable_name)]
    for name, value in cvars.items():
        command += ["+set", name, value]
    command += ["+spawnServer", map_name]
    (output / "launch.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    return command


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-bot-skill-ladder")
    parser.add_argument("--map", dest="maps", action="append", default=None,
                        help="one match per map, run concurrently and pooled (default mp/q4dm1); repeatable")
    parser.add_argument("--skills", default="5,1",
                        help="comma-separated skill per group, e.g. 1,2,3,4,5 for the whole curve")
    parser.add_argument("--per-group", type=int, default=2, help="bots in each group")
    parser.add_argument("--seconds", type=int, default=180)
    parser.add_argument("--port", type=int, default=28976, help="first match's port; later matches add 4 each")
    parser.add_argument("--timeout", type=int, default=0, help="defaults to seconds + 150")
    parser.add_argument("--require-ordered", action="store_true",
                        help="fail unless each group's rating is above every lower-skill group's")
    parser.add_argument("--min-step", type=float, default=None,
                        help="fail unless every adjacent pair of distinct skills is this many rating points apart")
    parser.add_argument("--pool", type=Path, action="append", default=[], metavar="REPORT",
                        help="an earlier report.json of the same --skills whose matches pool into the ratings")
    parser.add_argument("--set", action="append", default=[], metavar="CVAR=VALUE",
                        help="extra server cvar; repeatable")
    args = parser.parse_args()
    maps = args.maps or ["mp/q4dm1"]
    try:
        skills = [int(value) for value in args.skills.split(",") if value.strip()]
    except ValueError:
        parser.error("--skills takes comma-separated integers")
    if len(skills) < 2 or any(not 1 <= skill <= 5 for skill in skills):
        parser.error("--skills needs at least two groups, each 1 to 5")
    if args.per_group < 1 or len(skills) * args.per_group > len(NAMES):
        parser.error(f"at most {len(NAMES)} bots in all")
    for extra in args.set:
        if "=" not in extra or extra.startswith("="):
            parser.error(f"--set expects CVAR=VALUE, not {extra!r}")
    timeout = args.timeout or args.seconds + 150
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    exe = runtime / args.executable_name
    if not exe.is_file() or not (args.basepath / "q4base").is_dir():
        parser.error("the staged client and retail q4base assets are required")
    verify_runtime_is_current(runtime, args.executable_name)

    # Interleaved, so no group gets the earlier client slots.
    roster = []
    for i in range(args.per_group):
        for group, skill in enumerate(skills):
            roster.append((NAMES[i * len(skills) + group], skill, group))
    group_of = {name: group for name, _, group in roster}

    matches = []
    started = time.time()
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    for index, map_name in enumerate(maps):
        match_dir = output / f"{index}-{map_name.replace('/', '_')}"
        game = match_dir / "save" / "baseoq4"
        game.mkdir(parents=True, exist_ok=True)
        log_path = game / "logs/openq4.log"
        if log_path.exists():
            log_path.unlink()
        command = write_launch(game, match_dir / "save", args, roster, map_name, args.port + 4 * index, match_dir)
        console = (match_dir / "console.log").open("w", encoding="utf-8")
        process = subprocess.Popen(command, cwd=runtime, stdout=console, stderr=subprocess.STDOUT,
                                   creationflags=creationflags)
        matches.append({"map": map_name, "log": log_path, "process": process, "console": console, "code": None})

    deadline = started + timeout
    while any(match["code"] is None for match in matches):
        for match in matches:
            if match["code"] is not None:
                continue
            code = match["process"].poll()
            if code is None and (log_has_fatal(match["log"]) or time.time() > deadline):
                match["process"].kill()
                match["process"].wait()
                code = -1
            if code is not None:
                match["code"] = code
                match["console"].close()
        time.sleep(0.5)

    groups = len(skills)
    wins = [[0] * groups for _ in range(groups)]
    same_group_kills = [0] * groups
    totals = [{key: 0 for key in STAT_KEYS} for _ in range(groups)]
    # Per bot and match, so the median ignores outliers: the statistics record
    # damage before it is clamped to the victim's health, and one telefrag
    # adds about ten thousand.
    damage_rates = [[] for _ in range(groups)]
    weapon_totals = [{} for _ in range(groups)]
    minutes = max(args.seconds / 60.0, 1e-6)
    failures = []
    per_match = []
    for match in matches:
        clean = strip_colours(match["log"].read_text(encoding="utf-8", errors="replace")
                              if match["log"].exists() else "")
        before, _, measured = clean.partition("\nMP_BOT_LADDER_BEGIN")
        sample = measured.partition("\nMP_BOT_LADDER_END")[0]
        if match["code"] != 0 or "\nMP_BOT_LADDER_END" not in clean:
            failures.append(f"{match['map']}: server failed to complete (exit {match['code']})")
        failures.extend(f"{match['map']}: {line}"
                        for line in re.findall(r"^.*(?:ERROR:|FATAL:|Assertion failed).*$", clean, re.M))
        start_rows, end_rows = botlist_rows(before), botlist_rows(sample)
        if len(end_rows) < len(roster):
            failures.append(f"{match['map']}: botlist reported {len(end_rows)} of {len(roster)} bots")
        kills_here = 0
        match_wins = [[0] * groups for _ in range(groups)]
        for victim, killer in (re.findall(OBITUARY_KILLED_BY, sample, re.M) +
                               re.findall(OBITUARY_POSSESSIVE, sample, re.M)):
            if victim in group_of and killer in group_of and victim != killer:
                kills_here += 1
                if group_of[killer] == group_of[victim]:
                    same_group_kills[group_of[killer]] += 1
                else:
                    match_wins[group_of[killer]][group_of[victim]] += 1
        for name, _, group in roster:
            for key in STAT_KEYS:
                totals[group][key] += end_rows.get(name, {}).get(key, 0) - start_rows.get(name, {}).get(key, 0)
            damage_rates[group].append((end_rows.get(name, {}).get("damage", 0) -
                                        start_rows.get(name, {}).get("damage", 0)) / minutes)
            start_weapons = start_rows.get(name, {}).get("weapons", {})
            for weapon, figures in end_rows.get(name, {}).get("weapons", {}).items():
                pooled = weapon_totals[group].setdefault(weapon, {"hits": 0, "shots": 0, "kills": 0})
                for key in pooled:
                    pooled[key] += figures[key] - start_weapons.get(weapon, {}).get(key, 0)
        per_match.append({"map": match["map"], "exit_code": match["code"], "bot_kills": kills_here,
                          "kill_matrix": match_wins, "bots": end_rows})

    # Earlier runs of the same curve pool in by their per-match matrices.
    pooled_matrices = [m["kill_matrix"] for m in per_match]
    for path in args.pool:
        earlier = json.loads(path.read_text(encoding="utf-8"))
        if earlier.get("skills") != skills:
            parser.error(f"{path} was run with --skills {earlier.get('skills')}, not {skills}")
        pooled_matrices += [m["kill_matrix"] for m in earlier.get("matches", []) if "kill_matrix" in m]
    for matrix in pooled_matrices:
        for i in range(groups):
            for j in range(groups):
                wins[i][j] += matrix[i][j]

    ratings = bradley_terry(wins)
    report_groups = []
    for group, skill in enumerate(skills):
        t = totals[group]
        report_groups.append({
            "group": group, "skill": skill, "rating": round(ratings[group], 1),
            "kills_on_others": sum(wins[group]), "deaths_to_others": sum(row[group] for row in wins),
            "accuracy": round(t["hits"] / t["shots"], 3) if t["shots"] else None,
            "median_damage_per_bot_minute": round(statistics.median(damage_rates[group]), 1)
            if damage_rates[group] else None,
            **t,
            "weapons": {weapon: figures for weapon, figures in sorted(weapon_totals[group].items())
                        if figures["shots"] or figures["kills"]},
        })

    by_skill = sorted(report_groups, key=lambda g: g["skill"])
    if args.require_ordered:
        for low in by_skill:
            for high in by_skill:
                if high["skill"] > low["skill"] and high["rating"] <= low["rating"]:
                    failures.append(f"skill {high['skill']} rates {high['rating']}, not above skill "
                                    f"{low['skill']} at {low['rating']}")
    levels = sorted(set(skills))

    def level_steps(matrix_wins: list[list[float]]) -> list[float]:
        rated = bradley_terry(matrix_wins, iterations=600)
        mean = {s: statistics.fmean(rated[g] for g, skill in enumerate(skills) if skill == s) for s in levels}
        return [mean[high] - mean[low] for low, high in zip(levels, levels[1:])]

    # Kills cluster within a match - who spawned where, which fights a map
    # makes - so the spread comes from resampling whole matches, not kills.
    # Single rounds of about a thousand kills disagreed by more than counting
    # noise alone allows.
    spreads = None
    if len(pooled_matrices) >= 3:
        rng = random.Random(1)
        samples = []
        for _ in range(200):
            pick = [pooled_matrices[rng.randrange(len(pooled_matrices))] for _ in pooled_matrices]
            resampled = [[sum(m[i][j] for m in pick) for j in range(groups)] for i in range(groups)]
            samples.append(level_steps(resampled))
        spreads = [statistics.stdev(column) for column in zip(*samples)]
    steps = []
    for index, ((low, high), step) in enumerate(zip(zip(levels, levels[1:]), level_steps(wins))):
        entry = {"from": low, "to": high, "step": round(step, 1)}
        if spreads:
            entry["sd"] = round(spreads[index], 1)
        steps.append(entry)
        if args.min_step is not None and step < args.min_step:
            failures.append(f"skill {low} to {high} is {step:.1f} rating points, below {args.min_step}")

    report = {
        "maps": maps, "seconds": args.seconds, "per_group": args.per_group, "skills": skills,
        "elapsed_seconds": round(time.time() - started, 1),
        "pooled_matches": len(pooled_matrices), "kill_matrix": wins, "same_group_kills": same_group_kills,
        "groups": report_groups, "steps": steps, "matches": per_match, "failures": failures,
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("kill_matrix", "groups", "steps", "failures")}, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
