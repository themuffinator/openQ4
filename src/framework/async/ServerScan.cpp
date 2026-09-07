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




#include "../../ui/ListGUILocal.h"

idCVar gui_filter_password( "gui_filter_password", "0", CVAR_GUI | CVAR_INTEGER | CVAR_ARCHIVE, "Password filter" );
idCVar gui_filter_players( "gui_filter_players", "0", CVAR_GUI | CVAR_INTEGER | CVAR_ARCHIVE, "Players filter" );
idCVar gui_filter_gameType( "gui_filter_gameType", "0", CVAR_GUI | CVAR_INTEGER | CVAR_ARCHIVE, "Gametype filter" );
idCVar gui_filter_idle( "gui_filter_idle", "0", CVAR_GUI | CVAR_INTEGER | CVAR_ARCHIVE, "Idle servers filter" );
idCVar gui_filter_game( "gui_filter_game", "0", CVAR_GUI | CVAR_INTEGER | CVAR_ARCHIVE, "Game filter" );
idCVar gui_filter_mod( "gui_filter_mod", "", CVAR_GUI | CVAR_ARCHIVE, "Server browser mod filter; empty shows every mod" );

static const int MAX_BROWSER_SERVERS = 4096;
static const int MAX_BROWSER_FAVORITES = 256;
static const char *BROWSER_FAVORITES_FILE = "server_favorites.list";

// Network text is data, never a row delimiter, a material escape or GUI code.
// Retain printable localized text, but strip color/image escapes and controls.
static idStr ServerBrowserText( const char *text, int limit = 96 ) {
	idStr result;
	if ( text == NULL ) {
		return result;
	}
	for ( int i = 0; text[i] && result.Length() < limit; ++i ) {
		const unsigned char ch = static_cast<unsigned char>( text[i] );
		if ( ch >= 128 ) {
			const int bytes = ch >= 0xc2 && ch <= 0xdf ? 2 : ch >= 0xe0 && ch <= 0xef ? 3 : ch >= 0xf0 && ch <= 0xf4 ? 4 : 0;
			bool valid = bytes != 0;
			for ( int j = 1; valid && j < bytes; ++j ) {
				const unsigned char next = static_cast<unsigned char>( text[i+j] );
				valid = next >= 0x80 && next <= 0xbf;
				if ( j == 1 ) {
					valid = valid && !( ( ch == 0xe0 && next < 0xa0 ) || ( ch == 0xed && next >= 0xa0 ) ||
						( ch == 0xf0 && next < 0x90 ) || ( ch == 0xf4 && next >= 0x90 ) );
				}
			}
			if ( !valid ) {
				result += '?';
				continue;
			}
			if ( result.Length() + bytes > limit ) {
				break;
			}
			for ( int j = 0; j < bytes; ++j ) {
				result += text[i+j];
			}
			i += bytes - 1;
			continue;
		}
		if ( ch == '^' ) {
			if ( text[i + 1] >= '0' && text[i + 1] <= '9' ) {
				++i;
			}
			continue;
		}
		result += ( ch < 32 || ch == 127 ) ? ' ' : text[i];
	}
	return result;
}

// A mod's identity must survive display, selection and filtering unchanged.
// Reject malformed folder identifiers rather than inventing a sanitized token.
static idStr ServerBrowserMod( const char *mod ) {
	if ( !mod || !mod[0] ) {
		return "baseoq4";
	}
	if ( strlen( mod ) > 64 || !strcmp( mod, "." ) || !strcmp( mod, ".." ) ) {
		return "";
	}
	for ( const char *p = mod; *p; ++p ) {
		if ( !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) ||
			( *p >= '0' && *p <= '9' ) || *p == '_' || *p == '-' || *p == '.' ) ) {
			return "";
		}
	}
	idStr result = mod;
	result.ToLower();
	return result;
}

static bool ServerBrowserMapPath( const char *path ) {
	if ( path == NULL || !path[0] || strlen( path ) > 128 || strstr( path, ".." ) || path[0] == '/' ) {
		return false;
	}
	for ( const char *p = path; *p; ++p ) {
		if ( !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) ||
			( *p >= '0' && *p <= '9' ) || *p == '/' || *p == '_' || *p == '-' ) ) {
			return false;
		}
	}
	return true;
}

