//----------------------------------------------------------------
// BotCombat.cpp
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "../Projectile.h"
#include "BotCombat.h"
#include "NavMesh.h"

// Points remain slightly inside the collision bounds.  Chest first gives a
// stable centre-mass aim when the whole player is exposed; head and pelvis are
// fallbacks for partial cover.
static const float BOTCOMBAT_CHEST_FRACTION	= 0.62f;
static const float BOTCOMBAT_HEAD_FRACTION	= 0.84f;
static const float BOTCOMBAT_PELVIS_FRACTION	= 0.38f;
static const float BOTCOMBAT_LATERAL_INSET	= 0.32f;

// Combat queries run on every active bot, so trajectory work is deliberately
// bounded.  A 48-segment cap still gives a fast MP nail a segment shorter than
// a player hull over the caller's ordinary sub-second dodge horizon.
static const int   BOTCOMBAT_MIN_TRAJECTORY_STEPS = 6;
static const int   BOTCOMBAT_MAX_TRAJECTORY_STEPS = 48;
static const float BOTCOMBAT_MAX_SHOT_FLIGHT	= 2.0f;
static const float BOTCOMBAT_MIN_PROJECTILE_EXTENT = 0.25f;

// Splash a bot will accept on itself when the same blast does at least twice
// as much to the target, and the floor below which it refuses any more.
static const float BOTCOMBAT_SELF_SPLASH_TOLERANCE	= 12.0f;
static const float BOTCOMBAT_SELF_SPLASH_RESERVE	= 25.0f;
// Team mates matter only when team damage is on, and then any real hurt counts.
static const float BOTCOMBAT_TEAM_SPLASH_TOLERANCE	= 5.0f;
// With team damage off a blast still pushes: an impact this close to a
// team-mate's hull is refused for the knockback alone.
static const float BOTCOMBAT_TEAM_PUSH_RADIUS		= 48.0f;
// A kick slower than this moves a player but cannot throw one off a walkway;
// it is the same strong-knock floor a bot's own air recovery reacts to.
static const float BOTCOMBAT_TEAM_KNOCK_MIN_SPEED	= 150.0f;
// No stock round's blast, burn or residual radius exceeds the Dark Matter
// Gun's 768; a round further off than that plus its travel is rejected before
// its definitions are read.
static const float BOTCOMBAT_MAX_SPLASH_REACH		= 768.0f;

struct botCombatShotModel_t {
	const idDict *projectileDef;
	float		 speed;
	idVec3		 gravity;
	idBounds	 bounds;
	float		 boundsRadius;
	int			 clipMask;
	float		 splashRadius;		// from def_splash_damage, 0 for none
	float		 splashDamage;
	float		 attackerScale;		// RadiusDamage's attackerDamageScale
	float		 directDamage;		// def_damage on a direct hit
	float		 power;				// the shooter's powerup scale on the blast
	float		 teamKnockback;		// the splash's knockback_team, else knockback
	float		 selfKnockback;		// ordinary or team-mode knockback, before self scaling
	float		 selfPushScale;		// idPlayer::Damage's attackerPushScale

	botCombatShotModel_t() :
		projectileDef( NULL ),
		speed( 0.0f ),
		gravity( vec3_zero ),
		bounds( idVec3( -BOTCOMBAT_MIN_PROJECTILE_EXTENT, -BOTCOMBAT_MIN_PROJECTILE_EXTENT, -BOTCOMBAT_MIN_PROJECTILE_EXTENT ),
				idVec3( BOTCOMBAT_MIN_PROJECTILE_EXTENT, BOTCOMBAT_MIN_PROJECTILE_EXTENT, BOTCOMBAT_MIN_PROJECTILE_EXTENT ) ),
		boundsRadius( BOTCOMBAT_MIN_PROJECTILE_EXTENT ),
		clipMask( MASK_SHOT_BOUNDINGBOX ),
		splashRadius( 0.0f ),
		splashDamage( 0.0f ),
		attackerScale( 0.5f ),
		directDamage( 0.0f ),
		power( 1.0f ),
		teamKnockback( 0.0f ),
		selfKnockback( 0.0f ),
		selfPushScale( 2.0f ) {
	}
};

/*
================
BotCombatSymmetricProjectileBounds

Entity `size` bounds are based at z=0 and projectile trace models rotate in
flight.  TraceBounds has no axis parameter, so use the conservative symmetric
extents of the authored bounds.  The live-projectile query uses its physics
bounds through this same conversion.
================
*/
static idBounds BotCombatSymmetricProjectileBounds( const idBounds &authored ) {
	idVec3 extent;

	for ( int i = 0; i < 3; i++ ) {
		extent[i] = Max( BOTCOMBAT_MIN_PROJECTILE_EXTENT,
			Max( idMath::Fabs( authored[0][i] ), idMath::Fabs( authored[1][i] ) ) );
	}

	return idBounds( -extent, extent );
}

/*
================
BotCombatProjectileBoundsFromDef
================
*/
static idBounds BotCombatProjectileBoundsFromDef( const idDict *projectileDef ) {
	idBounds authored;
	idVec3 size;

	if ( projectileDef &&
		 projectileDef->GetVector( "mins", NULL, authored[0] ) &&
		 projectileDef->GetVector( "maxs", NULL, authored[1] ) ) {
		return BotCombatSymmetricProjectileBounds( authored );
	}

	if ( projectileDef && projectileDef->GetVector( "size", NULL, size ) ) {
		authored[0].Set( size.x * -0.5f, size.y * -0.5f, 0.0f );
		authored[1].Set( size.x * 0.5f, size.y * 0.5f, size.z );
		return BotCombatSymmetricProjectileBounds( authored );
	}

	return idBounds( idVec3( -BOTCOMBAT_MIN_PROJECTILE_EXTENT, -BOTCOMBAT_MIN_PROJECTILE_EXTENT, -BOTCOMBAT_MIN_PROJECTILE_EXTENT ),
					 idVec3( BOTCOMBAT_MIN_PROJECTILE_EXTENT, BOTCOMBAT_MIN_PROJECTILE_EXTENT, BOTCOMBAT_MIN_PROJECTILE_EXTENT ) );
}

/*
================
BotCombatProjectileClipMask

Match idProjectile::Launch's content policy closely enough for a speculative
shot sweep.  The live threat path uses the projectile's exact physics mask.
================
*/
static int BotCombatProjectileClipMask( const idDict *projectileDef ) {
	if ( !projectileDef ) {
		return MASK_SHOT_BOUNDINGBOX;
	}

	// No CONTENTS_WATER: idProjectile::Launch never adds it and a rocket flies
	// through a pool.  With it the sweep stopped on the surface, so a foe
	// across water was "behind" an impact that never happens and a pool edge
	// near the shooter vetoed shots as self splash.
	int mask = projectileDef->GetBool( "clipmask_rendermodel", "1" ) ?
		MASK_SHOT_RENDERMODEL : MASK_SHOT_BOUNDINGBOX;
	mask |= CONTENTS_PROJECTILECLIP | CONTENTS_PROJECTILE;
	if ( projectileDef->GetBool( "clipmask_largeshot", "1" ) ) {
		mask |= CONTENTS_LARGESHOTCLIP;
	}
	if ( projectileDef->GetBool( "clipmask_moveable", "0" ) ) {
		mask |= CONTENTS_MOVEABLECLIP;
	}
	if ( projectileDef->GetBool( "clipmask_monsterclip", "0" ) ) {
		mask |= CONTENTS_MONSTERCLIP;
	}

	return mask;
}

