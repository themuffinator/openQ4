#!/usr/bin/env python3
"""Contracts and executable invariants for the multiplayer map database (MP-13).

One database describes every multiplayer map a server can play: the gametypes
it supports, its player bounds, its per-map settings and whether it takes part
in the rotation, in votes and in the player queue. The rotation, the queue
(mymap) and the end-of-match pick (mappick) read it, and so does every list of
maps the multiplayer game shows or accepts.

The static checks pin that nothing in the multiplayer game enumerates the
engine's map decls around the database. The executable part compiles the core
on its own and walks its rules.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MP = ROOT / "src/mpgame"
HEADER = MP / "mp/MapDatabase.h"
SOURCE = MP / "mp/MapDatabase.cpp"
HOST_HEADER = MP / "mp/MapDatabaseHost.h"
HOST = MP / "mp/MapDatabaseHost.cpp"
LANGUAGES = ("english", "brazilian", "czech", "french", "german", "hungarian", "italian", "polish", "russian",
             "spanish", "turkish", "ukrainian")
STRING_IDS = range(232100, 232116)


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="replace")


def require(text: str, token: str, context: str) -> int:
    position = text.find(token)
    if position < 0:
        raise AssertionError(f"missing {token!r} in {context}")
    return position


def body(text: str, signature: str) -> str:
    """The text of the function that starts at `signature`, up to the next banner."""
    start = require(text, signature, signature)
    end = text.find("\n/*\n", start)
    return text[start:end if end > 0 else len(text)]


def validate_core() -> None:
    combined = read(HEADER) + read(SOURCE)
    for token in (
        "MP_MAPDB_MAX_MAPS = 256",
        "MP_MAPDB_TOKEN_BYTES = 64",
        "MP_MAPDB_MAX_QUEUE = 32",
        "MP_MAPDB_PICK_CANDIDATES = 3",
        "MP_MAPDB_ROTATION",
        "MP_MAPDB_VOTE",
        "MP_MAPDB_QUEUE",
        "MP_MAPDB_SOURCE_ROTATION_UNBOUNDED",
        "MP_MAPDB_QUEUE_DUPLICATE",
        "ApplyOverrides",
        "NextInRotation",
        "RandomFromRotation",
        "QueueAdd",
        "PickOpen",
        "ChooseNext",
        "ValidateInvariants",
    ):
        require(combined, token, "map database core")
    # The core is rules only; the adapter owns everything the engine provides.
    for forbidden in ("idStr", "idDict", "idCVar", "idBitMsg", "idFile", "idList<", "cmdSystem", "cvarSystem",
                      "fileSystem", "gameLocal", "common->", "declManager"):
        if re.search(r"(?<![A-Za-z_])" + re.escape(forbidden), combined):
            raise AssertionError(f"the map database core contains the engine dependency {forbidden!r}")
    # A series pool takes database tokens as they are, so the two rules agree.
    series = read(MP / "mp/match/MatchSeries.h")
    require(series, "MP_SERIES_MAP_TOKEN_BYTES = 64", "MatchSeries.h")


def validate_single_source() -> None:
    """Nothing in the multiplayer game lists the engine's map decls around the database."""
    allowed = {
        # the database's own discovery and its one decl accessor
        HOST: ("fileSystem->GetNumMaps", "fileSystem->GetMapDecl"),
        HOST_HEADER: ("fileSystem->GetNumMaps", "fileSystem->GetMapDecl"),
        # a developer levelshot command resolving one named map for capture; it lists nothing to players
        MP / "gamesys/SysCmds.cpp": ("fileSystem->GetNumMaps", "fileSystem->GetMapDecl"),
    }
    for path in sorted(MP.rglob("*.cpp")) + sorted(MP.rglob("*.h")):
        text = read(path)
        for token in ("fileSystem->GetNumMaps", "fileSystem->GetMapDecl"):
            if token in text and token not in allowed.get(path, ()):
                raise AssertionError(f"{path.relative_to(ROOT)} reads {token} around the map database")
    commands = read(MP / "gamesys/SysCmds.cpp")
    helper = body(commands, "static bool Cmd_GetLevelshotMapDeclForNormalizedPath(")
    if commands.count("fileSystem->GetNumMaps") != helper.count("fileSystem->GetNumMaps") or \
            commands.count("fileSystem->GetMapDecl") != helper.count("fileSystem->GetMapDecl"):
        raise AssertionError("SysCmds.cpp may read map decls only in its levelshot helper")
    # The file scan PickMap used listed maps the database might not hold.
    game = read(MP / "MultiplayerGame.cpp")
    if 'ListFiles( "maps/mp"' in game:
        raise AssertionError("MultiplayerGame.cpp scans maps/mp for files again")


def validate_consumers() -> None:
    game = read(MP / "MultiplayerGame.cpp")
    for signature, tokens in (
        ("const idDict *MultiplayerResolveMapDecl( const char *mapPath ) {", ("mpMapHost.DeclForPath(",)),
        ("static const idDict *ResolveScoreboardMapDecl(", ("mpMapHost.DeclForPath(",)),
        ("bool idMultiplayerGame::BuildCompetitionSeriesMapPool(",
         ("mpMapHost.Database()", "mapDatabase.RotationEntry( position )", "mapDatabase.Supports( entryIndex, gameLocal.gameType )")),
        ("void idMultiplayerGame::SendMapList( int clientNum ) {", ("mapDatabase.Collect( -1, MP_MAPDB_VOTE,",)),
        ("void idMultiplayerGame::ReadMapList( const idBitMsg &msg ) {", ("mapDatabase.Find( path )",)),
        ("bool idMultiplayerGame::RequestVoteMaps( int flags ) {", ("mpMapHost.CollectDecls( -1, MP_MAPDB_VOTE, voteMapDecls )",)),
        ("void idMultiplayerGame::SetMapList(", ("mapDatabase.FindHostIndex( voteMapDecls[ i ] )", "mapDatabase.Supports( entryIndex, listType->type )")),
        ("void idMultiplayerGame::RetainedVoteChoices( void ) {", ("mapDatabase.Supports( entryIndex, draftedType->type )",)),
        ("idStr idMultiplayerGame::PredictNextMap( void ) const {", ("mpMapHost.ServerPredictNext()", '"si_nextMap"')),
        ("bool idMultiplayerGame::PickMap( idStr gameType, bool checkOnly ) {",
         ("mpMapHost.Supports( mapName, wanted->type )", "mapDatabase.Supports( entryIndex, wanted->type )")),
        ("void idMultiplayerGame::Run( void ) {", ("mpMapHost.ServerThink();",)),
        ("void idMultiplayerGame::DisconnectClient( int clientNum ) {", ("mpMapHost.ServerClientDisconnect( clientNum );",)),
    ):
        text = body(game, signature)
        for token in tokens:
            require(text, token, signature)
    # Both retained pages (Vote and Admin) filter their map rows by the database.
    if game.count("mapDatabase.Supports( entryIndex, draftedType->type )") != 2:
        raise AssertionError("the retained Vote and Admin pages must both filter maps by the database")
    # Both ways to call a map vote ask the database whether the map takes votes.
    if game.count("( votedMap->flags & MP_MAPDB_VOTE )") != 2:
        raise AssertionError("the single and the packed map vote must both check MP_MAPDB_VOTE")
    # The server card lists the rotation the database holds, not the raw cvar text.
    require(game, "mapDatabase.RotationEntry( position );", "PublishRetainedMenu rotation rows")
    if '"PublishRetainedMenu", LEXFL' in game or '"PredictNextMap", LEXFL' in game:
        raise AssertionError("si_mapCycle is parsed outside the map database again")


