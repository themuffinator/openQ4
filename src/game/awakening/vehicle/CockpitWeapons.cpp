#include "../../Game_local.h"
#include "../../ai/AI_Manager.h"
#include "../SpaceRocket.h"

/*
===============================================================================

	Cockpit weapons

	The pulse cannon and missile launcher of the Awakening's turret cockpits:
	the m03 dropship guns and the m06 MCC side guns. Both sit on one turret
	and the driver switches between them.

	Around rvVehicleWeapon's firing they add
		- a crosshair that follows what the driver aims at: the def's
		  "mtr_crosshair" / "color_crosshair" normally and
		  "mtr_<state>Crosshair" / "color_<state>Crosshair" for friendly,
		  out-of-range, acquiring and locked targets;
		- guide effects ("fx_...Guide") that mark targets for the driver,
		  gliding from target to target over "reticuleInterpolationTime";
		- the cockpit screens, which get events and values through every GUI
		  on the vehicle.

	Weapon sounds are one-shots on SND_CHANNEL_ANY: vehicle weapons share a
	sound channel with the part added before them, so private channels would
	cut off the turret's own motor sounds.

===============================================================================
*/

/*
===============================================================================

	riReticuleEffect

	One guide effect for one weapon. It sits on a target's middle and, when
	the target changes, glides there from where it was. Targets farther than
	the weapon's "reticuleScaleDistance" are marked at that distance along the
	line of sight, so a far target keeps a readable marker. Only the driver
	sees it.

===============================================================================
*/

class riReticuleEffect {
public:
							riReticuleEffect( void );

	void					Init( const char *effectKey, int interpolationTime, float scaleDistance );
	void					Update( const idDict &args, idEntity *newTarget, idPlayer *viewer );
	void					Stop( void );

	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

private:
	idStr					effectKey;
	int						interpolationTime;
	float					scaleDistance;

	idEntityPtr<idEntity>	target;
	idVec3					position;
	idVec3					glideFrom;
	int						glideStart;
	rvClientEntityPtr<rvClientEffect>	effect;
};

/*
================
riReticuleEffect::riReticuleEffect
================
*/
riReticuleEffect::riReticuleEffect( void ) {
	interpolationTime = 0;
	scaleDistance = 0.0f;
	position.Zero();
	glideFrom.Zero();
	glideStart = 0;
	effect = NULL;
}

/*
================
riReticuleEffect::Init
================
*/
void riReticuleEffect::Init( const char *key, int time, float distance ) {
	effectKey = key;
	interpolationTime = time;
	scaleDistance = distance;
}

/*
================
riReticuleEffect::Stop
================
*/
void riReticuleEffect::Stop( void ) {
	if ( effect ) {
		effect->Stop();
		effect = NULL;
	}
	target = NULL;
}

/*
================
riReticuleEffect::Update
================
*/
void riReticuleEffect::Update( const idDict &args, idEntity *newTarget, idPlayer *viewer ) {
	if ( newTarget == NULL || viewer == NULL ) {
		Stop();
		return;
	}

	const idVec3 targetPoint = AwakeningProjectileTargetPoint( newTarget );
	if ( newTarget != target.GetEntity() ) {
		glideFrom = target.GetEntity() ? position : targetPoint;
		glideStart = gameLocal.time;
		target = newTarget;
	}

	if ( interpolationTime > 0 && gameLocal.time < glideStart + interpolationTime ) {
		const float fraction = (float)( gameLocal.time - glideStart ) / interpolationTime;
		position = glideFrom + ( targetPoint - glideFrom ) * fraction;
	} else {
		position = targetPoint;
	}

	const idVec3 eye = viewer->firstPersonViewOrigin;
	idVec3 marker = position;
	idVec3 toMarker = position - eye;
	const float distance = toMarker.Normalize();
	if ( scaleDistance > 0.0f && distance > scaleDistance ) {
		marker = eye + toMarker * scaleDistance;
	}

	const idMat3 axis = viewer->firstPersonViewAxis.Transpose();
	if ( effect ) {
		effect->SetOrigin( marker );
		effect->SetAxis( axis );
		return;
	}
	effect = gameLocal.PlayEffect( gameLocal.GetEffect( args, effectKey ), marker, axis, true, vec3_origin, false );
	if ( effect ) {
		effect->GetRenderEffect()->weaponDepthHackInViewID = viewer->entityNumber + 1;
		effect->GetRenderEffect()->allowSurfaceInViewID = viewer->entityNumber + 1;
	}
}

/*
================
riReticuleEffect::Save
================
*/
void riReticuleEffect::Save( idSaveGame *savefile ) const {
	savefile->WriteString( effectKey );
	savefile->WriteInt( interpolationTime );
	savefile->WriteFloat( scaleDistance );
	target.Save( savefile );
	savefile->WriteVec3( position );
	savefile->WriteVec3( glideFrom );
	savefile->WriteInt( glideStart );
}

/*
================
riReticuleEffect::Restore

The effect itself restarts on the next update.
================
*/
void riReticuleEffect::Restore( idRestoreGame *savefile ) {
	savefile->ReadString( effectKey );
	savefile->ReadInt( interpolationTime );
	savefile->ReadFloat( scaleDistance );
	target.Restore( savefile );
	savefile->ReadVec3( position );
	savefile->ReadVec3( glideFrom );
	savefile->ReadInt( glideStart );
	effect = NULL;
}

/*
===============================================================================

	riVehicleCockpitWeapon

===============================================================================
*/

class riVehicleCockpitWeapon : public rvVehicleWeapon {
public:
	CLASS_PROTOTYPE( riVehicleCockpitWeapon );

							riVehicleCockpitWeapon( void );

	void					Spawn( void );
	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

