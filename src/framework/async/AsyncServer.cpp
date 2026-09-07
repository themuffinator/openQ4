/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#include "AsyncNetwork.h"
#include "Rcon2Protocol.h"

#include "../Session_local.h"

#include <cstdint>

static ID_INLINE int AsyncServer_NextGameFrameMsec( int gameFrame ) {
	return common->GetUserCmdDeltaMsec( gameFrame + 1 );
}

const int MIN_RECONNECT_TIME			= 2000;
const int EMPTY_RESEND_TIME				= 500;
const int PING_RESEND_TIME				= 500;
const int NOINPUT_IDLE_TIME				= 30000;

const int HEARTBEAT_MSEC				= 5*60*1000;

const int CONNECTION_CHALLENGE_TIMEOUT_MSEC = 30000;
const int RCON2_CHALLENGE_TIMEOUT_MSEC = 10000;
const int RCON2_RESEND_MIN_MSEC = 500;
const int RCON2_RATE_WINDOW_MSEC = 10000;
const int RCON2_RATE_MAX_CHALLENGES = 5;
const int RCON2_FAILURE_WINDOW_MSEC = 60000;
const int RCON2_FAILURE_MAX_ATTEMPTS = 5;
const int RCON2_FAILURE_BLOCK_MSEC = 30000;
const int OOB_RATE_WINDOW_MSEC = 1000;
const int OOB_INFO_MAX_PER_SOURCE = 16;
const int OOB_CHALLENGE_MAX_PER_SOURCE = 4;
const int OOB_INFO_MAX_GLOBAL = 512;
const int OOB_CHALLENGE_MAX_GLOBAL = 256;

static ID_INLINE bool AsyncServer_SameEndpoint( const netadr_t &left, const netadr_t &right ) {
	return left.port == right.port && Sys_CompareNetAdrBase( left, right );
}

static ID_INLINE std::uint32_t AsyncServer_Elapsed( int now, int then ) {
	return static_cast<std::uint32_t>( now ) - static_cast<std::uint32_t>( then );
}

static ID_INLINE bool AsyncServer_TimeBefore( int now, int future ) {
	return static_cast<std::int32_t>( static_cast<std::uint32_t>( now ) -
		static_cast<std::uint32_t>( future ) ) < 0;
}

static bool AsyncServer_SecureConnectionId( int &identifier ) {
	std::uint32_t randomValue = 0;
	if ( !Sys_GetSecureRandomBytes( &randomValue, sizeof( randomValue ) ) ) {
		return false;
	}
	identifier = static_cast<int>( randomValue & CONNECTIONLESS_MESSAGE_ID_MASK );
	if ( identifier == CONNECTIONLESS_MESSAGE_ID_MASK ) {
		identifier = 0;
	}
	return true;
}

static void AsyncServer_ClearChallenge( challenge_t &challenge ) {
	challenge.valid = false;
	memset( &challenge.address, 0, sizeof( challenge.address ) );
	challenge.clientId = 0;
	challenge.challenge = 0;
	challenge.time = 0;
	challenge.pingTime = 0;
	challenge.connected = false;
	challenge.authState = CDK_WAIT;
	challenge.authReply = AUTH_NONE;
	challenge.authReplyMsg = AUTH_REPLY_WAITING;
	challenge.authReplyPrint.Clear();
	challenge.guid[ 0 ] = '\0';
	challenge.OS = 0;
}

static void AsyncServer_ClearChallenges( challenge_t challenges[ MAX_CHALLENGES ] ) {
	for ( int index = 0; index < MAX_CHALLENGES; ++index ) {
		AsyncServer_ClearChallenge( challenges[ index ] );
	}
}

// openQ4: below this much room in the message channel there is no point building
// a snapshot - the channel would refuse it and the game would have already
// consumed the client's unreliable message queue writing it.
const int MIN_SNAPSHOT_SEND_SIZE		= 1024;

// must be kept in sync with authReplyMsg_t
const char* authReplyMsg[] = {
	//	"Waiting for authorization",
	"#str_07204",
	//	"Client unknown to auth",
	"#str_07205",
	//	"Access denied - CD Key in use",
	"#str_07206",
	//	"Auth custom message", // placeholder - we propagate a message from the master
	"#str_07207",
	//	"Authorize Server - Waiting for client"
	"#str_07208"
};

const char* authReplyStr[] = {
	"AUTH_NONE",
	"AUTH_OK",
	"AUTH_WAIT",
	"AUTH_DENY"
};

/*
==================
idAsyncServer::idAsyncServer
==================
*/
idAsyncServer::idAsyncServer( void ) {
	int i;

	active = false;
	realTime = 0;
	serverTime = 0;
	serverId = 0;
	serverDataChecksum = 0;
	localClientNum = -1;
	gameInitId = 0;
	gameFrame = 0;
	gameTime = 0;
	gameTimeResidual = 0;
	AsyncServer_ClearChallenges( challenges );
	memset( rcon2Challenges, 0, sizeof( rcon2Challenges ) );
	memset( rconRateLimits, 0, sizeof( rconRateLimits ) );
	memset( oobRateLimits, 0, sizeof( oobRateLimits ) );
	memset( userCmds, 0, sizeof( userCmds ) );
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		ClearClient( i );
	}
	serverReloadingEngine = false;
	nextHeartbeatTime = 0;
	nextAsyncStatsTime = 0;
	noRconOutput = true;
	rcon2VerifierInitialized = false;
	rcon2VerifierValid = false;
	memset( rcon2Salt, 0, sizeof( rcon2Salt ) );
	memset( rcon2Verifier, 0, sizeof( rcon2Verifier ) );
	oobWindowStart = 0;
	oobInfoResponses = 0;
	oobChallengeResponses = 0;
	lastAuthTime = 0;

	memset( stats_outrate, 0, sizeof( stats_outrate ) );
	stats_current = 0;
	stats_average_sum = 0;
	stats_max = 0;
	stats_max_index = 0;
}

/*
==================
idAsyncServer::InitPort
==================
*/
bool idAsyncServer::InitPort( void ) {
	int lastPort;
	const int configuredPort = cvarSystem->GetCVarInteger( "net_port" );
	if ( configuredPort < 0 || configuredPort > 65535 ) {
		common->Printf( "Invalid net_port %d; expected 0 (automatic) or 1..65535\n", configuredPort );
		return false;
	}

	// if this is the first time we have spawned a server, open the UDP port
	if ( !serverPort.GetPort() ) {
		if ( configuredPort != 0 ) {
			if ( !serverPort.InitForPort( configuredPort ) ) {
				common->Printf( "Unable to open server on port %d (net_port)\n", configuredPort );
				return false;
			}
		} else {
			// scan for multiple ports, in case other servers are running on this IP already
			for ( lastPort = 0; lastPort < NUM_SERVER_PORTS; lastPort++ ) {
				if ( serverPort.InitForPort( PORT_SERVER + lastPort ) ) {
					break;
				}
			}
			if ( lastPort >= NUM_SERVER_PORTS ) {
				common->Printf( "Unable to open server network port.\n" );
				return false;
			}
		}
	}

	return true;
}

/*
==================
idAsyncServer::ClosePort
==================
*/
void idAsyncServer::ClosePort( void ) {
	int i;

	serverPort.Close();
	for ( i = 0; i < MAX_CHALLENGES; i++ ) {
		challenges[ i ].authReplyPrint.Clear();
	}
	ClearRconSecurityState( true );
}

/*
==================
idAsyncServer::Spawn
==================
*/
void idAsyncServer::Spawn( void ) {
	int			i, size;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	netadr_t	from;

	// shutdown any current game
	session->Stop();

	if ( active ) {
		return;
	}

	if ( !InitPort() ) {
		return;
	}

	// trash any currently pending packets
	while( serverPort.GetPacket( from, msgBuf, size, sizeof( msgBuf ) ) ) {
	}

	// reset cheats cvars
	if ( !idAsyncNetwork::AreCheatsEnabled() ) {
		cvarSystem->ResetFlaggedVariables( CVAR_CHEAT );
	}

	AsyncServer_ClearChallenges( challenges );
	memset( rcon2Challenges, 0, sizeof( rcon2Challenges ) );
	memset( rconRateLimits, 0, sizeof( rconRateLimits ) );
	memset( oobRateLimits, 0, sizeof( oobRateLimits ) );
	oobWindowStart = serverTime;
	oobInfoResponses = 0;
	oobChallengeResponses = 0;
	memset( userCmds, 0, sizeof( userCmds ) );
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		ClearClient( i );
	}

	common->Printf( "Server spawned on port %i.\n", serverPort.GetPort() );

	// calculate a checksum on some of the essential data used
	serverDataChecksum = declManager->GetChecksum();
	common->DPrintf( "Server decl checksum: 0x%08x\n", static_cast<unsigned int>( serverDataChecksum ) );

	// A server id is only a short wire correlation value, not an authentication
	// secret, but making it unpredictable closes an unnecessary spoofing aid.
	if ( !AsyncServer_SecureConnectionId( serverId ) ) {
		common->Warning( "OS secure random unavailable; refusing to spawn network server" );
		serverPort.Close();
		return;
	}

	active = true;
	RefreshRcon2Verifier();

	nextHeartbeatTime = 0;
	nextAsyncStatsTime = 0;

	ExecuteMapChange();
}

/*
==================
idAsyncServer::Kill
==================
*/
void idAsyncServer::Kill( void ) {
	int i, j;

	if ( !active ) {
		return;
	}

	// drop all clients
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		DropClient( i, "#str_07135" );
	}

	// send some empty messages to the zombie clients to make sure they disconnect
	for ( j = 0; j < 4; j++ ) {
		for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
			if ( clients[i].clientState == SCS_ZOMBIE ) {
				if ( clients[i].channel.UnsentFragmentsLeft() ) {
					clients[i].channel.SendNextFragment( serverPort, serverTime );
				} else {
					SendEmptyToClient( i, true );
				}
			}
		}
		Sys_Sleep( 10 );
	}

	// reset any pureness
	fileSystem->ClearPureChecksums();

	active = false;
	ClearRconSecurityState( true );

	// shutdown any current game
	session->Stop();
}

/*
==================
idAsyncServer::ExecuteMapChange
==================
*/
void idAsyncServer::ExecuteMapChange( void ) {
	int			i;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	idStr		mapName;
	findFile_t	ff;
	bool		addonReload = false;
	//char		bestGameType[ MAX_STRING_CHARS ];

	assert( active );
	idAsyncNetwork::multiViewDemo.OnServerMapChange();

	// reset any pureness
	fileSystem->ClearPureChecksums();

	// make sure the map/gametype combo is good
	//game->GetBestGameType( cvarSystem->GetCVarString("si_map"), cvarSystem->GetCVarString("si_gametype"), bestGameType );
	//cvarSystem->SetCVarString("si_gametype", bestGameType );

	// initialize map settings
	cmdSystem->BufferCommandText( CMD_EXEC_NOW, "rescanSI" );

	mapName = "maps/";
	mapName += sessLocal.mapSpawnData.serverInfo.GetString( "si_map" );
	mapName.SetFileExtension( ".map" );
	ff = fileSystem->FindFile( mapName, !serverReloadingEngine );
	switch( ff ) {
	case FIND_NO:
		common->Printf( "Can't find map %s\n", mapName.c_str() );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
		return;
	case FIND_ADDON:
		// NOTE: we have no problem with addon dependencies here because if the map is in
		// an addon pack that's already on search list, then all it's deps are assumed to be on search as well
		common->Printf( "map %s is in an addon pak - reloading\n", mapName.c_str() );
		addonReload = true;
		break;
	default:
		break;
	}

	// if we are asked to do a full reload, the strategy is completely different
	if ( !serverReloadingEngine && ( addonReload || idAsyncNetwork::serverReloadEngine.GetInteger() != 0 ) ) {
		if ( idAsyncNetwork::serverReloadEngine.GetInteger() != 0 ) {
			common->Printf( "net_serverReloadEngine enabled - doing a full reload\n" );
		}
		// tell the clients to reconnect
		// FIXME: shouldn't they wait for the new pure list, then reload?
		// in a lot of cases this is going to trigger two reloadEngines for the clients
		// one to restart, the other one to set paks right ( with addon for instance )
		// can fix by reconnecting without reloading and waiting for the server to tell..
		for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
			if ( clients[ i ].clientState >= SCS_PUREWAIT && i != localClientNum ) {
				msg.Init( msgBuf, sizeof( msgBuf ) );
				msg.WriteByte( SERVER_RELIABLE_MESSAGE_RELOAD );
				SendReliableMessage( i, msg );
				clients[ i ].clientState = SCS_ZOMBIE; // so we don't bother sending a disconnect
			}
		}
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "reloadEngine" );
		serverReloadingEngine = true; // don't get caught in endless loop
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "spawnServer\n" );
		// decrease feature
		if ( idAsyncNetwork::serverReloadEngine.GetInteger() > 0 ) {
			idAsyncNetwork::serverReloadEngine.SetInteger( idAsyncNetwork::serverReloadEngine.GetInteger() - 1 );
		}
		return;
	}
	serverReloadingEngine = false;

	serverTime = 0;

	// initialize game id and time
	gameInitId ^= Sys_Milliseconds();	// NOTE: make sure the gameInitId is always a positive number because negative numbers have special meaning
	gameFrame = 0;
	gameTime = 0;
	gameTimeResidual = 0;
	memset( userCmds, 0, sizeof( userCmds ) );

	if ( idAsyncNetwork::serverDedicated.GetInteger() == 0 ) {
		InitLocalClient( 0, false );
	} else {
		localClientNum = -1;
	}

	// openQ4: a bot has no remote end to announce itself on the new map, and
	// InitClient below wipes the user info that carries its name, so both are
	// remembered here and restored once the map is up.
	bool	botClient[MAX_ASYNC_CLIENTS];
	idStr	botClientName[MAX_ASYNC_CLIENTS];

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		botClient[i] = ( clients[i].clientState >= SCS_PUREWAIT &&
						 clients[i].channel.GetRemoteAddress().type == NA_BOT );
		if ( botClient[i] ) {
			botClientName[i] = sessLocal.mapSpawnData.userInfo[i].GetString( "ui_name" );
		}
	}

	// re-initialize all connected clients for the new map
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState >= SCS_PUREWAIT && i != localClientNum ) {

			InitClient( i, clients[i].clientId, clients[i].clientRate );

			SendGameInitToClient( i );

			if ( sessLocal.mapSpawnData.serverInfo.GetBool( "si_pure" ) ) {
				clients[ i ].clientState = SCS_PUREWAIT;
			}
		}
	}

	// setup the game pak checksums
	// since this is not dependant on si_pure we catch anything bad before loading map
	if ( sessLocal.mapSpawnData.serverInfo.GetInt( "si_pure" ) ) {
		if ( !fileSystem->UpdateGamePakChecksums( ) ) {
			session->MessageBox( MSG_OK, common->GetLanguageDict()->GetString ( "#str_04337" ), common->GetLanguageDict()->GetString ( "#str_04338" ), true );
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
			return;
		}
	}

	// load map
	sessLocal.ExecuteMapChange();

	if ( localClientNum >= 0 ) {
		BeginLocalClient();
	} else {
		game->SetLocalClient( -1 );
	}

	// openQ4: put the bots back into the game.  A remote client does this for
	// itself with CLIENT_RELIABLE_MESSAGE_INGAME once it has the new map.
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( !botClient[i] ) {
			continue;
		}

		idDict botSpawnArgs;

		clients[i].clientState = SCS_INGAME;
		botSpawnArgs.Set( "ui_name", botClientName[i].c_str() );

		game->ServerClientBegin( i, true, botClientName[i].c_str() );
		SendUserInfoBroadcast( i, botSpawnArgs, true );
	}

	if ( sessLocal.mapSpawnData.serverInfo.GetInt( "si_pure" ) ) {
		// lock down the pak list
		fileSystem->UpdatePureServerChecksums( );
		// tell the clients so they can work out their pure lists
		for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
			if ( clients[ i ].clientState == SCS_PUREWAIT ) {
				if ( !SendReliablePureToClient( i ) ) {
					// Never promote a client when the server cannot prove the
					// prerequisites for its own pure policy.
					DropClient( i, "#str_04337" );
				}
			}
		}
	}

	// serverTime gets reset, force a heartbeat so timings restart
	MasterHeartbeat( true );
}

/*
==================
idAsyncServer::GetPort
==================
*/
int idAsyncServer::GetPort( void ) const {
	return serverPort.GetPort();
}