static const char *ServerBrowserGameType( const char *type ) {
	static const char *tokens[] = { "DM", "Tourney", "Team DM", "CTF", "Arena CTF", "DeadZone",
		"Duel", "Clan Arena", "Freeze Tag", "Red Rover", "One Flag CTF", "Arena One Flag CTF" };
	static const char *labels[] = { "#str_107679", "#str_107676", "#str_107677", "#str_107678",
		"#str_107681", "#str_42860", "#str_41300", "#str_41301", "#str_41302", "#str_41303",
		"#str_107680", "#str_107682" };
	for ( int i = 0; i < static_cast<int>( sizeof( tokens ) / sizeof( tokens[0] ) ); ++i ) {
		if ( !idStr::Icmp( type, tokens[i] ) ) {
			return common->GetLanguageDict()->GetString( labels[i] );
		}
	}
	return type;
}

const char* l_gameTypes[] = {
	"DM",
	"Tourney",
	"Team DM",
	"CTF",
	"Arena CTF",
	"DeadZone",
	"Duel",
	"Clan Arena",
	"Freeze Tag",
	"Red Rover",
	"One Flag CTF",
	"Arena One Flag CTF",
	NULL
};

static idServerScan *l_serverScan = NULL;

/*
================
idServerScan::idServerScan
================
*/
idServerScan::idServerScan( ) {
	m_pGUI = NULL;
	listGUI = NULL;
	favoritesLoaded = false;
	m_sort = SORT_PING;
	m_sortAscending = true;
	challenge = 0;
	LocalClear();
}

/*
================
idServerScan::LocalClear
================
*/
void idServerScan::LocalClear( ) {
	scan_state = IDLE;
	incoming_net = false;
	lan_pingtime = -1;
	net_info.Clear();
	net_servers.Clear();
	cur_info = 0;
	if ( listGUI && listGUI->IsConfigured() ) {
		listGUI->Clear();
		listGUI->SetSelection( -1 );
		m_pGUI->SetStateInt( "serverList_selid_0", -1 );
	}
	incoming_useTimeout = false;
	m_sortedServers.Clear();
}

/*
================
idServerScan::Clear
================
*/
void idServerScan::Clear( ) {
	LocalClear();
	idList<networkServer_t>::Clear();
}

/*
================
idServerScan::Shutdown
================
*/
void idServerScan::Shutdown( ) {
	m_pGUI = NULL;
	if ( listGUI ) {
		listGUI->Config( NULL, NULL );
		uiManager->FreeListGUI( listGUI );
		listGUI = NULL;
	}
	screenshot.Clear();
}

/*
================
idServerScan::SetupLANScan
================
*/
void idServerScan::SetupLANScan( ) {
	Clear();
	GUIUpdateSelected();
	scan_state = LAN_SCAN;
	challenge++;
	lan_pingtime = Sys_Milliseconds();
	UpdateBrowserStatus();
	common->DPrintf( "SetupLANScan with challenge %d\n", challenge );
}

/*
================
idServerScan::InfoResponse
================
*/
int idServerScan::InfoResponse( networkServer_t &server ) {
	if ( ( scan_state != LAN_SCAN && scan_state != NET_SCAN ) ||
		server.clients < 0 || server.clients > MAX_ASYNC_CLIENTS || Num() >= MAX_BROWSER_SERVERS ) {
		return -1;
	}

	idStr serv = Sys_NetAdrToString( server.adr );

	if ( server.challenge != challenge ) {
		common->DPrintf( "idServerScan::InfoResponse - ignoring response from %s, wrong challenge %d.", serv.c_str(), server.challenge );
		return false;
	}

	if ( scan_state == NET_SCAN ) {	
		const idKeyValue *info = net_info.FindKey( serv.c_str() );
		if ( !info ) {
			common->DPrintf( "idServerScan::InfoResponse NET_SCAN: reply from unknown %s\n", serv.c_str() );
			return false;
		}
		int id = atoi( info->GetValue() );
		net_info.Delete( serv.c_str() );
		inServer_t iserv = net_servers[ id ];
		server.ping = Sys_Milliseconds() - iserv.time;
		server.id = iserv.id;
	} else {
		server.ping = Sys_Milliseconds() - lan_pingtime;
		server.id = 0;

		// check for duplicate servers
		for ( int i = 0; i < Num() ; i++ ) {
			if ( Sys_CompareNetAdrBase( (*this)[ i ].adr, server.adr ) && (*this)[ i ].adr.port == server.adr.port ) {
				common->DPrintf( "idServerScan::InfoResponse LAN_SCAN: duplicate server %s\n", serv.c_str() );
				return true;
			}
		}
	}

	const char *si_map = server.serverInfo.GetString( "si_map" );
	const idDecl *mapDecl = ServerBrowserMapPath( si_map ) ? declManager->FindType( DECL_MAPDEF, si_map, false ) : NULL;
	const idDeclEntityDef *mapDef = static_cast< const idDeclEntityDef * >( mapDecl );
	if ( mapDef ) {
		const char *mapName = common->GetLanguageDict()->GetString( mapDef->dict.GetString( "name", si_map ) );
		server.serverInfo.Set( "si_mapName", mapName );
	} else {
		server.serverInfo.Set( "si_mapName", si_map );
	}

	int index = Append( server );
	m_sortedServers.Append( Num()-1 );
	l_serverScan = this;
	m_sortedServers.Sort( idServerScan::Cmp );
	ApplyFilter();

	return index;
}

