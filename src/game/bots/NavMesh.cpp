//----------------------------------------------------------------
// NavMesh.cpp
//
// Runtime navigation mesh generation and routing.  See NavMesh.h for why this
// exists instead of AAS.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "NavMesh.h"

rvNavMesh					navMesh;

/*
===============================================================================

	Generation constants.

	Everything that describes the agent is derived from the movement cvars the
	server is actually running, so a mod that widens the player also widens the
	navmesh without touching this file.

===============================================================================
*/

// Only CONTENTS_SOLID and CONTENTS_PLAYERCLIP: MASK_PLAYERSOLID also carries
// CONTENTS_BODY, and generation must not see players standing in doorways.
#define NAV_CLIP_MASK			( CONTENTS_SOLID | CONTENTS_PLAYERCLIP )

static const float	NAV_WALKABLE_NORMAL_Z	= 0.7f;		// ~45 degrees, matches idPhysics_Player
// Furthest one-way ledge drop we will route over. Multiplayer disables the
// fatal damage tier, but a 375-unit fall still costs 10 HP and can kill an
// injured player; selected routes also obey that bot's landing-damage limit.
// Stock maps hide power items behind 224-576 unit drops (the Red Armor in
// q4ctf6's sunken centre, the Quad in q4xdm13's basin), so 192 left those
// unreachable and turned ordinary drops into detours up to fifteen times
// longer.  Kill pits below this are refused by their own hazard volumes.
static const float	NAV_MAX_DROP			= 512.0f;
// Falls this far deal the soft and hard landing damage in multiplayer.
static const float	NAV_SOFT_FALL			= 250.0f;
static const float	NAV_HARD_FALL			= 375.0f;
static const float	NAV_FALL_DAMAGE_PENALTY	= 64.0f;
static const float	NAV_MERGE_HEIGHT		= 28.0f;	// two floors this close in a cell are one node
static const float	NAV_GROUND_OFFSET		= 1.0f;		// lift traces off the surface they start on
static const int	NAV_MAX_NODES			= 65536;
static const int	NAV_HASH_BUCKETS		= 16384;	// cell hash size; idHashIndex never rehashes
static const int	NAV_MAX_WALKABLE_PROBES	= 48;		// collision probes one snap query may spend
static const int	NAV_SMOOTH_LOOKAHEAD	= 8;		// nodes a single string-pull step may skip
static const float	NAV_SMOOTH_MAX_DIP		= 48.0f;	// how far a shortcut may float above the ground it crosses
static const float	NAV_WALK_RISE_SLACK		= 1.0f;		// over a step, between half-samples: hull and slope rounding
static const float	NAV_LEG_SIDE_SLACK		= 14.0f;	// how far beside a corner a bot counts it reached: half BOT_CORNER_REACH
static const float	NAV_RIM_OVERHANG		= 8.0f;		// a node whose hull hangs further than this past an edge is a rim node
static const float	NAV_RIM_PENALTY			= 32.0f;	// per link into a rim node: more than a cell, so the chain inland wins
static const int	NAV_MAX_ROUTE_REPAIRS	= 16;		// blocked graph edges excluded before giving up
static const int	NAV_SNAP_RETRY_MSEC		= 5000;		// a failed entity snap is probed again after this

// Extra cost so a route prefers walking around to falling or jumping, when
// more than one works.
static const float	NAV_DROP_PENALTY		= 96.0f;
static const float	NAV_JUMP_PENALTY		= 64.0f;
// Volume links are near instant to traverse but should not be spammed.
static const float	NAV_JUMPPAD_COST		= 128.0f;
static const float	NAV_TELEPORT_COST		= 64.0f;

// "No route" as a finite number, so landmark arithmetic stays defined.
static const float	NAV_INFINITE_COST		= 1.0e30f;
static const float	NAV_UNREACHED			= 1.0e29f;	// anything at or above this is infinite
// Stepping onto ground inside a survivable pain volume.  Large enough that a
// route detours around a pain floor whenever a sane detour exists.
static const float	NAV_HAZARD_PENALTY		= 384.0f;
// One touch of a trigger_hurt this strong kills a fresh spawn outright, and a
// volume burning this fast kills anyone who lingers - q4xdm14's lava pools are
// 40 HP a second of trigger_hurt over clip, with no lava contents at all.
static const float	NAV_LETHAL_HURT_DAMAGE	= 100.0f;
static const float	NAV_LETHAL_HURT_RATE	= 30.0f;
// A mover this close above a lethal volume is a trapdoor over it.
static const float	NAV_TRAPDOOR_DEPTH		= 192.0f;

// Running jumps across gaps.  The ballistic proof assumes a fraction of full
// run speed and a takeoff a little before the edge node, so a jump the graph
// offers is one a bot actually makes.
static const float	NAV_GAP_SPEED_FRACTION	= 0.85f;
static const float	NAV_GAP_TAKEOFF_SLACK	= 16.0f;
static const float	NAV_GAP_MAX_FALL		= 128.0f;	// landing at most this far below the takeoff
static const float	NAV_GAP_PENALTY			= 128.0f;	// a gap jump can be missed; prefer walking round
static const float	NAV_GAP_RUNUP			= 56.0f;	// straight approach a bot takes before a gap jump
static const float	NAV_LANDING_SUPPORT		= 8.0f;		// support probes around an airborne landing's centre
static const float	NAV_TRANSPORT_GOAL_REACH = 64.0f;	// a goal touching a pad ends its route this close
static const float	NAV_SWEEP_SLACK			= 2.0f;		// a sweep stopped this short of its end arrived
static const float	NAV_GOAL_TOUCH_REACH	= 24.0f;	// a box stopped this near a goal overlaps what stands there
static const float	NAV_NEAR_GOAL_REACH		= 128.0f;	// a pursued player is approached to within this of its spot
static const float	NAV_PAD_STEP_SECONDS	= 0.016f;	// pad arc integration step: one multiplayer frame
static const int	NAV_PAD_MAX_STEPS		= 625;		// ten seconds of flight before a pad arc is given up
static const float	NAV_PAD_STEER_REACH		= 256.0f;	// floors this far round a pad's apex are candidate landings
static const float	NAV_PAD_LEVEL_BAND		= 48.0f;	// candidates closer in height than this share a floor level
static const int	NAV_PAD_MAX_LEVELS		= 6;
static const float	NAV_PAD_LANDING_REACH	= 32.0f;	// a steered flight this close to its candidate came down on it
static const float	NAV_PAD_RIDE_SHORT		= 8.0f;		// a ridden arc may come down this far short of its landing (BotRidesPadArc)
static const float	NAV_PAD_RIDE_SIDE		= 32.0f;	// and this far beside it: a hull
static const float	NAV_PAD_RIDE_OVERSHOOT	= 128.0f;	// past that it came down somewhere else
static const float	NAV_PAD_AIR_ACCELERATE	= 1.18f;	// PM_AIRACCELERATE_MP: what a steering player adds in flight
static const float	NAV_DODGE_RUNOUT_SECONDS = 0.15f;	// grounded edge guard's momentum lookahead after a hop
static const int	NAV_MAX_LANDMARKS		= 8;
static const int	NAV_LANDMARK_STRIDE		= 2 * NAV_MAX_LANDMARKS;	// floats per node row: from, to per landmark
static const int	NAV_PRICING_MIN_BUDGET	= 2048;		// exact-search expansions one goal decision may spend
// The condensation closure is components^2 bits; past this it is skipped and
// cross-component queries fall back to searching.
static const int	NAV_MAX_CLOSURE_COMPONENTS = 4096;

static const int	navNeighbourX[8] = { 1,  1,  0, -1, -1, -1,  0,  1 };
static const int	navNeighbourY[8] = { 0,  1,  1,  1,  0, -1, -1, -1 };

// Loaded once per build and reused for every probe; rebuilding a trace model
// per trace dominates generation time otherwise.
static idClipModel *		navAgentModel = NULL;
static idTraceModel			navAgentTrace;
static idBounds				navAgentBounds;
static idTraceModel			navSupportTrace;	// the middle of the agent's footprint, NAV_LANDING_SUPPORT each way
static idBounds				navSupportBounds;

/*
================
NavCreateAgentTrace

Match idPlayer::SetClipModel, including the server's cylinder side count and
alignment.  A box can balance on a diagonal platform corner that the default
eight-sided player hull never touches, creating a fatal drop onto empty air.
================
*/
static idTraceModel NavCreateAgentTrace( const idBounds &bounds ) {
	int sides = pm_usecylinder.GetInteger();
	const bool alternateAlignment = sides > 0;
	sides = idMath::Abs( sides );
	if ( sides >= 1 ) {
		return idTraceModel( bounds, sides < 3 ? 8 : sides, alternateAlignment );
	}
	return idTraceModel( bounds );
}

/*
================
NavTravelVolumeBounds

Jump pads move their brush clip out of the entity physics object and into the
force-field evaluator during Spawn.  Teleporter triggers retain ordinary
physics bounds.  This keeps both generation and execution aimed at the volume
that actually activates.
================
*/
static bool NavTravelVolumeBounds( idEntity *ent, idBounds &bounds ) {
	if ( !ent ) {
		return false;
	}

	if ( ent->IsType( rvJumpPad::GetClassType() ) ) {
		return static_cast<rvJumpPad *>( ent )->GetForceFieldBounds( bounds );
	}

	if ( !ent->GetPhysics() ) {
		return false;
	}

	bounds = ent->GetPhysics()->GetAbsBounds();
	return true;
}

/*
================
NavTravelVolumeClip

The clip model a pad or a teleporter tests its contacts against, for the exact
touch test its bounding box only approximates.
================
*/
static const idClipModel *NavTravelVolumeClip( idEntity *ent ) {
	if ( !ent ) {
		return NULL;
	}
	if ( ent->IsType( rvJumpPad::GetClassType() ) ) {
		return static_cast<rvJumpPad *>( ent )->GetForceFieldClipModel();
	}
	return ent->GetPhysics() ? ent->GetPhysics()->GetClipModel() : NULL;
}

// A pad applies its velocity every frame that the player hull touches its
// actual brush, including a sloped trigger's upper portion during takeoff.
static bool NavPadVolumeTouches( idEntity *pad, const idVec3 &position ) {
	const idClipModel *clip = NavTravelVolumeClip( pad );
	if ( !clip ) {
		return false;
	}
	idBounds hull = rvNavMesh::GetAgentBounds();
	hull.TranslateSelf( position );
	return hull.IntersectsBounds( clip->GetAbsBounds() ) &&
		collisionModelManager->Contents( position, &navAgentTrace, mat3_identity, -1,
			clip->GetCollisionModel(), clip->GetOrigin(), clip->GetAxis() ) != 0;
}

// The force field replaces velocity on every touching frame. Share its launch
// calculation between route generation and a live coast caught by another pad.
static bool NavPadLaunchVelocity( idEntity *pad, const idVec3 &gravity, idVec3 &velocity ) {
	idEntity *target = ( pad && pad->targets.Num() ) ? pad->targets[0].GetEntity() : NULL;
	const float g = gravity.Length();
	if ( !target || !target->GetPhysics() || !pad->GetPhysics() || g <= 1.0f ) {
		return false;
	}
	const idVec3 down = gravity / g;
	const idVec3 diff = target->GetPhysics()->GetOrigin() - pad->GetPhysics()->GetOrigin();
	const idVec3 vert = ( diff * down ) * down;
	const float flightTime = idMath::Sqrt( vert.Length() / ( 0.5f * g ) );
	if ( flightTime <= 0.0f ) {
		return false;
	}
	idVec3 horizontal = diff - vert;
	const float span = horizontal.Normalize();
	velocity = horizontal * ( span / flightTime ) - gravity * flightTime;
	return true;
}

/*
================
NavDoorIsWalkActivated

Only an ordinary unlocked proximity door is equivalent to open floor.  Script,
key, shoot, and locked doors need decisions the movement layer cannot make and
must remain solid until their game logic opens them.
================
*/
static bool NavDoorIsWalkActivated( idEntity *ent ) {
	if ( !ent || !ent->IsType( idDoor::GetClassType() ) ) {
		return false;
	}

	idDoor *door = static_cast<idDoor *>( ent );
	return !door->IsLocked() && !door->IsNoTouch() &&
		   !door->spawnArgs.GetString( "requires", "" )[0];
}

// openQ4 BEGIN
/*
================
NavPointInLethalLiquid

Lava and slime are not solid, so every generation trace passes straight through them and the graph
would happily lay a walkable node on the bottom of a lava pool - then route bots across it. Refusing
those cells keeps routing out of the hazard entirely, which is what Quake 3's AAS achieves by
classifying liquid areas up front.

Water is deliberately not excluded. It is crossable, so it stays in the graph and swimming is
handled at run time by rvBot::UpdateLiquidMovement.
================
*/
static bool NavPointInLethalLiquid( const idVec3 &point ) {
	const idVec3 probe = point + idVec3( 0.0f, 0.0f, NAV_GROUND_OFFSET );
	return ( gameLocal.LiquidContentsAtPoint( probe, NULL ) & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) != 0;
}
// openQ4 END

// CrashLand squares the downward impact speed, then discounts water. A
// nodamage contact or water above the head suppresses fall damage entirely.
static float NavLandingImpactDelta( const trace_t &trace, const idVec3 &velocity, const idVec3 &gravity ) {
	const float g = gravity.Length();
	if ( g <= 1.0f || ( trace.c.material && ( trace.c.material->GetSurfaceFlags() & SURF_NODAMAGE ) ) ) {
		return 0.0f;
	}
	const idVec3 down = gravity / g;
	float delta = Square( Max( 0.0f, velocity * down ) ) * 0.0001f;
	const idBounds &bounds = rvNavMesh::GetAgentBounds();
	if ( gameLocal.LiquidContentsAtPoint( trace.endpos - down * ( bounds[0].z + 1.0f ), NULL ) & MASK_WATER ) {
		if ( gameLocal.LiquidContentsAtPoint( trace.endpos - down * ( ( bounds[1].z - bounds[0].z ) * 0.5f ), NULL ) & MASK_WATER ) {
			if ( gameLocal.LiquidContentsAtPoint( trace.endpos - down * ( bounds[1].z - 1.0f ), NULL ) & MASK_WATER ) {
				return 0.0f;
			}
			delta *= 0.25f;
		} else {
			delta *= 0.5f;
		}
	}
	return delta;
}

/*
================
NavHurtDamagePerTouch

What one touch of a trigger_hurt deals, from the live damage declaration so a
mod's retune is honoured.  A gib declaration kills regardless of its number.
================
*/
static float NavHurtDamagePerTouch( const idEntity *ent, bool &gibs ) {
	gibs = false;

	const char *damageName = ent->spawnArgs.GetString( "def_damage", "damage_painTrigger" );
	const idDict *damage = damageName[0] ? gameLocal.FindEntityDefDict( damageName, false ) : NULL;
	if ( !damage ) {
		return 0.0f;
	}

	gibs = damage->GetBool( "gib", "0" );
	return Max( 0.0f, damage->GetFloat( "damage", "0" ) );
}

/*
================
NavAgentTranslation

The agent sweep behind every navmesh probe: the world and every solid entity
except bodies.  Multiplayer players carry CONTENTS_SOLID for their combat box,
so the stock trace collided with them - with the bot asking for a route most
of all, whose own box overlaps every sweep it starts - and route snaps were
refused on the very floor the bot stood on: a third of all failed searches on
a busy q4dm5.  Players and corpses move; they are never architecture.

A navmesh built while a normal multiplayer door is closed must still contain
the floor on its far side.  Players activate those doors simply by approaching
them, so they are traversable rather than permanent walls.  Locked, keyed,
no-touch and scripted doors remain obstructions, as does every other solid
entity: movers are reconsidered by route searches at run time.
================
*/
static bool NavTraceTranslation( trace_t &trace, const idVec3 &start, const idVec3 &end,
								 const idTraceModel &model, const idBounds &bounds ) {
	static idClipModel *touching[MAX_GENTITIES];

	// The navmesh is built over the main instance's clip world.
	const idClip *clipWorld = gameLocal.GetEntityClipWorld( NULL );
	collisionModelManager->Translation( &trace, start, end, &model, mat3_identity, NAV_CLIP_MASK,
										clipWorld->GetWorldCollisionModel(), vec3_origin, mat3_identity );
	trace.c.entityNum = ( trace.fraction < 1.0f ) ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
	if ( trace.fraction <= 0.0f ) {
		return false;
	}

	idBounds swept;
	swept.FromBoundsTranslation( bounds, start, mat3_identity, trace.endpos - start );
	const int num = clipWorld->ClipModelsTouchingBounds( swept, NAV_CLIP_MASK, touching, MAX_GENTITIES );

	for ( int i = 0; i < num; i++ ) {
		const idClipModel *touch = touching[i];
		idEntity *ent = touch ? touch->GetEntity() : NULL;
		if ( !ent || !touch->GetCollisionModel() ||
			 ( touch->GetContents() & ( CONTENTS_BODY | CONTENTS_CORPSE ) ) ||
			 ent->IsType( idActor::GetClassType() ) || NavDoorIsWalkActivated( ent ) ) {
			continue;
		}

		trace_t hit;
		collisionModelManager->Translation( &hit, start, end, &model, mat3_identity, NAV_CLIP_MASK,
											touch->GetCollisionModel(), touch->GetOrigin(), touch->GetAxis() );
		if ( hit.fraction < trace.fraction ) {
			trace = hit;
			trace.c.entityNum = ent->entityNumber;
			if ( trace.fraction <= 0.0f ) {
				break;
			}
		}
	}

	return trace.fraction >= 1.0f;
}

static bool NavAgentTranslation( trace_t &trace, const idVec3 &start, const idVec3 &end ) {
	return NavTraceTranslation( trace, start, end, navAgentTrace, navAgentBounds );
}

// Live recovery also needs the players that can deflect this flight. Keep
// bodies out of route generation; only a caller supplying its own player asks
// for this current-position traffic prediction. Replanning refreshes the poses.
static bool NavRecoveryTranslation( trace_t &trace, const idVec3 &start, const idVec3 &end, const idPlayer *self ) {
	NavAgentTranslation( trace, start, end );
	if ( !self || trace.fraction <= 0.0f ) {
		return trace.fraction >= 1.0f;
	}
	idBounds swept;
	swept.FromBoundsTranslation( navAgentBounds, start, mat3_identity, trace.endpos - start );
	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *entity = gameLocal.entities[i];
		if ( !entity || entity == self || !entity->IsType( idPlayer::GetClassType() ) ||
			 entity->GetInstance() != self->GetInstance() ) {
			continue;
		}
		const idPlayer *other = static_cast<const idPlayer *>( entity );
		const idClipModel *clip = other->GetPhysics()->GetClipModel();
		if ( other->spectating || other->health <= 0 || !clip || !clip->GetCollisionModel() ||
			 !( clip->GetContents() & MASK_PLAYERSOLID ) || !swept.IntersectsBounds( clip->GetAbsBounds() ) ) {
			continue;
		}
		trace_t hit;
		collisionModelManager->Translation( &hit, start, end, &navAgentTrace, mat3_identity, MASK_PLAYERSOLID,
			clip->GetCollisionModel(), clip->GetOrigin(), clip->GetAxis() );
		if ( hit.fraction < trace.fraction ) {
			trace = hit;
			trace.c.entityNum = other->entityNumber;
		}
	}
	return trace.fraction >= 1.0f;
}

// CrashLand requires actual upward-facing contacts at the end of the physics
// step. A swept collision can stop descent outside that contact envelope.
static bool NavAgentHasGroundContacts( const idVec3 &origin, const idVec3 &gravity ) {
	idVec6 direction;
	direction.SubVec3( 0 ) = gravity;
	direction.SubVec3( 0 ).Normalize();
	direction.SubVec3( 1 ).Zero();
	contactInfo_t contacts[10];
	const idClip *clipWorld = gameLocal.GetEntityClipWorld( NULL );
	int count = collisionModelManager->Contacts( contacts, 10, origin, direction, CONTACT_EPSILON,
		&navAgentTrace, mat3_identity, NAV_CLIP_MASK, clipWorld->GetWorldCollisionModel(), vec3_origin, mat3_identity );
	for ( int i = 0; i < count; i++ ) {
		if ( contacts[i].normal * -direction.SubVec3( 0 ) > 0.0f ) {
			return true;
		}
	}
	idBounds bounds = navAgentBounds;
	bounds.TranslateSelf( origin );
	bounds.ExpandSelf( CONTACT_EPSILON );
	static idClipModel *touching[MAX_GENTITIES];
	const int num = clipWorld->ClipModelsTouchingBounds( bounds, NAV_CLIP_MASK, touching, MAX_GENTITIES );
	for ( int i = 0; i < num; i++ ) {
		const idClipModel *touch = touching[i];
		idEntity *ent = touch ? touch->GetEntity() : NULL;
		if ( !ent || !touch->GetCollisionModel() ||
			( touch->GetContents() & ( CONTENTS_BODY | CONTENTS_CORPSE ) ) ||
			ent->IsType( idActor::GetClassType() ) || NavDoorIsWalkActivated( ent ) ) {
			continue;
		}
		count = collisionModelManager->Contacts( contacts, 10, origin, direction, CONTACT_EPSILON,
			&navAgentTrace, mat3_identity, NAV_CLIP_MASK, touch->GetCollisionModel(), touch->GetOrigin(), touch->GetAxis() );
		for ( int j = 0; j < count; j++ ) {
			if ( contacts[j].normal * -direction.SubVec3( 0 ) > 0.0f ) {
				return true;
			}
		}
	}
	return false;
}

// Keep ordinary route nodes away from a hull-only sliver of support. A route
// along q4dz3's outer rim was supported only by the last two units of the
// player's footprint; normal corner steering put the feet into the void.
static bool NavFloorSupported( const idVec3 &floor ) {
	trace_t trace;
	const float step = pm_stepsize.GetFloat();
	// Offset landing probes can be uphill on a valid slope. Start above that
	// rise rather than mistaking a probe that begins inside it for no floor.
	const float lift = Max( step, navSupportBounds[1].x * 2.0f ) + NAV_GROUND_OFFSET;
	const idVec3 start = floor + idVec3( 0.0f, 0.0f, lift );
	// Include a tread exactly one step lower inside the sweep, not at its
	// no-contact endpoint (also reached by side probes on a 45-degree slope).
	const idVec3 end = floor - idVec3( 0.0f, 0.0f, step + NAV_GROUND_OFFSET );
	return !NavTraceTranslation( trace, start, end, navSupportTrace, navSupportBounds ) &&
		trace.fraction > 0.0f && trace.c.normal.z >= NAV_WALKABLE_NORMAL_Z &&
		idMath::Fabs( trace.endpos.z - floor.z ) <= step + NAV_GROUND_OFFSET;
}

/*
================
NavLandingSupported

A jump or drop has to come down where the floor is, not where the hull merely
clips an edge. Probe the centre and each side of it: one small box alone still
accepted an eight-unit overhang beside q4ctf3's decorative rim. Overlapping
probes tolerate small brush seams without accepting floor on only one side.
================
*/
static bool NavLandingSupported( const idVec3 &floor ) {
	const float support = navSupportBounds[1].x;
	const idVec3 offsets[5] = {
		idVec3( 0.0f, 0.0f, 0.0f ),
		idVec3( support, 0.0f, 0.0f ),
		idVec3( -support, 0.0f, 0.0f ),
		idVec3( 0.0f, support, 0.0f ),
		idVec3( 0.0f, -support, 0.0f )
	};
	for ( int i = 0; i < 5; i++ ) {
		if ( !NavFloorSupported( floor + offsets[i] ) ) {
			return false;
		}
	}
	return true;
}

/*
================
NavDropBeside

Is the ground this far beside a floor point missing: nothing within a step of
the floor's height under a support box there, or only something too steep to
stand on.  A probe that begins inside solid - the box reaches past the hull's
rounded footprint, so beside a wall it can - is a wall, not a drop.
================
*/
static bool NavDropBeside( const idVec3 &floor, const idVec3 &offset ) {
	trace_t trace;
	const float step = pm_stepsize.GetFloat();
	const float lift = Max( step, navSupportBounds[1].x * 2.0f ) + NAV_GROUND_OFFSET;
	const idVec3 start = floor + offset + idVec3( 0.0f, 0.0f, lift );
	// Reach the ground offset below the tolerance, so a tread exactly a step
	// down is met inside the sweep rather than at its very end.
	const idVec3 end = floor + offset - idVec3( 0.0f, 0.0f, step + NAV_GROUND_OFFSET );
	if ( NavTraceTranslation( trace, start, end, navSupportTrace, navSupportBounds ) ) {
		return true;
	}
	if ( trace.fraction <= 0.0f ) {
		return false;
	}
	return trace.c.normal.z < NAV_WALKABLE_NORMAL_Z ||
		idMath::Fabs( trace.endpos.z - floor.z ) > step + NAV_GROUND_OFFSET;
}

/*
================
NavRimNode

A standing hull rests on any overlap, and the support box accepts a floor
under any part of it, so flood fill lays nodes whose hull hangs most of the
way over an edge: q4ctf1's 64-unit platforms and q4dz1's catwalk each end
four units short of a column of cell centres, and the nodes there stand with
twelve of the hull's thirty-two units on the floor.  A player can stand on
such a node, but a route leg along a row of them runs the whole way with the
feet half over the drop, and any lateral error walks the bot off.  A node
whose hull hangs more than NAV_RIM_OVERHANG past an edge on any side is a rim
node: routing pays to pass through it, so the chain a cell inland is taken
wherever one exists, and it remains for the places that have nothing else.
================
*/
static bool NavRimNode( const idVec3 &floor ) {
	const float reach = navSupportBounds[1].x + NAV_RIM_OVERHANG;
	const idVec3 offsets[4] = {
		idVec3( reach, 0.0f, 0.0f ),
		idVec3( -reach, 0.0f, 0.0f ),
		idVec3( 0.0f, reach, 0.0f ),
		idVec3( 0.0f, -reach, 0.0f )
	};
	for ( int i = 0; i < 4; i++ ) {
		if ( NavDropBeside( floor, offsets[i] ) ) {
			return true;
		}
	}
	return false;
}

/*
================
NavIsDynamicSolid

Solids that can be somewhere else at run time than at build time.  Stock maps
are mostly func_static brush entities, which never move and which generation
already proved; only these can make a proven link stale.
================
*/
static bool NavIsDynamicSolid( const idEntity *ent ) {
	return ent && ( ent->IsType( idMover::GetClassType() ) ||
					ent->IsType( idMover_Binary::GetClassType() ) ||
					ent->IsType( idMover_Periodic::GetClassType() ) ||
					ent->IsType( idMoveable::GetClassType() ) );
}

/*
================
rvNavMesh::GetAgentBounds

The player's own standing bounding box, origin at the feet.
================
*/
const idBounds &rvNavMesh::GetAgentBounds( void ) {
	const float halfWidth = pm_bboxwidth.GetFloat() * 0.5f;

	navAgentBounds[0].Set( -halfWidth, -halfWidth, 0.0f );
	navAgentBounds[1].Set( halfWidth, halfWidth, pm_normalheight.GetFloat() );

	return navAgentBounds;
}

/*
===============================================================================

	rvNavPath

===============================================================================
*/

/*
================
rvNavPath::Append
================
*/
void rvNavPath::Append( const idVec3 &origin, int travelType, int actionEntityNum ) {
	navCorner_t corner;

	corner.origin			= origin;
	corner.travelType		= (short)travelType;
	corner.actionEntityNum	= (short)actionEntityNum;

	corners.Append( corner );
}

/*
================
rvNavPath::Length
================
*/
float rvNavPath::Length( void ) const {
	float length = 0.0f;

	for ( int i = 1; i < corners.Num(); i++ ) {
		length += ( corners[i].origin - corners[i - 1].origin ).Length();
	}

	return length;
}

/*
================
rvNavPath::LengthFrom

Length() describes only the geometry between stored corners.  A route created
by FindPath also retains its graph cost, including the exact first/last legs,
jump/drop penalties, and cheap authored transports.  The geometric fallback
keeps manually assembled paths useful from another origin.
================
*/
float rvNavPath::LengthFrom( const idVec3 &origin ) const {
	if ( corners.Num() == 0 ) {
		return 0.0f;
	}
	if ( hasTravelCost && ( origin - travelStart ).LengthSqr() <= 1.0f ) {
		return travelCost;
	}

	return ( corners[0].origin - origin ).Length() + Length();
}

/*
================
rvNavPath::SetTravelCost

String pulling removes intermediate nodes, but their jump/drop penalties and
cheap transport edges still matter when comparing goals.  Keep the exact graph
cost alongside the steering corners without changing the movement interface.
================
*/
void rvNavPath::SetTravelCost( const idVec3 &origin, float cost ) {
	travelStart = origin;
	travelCost = Max( 0.0f, cost );
	hasTravelCost = true;
}

