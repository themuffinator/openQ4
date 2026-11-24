#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Exercise forced round conversions against live admission, locks and roster seats."""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_vote_electorate_contract import function

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <cstdio>
#include <cstdlib>
#include "game/mp/match/MatchSession.h"
#include "game/mp/match/MatchTeams.h"
#include "game/mp/match/MatchTeamCommunication.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
#define ACCEPT(x) CHECK(!(x).WasRejected())
static void phase(mpMatchSession& s,mpGameState_t next,mpMatchTransitionReason_t why) {
    ACCEPT(s.TransitionPhase(next,why,mpParticipantId::Invalid(),s.GetSessionRevision()));
    CHECK(s.ValidateInvariants());
}
static void exercise(int count,bool enabled,bool rostered) {
    mpMatchSession session;
    CHECK(session.Reset(77,mpMatchEngineTime::FromMilliseconds(0)));
    mpMatchReadinessPolicy policy;
    policy.teamMode=true;
    policy.policy=MP_MATCH_READY_INDIVIDUAL;
    policy.readyThresholdBasisPoints=10000;
    policy.minimumActiveHumans=2;
    policy.requiredSideMask=3;
    policy.minimumActivePerRequiredSide=1;
    policy.maximumActivePerSide=count/2;
    policy.allowRoundSideChanges=enabled;
    policy.requireDeclaredRosterSeats=rostered;
    ACCEPT(session.ConfigureReadiness(policy,session.GetSessionRevision()));
    ACCEPT(session.FreezeRules(1,123,session.GetSessionRevision()));
    phase(session,WARMUP,MP_MATCH_TRANSITION_SESSION_INITIALIZED);
    mpParticipantId players[32];
    for(int i=0;i<count;++i) {
        ACCEPT(session.BindParticipant(i,true,MPMatchRoleBit(MP_MATCH_ROLE_PLAYER),session.GetSessionRevision(),players[i]));
        ACCEPT(session.SetParticipantSide(players[i],i&1,session.GetSessionRevision()));
        ACCEPT(session.SetParticipantActive(players[i],true,session.GetSessionRevision()));
        if(rostered) {
            const auto role=i<2?MP_MATCH_ROSTER_CAPTAIN:MP_MATCH_ROSTER_PLAYER;
            ACCEPT(session.SetParticipantRoles(players[i],MPMatchPrincipalRolesForRosterRole(role),session.GetSessionRevision()));
            ACCEPT(session.DeclareRosterSeat(i,i&1,role,true,session.GetSessionRevision()));
            ACCEPT(session.AssignRosterSeat(i,players[i],session.GetSessionRevision()));
        }
    }
    for(int i=0;i<count;++i) ACCEPT(session.SetParticipantReady(players[i],true,session.GetSessionRevision()));
    CHECK(session.SetParticipantRoundSide(players[1],0,session.GetSessionRevision()).WasRejected());
    mpMatchTeams teams;
    CHECK(teams.Reset(session.GetSessionId(),mpMatchEngineTime::FromMilliseconds(0)));
    ACCEPT(teams.SetSideLocked(session,0,true,teams.GetRevision()));
    ACCEPT(teams.SetSideLocked(session,1,true,teams.GetRevision()));
    const auto lockRevision=teams.GetRevision();
    mpMatchTeamsPolicy_t admission;
    admission.Clear(); admission.teamMode=true; admission.maximumActiveTotal=count;
    admission.maximumActivePerSide=count/2; admission.requireRosterMembership=rostered;
    CHECK(!teams.EvaluateJoin(session,players[1],0,0,admission,mpMatchEngineTime::FromMilliseconds(0)).IsAllowed());
    mpMatchTeamCommunicationBinding_t sender,recipient;
    uint32_t senderGeneration=0,recipientGeneration=0;
    CHECK(session.GetSlotGeneration(0,senderGeneration) && session.GetSlotGeneration(1,recipientGeneration));
    CHECK(MPMatchBuildTeamCommunicationBinding(session,0,senderGeneration,sender));
    CHECK(MPMatchBuildTeamCommunicationBinding(session,1,recipientGeneration,recipient));
    CHECK(!MPMatchMayReceiveManagedTeamText(session,sender,recipient));
    phase(session,COUNTDOWN,MP_MATCH_TRANSITION_READY_GATE);
    phase(session,GAMEON,MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE);
    ACCEPT(session.TransitionRound(RS_COUNTDOWN,MP_MATCH_ROUND_TRANSITION_PARENT_ACTIVE,session.GetSessionRevision()));
    ACCEPT(session.TransitionRound(RS_ACTIVE,MP_MATCH_ROUND_TRANSITION_COUNTDOWN_COMPLETE,session.GetSessionRevision()));
    auto revision=session.GetSessionRevision();
    CHECK(session.SetParticipantRoundSide(players[1],0,revision-1).WasRejected());
    CHECK(session.SetParticipantRoundSide(players[1],-1,revision).WasRejected());
    CHECK(session.SetParticipantRoundSide(mpParticipantId::Invalid(),0,revision).WasRejected());
    CHECK(session.GetSessionRevision()==revision);
    CHECK(session.SetParticipantSide(players[1],0,revision).WasRejected());
    CHECK(!teams.EvaluateJoin(session,players[1],0,0,admission,mpMatchEngineTime::FromMilliseconds(0)).IsAllowed());
    if(!enabled) {
        CHECK(session.SetParticipantRoundSide(players[1],0,revision).WasRejected());
        CHECK(session.GetSessionRevision()==revision && session.FindParticipant(players[1])->side==1);
        return;
    }
    // Every active player may convert even when a full locked side absorbs the
    // whole server. No invitation, queue entry, roster seat or lock is consumed.
    for(int i=0;i<count;++i) {
        ACCEPT(session.SetParticipantRoundSide(players[i],0,session.GetSessionRevision()));
        CHECK(session.FindParticipant(players[i])->side==0);
        CHECK(session.FindParticipant(players[i])->active);
        if(rostered) {
            CHECK(session.FindRosterSeat(players[i])==i);
            CHECK(session.GetRosterSeat(i)->side==(i&1));
            CHECK(session.GetRosterSeat(i)->occupant==players[i]);
            CHECK(session.FindParticipant(players[i])->roles==MPMatchPrincipalRolesForRosterRole(session.GetRosterSeat(i)->role));
        }
        CHECK(session.ValidateInvariants());
    }
    CHECK(teams.GetRevision()==lockRevision && teams.IsSideLocked(0) && teams.IsSideLocked(1));
    CHECK(teams.GetInvitationCount()==0 && teams.GetQueueCount()==0 && teams.ValidateInvariants());
    CHECK(MPMatchMayReceiveManagedTeamText(session,sender,recipient));
    CHECK(MPMatchMayReceiveManagedTeamVoice(session,sender,recipient));
    auto paused=session;
    ACCEPT(paused.RequestTechnicalPause(MP_MATCH_PAUSE_REASON_REFEREE,paused.GetSessionRevision()));
    CHECK(paused.SetParticipantRoundSide(players[1],1,paused.GetSessionRevision()).WasRejected());
    auto withdrawn=session;
    if(rostered) ACCEPT(withdrawn.VacateRosterSeat(1,withdrawn.GetSessionRevision()));
    ACCEPT(withdrawn.SetParticipantActive(players[1],false,withdrawn.GetSessionRevision()));
    CHECK(withdrawn.SetParticipantRoundSide(players[1],1,withdrawn.GetSessionRevision()).WasRejected());
    uint32_t generation=0;
    CHECK(withdrawn.GetSlotGeneration(1,generation));
    ACCEPT(withdrawn.UnbindParticipant(1,generation,withdrawn.GetSessionRevision()));
    mpParticipantId replacement;
    ACCEPT(withdrawn.BindParticipant(1,true,MPMatchRoleBit(MP_MATCH_ROLE_PLAYER),withdrawn.GetSessionRevision(),replacement));
    CHECK(withdrawn.SetParticipantRoundSide(players[1],1,withdrawn.GetSessionRevision()).WasRejected());
    CHECK(withdrawn.SetParticipantRoundSide(replacement,1,withdrawn.GetSessionRevision()).WasRejected());
    ACCEPT(session.TransitionRound(RS_COMPLETE,MP_MATCH_ROUND_TRANSITION_RESULT_COMMITTED,session.GetSessionRevision()));
    CHECK(session.SetParticipantRoundSide(players[1],1,session.GetSessionRevision()).WasRejected());
    auto review=session;
    phase(review,GAMEREVIEW,MP_MATCH_TRANSITION_LIMIT_REACHED);
    CHECK(review.SetParticipantRoundSide(players[1],1,review.GetSessionRevision()).WasRejected());
    phase(review,NEXTGAME,MP_MATCH_TRANSITION_REVIEW_COMPLETE);
    phase(review,WARMUP,MP_MATCH_TRANSITION_SAME_MAP_RESTART);
    auto fixedPolicy=policy; fixedPolicy.allowRoundSideChanges=false;
    if(rostered) CHECK(review.ConfigureReadiness(fixedPolicy,review.GetSessionRevision()).WasRejected());
    for(int i=0;i<count;++i) ACCEPT(review.SetParticipantSide(players[i],i&1,review.GetSessionRevision()));
    ACCEPT(review.ConfigureReadiness(fixedPolicy,review.GetSessionRevision()));
    CHECK(review.ValidateInvariants());
    ACCEPT(session.TransitionRound(RS_COUNTDOWN,MP_MATCH_ROUND_TRANSITION_NEXT_ROUND,session.GetSessionRevision()));
    for(int i=0;i<count;++i) ACCEPT(session.SetParticipantRoundSide(players[i],i&1,session.GetSessionRevision()));
    CHECK(session.ValidateInvariants());
    CHECK(!MPMatchMayReceiveManagedTeamText(session,sender,recipient));
    CHECK(!MPMatchMayReceiveManagedTeamVoice(session,sender,recipient));
}
int main() {
    for(int count: {2,4,32}) for(bool enabled: {false,true}) for(bool rostered: {false,true}) exercise(count,enabled,rostered);
    mpMatchSession session;
    CHECK(session.Reset(7,mpMatchEngineTime::FromMilliseconds(0)));
    mpMatchReadinessPolicy invalid; invalid.allowRoundSideChanges=true;
    CHECK(session.ConfigureReadiness(invalid,session.GetSessionRevision()).WasRejected());
}
'''


def main() -> None:
    source = (ROOT / "src/game/MultiplayerGame.cpp").read_text(encoding="utf-8")
    modes = (ROOT / "src/game/mp/RoundModes.cpp").read_text(encoding="utf-8")
    adapter = function(source, "bool idMultiplayerGame::ApplyRoundTeamAssignment(")
    policy = function(source, "mpMatchTeamsPolicy_t idMultiplayerGame::BuildMatchTeamsPolicy(")
    assert "policy.allowLiveJoin = false" in policy
    assert "GTF_TEAMSWAP" in adapter and "player->health <= 0" in adapter
    assert adapter.index("matchSession = candidate") < adapter.index('Set( "ui_team"')
    assert "SetParticipantRoundSide" in adapter and "candidate.ValidateInvariants()" in adapter
    for method in ("void rvRedRoverGameState::PlayerDeath(", "void rvRedRoverGameState::PrepareNextRound("):
        assert "ApplyRoundTeamAssignment" in function(modes, method)
    assert "targetTeam = seat->side" in function(modes, "void rvRedRoverGameState::PrepareNextRound(")
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for round team regressions")
    with tempfile.TemporaryDirectory(prefix="round-teams-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        cpp, exe = path / "teams.cpp", path / ("teams.exe" if os.name == "nt" else "teams")
        cpp.write_text("#include <initializer_list>\n" + HARNESS, encoding="utf-8")
        command = [compiler, "-std=c++17", "-DMP_MATCH_SESSION_STANDALONE_TEST", "-DMP_MATCH_TEAMS_STANDALONE_TEST",
                   f"-I{ROOT / 'src'}", str(cpp), str(ROOT / "src/game/mp/match/MatchSession.cpp"),
                   str(ROOT / "src/game/mp/match/MatchTeams.cpp"),
                   str(ROOT / "src/game/mp/match/MatchTeamCommunication.cpp"), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
    print("mp_match_round_teams_contract: PASS")


if __name__ == "__main__":
    main()
