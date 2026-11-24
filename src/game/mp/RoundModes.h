//----------------------------------------------------------------
// RoundModes.h
//
// openQ4 round based gametypes carried over from Quake Live:
// Clan Arena, Freeze Tag and Red Rover.
//
// All three share the round machine in rvRoundGameState and differ only in
// what a death means: in Clan Arena it removes you until the next round, in
// Freeze Tag it leaves a body a team mate can thaw, and in Red Rover it moves
// you onto the opposite side.
//----------------------------------------------------------------

#ifndef __ROUNDMODES_H__
#define __ROUNDMODES_H__

#include "RoundGameState.h"

/*
===============================================================================

rvClanArenaGameState

Round based team elimination.  Item pickups do not decide anything: everyone
spawns with the full arsenal and the round goes to whoever is left standing.
Personal score comes from damage dealt rather than frags, so a player who
softens up the enemy team is credited for it.

===============================================================================
*/
class rvClanArenaGameState : public rvRoundGameState {
public:
					rvClanArenaGameState( bool allocPrevious = true );

	virtual void	Clear( void );
	virtual void	PlayerDamage( idPlayer* attacker, idPlayer* victim, int damage, int armorSave );

	virtual	bool	IsType( gameStateType_t type ) const;
	static gameStateType_t GetClassType( void );

protected:
	virtual void	RoundBegin( void );

private:
	// damage left over between score awards, so partial hits are not lost
	int				damageResidue[ MAX_CLIENTS ];

	static gameStateType_t type;
};

/*
===============================================================================

rvFreezeTagGameState

Death freezes rather than kills.  A frozen player stays put as a body any team
mate can stand next to and thaw; a team is out when every one of them is
frozen at once.

The body is the dead player themselves rather than a separate entity: Quake 4
already leaves the corpse in place until the player asks to respawn, and it is
already replicated, so Freeze Tag needs no new entity and no new map content.

===============================================================================
*/
class rvFreezeTagGameState : public rvRoundGameState {
public:
					rvFreezeTagGameState( bool allocPrevious = true );

	virtual void	Clear( void );
	virtual void	Run( void );
	virtual void	ShiftMatchTime( int deltaMsec );
	virtual void	PlayerDeath( idPlayer* dead, idPlayer* killer );
	virtual void	ClientDisconnect( idPlayer* player );
	void			ReportThawState( void ) const;

	// frozen players stay on their own body instead of becoming spectators
	virtual bool	EliminatedBecomesSpectator( void ) const { return false; }

	virtual	bool	IsType( gameStateType_t type ) const;
	static gameStateType_t GetClassType( void );

	// a team mate may only thaw somebody they can actually see, unless the
	// server says otherwise.  Public so a rescuing bot holds exactly where the
	// rule counts its contact, not merely somewhere near the body.
	bool			CanReachToThaw( idPlayer* frozen, idPlayer* mate, int thawRadiusSquared ) const;

protected:
	virtual void	RoundBegin( void );

private:
	void			ThawPlayer( idPlayer* frozen, idPlayer* thawer );
	// somewhere the thawed player can stand without telefragging their rescuer
	bool			FindThawSpot( idPlayer* frozen, idPlayer* thawer, idVec3 &origin ) const;

	// milliseconds of team mate contact accumulated against each frozen player
	int				thawProgress[ MAX_CLIENTS ];
	int				lastThawAnnounce[ MAX_CLIENTS ];
	// when a frozen player thaws with no help at all, so a body nobody can
	// reach - down a pit, in the lava, behind the enemy team - cannot hold the
	// round hostage until the round clock runs out.  0 means never.
	int				autoThawTime[ MAX_CLIENTS ];

	static gameStateType_t type;
};

/*
===============================================================================

rvRedRoverGameState

A death moves the victim onto the opposite side, so the round ends when one
side has absorbed everybody.  Nobody is eliminated, so respawns stay normal
and only the team assignment changes.

Red Rover is the one team mode Quake Live reports on individual score rather
than team score, and its round limit counts total rounds played rather than
rounds won by either side.

===============================================================================
*/
class rvRedRoverGameState : public rvRoundGameState {
public:
					rvRedRoverGameState( bool allocPrevious = true );

	virtual bool	NewState( mpGameState_t newState );
	virtual void	PlayerDeath( idPlayer* dead, idPlayer* killer );

	virtual	bool	IsType( gameStateType_t type ) const;
	static gameStateType_t GetClassType( void );

protected:
	virtual bool	CheckRoundEnd( int &winningTeam );
	virtual bool	CheckMatchEnd( int &winningTeam );
	virtual int		ResolveRoundTimeout( void );
	virtual void	RoundBegin( void );
	virtual void	PrepareNextRound( void );

private:
	// tells the one player left on a side that they are alone; the round layer's
	// shared warning is for elimination modes only
	void			NotifyLastOnSide( int team ) const;

	static gameStateType_t type;
};

#endif // __ROUNDMODES_H__
