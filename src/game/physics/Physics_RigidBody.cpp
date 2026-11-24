
#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "PhysicsTelemetry.h"

CLASS_DECLARATION( idPhysics_Base, idPhysics_RigidBody )
END_CLASS

const float STOP_SPEED		= 10.0f;
const int RB_CONTACTS_MAX = 32;
const int RB_MAX_COLLISION_ITERATIONS = 3;
const int RB_MAX_PLANAR_CONTACTS = 4;
const float RB_COLLISION_MIN_TIMESTEP = 0.0001f;
const float RB_COLLISION_FRACTION_EPSILON = 0.0001f;
const float RB_CONTACT_MERGE_EPSILON = 2.0f;
const float RB_WATER_FRICTION_SCALE = 20.0f;
// openQ4: fraction of a submerged body's weight the liquid holds up. Below 1 everything still
// sinks, just slowly, which is what Quake's crates and gibs do.
const float RB_LIQUID_BUOYANCY = 0.8f;

static ID_INLINE int RigidBodyClipMask( int clipMask ) {
	// openQ4: liquids are passed through, not collided with - all three of them, not just water
	return clipMask & ~MASK_WATER;
}

static ID_INLINE idMat3 RigidBodyWorldInertiaTensor( const idMat3 &orientation, const idMat3 &inertiaTensor ) {
	return orientation.Transpose() * inertiaTensor * orientation;
}

static ID_INLINE idMat3 RigidBodyInverseWorldInertiaTensor( const idMat3 &orientation, const idMat3 &inverseInertiaTensor ) {
	return orientation.Transpose() * inverseInertiaTensor * orientation;
}


#undef RB_TIMINGS

#ifdef RB_TIMINGS
static int lastTimerReset = 0;
static int numRigidBodies = 0;
static idTimer timer_total, timer_collision;
#endif


/*
================
RigidBodyDerivatives
================
*/
void RigidBodyDerivatives( const float t, const void *clientData, const float *state, float *derivatives ) {
	const idPhysics_RigidBody *p = (idPhysics_RigidBody *) clientData;
	rigidBodyIState_t *s = (rigidBodyIState_t *) state;
	// NOTE: this struct should be build conform rigidBodyIState_t
	struct rigidBodyDerivatives_s {
		idVec3				linearVelocity;
		idMat3				angularMatrix;
		idVec3				force;
		idVec3				torque;
	} *d = (struct rigidBodyDerivatives_s *) derivatives;
	idVec3 angularVelocity;
	idMat3 inverseWorldInertiaTensor;

	inverseWorldInertiaTensor = s->orientation * p->inverseInertiaTensor * s->orientation.Transpose();
	angularVelocity = inverseWorldInertiaTensor * s->angularMomentum;
	// derivatives
	d->linearVelocity = p->inverseMass * s->linearMomentum;
	d->angularMatrix = SkewSymmetric( angularVelocity ) * s->orientation;
	d->force = - p->linearFriction * s->linearMomentum + p->current.externalForce;
	d->torque = - p->angularFriction * s->angularMomentum + p->current.externalTorque;
}

/*
================
idPhysics_RigidBody::Integrate

  Calculate next state from the current state using an integrator.
================
*/
void idPhysics_RigidBody::Integrate( float deltaTime, rigidBodyPState_t &next ) {
	idVec3 position;

	position = current.i.position;
	current.i.position += centerOfMass * current.i.orientation;

	current.i.orientation.TransposeSelf();

	integrator->Evaluate( (float *) &current.i, (float *) &next.i, 0, deltaTime );
	next.i.orientation.OrthoNormalizeSelf();

	// apply gravity
	next.i.linearMomentum += deltaTime * gravityVector * mass;

	current.i.orientation.TransposeSelf();
	next.i.orientation.TransposeSelf();

	current.i.position = position;
	next.i.position -= centerOfMass * next.i.orientation;

	next.atRest = current.atRest;
}

/*
================
idPhysics_RigidBody::CollisionImpulse

  Calculates the collision impulse using the velocity relative to the collision object.
  The current state should be set to the moment of impact.
================
*/
bool idPhysics_RigidBody::CollisionImpulse( const trace_t &collision, idVec3 &impulse ) {
	idVec3 r, linearVelocity, angularVelocity, velocity;
	idMat3 inverseWorldInertiaTensor;
	float impulseNumerator, impulseDenominator, vel;
	impactInfo_t info;
	idEntity *ent;

	// get info from other entity involved
	ent = gameLocal.entities[collision.c.entityNum];
	ent->GetImpactInfo( self, collision.c.id, collision.c.point, &info );

	// collision point relative to the body center of mass
	r = collision.c.point - (current.i.position + centerOfMass * current.i.orientation);

	// the velocity at the collision point
	linearVelocity = inverseMass * current.i.linearMomentum;
	inverseWorldInertiaTensor = RigidBodyInverseWorldInertiaTensor( current.i.orientation, inverseInertiaTensor );
	angularVelocity = inverseWorldInertiaTensor * current.i.angularMomentum;
	velocity = linearVelocity + angularVelocity.Cross(r);
	// subtract velocity of other entity
	velocity -= info.velocity;

	// velocity in normal direction
	vel = velocity * collision.c.normal;

	if ( vel > -STOP_SPEED ) {
		impulseNumerator = STOP_SPEED;
	}
	else {
		impulseNumerator = -( 1.0f + bouncyness ) * vel;
	}
	impulseDenominator = inverseMass + ( ( inverseWorldInertiaTensor * r.Cross( collision.c.normal ) ).Cross( r ) * collision.c.normal );
	if ( info.invMass ) {
		impulseDenominator += info.invMass + ( ( info.invInertiaTensor * info.position.Cross( collision.c.normal ) ).Cross( info.position ) * collision.c.normal );
	}
	impulse = (impulseNumerator / impulseDenominator) * collision.c.normal;

	// update linear and angular momentum with impulse
	current.i.linearMomentum += impulse;
	current.i.angularMomentum += r.Cross(impulse);

	// if no movement at all don't blow up
	if ( collision.fraction < 0.0001f ) {
		current.i.linearMomentum *= 0.5f;
		current.i.angularMomentum *= 0.5f;
	}

	// callback to self to let the entity know about the collision
	return self->Collide( collision, velocity );
}

/*
================
idPhysics_RigidBody::CheckForCollisions

  Check for collisions between the current and next state.
  If there is a collision the next state is set to the state at the moment of impact.
================
*/
bool idPhysics_RigidBody::CheckForCollisions( const float deltaTime, rigidBodyPState_t &next, trace_t &collision ) {
//#define TEST_COLLISION_DETECTION
	idMat3 axis;
	idRotation rotation;
	bool collided = false;

#ifdef TEST_COLLISION_DETECTION
	bool startsolid;
// RAVEN BEGIN
// ddynerman: multiple collision worlds
	if ( gameLocal.Contents( self, current.i.position, clipModel, current.i.orientation, RigidBodyClipMask( clipMask ), self ) ) {
// RAVEN END
		startsolid = true;
	}
#endif

	TransposeMultiply( current.i.orientation, next.i.orientation, axis );
	rotation = axis.ToRotation();
	rotation.SetOrigin( current.i.position );

	// if there was a collision
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	if ( gameLocal.Motion( self, collision, current.i.position, next.i.position, rotation, clipModel, current.i.orientation, RigidBodyClipMask( clipMask ), self ) ) {
// RAVEN END
		// set the next state to the state at the moment of impact
		next.i.position = collision.endpos;
		next.i.orientation = collision.endAxis;
		next.i.linearMomentum = current.i.linearMomentum;
		next.i.angularMomentum = current.i.angularMomentum;
		collided = true;
	}

#ifdef TEST_COLLISION_DETECTION
// RAVEN BEGIN
// ddynerman: multiple collision worlds
	if ( gameLocal.Contents( self, next.i.position, clipModel, next.i.orientation, RigidBodyClipMask( clipMask ), self ) ) {
// RAVEN END
		if ( !startsolid ) {
			int bah = 1;
		}
	}
#endif
	return collided;
}