def validate_rotation_and_settings() -> None:
    local = read(MP / "Game_local.cpp")
    next_map = body(local, "bool idGameLocal::NextMap( void ) {")
    advance = require(next_map, "switch ( mpMapHost.ServerAdvance() ) {", "idGameLocal::NextMap")
    script = require(next_map, 'program.FindFunction( "mapcycle::cycle" )', "idGameLocal::NextMap")
    if not advance < script:
        raise AssertionError("the map database must decide before the legacy cycle script runs")
    for token in ("case MP_MAP_ADVANCE_CHANGED:", "case MP_MAP_ADVANCE_STAY:"):
        require(next_map, token, "idGameLocal::NextMap")
    if "mapCycleList" in next_map or "si_mapCycle.GetString()" in next_map:
        raise AssertionError("idGameLocal::NextMap walks si_mapCycle itself again")

    validate = body(local, "bool idGameLocal::ValidateServerSettings( const char* map, const char* gametype ) {")
    require(validate, "mpMapHost.ServerPrepareMap( map );", "idGameLocal::ValidateServerSettings")
    init = body(local, "void idGameLocal::InitFromNewMap(")
    require(init, "mpMapHost.ServerMapStarted( mapName );", "idGameLocal::InitFromNewMap")
    require(init, "mpMapHost.NonServerMapStarted();", "idGameLocal::InitFromNewMap")
    require(body(local, "void idGameLocal::MapShutdown( void ) {"), "mpMapHost.ServerMapShutdown();", "idGameLocal::MapShutdown")
    require(body(local, "void idGameLocal::Shutdown( void ) {"), "mpMapHost.Shutdown();", "idGameLocal::Shutdown")

    # The engine asks before it reads the server info, so a map's settings are in it.
    server = body(read(ROOT / "src/framework/async/AsyncServer.cpp"), "void idAsyncServer::ExecuteMapChange( void ) {")
    hook = require(server, 'game->ValidateServerSettings( cvarSystem->GetCVarString( "si_map" )', "idAsyncServer::ExecuteMapChange")
    rescan = require(server, 'cmdSystem->BufferCommandText( CMD_EXEC_NOW, "rescanSI" );', "idAsyncServer::ExecuteMapChange")
    if not hook < rescan:
        raise AssertionError("the map's settings must be applied before the server info is rescanned")

    host = read(HOST)
    prepare = body(host, "void mpMapDatabaseHost::ServerPrepareMap( const char *map ) {")
    revert = require(prepare, "RevertSettings();", "ServerPrepareMap")
    apply_ = require(prepare, "ApplySettings( index );", "ServerPrepareMap")
    if not revert < apply_:
        raise AssertionError("the previous map's settings must go back before the next map's are applied")
    # A casual session imports its rules from the settings once and mirrors them
    # back at every map start; a map's own limits would be overwritten there.
    for token in ("const bool hadSettings = appliedSettings.Num() > 0;", "gameLocal.mpGame.InvalidateCasualRulesImport();",
                  "!gameLocal.mpGame.IsManagedMatch()"):
        require(prepare, token, "ServerPrepareMap")
    invalidate = body(read(MP / "MultiplayerGame.cpp"), "void idMultiplayerGame::InvalidateCasualRulesImport( void ) {")
    for token in ("MP_RULE_MANAGED_MATCH", "MP_MATCH_PROFILE_CASUAL", "competitiveRulesInitialized = false;"):
        require(invalidate, token, "idMultiplayerGame::InvalidateCasualRulesImport")
    allowed = body(host, "bool mpMapDatabaseHost::SettingAllowed(")
    for token in ('"si_map"', '"si_gameType"', '"si_mapCycle"', '"si_pure"', '"si_maxPlayers"', '"password"', '"rcon"',
                  "CVAR_SERVERINFO | CVAR_GAME", "CVAR_ROM | CVAR_INIT | CVAR_CHEAT"):
        require(allowed, token, "mpMapDatabaseHost::SettingAllowed")
    reverted = body(host, "void mpMapDatabaseHost::RevertSettings( void ) {")
    # A value somebody changed while the map ran is theirs.
    require(reverted, "applied.applied.Cmp( cvar->GetString() ) == 0", "mpMapDatabaseHost::RevertSettings")

    # A pure server serves only pack files once a map is up; mapdb check and
    # mapdb reload read the operator's file by its real path.
    reader = body(host, "static bool MapDbReadOverrideFile( const char *fileName, idStr &text ) {")
    for token in ('"fs_savepath"', "fileSystem->OpenExplicitFileRead(", "MP_MAPDB_MAX_TEXT_BYTES"):
        require(reader, token, "MapDbReadOverrideFile")
    if host.count("MapDbReadOverrideFile( fileName, text )") != 2:
        raise AssertionError("the override file must be read through MapDbReadOverrideFile at start and by mapdb check")

    advance_body = body(host, "mpMapAdvance_t mpMapDatabaseHost::ServerAdvance( void ) {")
    for token in ("database.ChooseNext(", "MP_MAP_ROTATION_SCRIPT", "MP_MAP_ADVANCE_SCRIPT", "si_map.SetString( entry->token );"):
        require(advance_body, token, "mpMapDatabaseHost::ServerAdvance")
    # Managed matches and the Arena campaign choose their own maps.
    active = body(host, "bool mpMapDatabaseHost::RotationActive( void ) const {")
    for token in ("IsManagedMatch()", "IsArenaCampaignMatch()"):
        require(active, token, "mpMapDatabaseHost::RotationActive")
    # MP-12: the match's end offers the pick (ServerOfferPick) or names the next map (ServerAnnounceNext)
    for signature in ("void mpMapDatabaseHost::ServerQueueSet(", "bool mpMapDatabaseHost::ServerOfferPick( void ) {",
                      "void mpMapDatabaseHost::ServerAnnounceNext( void ) {"):
        require(body(host, signature), "RotationActive()", signature)
    require(body(host, "void mpMapDatabaseHost::ServerMatchEnded( void ) {"), "if ( !ServerOfferPick() ) {",
            "void mpMapDatabaseHost::ServerMatchEnded( void ) {")

    state = read(MP / "mp/GameState.cpp")
    require(state, "mpMapHost.ServerMatchEnded();", "rvGameState::NewState GAMEREVIEW")


