//----------------------------------------------------------------
// RoundModes.cpp
//
// openQ4 round based gametypes carried over from Quake Live.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "match/MatchDeadline.h"
#include "RoundModes.h"

gameStateType_t rvClanArenaGameState::type = GS_CA;
gameStateType_t rvFreezeTagGameState::type = GS_FREEZETAG;
gameStateType_t rvRedRoverGameState::type = GS_REDROVER;

// Clan Arena pays a point of personal score per this much damage dealt
const int CA_DAMAGE_PER_SCORE = 100;

/*
===============================================================================

rvClanArenaGameState

===============================================================================
*/

/*
================
rvClanArenaGameState::rvClanArenaGameState
================
*/
rvClanArenaGameState::rvClanArenaGameState( bool allocPrevious ) : rvRoundGameState( false ) {
	Clear();

	if ( allocPrevious ) {
		previousGameState = new rvClanArenaGameState( false );
	} else {
		previousGameState = NULL;
	}

	trackPrevious = allocPrevious;
}

/*
================
rvClanArenaGameState::Clear
================
*/
void rvClanArenaGameState::Clear( void ) {
	int i;

	rvRoundGameState::Clear();

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		damageResidue[ i ] = 0;
	}
}

/*
================
rvClanArenaGameState::RoundBegin

The loadout itself is granted on spawn, from the GTF_FULLARSENAL flag, so that
a player is armed for the whole countdown rather than from "FIGHT" onwards.
What is left here is the per-round scoring reset, and topping anyone back up
who took chip damage during the countdown.
================
*/
void rvClanArenaGameState::RoundBegin( void ) {
	int i;

	rvRoundGameState::RoundBegin();

	if ( gameLocal.isClient ) {
		return;
	}

	for ( i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[ i ];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *p = static_cast< idPlayer * >( ent );

		damageResidue[ i ] = 0;

		// Never top up a corpse.  GiveStuffToPlayer writes health straight into
		// idPlayer with no death test, and a dead player carrying full health is
		// counted as a survivor by PlayerIsAlive and so by every round-end and
		// tie-break tally - a wiped-out team that cannot lose the round.  Remote
		// clients read the same health and draw a player who is not there.
		if ( p->spectating || p->wantSpectate || p->health <= 0 ) {
			continue;
		}

		GiveStuffToPlayer( p, "ammo", "" );
		GiveStuffToPlayer( p, "armor", "" );
		GiveStuffToPlayer( p, "health", "" );
	}
}

/*
================
rvClanArenaGameState::PlayerDamage
================
*/
void rvClanArenaGameState::PlayerDamage( idPlayer* attacker, idPlayer* victim, int damage, int armorSave ) {
	int index, award, credit;

	if ( attacker == NULL || victim == NULL || attacker == victim ) {
		return;
	}

	if ( !RoundIsLive() ) {
		return;
	}

	// no credit for shooting your own side
	if ( attacker->team == victim->team ) {
		return;
	}

	index = attacker->entityNumber;
	if ( index < 0 || index >= MAX_CLIENTS ) {
		return;
	}

	// Quake Live credits what the shot actually took off the target: the health
	// it removes plus the armour it burns through, and no more than the target
	// had left.  Crediting the raw damage instead would pay a rocket into a
	// one-health player as if it had been a full hit, and would pay nothing at
	// all for stripping somebody's armour.
	credit = Min( Max( 0, damage ), Max( 0, victim->health ) ) + Max( 0, armorSave );
	if ( credit <= 0 ) {
		return;
	}

	damageResidue[ index ] += credit;

	award = damageResidue[ index ] / CA_DAMAGE_PER_SCORE;
	if ( award > 0 ) {
		damageResidue[ index ] -= award * CA_DAMAGE_PER_SCORE;
		gameLocal.mpGame.AddPlayerScore( attacker, award );
	}
}

/*
================
rvClanArenaGameState::IsType
================
*/
bool rvClanArenaGameState::IsType( gameStateType_t t ) const {
	return ( t == rvClanArenaGameState::type );
}

/*
================
rvClanArenaGameState::GetClassType
================
*/
gameStateType_t rvClanArenaGameState::GetClassType( void ) {
	return rvClanArenaGameState::type;
}