/*
================
idPhysics_RigidBody::ContactFriction

  Does not solve friction for multiple simultaneous contacts but applies contact friction in isolation.
  Uses absolute velocity at the contact points instead of the velocity relative to the contact object.
================
*/
void idPhysics_RigidBody::ContactFriction( float deltaTime ) {
	int i;
	float magnitude, impulseNumerator, impulseDenominator;
	idMat3 inverseWorldInertiaTensor;
	idVec3 linearVelocity, angularVelocity;
	idVec3 massCenter, r, velocity, normal, impulse, normalVelocity;

	inverseWorldInertiaTensor = RigidBodyInverseWorldInertiaTensor( current.i.orientation, inverseInertiaTensor );

	massCenter = current.i.position + centerOfMass * current.i.orientation;

	for ( i = 0; i < contacts.Num(); i++ ) {

		r = contacts[i].point - massCenter;

		// calculate velocity at contact point
		linearVelocity = inverseMass * current.i.linearMomentum;
		angularVelocity = inverseWorldInertiaTensor * current.i.angularMomentum;
		velocity = linearVelocity + angularVelocity.Cross(r);

		// velocity along normal vector
		normalVelocity = ( velocity * contacts[i].normal ) * contacts[i].normal;

		// calculate friction impulse
		normal = -( velocity - normalVelocity );
		magnitude = normal.Normalize();
		impulseNumerator = contactFriction * magnitude;
		impulseDenominator = inverseMass + ( ( inverseWorldInertiaTensor * r.Cross( normal ) ).Cross( r ) * normal );
		impulse = (impulseNumerator / impulseDenominator) * normal;

		// apply friction impulse
		current.i.linearMomentum += impulse;
		current.i.angularMomentum += r.Cross(impulse);

		// if moving towards the surface at the contact point
		if ( normalVelocity * contacts[i].normal < 0.0f ) {
			// calculate impulse
			normal = -normalVelocity;
			impulseNumerator = normal.Normalize();
			impulseDenominator = inverseMass + ( ( inverseWorldInertiaTensor * r.Cross( normal ) ).Cross( r ) * normal );
			impulse = (impulseNumerator / impulseDenominator) * normal;

			// apply impulse
			current.i.linearMomentum += impulse;
			current.i.angularMomentum += r.Cross( impulse );
		}
	}
}

/*
================
idPhysics_RigidBody::TestIfAtRest

  Returns true if the body is considered at rest.
  Does not catch all cases where the body is at rest but is generally good enough.
================
*/
bool idPhysics_RigidBody::TestIfAtRest( void ) const {
	int i;
	float gv;
	idVec3 v, av, normal, point;
	idMat3 inverseWorldInertiaTensor;
	idFixedWinding contactWinding;

	if ( current.atRest >= 0 ) {
		return true;
	}

	// need at least 3 contact points to come to rest
	if ( contacts.Num() < 3 ) {
		return false;
	}

	// get average contact plane normal
	normal.Zero();
	for ( i = 0; i < contacts.Num(); i++ ) {
		normal += contacts[i].normal;
	}
	normal /= (float) contacts.Num();
	normal.Normalize();

	// if on a too steep surface
	if ( (normal * gravityNormal) > -0.7f ) {
		return false;
	}

	// create bounds for contact points
	contactWinding.Clear();
	for ( i = 0; i < contacts.Num(); i++ ) {
		// project point onto plane through origin orthogonal to the gravity
		point = contacts[i].point - (contacts[i].point * gravityNormal) * gravityNormal;
		contactWinding.AddToConvexHull( point, gravityNormal );
	}

	// need at least 3 contact points to come to rest
	if ( contactWinding.GetNumPoints() < 3 ) {
		return false;
	}

	// center of mass in world space
	point = current.i.position + centerOfMass * current.i.orientation;
	point -= (point * gravityNormal) * gravityNormal;

	// if the point is not inside the winding
	if ( !contactWinding.PointInside( gravityNormal, point, 0 ) ) {
		return false;
	}

	// linear velocity of body
	v = inverseMass * current.i.linearMomentum;
	// linear velocity in gravity direction
	gv = v * gravityNormal;
	// linear velocity orthogonal to gravity direction
	v -= gv * gravityNormal;

	// if too much velocity orthogonal to gravity direction
	if ( v.Length() > STOP_SPEED ) {
		return false;
	}
	// if too much velocity in gravity direction
	if ( gv > 2.0f * STOP_SPEED || gv < -2.0f * STOP_SPEED ) {
		return false;
	}

	// calculate rotational velocity
	inverseWorldInertiaTensor = RigidBodyInverseWorldInertiaTensor( current.i.orientation, inverseInertiaTensor );
	av = inverseWorldInertiaTensor * current.i.angularMomentum;

	// if too much rotational velocity
	if ( av.LengthSqr() > STOP_SPEED ) {
		return false;
	}

	return true;
}

/*
================
idPhysics_RigidBody::DropToFloorAndRest

  Drops the object straight down to the floor and verifies if the object is at rest on the floor.
================
*/
void idPhysics_RigidBody::DropToFloorAndRest( void ) {
	idVec3 down;
	trace_t tr;

	if ( testSolid ) {

		testSolid = false;
// RAVEN BEGIN
// ddynerman: multiple collision worlds
		if ( gameLocal.Contents( self, current.i.position, clipModel, current.i.orientation, RigidBodyClipMask( clipMask ), self ) ) {
// RAVEN END
			gameLocal.DWarning( "rigid body in solid for entity '%s' type '%s' at (%s)",
								self->name.c_str(), self->GetType()->classname, current.i.position.ToString(0) );
			Rest();
			dropToFloor = false;
			return;
		}
	}

	// put the body on the floor
	down = current.i.position + gravityNormal * 128.0f;
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	gameLocal.Translation( self, tr, current.i.position, down, clipModel, current.i.orientation, RigidBodyClipMask( clipMask ), self );
// RAVEN END
	current.i.position = tr.endpos;
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	clipModel->Link( self, clipModel->GetId(), tr.endpos, current.i.orientation );
// RAVEN END

	// if on the floor already
	if ( tr.fraction == 0.0f ) {
		// test if we are really at rest
		EvaluateContacts();
		if ( !TestIfAtRest() ) {
			// Rest() below fully recovers this authored-placement edge case; keep
			// the detail visible in developer output without reporting a fault.
			gameLocal.DPrintf( "rigid body rest heuristic did not settle entity '%s' type '%s' at (%s); forcing rest\n",
								self->name.c_str(), self->GetType()->classname, current.i.position.ToString(0) );
		}
		Rest();
		dropToFloor = false;
	} else if ( IsOutsideWorld() ) {
		gameLocal.Warning( "rigid body outside world bounds for entity '%s' type '%s' at (%s)",
							self->name.c_str(), self->GetType()->classname, current.i.position.ToString(0) );
		Rest();
		dropToFloor = false;
	}
}

/*
================
idPhysics_RigidBody::DebugDraw
================
*/
void idPhysics_RigidBody::DebugDraw( void ) {

	if ( rb_showBodies.GetBool() || ( rb_showActive.GetBool() && current.atRest < 0 ) ) {
		collisionModelManager->DrawModel( clipModel->GetCollisionModel(), clipModel->GetOrigin(), clipModel->GetAxis(), vec3_origin, mat3_identity, 0.0f );
	}

	if ( rb_showMass.GetBool() ) {
// RAVEN BEGIN
// bdube: draw center of mass at the center of mass
		gameRenderWorld->DrawText( va( "\n%1.2f", mass ), current.i.position + centerOfMass * current.i.orientation, 0.08f, colorCyan, gameLocal.GetLocalPlayer()->viewAngles.ToMat3(), 1 );
// RAVEN END
	}

	if ( rb_showInertia.GetBool() ) {
		idMat3 &I = inertiaTensor;
		gameRenderWorld->DrawText( va( "\n\n\n( %.1f %.1f %.1f )\n( %.1f %.1f %.1f )\n( %.1f %.1f %.1f )",
									I[0].x, I[0].y, I[0].z,
									I[1].x, I[1].y, I[1].z,
									I[2].x, I[2].y, I[2].z ),
									current.i.position, 0.05f, colorCyan, gameLocal.GetLocalPlayer()->viewAngles.ToMat3(), 1 );
	}

	if ( rb_showVelocity.GetBool() ) {
		DrawVelocity( clipModel->GetId(), 0.1f, 4.0f );
	}
	
// RAVEN BEGIN
// bdube: added more debug info
	if ( rb_showContacts.GetBool() ) {
		int i;
		for ( i = 0; i < contacts.Num(); i ++ ) {
			idVec3 x, y;
			contacts[i].normal.NormalVectors( x, y );
			gameRenderWorld->DebugLine( colorWhite, contacts[i].point, contacts[i].point + 6.0f * contacts[i].normal );
			gameRenderWorld->DebugLine( colorWhite, contacts[i].point - 2.0f * x, contacts[i].point + 2.0f * x );
			gameRenderWorld->DebugLine( colorWhite, contacts[i].point - 2.0f * y, contacts[i].point + 2.0f * y );
		}		
	}	
// RAVEN END
}

