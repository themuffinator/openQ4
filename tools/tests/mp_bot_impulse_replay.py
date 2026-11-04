#!/usr/bin/env python3
"""Classify multiplayer bot world deaths under the impulse-exclusion policy.

The MP-02 traversal gate counts navigation and control deaths. A world death
is excluded as impulse-caused only when all three conditions hold:

1. A logged enemy impulse precedes it: a `BOT_IMPULSE` line (bot_debug 2) for
   the victim, from another client that was its enemy (any other client in a
   free-for-all mode; a client on the other team in a team mode, read from the
   nearest preceding `MP_PLAYER` report), no more than IMPULSE_WINDOW_MSEC
   before the death and with no other world death of the victim in between.
2. An isolated replay of the post-impulse state proves the fall unrecoverable
   with the game's existing physics: one bot, no enemies, teleported to the
   logged origin with the logged post-impulse velocity, remaining health, armour and
   knockback time (`bot_probeHealth`), then every one of
   AIR_HEADINGS fixed full air-input directions plus no input (`bot_probeAir`)
   and the bot's own controller (`bot_probeMove` back toward where it was hit)
   is flown. If any trial survives - grounded and alive at the end of its
   observation window - the death was recoverable and stays a navigation
   failure. A trial that fails to arm, or a session that does not complete,
   leaves the death a navigation failure too (fail closed).
3. The impulse makes the difference: the same moment without it (the logged
   velocity minus its kick, at the health and armour before the hit) lands in at least
   one of three counterfactual trials. Otherwise the bot was already lost.

Everything else - no impulse, a self/team/world impulse, an impulse outside
the window, a missing replay - is a navigation/control death. Every excluded
death is still reported, with its impulse and every trial's outcome.

The replay is conservative in the bot's favour: it does not recreate any
later hit, so it can only find the fall easier to recover than the match
did. Pushes after the replayed enemy impulse are listed with the death. A constant full-input direction is the extremal
air-control policy for displacement along that direction; sixteen headings
sample that envelope, which is evidence rather than proof for a landing
inside a sector narrower than the sampling.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

IMPULSE_WINDOW_MSEC = 6000
AIR_HEADINGS = 16
POLICY_VERSION = 2  # exact post/pre-hit armour and actual knockback timers required
TRIAL_OBSERVE_MSEC = 6000
TRIAL_RESPAWN_MSEC = 3000  # each trial gets a freshly added bot
TRIAL_REMOVE_MSEC = 2500   # the removed client takes a moment to leave its slot
FREE_FOR_ALL_MODES = {"DM", "TOURNEY"}  # every other mode has teams; unknown membership fails closed

IMPULSE = re.compile(
    r"^BOT_IMPULSE client=(\d+) time=(-?\d+) attacker=(-?\d+) damage=(-?\d+) origin=(\S+) (\S+) (\S+) "
    r"kick=(\S+) (\S+) (\S+) velocity=(\S+) (\S+) (\S+) def=(\S+)(?: health=(-?\d+))?(?: knock=(\d+))?"
    r"(?: armor=(-?\d+))?(?: armorSave=(-?\d+))?(?: preKnock=(\d+))?", re.M)
WORLD_DEATH = re.compile(r"^(\S+) (?:was killed by world|died|drowned|dissolved in slime|burned in lava)$", re.M)
WORLD_HIT = re.compile(r"^bot (\S+): hit by world for \d+ at (\S+) (\S+) (\S+) .*\(time (\d+)\)$", re.M)
# `botlist` rows: client, name, character, style, skill, then (since the combat
# columns) kills, deaths, hits/shots, accuracy and damage.
ROSTER = re.compile(r"^\s*(\d+)\s+(\S+)\s+\S+\s+\S+\s+[\d.]+"
                    r"(?:\s+\d+\s+\d+\s+\d+/\d+\s+\d+%\s+\d+)?\s*$", re.M)


def strip_colours(text: str) -> str:
    return re.sub(r"\^[0-9a-zA-Z]", "", text)


def roster(clean: str) -> dict[str, int]:
    """Bot name to client number, from `botlist`."""
    return {name: int(client) for client, name in ROSTER.findall(clean)}


def team_reports(clean: str) -> list[tuple[int, dict[int, int]]]:
    """(game time, {slot: team}) for every MP_STATE report, in log order."""
    reports = []
    for block in clean.split("MP_STATE time=")[1:]:
        stamp = re.match(r"(\d+)", block)
        if not stamp:
            continue
        teams = {int(slot): int(team) for slot, team in re.findall(r"MP_PLAYER slot=(\d+) bot=\d team=(-?\d+) ", block)}
        reports.append((int(stamp.group(1)), teams))
    return reports


def is_enemy(victim: int, attacker: int, at_time: int, gametype: str, reports) -> tuple[bool, str]:
    if attacker == victim:
        return False, "self impulse"
    if attacker < 0 or attacker >= 64:
        return False, "non-player impulse"
    if gametype.strip().upper() in FREE_FOR_ALL_MODES:
        return True, "free-for-all opponent"
    # The report at or before the impulse; before the first one, the first
    # report (teams are assigned before anyone can fire).
    teams = None
    for stamp, block in reports:
        if stamp > at_time:
            if teams is None:
                teams = block
            break
        teams = block
    if not teams or victim not in teams or attacker not in teams:
        return False, "team membership unknown"
    if teams[victim] == teams[attacker]:
        return False, "team-mate impulse"
    return True, "opposing team"


def world_deaths(clean: str, names: set[str]) -> list[dict]:
    """Each bot world death with its time and position from the following damage line."""
    deaths = []
    for match in WORLD_DEATH.finditer(clean):
        name = match.group(1)
        if name not in names:
            continue
        hit = None
        for candidate in WORLD_HIT.finditer(clean, match.end(), match.end() + 4000):
            if candidate.group(1) == name:
                hit = candidate
                break
        deaths.append({"victim": name, "offset": match.start(),
                       "time": int(hit.group(5)) if hit else None,
                       "origin": [float(hit.group(i)) for i in (2, 3, 4)] if hit else None})
    return deaths


def classify(clean: str, names: set[str], gametype: str, window_msec: int = IMPULSE_WINDOW_MSEC) -> list[dict]:
    """Classify every bot world death: 'navigation', or 'impulse-candidate' needing a replay."""
    clients = roster(clean)
    reports = team_reports(clean)
    impulses = [{"client": int(m.group(1)), "time": int(m.group(2)), "attacker": int(m.group(3)),
                 "damage": int(m.group(4)), "origin": [float(m.group(i)) for i in (5, 6, 7)],
                 "kick": [float(m.group(i)) for i in (8, 9, 10)],
                 "velocity": [float(m.group(i)) for i in (11, 12, 13)], "def": m.group(14),
                 "health": int(m.group(15)) if m.group(15) else None,
                 "knock_msec": int(m.group(16)) if m.group(16) else None,
                 "armor": int(m.group(17)) if m.group(17) else None,
                 "armor_save": int(m.group(18)) if m.group(18) else None,
                 "pre_knock_msec": int(m.group(19)) if m.group(19) else None, "offset": m.start()}
                for m in IMPULSE.finditer(clean)]
    # One shotgun shot applies several pellets before physics advances. Replay
    # its complete kick, not every intermediate pellet as a separate cause.
    # Require a contiguous same-attacker/pose/definition chain whose logged
    # velocity and health changes account for the next pellet exactly (within
    # the three-decimal velocity log precision). Keep every component as evidence.
    grouped = []
    for impulse in impulses:
        previous = grouped[-1] if grouped else None
        same_shot = (previous is not None and impulse["def"].startswith("damage_pellet") and
                     all(previous[key] == impulse[key] for key in
                         ("client", "time", "attacker", "origin", "def", "knock_msec")) and
                     previous["health"] is not None and impulse["health"] is not None and
                     previous["health"] - impulse["damage"] == impulse["health"] and
                     previous["armor"] is not None and impulse["armor"] is not None and
                     previous["armor_save"] is not None and impulse["armor_save"] is not None and
                     previous["armor"] - impulse["armor_save"] == impulse["armor"] and
                     previous["knock_msec"] == impulse["pre_knock_msec"] and
                     all(abs(previous["velocity"][axis] + impulse["kick"][axis] - impulse["velocity"][axis]) < 0.01
                         for axis in range(3)))
        if same_shot:
            pellets = previous.get("pellets", [{k: v for k, v in previous.items() if k != "offset"}])
            grouped[-1] = {**impulse, "damage": previous["damage"] + impulse["damage"],
                           "armor_save": previous["armor_save"] + impulse["armor_save"],
                           "pre_knock_msec": previous["pre_knock_msec"],
                           "kick": [previous["kick"][axis] + impulse["kick"][axis] for axis in range(3)],
                           "pellets": pellets + [{k: v for k, v in impulse.items() if k != "offset"}]}
        else:
            grouped.append(impulse)
    impulses = grouped
    deaths = world_deaths(clean, names)
    results = []
    for index, death in enumerate(deaths):
        record = {"victim": death["victim"], "death_time": death["time"], "death_origin": death["origin"],
                  "client": clients.get(death["victim"]), "impulse": None, "status": "navigation"}
        if record["client"] is None or death["time"] is None:
            record["reason"] = "victim client or death time not logged"
            results.append(record)
            continue
        previous = max((d["offset"] for d in deaths[:index] if d["victim"] == death["victim"]), default=-1)
        prior = [i for i in impulses if i["client"] == record["client"] and previous < i["offset"] < death["offset"]
                 and 0 <= death["time"] - i["time"] <= window_msec]
        enemy, other = [], set()
        for impulse in prior:
            ok, why = is_enemy(record["client"], impulse["attacker"], impulse["time"], gametype, reports)
            impulse = {k: v for k, v in impulse.items() if k != "offset"}
            impulse["enemy_reason"] = why
            (enemy.append(impulse) if ok else other.add(why))
        if not enemy:
            record["reason"] = ("no logged impulse within the window" if not prior else
                                "only non-enemy impulses: " + ", ".join(sorted(other)))
            results.append(record)
            continue
        # Pushes after the enemy's are reported, not replayed: a replay from
        # the enemy's post-impulse state that falls under every input shows
        # the enemy's impulse alone decided the fall, and one that lands
        # leaves the death a navigation failure whatever came later.
        record["later_impulses"] = [{k: v for k, v in i.items() if k != "offset"}
                                    for i in prior if i["time"] > enemy[-1]["time"]]
        record["impulse"] = enemy[-1]
        record["earlier_candidates"] = [
            {"impulse": earlier,
             "later_impulses": [{k: v for k, v in i.items() if k != "offset"}
                                for i in prior if i["time"] > earlier["time"]]}
            for earlier in reversed(enemy[:-1])]
        record["status"] = "impulse-candidate"
        record["reason"] = "enemy impulse precedes the death; replay required"
        results.append(record)
    return results


def replay_state_complete(impulse: dict) -> bool:
    """Never qualify a fall using spawn defaults or a rejected probe setup."""
    limits = {"health": (1, 1000), "knock_msec": (0, 1000), "armor": (0, 1000),
              "damage": (0, 1000), "armor_save": (0, 1000), "pre_knock_msec": (0, 1000)}
    if not all(isinstance(impulse.get(key), int) and lo <= impulse[key] <= hi
               for key, (lo, hi) in limits.items()):
        return False
    return impulse["health"] + impulse["damage"] <= 1000 and impulse["armor"] + impulse["armor_save"] <= 1000


def trials_for(impulse: dict) -> list[dict]:
    """The decisive fixed-input trials and the informative controller trial."""
    ox, oy, oz = impulse["origin"]
    vx, vy, vz = impulse["velocity"]
    state = f"{ox:.3f} {oy:.3f} {oz:.3f} {vx:.3f} {vy:.3f} {vz:.3f}"
    trials = []
    for k in range(AIR_HEADINGS):
        angle = 2.0 * math.pi * k / AIR_HEADINGS
        trials.append({"name": f"air{k:02d}", "decisive": True,
                       "command": f"bot_probeAir 1 {state} {math.cos(angle):.4f} {math.sin(angle):.4f} 0"})
    trials.append({"name": "none", "decisive": True, "command": f"bot_probeAir 1 {state} 0 0 0"})
    trials.append({"name": "controller", "decisive": False,
                   "command": f"bot_probeMove 1 {state} {ox:.3f} {oy:.3f} {oz:.3f}"})
    # Fall damage is survived or not at the health the hit left, and the
    # knockback time takes ground friction and control away as it did.
    health = impulse.get("health")
    if replay_state_complete(impulse):
        knock, armor = impulse["knock_msec"], impulse["armor"]
        for trial in trials:
            trial["setup"] = [f"bot_probeHealth 1 {health} {knock} 0 0 0 {armor}"]
    # The counterfactual: the same moment without the impulse. A bot already
    # falling into nothing before a machine gun's tap was not thrown by it.
    kick = impulse.get("kick")
    if kick is not None:
        px, py, pz = (vx - kick[0], vy - kick[1], vz - kick[2])
        before = f"{ox:.3f} {oy:.3f} {oz:.3f} {px:.3f} {py:.3f} {pz:.3f}"
        flat = math.hypot(px, py)
        bx, by = ((-px / flat, -py / flat) if flat > 1.0 else (0.0, 0.0))
        counterfactual = [
            {"name": "preNone", "command": f"bot_probeAir 1 {before} 0 0 0"},
            {"name": "preBrake", "command": f"bot_probeAir 1 {before} {bx:.4f} {by:.4f} 0"},
            {"name": "preController", "command": f"bot_probeMove 1 {before} {ox:.3f} {oy:.3f} {oz:.3f}"},
        ]
        for trial in counterfactual:
            trial.update(decisive=False, counterfactual=True)
            if replay_state_complete(impulse):
                trial["setup"] = [f"bot_probeHealth 1 {health + impulse['damage']} {impulse['pre_knock_msec']} 0 0 0 {armor + impulse['armor_save']}"]
        trials += counterfactual
    return trials


def assess_trials(sample: str, trials: list[dict], bot: str = "Rhodes") -> list[dict]:
    """Per trial: armed exactly once, died to the world, still grounded alive at the end."""
    outcomes = []
    for trial in trials:
        begin = f"MP_BOT_TRIAL_BEGIN {trial['name']}"
        end = f"MP_BOT_TRIAL_END {trial['name']}"
        starts = list(re.finditer(rf"^{re.escape(begin)}$", sample, re.M))
        ends = list(re.finditer(rf"^{re.escape(end)}$", sample, re.M))
        if len(starts) != 1 or len(ends) != 1 or starts[0].end() >= ends[0].start():
            outcomes.append({**trial, "armed": False, "died": None, "grounded": None, "survived": False,
                             "reason": "trial markers missing"})
            continue
        body = sample[starts[0].end():ends[0].start()]
        # Traversal regressions can name their exact marker. Causal impulse
        # envelopes retain the original AIR/MOVE restriction by default.
        marker = trial.get("probe_type")
        if marker is not None and marker not in ("AIR", "MOVE", "JUMP", "DROP", "PAD", "DODGE"):
            raise ValueError(f"unknown traversal probe marker: {marker}")
        probes = re.findall(r"^BOT_(AIR|MOVE|JUMP|DROP|PAD|DODGE)_PROBE client=1 ", body, re.M)
        setup_errors = re.findall(r"^(?:usage: bot_probe\w+|bot_probe\w+:|Unknown command ['\"]bot_probe).*$", body, re.M)
        armed = (not setup_errors and len(probes) == 1
                 and probes[0] in ((marker,) if marker else ("AIR", "MOVE")))
        died = any(match.group(1) == bot for match in WORLD_DEATH.finditer(body))
        grounded = re.search(r"^BOT_PROBE_GROUNDED client=1 .* health=(\d+)$", body, re.M)
        terminal = re.findall(r"^MP_PHYSICS slot=1 grounded=([01]) health=(-?\d+) velocity=.*$", body, re.M)
        final_grounded = bool(terminal and terminal[-1][0] == "1")
        final_health = int(terminal[-1][1]) if terminal else None
        release_settled = True
        if trial.get("release_input_after_msec"):
            marker = f"MP_BOT_RELEASE_INPUT {trial['name']}"
            sections = re.split(rf"^{re.escape(marker)}$", body, flags=re.M)
            releases = re.findall(r"^MP_PHYSICS slot=1 grounded=([01]) health=(-?\d+) velocity=([-\d.]+) ([-\d.]+) ([-\d.]+)$",
                                  sections[0], re.M) if len(sections) == 2 else []
            release_settled = bool(releases and releases[-1][0] == "1" and int(releases[-1][1]) > 0
                                   and sum(float(v) ** 2 for v in releases[-1][2:]) <= 1.0)
        survived = (armed and not died and grounded is not None and int(grounded.group(1)) > 0
                    and release_settled and final_grounded and final_health > 0)
        outcomes.append({**trial, "armed": armed, "died": died, "grounded": grounded is not None,
                         "setup_errors": setup_errors,
                         "final_grounded": final_grounded, "final_health": final_health, "release_settled": release_settled,
                         "survived": survived,
                         "reason": ("probe setup rejected" if setup_errors else "not armed" if not armed else "died to the world" if died else
                                    "final state missing" if not terminal else
                                    "not alive and grounded at the end" if not survived else "landed alive")})
    return outcomes


def decide(candidate: dict, outcomes: list[dict] | None, session_ok: bool) -> dict:
    """Exclude only when every post-hit trial, including the controller, died."""
    decided = dict(candidate)
    decided["replay"] = outcomes
    if candidate.get("status") != "impulse-candidate":
        return decided
    if not replay_state_complete(candidate["impulse"]):
        decided.update(status="navigation", reason="health, armour or knockback replay state missing or out of range (fail closed)")
        return decided
    if not session_ok or not outcomes:
        decided.update(status="navigation", reason="impulse replay did not complete (fail closed)")
        return decided
    decisive = [o for o in outcomes if o["decisive"]]
    if len(decisive) != AIR_HEADINGS + 1 or not all(o["armed"] for o in decisive):
        decided.update(status="navigation", reason="an impulse replay trial did not arm (fail closed)")
        return decided
    survivors = [o["name"] for o in outcomes if o["survived"] and not o.get("counterfactual")]
    if survivors:
        decided.update(status="navigation",
                       reason="recoverable: replay survived with " + ", ".join(survivors))
        return decided
    if not all(o["died"] for o in decisive):
        decided.update(status="navigation", reason="a decisive trial neither survived nor died (fail closed)")
        return decided
    controller = [o for o in outcomes if o["name"] == "controller"]
    if len(controller) != 1 or not controller[0]["armed"] or not controller[0]["died"]:
        decided.update(status="navigation", reason="controller replay missing, unarmed or unresolved (fail closed)")
        return decided
    # The impulse has to make the difference: the same moment without it
    # must be survivable, or the bot was already lost to navigation.
    before = [o for o in outcomes if o.get("counterfactual")]
    if not before or not any(o["survived"] for o in before):
        decided.update(status="navigation",
                       reason="unrecoverable after the impulse, but not shown survivable without it (fail closed)")
        return decided
    decided.update(status="excluded-impulse",
                   reason="enemy impulse; every fixed air input and no input fell to the world in replay, "
                          "and the same moment without the impulse landed (" +
                          ", ".join(o["name"] for o in before if o["survived"]) + ")")
    return decided


def summarise(decided: list[dict]) -> dict:
    navigation = [d for d in decided if d["status"] != "excluded-impulse"]
    excluded = [d for d in decided if d["status"] == "excluded-impulse"]
    return {"policy_version": POLICY_VERSION, "world_deaths": len(decided), "navigation_world_deaths": len(navigation),
            "excluded_impulse_deaths": len(excluded), "deaths": decided}


def replay_chain(candidate: dict, run) -> dict:
    """Trace at most eight earlier hits only after a complete already-lost replay.

    A surviving input, missing trial or incomplete session never permits a
    fallback. Every attempted cause still needs the unchanged exclusion proof.
    After that gate, test the strongest earlier kicks first: a stream of taps
    during an already-lost rocket flight must not consume the whole search.
    Logged vectors have three decimal places. Group their sub-centimetre
    magnitude noise so equal-strength hits retain reverse chronological order.
    """
    attempts = []
    earlier = sorted(candidate.get("earlier_candidates", []), key=lambda row:
        (round(math.sqrt(sum(component * component for component in row["impulse"]["kick"])), 2),
         row["impulse"]["time"]),
        reverse=True)
    choices = [candidate] + [{**candidate, **row} for row in earlier[:8]]
    for attempt, choice in enumerate(choices):
        result = run(choice, attempt)
        attempts.append({k: v for k, v in result.items() if k != "earlier_candidates"})
        rows = result.get("replay") or []
        before = [row for row in rows if row.get("counterfactual")]
        # decide() has already checked the fixed-input envelope. Also require
        # the controller and all three counterfactuals to arm and die.
        if (result.get("reason") != "unrecoverable after the impulse, but not shown survivable without it (fail closed)"
                or len(rows) != AIR_HEADINGS + 5 or len(before) != 3
                or not all(row["armed"] and row["died"] for row in rows)):
            break
    result["causal_replays"] = attempts
    return result


def replay_session(runtime: Path, executable: str, basepath: Path, output: Path, map_name: str,
                   gametype: str, trials: list[dict], port: int, timeout: int) -> tuple[bool, str, int]:
    """Run trials one after another with one isolated bot; returns (completed, clean log, exit code)."""
    from mp_round_remote_smoke import verify_runtime_is_current
    verify_runtime_is_current(runtime, executable)
    identity_paths = [runtime / executable, *sorted((runtime / "baseoq4").glob("game-mp*"))]
    identity_paths = [path for path in identity_paths if path.is_file() and path.suffix not in (".pdb", ".lib", ".exp")]
    def identity():
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in identity_paths}
    before_identity = identity()
    game = output / "save/baseoq4"
    game.mkdir(parents=True, exist_ok=False)
    first = trials[0]["command"].split()
    observer = f"{float(first[2]):.0f} {float(first[3]):.0f} {float(first[4]) + 480:.0f} 60 0 0"
    script = ['set g_autoExecAfterMapLoad ""', "notarget", "god", "noclip", "set bot_debug 2", "set bot_chat 0", f"setviewpos {observer}",
              "addbot Rhodes 4 exact", "waitMsec 2000", "navmesh info", "echo MP_BOT_REPLAY_BEGIN"]
    for trial in trials:
        words = trial["command"].split()
        neutral = " ".join(words[:5] + ["0", "0", "0"] + words[8:])
        if "neutral_command" in trial:
            # Positive flight regressions may prepare an airborne physics
            # state instead of settling on the floor beneath their saved pose.
            # Causal envelopes keep their fixed preparation unchanged.
            if trial.get("expect_survival") is not True or trial.get("counterfactual"):
                raise ValueError("custom neutral preparation is only for positive recovery regressions")
            neutral = trial["neutral_command"]
        # Restore the world as well as the bot. A preceding trial can collect
        # the health pickup that makes the next heading survivable. Restart
        # repopulates items; player Init clears god/noclip but preserves notarget.
        script += ["set bot_pause 1", "serverMapRestart", "waitMsec 1000", "god", "noclip", f"setviewpos {observer}",
                   neutral, "waitMsec 64", f"echo MP_BOT_TRIAL_BEGIN {trial['name']}",
                   *trial.get("setup", []), trial["command"], "set bot_pause 0"]
        release = trial.get("release_input_after_msec")
        observe = trial.get("observe_msec", TRIAL_OBSERVE_MSEC)
        if not isinstance(observe, int) or not TRIAL_OBSERVE_MSEC <= observe <= 30000:
            raise ValueError("observation may extend, never shorten, the six-second terminal check")
        if release is not None:
            if not 0 < release < observe:
                raise ValueError("input release must be inside the observation window")
            # This optional isolated-recovery mode stops issuing commands after
            # the controller has settled. World/player physics keeps running;
            # assessment requires rest BEFORE release as well as final survival.
            script += [f"waitMsec {release}", "openq4_reportMPState",
                       f"echo MP_BOT_RELEASE_INPUT {trial['name']}", "set bot_pause 1",
                       f"waitMsec {observe - release}"]
        else:
            sample = trial.get("sample_msec", observe)
            if not isinstance(sample, int) or not 16 <= sample <= observe:
                raise ValueError("physics sample interval must be between 16 ms and the observation length")
            remaining = observe
            while remaining > 0:
                step = min(sample, remaining)
                script += [f"waitMsec {step}"]
                remaining -= step
                if remaining:
                    script += ["openq4_reportMPState"]
        script += ["openq4_reportMPState",
                   f"echo MP_BOT_TRIAL_END {trial['name']}",
                   # A bot that died, or walked off later, would refuse or
                   # spoil the next trial: replace it with a fresh one.
                   # The removed client takes a moment to leave its slot.
                   "removebot Rhodes", f"waitMsec {TRIAL_REMOVE_MSEC}", "addbot Rhodes 4 exact", f"waitMsec {TRIAL_RESPAWN_MSEC}"]
    script += ["echo MP_BOT_REPLAY_END", "quit"]
    config = "\n".join(script) + "\n"
    # CmdSystem's text buffer is 64 KiB, including commands already queued
    # around exec. Dense extended probes must split trials or sample less
    # often; silently dropping the script leaves a hidden server running.
    if len(config.encode("utf-8")) >= 60 * 1024:
        raise ValueError("replay script exceeds the command-buffer budget; split trials or increase sample_msec")
    (game / "impulse_replay.cfg").write_text(config, encoding="utf-8")
    cvars = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "sys_consoleWindow": "0", "in_tty": "0", "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
        "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "s_noSound": "1",
        "r_mode": "-1", "r_customWidth": "640", "r_customHeight": "480", "r_renderApi": "vulkan",
        "r_swapInterval": "0", "com_maxfps": "60", "ui_retained": "0",
        "fs_basepath": str(basepath), "fs_savepath": str(game.parent), "fs_devpath": str(game.parent),
        "fs_game": "baseoq4", "com_gameMode": "MP", "logFile": "2", "logFileName": "logs/openq4.log",
        "developer": "1", "ui_autoJoin": "1", "ui_spectate": "Play", "ui_name": "BotAuditObserver",
        "net_serverDedicated": "0", "net_LANServer": "1", "net_ip": "127.0.0.1", "net_enableIPv6": "0",
        "net_allowCheats": "1", "sv_cheats": "1", "net_port": str(port), "si_pure": "0",
        # Stay in playable warmup: replacing the sole bot during a live match
        # forfeits it and puts later probes into the spectator/intermission state.
        "si_gameType": gametype, "si_maxPlayers": "4", "si_minPlayers": "4", "si_warmup": "1", "si_useReady": "1",
        "si_warmupReadyPercentage": "1", "ui_ready": "0",
        "si_fragLimit": "999", "si_captureLimit": "999", "si_timeLimit": "0", "g_matchProfile": "casual",
        "bot_enable": "1", "bot_minPlayers": "0", "bot_pause": "0", "bot_characters": "1",
        "bot_skillVariance": "0", "com_skipLoadingContinue": "1",
        "g_autoExecAfterMapLoad": "impulse_replay.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
    }
    command = [str(runtime / executable)]
    for key, value in cvars.items():
        command += ["+set", key, value]
    command += ["+spawnServer", map_name]
    (output / "launch.json").write_text(json.dumps(command, indent=2) + "\n", encoding="utf-8")
    with (output / "console.log").open("w", encoding="utf-8") as console:
        process = subprocess.Popen(command, cwd=runtime, stdout=console, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            code = -1
    log_path = game / "logs/openq4.log"
    clean = strip_colours(log_path.read_text(encoding="utf-8", errors="replace")) if log_path.exists() else ""
    clean = "\n" + "\n".join(line.rstrip() for line in clean.splitlines()) + "\n"
    fatal = re.search(r"^.*(?:ERROR:|FATAL:|Assertion failed).*$", clean, re.M)
    try:
        unchanged = identity() == before_identity
    except OSError:
        unchanged = False
    completed = code == 0 and "\nMP_BOT_REPLAY_END\n" in clean and not fatal and unchanged
    (output / "run.json").write_text(json.dumps({
        "complete": completed, "exit": code, "runtime_unchanged": unchanged,
        "identity": before_identity, "log": str(log_path), "trials": trials,
    }, indent=2) + "\n", encoding="utf-8")
    return completed, clean, code


def replay_identity(runtime: Path, executable: str) -> dict[str, str]:
    paths = [runtime / executable, *sorted((runtime / "baseoq4").glob("game-mp*")),
             *sorted(runtime.glob("renderer-*")), runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4"]
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths
            if p.is_file() and p.suffix not in (".pdb", ".lib", ".exp")}


def classify_log(clean: str, names: set[str], gametype: str, map_name: str, runtime: Path, executable: str,
                 basepath: Path, output: Path, port: int) -> dict:
    """Classify a match log and replay every impulse candidate; returns the summary."""
    decided = []
    initial_identity = replay_identity(runtime, executable)
    for index, candidate in enumerate(classify(clean, names, gametype)):
        if candidate["status"] != "impulse-candidate":
            decided.append(decide(candidate, None, False))
            continue
        def run(choice, attempt):
            if not replay_state_complete(choice["impulse"]):
                return decide(choice, None, False)
            trials = trials_for(choice["impulse"])
            folder = output / f"replay-{index:02d}-{candidate['victim']}-cause{attempt}"
            per_trial = TRIAL_OBSERVE_MSEC + TRIAL_REMOVE_MSEC + TRIAL_RESPAWN_MSEC + 200
            samples, batches = [], []
            completed, code = True, None
            try:
                # Every trial already restores the world and bot. Bound each
                # process lifetime too; a late failure must not waive any
                # earlier/later trial or the counterfactual proof.
                for first in range(0, len(trials), 8):
                    batch = trials[first:first + 8]
                    if replay_identity(runtime, executable) != initial_identity:
                        raise RuntimeError("runtime changed between causality replay batches")
                    timeout = 150 + len(batch) * per_trial * 3 // 2000
                    done, sample_log, code = replay_session(runtime, executable, basepath,
                        folder / f"batch-{first // 8:02d}", map_name, gametype, batch, port, timeout)
                    batches.append({"first": first, "trials": len(batch), "complete": done, "exit": code})
                    samples.append(sample_log.partition("\nMP_BOT_REPLAY_BEGIN\n")[2])
                    completed = completed and done
                    if replay_identity(runtime, executable) != initial_identity:
                        raise RuntimeError("runtime changed during causality replay batches")
                    if not done:
                        break
            except (OSError, RuntimeError, SystemExit) as error:
                result = decide(choice, None, False)
                result.update(replay_dir=str(folder), replay_exit=None, replay_failure=str(error), replay_batches=batches)
                return result
            sample = "\n".join(samples)
            result = decide(choice, assess_trials(sample, trials), completed)
            result.update(replay_dir=str(folder), replay_exit=code, replay_batches=batches)
            return result
        decided.append(replay_chain(candidate, run))
    return summarise(decided)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--report", type=Path, required=True, help="an mp_bot_audit.py report.json")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--port", type=int, default=28973)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    runtime = args.runtime_dir.resolve()
    for original, digest in report.get("runtime_identity", {}).items():
        source = Path(original)
        current = runtime / ("baseoq4" if source.parent.name.lower() == "baseoq4" else "") / source.name
        if not current.is_file() or hashlib.sha256(current.read_bytes()).hexdigest() != digest:
            parser.error(f"causality requires the match's recorded runtime: {source.name} differs")
    output = args.output_dir.resolve()
    if output.exists():
        parser.error("use a new output directory to preserve earlier evidence")
    output.mkdir(parents=True)
    from mp_bot_audit import NAMES
    clean = strip_colours(Path(report["log"]).read_text(encoding="utf-8", errors="replace"))
    started = time.monotonic()
    summary = classify_log(clean, set(NAMES[:report["bots"]]), report["gametype"], report["map"],
                           args.runtime_dir.resolve(), args.executable_name, args.basepath.resolve(), output, args.port)
    summary.update(report=str(args.report.resolve()), elapsed_seconds=round(time.monotonic() - started, 1),
                   policy=__doc__.split("\n\n")[1].strip())
    (output / "classification.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: summary[k] for k in ("world_deaths", "navigation_world_deaths", "excluded_impulse_deaths")}))
    for death in summary["deaths"]:
        print(f"  {death['victim']} at {death['death_time']}: {death['status']} - {death['reason']}")
    return int(summary["navigation_world_deaths"] > 0)


if __name__ == "__main__":
    raise SystemExit(main())