/*
================
BotCombatCurrentShotModel

rvWeapon::spawnArgs is the mod-resolved view of the currently selected weapon;
FindEntityDefDict then applies the multiplayer projectile override.
================
*/
static bool BotCombatCurrentShotModel( idPlayer *shooter, botCombatShotModel_t &model ) {
	if ( !shooter || !shooter->weapon ) {
		return false;
	}

	const char *projectileName = shooter->weapon->spawnArgs.GetString( "def_projectile", "" );
	if ( !projectileName[0] ) {
		return false;
	}

	model.projectileDef = gameLocal.FindEntityDefDict( projectileName, false );
	if ( !model.projectileDef ) {
		return false;
	}

	model.speed = idProjectile::GetVelocity( model.projectileDef ).x;
	if ( model.speed <= idMath::FLOAT_EPSILON ) {
		return false;
	}

	model.gravity		= idProjectile::GetGravity( model.projectileDef );
	model.bounds		= BotCombatProjectileBoundsFromDef( model.projectileDef );
	model.boundsRadius	= model.bounds.GetRadius();
	model.clipMask		= BotCombatProjectileClipMask( model.projectileDef );

	// rvWeapon::Attack scales the launch power by the shooter's damage
	// powerups - quad, doubler, the team damage buy - and RadiusDamage applies
	// that power to the shooter's own splash too.  A quad bot that priced its
	// self splash at the unscaled figure fired a rocket at a foe thirty units
	// away for 37 of health on paper and 112 in the game.
	const float power = Max( 1.0f, shooter->PowerUpModifier( PMOD_PROJECTILE_DAMAGE ) );

	const char *splashName = model.projectileDef->GetString( "def_splash_damage", "" );
	const idDict *splash = splashName[0] ? gameLocal.FindEntityDefDict( splashName, false ) : NULL;
	model.power = power;
	if ( splash ) {
		model.splashRadius	= Max( 0.0f, splash->GetFloat( "radius", "50" ) );
		model.splashDamage	= Max( 0.0f, splash->GetFloat( "damage", "20" ) ) * power;
		model.attackerScale	= Max( 0.0f, splash->GetFloat( "attackerDamageScale", "0.5" ) );
		// idPlayer::Damage reads knockback_team in team games, falling back to knockback.
		const int knockback = splash->GetInt( "knockback", "0" );
		model.teamKnockback	= (float)Max( 0, splash->GetInt( "knockback_team", va( "%d", knockback ) ) );
		model.selfKnockback	= gameLocal.IsTeamGame() ? model.teamKnockback : (float)Max( 0, knockback );
		model.selfPushScale	= Max( 0.0f, splash->GetFloat( "attackerPushScale", "2" ) );
	}

	const char *directName = model.projectileDef->GetString( "def_damage", "" );
	const idDict *direct = directName[0] ? gameLocal.FindEntityDefDict( directName, false ) : NULL;
	if ( direct ) {
		model.directDamage	= Max( 0.0f, direct->GetFloat( "damage", "0" ) ) * power;
	}
	return true;
}

/*
================
BotCombatTrajectoryPoint
================
*/
static idVec3 BotCombatTrajectoryPoint( const idVec3 &origin, const idVec3 &velocity,
									 const idVec3 &gravity, float time ) {
	return origin + velocity * time + gravity * ( 0.5f * time * time );
}

/*
================
BotCombatProjectileSplashRadius
================
*/
static float BotCombatProjectileSplashRadius( const idProjectile *projectile ) {
	if ( !projectile ) {
		return 0.0f;
	}

	// The blast on detonation, and the area a round hurts while it lies there:
	// a napalm glob has no splash at all and burns 140 units around itself
	// every three quarters of a second for six seconds.  Both are reasons to
	// move, and both are radii the threat scan can price.
	static const char *radiusKeys[] = { "def_splash_damage", "def_emit_damage", "def_residual_damage" };
	float radius = 0.0f;
	for ( int i = 0; i < (int)( sizeof( radiusKeys ) / sizeof( radiusKeys[0] ) ); i++ ) {
		const char *damageName = projectile->spawnArgs.GetString( radiusKeys[i], "" );
		const idDict *damageDef = damageName[0] ? gameLocal.FindEntityDefDict( damageName, false ) : NULL;
		if ( damageDef ) {
			radius = Max( radius, damageDef->GetFloat( "radius", "0" ) );
		}
	}
	return radius;
}

/*
================
BotCombatProjectileLingers

A round that keeps hurting after it has stopped moving: a fused grenade, or a
glob that burns where it lies.  Neither has a useful closest-approach solution
once it is at rest beside the bot, and both are exactly why the bot should go.
================
*/
static bool BotCombatProjectileLingers( const idProjectile *projectile ) {
	return projectile->spawnArgs.GetBool( "detonate_on_fuse", "0" ) ||
		   projectile->spawnArgs.GetString( "def_emit_damage", "" )[0] ||
		   projectile->spawnArgs.GetString( "def_residual_damage", "" )[0];
}

/*
================
BotCombatTraceEntity

GetTraceEntity resolves a hit on a bound player attachment back to its master,
which is important for models whose head owns a separate clip model.
================
*/
static idEntity *BotCombatTraceEntity( const trace_t &trace ) {
	if ( trace.c.entityNum < 0 || trace.c.entityNum >= MAX_GENTITIES ) {
		return NULL;
	}

	return gameLocal.GetTraceEntity( trace );
}

/*
================
BotCombatSplashIgnore

The entity a splash exposure trace may pass through.  Only an actor the shot
stopped on qualifies: RadiusDamage skips the directly hit actor and nothing
else, and its cover test is a world trace.  The worldspawn entity in particular
must never become the pass entity - idClip::Translation skips the whole world
model for ENTITYNUM_WORLD - so a wall hit used to switch every cover test off:
a bot behind a pillar read as exposed to its own blast on the far side of it,
the "is this impact useful" test said a rocket into the wall splashed a foe
behind cover, and a grenade landing behind a thin wall was dodged.  A mover or
a crate is cover as well, and is not passed through either.
================
*/
static idEntity *BotCombatSplashIgnore( idEntity *hit, idEntity *fallback ) {
	if ( hit && hit->IsType( idActor::GetClassType() ) ) {
		return hit;
	}
	return fallback;
}

/*
================
BotCombatSameTeam
================
*/
static bool BotCombatSameTeam( const idPlayer *self, const idPlayer *other ) {
	return self && other && gameLocal.IsTeamGame() && self->team == other->team;
}

/*
================
BotCombatValidClientPlayer

CanPlay indexes multiplayer state by entityNumber, so every helper that accepts
a player from an entity pointer must establish the client range first.
================
*/
static bool BotCombatValidClientPlayer( idPlayer *player ) {
	return player && player->entityNumber >= 0 && player->entityNumber < MAX_CLIENTS &&
		player->GetPhysics();
}

