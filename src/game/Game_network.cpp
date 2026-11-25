#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Game_local.h"
#include "physics/PhysicsTelemetry.h"
// RAVEN BEGIN
// bdube: client entities
#include "client/ClientEffect.h"
// RAVEN END
// openQ4 MP-18: bans by address, text mutes, the chat flood window, the admin log
#include "mp/ModerationFile.h"

/*
===============================================================================

	Client running game code:
	- entity events don't work and should not be issued
	- entities should never be spawned outside idGameLocal::ClientReadSnapshot

===============================================================================
*/

// adds tags to the network protocol to detect when things go bad ( internal consistency )
// NOTE: this changes the network protocol
// the check tag written after every entity in a snapshot (ASYNC_WRITE_TAGS)
const int SNAPSHOT_ENTITY_TAG_BITS = 8;
const int SNAPSHOT_ENTITY_TAG_MASK = ( 1 << SNAPSHOT_ENTITY_TAG_BITS ) - 1;

#ifndef ASYNC_WRITE_TAGS
	#define ASYNC_WRITE_TAGS 1
#endif

idCVar net_clientShowSnapshot( "net_clientShowSnapshot", "0", CVAR_GAME | CVAR_INTEGER, "", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar net_clientShowSnapshotRadius( "net_clientShowSnapshotRadius", "128", CVAR_GAME | CVAR_FLOAT, "" );
idCVar net_clientMaxPrediction( "net_clientMaxPrediction", "1000", CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT, "maximum number of milliseconds a client can predict ahead of server." );
idCVar net_clientLagOMeter( "net_clientLagOMeter", "0", CVAR_GAME | CVAR_BOOL | CVAR_NOCHEAT | PC_CVAR_ARCHIVE, "draw prediction graph" );
idCVar net_clientLagOMeterResolution( "net_clientLagOMeterResolution", "16", CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT | PC_CVAR_ARCHIVE, "resolution for predict ahead graph (upper part of lagometer)", 1, 100 );

// RAVEN BEGIN
// ddynerman: performance profiling
int net_entsInSnapshot;
int net_snapshotSize;

extern const int ASYNC_PLAYER_FRAG_BITS;
// RAVEN END

// message senders
idNullMessageSender nullSender;
idServerReliableMessageSender serverReliableSender;
idRepeaterReliableMessageSender repeaterReliableSender;

/*
================
HasBoundedReliableString

Typed protocol fields must reject a missing terminator or truncation rather
than letting idBitMsg::ReadString silently normalize an oversized value.
================
*/
static bool HasBoundedReliableString( const idBitMsg &msg, int maxBytesIncludingTerminator ) {
	if ( msg.GetReadBit() != 0 || maxBytesIncludingTerminator <= 0 ) {
		return false;
	}

	const int remainingBytes = msg.GetRemainingData();
	if ( remainingBytes <= 0 ) {
		return false;
	}

	const int searchBytes = Min( remainingBytes, maxBytesIncludingTerminator );
	return memchr( msg.GetReadData(), '\0', searchBytes ) != NULL;
}

/*
================
idGameLocal::InitAsyncNetwork
================
*/
void idGameLocal::InitAsyncNetwork( void ) {
	memset( clientEntityStates, 0, sizeof( clientEntityStates ) );
	memset( clientPVS, 0, sizeof( clientPVS ) );
	memset( clientSnapshots, 0, sizeof( clientSnapshots ) );

	if ( maxViewers > 0 ) {
		memset( viewerEntityStates, 0, sizeof( *viewerEntityStates ) * maxViewers );
		memset( viewerPVS, 0, sizeof( *viewerPVS ) * maxViewers );
		memset( viewerSnapshots, 0, sizeof( *viewerSnapshots ) * maxViewers );
		memset( viewerUnreliableMessages, 0, sizeof( *viewerUnreliableMessages ) * maxViewers );
	}

	eventQueue.Init();

	// we used to have some special case -1 value, but we don't anymore so this is always >= 0 now
	entityDefBits = idMath::BitsForInteger( declManager->GetNumDecls( DECL_ENTITYDEF ) ) + 1;
	// The campaign never sends entity defs, but its saves record the value the
	// single-player library stored: Doom 3's negated (signed-bits) form.
	if ( !IsMultiplayerMode() ) {
		entityDefBits = -entityDefBits;
	}
	localClientNum = 0; // on a listen server SetLocalUser will set this right
	realClientTime = 0;
	clientSnapshotTime = 0;
	isNewFrame = true;
	clientReplayTics = 0;
	lastChainReplayTics = 0;

	nextLagoCheck = 0;
}

/*
================
idGameLocal::ShutdownAsyncNetwork
================
*/
void idGameLocal::ShutdownAsyncNetwork( void ) {
	entityStateAllocator.Shutdown();
	snapshotAllocator.Shutdown();
	eventQueue.Shutdown();
	memset( clientEntityStates, 0, sizeof( clientEntityStates ) );
	memset( clientPVS, 0, sizeof( clientPVS ) );
	memset( clientSnapshots, 0, sizeof( clientSnapshots ) );

	if ( maxViewers > 0 ) {
		memset( viewerEntityStates, 0, sizeof( *viewerEntityStates ) * maxViewers );
		memset( viewerPVS, 0, sizeof( *viewerPVS ) * maxViewers );
		memset( viewerSnapshots, 0, sizeof( *viewerSnapshots ) * maxViewers );
		memset( viewerUnreliableMessages, 0, sizeof( *viewerUnreliableMessages ) * maxViewers );
	}
}

/*
================
idGameLocal::InitLocalClient
================
*/
void idGameLocal::InitLocalClient( int clientNum ) {
	isServer = false;
	isClient = true;
	localClientNum = clientNum;

	if ( clientNum == MAX_CLIENTS && !entities[ ENTITYNUM_NONE ] ) {
		const int restoreFollowClient = GetDemoFollowClient();
		SpawnPlayer( ENTITYNUM_NONE );
		idPlayer *player = gameLocal.GetLocalPlayer();
		assert( player );
		player->SpawnFromSpawnSpot();
		// World restarts recreate the offline viewer while keeping real client
		// slots. Restore its selected POV before prediction publishes free view.
		if ( !SetDemoFollowClient( restoreFollowClient ) ) {
			SetDemoFollowClient( -1 );
		}
	}
}

/*
================
idGameLocal::ServerAllowClient
clientId is the ID of the connecting client - can later be mapped to a clientNum by calling networkSystem->ServerGetClientNum( clientId )
================
*/
allowReply_t idGameLocal::ServerAllowClient( int clientId, int numClients, const char *IP, const char *guid, const char *password, const char *privatePassword, char reason[ MAX_STRING_CHARS ] ) {
	reason[0] = '\0';

	// openQ4 MP-18: bans are by address. IP is the endpoint the connect came
	// from, which proved its challenge there; the GUID is empty on every client.
	if ( ModerationRefuseBanned( IP ) ) {
		idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107239" );
		return ALLOW_NO;
	}

	if ( serverInfo.GetInt( "si_pure" ) && !mpGame.IsPureReady() ) {
		idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107139" );
		return ALLOW_NOTYET;
	}

	if ( !serverInfo.GetInt( "si_maxPlayers" ) ) {
		idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107140" );
		return ALLOW_NOTYET;
	}

	// completely full
	if ( numClients >= serverInfo.GetInt( "si_maxPlayers" ) ) {
		idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107141" );
		return ALLOW_NOTYET;
	}

	// check private clients
	if( serverInfo.GetInt( "si_privatePlayers" ) > 0 ) {
		// just in case somehow we have a stale private clientId that matches a new client
		mpGame.RemovePrivatePlayer( clientId );

		const char *privatePass = cvarSystem->GetCVarString( "g_privatePassword" );
		if( privatePass[ 0 ] == '\0' ) {
			common->Warning( "idGameLocal::ServerAllowClient() - si_privatePlayers > 0 with no g_privatePassword" );
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "say si_privatePlayers is set but g_privatePassword is empty" );
			idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107142" );
			return ALLOW_NOTYET;
		}


		int numPrivateClients = cvarSystem->GetCVarInteger( "si_numPrivatePlayers" );

		// private clients that take up public slots are considered public clients
		numPrivateClients = idMath::ClampInt( 0, serverInfo.GetInt( "si_privatePlayers" ), numPrivateClients );

		if ( !idStr::Cmp( privatePass, privatePassword ) ) {
			// once this client spawns in, they'll be marked private
			mpGame.AddPrivatePlayer( clientId );
		} else if( (numClients - numPrivateClients) >= (serverInfo.GetInt( "si_maxPlayers" ) - serverInfo.GetInt( "si_privatePlayers" )) ) {
			// if the number of public clients is greater than or equal to the number of public slots, require a private slot
			idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107141" );
			return ALLOW_NOTYET;
		}
	} 
	

	if ( !cvarSystem->GetCVarBool( "si_usePass" ) ) {
		return ALLOW_YES;
	}

	const char *pass = cvarSystem->GetCVarString( "g_password" );
	if ( pass[ 0 ] == '\0' ) {
		common->Warning( "si_usePass is set but g_password is empty" );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "say si_usePass is set but g_password is empty" );
		// avoids silent misconfigured state
		idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107142" );
		return ALLOW_NOTYET;
	}

	if ( !idStr::Cmp( pass, password ) ) {
		return ALLOW_YES;
	}

	idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107143" );
	Printf( "Rejecting client %s from IP %s: invalid password\n", guid, IP );
	return ALLOW_BADPASS;
}

/*
================
idGameLocal::ReallocViewers
Allocate/Reallocate space for viewers
================
*/
template<typename T> static ID_INLINE void ReallocateZero( T *&var, int oldSize, int newSize ) {
	T *new_var = newSize ? new T[ newSize ] : NULL;
	const int copyCount = Min( newSize, oldSize );
	if ( copyCount > 0 ) {
		SIMDProcessor->Memcpy( new_var, var, sizeof( *new_var ) * copyCount );
	}
	delete[] var;
	var = new_var;
	if ( newSize > oldSize ) {
		SIMDProcessor->Memset( var + oldSize, 0, sizeof(*var) * (newSize - oldSize) );
	}
}

static ID_INLINE void ReallocateZeroClearInfo( viewer_t *&var, int oldSize, int newSize ) {
	int minBound = Min( newSize, oldSize ), i;
	viewer_t *new_var = newSize ? new viewer_t[ newSize ] : NULL;
	for (i = 0; i < minBound; i++ ) {
		new_var[ i ] = var[ i ];
	}
	for (i = 0; i < oldSize; i++ ) {
		var[ i ].info.Clear();
	}
	delete[] var;
	var = new_var;
	for (i = oldSize; i < newSize; i++) {
		var[i].connected = var[i].active = var[i].priv = var[i].nopvs = false;
		var[i].pvsArea = -1;
	}
}

void idGameLocal::ReallocViewers( int newMaxViewers ) {
	int i;

	if ( newMaxViewers == maxViewers ) {
		return;
	}

	if ( newMaxViewers > 0 ) { // zinx: during shutdown we clear everything, but there are still connected viewers.
		for ( i = newMaxViewers; i < maxViewers; i++ ) {
			if ( viewers[ i ].connected ) {
				// this should never happen
				Error( "Too many active viewers for ri_maxViewers change" );
			}
		}
		assert( maxViewer <= newMaxViewers );
	} else {
		maxViewer = 0;
	}

	ReallocateZeroClearInfo( viewers, maxViewers, newMaxViewers );
	ReallocateZero( viewerEntityStates, maxViewers, newMaxViewers );
	ReallocateZero( viewerPVS, maxViewers, newMaxViewers );
	ReallocateZero( viewerSnapshots, maxViewers, newMaxViewers );
	ReallocateZero( viewerUnreliableMessages, maxViewers, newMaxViewers );

	maxViewers = newMaxViewers;

	UpdateRepeaterInfo();
}

/*
================
idGameLocal::RepeaterAllowClient
clientId is the ID of the connecting client - can later be mapped to a clientNum by calling networkSystem->ServerGetClientNum( clientId )
================
*/
allowReply_t idGameLocal::RepeaterAllowClient( int clientId, int numClients, const char *IP, const char *guid, bool repeater, const char *password, const char *privatePassword, char reason[ MAX_STRING_CHARS ] ) {
	reason[0] = '\0';

	if ( ModerationRefuseBanned( IP ) ) {
		idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107239" );
		return ALLOW_NO;
	}

	if ( serverInfo.GetInt( "si_pure" ) != 0 && !mpGame.IsPureReady() ) {
		idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107139" );
		return ALLOW_NOTYET;
	}

	if ( !cvarSystem->GetCVarInteger( "ri_maxViewers" ) ) {
		idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_123000" );
		return ALLOW_NOTYET;
	}

	// completely full
	if ( numClients >= cvarSystem->GetCVarInteger( "ri_maxViewers" ) ) {
		idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_123000" );
		return ALLOW_NOTYET;
	}

	// check private clients
	// just in case somehow we have a stale private clientId that matches a new client
	privateViewerIds.Remove( clientId );
	nopvsViewerIds.Remove( clientId );

	const char *privatePass = cvarSystem->GetCVarString( "g_privateViewerPassword" );
	if ( privatePass[ 0 ] == '\0' && ri_privateViewers.GetInteger() > 0 ) {
		common->Warning( "idGameLocal::RepeaterAllowClient() - ri_privateViewers > 0 with no g_privateViewerPassword" );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "say ri_privateViewers is set but g_privateViewerPassword is empty" );
		idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107142" );
		return ALLOW_NOTYET;
	}

	int numPrivateClients = privateViewerIds.Num();

	// private clients that take up public slots are considered public clients
	numPrivateClients = idMath::ClampInt( 0, ri_privateViewers.GetInteger(), numPrivateClients );

	if ( repeater ) {
		const char *nopvsPass = cvarSystem->GetCVarString( "g_repeaterPassword" );

		if ( nopvsPass[ 0 ] != '\0' ) {
			if ( idStr::Cmp( nopvsPass, privatePassword ) ) {
				// password is set, and they don't match.
				idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107143" );
				Printf( "Rejecting viewer %s from IP %s: invalid password for repeater\n", guid, IP );
				return ALLOW_BADPASS;
			}

			// once this client spawns in, they'll be marked private
			privateViewerIds.Append( clientId );
		} else {
			if ( privatePass[ 0 ] != '\0' && !idStr::Cmp( privatePass, privatePassword ) ) {
				// once this client spawns in, they'll be marked private
				privateViewerIds.Append( clientId );
			}
		}

		// once this client spawns in, they'll be marked as a broadcaster
		nopvsViewerIds.Append( clientId );
	} else if ( privatePass[ 0 ] != '\0' && !idStr::Cmp( privatePass, privatePassword ) ) {
		// once this client spawns in, they'll be marked private
		privateViewerIds.Append( clientId );
	} else if( (numClients - numPrivateClients) >= ( cvarSystem->GetCVarInteger( "ri_maxViewers" ) - ri_privateViewers.GetInteger() ) ) {
		// if the number of public clients is greater than or equal to the number of public slots, require a private slot
		idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107141" );
		return ALLOW_NOTYET;
	}

	if ( !ri_useViewerPass.GetBool() ) {
		return ALLOW_YES;
	}

	const char *pass = cvarSystem->GetCVarString( "g_viewerPassword" );
	if ( pass[ 0 ] == '\0' ) {
		common->Warning( "ri_useViewerPass is set but g_viewerPassword is empty" );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "say ri_useViewerPass is set but g_viewerPassword is empty" );
		// avoids silent misconfigured state
		idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107142" );
		return ALLOW_NOTYET;
	}

	if ( !idStr::Cmp( pass, password ) ) {
		return ALLOW_YES;
	}

	idStr::snPrintf( reason, MAX_STRING_CHARS, "#str_107143" );
	Printf( "Rejecting viewer %s from IP %s: invalid password\n", guid, IP );
	return ALLOW_BADPASS;
}

/*
================
idGameLocal::ServerClientConnect
================
*/
void idGameLocal::ServerClientConnect( int clientNum, const char *guid ) {
	mpGame.ResetVoiceState( clientNum );
	// make sure no parasite entity is left
	if ( entities[ clientNum ] ) {
		common->DPrintf( "ServerClientConnect: remove old player entity\n" );
		delete entities[ clientNum ];
	}
	unreliableMessages[ clientNum ].Init( 0 );
	userInfo[ clientNum ].Clear();
	// openQ4: a slot reused by a new player must not inherit the previous
	// occupant's reliable message cooldowns
	ResetClientReliableFlood( clientNum );
	// openQ4 MP-18: the slot's address, and a text mute its address still carries
	ModerationClientConnect( clientNum );
	mpGame.ServerClientConnect( clientNum );
	// openQ4 MP-17: a new connection, or a new map, proves its identity again
	mpGame.IdentityClientConnect( clientNum );
	Printf( "client %d connected.\n", clientNum );
}

/*
================
idGameLocal::RepeaterClientConnect
================
*/
void idGameLocal::RepeaterClientConnect( int clientNum ) {
	// ensure that we have enough slots for this client
	assert( cvarSystem->GetCVarInteger( "ri_maxViewers" ) > clientNum );
	ReallocViewers( cvarSystem->GetCVarInteger( "ri_maxViewers" ) );

	viewers[ clientNum ].connected = true;
	viewers[ clientNum ].active = false;

	if ( maxViewer <= clientNum ) {
		maxViewer = clientNum + 1;
	}

	// mark them as private/nopvs
	for ( int i = 0; i < privateViewerIds.Num(); i++ ) {
		int num = networkSystem->RepeaterGetClientNum( privateViewerIds[ i ] );

		// check for timed out clientids
		if ( num < 0 || (num == clientNum && viewers[ clientNum ].priv) ) {
			privateViewerIds.RemoveIndex( i );
			i--;
			continue;
		}

		if ( num == clientNum ) {
			viewers[ clientNum ].priv = true;
			continue;
		}
	}

	for ( int i = 0; i < nopvsViewerIds.Num(); i++ ) {
		int num = networkSystem->RepeaterGetClientNum( nopvsViewerIds[ i ] );

		// check for timed out clientids
		if ( num < 0 || (num == clientNum && viewers[ clientNum ].nopvs) ) {
			nopvsViewerIds.RemoveIndex( i );
			i--;
			continue;
		}

		if ( num == clientNum ) {
			viewers[ clientNum ].nopvs = true;
			continue;
		}
	}

	Printf( "viewer %d connected%s.\n", clientNum, viewers[ clientNum ].nopvs ? " (repeater)" : "" );

	// count up viewer slots so players know when servers are full
	int numClients = 0;
	for ( int i = 0; i < maxViewer; i++ ) {
		if ( !viewers[ i ].connected ) {
			continue;
		}

		++numClients;
	}

	int numPrivateClients = privateViewerIds.Num();
	// private clients that take up public slots are considered public clients
	numPrivateClients = idMath::ClampInt( 0, ri_privateViewers.GetInteger(), numPrivateClients );

	ri_numViewers.SetInteger( numClients );
	ri_numPrivateViewers.SetInteger( numPrivateClients );

	UpdateRepeaterInfo();
}

/*
================
idGameLocal::ServerClientBegin
================
*/
void idGameLocal::ServerClientBegin( int clientNum, bool isBot, const char* botName ) {
	idBitMsg	outMsg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];

	// spawn the player
	SpawnPlayer( clientNum, isBot, botName );
	if ( clientNum == localClientNum ) {
		mpGame.EnterGame( clientNum );
	}

	// ddynerman: connect time
	((idPlayer*)entities[ clientNum ])->SetConnectTime( time );
	// openQ4 MP-17: ask for the player's identity on this server
	mpGame.IdentityClientBegin( clientNum, isBot );

	// send message to spawn the player at the clients
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.BeginWriting();
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_SPAWN_PLAYER );
	outMsg.WriteByte( clientNum );
	outMsg.WriteLong( spawnIds[ clientNum ] );
	networkSystem->ServerSendReliableMessage( -1, outMsg );

	if( gameType != GAME_TOURNEY ) {
		((idPlayer*)entities[ clientNum ])->JoinInstance( 0 );
	} else {
		// instance 0 might be empty in Tourney
		((idPlayer*)entities[ clientNum ])->JoinInstance( ((rvTourneyGameState*)gameLocal.mpGame.GetGameState())->GetNextActiveArena( 0 ) );
	}
//RAVEN BEGIN
//asalmon: This client has finish loading and will be spawned mark them as ready.
#ifdef _XENON
	Live()->ClientReady(clientNum);
#endif
//RAVEN END
}

/*
================
idGameLocal::RepeaterClientBegin
================
*/
void idGameLocal::RepeaterClientBegin( int clientNum ) {
	viewers[ clientNum ].active = true;
}

/*
================
idGameLocal::ServerClientDisconnect
clientNum == MAX_CLIENTS for cleanup of server demo recording data
================
*/
void idGameLocal::ServerClientDisconnect( int clientNum ) {
	if ( clientNum < MAX_CLIENTS ) mpGame.ResetVoiceState( clientNum );
	int			i;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];

	// openQ4 MP-18: first, while the engine's reason for this drop still stands and the bot
	// manager still knows the slot. The broadcast below can overflow another client's reliable
	// queue and drop it, which writes net_serverLastDrop again.
	if ( clientNum < MAX_CLIENTS ) {
		ModerationClientDisconnect( clientNum );
	}
	botManager.OnClientDisconnect( clientNum );

	if ( clientNum < MAX_CLIENTS ) {
		outMsg.Init( msgBuf, sizeof( msgBuf ) );
		outMsg.BeginWriting();
		outMsg.WriteByte( GAME_RELIABLE_MESSAGE_DELETE_ENT );
		outMsg.WriteBits( PackEntitySpawnId( spawnIds[ clientNum ], clientNum ), 32 ); // see GetSpawnId
		networkSystem->ServerSendReliableMessage( -1, outMsg );
	}

	// free snapshots stored for this client
	FreeSnapshotsOlderThanSequence( clientNum, 0x7FFFFFFF );

	// free entity states stored for this client
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		if ( clientEntityStates[ clientNum ][ i ] ) {
			entityStateAllocator.Free( clientEntityStates[ clientNum ][ i ] );
			clientEntityStates[ clientNum ][ i ] = NULL;
		}
	}

	// clear the client PVS
	memset( clientPVS[ clientNum ], 0, sizeof( clientPVS[ clientNum ] ) );

	if ( clientNum == MAX_CLIENTS ) {
		return;
	}

	// openQ4: drop this slot's reliable message cooldowns so the next player to
	// take the slot starts clean
	ResetClientReliableFlood( clientNum );
	// ...and its recorded positions, so a shot cannot be rewound onto the previous
	// occupant's 1024 ms of history.
	InvalidateMPLagCompensationHistory( clientNum );

	// only drop MP clients if we're in multiplayer and the server isn't going down
	if ( gameLocal.isMultiplayer && !(gameLocal.isListenServer && clientNum == gameLocal.localClientNum ) ) {
		// idMultiplayerGame::DisconnectClient will do the delete in MP
		mpGame.DisconnectClient( clientNum );
	} else {
		// delete the player entity
		delete entities[ clientNum ];
	}
	

}

/*
================
idGameLocal::RepeaterClientDisconnect
================
*/
void idGameLocal::RepeaterClientDisconnect( int clientNum ) {
	int			i;

	if ( clientNum >= maxViewers ) {
		// this can happen during shutdown, because repeater clients are
		// discarded when the (shared) entity states/snapshots of the other
		// clients are freed
		return;
	}

	// free snapshots stored for this client
	FreeSnapshotsOlderThanSequence( viewerSnapshots[ clientNum ], 0x7FFFFFFF );

	// free entity states stored for this client
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		if ( viewerEntityStates[ clientNum ][ i ] ) {
			entityStateAllocator.Free( viewerEntityStates[ clientNum ][ i ] );
			viewerEntityStates[ clientNum ][ i ] = NULL;
		}
	}

	// clear the client PVS
	memset( viewerPVS[ clientNum ], 0, sizeof( viewerPVS[ clientNum ] ) );

	viewerUnreliableMessages[ clientNum ].Init( 0 );

	viewers[ clientNum ].connected = false;
	viewers[ clientNum ].active = false;
	viewers[ clientNum ].priv = false;
	viewers[ clientNum ].nopvs = false;

	if ( maxViewer <= (clientNum+1) ) {
		// find the new highest client
		for ( maxViewer = clientNum; maxViewer >= 0 && !viewers[ maxViewer ].connected; maxViewer-- ) ;
		maxViewer++;
	}

	// remove them from private/nopvs lists
	for ( int i = 0; i < privateViewerIds.Num(); i++ ) {
		int num = networkSystem->RepeaterGetClientNum( privateViewerIds[ i ] );

		// check for timed out clientids
		if ( num < 0 ) {
			nopvsViewerIds.Remove( privateViewerIds[ i ] );
			privateViewerIds.RemoveIndex( i );
			i--;
			continue;
		}

		if ( num == clientNum ) {
			nopvsViewerIds.Remove( privateViewerIds[ i ] );
			privateViewerIds.RemoveIndex( i );
			i--;
			continue;
		}
	}

	// count up viewer slots so players know when servers are full
	int numClients = 0;
	for ( int i = 0; i < maxViewer; i++ ) {
		if ( !viewers[ i ].connected ) {
			continue;
		}

		++numClients;
	}

	int numPrivateClients = privateViewerIds.Num();
	// private clients that take up public slots are considered public clients
	numPrivateClients = idMath::ClampInt( 0, ri_privateViewers.GetInteger(), numPrivateClients );

	ri_numViewers.SetInteger( numClients );
	ri_numPrivateViewers.SetInteger( numPrivateClients );

	UpdateRepeaterInfo();
}

/*
================
idGameLocal::ServerWriteInitialReliableMessages

  Send reliable messages to initialize the client game up to a certain initial state.
================
*/
void idGameLocal::ServerWriteInitialReliableMessages( int clientNum ) {
	int			i;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];

	// spawn players
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( entities[i] == NULL || i == clientNum ) {
			continue;
		}
		outMsg.Init( msgBuf, sizeof( msgBuf ) );
		outMsg.BeginWriting( );
		outMsg.WriteByte( GAME_RELIABLE_MESSAGE_SPAWN_PLAYER );
		outMsg.WriteByte( i );
		outMsg.WriteLong( spawnIds[ i ] );
		networkSystem->ServerSendReliableMessage( clientNum, outMsg );
	}

	// The owner already has a freshly initialized full reservoir; each
	// existing remote player supplies its authoritative HUD baseline after
	// the spawn messages, using the same optional event as live updates.
	for ( i = 0; i < MAX_CLIENTS; ++i ) {
		if ( i == clientNum || !entities[i] || !entities[i]->IsType( idPlayer::GetClassType() ) ) { continue; }
		outMsg.Init( msgBuf, sizeof( msgBuf ) );
		static_cast<idPlayer *>( entities[i] )->WriteAirReadoutMessage( outMsg, true );
		networkSystem->ServerSendReliableMessage( clientNum, outMsg );
	}

	// update portals for opened doors
	int numPortals = gameRenderWorld->NumPortals();
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.BeginWriting();
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_PORTALSTATES );
	outMsg.WriteLong( numPortals );
	for ( i = 0; i < numPortals; i++ ) {
		outMsg.WriteBits( gameRenderWorld->GetPortalState( (qhandle_t) (i+1) ) , NUM_RENDER_PORTAL_BITS );
	}
	networkSystem->ServerSendReliableMessage( clientNum, outMsg );

	mpGame.ServerWriteInitialReliableMessages( serverReliableSender.To( clientNum, true ), clientNum );
}

/*
================
idGameLocal::RepeaterWriteInitialReliableMessages

  Send reliable messages to initialize the client game up to a certain initial state.
================
*/
void idGameLocal::RepeaterWriteInitialReliableMessages( int clientNum ) {
	int			i;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];

	// spawn players
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( entities[i] == NULL ) {
			continue;
		}
		outMsg.Init( msgBuf, sizeof( msgBuf ) );
		outMsg.BeginWriting( );
		outMsg.WriteByte( GAME_RELIABLE_MESSAGE_SPAWN_PLAYER );
		outMsg.WriteByte( i );
		outMsg.WriteLong( spawnIds[ i ] );
// jmarshall - engine has no repeater reliable channel
		//networkSystem->RepeaterSendReliableMessage( clientNum, outMsg );
// jmarshall end
	}

	// update portals for opened doors
	int numPortals = gameRenderWorld->NumPortals();
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.BeginWriting();
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_PORTALSTATES );
	outMsg.WriteLong( numPortals );
	for ( i = 0; i < numPortals; i++ ) {
		outMsg.WriteBits( gameRenderWorld->GetPortalState( (qhandle_t) (i+1) ) , NUM_RENDER_PORTAL_BITS );
	}
// jmarshall - engine has no repeater reliable channel
	//networkSystem->RepeaterSendReliableMessage( clientNum, outMsg );
// jmarshall end

	mpGame.ServerWriteInitialReliableMessages( repeaterReliableSender.To( clientNum ), ENTITYNUM_NONE );
}

/*
================
idGameLocal::FreeSnapshotsOlderThanSequence
================
*/
void idGameLocal::FreeSnapshotsOlderThanSequence( snapshot_t *&clientSnapshot, int sequence ) {
	snapshot_t *snapshot, *lastSnapshot, *nextSnapshot;
	entityState_t *state;

	for ( lastSnapshot = NULL, snapshot = clientSnapshot; snapshot; snapshot = nextSnapshot ) {
		nextSnapshot = snapshot->next;
		if ( snapshot->sequence < sequence ) {
			for ( state = snapshot->firstEntityState; state; state = snapshot->firstEntityState ) {
				snapshot->firstEntityState = snapshot->firstEntityState->next;
				entityStateAllocator.Free( state );
			}
			if ( lastSnapshot ) {
				lastSnapshot->next = snapshot->next;
			} else {
				clientSnapshot = snapshot->next;
			}
			snapshotAllocator.Free( snapshot );
		} else {
			lastSnapshot = snapshot;
		}
	}
}

/*
================
idGameLocal::FreeSnapshotsOlderThanSequence
================
*/
void idGameLocal::FreeSnapshotsOlderThanSequence( int clientNum, int sequence ) {
	FreeSnapshotsOlderThanSequence( clientSnapshots[ clientNum ], sequence );
}

/*
================
idGameLocal::ApplySnapshot
================
*/
bool idGameLocal::ApplySnapshot( snapshot_t *&clientSnapshot, entityState_t *entityStates[MAX_GENTITIES], int PVS[ENTITY_PVS_SIZE], int sequence ) {
	snapshot_t *snapshot, *lastSnapshot, *nextSnapshot;
	entityState_t *state;

	FreeSnapshotsOlderThanSequence( clientSnapshot, sequence );

	for ( lastSnapshot = NULL, snapshot = clientSnapshot; snapshot; snapshot = nextSnapshot ) {
		nextSnapshot = snapshot->next;
		if ( snapshot->sequence == sequence ) {
			for ( state = snapshot->firstEntityState; state; state = state->next ) {
				if ( entityStates[state->entityNumber] ) {
					entityStateAllocator.Free( entityStates[state->entityNumber] );
				}
				entityStates[state->entityNumber] = state;
			}
			// ~512 bytes
			memcpy( PVS, snapshot->pvs, sizeof( snapshot->pvs ) );
			if ( lastSnapshot ) {
				lastSnapshot->next = nextSnapshot;
			} else {
				clientSnapshot = nextSnapshot;
			}
			snapshotAllocator.Free( snapshot );
			return true;
		} else {
			lastSnapshot = snapshot;
		}
	}

	return false;
}

/*
================
idGameLocal::ApplySnapshot
================
*/
bool idGameLocal::ApplySnapshot( int clientNum, int sequence ) {
	return ApplySnapshot( clientSnapshots[ clientNum ], clientEntityStates[ clientNum ], clientPVS[ clientNum ], sequence );
}

/*
================
idGameLocal::WriteGameStateToSnapshot
================
*/
void idGameLocal::WriteGameStateToSnapshot( idBitMsgDelta &msg ) const {
	int i;

	for( i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ ) {
		msg.WriteFloat( globalShaderParms[i] );
	}

	mpGame.WriteToSnapshot( msg );
}

/*
================
idGameLocal::ReadGameStateFromSnapshot
================
*/
void idGameLocal::ReadGameStateFromSnapshot( const idBitMsgDelta &msg ) {
	int i;

	for( i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ ) {
		globalShaderParms[i] = msg.ReadFloat();
	}

	mpGame.ReadFromSnapshot( msg );
}

