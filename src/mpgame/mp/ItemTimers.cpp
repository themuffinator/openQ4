//----------------------------------------------------------------
// ItemTimers.cpp
//
// openQ4 item timers on casual servers (MP-15). See ItemTimers.h for the
// policy; this file tracks the items on the server and holds what a client
// was sent.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "ItemTimers.h"

/*
================
rvItemTimers::KindForItem

A strict allowlist by entity definition, the same items a managed match times.
================
*/
itemTimerKind_t rvItemTimers::KindForItem( const idItem *item ) {
	if ( item == NULL ) {
		return ITEMTIMER_NONE;
	}
	const char *className = item->spawnArgs.GetString( "classname" );
	if ( idStr::Icmp( className, "powerup_quad_damage" ) == 0 ) {
		return ITEMTIMER_QUAD_DAMAGE;
	}
	if ( idStr::Icmp( className, "powerup_haste" ) == 0 ) {
		return ITEMTIMER_HASTE;
	}
	if ( idStr::Icmp( className, "powerup_regeneration" ) == 0 ) {
		return ITEMTIMER_REGENERATION;
	}
	if ( idStr::Icmp( className, "powerup_invisibility" ) == 0 ) {
		return ITEMTIMER_INVISIBILITY;
	}
	if ( idStr::Icmp( className, "item_health_mega" ) == 0 ) {
		return ITEMTIMER_MEGA_HEALTH;
	}
	if ( idStr::Icmp( className, "item_armor_large" ) == 0 || idStr::Icmp( className, "item_armor_large_mp" ) == 0 ) {
		return ITEMTIMER_LARGE_ARMOR;
	}
	if ( idStr::Icmp( className, "item_armor_small" ) == 0 || idStr::Icmp( className, "item_armor_small_mp" ) == 0 ) {
		return ITEMTIMER_SMALL_ARMOR;
	}
	return ITEMTIMER_NONE;
}

/*
================
rvItemTimers::DefNameForKind
================
*/
const char *rvItemTimers::DefNameForKind( int kind ) {
	static const char * const defNames[ ITEMTIMER_KIND_COUNT ] = {
		"",
		"powerup_quad_damage",
		"powerup_haste",
		"powerup_regeneration",
		"powerup_invisibility",
		"item_health_mega",
		"item_armor_large",
		"item_armor_small"
	};
	return ( kind > ITEMTIMER_NONE && kind < ITEMTIMER_KIND_COUNT ) ? defNames[ kind ] : "";
}

/*
================
rvItemTimers::rvItemTimers
================
*/
rvItemTimers::rvItemTimers( void ) {
	Clear();
}

/*
================
rvItemTimers::Clear
================
*/
void rvItemTimers::Clear( void ) {
	memset( respawnAt, 0, sizeof( respawnAt ) );
	nextSendTime = 0;
	memset( clientRows, 0, sizeof( clientRows ) );
	clientRowCount = 0;
	clientReceived = 0;
}

/*
================
rvItemTimers::Tracked

A map item of a timed kind that comes back: not one a player dropped, was
handed, or that the map placed once.
================
*/
bool rvItemTimers::Tracked( const idItem *item ) const {
	if ( item == NULL || item->entityNumber < MAX_CLIENTS || item->entityNumber >= MAX_GENTITIES ||
		 KindForItem( item ) == ITEMTIMER_NONE ) {
		return false;
	}
	const idDict &args = item->spawnArgs;
	if ( args.GetBool( "dropped" ) || args.GetBool( "no_respawn" ) || args.GetBool( "inv_objective" ) ||
		 args.GetInt( "givenToPlayer", "-1" ) != -1 ) {
		return false;
	}
	float respawn = args.GetFloat( va( "respawn_%s", gameLocal.serverInfo.GetString( "si_gameType" ) ), "-1.0" );
	if ( respawn < 0.0f ) {
		respawn = args.GetFloat( "respawn", "5.0" );
	}
	return respawn > 0.0f;
}