/*
================
BotCombatValidFoe

The target can die, spectate, change team, or move to another arena after the
bot selected it and before the final trigger decision.
================
*/
static bool BotCombatValidFoe( idPlayer *shooter, idPlayer *foe ) {
	if ( !BotCombatValidClientPlayer( shooter ) || !BotCombatValidClientPlayer( foe ) ||
		 !gameLocal.mpGame.CanPlay( shooter ) || shooter->spectating || shooter->health <= 0 ||
		 foe == shooter || foe->GetInstance() != shooter->GetInstance() ||
		 !gameLocal.mpGame.CanPlay( foe ) || foe->spectating || foe->health <= 0 ||
		 foe->fl.notarget ) {
		return false;
	}

	return !BotCombatSameTeam( shooter, foe );
}

/*
================
BotCombatPredictedBoundsExposed

CanDamage is ideal at time zero because it samples the live entity hull.  For a
projectile arriving later, trace to the same centre/corner pattern translated
by current velocity.  World cover still counts, while the teammate's present
clip model is not mistaken for their future position.
================
*/
static bool BotCombatPredictedBoundsExposed( idPlayer *player, idEntity *splashIgnore,
											  const idVec3 &impact, float impactTime ) {
	if ( impactTime <= 0.05f ) {
		idVec3 damagePoint;
		return player->CanDamage( impact, damagePoint, splashIgnore );
	}

	idBounds predicted = player->GetPhysics()->GetAbsBounds() +
		player->GetPhysics()->GetLinearVelocity() * impactTime;
	const idVec3 middle = predicted.GetCenter();
	const float insetX = Min( 15.0f, ( predicted[1].x - predicted[0].x ) * 0.35f );
	const float insetY = Min( 15.0f, ( predicted[1].y - predicted[0].y ) * 0.35f );
	const float insetZ = Min( 15.0f, ( predicted[1].z - predicted[0].z ) * 0.25f );
	const idVec3 samples[] = {
		middle,
		middle + idVec3( insetX,  insetY, 0.0f ),
		middle + idVec3( insetX, -insetY, 0.0f ),
		middle + idVec3( -insetX, insetY, 0.0f ),
		middle + idVec3( -insetX, -insetY, 0.0f ),
		middle + idVec3( 0.0f, 0.0f, insetZ ),
		middle - idVec3( 0.0f, 0.0f, insetZ )
	};

	for ( int i = 0; i < (int)( sizeof( samples ) / sizeof( samples[0] ) ); i++ ) {
		trace_t trace;
		gameLocal.TracePoint( player, trace, impact, samples[i], MASK_SOLID, splashIgnore );
		if ( trace.fraction >= 1.0f || BotCombatTraceEntity( trace ) == player ) {
			return true;
		}
	}

	return false;
}

/*
================
BotCombatSplashThreatensPlayer
================
*/
static bool BotCombatSplashThreatensPlayer( idPlayer *player, idEntity *splashIgnore,
											 const idVec3 &impact, float radius,
											 float impactTime ) {
	if ( !BotCombatValidClientPlayer( player ) || radius <= 0.0f ) {
		return false;
	}

	const idBounds predicted = player->GetPhysics()->GetAbsBounds() +
		player->GetPhysics()->GetLinearVelocity() * Max( 0.0f, impactTime );
	if ( predicted.ShortestDistance( impact ) >= radius ) {
		return false;
	}

	return BotCombatPredictedBoundsExposed( player, splashIgnore, impact,
		Max( 0.0f, impactTime ) );
}

/*
================
BotCombatSplashDamageTo

The health a blast at impact would cost this player, mirroring RadiusDamage:
linear falloff over the distance from the hull's edge, the attacker's own
scale for the shooter, nothing behind cover, and nothing for a team mate while
team damage is off.  The hull is projected to impact time.
================
*/
static float BotCombatSplashDamageTo( idPlayer *shooter, idPlayer *player, idEntity *splashIgnore,
									  const idVec3 &impact, const botCombatShotModel_t &model,
									  float impactTime ) {
	if ( !player || model.splashRadius <= 0.0f || model.splashDamage <= 0.0f ||
		 player->entityNumber < 0 || player->entityNumber >= MAX_CLIENTS || !player->GetPhysics() ) {
		return 0.0f;
	}
	if ( player != shooter && gameLocal.IsTeamGame() && shooter && player->team == shooter->team &&
		 !gameLocal.serverInfo.GetBool( "si_teamDamage" ) ) {
		return 0.0f;
	}

	const idBounds predicted = player->GetPhysics()->GetAbsBounds() +
		player->GetPhysics()->GetLinearVelocity() * Max( 0.0f, impactTime );
	const float distance = predicted.ShortestDistance( impact );
	if ( distance >= model.splashRadius ) {
		return 0.0f;
	}

	float damage = model.splashDamage * ( 1.0f - distance / model.splashRadius );
	if ( player == shooter ) {
		damage *= model.attackerScale;
	}
	if ( damage <= 0.0f ) {
		return 0.0f;
	}

	return BotCombatPredictedBoundsExposed( player, splashIgnore, impact, Max( 0.0f, impactTime ) ) ?
		damage : 0.0f;
}

/*
================
BotCombatSplashThreatensTeamMate

Direct traces only protect a teammate standing in the projectile path.  Splash
also reaches players beside the impact, so mirror RadiusDamage's bounds-distance
and exposure tests for live teammates in this arena, projected to impact time.
================
*/
static bool BotCombatSplashThreatensTeamMate( idPlayer *shooter, idEntity *splashIgnore,
											  const idVec3 &impact, float radius,
											  float impactTime ) {
	if ( !shooter || !gameLocal.IsTeamGame() || radius <= 0.0f ) {
		return false;
	}

	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idPlayer *teamMate = gameLocal.GetClientByNum( i );
		if ( !BotCombatValidClientPlayer( teamMate ) || teamMate == shooter ||
			 teamMate->GetInstance() != shooter->GetInstance() ||
			 !gameLocal.mpGame.CanPlay( teamMate ) || teamMate->spectating ||
			 teamMate->health <= 0 || !BotCombatSameTeam( shooter, teamMate ) ) {
			continue;
		}

		if ( BotCombatSplashThreatensPlayer( teamMate, splashIgnore, impact,
											 radius, impactTime ) ) {
			return true;
		}
	}

	return false;
}