/*
===============
idAsyncServer::GetBoundAdr
===============
*/
netadr_t idAsyncServer::GetBoundAdr( void ) const {
	return serverPort.GetAdr();
}

/*
==================
idAsyncServer::GetOutgoingRate
==================
*/
int idAsyncServer::GetOutgoingRate( void ) const {
	int i, rate;

	rate = 0;
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		const serverClient_t &client = clients[i];

		if ( client.clientState >= SCS_CONNECTED ) {
			rate += client.channel.GetOutgoingRate();
		}
	}
	return rate;
}

/*
==================
idAsyncServer::GetIncomingRate
==================
*/
int idAsyncServer::GetIncomingRate( void ) const {
	int i, rate;

	rate = 0;
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		const serverClient_t &client = clients[i];

		if ( client.clientState >= SCS_CONNECTED ) {
			rate += client.channel.GetIncomingRate();
		}
	}
	return rate;
}

/*
==================
idAsyncServer::IsClientInGame
==================
*/
bool idAsyncServer::IsClientInGame( int clientNum ) const {
	return ( clients[clientNum].clientState >= SCS_INGAME );
}

/*
==================
idAsyncServer::GetClientPing
==================
*/
int idAsyncServer::GetClientPing( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 99999;
	} else {
		return client.clientPing;
	}
}

/*
==================
idAsyncServer::GetClientPrediction
==================
*/
int idAsyncServer::GetClientPrediction( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 99999;
	} else {
		return client.clientPrediction;
	}
}

/*
==================
idAsyncServer::GetClientTimeSinceLastPacket
==================
*/
int idAsyncServer::GetClientTimeSinceLastPacket( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 99999;
	} else {
		return serverTime - client.lastPacketTime;
	}
}

/*
==================
idAsyncServer::GetClientTimeSinceLastInput
==================
*/
int idAsyncServer::GetClientTimeSinceLastInput( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 99999;
	} else {
		return serverTime - client.lastInputTime;
	}
}

/*
==================
idAsyncServer::GetClientOutgoingRate
==================
*/
int idAsyncServer::GetClientOutgoingRate( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return -1;
	} else {
		return client.channel.GetOutgoingRate();
	}
}

/*
==================
idAsyncServer::GetClientIncomingRate
==================
*/
int idAsyncServer::GetClientIncomingRate( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return -1;
	} else {
		return client.channel.GetIncomingRate();
	}
}

/*
==================
idAsyncServer::GetClientOutgoingCompression
==================
*/
float idAsyncServer::GetClientOutgoingCompression( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 0.0f;
	} else {
		return client.channel.GetOutgoingCompression();
	}
}

/*
==================
idAsyncServer::GetClientIncomingCompression
==================
*/
float idAsyncServer::GetClientIncomingCompression( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 0.0f;
	} else {
		return client.channel.GetIncomingCompression();
	}
}

/*
==================
idAsyncServer::GetClientIncomingPacketLoss
==================
*/
float idAsyncServer::GetClientIncomingPacketLoss( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 0.0f;
	} else {
		return client.channel.GetIncomingPacketLoss();
	}
}

/*
==================
idAsyncServer::GetNumClients
==================
*/
int idAsyncServer::GetNumClients( void ) const {
	int ret = 0;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[ i ].clientState >= SCS_CONNECTED ) {
			ret++;
		}
	}
	return ret;
}

/*
==================
idAsyncServer::GetNumIdleClients
==================
*/
int idAsyncServer::GetNumIdleClients( void ) const {
	int ret = 0;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[ i ].clientState >= SCS_CONNECTED ) {
			// A bot supplies a user command every server frame, but it does so
			// by writing the game's own array rather than by sending a packet,
			// and receiving a packet is the only thing that refreshes
			// lastInputTime.  Ageing a bot out as idle would flip si_idleServer
			// on any server kept populated by bot_minPlayers, and the server
			// browser filters idle servers out by default - so the setting whose
			// entire purpose is to stop a server looking empty would be what
			// removed it from the list.
			if ( clients[ i ].channel.GetRemoteAddress().type == NA_BOT ) {
				continue;
			}
			if ( serverTime - clients[ i ].lastInputTime > NOINPUT_IDLE_TIME ) {
				ret++;
			}
		}
	}
	return ret;
}

/*
==================
idAsyncServer::DuplicateUsercmds
==================
*/
void idAsyncServer::DuplicateUsercmds( int frame, int time ) {
	int i, previousIndex, currentIndex;

	previousIndex = ( frame - 1 ) & ( MAX_USERCMD_BACKUP - 1 );
	currentIndex = frame & ( MAX_USERCMD_BACKUP - 1 );

	// duplicate previous user commands if no new commands are available for a client
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState == SCS_FREE ) {
			continue;
		}

		if ( idAsyncNetwork::DuplicateUsercmd( userCmds[previousIndex][i], userCmds[currentIndex][i], frame, time ) ) {
			clients[i].numDuplicatedUsercmds++;
		}
	}
}

/*
==================
idAsyncServer::ClearClient
==================
*/
void idAsyncServer::ClearClient( int clientNum ) {
	serverClient_t &client = clients[clientNum];
	client.clientId = 0;
	client.clientState = SCS_FREE;
	client.clientPrediction = 0;
	client.clientAheadTime = 0;
	client.clientRate = 0;
	client.clientPing = 0;
	client.gameInitSequence = 0;
	client.gameFrame = 0;
	client.gameTime = 0;
	client.channel.Shutdown();
	client.lastConnectTime = 0;
	client.lastEmptyTime = 0;
	client.lastPingTime = 0;
	client.lastSnapshotTime = 0;
	client.lastSnapshotGameFrame = 0;
	client.lastPacketTime = 0;
	client.lastInputTime = 0;
	client.snapshotSequence = 0;
	client.acknowledgeSnapshotSequence = 0;
	client.numDuplicatedUsercmds = 0;
	client.statsSnapshotsSent = 0;
	client.statsSnapshotBytes = 0;
	client.statsSendsRefusedByRate = 0;
}

/*
==================
idAsyncServer::InitClient
==================
*/
void idAsyncServer::InitClient( int clientNum, int clientId, int clientRate ) {
	int i;

	// clear the user info
	sessLocal.mapSpawnData.userInfo[ clientNum ].Clear();	// always start with a clean base

	// clear the server client
	serverClient_t &client = clients[clientNum];
	client.clientId = clientId;
	client.clientState = SCS_CONNECTED;
	client.clientPrediction = 0;
	client.clientAheadTime = 0;
	client.gameInitSequence = -1;
	client.gameFrame = 0;
	client.gameTime = 0;
	client.channel.ResetRate();
	client.clientRate = clientRate ? clientRate : idAsyncNetwork::serverMaxClientRate.GetInteger();
	client.channel.SetMaxOutgoingRate( Min( idAsyncNetwork::serverMaxClientRate.GetInteger(), client.clientRate ) );
	client.clientPing = 0;
	client.lastConnectTime = serverTime;
	client.lastEmptyTime = serverTime;
	client.lastPingTime = serverTime;
	client.lastSnapshotTime = serverTime;
	// openQ4: gameFrame restarts at 0 on a map change, so a frame number carried over
	// from the previous map would sit in the future and suppress everything the game
	// gates on it until the new map caught up.
	client.lastSnapshotGameFrame = gameFrame;
	client.lastPacketTime = serverTime;
	client.lastInputTime = serverTime;
	client.acknowledgeSnapshotSequence = 0;
	client.numDuplicatedUsercmds = 0;

	// clear the user commands
	for ( i = 0; i < MAX_USERCMD_BACKUP; i++ ) {
		memset( &userCmds[i][clientNum], 0, sizeof( userCmds[i][clientNum] ) );
	}

	// let the game know a player connected
	game->ServerClientConnect( clientNum, client.guid );
}

/*
==================
idAsyncServer::InitLocalClient
==================
*/
void idAsyncServer::InitLocalClient( int clientNum, bool isBot ) {
	netadr_t badAddress;

	InitClient( clientNum, 0, 0 );
	memset( &badAddress, 0, sizeof( badAddress ) );
// jmarshall
	if (isBot)
	{
		badAddress.type = NA_BOT;
	}
	else
	{
		badAddress.type = NA_BAD;
		localClientNum = clientNum;
	}
// jmarshall end
	clients[clientNum].channel.Init( badAddress, serverId );
	clients[clientNum].clientState = SCS_INGAME;
	sessLocal.mapSpawnData.userInfo[clientNum] = *cvarSystem->MoveCVarsToDict( CVAR_USERINFO );
}

/*
==================
idAsyncServer::BeginLocalClient
==================
*/
void idAsyncServer::BeginLocalClient( void ) {
	game->SetLocalClient( localClientNum );
	game->SetUserInfo( localClientNum, sessLocal.mapSpawnData.userInfo[localClientNum], false );
	game->ServerClientBegin( localClientNum, false, NULL );
}

/*
==================
idAsyncServer::LocalClientInput
==================
*/
void idAsyncServer::LocalClientInput( void ) {
	int index;

	if ( localClientNum < 0 ) {
		return;
	}

	index = gameFrame & ( MAX_USERCMD_BACKUP - 1 );
	userCmds[index][localClientNum] = usercmdGen->GetDirectUsercmd();
	userCmds[index][localClientNum].gameFrame = gameFrame;
	userCmds[index][localClientNum].gameTime = gameTime;
	if ( idAsyncNetwork::UsercmdInputChanged( userCmds[( gameFrame - 1 ) & ( MAX_USERCMD_BACKUP - 1 )][localClientNum], userCmds[index][localClientNum] ) ) {
		clients[localClientNum].lastInputTime = serverTime;
	}
	clients[localClientNum].gameFrame = gameFrame;
	clients[localClientNum].gameTime = gameTime;
	clients[localClientNum].lastPacketTime = serverTime;
}

/*
==================
idAsyncServer::DropClient
==================
*/
void idAsyncServer::DropClient( int clientNum, const char *reason ) {
	int			i;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	// openQ4: this is reachable from console commands and from the game, so validate
	// before the array access rather than trusting every caller to have done it.
	if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS ) {
		common->DWarning( "idAsyncServer::DropClient: client %d out of range", clientNum );
		return;
	}

	serverClient_t &client = clients[clientNum];

	if ( client.clientState <= SCS_ZOMBIE ) {
		return;
	}

	// Claim the transition before any reliable send or game callback. Either can
	// discover another failed queue and recursively request a drop; the zombie
	// state makes every slot's teardown an exactly-once operation.
	const serverClientState_t priorState = client.clientState;
	client.clientState = SCS_ZOMBIE;

	if ( priorState >= SCS_PUREWAIT && clientNum != localClientNum ) {
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.WriteByte( SERVER_RELIABLE_MESSAGE_DISCONNECT );
		msg.WriteLong( clientNum );
		msg.WriteString( reason );
		for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
			// clientNum so SCS_PUREWAIT client gets it's own disconnect msg
			if ( i == clientNum || clients[i].clientState >= SCS_CONNECTED ) {
				SendReliableMessage( i, msg );
			}
		}
	}

	reason = common->GetLanguageDict()->GetString( reason );
	common->Printf( "client %d %s\n", clientNum, reason );
	idAsyncNetwork::ShowClientDisconnectMessage(
		sessLocal.mapSpawnData.userInfo[ clientNum ].GetString( "ui_name" ), reason );

	// remove the player from the game
	game->ServerClientDisconnect( clientNum );
}

/*
==================
idAsyncServer::SendReliableMessage
==================
*/
void idAsyncServer::SendReliableMessage( int clientNum, const idBitMsg &msg ) {
	if ( clientNum == localClientNum ) {
		return;
	}
// jmarshall
	if (clients[clientNum].channel.GetRemoteAddress().type == NA_BOT)
		return;
// jmarshall end

	if ( !clients[ clientNum ].channel.SendReliableMessage( msg ) ) {
		clients[ clientNum ].channel.ClearReliableMessages();
		DropClient( clientNum, "#str_07136" );
	}
}

/*
==================
idAsyncServer::CheckClientTimeouts
==================
*/
void idAsyncServer::CheckClientTimeouts( void ) {
	int i, zombieTimeout, clientTimeout;

	zombieTimeout = serverTime - idAsyncNetwork::serverZombieTimeout.GetInteger() * 1000;
	clientTimeout = serverTime - idAsyncNetwork::serverClientTimeout.GetInteger() * 1000;

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[i];

		if ( i == localClientNum ) {
			continue;
		}

// jmarshall
		if (client.channel.GetRemoteAddress().type == NA_BOT)
		{
			// openQ4: a bot never sends a packet, so lastPacketTime is frozen
			// and the zombie timeout below can never fire for it.  Without this
			// every removed bot would hold its client slot for the rest of the
			// map and the server would eventually run out of slots.
			if ( client.clientState == SCS_ZOMBIE ) {
				client.channel.Shutdown();
				client.clientState = SCS_FREE;
			}
			continue;
		}
// jmarshall end

		if ( client.lastPacketTime > serverTime ) {
			client.lastPacketTime = serverTime;
			continue;
		}

		if ( client.clientState == SCS_ZOMBIE && client.lastPacketTime < zombieTimeout ) {
			client.channel.Shutdown();
			client.clientState = SCS_FREE;
			continue;
		}

		if ( client.clientState >= SCS_PUREWAIT && client.lastPacketTime < clientTimeout ) {
			DropClient( i, "#str_07137" );
			continue;
		}
	}
}

/*
==================
idAsyncServer::SendPrintBroadcast
==================
*/
void idAsyncServer::SendPrintBroadcast( const char *string ) {
	int			i;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_PRINT );
	msg.WriteString( string );

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState >= SCS_CONNECTED ) {
			SendReliableMessage( i, msg );
		}
	}
}

/*
==================
idAsyncServer::SendPrintToClient
==================
*/
void idAsyncServer::SendPrintToClient( int clientNum, const char *string ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return;
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_PRINT );
	msg.WriteString( string );

	SendReliableMessage( clientNum, msg );
}

/*
==================
idAsyncServer::SendUserInfoBroadcast
==================
*/
void idAsyncServer::SendUserInfoBroadcast( int userInfoNum, const idDict &info, bool sendToAll ) {
	idBitMsg		msg;
	byte			msgBuf[MAX_MESSAGE_SIZE];
	const idDict	*gameInfo;
	bool			gameModifiedInfo;

	gameInfo = game->SetUserInfo( userInfoNum, info, false );
	if ( gameInfo ) {
		gameModifiedInfo = true;
	} else {
		gameModifiedInfo = false;
		gameInfo = &info;
	}

	if ( userInfoNum == localClientNum ) {
		common->DPrintf( "local user info modified by server\n" );
		cvarSystem->SetCVarsFromDictByFlags( *gameInfo, CVAR_USERINFO );
		cvarSystem->ClearModifiedFlags( CVAR_USERINFO ); // don't emit back
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_CLIENTINFO );
	msg.WriteByte( userInfoNum );
	if ( gameModifiedInfo || sendToAll ) {
		msg.WriteBits( 0, 1 );
	} else {
		msg.WriteBits( 1, 1 );
	}

#if ID_CLIENTINFO_TAGS
	msg.WriteLong( sessLocal.mapSpawnData.userInfo[userInfoNum].Checksum() );
	common->DPrintf( "broadcast for client %d: 0x%x\n", userInfoNum, sessLocal.mapSpawnData.userInfo[userInfoNum].Checksum() );
	sessLocal.mapSpawnData.userInfo[userInfoNum].Print();
#endif

	if ( gameModifiedInfo || sendToAll ) {
		msg.WriteDeltaDict( *gameInfo, NULL );
	} else {
		msg.WriteDeltaDict( *gameInfo, &sessLocal.mapSpawnData.userInfo[userInfoNum] );
	}

	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState >= SCS_CONNECTED && ( sendToAll || i != userInfoNum || gameModifiedInfo ) ) {
			SendReliableMessage( i, msg );
		}
	}

	sessLocal.mapSpawnData.userInfo[userInfoNum] = *gameInfo;
}

/*
==================
idAsyncServer::UpdateUI
if the game modifies userInfo, it will call this through command system
we then need to get the info from the game, and broadcast to clients
( using DeltaDict and our current mapSpawnData as a base )
==================
*/
void idAsyncServer::UpdateUI( int clientNum ) {
	const idDict	*info = game->GetUserInfo( clientNum );

	if ( !info ) {
		common->Warning( "idAsyncServer::UpdateUI: no info from game\n" );
		return;
	}

	SendUserInfoBroadcast( clientNum, *info, true );
}

