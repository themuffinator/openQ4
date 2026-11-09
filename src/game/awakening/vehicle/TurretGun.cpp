/*
 * The Awakening's mannable chaingun turret; see LICENSES/QUAKE-4-SDK-EULA.rtf.
 */

#include "../../Game_local.h"

/*
===============================================================================

	idVehicleTurretGun

	openQ4's weapon for The Awakening's Strogg chaingun turret (m01's trench
	turret and m04's prison turrets). The expansion's gun fired at full rate
	and full accuracy for as long as the trigger was held; this one heats.

	Every round a player fires adds "heatPerShot". Once the trigger has been
	off for "heatCoolDelay" seconds the barrel sheds "heatDissipationPerSecond".
	At "heatTolerance" the gun overheats: it vents ("fx_overheat" at the muzzle,
	"snd_overheat", which loops), sheds "overheatDissipationPerSecond" and will
	not fire ("snd_overheatFire" on each pull of the trigger) until it is back
	at "safeHeatLevel" ("snd_overheatRecover").

	Heat also opens the spread, from "spreadCold" to the def's "spread", and a
	zoomed gunner's rounds spread by "zoomSpreadScale" as much. A gun crewed by
	a script's AI (m04) never heats and keeps the def's spread.

	The vehicle's HUD reads vehicle_heat (0..1 of the tolerance),
	vehicle_overheat, vehicle_heatwarn (the fraction "heatWarnLevel" from which
	the HUD warns) and vehicle_zoom.

===============================================================================
*/

class idVehicleTurretGun : public rvVehicleWeapon {
public:
	CLASS_PROTOTYPE( idVehicleTurretGun );

							idVehicleTurretGun( void );

	void					Spawn( void );
	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

	virtual void			RunPostPhysics( void );
	virtual void			Activate( bool activate );
	virtual bool			Fire( void );
	virtual void			UpdateHUD( idUserInterface *gui );

private:
	void					ReadHeatParms( void );
	idPlayer *				GetPlayerDriver( void ) const;
	void					Cool( void );
	void					Overheat( void );
	float					HeatFraction( void ) const;

	// tuning, from the def (not saved)
	float					spreadCold;
	float					spreadHot;
	float					zoomSpreadScale;
	float					heatPerShot;
	float					heatTolerance;
	float					safeHeatLevel;
	int						heatCoolDelay;
	float					heatDissipation;
	float					overheatDissipation;
	float					heatWarnLevel;
	const idSoundShader *	overheatSound;

	// state (saved)
	float					heat;
	bool					overheated;
	int						lastHeatTime;
	int						lastShotTime;

	// presentation (not saved): a loaded overheated gun vents silently
	bool					attackHeld;
	bool					ventSoundOn;
};

CLASS_DECLARATION( rvVehicleWeapon, idVehicleTurretGun )
END_CLASS

static idCVar g_debugTurretGun( "g_debugTurretGun", "0", CVAR_GAME | CVAR_BOOL,
	"print the Awakening turret gun's heat as it fires, overheats and recovers" );

/*
================
idVehicleTurretGun::idVehicleTurretGun
================
*/
idVehicleTurretGun::idVehicleTurretGun( void ) {
	spreadCold = 0.0f;
	spreadHot = 0.0f;
	zoomSpreadScale = 1.0f;
	heatPerShot = 0.0f;
	heatTolerance = 0.0f;
	safeHeatLevel = 0.0f;
	heatCoolDelay = 0;
	heatDissipation = 0.0f;
	overheatDissipation = 0.0f;
	heatWarnLevel = 1.0f;
	overheatSound = NULL;
	heat = 0.0f;
	overheated = false;
	lastHeatTime = 0;
	lastShotTime = 0;
	attackHeld = false;
	ventSoundOn = false;
}

