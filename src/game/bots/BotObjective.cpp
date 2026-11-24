//----------------------------------------------------------------
// BotObjective.cpp
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "../mp/GameState.h"
#include "../mp/RoundGameState.h"
#include "BotObjective.h"

// These priorities express rule urgency, not route desirability.  Bot.cpp may
// combine them with travel cost, personality, reservations and hysteresis.
static const float BOTOBJ_PRIORITY_CAPTURE	= 100.0f;
static const float BOTOBJ_PRIORITY_CARRIER	= 100.0f;
static const float BOTOBJ_PRIORITY_INTERCEPT	= 90.0f;
static const float BOTOBJ_PRIORITY_RESCUE	= 88.0f;
static const float BOTOBJ_PRIORITY_RETURN	= 85.0f;
static const float BOTOBJ_PRIORITY_ESCORT	= 72.0f;
static const float BOTOBJ_PRIORITY_CONTROL	= 68.0f;
static const float BOTOBJ_PRIORITY_FETCH	= 60.0f;
static const float BOTOBJ_PRIORITY_DEFEND	= 54.0f;
// An order outranks the role the allocator would have handed out, but not the
// rules' urgent steps: a carrier still captures and anyone may still grab
// health at the edge of death.
static const float BOTOBJ_ORDER_BONUS		= 20.0f;

// A bot already filling a role keeps it unless a challenger is clearly closer:
// three quarters of its distance, less a fixed margin.
static const float BOTOBJ_INCUMBENT_SCALE	= 0.75f;
static const float BOTOBJ_INCUMBENT_MARGIN	= 256.0f;

/*
================
botObjective_t::botObjective_t
================
*/
botObjective_s::botObjective_s( void ) {
	Clear();
}

/*
================
botObjective_t::Clear
================
*/
void botObjective_s::Clear( void ) {
	kind			= BOTOBJ_NONE;
	entity			= NULL;
	origin.Zero();
	priority		= 0.0f;
	holdPosition	= false;
}

/*
================
BotObjectiveSet
================
*/
static bool BotObjectiveSet( botObjective_t &out, botObjectiveKind_t kind,
							 idEntity *entity, const idVec3 &origin, float priority,
							 bool holdPosition ) {
	out.kind			= kind;
	out.entity			= entity;
	out.origin			= origin;
	out.priority		= idMath::ClampFloat( 0.0f, 100.0f, priority );
	out.holdPosition	= holdPosition;
	return true;
}

/*
================
BotObjectiveMatchIsLive
================
*/
static bool BotObjectiveMatchIsLive( void ) {
	rvGameState *state = gameLocal.mpGame.GetGameState();
	if ( !state ) {
		return false;
	}

	const mpGameState_t matchState = state->GetMPGameState();
	return ( matchState == GAMEON || matchState == SUDDENDEATH );
}

/*
================
BotObjectiveValidTeam
================
*/
static bool BotObjectiveValidTeam( int team ) {
	return ( team >= TEAM_MARINE && team < TEAM_MAX );
}

/*
================
BotObjectiveSameInstance
================
*/
static bool BotObjectiveSameInstance( const idPlayer *self, const idEntity *other ) {
	return ( self && other && other->GetInstance() == self->GetInstance() );
}

/*
================
BotObjectiveLivePlayer

CanPlay rejects players who have left the match even during the short interval
in which their idPlayer still exists.  Spectating and health are separate:
CanPlay intentionally remains true for a dead Freeze Tag body.
================
*/
static bool BotObjectiveLivePlayer( idPlayer *self, idPlayer *other ) {
	if ( !other || other->entityNumber < 0 || other->entityNumber >= MAX_CLIENTS ) {
		return false;
	}
	if ( !BotObjectiveSameInstance( self, other ) || !other->GetPhysics() ||
		 !gameLocal.mpGame.CanPlay( other ) || other->wantSpectate ||
		 other->spectating || other->health <= 0 ) {
		return false;
	}
	return BotObjectiveValidTeam( other->team );
}

/*
================
BotObjectiveOrigin
================
*/
static bool BotObjectiveOrigin( idEntity *entity, idVec3 &origin ) {
	if ( !entity || !entity->GetPhysics() ) {
		return false;
	}
	origin = entity->GetPhysics()->GetOrigin();
	return true;
}

/*
================
BotObjectiveRolePlayer

Objective roles are allocated among bots, not humans whose intent cannot be
inferred.  The querying player is retained even during its short manager bind
window so objective discovery never loses its only eligible role owner.
================
*/
static bool BotObjectiveRolePlayer( idPlayer *self, idPlayer *candidate,
									idPlayer *excluded, const bool *blocked ) {
	if ( candidate == excluded || !BotObjectiveLivePlayer( self, candidate ) ||
		 candidate->team != self->team ) {
		return false;
	}
	if ( candidate != self && !botManager.IsBot( candidate->entityNumber ) ) {
		return false;
	}
	return !blocked || !blocked[candidate->entityNumber];
}

/*
================
BotObjectiveCountRolePlayers
================
*/
static int BotObjectiveCountRolePlayers( idPlayer *self, idPlayer *excluded,
									 const bool *blocked ) {
	int count = 0;
	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		if ( BotObjectiveRolePlayer( self, gameLocal.GetClientByNum( i ), excluded, blocked ) ) {
			count++;
		}
	}
	return count;
}

