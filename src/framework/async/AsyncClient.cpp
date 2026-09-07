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

#include "../ArenaCampaign.h"
#include "../Session_local.h"
#include "../../sys/NetworkEndpoint.h"
#include "../../sys/URLPolicy.h"

#include <cstdint>

static const int ASYNC_CLIENT_MAX_NETWORK_TIME = idMath::INT_MAX - 65536;
static const int ASYNC_CLIENT_MAX_PREDICTION_MSEC = 60000;

static ID_INLINE int AsyncClient_MaxNetworkGameFrame() {
	const std::int64_t msecNumerator = common->GetUserCmdMsecNumerator();
	const std::int64_t msecDenominator = common->GetUserCmdMsecDenominator();
	const std::int64_t legacyTickMsec = common->GetUserCmdMSec();
	if ( msecNumerator <= 0 || msecDenominator <= 0 || legacyTickMsec <= 0 ) {
		return 0;
	}

	// Both exact-tic and legacy integer-tic conversions are used below. Leave
	// one frame of headroom for GetUserCmdTime( frame + 1 ) as well.
	const std::int64_t exactLimit =
		( static_cast<std::int64_t>( ASYNC_CLIENT_MAX_NETWORK_TIME ) * msecDenominator ) / msecNumerator;
	const std::int64_t legacyLimit =
		static_cast<std::int64_t>( ASYNC_CLIENT_MAX_NETWORK_TIME ) / legacyTickMsec;
	const std::int64_t maximumFrame = ( exactLimit < legacyLimit ? exactLimit : legacyLimit ) - 1;
	return maximumFrame > 0 ? static_cast<int>( maximumFrame ) : 0;
}

static ID_INLINE bool AsyncClient_ValidNetworkTiming( int gameFrame, int gameTime ) {
	return gameFrame >= 0 && gameFrame <= AsyncClient_MaxNetworkGameFrame() &&
		gameTime >= 0 && gameTime <= ASYNC_CLIENT_MAX_NETWORK_TIME;
}

static void AsyncClient_StopAfterMalformedSnapshot() {
	// ClientReadSnapshot may already have published timing, entities, or PVS state
	// before a later field proves that the packet is malformed.  Disconnecting the
	// channel alone leaves that partially updated map available to the remainder of
	// the outer frame, so tear down the loaded session immediately.
	arenaCampaign.AbortMatch();
	session->Stop();
}

static ID_INLINE int AsyncClient_MaxPredictionMsec() {
	return idMath::ClampInt( 0, ASYNC_CLIENT_MAX_PREDICTION_MSEC,
		idAsyncNetwork::clientMaxPrediction.GetInteger() );
}

static ID_INLINE int AsyncClient_NextGameFrameMsec( int gameFrame ) {
	return common->GetUserCmdDeltaMsec( gameFrame + 1 );
}

static ID_INLINE int AsyncClient_ConfiguredPredictionMsec( int gameFrame ) {
	// Preserve the legacy default as "one exact tic" instead of a literal 16 ms.
	if ( idAsyncNetwork::clientPrediction.GetInteger() == 16 ) {
		return Min( AsyncClient_NextGameFrameMsec( gameFrame ), AsyncClient_MaxPredictionMsec() );
	}
	return idMath::ClampInt( 0, AsyncClient_MaxPredictionMsec(),
		idAsyncNetwork::clientPrediction.GetInteger() );
}

static ID_INLINE bool AsyncClient_SameEndpoint( const netadr_t &left, const netadr_t &right ) {
	return left.port == right.port && Sys_CompareNetAdrBase( left, right );
}

static ID_INLINE std::uint32_t AsyncClient_Elapsed( int now, int then ) {
	return static_cast<std::uint32_t>( now ) - static_cast<std::uint32_t>( then );
}