/*
================
BotCombatSplashThrowsShooter

Health-only splash trades miss the shooter's own knockback. Follow the exact
self scaling and require supported ground after the slide/flight before firing.
================
*/
static bool BotCombatSplashThrowsShooter( idPlayer *shooter, idEntity *splashIgnore,
		const idVec3 &impact, const botCombatShotModel_t &model, float impactTime ) {
	if ( !shooter || !shooter->GetPhysics() || !navMesh.IsValid() ||
		model.splashRadius <= 0.0f || model.selfKnockback <= 0.0f ||
		model.attackerScale <= 0.0f ) {
		return false;
	}
	const float leadTime = Max( 0.0f, impactTime );
	const idVec3 velocity = shooter->GetPhysics()->GetLinearVelocity();
	// A voluntary flight is proven for its held movement, not an additional
	// self-blast. In particular a rocket fired just before a narrow touchdown
	// can arrive after it and throw the shooter straight back off the ledge.
	// Check the present hull as well as the projected one: a linear projection
	// does not know where a jump will collide/land during the projectile flight.
	const float presentDistance = shooter->GetPhysics()->GetAbsBounds().ShortestDistance( impact );
	const int presentKnockback = (int)( model.selfKnockback * model.power *
		Max( 0.0f, 1.0f - presentDistance / model.splashRadius ) * model.attackerScale );
	if ( !shooter->pfl.onGround && model.selfPushScale > 0.0f &&
		 presentKnockback > 0 &&
		 BotCombatPredictedBoundsExposed( shooter, splashIgnore, impact, 0.0f ) ) {
		return true;
	}
	const idVec3 origin = shooter->GetPhysics()->GetOrigin() + velocity * leadTime;
	const idBounds predicted = shooter->GetPhysics()->GetAbsBounds() + velocity * leadTime;
	const float distance = predicted.ShortestDistance( impact );
	if ( distance >= model.splashRadius ||
		!BotCombatPredictedBoundsExposed( shooter, splashIgnore, impact, leadTime ) ) {
		return false;
	}
	// RadiusDamage scales self damage before Damage truncates knockback;
	// attackerPushScale applies afterwards and does not lengthen the timer.
	const int knockback = (int)( model.selfKnockback * model.power *
		( 1.0f - distance / model.splashRadius ) * model.attackerScale );
	if ( knockback <= 0 ) {
		return false;
	}
	if ( !shooter->pfl.onGround && model.selfPushScale > 0.0f ) {
		return true;
	}
	idVec3 direction = ( origin + idVec3( 0.0f, 0.0f, 31.875f ) ) - impact;
	direction.z += 24.0f;
	direction.Normalize();
	const idVec3 kick = direction * ( g_knockback.GetFloat() * (float)knockback * model.selfPushScale / 200.0f );
	// An affordable health trade can still throw the shooter into the void.
	// Prove the grounded slide as well as the flight; ordinary stopping friction
	// is unavailable during the new knockback interval, even when the authored
	// push scale or direction is zero (Damage still starts the timer).
	return !navMesh.KnockSlideLandingSafe( origin, velocity + kick,
		idMath::ClampInt( 50, 200, knockback * 2 ), idMath::INFINITY );
}

/*
================
BotCombatSplashThrowsTeamMate

Team damage on or off, RadiusDamage pushes every exposed hull in a blast and
idPlayer::Damage applies knockback_team to a team-mate either way. A rocket
beside a team-mate on a walkway can throw them into the void. Predict the
falloff, multiplayer push direction and g_knockback, then require a landing.
================
*/
static bool BotCombatSplashThrowsTeamMate( idPlayer *shooter, idEntity *splashIgnore,
										   const idVec3 &impact, const botCombatShotModel_t &model,
										   float impactTime ) {
	if ( !shooter || !gameLocal.IsTeamGame() || model.splashRadius <= 0.0f ||
		 model.teamKnockback <= 0.0f || !navMesh.IsValid() ) {
		return false;
	}

	const float leadTime = Max( 0.0f, impactTime );
	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idPlayer *teamMate = gameLocal.GetClientByNum( i );
		if ( !BotCombatValidClientPlayer( teamMate ) || teamMate == shooter ||
			 teamMate->GetInstance() != shooter->GetInstance() ||
			 !gameLocal.mpGame.CanPlay( teamMate ) || teamMate->spectating ||
			 teamMate->health <= 0 || !BotCombatSameTeam( shooter, teamMate ) ||
			 !teamMate->GetPhysics() ) {
			continue;
		}

		const idVec3 velocity = teamMate->GetPhysics()->GetLinearVelocity();
		const idBounds predicted = teamMate->GetPhysics()->GetAbsBounds() + velocity * leadTime;
		const float distance = predicted.ShortestDistance( impact );
		if ( distance >= model.splashRadius ||
			 !BotCombatPredictedBoundsExposed( teamMate, splashIgnore, impact, leadTime ) ) {
			continue;
		}

		// RadiusDamage's falloff scale; idPlayer::Damage truncates the scaled knockback.
		const int knockback = (int)( model.teamKnockback * model.power * ( 1.0f - distance / model.splashRadius ) );
		if ( knockback <= 0 ) {
			continue;
		}
		const idVec3 origin = teamMate->GetPhysics()->GetOrigin() + velocity * leadTime;
		idVec3 direction = ( origin + idVec3( 0.0f, 0.0f, 31.875f ) ) - impact;
		direction.z += 24.0f;
		if ( direction.Normalize() <= 0.0f ) {
			continue;
		}
		const idVec3 kick = direction * ( g_knockback.GetFloat() * (float)knockback / 200.0f );
		if ( kick.LengthSqr() < Square( BOTCOMBAT_TEAM_KNOCK_MIN_SPEED ) ) {
			continue;
		}
		if ( !navMesh.AirInputLandingSafe( origin, velocity + kick, vec3_origin, idMath::INFINITY ) ) {
			return true;
		}
	}
	return false;
}

/*
================
BotCombatPenetratingShotHitsTeamMate

A penetrating hitscan - the multiplayer railgun, "penetrate" 1.0 - passes
through every actor it strikes until something else stops it
(idGameLocal::HitScan), so the first entity on the line proves nothing about
the ones behind it, beyond the aim point included. Trace the weapon's whole
range the same way and report a team-mate on it: with team damage off the hit
still pushes them, and a q4ctf5 bot railed through an enemy knocked its
team-mate 500 units/s.
================
*/
static bool BotCombatPenetratingShotHitsTeamMate( idPlayer *shooter, const idVec3 &shotOrigin,
												  const idVec3 &shotDirection ) {
	if ( !shooter || !shooter->weapon || !gameLocal.IsTeamGame() ) {
		return false;
	}
	const char *hitscanName = shooter->weapon->spawnArgs.GetString( "def_hitscan", "" );
	const idDict *hitscan = hitscanName[0] ? gameLocal.FindEntityDefDict( hitscanName, false ) : NULL;
	if ( !hitscan || hitscan->GetFloat( "penetrate" ) <= 0.0f ) {
		return false;
	}

	const idVec3 end = shotOrigin + shotDirection * hitscan->GetFloat( "range", "40000" );
	idVec3 start = shotOrigin;
	idEntity *ignore = shooter;
	// Each pass leaves one actor behind, so the loop ends after every client.
	for ( int pass = 0; pass <= MAX_CLIENTS; pass++ ) {
		trace_t trace;
		gameLocal.TracePoint( shooter, trace, start, end, MASK_SHOT_BOUNDINGBOX, ignore );
		idEntity *hit = ( trace.fraction < 1.0f ) ? BotCombatTraceEntity( trace ) : NULL;
		if ( !hit || !hit->IsType( idActor::GetClassType() ) ) {
			return false;	// open air, the world or a mover stops the round
		}
		if ( hit->IsType( idPlayer::GetClassType() ) &&
			 BotCombatSameTeam( shooter, static_cast<idPlayer *>( hit ) ) ) {
			return true;
		}
		start = trace.endpos;
		ignore = hit;
	}
	return false;
}