/*
================
rvItemTimers::OnPickup
================
*/
void rvItemTimers::OnPickup( const idItem *item, float respawnSeconds ) {
	if ( gameLocal.isClient || !Tracked( item ) ) {
		return;
	}
	if ( !( respawnSeconds > 0.0f ) || respawnSeconds > 24.0f * 60.0f * 60.0f ) {
		return;
	}
	respawnAt[ item->entityNumber ] = Max( 1, gameLocal.time + SEC2MS( respawnSeconds ) );
	nextSendTime = 0;
}

/*
================
rvItemTimers::OnHeld

Taken, with a respawn that has not started counting: Classic mega health waits
for its holder to drain first.
================
*/
void rvItemTimers::OnHeld( const idItem *item ) {
	if ( gameLocal.isClient || !Tracked( item ) ) {
		return;
	}
	respawnAt[ item->entityNumber ] = -1;
	nextSendTime = 0;
}

/*
================
rvItemTimers::OnMapRestart

The restart in place at a match's start and at each round respawns every map
entity under its old number and keeps the clock running, but shuts the event
queue, so no item's respawn event ever reports it back. A quad taken in warmup
went on counting down on the list through the first minute of the match while
it lay on its pad.
================
*/
void rvItemTimers::OnMapRestart( int instance ) {
	if ( gameLocal.isClient ) {
		return;
	}
	for ( int entityNum = 0; entityNum < MAX_GENTITIES; entityNum++ ) {
		if ( respawnAt[ entityNum ] == 0 ) {
			continue;
		}
		// a Tourney arena restarts alone: the other arenas' items are still out
		const idEntity *ent = gameLocal.entities[ entityNum ];
		if ( instance == -1 || ent == NULL || ent->GetInstance() == instance ) {
			respawnAt[ entityNum ] = 0;
		}
	}
	nextSendTime = 0;
}

/*
================
rvItemTimers::OnAvailable
================
*/
void rvItemTimers::OnAvailable( const idItem *item ) {
	if ( gameLocal.isClient || item == NULL || item->entityNumber < 0 || item->entityNumber >= MAX_GENTITIES ) {
		return;
	}
	if ( respawnAt[ item->entityNumber ] != 0 ) {
		respawnAt[ item->entityNumber ] = 0;
		nextSendTime = 0;
	}
}

/*
================
rvItemTimers::ShiftMatchTime

The game clock keeps running through a pause while the item's own respawn
event is moved later, so the time it returns is moved with it.
================
*/
void rvItemTimers::ShiftMatchTime( int deltaMsec ) {
	if ( gameLocal.isClient || deltaMsec <= 0 ) {
		return;
	}
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		if ( respawnAt[ i ] > 0 ) {
			respawnAt[ i ] += deltaMsec;
		}
	}
}

/*
================
rvItemTimers::BuildRows

The tracked items of one instance (every instance when it is negative), the
items a match turns on first and in map order within a kind, so a row keeps
its place from one second to the next.
================
*/
int rvItemTimers::BuildRows( itemTimerRow_t *rows, int maxRows, int instance ) const {
	itemTimerTracked_t tracked[ MP_ITEMTIMERS_MAX_TRACKED ];
	const int trackedCount = CollectTracked( tracked, MP_ITEMTIMERS_MAX_TRACKED );
	return RowsFromTracked( tracked, trackedCount, rows, maxRows, instance );
}

