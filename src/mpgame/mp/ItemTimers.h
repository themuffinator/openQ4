//----------------------------------------------------------------
// ItemTimers.h
//
// openQ4 item timers on casual servers (MP-15). si_itemTimers says who may see
// when the powerups, mega health and armour come back: nobody, spectators, or
// everyone including the players. The server tracks the items, and sends the
// list once a second only to the clients the policy admits, so a client that
// is not admitted holds nothing to show.
//
// Managed matches keep their own disclosure policy (mp/match/): this stream
// stays silent while one runs.
//
// The policy core below owns no engine types, so tools/tests/native can
// compile it alone.
//----------------------------------------------------------------

#ifndef __GAME_MP_ITEMTIMERS_H__
#define __GAME_MP_ITEMTIMERS_H__

// si_itemTimers
typedef enum {
	ITEMTIMERS_OFF = 0,
	ITEMTIMERS_SPECTATORS,		// people watching, never people playing
	ITEMTIMERS_EVERYONE,
	ITEMTIMERS_POLICY_COUNT
} itemTimerPolicy_t;

typedef enum {
	ITEMTIMER_NONE = 0,
	ITEMTIMER_QUAD_DAMAGE,
	ITEMTIMER_HASTE,
	ITEMTIMER_REGENERATION,
	ITEMTIMER_INVISIBILITY,
	ITEMTIMER_MEGA_HEALTH,
	ITEMTIMER_LARGE_ARMOR,
	ITEMTIMER_SMALL_ARMOR,
	ITEMTIMER_KIND_COUNT
} itemTimerKind_t;

static const int MP_ITEMTIMERS_MAX_ROWS = 12;
// tracked items a send collects across every instance (Tourney arenas each have theirs)
static const int MP_ITEMTIMERS_MAX_TRACKED = 256;
// the HUD lists the first six: the powerups and mega health, then armour
static const int MP_ITEMTIMERS_HUD_ROWS = 6;
static const int MP_ITEMTIMERS_SEND_MSEC = 1000;
static const int MP_ITEMTIMERS_FRESH_MSEC = 3500;

// The wire state of one item, in a byte.
static const int MP_ITEMTIMER_STATE_AVAILABLE = 0;
static const int MP_ITEMTIMER_STATE_MAX_SECONDS = 250;
static const int MP_ITEMTIMER_STATE_HELD = 255;		// taken, and its clock has not started

// Who the policy admits. wantsToWatch is a client that chose to spectate; a
// player waiting out a round or a respawn is still playing and is not one.
inline bool MPItemTimersAdmit( int policy, bool managedMatch, bool wantsToWatch ) {
	if ( managedMatch ) {
		return false;
	}
	if ( policy == ITEMTIMERS_EVERYONE ) {
		return true;
	}
	return policy == ITEMTIMERS_SPECTATORS && wantsToWatch;
}

// Lower sorts first: the items a match turns on, then heavy armour, then the rest.
inline int MPItemTimerPriority( int kind ) {
	switch ( kind ) {
		case ITEMTIMER_QUAD_DAMAGE:
		case ITEMTIMER_HASTE:
		case ITEMTIMER_REGENERATION:
		case ITEMTIMER_INVISIBILITY:
		case ITEMTIMER_MEGA_HEALTH:
			return 0;
		case ITEMTIMER_LARGE_ARMOR:
			return 1;
		case ITEMTIMER_SMALL_ARMOR:
			return 2;
		default:
			return 3;
	}
}

// Seconds left, rounded up so a timer reads 1 until the item is there.
inline int MPItemTimerPackState( int respawnAtMsec, int nowMsec ) {
	if ( respawnAtMsec < 0 ) {
		return MP_ITEMTIMER_STATE_HELD;
	}
	if ( respawnAtMsec == 0 || respawnAtMsec <= nowMsec ) {
		return MP_ITEMTIMER_STATE_AVAILABLE;
	}
	const int seconds = ( respawnAtMsec - nowMsec + 999 ) / 1000;
	return seconds > MP_ITEMTIMER_STATE_MAX_SECONDS ? MP_ITEMTIMER_STATE_MAX_SECONDS : seconds;
}

#ifndef MP_CLIENT_OPTIONS_CORE_ONLY
class idItem;

typedef struct itemTimerRow_s {
	int		kind;			// itemTimerKind_t
	int		entityNumber;
	int		state;			// wire state when it arrived
	int		respawnAt;		// client real time the item returns, 0 when it is there
	int		totalMsec;		// the whole respawn while it counts, 0 when not known
} itemTimerRow_t;

// a tracked item as a send collects it once, before choosing each recipient's rows
typedef struct itemTimerTracked_s {
	int		entityNumber;
	int		kind;
	int		priority;
	int		instance;
} itemTimerTracked_t;

/*
===============================================================================

	rvItemTimers

===============================================================================
*/
class rvItemTimers {
public:
							rvItemTimers( void );

	void					Clear( void );

	static itemTimerKind_t	KindForItem( const idItem *item );
	// the entity definition that names a kind on both ends
	static const char *		DefNameForKind( int kind );

	// server
	void					OnPickup( const idItem *item, float respawnSeconds );
	void					OnHeld( const idItem *item );
	void					OnAvailable( const idItem *item );
	// a restart in place puts every item back: nothing is waiting to return
	void					OnMapRestart( int instance );
	void					ServerFrame( void );
	// a pause moved every deadline in the match forward by this much
	void					ShiftMatchTime( int deltaMsec );

	// client
	void					ClientReceive( const idBitMsg &msg );

	// What this end may show now. A listen server's host reads the tracker
	// directly, under the same policy as everyone else.
	int						GetRows( itemTimerRow_t *rows, int maxRows ) const;

private:
	int						BuildRows( itemTimerRow_t *rows, int maxRows, int instance ) const;
	int						CollectTracked( itemTimerTracked_t *tracked, int maxTracked ) const;
	int						RowsFromTracked( const itemTimerTracked_t *tracked, int trackedCount, itemTimerRow_t *rows, int maxRows, int instance ) const;
	bool					Tracked( const idItem *item ) const;
	// the respawn an item's definition gives this game type, in msec (0 when none)
	static int				DefinedRespawnMsec( const idEntity *ent );

	// server: game time the item returns, 0 while it is there, -1 while held
	int						respawnAt[ MAX_GENTITIES ];
	// server: the length of the respawn counting down, for the host's own HUD
	int						respawnTotal[ MAX_GENTITIES ];
	int						nextSendTime;

	// client
	itemTimerRow_t			clientRows[ MP_ITEMTIMERS_MAX_ROWS ];
	int						clientRowCount;
	int						clientReceived;		// realClientTime, 0 for never
};
#endif

#endif	/* !__GAME_MP_ITEMTIMERS_H__ */