	virtual void			UpdateCursorGUI( idUserInterface* gui ) const;

protected:
	enum crosshair_t {
		CROSSHAIR_DEFAULT,
		CROSSHAIR_FRIENDLY,
		CROSSHAIR_OUT_OF_RANGE,
		CROSSHAIR_ACQUIRING,
		CROSSHAIR_LOCKED,
		NUM_CROSSHAIRS
	};

	idPlayer *				GetPlayerDriver( void ) const;
	void					SendGuiEvent( const char *name ) const;
	void					SetGuiStateFloat( const char *name, float value ) const;
	void					PrepareScreens( void ) const;
	void					PrecacheSound( const char *soundKey, const char *defaultShader = "" ) const;
	void					PlaySound( const char *soundKey, const char *defaultShader = "" ) const;

	idEntity *				GetAimedEntity( float range, idVec3 &point ) const;
	bool					IsFriendly( idEntity *ent ) const;
							// the living enemy nearest the middle of the crosshair, within coneDegrees of it
	idActor *				FindEnemyNearCrosshair( float coneDegrees, float range, bool &outOfRange ) const;

	float					reticuleScaleDistance;
	int						reticuleInterpolationTime;
	crosshair_t				crosshair;

private:
	static const char *		crosshairNames[ NUM_CROSSHAIRS ];
};

CLASS_DECLARATION( rvVehicleWeapon, riVehicleCockpitWeapon )
END_CLASS

const char *riVehicleCockpitWeapon::crosshairNames[ NUM_CROSSHAIRS ] = {
	"",
	"friendly",
	"outOfRange",
	"acquiringTarget",
	"targetLock"
};

static const float COCKPIT_TARGET_CONE = 30.0f;

// sounds the expansion hard-coded; defs may override them with these keys
static const char * const PULSE_STARTUP_SOUND	= "space_cannon_startup";
static const char * const PULSE_SELECT_SOUND	= "space_cannon_switch_to_turret";
static const char * const MISSILE_SELECT_SOUND	= "space_cannon_switch_to_missiles";

// the pulse cannon's barrels, moved as the expansion's code moved them. The
// offsets are along the turret pivot's axes, the fold angles about the
// barrel's own.
static const char * const PULSE_BARREL_JOINTS[ 2 ] = { "pulse_cannon_l", "pulse_cannon_r" };
static const float BARREL_RECOIL			= 32.0f;	// units back along the aim
static const float BARREL_KICK_SEC			= 0.01f;
static const float BARREL_RETURN_SEC		= 0.1f;
static const float BARREL_FOLD_SEC			= 0.4f;
static const float BARREL_UNFOLD_SEC		= 0.2f;
static const float BARREL_FOLD_OFFSET		= 12.0f;	// out to the side, and down
static const float BARREL_FOLD_ANGLE_X		= -7.5f;	// degrees about the barrel joint's x axis, then
static const float BARREL_FOLD_ANGLE_Z		= -15.0f;	// its z axis: on the turret model, a twist and a dip

/*
================
riVehicleCockpitWeapon::riVehicleCockpitWeapon
================
*/
riVehicleCockpitWeapon::riVehicleCockpitWeapon( void ) {
	reticuleScaleDistance = 0.0f;
	reticuleInterpolationTime = 0;
	crosshair = CROSSHAIR_DEFAULT;
}

/*
================
riVehicleCockpitWeapon::Spawn
================
*/
void riVehicleCockpitWeapon::Spawn( void ) {
	reticuleScaleDistance = spawnArgs.GetFloat( "reticuleScaleDistance", "5000" );
	reticuleInterpolationTime = SEC2MS( spawnArgs.GetFloat( "reticuleInterpolationTime", "1" ) );
	crosshair = CROSSHAIR_DEFAULT;
	PrepareScreens();
}

/*
================
riVehicleCockpitWeapon::Save
================
*/
void riVehicleCockpitWeapon::Save( idSaveGame *savefile ) const {
	savefile->WriteFloat( reticuleScaleDistance );
	savefile->WriteInt( reticuleInterpolationTime );
	savefile->WriteInt( crosshair );
}

/*
================
riVehicleCockpitWeapon::Restore
================
*/
void riVehicleCockpitWeapon::Restore( idRestoreGame *savefile ) {
	savefile->ReadFloat( reticuleScaleDistance );
	savefile->ReadInt( reticuleInterpolationTime );
	int value;
	savefile->ReadInt( value );
	crosshair = (crosshair_t)value;
}

/*
================
riVehicleCockpitWeapon::UpdateCursorGUI
================
*/
void riVehicleCockpitWeapon::UpdateCursorGUI( idUserInterface* gui ) const {
	rvVehicleWeapon::UpdateCursorGUI( gui );

	const char *state = crosshairNames[ crosshair ];
	const char *material = spawnArgs.GetString( va( "mtr_%sCrosshair", state ), "" );
	const char *color = spawnArgs.GetString( va( "color_%sCrosshair", state ), "" );
	if ( crosshair == CROSSHAIR_DEFAULT || !material[ 0 ] ) {
		material = spawnArgs.GetString( "mtr_crosshair" );
	}
	if ( crosshair == CROSSHAIR_DEFAULT || !color[ 0 ] ) {
		color = spawnArgs.GetString( "color_crosshair", g_crosshairColor.GetString() );
	}

	const idMaterial *shader = declManager->FindMaterial( material, false );
	if ( shader ) {
		shader->SetSort( SS_GUI );
	}
	gui->SetStateString( "crossImage", material );
	gui->SetStateString( "crossColor", color );
	gui->StateChanged( gameLocal.time );
	// A seated gunner cannot talk to the crew through the hull. Their focus
	// cursor (or the entering cinematic) must not hide the weapon reticle.
	gui->HandleNamedEvent( "showCrossCombat" );
	gui->HandleNamedEvent( "weaponChange" );
}