/*
================
idGameLocal::WriteSnapshot
Write a snapshot of the current game state for the given client.
================
*/
/*
================
idGameLocal::SnapshotExemptFromPVS

An outline drawn wherever a player is needs that player replicated, and the PVS
gate is what would otherwise stop it. Only the players the recipient is already
entitled to see are exempted:

  - every player, when the recipient is spectating. A spectator is watching the
    whole match; that is what spectating is.
  - players on the recipient's own team. You are allowed to know where your allies
    are, and a team outline is only that knowledge drawn.

Opponents of a live player are deliberately not exempt. Sending every client the
position of every enemy would turn a wallhack into a matter of reading packets
rather than of beating the game, and this gate is the only thing preventing that.
So enemy outlines stay bounded by what the player could actually see.
================
*/
bool idGameLocal::SnapshotExemptFromPVS( const idEntity *ent, bool exemptAllPlayers, int exemptTeam ) const {
	if ( ent == NULL || ent->entityNumber >= MAX_CLIENTS || !ent->IsType( idPlayer::GetClassType() ) ) {
		return false;
	}

	const idPlayer *other = static_cast<const idPlayer *>( ent );
	// Nothing to mark, so nothing worth the bandwidth.
	if ( other->spectating || other->health <= 0 ) {
		return false;
	}
	if ( exemptAllPlayers ) {
		return true;
	}

	return IsTeamGame() && exemptTeam != TEAM_NONE && other->team == exemptTeam;
}

void idGameLocal::WriteSnapshot( snapshot_t *&clientSnapshot, entityState_t *entityStates[MAX_GENTITIES], int PVS[ENTITY_PVS_SIZE], idMsgQueue &unreliable, int sequence, idBitMsg &msg, int transmitEntity, int transmitEntity2, int instance, bool doPVS, const idBounds &pvs_bounds, int lastSnapshotFrame, bool pvsExemptAllPlayers, int pvsExemptTeam, bool writeEntityInstances ) {
	int i, msgSize, msgWriteBit;
	idEntity *ent;
	pvsHandle_t pvsHandle = { 0, 0 };	// shut warning
	idBitMsgDelta deltaMsg;
	snapshot_t *snapshot;
	entityState_t *base, *newBase;
	int numSourceAreas, sourceAreas[ idEntity::MAX_PVS_AREAS ];
	int pvsResendMask[ENTITY_PVS_SIZE];

	// free too old snapshots
	// ( that's a security, normal acking from server keeps a smaller backlog of snaps )
	FreeSnapshotsOlderThanSequence( clientSnapshot, sequence - 64 );

	// allocate new snapshot
	snapshot = snapshotAllocator.Alloc();
	snapshot->sequence = sequence;
	snapshot->firstEntityState = NULL;
	snapshot->next = clientSnapshot;
	clientSnapshot = snapshot;
	memset( snapshot->pvs, 0, sizeof( snapshot->pvs ) );

	// openQ4: collect the entities that have dropped out of this recipient's PVS at or since the
	// snapshot its delta baseline is built from. PVS holds the bits of the last acknowledged
	// snapshot ( ApplySnapshot copies them there as it installs that snapshot's entity states ),
	// and the snapshots still on the list are the ones sent since, none of which are acknowledged
	// yet. An entity flagged here has to be written in full the next time it is in PVS, see the
	// rollback below, and stays flagged until an acknowledged snapshot carries it again - which is
	// the same retry-until-acked rule the delta baselines themselves follow.
	for ( i = 0; i < ENTITY_PVS_SIZE; i++ ) {
		pvsResendMask[i] = ~PVS[i];
	}
	for ( const snapshot_t *sent = snapshot->next; sent != NULL; sent = sent->next ) {
		for ( i = 0; i < ENTITY_PVS_SIZE; i++ ) {
			pvsResendMask[i] |= ~sent->pvs[i];
		}
	}

	if ( doPVS ) {
		// get PVS for this player
		// don't use PVSAreas for networking - PVSAreas depends on animations (and md5 bounds), which are not synchronized
		numSourceAreas = gameRenderWorld->BoundsInAreas( pvs_bounds, sourceAreas, idEntity::MAX_PVS_AREAS );
		pvsHandle = gameLocal.pvs.SetupCurrentPVS( sourceAreas, numSourceAreas, PVS_NORMAL );
	}

#if ASYNC_WRITE_TAGS
	// openQ4: the seed came from the gameplay random stream, so every snapshot written
	// for every client moved the numbers the match itself drew next
	static unsigned int snapshotTagSeed = 0x2545F491u;
	snapshotTagSeed = snapshotTagSeed * 1664525u + 1013904223u;
	idRandom tagRandom;
	tagRandom.SetSeed( static_cast<int>( snapshotTagSeed >> 1 ) );
	msg.WriteLong( tagRandom.GetSeed() );
#endif
	
	// write unreliable messages
	unreliable.WriteTo( msg ); // NOTE: No flush

#if ASYNC_WRITE_TAGS
	msg.WriteLong( tagRandom.RandomInt() );
#endif

	// create the snapshot
	for( ent = spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {	
		// local fake client
		if ( ent->entityNumber == ENTITYNUM_NONE ) {
			continue;
		}

		// openQ4: an entity that is neither a map entity nor networked leaves no
		// mark below whatever the PVS says; skip it before the PVS test, which
		// ran for every spawned entity once a recipient a snapshot.
		if ( !ent->fl.networkSync && !isMapEntity[ ent->entityNumber ] ) {
			continue;
		}

		// if the entity is not in the player PVS
		if ( doPVS && !ent->PhysicsTeamInPVS( pvsHandle ) && ent->entityNumber != transmitEntity && ent->entityNumber != transmitEntity2 ) {
			// A player this recipient may see wherever they are still has to reach
			// them, or no renderer change could draw an outline for someone the
			// client was never told about. Everything that survives this is marked
			// in snapshot->pvs below like any other entity, so the client treats it
			// as present rather than as something that just popped in.
			if ( !SnapshotExemptFromPVS( ent, pvsExemptAllPlayers, pvsExemptTeam ) ) {
				continue;
			}
		}

		if ( !writeEntityInstances && ent->GetInstance() != instance ) {
			continue;
		}

		// if the entity is a map entity, mark it in PVS
		if ( isMapEntity[ ent->entityNumber ] ) {
			snapshot->pvs[ ent->entityNumber >> 5 ] |= 1 << ( ent->entityNumber & 31 );
		}

		// if that entity is not marked for network synchronization
		if ( !ent->fl.networkSync ) {
			continue;
		}

		// add the entity to the snapshot PVS
		snapshot->pvs[ ent->entityNumber >> 5 ] |= 1 << ( ent->entityNumber & 31 );

		// openQ4: an entity that left this recipient's PVS and is back has to be written even when
		// nothing in its state changed while it was away. Clients destroy entities whose
		// ClientStale() returns true - idProjectile is one, and it expects to be "re-spawned" when
		// it pops back into PVS - so rolling it back out of the message here leaves the client with
		// no entity, and nothing but a state change would ever bring it back. A rocket keeps
		// writing the same launch time, origin and direction for its whole flight, so it would stay
		// invisible until it explodes inside the PVS.
		// Note this forces a write instead of dropping the delta baseline: idBitMsgDelta lays the
		// bits out differently when its base is NULL, so writer and reader have to agree on base
		// nullity or the entire snapshot decodes as garbage. Whether an entity block is present is
		// self describing on the wire, so forcing one needs no agreement with the reader at all.
		const bool leftPVSSinceAck = ( pvsResendMask[ ent->entityNumber >> 5 ] & ( 1 << ( ent->entityNumber & 31 ) ) ) != 0;

		// save the write state to which we can revert when the entity didn't change at all
		msg.SaveWriteState( msgSize, msgWriteBit );

		// write the entity to the snapshot
		msg.WriteBits( ent->entityNumber, GENTITYNUM_BITS );

		base = entityStates[ent->entityNumber];
		if ( base ) {
			base->state.BeginReading();
		}
		newBase = entityStateAllocator.Alloc();
		newBase->entityNumber = ent->entityNumber;
		newBase->state.Init( newBase->stateBuf, sizeof( newBase->stateBuf ) );
		newBase->state.BeginWriting();

		deltaMsg.InitWriting( base ? &base->state : NULL, &newBase->state, &msg );

		deltaMsg.WriteBits( spawnIds[ ent->entityNumber ], 32 - GENTITYNUM_BITS );
		assert( ent->entityDefNumber > 0 );
		deltaMsg.WriteBits( ent->entityDefNumber, entityDefBits );
		if ( writeEntityInstances ) {
			assert( ent->GetInstance() >= 0 && ent->GetInstance() < MAX_INSTANCES );
			deltaMsg.WriteBits( idMath::ClampInt( 0, MAX_INSTANCES - 1, ent->GetInstance() ), ASYNC_PLAYER_INSTANCE_BITS );
		}

		// write the class specific data to the snapshot
		ent->WriteToSnapshot( deltaMsg );

		// if this is a player and a transmit of the player state has been requested, write the player state
		if ( ent->entityNumber < MAX_CLIENTS ) {
			if ( transmitEntity < 0 || transmitEntity == ent->entityNumber || transmitEntity2 == ent->entityNumber ) {
				assert( ent->IsType( idPlayer::GetClassType() ) );
				deltaMsg.WriteBits( 1, 1 );
				static_cast< idPlayer * >( ent )->WritePlayerStateToSnapshot( lastSnapshotFrame, deltaMsg );
			} else {
				deltaMsg.WriteBits( 0, 1 );
			}
		}

		if ( !leftPVSSinceAck && !deltaMsg.HasChanged() ) {
			msg.RestoreWriteState( msgSize, msgWriteBit );
			entityStateAllocator.Free( newBase );
		} else {
			newBase->next = snapshot->firstEntityState;
			snapshot->firstEntityState = newBase;

#if ASYNC_WRITE_TAGS
			// openQ4: one byte tells a write and a read that fell out of step apart as
			// well as four did, for every entity written (protocol 2.42)
			msg.WriteBits( tagRandom.RandomInt() & SNAPSHOT_ENTITY_TAG_MASK, SNAPSHOT_ENTITY_TAG_BITS );
#endif
		}
	}

	msg.WriteBits( ENTITYNUM_NONE, GENTITYNUM_BITS );

	// write the PVS to the snapshot
#if ASYNC_WRITE_PVS
	if ( doPVS ) {
		for ( i = 0; i < idEntity::MAX_PVS_AREAS; i++ ) {
			if ( i < numSourceAreas ) {
				msg.WriteLong( sourceAreas[ i ] );
			} else {
				msg.WriteLong( 0 );
			}
		}
		gameLocal.pvs.WritePVS( pvsHandle, msg );
	}
#endif
	// always write the entity pvs, even when doPVS isn't set
	for ( i = 0; i < ENTITY_PVS_SIZE; i++ ) {
		msg.WriteDeltaLong( PVS[i], snapshot->pvs[i] );
	}

	if ( doPVS ) {
		// free the PVS
		pvs.FreeCurrentPVS( pvsHandle );
	}

	// write the game state to the snapshot
	base = entityStates[ENTITYNUM_NONE];	// ENTITYNUM_NONE is used for the game state
	if ( base ) {
		base->state.BeginReading();
	}
	newBase = entityStateAllocator.Alloc();
	newBase->entityNumber = ENTITYNUM_NONE;
	newBase->next = snapshot->firstEntityState;
	snapshot->firstEntityState = newBase;
	newBase->state.Init( newBase->stateBuf, sizeof( newBase->stateBuf ) );
	newBase->state.BeginWriting();
	deltaMsg.InitWriting( base ? &base->state : NULL, &newBase->state, &msg );

	WriteGameStateToSnapshot( deltaMsg );
}

/*
================
idGameLocal::ServerWriteSnapshot
Write a snapshot of the current game state for the given client.
================
*/
void idGameLocal::ServerWriteSnapshot( int clientNum, int sequence, idBitMsg &msg, dword *clientInPVS, int numPVSClients, int lastSnapshotFrame ) {

	int i;
	idPlayer *player, *spectated = NULL;

	player = static_cast<idPlayer *>( entities[ clientNum ] );
	assert( player );

	if ( player->spectating && player->spectator != clientNum && entities[ player->spectator ] ) {
		spectated = static_cast< idPlayer * >( entities[ player->spectator ] );
	} else {
		spectated = player;
	}

	// Which players this client may see wherever they stand. A spectator watches
	// the whole match; a live player is entitled to its own team. The team is taken
	// from the player being spectated when there is one, so a follow spectator sees
	// exactly what the player it follows would.
	// A spectator is someone who chose to watch (wantSpectate). The spectating flag
	// alone is also an eliminated team member waiting for the round to end, and
	// that one was sent every living opponent's position, health and armour while
	// still able to talk to the team mates who were playing on.
	const bool pvsExemptAllPlayers = player->spectating && player->wantSpectate;
	const int pvsExemptTeam = ( !pvsExemptAllPlayers && IsTeamGame() ) ? spectated->team : TEAM_NONE;

	WriteSnapshot(
		clientSnapshots[ clientNum ],
		clientEntityStates[ clientNum ],
		clientPVS[ clientNum ],
		unreliableMessages[ clientNum ],
		sequence,
		msg,
		spectated->entityNumber,
		clientNum,
		player->GetInstance(),
		true,
		spectated->GetPlayerPhysics()->GetAbsBounds(),
		lastSnapshotFrame,
		pvsExemptAllPlayers,
		pvsExemptTeam
	);

	unreliableMessages[ clientNum ].Init( 0 ); // Flush

	// copy the client PVS string
// RAVEN BEGIN
// JSinger: Changed to call optimized memcpy
// jnewquist: Use dword array to match pvs array so we don't have endianness problems.
	const int numDwords = ( numPVSClients + 31 ) >> 5;
	for ( i = 0; i < numDwords; i++ ) {
		clientInPVS[i] = clientSnapshots[ clientNum ]->pvs[i];
	}
// RAVEN END
}

/*
===============
idGameLocal::ServerWriteServerDemoSnapshot
===============
*/
void idGameLocal::ServerWriteServerDemoSnapshot( int sequence, idBitMsg &msg, int lastSnapshotFrame, bool fullWorldInstances ) {
	// Bootstrap every POV even when recording starts between air updates.
	// ReadNetworkInfo has already spawned the recorded player identities before
	// this first snapshot's queued events are consumed.
	if ( sequence == 1 ) {
		for ( int i = 0; i < MAX_CLIENTS; ++i ) {
			if ( !entities[i] || !entities[i]->IsType( idPlayer::GetClassType() ) ) { continue; }
			byte bytes[32]; idBitMsg air;
			air.Init( bytes, sizeof( bytes ) );
			static_cast<idPlayer *>( entities[i] )->WriteAirReadoutMessage( air, false );
			byte routeBytes[2]; idBitMsg route;
			route.Init( routeBytes, sizeof( routeBytes ) );
			route.WriteByte( GAME_UNRELIABLE_RECORD_CLIENTNUM );
			route.WriteChar( -1 );
			if ( !unreliableMessages[MAX_CLIENTS].AddConcat( route.GetData(), route.GetSize(),
					air.GetData(), air.GetSize(), false ) ) {
				Warning( "MVD initial air readout batch is full" );
			}
		}
	}
	serverDemoInstances = fullWorldInstances;
	bool ret = ApplySnapshot( clientSnapshots[ MAX_CLIENTS ], clientEntityStates[ MAX_CLIENTS ], clientPVS[ MAX_CLIENTS ], sequence - 1 );
	ret = ret;	// shut the warning
	assert( ret || sequence == 1 );	// past the first snapshot of the server demo stream, there's always exactly one to clear

	idBounds dummy_bounds;

	WriteSnapshot(
		clientSnapshots[ MAX_CLIENTS ],
		clientEntityStates[ MAX_CLIENTS ],
		clientPVS[ MAX_CLIENTS ],
		unreliableMessages[ MAX_CLIENTS ],
		sequence,
		msg,
		-1,
		-1,
		0,
		false,
		dummy_bounds,
		lastSnapshotFrame,
		false,
		TEAM_NONE,
		fullWorldInstances
	);

	unreliableMessages[ MAX_CLIENTS ].Init( 0 ); // Flush
}

/*
===============
idGameLocal::RepeaterWriteSnapshot
===============
*/
void idGameLocal::RepeaterWriteSnapshot( int clientNum, int sequence, idBitMsg &msg, dword *clientInPVS, int numPVSClients, const userOrigin_t &pvs_origin, int lastSnapshotFrame ) {
	int i;
	idBounds bounds;

	// if the client is spectating a valid player
	if ( pvs_origin.followClient >= 0 && pvs_origin.followClient < MAX_CLIENTS && entities[ pvs_origin.followClient ] && entities[ pvs_origin.followClient ]->IsType( idPlayer::GetClassType() ) ) {
		idPlayer *spectated = static_cast< idPlayer * >( entities[ pvs_origin.followClient ] );
		bounds = spectated->GetPlayerPhysics()->GetAbsBounds();
	} else {
		// zinx - FIXME - bounds, not point
		bounds = idBounds( pvs_origin.origin );
	}

	WriteSnapshot(
		viewerSnapshots[ clientNum ],
		viewerEntityStates[ clientNum ],
		viewerPVS[ clientNum ],
		viewerUnreliableMessages[ clientNum ],
		sequence,
		msg,
		viewers[ clientNum ].nopvs ? -1 : (pvs_origin.followClient >= 0 ? pvs_origin.followClient : ENTITYNUM_NONE ),
		ENTITYNUM_NONE,
		0,
		viewers[ clientNum ].nopvs ? false : true,
		bounds,
		lastSnapshotFrame,
		// A repeater viewer is a spectator by construction - it plays no part in the
		// match - so it may see every player wherever they are.
		true,
		TEAM_NONE
	);

	viewerUnreliableMessages[ clientNum ].Init( 0 ); // Flush

	// copy the client PVS string
// RAVEN BEGIN
// JSinger: Changed to call optimized memcpy
// jnewquist: Use dword array to match pvs array so we don't have endianness problems.
	const int numDwords = ( numPVSClients + 31 ) >> 5;
	for ( i = 0; i < numDwords; i++ ) {
		clientInPVS[i] = viewerSnapshots[ clientNum ]->pvs[i];
	}
// RAVEN END
}

/*
===============
idGameLocal::RepeaterEndSnapshots
===============
*/
void idGameLocal::RepeaterEndSnapshots( void ) {
}

/*
================
idGameLocal::ServerApplySnapshot
================
*/
bool idGameLocal::ServerApplySnapshot( int clientNum, int sequence ) {
	return ApplySnapshot( clientNum, sequence );
}

/*
================
idGameLocal::RepeaterApplySnapshot
================
*/
bool idGameLocal::RepeaterApplySnapshot( int clientNum, int sequence ) {
	return ApplySnapshot( viewerSnapshots[ clientNum ], viewerEntityStates[ clientNum ], viewerPVS[ clientNum ], sequence );
}

/*
================
idGameLocal::NetworkEventWarning
================
*/
void idGameLocal::NetworkEventWarning( const entityNetEvent_t *event, const char *fmt, ... ) {
	char buf[1024];
	int length = 0;
	va_list argptr;

	int entityNum	= event->spawnId & ( ( 1 << GENTITYNUM_BITS ) - 1 );
	int id			= UnpackEntitySpawnId( event->spawnId );

	length += idStr::snPrintf( buf+length, sizeof(buf)-1-length, "event %d for entity %d %d: ", event->event, entityNum, id );
	va_start( argptr, fmt );
	length = idStr::vsnPrintf( buf+length, sizeof(buf)-1-length, fmt, argptr );
	va_end( argptr );
	idStr::Append( buf, sizeof(buf), "\n" );

	common->DWarning( "%s", buf );
}

/*
================
idGameLocal::CountQueuedClientEvents
openQ4: how many events the given client already has pending in the shared
event queue.  The queue is server wide, so a sender that can put an unbounded
number of events in it starves every other client.
================
*/
int idGameLocal::CountQueuedClientEvents( int clientNum ) {
	int count = 0;

	for ( const entityNetEvent_t *event = eventQueue.Start(); event; event = event->next ) {
		if ( event->sender == clientNum ) {
			count++;
		}
	}

	return count;
}

/*
================
idGameLocal::CheckClientReliableFlood
openQ4: returns true when the client may send another message of this flood
class right now, stamping the time as it does so.  A false result means the
message must be dropped outright - queueing it for later would only delay the
flood rather than stop it.
================
*/
bool idGameLocal::CheckClientReliableFlood( int clientNum, int floodClass, int delayMS ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		// server generated traffic, or the repeater; not rate limited
		return true;
	}
	if ( floodClass < 0 || floodClass >= RELIABLE_FLOOD_NUM ) {
		return true;
	}

	const int last = clientReliableFloodTime[ clientNum ][ floodClass ];

	// a map restart winds gameLocal.time back, so a stamp in the future is
	// stale rather than a cooldown - only enforce while it is in the past
	if ( time >= last && time - last < delayMS ) {
		return false;
	}

	clientReliableFloodTime[ clientNum ][ floodClass ] = time;
	return true;
}

/*
================
idGameLocal::ResetClientReliableFlood
openQ4: clear a client slot's flood cooldowns so a slot reused by a new player
does not inherit the previous occupant's throttle.
================
*/
void idGameLocal::ResetClientReliableFlood( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}

	for ( int i = 0; i < RELIABLE_FLOOD_NUM; i++ ) {
		clientReliableFloodTime[ clientNum ][ i ] = RELIABLE_FLOOD_NEVER;
	}
}

/*
================
idGameLocal::ServerProcessEntityNetworkEventQueue
================
*/
void idGameLocal::ServerProcessEntityNetworkEventQueue( void ) {
	idEntity			*ent;
	entityNetEvent_t	*event;
	idBitMsg			eventMsg;

	while ( eventQueue.Start() ) {
		event = eventQueue.Start();

		if ( event->time > time ) {
			break;
		}

		idEntityPtr< idEntity > entPtr;
			
		if ( !entPtr.SetSpawnId( event->spawnId ) ) {
			NetworkEventWarning( event, "Entity does not exist any longer, or has not been spawned yet." );
		// openQ4: SetSpawnId only compares the spawn counter, and a freed slot
		// holds -1, so a forged id with every high bit set resolves "successfully"
		// to a NULL entity.  Reject that here rather than letting the dispatch
		// below dereference it - the assert is compiled out of a release build.
		} else if ( entPtr.GetEntity() == NULL ) {
			NetworkEventWarning( event, "Entity does not exist any longer, or has not been spawned yet." );
		// openQ4: the spawnId came off the wire.  Every client->server event is
		// sent by an entity for itself - idEntity::ClientSendEvent always writes
		// gameLocal.GetSpawnId( this ), and player input, emote and spectator
		// follow requests all name that player - so any other entity is
		// a forged spawn id.  Without this check a modified client can drive
		// PerformImpulse on any other player.
		} else if ( event->sender >= 0 && ( event->sender >= MAX_CLIENTS || entPtr.GetEntity() != entities[ event->sender ] ) ) {
			NetworkEventWarning( event, "client %d sent an event for an entity it does not own", event->sender );
		} else {
			ent = entPtr.GetEntity();
			assert( ent );

			eventMsg.Init( event->paramsBuf, sizeof( event->paramsBuf ) );
			eventMsg.SetSize( event->paramsSize );
			eventMsg.BeginReading();
			if ( !ent->ServerReceiveEvent( event->event, event->time, eventMsg ) ) {
				NetworkEventWarning( event, "unknown event" );
			}
		}

#ifdef _DEBUG
		entityNetEvent_t* freedEvent = eventQueue.Dequeue();
		assert( freedEvent == event );
#else
		eventQueue.Dequeue();
#endif
		eventQueue.Free( event );
	}
}

/*
================
idGameLocal::ServerSendChatMessage
================
*/
void idGameLocal::ServerSendChatMessage( int to, const char *name, const char *text, const char *parm ) {
	idBitMsg outMsg;
	byte msgBuf[ MAX_GAME_MESSAGE_SIZE ];

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.BeginWriting();
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_CHAT );
	outMsg.WriteString( name );
	outMsg.WriteString( text );
	outMsg.WriteString( parm );
	networkSystem->ServerSendReliableMessage( to, outMsg );

	if ( to == -1 || to == localClientNum ) {
		idStr temp = va( "%s%s", common->GetLocalizedString( text ), parm );
		mpGame.AddChatLine( "%s^0: %s", name, temp.c_str() );
	}
}

/*
================
idGameLocal::ServerProcessReliableMessage
================
*/
void idGameLocal::ServerProcessReliableMessage( int clientNum, const idBitMsg &msg ) {
	int id;

	id = msg.ReadByte();
	switch( id ) {
		case GAME_RELIABLE_MESSAGE_CHAT:
		case GAME_RELIABLE_MESSAGE_TCHAT: {
			char name[128];
			char text[128];
			char parm[128];

			// openQ4: every accepted chat line is rebroadcast to all clients, so
			// an unthrottled sender fills the other clients' reliable queues and
			// the engine disconnects the victims.  Drop, do not queue.
			if ( !CheckClientReliableFlood( clientNum,
				( id == GAME_RELIABLE_MESSAGE_TCHAT ) ? RELIABLE_FLOOD_TCHAT : RELIABLE_FLOOD_CHAT,
				RELIABLE_FLOOD_CHAT_DELAY ) ) {
				break;
			}

			msg.ReadString( name, sizeof( name ) );
			msg.ReadString( text, sizeof( text ) );
			// This parameter is ignored - it is only used when going to client from server
			msg.ReadString( parm, sizeof( parm ) );

			// openQ4 MP-18: a muted sender, or one past the flood window, reaches nobody
			if ( !ModerationAllowChat( clientNum, true ) ) {
				break;
			}

			// openQ4: the display name arrived on the sender's own packet and is
			// formatted straight into the broadcast, so a modified client could
			// impersonate another player or the server.  Use the server's copy of
			// the sender's userinfo instead of anything it told us.
			if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
				const char *serverName = userInfo[ clientNum ].GetString( "ui_name" );
				idStr::Copynz( name, ( serverName && *serverName ) ? serverName : "player", sizeof( name ) );
			}

			mpGame.ProcessChatMessage( clientNum, id == GAME_RELIABLE_MESSAGE_TCHAT, name, text, NULL, true );
			break;
		}
		case GAME_RELIABLE_MESSAGE_VCHAT: {
			// openQ4: voice chat is rebroadcast the same way chat is
			if ( !CheckClientReliableFlood( clientNum, RELIABLE_FLOOD_VCHAT, RELIABLE_FLOOD_VCHAT_DELAY ) ) {
				break;
			}

			int index = msg.ReadLong();
			bool team = msg.ReadBits( 1 ) != 0;
			// openQ4 MP-18: a voice-menu line is a chat line with a sound
			if ( !ModerationAllowChat( clientNum, true ) ) {
				break;
			}
			mpGame.ProcessVoiceChat( clientNum, team, index );
			break;
		}
		case GAME_RELIABLE_MESSAGE_KILL: {
			mpGame.WantKilled( clientNum );
			break;
		}
		case GAME_RELIABLE_MESSAGE_DROPWEAPON: {
			// openQ4: no client offers this (clientDropWeapon is not registered), so it
			// only comes from a modified one. The throw it asks for leaves a weapon item
			// holding a full starting load of ammunition, once for every message sent
			// while the weapon lowers: a refill for the sender, and enough items to run
			// the map out of entities. A weapon is dropped when its carrier dies.
			common->DPrintf( "client %d: ignored a drop weapon request\n", clientNum );
			break;
		}
		case GAME_RELIABLE_MESSAGE_CALLVOTE: {
			mpGame.ServerCallVote( clientNum, msg );
			break;
		}
		case GAME_RELIABLE_MESSAGE_CASTVOTE: {
			if ( msg.GetRemainingReadBits() != 8 ) {
				Warning( "Ignoring malformed cast-vote message from client %d", clientNum );
				break;
			}
			const int voteValue = msg.ReadByte();
			if ( voteValue != 0 && voteValue != 1 ) {
				Warning( "Ignoring invalid cast-vote value %d from client %d", voteValue, clientNum );
				break;
			}
			mpGame.CastVote( clientNum, voteValue != 0 );
			break;
		}
		case GAME_RELIABLE_MESSAGE_CALLPACKEDVOTE: {
			mpGame.ServerCallPackedVote( clientNum, msg );
			break;
		}
#if 0
		// uncomment this if you want to track when players are in a menu
		case GAME_RELIABLE_MESSAGE_MENU: {
			bool menuUp = ( msg.ReadBits( 1 ) != 0 );
			break;
		}
#endif
		case GAME_RELIABLE_MESSAGE_EVENT: {
			entityNetEvent_t *event;

			// allocate new event
			event = eventQueue.Alloc();

			event->spawnId = msg.ReadBits( 32 );
			event->event = msg.ReadByte();
			// openQ4: the wire carries the sender's own gameLocal.time here, and
			// that value is the ordering key of the server wide event queue -
			// a client sending 0 makes Enqueue's OUTOFORDER_DROP delete everyone
			// else's queued events, and 0x7FFFFFFF parks an event at the head
			// that the drain loop never passes.  Read it to keep the wire format
			// intact, then discard it and stamp the arrival time server side.
			msg.ReadLong();
			event->time = time;
			// openQ4: remember who claimed this spawnId so the drain loop can
			// prove the event targets the sender's own entity.
			event->sender = clientNum;

			// openQ4: validate and enqueue at the end rather than enqueueing the
			// event first and abandoning it on a bad param size.  Nothing was
			// unsafe about the original order - the drain clamps through
			// idBitMsg::SetSize - but a rejected event stayed in the queue where
			// it still counted against the per sender cap added below.
			event->paramsSize = msg.ReadBits( idMath::BitsForInteger( MAX_EVENT_PARAM_SIZE ) );
			if ( event->paramsSize ) {
				if ( event->paramsSize > MAX_EVENT_PARAM_SIZE ) {
					NetworkEventWarning( event, "invalid param size" );
					eventQueue.Free( event );
					break;
				}
				msg.ReadByteAlign();
				msg.ReadData( event->paramsBuf, event->paramsSize );
			}

			// openQ4: cap the queue share of a single sender.  Log the drop at the
			// same rate the chat flood limiter uses, otherwise the client that is
			// flooding decides how fast the server writes to its console and log.
			if ( CountQueuedClientEvents( clientNum ) >= MAX_CLIENT_QUEUED_EVENTS ) {
				if ( CheckClientReliableFlood( clientNum, RELIABLE_FLOOD_EVENT, RELIABLE_FLOOD_EVENT_DELAY ) ) {
					Warning( "Dropping network events from client %d: already has %d queued", clientNum, MAX_CLIENT_QUEUED_EVENTS );
				}
				eventQueue.Free( event );
				break;
			}

			eventQueue.Enqueue( event, idEventQueue::OUTOFORDER_DROP );
			break;
		}

// RAVEN BEGIN
// jscott: voice comms
		case GAME_RELIABLE_MESSAGE_VOICEDATA_CLIENT:
		case GAME_RELIABLE_MESSAGE_VOICEDATA_CLIENT_ECHO:
		case GAME_RELIABLE_MESSAGE_VOICEDATA_CLIENT_TEST:
		case GAME_RELIABLE_MESSAGE_VOICEDATA_CLIENT_ECHO_TEST: {
			// Obsolete reliable media is intentionally discarded. Versioned voice
			// enters only through ServerReceiveVoice's unreliable boundary.
			break;
		}
// ddynerman: stats
		case GAME_RELIABLE_MESSAGE_STAT: {
			int client = msg.ReadByte();	
			statManager->SendStat( clientNum, client );
			break;
		}
// shouchard:  voice chat
		case GAME_RELIABLE_MESSAGE_VOICECHAT_MUTING: {
			// Slot, exact boolean, packed spawn identity. In particular, never
			// interpret an older slot-only request against a replacement player.
			if ( msg.GetRemainingReadBits() != 48 ) {
				break;
			}
			const int clientDest = msg.ReadByte();
			const int mute = msg.ReadByte();
			const int identity = msg.ReadLong();
			if ( mute == 0 || mute == 1 ) {
				mpGame.ServerHandleVoiceMuting( clientNum, clientDest, identity, mute != 0 );
			}
			break;
		}
// shouchard:  server admin
		case GAME_RELIABLE_MESSAGE_SERVER_ADMIN: {
			// This message has no authentication material.  Only the in-process
			// listen-server player is intrinsically trusted; remote clients must
			// use the engine's authenticated rcon path instead.
			if ( !isListenServer || clientNum != localClientNum ) {
				Warning( "Rejected unauthenticated server admin message from client %d", clientNum );
				break;
			}

			int commandType = msg.ReadByte();
			int targetClientNum = msg.ReadByte();
			if ( SERVER_ADMIN_REMOVE_BAN == commandType ) {
				mpGame.HandleServerAdminRemoveBan( "" );
			} else if ( SERVER_ADMIN_KICK == commandType ) {
				mpGame.HandleServerAdminKickPlayer( targetClientNum );
			} else if ( SERVER_ADMIN_FORCE_SWITCH == commandType ) {
				mpGame.HandleServerAdminForceTeamSwitch( targetClientNum );
			} else {
				Warning( "Server admin packet with bad type %d", commandType );
			}
			break;
		}
// mekberg: get ban list for server
		case GAME_RELIABLE_MESSAGE_GETADMINBANLIST: {
			if ( !isListenServer || clientNum != localClientNum ) {
				Warning( "Rejected unauthenticated admin ban-list request from client %d", clientNum );
				break;
			}
			// openQ4 MP-18: the listen server's host reads the list it already holds
			break;
		}
// RAVEN END

		case GAME_RELIABLE_MESSAGE_GETVOTEMAPS: {
			// openQ4: SendMapList is an 8 KB reliable send back to the caller;
			// repeated requests overflow that client's own reliable queue and
			// stall the server writing them
			if ( !CheckClientReliableFlood( clientNum, RELIABLE_FLOOD_VOTEMAPS, RELIABLE_FLOOD_VOTEMAPS_DELAY ) ) {
				break;
			}

			mpGame.SendMapList( clientNum );
			break;
		}

		// openQ4 BEGIN
		case GAME_RELIABLE_MESSAGE_READY: {
			if ( msg.GetRemainingReadBits() != 8 ) {
				Warning( "Ignoring malformed ready message from client %d", clientNum );
				break;
			}
			const int readyValue = msg.ReadByte();
			if ( readyValue != 0 && readyValue != 1 ) {
				Warning( "Ignoring invalid ready value %d from client %d", readyValue, clientNum );
				break;
			}
			mpGame.ServerSetPlayerReady( clientNum, readyValue != 0 );
			break;
		}
		case GAME_RELIABLE_MESSAGE_MATCH_REQUEST:
			mpGame.ServerReceiveMatchOperation( clientNum, msg );
			break;
		case GAME_RELIABLE_MESSAGE_MAPDB: {
			// a queued map is announced to everyone; one request a second is plenty
			if ( !CheckClientReliableFlood( clientNum, RELIABLE_FLOOD_MAPDB, RELIABLE_FLOOD_MAPDB_DELAY ) ) {
				break;
			}
			mpMapHost.ServerReceiveRequest( clientNum, msg );
			break;
		}
		case GAME_RELIABLE_MESSAGE_MODERATION: {
			// an admin's request writes the admin log; nobody types two in half a second
			if ( !CheckClientReliableFlood( clientNum, RELIABLE_FLOOD_MODERATION, RELIABLE_FLOOD_MODERATION_DELAY ) ) {
				break;
			}
			ServerModerationRequest( clientNum, msg );
			break;
		}
		case GAME_RELIABLE_MESSAGE_IDENTITY: {
			// MP-17: a proof answers one challenge, a ghost code is typed by hand
			if ( !CheckClientReliableFlood( clientNum, RELIABLE_FLOOD_IDENTITY, RELIABLE_FLOOD_IDENTITY_DELAY ) ) {
				break;
			}
			mpGame.ServerReceiveIdentity( clientNum, msg );
			break;
		}
		// openQ4 END

		default: {
			Warning( "Unknown client->server reliable message: %d", id );
			break;
		}
	}
}

