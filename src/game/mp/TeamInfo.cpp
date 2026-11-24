//----------------------------------------------------------------
// TeamInfo.cpp
//
// openQ4 team overlay data (MP-15). See TeamInfo.h for what travels and to
// whom.
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

/*
================
TeamInfoWatched

A playing client slot's player for someone who chose to watch, on a team or
not (Duel and Tourney have none).
================
*/
static idPlayer *TeamInfoWatched( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return NULL;
	}
	idEntity *ent = gameLocal.entities[ clientNum ];
	if ( ent == NULL || !ent->IsType( idPlayer::GetClassType() ) ) {
		return NULL;
	}
	idPlayer *player = static_cast< idPlayer * >( ent );
	return player->wantSpectate ? NULL : player;
}

/*
================
TeamInfoAmmoKnown

Ammunition is private player state: exact on a server, and on a client only
for its own player and the one it follows.
================
*/
static bool TeamInfoAmmoKnown( const idPlayer *player ) {
	if ( !gameLocal.isClient ) {
		return true;
	}
	const idPlayer *local = gameLocal.GetLocalPlayer();
	if ( local == NULL || gameLocal.IsServerDemoPlaying() ) {
		return false;
	}
	return player == local || ( local->spectating && local->spectator == player->entityNumber );
}

static int TeamInfoAmmo( const idPlayer *player ) {
	const rvWeapon *weapon = player->weapon;
	if ( weapon == NULL || !TeamInfoAmmoKnown( player ) ) {
		return MP_TEAMINFO_AMMO_UNKNOWN;
	}
	const int type = weapon->GetAmmoType();
	const bool unlimited = type <= 0 || type >= MAX_AMMO || weapon->AmmoRequired() <= 0 || player->inventory.ammo[ type ] < 0;
	return MPTeamInfoPackAmmo( unlimited ? 0 : player->inventory.ammo[ type ], weapon->GetMaxAmmo(), unlimited );
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
	entry.ammo = dead ? MP_TEAMINFO_AMMO_UNKNOWN : TeamInfoAmmo( player );
}

/*
================
TeamInfoSend
================
*/
static void TeamInfoSend( int recipient, const mpTeamInfoEntry_t *records, int count ) {
	idBitMsg	msg;
	byte		msgBuf[ 16 + MAX_CLIENTS * 11 ];
	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.BeginWriting();
	msg.WriteByte( GAME_UNRELIABLE_MESSAGE_TEAMINFO );
	msg.WriteByte( count );
	for ( int i = 0; i < count; i++ ) {
		msg.WriteByte( records[ i ].clientNum );
		msg.WriteByte( records[ i ].health );
		msg.WriteByte( records[ i ].armor );
		msg.WriteByte( MPTeamInfoPackWeapon( records[ i ].weapon, MAX_WEAPONS ) );
		msg.WriteShort( records[ i ].origin[ 0 ] );
		msg.WriteShort( records[ i ].origin[ 1 ] );
		msg.WriteShort( records[ i ].origin[ 2 ] );
	}
	// openQ4: the ammunition trailer, after everything a 0.15.0 client reads
	for ( int i = 0; i < count; i++ ) {
		msg.WriteByte( records[ i ].ammo );
	}
	gameLocal.QueueUnreliableMessage( recipient, msg );
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
sides stops hearing about the old team at the next send. On a casual server
someone who chose to watch hears about every player, in any mode; a managed
match keeps its own disclosure policy, as the item timers do.
================
*/
void rvTeamInfo::ServerFrame( void ) {
	if ( gameLocal.isClient || !gameLocal.isMultiplayer ) {
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
		idEntity *recipientEnt = gameLocal.entities[ recipient ];
		if ( recipientEnt != NULL && recipientEnt->IsType( idPlayer::GetClassType() ) &&
			 static_cast< idPlayer * >( recipientEnt )->wantSpectate ) {
			if ( gameLocal.mpGame.IsManagedMatch() ) {
				continue;
			}
			mpTeamInfoEntry_t watched[ MAX_CLIENTS ];
			int count = 0;
			for ( int other = 0; other < Min( gameLocal.numClients, MAX_CLIENTS ); other++ ) {
				const idPlayer *player = TeamInfoWatched( other );
				if ( player != NULL ) {
					TeamInfoFromEntity( player, watched[ count++ ] );
				}
			}
			if ( count > 0 ) {
				TeamInfoSend( recipient, watched, count );
			}
			continue;
		}
		const idPlayer *member = gameLocal.IsTeamGame() ? TeamInfoPlayer( recipient ) : NULL;
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
		TeamInfoSend( recipient, mates, count );
	}
}

/*
================
rvTeamInfo::ClientReceive

Every field is bounded before it is kept: the record feeds the HUD, and a
damaged packet must not be able to put a team mate outside the roster. The
ammunition trailer is read when it is there; an older server sends none.
================
*/
void rvTeamInfo::ClientReceive( const idBitMsg &msg ) {
	const int count = msg.ReadByte();
	int slots[ 256 ];
	int read = 0;
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
		entry.ammo = MP_TEAMINFO_AMMO_UNKNOWN;
		const bool valid = entry.clientNum >= 0 && entry.clientNum < MAX_CLIENTS;
		slots[ read++ ] = valid ? entry.clientNum : -1;
		if ( !valid ) {
			continue;
		}
		entries[ entry.clientNum ] = entry;
		received[ entry.clientNum ] = ( gameLocal.realClientTime != 0 ) ? gameLocal.realClientTime : 1;
	}
	if ( msg.GetRemainingReadBits() < read * 8 ) {
		return;
	}
	for ( int i = 0; i < read; i++ ) {
		const int ammo = MPTeamInfoUnpackAmmo( msg.ReadByte() );
		if ( slots[ i ] >= 0 ) {
			entries[ slots[ i ] ].ammo = ammo;
		}
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
	// openQ4: what the snapshot cannot say, a fresh record can
	const int clientNum = player->entityNumber;
	if ( entry.ammo == MP_TEAMINFO_AMMO_UNKNOWN && entry.health > 0 && gameLocal.isClient && !gameLocal.IsServerDemoPlaying() &&
		 clientNum >= 0 && clientNum < MAX_CLIENTS && MPTeamInfoFresh( received[ clientNum ], gameLocal.realClientTime ) ) {
		entry.ammo = entries[ clientNum ].ammo;
	}
}

/*
================
rvTeamInfo::UsesRecord

The record describes a team mate of the player it was sent to. It is used only
while that is still who is looking, and only for somebody on their side: a
client that has since moved to the other team falls back to what its own
snapshot shows. Someone who chose to watch holds records of every player and
uses them whole, so a player across the map reads current.
================
*/
bool rvTeamInfo::UsesRecord( const idPlayer *player ) const {
	const int clientNum = player->entityNumber;
	const idPlayer *local = gameLocal.GetLocalPlayer();
	return gameLocal.isClient && !gameLocal.IsServerDemoPlaying() && local != NULL && player != local &&
		( local->wantSpectate || player->team == local->team ) &&
		clientNum >= 0 && clientNum < MAX_CLIENTS && MPTeamInfoFresh( received[ clientNum ], gameLocal.realClientTime );
}
