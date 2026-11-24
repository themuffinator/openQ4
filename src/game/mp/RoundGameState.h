//----------------------------------------------------------------
// RoundGameState.h
//
// openQ4 round based match progression.
//
// Quake 4's rvGameState runs one continuous match: warmup, countdown, game
// on, review.  Quake Live's Clan Arena, Freeze Tag, Red Rover and Attack &
// Defend instead play a series of short rounds inside a single match, each
// with its own countdown, its own clock and its own winner, with the match
// score being the count of rounds won.
//
// The round machine is deliberately a SEPARATE state field rather than extra
// mpGameState_t values.  GameTime(), ScheduleTimeAnnouncements(),
// CheckRespawns() and UpdateHud() all branch on currentState, and folding
// round phases into it would change the meaning of the match phase for every
// existing gametype.  rvTourneyArena already proves the separate-field shape
// works: it keeps its own per-arena clock alongside the match one.
//----------------------------------------------------------------

#ifndef __ROUNDGAMESTATE_H__
#define __ROUNDGAMESTATE_H__

#include "GameState.h"

class rvRoundGameState : public rvGameState {
public:
					rvRoundGameState( bool allocPrevious = true );

	virtual void	Clear( void );
	virtual void	Run( void );
	virtual bool	NewState( mpGameState_t newState );
	virtual void	ShiftMatchTime( int deltaMsec );
	virtual void	GameStateChanged( void );

	virtual void	SendState( const idMessageSender &sender, int clientNum = -1 );
	virtual void	ReceiveState( const idBitMsg& msg );
	virtual void	SendInitialState( const idMessageSender &sender, int clientNum );

	virtual void	PackState( idBitMsg& outMsg );
	virtual void	WriteState( idBitMsg &msg );
	virtual void	UnpackState( const idBitMsg& inMsg );

	virtual void	ClientDisconnect( idPlayer* player );
	virtual void	Spectate( idPlayer* player );

	virtual bool	AllowRespawn( idPlayer* player );
	virtual void	PlayerDeath( idPlayer* dead, idPlayer* killer );
	virtual void	PlayerWithdrew( idPlayer* player );
	virtual bool	WeaponsLocked( void ) const;

	// Clan Arena style modes put an eliminated player into follow-spectator;
	// Freeze Tag leaves them on their own body until a team mate thaws it.
	virtual bool	EliminatedBecomesSpectator( void ) const { return true; }

	roundState_t	GetRoundState( void ) const		{ return roundState; }
	int				GetRoundNumber( void ) const	{ return roundNumber; }
	int				GetRoundStartTime( void ) const	{ return roundStartTime; }
	int				GetRoundStateTime( void ) const	{ return roundStateTime; }
	int				GetRoundWinner( void ) const	{ return roundWinner; }
	bool			RoundIsLive( void ) const		{ return ( roundState == RS_ACTIVE ); }

	// milliseconds left in the current round, or -1 when the round is untimed
	int				GetRoundTimeRemaining( void ) const;

	bool				operator==( const rvRoundGameState& rhs ) const;
	rvRoundGameState&	operator=( const rvRoundGameState& rhs );

	virtual	bool	IsType( gameStateType_t type ) const;
	static gameStateType_t GetClassType( void );

protected:
	// ---- hooks for the individual round gametypes ----

	// a round has just gone live
	virtual void	RoundBegin( void );
	// last chance to normalize mode-specific state before the next respawn
	virtual void	PrepareNextRound( void );
	// the round is over; winningTeam is TEAM_NONE for a draw
	virtual void	RoundEnd( int winningTeam );
	// true when the round has been decided; sets winningTeam
	virtual bool	CheckRoundEnd( int &winningTeam );
	// winner when the round clock runs out; TEAM_NONE for a draw
	virtual int		ResolveRoundTimeout( void );
	// true when the match itself is over; sets winningTeam
	virtual bool	CheckMatchEnd( int &winningTeam );
	// score awarded to the round winner, in match points
	virtual int		RoundWinScore( void ) const { return 1; }
	// true when a player still counts towards their team this round
	virtual bool	PlayerIsAlive( idPlayer* player ) const;
	// true when death removes a player from the round
	virtual bool	IsEliminationMode( void ) const;

	// ---- shared helpers ----

	int				CountLiveTeamPlayers( int team ) const;
	int				CountTeamHealth( int team ) const;
	// team with players left when the other side is wiped, or TEAM_NONE
	int				LastTeamStanding( void ) const;
	// announces the round result and scores it
	void			AwardRound( int winningTeam );

	bool			SetRoundState( roundState_t newState );
	// forces a respawn for anyone dead but not eliminated, so a round never
	// opens with a corpse that the round-end test still counts as alive
	void			ReviveForRound( void );
	// closes the round's roster: every slot that is not a live participant at
	// the moment the round goes live is out of it, including the empty ones, so
	// a player who connects or joins mid-round waits for the next one
	void			SealRound( void );
	// wipes the arena and puts everyone back on their feet
	void			ResetRound( void );
	bool			ScheduleNextRound( void );

	bool			IsEliminated( int clientNum ) const;
	void			SetEliminated( int clientNum, bool value );

	roundState_t	roundState;
	int				roundNumber;
	int				roundStartTime;
	int				roundStateTime;		// when the current round state expires
	int				roundWinner;		// team that took the last round; TEAM_NONE for a draw

	// server side only: who is out of the current round.  Clients infer this
	// from spectating, so it does not need replicating.
	bool			eliminated[ MAX_CLIENTS ];

	static gameStateType_t type;
};

#endif // __ROUNDGAMESTATE_H__