/*
================
BotCombatShotCrossesMovingTeamMate

A point trace only sees where a teammate is now.  Transform each projectile
segment into the teammate's moving frame and intersect it with an expanded
copy of their current hull.  This catches the classic rocket/team-mate doorway
cross even when neither occupies the intersection point on the firing frame.
================
*/
static bool BotCombatShotCrossesMovingTeamMate( idPlayer *shooter,
												 const idVec3 &segmentStart,
												 const idVec3 &segmentEnd,
												 float startTime, float endTime,
												 float projectileRadius ) {
	if ( !shooter || !gameLocal.IsTeamGame() ) {
		return false;
	}

	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idPlayer *teamMate = gameLocal.GetClientByNum( i );
		if ( !BotCombatValidClientPlayer( teamMate ) || teamMate == shooter ||
			 teamMate->GetInstance() != shooter->GetInstance() ||
			 !gameLocal.mpGame.CanPlay( teamMate ) || teamMate->spectating ||
			 teamMate->health <= 0 || !BotCombatSameTeam( shooter, teamMate ) ) {
			continue;
		}

		const idVec3 velocity = teamMate->GetPhysics()->GetLinearVelocity();
		const idVec3 relativeStart = segmentStart - velocity * Max( 0.0f, startTime );
		const idVec3 relativeEnd = segmentEnd - velocity * Max( 0.0f, endTime );
		const idBounds corridor = teamMate->GetPhysics()->GetAbsBounds().Expand(
			Max( 0.0f, projectileRadius ) );

		if ( corridor.LineIntersection( relativeStart, relativeEnd ) ) {
			return true;
		}
	}

	return false;
}

/*
================
BotCombatHostileProjectileOwner

The projectile's own instance decides whether it can reach this bot.  A valid,
same-instance owner can prove the projectile friendly; a missing or stale owner
cannot prove that old ordnance is safe after a disconnect, spectate, or arena
transition.
================
*/
static bool BotCombatHostileProjectileOwner( idPlayer *self, idPlayer *owner ) {
	if ( !BotCombatValidClientPlayer( self ) ) {
		return false;
	}
	if ( !BotCombatValidClientPlayer( owner ) ||
		 owner->GetInstance() != self->GetInstance() ||
		 !gameLocal.mpGame.CanPlay( owner ) ) {
		return true;
	}
	// The bot's own blast always hurts it, and a team mate's does whenever
	// team damage is on; both are threats to be dodged, if never targets.
	if ( owner == self ) {
		return true;
	}
	if ( BotCombatSameTeam( self, owner ) ) {
		return gameLocal.serverInfo.GetBool( "si_teamDamage" );
	}

	return true;
}

/*
================
BotCombatProjectileCanDamage

Unknown-owner projectiles are treated conservatively only when their live
definition contains a direct, splash, residual, or emitted damage channel.
This keeps cosmetic and scripted non-damaging projectiles out of dodge scans.
================
*/
static bool BotCombatProjectileCanDamage( const idProjectile *projectile ) {
	if ( !projectile ) {
		return false;
	}

	static const char *damageKeys[] = {
		"def_damage",
		"def_splash_damage",
		"def_residual_damage",
		"def_emit_damage"
	};
	for ( int i = 0; i < (int)( sizeof( damageKeys ) / sizeof( damageKeys[0] ) ); i++ ) {
		if ( projectile->spawnArgs.GetString( damageKeys[i], "" )[0] ) {
			return true;
		}
	}

	return false;
}

/*
================
BotCombatFindVisibleAimPoint
================
*/
bool BotCombatFindVisibleAimPoint( idPlayer *observer, idPlayer *target,
								   idVec3 &visiblePoint ) {
	visiblePoint.Zero();

	if ( !BotCombatValidClientPlayer( observer ) || !BotCombatValidClientPlayer( target ) ||
		 observer == target ) {
		return false;
	}
	if ( observer->GetInstance() != target->GetInstance() ) {
		return false;
	}
	if ( observer->spectating || observer->health <= 0 || !gameLocal.mpGame.CanPlay( observer ) ||
		 target->spectating || target->health <= 0 || !gameLocal.mpGame.CanPlay( target ) ) {
		return false;
	}

	idVec3 eye;
	idMat3 axis;

	observer->GetViewPos( eye, axis );

	const idBounds &bounds = target->GetPhysics()->GetAbsBounds();
	const float height = bounds[1].z - bounds[0].z;

	if ( height <= idMath::FLOAT_EPSILON ) {
		return false;
	}

	idVec3 targetEye;
	idMat3 targetAxis;
	target->GetViewPos( targetEye, targetAxis );
	(void)targetEye;
	idVec3 lateral = targetAxis[1];
	lateral.z = 0.0f;
	if ( lateral.Normalize() <= idMath::FLOAT_EPSILON ) {
		lateral.Set( 0.0f, 1.0f, 0.0f );
	}
	const float lateralDistance = Min( bounds[1].x - bounds[0].x,
		bounds[1].y - bounds[0].y ) * BOTCOMBAT_LATERAL_INSET;
	lateral *= lateralDistance;

	idVec3 chest = bounds.GetCenter();
	chest.z = bounds[0].z + height * BOTCOMBAT_CHEST_FRACTION;
	idVec3 head = bounds.GetCenter();
	head.z = bounds[0].z + height * BOTCOMBAT_HEAD_FRACTION;
	idVec3 pelvis = bounds.GetCenter();
	pelvis.z = bounds[0].z + height * BOTCOMBAT_PELVIS_FRACTION;
	idVec3 samples[] = {
		chest,
		head,
		pelvis,
		chest + lateral,
		chest - lateral,
		pelvis + lateral,
		pelvis - lateral
	};

	for ( int i = 0; i < (int)( sizeof( samples ) / sizeof( samples[0] ) ); i++ ) {
		idVec3 sample = samples[i];
		// Diagonal facing axes can put a shoulder sample just outside one side of
		// an axis-aligned world bound.  Keep every fallback inside the live hull.
		sample.x = idMath::ClampFloat( bounds[0].x + 0.5f, bounds[1].x - 0.5f, sample.x );
		sample.y = idMath::ClampFloat( bounds[0].y + 0.5f, bounds[1].y - 0.5f, sample.y );

		trace_t trace;
		gameLocal.TracePoint( observer, trace, eye, sample, MASK_SHOT_BOUNDINGBOX, observer );

		if ( trace.fraction >= 1.0f || BotCombatTraceEntity( trace ) == target ) {
			visiblePoint = sample;
			return true;
		}
	}

	return false;
}

