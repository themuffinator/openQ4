//----------------------------------------------------------------
// LiveEventStream.cpp
//
// openQ4: the live match event stream. Collects the state LiveEvents.h
// compares and appends the resulting lines to a file overlay tools follow.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "LiveEventStream.h"

idCVar g_matchEventStream( "g_matchEventStream", "0", CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT,
	"write typed multiplayer match events, one JSON object per line, to match-events/live.ndjson under the save path "
	"for overlay tools; a server writes the match it runs, a client the match it is shown" );

// openQ4 MP-18: the same events as a server's durable record. Off by default: a
// busy server writes a line for every kill, score and roster change.
idCVar g_matchLog( "g_matchLog", "0", CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT,
	"on a server, append typed multiplayer match events, one JSON object per line, to logs/match.ndjson under the "
	"save path; the file is never truncated, and each map begins a stream whose first line gives the UTC time" );

static const char *const MP_LIVE_EVENT_FILE = "match-events/live.ndjson";
static const char *const MP_MATCH_LOG_FILE = "logs/match.ndjson";

static_assert( MP_LIVE_EVENT_CLIENTS == MAX_CLIENTS && MP_LIVE_EVENT_FLAGS == MAX_CTF_FLAGS &&
	MP_LIVE_EVENT_TEAMS == TEAM_MAX && TEAM_MARINE == 0 && TEAM_STROGG == 1, "live event state sizes" );
static_assert( MP_LIVE_EVENT_PHASES == STATE_COUNT && INACTIVE == 0 && WARMUP == 1 && COUNTDOWN == 2 && GAMEON == 3 &&
	SUDDENDEATH == 4 && GAMEREVIEW == MP_LIVE_PHASE_REVIEW && NEXTGAME == 6, "live event phase names" );
static_assert( MP_LIVE_FLAG_AT_BASE == FS_AT_BASE && MP_LIVE_FLAG_DROPPED == FS_DROPPED && MP_LIVE_FLAG_TAKEN == FS_TAKEN &&
	MP_LIVE_FLAG_TAKEN_STROGG == FS_TAKEN_STROGG && MP_LIVE_FLAG_TAKEN_MARINE == FS_TAKEN_MARINE, "live event flag states" );
static_assert( POWERUP_QUADDAMAGE == 0 && POWERUP_HASTE == 1 && POWERUP_REGENERATION == 2 && POWERUP_INVISIBILITY == 3 &&
	POWERUP_CTF_MARINEFLAG == 4 && POWERUP_CTF_STROGGFLAG == 5 && POWERUP_CTF_ONEFLAG == 6 && POWERUP_AMMOREGEN == 7 &&
	POWERUP_GUARD == 8 && POWERUP_DOUBLER == 9 && POWERUP_SCOUT == 10 && POWERUP_MODERATOR == 11 &&
	POWERUP_DEADZONE == 12 && POWERUP_TEAM_AMMO_REGEN == 13 && POWERUP_TEAM_HEALTH_REGEN == 14 &&
	POWERUP_TEAM_DAMAGE_MOD == 15 && POWERUP_ADRENALINE == 16 && POWERUP_FC_ARMOR_REGEN == 17 &&
	MP_LIVE_EVENT_POWERUPS == 18, "live event powerup names" );

// Item powerups only: a flag is reported as a flag event, and the team and
// DeadZone powerups are match state, not something a player picked up.
static const std::uint32_t MP_LIVE_EVENT_TRACKED_POWERUPS =
	( 1u << POWERUP_QUADDAMAGE ) | ( 1u << POWERUP_HASTE ) | ( 1u << POWERUP_REGENERATION ) |
	( 1u << POWERUP_INVISIBILITY ) | ( 1u << POWERUP_AMMOREGEN ) | ( 1u << POWERUP_GUARD ) |
	( 1u << POWERUP_DOUBLER ) | ( 1u << POWERUP_SCOUT ) | ( 1u << POWERUP_ADRENALINE ) |
	( 1u << POWERUP_FC_ARMOR_REGEN );

mpLiveEventStream::mpLiveEventStream( kind_t streamKind ) {
	kind = streamKind;
	file = NULL;
	failed = false;
	sequence = 0;
	lastTime = 0;
}