void riVehicleCockpitWeapon::PrepareScreens( void ) const {
	rvVehicle *vehicle = parent.GetEntity();
	if ( vehicle == NULL ) {
		return;
	}
	// The recovered screens left their event-only timers running at startup.
	// All lock and magazine states then execute together on the first redraw,
	// ending at LOCKED/empty regardless of the weapon. Named events restart
	// only the appropriate timer. Keep the installed artwork and animations.
	for ( int i = 0; i < MAX_RENDERENTITY_GUI; ++i ) {
		idUserInterface *&gui = vehicle->GetRenderEntity()->gui[i];
		if ( gui == NULL ) {
			continue;
		}
		// Noninteractive map GUIs normally share an instance. These screens
		// receive per-weapon state, so an idle cockpit must not overwrite another
		// gun's heat, selection, lock or magazine. The unique flag also survives
		// the ordinary GUI save/restore path.
		if ( !gui->IsUniqued() ) {
			gui = uiManager->FindGui( gui->Name(), true, true );
			if ( gui == NULL ) {
				continue;
			}
			UpdateGuiParms( gui, &vehicle->spawnArgs );
		}
		for ( const char *timer : { "firstRocketLoaded", "secondRocketLoaded", "thirdRocketLoaded",
			"fourthRocketLoaded", "fifthRocketLoaded", "rocketEmpty", "lock_status_notarget",
			"lock_status_acquiringtarget", "lock_status_lockedtarget", "anim_active_guns", "anim_active_rockets" } ) {
			gui->SetPresentationValue( va( "%s::notime", timer ), "1" );
		}
	}
}

/*
================
riVehicleCockpitWeapon::GetPlayerDriver

The driver, when the driver is the local player: guides and screens are
only for them.
================
*/
idPlayer *riVehicleCockpitWeapon::GetPlayerDriver( void ) const {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player == NULL || position == NULL || position->GetDriver() != player ) {
		return NULL;
	}
	return player;
}

/*
================
riVehicleCockpitWeapon::SendGuiEvent
================
*/
void riVehicleCockpitWeapon::SendGuiEvent( const char *name ) const {
	rvVehicle *vehicle = parent.GetEntity();
	if ( vehicle == NULL ) {
		return;
	}
	for ( int i = 0; i < MAX_RENDERENTITY_GUI; i++ ) {
		idUserInterface *gui = vehicle->GetRenderEntity()->gui[ i ];
		if ( gui ) {
			gui->HandleNamedEvent( name );
		}
	}
	if ( vehicle->GetHud() ) {
		vehicle->GetHud()->HandleNamedEvent( name );
	}
}

/*
================
riVehicleCockpitWeapon::SetGuiStateFloat
================
*/
void riVehicleCockpitWeapon::SetGuiStateFloat( const char *name, float value ) const {
	rvVehicle *vehicle = parent.GetEntity();
	if ( vehicle == NULL ) {
		return;
	}
	for ( int i = 0; i < MAX_RENDERENTITY_GUI; i++ ) {
		idUserInterface *gui = vehicle->GetRenderEntity()->gui[ i ];
		if ( gui ) {
			gui->SetStateFloat( name, value );
			gui->StateChanged( gameLocal.time );
		}
	}
	if ( vehicle->GetHud() ) {
		vehicle->GetHud()->SetStateFloat( name, value );
	}
}

/*
================
riVehicleCockpitWeapon::PrecacheSound

Loads a sound while the map loads, so that playing it later does not.
================
*/
void riVehicleCockpitWeapon::PrecacheSound( const char *soundKey, const char *defaultShader ) const {
	const char *shaderName = spawnArgs.GetString( soundKey, defaultShader );
	if ( shaderName[ 0 ] ) {
		declManager->FindSound( shaderName, false );
	}
}

/*
================
riVehicleCockpitWeapon::PlaySound
================
*/
void riVehicleCockpitWeapon::PlaySound( const char *soundKey, const char *defaultShader ) const {
	rvVehicle *vehicle = parent.GetEntity();
	const char *shaderName = spawnArgs.GetString( soundKey, defaultShader );
	if ( vehicle == NULL || !shaderName[ 0 ] ) {
		return;
	}
	vehicle->StartSoundShader( declManager->FindSound( shaderName, false ), SND_CHANNEL_ANY, 0, false, NULL );
}

/*
================
riVehicleCockpitWeapon::GetAimedEntity
================
*/
idEntity *riVehicleCockpitWeapon::GetAimedEntity( float range, idVec3 &point ) const {
	trace_t tr;
	const idVec3 eye = position->GetEyeOrigin();
	gameLocal.TracePoint( parent.GetEntity(), tr, eye, eye + position->GetEyeAxis()[ 0 ] * range, MASK_SHOT_RENDERMODEL, parent.GetEntity() );
	point = tr.endpos;
	if ( tr.fraction >= 1.0f ) {
		return NULL;
	}
	return gameLocal.entities[ tr.c.entityNum ];
}

/*
================
riVehicleCockpitWeapon::IsFriendly
================
*/
bool riVehicleCockpitWeapon::IsFriendly( idEntity *ent ) const {
	idActor *driver = position->GetDriver();
	return ent != NULL && driver != NULL && ent->IsType( idActor::GetClassType() ) && ent->health > 0 &&
		static_cast<idActor *>( ent )->team == driver->team;
}