/*
================
BotCombatLineOfFireIsSafe
================
*/
bool BotCombatLineOfFireIsSafe( idPlayer *shooter, idPlayer *intendedFoe,
								const idVec3 &shotOrigin, const idVec3 &aimPoint,
								float splashSafetyRadius, bool requireUsefulImpact,
								idVec3 *impactPoint ) {
	if ( impactPoint ) {
		*impactPoint = shotOrigin;
	}
	if ( !BotCombatValidFoe( shooter, intendedFoe ) ||
		 ( aimPoint - shotOrigin ).LengthSqr() <= idMath::FLOAT_EPSILON ) {
		return false;
	}

	idVec3 shotDirection = aimPoint - shotOrigin;
	const float aimDistance = shotDirection.Normalize();

	trace_t trace;
	trace.fraction = 1.0f;
	trace.endpos = aimPoint;
	idVec3 actualImpact = aimPoint;
	float impactTime = 0.0f;
	idEntity *hit = NULL;
	// Set when the sweep stopped because its flight budget ran out short of
	// the aim point: the round is still flying and its blast point is unknown.
	bool sweepCapped = false;

	botCombatShotModel_t shotModel;
	const bool projectileShot = BotCombatCurrentShotModel( shooter, shotModel );
	if ( projectileShot ) {
		const float minimumFlight = Max( MS2SEC( gameLocal.GetMSec() ), 0.01f );
		sweepCapped = aimDistance / shotModel.speed > BOTCOMBAT_MAX_SHOT_FLIGHT;
		const float flightTime = idMath::ClampFloat( minimumFlight,
			BOTCOMBAT_MAX_SHOT_FLIGHT, aimDistance / shotModel.speed );
		const idVec3 launchVelocity = shotDirection * shotModel.speed;
		const float estimatedTravel = ( shotModel.speed +
			shotModel.gravity.Length() * flightTime * 0.5f ) * flightTime;
		const int steps = idMath::ClampInt( BOTCOMBAT_MIN_TRAJECTORY_STEPS,
			BOTCOMBAT_MAX_TRAJECTORY_STEPS, idMath::Ceil( estimatedTravel / 48.0f ) );

		idVec3 previous = shotOrigin;
		float previousTime = 0.0f;
		for ( int i = 1; i <= steps; i++ ) {
			const float segmentTime = flightTime * ( (float)i / (float)steps );
			const idVec3 segmentEnd = BotCombatTrajectoryPoint( shotOrigin, launchVelocity,
				shotModel.gravity, segmentTime );

			gameLocal.TraceBounds( shooter, trace, previous, segmentEnd, shotModel.bounds,
				shotModel.clipMask, shooter );
			const float tracedTime = previousTime +
				( segmentTime - previousTime ) * idMath::ClampFloat( 0.0f, 1.0f, trace.fraction );

			if ( BotCombatShotCrossesMovingTeamMate( shooter, previous, trace.endpos,
												previousTime, tracedTime,
												shotModel.boundsRadius ) ) {
				return false;
			}

			actualImpact = trace.endpos;
			impactTime = tracedTime;
			if ( trace.fraction < 1.0f ) {
				hit = BotCombatTraceEntity( trace );
				break;
			}

			previous = segmentEnd;
			previousTime = segmentTime;
		}
	} else {
		gameLocal.TracePoint( shooter, trace, shotOrigin, aimPoint,
							 MASK_SHOT_BOUNDINGBOX, shooter );
		actualImpact = ( trace.fraction < 1.0f ) ? trace.endpos : aimPoint;
		hit = ( trace.fraction < 1.0f ) ? BotCombatTraceEntity( trace ) : NULL;
		if ( BotCombatPenetratingShotHitsTeamMate( shooter, shotOrigin, shotDirection ) ) {
			return false;
		}
	}

	if ( impactPoint ) {
		*impactPoint = actualImpact;
	}

	idPlayer *hitPlayer = ( hit && hit->IsType( idPlayer::GetClassType() ) ) ?
		static_cast<idPlayer *>( hit ) : NULL;

	// Test team safety before accepting the intended target.  A bad caller must
	// not be able to authorize friendly fire by labelling a teammate as the foe.
	if ( hitPlayer && ( hitPlayer == shooter || BotCombatSameTeam( shooter, hitPlayer ) ) ) {
		return false;
	}

	// Predict where the shooter and teammates will be when the projectile gets
	// there, and what the blast would actually cost them.  A radius test alone
	// refused every shot whose blast could reach the shooter at all: the Dark
	// Matter Gun's 768-unit radius meant it was never fired, and team mates
	// near a carrier vetoed each other's rockets with team damage off.  The
	// trade is judged instead: a little self splash is accepted when the same
	// blast hurts the target at least twice as much and the bot can afford it.
	// A sweep that ran out of flight budget has no impact yet and proves
	// nothing either way.  Exposure tracing still permits cover-assisted shots.
	idEntity *splashIgnore = BotCombatSplashIgnore( hit, intendedFoe );
	const bool impactKnown = hit != NULL || !sweepCapped;
	if ( projectileShot && impactKnown && shotModel.splashRadius > 0.0f ) {
		if ( BotCombatSplashThrowsShooter( shooter, splashIgnore, actualImpact, shotModel, impactTime ) ) {
			return false;
		}
		const float selfDamage = BotCombatSplashDamageTo( shooter, shooter, splashIgnore,
			actualImpact, shotModel, impactTime );
		if ( selfDamage > 0.0f ) {
			// A direct hit does the direct damage and nothing more: the game
			// hands RadiusDamage the struck actor as the one to skip.  Counting
			// the splash as well let a napalm shot at point blank pass as a
			// trade it was not.
			const float foeDamage = ( hitPlayer == intendedFoe ) ?
				shotModel.directDamage :
				BotCombatSplashDamageTo( shooter, intendedFoe, splashIgnore, actualImpact, shotModel, impactTime );
			const float pool = (float)Max( 0, shooter->health ) +
				(float)Max( 0, shooter->inventory.armor ) * 0.66f;
			if ( selfDamage >= pool - BOTCOMBAT_SELF_SPLASH_RESERVE ||
				 ( selfDamage > BOTCOMBAT_SELF_SPLASH_TOLERANCE && selfDamage * 2.0f > foeDamage ) ) {
				return false;
			}
		}

		if ( gameLocal.IsTeamGame() && gameLocal.serverInfo.GetBool( "si_teamDamage" ) ) {
			for ( int i = 0; i < gameLocal.numClients; i++ ) {
				idPlayer *teamMate = gameLocal.GetClientByNum( i );
				if ( !BotCombatValidClientPlayer( teamMate ) || teamMate == shooter ||
					 teamMate->GetInstance() != shooter->GetInstance() ||
					 !gameLocal.mpGame.CanPlay( teamMate ) || teamMate->spectating ||
					 teamMate->health <= 0 || !BotCombatSameTeam( shooter, teamMate ) ) {
					continue;
				}
				if ( BotCombatSplashDamageTo( shooter, teamMate, splashIgnore, actualImpact,
											   shotModel, impactTime ) > BOTCOMBAT_TEAM_SPLASH_TOLERANCE ) {
					return false;
				}
			}
		} else if ( gameLocal.IsTeamGame() ) {
			// Team damage off still leaves the push: RadiusDamage shoves every
			// hull in the blast before it asks whether it may hurt it.  A
			// rocket landing at a team-mate's feet on a ledge is a team-mate in
			// the void, so an impact right beside one is refused regardless.
			if ( BotCombatSplashThreatensTeamMate( shooter, splashIgnore, actualImpact,
					Min( shotModel.splashRadius, BOTCOMBAT_TEAM_PUSH_RADIUS ), impactTime ) ) {
				return false;
			}
		}
		// Further out the push is weaker but still real, with team damage on or
		// off: refuse a blast whose kick would throw a team-mate onto nothing.
		if ( BotCombatSplashThrowsTeamMate( shooter, splashIgnore, actualImpact, shotModel, impactTime ) ) {
			return false;
		}
	} else if ( !projectileShot && splashSafetyRadius > 0.0f ) {
		// A caller-supplied radius without a projectile model keeps the old
		// conservative proof.
		if ( BotCombatSplashThreatensPlayer( shooter, splashIgnore, actualImpact,
											 splashSafetyRadius, impactTime ) ||
			 BotCombatSplashThreatensTeamMate( shooter, splashIgnore, actualImpact,
											   splashSafetyRadius, impactTime ) ) {
			return false;
		}
	}

	if ( hitPlayer == intendedFoe ) {
		return true;
	}

	if ( requireUsefulImpact ) {
		// Nothing stopped the shot, so it travels to the exact point the bot
		// chose to aim at.  Whatever distance remains between that point and the
		// target is the aim model's own error, which is deliberate and is the
		// whole of how skill reads in play - re-testing it here is the failure
		// the design record warns about, a bot that tracks correctly and then
		// refuses to pull the trigger.  It also has to be answered before the
		// exposure test below, because a weapon with no splash carries a zero
		// radius, and BotCombatSplashThreatensPlayer answers false for a zero
		// radius unconditionally: every hitscan weapon and every splash-free
		// projectile would hold fire unless the trace happened to terminate on
		// the target hull, which the deliberate aim error usually prevents.
		if ( !hit ) {
			return true;
		}

		// The shot stops on another hostile instead.  Team safety was already
		// proven above, so that is a hit and not a wasted round; only geometry
		// and cover make an impact useless.
		if ( hitPlayer ) {
			return true;
		}

		// It terminated on the world, a mover or a crate.  Now it is only worth
		// taking if the resulting splash still reaches the foe.  Reuse the same
		// impact-time bounds and exposure model used by splash safety instead of
		// reintroducing a straight eye-to-foe ray.
		const float usefulRadius = ( projectileShot && shotModel.splashRadius > 0.0f ) ?
			shotModel.splashRadius : splashSafetyRadius;
		return BotCombatSplashThreatensPlayer( intendedFoe, BotCombatSplashIgnore( hit, NULL ),
			actualImpact, usefulRadius, impactTime );
	}

	// Hitting another hostile player or a distant piece of world is safe for
	// deliberate suppression.  The caller explicitly opted out of usefulness.
	return true;
}

