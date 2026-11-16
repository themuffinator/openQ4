// openQ4 additions to the SDK game sources; see src/game/LICENSE.
//
// Enhanced rigid bodies (PHYS-02, g_enhancedPhysics 1). Included at the end of
// both game trees' physics/Physics_RigidBody.cpp, so the file-local helpers and
// constants there are in scope. The solver kernels live in EnhancedSolver.h.
//
// One step, for a body that is awake, free and passive debris:
//   1. semi-implicit integration of gravity, external forces, liquid drag and
//      buoyancy and the authored linear/angular friction
//   2. one translation sweep along the motion (idClip::Contacts with a
//      speculative depth), which is both the collision test and the contact
//      query, plus persistent contacts kept from earlier steps
//   3. warm-started sequential impulses with Coulomb, rolling and spinning
//      friction, restitution above a threshold, speculative gaps and
//      pseudo-velocity penetration recovery
//   4. the position step limited to the time of impact, and a rotation sweep
//      only when the body turns far enough in one step to swing into a surface
//   5. quiet-frame sleeping with a correct wake on any impulse, force, push or
//      script move (Activate), and an energy guard
// Everything enhanced-only is transient: nothing new is saved or networked.

#include "EnhancedContacts.h"

/*
================
idPhysics_RigidBody::UsesEnhancedPhysics

  Enhanced mode applies to passive bodies only: moveables, moveable items,
  brittle-fracture shards and client-side debris. Projectiles (grenade bounces
  are gameplay), vehicles, AI that fly on rigid-body physics and bound bodies
  keep the legacy simulation in both modes.
================
*/
bool idPhysics_RigidBody::UsesEnhancedPhysics( void ) const {
	if ( !gameLocal.enhancedPhysics || hasMaster || noContact ) {
		return false;
	}
	if ( enhancedEligibility < 0 ) {
		// projectiles derive from idEntity directly, so they never match
		bool eligible = false;
		if ( self ) {
			eligible = self->entityNumber == ENTITYNUM_CLIENT ||
						self->IsType( idMoveable::GetClassType() ) ||
						self->IsType( idItem::GetClassType() ) ||
						self->IsType( idBrittleFracture::GetClassType() );
		}
		enhancedEligibility = eligible ? 1 : 0;
	}
	return enhancedEligibility == 1;
}

/*
================
StoreEnhancedContactFrame

  Records a cached contact in the frame of the body it touches (identity for the
  world), so the persistent manifold can follow that body.
================
*/
static void StoreEnhancedContactFrame( OpenQ4Physics::cachedContact_t &cached ) {
	cached.localPoint = cached.point;
	cached.localNormal = cached.normal;
	idEntity *other = ( cached.entityNum >= 0 && cached.entityNum < MAX_GENTITIES ) ? gameLocal.entities[cached.entityNum] : NULL;
	if ( !other || other == gameLocal.world || !other->GetPhysics() ) {
		return;
	}
	const idPhysics *physics = other->GetPhysics();
	const int id = cached.id < Max( 1, physics->GetNumClipModels() ) ? cached.id : 0;
	const idVec3 &origin = physics->GetOrigin( id );
	const idMat3 &axis = physics->GetAxis( id );
	const idVec3 point( cached.point.x, cached.point.y, cached.point.z );
	const idVec3 normal( cached.normal.x, cached.normal.y, cached.normal.z );
	const idVec3 localPoint = ( point - origin ) * axis.Transpose();
	const idVec3 localNormal = normal * axis.Transpose();
	cached.localPoint = OpenQ4Physics::Vec3( localPoint.ToFloatPtr() );
	cached.localNormal = OpenQ4Physics::Vec3( localNormal.ToFloatPtr() );
}