/*
==================
idAsyncServer::SendUserInfoToClient
==================
*/
void idAsyncServer::SendUserInfoToClient( int clientNum, int userInfoNum, const idDict &info ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( clients[clientNum].clientState < SCS_CONNECTED ) {
		return;
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_CLIENTINFO );
	msg.WriteByte( userInfoNum );
	msg.WriteBits( 0, 1 );

#if ID_CLIENTINFO_TAGS
	msg.WriteLong( 0 );
	common->DPrintf( "user info %d to client %d: NULL base\n", userInfoNum, clientNum );
#endif

	msg.WriteDeltaDict( info, NULL );

	SendReliableMessage( clientNum, msg );
}

/*
==================
idAsyncServer::SendSyncedCvarsBroadcast
==================
*/
void idAsyncServer::SendSyncedCvarsBroadcast( const idDict &cvars ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	int			i;

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_SYNCEDCVARS );
	msg.WriteDeltaDict( cvars, &sessLocal.mapSpawnData.syncedCVars );

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState >= SCS_CONNECTED ) {
			SendReliableMessage( i, msg );
		}
	}

	sessLocal.mapSpawnData.syncedCVars = cvars;
}

/*
==================
idAsyncServer::SendSyncedCvarsToClient
==================
*/
void idAsyncServer::SendSyncedCvarsToClient( int clientNum, const idDict &cvars ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( clients[clientNum].clientState < SCS_CONNECTED ) {
		return;
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_SYNCEDCVARS );
	msg.WriteDeltaDict( cvars, NULL );

	SendReliableMessage( clientNum, msg );
}

/*
==================
idAsyncServer::SendApplySnapshotToClient
==================
*/
void idAsyncServer::SendApplySnapshotToClient( int clientNum, int sequence ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_APPLYSNAPSHOT );
	msg.WriteLong( sequence );

	SendReliableMessage( clientNum, msg );
}