/*
================
idGameLocal::RepeaterProcessReliableMessage
================
*/
void idGameLocal::RepeaterProcessReliableMessage( int clientNum, const idBitMsg &msg ) {
	int id;

	id = msg.ReadByte();
	switch( id ) {
		case GAME_RELIABLE_MESSAGE_CHAT:
		case GAME_RELIABLE_MESSAGE_TCHAT: {
			if ( !g_noTVChat.GetBool() )
			{
				char name[128], text[128], parm[128];
				idStr suffixed_name, mangled_text;

				msg.ReadString( name, sizeof( name ) );
				msg.ReadString( text, sizeof( text ) );
				msg.ReadString( parm, sizeof( parm ) );

				suffixed_name = va( "^0%s^c777 (viewer)", name );
				mangled_text = va( "^c777%s", text );

				idBitMsg outMsg;
				byte msgBuf[ MAX_GAME_MESSAGE_SIZE ];
				outMsg.Init( msgBuf, sizeof( msgBuf ) );
				outMsg.BeginWriting();
				outMsg.WriteByte( GAME_RELIABLE_MESSAGE_CHAT );
				outMsg.WriteString( suffixed_name.c_str() );
				outMsg.WriteString( mangled_text.c_str() );
				outMsg.WriteString( "" );
// jmarshall - engine has no repeater reliable channel
				//networkSystem->RepeaterSendReliableMessage( -1, outMsg );
// jmarshall end
			}
			break;
		}
		case GAME_RELIABLE_MESSAGE_VCHAT: {
			break;
		}
		case GAME_RELIABLE_MESSAGE_KILL: {
			break;
		}
		case GAME_RELIABLE_MESSAGE_DROPWEAPON: {
			break;
		}
		case GAME_RELIABLE_MESSAGE_CALLVOTE: {
			break;
		}
		case GAME_RELIABLE_MESSAGE_CASTVOTE: {
			break;
		}
// RAVEN BEGIN
// shouchard:  multivalue votes
		case GAME_RELIABLE_MESSAGE_CALLPACKEDVOTE: {
			break;
		}
// RAVEN END
#if 0
		// uncomment this if you want to track when players are in a menu
		case GAME_RELIABLE_MESSAGE_MENU: {
			bool menuUp = ( msg.ReadBits( 1 ) != 0 );
			break;
		}
#endif
		case GAME_RELIABLE_MESSAGE_EVENT: {
			break;
		}

// RAVEN BEGIN
// jscott: voice comms
		case GAME_RELIABLE_MESSAGE_VOICEDATA_CLIENT:
		case GAME_RELIABLE_MESSAGE_VOICEDATA_CLIENT_ECHO:
		case GAME_RELIABLE_MESSAGE_VOICEDATA_CLIENT_TEST:
		case GAME_RELIABLE_MESSAGE_VOICEDATA_CLIENT_ECHO_TEST: {
			break;
		}
// ddynerman: stats
		case GAME_RELIABLE_MESSAGE_STAT: {
			break;
		}
// shouchard:  voice chat
		case GAME_RELIABLE_MESSAGE_VOICECHAT_MUTING: {
			break;
		}
// shouchard:  server admin
		case GAME_RELIABLE_MESSAGE_SERVER_ADMIN: {
			break;
		}
// mekberg: get ban list for server
		case GAME_RELIABLE_MESSAGE_GETADMINBANLIST: {
			break;
		}
// RAVEN END

		case GAME_RELIABLE_MESSAGE_GETVOTEMAPS: {
			break;
		}

		// openQ4: viewers have no ready state
		case GAME_RELIABLE_MESSAGE_READY: {
			break;
		}

		// openQ4: viewers neither queue nor pick maps
		case GAME_RELIABLE_MESSAGE_MAPDB: {
			break;
		}

		// openQ4: viewers are never admins
		case GAME_RELIABLE_MESSAGE_MODERATION: {
			break;
		}

		// openQ4 MP-17: viewers have no identity
		case GAME_RELIABLE_MESSAGE_IDENTITY: {
			break;
		}

		default: {
			Warning( "Unknown client->server reliable message: %d", id );
			break;
		}
	}
}

/*
================
idGameLocal::ClientShowSnapshot
================
*/
void idGameLocal::ClientShowSnapshot( int clientNum ) const {
	int baseBits;
	idEntity *ent;
	idPlayer *player;
	idMat3 viewAxis;
	idBounds viewBounds;
	entityState_t *base;

	if ( !net_clientShowSnapshot.GetInteger() ) {
		return;
	}

	player = static_cast<idPlayer *>( entities[clientNum] );
	if ( !player ) {
		return;
	}

	viewAxis = player->viewAngles.ToMat3();
	viewBounds = player->GetPhysics()->GetAbsBounds().Expand( net_clientShowSnapshotRadius.GetFloat() );

	for( ent = snapshotEntities.Next(); ent != NULL; ent = ent->snapshotNode.Next() ) {

		if ( net_clientShowSnapshot.GetInteger() == 1 && ent->snapshotBits == 0 ) {
			continue;
		}

		const idBounds &entBounds = ent->GetPhysics()->GetAbsBounds();

		if ( !entBounds.IntersectsBounds( viewBounds ) ) {
			continue;
		}

		base = clientEntityStates[clientNum][ent->entityNumber];
		if ( base ) {
			baseBits = base->state.GetNumBitsWritten();
		} else {
			baseBits = 0;
		}

		if ( net_clientShowSnapshot.GetInteger() == 2 && baseBits == 0 ) {
			continue;
		}

		gameRenderWorld->DebugBounds( colorGreen, entBounds );
		gameRenderWorld->DrawText( va( "%d: %s (%d,%d bytes of %d,%d)\n", ent->entityNumber,
						ent->name.c_str(), ent->snapshotBits >> 3, ent->snapshotBits & 7, baseBits >> 3, baseBits & 7 ),
							entBounds.GetCenter(), 0.1f, colorWhite, viewAxis, 1 );
	}
}


/*
================
idGameLocal::ReadSnapshot
================
*/
bool idGameLocal::ClientReadSnapshot( int clientNum, int snapshotSequence, const int gameFrame, const int gameTime, const int dupeUsercmds, const int aheadOfServer, const idBitMsg &msg, bool readEntityInstances ) {
	int						i, entityDefNumber, numBitsRead;
	idEntity				*ent;
	idPlayer				*player, *spectated;
	pvsHandle_t				pvsHandle;
	idDict					args;
	idBitMsgDelta			deltaMsg;
	snapshot_t				*snapshot;
	entityState_t			*base, *newBase;
	int						spawnId, entityInstance;
	int						numSourceAreas, sourceAreas[ idEntity::MAX_PVS_AREAS ];

	const idDeclEntityDef	*decl;

	if ( clientNum < 0 || clientNum > MAX_CLIENTS ) {
		common->Warning( "ClientReadSnapshot: invalid client number %d", clientNum );
		return false;
	}

	if ( net_clientLagOMeter.GetBool() && renderSystem && !(GetDemoState() == DEMO_PLAYING && ( IsServerDemoPlaying() || IsTimeDemo() )) ) {
		lagometer.Update( aheadOfServer, dupeUsercmds );
	}

	InitLocalClient( clientNum );

	gameRenderWorld->DebugClear( time );

	// update the game time
	framenum = gameFrame;
	time = gameTime;
	previousTime = time - GetMSec();
	clientSnapshotTime = gameTime;

	isNewFrame = true;

	// This snapshot closes the replay chain the last one opened; the players read below
	// still hold their re-simulated tics for it.
	lastChainReplayTics = clientReplayTics;
	clientReplayTics = 0;

	// clear the snapshot entity list
	snapshotEntities.Clear();

	// allocate new snapshot
	snapshot = snapshotAllocator.Alloc();
	snapshot->sequence = snapshotSequence;
	snapshot->firstEntityState = NULL;
	snapshot->next = clientSnapshots[clientNum];
	clientSnapshots[clientNum] = snapshot;

#if ASYNC_WRITE_TAGS
	idRandom tagRandom;
	tagRandom.SetSeed( msg.ReadLong() );
#endif

	ClientReadUnreliableMessages( msg );
	if ( msg.IsReadOverflowed() ) {
		common->Warning( "ClientReadSnapshot: truncated unreliable-message queue" );
		return false;
	}

#if ASYNC_WRITE_TAGS
	if ( msg.ReadLong() != tagRandom.RandomInt() ) {
		common->Warning( "ClientReadSnapshot: unreliable-message tag mismatch" );
		return false;
	}
#endif

	// read all entities from the snapshot
	for ( i = msg.ReadBits( GENTITYNUM_BITS ); i != ENTITYNUM_NONE; i = msg.ReadBits( GENTITYNUM_BITS ) ) {
		if ( i < 0 || i >= ENTITYNUM_NONE ) {
			common->Warning( "ClientReadSnapshot: invalid or truncated entity number %d", i );
			return false;
		}
		base = clientEntityStates[clientNum][i];
		if ( base ) {
			base->state.BeginReading();
		}
		newBase = entityStateAllocator.Alloc();
		newBase->entityNumber = i;
		newBase->next = snapshot->firstEntityState;
		snapshot->firstEntityState = newBase;
		newBase->state.Init( newBase->stateBuf, sizeof( newBase->stateBuf ) );
		newBase->state.BeginWriting();

		numBitsRead = msg.GetNumBitsRead();

		deltaMsg.InitReading( base ? &base->state : NULL, &newBase->state, &msg );
		deltaMsg.SetRetailBaseReads( RetailSnapshotFormat() );

		spawnId = deltaMsg.ReadBits( 32 - GENTITYNUM_BITS );
		entityDefNumber = NetworkDeclIndex( DECL_ENTITYDEF, deltaMsg.ReadBits( entityDefBits ) );
		entityInstance = readEntityInstances ? deltaMsg.ReadBits( ASYNC_PLAYER_INSTANCE_BITS ) : -1;
		if ( spawnId < 0 || entityDefNumber < 0 ||
			 entityDefNumber >= declManager->GetNumDecls( DECL_ENTITYDEF ) ||
			 ( readEntityInstances && ( entityInstance < 0 || entityInstance >= MAX_INSTANCES ) ) ) {
			common->Warning( "ClientReadSnapshot: invalid entity header %d/%d/%d", spawnId, entityDefNumber, entityInstance );
			return false;
		}

		ent = entities[i];
		if ( RetailSnapshotFormat() && cvarSystem->GetCVarInteger( "net_retailDemoDebug" ) > 2 ) {
			// openQ4 MP-22: each entity a retail recording's snapshot holds
			common->Printf( "retail snapshot %d entity %d spawn %d def %s%s\n", snapshotSequence, i, spawnId,
				declManager->DeclByIndex( DECL_ENTITYDEF, entityDefNumber, false )->GetName(),
				base ? "" : " (no base)" );
		}

		// if there is no entity or an entity of the wrong type
		if ( !ent || ent->entityDefNumber != entityDefNumber || spawnId != spawnIds[ i ] ) {

			if ( i < MAX_CLIENTS && ent ) {
				// SPAWN_PLAYER should be taking care of spawning the entity with the right spawnId
				common->Warning( "ClientReadSnapshot: recycling client entity %d\n", i );
			}

			delete ent;

			spawnCount = spawnId;

			args.Clear();
			args.SetInt( "spawn_entnum", i );
			args.Set( "name", va( "entity%d", i ) );

			if ( readEntityInstances ) {
				args.SetInt( "instance", entityInstance );
			} else if ( gameLocal.GetLocalPlayer() ) {
				// Live and legacy snapshots contain only the local instance.
				args.SetInt( "instance", gameLocal.GetLocalPlayer()->GetInstance() );
			}
			
			decl = static_cast< const idDeclEntityDef * >( declManager->DeclByIndex( DECL_ENTITYDEF, entityDefNumber, false ) );
			if ( decl == NULL || decl->GetType() != DECL_ENTITYDEF ) {
				common->Warning( "ClientReadSnapshot: invalid entityDef %d", entityDefNumber );
				return false;
			}
			args.Set( "classname", decl->GetName() );
			if ( !SpawnEntityDef( args, &ent ) || !entities[i] ) {
				common->Warning( "ClientReadSnapshot: failed to spawn entity '%s'", decl->GetName() );
				return false;
			}

			if ( i < MAX_CLIENTS && i >= numClients ) {
				numClients = i + 1;
			}
		}

		if ( readEntityInstances && ent->GetInstance() != entityInstance ) {
			ent->SetInstance( entityInstance );
		}

		// add the entity to the snapshot list
		ent->snapshotNode.AddToEnd( snapshotEntities );
		ent->snapshotSequence = snapshotSequence;

// RAVEN BEGIN
// bdube: stale network entities
		// Ensure the clipmodel is relinked when transitioning from state
		if ( ent->fl.networkStale ) {
			ent->GetPhysics()->LinkClip();
		}
// RAVEN END

		// read the class specific data from the snapshot
		ent->ReadFromSnapshot( deltaMsg );
		if ( deltaMsg.IsReadOverflowed() ) {
			common->Warning( "ClientReadSnapshot: truncated entity state for %d (%s, %s)", i, ent->GetClassname(), ent->GetEntityDefName() );
			return false;
		}

		// read the player state from player entities
		if ( ent->entityNumber < MAX_CLIENTS ) {
			if ( deltaMsg.ReadBits( 1 ) == 1 ) {
				if ( !ent->IsType( idPlayer::GetClassType() ) ) {
					common->Warning( "ClientReadSnapshot: non-player entity %d has player state", ent->entityNumber );
					return false;
				}
				static_cast< idPlayer * >( ent )->ReadPlayerStateFromSnapshot( deltaMsg );
				if ( deltaMsg.IsReadOverflowed() ) {
					common->Warning( "ClientReadSnapshot: truncated player state for %d", ent->entityNumber );
					return false;
				}
			}
		}

		// once we read new snapshot data, unstale the ent
		if( ent->fl.networkStale ) {
			ent->ClientUnstale();
			ent->fl.networkStale = false;
		}
		ent->snapshotBits = msg.GetNumBitsRead() - numBitsRead;

#if ASYNC_WRITE_TAGS
		const int entityTag = tagRandom.RandomInt();
		if ( LegacySnapshotFormat() ? ( msg.ReadLong() != entityTag ) :
			 ( msg.ReadBits( SNAPSHOT_ENTITY_TAG_BITS ) != ( entityTag & SNAPSHOT_ENTITY_TAG_MASK ) ) ) {
			//cmdSystem->BufferCommandText( CMD_EXEC_NOW, "writeGameState" );
			assert( entityDefNumber >= 0 );
			assert( entityDefNumber < declManager->GetNumDecls( DECL_ENTITYDEF ) );
			const char * classname = declManager->DeclByIndex( DECL_ENTITYDEF, entityDefNumber, false )->GetName();
			common->Warning( "ClientReadSnapshot: write/read mismatch for classname '%s'", classname );
			return false;
		}
#endif
	}

	if ( clientNum < MAX_CLIENTS ) {
		idEntity *clientEntity = entities[ clientNum ];
		player = clientEntity != NULL && clientEntity->IsType( idPlayer::GetClassType() ) ?
			static_cast<idPlayer *>( clientEntity ) : NULL;
	} else {
		player = gameLocal.GetLocalPlayer();
	}
	if ( player == NULL || !player->IsType( idPlayer::GetClassType() ) ) {
		common->Warning( "ClientReadSnapshot: no valid local demo player" );
		return false;
	}

	if ( player->spectating &&
		 player->spectator != clientNum &&
		 player->spectator >= 0 &&
		 player->spectator < MAX_CLIENTS &&
		 entities[ player->spectator ] != NULL &&
		 entities[ player->spectator ]->IsType( idPlayer::GetClassType() ) ) {
		spectated = static_cast< idPlayer * >( entities[ player->spectator ] );
	} else {
		spectated = player;
	}

	if ( spectated ) {
		// get PVS for this player
		// don't use PVSAreas for networking - PVSAreas depends on animations (and md5 bounds), which are not synchronized
		numSourceAreas = gameRenderWorld->BoundsInAreas( spectated->GetPlayerPhysics()->GetAbsBounds(), sourceAreas, idEntity::MAX_PVS_AREAS );
		pvsHandle = gameLocal.pvs.SetupCurrentPVS( sourceAreas, numSourceAreas, PVS_NORMAL );
	} else {
		gameLocal.Printf( "Client hasn't spawned self yet.\n" );
		numSourceAreas = gameRenderWorld->BoundsInAreas( idBounds( vec3_origin ), sourceAreas, idEntity::MAX_PVS_AREAS );
		pvsHandle = gameLocal.pvs.SetupCurrentPVS( sourceAreas, numSourceAreas, PVS_NORMAL );
	}

	// read the PVS from the snapshot
#if ASYNC_WRITE_PVS
// zinx - FIXME - this isn't written for server demos or for non-PVS'd repeaters, so we shouldn't read it.
	int serverPVS[idEntity::MAX_PVS_AREAS];
	i = numSourceAreas;
	while ( i < idEntity::MAX_PVS_AREAS ) {
		sourceAreas[ i++ ] = 0;
	}
	for ( i = 0; i < idEntity::MAX_PVS_AREAS; i++ ) {
		serverPVS[ i ] = msg.ReadLong();
	}
	if ( memcmp( sourceAreas, serverPVS, idEntity::MAX_PVS_AREAS * sizeof( int ) ) ) {
		common->Warning( "client PVS areas != server PVS areas, sequence 0x%x", snapshotSequence );
		for ( i = 0; i < idEntity::MAX_PVS_AREAS; i++ ) {
			common->DPrintf( "%3d ", sourceAreas[ i ] );
		}
		common->DPrintf( "\n" );
		for ( i = 0; i < idEntity::MAX_PVS_AREAS; i++ ) {
			common->DPrintf( "%3d ", serverPVS[ i ] );
		}
		common->DPrintf( "\n" );
	}
	gameLocal.pvs.ReadPVS( pvsHandle, msg );
#endif
	for ( i = 0; i < ENTITY_PVS_SIZE; i++ ) {
		snapshot->pvs[i] = msg.ReadDeltaLong( clientPVS[clientNum][i] );
	}
	if ( msg.IsReadOverflowed() ) {
		common->Warning( "ClientReadSnapshot: truncated PVS state" );
		pvs.FreeCurrentPVS( pvsHandle );
		return false;
	}
// RAVEN BEGIN
// ddynerman: performance profiling
	net_entsInSnapshot += snapshotEntities.Num();
	net_snapshotSize += msg.GetSize();
// RAVEN END
	// add entities in the PVS that haven't changed since the last applied snapshot
	idEntity *nextSpawnedEnt;
	for( ent = spawnedEntities.Next(); ent != NULL; ent = nextSpawnedEnt ) {
		nextSpawnedEnt = ent->spawnNode.Next();

		// local fake client
		if ( ent->entityNumber == ENTITYNUM_NONE ) {
			continue;
		}

		// if the entity is already in the snapshot
		if ( ent->snapshotSequence == snapshotSequence ) {
			continue;
		}

		// if the entity is not in the snapshot PVS
		if ( !( snapshot->pvs[ent->entityNumber >> 5] & ( 1 << ( ent->entityNumber & 31 ) ) ) ) {

			if ( !ent->fl.networkSync ) {
				// don't do stale / unstale on entities that are not marked network sync
				continue;
			}

			if ( ent->PhysicsTeamInPVS( pvsHandle ) ) {
				if ( ent->entityNumber >= MAX_CLIENTS && isMapEntity[ ent->entityNumber ] ) {
					// server says it's not in PVS, client says it's in PVS
					// if that happens on map entities, most likely something is wrong
					// I can see that moving pieces along several PVS could be a legit situation though
					// this is a band aid, which means something is not done right elsewhere
					if ( net_warnStale.GetInteger() > 1 || ( net_warnStale.GetInteger() == 1 && !ent->fl.networkStale ) ) {
						common->Warning( "client thinks map entity 0x%x (%s) is stale, sequence 0x%x", ent->entityNumber, ent->name.c_str(), snapshotSequence );
					}
				}
// RAVEN BEGIN
// bdube: hide while not in snapshot
				if ( !ent->fl.networkStale ) {
					if ( ent->ClientStale() ) {
						delete ent;
						ent = NULL;
					} else {
						ent->fl.networkStale = true;
					}
				}

			} else {
				if ( !ent->fl.networkStale ) {
					if ( ent->ClientStale() ) {
						delete ent;
						ent = NULL;
					} else {
						ent->fl.networkStale = true;
					}
				}
			}
// RAVEN END

			continue;
		}

		// add the entity to the snapshot list
		ent->snapshotNode.AddToEnd( snapshotEntities );
		ent->snapshotSequence = snapshotSequence;
		ent->snapshotBits = 0;

// RAVEN BEGIN
// bdube: hide while not in snapshot
		// Ensure the clipmodel is relinked when transitioning from state
		if ( ent->fl.networkStale ) {
			ent->GetPhysics()->LinkClip();
		}
// RAVEN END

		base = clientEntityStates[clientNum][ent->entityNumber];
		if ( !base ) {
			// entity has probably fl.networkSync set to false
			// non netsynced map entities go in and out of PVS, and may need stale/unstale calls
			if ( ent->fl.networkStale ) {
				ent->ClientUnstale();
				ent->fl.networkStale = false;
			}
			continue;
		}

		if ( !ent->fl.networkSync ) {
			// this is not supposed to happen
			// it did however, when restarting a map with a different inhibit of entities caused entity numbers to be laid differently
			// an idLight would occupy the entity number of an idItem for instance, and although it's not network-synced ( static level light ),
			// the presence of a base would cause the system to think that it is and corrupt things
			// we changed the map population so the entity numbers are kept the same no matter how things are inhibited
			// this code is left as a fall-through fixup / sanity type of thing
			// if this still happens, it's likely "client thinks map entity is stale" is happening as well, and we're still at risk of corruption
			Warning( "ClientReadSnapshot: entity %d of type %s is not networkSync and has a snapshot base", ent->entityNumber, ent->GetType()->classname );
			entityStateAllocator.Free( clientEntityStates[clientNum][ent->entityNumber] );
			clientEntityStates[clientNum][ent->entityNumber] = NULL;
			continue;
		}

		base->state.BeginReading();

		deltaMsg.InitReading( &base->state, NULL, (const idBitMsg *)NULL );
		spawnId = deltaMsg.ReadBits( 32 - GENTITYNUM_BITS );
		entityDefNumber = deltaMsg.ReadBits( entityDefBits );
		if ( readEntityInstances ) {
			entityInstance = deltaMsg.ReadBits( ASYNC_PLAYER_INSTANCE_BITS );
			if ( entityInstance < 0 || entityInstance >= MAX_INSTANCES ) {
				common->Warning( "ClientReadSnapshot: invalid baseline instance %d", entityInstance );
				pvs.FreeCurrentPVS( pvsHandle );
				return false;
			}
			if ( ent->GetInstance() != entityInstance ) {
				ent->SetInstance( entityInstance );
			}
		}

		// read the class specific data from the base state
		ent->ReadFromSnapshot( deltaMsg );
		if ( deltaMsg.IsReadOverflowed() ) {
			common->Warning( "ClientReadSnapshot: truncated baseline state for %d", ent->entityNumber );
			pvs.FreeCurrentPVS( pvsHandle );
			return false;
		}

		// after snapshot read, notify client of unstale
		if ( ent->fl.networkStale ) {
			ent->ClientUnstale();
			ent->fl.networkStale = false;
		}
	}

// RAVEN BEGIN
// ddynerman: add the ambient lights to the snapshot entities
	for( int i = 0; i < ambientLights.Num(); i++ ) {
		ambientLights[ i ]->snapshotNode.AddToEnd( snapshotEntities );
		ambientLights[ i ]->fl.networkStale = false;
	}
// RAVEN END

	// free the PVS
	pvs.FreeCurrentPVS( pvsHandle );

	// read the game state from the snapshot
	base = clientEntityStates[clientNum][ENTITYNUM_NONE];	// ENTITYNUM_NONE is used for the game state
	if ( base ) {
		base->state.BeginReading();
	}
	newBase = entityStateAllocator.Alloc();
	newBase->entityNumber = ENTITYNUM_NONE;
	newBase->next = snapshot->firstEntityState;
	snapshot->firstEntityState = newBase;
	newBase->state.Init( newBase->stateBuf, sizeof( newBase->stateBuf ) );
	newBase->state.BeginWriting();
	deltaMsg.InitReading( base ? &base->state : NULL, &newBase->state, &msg );
	deltaMsg.SetRetailBaseReads( RetailSnapshotFormat() );

	ReadGameStateFromSnapshot( deltaMsg );
	if ( deltaMsg.IsReadOverflowed() ) {
		common->Warning( "ClientReadSnapshot: truncated game state" );
		return false;
	}

	// visualize the snapshot
	if ( clientNum < MAX_CLIENTS ) {
		ClientShowSnapshot( clientNum );
	} else {
		// FIXME
	}

	// process entity events
	ClientProcessEntityNetworkEventQueue();

	return true;
}

/*
===============
idGameLocal::ClientReadServerDemoSnapshot
===============
*/
bool idGameLocal::ClientReadServerDemoSnapshot( int sequence, const int gameFrame, const int gameTime, const idBitMsg &msg, bool fullWorldInstances ) {
	const bool ret = ClientApplySnapshot( MAX_CLIENTS, sequence - 1 );
	if ( !ret && sequence != 1 ) {
		common->Warning( "ClientReadServerDemoSnapshot: missing baseline for sequence %d", sequence );
		return false;
	}

	return ClientReadSnapshot( MAX_CLIENTS, sequence, gameFrame, gameTime, 0, 0, msg, fullWorldInstances );
}

/*
===============
idGameLocal::ServerWriteServerDemoBaseline

openQ4: an MVD keyframe's delta baseline. Right after snapshot `sequence` is
written it is the only one pending; once applied, its entity states replace the
acknowledged ones. That merged table, stale entries included (a reused entity
slot is delta-encoded against them), and the snapshot's entity PVS are what the
next snapshot will be written against. The grammar is versioned on its own and
validated by the engine before it is recorded or restored.
===============
*/
static const int MVD_BASELINE_VERSION = 1;

bool idGameLocal::ServerWriteServerDemoBaseline( int sequence, idFile *file ) {
	if ( !IsServerDemoRecording() || file == NULL ) {
		return false;
	}
	const snapshot_t *pending = clientSnapshots[ MAX_CLIENTS ];
	if ( pending == NULL || pending->next != NULL || pending->sequence != sequence ) {
		return false;
	}

	const entityState_t *baseline[ MAX_GENTITIES ];
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		baseline[ i ] = clientEntityStates[ MAX_CLIENTS ][ i ];
	}
	for ( const entityState_t *state = pending->firstEntityState; state != NULL; state = state->next ) {
		if ( state->entityNumber < 0 || state->entityNumber >= MAX_GENTITIES ) {
			return false;
		}
		baseline[ state->entityNumber ] = state;
	}

	int count = 0;
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		if ( baseline[ i ] != NULL ) {
			count++;
		}
	}
	file->WriteInt( MVD_BASELINE_VERSION );
	file->WriteInt( sequence );
	file->WriteUnsignedChar( serverDemoInstances ? 1 : 0 );
	file->WriteInt( count );
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		if ( baseline[ i ] == NULL ) {
			continue;
		}
		const int size = baseline[ i ]->state.GetSize();
		if ( size <= 0 || size > MAX_ENTITY_STATE_SIZE ) {
			return false;
		}
		file->WriteUnsignedShort( static_cast<unsigned short>( i ) );
		file->WriteUnsignedShort( static_cast<unsigned short>( size ) );
		file->Write( baseline[ i ]->state.GetData(), size );
	}
	for ( int i = 0; i < ENTITY_PVS_SIZE; i++ ) {
		file->WriteInt( pending->pvs[ i ] );
	}
	return true;
}

