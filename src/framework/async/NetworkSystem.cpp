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




#include "NetworkSystem.h"
#include "AsyncServer.h"
#include "AsyncNetwork.h"
#ifndef ID_DEDICATED
#include "../Session_local.h"
#endif

idNetworkSystem		networkSystemLocal;
idNetworkSystem *	networkSystem = &networkSystemLocal;

/*
==================
idNetworkSystem::GetServerAddress
==================
*/
const char* idNetworkSystem::GetServerAddress( void ) {
	static char serverAddress[ MAX_STRING_CHARS ];
	serverAddress[ 0 ] = '\0';

	if ( idAsyncNetwork::client.IsActive() ) {
		idStr::Copynz( serverAddress, Sys_NetAdrToString( idAsyncNetwork::client.GetServerAddress() ), sizeof( serverAddress ) );
	} else if ( idAsyncNetwork::server.IsActive() ) {
		idStr::Copynz( serverAddress, Sys_NetAdrToString( idAsyncNetwork::server.GetBoundAdr() ), sizeof( serverAddress ) );
	}

	return serverAddress;
}


/*
==================
idNetworkSystem::ServerSendReliableMessage
==================
*/
void idNetworkSystem::ServerSendReliableMessage( int clientNum, const idBitMsg &msg ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		idAsyncNetwork::server.SendReliableGameMessage( clientNum, msg );
	}
}

/*
==================
idNetworkSystem::ServerSendReliableMessageExcluding
==================
*/
void idNetworkSystem::ServerSendReliableMessageExcluding( int clientNum, const idBitMsg &msg ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		idAsyncNetwork::server.SendReliableGameMessageExcluding( clientNum, msg );
	}
}

/*
==================
idNetworkSystem::ServerGetClientPing
==================
*/
int idNetworkSystem::ServerGetClientPing( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientPing( clientNum );
	}
	return 0;
}

/*
==================
idNetworkSystem::ServerGetClientPrediction
==================
*/
int idNetworkSystem::ServerGetClientPrediction( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientPrediction( clientNum );
	}
	return 0;
}

/*
==================
idNetworkSystem::ServerGetClientTimeSinceLastPacket
==================
*/
int idNetworkSystem::ServerGetClientTimeSinceLastPacket( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientTimeSinceLastPacket( clientNum );
	}
	return 0;
}

/*
==================
idNetworkSystem::ServerGetClientTimeSinceLastInput
==================
*/
int idNetworkSystem::ServerGetClientTimeSinceLastInput( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientTimeSinceLastInput( clientNum );
	}
	return 0;
}

/*
==================
idNetworkSystem::ServerGetClientOutgoingRate
==================
*/
int idNetworkSystem::ServerGetClientOutgoingRate( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientOutgoingRate( clientNum );
	}
	return 0;
}

/*
==================
idNetworkSystem::ServerGetClientIncomingRate
==================
*/
int idNetworkSystem::ServerGetClientIncomingRate( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientIncomingRate( clientNum );
	}
	return 0;
}

/*
==================
idNetworkSystem::ServerGetClientIncomingPacketLoss
==================
*/
float idNetworkSystem::ServerGetClientIncomingPacketLoss( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientIncomingPacketLoss( clientNum );
	}
	return 0.0f;
}

/*
==================
idNetworkSystem::ClientSendReliableMessage
==================
*/
void idNetworkSystem::ClientSendReliableMessage( const idBitMsg &msg ) {
	if ( idAsyncNetwork::client.IsActive() ) {
		idAsyncNetwork::client.SendReliableGameMessage( msg );
	} else if ( idAsyncNetwork::server.IsActive() ) {
		idAsyncNetwork::server.LocalClientSendReliableMessage( msg );
	}
}

/*
==================
idNetworkSystem::ClientGetPrediction
==================
*/
int idNetworkSystem::ClientGetPrediction( void ) {
	if ( idAsyncNetwork::client.IsActive() ) {
		return idAsyncNetwork::client.GetPrediction();
	}
	return 0;
}

/*
==================
idNetworkSystem::ClientGetTimeSinceLastPacket
==================
*/
int idNetworkSystem::ClientGetTimeSinceLastPacket( void ) {
	if ( idAsyncNetwork::client.IsActive() ) {
		return idAsyncNetwork::client.GetTimeSinceLastPacket();
	}
	return 0;
}