/*
================
idVehicleTurretGun::ReadHeatParms
================
*/
void idVehicleTurretGun::ReadHeatParms( void ) {
	spreadHot = spawnArgs.GetFloat( "spread" );
	spreadCold = idMath::ClampFloat( 0.0f, spreadHot, spawnArgs.GetFloat( "spreadCold", va( "%f", spreadHot ) ) );
	zoomSpreadScale = idMath::ClampFloat( 0.0f, 1.0f, spawnArgs.GetFloat( "zoomSpreadScale", "1" ) );
	heatPerShot = Max( 0.0f, spawnArgs.GetFloat( "heatPerShot" ) );
	heatTolerance = Max( 0.0f, spawnArgs.GetFloat( "heatTolerance" ) );
	safeHeatLevel = idMath::ClampFloat( 0.0f, heatTolerance, spawnArgs.GetFloat( "safeHeatLevel" ) );
	heatCoolDelay = SEC2MS( Max( 0.0f, spawnArgs.GetFloat( "heatCoolDelay" ) ) );
	heatDissipation = Max( 0.0f, spawnArgs.GetFloat( "heatDissipationPerSecond" ) );
	overheatDissipation = Max( 0.0f, spawnArgs.GetFloat( "overheatDissipationPerSecond", va( "%f", heatDissipation ) ) );
	heatWarnLevel = idMath::ClampFloat( 0.0f, 1.0f, spawnArgs.GetFloat( "heatWarnLevel", "1" ) );
	overheatSound = declManager->FindSound( spawnArgs.GetString( "snd_overheat" ), false );
	declManager->FindSound( spawnArgs.GetString( "snd_overheatFire" ), false );
	declManager->FindSound( spawnArgs.GetString( "snd_overheatRecover" ), false );
	gameLocal.GetEffect( spawnArgs, "fx_overheat" );
}

/*
================
idVehicleTurretGun::Spawn
================
*/
void idVehicleTurretGun::Spawn( void ) {
	ReadHeatParms();
	heat = 0.0f;
	overheated = false;
	lastHeatTime = gameLocal.time;
	lastShotTime = 0;
	attackHeld = false;
	ventSoundOn = false;
}

/*
================
idVehicleTurretGun::Save
================
*/
void idVehicleTurretGun::Save( idSaveGame *savefile ) const {
	savefile->WriteFloat( heat );
	savefile->WriteBool( overheated );
	savefile->WriteInt( lastHeatTime );
	savefile->WriteInt( lastShotTime );
}

/*
================
idVehicleTurretGun::Restore
================
*/
void idVehicleTurretGun::Restore( idRestoreGame *savefile ) {
	savefile->ReadFloat( heat );
	savefile->ReadBool( overheated );
	savefile->ReadInt( lastHeatTime );
	savefile->ReadInt( lastShotTime );
	ReadHeatParms();
	heat = idMath::ClampFloat( 0.0f, heatTolerance, heat );
	attackHeld = false;
	ventSoundOn = false;
}

/*
================
idVehicleTurretGun::GetPlayerDriver
================
*/
idPlayer *idVehicleTurretGun::GetPlayerDriver( void ) const {
	idActor *driver = position ? position->GetDriver() : NULL;
	return ( driver && driver->IsType( idPlayer::GetClassType() ) ) ? static_cast<idPlayer *>( driver ) : NULL;
}

/*
================
idVehicleTurretGun::HeatFraction
================
*/
float idVehicleTurretGun::HeatFraction( void ) const {
	return heatTolerance > 0.0f ? idMath::ClampFloat( 0.0f, 1.0f, heat / heatTolerance ) : 0.0f;
}

/*
================
idVehicleTurretGun::Cool

Brings the heat up to date. An overheated barrel cools from the moment it
locks; otherwise cooling waits for heatCoolDelay after the last round, and
only the part of the elapsed time after that counts.
================
*/
void idVehicleTurretGun::Cool( void ) {
	const int now = gameLocal.time;
	int from = lastHeatTime;
	lastHeatTime = now;
	if ( now <= from || heat <= 0.0f ) {
		heat = Max( 0.0f, heat );
		return;
	}
	float rate = heatDissipation;
	if ( overheated ) {
		rate = overheatDissipation;
	} else {
		from = Max( from, lastShotTime + heatCoolDelay );
	}
	if ( now > from ) {
		heat = Max( 0.0f, heat - rate * MS2SEC( now - from ) );
	}
	if ( overheated && heat <= safeHeatLevel ) {
		overheated = false;
		if ( ventSoundOn && parent.GetEntity() ) {
			parent->StopSound( soundChannel, false );
		}
		ventSoundOn = false;
		if ( GetPlayerDriver() && parent.GetEntity() ) {
			parent->StartSoundShader( declManager->FindSound( spawnArgs.GetString( "snd_overheatRecover" ), false ), SND_CHANNEL_ANY, 0, false, NULL );
		}
		if ( g_debugTurretGun.GetBool() ) {
			gameLocal.Printf( "%d TURRET_GUN cooled heat=%.1f\n", now, heat );
		}
	}
}

