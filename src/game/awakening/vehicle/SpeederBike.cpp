#include "../../Game_local.h"
#include "../../vehicle/VehicleRigid.h"
#include "../ScriptEvents.h"

/*
===============================================================================

	riVehicleSpeederBike

	The hover bike of the m06 invasion and the m07 races: a rigid-body
	vehicle whose parts (thrusters, hover suspension, the boost) do the
	driving. Once physics has run each frame the bike

		- keeps 95% of its sideways speed, so it grips instead of drifting,
		- limits pitch and roll to 45 degrees and eases both back towards
		  level, so it cannot flip, and
		- shakes its rider's view in proportion to its speed.

	Its def's "friction_angular" of 5 is what stops a turn: the expansion's
	rigid bodies took friction up to 10. Under Quake 4's ceiling of 1 the
	physics dropped the bike's whole friction set for the 0.6 fallback, and
	a held turn spun the bike up past 500 degrees a second, still turning
	two full circles after the stick was let go.

	Its four script events drive a spline tether that the shipped bike does
	not carry ("def_part_splinetether" is commented out of its driver def),
	so, as in the expansion, they are accepted and do nothing.

===============================================================================
*/

class riVehicleSpeederBike : public rvVehicleRigid {
public:
	CLASS_PROTOTYPE( riVehicleSpeederBike );

protected:
	virtual void			RunPostPhysics			( void );
	virtual float			GetMaxFriction			( void ) const;

private:
	void					Event_SetSpline			( idEntity *spline );
	void					Event_SetSpeed			( float speed, float transitionTime );
	void					Event_SetMaxGravityDistance( float distance, float transitionTime );
	void					Event_SetBoostEnabled	( float enabled );
};

CLASS_DECLARATION( rvVehicleRigid, riVehicleSpeederBike )
	EVENT( EV_SetSpline,							riVehicleSpeederBike::Event_SetSpline )
	EVENT( EV_SpeederBike_SetSpeed,					riVehicleSpeederBike::Event_SetSpeed )
	EVENT( EV_SpeederBike_SetMaxGravityDistance,	riVehicleSpeederBike::Event_SetMaxGravityDistance )
	EVENT( EV_SpeederBike_SetBoostEnabled,			riVehicleSpeederBike::Event_SetBoostEnabled )
END_CLASS

static const float SPEEDERBIKE_SIDE_GRIP		= 0.95f;
static const float SPEEDERBIKE_MAX_TILT			= 45.0f;
static const float SPEEDERBIKE_TILT_DAMPING		= 0.99f;
static const float SPEEDERBIKE_SHAKE_PER_SPEED	= 0.02f;
static const float SPEEDERBIKE_MAX_FRICTION		= 10.0f;

/*
================
riVehicleSpeederBike::GetMaxFriction
================
*/
float riVehicleSpeederBike::GetMaxFriction( void ) const {
	return SPEEDERBIKE_MAX_FRICTION;
}

/*
================
riVehicleSpeederBike::RunPostPhysics
================
*/
void riVehicleSpeederBike::RunPostPhysics( void ) {
	const idMat3 &axis = physicsObj.GetAxis();

	// grip: bleed off the sideways part of the velocity, measured along the
	// bike's own axes (axis * v would turn v by the bike's heading instead)
	const idVec3 worldVelocity = physicsObj.GetLinearVelocity();
	idVec3 local( worldVelocity * axis[0], worldVelocity * axis[1], worldVelocity * axis[2] );
	local[1] *= SPEEDERBIKE_SIDE_GRIP;
	const idVec3 velocity = local * axis;
	physicsObj.SetLinearVelocity( velocity );

	// a faint rumble for the rider that grows with speed
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player != NULL && positions.Num() && positions[ 0 ].GetDriver() == player ) {
		player->playerView.SetShakeParms( MS2SEC( gameLocal.time + 10 ), velocity.Length() * SPEEDERBIKE_SHAKE_PER_SPEED );
	}

	// stay upright
	idAngles angles = axis.ToAngles();
	angles.pitch = idMath::ClampFloat( -SPEEDERBIKE_MAX_TILT, SPEEDERBIKE_MAX_TILT, angles.pitch ) * SPEEDERBIKE_TILT_DAMPING;
	angles.roll = idMath::ClampFloat( -SPEEDERBIKE_MAX_TILT, SPEEDERBIKE_MAX_TILT, angles.roll ) * SPEEDERBIKE_TILT_DAMPING;
	physicsObj.SetAxis( angles.ToMat3() );
}