/*
================
BotObjectiveRoleDistance

Straight-line distance to a role's target, with the bot already filling that
role given an incumbent's margin.  Without it every bot re-ranked by raw
distance on its own decision tick, so an escort trailing its carrier lost the
role to a defender the carrier ran past, dropped back, and won it again - two
bots trading the same two jobs every quarter second.
================
*/
static float BotObjectiveRoleDistance( idPlayer *candidate, const idVec3 &targetOrigin,
									   botObjectiveKind_t kind, const idEntity *target ) {
	float distance = ( candidate->GetPhysics()->GetOrigin() - targetOrigin ).Length();

	// How far the run is, not how far the crow flies: on a stacked map the bot
	// standing over the flag can be the furthest from it.  The landmark bound
	// is constant time and sees walls and floors; a player the navmesh cannot
	// place keeps the straight line, and one that cannot get there at all
	// goes last.
	if ( target && navMesh.IsValid() ) {
		const int from = botManager.ClientNavNode( candidate->entityNumber );
		const int to = ( target->entityNumber >= 0 && target->entityNumber < MAX_CLIENTS ) ?
			botManager.ClientNavNode( target->entityNumber ) : navMesh.NodeForEntity( target );
		if ( from >= 0 && to >= 0 ) {
			const float bound = navMesh.RouteLowerBound( from, to );
			distance = ( bound >= 1.0e29f ) ? 1.0e8f : Max( bound, 0.0f );
		}
	}

	if ( kind != BOTOBJ_NONE && botManager.ObjectiveKindOf( candidate->entityNumber ) == kind &&
		 ( !target || botManager.ObjectiveEntityOf( candidate->entityNumber ) == target ) ) {
		distance = Max( 0.0f, distance * BOTOBJ_INCUMBENT_SCALE - BOTOBJ_INCUMBENT_MARGIN );
	}

	return distance;
}

/*
================
BotObjectiveSelectClosestRolePlayers

Produces the same mask for every bot on a team.  Distance assigns the useful
players while the entity number makes exact ties deterministic, avoiding the
all-team dogpile that a nearest-objective query creates.  Incumbents keep a
role unless a challenger is clearly better placed.
================
*/
static int BotObjectiveSelectClosestRolePlayers( idPlayer *self, const idVec3 &targetOrigin,
										  int requested, idPlayer *excluded,
										  const bool *blocked, bool selected[MAX_CLIENTS],
										  botObjectiveKind_t kind = BOTOBJ_NONE,
										  const idEntity *target = NULL ) {
	memset( selected, 0, sizeof( bool ) * MAX_CLIENTS );
	requested = Max( 0, requested );
	int selectedCount = 0;

	while ( selectedCount < requested ) {
		idPlayer *best = NULL;
		float bestDistanceSqr = idMath::INFINITY;

		for ( int i = 0; i < gameLocal.numClients; i++ ) {
			idPlayer *candidate = gameLocal.GetClientByNum( i );
			if ( !BotObjectiveRolePlayer( self, candidate, excluded, blocked ) || selected[i] ) {
				continue;
			}

			const float distanceSqr = Square( BotObjectiveRoleDistance( candidate, targetOrigin, kind, target ) );
			if ( distanceSqr < bestDistanceSqr ||
				 ( distanceSqr == bestDistanceSqr &&
				   ( !best || candidate->entityNumber < best->entityNumber ) ) ) {
				bestDistanceSqr = distanceSqr;
				best = candidate;
			}
		}

		if ( !best ) {
			break;
		}
		selected[best->entityNumber] = true;
		selectedCount++;
	}

	return selectedCount;
}

/*
================
BotObjectiveMergeRoles
================
*/
static void BotObjectiveMergeRoles( bool assigned[MAX_CLIENTS], const bool selected[MAX_CLIENTS] ) {
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		assigned[i] = assigned[i] || selected[i];
	}
}

/*
================
BotObjectiveMajoritySlots
================
*/
static int BotObjectiveMajoritySlots( int available ) {
	return available > 0 ? Max( 1, ( available * 2 + 2 ) / 3 ) : 0;
}

/*
================
BotObjectiveSupportSlots
================
*/
static int BotObjectiveSupportSlots( int available ) {
	return available > 0 ? Max( 1, ( available + 1 ) / 2 ) : 0;
}

/*
================
BotObjectiveAssignUniqueTarget

Greedy global matching gives each rescue body or spawned artifact one nearby
bot before assigning a second bot to anything.  This keeps the result stable
and deterministic without persistent squad state in the objective query.
================
*/
static idEntity *BotObjectiveAssignUniqueTarget( idPlayer *self,
										  idEntity **targets, int targetCount,
										  const bool *blocked, botObjectiveKind_t kind ) {
	idPlayer *rolePlayers[MAX_CLIENTS];
	bool playerAssigned[MAX_CLIENTS];
	bool targetAssigned[MAX_CLIENTS];
	int rolePlayerCount = 0;

	memset( playerAssigned, 0, sizeof( playerAssigned ) );
	memset( targetAssigned, 0, sizeof( targetAssigned ) );

	for ( int i = 0; i < gameLocal.numClients && rolePlayerCount < MAX_CLIENTS; i++ ) {
		idPlayer *candidate = gameLocal.GetClientByNum( i );
		if ( BotObjectiveRolePlayer( self, candidate, NULL, blocked ) ) {
			rolePlayers[rolePlayerCount++] = candidate;
		}
	}

	const int assignments = Min( rolePlayerCount, Min( targetCount, MAX_CLIENTS ) );
	for ( int assignment = 0; assignment < assignments; assignment++ ) {
		int bestPlayer = -1;
		int bestTarget = -1;
		float bestDistanceSqr = idMath::INFINITY;

		for ( int playerIndex = 0; playerIndex < rolePlayerCount; playerIndex++ ) {
			if ( playerAssigned[playerIndex] ) {
				continue;
			}
			for ( int targetIndex = 0; targetIndex < targetCount; targetIndex++ ) {
				idEntity *target = targets[targetIndex];
				if ( targetAssigned[targetIndex] || !target || !target->GetPhysics() ||
					 !BotObjectiveSameInstance( self, target ) ) {
					continue;
				}

				const float distanceSqr = Square( BotObjectiveRoleDistance( rolePlayers[playerIndex],
					target->GetPhysics()->GetOrigin(), kind, target ) );
				const bool lowerPlayer = bestPlayer < 0 ||
					rolePlayers[playerIndex]->entityNumber < rolePlayers[bestPlayer]->entityNumber;
				const bool lowerTarget = bestTarget < 0 ||
					target->entityNumber < targets[bestTarget]->entityNumber;
				if ( distanceSqr < bestDistanceSqr ||
					 ( distanceSqr == bestDistanceSqr && ( lowerPlayer ||
					   ( bestPlayer >= 0 && rolePlayers[playerIndex]->entityNumber ==
						 rolePlayers[bestPlayer]->entityNumber && lowerTarget ) ) ) ) {
					bestDistanceSqr = distanceSqr;
					bestPlayer = playerIndex;
					bestTarget = targetIndex;
				}
			}
		}

		if ( bestPlayer < 0 || bestTarget < 0 ) {
			break;
		}
		playerAssigned[bestPlayer] = true;
		targetAssigned[bestTarget] = true;
		if ( rolePlayers[bestPlayer] == self ) {
			return targets[bestTarget];
		}
	}

	return NULL;
}