/*
================
riVehicleCockpitWeapon::FindEnemyNearCrosshair
================
*/
idActor *riVehicleCockpitWeapon::FindEnemyNearCrosshair( float coneDegrees, float range, bool &outOfRange ) const {
	outOfRange = false;
	idActor *driver = position->GetDriver();
	if ( driver == NULL ) {
		return NULL;
	}

	const idVec3 eye = position->GetEyeOrigin();
	const idVec3 forward = position->GetEyeAxis()[ 0 ];
	const float minDot = idMath::Cos( DEG2RAD( coneDegrees ) );

	idActor *best = NULL;
	idActor *bestOutOfRange = NULL;
	float bestDot = minDot;
	float bestOutOfRangeDot = minDot;
	for ( idActor *actor = aiManager.GetEnemyTeam( (aiTeam_t)driver->team ); actor != NULL; actor = actor->teamNode.Next() ) {
		if ( actor->health <= 0 || actor->IsHidden() || actor->fl.notarget ) {
			continue;
		}
		const idVec3 point = AwakeningProjectileTargetPoint( actor );
		idVec3 dir = point - eye;
		const float distance = dir.Normalize();
		const float dot = dir * forward;
		const bool tooFar = range > 0.0f && distance > range;
		if ( dot <= ( tooFar ? bestOutOfRangeDot : bestDot ) ) {
			continue;
		}
		trace_t tr;
		gameLocal.TracePoint( parent.GetEntity(), tr, eye, point, MASK_SHOT_RENDERMODEL, parent.GetEntity() );
		if ( tr.fraction < 1.0f && tr.c.entityNum != actor->entityNumber ) {
			continue;
		}
		if ( tooFar ) {
			bestOutOfRange = actor;
			bestOutOfRangeDot = dot;
		} else {
			best = actor;
			bestDot = dot;
		}
	}
	outOfRange = best == NULL && bestOutOfRange != NULL;
	return best != NULL ? best : bestOutOfRange;
}

/*
===============================================================================

	riVCWPulseCannon

	A rapid-fire hitscan cannon that heats up. Every shot adds "heatPerShot";
	"heatDissipationPerSecond" bleeds it off. At "heatTolerance" the cannon
	jams ("snd_overheatedSound", JammedTextOn on the screens) and will not
	fire ("snd_overheatedFireSound" if you try) until it has cooled to
	"safeHeatLevel" ("snd_overheatRecoverSound", JammedTextOff). The screens
	show the heat as gui::HeatPercentage, 0..1. The expansion's code sent
	WeaponOverheated / WeaponNormal, which no screen handles; the screens'
	JAMMED text, which m03's ammo-jam script also shows, stands in for them.

	The nearest enemy near the crosshair is marked with
	"fx_closestTargetGuide", or "fx_outOfRangeGuide" when it is beyond the
	cannon's "range".

	The cannon's two barrels are the vehicle's "pulse_cannon_l" and
	"pulse_cannon_r" joints, moved as the expansion moved them: a shot kicks
	the barrel that fired back along the aim and it slides home, and while the
	missiles are selected both barrels fold out and down out of the way. The
	cannon keeps cooling while the missiles are selected.

===============================================================================
*/

class riVCWPulseCannon : public riVehicleCockpitWeapon {
public:
	CLASS_PROTOTYPE( riVCWPulseCannon );

							riVCWPulseCannon( void );

	void					Spawn( void );
	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

	virtual void			RunPostPhysics( void );
	virtual void			RunInactivePostPhysics( void );
	virtual void			Activate( bool activate );
	virtual bool			Fire( void );
	virtual void			Select( bool select );

private:
	enum barrelKick_t {
		BARREL_AT_REST,
		BARREL_KICKING,
		BARREL_RETURNING
	};

	struct barrel_t {
		jointHandle_t		joint;
		barrelKick_t		kickState;
		float				kick;		// 0 at rest .. 1 kicked fully back
		float				fold;		// 0 in the firing pose .. 1 folded away
	};

	static const int		NUM_BARRELS = 2;

	void					Cool( void );
	void					UpdateTargeting( void );
	void					StopGuides( void );
	void					UpdateBarrels( void );

	barrel_t				barrels[ NUM_BARRELS ];
	bool					barrelsSettled;	// not saved: a spawned or loaded cannon settles its barrels to the selection

	float					heat;
	float					heatPerShot;
	float					heatTolerance;
	float					safeHeatLevel;
	float					heatDissipationPerSecond;
	int						lastHeatTime;
	bool					overheated;
	bool					attackHeld;
	float					range;

	riReticuleEffect		closestGuide;
	riReticuleEffect		outOfRangeGuide;
};

CLASS_DECLARATION( riVehicleCockpitWeapon, riVCWPulseCannon )
END_CLASS

/*
================
riVCWPulseCannon::riVCWPulseCannon
================
*/
riVCWPulseCannon::riVCWPulseCannon( void ) {
	heat = 0.0f;
	heatPerShot = 0.0f;
	heatTolerance = 0.0f;
	safeHeatLevel = 0.0f;
	heatDissipationPerSecond = 0.0f;
	lastHeatTime = 0;
	overheated = false;
	attackHeld = false;
	range = 0.0f;
	for ( int i = 0; i < NUM_BARRELS; i++ ) {
		barrels[ i ].joint = INVALID_JOINT;
		barrels[ i ].kickState = BARREL_AT_REST;
		barrels[ i ].kick = 0.0f;
		barrels[ i ].fold = 0.0f;
	}
	barrelsSettled = false;
}

/*
================
riVCWPulseCannon::Spawn
================
*/
void riVCWPulseCannon::Spawn( void ) {
	heatPerShot = spawnArgs.GetFloat( "heatPerShot" );
	heatTolerance = spawnArgs.GetFloat( "heatTolerance" );
	safeHeatLevel = spawnArgs.GetFloat( "safeHeatLevel" );
	heatDissipationPerSecond = spawnArgs.GetFloat( "heatDissipationPerSecond" );
	range = spawnArgs.GetFloat( "range", "8192" );
	heat = 0.0f;
	lastHeatTime = gameLocal.time;
	overheated = false;

	closestGuide.Init( "fx_closestTargetGuide", reticuleInterpolationTime, reticuleScaleDistance );
	outOfRangeGuide.Init( "fx_outOfRangeGuide", reticuleInterpolationTime, reticuleScaleDistance );

	PrecacheSound( "snd_fire_stereo" );
	PrecacheSound( "snd_overheatedSound" );
	PrecacheSound( "snd_overheatedFireSound" );
	PrecacheSound( "snd_overheatRecoverSound" );
	PrecacheSound( "snd_startup", PULSE_STARTUP_SOUND );
	PrecacheSound( "snd_select", PULSE_SELECT_SOUND );
}