mpLiveEventStream::~mpLiveEventStream() {
	// Nothing to do: idMultiplayerGame::Shutdown and Reset close the stream
	// while the file system is still there. This object lives in the game's
	// globals, whose destructors run after the engine has taken it away.
}

/*
================
mpLiveEventStream::Wanted

A demo being played back is not a live match, and its clock can run backwards.
================
*/
bool mpLiveEventStream::Wanted( void ) const {
	// the match log is the server's record of the match it ran; a client has none
	const bool enabled = kind == MATCH_LOG ? ( g_matchLog.GetBool() && gameLocal.isServer ) : g_matchEventStream.GetBool();
	return enabled && gameLocal.isMultiplayer &&
		( gameLocal.isServer || gameLocal.isClient ) &&
		gameLocal.GetDemoState() != DEMO_PLAYING && !gameLocal.isRepeater &&
		gameLocal.mpGame.GetGameState() != NULL;
}

/*
================
mpLiveEventStream::Path
mpLiveEventStream::Setting
================
*/
const char *mpLiveEventStream::Path( void ) const {
	return kind == MATCH_LOG ? MP_MATCH_LOG_FILE : MP_LIVE_EVENT_FILE;
}

const char *mpLiveEventStream::Setting( void ) const {
	return kind == MATCH_LOG ? "g_matchLog" : "g_matchEventStream";
}

/*
================
mpLiveEventStream::Stamp

The "t" of the next line. A client reads a death while its clock is on the
prediction it is running and compares scores on the frame it draws, two clocks
about a tenth of a second apart; taken as they come, a kill and the score it
caused went into the file with time running backwards.
================
*/
int mpLiveEventStream::Stamp( void ) {
	if ( gameLocal.time > lastTime ) {
		lastTime = gameLocal.time;
	}
	return lastTime;
}

/*
================
mpLiveEventStream::Capture

Only what the scoreboard, the flag status and the kill feed already show a
spectator: no positions, vitals or timers. A client reads the same fields from
the state the server replicated to it.
================
*/
void mpLiveEventStream::Capture( mpLiveEventState &current ) const {
	rvGameState *gameState = gameLocal.mpGame.GetGameState();
	current.phase = gameState->GetMPGameState();
	current.teamGame = gameLocal.IsTeamGame();
	// the teams' totals; GetTeamScore( n ) is client n's share of one
	current.teamScore[ TEAM_MARINE ] = gameLocal.mpGame.GetScoreForTeam( TEAM_MARINE );
	current.teamScore[ TEAM_STROGG ] = gameLocal.mpGame.GetScoreForTeam( TEAM_STROGG );
	current.flags = gameLocal.IsFlagGameType() && gameState->IsType( rvCTFGameState::GetClassType() );
	if ( current.flags ) {
		rvCTFGameState *ctf = static_cast< rvCTFGameState * >( gameState );
		for ( int flag = 0; flag < MAX_CTF_FLAGS; flag++ ) {
			current.flagState[ flag ] = ctf->GetFlagState( flag );
			current.flagCarrier[ flag ] = ctf->GetFlagCarrier( flag );
		}
	}
	for ( int slot = 0; slot < MAX_CLIENTS; slot++ ) {
		idEntity *entity = gameLocal.entities[ slot ];
		if ( slot >= gameLocal.numClients || entity == NULL || !entity->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}
		const idPlayer *player = static_cast< const idPlayer * >( entity );
		if ( player->IsFakeClient() || !gameLocal.mpGame.IsInGame( slot ) ) {
			continue;
		}
		mpLiveEventPlayer &row = current.players[ slot ];
		row.present = true;
		row.spectator = player->spectating;
		row.bot = gameLocal.isServer ? ( botManager.IsBot( slot ) ? 1 : 0 ) : -1;
		row.team = current.teamGame ? player->team : -1;
		row.score = gameLocal.mpGame.GetScore( slot );
		row.powerups = static_cast< std::uint32_t >( player->inventory.powerups );
		idStr name = gameLocal.userInfo[ slot ].GetString( "ui_name" );
		name.RemoveEscapes();
		idStr::Copynz( row.name, name.c_str(), sizeof( row.name ) );
	}
}

