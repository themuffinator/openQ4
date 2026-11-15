//----------------------------------------------------------------
// MapDatabase.h
//
// openQ4: one bounded database of the multiplayer maps a server can play.
//
// For each discovered map it records the gametypes the map supports, its
// player-count bounds, its per-map settings and whether it takes part in the
// rotation, in votes and in the player queue.  The rotation, the player queue
// and the end-of-match pick read the same entries, so every menu, filter and
// vote that lists maps lists these and nothing else.
//
// The core owns no cvars, console commands, files, decls or network messages.
// An adapter (MapDatabaseHost.cpp) discovers the installed maps, feeds them
// in and carries out what the core decides.
//----------------------------------------------------------------

#ifndef __MP_MAP_DATABASE_H__
#define __MP_MAP_DATABASE_H__

#include <stdint.h>
#include <stddef.h>

static const int MP_MAPDB_MAX_MAPS = 256;
static const int MP_MAPDB_TOKEN_BYTES = 64;			// matches MP_SERIES_MAP_TOKEN_BYTES
static const int MP_MAPDB_NAME_BYTES = 64;
static const int MP_MAPDB_MAX_SETTINGS = 512;		// shared by every map
static const int MP_MAPDB_MAX_MAP_SETTINGS = 16;	// for one map
static const int MP_MAPDB_SETTING_KEY_BYTES = 48;
static const int MP_MAPDB_SETTING_VALUE_BYTES = 80;
static const int MP_MAPDB_MAX_QUEUE = 32;			// one entry a player
static const int MP_MAPDB_MAX_VOTERS = 32;
static const int MP_MAPDB_PICK_CANDIDATES = 3;
static const int MP_MAPDB_HISTORY = 8;
static const int MP_MAPDB_MAX_PLAYERS = 64;			// bounds are clamped to this
static const int MP_MAPDB_ERROR_BYTES = 192;
static const int MP_MAPDB_MAX_TEXT_BYTES = 262144;	// an override file larger than this is refused

// Entry flags.
static const int MP_MAPDB_ROTATION		= 1 << 0;	// takes part in the rotation
static const int MP_MAPDB_VOTE			= 1 << 1;	// may be called in a map vote and offered in a pick
static const int MP_MAPDB_QUEUE			= 1 << 2;	// a player may queue it
static const int MP_MAPDB_STOCK			= 1 << 3;	// ships with Quake 4 or its expansion
static const int MP_MAPDB_OVERRIDDEN	= 1 << 4;	// the server's override file changed it
static const int MP_MAPDB_DEFAULT_FLAGS	= MP_MAPDB_ROTATION | MP_MAPDB_VOTE | MP_MAPDB_QUEUE;

struct mpMapSetting {
	char				key[ MP_MAPDB_SETTING_KEY_BYTES ];
	char				value[ MP_MAPDB_SETTING_VALUE_BYTES ];
};

struct mpMapEntry {
	char				token[ MP_MAPDB_TOKEN_BYTES ];	// canonical path: "mp/q4dm1"
	char				name[ MP_MAPDB_NAME_BYTES ];	// display name or localization key
	uint32_t			modes;							// bit per gametype id the map supports
	int					minPlayers;						// 0: no lower bound
	int					maxPlayers;						// 0: no upper bound
	int					flags;
	int					hostIndex;						// the adapter's own handle (map decl index), -1 without one
	int					settingCount;
	int					settings[ MP_MAPDB_MAX_MAP_SETTINGS ];	// indices into the shared pool
};

// Why a map was chosen.  Append only: the values are logged.
typedef enum {
	MP_MAPDB_SOURCE_NONE = 0,			// nothing playable; stay on the current map
	MP_MAPDB_SOURCE_QUEUE,				// a player's queued map
	MP_MAPDB_SOURCE_PICK,				// the end-of-match pick
	MP_MAPDB_SOURCE_ROTATION,			// the next map of the rotation that fits the player count
	MP_MAPDB_SOURCE_ROTATION_UNBOUNDED,	// no map fits the player count; the rotation's next
	MP_MAPDB_SOURCE_RANDOM,				// a shuffled rotation's draw that fits the player count
	MP_MAPDB_SOURCE_RANDOM_UNBOUNDED,	// no map fits the player count; a shuffled rotation's draw
	MP_MAPDB_SOURCE_COUNT
} mpMapSource_t;