/*
================
idPhysics_RigidBody::idPhysics_RigidBody
================
*/
idPhysics_RigidBody::idPhysics_RigidBody( void ) {

	// set default rigid body properties
	SetClipMask( MASK_SOLID );
	SetBouncyness( 0.6f );
	SetFriction( 0.6f, 0.6f, 0.0f );
	clipModel = NULL;

	memset( &current, 0, sizeof( current ) );

	current.atRest = -1;
// RAVEN BEGIN
// bdube: use GetMSec access rather than USERCMD_TIME
	current.lastTimeStep = gameLocal.GetMSec();
// RAVEN END

	current.i.position.Zero();
	current.i.orientation.Identity();

	current.i.linearMomentum.Zero();
	current.i.angularMomentum.Zero();

	saved = current;

	mass = 1.0f;
	inverseMass = 1.0f;
	centerOfMass.Zero();
	inertiaTensor.Identity();
	inverseInertiaTensor.Identity();

	// use the least expensive euler integrator
	integrator = new idODE_Euler( sizeof(rigidBodyIState_t) / sizeof(float), RigidBodyDerivatives, this );

	dropToFloor = false;
	testSolid = false;
	noImpact = false;
	noContact = false;

	hasMaster = false;
	isOrientated = false;

	enhancedEligibility = -1;
	enhancedQuietFrames = 0;
	enhancedUndisturbed = true;
	enhancedNumCached = 0;

#ifdef RB_TIMINGS
	lastTimerReset = 0;
#endif
}

/*
================
idPhysics_RigidBody::~idPhysics_RigidBody
================
*/
idPhysics_RigidBody::~idPhysics_RigidBody( void ) {
	if ( clipModel ) {
		delete clipModel;
		clipModel = NULL;
	}
	delete integrator;
}

/*
================
idPhysics_RigidBody_SavePState
================
*/
void idPhysics_RigidBody_SavePState( idSaveGame *savefile, const rigidBodyPState_t &state ) {
	savefile->WriteInt( state.atRest );
	savefile->WriteFloat( state.lastTimeStep );
	savefile->WriteVec3( state.localOrigin );
	savefile->WriteMat3( state.localAxis );
	savefile->WriteVec6( state.pushVelocity );
	savefile->WriteVec3( state.externalForce );
	savefile->WriteVec3( state.externalTorque );

	savefile->WriteVec3( state.i.position );
	savefile->WriteMat3( state.i.orientation );
	savefile->WriteVec3( state.i.linearMomentum );
	savefile->WriteVec3( state.i.angularMomentum );
}

/*
================
idPhysics_RigidBody_RestorePState
================
*/
void idPhysics_RigidBody_RestorePState( idRestoreGame *savefile, rigidBodyPState_t &state ) {
	savefile->ReadInt( state.atRest );
	savefile->ReadFloat( state.lastTimeStep );
	savefile->ReadVec3( state.localOrigin );
	savefile->ReadMat3( state.localAxis );
	if ( savefile->GetOpenQ4SaveGameCompatibilityVersion() >= OPENQ4_SAVEGAME_TYPED_COMPATIBILITY_VERSION ) {
		savefile->ReadVec6( state.pushVelocity );
	} else {
		savefile->Read( &state.pushVelocity, sizeof( state.pushVelocity ) );
	}
	savefile->ReadVec3( state.externalForce );
	savefile->ReadVec3( state.externalTorque );

	savefile->ReadVec3( state.i.position );
	savefile->ReadMat3( state.i.orientation );
	savefile->ReadVec3( state.i.linearMomentum );
	savefile->ReadVec3( state.i.angularMomentum );
}

/*
================
idPhysics_RigidBody::Save
================
*/
void idPhysics_RigidBody::Save( idSaveGame *savefile ) const {

	idPhysics_RigidBody_SavePState( savefile, current );
	idPhysics_RigidBody_SavePState( savefile, saved );

	savefile->WriteFloat( linearFriction );
	savefile->WriteFloat( angularFriction );
	savefile->WriteFloat( contactFriction );
	savefile->WriteFloat( bouncyness );
	savefile->WriteClipModel( clipModel );

	savefile->WriteFloat( mass );
	savefile->WriteFloat( inverseMass );
	savefile->WriteVec3( centerOfMass );
	savefile->WriteMat3( inertiaTensor );
	savefile->WriteMat3( inverseInertiaTensor );

	savefile->WriteBool( dropToFloor );
	savefile->WriteBool( testSolid );
	savefile->WriteBool( noImpact );
	savefile->WriteBool( noContact );

	savefile->WriteBool( hasMaster );
	savefile->WriteBool( isOrientated );
}

/*
================
idPhysics_RigidBody::Restore
================
*/
void idPhysics_RigidBody::Restore( idRestoreGame *savefile ) {

	idPhysics_RigidBody_RestorePState( savefile, current );
	idPhysics_RigidBody_RestorePState( savefile, saved );

	savefile->ReadFloat( linearFriction );
	savefile->ReadFloat( angularFriction );
	savefile->ReadFloat( contactFriction );
	savefile->ReadFloat( bouncyness );
	savefile->ReadClipModel( clipModel );

	savefile->ReadFloat( mass );
	savefile->ReadFloat( inverseMass );
	savefile->ReadVec3( centerOfMass );
	savefile->ReadMat3( inertiaTensor );
	savefile->ReadMat3( inverseInertiaTensor );

	savefile->ReadBool( dropToFloor );
	savefile->ReadBool( testSolid );
	savefile->ReadBool( noImpact );
	savefile->ReadBool( noContact );

	savefile->ReadBool( hasMaster );
	savefile->ReadBool( isOrientated );
}

/*
================
idPhysics_RigidBody::SetClipModel
================
*/
#define MAX_INERTIA_SCALE		10.0f

void idPhysics_RigidBody::SetClipModel( idClipModel *model, const float density, int id, bool freeOld ) {
	int minIndex;
	idMat3 inertiaScale;

	assert( self );
	assert( model );					// we need a clip model
	assert( model->IsTraceModel() );	// and it should be a trace model
	assert( density > 0.0f );			// density should be valid

	if ( clipModel && clipModel != model && freeOld ) {
		delete clipModel;
	}
	clipModel = model;
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	clipModel->Link( self, 0, current.i.position, current.i.orientation );
// RAVEN END

	// get mass properties from the trace model
	clipModel->GetMassProperties( density, mass, centerOfMass, inertiaTensor );

	// check whether or not the clip model has valid mass properties
	if ( mass <= 0.0f || FLOAT_IS_NAN( mass ) ) {
		gameLocal.Warning( "idPhysics_RigidBody::SetClipModel: invalid mass for entity '%s' type '%s'",
							self->name.c_str(), self->GetType()->classname );
		mass = 1.0f;
		centerOfMass.Zero();
		inertiaTensor.Identity();
	}

	// check whether or not the inertia tensor is balanced
	minIndex = Min3Index( inertiaTensor[0][0], inertiaTensor[1][1], inertiaTensor[2][2] );
	inertiaScale.Identity();
	inertiaScale[0][0] = inertiaTensor[0][0] / inertiaTensor[minIndex][minIndex];
	inertiaScale[1][1] = inertiaTensor[1][1] / inertiaTensor[minIndex][minIndex];
	inertiaScale[2][2] = inertiaTensor[2][2] / inertiaTensor[minIndex][minIndex];

	if ( inertiaScale[0][0] > MAX_INERTIA_SCALE || inertiaScale[1][1] > MAX_INERTIA_SCALE || inertiaScale[2][2] > MAX_INERTIA_SCALE ) {
		// The bounded clamp below produces a valid tensor, so this is a
		// developer-visible recovery rather than an unresolved physics warning.
		gameLocal.DPrintf( "idPhysics_RigidBody::SetClipModel: clamping unbalanced inertia tensor for entity '%s' type '%s'\n",
							self->name.c_str(), self->GetType()->classname );
		float min = inertiaTensor[minIndex][minIndex] * MAX_INERTIA_SCALE;
		inertiaScale[(minIndex+1)%3][(minIndex+1)%3] = min / inertiaTensor[(minIndex+1)%3][(minIndex+1)%3];
		inertiaScale[(minIndex+2)%3][(minIndex+2)%3] = min / inertiaTensor[(minIndex+2)%3][(minIndex+2)%3];
		inertiaTensor *= inertiaScale;
	}

	inverseMass = 1.0f / mass;
	inverseInertiaTensor = inertiaTensor.Inverse() * ( 1.0f / 6.0f );

	current.i.linearMomentum.Zero();
	current.i.angularMomentum.Zero();
}