/*
================
idServerScan::AddServer
================
*/
void idServerScan::AddServer( int id, const char *srv ) {
	inServer_t s;
	if ( net_servers.Num() >= MAX_BROWSER_SERVERS ) {
		return;
	}
	
	incoming_net = true;
	incoming_lastTime = Sys_Milliseconds() + INCOMING_TIMEOUT;
	s.id = id;
	
	// using IPs, not hosts
	if ( !Sys_StringToNetAdr( srv, &s.adr, false ) ) {
		common->DPrintf( "idServerScan::AddServer: failed to parse server %s\n", srv );
		return;
	}
	if ( !s.adr.port ) {
		s.adr.port = PORT_SERVER;
	}

	// A master that answers both the legacy and the extended list request names
	// the same server twice, and a dual-stack LAN host answers both sweeps.
	// Listing an endpoint once keeps the ping accounting and the browser honest.
	for ( int i = 0; i < net_servers.Num(); i++ ) {
		if ( Sys_CompareNetAdrBase( net_servers[ i ].adr, s.adr ) && net_servers[ i ].adr.port == s.adr.port ) {
			return;
		}
	}

	net_servers.Append( s );
}

/*
================
idServerScan::EndServers
================
*/
void idServerScan::EndServers( ) {
	incoming_net = false;
	l_serverScan = this;
	m_sortedServers.Sort( idServerScan::Cmp );
	ApplyFilter();
} 

/*
================
idServerScan::StartServers
================
*/
void idServerScan::StartServers( bool timeout ) {
	incoming_net = true;
	incoming_useTimeout = timeout;
	incoming_lastTime = Sys_Milliseconds() + REFRESH_START;
}

/*
================
idServerScan::EmitGetInfo
================
*/
void idServerScan::EmitGetInfo( netadr_t &serv ) {
	idAsyncNetwork::client.GetServerInfo( serv );
}

/*
===============
idServerScan::GetChallenge
===============
*/
int idServerScan::GetChallenge( ) {
	return challenge;
}

/*
================
idServerScan::NetScan
================
*/
void idServerScan::NetScan( ) {
	if ( !idAsyncNetwork::client.IsPortInitialized() ) {
		// if the port isn't open, initialize it, but wait for a short
		// time to let the OS do whatever magic things it needs to do...
		idAsyncNetwork::client.InitPort();
		// start the scan one second from now...
		scan_state = WAIT_ON_INIT;
		endWaitTime = Sys_Milliseconds() + 1000;
		UpdateBrowserStatus();
		return;
	}

	// make sure the client port is open
	idAsyncNetwork::client.InitPort();

	scan_state = NET_SCAN;
	challenge++;
	
	idList<networkServer_t>::Clear();
	m_sortedServers.Clear();
	cur_info = 0;
	net_info.Clear();
	if ( listGUI && listGUI->IsConfigured() ) {
		listGUI->Clear();
		listGUI->SetSelection( -1 );
	}
	GUIUpdateSelected();
	UpdateBrowserStatus();
	common->DPrintf( "NetScan with challenge %d\n", challenge );
	
	while ( cur_info < Min( net_servers.Num(), MAX_PINGREQUESTS ) ) {
		netadr_t serv = net_servers[ cur_info ].adr;
		EmitGetInfo( serv );
		net_servers[ cur_info ].time = Sys_Milliseconds();
		net_info.SetInt( Sys_NetAdrToString( serv ), cur_info );
		cur_info++;
	}
}