struct mpMapSelection {
	int					index;			// entry index, -1 with MP_MAPDB_SOURCE_NONE
	mpMapSource_t		source;
	int					owner;			// the queueing player's slot, -1 otherwise
	int					votes;			// the pick's winning vote count
};

// Append only: the values are logged and sent to the caller.
typedef enum {
	MP_MAPDB_QUEUE_ADDED = 0,
	MP_MAPDB_QUEUE_REPLACED,			// the player's earlier entry gave way; it keeps its place
	MP_MAPDB_QUEUE_UNKNOWN_MAP,
	MP_MAPDB_QUEUE_NOT_QUEUEABLE,		// the database keeps the map out of the queue
	MP_MAPDB_QUEUE_WRONG_MODE,			// the map does not support the gametype
	MP_MAPDB_QUEUE_CURRENT_MAP,
	MP_MAPDB_QUEUE_DUPLICATE,			// another player queued it first
	MP_MAPDB_QUEUE_FULL,
	MP_MAPDB_QUEUE_BAD_OWNER,
	MP_MAPDB_QUEUE_RESULT_COUNT
} mpMapQueueResult_t;

struct mpMapQueueEntry {
	int					mapIndex;
	int					owner;			// client slot
	uint64_t			ownerId;		// the connection that owns the slot
};

struct mpMapPick {
	bool				open;
	int					candidateCount;
	int					candidates[ MP_MAPDB_PICK_CANDIDATES ];
	int					votes[ MP_MAPDB_PICK_CANDIDATES ];
	int					choice[ MP_MAPDB_MAX_VOTERS ];	// candidate each slot chose, -1 for none
};

// Resolves a gametype name of the override file to its id, -1 when unknown.
typedef int ( *mpMapModeResolver_t )( const char *name );

struct mpMapOverrideReport {
	int					mapsChanged;
	int					mapsUnknown;		// "map" blocks for maps that are not installed
	int					rotationListed;		// maps the rotation block placed
	int					rotationUnknown;	// rotation tokens that are not installed
	int					settingsDropped;	// settings past a map's or the pool's capacity
	int					line;				// the line of the first error, 0 without one
	char				error[ MP_MAPDB_ERROR_BYTES ];
};

class mpMapDatabase {
public:
						mpMapDatabase( void );

	void				Clear( void );

	// Canonical map token: slashes forward, no "maps/" prefix, no extension,
	// lower case.  False when the result is empty, too long or unsafe.
	static bool			NormalizeToken( const char *path, char token[ MP_MAPDB_TOKEN_BYTES ] );
	static bool			IsSafeToken( const char *token );

	// Default bounds of a map nobody described: the stock tourney maps hold two
	// to four players, a map with no free-for-all mode needs four.
	static void			DefaultBounds( const char *token, uint32_t modes, uint32_t freeForAllModes,
							int &minPlayers, int &maxPlayers );
	static bool			IsStockToken( const char *token );

	// Discovery.  Returns the entry index, or -1 for an unsafe token, a
	// duplicate or a full database.
	int					AddMap( const char *path, const char *name, uint32_t modes, int hostIndex );
	bool				SetBounds( int index, int minPlayers, int maxPlayers );
	bool				SetFlags( int index, int flags );
	bool				AddSetting( int index, const char *key, const char *value );
	void				ClearSettings( int index );

	int					Num( void ) const { return count; }
	const mpMapEntry *	Entry( int index ) const;
	int					Find( const char *path ) const;
	int					FindHostIndex( int hostIndex ) const;
	const mpMapSetting *Setting( int index, int setting ) const;
	uint32_t			Revision( void ) const { return revision; }

	bool				Supports( int index, int gameType ) const;
	bool				FitsPlayers( int index, int playerCount ) const;
	// Maps of the gametype, in database order; any map with gameType < 0.
	int					Collect( int gameType, int requiredFlags, int *indices, int maximum ) const;

