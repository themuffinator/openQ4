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

#ifndef __SERVERSCAN_H__
#define __SERVERSCAN_H__

/*
===============================================================================

	Scan for servers, on the LAN or from a list
	Update a listDef GUI through usage of idListGUI class
	When updating large lists of servers, sends out getInfo in small batches to avoid congestion

===============================================================================
*/

// storage for incoming servers / server scan
typedef struct {
	netadr_t	adr;
	int			id;
	int			time;
} inServer_t;

// the menu gui uses a hard-coded control type to display a list of network games
typedef struct {
	netadr_t	adr;
	idDict		serverInfo;
	int			ping;
	int			id;			// idnet mode sends an id for each server in list
	int			clients;
	char		nickname[ MAX_NICKLEN ][ MAX_ASYNC_CLIENTS ];
	short		pings[ MAX_ASYNC_CLIENTS ];
	int			rate[ MAX_ASYNC_CLIENTS ];
	int			OSMask;
    int			challenge;
} networkServer_t;

typedef enum {
	SORT_PING,
	SORT_SERVERNAME,
	SORT_PLAYERS,
	SORT_GAMETYPE,
	SORT_MAP,
	SORT_GAME,
	SORT_FAVORITE,
	SORT_PASSWORD,
	SORT_DEDICATED,
	SORT_PUNKBUSTER,
	SORT_REPEATER,
	SORT_NONE
} serverSort_t;

class idServerScan : public idList<networkServer_t> {
public:	
						idServerScan( );
	
	int					InfoResponse( networkServer_t &server );

	// add an internet server - ( store a numeric id along with it )
	void				AddServer( int id, const char *srv );

	// we are going to feed server entries to be pinged
	// if timeout is true, use a timeout once we start AddServer to trigger EndServers and decide the scan is done
	void				StartServers( bool timeout );
	// we are done filling up the list of server entries
	void				EndServers( );

	// scan the current list of servers - used for refreshes and while receiving a fresh list
	void				NetScan( );

	// clear
	void				Clear( );

	// called each game frame. Updates the scanner state, takes care of ongoing scans
	void				RunFrame( );
	
	typedef enum {
		IDLE = 0,
		WAIT_ON_INIT,
		LAN_SCAN,
		NET_SCAN
	} scan_state_t;

	scan_state_t		GetState() { return scan_state; }
	void				SetState( scan_state_t );
	
	bool				GetBestPing( networkServer_t &serv );

						// prepare for a LAN scan. idAsyncClient does the network job (UDP broadcast), we do the storage
	void				SetupLANScan( );

	void				GUIConfig( idUserInterface *pGUI, const char *name );
						// update the GUI fields with information about the currently selected server
	void				GUIUpdateSelected( void );
	void				GUIInit();
	bool				GetSelectedAddress( idStr &address );
	void				ToggleFavorite();
	// Adds a server to the favorites or takes it off them, as ToggleFavorite
	// does for the stock list's selection. False when the favorites cannot
	// change (no such server, the limit reached, the file not written).
	bool				ToggleFavoriteServer( int index );
	void				AddFavoriteServers();
	void				UpdateFilterByMod( int direction );
	// FIND PLAYER/CLAN: only servers with a player whose name holds `text`
	// (colors removed, in any case) pass the filter; empty text clears it.
	// Retail asked the master server, which no master answers now; a clan
	// tag is part of a player's name.
	void				SetPlayerSearch( const char *text );
	const char *		GetPlayerSearch() const { return playerSearch.c_str(); }
	// The list's own filters, for a menu that draws its own list: full and
	// empty servers hidden (gui_filter_players' two bits, in the order of the
	// stock strings' "Show All; Hide Full; Hide Empty; Hide Full and Empty"),
	// only servers without a password (gui_filter_password 2), and one game
	// type (gui_filter_gameType: 0 for every type, else 1 past its index in
	// the browser's registry). Each change filters the list again.
	typedef enum {
		BROWSER_FILTER_FULL,
		BROWSER_FILTER_EMPTY,
		BROWSER_FILTER_PASSWORD
	} browserFilter_t;
	bool				IsFilterSet( browserFilter_t filter ) const;
	void				ToggleFilter( browserFilter_t filter );
	int					GetGameTypeFilter() const;
	// The previous or next game type, round through every type; a filter the
	// registry does not name steps from every type.
	void				StepGameTypeFilter( int direction );
	// A game type filter's name in the player's language; "" for every type
	// or a filter the registry does not name.
	static const char *	GameTypeFilterName( int filter );
	void				ResetSorting();
	// Every filter a menu offers back at showing every server: the players,
	// password, game type and mod filters and the player search, the list
	// filtered once.
	void				ClearFilters();

	void				Shutdown( );

	void				ApplyFilter( );

						// there is an internal toggle, call twice with same sort to switch
	void				SetSorting( serverSort_t sort );