static bool AsyncClient_SecureConnectionId( int &identifier ) {
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

static const int RCON2_CLIENT_TIMEOUT_MSEC = 10000;
static const int RCON2_CLIENT_RESEND_MSEC = 1000;

static bool AsyncClient_IsWindowsDevicePathSegment( const char *segment, int segmentLength ) {
	int stemLength = 0;
	while ( stemLength < segmentLength && segment[ stemLength ] != '.' ) {
		stemLength++;
	}

	if ( stemLength == 3 ) {
		return idStr::Icmpn( segment, "con", 3 ) == 0 ||
			idStr::Icmpn( segment, "prn", 3 ) == 0 ||
			idStr::Icmpn( segment, "aux", 3 ) == 0 ||
			idStr::Icmpn( segment, "nul", 3 ) == 0;
	}

	const bool portPrefix = stemLength >= 4 &&
		( idStr::Icmpn( segment, "com", 3 ) == 0 || idStr::Icmpn( segment, "lpt", 3 ) == 0 );
	if ( !portPrefix ) {
		return false;
	}

	const unsigned char digit = static_cast<unsigned char>( segment[ 3 ] );
	if ( stemLength == 4 ) {
		return ( digit >= '1' && digit <= '9' ) || digit == 0xB9 || digit == 0xB2 || digit == 0xB3;
	}

	return stemLength == 5 && digit == 0xC2 &&
		( static_cast<unsigned char>( segment[ 4 ] ) == 0xB9 ||
		  static_cast<unsigned char>( segment[ 4 ] ) == 0xB2 ||
		  static_cast<unsigned char>( segment[ 4 ] ) == 0xB3 );
}

static bool AsyncClient_IsSafeDownloadPath( const char *path ) {
	if ( path == NULL || path[ 0 ] == '\0' || path[ 0 ] == '/' || path[ 0 ] == '\\' ) {
		return false;
	}

	const char *segmentStart = path;
	for ( const char *scan = path; ; scan++ ) {
		const char c = *scan;
		if ( c == '\\' || c == ':' ) {
			return false;
		}
		if ( c != '\0' && ( static_cast<unsigned char>( c ) < 32 ||
			 c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' ) ) {
			return false;
		}
		if ( c != '/' && c != '\0' ) {
			continue;
		}

		const int segmentLength = static_cast<int>( scan - segmentStart );
		if ( segmentLength == 0 ||
			 ( segmentLength == 1 && segmentStart[ 0 ] == '.' ) ||
			 ( segmentLength == 2 && segmentStart[ 0 ] == '.' && segmentStart[ 1 ] == '.' ) ||
			 segmentStart[ 0 ] == ' ' ||
			 segmentStart[ segmentLength - 1 ] == '.' || segmentStart[ segmentLength - 1 ] == ' ' ) {
			return false;
		}
		if ( AsyncClient_IsWindowsDevicePathSegment( segmentStart, segmentLength ) ) {
			return false;
		}
		if ( c == '\0' ) {
			return segmentLength > 4 && idStr::Icmp( scan - 4, ".pk4" ) == 0;
		}
		segmentStart = scan + 1;
	}
}

static void AsyncClient_CloseBackgroundDownloadFile( backgroundDownload_t &download ) {
	idFile *file = download.f;
	download.f = NULL;
	if ( file != NULL ) {
		fileSystem->CloseFile( file );
	}
}

const int SETUP_CONNECTION_RESEND_TIME	= 1000;
const int EMPTY_RESEND_TIME				= 500;
const int PREDICTION_FAST_ADJUST		= 4;


/*
==================
idAsyncClient::idAsyncClient
==================
*/
idAsyncClient::idAsyncClient( void ) {
	guiNetMenu = NULL;
	updateState = UPDATE_NONE;
	Clear();
}

bool idAsyncClient::IsActive( void ) const {
	return active || idAsyncNetwork::multiViewDemo.IsPlaying();
}

int idAsyncClient::GetLocalClientNum( void ) const {
	return idAsyncNetwork::multiViewDemo.IsPlaying() ? MAX_ASYNC_CLIENTS : clientNum;
}

/*
==================
idAsyncClient::Clear
==================
*/
void idAsyncClient::Clear( void ) {
	active = false;
	realTime = 0;
	clientTime = 0;
	clientId = 0;
	clientDataChecksum = 0;
	clientNum = 0;
	clientState = CS_DISCONNECTED;
	clientPrediction = 0;
	clientPredictTime = 0;
	serverId = 0;
	serverChallenge = 0;
	serverMessageSequence = 0;
	lastConnectTime = -9999;
	lastEmptyTime = -9999;
	lastPacketTime = -9999;
	lastSnapshotTime = -9999;
	snapshotGameFrame = 0;
	snapshotGameTime = 0;
	snapshotSequence = 0;
	gameInitId = GAME_INIT_ID_INVALID;
	gameFrame = 0;
	gameTimeResidual = 0;
	gameTime = 0;
	memset( userCmds, 0, sizeof( userCmds ) );
	backgroundDownload.completed = true;
	backgroundDownload.url.expectedSize = 0;
	lastRconTime = 0;
	memset( &lastRconAddress, 0, sizeof( lastRconAddress ) );
	idCrypto::SecureZero( &rcon2Request, sizeof( rcon2Request ) );
	rcon2Request.state = RCON_REPLY_NONE;
	showUpdateMessage = false;
	lastFrameDelta = 0;

	dlRequest = -1;
	dlCount = -1;
	memset( dlChecksums, 0, sizeof( int ) * MAX_PURE_PAKS );
	currentDlSize = 0;
	totalDlSize = 0;
}

/*
==================
idAsyncClient::Shutdown
==================
*/
void idAsyncClient::Shutdown( void ) {
	guiNetMenu = NULL;
	updateMSG.Clear();
	backgroundDownload.url.url.Clear();
	dlList.Clear();
}

/*
==================
idAsyncClient::InitPort
==================
*/
bool idAsyncClient::InitPort( void ) {
	// if this is the first time we connect to a server, open the UDP port
	if ( !clientPort.GetPort() ) {
		if ( !clientPort.InitForPort( PORT_ANY ) ) {
			common->Printf( "Couldn't open client network port.\n" );
			return false;
		}
	}
	// maintain it valid between connects and ui manager reloads
	guiNetMenu = uiManager->FindGui( "guis/netmenu.gui", true, false, true );

	return true;
}

/*
==================
idAsyncClient::ClosePort
==================
*/
void idAsyncClient::ClosePort( void ) {
	ClearRemoteConsoleRequest();
	clientPort.Close();
}

/*
==================
idAsyncClient::ClearPendingPackets
==================
*/
void idAsyncClient::ClearPendingPackets( void ) {
	int			size;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	netadr_t	from;

	while( clientPort.GetPacket( from, msgBuf, size, sizeof( msgBuf ) ) ) {
	}
}

/*
==================
idAsyncClient::HandleGuiCommandInternal
==================
*/
const char* idAsyncClient::HandleGuiCommandInternal( const char *cmd ) {
	if ( !idStr::Cmp( cmd, "abort" ) || !idStr::Cmp( cmd, "pure_abort" ) ) {
		common->DPrintf( "connection aborted\n" );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
		return "";
	} else {
		common->DWarning( "idAsyncClient::HandleGuiCommand: unknown cmd %s", cmd );
	}
	return NULL;
}

/*
==================
idAsyncClient::HandleGuiCommand
==================
*/
const char* idAsyncClient::HandleGuiCommand( const char *cmd ) {
	return idAsyncNetwork::client.HandleGuiCommandInternal( cmd );
}

/*
==================
idAsyncClient::ConnectToServer
==================
*/
void idAsyncClient::ConnectToServer( const netadr_t adr ) {
	// shutdown any current game. that includes network disconnect
	// A console connect/reconnect can arrive during Arena's pre-server entrance,
	// or replace its loopback client. Release that campaign transaction before
	// Stop dismantles the session so its temporary multiplayer settings cannot
	// survive into the remote connection.
	arenaCampaign.AbortMatch();
	session->Stop();

	if ( !InitPort() ) {
		return;
	}

	if ( cvarSystem->GetCVarBool( "net_serverDedicated" ) ) {
		common->Printf( "Can't connect to a server as dedicated\n" );
		return;
	}

	// trash any currently pending packets
	ClearPendingPackets();
	
	serverAddress = adr;

	// clear the client state
	Clear();

	// Keep the legacy 15-bit wire field but remove the predictable clock seed.
	if ( !AsyncClient_SecureConnectionId( clientId ) ) {
		common->Warning( "OS secure random unavailable; connection attempt cancelled" );
		return;
	}

	// calculate a checksum on some of the essential data used
	clientDataChecksum = declManager->GetChecksum();
	common->DPrintf( "Client decl checksum: 0x%08x\n", static_cast<unsigned int>( clientDataChecksum ) );

	// start challenging the server
	clientState = CS_CHALLENGING;

	active = true;

	guiNetMenu = uiManager->FindGui( "guis/netmenu.gui", true, false, true );
	guiNetMenu->SetStateString( "status", va( common->GetLanguageDict()->GetString( "#str_06749" ), Sys_NetAdrToString( adr ) ) );
	session->SetGUI( guiNetMenu, HandleGuiCommand );
}

/*
==================
idAsyncClient::Reconnect
==================
*/
void idAsyncClient::Reconnect( void ) {
	ConnectToServer( serverAddress );
}

/*
==================
idAsyncClient::ConnectToServer
==================
*/
void idAsyncClient::ConnectToServer( const char *address ) {
	int serverNum;
	netadr_t adr;

	if ( idStr::IsNumeric( address ) ) {
		serverNum = atoi( address );
		if ( serverNum < 0 || serverNum >= serverList.Num() ) {
			session->MessageBox( MSG_OK, va( common->GetLanguageDict()->GetString( "#str_06733" ), serverNum ), common->GetLanguageDict()->GetString( "#str_06735" ), true );
			return;
		}
		adr = serverList[ serverNum ].adr;
	} else {
		if ( !Sys_StringToNetAdr( address, &adr, true ) ) {
			session->MessageBox( MSG_OK, va( common->GetLanguageDict()->GetString( "#str_06734" ), address ), common->GetLanguageDict()->GetString( "#str_06735" ), true );
			return;
		}
	}
	if ( !adr.port ) {
		adr.port = PORT_SERVER;
	}

	common->Printf( "\"%s\" resolved to %s\n", address, Sys_NetAdrToString( adr ) );

	ConnectToServer( adr );
}

/*
==================
idAsyncClient::DisconnectFromServer
==================
*/
void idAsyncClient::DisconnectFromServer( void ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( clientState >= CS_CONNECTED ) {
		// if we were actually connected, clear the pure list
		fileSystem->ClearPureChecksums();

		// send reliable disconnect to server
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.WriteByte( CLIENT_RELIABLE_MESSAGE_DISCONNECT );
		msg.WriteString( "disconnect" );

		// openQ4: a full reliable queue is ordinary backpressure, not a programming
		// error, and here the connection is already being torn down - there is nothing
		// left to salvage by killing the process.  Drop the backlog and carry on.
		if ( !channel.SendReliableMessage( msg ) ) {
			common->Warning( "client->server reliable message queue is full, disconnecting without notifying the server" );
		}

		SendEmptyToServer( true );
		SendEmptyToServer( true );
		SendEmptyToServer( true );
	}

	if ( clientState != CS_PURERESTART ) {
		channel.Shutdown();
		clientState = CS_DISCONNECTED;
	}

	active = false;
}

/*
==================
idAsyncClient::GetServerInfo
==================
*/
void idAsyncClient::GetServerInfo( const netadr_t adr ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	
	if ( !InitPort() ) {
		return;
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	msg.WriteString( "getInfo" );
	msg.WriteLong( serverList.GetChallenge() );	// challenge

	clientPort.SendPacket( adr, msg.GetData(), msg.GetSize() );	
}

/*
==================
idAsyncClient::GetServerInfo
==================
*/
void idAsyncClient::GetServerInfo( const char *address ) {
	netadr_t	adr;

	if ( address && *address != '\0' ) {
		if ( !Sys_StringToNetAdr( address, &adr, true ) ) {
			common->Printf( "Couldn't get server address for \"%s\"\n", address );
			return;
		}
	} else if ( active ) {
		adr = serverAddress;
	} else if ( idAsyncNetwork::server.IsActive() ) {
		// used to be a Sys_StringToNetAdr( "localhost", &adr, true ); and send a packet over loopback
		// but this breaks with net_ip ( typically, for multi-homed servers )
		idAsyncNetwork::server.PrintLocalServerInfo();
		return;
	} else {
		common->Printf( "no server found\n" );
		return;
	}

	if ( !adr.port ) {
		adr.port = PORT_SERVER;
	}

	GetServerInfo( adr );
}

/*
==================
idAsyncClient::GetLANServers
==================
*/
void idAsyncClient::GetLANServers( void ) {
	int			i;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	netadr_t	broadcastAddress;

	if ( !InitPort() ) {
		return;
	}

	idAsyncNetwork::LANServer.SetBool( true );

	serverList.SetupLANScan();

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	msg.WriteString( "getInfo" );
	msg.WriteLong( serverList.GetChallenge() );

	// Only .type and .port were ever assigned here, leaving the address bytes
	// and the scope id holding stack garbage that the IPv6 sweep below reads.
	memset( &broadcastAddress, 0, sizeof( broadcastAddress ) );
	broadcastAddress.type = NA_BROADCAST;
	for ( i = 0; i < MAX_SERVER_PORTS; i++ ) {
		broadcastAddress.port = PORT_SERVER + i;
		clientPort.SendPacket( broadcastAddress, msg.GetData(), msg.GetSize() );
	}

	// IPv6 has no broadcast address, so the same sweep goes to the link-local
	// discovery group. The platform layer expands one send into one datagram
	// per attached link. Without this an IPv6-only server is permanently
	// invisible to the server browser.
	netadr_t multicastAddress;
	memset( &multicastAddress, 0, sizeof( multicastAddress ) );
	multicastAddress.type = NA_MULTICAST6;
	for ( i = 0; i < MAX_SERVER_PORTS; i++ ) {
		multicastAddress.port = PORT_SERVER + i;
		clientPort.SendPacket( multicastAddress, msg.GetData(), msg.GetSize() );
	}
}

/*
==================
idAsyncClient::GetNETServers
==================
*/
void idAsyncClient::GetNETServers( void ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	idAsyncNetwork::LANServer.SetBool( false );

	// NetScan only clears GUI and results, not the stored list
	serverList.Clear( );
	serverList.NetScan( );
	serverList.StartServers( true );

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	msg.WriteString( "getServers" );
	msg.WriteLong( ASYNC_PROTOCOL_VERSION );
	msg.WriteString( cvarSystem->GetCVarString( "fs_game" ) );
	msg.WriteBits( cvarSystem->GetCVarInteger( "gui_filter_password" ), 2 );
	msg.WriteBits( cvarSystem->GetCVarInteger( "gui_filter_players" ), 2 );
	// The public master protocol has only two legacy gametype bits.  Request an
	// unfiltered list for extended openQ4 modes and apply the full registry in
	// idServerScan; truncating a larger value would ask the master for the wrong
	// legacy mode and hide valid results.
	const int localGameTypeFilter = cvarSystem->GetCVarInteger( "gui_filter_gameType" );
	const int masterGameTypeFilter = ( localGameTypeFilter >= 0 && localGameTypeFilter <= 3 ) ? localGameTypeFilter : 0;
	msg.WriteBits( masterGameTypeFilter, 2 );

	netadr_t adr;
	if ( idAsyncNetwork::GetMasterAddress( 0, adr ) ) {
		clientPort.SendPacket( adr, msg.GetData(), msg.GetSize() );

		// Ask for the address-family tagged list as well. A master that does
		// not implement it simply ignores the request, and the legacy reply
		// above still arrives, so this costs one datagram and never regresses
		// an IPv4-only deployment.
		idBitMsg	extMsg;
		byte		extMsgBuf[MAX_MESSAGE_SIZE];
		extMsg.Init( extMsgBuf, sizeof( extMsgBuf ) );
		extMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
		extMsg.WriteString( "getServersExt" );
		extMsg.WriteLong( ASYNC_PROTOCOL_VERSION );
		extMsg.WriteString( cvarSystem->GetCVarString( "fs_game" ) );
		extMsg.WriteBits( cvarSystem->GetCVarInteger( "gui_filter_password" ), 2 );
		extMsg.WriteBits( cvarSystem->GetCVarInteger( "gui_filter_players" ), 2 );
		extMsg.WriteBits( masterGameTypeFilter, 2 );
		clientPort.SendPacket( adr, extMsg.GetData(), extMsg.GetSize() );
	}
}

/*
==================
idAsyncClient::ListServers
==================
*/
void idAsyncClient::ListServers( void ) {
	int i;

	for ( i = 0; i < serverList.Num(); i++ ) {
		common->Printf( "%3d: %s %dms (%s)\n", i, serverList[i].serverInfo.GetString( "si_name" ), serverList[ i ].ping, Sys_NetAdrToString( serverList[i].adr ) );
	}
}

/*
==================
idAsyncClient::ClearServers
==================
*/
void idAsyncClient::ClearServers( void ) {
	serverList.Clear();
}

/*
==================
idAsyncClient::RemoteConsole
==================
*/
void idAsyncClient::RemoteConsole( const char *command ) {
	netadr_t	adr;

	if ( !InitPort() ) {
		return;
	}
	if ( command == NULL || command[0] == '\0' || strlen( command ) >= MAX_STRING_CHARS ) {
		common->Printf( "usage: rcon <command> (maximum %d bytes)\n", MAX_STRING_CHARS - 1 );
		return;
	}

	if ( active ) {
		adr = serverAddress;
	} else {
		const char *address = idAsyncNetwork::clientRemoteConsoleAddress.GetString();
		if ( !Sys_StringToNetAdr( address, &adr, true ) ) {
			common->Printf( "Couldn't resolve remote console address \"%s\"\n", address );
			return;
		}
	}
	
	if ( !adr.port ) {
		adr.port = PORT_SERVER;
	}

	ClearRemoteConsoleRequest();
	lastRconAddress = adr;
	lastRconTime = realTime;

	const char *password = idAsyncNetwork::clientRemoteConsolePassword.GetString();
	if ( password[0] == '\0' ) {
		common->Printf( "Set net_clientRemoteConsolePassword before using rcon.\n" );
		ClearRemoteConsoleRequest();
		return;
	}

	if ( idAsyncNetwork::clientUseLegacyRcon.GetBool() ) {
		byte msgBuf[MAX_MESSAGE_SIZE];
		idBitMsg msg;
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
		msg.WriteString( "rcon" );
		msg.WriteString( password );
		msg.WriteString( command );
		rcon2Request.state = RCON_REPLY_LEGACY;
		rcon2Request.address = adr;
		rcon2Request.startTime = realTime;
		common->Warning( "sending legacy rcon password as plaintext because net_clientUseLegacyRcon is enabled" );
		clientPort.SendPacket( adr, msg.GetData(), msg.GetSize() );
		idCrypto::SecureZero( msgBuf, sizeof( msgBuf ) );
		return;
	}

	if ( strlen( password ) < idRcon2::MIN_PASSWORD_BYTES ) {
		common->Printf( "rcon2 requires a password of at least %u bytes.\n",
			static_cast<unsigned int>( idRcon2::MIN_PASSWORD_BYTES ) );
		ClearRemoteConsoleRequest();
		return;
	}

	rcon2Request.state = RCON_REPLY_CHALLENGE;
	rcon2Request.address = adr;
	rcon2Request.startTime = realTime;
	rcon2Request.lastSendTime = realTime;
	idStr::Copynz( rcon2Request.command, command, sizeof( rcon2Request.command ) );
	if ( !Sys_GetSecureRandomBytes( rcon2Request.clientNonce, sizeof( rcon2Request.clientNonce ) ) ) {
		common->Warning( "OS secure random unavailable; rcon2 request cancelled" );
		ClearRemoteConsoleRequest();
		return;
	}
	idRcon2::HashRequest( rcon2Request.command, rcon2Request.requestDigest );
	SendRemoteConsole2Challenge();
}

/*
==================
idAsyncClient::ClearRemoteConsoleRequest
==================
*/
void idAsyncClient::ClearRemoteConsoleRequest( void ) {
	idCrypto::SecureZero( &rcon2Request, sizeof( rcon2Request ) );
	rcon2Request.state = RCON_REPLY_NONE;
	memset( &lastRconAddress, 0, sizeof( lastRconAddress ) );
	lastRconTime = 0;
}

void idAsyncClient::SendRemoteConsole2Challenge( void ) {
	if ( rcon2Request.state != RCON_REPLY_CHALLENGE ) {
		return;
	}
	byte msgBuf[128];
	idBitMsg msg;
	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	msg.WriteString( "rcon2Challenge" );
	msg.WriteByte( idRcon2::PROTOCOL_VERSION );
	msg.WriteData( rcon2Request.clientNonce, sizeof( rcon2Request.clientNonce ) );
	msg.WriteData( rcon2Request.requestDigest, sizeof( rcon2Request.requestDigest ) );
	clientPort.SendPacket( rcon2Request.address, msg.GetData(), msg.GetSize() );
	rcon2Request.lastSendTime = realTime;
	lastRconTime = realTime;
	idCrypto::SecureZero( msgBuf, sizeof( msgBuf ) );
}

void idAsyncClient::SendRemoteConsole2Proof( void ) {
	if ( rcon2Request.state != RCON_REPLY_OUTPUT ) {
		return;
	}
	byte msgBuf[MAX_MESSAGE_SIZE];
	idBitMsg msg;
	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	msg.WriteString( "rcon2" );
	msg.WriteByte( idRcon2::PROTOCOL_VERSION );
	msg.WriteData( rcon2Request.clientNonce, sizeof( rcon2Request.clientNonce ) );
	msg.WriteData( rcon2Request.serverNonce, sizeof( rcon2Request.serverNonce ) );
	msg.WriteString( rcon2Request.command );
	msg.WriteData( rcon2Request.proof, sizeof( rcon2Request.proof ) );
	clientPort.SendPacket( rcon2Request.address, msg.GetData(), msg.GetSize() );
	rcon2Request.lastSendTime = realTime;
	lastRconTime = realTime;
	idCrypto::SecureZero( msgBuf, sizeof( msgBuf ) );
}

void idAsyncClient::UpdateRemoteConsoleRequest( void ) {
	if ( rcon2Request.state == RCON_REPLY_NONE ) {
		return;
	}
	if ( AsyncClient_Elapsed( realTime, rcon2Request.startTime ) > RCON2_CLIENT_TIMEOUT_MSEC ) {
		common->Printf( "remote console request timed out\n" );
		ClearRemoteConsoleRequest();
		return;
	}
	if ( rcon2Request.state == RCON_REPLY_LEGACY ) {
		return;
	}
	if ( AsyncClient_Elapsed( realTime, rcon2Request.lastSendTime ) < RCON2_CLIENT_RESEND_MSEC ) {
		return;
	}
	if ( rcon2Request.state == RCON_REPLY_CHALLENGE ) {
		SendRemoteConsole2Challenge();
	} else if ( rcon2Request.state == RCON_REPLY_OUTPUT ) {
		SendRemoteConsole2Proof();
	}
}

/*
==================
idAsyncClient::ProcessRemoteConsole2ChallengeResponse
==================
*/
void idAsyncClient::ProcessRemoteConsole2ChallengeResponse( const netadr_t from, const idBitMsg &msg ) {
	const int responseBytes = 1 + idRcon2::NONCE_BYTES + idRcon2::NONCE_BYTES +
		idRcon2::SALT_BYTES + 4 + idRcon2::ENDPOINT_BINDING_BYTES + idRcon2::REQUEST_DIGEST_BYTES;
	if ( rcon2Request.state != RCON_REPLY_CHALLENGE ||
		!AsyncClient_SameEndpoint( from, rcon2Request.address ) ||
		msg.GetRemainingData() != responseBytes ||
		msg.ReadByte() != idRcon2::PROTOCOL_VERSION ) {
		return;
	}
	byte clientNonce[idRcon2::NONCE_BYTES];
	byte serverNonce[idRcon2::NONCE_BYTES];
	byte salt[idRcon2::SALT_BYTES];
	byte endpointBinding[idRcon2::ENDPOINT_BINDING_BYTES];
	byte requestDigest[idRcon2::REQUEST_DIGEST_BYTES];
	byte verifier[idRcon2::VERIFIER_BYTES];
	msg.ReadData( clientNonce, sizeof( clientNonce ) );
	msg.ReadData( serverNonce, sizeof( serverNonce ) );
	msg.ReadData( salt, sizeof( salt ) );
	const int iterations = msg.ReadLong();
	msg.ReadData( endpointBinding, sizeof( endpointBinding ) );
	msg.ReadData( requestDigest, sizeof( requestDigest ) );

	const bool responseMatches = iterations == static_cast<int>( idRcon2::PBKDF2_ITERATIONS ) &&
		idCrypto::ConstantTimeEquals( clientNonce, rcon2Request.clientNonce, sizeof( clientNonce ) ) &&
		idCrypto::ConstantTimeEquals( requestDigest, rcon2Request.requestDigest, sizeof( requestDigest ) );
	if ( !responseMatches || !idRcon2::DeriveVerifier(
		idAsyncNetwork::clientRemoteConsolePassword.GetString(), salt, verifier ) ) {
		common->Warning( "invalid rcon2 challenge response" );
		idCrypto::SecureZero( clientNonce, sizeof( clientNonce ) );
		idCrypto::SecureZero( serverNonce, sizeof( serverNonce ) );
		idCrypto::SecureZero( salt, sizeof( salt ) );
		idCrypto::SecureZero( endpointBinding, sizeof( endpointBinding ) );
		idCrypto::SecureZero( requestDigest, sizeof( requestDigest ) );
		idCrypto::SecureZero( verifier, sizeof( verifier ) );
		ClearRemoteConsoleRequest();
		return;
	}

	memcpy( rcon2Request.serverNonce, serverNonce, sizeof( rcon2Request.serverNonce ) );
	idRcon2::ComputeProof( verifier, rcon2Request.clientNonce, serverNonce,
		endpointBinding, rcon2Request.requestDigest, rcon2Request.proof );
	rcon2Request.state = RCON_REPLY_OUTPUT;
	rcon2Request.lastSendTime = realTime;
	SendRemoteConsole2Proof();

	idCrypto::SecureZero( clientNonce, sizeof( clientNonce ) );
	idCrypto::SecureZero( serverNonce, sizeof( serverNonce ) );
	idCrypto::SecureZero( salt, sizeof( salt ) );
	idCrypto::SecureZero( endpointBinding, sizeof( endpointBinding ) );
	idCrypto::SecureZero( requestDigest, sizeof( requestDigest ) );
	idCrypto::SecureZero( verifier, sizeof( verifier ) );
}

void idAsyncClient::ProcessRemoteConsole2Complete( const netadr_t from, const idBitMsg &msg ) {
	const int completeBytes = 1 + idRcon2::NONCE_BYTES + idRcon2::NONCE_BYTES;
	if ( rcon2Request.state != RCON_REPLY_OUTPUT ||
		!AsyncClient_SameEndpoint( from, rcon2Request.address ) ||
		msg.GetRemainingData() != completeBytes ||
		msg.ReadByte() != idRcon2::PROTOCOL_VERSION ) {
		return;
	}
	byte clientNonce[idRcon2::NONCE_BYTES];
	byte serverNonce[idRcon2::NONCE_BYTES];
	msg.ReadData( clientNonce, sizeof( clientNonce ) );
	msg.ReadData( serverNonce, sizeof( serverNonce ) );
	const bool matches = idCrypto::ConstantTimeEquals( clientNonce,
		rcon2Request.clientNonce, sizeof( clientNonce ) ) &&
		idCrypto::ConstantTimeEquals( serverNonce, rcon2Request.serverNonce, sizeof( serverNonce ) );
	idCrypto::SecureZero( clientNonce, sizeof( clientNonce ) );
	idCrypto::SecureZero( serverNonce, sizeof( serverNonce ) );
	if ( matches ) {
		ClearRemoteConsoleRequest();
	}
}

/*
==================
idAsyncClient::GetPrediction
==================
*/
int idAsyncClient::GetPrediction( void ) const {
	if ( clientState < CS_CONNECTED ) {
		return -1;
	} else {
		return clientPrediction;
	}
}

/*
==================
idAsyncClient::GetTimeSinceLastPacket
==================
*/
int idAsyncClient::GetTimeSinceLastPacket( void ) const {
	if ( clientState < CS_CONNECTED ) {
		return -1;
	} else {
		return clientTime - lastPacketTime;
	}
}

/*
==================
idAsyncClient::GetOutgoingRate
==================
*/
int idAsyncClient::GetOutgoingRate( void ) const {
	if ( clientState < CS_CONNECTED ) {
		return -1;
	} else {
		return channel.GetOutgoingRate();
	}
}

/*
==================
idAsyncClient::GetIncomingRate
==================
*/
int idAsyncClient::GetIncomingRate( void ) const {
	if ( clientState < CS_CONNECTED ) {
		return -1;
	} else {
		return channel.GetIncomingRate();
	}
}

/*
==================
idAsyncClient::GetOutgoingCompression
==================
*/
float idAsyncClient::GetOutgoingCompression( void ) const {
	if ( clientState < CS_CONNECTED ) {
		return 0.0f;
	} else {
		return channel.GetOutgoingCompression();
	}
}

/*
==================
idAsyncClient::GetIncomingCompression
==================
*/
float idAsyncClient::GetIncomingCompression( void ) const {
	if ( clientState < CS_CONNECTED ) {
		return 0.0f;
	} else {
		return channel.GetIncomingCompression();
	}
}

/*
==================
idAsyncClient::GetIncomingPacketLoss
==================
*/
float idAsyncClient::GetIncomingPacketLoss( void ) const {
	if ( clientState < CS_CONNECTED ) {
		return 0.0f;
	} else {
		return channel.GetIncomingPacketLoss();
	}
}

/*
==================
idAsyncClient::DuplicateUsercmds
==================
*/
void idAsyncClient::DuplicateUsercmds( int frame, int time ) {
	int i, previousIndex, currentIndex;

	previousIndex = ( frame - 1 ) & ( MAX_USERCMD_BACKUP - 1 );
	currentIndex = frame & ( MAX_USERCMD_BACKUP - 1 );

	// duplicate previous user commands if no new commands are available for a client
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		idAsyncNetwork::DuplicateUsercmd( userCmds[previousIndex][i], userCmds[currentIndex][i], frame, time );
	}
}

/*
==================
idAsyncClient::SendUserInfoToServer
==================
*/
void idAsyncClient::SendUserInfoToServer( void ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	idDict		info;

	if ( clientState < CS_CONNECTED ) {
		return;
	}

	info = *cvarSystem->MoveCVarsToDict( CVAR_USERINFO );
	
	// send reliable client info to server
	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( CLIENT_RELIABLE_MESSAGE_CLIENTINFO );
	msg.WriteDeltaDict( info, &sessLocal.mapSpawnData.userInfo[ clientNum ] );

	// openQ4: a full reliable queue is backpressure, not a programming error, so it
	// must not bring the whole process down through common->Error.  Warn and take the
	// ordinary disconnect path, the way idAsyncServer::SendReliableMessage drops a
	// client it can no longer talk to.  Unlike the server we deliberately leave the
	// queue intact - see below.
	if ( !channel.SendReliableMessage( msg ) ) {
		common->Warning( "client->server reliable message queue overflowed, disconnecting" );
		// Deliberately NOT ClearReliableMessages(): that re-inits reliableSend to
		// sequence 1 while the server still expects the next sequence after the one
		// it last accepted, so every later message - including the disconnect notice
		// itself - would be silently discarded and the server would hold a ghost slot
		// until the client timeout.  The queue dies with the channel a moment later.
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
		return;
	}

	sessLocal.mapSpawnData.userInfo[clientNum] = info;
}

/*
==================
idAsyncClient::SendEmptyToServer
==================
*/
void idAsyncClient::SendEmptyToServer( bool force, bool mapLoad ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( lastEmptyTime > realTime ) {
		lastEmptyTime = realTime;
	}

	if ( !force && ( realTime - lastEmptyTime < EMPTY_RESEND_TIME ) ) {
		return;
	}

	if ( idAsyncNetwork::verbose.GetInteger() ) {
		common->Printf( "sending empty to server, gameInitId = %d\n", mapLoad ? GAME_INIT_ID_MAP_LOAD : gameInitId );
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteLong( serverMessageSequence );
	msg.WriteLong( mapLoad ? GAME_INIT_ID_MAP_LOAD : gameInitId );
	msg.WriteLong( snapshotSequence );
	msg.WriteByte( CLIENT_UNRELIABLE_MESSAGE_EMPTY );

	channel.SendMessage( clientPort, clientTime, msg );

	while( channel.UnsentFragmentsLeft() ) {
		channel.SendNextFragment( clientPort, clientTime );
	}

	lastEmptyTime = realTime;
}

/*
==================
idAsyncClient::SendPingResponseToServer
==================
*/
void idAsyncClient::SendPingResponseToServer( int time ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( idAsyncNetwork::verbose.GetInteger() == 2 ) {
		common->Printf( "sending ping response to server, gameInitId = %d\n", gameInitId );
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteLong( serverMessageSequence );
	msg.WriteLong( gameInitId );
	msg.WriteLong( snapshotSequence );
	msg.WriteByte( CLIENT_UNRELIABLE_MESSAGE_PINGRESPONSE );
	msg.WriteLong( time );

	channel.SendMessage( clientPort, clientTime, msg );
	while( channel.UnsentFragmentsLeft() ) {
		channel.SendNextFragment( clientPort, clientTime );
	}
}

/*
==================
idAsyncClient::SendUsercmdsToServer
==================
*/
void idAsyncClient::SendUsercmdsToServer( void ) {
	int			i, numUsercmds, index;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	usercmd_t *	last;

	if ( idAsyncNetwork::verbose.GetInteger() == 2 ) {
		common->Printf( "sending usercmd to server: gameInitId = %d, gameFrame = %d, gameTime = %d\n", gameInitId, gameFrame, gameTime );
	}
	if ( gameFrame < 0 || gameFrame > AsyncClient_MaxNetworkGameFrame() ) {
		common->Warning( "cannot send a user command for invalid game frame %d", gameFrame );
		return;
	}

	// generate user command for this client
	index = gameFrame & ( MAX_USERCMD_BACKUP - 1 );
	userCmds[index][clientNum] = usercmdGen->GetDirectUsercmd();
	userCmds[index][clientNum].gameFrame = gameFrame;
	userCmds[index][clientNum].gameTime = gameTime;

	// send the user commands to the server
	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteLong( serverMessageSequence );
	msg.WriteLong( gameInitId );
	msg.WriteLong( snapshotSequence );
	msg.WriteByte( CLIENT_UNRELIABLE_MESSAGE_USERCMD );
	msg.WriteShort( clientPrediction );

	const int requestedUsercmds = idMath::ClampInt( 0, MAX_USERCMD_PACKET_COMMANDS - 1,
		idAsyncNetwork::clientUsercmdBackup.GetInteger() ) + 1;
	numUsercmds = gameFrame >= requestedUsercmds - 1 ? requestedUsercmds : gameFrame + 1;

	// write the user commands
	msg.WriteLong( gameFrame );
	msg.WriteByte( numUsercmds );
	for ( last = NULL, i = gameFrame - numUsercmds + 1; i <= gameFrame; i++ ) {
		index = i & ( MAX_USERCMD_BACKUP - 1 );
		idAsyncNetwork::WriteUserCmdDelta( msg, userCmds[index][clientNum], last );
		last = &userCmds[index][clientNum];
	}

	channel.SendMessage( clientPort, clientTime, msg );
	while( channel.UnsentFragmentsLeft() ) {
		channel.SendNextFragment( clientPort, clientTime );
	}
}

/*
==================
idAsyncClient::InitGame
==================
*/
void idAsyncClient::InitGame( int serverGameInitId, int serverGameFrame, int serverGameTime, const idDict &serverSI ) {
	gameInitId = serverGameInitId;
	gameFrame = snapshotGameFrame = serverGameFrame;
	gameTime = snapshotGameTime = serverGameTime;
	gameTimeResidual = 0;
	memset( userCmds, 0, sizeof( userCmds ) );

	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		sessLocal.mapSpawnData.userInfo[ i ].Clear();
	}

	sessLocal.mapSpawnData.serverInfo = serverSI;
}

/*
==================
idAsyncClient::ProcessUnreliableServerMessage
==================
*/
void idAsyncClient::ProcessUnreliableServerMessage( const idBitMsg &msg ) {
	int i, j, index, id, numDuplicatedUsercmds, aheadOfServer, numUsercmds, delta;
	int serverGameInitId, serverGameFrame, serverGameTime;
	idDict serverSI;
	usercmd_t *last;
	bool pureWait;

	if ( msg.GetRemainingReadBits() < 32 + 8 ) {
		common->Warning( "server sent a truncated unreliable message; disconnecting safely" );
		DisconnectFromServer();
		return;
	}
	serverGameInitId = msg.ReadLong();

	id = msg.ReadByte();
	switch( id ) {
		case SERVER_UNRELIABLE_MESSAGE_EMPTY: {
			if ( idAsyncNetwork::verbose.GetInteger() ) {
				common->Printf( "received empty message from server\n" );
			}
			break;
		}
		case SERVER_UNRELIABLE_MESSAGE_PING: {
			if ( idAsyncNetwork::verbose.GetInteger() == 2 ) {
				common->Printf( "received ping message from server\n" );
			}
			if ( msg.GetRemainingReadBits() < 32 ) {
				common->Warning( "server sent a truncated ping; disconnecting safely" );
				DisconnectFromServer();
				return;
			}
			SendPingResponseToServer( msg.ReadLong() );
			break;
		}
		case SERVER_UNRELIABLE_MESSAGE_GAMEINIT: {
			if ( msg.GetRemainingReadBits() < 32 + 32 ) {
				common->Warning( "server sent a truncated game-init message; disconnecting safely" );
				DisconnectFromServer();
				return;
			}
			serverGameFrame = msg.ReadLong();
			serverGameTime = msg.ReadLong();
			msg.ReadDeltaDict( serverSI, NULL );
			if ( msg.IsReadOverflowed() || !AsyncClient_ValidNetworkTiming( serverGameFrame, serverGameTime ) ) {
				common->Warning( "server sent invalid game-init timing; disconnecting safely" );
				DisconnectFromServer();
				return;
			}
			pureWait = serverSI.GetBool( "si_pure" );

			InitGame( serverGameInitId, serverGameFrame, serverGameTime, serverSI );

			channel.ResetRate();

			if ( idAsyncNetwork::verbose.GetInteger() ) {
				common->Printf( "received gameinit, gameInitId = %d, gameFrame = %d, gameTime = %d\n", gameInitId, gameFrame, gameTime );
			}

			// mute sound
			soundSystem->SetMute( true );

			// ensure chat icon goes away when the GUI is changed...
			//cvarSystem->SetCVarBool( "ui_chat", false );

			if ( pureWait ) {
				guiNetMenu = uiManager->FindGui( "guis/netmenu.gui", true, false, true );
				session->SetGUI( guiNetMenu, HandleGuiCommand );
				session->MessageBox( MSG_ABORT, common->GetLanguageDict()->GetString ( "#str_04317" ), common->GetLanguageDict()->GetString ( "#str_04318" ), false, "pure_abort" );
			} else {
				// load map
				session->SetGUI( NULL, NULL );
				sessLocal.ExecuteMapChange();
			}

			break;
		}
		case SERVER_UNRELIABLE_MESSAGE_SNAPSHOT: {
			// if the snapshot is from a different game
			if ( serverGameInitId != gameInitId ) {
				if ( idAsyncNetwork::verbose.GetInteger() ) {
					common->Printf( "ignoring snapshot with != gameInitId\n" );
				}
				break;
			}

			if ( msg.GetRemainingReadBits() < 32 + 32 + 32 + 8 + 16 ) {
				common->Warning( "server sent a truncated snapshot header; disconnecting safely" );
				DisconnectFromServer();
				return;
			}
			const int receivedSnapshotSequence = msg.ReadLong();
			const int receivedSnapshotGameFrame = msg.ReadLong();
			const int receivedSnapshotGameTime = msg.ReadLong();
			numDuplicatedUsercmds = msg.ReadByte();
			aheadOfServer = msg.ReadShort();
			if ( msg.IsReadOverflowed() ||
				 !AsyncClient_ValidNetworkTiming( receivedSnapshotGameFrame, receivedSnapshotGameTime ) ) {
				common->Warning( "server sent invalid snapshot timing; disconnecting safely" );
				DisconnectFromServer();
				return;
			}
			snapshotSequence = receivedSnapshotSequence;
			snapshotGameFrame = receivedSnapshotGameFrame;
			snapshotGameTime = receivedSnapshotGameTime;

			// read the game snapshot
			if ( !game->ClientReadSnapshot(
					clientNum, snapshotSequence, snapshotGameFrame, snapshotGameTime,
					numDuplicatedUsercmds, aheadOfServer, msg ) ) {
				common->Warning( "server sent malformed snapshot %d; disconnecting safely",
					snapshotSequence );
				AsyncClient_StopAfterMalformedSnapshot();
				return;
			}

			// read user commands of other clients from the snapshot
			last = NULL;
			while ( true ) {
				i = msg.ReadByte();
				if ( msg.IsReadOverflowed() || i > MAX_ASYNC_CLIENTS ) {
					common->Warning( "snapshot %d has an invalid user-command terminator; disconnecting safely",
						snapshotSequence );
					AsyncClient_StopAfterMalformedSnapshot();
					return;
				}
				if ( i == MAX_ASYNC_CLIENTS ) {
					break;
				}
				numUsercmds = msg.ReadByte();
				if ( msg.IsReadOverflowed() || numUsercmds < 1 || numUsercmds > MAX_USERCMD_RELAY ) {
					common->Warning( "snapshot %d contains an invalid user-command count for client %d; disconnecting safely",
						snapshotSequence, i );
					AsyncClient_StopAfterMalformedSnapshot();
					return;
				}
				for ( j = 0; j < numUsercmds; j++ ) {
					index = ( snapshotGameFrame + j ) & ( MAX_USERCMD_BACKUP - 1 );
					if ( !idAsyncNetwork::ReadUserCmdDelta( msg, userCmds[index][i], last ) ) {
						common->Warning( "snapshot %d contains a truncated user command for client %d; disconnecting safely",
							snapshotSequence, i );
						AsyncClient_StopAfterMalformedSnapshot();
						return;
					}
					userCmds[index][i].gameFrame = snapshotGameFrame + j;
					userCmds[index][i].duplicateCount = 0;
					last = &userCmds[index][i];
				}
				// clear all user commands after the ones just read from the snapshot
				for ( j = numUsercmds; j < MAX_USERCMD_BACKUP; j++ ) {
					index = ( snapshotGameFrame + j ) & ( MAX_USERCMD_BACKUP - 1 );
					userCmds[index][i].gameFrame = 0;
					userCmds[index][i].gameTime = 0;
				}
			}

			// if this is the first snapshot after a game init was received
			if ( clientState == CS_CONNECTED ) {
				gameTimeResidual = 0;
				clientState = CS_INGAME;
				assert( !sessLocal.GetActiveMenu( ) );
				if ( idAsyncNetwork::verbose.GetInteger() ) {
					common->Printf( "received first snapshot, gameInitId = %d, gameFrame %d gameTime %d\n", gameInitId, snapshotGameFrame, snapshotGameTime );
				}
			}

			// if the snapshot is newer than the clients current game time
			const int maximumPredictionMsec = AsyncClient_MaxPredictionMsec();
			if ( gameTime < snapshotGameTime || gameTime > snapshotGameTime + maximumPredictionMsec ) {
				gameFrame = snapshotGameFrame;
				gameTime = snapshotGameTime;
				gameTimeResidual = idMath::ClampInt( -maximumPredictionMsec, maximumPredictionMsec, gameTimeResidual );
				clientPredictTime = idMath::ClampInt( -maximumPredictionMsec, maximumPredictionMsec, clientPredictTime );
			}

			// adjust the client prediction time based on the snapshot time
			const int configuredPredictionMsec = AsyncClient_ConfiguredPredictionMsec( gameFrame );
			clientPrediction -= ( 1 - ( INTSIGNBITSET( aheadOfServer - configuredPredictionMsec ) << 1 ) );
			clientPrediction = idMath::ClampInt( configuredPredictionMsec, maximumPredictionMsec, clientPrediction );
			delta = gameTime - ( snapshotGameTime + clientPrediction );
			const std::int64_t adjustedPredictTime = static_cast<std::int64_t>( clientPredictTime ) -
				( delta / PREDICTION_FAST_ADJUST ) - ( 1 - ( INTSIGNBITSET( delta ) << 1 ) );
			clientPredictTime = adjustedPredictTime < -maximumPredictionMsec ? -maximumPredictionMsec :
				( adjustedPredictTime > maximumPredictionMsec ? maximumPredictionMsec :
					static_cast<int>( adjustedPredictTime ) );

			lastSnapshotTime = clientTime;

			if ( idAsyncNetwork::verbose.GetInteger() == 2 ) {
				common->Printf( "received snapshot, gameInitId = %d, gameFrame = %d, gameTime = %d\n", gameInitId, gameFrame, gameTime );
			}

			if ( numDuplicatedUsercmds && ( idAsyncNetwork::verbose.GetInteger() == 2 ) ) {
				common->Printf( "server duplicated %d user commands before snapshot %d\n", numDuplicatedUsercmds, snapshotGameFrame );
			}
			break;
		}
		default: {
			common->Printf( "unknown unreliable server message %d\n", id );
			break;
		}
	}
}

/*
==================
idAsyncClient::ProcessReliableMessagePure
==================
*/
void idAsyncClient::ProcessReliableMessagePure( const idBitMsg &msg ) {
	idBitMsg	outMsg;
	byte		msgBuf[ MAX_MESSAGE_SIZE ];
	int			inChecksums[ MAX_PURE_PAKS ];
	int			i;
	int			gamePakChecksum;
	int			serverGameInitId;

	session->SetGUI( NULL, NULL );

	serverGameInitId = msg.ReadLong();

	if ( serverGameInitId != gameInitId ) {
		common->DPrintf( "ignoring pure server checksum from an outdated gameInitId (%d)\n", serverGameInitId );
		return;
	}

	if ( !ValidatePureServerChecksums( serverAddress, msg ) ) {
		
		return;
	}

	if ( idAsyncNetwork::verbose.GetInteger() ) {
		common->Printf( "received new pure server info. ExecuteMapChange and report back\n" );
	}

	// it is now ok to load the next map with updated pure checksums
	sessLocal.ExecuteMapChange( true );

	// upon receiving our pure list, the server will send us SCS_INGAME and we'll start getting snapshots
	fileSystem->GetPureServerChecksums( inChecksums, -1, &gamePakChecksum );
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( CLIENT_RELIABLE_MESSAGE_PURE );

	outMsg.WriteLong( gameInitId );

	i = 0;
	while ( inChecksums[ i ] ) {
		outMsg.WriteLong( inChecksums[ i++ ] );
	}
	outMsg.WriteLong( 0 );
	outMsg.WriteLong( gamePakChecksum );

	// openQ4: backpressure, not a programming error - see SendUserInfoToServer.
	if ( !channel.SendReliableMessage( outMsg ) ) {
		common->Warning( "client->server reliable message queue overflowed, disconnecting" );
		// Deliberately NOT ClearReliableMessages(): that re-inits reliableSend to
		// sequence 1 while the server still expects the next sequence after the one
		// it last accepted, so every later message - including the disconnect notice
		// itself - would be silently discarded and the server would hold a ghost slot
		// until the client timeout.  The queue dies with the channel a moment later.
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
	}
}

/*
===============
idAsyncClient::ReadLocalizedServerString
===============
*/
void idAsyncClient::ReadLocalizedServerString( const idBitMsg &msg, char *out, int maxLen ) {
	if ( out == NULL || maxLen <= 0 ) {
		return;
	}
	msg.ReadString( out, maxLen );
	// look up localized string. if the message is not an #str_ format, we'll just get it back unchanged
	const idStr localized = common->GetLanguageDict()->GetString( out );
	idStr::Copynz( out, localized.c_str(), maxLen );
}

/*
==================
idAsyncClient::ProcessReliableServerMessages
==================
*/
void idAsyncClient::ProcessReliableServerMessages( void ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	byte		id;

	msg.Init( msgBuf, sizeof( msgBuf ) );

	while ( channel.GetReliableMessage( msg ) ) {
		id = msg.ReadByte();
		if ( msg.IsReadOverflowed() ) {
			common->Warning( "server sent an empty reliable message; disconnecting safely" );
			DisconnectFromServer();
			return;
		}
		switch( id ) {
			case SERVER_RELIABLE_MESSAGE_CLIENTINFO: {
				int clientNum;
				clientNum = msg.ReadByte();

				// openQ4: wire value, so it indexes nothing until it is known to be a
				// client slot.  userInfo only has MAX_ASYNC_CLIENTS entries.
				if ( msg.IsReadOverflowed() || clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS ) {
					common->Warning( "SERVER_RELIABLE_MESSAGE_CLIENTINFO: bad client number %d, ignored", clientNum );
					DisconnectFromServer();
					return;
				}

				idDict &info = sessLocal.mapSpawnData.userInfo[ clientNum ];
				bool haveBase = ( msg.ReadBits( 1 ) != 0 );
				if ( msg.IsReadOverflowed() ) {
					common->Warning( "SERVER_RELIABLE_MESSAGE_CLIENTINFO: truncated base flag" );
					DisconnectFromServer();
					return;
				}

#if ID_CLIENTINFO_TAGS
				int checksum = info.Checksum();
				int srv_checksum = msg.ReadLong();
				if ( checksum != srv_checksum ) {
					common->DPrintf( "SERVER_RELIABLE_MESSAGE_CLIENTINFO %d (haveBase: %s): != checksums srv: 0x%x local: 0x%x\n", clientNum, haveBase ? "true" : "false", checksum, srv_checksum );
					info.Print();
				} else {
					common->DPrintf( "SERVER_RELIABLE_MESSAGE_CLIENTINFO %d (haveBase: %s): checksums ok 0x%x\n", clientNum, haveBase ? "true" : "false", checksum );
				}
#endif

				if ( haveBase ) {
					msg.ReadDeltaDict( info, &info );
				} else {
					msg.ReadDeltaDict( info, NULL );
				}
				if ( msg.IsReadOverflowed() ) {
					common->Warning( "SERVER_RELIABLE_MESSAGE_CLIENTINFO: malformed userinfo dictionary" );
					DisconnectFromServer();
					return;
				}

				// server forces us to a different userinfo
				if ( clientNum == idAsyncClient::clientNum ) {
					common->DPrintf( "local user info modified by server\n" );
					cvarSystem->SetCVarsFromDictByFlags( info, CVAR_USERINFO );
					cvarSystem->ClearModifiedFlags( CVAR_USERINFO ); // don't emit back
				}
				game->SetUserInfo( clientNum, info, true );
				break;
			}
			case SERVER_RELIABLE_MESSAGE_SYNCEDCVARS: {
				idDict &info = sessLocal.mapSpawnData.syncedCVars;
				msg.ReadDeltaDict( info, &info );
				if ( msg.IsReadOverflowed() ) {
					common->Warning( "SERVER_RELIABLE_MESSAGE_SYNCEDCVARS: malformed CVar dictionary" );
					DisconnectFromServer();
					return;
				}
				cvarSystem->SetCVarsFromDictByFlags( info, CVAR_NETWORKSYNC );
				if ( !idAsyncNetwork::AreCheatsEnabled() ) {
					cvarSystem->ResetFlaggedVariables( CVAR_CHEAT );
				}
				break;
			}
			case SERVER_RELIABLE_MESSAGE_PRINT: {
				char string[MAX_STRING_CHARS];
				msg.ReadString( string, MAX_STRING_CHARS );
				common->Printf( "%s\n", string );
				break;
			}
			case SERVER_RELIABLE_MESSAGE_DISCONNECT: {
				int clientNum;
				char string[MAX_STRING_CHARS];
				clientNum = msg.ReadLong( );
				ReadLocalizedServerString( msg, string, MAX_STRING_CHARS );
				// openQ4: wire value - see SERVER_RELIABLE_MESSAGE_CLIENTINFO above.
				if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS ) {
					common->Warning( "SERVER_RELIABLE_MESSAGE_DISCONNECT: bad client number %d, ignored", clientNum );
					break;
				}
				if ( clientNum == idAsyncClient::clientNum ) {
					// Server-directed disconnects bypass the console disconnect command.
					// Restore any Arena transaction before tearing down its listen session.
					arenaCampaign.AbortMatch();
					session->Stop();
					session->MessageBox( MSG_OK, string, common->GetLanguageDict()->GetString ( "#str_04319" ), true );
					session->StartMenu();
				} else {
					common->Printf( "client %d %s\n", clientNum, string );
					idAsyncNetwork::ShowClientDisconnectMessage(
						sessLocal.mapSpawnData.userInfo[ clientNum ].GetString( "ui_name" ), string );
					sessLocal.mapSpawnData.userInfo[ clientNum ].Clear();
				}
				break;
			}
			case SERVER_RELIABLE_MESSAGE_APPLYSNAPSHOT: {
				int sequence;
				sequence = msg.ReadLong();
				if ( !game->ClientApplySnapshot( clientNum, sequence ) ) {
					session->Stop();
					common->Error( "couldn't apply snapshot %d", sequence );
				}
				break;
			}
			case SERVER_RELIABLE_MESSAGE_PURE: {
				ProcessReliableMessagePure( msg );
				break;
			}
			case SERVER_RELIABLE_MESSAGE_RELOAD: {
				if ( idAsyncNetwork::verbose.GetBool() ) {
					common->Printf( "got MESSAGE_RELOAD from server\n" );
				}
				// simply reconnect, so that if the server restarts in pure mode we can get the right list and avoid spurious reloads
				cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "reconnect\n" );
				break;
			}
			case SERVER_RELIABLE_MESSAGE_ENTERGAME: {
				SendUserInfoToServer();
				game->SetUserInfo( clientNum, sessLocal.mapSpawnData.userInfo[ clientNum ], true );
				cvarSystem->ClearModifiedFlags( CVAR_USERINFO );
				break;
			}
			default: {
				// pass reliable message on to game code
				game->ClientProcessReliableMessage( clientNum, msg );
				break;
			}
		}
	}
}

/*
==================
idAsyncClient::ProcessChallengeResponseMessage
==================
*/
void idAsyncClient::ProcessChallengeResponseMessage( const netadr_t from, const idBitMsg &msg ) {	
	char serverGame[ MAX_STRING_CHARS ], serverGameBase[ MAX_STRING_CHARS ];

	if ( clientState != CS_CHALLENGING ) {
		common->Printf( "Unwanted challenge response received.\n" );
		return;
	}

	serverChallenge = msg.ReadLong();
	serverId = msg.ReadShort();
	msg.ReadString( serverGameBase, MAX_STRING_CHARS );
	msg.ReadString( serverGame, MAX_STRING_CHARS );

	// the server is running a different game... we need to reload in the correct fs_game
	// even pure pak checks would fail if we didn't, as there are files we may not even see atm
	// NOTE: we could read the pure list from the server at the same time and set it up for the restart
	// ( if the client can restart directly with the right pak order, then we avoid an extra reloadEngine later.. )
	if ( idStr::Icmp( cvarSystem->GetCVarString( "fs_game_base" ), serverGameBase ) ||
		idStr::Icmp( cvarSystem->GetCVarString( "fs_game" ), serverGame ) ) {
		// bug #189 - if the server is running ROE and ROE is not locally installed, refuse to connect or we might crash
		if ( !fileSystem->HasD3XP() && ( !idStr::Icmp( serverGameBase, "d3xp" ) || !idStr::Icmp( serverGame, "d3xp" ) ) ) {
			common->Printf( "The server is running an expansion pack that is not installed on this client. Aborting the connection..\n" );
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
			return;
		}

		idStr modReason;
		idModInfo modInfo;
		if ( serverGameBase[ 0 ] &&
			 idStr::Icmp( serverGameBase, BASE_GAMEDIR ) &&
			 !fileSystem->GetModInfo( serverGameBase, modInfo, &modReason ) ) {
			common->Printf( "The server requires base mod '%s', but it is not runnable on this client: %s\n", serverGameBase, modReason.c_str() );
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
			return;
		}
		if ( serverGame[ 0 ] &&
			 idStr::Icmp( serverGame, BASE_GAMEDIR ) &&
			 !fileSystem->GetModInfo( serverGame, modInfo, &modReason ) ) {
			common->Printf( "The server requires mod '%s', but it is not runnable on this client: %s\n", serverGame, modReason.c_str() );
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
			return;
		}

		common->Printf( "The server is running a different mod (%s-%s). Restarting..\n", serverGameBase, serverGame );
		cvarSystem->SetCVarString( "fs_game_base", serverGameBase );
		cvarSystem->SetCVarString( "fs_game", serverGame );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "reloadEngine" );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "reconnect\n" );
		return;
	}

	common->DPrintf( "received connection challenge response from %s\n", Sys_NetAdrToString( from ) );

	// start sending connect packets instead of challenge request packets
	clientState = CS_CONNECTING;
	lastConnectTime = -9999;

	// take this address as the new server address.  This allows
	// a server proxy to hand off connections to multiple servers
	serverAddress = from;
}

