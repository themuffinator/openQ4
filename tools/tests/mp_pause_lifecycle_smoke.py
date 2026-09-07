#!/usr/bin/env python3
"""Qualify managed match/round/arena pause boundaries with two windowed peers."""
from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path

from mp_series_smoke import ROOT, SeriesRun, read_log
from mp_pause_world_smoke import fields


class LifecycleRun(SeriesRun):
    def __init__(self, args):
        super().__init__(args)
        self.samples = {}

    def launch_overrides(self, role):
        if role != "host":
            return {}
        return {"si_gameType": self.args.gametype, "si_minPlayers": "2",
                "g_matchProfile": "competitive_tourney" if self.args.gametype == "Tourney" else "competitive_round"}

    def admission(self):
        if self.args.gametype != "Tourney":
            return super().admission()
        # Tourney contestants intentionally spectate until their arena opens.
        # The accepted wish to play and game admission identify them in warmup.
        self.wait("two admitted Tourney contestants", lambda texts: all(
            "MP_SERIES_LOADED" in texts[role] and re.search(
                rf"MP_PLAYER slot={slot} .*wantSpectate=0 ingame=1",
                texts[role].rsplit("\nMP_STATE ", 1)[-1])
            for slot, role in enumerate(self.games)))

    def open_menu(self, role, review=False):
        if self.args.gametype != "Tourney" or review:
            return super().open_menu(role, review)
        self.perform(role, "Tourney menu", ["openq4_assertMenuActivation 3000 game",
            "waitMsec 1500", "openq4_guiSet desktop::active 1", "openq4_guiSet desktop::dest 21",
            "GuiEvent hideMain", "GuiEvent chooseCurr", "GuiEvent resetMain0", "waitMsec 1500",
            "openq4_guiSet desktop::match_tab 0"])

    def state(self, text):
        latest = text.rsplit("\nMP_STATE ", 1)[-1]
        first = latest.splitlines()[0] if latest else ""
        arenas = [fields(line) for line in latest.splitlines() if line.startswith("MP_TOURNEY ")]
        return fields(first), arenas

    def sample(self, role, label):
        name = f"{label}-{role}"
        self.perform(role, name, ["openq4_reportMPState", f'screenshot "screenshots/{name}.tga"'])
        text = read_log(self.logs[role])
        state, arenas = self.state(text)
        players = [fields(line) for line in text.rsplit("\nMP_STATE ", 1)[-1].splitlines()
                   if line.startswith("MP_PLAYER ")]
        view = self.decoder.decode(self.games[role] / f"match-probe/view-{0 if role == 'host' else 1}.bin")
        sample = {"state": state, "arenas": arenas, "players": players, "view": view}
        self.samples[name] = sample
        return sample

    def hold(self, label, phase, round_state=None, arena_state=None):
        self.action("host", "tech_pause")
        for role in self.games:
            self.await_view(role, label + " paused", lambda v: v["pause"] == 2 and v["phase"] == phase)
        before = {role: self.sample(role, label + "-start") for role in self.games}
        self.perform("host", label + " hold", ["waitMsec 15000"])
        after = {role: self.sample(role, label + "-end") for role in self.games}
        for role in self.games:
            a, b = before[role], after[role]
            va, vb = a["view"], b["view"]
            if (va["phase"] != phase or vb["phase"] != phase or
                va["pause"] != 2 or vb["pause"] != 2 or va["match"] != vb["match"] or
                vb["engine"] - va["engine"] < 15000):
                raise RuntimeError(f"{role} countdown clocks advanced during {label}")
            for key in ("phase", "session_phase", "round", "round_number", "marine", "strogg"):
                if a["state"].get(key) != b["state"].get(key):
                    raise RuntimeError(f"{role} changed {key} during {label}")
            arena_identities = lambda rows: [{k: v for k, v in row.items() if k != "remaining"} for row in rows]
            if arena_identities(a["arenas"]) != arena_identities(b["arenas"]):
                raise RuntimeError(f"{role} arena advanced during {label}")
            if round_state is not None and (a["state"].get("round") != str(round_state) or
                    va["round"] != round_state or vb["round"] != round_state):
                raise RuntimeError(f"{role} missed the round countdown")
            if arena_state is not None and (not a["arenas"] or
                    any(row["state"] != str(arena_state) for row in a["arenas"])):
                raise RuntimeError(f"{role} missed the arena countdown")
            if role == "host" and round_state is not None:
                remaining = int(a["state"]["round_remaining"])
                if remaining <= 0 or b["state"]["round_remaining"] != str(remaining):
                    raise RuntimeError("the round's remaining countdown was consumed by wall time")
            if role == "host" and arena_state is not None:
                if any(int(old["remaining"]) <= 0 or old["remaining"] != new["remaining"]
                       for old, new in zip(a["arenas"], b["arenas"])):
                    raise RuntimeError("an arena's remaining countdown was consumed by wall time")
        self.action("host", "resume")
        for role in self.games:
            self.await_view(role, label + " resumed", lambda v: v["pause"] == 0)

    def run(self):
        tourney = self.args.gametype == "Tourney"
        self.launch("host", "mp/q4tourney1" if tourney else "mp/q4dm1")
        self.wait("host warmup", lambda t: "MP match phase: 0 -> 1" in t["host"])
        self.launch("client")
        self.admission()
        for role in self.games:
            self.open_menu(role)
            self.perform(role, "status tab", ["openq4_guiSet desktop::match_tab 0"])
        self.action("host", "force_ready", confirm=True)
        self.await_view("host", "match countdown", lambda v: v["phase"] == 2)
        self.hold("match-countdown", 2)
        self.await_view("host", "match live", lambda v: v["phase"] == 3, timeout=60)

        def countdown(texts):
            state, arenas = self.state(texts["host"])
            return (any(row["state"] == "1" for row in arenas) if tourney
                    else state.get("round") == "1")
        self.wait("arena or round countdown", countdown)
        self.hold("arena-countdown" if tourney else "round-countdown", 3,
                  round_state=None if tourney else 1, arena_state=1 if tourney else None)

        def playing(texts):
            state, arenas = self.state(texts["host"])
            return (any(row["state"] == "2" for row in arenas) if tourney
                    else state.get("round") == "2")
        self.wait("resumed arena or round gameplay", playing)
        for slot, role in enumerate(self.games):
            sample = self.sample(role, "resumed-gameplay")
            local = next((row for row in sample["players"] if row["slot"] == str(slot)), None)
            if local is None or any(local[key] != expected for key, expected in (
                    ("spectating", "0"), ("wantSpectate", "0"), ("ingame", "1"))):
                raise RuntimeError(f"{role} did not enter resumed gameplay")
        self.action("host", "tech_pause")
        for role in self.games:
            self.await_view(role, "live pause", lambda v: v["pause"] == 2)
        self.action("host", "abort", confirm=True)
        for role in self.games:
            self.await_view(role, "abort from pause", lambda v: v["phase"] == 5 and v["pause"] == 0)
            self.open_menu(role, review=True)
            self.sample(role, "aborted-review")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-pause-lifecycle")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--gametype", choices=("Clan Arena", "Tourney"), default="Clan Arena")
    parser.add_argument("--timeout", type=int, default=480)
    args = parser.parse_args()
    args.duel, args.network_trace = False, False
    run = LifecycleRun(args)
    failures = []
    try:
        run.run()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        failures.append(str(error))
    finally:
        exits = run.close()
    if len(exits) != 2 or any(exits.values()):
        failures.append(f"incomplete shutdown: {exits}")
    result = {"status": "fail" if failures else "pass", "failures": failures, "exits": exits,
              "gametype": args.gametype, "samples": run.samples,
              "logs": {r: str(p) for r, p in run.logs.items()}}
    (run.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "samples"}, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