/*
================
rvItemTimers::CollectTracked

openQ4: every tracked item in list order (priority, then map order), in one pass
over the entities. The list used to be built from three passes over every entity
slot for each recipient, and once more for a recording; now a send collects once
and RowsFromTracked picks out each recipient's instance.
================
*/
int rvItemTimers::CollectTracked( itemTimerTracked_t *tracked, int maxTracked ) const {
	int bucketCount[ 3 ] = { 0, 0, 0 };
	itemTimerTracked_t found[ MP_ITEMTIMERS_MAX_TRACKED ];
	int foundCount = 0;
	for ( int entityNumber = MAX_CLIENTS; entityNumber < MAX_GENTITIES && foundCount < MP_ITEMTIMERS_MAX_TRACKED; entityNumber++ ) {
		idEntity *ent = gameLocal.entities[ entityNumber ];
		if ( ent == NULL || !ent->IsType( idItem::GetClassType() ) ) {
			continue;
		}
		const idItem *item = static_cast< const idItem * >( ent );
		const itemTimerKind_t kind = KindForItem( item );
		const int priority = MPItemTimerPriority( kind );
		if ( priority < 0 || priority > 2 || !Tracked( item ) ) {
			continue;
		}
		itemTimerTracked_t &entry = found[ foundCount++ ];
		entry.entityNumber = entityNumber;
		entry.kind = kind;
		entry.priority = priority;
		entry.instance = item->GetInstance();
		bucketCount[ priority ]++;
	}

	// a stable split by priority keeps map order within each
	int count = 0;
	for ( int priority = 0; priority < 3; priority++ ) {
		for ( int i = 0; i < foundCount && count < maxTracked && bucketCount[ priority ] > 0; i++ ) {
			if ( found[ i ].priority == priority ) {
				tracked[ count++ ] = found[ i ];
			}
		}
	}
	return count;
}

/*
================
rvItemTimers::RowsFromTracked
================
*/
int rvItemTimers::RowsFromTracked( const itemTimerTracked_t *tracked, int trackedCount, itemTimerRow_t *rows, int maxRows, int instance ) const {
	int count = 0;
	for ( int i = 0; i < trackedCount && count < maxRows; i++ ) {
		const itemTimerTracked_t &entry = tracked[ i ];
		if ( instance >= 0 && entry.instance != instance ) {
			continue;
		}
		itemTimerRow_t &row = rows[ count++ ];
		row.kind = entry.kind;
		row.entityNumber = entry.entityNumber;
		row.state = MPItemTimerPackState( respawnAt[ entry.entityNumber ], gameLocal.time );
		row.respawnAt = 0;
	}
	return count;
}

/*
================
rvItemTimers::ServerFrame

Once a second, and at once when an item is taken or returns. Only an admitted
client is sent anything: the policy is enforced here, where the message is
written, and not by asking the client to look away.
================
*/
void rvItemTimers::ServerFrame( void ) {
	if ( gameLocal.isClient || !gameLocal.isMultiplayer ) {
		return;
	}
	// a send time further off than one interval is left over from another clock
	if ( gameLocal.time < nextSendTime && nextSendTime - gameLocal.time <= MP_ITEMTIMERS_SEND_MSEC ) {
		return;
	}
	nextSendTime = gameLocal.time + MP_ITEMTIMERS_SEND_MSEC;

	const int policy = si_itemTimers.GetInteger();
	const bool managed = gameLocal.mpGame.IsManagedMatch();
	if ( policy <= ITEMTIMERS_OFF || managed ) {
		return;
	}

	itemTimerTracked_t tracked[ MP_ITEMTIMERS_MAX_TRACKED ];
	const int trackedCount = CollectTracked( tracked, MP_ITEMTIMERS_MAX_TRACKED );

	bool sent = false;
	for ( int recipient = 0; recipient < Min( gameLocal.numClients, MAX_CLIENTS ); recipient++ ) {
		if ( recipient == gameLocal.localClientNum || botManager.IsBot( recipient ) ) {
			continue;
		}
		idEntity *ent = gameLocal.entities[ recipient ];
		if ( ent == NULL || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}
		const idPlayer *player = static_cast< const idPlayer * >( ent );
		if ( !MPItemTimersAdmit( policy, managed, player->wantSpectate ) ) {
			continue;
		}

		itemTimerRow_t rows[ MP_ITEMTIMERS_MAX_ROWS ];
		const int count = RowsFromTracked( tracked, trackedCount, rows, MP_ITEMTIMERS_MAX_ROWS, player->GetInstance() );

		idBitMsg	msg;
		byte		msgBuf[ 16 + MP_ITEMTIMERS_MAX_ROWS * 4 ];
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.BeginWriting();
		msg.WriteByte( GAME_UNRELIABLE_MESSAGE_ITEMTIMERS );
		msg.WriteByte( count );
		for ( int i = 0; i < count; i++ ) {
			msg.WriteShort( rows[ i ].entityNumber );
			msg.WriteByte( rows[ i ].kind );
			msg.WriteByte( rows[ i ].state );
		}
		gameLocal.QueueUnreliableMessage( recipient, msg );
		sent = true;
	}

	// A recording is watched by spectators, so it carries the list whenever the
	// policy shows it to anyone; one copy, addressed to whoever plays it back.
	if ( sent || gameLocal.IsServerDemoRecording() ) {
		itemTimerRow_t rows[ MP_ITEMTIMERS_MAX_ROWS ];
		const int count = RowsFromTracked( tracked, trackedCount, rows, MP_ITEMTIMERS_MAX_ROWS, -1 );

		idBitMsg	msg;
		byte		msgBuf[ 16 + MP_ITEMTIMERS_MAX_ROWS * 4 ];
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.BeginWriting();
		msg.WriteByte( GAME_UNRELIABLE_MESSAGE_ITEMTIMERS );
		msg.WriteByte( count );
		for ( int i = 0; i < count; i++ ) {
			msg.WriteShort( rows[ i ].entityNumber );
			msg.WriteByte( rows[ i ].kind );
			msg.WriteByte( rows[ i ].state );
		}
		gameLocal.RecordUnreliableMessage( msg );
	}
}