/*
==================
idAsyncClient::ProcessConnectResponseMessage
==================
*/
void idAsyncClient::ProcessConnectResponseMessage( const netadr_t from, const idBitMsg &msg ) {
	int serverGameInitId, serverGameFrame, serverGameTime;
	idDict serverSI;

	if ( clientState >= CS_CONNECTED ) {
		common->Printf( "Duplicate connect received.\n" );
		return;
	}
	if ( clientState != CS_CONNECTING ) {
		common->Printf( "Connect response packet while not connecting.\n" );
		return;
	}
	if ( !Sys_CompareNetAdrBase( from, serverAddress ) ) {
		common->Printf( "Connect response from a different server.\n" );
		common->Printf( "%s should have been %s\n", Sys_NetAdrToString( from ), Sys_NetAdrToString( serverAddress ) );
		return;
	}

	if ( msg.GetRemainingReadBits() < 32 + 32 + 32 + 32 ) {
		common->Warning( "server sent a truncated connect response - aborting the connection" );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
		return;
	}
	const int serverClientNum = msg.ReadLong();
	serverGameInitId = msg.ReadLong();
	serverGameFrame = msg.ReadLong();
	serverGameTime = msg.ReadLong();
	msg.ReadDeltaDict( serverSI, NULL );

	// openQ4: this wire value ends up indexing userInfo and userCmds for the whole
	// session, so refuse the connection outright rather than accept a bad slot.
	if ( msg.IsReadOverflowed() || serverClientNum < 0 || serverClientNum >= MAX_ASYNC_CLIENTS ||
		 !AsyncClient_ValidNetworkTiming( serverGameFrame, serverGameTime ) ) {
		common->Warning( "server sent a malformed connect response - aborting the connection" );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
		return;
	}
	common->Printf( "received connect response from %s\n", Sys_NetAdrToString( from ) );

	channel.Init( from, clientId );
	clientNum = serverClientNum;
	clientState = CS_CONNECTED;
	lastPacketTime = -9999;

	InitGame( serverGameInitId, serverGameFrame, serverGameTime, serverSI );

	// load map
	session->SetGUI( NULL, NULL );
	sessLocal.ExecuteMapChange();

	const std::uint32_t connectElapsed = AsyncClient_Elapsed( clientTime, lastConnectTime );
	const std::uint32_t maximumPrediction = static_cast<std::uint32_t>( AsyncClient_MaxPredictionMsec() );
	clientPredictTime = clientPrediction = static_cast<int>(
		connectElapsed < maximumPrediction ? connectElapsed : maximumPrediction );
}

