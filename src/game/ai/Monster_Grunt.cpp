
#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"

class rvMonsterGrunt : public idAI {
public:

	CLASS_PROTOTYPE( rvMonsterGrunt );

	rvMonsterGrunt ( void );
	
	void				Spawn					( void );
	void				Save					( idSaveGame *savefile ) const;
	void				Restore					( idRestoreGame *savefile );
	
	virtual void		AdjustHealthByDamage	( int damage );

protected:

	rvAIAction			actionMeleeMoveAttack;
	rvAIAction			actionChaingunAttack;

	virtual bool		CheckActions		( void );

	virtual void		OnTacticalChange	( aiTactical_t oldTactical );
	virtual void		OnDeath				( void );

private:

	int					standingMeleeNoAttackTime;
	int					rageThreshold;

	// openQ4: The Awakening's elite grunt ("q4xElite"); read from the def,
	// so not saved, and a restored grunt may pull at once
	bool				q4xElite;
	float				gravityWellMinRange;
	float				gravityWellMaxRange;
	int					nextGravityWellTime;

	void				ReadEliteSettings	( void );

	void				RageStart			( void );
	void				RageStop			( void );
	
	// Torso States
	stateResult_t		State_Torso_Enrage		( const stateParms_t& parms );
	stateResult_t		State_Torso_Pain		( const stateParms_t& parms );
	stateResult_t		State_Torso_LeapAttack	( const stateParms_t& parms );
	stateResult_t		State_Torso_GravityWell	( const stateParms_t& parms );

	// Frame commands
	stateResult_t		Frame_GravityWell		( const stateParms_t& parms );

	CLASS_STATES_PROTOTYPE ( rvMonsterGrunt );
};

CLASS_DECLARATION( idAI, rvMonsterGrunt )
END_CLASS

/*
================
rvMonsterGrunt::rvMonsterGrunt
================
*/
rvMonsterGrunt::rvMonsterGrunt ( void ) {
	standingMeleeNoAttackTime = 0;
	q4xElite = false;
	gravityWellMinRange = 0.0f;
	gravityWellMaxRange = 0.0f;
	nextGravityWellTime = 0;
}

/*
================
rvMonsterGrunt::ReadEliteSettings

openQ4: The Awakening's elite grunt pulls its enemy in with a gravity well
("gravity_well", whose frame command Frame_GravityWell does the pull) when
the enemy is in sight between "gravityWellMinRange" and
"gravityWellMaxRange" away, at most once every "gravityWellTime" seconds.
The defaults are its game code's.
================
*/
void rvMonsterGrunt::ReadEliteSettings ( void ) {
	q4xElite = spawnArgs.GetBool ( "q4xElite" );
	gravityWellMinRange = spawnArgs.GetFloat ( "gravityWellMinRange", "128" );
	gravityWellMaxRange = spawnArgs.GetFloat ( "gravityWellMaxRange", "256" );
	nextGravityWellTime = 0;
}

/*
================
rvMonsterGrunt::Spawn
================
*/
void rvMonsterGrunt::Spawn ( void ) {
	rageThreshold = spawnArgs.GetInt ( "health_rageThreshold" );

	// Custom actions
	actionMeleeMoveAttack.Init	( spawnArgs, "action_meleeMoveAttack",	NULL,				AIACTIONF_ATTACK );
	actionChaingunAttack.Init	( spawnArgs, "action_chaingunAttack",	NULL,				AIACTIONF_ATTACK );
	actionLeapAttack.Init		( spawnArgs, "action_leapAttack",		"Torso_LeapAttack",	AIACTIONF_ATTACK );

	ReadEliteSettings ( );

	// Enraged to start?
	if ( spawnArgs.GetBool ( "preinject" ) ) {
		RageStart ( );
	}	
}

/*
================
rvMonsterGrunt::Save
================
*/
void rvMonsterGrunt::Save ( idSaveGame *savefile ) const {
	actionMeleeMoveAttack.Save( savefile );
	actionChaingunAttack.Save( savefile );

	savefile->WriteInt( rageThreshold );
	savefile->WriteInt( standingMeleeNoAttackTime );
}

/*
================
rvMonsterGrunt::Restore
================
*/
void rvMonsterGrunt::Restore ( idRestoreGame *savefile ) {
	actionMeleeMoveAttack.Restore( savefile );
	actionChaingunAttack.Restore( savefile );

	savefile->ReadInt( rageThreshold );
	savefile->ReadInt( standingMeleeNoAttackTime );

	ReadEliteSettings ( );
}