	int					GetChallenge( );

	// For a menu that draws its own list (the retained Join Game page): the
	// servers the filter passes, in the sort's walking order as ApplyFilter
	// lists them; whether one is a favorite; the sort; and a revision that
	// moves whenever any of them or the scan's state may have changed.
	void				VisibleServers( idList<int> &out );
	bool				IsFavoriteServer( int index ) const;
	serverSort_t		GetSorting( bool &ascending ) const { ascending = m_sortAscending; return m_sort; }
	int					GetRevision() const { return revision; }
	// A server's si_gameType as the browser names it, in the player's language.
	static const char *	GameTypeName( const char *type );
	// Whether a server's si_map is a map path the browser may look up (no
	// parent references, no absolute paths, plain path characters).
	static bool			ValidMapPath( const char *path );
	// The scan's progress, as retail's browser showed it under the list
	// (gui::loadNotice over gui::loadPct's bar, which this publishes too): the
	// notice names the scan's stage or the server that answered last, and the
	// fraction is of the listed servers the scan has heard from or given up on
	// (a LAN scan's, of its reply window). A completed scan keeps its count
	// and a full bar until the next scan or a clear.
	const char *		GetLoadNotice() const { return loadNotice.c_str(); }
	float				GetLoadProgress() const { return loadPct; }
	// A server's details as the stock browser's tooltip shows them: its
	// address, its players' names and its settings as "label: value" lines
	// (the map, the time limit, the limit its game type ends on, team damage,
	// team autobalance, pure, spectators), its text read as the browser reads
	// a server's. False for a server the scan does not hold.
	bool				GetServerDetails( int index, idStr &address, idStrList &players, idStrList &settings ) const;
	// The stock browser's server tooltip, kept each frame the main menu runs:
	// the row the pointer is over (<list>_hover, which the list draws) shows
	// its server's details, and no row hides them.
	void				GUIUpdateHover();
	// openq4_browser hover: a visible row treated as the hovered one, so the
	// tooltip shows without a pointer; -1 follows the pointer again.
	void				SetHoverOverride( int row ) { hoverOverride = row; tooltipServer = -2; }

private:
	static const int	MAX_PINGREQUESTS 	= 32;		// how many servers to query at once
	static const int	REPLY_TIMEOUT 		= 999;		// how long should we wait for a reply from a game server
	static const int	INCOMING_TIMEOUT	= 1500;		// when we got an incoming server list, how long till we decide the list is done
	static const int	REFRESH_START		= 10000;	// how long to wait when sending the initial refresh request

	scan_state_t		scan_state;
	
	bool				incoming_net;	// set to true while new servers are fed through AddServer
	bool				incoming_useTimeout;
	int					incoming_lastTime;
	
	int					lan_pingtime;	// holds the time of LAN scan
	
						// servers we're waiting for a reply from
						// won't exceed MAX_PINGREQUESTS elements
						// holds index of net_servers elements, indexed by 'from' string
	idDict				net_info;		

	idList<inServer_t>	net_servers;
						// where we are in net_servers list for getInfo emissions ( NET_SCAN only )
						// we may either be waiting on MAX_PINGREQUESTS, or for net_servers to grow some more ( through AddServer )
	int					cur_info;

	idUserInterface		*m_pGUI;
	idListGUI *			listGUI;

	serverSort_t		m_sort;
	bool				m_sortAscending;
	idList<int>			m_sortedServers;	// use ascending for the walking order

	idStr				screenshot;
	idStr				playerSearch;		// see SetPlayerSearch
	idStrList			favorites;
	bool				favoritesLoaded;
	int					challenge;			// challenge for current scan
	
	int					endWaitTime;		// when to stop waiting on a port init
	int					revision;			// see GetRevision

	idStr				loadNotice;			// see GetLoadNotice
	float				loadPct;
	idStr				lastResponder;		// the server that answered this scan last, as the notice names it
	int					scanned;			// servers the last completed scan covered; -1 until one completes

	idStr				listName;			// the stock list's name (GUIConfig), whose _hover the list draws
	int					hoverOverride;		// see SetHoverOverride
	int					tooltipServer;		// the server the tooltip shows (-1 none, -2 to publish again)
	int					tooltipRevision;	// the scan's revision when it was published

private:
	void				LocalClear( );		// we need to clear some internal data as well

	void				EmitGetInfo( netadr_t &serv );
	void				GUIAdd( int id, const networkServer_t &server );
	bool				IsFiltered( const networkServer_t &server );
	void				UpdateBrowserStatus();
	void				UpdateLoadProgress();
	void				SetLoadProgress( const char *notice, float pct );
	void				LoadFavorites();
	bool				IsFavorite( const netadr_t &address ) const;

	static int			Cmp( const int *a, const int *b );
};

#endif /* !__SERVERSCAN_H__ */