/*
==================
idAsyncClient::ProcessDisconnectMessage
==================
*/
void idAsyncClient::ProcessDisconnectMessage( const netadr_t from, const idBitMsg &msg ) {
	if ( clientState == CS_DISCONNECTED ) {
		common->Printf( "Disconnect packet while not connected.\n" );
		return;
	}
	if ( !AsyncClient_SameEndpoint( from, serverAddress ) ) {
		common->Printf( "Disconnect packet from unknown server.\n" );
		return;
	}
	arenaCampaign.AbortMatch();
	session->Stop();
	session->MessageBox( MSG_OK, common->GetLanguageDict()->GetString ( "#str_04320" ), NULL, true );
	session->StartMenu();
}

/*
==================
idAsyncClient::ProcessInfoResponseMessage
==================
*/
void idAsyncClient::ProcessInfoResponseMessage( const netadr_t from, const idBitMsg &msg ) {
	int i, protocol, index;
	networkServer_t serverInfo;
	bool verbose = false;

	if ( from.type == NA_LOOPBACK || cvarSystem->GetCVarBool( "developer" ) ) {
		verbose = true;
	}

	serverInfo.clients = 0;
	serverInfo.adr = from;
	serverInfo.challenge = msg.ReadLong();			// challenge
	protocol = msg.ReadLong();
	if ( protocol != ASYNC_PROTOCOL_VERSION ) {
		common->Printf( "server %s ignored - protocol %d.%d, expected %d.%d\n", Sys_NetAdrToString( serverInfo.adr ), protocol >> 16, protocol & 0xffff, ASYNC_PROTOCOL_MAJOR, ASYNC_PROTOCOL_MINOR );
		return;
	}
	msg.ReadDeltaDict( serverInfo.serverInfo, NULL );

	if ( verbose ) {
		common->Printf( "server IP = %s\n", Sys_NetAdrToString( serverInfo.adr ) );
		serverInfo.serverInfo.Print();
	}
	for ( i = msg.ReadByte(); i < MAX_ASYNC_CLIENTS; i = msg.ReadByte() ) {
		if ( serverInfo.clients >= MAX_ASYNC_CLIENTS ) {
			common->Printf( "server %s ignored - too many clients in info response\n", Sys_NetAdrToString( serverInfo.adr ) );
			return;
		}
		serverInfo.pings[ serverInfo.clients ] = msg.ReadShort();
		serverInfo.rate[ serverInfo.clients ] = msg.ReadLong();
		msg.ReadString( serverInfo.nickname[ serverInfo.clients ], MAX_NICKLEN );
		if ( verbose ) {
			common->Printf( "client %2d: %s, ping = %d, rate = %d\n", i, serverInfo.nickname[ serverInfo.clients ], serverInfo.pings[ serverInfo.clients ], serverInfo.rate[ serverInfo.clients ] );
		}
		serverInfo.clients++;
	}
	serverInfo.OSMask = msg.ReadLong();
	if ( msg.IsReadOverflowed() ) {
		common->DPrintf( "server %s ignored - truncated info response\n", Sys_NetAdrToString( from ) );
		return;
	}
	index = serverList.InfoResponse( serverInfo );

	common->Printf( "%d: server %s - protocol %d.%d - %s\n", index, Sys_NetAdrToString( serverInfo.adr ), protocol >> 16, protocol & 0xffff, serverInfo.serverInfo.GetString( "si_name" ) );
}