/*
================
idPhysics_RigidBody::GetClipModel
================
*/
idClipModel *idPhysics_RigidBody::GetClipModel( int id ) const {
	return clipModel;
}

/*
================
idPhysics_RigidBody::GetNumClipModels
================
*/
int idPhysics_RigidBody::GetNumClipModels( void ) const {
	return 1;
}

/*
================
idPhysics_RigidBody::SetMass
================
*/
void idPhysics_RigidBody::SetMass( float mass, int id ) {
	assert( mass > 0.0f );
	inertiaTensor *= mass / this->mass;
	inverseInertiaTensor = inertiaTensor.Inverse() * (1.0f / 6.0f);
	this->mass = mass;
	inverseMass = 1.0f / mass;
}

/*
================
idPhysics_RigidBody::GetMass
================
*/
float idPhysics_RigidBody::GetMass( int id ) const {
	return mass;
}

// RAVEN BEGIN
// bdube: means of getting center of mass
/*
================
idPhysics_RigidBody::GetCenterMass
================
*/
idVec3 idPhysics_RigidBody::GetCenterMass( int id ) const 
{
	return (current.i.position + centerOfMass * current.i.orientation);
}
// RAVEN END

/*
================
idPhysics_RigidBody::SetFriction

openQ4: a value outside 0..maxFriction drops all three, as Quake 4 does
against its ceiling of 1. The Awakening's game code raised the ceiling to 10
for every rigid body, and its speeder bike's def relies on that.
================
*/
void idPhysics_RigidBody::SetFriction( const float linear, const float angular, const float contact, const float maxFriction ) {
	if (	linear < 0.0f || linear > maxFriction ||
			angular < 0.0f || angular > maxFriction ||
			contact < 0.0f || contact > maxFriction ) {
		return;
	}
	linearFriction = linear;
	angularFriction = angular;
	contactFriction = contact;
}

/*
================
idPhysics_RigidBody::SetBouncyness
================
*/
void idPhysics_RigidBody::SetBouncyness( const float b ) {
	if ( b < 0.0f || b > 1.0f ) {
		return;
	}
	bouncyness = b;
}

/*
================
idPhysics_RigidBody::Rest
================
*/
void idPhysics_RigidBody::Rest( void ) {
	current.atRest = gameLocal.time;
	current.i.linearMomentum.Zero();
	current.i.angularMomentum.Zero();
	self->BecomeInactive( TH_PHYSICS );
}

/*
================
idPhysics_RigidBody::DropToFloor
================
*/
void idPhysics_RigidBody::DropToFloor( void ) {
	dropToFloor = true;
	testSolid = true;
}

/*
================
idPhysics_RigidBody::NoContact
================
*/
void idPhysics_RigidBody::NoContact( void ) {
	noContact = true;
}

/*
================
idPhysics_RigidBody::Activate
================
*/
void idPhysics_RigidBody::Activate( void ) {
	// openQ4: a body woken from rest counts its quiet steps again (enhanced
	// sleeping); nudges while it is already awake do not restart the count
	if ( current.atRest >= 0 ) {
		OpenQ4Physics::GetTelemetry().NoteCounter( OpenQ4Physics::COUNTER_WAKES );
		enhancedQuietFrames = 0;
	}
	current.atRest = -1;
	self->BecomeActive( TH_PHYSICS );
}

/*
================
idPhysics_RigidBody::PutToRest

  put to rest untill something collides with this physics object
================
*/
void idPhysics_RigidBody::PutToRest( void ) {
	Rest();
}

/*
================
idPhysics_RigidBody::EnableImpact
================
*/
void idPhysics_RigidBody::EnableImpact( void ) {
	noImpact = false;
}

/*
================
idPhysics_RigidBody::DisableImpact
================
*/
void idPhysics_RigidBody::DisableImpact( void ) {
	noImpact = true;
}

/*
================
idPhysics_RigidBody::SetContents
================
*/
void idPhysics_RigidBody::SetContents( int contents, int id ) {
	clipModel->SetContents( contents );
}

/*
================
idPhysics_RigidBody::GetContents
================
*/
int idPhysics_RigidBody::GetContents( int id ) const {
	return clipModel->GetContents();
}

/*
================
idPhysics_RigidBody::GetBounds
================
*/
const idBounds &idPhysics_RigidBody::GetBounds( int id ) const {
	return clipModel->GetBounds();
}

/*
================
idPhysics_RigidBody::GetAbsBounds
================
*/
const idBounds &idPhysics_RigidBody::GetAbsBounds( int id ) const {
	return clipModel->GetAbsBounds();
}