struct botCombatProjectileImpact_t {
	bool		 collided;		// the sweep met something inside the horizon
	bool		 terminating;
	bool		 hitsSelf;
	float		 time;
	idVec3		 point;
	idEntity	*hit;

	botCombatProjectileImpact_t() :
		collided( false ), terminating( false ), hitsSelf( false ), time( 0.0f ),
		point( vec3_zero ), hit( NULL ) {
	}
};

/*
================
BotCombatProjectileImpactTerminates

Bounce projectiles are deliberately not extrapolated after their first bounce;
continuing the old parabola is safer than inventing a reflected direction.
Only a collision that the live definition says detonates can prove the original
path ended before reaching the bot.
================
*/
static bool BotCombatProjectileImpactTerminates( const idProjectile *projectile,
												   idEntity *hit ) {
	if ( !projectile ) {
		return false;
	}

	const bool actorHit = hit && hit->IsType( idActor::GetClassType() );
	return actorHit ? projectile->spawnArgs.GetBool( "detonate_on_actor", "0" ) :
		projectile->spawnArgs.GetBool( "detonate_on_world", "0" );
}

/*
================
BotCombatFindProjectileImpact

Sweep the live projectile hull over short parabolic segments.  This filters a
rocket safely intercepted by a wall while preserving a threat when that wall
impact is close enough for splash.  A non-terminating bounce makes future path
prediction uncertain and therefore cannot be used as proof of safety.
================
*/
static botCombatProjectileImpact_t BotCombatFindProjectileImpact(
		idProjectile *projectile, idPlayer *self, float horizon, int steps ) {
	botCombatProjectileImpact_t result;
	if ( !projectile || !projectile->GetPhysics() || horizon <= 0.0f || steps <= 0 ) {
		return result;
	}

	const idVec3 origin = projectile->GetPhysics()->GetOrigin();
	const idVec3 velocity = projectile->GetPhysics()->GetLinearVelocity();
	const idVec3 gravity = projectile->GetPhysics()->GetGravity();
	const idBounds bounds = BotCombatSymmetricProjectileBounds(
		projectile->GetPhysics()->GetBounds() );
	const int clipMask = projectile->GetPhysics()->GetClipMask();
	idVec3 previous = origin;
	float previousTime = 0.0f;

	for ( int i = 1; i <= steps; i++ ) {
		const float segmentTime = horizon * ( (float)i / (float)steps );
		const idVec3 segmentEnd = BotCombatTrajectoryPoint( origin, velocity, gravity,
			segmentTime );
		trace_t trace;
		gameLocal.TraceBounds( projectile, trace, previous, segmentEnd, bounds,
			clipMask, projectile );

		if ( trace.fraction < 1.0f ) {
			idEntity *hit = BotCombatTraceEntity( trace );
			result.collided = true;
			result.hitsSelf = ( hit == self );
			result.terminating = result.hitsSelf ||
				BotCombatProjectileImpactTerminates( projectile, hit );
			result.time = previousTime + ( segmentTime - previousTime ) *
				idMath::ClampFloat( 0.0f, 1.0f, trace.fraction );
			result.point = trace.endpos;
			result.hit = hit;
			return result;
		}

		previous = segmentEnd;
		previousTime = segmentTime;
	}

	return result;
}

/*
================
BotCombatProjectileDistanceAtTime
================
*/
static float BotCombatProjectileDistanceAtTime( const idBounds &selfBounds,
												  const idVec3 &selfVelocity,
												  const idVec3 &projectileOrigin,
												  const idVec3 &projectileVelocity,
												  const idVec3 &projectileGravity,
												  float time, idVec3 *projectilePoint ) {
	const idVec3 point = BotCombatTrajectoryPoint( projectileOrigin, projectileVelocity,
		projectileGravity, time );
	if ( projectilePoint ) {
		*projectilePoint = point;
	}
	return ( selfBounds + selfVelocity * time ).ShortestDistance( point );
}