/*
===============================================================================

	rvNavMesh construction

===============================================================================
*/

/*
================
rvNavMesh::rvNavMesh
================
*/
// The cell hash is sized for the mesh rather than left on idHashIndex's default
// 1024 buckets.  idHashIndex never rehashes - Add only ever grows the index
// chain - so a default-constructed table stays 64x oversubscribed at
// NAV_MAX_NODES, and every FindNode during generation walks a chain that long.
// The buckets are not allocated until the first Add, so a small map pays
// nothing for this.
rvNavMesh::rvNavMesh( void ) : nodeHash( NAV_HASH_BUCKETS, NAV_HASH_BUCKETS ) {
	gridOrigin.Zero();
	cellSize		= 24.0f;
	numAreas		= 0;
	lethalLiquidSeen = false;
	transportNodes	= 0;
	numComponents	= 0;
	componentWords	= 0;
	numLandmarks	= 0;
	buildMsec		= 0;
	gapJumpLinks	= 0;
	trenchRimLinks	= 0;
	heuristicScale = 1.0f;
	searchSerial	= 0;
	fieldSerial		= 0;
	rejectSerial	= 0;
	linkRejectTag.Clear();
	fieldPriced		= false;
	nextSnapReportTime = 0;
	memset( &stats, 0, sizeof( stats ) );

	nodes.SetGranularity( 1024 );
	links.SetGranularity( 4096 );
}

/*
================
rvNavMesh::~rvNavMesh
================
*/
rvNavMesh::~rvNavMesh( void ) {
	Clear();
}

/*
================
rvNavMesh::Clear
================
*/
void rvNavMesh::Clear( void ) {
	nodes.Clear();
	links.Clear();
	nodeHash.Clear();
	hazards.Clear();
	transports.Clear();

	componentReach.Clear();
	landmarkNodes.Clear();
	landmarkCost.Clear();
	reverseOffset.Clear();
	reverseSource.Clear();
	reverseCost.Clear();
	entitySnaps.Clear();
	dynamicSolids.Clear();
	dynamicSolidSpawnIds.Clear();
	fieldTargetTag.Clear();
	fieldPricedTag.Clear();
	fieldPricedCost.Clear();

	gScore.Clear();
	fScore.Clear();
	cameFrom.Clear();
	openTag.Clear();
	closedTag.Clear();
	heap.Clear();

	numAreas		= 0;
	lethalLiquidSeen = false;
	transportNodes	= 0;
	numComponents	= 0;
	componentWords	= 0;
	numLandmarks	= 0;
	buildMsec		= 0;
	gapJumpLinks	= 0;
	trenchRimLinks	= 0;
	heuristicScale = 1.0f;
	searchSerial	= 0;
	fieldSerial		= 0;
	rejectSerial	= 0;
	linkRejectTag.Clear();
	fieldPriced		= false;
	nextSnapReportTime = 0;

	if ( navAgentModel ) {
		delete navAgentModel;
		navAgentModel = NULL;
	}
}

/*
================
rvNavMesh::CellCoords
================
*/
void rvNavMesh::CellCoords( const idVec3 &origin, int &gx, int &gy ) const {
	gx = (int)idMath::Floor( ( origin.x - gridOrigin.x ) / cellSize );
	gy = (int)idMath::Floor( ( origin.y - gridOrigin.y ) / cellSize );
}

/*
================
rvNavMesh::CellCentre
================
*/
idVec3 rvNavMesh::CellCentre( int gx, int gy, float z ) const {
	return idVec3( gridOrigin.x + ( gx + 0.5f ) * cellSize,
				   gridOrigin.y + ( gy + 0.5f ) * cellSize,
				   z );
}

/*
================
rvNavMesh::FindNode

Nodes are hashed by grid cell.  A cell can hold several floors - a catwalk over
a corridor - so the z has to be matched as well.
================
*/
int rvNavMesh::FindNode( int gx, int gy, float z ) const {
	const int key = CellKey( gx, gy );

	for ( int i = nodeHash.First( key ); i != -1; i = nodeHash.Next( i ) ) {
		if ( nodes[i].gx != gx || nodes[i].gy != gy ) {
			continue;
		}
		if ( idMath::Fabs( nodes[i].origin.z - z ) <= NAV_MERGE_HEIGHT ) {
			return i;
		}
	}

	return -1;
}

/*
================
rvNavMesh::AddNode
================
*/
int rvNavMesh::AddNode( int gx, int gy, const idVec3 &origin ) {
	navNode_t node;

	node.origin		= origin;
	node.gx			= gx;
	node.gy			= gy;
	node.firstLink	= -1;
	node.area		= -1;
	node.component	= -1;
	node.flags		= ( HazardAtFloor( origin ) >= 0 ) ? NAVNODE_HAZARD : 0;
	if ( NavRimNode( origin ) ) {
		node.flags |= NAVNODE_RIM;
	}

	const int index = nodes.Append( node );
	nodeHash.Add( CellKey( gx, gy ), index );

	return index;
}

/*
================
rvNavMesh::LinkNodes
================
*/
void rvNavMesh::LinkNodes( int from, int to, float cost, int travelType, int actionEntityNum ) {
	navLink_t link;
	const float geometricCost = ( nodes[to].origin - nodes[from].origin ).Length();
	const float edgeScale = ( geometricCost > idMath::FLOAT_EPSILON )
		? Max( 0.0f, cost / geometricCost )
		: 1.0f;

	// Do not duplicate a link that already exists with a cheaper cost.
	for ( int i = nodes[from].firstLink; i != -1; i = links[i].next ) {
		if ( links[i].node == to ) {
			if ( cost < links[i].cost ) {
				links[i].cost				= cost;
				links[i].travelType			= (short)travelType;
				links[i].actionEntityNum	= (short)actionEntityNum;
				heuristicScale = Min( heuristicScale, edgeScale );
			}
			return;
		}
	}

	link.node				= to;
	link.next				= nodes[from].firstLink;
	link.cost				= cost;
	link.travelType			= (short)travelType;
	link.actionEntityNum	= (short)actionEntityNum;

	nodes[from].firstLink = links.Append( link );
	heuristicScale = Min( heuristicScale, edgeScale );
}

/*
================
rvNavMesh::SampleFloor

Drops the agent box straight down and reports where it comes to rest.  Fails on
a pit deeper than maxDrop and on anything too steep to stand on.
================
*/
bool rvNavMesh::SampleFloor( const idVec3 &start, float maxDrop, idVec3 &floorOut ) const {
	trace_t	trace;
	idVec3	end;

	end = start;
	end.z -= maxDrop;

	NavAgentTranslation( trace, start, end );

	if ( trace.fraction >= 1.0f ) {
		return false;
	}

	// A sweep that begins inside solid also reports fraction 0, with endpos at
	// the start and whatever normal the first plane happened to have.  Callers
	// always probe from at least NAV_GROUND_OFFSET above a surface, so a zero
	// fraction here means the probe point itself was solid, not that it landed.
	if ( trace.fraction <= 0.0f ) {
		return false;
	}

	if ( trace.c.normal.z < NAV_WALKABLE_NORMAL_Z ) {
		return false;
	}

	floorOut = trace.endpos;

	return NavFloorSupported( floorOut );
}

/*
================
rvNavMesh::TryStep

Reproduces one step of player movement into the neighbouring cell: try to walk
there flat, and if that is blocked try again from step height, exactly as
idPhysics_Player does when it climbs a stair.  Whatever the horizontal move
reaches is then dropped onto the floor.
================
*/
bool rvNavMesh::TryStep( const idVec3 &fromFloor, const idVec3 &toCentre, idVec3 &toFloor, int &travelType ) const {
	const float	stepSize = pm_stepsize.GetFloat();
	const float	jumpSize = pm_jumpheight.GetFloat();

	// Try the cheapest traversal that works: walk flat, step up, then jump up.
	// The lift heights are the same ones idPhysics_Player will allow.
	const float lifts[3] = { NAV_GROUND_OFFSET, NAV_GROUND_OFFSET + stepSize, NAV_GROUND_OFFSET + jumpSize };

	for ( int attempt = 0; attempt < 3; attempt++ ) {
		trace_t	trace;
		idVec3	start = fromFloor;
		idVec3	end;

		start.z += NAV_GROUND_OFFSET;

		// Clear headroom for the lift before moving sideways at that height.
		if ( attempt > 0 ) {
			end = start;
			end.z = fromFloor.z + lifts[attempt];

			// A partial lift is still a lift: the player's own step-up moves
			// sideways from wherever the ceiling stopped it, so a threshold
			// under a low lintel is a step, not a wall.  Only no lift at all
			// says nothing above this attempt can work either.
			if ( !NavAgentTranslation( trace, start, end ) && trace.fraction <= 0.0f ) {
				return false;
			}
			start = trace.endpos;
		}

		end = toCentre;
		end.z = start.z;

		if ( !NavAgentTranslation( trace, start, end ) ) {
			continue;
		}

		// Land on whatever is under the cell centre.
		if ( !SampleFloor( trace.endpos, lifts[attempt] + NAV_MAX_DROP, toFloor ) ) {
			continue;
		}

		const float rise = toFloor.z - fromFloor.z;
		if ( rise > jumpSize ) {
			continue;
		}

		// A falling bot needs support near its centre, just like a jump.
		// A hull grazing the outer edge is not a reliable landing target.
		if ( rise < -stepSize && !NavLandingSupported( toFloor ) ) {
			continue;
		}

		if ( attempt == 2 ) {
			// Only the jump-height lift got through, so the bot has to jump
			// whatever the destination floor turns out to be.  A railing it has
			// to hop over lands it back at the same height, and calling that a
			// walk would leave the bot pressed against the railing forever.
			if ( !NavLandingSupported( toFloor ) ) {
				continue;		// a sliver of an edge: no jump comes down on it
			}
			// A vertical lift proves headroom, not touchdown. Both a lower
			// ledge and a raised decorative lip can be crossed while rising,
			// then left behind over a pit. Prove the complete running hop.
			const float gravity = Max( 1.0f, gameLocal.GetGravity().Length() );
			if ( !GapJumpClear( fromFloor, toFloor,
				Max( 1.0f, pm_speed.GetFloat() ) * NAV_GAP_SPEED_FRACTION,
				idMath::Sqrt( 2.0f * gravity * jumpSize ), gravity, true ) ) {
				continue;
			}
			travelType = NAVTRAVEL_JUMP;
		} else if ( rise > stepSize ) {
			// Reachable only by jumping, but that is not what was tried here.
			continue;
		} else {
			travelType = ( fromFloor.z - toFloor.z > stepSize ) ? NAVTRAVEL_DROP : NAVTRAVEL_WALK;
			if ( travelType == NAVTRAVEL_DROP && !RunningDropClear( fromFloor, toFloor ) ) {
				continue;
			}
		}

		return true;
	}

	return false;
}

/*
================
rvNavMesh::RunningDropClear

A vertical floor probe does not prove a running drop: momentum can carry the
player over its platform before air control brakes it. Follow the actual
walk-off and falling controller, then require support and room to stop.
================
*/
bool rvNavMesh::RunningDropClear( const idVec3 &from, const idVec3 &to,
		const idVec3 *initialVelocity, const idVec3 *groundWish, float maxImpactDelta ) const {
	idVec3 direction = groundWish ? *groundWish : to - from;
	direction.z = 0.0f;
	const float inputLength = direction.Normalize();
	if ( inputLength < ( groundWish ? 0.001f : 1.0f ) ) {
		return false;
	}
	const float gravity = Max( 1.0f, gameLocal.GetGravity().Length() );
	const float speed = Max( 1.0f, pm_speed.GetFloat() );
	const float groundSpeed = speed * ( groundWish ? Min( 1.0f, inputLength ) : 1.0f );
	const float dt = NAV_PAD_STEP_SECONDS;
	const idVec3 lift( 0.0f, 0.0f, NAV_GROUND_OFFSET );
	const float lateral = Min( 64.0f, speed * 0.2f );
	const idVec3 side( -direction.y, direction.x, 0.0f );
	auto reject = [&]( const char *reason, const idVec3 &at ) {
		if ( initialVelocity && bot_debug.GetInteger() >= 4 ) {
			gameLocal.Printf( "NAV_DROP_REJECT time=%d origin=%.3f %.3f %.3f at=%.3f %.3f %.3f reason=%s\n",
				gameLocal.time, from.x, from.y, from.z, at.x, at.y, at.z, reason );
		}
		return false;
	};
	for ( int approach = 0; approach < ( initialVelocity ? 1 : 4 ); approach++ ) {
		idVec3 position = from + lift;
		idVec3 velocity = direction * speed * ( approach ? 1.0f : 0.25f );
		if ( approach >= 2 ) {
			velocity = direction * idMath::Sqrt( speed * speed - lateral * lateral ) +
				side * ( approach == 2 ? lateral : -lateral );
		}
		if ( initialVelocity ) {
			velocity.Set( initialVelocity->x, initialVelocity->y, 0.0f );
		}
		bool falling = false, landed = false;
		for ( int frame = 0; frame < 160; frame++ ) {
			trace_t trace;
			if ( !falling ) {
				// While supported the controller keeps walking off the lip.
				// Apply MP ground friction and acceleration, including a slow
				// approach that gains speed before its hull leaves the ledge.
				const float currentSpeed = velocity.Length();
				if ( currentSpeed > 0.0f ) {
					velocity *= Max( 0.0f, currentSpeed - Max( currentSpeed, 100.0f ) * 6.0f * dt ) / currentSpeed;
				}
				const float add = groundSpeed - velocity * direction;
				if ( add > 0.0f ) {
					velocity += direction * Min( add, 15.0f * dt * groundSpeed );
				}
				const idVec3 next = position + velocity * dt;
				if ( initialVelocity && TransportOnMove( position, next, -1 ) >= 0 ) {
					return reject( "ground transport", next );
				}
				if ( !NavAgentTranslation( trace, position, next ) ) {
					// WalkMove can step over a low lip before walking off. Keep
					// its momentum on a full up/across/down success; partial
					// steps need more collision planes and remain unproven.
					const idVec3 stepLift( 0.0f, 0.0f, pm_stepsize.GetFloat() );
					trace_t up, across, down;
					NavAgentTranslation( up, trace.endpos, trace.endpos + stepLift );
					NavAgentTranslation( across, up.endpos, up.endpos + next - trace.endpos );
					NavAgentTranslation( down, across.endpos, across.endpos - stepLift );
					if ( across.fraction < 1.0f ||
						( down.fraction < 1.0f && down.c.normal.z < NAV_WALKABLE_NORMAL_Z ) ) {
						return reject( "step", across.endpos );
					}
					position = down.endpos + ( down.fraction < 1.0f ? lift : idVec3( 0.0f, 0.0f, 0.0f ) );
				} else {
					position = next;
				}
				NavAgentTranslation( trace, position, position -
					idVec3( 0.0f, 0.0f, pm_stepsize.GetFloat() + NAV_GROUND_OFFSET ) );
				if ( trace.fraction < 1.0f && trace.c.normal.z >= NAV_WALKABLE_NORMAL_Z &&
					trace.endpos.z >= from.z - pm_stepsize.GetFloat() ) {
					position = trace.endpos + lift;
					continue;
				}
				falling = true;
				continue;	// the walk-off already consumed this frame's displacement
			}
			idVec3 wish;
			if ( NavAirSteer( position, velocity, to, gravity, wish ) ) {
				const float add = speed - velocity * wish;
				if ( add > 0.0f ) {
					velocity += wish * Min( add, NAV_PAD_AIR_ACCELERATE * dt * speed );
				}
			}
			const idVec3 next = position + velocity * dt - idVec3( 0.0f, 0.0f, 0.5f * gravity * dt * dt );
			if ( initialVelocity && TransportOnMove( position, next, -1 ) >= 0 ) {
				return reject( "air transport", next );
			}
			if ( !NavAgentTranslation( trace, position, next ) ) {
				if ( trace.c.normal.z < NAV_WALKABLE_NORMAL_Z ||
					idMath::Fabs( trace.endpos.z - to.z ) > pm_stepsize.GetFloat() ||
					!NavLandingSupported( trace.endpos ) || IsLethalFloor( trace.endpos ) ||
					!WalkableLine( trace.endpos, to ) ||
					( initialVelocity && NavLandingImpactDelta( trace, velocity - idVec3( 0.0f, 0.0f, gravity * dt * trace.fraction ),
						idVec3( 0.0f, 0.0f, -gravity ) ) > maxImpactDelta ) ) {
					return reject( "landing", trace.endpos );
				}
				// Ignore friction and use less than MP's 15x acceleration: this
				// stop reserve is conservative, including one input-frame delay.
				idVec3 flat( velocity.x, velocity.y, 0.0f );
				const idVec3 stop = trace.endpos + flat * ( dt + flat.Length() / ( 20.0f * speed ) );
				if ( !LandingContinuationClear( trace.endpos, stop ) ||
					 ( initialVelocity && TransportOnMove( trace.endpos, stop, -1 ) >= 0 ) ) {
					return reject( "stopping path", stop );
				}
				landed = true;
				break;
			}
			position = next;
			velocity.z -= gravity * dt;
		}
		if ( !landed ) {
			return reject( "no landing", position );
		}
	}
	return true;
}

/*
================
rvNavMesh::AirInputLandingSafe

The knocked-fall counterpart of RunningDropClear: idPhysics_Player's air
acceleration toward one fixed wish, gravity, and the hull swept frame by
frame, sliding along walls, until it comes down on walkable ground. A known
pad catch hands movement to its normal coast, as the live controller does. The
landing must be supported and not lethal, cost no more than the impact the
bot can afford, and leave room to stop at MP ground acceleration. Each fall
and caught-pad continuation has a bounded flight budget.
================
*/
bool rvNavMesh::AirInputLandingSafe( const idVec3 &origin, const idVec3 &velocityIn, const idVec3 &wishIn,
									 float maxImpactDelta, idVec3 *touchdown, bool *landedSupported, bool *touchedAffordable,
									 bool *brakedAffordable, int knockMsec, const idPlayer *self ) const {
	if ( brakedAffordable ) {
		*brakedAffordable = false;
	}
	if ( landedSupported ) {
		*landedSupported = false;
	}
	if ( touchedAffordable ) {
		*touchedAffordable = false;
	}
	const idVec3 gravity = gameLocal.GetGravity();
	if ( gravity.Length() <= 1.0f ) {
		return true;
	}
	idVec3 wish( wishIn.x, wishIn.y, 0.0f );
	bool steering = wish.Normalize() > 0.0f;
	const float speed = Max( 1.0f, pm_speed.GetFloat() );
	// Match the 16 ms player integration step. Doubling it changes the air
	// acceleration before each sweep and misses narrow recoverable landings.
	const float dt = NAV_PAD_STEP_SECONDS;
	// This is a live fall, not a generation probe. Lifting it invents clearance
	// over a ledge whose side the real player hull can still hit.
	idVec3 position = origin;
	idVec3 velocity = velocityIn;
	int unstuck = 0;
	int caughtPad = -1;
	int maxFrames = 200;
	auto reject = [&]( const char *reason ) {
		if ( bot_debug.GetInteger() >= 4 ) {
			gameLocal.Printf( "BOT_AIR_PROOF time=%d reason=%s input=%.3f %.3f start=%.3f %.3f %.3f at=%.3f %.3f %.3f velocity=%.3f %.3f %.3f\n",
				gameLocal.time, reason, wish.x, wish.y, origin.x, origin.y, origin.z,
				position.x, position.y, position.z, velocity.x, velocity.y, velocity.z );
		}
		return false;
	};
	auto recoveryTrace = [&]( trace_t &trace, const idVec3 &from, const idVec3 &to ) {
		return NavRecoveryTranslation( trace, from, to, self );
	};
	for ( int frame = 0; frame < maxFrames; frame++ ) {
		const int transport = TransportAtFloor( position );
		if ( transport >= 0 ) {
			// Recovery may steer into a real, known pad catch. The live bot
			// hands movement to the pad's normal coast when it launches, so
			// prove that whole continuation from this exact contact, including
			// repeated force-field pushes. Teleporters and a second different
			// pad remain outside this bounded proof.
			idVec3 registeredLanding;
			idEntity *pad = transport < MAX_GENTITIES ? gameLocal.entities[transport] : NULL;
			if ( frame == 0 || ( caughtPad >= 0 && transport != caughtPad ) ||
				 !pad || !pad->IsType( rvJumpPad::GetClassType() ) || !pad->IsActive( TH_THINK ) ||
				 !NaturalPadLanding( transport, registeredLanding ) || !NavPadVolumeTouches( pad, position ) ) {
				return reject( "unproven transport" );
			}
			const idVec3 padGravity = gameLocal.GetCurrentGravity( pad );
			if ( ( padGravity - gravity ).LengthSqr() > 0.01f || !NavPadLaunchVelocity( pad, padGravity, velocity ) ) {
				return reject( "pad launch" );
			}
			if ( caughtPad < 0 ) {
				caughtPad = transport;
				maxFrames = frame + NAV_PAD_MAX_STEPS;
			}
			steering = false;
		}
		if ( steering ) {
			const float add = speed - velocity * wish;
			if ( add > 0.0f ) {
				velocity += wish * Min( add, NAV_PAD_AIR_ACCELERATE * dt * speed );
			}
		}
		const idVec3 next = position + velocity * dt + gravity * ( 0.5f * dt * dt );
		trace_t trace;
		if ( recoveryTrace( trace, position, next ) ) {
			position = next;
			velocity += gravity * dt;
			continue;
		}
		if ( bot_debug.GetInteger() >= 5 ) {
			gameLocal.Printf( "BOT_AIR_CONTACT time=%d input=%.3f %.3f frame=%d fraction=%.3f at=%.3f %.3f %.3f normal=%.3f %.3f %.3f\n",
				gameLocal.time, wish.x, wish.y, frame, trace.fraction, trace.endpos.x, trace.endpos.y, trace.endpos.z,
				trace.c.normal.x, trace.c.normal.y, trace.c.normal.z );
		}
		if ( idMath::Fabs( trace.c.normal.z ) < 0.01f ) {
			// MP AirMove may step over a lip while close to a floor, including
			// while descending. Keep the remaining displacement and momentum
			// on a full up/across/down success, as player SlideMove does.
			const idVec3 lift( 0.0f, 0.0f, pm_stepsize.GetFloat() );
			trace_t ground, up, across, down;
			if ( !recoveryTrace( ground, trace.endpos, trace.endpos - lift ) &&
				 ground.c.normal.z > NAV_WALKABLE_NORMAL_Z ) {
				recoveryTrace( up, trace.endpos, trace.endpos + lift );
				recoveryTrace( across, up.endpos, up.endpos + next - trace.endpos );
				recoveryTrace( down, across.endpos, across.endpos - lift );
				if ( across.fraction >= 1.0f &&
					( down.fraction >= 1.0f || down.c.normal.z > NAV_WALKABLE_NORMAL_Z ) ) {
					position = down.endpos;
					velocity += gravity * dt;
					continue;
				}
				if ( across.fraction > trace.fraction ) {
					return reject( "partial step" );	// needs additional collision planes
				}
			}
		}
		if ( trace.c.normal.z >= NAV_WALKABLE_NORMAL_Z && velocity * gravity > 0.0f ) {
			if ( touchdown ) {
				*touchdown = trace.endpos;
			}
			if ( IsLethalFloor( trace.endpos ) ) {
				return reject( "lethal floor" );
			}
			// SlideMove clips both its midpoint and end velocity, then
			// CheckGround/CrashLand inspect contacts at the final position.
			// An affordable ramp touch is not automatically a landing: CTF5's
			// walkway bevel redirects the bot upward and back over the void.
			// Continue that single-plane step until actual ground is reached.
			const idVec3 clipped = idPhysics_Player::AdjustVertically( trace.c.normal, velocity + gravity * ( dt * 0.5f ) );
			trace_t remainder;
			const idVec3 end = trace.endpos + clipped * ( dt * ( 1.0f - trace.fraction ) );
			if ( !recoveryTrace( remainder, trace.endpos, end ) ||
				 TransportOnMove( trace.endpos, end, -1 ) >= 0 ) {
				return reject( "landing remainder" );	// another collision or launch
			}
			if ( !NavAgentHasGroundContacts( end, gravity ) ) {
				position = end;
				velocity = idPhysics_Player::AdjustVertically( trace.c.normal, velocity + gravity * dt );
				continue;
			}
			if ( NavLandingImpactDelta( trace, velocity + gravity * ( dt * trace.fraction ), gravity ) > maxImpactDelta ) {
				return reject( "impact budget" );
			}
			idVec3 flat( velocity.x, velocity.y, 0.0f );
			auto recoveryRunout = [&]() {
				if ( brakedAffordable ) {
					*brakedAffordable = RecoveryBrakeClear( trace, flat, dt * ( 1.0f - trace.fraction ),
						Max( 0, knockMsec - ( frame + 1 ) * 16 ) );
				}
				return reject( "landing runout" );
			};
			const idVec3 stop = trace.endpos + flat * ( dt + flat.Length() / ( 20.0f * speed ) );
			// A landing whose runout reaches a pad or a teleporter is another
			// throw: a q4ctf5 bot that survived a rail's kick slid onto
			// func_jumppad_3 and was flown off the map.
			if ( TransportOnMove( trace.endpos, stop, -1 ) >= 0 ) {
				return recoveryRunout();
			}
			if ( touchedAffordable ) {
				*touchedAffordable = true;
			}
			idVec3 supportedOrigin = trace.endpos;
			if ( !NavLandingSupported( supportedOrigin ) &&
				 !RecoveryStepClear( trace.endpos, stop, &supportedOrigin ) ) {
				return recoveryRunout();
			}
			if ( landedSupported ) {
				*landedSupported = true;
			}
			return LandingContinuationClear( supportedOrigin, stop ) || recoveryRunout();
		}
		if ( trace.fraction <= 0.0f ) {
			// Resting against the wall it slid down: step off its plane, as
			// the player's own clip epsilon does, rather than read a wedge.
			if ( ++unstuck > 8 ) {
				return reject( "wedged" );
			}
			position += trace.c.normal * CM_CLIP_EPSILON;
		} else {
			position = trace.endpos;
		}
		velocity -= trace.c.normal * ( velocity * trace.c.normal );
		velocity += gravity * dt;
	}
	return reject( "no landing" );
}

// Ground contact during knockback does not end the trajectory. Prove the
// selected input through its friction-free slide, then ordinary ground control
// until stopping is safe or the same input owns the resulting flight.
// Steps, walls, slick floors and slopes remain unproven by this narrow solver.
bool rvNavMesh::KnockSlideLandingSafe( const idVec3 &origin, const idVec3 &velocityIn,
		int knockMsec, float maxImpactDelta, const idVec3 &wishIn ) const {
	auto reject = [&]( const char *reason, const idVec3 &position, const idVec3 &velocity, const idVec3 &normal ) {
		if ( bot_debug.GetInteger() >= 4 ) {
			gameLocal.Printf( "BOT_SLIDE_PROOF time=%d reason=%s origin=%.3f %.3f %.3f input=%.3f %.3f knock=%d at=%.3f %.3f %.3f velocity=%.3f %.3f %.3f normal=%.3f %.3f %.3f\n",
				gameLocal.time, reason, origin.x, origin.y, origin.z, wishIn.x, wishIn.y, knockMsec,
				position.x, position.y, position.z, velocity.x, velocity.y, velocity.z, normal.x, normal.y, normal.z );
		}
		return false;
	};
	if ( knockMsec < 0 || knockMsec > 1000 || pm_frictionoverride.GetFloat() >= 0.0f ) {
		return false;
	}
	idVec3 wish( wishIn.x, wishIn.y, 0.0f );
	wish.Normalize();
	if ( velocityIn.z > 0.0f ) {
		return AirInputLandingSafe( origin, velocityIn, wish, maxImpactDelta );
	}
	idVec3 position = origin;
	idVec3 velocity = velocityIn;
	const float dt = NAV_PAD_STEP_SECONDS;
	const float speed = Max( 1.0f, pm_speed.GetFloat() );
	const idVec3 groundProbe( 0.0f, 0.0f, 2.0f * CM_CLIP_EPSILON );
	for ( int elapsed = 0; elapsed < 1000; elapsed += 16 ) {
		trace_t ground;
		if ( NavAgentTranslation( ground, position, position - groundProbe ) ) {
			return AirInputLandingSafe( position, velocity, wish, maxImpactDelta ) ||
				reject( "air", position, velocity, vec3_origin );
		}
		// A future splash can arrive while the shooter is already falling.
		// Only actual floor contact cancels descent; flattening it before the
		// probe invents airtime to reach a ledge (and reduces impact damage).
		velocity.z = 0.0f;
		if ( ground.c.normal.z < 0.999f || IsLethalFloor( ground.endpos ) ||
			 ( ground.c.material && ( ground.c.material->GetSurfaceFlags() & SURF_SLICK ) ) ||
			 ( gameLocal.LiquidContentsAtPoint( position, NULL ) & MASK_WATER ) ) {
			return reject( "ground", position, velocity, ground.c.normal );
		}
		// MovePlayer drops timers before WalkMove: the expiration frame
		// already has friction and normal ground acceleration.
		const bool knockActive = elapsed + 16 < knockMsec;
		if ( !knockActive ) {
			const idVec3 stop = position + velocity * ( dt + velocity.Length() / ( 20.0f * speed ) );
			if ( TransportOnMove( position, stop, -1 ) < 0 && LandingContinuationClear( position, stop ) ) {
				return true;
			}
			const float currentSpeed = velocity.Length();
			if ( currentSpeed > 0.0f ) {
				const float drop = Max( currentSpeed, 100.0f ) * 6.0f * dt;
				velocity *= Max( 0.0f, currentSpeed - drop ) / currentSpeed;
			}
		}
		const float add = speed - velocity * wish;
		if ( add > 0.0f ) {
			velocity += wish * Min( add, ( knockActive ? NAV_PAD_AIR_ACCELERATE : 15.0f ) * dt * speed );
		}
		trace_t across;
		const idVec3 next = position + velocity * dt;
		if ( TransportOnMove( position, next, -1 ) >= 0 ) {
			return reject( "transport", position, velocity, vec3_origin );
		}
		if ( !NavAgentTranslation( across, position, next ) ) {
			return reject( "collision", across.endpos, velocity, across.c.normal );
		}
		position = next;
	}
	return false;
}