/*
==================
idAsyncServer::SendEmptyToClient
==================
*/
bool idAsyncServer::SendEmptyToClient( int clientNum, bool force ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	serverClient_t &client = clients[clientNum];

	if ( client.lastEmptyTime > realTime ) {
		client.lastEmptyTime = realTime;
	}

	if ( !force && ( realTime - client.lastEmptyTime < EMPTY_RESEND_TIME ) ) {
		return false;
	}

	if ( idAsyncNetwork::verbose.GetInteger() ) {
		common->Printf( "sending empty to client %d: gameInitId = %d, gameFrame = %d, gameTime = %d\n", clientNum, gameInitId, gameFrame, gameTime );
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteLong( gameInitId );
	msg.WriteByte( SERVER_UNRELIABLE_MESSAGE_EMPTY );

	client.channel.SendMessage( serverPort, serverTime, msg );

	client.lastEmptyTime = realTime;

	return true;
}

/*
==================
idAsyncServer::SendPingToClient
==================
*/
bool idAsyncServer::SendPingToClient( int clientNum ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	serverClient_t &client = clients[clientNum];

	if ( client.lastPingTime > realTime ) {
		client.lastPingTime = realTime;
	}

	if ( realTime - client.lastPingTime < PING_RESEND_TIME ) {
		return false;
	}

	if ( idAsyncNetwork::verbose.GetInteger() == 2 ) {
		common->Printf( "pinging client %d: gameInitId = %d, gameFrame = %d, gameTime = %d\n", clientNum, gameInitId, gameFrame, gameTime );
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteLong( gameInitId );
	msg.WriteByte( SERVER_UNRELIABLE_MESSAGE_PING );
	msg.WriteLong( realTime );

	client.channel.SendMessage( serverPort, serverTime, msg );

	client.lastPingTime = realTime;

	return true;
}

/*
==================
idAsyncServer::SendGameInitToClient
==================
*/
void idAsyncServer::SendGameInitToClient( int clientNum ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( idAsyncNetwork::verbose.GetInteger() ) {
		common->Printf( "sending gameinit to client %d: gameInitId = %d, gameFrame = %d, gameTime = %d\n", clientNum, gameInitId, gameFrame, gameTime );
	}

	serverClient_t &client = clients[clientNum];

	// clear the unsent fragments. might flood winsock but that's ok
	while( client.channel.UnsentFragmentsLeft() ) {
		client.channel.SendNextFragment( serverPort, serverTime );
	}			

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteLong( gameInitId );
	msg.WriteByte( SERVER_UNRELIABLE_MESSAGE_GAMEINIT );
	msg.WriteLong( gameFrame );
	msg.WriteLong( gameTime );
	msg.WriteDeltaDict( sessLocal.mapSpawnData.serverInfo, NULL );
	client.gameInitSequence = client.channel.SendMessage( serverPort, serverTime, msg );
}

/*
==================
idAsyncServer::SendSnapshotToClient
==================
*/
bool idAsyncServer::SendSnapshotToClient( int clientNum ) {
	int			i, j, index, numUsercmds;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	usercmd_t *	last;
	// openQ4: the game fills this dword-packed - one bit per client, ( n >> 5 ) / ( n & 31 ) -
	// and only writes ( numPVSClients + 31 ) >> 5 dwords.  It used to be sized and read as if
	// it were byte-packed, so clients 8 and up were gated on dwords the game never touched.
	dword		clientInPVS[( MAX_ASYNC_CLIENTS + 31 ) >> 5];

	serverClient_t &client = clients[clientNum];

	if ( serverTime - client.lastSnapshotTime < idAsyncNetwork::serverSnapshotDelay.GetInteger() ) {
		return false;
	}

	if ( idAsyncNetwork::verbose.GetInteger() == 2 ) {
		common->Printf( "sending snapshot to client %d: gameInitId = %d, gameFrame = %d, gameTime = %d\n", clientNum, gameInitId, gameFrame, gameTime );
	}

	// openQ4: building a snapshot is destructive - game->ServerWriteSnapshot below
	// serialises this client's queued unreliable messages (hitscans, effects) and
	// then clears the queue, and it advances the client's delta baseline.  If the
	// pending reliable backlog has already eaten the packet budget, the channel
	// refuses the send and all of that is thrown away silently.  Find out first,
	// and leave the queue alone so the batch rides the next snapshot instead.
	if ( client.channel.GetMaxSendMessageSize() < MIN_SNAPSHOT_SEND_SIZE ) {
		if ( idAsyncNetwork::verbose.GetInteger() ) {
			common->Printf( "client %d: reliable backlog leaves no room for a snapshot, deferring\n", clientNum );
		}
		return false;
	}

	// How far the client is ahead of the server minus the packet delay. The
	// client-reported time is untrusted, so keep every intermediate widened.
	const std::int64_t clientAheadTime = static_cast<std::int64_t>( client.gameTime ) -
		static_cast<std::int64_t>( gameTime ) - static_cast<std::int64_t>( gameTimeResidual );
	client.clientAheadTime = clientAheadTime < idMath::INT_MIN ? idMath::INT_MIN :
		( clientAheadTime > idMath::INT_MAX ? idMath::INT_MAX : static_cast<int>( clientAheadTime ) );

	// write the snapshot
	msg.Init( msgBuf, sizeof( msgBuf ) );
	// openQ4: a snapshot carries the game's whole queued unreliable batch as well
	// as every entity state, and idBitMsg's default answer to running out of room
	// is common->FatalError - so the busiest moment of a match could take the
	// server down outright.  Let it overflow, then find out below and drop that one
	// snapshot instead.  The overflow itself is reported by CheckOverflow.
	msg.SetAllowOverflow( true );
	msg.WriteLong( gameInitId );
	msg.WriteByte( SERVER_UNRELIABLE_MESSAGE_SNAPSHOT );
	msg.WriteLong( client.snapshotSequence );
	msg.WriteLong( gameFrame );
	msg.WriteLong( gameTime );
	msg.WriteByte( idMath::ClampChar( client.numDuplicatedUsercmds ) );
	msg.WriteShort( idMath::ClampShort( client.clientAheadTime ) );

	// write the game snapshot
	// openQ4: the game only fills the dwords it needs, so start from a known state rather
	// than whatever happened to be on the stack.  lastSnapshotGameFrame used to be a
	// literal 0, which made every "did this happen since your last snapshot" test in the
	// game true forever - the hit confirm bit latched on and the client retriggered the
	// hit feedback on every single snapshot for the rest of the life.
	memset( clientInPVS, 0, sizeof( clientInPVS ) );
	game->ServerWriteSnapshot( clientNum, client.snapshotSequence, msg, clientInPVS, MAX_ASYNC_CLIENTS, client.lastSnapshotGameFrame );

	// write the latest user commands from the other clients in the PVS to the snapshot
	for ( last = NULL, i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[i];

		if ( client.clientState == SCS_FREE || i == clientNum ) {
			continue;
		}

		// if the client is not in the PVS
		if ( !( clientInPVS[i >> 5] & ( 1 << ( i & 31 ) ) ) ) {
			continue;
		}

		int maxRelay = idMath::ClampInt( 1, MAX_USERCMD_RELAY, idAsyncNetwork::serverMaxUsercmdRelay.GetInteger() );

		// Max( 1, to always send at least one cmd, which we know we have because we call DuplicateUsercmds in RunFrame
		numUsercmds = Max( 1, Min( client.gameFrame, gameFrame + maxRelay ) - gameFrame );
		msg.WriteByte( i );
		msg.WriteByte( numUsercmds );
		for ( j = 0; j < numUsercmds; j++ ) {
			index = ( gameFrame + j ) & ( MAX_USERCMD_BACKUP - 1 );
			idAsyncNetwork::WriteUserCmdDelta( msg, userCmds[index][i], last );
			last = &userCmds[index][i];
		}
	}
	msg.WriteByte( MAX_ASYNC_CLIENTS );

	// openQ4: an overflowed idBitMsg has already rewound its own write cursor, so
	// what is in the buffer is a mixture of the head and the tail of the snapshot.
	// Sending it would hand the client an undecodable snapshot and disconnect it.
	if ( msg.IsOverflowed() ) {
		common->DWarning( "client %d: snapshot %d overflowed the message buffer and was dropped",
						  clientNum, client.snapshotSequence );
		client.lastSnapshotTime = serverTime;
		client.lastSnapshotGameFrame = gameFrame;
		client.snapshotSequence++;
		client.numDuplicatedUsercmds = 0;
		return true;
	}

	// openQ4: a refused send used to look exactly like a successful one.  The
	// snapshot and its unreliable batch are gone either way at this point, but say
	// so rather than leaving a client quietly starved of entity and effect updates.
	if ( client.channel.SendMessage( serverPort, serverTime, msg ) == -1 ) {
		common->DWarning( "client %d: snapshot %d dropped, %d bytes did not fit the channel",
						  clientNum, client.snapshotSequence, msg.GetSize() );
	} else {
		client.statsSnapshotsSent++;
		client.statsSnapshotBytes += msg.GetSize();
	}

	client.lastSnapshotTime = serverTime;
	client.lastSnapshotGameFrame = gameFrame;
	client.snapshotSequence++;
	client.numDuplicatedUsercmds = 0;

	return true;
}

/*
==================
idAsyncServer::ProcessUnreliableClientMessage
==================
*/
void idAsyncServer::ProcessUnreliableClientMessage( int clientNum, const idBitMsg &msg ) {
	int i, id, acknowledgeSequence, clientGameInitId, clientGameFrame, numUsercmds, index;
	usercmd_t *last;

	serverClient_t &client = clients[clientNum];

	if ( client.clientState == SCS_ZOMBIE ) {
		return;
	}
	if ( msg.GetRemainingReadBits() < 64 ) {
		DropClient( clientNum, "#str_07138" );
		return;
	}

	acknowledgeSequence = msg.ReadLong();
	clientGameInitId = msg.ReadLong();

	// while loading a map the client may send empty messages to keep the connection alive
	if ( clientGameInitId == GAME_INIT_ID_MAP_LOAD ) {
		if ( idAsyncNetwork::verbose.GetInteger() ) {			
			common->Printf( "ignore unreliable msg from client %d, gameInitId == ID_MAP_LOAD\n", clientNum );
		}
		return;
	}

	// check if the client is in the right game
	if ( clientGameInitId != gameInitId ) {
		if ( acknowledgeSequence > client.gameInitSequence ) {
			// the client is connected but not in the right game
			client.clientState = SCS_CONNECTED;

			// send game init to client
			SendGameInitToClient( clientNum );

			if ( sessLocal.mapSpawnData.serverInfo.GetBool( "si_pure" ) ) {
				client.clientState = SCS_PUREWAIT;
				if ( !SendReliablePureToClient( clientNum ) ) {
					DropClient( clientNum, "#str_04337" );
					return;
				}
			}
		} else if ( idAsyncNetwork::verbose.GetInteger() ) {
			common->Printf( "ignore unreliable msg from client %d, wrong gameInit, old sequence\n", clientNum );
		}
		return;
	}
	if ( msg.GetRemainingReadBits() < 32 + 8 ) {
		DropClient( clientNum, "#str_07138" );
		return;
	}

	client.acknowledgeSnapshotSequence = msg.ReadLong();

	if ( client.clientState == SCS_CONNECTED ) {

		// the client is in the right game
		client.clientState = SCS_INGAME;

		// send the user info of other clients
		for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
			if ( clients[i].clientState >= SCS_CONNECTED && i != clientNum ) {
				SendUserInfoToClient( clientNum, i, sessLocal.mapSpawnData.userInfo[i] );
			}
		}

		// send synchronized cvars to client
		SendSyncedCvarsToClient( clientNum, sessLocal.mapSpawnData.syncedCVars );

		SendEnterGameToClient( clientNum );

		// get the client running in the game
		game->ServerClientBegin( clientNum, false, NULL );

		// write any reliable messages to initialize the client game state
		game->ServerWriteInitialReliableMessages( clientNum );
	} else if ( client.clientState == SCS_INGAME ) {

		// apply the last snapshot the client received
		if ( game->ServerApplySnapshot( clientNum, client.acknowledgeSnapshotSequence ) ) {
			SendApplySnapshotToClient( clientNum, client.acknowledgeSnapshotSequence );
		}
	}

	// process the unreliable message
	id = msg.ReadByte();
	switch( id ) {
		case CLIENT_UNRELIABLE_MESSAGE_EMPTY: {
			if ( idAsyncNetwork::verbose.GetInteger() ) {
				common->Printf( "received empty message for client %d\n", clientNum );
			}
			break;
		}
		case CLIENT_UNRELIABLE_MESSAGE_PINGRESPONSE: {
			if ( msg.GetRemainingReadBits() < 32 ) {
				DropClient( clientNum, "#str_07138" );
				return;
			}
			const int echoedPingTime = msg.ReadLong();
			if ( echoedPingTime != client.lastPingTime ) {
				break;
			}
			client.clientPing = static_cast<int>( Min( AsyncServer_Elapsed( realTime, echoedPingTime ), 32767u ) );
			break;
		}
		case CLIENT_UNRELIABLE_MESSAGE_USERCMD: {

			if ( msg.GetRemainingReadBits() < 16 + 32 + 8 ) {
				DropClient( clientNum, "#str_07138" );
				return;
			}
			client.clientPrediction = msg.ReadShort();

			// read user commands
			clientGameFrame = msg.ReadLong();
			numUsercmds = msg.ReadByte();
			if ( numUsercmds < 1 || numUsercmds > MAX_USERCMD_PACKET_COMMANDS ||
				 clientGameFrame < numUsercmds - 1 ) {
				DropClient( clientNum, "#str_07138" );
				return;
			}
			if ( static_cast<int64>( clientGameFrame ) > static_cast<int64>( gameFrame ) + MAX_USERCMD_BACKUP ) {
				// A synchronous world reset can stall the server while a remote
				// client keeps predicting. Ignore commands outside our ring window;
				// the next snapshot resynchronizes the client without a disconnect.
				// Never decode them into the authoritative command history.
				return;
			}

			const int firstClientGameFrame = clientGameFrame - numUsercmds + 1;
			last = NULL;
			for ( int commandIndex = 0; commandIndex < numUsercmds; commandIndex++ ) {
				i = firstClientGameFrame + commandIndex;
				index = i & ( MAX_USERCMD_BACKUP - 1 );
				if ( !idAsyncNetwork::ReadUserCmdDelta( msg, userCmds[index][clientNum], last ) ) {
					DropClient( clientNum, "#str_07138" );
					return;
				}
				userCmds[index][clientNum].gameFrame = i;
				// openQ4: gameTime is authored by the client and reaches the game unchecked, where
				// anything that measures a command's age against the server clock becomes a dial
				// the client can turn.  An honest client sends exactly this value, so overwriting
				// it costs nothing and closes the hole permanently.  Staleness detection is
				// unaffected: a slot that was never received still holds a stamp from a whole
				// backup window ago.
				userCmds[index][clientNum].gameTime = i * common->GetUserCmdMSec();
				userCmds[index][clientNum].duplicateCount = 0;
				if ( idAsyncNetwork::UsercmdInputChanged( userCmds[( i - 1 ) & ( MAX_USERCMD_BACKUP - 1 )][clientNum], userCmds[index][clientNum] ) ) {
					client.lastInputTime = serverTime;
				}
				last = &userCmds[index][clientNum];
			}

			if ( last ) {
				client.gameFrame = last->gameFrame;
				client.gameTime = last->gameTime;
			}

			if ( idAsyncNetwork::verbose.GetInteger() == 2 ) {
				common->Printf( "received user command for client %d, gameInitId = %d, gameFrame, %d gameTime %d\n", clientNum, clientGameInitId, client.gameFrame, client.gameTime );
			}
			break;
		}
		default: {
			common->Printf( "unknown unreliable message %d from client %d\n", id, clientNum );
			break;
		}
	}
}

/*
==================
idAsyncServer::ProcessReliableClientMessages
==================
*/
void idAsyncServer::ProcessReliableClientMessages( int clientNum ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	byte		id;

	serverClient_t &client = clients[clientNum];

	msg.Init( msgBuf, sizeof( msgBuf ) );

	while ( client.channel.GetReliableMessage( msg ) ) {
		id = msg.ReadByte();
		if ( msg.IsReadOverflowed() ) {
			DropClient( clientNum, "#str_07138" );
			return;
		}
		switch( id ) {
			case CLIENT_RELIABLE_MESSAGE_CLIENTINFO: {
				idDict info;
				msg.ReadDeltaDict( info, &sessLocal.mapSpawnData.userInfo[clientNum] );
				if ( msg.IsReadOverflowed() ) {
					DropClient( clientNum, "#str_07138" );
					return;
				}
				SendUserInfoBroadcast( clientNum, info );
				break;
			}
			case CLIENT_RELIABLE_MESSAGE_PRINT: {
				char string[MAX_STRING_CHARS];
				msg.ReadString( string, sizeof( string ) );
				common->Printf( "%s\n", string );
				break;
			}
			case CLIENT_RELIABLE_MESSAGE_DISCONNECT: {
				DropClient( clientNum, "#str_07138" );
				break;
			}
			case CLIENT_RELIABLE_MESSAGE_PURE: {
				// we get this message once the client has successfully updated it's pure list
				ProcessReliablePure( clientNum, msg );
				break;
			}
			default: {
				// pass reliable message on to game code
				game->ServerProcessReliableMessage( clientNum, msg );
				break;
			}
		}
	}
}

/*
==================
idAsyncServer::ProcessAuthMessage
==================
*/
void idAsyncServer::ProcessAuthMessage( const idBitMsg &msg ) {
	netadr_t		client_from;
	char			client_guid[ 12 ], string[ MAX_STRING_CHARS ];
	int				i, clientId;
	authReply_t		reply;
	authReplyMsg_t	replyMsg = AUTH_REPLY_WAITING;
	idStr			replyPrintMsg;
	
	reply = (authReply_t)msg.ReadByte();
	if ( reply <= 0 || reply >= AUTH_MAXSTATES ) {
		common->DPrintf( "auth: invalid reply %d\n", reply );
		return;
	}
	clientId = msg.ReadShort( );
	msg.ReadNetadr( &client_from );
	msg.ReadString( client_guid, sizeof( client_guid ) );
	if ( reply != AUTH_OK ) {		
		replyMsg = (authReplyMsg_t)msg.ReadByte();
		if ( replyMsg <= 0 || replyMsg >= AUTH_REPLY_MAXSTATES ) {
			common->DPrintf( "auth: invalid reply msg %d\n", replyMsg );
			return;
		}
		if ( replyMsg == AUTH_REPLY_PRINT ) {
			msg.ReadString( string, MAX_STRING_CHARS );
			replyPrintMsg = string;
		}
	}

	lastAuthTime = serverTime;

	// no message parsing below
	
	for ( i = 0; i < MAX_CHALLENGES; i++ ) {
		if ( challenges[ i ].valid && !challenges[i].connected &&
			challenges[ i ].clientId == clientId ) {
			// return if something is wrong
			// break if we have found a valid auth
			if ( !strlen( challenges[ i ].guid ) ) {
				common->DPrintf( "auth: client %s has no guid yet\n", Sys_NetAdrToString( challenges[ i ].address ) );
				return;
			}
			if ( idStr::Cmp( challenges[ i ].guid, client_guid ) ) {
				common->DPrintf( "auth: client %s %s not matched, auth server says guid %s\n", Sys_NetAdrToString( challenges[ i ].address ), challenges[i].guid, client_guid );
				return;
			}
			if ( !Sys_CompareNetAdrBase( client_from, challenges[i].address ) ) {
				// let auth work when server and master don't see the same IP
				common->DPrintf( "auth: matched guid '%s' for != IPs %s and %s\n", client_guid, Sys_NetAdrToString( client_from ), Sys_NetAdrToString( challenges[i].address ) );
			}
			break;
		}
	}
	if ( i >= MAX_CHALLENGES ) {
		common->DPrintf( "auth: failed client lookup %s %s\n", Sys_NetAdrToString( client_from ), client_guid );
		return;
	}

	if ( challenges[ i ].authState != CDK_WAIT ) {
		common->DWarning( "auth: challenge for %s has authState %d instead of CDK_WAIT", Sys_NetAdrToString( challenges[ i ].address ), challenges[ i ].authState );
		return;
	}
	
	idStr::snPrintf( challenges[ i ].guid, sizeof( challenges[ i ].guid ), "%s", client_guid );
	if ( reply == AUTH_OK ) {
		challenges[ i ].authState = CDK_OK;
		common->Printf( "client %s %s is authed\n", Sys_NetAdrToString( client_from ), client_guid );
	} else {
		const char *msg;
		if ( replyMsg != AUTH_REPLY_PRINT ) {
			msg = authReplyMsg[ replyMsg ];
		} else {
			msg = replyPrintMsg.c_str();
		}
		// maybe localize it
		const char *l_msg = common->GetLanguageDict()->GetString( msg );
		common->DPrintf( "auth: client %s %s - %s %s\n", Sys_NetAdrToString( client_from ), client_guid, authReplyStr[ reply ], l_msg );
		challenges[ i ].authReply = reply;
		challenges[ i ].authReplyMsg = replyMsg;
		challenges[ i ].authReplyPrint = replyPrintMsg;
	}
}

/*
==================
idAsyncServer::AllowConnectionlessResponse

Bound the two unauthenticated replies that can amplify a spoofed UDP request.
LAN browser traffic gets enough per-source headroom to probe every legacy
server port in one pass, while the global budget also limits distributed
reflection traffic.
==================
*/
bool idAsyncServer::AllowConnectionlessResponse( const netadr_t from, bool infoResponse ) {
	if ( AsyncServer_Elapsed( serverTime, oobWindowStart ) >= OOB_RATE_WINDOW_MSEC ) {
		oobWindowStart = serverTime;
		oobInfoResponses = 0;
		oobChallengeResponses = 0;
	}

	int slot = -1;
	int oldest = 0;
	std::uint32_t oldestAge = 0;
	for ( int index = 0; index < MAX_OOB_RATE_LIMITS; ++index ) {
		if ( oobRateLimits[ index ].active &&
			Sys_CompareNetAdrBase( from, oobRateLimits[ index ].address ) ) {
			slot = index;
			break;
		}
		if ( !oobRateLimits[ index ].active ) {
			oldest = index;
			oldestAge = static_cast<std::uint32_t>( -1 );
		} else {
			const std::uint32_t age = AsyncServer_Elapsed( serverTime,
				oobRateLimits[ index ].windowStart );
			if ( oldestAge != static_cast<std::uint32_t>( -1 ) && age > oldestAge ) {
				oldest = index;
				oldestAge = age;
			}
		}
	}
	if ( slot < 0 ) {
		slot = oldest;
		memset( &oobRateLimits[ slot ], 0, sizeof( oobRateLimits[ slot ] ) );
		oobRateLimits[ slot ].active = true;
		oobRateLimits[ slot ].address = from;
		oobRateLimits[ slot ].windowStart = serverTime;
	}

	oobRateLimit_t &source = oobRateLimits[ slot ];
	if ( AsyncServer_Elapsed( serverTime, source.windowStart ) >= OOB_RATE_WINDOW_MSEC ) {
		source.windowStart = serverTime;
		source.infoResponses = 0;
		source.challengeResponses = 0;
	}

	if ( infoResponse ) {
		const int sourceLimit = Sys_IsLANAddress( from ) ? OOB_INFO_MAX_PER_SOURCE * 2 : OOB_INFO_MAX_PER_SOURCE;
		if ( source.infoResponses >= sourceLimit || oobInfoResponses >= OOB_INFO_MAX_GLOBAL ) {
			return false;
		}
		source.infoResponses++;
		oobInfoResponses++;
	} else {
		if ( source.challengeResponses >= OOB_CHALLENGE_MAX_PER_SOURCE ||
			oobChallengeResponses >= OOB_CHALLENGE_MAX_GLOBAL ) {
			return false;
		}
		source.challengeResponses++;
		oobChallengeResponses++;
	}
	return true;
}

/*
==================
idAsyncServer::ClearRconSecurityState
==================
*/
void idAsyncServer::ClearRconSecurityState( bool clearRateLimits ) {
	idCrypto::SecureZero( rcon2Challenges, sizeof( rcon2Challenges ) );
	idCrypto::SecureZero( rcon2Salt, sizeof( rcon2Salt ) );
	idCrypto::SecureZero( rcon2Verifier, sizeof( rcon2Verifier ) );
	rcon2VerifierInitialized = false;
	rcon2VerifierValid = false;
	if ( clearRateLimits ) {
		idCrypto::SecureZero( rconRateLimits, sizeof( rconRateLimits ) );
	}
}

/*
==================
idAsyncServer::RefreshRcon2Verifier

The expensive password KDF runs once per configured password, never once per
untrusted request. Changing the password invalidates every outstanding proof.
==================
*/
bool idAsyncServer::RefreshRcon2Verifier( void ) {
	if ( rcon2VerifierInitialized && !idAsyncNetwork::serverRemoteConsolePassword.IsModified() ) {
		return rcon2VerifierValid;
	}

	idCrypto::SecureZero( rcon2Challenges, sizeof( rcon2Challenges ) );
	idCrypto::SecureZero( rcon2Salt, sizeof( rcon2Salt ) );
	idCrypto::SecureZero( rcon2Verifier, sizeof( rcon2Verifier ) );
	rcon2VerifierInitialized = true;
	rcon2VerifierValid = false;
	idAsyncNetwork::serverRemoteConsolePassword.ClearModified();

	const char *password = idAsyncNetwork::serverRemoteConsolePassword.GetString();
	const size_t passwordBytes = strlen( password );
	if ( passwordBytes == 0 ) {
		return false;
	}
	if ( passwordBytes < idRcon2::MIN_PASSWORD_BYTES ) {
		common->Warning( "rcon2 is disabled: net_serverRemoteConsolePassword must contain at least %u bytes",
			static_cast<unsigned int>( idRcon2::MIN_PASSWORD_BYTES ) );
		return false;
	}
	if ( !Sys_GetSecureRandomBytes( rcon2Salt, sizeof( rcon2Salt ) ) ) {
		common->Warning( "rcon2 is disabled: OS secure random is unavailable" );
		return false;
	}
	if ( !idRcon2::DeriveVerifier( password, rcon2Salt, rcon2Verifier ) ) {
		idCrypto::SecureZero( rcon2Salt, sizeof( rcon2Salt ) );
		common->Warning( "rcon2 verifier derivation failed" );
		return false;
	}
	rcon2VerifierValid = true;
	return true;
}

/*
==================
idAsyncServer::AllowRconChallenge
==================
*/
bool idAsyncServer::AllowRconChallenge( const netadr_t from ) {
	int slot = -1;
	int oldest = 0;
	std::uint32_t oldestAge = 0;
	for ( int index = 0; index < MAX_RCON_RATE_LIMITS; ++index ) {
		if ( rconRateLimits[ index ].active &&
			Sys_CompareNetAdrBase( from, rconRateLimits[ index ].address ) ) {
			slot = index;
			break;
		}
		if ( !rconRateLimits[ index ].active ) {
			oldest = index;
			oldestAge = static_cast<std::uint32_t>( -1 );
		} else {
			const std::uint32_t age = AsyncServer_Elapsed( serverTime,
				rconRateLimits[ index ].challengeWindowStart );
			if ( oldestAge != static_cast<std::uint32_t>( -1 ) && age > oldestAge ) {
				oldest = index;
				oldestAge = age;
			}
		}
	}
	if ( slot < 0 ) {
		slot = oldest;
		memset( &rconRateLimits[ slot ], 0, sizeof( rconRateLimits[ slot ] ) );
		rconRateLimits[ slot ].active = true;
		rconRateLimits[ slot ].address = from;
		rconRateLimits[ slot ].challengeWindowStart = serverTime;
		rconRateLimits[ slot ].failureWindowStart = serverTime;
	}

	rconRateLimit_t &limit = rconRateLimits[ slot ];
	if ( limit.blockedUntil != 0 ) {
		if ( AsyncServer_TimeBefore( serverTime, limit.blockedUntil ) ) {
			return false;
		}
		limit.blockedUntil = 0;
	}
	if ( limit.challengeCount > 0 &&
		AsyncServer_Elapsed( serverTime, limit.lastChallengeTime ) < RCON2_RESEND_MIN_MSEC ) {
		return false;
	}
	if ( AsyncServer_Elapsed( serverTime, limit.challengeWindowStart ) >= RCON2_RATE_WINDOW_MSEC ) {
		limit.challengeWindowStart = serverTime;
		limit.challengeCount = 0;
	}
	if ( limit.challengeCount >= RCON2_RATE_MAX_CHALLENGES ) {
		return false;
	}
	limit.lastChallengeTime = serverTime;
	limit.challengeCount++;
	return true;
}

bool idAsyncServer::AllowRconProofAttempt( const netadr_t from ) {
	for ( int index = 0; index < MAX_RCON_RATE_LIMITS; ++index ) {
		if ( rconRateLimits[ index ].active &&
			Sys_CompareNetAdrBase( from, rconRateLimits[ index ].address ) ) {
			rconRateLimit_t &limit = rconRateLimits[ index ];
			if ( limit.blockedUntil == 0 ) {
				return true;
			}
			if ( AsyncServer_TimeBefore( serverTime, limit.blockedUntil ) ) {
				return false;
			}
			limit.blockedUntil = 0;
			return true;
		}
	}
	return true;
}

void idAsyncServer::RecordRconFailure( const netadr_t from ) {
	// Ensure a rate slot exists without allowing the failure to bypass a full
	// table. A proof can only reach this point after a challenge used this path.
	for ( int index = 0; index < MAX_RCON_RATE_LIMITS; ++index ) {
		rconRateLimit_t &limit = rconRateLimits[ index ];
		if ( !limit.active || !Sys_CompareNetAdrBase( from, limit.address ) ) {
			continue;
		}
		if ( AsyncServer_Elapsed( serverTime, limit.failureWindowStart ) >= RCON2_FAILURE_WINDOW_MSEC ) {
			limit.failureWindowStart = serverTime;
			limit.failureCount = 0;
		}
		limit.failureCount++;
		if ( limit.failureCount >= RCON2_FAILURE_MAX_ATTEMPTS ) {
			limit.blockedUntil = static_cast<int>( static_cast<std::uint32_t>( serverTime ) +
				static_cast<std::uint32_t>( RCON2_FAILURE_BLOCK_MSEC ) );
			for ( int challengeIndex = 0; challengeIndex < MAX_RCON2_CHALLENGES; ++challengeIndex ) {
				if ( rcon2Challenges[ challengeIndex ].active &&
					Sys_CompareNetAdrBase( from, rcon2Challenges[ challengeIndex ].address ) ) {
					idCrypto::SecureZero( &rcon2Challenges[ challengeIndex ],
						sizeof( rcon2Challenges[ challengeIndex ] ) );
				}
			}
		}
		return;
	}
}

/*
==================
idAsyncServer::ProcessChallengeMessage
==================
*/
void idAsyncServer::ProcessChallengeMessage( const netadr_t from, const idBitMsg &msg ) {
	int			i, clientId, oldest;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( msg.GetRemainingData() != 4 ) {
		return;
	}
	clientId = msg.ReadLong();

	oldest = 0;
	bool foundFree = false;
	std::uint32_t oldestAge = 0;

	// see if we already have a challenge for this ip
	for ( i = 0; i < MAX_CHALLENGES; i++ ) {
		if ( challenges[ i ].valid &&
			AsyncServer_Elapsed( serverTime, challenges[ i ].time ) > CONNECTION_CHALLENGE_TIMEOUT_MSEC ) {
			AsyncServer_ClearChallenge( challenges[ i ] );
		}
		if ( challenges[i].valid && !challenges[i].connected &&
			AsyncServer_SameEndpoint( from, challenges[i].address ) &&
			clientId == challenges[i].clientId ) {
			break;
		}
		if ( !challenges[ i ].valid ) {
			if ( !foundFree ) {
				oldest = i;
				foundFree = true;
			}
		} else if ( !foundFree ) {
			const std::uint32_t age = AsyncServer_Elapsed( serverTime, challenges[ i ].time );
			if ( age > oldestAge ) {
				oldestAge = age;
				oldest = i;
			}
		}
	}

	if ( !AllowConnectionlessResponse( from, false ) ) {
		return;
	}

	if ( i >= MAX_CHALLENGES ) {
		// this is the first time this client has asked for a challenge
		i = oldest;
		std::uint32_t secureChallenge = 0;
		if ( !Sys_GetSecureRandomBytes( &secureChallenge, sizeof( secureChallenge ) ) ) {
			common->Warning( "OS secure random unavailable; connection challenge not issued" );
			return;
		}
		if ( secureChallenge == 0 ) {
			secureChallenge = 1;
		}
		AsyncServer_ClearChallenge( challenges[ i ] );
		challenges[i].valid = true;
		challenges[i].address = from;
		challenges[i].clientId = clientId;
		challenges[i].challenge = static_cast<int>( secureChallenge );
		challenges[i].time = serverTime;
		challenges[i].pingTime = serverTime;
		challenges[i].connected = false;
		challenges[i].authState = CDK_WAIT;
		challenges[i].authReply = AUTH_NONE;
		challenges[i].authReplyMsg = AUTH_REPLY_WAITING;
		challenges[i].authReplyPrint = "";
		challenges[i].guid[0] = '\0';
	}
	common->DPrintf( "sending connection challenge to %s\n", Sys_NetAdrToString( from ) );

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "challengeResponse" );
	outMsg.WriteLong( challenges[i].challenge );
	outMsg.WriteShort( serverId );
	outMsg.WriteString( cvarSystem->GetCVarString( "fs_game_base" ) );
	outMsg.WriteString( cvarSystem->GetCVarString( "fs_game" ) );

	serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );

	if ( Sys_IsLANAddress( from ) ) {
		challenges[i].authState = CDK_OK;
	} else {
		if ( idAsyncNetwork::LANServer.GetBool() ) {
			common->Printf( "net_LANServer is enabled. Client %s is not a LAN address, will be rejected\n", Sys_NetAdrToString( from ) );
			challenges[ i ].authState = CDK_ONLYLAN;
		} else {
			challenges[ i ].authState = CDK_OK;
		}
	}
}