/*
================
rvMonsterGrunt::RageStart
================
*/
void rvMonsterGrunt::RageStart ( void ) {
	SetShaderParm ( 6, 1 );

	// Disable non-rage actions
	actionEvadeLeft.fl.disabled = true;
	actionEvadeRight.fl.disabled = true;
	
	// Speed up animations
	animator.SetPlaybackRate ( 1.25f );

	// Disable pain
	pain.threshold = 0;

	// Start over with health when enraged
	health = spawnArgs.GetInt ( "health" );
	
	// No more going to rage
	rageThreshold = 0;
}

/*
================
rvMonsterGrunt::RageStop
================
*/
void rvMonsterGrunt::RageStop ( void ) {
	SetShaderParm ( 6, 0 );
}

/*
================
rvMonsterGrunt::CheckActions
================
*/
bool rvMonsterGrunt::CheckActions ( void ) {
	// The Awakening's elite grunt pulls a visible enemy in with its gravity well
	if ( q4xElite && enemy.fl.visible && gameLocal.time > nextGravityWellTime ) {
		if ( !enemy.range || ( enemy.range >= gravityWellMinRange && enemy.range <= gravityWellMaxRange ) ) {
			PerformAction ( "Torso_GravityWell", 0, false );
			return true;
		}
	}

	// If our health is below the rage threshold then enrage
	if ( health < rageThreshold ) { 
		PerformAction ( "Torso_Enrage", 4, true );
		return true;
	}

	// Moving melee attack?
	if ( PerformAction ( &actionMeleeMoveAttack, (checkAction_t)&idAI::CheckAction_MeleeAttack, NULL ) ) {
		return true;
	}
	
	// Default actions
	if ( CheckPainActions ( ) ) {
		return true;
	}

	if ( PerformAction ( &actionEvadeLeft,   (checkAction_t)&idAI::CheckAction_EvadeLeft, &actionTimerEvade )			 ||
			PerformAction ( &actionEvadeRight,  (checkAction_t)&idAI::CheckAction_EvadeRight, &actionTimerEvade )			 ||
			PerformAction ( &actionJumpBack,	 (checkAction_t)&idAI::CheckAction_JumpBack, &actionTimerEvade )			 ||
			PerformAction ( &actionLeapAttack,  (checkAction_t)&idAI::CheckAction_LeapAttack )	) {
		return true;
	} else if ( PerformAction ( &actionMeleeAttack, (checkAction_t)&idAI::CheckAction_MeleeAttack ) ) {
		standingMeleeNoAttackTime = 0;
		return true;
	} else {
		if ( actionMeleeAttack.status != rvAIAction::STATUS_FAIL_TIMER
			&& actionMeleeAttack.status != rvAIAction::STATUS_FAIL_EXTERNALTIMER
			&& actionMeleeAttack.status != rvAIAction::STATUS_FAIL_CHANCE )
		{//melee attack fail for any reason other than timer?
			if ( combat.tacticalCurrent == AITACTICAL_MELEE && !move.fl.moving )
			{//special case: we're in tactical melee and we're close enough to think we've reached the enemy, but he's just out of melee range!
				if ( !standingMeleeNoAttackTime )
				{
					standingMeleeNoAttackTime = gameLocal.GetTime();
				}
				else if ( standingMeleeNoAttackTime + 2500 < gameLocal.GetTime() )
				{//we've been standing still and not attacking for at least 2.5 seconds, fall back to ranged attack
					//allow ranged attack
					actionRangedAttack.fl.disabled = false;
				}
			}
		}
		if ( PerformAction ( &actionRangedAttack,(checkAction_t)&idAI::CheckAction_RangedAttack, &actionTimerRangedAttack ) ) {
			return true;
		}
	}
	return false;
}

/*
================
rvMonsterGrunt::OnDeath
================
*/
void rvMonsterGrunt::OnDeath ( void ) {
	RageStop ( );
	return idAI::OnDeath ( );
}

/*
================
rvMonsterGrunt::OnTacticalChange

Enable/Disable the ranged attack based on whether the grunt needs it
================
*/
void rvMonsterGrunt::OnTacticalChange ( aiTactical_t oldTactical ) {
	switch ( combat.tacticalCurrent ) {
		case AITACTICAL_MELEE:
			actionRangedAttack.fl.disabled = true;
			break;

		default:
			actionRangedAttack.fl.disabled = false;
			break;
	}
}

/*
=====================
rvMonsterGrunt::AdjustHealthByDamage
=====================
*/
void rvMonsterGrunt::AdjustHealthByDamage ( int damage ) {
	// Take less damage during enrage process 
	if ( rageThreshold && health < rageThreshold ) { 
		health -= (damage * 0.25f);
		return;
	}
	return idAI::AdjustHealthByDamage ( damage );
}

