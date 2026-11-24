//----------------------------------------------------------------
// StatWindow.h
//
// Copyright 2002-2005 Raven Software
//----------------------------------------------------------------

#ifndef __STATWINDOW_H__
#define __STATWINDOW_H__

/*
===============================================================================

Stat selection window

===============================================================================
*/

class rvStatWindow {
public:
	rvStatWindow();
	void						SetupStatWindow( idUserInterface* statHud, bool useSpectator = false );
	void						SelectPlayer( int clientNum );
	int							ClientNumFromSelection( int selectionIndex, int selectionTeam );
	// openQ4: the list-lookup half of ClientNumFromSelection with no side effects -
	// it neither rewrites the gui selection keys nor warns.  A poller that only
	// wants to know which client a live selection points at must use this one.
	int							ResolveSelection( int selectionIndex, int selectionTeam ) const;
	void						ClearWindow( void );
	int							GetSelectedClientNum( int* selectionIndexOut, int* selectionTeamOut );
	// openQ4: asks the server for a client's stats, unless a request for them
	// is already in flight
	void						RequestStats( int clientNum );
private:
	idList<idPlayer*>			stroggPlayers;
	idList<idPlayer*>			marinePlayers;
	idList<idPlayer*>			players;
	idList<idPlayer*>			spectators;
	
	idUserInterface*			statHud;

	// openQ4: last time we asked the server for each client's stats, so a held stats key doesn't
	// re-send the request every frame until the reply lands
	int							lastStatRequestTime[ MAX_CLIENTS ];
};


#endif
