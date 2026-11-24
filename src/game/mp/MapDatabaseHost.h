//----------------------------------------------------------------
// MapDatabaseHost.h
//
// openQ4: the game's adapter around mpMapDatabase.
//
// Discovers the installed multiplayer maps from their mapDefs, applies the
// server's override file, and carries out the rotation, the player queue,
// the end-of-match pick and the per-map settings on a running server.  Every
// list of maps the multiplayer game shows or accepts comes from here.
//----------------------------------------------------------------

#ifndef __MP_MAP_DATABASE_HOST_H__
#define __MP_MAP_DATABASE_HOST_H__

#include "MapDatabase.h"

// g_mapRotation values.  Append only: the cvar is archived.
enum {
	MP_MAP_ROTATION_STAY = 0,		// replay the current map
	MP_MAP_ROTATION_ORDER,			// the database rotation, in order
	MP_MAP_ROTATION_SHUFFLE,		// the database rotation, shuffled
	MP_MAP_ROTATION_SCRIPT,			// the legacy g_mapCycle script
	MP_MAP_ROTATION_COUNT
};

// What idGameLocal::NextMap does with the host's decision.
typedef enum {
	MP_MAP_ADVANCE_STAY = 0,		// no change of map
	MP_MAP_ADVANCE_CHANGED,			// si_map names the next map
	MP_MAP_ADVANCE_SCRIPT			// run the legacy cycle script
} mpMapAdvance_t;

// Client to server requests carried by GAME_RELIABLE_MESSAGE_MAPDB.  Append
// only: these values are the wire format.
enum {
	MP_MAP_REQUEST_QUEUE_LIST = 0,
	MP_MAP_REQUEST_QUEUE_SET,		// + map path string
	MP_MAP_REQUEST_QUEUE_CLEAR,
	MP_MAP_REQUEST_PICK,			// + candidate byte, 0 based
	MP_MAP_REQUEST_COUNT
};

class mpMapDatabaseHost {
public:
						mpMapDatabaseHost( void );

	// Builds the database on first use and again when the installed maps or
	// si_mapCycle changed.
	void				Refresh( void );
	// Discovers again and reads the override file again; the queue and the
	// history are carried over by map path.
	void				Rebuild( void );
	void				Shutdown( void );

	const mpMapDatabase &Database( void );
	int					Find( const char *path );
	// The entry of a map decl index, as fileSystem->GetMapDecl numbers them.
	int					FindDecl( int declIndex );
	// The map's decl.  The engine hands out one shared dictionary: the result
	// is valid until the next call.
	const idDict *		Decl( int entryIndex );
	const idDict *		DeclForPath( const char *path );
	const char *		DisplayName( int entryIndex );

	// What the database says about a map and a gametype; the mapDef's own
	// word for a map the database does not hold.
	bool				Supports( const char *path, int gameType );

	// Decl indices of the maps a list shows, in database order.
	void				CollectDecls( int gameType, int requiredFlags, idList<int> &declIndices );

	// --- server ---

	// The engine is about to read the server info for a change to this map:
	// revert the settings of the map that is ending and apply the new map's.
	void				ServerPrepareMap( const char *map );
	void				ServerMapStarted( const char *map );
	void				ServerMapShutdown( void );
	void				NonServerMapStarted( void );
	// Once a second: publishes the predicted next map.
	void				ServerThink( void );
	// The match ended: offer the pick if the server wants one.
	void				ServerMatchEnded( void );
	bool				ServerOfferPick( void );		// the end-of-match pick; true when one was offered
	void				ServerAnnounceNext( void );	// MP-12: the next map, at the intermission
	// Chooses the next map, consuming a queued map or the pick, and writes
	// si_map.
	mpMapAdvance_t		ServerAdvance( void );
	void				ServerClientDisconnect( int clientNum );
	void				ServerReceiveRequest( int clientNum, const idBitMsg &msg );
	// The map the rotation would load now, "" when it would stay.
	idStr				ServerPredictNext( mpMapSelection *selection = NULL );

	// --- client and console ---

	static void			MyMap_f( const idCmdArgs &args );
	static void			MapPick_f( const idCmdArgs &args );
	static void			MapDb_f( const idCmdArgs &args );
	static void			ArgCompletion_Map( const idCmdArgs &args, void ( *callback )( const char *s ) );

private:
	void				Discover( void );
	void				LoadOverrides( void );
	void				SyncRotation( bool force );
	void				RevertSettings( void );
	void				ApplySettings( int entryIndex );
	bool				SettingAllowed( const char *name, const char **reason ) const;
	int					CurrentEntry( void );
	int					PlayerCount( void ) const;
	bool				RotationActive( void ) const;
	bool				ClientRole( void ) const;
	int					NextGameType( void ) const;
	void				ServerQueueSet( int clientNum, const char *path );
	void				ServerQueueClear( int clientNum );
	void				ServerQueueList( int clientNum );
	void				ServerPick( int clientNum, int candidate );
	void				PublishServerInfo( bool force );
	void				Reply( int clientNum, const char *textId, const char *parm = "" );
	void				PrintList( void );
	void				PrintRotation( void );
	void				PrintQueue( void );

	struct appliedSetting_t {
		idStr			name;
		idStr			original;
		idStr			applied;
	};

	mpMapDatabase		database;
	bool				built;
	int					discoveredDecls;		// fileSystem->GetNumMaps() at the last build
	idStr				rotationList;			// the si_mapCycle the rotation was built from
	bool				rotationFromFile;		// the override file listed a rotation
	idStr				overrideRotation;		// its tokens, to restore when si_mapCycle is cleared

	idList<appliedSetting_t> appliedSettings;
	idStr				appliedMap;				// the map the applied settings belong to
	bool				mapChangePending;		// ServerPrepareMap ran; the map has not started yet
	bool				hostingNext;			// ServerPrepareMap ran for a map this process will serve

	idStr				advanceIssued;			// the map ServerAdvance last wrote to si_map
	idStr				intermissionAnnounced;	// MP-12: the map the intermission named; the change does not name it again
	int					advanceAttempts;

	uint32_t			mapSeed;				// one draw a map: shuffle and tie breaks
	int					nextThinkTime;
	idStr				publishedNext;
	idStr				publishedPick;
};

extern mpMapDatabaseHost	mpMapHost;

// A map decl by the engine's index.  The one place the multiplayer game
// reads fileSystem->GetMapDecl.
const idDict *			MPMapDecl( int declIndex );

#endif // __MP_MAP_DATABASE_HOST_H__