/*
================
BotObjectiveNearbyEnemies
================
*/
static int BotObjectiveNearbyEnemies( idPlayer *self, const idVec3 &origin, float radius ) {
	const float radiusSqr = radius * radius;
	int count = 0;
	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idPlayer *candidate = gameLocal.GetClientByNum( i );
		if ( !BotObjectiveLivePlayer( self, candidate ) || candidate->team == self->team ) {
			continue;
		}
		if ( ( candidate->GetPhysics()->GetOrigin() - origin ).LengthSqr() <= radiusSqr ) {
			count++;
		}
	}
	return count;
}

/*
================
BotObjectiveTeamCarrier

The carrier a whole team agrees on, by lowest client number.

Deliberately not "the one nearest me".  The role allocator below promises to
produce the same mask for every bot on a team, and it can only keep that promise
if every bot hands it the same targetOrigin and the same excluded player.  With
one flag of each colour there is only ever one carrier and the distinction does
not arise, but DeadZone runs several artifacts at once by design, and there a
per-bot answer makes each bot solve a different allocation: every one of them
can come out an interceptor and nobody escorts the carrier or holds the zone -
in the one mode whose entire scoring rule is standing in the zone.

Which carrier is picked matters far less than that everyone picks the same one;
the allocator still assigns the closest useful players to it.
================
*/
static idPlayer *BotObjectiveTeamCarrier( idPlayer *self, int team, int powerup ) {
	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idPlayer *candidate = gameLocal.GetClientByNum( i );
		if ( candidate == self || !BotObjectiveLivePlayer( self, candidate ) ||
			 candidate->team != team || !candidate->PowerUpActive( powerup ) ) {
			continue;
		}

		return candidate;
	}

	return NULL;
}

/*
================
BotObjectiveFlagPowerup
================
*/
static int BotObjectiveFlagPowerup( int flagTeam ) {
	return ( flagTeam == TEAM_MARINE ) ? POWERUP_CTF_MARINEFLAG : POWERUP_CTF_STROGGFLAG;
}

/*
================
BotObjectiveFindFlag

GetFlagEntity is authoritative for an active team flag, but stores only one
pointer per team and cannot represent TEAM_MAX.  The guarded entity scan is the
fallback for instance copies, neutral flags, and the hidden permanent base used
as a capture destination while its flag is being carried.
================
*/
static rvItemCTFFlag *BotObjectiveFindFlag( idPlayer *self, int flagTeam,
										bool activeOnly, bool baseOnly ) {
	rvItemCTFFlag *best = NULL;
	float bestDistanceSqr = idMath::INFINITY;
	const idVec3 selfOrigin = self->GetPhysics()->GetOrigin();

	if ( activeOnly && flagTeam >= TEAM_MARINE && flagTeam < TEAM_MAX ) {
		idEntity *authoritative = gameLocal.mpGame.GetFlagEntity( flagTeam );
		if ( authoritative && authoritative->IsType( rvItemCTFFlag::GetClassType() ) &&
			 BotObjectiveSameInstance( self, authoritative ) ) {
			rvItemCTFFlag *flag = static_cast<rvItemCTFFlag *>( authoritative );
			if ( flag->spawnArgs.GetInt( "team", "-1" ) == flagTeam &&
				 !flag->pickedUp && !flag->IsHidden() && flag->GetPhysics() ) {
				return flag;
			}
		}
	}

	for ( idEntity *entity = gameLocal.spawnedEntities.Next(); entity != NULL;
		  entity = entity->spawnNode.Next() ) {
		if ( !entity->IsType( rvItemCTFFlag::GetClassType() ) ||
			 !BotObjectiveSameInstance( self, entity ) ||
			 entity->spawnArgs.GetInt( "team", "-1" ) != flagTeam ) {
			continue;
		}

		rvItemCTFFlag *flag = static_cast<rvItemCTFFlag *>( entity );
		if ( baseOnly && flag->spawnArgs.GetBool( "dropped", "0" ) ) {
			continue;
		}
		if ( activeOnly && ( flag->pickedUp || flag->IsHidden() ) ) {
			continue;
		}
		if ( !flag->GetPhysics() ) {
			continue;
		}

		const float distanceSqr = ( flag->GetPhysics()->GetOrigin() - selfOrigin ).LengthSqr();
		if ( distanceSqr < bestDistanceSqr ) {
			bestDistanceSqr = distanceSqr;
			best = flag;
		}
	}

	return best;
}