/*
===============
idGameLocal::ClientReadServerDemoBaseline

openQ4: restores an MVD keyframe after ReadNetworkInfo has spawned its players.
The baseline becomes the acknowledged state of a pending snapshot numbered
`sequence`, so the next recorded snapshot applies it and decodes against
exactly the states the recorder used. Entities present at the keyframe are
spawned or updated from their states as a snapshot would; those absent are
staled the same way.
===============
*/
bool idGameLocal::ClientReadServerDemoBaseline( int sequence, idFile *file ) {
	if ( !IsServerDemoPlaying() || IsRepeaterDemoPlaying() || isServer || localClientNum != MAX_CLIENTS ||
		 file == NULL ) {
		return false;
	}
	int version = 0;
	int recordedSequence = 0;
	unsigned char instanceFlag = 0;
	int count = 0;
	if ( file->ReadInt( version ) != sizeof( version ) || version != MVD_BASELINE_VERSION ||
		 file->ReadInt( recordedSequence ) != sizeof( recordedSequence ) || recordedSequence != sequence ||
		 file->ReadUnsignedChar( instanceFlag ) != sizeof( instanceFlag ) || instanceFlag > 1 ||
		 file->ReadInt( count ) != sizeof( count ) || count < 0 || count > MAX_GENTITIES ) {
		common->Warning( "ClientReadServerDemoBaseline: invalid baseline header" );
		return false;
	}
	const bool readEntityInstances = instanceFlag != 0;

	// This baseline replaces whatever the reset left behind.
	FreeSnapshotsOlderThanSequence( clientSnapshots[ MAX_CLIENTS ], idMath::INT_MAX );
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		if ( clientEntityStates[ MAX_CLIENTS ][ i ] != NULL ) {
			entityStateAllocator.Free( clientEntityStates[ MAX_CLIENTS ][ i ] );
			clientEntityStates[ MAX_CLIENTS ][ i ] = NULL;
		}
	}
	int previous = -1;
	for ( int n = 0; n < count; n++ ) {
		unsigned short entityNumber = 0;
		unsigned short size = 0;
		if ( file->ReadUnsignedShort( entityNumber ) != sizeof( entityNumber ) ||
			 file->ReadUnsignedShort( size ) != sizeof( size ) ||
			 entityNumber <= previous || entityNumber >= MAX_GENTITIES ||
			 size == 0 || size > MAX_ENTITY_STATE_SIZE ) {
			common->Warning( "ClientReadServerDemoBaseline: invalid baseline entry %d", n );
			return false;
		}
		entityState_t *state = entityStateAllocator.Alloc();
		state->entityNumber = entityNumber;
		state->next = NULL;
		state->state.Init( state->stateBuf, sizeof( state->stateBuf ) );
		state->state.SetSize( size );
		clientEntityStates[ MAX_CLIENTS ][ entityNumber ] = state;
		if ( file->Read( state->state.GetData(), size ) != size ) {
			common->Warning( "ClientReadServerDemoBaseline: truncated baseline entry %d", n );
			return false;
		}
		previous = entityNumber;
	}
	for ( int i = 0; i < ENTITY_PVS_SIZE; i++ ) {
		if ( file->ReadInt( clientPVS[ MAX_CLIENTS ][ i ] ) != sizeof( int ) ) {
			common->Warning( "ClientReadServerDemoBaseline: truncated entity PVS" );
			return false;
		}
	}

	// An empty pending snapshot: applying it before the next snapshot keeps
	// these states and PVS as the acknowledged baseline.
	snapshot_t *snapshot = snapshotAllocator.Alloc();
	snapshot->sequence = sequence;
	snapshot->firstEntityState = NULL;
	snapshot->next = NULL;
	memcpy( snapshot->pvs, clientPVS[ MAX_CLIENTS ], sizeof( snapshot->pvs ) );
	clientSnapshots[ MAX_CLIENTS ] = snapshot;

	snapshotEntities.Clear();
	idEntity *nextSpawnedEnt;
	for ( idEntity *ent = spawnedEntities.Next(); ent != NULL; ent = nextSpawnedEnt ) {
		nextSpawnedEnt = ent->spawnNode.Next();
		const int number = ent->entityNumber;
		if ( number == ENTITYNUM_NONE || !ent->fl.networkSync || ent->fl.networkStale ||
			 ( clientPVS[ MAX_CLIENTS ][ number >> 5 ] & ( 1 << ( number & 31 ) ) ) != 0 ) {
			continue;
		}
		if ( ent->ClientStale() ) {
			delete ent;
		} else {
			ent->fl.networkStale = true;
		}
	}

	for ( int i = 0; i < ENTITYNUM_NONE; i++ ) {
		entityState_t *base = clientEntityStates[ MAX_CLIENTS ][ i ];
		if ( base == NULL || ( clientPVS[ MAX_CLIENTS ][ i >> 5 ] & ( 1 << ( i & 31 ) ) ) == 0 ) {
			continue;
		}
		idBitMsgDelta deltaMsg;
		base->state.BeginReading();
		deltaMsg.InitReading( &base->state, NULL, NULL );
		const int spawnId = deltaMsg.ReadBits( 32 - GENTITYNUM_BITS );
		const int entityDefNumber = NetworkDeclIndex( DECL_ENTITYDEF, deltaMsg.ReadBits( entityDefBits ) );
		const int entityInstance = readEntityInstances ? deltaMsg.ReadBits( ASYNC_PLAYER_INSTANCE_BITS ) : -1;
		if ( spawnId < 0 || entityDefNumber < 0 ||
			 entityDefNumber >= declManager->GetNumDecls( DECL_ENTITYDEF ) ||
			 ( readEntityInstances && ( entityInstance < 0 || entityInstance >= MAX_INSTANCES ) ) ) {
			common->Warning( "ClientReadServerDemoBaseline: invalid entity header %d/%d/%d", spawnId, entityDefNumber, entityInstance );
			return false;
		}

		idEntity *ent = entities[ i ];
		if ( ent == NULL || ent->entityDefNumber != entityDefNumber || spawnId != spawnIds[ i ] ) {
			if ( i < MAX_CLIENTS && ent != NULL ) {
				common->Warning( "ClientReadServerDemoBaseline: recycling client entity %d", i );
			}
			delete ent;
			ent = NULL;
			spawnCount = spawnId;

			idDict args;
			args.SetInt( "spawn_entnum", i );
			args.Set( "name", va( "entity%d", i ) );
			if ( readEntityInstances ) {
				args.SetInt( "instance", entityInstance );
			} else if ( GetLocalPlayer() != NULL ) {
				args.SetInt( "instance", GetLocalPlayer()->GetInstance() );
			}
			const idDeclEntityDef *decl = static_cast< const idDeclEntityDef * >(
				declManager->DeclByIndex( DECL_ENTITYDEF, entityDefNumber, false ) );
			if ( decl == NULL || decl->GetType() != DECL_ENTITYDEF ) {
				common->Warning( "ClientReadServerDemoBaseline: invalid entityDef %d", entityDefNumber );
				return false;
			}
			args.Set( "classname", decl->GetName() );
			if ( !SpawnEntityDef( args, &ent ) || entities[ i ] == NULL ) {
				common->Warning( "ClientReadServerDemoBaseline: failed to spawn entity '%s'", decl->GetName() );
				return false;
			}
			if ( i < MAX_CLIENTS && i >= numClients ) {
				numClients = i + 1;
			}
		}

		if ( readEntityInstances && ent->GetInstance() != entityInstance ) {
			ent->SetInstance( entityInstance );
		}
		ent->snapshotNode.AddToEnd( snapshotEntities );
		ent->snapshotSequence = sequence;
		if ( ent->fl.networkStale ) {
			ent->GetPhysics()->LinkClip();
		}
		ent->ReadFromSnapshot( deltaMsg );
		if ( i < MAX_CLIENTS && !deltaMsg.IsReadOverflowed() && deltaMsg.ReadBits( 1 ) == 1 ) {
			if ( !ent->IsType( idPlayer::GetClassType() ) ) {
				common->Warning( "ClientReadServerDemoBaseline: non-player entity %d has player state", i );
				return false;
			}
			static_cast< idPlayer * >( ent )->ReadPlayerStateFromSnapshot( deltaMsg );
		}
		if ( deltaMsg.IsReadOverflowed() ) {
			common->Warning( "ClientReadServerDemoBaseline: truncated state for entity %d", i );
			return false;
		}
		// As in ReadNetworkInfo: this resets presentation, so powerups appear.
		ent->ClientUnstale();
		ent->fl.networkStale = false;
	}

	entityState_t *gameStateBase = clientEntityStates[ MAX_CLIENTS ][ ENTITYNUM_NONE ];
	if ( gameStateBase != NULL ) {
		idBitMsgDelta deltaMsg;
		gameStateBase->state.BeginReading();
		deltaMsg.InitReading( &gameStateBase->state, NULL, NULL );
		ReadGameStateFromSnapshot( deltaMsg );
		if ( deltaMsg.IsReadOverflowed() ) {
			common->Warning( "ClientReadServerDemoBaseline: truncated game state" );
			return false;
		}
	}
	return true;
}

/*
===============
idGameLocal::ClientReadRepeaterSnapshot
===============
*/
void idGameLocal::ClientReadRepeaterSnapshot( int sequence, const int gameFrame, const int gameTime, const int aheadOfServer, const idBitMsg &msg ) {
	if ( !ClientReadSnapshot( MAX_CLIENTS, sequence, gameFrame, gameTime, 0, aheadOfServer, msg ) ) {
		common->Error( "ClientReadRepeaterSnapshot: malformed snapshot %d", sequence );
		return;
	}
}

/*
===============
idGameLocal::SetDemoFollowClient
===============
*/
bool idGameLocal::SetDemoFollowClient( int clientNum ) {
	if ( !IsServerDemoPlaying() || isServer || !isClient || isRepeater ||
		IsRepeaterDemoPlaying() || localClientNum != MAX_CLIENTS ||
		clientNum < DEMO_FOLLOW_PREV || clientNum >= MAX_CLIENTS ) {
		return false;
	}

	idPlayer *localPlayer = GetLocalPlayer();
	if ( localPlayer == NULL || !localPlayer->IsFakeClient() ||
		localPlayer->entityNumber != ENTITYNUM_NONE || !localPlayer->spectating ) {
		return false;
	}

	if ( clientNum == DEMO_FOLLOW_NEXT || clientNum == DEMO_FOLLOW_PREV ) {
		// Walk the slots from the current camera in either direction. From free
		// flight the walk starts before slot 0 going forward and after the last
		// slot going back.
		const int step = ( clientNum == DEMO_FOLLOW_NEXT ) ? 1 : MAX_CLIENTS - 1;
		int candidate = GetDemoFollowClient();
		if ( candidate < 0 ) {
			candidate = ( clientNum == DEMO_FOLLOW_NEXT ) ? MAX_CLIENTS - 1 : 0;
		}
		clientNum = DEMO_FOLLOW_FREE;
		for ( int attempt = 0; attempt < MAX_CLIENTS; ++attempt ) {
			candidate = ( candidate + step ) % MAX_CLIENTS;
			if ( mpGame.CanSpectatorFollow( localPlayer->entityNumber, candidate ) ) {
				clientNum = candidate;
				break;
			}
		}
	}

	idPlayer *target = NULL;
	if ( clientNum >= 0 ) {
		// Restoring a saved target can occur before the new round's snapshot
		// makes it active again. Eligibility gates explicit cycling above and
		// is rechecked by UpdateSpectating before that POV is presented.
		if ( entities[ clientNum ] == NULL || !entities[ clientNum ]->IsType( idPlayer::GetClassType() ) ) {
			return false;
		}
		target = static_cast< idPlayer * >( entities[ clientNum ] );
	}
	if ( target == NULL && localPlayer->spectator != localPlayer->entityNumber ) {
		// Leave follow at the viewed player's position, as live free-fly does.
		localPlayer->SpectateFreeFly( true );
	}

	followPlayer = clientNum;
	localPlayer->spectating = true;
	localPlayer->spectator = target != NULL ? clientNum : localPlayer->entityNumber;

	if ( target != NULL ) {
		// A seek skips the prediction frames that normally blend crouch height.
		// Present the recorded stance immediately, even while playback is paused.
		target->UpdateEyeHeight( true );
	}

	if ( target != NULL && localPlayer->GetInstance() != target->GetInstance() ) {
		localPlayer->SetInstance( target->GetInstance() );
		mpGame.ServerSetInstance( target->GetInstance() );
	}

	return true;
}

/*
================
idGameLocal::ClientApplySnapshot
================
*/
bool idGameLocal::ClientApplySnapshot( int clientNum, int sequence ) {
	return ApplySnapshot( clientNum, sequence );
}

/*
================
idGameLocal::ClientProcessEntityNetworkEventQueue
================
*/
void idGameLocal::ClientProcessEntityNetworkEventQueue( void ) {
	idEntity			*ent;
	entityNetEvent_t	*event;
	idBitMsg			eventMsg;

	while ( eventQueue.Start() ) {
		event = eventQueue.Start();

		// only process forward, in order
		if ( event->time > time ) {
			break;
		}

		idEntityPtr< idEntity > entPtr;
			
		if ( !entPtr.SetSpawnId( event->spawnId ) ) {
			if( !gameLocal.entities[ event->spawnId & ( ( 1 << GENTITYNUM_BITS ) - 1 ) ] ) {
				// if new entity exists in this position, silently ignore
				NetworkEventWarning( event, "Entity does not exist any longer, or has not been spawned yet." );
			}
		} else if ( ( ent = entPtr.GetEntity() ) == NULL ) {
			// openQ4: an id can pass SetSpawnId and still name no entity; the server's
			// queue tests for it, and this one went on to call through the pointer
			NetworkEventWarning( event, "Entity does not exist any longer, or has not been spawned yet." );
		} else {
			eventMsg.Init( event->paramsBuf, sizeof( event->paramsBuf ) );
			eventMsg.SetSize( event->paramsSize );
			eventMsg.BeginReading();
			if ( !ent->ClientReceiveEvent( event->event, event->time, eventMsg ) ) {
				NetworkEventWarning( event, "unknown event" );
			}
		}

#ifdef _DEBUG
		entityNetEvent_t* freedEvent = eventQueue.Dequeue();
		assert( freedEvent == event );
#else
		eventQueue.Dequeue();
#endif
		eventQueue.Free( event );
	}
}

// RAVEN BEGIN
// bdube: client side hitscan

/*
================
idGameLocal::ClientHitScan
================
*/
void idGameLocal::ClientHitScan( const idBitMsg &msg ) {
	int				hitscanDefIndex;
	idVec3			muzzleOrigin;
	idVec3			dir;
	idVec3			fxOrigin;
	const idDeclEntityDef *decl;
	int				num_hitscans;
	int				i;
	idEntity		*owner;

	assert( isClient );

	hitscanDefIndex = NetworkDeclIndex( DECL_ENTITYDEF, msg.ReadLong() );
	if ( msg.IsReadOverflowed() || hitscanDefIndex < 0 || hitscanDefIndex >= declManager->GetNumDecls( DECL_ENTITYDEF ) ) {
		common->Warning( "idGameLocal::ClientHitScan: invalid or truncated entity def index %d\n", hitscanDefIndex );
		return;
	}
	decl = static_cast< const idDeclEntityDef *>( declManager->DeclByIndex( DECL_ENTITYDEF, hitscanDefIndex ) );
	if ( !decl ) {
		common->Warning( "idGameLocal::ClientHitScan: entity def index %d not found\n", hitscanDefIndex );
		return;
	}
	num_hitscans = decl->dict.GetInt( "hitscans", "1" );

	const int ownerNumber = msg.ReadBits( idMath::BitsForInteger( MAX_CLIENTS ) );
	if ( msg.IsReadOverflowed() || ownerNumber < 0 || ownerNumber >= MAX_CLIENTS ) {
		common->Warning( "idGameLocal::ClientHitScan: invalid or truncated owner %d\n", ownerNumber );
		return;
	}
	owner = entities[ ownerNumber ];

	muzzleOrigin[0] = msg.ReadFloat();
	muzzleOrigin[1] = msg.ReadFloat();
	muzzleOrigin[2] = msg.ReadFloat();
	fxOrigin[0] = msg.ReadFloat();
	fxOrigin[1] = msg.ReadFloat();
	fxOrigin[2] = msg.ReadFloat();
	if ( msg.IsReadOverflowed() ) {
		common->Warning( "idGameLocal::ClientHitScan: truncated hit-scan origin" );
		return;
	}

	// openQ4: this client drew its own shot when it predicted it (rvWeapon::Attack), from the
	// gun it drew then.  This copy is a round trip late and starts where the server had the
	// gun, so it adds only what the prediction leaves to the server: impacts on actors.  A
	// copy with no prediction waiting - one this client mispredicted - draws in full.
	hitscanEffects_t effects = HITSCAN_EFFECTS_ALL;
	if ( ownerNumber == localClientNum && ConsumePredictedHitscan( hitscanDefIndex ) ) {
		effects = HITSCAN_EFFECTS_CONFIRMED;
	}

	// one direction sent per hitscan
	for( i = 0; i < num_hitscans; i++ ) {
		dir = msg.ReadDir( 24 );
		if ( msg.IsReadOverflowed() ) {
			common->Warning( "idGameLocal::ClientHitScan: truncated hit-scan direction" );
			return;
		}
		const idEntity *hit = gameLocal.HitScan( decl->dict, muzzleOrigin, dir, fxOrigin, owner, false, 1.0f, NULL, NULL, effects );
		if ( i == 0 && net_mpHitscanDebug.GetBool() && owner && owner->IsType( idPlayer::GetClassType() ) &&
				static_cast<idPlayer *>( owner )->weapon ) {
			static_cast<idPlayer *>( owner )->weapon->LogHitscanDebug(
				effects == HITSCAN_EFFECTS_CONFIRMED ? "confirmed" : "echo", hitscanDefIndex, fxOrigin, dir, 0, hit );
		}
	}
}

// RAVEN END

/*
================
idGameLocal::ClientProcessReliableMessage
================
*/
void idGameLocal::ClientProcessReliableMessage( int clientNum, const idBitMsg &msg ) {
	int			id;
	idDict		backupSI;
	int			toClient = -1, excludeClient = -1;
	bool		noEffect = false;

	InitLocalClient( clientNum );

	if ( IsServerDemoPlaying() ) {
		if ( msg.GetRemainingReadBits() < 8 ) {
			common->Warning( "Ignoring truncated server-demo reliable route" );
			return;
		}
		int record_type = msg.ReadByte();
		if ( record_type < DEMO_RECORD_CLIENTNUM || record_type >= DEMO_RECORD_COUNT ) {
			common->Warning( "Ignoring invalid server-demo reliable route %d", record_type );
			return;
		}
		// if you need to do some special filtering:
		switch ( record_type ) {
		case DEMO_RECORD_CLIENTNUM: {
			if ( msg.GetRemainingReadBits() < 8 ) {
				common->Warning( "Ignoring truncated targeted server-demo reliable route" );
				return;
			}
			toClient = msg.ReadChar();
			if ( toClient >= 0 && toClient < MAX_CLIENTS ) {
				// reliable was targetted
				if ( toClient != followPlayer ) {
					// we're free flying or following someone else
					if ( !isRepeater ) {
						return;
					} else {
						noEffect = true;
					}
				}
			} else {
				// openQ4 MP-22: a retail server demo sent each player's copy to its own
				// slot as well; the copy for the player the camera follows shows instead
				if ( RetailSnapshotFormat() && toClient == MAX_CLIENTS && followPlayer >= 0 &&
					 followPlayer < MAX_CLIENTS && !isRepeater ) {
					return;
				}
				toClient = -1;
			}
			break;
		}
		case DEMO_RECORD_EXCLUDE: {
			if ( msg.GetRemainingReadBits() < 8 ) {
				common->Warning( "Ignoring truncated excluded server-demo reliable route" );
				return;
			}
			excludeClient = msg.ReadChar(); // may be -1
			if ( excludeClient >= 0 && excludeClient == followPlayer ) {
				if ( !isRepeater ) {
					return;
				} else {
					noEffect = true;
				}
			}
			toClient = -1;
			break;
		}
		case DEMO_RECORD_INSTANCE: {
			if ( msg.GetRemainingReadBits() < 16 ) {
				common->Warning( "Ignoring truncated instance server-demo reliable route" );
				return;
			}
			excludeClient = msg.ReadChar(); // may be -1
			const int routeInstance = msg.ReadChar();
			if ( routeInstance < 0 || routeInstance >= MAX_INSTANCES ||
				 excludeClient < -1 || excludeClient >= MAX_CLIENTS ) {
				common->Warning( "Ignoring invalid instance server-demo reliable route %d/%d", routeInstance, excludeClient );
				return;
			}

			idEntity *viewEntity = NULL;
			if ( followPlayer >= 0 && followPlayer < MAX_CLIENTS ) {
				viewEntity = entities[ followPlayer ];
			}
			if ( viewEntity == NULL ) {
				viewEntity = GetLocalPlayer();
			}
			const bool routeMatches = viewEntity != NULL && viewEntity->GetInstance() == routeInstance &&
				( excludeClient < 0 || excludeClient != followPlayer );
			if ( !routeMatches ) {
				if ( !isRepeater ) {
					return;
				}
				noEffect = true;
			}
			toClient = -1;
			break;
		}
		}
	}

	if ( msg.GetRemainingReadBits() < 8 ) {
		common->Warning( "Ignoring truncated server-demo reliable message" );
		return;
	}
	id = msg.ReadByte();
	if ( id < GAME_RELIABLE_MESSAGE_SPAWN_PLAYER ||
		 id >= GAME_RELIABLE_MESSAGE_COUNT ) {
		common->Warning( "Ignoring invalid reliable game message %d", id );
		return;
	}

	if ( isRepeater ) {
		bool		inhibitRepeater = false;

		switch( id ) {
			case GAME_RELIABLE_MESSAGE_SPAWN_PLAYER: {
				if ( toClient != -1 ) {
					inhibitRepeater = true;
				}
				break;
			}
	
			case GAME_RELIABLE_MESSAGE_PORTALSTATES: {
				inhibitRepeater = true;
				break;
			}
	
			case GAME_RELIABLE_MESSAGE_STARTSTATE: {
				if ( toClient != -1 ) {
					inhibitRepeater = true;
				}
				break;
			}
	
			case GAME_RELIABLE_MESSAGE_GAMESTATE: {
				if ( toClient != -1 ) {
					inhibitRepeater = true;
				}
				break;
			}
	
			case GAME_RELIABLE_MESSAGE_SET_INSTANCE: {
				inhibitRepeater = true;
				break;
			}
	
			case GAME_RELIABLE_MESSAGE_GETVOTEMAPS: {
				inhibitRepeater = true;
				break;
			}
	
			case GAME_RELIABLE_MESSAGE_UNRELIABLE_MESSAGE: {
				inhibitRepeater = true;
				break;
			}

			// openQ4: a notice aimed at one player is not for the viewers
			case GAME_RELIABLE_MESSAGE_CENTERPRINT:
			case GAME_RELIABLE_MESSAGE_ANNOUNCER: {
				if ( toClient != -1 ) {
					inhibitRepeater = true;
				}
				break;
			}

			case GAME_RELIABLE_MESSAGE_MATCH_RESULT:
			case GAME_RELIABLE_MESSAGE_MATCH_VIEW:
			case GAME_RELIABLE_MESSAGE_MATCH_AUTH_CHALLENGE:
			case GAME_RELIABLE_MESSAGE_IDENTITY:
				// These payloads are recipient-specific and must never be copied to
				// an observer/repeater channel.
				inhibitRepeater = true;
				break;

			default:
				break;
		}

// jmarshall - engine has no repeater reliable channel
		//if ( !inhibitRepeater ) {
		//	// pass it along to the connected viewers
		//	if ( excludeClient == -1 ) {
		//		networkSystem->RepeaterSendReliableMessage( -1, msg, IsServerDemoPlaying(), toClient );
		//	} else {
		//		networkSystem->RepeaterSendReliableMessageExcluding( excludeClient, msg, IsServerDemoPlaying() );
		//	}
		//}
// jmarshall end

		if ( noEffect ) {
			return;
		}
	}

	switch( id ) {
		case GAME_RELIABLE_MESSAGE_SPAWN_PLAYER: {
			int client = msg.ReadByte();
			int spawnId = msg.ReadLong();
			if ( client < 0 || client >= MAX_CLIENTS ) {
				common->Warning( "Ignoring invalid spawn-player client %d in server demo", client );
				break;
			}
			if ( !entities[ client ] ) {
				SpawnPlayer( client );
				entities[ client ]->FreeModelDef();
			}
			// fix up the spawnId to match what the server says
			// otherwise there is going to be a bogus delete/new of the client entity in the first ClientReadFromSnapshot
			spawnIds[ client ] = spawnId;
			break;
		}
		case GAME_RELIABLE_MESSAGE_DELETE_ENT: {
			int spawnId = msg.ReadBits( 32 );
			idEntityPtr< idEntity > entPtr;
			if( !entPtr.SetSpawnId( spawnId ) ) {
				break;
			}
			if( entPtr.GetEntity() && entPtr.GetEntity()->entityNumber < MAX_CLIENTS ) {
				mpGame.ResetVoiceState( entPtr.GetEntity()->entityNumber );
				delete entPtr.GetEntity();
				gameLocal.mpGame.UpdatePlayerRanks();
			} else {
				delete entPtr.GetEntity();
			}
			break;
		}
		case GAME_RELIABLE_MESSAGE_CHAT:
		case GAME_RELIABLE_MESSAGE_TCHAT: {
			char name[128];
			char text[128];
			char parm[128];
			msg.ReadString( name, sizeof( name ) );
			msg.ReadString( text, sizeof( text ) );
			msg.ReadString( parm, sizeof( parm ) );
			idStr temp = va( "%s^0: %s%s\n", name, common->GetLocalizedString( text ), parm );
			mpGame.PrintChatLine( temp.c_str(), ( id == GAME_RELIABLE_MESSAGE_TCHAT && gameLocal.IsTeamGame() ) );
			break;
		}
		case GAME_RELIABLE_MESSAGE_DB: {
			msg_evt_t msg_evt = (msg_evt_t)msg.ReadByte();
			int parm1, parm2;
			parm1 = msg.ReadChar( );
			parm2 = msg.ReadChar( );
			mpGame.PrintMessageEvent( -1, msg_evt, parm1, parm2 );
			break;
		}
		case GAME_RELIABLE_MESSAGE_EVENT: {
			entityNetEvent_t *event;

			// allocate new event
			event = eventQueue.Alloc();

			event->spawnId = msg.ReadBits( 32 );
			event->event = msg.ReadByte();
			event->time = msg.ReadLong();
			// openQ4: server originated, so there is no sending client to
			// attribute it to.  idEventQueue::Alloc does not zero the struct.
			event->sender = -1;

			eventQueue.Enqueue( event, idEventQueue::OUTOFORDER_IGNORE );

			event->paramsSize = msg.ReadBits( idMath::BitsForInteger( MAX_EVENT_PARAM_SIZE ) );
			if ( event->paramsSize ) {
				if ( event->paramsSize > MAX_EVENT_PARAM_SIZE ) {
					NetworkEventWarning( event, "invalid param size" );
					return;
				}
				msg.ReadByteAlign();
				msg.ReadData( event->paramsBuf, event->paramsSize );
			}
			break;
		}
		case GAME_RELIABLE_MESSAGE_SERVERINFO: {
			idDict info;
			msg.ReadDeltaDict( info, NULL );
			if ( msg.IsReadOverflowed() ) {
				Warning( "Ignoring malformed reliable server-info dictionary" );
				break;
			}
			SetServerInfo( info );
			break;
		}
		case GAME_RELIABLE_MESSAGE_RESTART: {
			MapRestart();
			break;
		}
		case GAME_RELIABLE_MESSAGE_ROUNDRESTART: {
			// The full restart recreates the gametype state. Between rounds that
			// loses the current phase/round and replays match-start presentation.
			// This payload-free message only restarts the world, as on the server.
			rvGameState *state = mpGame.GetGameState();
			if ( msg.GetRemainingReadBits() != 0 ||
				!MPGameTypeHasAny( gameType, GTF_ROUND ) || state == NULL ||
				state->GetMPGameState() != GAMEON ) {
				Warning( "Ignoring invalid round world restart" );
				break;
			}
			LocalMapRestart();
			common->DPrintf( "MP round world restart: gametype=%d phase=%d statePreserved=%d\n",
				gameType, mpGame.GetGameState()->GetMPGameState(), mpGame.GetGameState() == state );
			break;
		}
		case GAME_RELIABLE_MESSAGE_STARTVOTE: {
			char voteString[ MAX_STRING_CHARS ];
			int clientNum = msg.ReadByte( );
			msg.ReadString( voteString, sizeof( voteString ) );
			mpGame.ClientStartVote( clientNum, voteString );
			break;
		}
		case GAME_RELIABLE_MESSAGE_PRINT: {
			char str[ MAX_PRINT_LEN ] = { '\0' };
			msg.ReadString( str, MAX_PRINT_LEN );
			mpGame.PrintMessage( -1, str );
			break;
		}
		case GAME_RELIABLE_MESSAGE_STARTPACKEDVOTE: {
			voteStruct_t voteData;
			idMultiplayerGame::EmptyVoteStruct( voteData );
			int clientNum = msg.ReadByte();
			// openQ4 MP-22: Quake 4 1.4.2 sent its twelve vote flags in a short
			voteData.m_fieldFlags = RetailSnapshotFormat() ? ( msg.ReadShort() & 0x0fff ) : msg.ReadLong();
			if ( voteData.m_fieldFlags <= 0 || ( voteData.m_fieldFlags & ~VOTEFLAG_ALL ) != 0 ) {
				Warning( "Ignoring packed vote state with invalid field flags 0x%x", voteData.m_fieldFlags );
				break;
			}
			char mapName[256];
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_KICK ) ) {
				voteData.m_kick = msg.ReadByte();
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_MAP ) ) {
				if ( !HasBoundedReliableString( msg, sizeof( mapName ) ) ) {
					Warning( "Ignoring packed vote state with a missing or oversized map token" );
					break;
				}
				msg.ReadString( mapName, sizeof( mapName ) );
				voteData.m_map = mapName;
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_GAMETYPE ) ) {
				voteData.m_gameType = msg.ReadByte();
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_TIMELIMIT ) ) {
				voteData.m_timeLimit = msg.ReadByte();
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_FRAGLIMIT ) ) {
				voteData.m_fragLimit = msg.ReadShort();
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_TOURNEYLIMIT ) ) {
				voteData.m_tourneyLimit = msg.ReadShort();
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_CAPTURELIMIT ) ) {
				voteData.m_captureLimit = msg.ReadShort();
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_BUYING ) ) {
				voteData.m_buying = msg.ReadByte();
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_TEAMBALANCE ) ) {
				voteData.m_teamBalance = msg.ReadByte();
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_CONTROLTIME ) ) {
				voteData.m_controlTime = msg.ReadShort();
			}
			if ( RetailSnapshotFormat() ) {
				// 1.4.2's packed vote ends with the control time
				mpGame.ClientStartPackedVote( clientNum, voteData );
				break;
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_ROUNDLIMIT ) ) {
				voteData.m_roundLimit = msg.ReadShort();
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_RULESET ) ) {
				voteData.m_ruleset = msg.ReadByte();
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_INSTAGIB ) ) {
				voteData.m_instagib = msg.ReadByte();
			}
			if ( 0 != ( voteData.m_fieldFlags & VOTEFLAG_OVERTIME ) ) {
				voteData.m_overtime = msg.ReadShort();
			}
			// openQ4 MP-14/MP-16: the modifier, spectator chat, preset and arena fields
			if ( !idMultiplayerGame::ReadVoteExtensionFields( msg, voteData ) ) {
				Warning( "Ignoring packed vote state with a malformed modifier, preset or arena field" );
				break;
			}
			if ( msg.GetRemainingReadBits() != 0 ) {
				Warning( "Ignoring packed vote state with %d trailing bits", msg.GetRemainingReadBits() );
				break;
			}
			mpGame.ClientStartPackedVote( clientNum, voteData );
			break;
		}
		case GAME_RELIABLE_MESSAGE_UPDATEVOTE: {
			int result = msg.ReadByte( );
			int yesCount = msg.ReadByte( );
			int noCount = msg.ReadByte( );
			int multiVote = msg.ReadByte( );
			if ( result < idMultiplayerGame::VOTE_UPDATE || result > idMultiplayerGame::VOTE_RESET ||
				yesCount < 0 || yesCount > MAX_CLIENTS || noCount < 0 || noCount > MAX_CLIENTS ||
				( multiVote != 0 && multiVote != 1 ) ) {
				Warning( "Ignoring malformed vote update header" );
				break;
			}
			voteStruct_t voteData;
			char mapNameBuffer[256] = { '\0' };
			idMultiplayerGame::EmptyVoteStruct( voteData );
			if ( multiVote ) {
				voteData.m_fieldFlags = RetailSnapshotFormat() ? ( msg.ReadShort() & 0x0fff ) : msg.ReadLong();
				if ( voteData.m_fieldFlags <= 0 || ( voteData.m_fieldFlags & ~VOTEFLAG_ALL ) != 0 ) {
					Warning( "Ignoring vote update with invalid field flags 0x%x", voteData.m_fieldFlags );
					break;
				}
				voteData.m_kick = msg.ReadByte();
				if ( !HasBoundedReliableString( msg, sizeof( mapNameBuffer ) ) ) {
					Warning( "Ignoring vote update with a missing or oversized map token" );
					break;
				}
				msg.ReadString( mapNameBuffer, sizeof( mapNameBuffer ) );
				voteData.m_map = mapNameBuffer;
				voteData.m_gameType = msg.ReadByte();
				voteData.m_timeLimit = msg.ReadByte();
				voteData.m_fragLimit = msg.ReadShort();
				voteData.m_tourneyLimit = msg.ReadShort();
				voteData.m_captureLimit = msg.ReadShort();
				voteData.m_buying = msg.ReadByte();
				voteData.m_teamBalance = msg.ReadByte();
				voteData.m_controlTime = msg.ReadShort();
				// openQ4 MP-22: 1.4.2's vote update ends with the control time
				if ( !RetailSnapshotFormat() ) {
					voteData.m_roundLimit = msg.ReadShort();
					voteData.m_ruleset = msg.ReadByte();
					voteData.m_instagib = msg.ReadByte();
					voteData.m_overtime = msg.ReadShort();
				}
				if ( !RetailSnapshotFormat() && !idMultiplayerGame::ReadVoteExtensionFields( msg, voteData ) ) {
					Warning( "Ignoring vote update with a malformed modifier, preset or arena field" );
					break;
				}
				if ( ( voteData.m_ruleset != 0 && voteData.m_ruleset != 1 ) || ( voteData.m_instagib != 0 && voteData.m_instagib != 1 ) ) {
					Warning( "Ignoring vote update with invalid casual vote values" );
					break;
				}
				if ( ( ( voteData.m_fieldFlags & VOTEFLAG_KICK ) != 0 && ( voteData.m_kick < 0 || voteData.m_kick >= MAX_CLIENTS ) ) ||
					( ( voteData.m_fieldFlags & VOTEFLAG_GAMETYPE ) != 0 && ( voteData.m_gameType < 0 || voteData.m_gameType >= MPVoteGameTypeCount() ) ) ||
					( voteData.m_buying < BUYING_MODE_OFF || voteData.m_buying >= BUYING_MODE_COUNT ) ||
					( voteData.m_teamBalance != 0 && voteData.m_teamBalance != 1 ) ) {
					Warning( "Ignoring vote update with invalid typed values" );
					break;
				}
			}
			if ( msg.GetRemainingReadBits() != 0 ) {
				Warning( "Ignoring vote update with %d trailing bits", msg.GetRemainingReadBits() );
				break;
			}
			mpGame.ClientUpdateVote( (idMultiplayerGame::vote_result_t)result, yesCount, noCount, voteData );
			break;
		}
		case GAME_RELIABLE_MESSAGE_PORTALSTATES: {
			int numPortals = msg.ReadLong();
			assert( numPortals == gameRenderWorld->NumPortals() );
			for ( int i = 0; i < numPortals; i++ ) {
				gameRenderWorld->SetPortalState( (qhandle_t) (i+1), msg.ReadBits( NUM_RENDER_PORTAL_BITS ) );
			}
			break;
		}
		case GAME_RELIABLE_MESSAGE_PORTAL: {
			qhandle_t portal = msg.ReadLong();
			int blockingBits = msg.ReadBits( NUM_RENDER_PORTAL_BITS );
			assert( portal > 0 && portal <= gameRenderWorld->NumPortals() );
			gameRenderWorld->SetPortalState( portal, blockingBits );
			break;
		}
		case GAME_RELIABLE_MESSAGE_STARTSTATE: {
			mpGame.ClientReadStartState( msg );
			break;
		}
		case GAME_RELIABLE_MESSAGE_ITEMACQUIRESOUND:
			mpGame.PlayGlobalItemAcquireSound( NetworkDeclIndex( DECL_ENTITYDEF, msg.ReadBits( gameLocal.entityDefBits ) ) );
			break;
		case GAME_RELIABLE_MESSAGE_DEATH: {
			int attackerEntityNumber = msg.ReadByte( );
			int attackerScore = -1;
			if( attackerEntityNumber >= 0 && attackerEntityNumber < MAX_CLIENTS ) {
				attackerScore = msg.ReadBits( ASYNC_PLAYER_FRAG_BITS );
			}
			int victimEntityNumber = msg.ReadByte( );
			int victimScore = -1;
			if( victimEntityNumber >= 0 && victimEntityNumber < MAX_CLIENTS ) {
				victimScore = msg.ReadBits( ASYNC_PLAYER_FRAG_BITS );
			}

			// openQ4: the server names a player or sends 255. Anything else off the
			// wire used to be cast to idPlayer whatever it was, and its entity
			// number then indexed the MAX_CLIENTS-long userInfo and playerState.
			idPlayer* attacker = NULL;
			idPlayer* victim = NULL;
			if ( attackerEntityNumber < MAX_CLIENTS && gameLocal.entities[ attackerEntityNumber ] != NULL &&
				 gameLocal.entities[ attackerEntityNumber ]->IsType( idPlayer::GetClassType() ) ) {
				attacker = static_cast<idPlayer*>( gameLocal.entities[ attackerEntityNumber ] );
			}
			if ( victimEntityNumber < MAX_CLIENTS && gameLocal.entities[ victimEntityNumber ] != NULL &&
				 gameLocal.entities[ victimEntityNumber ]->IsType( idPlayer::GetClassType() ) ) {
				victim = static_cast<idPlayer*>( gameLocal.entities[ victimEntityNumber ] );
			}
			int methodOfDeath = msg.ReadByte( );
			bool quadKill = msg.ReadBits( 1 ) != 0;
			if ( IsServerDemoPlaying() ) {
				demoAutoTrack.NoteKill( attackerEntityNumber, victimEntityNumber, GetDemoFollowClient(), time );
			}
			// openQ4 MP-22: a recording of this player's view marks each kill it hears of
			if ( !isServer && victim != NULL && networkSystem->ServerIsMVDRecording() ) {
				serverMVDMarker_t marker;
				memset( &marker, 0, sizeof( marker ) );
				marker.kind = SERVER_MVD_MARKER_KILL;
				marker.client = attacker != NULL && attacker != victim ? attacker->entityNumber : -1;
				marker.instance = victim->GetInstance();
				marker.value0 = victim->entityNumber;
				marker.value1 = methodOfDeath;
				networkSystem->ServerRecordMVDMarker( marker );
			}
		
			mpGame.ReceiveDeathMessage( attacker, attackerScore, victim, victimScore, methodOfDeath, quadKill );
			break;
		}
		case GAME_RELIABLE_MESSAGE_GAMESTATE: {
			mpGame.GetGameState()->ReceiveState( msg );
			break;
		}
		case GAME_RELIABLE_MESSAGE_STAT: {
			// openQ4: rvStatManager::ReceiveStat re-reads the client byte itself
			// and indexes playerStats[] with it unchecked, so bound check it here
			// without disturbing the read position.
			int savedReadCount, savedReadBit;
			msg.SaveReadState( savedReadCount, savedReadBit );
			const int statClient = msg.ReadByte();
			msg.RestoreReadState( savedReadCount, savedReadBit );
			if ( statClient < 0 || statClient >= MAX_CLIENTS ) {
				Warning( "Ignoring stat message for invalid client num %d", statClient );
				break;
			}
			statManager->ReceiveStat( msg );
			break;
		}
		case GAME_RELIABLE_MESSAGE_ALL_STATS: {
			statManager->ReceiveAllStats( msg );
			break;
		}
		case GAME_RELIABLE_MESSAGE_SET_INSTANCE: {
			if ( !IsServerDemoPlaying() ) {
				mpGame.ClientSetInstance( msg );
			}
			break;
		}
		case GAME_RELIABLE_MESSAGE_INGAMEAWARD: {
			statManager->ReceiveInGameAward( msg );
			break;
		}
		case GAME_RELIABLE_MESSAGE_GETADMINBANLIST: {
			// openQ4 MP-18: no server sends its ban list any more; a remote
			// administrator reads it with 'rcon banlist'
			break;
		}
		case GAME_RELIABLE_MESSAGE_GETVOTEMAPS: {
			mpGame.ReadMapList( msg );
			break;
		}
		case GAME_RELIABLE_MESSAGE_UNRELIABLE_MESSAGE: {
			idBitMsg unreliable;
			unreliable.Init( msg.GetReadData(), msg.GetRemainingData() );
			unreliable.SetSize( msg.GetRemainingData() );
			unreliable.BeginReading();
			ProcessUnreliableMessage( unreliable );
			break;
		}
		case GAME_RELIABLE_MESSAGE_EVENT_ACK: {
			clientAckSequence = msg.ReadLong();
			// make sure the local client is waiting for a ACK
			// if he's not, this may indicate a problem
			idPlayer *p = GetLocalPlayer();
			if ( net_clientPredictWeaponSwitch.GetBool() && p != NULL && !p->IsWaitingForPredictAck() ) {
				common->Warning( "frame %d: got EVENT_ACK seq %d while not expecting one", gameLocal.framenum, clientAckSequence );
			}
			break;
		}
		// openQ4 BEGIN
		case GAME_RELIABLE_MESSAGE_CENTERPRINT: {
			mpGame.ReceiveCenterPrint( msg );
			break;
		}
		case GAME_RELIABLE_MESSAGE_ANNOUNCER: {
			mpGame.ReceiveAnnouncer( msg );
			break;
		}
		case GAME_RELIABLE_MESSAGE_MATCH_RESULT:
			mpGame.ClientReceiveMatchOperationResult( msg );
			break;
		case GAME_RELIABLE_MESSAGE_MATCH_VIEW:
			mpGame.ClientReceiveMatchView( msg );
			break;
		case GAME_RELIABLE_MESSAGE_MATCH_AUTH_CHALLENGE:
			mpGame.ClientReceiveRefereeAuthChallenge( msg );
			break;
		case GAME_RELIABLE_MESSAGE_IDENTITY:
			// openQ4 MP-17: the server asks for this player's identity
			mpGame.ClientReceiveIdentityChallenge( msg );
			break;
		// openQ4 END
		default: {
			Error( "Unknown server->client reliable message: %d", id );
			break;
		}
	}
}