/*
===============================================================================

rvFreezeTagGameState

===============================================================================
*/

/*
================
rvFreezeTagGameState::rvFreezeTagGameState
================
*/
rvFreezeTagGameState::rvFreezeTagGameState( bool allocPrevious ) : rvRoundGameState( false ) {
	Clear();

	if ( allocPrevious ) {
		previousGameState = new rvFreezeTagGameState( false );
	} else {
		previousGameState = NULL;
	}

	trackPrevious = allocPrevious;
}

/*
================
rvFreezeTagGameState::Clear
================
*/
void rvFreezeTagGameState::Clear( void ) {
	int i;

	rvRoundGameState::Clear();

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		thawProgress[ i ] = 0;
		lastThawAnnounce[ i ] = 0;
		autoThawTime[ i ] = 0;
	}
}

void rvFreezeTagGameState::ShiftMatchTime( int deltaMsec ) {
	rvRoundGameState::ShiftMatchTime( deltaMsec );
	for ( int index = 0; index < MAX_CLIENTS; ++index ) {
		mpMatchShiftOptionalDeadline( lastThawAnnounce[ index ], deltaMsec );
		mpMatchShiftOptionalDeadline( autoThawTime[ index ], deltaMsec );
	}
}

/*
================
rvFreezeTagGameState::RoundBegin
================
*/
void rvFreezeTagGameState::RoundBegin( void ) {
	int i;

	rvRoundGameState::RoundBegin();

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		thawProgress[ i ] = 0;
		lastThawAnnounce[ i ] = 0;
		autoThawTime[ i ] = 0;
	}
}

/*
================
rvFreezeTagGameState::PlayerDeath
================
*/
void rvFreezeTagGameState::PlayerDeath( idPlayer* dead, idPlayer* killer ) {
	rvRoundGameState::PlayerDeath( dead, killer );

	if ( dead == NULL || gameLocal.isClient ) {
		return;
	}

	if ( !RoundIsLive() ) {
		return;
	}

	thawProgress[ dead->entityNumber ] = 0;
	lastThawAnnounce[ dead->entityNumber ] = 0;

	// A frozen player is meant to be a body you have to reach, not a target.
	// Quake Live makes them outright invulnerable; Quake 4 leaves the corpse
	// takedamage so it can still be gibbed, which here means splash damage into
	// a pile of frozen team mates feeds hit markers, damage numbers and stats
	// for hits on people who are already out.  SpawnToPoint -> Init restores it
	// on the way back in.
	dead->fl.takedamage = false;

	// Arm the unattended thaw.  A player the world killed rather than an enemy
	// is usually somewhere nobody can stand - the bottom of a pit, a lava pool -
	// so they come back on a much shorter fuse, as they do in Quake Live.
	//
	// openQ4: only a genuine world kill qualifies.  killer == dead is a suicide -
	// idMultiplayerGame::PlayerDeath passes the victim as their own killer for the
	// "kill" command and for self-inflicted splash - and that is a deliberate act
	// in a place the player chose, not the mode failing to give them a reachable
	// body.  Treating it as a world death let a cornered player type "kill" and be
	// back in five seconds for the price of one point, which is the whole mode.
	{
		const bool worldDeath = ( killer == NULL );
		const int seconds = worldDeath
			? gameLocal.serverInfo.GetInt( "si_freezeWorldDeathDelay" )
			: gameLocal.serverInfo.GetInt( "si_freezeAutoThawTime" );

		autoThawTime[ dead->entityNumber ] = ( seconds > 0 ) ? gameLocal.time + seconds * 1000 : 0;
	}

	// The team notice reaches the victim too and a notice replaces whatever is
	// on screen, so the personal one is sent second and is the one they keep.
	if ( killer != NULL && killer != dead ) {
		// "%s froze %s" - both names, or the message reads with a hole in it
		gameLocal.mpGame.CenterPrintTeam( dead->team, "#str_41351",
			idMultiplayerGame::CPARM_CLIENT, killer->entityNumber,
			idMultiplayerGame::CPARM_CLIENT, dead->entityNumber );
	}

	gameLocal.mpGame.CenterPrint( dead->entityNumber, "#str_41350" );
}