/*
================
BotObjectiveNextAssaultPoint

The multiplayer manager's legacy NextAP query is global.  Selecting from its
authored chain here preserves the index ordering while filtering tournament
instances and stale pointers before a bot commits to a route.
================
*/
static rvCTF_AssaultPoint *BotObjectiveNextAssaultPoint( idPlayer *self, int team ) {
	rvCTF_AssaultPoint *best = NULL;
	int bestIndex = ( team == TEAM_MARINE ) ? MAX_AP : -1;

	for ( int i = 0; i < gameLocal.mpGame.assaultPoints.Num(); i++ ) {
		rvCTF_AssaultPoint *point = gameLocal.mpGame.assaultPoints[i].GetEntity();
		if ( !point || !BotObjectiveSameInstance( self, point ) || !point->GetPhysics() ||
			 point->GetOwner() == team ) {
			continue;
		}

		const int index = point->GetIndex();
		if ( ( team == TEAM_MARINE && index < bestIndex ) ||
			 ( team == TEAM_STROGG && index > bestIndex ) ) {
			best = point;
			bestIndex = index;
		}
	}

	return best;
}

/*
================
BotObjectiveFromOrder

What a standing order means in the current mode.  False leaves the bot to its
own judgement for now - a follow order whose leader is dead, an attack order
outside CTF - without cancelling the order.
================
*/
static bool BotObjectiveFromOrder( idPlayer *self, const botOrder_t &order, botObjective_t &out ) {
	switch ( order.kind ) {
		case BOTORDER_FOLLOW: {
			idPlayer *leader = gameLocal.GetClientByNum( order.commander );
			idVec3 leaderOrigin;
			if ( !leader || !BotObjectiveLivePlayer( self, leader ) || leader->team != self->team ||
				 !BotObjectiveOrigin( leader, leaderOrigin ) ) {
				return false;
			}
			return BotObjectiveSet( out, BOTOBJ_ESCORT, leader, leaderOrigin,
				BOTOBJ_PRIORITY_ESCORT + BOTOBJ_ORDER_BONUS, false );
		}

		case BOTORDER_DEFEND: {
			idEntity *anchor = order.anchor.GetEntity();
			if ( !anchor && MPGameTypeHasAny( gameLocal.gameType, GTF_FLAG ) ) {
				anchor = BotObjectiveFindFlag( self, self->team, false, true );
			}
			idVec3 anchorOrigin;
			if ( !anchor || !BotObjectiveSameInstance( self, anchor ) || !BotObjectiveOrigin( anchor, anchorOrigin ) ) {
				return false;
			}
			return BotObjectiveSet( out, BOTOBJ_DEFEND, anchor, anchorOrigin,
				BOTOBJ_PRIORITY_DEFEND + BOTOBJ_ORDER_BONUS, true );
		}

		case BOTORDER_ATTACK: {
			if ( !MPGameTypeHasAny( gameLocal.gameType, GTF_FLAG ) ) {
				return false;
			}
			// The flag to take, wherever it lies; once a team-mate has it, its
			// escort is where an attacker is useful.
			const bool oneFlag = MPGameTypeHasAny( gameLocal.gameType, GTF_ONEFLAG );
			const int enemyTeam = gameLocal.mpGame.OpposingTeam( self->team );
			rvItemCTFFlag *flag = BotObjectiveFindFlag( self, oneFlag ? TEAM_MAX : enemyTeam, true, false );
			idVec3 flagOrigin;
			if ( flag && BotObjectiveOrigin( flag, flagOrigin ) ) {
				return BotObjectiveSet( out, BOTOBJ_FETCH, flag, flagOrigin,
					BOTOBJ_PRIORITY_FETCH + BOTOBJ_ORDER_BONUS, false );
			}
			idPlayer *carrier = BotObjectiveTeamCarrier( self, self->team,
				oneFlag ? POWERUP_CTF_ONEFLAG : BotObjectiveFlagPowerup( enemyTeam ) );
			idVec3 carrierOrigin;
			if ( carrier && BotObjectiveOrigin( carrier, carrierOrigin ) ) {
				return BotObjectiveSet( out, BOTOBJ_ESCORT, carrier, carrierOrigin,
					BOTOBJ_PRIORITY_ESCORT + BOTOBJ_ORDER_BONUS, false );
			}
			return false;
		}

		default:
			return false;
	}
}

/*
================
BotObjectiveBlockOrdered

Bots under a team order are not the allocator's to place.  Every bot on the
team marks the same ones - the orders table is shared - so the masks the
allocator promises to agree on still agree.

Only an order that means something right now takes a bot out of the
allocation.  An attack order outside CTF, or a follow order whose leader is
dead, resolves to nothing and leaves the bot to its own judgement - and a bot
left to its own judgement must still be handed a role, or "everyone attack"
in Freeze Tag stopped every rescue for the rest of the map and a follow
order left the bot with no objective at all while its leader was dead.
================
*/
static void BotObjectiveBlockOrdered( idPlayer *self, bool blocked[MAX_CLIENTS] ) {
	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idPlayer *candidate = gameLocal.GetClientByNum( i );
		if ( !candidate || candidate->team != self->team ) {
			continue;
		}
		const botOrder_t *order = botManager.GetOrder( i );
		if ( !order ) {
			continue;
		}
		botObjective_t resolved;
		if ( BotObjectiveFromOrder( candidate, *order, resolved ) ) {
			blocked[i] = true;
		}
	}
}

