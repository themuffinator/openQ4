#include "../Game_local.h"

/*
===============================================================================

	DOTEntity

	Damage over time. A damage def that names "spawnclass" "DOTEntity" does
	not hurt an animated entity directly: the game spawns this entity from the
	def instead (idEntity::SpawnDamageEntity), with the victim, the attacker
	and the joint that was hit in its spawn args. Once a second, for
	"lifetime" milliseconds, it takes "damagePerSecond" off the victim, whose
	pain animations follow the def's "pain" type. The def's "fx_dmgeffect"
	plays on the joint that was hit ("chest" when the hit had no joint).

	The player's squad is immune: on a victim of the local player's team the
	entity removes itself without doing anything, as it does in the expansion.

	The expansion looked "fx_dmgeffect" up on the victim, where no def sets it,
	so its first flame never showed; the damage def is where the key lives.

	A victim burns once. The expansion spawned a new entity for every hit, and
	one goob shot - its glob, the splash and the six globs it bursts into -
	lit up to eight fires on a victim, each spreading over the whole body and
	together dealing about 350 damage. Here a second hit on a victim that is
	already burning feeds the burn it has instead: the burn lasts "lifetime"
	from the newest hit, and grows by the new hit's "damagePerSecond", up to
	DOT_MAX_STACKS times its own. A dead victim keeps burning but takes no
	more damage.

===============================================================================
*/

// how many hits' worth of damage one burn can carry
static const int DOT_MAX_STACKS = 3;

class DOTEntity : public idEntity {
public:
	CLASS_PROTOTYPE( DOTEntity );

							DOTEntity( void );

	void					Spawn( void );
	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

	virtual void			Think( void );

protected:
							// the victim, or NULL (and the entity on its way out) once there is nothing left to hurt
	idAnimatedEntity *		CheckVictim( void );
	void					PlayDamageEffect( idAnimatedEntity *ent, jointHandle_t joint );

	idEntityPtr<idAnimatedEntity>	victim;
	jointHandle_t			hitJoint;

	bool					merged;					// fed a burn the victim already had, and is going away (not saved)

private:
							// feeds a burn of this class the victim already has; true when it did
	bool					FeedExistingBurn( idAnimatedEntity *target );
	void					DealDamage( idAnimatedEntity *ent );

	idEntityPtr<idEntity>	attacker;
	float					damagePerSecond;
	int						endTime;
	int						lastDamageTime;
};

CLASS_DECLARATION( idEntity, DOTEntity )
END_CLASS

/*
================
DOTEntity::DOTEntity
================
*/
DOTEntity::DOTEntity( void ) {
	hitJoint = INVALID_JOINT;
	merged = false;
	damagePerSecond = 0.0f;
	endTime = 0;
	lastDamageTime = 0;
}

/*
================
DOTEntity::FeedExistingBurn
================
*/
bool DOTEntity::FeedExistingBurn( idAnimatedEntity *target ) {
	for ( idEntity *ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		if ( ent == this || ent->GetType() != GetType() ) {
			continue;
		}
		DOTEntity *burn = static_cast<DOTEntity *>( ent );
		if ( burn->merged || burn->victim.GetEntity() != target || burn->endTime <= gameLocal.time ) {
			continue;
		}

		const float maxRate = burn->spawnArgs.GetFloat( "damagePerSecond" ) * DOT_MAX_STACKS;
		burn->damagePerSecond = Min( burn->damagePerSecond + damagePerSecond, Max( maxRate, burn->damagePerSecond ) );
		burn->endTime = Max( burn->endTime, endTime );
		if ( g_debugDamage.GetBool() ) {
			gameLocal.Printf( "%s '%s' feeds '%s' on '%s': %.0f damage a second\n", GetClassname(), name.c_str(), burn->name.c_str(), target->name.c_str(), burn->damagePerSecond );
		}
		return true;
	}
	return false;
}

/*
================
DOTEntity::Spawn
================
*/
void DOTEntity::Spawn( void ) {
	idEntity *ent = gameLocal.FindEntity( spawnArgs.GetString( "damageVictim" ) );
	if ( ent != NULL && ent->IsType( idAnimatedEntity::GetClassType() ) ) {
		victim = static_cast<idAnimatedEntity *>( ent );
	}
	attacker = gameLocal.FindEntity( spawnArgs.GetString( "damageAttacker" ) );

	damagePerSecond = spawnArgs.GetFloat( "damagePerSecond" );
	endTime = gameLocal.time + spawnArgs.GetInt( "lifetime" );
	lastDamageTime = gameLocal.time;

	idAnimatedEntity *target = victim.GetEntity();
	if ( target == NULL ) {
		gameLocal.Warning( "%s '%s' has no victim", GetClassname(), name.c_str() );
		PostEventMS( &EV_Remove, 0 );
		return;
	}

	// a victim that is already burning burns harder instead of twice
	if ( FeedExistingBurn( target ) ) {
		merged = true;
		PostEventMS( &EV_Remove, 0 );
		return;
	}

	// a hit with no joint (splash damage, say) burns the chest
	idAnimator *animator = target->GetAnimator();
	hitJoint = (jointHandle_t)spawnArgs.GetInt( "damageLocation", "-1" );
	if ( hitJoint < 0 || hitJoint >= animator->NumJoints() ) {
		hitJoint = animator->GetJointHandle( "chest" );
	}

	PlayDamageEffect( target, hitJoint );
	BecomeActive( TH_THINK );
}

