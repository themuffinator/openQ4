#!/usr/bin/env python3
"""Qualify roster invitation, review and reconnect boundaries with three humans.

Uses native engine menu commands, an immutable command pump and accepted wire
views. All game windows are hidden/windowed; no operating-system input is used.
The output directory must be fresh. Run against a copied runtime containing its
matching match-view-decode executable when other source work is in progress.
"""
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
from mp_series_smoke import SeriesRun

ROOT = Path(__file__).resolve().parents[2]
ROLES = {"host": (0, "host"), "client": (1, "client"),
         "observer": (2, "observer"), "client-replacement": (1, "client"),
         "observer-replacement": (2, "observer")}
CAPTAIN, REFEREE = 4, 32


class RosterRun(SeriesRun):
    def __init__(self, args):
        super().__init__(args)
        self.stage = "startup"
        self.samples, self.exits = {}, {}
        self.last_mutation = -3000
        self.started = time.monotonic()

    def start_script(self, role):
        # Arm the first command once in launch(), not on each map load. The
        # inherited per-operation scripts retire themselves before acting.
        write_script(self.games[role] / "series_start.cfg", [
            'set oq4_series_pump "exec series_pump.cfg"',
            "echo MP_ROSTER_LOADED", "exec series_pump.cfg"])

    def launch(self, role, *, spectator=False):
        if role not in self.games:
            game = self.output / role / "baseoq4"
            game.mkdir(parents=True)
            self.games[role], self.logs[role] = game, game / "logs/openq4.log"
            self.command_numbers[role] = 0
            self.loops[role] = game / "series_commands.cfg"
            write_script(self.loops[role], ["// Waiting for the first operation."])
            write_script(game / "series_pump.cfg", ["exec series_commands.cfg", "waitMsec 250",
                         "openq4_reportMPState", "vstr oq4_series_pump"])
            self.start_script(role)
        a = self.args
        port_offset, display_name = ROLES[role]
        cvars = {
            "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
            "fs_basepath": str(a.basepath.resolve()), "fs_savepath": str(self.games[role].parent),
            "fs_devpath": str(self.games[role].parent), "fs_game": "baseoq4", "com_gameMode": "MP",
            "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
            "r_borderlessDefaultMigrated": "1", "r_mode": "-1", "r_customWidth": "960",
            "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540",
            "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
            "r_renderApi": a.renderer, "com_maxfps": "60", "r_swapInterval": "0",
            "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
            "ui_autoJoin": "1", "ui_spectate": "Spectate" if spectator else "Play",
            "ui_name": display_name, "ui_team": "Marine" if role == "host" else "Strogg",
            "net_allowCheats": "1", "com_skipLoadingContinue": "1",
            "g_autoExecAfterMapLoad": "series_start.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
            "oq4_series_cmd_1": "exec series_step_1.cfg", "net_port": str(a.port + port_offset),
        }
        if role == "host":
            cvars.update(net_serverDedicated="0", net_LANServer="1", si_pure="0", si_maxPlayers="4",
                         si_gameType="Team DM", bot_minPlayers="0", g_matchProfile="competitive_tdm",
                         g_gameReviewPause="90", g_refPassword="mprostersmokeonly",
                         si_mapCycle="", g_mapCycle="")
        command = [str(self.runtime / a.executable_name)]
        for key, value in cvars.items():
            command += ["+set", key, value]
        command += ["+spawnServer", "mp/q4dm1"] if role == "host" else ["+connect", f"127.0.0.1:{a.port}"]
        (self.output / f"{role}-launch.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
        stream = (self.output / f"{role}-console.log").open("w", encoding="utf-8")
        self.streams.append(stream)
        self.processes[role] = subprocess.Popen(command, cwd=self.runtime, stdout=stream, stderr=subprocess.STDOUT)
        self.wait(role + " admission", lambda texts: "MP_ROSTER_LOADED" in texts[role] and
                  re.search(rf"MP_LOCAL client={port_offset} entity={port_offset} ", texts[role]), a.startup_timeout)
        self.open_menu(role, review=True)

    def wait(self, label, predicate, timeout=45):
        # Rejected roster operations are deliberate test inputs. Fail on engine
        # and malformed diagnostic failures, not their expected denial text.
        self.stage = label
        until = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < until:
            texts = {role: read_log(path) for role, path in self.logs.items()}
            if re.search(r"FATAL ERROR|ERROR:|Unknown command|Reliable messages overflow|"
                         r"reliable message queue overflowed|usage: openq4_gui(?:Set|Get)|"
                         r"openq4_gui(?:Set|Get): unknown|MP_CONTROL action requires|"
                         r"MP_CONTROL rejected", "\n".join(texts.values())):
                raise RuntimeError("engine or diagnostic failure during " + label)
            if any(p.poll() is not None for p in self.processes.values()):
                raise RuntimeError("a process ended during " + label)
            if predicate(texts):
                return texts
            time.sleep(0.2)
        raise RuntimeError("timeout during " + label)

    def server_time(self):
        state = read_log(self.logs["host"]).rsplit("\nMP_STATE ", 1)[-1]
        clock = re.match(r"time=(\d+) ", state)
        value = int(clock[1]) if clock else -1
        # This fixture disables rotation and stays in one session. GAMEON's
        # LocalMapRestart preserves time; do not disguise a full restart by
        # rebasing cooldowns onto an unintended new session/clock epoch.
        if 0 <= value < self.last_mutation:
            raise RuntimeError("server clock rewound during a fixture that requires one continuous session")
        return value

    def view(self, role):
        self.perform(role, "accepted view for " + role, ["waitMsec 350", "openq4_reportMPState"])
        view = self.decoder.decode(self.games[role] / f"match-probe/view-{ROLES[role][0]}.bin")
        if not all(k in view for k in ("generation", "roster_seats", "invitations", "global_proposal")):
            raise RuntimeError("runtime decoder lacks roster/proposal fields; regenerate its pinned decoder")
        return view

    def sample(self, role, label, predicate=lambda view: True):
        view = self.await_view(role, label, predicate)
        folder = self.output / "views"
        folder.mkdir(exist_ok=True)
        shutil.copy2(self.games[role] / f"match-probe/view-{ROLES[role][0]}.bin", folder / f"{label}.bin")
        self.perform("host", label + " server state", ["openq4_reportMPState"])
        server = read_log(self.logs["host"]).rsplit("\nMP_STATE ", 1)[-1]
        menu = read_log(self.logs[role]).rsplit("\nMP_VIEW ", 1)[-1]
        result = {"role": role, "view": view, "server_state": server, "menu": menu}
        self.samples[label] = result
        (folder / f"{label}.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        return view

    def action(self, role, token, *, before=(), confirm=False, denied=False):
        self.wait(token + " simulation cooldown", lambda texts: self.server_time() >= self.last_mutation + 2250)
        self.perform(role, token + " selection", list(before))
        menu = read_log(self.logs[role]).rsplit("\nMP_VIEW ", 1)[-1]
        opcode = "team_join" if token == "team_spectate" else token
        available = re.search(rf"MP_CONTROL match_op_{re.escape(opcode)}_available=(\d+)", menu)
        if available is None or bool(int(available[1])) == denied:
            raise RuntimeError(f"{role}: unexpected {token} availability (denied={denied})")
        self.perform(role, token, ["openq4_matchControl action " + ("arm_" if confirm else "") + token,
                     *(["waitMsec 100", "openq4_matchControl action confirm"] if confirm else []), "waitMsec 750"])
        self.last_mutation = self.server_time()

    def participant_selection(self, actor, participant):
        view = self.view(actor)
        index = next(i for i, p in enumerate(view["participants"]) if p["participant"] == participant)
        return [f"openq4_matchControl set match_team_rows_sel_0 {2 + index}",
                "openq4_matchControl action select_team_row"]

    def assign(self, actor, target, role, *, denied=False):
        participant = self.view(target)["participant"]
        self.action(actor, "role_assign", denied=denied, before=[
            *self.participant_selection(actor, participant), f"openq4_matchControl set match_role_choice {role}"])

    def invite(self, actor, target, role=3):
        participant = self.view(target)["participant"]
        self.perform(actor, "invitation replacement choices", [])
        menu = read_log(self.logs[actor]).rsplit("\nMP_VIEW ", 1)[-1]
        name = re.escape(ROLES[target][1])
        replacement = re.search(rf"MP_CONTROL match_replacement_rows_item_(\d+)={name}(?:\t|\r?$)", menu, re.M)
        if replacement is None:
            raise RuntimeError("invitation target is absent from connected replacement choices")
        self.action(actor, "roster_invite", before=[
            "openq4_matchControl set match_team_rows_sel_0 1", "openq4_matchControl action select_team_row",
            f"openq4_matchControl set match_replacement_rows_sel_0 {replacement[1]}",
            "openq4_matchControl action select_replacement_row", f"openq4_matchControl set match_role_choice {role}"])
        issuer = self.view(actor)["participant"]
        view = self.await_view(target, "accepted invitation", lambda v: any(
            i["issuer"] == issuer and i["target"] == participant for i in v["invitations"]))
        return next(i["id"] for i in view["invitations"] if i["issuer"] == issuer and i["target"] == participant)

    def accept(self, target, invitation, *, denied=False):
        self.action(target, "roster_accept", denied=denied, before=self.invitation_selection(target, invitation))

    def invitation_selection(self, target, invitation):
        view = self.view(target)
        index = next(i for i, entry in enumerate(view["invitations"]) if entry["id"] == invitation)
        row = 2 + len(view["participants"]) + len(view["roster_seats"]) + index
        return [f"openq4_matchControl set match_team_rows_sel_0 {row}", "openq4_matchControl action select_team_row"]

    def stop(self, role):
        process = self.processes[role]
        self.inject(role, ["quit"])
        try:
            self.exits[role] = process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            self.exits[role] = -1
        del self.processes[role]
        if self.exits[role]:
            raise RuntimeError(f"{role} did not exit cleanly: {self.exits[role]}")
        if role != "host":
            slot = ROLES[role][0]
            self.wait(role + " server departure", lambda texts: not re.search(
                rf"MP_PLAYER slot={slot} ", texts["host"].rsplit("\nMP_STATE ", 1)[-1]))

    def run(self):
        for role in ("host", "client", "observer"):
            self.launch(role)
        self.action("observer", "team_spectate")
        self.sample("observer", "unrostered-observer", lambda v: v["side"] == -1 and not v["active"])
        # A two-seat side has both a captain and a player seat, so demotion
        # below is a valid role change instead of a missing-seat rejection.
        self.action("host", "rules_stage_field", before=[
            "openq4_matchControl set match_rule_rows_sel_0 9", "openq4_matchControl action select_rule_row",
            "openq4_matchControl set match_rule_value 2"])
        for role in ("host", "client"):
            self.assign("host", role, 2)
            self.sample(role, role + "-captain", lambda v: v["roles"] & CAPTAIN and v["active"])

        invitation = self.invite("client", "observer")
        self.sample("observer", "valid-captain-invitation", lambda v: any(
            i["id"] == invitation for i in v["invitations"]))
        self.accept("observer", invitation)
        self.sample("observer", "accepted-coach", lambda v:
                    v["roles"] == 8 and v["side"] == 1 and not v["active"] and not v["invitations"])
        self.action("observer", "roster_leave")
        self.sample("observer", "coach-left-roster", lambda v: v["roles"] == 2 and v["side"] == -1)

        for role_choice, label in ((1, "demoted"), (4, "benched")):
            invitation = self.invite("client", "observer")
            self.sample("observer", label + "-invitation-before", lambda v: any(
                i["id"] == invitation for i in v["invitations"]))
            self.perform("observer", label + " selected invitation", self.invitation_selection("observer", invitation))
            self.assign("host", "client", role_choice)
            self.sample("client", label + "-issuer", lambda v:
                        not v["roles"] & CAPTAIN and bool(v["active"]) == (role_choice == 1))
            self.sample("observer", label + "-invitation-invalidated", lambda v: not v["invitations"])
            # The last selected invitation has disappeared. An old menu action
            # must not assign a seat even though acceptance is generally a
            # warmup capability for players.
            self.perform("observer", label + " stale acceptance", [
                "openq4_matchControl action roster_accept", "waitMsec 750"])
            self.sample("observer", label + "-stale-accept-denied", lambda v:
                        v["roles"] == 2 and v["side"] == -1 and not v["active"] and not v["invitations"])
            self.assign("host", "client", 2)
            self.sample("client", label + "-captain-restored", lambda v: v["roles"] & CAPTAIN and v["active"])

        # The remaining gameplay has one active human per side. Return to
        # that explicit size while all non-captain seats are empty.
        self.action("host", "rules_stage_field", before=[
            "openq4_matchControl set match_rule_rows_sel_0 9", "openq4_matchControl action select_rule_row",
            "openq4_matchControl set match_rule_value 1"])

        # Two active humans keep caller auto-YES below the global threshold.
        # Use different templates to avoid conflating slot cleanup with the
        # separate 30-second cooldown for each proposed opcode.
        self.action("host", "proposal_create", before=[
            "openq4_matchControl set match_proposal_rows_sel_0 3", "openq4_matchControl action select_proposal_row"])
        first = self.sample("host", "global-proposal-created", lambda v: v["global_proposal"]["present"])
        ballot = first["global_proposal"]
        if ballot["eligible"] != 2 or ballot["yes"] != 1:
            raise RuntimeError("proposal did not retain the expected two-human electorate and single YES")
        self.action("host", "proposal_cancel", before=["openq4_matchControl set match_proposal_scope_choice global"])
        self.sample("host", "global-proposal-cancelled", lambda v: not v["global_proposal"]["present"])
        self.action("host", "proposal_create", before=[
            "openq4_matchControl set match_profile_rows_sel_0 1", "openq4_matchControl action select_profile_row",
            "openq4_matchControl set match_proposal_rows_sel_0 1", "openq4_matchControl action select_proposal_row"])
        self.sample("host", "global-proposal-replaced", lambda v:
                    v["global_proposal"]["present"] and v["global_proposal"]["id"] > ballot["id"] and
                    v["global_proposal"]["eligible"] == 2 and v["global_proposal"]["yes"] == 1)
        self.action("host", "proposal_cancel", before=["openq4_matchControl set match_proposal_scope_choice global"])

        old = self.sample("client", "captain-before-disconnect")
        self.stop("client")
        self.launch("client-replacement")
        replacement = self.sample("client-replacement", "captain-slot-replacement", lambda v:
                                  v["participant"] != old["participant"] and v["generation"] != old["generation"])
        if (replacement["session"] != old["session"] or replacement["slot"] != old["slot"] or
                replacement["roles"] & (CAPTAIN | REFEREE)):
            raise RuntimeError("replacement slot inherited the departed captain's authority")
        if any(o["available"] for o in replacement["operations"] if o["opcode"] in (21, 24, 25)):
            raise RuntimeError("replacement captain inherited roster-management capabilities")
        captain = "client-replacement"
        # A new connection receives a fresh explicit invitation; its reused
        # display name and slot are never used as an authority credential.
        invitation = self.invite("host", captain, role=2)
        self.accept(captain, invitation)
        self.sample(captain, "replacement-explicitly-appointed", lambda v: v["roles"] & CAPTAIN and v["active"])

        invitation = self.invite(captain, "observer")
        self.action("host", "force_ready", confirm=True)
        self.sample("host", "first-live-match", lambda v: v["phase"] == 3)
        self.action("host", "abort", confirm=True)
        review = self.sample("host", "review-operations", lambda v: v["phase"] == 5)
        # Review switches each client to its result screen. Wait until that
        # transition is accepted before reopening the actual multiplayer menu;
        # otherwise its late arrival can close a menu opened too early.
        for role in ("observer", "host", captain):
            self.await_view(role, role + " accepted review", lambda v: v["phase"] == 5)
            self.open_menu(role, review=True)
        for opcode in (22, 24, 25):
            operation = next(o for o in review["operations"] if o["opcode"] == opcode)
            if operation["available"] or operation["reason"] == 1:
                raise RuntimeError(f"review still advertises roster operation {opcode}")
        self.sample("observer", "review-invitation-retained", lambda v: v["phase"] == 5 and any(
            i["id"] == invitation for i in v["invitations"]))
        self.accept("observer", invitation, denied=True)
        self.sample("observer", "review-accept-denied", lambda v:
                    v["phase"] == 5 and v["roles"] == 2 and v["side"] == -1 and not v["active"])
        self.assign("host", captain, 3, denied=True)
        self.sample(captain, "review-assignment-denied", lambda v: v["phase"] == 5 and v["roles"] & CAPTAIN)
        host = self.view("host")
        target_id = self.view(captain)["participant"]
        seat = next(i for i, s in enumerate(host["roster_seats"]) if s["participant"] == target_id and s["occupied"])
        row = 2 + len(host["participants"]) + seat
        menu = read_log(self.logs["host"]).rsplit("\nMP_VIEW ", 1)[-1]
        replacement_row = re.search(r"MP_CONTROL match_replacement_rows_item_(\d+)=observer(?:\t|\r?$)", menu, re.M)
        if replacement_row is None:
            raise RuntimeError("review substitution target disappeared")
        self.action("host", "roster_substitute", confirm=True, denied=True, before=[
            f"openq4_matchControl set match_team_rows_sel_0 {row}", "openq4_matchControl action select_team_row",
            f"openq4_matchControl set match_replacement_rows_sel_0 {replacement_row[1]}",
            "openq4_matchControl action select_replacement_row"])
        self.sample(captain, "review-substitution-denied", lambda v: v["phase"] == 5 and v["roles"] & CAPTAIN)
        self.sample("observer", "review-substitution-target-unchanged", lambda v:
                    v["phase"] == 5 and v["roles"] == 2 and v["side"] == -1)

        warmup = self.await_view("host", "next warmup", lambda v: v["phase"] == 1, timeout=150)
        if warmup["session"] != review["session"] or warmup["engine"] < review["engine"]:
            raise RuntimeError("review turnover replaced the session or rewound its clock despite disabled rotation")
        for role in ("host", captain, "observer"):
            self.open_menu(role, review=True)
        self.action("host", "force_ready", confirm=True)
        self.sample("host", "second-live-match", lambda v: v["phase"] == 3)
        self.perform("observer", "referee sign in", [
            'openq4_guiSet match_referee_credential::text "mprostersmokeonly"',
            "openq4_matchControl action referee_login", "waitMsec 750"])
        old_ref = self.sample("observer", "referee-before-disconnect", lambda v:
                              v["phase"] == 3 and v["roles"] == REFEREE and bool(v["items"]) and
                              {vital["side"] for vital in v["vitals"]} == {0, 1})
        self.stop("observer")
        self.launch("observer-replacement", spectator=True)
        replacement = self.sample("observer-replacement", "referee-slot-replacement", lambda v:
                                  v["phase"] == 3 and v["participant"] != old_ref["participant"] and
                                  v["generation"] != old_ref["generation"])
        if (replacement["session"] != old_ref["session"] or replacement["slot"] != old_ref["slot"] or
                replacement["roles"] != 2 or replacement["active"] or
                replacement["side"] != -1 or any(replacement[k] for k in ("vitals", "items", "follows"))):
            raise RuntimeError("replacement connection inherited referee authority or private observation")
        server = self.samples["referee-slot-replacement"]["server_state"]
        cameras = re.findall(r"MP_CAMERA observer=2 target=(\d+) allowed=(\d+)", server)
        if not cameras or any(int(allowed) for _, allowed in cameras):
            raise RuntimeError("server camera policy exposes a target to the replacement spectator")
        for role, label in (("host", "final-host"), ("observer-replacement", "replacement-spectator")):
            self.open_menu(role, review=True)
            # A fresh menu can have curr=0; chooseCurr only routes an outgoing
            # panel. Explicitly finish destination routing for this capture.
            self.perform(role, label + " capture", ["GuiEvent chooseDest", "waitMsec 1500",
                         "openq4_guiSet desktop::match_tab 0",
                         "waitMsec 750", f'screenshot "screenshots/{label}.tga"'])
            path = self.games[role] / "screenshots" / f"{label}.tga"
            if not path.is_file() or path.stat().st_size <= 18:
                raise RuntimeError("engine screenshot was not written: " + str(path))
            self.captures.append(str(path))
        self.stage = "complete"

    def close(self):
        for role in list(self.processes):
            if self.processes[role].poll() is None:
                try:
                    self.inject(role, ["quit"])
                except OSError:
                    pass
        for role, process in self.processes.items():
            try:
                self.exits[role] = process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                self.exits[role] = -1
        for stream in self.streams:
            stream.close()
        self.decoder.close()
        return self.exits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-roster-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--port", type=int, default=28961, help="first of three consecutive ports")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--startup-timeout", type=int, default=90)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65533 or min(args.timeout, args.startup_timeout) <= 0:
        parser.error("reserve three unprivileged ports and positive time budgets")
    if not (args.runtime_dir / args.executable_name).is_file() or not args.basepath.is_dir():
        parser.error("a staged runtime and installed stock Quake 4 assets are required")
    if args.output_dir.exists() and any(args.output_dir.iterdir()):
        parser.error("output-dir must be empty to prevent stale runtime evidence")
    run = RosterRun(args)
    manifest = run.runtime / "manifest.json"
    if manifest.is_file():
        shutil.copy2(manifest, run.output / "runtime-manifest.json")
    failures = []
    try:
        run.run()
    except (RuntimeError, OSError, ValueError, KeyError, StopIteration) as error:
        failures.append(str(error) or type(error).__name__)
    finally:
        exits = run.close()
    if run.stage != "complete" or len(exits) != len(ROLES) or any(exits.values()):
        failures.append(f"incomplete run: stage={run.stage}, exits={exits}")
    result = {"status": "fail" if failures else "pass", "stage": run.stage, "failures": failures,
              "exits": exits, "mode": "Team DM", "renderer": args.renderer,
              "duration_seconds": round(time.monotonic() - run.started, 1), "samples": run.samples,
              "logs": {r: str(p) for r, p in run.logs.items()}, "captures": run.captures}
    (run.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "samples"}, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