/*
================
BotFindCTFObjective
================
*/
static bool BotFindCTFObjective( idPlayer *self, botObjective_t &out ) {
	const int ownTeam = self->team;
	const int enemyTeam = gameLocal.mpGame.OpposingTeam( ownTeam );
	const bool oneFlag = MPGameTypeHasAny( gameLocal.gameType, GTF_ONEFLAG );
	const int carriedPowerup = oneFlag ? POWERUP_CTF_ONEFLAG : BotObjectiveFlagPowerup( enemyTeam );

	// A carrier completes the rules' next required step even while fighting.
	if ( self->PowerUpActive( carriedPowerup ) ) {
		if ( !oneFlag ) {
			rvCTF_AssaultPoint *point = BotObjectiveNextAssaultPoint( self, ownTeam );
			idVec3 pointOrigin;
			if ( point && BotObjectiveOrigin( point, pointOrigin ) ) {
				return BotObjectiveSet( out, BOTOBJ_CAPTURE, point, pointOrigin,
					BOTOBJ_PRIORITY_CAPTURE, false );
			}
		}

		const int baseTeam = oneFlag ? enemyTeam : ownTeam;
		rvItemCTFFlag *base = BotObjectiveFindFlag( self, baseTeam, false, true );
		idVec3 baseOrigin;
		if ( base && BotObjectiveOrigin( base, baseOrigin ) ) {
			return BotObjectiveSet( out, BOTOBJ_CAPTURE, base, baseOrigin,
				BOTOBJ_PRIORITY_CAPTURE, true );
		}
		return false;
	}

	// Resolve all live states before assigning roles.  Standard CTF indexes only
	// real team flags; One Flag deliberately discovers TEAM_MAX through entities
	// and the carrier powerup, never through the two-element flag-state arrays.
	const int hostilePowerup = oneFlag ? POWERUP_CTF_ONEFLAG : BotObjectiveFlagPowerup( ownTeam );
	const int friendlyPowerup = oneFlag ? POWERUP_CTF_ONEFLAG : BotObjectiveFlagPowerup( enemyTeam );
	idPlayer *enemyCarrier = BotObjectiveTeamCarrier( self, enemyTeam, hostilePowerup );
	idPlayer *friendlyCarrier = BotObjectiveTeamCarrier( self, ownTeam, friendlyPowerup );

	rvCTFGameState *state = NULL;
	rvGameState *rawState = gameLocal.mpGame.GetGameState();
	if ( rawState && rawState->IsType( rvCTFGameState::GetClassType() ) ) {
		state = static_cast<rvCTFGameState *>( rawState );
	}

	// A standing order comes after the carrier's duty and before any role.
	const botOrder_t *order = botManager.GetOrder( self->entityNumber );
	if ( order && BotObjectiveFromOrder( self, *order, out ) ) {
		return true;
	}

	rvItemCTFFlag *droppedOwnFlag = NULL;
	if ( !oneFlag && state && state->GetFlagState( ownTeam ) == FS_DROPPED ) {
		rvItemCTFFlag *candidate = BotObjectiveFindFlag( self, ownTeam, true, false );
		if ( candidate && candidate->spawnArgs.GetBool( "dropped", "0" ) ) {
			droppedOwnFlag = candidate;
		}
	}

	bool assigned[MAX_CLIENTS];
	bool selected[MAX_CLIENTS];
	memset( assigned, 0, sizeof( assigned ) );
	BotObjectiveBlockOrdered( self, assigned );
	const int selfNum = self->entityNumber;

	// One or two nearby returners are enough; the rest keep pressure on the
	// other objective instead of forming a queue behind one dropped flag.
	if ( droppedOwnFlag ) {
		idVec3 flagOrigin;
		if ( BotObjectiveOrigin( droppedOwnFlag, flagOrigin ) ) {
			const int available = BotObjectiveCountRolePlayers( self, friendlyCarrier, assigned );
			const int returners = Min( available, available >= 5 ? 2 : 1 );
			BotObjectiveSelectClosestRolePlayers( self, flagOrigin, returners,
				friendlyCarrier, assigned, selected, BOTOBJ_RETURN, droppedOwnFlag );
			if ( selected[selfNum] ) {
				return BotObjectiveSet( out, BOTOBJ_RETURN, droppedOwnFlag, flagOrigin,
					BOTOBJ_PRIORITY_RETURN + 9.0f, false );
			}
			BotObjectiveMergeRoles( assigned, selected );
		}
	}

	// Split a flag standoff: the closest defenders chase their carrier while
	// the remaining bots can escort ours.  Without a friendly carrier, commit a
	// clear majority but retain one counter-attacker on teams large enough.
	if ( enemyCarrier ) {
		idVec3 carrierOrigin;
		if ( BotObjectiveOrigin( enemyCarrier, carrierOrigin ) ) {
			const int available = BotObjectiveCountRolePlayers( self, friendlyCarrier, assigned );
			const int interceptors = friendlyCarrier ? BotObjectiveSupportSlots( available ) :
				BotObjectiveMajoritySlots( available );
			BotObjectiveSelectClosestRolePlayers( self, carrierOrigin, interceptors,
				friendlyCarrier, assigned, selected, BOTOBJ_INTERCEPT, enemyCarrier );
			if ( selected[selfNum] ) {
				const float urgency = friendlyCarrier ? 0.0f : 6.0f;
				return BotObjectiveSet( out, BOTOBJ_INTERCEPT, enemyCarrier, carrierOrigin,
					BOTOBJ_PRIORITY_INTERCEPT + urgency, false );
			}
			BotObjectiveMergeRoles( assigned, selected );
		}
	}

	if ( friendlyCarrier ) {
		idVec3 carrierOrigin;
		if ( BotObjectiveOrigin( friendlyCarrier, carrierOrigin ) ) {
			const int available = BotObjectiveCountRolePlayers( self, friendlyCarrier, assigned );
			int escorts = BotObjectiveSupportSlots( available );
			const bool carrierHurt = friendlyCarrier->health <=
				Max( 35, friendlyCarrier->inventory.maxHealth / 2 );
			if ( carrierHurt ) {
				escorts = Min( available, escorts + 1 );
			}
			BotObjectiveSelectClosestRolePlayers( self, carrierOrigin, escorts,
				friendlyCarrier, assigned, selected, BOTOBJ_ESCORT, friendlyCarrier );
			if ( selected[selfNum] ) {
				return BotObjectiveSet( out, BOTOBJ_ESCORT, friendlyCarrier, carrierOrigin,
					BOTOBJ_PRIORITY_ESCORT + ( carrierHurt ? 8.0f : 0.0f ), false );
			}
			BotObjectiveMergeRoles( assigned, selected );
		}
	}

	const int fetchTeam = oneFlag ? TEAM_MAX : enemyTeam;
	rvItemCTFFlag *flag = BotObjectiveFindFlag( self, fetchTeam, true, false );
	idVec3 flagOrigin;
	if ( flag && BotObjectiveOrigin( flag, flagOrigin ) ) {
		const int available = BotObjectiveCountRolePlayers( self, friendlyCarrier, assigned );
		const int attackers = available <= 2 ? available : BotObjectiveMajoritySlots( available );
		BotObjectiveSelectClosestRolePlayers( self, flagOrigin, attackers,
			friendlyCarrier, assigned, selected, BOTOBJ_FETCH, flag );
		if ( selected[selfNum] ) {
			return BotObjectiveSet( out, BOTOBJ_FETCH, flag, flagOrigin,
				BOTOBJ_PRIORITY_FETCH, false );
		}
		BotObjectiveMergeRoles( assigned, selected );
	}

	// A deliberately unassigned bot guards the capture end instead of chasing
	// the same flag.  BOTOBJ_CONTROL already supplies the required hold-volume
	// behavior in Bot.cpp and works with the authored flag bounds.
	const int defendTeam = ownTeam;
	rvItemCTFFlag *base = BotObjectiveFindFlag( self, defendTeam, false, true );
	idVec3 baseOrigin;
	if ( !assigned[selfNum] && base && BotObjectiveOrigin( base, baseOrigin ) ) {
		return BotObjectiveSet( out, BOTOBJ_DEFEND, base, baseOrigin,
			BOTOBJ_PRIORITY_DEFEND + ( enemyCarrier ? 10.0f : 0.0f ), true );
	}

	return false;
}