// RAVEN BEGIN
/*
================
idGameLocal::ClientRun
Called once each client render frame (before any ClientPrediction frames have been run)
================
*/
void idGameLocal::ClientRun( void ) {
	if( isMultiplayer ) {
		UpdateAdvertisedRemoteExtrapolation();
		mpGame.ClientRun();
	}
}

/*
================
idGameLocal::ClientEndFrame
Called once each client render frame (after all ClientPrediction frames have been run)
================
*/
void idGameLocal::ClientEndFrame( void ) {
	if( isMultiplayer ) {
		mpGame.ClientEndFrame();
	}
}

/*
================
idGameLocal::ProcessRconReturn
================
*/
void idGameLocal::ProcessRconReturn( bool success )	{
	if( isMultiplayer )	{
		mpGame.ProcessRconReturn( success );
	}
}

/*
================
idGameLocal::ResetGuiRconStatus
================
*/
void idGameLocal::ResetRconGuiStatus( void )	{
	if( isMultiplayer )	{
		mpGame.ResetRconGuiStatus( );
	}
}

// RAVEN END

/*
================
idGameLocal::ClientPrediction
server demos: clientNum == MAX_CLIENTS
================
*/
gameReturn_t idGameLocal::ClientPrediction( int clientNum, const usercmd_t *clientCmds, bool lastPredictFrame, ClientStats_t *cs ) {
	// openQ4: predicted client frames run corpse and debris physics too (PHYS-01 telemetry)
	OpenQ4Physics::GetTelemetry().FrameTick( framenum + 1 );
	idEntity		*ent;
	idPlayer		*player;	// may be NULL when predicting for a server demo
	gameReturn_t	ret;

	ret.sessionCommand[ 0 ] = '\0';

	if ( g_showDebugHud.GetInteger() && net_entsInSnapshot && net_snapshotSize) {
		gameDebug.SetInt( "snap_ents", net_entsInSnapshot );
		gameDebug.SetInt( "snap_size", net_snapshotSize );

		net_entsInSnapshot = 0;
		net_snapshotSize = 0; 
	}

	if ( clientNum == localClientNum ) {
		gameDebug.BeginFrame();
		gameLog->BeginFrame( time );
	}

	isLastPredictFrame = lastPredictFrame;

	InitLocalClient( clientNum );

	// update the game time
	framenum++;
	previousTime = time;
	time += GetMSec();

	// update the real client time and the new frame flag
	int newFrameMsec = 0;
	if ( time > realClientTime ) {
		// A snapshot correction can skip predicted frames.  Paused clocks
		// must follow the entire forward step, while replayed frames must
		// not shift them again.  The first frame after a join/demo seek has
		// no previous prediction baseline and advances by one tick only.
		newFrameMsec = realClientTime > 0 ? time - realClientTime : GetMSec();
		realClientTime = time;
		isNewFrame = true;
	} else {
		isNewFrame = false;
		clientReplayTics++;
	}

	if ( clientNum == MAX_CLIENTS ) {
		player = gameLocal.GetLocalPlayer();
		assert( player );
	} else {
		player = static_cast<idPlayer *>( entities[clientNum] );
	}

	// check for local client lag
	if ( player ) {
		if ( networkSystem->ClientGetTimeSinceLastPacket() >= net_clientMaxPrediction.GetInteger() ) {
			player->isLagged = true;
		} else {
			player->isLagged = false;
		}
	}

	if ( cs ) {
		cs->isLastPredictFrame = isLastPredictFrame;
		cs->isLagged = player ? player->isLagged : false;
		cs->isNewFrame = isNewFrame;
	}

	// set the user commands for this frame
	usercmds = clientCmds;

	const bool competitiveGameplayFrozen = mpGame.IsGameplayFrozen();
	if ( competitiveGameplayFrozen ) {
		if ( isNewFrame ) {
			const int frameMsec = newFrameMsec;
			// Prediction can replay the same snapshot more than once.  Rebase the
			// complete set of gameplay deadline owners only on the first pass for
			// a real client frame, matching the authoritative server traversal.
			// Suppress per-player command presentation during this pass; the view
			// owner is presented once below after the original command array is
			// restored.
			// The HUD clock's origin moves with the frozen frame, as on the server.
			mpGame.RebaseClientPauseClock( frameMsec );
			const usercmd_t *savedUsercmds = usercmds;
			usercmds = NULL;
			for ( ent = spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
				currentThinkingEntity = ent;
				ent->ThinkMatchPaused( frameMsec );
				currentThinkingEntity = NULL;
			}
			usercmds = savedUsercmds;

			// Posted gameplay events use absolute game time.  Move them with the
			// frozen simulation instead of servicing them while client time runs.
			if ( !idEvent::ShiftEvents( frameMsec ) ) {
				Warning( "competitive client pause could not rebase the posted-event queue" );
			}
		}

		// Keep camera angles, HUD and render-view state responsive on every
		// prediction pass without advancing movement, weapons, effects or other
		// gameplay.  Repeaters render through their followed player; normal
		// server demos render through the fake free-view player.
		idPlayer *pauseViewPlayer = player;
		if ( clientNum == MAX_CLIENTS && isRepeater ) {
			pauseViewPlayer = NULL;
			const int demoFollowClient = GetDemoFollowClient();
			if ( demoFollowClient >= 0 && demoFollowClient < MAX_CLIENTS &&
				 entities[ demoFollowClient ] != NULL &&
				 entities[ demoFollowClient ]->IsType( idPlayer::GetClassType() ) ) {
				pauseViewPlayer = static_cast< idPlayer * >( entities[ demoFollowClient ] );
			}
		}
		if ( pauseViewPlayer != NULL ) {
			pauseViewPlayer->ThinkMatchPaused( 0 );
		}

		// Preserve the free-view routing state without invoking the normal
		// prediction path, which would move the spectator and service gameplay.
		if ( clientNum == MAX_CLIENTS && player && isNewFrame && !isRepeater ) {
			assert( player->IsFakeClient() );
			player->UpdateVisuals();
			assert( player->spectating );

			if ( player->spectator != player->entityNumber ) {
				followPlayer = player->spectator;
			} else {
				followPlayer = -1;
			}
		}
	} else {
		// run prediction on all entities from the last snapshot
		for ( ent = snapshotEntities.Next(); ent != NULL; ent = ent->snapshotNode.Next() ) {
			// don't force TH_PHYSICS on, only call ClientPredictionThink if thinkFlags != 0
			// it's better to synchronize TH_PHYSICS on specific entities when needed ( movers may be trouble )
			if ( ent->thinkFlags != 0 ) {
				ent->ClientPredictionThink();
			}
		}

		// run client entities
		if ( isNewFrame ) {
			// rjohnson: only run the entire logic when it is a new frame
			rvClientEntity* cent;
			for ( cent = clientSpawnedEntities.Next(); cent != NULL; cent = cent->spawnNode.Next() ) {
				cent->Think();
			}
		}

		// freeView
		if ( clientNum == MAX_CLIENTS && player && isNewFrame && !isRepeater) {
			assert( player->IsFakeClient() );

			if ( IsServerDemoPlaying() ) {
				UpdateDemoAutoTrack( player, clientCmds[ clientNum ] );
			}
			player->ClientPredictionThink();
			player->UpdateVisuals();

			assert( player->spectating );

			if ( player->spectator != player->entityNumber ) {
				followPlayer = player->spectator;
			} else {
				followPlayer = -1;
			}
		}

		// service any pending events
		idEvent::ServiceEvents();

		// openQ4: replicated entity events are otherwise only drained at the end of
		// ClientReadSnapshot, so anything still queued because its server timestamp
		// had not been reached yet waits a whole snapshot interval - 50ms at the
		// default net_serverSnapshotDelay - before it fires.  That is a client behind
		// the server, which is exactly when its combat feedback is already thin.  The
		// queue is time ordered and the drain stops at the first event in the future,
		// so running it again here only ever releases events whose time has come.
		if ( isNewFrame ) {
			ClientProcessEntityNetworkEventQueue();
		}
	}

	if ( isNewFrame && lastPredictFrame ) {
		// authoritative transforms are final for this tic; snapshot them so
		// presentation frames can draw movers at the camera's presentation time
		SamplePresentationEntityPoses();
	}

	// show any debug info for this frame
	if ( isNewFrame ) {
		RunDebugInfo();
		D_DrawDebugLines();
	}

	if ( clientNum == localClientNum && isNewFrame && lastPredictFrame ) {
		CheckAutoExecAfterMapLoad();
	}

	if ( sessionCommand.Length() ) {
		idStr::Copynz( ret.sessionCommand, sessionCommand, sizeof( ret.sessionCommand ) );
		sessionCommand = "";
	}

// RAVEN BEGIN
// ddynerman: client logging/debugging
	if ( clientNum == localClientNum ) {
		gameDebug.EndFrame();
		gameLog->EndFrame();
	}
// RAVEN END

	g_simpleItems.ClearModified();
	return ret;
}

/*
===============
idGameLocal::Tokenize
===============
*/
void idGameLocal::Tokenize( idStrList &out, const char *in ) {
	char buf[ MAX_STRING_CHARS ];
	char *token, *next;

	idStr::Copynz( buf, in, MAX_STRING_CHARS );
	token = buf;
	next = strchr( token, ';' );
	while ( token ) {
		if ( next ) {
			*next = '\0';
		}
		idStr::ToLower( token );
		out.Append( token );
		if ( next ) {
			token = next + 1;
			next = strchr( token, ';' );
		} else {
			token = NULL;
		}		
	}
}

/*
===============
idGameLocal::DownloadRequest
===============
*/
bool idGameLocal::DownloadRequest( const char *IP, const char *guid, const char *paks, char urls[ MAX_STRING_CHARS ] ) {
	if ( !cvarSystem->GetCVarInteger( "net_serverDownload" ) ) {
		return false;
	}
	if ( cvarSystem->GetCVarInteger( "net_serverDownload" ) == 1 ) {
		// 1: single URL redirect
		if ( !strlen( cvarSystem->GetCVarString( "si_serverURL" ) ) ) {
			common->Warning( "si_serverURL not set" );
			return false;
		}
		idStr::snPrintf( urls, MAX_STRING_CHARS, "1;%s", cvarSystem->GetCVarString( "si_serverURL" ) );
		return true;
	} else {
		// 2: table of pak URLs
		// 3: table of pak URLs with built-in http server
		// first token is the game pak if requested, empty if not requested by the client
		// there may be empty tokens for paks the server couldn't pinpoint - the order matters
		idStr 		reply = "2;";
		idStrList	dlTable, pakList;
		bool		matchAll = false;
		int			i, j;

		if ( !idStr::Icmp( cvarSystem->GetCVarString( "net_serverDlTable" ), "*" ) ) {
			matchAll = true;
		} else {
			Tokenize( dlTable, cvarSystem->GetCVarString( "net_serverDlTable" ) );
		}
		Tokenize( pakList, paks );

		for ( i = 0; i < pakList.Num(); i++ ) {
			if ( i > 0 ) {
				reply += ";";
			}
			if ( pakList[ i ][ 0 ] == '\0' ) {
				if ( i == 0 ) {
					// pak 0 will always miss when client doesn't ask for game bin
					common->DPrintf( "no game pak request\n" );
				} else {
					common->DPrintf( "no pak %d\n", i );
				}
				continue;
			}

			idStr url = cvarSystem->GetCVarString( "net_serverDlBaseURL" );

			if ( !url.Length() ) {
				if ( cvarSystem->GetCVarInteger( "net_serverDownload" ) == 2 ) {
					common->Warning( "net_serverDownload == 2 and net_serverDlBaseURL not set" );
				} else {
					url = cvarSystem->GetCVarString( "net_httpServerBaseURL" );
				}
			}
			
			if ( matchAll ) {
				url.AppendPath( pakList[i] );
				reply += url;
				common->Printf( "download for %s: %s\n", IP, url.c_str() );
			} else {
				for ( j = 0; j < dlTable.Num(); j++ ) {
					if ( !pakList[ i ].Icmp( dlTable[ j ] ) ) {
						break;
					}
				}
				if ( j == dlTable.Num() ) {
					common->Printf( "download for %s: pak not matched: %s\n", IP, pakList[ i ].c_str() );
				} else {
					url.AppendPath( dlTable[ j ] );
					reply += url;
					common->Printf( "download for %s: %s\n", IP, url.c_str() );
				}
			}
		}

		idStr::Copynz( urls, reply, MAX_STRING_CHARS );
		return true;
	}
}

/*
===============
idGameLocal::HTTPRequest
===============
*/
bool idGameLocal::HTTPRequest( const char *IP, const char *file, bool isGamePak ) {
	idStrList	dlTable;
	int			i;

	if ( !idStr::Icmp( cvarSystem->GetCVarString( "net_serverDlTable" ), "*" ) ) {
		return true;
	}

	Tokenize( dlTable, cvarSystem->GetCVarString( "net_serverDlTable" ) );
	while ( *file == '/' ) ++file; // net_serverDlTable doesn't include the initial /

	for ( i = 0; i < dlTable.Num(); i++ ) {
		if ( !dlTable[ i ].Icmp( file ) ) {
			return true;
		}
	}

	return false;
}

/*
===============
idEventQueue::Alloc
===============
*/
entityNetEvent_t* idEventQueue::Alloc() {
	entityNetEvent_t* event = eventAllocator.Alloc();
	event->prev = NULL;
	event->next = NULL;
	return event;
}

/*
===============
idEventQueue::Free
===============
*/
void idEventQueue::Free( entityNetEvent_t *event ) {
	// should only be called on an unlinked event!
	assert( !event->next && !event->prev );
	eventAllocator.Free( event );
}

/*
===============
idEventQueue::Shutdown
===============
*/
void idEventQueue::Shutdown() {
	eventAllocator.Shutdown();
	Init();
}

/*
===============
idEventQueue::Init
===============
*/
void idEventQueue::Init( void ) {
	start = NULL;
	end = NULL;
}

/*
===============
idEventQueue::Dequeue
===============
*/
entityNetEvent_t* idEventQueue::Dequeue( void ) {
	entityNetEvent_t* event = start;
	if ( !event ) {
		return NULL;
	}

	start = start->next;

	if ( !start ) {
		end = NULL;
	} else {
		start->prev = NULL;
	}

	event->next = NULL;
	event->prev = NULL;

	return event;
}

/*
===============
idEventQueue::RemoveLast
===============
*/
entityNetEvent_t* idEventQueue::RemoveLast( void ) {
	entityNetEvent_t *event = end;
	if ( !event ) {
		return NULL;
	}

	end = event->prev;

	if ( !end ) {
		start = NULL;
	} else {
		end->next = NULL;		
	}

	event->next = NULL;
	event->prev = NULL;

	return event;
}

/*
===============
idEventQueue::Enqueue
===============
*/
void idEventQueue::Enqueue( entityNetEvent_t *event, outOfOrderBehaviour_t behaviour ) {
	if ( behaviour == OUTOFORDER_IGNORE ) {
		// warn if an event comes in with a time before the last one in queue
		// as that would cause the one we're adding to get delayed processing
		if ( end && end->time > event->time ) {
			common->Warning( "new event with id %d (time %d) is earlier than head event with id %d (time %d) - is OUTOFORDER_SORT needed?", event->event, event->time, end->event, end->time );
		}
	}

	if ( behaviour == OUTOFORDER_DROP ) {
		// go backwards through the queue and determine if there are
		// any out-of-order events
		while ( end && end->time > event->time ) {
			entityNetEvent_t *outOfOrder = RemoveLast();
			common->Warning( "new event with id %d (time %d) caused removal of event with id %d (time %d), game time = %d", event->event, event->time, outOfOrder->event, outOfOrder->time, gameLocal.time );
			Free( outOfOrder );
		}
	} else if ( behaviour == OUTOFORDER_SORT && end != NULL ) {
		entityNetEvent_t *cur = end;
		// iterate until we find a time < the new event's
		while ( cur && cur->time > event->time ) {
			cur = cur->prev;
		}
		if ( cur == end ) {
			end = event;
		}
		if ( !cur ) {
			// add to start
			event->next = start;
			event->prev = NULL;
			start->prev = event;	// start cannot be NULL because end is already != NULL
			start = event;
		} else {
			// insert
			event->prev = cur;
			event->next = cur->next;
			if ( cur->next != NULL ) {	// insert possibly at the end so have to check that
				cur->next->prev = event;
			}
			cur->next = event;
		}
		return;
	} 

	// add the new event
	event->next = NULL;
	event->prev = NULL;

	if ( end ) {
		end->next = event;
		event->prev = end;
	} else {
		start = event;
	}
	end = event;
}

// RAVEN BEGIN
// shouchard:  ban list stuff here
// openQ4 MP-18: Quake 4 keyed this list to a GUID no client sends, so a ban
// matched nobody. Bans are address ranges with an expiry and a reason
// (mp/Moderation.h), kept as text in banlist.txt (mp/ModerationFile.h); text
// mutes, the chat flood window and the admin log live here with them.

#define MP_MOD_ADMIN_LOG_FILENAME	"logs/admin.ndjson"
// refused connections reach the log this often for one ban, and this many times a minute in all
static const int MP_MOD_REFUSAL_LOG_BURST = 30;
// how often a silenced or muted sender is told why their line went nowhere
static const int MP_MOD_NOTICE_MSEC = 10000;
// the reason the engine's own "kick <slot>" drops a client with
#define MP_MOD_ENGINE_KICK_REASON	"#str_07134"
// how often one refused address is written to the admin log
static const int MP_MOD_REFUSAL_LOG_SECONDS = 60;

static mpModTime_t ModerationNow( void ) {
	return static_cast<mpModTime_t>( time( NULL ) );
}

/*
================
idGameLocal::LoadBanList
================
*/
void idGameLocal::LoadBanList() {
	const bool reload = banListLoaded && modBans != NULL;
	mpModBanList_t *loaded = new mpModBanList_t;
	idStrList kept;
	mpModBanFileResult_t result;
	MPModLoadBanFile( *loaded, kept, result );
	banListLoaded = true;
	modBanFileWarned = false;
	if ( result.unreadable ) {
		common->Warning( "ban list: %s was not taken: %s. It is left as it is and nothing is saved over it; correct it and run 'banlist reload'",
			MP_MOD_BANLIST_FILENAME, result.why );
	}
	if ( reload && ( result.unreadable || !result.found ) ) {
		// A reload that found nothing it could use. The bans in memory still hold: a
		// file an editor has open, or a slip of the hand, must not lift every ban.
		delete loaded;
		if ( !result.found ) {
			common->Printf( "ban list: %s is not there; the %d ban(s) in memory still hold and are written back with the next change\n",
				MP_MOD_BANLIST_FILENAME, modBans->count );
			banListChanged = true;
		}
		return;
	}
	delete modBans;
	modBans = loaded;
	modBanKeptLines = kept;
	banListChanged = result.rewrite;
	if ( result.legacy > 0 ) {
		common->Printf( "ban list: %d Quake 4 GUID ban(s) in %s are kept as comments; they never matched a player\n", result.legacy, MP_MOD_BANLIST_FILENAME );
	}
	if ( result.overflow > 0 ) {
		common->Warning( "ban list: %d ban(s) past the first %d in %s are not enforced", result.overflow, MP_MOD_MAX_BANS, MP_MOD_BANLIST_FILENAME );
	}
	if ( result.kept > 0 ) {
		common->Printf( "ban list: %d line(s) of %s were not understood and are kept as written\n", result.kept, MP_MOD_BANLIST_FILENAME );
	}
	if ( result.unreadable ) {
		// what was read of it is enforced; the file itself is the operator's to mend
		return;
	}
	if ( MPModBanPurgeExpired( *modBans, ModerationNow() ) > 0 ) {
		banListChanged = true;
	}
	SaveBanList();
}

/*
================
idGameLocal::SaveBanList
================
*/
void idGameLocal::SaveBanList() {
	if ( !banListChanged || modBans == NULL ) {
		return;
	}
	if ( MPModBanFileBlocked() ) {
		// said once for each load that failed: the menus ask for the list every frame
		if ( !modBanFileWarned ) {
			modBanFileWarned = true;
			common->Warning( "ban list: not saved. %s could not be used when it was read and is not written over; bans made now last until the server stops. Correct the file and run 'banlist reload'",
				MP_MOD_BANLIST_FILENAME );
		}
		return;
	}
	if ( !MPModSaveBanFile( *modBans, modBanKeptLines ) ) {
		common->Warning( "ban list: %s could not be written", MP_MOD_BANLIST_FILENAME );
		return;
	}
	banListChanged = false;
}

/*
================
idGameLocal::FlushBanList
================
*/
void idGameLocal::FlushBanList() {
	delete modBans;
	modBans = NULL;
	modBanKeptLines.Clear();
	banListLoaded = false;
	banListChanged = false;
}

/*
================
idGameLocal::FindBan
The ban in force that covers this address, or NULL.
================
*/
const mpModBan_t *idGameLocal::FindBan( const mpModAddress_t &address ) {
	if ( !banListLoaded ) {
		LoadBanList();
	}
	const int index = MPModBanFind( *modBans, address, ModerationNow() );
	return index >= 0 ? &modBans->bans[ index ] : NULL;
}

/*
================
idGameLocal::AddBan
Returns the ban's row, or -1 for a range that is too wide or a full list.
================
*/
int idGameLocal::AddBan( const mpModAddress_t &address, int prefixBits, mpModTime_t durationSeconds, const char *name, const char *reason ) {
	if ( !banListLoaded ) {
		LoadBanList();
	}
	const int index = MPModBanAdd( *modBans, address, prefixBits, ModerationNow(), durationSeconds, name, reason );
	if ( index >= 0 ) {
		banListChanged = true;
		SaveBanList();
	}
	return index;
}

/*
================
idGameLocal::RemoveBan
================
*/
bool idGameLocal::RemoveBan( int index ) {
	if ( !banListLoaded ) {
		LoadBanList();
	}
	if ( !MPModBanRemove( *modBans, index ) ) {
		return false;
	}
	banListChanged = true;
	SaveBanList();
	return true;
}

/*
================
idGameLocal::RemoveGuidFromBanList
The menus name a ban by the text PopulateBanList gave them. That was a GUID in
Quake 4; it is the banned range now.
================
*/
void idGameLocal::RemoveGuidFromBanList( const char *guid ) {
	if ( guid == NULL ) {
		return;
	}
	if ( !banListLoaded ) {
		LoadBanList();
	}
	// a range, or (MP-17) "id:" and an identity
	const int index = MPModBanFindTarget( *modBans, guid );
	if ( index >= 0 ) {
		// the campaign mode's Create Server page never wrote the admin log
		if ( IsMultiplayerMode() ) {
			ModerationLogBan( "unban", "menu", modBans->bans[ index ] );
		}
		RemoveBan( index );
	}
}

/*
================
idGameLocal::AddGuidToBanList
Part of the engine's game interface. No client sends a GUID, so there is
nothing to add; bans are made by address (mp/Moderation.h).
================
*/
void idGameLocal::AddGuidToBanList( const char *guid ) {
}

/*
================
idGameLocal::RegisterClientGuid
================
*/
void idGameLocal::RegisterClientGuid( int clientNum, const char *guid ) {
	assert( clientNum >= 0 && clientNum < MAX_CLIENTS );
	assert( guid );
	memset( clientGuids[ clientNum ], 0, CLIENT_GUID_LENGTH ); // just in case
	idStr::Copynz( clientGuids[ clientNum ], guid, CLIENT_GUID_LENGTH );
}