/*
==================
idAsyncServer::SendPureServerMessage
==================
*/
bool idAsyncServer::SendPureServerMessage( const netadr_t to, int OS ) {
	idBitMsg	outMsg;
	byte		msgBuf[ MAX_MESSAGE_SIZE ];
	int			serverChecksums[ MAX_PURE_PAKS ];
	int			gamePakChecksum;
	int			i;

	fileSystem->GetPureServerChecksums( serverChecksums, OS, &gamePakChecksum );
	if ( !serverChecksums[ 0 ] || !gamePakChecksum ) {
		// happens if you run fully expanded assets with si_pure 1
		common->Warning( "pure server has no referenced pak files or compatible game module" );
		return false;
	}
	common->DPrintf( "client %s: sending pure pak list\n", Sys_NetAdrToString( to ) );

	// send our list of required paks
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "pureServer" );

	i = 0;
	while ( serverChecksums[ i ] ) {
		outMsg.WriteLong( serverChecksums[ i++ ] );
	}
	outMsg.WriteLong( 0 );

	// write the pak checksum for game code
	outMsg.WriteLong( gamePakChecksum );

	serverPort.SendPacket( to, outMsg.GetData(), outMsg.GetSize() );
	return true;
}

/*
==================
idAsyncServer::SendReliablePureToClient
==================
*/
bool idAsyncServer::SendReliablePureToClient( int clientNum ) {
	idBitMsg	msg;
	byte		msgBuf[ MAX_MESSAGE_SIZE ];
	int			serverChecksums[ MAX_PURE_PAKS ];
	int			i;
	int			gamePakChecksum;

	fileSystem->GetPureServerChecksums( serverChecksums, clients[ clientNum ].OS, &gamePakChecksum );
	if ( !serverChecksums[ 0 ] || !gamePakChecksum ) {
		// happens if you run fully expanded assets with si_pure 1
		common->Warning( "pure server has no referenced pak files or compatible game module" );
		return false;
	}

	common->DPrintf( "client %d: sending pure pak list (reliable channel) @ gameInitId %d\n", clientNum, gameInitId );

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_PURE );

	msg.WriteLong( gameInitId );

	i = 0;
	while ( serverChecksums[ i ] ) {
		msg.WriteLong( serverChecksums[ i++ ] );
	}
	msg.WriteLong( 0 );
	msg.WriteLong( gamePakChecksum );

	SendReliableMessage( clientNum, msg );

	return true;
}

/*
==================
idAsyncServer::ValidateChallenge
==================
*/
int idAsyncServer::ValidateChallenge( const netadr_t from, int challenge, int clientId ) {
	int i;
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		const serverClient_t &client = clients[i];

		if ( client.clientState == SCS_FREE ) {
			continue;
		}
		if ( Sys_CompareNetAdrBase( from, client.channel.GetRemoteAddress() ) &&
					( clientId == client.clientId || from.port == client.channel.GetRemoteAddress().port ) ) {
			if ( AsyncServer_Elapsed( serverTime, client.lastConnectTime ) < MIN_RECONNECT_TIME ) {
				common->Printf( "%s: reconnect rejected : too soon\n", Sys_NetAdrToString( from ) );
				return -1;
			}
			break;
		}
	}

	for ( i = 0; i < MAX_CHALLENGES; i++ ) {
		if ( challenges[ i ].valid &&
			AsyncServer_Elapsed( serverTime, challenges[ i ].time ) > CONNECTION_CHALLENGE_TIMEOUT_MSEC ) {
			AsyncServer_ClearChallenge( challenges[ i ] );
			continue;
		}
		if ( challenges[ i ].valid && !challenges[ i ].connected &&
			AsyncServer_Elapsed( serverTime, challenges[ i ].time ) <= CONNECTION_CHALLENGE_TIMEOUT_MSEC &&
			AsyncServer_SameEndpoint( from, challenges[i].address ) &&
			clientId == challenges[ i ].clientId ) {
			if ( challenge == challenges[i].challenge ) {
				break;
			}
		}
	}
	if ( i == MAX_CHALLENGES ) {
		if ( AllowConnectionlessResponse( from, false ) ) {
			PrintOOB( from, SERVER_PRINT_BADCHALLENGE, "#str_04840" );
		}
		return -1;
	}
	return i;
}

/*
==================
idAsyncServer::ProcessConnectMessage
==================
*/
void idAsyncServer::ProcessConnectMessage( const netadr_t from, const idBitMsg &msg ) {
	int			clientNum, protocol, clientDataChecksum, challenge, clientId, ping, clientRate;
	idBitMsg	outMsg;
	byte		msgBuf[ MAX_MESSAGE_SIZE ];
	char		guid[ 12 ];
	char		password[ 17 ];
	int			i, ichallenge, islot, OS, numClients;

	// Parse the complete fixed header before deciding whether to reply. All
	// response paths below require the endpoint-bound challenge first, which
	// prevents malformed or spoofed connect packets from becoming reflectors.
	const int fixedHeaderBytes = 4 + 2 + 4 + 4 + 2 + 4;
	if ( msg.GetRemainingData() < fixedHeaderBytes ) {
		return;
	}
	protocol = msg.ReadLong();
	OS = msg.ReadShort();
	clientDataChecksum = msg.ReadLong();
	challenge = msg.ReadLong();
	clientId = msg.ReadShort();
	clientRate = msg.ReadLong();
	if ( OS < 0 || OS >= MAX_GAME_OS ) {
		common->DPrintf( "connect from %s rejected: invalid OS %d\n", Sys_NetAdrToString( from ), OS );
		return;
	}
	if ( sessLocal.mapSpawnData.serverInfo.GetInt( "si_pure" ) ) {
		const int osMask = fileSystem->GetOSMask();
		if ( osMask < 0 || ( static_cast<unsigned int>( osMask ) & ( 1u << OS ) ) == 0 ) {
			common->DPrintf( "connect from %s rejected: unsupported pure OS %d\n", Sys_NetAdrToString( from ), OS );
			return;
		}
	}

	if ( ( ichallenge = ValidateChallenge( from, challenge, clientId ) ) == -1 ) {
		return;
	}
	if ( !AllowConnectionlessResponse( from, false ) ) {
		return;
	}

	// check the protocol version only after the request proves possession of
	// the challenge issued to this exact endpoint.
	if ( protocol != ASYNC_PROTOCOL_VERSION ) {
		PrintOOB( from, SERVER_PRINT_BADPROTOCOL, va( "server uses protocol %d.%d\n", ASYNC_PROTOCOL_MAJOR, ASYNC_PROTOCOL_MINOR ) );
		return;
	}

	// check the client data - only for non pure servers
	if ( !sessLocal.mapSpawnData.serverInfo.GetInt( "si_pure" ) && clientDataChecksum != serverDataChecksum ) {
		common->DPrintf( "Decl checksum mismatch from %s: client=0x%08x server=0x%08x (non-pure)\n",
			Sys_NetAdrToString( from ), static_cast<unsigned int>( clientDataChecksum ),
			static_cast<unsigned int>( serverDataChecksum ) );
		PrintOOB( from, SERVER_PRINT_MISC, "#str_04842" );
		return;
	}

	msg.ReadString( guid, sizeof( guid ) );
	if ( msg.IsReadOverflowed() ) {
		common->DPrintf( "connect from %s rejected: truncated GUID\n", Sys_NetAdrToString( from ) );
		return;
	}
	challenges[ ichallenge ].OS = OS;

	switch ( challenges[ ichallenge ].authState ) {
		case CDK_PUREWAIT:
			if ( !SendPureServerMessage( from, OS ) ) {
				common->DPrintf( "client %s: pure challenge resend failed; rejecting connection\n",
					Sys_NetAdrToString( from ) );
				PrintOOB( from, SERVER_PRINT_MISC, "#str_04337" );
				AsyncServer_ClearChallenge( challenges[ ichallenge ] );
			}
			return;
		case CDK_ONLYLAN:
			common->DPrintf( "%s: not a lan client\n", Sys_NetAdrToString( from ) );
			PrintOOB( from, SERVER_PRINT_MISC, "#str_04843" );
			return;
		default:
			break;
	}

	numClients = 0;
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[ i ];
		if ( client.clientState >= SCS_PUREWAIT ) {
			numClients++;
		}
	}

	// game may be passworded, client banned by IP or GUID
	// if authState == CDK_PUREOK, the check was already performed once before entering pure checks
	// but meanwhile, the max players may have been reached
	msg.ReadString( password, sizeof( password ) );
	if ( msg.IsReadOverflowed() ) {
		idCrypto::SecureZero( password, sizeof( password ) );
		common->DPrintf( "connect from %s rejected: truncated password\n", Sys_NetAdrToString( from ) );
		return;
	}
	char reason[MAX_STRING_CHARS];
	allowReply_t reply = game->ServerAllowClient(clientId, numClients, Sys_NetAdrToString( from ), guid, password, password, reason );
	idCrypto::SecureZero( password, sizeof( password ) );
	if ( reply != ALLOW_YES ) {
		common->DPrintf( "game denied connection for %s\n", Sys_NetAdrToString( from ) );

		// SERVER_PRINT_GAMEDENY passes the game opcode through. Don't use PrintOOB
		outMsg.Init( msgBuf, sizeof( msgBuf ) );
		outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
		outMsg.WriteString( "print" );
		outMsg.WriteLong( SERVER_PRINT_GAMEDENY );
		outMsg.WriteLong( reply );
		outMsg.WriteString( reason );
		serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );

		return;
	}

	// enter pure checks if necessary
	if ( sessLocal.mapSpawnData.serverInfo.GetInt( "si_pure" ) && challenges[ ichallenge ].authState != CDK_PUREOK ) {
		if ( !SendPureServerMessage( from, OS ) ) {
			common->DPrintf( "client %s: pure challenge could not be issued; rejecting connection\n",
				Sys_NetAdrToString( from ) );
			PrintOOB( from, SERVER_PRINT_MISC, "#str_04337" );
			AsyncServer_ClearChallenge( challenges[ ichallenge ] );
			return;
		}
		challenges[ ichallenge ].authState = CDK_PUREWAIT;
		return;
	}

	// push back decl checksum here when running pure. just an additional safe check
	if ( sessLocal.mapSpawnData.serverInfo.GetInt( "si_pure" ) && clientDataChecksum != serverDataChecksum ) {
		common->DPrintf( "Decl checksum mismatch from %s: client=0x%08x server=0x%08x (pure)\n",
			Sys_NetAdrToString( from ), static_cast<unsigned int>( clientDataChecksum ),
			static_cast<unsigned int>( serverDataChecksum ) );
		PrintOOB( from, SERVER_PRINT_MISC, "#str_04844" );
		return;
	}

	const std::uint32_t pingElapsed = AsyncServer_Elapsed( serverTime, challenges[ ichallenge ].pingTime );
	ping = pingElapsed > static_cast<std::uint32_t>( idMath::INT_MAX ) ? idMath::INT_MAX :
		static_cast<int>( pingElapsed );
	common->Printf( "challenge from %s connecting with %d ping\n", Sys_NetAdrToString( from ), ping );
	challenges[ ichallenge ].connected = true;

	// find a slot for the client
	for ( islot = 0; islot < 3; islot++ ) {
		for ( clientNum = 0; clientNum < MAX_ASYNC_CLIENTS; clientNum++ ) {
			serverClient_t &client = clients[ clientNum ];

			if ( islot == 0 ) {
				// if this slot uses the same IP and port
				if ( Sys_CompareNetAdrBase( from, client.channel.GetRemoteAddress() ) &&
						( clientId == client.clientId || from.port == client.channel.GetRemoteAddress().port ) ) {
					break;
				}
			} else if ( islot == 1 ) {
				// if this client is not connected and the slot uses the same IP
				if ( client.clientState >= SCS_PUREWAIT ) {
					continue;
				}
				if ( Sys_CompareNetAdrBase( from, client.channel.GetRemoteAddress() ) ) {
					break;
				}
			} else if ( islot == 2 ) {
				// if this slot is free
				if ( client.clientState == SCS_FREE ) {
					break;
				}
			}
		}

		if ( clientNum < MAX_ASYNC_CLIENTS ) {
			// initialize
			clients[ clientNum ].channel.Init( from, serverId );
			clients[ clientNum ].OS = OS;
			idStr::Copynz( clients[ clientNum ].guid, guid, sizeof( clients[ clientNum ].guid ) );
			break;
		}
	}

	// if no free spots available
	if ( clientNum >= MAX_ASYNC_CLIENTS ) {
		PrintOOB( from, SERVER_PRINT_MISC, "#str_04845" );
		return;
	}

	common->Printf( "sending connect response to %s\n", Sys_NetAdrToString( from ) );

	// send connect response message
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "connectResponse" );
	outMsg.WriteLong( clientNum );
	outMsg.WriteLong( gameInitId );
	outMsg.WriteLong( gameFrame );
	outMsg.WriteLong( gameTime );
	outMsg.WriteDeltaDict( sessLocal.mapSpawnData.serverInfo, NULL );

	serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
	
	InitClient( clientNum, clientId, clientRate );

	clients[clientNum].gameInitSequence = 1;
	clients[clientNum].snapshotSequence = 1;

	// clear the challenge struct so a reconnect from this client IP starts clean
	AsyncServer_ClearChallenge( challenges[ ichallenge ] );
}