/*
================
riVehicleSpeederBike::Event_SetSpline
================
*/
void riVehicleSpeederBike::Event_SetSpline( idEntity *spline ) {
}

/*
================
riVehicleSpeederBike::Event_SetSpeed
================
*/
void riVehicleSpeederBike::Event_SetSpeed( float speed, float transitionTime ) {
}

/*
================
riVehicleSpeederBike::Event_SetMaxGravityDistance
================
*/
void riVehicleSpeederBike::Event_SetMaxGravityDistance( float distance, float transitionTime ) {
}

/*
================
riVehicleSpeederBike::Event_SetBoostEnabled
================
*/
void riVehicleSpeederBike::Event_SetBoostEnabled( float enabled ) {
}

/*
===============================================================================

	riVehiclePartBoost

	The speeder bike's boost. Holding crouch (move down) while it is ready
	fires it: for "boostEnvelopeAttackSeconds" the push forward ramps up to
	"boostForwardForceMax", holds for "boostEnvelopeSustainSeconds" and dies
	away over "boostEnvelopeDecaySeconds", while the driver's view widens by
	up to "boostFovIncreaseMax" degrees. It is ready again
	"boostEnvelopeRefreshSeconds" after the push ends. The vehicle HUD's
	"boost_active" is set while it pushes, with "snd_boost" and
	"snd_boostEnd" at either end; the Remastered HUD also reads the state
	("oq4_boost"), the push's envelope and the crouch control.

	The expansion widened the view by writing g_fov, and left it at 90
	afterwards whatever the player had chosen; this sets the vehicle's FOV
	offset instead.

===============================================================================
*/

class riVehiclePartBoost : public rvVehiclePart {
public:
	CLASS_PROTOTYPE( riVehiclePartBoost );

							riVehiclePartBoost		( void );

	void					Spawn					( void );
	void					Save					( idSaveGame *savefile ) const;
	void					Restore					( idRestoreGame *savefile );

	virtual void			RunPostPhysics			( void );
	virtual void			Activate				( bool activate );

private:
	enum boostState_t {
		BOOST_READY,
		BOOST_PUSHING,
		BOOST_RECHARGING
	};

	float					Envelope				( int elapsed ) const;
	void					SetBoostActive			( bool active );

	boostState_t			state;
	int						attackTime;
	int						sustainTime;
	int						decayTime;
	int						refreshTime;
	float					forceMax;
	float					fovIncreaseMax;
	int						startTime;
	int						readyTime;
};

CLASS_DECLARATION( rvVehiclePart, riVehiclePartBoost )
END_CLASS

/*
================
riVehiclePartBoost::riVehiclePartBoost
================
*/
riVehiclePartBoost::riVehiclePartBoost( void ) {
	state = BOOST_READY;
	attackTime = 0;
	sustainTime = 0;
	decayTime = 0;
	refreshTime = 0;
	forceMax = 0.0f;
	fovIncreaseMax = 0.0f;
	startTime = 0;
	readyTime = 0;
}

/*
================
riVehiclePartBoost::Spawn
================
*/
void riVehiclePartBoost::Spawn( void ) {
	attackTime = SEC2MS( spawnArgs.GetFloat( "boostEnvelopeAttackSeconds", "0.5" ) );
	sustainTime = SEC2MS( spawnArgs.GetFloat( "boostEnvelopeSustainSeconds", "3.0" ) );
	decayTime = SEC2MS( spawnArgs.GetFloat( "boostEnvelopeDecaySeconds", "2.0" ) );
	refreshTime = SEC2MS( spawnArgs.GetFloat( "boostEnvelopeRefreshSeconds", "6.0" ) );
	forceMax = spawnArgs.GetFloat( "boostForwardForceMax", "10000" );
	fovIncreaseMax = spawnArgs.GetFloat( "boostFovIncreaseMax", "20" );
	state = BOOST_READY;
	startTime = 0;
	readyTime = gameLocal.time;
}

/*
================
riVehiclePartBoost::Save
================
*/
void riVehiclePartBoost::Save( idSaveGame *savefile ) const {
	savefile->WriteInt( state );
	savefile->WriteInt( attackTime );
	savefile->WriteInt( sustainTime );
	savefile->WriteInt( decayTime );
	savefile->WriteInt( refreshTime );
	savefile->WriteFloat( forceMax );
	savefile->WriteFloat( fovIncreaseMax );
	savefile->WriteInt( startTime );
	savefile->WriteInt( readyTime );
}