/*
==================
idNetworkSystem::ClientGetOutgoingRate
==================
*/
int idNetworkSystem::ClientGetOutgoingRate( void ) {
	if ( idAsyncNetwork::client.IsActive() ) {
		return idAsyncNetwork::client.GetOutgoingRate();
	}
	return 0;
}

/*
==================
idNetworkSystem::ClientGetIncomingRate
==================
*/
int idNetworkSystem::ClientGetIncomingRate( void ) {
	if ( idAsyncNetwork::client.IsActive() ) {
		return idAsyncNetwork::client.GetIncomingRate();
	}
	return 0;
}

/*
==================
idNetworkSystem::ClientGetIncomingPacketLoss
==================
*/
float idNetworkSystem::ClientGetIncomingPacketLoss( void ) {
	if ( idAsyncNetwork::client.IsActive() ) {
		return idAsyncNetwork::client.GetIncomingPacketLoss();
	}
	return 0.0f;
}

/*
==================
idNetworkSystem::AllocateClientSlotForBot
==================
*/
int idNetworkSystem::AllocateClientSlotForBot(const char* botName, int maxPlayersOnServer) {
	return idAsyncNetwork::server.AllocOpenClientSlotForAI(botName, maxPlayersOnServer);
}

/*
==================
idNetworkSystem::ServerSetBotUserCommand
==================
*/
int idNetworkSystem::ServerSetBotUserCommand(int clientNum, int frameNum, const usercmd_t& cmd) {
	return idAsyncNetwork::server.ServerSetBotUserCommand(clientNum, frameNum, cmd);
}

/*
==================
idNetworkSystem::ServerSetBotUserName
==================
*/
int idNetworkSystem::ServerSetBotUserName(int clientNum, const char* playerName) {
	return 0;
}

