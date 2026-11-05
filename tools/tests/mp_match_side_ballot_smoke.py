#!/usr/bin/env python3
"""Qualify side-scoped ballots (team forfeit votes) live with three humans.

The host is the Marine captain, the client the Strogg captain and the observer
a second, invited Strogg player. During live Team DM play the observer opens
the Forfeit | Side ballot from Match Control:

* only the Strogg side sees and may vote on it; the Marine host sees neither a
  side nor a global ballot, and the caller's YES alone (1 of 2) does not pass;
* the Strogg captain's NO fails it and the match continues;
* after the per-opcode cooldown a second ballot passes on the captain's YES,
  forfeits for Strogg and records a forfeit won by Marine.

Uses native engine menu commands, an immutable command pump and accepted wire
views. All game windows are hidden; no operating-system input is used. The
output directory must be fresh.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import time
from pathlib import Path

from mp_match_edges_smoke import EdgesRun, GAMEON, OPCODES, REVIEW, WARMUP
from mp_roster_smoke import CAPTAIN

ROOT = Path(__file__).resolve().parents[2]
FORFEIT_OPCODE = 31
SIDE_FORFEIT_TEMPLATE_ROW = 6  # the seventh template: Forfeit | Side
VIEW_RESULT_FORFEIT, VIEW_RESULT_REASON_FORFEIT = 2, 2
PROPOSAL_COOLDOWN_MS = 30000
MARINE, STROGG = 0, 1
# Both ballots are proposal_cast; the edges table names only the YES token.
OPCODES.setdefault("proposal_no", "proposal_cast")


class SideBallotRun(EdgesRun):
    def launch_overrides(self, role):
        return {"g_gameReviewPause": "15"} if role == "host" else {}

    def open_side_ballot(self, label):
        self.action("observer", "proposal_create", before=[
            "openq4_matchControl set match_proposal_scope_choice side",
            f"openq4_matchControl set match_proposal_rows_sel_0 {SIDE_FORFEIT_TEMPLATE_ROW}",
            "openq4_matchControl action select_proposal_row"])
        observer = self.view("observer")["participant"]
        opened = self.sample("observer", label + "-open", lambda v: v["side_proposal"]["present"] and
                             v["side_proposal"]["caller"] == observer)
        ballot = opened["side_proposal"]
        if (ballot["opcode"] != FORFEIT_OPCODE or ballot["scope"] != 1 or ballot["side"] != STROGG or
                ballot["eligible"] != 2 or ballot["required_yes"] != 2 or ballot["yes"] != 1 or
                opened["global_proposal"]["present"] or opened["phase"] != GAMEON):
            raise RuntimeError(f"{label}: side ballot did not open as a two-voter Strogg forfeit: {ballot}")
        captain = self.sample("client", label + "-strogg-captain", lambda v: v["side_proposal"]["present"] and
                              v["side_proposal"]["id"] == ballot["id"])
        if not captain["side_proposal"]["recipient_eligible"] or captain["side_proposal"]["recipient_ballot"]:
            raise RuntimeError(f"{label}: the Strogg captain cannot vote on the side ballot")
        marine = self.sample("host", label + "-marine-hidden", lambda v: v["phase"] == GAMEON)
        if marine["side_proposal"]["present"] or marine["global_proposal"]["present"]:
            raise RuntimeError(f"{label}: the opposing side sees the Strogg side ballot")
        self.facts[label] = {"id": ballot["id"], "scope": ballot["scope"], "side": ballot["side"],
                             "eligible": ballot["eligible"], "required_yes": ballot["required_yes"],
                             "opened_engine": opened["engine"], "expires": ballot["expires"]}
        return ballot, opened["engine"]

    def vote(self, token, ballot):
        self.action("client", token, before=["openq4_matchControl set match_proposal_scope_choice side"])
        return ballot

    def run(self):
        for role in ("host", "client", "observer"):
            self.launch(role)
        self.action("observer", "team_spectate")
        self.sample("observer", "unrostered-observer", lambda v: v["side"] == -1 and not v["active"])
        # Two seats per side: each captain plus one invited player.
        self.action("host", "rules_stage_field", before=[
            "openq4_matchControl set match_rule_rows_sel_0 9", "openq4_matchControl action select_rule_row",
            "openq4_matchControl set match_rule_value 2"])
        for role in ("host", "client"):
            self.assign("host", role, 2)
            self.sample(role, role + "-captain", lambda v: v["roles"] & CAPTAIN and v["active"])
        invitation = self.invite("client", "observer", role=1)
        self.accept("observer", invitation)
        self.sample("observer", "strogg-player", lambda v: v["side"] == STROGG and v["active"] and
                    not v["roles"] & CAPTAIN)
        self.action("host", "force_ready", confirm=True)
        self.sample("host", "live-match", lambda v: v["phase"] == GAMEON, timeout=60)
        for role in ("host", "client", "observer"):
            self.await_view(role, role + " live", lambda v: v["phase"] == GAMEON)
            self.open_menu(role, review=True)

        # A side ballot needs its side's majority: the captain's NO fails it.
        failed, _ = self.open_side_ballot("side-ballot-1")
        self.vote("proposal_no", failed)
        settled = self.sample("observer", "side-ballot-1-failed", lambda v: not v["side_proposal"]["present"] or
                              v["side_proposal"]["id"] != failed["id"])
        if settled["phase"] != GAMEON:
            raise RuntimeError("a failed side ballot ended the match")
        host = self.sample("host", "side-ballot-1-match-continues", lambda v: v["phase"] == GAMEON)
        self.facts["side-ballot-1"]["result"] = "failed"
        self.facts["side-ballot-1"]["phase_after"] = host["phase"]

        # Each proposed opcode's cooldown starts when its ballot ends, not when
        # it opens.
        failed_at = settled["engine"]
        self.facts["side-ballot-1"]["ended_engine_by"] = failed_at
        self.wait("forfeit proposal cooldown", lambda texts: self.server_time() >= failed_at + PROPOSAL_COOLDOWN_MS + 1500,
                  timeout=75)
        passed, _ = self.open_side_ballot("side-ballot-2")
        if passed["id"] <= failed["id"]:
            raise RuntimeError("the second side ballot reused the failed ballot's identifier")
        self.vote("proposal_yes", passed)
        review = self.sample("host", "side-ballot-2-forfeit", lambda v: v["phase"] == REVIEW, timeout=45)
        result = review["result"]
        if (result["outcome"] != VIEW_RESULT_FORFEIT or result["reason"] != VIEW_RESULT_REASON_FORFEIT or
                result["winner_side"] != MARINE):
            raise RuntimeError(f"the passed side ballot did not record a Strogg forfeit won by Marine: {result}")
        for role in ("observer", "client"):
            self.await_view(role, role + " accepted forfeit review", lambda v: v["phase"] == REVIEW and
                            not v["side_proposal"]["present"])
        self.facts["side-ballot-2"]["result"] = "passed"
        self.facts["side-ballot-2"]["terminal_result"] = result
        self.facts["session_continuity"] = review["session"]
        self.await_view("host", "next warmup", lambda v: v["phase"] == WARMUP, timeout=60)
        self.stage = "complete"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-match-side-ballot-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--port", type=int, default=28981, help="first of three consecutive ports")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--startup-timeout", type=int, default=90)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65533 or min(args.timeout, args.startup_timeout) <= 0:
        parser.error("reserve three unprivileged ports and positive time budgets")
    if not (args.runtime_dir / args.executable_name).is_file() or not args.basepath.is_dir():
        parser.error("a staged runtime and installed stock Quake 4 assets are required")
    if args.output_dir.exists() and any(args.output_dir.iterdir()):
        parser.error("output-dir must be empty to prevent stale runtime evidence")
    run = SideBallotRun(args)
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
    if run.stage != "complete" or set(exits) != {"host", "client", "observer"} or any(exits.values()):
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