/*
================
riVehiclePartBoost::Restore
================
*/
void riVehiclePartBoost::Restore( idRestoreGame *savefile ) {
	int value;
	savefile->ReadInt( value );
	state = (boostState_t)value;
	savefile->ReadInt( attackTime );
	savefile->ReadInt( sustainTime );
	savefile->ReadInt( decayTime );
	savefile->ReadInt( refreshTime );
	savefile->ReadFloat( forceMax );
	savefile->ReadFloat( fovIncreaseMax );
	savefile->ReadInt( startTime );
	savefile->ReadInt( readyTime );
}

/*
================
riVehiclePartBoost::Activate

Leaving the seat ends thrust and its view effect immediately. Preserve the
original recharge deadline so getting out and back in cannot reset the boost.
================
*/
void riVehiclePartBoost::Activate( bool activate ) {
	rvVehiclePart::Activate( activate );
	if ( !activate && state == BOOST_PUSHING ) {
		state = BOOST_RECHARGING;
		SetBoostActive( false );
	}
}

/*
================
riVehiclePartBoost::Envelope

How hard the boost pushes, 0..1, "elapsed" milliseconds into it.
================
*/
float riVehiclePartBoost::Envelope( int elapsed ) const {
	if ( elapsed < 0 || elapsed >= attackTime + sustainTime + decayTime ) {
		return 0.0f;
	}
	if ( elapsed < attackTime ) {
		return (float)elapsed / attackTime;
	}
	if ( elapsed < attackTime + sustainTime ) {
		return 1.0f;
	}
	return 1.0f - (float)( elapsed - attackTime - sustainTime ) / decayTime;
}

/*
================
riVehiclePartBoost::SetBoostActive
================
*/
void riVehiclePartBoost::SetBoostActive( bool active ) {
	rvVehicle *vehicle = parent.GetEntity();
	if ( vehicle == NULL ) {
		return;
	}
	if ( vehicle->GetHud() ) {
		vehicle->GetHud()->SetStateBool( "boost_active", active );
	}
	vehicle->StartSound( active ? "snd_boost" : "snd_boostEnd", SND_CHANNEL_ANY, 0, false, NULL );
	if ( !active ) {
		vehicle->SetFovOffset( 0.0f );
	}
}

/*
================
riVehiclePartBoost::RunPostPhysics
================
*/
void riVehiclePartBoost::RunPostPhysics( void ) {
	rvVehicle *vehicle = parent.GetEntity();
	if ( vehicle == NULL ) {
		return;
	}

	const bool canBoost = position != NULL && position->IsOccupied() && vehicle->IsMovementEnabled()
		&& !vehicle->IsHidden() && vehicle->health > 0;
	if ( !canBoost && state == BOOST_PUSHING ) {
		state = BOOST_RECHARGING;
		SetBoostActive( false );
	}

	switch ( state ) {
		case BOOST_READY:
			if ( canBoost && position->mInputCmd.upmove < 0 ) {
				state = BOOST_PUSHING;
				startTime = gameLocal.time;
				readyTime = gameLocal.time + attackTime + sustainTime + decayTime + refreshTime;
				SetBoostActive( true );
			}
			break;

		case BOOST_PUSHING: {
			const int elapsed = gameLocal.time - startTime;
			if ( elapsed >= attackTime + sustainTime + decayTime ) {
				state = BOOST_RECHARGING;
				SetBoostActive( false );
				break;
			}
			const float envelope = Envelope( elapsed );
			// the view widens fast and settles slowly
			const float ease = 1.0f - ( 1.0f - envelope ) * ( 1.0f - envelope );
			vehicle->SetFovOffset( ease * fovIncreaseMax );

			UpdateOrigin();
			const float force = OpenQ4_TurboModeActive() ? forceMax * OPENQ4_TURBO_VEHICLE_SPEED_SCALE : forceMax;
			vehicle->GetPhysics()->ApplyImpulse( 0, worldOrigin, worldAxis[0] * force * envelope );
			break;
		}

		case BOOST_RECHARGING:
			if ( gameLocal.time >= readyTime ) {
				state = BOOST_READY;
			}
			break;
	}

	// openQ4: the Remastered HUD prompt shows Boost on the crouch control while
	// a boost is ready and Boosting while it pushes, and the view streaks with
	// the push's envelope. The expansion's HUD named only CROUCH, in text.
	if ( idUserInterface *hud = vehicle->GetHud() ) {
		hud->SetStateInt( "oq4_boost", state );
		hud->SetStateFloat( "oq4_boost_envelope", state == BOOST_PUSHING ? Envelope( gameLocal.time - startTime ) : 0.0f );
		hud->SetStateString( "oq4_key_crouch", common->KeysFromBinding( "_movedown" ) );
	}
}
