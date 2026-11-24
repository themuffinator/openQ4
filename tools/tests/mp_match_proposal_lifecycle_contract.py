#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Run the live terminal-proposal adapter against the real proposal service."""
from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_proposal_contract import HARNESS as SERVICE_HARNESS
from mp_match_vote_electorate_contract import function

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <cstdio>
#include <cstdlib>

struct mpParticipantId { static mpParticipantId Invalid() { return {}; } };
struct GameLocal {
    int warnings = 0;
    template<class... Args> void Warning(const char *, Args...) { ++warnings; }
} gameLocal;

struct idMultiplayerGame {
    struct Session {
        mpProposalSessionId_t GetSessionId() const { return 42; }
    } matchSession;
    mpProposalService matchProposals;
    mpProposalId_t observedId[MP_PROPOSAL_SCOPE_COUNT] = {};
    mpProposalStatus_t observedStatus[MP_PROPOSAL_SCOPE_COUNT] = {};
    int terminalEvents[MP_PROPOSAL_SCOPE_COUNT] = {};
    void RetireUnpassedMatchProposals();
    void ObserveMatchEvidence(mpParticipantId) {
        for (int index = 0; index < MP_PROPOSAL_SCOPE_COUNT; ++index) {
            const auto *record = matchProposals.GetProposal(
                static_cast<mpProposalScope_t>(index));
            if (record->IsTerminal() && (observedId[index] != record->proposalId ||
                    observedStatus[index] != record->status)) {
                ++terminalEvents[index];
            }
            observedId[index] = record->proposalId;
            observedStatus[index] = record->status;
        }
    }
};

@ADAPTER@