/*
==================
idAsyncServer::VerifyChecksumMessage
==================
*/
bool idAsyncServer::VerifyChecksumMessage( int clientNum, const netadr_t *from, const idBitMsg &msg, idStr &reply, int OS ) {
	int		i, numChecksums;
	int		checksums[ MAX_PURE_PAKS ];
	int		gamePakChecksum;
	int		serverChecksums[ MAX_PURE_PAKS ];
	int		serverGamePakChecksum;

	// pak checksums, in a 0-terminated list
	numChecksums = 0;
	do {
		i = msg.ReadLong( );
		checksums[ numChecksums++ ] = i;
		// just to make sure a broken client doesn't crash us
		if ( numChecksums >= MAX_PURE_PAKS ) {
			common->Warning( "MAX_PURE_PAKS ( %d ) exceeded in idAsyncServer::ProcessPureMessage\n", MAX_PURE_PAKS );
			reply = "#str_07144";
			return false;
		}
	} while ( i );
	numChecksums--;

	// code pak checksum
	gamePakChecksum = msg.ReadLong( );

	fileSystem->GetPureServerChecksums( serverChecksums, OS, &serverGamePakChecksum );
	assert( serverChecksums[ 0 ] );

	// compare the lists
	if ( serverGamePakChecksum != gamePakChecksum ) {
		common->Printf( "client %s: invalid game code pak ( 0x%x )\n", from ? Sys_NetAdrToString( *from ) : va( "%d", clientNum ), gamePakChecksum );
		reply = "#str_07145";
		return false;
	}
	for ( i = 0; serverChecksums[ i ] != 0; i++ ) {
		if ( checksums[ i ] != serverChecksums[ i ] ) {
			common->DPrintf( "client %s: pak missing ( 0x%x )\n", from ? Sys_NetAdrToString( *from ) : va( "%d", clientNum ), serverChecksums[ i ] );
			reply = va( "pak missing ( 0x%x )\n", serverChecksums[ i ] );
			return false;
		}
	}
	if ( checksums[ i ] != 0 ) {
		common->DPrintf( "client %s: extra pak file referenced ( 0x%x )\n", from ? Sys_NetAdrToString( *from ) : va( "%d", clientNum ), checksums[ i ] );
		reply = va( "extra pak file referenced ( 0x%x )\n", checksums[ i ] );
		return false;
	}
	return true;
}

/*
==================
idAsyncServer::ProcessPureMessage
==================
*/
void idAsyncServer::ProcessPureMessage( const netadr_t from, const idBitMsg &msg ) {
	int		iclient, challenge, clientId;
	idStr	reply;

	challenge = msg.ReadLong();
	clientId = msg.ReadShort();

	if ( ( iclient = ValidateChallenge( from, challenge, clientId ) ) == -1 ) {
		return;
	}

	if ( challenges[ iclient ].authState != CDK_PUREWAIT ) {
		common->DPrintf( "client %s: got pure message, not in CDK_PUREWAIT\n", Sys_NetAdrToString( from ) );
		return;
	}

	if ( !VerifyChecksumMessage( iclient, &from, msg, reply, challenges[ iclient ].OS ) ) {
		if ( AllowConnectionlessResponse( from, false ) ) {
			PrintOOB( from, SERVER_PRINT_MISC, reply );
		}
		return;
	}

	common->DPrintf( "client %s: passed pure checks\n", Sys_NetAdrToString( from ) );
	challenges[ iclient ].authState = CDK_PUREOK; // next connect message will get the client through completely
}

/*
==================
idAsyncServer::ProcessReliablePure
==================
*/
void idAsyncServer::ProcessReliablePure( int clientNum, const idBitMsg &msg ) {
	idStr		reply;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	int			clientGameInitId;
	
	clientGameInitId = msg.ReadLong();
	if ( clientGameInitId != gameInitId ) {
		common->DPrintf( "client %d: ignoring reliable pure from an old gameInit (%d)\n", clientNum, clientGameInitId );
		return;
	}

	if ( clients[ clientNum ].clientState != SCS_PUREWAIT ) {
		// should not happen unless something is very wrong. still, don't let this crash us, just get rid of the client
		common->DPrintf( "client %d: got reliable pure while != SCS_PUREWAIT, sending a reload\n", clientNum );
		outMsg.Init( msgBuf, sizeof( msgBuf ) );
		outMsg.WriteByte( SERVER_RELIABLE_MESSAGE_RELOAD );
		SendReliableMessage( clientNum, outMsg );
		// go back to SCS_CONNECTED to sleep on the client until it goes away for a reconnect
		clients[ clientNum ].clientState = SCS_CONNECTED;
		return;
	}

	if ( !VerifyChecksumMessage( clientNum, NULL, msg, reply, clients[ clientNum ].OS ) ) {
		DropClient( clientNum, reply );
		return;
	}
	common->DPrintf( "client %d: passed pure checks (reliable channel)\n", clientNum );
	clients[ clientNum ].clientState = SCS_CONNECTED;
}

/*
==================
idAsyncServer::RemoteConsoleOutput
==================
*/
void idAsyncServer::RemoteConsoleOutput( const char *string ) {
	noRconOutput = false;
	PrintOOB( rconAddress, SERVER_PRINT_RCON, string );
}

/*
==================
RConRedirect
==================
*/
void RConRedirect( const char *string ) {
	idAsyncNetwork::server.RemoteConsoleOutput( string );
}

/*
==================
idAsyncServer::ExecuteRemoteConsoleCommand
==================
*/
void idAsyncServer::ExecuteRemoteConsoleCommand( const netadr_t from, const char *command, bool authenticated ) {
	byte		msgBuf[952];
	common->Printf( "%s remote console command accepted from %s\n",
		authenticated ? "authenticated" : "legacy plaintext", Sys_NetAdrToString( from ) );

	rconAddress = from;
	noRconOutput = true;
	common->BeginRedirect( (char *)msgBuf, sizeof( msgBuf ), RConRedirect );
	cmdSystem->BufferCommandText( CMD_EXEC_NOW, command );
	common->EndRedirect();

	if ( noRconOutput ) {
		PrintOOB( rconAddress, SERVER_PRINT_RCON, "#str_04848" );
	}
}

/*
==================
idAsyncServer::SendRemoteConsole2Complete
==================
*/
void idAsyncServer::SendRemoteConsole2Complete( const netadr_t to,
		const byte clientNonce[16], const byte serverNonce[16] ) {
	byte msgBuf[128];
	idBitMsg outMsg;
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "rcon2Complete" );
	outMsg.WriteByte( idRcon2::PROTOCOL_VERSION );
	outMsg.WriteData( clientNonce, idRcon2::NONCE_BYTES );
	outMsg.WriteData( serverNonce, idRcon2::NONCE_BYTES );
	serverPort.SendPacket( to, outMsg.GetData(), outMsg.GetSize() );
}

/*
==================
idAsyncServer::ProcessRemoteConsoleMessage

Quake 4's original packet sends the password verbatim. It remains available
only as an explicit two-sided compatibility escape hatch and is still bounded
and constant-time checked.
==================
*/
void idAsyncServer::ProcessRemoteConsoleMessage( const netadr_t from, const idBitMsg &msg ) {
	char suppliedPassword[MAX_STRING_CHARS] = {};
	char command[MAX_STRING_CHARS] = {};
	if ( !idAsyncNetwork::serverAllowLegacyRcon.GetBool() ||
		idAsyncNetwork::serverRemoteConsolePassword.GetString()[0] == '\0' ) {
		return;
	}
	if ( !AllowRconChallenge( from ) || !AllowConnectionlessResponse( from, false ) ) {
		return;
	}
	msg.ReadString( suppliedPassword, sizeof( suppliedPassword ) );
	msg.ReadString( command, sizeof( command ) );
	if ( msg.GetRemainingData() != 0 || command[0] == '\0' ) {
		idCrypto::SecureZero( suppliedPassword, sizeof( suppliedPassword ) );
		idCrypto::SecureZero( command, sizeof( command ) );
		return;
	}

	const char *configuredPassword = idAsyncNetwork::serverRemoteConsolePassword.GetString();
	const size_t suppliedBytes = strlen( suppliedPassword );
	const size_t configuredBytes = strlen( configuredPassword );
	const bool passwordMatches = suppliedBytes == configuredBytes &&
		idCrypto::ConstantTimeEquals( suppliedPassword, configuredPassword, configuredBytes );
	if ( !passwordMatches ) {
		RecordRconFailure( from );
		PrintOOB( from, SERVER_PRINT_MISC, "#str_04847" );
		idCrypto::SecureZero( suppliedPassword, sizeof( suppliedPassword ) );
		idCrypto::SecureZero( command, sizeof( command ) );
		return;
	}

	idCrypto::SecureZero( suppliedPassword, sizeof( suppliedPassword ) );
	ExecuteRemoteConsoleCommand( from, command, false );
	idCrypto::SecureZero( command, sizeof( command ) );
}

/*
==================
idAsyncServer::ProcessRemoteConsole2ChallengeMessage
==================
*/
void idAsyncServer::ProcessRemoteConsole2ChallengeMessage( const netadr_t from, const idBitMsg &msg ) {
	const int requestBytes = 1 + idRcon2::NONCE_BYTES + idRcon2::REQUEST_DIGEST_BYTES;
	if ( !active || msg.GetRemainingData() != requestBytes || !RefreshRcon2Verifier() ) {
		return;
	}
	if ( msg.ReadByte() != idRcon2::PROTOCOL_VERSION ) {
		return;
	}
	byte clientNonce[idRcon2::NONCE_BYTES];
	byte requestDigest[idRcon2::REQUEST_DIGEST_BYTES];
	msg.ReadData( clientNonce, sizeof( clientNonce ) );
	msg.ReadData( requestDigest, sizeof( requestDigest ) );

	int slot = -1;
	int oldest = 0;
	std::uint32_t oldestAge = 0;
	for ( int index = 0; index < MAX_RCON2_CHALLENGES; ++index ) {
		rcon2Challenge_t &candidate = rcon2Challenges[ index ];
		if ( candidate.active &&
			AsyncServer_Elapsed( serverTime, candidate.createdTime ) > RCON2_CHALLENGE_TIMEOUT_MSEC ) {
			idCrypto::SecureZero( &candidate, sizeof( candidate ) );
		}
		if ( candidate.active && AsyncServer_SameEndpoint( from, candidate.address ) &&
			idCrypto::ConstantTimeEquals( clientNonce, candidate.clientNonce, sizeof( clientNonce ) ) &&
			idCrypto::ConstantTimeEquals( requestDigest, candidate.requestDigest, sizeof( requestDigest ) ) ) {
			slot = index;
			break;
		}
		if ( !candidate.active ) {
			oldest = index;
			oldestAge = static_cast<std::uint32_t>( -1 );
		} else {
			const std::uint32_t age = AsyncServer_Elapsed( serverTime, candidate.createdTime );
			if ( oldestAge != static_cast<std::uint32_t>( -1 ) && age > oldestAge ) {
				oldest = index;
				oldestAge = age;
			}
		}
	}
	if ( !AllowRconChallenge( from ) || !AllowConnectionlessResponse( from, false ) ) {
		idCrypto::SecureZero( clientNonce, sizeof( clientNonce ) );
		idCrypto::SecureZero( requestDigest, sizeof( requestDigest ) );
		return;
	}
	if ( slot < 0 ) {
		slot = oldest;
		rcon2Challenge_t &issued = rcon2Challenges[ slot ];
		idCrypto::SecureZero( &issued, sizeof( issued ) );
		byte randomValues[idRcon2::NONCE_BYTES + idRcon2::ENDPOINT_BINDING_BYTES];
		if ( !Sys_GetSecureRandomBytes( randomValues, sizeof( randomValues ) ) ) {
			common->Warning( "OS secure random unavailable; rcon2 challenge not issued" );
			idCrypto::SecureZero( clientNonce, sizeof( clientNonce ) );
			idCrypto::SecureZero( requestDigest, sizeof( requestDigest ) );
			return;
		}
		issued.active = true;
		issued.address = from;
		issued.createdTime = serverTime;
		memcpy( issued.clientNonce, clientNonce, sizeof( issued.clientNonce ) );
		memcpy( issued.serverNonce, randomValues, sizeof( issued.serverNonce ) );
		memcpy( issued.endpointBinding, randomValues + sizeof( issued.serverNonce ),
			sizeof( issued.endpointBinding ) );
		memcpy( issued.requestDigest, requestDigest, sizeof( issued.requestDigest ) );
		idCrypto::SecureZero( randomValues, sizeof( randomValues ) );
	}

	rcon2Challenge_t &issued = rcon2Challenges[ slot ];
	issued.lastResponseTime = serverTime;
	byte msgBuf[192];
	idBitMsg outMsg;
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "rcon2ChallengeResponse" );
	outMsg.WriteByte( idRcon2::PROTOCOL_VERSION );
	outMsg.WriteData( issued.clientNonce, sizeof( issued.clientNonce ) );
	outMsg.WriteData( issued.serverNonce, sizeof( issued.serverNonce ) );
	outMsg.WriteData( rcon2Salt, sizeof( rcon2Salt ) );
	outMsg.WriteLong( static_cast<int>( idRcon2::PBKDF2_ITERATIONS ) );
	outMsg.WriteData( issued.endpointBinding, sizeof( issued.endpointBinding ) );
	outMsg.WriteData( issued.requestDigest, sizeof( issued.requestDigest ) );
	serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
	idCrypto::SecureZero( clientNonce, sizeof( clientNonce ) );
	idCrypto::SecureZero( requestDigest, sizeof( requestDigest ) );
}

/*
==================
idAsyncServer::ProcessRemoteConsole2Message
==================
*/
void idAsyncServer::ProcessRemoteConsole2Message( const netadr_t from, const idBitMsg &msg ) {
	const int fixedPrefixBytes = 1 + idRcon2::NONCE_BYTES + idRcon2::NONCE_BYTES;
	if ( !active || msg.GetRemainingData() < fixedPrefixBytes + 1 + idRcon2::PROOF_BYTES ||
		!RefreshRcon2Verifier() || !AllowRconProofAttempt( from ) ) {
		return;
	}
	if ( msg.ReadByte() != idRcon2::PROTOCOL_VERSION ) {
		return;
	}
	byte clientNonce[idRcon2::NONCE_BYTES];
	byte serverNonce[idRcon2::NONCE_BYTES];
	msg.ReadData( clientNonce, sizeof( clientNonce ) );
	msg.ReadData( serverNonce, sizeof( serverNonce ) );

	int slot = -1;
	for ( int index = 0; index < MAX_RCON2_CHALLENGES; ++index ) {
		const rcon2Challenge_t &candidate = rcon2Challenges[ index ];
		if ( candidate.active &&
			AsyncServer_Elapsed( serverTime, candidate.createdTime ) <= RCON2_CHALLENGE_TIMEOUT_MSEC &&
			AsyncServer_SameEndpoint( from, candidate.address ) &&
			idCrypto::ConstantTimeEquals( clientNonce, candidate.clientNonce, sizeof( clientNonce ) ) &&
			idCrypto::ConstantTimeEquals( serverNonce, candidate.serverNonce, sizeof( serverNonce ) ) ) {
			slot = index;
			break;
		}
	}
	if ( slot < 0 ) {
		idCrypto::SecureZero( clientNonce, sizeof( clientNonce ) );
		idCrypto::SecureZero( serverNonce, sizeof( serverNonce ) );
		return;
	}

	rcon2Challenge_t issued = rcon2Challenges[ slot ];
	// Consume before parsing or checking the proof: malformed, failed, and
	// successful attempts are all one-shot and cannot be replayed.
	idCrypto::SecureZero( &rcon2Challenges[ slot ], sizeof( rcon2Challenges[ slot ] ) );
	char command[MAX_STRING_CHARS] = {};
	msg.ReadString( command, sizeof( command ) );
	if ( command[0] == '\0' || msg.GetRemainingData() != idRcon2::PROOF_BYTES ) {
		RecordRconFailure( from );
		idCrypto::SecureZero( command, sizeof( command ) );
		idCrypto::SecureZero( &issued, sizeof( issued ) );
		idCrypto::SecureZero( clientNonce, sizeof( clientNonce ) );
		idCrypto::SecureZero( serverNonce, sizeof( serverNonce ) );
		return;
	}
	byte suppliedProof[idRcon2::PROOF_BYTES];
	byte expectedProof[idRcon2::PROOF_BYTES];
	byte requestDigest[idRcon2::REQUEST_DIGEST_BYTES];
	msg.ReadData( suppliedProof, sizeof( suppliedProof ) );
	idRcon2::HashRequest( command, requestDigest );
	idRcon2::ComputeProof( rcon2Verifier, issued.clientNonce, issued.serverNonce,
		issued.endpointBinding, issued.requestDigest, expectedProof );
	const bool requestMatches = idCrypto::ConstantTimeEquals( requestDigest,
		issued.requestDigest, sizeof( requestDigest ) );
	const bool proofMatches = idCrypto::ConstantTimeEquals( suppliedProof,
		expectedProof, sizeof( suppliedProof ) );

	if ( !requestMatches || !proofMatches ) {
		RecordRconFailure( from );
		PrintOOB( from, SERVER_PRINT_MISC, "#str_04847" );
	} else {
		for ( int index = 0; index < MAX_RCON_RATE_LIMITS; ++index ) {
			if ( rconRateLimits[ index ].active &&
				Sys_CompareNetAdrBase( from, rconRateLimits[ index ].address ) ) {
				rconRateLimits[ index ].failureCount = 0;
				rconRateLimits[ index ].failureWindowStart = serverTime;
				break;
			}
		}
		ExecuteRemoteConsoleCommand( from, command, true );
	}
	SendRemoteConsole2Complete( from, issued.clientNonce, issued.serverNonce );

	idCrypto::SecureZero( command, sizeof( command ) );
	idCrypto::SecureZero( suppliedProof, sizeof( suppliedProof ) );
	idCrypto::SecureZero( expectedProof, sizeof( expectedProof ) );
	idCrypto::SecureZero( requestDigest, sizeof( requestDigest ) );
	idCrypto::SecureZero( &issued, sizeof( issued ) );
	idCrypto::SecureZero( clientNonce, sizeof( clientNonce ) );
	idCrypto::SecureZero( serverNonce, sizeof( serverNonce ) );
}