/*
================
BotFindFreezeTagObjective
================
*/
static bool BotFindFreezeTagObjective( idPlayer *self, botObjective_t &out ) {
	rvRoundGameState *state = static_cast<rvRoundGameState *>( gameLocal.mpGame.GetGameState() );
	if ( !state || !state->RoundIsLive() ) {
		return false;
	}

	idEntity *frozenBodies[MAX_CLIENTS];
	int frozenCount = 0;
	int liveTeamCount = 0;

	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idPlayer *candidate = gameLocal.GetClientByNum( i );
		if ( !candidate || candidate->team != self->team ||
			 candidate->entityNumber < 0 || candidate->entityNumber >= MAX_CLIENTS ||
			 !BotObjectiveSameInstance( self, candidate ) || !candidate->GetPhysics() ||
			 !gameLocal.mpGame.CanPlay( candidate ) || candidate->wantSpectate ||
			 candidate->spectating ) {
			continue;
		}

		if ( candidate->health <= 0 ) {
			if ( candidate != self && frozenCount < MAX_CLIENTS ) {
				frozenBodies[frozenCount++] = candidate;
			}
		} else {
			liveTeamCount++;
		}
	}

	const botOrder_t *order = botManager.GetOrder( self->entityNumber );
	if ( order && BotObjectiveFromOrder( self, *order, out ) ) {
		return true;
	}

	if ( !frozenCount ) {
		return false;
	}

	bool blocked[MAX_CLIENTS];
	memset( blocked, 0, sizeof( blocked ) );
	BotObjectiveBlockOrdered( self, blocked );
	idEntity *assignedBody = BotObjectiveAssignUniqueTarget( self, frozenBodies, frozenCount, blocked, BOTOBJ_RESCUE );
	idVec3 frozenOrigin;
	if ( !assignedBody || !BotObjectiveOrigin( assignedBody, frozenOrigin ) ) {
		return false;
	}

	const float frozenRatio = (float)frozenCount / (float)Max( 1, frozenCount + liveTeamCount );
	const int pressure = BotObjectiveNearbyEnemies( self, frozenOrigin, 640.0f );
	const float urgency = frozenRatio * 8.0f + ( liveTeamCount <= 1 ? 3.0f : 0.0f ) +
		Min( 3.0f, (float)pressure * 1.5f );
	return BotObjectiveSet( out, BOTOBJ_RESCUE, assignedBody, frozenOrigin,
		BOTOBJ_PRIORITY_RESCUE + urgency, true );
}