/*
================
rvFreezeTagGameState::ClientDisconnect
================
*/
void rvFreezeTagGameState::ClientDisconnect( idPlayer* player ) {
	rvRoundGameState::ClientDisconnect( player );

	if ( player != NULL && player->entityNumber >= 0 && player->entityNumber < MAX_CLIENTS ) {
		thawProgress[ player->entityNumber ] = 0;
		lastThawAnnounce[ player->entityNumber ] = 0;
		autoThawTime[ player->entityNumber ] = 0;
	}
}

/*
================
rvFreezeTagGameState::ThawPlayer

Brings a frozen player back into the round where they fell, which is the whole
point of the mode: position matters, so a good thaw is a push.
================
*/
void rvFreezeTagGameState::ThawPlayer( idPlayer* frozen, idPlayer* thawer ) {
	idVec3		origin;
	idAngles	angles;

	if ( frozen == NULL ) {
		return;
	}

	origin = frozen->GetPhysics()->GetOrigin();
	angles = frozen->GetPhysics()->GetAxis().ToAngles();
	angles.pitch = 0.0f;
	angles.roll = 0.0f;

	SetEliminated( frozen->entityNumber, false );
	thawProgress[ frozen->entityNumber ] = 0;
	lastThawAnnounce[ frozen->entityNumber ] = 0;
	autoThawTime[ frozen->entityNumber ] = 0;

	// Respawn straight onto the body rather than respawning at a spawn point and
	// then teleporting.  The old order ran a full SelectSpawnPoint first, which
	// fired an unrelated spawn point's targets, played the spawn effect over
	// there and telefragged whatever was standing on it - all for a position
	// that was thrown away one line later.
	// An unattended thaw is the timer giving up: nobody reached the body, and
	// very often nobody could, because it is at the bottom of a pit or inside a
	// hurt volume. Respawning onto it there kills the player again, freezes them
	// again and re-arms the same fuse, for the rest of the round. FindThawSpot
	// cannot see that hazard either - it only rejects solids and other players,
	// and with no thawer to step away from it has no direction to search in. Take
	// an ordinary spawn point instead; only a real rescue earns the body spot.
	if ( thawer != NULL && FindThawSpot( frozen, thawer, origin ) ) {
		frozen->forceRespawn = false;
		frozen->SpawnToPoint( origin, angles );
	} else {
		// the body is somewhere nobody can stand; take an ordinary spawn point
		frozen->forceRespawn = true;
		frozen->ServerSpectate( false );
	}

	if ( thawer != NULL ) {
		gameLocal.mpGame.AddPlayerScore( thawer, 1 );
		// "%s thawed %s" - the team wants to know who came back, not just who did it
		gameLocal.mpGame.CenterPrintTeam( frozen->team, "#str_41353",
			idMultiplayerGame::CPARM_CLIENT, thawer->entityNumber,
			idMultiplayerGame::CPARM_CLIENT, frozen->entityNumber );
		gameLocal.mpGame.CenterPrint( frozen->entityNumber, "#str_41352", idMultiplayerGame::CPARM_CLIENT, thawer->entityNumber );
	} else {
		// unattended thaw: say why they are suddenly back on their feet
		gameLocal.mpGame.CenterPrint( frozen->entityNumber, "#str_41357" );
	}
}