/*
================
riVCWPulseCannon::Save
================
*/
void riVCWPulseCannon::Save( idSaveGame *savefile ) const {
	savefile->WriteFloat( heat );
	savefile->WriteFloat( heatPerShot );
	savefile->WriteFloat( heatTolerance );
	savefile->WriteFloat( safeHeatLevel );
	savefile->WriteFloat( heatDissipationPerSecond );
	savefile->WriteInt( lastHeatTime );
	savefile->WriteBool( overheated );
	savefile->WriteBool( attackHeld );
	savefile->WriteFloat( range );
	closestGuide.Save( savefile );
	outOfRangeGuide.Save( savefile );
}

/*
================
riVCWPulseCannon::Restore
================
*/
void riVCWPulseCannon::Restore( idRestoreGame *savefile ) {
	savefile->ReadFloat( heat );
	savefile->ReadFloat( heatPerShot );
	savefile->ReadFloat( heatTolerance );
	savefile->ReadFloat( safeHeatLevel );
	savefile->ReadFloat( heatDissipationPerSecond );
	savefile->ReadInt( lastHeatTime );
	savefile->ReadBool( overheated );
	savefile->ReadBool( attackHeld );
	savefile->ReadFloat( range );
	closestGuide.Restore( savefile );
	outOfRangeGuide.Restore( savefile );
}

/*
================
riVCWPulseCannon::Cool
================
*/
void riVCWPulseCannon::Cool( void ) {
	heat = Max( 0.0f, heat - heatDissipationPerSecond * MS2SEC( gameLocal.time - lastHeatTime ) );
	lastHeatTime = gameLocal.time;

	if ( overheated && heat <= safeHeatLevel ) {
		overheated = false;
		if ( g_debugVehicle.GetInteger() ) {
			gameLocal.Printf( "%d %s: cooled to %.0f\n", gameLocal.time, GetClassname(), heat );
		}
		PlaySound( "snd_overheatRecoverSound" );
		// m03's script jams the gun with the same screen text: leave it up
		// while the script has the weapon disabled, the script clears it
		rvVehicle *vehicle = parent.GetEntity();
		if ( vehicle == NULL || vehicle->IsShootingEnabled() ) {
			SendGuiEvent( "JammedTextOff" );
		}
	}
	SetGuiStateFloat( "HeatPercentage", heatTolerance > 0.0f ? idMath::ClampFloat( 0.0f, 1.0f, heat / heatTolerance ) : 0.0f );
}

/*
================
riVCWPulseCannon::StopGuides
================
*/
void riVCWPulseCannon::StopGuides( void ) {
	closestGuide.Stop();
	outOfRangeGuide.Stop();
}

/*
================
riVCWPulseCannon::UpdateTargeting
================
*/
void riVCWPulseCannon::UpdateTargeting( void ) {
	idPlayer *player = GetPlayerDriver();
	if ( player == NULL ) {
		StopGuides();
		return;
	}

	idVec3 point;
	idEntity *aimed = GetAimedEntity( range, point );
	bool outOfRange;
	idActor *enemy = FindEnemyNearCrosshair( COCKPIT_TARGET_CONE, range, outOfRange );

	if ( IsFriendly( aimed ) ) {
		crosshair = CROSSHAIR_FRIENDLY;
	} else if ( outOfRange ) {
		crosshair = CROSSHAIR_OUT_OF_RANGE;
	} else {
		crosshair = CROSSHAIR_DEFAULT;
	}

	closestGuide.Update( spawnArgs, outOfRange ? NULL : enemy, player );
	outOfRangeGuide.Update( spawnArgs, outOfRange ? enemy : NULL, player );
}

/*
================
riVCWPulseCannon::UpdateBarrels

Kicks run out and home; the fold follows the selection, out while the missiles
are selected. A spawned or loaded cannon starts with its barrels settled.
================
*/
void riVCWPulseCannon::UpdateBarrels( void ) {
	rvVehicle *vehicle = parent.GetEntity();
	idAnimator *animator = vehicle != NULL ? vehicle->GetAnimator() : NULL;
	const float foldTarget = ( position != NULL && position->GetActiveWeapon() != this ) ? 1.0f : 0.0f;
	const float step = MS2SEC( gameLocal.msec );

	for ( int i = 0; i < NUM_BARRELS; i++ ) {
		barrel_t &barrel = barrels[ i ];
		if ( !barrelsSettled ) {
			barrel.joint = animator != NULL ? animator->GetJointHandle( PULSE_BARREL_JOINTS[ i ] ) : INVALID_JOINT;
			barrel.fold = foldTarget;
		}

		if ( barrel.kickState == BARREL_KICKING ) {
			barrel.kick += step / BARREL_KICK_SEC;
			if ( barrel.kick >= 1.0f ) {
				barrel.kick = 1.0f;
				barrel.kickState = BARREL_RETURNING;
			}
		} else if ( barrel.kickState == BARREL_RETURNING ) {
			barrel.kick -= step / BARREL_RETURN_SEC;
			if ( barrel.kick <= 0.0f ) {
				barrel.kick = 0.0f;
				barrel.kickState = BARREL_AT_REST;
			}
		}

		if ( barrel.fold < foldTarget ) {
			barrel.fold = Min( foldTarget, barrel.fold + step / BARREL_FOLD_SEC );
		} else if ( barrel.fold > foldTarget ) {
			barrel.fold = Max( foldTarget, barrel.fold - step / BARREL_UNFOLD_SEC );
		}

		if ( animator == NULL || barrel.joint == INVALID_JOINT ) {
			continue;
		}
		const float side = ( i == 0 ) ? 1.0f : -1.0f;
		idMat3 axis;
		axis.Identity();
		axis.RotateAbsolute( 0, barrel.fold * BARREL_FOLD_ANGLE_X );
		axis.RotateRelative( 2, barrel.fold * BARREL_FOLD_ANGLE_Z );
		animator->SetJointPos( barrel.joint, JOINTMOD_LOCAL,
			idVec3( -BARREL_RECOIL * barrel.kick, side * BARREL_FOLD_OFFSET * barrel.fold, -BARREL_FOLD_OFFSET * barrel.fold ) );
		animator->SetJointAxis( barrel.joint, JOINTMOD_LOCAL, axis );
	}
	barrelsSettled = true;
}