/*
===============
idServerScan::ServerScanFrame
===============
*/
void idServerScan::RunFrame( ) {
	if ( scan_state == IDLE ) {
		return;
	} 
	
	if ( scan_state == WAIT_ON_INIT ) {
		if ( Sys_Milliseconds() >= endWaitTime ) {
				scan_state = IDLE;
				NetScan();
			}
		return;
	} 
	
	int timeout_limit = Sys_Milliseconds() - REPLY_TIMEOUT;
	
	if ( scan_state == LAN_SCAN ) {
		if ( timeout_limit > lan_pingtime ) {
			common->Printf( "Scanned for servers on the LAN\n" );
			scan_state = IDLE;
			EndServers();
		}
		return;
	}
	
	// if scan_state == NET_SCAN
	
	// check for timeouts
	int i = 0;
	while ( i < net_info.GetNumKeyVals() ) {
		if ( timeout_limit > net_servers[ atoi( net_info.GetKeyVal( i )->GetValue().c_str() ) ].time ) {
			common->DPrintf( "timeout %s\n", net_info.GetKeyVal( i )->GetKey().c_str() );
			net_info.Delete( net_info.GetKeyVal( i )->GetKey().c_str() );
		} else {
			i++;
		}
	}
			
	// possibly send more queries
	while ( cur_info < net_servers.Num() && net_info.GetNumKeyVals() < MAX_PINGREQUESTS ) {
		netadr_t serv = net_servers[ cur_info ].adr;
		EmitGetInfo( serv );
		net_servers[ cur_info ].time = Sys_Milliseconds();
		net_info.SetInt( Sys_NetAdrToString( serv ), cur_info );
		cur_info++;
	}
	
	// update state
	if ( ( !incoming_net || ( incoming_useTimeout && Sys_Milliseconds() > incoming_lastTime ) ) && net_info.GetNumKeyVals() == 0 ) {
		// the list is complete, we are no longer waiting for any getInfo replies
		common->Printf( "Scanned %d servers.\n", cur_info );
		scan_state = IDLE;
		EndServers();
	}
	UpdateBrowserStatus();
}

/*
===============
idServerScan::GetBestPing
===============
*/
bool idServerScan::GetBestPing( networkServer_t &serv ) {
	int i, ic;
	ic = Num();
	if ( !ic ) {
		return false;
	}
	serv = (*this)[ 0 ];
	for ( i = 0 ; i < ic ; i++ ) {
		if ( (*this)[ i ].ping < serv.ping ) {
			serv = (*this)[ i ];
		}
	}
	return true;
}

/*
================
idServerScan::GUIConfig
================
*/
void idServerScan::GUIConfig( idUserInterface *pGUI, const char *name ) {
	m_pGUI = pGUI;
	if ( listGUI == NULL ) {
		listGUI = uiManager->AllocListGUI();
	}
	listGUI->Config( pGUI, name );
}

