//----------------------------------------------------------------
// TeamInfo.cpp
//
// openQ4 team overlay data (MP-15). See TeamInfo.h for what travels and why
// it travels to the team member alone.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "TeamInfo.h"

/*
================
TeamInfoPlayer

A client slot's player when it is on a team and playing: somebody who chose to
watch is not a team mate, somebody waiting out a round is.
================
*/
static idPlayer *TeamInfoPlayer( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return NULL;
	}
	idEntity *ent = gameLocal.entities[ clientNum ];
	if ( ent == NULL || !ent->IsType( idPlayer::GetClassType() ) ) {
		return NULL;
	}
	idPlayer *player = static_cast< idPlayer * >( ent );
	if ( player->wantSpectate || ( player->team != TEAM_MARINE && player->team != TEAM_STROGG ) ) {
		return NULL;
	}
	return player;
}

static void TeamInfoFromEntity( const idPlayer *player, mpTeamInfoEntry_t &entry ) {
	const bool dead = player->health <= 0 || player->spectating;
	const idVec3 &origin = player->GetPhysics()->GetOrigin();
	entry.clientNum = player->entityNumber;
	entry.health = dead ? 0 : MPTeamInfoPackVital( player->health );
	entry.armor = dead ? 0 : MPTeamInfoPackVital( player->inventory.armor );
	entry.weapon = dead ? -1 : MPTeamInfoUnpackWeapon( MPTeamInfoPackWeapon( player->GetCurrentWeapon(), MAX_WEAPONS ), MAX_WEAPONS );
	entry.origin[ 0 ] = MPTeamInfoPackCoord( origin.x );
	entry.origin[ 1 ] = MPTeamInfoPackCoord( origin.y );
	entry.origin[ 2 ] = MPTeamInfoPackCoord( origin.z );
}

/*
================
rvTeamInfo::rvTeamInfo
================
*/
rvTeamInfo::rvTeamInfo( void ) {
	Clear();
}

/*
================
rvTeamInfo::Clear
================
*/
void rvTeamInfo::Clear( void ) {
	memset( entries, 0, sizeof( entries ) );
	memset( received, 0, sizeof( received ) );
	nextSendTime = 0;
}

/*
================
rvTeamInfo::ServerFrame

One message per team member, holding that member's own team and nobody else.
The recipient is taken from the roster this frame, so a player who changes
sides stops hearing about the old team at the next send.
================
*/
void rvTeamInfo::ServerFrame( void ) {
	if ( gameLocal.isClient || !gameLocal.isMultiplayer || !gameLocal.IsTeamGame() ) {
		return;
	}
	// a send time further off than one interval is left over from another clock
	if ( gameLocal.time < nextSendTime && nextSendTime - gameLocal.time <= MP_TEAMINFO_SEND_MSEC ) {
		return;
	}
	nextSendTime = gameLocal.time + MP_TEAMINFO_SEND_MSEC;

	for ( int recipient = 0; recipient < Min( gameLocal.numClients, MAX_CLIENTS ); recipient++ ) {
		if ( recipient == gameLocal.localClientNum || botManager.IsBot( recipient ) ) {
			// the host reads the players directly; a bot has no HUD
			continue;
		}
		const idPlayer *member = TeamInfoPlayer( recipient );
		if ( member == NULL ) {
			continue;
		}

		mpTeamInfoEntry_t mates[ MAX_CLIENTS ];
		int count = 0;
		for ( int other = 0; other < Min( gameLocal.numClients, MAX_CLIENTS ); other++ ) {
			const idPlayer *mate = TeamInfoPlayer( other );
			if ( mate == NULL || mate == member || mate->team != member->team ) {
				continue;
			}
			TeamInfoFromEntity( mate, mates[ count++ ] );
		}
		if ( count == 0 ) {
			continue;
		}

		idBitMsg	msg;
		byte		msgBuf[ 16 + MAX_CLIENTS * 10 ];
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.BeginWriting();
		msg.WriteByte( GAME_UNRELIABLE_MESSAGE_TEAMINFO );
		msg.WriteByte( count );
		for ( int i = 0; i < count; i++ ) {
			msg.WriteByte( mates[ i ].clientNum );
			msg.WriteByte( mates[ i ].health );
			msg.WriteByte( mates[ i ].armor );
			msg.WriteByte( MPTeamInfoPackWeapon( mates[ i ].weapon, MAX_WEAPONS ) );
			msg.WriteShort( mates[ i ].origin[ 0 ] );
			msg.WriteShort( mates[ i ].origin[ 1 ] );
			msg.WriteShort( mates[ i ].origin[ 2 ] );
		}
		gameLocal.QueueUnreliableMessage( recipient, msg );
	}
}

/*
================
rvTeamInfo::ClientReceive

Every field is bounded before it is kept: the record feeds the HUD, and a
damaged packet must not be able to put a team mate outside the roster.
================
*/
void rvTeamInfo::ClientReceive( const idBitMsg &msg ) {
	const int count = msg.ReadByte();
	for ( int i = 0; i < count; i++ ) {
		if ( msg.GetRemainingReadBits() < 10 * 8 ) {
			return;
		}
		mpTeamInfoEntry_t entry;
		entry.clientNum = msg.ReadByte();
		entry.health = msg.ReadByte();
		entry.armor = msg.ReadByte();
		entry.weapon = MPTeamInfoUnpackWeapon( msg.ReadByte(), MAX_WEAPONS );
		entry.origin[ 0 ] = msg.ReadShort();
		entry.origin[ 1 ] = msg.ReadShort();
		entry.origin[ 2 ] = msg.ReadShort();
		if ( entry.clientNum < 0 || entry.clientNum >= MAX_CLIENTS ) {
			continue;
		}
		entries[ entry.clientNum ] = entry;
		received[ entry.clientNum ] = ( gameLocal.realClientTime != 0 ) ? gameLocal.realClientTime : 1;
	}
}

/*
================
rvTeamInfo::Resolve
================
*/
void rvTeamInfo::Resolve( const idPlayer *player, mpTeamInfoEntry_t &entry ) const {
	if ( UsesRecord( player ) ) {
		entry = entries[ player->entityNumber ];
		return;
	}
	TeamInfoFromEntity( player, entry );
}

/*
================
rvTeamInfo::UsesRecord

The record describes a team mate of the player it was sent to. It is used only
while that is still who is looking, and only for somebody on their side: a
client that has since moved to the other team falls back to what its own
snapshot shows.
================
*/
bool rvTeamInfo::UsesRecord( const idPlayer *player ) const {
	const int clientNum = player->entityNumber;
	const idPlayer *local = gameLocal.GetLocalPlayer();
	return gameLocal.isClient && !gameLocal.IsServerDemoPlaying() && local != NULL && player != local &&
		!local->wantSpectate && player->team == local->team &&
		clientNum >= 0 && clientNum < MAX_CLIENTS && MPTeamInfoFresh( received[ clientNum ], gameLocal.realClientTime );
}