/*
================
riVCWPulseCannon::RunInactivePostPhysics
================
*/
void riVCWPulseCannon::RunInactivePostPhysics( void ) {
	if ( IsActive() ) {
		Cool();
	}
	UpdateBarrels();
}

/*
================
riVCWPulseCannon::RunPostPhysics
================
*/
void riVCWPulseCannon::RunPostPhysics( void ) {
	UpdateBarrels();
	Cool();

	const bool attack = ( position->mInputCmd.buttons & BUTTON_ATTACK ) != 0;
	if ( overheated && attack && !attackHeld && IsActive() ) {
		PlaySound( "snd_overheatedFireSound" );
	}
	attackHeld = attack;

	if ( IsActive() ) {
		UpdateTargeting();
	}
	rvVehicleWeapon::RunPostPhysics();
}

/*
================
riVCWPulseCannon::Fire
================
*/
bool riVCWPulseCannon::Fire( void ) {
	if ( overheated ) {
		return false;
	}

	PlaySound( "snd_fire_stereo" );
	const int firingBarrel = jointIndex;	// the launch joints alternate between the barrels
	if ( !rvVehicleWeapon::Fire() ) {
		return false;
	}
	if ( firingBarrel >= 0 && firingBarrel < NUM_BARRELS ) {
		barrels[ firingBarrel ].kickState = BARREL_KICKING;
	}

	heat += heatPerShot;
	if ( heatTolerance > 0.0f && heat >= heatTolerance ) {
		heat = heatTolerance;
		overheated = true;
		if ( g_debugVehicle.GetInteger() ) {
			gameLocal.Printf( "%d %s: overheated\n", gameLocal.time, GetClassname() );
		}
		PlaySound( "snd_overheatedSound" );
		SendGuiEvent( "JammedTextOn" );
	}
	return true;
}

/*
================
riVCWPulseCannon::Activate
================
*/
void riVCWPulseCannon::Activate( bool activate ) {
	rvVehicleWeapon::Activate( activate );
	lastHeatTime = gameLocal.time;
	if ( activate ) {
		PlaySound( "snd_startup", PULSE_STARTUP_SOUND );
	} else {
		StopGuides();
	}
}

/*
================
riVCWPulseCannon::Select
================
*/
void riVCWPulseCannon::Select( bool select ) {
	rvVehicleWeapon::Select( select );
	if ( select ) {
		if ( GetPlayerDriver() ) {
			PlaySound( "snd_select", PULSE_SELECT_SOUND );
		}
		SendGuiEvent( "anim_active_guns" );
	} else {
		StopGuides();
	}
}

/*
===============================================================================

	riVCWMissileTurret

	Fires homing rockets (riProjectileSpaceRocket), from alternate launch
	joints with "snd_rightFire" / "snd_leftFire" ("snd_multiFire" when one
	shot fires several).

	Targeting: the enemy nearest the crosshair within "lockRange" is locked
	on to after "timeToHardLock" milliseconds of acquiring. While acquiring
	("snd_acquiringTarget", "fx_acquiringTargetGuide") the launcher holds its
	fire; once locked ("snd_targetLock", "fx_guide") every rocket homes on
	the target. Unlocked rockets fly at the point under the crosshair. With
	several rockets to a shot, "missileSpreadWeight" fans them out in a ring
	round the target. The screens get lockStatusNoTarget / ...AcquiringTarget
	/ ...LockedTarget.

	The rocket screen counts a five-rocket magazine: empty on firing
	(rocketEmpty), one more rocket loaded every "loaddelay" seconds
	(firstRocketLoaded ... fifthRocketLoaded), also while the pulse cannon is
	selected. A volley consumes the displayed charge; "firedelay" also limits
	how soon the next nonempty volley can fire.

===============================================================================
*/

class riVCWMissileTurret : public riVehicleCockpitWeapon {
public:
	CLASS_PROTOTYPE( riVCWMissileTurret );

							riVCWMissileTurret( void );

	void					Spawn( void );
	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

	virtual void			RunPostPhysics( void );
	virtual void			RunInactivePostPhysics( void );
	virtual void			Activate( bool activate );
	virtual bool			Fire( void );
	virtual void			Select( bool select );
	virtual void			ProjectileLaunched( idProjectile *projectile );

private:
	enum lockState_t {
		LOCK_NONE,
		LOCK_ACQUIRING,
		LOCK_LOCKED
	};

	static const int		MAGAZINE_SIZE = 5;

	void					SetLockState( lockState_t state );
	void					UpdateTargeting( void );
	void					SetRocketsLoaded( int count );
	void					UpdateMagazine( void );
	void					StopGuides( void );

	lockState_t				lockState;
	idEntityPtr<idActor>	lockTarget;
	int						lockStartTime;
	int						timeToHardLock;
	idVec3					aimPoint;
	idMat3					launchAxis;
	int						shotIndex;

	int						rocketsLoaded;
	int						nextLoadTime;
	int						loadDelay;

	riReticuleEffect		closestGuide;
	riReticuleEffect		outOfRangeGuide;
	riReticuleEffect		acquiringGuide;
	riReticuleEffect		lockedGuide;
};

CLASS_DECLARATION( riVehicleCockpitWeapon, riVCWMissileTurret )
END_CLASS