	// The server's override text; see docs/user/server-setup.md.  Statements
	// apply in file order.  False on a syntax error: the report names the line
	// and the database is left as it was before the call.
	bool				ApplyOverrides( const char *text, int length, mpMapModeResolver_t resolveMode,
							mpMapOverrideReport &report );

	// Rotation.  An explicit list (the override file's rotation block or the
	// legacy si_mapCycle string) fixes the order; without one every map
	// flagged for rotation takes part in database order.
	void				ClearRotation( void );
	bool				AppendRotation( const char *path );
	// Semicolon or whitespace separated legacy list; returns the maps placed.
	// unknown counts the names left out: not installed, or listed twice.
	int					SetRotationFromList( const char *list, int *unknown = NULL );
	bool				HasExplicitRotation( void ) const { return rotationExplicit; }
	int					RotationNum( void ) const;
	int					RotationEntry( int position ) const;

	// The rotation's map after the current one for this gametype and player
	// count.  A rotation that offers no map inside its bounds ignores them
	// rather than stall.
	mpMapSelection		NextInRotation( int currentIndex, int gameType, int playerCount ) const;
	// A draw from the rotation that avoids the current and recently played maps.
	mpMapSelection		RandomFromRotation( int currentIndex, int gameType, int playerCount, uint32_t seed ) const;

	// Player queue: one entry a player, played in the order it was filled.
	mpMapQueueResult_t	QueueAdd( int owner, uint64_t ownerId, int mapIndex, int gameType, int currentIndex );
	bool				QueueRemoveOwner( int owner );
	// Drops entries whose owner left or changed; isCurrent( slot, id ) says.
	int					QueuePrune( bool ( *isCurrent )( int owner, uint64_t ownerId ) );
	void				QueueClear( void );
	int					QueueNum( void ) const { return queueCount; }
	const mpMapQueueEntry *QueueEntry( int position ) const;
	int					QueueFindOwner( int owner ) const;

	// End-of-match pick.  The first candidate is the rotation's own next map;
	// the others are drawn from the maps that fit the player count (from any
	// playable map when none fits and the rotation itself ignores the bounds).
	bool				PickOpen( int currentIndex, int gameType, int playerCount, uint32_t seed );
	bool				PickVote( int voter, int candidate );
	void				PickRemoveVoter( int voter );
	void				PickClose( void );
	const mpMapPick &	Pick( void ) const { return pick; }
	// The candidate with the most votes; a tie goes to the seeded draw among
	// the leaders, and no vote at all to the first candidate.
	int					PickLeader( uint32_t seed, int *votes = NULL ) const;

	// The next map: a queued map first, then an open pick, then the rotation.
	// consume takes the queued entry and closes the pick; without it the call
	// only predicts.
	mpMapSelection		ChooseNext( int currentIndex, int gameType, int playerCount, bool shuffle,
							uint32_t seed, bool consume );

	// Recently played maps, newest first.
	void				NotePlayed( int index );
	bool				RecentlyPlayed( int index ) const;
	int					HistoryNum( void ) const { return historyCount; }
	int					HistoryEntry( int position ) const;

	bool				ValidateInvariants( void ) const;

private:
	bool				RotationEligible( int index, int gameType ) const;
	int					RotationPosition( int index ) const;
	int					RotationSlots( void ) const;
	int					RotationSlot( int position ) const;
	int					AllocateSetting( void );

	mpMapEntry			entries[ MP_MAPDB_MAX_MAPS ];
	int					count;
	mpMapSetting		settingPool[ MP_MAPDB_MAX_SETTINGS ];
	bool				settingUsed[ MP_MAPDB_MAX_SETTINGS ];
	uint32_t			revision;

	int					rotation[ MP_MAPDB_MAX_MAPS ];
	int					rotationCount;
	bool				rotationExplicit;

	mpMapQueueEntry		queue[ MP_MAPDB_MAX_QUEUE ];
	int					queueCount;

	mpMapPick			pick;

	int					history[ MP_MAPDB_HISTORY ];
	int					historyCount;
};

#endif // __MP_MAP_DATABASE_H__