/*
==================
idAsyncServer::ProcessGetInfoMessage
==================
*/
void idAsyncServer::ProcessGetInfoMessage( const netadr_t from, const idBitMsg &msg ) {
	int			i, challenge;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( !IsActive() ) {
		return;
	}
	if ( msg.GetRemainingData() != 4 || !AllowConnectionlessResponse( from, true ) ) {
		return;
	}

	common->DPrintf( "Sending info response to %s\n", Sys_NetAdrToString( from ) );

	challenge = msg.ReadLong();

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "infoResponse" );
	outMsg.WriteLong( challenge );
	outMsg.WriteLong( ASYNC_PROTOCOL_VERSION );
	outMsg.WriteDeltaDict( sessLocal.mapSpawnData.serverInfo, NULL );

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[i];

		if ( client.clientState < SCS_CONNECTED ) {
			continue;
		}

		outMsg.WriteByte( i );
		outMsg.WriteShort( client.clientPing );
		outMsg.WriteLong( client.channel.GetMaxOutgoingRate() );
		outMsg.WriteString( sessLocal.mapSpawnData.userInfo[i].GetString( "ui_name", "Player" ) );
	}
	outMsg.WriteByte( MAX_ASYNC_CLIENTS );
	outMsg.WriteLong( fileSystem->GetOSMask() );

	serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
}

/*
===============
idAsyncServer::PrintLocalServerInfo
see (client) "getInfo" -> (server) "infoResponse" -> (client)ProcessGetInfoMessage
===============
*/
void idAsyncServer::PrintLocalServerInfo( void ) {
	int i;

	common->Printf( "server '%s' IP = %s\nprotocol %d.%d OS mask 0x%x\n", 
					sessLocal.mapSpawnData.serverInfo.GetString( "si_name" ),
					Sys_NetAdrToString( serverPort.GetAdr() ),
					ASYNC_PROTOCOL_MAJOR,
					ASYNC_PROTOCOL_MINOR,
					fileSystem->GetOSMask() );
	sessLocal.mapSpawnData.serverInfo.Print();
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[i];
		if ( client.clientState < SCS_CONNECTED ) {
			continue;
		}
		common->Printf( "client %2d: %s, ping = %d, rate = %d\n", i,
						sessLocal.mapSpawnData.userInfo[i].GetString( "ui_name", "Player" ),
						client.clientPing, client.channel.GetMaxOutgoingRate() );
	}
}

/*
==================
idAsyncServer::ConnectionlessMessage
==================
*/
bool idAsyncServer::ConnectionlessMessage( const netadr_t from, const idBitMsg &msg ) {
	char		string[MAX_STRING_CHARS*2];  // M. Quinn - Even Balance - PB Packets need more than 1024

	msg.ReadString( string, sizeof( string ) );

	// info request
	if ( idStr::Icmp( string, "getInfo" ) == 0 ) {
		ProcessGetInfoMessage( from, msg );
		return false;
	}

	// remote console
	if ( idStr::Icmp( string, "rcon" ) == 0 ) {
		ProcessRemoteConsoleMessage( from, msg );
		return true;
	}
	if ( idStr::Icmp( string, "rcon2Challenge" ) == 0 ) {
		ProcessRemoteConsole2ChallengeMessage( from, msg );
		return false;
	}
	if ( idStr::Icmp( string, "rcon2" ) == 0 ) {
		ProcessRemoteConsole2Message( from, msg );
		return true;
	}

	if ( !active ) {
		if ( AllowConnectionlessResponse( from, false ) ) {
			PrintOOB( from, SERVER_PRINT_MISC, "#str_04849" );
		}
		return false;
	}

	// challenge from a client
	if ( idStr::Icmp( string, "challenge" ) == 0 ) {
		ProcessChallengeMessage( from, msg );
		return false;
	}

	// connect from a client
	if ( idStr::Icmp( string, "connect" ) == 0 ) {
		ProcessConnectMessage( from, msg );
		return false;
	}

	// pure mesasge from a client
	if ( idStr::Icmp( string, "pureClient" ) == 0 ) {
		ProcessPureMessage( from, msg );
		return false;
	}

	// download request
	if ( idStr::Icmp( string, "downloadRequest" ) == 0 ) {		
		ProcessDownloadRequestMessage( from, msg );
	}
	
	// auth server
	if ( idStr::Icmp( string, "auth" ) == 0 ) {
		if ( !Sys_CompareNetAdrBase( from, idAsyncNetwork::GetMasterAddress() ) ) {
			common->Printf( "auth: bad source %s\n", Sys_NetAdrToString( from ) );
			return false;
		}
		if ( idAsyncNetwork::LANServer.GetBool() ) {
			common->Printf( "auth message from master. net_LANServer is enabled, ignored.\n" );
		}
		ProcessAuthMessage( msg );
		return false;
	}

	return false;
}

/*
==================
idAsyncServer::ProcessMessage
==================
*/
bool idAsyncServer::ProcessMessage( const netadr_t from, idBitMsg &msg ) {
	int			i, id, sequence;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	id = msg.ReadShort();

	// check for a connectionless message
	if ( id == CONNECTIONLESS_MESSAGE_ID ) {
		return ConnectionlessMessage( from, msg );
	}

	if ( msg.GetRemaingData() < 4 ) {
		common->DPrintf( "%s: tiny packet\n", Sys_NetAdrToString( from ) );
		return false;
	}

	// find out which client the message is from
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[i];

		if ( client.clientState == SCS_FREE ) {
			continue;
		}

		// This does not compare the UDP port, because some address translating
		// routers will change that at arbitrary times.
		if ( !Sys_CompareNetAdrBase( from, client.channel.GetRemoteAddress() ) || id != client.clientId ) {
			continue;
		}

		// make sure it is a valid, in sequence packet
		if ( !client.channel.Process( from, serverTime, msg, sequence ) ) {
			return false;		// out of order, duplicated, fragment, etc.
		}

		// zombie clients still need to do the channel processing to make sure they don't
		// need to retransmit the final reliable message, but they don't do any other processing
		if ( client.clientState == SCS_ZOMBIE ) {
			return false;
		}

		client.lastPacketTime = serverTime;

		ProcessReliableClientMessages( i );
		ProcessUnreliableClientMessage( i, msg );

		return false;
	}
	
	// if we received a sequenced packet from an address we don't recognize,
	// send an out of band disconnect packet to it. This is still a pre-auth
	// response, so share the global/per-source control budget used by the
	// connectionless admission paths.
	if ( !AllowConnectionlessResponse( from, false ) ) {
		return false;
	}
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "disconnect" );
	serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );

	return false;
}

/*
==================
idAsyncServer::SendReliableGameMessage
==================
*/
void idAsyncServer::SendReliableGameMessage( int clientNum, const idBitMsg &msg, bool captureDemo ) {
	int			i;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( captureDemo && idAsyncNetwork::multiViewDemo.IsRecording() ) {
		const int routeClient = clientNum >= MAX_ASYNC_CLIENTS ? -1 : clientNum;
		idAsyncNetwork::multiViewDemo.CaptureReliableMessage( msg, DEMO_RECORD_CLIENTNUM, routeClient );
	}

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( SERVER_RELIABLE_MESSAGE_GAME );
	outMsg.WriteData( msg.GetData(), msg.GetSize() );

	// openQ4: only a negative clientNum means "everyone".  A clientNum at or past
	// the end of the client array is the game's server-demo pseudo client
	// (idGameLocal passes MAX_CLIENTS for it from ServerSendInstanceReliableMessage*
	// whenever the owner is in instance 0, i.e. every non-tourney match).  This
	// engine records no server demos, so that message has nowhere to go - and
	// falling through to the broadcast below delivered every instance-scoped
	// event a second time to every client, including the one the caller had just
	// asked to exclude.
	if ( clientNum >= 0 ) {
		if ( clientNum < MAX_ASYNC_CLIENTS && clients[clientNum].clientState == SCS_INGAME ) {
			SendReliableMessage( clientNum, outMsg );
		}
		return;
	}

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState != SCS_INGAME ) {
			continue;
		}
		SendReliableMessage( i, outMsg );
	}
}

/*
==================
idAsyncServer::LocalClientSendReliableMessageExcluding
==================
*/
void idAsyncServer::SendReliableGameMessageExcluding( int clientNum, const idBitMsg &msg, bool captureDemo ) {
	int			i;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( captureDemo && idAsyncNetwork::multiViewDemo.IsRecording() ) {
		idAsyncNetwork::multiViewDemo.CaptureReliableMessage( msg, DEMO_RECORD_EXCLUDE, clientNum );
	}

	//assert( clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS );

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( SERVER_RELIABLE_MESSAGE_GAME );
	outMsg.WriteData( msg.GetData(), msg.GetSize() );

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( i == clientNum ) {
			continue;
		}
		if ( clients[i].clientState != SCS_INGAME ) {
			continue;
		}
		SendReliableMessage( i, outMsg );
	}	
}

/*
==================
idAsyncServer::LocalClientSendReliableMessage
==================
*/
void idAsyncServer::LocalClientSendReliableMessage( const idBitMsg &msg ) {
	if ( localClientNum < 0 ) {
		common->Printf( "LocalClientSendReliableMessage: no local client\n" );
		return;
	}
	game->ServerProcessReliableMessage( localClientNum, msg );
}

/*
==================
idAsyncServer::ProcessConnectionLessMessages
==================
*/
void idAsyncServer::ProcessConnectionLessMessages( void ) {
	int			size, id;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	netadr_t	from;

	if ( !serverPort.GetPort() ) {
		return;
	}

	while( serverPort.GetPacket( from, msgBuf, size, sizeof( msgBuf ) ) ) {
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.SetSize( size );
		msg.BeginReading();
		id = msg.ReadShort();
		if ( id == CONNECTIONLESS_MESSAGE_ID ) {
			ConnectionlessMessage( from, msg );
		}
	}
}

/*
==================
idAsyncServer::UpdateTime
==================
*/
int idAsyncServer::UpdateTime( int clamp ) {
	int time, msec;

	time = Sys_Milliseconds();
	msec = idMath::ClampInt( 0, clamp, time - realTime );
	realTime = time;
	serverTime += msec;
	return msec;
}

/*
==================
idAsyncServer::RunFrame
==================
*/
void idAsyncServer::RunFrame( bool allowBlocking ) {
	int			i, msec, size;
	bool		newPacket;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	netadr_t	from;
	int			outgoingRate, incomingRate;
	float		outgoingCompression, incomingCompression;

	msec = UpdateTime( 100 );

	if ( !serverPort.GetPort() ) {
		return;
	}

	if ( !active ) {
		ProcessConnectionLessMessages();
		return;
	}
	
	gameTimeResidual += msec;

	// spin in place processing incoming packets until enough time lapsed to run a new game frame
	do {

		do {
			const int nextGameFrameMsec = AsyncServer_NextGameFrameMsec( gameFrame );
			const int packetTimeout = allowBlocking ? ( nextGameFrameMsec - gameTimeResidual - 1 ) : -1;

			// Foreground listen-server play polls instead of blocking so the render loop can
			// present repeated-state frames between authoritative server game ticks.
			newPacket = serverPort.GetPacketBlocking( from, msgBuf, size, sizeof( msgBuf ), packetTimeout );
			if ( newPacket ) {
				msg.Init( msgBuf, sizeof( msgBuf ) );
				msg.SetSize( size );
				msg.BeginReading();
				if ( ProcessMessage( from, msg ) ) {
					return;	// return because rcon was used
				}
			}

			msec = UpdateTime( 100 );
			gameTimeResidual += msec;

		} while( newPacket );

		if ( !allowBlocking ) {
			break;
		}

	} while( gameTimeResidual < AsyncServer_NextGameFrameMsec( gameFrame ) );

	// send heart beat to master servers
	MasterHeartbeat();

	// check for clients that timed out
	CheckClientTimeouts();

	if ( idAsyncNetwork::idleServer.GetBool() == ( !GetNumClients() || GetNumIdleClients() != GetNumClients()  ) ) {
		idAsyncNetwork::idleServer.SetBool( !idAsyncNetwork::idleServer.GetBool() );
		// the need to propagate right away, only this
		sessLocal.mapSpawnData.serverInfo.Set( "si_idleServer", idAsyncNetwork::idleServer.GetString() );
		game->SetServerInfo( sessLocal.mapSpawnData.serverInfo );
	}

	// make sure the time doesn't wrap
	if ( serverTime > 0x70000000 ) {
		ExecuteMapChange();
		return;
	}

	// check for synchronized cvar changes
	if ( cvarSystem->GetModifiedFlags() & CVAR_NETWORKSYNC ) {
		idDict newCvars;
		newCvars = *cvarSystem->MoveCVarsToDict( CVAR_NETWORKSYNC );
		SendSyncedCvarsBroadcast( newCvars );
		cvarSystem->ClearModifiedFlags( CVAR_NETWORKSYNC );
	}

	// check for user info changes of the local client
	if ( cvarSystem->GetModifiedFlags() & CVAR_USERINFO ) {
		if ( localClientNum >= 0 ) {
			idDict newInfo;
			game->ThrottleUserInfo( );
			newInfo = *cvarSystem->MoveCVarsToDict( CVAR_USERINFO );
			SendUserInfoBroadcast( localClientNum, newInfo );
		}
		cvarSystem->ClearModifiedFlags( CVAR_USERINFO );
	}

	// advance the server game
	while( gameTimeResidual >= AsyncServer_NextGameFrameMsec( gameFrame ) ) {
		const int nextGameFrameMsec = AsyncServer_NextGameFrameMsec( gameFrame );

		// sample input for the local client
		LocalClientInput();

		// duplicate usercmds for clients if no new ones are available
		DuplicateUsercmds( gameFrame, gameTime );

		// advance game
		gameReturn_t ret = game->RunFrame( userCmds[gameFrame & ( MAX_USERCMD_BACKUP - 1 ) ], 0, true, gameFrame );

		idAsyncNetwork::ExecuteSessionCommand( ret.sessionCommand );

		// update time
		gameFrame++;
		// openQ4: this stamp is handed to the game in every snapshot header and is
		// then compared against timestamps the game produced itself - projectile
		// launch times, entity event times - so it has to advance exactly the way
		// idGameLocal advances gameLocal.time, which is one GetUserCmdMSec() per
		// frame.  GetUserCmdTime()'s exact 1000/Hz ladder is 16.667ms at 60Hz
		// against the game's 16, and that 0.667ms a frame compounds into 40ms for
		// every second of map time.  Frame pacing still uses the exact ladder
		// (nextGameFrameMsec below); only the label the game sees changes.
		gameTime = gameFrame * common->GetUserCmdMSec();
		gameTimeResidual -= nextGameFrameMsec;
	}

	// duplicate usercmds so there is always at least one available to send with snapshots
	DuplicateUsercmds( gameFrame, gameTime );

	// send snapshots to connected clients
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[i];

		if ( client.clientState == SCS_FREE || i == localClientNum ) {
			continue;
		}

// jmarshall - Don't send update packets to bots.
		if (client.channel.GetRemoteAddress().type == NA_BOT)
			continue;
// jmarshall end

		// modify maximum rate if necesary
		if ( idAsyncNetwork::serverMaxClientRate.IsModified() ) {
			client.channel.SetMaxOutgoingRate( Min( client.clientRate, idAsyncNetwork::serverMaxClientRate.GetInteger() ) );
		}

		// if the channel is not yet ready to send new data
		if ( !client.channel.ReadyToSend( serverTime ) ) {
			if ( client.clientState == SCS_INGAME &&
				 serverTime - client.lastSnapshotTime >= idAsyncNetwork::serverSnapshotDelay.GetInteger() ) {
				// a snapshot was due and the rate limiter took it
				client.statsSendsRefusedByRate++;
			}
			continue;
		}

		// send additional message fragments if the last message was too large to send at once
		if ( client.channel.UnsentFragmentsLeft() ) {
			client.channel.SendNextFragment( serverPort, serverTime );
			continue;
		}

		if ( client.clientState == SCS_INGAME ) {
			if ( !SendSnapshotToClient( i ) ) {
				SendPingToClient( i );
			}
		} else {
			SendEmptyToClient( i );
		}
	}

	if ( com_showAsyncStats.GetBool() ) {

		UpdateAsyncStatsAvg();

		// openQ4: this used to be dedicated-only, which meant the one number that
		// explains "clients stop getting effects when it gets busy" - the update
		// rate a client is actually served, against the rate that was asked for -
		// could not be read on the listen server most testing happens on.
		const bool statsDue = serverTime >= nextAsyncStatsTime;
		if ( statsDue ) {
			for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
				serverClient_t &statsClient = clients[i];
				if ( statsClient.clientState != SCS_INGAME || i == localClientNum ) {
					continue;
				}
				if ( statsClient.statsSnapshotsSent > 0 || statsClient.statsSendsRefusedByRate > 0 ) {
					common->Printf( "client %d: %d snapshots/s (asked %d), avg %d bytes, %d refused by rate, cap %d B/s\n",
									i, statsClient.statsSnapshotsSent,
									1000 / Max( 1, idAsyncNetwork::serverSnapshotDelay.GetInteger() ),
									statsClient.statsSnapshotsSent > 0 ?
										statsClient.statsSnapshotBytes / statsClient.statsSnapshotsSent : 0,
									statsClient.statsSendsRefusedByRate,
									statsClient.channel.GetMaxOutgoingRate() );
				}
				statsClient.statsSnapshotsSent = 0;
				statsClient.statsSnapshotBytes = 0;
				statsClient.statsSendsRefusedByRate = 0;
			}
		}

		// dedicated will verbose to console
		if ( idAsyncNetwork::serverDedicated.GetBool() && statsDue ) {
			common->Printf( "delay = %d msec, total outgoing rate = %d KB/s, total incoming rate = %d KB/s\n", GetDelay(), 
							GetOutgoingRate() >> 10, GetIncomingRate() >> 10 );

			for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {

				outgoingRate = GetClientOutgoingRate( i );
				incomingRate = GetClientIncomingRate( i );
				outgoingCompression = GetClientOutgoingCompression( i );
				incomingCompression = GetClientIncomingCompression( i );

				if ( outgoingRate != -1 && incomingRate != -1 ) {
					common->Printf( "client %d: out rate = %d B/s (% -2.1f%%), in rate = %d B/s (% -2.1f%%)\n",
									i, outgoingRate, outgoingCompression, incomingRate, incomingCompression );
				}
			}

			idStr msg;
			GetAsyncStatsAvgMsg( msg );
			common->Printf( "%s\n", msg.c_str() );
		}

		if ( statsDue ) {
			nextAsyncStatsTime = serverTime + 1000;
		}
	}

	idAsyncNetwork::serverMaxClientRate.ClearModified();
}

