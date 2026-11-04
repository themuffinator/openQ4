#include "../../Game_local.h"
#include "../../ai/Monster_Turret.h"

/*
===============================================================================

	riMonsterTurret

	The expansion's version of rvMonsterTurret. Awakening turret spawns become one
	(the campaign-scoped substitution below), and without the keys that switch its
	additions on it behaves exactly like the stock turret.

	"dynamicAccuracy": every burst starts wild and tightens as it goes: over
	"accuracyLerpTime" milliseconds its blaster spread falls from
	"dynamicAccuracyMax" to "dynamicAccuracyMin" (the m01 beach turrets).

	"delayedTracking": it lets its target get "10 degrees" away before it
	turns ("snd_turn"), stops with "snd_stop", and only fires again once it
	is facing its target and "lockDelay" seconds have passed since it stopped
	(the m04 prison turrets).

	"scanAnim": it sweeps until an enemy is visible, then drops back to
	"idle" to track it (the prison security cameras).

===============================================================================
*/

class riMonsterTurret : public rvMonsterTurret {
public:
	CLASS_PROTOTYPE( riMonsterTurret );

							riMonsterTurret( void );

	void					Spawn( void );
	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

protected:
	virtual bool			CheckActions( void );

private:
	void					ReadSpawnArgs( void );
	bool					UpdateTracking( void );

	bool					dynamicAccuracy;
	int						accuracyLerpTime;
	float					accuracyMin;
	float					accuracyMax;
	int						burstStartTime;

	bool					turning;
	int						holdFireUntil;

	stateResult_t			State_Combat( const stateParms_t &parms );
	stateResult_t			State_Torso_BlasterAttack( const stateParms_t &parms );

	CLASS_STATES_PROTOTYPE( riMonsterTurret );
};

CLASS_DECLARATION( rvMonsterTurret, riMonsterTurret )
END_CLASS

CLASS_STATES_DECLARATION( riMonsterTurret )
	STATE( "State_Combat",			riMonsterTurret::State_Combat )
	STATE( "Torso_BlasterAttack",	riMonsterTurret::State_Torso_BlasterAttack )
END_CLASS_STATES

SPAWNCLASS_SUBSTITUTION_FOR_GAME( "q4xbase", rvMonsterTurret, riMonsterTurret )

static const float TURRET_TRACKING_SLACK = 10.0f;

/*
================
riMonsterTurret::riMonsterTurret
================
*/
riMonsterTurret::riMonsterTurret( void ) {
	dynamicAccuracy = false;
	accuracyLerpTime = 0;
	accuracyMin = 0.0f;
	accuracyMax = 0.0f;
	burstStartTime = 0;
	turning = false;
	holdFireUntil = 0;
}

/*
================
riMonsterTurret::ReadSpawnArgs
================
*/
void riMonsterTurret::ReadSpawnArgs( void ) {
	dynamicAccuracy = spawnArgs.GetBool( "dynamicAccuracy" );
	accuracyLerpTime = spawnArgs.GetInt( "accuracyLerpTime", "4" );
	accuracyMin = spawnArgs.GetFloat( "dynamicAccuracyMin", "0.25" );
	accuracyMax = spawnArgs.GetFloat( "dynamicAccuracyMax", "5.0" );
}

/*
================
riMonsterTurret::Spawn
================
*/
void riMonsterTurret::Spawn( void ) {
	ReadSpawnArgs();
}

/*
================
riMonsterTurret::Save
================
*/
void riMonsterTurret::Save( idSaveGame *savefile ) const {
	savefile->WriteInt( burstStartTime );
	savefile->WriteBool( turning );
	savefile->WriteInt( holdFireUntil );
}

/*
================
riMonsterTurret::Restore
================
*/
void riMonsterTurret::Restore( idRestoreGame *savefile ) {
	savefile->ReadInt( burstStartTime );
	savefile->ReadBool( turning );
	savefile->ReadInt( holdFireUntil );
	ReadSpawnArgs();
}