/*
================
idPhysics_RigidBody::EvaluateLegacy

  Evaluate the impulse based rigid body physics.
  When a collision occurs an impulse is applied at the moment of impact and a
  small bounded amount of the remaining time is processed within the same step.
================
*/
bool idPhysics_RigidBody::EvaluateLegacy( int timeStepMSec, int endTimeMSec ) {
	rigidBodyPState_t next, impactState;
	idAngles angles;
	trace_t collision;
	trace_t impactCollisions[RB_MAX_COLLISION_ITERATIONS];
	idVec3 impulse;
	idVec3 impactImpulses[RB_MAX_COLLISION_ITERATIONS];
	idEntity *ent;
	idVec3 oldOrigin, masterOrigin;
	idMat3 oldAxis, masterAxis;
	float timeStep, remainingTime, collisionTime;
	int i, collisionIterations, numImpactCollisions;
	bool collided, cameToRest = false;

	timeStep = MS2SEC( timeStepMSec );
	current.lastTimeStep = timeStep;

	if ( hasMaster ) {
		oldOrigin = current.i.position;
		oldAxis = current.i.orientation;
		self->GetMasterPosition( masterOrigin, masterAxis );
		current.i.position = masterOrigin + current.localOrigin * masterAxis;
		if ( isOrientated ) {
			current.i.orientation = current.localAxis * masterAxis;
		}
		else {
			current.i.orientation = current.localAxis;
		}
// RAVEN BEGIN
// ddynerman: multiple clip worlds
		clipModel->Link( self, clipModel->GetId(), current.i.position, current.i.orientation );
// RAVEN END
		const bool movedWithMaster = ( current.i.position != oldOrigin || current.i.orientation != oldAxis );
		if ( movedWithMaster ) {
			// Bound rigid bodies do not normally run pusher logic. For scripted
			// convoy vehicles this can leave the visible vehicle body non-pushing
			// because the bind master is either non-solid or uses a tiny proxy clip.
			// In that case, run a proxy push from the rigid body's actual motion.
			bool needsProxyPush = true;
			idEntity *bindMaster = self->GetBindMaster();
			if ( bindMaster && bindMaster->GetPhysics() ) {
				const idPhysics *masterPhysics = bindMaster->GetPhysics();
				const idClipModel *masterClip = masterPhysics->GetClipModel();
				const float selfVolume = clipModel->GetBounds().GetVolume();
				const float masterVolume = masterClip ? masterClip->GetBounds().GetVolume() : 0.0f;

				if ( masterPhysics->GetContents() != 0 &&
					 masterVolume > 0.0f &&
					 ( selfVolume <= 0.0f || masterVolume >= selfVolume * 0.25f ) ) {
					needsProxyPush = false;
				}
			}

			if ( needsProxyPush ) {
				trace_t pushResults;
				idVec3 pushedOrigin = current.i.position;
				idMat3 pushedAxis = current.i.orientation;
				memset( &pushResults, 0, sizeof( pushResults ) );
				gameLocal.push.ClipPush( pushResults, self, 0, oldOrigin, oldAxis, pushedOrigin, pushedAxis );
			}
		}
		current.i.linearMomentum = mass * ( ( current.i.position - oldOrigin ) / timeStep );
		current.i.angularMomentum = RigidBodyWorldInertiaTensor( current.i.orientation, inertiaTensor ) *
									( ( current.i.orientation * oldAxis.Transpose() ).ToAngularVelocity() / timeStep );
		current.externalForce.Zero();
		current.externalTorque.Zero();

		return ( current.i.position != oldOrigin || current.i.orientation != oldAxis );
	}

	// if the body is at rest
	if ( current.atRest >= 0 || timeStep <= 0.0f ) {
		DebugDraw();
		return false;
	}

	// if putting the body to rest
	if ( dropToFloor ) {
		DropToFloorAndRest();
		current.externalForce.Zero();
		current.externalTorque.Zero();
		return true;
	}

#ifdef RB_TIMINGS
	if ( rb_showTimings->integer != 0 ) {
		timer_total.Start();
	}
#endif

	// move the rigid body velocity into the frame of a pusher
//	current.i.linearMomentum -= current.pushVelocity.SubVec3( 0 ) * mass;
//	current.i.angularMomentum -= current.pushVelocity.SubVec3( 1 ) * inertiaTensor;

	clipModel->Unlink();
	remainingTime = timeStep;
	collisionIterations = 0;
	numImpactCollisions = 0;

	while ( remainingTime > RB_COLLISION_MIN_TIMESTEP ) {
		next = current;

		// Treat liquid as a damping medium instead of a one-time blocking collision hack.
		if ( IsInWater() ) {
			current.externalForce -= ( RB_WATER_FRICTION_SCALE - 1.0f ) * linearFriction * current.i.linearMomentum;
			current.externalTorque -= ( RB_WATER_FRICTION_SCALE - 1.0f ) * angularFriction * current.i.angularMomentum;

// openQ4 BEGIN
			// Buoyancy. Without this a crate dropped in a pool falls to the bottom at full gravity
			// and only looks slow because of the drag above. Cancelling most of the weight lets
			// debris settle the way it does in the older Quake games.
			current.externalForce -= RB_LIQUID_BUOYANCY * mass * gravityVector;
// openQ4 END
		}

		// calculate next position and orientation
		Integrate( remainingTime, next );

#ifdef RB_TIMINGS
		if ( rb_showTimings->integer != 0 ) {
			timer_collision.Start();
		}
#endif

		// check for collisions from the current to the next state
		collided = CheckForCollisions( remainingTime, next, collision );

#ifdef RB_TIMINGS
		if ( rb_showTimings->integer != 0 ) {
			timer_collision.Stop();
		}
#endif

		if ( collided &&
			 collision.fraction > RB_COLLISION_FRACTION_EPSILON &&
			 collision.fraction < 1.0f ) {
			collisionTime = remainingTime * collision.fraction;
			impactState = current;
			Integrate( collisionTime, impactState );
			next.i.linearMomentum = impactState.i.linearMomentum;
			next.i.angularMomentum = impactState.i.angularMomentum;
		}

		// set the new state
		current = next;

		if ( !collided ) {
			break;
		}

//	trace_t					pushResults;
//	gameLocal.push.ClipPush( pushResults, self, PUSHFL_CRUSH | PUSHFL_CLIP, saved.localOrigin, saved.localAxis, current.localOrigin, current.localAxis );

		collisionIterations++;

		// apply collision impulse
		if ( CollisionImpulse( collision, impulse ) ) {
			current.atRest = gameLocal.time;
		}

		if ( numImpactCollisions < RB_MAX_COLLISION_ITERATIONS ) {
			impactCollisions[numImpactCollisions] = collision;
			impactImpulses[numImpactCollisions] = impulse;
			numImpactCollisions++;
		}

		if ( current.atRest >= 0 ||
			 collisionIterations >= RB_MAX_COLLISION_ITERATIONS ||
			 collision.fraction <= RB_COLLISION_FRACTION_EPSILON ) {
			break;
		}

		remainingTime *= Max( 0.0f, 1.0f - collision.fraction );
	}

	// update the position of the clip model
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	clipModel->Link( self, clipModel->GetId(), current.i.position, current.i.orientation );
// RAVEN END

	DebugDraw();

	if ( !noContact ) {

#ifdef RB_TIMINGS
		if ( rb_showTimings->integer != 0 ) {
			timer_collision.Start();
		}
#endif
		// get contacts
		EvaluateContacts();

#ifdef RB_TIMINGS
		if ( rb_showTimings->integer != 0 ) {
			timer_collision.Stop();
		}
#endif

		// check if the body has come to rest
		if ( TestIfAtRest() ) {
			// put to rest
			Rest();
			cameToRest = true;
		}  else {
			// apply contact friction
			ContactFriction( timeStep );
		}
	}

	if ( current.atRest < 0 ) {
		ActivateContactEntities();
	}

	for ( i = 0; i < numImpactCollisions; i++ ) {
		// if the rigid body didn't come to rest or the other entity is not at rest
		ent = gameLocal.entities[impactCollisions[i].c.entityNum];
		if ( ent && ( !cameToRest || !ent->IsAtRest() ) ) {
			// apply impact to other entity
			ent->ApplyImpulse( self, impactCollisions[i].c.id, impactCollisions[i].c.point, -impactImpulses[i] );
		}
	}

	// move the rigid body velocity back into the world frame
//	current.i.linearMomentum += current.pushVelocity.SubVec3( 0 ) * mass;
//	current.i.angularMomentum += current.pushVelocity.SubVec3( 1 ) * inertiaTensor;
	current.pushVelocity.Zero();

	current.lastTimeStep = timeStep;
	current.externalForce.Zero();
	current.externalTorque.Zero();

	if ( IsOutsideWorld() ) {
		gameLocal.Warning( "rigid body moved outside world bounds for entity '%s' type '%s' at (%s)",
					self->name.c_str(), self->GetType()->classname, current.i.position.ToString(0) );
		Rest();
	}

#ifdef RB_TIMINGS
	if ( rb_showTimings->integer != 0 ) {
		timer_total.Stop();

		if ( rb_showTimings->integer == 1 ) {
			gameLocal.Printf( "%12s: t %1.4f cd %1.4f\n",
							self->name.c_str(),
							timer_total.Milliseconds(), timer_collision.Milliseconds() );
			lastTimerReset = 0;
		}
		else if ( rb_showTimings->integer == 2 ) {
			numRigidBodies++;
			if ( endTimeMSec > lastTimerReset ) {
				gameLocal.Printf( "rb %d: t %1.4f cd %1.4f\n",
								numRigidBodies,
								timer_total.Milliseconds(), timer_collision.Milliseconds() );
			}
		}
		if ( endTimeMSec > lastTimerReset ) {
			lastTimerReset = endTimeMSec;
			numRigidBodies = 0;
			timer_total.Clear();
			timer_collision.Clear();
		}
	}
#endif

	return true;
}

