// Copyright (C) 2026 DarkMatter Productions. Quake 4 SDK EULA applies to this game-module source.
//
// openQ4 MP-22: autoaction. A player's ui_autoAction and a server's
// si_autoAction name what is done for every match: "demo" records it from the
// start of live play to the result, "ss" takes a screenshot of the result,
// "stats" writes the result's stats to stats/<match>.json. Each match's files
// share one name (MPBuildMatchBasename). The player's side runs on the local
// game's state edges; the server's on its own.

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "AutoAction.h"
#include "stats/StatManager.h"

#include <time.h>

// The result's screenshot, stats and the end of the demo wait this long into
// the review, so they show the final scoreboard.
static const int MP_AUTOACTION_RESULT_DELAY_MS = 2000;

/*
================
MPAutoActionBasename
================
*/
static idStr MPAutoActionBasename( bool local ) {
	const time_t now = ::time( NULL );
	struct tm parts;
	memset( &parts, 0, sizeof( parts ) );
#ifdef _WIN32
	localtime_s( &parts, &now );
#else
	localtime_r( &now, &parts );
#endif
	idStr player;
	if ( local && gameLocal.localClientNum >= 0 && gameLocal.localClientNum < MAX_CLIENTS ) {
		player = gameLocal.userInfo[ gameLocal.localClientNum ].GetString( "ui_name" );
	}
	char name[ MP_MATCH_BASENAME_MAX + 1 ];
	MPBuildMatchBasename( name, sizeof( name ), parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
		parts.tm_hour, parts.tm_min, parts.tm_sec, MPGameTypeName( gameLocal.gameType ),
		gameLocal.serverInfo.GetString( "si_map" ), local ? player.c_str() : NULL );
	return name;
}

/*
================
idMultiplayerGame::AutoActionActions
================
*/
int idMultiplayerGame::AutoActionActions( bool local ) const {
	// a demo being watched is not a match being played
	if ( gameLocal.GetDemoState() == DEMO_PLAYING || gameLocal.IsServerDemoPlaying() ) {
		return 0;
	}
	if ( local ) {
		if ( gameLocal.GetLocalPlayer() == NULL ) {
			return 0;
		}
		int actions = MPAutoActionWords( ui_autoAction.GetString() );
		// a host whose server records the match records it once
		if ( gameLocal.isServer && ( MPAutoActionWords( si_autoAction.GetString() ) & MP_AUTOACTION_DEMO ) != 0 ) {
			actions &= ~MP_AUTOACTION_DEMO;
		}
		return actions;
	}
	if ( !gameLocal.isServer ) {
		return 0;
	}
	// the server takes no screenshot: a dedicated one has no view, a host's player takes their own
	return MPAutoActionWords( si_autoAction.GetString() ) & ( MP_AUTOACTION_DEMO | MP_AUTOACTION_STATS );
}

/*
================
idMultiplayerGame::AutoActionMatchStarted

Live play began: start the demo, and note the match's name.
================
*/
void idMultiplayerGame::AutoActionMatchStarted( bool local ) {
	const int slot = local ? 0 : 1;
	autoActionPending[ slot ] = 0;
	autoActionResultTime[ slot ] = 0;
	autoActionDemo[ slot ] = false;
	const int actions = AutoActionActions( local );
	autoActionActions[ slot ] = actions;
	if ( actions == 0 ) {
		autoActionName[ slot ].Clear();
		return;
	}
	autoActionName[ slot ] = MPAutoActionBasename( local );
	if ( ( actions & MP_AUTOACTION_DEMO ) != 0 ) {
		if ( networkSystem->ServerIsMVDRecording() ) {
			// someone's own recording runs on; autoaction neither restarts nor ends it
			gameLocal.Printf( "autoaction: a demo is already recording; this match is not recorded separately\n" );
		} else if ( local ) {
			// quoted: the tokenizer would split the name at the dash before its time
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "record \"%s\"\n", autoActionName[ slot ].c_str() ) );
			autoActionDemo[ slot ] = true;
		} else if ( !IsManagedMatch() ) {
			// a managed match records itself (the session's own MVD)
			autoActionDemo[ slot ] = networkSystem->ServerStartMVDRecording( autoActionName[ slot ].c_str() );
		}
	}
	gameLocal.Printf( "autoaction: %s match %s\n", local ? "this player's" : "the server's", autoActionName[ slot ].c_str() );
}

/*
================
idMultiplayerGame::AutoActionMatchEnded

The result is in: the screenshot, the stats and the end of the demo follow
once the scoreboard is up.
================
*/
void idMultiplayerGame::AutoActionMatchEnded( bool local ) {
	const int slot = local ? 0 : 1;
	if ( autoActionName[ slot ].IsEmpty() ) {
		return;
	}
	autoActionPending[ slot ] = autoActionActions[ slot ] & ( MP_AUTOACTION_SHOT | MP_AUTOACTION_STATS );
	if ( autoActionDemo[ slot ] ) {
		autoActionPending[ slot ] |= MP_AUTOACTION_DEMO;
	}
	autoActionResultTime[ slot ] = gameLocal.time + MP_AUTOACTION_RESULT_DELAY_MS;
}