/*
================
rvFreezeTagGameState::Run

Ticks the thaw timers.  A frozen player thaws once a live team mate has stood
within si_freezeThawRadius of them for si_freezeThawTime seconds; stepping
away lets the progress drain back rather than resetting it outright, so a
contested thaw is a real contest.
================
*/
void rvFreezeTagGameState::Run( void ) {
	int		i, j;
	int		thawTime, thawRadiusSquared;

	rvRoundGameState::Run();

	if ( gameLocal.isClient || !RoundIsLive() ) {
		return;
	}

	thawTime = Max( 1, gameLocal.serverInfo.GetInt( "si_freezeThawTime" ) ) * 1000;
	// a serverInfo that has somehow lost the key would otherwise square zero and
	// make every frozen player permanently unreachable
	thawRadiusSquared = Max( 16, gameLocal.serverInfo.GetInt( "si_freezeThawRadius" ) );
	thawRadiusSquared *= thawRadiusSquared;

	for ( i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[ i ];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *frozen = static_cast< idPlayer * >( ent );
		if ( !IsEliminated( i ) || frozen->wantSpectate || frozen->team < 0 ) {
			continue;
		}

		// A frozen player is a body lying where they fell.  Somebody who is
		// spectating - held out of the round, or watching from the free-fly
		// camera - has no body, and their camera position is not a place a team
		// mate can stand next to.  Thawing that would spawn them out of thin air
		// wherever they happened to be looking.
		if ( frozen->spectating ) {
			continue;
		}

		// nobody got there in time, or nobody could
		if ( autoThawTime[ i ] > 0 && gameLocal.time >= autoThawTime[ i ] ) {
			ThawPlayer( frozen, NULL );
			continue;
		}

		idPlayer *thawer = NULL;

		for ( j = 0; j < gameLocal.numClients; j++ ) {
			idEntity *other = gameLocal.entities[ j ];

			if ( j == i || !other || !other->IsType( idPlayer::GetClassType() ) ) {
				continue;
			}

			idPlayer *mate = static_cast< idPlayer * >( other );
			if ( mate->team != frozen->team || !PlayerIsAlive( mate ) || mate->spectating ) {
				continue;
			}

			if ( !CanReachToThaw( frozen, mate, thawRadiusSquared ) ) {
				continue;
			}

			thawer = mate;
			break;
		}

		if ( thawer != NULL ) {
			thawProgress[ i ] += gameLocal.msec;

			// A heartbeat once a second, to the two people it concerns: the one
			// doing the work and the one waiting on it.  The frozen player is
			// told last so the notice they keep is the one about them.
			if ( gameLocal.time - lastThawAnnounce[ i ] > 1000 ) {
				lastThawAnnounce[ i ] = gameLocal.time;
				gameLocal.mpGame.CenterPrint( thawer->entityNumber, "#str_41355",
					idMultiplayerGame::CPARM_CLIENT, i );
				gameLocal.mpGame.CenterPrint( i, "#str_41356",
					idMultiplayerGame::CPARM_CLIENT, thawer->entityNumber );
			}

			if ( thawProgress[ i ] >= thawTime ) {
				ThawPlayer( frozen, thawer );
			}
		} else if ( thawProgress[ i ] > 0 ) {
			thawProgress[ i ] = Max( 0, thawProgress[ i ] - gameLocal.msec );
		}
	}
}

/*
================
rvFreezeTagGameState::FindThawSpot

Picks somewhere the thawed player can legally stand.  SpawnToPoint telefrags
whatever occupies the destination, and the one player guaranteed to be near a
body is the team mate who just spent two seconds thawing it, so the body's own
position is only used when it is clear.  Failing that, step away from the thawer
before giving up.
================
*/
bool rvFreezeTagGameState::FindThawSpot( idPlayer* frozen, idPlayer* thawer, idVec3 &origin ) const {
	// a player stands in a 32 wide, 74 tall box; anything closer than this
	// overlaps and would be telefragged
	const float		clearRadius = 34.0f;
	const float		clearHeight = 72.0f;
	const idVec3	body = origin;
	idVec3			away;
	int				attempt;

	if ( frozen == NULL ) {
		return false;
	}

	away.Zero();
	if ( thawer != NULL ) {
		away = body - thawer->GetPhysics()->GetOrigin();
		away.z = 0.0f;
		if ( away.Normalize() <= 0.0f ) {
			away.Set( 1.0f, 0.0f, 0.0f );
		}
	}

	for ( attempt = 0; attempt < 4; attempt++ ) {
		const idVec3	candidate = body + away * ( clearRadius * attempt );
		trace_t			trace;
		int				i;
		bool			blocked = false;

		// inside the world, or with no floor under it, is not a place to stand
		if ( gameLocal.Contents( frozen, candidate + idVec3( 0.0f, 0.0f, 8.0f ), NULL,
				mat3_identity, MASK_PLAYERSOLID, frozen ) ) {
			continue;
		}
		gameLocal.TracePoint( frozen, trace, body + idVec3( 0.0f, 0.0f, 8.0f ),
			candidate + idVec3( 0.0f, 0.0f, 8.0f ), MASK_PLAYERSOLID, frozen );
		if ( trace.fraction < 1.0f ) {
			continue;
		}

		for ( i = 0; i < gameLocal.numClients; i++ ) {
			idEntity *ent = gameLocal.entities[ i ];

			if ( !ent || ent == frozen || !ent->IsType( idPlayer::GetClassType() ) ) {
				continue;
			}

			idPlayer *other = static_cast< idPlayer * >( ent );
			if ( other->spectating || other->health <= 0 ) {
				continue;
			}

			const idVec3 delta = other->GetPhysics()->GetOrigin() - candidate;
			if ( idMath::Fabs( delta.z ) < clearHeight &&
				 ( delta.x * delta.x + delta.y * delta.y ) < ( clearRadius * clearRadius ) ) {
				blocked = true;
				break;
			}
		}

		if ( !blocked ) {
			origin = candidate;
			return true;
		}

		if ( away.LengthSqr() <= 0.0f ) {
			break;
		}
	}

	return false;
}