bool rvNavMesh::LandingContinuationClear( const idVec3 &origin, const idVec3 &next ) const {
	const float step = pm_stepsize.GetFloat();
	const idVec3 points[2] = { origin, next };
	idVec3 floors[2];
	const int count = ( next - origin ).LengthSqr() < 1.0f ? 1 : 2;
	for ( int i = 0; i < count; i++ ) {
		// Start at actual standing height: lifting the entire hull a step
		// first would overlap the ceiling in a valid low corridor.
		if ( !SampleFloor( points[i] + idVec3( 0.0f, 0.0f, NAV_GROUND_OFFSET ),
			step + 2.0f * NAV_GROUND_OFFSET, floors[i] ) ) {
			// An item origin can float above its floor; an edge probe can sit
			// below the player's standing height on a slope. Find the surface
			// with the low support hull, then resample with the full hull.
			trace_t trace;
			const idVec3 start = points[i] + idVec3( 0.0f, 0.0f, step + NAV_GROUND_OFFSET );
			const idVec3 end = points[i] - idVec3( 0.0f, 0.0f, step + NAV_GROUND_OFFSET );
			if ( NavTraceTranslation( trace, start, end, navSupportTrace, navSupportBounds ) ||
				trace.fraction <= 0.0f || trace.c.normal.z < NAV_WALKABLE_NORMAL_Z ) {
				return false;
			}
			// The two hulls contact a sloped plane at different origin heights.
			// Account for their footprints before the full-hull sweep, instead
			// of starting inside the slope. Raising by a whole step would
			// reject a valid corridor with only a little headroom.
			float supportOffset = idMath::INFINITY;
			float agentOffset = idMath::INFINITY;
			for ( int v = 0; v < navSupportTrace.numVerts; v++ ) {
				supportOffset = Min( supportOffset, navSupportTrace.verts[v] * trace.c.normal );
			}
			for ( int v = 0; v < navAgentTrace.numVerts; v++ ) {
				agentOffset = Min( agentOffset, navAgentTrace.verts[v] * trace.c.normal );
			}
			const float rise = Max( 0.0f, ( supportOffset - agentOffset ) / trace.c.normal.z );
			const idVec3 standing = trace.endpos + idVec3( 0.0f, 0.0f, rise + NAV_GROUND_OFFSET );
			if ( !SampleFloor( standing, step + 2.0f * NAV_GROUND_OFFSET, floors[i] ) ) {
				return false;
			}
		}
		if ( idMath::Fabs( floors[i].z - points[i].z ) > step ||
			!NavLandingSupported( floors[i] ) || IsLethalFloor( floors[i] ) ) {
			return false;
		}
	}
	return count == 1 || WalkableLine( floors[0], floors[1] );
}

static bool NavRecoveryFootprintContains( const idVec3 &point ) {
	const float margin = 0.03f;
	if ( point.x < navAgentTrace.bounds[0].x + margin || point.x > navAgentTrace.bounds[1].x - margin ||
		 point.y < navAgentTrace.bounds[0].y + margin || point.y > navAgentTrace.bounds[1].y - margin ) {
		return false;
	}
	for ( int i = 0; i < navAgentTrace.numPolys; i++ ) {
		const traceModelPoly_t &plane = navAgentTrace.polys[i];
		if ( idMath::Fabs( plane.normal.z ) < 0.5f && plane.normal * point > plane.dist - margin ) {
			return false;
		}
	}
	return true;
}

static bool NavRecoverySharedSupport( const idVec3 &from, const idVec3 &to ) {
	static const idBounds pinBounds( idVec3( -0.01f, -0.01f, 0.0f ), idVec3( 0.01f, 0.01f, 0.01f ) );
	static const idTraceModel pin( pinBounds );
	const idVec3 bases[2] = { from, to };
	for ( int side = 0; side < 2; side++ ) {
		for ( int i = 0; i < navAgentTrace.numVerts; i++ ) {
			if ( navAgentTrace.verts[i].z > navAgentTrace.bounds[0].z + 0.01f ) {
				continue;
			}
			// A tiny inset gives the probe real area inside both footprints.
			// Convexity then keeps this known floor patch under the hull at
			// EVERY intermediate position, not just at four-unit samples.
			idVec3 anchor = bases[side] + navAgentTrace.verts[i] * 0.995f;
			if ( !NavRecoveryFootprintContains( anchor - from ) ||
				 !NavRecoveryFootprintContains( anchor - to ) ) {
				continue;
			}
			trace_t trace;
			const idVec3 lift( 0.0f, 0.0f, NAV_GROUND_OFFSET );
			if ( !NavTraceTranslation( trace, anchor + lift, anchor - lift, pin, pinBounds ) &&
				 trace.fraction > 0.0f && trace.c.normal.z >= NAV_WALKABLE_NORMAL_Z &&
				 idMath::Fabs( trace.endpos.z - from.z ) <= NAV_GROUND_OFFSET ) {
				return true;
			}
		}
	}
	return false;
}

// A narrow involuntary landing may have enough real hull contact to stop,
// while failing the wider five-probe support required by voluntary routes.
// Complete the airborne collision frame before applying any ground brake.
// Then require a shared patch of flat floor under every segment, including
// actual end contacts. A momentary edge touch is not a surviving runout.
bool rvNavMesh::RecoveryBrakeClear( const trace_t &impact, const idVec3 &velocityIn,
		float remainingSeconds, int knockMsec ) const {
	if ( impact.c.normal.z < 0.999f || knockMsec < 0 || knockMsec > 1000 ||
		 pm_frictionoverride.GetFloat() >= 0.0f ) {
		return false;
	}
	idVec3 position = impact.endpos;
	idVec3 velocity( velocityIn.x, velocityIn.y, 0.0f );
	auto reject = [&]( const char *reason ) {
		if ( bot_debug.GetInteger() >= 4 ) {
			gameLocal.Printf( "BOT_BRAKE_PROOF time=%d reason=%s origin=%.3f %.3f %.3f at=%.3f %.3f %.3f velocity=%.3f %.3f knock=%d\n",
				gameLocal.time, reason, impact.endpos.x, impact.endpos.y, impact.endpos.z,
				position.x, position.y, position.z, velocity.x, velocity.y, knockMsec );
		}
		return false;
	};
	const float dt = NAV_PAD_STEP_SECONDS;
	const idVec3 down( 0.0f, 0.0f, 2.0f * CM_CLIP_EPSILON );
	// Leave room for command-byte rounding instead of assuming exact reverse.
	const float wishSpeed = Max( 1.0f, pm_speed.GetFloat() );
	for ( int frame = 0; frame < 128; frame++ ) {
		trace_t ground;
		if ( NavAgentTranslation( ground, position, position - down ) || ground.c.normal.z < 0.999f ||
			 ( ground.c.material && ( ground.c.material->GetSurfaceFlags() & SURF_SLICK ) ) ||
			 IsLethalFloor( position ) || ( gameLocal.LiquidContentsAtPoint( position, NULL ) & MASK_WATER ) ) {
			return reject( "floor" );
		}
		float seconds = remainingSeconds;
		if ( frame > 0 ) {
			seconds = dt;
			const float speed = velocity.Length();
			if ( speed < 1.0f ) {
				return NavAgentHasGroundContacts( position, gameLocal.GetGravity() );
			}
			knockMsec = Max( 0, knockMsec - 16 );
			const float friction = knockMsec ? 0.0f : Max( speed, 100.0f ) * 6.0f * dt;
			// Below the runout controller's stop speed, friction finishes the
			// stop. Otherwise allow one command byte of rounding in its brake.
			const float brake = knockMsec ? NAV_PAD_AIR_ACCELERATE * dt * wishSpeed * 0.99f :
				( speed <= 10.0f ? 0.0f : Max( 0.0f, Min( speed - friction, 15.0f * dt * wishSpeed ) - 15.0f * dt * wishSpeed / 127.0f ) );
			velocity *= Max( 0.0f, speed - friction - brake ) / speed;
		}
		idVec3 next = position + velocity * seconds;
		trace_t across;
		if ( !NavAgentTranslation( across, position, next ) ) {
			// A vertical wall can arrest a landing's runout. CTF5's lower
			// walkway catches a coast this way; rejecting the wall made the
			// controller brake in air and fall short of the floor entirely.
			// Keep both supported segments and the clipped frame remainder.
			if ( idMath::Fabs( across.c.normal.z ) >= 0.01f ||
				 TransportOnMove( position, across.endpos, -1 ) >= 0 ||
				 !NavRecoverySharedSupport( position, across.endpos ) ||
				 !NavAgentHasGroundContacts( across.endpos, gameLocal.GetGravity() ) ) {
				return reject( "blocked runout" );
			}
			velocity -= across.c.normal * ( ( velocity * across.c.normal ) * 1.001f ); // player OVERCLIP
			next = across.endpos + velocity * ( seconds * ( 1.0f - across.fraction ) );
			position = across.endpos;
			trace_t remainder;
			if ( !NavAgentTranslation( remainder, position, next ) ) {
				return reject( "runout corner" );
			}
		}
		if ( TransportOnMove( position, next, -1 ) >= 0 ||
			 !NavRecoverySharedSupport( position, next ) ||
			 !NavAgentHasGroundContacts( next, gameLocal.GetGravity() ) ) {
			return reject( "runout" );
		}
		position = next;
	}
	return false;
}

bool rvNavMesh::RecoveryStepClear( const idVec3 &origin, const idVec3 &target, idVec3 *supportedOrigin ) const {
	if ( NavLandingSupported( origin ) ) {
		return false;		// never relax an ordinary supported WALK or gap approach
	}
	idVec3 direction = target - origin;
	direction.z = 0.0f;
	const float distance = Min( 64.0f, direction.Normalize() );
	if ( distance <= 1.0f ) {
		return false;
	}
	const idVec3 lift( 0.0f, 0.0f, NAV_GROUND_OFFSET );
	idVec3 previous = origin;
	// Four-unit samples are well inside the stock hull's width. This is a
	// bounded local escape, not a distant endpoint test across missing floor.
	const int steps = (int)idMath::Ceil( distance / 4.0f );
	for ( int i = 0; i <= steps; i++ ) {
		idVec3 point = origin + direction * ( distance * (float)i / (float)steps );
		point.z = previous.z;
		trace_t trace;
		if ( !NavAgentTranslation( trace, previous + lift, point + lift ) ||
			 LineEntersTransport( previous, point ) ||
			 ( i > 0 && !NavRecoverySharedSupport( previous, point ) ) ) {
			return false;
		}
		// The real standing hull must retain same-height ground contact on
		// every sample, including the marginal start. A wall is not a floor.
		if ( NavAgentTranslation( trace, point + lift, point - lift ) ||
			 trace.fraction <= 0.0f || trace.c.normal.z < NAV_WALKABLE_NORMAL_Z ||
			 idMath::Fabs( trace.endpos.z - origin.z ) > 2.0f * NAV_GROUND_OFFSET ||
			 IsLethalFloor( trace.endpos ) ) {
			return false;
		}
		previous = trace.endpos;
		if ( i > 0 && NavLandingSupported( previous ) ) {
			if ( supportedOrigin ) {
				*supportedOrigin = previous;
			}
			return true;
		}
	}
	return false;
}

/*
================
rvNavMesh::GatherSeeds

Seeds are places the map itself guarantees a player can stand: spawn points,
items, jump pad landings and teleport exits.  Flooding out from those covers
the playable space without probing the solid two thirds of the world bounds.
================
*/
void rvNavMesh::GatherSeeds( idList<idVec3> &seeds ) const {
	idEntity *ent;

	for ( ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		if ( ent->IsType( idPlayerStart::GetClassType() ) || ent->IsType( idItem::GetClassType() ) ) {
			seeds.Append( ent->GetPhysics()->GetOrigin() );
			continue;
		}

		// A jump pad launches into space; its target is where a player lands.
		if ( ent->IsType( rvJumpPad::GetClassType() ) ) {
			idBounds padBounds;
			if ( NavTravelVolumeBounds( ent, padBounds ) ) {
				seeds.Append( padBounds.GetCenter() );
			}
			for ( int i = 0; i < ent->targets.Num(); i++ ) {
				if ( ent->targets[i].GetEntity() ) {
					seeds.Append( ent->targets[i].GetEntity()->GetPhysics()->GetOrigin() );
				}
			}
		}
	}

	// Anyone already in the map is standing somewhere legal.
	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idPlayer *player = gameLocal.GetClientByNum( i );
		if ( player && !player->spectating ) {
			seeds.Append( player->GetPhysics()->GetOrigin() );
		}
	}
}

/*
================
rvNavMesh::GatherHazards

A trigger_hurt is not solid, so the generator's collision probes walk straight
into it: stock maps put 1000-damage and gib volumes at the bottom of their
pits, and without this the graph happily lays floor there and routes, roams and
recovers bots into them.  A volume that kills on one touch refuses ground
outright, like lava. A deadly volume on a translating binary mover reserves
its whole travel envelope even when initially off: q4ctf5 switches its bound
crusher on as the door moves. Other switched-off or unmodelled moving volumes
and survivable pain only make their initial ground expensive.
================
*/
void rvNavMesh::GatherHazards( void ) {
	hazards.Clear();

	for ( idEntity *ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		if ( !ent->IsType( idTrigger_Hurt::GetClassType() ) || !ent->GetPhysics() ) {
			continue;
		}

		bool gibs = false;
		const float damage = NavHurtDamagePerTouch( ent, gibs );
		if ( damage <= 0.0f && !gibs ) {
			continue;
		}

		navHazard_t hazard;
		hazard.bounds = ent->GetPhysics()->GetAbsBounds();
		if ( hazard.bounds.IsCleared() ) {
			continue;
		}
		// idTrigger_Hurt applies its damage once per "delay" seconds of contact.
		const float delay = Max( 0.05f, ent->spawnArgs.GetFloat( "delay", "1.0" ) );
		const bool deadly = gibs || damage >= NAV_LETHAL_HURT_DAMAGE ||
							damage / delay >= NAV_LETHAL_HURT_RATE;
		bool swept = false;
		idEntity *master = ent->GetBindMaster();
		if ( deadly && master && master->IsType( idMover_Binary::GetClassType() ) ) {
			swept = static_cast<idMover_Binary *>( master )->GetTranslationBounds( hazard.bounds, hazard.bounds );
		}
		hazard.lethal = deadly && ( swept || ( ent->spawnArgs.GetBool( "on", "1" ) && !ent->IsBound() ) );
		hazards.Append( hazard );
	}

	// A mover is floor that can leave.  One sitting just above a lethal volume
	// is a trapdoor - q4xdm14's pit panels open under whoever stands on them -
	// and one that crushes kills whoever it closes on.  The graph cannot know
	// when either happens, so the ground on and over them is made expensive:
	// usable when it is the only way, never somewhere to loiter or route
	// through for convenience.
	const int lethalVolumes = hazards.Num();
	const float agentHeight = GetAgentBounds()[1].z;
	for ( idEntity *ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		if ( !ent->IsType( idMover::GetClassType() ) || !ent->GetPhysics() ) {
			continue;
		}

		const idBounds moverBounds = ent->GetPhysics()->GetAbsBounds();
		if ( moverBounds.IsCleared() ) {
			continue;
		}

		bool dangerous = ent->spawnArgs.GetInt( "damage", "0" ) > 0;
		for ( int i = 0; i < lethalVolumes && !dangerous; i++ ) {
			const idBounds &below = hazards[i].bounds;
			if ( !hazards[i].lethal ||
				 below[1].x < moverBounds[0].x || below[0].x > moverBounds[1].x ||
				 below[1].y < moverBounds[0].y || below[0].y > moverBounds[1].y ) {
				continue;
			}
			const float depth = moverBounds[0].z - below[1].z;
			dangerous = depth >= -4.0f && depth <= NAV_TRAPDOOR_DEPTH;
		}
		if ( !dangerous ) {
			continue;
		}

		navHazard_t hazard;
		hazard.bounds = moverBounds;
		hazard.bounds[1].z += agentHeight + 8.0f;	// standing on it counts
		hazard.lethal = false;
		hazards.Append( hazard );
	}
}

/*
================
rvNavMesh::HazardAtFloor

The agent's standing box at this floor point against every hazard volume.  A
lethal volume wins over a survivable one so callers can refuse on it.
================
*/
int rvNavMesh::HazardAtFloor( const idVec3 &floor ) const {
	if ( hazards.Num() == 0 ) {
		return -1;
	}

	idBounds agent = GetAgentBounds();
	agent.TranslateSelf( floor );

	int found = -1;
	for ( int i = 0; i < hazards.Num(); i++ ) {
		if ( !hazards[i].bounds.IntersectsBounds( agent ) ) {
			continue;
		}
		if ( hazards[i].lethal ) {
			return i;
		}
		found = i;
	}

	return found;
}

/*
================
rvNavMesh::IsHazardous
================
*/
bool rvNavMesh::IsHazardous( const idVec3 &floor, bool lethalOnly ) const {
	const int hazard = HazardAtFloor( floor );
	if ( hazard >= 0 && ( hazards[hazard].lethal || !lethalOnly ) ) {
		return true;
	}

	return NavPointInLethalLiquid( floor );
}

/*
================
rvNavMesh::FloodFill
================
*/
void rvNavMesh::FloodFill( const idList<idVec3> &seeds ) {
	idList<int>	openList;
	int			i;

	openList.SetGranularity( 1024 );

	for ( i = 0; i < seeds.Num(); i++ ) {
		idVec3 probe = seeds[i];
		idVec3 floor;

		// Seeds sit a little above the floor, and items hover.  Look up a touch
		// first so a seed embedded in the surface still resolves.
		probe.z += pm_stepsize.GetFloat();

		if ( !SampleFloor( probe, 256.0f, floor ) ) {
			continue;
		}

		int gx, gy;
		CellCoords( floor, gx, gy );

		// Re-drop at the cell centre so the node is where the grid says it is.
		idVec3 centre = CellCentre( gx, gy, floor.z + pm_stepsize.GetFloat() );
		idVec3 snapped;
		if ( !SampleFloor( centre, pm_stepsize.GetFloat() + NAV_MAX_DROP, snapped ) ) {
			continue;
		}

		if ( FindNode( gx, gy, snapped.z ) != -1 ) {
			continue;
		}

// openQ4 BEGIN
		if ( NavPointInLethalLiquid( snapped ) ) {
			lethalLiquidSeen = true;
			continue;
		}
// openQ4 END

		// An item or spawn point inside a kill volume is still not ground.
		const int seedHazard = HazardAtFloor( snapped );
		if ( seedHazard >= 0 && hazards[seedHazard].lethal ) {
			continue;
		}

		openList.Append( AddNode( gx, gy, snapped ) );
	}

	// Breadth first so the mesh grows evenly and the open list stays small.
	//
	// Hitting the node limit stops the graph GROWING; it must not stop the
	// expansion loop.  Everything still queued was discovered by some earlier
	// node and therefore already has an inbound link, so abandoning it leaves it
	// with firstLink == -1 - a sink.  BuildAreas walks the links as undirected
	// and folds a sink into the same area as the rest of the map, so the cheap
	// area rejection says "reachable", A* then pops the node, finds nowhere to
	// go, and reports no route from it at all.  Draining the queue without
	// adding to it costs one more pass over a list that can no longer grow, and
	// leaves a smaller graph that is properly closed instead of a larger one
	// with a rim nobody can move off.
	bool atNodeLimit = false;
	const float stepSize = pm_stepsize.GetFloat();

	// A level walk is the same sweep in both directions, so a proven walk links
	// back at once and the neighbour skips probing toward this node when it is
	// expanded.  Only a neighbour that has not been expanded yet gets the
	// reverse link: one that already probed this way and failed keeps its
	// verdict, so a genuinely one-way step stays one way.  This halves the
	// collision sweeps of a build, which is most of its time.
	idList<bool> expanded;
	expanded.SetGranularity( 4096 );

	for ( i = 0; i < openList.Num(); i++ ) {
		const int current = openList[i];
		expanded.AssureSize( nodes.Num(), false );
		expanded[current] = true;

		if ( !atNodeLimit && nodes.Num() >= NAV_MAX_NODES ) {
			atNodeLimit = true;
			gameLocal.Warning( "rvNavMesh: node limit (%d) reached at bot_navCellSize %.0f. Part of "
							   "this map has no navigation; raise the cell size to cover it all",
							   NAV_MAX_NODES, cellSize );
		}

		// Copied, because AddNode below can reallocate the node list.
		const idVec3	fromFloor	= nodes[current].origin;
		const int		fromGX		= nodes[current].gx;
		const int		fromGY		= nodes[current].gy;

		for ( int dir = 0; dir < 8; dir++ ) {
			const int gx = fromGX + navNeighbourX[dir];
			const int gy = fromGY + navNeighbourY[dir];

			idVec3	toFloor;
			int		travelType;

			// Already linked into that cell by a neighbour's reverse walk.
			bool linkedAlready = false;
			for ( int l = nodes[current].firstLink; l != -1; l = links[l].next ) {
				const navNode_t &linked = nodes[links[l].node];
				if ( linked.gx == gx && linked.gy == gy ) {
					linkedAlready = true;
					break;
				}
			}
			if ( linkedAlready ) {
				continue;
			}

			if ( !TryStep( fromFloor, CellCentre( gx, gy, fromFloor.z ), toFloor, travelType ) ) {
				continue;
			}

			int neighbour = FindNode( gx, gy, toFloor.z );
			if ( neighbour == -1 ) {
				// Out of nodes: link only to ground that already exists.
				if ( atNodeLimit ) {
					continue;
				}
// openQ4 BEGIN
				if ( NavPointInLethalLiquid( toFloor ) ) {
					lethalLiquidSeen = true;
					continue;
				}
// openQ4 END
				const int hazard = HazardAtFloor( toFloor );
				if ( hazard >= 0 && hazards[hazard].lethal ) {
					continue;
				}
				neighbour = AddNode( gx, gy, toFloor );
				openList.Append( neighbour );
			}

			const float distance = ( toFloor - fromFloor ).Length();
			float cost = distance;
			if ( travelType == NAVTRAVEL_DROP ) {
				cost += NAV_DROP_PENALTY;
				const float fall = fromFloor.z - toFloor.z;
				if ( fall > NAV_SOFT_FALL ) {
					cost += NAV_FALL_DAMAGE_PENALTY;
				}
				if ( fall > NAV_HARD_FALL ) {
					cost += NAV_FALL_DAMAGE_PENALTY;
				}
			} else if ( travelType == NAVTRAVEL_JUMP ) {
				cost += NAV_JUMP_PENALTY;
			}
			if ( nodes[neighbour].flags & NAVNODE_HAZARD ) {
				cost += NAV_HAZARD_PENALTY;
			}
			// Stepping onto a rim node is dearer than stepping onto the one
			// beside it inland, so a route runs along a platform's edge only
			// where the edge is all there is.
			if ( nodes[neighbour].flags & NAVNODE_RIM ) {
				cost += NAV_RIM_PENALTY;
			}

			LinkNodes( current, neighbour, cost, travelType );

			// A drop is one way unless the reverse step is walkable on its own,
			// which the neighbour discovers when it is expanded.  A level walk is
			// linked back now, before the neighbour spends a sweep proving it.
			expanded.AssureSize( nodes.Num(), false );
			if ( travelType == NAVTRAVEL_WALK && !expanded[neighbour] &&
				 idMath::Fabs( toFloor.z - fromFloor.z ) <= stepSize ) {
				const float reverseCost = distance +
					( ( nodes[current].flags & NAVNODE_HAZARD ) ? NAV_HAZARD_PENALTY : 0.0f ) +
					( ( nodes[current].flags & NAVNODE_RIM ) ? NAV_RIM_PENALTY : 0.0f );
				LinkNodes( neighbour, current, reverseCost, NAVTRAVEL_WALK );
			}
		}
	}
}

/*
================
rvNavMesh::LevelNodeInCell

The node in a cell standing closest to height z, within tolerance, or -1.
================
*/
int rvNavMesh::LevelNodeInCell( int gx, int gy, float z, float tolerance ) const {
	const int key = CellKey( gx, gy );
	int best = -1;
	float bestDelta = tolerance;

	for ( int i = nodeHash.First( key ); i != -1; i = nodeHash.Next( i ) ) {
		if ( nodes[i].gx != gx || nodes[i].gy != gy ) {
			continue;
		}
		const float delta = idMath::Fabs( nodes[i].origin.z - z );
		if ( delta <= bestDelta ) {
			bestDelta = delta;
			best = i;
		}
	}

	return best;
}