/*
================
BotObjectiveFindControlZone

controlZone uses the stock trigger encoding: team + 1, or 3 for both teams.
The requiresDeadZonePowerup key is authoritative for maps whose zone can be
held without an artifact.  Disabled triggers have no trigger contents and are
not valid tactical destinations.
================
*/
static idTrigger_Multi *BotObjectiveFindControlZone( idPlayer *self,
											  bool carrier, idVec3 &zoneOrigin ) {
	idTrigger_Multi *best = NULL;
	float bestDistanceSqr = idMath::INFINITY;
	const idVec3 selfOrigin = self->GetPhysics()->GetOrigin();

	for ( idEntity *entity = gameLocal.spawnedEntities.Next(); entity != NULL;
		  entity = entity->spawnNode.Next() ) {
		if ( !entity->IsType( idTrigger_Multi::GetClassType() ) ||
			 !BotObjectiveSameInstance( self, entity ) || !entity->GetPhysics() ||
			 !( entity->GetPhysics()->GetContents() & CONTENTS_TRIGGER ) ) {
			continue;
		}

		const int zoneTeam = entity->spawnArgs.GetInt( "controlZone", "0" );
		if ( zoneTeam != self->team + 1 && zoneTeam != 3 ) {
			continue;
		}
		if ( !carrier && entity->spawnArgs.GetBool( "requiresDeadZonePowerup", "1" ) ) {
			continue;
		}

		const idVec3 candidateOrigin = entity->GetPhysics()->GetAbsBounds().GetCenter();
		const float distanceSqr = ( candidateOrigin - selfOrigin ).LengthSqr();
		if ( distanceSqr < bestDistanceSqr ) {
			bestDistanceSqr = distanceSqr;
			best = static_cast<idTrigger_Multi *>( entity );
			zoneOrigin = candidateOrigin;
		}
	}

	return best;
}

/*
================
BotObjectiveCollectArtifacts
================
*/
static int BotObjectiveCollectArtifacts( idPlayer *self, idEntity *artifacts[MAX_CLIENTS] ) {
	int count = 0;
	for ( idEntity *entity = gameLocal.spawnedEntities.Next(); entity != NULL && count < MAX_CLIENTS;
		  entity = entity->spawnNode.Next() ) {
		if ( !entity->IsType( riDeadZonePowerup::GetClassType() ) ||
			 !BotObjectiveSameInstance( self, entity ) ) {
			continue;
		}

		riDeadZonePowerup *artifact = static_cast<riDeadZonePowerup *>( entity );
		if ( artifact->powerup != POWERUP_DEADZONE || artifact->pickedUp ||
			 artifact->IsHidden() || !artifact->GetPhysics() ) {
			continue;
		}
		artifacts[count++] = artifact;
	}
	return count;
}

