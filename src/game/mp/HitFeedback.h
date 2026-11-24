//----------------------------------------------------------------
// HitFeedback.h
//
// openQ4 attacker side hit feedback: floating damage numbers, ported from
// Quake Live's damage plums.
//----------------------------------------------------------------

#ifndef __GAME_MP_HITFEEDBACK_H__
#define __GAME_MP_HITFEEDBACK_H__

class idPlayer;
class idEntity;

// Written into GAME_UNRELIABLE_MESSAGE_HITINFO so the client, not the server,
// decides which hits its own hud_damageNumbers setting wants to see.
typedef enum {
	HITFLAG_TEAM	= BIT( 0 ),		// victim shares the attacker's team
	HITFLAG_SELF	= BIT( 1 ),		// attacker damaged themselves
	HITFLAG_ARMOR	= BIT( 2 ),		// armour absorbed part of the hit
	HITFLAG_KILL	= BIT( 3 )		// the hit finished the victim
} hitFeedbackFlags_t;

// hud_damageNumbers
typedef enum {
	DAMAGENUMBERS_OFF = 0,
	DAMAGENUMBERS_OPPONENTS,		// what Quake Live shows
	DAMAGENUMBERS_ALL				// adds team damage and self damage
} damageNumbersMode_t;

// hud_damageNumberStyle.  One based, matching Quake Live's
// cg_damagePlumColorStyle so a player's muscle memory carries over.
typedef enum {
	DAMAGENUMBERSTYLE_FADE = 1,		// white through red across the damage range
	DAMAGENUMBERSTYLE_AMOUNT,		// one colour per damage band
	DAMAGENUMBERSTYLE_WEAPON		// one colour per weapon
} damageNumberStyle_t;

/*
===============================================================================

	rvDamageNumbers

	Client side slab of live damage numbers.  A number is a world position
	plus a screen space velocity: it is projected fresh every frame and then
	carried along a short ballistic arc in 2D, which is what makes Quake
	Live's plums read as attached to the target without ever intersecting
	geometry.

	Everything about a number is a pure function of its spawn record and the
	elapsed time, so there is no update pass to keep in step with the draw.

===============================================================================
*/
class rvDamageNumbers {
public:
	rvDamageNumbers( void );

	void					Clear( void );

	// client side staging, either off the wire or straight from the server
	// on a listen server
	void					Add( const idVec3 &origin, int damage, int weapon, int flags );
	void					ClientReceive( const idBitMsg &msg );

	// 2D pass, after the scene has been submitted for this view
	void					Draw( const renderView_t *view );

	// server side.  Gates on g_hitFeedback and decides nothing about
	// presentation - that is the recipient's business.  hitFlags carries what
	// only the damage site knows: whether armour soaked part of the hit and
	// whether it finished the victim.
	static void				ServerSend( idPlayer *victim, idEntity *attacker, int weapon, int damage, int hitFlags );

private:
	// Quake Live sizes its world marker slab at 32 and drops new markers when
	// it is full.  A ring drops the oldest instead, so sustained fire keeps
	// showing the hit that just landed rather than freezing on stale numbers.
	static const int		MAX_NUMBERS = 32;
	static const int		MAX_NUMBER_TEXT = 8;

	typedef struct damageNumber_s {
		int					startTime;
		idVec3				origin;
		idVec2				screenVelocity;
		idVec4				color;
		char				text[ MAX_NUMBER_TEXT ];
	} damageNumber_t;

	void					ColorForHit( int damage, int weapon, idVec4 &color );

	damageNumber_t			numbers[ MAX_NUMBERS ];
	int						nextNumber;
	// private stream: gameLocal.random drives client side prediction and must
	// not be perturbed by a cosmetic effect
	idRandom				random;
	const idMaterial *		charSetMaterial;
};

#endif	/* !__GAME_MP_HITFEEDBACK_H__ */