/*
================
idGameLocal::GetBanListCount
Bans that ran out leave the list here, so a menu never shows one.
================
*/
int idGameLocal::GetBanListCount() {
	if ( !banListLoaded ) {
		LoadBanList();
	}
	if ( MPModBanPurgeExpired( *modBans, ModerationNow() ) > 0 ) {
		banListChanged = true;
		SaveBanList();
	}
	return modBans->count;
}

/*
================
idGameLocal::GetBan
================
*/
const mpModBan_t *idGameLocal::GetBan( int entry ) {
	if ( !banListLoaded ) {
		LoadBanList();
	}
	if ( entry < 0 || entry >= modBans->count ) {
		return NULL;
	}
	return &modBans->bans[ entry ];
}

/*
================
idGameLocal::GetBanListEntry
The row as the menus read it. The result is valid until the next call.
================
*/
const mpBanInfo_t* idGameLocal::GetBanListEntry( int entry ) {
	static mpBanInfo_t info;
	const mpModBan_t *ban = GetBan( entry );
	if ( ban == NULL ) {
		return NULL;
	}
	idStr::Copynz( info.name, ban->name, sizeof( info.name ) );
	MPModBanTarget( *ban, info.guid, sizeof( info.guid ) );
	idStr::Copynz( info.reason, ban->reason, sizeof( info.reason ) );
	info.expires = ban->expires;
	return &info;
}

/*
================
idGameLocal::GetGuidByClientNum
================
*/
const char *idGameLocal::GetGuidByClientNum( int clientNum ) {
	assert( clientNum >= 0 && clientNum < numClients );

	return clientGuids[ clientNum ];
}

// mekberg: so we can populate ban list outside of multiplayer game
/*
===================
idGameLocal::PopulateBanList
===================
*/
void idGameLocal::PopulateBanList( idUserInterface* hud ) {
	if ( !hud ) {
		return;
	}

	int bans = GetBanListCount();
	for ( int i = 0; i < bans; i++ ) {
		const mpBanInfo_t * banInfo = GetBanListEntry( i );
		hud->SetStateString( va( "sa_banList_item_%d", i ), va( "%d:  %s\t%s", i+1, banInfo->name, banInfo->guid ) );
	}
	hud->DeleteStateVar( va( "sa_banList_item_%d", bans ) );
	hud->SetStateString( "sa_banList_sel_0", "-1" );
	// used to trigger a redraw, was slow, and doesn't seem to do anything so took it out. fixes #13675
	hud->StateChanged( gameLocal.time, false );
}

/*
================
idGameLocal::GetClientModAddress
A connected client's address as the engine has it. The family is 0 for a bot
and for the listen server's own player: neither has an address to ban.
================
*/
const mpModAddress_t &idGameLocal::GetClientModAddress( int clientNum ) const {
	static mpModAddress_t none;
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return none;
	}
	return clientModAddress[ clientNum ];
}

/*
================
idGameLocal::ModerationWriteLog
One JSON object a line, appended to logs/admin.ndjson in the save folder.
================
*/
void idGameLocal::ModerationWriteLog( mpModLogLine_t &line ) {
	// a console line that came by rcon is logged with where it came from; the
	// engine publishes that while the command runs (net_serverRconFrom)
	const char *rconFrom = cvarSystem->GetCVarString( "net_serverRconFrom" );
	if ( rconFrom != NULL && rconFrom[ 0 ] != '\0' ) {
		MPModLogString( line, "rcon", rconFrom );
	}
	// what a logged-in admin asked for is logged with who asked
	if ( modActorClient >= 0 && modActorClient < MAX_CLIENTS ) {
		MPModLogNumber( line, "admin_slot", modActorClient );
		MPModLogString( line, "admin_name", userInfo[ modActorClient ].GetString( "ui_name" ) );
		if ( clientModAddress[ modActorClient ].family != 0 ) {
			char address[ MP_MOD_RANGE_TEXT_BYTES ];
			MPModFormatAddress( clientModAddress[ modActorClient ], address, sizeof( address ) );
			MPModLogString( line, "admin_address", address );
		}
	}
	MPModLogEnd( line );
	if ( !g_adminLog.GetBool() ) {
		return;
	}
	idFile *file = fileSystem->OpenFileAppend( MP_MOD_ADMIN_LOG_FILENAME, true, "fs_savepath" );
	if ( file == NULL ) {
		static bool warned = false;
		if ( !warned ) {
			warned = true;
			common->Warning( "admin log: %s could not be opened", MP_MOD_ADMIN_LOG_FILENAME );
		}
		return;
	}
	file->Write( line.text, line.length );
	fileSystem->CloseFile( file );
}

/*
================
idGameLocal::ModerationLogClient
================
*/
void idGameLocal::ModerationLogClient( const char *event, const char *by, int clientNum, const char *reason, mpModTime_t seconds ) {
	mpModLogLine_t line;
	MPModLogBegin( line, ModerationNow(), event );
	MPModLogString( line, "by", by );
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		MPModLogNumber( line, "slot", clientNum );
		MPModLogString( line, "name", userInfo[ clientNum ].GetString( "ui_name" ) );
		if ( clientModAddress[ clientNum ].family != 0 ) {
			char address[ MP_MOD_RANGE_TEXT_BYTES ];
			MPModFormatAddress( clientModAddress[ clientNum ], address, sizeof( address ) );
			MPModLogString( line, "address", address );
		}
	}
	MPModLogString( line, "reason", reason );
	if ( seconds > 0 ) {
		MPModLogNumber( line, "seconds", seconds );
	}
	ModerationWriteLog( line );
}

/*
================
idGameLocal::ModerationLogBan
================
*/
void idGameLocal::ModerationLogBan( const char *event, const char *by, const mpModBan_t &ban ) {
	char range[ MP_MOD_RANGE_TEXT_BYTES ];
	MPModFormatRange( ban.address, ban.prefixBits, range, sizeof( range ) );
	mpModLogLine_t line;
	MPModLogBegin( line, ModerationNow(), event );
	MPModLogString( line, "by", by );
	if ( MPModBanIsIdentity( ban ) ) {
		MPModLogString( line, "identity", ban.identity );		// MP-17
	} else {
		MPModLogString( line, "range", range );
	}
	MPModLogString( line, "name", ban.name );
	MPModLogString( line, "reason", ban.reason );
	if ( ban.expires != 0 ) {
		char expires[ MP_MOD_UTC_TEXT_BYTES ];
		MPModFormatUtc( ban.expires, expires );
		MPModLogString( line, "expires", expires );
	}
	ModerationWriteLog( line );
}

/*
================
idGameLocal::ModerationClientConnect
A slot has a new occupant: learn its address, and carry over a text mute that
its address earned before it reconnected.
================
*/
void idGameLocal::ModerationClientConnect( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	MPModClearAddress( clientModAddress[ clientNum ] );
	modTextMuteUntil[ clientNum ] = 0;
	modNoticeTime[ clientNum ] = 0;
	modMutedName[ clientNum ].Clear();
	modMutedClan[ clientNum ].Clear();
	MPModFloodReset( modChatFlood[ clientNum ] );

	int prefixBits = 0;
	const char *text = networkSystem->GetClientAddress( clientNum );
	if ( text == NULL || !MPModParseAddress( text, clientModAddress[ clientNum ], prefixBits ) ) {
		MPModClearAddress( clientModAddress[ clientNum ] );
		return;
	}
	const int mute = MPModMuteFind( modMutes, clientModAddress[ clientNum ], ModerationNow() );
	if ( mute >= 0 ) {
		modTextMuteUntil[ clientNum ] = modMutes.mutes[ mute ].expires != 0 ? modMutes.mutes[ mute ].expires : -1;
	}
}

/*
================
idGameLocal::ModerationClientDisconnect
================
*/
void idGameLocal::ModerationClientDisconnect( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	// "kick <slot>" is the engine's command and reaches the game only as this
	// disconnect. The engine publishes why it drops a client in
	// net_serverLastDrop; a removal the game began itself was logged where it began.
	if ( isServer && !mpGame.ServerDropInFlight( clientNum ) && !botManager.IsBot( clientNum ) &&
		idStr::Cmp( cvarSystem->GetCVarString( "net_serverLastDrop" ), va( "%d %s", clientNum, MP_MOD_ENGINE_KICK_REASON ) ) == 0 ) {
		ModerationLogClient( "kick", "console", clientNum, "", 0 );
	}
	MPModClearAddress( clientModAddress[ clientNum ] );
	modTextMuteUntil[ clientNum ] = 0;
	modNoticeTime[ clientNum ] = 0;
	modMutedName[ clientNum ].Clear();
	modMutedClan[ clientNum ].Clear();
	MPModFloodReset( modChatFlood[ clientNum ] );
}

/*
================
idGameLocal::ModerationRefuseBanned
ServerAllowClient and RepeaterAllowClient: true when the endpoint the connect
came from is banned. A banned client tries again every few seconds, so one
address reaches the admin log once a minute.
================
*/
bool idGameLocal::ModerationRefuseBanned( const char *endpoint ) {
	mpModAddress_t address;
	int prefixBits = 0;
	if ( !MPModParseAddress( endpoint, address, prefixBits ) ) {
		return false;
	}
	if ( !banListLoaded ) {
		LoadBanList();
	}
	const mpModTime_t now = ModerationNow();
	const int index = MPModBanFind( *modBans, address, now );
	if ( index < 0 ) {
		return false;
	}
	// The pacing belongs to the ban, not to the last address seen: a client that
	// alternates between two banned addresses would otherwise write a line for
	// every packet. One line a minute for each ban, and no more than
	// MP_MOD_REFUSAL_LOG_BURST lines a minute for the whole list.
	mpModBan_t &ban = modBans->bans[ index ];
	const bool banDue = ban.refusedLogged == 0 || now - ban.refusedLogged >= MP_MOD_REFUSAL_LOG_SECONDS || now < ban.refusedLogged;
	if ( now - modRefusalWindowStart >= MP_MOD_REFUSAL_LOG_SECONDS || now < modRefusalWindowStart ) {
		modRefusalWindowStart = now;
		modRefusalWindowCount = 0;
	}
	if ( banDue && modRefusalWindowCount < MP_MOD_REFUSAL_LOG_BURST ) {
		ban.refusedLogged = now;
		modRefusalWindowCount++;
		char text[ MP_MOD_RANGE_TEXT_BYTES ];
		char range[ MP_MOD_RANGE_TEXT_BYTES ];
		MPModFormatAddress( address, text, sizeof( text ) );
		MPModFormatRange( ban.address, ban.prefixBits, range, sizeof( range ) );
		common->Printf( "refused a connection from %s: banned (%s)\n", text, range );
		mpModLogLine_t line;
		MPModLogBegin( line, now, "refused" );
		MPModLogString( line, "address", text );
		MPModLogString( line, "range", range );
		MPModLogString( line, "name", ban.name );
		MPModLogString( line, "reason", ban.reason );
		ModerationWriteLog( line );
	}
	return true;
}

/*
================
idGameLocal::FindIdentityBan
openQ4 MP-17: the ban in force on this identity, or NULL.
================
*/
const mpModBan_t *idGameLocal::FindIdentityBan( const char *identity ) {
	if ( !banListLoaded ) {
		LoadBanList();
	}
	const int index = MPModBanFindIdentity( *modBans, identity, ModerationNow() );
	return index >= 0 ? &modBans->bans[ index ] : NULL;
}

/*
================
idGameLocal::ServerBanIdentity
openQ4 MP-17: bans an identity and removes whoever holds it. Returns the ban's
row, or -1 when the list refused it.
================
*/
int idGameLocal::ServerBanIdentity( const char *identity, mpModTime_t durationSeconds, const char *name, const char *reason, const char *by ) {
	if ( !banListLoaded ) {
		LoadBanList();
	}
	const int index = MPModBanAddIdentity( *modBans, identity, ModerationNow(), durationSeconds, name, reason );
	if ( index < 0 ) {
		common->Printf( "ban: refused; an identity is sixteen hex digits, and the list holds %d bans\n", MP_MOD_MAX_BANS );
		return -1;
	}
	banListChanged = true;
	SaveBanList();
	const mpModBan_t ban = modBans->bans[ index ];
	char length[ 32 ];
	MPModFormatDuration( durationSeconds, length, sizeof( length ) );
	common->Printf( "banned %s%s (%s)%s%s\n", MP_MOD_IDENTITY_PREFIX, ban.identity, length, ban.reason[ 0 ] != '\0' ? ": " : "", ban.reason );
	ModerationLogBan( "ban", by, ban );
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( i == localClientNum || idStr::Cmp( mpGame.ClientIdentity( i ), ban.identity ) != 0 ) {
			continue;
		}
		ModerationLogClient( "kick", by, i, "banned", 0 );
		mpGame.BeginServerDrop( i );
		networkSystem->ServerDropClient( i, "#str_107239" );
		mpGame.EndServerDrop( i );
	}
	return index;
}

/*
================
idGameLocal::ServerBanClient
openQ4 MP-17: a connected player's ban, by the range they hold (over IPv6
their /64) and, where they proved one, their identity, so neither a new
address nor a new key alone brings them back. Returns the range ban's row, or
the identity ban's for a player with no address; -1 when nothing was banned.
================
*/
int idGameLocal::ServerBanClient( int clientNum, mpModTime_t durationSeconds, const char *reason, const char *by ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || clientNum == localClientNum ) {
		return -1;
	}
	const idStr name = userInfo[ clientNum ].GetString( "ui_name" );
	const idStr identity = mpGame.ClientIdentity( clientNum );
	const mpModAddress_t address = GetClientModAddress( clientNum );
	int index = -1;
	// the identity first: the range ban removes the player, and their identity with them
	if ( identity.Length() > 0 ) {
		index = ServerBanIdentity( identity.c_str(), durationSeconds, name.c_str(), reason, by );
	}
	if ( address.family != 0 ) {
		const int range = ServerBan( address, MPModPlayerPrefixBits( address ), durationSeconds, name.c_str(), reason, by );
		index = range >= 0 ? range : index;
	}
	return index;
}

/*
================
idGameLocal::ServerBan
Bans a range and removes every connected client inside it. Returns the ban's
row, or -1 when the list refused it.
================
*/
int idGameLocal::ServerBan( const mpModAddress_t &address, int prefixBits, mpModTime_t durationSeconds, const char *name, const char *reason, const char *by ) {
	const int index = AddBan( address, prefixBits, durationSeconds, name, reason );
	if ( index < 0 ) {
		common->Printf( "ban: refused; a range may be no wider than /%d (IPv4) or /%d (IPv6), and the list holds %d bans\n",
			MP_MOD_MIN_PREFIX_V4, MP_MOD_MIN_PREFIX_V6, MP_MOD_MAX_BANS );
		return -1;
	}
	// dropping a client runs game code; work from a copy of the row
	const mpModBan_t ban = modBans->bans[ index ];
	char range[ MP_MOD_RANGE_TEXT_BYTES ];
	char length[ 32 ];
	MPModFormatRange( ban.address, ban.prefixBits, range, sizeof( range ) );
	MPModFormatDuration( durationSeconds, length, sizeof( length ) );
	common->Printf( "banned %s (%s)%s%s\n", range, length, ban.reason[ 0 ] != '\0' ? ": " : "", ban.reason );
	ModerationLogBan( "ban", by, ban );

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( i == localClientNum || clientModAddress[ i ].family == 0 ||
			!MPModAddressInRange( clientModAddress[ i ], ban.address, ban.prefixBits ) ) {
			continue;
		}
		ModerationLogClient( "kick", by, i, "banned", 0 );
		mpGame.BeginServerDrop( i );
		networkSystem->ServerDropClient( i, "#str_107239" );
		mpGame.EndServerDrop( i );
	}
	return index;
}

/*
================
idGameLocal::IsClientTextMuted
================
*/
bool idGameLocal::IsClientTextMuted( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || modTextMuteUntil[ clientNum ] == 0 ) {
		return false;
	}
	if ( modTextMuteUntil[ clientNum ] > 0 && modTextMuteUntil[ clientNum ] <= ModerationNow() ) {
		modTextMuteUntil[ clientNum ] = 0;
		return false;
	}
	return true;
}

/*
================
idGameLocal::SetClientTextMute
A muted player's chat and voice-menu lines reach nobody. The mute is kept by
address as well, so leaving and joining again does not lift it; it lasts until
it runs out, it is lifted, or the server stops. False when nothing changed.
================
*/
bool idGameLocal::SetClientTextMute( int clientNum, bool mute, mpModTime_t durationSeconds, const char *by ) {
	if ( !isServer || clientNum < 0 || clientNum >= numClients || clientNum == localClientNum ||
		entities[ clientNum ] == NULL || !entities[ clientNum ]->IsType( idPlayer::GetClassType() ) ) {
		return false;
	}
	const mpModTime_t now = ModerationNow();
	if ( mute ) {
		modTextMuteUntil[ clientNum ] = durationSeconds > 0 ? now + durationSeconds : -1;
		MPModMuteAdd( modMutes, clientModAddress[ clientNum ], now, durationSeconds );
		// the name and clan tag the mute holds the player to (idGameLocal::SetUserInfo)
		modMutedName[ clientNum ] = userInfo[ clientNum ].GetString( "ui_name" );
		modMutedClan[ clientNum ] = userInfo[ clientNum ].GetString( "ui_clan" );
	} else {
		if ( !IsClientTextMuted( clientNum ) ) {
			return false;
		}
		modTextMuteUntil[ clientNum ] = 0;
		MPModMuteRemove( modMutes, clientModAddress[ clientNum ] );
		modMutedName[ clientNum ].Clear();
		modMutedClan[ clientNum ].Clear();
	}
	modNoticeTime[ clientNum ] = 0;
	if ( !botManager.IsBot( clientNum ) ) {
		ServerSendChatMessage( clientNum, "server", mute ? "#str_232641" : "#str_232643", "" );
	}
	ModerationLogClient( mute ? "mute" : "unmute", by, clientNum, "", mute ? durationSeconds : 0 );
	return true;
}

/*
================
idGameLocal::ModerationAllowChat
True when the server may pass this client's chat or voice-menu line on. A line
that arrived from a remote client also answers to the flood window
(g_chatFloodMessages lines in any g_chatFloodSeconds, then g_chatFloodWait
seconds of silence). The sender is told why, at most every ten seconds.
================
*/
bool idGameLocal::ModerationAllowChat( int clientNum, bool fromNetwork ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return true;
	}
	const char *notice = NULL;
	if ( IsClientTextMuted( clientNum ) ) {
		notice = "#str_232641";
	} else if ( fromNetwork ) {
		const bool wasSilenced = modChatFlood[ clientNum ].silenced;
		if ( MPModFloodCheck( modChatFlood[ clientNum ], time, g_chatFloodMessages.GetInteger(),
				g_chatFloodSeconds.GetInteger() * 1000, g_chatFloodWait.GetInteger() * 1000 ) > 0 ) {
			notice = "#str_232642";
			if ( !wasSilenced ) {
				ModerationLogClient( "flood", "server", clientNum, "chat", g_chatFloodWait.GetInteger() );
				modNoticeTime[ clientNum ] = 0;
			}
		}
	}
	if ( notice == NULL ) {
		return true;
	}
	// a restart winds game time back: a stamp far in the future is stale
	if ( time >= modNoticeTime[ clientNum ] || modNoticeTime[ clientNum ] - time > MP_MOD_NOTICE_MSEC ) {
		modNoticeTime[ clientNum ] = time + MP_MOD_NOTICE_MSEC;
		if ( !botManager.IsBot( clientNum ) ) {
			ServerSendChatMessage( clientNum, "server", notice, "" );
		}
	}
	return false;
}

// openQ4 MP-18: graceful shutdown. The server tells its players, lets the match
// end or a time run out, says goodbye and stops: a dedicated server's process
// quits, a server hosted from the game goes back to its menu.
enum {
	MOD_SHUTDOWN_NONE = 0,
	MOD_SHUTDOWN_MATCH,			// when this match ends, or at once on a server with no human on it
	MOD_SHUTDOWN_TIME,			// at modShutdownAt
	MOD_SHUTDOWN_STOPPING,		// the goodbye is on its way; the stop command follows
	MOD_SHUTDOWN_STOPPED		// the stop command is queued: nothing more may start until the server is gone
};
// how long the goodbye has to reach the players before the stop command runs
static const int MP_MOD_SHUTDOWN_STOP_MSEC = 1500;
// seconds left at which players are told again
static const int modShutdownNotices[] = { 3600, 1800, 600, 300, 60, 30, 10 };

static int ModerationNextShutdownNotice( mpModTime_t left ) {
	for ( int i = 0; i < static_cast<int>( sizeof( modShutdownNotices ) / sizeof( modShutdownNotices[ 0 ] ) ); i++ ) {
		if ( modShutdownNotices[ i ] < left ) {
			return modShutdownNotices[ i ];
		}
	}
	return -1;
}

/*
================
idGameLocal::ModerationLogShutdown
================
*/
void idGameLocal::ModerationLogShutdown( const char *event, const char *by, const char *when, mpModTime_t seconds ) {
	mpModLogLine_t line;
	MPModLogBegin( line, ModerationNow(), event );
	MPModLogString( line, "by", by );
	MPModLogString( line, "when", when );
	if ( seconds > 0 ) {
		MPModLogNumber( line, "seconds", seconds );
	}
	MPModLogString( line, "reason", modShutdownReason.c_str() );
	ModerationWriteLog( line );
}

/*
================
idGameLocal::ScheduleServerShutdown
After this match (afterMatch), or in so many seconds; 0 seconds is now.
================
*/
void idGameLocal::ScheduleServerShutdown( bool afterMatch, mpModTime_t seconds, const char *reason, const char *by ) {
	modShutdownRun = cvarSystem->GetCVarInteger( "net_serverRunId" );
	char clean[ MP_MOD_REASON_BYTES ];
	MPModCopyText( clean, sizeof( clean ), reason );
	modShutdownReason = clean;
	modShutdownMode = afterMatch ? MOD_SHUTDOWN_MATCH : MOD_SHUTDOWN_TIME;
	modShutdownAt = ModerationNow() + ( seconds > 0 ? seconds : 0 );
	modShutdownNextNotice = afterMatch ? -1 : ModerationNextShutdownNotice( seconds );

	char length[ 32 ];
	MPModFormatDuration( seconds, length, sizeof( length ) );
	if ( afterMatch ) {
		ServerSendChatMessage( -1, "server", "#str_232644", "" );
		common->Printf( "server shutdown: when this match ends\n" );
	} else if ( seconds > 0 ) {
		ServerSendChatMessage( -1, "server", "#str_232645", length );
		common->Printf( "server shutdown: in %s\n", length );
	}
	if ( clean[ 0 ] != '\0' && ( afterMatch || seconds > 0 ) ) {
		ServerSendChatMessage( -1, "server", clean, "" );
	}
	ModerationLogShutdown( "shutdown_scheduled", by, afterMatch ? "match" : "time", afterMatch ? 0 : seconds );
}

/*
================
idGameLocal::CancelServerShutdown
False when no shutdown was waiting.
================
*/
bool idGameLocal::CancelServerShutdown( const char *by ) {
	if ( modShutdownMode != MOD_SHUTDOWN_MATCH && modShutdownMode != MOD_SHUTDOWN_TIME ) {
		return false;
	}
	modShutdownMode = MOD_SHUTDOWN_NONE;
	ServerSendChatMessage( -1, "server", "#str_232646", "" );
	common->Printf( "server shutdown: called off\n" );
	ModerationLogShutdown( "shutdown_cancelled", by, "", 0 );
	modShutdownReason.Clear();
	return true;
}

/*
================
idGameLocal::ServerShutdownMapEnded
The map is being shut down. A server hosted from the game stops with
"disconnect" and its process lives on: the next server it starts must not
inherit a shutdown that has already happened.
================
*/
void idGameLocal::ServerShutdownMapEnded( void ) {
	if ( modShutdownMode == MOD_SHUTDOWN_STOPPED ) {
		modShutdownMode = MOD_SHUTDOWN_NONE;
		modShutdownReason.Clear();
	}
}

/*
================
idGameLocal::RunServerShutdown
Every server frame, and with matchEnded when a match is over and the next map
is about to be chosen. True while the server is stopping: the caller must not
start another map.
================
*/
bool idGameLocal::RunServerShutdown( bool matchEnded ) {
	if ( !isServer || modShutdownMode == MOD_SHUTDOWN_NONE ) {
		return false;
	}
	// A shutdown belongs to the server it was asked of. A listen host who leaves by hand
	// and hosts again has a new server (the engine counts them in net_serverRunId).
	if ( modShutdownRun != cvarSystem->GetCVarInteger( "net_serverRunId" ) ) {
		modShutdownMode = MOD_SHUTDOWN_NONE;
		modShutdownReason.Clear();
		return false;
	}
	if ( modShutdownMode == MOD_SHUTDOWN_STOPPED ) {
		return true;
	}
	if ( modShutdownMode == MOD_SHUTDOWN_STOPPING ) {
		// a restart winds game time back: a stop time far ahead is stale
		if ( time >= modShutdownStopTime || modShutdownStopTime - time > MP_MOD_SHUTDOWN_STOP_MSEC ) {
			// The command runs with the next command buffer, frames from now. Until the
			// server is gone this still answers true, or the game state would choose
			// and start the next map in between.
			modShutdownMode = MOD_SHUTDOWN_STOPPED;
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, cvarSystem->GetCVarInteger( "net_serverDedicated" ) != 0 ? "quit\n" : "disconnect\n" );
		}
		return true;
	}

	bool stop = false;
	if ( modShutdownMode == MOD_SHUTDOWN_MATCH ) {
		stop = matchEnded;
		if ( !stop ) {
			// nobody is playing a match on a server with no human on it
			stop = true;
			for ( int i = 0; i < numClients; i++ ) {
				if ( entities[ i ] != NULL && entities[ i ]->IsType( idPlayer::GetClassType() ) && !botManager.IsBot( i ) ) {
					stop = false;
					break;
				}
			}
		}
	} else {
		const mpModTime_t left = modShutdownAt - ModerationNow();
		if ( left <= 0 ) {
			stop = true;
		} else if ( modShutdownNextNotice > 0 && left <= modShutdownNextNotice ) {
			char length[ 32 ];
			MPModFormatDuration( left, length, sizeof( length ) );
			ServerSendChatMessage( -1, "server", "#str_232645", length );
			modShutdownNextNotice = ModerationNextShutdownNotice( left );
		}
	}
	if ( !stop ) {
		return false;
	}
	ServerSendChatMessage( -1, "server", "#str_232647", "" );
	common->Printf( "server shutdown: stopping\n" );
	ModerationLogShutdown( "shutdown", "server", modShutdownMode == MOD_SHUTDOWN_MATCH ? "match" : "time", 0 );
	modShutdownMode = MOD_SHUTDOWN_STOPPING;
	modShutdownStopTime = time + MP_MOD_SHUTDOWN_STOP_MSEC;
	return true;
}

/*
================
idGameLocal::ServerModerationRequest

GAME_RELIABLE_MESSAGE_MODERATION: what a logged-in admin asks the server to do
to a connected player. On the wire: the verb (mpModAdminVerb_t) and the slot,
a byte each; the slot's spawn identity and a length in seconds, 32 bits each;
then a reason. The identity is the player the admin was looking at: if that
player left and somebody else took the slot, nothing happens to the newcomer.

An admin reaches connected players only and never an address, so this takes
no range, lifts no ban and lists nothing. Who is refused is in
MPModAdminCheck. Everything done is in the admin log with the admin's own
slot, name and address beside it.
================
*/
static const int MP_MOD_ADMIN_REQUEST_MIN_BYTES = 1 + 1 + 4 + 4 + 1;

void idGameLocal::ServerModerationRequest( int clientNum, const idBitMsg &msg ) {
	if ( !isServer || !isMultiplayer || clientNum < 0 || clientNum >= numClients || clientNum >= MAX_CLIENTS ||
		clientNum == localClientNum ) {
		return;
	}
	const int bits = msg.GetRemainingReadBits();
	if ( bits < MP_MOD_ADMIN_REQUEST_MIN_BYTES * 8 || bits > ( MP_MOD_ADMIN_REQUEST_MIN_BYTES + MP_MOD_REASON_BYTES ) * 8 ) {
		return;
	}
	const int verb = msg.ReadByte();
	const int targetSlot = msg.ReadByte();
	const int identity = msg.ReadLong();
	const int requestedSeconds = msg.ReadLong();
	char wireReason[ MP_MOD_REASON_BYTES ];
	msg.ReadString( wireReason, sizeof( wireReason ) );
	if ( msg.GetRemainingReadBits() != 0 ) {
		return;
	}
	char reason[ MP_MOD_REASON_BYTES ];
	MPModCopyText( reason, sizeof( reason ), wireReason );

	if ( !mpGame.IsServerAdmin( clientNum ) ) {
		ServerSendChatMessage( clientNum, "server", "#str_232654", "" );
		return;
	}
	if ( verb == MP_MOD_ADMIN_LOGOUT ) {
		mpGame.ServerAdminLogout( clientNum );
		common->Printf( "client %d (%s^0) is no longer an admin\n", clientNum, userInfo[ clientNum ].GetString( "ui_name" ) );
		ModerationLogClient( "admin_logout", "server", clientNum, "", 0 );
		ServerSendChatMessage( clientNum, "server", "#str_232652", "" );
		return;
	}

	mpModAdminTarget_t target;
	memset( &target, 0, sizeof( target ) );
	if ( targetSlot >= 0 && targetSlot < numClients && targetSlot < MAX_CLIENTS && entities[ targetSlot ] != NULL &&
		entities[ targetSlot ]->IsType( idPlayer::GetClassType() ) ) {
		target.connected = true;
		target.current = identity != 0 && GetSpawnId( entities[ targetSlot ] ) == identity;
		target.host = targetSlot == localClientNum;
		target.admin = mpGame.IsServerAdmin( targetSlot );
		target.bot = botManager.IsBot( targetSlot );
		// a ban or a mute is of the target's range: does it hold an admin, this one included?
		for ( int i = 0; i < numClients && i < MAX_CLIENTS; i++ ) {
			if ( i != targetSlot && ( i == clientNum || mpGame.IsServerAdmin( i ) ) &&
				MPModSamePlayer( clientModAddress[ i ], clientModAddress[ targetSlot ] ) ) {
				target.coversAdmin = true;
				break;
			}
		}
	}
	const int maxBanMinutes = g_adminMaxBanMinutes.GetInteger();
	const mpModAdminResult_t allowed = MPModAdminCheck( verb, clientNum, targetSlot, target, maxBanMinutes );
	if ( allowed != MP_MOD_ADMIN_OK ) {
		ServerSendChatMessage( clientNum, "server", allowed == MP_MOD_ADMIN_REFUSED_BANS_OFF ? "#str_232657" : "#str_232656", "" );
		return;
	}

	mpModTime_t seconds = requestedSeconds > 0 ? requestedSeconds : 0;
	if ( seconds > MP_MOD_MAX_DURATION_SECONDS ) {
		seconds = MP_MOD_MAX_DURATION_SECONDS;
	}
	// the target may be gone by the time the reply is written
	const idStr targetName = userInfo[ targetSlot ].GetString( "ui_name" );
	bool done = true;
	// the admin log names the admin for everything this request does, and for
	// nothing after it, however the request ends (a command can raise an error)
	struct actorScope_t {
		int &actor;
		actorScope_t( int &slot, int clientNum ) : actor( slot ) { actor = clientNum; }
		~actorScope_t() { actor = -1; }
	} actorScope( modActorClient, clientNum );
	switch ( verb ) {
		case MP_MOD_ADMIN_KICK: {
			ModerationLogClient( "kick", "admin", targetSlot, reason, 0 );
			mpGame.BeginServerDrop( targetSlot );
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "kick %d\n", targetSlot ) );
			mpGame.EndServerDrop( targetSlot );
			break;
		}
		case MP_MOD_ADMIN_MUTE: {
			done = SetClientTextMute( targetSlot, true, seconds, "admin" );
			break;
		}
		case MP_MOD_ADMIN_UNMUTE: {
			done = SetClientTextMute( targetSlot, false, 0, "admin" );
			break;
		}
		case MP_MOD_ADMIN_BAN: {
			// the range they hold and (MP-17) their identity
			done = ServerBanClient( targetSlot, MPModAdminBanSeconds( seconds, maxBanMinutes ), reason, "admin" ) >= 0;
			break;
		}
		default: {
			done = false;
			break;
		}
	}
	common->Printf( "admin %d (%s^0): %s %s^0%s\n", clientNum, userInfo[ clientNum ].GetString( "ui_name" ),
		MPModAdminVerbName( verb ), targetName.c_str(), done ? "" : " (nothing changed)" );
	ServerSendChatMessage( clientNum, "server", done ? "#str_232655" : "#str_232656", done ? targetName.c_str() : "" );
}