/*
================
rvFreezeTagGameState::ReportThawState

Server-local diagnostics used by the engine-command gameplay smoke.
================
*/
void rvFreezeTagGameState::ReportThawState( void ) const {
	const int radius = Max( 16, gameLocal.serverInfo.GetInt( "si_freezeThawRadius" ) );
	for ( int i = 0; i < gameLocal.numClients; ++i ) {
		idPlayer *frozen = gameLocal.GetClientByNum( i );
		if ( frozen == NULL || frozen->health > 0 ) {
			continue;
		}
		gameLocal.Printf( "MP_FREEZE slot=%d eliminated=%d progress=%d auto=%d radius=%d duration=%d live=%d\n",
			i, IsEliminated( i ), thawProgress[ i ], autoThawTime[ i ], radius,
			gameLocal.serverInfo.GetInt( "si_freezeThawTime" ), RoundIsLive() );
		for ( int j = 0; j < gameLocal.numClients; ++j ) {
			idPlayer *mate = gameLocal.GetClientByNum( j );
			if ( mate == NULL || mate == frozen || mate->team != frozen->team ) {
				continue;
			}
			trace_t trace;
			memset( &trace, 0, sizeof( trace ) );
			gameLocal.TracePoint( mate, trace, mate->GetEyePosition(),
				frozen->GetPhysics()->GetOrigin() + idVec3( 0, 0, 16 ), MASK_SOLID, mate );
			gameLocal.Printf( "MP_THAW body=%d mate=%d alive=%d reachable=%d trace=%.3f hit=%d contents=%d\n",
				i, j, PlayerIsAlive( mate ), CanReachToThaw( frozen, mate, radius * radius ),
				trace.fraction, trace.c.entityNum, trace.c.contents );
		}
	}
}

// A rescue requires visible contact unless the server explicitly enables
// through-surface thawing. Distance alone would also rescue through floors.
bool rvFreezeTagGameState::CanReachToThaw( idPlayer* frozen, idPlayer* mate, int thawRadiusSquared ) const {
	idVec3	frozenOrigin;
	idVec3	mateOrigin;
	trace_t	trace;

	if ( frozen == NULL || mate == NULL ) {
		return false;
	}

	frozenOrigin = frozen->GetPhysics()->GetOrigin();
	mateOrigin = mate->GetPhysics()->GetOrigin();

	if ( ( mateOrigin - frozenOrigin ).LengthSqr() > thawRadiusSquared ) {
		return false;
	}

	if ( gameLocal.serverInfo.GetBool( "si_freezeThawThroughSurface" ) ) {
		return true;
	}

	// Start at the rescuer's eyes and ignore their own solid player box.
	// Tracing toward their feet from the body hit that box on every approach,
	// preventing otherwise unobstructed rescues. A hit on the frozen body is
	// visible contact, while intervening world geometry still blocks the thaw.
	frozenOrigin.z += 16.0f;
	mateOrigin = mate->GetEyePosition();
	memset( &trace, 0, sizeof( trace ) );
	trace.c.entityNum = ENTITYNUM_NONE;
	gameLocal.TracePoint( mate, trace, mateOrigin, frozenOrigin, MASK_SOLID, mate );
	return ( trace.fraction >= 1.0f || trace.c.entityNum == frozen->entityNumber );
}

/*
================
rvFreezeTagGameState::IsType
================
*/
bool rvFreezeTagGameState::IsType( gameStateType_t t ) const {
	return ( t == rvFreezeTagGameState::type );
}