/*
================
rvNavMesh::GapJumpClear

Proves a running jump through touchdown, using the same direction-to-corner
air input as the bot. Reaching the destination's horizontal position while
still airborne proves nothing about where momentum finally puts the player.
Check both the conservative approach speed and full speed, including the
takeoff margin. A legal landing has supported, nonlethal ground connected to
the destination; flying past a narrow ledge fails closed. Adjacent steps may
slide upward along a vertical riser before clearing it, as player physics does.
================
*/
bool rvNavMesh::GapJumpClear( const idVec3 &start, const idVec3 &end, float runSpeed,
							  float jumpSpeed, float gravity, bool stepHop,
							  const idVec3 *actualVelocity, float actualWishSpeed,
							  const idVec3 *firstWish, float firstWishSpeed ) const {
	idVec3 direction = end - start;
	direction.z = 0.0f;
	const float distance = direction.Normalize();
	if ( distance <= 0.0f ) {
		return false;
	}

	// The edge must not be right under the takeoff node: a bot reaches the
	// node a corner radius early and may need a frame to leave the ground.
	if ( !stepHop && !actualVelocity ) {
		idVec3 lip;
		if ( !SampleFloor( start + direction * 8.0f + idVec3( 0.0f, 0.0f, NAV_GROUND_OFFSET + 8.0f ), 24.0f, lip ) ||
			 idMath::Fabs( lip.z - start.z ) > pm_stepsize.GetFloat() ) {
			return false;
		}
	}

	const idVec3 takeoff = actualVelocity ? start : start - direction * NAV_GAP_TAKEOFF_SLACK;
	if ( stepHop ) {
		// The point ahead of a step can already be inside its raised face.
		// Check the actual takeoff's support instead of probing through it.
		idVec3 floor;
		if ( !SampleFloor( takeoff + idVec3( 0.0f, 0.0f, NAV_GROUND_OFFSET ),
			 pm_stepsize.GetFloat() + NAV_GROUND_OFFSET, floor ) ||
			 idMath::Fabs( floor.z - start.z ) > pm_stepsize.GetFloat() ) {
			return false;
		}
	}
	const float span = distance + NAV_GAP_TAKEOFF_SLACK;
	const float rise = end.z - start.z;
	const float discriminant = jumpSpeed * jumpSpeed - 2.0f * gravity * rise;
	if ( discriminant < 0.0f ) {
		return false;		// too high to reach at all
	}
	const float landTime = ( jumpSpeed + idMath::Sqrt( discriminant ) ) / gravity;
	if ( !actualVelocity && runSpeed * landTime < span ) {
		return false;		// comes down short of the landing
	}

	const float wishSpeed = Max( 1.0f, actualVelocity ? actualWishSpeed : pm_speed.GetFloat() );
	const float steppedLandTime = stepHop ?
		( jumpSpeed + idMath::Sqrt( discriminant + 2.0f * gravity * pm_stepsize.GetFloat() ) ) / gravity : landTime;
	const int steps = Min( NAV_PAD_MAX_STEPS,
		(int)idMath::Ceil( ( steppedLandTime + 0.1f ) / NAV_PAD_STEP_SECONDS ) );
	const bool traceJump = actualVelocity && bot_debug.GetInteger() >= 4;
	for ( int approach = 0; approach < ( actualVelocity ? 1 : 2 ); approach++ ) {
		// A live proof starts at the player's exact collision origin. Raising
		// it for floor-probe clearance can cross the air-step distance limit:
		// the proof then clips speed at a wall which the player steps over.
		idVec3 position = actualVelocity ? takeoff :
			takeoff + idVec3( 0.0f, 0.0f, NAV_GROUND_OFFSET );
		idVec3 velocity = actualVelocity ? *actualVelocity : direction * ( approach ? wishSpeed : runSpeed );
		velocity.z = jumpSpeed;
		bool landed = false;
		for ( int step = 0; step < steps; step++ ) {
			idVec3 wish = end - position;
			wish.z = 0.0f;
			const float frameWishSpeed = ( step == 0 && firstWish ) ? firstWishSpeed : wishSpeed;
			if ( step == 0 && firstWish ) {
				wish = *firstWish;
			}
			if ( wish.Normalize() > 0.0f ) {
				const float add = frameWishSpeed - velocity * wish;
				if ( add > 0.0f ) {
					velocity += wish * Min( add, NAV_PAD_AIR_ACCELERATE * NAV_PAD_STEP_SECONDS * frameWishSpeed );
				}
			}
			idVec3 next = position + velocity * NAV_PAD_STEP_SECONDS -
				idVec3( 0.0f, 0.0f, 0.5f * gravity * NAV_PAD_STEP_SECONDS * NAV_PAD_STEP_SECONDS );
			trace_t trace;
			bool clear = false;
			for ( int bump = 0; bump < 3; bump++ ) {
				if ( NavAgentTranslation( trace, position, next ) ) {
					clear = true;
					break;
				}
				if ( stepHop && velocity.z > 0.0f && trace.fraction > 0.0f &&
					 idMath::Fabs( trace.c.normal.z ) < 0.01f ) {
					const idVec3 remainder = next - trace.endpos;
					const idVec3 stepLift( 0.0f, 0.0f, pm_stepsize.GetFloat() );
					trace_t down, up, across;
					// MP AirMove can step while close to the floor. A successful
					// up/across/down keeps horizontal momentum, so clipping it
					// first would falsely approve a short raised platform.
					if ( !NavAgentTranslation( down, trace.endpos, trace.endpos - stepLift ) &&
						 down.c.normal.z > NAV_WALKABLE_NORMAL_Z ) {
						NavAgentTranslation( up, trace.endpos, trace.endpos + stepLift );
						NavAgentTranslation( across, up.endpos, up.endpos + remainder );
						NavAgentTranslation( down, across.endpos, across.endpos - stepLift );
						if ( down.fraction >= 1.0f || down.c.normal.z > NAV_WALKABLE_NORMAL_Z ) {
							if ( across.fraction >= 1.0f ) {
								next = down.endpos;
								clear = true;
								break;
							}
							if ( across.fraction > trace.fraction ) {
								return false;	// partial step needs more collision planes
							}
						}
					}
					// Hitting a stair's side on ascent removes inward speed;
					// it does not stop the vertical jump. Sweep the remainder
					// along that face. Descending impacts still fail closed.
					position = trace.endpos + trace.c.normal * NAV_GROUND_OFFSET;
					next = position + remainder - trace.c.normal * Min( 0.0f, remainder * trace.c.normal );
					velocity -= trace.c.normal * Min( 0.0f, velocity * trace.c.normal );
					continue;
				}
				break;
			}
			if ( traceJump ) {
				gameLocal.Printf( "NAV_JUMP_FRAME time=%d step=%d position=%.3f %.3f %.3f next=%.3f %.3f %.3f velocity=%.3f %.3f %.3f clear=%d fraction=%.4f\n",
					gameLocal.time, step, position.x, position.y, position.z, next.x, next.y, next.z,
					velocity.x, velocity.y, velocity.z, clear ? 1 : 0, trace.fraction );
			}
			if ( !clear ) {
				landed = velocity.z < 0.0f && trace.fraction > 0.0f &&
					trace.c.normal.z >= NAV_WALKABLE_NORMAL_Z &&
					idMath::Fabs( trace.endpos.z - end.z ) <= pm_stepsize.GetFloat() &&
					NavLandingSupported( trace.endpos ) && !IsLethalFloor( trace.endpos ) &&
					WalkableLine( trace.endpos, end );
				break;
			}
			position = next;
			velocity.z -= gravity * NAV_PAD_STEP_SECONDS;
		}
		if ( !landed ) {
			return false;
		}
	}
	return true;
}

bool rvNavMesh::JumpTakeoffClear( const idVec3 &origin, const idVec3 &velocity,
								const idVec3 &landing, float gravity, float wishSpeed,
								const idVec3 *firstWish, float firstWishSpeed ) const {
	if ( gravity <= 1.0f || pm_jumpheight.GetFloat() <= 0.0f ) {
		return false;
	}
	trace_t ground;
	const idVec3 lifted = origin + idVec3( 0.0f, 0.0f, NAV_GROUND_OFFSET );
	if ( NavAgentTranslation( ground, lifted, lifted - idVec3( 0.0f, 0.0f, 2.0f * NAV_GROUND_OFFSET ) ) ||
		 ground.fraction <= 0.0f || ground.c.normal.z < NAV_WALKABLE_NORMAL_Z ||
		 velocity * ground.c.normal < -1.0f ) {
		// CheckJump projects inward velocity onto the ground before adding the
		// impulse. Wait for a settled approach instead of guessing that slide.
		return false;
	}
	const float jumpSpeed = velocity.z + idMath::Sqrt( 2.0f * gravity * pm_jumpheight.GetFloat() );
	return jumpSpeed > 0.0f && GapJumpClear( origin, landing, wishSpeed, jumpSpeed,
		gravity, true, &velocity, wishSpeed, firstWish, firstWishSpeed );
}

/*
================
rvNavMesh::DodgeJumpClear

The walking dodge probe covers 72 units; a running hop carries a player more
than 200. Prove a supported touchdown and room for the remaining momentum,
both for the actual velocity and for a full-speed approach. These voluntary hops do not need to reach a lower
floor or use a transport, so stop at the normal flight time and fail closed.
================
*/
bool rvNavMesh::DodgeJumpClear( const idVec3 &origin, const idVec3 &velocity,
							  const idVec3 &direction, float gravity, float effectiveWishSpeed ) const {
	if ( gravity <= 1.0f || pm_jumpheight.GetFloat() <= 0.0f ) {
		return false;
	}
	idVec3 wish = direction;
	wish.z = 0.0f;
	wish.Normalize();
	const float wishSpeed = Max( 1.0f, effectiveWishSpeed > 0.0f ? effectiveWishSpeed : pm_speed.GetFloat() );
	const float jumpSpeed = idMath::Sqrt( 2.0f * gravity * pm_jumpheight.GetFloat() );
	const float verticalSpeed = Max( 0.0f, velocity.z ) + jumpSpeed;
	const float flightSeconds = 2.0f * verticalSpeed / gravity + 0.1f;
	const int steps = Min( 96, (int)idMath::Ceil( flightSeconds / NAV_PAD_STEP_SECONDS ) );

	// Most hops are nowhere near a pad or a teleporter.  One box over the whole
	// flight's reach decides whether the per-step brush sweeps are needed at
	// all; a bot hopping its way back onto the mesh asked for them on every
	// step of every other grounded frame.
	const float reach = Max( wishSpeed, idVec3( velocity.x, velocity.y, 0.0f ).Length() ) * flightSeconds + 32.0f;
	idBounds flight;
	flight.Clear();
	flight.AddPoint( origin - idVec3( reach, reach, pm_stepsize.GetFloat() + NAV_SMOOTH_MAX_DIP ) );
	flight.AddPoint( origin + idVec3( reach, reach,
		verticalSpeed * verticalSpeed / ( 2.0f * gravity ) + GetAgentBounds()[1].z + NAV_SMOOTH_MAX_DIP ) );
	const bool nearTransport = TransportAt( flight ) >= 0;

	for ( int approach = 0; approach < 2; approach++ ) {
		idVec3 speed = approach ? wish * wishSpeed : velocity;
		speed.z = verticalSpeed;
		idVec3 position = origin + idVec3( 0.0f, 0.0f, NAV_GROUND_OFFSET );
		bool landed = false;
		for ( int step = 0; step < steps; step++ ) {
			const float add = wishSpeed - speed * wish;
			if ( add > 0.0f ) {
				speed += wish * Min( add, NAV_PAD_AIR_ACCELERATE * NAV_PAD_STEP_SECONDS * wishSpeed );
			}
			const idVec3 next = position + speed * NAV_PAD_STEP_SECONDS -
				idVec3( 0.0f, 0.0f, 0.5f * gravity * NAV_PAD_STEP_SECONDS * NAV_PAD_STEP_SECONDS );
			if ( nearTransport && LineEntersTransport( position, next ) ) {
				return false;
			}
			trace_t trace;
			if ( !NavAgentTranslation( trace, position, next ) ) {
				idBounds standing = GetAgentBounds();
				standing.TranslateSelf( trace.endpos );
				landed = speed.z < 0.0f && trace.fraction > 0.0f &&
					trace.c.normal.z >= NAV_WALKABLE_NORMAL_Z &&
					trace.endpos.z >= origin.z - pm_stepsize.GetFloat() &&
					NavLandingSupported( trace.endpos ) && !IsLethalFloor( trace.endpos ) &&
					TransportAt( standing ) < 0;
				if ( landed ) {
					// A rim touchdown can retain enough sideways speed to leave
					// the floor before grounded steering can stop it. Reserve the
					// same supported runway used by the grounded edge guard.
					const idVec3 runout = trace.endpos +
						idVec3( speed.x, speed.y, 0.0f ) * NAV_DODGE_RUNOUT_SECONDS;
					landed = LandingContinuationClear( trace.endpos, runout );
				}
				if ( bot_debug.GetInteger() >= 4 ) {
					gameLocal.Printf( "NAV_DODGE_LANDING approach=%d supported=%d origin=%.3f %.3f %.3f touchdown=%.3f %.3f %.3f velocity=%.3f %.3f %.3f\n",
						approach, landed ? 1 : 0, origin.x, origin.y, origin.z,
						trace.endpos.x, trace.endpos.y, trace.endpos.z, speed.x, speed.y, speed.z );
				}
				break;
			}
			position = next;
			speed.z -= gravity * NAV_PAD_STEP_SECONDS;
		}
		if ( !landed ) {
			return false;
		}
	}
	return true;
}

/*
================
rvNavMesh::AddGapJumpLinks

Running jumps across pits and between platforms that flood fill, which only
steps, hops and drops between neighbouring cells, cannot connect.  From a
ledge node - no level floor in front of it - with a straight run-up behind it,
each grid direction is searched for the first floor beyond the gap within a
running jump's reach, and the jump is linked only if GapJumpClear proves it.
Stock maps hold real items behind such jumps: q4xdm14's mega health alcove
over the lava pit, q4dm5's red armour ledge.
================
*/
void rvNavMesh::AddGapJumpLinks( void ) {
	const idVec3 &gravityVector = gameLocal.GetGravity();
	const float gravity = ( gravityVector.LengthSqr() > 1.0f ) ? gravityVector.Length() : 800.0f;
	const float stepSize = pm_stepsize.GetFloat();
	const float jumpHeight = pm_jumpheight.GetFloat();
	const float runSpeed = Max( 1.0f, pm_speed.GetFloat() ) * NAV_GAP_SPEED_FRACTION;
	const float jumpSpeed = idMath::Sqrt( 2.0f * gravity * jumpHeight );
	const float longestAir = ( jumpSpeed + idMath::Sqrt( jumpSpeed * jumpSpeed + 2.0f * gravity * NAV_GAP_MAX_FALL ) ) / gravity;
	const float maxReach = runSpeed * longestAir;
	const int numBuilt = nodes.Num();
	int added = 0;
	int rimAdded = 0;

	for ( int takeoff = 0; takeoff < numBuilt; takeoff++ ) {
		const idVec3 origin = nodes[takeoff].origin;
		const int gx = nodes[takeoff].gx;
		const int gy = nodes[takeoff].gy;

		for ( int dir = 0; dir < 8; dir++ ) {
			const int dx = navNeighbourX[dir];
			const int dy = navNeighbourY[dir];

			// A ledge: nothing level to walk onto in this direction...
			if ( LevelNodeInCell( gx + dx, gy + dy, origin.z, stepSize ) >= 0 ) {
				continue;
			}
			// ...and a straight run-up behind it.
			const int runup = LevelNodeInCell( gx - dx, gy - dy, origin.z, stepSize );
			if ( runup < 0 || FindLink( runup, takeoff ) < 0 ) {
				continue;
			}

			const float cellStep = cellSize * ( ( dx != 0 && dy != 0 ) ? idMath::SQRT_TWO : 1.0f );
			const int maxCells = (int)( maxReach / cellStep );
			// Set once the first floor has proved to be a trench's: more than a
			// step below the takeoff.  The scan then looks past it for a rim at
			// the takeoff's own level.
			bool seekingRim = false;
			for ( int k = 2; k <= maxCells; k++ ) {
				// The highest floor in the landing band: no more than a jump's
				// height above the takeoff, no more than NAV_GAP_MAX_FALL below.
				const int cx = gx + dx * k;
				const int cy = gy + dy * k;
				const int key = CellKey( cx, cy );
				int landing = -1;
				for ( int n = nodeHash.First( key ); n != -1; n = nodeHash.Next( n ) ) {
					if ( nodes[n].gx != cx || nodes[n].gy != cy ) {
						continue;
					}
					const float rise = nodes[n].origin.z - origin.z;
					if ( rise > jumpHeight - 8.0f || rise < -NAV_GAP_MAX_FALL ) {
						continue;
					}
					if ( landing < 0 || nodes[n].origin.z > nodes[landing].origin.z ) {
						landing = n;
					}
				}
				if ( landing < 0 ) {
					continue;	// still over the gap
				}
				const bool belowTakeoff = nodes[landing].origin.z < origin.z - stepSize;
				if ( seekingRim && belowTakeoff ) {
					continue;	// still the trench's floor
				}

				// The first floor beyond the gap decides this direction.  Its
				// near edge cell may only clip the platform; then the next cell
				// on the same platform is the landing a running jump makes.
				if ( !NavLandingSupported( nodes[landing].origin ) ) {
					landing = LevelNodeInCell( cx + dx, cy + dy, nodes[landing].origin.z, stepSize );
					if ( landing >= 0 && !NavLandingSupported( nodes[landing].origin ) ) {
						landing = -1;
					}
				}
				if ( landing >= 0 && FindLink( takeoff, landing ) < 0 &&
					 GapJumpClear( origin, nodes[landing].origin, runSpeed, jumpSpeed, gravity ) ) {
					const float cost = ( nodes[landing].origin - origin ).Length() + NAV_JUMP_PENALTY +
						NAV_GAP_PENALTY + ( ( nodes[landing].flags & NAVNODE_HAZARD ) ? NAV_HAZARD_PENALTY : 0.0f );
					LinkNodes( takeoff, landing, cost, NAVTRAVEL_JUMP );
					added++;
					if ( seekingRim ) {
						rimAdded++;
					}
				}
				// A trench no deeper than NAV_GAP_MAX_FALL is a floor in the
				// landing band, so the scan used to stop at it and link only
				// the drop in.  Keep looking for a far rim at the takeoff's
				// level; GapJumpClear still proves the whole arc over it.
				if ( belowTakeoff && !seekingRim ) {
					seekingRim = true;
					continue;
				}
				break;
			}
		}
	}

	if ( bot_debug.GetBool() ) {
		gameLocal.Printf( "rvNavMesh: %d gap jump links (%d over trenches)\n", added, rimAdded );
	}
	gapJumpLinks = added;
	trenchRimLinks = rimAdded;
}

/*
================
rvNavMesh::NodeForEntityPoint

Snaps an entity's world position onto the mesh.  Entities that matter here -
jump pads, teleport exits - are placed by mappers with a generous margin, so a
wide search radius is the right trade.
================
*/
int rvNavMesh::NodeForEntityPoint( const idVec3 &point ) const {
	idVec3 floor;
	idVec3 probe = point;

	probe.z += pm_stepsize.GetFloat();

	if ( SampleFloor( probe, 512.0f, floor ) ) {
		const int node = FindNearestNode( floor, 128.0f );
		if ( node != -1 ) {
			return node;
		}
	}

	return FindNearestNode( point, 256.0f );
}

/*
================
rvNavMesh::PadLanding

Where a jump pad really puts a player down.  rvJumpPad sets a velocity that
spends its climb at the target, so the target is the apex of the arc, not the
landing: q4dm1's pad 24 aims above a ledge at 224 and drops players onto the
floor at 64, 75 units on.  Linking the floor under the target sent every route
through that pad to a corner nobody arrives at.  The arc is integrated from the
pad's own launch velocity - recomputed exactly as rvJumpPad::Event_FindTargets
does - and swept with the player's box until it comes down on a floor.

When steerTo is given the flight steers for it as the bot will, with
NavAirSteer through idPhysics_Player's air acceleration.
================
*/
bool rvNavMesh::PadLanding( idEntity *pad, const idVec3 &launch, const idVec3 *steerTo, idVec3 &landing,
							float *flightSeconds, float *impactDelta ) const {
	const idVec3 gravity = gameLocal.GetCurrentGravity( pad );
	const float g = gravity.Length();
	idVec3 launchVelocity;
	if ( !NavPadLaunchVelocity( pad, gravity, launchVelocity ) ) {
		return false;
	}
	const idVec3 down = gravity / g;
	idVec3 velocity = launchVelocity;

	const float wishSpeed = Max( 1.0f, pm_speed.GetFloat() );
	idVec3 position = launch + idVec3( 0.0f, 0.0f, NAV_GROUND_OFFSET );
	for ( int step = 0; step < NAV_PAD_MAX_STEPS; step++ ) {
		// Force_Field runs after player movement. The following command starts
		// with the pad's velocity again if that movement still touched it.
		// Missing these repeated pushes underestimates both range and impact.
		if ( step > 0 && NavPadVolumeTouches( pad, position ) ) {
			velocity = launchVelocity;
		}
		idVec3 wish;
		if ( steerTo && NavAirSteer( position, velocity, *steerTo, g, wish ) ) {
			// idPhysics_Player::Accelerate, Quake 2 style.
			const float add = wishSpeed - velocity * wish;
			if ( add > 0.0f ) {
				velocity += wish * Min( add, NAV_PAD_AIR_ACCELERATE * NAV_PAD_STEP_SECONDS * wishSpeed );
			}
		}

		const idVec3 next = position + velocity * NAV_PAD_STEP_SECONDS +
			gravity * ( 0.5f * NAV_PAD_STEP_SECONDS * NAV_PAD_STEP_SECONDS );
		trace_t trace;
		if ( NavAgentTranslation( trace, position, next ) ) {
			position = next;
			velocity += gravity * NAV_PAD_STEP_SECONDS;
			continue;
		}

		// Coming down on standable ground ends the flight.  A wall or a
		// ceiling takes the velocity into it and the arc carries on along it.
		if ( trace.c.normal.z >= NAV_WALKABLE_NORMAL_Z && velocity * down > 0.0f ) {
			landing = trace.endpos;
			if ( flightSeconds ) {
				*flightSeconds = (float)( step + 1 ) * NAV_PAD_STEP_SECONDS;
			}
			if ( impactDelta ) {
				*impactDelta = NavLandingImpactDelta( trace,
					velocity + gravity * ( NAV_PAD_STEP_SECONDS * trace.fraction ), gravity );
			}
			return true;
		}
		if ( trace.fraction <= 0.0f && ( velocity * trace.c.normal ) >= 0.0f ) {
			return false;		// wedged: no honest landing to report
		}
		position = trace.endpos;
		velocity -= trace.c.normal * ( velocity * trace.c.normal );
		velocity += gravity * NAV_PAD_STEP_SECONDS;
	}

	return false;
}

/*
================
NavPadFlightCost

What a pad's flight is worth to a route: the ground a bot would have covered
running for as long as it is in the air, and never less than the old flat
price.  A flat price made every flight look like a 128-unit walk, so a route
could take a second and a half in the air beside a vertical pad just to get
past it.
================
*/
static float NavPadFlightCost( float flightSeconds ) {
	return Max( NAV_JUMPPAD_COST, flightSeconds * Max( 1.0f, pm_speed.GetFloat() ) );
}

/*
================
rvNavMesh::PadLandings

Every place a bot can bring a pad's flight down.  The natural landing comes
first - the arc as the pad throws it, which a bot rides without steering.  Then, for each floor level within
air-control reach of the apex, the supported node nearest the apex is tried as
a steered landing and kept when the flight really comes down on it; Quake
III's AAS links a jump pad to the areas under its trajectory the same way.
That is what takes a bot up q4dm1's vertical pads: their arc falls back down
their own shaft, and only steering puts it on the ledge beside it.  Nothing
that comes down on the pad itself is a landing.

A bot does not leave from the pad's centre, though: the pad throws it the
moment its box first touches the volume, half a box outside it on whichever
side it walked in from.  A steered landing that only works from the centre -
q4dm5's pad 2 slides its arc along a wall and under a ledge's lip to reach the
ledge - timed out in play, so an air-control landing is kept only when the
flight makes it from every side a box can stand at.

The natural landing's node is the nearest one to the touchdown with support
under the middle of the hull, and the flight from the centre has to come
down on it the way a bot flies it - ridden where the arc lands on or past
it, steered for it otherwise.  q4dz1's vertical pads sit in a shaft whose
ledge the arc from the pad's centre just catches: the nearest node to that
touchdown hung half a hull over the shaft, and from the front of the pad,
where every bot enters, a flight steered for it came down short, into the
shaft, and bounced until the traversal timed out; steered for the supported
node a hull further in, it lands.
================
*/
bool rvNavMesh::PadLandings( idEntity *pad, int from, const idBounds &entryBounds, int targetNode,
							 idList<int> &landings, idList<float> &costs ) const {
	idEntity *target = ( pad && pad->targets.Num() ) ? pad->targets[0].GetEntity() : NULL;
	if ( !target || !target->GetPhysics() ) {
		return false;
	}
	const idVec3 launch = nodes[from].origin;
	const idBounds agent = GetAgentBounds();

	// Where a bot can really be thrown from: the centre the arc was flown
	// from, and the middle of each side of the volume where a standing box
	// first touches it on the pad's floor.
	idVec3	launches[5];
	int		numLaunches = 0;
	launches[numLaunches++] = launch;
	const idVec3 centre = entryBounds.GetCenter();
	for ( int side = 0; side < 4; side++ ) {
		idVec3 edge( centre.x, centre.y, launch.z );
		const int axis = side >> 1;
		edge[axis] = ( side & 1 ) ? entryBounds[1][axis] + agent[1][axis] : entryBounds[0][axis] + agent[0][axis];
		idVec3 floor;
		if ( SampleFloor( edge + idVec3( 0.0f, 0.0f, pm_stepsize.GetFloat() ), pm_stepsize.GetFloat() * 2.0f, floor ) &&
			 idMath::Fabs( floor.z - launch.z ) <= pm_stepsize.GetFloat() ) {
			launches[numLaunches++] = floor;
		}
	}

	idVec3 landing;
	float flight = 0.0f;
	if ( PadLanding( pad, launch, NULL, landing, &flight ) ) {
		idBounds standing = agent;
		standing.TranslateSelf( landing );
		int landed = -1;
		if ( !standing.IntersectsBounds( entryBounds ) ) {
			landed = NearestLandingNode( landing, from, entryBounds );
			if ( landed == -1 ) {
				landed = FindNearestNode( landing, 128.0f, true );
			}
		}
		if ( landed != -1 && landed != from ) {
			// From the centre only: a bot thrown from a side of a directional
			// pad comes down beside the ledge, as q4dm5's forty pads show, and
			// recovers from there; demanding the ledge from every side sealed
			// twenty of them.
			if ( PadFlightReaches( pad, launches, 1, nodes[landed].origin, flight, true ) ) {
				landings.Append( landed );
				costs.Append( NavPadFlightCost( flight ) );
			} else if ( bot_debug.GetBool() ) {
				idVec3 steered;
				const bool flew = PadLanding( pad, launch, &nodes[landed].origin, steered );
				gameLocal.Printf( "rvNavMesh: pad '%s' natural landing %s not reached: ridden %s steered %s\n",
								  pad->GetName(), nodes[landed].origin.ToString( 0 ), landing.ToString( 0 ),
								  flew ? steered.ToString( 0 ) : "nowhere" );
			}
		}
	} else if ( targetNode != -1 && targetNode != from ) {
		// No arc to fly - no gravity here, or wedged at launch: the floor
		// under the target, as before there was a simulation.
		landings.Append( targetNode );
		costs.Append( NAV_JUMPPAD_COST );
		return true;
	}
	const bool naturalFirst = landings.Num() > 0;

	// The supported node nearest the apex on each floor level below it.
	const idVec3 apex = target->GetPhysics()->GetOrigin();
	int		levelNode[NAV_PAD_MAX_LEVELS];
	float	levelZ[NAV_PAD_MAX_LEVELS];
	float	levelDistance[NAV_PAD_MAX_LEVELS];
	int		numLevels = 0;

	int minX, minY, maxX, maxY;
	CellCoords( apex - idVec3( NAV_PAD_STEER_REACH, NAV_PAD_STEER_REACH, 0.0f ), minX, minY );
	CellCoords( apex + idVec3( NAV_PAD_STEER_REACH, NAV_PAD_STEER_REACH, 0.0f ), maxX, maxY );
	for ( int gy = minY; gy <= maxY; gy++ ) {
		for ( int gx = minX; gx <= maxX; gx++ ) {
			const int key = CellKey( gx, gy );
			for ( int n = nodeHash.First( key ); n != -1; n = nodeHash.Next( n ) ) {
				const navNode_t &node = nodes[n];
				if ( node.gx != gx || node.gy != gy || n == from || node.origin.z > apex.z - NAV_GROUND_OFFSET ) {
					continue;
				}
				idVec3 aside = node.origin - apex;
				aside.z = 0.0f;
				const float distance = aside.LengthSqr();
				if ( distance > Square( NAV_PAD_STEER_REACH ) ) {
					continue;
				}
				int level = 0;
				while ( level < numLevels && idMath::Fabs( levelZ[level] - node.origin.z ) > NAV_PAD_LEVEL_BAND ) {
					level++;
				}
				if ( level < numLevels && distance >= levelDistance[level] ) {
					continue;
				}
				if ( level == numLevels && numLevels == NAV_PAD_MAX_LEVELS ) {
					continue;
				}
				idBounds standing = agent;
				standing.TranslateSelf( node.origin );
				if ( standing.IntersectsBounds( entryBounds ) || !NavLandingSupported( node.origin ) ) {
					continue;
				}
				if ( level == numLevels ) {
					numLevels++;
				}
				levelNode[level]		= n;
				levelZ[level]			= node.origin.z;
				levelDistance[level]	= distance;
			}
		}
	}

	// Nearest first, so the cap keeps the landings least air control reaches.
	while ( numLevels > 0 && landings.Num() < NAV_MAX_TRANSPORT_LANDINGS ) {
		int nearest = 0;
		for ( int i = 1; i < numLevels; i++ ) {
			if ( levelDistance[i] < levelDistance[nearest] ) {
				nearest = i;
			}
		}
		const int candidate = levelNode[nearest];
		numLevels--;
		levelNode[nearest]		= levelNode[numLevels];
		levelZ[nearest]			= levelZ[numLevels];
		levelDistance[nearest]	= levelDistance[numLevels];

		if ( landings.FindIndex( candidate ) >= 0 ) {
			continue;
		}
		float centreFlight = 0.0f;
		if ( PadFlightReaches( pad, launches, numLaunches, nodes[candidate].origin, centreFlight, false ) ) {
			landings.Append( candidate );
			costs.Append( NavPadFlightCost( centreFlight ) );
		}
	}
	return naturalFirst;
}