/*
================
idPhysics_RigidBody::EvaluateEnhanced
================
*/
bool idPhysics_RigidBody::EvaluateEnhanced( int timeStepMSec, int endTimeMSec ) {
	// rest, drop-to-floor placement and zero steps behave exactly as legacy
	if ( current.atRest >= 0 || timeStepMSec <= 0 || dropToFloor ) {
		return EvaluateLegacy( timeStepMSec, endTimeMSec );
	}

	const float timeStep = MS2SEC( timeStepMSec );
	current.lastTimeStep = timeStep;
	OpenQ4Physics::Telemetry &telemetry = OpenQ4Physics::GetTelemetry();

	clipModel->Unlink();

	// ---- 1. integrate forces into velocities (semi-implicit) ----
	idMat3 invInertiaWorld = RigidBodyInverseWorldInertiaTensor( current.i.orientation, inverseInertiaTensor );
	idVec3 linearVelocity = inverseMass * current.i.linearMomentum;
	idVec3 angularVelocity = invInertiaWorld * current.i.angularMomentum;
	const idMat3 inertiaWorld = RigidBodyWorldInertiaTensor( current.i.orientation, inertiaTensor );
	const float energyBefore = 0.5f * mass * linearVelocity.LengthSqr() + 0.5f * ( angularVelocity * ( inertiaWorld * angularVelocity ) );

	idVec3 force = current.externalForce;
	idVec3 torque = current.externalTorque;
	if ( IsInWater() ) {
		force -= ( RB_WATER_FRICTION_SCALE - 1.0f ) * linearFriction * current.i.linearMomentum;
		torque -= ( RB_WATER_FRICTION_SCALE - 1.0f ) * angularFriction * current.i.angularMomentum;
		force -= RB_LIQUID_BUOYANCY * mass * gravityVector;
	}
	linearVelocity += timeStep * ( gravityVector + inverseMass * force );
	angularVelocity += timeStep * ( invInertiaWorld * torque );
	// the authored friction is a momentum decay rate, applied implicitly
	linearVelocity *= 1.0f / ( 1.0f + linearFriction * timeStep );
	angularVelocity *= 1.0f / ( 1.0f + angularFriction * timeStep );
	const idVec3 forceWorkVelocity = linearVelocity;
	const idVec3 preSolveAngularVelocity = angularVelocity;

	// ---- 2. contacts: one sweep along the motion, plus the persistent manifold ----
	enhancedShape_t shape;
	shape.Set( clipModel, current.i.position, current.i.orientation );
	const idTraceModel *trm = shape.trm;
	float *vertices = shape.vertices;
	const int numVertices = shape.numVertices;

	const float speed = linearVelocity.Length();
	idVec6 dir;
	dir.SubVec3( 0 ) = linearVelocity;
	if ( dir.SubVec3( 0 ).Normalize() < 1e-3f ) {
		dir.SubVec3( 0 ) = gravityNormal;
	}
	dir.SubVec3( 1 ) = angularVelocity;
	dir.SubVec3( 1 ).Normalize();
	const float depth = Max( CONTACT_EPSILON, speed * timeStep + rb_enhancedMargin.GetFloat() );

	contactInfo_t swept[RB_CONTACTS_MAX];
	const int numSwept = gameLocal.Contacts( self, swept, RB_CONTACTS_MAX, current.i.position, dir, depth, clipModel,
								current.i.orientation, RigidBodyClipMask( clipMask ), self );

	OpenQ4Physics::rigidContact_t solve[RB_CONTACTS_MAX];
	contactInfo_t infos[RB_CONTACTS_MAX];
	int ages[RB_CONTACTS_MAX];
	int types[RB_CONTACTS_MAX];			// the collision model's contact type and the body
	int features[RB_CONTACTS_MAX];		// feature that made each contact (EnhancedContacts.h)
	float gaps[RB_CONTACTS_MAX];
	int count = 0;

	for ( int i = 0; i < numSwept && count < RB_CONTACTS_MAX; i++ ) {
		const contactInfo_t &info = swept[i];
		// merge nearby contacts on the same surface, as legacy does
		bool merged = false;
		for ( int j = 0; j < count; j++ ) {
			if ( infos[j].entityNum == info.entityNum && infos[j].id == info.id &&
				 ( infos[j].point - info.point ).LengthSqr() < Square( RB_CONTACT_MERGE_EPSILON ) &&
				 infos[j].normal * info.normal > 0.9f ) {
				merged = true;
				break;
			}
		}
		if ( merged ) {
			continue;
		}
		infos[count] = info;
		ages[count] = 0;
		types[count] = info.type;
		features[count] = -1;
		gaps[count] = 0.0f;
		count++;
	}

	// persistent manifold: surfaces touched in recent steps that this sweep did not
	// report. A sweep that starts slightly inside another body reports nothing, so
	// without this a body resting on a jostled neighbour could drop through it. The
	// plane is kept in the other body's frame and follows it as it moves.
	for ( int c = 0; c < enhancedNumCached && count < RB_CONTACTS_MAX; c++ ) {
		const OpenQ4Physics::cachedContact_t &cached = enhancedCache[c];
		if ( cached.age >= rb_enhancedPersistFrames.GetInteger() ) {
			continue;
		}
		idEntity *other = gameLocal.entities[cached.entityNum];
		if ( !other ) {
			continue;
		}
		idVec3 cachedPoint( cached.point.x, cached.point.y, cached.point.z );
		idVec3 cachedNormal( cached.normal.x, cached.normal.y, cached.normal.z );
		if ( other != gameLocal.world ) {
			const idPhysics *otherPhysics = other->GetPhysics();
			if ( !otherPhysics || cached.id >= Max( 1, otherPhysics->GetNumClipModels() ) ) {
				continue;
			}
			const idVec3 &otherOrigin = otherPhysics->GetOrigin( cached.id );
			const idMat3 &otherAxis = otherPhysics->GetAxis( cached.id );
			cachedPoint = otherOrigin + idVec3( cached.localPoint.x, cached.localPoint.y, cached.localPoint.z ) * otherAxis;
			cachedNormal = idVec3( cached.localNormal.x, cached.localNormal.y, cached.localNormal.z ) * otherAxis;
		}
		bool reported = false;
		for ( int j = 0; j < count && !reported; j++ ) {
			reported = infos[j].entityNum == cached.entityNum && infos[j].id == cached.id &&
						infos[j].normal * cachedNormal > 0.9f && ( infos[j].point - cachedPoint ).LengthSqr() < Square( RB_CONTACT_MERGE_EPSILON );
		}
		if ( reported ) {
			continue;
		}
		idVec3 point;
		float separation;
		if ( !EnhancedRenewContact( shape, cached.type, cached.feature, cachedPoint, cachedNormal, rb_enhancedMargin.GetFloat(), 8.0f,
				point, separation ) ) {
			continue;
		}
		contactInfo_t &info = infos[count];
		memset( &info, 0, sizeof( info ) );
		info.type = CONTACT_TRMVERTEX;
		info.point = point;
		info.normal = cachedNormal;
		info.dist = info.normal * info.point;
		info.entityNum = cached.entityNum;
		info.id = cached.id;
		info.material = NULL;
		ages[count] = cached.age + 1;
		types[count] = cached.type;
		features[count] = cached.feature;
		gaps[count] = separation;
		count++;
	}

	// build solver contacts
	const idVec3 centerOfMassWorld = current.i.position + centerOfMass * current.i.orientation;
	bool owned[RB_CONTACTS_MAX];
	for ( int i = 0; i < count; i++ ) {
		owned[i] = true;
		OpenQ4Physics::rigidContact_t &c = solve[i];
		memset( &c, 0, sizeof( c ) );
		c.point = OpenQ4Physics::Vec3( infos[i].point.ToFloatPtr() );
		c.normal = OpenQ4Physics::Vec3( infos[i].normal.ToFloatPtr() );
		if ( !numVertices ) {
			c.separation = 0.0f;
		} else if ( ages[i] > 0 ) {
			c.separation = gaps[i];		// renewed: measured on the feature that made it
		} else {
			// the row goes where the body is now, not where the sweep met the surface
			idVec3 pointNow;
			c.separation = gaps[i] = EnhancedContactGap( self, infos[i], shape, dir.SubVec3( 0 ), rb_enhancedFeatureGaps.GetBool(), pointNow, features[i] );
			c.point = OpenQ4Physics::Vec3( pointNow.ToFloatPtr() );
			infos[i].point = pointNow;
			infos[i].dist = infos[i].normal * infos[i].point;
		}
		idEntity *other = gameLocal.entities[infos[i].entityNum];
		if ( other && other != gameLocal.world ) {
			impactInfo_t impact;
			memset( &impact, 0, sizeof( impact ) );
			other->GetImpactInfo( self, infos[i].id, infos[i].point, &impact );
			c.otherInvMass = impact.invMass;
			c.otherInvInertia = OpenQ4Physics::Mat3( impact.invInertiaTensor.ToFloatPtr() );
			c.otherR = OpenQ4Physics::Vec3( impact.position.ToFloatPtr() );
			c.otherVelocity = OpenQ4Physics::Vec3( impact.velocity.ToFloatPtr() );
			c.otherSleeping = impact.invMass > 0.0f && other->IsAtRest();
			// Contact ownership between two awake enhanced bodies: each contact is
			// solved once per frame, by the body on top for a stacking contact and
			// by the lower entity number for a side contact. Both bodies sweeping
			// into each other and both solving (and both handing back reactions)
			// pumped energy up and down stacks.
			if ( impact.invMass > 0.0f && !c.otherSleeping ) {
				const idPhysics *otherPhysics = other->GetPhysics();
				if ( otherPhysics && otherPhysics->IsType( idPhysics_RigidBody::GetClassType() ) &&
					 static_cast<const idPhysics_RigidBody *>( otherPhysics )->UsesEnhancedPhysics() ) {
					const float up = -( infos[i].normal * gravityNormal );	// > 0: this body rests on the other
					if ( up < -0.3f || ( up <= 0.3f && self->entityNumber > other->entityNumber ) ) {
						owned[i] = false;
					}
				}
			}
		}
	}

	// the time of impact uses every contact; the solve only the owned, leading ones per surface
	OpenQ4Physics::rigidContact_t all[RB_CONTACTS_MAX];
	const int allCount = count;
	memcpy( all, solve, sizeof( solve[0] ) * count );
	{
		bool keep[RB_CONTACTS_MAX];
		OpenQ4Physics::MarkLeadingContacts( solve, count, 1.0f, [&infos]( int a, int b ) {
			return infos[a].entityNum == infos[b].entityNum && infos[a].id == infos[b].id;
		}, keep );
		int kept = 0;
		for ( int i = 0; i < count; i++ ) {
			if ( keep[i] && owned[i] ) {
				solve[kept] = solve[i];
				infos[kept] = infos[i];
				ages[kept] = ages[i];
				types[kept] = types[i];
				features[kept] = features[i];
				kept++;
			}
		}
		count = kept;
	}

	// warm start from last step's impulses
	for ( int i = 0; i < count; i++ ) {
		OpenQ4Physics::contactKey_t key;
		key.point = solve[i].point;
		key.normal = solve[i].normal;
		key.entityNum = infos[i].entityNum;
		key.id = infos[i].id;
		key.body = 0;
		const int match = OpenQ4Physics::MatchCachedContact( key, enhancedCache, enhancedNumCached, RB_CONTACT_MERGE_EPSILON );
		if ( match >= 0 ) {
			memcpy( solve[i].impulse, enhancedCache[match].impulse, sizeof( solve[i].impulse ) );
		}
		solve[i].newContact = match < 0 && ages[i] == 0;
	}

	// ---- 3. solve ----
	OpenQ4Physics::rigidSolverSettings_t settings;
	settings.timeStep = timeStep;
	settings.iterations = idMath::ClampInt( 1, 64, rb_enhancedIterations.GetInteger() );
	settings.friction = idMath::ClampFloat( 0.05f, 1.2f, contactFriction * rb_enhancedFrictionScale.GetFloat() );
	settings.rollingFriction = rb_enhancedRollingFriction.GetFloat();
	settings.spinningFriction = rb_enhancedSpinningFriction.GetFloat();
	settings.restitution = idMath::ClampFloat( 0.0f, 1.0f, bouncyness );
	// a body never bounces off what gravity alone gave it in a couple of steps
	settings.bounceThreshold = Max( 2.0f * STOP_SPEED, 2.0f * gravityVector.Length() * timeStep );
	settings.slop = 0.1f;
	settings.recoveryRate = 0.4f;
	settings.maxRecoverySpeed = 40.0f;
	settings.deepPenetration = 4.0f;
	settings.warmStart = 0.85f;
	settings.impactFriction = idMath::ClampFloat( 0.0f, 1.0f, rb_enhancedImpactFrictionScale.GetFloat() );
	// A body that touches another moving body does not rebound off its own
	// support either: the weight of a stack reaches the body under it as
	// reactions, which arrive as impacts on the floor, and a stack that bounced
	// on them never came to rest.
	bool touchesMoving = false;
	for ( int i = 0; i < allCount && !touchesMoving; i++ ) {
		touchesMoving = all[i].otherInvMass > 0.0f;
	}
	settings.bounceResting = rb_enhancedBounceResting.GetBool() && !touchesMoving;

	OpenQ4Physics::rigidBody_t body;
	body.invMass = inverseMass;
	body.invInertia = OpenQ4Physics::Mat3( invInertiaWorld.ToFloatPtr() );
	body.centerOfMass = OpenQ4Physics::Vec3( centerOfMassWorld.ToFloatPtr() );
	body.linearVelocity = OpenQ4Physics::Vec3( linearVelocity.ToFloatPtr() );
	body.angularVelocity = OpenQ4Physics::Vec3( angularVelocity.ToFloatPtr() );
	OpenQ4Physics::SolveRigidContacts( body, solve, count, settings );
	linearVelocity.Set( body.linearVelocity.x, body.linearVelocity.y, body.linearVelocity.z );
	angularVelocity.Set( body.angularVelocity.x, body.angularVelocity.y, body.angularVelocity.z );

	// energy guard: contact resolution may only remove energy; gravity and
	// external forces may add the work they did over the step
	{
		const float work = Max( 0.0f, timeStep * ( ( mass * gravityVector + force ) * forceWorkVelocity ) );
		const float energyAfter = 0.5f * mass * linearVelocity.LengthSqr() + 0.5f * ( angularVelocity * ( inertiaWorld * angularVelocity ) );
		const float scale = OpenQ4Physics::EnergyScale( energyAfter, energyBefore + work + 1e-3f * ( energyBefore + 1.0f ) );
		if ( scale < 1.0f ) {
			linearVelocity *= scale;
			angularVelocity *= scale;
			body.linearVelocity *= scale;
			body.angularVelocity *= scale;
			telemetry.NoteCounter( OpenQ4Physics::COUNTER_ENERGY_CLAMPS );
		}
	}

	// ---- 4. move: translate up to the time of impact, sweep large rotations ----
	const float fraction = OpenQ4Physics::MotionFraction( body, all, allCount, timeStep, settings.slop );
	const idVec3 pseudoLinear( body.pseudoLinear.x, body.pseudoLinear.y, body.pseudoLinear.z );
	const idVec3 pseudoAngular( body.pseudoAngular.x, body.pseudoAngular.y, body.pseudoAngular.z );
	const idVec3 translation = ( timeStep * fraction ) * ( linearVelocity + pseudoLinear );
	idVec3 newCenter = centerOfMassWorld + translation;

	idVec3 spin = angularVelocity + pseudoAngular;
	const float spinSpeed = spin.Normalize();
	idMat3 newOrientation = current.i.orientation;
	trace_t rotationCollision;
	bool rotationCollided = false;
	if ( spinSpeed > 1e-6f ) {
		// the articulated-figure integrator's convention: idMat3 rows are the
		// body axes, so the axis turns by the negative angle on the right
		idRotation turn( vec3_origin, spin, -RAD2DEG( spinSpeed * timeStep * fraction ) );
		turn.Normalize180();
		newOrientation = current.i.orientation * turn.ToMat3();
		newOrientation.OrthoNormalizeSelf();
		const float radius = clipModel->GetBounds().GetRadius();
		if ( spinSpeed * timeStep * fraction * radius > rb_enhancedRotationSweep.GetFloat() ) {
			// a turn this large can swing a corner into a surface the sweep did
			// not see: test it the way legacy does, about the centre of mass
			idMat3 axis;
			TransposeMultiply( current.i.orientation, newOrientation, axis );
			idRotation rotation = axis.ToRotation();
			rotation.SetOrigin( newCenter );
			const idVec3 start = newCenter - centerOfMass * current.i.orientation;
			telemetry.NoteCounter( OpenQ4Physics::COUNTER_SWEPT );
			if ( gameLocal.Rotation( self, rotationCollision, start, rotation, clipModel, current.i.orientation,
									RigidBodyClipMask( clipMask ), self ) ) {
				// stop the turn at the surface; next step's solve meets it through
				// the cached contact
				rotationCollided = true;
				newOrientation = rotationCollision.endAxis;
				newCenter = rotationCollision.endpos + centerOfMass * newOrientation;
			}
		}
	}

	const idVec3 oldPosition = current.i.position;
	current.i.orientation = newOrientation;
	current.i.position = newCenter - centerOfMass * newOrientation;
	current.i.linearMomentum = mass * linearVelocity;
	current.i.angularMomentum = RigidBodyWorldInertiaTensor( newOrientation, inertiaTensor ) * angularVelocity;

	clipModel->Link( self, clipModel->GetId(), current.i.position, current.i.orientation );

	DebugDraw();

	// ---- reactions, impact callbacks, contact bookkeeping ----
	bool supported = false;
	float strongestApproach = 0.0f;
	int strongest = -1;
	for ( int i = 0; i < count; i++ ) {
		const OpenQ4Physics::rigidContact_t &c = solve[i];
		if ( c.impulse[0] > 0.0f && -( infos[i].normal * gravityNormal ) > 0.7f ) {
			supported = true;
		}
		if ( c.approachSpeed < strongestApproach ) {
			strongestApproach = c.approachSpeed;
			strongest = i;
		}
		idEntity *other = gameLocal.entities[infos[i].entityNum];
		if ( !other || other == gameLocal.world || !c.sharesMass ) {
			continue;
		}
		const OpenQ4Physics::Vec3 J = OpenQ4Physics::ContactImpulse( c );
		const idVec3 impulse( -J.x, -J.y, -J.z );
		other->ApplyImpulse( self, infos[i].id, infos[i].point, impulse );
	}

	bool stoppedByCallback = false;
	if ( strongest >= 0 && strongestApproach < -STOP_SPEED ) {
		// the impact callback legacy issues for each collision: bounce sounds,
		// impact damage, break-on-impact and explode-on-impact
		trace_t collision;
		memset( &collision, 0, sizeof( collision ) );
		collision.fraction = 0.0f;
		collision.endpos = current.i.position;
		collision.endAxis = current.i.orientation;
		collision.c = infos[strongest];
		// velocity at the contact point before the solve, relative to the other body
		const idVec3 r = infos[strongest].point - centerOfMassWorld;
		const idVec3 otherVelocity( solve[strongest].otherVelocity.x, solve[strongest].otherVelocity.y, solve[strongest].otherVelocity.z );
		const idVec3 impactVelocity = forceWorkVelocity + preSolveAngularVelocity.Cross( r ) - otherVelocity;
		stoppedByCallback = self->Collide( collision, impactVelocity );
	}
	if ( rotationCollided && !stoppedByCallback ) {
		idEntity *other = gameLocal.entities[rotationCollision.c.entityNum];
		if ( other && other != gameLocal.world && !other->IsAtRest() ) {
			other->ApplyImpulse( self, rotationCollision.c.id, rotationCollision.c.point, vec3_origin );
		}
	}

	// cache this step's contacts for warm starting and the persistent manifold
	enhancedNumCached = 0;
	for ( int i = 0; i < count && enhancedNumCached < ENHANCED_CACHE_SIZE; i++ ) {
		OpenQ4Physics::cachedContact_t &cached = enhancedCache[enhancedNumCached++];
		cached.point = solve[i].point;
		cached.normal = solve[i].normal;
		cached.entityNum = infos[i].entityNum;
		cached.id = infos[i].id;
		cached.body = 0;
		cached.age = ages[i];
		cached.type = features[i] >= 0 ? types[i] : 0;
		cached.feature = features[i];
		memcpy( cached.impulse, solve[i].impulse, sizeof( cached.impulse ) );
		StoreEnhancedContactFrame( cached );
	}
	if ( rotationCollided && enhancedNumCached < ENHANCED_CACHE_SIZE ) {
		OpenQ4Physics::cachedContact_t &cached = enhancedCache[enhancedNumCached++];
		memset( &cached, 0, sizeof( cached ) );
		cached.point = OpenQ4Physics::Vec3( rotationCollision.c.point.ToFloatPtr() );
		cached.normal = OpenQ4Physics::Vec3( rotationCollision.c.normal.ToFloatPtr() );
		cached.entityNum = rotationCollision.c.entityNum;
		cached.id = rotationCollision.c.id;
		cached.feature = -1;
		StoreEnhancedContactFrame( cached );
	}

	// the shared contact list (ground checks, contact entities) holds what still touches
	ClearContacts();
	if ( numVertices ) {
		for ( int i = 0; i < numVertices; i++ ) {
			const idVec3 world = current.i.position + trm->verts[i] * current.i.orientation;
			vertices[i * 3 + 0] = world.x;
			vertices[i * 3 + 1] = world.y;
			vertices[i * 3 + 2] = world.z;
		}
	}
	for ( int i = 0; i < count; i++ ) {
		const float gap = numVertices ? OpenQ4Physics::PlaneSeparation( vertices, numVertices, solve[i].normal, solve[i].point ) : 0.0f;
		if ( gap <= CONTACT_EPSILON * 4.0f ) {
			contacts.Append( infos[i] );
		}
	}
	AddContactEntitiesForContacts();

	// ---- 5. sleep ----
	OpenQ4Physics::sleepSettings_t sleep;
	sleep.linearSpeed = rb_enhancedSleepSpeed.GetFloat();
	sleep.angularSpeed = rb_enhancedSleepAngularSpeed.GetFloat();
	sleep.quietFrames = Max( 1, rb_enhancedSleepFrames.GetInteger() );
	bool cameToRest = false;
	if ( linearVelocity.LengthSqr() > Square( sleep.linearSpeed ) || angularVelocity.LengthSqr() > Square( sleep.angularSpeed ) ) {
		enhancedUndisturbed = false;
	}
	if ( rb_enhancedDebug.GetString()[0] && self && !idStr::Icmp( self->name.c_str(), rb_enhancedDebug.GetString() ) ) {
		gameLocal.Printf( "RB_ENHANCED %s t=%d swept=%d contacts=%d supported=%d v=%.3f w=%.3f quiet=%d fraction=%.3f pos=%s\n",
			self->name.c_str(), gameLocal.time, numSwept, count, supported ? 1 : 0, linearVelocity.Length(), angularVelocity.Length(),
			enhancedQuietFrames, fraction, current.i.position.ToString( 2 ) );
		for ( int i = 0; i < count; i++ ) {
			gameLocal.Printf( "  c%d ent=%d n=%s sep=%.3f appr=%.2f imp=%.3f age=%d type=%d feature=%d\n", i, infos[i].entityNum, infos[i].normal.ToString( 2 ),
				solve[i].separation, solve[i].approachSpeed, solve[i].impulse[0], ages[i], types[i], features[i] );
		}
	}
	if ( stoppedByCallback ) {
		current.atRest = gameLocal.time;
		cameToRest = true;
	} else if ( OpenQ4Physics::UpdateSleep( enhancedQuietFrames, linearVelocity.Length(), angularVelocity.Length(), supported, sleep ) ) {
		Rest();
		cameToRest = true;
	} else if ( enhancedUndisturbed && rb_enhancedRestTest.GetBool() && supported && TestIfAtRest() ) {
		// Legacy's own test: three contacts around the centre of mass and almost
		// no motion. A body placed in a map passes it on its first step and never
		// simulates, in either mode. Waiting for the quiet window instead let a
		// pickup balanced on its perch lose the support contacts its sweep no
		// longer reported, tip over and fall out of reach. Only a body that has
		// not moved since it was created rests this way: one that has been
		// knocked about waits for the quiet window, or a settling pile would put
		// its members to rest and wake them again every few steps, as legacy does.
		Rest();
		cameToRest = true;
	}
	// island sleeping: only a body that is still moving wakes what it touches; a
	// quiet one settling next to a neighbour lets both go to sleep together
	if ( !cameToRest && current.atRest < 0 && enhancedQuietFrames == 0 ) {
		ActivateContactEntities();
	}

	current.pushVelocity.Zero();
	current.lastTimeStep = timeStep;
	current.externalForce.Zero();
	current.externalTorque.Zero();

	if ( IsOutsideWorld() ) {
		gameLocal.Warning( "rigid body moved outside world bounds for entity '%s' type '%s' at (%s)",
					self->name.c_str(), self->GetType()->classname, current.i.position.ToString(0) );
		Rest();
	}

	return current.i.position != oldPosition || !cameToRest;
}