/*
==================
idAsyncClient::ProcessPrintMessage
==================
*/
void idAsyncClient::ProcessPrintMessage( const netadr_t from, const idBitMsg &msg ) {
	char		string[ MAX_STRING_CHARS ];
	int			opcode;
	int			game_opcode = ALLOW_YES;
	const char	*retpass;

	opcode = msg.ReadLong();
	if ( opcode == SERVER_PRINT_GAMEDENY ) {
		game_opcode = msg.ReadLong();
	}
	ReadLocalizedServerString( msg, string, MAX_STRING_CHARS );
	common->Printf( "%s\n", string );
	guiNetMenu->SetStateString( "status", string );
	if ( opcode == SERVER_PRINT_GAMEDENY ) {
		if ( game_opcode == ALLOW_BADPASS ) {
			retpass = session->MessageBox( MSG_PROMPT, common->GetLanguageDict()->GetString ( "#str_04321" ), string, true, "passprompt_ok" );
			ClearPendingPackets();
			guiNetMenu->SetStateString( "status",  common->GetLanguageDict()->GetString ( "#str_04322" ));
			if ( retpass ) {
				// #790
				cvarSystem->SetCVarString( "password", "" );
				cvarSystem->SetCVarString( "password", retpass );			
			} else {
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
			}
		} else if ( game_opcode == ALLOW_NO ) {
			session->MessageBox( MSG_OK, string, common->GetLanguageDict()->GetString ( "#str_04323" ), true );
			ClearPendingPackets();
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
		}
		// ALLOW_NOTYET just keeps running as usual. The GUI has an abort button
	} else if ( opcode == SERVER_PRINT_BADCHALLENGE && clientState >= CS_CONNECTING ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "reconnect" );
	}
}

