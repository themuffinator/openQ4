#!/usr/bin/env python3
"""Qualify the remaining live competitive-match edges with three humans.

Covers successful bench substitutions in both directions, timed roster
invitation expiry and its stale acceptance, countdown cancellation by an
unready player and by a captain's disconnect, a spectator holding a captain's
invitation who is denied live cameras and an explicit follow, duplicate and
simultaneous timeout requests with once-only budget charging, a passed abort
ballot created with the side scope selected (it must stay global) that
executes after an unrelated session change during its vote, and a referee
camera released when its target disconnects and is not inherited when the
slot is reused.

Uses native engine menu commands, an immutable command pump and accepted wire
views. All game windows are hidden; no operating-system input is used. The
output directory must be fresh.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import time
from pathlib import Path

from mp_round_remote_smoke import read_log
from mp_roster_smoke import CAPTAIN, REFEREE, ROLES, RosterRun

ROOT = Path(__file__).resolve().parents[2]
WARMUP, COUNTDOWN, GAMEON, REVIEW = 1, 2, 3, 5
RUNNING, PENDING, PAUSED = 0, 1, 2
TEAM_TIMEOUT = 1
ABORT_TEMPLATE_ROW = 4
COUNTDOWN_RULE_ROW, COUNTDOWN_SECONDS = 10, 25
# Menu tokens whose accepted availability is published under the opcode name.
OPCODES = {"ready_toggle": "ready_set", "timeout": "timeout_request", "resume": "resume_request",
           "proposal_yes": "proposal_cast", "team_spectate": "team_join"}


class EdgesRun(RosterRun):
    def __init__(self, args):
        super().__init__(args)
        self.facts = {}

    def launch_overrides(self, role):
        # Shorter review keeps the run bounded; rotation stays disabled.
        return {"g_gameReviewPause": "15"} if role == "host" else {}

    def action(self, role, token, *, before=(), confirm=False, denied=False):
        self.wait(token + " simulation cooldown", lambda texts: self.server_time() >= self.last_mutation + 2250)
        self.perform(role, token + " selection", list(before))
        menu = read_log(self.logs[role]).rsplit("\nMP_VIEW ", 1)[-1]
        opcode = OPCODES.get(token, token)
        available = re.search(rf"MP_CONTROL match_op_{re.escape(opcode)}_available=(\d+)", menu)
        if available is None or bool(int(available[1])) == denied:
            raise RuntimeError(f"{role}: unexpected {token} availability (denied={denied})")
        self.perform(role, token, ["openq4_matchControl action " + ("arm_" if confirm else "") + token,
                     *(["waitMsec 100", "openq4_matchControl action confirm"] if confirm else []), "waitMsec 750"])
        self.last_mutation = self.server_time()

    def ensure_ready(self, role):
        # Ready is a toggle; a flag that survived review must not be undone.
        if not self.view(role)["ready"]:
            self.action(role, "ready_toggle")

    def seat_of(self, view, participant):
        return next((s for s in view["roster_seats"] if s["occupied"] and s["participant"] == participant), None)

    def substitute(self, actor, outgoing, incoming):
        host = self.view(actor)
        outgoing_id = self.view(outgoing)["participant"]
        seat = next(i for i, s in enumerate(host["roster_seats"])
                    if s["participant"] == outgoing_id and s["occupied"])
        row = 2 + len(host["participants"]) + seat
        self.perform(actor, "substitution choices", [f"openq4_matchControl set match_team_rows_sel_0 {row}",
                     "openq4_matchControl action select_team_row"])
        menu = read_log(self.logs[actor]).rsplit("\nMP_VIEW ", 1)[-1]
        name = re.escape(ROLES[incoming][1])
        replacement = re.search(rf"MP_CONTROL match_replacement_rows_item_(\d+)={name}(?:\t|\r?$)", menu, re.M)
        if replacement is None:
            raise RuntimeError(f"{incoming} is absent from the substitution replacement choices")
        self.action(actor, "roster_substitute", confirm=True, before=[
            f"openq4_matchControl set match_team_rows_sel_0 {row}", "openq4_matchControl action select_team_row",
            f"openq4_matchControl set match_replacement_rows_sel_0 {replacement[1]}",
            "openq4_matchControl action select_replacement_row"])

    def cameras(self, label):
        self.perform("host", label + " camera policy", ["openq4_reportMPState"])
        state = read_log(self.logs["host"]).rsplit("\nMP_STATE ", 1)[-1]
        return {int(t): (bool(int(a)), bool(int(f))) for t, a, f in
                re.findall(r"MP_CAMERA observer=2 target=(\d+) allowed=(\d+) following=(\d+)", state)}

    @staticmethod
    def following(cameras):
        # The engine reports a free camera as the observer following its own slot.
        return [target for target, (_, follows) in cameras.items() if follows and target != ROLES["observer"][0]]

    def await_cameras(self, label, predicate, timeout=20):
        until = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < until:
            cameras = self.cameras(label)
            if predicate(cameras):
                self.facts[label] = {str(k): list(v) for k, v in cameras.items()}
                return cameras
        raise RuntimeError(f"timeout during {label}: {cameras}")

    def substitutions(self):
        observer = self.view("observer")["participant"]
        invitation = self.invite("client", "observer", role=4)
        self.accept("observer", invitation)
        self.sample("observer", "bench-accepted", lambda v: v["side"] == 1 and not v["active"] and
                    not v["roles"] & CAPTAIN and not v["invitations"] and self.seat_of(v, observer) is not None)
        self.substitute("host", "client", "observer")
        self.sample("observer", "bench-promoted", lambda v: v["active"] and v["side"] == 1 and v["roles"] & CAPTAIN)
        client = self.view("client")["participant"]
        self.sample("client", "captain-benched", lambda v: not v["active"] and v["side"] == 1 and
                    not v["roles"] & CAPTAIN and self.seat_of(v, client) is not None)
        self.substitute("host", "observer", "client")
        self.sample("client", "bench-restored", lambda v: v["active"] and v["side"] == 1 and v["roles"] & CAPTAIN)
        self.sample("observer", "promoted-player-benched", lambda v: not v["active"] and v["side"] == 1 and
                    not v["roles"] & CAPTAIN and self.seat_of(v, observer) is not None)
        self.action("observer", "roster_leave")
        self.sample("observer", "bench-left", lambda v: v["side"] == -1 and not v["active"] and
                    self.seat_of(v, observer) is None)

    def invitation_expiry(self):
        observer = self.view("observer")["participant"]
        invitation = self.invite("client", "observer", role=3)
        view = self.sample("observer", "expiring-invitation", lambda v: any(
            i["id"] == invitation for i in v["invitations"]))
        entry = next(i for i in view["invitations"] if i["id"] == invitation)
        lifetime = entry["expires"] - view["engine"]
        if not 50000 <= lifetime <= 60000:
            raise RuntimeError(f"invitation lifetime is not the 60-second contract: {lifetime} ms remain")
        self.facts["invitation_expiry"] = {"id": invitation, "expires": entry["expires"],
                                           "observed_engine": view["engine"]}
        # Keep the menu's selection on the invitation so the later action is
        # exactly the stale click a player would make after it lapsed.
        self.perform("observer", "select expiring invitation", self.invitation_selection("observer", invitation))
        time.sleep(max(0.0, lifetime / 1000.0 - 3.0))
        expired = self.await_view("observer", "invitation lifetime", lambda v:
                                  v["engine"] >= entry["expires"] + 1000 and not v["invitations"], timeout=60)
        self.facts["invitation_expiry"]["expired_engine"] = expired["engine"]
        self.sample("observer", "invitation-expired", lambda v: not v["invitations"])
        self.perform("observer", "expired acceptance", ["openq4_matchControl action roster_accept", "waitMsec 750"])
        self.sample("observer", "expired-accept-denied", lambda v: v["side"] == -1 and not v["active"] and
                    not v["invitations"] and self.seat_of(v, observer) is None)

    def countdown_cancellation(self):
        for role in ("host", "client"):
            self.ensure_ready(role)
        started = self.sample("host", "countdown-started", lambda v: v["phase"] == COUNTDOWN)
        self.await_view("client", "client sees the countdown", lambda v: v["phase"] == COUNTDOWN)
        self.action("client", "ready_toggle")
        cancelled = self.sample("host", "countdown-cancelled", lambda v: v["phase"] == WARMUP)
        if cancelled["session"] != started["session"]:
            raise RuntimeError("countdown cancellation replaced the session")
        client = self.sample("client", "unready-after-cancel", lambda v: v["phase"] == WARMUP and not v["ready"])
        self.facts["countdown"] = {"started_engine": started["engine"], "cancelled_engine": cancelled["engine"],
                                   "readiness_after": cancelled["readiness"], "client_ready": client["ready"]}

    def timeouts(self, live):
        before = live["timeouts"]
        if [b["remaining"] for b in before] != [2, 2] or any(b["consumed"] for b in before):
            raise RuntimeError(f"live match did not start with two timeouts per side: {before}")
        # One remote captain submits twice in one frame. Both requests carry
        # the same accepted revision; the server must charge exactly once.
        self.wait("duplicate timeout cooldown", lambda texts: self.server_time() >= self.last_mutation + 2250)
        self.perform("client", "duplicate timeout", ["openq4_matchControl action timeout",
                     "openq4_matchControl action timeout", "waitMsec 750"])
        self.last_mutation = self.server_time()
        # The budget is charged when PENDING commits to PAUSED; judge it there.
        paused = self.sample("host", "duplicate-timeout-paused", lambda v: v["pause"] == PAUSED and
                             v["pause_kind"] == TEAM_TIMEOUT and v["pause_owner"] == 1)
        if [b["consumed"] for b in paused["timeouts"]] != [0, 1]:
            raise RuntimeError(f"duplicate timeout charged {paused['timeouts']}")
        self.await_view("client", "timeout owner can resume", lambda v: v["pause"] == PAUSED)
        self.action("client", "resume")
        self.await_view("host", "duplicate timeout resumed", lambda v: v["pause"] == RUNNING, timeout=45)
        # Both captains request in the same instant. Exactly one pause and one
        # charge may result; the other request must be refused uncharged.
        self.wait("simultaneous timeout cooldown", lambda texts: self.server_time() >= self.last_mutation + 2250)
        # Each pump must run its request before the next injection replaces
        # the command selector, or the chain of numbered steps breaks.
        markers = {}
        for role in ("client", "host"):
            markers[role] = f"MP_EDGES_TIMEOUT_{self.operation + 1}"
            self.inject(role, ["openq4_matchControl action timeout", "echo " + markers[role], "waitMsec 750"])
        self.wait("simultaneous timeout requests", lambda texts: all(
            "\n" + marker + " " in texts[role] for role, marker in markers.items()))
        winner = self.await_view("host", "simultaneous timeout", lambda v: v["pause"] in (PENDING, PAUSED) and
                                 v["pause_kind"] == TEAM_TIMEOUT)
        self.last_mutation = self.server_time()
        time.sleep(2.0)
        settled = self.sample("host", "simultaneous-timeout-settled", lambda v: v["pause"] == PAUSED)
        owner = settled["pause_owner"]
        expected = [0, 1]
        expected[owner] += 1
        if owner not in (0, 1) or [b["consumed"] for b in settled["timeouts"]] != expected:
            raise RuntimeError(f"simultaneous timeouts charged {settled['timeouts']} with owner {owner}")
        self.facts["timeouts"] = {"duplicate_owner": 1, "simultaneous_owner": owner,
                                  "budgets": settled["timeouts"], "first_pause_engine": paused["engine"],
                                  "simultaneous_pause_engine": winner["engine"]}
        resumer = "client" if owner == 1 else "host"
        self.await_view(resumer, "simultaneous owner can resume", lambda v: v["pause"] == PAUSED)
        self.action(resumer, "resume")
        self.await_view("host", "simultaneous timeout resumed", lambda v: v["pause"] == RUNNING, timeout=45)

    def passed_abort(self):
        client = self.view("client")
        # Every creation template is global. Selecting the side scope first
        # must not turn the captain's proposal into a side ballot.
        self.action("client", "proposal_create", before=[
            "openq4_matchControl set match_proposal_scope_choice side",
            f"openq4_matchControl set match_proposal_rows_sel_0 {ABORT_TEMPLATE_ROW}",
            "openq4_matchControl action select_proposal_row"])
        created = self.sample("host", "abort-ballot-open", lambda v: v["global_proposal"]["present"] and
                              v["global_proposal"]["caller"] == client["participant"])
        ballot = created["global_proposal"]
        if ballot["opcode"] != 32 or ballot["eligible"] != 2 or ballot["yes"] != 1:
            raise RuntimeError(f"abort ballot did not open with two voters and the caller's YES: {ballot}")
        caller = self.sample("client", "abort-ballot-caller", lambda v: v["global_proposal"]["present"])
        if ballot["scope"] != 0 or caller["side_proposal"]["present"] or created["side_proposal"]["present"]:
            raise RuntimeError("the side scope selector created a side ballot")
        self.perform("client", "restore global proposal scope",
                     ["openq4_matchControl set match_proposal_scope_choice global"])
        # An unrelated session change inside the vote: the spectator signs in
        # as referee. Before 23 October this voided the passed ballot.
        self.perform("observer", "referee sign in during vote", [
            'openq4_guiSet match_referee_credential::text "mprostersmokeonly"',
            "openq4_matchControl action referee_login", "waitMsec 750"])
        referee = self.sample("observer", "referee-during-vote", lambda v: v["roles"] == REFEREE and
                              v["global_proposal"]["present"])
        if referee["revision"] <= created["revision"]:
            raise RuntimeError("the referee sign-in did not advance the session revision during the vote")
        self.action("host", "proposal_yes", before=["openq4_matchControl set match_proposal_scope_choice global"])
        executed = self.sample("host", "passed-abort-executed", lambda v: v["phase"] == REVIEW and
                               not v["global_proposal"]["present"])
        self.facts["passed_ballot"] = {"id": ballot["id"], "scope": ballot["scope"],
                                       "created_with_side_selector": True,
                                       "created_revision": created["revision"],
                                       "revision_during_vote": referee["revision"],
                                       "executed_revision": executed["revision"]}
        return executed

    def stale_camera(self, review):
        warmup = self.await_view("host", "next warmup", lambda v: v["phase"] == WARMUP, timeout=90)
        if warmup["session"] != review["session"]:
            raise RuntimeError("review turnover replaced the session despite disabled rotation")
        for role in ("host", "client", "observer"):
            self.open_menu(role, review=True)
        self.perform("observer", "referee follows the Strogg captain", ["follow 1", "waitMsec 750"])
        self.await_cameras("referee-following-client", lambda c: c.get(1) == (True, True))
        # The followed captain leaves during a running countdown: the match
        # must fall back to warmup and the referee camera must let go.
        for role in ("host", "client"):
            self.ensure_ready(role)
        started = self.sample("host", "disconnect-countdown-started", lambda v: v["phase"] == COUNTDOWN)
        old = self.sample("client", "followed-before-disconnect", lambda v: v["phase"] == COUNTDOWN)
        self.stop("client")
        cancelled = self.sample("host", "countdown-cancelled-by-disconnect", lambda v: v["phase"] == WARMUP)
        if cancelled["session"] != started["session"]:
            raise RuntimeError("the disconnect cancellation replaced the session")
        seat = self.seat_of(cancelled, old["participant"])
        self.facts["countdown_disconnect"] = {
            "started_engine": started["engine"], "cancelled_engine": cancelled["engine"],
            "readiness_after": cancelled["readiness"], "departed_seat": seat}
        self.await_cameras("follow-released-on-disconnect", lambda c: bool(c) and not self.following(c))
        self.launch("client-replacement")
        replacement = self.sample("client-replacement", "followed-slot-replacement", lambda v:
                                  v["participant"] != old["participant"] and v["generation"] != old["generation"])
        if replacement["slot"] != old["slot"] or replacement["roles"] & (CAPTAIN | REFEREE):
            raise RuntimeError("replacement connection inherited the departed captain's slot authority")
        self.await_cameras("replacement-not-followed", lambda c: 1 in c and not c[1][1])
        state = read_log(self.logs["observer"]).rsplit("\nMP_STATE ", 1)[-1]
        local = re.search(r"MP_LOCAL client=2 entity=2 .*spectator=(-?\d+)", state)
        if local is not None and int(local[1]) == 1:
            raise RuntimeError("the referee's local camera still follows the reused slot")

    def run(self):
        for role in ("host", "client", "observer"):
            self.launch(role)
        self.action("observer", "team_spectate")
        self.sample("observer", "unrostered-observer", lambda v: v["side"] == -1 and not v["active"])
        # Two seats per side give each captain an empty seat and a bench.
        self.action("host", "rules_stage_field", before=[
            "openq4_matchControl set match_rule_rows_sel_0 9", "openq4_matchControl action select_rule_row",
            "openq4_matchControl set match_rule_value 2"])
        for role in ("host", "client"):
            self.assign("host", role, 2)
            self.sample(role, role + "-captain", lambda v: v["roles"] & CAPTAIN and v["active"])
        self.substitutions()
        self.invitation_expiry()
        # Empty required seats block readiness. Return to one seat per side
        # now that every non-captain seat is vacant.
        self.action("host", "rules_stage_field", before=[
            "openq4_matchControl set match_rule_rows_sel_0 9", "openq4_matchControl action select_rule_row",
            "openq4_matchControl set match_rule_value 1"])
        self.sample("host", "one-seat-rosters", lambda v: v["readiness"]["vacant_required"] == 0)
        # The default 10-second countdown is shorter than one cooldown-paced
        # menu action, so an unready would race the countdown's own end.
        self.action("host", "rules_stage_field", before=[
            f"openq4_matchControl set match_rule_rows_sel_0 {COUNTDOWN_RULE_ROW}",
            "openq4_matchControl action select_rule_row",
            f"openq4_matchControl set match_rule_value {COUNTDOWN_SECONDS}"])
        self.perform("host", "committed countdown rule", [])
        menu = read_log(self.logs["host"]).rsplit("\nMP_VIEW ", 1)[-1]
        if not re.search(rf"MP_CONTROL match_rule_rows_item_{COUNTDOWN_RULE_ROW}=[^\n]*\t{COUNTDOWN_SECONDS}\s*\n", menu):
            raise RuntimeError("the countdown rule was not committed")
        self.countdown_cancellation()
        invitation = self.invite("client", "observer", role=3)
        self.action("host", "force_ready", confirm=True)
        # Forced readiness still runs the staged countdown before GAMEON.
        live = self.sample("host", "live-match", lambda v: v["phase"] == GAMEON, timeout=COUNTDOWN_SECONDS + 35)
        spectator = self.sample("observer", "invited-spectator-live", lambda v: v["phase"] == GAMEON)
        held = any(i["id"] == invitation for i in spectator["invitations"])
        cameras = self.await_cameras("invited-spectator-cameras", lambda c: bool(c) and not any(a for a, _ in c.values()))
        if spectator["follows"]:
            raise RuntimeError("a spectator holding a captain's invitation received live follow targets")
        # Ask for the inviting captain's camera directly; the server must
        # refuse it and leave the spectator on a free camera.
        self.perform("observer", "invited spectator follows the captain", ["follow 1", "waitMsec 750"])
        refused = self.await_cameras("invited-spectator-follow-denied", lambda c: bool(c) and
                                     not any(a for a, _ in c.values()) and not self.following(c))
        state = read_log(self.logs["observer"]).rsplit("\nMP_STATE ", 1)[-1]
        local = re.search(r"MP_LOCAL client=2 entity=2 .*spectator=(-?\d+)", state)
        if local is None or int(local[1]) == 1:
            raise RuntimeError("the invited spectator's local camera follows the inviting captain")
        if self.sample("observer", "invited-spectator-after-follow", lambda v: v["phase"] == GAMEON)["follows"]:
            raise RuntimeError("an explicit follow gave the invited spectator live follow targets")
        self.facts["spectator_invitation"] = {"invitation": invitation, "held_live": held,
                                              "cameras": {str(k): list(v) for k, v in cameras.items()},
                                              "after_follow": {str(k): list(v) for k, v in refused.items()},
                                              "local_spectator": int(local[1])}
        for role in ("host", "client", "observer"):
            self.open_menu(role, review=True)
        self.timeouts(live)
        review = self.passed_abort()
        for role in ("observer", "host", "client"):
            self.await_view(role, role + " accepted review", lambda v: v["phase"] == REVIEW)
        self.stale_camera(review)
        self.stage = "complete"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-match-edges-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--port", type=int, default=28971, help="first of three consecutive ports")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--startup-timeout", type=int, default=90)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65533 or min(args.timeout, args.startup_timeout) <= 0:
        parser.error("reserve three unprivileged ports and positive time budgets")
    if not (args.runtime_dir / args.executable_name).is_file() or not args.basepath.is_dir():
        parser.error("a staged runtime and installed stock Quake 4 assets are required")
    if args.output_dir.exists() and any(args.output_dir.iterdir()):
        parser.error("output-dir must be empty to prevent stale runtime evidence")
    run = EdgesRun(args)
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
    expected_roles = {"host", "client", "observer", "client-replacement"}
    if run.stage != "complete" or set(exits) != expected_roles or any(exits.values()):
        failures.append(f"incomplete run: stage={run.stage}, exits={exits}")
    result = {"status": "fail" if failures else "pass", "stage": run.stage, "failures": failures,
              "exits": exits, "mode": "Team DM", "renderer": args.renderer,
              "duration_seconds": round(time.monotonic() - run.started, 1), "facts": run.facts,
              "samples": run.samples, "logs": {r: str(p) for r, p in run.logs.items()}}
    (run.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "samples"}, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