/*
================
BotFindDeadZoneObjective
================
*/
static bool BotFindDeadZoneObjective( idPlayer *self, botObjective_t &out ) {
	riDZGameState *state = NULL;
	rvGameState *rawState = gameLocal.mpGame.GetGameState();
	if ( rawState && rawState->IsType( riDZGameState::GetClassType() ) ) {
		state = static_cast<riDZGameState *>( rawState );
	}

	const int enemyTeam = gameLocal.mpGame.OpposingTeam( self->team );
	const dzState_t ownZoneState = state ? state->GetDZState( self->team ) : DZ_NONE;
	const dzState_t enemyZoneState = state ? state->GetDZState( enemyTeam ) : DZ_NONE;
	bool ownControls = ( ownZoneState == DZ_TAKEN );
	bool enemyControls = ( enemyZoneState == DZ_TAKEN );
	bool deadlocked = ( ownZoneState == DZ_DEADLOCK || enemyZoneState == DZ_DEADLOCK );

	// Both slots reading TAKEN is not a state the rules can mean.  The zone
	// transition used to clear only the Marine slot before recording a new
	// holder, so after a contested fight the loser's slot kept saying TAKEN
	// for the rest of the hold; a team that trusted either slot picked a side
	// at random.  Read it as the contest it describes until the slots agree.
	if ( ownControls && enemyControls ) {
		ownControls = false;
		enemyControls = false;
		deadlocked = true;
	}

	idVec3 zoneOrigin;
	if ( self->PowerUpActive( POWERUP_DEADZONE ) ) {
		idTrigger_Multi *zone = BotObjectiveFindControlZone( self, true, zoneOrigin );
		if ( zone ) {
			return BotObjectiveSet( out, BOTOBJ_CONTROL, zone, zoneOrigin,
				BOTOBJ_PRIORITY_CARRIER, true );
		}
		return false;
	}

	const botOrder_t *order = botManager.GetOrder( self->entityNumber );
	if ( order && BotObjectiveFromOrder( self, *order, out ) ) {
		return true;
	}

	idPlayer *enemyCarrier = BotObjectiveTeamCarrier( self, enemyTeam, POWERUP_DEADZONE );
	idPlayer *friendlyCarrier = BotObjectiveTeamCarrier( self, self->team, POWERUP_DEADZONE );
	bool assigned[MAX_CLIENTS];
	bool selected[MAX_CLIENTS];
	memset( assigned, 0, sizeof( assigned ) );
	BotObjectiveBlockOrdered( self, assigned );
	const int selfNum = self->entityNumber;

	if ( enemyCarrier ) {
		idVec3 carrierOrigin;
		if ( BotObjectiveOrigin( enemyCarrier, carrierOrigin ) ) {
			const int available = BotObjectiveCountRolePlayers( self, friendlyCarrier, assigned );
			const int interceptors = ( enemyControls || deadlocked ) ?
				BotObjectiveMajoritySlots( available ) : BotObjectiveSupportSlots( available );
			BotObjectiveSelectClosestRolePlayers( self, carrierOrigin, interceptors,
				friendlyCarrier, assigned, selected, BOTOBJ_INTERCEPT, enemyCarrier );
			if ( selected[selfNum] ) {
				return BotObjectiveSet( out, BOTOBJ_INTERCEPT, enemyCarrier, carrierOrigin,
					BOTOBJ_PRIORITY_INTERCEPT + ( enemyControls ? 8.0f : ( deadlocked ? 5.0f : 0.0f ) ),
					false );
			}
			BotObjectiveMergeRoles( assigned, selected );
		}
	}

	if ( friendlyCarrier ) {
		idVec3 carrierOrigin;
		if ( BotObjectiveOrigin( friendlyCarrier, carrierOrigin ) ) {
			const int available = BotObjectiveCountRolePlayers( self, friendlyCarrier, assigned );
			const int escorts = ( ownControls || deadlocked ) ?
				BotObjectiveSupportSlots( available ) : BotObjectiveMajoritySlots( available );
			BotObjectiveSelectClosestRolePlayers( self, carrierOrigin, escorts,
				friendlyCarrier, assigned, selected, BOTOBJ_ESCORT, friendlyCarrier );
			if ( selected[selfNum] ) {
				return BotObjectiveSet( out, BOTOBJ_ESCORT, friendlyCarrier, carrierOrigin,
					BOTOBJ_PRIORITY_ESCORT + ( deadlocked ? 10.0f : ( ownControls ? 6.0f : 0.0f ) ),
					false );
			}
			BotObjectiveMergeRoles( assigned, selected );
		}
	}

	// Some layouts explicitly waive the artifact requirement.  Contest an
	// enemy-held/deadlocked zone with a majority; once safely owned, one nearby
	// bot is enough to anchor it while team mates collect supplies and fight.
	idTrigger_Multi *openZone = BotObjectiveFindControlZone( self, false, zoneOrigin );
	if ( openZone ) {
		const int available = BotObjectiveCountRolePlayers( self, friendlyCarrier, assigned );
		const int controllers = ownControls && !deadlocked ? Min( 1, available ) :
			BotObjectiveMajoritySlots( available );
		BotObjectiveSelectClosestRolePlayers( self, zoneOrigin, controllers,
			friendlyCarrier, assigned, selected, BOTOBJ_CONTROL, openZone );
		if ( selected[selfNum] ) {
			const float urgency = enemyControls ? 16.0f : ( deadlocked ? 12.0f :
				( ownControls ? 2.0f : 7.0f ) );
			return BotObjectiveSet( out, BOTOBJ_CONTROL, openZone, zoneOrigin,
				BOTOBJ_PRIORITY_CONTROL + urgency, true );
		}
		BotObjectiveMergeRoles( assigned, selected );
	}

	// Assign one collector to each live artifact.  Multiple spawn tokens now
	// produce multiple routes instead of every bot selecting whichever is
	// nearest to itself and converging on the same pickup.
	idEntity *artifacts[MAX_CLIENTS];
	const int artifactCount = BotObjectiveCollectArtifacts( self, artifacts );
	idEntity *artifact = BotObjectiveAssignUniqueTarget( self, artifacts, artifactCount, assigned, BOTOBJ_FETCH );
	idVec3 artifactOrigin;
	if ( !assigned[selfNum] && artifact && BotObjectiveOrigin( artifact, artifactOrigin ) ) {
		return BotObjectiveSet( out, BOTOBJ_FETCH, artifact, artifactOrigin,
			BOTOBJ_PRIORITY_FETCH + ( enemyControls ? 12.0f : ( deadlocked ? 8.0f : 0.0f ) ),
			false );
	}

	return false;
}

/*
================
BotFindObjective
================
*/
bool BotFindObjective( idPlayer *self, botObjective_t &out ) {
	out.Clear();

	if ( !self || !gameLocal.isMultiplayer || gameLocal.isClient ||
		 self->entityNumber < 0 || self->entityNumber >= MAX_CLIENTS ||
		 !self->GetPhysics() || self->spectating || self->health <= 0 ||
		 !BotObjectiveValidTeam( self->team ) ||
		 !gameLocal.mpGame.CanPlay( self ) || !BotObjectiveMatchIsLive() ) {
		return false;
	}

	// These descriptors exist so menus and map filtering can evolve without
	// renumbering the protocol, but their authoritative state/entity rules are
	// not implemented yet.  Do not mistake borrowed CTF entities for a working
	// objective: doing so makes bots chase pickups that cannot score.
	if ( gameLocal.gameType == GAME_ATTACK_DEFEND ||
		 gameLocal.gameType == GAME_OVERLOAD ||
		 gameLocal.gameType == GAME_HARVESTER ||
		 gameLocal.gameType == GAME_DOMINATION ) {
		return false;
	}

	if ( MPGameTypeHasAny( gameLocal.gameType, GTF_FLAG ) ) {
		return BotFindCTFObjective( self, out );
	}
	if ( gameLocal.gameType == GAME_FREEZETAG ) {
		return BotFindFreezeTagObjective( self, out );
	}
	if ( gameLocal.gameType == GAME_DEADZONE ) {
		return BotFindDeadZoneObjective( self, out );
	}

	// Team Deathmatch and the round modes have no objective of their own, but
	// an order to follow or to hold a spot still is one.
	const botOrder_t *order = gameLocal.IsTeamGame() ? botManager.GetOrder( self->entityNumber ) : NULL;
	if ( order ) {
		return BotObjectiveFromOrder( self, *order, out );
	}

	return false;
}