/*
================
riMonsterTurret::UpdateTracking

Delayed tracking: true once the turret faces its target and may fire.
================
*/
bool riMonsterTurret::UpdateTracking( void ) {
	const bool facing = move.ideal_yaw == move.current_yaw;

	if ( !turning && idMath::Fabs( move.ideal_yaw - move.current_yaw ) > TURRET_TRACKING_SLACK ) {
		StartSound( "snd_turn", SND_CHANNEL_WEAPON, 0, false, NULL );
		turning = true;
	} else if ( turning && facing ) {
		StopSound( SND_CHANNEL_WEAPON, false );
		StartSound( "snd_stop", SND_CHANNEL_WEAPON, 0, false, NULL );
		holdFireUntil = gameLocal.time + SEC2MS( spawnArgs.GetFloat( "lockDelay", "1.5" ) );
		turning = false;
	}

	return facing && gameLocal.time >= holdFireUntil;
}

/*
================
riMonsterTurret::CheckActions
================
*/
bool riMonsterTurret::CheckActions( void ) {
	if ( !spawnArgs.GetBool( "delayedTracking" ) ) {
		return rvMonsterTurret::CheckActions();
	}
	if ( UpdateTracking() && PerformAction( &actionBlasterAttack, (checkAction_t)&idAI::CheckAction_RangedAttack, &actionTimerRangedAttack ) ) {
		return true;
	}
	return idAI::CheckActions();
}

/*
================
riMonsterTurret::State_Combat
================
*/
stateResult_t riMonsterTurret::State_Combat( const stateParms_t &parms ) {
	const char *scanAnim = spawnArgs.GetString( "scanAnim" );

	if ( !enemy.ent ) {
		CheckForEnemy( true );
	}

	// A teammate can supply an unseen enemy before our first combat frame.
	// Always initialize the pose, and keep sweeping until there is something
	// this camera can actually see. Do not restart the cycle on every frame.
	if ( scanAnim[ 0 ] ) {
		const bool watching = IsEnemyVisible();
		const char *anim = watching ? "idle" : scanAnim;
		const idAnim *current = animator.CurrentAnim( ANIMCHANNEL_ALL )->Anim();
		if ( current == NULL || idStr::Icmp( current->Name(), anim ) ) {
			animator.CycleAnim( ANIMCHANNEL_ALL, animator.GetAnim( anim ), gameLocal.time, 0 );
		}
		if ( !watching ) {
			return SRESULT_WAIT;
		}
	}

	FaceEnemy();
	UpdateAction();
	return SRESULT_WAIT;
}

/*
================
riMonsterTurret::State_Torso_BlasterAttack
================
*/
stateResult_t riMonsterTurret::State_Torso_BlasterAttack( const stateParms_t &parms ) {
	enum {
		STAGE_INIT,
		STAGE_FIRE,
		STAGE_WAIT
	};
	switch ( parms.stage ) {
		case STAGE_INIT:
			DisableAnimState( ANIMCHANNEL_LEGS );
			shots = ( minShots + gameLocal.random.RandomInt( maxShots - minShots + 1 ) ) * combat.aggressiveScale;
			burstStartTime = gameLocal.time;
			return SRESULT_STAGE( STAGE_FIRE );

		case STAGE_FIRE:
			PlayAnim( ANIMCHANNEL_TORSO, "range_attack", 2 );
			return SRESULT_STAGE( STAGE_WAIT );

		case STAGE_WAIT:
			if ( AnimDone( ANIMCHANNEL_TORSO, 2 ) ) {
				if ( --shots <= 0 ) {
					return SRESULT_DONE;
				}
				return SRESULT_STAGE( STAGE_FIRE );
			}
			if ( dynamicAccuracy ) {
				const float settled = accuracyLerpTime > 0 ? idMath::ClampFloat( 0.0f, 1.0f, (float)( gameLocal.time - burstStartTime ) / accuracyLerpTime ) : 1.0f;
				spawnArgs.SetFloat( "attack_blaster_accuracy", ( 1.0f - settled ) * ( accuracyMax - accuracyMin ) + accuracyMin );
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}
