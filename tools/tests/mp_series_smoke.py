#!/usr/bin/env python3
"""Qualify a managed series through two windowed clients and native GUI actions."""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import read_log, write_script
from mp_view_decoder import MatchViewDecoder

ROOT = Path(__file__).resolve().parents[2]


class SeriesRun:
    def __init__(self, args, *, roles=("host", "client")):
        self.args = args
        self.output = args.output_dir.resolve()
        self.runtime = args.runtime_dir.resolve()
        self.games = {role: self.output / role / "baseoq4" for role in roles}
        self.slots = {role: slot for slot, role in enumerate(roles)}
        self.port = getattr(args, "port", 28901)
        self.logs = {role: path / "logs/openq4.log" for role, path in self.games.items()}
        self.processes, self.streams, self.loops = {}, [], {}
        self.command_numbers = {role: 0 for role in self.games}
        self.operation = 0
        self.deadline = time.monotonic() + args.timeout
        # A copied runtime can outlive further edits in the shared source tree.
        # Use its production decoder when supplied so validation uses that build's wire schema.
        pinned_decoder = self.runtime / "match-view-decode.exe"
        self.decoder = MatchViewDecoder(self.output,
            executable=pinned_decoder if pinned_decoder.is_file() else None)
        self.captures = []
        self.restarts = []
        self.blocked_artifacts = []
        for role, game in self.games.items():
            game.mkdir(parents=True, exist_ok=True)
            self.loops[role] = game / "series_commands.cfg"
            write_script(self.loops[role], ["// Waiting for the first operation."])
            write_script(game / "series_pump.cfg", ["exec series_commands.cfg", "waitMsec 250",
                         "openq4_reportMPState", "vstr oq4_series_pump"])
            self.start_script(role)

    def start_script(self, role):
        # This pump is immutable while the process runs. A transient Windows
        # read failure on the replaceable command selector cannot stop it.
        write_script(self.games[role] / "series_start.cfg", [
            'set oq4_series_cmd_1 "exec series_step_1.cfg"',
            'set oq4_series_pump "exec series_pump.cfg"',
            "echo MP_SERIES_LOADED", "exec series_pump.cfg"])

    def launch_overrides(self, role):
        return {}

    def launch(self, role, map_token="mp/q4dm1", recovery_id=0):
        a = self.args
        cvars = {
            "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
            "fs_basepath": str(a.basepath.resolve()), "fs_savepath": str(self.games[role].parent),
            "fs_devpath": str(self.games[role].parent), "fs_game": "baseoq4", "com_gameMode": "MP",
            "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
            "r_borderlessDefaultMigrated": "1", "r_mode": "-1", "r_customWidth": "960",
            "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540",
            "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
            "r_renderApi": "gl", "com_maxfps": "60", "r_swapInterval": "0", "logFile": "2",
            "logFileName": "logs/openq4.log", "developer": "1", "ui_autoJoin": "1",
            "ui_spectate": "Play", "ui_name": role, "ui_team": "Marine" if role == "host" else "Strogg",
            "net_allowCheats": "1", "com_skipLoadingContinue": "1",
            "net_verbose": "2" if a.network_trace else "0",
            "g_autoExecAfterMapLoad": "series_start.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
            "net_port": str(self.port + self.slots[role]),
        }
        if role == "host":
            cvars.update(net_serverDedicated="0", net_LANServer="1", si_pure="0", si_maxPlayers="4",
                         si_gameType="Duel" if a.duel else "Team DM", bot_minPlayers="0",
                         g_matchProfile="competitive_duel" if a.duel else "competitive_tdm",
                         g_gameReviewPause="600", si_mapCycle=";".join(f"mp/q4dm{i}" for i in range(1, 6)))
            if recovery_id:
                cvars["g_matchSeriesRecoveryId"] = f"{recovery_id:016x}"
        cvars.update(self.launch_overrides(role))
        command = [str(self.runtime / a.executable_name)]
        for key, value in cvars.items():
            command += ["+set", key, value]
        command += ["+spawnServer", map_token] if role == "host" else ["+connect", f"127.0.0.1:{self.port}"]
        (self.output / (role + "-launch.json")).write_text(json.dumps(command, indent=2), encoding="utf-8")
        stream = (self.output / (role + "-console.log")).open("w", encoding="utf-8")
        self.streams.append(stream)
        self.processes[role] = subprocess.Popen(command, cwd=self.runtime, stdout=stream, stderr=subprocess.STDOUT)

    def wait(self, label, predicate, timeout=60):
        until = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < until:
            texts = {role: read_log(path) for role, path in self.logs.items()}
            error = re.search(r"(?:FATAL(?: ERROR|:)|ERROR:|Unknown command|usage: openq4_guiSet|"
                              r"Reliable messages overflow|reliable message queue overflowed|"
                              r"competition report rejected|could not project match evidence|"
                              r"could not (?:capture|persist|finalize) competition|"
                              r"MP_CONTROL match_result_message=[^\n]* \| Rejected \||"
                              r"MP_CONTROL action requires|MP_CONTROL rejected|"
                              r"competitive finalization remains pending)[^\n]*", "\n".join(texts.values()))
            if error:
                raise RuntimeError(f"engine or diagnostic error during {label}: {error[0]}")
            if any(p.poll() is not None for p in self.processes.values()):
                raise RuntimeError("a process ended during " + label)
            if predicate(texts):
                return texts
            time.sleep(0.2)
        raise RuntimeError("timeout during " + label)

    def inject(self, role, actions):
        self.operation += 1
        self.command_numbers[role] += 1
        number = self.command_numbers[role]
        step = self.games[role] / f"series_step_{number}.cfg"
        # Retire this operation before running it and arm the next unique
        # command. Re-reading the selector can never repeat a typed action.
        write_script(step, [f'set oq4_series_cmd_{number} ""',
                     f'set oq4_series_cmd_{number + 1} "exec series_step_{number + 1}.cfg"', *actions])
        write_script(self.loops[role], [f"vstr oq4_series_cmd_{number}"])

    def perform(self, role, label, actions):
        marker = f"MP_SERIES_DONE_{self.operation + 1}"
        self.inject(role, [*actions, "openq4_matchControl report", "echo " + marker])
        self.wait(label, lambda texts: "\n" + marker + " " in texts[role])

    def view(self, role):
        self.perform(role, "accepted view", ["waitMsec 350"])
        return self.decoder.decode(self.games[role] / f"match-probe/view-{self.slots[role]}.bin")

    def await_view(self, role, label, predicate, timeout=30):
        until = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < until:
            view = self.view(role)
            if predicate(view):
                return view
        raise RuntimeError(f"timeout during {label}: {view}")

    def action(self, role, token, *, confirm=False, before=()):
        self.perform(role, "availability for " + token, [])
        opcode = "veto_select" if token.startswith("veto_") else {
            "series_stage": "series_stage_profile", "tech_pause": "tech_pause_request",
            "resume": "resume_request", "timeout": "timeout_request"}.get(token, token)
        latest = read_log(self.logs[role]).rsplit("\nMP_VIEW ", 1)[-1]
        if f"MP_CONTROL match_op_{opcode}_available=1" not in latest:
            reason = re.search(rf"MP_CONTROL match_op_{opcode}_reason=([^\n]*)", latest)
            raise RuntimeError(f"{role} cannot {token}: {reason[1] if reason else 'no accepted availability'}")
        self.perform(role, token, ["waitMsec 2200", *before,
                     "openq4_matchControl action " + ("arm_" if confirm else "") + token,
                     *(["waitMsec 100", "openq4_matchControl action confirm"] if confirm else []),
                     "waitMsec 500"])

    def open_menu(self, role, review=False):
        opening = ["openq4_matchControl open"] if review else [
            "openq4_assertMPClientActive", "openq4_assertMenuActivation 3000 game"]
        self.perform(role, "series menu", [*opening,
                     "waitMsec 1500", "openq4_guiSet desktop::active 1", "openq4_guiSet desktop::dest 21",
                     "GuiEvent hideMain", "GuiEvent chooseCurr", "GuiEvent resetMain0", "waitMsec 1500",
                     "openq4_guiSet desktop::match_tab 4"])

    def capture(self, role, label, expected):
        view = self.await_view(role, label + " authoritative state", lambda v:
            v["session"] == expected["session"] and v["phase"] == expected["phase"] and v["pause"] == expected["pause"] and
            all(v["series"][key] == expected["series"][key]
                for key in ("id", "state", "map_number", "wins", "history")))
        if not self.args.duel and view["series"]["state"] in (4, 5):
            current = next(m for m in view["series"]["maps"]
                           if m["selection"] == view["series"]["map_number"])
            competition_side = (current["side_chosen_by"] if view["side"] == current["starting_side"]
                                else 1 - current["side_chosen_by"])
            if view["competition_side"] != competition_side:
                raise RuntimeError(f"{role} recovered the wrong competition-to-game-side mapping")
        folder = self.output / "views"
        folder.mkdir(exist_ok=True)
        name = f"{label}-{role}"
        shutil.copy2(self.games[role] / f"match-probe/view-{0 if role == 'host' else 1}.bin", folder / (name + ".bin"))
        (folder / (name + ".json")).write_text(json.dumps(view, indent=2), encoding="utf-8")
        screenshot = self.games[role] / f"screenshots/{name}.tga"
        if screenshot.exists():
            raise RuntimeError(f"capture path already exists: {screenshot}")
        self.perform(role, name + " screenshot", [
            "openq4_guiGet match_series_profile_choice::choices",
            "openq4_guiGet match_series_profile_choice::values",
            "openq4_guiGet match_series_profile_choice::rect",
            f'screenshot "screenshots/{name}.tga"'])
        if not screenshot.is_file() or screenshot.stat().st_size < 18:
            raise RuntimeError(f"engine did not write screenshot: {screenshot}")
        header = screenshot.read_bytes()[:18]
        if (int.from_bytes(header[12:14], "little"), int.from_bytes(header[14:16], "little")) != (960, 540):
            raise RuntimeError(f"unexpected engine screenshot dimensions: {screenshot}")
        self.captures.append({"label": name, "session": view["session"], "series": view["series"]})
        return view

    def validate_reports(self, series_id):
        game = self.games["host"]
        final_path = game / f"match-results/series-{series_id}.json"
        report = json.loads(final_path.read_text(encoding="utf-8"))
        needed = self.args.best_of // 2 + 1
        played = self.args.best_of if self.args.full_distance else needed
        final_score = [needed, needed - 1 if self.args.full_distance else 0]
        if (report["schema"] != 1 or report["seriesId"] != series_id or
            report["profile"]["bestOf"] != self.args.best_of or
            report["seriesScore"] != final_score or report["final"]["outcome"] != "complete" or
            report["final"]["winner"] != 0):
            raise RuntimeError("persisted final series identity, score or winner differs")
        maps = report["maps"]
        if maps["accepted"] != played or maps["dropped"] != 0 or len(maps["entries"]) != played:
            raise RuntimeError("persisted series map count differs or contains drops")
        paths, sessions = [str(final_path)], []
        for number, entry in enumerate(maps["entries"], 1):
            winner = (number - 1) % 2 if self.args.full_distance else 0
            expected = next(c for c in self.captures if c["label"] == f"map-{number}-live-host")
            if (entry["attempt"] != number or entry["sessionId"] != expected["session"] or
                entry["outcome"] != "forfeit" or entry["winner"] != winner or
                entry["rulesDigest"] != report["rules"]["digest"]):
                raise RuntimeError(f"persisted series map {number} differs from accepted gameplay")
            sessions.append(entry["sessionId"])
            artifacts = {}
            for kind in ("evidence", "mvd"):
                artifact = entry["artifacts"][kind]
                failed = kind == self.args.fail_artifact and number == self.args.failure_map
                if failed:
                    blocked = next(b for b in self.blocked_artifacts if b["map_number"] == number)
                    destination = game / blocked["qpath"]
                    if (artifact["status"] != "failed" or artifact["reason"] == 0 or
                        not destination.is_dir() or
                        (destination / "fixture.txt").read_text() != "Deliberate publication failure.\n"):
                        raise RuntimeError(f"map {number} {kind} failure was not isolated and reported")
                    if kind == "evidence" and artifact["qpath"]:
                        raise RuntimeError("failed evidence was advertised as a usable file")
                    if kind == "mvd":
                        partial = game / artifact["qpath"]
                        if (not partial.resolve().is_relative_to(game) or
                            partial != destination.with_suffix(".mvd.part") or
                            not partial.is_file() or partial.stat().st_size == 0):
                            raise RuntimeError("failed MVD did not retain its reported recoverable stream")
                        paths.append(str(partial))
                    continue
                path = (game / artifact["qpath"]).resolve()
                if (artifact["status"] != "available" or artifact["reason"] != 0 or
                    not path.is_relative_to(game) or not path.is_file() or path.stat().st_size == 0):
                    raise RuntimeError(f"series map {number} has no committed {kind} artifact")
                artifacts[kind] = path
                paths.append(str(path))
            if "evidence" not in artifacts:
                continue
            evidence = json.loads(artifacts["evidence"].read_text(encoding="utf-8"))
            if (evidence["schema"] != 2 or evidence["sessionId"] != entry["sessionId"] or
                evidence["seriesId"] != series_id or evidence["map"] != entry["map"] or
                evidence["rulesDigest"] != entry["rulesDigest"]):
                raise RuntimeError(f"map {number} evidence identity differs from series")
            journal = evidence["journal"]
            events = journal["events"]
            if journal["dropped"] or journal["accepted"] != len(events) or [e["sequence"] for e in events] != list(range(1, len(events) + 1)):
                raise RuntimeError(f"map {number} journal has missing or dropped events")
            for field in ("sessionRevision", "matchTimeMsec", "hostTimeUtcMsec"):
                if any(a[field] > b[field] for a, b in zip(events, events[1:])):
                    raise RuntimeError(f"map {number} journal {field} went backwards")
            results = [e["data"] for e in events if e["kind"] == "result"]
            if len(results) != 1 or results[0]["outcome"] != "forfeit" or results[0]["winnerSide"] != winner:
                raise RuntimeError(f"map {number} did not seal exactly one matching result")
            linked = [a["qpath"] for a in evidence["artifacts"] if a["kind"] == "mvd"]
            mvd_failed = self.args.fail_artifact == "mvd" and number == self.args.failure_map
            expected_links = [] if mvd_failed else [entry["artifacts"]["mvd"]["qpath"]]
            if linked != expected_links:
                raise RuntimeError(f"map {number} MVD link or publication differs")
            if "mvd" in artifacts and artifacts["mvd"].with_suffix(".mvd.part").exists():
                raise RuntimeError(f"map {number} left an unpublished MVD")
            if mvd_failed:
                stops = [e["data"] for e in events if e["kind"] == "outputFailure" and e["data"]["output"] == "mvdStop"]
                if len(stops) != 1 or stops[0]["reason"] == 0:
                    raise RuntimeError("MVD publication failure is absent from the match journal")
        if len(set(sessions)) != played:
            raise RuntimeError("series reused a match session across maps")
        if list((game / "match-results").glob("*.pending-*")):
            raise RuntimeError("successful series left pending result files")
        return paths

    def block_artifact_publication(self, view, selection):
        number = view["series"]["map_number"]
        if not self.args.fail_artifact or number != self.args.failure_map:
            return
        session, series_id = view["session"], view["series"]["id"]
        if self.args.fail_artifact == "evidence":
            qpath = f"match-results/session-{session}_series-{series_id}_{Path(selection['token']).name}.json"
        else:
            # Use the recording destination the engine actually announced.
            matches = re.findall(r"Recording multi-view demo to '([^']+)'", read_log(self.logs["host"]))
            if not matches or f"match_{session}_" not in matches[-1]:
                raise RuntimeError("no current engine MVD destination to block")
            qpath = matches[-1]
        game = self.games["host"]
        destination = (game / qpath).resolve()
        if not destination.is_relative_to(game) or destination.exists():
            raise RuntimeError("artifact blocker must target a new file in this fixture's save path")
        destination.mkdir(parents=True)
        (destination / "fixture.txt").write_text("Deliberate publication failure.\n")
        self.blocked_artifacts.append({"map_number": number, "kind": self.args.fail_artifact, "qpath": qpath})

    def admission(self):
        def admitted(texts):
            return all("MP_SERIES_LOADED" in texts[role] and
                       re.search(rf"MP_PLAYER slot={slot} .*spectating=0 wantSpectate=0 ingame=1",
                                 texts[role].rsplit("Game Map Init", 1)[-1].rsplit("\nMP_STATE ", 1)[-1])
                       for slot, role in enumerate(self.games))
        self.wait("two admitted humans", admitted)

    def wait_map(self, previous_session, loaded_counts):
        self.wait("selected map load", lambda texts: all(
            text.count("Game Map Init") > loaded_counts[role]
            for role, text in texts.items()), timeout=90)
        self.admission()
        for role in self.games:
            self.open_menu(role)
        return self.await_view("host", "new map session",
                               lambda v: v["session"] != previous_session, timeout=60)

    def load_counts(self):
        return {role: read_log(path).count("Game Map Init") for role, path in self.logs.items()}

    def restart_after_review(self, expected):
        series = expected["series"]
        current = next(m for m in series["maps"] if m["selection"] == series["map_number"])
        checkpoint = self.games["host"] / f"match-series/series-{series['id']}.oq4series"
        if not checkpoint.is_file() or checkpoint.stat().st_size == 0:
            raise RuntimeError("no durable checkpoint before interrupted restart")
        archive = self.output / f"restart-after-map-{series['map_number']}"
        archive.mkdir()
        # Abruptly stop only this fixture's processes, after the committed
        # review. Recovery must work without a graceful shutdown checkpoint.
        for process in self.processes.values():
            if process.poll() is not None:
                raise RuntimeError("a peer ended before the planned interruption")
            process.kill()
        exits = {role: p.wait(timeout=10) for role, p in self.processes.items()}
        for stream in self.streams:
            stream.close()
        for role, log in self.logs.items():
            shutil.move(str(log), archive / f"{role}-openq4.log")
            shutil.move(str(self.output / f"{role}-console.log"), archive / f"{role}-console.log")
            self.command_numbers[role] = 0
            write_script(self.loops[role], ["// Waiting for the first recovered operation."])
            self.start_script(role)
        self.processes, self.streams = {}, []
        self.launch("host", current["token"], series["id"])
        self.wait("restored host warmup", lambda t:
                  f"restored competition series {series['id']}" in t["host"] and
                  "MP match phase: 0 -> 1" in t["host"], timeout=90)
        self.launch("client")
        self.admission()
        for role in self.games:
            self.open_menu(role)
        restored = self.await_view("host", "restored completed map", lambda v:
            v["phase"] == 1 and v["session"] != expected["session"] and
            all(v["series"][key] == series[key] for key in ("id", "state", "map_number", "wins", "history")))
        for role in self.games:
            self.capture(role, f"map-{series['map_number']}-recovered", restored)
        self.restarts.append({"map_number": series["map_number"], "terminated_exits": exits,
                              "previous_session": expected["session"], "restored_session": restored["session"]})
        return restored

    def run(self):
        self.launch("host")
        self.wait("host warmup", lambda t: "MP match phase: 0 -> 1" in t["host"])
        self.launch("client")
        self.admission()
        for role in self.games:
            self.open_menu(role)
        profile = {1: "best_of_one", 3: "best_of_three", 5: "best_of_five"}[self.args.best_of]
        self.action("host", "series_stage", before=["openq4_matchControl set match_series_profile_choice " + profile])
        first = self.await_view("host", "series setup", lambda v: v["series"]["state"] == 1)
        series_id = first["series"]["id"]
        if first["series"]["best_of"] != self.args.best_of or len(first["series"]["maps"]) != 5:
            raise RuntimeError("series profile or stock map pool differs")
        self.action("host", "series_start", confirm=True)
        view = self.await_view("host", "veto start", lambda v: v["series"]["state"] == 2)
        while view["series"]["has_veto"]:
            series = view["series"]
            action = series["veto_action"]
            available = [m for m in series["maps"] if m["disposition"] == 0]
            chosen = (max((m for m in series["maps"] if m["disposition"] == 2), key=lambda m: m["selection"])
                      if action == 2 else available[0])
            # The local operator can resolve either veto turn. Remote turn
            # authority is a separate role qualification, not inferred here.
            token = {0: "veto_ban", 1: "veto_pick", 2: "veto_side_marine", 3: "veto_decider"}[action]
            last_veto = series["veto_step"] + 1 == series["veto_steps"]
            previous_session, loaded_counts = view["session"], self.load_counts()
            self.action("host", token, confirm=True, before=[
                f"openq4_matchControl set match_series_map_rows_sel_0 {chosen['pool_index']}",
                "openq4_matchControl action select_series_map"])
            if last_veto:
                # The accepted final veto schedules the first selected map.
                # Subsequent map handoffs use the explicit Advance action.
                view = self.wait_map(previous_session, loaded_counts)
                break
            view = self.await_view("host", "veto step", lambda v: v["series"]["revision"] > series["revision"])
        if view["series"]["state"] != 4:
            raise RuntimeError("veto did not produce the first active series map")
        for role in self.games:
            self.capture(role, "veto-complete", view)
        played = self.args.best_of if self.args.full_distance else self.args.best_of // 2 + 1
        expected_wins = [0, 0]
        for number in range(1, played + 1):
            if number > 1:
                previous_session, loaded_counts = view["session"], self.load_counts()
                self.action("host", "series_advance", confirm=True)
                view = self.wait_map(previous_session, loaded_counts)
            if self.args.duel:
                self.perform("host", "bind recovered Duel contestants", [
                    "waitMsec 2200", "matchSeriesBind a 0",
                    "waitMsec 2200", "matchSeriesBind b 1", "waitMsec 500"])
                for slot, role in enumerate(self.games):
                    self.await_view(role, "recovered Duel binding", lambda v: v["competition_side"] == slot and v["active"])
            self.action("host", "force_ready", confirm=True)
            view = self.await_view("host", "live series map", lambda v: v["phase"] == 3 and v["series"]["state"] == 4)
            if view["series"]["id"] != series_id or view["series"]["map_number"] != number:
                raise RuntimeError("series identity or map ordinal changed across maps")
            for role in self.games:
                self.capture(role, f"map-{number}-live", view)
            selection = next(m for m in view["series"]["maps"] if m["selection"] == number)
            self.block_artifact_publication(view, selection)
            if self.args.pause_before_forfeit:
                self.action("host", "tech_pause")
                for role in self.games:
                    paused = self.await_view(role, "pause before forfeit", lambda v: v["phase"] == 3 and v["pause"] == 2)
                    self.capture(role, f"map-{number}-paused", paused)
            winner = (number - 1) % 2 if self.args.full_distance else 0
            forfeiting_competition = 1 - winner
            forfeiting_side = (forfeiting_competition if self.args.duel else selection["starting_side"]
                               if selection["side_chosen_by"] == forfeiting_competition else 1 - selection["starting_side"])
            self.action("host", "forfeit", confirm=True,
                        before=["openq4_matchControl action action_side_" + ("b" if forfeiting_side else "a")])
            view = self.await_view("host", "map forfeit result", lambda v: v["phase"] == 5 and v["pause"] == 0 and v["series"]["state"] == 5)
            expected_wins[winner] += 1
            if view["series"]["wins"] != expected_wins:
                raise RuntimeError(f"wrong series score after map {number}: {view['series']}")
            for role in self.games:
                # Match review closes the gameplay menu and opens its result
                # surface. Reopen Match Control before inspecting or advancing.
                self.open_menu(role, review=True)
                self.capture(role, f"map-{number}-review", view)
            if number == self.args.restart_after_map:
                view = self.restart_after_review(view)
        self.action("host", "series_advance", confirm=True)
        view = self.await_view("host", "series completion", lambda v: v["series"]["state"] == 6)
        for role in self.games:
            self.capture(role, "series-complete", view)
        return {"series_id": series_id, "captures": self.captures, "reports": self.validate_reports(series_id)}

    def close(self):
        exits = {}
        for role, process in self.processes.items():
            if process.poll() is None:
                try:
                    self.inject(role, ["quit"])
                except OSError:
                    pass
        for role, process in self.processes.items():
            try:
                exits[role] = process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                exits[role] = -1
        for stream in self.streams:
            stream.close()
        self.decoder.close()
        return exits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-series-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--best-of", type=int, choices=(1, 3, 5), default=1)
    parser.add_argument("--duel", action="store_true")
    parser.add_argument("--full-distance", action="store_true", help="alternate map winners to play the deciding map")
    parser.add_argument("--network-trace", action="store_true")
    parser.add_argument("--pause-before-forfeit", action="store_true", help="qualify each map's forfeit from a technical pause")
    parser.add_argument("--fail-artifact", choices=("evidence", "mvd"),
                        help="block one final artifact path and verify result publication survives")
    parser.add_argument("--failure-map", type=int, default=1)
    parser.add_argument("--restart-after-map", type=int, default=0,
                        help="abruptly stop both test peers after this review and restore the durable series")
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()
    played = args.best_of if args.full_distance else args.best_of // 2 + 1
    if args.restart_after_map < 0 or args.restart_after_map >= played:
        parser.error("restart-after-map must be zero or precede the winning map")
    if not 1 <= args.failure_map <= played:
        parser.error("failure-map must identify a played map")
    run = SeriesRun(args)
    result, failures = {}, []
    try:
        result = run.run()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        failures.append(str(error))
    finally:
        exits = run.close()
    if len(exits) != 2 or any(exits.values()):
        failures.append(f"incomplete shutdown: {exits}")
    result.update(status="fail" if failures else "pass", failures=failures, exits=exits,
                  best_of=args.best_of, mode="Duel" if args.duel else "Team DM",
                  full_distance=args.full_distance,
                  paused_forfeits=args.pause_before_forfeit,
                  blocked_artifacts=run.blocked_artifacts,
                  logs={r: str(p) for r, p in run.logs.items()}, captures=run.captures, restarts=run.restarts)
    (run.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "captures"}, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