/*
================
idPhysics_RigidBody::Evaluate

  openQ4: dispatches to the physics mode committed at map load and records
  per-frame telemetry while physicsStats is recording.
================
*/
bool idPhysics_RigidBody::Evaluate( int timeStepMSec, int endTimeMSec ) {
	OpenQ4Physics::ScopedEvaluation telemetry( OpenQ4Physics::EVAL_RIGID_BODY );
	const bool wasAtRest = current.atRest >= 0;
	const bool moved = telemetry.Moved( UsesEnhancedPhysics() ? EvaluateEnhanced( timeStepMSec, endTimeMSec ) : EvaluateLegacy( timeStepMSec, endTimeMSec ) );
	if ( OpenQ4Physics::GetTelemetry().IsRecording() ) {
		if ( self && self->entityNumber == ENTITYNUM_CLIENT ) {
			OpenQ4Physics::GetTelemetry().NoteCounter( OpenQ4Physics::COUNTER_CLIENT_RIGID );
		}
		const float speed = ( inverseMass * current.i.linearMomentum ).Length();
		OpenQ4Physics::GetTelemetry().NoteLinearSpeed( speed );
		if ( speed > 2000.0f ) {
			gameLocal.Printf( "PHYSICS_FAST rigid entity=%s class=%s speed=%.1f mass=%.2f time=%d\n",
				self ? self->name.c_str() : "?", self ? self->GetClassname() : "?", speed, mass, gameLocal.time );
		}
		if ( !wasAtRest && current.atRest >= 0 ) {
			OpenQ4Physics::GetTelemetry().NoteCounter( OpenQ4Physics::COUNTER_SLEEPS );
		}
		const double elapsed = telemetry.ElapsedMs();
		if ( elapsed > 10.0 ) {
			gameLocal.Printf( "PHYSICS_SLOW rigid entity=%s class=%s ms=%.2f contacts=%d time=%d\n",
				self ? self->name.c_str() : "?", self ? self->GetClassname() : "?", elapsed, contacts.Num(), gameLocal.time );
		}
	}
	return moved;
}

/*
================
idPhysics_RigidBody::UpdateTime
================
*/
void idPhysics_RigidBody::UpdateTime( int endTimeMSec ) {
}

/*
================
idPhysics_RigidBody::GetTime
================
*/
int idPhysics_RigidBody::GetTime( void ) const {
	return gameLocal.time;
}

/*
================
idPhysics_RigidBody::GetImpactInfo
================
*/
void idPhysics_RigidBody::GetImpactInfo( const int id, const idVec3 &point, impactInfo_t *info ) const {
	idVec3 linearVelocity, angularVelocity;
	idMat3 inverseWorldInertiaTensor;

	linearVelocity = inverseMass * current.i.linearMomentum;
	inverseWorldInertiaTensor = RigidBodyInverseWorldInertiaTensor( current.i.orientation, inverseInertiaTensor );
	angularVelocity = inverseWorldInertiaTensor * current.i.angularMomentum;

	info->invMass = inverseMass;
	info->invInertiaTensor = inverseWorldInertiaTensor;
	info->position = point - ( current.i.position + centerOfMass * current.i.orientation );
	info->velocity = linearVelocity + angularVelocity.Cross( info->position );
}

/*
================
idPhysics_RigidBody::ApplyImpulse
================
*/
void idPhysics_RigidBody::ApplyImpulse( const int id, const idVec3 &point, const idVec3 &impulse ) {
	if ( noImpact ) {
		return;
	}
	current.i.linearMomentum += impulse;
	current.i.angularMomentum += ( point - ( current.i.position + centerOfMass * current.i.orientation ) ).Cross( impulse );
	Activate();
	if ( OpenQ4Physics::GetTelemetry().IsRecording() ) {
		const float magnitude = impulse.Length();
		OpenQ4Physics::GetTelemetry().NoteImpulse( magnitude, magnitude * inverseMass, false );
	}
}

/*
================
idPhysics_RigidBody::AddForce
================
*/
void idPhysics_RigidBody::AddForce( const int id, const idVec3 &point, const idVec3 &force ) {
	if ( noImpact ) {
		return;
	}
	current.externalForce += force;
	current.externalTorque += ( point - ( current.i.position + centerOfMass * current.i.orientation ) ).Cross( force );
	Activate();
}

/*
================
idPhysics_RigidBody::IsAtRest
================
*/
bool idPhysics_RigidBody::IsAtRest( void ) const {
	return current.atRest >= 0;
}

/*
================
idPhysics_RigidBody::GetRestStartTime
================
*/
int idPhysics_RigidBody::GetRestStartTime( void ) const {
	return current.atRest;
}

/*
================
idPhysics_RigidBody::IsPushable
================
*/
bool idPhysics_RigidBody::IsPushable( void ) const {
	return ( !noImpact && !hasMaster );
}

/*
================
idPhysics_RigidBody::IsInWater
================
*/
bool idPhysics_RigidBody::IsInWater( void ) const {
	if ( clipModel == NULL ) {
		return false;
	}
	return ( gameLocal.Contents( self, current.i.position, clipModel, current.i.orientation, MASK_WATER, self ) & MASK_WATER ) != 0;
}

/*
================
idPhysics_RigidBody::SaveState
================
*/
void idPhysics_RigidBody::SaveState( void ) {
	saved = current;
}

/*
================
idPhysics_RigidBody::RestoreState
================
*/
void idPhysics_RigidBody::RestoreState( void ) {
	current = saved;
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	clipModel->Link( self, clipModel->GetId(), current.i.position, current.i.orientation );
// RAVEN END

	EvaluateContacts();
}

/*
================
idPhysics::SetOrigin
================
*/
void idPhysics_RigidBody::SetOrigin( const idVec3 &newOrigin, int id ) {
	idVec3 masterOrigin;
	idMat3 masterAxis;

	current.localOrigin = newOrigin;
	if ( hasMaster ) {
		self->GetMasterPosition( masterOrigin, masterAxis );
		current.i.position = masterOrigin + newOrigin * masterAxis;
	}
	else {
		current.i.position = newOrigin;
	}
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	clipModel->Link( self, clipModel->GetId(), current.i.position, clipModel->GetAxis() );
// RAVEN END

	Activate();
}

/*
================
idPhysics::SetAxis
================
*/
void idPhysics_RigidBody::SetAxis( const idMat3 &newAxis, int id ) {
	idVec3 masterOrigin;
	idMat3 masterAxis;

	current.localAxis = newAxis;
	if ( hasMaster && isOrientated ) {
		self->GetMasterPosition( masterOrigin, masterAxis );
		current.i.orientation = newAxis * masterAxis;
	}
	else {
		current.i.orientation = newAxis;
	}
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	clipModel->Link( self, clipModel->GetId(), clipModel->GetOrigin(), current.i.orientation );
// RAVEN END

	Activate();
}

/*
================
idPhysics::Move
================
*/
void idPhysics_RigidBody::Translate( const idVec3 &translation, int id ) {

	current.localOrigin += translation;
	current.i.position += translation;
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	clipModel->Link( self, clipModel->GetId(), current.i.position, clipModel->GetAxis() );
// RAVEN END

	Activate();
}

/*
================
idPhysics::Rotate
================
*/
void idPhysics_RigidBody::Rotate( const idRotation &rotation, int id ) {
	idVec3 masterOrigin;
	idMat3 masterAxis;

	current.i.orientation *= rotation.ToMat3();
	current.i.position *= rotation;

	if ( hasMaster ) {
		self->GetMasterPosition( masterOrigin, masterAxis );
		current.localAxis *= rotation.ToMat3();
		current.localOrigin = ( current.i.position - masterOrigin ) * masterAxis.Transpose();
	}
	else {
		current.localAxis = current.i.orientation;
		current.localOrigin = current.i.position;
	}
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	clipModel->Link( self, clipModel->GetId(), current.i.position, current.i.orientation );
// RAVEN END

	Activate();
}

/*
================
idPhysics_RigidBody::GetOrigin
================
*/
const idVec3 &idPhysics_RigidBody::GetOrigin( int id ) const {
	return current.i.position;
}

/*
================
idPhysics_RigidBody::GetAxis
================
*/
const idMat3 &idPhysics_RigidBody::GetAxis( int id ) const {
	return current.i.orientation;
}

/*
================
idPhysics_RigidBody::SetLinearVelocity
================
*/
void idPhysics_RigidBody::SetLinearVelocity( const idVec3 &newLinearVelocity, int id ) {
	current.i.linearMomentum = newLinearVelocity * mass;
	Activate();
}

/*
================
idPhysics_RigidBody::SetAngularVelocity
================
*/
void idPhysics_RigidBody::SetAngularVelocity( const idVec3 &newAngularVelocity, int id ) {
	current.i.angularMomentum = RigidBodyWorldInertiaTensor( current.i.orientation, inertiaTensor ) * newAngularVelocity;
	Activate();
}

/*
================
idPhysics_RigidBody::GetLinearVelocity
================
*/
const idVec3 &idPhysics_RigidBody::GetLinearVelocity( int id ) const {
	static idVec3 curLinearVelocity;
	curLinearVelocity = current.i.linearMomentum * inverseMass;
	return curLinearVelocity;
}