def validate_surface() -> None:
    host = read(HOST)
    for name, flags in (("g_mapDatabase", "CVAR_GAME | CVAR_ARCHIVE"), ("g_mapRotation", "CVAR_INTEGER"),
                        ("g_mapQueue", "CVAR_BOOL"), ("g_mapPick", "CVAR_BOOL"),
                        ("si_nextMap", "CVAR_SERVERINFO | CVAR_ROM"), ("si_mapPick", "CVAR_SERVERINFO | CVAR_ROM")):
        declaration = re.search(r'^idCVar ' + name + r'\(.*?\);', host, re.M | re.S)
        if declaration is None or flags not in declaration.group(0):
            raise AssertionError(f"{name} must be declared with {flags}")
    commands = read(MP / "gamesys/SysCmds.cpp")
    for token in ('"mymap",', '"mappick",', '"mapdb",', "mpMapDatabaseHost::MyMap_f", "mpMapDatabaseHost::MapPick_f",
                  "mpMapDatabaseHost::MapDb_f"):
        require(commands, token, "SysCmds.cpp")

    header = read(MP / "Game_local.h")
    restart = require(header, "GAME_RELIABLE_MESSAGE_ROUNDRESTART,", "Game_local.h")
    request = require(header, "GAME_RELIABLE_MESSAGE_MAPDB,", "Game_local.h")
    count = require(header, "GAME_RELIABLE_MESSAGE_COUNT,", "Game_local.h")
    if not restart < request < count:
        raise AssertionError("GAME_RELIABLE_MESSAGE_MAPDB must be appended: the ordinals are the wire format")
    require(header, "RELIABLE_FLOOD_MAPDB,", "Game_local.h")
    network = read(MP / "Game_network.cpp")
    server = body(network, "void idGameLocal::ServerProcessReliableMessage( int clientNum, const idBitMsg &msg ) {")
    case = require(server, "case GAME_RELIABLE_MESSAGE_MAPDB: {", "ServerProcessReliableMessage")
    require(server[case:case + 400], "CheckClientReliableFlood( clientNum, RELIABLE_FLOOD_MAPDB, RELIABLE_FLOOD_MAPDB_DELAY )",
            "the map request flood gate")
    if network.count("case GAME_RELIABLE_MESSAGE_MAPDB: {") != 2:
        raise AssertionError("the server and the repeater must each handle GAME_RELIABLE_MESSAGE_MAPDB")
    receive = body(host, "void mpMapDatabaseHost::ServerReceiveRequest( int clientNum, const idBitMsg &msg ) {")
    for token in ("msg.GetRemainingReadBits() != 0", "msg.GetRemainingReadBits() != 8", "remaining > static_cast<int>( sizeof( path ) )"):
        require(receive, token, "mpMapDatabaseHost::ServerReceiveRequest")


def validate_strings() -> None:
    host = read(HOST)
    for number in STRING_IDS:
        require(host, f'"#str_{number}"', "MapDatabaseHost.cpp")
    english = read(ROOT / "content/baseoq4/pak0/strings/english_openq4.lang")
    for language in LANGUAGES:
        table = read(ROOT / f"content/baseoq4/pak0/strings/{language}_openq4.lang")
        for number in STRING_IDS:
            entry = re.search(rf'"#str_{number}"\t"([^"]*)"', table)
            reference = re.search(rf'"#str_{number}"\t"([^"]*)"', english)
            if entry is None or reference is None or not entry.group(1).strip():
                raise AssertionError(f"{language}_openq4.lang: #str_{number} is missing")
            # A map name or a list follows these texts, so the separator is part of them.
            if entry.group(1).endswith(": ") != reference.group(1).endswith(": "):
                raise AssertionError(f"{language}_openq4.lang: #str_{number} must end as the English text does")
            if "%" in entry.group(1):
                raise AssertionError(f"{language}_openq4.lang: #str_{number} takes no format arguments")


def validate_documents() -> None:
    guide = read(ROOT / "docs/user/server-setup.md")
    for token in ("mapdb.txt", "g_mapRotation", "g_mapQueue", "g_mapPick", "mymap", "mappick", "mapdb check", "players 2 8"):
        require(guide, token, "docs/user/server-setup.md")
    roadmap = read(ROOT / "docs/dev/roadmap.md")
    require(roadmap, "mp_map_database_contract.py", "docs/dev/roadmap.md MP-13")
    validator = read(ROOT / "tools/validation/openq4_validate.py")
    if validator.count('"mp_map_database_contract.py"') != 1:
        raise AssertionError("openq4_validate.py must run mp_map_database_contract.py exactly once")