/*
================
DOTEntity::Save
================
*/
void DOTEntity::Save( idSaveGame *savefile ) const {
	victim.Save( savefile );
	savefile->WriteJoint( hitJoint );
	attacker.Save( savefile );
	savefile->WriteFloat( damagePerSecond );
	savefile->WriteInt( endTime );
	savefile->WriteInt( lastDamageTime );
}

/*
================
DOTEntity::Restore
================
*/
void DOTEntity::Restore( idRestoreGame *savefile ) {
	victim.Restore( savefile );
	savefile->ReadJoint( hitJoint );
	attacker.Restore( savefile );
	savefile->ReadFloat( damagePerSecond );
	savefile->ReadInt( endTime );
	savefile->ReadInt( lastDamageTime );
}

/*
================
DOTEntity::CheckVictim
================
*/
idAnimatedEntity *DOTEntity::CheckVictim( void ) {
	idAnimatedEntity *ent = victim.GetEntity();
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( ent != NULL && player != NULL && ent->IsType( idActor::GetClassType() ) && static_cast<idActor *>( ent )->team == player->team ) {
		ent = NULL;
	}
	if ( ent == NULL ) {
		PostEventMS( &EV_Remove, 0 );
	}
	return ent;
}

/*
================
DOTEntity::PlayDamageEffect
================
*/
void DOTEntity::PlayDamageEffect( idAnimatedEntity *ent, jointHandle_t joint ) {
	const idDecl *effect = gameLocal.GetEffect( spawnArgs, "fx_dmgeffect" );
	if ( effect != NULL ) {
		ent->PlayEffect( effect, joint, vec3_origin, mat3_identity, false, vec3_origin, true );
	}
}

/*
================
DOTEntity::DealDamage

Ticks once a whole second has gone by, then hurts the victim for every whole
second since the last tick.
================
*/
void DOTEntity::DealDamage( idAnimatedEntity *ent ) {
	int elapsed = gameLocal.time - lastDamageTime;
	if ( elapsed < 1000 ) {
		return;
	}

	// a corpse burns on without being hurt again
	if ( ent->health <= 0 && ent->IsType( idActor::GetClassType() ) ) {
		lastDamageTime = gameLocal.time;
		if ( gameLocal.time >= endTime ) {
			PostEventMS( &EV_Remove, 0 );
		}
		return;
	}

	if ( ent->IsType( idActor::GetClassType() ) ) {
		static_cast<idActor *>( ent )->SetPainType( spawnArgs.GetString( "pain" ) );
	}
	int damage = idMath::FtoiFast( idMath::Rint( idMath::Rint( MS2SEC( elapsed ) ) * damagePerSecond ) );
	if ( g_debugDamage.GetBool() ) {
		gameLocal.Printf( "%s '%s': %d damage to '%s' (health %d)\n", GetClassname(), name.c_str(), damage, ent->name.c_str(), ent->health );
	}
	ent->ApplyDamage( NULL, attacker.GetEntity(), vec3_origin, damage, hitJoint );
	lastDamageTime = gameLocal.time;

	if ( gameLocal.time >= endTime ) {
		PostEventMS( &EV_Remove, 0 );
	}
}

/*
================
DOTEntity::Think
================
*/
void DOTEntity::Think( void ) {
	idAnimatedEntity *ent = CheckVictim();
	if ( ent != NULL ) {
		DealDamage( ent );
	}
}

/*
===============================================================================

	FireDOTEntity

	A DOTEntity whose fire spreads over its victim. Every "firespreadtime"
	milliseconds, up to "numfirespreads" times, the fire moves from the joint
	it last lit to the nearest joint that is not burning yet, lighting it with
	the def's "fx_dmgeffect". Leaf joints (finger tips, the top of the head)
	are skipped. This is what sets the goob gun's victims alight.

===============================================================================
*/

class FireDOTEntity : public DOTEntity {
public:
	CLASS_PROTOTYPE( FireDOTEntity );