/*
================
idVehicleTurretGun::Overheat
================
*/
void idVehicleTurretGun::Overheat( void ) {
	heat = heatTolerance;
	overheated = true;
	// the venting replaces the smoke a long burst would trail
	burstShots = 0;
	if ( parent.GetEntity() ) {
		if ( overheatSound ) {
			parent->StartSoundShader( overheatSound, soundChannel, 0, false, NULL );
			ventSoundOn = true;
		}
		const jointHandle_t muzzle = joints.Num() ? joints[ ( jointIndex + joints.Num() - 1 ) % joints.Num() ] : INVALID_JOINT;
		if ( muzzle != INVALID_JOINT ) {
			parent->PlayEffect( gameLocal.GetEffect( spawnArgs, "fx_overheat" ), muzzle, vec3_origin, mat3_identity );
		}
	}
	if ( g_debugTurretGun.GetBool() ) {
		gameLocal.Printf( "%d TURRET_GUN overheated\n", gameLocal.time );
	}
}

/*
================
idVehicleTurretGun::Activate
================
*/
void idVehicleTurretGun::Activate( bool activate ) {
	Cool();
	rvVehicleWeapon::Activate( activate );
	if ( !activate ) {
		if ( ventSoundOn && parent.GetEntity() ) {
			parent->StopSound( soundChannel, false );
		}
		ventSoundOn = false;
		attackHeld = false;
	}
}

/*
================
idVehicleTurretGun::RunPostPhysics
================
*/
void idVehicleTurretGun::RunPostPhysics( void ) {
	Cool();

	const bool attack = ( position->mInputCmd.buttons & BUTTON_ATTACK ) != 0;
	if ( overheated && IsActive() && parent->IsShootingEnabled() ) {
		// locked: the trigger only clicks, once a pull
		if ( attack && !attackHeld && GetPlayerDriver() ) {
			parent->StartSoundShader( declManager->FindSound( spawnArgs.GetString( "snd_overheatFire" ), false ), SND_CHANNEL_ANY, 0, false, NULL );
		}
		attackHeld = attack;
		UpdateLock();
		return;
	}
	attackHeld = attack;

	// the spread the next round leaves with
	idPlayer *player = GetPlayerDriver();
	if ( player && heatTolerance > 0.0f ) {
		spread = spreadCold + ( spreadHot - spreadCold ) * HeatFraction();
		if ( player->IsZoomed() ) {
			spread *= zoomSpreadScale;
		}
	} else {
		spread = spreadHot;
	}

	rvVehicleWeapon::RunPostPhysics();
}

/*
================
idVehicleTurretGun::Fire
================
*/
bool idVehicleTurretGun::Fire( void ) {
	if ( overheated ) {
		return false;
	}
	if ( !rvVehicleWeapon::Fire() ) {
		return false;
	}
	if ( GetPlayerDriver() && heatTolerance > 0.0f ) {
		Cool();
		heat += heatPerShot;
		lastShotTime = gameLocal.time;
		if ( g_debugTurretGun.GetBool() ) {
			gameLocal.Printf( "%d TURRET_GUN fire heat=%.1f spread=%.2f\n", gameLocal.time, heat, spread );
		}
		if ( heat >= heatTolerance ) {
			Overheat();
		}
	}
	return true;
}

/*
================
idVehicleTurretGun::UpdateHUD
================
*/
void idVehicleTurretGun::UpdateHUD( idUserInterface *gui ) {
	Cool();
	idPlayer *player = GetPlayerDriver();
	gui->SetStateFloat( "vehicle_heat", HeatFraction() );
	gui->SetStateInt( "vehicle_overheat", overheated ? 1 : 0 );
	gui->SetStateFloat( "vehicle_heatwarn", heatWarnLevel );
	gui->SetStateInt( "vehicle_zoom", ( player && player->IsZoomed() ) ? 1 : 0 );
}