/*
================
mpLiveEventStream::Open
================
*/
bool mpLiveEventStream::Open( const mpLiveEventState &current ) {
	if ( failed ) {
		return false;
	}
	// The live file begins again with every stream. The match log keeps every
	// stream it was ever given, and says when each one began.
	char utc[ MP_MOD_UTC_TEXT_BYTES ] = "";
	if ( kind == MATCH_LOG ) {
		file = fileSystem->OpenFileAppend( Path(), false, "fs_savepath" );
		MPModFormatUtc( static_cast<mpModTime_t>( ::time( NULL ) ), utc );
	} else {
		file = fileSystem->OpenFileWrite( Path(), "fs_savepath" );
	}
	if ( file == NULL ) {
		failed = true;
		common->Warning( "%s: could not write %s", Setting(), Path() );
		return false;
	}
	sequence = 0;
	lastTime = gameLocal.time;
	idStr gameType = gameLocal.serverInfo.GetString( "si_gameType" );
	if ( MPLiveEventStreamLine( line, sequence, lastTime, gameLocal.isServer ? "server" : "client",
			gameLocal.serverInfo.GetString( "si_map" ), gameType.c_str(), current, utc ) ) {
		Write( line );
	}
	sequence++;
	return true;
}

/*
================
mpLiveEventStream::Write

Flushed line by line: a reader following the file sees an event when it
happens, and never half of one.
================
*/
void mpLiveEventStream::Write( const mpLiveEventLine &complete ) {
	if ( file == NULL ) {
		return;
	}
	if ( file->Write( complete.Text(), complete.Length() ) != complete.Length() ) {
		common->Warning( "%s: write to %s failed; the stream is closed", Setting(), Path() );
		fileSystem->CloseFile( file );
		file = NULL;
		failed = true;
		return;
	}
	file->Flush();
}

/*
================
mpLiveEventStream::Update
================
*/
void mpLiveEventStream::Update( void ) {
	if ( !Wanted() ) {
		if ( file != NULL || failed ) {
			Close();
		}
		return;
	}
	// A clock that falls well behind the stream is a new timeline, not jitter.
	if ( file != NULL && gameLocal.time < lastTime - 5000 ) {
		Close();
	}
	mpLiveEventState current;
	Capture( current );
	if ( file == NULL ) {
		if ( Open( current ) ) {
			state = current;
		}
		return;
	}
	MPLiveEventDiff( state, current, MP_LIVE_EVENT_TRACKED_POWERUPS, sequence, Stamp(), line,
		[this]( const mpLiveEventLine &complete ) { Write( complete ); } );
	state = current;
}

/*
================
mpLiveEventStream::Kill

methodOfDeath below MAX_WEAPONS is the weapon's index; MAX_WEAPONS is a death
with no weapon, and MAX_WEAPONS + 1 a telefrag.
================
*/
void mpLiveEventStream::Kill( const idPlayer *killer, const idPlayer *victim, int methodOfDeath, bool quad ) {
	if ( file == NULL || victim == NULL || !Wanted() ) {
		return;
	}
	idStr weapon;
	const char *cause = "weapon";
	if ( methodOfDeath >= 0 && methodOfDeath < MAX_WEAPONS ) {
		weapon = victim->spawnArgs.GetString( va( "def_weapon%d", methodOfDeath ) );
		weapon.StripLeading( "weapon_" );
	} else {
		cause = methodOfDeath == MAX_WEAPONS + 1 ? "telefrag" : "world";
	}
	if ( MPLiveEventKillLine( line, sequence, Stamp(), state, killer != NULL ? killer->entityNumber : -1,
			victim->entityNumber, weapon.Length() > 0 ? weapon.c_str() : NULL, cause, quad ) ) {
		Write( line );
	}
	sequence++;
}

/*
================
mpLiveEventStream::Close
================
*/
void mpLiveEventStream::Close( void ) {
	if ( file != NULL ) {
		fileSystem->CloseFile( file );
		file = NULL;
	}
	failed = false;
	sequence = 0;
	lastTime = 0;
	state = mpLiveEventState();
}