							FireDOTEntity( void );

	void					Spawn( void );
	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

	virtual void			Think( void );

private:
	void					SpreadFire( idAnimatedEntity *ent );

	int						numSpreads;
	int						spreadInterval;
	int						nextSpreadTime;
	int						spreads;
	jointHandle_t			fireJoint;
	idList<int>				burningJoints;
};

CLASS_DECLARATION( DOTEntity, FireDOTEntity )
END_CLASS

/*
================
FireDOTEntity::FireDOTEntity
================
*/
FireDOTEntity::FireDOTEntity( void ) {
	numSpreads = 0;
	spreadInterval = 0;
	nextSpreadTime = 0;
	spreads = 0;
	fireJoint = INVALID_JOINT;
}

/*
================
FireDOTEntity::Spawn
================
*/
void FireDOTEntity::Spawn( void ) {
	if ( merged ) {
		return;
	}
	numSpreads = spawnArgs.GetInt( "numfirespreads" );
	spreadInterval = spawnArgs.GetInt( "firespreadtime" );
	nextSpreadTime = gameLocal.time + spreadInterval;

	// the fire starts where DOTEntity lit it
	fireJoint = hitJoint;
	if ( fireJoint != INVALID_JOINT ) {
		burningJoints.Append( fireJoint );
	}
}

/*
================
FireDOTEntity::Save
================
*/
void FireDOTEntity::Save( idSaveGame *savefile ) const {
	savefile->WriteInt( numSpreads );
	savefile->WriteInt( spreadInterval );
	savefile->WriteInt( nextSpreadTime );
	savefile->WriteInt( spreads );
	savefile->WriteJoint( fireJoint );
	savefile->WriteInt( burningJoints.Num() );
	for ( int i = 0; i < burningJoints.Num(); i++ ) {
		savefile->WriteInt( burningJoints[ i ] );
	}
}

/*
================
FireDOTEntity::Restore
================
*/
void FireDOTEntity::Restore( idRestoreGame *savefile ) {
	savefile->ReadInt( numSpreads );
	savefile->ReadInt( spreadInterval );
	savefile->ReadInt( nextSpreadTime );
	savefile->ReadInt( spreads );
	savefile->ReadJoint( fireJoint );
	int num;
	savefile->ReadInt( num );
	// A burning joint is a joint of the victim, and idAnimator::Restore accepts
	// at most 4096 joints.
	if ( num < 0 || num > 4096 ) {
		savefile->Error( "FireDOTEntity::Restore: invalid burning joint count %d", num );
	}
	burningJoints.SetNum( num );
	for ( int i = 0; i < num; i++ ) {
		savefile->ReadInt( burningJoints[ i ] );
	}
}

/*
================
FireDOTEntity::SpreadFire
================
*/
void FireDOTEntity::SpreadFire( idAnimatedEntity *ent ) {
	spreads++;

	idVec3 from;
	idMat3 axis;
	if ( fireJoint == INVALID_JOINT || !ent->GetJointWorldTransform( fireJoint, gameLocal.time, from, axis ) ) {
		gameLocal.Warning( "%s '%s' can't find the joint its fire spreads from", GetClassname(), name.c_str() );
		PostEventMS( &EV_Remove, 0 );
		return;
	}

	idAnimator *animator = ent->GetAnimator();
	jointHandle_t nearest = INVALID_JOINT;
	float nearestDistSqr = 0.0f;
	for ( int i = animator->NumJoints() - 1; i > 0; i-- ) {
		jointHandle_t joint = (jointHandle_t)i;
		if ( joint == fireJoint || animator->GetFirstChild( joint ) == joint || burningJoints.FindIndex( i ) >= 0 ) {
			continue;
		}
		idVec3 origin;
		ent->GetJointWorldTransform( joint, gameLocal.time, origin, axis );
		float distSqr = ( origin - from ).LengthSqr();
		if ( nearest == INVALID_JOINT || distSqr < nearestDistSqr ) {
			nearest = joint;
			nearestDistSqr = distSqr;
		}
	}
	if ( nearest == INVALID_JOINT ) {
		return;
	}

	PlayDamageEffect( ent, nearest );
	fireJoint = nearest;
	burningJoints.Append( nearest );
}

/*
================
FireDOTEntity::Think
================
*/
void FireDOTEntity::Think( void ) {
	idAnimatedEntity *ent = CheckVictim();
	if ( ent == NULL ) {
		return;
	}
	if ( spreads < numSpreads && gameLocal.time > nextSpreadTime ) {
		SpreadFire( ent );
		nextSpreadTime = gameLocal.time + spreadInterval;
	}
	DOTEntity::Think();
}