/*
==================
idAsyncClient::ProcessServersListMessage
==================
*/
void idAsyncClient::ProcessServersListMessage( const netadr_t from, const idBitMsg &msg ) {
	if ( !Sys_CompareNetAdrBase( idAsyncNetwork::GetMasterAddress(), from ) ) {
		common->DPrintf( "received a server list from %s - not a valid master\n", Sys_NetAdrToString( from ) );
		return;
	}
	while ( msg.GetRemaingData() >= 6 ) {
		int a,b,c,d;
		a = msg.ReadByte(); b = msg.ReadByte(); c = msg.ReadByte(); d = msg.ReadByte();
		serverList.AddServer( serverList.Num(), va( "%i.%i.%i.%i:%i", a, b, c, d, msg.ReadUShort() ) );
	}
	if ( msg.GetRemaingData() != 0 ) {
		common->DPrintf( "received a malformed server list from %s - trailing address data ignored\n", Sys_NetAdrToString( from ) );
	}
}

/*
==================
idAsyncClient::ProcessServersListExtMessage

The legacy "servers" reply is an untagged stream of four octet IPv4 records, so
it cannot carry an IPv6 address without desynchronizing every reader. This
extended reply tags each record with the separator convention shared by the
dpmaster family of master servers: '\' introduces a four byte IPv4 record and
'/' a sixteen byte IPv6 record, each followed by a two byte port in network
byte order. Note that idBitMsg::ReadUShort is little-endian, so the port is
assembled from explicit bytes rather than read as a short.
==================
*/
void idAsyncClient::ProcessServersListExtMessage( const netadr_t from, const idBitMsg &msg ) {
	if ( !Sys_CompareNetAdrBase( idAsyncNetwork::GetMasterAddress(), from ) ) {
		common->DPrintf( "received an extended server list from %s - not a valid master\n", Sys_NetAdrToString( from ) );
		return;
	}

	bool malformed = false;
	while ( msg.GetRemaingData() > 0 ) {
		const int separator = msg.ReadByte();
		int addressBytes;
		if ( separator == '\\' ) {
			addressBytes = 4;
		} else if ( separator == '/' ) {
			addressBytes = 16;
		} else {
			// dpmaster terminates the payload with "EOT\0\0\0"; anything else
			// here means the stream is no longer record aligned, so stop rather
			// than reinterpret the remainder as addresses.
			malformed = ( separator != 'E' );
			break;
		}

		// The port always follows the address, so a record that does not fit
		// the remaining payload is truncated and must not be read.
		if ( msg.GetRemaingData() < addressBytes + 2 ) {
			malformed = true;
			break;
		}

		netadr_t server;
		memset( &server, 0, sizeof( server ) );
		if ( addressBytes == 4 ) {
			for ( int octet = 0; octet < 4; octet++ ) {
				server.ip[octet] = msg.ReadByte();
			}
			server.type = NA_IP;
		} else {
			for ( int octet = 0; octet < 16; octet++ ) {
				server.ip6[octet] = msg.ReadByte();
			}
			server.type = NA_IP6;
		}
		const int portHigh = msg.ReadByte();
		const int portLow = msg.ReadByte();
		server.port = static_cast<unsigned short>( ( portHigh << 8 ) | portLow );

		// A record with no port cannot be pinged, and the unspecified address
		// is the master's own padding rather than a reachable server.
		if ( server.port == 0 ) {
			continue;
		}
		if ( server.type == NA_IP6 && idNetworkEndpoint::IsIPv6Unspecified( server.ip6 ) ) {
			continue;
		}

		serverList.AddServer( serverList.Num(), Sys_NetAdrToString( server ) );
	}

	if ( malformed ) {
		common->DPrintf( "received a malformed extended server list from %s - trailing address data ignored\n", Sys_NetAdrToString( from ) );
	}
}

/*
==================
idAsyncClient::ProcessAuthKeyMessage
==================
*/
void idAsyncClient::ProcessAuthKeyMessage( const netadr_t from, const idBitMsg &msg ) {
	(void)from;
	(void)msg;
	common->DPrintf( "ignoring legacy authKey message (CD key auth disabled)\n" );
}

/*
==================
idAsyncClient::ProcessVersionMessage
==================
*/
void idAsyncClient::ProcessVersionMessage( const netadr_t from, const idBitMsg &msg ) {
	char ignoredNetworkField[ MAX_STRING_CHARS ];
	(void)from;

	if ( updateState != UPDATE_SENT ) {
		common->Printf( "ProcessVersionMessage: version reply, != UPDATE_SENT\n" );
		return;
	}

	// Consume the complete legacy payload so old master servers remain wire
	// compatible. The message, direct-download flag, URL, MIME action, and
	// fallback URL are intentionally ignored: an unauthenticated datagram must
	// never supply instructions or choose what the client downloads, opens, or
	// executes.
	msg.ReadString( ignoredNetworkField, sizeof( ignoredNetworkField ) );
	(void)msg.ReadByte();
	msg.ReadString( ignoredNetworkField, sizeof( ignoredNetworkField ) );
	(void)msg.ReadByte();
	msg.ReadString( ignoredNetworkField, sizeof( ignoredNetworkField ) );

	updateMSG = common->GetLanguageDict()->GetString( "#str_104330" );
	common->Printf( "A new version is available\n" );
	updateState = UPDATE_READY;
}

/*
==================
idAsyncClient::ValidatePureServerChecksums
==================
*/
bool idAsyncClient::ValidatePureServerChecksums( const netadr_t from, const idBitMsg &msg ) {
	int			i, numChecksums, numMissingChecksums;
	int			inChecksums[ MAX_PURE_PAKS ];
	int			inGamePakChecksum;
	int			missingChecksums[ MAX_PURE_PAKS ];
	int			missingGamePakChecksum;
	idBitMsg	dlmsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	// read checksums
	// pak checksums, in a 0-terminated list
	numChecksums = 0;
	do {
		i = msg.ReadLong( );
		inChecksums[ numChecksums++ ] = i;
		// just to make sure a broken message doesn't crash us
		if ( numChecksums >= MAX_PURE_PAKS ) {
			common->Warning( "MAX_PURE_PAKS ( %d ) exceeded in idAsyncClient::ProcessPureMessage\n", MAX_PURE_PAKS );
			return false;
		}
	} while ( i );
	inChecksums[ numChecksums ] = 0;
	inGamePakChecksum = msg.ReadLong();

	fsPureReply_t reply = fileSystem->SetPureServerChecksums( inChecksums, inGamePakChecksum, missingChecksums, &missingGamePakChecksum );
	switch ( reply ) {
		case PURE_RESTART:
			// need to restart the filesystem with a different pure configuration
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
			// restart with the right FS configuration and get back to the server
			clientState = CS_PURERESTART;	
			fileSystem->SetRestartChecksums( inChecksums, inGamePakChecksum );
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "reloadEngine" );
			return false;
		case PURE_MISSING: {

			idStr checksums;

			i = 0;
			while ( missingChecksums[ i ] ) {
				checksums += va( "0x%x ", missingChecksums[ i++ ] );
			}
			numMissingChecksums = i;

			if ( idAsyncNetwork::clientDownload.GetInteger() == 0 ) {
				// never any downloads
				idStr message = va( common->GetLanguageDict()->GetString( "#str_07210" ), Sys_NetAdrToString( from ) );

				if ( numMissingChecksums > 0 ) {
					message += va( common->GetLanguageDict()->GetString( "#str_06751" ), numMissingChecksums, checksums.c_str() );
				}
				if ( missingGamePakChecksum ) {
					message += va( common->GetLanguageDict()->GetString( "#str_06750" ), missingGamePakChecksum );
				}

				common->Printf( "%s", message.c_str() );
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
				session->MessageBox( MSG_OK, message, common->GetLanguageDict()->GetString( "#str_06735" ), true );
			} else {
				if ( clientState >= CS_CONNECTED ) {
					// we are already connected, reconnect to negociate the paks in connectionless mode
					cmdSystem->BufferCommandText( CMD_EXEC_NOW, "reconnect" );
					return false;
				}
				// ask the server to send back download info
				common->DPrintf( "missing %d paks: %s\n", numMissingChecksums + ( missingGamePakChecksum ? 1 : 0 ), checksums.c_str() );
				if ( missingGamePakChecksum ) {
					common->DPrintf( "game code pak: 0x%x\n", missingGamePakChecksum );
				}
				// store the requested downloads
				if ( GetDownloadRequest( missingChecksums, numMissingChecksums, missingGamePakChecksum ) == -1 ) {
					common->Warning( "OS secure random unavailable; download request cancelled" );
					return false;
				}
				// build the download request message
				// NOTE: in a specific function?
				dlmsg.Init( msgBuf, sizeof( msgBuf ) );
				dlmsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
				dlmsg.WriteString( "downloadRequest" );
				dlmsg.WriteLong( serverChallenge );
				dlmsg.WriteShort( clientId );
				// used to make sure the server replies to the same download request
				dlmsg.WriteLong( dlRequest );
				// special case the code pak - if we have a 0 checksum then we don't need to download it
				dlmsg.WriteLong( missingGamePakChecksum );
				// 0-terminated list of missing paks
				i = 0;
				while ( missingChecksums[ i ] ) {
					dlmsg.WriteLong( missingChecksums[ i++ ] );
				}
				dlmsg.WriteLong( 0 );
				clientPort.SendPacket( from, dlmsg.GetData(), dlmsg.GetSize() );
			}

			return false;
		}
		case PURE_NODLL:
			common->Printf( common->GetLanguageDict()->GetString( "#str_07211" ), Sys_NetAdrToString( from ) );
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
			return false;
		default:
			return true;
	}
	return true;
}

/*
==================
idAsyncClient::ProcessPureMessage
==================
*/
void idAsyncClient::ProcessPureMessage( const netadr_t from, const idBitMsg &msg ) {
	idBitMsg	outMsg;
	byte		msgBuf[ MAX_MESSAGE_SIZE ];
	int			i;
	int			inChecksums[ MAX_PURE_PAKS ];
	int			gamePakChecksum;

	if ( clientState != CS_CONNECTING ) {
		common->Printf( "clientState != CS_CONNECTING, pure msg ignored\n" );
		return;
	}

	if ( !ValidatePureServerChecksums( from, msg ) ) {
		return;
	}

	fileSystem->GetPureServerChecksums( inChecksums, -1, &gamePakChecksum );
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "pureClient" );
	outMsg.WriteLong( serverChallenge );
	outMsg.WriteShort( clientId );
	i = 0;
	while ( inChecksums[ i ] ) {
		outMsg.WriteLong( inChecksums[ i++ ] );
	}
	outMsg.WriteLong( 0 );
	outMsg.WriteLong( gamePakChecksum );
	clientPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
}

/*
==================
idAsyncClient::ConnectionlessMessage
==================
*/
void idAsyncClient::ConnectionlessMessage( const netadr_t from, const idBitMsg &msg ) {
	char string[MAX_STRING_CHARS*2];  // M. Quinn - Even Balance - PB packets can go beyond 1024

	msg.ReadString( string, sizeof( string ) );

	// info response from a server, are accepted from any source
	if ( idStr::Icmp( string, "infoResponse" ) == 0 ) {
		ProcessInfoResponseMessage( from, msg );
		return;
	}

	// from master server:
	if ( Sys_CompareNetAdrBase( from, idAsyncNetwork::GetMasterAddress( ) ) ) {
		// server list
		if ( idStr::Icmp( string, "servers" ) == 0 ) {
			ProcessServersListMessage( from, msg );
			return;
		}

		// extended, address-family tagged server list
		if ( idStr::Icmp( string, "serversExt" ) == 0 ) {
			ProcessServersListExtMessage( from, msg );
			return;
		}

		if ( idStr::Icmp( string, "authKey" ) == 0 ) {
			ProcessAuthKeyMessage( from, msg );
			return;
		}

		if ( idStr::Icmp( string, "newVersion" ) == 0 ) {
			ProcessVersionMessage( from, msg );
			return;
		}
	}

	const bool fromCurrentServer = active && AsyncClient_SameEndpoint( from, serverAddress );
	const bool fromPendingRcon = rcon2Request.state != RCON_REPLY_NONE &&
		AsyncClient_Elapsed( realTime, rcon2Request.startTime ) <= RCON2_CLIENT_TIMEOUT_MSEC &&
		AsyncClient_SameEndpoint( from, rcon2Request.address );
	const bool fromPendingRconOutput = fromPendingRcon &&
		( rcon2Request.state == RCON_REPLY_OUTPUT || rcon2Request.state == RCON_REPLY_LEGACY );

	// Remote-console opcodes have their own exact-endpoint capability. A
	// pending rcon request must not authorize this endpoint to inject game
	// control messages, and the game server must not manufacture rcon replies
	// when no matching request is pending.
	if ( idStr::Icmp( string, "rcon2ChallengeResponse" ) == 0 ) {
		if ( !fromPendingRcon ) {
			common->DPrintf( "got rcon2 challenge response from bad source: %s\n", Sys_NetAdrToString( from ) );
			return;
		}
		ProcessRemoteConsole2ChallengeResponse( from, msg );
		return;
	}
	if ( idStr::Icmp( string, "rcon2Complete" ) == 0 ) {
		if ( !fromPendingRcon ) {
			common->DPrintf( "got rcon2 completion from bad source: %s\n", Sys_NetAdrToString( from ) );
			return;
		}
		ProcessRemoteConsole2Complete( from, msg );
		return;
	}
	if ( idStr::Icmp( string, "print" ) == 0 ) {
		// A challenge request has not authorized an output window yet. Secure
		// rcon2 opens it only after the proof is sent; legacy mode keeps its
		// explicitly insecure bounded window for compatibility. Ordinary game
		// prints remain tied to the exact current-server endpoint.
		if ( !fromCurrentServer && !fromPendingRconOutput ) {
			common->DPrintf( "got print from bad source: %s\n", Sys_NetAdrToString( from ) );
			return;
		}
		ProcessPrintMessage( from, msg );
		return;
	}

	// Everything below is game-session control and therefore requires the
	// exact current server endpoint. Merely owning an rcon reply window is not
	// sufficient authority.
	if ( !fromCurrentServer ) {
		common->DPrintf( "got game control message '%s' from bad source: %s\n", string, Sys_NetAdrToString( from ) );
		return;
	}

	// challenge response from the server we are connecting to
	if ( idStr::Icmp( string, "challengeResponse" ) == 0 ) {
		ProcessChallengeResponseMessage( from, msg );
		return;
	}

	// connect response from the server we are connecting to
	if ( idStr::Icmp( string, "connectResponse" ) == 0 ) {
		ProcessConnectResponseMessage( from, msg );
		return;
	}

	// a disconnect message from the server, which will happen if the server
	// dropped the connection but is still getting packets from this client
	if ( idStr::Icmp( string, "disconnect" ) == 0 ) {
		ProcessDisconnectMessage( from, msg );
		return;
	}

	// server pure list
	if ( idStr::Icmp( string, "pureServer" ) == 0 ) {
		ProcessPureMessage( from, msg );
		return;
	}

	if ( idStr::Icmp( string, "downloadInfo" ) == 0 ) {
		ProcessDownloadInfoMessage( from, msg );
		return;
	}

	common->DPrintf( "ignored message from %s: %s\n", Sys_NetAdrToString( from ), string );
}