/*
================
riVCWMissileTurret::riVCWMissileTurret
================
*/
riVCWMissileTurret::riVCWMissileTurret( void ) {
	lockState = LOCK_NONE;
	lockStartTime = 0;
	timeToHardLock = 0;
	aimPoint.Zero();
	launchAxis.Identity();
	shotIndex = 0;
	rocketsLoaded = 0;
	nextLoadTime = 0;
	loadDelay = 0;
}

/*
================
riVCWMissileTurret::Spawn
================
*/
void riVCWMissileTurret::Spawn( void ) {
	timeToHardLock = spawnArgs.GetInt( "timeToHardLock" );
	loadDelay = SEC2MS( spawnArgs.GetFloat( "loaddelay" ) );
	lockState = LOCK_NONE;
	rocketsLoaded = 0;
	nextLoadTime = gameLocal.time;

	closestGuide.Init( "fx_closestTargetGuide", reticuleInterpolationTime, reticuleScaleDistance );
	outOfRangeGuide.Init( "fx_outOfRangeGuide", reticuleInterpolationTime, reticuleScaleDistance );
	acquiringGuide.Init( "fx_acquiringTargetGuide", reticuleInterpolationTime, reticuleScaleDistance );
	lockedGuide.Init( "fx_guide", reticuleInterpolationTime, reticuleScaleDistance );

	PrecacheSound( "snd_acquiringTarget" );
	PrecacheSound( "snd_targetLock" );
	PrecacheSound( "snd_multiFire" );
	PrecacheSound( "snd_rightFire" );
	PrecacheSound( "snd_leftFire" );
	PrecacheSound( "snd_select", MISSILE_SELECT_SOUND );
	SendGuiEvent( "lockStatusNoTarget" );
	SendGuiEvent( "rocketEmpty" );
}

/*
================
riVCWMissileTurret::Save
================
*/
void riVCWMissileTurret::Save( idSaveGame *savefile ) const {
	savefile->WriteInt( lockState );
	lockTarget.Save( savefile );
	savefile->WriteInt( lockStartTime );
	savefile->WriteInt( timeToHardLock );
	savefile->WriteVec3( aimPoint );
	savefile->WriteMat3( launchAxis );
	savefile->WriteInt( shotIndex );
	savefile->WriteInt( rocketsLoaded );
	savefile->WriteInt( nextLoadTime );
	savefile->WriteInt( loadDelay );
	closestGuide.Save( savefile );
	outOfRangeGuide.Save( savefile );
	acquiringGuide.Save( savefile );
	lockedGuide.Save( savefile );
}

/*
================
riVCWMissileTurret::Restore
================
*/
void riVCWMissileTurret::Restore( idRestoreGame *savefile ) {
	int value;
	savefile->ReadInt( value );
	lockState = (lockState_t)value;
	lockTarget.Restore( savefile );
	savefile->ReadInt( lockStartTime );
	savefile->ReadInt( timeToHardLock );
	savefile->ReadVec3( aimPoint );
	savefile->ReadMat3( launchAxis );
	savefile->ReadInt( shotIndex );
	savefile->ReadInt( rocketsLoaded );
	savefile->ReadInt( nextLoadTime );
	savefile->ReadInt( loadDelay );
	closestGuide.Restore( savefile );
	outOfRangeGuide.Restore( savefile );
	acquiringGuide.Restore( savefile );
	lockedGuide.Restore( savefile );
}

/*
================
riVCWMissileTurret::StopGuides
================
*/
void riVCWMissileTurret::StopGuides( void ) {
	closestGuide.Stop();
	outOfRangeGuide.Stop();
	acquiringGuide.Stop();
	lockedGuide.Stop();
}

/*
================
riVCWMissileTurret::SetLockState
================
*/
void riVCWMissileTurret::SetLockState( lockState_t state ) {
	if ( state == lockState ) {
		return;
	}
	lockState = state;
	if ( g_debugVehicle.GetInteger() ) {
		gameLocal.Printf( "%d %s: lock %d on %s\n", gameLocal.time, GetClassname(), (int)state,
			lockTarget.GetEntity() ? lockTarget->GetName() : "-" );
	}
	switch ( state ) {
		case LOCK_NONE:
			lockTarget = NULL;
			SendGuiEvent( "lockStatusNoTarget" );
			break;
		case LOCK_ACQUIRING:
			lockStartTime = gameLocal.time;
			PlaySound( "snd_acquiringTarget" );
			SendGuiEvent( "lockStatusAcquiringTarget" );
			break;
		case LOCK_LOCKED:
			PlaySound( "snd_targetLock" );
			SendGuiEvent( "lockStatusLockedTarget" );
			break;
	}
}

/*
================
riVCWMissileTurret::UpdateTargeting
================
*/
void riVCWMissileTurret::UpdateTargeting( void ) {
	idPlayer *player = GetPlayerDriver();
	if ( player == NULL ) {
		SetLockState( LOCK_NONE );
		StopGuides();
		return;
	}

	idEntity *aimed = GetAimedEntity( lockRange > 0.0f ? lockRange : 8192.0f, aimPoint );
	bool outOfRange;
	idActor *enemy = FindEnemyNearCrosshair( COCKPIT_TARGET_CONE, lockRange, outOfRange );
	idActor *candidate = ( enemy != NULL && !outOfRange ) ? enemy : NULL;

	if ( candidate == NULL ) {
		SetLockState( LOCK_NONE );
	} else if ( candidate != lockTarget.GetEntity() ) {
		lockTarget = candidate;
		lockState = LOCK_NONE;
		SetLockState( LOCK_ACQUIRING );
	} else if ( lockState == LOCK_ACQUIRING && gameLocal.time >= lockStartTime + timeToHardLock ) {
		SetLockState( LOCK_LOCKED );
	}

	if ( IsFriendly( aimed ) ) {
		crosshair = CROSSHAIR_FRIENDLY;
	} else if ( lockState == LOCK_LOCKED ) {
		crosshair = CROSSHAIR_LOCKED;
	} else if ( lockState == LOCK_ACQUIRING ) {
		crosshair = CROSSHAIR_ACQUIRING;
	} else if ( outOfRange ) {
		crosshair = CROSSHAIR_OUT_OF_RANGE;
	} else {
		crosshair = CROSSHAIR_DEFAULT;
	}

	lockedGuide.Update( spawnArgs, lockState == LOCK_LOCKED ? candidate : NULL, player );
	acquiringGuide.Update( spawnArgs, lockState == LOCK_ACQUIRING ? candidate : NULL, player );
	outOfRangeGuide.Update( spawnArgs, outOfRange ? enemy : NULL, player );
	closestGuide.Stop();
}