/*
================
idGameLocal::ClientModerationRequest

A client's half of the above. The server answers in the chat area: done,
refused, or that this connection is not logged in as an admin.
================
*/
bool idGameLocal::ClientModerationRequest( int verb, int clientNum, mpModTime_t seconds, const char *reason ) {
	if ( !isMultiplayer || !isClient ) {
		return false;
	}
	int identity = 0;
	if ( MPModAdminVerbHasTarget( verb ) ) {
		if ( clientNum < 0 || clientNum >= numClients || clientNum >= MAX_CLIENTS || entities[ clientNum ] == NULL ||
			!entities[ clientNum ]->IsType( idPlayer::GetClassType() ) ) {
			return false;
		}
		identity = GetSpawnId( entities[ clientNum ] );
	} else if ( verb == MP_MOD_ADMIN_LOGOUT ) {
		clientNum = 0;
	} else {
		return false;
	}
	if ( seconds < 0 ) {
		seconds = 0;
	} else if ( seconds > MP_MOD_MAX_DURATION_SECONDS ) {
		seconds = MP_MOD_MAX_DURATION_SECONDS;
	}
	char clean[ MP_MOD_REASON_BYTES ];
	MPModCopyText( clean, sizeof( clean ), reason );

	idBitMsg outMsg;
	byte msgBuf[ MP_MOD_ADMIN_REQUEST_MIN_BYTES + MP_MOD_REASON_BYTES + 1 ];
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.BeginWriting();
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_MODERATION );
	outMsg.WriteByte( verb );
	outMsg.WriteByte( clientNum );
	outMsg.WriteLong( identity );
	outMsg.WriteLong( static_cast<int>( seconds ) );
	outMsg.WriteString( clean );
	if ( outMsg.IsOverflowed() ) {
		return false;
	}
	networkSystem->ClientSendReliableMessage( outMsg );
	return true;
}

// A connected player's slot from a slot number or a name, or -1.
static int ModerationFindClient( const char *text ) {
	if ( text == NULL || text[ 0 ] == '\0' ) {
		return -1;
	}
	if ( idStr::IsNumeric( text ) ) {
		const int clientNum = atoi( text );
		if ( clientNum < 0 || clientNum >= gameLocal.numClients || gameLocal.entities[ clientNum ] == NULL ||
			!gameLocal.entities[ clientNum ]->IsType( idPlayer::GetClassType() ) ) {
			return -1;
		}
		return clientNum;
	}
	const idPlayer *player = gameLocal.GetClientByName( text );
	return player != NULL ? player->entityNumber : -1;
}

// The command's words. The console's tokenizer is a script lexer: it cuts
// "192.0.2.0/24" and "10m" in two, reads "3rd offence" as a number and words,
// and takes "1e0f" inside an IPv6 address for a float, losing a character. The
// engine publishes the line as it was typed for a command registered with
// CMD_FL_RAWTEXT (cmd_rawText), and the line is cut at spaces only
// (MPModSplitWords). Where there is no such line (an older engine, or a line
// with "$", whose cvar values only the tokens hold) the tokens are used, and
// the core's joiners put back what they can.
static const int MODERATION_MAX_ARGS = MP_MOD_MAX_WORDS;
static int ModerationArgs( const idCmdArgs &args, mpModWords_t &words, const char *argv[ MODERATION_MAX_ARGS ] ) {
	const char *name = args.Argv( 0 );
	if ( ( name[ 0 ] == '/' || name[ 0 ] == '\\' ) && name[ 1 ] != '\0' ) {
		name++;
	}
	const int nameLength = static_cast<int>( strlen( name ) );
	const char *raw = cvarSystem->GetCVarString( "cmd_rawText" );
	while ( *raw == ' ' || *raw == '\t' ) {
		raw++;
	}
	if ( *raw == '/' || *raw == '\\' ) {
		raw++;
	}
	if ( nameLength > 0 && idStr::Icmpn( raw, name, nameLength ) == 0 &&
		( raw[ nameLength ] == '\0' || raw[ nameLength ] == ' ' || raw[ nameLength ] == '\t' || raw[ nameLength ] == '\n' || raw[ nameLength ] == '\r' ) &&
		strchr( raw, '$' ) == NULL && MPModSplitWords( raw, words ) && words.count >= 1 ) {
		for ( int i = 0; i < words.count; i++ ) {
			argv[ i ] = words.word[ i ];
		}
		return words.count;
	}
	const int argc = Min( args.Argc(), MODERATION_MAX_ARGS );
	for ( int i = 0; i < argc; i++ ) {
		argv[ i ] = args.Argv( i );
	}
	return argc;
}

// True when the word is a slot number: digits and nothing else.
static bool ModerationIsSlot( const char *text ) {
	if ( text == NULL || text[ 0 ] == '\0' ) {
		return false;
	}
	for ( const char *at = text; *at != '\0'; at++ ) {
		if ( *at < '0' || *at > '9' ) {
			return false;
		}
	}
	return true;
}

// ban, mute, unmute and gameKick typed on a client: an admin's request to the
// server, for a connected player and nothing else (ServerModerationRequest).
static void ModerationClientCommand( int verb, const idCmdArgs &args ) {
	const char *command = args.Argv( 0 );
	const bool takesTime = verb == MP_MOD_ADMIN_BAN || verb == MP_MOD_ADMIN_MUTE;
	const bool takesReason = verb == MP_MOD_ADMIN_BAN || verb == MP_MOD_ADMIN_KICK;
	mpModWords_t words;
	const char *argv[ MODERATION_MAX_ARGS ];
	const int argc = ModerationArgs( args, words, argv );
	if ( argc < 2 ) {
		common->Printf( "usage: %s <client slot|name>%s%s\n  the server does this for a logged-in admin: set net_clientAdminPassword, then 'admin'\n",
			command, takesTime ? " [time]" : "", takesReason ? " [reason]" : "" );
		return;
	}
	const int clientNum = ModerationFindClient( argv[ 1 ] );
	if ( clientNum < 0 ) {
		common->Printf( "%s: no connected player matches '%s'; an admin acts on connected players only\n", command, argv[ 1 ] );
		return;
	}
	int next = 2;
	mpModTime_t duration = 0;
	if ( takesTime ) {
		next += MPModJoinDurationArgs( argc, argv, next, duration );
	}
	idStr reason;
	if ( takesReason ) {
		for ( int i = next; i < argc; i++ ) {
			if ( reason.Length() > 0 ) {
				reason += " ";
			}
			reason += argv[ i ];
		}
	} else if ( next != argc ) {
		common->Printf( "%s: '%s' is not %s\n", command, argv[ next ], takesTime ? "a time" : "expected" );
		return;
	}
	if ( !gameLocal.ClientModerationRequest( verb, clientNum, duration, reason.c_str() ) ) {
		common->Printf( "%s: the request could not be sent\n", command );
	}
}

/*
================
idGameLocal::ClientKick_f
"gameKick" typed on a client.
================
*/
void idGameLocal::ClientKick_f( const idCmdArgs &args ) {
	ModerationClientCommand( MP_MOD_ADMIN_KICK, args );
}

/*
================
idGameLocal::Admin_f

"admin": log in to the server as an admin. The password is read from
net_clientAdminPassword, never from the command line: that cvar is private, so
setting it stays out of the console history, the log and the config file,
where a password typed after the command would not.
================
*/
void idGameLocal::Admin_f( const idCmdArgs &args ) {
	if ( !gameLocal.isMultiplayer || ( !gameLocal.isClient && !gameLocal.isServer ) ) {
		common->Printf( "admin: join a server first\n" );
		return;
	}
	const char *word = args.Argc() > 1 ? args.Argv( 1 ) : "";
	if ( gameLocal.isServer ) {
		// The host's side: who is an admin, and taking the right away. The host needs no login.
		if ( idStr::Icmp( word, "off" ) == 0 && args.Argc() == 2 ) {
			gameLocal.mpGame.ServerAdminLoginsOff();
			common->Printf( "admin logins are off: every admin is logged out and the password is forgotten\n" );
			mpModLogLine_t line;
			MPModLogBegin( line, ModerationNow(), "admin_logins_off" );
			MPModLogString( line, "by", "console" );
			gameLocal.ModerationWriteLog( line );
			return;
		}
		if ( idStr::Icmp( word, "revoke" ) == 0 && args.Argc() == 3 ) {
			const bool all = idStr::Icmp( args.Argv( 2 ), "all" ) == 0;
			const int only = all ? -1 : ModerationFindClient( args.Argv( 2 ) );
			if ( !all && only < 0 ) {
				common->Printf( "admin: no connected player matches '%s'\n", args.Argv( 2 ) );
				return;
			}
			int revoked = 0;
			for ( int i = 0; i < gameLocal.numClients && i < MAX_CLIENTS; i++ ) {
				if ( ( all || i == only ) && gameLocal.mpGame.ServerAdminLogout( i ) ) {
					common->Printf( "%s^0 is no longer an admin\n", gameLocal.userInfo[ i ].GetString( "ui_name" ) );
					gameLocal.ModerationLogClient( "admin_logout", "console", i, "revoked", 0 );
					gameLocal.ServerSendChatMessage( i, "server", "#str_232652", "" );
					revoked++;
				}
			}
			if ( revoked == 0 ) {
				common->Printf( all ? "admin: nobody is logged in as an admin\n" : "admin: that player is not an admin\n" );
			}
			return;
		}
		if ( args.Argc() > 2 || ( args.Argc() == 2 && idStr::Icmp( word, "list" ) != 0 ) ) {
			common->Printf( "usage: admin                          who is logged in as an admin\n"
				"       admin revoke <slot|name|all>   end an admin's login\n"
				"       admin off                      end every login and forget the password\n"
				"  players log in with the password given in net_serverAdminPassword\n" );
			return;
		}
		common->Printf( "admin logins are %s\n", gameLocal.mpGame.AdminLoginAvailable() ? "on" :
			"off: set net_serverAdminPassword (at least 8 bytes) to turn them on" );
		int admins = 0;
		for ( int i = 0; i < gameLocal.numClients && i < MAX_CLIENTS; i++ ) {
			if ( gameLocal.mpGame.IsServerAdmin( i ) ) {
				common->Printf( "%4d  %s^0\n", i, gameLocal.userInfo[ i ].GetString( "ui_name" ) );
				admins++;
			}
		}
		common->Printf( "%d admin(s)\n", admins );
		return;
	}
	if ( args.Argc() == 2 && idStr::Icmp( word, "logout" ) == 0 ) {
		if ( !gameLocal.ClientModerationRequest( MP_MOD_ADMIN_LOGOUT, 0, 0, "" ) ) {
			common->Printf( "admin: the request could not be sent\n" );
		}
		return;
	}
	if ( args.Argc() != 1 ) {
		// The natural slip is the password after the command. The engine keeps such a
		// line out of the console history (idPrivateCommand::NamesAdminPassword).
		common->Printf( "admin: the password is not typed after the command. Set it once, then log in:\n"
			"  set net_clientAdminPassword \"...\"\n"
			"  admin\n"
			"The line just typed was not kept in the console history. 'admin logout' gives the right up.\n" );
		return;
	}
	const char *password = cvarSystem->GetCVarString( "net_clientAdminPassword" );
	if ( password == NULL || password[ 0 ] == '\0' ) {
		common->Printf( "admin: set the password first: set net_clientAdminPassword \"...\" (it is kept out of the console history and the config), then run 'admin'\n" );
		return;
	}
	if ( !gameLocal.mpGame.RequestAdminAuthentication( password ) ) {
		common->Printf( "admin: the login could not be sent; wait until you are in the game, then try again\n" );
		return;
	}
	common->Printf( "admin: asked the server; its answer appears with the chat\n" );
}

// These change who may play on the server, so they run where the server does:
// its console, a listen server's host, or rcon.
static bool ModerationServerCommand( const char *command ) {
	if ( !gameLocal.isMultiplayer || !gameLocal.isServer ) {
		common->Printf( "%s: only the server runs this; from a client use 'rcon %s ...'\n", command, command );
		return false;
	}
	return true;
}

/*
================
idGameLocal::Ban_f
================
*/
void idGameLocal::Ban_f( const idCmdArgs &args ) {
	if ( gameLocal.isMultiplayer && gameLocal.isClient ) {
		ModerationClientCommand( MP_MOD_ADMIN_BAN, args );
		return;
	}
	if ( !ModerationServerCommand( "ban" ) ) {
		return;
	}
	mpModWords_t words;
	const char *argv[ MODERATION_MAX_ARGS ];
	const int argc = ModerationArgs( args, words, argv );
	if ( argc < 2 ) {
		common->Printf( "usage: ban <client slot|name|address[/prefix]|id:<identity>> [time] [reason]\n"
			"  time: 30m, 2h, 7d, 1w, a number of minutes, or perm (the default)\n"
			"  a player is banned by the address they hold (over IPv6, their /64) and by their identity\n"
			"  an IPv6 address may be written in brackets, and needs them before a port: [2001:db8::1]:28004\n" );
		return;
	}
	// A slot number first, then an address, then a name. An address is read
	// before a name so that no player can take a ban meant for an address by
	// calling themselves "192.0.2.7" or "[".
	int next = 2;
	mpModAddress_t address;
	MPModClearAddress( address );
	int prefixBits = 0;
	idStr name;
	int clientNum = -1;
	char text[ 128 ];
	text[ 0 ] = '\0';
	char identity[ MP_MOD_IDENTITY_BYTES ];
	identity[ 0 ] = '\0';
	if ( idStr::Icmpn( argv[ 1 ], MP_MOD_IDENTITY_PREFIX, sizeof( MP_MOD_IDENTITY_PREFIX ) - 1 ) == 0 ) {
		// MP-17: an identity, connected or not
		if ( !MPModParseIdentity( argv[ 1 ], identity ) ) {
			common->Printf( "ban: '%s' is not an identity; status shows each player's sixteen hex digits\n", argv[ 1 ] );
			return;
		}
	} else if ( ModerationIsSlot( argv[ 1 ] ) ) {
		clientNum = ModerationFindClient( argv[ 1 ] );
		if ( clientNum < 0 ) {
			common->Printf( "ban: no player is in slot %s\n", argv[ 1 ] );
			return;
		}
	} else {
		const int used = MPModJoinAddressArgs( argc, argv, 1, text, sizeof( text ) );
		if ( used > 0 && strpbrk( text, ".:" ) != NULL && MPModParseAddress( text, address, prefixBits ) ) {
			next = 1 + used;
		} else {
			MPModClearAddress( address );
			clientNum = ModerationFindClient( argv[ 1 ] );
			if ( clientNum < 0 ) {
				common->Printf( "ban: '%s' is neither a connected player nor an address\n",
					text[ 0 ] != '\0' ? text : argv[ 1 ] );
				return;
			}
		}
	}
	if ( clientNum >= 0 ) {
		address = gameLocal.GetClientModAddress( clientNum );
		if ( address.family == 0 && gameLocal.mpGame.ClientIdentity( clientNum )[ 0 ] == '\0' ) {
			common->Printf( "ban: '%s' has no network address (a bot, or this server's own player)\n", argv[ 1 ] );
			return;
		}
		// a player is banned by the range they hold: over IPv6 that is their /64
		prefixBits = MPModPlayerPrefixBits( address );
		name = gameLocal.userInfo[ clientNum ].GetString( "ui_name" );
	}
	mpModTime_t duration = 0;
	next += MPModJoinDurationArgs( argc, argv, next, duration );
	idStr reason;
	for ( int i = next; i < argc; i++ ) {
		if ( reason.Length() > 0 ) {
			reason += " ";
		}
		reason += argv[ i ];
	}
	if ( identity[ 0 ] != '\0' ) {
		// a connected player's name labels the ban when they hold the identity
		for ( int i = 0; i < MAX_CLIENTS && name.Length() == 0; i++ ) {
			if ( idStr::Cmp( gameLocal.mpGame.ClientIdentity( i ), identity ) == 0 ) {
				name = gameLocal.userInfo[ i ].GetString( "ui_name" );
			}
		}
		gameLocal.ServerBanIdentity( identity, duration, name.c_str(), reason.c_str(), "console" );
		return;
	}
	if ( clientNum >= 0 ) {
		gameLocal.ServerBanClient( clientNum, duration, reason.c_str(), "console" );
		return;
	}
	gameLocal.ServerBan( address, prefixBits, duration, name.c_str(), reason.c_str(), "console" );
}

/*
================
idGameLocal::Unban_f
================
*/
void idGameLocal::Unban_f( const idCmdArgs &args ) {
	if ( !ModerationServerCommand( "unban" ) ) {
		return;
	}
	mpModWords_t words;
	const char *argv[ MODERATION_MAX_ARGS ];
	const int argc = ModerationArgs( args, words, argv );
	char text[ 128 ];
	if ( argc < 2 || 1 + MPModJoinAddressArgs( argc, argv, 1, text, sizeof( text ) ) != argc ) {
		common->Printf( "usage: unban <row of banlist|address[/prefix]|id:<identity>>\n" );
		return;
	}
	int index = -1;
	const int count = gameLocal.GetBanListCount();
	if ( idStr::IsNumeric( text ) && strchr( text, '.' ) == NULL ) {
		index = atoi( text ) - 1;
	} else {
		// an address and prefix, or (MP-17) "id:" and an identity
		index = MPModBanFindTarget( *gameLocal.modBans, text );
	}
	const mpModBan_t *ban = index >= 0 && index < count ? gameLocal.GetBan( index ) : NULL;
	if ( ban == NULL ) {
		common->Printf( "unban: no ban matches '%s'; 'banlist' shows the rows\n", text );
		return;
	}
	char range[ MP_MOD_RANGE_TEXT_BYTES ];
	MPModBanTarget( *ban, range, sizeof( range ) );
	gameLocal.ModerationLogBan( "unban", "console", *ban );
	gameLocal.RemoveBan( index );
	common->Printf( "unbanned %s\n", range );
}

/*
================
idGameLocal::BanList_f
================
*/
void idGameLocal::BanList_f( const idCmdArgs &args ) {
	if ( !ModerationServerCommand( "banlist" ) ) {
		return;
	}
	if ( args.Argc() > 1 ) {
		if ( idStr::Icmp( args.Argv( 1 ), "reload" ) != 0 ) {
			common->Printf( "usage: banlist [reload]\n" );
			return;
		}
		// read a list that was edited by hand while the server ran
		gameLocal.SaveBanList();
		gameLocal.LoadBanList();
		// a ban the file gained covers whoever is connected, as one made here does
		for ( int i = 0; i < MAX_CLIENTS; i++ ) {
			if ( i == gameLocal.localClientNum || gameLocal.clientModAddress[ i ].family == 0 ||
				gameLocal.FindBan( gameLocal.clientModAddress[ i ] ) == NULL ) {
				continue;
			}
			gameLocal.ModerationLogClient( "kick", "console", i, "banned", 0 );
			gameLocal.mpGame.BeginServerDrop( i );
			networkSystem->ServerDropClient( i, "#str_107239" );
			gameLocal.mpGame.EndServerDrop( i );
		}
	}
	const int count = gameLocal.GetBanListCount();
	const mpModTime_t now = ModerationNow();
	for ( int i = 0; i < count; i++ ) {
		const mpModBan_t *ban = gameLocal.GetBan( i );
		char range[ MP_MOD_RANGE_TEXT_BYTES ];
		char left[ 32 ];
		MPModBanTarget( *ban, range, sizeof( range ) );
		MPModFormatDuration( ban->expires != 0 ? ban->expires - now : 0, left, sizeof( left ) );
		common->Printf( "%3d  %-24s %-10s %s%s%s\n", i + 1, range, left, ban->name,
			ban->reason[ 0 ] != '\0' ? " : " : "", ban->reason );
	}
	common->Printf( "%d ban(s)\n", count );
}

/*
================
idGameLocal::Mute_f
"mute" and "unmute": the server's text mute, by slot or name.
================
*/
void idGameLocal::Mute_f( const idCmdArgs &args ) {
	const bool mute = idStr::Icmp( args.Argv( 0 ), "unmute" ) != 0;
	if ( gameLocal.isMultiplayer && gameLocal.isClient ) {
		ModerationClientCommand( mute ? MP_MOD_ADMIN_MUTE : MP_MOD_ADMIN_UNMUTE, args );
		return;
	}
	if ( !ModerationServerCommand( mute ? "mute" : "unmute" ) ) {
		return;
	}
	mpModWords_t words;
	const char *argv[ MODERATION_MAX_ARGS ];
	const int argc = ModerationArgs( args, words, argv );
	if ( argc < 2 || ( !mute && argc != 2 ) ) {
		common->Printf( mute ? "usage: mute <client slot|name> [time]\n  time: 30m, 2h, a number of minutes; without one, until unmute or a server restart\n" :
			"usage: unmute <client slot|name>\n" );
		return;
	}
	const int clientNum = ModerationFindClient( argv[ 1 ] );
	if ( clientNum < 0 ) {
		common->Printf( "%s: no connected player matches '%s'\n", args.Argv( 0 ), argv[ 1 ] );
		return;
	}
	mpModTime_t duration = 0;
	if ( mute && argc > 2 && 2 + MPModJoinDurationArgs( argc, argv, 2, duration ) != argc ) {
		idStr rest;
		for ( int i = 2; i < argc; i++ ) {
			rest += i > 2 ? " " : "";
			rest += argv[ i ];
		}
		common->Printf( "mute: '%s' is not a time\n", rest.c_str() );
		return;
	}
	if ( !gameLocal.SetClientTextMute( clientNum, mute, duration, "console" ) ) {
		common->Printf( mute ? "mute: this server's own player cannot be muted\n" : "unmute: that player is not muted\n" );
		return;
	}
	char length[ 32 ];
	MPModFormatDuration( duration, length, sizeof( length ) );
	common->Printf( mute ? "muted %s (%s)\n" : "unmuted %s\n", gameLocal.userInfo[ clientNum ].GetString( "ui_name" ),
		duration > 0 ? length : "until unmute" );
}

/*
================
idGameLocal::Status_f
Who is connected, from where. Identity is "-" until MP-17 gives players one.
================
*/
void idGameLocal::Status_f( const idCmdArgs &args ) {
	if ( !ModerationServerCommand( "status" ) ) {
		return;
	}
	common->Printf( "map: %s  game type: %s\n", gameLocal.serverInfo.GetString( "si_map" ), gameLocal.serverInfo.GetString( "si_gametype" ) );
	common->Printf( "slot ping  state      muted address                                  identity         name\n" );
	int players = 0;
	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		if ( gameLocal.entities[ i ] == NULL || !gameLocal.entities[ i ]->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}
		const idPlayer *player = static_cast<const idPlayer *>( gameLocal.entities[ i ] );
		char address[ MP_MOD_RANGE_TEXT_BYTES ];
		const char *where = address;
		if ( botManager.IsBot( i ) ) {
			where = "bot";
		} else if ( i == gameLocal.localClientNum ) {
			where = "local";
		} else if ( gameLocal.clientModAddress[ i ].family != 0 ) {
			MPModFormatAddress( gameLocal.clientModAddress[ i ], address, sizeof( address ) );
		} else {
			where = "unknown";
		}
		// openQ4 MP-17: the identity the player proved on this server
		const char *identity = gameLocal.mpGame.ClientIdentity( i );
		common->Printf( "%4d %4d  %-10s %-5s %-40s %-16s %s\n", i, networkSystem->ServerGetClientPing( i ),
			player->spectating ? "spectator" : "playing", gameLocal.IsClientTextMuted( i ) ? "yes" : "no", where,
			identity[ 0 ] != '\0' ? identity : "-", gameLocal.userInfo[ i ].GetString( "ui_name" ) );
		players++;
	}
	common->Printf( "%d client(s)\n", players );
}

/*
================
idGameLocal::Shutdown_f
"serverShutdown": stop the server without cutting a match short.
================
*/
void idGameLocal::Shutdown_f( const idCmdArgs &args ) {
	if ( !ModerationServerCommand( "serverShutdown" ) ) {
		return;
	}
	mpModWords_t words;
	const char *argv[ MODERATION_MAX_ARGS ];
	const int argc = ModerationArgs( args, words, argv );
	if ( argc < 2 ) {
		common->Printf( "usage: serverShutdown <match|now|time|cancel> [reason]\n"
			"  match: when this match ends; time: 90s, 5m, 1h or a number of minutes\n" );
		if ( gameLocal.modShutdownMode == MOD_SHUTDOWN_MATCH ) {
			common->Printf( "the server shuts down when this match ends\n" );
		} else if ( gameLocal.modShutdownMode == MOD_SHUTDOWN_TIME ) {
			char length[ 32 ];
			const mpModTime_t left = gameLocal.modShutdownAt - ModerationNow();
			MPModFormatDuration( left > 0 ? left : 1, length, sizeof( length ) );
			common->Printf( "the server shuts down in %s\n", length );
		} else if ( gameLocal.modShutdownMode == MOD_SHUTDOWN_NONE ) {
			common->Printf( "no shutdown is waiting\n" );
		}
		return;
	}
	if ( gameLocal.modShutdownMode == MOD_SHUTDOWN_STOPPING || gameLocal.modShutdownMode == MOD_SHUTDOWN_STOPPED ) {
		common->Printf( "serverShutdown: the server is stopping now\n" );
		return;
	}
	if ( idStr::Icmp( argv[ 1 ], "cancel" ) == 0 ) {
		if ( !gameLocal.CancelServerShutdown( "console" ) ) {
			common->Printf( "serverShutdown: no shutdown is waiting\n" );
		}
		return;
	}
	bool afterMatch = false;
	mpModTime_t seconds = 0;
	int next = 2;
	if ( idStr::Icmp( argv[ 1 ], "match" ) == 0 ) {
		afterMatch = true;
	} else if ( idStr::Icmp( argv[ 1 ], "now" ) != 0 ) {
		const int used = MPModJoinDurationArgs( argc, argv, 1, seconds );
		if ( used == 0 || seconds <= 0 ) {
			common->Printf( "serverShutdown: '%s' is not match, now, cancel or a time\n", argv[ 1 ] );
			return;
		}
		next = 1 + used;
	}
	idStr reason;
	for ( int i = next; i < argc; i++ ) {
		if ( reason.Length() > 0 ) {
			reason += " ";
		}
		reason += argv[ i ];
	}
	gameLocal.ScheduleServerShutdown( afterMatch, seconds, reason.c_str(), "console" );
}
// RAVEN END

/*
================
idGameLocal::ServerSendInstanceReliableMessageExcluding
Works like networkSystem->ServerSendReliableMessageExcluding, but only sends to entities in the owner's instance
================
*/
void idGameLocal::ServerSendInstanceReliableMessageExcluding( const idEntity* owner, int excludeClient, const idBitMsg& msg ) {
	int i;
	assert( isServer );

	if ( owner == NULL ) {
		assert( false ); // does not happen in q4mp, previous revisions of the code would have crashed
		networkSystem->ServerSendReliableMessageExcluding( excludeClient, msg );	// NOTE: will also transmit through repeater
		return;
	}

	for ( i = 0; i < numClients; i++ ) {
		if ( i == excludeClient ) {
			continue;
		}

		if ( entities[ i ] == NULL ) {
			continue;
		}

		if ( entities[ i ]->GetInstance() != owner->GetInstance() ) {
			continue;
		}

		networkSystem->ServerSendReliableMessageNoDemo( i, msg );
	}

	// Record one semantic instance route instead of duplicating every physical
	// recipient in the demo stream.
	networkSystem->ServerRecordInstanceReliableMessage( owner->GetInstance(), excludeClient, msg );

// jmarshall - engine has no repeater reliable channel
	//networkSystem->RepeaterSendReliableMessageExcluding( excludeClient, msg );
// jmarshall end
}

/*
================
idGameLocal::ServerSendInstanceReliableMessage
Works like networkSystem->ServerSendReliableMessage, but only sends to entities in the owner's instance
================
*/
void idGameLocal::ServerSendInstanceReliableMessage( const idEntity* owner, int clientNum, const idBitMsg& msg ) {
	int i;
	assert( isServer );

	if ( owner == NULL ) {
		networkSystem->ServerSendReliableMessage( clientNum, msg );	// NOTE: will also transmit through repeater
		return;
	}

	if ( clientNum == -1 ) {
		for ( i = 0; i < numClients; i++ ) {
			if ( entities[ i ] == NULL ) {
				continue;
			}

			if ( entities[ i ]->GetInstance() != owner->GetInstance() ) {
				continue;
			}

			networkSystem->ServerSendReliableMessageNoDemo( i, msg );
		}

		networkSystem->ServerRecordInstanceReliableMessage( owner->GetInstance(), -1, msg );
// jmarshall - engine has no repeater reliable channel
		//networkSystem->RepeaterSendReliableMessage( -1, msg );
// jmarshall end
	} else {
		assert( false ); // not used in q4mp
		if ( entities[ clientNum ] && entities[ clientNum ]->GetInstance() == owner->GetInstance() ) {
			networkSystem->ServerSendReliableMessage( clientNum, msg );

			if ( owner->GetInstance() == 0 ) {
				for ( i = 0; i < maxViewer; i++ ) {
					if ( !viewers[ i ].active ) {
						continue;
					}

					if ( viewers[ i ].origin.followClient != clientNum && !viewers[ i ].nopvs ) {
						continue;
					}

// jmarshall - engine has no repeater reliable channel
					//networkSystem->RepeaterSendReliableMessage( clientNum, msg, false, i );
// jmarshall end
				}
			}
		}
	}
}

/*
===============
idGameLocal::RepeaterAppendUnreliableMessage
===============
*/
void idGameLocal::RepeaterAppendUnreliableMessage( int icl, const idBitMsg &msg, const idBitMsg *header ) {
	assert( isRepeater );

	// send the normally unreliable message as a reliable message
	if ( ( viewers[ icl ].nopvs && ( cvarSystem->GetCVarInteger( "g_repeaterReliableOnly" ) >= 1 ) ) || ( cvarSystem->GetCVarInteger( "g_repeaterReliableOnly" ) >= 2 ) ) {
		byte msgBuf[ MAX_GAME_MESSAGE_SIZE ];
		idBitMsg reliable;

		reliable.Init( msgBuf, sizeof( msgBuf ) );
		reliable.WriteByte( GAME_RELIABLE_MESSAGE_UNRELIABLE_MESSAGE );
		if ( header ) {
			reliable.WriteData( header->GetData(), header->GetSize() );
		}
		reliable.WriteData( msg.GetData(), msg.GetSize() );

// jmarshall - engine has no repeater reliable channel
		//networkSystem->RepeaterSendReliableMessage( icl, reliable );
// jmarshall end
		return;
	}

	// send the message
	if ( header ) {
		viewerUnreliableMessages[ icl ].AddConcat( header->GetData(), header->GetSize(), msg.GetData(), msg.GetSize(), false );
	} else {
		viewerUnreliableMessages[ icl ].Add( msg.GetData(), msg.GetSize(), false );
	}
}

/*
===============
idGameLocal::RepeaterUnreliableMessage
for spectating support, we have to loop through the clients and emit to the spectator client too
note that a clientNum == -1 means send to everyone
header and msg are concatenated in case a header is needed.
===============
*/
void idGameLocal::RepeaterUnreliableMessage( const idBitMsg &msg, const int clientNum, const idBitMsg *header ) {
	if ( !isRepeater ) {
		return;
	}

	int icl;

	for ( icl = 0; icl < maxViewer; icl++ ) {
		if ( !viewers[ icl ].active ) {
			continue;
		}

		if ( clientNum != -1 && viewers[ icl ].origin.followClient != clientNum && !viewers[ icl ].nopvs ) {
			// not global, not to the followed client

			// not following anyone
			if ( viewers[ icl ].origin.followClient == -1 ) {
				continue;
			}
	
			// the followed player doesn't exist
			if ( !entities[ viewers[ icl ].origin.followClient ] ) {
				continue;
			}
	
			// the followed player isn't a player
			if ( !entities[ viewers[ icl ].origin.followClient ]->IsType( idPlayer::GetClassType() ) ) {
				continue;
			}
	
			// following a valid player
	
			idPlayer *player = static_cast< idPlayer * >( entities[ viewers[ icl ].origin.followClient ] );
			if ( !player->spectating || viewers[ icl ].origin.followClient != player->spectator ) {
				// .. but not the right player
				continue;
			}

			// fallthrough
		}

		RepeaterAppendUnreliableMessage( icl, msg, header );
	}
}