/*
================
idServerScan::GUIUpdateSelected
================
*/
void idServerScan::GUIUpdateSelected( void ) {
	if ( !m_pGUI || !listGUI || !listGUI->IsConfigured() ) {
		return;
	}
	idStr address;
	const bool selected = GetSelectedAddress( address );
	m_pGUI->SetStateBool( "browser_selected", selected );
	m_pGUI->SetStateString( "favoriteStatus", common->GetLanguageDict()->GetString( "#str_200292" ) );
	m_pGUI->SetStateString( "server_tooltip_ip", address.c_str() );
	m_pGUI->SetStateString( "server_IP", address.c_str() );
	m_pGUI->SetStateBool( "serverinfo_visible", false );
	for ( int j = 0; j < MAX_ASYNC_CLIENTS; ++j ) {
		m_pGUI->DeleteStateVar( va( "server_tooltip_playerlist_item_%d", j ) );
	}
	for ( int j = 0; j < 8; ++j ) {
		m_pGUI->DeleteStateVar( va( "server_tooltip_settingslist_item_%d", j ) );
	}
	if ( !selected ) {
		m_pGUI->SetStateString( "server_name", "" );
		m_pGUI->SetStateString( "player1", "" );
		m_pGUI->SetStateString( "player2", "" );
		m_pGUI->SetStateString( "player3", "" );
		m_pGUI->SetStateString( "player4", "" );
		m_pGUI->SetStateString( "player5", "" );
		m_pGUI->SetStateString( "player6", "" );
		m_pGUI->SetStateString( "player7", "" );
		m_pGUI->SetStateString( "player8", "" );
		m_pGUI->SetStateString( "server_map", "" );
		m_pGUI->SetStateString( "browser_levelshot", "" );
		m_pGUI->SetStateString( "server_gameType", "" );
		m_pGUI->SetStateString( "server_IP", "" );
		m_pGUI->SetStateString( "server_passworded", "" );
	} else {
		const int i = listGUI->GetSelection( NULL, 0 );
		const networkServer_t &server = (*this)[i];
		m_pGUI->SetStateString( "server_name", ServerBrowserText( server.serverInfo.GetString( "si_name" ) ).c_str() );
		for ( int j = 0; j < 8; j++ ) {
			if ( server.clients > j ) {
				m_pGUI->SetStateString( va( "player%i", j + 1 ) , ServerBrowserText( server.nickname[j], MAX_NICKLEN - 1 ).c_str() );
			} else {
				m_pGUI->SetStateString( va( "player%i", j + 1 ) , "" );
			}
		}
		for ( int j = 0; j < server.clients; ++j ) {
			m_pGUI->SetStateString( va( "server_tooltip_playerlist_item_%d", j ), ServerBrowserText( server.nickname[j], MAX_NICKLEN - 1 ).c_str() );
		}
		m_pGUI->SetStateString( "server_map", ServerBrowserText( server.serverInfo.GetString( "si_mapName" ) ).c_str() );
		char levelshot[ MAX_STRING_CHARS ] = "";
		const char *map = server.serverInfo.GetString( "si_map" );
		if ( ServerBrowserMapPath( map ) ) {
			fileSystem->FindMapScreenshot( map, levelshot, sizeof( levelshot ) );
		}
		m_pGUI->SetStateString( "browser_levelshot", levelshot );
		m_pGUI->SetStateString( "server_gameType", ServerBrowserText( ServerBrowserGameType( server.serverInfo.GetString( "si_gameType" ) ) ).c_str() );
		m_pGUI->SetStateString( "server_passworded", server.serverInfo.GetBool( "si_usePass" ) ? common->GetLanguageDict()->GetString( "#str_200933" ) : "" );
		m_pGUI->SetStateString( "favoriteStatus", common->GetLanguageDict()->GetString( IsFavorite( server.adr ) ? "#str_200293" : "#str_200292" ) );
	}
	m_pGUI->StateChanged( common->GetPresentationTime() );
}

bool idServerScan::GetSelectedAddress( idStr &address ) {
	address.Clear();
	if ( !m_pGUI || !listGUI || !listGUI->IsConfigured() ||
		m_pGUI->State().GetInt( "serverList_sel_0", "-1" ) < 0 ) {
		return false;
	}
	const int selected = listGUI->GetSelection( NULL, 0 );
	if ( selected < 0 || selected >= Num() || IsFiltered( (*this)[selected] ) ) {
		m_pGUI->SetStateInt( "serverList_selid_0", -1 );
		return false;
	}
	address = Sys_NetAdrToString( (*this)[selected].adr );
	return true;
}

void idServerScan::GUIInit() {
	LoadFavorites();
	UpdateFilterByMod( 0 );
}

void idServerScan::LoadFavorites() {
	if ( favoritesLoaded ) {
		return;
	}
	favoritesLoaded = true;
	const int size = fileSystem->ReadFile( BROWSER_FAVORITES_FILE, NULL );
	if ( size <= 0 || size > 32768 ) {
		return;
	}
	void *buffer = NULL;
	const int read = fileSystem->ReadFile( BROWSER_FAVORITES_FILE, &buffer );
	if ( buffer == NULL ) {
		return;
	}
	if ( read > 0 && read <= 32768 ) {
		const char *bytes = static_cast<const char *>( buffer );
		idStr line;
		for ( int i = 0; i <= read && favorites.Num() < MAX_BROWSER_FAVORITES; ++i ) {
			if ( i == read || bytes[i] == '\n' ) {
				netadr_t endpoint;
				if ( line.Length() && line.Length() < 128 && Sys_StringToNetAdr( line.c_str(), &endpoint, false ) &&
					( endpoint.type == NA_IP || endpoint.type == NA_IP6 || endpoint.type == NA_LOOPBACK ) ) {
					if ( !endpoint.port ) {
						endpoint.port = PORT_SERVER;
					}
					favorites.AddUnique( Sys_NetAdrToString( endpoint ) );
				}
				line.Clear();
			} else if ( bytes[i] != '\r' && line.Length() < 128 ) {
				line += bytes[i];
			}
		}
	}
	fileSystem->FreeFile( buffer );
}