/*
==================
idNetworkSystem::SetLoadingText
==================
*/
void idNetworkSystem::SetLoadingText( const char *loadingText ) {
#ifndef ID_DEDICATED
	if ( !sessLocal.guiLoading ) {
		return;
	}

	const char *text = loadingText ? loadingText : "";
	sessLocal.guiLoading->SetStateString( "loading_message", text );
	sessLocal.guiLoading->SetStateString( "server_loadinfo", text );

	// Make sure MP loading metadata is eventually populated even if it wasn't available
	// at initial loading GUI setup time.
	const char *gameType = sessLocal.mapSpawnData.serverInfo.GetString( "si_gameType", cvarSystem->GetCVarString( "si_gameType" ) );
	const char *spawnMapPath = sessLocal.mapSpawnData.serverInfo.GetString( "si_map", text );
	const bool mapLooksMultiplayer = !idStr::Icmpn( spawnMapPath, "mp/", 3 ) || !idStr::Icmpn( text, "mp/", 3 );
	const bool isMultiplayerLoad = mapLooksMultiplayer || ( gameType && gameType[ 0 ] && idStr::Icmp( gameType, "singleplayer" ) != 0 );
	if ( isMultiplayerLoad ) {
		const char *resolvedGameType = gameType;
		if ( resolvedGameType == NULL || resolvedGameType[ 0 ] == '\0' ) {
			resolvedGameType = cvarSystem->GetCVarString( "si_gameType" );
		}
		if ( resolvedGameType == NULL || resolvedGameType[ 0 ] == '\0' ) {
			resolvedGameType = "DM";
		}

		const char *serverName = sessLocal.mapSpawnData.serverInfo.GetString( "si_name", cvarSystem->GetCVarString( "si_name" ) );
		if ( serverName && serverName[ 0 ] && !sessLocal.guiLoading->State().GetString( "server_name", "" )[ 0 ] ) {
			sessLocal.guiLoading->SetStateString( "server_name", serverName );
		}

		idStr serverAddress = GetServerAddress();
		if ( !serverAddress.Length() ) {
			const char *netIP = cvarSystem->GetCVarString( "net_ip" );
			const int netPort = cvarSystem->GetCVarInteger( "net_port" );
			if ( netIP && netIP[ 0 ] ) {
				serverAddress = va( "%s:%d", idStr::Icmp( netIP, "0.0.0.0" ) ? netIP : "127.0.0.1", netPort );
			} else if ( netPort > 0 ) {
				serverAddress = va( "127.0.0.1:%d", netPort );
			}
		}
		if ( serverAddress.Length() && !sessLocal.guiLoading->State().GetString( "server_ip", "" )[ 0 ] ) {
			sessLocal.guiLoading->SetStateString( "server_ip", serverAddress.c_str() );
		}

		if ( !sessLocal.guiLoading->State().GetString( "server_gametype", "" )[ 0 ] ) {
			const char *localizedGameType = resolvedGameType;
			if ( !idStr::Icmp( resolvedGameType, "DM" ) ) {
				localizedGameType = common->GetLocalizedString( "#str_110011" );
			} else if ( !idStr::Icmp( resolvedGameType, "Tourney" ) ) {
				localizedGameType = common->GetLocalizedString( "#str_110012" );
			} else if ( !idStr::Icmp( resolvedGameType, "Team DM" ) ) {
				localizedGameType = common->GetLocalizedString( "#str_110013" );
			} else if ( !idStr::Icmp( resolvedGameType, "CTF" ) ) {
				localizedGameType = common->GetLocalizedString( "#str_110014" );
			} else if ( !idStr::Icmp( resolvedGameType, "Arena CTF" ) ) {
				localizedGameType = common->GetLocalizedString( "#str_110015" );
			} else if ( !idStr::Icmp( resolvedGameType, "DeadZone" ) ) {
				localizedGameType = common->GetLocalizedString( "#str_122001" );
			}
			sessLocal.guiLoading->SetStateString( "server_gametype", localizedGameType );
		}

		if ( !sessLocal.guiLoading->State().GetString( "server_limit", "" )[ 0 ] ) {
			const bool useCaptureLimit = !idStr::Icmp( resolvedGameType, "CTF" ) || !idStr::Icmp( resolvedGameType, "Arena CTF" );
			const char *limitKey = useCaptureLimit ? "si_captureLimit" : "si_fragLimit";
			int limit = sessLocal.mapSpawnData.serverInfo.GetInt( limitKey );
			if ( limit <= 0 ) {
				limit = cvarSystem->GetCVarInteger( limitKey );
			}
			if ( limit > 0 ) {
				const char *limitLabel = useCaptureLimit ? common->GetLocalizedString( "#str_107661" ) : common->GetLocalizedString( "#str_107660" );
				sessLocal.guiLoading->SetStateString( "server_limit", va( "%s %d", limitLabel, limit ) );
			}
		}
	}

	// New map load starts with map path text, so clear icon state once at map start.
	const bool isMapPathText =
		!idStr::Icmpn( text, "mp/", 3 ) ||
		!idStr::Icmpn( text, "game/", 5 ) ||
		!idStr::Icmpn( text, "maps/", 5 ) ||
		!idStr::Icmpn( text, "mp\\", 3 ) ||
		!idStr::Icmpn( text, "game\\", 5 ) ||
		!idStr::Icmpn( text, "maps\\", 5 );
	if ( isMapPathText ) {
		sessLocal.guiLoading->SetStateInt( "load_icons", 0 );
		for ( int i = 1; i <= 20; i++ ) {
			sessLocal.guiLoading->SetStateInt( va( "load_icon_%d", i ), 0 );
			sessLocal.guiLoading->SetStateString( va( "load_icon_img_%d", i ), "" );
		}
	}

	sessLocal.guiLoading->StateChanged( com_frameTime );
#endif
}

/*
==================
idNetworkSystem::AddLoadingIcon
==================
*/
void idNetworkSystem::AddLoadingIcon( const char *icon ) {
#ifndef ID_DEDICATED
	if ( !sessLocal.guiLoading || !icon || !icon[ 0 ] ) {
		return;
	}

	int numIcons = sessLocal.guiLoading->State().GetInt( "load_icons" );
	if ( numIcons < 0 ) {
		numIcons = 0;
	}
	if ( numIcons >= 20 ) {
		return;
	}

	numIcons++;
	sessLocal.guiLoading->SetStateInt( "load_icons", numIcons );
	sessLocal.guiLoading->SetStateInt( va( "load_icon_%d", numIcons ), 1 );
	sessLocal.guiLoading->SetStateString( va( "load_icon_img_%d", numIcons ), icon );
	sessLocal.guiLoading->StateChanged( com_frameTime );
#endif
}