/*
=================
idAsyncClient::ProcessMessage
=================
*/
void idAsyncClient::ProcessMessage( const netadr_t from, idBitMsg &msg ) {
	int id;

	id = msg.ReadShort();

	// check for a connectionless packet
	if ( id == CONNECTIONLESS_MESSAGE_ID ) {
		ConnectionlessMessage( from, msg );
		return;
	}

	if ( clientState < CS_CONNECTED ) {
		return;		// can't be a valid sequenced packet
	}

	if ( msg.GetRemaingData() < 4 ) {
		common->DPrintf( "%s: tiny packet\n", Sys_NetAdrToString( from ) );
		return;
	}

	// is this a packet from the server
	if ( !Sys_CompareNetAdrBase( from, channel.GetRemoteAddress() ) || id != serverId ) {
		common->DPrintf( "%s: sequenced server packet without connection\n", Sys_NetAdrToString( from ) );
		return;
	}

	if ( !channel.Process( from, clientTime, msg, serverMessageSequence ) ) {
		return;		// out of order, duplicated, fragment, etc.
	}

	lastPacketTime = clientTime;
	ProcessReliableServerMessages();
	ProcessUnreliableServerMessage( msg );
}

/*
==================
idAsyncClient::SetupConnection
==================
*/
void idAsyncClient::SetupConnection( void ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( clientTime - lastConnectTime < SETUP_CONNECTION_RESEND_TIME ) {
		return;
	}

	if ( clientState == CS_CHALLENGING ) {
		common->Printf( "sending challenge to %s\n", Sys_NetAdrToString( serverAddress ) );
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
		msg.WriteString( "challenge" );
		msg.WriteLong( clientId );
		clientPort.SendPacket( serverAddress, msg.GetData(), msg.GetSize() );
	} else if ( clientState == CS_CONNECTING ) {
		common->DPrintf( "sending authenticated connect request to %s\n", Sys_NetAdrToString( serverAddress ) );
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
		msg.WriteString( "connect" );
		msg.WriteLong( ASYNC_PROTOCOL_VERSION );
#if ID_FAKE_PURE
		// fake win32 OS - might need to adapt depending on the case
		msg.WriteShort( 0 );
#else
		msg.WriteShort( BUILD_OS_ID );
#endif
		msg.WriteLong( clientDataChecksum );
		msg.WriteLong( serverChallenge );
		msg.WriteShort( clientId );
		msg.WriteLong( cvarSystem->GetCVarInteger( "net_clientMaxRate" ) );
		msg.WriteString( cvarSystem->GetCVarString( "com_guid" ) );
		msg.WriteString( cvarSystem->GetCVarString( "password" ), -1 );
		// do not make the protocol depend on PB
		msg.WriteShort( 0 );
		clientPort.SendPacket( serverAddress, msg.GetData(), msg.GetSize() );
		
		if ( idAsyncNetwork::LANServer.GetBool() ) {
			common->Printf( "net_LANServer is set, connecting in LAN mode\n" );
		}
	} else {
		return;
	}

	lastConnectTime = clientTime;
}

/*
==================
idAsyncClient::SendReliableGameMessage
==================
*/
void idAsyncClient::SendReliableGameMessage( const idBitMsg &msg ) {
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		return;
	}
	idBitMsg	outMsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( clientState < CS_INGAME ) {
		return;
	}

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( CLIENT_RELIABLE_MESSAGE_GAME );
	outMsg.WriteData( msg.GetData(), msg.GetSize() );
	// openQ4: backpressure, not a programming error - see SendUserInfoToServer.  The
	// disconnect is buffered rather than immediate because this runs from game code.
	if ( !channel.SendReliableMessage( outMsg ) ) {
		common->Warning( "client->server reliable message queue overflowed, disconnecting" );
		// Deliberately NOT ClearReliableMessages(): that re-inits reliableSend to
		// sequence 1 while the server still expects the next sequence after the one
		// it last accepted, so every later message - including the disconnect notice
		// itself - would be silently discarded and the server would hold a ghost slot
		// until the client timeout.  The queue dies with the channel a moment later.
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
	}
}

/*
==================
idAsyncClient::Idle
==================
*/
void idAsyncClient::Idle( void ) {
	// also need to read mouse for the connecting guis
	usercmdGen->GetDirectUsercmd();

	SendEmptyToServer();
}

/*
==================
idAsyncClient::UpdateTime
==================
*/
int idAsyncClient::UpdateTime( int clamp ) {
	int time, msec;

	time = Sys_Milliseconds();
	msec = idMath::ClampInt( 0, clamp, time - realTime );
	realTime = time;
	clientTime += msec;
	return msec;
}

/*
==================
idAsyncClient::RunFrame
==================
*/
void idAsyncClient::RunFrame( bool allowBlocking ) {
	int			msec, size;
	bool		newPacket;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	netadr_t	from;

	msec = UpdateTime( 100 );

	if ( !clientPort.GetPort() ) {
		return;
	}
	UpdateRemoteConsoleRequest();

	// handle ongoing pk4 downloads and patch downloads
	HandleDownloads();

	gameTimeResidual += msec;

	// spin in place processing incoming packets until enough time lapsed to run a new game frame
	do {

		do {
			const int nextGameFrameMsec = AsyncClient_NextGameFrameMsec( gameFrame );
			const int packetTimeout = allowBlocking ? ( nextGameFrameMsec - ( gameTimeResidual + clientPredictTime ) - 1 ) : -1;

			// Foreground high-refresh netplay polls instead of blocking so presentation can
			// continue between the authoritative 60 Hz client/server game frames.
			newPacket = clientPort.GetPacketBlocking( from, msgBuf, size, sizeof( msgBuf ), packetTimeout );
			if ( newPacket ) {
				msg.Init( msgBuf, sizeof( msgBuf ) );
				msg.SetSize( size );
				msg.BeginReading();
				ProcessMessage( from, msg );
			}

			msec = UpdateTime( 100 );
			gameTimeResidual += msec;

		} while( newPacket );

		if ( !allowBlocking ) {
			break;
		}

	} while( gameTimeResidual + clientPredictTime < AsyncClient_NextGameFrameMsec( gameFrame ) );

	// update server list
	serverList.RunFrame();

	if ( clientState == CS_DISCONNECTED ) {
		usercmdGen->GetDirectUsercmd();
		gameTimeResidual = AsyncClient_NextGameFrameMsec( gameFrame ) - 1;
		clientPredictTime = 0;
		return;
	}

	if ( clientState == CS_PURERESTART ) {
		clientState = CS_DISCONNECTED;
		Reconnect();
		gameTimeResidual = AsyncClient_NextGameFrameMsec( gameFrame ) - 1;
		clientPredictTime = 0;
		return;
	}

	// if not connected setup a connection
	if ( clientState < CS_CONNECTED ) {
		// also need to read mouse for the connecting guis
		usercmdGen->GetDirectUsercmd();
		SetupConnection();
		gameTimeResidual = AsyncClient_NextGameFrameMsec( gameFrame ) - 1;
		clientPredictTime = 0;
		return;
	}

	if ( CheckTimeout() ) {
		return;
	}

	// if not yet in the game send empty messages to keep data flowing through the channel
	if ( clientState < CS_INGAME ) {
		Idle();
		gameTimeResidual = 0;
		return;
	}

	// check for user info changes
	if ( cvarSystem->GetModifiedFlags() & CVAR_USERINFO ) {
		game->ThrottleUserInfo( );
		SendUserInfoToServer( );
		game->SetUserInfo( clientNum, sessLocal.mapSpawnData.userInfo[ clientNum ], true );
		cvarSystem->ClearModifiedFlags( CVAR_USERINFO );
	}

	// Quake 4 services client work once per presentation frame, including
	// reliable referee challenges and UI updates, even without a prediction tic.
	game->ClientRun();

	if ( gameTimeResidual + clientPredictTime >= AsyncClient_NextGameFrameMsec( gameFrame ) ) {
		lastFrameDelta = 0;
	}

	// generate user commands for the predicted time
	while ( gameTimeResidual + clientPredictTime >= AsyncClient_NextGameFrameMsec( gameFrame ) ) {
		const int nextGameFrameMsec = AsyncClient_NextGameFrameMsec( gameFrame );

		// send the user commands of this client to the server
		SendUsercmdsToServer();

		// update time
		gameFrame++;
		// openQ4: keep the client's game clock on the same ladder the game itself
		// uses (see idAsyncServer::RunFrame) so replicated timestamps compare
		// against a clock that means the same thing on both ends.
		gameTime = gameFrame * common->GetUserCmdMSec();
		gameTimeResidual -= nextGameFrameMsec;

		// run from the snapshot up to the local game frame
		while ( snapshotGameFrame < gameFrame ) {

			lastFrameDelta++;

			// duplicate usercmds for clients if no new ones are available
			DuplicateUsercmds( snapshotGameFrame, snapshotGameTime );

			// indicate the last prediction frame before a render
			bool lastPredictFrame = ( snapshotGameFrame + 1 >= gameFrame && gameTimeResidual + clientPredictTime < AsyncClient_NextGameFrameMsec( gameFrame ) );

			// run client prediction
			gameReturn_t ret = game->ClientPrediction( clientNum, userCmds[ snapshotGameFrame & ( MAX_USERCMD_BACKUP - 1 ) ], lastPredictFrame );

			idAsyncNetwork::ExecuteSessionCommand( ret.sessionCommand );

			snapshotGameFrame++;
			snapshotGameTime = snapshotGameFrame * common->GetUserCmdMSec();
		}
	}
	game->ClientEndFrame();
}

/*
==================
idAsyncClient::PacifierUpdate
==================
*/
void idAsyncClient::PacifierUpdate( void ) {
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		return;
	}
	if ( !IsActive() ) {
		return;
	}
	realTime = Sys_Milliseconds();
	SendEmptyToServer( false, true );
}

/*
==================
idAsyncClient::SendVersionCheck
==================
*/
void idAsyncClient::SendVersionCheck( bool fromMenu ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( updateState != UPDATE_NONE && !fromMenu ) {
		common->DPrintf( "up-to-date check was already performed\n" );
		return;
	}

	InitPort();
	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	msg.WriteString( "versionCheck" );
	msg.WriteLong( ASYNC_PROTOCOL_VERSION );
	msg.WriteShort( BUILD_OS_ID );
	msg.WriteString( cvarSystem->GetCVarString( "si_version" ) );
	// Retain the legacy field position without leaking the persistent client
	// identifier to the unauthenticated UDP update service.
	msg.WriteString( "" );
	clientPort.SendPacket( idAsyncNetwork::GetMasterAddress(), msg.GetData(), msg.GetSize() );

	common->DPrintf( "sent a version check request\n" );

	updateState = UPDATE_SENT;
	updateSentTime = clientTime;
	showUpdateMessage = fromMenu;
}