/*
================
NavPadArcRidden

Whether a bot thrown here would ride the pad's own arc to this landing, as
BotRidesPadArc decides in flight: the arc comes down on the landing's floor,
on or at most a little short of it along the flight and within a hull of it
to the side - and not so far past it that it is somewhere else.
================
*/
static bool NavPadArcRidden( const idVec3 &launch, const idVec3 &landing, const idVec3 &target ) {
	if ( idMath::Fabs( landing.z - target.z ) > pm_stepsize.GetFloat() ) {
		return false;
	}
	idVec3 flat = landing - launch;
	flat.z = 0.0f;
	if ( flat.Normalize() < 1.0f ) {
		return false;
	}
	idVec3 toTarget = target - landing;
	toTarget.z = 0.0f;
	const float along = toTarget * flat;
	const idVec3 side = toTarget - flat * along;
	return along <= NAV_PAD_RIDE_SHORT && along >= -NAV_PAD_RIDE_OVERSHOOT &&
		   side.LengthSqr() <= Square( NAV_PAD_RIDE_SIDE );
}

/*
================
rvNavMesh::PadFlightReaches

Whether a pad's flight really comes down on the landing from each of these
launches.  A natural landing is flown as the bot flies it:
ridden when the pad's own arc comes down on it, steered for otherwise.  A
steered landing is always steered for.  q4ctf1's pad 17 slides its arc along
a wall and comes down ten units past its node; steering for that node takes
the flight off a ledge 192 units lower, and a bot rides it instead.
centreFlight is how long the flight from the first launch - the pad's
centre - takes.
================
*/
bool rvNavMesh::PadFlightReaches( idEntity *pad, const idVec3 *launches, int numLaunches,
								  const idVec3 &target, float &centreFlight, bool natural ) const {
	centreFlight = 0.0f;
	for ( int l = 0; l < numLaunches; l++ ) {
		idVec3 landing;
		float flight = 0.0f;
		if ( natural && PadLanding( pad, launches[l], NULL, landing, &flight ) &&
			 NavPadArcRidden( launches[l], landing, target ) ) {
			if ( l == 0 ) {
				centreFlight = flight;
			}
			continue;
		}
		if ( !PadLanding( pad, launches[l], &target, landing, &flight ) ) {
			return false;
		}
		idVec3 miss = landing - target;
		if ( idMath::Fabs( miss.z ) > pm_stepsize.GetFloat() ) {
			return false;
		}
		miss.z = 0.0f;
		if ( miss.LengthSqr() > Square( NAV_PAD_LANDING_REACH ) ) {
			return false;
		}
		if ( l == 0 ) {
			centreFlight = flight;
		}
	}
	return true;
}

/*
================
rvNavMesh::NearestLandingNode

The node nearest to where a flight touched down that a landing can be made
on: on the same floor, clear of the pad's own volume, and with floor under
the middle of the hull, not only under one edge.  The hull sample that found
the touchdown rests on whatever is highest under any part of it, so the
nearest node by distance alone can hang over the edge of the floor the
flight reached; a bot steering for that node's centre comes down short of
the floor as often as on it.
================
*/
int rvNavMesh::NearestLandingNode( const idVec3 &touchdown, int from, const idBounds &entryBounds ) const {
	const float reach = NAV_PAD_LANDING_REACH * 4.0f;
	int minX, minY, maxX, maxY;
	CellCoords( touchdown - idVec3( reach, reach, 0.0f ), minX, minY );
	CellCoords( touchdown + idVec3( reach, reach, 0.0f ), maxX, maxY );

	int		best = -1;
	float	bestDistance = Square( reach );
	for ( int gy = minY; gy <= maxY; gy++ ) {
		for ( int gx = minX; gx <= maxX; gx++ ) {
			const int key = CellKey( gx, gy );
			for ( int n = nodeHash.First( key ); n != -1; n = nodeHash.Next( n ) ) {
				const navNode_t &node = nodes[n];
				if ( node.gx != gx || node.gy != gy || n == from || ( node.flags & NAVNODE_TRANSPORT ) ||
					 idMath::Fabs( node.origin.z - touchdown.z ) > NAV_SMOOTH_MAX_DIP ) {
					continue;
				}
				idVec3 aside = node.origin - touchdown;
				aside.z = 0.0f;
				const float distance = aside.LengthSqr();
				if ( distance >= bestDistance ) {
					continue;
				}
				idBounds standing = GetAgentBounds();
				standing.TranslateSelf( node.origin );
				if ( standing.IntersectsBounds( entryBounds ) || !NavLandingSupported( node.origin ) ) {
					continue;
				}
				best = n;
				bestDistance = distance;
			}
		}
	}
	return best;
}

/*
================
rvNavMesh::AddOffMeshLinks

Jump pads and teleporters move a player between places the walking flood fill
can never connect.  Both are "walk into the volume and you are carried", so
they become one way links whose entry point is the volume itself.
================
*/
void rvNavMesh::AddOffMeshLinks( void ) {
	idEntity *ent;
	int		 numLinksAdded = 0;

	for ( ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		int		travelType;
		float	cost;

		if ( ent->IsType( rvJumpPad::GetClassType() ) ) {
			travelType	= NAVTRAVEL_JUMPPAD;
			cost		= NAV_JUMPPAD_COST;
		} else if ( ent->IsType( idTrigger_Multi::GetClassType() ) ) {
			// idTrigger_Multi doubles as the teleporter trigger: a single
			// target, and that target is a player start.
			if ( ent->targets.Num() != 1 || !ent->targets[0].GetEntity() ) {
				continue;
			}
			if ( !ent->targets[0].GetEntity()->IsType( idPlayerStart::GetClassType() ) ) {
				continue;
			}
			travelType	= NAVTRAVEL_TELEPORT;
			cost		= NAV_TELEPORT_COST;
		} else {
			continue;
		}

		if ( ent->targets.Num() < 1 || !ent->targets[0].GetEntity() ) {
			continue;
		}

		idBounds entryBounds;
		if ( !NavTravelVolumeBounds( ent, entryBounds ) ) {
			continue;
		}

		const int from = NodeForEntityPoint( entryBounds.GetCenter() );
		const int to = NodeForEntityPoint( ent->targets[0].GetEntity()->GetPhysics()->GetOrigin() );
		// A live pad still launches whoever walks across it when no entry
		// node or landing was accepted. Keep that activation volume below;
		// sealing it with zero exits prevents an ordinary route through it.
		if ( from == -1 && travelType != NAVTRAVEL_JUMPPAD ) {
			continue;
		}

		// A pad's target is the apex of its arc, not a landing: link where its
		// flight comes down, and every other floor a bot can steer it onto.  A
		// pad whose flight only comes back down on itself is a trampoline a bot
		// would bounce on until the traversal timed out, and gets no link.
		idList<int> landings;
		idList<float> costs;
		bool naturalFirst = true;
		if ( travelType == NAVTRAVEL_JUMPPAD && from != -1 ) {
			naturalFirst = PadLandings( ent, from, entryBounds, to, landings, costs );
		} else if ( travelType == NAVTRAVEL_TELEPORT && to != -1 && to != from ) {
			landings.Append( to );
			costs.Append( cost );
		}
		if ( landings.Num() == 0 && travelType != NAVTRAVEL_JUMPPAD ) {
			continue;
		}

		for ( int i = 0; i < landings.Num(); i++ ) {
			LinkNodes( from, landings[i], costs[i], travelType, ent->entityNumber );
			numLinksAdded++;
			if ( bot_debug.GetBool() && travelType == NAVTRAVEL_JUMPPAD ) {
				gameLocal.Printf( "rvNavMesh: pad '%s' from %s lands at %s, cost %.0f%s\n", ent->GetName(),
								  nodes[from].origin.ToString( 0 ), nodes[landings[i]].origin.ToString( 0 ), costs[i],
								  i == 0 ? "" : " (steered)" );
			}
		}

		// Only a volume that is carrying players right now seals the ground
		// under it.  A pad switched off at load is floor until something turns
		// it on, and a disabled trigger has no contents to touch.
		const bool live = ( travelType == NAVTRAVEL_JUMPPAD ) ?
			ent->IsActive( TH_THINK ) :
			( ent->GetPhysics()->GetContents() != 0 );
		if ( live ) {
			navTransport_t transport;
			transport.bounds		= entryBounds;
			transport.entityNum		= ent->entityNumber;
			transport.travelType	= travelType;
			transport.naturalFirst	= naturalFirst && landings.Num() > 0;
			transport.numDestinations = Min( landings.Num(), NAV_MAX_TRANSPORT_LANDINGS );
			for ( int i = 0; i < transport.numDestinations; i++ ) {
				transport.destinations[i]	= landings[i];
				transport.costs[i]			= costs[i];
			}
			transports.Append( transport );
			if ( bot_debug.GetBool() && transport.numDestinations == 0 ) {
				gameLocal.Printf( "rvNavMesh: active pad '%s' at %s sealed without a routable landing\n",
					ent->GetName(), entryBounds.GetCenter().ToString( 0 ) );
			}
		}
	}

	if ( bot_debug.GetBool() ) {
		gameLocal.Printf( "rvNavMesh: %d off-mesh links\n", numLinksAdded );
	}
}

/*
================
rvNavMesh::SealTransportVolumes

A jump pad or a teleporter takes whoever touches it, so the ground under its
volume is not floor anyone walks across.  Flood fill laid ordinary links over
it all the same, and routes along them launched bots somewhere they never
meant to go: on q4dm5, a map of forty pads, most stuck reports were bots
carried off mid-route by a pad they were only walking past, or dropped onto a
landing beside one that threw them straight back up.  Every node whose
standing box touches a live volume keeps the transport as its only exit, so a
route either takes it or goes round, and WalkableLine refuses a shortcut that
would step into one.  Links into the volume stay: entering it is how a route
takes the transport.
================
*/
void rvNavMesh::SealTransportVolumes( void ) {
	transportNodes = 0;
	if ( transports.Num() == 0 ) {
		return;
	}

	const idBounds &agent = GetAgentBounds();
	idList<int> sealedBy;
	sealedBy.SetNum( nodes.Num() );
	for ( int i = 0; i < nodes.Num(); i++ ) {
		sealedBy[i] = -1;
	}

	for ( int t = 0; t < transports.Num(); t++ ) {
		// The cells whose standing box can reach the volume: the volume grown
		// by the box, horizontally.
		const idBounds &volume = transports[t].bounds;
		const idClipModel *clip = NavTravelVolumeClip( gameLocal.entities[transports[t].entityNum] );
		int minX, minY, maxX, maxY;
		CellCoords( volume[0] - idVec3( agent[1].x, agent[1].y, 0.0f ), minX, minY );
		CellCoords( volume[1] - idVec3( agent[0].x, agent[0].y, 0.0f ), maxX, maxY );

		for ( int gy = minY; gy <= maxY; gy++ ) {
			for ( int gx = minX; gx <= maxX; gx++ ) {
				const int key = CellKey( gx, gy );
				for ( int n = nodeHash.First( key ); n != -1; n = nodeHash.Next( n ) ) {
					if ( nodes[n].gx != gx || nodes[n].gy != gy || sealedBy[n] != -1 ) {
						continue;
					}
					idBounds standing = agent;
					standing.TranslateSelf( nodes[n].origin );
					if ( !standing.IntersectsBounds( volume ) ) {
						continue;
					}
					// The box against the brush itself, as the field and the
					// trigger test it: a diagonal pad's bounding box covers
					// floor beside the pad, and a node sealed there was a pad
					// entry no bot was ever launched from.
					if ( clip && clip->GetCollisionModel() &&
						 !collisionModelManager->Contents( nodes[n].origin, &navAgentTrace, mat3_identity, -1,
														   clip->GetCollisionModel(), clip->GetOrigin(),
														   clip->GetAxis() ) ) {
						continue;
					}
					sealedBy[n] = t;
				}
			}
		}
	}

	for ( int n = 0; n < nodes.Num(); n++ ) {
		const int t = sealedBy[n];
		if ( t < 0 ) {
			continue;
		}

		const navTransport_t &transport = transports[t];
		nodes[n].flags |= NAVNODE_TRANSPORT;
		nodes[n].firstLink = -1;
		for ( int d = 0; d < transport.numDestinations; d++ ) {
			if ( n != transport.destinations[d] ) {
				LinkNodes( n, transport.destinations[d], transport.costs[d], transport.travelType,
						   transport.entityNum );
			}
		}
		transportNodes++;
	}
}

/*
================
rvNavMesh::CompactLinks

Generation appends links in discovery order - a node's walks, then the reverse
walks its neighbours added, then gap jumps, then transports - so walking one
node's list jumped all over a quarter of a million entries, and every search
paid a cache miss per edge for it.  Once the graph is final, rewrite the
array so each node's links are one contiguous run in node order.  The lists
keep their form, so nothing that walks them changes; links that sealing
unhooked are dropped on the way.
================
*/
void rvNavMesh::CompactLinks( void ) {
	idList<navLink_t> compact;
	int live = 0;
	for ( int n = 0; n < nodes.Num(); n++ ) {
		for ( int l = nodes[n].firstLink; l != -1; l = links[l].next ) {
			live++;
		}
	}
	compact.SetNum( live );

	int write = 0;
	for ( int n = 0; n < nodes.Num(); n++ ) {
		const int first = write;
		for ( int l = nodes[n].firstLink; l != -1; l = links[l].next ) {
			compact[write] = links[l];
			compact[write].next = write + 1;
			write++;
		}
		if ( write > first ) {
			compact[write - 1].next = -1;
			nodes[n].firstLink = first;
		} else {
			nodes[n].firstLink = -1;
		}
	}

	links.Swap( compact );
}

/*
================
NavLineTouchesVolumeBrush

LineEntersTransport's test against a volume's brush itself, as sealing and the
trigger test it: the standing box carried along the line, and again
NAV_SMOOTH_MAX_DIP under it, as far as a string-pulled line floats over its
ground.  A pass whose box begins touching the brush is leaving it.  A diagonal
pad's bounding box covers floor beside the pad that sealing leaves walkable,
and a line that began on that floor used to be exempt from the pad altogether
- so a shortcut from there could cross the brush and launch the bot.
================
*/
static bool NavLineTouchesVolumeBrush( const idVec3 &from, const idVec3 &to, const idClipModel *clip ) {
	const idVec3 dip( 0.0f, 0.0f, NAV_SMOOTH_MAX_DIP );
	for ( int pass = 0; pass < 2; pass++ ) {
		const idVec3 start = pass ? from - dip : from;
		const idVec3 end = pass ? to - dip : to;
		if ( collisionModelManager->Contents( start, &navAgentTrace, mat3_identity, -1, clip->GetCollisionModel(),
											   clip->GetOrigin(), clip->GetAxis() ) ) {
			continue;
		}
		trace_t trace;
		collisionModelManager->Translation( &trace, start, end, &navAgentTrace, mat3_identity, -1,
											clip->GetCollisionModel(), clip->GetOrigin(), clip->GetAxis() );
		if ( trace.fraction < 1.0f ) {
			return true;
		}
	}
	return false;
}

/*
================
rvNavMesh::IsSteeredPadLanding
================
*/
bool rvNavMesh::IsSteeredPadLanding( int padEntityNum, const idVec3 &landing ) const {
	for ( int t = 0; t < transports.Num(); t++ ) {
		const navTransport_t &transport = transports[t];
		if ( transport.entityNum != padEntityNum ) {
			continue;
		}
		for ( int d = 0; d < transport.numDestinations; d++ ) {
			if ( ( nodes[transport.destinations[d]].origin - landing ).LengthSqr() <= 1.0f ) {
				return !( d == 0 && transport.naturalFirst );
			}
		}
		return false;
	}
	return false;
}

/*
================
rvNavMesh::LineEntersTransport

Would a standing box carried along this line touch a transport volume it did
not start in?  The line is a string-pulled one, which may float up to
NAV_SMOOTH_MAX_DIP over the ground it crosses, so the box is taken to reach
that far below it.  A line that begins in a volume is leaving it, which is
how a bot already on a pad snaps onto the mesh.
================
*/
bool rvNavMesh::LineEntersTransport( const idVec3 &from, const idVec3 &to ) const {
	if ( transports.Num() == 0 ) {
		return false;
	}

	const idBounds &agent = GetAgentBounds();
	idBounds sweep;
	sweep.Clear();
	sweep.AddPoint( from );
	sweep.AddPoint( to );
	const idVec3 delta = to - from;

	for ( int t = 0; t < transports.Num(); t++ ) {
		// The line touches the volume grown by the box exactly when the box,
		// carried along it, touches the volume.
		idBounds grown;
		grown[0] = transports[t].bounds[0] - agent[1];
		grown[1] = transports[t].bounds[1] - agent[0];
		grown[1].z += NAV_SMOOTH_MAX_DIP;

		if ( !grown.IntersectsBounds( sweep ) ) {
			continue;
		}

		// A volume with a brush answers exactly; only a brushless one is
		// judged by its box.
		const idClipModel *clip = NavTravelVolumeClip( gameLocal.entities[transports[t].entityNum] );
		if ( clip && clip->GetCollisionModel() ) {
			if ( NavLineTouchesVolumeBrush( from, to, clip ) ) {
				return true;
			}
			continue;
		}
		if ( grown.ContainsPoint( from ) ) {
			continue;
		}

		float enter = 0.0f;
		float leave = 1.0f;
		bool crosses = true;
		for ( int axis = 0; axis < 3 && crosses; axis++ ) {
			if ( idMath::Fabs( delta[axis] ) < idMath::FLOAT_EPSILON ) {
				crosses = from[axis] >= grown[0][axis] && from[axis] <= grown[1][axis];
				continue;
			}
			// Not "near"/"far": windef.h defines both away.
			float lo = ( grown[0][axis] - from[axis] ) / delta[axis];
			float hi = ( grown[1][axis] - from[axis] ) / delta[axis];
			if ( lo > hi ) {
				const float swap = lo;
				lo = hi;
				hi = swap;
			}
			enter = Max( enter, lo );
			leave = Min( leave, hi );
			crosses = enter <= leave;
		}
		if ( crosses ) {
			return true;
		}
	}

	return false;
}

/*
================
rvNavMesh::TransportAt
================
*/
int rvNavMesh::TransportAt( const idBounds &absBounds ) const {
	for ( int t = 0; t < transports.Num(); t++ ) {
		if ( transports[t].bounds.IntersectsBounds( absBounds ) ) {
			return transports[t].entityNum;
		}
	}
	return -1;
}

/*
================
rvNavMesh::TransportAtFloor

The exact form of TransportAt for a standing box on a floor point, as the
sealing pass tests nodes: a box against the volume's own brush.  The bounding
box of a diagonal pad covers floor beside it that the pad never touches, and a
goal standing there - an item beside the pad - read as "in the pad": its
route was allowed to end a cell short and arriving there picked up nothing.
================
*/
int rvNavMesh::TransportAtFloor( const idVec3 &floor ) const {
	idBounds standing = GetAgentBounds();
	standing.TranslateSelf( floor );

	for ( int t = 0; t < transports.Num(); t++ ) {
		if ( !transports[t].bounds.IntersectsBounds( standing ) ) {
			continue;
		}
		const idClipModel *clip = NavTravelVolumeClip( gameLocal.entities[transports[t].entityNum] );
		if ( clip && clip->GetCollisionModel() &&
			 !collisionModelManager->Contents( floor, &navAgentTrace, mat3_identity, -1,
											   clip->GetCollisionModel(), clip->GetOrigin(), clip->GetAxis() ) ) {
			continue;
		}
		return transports[t].entityNum;
	}
	return -1;
}

/*
================
rvNavMesh::TransportOnMove

The run-time form of LineEntersTransport for the bot's own box at its actual
origin: no string-pull dip, and a box that starts in contact is reported
rather than treated as leaving, because the bot asking is still on the
ground and the next frame of contact launches it.
================
*/
int rvNavMesh::TransportOnMove( const idVec3 &from, const idVec3 &to, int ignoreEntityNum ) const {
	const idBounds &agent = GetAgentBounds();
	idBounds sweep;
	sweep.Clear();
	sweep.AddPoint( from );
	sweep.AddPoint( to );
	sweep[0] += agent[0];
	sweep[1] += agent[1];

	for ( int t = 0; t < transports.Num(); t++ ) {
		const navTransport_t &transport = transports[t];
		if ( transport.entityNum == ignoreEntityNum || !transport.bounds.IntersectsBounds( sweep ) ) {
			continue;
		}
		const idClipModel *clip = NavTravelVolumeClip( gameLocal.entities[transport.entityNum] );
		if ( !clip || !clip->GetCollisionModel() ) {
			return transport.entityNum;		// a brushless volume is its box
		}
		if ( collisionModelManager->Contents( from, &navAgentTrace, mat3_identity, -1, clip->GetCollisionModel(),
											   clip->GetOrigin(), clip->GetAxis() ) ) {
			return transport.entityNum;
		}
		trace_t trace;
		collisionModelManager->Translation( &trace, from, to, &navAgentTrace, mat3_identity, -1,
											clip->GetCollisionModel(), clip->GetOrigin(), clip->GetAxis() );
		if ( trace.fraction < 1.0f ) {
			return transport.entityNum;
		}
	}
	return -1;
}

bool rvNavMesh::TransportCentre( int entityNum, idVec3 &centre ) const {
	for ( int t = 0; t < transports.Num(); t++ ) {
		if ( transports[t].entityNum == entityNum ) {
			centre = transports[t].bounds.GetCenter();
			return true;
		}
	}
	return false;
}

bool rvNavMesh::NaturalPadLanding( int padEntityNum, idVec3 &landing ) const {
	for ( int t = 0; t < transports.Num(); t++ ) {
		const navTransport_t &transport = transports[t];
		if ( transport.entityNum == padEntityNum && transport.travelType == NAVTRAVEL_JUMPPAD &&
			 transport.naturalFirst && transport.numDestinations > 0 ) {
			landing = nodes[transport.destinations[0]].origin;
			return true;
		}
	}
	return false;
}

/*
================
rvNavMesh::LandingSurvivable

Only a bot that cannot afford one of the live fall-damage tiers supplies a
finite limit. Keep the shared graph intact: this qualifies one route/entry for
that bot's current health, including safe materials and water at touchdown.
================
*/
bool rvNavMesh::LandingSurvivable( const idVec3 &from, const navCorner_t &corner, float maxImpactDelta ) const {
	if ( maxImpactDelta >= NAV_UNREACHED || ( corner.travelType != NAVTRAVEL_DROP &&
		 corner.travelType != NAVTRAVEL_JUMP && corner.travelType != NAVTRAVEL_JUMPPAD ) ) {
		return true;
	}
	if ( corner.travelType == NAVTRAVEL_JUMPPAD ) {
		if ( corner.actionEntityNum < 0 || corner.actionEntityNum >= MAX_GENTITIES ) {
			return false;
		}
		idEntity *pad = gameLocal.entities[corner.actionEntityNum];
		idVec3 landing;
		float delta = 0.0f;
		const idVec3 *steer = IsSteeredPadLanding( corner.actionEntityNum, corner.origin ) ? &corner.origin : NULL;
		return PadLanding( pad, from, steer, landing, NULL, &delta ) && delta <= maxImpactDelta;
	}

	const idVec3 gravity = gameLocal.GetGravity();
	const float g = gravity.Length();
	if ( g <= 1.0f ) {
		return true;
	}
	const float fall = Max( 0.0f, from.z - corner.origin.z +
		( corner.travelType == NAVTRAVEL_JUMP ? pm_jumpheight.GetFloat() : 0.0f ) );
	if ( 2.0f * g * fall * 0.0001f <= maxImpactDelta ) {
		return true;		// water/materials only reduce an already safe impact
	}
	const idVec3 velocity = gravity * ( idMath::Sqrt( 2.0f * g * fall ) / g );
	trace_t trace;
	const idVec3 lift( 0.0f, 0.0f, pm_stepsize.GetFloat() + NAV_GROUND_OFFSET );
	if ( NavAgentTranslation( trace, corner.origin + lift, corner.origin - lift ) ||
		 trace.fraction <= 0.0f || trace.c.normal.z < NAV_WALKABLE_NORMAL_Z ) {
		return false;
	}
	return NavLandingImpactDelta( trace, velocity, gravity ) <= maxImpactDelta;
}

/*
================
rvNavMesh::BuildAreas

Connected components over the links treated as undirected.  Two nodes in
different components definitely cannot reach each other, which lets a bot
reject an unreachable goal without paying for a full search.  The converse is
not guaranteed - a one way drop puts both ends in the same component - so this
is only ever used to say no.
================
*/
void rvNavMesh::BuildAreas( void ) {
	idList<int>	stack;
	int			i;

	// Reverse adjacency, so an undirected walk can follow one way links back.
	idList<int> reverseHead;
	idList<int> reverseNext;
	idList<int> reverseNode;

	reverseHead.AssureSize( nodes.Num(), -1 );
	reverseNext.SetGranularity( 4096 );
	reverseNode.SetGranularity( 4096 );

	for ( i = 0; i < nodes.Num(); i++ ) {
		for ( int l = nodes[i].firstLink; l != -1; l = links[l].next ) {
			const int dest = links[l].node;
			reverseNode.Append( i );
			reverseNext.Append( reverseHead[dest] );
			reverseHead[dest] = reverseNode.Num() - 1;
		}
	}

	numAreas = 0;

	for ( i = 0; i < nodes.Num(); i++ ) {
		if ( nodes[i].area != -1 ) {
			continue;
		}

		const int area = numAreas++;

		stack.Clear();
		stack.Append( i );
		nodes[i].area = area;

		while ( stack.Num() ) {
			const int current = stack[stack.Num() - 1];
			stack.SetNum( stack.Num() - 1, false );

			for ( int l = nodes[current].firstLink; l != -1; l = links[l].next ) {
				if ( nodes[links[l].node].area == -1 ) {
					nodes[links[l].node].area = area;
					stack.Append( links[l].node );
				}
			}

			for ( int r = reverseHead[current]; r != -1; r = reverseNext[r] ) {
				if ( nodes[reverseNode[r]].area == -1 ) {
					nodes[reverseNode[r]].area = area;
					stack.Append( reverseNode[r] );
				}
			}
		}
	}
}

/*
================
rvNavMesh::Build
================
*/
bool rvNavMesh::Build( void ) {
	idList<idVec3>	seeds;
	const int		startMsec = Sys_Milliseconds();

	// There is no collision world to probe until a map is running, and
	// GetWorldBounds would index an empty clip world list.
	if ( gameLocal.GameState() != GAMESTATE_ACTIVE ) {
		gameLocal.Warning( "rvNavMesh: no map is running, cannot build a navmesh" );
		return false;
	}

	Clear();

	cellSize = bot_navCellSize.GetFloat();
	if ( cellSize < 8.0f ) {
		cellSize = 8.0f;
	}

	gridOrigin = gameLocal.GetWorldBounds( NULL )[0];

	navAgentTrace = NavCreateAgentTrace( GetAgentBounds() );
	navAgentModel = new idClipModel( navAgentTrace );
	const float support = Min( NAV_LANDING_SUPPORT, GetAgentBounds()[1].x * 0.5f );
	navSupportBounds[0].Set( -support, -support, 0.0f );
	navSupportBounds[1].Set( support, support, support );
	navSupportTrace = idTraceModel( navSupportBounds );

	seeds.SetGranularity( 256 );
	GatherSeeds( seeds );

	if ( seeds.Num() == 0 ) {
		gameLocal.Warning( "rvNavMesh: no spawn points or items to seed from, cannot build a navmesh" );
		Clear();
		return false;
	}

	GatherHazards();
	GatherDynamicSolids();
	FloodFill( seeds );

	if ( nodes.Num() == 0 ) {
		gameLocal.Warning( "rvNavMesh: no walkable ground found from %d seeds", seeds.Num() );
		Clear();
		return false;
	}

	AddGapJumpLinks();
	AddOffMeshLinks();
	SealTransportVolumes();
	CompactLinks();
	BuildAreas();
	BuildComponents();

	// The graph is fixed from here on, so the search scratch can be sized once
	// instead of on every query.
	gScore.SetNum( nodes.Num(), true );
	fScore.SetNum( nodes.Num(), true );
	cameFrom.SetNum( nodes.Num(), true );
	openTag.SetNum( nodes.Num(), true );
	closedTag.SetNum( nodes.Num(), true );
	fieldTargetTag.SetNum( nodes.Num(), true );
	fieldPricedTag.SetNum( nodes.Num(), true );
	fieldPricedCost.SetNum( nodes.Num(), true );
	heap.Resize( 1024, 1024 );

	for ( int i = 0; i < nodes.Num(); i++ ) {
		openTag[i]	 = 0;
		closedTag[i] = 0;
		fieldTargetTag[i] = 0;
		fieldPricedTag[i] = 0;
	}
	searchSerial = 0;
	fieldSerial = 0;

	BuildLandmarks();

	entitySnaps.SetNum( MAX_GENTITIES, true );
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		entitySnaps[i].spawnId = -1;
		entitySnaps[i].node = -1;
		entitySnaps[i].time = 0;
		entitySnaps[i].origin.Zero();
	}
	ResetStats();

	// The probe model is only needed for generation; routing traces reuse it,
	// so it stays alive until Clear().
	buildMsec = Sys_Milliseconds() - startMsec;

	int hazardNodes = 0;
	for ( int i = 0; i < nodes.Num(); i++ ) {
		if ( nodes[i].flags & NAVNODE_HAZARD ) {
			hazardNodes++;
		}
	}

	// How much of the map one route can reach and come back from, and how
	// many nodes are a dead end - ground with no way off it at all, which a
	// pad that only bounces in place or a sealed volume with no landing
	// leaves behind.  A map sweep reads these to find where bots get trapped.
	idList<int> componentSize;
	componentSize.SetNum( Max( numComponents, 1 ) );
	for ( int i = 0; i < componentSize.Num(); i++ ) {
		componentSize[i] = 0;
	}
	int deadEnds = 0;
	for ( int i = 0; i < nodes.Num(); i++ ) {
		if ( nodes[i].component >= 0 && nodes[i].component < componentSize.Num() ) {
			componentSize[nodes[i].component]++;
		}
		if ( nodes[i].firstLink == -1 ) {
			deadEnds++;
		}
	}
	int largestComponent = 0;
	for ( int i = 0; i < componentSize.Num(); i++ ) {
		largestComponent = Max( largestComponent, componentSize[i] );
	}

	gameLocal.Printf( "rvNavMesh: %d nodes, %d links, %d areas, %d components (largest %.1f%%, %d dead ends), "
					  "%d landmarks, %d gap jumps (%d over trenches), %d hazard volumes (%d priced nodes), %d transports (%d sealed nodes), "
					  "%d seeds in %d ms (cell %g)\n",
					  nodes.Num(), links.Num(), numAreas, numComponents,
					  100.0f * (float)largestComponent / (float)Max( nodes.Num(), 1 ), deadEnds, numLandmarks,
					  gapJumpLinks, trenchRimLinks, hazards.Num(), hazardNodes, transports.Num(), transportNodes, seeds.Num(),
					  buildMsec, cellSize );

	return true;
}

