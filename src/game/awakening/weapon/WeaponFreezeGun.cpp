#include "../../Game_local.h"
#include "../../Weapon.h"

/*
===============================================================================

	rvWeaponFreezeGun

	A short-range freezing spray built on the hyperblaster's body: every shot
	throws five short-fused projectile_freezegun bolts. Its damage defs carry
	"filter_freeze", which puts out func_fire_volume fires, and its projectile
	carries "freezeEnemies". Its battery recharges by itself: idPlayer adds the
	def's ammoRegenStep every ammoRegenTime ms up to the starting charge, and
	each shot holds that recharge back for FREEZEGUN_RECHARGE_DELAY ms, so a
	sustained spray still drains it. Shader parm 6 on the view model is the
	drained fraction of that charge (0 full, 1 empty): openQ4's override of
	the coolant-tube material dims the tubes' glow with it.

===============================================================================
*/

class rvWeaponFreezeGun : public rvWeapon {
public:
	CLASS_PROTOTYPE( rvWeaponFreezeGun );

							rvWeaponFreezeGun( void );

	virtual void			Spawn( void );
	virtual void			Think( void );
	void					PreSave( void );
	void					PostSave( void );

private:
	void					UpdateBatteryGlow( void );

	float					shownDrain;	// last parm 6 written; not saved, rewritten after a load

	stateResult_t			State_Idle( const stateParms_t &parms );
	stateResult_t			State_Fire( const stateParms_t &parms );
	stateResult_t			State_Reload( const stateParms_t &parms );

	CLASS_STATES_PROTOTYPE( rvWeaponFreezeGun );
};

CLASS_DECLARATION( rvWeapon, rvWeaponFreezeGun )
END_CLASS

CLASS_STATES_DECLARATION( rvWeaponFreezeGun )
	STATE( "Idle",		rvWeaponFreezeGun::State_Idle )
	STATE( "Fire",		rvWeaponFreezeGun::State_Fire )
	STATE( "Reload",	rvWeaponFreezeGun::State_Reload )
END_CLASS_STATES

static const int FREEZEGUN_SPARM_BATTERY = 6;
static const int FREEZEGUN_BOLTS_PER_SHOT = 5;
static const int FREEZEGUN_RECHARGE_DELAY = 1000;

/*
================
rvWeaponFreezeGun::rvWeaponFreezeGun
================
*/
rvWeaponFreezeGun::rvWeaponFreezeGun( void ) {
	shownDrain = -1.0f;
}

/*
================
rvWeaponFreezeGun::Spawn
================
*/
void rvWeaponFreezeGun::Spawn( void ) {
	SetState( "Raise", 0 );
}

/*
================
rvWeaponFreezeGun::PreSave
================
*/
void rvWeaponFreezeGun::PreSave( void ) {
	SetState( "Idle", 4 );
	StopSound( SND_CHANNEL_WEAPON, false );
	StopSound( SND_CHANNEL_BODY, false );
	StopSound( SND_CHANNEL_ITEM, false );
	StopSound( SND_CHANNEL_ANY, false );
}

/*
================
rvWeaponFreezeGun::PostSave
================
*/
void rvWeaponFreezeGun::PostSave( void ) {
}

/*
================
rvWeaponFreezeGun::UpdateBatteryGlow
================
*/
void rvWeaponFreezeGun::UpdateBatteryGlow( void ) {
	if ( viewModel == NULL ) {
		return;
	}
	float charge = 1.0f;
	if ( ClipSize() ) {
		charge = (float)AmmoInClip() / ClipSize();
	} else {
		// the recharge limit: the def's starting charge
		const idKeyValue *start = spawnArgs.MatchPrefix( "inv_start_ammo" );
		const int full = start ? atoi( start->GetValue() ) : 0;
		if ( full > 0 ) {
			charge = (float)AmmoAvailable() / full;
		}
	}
	const float drain = 1.0f - idMath::ClampFloat( 0.0f, 1.0f, charge );
	if ( drain != shownDrain ) {
		shownDrain = drain;
		viewModel->SetShaderParm( FREEZEGUN_SPARM_BATTERY, drain );
	}
}

/*
================
rvWeaponFreezeGun::Think
================
*/
void rvWeaponFreezeGun::Think( void ) {
	rvWeapon::Think();
	UpdateBatteryGlow();
}