/*
==================
idAsyncClient::HandleDownloads
==================
*/
void idAsyncClient::HandleDownloads( void ) {

	if ( updateState == UPDATE_SENT && clientTime > updateSentTime + 2000 ) {
		// timing out on no reply
		updateState = UPDATE_DONE;
		if ( showUpdateMessage ) {
			session->MessageBox( MSG_OK, common->GetLanguageDict()->GetString ( "#str_104839" ), common->GetLanguageDict()->GetString ( "#str_104837" ), true );
			showUpdateMessage = false;
		}
		common->DPrintf( "No update available\n" );
	} else if ( backgroundDownload.completed ) {
		// only enter these if the download slot is free
		if ( updateState == UPDATE_READY ) {
			const bool openReleasePage = session->MessageBox( MSG_YESNO, updateMSG,
				common->GetLanguageDict()->GetString( "#str_04330" ), true, "yes" )[ 0 ] != '\0';
			updateState = UPDATE_DONE;
			showUpdateMessage = false;
			if ( openReleasePage ) {
				// This compile-time HTTPS destination is the only action a legacy
				// version reply may trigger. The network-provided URLs are inert.
				sys->OpenURL( PROJECT_RELEASES_URL, false );
			}
		} else if ( dlList.Num() ) {

			int numPaks = dlList.Num();
			int pakCount = 1;
			int progress_start, progress_end;
			currentDlSize = 0;

			do {

				if ( dlList[ 0 ].url[ 0 ] == '\0' ) {
					// ignore empty files
					dlList.RemoveIndex( 0 );
					continue;
				}
				common->Printf( "start download for %s\n", dlList[ 0 ].url.c_str() );

				idFile_Permanent *f = static_cast< idFile_Permanent *>( fileSystem->MakeTemporaryFile( ) );
				if ( !f ) {
					common->Warning( "could not create temporary file" );
					dlList.Clear();
					return;
				}

				backgroundDownload.completed = false;
				backgroundDownload.opcode = DLTYPE_URL;
				backgroundDownload.f = f;
				backgroundDownload.url.status = DL_WAIT;
				backgroundDownload.url.expectedSize = dlList[ 0 ].size;
				backgroundDownload.url.dlnow = 0;
				backgroundDownload.url.dltotal = 0;
				backgroundDownload.url.url = dlList[ 0 ].url;
				fileSystem->BackgroundDownload( &backgroundDownload );
				idStr dltitle;
				// "Downloading %s"
				const char *downloadFormat = common->GetLanguageDict()->GetString( "#str_07213" );
				const char *downloadPlaceholder = strstr( downloadFormat, "%s" );
				if ( downloadPlaceholder != NULL ) {
					dltitle.Append( downloadFormat, static_cast<int>( downloadPlaceholder - downloadFormat ) );
					dltitle += dlList[ 0 ].filename;
					dltitle += downloadPlaceholder + 2;
				} else {
					dltitle = downloadFormat;
					dltitle += " ";
					dltitle += dlList[ 0 ].filename;
				}
				if ( numPaks > 1 ) {
					dltitle += va( " (%d/%d)", pakCount, numPaks );
				}
				if ( totalDlSize ) {
					progress_start = (int)( (float)currentDlSize * 100.0f / (float)totalDlSize );
					progress_end = (int)( (float)( currentDlSize + dlList[ 0 ].size ) * 100.0f / (float)totalDlSize );
				} else {
					progress_start = 0;
					progress_end = 100;
				}
				session->DownloadProgressBox( &backgroundDownload, dltitle, progress_start, progress_end );
				if ( backgroundDownload.url.status == DL_DONE ) {				
					idFile		*saveas;
					const int	CHUNK_SIZE = 1024 * 1024;
					byte		*buf;
					int			remainlen;
					int			readlen;
					int			retlen;
					int			checksum;

					common->Printf( "file downloaded\n" );
					idStr finalPath = cvarSystem->GetCVarString( "fs_savepath" );
					finalPath.AppendPath( dlList[ 0 ].filename );
					idFile *existingDestination = fileSystem->OpenExplicitFileRead( finalPath );
					if ( existingDestination ) {
						fileSystem->CloseFile( existingDestination );
						common->Warning( "refusing to replace existing download destination '%s'", finalPath.c_str() );
						AsyncClient_CloseBackgroundDownloadFile( backgroundDownload );
						dlList.Clear();
						session->MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07215" ), common->GetLanguageDict()->GetString( "#str_07216" ), true );
						return;
					}
					fileSystem->CreateOSPath( finalPath );
					// do the final copy ourselves so we do by small chunks in case the file is big
					saveas = fileSystem->OpenExplicitFileWrite( finalPath );
					if ( !saveas ) {
						common->Warning( "could not open download destination '%s'", finalPath.c_str() );
						AsyncClient_CloseBackgroundDownloadFile( backgroundDownload );
						dlList.Clear();
						session->MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07215" ), common->GetLanguageDict()->GetString( "#str_07216" ), true );
						return;
					}
					buf = (byte*)Mem_Alloc( CHUNK_SIZE );
					f->Seek( 0, FS_SEEK_END );
					remainlen = f->Tell();
					f->Seek( 0, FS_SEEK_SET );
					while ( remainlen ) {
						readlen = Min( remainlen, CHUNK_SIZE );
						retlen = f->Read( buf, readlen );
						if ( retlen != readlen ) {
							common->FatalError( "short read %d of %d in idFileSystem::HandleDownload", retlen, readlen );
						}
						retlen = saveas->Write( buf, readlen );
						if ( retlen != readlen ) {
							common->FatalError( "short write %d of %d in idFileSystem::HandleDownload", retlen, readlen );
						}
						remainlen -= readlen;
					}
					AsyncClient_CloseBackgroundDownloadFile( backgroundDownload );
					fileSystem->CloseFile( saveas );
					common->Printf( "saved as %s\n", finalPath.c_str() );
					Mem_Free( buf );
					
					// add that file to our paks list
					checksum = fileSystem->AddZipFile( dlList[ 0 ].filename );

					// verify the checksum to be what the server says
					if ( !checksum || checksum != dlList[ 0 ].checksum ) {
						// "pak is corrupted ( checksum 0x%x, expected 0x%x )"
						session->MessageBox( MSG_OK, va( common->GetLanguageDict()->GetString( "#str_07214" ) , checksum, dlList[0].checksum ), "Download failed", true );
						fileSystem->RemoveExplicitFile( finalPath );
						dlList.Clear();
						return;
					}

					currentDlSize += dlList[ 0 ].size;
					
				} else {
					common->Warning( "download failed: %s", dlList[ 0 ].url.c_str() );
					if ( backgroundDownload.url.dlerror[ 0 ] ) {
						common->Warning( "curl error: %s", backgroundDownload.url.dlerror );
					}
					AsyncClient_CloseBackgroundDownloadFile( backgroundDownload );
					// "The download failed or was cancelled"
					// "Download failed"
					session->MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07215" ), common->GetLanguageDict()->GetString( "#str_07216" ), true );
					dlList.Clear();
					return;
				}

				pakCount++;
				dlList.RemoveIndex( 0 );			
			} while ( dlList.Num() );
			
			// all downloads successful - do the dew
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "reconnect\n" );
		}
	}
}

/*
===============
idAsyncClient::SendAuthCheck
===============
*/
bool idAsyncClient::SendAuthCheck( const char *cdkey, const char *xpkey ) {
	(void)cdkey;
	(void)xpkey;
	return false;
}

/*
===============
idAsyncClient::CheckTimeout
===============
*/
bool idAsyncClient::CheckTimeout( void ) {
	if ( lastPacketTime > 0 && ( lastPacketTime + idAsyncNetwork::clientServerTimeout.GetInteger()*1000 < clientTime ) ) {
		session->StopBox();
		session->MessageBox( MSG_OK, common->GetLanguageDict()->GetString ( "#str_04328" ), common->GetLanguageDict()->GetString ( "#str_04329" ), true );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
		return true;
	}
	return false;
}

/*
===============
idAsyncClient::ProcessDownloadInfoMessage
===============
*/
void idAsyncClient::ProcessDownloadInfoMessage( const netadr_t from, const idBitMsg &msg ) {
	char			buf[ MAX_STRING_CHARS ];
	int				srvDlRequest = msg.ReadLong();
	int				infoType = msg.ReadByte();
	int				pakDl;
	int				pakIndex;
	
	pakDlEntry_t	entry;
	bool			gotAllFiles = true;
	idStr			sizeStr;
	bool			gotGame = false;

	if ( dlRequest == -1 || srvDlRequest != dlRequest ) {
		common->Warning( "bad download id from server, ignored" );
		return;
	}
	// mark the dlRequest as dead now whatever how we process it
	dlRequest = -1;

	if ( infoType == SERVER_DL_REDIRECT ) {
		msg.ReadString( buf, MAX_STRING_CHARS );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
		if ( !idURLPolicy::IsAllowedHTTPURL( buf ) ) {
			common->Warning( "server supplied an invalid download information URL; request ignored" );
			return;
		}
		// "You are missing required pak files to connect to this server.\nThe server gave a web page though:\n%s\nDo you want to go there now?"
		// "Missing required files"
		if ( session->MessageBox( MSG_YESNO, va( common->GetLanguageDict()->GetString( "#str_07217" ), buf ),
								  common->GetLanguageDict()->GetString( "#str_07218" ), true, "yes" )[ 0 ] ) {
			sys->OpenURL( buf, true );
		}
	} else if ( infoType == SERVER_DL_LIST ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
		if ( dlList.Num() ) {
			common->Warning( "tried to process a download list while already busy downloading things" );
			return;
		}
		if ( dlCount < 0 || dlCount > MAX_PURE_PAKS ) {
			common->Warning( "bad download count %d from pending request", dlCount );
			return;
		}
		// read the URLs, check against what we requested, prompt for download
		pakIndex = -1;
		totalDlSize = 0;
		do {
			pakIndex++;
			pakDl = msg.ReadByte();
			if ( pakDl != SERVER_PAK_END && pakIndex >= dlCount ) {
				common->Warning( "server sent more download entries than requested" );
				dlList.Clear();
				return;
			}
			if ( pakDl == SERVER_PAK_YES ) {
				if ( pakIndex == 0 ) {
					gotGame = true;
				}
				msg.ReadString( buf, MAX_STRING_CHARS );
				if ( !AsyncClient_IsSafeDownloadPath( buf ) ) {
					common->Warning( "server sent unsafe download path '%s'; download list ignored", buf );
					dlList.Clear();
					return;
				}
				entry.filename = buf;
				msg.ReadString( buf, MAX_STRING_CHARS );
				if ( !idURLPolicy::IsAllowedHTTPURL( buf ) ) {
					common->Warning( "server supplied a package URL outside the bounded HTTP/HTTPS policy; download list ignored" );
					dlList.Clear();
					return;
				}
				entry.url = buf;
				entry.size = msg.ReadLong();
				if ( entry.size <= 0 || totalDlSize > idMath::INT_MAX - entry.size ) {
					common->Warning( "server sent invalid download size %d for '%s'; download list ignored", entry.size, entry.filename.c_str() );
					dlList.Clear();
					return;
				}
				// checksums are not transmitted, we read them from the dl request we sent
				entry.checksum = dlChecksums[ pakIndex ];
				totalDlSize += entry.size;
				dlList.Append( entry );
				common->Printf( "download %s from %s ( 0x%x )\n", entry.filename.c_str(), entry.url.c_str(), entry.checksum );
			} else if ( pakDl == SERVER_PAK_NO ) {
				msg.ReadString( buf, MAX_STRING_CHARS );
				entry.filename = buf;
				entry.url = "";
				entry.size = 0;
				entry.checksum = 0;
				dlList.Append( entry );
				// first pak is game pak, only fail it if we actually requested it
				if ( pakIndex != 0 || dlChecksums[ 0 ] != 0 ) {
					common->Printf( "no download offered for %s ( 0x%x )\n", entry.filename.c_str(), dlChecksums[ pakIndex ] );
					gotAllFiles = false;
				}
			} else if ( pakDl != SERVER_PAK_END ) {
				common->Warning( "server sent invalid download entry type %d", pakDl );
				dlList.Clear();
				return;
			}			
		} while ( pakDl != SERVER_PAK_END );
		if ( dlList.Num() < dlCount ) {
			common->Printf( "%d files were ignored by the server\n", dlCount - dlList.Num() );
			gotAllFiles = false;
		}
		sizeStr.BestUnit( "%.2f", totalDlSize, MEASURE_SIZE );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
		if ( totalDlSize == 0 ) {
			// was no downloadable stuff for us
			// "Can't connect to the pure server: no downloads offered"
			// "Missing required files"
			dlList.Clear();
			session->MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07219" ), common->GetLanguageDict()->GetString( "#str_07218" ), true );
			return;
		}
		bool asked = false;
		if ( gotGame ) {
			asked = true;
			// "You need to download game code to connect to this server. Are you sure? You should only answer yes if you trust the server administrators."
			// "Missing game binaries"
			if ( !session->MessageBox( MSG_YESNO, common->GetLanguageDict()->GetString( "#str_07220" ), common->GetLanguageDict()->GetString( "#str_07221" ), true, "yes" )[ 0 ] ) {
				dlList.Clear();
				return;
			}
		}
		if ( !gotAllFiles ) {
			asked = true;
			// "The server only offers to download some of the files required to connect ( %s ). Download anyway?"
			// "Missing required files"
			if ( !session->MessageBox( MSG_YESNO, va( common->GetLanguageDict()->GetString( "#str_07222" ), sizeStr.c_str() ),
									   common->GetLanguageDict()->GetString( "#str_07218" ), true, "yes" )[ 0 ] ) {
				dlList.Clear();
				return;
			}
		}
		if ( !asked && idAsyncNetwork::clientDownload.GetInteger() == 1 ) {
			// "You need to download some files to connect to this server ( %s ), proceed?"
			// "Missing required files"
			if ( !session->MessageBox( MSG_YESNO, va( common->GetLanguageDict()->GetString( "#str_07224" ), sizeStr.c_str() ),
									   common->GetLanguageDict()->GetString( "#str_07218" ), true, "yes" )[ 0 ] ) {
				dlList.Clear();
				return;
			}
		}
	} else {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
		// "You are missing some files to connect to this server, and the server doesn't provide downloads."
		// "Missing required files"
		session->MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07223" ), common->GetLanguageDict()->GetString( "#str_07218" ), true );
	}
}

/*
===============
idAsyncClient::GetDownloadRequest
===============
*/
int idAsyncClient::GetDownloadRequest( const int checksums[ MAX_PURE_PAKS ], int count, int gamePakChecksum ) {
	const int storedChecksumCount = idMath::ClampInt( 0, MAX_PURE_PAKS - 1, count );
	assert( count == storedChecksumCount );
	assert( !checksums[ storedChecksumCount ] ); // 0-terminated
	if ( count != storedChecksumCount ) {
		common->Warning( "download request checksum count %d exceeds storage capacity %d", count, storedChecksumCount );
	}
	if ( count != storedChecksumCount || memcmp( dlChecksums + 1, checksums, sizeof( int ) * storedChecksumCount ) || gamePakChecksum != dlChecksums[ 0 ] ) {
		memset( dlChecksums, 0, sizeof( dlChecksums ) );
		dlChecksums[ 0 ] = gamePakChecksum;
		memcpy( dlChecksums + 1, checksums, sizeof( int ) * storedChecksumCount );

		std::uint32_t secureRequest = 0;
		if ( !Sys_GetSecureRandomBytes( &secureRequest, sizeof( secureRequest ) ) ) {
			memset( dlChecksums, 0, sizeof( dlChecksums ) );
			dlRequest = -1;
			dlCount = -1;
			return -1;
		}
		if ( secureRequest == static_cast<std::uint32_t>( -1 ) ) {
			secureRequest = 0;
		}
		dlRequest = static_cast<int>( secureRequest );
		dlCount = storedChecksumCount + 1;
		return dlRequest;
	}
	// this is the same dlRequest, we haven't heard from the server. keep the same id
	return dlRequest;
}