int main() {
    int cases = 0;
    for (int rawScope = 0; rawScope < MP_PROPOSAL_SCOPE_COUNT; ++rawScope) {
        const auto scope = static_cast<mpProposalScope_t>(rawScope);
        for (int terminal = 0; terminal < 4; ++terminal) {
            idMultiplayerGame game;
            auto &service = game.matchProposals;
            mpProposalCooldownPolicy_t cooldowns;
            cooldowns.Clear();
            cooldowns.durationMsec[MP_MATCH_COOLDOWN_PRIVILEGED] = 200;
            CHECK(service.Reset(42, mpProposalEngineTime::FromMilliseconds(0), cooldowns));
            auto proposal = Proposal(42, 1, scope, MP_MATCH_OP_RULES_COMMIT,
                10, 100, 2, 2, 2, MP_PROPOSAL_CALLER_VOTE_YES);
            CHECK(service.Create(proposal, service.GetRevision()).WasApplied());
            game.ObserveMatchEvidence(mpParticipantId::Invalid());
            const auto activeRevision = service.GetRevision();
            game.RetireUnpassedMatchProposals();
            CHECK(service.GetRevision() == activeRevision);
            CHECK(service.GetProposal(scope)->IsActive());
            int terminalTime = 20;
            switch (terminal) {
                case 0:
                    CHECK(service.Cancel(42, scope, 1, MP_PROPOSAL_CANCEL_PROPOSER,
                        mpProposalEngineTime::FromMilliseconds(terminalTime),
                        service.GetRevision()).WasApplied());
                    break;
                case 1:
                    terminalTime = 100;
                    CHECK(service.Expire(42, mpProposalEngineTime::FromMilliseconds(terminalTime),
                        service.GetRevision()).WasApplied());
                    break;
                case 2:
                    CHECK(service.CastBallot(42, scope, 1, 20, MP_PROPOSAL_BALLOT_NO,
                        mpProposalEngineTime::FromMilliseconds(terminalTime),
                        service.GetRevision()).WasApplied());
                    break;
                default:
                    CHECK(service.InvalidateForPhase(42, GAMEON,
                        mpProposalEngineTime::FromMilliseconds(terminalTime),
                        service.GetRevision()).WasApplied());
                    break;
            }
            CHECK(service.GetProposal(scope)->IsTerminal());
            const auto terminalRevision = service.GetRevision();
            // The live adapter must observe the terminal state before releasing
            // its service slot, including deadlines and phase invalidations.
            game.RetireUnpassedMatchProposals();
            CHECK(!service.GetProposal(scope)->IsOccupied());
            CHECK(service.GetRevision() == terminalRevision + 1);
            CHECK(game.terminalEvents[rawScope] == 1);
            game.RetireUnpassedMatchProposals();
            CHECK(service.GetRevision() == terminalRevision + 1);
            CHECK(game.terminalEvents[rawScope] == 1);
            CHECK(!service.GetProposal(scope)->IsOccupied());

            auto replacement = Proposal(42, 2, scope, MP_MATCH_OP_RULES_COMMIT,
                terminalTime + 1, terminalTime + 400, 2, 2, 2,
                MP_PROPOSAL_CALLER_VOTE_YES);
            CHECK(service.Create(replacement, service.GetRevision()).reason ==
                MP_PROPOSAL_REASON_COOLDOWN_ACTIVE);
            replacement.createdAt = mpProposalEngineTime::FromMilliseconds(terminalTime + 200);
            CHECK(service.Create(replacement, service.GetRevision()).WasApplied());
            CHECK(service.GetProposal(scope)->proposalId == 2);
            CHECK(service.CastBallot(42, scope, 1, 20, MP_PROPOSAL_BALLOT_YES,
                replacement.createdAt, service.GetRevision()).reason ==
                MP_PROPOSAL_REASON_PROPOSAL_MISMATCH);
            CHECK(service.CastBallot(42, scope, 2, 20, MP_PROPOSAL_BALLOT_YES,
                replacement.createdAt, service.GetRevision()).WasApplied());
            CHECK(service.GetProposal(scope)->status == MP_PROPOSAL_STATUS_PASSED);
            const auto passedRevision = service.GetRevision();
            game.RetireUnpassedMatchProposals();
            CHECK(service.GetRevision() == passedRevision);
            CHECK(service.GetProposal(scope)->status == MP_PROPOSAL_STATUS_PASSED);
            // Passage remains retained for the existing executor and explicit
            // acknowledgement; terminal cleanup must never consume that work.
            CHECK(service.Acknowledge(42, scope, 2, service.GetRevision()).WasApplied());
            CHECK(service.Acknowledge(42, scope, 2, service.GetRevision()).WasRejected());
            CHECK(service.ValidateInvariants());
            ++cases;
        }
    }
    // Concurrent terminal scopes must all retire using the current service CAS.
    idMultiplayerGame concurrent;
    mpProposalCooldownPolicy_t noCooldowns;
    noCooldowns.Clear();
    auto &service = concurrent.matchProposals;
    CHECK(service.Reset(42, mpProposalEngineTime::FromMilliseconds(0), noCooldowns));
    for (int rawScope = 0; rawScope < MP_PROPOSAL_SCOPE_COUNT; ++rawScope) {
        auto proposal = Proposal(42, rawScope + 1,
            static_cast<mpProposalScope_t>(rawScope), MP_MATCH_OP_RULES_COMMIT,
            10, 100, 2, 2, 2, MP_PROPOSAL_CALLER_VOTE_YES);
        CHECK(service.Create(proposal, service.GetRevision()).WasApplied());
    }
    CHECK(service.Expire(42, mpProposalEngineTime::FromMilliseconds(100),
        service.GetRevision()).WasApplied());
    concurrent.RetireUnpassedMatchProposals();
    CHECK(service.GetOccupiedScopeMask() == 0);
    CHECK(service.ValidateInvariants());
    for (int scope = 0; scope < MP_PROPOSAL_SCOPE_COUNT; ++scope) {
        CHECK(concurrent.terminalEvents[scope] == 1);
    }
    CHECK(gameLocal.warnings == 0);
    std::printf("proposal lifecycle: PASS (%d terminal replacements; concurrent expiry)\n", cases);
    return 0;
}
'''


def main() -> None:
    multiplayer = (ROOT / "src/game/MultiplayerGame.cpp").read_text(encoding="utf-8")
    frame = function(multiplayer, "void idMultiplayerGame::BeginCompetitiveFrame")
    retirement = frame.index("RetireUnpassedMatchProposals();")
    if not (frame.index("ProcessPassedMatchProposals();") <
            frame.rindex("matchProposals.InvalidateForPhase(") < retirement):
        raise AssertionError("terminal proposals must retire after passage and phase invalidation")
    adapter = function(multiplayer, "void idMultiplayerGame::RetireUnpassedMatchProposals")
    # Reuse the service contract's narrow protocol fixtures; compile the actual
    # proposal service and live adapter method, not a model of their behavior.
    preamble = SERVICE_HARNESS.split("static bool AppliedOnce", 1)[0]
    harness = preamble + HARNESS.replace("@ADAPTER@", adapter)
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("a native C++ compiler is required")
    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="proposal-lifecycle-", dir=temp_root) as temp:
        cpp, executable = Path(temp) / "test.cpp", Path(temp) / "test.exe"
        cpp.write_text(harness, encoding="utf-8")
        command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                   *shlex.split(os.environ.get("CXXFLAGS", "")),
                   "-DMP_PROPOSAL_STANDALONE_TEST", f"-I{ROOT / 'src'}",
                   str(cpp), str(ROOT / "src/game/mp/match/MatchProposal.cpp"),
                   "-o", str(executable)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)
    print("mp_match_proposal_lifecycle_contract: PASS")


if __name__ == "__main__":
    main()
