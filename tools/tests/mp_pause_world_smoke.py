#!/usr/bin/env python3
"""Qualify repeated MP pauses against real stock entities without device input."""
from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path

from mp_series_smoke import ROOT, SeriesRun, read_log


def fields(line):
    return {key: value.strip('"') for key, value in re.findall(r'(\w+)=("[^"]*"|\S+)', line)}


class WorldRun(SeriesRun):
    def __init__(self, args):
        super().__init__(args)
        self.samples = {}

    def sample(self, role, label):
        marker = "MP_WORLD_SAMPLE_" + label.replace("-", "_")
        names = ["player1" if role == "host" else "player2"]
        if role == "host":
            names += ["oq4_pause_rotater", "oq4_pause_rocket", "item_armor_large_1"]
        self.perform(role, label, ["echo " + marker,
            *("openq4_reportMPWorld " + name for name in names),
            f'screenshot "screenshots/{label}-{role}.tga"'])
        section = read_log(self.logs[role]).rsplit("\n" + marker + " ", 1)[-1]
        grouped = {kind: [fields(line) for line in section.splitlines() if line.startswith(kind + " ")]
                   for kind in ("MP_WORLD", "MP_WORLD_ANIM", "MP_WORLD_POWERUP", "MP_WORLD_WEAPON", "MP_WORLD_MISSING")}
        view = self.decoder.decode(self.games[role] / f"match-probe/view-{0 if role == 'host' else 1}.bin")
        result = {"view": view, "world": grouped}
        self.samples[f"{label}-{role}"] = result
        return result

    @staticmethod
    def entity(sample, name):
        return next((e for e in sample["world"]["MP_WORLD"] if e["name"] == name), None)

    @staticmethod
    def check_frozen(first, last, hold):
        a, b = first["view"], last["view"]
        if not (a["phase"] == b["phase"] == 3 and a["pause"] == b["pause"] == 2 and
                a["match"] == b["match"] and b["engine"] - a["engine"] >= hold):
            raise RuntimeError("accepted pause clocks did not remain frozen")
        before, after = first["world"], last["world"]
        if before["MP_WORLD_MISSING"] or after["MP_WORLD_MISSING"]:
            raise RuntimeError("a world entity disappeared during pause")
        if not before["MP_WORLD"] or len(before["MP_WORLD"]) != len(after["MP_WORLD"]):
            raise RuntimeError("world diagnostic returned incomplete entities")
        for old, new in zip(before["MP_WORLD"], after["MP_WORLD"]):
            for key in ("entity", "name", "type", "hidden", "health", "origin", "velocity", "angles"):
                if old[key] != new[key]:
                    raise RuntimeError(f"{old['name']} changed {key} during pause: {old[key]} -> {new[key]}")
            if old["frozen"] != "1" or new["frozen"] != "1":
                raise RuntimeError("world sample was outside the pause boundary")
        for kind in ("MP_WORLD_ANIM", "MP_WORLD_POWERUP", "MP_WORLD_WEAPON"):
            if before[kind] != after[kind]:
                raise RuntimeError(f"{kind} advanced while gameplay was frozen")
        if not before["MP_WORLD_ANIM"] or not before["MP_WORLD_WEAPON"]:
            raise RuntimeError("no active player/weapon animation to qualify")

    def run(self):
        self.launch("host")
        self.wait("host warmup", lambda t: "MP match phase: 0 -> 1" in t["host"])
        self.launch("client")
        self.admission()
        for role in self.games:
            self.open_menu(role)
            self.perform(role, "status tab", ["openq4_guiSet desktop::match_tab 0"])
        self.action("host", "force_ready", confirm=True)
        self.await_view("host", "live world", lambda v: v["phase"] == 3)
        self.perform("host", "stock world fixtures", [
            'spawn func_rotating name oq4_pause_rotater model models/mapobjects/strogg/barrels/medium1_low.lwo origin "-4720 6640 340" speed 35 nopush 1 solid 0 neverDormant 1',
            'openq4_launchMPTestProjectile projectile_rocket_mp oq4_pause_rocket "-4768 6600 360" "0 -1 0" 4 45',
            "trigger item_armor_large_1", "give quad", "waitMsec 500"])
        for number, hold in ((1, 28000), (2, 3500)):
            self.action("host", "tech_pause")
            for role in self.games:
                self.await_view(role, "world paused", lambda v: v["pause"] == 2)
            first = {role: self.sample(role, f"pause-{number}-start") for role in self.games}
            self.perform("host", "hold frozen world", [f"waitMsec {hold}"])
            last = {role: self.sample(role, f"pause-{number}-end") for role in self.games}
            for role in self.games:
                self.check_frozen(first[role], last[role], hold)
            armor = self.entity(last["host"], "item_armor_large_1")
            if armor is None or armor["hidden"] != "1":
                raise RuntimeError("the collected stock armor respawned on wall time")
            if not first["host"]["world"]["MP_WORLD_POWERUP"]:
                raise RuntimeError("the test did not retain an active timed powerup")
            self.action("host", "resume")
            for role in self.games:
                self.await_view(role, "world resumed", lambda v: v["pause"] == 0)
            self.perform("host", "advance resumed world", ["waitMsec 1000"])
            resumed = self.sample("host", f"pause-{number}-resumed")
            for name, key in (("oq4_pause_rotater", "angles"), ("oq4_pause_rocket", "origin")):
                old, new = self.entity(last["host"], name), self.entity(resumed, name)
                if old is None or new is None or old[key] == new[key]:
                    raise RuntimeError(f"{name} did not resume its physical motion")
            if resumed["world"]["MP_WORLD_ANIM"] == last["host"]["world"]["MP_WORLD_ANIM"]:
                raise RuntimeError("player and weapon animation did not resume")
        self.perform("host", "remaining gameplay deadlines", ["waitMsec 45000"])
        expired = self.sample("host", "deadlines-expired")
        if expired["world"]["MP_WORLD_POWERUP"]:
            raise RuntimeError("timed powerup never expired after resumption")
        armor = self.entity(expired, "item_armor_large_1")
        if armor is None or armor["hidden"] != "0":
            raise RuntimeError("stock armor failed to respawn after sufficient gameplay time")
        if self.entity(expired, "oq4_pause_rocket") is not None:
            raise RuntimeError("the projectile's posted removal never ran after resumption")
        self.action("host", "abort", confirm=True)
        for role in self.games:
            self.await_view(role, "final review", lambda v: v["phase"] == 5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-pause-world")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--timeout", type=int, default=480)
    args = parser.parse_args()
    args.duel, args.network_trace = False, False
    run = WorldRun(args)
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
              "samples": run.samples, "logs": {r: str(p) for r, p in run.logs.items()}}
    (run.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "samples"}, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