bool idServerScan::IsFavorite( const netadr_t &address ) const {
	return favorites.FindIndex( idStr( Sys_NetAdrToString( address ) ) ) >= 0;
}

void idServerScan::ToggleFavorite() {
	LoadFavorites();
	idStr address;
	if ( !GetSelectedAddress( address ) ) {
		return;
	}
	idStrList updated = favorites;
	const int old = updated.FindIndex( address );
	if ( old >= 0 ) {
		updated.RemoveIndex( old );
	} else if ( updated.Num() < MAX_BROWSER_FAVORITES ) {
		updated.Append( address );
	} else {
		common->Warning( "Server favorites limit reached (%d)", MAX_BROWSER_FAVORITES );
		return;
	}
	idStr contents;
	for ( int i = 0; i < updated.Num(); ++i ) {
		contents += updated[i];
		contents += '\n';
	}
	const char *pending = "server_favorites.pending";
	if ( fileSystem->WriteFile( pending, contents.c_str(), contents.Length() ) != contents.Length() ||
		!fileSystem->PromoteFile( pending, BROWSER_FAVORITES_FILE ) ) {
		fileSystem->RemoveFile( pending );
		common->Warning( "Could not save server favorites" );
		return;
	}
	favorites = updated;
	l_serverScan = this;
	m_sortedServers.Sort( idServerScan::Cmp );
	ApplyFilter();
}

void idServerScan::AddFavoriteServers() {
	LoadFavorites();
	for ( int i = 0; i < favorites.Num(); ++i ) {
		AddServer( i, favorites[i].c_str() );
	}
}

void idServerScan::UpdateFilterByMod( int direction ) {
	idStrList mods;
	mods.Append( "" );
	mods.Append( "baseoq4" );
	for ( int i = 0; i < Num(); ++i ) {
		const idStr mod = ServerBrowserMod( (*this)[i].serverInfo.GetString( "fs_game" ) );
		if ( mod.Length() ) {
			mods.AddUnique( mod );
		}
	}
	mods.Sort();
	const idStr archived = gui_filter_mod.GetString()[0] ? ServerBrowserMod( gui_filter_mod.GetString() ) : idStr( "" );
	int index = mods.FindIndex( archived );
	if ( index < 0 && direction == 0 ) {
		// Keep an archived mod filter while its servers are still being queried.
		mods.Append( archived );
		index = mods.Num() - 1;
	} else {
		index = ( Max( 0, index ) + ( direction < 0 ? mods.Num() - 1 : direction > 0 ? 1 : 0 ) ) % mods.Num();
	}
	gui_filter_mod.SetString( mods[index].c_str() );
	if ( m_pGUI ) {
		m_pGUI->SetStateString( "filterMod", mods[index].Length() ? ServerBrowserText( mods[index].c_str(), 64 ).c_str() : common->GetLanguageDict()->GetString( "#str_41502" ) );
	}
	ApplyFilter();
}

void idServerScan::UpdateBrowserStatus() {
	if ( !m_pGUI || !listGUI || !listGUI->IsConfigured() ) {
		return;
	}
	int visible = 0;
	for ( int i = 0; i < Num(); ++i ) {
		visible += !IsFiltered( (*this)[i] );
	}
	idStr status;
	if ( scan_state == LAN_SCAN ) {
		status = common->GetLanguageDict()->GetString( "#str_107293" );
	} else if ( scan_state != IDLE ) {
		const idStr count = va( "%d", visible );
		status = va( common->GetLanguageDict()->GetString( "#str_107298" ), count.c_str() );
	} else {
		status = va( common->GetLanguageDict()->GetString( "#str_42861" ), visible, Num() );
	}
	m_pGUI->SetStateInt( "browser_count", visible );
	m_pGUI->SetStateInt( "browser_total", Num() );
	m_pGUI->SetStateBool( "browser_scanning", scan_state != IDLE );
	if ( status.Icmp( m_pGUI->State().GetString( "sortNotice" ) ) ) {
		m_pGUI->SetStateString( "sortNotice", status.c_str() );
		m_pGUI->StateChanged( common->GetPresentationTime() );
	}
}