/*
==================
idAsyncServer::PacifierUpdate
==================
*/
void idAsyncServer::PacifierUpdate( void ) {
	int i;

	if ( !IsActive() ) {
		return;
	}
	realTime = Sys_Milliseconds();
	ProcessConnectionLessMessages();
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState >= SCS_PUREWAIT ) {
			if ( clients[i].channel.UnsentFragmentsLeft() ) {
				clients[i].channel.SendNextFragment( serverPort, serverTime );
			} else {
				SendEmptyToClient( i );
			}
		}
	}
}

/*
==================
idAsyncServer::PrintOOB
==================
*/
void idAsyncServer::PrintOOB( const netadr_t to, int opcode, const char *string ) {
	idBitMsg	outMsg;
	byte		msgBuf[ MAX_MESSAGE_SIZE ];

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "print" );
	outMsg.WriteLong( opcode );
	outMsg.WriteString( string );
	serverPort.SendPacket( to, outMsg.GetData(), outMsg.GetSize() );
}

/*
==================
idAsyncServer::MasterHeartbeat
==================
*/
void idAsyncServer::MasterHeartbeat( bool force ) {
	if ( idAsyncNetwork::LANServer.GetBool() ) {
		if ( force ) {
			common->Printf( "net_LANServer is enabled. Not sending heartbeats\n" );
		}
		return;
	}
	if ( force ) {
		nextHeartbeatTime = 0;
	}
	// not yet
	if ( serverTime < nextHeartbeatTime ) {
		return;
	}
	nextHeartbeatTime = serverTime + HEARTBEAT_MSEC;
	for ( int i = 0 ; i < MAX_MASTER_SERVERS ; i++ ) {
		netadr_t adr;
		if ( idAsyncNetwork::GetMasterAddress( i, adr ) ) {
			common->Printf( "Sending heartbeat to %s\n", Sys_NetAdrToString( adr ) );
			idBitMsg outMsg;
			byte msgBuf[ MAX_MESSAGE_SIZE ];
			outMsg.Init( msgBuf, sizeof( msgBuf ) );
			outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
			outMsg.WriteString( "heartbeat" );
			serverPort.SendPacket( adr, outMsg.GetData(), outMsg.GetSize() );
		}
	}
}

/*
===============
idAsyncServer::SendEnterGameToClient
===============
*/
void idAsyncServer::SendEnterGameToClient( int clientNum ) {
	idBitMsg	msg;
	byte		msgBuf[ MAX_MESSAGE_SIZE ];

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_ENTERGAME );
	SendReliableMessage( clientNum, msg );
}

/*
===============
idAsyncServer::UpdateAsyncStatsAvg
===============
*/
void idAsyncServer::UpdateAsyncStatsAvg( void ) {
	stats_average_sum -= stats_outrate[ stats_current ];
	stats_outrate[ stats_current ] = idAsyncNetwork::server.GetOutgoingRate();
	if ( stats_outrate[ stats_current ] > stats_max ) {
		stats_max = stats_outrate[ stats_current ];
		stats_max_index = stats_current;
	} else if ( stats_current == stats_max_index ) {
		// find the new max
		int i;
		stats_max = 0;
		for ( i = 0; i < stats_numsamples ; i++ ) {
			if ( stats_outrate[ i ] > stats_max ) {
				stats_max = stats_outrate[ i ];
				stats_max_index = i;
			}
		}
	}
	stats_average_sum += stats_outrate[ stats_current ];
	stats_current++; stats_current %= stats_numsamples;
}

/*
===============
idAsyncServer::GetAsyncStatsAvgMsg
===============
*/
void idAsyncServer::GetAsyncStatsAvgMsg( idStr &msg ) {
	msg = va( "avrg out: %d B/s - max %d B/s ( over %d ms )", stats_average_sum / stats_numsamples, stats_max, idAsyncNetwork::serverSnapshotDelay.GetInteger() * stats_numsamples );
}

/*
===============
idAsyncServer::ProcessDownloadRequestMessage
===============
*/
void idAsyncServer::ProcessDownloadRequestMessage( const netadr_t from, const idBitMsg &msg ) {
	int			challenge, clientId, iclient, numPaks, i;
	int			dlGamePak;
	int			dlPakChecksum;
	int			dlSize[ MAX_PURE_PAKS ] = {};	// sizes; slot 0 is intentionally empty when no game pak is requested
	idStrList	pakNames;					// relative path
	idStrList	pakURLs;					// game URLs
	char		pakbuf[ MAX_STRING_CHARS ];
	idStr		paklist;
	byte		msgBuf[ MAX_MESSAGE_SIZE ];
	byte		tmpBuf[ MAX_MESSAGE_SIZE ];
	idBitMsg	outMsg, tmpMsg;
	int			dlRequest;
	int			voidSlots = 0;				// to count and verbose the right number of paks requested for downloads

	challenge = msg.ReadLong();
	clientId = msg.ReadShort();
	dlRequest = msg.ReadLong();

	if ( ( iclient = ValidateChallenge( from, challenge, clientId ) ) == -1 ) {
		return;
	}

	if ( challenges[ iclient ].authState != CDK_PUREWAIT ) {
		common->DPrintf( "client %s: got download request message, not in CDK_PUREWAIT\n", Sys_NetAdrToString( from ) );
		return;
	}
	if ( !AllowConnectionlessResponse( from, false ) ) {
		return;
	}
	
	// the first token of the pak names list passed to the game will be empty if no game pak is requested
	dlGamePak = msg.ReadLong();
	if ( dlGamePak ) {
		if ( !( dlSize[ 0 ] = fileSystem->ValidateDownloadPakForChecksum( dlGamePak, pakbuf, true ) ) ) {
			common->Warning( "client requested unknown game pak 0x%x", dlGamePak );
			pakbuf[ 0 ] = '\0';
			voidSlots++;
		}
	} else {
		pakbuf[ 0 ] = '\0';
		voidSlots++;
	}
	pakNames.Append( pakbuf );
	numPaks = 1;

	// read the checksums, build path names and pass that to the game code
	dlPakChecksum = msg.ReadLong();
	while ( dlPakChecksum ) {
		if ( numPaks >= MAX_PURE_PAKS ) {
			common->Warning( "client requested too many download paks" );
			return;
		}
		if ( !( dlSize[ numPaks ] = fileSystem->ValidateDownloadPakForChecksum( dlPakChecksum, pakbuf, false ) ) ) {
			// we pass an empty token to the game so our list doesn't get offset
			common->Warning( "client requested an unknown pak 0x%x", dlPakChecksum );
			pakbuf[ 0 ] = '\0';
			voidSlots++;
		}
		pakNames.Append( pakbuf );
		numPaks++;
		dlPakChecksum = msg.ReadLong();
	}

	for ( i = 0; i < pakNames.Num(); i++ ) {
		if ( i > 0 ) {
			paklist += ";";
		}
		paklist += pakNames[ i ].c_str();
	}

	// read the message and pass it to the game code
	common->DPrintf( "got download request for %d paks - %s\n", numPaks - voidSlots, paklist.c_str() );

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "downloadInfo" );
	outMsg.WriteLong( dlRequest );
	if ( !game->DownloadRequest( Sys_NetAdrToString( from ), challenges[ iclient ].guid, paklist.c_str(), pakbuf ) ) {
		common->DPrintf( "game: no downloads\n" );
		outMsg.WriteByte( SERVER_DL_NONE );
		serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
		return;
	}

	char *token, *next;
	int type = 0;

	token = pakbuf;
	next = strchr( token, ';' );
	while ( token ) {
		if ( next ) {
			*next = '\0';
		}
		
		if ( type == 0 ) {
			type = atoi( token );
		} else if ( type == SERVER_DL_REDIRECT ) {
			common->DPrintf( "download request: redirect to URL %s\n", token );
			outMsg.WriteByte( SERVER_DL_REDIRECT );
			outMsg.WriteString( token );
			serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
			return;
		} else if ( type == SERVER_DL_LIST ) {
			pakURLs.Append( token );
		} else {
			common->DPrintf( "wrong op type %d\n", type );
			next = token = NULL;
		}
		
		if ( next ) {
			token = next + 1;
			next = strchr( token, ';' );
		} else {
			token = NULL;
		}
	}

	if ( type == SERVER_DL_LIST ) {
		if ( pakURLs.Num() > pakNames.Num() ) {
			common->Warning( "game returned %d download URLs for only %d requested paks", pakURLs.Num(), pakNames.Num() );
			outMsg.WriteByte( SERVER_DL_NONE );
			serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
			return;
		}
		int totalDlSize = 0;
		int numActualPaks = 0;
		
		// put the answer packet together
		outMsg.WriteByte( SERVER_DL_LIST );			
		
		tmpMsg.Init( tmpBuf, MAX_MESSAGE_SIZE );

		for ( i = 0; i < pakURLs.Num(); i++ ) {
			tmpMsg.BeginWriting();
			if ( dlSize[ i ] <= 0 || !pakURLs[ i ].Length() || totalDlSize > idMath::INT_MAX - dlSize[ i ] ) {
				if ( dlSize[ i ] > 0 && pakURLs[ i ].Length() && totalDlSize > idMath::INT_MAX - dlSize[ i ] ) {
					common->Warning( "download response exceeds the supported total size; omitting '%s'", pakNames[ i ].c_str() );
				}
				// still send the relative path so the client knows what it missed
				tmpMsg.WriteByte( SERVER_PAK_NO );
				tmpMsg.WriteString( pakNames[ i ] );
			} else {
				totalDlSize += dlSize[ i ];
				numActualPaks++;
				tmpMsg.WriteByte( SERVER_PAK_YES );
				tmpMsg.WriteString( pakNames[ i ] );
				tmpMsg.WriteString( pakURLs[ i ] );
				tmpMsg.WriteLong( dlSize[ i ] );
			}
			
			// keep last 5 bytes for an 'end of message' - SERVER_PAK_END and the totalDlSize long
			if ( outMsg.GetRemainingSpace() - tmpMsg.GetSize() > 5 ) {
				outMsg.WriteData( tmpMsg.GetData(), tmpMsg.GetSize() );
			} else {
				outMsg.WriteByte( SERVER_PAK_END );
				break;
			}
		}
		if ( i == pakURLs.Num() ) {
			// put a closure even if size not exceeded
			outMsg.WriteByte( SERVER_PAK_END );
		}
		common->DPrintf( "download request: download %d paks, %d bytes\n", numActualPaks, totalDlSize );

		serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
	}
}

// jmarshall
/*
===============
idAsyncServer::ServerSetBotUserCommand
===============
*/
int idAsyncServer::ServerSetBotUserCommand(int clientNum, int frameNum, const usercmd_t& cmd) {
	usercmd_t realcmd;

	// Ensure this client is a bot.
	if (clients[clientNum].channel.GetRemoteAddress().type != NA_BOT)
		return -1;

	realcmd = cmd;
	realcmd.gameTime = gameFrame;
	realcmd.duplicateCount = gameTime;

	int index = gameFrame & (MAX_USERCMD_BACKUP - 1);
	userCmds[index][clientNum] = realcmd;

	return 1;
}

/*
===============
idAsyncServer::AllocOpenClientSlotForAI
===============
*/
int idAsyncServer::AllocOpenClientSlotForAI(const char* botName, int maxPlayersOnServer) {
	int numActivePlayers = 0;
	int botClientId = -1;
	idDict spawnArgs;

	// Check to see how many active players we have.
	for (int i = 0; i < MAX_ASYNC_CLIENTS; i++)
	{
		if (clients[i].clientState >= SCS_PUREWAIT)
		{
			numActivePlayers++;
		}
	}

	if (numActivePlayers >= maxPlayersOnServer) {
		common->Warning("idAsyncServer::AllocateClientSlotForBot: No open slots for bot\n");
		return -1;
	}

	// Find a free slot for the bot.
	for (int i = 0; i < MAX_ASYNC_CLIENTS; i++)
	{
		if (clients[i].clientState == SCS_FREE)
		{
			botClientId = i;
			break;
		}
	}

	if (botClientId == -1)
	{
		common->Warning("idAsyncServer::AllocateClientSlotForBot: Invalid client number\n");
		return -1;
	}

	idAsyncServer::InitLocalClient(botClientId, true);

	// Set all the spawn args for the new bot.
	spawnArgs.Set("ui_name", botName);

	// Init the new client, and broadcast it to the rest of the players.
	game->ServerClientBegin(botClientId, true, botName);
	idAsyncServer::SendUserInfoBroadcast(botClientId, spawnArgs, true);

	// openQ4: hand back the slot that was actually allocated.  The game side
	// needs it to drive this bot's user commands, and it used to get a bare 1.
	return botClientId;
}
// jmarshall end