/*
================
rvItemTimers::ClientReceive
================
*/
void rvItemTimers::ClientReceive( const idBitMsg &msg ) {
	const int now = ( gameLocal.realClientTime != 0 ) ? gameLocal.realClientTime : 1;
	const int count = msg.ReadByte();
	int kept = 0;
	for ( int i = 0; i < count; i++ ) {
		if ( msg.GetRemainingReadBits() < 4 * 8 ) {
			break;
		}
		const int entityNumber = msg.ReadShort();
		const int kind = msg.ReadByte();
		const int state = msg.ReadByte();
		if ( kept >= MP_ITEMTIMERS_MAX_ROWS || kind <= ITEMTIMER_NONE || kind >= ITEMTIMER_KIND_COUNT ||
			 entityNumber < 0 || entityNumber >= MAX_GENTITIES ) {
			continue;
		}
		itemTimerRow_t &row = clientRows[ kept++ ];
		row.kind = kind;
		row.entityNumber = entityNumber;
		row.state = state;
		if ( state == MP_ITEMTIMER_STATE_HELD ) {
			row.respawnAt = -1;
		} else if ( state == MP_ITEMTIMER_STATE_AVAILABLE ) {
			row.respawnAt = 0;
		} else {
			row.respawnAt = now + Min( state, MP_ITEMTIMER_STATE_MAX_SECONDS ) * 1000;
		}
	}
	clientRowCount = kept;
	clientReceived = now;
}

/*
================
rvItemTimers::GetRows
================
*/
int rvItemTimers::GetRows( itemTimerRow_t *rows, int maxRows ) const {
	if ( rows == NULL || maxRows <= 0 || !gameLocal.isMultiplayer ) {
		return 0;
	}
	if ( gameLocal.isClient ) {
		const int now = gameLocal.realClientTime;
		if ( clientReceived == 0 || now - clientReceived < 0 || now - clientReceived > MP_ITEMTIMERS_FRESH_MSEC ) {
			return 0;
		}
		const int count = Min( clientRowCount, maxRows );
		for ( int i = 0; i < count; i++ ) {
			rows[ i ] = clientRows[ i ];
		}
		return count;
	}

	// the host of a listen server, under the policy everyone else is under
	const idPlayer *local = gameLocal.GetLocalPlayer();
	if ( local == NULL || !MPItemTimersAdmit( si_itemTimers.GetInteger(), gameLocal.mpGame.IsManagedMatch(), local->wantSpectate ) ) {
		return 0;
	}
	const int count = BuildRows( rows, Min( maxRows, MP_ITEMTIMERS_MAX_ROWS ), local->GetInstance() );
	for ( int i = 0; i < count; i++ ) {
		const int at = respawnAt[ rows[ i ].entityNumber ];
		rows[ i ].respawnAt = ( at < 0 ) ? -1 : ( at == 0 || at <= gameLocal.time ) ? 0 : gameLocal.realClientTime + ( at - gameLocal.time );
	}
	return count;
}
