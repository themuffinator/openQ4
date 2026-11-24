#!/usr/bin/env python3
"""Execute the casual caster grant against the real match session and disclosure policy.

The production bodies of idMultiplayerGame::SetCasualCasterRole,
ServerRequestCasterRole and UpdateCasualCasters are compiled with the real
MatchSession and MatchDisclosurePolicy and the operator path's own target
rule (MPOperationBroadcasterTargetIsValid). The harness checks who may take the caster view, what
the disclosure policy then grants them, and every way the view is withdrawn:
the policy switched off, a managed match, joining the game, an operator's role
change, and the slot changing hands. Single-reversal controls mutate the
extracted bodies and must fail. Source checks cover the wiring a harness cannot
reach: where the withdrawal runs each frame, the casual spectator's neutral
side, and that the client panel reads only the vitals the server disclosed.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_disclosure_policy_contract import HARNESS as POLICY_HARNESS, function_body

ROOT = Path(__file__).resolve().parents[2]
GAME = "src/game/MultiplayerGame.cpp"

SIGNATURES = (
    "bool idMultiplayerGame::SetCasualCasterRole( int clientNum, bool enabled )",
    "const char *idMultiplayerGame::ServerRequestCasterRole( int clientNum, bool enabled )",
    "void idMultiplayerGame::UpdateCasualCasters( void )",
    "void idMultiplayerGame::ReleaseBroadcasterOnJoin( int clientNum, mpParticipantId participant )",
)

MUTATIONS = (
    ("a managed match accepts a self-service caster",
     "if ( IsManagedMatch() || !g_casterAccess.GetBool() ) {", "if ( !g_casterAccess.GetBool() ) {"),
    ("the caster view is granted with the policy off",
     "if ( IsManagedMatch() || !g_casterAccess.GetBool() ) {", "if ( IsManagedMatch() ) {"),
    ("a player takes the caster view",
     "!MPOperationBroadcasterTargetIsValid( matchSession, participant ) ) {", "false ) {"),
    ("giving the view up strips an operator's grant",
     "if ( casualCaster[ clientNum ] ) {\n\t\t\tSetCasualCasterRole( clientNum, false );",
     "if ( true ) {\n\t\t\tSetCasualCasterRole( clientNum, false );"),
    ("a caster who joins the game keeps the view",
     "if ( allowed && !state->active && state->side == MP_MATCH_SIDE_NONE ) {", "if ( allowed ) {"),
    ("the view outlives the policy",
     "const bool allowed = g_casterAccess.GetBool() && !IsManagedMatch();", "const bool allowed = true;"),
    ("a broadcaster who joins the game keeps the role",
     "if ( state == NULL || state->roles != MPMatchRoleBit( MP_MATCH_ROLE_BROADCASTER ) ) {\n\t\treturn;\n\t}\n\tif ( clientNum >= 0",
     "if ( true ) {\n\t\treturn;\n\t}\n\tif ( clientNum >= 0"),
    ("a role the operator changed is withdrawn as if self-granted",
     "if ( state == NULL || !state->connected || state->roles != broadcasterRole ) {",
     "if ( state == NULL || !state->connected ) {"),
)

HARNESS = r'''
#include <cassert>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "game/mp/match/MatchSession.h"
#include "game/mp/match/MatchDisclosurePolicy.h"

static const int MAX_CLIENTS = 32;
struct CVar { bool value = false; bool GetBool() const { return value; } } g_casterAccess;
struct Game {
    bool isServer = true;
    std::vector<std::pair<int, std::string> > notices;
    void ServerSendChatMessage( int slot, const char *, const char *text, const char * ) { notices.emplace_back( slot, text ); }
} gameLocal;

class idMultiplayerGame {
public:
    mpMatchSession matchSession;
    bool matchSessionOperational = true;
    bool managed = false;
    bool casualCaster[ MAX_CLIENTS ] = {};
    int viewsSent = 0;
    bool IsManagedMatch() const { return managed; }
    void AdvanceMatchViewRevision( bool ) {}
    void SendChangedMatchViews() { ++viewsSent; }
    bool SetCasualCasterRole( int clientNum, bool enabled );
    const char *ServerRequestCasterRole( int clientNum, bool enabled );
    void UpdateCasualCasters( void );
    void ReleaseBroadcasterOnJoin( int clientNum, mpParticipantId participant );
    // SynchronizeMatchParticipant's casual join, in its production order.
    bool Join( int clientNum, mpParticipantId participant, int side ) {
        ReleaseBroadcasterOnJoin( clientNum, participant );
        return !matchSession.SetParticipantActive( participant, true, matchSession.GetSessionRevision() ).WasRejected() &&
            !matchSession.SetParticipantSide( participant, side, matchSession.GetSessionRevision() ).WasRejected();
    }
};
// The operator path's own target rule, from MatchOperations.cpp.
bool MPOperationBroadcasterTargetIsValid( const mpMatchSession &session,
		mpParticipantId participant ) {@TARGET@}
@BODIES@

static idMultiplayerGame *mp;
static mpParticipantId Bind( int slot, bool human, bool active, int side ) {
    mpParticipantId id;
    assert( !mp->matchSession.BindParticipant( slot, human, MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ),
        mp->matchSession.GetSessionRevision(), id ).WasRejected() );
    assert( !mp->matchSession.SetParticipantActive( id, active, mp->matchSession.GetSessionRevision() ).WasRejected() );
    assert( !mp->matchSession.SetParticipantSide( id, side, mp->matchSession.GetSessionRevision() ).WasRejected() );
    return id;
}
static mpMatchRoleMask_t Roles( mpParticipantId id ) { return mp->matchSession.FindParticipant( id )->roles; }
// The casual policy idMultiplayerGame::BuildMatchDisclosurePolicy builds: no
// locked sides, and no item timing for anyone.
static mpMatchDisclosureGrant_t Grant( int slot ) {
    mpMatchDisclosurePolicy_t policy;
    policy.Clear();
    policy.lockedSpectatorSideMask = 0;
    policy.allowCoachObservation = true;
    policy.allowLiveBroadcasterObservation = true;
    policy.allowRefereeObservation = true;
    uint32_t generation = 0;
    mpParticipantId id;
    assert( mp->matchSession.GetSlotGeneration( slot, generation ) && mp->matchSession.ResolveSlotBinding( slot, generation, id ) );
    const mpMatchParticipantState *state = mp->matchSession.FindParticipant( id );
    mpMatchDisclosureRecipient_t recipient;
    recipient.Clear();
    recipient.sessionId = mp->matchSession.GetSessionId();
    recipient.sessionRevision = mp->matchSession.GetSessionRevision();
    recipient.participantId = id.SequencePart();
    recipient.slot = slot;
    recipient.bindingGeneration = generation;
    recipient.side = state->side;
    recipient.roles = state->roles;
    recipient.active = state->active;
    recipient.repeater = false;
    mpMatchDisclosureGrant_t grant;
    grant.Clear();
    MPMatchDisclosureBuildGrant( policy, recipient, grant );
    return grant;
}
static bool Is( const char *notice, const char *expected ) { return notice != NULL && std::strcmp( notice, expected ) == 0; }

int main() {
    const mpMatchRoleMask_t player = MPMatchRoleBit( MP_MATCH_ROLE_PLAYER );
    const mpMatchRoleMask_t broadcaster = MPMatchRoleBit( MP_MATCH_ROLE_BROADCASTER );
    idMultiplayerGame game;
    mp = &game;
    assert( game.matchSession.Reset( 7, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
    assert( !game.matchSession.TransitionPhase( WARMUP, MP_MATCH_TRANSITION_SESSION_INITIALIZED,
        mpParticipantId::Invalid(), game.matchSession.GetSessionRevision() ).WasRejected() );
    mpMatchReadinessPolicy readiness = game.matchSession.GetReadinessPolicy();
    readiness.teamMode = true;
    assert( !game.matchSession.ConfigureReadiness( readiness, game.matchSession.GetSessionRevision() ).WasRejected() );
    const mpParticipantId watcher = Bind( 0, true, false, MP_MATCH_SIDE_NONE );
    const mpParticipantId marine = Bind( 1, true, true, 0 );
    const mpParticipantId bot = Bind( 2, false, true, 1 );
    const mpParticipantId second = Bind( 3, true, false, MP_MATCH_SIDE_NONE );
    ( void )marine; ( void )bot;

    // An ordinary casual spectator may follow both sides and is told nothing more.
    assert( Grant( 0 ).valid && Grant( 0 ).principal == MP_MATCH_DISCLOSURE_PRINCIPAL_SPECTATOR );
    assert( Grant( 0 ).followSideMask == MPMatchDisclosureAllSideBits() && !Grant( 0 ).itemTimingAllowed );

    // Off by default: the operator grants the view, a spectator cannot take it.
    assert( Is( game.ServerRequestCasterRole( 0, true ), "#str_232217" ) && Roles( watcher ) == player && !game.casualCaster[ 0 ] );
    g_casterAccess.value = true;
    game.managed = true;
    assert( Is( game.ServerRequestCasterRole( 0, true ), "#str_232217" ) && Roles( watcher ) == player );
    game.managed = false;

    // Granted to a spectator who has not joined; never to a player, a bot or a bad slot.
    const int viewsBefore = game.viewsSent;
    assert( Is( game.ServerRequestCasterRole( 0, true ), "#str_232215" ) && Roles( watcher ) == broadcaster && game.casualCaster[ 0 ] );
    assert( game.viewsSent == viewsBefore + 1 );
    assert( Grant( 0 ).valid && Grant( 0 ).principal == MP_MATCH_DISCLOSURE_PRINCIPAL_BROADCASTER );
    assert( Grant( 0 ).followSideMask == MPMatchDisclosureAllSideBits() && !Grant( 0 ).itemTimingAllowed );
    assert( Is( game.ServerRequestCasterRole( 1, true ), "#str_232218" ) && Roles( marine ) == player && !game.casualCaster[ 1 ] );
    assert( Is( game.ServerRequestCasterRole( 2, true ), "#str_232218" ) && Roles( bot ) == player && !game.casualCaster[ 2 ] );
    assert( Is( game.ServerRequestCasterRole( 9, true ), "#str_232218" ) && !game.casualCaster[ 9 ] );
    // A watching bot, and a watcher still affiliated with a side, are not neutral people.
    const mpParticipantId idleBot = Bind( 4, false, false, MP_MATCH_SIDE_NONE );
    const mpParticipantId benched = Bind( 5, true, false, 1 );
    assert( Is( game.ServerRequestCasterRole( 4, true ), "#str_232218" ) && Roles( idleBot ) == player && !game.casualCaster[ 4 ] );
    assert( Is( game.ServerRequestCasterRole( 5, true ), "#str_232218" ) && Roles( benched ) == player && !game.casualCaster[ 5 ] );
    assert( game.ServerRequestCasterRole( -1, true ) == NULL && game.ServerRequestCasterRole( MAX_CLIENTS, true ) == NULL );
    gameLocal.isServer = false;
    assert( game.ServerRequestCasterRole( 3, true ) == NULL && Roles( second ) == player );
    gameLocal.isServer = true;
    // Asking again changes nothing; several spectators may cast at once.
    assert( Is( game.ServerRequestCasterRole( 0, true ), "#str_232215" ) && Roles( watcher ) == broadcaster );
    assert( Is( game.ServerRequestCasterRole( 3, true ), "#str_232215" ) && Roles( second ) == broadcaster );

    // While the conditions hold nothing is withdrawn and nobody is messaged.
    game.UpdateCasualCasters();
    assert( Roles( watcher ) == broadcaster && Roles( second ) == broadcaster && gameLocal.notices.empty() );

    // Giving it up.
    assert( Is( game.ServerRequestCasterRole( 3, false ), "#str_232216" ) && Roles( second ) == player && !game.casualCaster[ 3 ] );
    assert( Grant( 3 ).principal == MP_MATCH_DISCLOSURE_PRINCIPAL_SPECTATOR );

    // The policy is switched off: withdrawn the same frame, with a notice.
    g_casterAccess.value = false;
    game.UpdateCasualCasters();
    assert( Roles( watcher ) == player && !game.casualCaster[ 0 ] );
    assert( gameLocal.notices.size() == 1 && gameLocal.notices[ 0 ].first == 0 && gameLocal.notices[ 0 ].second == "#str_232216" );
    assert( Grant( 0 ).principal == MP_MATCH_DISCLOSURE_PRINCIPAL_SPECTATOR );
    game.UpdateCasualCasters();
    assert( gameLocal.notices.size() == 1 );

    // A managed match begins.
    g_casterAccess.value = true;
    assert( Is( game.ServerRequestCasterRole( 0, true ), "#str_232215" ) );
    game.managed = true;
    game.UpdateCasualCasters();
    assert( Roles( watcher ) == player && !game.casualCaster[ 0 ] && gameLocal.notices.size() == 2 );
    game.managed = false;

    // The caster joins the game. The session will not activate a participant
    // without the player role, so the join itself gives the role up: they are
    // an ordinary player before any view is built, not a playing broadcaster.
    assert( Is( game.ServerRequestCasterRole( 0, true ), "#str_232215" ) );
    assert( game.matchSession.SetParticipantActive( watcher, true, game.matchSession.GetSessionRevision() ).WasRejected() );
    assert( Roles( watcher ) == broadcaster && Grant( 0 ).principal == MP_MATCH_DISCLOSURE_PRINCIPAL_BROADCASTER );
    assert( game.Join( 0, watcher, 1 ) );
    assert( Roles( watcher ) == player && !game.casualCaster[ 0 ] );
    assert( Grant( 0 ).valid && Grant( 0 ).principal == MP_MATCH_DISCLOSURE_PRINCIPAL_PLAYER );
    game.UpdateCasualCasters();
    assert( Roles( watcher ) == player && gameLocal.notices.size() == 2 );
    // So does an operator-granted broadcaster, and a plain player is untouched.
    assert( !game.matchSession.SetParticipantRoles( second, broadcaster, game.matchSession.GetSessionRevision() ).WasRejected() );
    assert( game.Join( 3, second, 0 ) && Roles( second ) == player );
    assert( Grant( 3 ).principal == MP_MATCH_DISCLOSURE_PRINCIPAL_PLAYER );
    assert( !game.matchSession.SetParticipantActive( second, false, game.matchSession.GetSessionRevision() ).WasRejected() );
    assert( !game.matchSession.SetParticipantSide( second, MP_MATCH_SIDE_NONE, game.matchSession.GetSessionRevision() ).WasRejected() );
    game.ReleaseBroadcasterOnJoin( 1, marine );
    assert( Roles( marine ) == player );
    // The update is a second guard: a caster the session shows as playing is withdrawn too.
    assert( !game.matchSession.SetParticipantActive( watcher, false, game.matchSession.GetSessionRevision() ).WasRejected() );
    assert( !game.matchSession.SetParticipantSide( watcher, MP_MATCH_SIDE_NONE, game.matchSession.GetSessionRevision() ).WasRejected() );
    assert( Is( game.ServerRequestCasterRole( 0, true ), "#str_232215" ) );
    assert( !game.matchSession.SetParticipantSide( watcher, 1, game.matchSession.GetSessionRevision() ).WasRejected() );
    game.UpdateCasualCasters();
    assert( Roles( watcher ) == player && !game.casualCaster[ 0 ] && gameLocal.notices.size() == 3 );
    assert( !game.matchSession.SetParticipantActive( watcher, true, game.matchSession.GetSessionRevision() ).WasRejected() );
    assert( Is( game.ServerRequestCasterRole( 0, true ), "#str_232218" ) );
    assert( !game.matchSession.SetParticipantActive( watcher, false, game.matchSession.GetSessionRevision() ).WasRejected() );
    assert( !game.matchSession.SetParticipantSide( watcher, MP_MATCH_SIDE_NONE, game.matchSession.GetSessionRevision() ).WasRejected() );

    // An operator's own change of the role is the operator's: forgotten, not undone.
    assert( Is( game.ServerRequestCasterRole( 0, true ), "#str_232215" ) );
    assert( !game.matchSession.SetParticipantRoles( watcher, player, game.matchSession.GetSessionRevision() ).WasRejected() );
    game.UpdateCasualCasters();
    assert( !game.casualCaster[ 0 ] && Roles( watcher ) == player && gameLocal.notices.size() == 3 );

    // An operator-granted broadcaster is not a casual caster: the policy does
    // not withdraw it, and the spectator's own "off" does not strip it.
    assert( !game.matchSession.SetParticipantRoles( second, broadcaster, game.matchSession.GetSessionRevision() ).WasRejected() );
    g_casterAccess.value = false;
    game.UpdateCasualCasters();
    assert( Roles( second ) == broadcaster && gameLocal.notices.size() == 3 );
    assert( Is( game.ServerRequestCasterRole( 3, false ), "#str_232216" ) && Roles( second ) == broadcaster );
    g_casterAccess.value = true;

    // The slot changes hands: the next connection in it starts with nothing.
    assert( Is( game.ServerRequestCasterRole( 0, true ), "#str_232215" ) );
    uint32_t generation = 0;
    assert( game.matchSession.GetSlotGeneration( 0, generation ) );
    assert( !game.matchSession.UnbindParticipant( 0, generation, game.matchSession.GetSessionRevision() ).WasRejected() );
    game.UpdateCasualCasters();
    assert( !game.casualCaster[ 0 ] && gameLocal.notices.size() == 3 );
    const mpParticipantId replacement = Bind( 0, true, false, MP_MATCH_SIDE_NONE );
    game.UpdateCasualCasters();
    assert( Roles( replacement ) == player && Grant( 0 ).principal == MP_MATCH_DISCLOSURE_PRINCIPAL_SPECTATOR );
    // Even with a stale flag left set, a replacement is never promoted by the update.
    game.casualCaster[ 0 ] = true;
    game.UpdateCasualCasters();
    assert( Roles( replacement ) == player && !game.casualCaster[ 0 ] );

    // Before the session is usable nothing is granted.
    game.matchSessionOperational = false;
    assert( Is( game.ServerRequestCasterRole( 0, true ), "#str_232218" ) && Roles( replacement ) == player );
    return 0;
}
'''


def build(bodies: str, directory: Path, name: str, compiler: str, strict: bool) -> Path:
    source = directory / f"{name}.cpp"
    exe = directory / (f"{name}.exe" if os.name == "nt" else name)
    operations = (ROOT / "src/game/mp/match/MatchOperations.cpp").read_text(encoding="utf-8")
    target =function_body(operations, "bool MPOperationBroadcasterTargetIsValid( const mpMatchSession &session,")
    # The disclosure contract's preamble supplies the engine types and the real MatchView.
    preamble = POLICY_HARNESS.split("#define CHECK")[0]
    source.write_text(preamble + HARNESS.replace("@TARGET@", target).replace("@BODIES@", bodies), encoding="utf-8")
    match = ROOT / "src/game/mp/match"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra"] + (["-Werror"] if strict else []) + [
        "-DMP_MATCH_SESSION_STANDALONE_TEST", "-DMP_MATCH_DISCLOSURE_STANDALONE_TEST", "-I" + str(ROOT / "src"), str(source),
        str(match / "MatchDisclosurePolicy.cpp"), str(match / "MatchSession.cpp"), "-o", str(exe)],
        check=True, capture_output=not strict)
    return exe


def main() -> None:
    game = (ROOT / GAME).read_text(encoding="utf-8").replace("\r\n", "\n")
    bodies = "\n".join(signature + " {" + function_body(game, signature) + "}\n" for signature in SIGNATURES)

    # Wiring the harness cannot reach.
    assert 'idCVar g_casterAccess( "g_casterAccess", "0", CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL,' in game, "the caster view must be off by default"
    run = function_body(game, "void idMultiplayerGame::Run( void )")
    assert run.index("SynchronizeAllMatchParticipants();") < run.index("UpdateCasualCasters();") < run.index("CheckVote();"), \
        "caster withdrawal must run each frame after participants are synchronized"
    assert "memset( casualCaster, 0, sizeof( casualCaster ) );" in function_body(game, "void idMultiplayerGame::Clear()")
    sync = function_body(game, "void idMultiplayerGame::SynchronizeMatchParticipant( int clientNum )")
    assert "\tif ( active ) {\n\t\tReleaseBroadcasterOnJoin( clientNum, participant );\n\t}\n\tif ( matchSession.SetParticipantActive( participant, active," in sync, \
        "a casual join must give up the broadcaster role before the session activates the player"
    assert "matchSession.SetParticipantSide( participant, active ? side : MP_MATCH_SIDE_NONE," in sync, \
        "a casual spectator must be neutral, not held to the team their userinfo names"
    policy = function_body(game, "mpMatchDisclosurePolicy_t idMultiplayerGame::BuildMatchDisclosurePolicy( void ) const")
    assert "policy.allowBroadcasterItemTiming = managedMatch;" in policy, "the caster grant must not add item timing"
    # The request reaches the grant only through the checked spectator request.
    player = (ROOT / "src/game/Player.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
    request = function_body(player, "void idPlayer::ServerSpectatorFollow( int operation, int targetSlot, int targetSpawnId )")
    assert request.index("if ( !spectating ) {") < request.index("gameLocal.mpGame.ServerRequestCasterRole( entityNumber, targetSlot != 0 )")
    assert game.count("ServerRequestCasterRole(") == 1 and player.count("ServerRequestCasterRole(") == 1
    # The panel shows the vitals of the accepted view and nothing gathered locally.
    panel = function_body(game, "void idMultiplayerGame::ProjectCasterVitals( idUserInterface *gui )")
    assert "clientMatchView.teamVitals[ index ]" in panel
    for local in ("->health", "inventory", "GetPhysics", "entities["):
        assert local not in panel, f"the caster panel reads {local} from the local game"
    project = function_body(game, "void idMultiplayerGame::ProjectClientManagedMatchContext(")
    # A player's view holds their own team's vitals too; only the broadcaster's role opens the panel.
    assert ('if ( gui->GetStateInt( "match_context_visible" ) == 0 && clientMatchView.teamVitalCount > 0 &&\n'
            '\t\t( clientMatchView.publicState.recipient.publicRoleMask &\n'
            '\t\t\tMPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_BROADCASTER ) ) != 0 ) {\n\t\tProjectCasterVitals( gui );') in project

    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the caster harness")
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="spectator-casting-", dir=output) as directory:
        path = Path(directory)
        subprocess.run([str(build(bodies, path, "caster", compiler, True))], check=True)
        for index, (label, original, mutated) in enumerate(MUTATIONS):
            if bodies.count(original) != 1:
                raise SystemExit(f"mp_spectator_casting_contract: mutation anchor not unique: {label}")
            try:
                control = build(bodies.replace(original, mutated, 1), path, f"control{index}", compiler, False)
                failed = subprocess.run([str(control)], capture_output=True).returncode != 0
            except subprocess.CalledProcessError:
                failed = False  # a control must compile and then fail at run time
            if not failed:
                raise SystemExit(f"mp_spectator_casting_contract: control did not fail: {label}")
    print(f"mp_spectator_casting_contract: PASS (grant, disclosure and every withdrawal; {len(MUTATIONS)} single-reversal controls fail)")


if __name__ == "__main__":
    main()