// Ceiling on how much unreliable traffic one client's batch may hold between
// snapshots.  idGameLocal::WriteSnapshot pours the whole batch into the snapshot
// message, which is MAX_MESSAGE_SIZE and does NOT allow overflow, so an
// unbounded batch could take the server down with a fatal idBitMsg overflow just
// as the fight got busy.  The batch only grows when snapshots have stopped going
// out, and at 20 snapshots a second this cap is 80KB/s of effect traffic - far
// above net_serverMaxClientRate, so it can never bind before the rate limiter
// does.  It exists to make the failure a logged drop instead of a crash.
static const int MAX_UNRELIABLE_BATCH_BYTES = 4096;
// Per-client throttle so a full batch reports once rather than once per message.
static const int UNRELIABLE_OVERFLOW_WARN_INTERVAL = 5000;
static int unreliableOverflowWarnTime[ MAX_CLIENTS + 1 ] = { 0 };

/*
===============
idGameLocal::QueueUnreliableMessage

Appends to one client's unreliable batch, which is drained into that client's
next snapshot.  The batch is a fixed ring; when it fills the message is simply
gone, and until now it went silently.  That is exactly the case worth hearing
about, because everything queued here is combat feedback - hit scans, impact
effects, hit info - and the batch only fills when snapshots have stopped going
out, which is when a player is most likely to notice things missing.
===============
*/
void idGameLocal::QueueUnreliableMessage( int clientNum, const idBitMsg &msg ) {
	if ( clientNum < 0 || clientNum > MAX_CLIENTS ) {
		return;
	}
	// Bots have no receiving connection or snapshots to drain a batch. Keep
	// the dedicated recording slot, and filter recipients rather than message
	// subjects so human spectators following a bot still get its feedback.
	if ( clientNum < MAX_CLIENTS && botManager.IsBot( clientNum ) ) {
		return;
	}
	idMsgQueue &batch = unreliableMessages[ clientNum ];
	if ( batch.GetTotalSize() + msg.GetSize() <= MAX_UNRELIABLE_BATCH_BYTES &&
		 batch.Add( msg.GetData(), msg.GetSize(), false ) ) {
		return;
	}
	if ( time >= unreliableOverflowWarnTime[ clientNum ] ) {
		unreliableOverflowWarnTime[ clientNum ] = time + UNRELIABLE_OVERFLOW_WARN_INTERVAL;
		common->Warning( "client %d: unreliable batch full (%d bytes), dropping a %d byte message - "
						 "snapshots are not draining it",
						 clientNum, batch.GetTotalSize(), msg.GetSize() );
	}
}

/*
===============
idGameLocal::SendUnreliableMessage
for spectating support, we have to loop through the clients and emit to the spectator client too
note that a clientNum == -1 means send to everyone
===============
*/
void idGameLocal::SendUnreliableMessage( const idBitMsg &msg, const int clientNum ) {
	int icl;
	idPlayer *player;
	
	for ( icl = 0; icl < numClients; icl++ ) {
		if ( icl == localClientNum || botManager.IsBot( icl ) ) {
			// not to local client
			// note that if local is spectated he will still get it
			continue;
		}
		if ( !entities[ icl ] ) {
			continue;
		}
		if ( clientNum != -1 && icl != clientNum ) {
			player = static_cast< idPlayer * >( entities[ icl ] );
			// drop all clients except the ones that follow the client we emit to
			if ( !player->spectating || player->spectator != clientNum ) {
				continue;
			}
		}
		QueueUnreliableMessage( icl, msg );
	}

	if ( demoState == DEMO_RECORDING || isRepeater ) {
		// record the type and destination for remap on readback
		idBitMsg	dest;
		byte		msgBuf[ 16 ];
		
		dest.Init( msgBuf, sizeof( msgBuf ) );
		dest.WriteByte( GAME_UNRELIABLE_RECORD_CLIENTNUM );
		dest.WriteChar( clientNum );
		
		if ( demoState == DEMO_RECORDING ) {
			unreliableMessages[ MAX_CLIENTS ].AddConcat( dest.GetData(), dest.GetSize(), msg.GetData(), msg.GetSize(), false );
		}

		RepeaterUnreliableMessage( msg, clientNum, &dest );
	}
}

/*
===============
idGameLocal::RecordUnreliableMessage

openQ4: a message for whoever plays a recording back, or watches through a
repeater, that no connected client is sent. Routed like a broadcast, so every
viewer of the recording receives it.
===============
*/
void idGameLocal::RecordUnreliableMessage( const idBitMsg &msg ) {
	if ( demoState != DEMO_RECORDING && !isRepeater ) {
		return;
	}

	idBitMsg	dest;
	byte		msgBuf[ 16 ];

	dest.Init( msgBuf, sizeof( msgBuf ) );
	dest.WriteByte( GAME_UNRELIABLE_RECORD_CLIENTNUM );
	dest.WriteChar( -1 );

	if ( demoState == DEMO_RECORDING ) {
		unreliableMessages[ MAX_CLIENTS ].AddConcat( dest.GetData(), dest.GetSize(), msg.GetData(), msg.GetSize(), false );
	}

	RepeaterUnreliableMessage( msg, -1, &dest );
}

/*
===============
idGameLocal::RepeaterUnreliableMessagePVS
Forwards an unreliable message to viewers based on PVS.
Concats header and msg in case a header needs to be added.
===============
*/
void idGameLocal::RepeaterUnreliableMessagePVS( const idBitMsg &msg, const int *areas, int numAreas, const idBitMsg *header ) {
	if ( !isRepeater ) {
		return;
	}

	int i, icl;
	pvsHandle_t pvsHandle[ 2 ];
	assert( numAreas <= 2 );

	for ( i = 0; i < numAreas; i++ ) {
		pvsHandle[ i ] = pvs.SetupCurrentPVS( areas[ i ], PVS_NORMAL );
	}

	for ( icl = 0; icl < maxViewer; icl ++ ) {
		if ( !viewers[ icl ].active ) {
			continue;
		}

		// if no areas are given, this is a global emit; also always emit for repeaters
		if ( numAreas && !viewers[ icl ].nopvs ) {
			// only send if pvs says this client can see it
			for ( i = 0; i < numAreas; i++ ) {
				if ( pvs.InCurrentPVS( pvsHandle[ i ], viewers[ icl ].pvsArea ) ) {
					RepeaterAppendUnreliableMessage( icl, msg, header );
					break;
				}
			}
		} else {
			// global, so always send it
			RepeaterAppendUnreliableMessage( icl, msg, header );
		}
	}

	for ( i = 0; i < numAreas; i++ ) {
		pvs.FreeCurrentPVS( pvsHandle[ i ] );
	}
}

/*
===============
idGameLocal::SendUnreliableMessagePVS
instanceEnt to NULL for no instance checks
excludeClient to -1 for no exclusions
===============
*/
void idGameLocal::SendUnreliableMessagePVS( const idBitMsg &msg, const idEntity *instanceEnt, int area1, int area2 ) {
	int			icl;
	int			matchInstance = instanceEnt ? instanceEnt->GetInstance() : -1;
	int			areas[ 2 ];
	int			numEvAreas;

	numEvAreas = 0;
	if ( area1 != -1 ) {
		areas[ 0 ] = area1;
		numEvAreas++;
	}
	if ( area2 != -1 ) {
		areas[ numEvAreas ] = area2;
		numEvAreas++;
	}

	for ( icl = 0; icl < numClients; icl++ ) {
		if ( icl == localClientNum || botManager.IsBot( icl ) ) {
			// local client is always excluded
			continue;
		}
		if ( !entities[ icl ] ) {
			continue;
		}
		if ( matchInstance >= 0 && entities[ icl ]->GetInstance() != matchInstance ) {
			continue;
		}
		const bool clientHasPVS = clientsPVS[ icl ].i >= 0;

		// if no areas are given, this is a global emit
		if ( numEvAreas && clientHasPVS ) {
			// only send if pvs says this client can see it
			if ( !pvs.InCurrentPVS( clientsPVS[ icl ], areas, numEvAreas ) ) {
				continue;
			}
		}

		QueueUnreliableMessage( icl, msg );
	}

	if ( demoState == DEMO_RECORDING || isRepeater ) {
		// record the target areas to the message
		idBitMsg	dest;
		byte		msgBuf[ 16 ];

		dest.Init( msgBuf, sizeof( msgBuf ) );
		if ( instanceEnt != NULL ) {
			dest.WriteByte( GAME_UNRELIABLE_RECORD_AREAS_INSTANCE );
		} else {
			dest.WriteByte( GAME_UNRELIABLE_RECORD_AREAS );
		}
		dest.WriteLong( area1 );
		dest.WriteLong( area2 );
		if ( instanceEnt != NULL ) {
			dest.WriteChar( instanceEnt->GetInstance() );
		}

		if ( demoState == DEMO_RECORDING ) {
			unreliableMessages[ MAX_CLIENTS ].AddConcat( dest.GetData(), dest.GetSize(), msg.GetData(), msg.GetSize(), false );
		}

		RepeaterUnreliableMessagePVS( msg, areas, numEvAreas, &dest );
	}
}

/*
===============
idGameLocal::ClientReadUnreliableMessages
===============
*/
void idGameLocal::ClientReadUnreliableMessages( const idBitMsg &_msg ) {
	idMsgQueue	localQueue;
	int			size;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];
	idBitMsg	msg;

	localQueue.ReadFrom( _msg );

	msg.Init( msgBuf, sizeof( msgBuf ) );
	while ( localQueue.Get( msg.GetData(), msg.GetMaxSize(), size, false ) ) {
		msg.SetSize( size );
		msg.BeginReading();
		ProcessUnreliableMessage( msg );
		msg.BeginWriting();
	}
}

/*
===============
idGameLocal::DemoReplayInAreas
checks if our current demo replay view ( server demo ) matches the areas given
===============
*/
bool idGameLocal::IsDemoReplayInAreas( const int areas[2], int numAreas ) {
	idVec3			view;
	pvsHandle_t		handle;

	bool			ret;

	assert( IsServerDemoPlaying() );

	if ( !numAreas ) {
		return true;
	}

	if ( followPlayer == -1 ) {
		view = gameLocal.GetLocalPlayer()->GetPhysics()->GetOrigin();
	} else {
		view = entities[ followPlayer ]->GetPhysics()->GetOrigin();
	}

	// could probably factorize this, at least for processing all unreliable messages, maybe at a higher level of the loop?
	handle = pvs.SetupCurrentPVS( view );
	ret = pvs.InCurrentPVS( handle, areas, numAreas );
	pvs.FreeCurrentPVS( handle );
	return ret;   
}

/*
===============
idGameLocal::ProcessUnreliableMessage
===============
*/
void idGameLocal::ProcessUnreliableMessage( const idBitMsg &msg ) {
	if ( IsServerDemoPlaying() ) {
		if ( msg.GetRemainingReadBits() < 8 ) {
			common->Warning( "Ignoring truncated server-demo unreliable route" );
			return;
		}
		int record_type = msg.ReadByte();
		if ( record_type < GAME_UNRELIABLE_RECORD_CLIENTNUM ||
			 record_type >= GAME_UNRELIABLE_RECORD_COUNT ) {
			common->Warning( "Ignoring invalid server-demo unreliable route %d", record_type );
			return;
		}
		switch ( record_type ) {
		case GAME_UNRELIABLE_RECORD_CLIENTNUM: {
			if ( msg.GetRemainingReadBits() < 8 ) {
				common->Warning( "Ignoring truncated targeted server-demo unreliable route" );
				return;
			}
			int client = msg.ReadChar();

			RepeaterUnreliableMessage( msg, client );

			if ( client != -1 ) {
				// unreliable was targetted
				if ( followPlayer != client ) {
					// either free flying, or following someone else
					return;
				}
			}
			break;
			}
		case GAME_UNRELIABLE_RECORD_AREAS:
		case GAME_UNRELIABLE_RECORD_AREAS_INSTANCE: {
			if ( msg.GetRemainingReadBits() < ( record_type == GAME_UNRELIABLE_RECORD_AREAS_INSTANCE ? 72 : 64 ) ) {
				common->Warning( "Ignoring truncated server-demo unreliable area route" );
				return;
			}
			int areas[ 2 ];
			int numEvAreas = 0;

			areas[ numEvAreas ] = msg.ReadLong();
			if ( areas[ numEvAreas ] != -1 ) ++numEvAreas;
			areas[ numEvAreas ] = msg.ReadLong();
			if ( areas[ numEvAreas ] != -1 ) ++numEvAreas;

			if ( record_type == GAME_UNRELIABLE_RECORD_AREAS_INSTANCE ) {
				const int routeInstance = msg.ReadChar();
				if ( routeInstance < 0 || routeInstance >= MAX_INSTANCES ) {
					common->Warning( "Ignoring invalid server-demo unreliable instance %d", routeInstance );
					return;
				}
				idEntity *viewEntity = NULL;
				if ( followPlayer >= 0 && followPlayer < MAX_CLIENTS ) {
					viewEntity = entities[ followPlayer ];
				}
				if ( viewEntity == NULL ) {
					viewEntity = GetLocalPlayer();
				}
				if ( viewEntity == NULL || viewEntity->GetInstance() != routeInstance ) {
					return;
				}
			}

			RepeaterUnreliableMessagePVS( msg, areas, numEvAreas );

			if ( !IsDemoReplayInAreas( areas, numEvAreas ) ) {
				return;
			}
			break;
			}
		}
	}

	if ( msg.GetRemainingReadBits() < 8 ) {
		common->Warning( "Ignoring truncated server-demo unreliable message" );
		return;
	}
	int type = msg.ReadByte();
	if ( type < GAME_UNRELIABLE_MESSAGE_EVENT ||
		 type >= GAME_UNRELIABLE_MESSAGE_COUNT ) {
		common->Warning( "Ignoring invalid server-demo unreliable message %d", type );
		return;
	}
	switch ( type ) {
		case GAME_UNRELIABLE_MESSAGE_EVENT: {
			idEntityPtr<idEntity> p;
			int spawnId = msg.ReadBits( 32 );
			p.SetSpawnId( spawnId );
			
			if ( p.GetEntity() ) {
				p.GetEntity()->ClientReceiveEvent( msg.ReadByte(), time, msg );
			} else {
				Warning( "ProcessUnreliableMessage: no local entity 0x%x for event %d", spawnId & ( ( 1 << GENTITYNUM_BITS ) - 1 ), msg.ReadByte() );
			}
			break;
		}
		case GAME_UNRELIABLE_MESSAGE_EFFECT: {
			idCQuat				quat;
			idVec3				origin, origin2;
			rvClientEffect*		effect;
			effectCategory_t	category;
			const idDecl		*decl;
				
			decl = idGameLocal::ReadDecl( msg, DECL_EFFECT );

			origin.x = msg.ReadFloat( );
			origin.y = msg.ReadFloat( );
			origin.z = msg.ReadFloat( );
				
			quat.x = msg.ReadFloat( );
			quat.y = msg.ReadFloat( );
			quat.z = msg.ReadFloat( );
			
			origin2.x = msg.ReadFloat( );
			origin2.y = msg.ReadFloat( );
			origin2.z = msg.ReadFloat( );

			category = ( effectCategory_t )msg.ReadByte();

			bool loop = msg.ReadBits( 1 ) != 0;
			bool predictBit = msg.ReadBits( 1 ) != 0;

			if ( !decl || ( predictBit && g_predictedProjectiles.GetBool() ) ) {
				break;
			}
			if ( bse->Filtered( decl->GetName(), category ) ) {
				break;
			}

			// Filtered() already applies category rate-limiting.
			const idMat3 axis = quat.ToMat3();
			effect = new rvClientEffect( decl );
			effect->SetOrigin( origin );
			effect->SetAxis( axis );
			effect->SetGravity( GetCurrentGravity( origin, axis ) );
			effect->Play( time, loop, origin2 );

			break;
		}
		case GAME_UNRELIABLE_MESSAGE_HITSCAN: {
			ClientHitScan( msg );
			break;
		}
		// openQ4: attacker only hit feedback
		case GAME_UNRELIABLE_MESSAGE_HITINFO: {
			mpGame.damageNumbers.ClientReceive( msg );
			break;
		}
		// openQ4 (MP-15): team overlay data and admitted item timers
		case GAME_UNRELIABLE_MESSAGE_TEAMINFO: {
			mpGame.teamInfo.ClientReceive( msg );
			break;
		}
		case GAME_UNRELIABLE_MESSAGE_ITEMTIMERS: {
			mpGame.itemTimers.ClientReceive( msg );
			break;
		}
#ifdef _USE_VOICECHAT
		case GAME_UNRELIABLE_MESSAGE_VOICEDATA_SERVER: {
			mpGame.ReceiveAndPlayVoiceData( msg );
			break;
		}
#else
		case GAME_UNRELIABLE_MESSAGE_VOICEDATA_SERVER: {
			break;
		}
#endif
		default: {
			Error( "idGameLocal::ProcessUnreliableMessage() - Unknown unreliable message '%d'\n", type );
		}
	}
}

/*
===============
idGameLocal::WriteNetworkInfo
===============
*/
void idGameLocal::WriteNetworkInfo( idFile* file, int clientNum ) {
	int				i, j;
	snapshot_t		*snapshot;
	entityState_t	*entityState;

	if ( !IsServerDemoRecording() ) {

		// save the current states
		for ( i = 0; i < MAX_GENTITIES; i++ ) {
			entityState = clientEntityStates[clientNum][i];
			file->WriteBool( !!entityState );
			if ( entityState ) {
				file->WriteInt( entityState->entityNumber );
				file->WriteInt( entityState->state.GetSize() );
				file->Write( entityState->state.GetData(), entityState->state.GetSize() );
			}
		}

		// save the PVS states
		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			for ( j = 0; j < ENTITY_PVS_SIZE; j++ ) {
				file->WriteInt( clientPVS[i][j] );
			}
		}

	}

	// players ( including local client )
	j = 0;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( !entities[i] ) {
			continue;
		}
		j++;
	}
	file->WriteInt( j );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( !entities[i] ) {
			continue;
		}
		file->WriteInt( i );
		file->WriteInt( spawnIds[ i ] );
	}

	if ( !IsServerDemoRecording() ) {

		// write number of snapshots so on readback we know how many to allocate
		i = 0;
		for ( snapshot = clientSnapshots[ clientNum ]; snapshot; snapshot = snapshot->next ) {
			i++;
		}
		file->WriteInt( i );

		for ( snapshot = clientSnapshots[ clientNum ]; snapshot; snapshot = snapshot->next ) {
			file->WriteInt( snapshot->sequence );

			// write number of entity states in the snapshot
			i = 0;
			for ( entityState = snapshot->firstEntityState; entityState; entityState = entityState->next ) {
				i++;
			}
			file->WriteInt( i );
		
			for ( entityState = snapshot->firstEntityState; entityState; entityState = entityState->next ) {
				file->WriteInt( entityState->entityNumber );
				file->WriteInt( entityState->state.GetSize() );
				file->Write( entityState->state.GetData(), entityState->state.GetSize() );
			}

			file->Write( snapshot->pvs, sizeof( snapshot->pvs ) );
		}

	}

	// write the 'initial reliables' data
	mpGame.WriteNetworkInfo( file, clientNum );
}

/*
===============
idGameLocal::ReadNetworkInfo
===============
*/
void idGameLocal::ReadNetworkInfo( int gameTime, idFile* file, int clientNum ) {
	int				i, j, num, numStates, stateSize;
	snapshot_t		*snapshot, **lastSnap;
	entityState_t	*entityState, **lastState;

	assert( clientNum == MAX_CLIENTS || !IsServerDemoPlaying() );
	InitLocalClient( clientNum );

	time = gameTime;
	previousTime = gameTime;

	// force new frame
	realClientTime = 0;
	isNewFrame = true;

	// clear the snapshot entity list
	snapshotEntities.Clear();

	if ( !IsServerDemoPlaying() || IsRepeaterDemoPlaying() ) {

		for ( i = 0; i < MAX_GENTITIES; i++ ) {
			bool isValid;
		
			file->ReadBool( isValid );
			if ( isValid ) {
				clientEntityStates[clientNum][i] = entityStateAllocator.Alloc();
				entityState = clientEntityStates[clientNum][i];
				entityState->next = NULL;
				file->ReadInt( entityState->entityNumber );
				file->ReadInt( stateSize );
				entityState->state.Init( entityState->stateBuf, sizeof( entityState->stateBuf ) );
				entityState->state.SetSize( stateSize );
				file->Read( entityState->state.GetData(), stateSize );
			} else {
				clientEntityStates[clientNum][i] = NULL;
			}
		}

		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			for ( j = 0; j < ENTITY_PVS_SIZE; j++ ) {
				file->ReadInt( clientPVS[i][j] );
			}
		}

	}

	// spawn player entities. ( numClients is not a count but the watermark of client indexes )
	if ( file->ReadInt( num ) != sizeof( num ) || num < 0 || num > MAX_CLIENTS ) {
		common->Warning( "idGameLocal::ReadNetworkInfo: invalid player count %d", num );
		return;
	}
	int previousClient = -1;
	for ( i = 0; i < num; i++ ) {
		int icl, spawnId;
		if ( file->ReadInt( icl ) != sizeof( icl ) ||
			 file->ReadInt( spawnId ) != sizeof( spawnId ) ||
			 icl < 0 || icl >= MAX_CLIENTS || icl <= previousClient ) {
			common->Warning( "idGameLocal::ReadNetworkInfo: invalid player index %d", icl );
			return;
		}
		SpawnPlayer( icl );
		spawnIds[ icl ] = spawnId;
		numClients = icl + 1;
		previousClient = icl;
	}

	if ( !IsServerDemoPlaying() || IsRepeaterDemoPlaying() ) {

		file->ReadInt( num );
		lastSnap = &clientSnapshots[ localClientNum ];
		for ( i = 0; i < num; i++ ) {
			snapshot = snapshotAllocator.Alloc();
			snapshot->firstEntityState = NULL;
			snapshot->next = NULL;
			file->ReadInt( snapshot->sequence );

			file->ReadInt( numStates );
			lastState = &snapshot->firstEntityState;
			for ( j = 0; j < numStates; j++ ) {			
				entityState = entityStateAllocator.Alloc();
				file->ReadInt( entityState->entityNumber );
				file->ReadInt( stateSize );
				entityState->state.Init( entityState->stateBuf, sizeof( entityState->stateBuf ) );
				entityState->state.SetSize( stateSize );
				file->Read( entityState->state.GetData(), stateSize );
				entityState->next = NULL;
				assert( !(*lastState ) );
				*lastState = entityState;
				lastState = &entityState->next;
			}

			file->Read( snapshot->pvs, sizeof( snapshot->pvs ) );

			assert( !(*lastSnap) );
			*lastSnap = snapshot;
			lastSnap = &snapshot->next;
		}

		// spawn entities
		for ( i = 0; i < ENTITYNUM_NONE; i++ ) {
			int						spawnId, entityDefNumber;
			idBitMsgDelta			deltaMsg;
			idDict					args;
			entityState_t			*base = clientEntityStates[clientNum][i];
			idEntity				*ent = entities[i];
			const idDeclEntityDef	*decl;
		
			if ( !base ) {
				continue;
			}
			base->state.BeginReading();
			deltaMsg.InitReading( &base->state, NULL, NULL );

			spawnId = deltaMsg.ReadBits( 32 - GENTITYNUM_BITS );
			entityDefNumber = NetworkDeclIndex( DECL_ENTITYDEF, deltaMsg.ReadBits( entityDefBits ) );
			if ( entityDefNumber < 0 || entityDefNumber >= declManager->GetNumDecls( DECL_ENTITYDEF ) ) {
				common->Warning( "ReadNetworkInfo: invalid entity definition for entity %d", i );
				continue;
			}

			if ( !ent || ent->entityDefNumber != entityDefNumber || spawnId != spawnIds[ i ] ) {

				delete ent;

				spawnCount = spawnId;

				args.Clear();
				args.SetInt( "spawn_entnum", i );
				args.Set( "name", va( "entity%d", i ) );

				// assume any items spawned from a server-snapshot are in our instance
				if( gameLocal.GetLocalPlayer() ) {
					args.SetInt( "instance", gameLocal.GetLocalPlayer()->GetInstance() );
				}

				assert( entityDefNumber >= 0 );
				if ( entityDefNumber >= declManager->GetNumDecls( DECL_ENTITYDEF ) ) {
					Error( "server has %d entityDefs instead of %d", entityDefNumber, declManager->GetNumDecls( DECL_ENTITYDEF ) );
				}
				decl = static_cast< const idDeclEntityDef * >( declManager->DeclByIndex( DECL_ENTITYDEF, entityDefNumber, false ) );
				assert( decl && decl->GetType() == DECL_ENTITYDEF );
				args.Set( "classname", decl->GetName() );					
				if ( !SpawnEntityDef( args, &ent ) || !entities[i] ) {
					Error( "Failed to spawn entity with classname '%s' of type '%s'", decl->GetName(), decl->dict.GetString("spawnclass") );
				}
			}

			// add the entity to the snapshot list
			ent->snapshotNode.AddToEnd( snapshotEntities );
		
			// read the class specific data from the snapshot
			ent->ReadFromSnapshot( deltaMsg );

			// read the player state from player entities
			if ( ent->entityNumber < MAX_CLIENTS ) {
				if ( deltaMsg.ReadBits( 1 ) == 1 ) {
					assert( ent->IsType( idPlayer::GetClassType() ) );
					static_cast< idPlayer * >( ent )->ReadPlayerStateFromSnapshot( deltaMsg );
				}
			}

			// this is useful. for instance on idPlayer, resets stuff so powerups actually appear
			ent->ClientUnstale();
		}

		{
			// specific state read for game and player state
			idBitMsgDelta	deltaMsg;
			entityState_t	*base = clientEntityStates[clientNum][ENTITYNUM_NONE];

			// it's possible to have a recording start right at CS_INGAME and not have a base for reading this yet
			if ( base ) {
				base->state.BeginReading();
				deltaMsg.InitReading( &base->state, NULL, NULL );
				ReadGameStateFromSnapshot( deltaMsg );
			}
		}

		// set self spectating state according to userinfo settings
		if ( !IsRepeaterDemoPlaying() ) {
			GetLocalPlayer()->Spectate( idStr::Icmp( userInfo[ clientNum ].GetString( "ui_spectate" ), "Spectate" ) == 0 );
		}

	}

	// read the 'initial reliables' data
	mpGame.ReadNetworkInfo( file, clientNum );
}

/*
============
idGameLocal::FillAutoTrackPlayer

openQ4: what an auto-tracking camera weighs about a player it may follow. The
caller has already decided that it may: a demo's playback camera and a live
spectator's server camera share this row and the chooser, not their authority.
============
*/
void idGameLocal::FillAutoTrackPlayer( const idPlayer *candidate, int slot, mpAutoTrackPlayer &row ) {
	row.eligible = true;
	row.alive = !candidate->spectating && candidate->health > 0;
	row.team = IsTeamGame() ? candidate->team : -1;
	row.flag = candidate->PowerUpActive( POWERUP_CTF_MARINEFLAG ) ||
		candidate->PowerUpActive( POWERUP_CTF_STROGGFLAG ) || candidate->PowerUpActive( POWERUP_CTF_ONEFLAG );
	static const int timed[] = { POWERUP_QUADDAMAGE, POWERUP_HASTE, POWERUP_REGENERATION, POWERUP_INVISIBILITY,
		POWERUP_ADRENALINE };
	row.powerups = 0;
	for ( int p = 0; p < static_cast<int>( sizeof( timed ) / sizeof( timed[ 0 ] ) ); p++ ) {
		row.powerups += candidate->PowerUpActive( timed[ p ] ) ? 1 : 0;
	}
	row.score = mpGame.GetScore( slot );
}

/*
============
idGameLocal::UpdateDemoAutoTrack

openQ4: moves an auto-tracking playback camera. Attack and jump are the
camera's own controls, so either one hands the camera back to the operator.
============
*/
void idGameLocal::UpdateDemoAutoTrack( idPlayer *viewer, const usercmd_t &cmd ) {
	const int mode = demo_autoTrack.GetInteger();
	if ( mode == MP_AUTOTRACK_OFF || viewer == NULL || !viewer->IsFakeClient() ) {
		return;
	}
	if ( ( cmd.buttons & BUTTON_ATTACK ) != 0 || cmd.upmove > 0 ) {
		demo_autoTrack.SetInteger( MP_AUTOTRACK_OFF );
		common->Printf( "MVD auto-tracking off\n" );
		return;
	}
	const char *teamName = demo_autoTrackTeam.GetString();
	const int teamFilter = !idStr::Icmp( teamName, "marine" ) ? TEAM_MARINE :
		!idStr::Icmp( teamName, "strogg" ) ? TEAM_STROGG : -1;

	mpAutoTrackPlayer players[ MP_AUTOTRACK_CLIENTS ];
	static_assert( MP_AUTOTRACK_CLIENTS == MAX_CLIENTS, "auto-track covers every client slot" );
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( entities[ i ] == NULL || !entities[ i ]->IsType( idPlayer::GetClassType() ) ||
			 !mpGame.CanSpectatorFollow( viewer->entityNumber, i ) ) {
			continue;
		}
		FillAutoTrackPlayer( static_cast< const idPlayer * >( entities[ i ] ), i, players[ i ] );
	}

	const int followed = GetDemoFollowClient();
	const int target = demoAutoTrack.Choose( mode, IsTeamGame() ? teamFilter : -1, followed, players, time );
	if ( target >= 0 && target != followed ) {
		SetDemoFollowClient( target );
	}
}

/*
============
idGameLocal::SetDemoState
============
*/
void idGameLocal::SetDemoState( demoState_t state, bool _serverDemo, bool _timeDemo ) {
	if ( demoState == DEMO_RECORDING && state == DEMO_NONE && serverDemo ) {
		ServerClientDisconnect( MAX_CLIENTS );
	}
	if ( state == DEMO_PLAYING && demoState != DEMO_PLAYING ) {
		demoAutoTrack.Reset();
	}
	demoState = state;
	serverDemo = _serverDemo;
	timeDemo = _timeDemo;
	// openQ4 MP-22: entity definitions are sent in as many bits as the sender's
	// count takes; a retail recording's is its own paks' count
	entityDefBits = idMath::BitsForInteger( NetworkDeclCount( DECL_ENTITYDEF ) ) + 1;
}

/*
============
idGameLocal::SetRepeaterState
============
*/
void idGameLocal::SetRepeaterState( bool isRepeater, bool serverIsRepeater ) {
	this->isRepeater = isRepeater;
	this->serverIsRepeater = serverIsRepeater;

	if ( cvarSystem->GetCVarInteger( "net_serverDownload" ) == 3 ) {
		networkSystem->HTTPEnable( this->isServer || this->isRepeater );
	}

	ReallocViewers( isRepeater ? cvarSystem->GetCVarInteger( "ri_maxViewers" ) : 0 );
}

/*
===============
idGameLocal::ValidateDemoProtocol
===============
*/
bool idGameLocal::ValidateDemoProtocol( int minor_ref, int minor ) {
	demo_protocol = minor;
	return IsDemoProtocolCompatible( minor_ref, minor );
}

/*
===============
idGameLocal::IsDemoProtocolCompatible
===============
*/
bool idGameLocal::IsDemoProtocolCompatible( int minor_ref, int minor ) const {
	// openQ4: protocol 2.42 changed what a snapshot holds for players, projectiles and
	// entity tags; a demo recorded with 2.41 is read in its own layout
	// (LegacySnapshotFormat), so recordings made before the change still play.
	// openQ4 MP-22: and a retail Quake 4 1.4.2 network demo (2.85) reads in 1.4.2's layout
	// (RetailSnapshotFormat), decoded and replayed by the engine's multi-view demo player.
	return ( minor_ref == minor ||
		( minor_ref == GAME_SNAPSHOT_PROTOCOL && minor == GAME_SNAPSHOT_PROTOCOL_LEGACY ) ||
		( minor_ref == GAME_SNAPSHOT_PROTOCOL && minor == GAME_SNAPSHOT_PROTOCOL_RETAIL ) );
}

/*
===============
idGameLocal::GetMVDSchemaVersion
===============
*/
void idGameLocal::GetMVDSchemaVersion( int &major, int &minor ) const {
	major = 1;
	minor = 0;
}

/*
===============
idGameLocal::IsMVDSchemaCompatible
===============
*/
bool idGameLocal::IsMVDSchemaCompatible( int major, int minor ) const {
	return major == 1 && minor >= 0 && minor <= 0;
}

/*
===============
idGameLocal::RandomSpawn
===============
*/
idPlayerStart *idGameLocal::RandomSpawn( void ) {
	return spawnSpots[ random.RandomInt( spawnSpots.Num() ) ];
}