/*
================
idMultiplayerGame::AutoActionFrame
================
*/
void idMultiplayerGame::AutoActionFrame( void ) {
	for ( int slot = 0; slot < 2; slot++ ) {
		if ( autoActionPending[ slot ] == 0 || gameLocal.time < autoActionResultTime[ slot ] ) {
			continue;
		}
		const int pending = autoActionPending[ slot ];
		autoActionPending[ slot ] = 0;
		const char *name = autoActionName[ slot ].c_str();
		if ( ( pending & MP_AUTOACTION_SHOT ) != 0 ) {
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "screenshot \"screenshots/%s.tga\"\n", name ) );
		}
		if ( ( pending & MP_AUTOACTION_STATS ) != 0 ) {
			WriteAutoActionStats( name );
		}
		if ( ( pending & MP_AUTOACTION_DEMO ) != 0 && networkSystem->ServerIsMVDRecording() ) {
			if ( slot == 0 ) {
				cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "stopRecord\n" );
			} else {
				networkSystem->ServerStopMVDRecording( "match ended" );
			}
		}
		autoActionDemo[ slot ] = false;
	}
}

/*
================
MPJsonString
================
*/
static idStr MPJsonString( const char *text ) {
	idStr clean = text != NULL ? text : "";
	clean.RemoveColors();
	idStr out = "\"";
	for ( int i = 0; i < clean.Length(); i++ ) {
		const unsigned char c = static_cast<unsigned char>( clean[ i ] );
		if ( c == '"' || c == '\\' ) {
			out += '\\';
			out += static_cast<char>( c );
		} else if ( c < 0x20 ) {
			out += va( "\\u%04x", c );
		} else {
			out += static_cast<char>( c );
		}
	}
	out += "\"";
	return out;
}

/*
================
idMultiplayerGame::WriteAutoActionStats

The result as this game knows it, in stats/<match>.json: the scoreboard and
each player's kills, deaths and damage. A client knows every score; the other
counts are what the stat manager holds for each player.
================
*/
void idMultiplayerGame::WriteAutoActionStats( const char *name ) {
	idStr json = "{\n";
	json += va( "  \"format\": \"openq4-match-stats\",\n  \"version\": 1,\n  \"match\": %s,\n", MPJsonString( name ).c_str() );
	json += va( "  \"map\": %s,\n  \"gameType\": %s,\n  \"server\": %s,\n",
		MPJsonString( gameLocal.serverInfo.GetString( "si_map" ) ).c_str(),
		MPJsonString( MPGameTypeName( gameLocal.gameType ) ).c_str(),
		MPJsonString( gameLocal.serverInfo.GetString( "si_name" ) ).c_str() );
	if ( gameLocal.IsTeamGame() ) {
		json += va( "  \"teamScores\": { \"marine\": %d, \"strogg\": %d },\n",
			GetScoreForTeam( TEAM_MARINE ), GetScoreForTeam( TEAM_STROGG ) );
	}
	json += "  \"players\": [";
	bool first = true;
	for ( int i = 0; i < gameLocal.numClients && i < MAX_CLIENTS; i++ ) {
		const idEntity *ent = gameLocal.entities[ i ];
		if ( ent == NULL || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}
		const idPlayer *player = static_cast<const idPlayer *>( ent );
		const rvPlayerStat *stat = statManager != NULL ? statManager->GetPlayerStat( i ) : NULL;
		const char *team = !gameLocal.IsTeamGame() ? "none" : player->team == TEAM_MARINE ? "marine" : "strogg";
		json += first ? "\n" : ",\n";
		first = false;
		json += va( "    { \"slot\": %d, \"name\": %s, \"team\": \"%s\", \"spectator\": %s, \"score\": %d",
			i, MPJsonString( gameLocal.userInfo[ i ].GetString( "ui_name" ) ).c_str(), team,
			player->spectating ? "true" : "false", GetScore( i ) );
		if ( stat != NULL ) {
			json += va( ", \"kills\": %d, \"deaths\": %d, \"suicides\": %d, \"damageGiven\": %d, \"damageTaken\": %d",
				stat->kills, stat->deaths, stat->suicides, stat->damageGiven, stat->damageTaken );
		}
		json += " }";
	}
	json += "\n  ]\n}\n";

	const idStr path = va( "stats/%s.json", name );
	idFile *file = fileSystem->OpenFileWrite( path.c_str() );
	if ( file == NULL ) {
		gameLocal.Warning( "autoaction: could not write %s", path.c_str() );
		return;
	}
	file->Write( json.c_str(), json.Length() );
	fileSystem->CloseFile( file );
	gameLocal.Printf( "autoaction: wrote %s\n", path.c_str() );
}