/*
================
idServerScan::GUIAdd
================
*/
void idServerScan::GUIAdd( int id, const networkServer_t server ) {
	const idStr name = ServerBrowserText( server.serverInfo.GetString( "si_name" ) );
	const idStr map = ServerBrowserText( server.serverInfo.GetString( "si_mapName" ), 64 );
	const idStr type = ServerBrowserText( ServerBrowserGameType( server.serverInfo.GetString( "si_gameType" ) ), 64 );
	const idStr row = va( "%s\t%s\t%s\t%s\t%s\t%d\t%s\t%d/%d\t%s\t%s",
		IsFavorite( server.adr ) ? "mtr_favorite" : "",
		server.serverInfo.GetBool( "si_usePass" ) ? "mtr_locked" : "",
		server.serverInfo.GetInt( "net_serverDedicated" ) != 0 ? "mtr_dedicated" : "",
		server.serverInfo.GetBool( "sv_punkbuster" ) ? "mtr_pb" : "",
		name.Length() ? name.c_str() : Sys_NetAdrToString( server.adr ), Max( 0, server.ping ),
		server.serverInfo.GetBool( "si_repeater" ) ? "mtr_repeater" : "",
		idMath::ClampInt( 0, MAX_ASYNC_CLIENTS, server.clients ),
		idMath::ClampInt( 0, MAX_ASYNC_CLIENTS, server.serverInfo.GetInt( "si_maxPlayers" ) ),
		type.c_str(), map.c_str() );
	// AllocListGUI creates the concrete engine feeder; the public game-module
	// interface intentionally has no Add method. Keep its ABI unchanged.
	static_cast<idListGUILocal *>( listGUI )->Add( id, row );
}

/*
================
idServerScan::ApplyFilter
================
*/
void idServerScan::ApplyFilter( ) {
	if ( !listGUI || !listGUI->IsConfigured() || !m_pGUI ) {
		return;
	}
	const int oldSelection = m_pGUI->State().GetInt( "serverList_sel_0", "-1" ) >= 0 ? listGUI->GetSelection( NULL, 0 ) : -1;
	int row = 0;
	int newSelection = -1;
	listGUI->SetStateChanges( false );
	listGUI->Clear();
	for ( int i = m_sortAscending ? 0 : m_sortedServers.Num() - 1;
			m_sortAscending ? i < m_sortedServers.Num() : i >= 0;
			m_sortAscending ? i++ : i-- ) {
		const networkServer_t &serv = (*this)[ m_sortedServers[ i ] ];
		if ( !IsFiltered( serv ) ) {
			GUIAdd( m_sortedServers[ i ], serv );
			if ( m_sortedServers[i] == oldSelection ) {
				newSelection = row;
			}
			++row;
		}
	}
	listGUI->SetSelection( newSelection );
	m_pGUI->SetStateInt( "serverList_selid_0", -1 );
	listGUI->SetStateChanges( true );
	GUIUpdateSelected();
	UpdateBrowserStatus();
}

/*
================
idServerScan::IsFiltered
================
*/
bool idServerScan::IsFiltered( const networkServer_t server ) {
	int i;
	const idKeyValue *keyval;

	// openQ4 supplies its own game modules on each platform. The legacy mask
	// describes downloadable proprietary game DLLs, not protocol compatibility.
	if ( server.clients < 0 || server.clients > MAX_ASYNC_CLIENTS ) {
		return true;
	}
	// password filter
	keyval = server.serverInfo.FindKey( "si_usePass" );
	if ( keyval && gui_filter_password.GetInteger() == 1 ) {
		// show passworded only
		if ( keyval->GetValue()[ 0 ] == '0' ) {
			return true;
		}
	} else if ( keyval && gui_filter_password.GetInteger() == 2 ) {
		// show no password only
		if ( keyval->GetValue()[ 0 ] != '0' ) {
			return true;
		}
	}
	// players filter
	keyval = server.serverInfo.FindKey( "si_maxPlayers" );
	if ( keyval ) {
		const int capacity = atoi( keyval->GetValue() );
		const bool full = capacity > 0 && server.clients >= capacity;
		if ( gui_filter_players.GetInteger() == 1 && full ) {
			return true;
		} else if ( gui_filter_players.GetInteger() == 2 && ( !server.clients || full ) ) {
			return true;
		}
	}
	// gametype filter
	keyval = server.serverInfo.FindKey( "si_gameType" );
	if ( gui_filter_gameType.GetInteger() ) {
		if ( keyval == NULL ) {
			return true;
		}
		const int requestedGameType = gui_filter_gameType.GetInteger() - 1;
		i = 0;
		while ( l_gameTypes[ i ] ) {
			if ( !keyval->GetValue().Icmp( l_gameTypes[ i ] ) ) {
				break;
			}
			i++;
		}
		// Unknown/hidden tokens fail closed.  The inherited implementation let
		// every unrecognized modern gametype bypass an active filter.
		if ( l_gameTypes[ i ] == NULL || i != requestedGameType ) {
			return true;
		}
	}
	// idle server filter
	keyval = server.serverInfo.FindKey( "si_idleServer" );
	if ( keyval && !gui_filter_idle.GetInteger() ) {
		if ( !keyval->GetValue().Icmp( "1" ) ) {
			return true;
		}
	}

	const idStr mod = ServerBrowserMod( server.serverInfo.GetString( "fs_game" ) );
	if ( gui_filter_mod.GetString()[0] && idStr::Icmp( gui_filter_mod.GetString(), mod.c_str() ) ) {
		return true;
	}

	return false;
}