/*
================
rvFreezeTagGameState::GetClassType
================
*/
gameStateType_t rvFreezeTagGameState::GetClassType( void ) {
	return rvFreezeTagGameState::type;
}

/*
===============================================================================

rvRedRoverGameState

===============================================================================
*/

/*
================
rvRedRoverGameState::rvRedRoverGameState
================
*/
rvRedRoverGameState::rvRedRoverGameState( bool allocPrevious ) : rvRoundGameState( false ) {
	Clear();

	if ( allocPrevious ) {
		previousGameState = new rvRedRoverGameState( false );
	} else {
		previousGameState = NULL;
	}

	trackPrevious = allocPrevious;
}

/*
================
rvRedRoverGameState::RoundBegin
================
*/
void rvRedRoverGameState::RoundBegin( void ) {
	int i;

	rvRoundGameState::RoundBegin();

	if ( gameLocal.isClient ) {
		return;
	}

	// Red Rover starts everyone fully stocked; PrepareNextRound has already
	// restored the sides before ResetRound respawned them.
	for ( i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[ i ];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *p = static_cast< idPlayer * >( ent );
		if ( p->wantSpectate ) {
			continue;
		}

		GiveStuffToPlayer( p, "ammo", "" );
	}
}

/*
================
rvRedRoverGameState::NewState

Restore declared roster affiliations before the next warmup's admission gate.
================
*/
bool rvRedRoverGameState::NewState( mpGameState_t newState ) {
	if ( !rvRoundGameState::NewState( newState ) ) {
		return false;
	}
	if ( !gameLocal.isClient && newState == WARMUP && gameLocal.mpGame.IsManagedMatch() ) {
		PrepareNextRound();
	}
	return true;
}

// Restore declared managed rosters; unrostered players use the stable balanced
// order. The world reset respawns them without a second death or score change.
void rvRedRoverGameState::PrepareNextRound( void ) {
	int activePlayer = 0;
	int teamCount[TEAM_MAX];

	memset( teamCount, 0, sizeof( teamCount ) );

	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[i];
		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *player = static_cast<idPlayer *>( ent );
		if ( !gameLocal.mpGame.CanPlay( player ) ) {
			continue;
		}

		int targetTeam = ( activePlayer++ & 1 ) ? TEAM_STROGG : TEAM_MARINE;
		if ( gameLocal.mpGame.IsManagedMatch() ) {
			const mpMatchSession &session = gameLocal.mpGame.GetMatchSession();
			mpParticipantId participant;
			uint32_t generation = 0;
			if ( session.GetSlotGeneration( i, generation ) &&
				session.ResolveSlotBinding( i, generation, participant ) ) {
				const mpMatchRosterSeat *seat = session.GetRosterSeat( session.FindRosterSeat( participant ) );
				if ( seat != NULL && seat->declared ) {
					targetTeam = seat->side;
				}
			}
		}
		if ( gameLocal.mpGame.ApplyRoundTeamAssignment( player, targetTeam, false ) ) {
			teamCount[targetTeam]++;
		}
	}

	gameLocal.Printf( "red rover: prepared round %d with %d Marine and %d Strogg players\n",
		roundNumber + 1, teamCount[TEAM_MARINE], teamCount[TEAM_STROGG] );
}

/*
================
rvRedRoverGameState::PlayerDeath

The whole mode in one rule: dying puts you on the other side.

Quake Live does not consult the killer at all - G_RRHandlePlayerDeath moves the
victim from oldTeam to the opposite team for every death it sees, suicides and
world deaths included.  Exempting those, as this used to, handed players a way
to keep their own side alive indefinitely: with two sides left and one player on
yours, typing "kill" respawns you on the same team and the round cannot resolve.
================
*/
void rvRedRoverGameState::PlayerDeath( idPlayer* dead, idPlayer* killer ) {
	int newTeam;

	rvRoundGameState::PlayerDeath( dead, killer );

	if ( gameLocal.isClient || dead == NULL || !RoundIsLive() ) {
		return;
	}

	if ( dead->team < 0 || dead->team >= TEAM_MAX ) {
		return;
	}

	// The userinfo round-trip below mutates dead->team synchronously, so the
	// side they are leaving has to be captured first - the warning belongs to
	// the team that just lost someone, not the one that gained them.
	const int oldTeam = dead->team;
	newTeam = ( dead->team == TEAM_MARINE ) ? TEAM_STROGG : TEAM_MARINE;

	if ( !gameLocal.mpGame.ApplyRoundTeamAssignment( dead, newTeam, true ) ) {
		gameLocal.Warning( "Red Rover could not commit a forced side conversion for client %d", dead->entityNumber );
		return;
	}

	gameLocal.mpGame.CenterPrint( dead->entityNumber, "#str_41360", idMultiplayerGame::CPARM_TEAM, newTeam );

	// Quake Live warns the last player left on a side here too.  The shared
	// warning in the round layer only fires for elimination modes, and Red Rover
	// is not one - nobody is out, they have just changed colours.
	NotifyLastOnSide( oldTeam );
}