/*
================
idPhysics_RigidBody::GetAngularVelocity
================
*/
const idVec3 &idPhysics_RigidBody::GetAngularVelocity( int id ) const {
	static idVec3 curAngularVelocity;
	idMat3 inverseWorldInertiaTensor;

	inverseWorldInertiaTensor = RigidBodyInverseWorldInertiaTensor( current.i.orientation, inverseInertiaTensor );
	curAngularVelocity = inverseWorldInertiaTensor * current.i.angularMomentum;
	return curAngularVelocity;
}

/*
================
idPhysics_RigidBody::ClipTranslation
================
*/
void idPhysics_RigidBody::ClipTranslation( trace_t &results, const idVec3 &translation, const idClipModel *model ) const {
	if ( model ) {
// RAVEN BEGIN
// ddynerman: multiple clip worlds
		gameLocal.TranslationModel( self, results, clipModel->GetOrigin(), clipModel->GetOrigin() + translation,
											clipModel, clipModel->GetAxis(), RigidBodyClipMask( clipMask ),
											model->GetCollisionModel(), model->GetOrigin(), model->GetAxis() );
	}
	else {
		gameLocal.Translation( self, results, clipModel->GetOrigin(), clipModel->GetOrigin() + translation,
											clipModel, clipModel->GetAxis(), RigidBodyClipMask( clipMask ), self );
// RAVEN END
	}
}

/*
================
idPhysics_RigidBody::ClipRotation
================
*/
void idPhysics_RigidBody::ClipRotation( trace_t &results, const idRotation &rotation, const idClipModel *model ) const {
	if ( model ) {
// RAVEN BEGIN
// ddynerman: multiple clip worlds
		gameLocal.RotationModel( self, results, clipModel->GetOrigin(), rotation,
											clipModel, clipModel->GetAxis(), RigidBodyClipMask( clipMask ),
											model->GetCollisionModel(), model->GetOrigin(), model->GetAxis() );
	}
	else {
		gameLocal.Rotation( self, results, clipModel->GetOrigin(), rotation,
											clipModel, clipModel->GetAxis(), RigidBodyClipMask( clipMask ), self );
// RAVEN END
	}
}

/*
================
idPhysics_RigidBody::ClipContents
================
*/
int idPhysics_RigidBody::ClipContents( const idClipModel *model ) const {
	if ( model ) {
// RAVEN BEGIN
// ddynerman: multiple collision worlds
		return gameLocal.ContentsModel( self, clipModel->GetOrigin(), clipModel, clipModel->GetAxis(), -1,
									model->GetCollisionModel(), model->GetOrigin(), model->GetAxis() );
	}
	else {
		return gameLocal.Contents( self, clipModel->GetOrigin(), clipModel, clipModel->GetAxis(), -1, NULL );
// RAVEN END
	}
}

/*
================
idPhysics_RigidBody::DisableClip
================
*/
void idPhysics_RigidBody::DisableClip( void ) {
	clipModel->Disable();
}

/*
================
idPhysics_RigidBody::EnableClip
================
*/
void idPhysics_RigidBody::EnableClip( void ) {
	clipModel->Enable();
}

/*
================
idPhysics_RigidBody::UnlinkClip
================
*/
void idPhysics_RigidBody::UnlinkClip( void ) {
	clipModel->Unlink();
}

/*
================
idPhysics_RigidBody::LinkClip
================
*/
void idPhysics_RigidBody::LinkClip( void ) {
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	clipModel->Link( self, clipModel->GetId(), current.i.position, current.i.orientation );
// RAVEN END
}

/*
================
idPhysics_RigidBody::EvaluateContacts
================
*/
bool idPhysics_RigidBody::EvaluateContacts( void ) {
	int i, j;
	idVec6 dir;
	int num;
	int numBodyContacts;
	contactInfo_t contactInfo[RB_CONTACTS_MAX];

	ClearContacts();

	dir.SubVec3(0) = current.i.linearMomentum + current.lastTimeStep * gravityVector * mass;
	dir.SubVec3(1) = current.i.angularMomentum;
	dir.SubVec3(0).Normalize();
	dir.SubVec3(1).Normalize();
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	num = gameLocal.Contacts( self, contactInfo, RB_CONTACTS_MAX, clipModel->GetOrigin(),
					dir, CONTACT_EPSILON, clipModel, clipModel->GetAxis(), RigidBodyClipMask( clipMask ), self );
// RAVEN END

	for ( i = 0; i < num; i++ ) {
		numBodyContacts = 0;

		for ( j = 0; j < contacts.Num(); j++ ) {
			if ( contacts[j].entityNum == contactInfo[i].entityNum && contacts[j].id == contactInfo[i].id ) {
				if ( ( contacts[j].point - contactInfo[i].point ).LengthSqr() < Square( RB_CONTACT_MERGE_EPSILON ) ) {
					break;
				}
				if ( idMath::Fabs( contacts[j].normal * contactInfo[i].normal ) > 0.9f ) {
					numBodyContacts++;
				}
			}
		}

		if ( j >= contacts.Num() && numBodyContacts < RB_MAX_PLANAR_CONTACTS ) {
			contacts.Append( contactInfo[i] );
		}
	}

	AddContactEntitiesForContacts();

	return ( contacts.Num() != 0 );
}

/*
================
idPhysics_RigidBody::SetPushed
================
*/
void idPhysics_RigidBody::SetPushed( int deltaTime ) {
	idRotation rotation;

	rotation = ( saved.i.orientation * current.i.orientation ).ToRotation();

	// velocity with which the af is pushed
	current.pushVelocity.SubVec3(0) += ( current.i.position - saved.i.position ) / ( deltaTime * idMath::M_MS2SEC );
	current.pushVelocity.SubVec3(1) += rotation.GetVec() * -DEG2RAD( rotation.GetAngle() ) / ( deltaTime * idMath::M_MS2SEC );
}

/*
================
idPhysics_RigidBody::GetPushedLinearVelocity
================
*/
const idVec3 &idPhysics_RigidBody::GetPushedLinearVelocity( const int id ) const {
	return current.pushVelocity.SubVec3(0);
}

/*
================
idPhysics_RigidBody::GetPushedAngularVelocity
================
*/
const idVec3 &idPhysics_RigidBody::GetPushedAngularVelocity( const int id ) const {
	return current.pushVelocity.SubVec3(1);
}

/*
================
idPhysics_RigidBody::SetMaster
================
*/
void idPhysics_RigidBody::SetMaster( idEntity *master, const bool orientated ) {
	idVec3 masterOrigin;
	idMat3 masterAxis;

	if ( master ) {
		if ( !hasMaster ) {
			// transform from world space to master space
			self->GetMasterPosition( masterOrigin, masterAxis );
			current.localOrigin = ( current.i.position - masterOrigin ) * masterAxis.Transpose();
			if ( orientated ) {
				current.localAxis = current.i.orientation * masterAxis.Transpose();
			}
			else {
				current.localAxis = current.i.orientation;
			}
			hasMaster = true;
			isOrientated = orientated;
			ClearContacts();
		}
	}
	else {
		if ( hasMaster ) {
			hasMaster = false;
			Activate();
		}
	}
}

const float	RB_VELOCITY_MAX				= 16000;
const int	RB_VELOCITY_TOTAL_BITS		= 16;
const int	RB_VELOCITY_EXPONENT_BITS	= idMath::BitsForInteger( idMath::BitsForFloat( RB_VELOCITY_MAX ) ) + 1;
const int	RB_VELOCITY_MANTISSA_BITS	= RB_VELOCITY_TOTAL_BITS - 1 - RB_VELOCITY_EXPONENT_BITS;
const float	RB_MOMENTUM_MAX				= 1e20f;
const int	RB_MOMENTUM_TOTAL_BITS		= 16;
const int	RB_MOMENTUM_EXPONENT_BITS	= idMath::BitsForInteger( idMath::BitsForFloat( RB_MOMENTUM_MAX ) ) + 1;
const int	RB_MOMENTUM_MANTISSA_BITS	= RB_MOMENTUM_TOTAL_BITS - 1 - RB_MOMENTUM_EXPONENT_BITS;
const float	RB_FORCE_MAX				= 1e20f;
const int	RB_FORCE_TOTAL_BITS			= 16;
const int	RB_FORCE_EXPONENT_BITS		= idMath::BitsForInteger( idMath::BitsForFloat( RB_FORCE_MAX ) ) + 1;
const int	RB_FORCE_MANTISSA_BITS		= RB_FORCE_TOTAL_BITS - 1 - RB_FORCE_EXPONENT_BITS;