/*
================
BotCombatFindIncomingProjectileThreat
================
*/
bool BotCombatFindIncomingProjectileThreat( idPlayer *self,
									 float lookAheadSeconds, float threatClearance,
									 idProjectile *&threatProjectile,
									 float &timeToClosest, idVec3 &closestPoint ) {
	threatProjectile	= NULL;
	timeToClosest		= 0.0f;
	closestPoint.Zero();

	if ( !BotCombatValidClientPlayer( self ) || lookAheadSeconds <= 0.0f ) {
		return false;
	}

	const idBounds selfBounds = self->GetPhysics()->GetAbsBounds();
	const idVec3 selfVelocity = self->GetPhysics()->GetLinearVelocity();
	const float playerTravel = selfVelocity.Length() * lookAheadSeconds;
	const float extraClearance = Max( 0.0f, threatClearance );

	float bestTime = idMath::INFINITY;
	float bestDistance = idMath::INFINITY;

	for ( idEntity *ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		if ( !ent->IsType( idProjectile::GetClassType() ) ) {
			continue;
		}

		idProjectile *projectile = static_cast<idProjectile *>( ent );
		if ( projectile->IsHidden() || projectile->GetInstance() != self->GetInstance() ) {
			continue;
		}
		if ( !BotCombatProjectileCanDamage( projectile ) ) {
			continue;
		}

		idEntity *ownerEntity = projectile->GetOwner();
		idPlayer *owner = ( ownerEntity && ownerEntity->IsType( idPlayer::GetClassType() ) ) ?
			static_cast<idPlayer *>( ownerEntity ) : NULL;

		if ( !BotCombatHostileProjectileOwner( self, owner ) ) {
			continue;
		}

		const idVec3 projectileOrigin = projectile->GetPhysics()->GetOrigin();
		const idVec3 projectileVelocity = projectile->GetPhysics()->GetLinearVelocity();
		const idVec3 projectileGravity = projectile->GetPhysics()->GetGravity();
		const float projectileRadius = projectile->GetPhysics()->GetBounds().GetRadius();
		const float unsafeClearance = extraClearance + projectileRadius;
		const float estimatedTravel = ( projectileVelocity.Length() +
			projectileGravity.Length() * lookAheadSeconds * 0.5f ) * lookAheadSeconds;

		// The exact blast radius costs up to three entity-def lookups per
		// round per bot per scan, and most rounds in flight are nowhere near
		// this bot.  Reject by the largest blast any stock round carries first.
		if ( selfBounds.ShortestDistance( projectileOrigin ) >
			 estimatedTravel + playerTravel + Max( unsafeClearance, BOTCOMBAT_MAX_SPLASH_REACH ) ) {
			continue;
		}
		const float splashRadius = BotCombatProjectileSplashRadius( projectile );

		// Analytic rejection first.  Over the horizon the projectile can cover at
		// most estimatedTravel and the player at most its own speed times the
		// horizon, so anything separated by more than the sum of those plus the
		// greater of direct-hit clearance and splash radius cannot become a
		// threat. A stationary grenade or a rocket hitting a nearby wall can hurt
		// the bot without the projectile itself ever entering direct-hit range.
		// Proving that with arithmetic matters because the sweep below is up to
		// BOTCOMBAT_MAX_TRAJECTORY_STEPS bounds traces against the collision
		// world, per projectile, per bot, several times a second: two players
		// trading nailgun fire at the far end of the map would otherwise cost
		// every bot on the server a full sweep of every round in flight.
		const float reach = estimatedTravel + playerTravel + Max( unsafeClearance, splashRadius );
		if ( selfBounds.ShortestDistance( projectileOrigin ) > reach ) {
			continue;
		}

		const float segmentLength = Max( 16.0f, unsafeClearance * 0.35f );
		const int steps = idMath::ClampInt( BOTCOMBAT_MIN_TRAJECTORY_STEPS,
			BOTCOMBAT_MAX_TRAJECTORY_STEPS,
			(int)idMath::Ceil( estimatedTravel / segmentLength ) );

		// A terminating wall/actor collision limits the direct-flight search.  A
		// bounce does not: without simulating the contact normal, it cannot prove
		// where the projectile will go next and is kept conservative.
		const botCombatProjectileImpact_t impact = BotCombatFindProjectileImpact(
			projectile, self, lookAheadSeconds, steps );
		const float searchHorizon = impact.terminating ?
			idMath::ClampFloat( 0.0f, lookAheadSeconds, impact.time ) : lookAheadSeconds;

		idVec3 currentPoint;
		const float currentDistance = BotCombatProjectileDistanceAtTime( selfBounds,
			selfVelocity, projectileOrigin, projectileVelocity, projectileGravity,
			0.0f, &currentPoint );
		float closestTime = 0.0f;
		float closestDistance = currentDistance;
		idVec3 projectedProjectile = currentPoint;

		// Midpoint plus endpoint sampling keeps the maximum unsampled travel below
		// half a segment; gravity is evaluated exactly at every candidate.
		for ( int i = 1; i <= steps && searchHorizon > 0.0f; i++ ) {
			const float intervalStart = searchHorizon * ( (float)( i - 1 ) / (float)steps );
			const float intervalEnd = searchHorizon * ( (float)i / (float)steps );
			const float sampleTimes[] = {
				( intervalStart + intervalEnd ) * 0.5f,
				intervalEnd
			};

			for ( int sample = 0; sample < 2; sample++ ) {
				idVec3 point;
				const float distance = BotCombatProjectileDistanceAtTime( selfBounds,
					selfVelocity, projectileOrigin, projectileVelocity, projectileGravity,
					sampleTimes[sample], &point );
				if ( distance < closestDistance ) {
					closestDistance = distance;
					closestTime = sampleTimes[sample];
					projectedProjectile = point;
				}
			}
		}

		bool projectileThreat = false;
		float candidateTime = idMath::INFINITY;
		float candidateDistance = idMath::INFINITY;
		idVec3 candidatePoint = vec3_zero;

		// The bot's own round never strikes it directly - projectiles do not
		// collide with their owner - so only its blast is a threat.
		const bool ownRound = ( owner == self );
		const idVec3 relativeOrigin = projectileOrigin - selfBounds.GetCenter();
		const idVec3 relativeVelocity = projectileVelocity - selfVelocity;
		const bool initiallyClosing = ( relativeOrigin * relativeVelocity ) < 0.0f;
		const bool trajectoryApproaches = closestTime > 0.0f &&
			closestDistance + 0.5f < currentDistance;
		if ( !ownRound && closestDistance <= unsafeClearance &&
			 ( trajectoryApproaches || ( initiallyClosing && currentDistance <= unsafeClearance ) ) ) {
			projectileThreat = true;
			candidateTime = closestTime;
			candidateDistance = closestDistance;
			candidatePoint = projectedProjectile;
		}

		// A bouncing grenade does not detonate where it first lands, but it
		// rolls and settles near there; a first bounce inside its blast radius
		// of the bot is already the time to move, not once it comes to rest.
		const bool bounceNearby = impact.collided && !impact.terminating && splashRadius > 0.0f &&
			projectile->spawnArgs.GetBool( "detonate_on_fuse", "0" );
		if ( impact.terminating || bounceNearby ) {
			const bool impactThreat = impact.hitsSelf ||
				BotCombatSplashThreatensPlayer( self, BotCombatSplashIgnore( impact.hit, NULL ), impact.point,
					splashRadius, impact.time );
			if ( impactThreat && impact.time < candidateTime ) {
				projectileThreat = true;
				candidateTime = impact.time;
				candidateDistance = ( selfBounds + selfVelocity * impact.time ).ShortestDistance(
					impact.point );
				candidatePoint = impact.point;
			}
		}

		// A grenade that has settled beside the bot has no useful closest-motion
		// solution, but its queued fuse is exactly why the bot should leave; so
		// is a napalm glob burning where it lies.  The event system does not
		// expose remaining fuse time, so use a short warning deadline for
		// nearly-resting ordnance that still hurts.
		const bool armedFuse = splashRadius > 0.0f && BotCombatProjectileLingers( projectile );
		if ( armedFuse && projectileVelocity.LengthSqr() <= Square( 8.0f ) ) {
			const float fuseWarningTime = Min( lookAheadSeconds, 0.25f );
			if ( BotCombatSplashThreatensPlayer( self, projectile, projectileOrigin,
					splashRadius, fuseWarningTime ) && fuseWarningTime < candidateTime ) {
				projectileThreat = true;
				candidateTime = fuseWarningTime;
				candidateDistance = ( selfBounds + selfVelocity * fuseWarningTime ).ShortestDistance(
					projectileOrigin );
				candidatePoint = projectileOrigin;
			}
		}

		if ( !projectileThreat ) {
			continue;
		}

		// Earliest danger is the most urgent.  Distance breaks ties between two
		// threats that mature on the same simulation tick.
		if ( candidateTime < bestTime ||
			 ( idMath::Fabs( candidateTime - bestTime ) <= idMath::FLOAT_EPSILON &&
			   candidateDistance < bestDistance ) ) {
			bestTime			= candidateTime;
			bestDistance		= candidateDistance;
			threatProjectile	= projectile;
			closestPoint		= candidatePoint;
		}
	}

	if ( !threatProjectile ) {
		return false;
	}

	timeToClosest = bestTime;
	return true;
}