/*
================
riVCWMissileTurret::SetRocketsLoaded
================
*/
void riVCWMissileTurret::SetRocketsLoaded( int count ) {
	static const char *loadedEvents[ MAGAZINE_SIZE ] = {
		"firstRocketLoaded", "secondRocketLoaded", "thirdRocketLoaded", "fourthRocketLoaded", "fifthRocketLoaded"
	};

	if ( count == 0 ) {
		SendGuiEvent( "rocketEmpty" );
	} else if ( count > rocketsLoaded ) {
		SendGuiEvent( loadedEvents[ count - 1 ] );
	}
	rocketsLoaded = count;
	nextLoadTime = gameLocal.time + loadDelay;
}

/*
================
riVCWMissileTurret::UpdateMagazine
================
*/
void riVCWMissileTurret::UpdateMagazine( void ) {
	if ( rocketsLoaded < MAGAZINE_SIZE && gameLocal.time >= nextLoadTime ) {
		// Loading follows game time, including a long frame or a restored timer.
		// Do not turn a five-rocket magazine into five frames of extra waiting.
		const int due = loadDelay > 0 ? 1 + ( gameLocal.time - nextLoadTime ) / loadDelay : MAGAZINE_SIZE;
		const int added = idMath::ClampInt( 1, MAGAZINE_SIZE - rocketsLoaded, due );
		const int scheduled = nextLoadTime + added * loadDelay;
		SetRocketsLoaded( rocketsLoaded + added );
		nextLoadTime = scheduled;
	}
}

/*
================
riVCWMissileTurret::RunPostPhysics
================
*/
void riVCWMissileTurret::RunPostPhysics( void ) {
	if ( IsActive() ) {
		UpdateMagazine();
		UpdateTargeting();
	}
	rvVehicleWeapon::RunPostPhysics();
}

/*
================
riVCWMissileTurret::RunInactivePostPhysics

The magazine reloads while the pulse cannon is selected too.
================
*/
void riVCWMissileTurret::RunInactivePostPhysics( void ) {
	if ( IsActive() ) {
		UpdateMagazine();
	}
}

/*
================
riVCWMissileTurret::Fire
================
*/
bool riVCWMissileTurret::Fire( void ) {
	if ( lockState == LOCK_ACQUIRING || rocketsLoaded <= 0 ) {
		return false;
	}

	// The five cockpit indicators are the charged volley, not a decorative
	// reload animation. Launch exactly that many rockets, including a partial
	// charge, and retain the authored base count outside this launch.
	const int baseCount = count;
	const int volley = idMath::ClampInt( 1, MAGAZINE_SIZE, rocketsLoaded );
	const int firingJoint = jointIndex;
	count = volley;
	launchAxis = position->GetEyeAxis();
	const bool fired = rvVehicleWeapon::Fire();
	count = baseCount;
	if ( !fired ) {
		return false;
	}
	PlaySound( volley > 1 ? "snd_multiFire" : firingJoint == 0 ? "snd_rightFire" : "snd_leftFire" );
	if ( g_debugVehicle.GetInteger() ) {
		gameLocal.Printf( "%d %s: fired %d loaded rockets\n", gameLocal.time, GetClassname(), volley );
	}
	SetRocketsLoaded( 0 );
	return true;
}

/*
================
riVCWMissileTurret::ProjectileLaunched
================
*/
void riVCWMissileTurret::ProjectileLaunched( idProjectile *projectile ) {
	if ( !projectile->IsType( riProjectileSpaceRocket::GetClassType() ) ) {
		return;
	}
	riProjectileSpaceRocket *rocket = static_cast<riProjectileSpaceRocket *>( projectile );

	// several rockets to a shot fan out in a ring, eight steps round
	if ( count > 1 ) {
		shotIndex = ( shotIndex + 1 ) & 7;
		const float angle = DEG2RAD( shotIndex * 45.0f );
		const float weight = spawnArgs.GetFloat( "missileSpreadWeight", "0.3" );
		rocket->BendDirection( ( launchAxis[ 1 ] * idMath::Cos( angle ) + launchAxis[ 2 ] * idMath::Sin( angle ) ) * weight );
	}

	idActor *target = lockTarget.GetEntity();
	if ( lockState == LOCK_LOCKED && target != NULL ) {
		rocket->SetTarget( target );
	} else {
		rocket->SetTargetPosition( aimPoint );
	}
}

/*
================
riVCWMissileTurret::Activate
================
*/
void riVCWMissileTurret::Activate( bool activate ) {
	rvVehicleWeapon::Activate( activate );
	if ( !activate ) {
		SetLockState( LOCK_NONE );
		StopGuides();
	}
}

/*
================
riVCWMissileTurret::Select
================
*/
void riVCWMissileTurret::Select( bool select ) {
	rvVehicleWeapon::Select( select );
	if ( select ) {
		if ( GetPlayerDriver() ) {
			PlaySound( "snd_select", MISSILE_SELECT_SOUND );
		}
		SendGuiEvent( "anim_active_rockets" );
	} else {
		SetLockState( LOCK_NONE );
		StopGuides();
	}
}