/*
================
rvWeaponFreezeGun::State_Idle
================
*/
stateResult_t rvWeaponFreezeGun::State_Idle( const stateParms_t &parms ) {
	enum {
		STAGE_INIT,
		STAGE_WAIT
	};
	switch ( parms.stage ) {
		case STAGE_INIT:
			SetStatus( AmmoAvailable() ? WP_READY : WP_OUTOFAMMO );
			UpdateBatteryGlow();
			PlayCycle( ANIMCHANNEL_ALL, "idle", parms.blendFrames );
			return SRESULT_STAGE( STAGE_WAIT );

		case STAGE_WAIT:
			if ( wsfl.lowerWeapon ) {
				SetState( "Lower", 4 );
				return SRESULT_DONE;
			}
			if ( !ClipSize() ) {
				// the battery recharges while idle
				const weaponStatus_t idleStatus = AmmoAvailable() ? WP_READY : WP_OUTOFAMMO;
				if ( status != idleStatus ) {
					SetStatus( idleStatus );
				}
				if ( gameLocal.time > nextAttackTime && wsfl.attack && AmmoAvailable() ) {
					SetState( "Fire", 0 );
					return SRESULT_DONE;
				}
				return SRESULT_WAIT;
			}
			if ( gameLocal.time > nextAttackTime && wsfl.attack && AmmoInClip() ) {
				SetState( "Fire", 0 );
				return SRESULT_DONE;
			}
			if ( ( wsfl.attack && AutoReload() && !AmmoInClip() && AmmoAvailable() ) ||
				 wsfl.netReload ||
				 ( wsfl.reload && AmmoInClip() < ClipSize() && AmmoAvailable() > AmmoInClip() ) ) {
				SetState( "Reload", 4 );
				return SRESULT_DONE;
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
rvWeaponFreezeGun::State_Fire
================
*/
stateResult_t rvWeaponFreezeGun::State_Fire( const stateParms_t &parms ) {
	enum {
		STAGE_INIT,
		STAGE_WAIT
	};
	switch ( parms.stage ) {
		case STAGE_INIT:
			nextAttackTime = gameLocal.time + ( fireRate * owner->PowerUpModifier( PMOD_FIRERATE ) );
			Attack( false, FREEZEGUN_BOLTS_PER_SHOT, spread, 0, 1.0f );
			if ( ammoType >= 0 && ammoType < MAX_AMMO ) {
				owner->nextAmmoRegenPulse[ ammoType ] = gameLocal.time + FREEZEGUN_RECHARGE_DELAY;
			}
			UpdateBatteryGlow();
			PlayAnim( ANIMCHANNEL_ALL, "fire", 0 );
			return SRESULT_STAGE( STAGE_WAIT );

		case STAGE_WAIT:
			if ( wsfl.attack && AmmoInClip() && !wsfl.lowerWeapon ) {
				if ( gameLocal.time >= nextAttackTime ) {
					SetState( "Fire", 0 );
					return SRESULT_DONE;
				}
				// keep the spray going rather than dropping to idle between shots
				return SRESULT_WAIT;
			}
			if ( AnimDone( ANIMCHANNEL_ALL, 0 ) ) {
				SetState( "Idle", 0 );
				return SRESULT_DONE;
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
rvWeaponFreezeGun::State_Reload
================
*/
stateResult_t rvWeaponFreezeGun::State_Reload( const stateParms_t &parms ) {
	enum {
		STAGE_INIT,
		STAGE_WAIT
	};
	switch ( parms.stage ) {
		case STAGE_INIT:
			if ( wsfl.netReload ) {
				wsfl.netReload = false;
			} else {
				NetReload();
			}
			SetStatus( WP_RELOAD );
			PlayAnim( ANIMCHANNEL_ALL, "reload", parms.blendFrames );
			return SRESULT_STAGE( STAGE_WAIT );

		case STAGE_WAIT:
			if ( AnimDone( ANIMCHANNEL_ALL, 4 ) ) {
				AddToClip( ClipSize() );
				SetState( "Idle", 4 );
				return SRESULT_DONE;
			}
			if ( wsfl.lowerWeapon ) {
				SetState( "Lower", 4 );
				return SRESULT_DONE;
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}