/*
================
idPhysics_RigidBody::WriteToSnapshot
================
*/
void idPhysics_RigidBody::WriteToSnapshot( idBitMsgDelta &msg ) const {
	idCQuat quat, localQuat;

	quat = current.i.orientation.ToCQuat();
	localQuat = current.localAxis.ToCQuat();

	msg.WriteLong( current.atRest );
	msg.WriteFloat( current.i.position[0] );
	msg.WriteFloat( current.i.position[1] );
	msg.WriteFloat( current.i.position[2] );
	msg.WriteFloat( quat.x );
	msg.WriteFloat( quat.y );
	msg.WriteFloat( quat.z );
	msg.WriteFloat( current.i.linearMomentum[0], RB_MOMENTUM_EXPONENT_BITS, RB_MOMENTUM_MANTISSA_BITS );
	msg.WriteFloat( current.i.linearMomentum[1], RB_MOMENTUM_EXPONENT_BITS, RB_MOMENTUM_MANTISSA_BITS );
	msg.WriteFloat( current.i.linearMomentum[2], RB_MOMENTUM_EXPONENT_BITS, RB_MOMENTUM_MANTISSA_BITS );
	msg.WriteFloat( current.i.angularMomentum[0], RB_MOMENTUM_EXPONENT_BITS, RB_MOMENTUM_MANTISSA_BITS );
	msg.WriteFloat( current.i.angularMomentum[1], RB_MOMENTUM_EXPONENT_BITS, RB_MOMENTUM_MANTISSA_BITS );
	msg.WriteFloat( current.i.angularMomentum[2], RB_MOMENTUM_EXPONENT_BITS, RB_MOMENTUM_MANTISSA_BITS );
	msg.WriteDeltaFloat( current.i.position[0], current.localOrigin[0] );
	msg.WriteDeltaFloat( current.i.position[1], current.localOrigin[1] );
	msg.WriteDeltaFloat( current.i.position[2], current.localOrigin[2] );
	msg.WriteDeltaFloat( quat.x, localQuat.x );
	msg.WriteDeltaFloat( quat.y, localQuat.y );
	msg.WriteDeltaFloat( quat.z, localQuat.z );
	msg.WriteDeltaFloat( 0.0f, current.pushVelocity[0], RB_VELOCITY_EXPONENT_BITS, RB_VELOCITY_MANTISSA_BITS );
	msg.WriteDeltaFloat( 0.0f, current.pushVelocity[1], RB_VELOCITY_EXPONENT_BITS, RB_VELOCITY_MANTISSA_BITS );
	msg.WriteDeltaFloat( 0.0f, current.pushVelocity[2], RB_VELOCITY_EXPONENT_BITS, RB_VELOCITY_MANTISSA_BITS );
	msg.WriteDeltaFloat( 0.0f, current.externalForce[0], RB_FORCE_EXPONENT_BITS, RB_FORCE_MANTISSA_BITS );
	msg.WriteDeltaFloat( 0.0f, current.externalForce[1], RB_FORCE_EXPONENT_BITS, RB_FORCE_MANTISSA_BITS );
	msg.WriteDeltaFloat( 0.0f, current.externalForce[2], RB_FORCE_EXPONENT_BITS, RB_FORCE_MANTISSA_BITS );
	msg.WriteDeltaFloat( 0.0f, current.externalTorque[0], RB_FORCE_EXPONENT_BITS, RB_FORCE_MANTISSA_BITS );
	msg.WriteDeltaFloat( 0.0f, current.externalTorque[1], RB_FORCE_EXPONENT_BITS, RB_FORCE_MANTISSA_BITS );
	msg.WriteDeltaFloat( 0.0f, current.externalTorque[2], RB_FORCE_EXPONENT_BITS, RB_FORCE_MANTISSA_BITS );
}

/*
================
idPhysics_RigidBody::ReadFromSnapshot
================
*/
void idPhysics_RigidBody::ReadFromSnapshot( const idBitMsgDelta &msg ) {
	idCQuat quat, localQuat;

	current.atRest = msg.ReadLong();
	current.i.position[0] = msg.ReadFloat();
	current.i.position[1] = msg.ReadFloat();
	current.i.position[2] = msg.ReadFloat();
	quat.x = msg.ReadFloat();
	quat.y = msg.ReadFloat();
	quat.z = msg.ReadFloat();
	current.i.linearMomentum[0] = msg.ReadFloat( RB_MOMENTUM_EXPONENT_BITS, RB_MOMENTUM_MANTISSA_BITS );
	current.i.linearMomentum[1] = msg.ReadFloat( RB_MOMENTUM_EXPONENT_BITS, RB_MOMENTUM_MANTISSA_BITS );
	current.i.linearMomentum[2] = msg.ReadFloat( RB_MOMENTUM_EXPONENT_BITS, RB_MOMENTUM_MANTISSA_BITS );
	current.i.angularMomentum[0] = msg.ReadFloat( RB_MOMENTUM_EXPONENT_BITS, RB_MOMENTUM_MANTISSA_BITS );
	current.i.angularMomentum[1] = msg.ReadFloat( RB_MOMENTUM_EXPONENT_BITS, RB_MOMENTUM_MANTISSA_BITS );
	current.i.angularMomentum[2] = msg.ReadFloat( RB_MOMENTUM_EXPONENT_BITS, RB_MOMENTUM_MANTISSA_BITS );
	current.localOrigin[0] = msg.ReadDeltaFloat( current.i.position[0] );
	current.localOrigin[1] = msg.ReadDeltaFloat( current.i.position[1] );
	current.localOrigin[2] = msg.ReadDeltaFloat( current.i.position[2] );
	localQuat.x = msg.ReadDeltaFloat( quat.x );
	localQuat.y = msg.ReadDeltaFloat( quat.y );
	localQuat.z = msg.ReadDeltaFloat( quat.z );
	current.pushVelocity[0] = msg.ReadDeltaFloat( 0.0f, RB_VELOCITY_EXPONENT_BITS, RB_VELOCITY_MANTISSA_BITS );
	current.pushVelocity[1] = msg.ReadDeltaFloat( 0.0f, RB_VELOCITY_EXPONENT_BITS, RB_VELOCITY_MANTISSA_BITS );
	current.pushVelocity[2] = msg.ReadDeltaFloat( 0.0f, RB_VELOCITY_EXPONENT_BITS, RB_VELOCITY_MANTISSA_BITS );
	current.externalForce[0] = msg.ReadDeltaFloat( 0.0f, RB_FORCE_EXPONENT_BITS, RB_FORCE_MANTISSA_BITS );
	current.externalForce[1] = msg.ReadDeltaFloat( 0.0f, RB_FORCE_EXPONENT_BITS, RB_FORCE_MANTISSA_BITS );
	current.externalForce[2] = msg.ReadDeltaFloat( 0.0f, RB_FORCE_EXPONENT_BITS, RB_FORCE_MANTISSA_BITS );
	current.externalTorque[0] = msg.ReadDeltaFloat( 0.0f, RB_FORCE_EXPONENT_BITS, RB_FORCE_MANTISSA_BITS );
	current.externalTorque[1] = msg.ReadDeltaFloat( 0.0f, RB_FORCE_EXPONENT_BITS, RB_FORCE_MANTISSA_BITS );
	current.externalTorque[2] = msg.ReadDeltaFloat( 0.0f, RB_FORCE_EXPONENT_BITS, RB_FORCE_MANTISSA_BITS );

	current.i.orientation = quat.ToMat3();
	current.localAxis = localQuat.ToMat3();

	if ( clipModel ) {
// RAVEN BEGIN
// ddynerman: multiple clip worlds
		clipModel->Link( self, clipModel->GetId(), current.i.position, current.i.orientation );
// RAVEN END
	}
}

// openQ4: enhanced rigid bodies (PHYS-02)
#include "EnhancedRigidBody.inl"
