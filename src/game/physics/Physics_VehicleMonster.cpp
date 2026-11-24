
#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"

CLASS_DECLARATION( idPhysics_RigidBody, rvPhysics_VehicleMonster )
END_CLASS

/*
================
rvPhysics_VehicleMonster::Evaluate

  Evaluate the impulse based rigid body physics.
  Uses the rigid-body solver's bounded within-step collision handling and then
  constrains the result back to a vehicle-friendly upright orientation.
================
*/
bool rvPhysics_VehicleMonster::Evaluate( int timeStepMSec, int endTimeMSec ) {
	if ( idPhysics_RigidBody::Evaluate( timeStepMSec, endTimeMSec ) ) {

		idAngles euler			= current.i.orientation.ToAngles();
		euler.pitch				= 0.0f;
		euler.roll				= 0.0f;
		current.i.orientation	= euler.ToMat3();

		return true;
	}

	return false;
}

void rvPhysics_VehicleMonster::SetGravity ( const idVec3 & v ) {
	gravityVector = v; 
	gravityNormal = gameLocal.GetGravity( );
	gravityNormal.Normalize();
}