/*
================
rvNavMesh::BuildComponents

Strongly connected components by an iterative Tarjan pass, then the transitive
closure of their condensation.  Weak areas can only ever say "no": a one-way
drop folds a pit and the floor above it into one area although nothing in the
pit can climb back out.  The closure answers the directed question exactly, so
a goal nobody can reach - an enemy standing in that pit - is refused in
constant time instead of by an A* that floods every node reachable from the
bot before it gives up, which on a failed chase is the whole map, every 750 ms.

Tarjan emits a component only after every component reachable from it, so the
ids run from sinks up, and a component's successors always have smaller ids:
one pass in id order ORs complete rows into the row being built.
================
*/
void rvNavMesh::BuildComponents( void ) {
	const int numNodes = nodes.Num();

	idList<int> index;
	idList<int> lowLink;
	idList<int> stack;
	idList<bool> onStack;
	idList<int> frameNode;
	idList<int> frameLink;

	index.AssureSize( numNodes, -1 );
	lowLink.AssureSize( numNodes, 0 );
	onStack.AssureSize( numNodes, false );
	stack.SetGranularity( 1024 );
	frameNode.SetGranularity( 1024 );
	frameLink.SetGranularity( 1024 );

	int nextIndex = 0;
	numComponents = 0;

	for ( int root = 0; root < numNodes; root++ ) {
		if ( index[root] != -1 ) {
			continue;
		}

		index[root] = lowLink[root] = nextIndex++;
		stack.Append( root );
		onStack[root] = true;
		frameNode.Append( root );
		frameLink.Append( nodes[root].firstLink );

		while ( frameNode.Num() ) {
			const int top = frameNode.Num() - 1;
			const int v = frameNode[top];
			const int l = frameLink[top];

			if ( l != -1 ) {
				frameLink[top] = links[l].next;

				const int w = links[l].node;
				if ( index[w] == -1 ) {
					index[w] = lowLink[w] = nextIndex++;
					stack.Append( w );
					onStack[w] = true;
					frameNode.Append( w );
					frameLink.Append( nodes[w].firstLink );
				} else if ( onStack[w] ) {
					lowLink[v] = Min( lowLink[v], index[w] );
				}
				continue;
			}

			if ( lowLink[v] == index[v] ) {
				while ( true ) {
					const int member = stack[stack.Num() - 1];
					stack.SetNum( stack.Num() - 1, false );
					onStack[member] = false;
					nodes[member].component = numComponents;
					if ( member == v ) {
						break;
					}
				}
				numComponents++;
			}

			frameNode.SetNum( top, false );
			frameLink.SetNum( top, false );
			if ( top > 0 ) {
				const int parent = frameNode[top - 1];
				lowLink[parent] = Min( lowLink[parent], lowLink[v] );
			}
		}
	}

	componentReach.Clear();
	componentWords = 0;
	if ( numComponents <= 0 || numComponents > NAV_MAX_CLOSURE_COMPONENTS ) {
		if ( numComponents > NAV_MAX_CLOSURE_COMPONENTS ) {
			gameLocal.Warning( "rvNavMesh: %d strongly connected components is more than the %d the "
							   "reachability closure covers; cross-component queries will search",
							   numComponents, NAV_MAX_CLOSURE_COMPONENTS );
		}
		return;
	}

	componentWords = ( numComponents + 31 ) >> 5;
	componentReach.AssureSize( numComponents * componentWords, 0u );

	// Group nodes by component so each row is built from its own members.
	idList<int> componentStart;
	idList<int> componentNodes;
	componentStart.AssureSize( numComponents + 1, 0 );
	componentNodes.SetNum( numNodes, false );
	for ( int i = 0; i < numNodes; i++ ) {
		componentStart[nodes[i].component + 1]++;
	}
	for ( int c = 0; c < numComponents; c++ ) {
		componentStart[c + 1] += componentStart[c];
	}
	idList<int> fill;
	fill.AssureSize( numComponents, 0 );
	for ( int i = 0; i < numNodes; i++ ) {
		const int c = nodes[i].component;
		componentNodes[componentStart[c] + fill[c]++] = i;
	}

	for ( int c = 0; c < numComponents; c++ ) {
		unsigned int *row = &componentReach[c * componentWords];
		row[c >> 5] |= 1u << ( c & 31 );

		for ( int m = componentStart[c]; m < componentStart[c + 1]; m++ ) {
			const int member = componentNodes[m];
			for ( int l = nodes[member].firstLink; l != -1; l = links[l].next ) {
				const int successor = nodes[links[l].node].component;
				if ( successor == c ) {
					continue;
				}
				assert( successor < c );
				const unsigned int *successorRow = &componentReach[successor * componentWords];
				for ( int w = 0; w < componentWords; w++ ) {
					row[w] |= successorRow[w];
				}
			}
		}
	}
}

/*
================
rvNavMesh::NodeCanReach
================
*/
bool rvNavMesh::NodeCanReach( int fromNode, int toNode ) const {
	if ( fromNode < 0 || fromNode >= nodes.Num() || toNode < 0 || toNode >= nodes.Num() ) {
		return false;
	}

	const int from = nodes[fromNode].component;
	const int to = nodes[toNode].component;
	if ( from == to ) {
		return true;
	}
	if ( nodes[fromNode].area != nodes[toNode].area ) {
		return false;
	}
	if ( componentWords <= 0 || from < 0 || to < 0 ) {
		return true;		// unknown: let the search decide
	}

	return ( componentReach[from * componentWords + ( to >> 5 )] & ( 1u << ( to & 31 ) ) ) != 0;
}

/*
================
rvNavMesh::Dijkstra

Unbounded single-source costs over the forward links, or over the reverse
adjacency for "cost to reach source".  Build-time only: it borrows the A*
heap and closed tags, so it must never run while a cost field is live.
================
*/
void rvNavMesh::Dijkstra( int source, bool reverse, float *distances ) const {
	const int numNodes = nodes.Num();

	for ( int i = 0; i < numNodes; i++ ) {
		distances[i] = NAV_INFINITE_COST;
	}
	if ( source < 0 || source >= numNodes ) {
		return;
	}

	BeginSearch();
	heap.SetNum( 0, false );

	distances[source] = 0.0f;
	HeapPush( source, 0.0f );

	while ( heap.Num() ) {
		const int current = HeapPop();
		if ( closedTag[current] == searchSerial ) {
			continue;
		}
		closedTag[current] = searchSerial;

		const float base = distances[current];
		if ( reverse ) {
			for ( int r = reverseOffset[current]; r < reverseOffset[current + 1]; r++ ) {
				const int previous = reverseSource[r];
				const float tentative = base + reverseCost[r];
				if ( tentative < distances[previous] ) {
					distances[previous] = tentative;
					HeapPush( previous, tentative );
				}
			}
		} else {
			for ( int l = nodes[current].firstLink; l != -1; l = links[l].next ) {
				const int next = links[l].node;
				const float tentative = base + links[l].cost;
				if ( tentative < distances[next] ) {
					distances[next] = tentative;
					HeapPush( next, tentative );
				}
			}
		}
	}
}

/*
================
rvNavMesh::BuildLandmarks

Picks landmarks by farthest-point sampling inside the largest strongly
connected component - the playable bulk of the map, where every node is
mutually reachable and every landmark cost is finite - and records exact
travel costs from and to each one.  Round-trip separation drives the choice,
so one-way drops cannot make a landmark look farther than it is.
================
*/
void rvNavMesh::BuildLandmarks( void ) {
	const int numNodes = nodes.Num();

	numLandmarks = 0;
	landmarkNodes.Clear();
	landmarkCost.Clear();

	if ( numNodes < 64 || numComponents <= 0 ) {
		return;
	}

	// Reverse adjacency, compressed by destination.
	reverseOffset.SetNum( numNodes + 1, false );
	for ( int i = 0; i <= numNodes; i++ ) {
		reverseOffset[i] = 0;
	}
	for ( int i = 0; i < numNodes; i++ ) {
		for ( int l = nodes[i].firstLink; l != -1; l = links[l].next ) {
			reverseOffset[links[l].node + 1]++;
		}
	}
	for ( int i = 0; i < numNodes; i++ ) {
		reverseOffset[i + 1] += reverseOffset[i];
	}
	reverseSource.SetNum( links.Num(), false );
	reverseCost.SetNum( links.Num(), false );
	{
		idList<int> cursor;
		cursor.SetNum( numNodes, false );
		for ( int i = 0; i < numNodes; i++ ) {
			cursor[i] = reverseOffset[i];
		}
		for ( int i = 0; i < numNodes; i++ ) {
			for ( int l = nodes[i].firstLink; l != -1; l = links[l].next ) {
				const int slot = cursor[links[l].node]++;
				reverseSource[slot] = i;
				reverseCost[slot] = links[l].cost;
			}
		}
	}

	// The largest strongly connected component.
	idList<int> componentSize;
	componentSize.AssureSize( numComponents, 0 );
	for ( int i = 0; i < numNodes; i++ ) {
		componentSize[nodes[i].component]++;
	}
	int bulk = 0;
	for ( int c = 1; c < numComponents; c++ ) {
		if ( componentSize[c] > componentSize[bulk] ) {
			bulk = c;
		}
	}
	if ( componentSize[bulk] < 64 ) {
		reverseOffset.Clear();
		reverseSource.Clear();
		reverseCost.Clear();
		return;
	}

	int seed = -1;
	for ( int i = 0; i < numNodes && seed < 0; i++ ) {
		if ( nodes[i].component == bulk ) {
			seed = i;
		}
	}

	idList<float> separation;
	separation.SetNum( numNodes, false );
	Dijkstra( seed, false, separation.Ptr() );

	// The first landmark is the far end of the map from an arbitrary member.
	int candidate = -1;
	float farthest = -1.0f;
	for ( int i = 0; i < numNodes; i++ ) {
		if ( nodes[i].component == bulk && separation[i] < NAV_UNREACHED && separation[i] > farthest ) {
			farthest = separation[i];
			candidate = i;
		}
	}
	for ( int i = 0; i < numNodes; i++ ) {
		separation[i] = NAV_INFINITE_COST;
	}

	// Each node's costs to and from every landmark sit together in one row,
	// so a heuristic lookup reads one cache line for the node instead of one
	// per landmark and direction from tables a megabyte apart.
	landmarkCost.SetNum( NAV_LANDMARK_STRIDE * numNodes, false );
	idList<float> fromScratch;
	idList<float> toScratch;
	fromScratch.SetNum( numNodes, false );
	toScratch.SetNum( numNodes, false );

	while ( candidate >= 0 && numLandmarks < NAV_MAX_LANDMARKS ) {
		const float *from = fromScratch.Ptr();
		const float *to = toScratch.Ptr();

		Dijkstra( candidate, false, fromScratch.Ptr() );
		Dijkstra( candidate, true, toScratch.Ptr() );
		for ( int i = 0; i < numNodes; i++ ) {
			landmarkCost[i * NAV_LANDMARK_STRIDE + numLandmarks * 2]		= from[i];
			landmarkCost[i * NAV_LANDMARK_STRIDE + numLandmarks * 2 + 1]	= to[i];
		}
		landmarkNodes.Append( candidate );
		numLandmarks++;

		// Next: the member whose round trip to every chosen landmark is longest.
		candidate = -1;
		farthest = 0.0f;
		for ( int i = 0; i < numNodes; i++ ) {
			if ( nodes[i].component != bulk ) {
				continue;
			}
			separation[i] = Min( separation[i], from[i] + to[i] );
			if ( separation[i] > farthest ) {
				farthest = separation[i];
				candidate = i;
			}
		}
	}

	reverseOffset.Clear();
	reverseSource.Clear();
	reverseCost.Clear();
}

/*
================
rvNavMesh::Heuristic

Admissible and consistent lower bound on the route cost from node to goal:
the larger of scaled straight-line distance and every landmark's triangle
bound.  A landmark that can reach the node but not the goal, or that the goal
can reach but the node cannot, proves the node can never lead to the goal, and
the search drops it instead of flooding a dead-end pit.
================
*/
float rvNavMesh::Heuristic( int node, int goal ) const {
	float h = heuristicScale * ( nodes[goal].origin - nodes[node].origin ).Length();
	const float *nodeRow = landmarkCost.Ptr() + node * NAV_LANDMARK_STRIDE;
	const float *goalRow = landmarkCost.Ptr() + goal * NAV_LANDMARK_STRIDE;

	for ( int k = 0; k < numLandmarks; k++ ) {
		// cost( node -> goal ) >= cost( L -> goal ) - cost( L -> node )
		const float landmarkGoal = goalRow[k * 2];
		const float landmarkNode = nodeRow[k * 2];
		if ( landmarkGoal < NAV_UNREACHED ) {
			if ( landmarkNode < NAV_UNREACHED ) {
				h = Max( h, landmarkGoal - landmarkNode );
			}
		} else if ( landmarkNode < NAV_UNREACHED ) {
			return NAV_INFINITE_COST;
		}

		// cost( node -> goal ) >= cost( node -> L ) - cost( goal -> L )
		const float goalLandmark = goalRow[k * 2 + 1];
		const float nodeLandmark = nodeRow[k * 2 + 1];
		if ( goalLandmark < NAV_UNREACHED ) {
			if ( nodeLandmark >= NAV_UNREACHED ) {
				return NAV_INFINITE_COST;
			}
			h = Max( h, nodeLandmark - goalLandmark );
		}
	}

	return h;
}

/*
===============================================================================

	Queries

===============================================================================
*/

/*
================
rvNavMesh::RouteLowerBound
================
*/
float rvNavMesh::RouteLowerBound( int fromNode, int toNode ) const {
	if ( fromNode < 0 || fromNode >= nodes.Num() || toNode < 0 || toNode >= nodes.Num() ) {
		return NAV_INFINITE_COST;
	}
	if ( fromNode == toNode ) {
		return 0.0f;
	}
	if ( !NodeCanReach( fromNode, toNode ) ) {
		return NAV_INFINITE_COST;
	}
	return Heuristic( fromNode, toNode );
}

/*
================
rvNavMesh::FindNearestNode

Searches the grid cells around the point outwards, so the common case - the
point is standing on a node - costs one cell lookup.
================
*/
int rvNavMesh::FindNearestNode( const idVec3 &origin, float maxRadius, bool requireWalkable ) const {
	if ( nodes.Num() == 0 ) {
		return -1;
	}

	int gx, gy;
	CellCoords( origin, gx, gy );

	const int	maxRings	= (int)idMath::Ceil( maxRadius / cellSize );
	int			best		= -1;
	float		bestDistSqr = maxRadius * maxRadius;

	// Walkability probes are budgeted.  bestDistSqr only tightens on a SUCCESS,
	// so when nothing is walk-reachable the ring early-out below can never fire
	// and every node inside maxRadius gets a WalkableLine - which is two box
	// sweeps at minimum plus an interior SampleFloor per cell crossed.  That
	// failure path is not the rare one: the widest caller is the off-mesh
	// recovery search at 1536 units, and it runs precisely when the bot is
	// somewhere no node can reach, twice a second, for as long as it is stuck.
	// Rings are walked outwards, so the budget is spent nearest-first and the
	// answer in every success case is the one it would have found anyway.
	int probesLeft = requireWalkable ? NAV_MAX_WALKABLE_PROBES : nodes.Num();

	for ( int ring = 0; ring <= maxRings && probesLeft > 0; ring++ ) {
		for ( int dy = -ring; dy <= ring; dy++ ) {
			// Step across the shell of this ring rather than walking the whole
			// square and skipping the interior: the interior was covered by the
			// rings before it, and iterating it anyway turns an O(rings^2) job
			// into an O(rings^3) one.  That matters because the off-mesh
			// recovery search uses a large radius.
			const bool	edgeRow	= ( abs( dy ) == ring );
			const int	step	= ( edgeRow || ring == 0 ) ? 1 : ( ring * 2 );

			for ( int dx = -ring; dx <= ring; dx += step ) {
				const int key = CellKey( gx + dx, gy + dy );
				for ( int i = nodeHash.First( key ); i != -1; i = nodeHash.Next( i ) ) {
					if ( nodes[i].gx != gx + dx || nodes[i].gy != gy + dy ) {
						continue;
					}

					const float distSqr = ( nodes[i].origin - origin ).LengthSqr();
					if ( distSqr >= bestDistSqr ) {
						continue;
					}

					if ( requireWalkable ) {
						// A route never starts or ends on a pad or in a
						// teleporter: whoever stands there is already leaving.
						if ( nodes[i].flags & NAVNODE_TRANSPORT ) {
							continue;
						}
						if ( probesLeft <= 0 ) {
							continue;
						}
						probesLeft--;

						if ( !WalkableLine( origin, nodes[i].origin ) ) {
							continue;
						}
					}

					bestDistSqr	= distSqr;
					best		= i;
				}
			}
		}

		// A hit one ring in cannot be beaten by anything two rings out.
		if ( best != -1 && bestDistSqr <= ( ring * cellSize ) * ( ring * cellSize ) ) {
			break;
		}
	}

	return best;
}

/*
================
rvNavMesh::SnapNode
================
*/
int rvNavMesh::SnapNode( const idVec3 &point ) const {
	if ( nodes.Num() == 0 ) {
		return -1;
	}

	idVec3 routed = point;
	idVec3 projected;
	if ( GroundUnder( point, projected ) && point.z - projected.z > pm_stepsize.GetFloat() ) {
		routed = projected;
	}

	return FindNearestNode( routed, 256.0f, true );
}

/*
================
rvNavMesh::GroundUnder

Where the agent box comes down from this point: the surface under it, whether
or not a node could stand there.  Route endpoints in the air are projected
onto it before they snap.  SampleFloor demands standable ground, so a jump
over a stair edge, a ramp too steep to stand on or the lip of a ledge left
the endpoint where it was - forty units up - and no walk from there to any
node passes the slope bound, so the route failed for a bot or an enemy that
was merely mid-jump.
================
*/
bool rvNavMesh::GroundUnder( const idVec3 &point, idVec3 &ground ) const {
	trace_t trace;
	NavAgentTranslation( trace, point, point - idVec3( 0.0f, 0.0f, NAV_MAX_DROP ) );
	if ( trace.fraction >= 1.0f || trace.fraction <= 0.0f ) {
		return false;		// a bottomless drop, or a point already inside solid
	}

	ground = trace.endpos;
	return true;
}

/*
================
rvNavMesh::NodeForEntity
================
*/
int rvNavMesh::NodeForEntity( const idEntity *ent ) const {
	if ( !ent || nodes.Num() == 0 || !ent->GetPhysics() ||
		 ent->entityNumber < 0 || ent->entityNumber >= entitySnaps.Num() ) {
		return -1;
	}

	const int number = ent->entityNumber;
	const idVec3 &origin = ent->GetPhysics()->GetOrigin();
	navEntitySnap_t &snap = entitySnaps[number];

	// A snap is a probe of the live world as well as of the mesh: its lines
	// are swept against movers and loose objects where they stand right now.
	// A failure is therefore kept only for a while, or an item whose alcove a
	// plat happened to be parked across on the first decision tick stayed
	// invisible to every bot for the rest of the map.
	if ( snap.spawnId == gameLocal.spawnIds[number] &&
		 ( snap.origin - origin ).LengthSqr() <= 1.0f &&
		 ( snap.node >= 0 || gameLocal.time - snap.time < NAV_SNAP_RETRY_MSEC ) ) {
		return snap.node;
	}

	snap.spawnId = gameLocal.spawnIds[number];
	snap.origin = origin;
	snap.time = gameLocal.time;
	snap.node = SnapNode( origin );
	return snap.node;
}

/*
================
rvNavMesh::IsReachable
================
*/
bool rvNavMesh::IsReachable( const idVec3 &start, const idVec3 &goal ) const {
	if ( !NodeCanReach( SnapNode( start ), SnapNode( goal ) ) ) {
		return false;
	}

	rvNavPath path;
	return FindPath( start, goal, path );
}

/*
================
rvNavMesh::RandomReachablePoint

Candidates are drawn from the whole graph and filtered by the component
closure, which is exact and constant time, so a ledge or island bot still
finds the small set of nodes it can truly reach without walking every link of
the map first.  Each accepted draw is then proven by a real route, because
movers can make one branch temporarily unavailable.
================
*/
bool rvNavMesh::RandomReachablePoint( const idVec3 &origin, idVec3 &result, rvNavPath *pathOut, float maxImpactDelta ) const {
	if ( pathOut ) {
		pathOut->Clear();
	}

	if ( nodes.Num() < 2 ) {
		return false;
	}

	const int startNode = SnapNode( origin );
	if ( startNode == -1 ) {
		return false;
	}

	int attempts = 0;
	for ( int draw = 0; draw < 256 && attempts < NAV_MAX_ROUTE_REPAIRS; draw++ ) {
		// idRandom yields 15 bits; a large map has more nodes than that, and the
		// top of the node list would otherwise never be drawn.
		const unsigned int wide = ( (unsigned int)gameLocal.random.RandomInt() << 15 ) |
								  (unsigned int)gameLocal.random.RandomInt();
		const int candidate = (int)( wide % (unsigned int)nodes.Num() );
		if ( candidate == startNode || ( nodes[candidate].flags & NAVNODE_TRANSPORT ) ||
			 !NodeCanReach( startNode, candidate ) ) {
			continue;		// a pad or teleporter is never somewhere to stop
		}
		attempts++;

		rvNavPath candidatePath;
		if ( !FindPath( origin, nodes[candidate].origin, candidatePath, false, maxImpactDelta ) ) {
			continue;
		}

		result = nodes[candidate].origin;
		if ( pathOut ) {
			*pathOut = candidatePath;
		}
		return true;
	}

	return false;
}

/*
================
rvNavMesh::BuildCostField
================
*/
int rvNavMesh::BuildCostField( const idVec3 &origin, float maxCost, const idList<int> *targets ) const {
	fieldSerial = 0;

	const int startNode = SnapNode( origin );
	if ( startNode == -1 ) {
		return -1;
	}

	const double startTicks = idLib::sys->GetClockTicks();

	BeginSearch();
	heap.SetNum( 0, false );

	// With a target list the search ends once every target is priced: a bot
	// whose useful pickups are all nearby does not pay for the far side of
	// the map.
	int remaining = 0;
	if ( targets ) {
		for ( int i = 0; i < targets->Num(); i++ ) {
			const int target = ( *targets )[i];
			if ( target >= 0 && target < nodes.Num() && fieldTargetTag[target] != searchSerial ) {
				fieldTargetTag[target] = searchSerial;
				remaining++;
			}
		}
	}

	gScore[startNode] = 0.0f;
	openTag[startNode] = searchSerial;
	HeapPush( startNode, 0.0f );

	while ( heap.Num() ) {
		const int current = HeapPop();
		if ( closedTag[current] == searchSerial ) {
			continue;
		}
		if ( gScore[current] > maxCost ) {
			break;
		}
		closedTag[current] = searchSerial;
		stats.fieldNodes++;

		if ( targets && ( remaining == 0 ||
			 ( fieldTargetTag[current] == searchSerial && --remaining == 0 ) ) ) {
			break;
		}

		for ( int l = nodes[current].firstLink; l != -1; l = links[l].next ) {
			const int next = links[l].node;
			const float tentative = gScore[current] + links[l].cost;
			if ( tentative > maxCost ) {
				continue;
			}
			if ( openTag[next] == searchSerial && tentative >= gScore[next] ) {
				continue;
			}

			openTag[next] = searchSerial;
			gScore[next] = tentative;
			HeapPush( next, tentative );
		}
	}

	fieldSerial = searchSerial;
	fieldPriced = false;
	stats.fieldSearches++;
	const double fieldTicks = idLib::sys->GetClockTicks() - startTicks;
	stats.searchTicks += fieldTicks;
	stats.fieldTicks += fieldTicks;

	return startNode;
}

/*
================
rvNavMesh::PathCost

Exact route cost between two nodes: A* under the landmark bound, without the
corner list, live link validation or string pulling that FindPath adds.  Goal
choice only needs to know what each route would cost; FindPath proves the
winner.  NAV_INFINITE_COST when no route exists within maxCost.
================
*/
float rvNavMesh::PathCost( int startNode, int goalNode, float maxCost ) const {
	if ( startNode < 0 || startNode >= nodes.Num() || goalNode < 0 || goalNode >= nodes.Num() ) {
		return NAV_INFINITE_COST;
	}
	if ( startNode == goalNode ) {
		return 0.0f;
	}
	if ( !NodeCanReach( startNode, goalNode ) ) {
		return NAV_INFINITE_COST;
	}

	const float startHeuristic = Heuristic( startNode, goalNode );
	if ( startHeuristic >= NAV_UNREACHED || startHeuristic > maxCost ) {
		return NAV_INFINITE_COST;
	}

	BeginSearch();
	heap.SetNum( 0, false );

	gScore[startNode]	= 0.0f;
	fScore[startNode]	= startHeuristic;
	openTag[startNode]	= searchSerial;
	HeapPush( startNode, startHeuristic );

	while ( heap.Num() ) {
		const int current = HeapPop();
		if ( closedTag[current] == searchSerial ) {
			continue;
		}
		closedTag[current] = searchSerial;
		stats.fieldNodes++;

		if ( current == goalNode ) {
			return gScore[current];
		}

		for ( int l = nodes[current].firstLink; l != -1; l = links[l].next ) {
			const int next = links[l].node;
			const float tentative = gScore[current] + links[l].cost;
			if ( openTag[next] == searchSerial && tentative >= gScore[next] ) {
				continue;
			}

			const float h = ( openTag[next] == searchSerial ) ?
				fScore[next] - gScore[next] : Heuristic( next, goalNode );
			if ( h >= NAV_UNREACHED || tentative + h > maxCost ) {
				continue;
			}

			openTag[next]	= searchSerial;
			gScore[next]	= tentative;
			fScore[next]	= tentative + h;
			HeapPush( next, fScore[next] );
		}
	}

	return NAV_INFINITE_COST;
}

/*
================
NavInsertBest

Keeps the keepBest highest scores, best first.
================
*/
static void NavInsertBest( float *best, int &numBest, int keepBest, float score ) {
	int at = numBest;
	if ( numBest < keepBest ) {
		numBest++;
	} else if ( score <= best[keepBest - 1] ) {
		return;
	} else {
		at = keepBest - 1;
	}
	while ( at > 0 && best[at - 1] < score ) {
		best[at] = best[at - 1];
		at--;
	}
	best[at] = score;
}