HARNESS = r'''
#include "mpgame/mp/MapDatabase.h"
#include <string.h>
#include <stdio.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

enum { DM = 1, TOURNEY = 2, TDM = 3, CTF = 4, DUEL = 9 };
static const unsigned FFA = (1u << DM) | (1u << TOURNEY) | (1u << DUEL);
static const unsigned DM_MODES = (1u << DM) | (1u << TDM) | (1u << DUEL);
static const unsigned CTF_MODES = (1u << CTF) | (1u << TDM);

static int ResolveMode(const char *name) {
	if (strcmp(name, "DM") == 0) return DM;
	if (strcmp(name, "Team DM") == 0) return TDM;
	if (strcmp(name, "CTF") == 0) return CTF;
	if (strcmp(name, "Duel") == 0) return DUEL;
	return -1;
}

static int Add(mpMapDatabase &db, const char *path, unsigned modes) {
	const int index = db.AddMap(path, "", modes, db.Num() + 100);
	if (index >= 0) {
		int low = 0, high = 0;
		mpMapDatabase::DefaultBounds(db.Entry(index)->token, modes, FFA, low, high);
		db.SetBounds(index, low, high);
	}
	return index;
}

static bool Current(int owner, uint64_t id) { return owner != 3 && id != 99; }

static bool Apply(mpMapDatabase &db, const char *text, mpMapOverrideReport &report) {
	return db.ApplyOverrides(text, static_cast<int>(strlen(text)), ResolveMode, report);
}

int main() {
	// --- tokens -----------------------------------------------------------
	char token[MP_MAPDB_TOKEN_BYTES];
	CHECK(mpMapDatabase::NormalizeToken("maps/mp/q4dm1.map", token) && strcmp(token, "mp/q4dm1") == 0);
	CHECK(mpMapDatabase::NormalizeToken("mp\\Q4DM1", token) && strcmp(token, "mp/Q4DM1") == 0);
	CHECK(mpMapDatabase::NormalizeToken("MAPS/mp/q4dm1", token) && strcmp(token, "mp/q4dm1") == 0);
	CHECK(!mpMapDatabase::NormalizeToken("../escape", token));
	CHECK(!mpMapDatabase::NormalizeToken("mp//q4dm1", token));
	CHECK(!mpMapDatabase::NormalizeToken("mp/q4 dm1", token));
	CHECK(!mpMapDatabase::NormalizeToken("mp/q4dm1;quit", token));
	CHECK(!mpMapDatabase::NormalizeToken("", token));
	CHECK(!mpMapDatabase::NormalizeToken(NULL, token));
	char longPath[MP_MAPDB_TOKEN_BYTES + 8];
	memset(longPath, 'a', sizeof(longPath));
	longPath[MP_MAPDB_TOKEN_BYTES - 1] = '\0';
	CHECK(mpMapDatabase::NormalizeToken(longPath, token));
	longPath[MP_MAPDB_TOKEN_BYTES - 1] = 'a';
	longPath[MP_MAPDB_TOKEN_BYTES] = '\0';
	CHECK(!mpMapDatabase::NormalizeToken(longPath, token));
	CHECK(mpMapDatabase::IsStockToken("mp/q4dm1") && mpMapDatabase::IsStockToken("MP/Q4CTF8"));
	CHECK(!mpMapDatabase::IsStockToken("mp/mymap"));

	// --- default bounds ---------------------------------------------------
	int low = -1, high = -1;
	mpMapDatabase::DefaultBounds("mp/q4tourney1", DM_MODES, FFA, low, high);
	CHECK(low == 2 && high == 4);
	mpMapDatabase::DefaultBounds("mp/q4dm11v1", DM_MODES, FFA, low, high);
	CHECK(low == 2 && high == 4);
	mpMapDatabase::DefaultBounds("mp/q4ctf1", 1u << CTF, FFA, low, high);
	CHECK(low == 4 && high == 0);
	mpMapDatabase::DefaultBounds("mp/q4dm1", DM_MODES, FFA, low, high);
	CHECK(low == 0 && high == 0);
	mpMapDatabase::DefaultBounds("mp/v1", DM_MODES, FFA, low, high);
	CHECK(low == 0 && high == 0);

	// --- discovery --------------------------------------------------------
	mpMapDatabase db;
	CHECK(db.ValidateInvariants() && db.Num() == 0);
	const int dm1 = Add(db, "mp/q4dm1", DM_MODES);
	const int dm2 = Add(db, "mp/q4dm2", DM_MODES);
	const int ctf1 = Add(db, "mp/q4ctf1", CTF_MODES);
	const int duel = Add(db, "mp/q4tourney1", DM_MODES);
	const int dm3 = Add(db, "mp/q4dm3", DM_MODES);
	CHECK(dm1 == 0 && dm2 == 1 && ctf1 == 2 && duel == 3 && dm3 == 4);
	CHECK(Add(db, "maps/MP/Q4DM1.map", DM_MODES) == -1);	// the same map
	CHECK(Add(db, "mp/bad name", DM_MODES) == -1);
	CHECK(db.Num() == 5 && db.ValidateInvariants());
	CHECK(db.Find("MP/q4dm2.map") == dm2 && db.Find("mp/none") == -1);
	CHECK(db.FindHostIndex(102) == ctf1 && db.FindHostIndex(-1) == -1 && db.FindHostIndex(7) == -1);
	CHECK((db.Entry(dm1)->flags & MP_MAPDB_STOCK) != 0);
	CHECK(db.Entry(dm1)->flags & MP_MAPDB_ROTATION);
	CHECK(db.Supports(ctf1, CTF) && !db.Supports(dm1, CTF) && !db.Supports(-1, DM) && !db.Supports(dm1, 40));
	CHECK(db.FitsPlayers(duel, 2) && db.FitsPlayers(duel, 4) && !db.FitsPlayers(duel, 5) && !db.FitsPlayers(duel, 1));
	CHECK(!db.FitsPlayers(ctf1, 3) && db.FitsPlayers(ctf1, 16) && db.FitsPlayers(dm1, 0));
	CHECK(!db.SetBounds(dm1, 8, 4) && !db.SetBounds(dm1, -1, 4) && !db.SetBounds(dm1, 0, 65) && !db.SetBounds(9, 0, 0));
	int found[8];
	CHECK(db.Collect(DM, MP_MAPDB_VOTE, found, 8) == 4 && found[0] == dm1 && found[3] == dm3);
	CHECK(db.Collect(CTF, 0, found, 8) == 1 && found[0] == ctf1);
	CHECK(db.Collect(-1, 0, found, 8) == 5);
	CHECK(db.Collect(-1, 0, found, 2) == 2);
	CHECK(db.Collect(-1, 0, NULL, 0) == 5);

	// --- rotation in order ------------------------------------------------
	// six players: the duel arena is skipped, the walk wraps
	mpMapSelection next = db.NextInRotation(dm1, DM, 6);
	CHECK(next.index == dm2 && next.source == MP_MAPDB_SOURCE_ROTATION);
	next = db.NextInRotation(dm2, DM, 6);
	CHECK(next.index == dm3);			// past the CTF map and the duel arena
	next = db.NextInRotation(dm3, DM, 6);
	CHECK(next.index == dm1);
	// three players: the duel arena takes its turn
	next = db.NextInRotation(dm2, DM, 3);
	CHECK(next.index == duel);
	// CTF with two players: the only CTF map needs four, and is the only choice
	next = db.NextInRotation(ctf1, CTF, 2);
	CHECK(next.index == ctf1 && next.source == MP_MAPDB_SOURCE_ROTATION_UNBOUNDED);
	next = db.NextInRotation(dm1, CTF, 8);
	CHECK(next.index == ctf1 && next.source == MP_MAPDB_SOURCE_ROTATION);
	// a gametype no map plays
	next = db.NextInRotation(dm1, 12, 4);
	CHECK(next.index == -1 && next.source == MP_MAPDB_SOURCE_NONE);
	// a map the database does not hold: start at the top
	next = db.NextInRotation(-1, DM, 6);
	CHECK(next.index == dm1);
	// out of rotation by flag
	CHECK(db.SetFlags(dm2, MP_MAPDB_VOTE | MP_MAPDB_QUEUE));
	next = db.NextInRotation(dm1, DM, 6);
	CHECK(next.index == dm3 && db.RotationNum() == 4);
	CHECK(db.SetFlags(dm2, MP_MAPDB_DEFAULT_FLAGS | MP_MAPDB_STOCK));
	// bounds that exclude every other map: the walk still turns
	CHECK(db.SetBounds(dm2, 10, 12) && db.SetBounds(dm3, 10, 12));
	next = db.NextInRotation(dm1, DM, 6);
	CHECK(next.index == dm1 && next.source == MP_MAPDB_SOURCE_ROTATION);		// only the current map fits
	CHECK(db.SetBounds(dm1, 10, 12));
	next = db.NextInRotation(dm1, DM, 6);
	CHECK(next.index == dm2 && next.source == MP_MAPDB_SOURCE_ROTATION_UNBOUNDED);
	CHECK(db.SetBounds(dm1, 0, 0) && db.SetBounds(dm2, 0, 0) && db.SetBounds(dm3, 0, 0));

	// --- an explicit list -------------------------------------------------
	int unknown = -1;
	CHECK(db.SetRotationFromList("mp/q4dm3;mp/q4dm1 ; mp/nowhere;mp/q4dm3;\"mp/q4ctf1\"", &unknown) == 3);
	CHECK(unknown == 2);					// the missing map and the repeat
	CHECK(db.HasExplicitRotation() && db.RotationNum() == 3 && db.RotationEntry(0) == dm3 && db.RotationEntry(2) == ctf1);
	CHECK(db.RotationEntry(3) == -1 && db.ValidateInvariants());
	next = db.NextInRotation(dm3, DM, 6);
	CHECK(next.index == dm1);
	next = db.NextInRotation(dm1, DM, 6);
	CHECK(next.index == dm3);				// past the CTF map, wrapping
	next = db.NextInRotation(dm2, DM, 6);
	CHECK(next.index == dm3);				// the current map is outside the list
	// a listed map plays even when its flag says no rotation
	CHECK(db.SetFlags(dm1, MP_MAPDB_VOTE));
	next = db.NextInRotation(dm3, DM, 6);
	CHECK(next.index == dm1);
	CHECK(db.SetFlags(dm1, MP_MAPDB_DEFAULT_FLAGS | MP_MAPDB_STOCK));
	CHECK(db.SetRotationFromList("mp/nowhere") == 0 && db.HasExplicitRotation() && db.RotationNum() == 0);
	next = db.NextInRotation(dm1, DM, 6);
	CHECK(next.index == -1);
	CHECK(db.SetRotationFromList("") == 0 && !db.HasExplicitRotation() && db.RotationNum() == 5);
	db.ClearRotation();

	// --- shuffle ----------------------------------------------------------
	for (unsigned seed = 1; seed < 200; ++seed) {
		const mpMapSelection draw = db.RandomFromRotation(dm1, DM, 6, seed);
		CHECK(draw.source == MP_MAPDB_SOURCE_RANDOM);
		CHECK(draw.index == dm2 || draw.index == dm3);		// never the current map, never the duel arena
		CHECK(db.RandomFromRotation(dm1, DM, 6, seed).index == draw.index);
	}
	bool sawBoth[2] = { false, false };
	for (unsigned seed = 1; seed < 64; ++seed) {
		sawBoth[db.RandomFromRotation(dm1, DM, 6, seed).index == dm2 ? 0 : 1] = true;
	}
	CHECK(sawBoth[0] && sawBoth[1]);
	db.NotePlayed(dm2);
	CHECK(db.RecentlyPlayed(dm2) && db.HistoryNum() == 1);
	for (unsigned seed = 1; seed < 64; ++seed) {
		CHECK(db.RandomFromRotation(dm1, DM, 6, seed).index == dm3);	// the fresh map
	}
	db.NotePlayed(dm3);
	db.NotePlayed(dm2);
	CHECK(db.HistoryNum() == 2 && db.HistoryEntry(0) == dm2 && db.HistoryEntry(1) == dm3);
	// half of five maps is two: both recent maps are stale, so they come back
	CHECK(db.RandomFromRotation(dm1, DM, 6, 5).index >= 0);
	CHECK(db.RandomFromRotation(ctf1, CTF, 8, 5).index == ctf1);
	CHECK(db.RandomFromRotation(dm1, 12, 8, 5).index == -1);
	CHECK(db.ValidateInvariants());

	// --- the player queue -------------------------------------------------
	CHECK(db.QueueAdd(0, 10, dm2, DM, dm1) == MP_MAPDB_QUEUE_ADDED);
	CHECK(db.QueueAdd(1, 11, dm2, DM, dm1) == MP_MAPDB_QUEUE_DUPLICATE);
	CHECK(db.QueueAdd(1, 11, dm1, DM, dm1) == MP_MAPDB_QUEUE_CURRENT_MAP);
	CHECK(db.QueueAdd(1, 11, ctf1, DM, dm1) == MP_MAPDB_QUEUE_WRONG_MODE);
	CHECK(db.QueueAdd(1, 11, 77, DM, dm1) == MP_MAPDB_QUEUE_UNKNOWN_MAP);
	CHECK(db.QueueAdd(-1, 11, dm3, DM, dm1) == MP_MAPDB_QUEUE_BAD_OWNER);
	CHECK(db.QueueAdd(MP_MAPDB_MAX_VOTERS, 11, dm3, DM, dm1) == MP_MAPDB_QUEUE_BAD_OWNER);
	CHECK(db.SetFlags(duel, MP_MAPDB_ROTATION | MP_MAPDB_VOTE));
	CHECK(db.QueueAdd(1, 11, duel, DM, dm1) == MP_MAPDB_QUEUE_NOT_QUEUEABLE);
	CHECK(db.SetFlags(duel, MP_MAPDB_DEFAULT_FLAGS | MP_MAPDB_STOCK));
	CHECK(db.QueueAdd(1, 11, dm3, DM, dm1) == MP_MAPDB_QUEUE_ADDED);
	CHECK(db.QueueNum() == 2 && db.QueueEntry(0)->owner == 0 && db.QueueEntry(1)->mapIndex == dm3);
	// one entry a player: a second request replaces the first and keeps its place
	CHECK(db.QueueAdd(0, 10, duel, DM, dm1) == MP_MAPDB_QUEUE_REPLACED);
	CHECK(db.QueueNum() == 2 && db.QueueEntry(0)->owner == 0 && db.QueueEntry(0)->mapIndex == duel);
	CHECK(db.QueueAdd(0, 10, duel, DM, dm1) == MP_MAPDB_QUEUE_REPLACED && db.QueueNum() == 2);
	CHECK(db.QueueFindOwner(1) == 1 && db.QueueFindOwner(5) == -1 && db.QueueEntry(2) == NULL);
	CHECK(db.ValidateInvariants());
	// a queued map is played before the rotation, whatever the bounds say
	mpMapSelection chosen = db.ChooseNext(dm1, DM, 12, false, 1, false);
	CHECK(chosen.index == duel && chosen.source == MP_MAPDB_SOURCE_QUEUE && chosen.owner == 0);
	CHECK(db.QueueNum() == 2);				// a prediction takes nothing
	chosen = db.ChooseNext(dm1, DM, 12, false, 1, true);
	CHECK(chosen.index == duel && db.QueueNum() == 1 && db.QueueEntry(0)->owner == 1);
	// the gametype changed since: the queued map is passed over and dropped
	chosen = db.ChooseNext(duel, CTF, 12, false, 1, false);
	CHECK(chosen.index == ctf1 && chosen.source == MP_MAPDB_SOURCE_ROTATION && db.QueueNum() == 1);
	chosen = db.ChooseNext(duel, CTF, 12, false, 1, true);
	CHECK(chosen.index == ctf1 && db.QueueNum() == 0);
	// leaving takes the entry along
	CHECK(db.QueueAdd(2, 20, dm2, DM, dm1) == MP_MAPDB_QUEUE_ADDED);
	CHECK(db.QueueAdd(3, 30, dm3, DM, dm1) == MP_MAPDB_QUEUE_ADDED);
	CHECK(db.QueueAdd(4, 99, duel, DM, dm1) == MP_MAPDB_QUEUE_ADDED);
	CHECK(db.QueueRemoveOwner(2) && !db.QueueRemoveOwner(2) && db.QueueNum() == 2);
	CHECK(db.QueuePrune(Current) == 2 && db.QueueNum() == 0 && db.QueuePrune(NULL) == 0);
	for (int owner = 0; owner < MP_MAPDB_MAX_QUEUE; ++owner) {
		// only four DM maps exist besides the current one, so the queue fills by map first
		const mpMapQueueResult_t result = db.QueueAdd(owner, 1, owner == 0 ? dm2 : (owner == 1 ? dm3 : duel), DM, dm1);
		CHECK(result == (owner < 3 ? MP_MAPDB_QUEUE_ADDED : MP_MAPDB_QUEUE_DUPLICATE));
	}
	CHECK(db.QueueNum() == 3 && db.ValidateInvariants());
	db.QueueClear();
	CHECK(db.QueueNum() == 0);

	// --- the end-of-match pick --------------------------------------------
	CHECK(!db.Pick().open && db.PickLeader(1) == -1 && !db.PickVote(0, 0));
	CHECK(db.PickOpen(dm1, DM, 3, 7));
	CHECK(db.Pick().candidateCount == 3 && db.ValidateInvariants());
	// the first candidate is the rotation's own next map
	CHECK(db.Pick().candidates[0] == db.NextInRotation(dm1, DM, 3).index);
	for (int candidate = 0; candidate < 3; ++candidate) {
		CHECK(db.Pick().candidates[candidate] != dm1 && db.Supports(db.Pick().candidates[candidate], DM));
	}
	// no vote at all: the rotation's next map
	CHECK(db.PickLeader(1) == db.Pick().candidates[0]);
	CHECK(db.PickVote(0, 2) && db.PickVote(1, 1) && db.PickVote(2, 2));
	CHECK(!db.PickVote(0, 3) && !db.PickVote(-1, 0) && !db.PickVote(MP_MAPDB_MAX_VOTERS, 0));
	int votes = 0;
	CHECK(db.PickLeader(1, &votes) == db.Pick().candidates[2] && votes == 2);
	// a change of mind moves the vote
	CHECK(db.PickVote(0, 1) && db.Pick().votes[1] == 2 && db.Pick().votes[2] == 1);
	CHECK(db.PickLeader(1, &votes) == db.Pick().candidates[1] && votes == 2);
	// a player who leaves takes the vote along; the tie goes to a seeded draw between the leaders
	db.PickRemoveVoter(0);
	db.PickRemoveVoter(0);
	CHECK(db.Pick().votes[1] == 1 && db.Pick().votes[2] == 1 && db.ValidateInvariants());
	bool tie[3] = { false, false, false };
	for (unsigned seed = 1; seed < 64; ++seed) {
		const int leader = db.PickLeader(seed, &votes);
		CHECK(votes == 1 && leader == db.PickLeader(seed));
		for (int candidate = 0; candidate < 3; ++candidate) {
			tie[candidate] |= leader == db.Pick().candidates[candidate];
		}
	}
	CHECK(!tie[0] && tie[1] && tie[2]);
	// the pick decides once the queue is empty
	CHECK(db.PickVote(5, 2));
	chosen = db.ChooseNext(dm1, DM, 3, false, 1, false);
	CHECK(chosen.index == db.Pick().candidates[2] && chosen.source == MP_MAPDB_SOURCE_PICK && chosen.votes == 2);
	CHECK(db.Pick().open);
	const int winner = chosen.index;
	chosen = db.ChooseNext(dm1, DM, 3, false, 1, true);
	CHECK(chosen.index == winner && !db.Pick().open && db.ValidateInvariants());
	// a queued map wins over an open pick and closes it
	CHECK(db.PickOpen(dm1, DM, 3, 9));
	CHECK(db.QueueAdd(6, 1, dm3, DM, dm1) == MP_MAPDB_QUEUE_ADDED);
	chosen = db.ChooseNext(dm1, DM, 3, false, 1, true);
	CHECK(chosen.index == dm3 && chosen.source == MP_MAPDB_SOURCE_QUEUE && !db.Pick().open);
	// one candidate is no choice
	CHECK(!db.PickOpen(dm1, CTF, 8, 3) && !db.Pick().open);
	CHECK(!db.PickOpen(ctf1, CTF, 8, 3));
	// maps the server keeps out of votes are not offered beside the rotation's own next
	CHECK(db.SetFlags(dm3, MP_MAPDB_ROTATION | MP_MAPDB_QUEUE));
	CHECK(db.SetFlags(duel, MP_MAPDB_ROTATION | MP_MAPDB_QUEUE));
	CHECK(!db.PickOpen(dm1, DM, 3, 3));
	CHECK(db.SetFlags(dm3, MP_MAPDB_DEFAULT_FLAGS) && db.SetFlags(duel, MP_MAPDB_DEFAULT_FLAGS));
	// six players are too many for the duel arena: it is not offered, and two maps are a pick
	CHECK(db.PickOpen(dm1, DM, 6, 21) && db.Pick().candidateCount == 2);
	CHECK(db.Pick().candidates[0] == dm2 && db.Pick().candidates[1] == dm3);
	db.PickClose();
	// when no map fits, the rotation ignores the bounds and so does the offer
	CHECK(db.SetBounds(dm1, 10, 12) && db.SetBounds(dm2, 10, 12) && db.SetBounds(dm3, 10, 12));
	CHECK(db.PickOpen(dm1, DM, 6, 21) && db.Pick().candidateCount == 3 && db.Pick().candidates[0] == dm2);
	db.PickClose();
	CHECK(db.SetBounds(dm1, 0, 0) && db.SetBounds(dm2, 0, 0) && db.SetBounds(dm3, 0, 0));
	// a shuffled rotation without queue or pick
	chosen = db.ChooseNext(dm1, DM, 6, true, 11, true);
	CHECK(chosen.source == MP_MAPDB_SOURCE_RANDOM && chosen.index != dm1 && chosen.index != duel);

	// --- the override file ------------------------------------------------
	mpMapOverrideReport report;
	const unsigned before = db.Revision();
	CHECK(Apply(db,
		"// a server's database\n"
		"defaults { players 2 12 vote 1 }\n"
		"map mp/q4dm1 {\n"
		"\tplayers 2 8\n"
		"\tmodes \"DM, Duel\"\n"
		"\tname \"The Yard\"\n"
		"\tset si_fragLimit 30\n"
		"\tset g_gravity \"900\"\n"
		"\tset si_fragLimit 25\n"
		"}\n"
		"/* out of the queue,\n   out of votes */\n"
		"map maps/mp/q4tourney1.map { queue 0 vote 0 rotation 0 players 0 0 }\n"
		"map mp/notinstalled { players 2 4 set si_timeLimit 5 }\n"
		"rotation { mp/q4dm2 mp/q4dm1 mp/notinstalled mp/q4dm2 }\n", report));
	CHECK(report.line == 0 && report.mapsChanged == 2 && report.mapsUnknown == 1);
	CHECK(report.rotationListed == 2 && report.rotationUnknown == 1 && report.settingsDropped == 0);
	CHECK(db.Revision() != before && db.ValidateInvariants());
	CHECK(db.Entry(dm1)->minPlayers == 2 && db.Entry(dm1)->maxPlayers == 8);
	CHECK(db.Entry(dm2)->minPlayers == 2 && db.Entry(dm2)->maxPlayers == 12);		// the defaults
	CHECK(db.Entry(duel)->minPlayers == 0 && db.Entry(duel)->maxPlayers == 0);
	CHECK(db.Supports(dm1, DM) && db.Supports(dm1, DUEL) && !db.Supports(dm1, TDM));
	CHECK(strcmp(db.Entry(dm1)->name, "The Yard") == 0);
	CHECK((db.Entry(dm1)->flags & MP_MAPDB_OVERRIDDEN) && !(db.Entry(dm2)->flags & MP_MAPDB_OVERRIDDEN));
	CHECK((db.Entry(duel)->flags & (MP_MAPDB_QUEUE | MP_MAPDB_VOTE | MP_MAPDB_ROTATION)) == 0);
	CHECK(db.Entry(dm1)->settingCount == 2);
	CHECK(strcmp(db.Setting(dm1, 0)->key, "si_fragLimit") == 0 && strcmp(db.Setting(dm1, 0)->value, "25") == 0);
	CHECK(strcmp(db.Setting(dm1, 1)->key, "g_gravity") == 0 && strcmp(db.Setting(dm1, 1)->value, "900") == 0);
	CHECK(db.Setting(dm1, 2) == NULL && db.Setting(dm2, 0) == NULL);
	CHECK(db.HasExplicitRotation() && db.RotationNum() == 2 && db.RotationEntry(0) == dm2 && db.RotationEntry(1) == dm1);
	CHECK(db.QueueAdd(7, 1, duel, DM, dm1) == MP_MAPDB_QUEUE_NOT_QUEUEABLE);

	// an error changes nothing and names its line
	const unsigned stable = db.Revision();
	static const struct { const char *text; int line; } bad[] = {
		{ "map mp/q4dm1 {\n\tplayers 9 4\n}\n", 2 },
		{ "map mp/q4dm1 {\n\tplayers 2\n}\n", 2 },
		{ "map mp/q4dm1 {\n\tplayers two 8\n}\n", 2 },
		{ "map mp/q4dm1 {\n\n\tmodes \"DM, Overload\"\n}\n", 3 },
		{ "map mp/q4dm1 {\n\trotation 2\n}\n", 2 },
		{ "map mp/q4dm1 {\n\tgravity 800\n}\n", 2 },
		{ "map mp/q4dm1 {\n\tname \"\"\n}\n", 2 },
		{ "map mp/q4dm1 {\n\tset si_fragLimit\n}\n", 2 },
		{ "map {\n}\n", 1 },
		{ "map mp/q4dm1\n\tplayers 2 8\n", 2 },
		{ "map \"mp/bad name\" { }\n", 1 },
		{ "map mp/q4dm1 { players 2 8\n", 2 },
		{ "defaults { modes \"DM\" }\n", 1 },
		{ "defaults { set si_fragLimit 3 }\n", 1 },
		{ "rotation { mp/q4dm1 { } }\n", 1 },
		{ "rotation { mp/q4dm1\n", 2 },
		{ "\n\nseta si_fragLimit 30\n", 3 },
		{ "map mp/q4dm1 { name \"unterminated\n}\n", 1 },
		{ "\n/* never closed\nmap mp/q4dm1 { }\n", 2 },
	};
	for (size_t index = 0; index < sizeof(bad) / sizeof(bad[0]); ++index) {
		if (Apply(db, bad[index].text, report) || report.line != bad[index].line || report.error[0] == '\0') {
			fprintf(stderr, "bad override %d: accepted or line %d (want %d): %s\n", static_cast<int>(index), report.line,
				bad[index].line, report.error);
			return 1;
		}
		CHECK(db.Revision() == stable && db.Entry(dm1)->maxPlayers == 8 && db.RotationNum() == 2);
		CHECK(report.mapsChanged == 0 && report.rotationListed == 0);
	}
	CHECK(!db.ApplyOverrides(NULL, 0, ResolveMode, report) && !db.ApplyOverrides("x", -1, ResolveMode, report));
	CHECK(!db.ApplyOverrides("x", MP_MAPDB_MAX_TEXT_BYTES + 1, ResolveMode, report));
	// text need not be terminated: the length bounds it
	CHECK(db.ApplyOverrides("defaults { queue 1 } trailing garbage", 20, ResolveMode, report));
	CHECK(db.ApplyOverrides("", 0, ResolveMode, report) && report.mapsChanged == 0);

	// a map holds sixteen settings; the seventeenth is dropped and counted
	char many[2048];
	int written = snprintf(many, sizeof(many), "map mp/q4dm2 {\n");
	for (int setting = 0; setting < MP_MAPDB_MAX_MAP_SETTINGS + 1; ++setting) {
		written += snprintf(many + written, sizeof(many) - written, "set g_setting%d %d\n", setting, setting);
	}
	snprintf(many + written, sizeof(many) - written, "}\n");
	CHECK(Apply(db, many, report) && report.settingsDropped == 1);
	CHECK(db.Entry(dm2)->settingCount == MP_MAPDB_MAX_MAP_SETTINGS && db.ValidateInvariants());
	db.ClearSettings(dm2);
	CHECK(db.Entry(dm2)->settingCount == 0 && db.Entry(dm1)->settingCount == 2 && db.ValidateInvariants());
	CHECK(!db.AddSetting(dm2, "", "1") && !db.AddSetting(-1, "a", "1") && !db.AddSetting(dm2, "a", NULL));

	// --- a full database --------------------------------------------------
	mpMapDatabase full;
	char name[32];
	for (int index = 0; index < MP_MAPDB_MAX_MAPS; ++index) {
		snprintf(name, sizeof(name), "mp/custom%d", index);
		CHECK(full.AddMap(name, name, 1u << DM, index) == index);
	}
	CHECK(full.AddMap("mp/onemore", "", 1u << DM, 999) == -1 && full.ValidateInvariants());
	CHECK(full.NextInRotation(MP_MAPDB_MAX_MAPS - 1, DM, 4).index == 0);
	CHECK(full.PickOpen(0, DM, 4, 123) && full.Pick().candidates[0] == 1 && full.ValidateInvariants());
	for (int index = 0; index < 20; ++index) {
		full.NotePlayed(index);
	}
	CHECK(full.HistoryNum() == MP_MAPDB_HISTORY && full.HistoryEntry(0) == 19 && full.RecentlyPlayed(12) && !full.RecentlyPlayed(11));
	full.NotePlayed(15);
	CHECK(full.HistoryNum() == MP_MAPDB_HISTORY && full.HistoryEntry(0) == 15 && full.HistoryEntry(1) == 19);
	for (unsigned seed = 1; seed < 400; ++seed) {
		const int draw = full.RandomFromRotation(0, DM, 4, seed).index;
		CHECK(draw > 0 && !full.RecentlyPlayed(draw));
	}
	full.Clear();
	CHECK(full.Num() == 0 && full.QueueNum() == 0 && !full.Pick().open && full.HistoryNum() == 0 && full.ValidateInvariants());
	return 0;
}
'''


def executable_contract() -> None:
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        print("mp_map_database_contract: executable checks skipped (no C++ compiler)")
        return
    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="map-database-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "map_database_contract.cpp"
        executable = temp_dir / ("map_database_contract.exe" if compiler.lower().endswith(".exe") else "map_database_contract")
        harness.write_text(HARNESS, encoding="utf-8")
        compiled = subprocess.run(
            [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DMP_MAP_DATABASE_STANDALONE_TEST",
             f"-I{ROOT / 'src'}", str(harness), str(SOURCE), "-o", str(executable)],
            cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode != 0:
            raise AssertionError("the standalone map database contract did not compile:\n" + compiled.stdout + compiled.stderr)
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError("a map database invariant failed:\n" + ran.stdout + ran.stderr)


def main() -> int:
    validate_core()
    validate_single_source()
    validate_consumers()
    validate_rotation_and_settings()
    validate_surface()
    validate_strings()
    validate_documents()
    executable_contract()
    print("mp_map_database_contract: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