/*
================
idServerScan::Cmp
================
*/

int idServerScan::Cmp( const int *a, const int *b ) {
	const networkServer_t &serv1 = (*l_serverScan)[ *a ];
	const networkServer_t &serv2 = (*l_serverScan)[ *b ];
	idStr s1, s2;
	int ret;

	switch ( l_serverScan->m_sort ) {
		case SORT_NONE:
			return ( *a > *b ) - ( *a < *b );
		case SORT_FAVORITE:
			return static_cast<int>( l_serverScan->IsFavorite( serv2.adr ) ) - static_cast<int>( l_serverScan->IsFavorite( serv1.adr ) );
		case SORT_PASSWORD:
			return static_cast<int>( serv2.serverInfo.GetBool( "si_usePass" ) ) - static_cast<int>( serv1.serverInfo.GetBool( "si_usePass" ) );
		case SORT_DEDICATED:
			return static_cast<int>( serv2.serverInfo.GetInt( "net_serverDedicated" ) != 0 ) - static_cast<int>( serv1.serverInfo.GetInt( "net_serverDedicated" ) != 0 );
		case SORT_PUNKBUSTER:
			return static_cast<int>( serv2.serverInfo.GetBool( "sv_punkbuster" ) ) - static_cast<int>( serv1.serverInfo.GetBool( "sv_punkbuster" ) );
		case SORT_REPEATER:
			return static_cast<int>( serv2.serverInfo.GetBool( "si_repeater" ) ) - static_cast<int>( serv1.serverInfo.GetBool( "si_repeater" ) );
		case SORT_PING:
			ret = serv1.ping < serv2.ping ? -1 : ( serv1.ping > serv2.ping ? 1 : 0 );
			return ret;
		case SORT_SERVERNAME:
			s1 = ServerBrowserText( serv1.serverInfo.GetString( "si_name" ) );
			s2 = ServerBrowserText( serv2.serverInfo.GetString( "si_name" ) );
			return s1.Icmp( s2 );
		case SORT_PLAYERS:
			ret = serv1.clients < serv2.clients ? -1 : ( serv1.clients > serv2.clients ? 1 : 0 );
			return ret;
		case SORT_GAMETYPE:
			serv1.serverInfo.GetString( "si_gameType", "", s1 );
			serv2.serverInfo.GetString( "si_gameType", "", s2 );
			return s1.Icmp( s2 );
		case SORT_MAP:
			serv1.serverInfo.GetString( "si_mapName", "", s1 );
			serv2.serverInfo.GetString( "si_mapName", "", s2 );
			return s1.Icmp( s2 );
		case SORT_GAME:
			serv1.serverInfo.GetString( "fs_game", "", s1 );
			serv2.serverInfo.GetString( "fs_game", "", s2 );
			return s1.Icmp( s2 );
	}
	return 0;
}

/*
================
idServerScan::SetSorting
================
*/
void idServerScan::SetSorting( serverSort_t sort ) {
	l_serverScan = this;
	if ( sort == m_sort ) {
		m_sortAscending = !m_sortAscending;
	} else {
		m_sort = sort;
		m_sortAscending = true; // is the default for any new sort
		m_sortedServers.Sort( idServerScan::Cmp );
	}
	// trigger a redraw
	ApplyFilter();
}

void idServerScan::ResetSorting() {
	m_sort = SORT_NONE;
	m_sortAscending = true;
	l_serverScan = this;
	m_sortedServers.Sort( idServerScan::Cmp );
	ApplyFilter();
}