/*
================
rvNavMesh::RankedSettle

Goal pricing's fallback past its work budget: one Dijkstra from startNode that
prices the candidates still in the running as it settles them, and stops as
soon as none of those left could enter the best few - the exact searches'
stop rule, applied to a single field.  A field that settled until every
candidate was priced cost more than the searches it replaced.
================
*/
void rvNavMesh::RankedSettle( int startNode, float maxCost, float startLeg, const navFieldTarget_t *targets,
							  const int *remaining, int numRemaining, bool ranked, int keepBest,
							  float *best, int &numBest, idList<int> &pricedNode, idList<float> &pricedCost ) const {
	static idList<bool> done;

	BeginSearch();
	heap.SetNum( 0, false );

	done.SetNum( Max( 0, numRemaining ), false );
	int unpriced = 0;
	float heaviest = 0.0f;			// the most an unpriced candidate weighs
	float lightestExtra = idMath::INFINITY;
	for ( int r = 0; r < numRemaining; r++ ) {
		const navFieldTarget_t &target = targets[remaining[r]];
		done[r] = false;
		fieldTargetTag[target.node] = searchSerial;
		unpriced++;
		heaviest = Max( heaviest, target.weight );
		lightestExtra = Min( lightestExtra, Max( 0.0f, target.extra ) );
	}

	gScore[startNode] = 0.0f;
	openTag[startNode] = searchSerial;
	HeapPush( startNode, 0.0f );

	while ( heap.Num() && unpriced > 0 ) {
		const int current = HeapPop();
		if ( closedTag[current] == searchSerial ) {
			continue;
		}
		if ( gScore[current] > maxCost ) {
			break;
		}
		closedTag[current] = searchSerial;
		stats.fieldNodes++;

		if ( fieldTargetTag[current] == searchSerial ) {
			bool pricedHere = false;
			for ( int r = 0; r < numRemaining; r++ ) {
				const navFieldTarget_t &target = targets[remaining[r]];
				if ( done[r] || target.node != current ) {
					continue;
				}
				done[r] = true;
				unpriced--;
				pricedHere = true;
				pricedNode.Append( current );
				pricedCost.Append( gScore[current] );
				NavInsertBest( best, numBest, keepBest, Min( target.minWeight, target.weight ) /
					Max( 1.0f, gScore[current] + startLeg + Max( 0.0f, target.extra ) ) );
			}
			if ( pricedHere ) {
				heaviest = 0.0f;
				lightestExtra = idMath::INFINITY;
				for ( int r = 0; r < numRemaining; r++ ) {
					if ( !done[r] ) {
						heaviest = Max( heaviest, targets[remaining[r]].weight );
						lightestExtra = Min( lightestExtra, Max( 0.0f, targets[remaining[r]].extra ) );
					}
				}
			}
		}

		// Nothing unpriced can score more than its weight over a route at
		// least as long as the ground settled so far.
		if ( ranked && numBest >= keepBest && unpriced > 0 &&
			 best[keepBest - 1] >= heaviest / Max( 1.0f, gScore[current] + startLeg + lightestExtra ) ) {
			break;
		}

		for ( int l = nodes[current].firstLink; l != -1; l = links[l].next ) {
			const int next = links[l].node;
			const float tentative = gScore[current] + links[l].cost;
			if ( tentative > maxCost ) {
				continue;
			}
			if ( openTag[next] == searchSerial && tentative >= gScore[next] ) {
				continue;
			}

			openTag[next] = searchSerial;
			gScore[next] = tentative;
			HeapPush( next, tentative );
		}
	}
}

/*
================
rvNavMesh::BuildCostField

Ranked form: branch and bound over the targets instead of a field.  The
landmark heuristic gives every target a lower bound on its route cost, so
weight / ( bound + startLeg + extra ) is the most it can score.  Targets are
priced exactly, best bound first, until the keepBest-th best exact score is
at least the bound of everything still unpriced - nothing left can then enter
the best few, and the caller's top few are exact.  A Dijkstra field that
proved the same thing settled a third of q4dm5 per decision and was four
fifths of all bot navigation time; a handful of landmark-guided searches
expand a few hundred nodes.

Where the bounds are weak - q4dm1's long detours around pads and drops - each
exact search re-expands the same ground, and a decision cost twice the map.
Past a work budget the candidates still unpriced are priced from one bounded
field instead, so a decision never costs much more than a field would have.
================
*/
int rvNavMesh::BuildCostField( const idVec3 &origin, float maxCost, const navFieldTarget_t *targets,
							   int numTargets, int keepBest ) const {
	static const int NAV_FIELD_MAX_KEEP = 8;
	// Search scratch reused across calls: routing is single threaded.
	static idList<int> order;
	static idList<float> bound;
	static idList<int> pricedNode;
	static idList<float> pricedCost;

	fieldSerial = 0;

	const int startNode = SnapNode( origin );
	if ( startNode == -1 ) {
		return -1;
	}

	const double startTicks = idLib::sys->GetClockTicks();

	keepBest = idMath::ClampInt( 1, NAV_FIELD_MAX_KEEP, keepBest );
	const float startLeg = ( nodes[startNode].origin - origin ).Length();

	order.SetNum( 0, false );
	bound.SetNum( Max( 0, numTargets ), false );
	pricedNode.SetNum( 0, false );
	pricedCost.SetNum( 0, false );
	bool ranked = true;
	for ( int i = 0; i < numTargets; i++ ) {
		const int node = targets[i].node;
		if ( node < 0 || node >= nodes.Num() ) {
			continue;		// nothing to price
		}
		if ( targets[i].weight <= 0.0f ) {
			ranked = false;
		}

		// A target the landmarks prove unreachable, or out of range, stays
		// unpriced without a search.
		const float lower = ( node == startNode ) ? 0.0f : Heuristic( startNode, node );
		if ( lower >= NAV_UNREACHED || lower > maxCost ) {
			continue;
		}
		bound[i] = ( targets[i].weight > 0.0f ) ?
			targets[i].weight / Max( 1.0f, lower + startLeg + Max( 0.0f, targets[i].extra ) ) : idMath::INFINITY;

		// Insertion by descending bound; target lists are at most a few hundred.
		int at = order.Num();
		order.Append( i );
		while ( at > 0 && bound[order[at - 1]] < bound[i] ) {
			order[at] = order[at - 1];
			at--;
		}
		order[at] = i;
	}

	float best[NAV_FIELD_MAX_KEEP];
	int numBest = 0;
	const int budget = Max( NAV_PRICING_MIN_BUDGET, nodes.Num() / 8 );
	int spent = 0;
	for ( int k = 0; k < order.Num(); k++ ) {
		const int i = order[k];
		if ( ranked && numBest >= keepBest && best[keepBest - 1] >= bound[i] ) {
			break;		// nothing still unpriced can enter the best few
		}

		const int node = targets[i].node;
		float cost = -1.0f;
		for ( int p = 0; p < pricedNode.Num(); p++ ) {
			if ( pricedNode[p] == node ) {
				cost = pricedCost[p];
				break;
			}
		}
		if ( cost < 0.0f ) {
			if ( spent > budget ) {
				// Everything still in the running, priced and ranked by one field.
				static idList<int> rest;
				rest.SetNum( 0, false );
				for ( int r = k; r < order.Num(); r++ ) {
					rest.Append( order[r] );
				}
				RankedSettle( startNode, maxCost, startLeg, targets, rest.Ptr(), rest.Num(), ranked, keepBest,
							  best, numBest, pricedNode, pricedCost );
				break;
			}
			const int expandedBefore = stats.fieldNodes;
			cost = PathCost( startNode, node, maxCost );
			spent += stats.fieldNodes - expandedBefore;
			pricedNode.Append( node );
			pricedCost.Append( cost );
		}
		if ( cost >= NAV_UNREACHED ) {
			continue;
		}

		// The least this target can be worth, so the stop rule never overrates
		// what is already priced.
		NavInsertBest( best, numBest, keepBest, Min( targets[i].minWeight, targets[i].weight ) /
			Max( 1.0f, cost + startLeg + Max( 0.0f, targets[i].extra ) ) );
	}

	// Publish under a serial of its own: CostFieldDistance answers from this
	// table until the next search of any kind.
	BeginSearch();
	fieldSerial = searchSerial;
	fieldPriced = true;
	for ( int p = 0; p < pricedNode.Num(); p++ ) {
		fieldPricedTag[pricedNode[p]] = fieldSerial;
		fieldPricedCost[pricedNode[p]] = pricedCost[p];
	}

	stats.fieldSearches++;
	const double fieldTicks = idLib::sys->GetClockTicks() - startTicks;
	stats.searchTicks += fieldTicks;
	stats.fieldTicks += fieldTicks;

	return startNode;
}

/*
================
rvNavMesh::CostFieldDistance
================
*/
float rvNavMesh::CostFieldDistance( int node ) const {
	if ( fieldSerial == 0 || fieldSerial != searchSerial || node < 0 || node >= nodes.Num() ) {
		return idMath::INFINITY;
	}

	if ( fieldPriced ) {
		return ( fieldPricedTag[node] == fieldSerial && fieldPricedCost[node] < NAV_UNREACHED ) ?
			fieldPricedCost[node] : idMath::INFINITY;
	}

	return ( closedTag[node] == searchSerial ) ? gScore[node] : idMath::INFINITY;
}

/*
================
rvNavMesh::ResetStats
================
*/
void rvNavMesh::ResetStats( void ) const {
	memset( &stats, 0, sizeof( stats ) );
}

/*
================
NavSweepNearlyClear

A sweep stopped in its last couple of units arrived.  A player standing flush
against a wall is a legal endpoint, and the clip epsilon halts a box sweep a
hair short of it: a bot following a team-mate who stopped in a corner had
every route to them refused at 99%.
================
*/
static bool NavSweepNearlyClear( const trace_t &trace, const idVec3 &start, const idVec3 &end ) {
	return ( 1.0f - trace.fraction ) * ( end - start ).Length() <= NAV_SWEEP_SLACK;
}

/*
================
NavWalkFailure
================
*/
static bool NavWalkFailure( const char **failure, const char *reason ) {
	if ( failure ) {
		*failure = reason;
	}
	return false;
}

/*
================
NavWalkBlockedReason

Diagnostics only: what each sweep met, so a refused snap in a bot_debug log
can be traced to the brush or entity at fault.
================
*/
static const char *NavWalkBlockedReason( const trace_t &flat, const trace_t &raised ) {
	static char blocked[256];
	const idEntity *flatHit = ( flat.c.entityNum >= 0 && flat.c.entityNum < MAX_GENTITIES ) ?
		gameLocal.entities[flat.c.entityNum] : NULL;
	const idEntity *raisedHit = ( raised.c.entityNum >= 0 && raised.c.entityNum < MAX_GENTITIES ) ?
		gameLocal.entities[raised.c.entityNum] : NULL;
	idStr::snPrintf( blocked, sizeof( blocked ),
					 "blocked: flat %.2f by %s normal %s, raised %.2f by %s normal %s",
					 flat.fraction, flatHit ? flatHit->GetName() : "?", flat.c.normal.ToString( 2 ),
					 raised.fraction, raisedHit ? raisedHit->GetName() : "?", raised.c.normal.ToString( 2 ) );
	return blocked;
}

/*
================
rvNavMesh::WalkableLine

String-pull test.  A straight box sweep proves nothing is in the way, but it
would also happily cut the corner across a pit, so the ground underneath is
sampled as well.

The sweep is a straight line between two lifted endpoints, so on a line that
climbs it rises with the line and clears the top of a riser the player on
the floor walks into: on q4ctf1 a chord from the low floor to a node on the
lip of a 64-unit platform, or on the side of a ramp, passed both sweeps and
every "floats over a drop" sample, and the bot pressed against the wall under
its corner until the stuck detector threw the route away.  The floor samples
along the line say where the ground really rises: where consecutive samples
- the endpoints included - climb more than a step, the ground between them
is sampled too, and a walk climbs at most a step on either side of it.  A
riser taller than a step fails that however the samples fall on it; a
45-degree slope or a stair of player-height steps passes.
================
*/
bool rvNavMesh::WalkableLine( const idVec3 &from, const idVec3 &to, const char **failure ) const {
	trace_t		trace;
	const float	stepSize = pm_stepsize.GetFloat();

	// A walk climbs or descends at most about a step per step: anything steeper
	// between the two points is another floor, however clear the straight sweep
	// between them happens to be.  Without this a bot under a balcony snapped
	// its route onto the balcony through the open air beside it.
	idVec3 flat = to - from;
	flat.z = 0.0f;
	if ( idMath::Fabs( to.z - from.z ) > stepSize + flat.Length() ) {
		return NavWalkFailure( failure, "steeper than a walk" );
	}

	if ( LineEntersTransport( from, to ) ) {
		return NavWalkFailure( failure, "steps onto a transport" );
	}

	idVec3 start = from + idVec3( 0.0f, 0.0f, NAV_GROUND_OFFSET );
	idVec3 end   = to   + idVec3( 0.0f, 0.0f, NAV_GROUND_OFFSET );
	float probeLift = NAV_GROUND_OFFSET;

	// Most shortcuts are ordinary floor movement.  Test at standing height
	// first so a legal low corridor is not rejected merely because a
	// step-height-raised player hull would overlap its ceiling.
	if ( !NavAgentTranslation( trace, start, end ) && !NavSweepNearlyClear( trace, start, end ) ) {
		const trace_t flat = trace;

		// A flat sweep can hit a stair that player physics will step over.  The
		// raised alternative is only needed in that case.
		start = from + idVec3( 0.0f, 0.0f, stepSize );
		end   = to   + idVec3( 0.0f, 0.0f, stepSize );
		probeLift = stepSize;

		if ( !NavAgentTranslation( trace, start, end ) && !NavSweepNearlyClear( trace, start, end ) ) {
			return NavWalkFailure( failure, failure ? NavWalkBlockedReason( flat, trace ) : "blocked" );
		}
	}

	const idVec3	delta	= to - from;
	const float		length	= delta.Length();
	const int		samples	= ( length > idMath::FLOAT_EPSILON )
								? Max( 2, (int)idMath::Ceil( length / cellSize ) )
								: 1;

	// Pain floor the graph priced at 384 a cell is not free on a shortcut
	// either, unless the walk starts and ends in it already.
	const bool endsInHazard = hazards.Num() > 0 && HazardAtFloor( from ) >= 0 && HazardAtFloor( to ) >= 0;
	// Ground lower than both ends by more than a step is a pit between them:
	// the graph classes the way in as a drop and the way out as a jump, each
	// with its own proof and penalty, and a chord across a sunken panel or a
	// drain walked the bot into it on every pass.  The dip tolerance below is
	// for a line descending toward its lower end, not for a hole on the way.
	const float pitFloor = Min( from.z, to.z ) - stepSize;
	// Where the ground rises between two samples by more than a step, it has
	// to be a slope or a stair, not a riser: the sample between them may then
	// climb at most a step on either side.  Samples are taken with the
	// standing hull, so a sample catches a step a hull-width early, as the
	// player's hull does, and a 45-degree slope climbs twelve units in the
	// twelve between the halves.
	float previousFloor = from.z;
	idVec3 previousPoint = from;
	// A support box's width out to either side of the line: where the ground
	// beside each interior sample has to be as well.
	idVec3 beside( -delta.y, delta.x, 0.0f );
	beside.Normalize();
	beside *= navSupportBounds[1].x;

	for ( int i = 1; i <= samples; i++ ) {
		const float	frac  = (float)i / (float)samples;
		const idVec3	expected = from + delta * frac;
		idVec3		probe = expected + idVec3( 0.0f, 0.0f, probeLift );
		idVec3		floor;

		if ( i == samples ) {
			// The end is a floor sample too: a corner on a lip above the last
			// floor the line crossed is reached by a climb, not a walk.
			floor = to;
		} else {
			if ( !SampleFloor( probe, probeLift + NAV_SMOOTH_MAX_DIP, floor ) ) {
				return NavWalkFailure( failure, "no floor under the line" );
			}

			// The straight line must stay close to the ground it crosses, or the
			// shortcut is really a fall.
			if ( expected.z - floor.z > NAV_SMOOTH_MAX_DIP ) {
				return NavWalkFailure( failure, "floats over a drop" );
			}

			if ( floor.z < pitFloor ) {
				return NavWalkFailure( failure, "crosses a pit" );
			}

			// A hull half over an edge still finds the floor under its other
			// half, so a line along a rim - q4ctf1's platform edges and q4dz1's
			// catwalk, each a column of nodes four units past the edge - walks
			// the whole way with the feet hanging over the drop, and any
			// lateral error walks the bot off it.  The ground a support box's
			// width to either side of the line has to be there as well.
			if ( NavDropBeside( floor, beside ) || NavDropBeside( floor, -beside ) ) {
				return NavWalkFailure( failure, "hangs over an edge" );
			}
		}

		if ( floor.z - previousFloor > stepSize ) {
			const idVec3 between = ( previousPoint + expected ) * 0.5f;
			idVec3 betweenFloor;
			if ( !SampleFloor( between + idVec3( 0.0f, 0.0f, probeLift ), probeLift + NAV_SMOOTH_MAX_DIP, betweenFloor ) ||
				 betweenFloor.z - previousFloor > stepSize + NAV_WALK_RISE_SLACK ||
				 floor.z - betweenFloor.z > stepSize + NAV_WALK_RISE_SLACK ) {
				return NavWalkFailure( failure, ( i == samples ) ? "rises more than a step onto its end"
																 : "rises more than a step between samples" );
			}
		}
		previousFloor = floor.z;
		previousPoint = expected;
		if ( i == samples ) {
			break;
		}

		// A dip the agent tolerates can still be a shallow kill pit or a lava
		// channel the graph itself refused; a shortcut must not reintroduce it.
		if ( IsLethalFloor( floor ) ) {
			return NavWalkFailure( failure, "crosses lethal floor" );
		}
		if ( !endsInHazard && hazards.Num() > 0 && HazardAtFloor( floor ) >= 0 ) {
			return NavWalkFailure( failure, "crosses pain floor" );
		}
	}

	return true;
}

/*
================
rvNavMesh::LegTouchesGoal

Sweeps the agent from the last corner toward a goal it could not reach
exactly.  When the box stops, on floor, close enough that it overlaps a box
standing at the goal, the stopping point is where the route ends.
================
*/
bool rvNavMesh::LegTouchesGoal( const idVec3 &from, const idVec3 &goal, idVec3 &touch ) const {
	idVec3 flat = goal - from;
	flat.z = 0.0f;
	if ( flat.LengthSqr() > Square( NAV_TRANSPORT_GOAL_REACH ) ||
		 idMath::Fabs( goal.z - from.z ) > pm_stepsize.GetFloat() ) {
		return false;
	}

	trace_t trace;
	const idVec3 lift( 0.0f, 0.0f, NAV_GROUND_OFFSET );
	NavAgentTranslation( trace, from + lift, goal + lift );

	idVec3 left = goal - trace.endpos;
	left.z = 0.0f;
	if ( left.LengthSqr() > Square( NAV_GOAL_TOUCH_REACH ) ) {
		return false;
	}

	idVec3 floor;
	if ( !SampleFloor( trace.endpos + idVec3( 0.0f, 0.0f, pm_stepsize.GetFloat() ),
					   pm_stepsize.GetFloat() + NAV_SMOOTH_MAX_DIP, floor ) || IsLethalFloor( floor ) ) {
		return false;
	}

	touch = floor;
	return true;
}

/*
================
rvNavMesh::IsLethalFloor

The cheap form of IsHazardous for the inner loops: hazard volumes are a short
list of bounds, and the liquid point query only runs on a map where flood fill
actually met lava or slime.
================
*/
bool rvNavMesh::IsLethalFloor( const idVec3 &floor ) const {
	const int hazard = HazardAtFloor( floor );
	if ( hazard >= 0 && hazards[hazard].lethal ) {
		return true;
	}

	return lethalLiquidSeen && NavPointInLethalLiquid( floor );
}

/*
================
rvNavMesh::FindLink
================
*/
int rvNavMesh::FindLink( int fromNode, int toNode ) const {
	if ( fromNode < 0 || fromNode >= nodes.Num() ) {
		return -1;
	}

	for ( int l = nodes[fromNode].firstLink; l != -1; l = links[l].next ) {
		if ( links[l].node == toNode ) {
			return l;
		}
	}

	return -1;
}

/*
================
rvNavMesh::GatherDynamicSolids
================
*/
void rvNavMesh::GatherDynamicSolids( void ) {
	dynamicSolids.Clear();
	dynamicSolidSpawnIds.Clear();

	for ( idEntity *ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		if ( !NavIsDynamicSolid( ent ) || !ent->GetPhysics() ||
			 !( ent->GetPhysics()->GetContents() & NAV_CLIP_MASK ) ) {
			continue;
		}
		dynamicSolids.Append( ent->entityNumber );
		dynamicSolidSpawnIds.Append( gameLocal.spawnIds[ent->entityNumber] );
	}
}

/*
================
rvNavMesh::DynamicSolidNear

Whether any recorded dynamic solid currently overlaps these bounds.  Most maps
have none at all, and most links are nowhere near the ones that exist, so this
spares nearly every route the collision sweeps of LinkTraversable.
================
*/
bool rvNavMesh::DynamicSolidNear( const idBounds &bounds ) const {
	for ( int i = 0; i < dynamicSolids.Num(); i++ ) {
		const int number = dynamicSolids[i];
		idEntity *ent = gameLocal.entities[number];
		if ( !ent || gameLocal.spawnIds[number] != dynamicSolidSpawnIds[i] || !ent->GetPhysics() ) {
			continue;
		}
		if ( ent->GetPhysics()->GetAbsBounds().IntersectsBounds( bounds ) ) {
			return true;
		}
	}
	return false;
}

/*
================
rvNavMesh::DynamicTranslation

The link-shape sweep against entities only, where only a dynamic solid that
is not a walk-activated door may block.  A static entity in the way was there
when generation proved the link through it, so it is passed and the sweep
repeated; a proof that would need more passes than the trace interface allows
is accepted, because everything left in the way was there at build time too.
================
*/
bool rvNavMesh::DynamicTranslation( trace_t &trace, const idVec3 &start, const idVec3 &end ) const {
	// Sweep against the recorded dynamic solids and nothing else.  The old
	// form swept every solid entity and passed the static ones it met, two at
	// most: the bot's own body spent the first pass on the very first link,
	// and a third static blocker ended the proof as "clear" with the mover
	// beyond it untested.  Only these entities can block a link the build
	// proved, so only they are asked.
	trace.fraction = 1.0f;
	trace.endpos = end;
	trace.c.entityNum = ENTITYNUM_NONE;

	idBounds swept;
	swept.FromBoundsTranslation( navAgentBounds, start, mat3_identity, end - start );

	for ( int i = 0; i < dynamicSolids.Num(); i++ ) {
		const int number = dynamicSolids[i];
		idEntity *ent = gameLocal.entities[number];
		if ( !ent || gameLocal.spawnIds[number] != dynamicSolidSpawnIds[i] || !ent->GetPhysics() ||
			 NavDoorIsWalkActivated( ent ) ) {
			continue;
		}
		idPhysics *physics = ent->GetPhysics();
		if ( !physics->GetAbsBounds().IntersectsBounds( swept ) ) {
			continue;
		}

		for ( int c = 0; c < physics->GetNumClipModels(); c++ ) {
			const idClipModel *clip = physics->GetClipModel( c );
			if ( !clip || !clip->GetCollisionModel() || !( clip->GetContents() & NAV_CLIP_MASK ) ) {
				continue;
			}

			trace_t hit;
			collisionModelManager->Translation( &hit, start, end, &navAgentTrace, mat3_identity, NAV_CLIP_MASK,
												clip->GetCollisionModel(), clip->GetOrigin(), clip->GetAxis() );
			if ( hit.fraction < trace.fraction ) {
				trace = hit;
				trace.c.entityNum = number;
				if ( trace.fraction <= 0.0f ) {
					return false;
				}
			}
		}
	}

	return trace.fraction >= 1.0f;
}

/*
================
rvNavMesh::LinkTraversable

Generation records the map's topology, but solid movers can change after the
mesh is built.  Validate only the links on a selected route (rather than every
edge A* examines) and let the caller exclude any stale link before searching
again.  Ordinary doors remain usable because approaching them activates them.
================
*/
bool rvNavMesh::LinkTraversable( int fromNode, int linkIndex ) const {
	if ( fromNode < 0 || fromNode >= nodes.Num() ||
		 linkIndex < 0 || linkIndex >= links.Num() ) {
		return false;
	}

	const navLink_t &link = links[linkIndex];
	if ( link.node < 0 || link.node >= nodes.Num() ) {
		return false;
	}

	switch ( link.travelType ) {
		case NAVTRAVEL_WALK:
		case NAVTRAVEL_DROP:
		case NAVTRAVEL_JUMP: {
			// The static world and traversal type were proven when this graph edge
			// was generated.  Only dynamic solid entities can make that proof stale.
			//
			// Reproduce the SHAPE the generator proved - lift, cross at that
			// height, then drop - instead of sweeping the straight chord between
			// the two node origins.  That chord is not a movement any player
			// makes, and for a drop or a jump it runs diagonally through the very
			// surface being left or landed on.  Harmless while the trace also
			// sees the world, because the world was proven; fatal here, because
			// this trace sees ONLY entities, so a ledge, crate or lift platform
			// that happens to be a brush model blocks its own link on every
			// single route and A* pays a full repair flood to route around ground
			// the bot can plainly stand on.
			const idVec3	fromFloor	= nodes[fromNode].origin;
			const idVec3	toFloor		= nodes[link.node].origin;
			const float		rise		= Max( 0.0f, toFloor.z - fromFloor.z );
			trace_t			trace;

			// Only something that moved since the build can invalidate the
			// proof, and nothing that moves is anywhere near most links.
			if ( dynamicSolids.Num() == 0 ) {
				return true;
			}
			idBounds swept;
			swept.Clear();
			swept.AddPoint( fromFloor );
			swept.AddPoint( toFloor );
			const idBounds &agent = GetAgentBounds();
			swept[0] += agent[0];
			swept[1] += agent[1];
			swept[1].z += pm_jumpheight.GetFloat() + NAV_GROUND_OFFSET;
			if ( !DynamicSolidNear( swept ) ) {
				return true;
			}

			idVec3 start = fromFloor;
			start.z += NAV_GROUND_OFFSET;

			// A jump commits to the full height whatever the rise; a step only
			// lifts as far as it has to, so a flat walk is not failed by a low
			// ceiling it passes under perfectly well.
			const float lift = ( link.travelType == NAVTRAVEL_JUMP ) ?
				pm_jumpheight.GetFloat() : Min( rise, pm_stepsize.GetFloat() );

			if ( lift > 0.0f ) {
				idVec3 lifted = start;
				lifted.z = fromFloor.z + NAV_GROUND_OFFSET + lift;

				if ( !DynamicTranslation( trace, start, lifted ) ) {
					return false;
				}
				start = lifted;
			}

			idVec3 across = toFloor;
			across.z = start.z;

			if ( !DynamicTranslation( trace, start, across ) ) {
				return false;
			}

			// Then down onto the destination floor.  This is the whole fall for a
			// drop, and nothing at all when the crossing already ended level.
			idVec3 down = across;
			down.z = toFloor.z + NAV_GROUND_OFFSET;

			if ( down.z >= across.z ) {
				return true;
			}

			return DynamicTranslation( trace, across, down );
		}

		case NAVTRAVEL_JUMPPAD:
		case NAVTRAVEL_TELEPORT: {
			if ( link.actionEntityNum < 0 || link.actionEntityNum >= MAX_GENTITIES ) {
				return false;
			}

			idEntity *action = gameLocal.entities[link.actionEntityNum];
			if ( !action || action->targets.Num() < 1 || !action->targets[0].GetEntity() ) {
				return false;
			}

			if ( link.travelType == NAVTRAVEL_JUMPPAD ) {
				if ( !action->IsType( rvJumpPad::GetClassType() ) ||
					 !action->IsActive( TH_THINK ) ) {
					return false;
				}
			} else if ( !action->IsType( idTrigger_Multi::GetClassType() ) ||
						!action->targets[0].GetEntity()->IsType( idPlayerStart::GetClassType() ) ) {
				return false;
			}

			idBounds activationBounds;
			return NavTravelVolumeBounds( action, activationBounds );
		}

		default:
			return false;
	}
}

/*
================
rvNavMesh::LinkTravelType

How a bot has to get from one node to the next.  Nodes that are not directly
linked - the result of a string pull - are walked.
================
*/
int rvNavMesh::LinkTravelType( int fromNode, int toNode ) const {
	const int link = FindLink( fromNode, toNode );
	return ( link != -1 ) ? links[link].travelType : NAVTRAVEL_WALK;
}

/*
================
rvNavMesh::LinkActionEntity

Volume traversal has to retain the pad or trigger that owns the graph edge.
The bot uses its live bounds to enter the volume before steering toward the
remote exit.
================
*/
int rvNavMesh::LinkActionEntity( int fromNode, int toNode ) const {
	const int link = FindLink( fromNode, toNode );
	return ( link != -1 ) ? links[link].actionEntityNum : -1;
}