/*
================
rvRedRoverGameState::NotifyLastOnSide
================
*/
void rvRedRoverGameState::NotifyLastOnSide( int team ) const {
	int i;

	if ( team < 0 || team >= TEAM_MAX || CountLiveTeamPlayers( team ) != 1 ) {
		return;
	}

	for ( i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[ i ];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *p = static_cast< idPlayer * >( ent );
		if ( p->team == team && PlayerIsAlive( p ) ) {
			gameLocal.mpGame.CenterPrint( i, "#str_41336" );
			gameLocal.mpGame.AnnounceTo( i, AS_MATCH_LAST_STANDING );
			break;
		}
	}
}

/*
================
rvRedRoverGameState::CheckRoundEnd

The round is over once everybody is wearing the same colours.
================
*/
bool rvRedRoverGameState::CheckRoundEnd( int &winningTeam ) {
	int marine = CountLiveTeamPlayers( TEAM_MARINE );
	int strogg = CountLiveTeamPlayers( TEAM_STROGG );

	if ( marine + strogg < 2 ) {
		// not enough players to decide anything
		return false;
	}

	if ( marine > 0 && strogg > 0 ) {
		return false;
	}

	winningTeam = marine ? TEAM_MARINE : TEAM_STROGG;
	return true;
}

/*
================
rvRedRoverGameState::ResolveRoundTimeout
================
*/
int rvRedRoverGameState::ResolveRoundTimeout( void ) {
	int marine = CountLiveTeamPlayers( TEAM_MARINE );
	int strogg = CountLiveTeamPlayers( TEAM_STROGG );

	if ( marine == strogg ) {
		return TEAM_NONE;
	}

	return ( marine > strogg ) ? TEAM_MARINE : TEAM_STROGG;
}

/*
================
rvRedRoverGameState::CheckMatchEnd

Quake Live counts Red Rover's round limit against the total number of rounds
played rather than either team's tally, because sides change constantly and a
per-team count means very little here.
================
*/
bool rvRedRoverGameState::CheckMatchEnd( int &winningTeam ) {
	int roundLimit, marine, strogg;

	winningTeam = TEAM_NONE;

	marine = gameLocal.mpGame.GetScoreForTeam( TEAM_MARINE );
	strogg = gameLocal.mpGame.GetScoreForTeam( TEAM_STROGG );

	roundLimit = gameLocal.serverInfo.GetInt( "si_roundLimit" );
	if ( roundLimit > 0 && ( marine + strogg ) >= roundLimit ) {
		winningTeam = ( marine > strogg ) ? TEAM_MARINE : ( ( strogg > marine ) ? TEAM_STROGG : TEAM_NONE );
		return true;
	}

	if ( gameLocal.mpGame.TimeLimitHit() ) {
		gameLocal.mpGame.PrintMessageEvent( -1, MSG_TIMELIMIT );
		winningTeam = ( marine > strogg ) ? TEAM_MARINE : ( ( strogg > marine ) ? TEAM_STROGG : TEAM_NONE );
		return true;
	}

	return false;
}

/*
================
rvRedRoverGameState::IsType
================
*/
bool rvRedRoverGameState::IsType( gameStateType_t t ) const {
	return ( t == rvRedRoverGameState::type );
}

/*
================
rvRedRoverGameState::GetClassType
================
*/
gameStateType_t rvRedRoverGameState::GetClassType( void ) {
	return rvRedRoverGameState::type;
}