/*
===============================================================================

	States 

===============================================================================
*/

CLASS_STATES_DECLARATION ( rvMonsterGrunt )
	STATE ( "Torso_Enrage",		rvMonsterGrunt::State_Torso_Enrage )
	STATE ( "Torso_Pain",		rvMonsterGrunt::State_Torso_Pain )
	STATE ( "Torso_LeapAttack",	rvMonsterGrunt::State_Torso_LeapAttack )
	STATE ( "Torso_GravityWell",	rvMonsterGrunt::State_Torso_GravityWell )

	STATE ( "Frame_GravityWell",	rvMonsterGrunt::Frame_GravityWell )
END_CLASS_STATES

/*
================
rvMonsterGrunt::State_Torso_Pain
================
*/
stateResult_t rvMonsterGrunt::State_Torso_Pain ( const stateParms_t& parms ) {
	// Stop streaming pain if its time to get angry
	if ( pain.loopEndTime && health < rageThreshold ) {
		pain.loopEndTime = 0;
	}
	return idAI::State_Torso_Pain ( parms );
}

/*
================
rvMonsterGrunt::State_Torso_Enrage
================
*/
stateResult_t rvMonsterGrunt::State_Torso_Enrage ( const stateParms_t& parms ) {
	enum {
		STAGE_ANIM,
		STAGE_ANIM_WAIT,
	};
	switch ( parms.stage ) {
		case STAGE_ANIM:
			DisableAnimState ( ANIMCHANNEL_LEGS );
			PlayAnim ( ANIMCHANNEL_TORSO, "anger", parms.blendFrames );
			return SRESULT_STAGE ( STAGE_ANIM_WAIT );
		
		case STAGE_ANIM_WAIT:
			if ( AnimDone ( ANIMCHANNEL_TORSO, 4 ) ) {
				RageStart ( );
				return SRESULT_DONE;
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}


/*
================
rvMonsterGrunt::State_Torso_LeapAttack
================
*/
stateResult_t rvMonsterGrunt::State_Torso_LeapAttack ( const stateParms_t& parms ) {
	enum {
		STAGE_ANIM,
		STAGE_ANIM_WAIT,
	};
	switch ( parms.stage ) {
		case STAGE_ANIM:
			DisableAnimState ( ANIMCHANNEL_LEGS );
			lastAttackTime = 0;
			// Play the action animation
			PlayAnim ( ANIMCHANNEL_TORSO, animator.GetAnim ( actionAnimNum )->FullName ( ), parms.blendFrames );
			return SRESULT_STAGE ( STAGE_ANIM_WAIT );
		
		case STAGE_ANIM_WAIT:
			if ( AnimDone ( ANIMCHANNEL_TORSO, parms.blendFrames ) ) {
				// If we missed our leap attack get angry
				if ( !lastAttackTime && rageThreshold ) {
					PostAnimState ( ANIMCHANNEL_TORSO, "Torso_Enrage", parms.blendFrames );
				}
				return SRESULT_DONE;
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
rvMonsterGrunt::State_Torso_GravityWell
================
*/
stateResult_t rvMonsterGrunt::State_Torso_GravityWell ( const stateParms_t& parms ) {
	enum {
		STAGE_ANIM,
		STAGE_ANIM_WAIT,
	};
	switch ( parms.stage ) {
		case STAGE_ANIM:
			TurnToward ( enemy.lastKnownPosition );
			DisableAnimState ( ANIMCHANNEL_LEGS );
			PlayAnim ( ANIMCHANNEL_TORSO, "gravity_well", parms.blendFrames );
			nextGravityWellTime = gameLocal.time + SEC2MS ( spawnArgs.GetFloat ( "gravityWellTime", "2" ) );
			return SRESULT_STAGE ( STAGE_ANIM_WAIT );

		case STAGE_ANIM_WAIT:
			if ( AnimDone ( ANIMCHANNEL_TORSO, 4 ) ) {
				return SRESULT_DONE;
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
rvMonsterGrunt::Frame_GravityWell

Yanks the enemy towards the grunt, faster the farther it is (6.1 units a
second for every unit of distance), when it is still within reach.
================
*/
stateResult_t rvMonsterGrunt::Frame_GravityWell ( const stateParms_t& parms ) {
	idEntity* enemyEnt = enemy.ent;
	if ( enemyEnt && enemy.range <= gravityWellMaxRange ) {
		const idVec3 toEnemy = enemyEnt->GetPhysics()->GetOrigin() - physicsObj.GetOrigin();
		enemyEnt->GetPhysics()->SetLinearVelocity( toEnemy * -6.1f );
	}
	return SRESULT_OK;
}