/*
================
rvNavMesh::StringPull

Turns the cell-by-cell node chain into the small number of corners a bot
actually has to steer through.  Only walk links are ever skipped: a jump pad or
a teleporter has to be entered deliberately, so those corners always survive.
================
*/
void rvNavMesh::StringPull( const idList<int> &nodePath, const idVec3 &goal, rvNavPath &path ) const {
	int i = 0;

	path.Clear();

	while ( i < nodePath.Num() - 1 ) {
		// How far ahead smoothing is even allowed to look: never past a link
		// that has to be travelled deliberately.
		int limit = Min( i + NAV_SMOOTH_LOOKAHEAD, nodePath.Num() - 1 );
		int immediateTravel = NAVTRAVEL_WALK;

		for ( int j = i + 1; j <= limit; j++ ) {
			const int travelType = LinkTravelType( nodePath[j - 1], nodePath[j] );
			if ( travelType != NAVTRAVEL_WALK ) {
				limit = j - 1;
				if ( j == i + 1 ) {
					immediateTravel = travelType;
				}
				break;
			}
		}

		// A mature reachability carries both its entry and exit.  If the route
		// begins with an action (or resumes immediately after one), preserve the
		// source explicitly before its destination.  The bot can then align
		// with a jump takeoff or enter a pad/teleporter volume instead of aiming
		// directly at a remote endpoint.
		if ( immediateTravel != NAVTRAVEL_WALK ) {
			const idVec3 &source = nodes[nodePath[i]].origin;

			// A running jump across a gap needs the bot moving at speed along
			// the jump line when it leaves the ground, not arriving at the edge
			// from the side, and so does a jump up more than two steps: a
			// 47-unit crate is within reach of a 48-unit jump only from a run.
			// Put a run-up corner behind the takeoff when both legs to it are
			// plain floor.
			if ( immediateTravel == NAVTRAVEL_JUMP ) {
				idVec3 across = nodes[nodePath[i + 1]].origin - source;
				across.z = 0.0f;
				const float rise = nodes[nodePath[i + 1]].origin.z - source.z;
				if ( across.Normalize() > cellSize * 1.6f || rise > pm_stepsize.GetFloat() * 2.0f ) {
					const idVec3 runup = source - across * NAV_GAP_RUNUP;
					const idVec3 &before = ( path.Num() > 0 ) ? path[path.Num() - 1].origin : nodes[nodePath[0]].origin;
					if ( ( before - runup ).LengthSqr() > 1.0f &&
						 WalkableLine( before, runup ) && WalkableLine( runup, source ) ) {
						path.Append( runup, NAVTRAVEL_WALK );
					}
				}
			}

			if ( path.Num() == 0 ||
				 ( path[path.Num() - 1].origin - source ).LengthSqr() > 1.0f ) {
				path.Append( source, NAVTRAVEL_WALK );
			}

			path.Append( nodes[nodePath[i + 1]].origin, immediateTravel,
						 LinkActionEntity( nodePath[i], nodePath[i + 1] ) );
			i++;
			continue;
		}

		// Walk forward while the shortcut still holds.  Stopping at the first
		// failure keeps this to a handful of traces per corner.
		int furthest = i + 1;
		for ( int j = i + 2; j <= limit; j++ ) {
			if ( !WalkableLeg( nodes[nodePath[i]].origin, nodes[nodePath[j]].origin ) ) {
				break;
			}
			furthest = j;
		}

		path.Append( nodes[nodePath[furthest]].origin, LinkTravelType( nodePath[furthest - 1], nodePath[furthest] ) );
		i = furthest;
	}

	// Finish at the caller's actual goal rather than the node under it, but
	// only when walking straight there is safe.  The old empty-path shortcut
	// skipped this proof when start and goal snapped to the same node, which
	// could invent a walk through a wall or between stacked floors.
	const idVec3 &lastOrigin = ( path.Num() > 0 )
		? path[path.Num() - 1].origin
		: nodes[nodePath[0]].origin;

	if ( WalkableLeg( lastOrigin, goal ) ) {
		path.Append( goal, NAVTRAVEL_WALK );
	}
}

/*
================
rvNavMesh::WalkableLeg

A string-pulled leg between two route corners.  A bot counts a corner as
reached up to a corner's reach short of it or to either side, so the leg it
walks next starts that far off the proven line.  On level ground that is
nothing.  On a leg that climbs or descends more than a step it can be the
difference between the line and a wall: q4ctf1's lip nodes along the north
edge of its ramp stand on a sliver of the ramp, a step above the floor
beside them, and the proven line from one ran diagonally up the lip onto
the ramp - while the bot, a hull's width north of it on the floor, walked
into the ramp's side.  Such a leg is proven from either side of its corner
as well.
================
*/
bool rvNavMesh::WalkableLeg( const idVec3 &from, const idVec3 &to ) const {
	if ( !WalkableLine( from, to ) ) {
		return false;
	}
	if ( idMath::Fabs( to.z - from.z ) <= pm_stepsize.GetFloat() ) {
		return true;
	}
	idVec3 along = to - from;
	along.z = 0.0f;
	if ( along.Normalize() < idMath::FLOAT_EPSILON ) {
		return true;
	}
	const idVec3 side( -along.y * NAV_LEG_SIDE_SLACK, along.x * NAV_LEG_SIDE_SLACK, 0.0f );
	return WalkableLine( from + side, to ) && WalkableLine( from - side, to );
}

/*
================
rvNavMesh::BeginSearch

The serial-tagged scratch arrays avoid clearing tens of thousands of entries
for every query.  Reset them explicitly before the signed serial can wrap;
otherwise a very long-running server could inherit stale open/closed tags.
================
*/
void rvNavMesh::BeginSearch( void ) const {
	if ( searchSerial >= 0x7ffffffe ) {
		for ( int i = 0; i < openTag.Num(); i++ ) {
			openTag[i] = 0;
			closedTag[i] = 0;
		}
		for ( int i = 0; i < fieldTargetTag.Num(); i++ ) {
			fieldTargetTag[i] = 0;
			fieldPricedTag[i] = 0;
		}
		searchSerial = 1;
	} else {
		searchSerial++;
	}
}

/*
================
rvNavMesh::HeapPush
================
*/
void rvNavMesh::HeapPush( int node, float key ) const {
	navHeapEntry_t entry;

	entry.node	= node;
	entry.key	= key;

	int child = heap.Append( entry );

	while ( child > 0 ) {
		const int parent = ( child - 1 ) >> 1;

		if ( heap[parent].key <= heap[child].key ) {
			break;
		}

		const navHeapEntry_t swap = heap[parent];
		heap[parent] = heap[child];
		heap[child]  = swap;
		child = parent;
	}
}

/*
================
rvNavMesh::HeapPop
================
*/
int rvNavMesh::HeapPop( void ) const {
	const int top = heap[0].node;

	heap[0] = heap[heap.Num() - 1];
	heap.SetNum( heap.Num() - 1, false );

	int parent = 0;
	while ( true ) {
		const int left  = parent * 2 + 1;
		const int right = left + 1;
		int		  best  = parent;

		if ( left < heap.Num() && heap[left].key < heap[best].key ) {
			best = left;
		}
		if ( right < heap.Num() && heap[right].key < heap[best].key ) {
			best = right;
		}
		if ( best == parent ) {
			break;
		}

		const navHeapEntry_t swap = heap[parent];
		heap[parent] = heap[best];
		heap[best]   = swap;
		parent = best;
	}

	return top;
}

/*
================
rvNavMesh::FindPath

A* uses Euclidean distance scaled by the cheapest cost-to-distance ratio in the
graph.  This remains admissible and consistent for jump pads and teleporters,
without making every search on those maps a wide Dijkstra pass.

The generated graph describes topology, not a promise that every mover remains
where it was at build time.  After A* selects a route, only that small set of
edges is checked against live collision.  Blocked edges are excluded and A* is
retried, allowing a bot to choose another corridor without tracing every link
the search merely considered.
================
*/
bool rvNavMesh::FindPath( const idVec3 &start, const idVec3 &goal, rvNavPath &path, bool nearGoal, float maxImpactDelta ) const {
	path.Clear();

	if ( nodes.Num() == 0 ) {
		return false;
	}

	const double startTicks = idLib::sys->GetClockTicks();
	stats.pathSearches++;
	const bool found = FindPathInternal( start, goal, path, nearGoal, maxImpactDelta );
	if ( !found ) {
		stats.pathFailures++;
	}
	stats.searchTicks += idLib::sys->GetClockTicks() - startTicks;

	return found;
}

/*
================
rvNavMesh::FindPathInternal
================
*/
bool rvNavMesh::FindPathInternal( const idVec3 &start, const idVec3 &goal, rvNavPath &path, bool nearGoal, float maxImpactDelta ) const {
	idVec3 routeStart = start;
	idVec3 routeGoal = goal;

	// A player can legitimately request a route while airborne.  Preserve the
	// strict endpoint proof for grounded items and objectives, but route an
	// endpoint that is clearly more than one step above standable ground from or
	// to that landing point.  A grounded endpoint starts its hull sweep touching
	// the floor and therefore cannot project through it onto a stacked level.
	idVec3 projected;
	if ( GroundUnder( start, projected ) && start.z - projected.z > pm_stepsize.GetFloat() ) {
		routeStart = projected;
	}
	if ( GroundUnder( goal, projected ) && goal.z - projected.z > pm_stepsize.GetFloat() ) {
		routeGoal = projected;
	}

	const int startNode = FindNearestNode( routeStart, 256.0f, true );
	int goalNode = FindNearestNode( routeGoal, 256.0f, true );

	// Pursuit settles for the nearest node on the player's own floor when the
	// exact spot proves no walk - a tight diagonal corridor, the lip of a
	// ledge.  The chase ends close enough to see and to fight.
	if ( goalNode == -1 && nearGoal && startNode != -1 ) {
		const int nearest = FindNearestNode( routeGoal, NAV_NEAR_GOAL_REACH, false );
		if ( nearest != -1 && !( nodes[nearest].flags & NAVNODE_TRANSPORT ) &&
			 idMath::Fabs( nodes[nearest].origin.z - routeGoal.z ) <= NAV_SMOOTH_MAX_DIP ) {
			goalNode = nearest;
			routeGoal = nodes[nearest].origin;
		}
	}

	if ( startNode == -1 || goalNode == -1 ) {
		stats.snapFailures++;
		if ( bot_debug.GetInteger() >= 1 && gameLocal.time >= nextSnapReportTime ) {
			// Say why: the nearest node by distance alone, and the first test
			// the walk onto it failed.
			nextSnapReportTime = gameLocal.time + 1000;
			const idVec3 &point = ( startNode == -1 ) ? routeStart : routeGoal;
			const int nearest = FindNearestNode( point, 256.0f, false );
			const char *failure = "no node within 256";
			if ( nearest != -1 ) {
				failure = ( nodes[nearest].flags & NAVNODE_TRANSPORT ) ? "nearest is a transport" : "walkable";
				WalkableLine( point, nodes[nearest].origin, &failure );
			}
			gameLocal.Printf( "rvNavMesh: no node near route %s %s: %s%s%s\n",
							  ( startNode == -1 ) ? "start" : "goal", point.ToString( 0 ), failure,
							  ( nearest != -1 ) ? " to " : "",
							  ( nearest != -1 ) ? nodes[nearest].origin.ToString( 0 ) : "" );
		}
		return false;
	}
	// Exact directed reachability, in constant time.  An unreachable goal used
	// to cost a search that settled everything the start could reach.
	if ( !NodeCanReach( startNode, goalNode ) ) {
		stats.quickRejects++;
		return false;
	}

	if ( startNode == goalNode ) {
		// Same cell, but "same cell" is not the same as "nothing in the way" -
		// a pillar can sit between the two exact points.  Both endpoints snapped
		// to this node through individually walkable legs, so use the node as a
		// real corner when the direct shortcut is blocked.  Recheck the outbound
		// direction because WalkableLine's floor sampling is directional.
		if ( !WalkableLine( routeStart, routeGoal ) ) {
			const idVec3 &via = nodes[startNode].origin;
			if ( !WalkableLine( routeStart, via ) || !WalkableLine( via, routeGoal ) ) {
				return false;
			}

			if ( ( via - routeStart ).LengthSqr() > 1.0f &&
				 ( via - routeGoal ).LengthSqr() > 1.0f ) {
				path.Append( via, NAVTRAVEL_WALK );
			}
		}

		if ( path.Num() == 0 ||
			 ( path[path.Num() - 1].origin - routeGoal ).LengthSqr() > 1.0f ) {
			path.Append( routeGoal, NAVTRAVEL_WALK );
		}

		const float exactCost = path.LengthFrom( start );
		path.SetTravelCost( start, exactCost );
		path.routeStart = routeStart;
		return path.Num() > 0;
	}

	// One stamp per excluded link, rather than a list scanned for every edge
	// relaxed by every repair pass: a wide blocker rejects a link a pass, and
	// a failed flood relaxes every edge of the graph against the whole list.
	rejectSerial++;
	if ( linkRejectTag.Num() != links.Num() ) {
		linkRejectTag.SetNum( links.Num() );
		for ( int i = 0; i < links.Num(); i++ ) {
			linkRejectTag[i] = 0;
		}
	}

	const float startHeuristic = Heuristic( startNode, goalNode );
	if ( startHeuristic >= NAV_UNREACHED ) {
		stats.quickRejects++;
		return false;
	}

	for ( int repair = 0; repair <= NAV_MAX_ROUTE_REPAIRS; repair++ ) {
		if ( repair > 0 ) {
			stats.repairSearches++;
		}

		BeginSearch();
		heap.SetNum( 0, false );

		gScore[startNode]	= 0.0f;
		fScore[startNode]	= startHeuristic;
		cameFrom[startNode]	= -1;
		openTag[startNode]	= searchSerial;

		HeapPush( startNode, fScore[startNode] );

		bool found = false;
		while ( heap.Num() ) {
			const int current = HeapPop();

			// A stale copy of a node that has already been expanded.
			if ( closedTag[current] == searchSerial ) {
				continue;
			}
			closedTag[current] = searchSerial;
			stats.nodesExpanded++;

			if ( current == goalNode ) {
				found = true;
				break;
			}

			for ( int l = nodes[current].firstLink; l != -1; l = links[l].next ) {
				if ( linkRejectTag[l] == rejectSerial ) {
					continue;
				}

				const int next = links[l].node;
				const float tentative = gScore[current] + links[l].cost;

				if ( openTag[next] == searchSerial && tentative >= gScore[next] ) {
					continue;
				}

				// A node first seen this search gets its bound computed once;
				// a cheaper path to an open node reuses it.
				const float h = ( openTag[next] == searchSerial ) ?
					fScore[next] - gScore[next] : Heuristic( next, goalNode );
				if ( h >= NAV_UNREACHED ) {
					continue;	// proven unable to lead to the goal
				}

				openTag[next]	= searchSerial;
				gScore[next]	= tentative;
				fScore[next]	= tentative + h;
				cameFrom[next]	= current;

				HeapPush( next, fScore[next] );
			}
		}

		if ( !found ) {
			stats.searchFailures++;
			path.Clear();
			return false;
		}

		// Unwind, then reverse.
		idList<int> nodePath;
		nodePath.SetGranularity( 64 );

		for ( int node = goalNode; node != -1; node = cameFrom[node] ) {
			nodePath.Append( node );
		}

		for ( int i = 0, j = nodePath.Num() - 1; i < j; i++, j-- ) {
			const int swap = nodePath[i];
			nodePath[i] = nodePath[j];
			nodePath[j] = swap;
		}

		// Recheck only the selected route against the current collision world.
		// Add every stale edge found in this pass, so wide blockers usually need
		// one repair search rather than one search per blocked cell.
		bool routeValid = true;
		for ( int i = 0; i + 1 < nodePath.Num(); i++ ) {
			const int link = FindLink( nodePath[i], nodePath[i + 1] );
			if ( link == -1 ) {
				path.Clear();
				return false;
			}
			navCorner_t landing;
			landing.origin = nodes[nodePath[i + 1]].origin;
			landing.travelType = links[link].travelType;
			landing.actionEntityNum = links[link].actionEntityNum;
			if ( LinkTraversable( nodePath[i], link ) &&
				 LandingSurvivable( nodes[nodePath[i]].origin, landing, maxImpactDelta ) ) {
				continue;
			}

			linkRejectTag[link] = rejectSerial;
			routeValid = false;
		}

		if ( !routeValid ) {
			continue;
		}

		const float exactTravelCost =
			( routeStart - start ).Length() +
			( nodes[startNode].origin - routeStart ).Length() +
			gScore[goalNode] +
			( routeGoal - nodes[goalNode].origin ).Length();

		StringPull( nodePath, routeGoal, path );

		// The routed start (the exact origin or an airborne landing projection) was
		// proven against startNode when it was selected, but a later string pull can
		// replace that first node with a farther corner.  Two individually clear legs
		// around a wall do not prove their diagonal.  Put the start node back when
		// that final shortcut is not itself clear.
		if ( path.Num() > 0 && !WalkableLine( routeStart, path[0].origin ) ) {
			rvNavPath safeStartPath;
			safeStartPath.Append( nodes[startNode].origin, NAVTRAVEL_WALK );

			for ( int i = 0; i < path.Num(); i++ ) {
				safeStartPath.Append( path[i].origin, path[i].travelType,
									  path[i].actionEntityNum );
			}

			path = safeStartPath;
		}

		// StringPull deliberately refuses an unsafe final shortcut from the last
		// sampled node to the exact goal or projected landing point.  That refusal is
		// a failed route, not a useful partial path: handing it to goal selection
		// makes the bot arrive at the wall or wrong floor and immediately choose the
		// same target again.
		// The one exception is a goal standing in the reach of a pad or a
		// teleporter - an item beside one, an enemy at its edge.  Nobody can
		// stand exactly there without being carried off, so the node nearest it
		// is as close as any route goes, and arriving there is arriving.
		if ( path.Num() > 0 &&
			 ( path[path.Num() - 1].origin - routeGoal ).LengthSqr() > 1.0f &&
			 ( path[path.Num() - 1].origin - routeGoal ).LengthSqr() <= Square( NAV_TRANSPORT_GOAL_REACH ) ) {
			if ( TransportAtFloor( routeGoal ) >= 0 ) {
				path.SetTravelCost( start, exactTravelCost );
				path.routeStart = routeStart;
				return true;
			}
		}

		// Two refusals are not real.  The goal's own snap proved the walk from
		// the goal to its node, and a sweep the other way can stall on a
		// slope's or a wall's clip epsilon that a box leaving the goal never
		// meets - so the reverse proof stands.  And a goal set against a wall
		// - an item in an alcove, an enemy flush with the brickwork - can sit
		// where a player's box does not fit: the box stops a few units short,
		// already overlapping whatever stands there, and that is arriving.
		// A goal touching a pad is the exception above, never these: the walk
		// from it starts inside the volume and proves nothing about walking in.
		if ( path.Num() > 0 && ( path[path.Num() - 1].origin - routeGoal ).LengthSqr() > 1.0f &&
			 TransportAtFloor( routeGoal ) < 0 ) {
			const idVec3 last = path[path.Num() - 1].origin;
			idVec3 touch;
			if ( WalkableLine( routeGoal, last ) ) {
				path.Append( routeGoal, NAVTRAVEL_WALK );
				path.SetTravelCost( start, exactTravelCost );
				path.routeStart = routeStart;
				return true;
			}
			if ( LegTouchesGoal( last, routeGoal, touch ) ) {
				path.Append( touch, NAVTRAVEL_WALK );
				path.SetTravelCost( start, exactTravelCost );
				path.routeStart = routeStart;
				return true;
			}
		}

		if ( path.Num() == 0 ||
			 ( path[path.Num() - 1].origin - routeGoal ).LengthSqr() > 1.0f ) {
			stats.legFailures++;
			if ( bot_debug.GetInteger() >= 1 && gameLocal.time >= nextSnapReportTime ) {
				nextSnapReportTime = gameLocal.time + 1000;
				const idVec3 &last = ( path.Num() > 0 ) ? path[path.Num() - 1].origin : nodes[goalNode].origin;
				const char *failure = "walkable";
				WalkableLine( last, routeGoal, &failure );
				gameLocal.Printf( "rvNavMesh: final leg %s to %s refused: %s\n",
								  last.ToString( 0 ), routeGoal.ToString( 0 ), failure );
			}
			path.Clear();
			return false;
		}

		path.SetTravelCost( start, exactTravelCost );
		path.routeStart = routeStart;
		return true;
	}

	stats.searchFailures++;
	path.Clear();
	return false;
}

/*
================
rvNavMesh::Probe

Diagnostics for "navmesh probe x y z [radius]": every node within twice the
radius of the point and 128 units of its height, with its links and the
support verdicts behind the graph, then height maps of the ground around the
point as the agent hull, the support box and a point each find it, four units
apart and relative to the point's height.  ".." is no floor within 256 units
below, "##" a start inside solid and a trailing "s" a surface too steep to
stand on.
================
*/
void rvNavMesh::Probe( const idVec3 &point, float radius ) const {
	static const char *travelNames[NAVTRAVEL_NUM] = { "walk", "drop", "jump", "pad", "tele" };
	const float step = pm_stepsize.GetFloat();
	const float support = navSupportBounds[1].x;

	gameLocal.Printf( "navmesh probe %s: cell %g, support box %g, step %g\n",
					  point.ToString( 0 ), cellSize, support, step );
	for ( int t = 0; t < transports.Num(); t++ ) {
		const navTransport_t &transport = transports[t];
		if ( transport.travelType != NAVTRAVEL_JUMPPAD ||
			 !transport.bounds.Expand( radius ).ContainsPoint( point ) ) {
			continue;
		}
		idEntity *pad = gameLocal.entities[transport.entityNum];
		idVec3 landing;
		float flight = 0.0f, impact = 0.0f;
		const bool landed = PadLanding( pad, point, NULL, landing, &flight, &impact );
		gameLocal.Printf( "NAV_PROBE_PAD pad=%d from=%s landed=%d landing=%s flight=%.3f impact=%.3f\n",
			transport.entityNum, point.ToString( 3 ), landed ? 1 : 0,
			landed ? landing.ToString( 3 ) : "none", flight, impact );
	}
	for ( int kind = 0; kind < 2; kind++ ) {
		trace_t trace;
		const idVec3 start = point + idVec3( 0.0f, 0.0f, step + NAV_GROUND_OFFSET );
		const idVec3 end = point - idVec3( 0.0f, 0.0f, 256.0f );
		if ( kind == 0 ) {
			NavAgentTranslation( trace, start, end );
		} else {
			NavTraceTranslation( trace, start, end, navSupportTrace, navSupportBounds );
		}
		const bool supported = trace.fraction > 0.0f && trace.fraction < 1.0f &&
			trace.c.normal.z >= NAV_WALKABLE_NORMAL_Z && LandingContinuationClear( trace.endpos, trace.endpos );
		gameLocal.Printf( "NAV_PROBE_SURFACE kind=%s point=%s floor=%s fraction=%.4f normal=%.3f %.3f %.3f continuation=%d\n",
			kind == 0 ? "agent" : "support", point.ToString( 2 ), trace.endpos.ToString( 2 ), trace.fraction,
			trace.c.normal.x, trace.c.normal.y, trace.c.normal.z, supported ? 1 : 0 );
	}

	for ( int i = 0; i < nodes.Num(); i++ ) {
		idVec3 delta = nodes[i].origin - point;
		if ( idMath::Fabs( delta.z ) > 128.0f ) {
			continue;
		}
		delta.z = 0.0f;
		if ( delta.LengthSqr() > Square( radius * 2.0f ) ) {
			continue;
		}

		const idVec3 &origin = nodes[i].origin;
		const idVec3 sides[4] = {
			idVec3( support, 0.0f, 0.0f ), idVec3( -support, 0.0f, 0.0f ),
			idVec3( 0.0f, support, 0.0f ), idVec3( 0.0f, -support, 0.0f )
		};
		idStr linksText;
		for ( int l = nodes[i].firstLink; l != -1; l = links[l].next ) {
			const idVec3 to = nodes[links[l].node].origin - origin;
			linksText += va( " %s(%.0f,%.0f,%.0f)", travelNames[idMath::ClampInt( 0, NAVTRAVEL_NUM - 1, links[l].travelType )],
							 to.x, to.y, to.z );
		}
		gameLocal.Printf( "  node %d at %s flags %d area %d: floor %s, landing %s (+x %s, -x %s, +y %s, -y %s), links:%s\n",
						  i, origin.ToString( 0 ), nodes[i].flags, nodes[i].area,
						  NavFloorSupported( origin ) ? "yes" : "NO",
						  NavLandingSupported( origin ) ? "yes" : "NO",
						  NavFloorSupported( origin + sides[0] ) ? "yes" : "NO",
						  NavFloorSupported( origin + sides[1] ) ? "yes" : "NO",
						  NavFloorSupported( origin + sides[2] ) ? "yes" : "NO",
						  NavFloorSupported( origin + sides[3] ) ? "yes" : "NO",
						  linksText.c_str() );
	}

	const int cells = (int)idMath::Ceil( radius / 4.0f );
	const idClip *clipWorld = gameLocal.GetEntityClipWorld( NULL );
	for ( int kind = 0; kind < 3; kind++ ) {
		gameLocal.Printf( "  %s floor heights, x %.0f..%.0f left to right, y %.0f..%.0f top to bottom:\n",
						  ( kind == 0 ) ? "agent hull" : ( kind == 1 ) ? "support box" : "point",
						  point.x - cells * 4.0f, point.x + cells * 4.0f, point.y + cells * 4.0f, point.y - cells * 4.0f );
		for ( int row = cells; row >= -cells; row-- ) {
			idStr line = va( "  y%+5.0f:", row * 4.0f );
			for ( int col = -cells; col <= cells; col++ ) {
				const idVec3 at = point + idVec3( col * 4.0f, row * 4.0f, 0.0f );
				const idVec3 start = at + idVec3( 0.0f, 0.0f, step + NAV_GROUND_OFFSET );
				const idVec3 end = at - idVec3( 0.0f, 0.0f, 256.0f );
				trace_t trace;
				bool clear;
				if ( kind == 0 ) {
					clear = NavAgentTranslation( trace, start, end );
				} else if ( kind == 1 ) {
					clear = NavTraceTranslation( trace, start, end, navSupportTrace, navSupportBounds );
				} else {
					memset( &trace, 0, sizeof( trace ) );
					trace.fraction = 1.0f;
					trace.endpos = end;
					if ( clipWorld ) {
						const_cast<idClip *>( clipWorld )->TracePoint( trace, start, end, NAV_CLIP_MASK, NULL );
					}
					clear = trace.fraction >= 1.0f;
				}
				if ( clear ) {
					line += "    ..";
				} else if ( trace.fraction <= 0.0f ) {
					line += "    ##";
				} else {
					line += va( "%5.0f%s", trace.endpos.z - point.z,
								( trace.c.normal.z < NAV_WALKABLE_NORMAL_Z ) ? "s" : " " );
				}
			}
			gameLocal.Printf( "%s\n", line.c_str() );
		}
	}
}

/*
================
rvNavMesh::DebugDraw
================
*/
void rvNavMesh::DebugDraw( const idVec3 &viewOrigin, float radius ) const {
	static const idVec4 *travelColors[NAVTRAVEL_NUM] = {
		&colorGreen,		// walk
		&colorYellow,		// drop
		&colorOrange,		// jump
		&colorMagenta,		// jump pad
		&colorCyan			// teleport
	};

	// The renderer keeps a fixed debug line pool (MAX_DEBUG_LINES, 16384) and
	// silently drops anything past it, so a dense mesh drawn without a budget
	// fills the pool with whichever nodes happened to come first and shows
	// nothing useful.  Spend the budget near the viewer instead.
	static const int NAV_DEBUG_MAX_LINES = 8192;

	const float	radiusSqr	= radius * radius;
	int			budget		= NAV_DEBUG_MAX_LINES;

	for ( int i = 0; i < nodes.Num() && budget > 0; i++ ) {
		if ( ( nodes[i].origin - viewOrigin ).LengthSqr() > radiusSqr ) {
			continue;
		}

		gameRenderWorld->DebugLine( colorBlue, nodes[i].origin, nodes[i].origin + idVec3( 0.0f, 0.0f, 8.0f ), 0 );
		budget--;

		for ( int l = nodes[i].firstLink; l != -1 && budget > 0; l = links[l].next ) {
			const int type = idMath::ClampInt( 0, NAVTRAVEL_NUM - 1, links[l].travelType );

			// One direction only; the reverse link would draw the same segment.
			if ( links[l].node < i && links[l].travelType == NAVTRAVEL_WALK ) {
				continue;
			}

			gameRenderWorld->DebugLine( *travelColors[type],
										nodes[i].origin + idVec3( 0.0f, 0.0f, 4.0f ),
										nodes[links[l].node].origin + idVec3( 0.0f, 0.0f, 4.0f ),
										0 );
			budget--;
		}
	}
}
