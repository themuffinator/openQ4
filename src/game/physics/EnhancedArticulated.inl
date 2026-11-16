// openQ4 additions to the SDK game sources; see src/game/LICENSE.
//
// Enhanced articulated figures (PHYS-03, g_enhancedPhysics 1): ragdolls and
// every other idPhysics_AF. Included at the end of both game trees'
// physics/Physics_AF.cpp, so its file-local constants are in scope.
//
// The step keeps the legacy structure and the authored figure exactly: bodies,
// masses, joints, limits, joint and contact friction are read as before, and
// primary (tree) joints are still solved exactly by idAFTree in linear time.
// What changes:
//   - contacts are speculative: each body sweeps its own motion plus a margin,
//     every contact knows its gap, and a persistent manifold keeps static
//     surfaces the sweep stopped reporting
//   - the dense LCP over auxiliary rows (limits, contacts, friction, loop
//     joints) is replaced by a warm-started projected Gauss-Seidel solve on the
//     same matrix; the legacy LCP remains a fallback when the iterative
//     result is not accurate enough
//   - overlap with the world or between bodies, and violated cone and pyramid
//     limits, are removed by a pseudo velocity that moves bodies without
//     accelerating them, with a bounded speed
//   - joint error correction as velocity is bounded far below legacy's 256
//     units/s while a joint holds, and rises to legacy's bound once the joint
//     is visibly stretched; the rest of a stretched joint's error is closed by
//     position only, through the same tree solve, so it adds no kinetic energy
//   - only bodies that move further than their speculative reach are swept,
//     and the surface that stops a swept body joins the manifold
//   - an energy guard keeps the solve from adding kinetic energy beyond the
//     work of gravity and external forces
//   - quiet figures sleep, and a quiet figure does not keep waking neighbours
// External impulses keep their linear momentum but a hit on a light limb is
// spread over the figure by mass (the impulse guard, PHYS-04).

#include "EnhancedContacts.h"

/*
================
idPhysics_AF::EnhancedKineticEnergy
================
*/
float idPhysics_AF::EnhancedKineticEnergy( const bool next ) const {
	float energy = 0.0f;
	for ( int i = 0; i < bodies.Num(); i++ ) {
		const idAFBody *body = bodies[i];
		const idVec6 &v = next ? body->next->spatialVelocity : body->current->spatialVelocity;
		const idMat3 axis = body->current->worldAxis.Transpose();
		const idMat3 inertia = axis * body->inertiaTensor * axis.Transpose();
		energy += 0.5f * body->mass * v.SubVec3( 0 ).LengthSqr() + 0.5f * ( v.SubVec3( 1 ) * ( inertia * v.SubVec3( 1 ) ) );
	}
	return energy;
}

/*
================
idPhysics_AF::EnhancedBodyEmbedded

  True when the body intersects the world or scenery that never moves. A
  figure is often authored or animated into geometry: a hanging light with
  chain links inside a beam, a monster whose arm is in a wall when it dies. A
  contact query reports nothing for a surface the body is already behind, so
  such a body would pass on through. Legacy sweeps every body every step, and a
  sweep that starts in a surface cannot move further into it: that is what
  holds Putra's lights where the map hung them.
================
*/
bool idPhysics_AF::EnhancedBodyEmbedded( idAFBody *body ) const {
	if ( body->clipMask == 0 || !body->clipModel ) {
		return false;
	}
	idClip *clip = gameLocal.GetEntityClipWorld( self );
	if ( !clip ) {
		return false;
	}
	const idVec3 &origin = body->current->worldOrigin;
	const idMat3 &axis = body->current->worldAxis;

	// a body attached to the world does not collide with it (SetupCollisionForBody)
	bool world = clip->GetWorldCollisionModel() != NULL;
	for ( int i = 0; i < body->constraints.Num() && world; i++ ) {
		world = !( body->constraints[i]->fl.noCollision && body->constraints[i]->body2 == NULL );
	}
	if ( world && clip->ContentsModel( origin, body->clipModel, axis, body->clipMask, clip->GetWorldCollisionModel(), vec3_origin, mat3_identity ) != 0 ) {
		return true;
	}

	idClipModel *touching[64];
	idBounds bounds;
	bounds.FromTransformedBounds( body->clipModel->GetBounds(), origin, axis );
	const int num = clip->ClipModelsTouchingBounds( bounds, body->clipMask, touching, 64 );
	for ( int i = 0; i < num; i++ ) {
		const idClipModel *other = touching[i];
		const idEntity *owner = other ? other->GetEntity() : NULL;
		if ( !owner || owner == self || other->IsRenderModel() || !other->GetCollisionModel() ) {
			continue;
		}
		const idPhysics *physics = owner->GetPhysics();
		if ( !physics || !( physics->IsType( idPhysics_Static::GetClassType() ) || physics->IsType( idPhysics_StaticMulti::GetClassType() ) ) ) {
			continue;
		}
		if ( clip->ContentsModel( origin, body->clipModel, axis, body->clipMask, other->GetCollisionModel(), other->GetOrigin(), other->GetAxis() ) != 0 ) {
			return true;
		}
	}
	return false;
}

/*
================
idPhysics_AF::EvaluateContactsEnhanced

  Legacy EvaluateContacts with a speculative reach along each body's motion, a
  gap for every contact and the persistent manifold. Also warm-starts the
  contact constraints' multipliers from last step.
================
*/
bool idPhysics_AF::EvaluateContactsEnhanced( float timeStep ) {
	idVecX dir( 6, VECX_ALLOCA( 6 ) );
	contactInfo_t contactInfo[10];

	EvaluateBodies( current.lastTimeStep );
	ClearContacts();
	contactBodies.SetNum( 0, false );
	enhancedSeparation.SetNum( 0, false );
	enhancedContactAge.SetNum( 0, false );
	enhancedContactType.SetNum( 0, false );
	enhancedContactFeature.SetNum( 0, false );
	enhancedReach.SetNum( bodies.Num(), false );

	if ( !enableCollision ) {
		return false;
	}

	const float margin = af_enhancedMargin.GetFloat();
	for ( int i = 0; i < bodies.Num(); i++ ) {
		idAFBody *body = bodies[i];
		enhancedReach[i] = 0.0f;

		if ( body->clipMask == 0 || fastEval ) {
			continue;
		}

		idEntity *passEntity = SetupCollisionForBody( body );

		body->InverseWorldSpatialInertiaMultiply( dir, body->current->externalForce.ToFloatPtr() );
		dir.SubVec6( 0 ) = body->current->spatialVelocity + current.lastTimeStep * dir.SubVec6( 0 );
		const float speed = dir.SubVec3( 0 ).Normalize();
		dir.SubVec3( 1 ).Normalize();
		if ( speed < 1e-3f ) {
			dir.SubVec3( 0 ) = gravityNormal;
		}
		// legacy reached 2 units; the enhanced reach also covers this step's motion
		const float depth = Max( 2.0f, speed * timeStep + margin );
		enhancedReach[i] = depth;

		const int numContacts = gameLocal.Contacts( self, contactInfo, 10, body->current->worldOrigin, dir.SubVec6( 0 ), depth,
							body->clipModel, body->current->worldAxis, body->clipMask, passEntity );

		// world-space vertices of the body for the contact gaps
		enhancedShape_t shape;
		shape.Set( body->clipModel, body->current->worldOrigin, body->current->worldAxis );

		const int firstForBody = contacts.Num();
		for ( int j = 0; j < numContacts; j++ ) {
			// merge nearby contacts between the same bodies and keep at most three
			// planar contacts between any pair, as legacy does
			int numBodyContacts = 0;
			int k;
			for ( k = 0; k < contacts.Num(); k++ ) {
				if ( contacts[k].entityNum == contactInfo[j].entityNum ) {
					if ( ( contacts[k].id == i && contactInfo[j].id == contactBodies[k] ) ||
							( contactBodies[k] == i && contacts[k].id == contactInfo[j].id ) ) {
						if ( ( contacts[k].point - contactInfo[j].point ).LengthSqr() < Square( 2.0f ) ) {
							break;
						}
						if ( idMath::Fabs( contacts[k].normal * contactInfo[j].normal ) > 0.9f ) {
							numBodyContacts++;
						}
					}
				}
			}
			if ( k >= contacts.Num() && numBodyContacts < 3 ) {
				// the row goes where the body is now, not where the sweep met the surface
				contactInfo_t info = contactInfo[j];
				float separation = 0.0f;
				int feature = -1;
				if ( shape.numVertices ) {
					idVec3 pointNow;
					separation = EnhancedContactGap( self, info, shape, dir.SubVec3( 0 ), af_enhancedFeatureGaps.GetBool(), pointNow, feature );
					info.point = pointNow;
					info.dist = info.normal * info.point;
				}
				contacts.Append( info );
				contactBodies.Append( i );
				enhancedSeparation.Append( separation );
				enhancedContactAge.Append( 0 );
				enhancedContactType.Append( feature >= 0 ? (int)contactInfo[j].type : 0 );
				enhancedContactFeature.Append( feature );
			}
		}

		// persistent manifold for this body: static surfaces the sweep did not report
		for ( int c = 0; c < enhancedCache.Num(); c++ ) {
			const OpenQ4Physics::cachedContact_t &cached = enhancedCache[c];
			if ( cached.body != i || cached.age >= af_enhancedPersistFrames.GetInteger() || cached.entityNum == self->entityNumber ) {
				continue;
			}
			idEntity *other = gameLocal.entities[cached.entityNum];
			if ( !other ) {
				continue;
			}
			if ( other != gameLocal.world ) {
				const idPhysics *otherPhysics = other->GetPhysics();
				if ( !otherPhysics || !otherPhysics->IsAtRest() ) {
					continue;
				}
			}
			const idVec3 cachedPoint( cached.point.x, cached.point.y, cached.point.z );
			const idVec3 cachedNormal( cached.normal.x, cached.normal.y, cached.normal.z );
			bool reported = false;
			for ( int k2 = firstForBody; k2 < contacts.Num() && !reported; k2++ ) {
				reported = contacts[k2].entityNum == cached.entityNum && contacts[k2].normal * cachedNormal > 0.9f &&
							( contacts[k2].point - cachedPoint ).LengthSqr() < Square( 2.0f );
			}
			if ( reported || contacts.Num() - firstForBody >= 10 ) {
				continue;
			}
			idVec3 point;
			float separation;
			if ( !EnhancedRenewContact( shape, cached.type, cached.feature, cachedPoint, cachedNormal, margin, 8.0f, point, separation ) ) {
				continue;
			}
			contactInfo_t info;
			memset( &info, 0, sizeof( info ) );
			info.type = CONTACT_TRMVERTEX;
			info.point = point;
			info.normal = cachedNormal;
			info.dist = info.normal * info.point;
			info.entityNum = cached.entityNum;
			info.id = cached.id;
			contacts.Append( info );
			contactBodies.Append( i );
			enhancedSeparation.Append( separation );
			enhancedContactAge.Append( cached.age + 1 );
			enhancedContactType.Append( cached.type );
			enhancedContactFeature.Append( cached.feature );
		}
	}

	AddContactEntitiesForContacts();

	return ( contacts.Num() != 0 );
}

/*
================
idPhysics_AF::SetupContactConstraintsEnhanced

  Legacy setup, then each contact's right-hand side becomes speculative: a gap
  may close within the step but not past the slop. Legacy wrote the bounce
  with the opposite sign, so stock figures never bounced and were tuned that
  way; a figure thrown at a wall bouncing back by its bouncyness travelled a
  third as far as legacy's sliding one. The enhanced step therefore does not
  bounce figures either unless af_enhancedBounce is set.
================
*/
void idPhysics_AF::SetupContactConstraintsEnhanced( float timeStep ) {
	SetupContactConstraints();

	const float invTimeStep = 1.0f / timeStep;
	const float slop = af_enhancedSlop.GetFloat();
	const float bounceThreshold = Max( 2.0f * SUSPEND_LINEAR_VELOCITY, 2.0f * gravityVector.Length() * timeStep );

	enhancedImpact.SetNum( contactConstraints.Num(), false );
	for ( int i = 0; i < contactConstraints.Num(); i++ ) {
		idAFConstraint_Contact *cc = contactConstraints[i];
		idAFConstraint *c = cc;
		const float separation = i < enhancedSeparation.Num() ? enhancedSeparation[i] : 0.0f;

		// approach speed along the contact normal
		float vel = c->J1.SubVec6( 0 ) * c->body1->current->spatialVelocity;
		if ( c->body2 ) {
			vel += c->J2.SubVec6( 0 ) * c->body2->current->spatialVelocity;
		}

		// warm start from the matching contact of last step
		bool matched = false;
		{
			OpenQ4Physics::contactKey_t key;
			key.point = OpenQ4Physics::Vec3( cc->GetContact().point.ToFloatPtr() );
			key.normal = OpenQ4Physics::Vec3( cc->GetContact().normal.ToFloatPtr() );
			key.entityNum = cc->GetContact().entityNum;
			key.id = cc->GetContact().id;
			key.body = contactBodies[i];
			const int match = enhancedCache.Num() ? OpenQ4Physics::MatchCachedContact( key, enhancedCache.Ptr(), enhancedCache.Num(), 2.0f ) : -1;
			if ( match >= 0 ) {
				matched = true;
				c->lm.SetSize( c->J1.GetNumRows() );
				c->lm[0] = enhancedCache[match].impulse[0];
				c->enhancedSolvedFrame = gameLocal.framenum - 1;
				idAFConstraint_ContactFriction *fc = cc->GetFrictionConstraint();
				if ( fc ) {
					idAFConstraint *f = fc;
					f->lm.SetSize( 2 );
					f->lm[0] = enhancedCache[match].impulse[1];
					f->lm[1] = enhancedCache[match].impulse[2];
					f->enhancedSolvedFrame = gameLocal.framenum - 1;
				}
			} else {
				c->enhancedSolvedFrame = 0;
			}
		}

		const float bouncyness = c->body1->GetBouncyness();
		const bool impact = vel < -bounceThreshold && separation + vel * timeStep <= slop;
		enhancedImpact[i] = impact;
		if ( impact && !matched && enhancedContactAge[i] == 0 && bouncyness > 0.0f && af_enhancedBounce.GetBool() ) {
			c->c1[0] = bouncyness * vel;					// J v >= -e vel
		} else if ( separation > slop ) {
			c->c1[0] = ( separation - slop ) * invTimeStep;	// J v >= -(gap - slop) / dt
		} else {
			c->c1[0] = 0.0f;
		}
	}
}

/*
================
idPhysics_AF::AuxiliaryForcesEnhanced

  Legacy AuxiliaryForces with the LCP replaced by warm-started PGS, plus the
  position-only recovery pass.
================
*/
void idPhysics_AF::AuxiliaryForcesEnhanced( float timeStep ) {
	int i, j, k, l, n, m, s, numAuxConstraints, *index, *boxIndex;
	float *ptr, *j1, *j2, *dstPtr, *forcePtr;
	float invStep, u;
	idAFBody *body;
	idAFConstraint *constraint;
	idVecX tmp;
	idMatX jmk;
	idVecX rhs, lm, lo, hi;

	for ( numAuxConstraints = 0, i = 0; i < auxiliaryConstraints.Num(); i++ ) {
		numAuxConstraints += auxiliaryConstraints[i]->J1.GetNumRows();
	}
	if ( numAuxConstraints == 0 ) {
		return;
	}

	forcePtr = (float *) _alloca16( bodies.Num() * numAuxConstraints * 8 * sizeof( float ) );
	index = (int *) _alloca16( bodies.Num() * numAuxConstraints * sizeof( int ) );
	for ( i = 0; i < bodies.Num(); i++ ) {
		body = bodies[i];
		body->response = forcePtr;
		body->responseIndex = index;
		body->numResponses = 0;
		body->maxAuxiliaryIndex = 0;
		forcePtr += numAuxConstraints * 8;
		index += numAuxConstraints;
	}

	// every body records its response to every row: the pseudo velocities need
	// the full responses, which legacy's symmetry shortcut leaves out
	afEnhancedFullResponses = true;
	for ( k = 0, i = 0; i < auxiliaryConstraints.Num(); i++ ) {
		constraint = auxiliaryConstraints[i];
		for ( j = 0; j < constraint->J1.GetNumRows(); j++, k++ ) {
			constraint->body1->tree->Response( constraint, j, k );
			if ( constraint->body2 && constraint->body2->tree != constraint->body1->tree ) {
				constraint->body2->tree->Response( constraint, j, k );
			}
		}
	}
	afEnhancedFullResponses = false;

	jmk.SetData( numAuxConstraints, ( ( numAuxConstraints + 3 ) & ~3 ), MATX_ALLOCA( numAuxConstraints * ( ( numAuxConstraints + 3 ) & ~3 ) ) );
	tmp.SetData( 6, VECX_ALLOCA( 6 ) );

	// lower triangle of the constraint matrix, then mirrored (it is symmetric)
	for ( k = 0, i = 0; i < auxiliaryConstraints.Num(); i++ ) {
		constraint = auxiliaryConstraints[i];
		for ( j = 0; j < constraint->J1.GetNumRows(); j++, k++ ) {
			constraint->body1->InverseWorldSpatialInertiaMultiply( tmp, constraint->J1[j] );
			j1 = tmp.ToFloatPtr();
			ptr = constraint->body1->response;
			index = constraint->body1->responseIndex;
			dstPtr = jmk[k];
			s = k + 1;
			for ( l = n = 0, m = index[n]; n < constraint->body1->numResponses && m < s; n++, m = index[n] ) {
				while ( l < m ) {
					dstPtr[l++] = 0.0f;
				}
				dstPtr[l++] = j1[0] * ptr[0] + j1[1] * ptr[1] + j1[2] * ptr[2] +
								j1[3] * ptr[3] + j1[4] * ptr[4] + j1[5] * ptr[5];
				ptr += 8;
			}
			while ( l < s ) {
				dstPtr[l++] = 0.0f;
			}
			if ( constraint->body2 ) {
				constraint->body2->InverseWorldSpatialInertiaMultiply( tmp, constraint->J2[j] );
				j2 = tmp.ToFloatPtr();
				ptr = constraint->body2->response;
				index = constraint->body2->responseIndex;
				for ( n = 0, m = index[n]; n < constraint->body2->numResponses && m < s; n++, m = index[n] ) {
					dstPtr[m] += j2[0] * ptr[0] + j2[1] * ptr[1] + j2[2] * ptr[2] +
										j2[3] * ptr[3] + j2[4] * ptr[4] + j2[5] * ptr[5];
					ptr += 8;
				}
			}
		}
	}
	n = jmk.GetNumColumns();
	for ( i = 0; i < numAuxConstraints; i++ ) {
		ptr = jmk.ToFloatPtr() + ( i + 1 ) * n + i;
		dstPtr = jmk.ToFloatPtr() + i * n + i + 1;
		for ( j = i + 1; j < numAuxConstraints; j++ ) {
			*dstPtr++ = *ptr;
			ptr += n;
		}
	}

	invStep = 1.0f / timeStep;

	for ( i = 0; i < bodies.Num(); i++ ) {
		body = bodies[i];
		body->InverseWorldSpatialInertiaMultiply( body->acceleration, body->totalForce.ToFloatPtr() );
		body->acceleration.SubVec6( 0 ) += body->current->spatialVelocity * invStep;
	}

	rhs.SetData( numAuxConstraints, VECX_ALLOCA( numAuxConstraints ) );
	lo.SetData( numAuxConstraints, VECX_ALLOCA( numAuxConstraints ) );
	hi.SetData( numAuxConstraints, VECX_ALLOCA( numAuxConstraints ) );
	lm.SetData( numAuxConstraints, VECX_ALLOCA( numAuxConstraints ) );
	boxIndex = (int *) _alloca16( numAuxConstraints * sizeof( int ) );
	float *pseudoTarget = (float *) _alloca16( numAuxConstraints * sizeof( float ) );
	bool *pseudoGuard = (bool *) _alloca16( numAuxConstraints * sizeof( bool ) );
	bool anyPseudo = false;
	bool anyGuard = false;

	for ( k = 0, i = 0; i < auxiliaryConstraints.Num(); i++ ) {
		auxiliaryConstraints[i]->firstIndex = k;
		k += auxiliaryConstraints[i]->J1.GetNumRows();
	}

	const float slop = af_enhancedSlop.GetFloat();
	const float maxRecovery = af_enhancedMaxRecovery.GetFloat();
	const float warmStart = af_enhancedWarmStart.GetFloat();
	const int previousFrame = gameLocal.framenum - 1;
	const bool impactFriction = af_enhancedImpactFriction.GetBool();
	const float impactFrictionScale = Max( 0.0f, af_enhancedImpactFrictionScale.GetFloat() );

	for ( k = 0, i = 0; i < auxiliaryConstraints.Num(); i++ ) {
		constraint = auxiliaryConstraints[i];
		const constraintType_t type = constraint->GetType();
		const bool isLimit = type == CONSTRAINT_CONELIMIT || type == CONSTRAINT_PYRAMIDLIMIT;
		const bool warm = constraint->enhancedSolvedFrame == previousFrame && constraint->lm.GetSize() == constraint->J1.GetNumRows();

		// contact index for the position recovery target
		int contactIndex = -1;
		if ( type == CONSTRAINT_CONTACT ) {
			for ( int c = 0; c < contactConstraints.Num(); c++ ) {
				if ( contactConstraints[c] == constraint ) {
					contactIndex = c;
					break;
				}
			}
		}

		for ( j = 0; j < constraint->J1.GetNumRows(); j++, k++ ) {
			j1 = constraint->J1[j];
			ptr = constraint->body1->acceleration.ToFloatPtr();
			rhs[k] = j1[0] * ptr[0] + j1[1] * ptr[1] + j1[2] * ptr[2] + j1[3] * ptr[3] + j1[4] * ptr[4] + j1[5] * ptr[5];

			// a violated limit's error correction moves bodies by position only
			float c1 = constraint->c1[j];
			pseudoTarget[k] = 0.0f;
			pseudoGuard[k] = false;
			if ( isLimit ) {
				pseudoTarget[k] = Min( Max( 0.0f, -c1 ), maxRecovery );
				c1 = Max( c1, 0.0f );
			} else if ( contactIndex >= 0 && contactIndex < enhancedSeparation.Num() ) {
				// a body that was inside geometry when the figure woke is not pushed
				// out of it: the map or the animation put it there
				const int contactBody = contactIndex < contactBodies.Num() ? contactBodies[contactIndex] : -1;
				const bool placedInside = contactBody >= 0 && contactBody < enhancedEmbedded.Num() && enhancedEmbedded[contactBody];
				if ( enhancedSeparation[contactIndex] < -slop && !placedInside ) {
					pseudoTarget[k] = Min( af_enhancedRecoveryRate.GetFloat() * ( -enhancedSeparation[contactIndex] - slop ) * invStep, maxRecovery );
				} else {
					// a contact that needs no recovery still holds position-only
					// motion back: a gap may close to the slop, never past it
					pseudoGuard[k] = true;
					pseudoTarget[k] = -Max( 0.0f, enhancedSeparation[contactIndex] - slop ) * invStep;
					anyGuard = true;
				}
			}
			anyPseudo |= pseudoTarget[k] > 0.0f;
			rhs[k] += c1 * invStep;

			if ( constraint->body2 ) {
				j2 = constraint->J2[j];
				ptr = constraint->body2->acceleration.ToFloatPtr();
				rhs[k] += j2[0] * ptr[0] + j2[1] * ptr[1] + j2[2] * ptr[2] + j2[3] * ptr[3] + j2[4] * ptr[4] + j2[5] * ptr[5];
				rhs[k] += constraint->c2[j] * invStep;
			}

			rhs[k] = -rhs[k];
			lo[k] = constraint->lo[j];
			hi[k] = constraint->hi[j];

			if ( constraint->boxIndex[j] >= 0 ) {
				if ( constraint->boxConstraint->fl.isPrimary ) {
					gameLocal.Error( "cannot reference primary constraints for the box index" );
				}
				boxIndex[k] = constraint->boxConstraint->firstIndex + constraint->boxIndex[j];
				// A fast new touch slides: its friction is bounded by a share of the
				// body's resting load, none of it by default, and never by the
				// impact force. Legacy resolves impacts without friction.
				if ( impactFriction && constraint->boxConstraint->GetType() == CONSTRAINT_CONTACT ) {
					for ( int c = 0; c < contactConstraints.Num() && c < enhancedImpact.Num(); c++ ) {
						if ( contactConstraints[c] == constraint->boxConstraint ) {
							if ( enhancedImpact[c] ) {
								const idVec3 &normal = contactConstraints[c]->GetContact().normal;
								const float load = impactFrictionScale * OpenQ4Physics::ImpactFrictionLoad( constraint->body1->mass, gravityVector.Length(), -( normal * gravityNormal ) );
								lo[k] = constraint->lo[j] * load;
								hi[k] = constraint->hi[j] * load;
								boxIndex[k] = -1;
							}
							break;
						}
					}
				}
			} else {
				boxIndex[k] = -1;
			}
			jmk[k][k] += constraint->e[j] * invStep;

			lm[k] = warm ? constraint->lm[j] * warmStart : 0.0f;
		}
	}

	// warm-started projected Gauss-Seidel on the auxiliary rows
	float change = 0.0f;
	OpenQ4Physics::SolveBoxedPGS( jmk.ToFloatPtr(), jmk.GetNumColumns(), lm.ToFloatPtr(), rhs.ToFloatPtr(), lo.ToFloatPtr(), hi.ToFloatPtr(),
						boxIndex, numAuxConstraints, idMath::ClampInt( 1, 200, af_enhancedIterations.GetInteger() ),
						af_enhancedTolerance.GetFloat(), change );
	{
		float scale = 1.0f;
		for ( k = 0; k < numAuxConstraints; k++ ) {
			scale = Max( scale, idMath::Fabs( lm[k] ) );
		}
		const float error = OpenQ4Physics::BoxedLCPError( jmk.ToFloatPtr(), jmk.GetNumColumns(), lm.ToFloatPtr(), rhs.ToFloatPtr(),
										lo.ToFloatPtr(), hi.ToFloatPtr(), boxIndex, numAuxConstraints );
		if ( error > af_enhancedFallbackError.GetFloat() * scale ) {
			// an ill-conditioned set the iterations could not settle: solve it exactly
			OpenQ4Physics::Telemetry &telemetry = OpenQ4Physics::GetTelemetry();
			telemetry.NoteCounter( OpenQ4Physics::COUNTER_SOLVER_FALLBACKS );
			const bool timed = telemetry.IsRecording();
			std::chrono::steady_clock::time_point started;
			if ( timed ) {
				started = std::chrono::steady_clock::now();
			}
			if ( !lcp->Solve( jmk, lm, rhs, lo, hi, boxIndex ) ) {
				lm.Zero();
			}
			if ( timed ) {
				const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - started;
				telemetry.NoteFallbackMs( elapsed.count() );
			}
			if ( af_enhancedDebug.GetInteger() & 4 ) {
				gameLocal.Printf( "AF_FALLBACK %s f=%d rows=%d contacts=%d error=%.4f scale=%.1f\n", self ? self->name.c_str() : "?",
					gameLocal.framenum, numAuxConstraints, contactConstraints.Num(), error, scale );
			}
		}
	}

	// position-only recovery: overlap and violated limits are removed by a pseudo
	// velocity integrated into the positions this step and then discarded. The
	// joint recovery has already given the bodies a pseudo velocity, solved in
	// the tree alone; every contact takes part here, so neither that motion nor
	// a limit's can carry a body into a surface the step knows about.
	bool jointPseudo = false;
	for ( i = 0; i < bodies.Num() && !jointPseudo; i++ ) {
		jointPseudo = bodies[i]->enhancedPseudoVelocity.SubVec3( 0 ).LengthSqr() + bodies[i]->enhancedPseudoVelocity.SubVec3( 1 ).LengthSqr() > 1e-8f;
	}
	if ( anyPseudo || ( jointPseudo && anyGuard ) ) {
		idVecX pseudoRhs, pseudoLo, pseudoHi, pseudoLm;
		pseudoRhs.SetData( numAuxConstraints, VECX_ALLOCA( numAuxConstraints ) );
		pseudoLo.SetData( numAuxConstraints, VECX_ALLOCA( numAuxConstraints ) );
		pseudoHi.SetData( numAuxConstraints, VECX_ALLOCA( numAuxConstraints ) );
		pseudoLm.SetData( numAuxConstraints, VECX_ALLOCA( numAuxConstraints ) );
		for ( k = 0, i = 0; i < auxiliaryConstraints.Num(); i++ ) {
			constraint = auxiliaryConstraints[i];
			for ( j = 0; j < constraint->J1.GetNumRows(); j++, k++ ) {
				pseudoLo[k] = 0.0f;
				pseudoLm[k] = 0.0f;
				if ( pseudoTarget[k] <= 0.0f && !pseudoGuard[k] ) {
					pseudoRhs[k] = 0.0f;
					pseudoHi[k] = 0.0f;
					continue;
				}
				// what the joint recovery already moves this row by
				j1 = constraint->J1[j];
				ptr = constraint->body1->enhancedPseudoVelocity.ToFloatPtr();
				float bias = j1[0] * ptr[0] + j1[1] * ptr[1] + j1[2] * ptr[2] + j1[3] * ptr[3] + j1[4] * ptr[4] + j1[5] * ptr[5];
				if ( constraint->body2 ) {
					j2 = constraint->J2[j];
					ptr = constraint->body2->enhancedPseudoVelocity.ToFloatPtr();
					bias += j2[0] * ptr[0] + j2[1] * ptr[1] + j2[2] * ptr[2] + j2[3] * ptr[3] + j2[4] * ptr[4] + j2[5] * ptr[5];
				}
				// J v_pseudo = bias + dt * ( jmk lambda ) must reach the target speed
				pseudoRhs[k] = ( pseudoTarget[k] - bias ) * invStep;
				pseudoHi[k] = idMath::INFINITY;
			}
		}
		OpenQ4Physics::SolveBoxedPGS( jmk.ToFloatPtr(), jmk.GetNumColumns(), pseudoLm.ToFloatPtr(), pseudoRhs.ToFloatPtr(),
							pseudoLo.ToFloatPtr(), pseudoHi.ToFloatPtr(), NULL, numAuxConstraints,
							idMath::ClampInt( 1, 200, af_enhancedIterations.GetInteger() ), af_enhancedTolerance.GetFloat(), change );
		idVecX force( 6, VECX_ALLOCA( 6 ) );
		idVecX velocity( 6, VECX_ALLOCA( 6 ) );
		for ( i = 0; i < bodies.Num(); i++ ) {
			body = bodies[i];
			force.Zero();
			ptr = body->response;
			for ( n = 0; n < body->numResponses; n++, ptr += 8 ) {
				u = pseudoLm[body->responseIndex[n]];
				if ( u != 0.0f ) {
					force[0] += ptr[0] * u; force[1] += ptr[1] * u; force[2] += ptr[2] * u;
					force[3] += ptr[3] * u; force[4] += ptr[4] * u; force[5] += ptr[5] * u;
				}
			}
			body->InverseWorldSpatialInertiaMultiply( velocity, force.ToFloatPtr() );
			body->enhancedPseudoVelocity += timeStep * velocity.SubVec6( 0 );
		}
	}

	// auxiliary constraint forces
	for ( k = 0, i = 0; i < auxiliaryConstraints.Num(); i++ ) {
		constraint = auxiliaryConstraints[i];
		constraint->lm.SetSize( constraint->J1.GetNumRows() );
		constraint->enhancedSolvedFrame = gameLocal.framenum;
		for ( j = 0; j < constraint->J1.GetNumRows(); j++, k++ ) {
			constraint->lm[j] = u = lm[k];

			j1 = constraint->J1[j];
			ptr = constraint->body1->auxForce.ToFloatPtr();
			ptr[0] += j1[0] * u; ptr[1] += j1[1] * u; ptr[2] += j1[2] * u;
			ptr[3] += j1[3] * u; ptr[4] += j1[4] * u; ptr[5] += j1[5] * u;

			if ( constraint->body2 ) {
				j2 = constraint->J2[j];
				ptr = constraint->body2->auxForce.ToFloatPtr();
				ptr[0] += j2[0] * u; ptr[1] += j2[1] * u; ptr[2] += j2[2] * u;
				ptr[3] += j2[3] * u; ptr[4] += j2[4] * u; ptr[5] += j2[5] * u;
			}
		}
	}

	PrimaryForces( timeStep );

	for ( i = 0; i < bodies.Num(); i++ ) {
		body = bodies[i];
		body->response = NULL;
		body->responseIndex = NULL;
	}
}

/*
================
idPhysics_AF::EnhancedJointRecovery

  Closes the part of each tree joint's error that the velocity-level
  correction leaves, by position only. A corpse thrown into geometry has its
  bodies stopped one by one where each hits, which pulls its joints apart by
  up to a step's travel. Legacy pulls them back at up to 256 units/s of real
  velocity, which is what makes a stuck limb fling its figure; a 32 units/s cap
  alone leaves the figure visibly stretched for a second.

  The tree is solved once more with no forces and no velocities, only the
  excess correction as the constraint target. The result is a pseudo velocity
  that moves the bodies back together this step and is then discarded, so the
  recovery cannot add kinetic energy. Runs after PrimaryFactor and before the
  real solves, which rebuild every temporary this pass uses.
================
*/
void idPhysics_AF::EnhancedJointRecovery( float timeStep, const float *jointExcess ) {
	const float invStep = 1.0f / timeStep;
	idVecX force( 6, VECX_ALLOCA( 6 ) );
	idVecX velocity( 6, VECX_ALLOCA( 6 ) );

	for ( int t = 0; t < trees.Num(); t++ ) {
		const idAFTree *tree = trees[t];
		if ( tree->sortedBodies.Num() < 2 ) {
			continue;
		}

		bool any = false;
		for ( int i = 0; i < tree->sortedBodies.Num(); i++ ) {
			idAFBody *body = tree->sortedBodies[i];
			body->s.Zero();
			body->fl.isZero = true;
			idAFConstraint *c = body->primaryConstraint;
			if ( !c ) {
				continue;
			}
			const int index = primaryConstraints.FindIndex( c );
			c->s.Zero();
			c->fl.isZero = true;
			if ( index < 0 ) {
				continue;
			}
			for ( int j = 0; j < c->s.GetSize() && j < 6; j++ ) {
				const float excess = jointExcess[index * 6 + j];
				if ( excess != 0.0f ) {
					c->s[j] = invStep * excess;
					c->fl.isZero = false;
					any = true;
				}
			}
		}
		if ( !any ) {
			continue;
		}

		tree->Solve();

		for ( int i = 0; i < tree->sortedBodies.Num(); i++ ) {
			idAFBody *body = tree->sortedBodies[i];
			force.Zero();
			if ( body->primaryConstraint ) {
				body->primaryConstraint->J1.TransposeMultiplyAdd( force, body->primaryConstraint->lm );
			}
			for ( int j = 0; j < body->children.Num(); j++ ) {
				idAFConstraint *child = body->children[j]->primaryConstraint;
				child->J2.TransposeMultiplyAdd( force, child->lm );
			}
			body->InverseWorldSpatialInertiaMultiply( velocity, force.ToFloatPtr() );
			body->enhancedPseudoVelocity += timeStep * velocity.SubVec6( 0 );
		}
	}
}

/*
================
idPhysics_AF::EvolveEnhanced

  Legacy Evolve, with the contact verification limited to touching contacts
  (speculative gaps may close), positions moved by the pseudo velocity as well,
  and the energy guard.
================
*/
void idPhysics_AF::EvolveEnhanced( float timeStep, float energyBefore ) {
	idVec3 vec;
	idRotation rotation;
	const float maxLinearVelocity = af_maxLinearVelocity.GetFloat() / timeStep;
	const float maxAngularVelocity = af_maxAngularVelocity.GetFloat() / timeStep;
	float work = 0.0f;

	for ( int i = 0; i < bodies.Num(); i++ ) {
		idAFBody *body = bodies[i];
		body->InverseWorldSpatialInertiaMultiply( body->acceleration, body->totalForce.ToFloatPtr() );
		body->next->spatialVelocity = body->current->spatialVelocity + timeStep * body->acceleration.SubVec6( 0 );

		if ( maxLinearVelocity > 0.0f ) {
			const float vSqr = body->next->spatialVelocity.SubVec3( 0 ).LengthSqr();
			if ( vSqr > Square( maxLinearVelocity ) ) {
				body->next->spatialVelocity.SubVec3( 0 ) *= idMath::InvSqrt( vSqr ) * maxLinearVelocity;
			}
		}
		if ( maxAngularVelocity > 0.0f ) {
			const float vSqr = body->next->spatialVelocity.SubVec3( 1 ).LengthSqr();
			if ( vSqr > Square( maxAngularVelocity ) ) {
				body->next->spatialVelocity.SubVec3( 1 ) *= idMath::InvSqrt( vSqr ) * maxAngularVelocity;
			}
		}
		// work done by gravity and external forces this step (an upper bound)
		work += Max( 0.0f, timeStep * ( body->current->externalForce.SubVec3( 0 ) * body->next->spatialVelocity.SubVec3( 0 ) +
										body->current->externalForce.SubVec3( 1 ) * body->next->spatialVelocity.SubVec3( 1 ) ) );
	}

	// a touching contact never keeps closing (legacy's verification, for real contacts only)
	const float slop = af_enhancedSlop.GetFloat();
	for ( int i = 0; i < contactConstraints.Num(); i++ ) {
		if ( i < enhancedSeparation.Num() && enhancedSeparation[i] > slop ) {
			continue;
		}
		idAFBody *body = contactConstraints[i]->body1;
		idVec3 normal = contactConstraints[i]->GetContact().normal;
		if ( normal * body->next->spatialVelocity.SubVec3( 0 ) <= 0.0f ) {
			body->next->spatialVelocity.SubVec3( 0 ) -= 1.0001f * ( normal * body->next->spatialVelocity.SubVec3( 0 ) ) * normal;
		}
		body = contactConstraints[i]->body2;
		if ( !body ) {
			continue;
		}
		normal = -normal;
		if ( normal * body->next->spatialVelocity.SubVec3( 0 ) <= 0.0f ) {
			body->next->spatialVelocity.SubVec3( 0 ) -= 1.0001f * ( normal * body->next->spatialVelocity.SubVec3( 0 ) ) * normal;
		}
	}

	// energy guard: constraint and contact resolution may not add kinetic energy.
	// The excess comes out of the motion relative to the centre of mass first,
	// which internal forces can change, so the figure keeps its momentum.
	{
		const float energyAfter = EnhancedKineticEnergy( true );
		const float allowed = energyBefore + work + 1e-3f * ( energyBefore + 1.0f );
		if ( energyAfter > allowed ) {
			float totalMass = 0.0f;
			idVec3 momentum = vec3_origin;
			for ( int i = 0; i < bodies.Num(); i++ ) {
				totalMass += bodies[i]->mass;
				momentum += bodies[i]->mass * bodies[i]->next->spatialVelocity.SubVec3( 0 );
			}
			const idVec3 comVelocity = totalMass > 0.0f ? momentum / totalMass : vec3_origin;
			const float comEnergy = 0.5f * totalMass * comVelocity.LengthSqr();
			const OpenQ4Physics::energySplit_t split = OpenQ4Physics::EnergyScaleKeepingMomentum( energyAfter, comEnergy, allowed );
			if ( af_enhancedDebug.GetInteger() & 1 ) {
				gameLocal.Printf( "AF_ENERGY %s before=%.0f work=%.0f after=%.0f com=%.0f relative=%.3f shared=%.3f\n", self ? self->name.c_str() : "?",
					energyBefore, work, energyAfter, comEnergy, split.relativeScale, split.comScale );
			}
			if ( !( af_enhancedDebug.GetInteger() & 2 ) ) {
				for ( int i = 0; i < bodies.Num(); i++ ) {
					idVec6 &v = bodies[i]->next->spatialVelocity;
					v.SubVec3( 0 ) = split.comScale * comVelocity + split.relativeScale * ( v.SubVec3( 0 ) - comVelocity );
					v.SubVec3( 1 ) *= split.relativeScale;
				}
				OpenQ4Physics::GetTelemetry().NoteCounter( OpenQ4Physics::COUNTER_ENERGY_CLAMPS );
			}
		}
	}

	// Position-only motion may not carry a body into a surface it already
	// touches: the joint recovery is solved without the contact rows.
	for ( int i = 0; i < contactConstraints.Num(); i++ ) {
		if ( i < enhancedSeparation.Num() && enhancedSeparation[i] > slop ) {
			continue;
		}
		const idVec3 normal = contactConstraints[i]->GetContact().normal;
		idAFBody *touching = contactConstraints[i]->body1;
		float into = normal * touching->enhancedPseudoVelocity.SubVec3( 0 );
		if ( into < 0.0f ) {
			touching->enhancedPseudoVelocity.SubVec3( 0 ) -= into * normal;
		}
		touching = contactConstraints[i]->body2;
		if ( touching ) {
			into = -( normal * touching->enhancedPseudoVelocity.SubVec3( 0 ) );
			if ( into < 0.0f ) {
				touching->enhancedPseudoVelocity.SubVec3( 0 ) += into * normal;
			}
		}
	}

	for ( int i = 0; i < bodies.Num(); i++ ) {
		idAFBody *body = bodies[i];
		const idVec6 motion = body->next->spatialVelocity + body->enhancedPseudoVelocity;

		body->next->worldOrigin = body->current->worldOrigin + timeStep * motion.SubVec3( 0 );

		vec = motion.SubVec3( 1 );
		const float angle = -timeStep * (float) RAD2DEG( vec.Normalize() );
		rotation = idRotation( vec3_origin, vec, angle );
		rotation.Normalize180();
		body->next->worldAxis = body->current->worldAxis * rotation.ToMat3();
		body->next->worldAxis.OrthoNormalizeSelf();

		body->next->spatialVelocity.SubVec3( 0 ) -= body->linearFriction * body->next->spatialVelocity.SubVec3( 0 );
		body->next->spatialVelocity.SubVec3( 1 ) -= body->angularFriction * body->next->spatialVelocity.SubVec3( 1 );
	}
}

/*
================
idPhysics_AF::CheckForCollisionsEnhanced

  Legacy sweeps every body every step. The enhanced step's contacts already
  reach along each body's motion, so only a body that moved or turned further
  than that reach is swept. The part of a body's motion that position-only
  recovery added is swept separately.
================
*/
void idPhysics_AF::CheckForCollisionsEnhanced( float timeStep ) {
	idMat3 axis;
	idRotation rotation;
	trace_t collision;

	collisions.SetNum( 0, false );

	if ( !enableCollision ) {
		return;
	}

	const float slop = af_enhancedSlop.GetFloat();
	const float recoveryStep = af_enhancedRecoverySweep.GetFloat();
	const int auditFrames = af_enhancedAuditFrames.GetInteger();

	// the poses the audit sweeps start from: where each body was when the figure woke
	if ( enhancedVerifiedOrigin.Num() != bodies.Num() ) {
		enhancedVerifiedOrigin.SetNum( bodies.Num(), false );
		enhancedVerifiedAxis.SetNum( bodies.Num(), false );
		for ( int i = 0; i < bodies.Num(); i++ ) {
			enhancedVerifiedOrigin[i] = bodies[i]->current->worldOrigin;
			enhancedVerifiedAxis[i] = bodies[i]->current->worldAxis;
		}
	}

	for ( int i = 0; i < bodies.Num(); i++ ) {
		idAFBody *body = bodies[i];

		if ( body->clipMask != 0 && !fastEval ) {
			// Position-only recovery of limits and joints is solved against the
			// contacts the step knows about, but a surface the contact query did
			// not report holds nothing back, and a unit a step is enough to walk a
			// limb into a wall. That part of the motion is therefore swept on its
			// own, after the real motion, so stopping it never stops the body's
			// real motion. A body being pushed out of an overlap is left alone: a
			// sweep that starts inside a surface cannot move.
			idVec3 recovery = timeStep * body->enhancedPseudoVelocity.SubVec3( 0 );
			bool sweepRecovery = recoveryStep > 0.0f && recovery.LengthSqr() > Square( recoveryStep );
			for ( int c = 0; sweepRecovery && c < contactBodies.Num() && c < enhancedSeparation.Num(); c++ ) {
				sweepRecovery = !( contactBodies[c] == i && enhancedSeparation[c] < -slop );
			}
			if ( sweepRecovery ) {
				body->next->worldOrigin -= recovery;
			}

			const float displacement = ( body->next->worldOrigin - body->current->worldOrigin ).Length();
			TransposeMultiply( body->current->worldAxis, body->next->worldAxis, axis );
			rotation = axis.ToRotation();
			const float turn = DEG2RAD( idMath::Fabs( rotation.GetAngle() ) ) * body->clipModel->GetBounds().GetRadius();
			const float reach = i < enhancedReach.Num() ? enhancedReach[i] : 0.0f;
			idEntity *passEntity = NULL;
			bool stopped = false;

			const bool embedded = af_enhancedEmbeddedSweep.GetInteger() >= 2 && i < enhancedEmbedded.Num() && enhancedEmbedded[i];
			const bool fast = embedded || displacement > reach - af_enhancedMargin.GetFloat() * 0.5f || turn > af_enhancedRotationSweep.GetFloat();

			// The audit. A body's contact query is one sweep along the velocity the
			// step started with; the solve can turn the body another way, and a
			// small move off that line goes through space the query never covered.
			// A corpse jittering at a few units a second walked its torso through
			// a thin sheet that way, a tenth of a unit a step. Legacy sweeps every
			// move of every body, which is most of what it costs; sweeping every
			// stray move here quadrupled the sweeps and stopped limbs in flight.
			// Instead a body that only creeps is swept every few steps along the
			// chord from the pose it was last verified at to where it is now (two
			// units at most: a tumbling limb goes round a bump that a longer chord
			// would cut through). A crossing in between is on that chord, and the
			// body is put back at it.
			//
			// A body at rest sits a slop from its support, which is as close as a
			// sweep lets anything come, so the audit acts only when the body is
			// more than that slop past where the sweep stops it; until then the
			// body keeps the pose it was verified at. A chord that starts in a
			// surface proves nothing and is taken as it is. An audit is not an
			// impact: nothing is called back and nothing is pushed.
			bool audit = false;
			bool verified = false;
			if ( !fast && auditFrames > 0 && i < enhancedVerifiedOrigin.Num() && ( ( gameLocal.framenum + i ) % auditFrames ) == 0 ) {
				const float chord = ( body->next->worldOrigin - enhancedVerifiedOrigin[i] ).Length();
				audit = chord <= 2.0f && ( chord > 1e-3f || enhancedVerifiedAxis[i] != body->next->worldAxis );
				verified = chord > 2.0f;		// moving, not creeping: the audit starts again from here
			}

			if ( fast || audit ) {
				const idVec3 from = audit ? enhancedVerifiedOrigin[i] : body->current->worldOrigin;
				const idMat3 fromAxis = audit ? enhancedVerifiedAxis[i] : body->current->worldAxis;
				if ( audit ) {
					TransposeMultiply( fromAxis, body->next->worldAxis, axis );
					rotation = axis.ToRotation();
				}
				passEntity = SetupCollisionForBody( body );
				rotation.SetOrigin( from );
				OpenQ4Physics::GetTelemetry().NoteCounter( OpenQ4Physics::COUNTER_SWEPT );
				const bool hit = gameLocal.Motion( self, collision, from, body->next->worldOrigin, rotation,
										body->clipModel, fromAxis, body->clipMask, passEntity );
				bool act = hit;
				if ( audit ) {
					const float past = ( body->next->worldOrigin - collision.endpos ).Length() +
										DEG2RAD( idMath::Fabs( rotation.GetAngle() ) ) * ( 1.0f - collision.fraction ) * body->clipModel->GetBounds().GetRadius();
					act = hit && collision.fraction > 1e-4f && past > slop;
				}
				verified = !hit || act || ( audit && collision.fraction <= 1e-4f );		// (a fast sweep is always one or the other)
				if ( act ) {
					stopped = true;
					body->next->worldOrigin = collision.endpos;
					body->next->worldAxis = collision.endAxis;
					if ( !audit ) {
						const int index = collisions.Num();
						collisions.SetNum( index + 1, false );
						collisions[index].trace = collision;
						collisions[index].body = body;
					}

					// The surface that stopped the body goes into the manifold, so the
					// next step solves against it even when the contact query does not
					// report it (an edge, or a surface off the body's line of motion).
					// Without the row the body keeps its velocity into the surface, is
					// stopped again every step, and its figure pulls away from it.
					if ( af_enhancedCollisionContacts.GetBool() ) {
						OpenQ4Physics::cachedContact_t cached;
						memset( &cached, 0, sizeof( cached ) );
						cached.point = OpenQ4Physics::Vec3( collision.c.point.ToFloatPtr() );
						cached.normal = OpenQ4Physics::Vec3( collision.c.normal.ToFloatPtr() );
						cached.localPoint = cached.point;
						cached.localNormal = cached.normal;
						cached.entityNum = collision.c.entityNum;
						cached.id = collision.c.id;
						cached.body = i;
						cached.feature = -1;
						enhancedCache.Append( cached );
					}
				}
			}

			// a body its real motion left against a surface takes no recovery this step
			if ( sweepRecovery && !stopped ) {
				if ( !passEntity ) {
					passEntity = SetupCollisionForBody( body );
				}
				trace_t recoveryTrace;
				OpenQ4Physics::GetTelemetry().NoteCounter( OpenQ4Physics::COUNTER_SWEPT );
				gameLocal.Translation( self, recoveryTrace, body->next->worldOrigin, body->next->worldOrigin + recovery,
										body->clipModel, body->next->worldAxis, body->clipMask, passEntity );
				body->next->worldOrigin = recoveryTrace.endpos;
			}

			// a body whose sweep came out clear, or was put back, is verified where it ends this step
			if ( ( verified || auditFrames <= 0 ) && i < enhancedVerifiedOrigin.Num() ) {
				enhancedVerifiedOrigin[i] = body->next->worldOrigin;
				enhancedVerifiedAxis[i] = body->next->worldAxis;
			}
		}

		body->clipModel->Link( self, body->clipModel->GetId(), body->next->worldOrigin, body->next->worldAxis );
	}
}

/*
================
idPhysics_AF::CacheEnhancedContacts
================
*/
void idPhysics_AF::CacheEnhancedContacts( void ) {
	enhancedCache.SetNum( 0, false );
	for ( int i = 0; i < contactConstraints.Num() && i < contacts.Num(); i++ ) {
		idAFConstraint_Contact *cc = contactConstraints[i];
		idAFConstraint *c = cc;
		OpenQ4Physics::cachedContact_t cached;
		memset( &cached, 0, sizeof( cached ) );
		cached.point = OpenQ4Physics::Vec3( contacts[i].point.ToFloatPtr() );
		cached.normal = OpenQ4Physics::Vec3( contacts[i].normal.ToFloatPtr() );
		cached.localPoint = cached.point;
		cached.localNormal = cached.normal;
		cached.entityNum = contacts[i].entityNum;
		cached.id = contacts[i].id;
		cached.body = contactBodies[i];
		cached.age = i < enhancedContactAge.Num() ? enhancedContactAge[i] : 0;
		cached.type = i < enhancedContactType.Num() ? enhancedContactType[i] : 0;
		cached.feature = i < enhancedContactFeature.Num() ? enhancedContactFeature[i] : -1;
		cached.impulse[0] = c->lm.GetSize() > 0 ? c->lm[0] : 0.0f;
		idAFConstraint_ContactFriction *fc = cc->GetFrictionConstraint();
		if ( fc ) {
			idAFConstraint *f = fc;
			if ( f->lm.GetSize() >= 2 ) {
				cached.impulse[1] = f->lm[0];
				cached.impulse[2] = f->lm[1];
			}
		}
		enhancedCache.Append( cached );
		if ( af_enhancedDebug.GetInteger() & 8 ) {
			float pre = c->J1.SubVec6( 0 ) * c->body1->current->spatialVelocity;
			float post = c->J1.SubVec6( 0 ) * c->body1->next->spatialVelocity;
			gameLocal.Printf( "AF_CONTACT %s imp=%d f=%d b=%d ent=%d n=(%.2f %.2f %.2f) sep=%.2f age=%d type=%d feature=%d p=(%.1f %.1f %.1f) pre=%.0f post=%.0f lm=%.0f fr=(%.0f %.0f) v=(%.0f %.0f %.0f)\n",
				self ? self->name.c_str() : "?", i < enhancedImpact.Num() && enhancedImpact[i] ? 1 : 0, gameLocal.framenum, contactBodies[i], contacts[i].entityNum, contacts[i].normal.x, contacts[i].normal.y, contacts[i].normal.z,
				i < enhancedSeparation.Num() ? enhancedSeparation[i] : 0.0f, cached.age, cached.type, cached.feature, contacts[i].point.x, contacts[i].point.y, contacts[i].point.z, pre, post, cached.impulse[0], cached.impulse[1], cached.impulse[2],
				c->body1->current->spatialVelocity.SubVec3( 0 ).x, c->body1->current->spatialVelocity.SubVec3( 0 ).y, c->body1->current->spatialVelocity.SubVec3( 0 ).z );
		}
	}
}

/*
================
idPhysics_AF::EvaluateEnhanced
================
*/
bool idPhysics_AF::EvaluateEnhanced( int timeStepMSec, int endTimeMSec ) {
	float timeStep;

	if ( timeScaleRampStart < MS2SEC( endTimeMSec ) && timeScaleRampEnd > MS2SEC( endTimeMSec ) ) {
		timeStep = MS2SEC( timeStepMSec ) * ( MS2SEC( endTimeMSec ) - timeScaleRampStart ) / ( timeScaleRampEnd - timeScaleRampStart );
	} else if ( af_timeScale.GetFloat() != 1.0f ) {
		timeStep = MS2SEC( timeStepMSec ) * af_timeScale.GetFloat();
	} else {
		timeStep = MS2SEC( timeStepMSec ) * timeScale;
	}
	current.lastTimeStep = timeStep;

	if ( changedAF || ( linearTime != af_useLinearTime.GetBool() ) ) {
		BuildTrees();
		changedAF = false;
		linearTime = af_useLinearTime.GetBool();
	}

	if ( masterBody ) {
		idVec3 masterOrigin;
		idMat3 masterAxis;
		self->GetMasterPosition( masterOrigin, masterAxis );
		if ( current.atRest >= 0 && ( masterBody->current->worldOrigin != masterOrigin || masterBody->current->worldAxis != masterAxis ) ) {
			Activate();
		}
		masterBody->current->worldOrigin = masterOrigin;
		masterBody->current->worldAxis = masterAxis;
	}

	if ( current.atRest >= 0 || timeStep <= 0.0f ) {
		// the bodies are tested for geometry again when the figure wakes, and the
		// audit starts again from where they are then
		enhancedEmbedded.SetNum( 0, false );
		enhancedVerifiedOrigin.SetNum( 0, false );
		enhancedVerifiedAxis.SetNum( 0, false );
		DebugDraw();
		return false;
	}

	// Bodies that start inside static geometry are swept every step, as legacy
	// sweeps every body: tested when the figure wakes, and again every half
	// second while any is still inside.
	if ( af_enhancedEmbeddedSweep.GetInteger() > 0 ) {
		const bool woke = enhancedEmbedded.Num() != bodies.Num();
		bool any = false;
		for ( int i = 0; i < enhancedEmbedded.Num() && !any; i++ ) {
			any = enhancedEmbedded[i];
		}
		if ( woke || ( any && ( ( gameLocal.framenum + ( self ? self->entityNumber : 0 ) ) & 31 ) == 0 ) ) {
			enhancedEmbedded.SetNum( bodies.Num(), false );
			for ( int i = 0; i < bodies.Num(); i++ ) {
				enhancedEmbedded[i] = ( woke || enhancedEmbedded[i] ) && !fastEval && EnhancedBodyEmbedded( bodies[i] );
				if ( enhancedEmbedded[i] && ( af_enhancedDebug.GetInteger() & 16 ) ) {
					gameLocal.Printf( "AF_EMBEDDED %s f=%d body=%d %s woke=%d\n", self ? self->name.c_str() : "?", gameLocal.framenum, i,
						bodies[i]->GetName().c_str(), woke ? 1 : 0 );
				}
			}
		}
	} else {
		enhancedEmbedded.SetNum( 0, false );
	}

	AddPushVelocity( -current.pushVelocity );

	const float energyBefore = EnhancedKineticEnergy( false );

	EvaluateContactsEnhanced( timeStep );
	SetupContactConstraintsEnhanced( timeStep );

	EvaluateConstraints( timeStep );

	// Bounded joint error correction. Legacy corrects every joint at up to 256
	// units/s of real velocity, which turns a limb stuck in a wall into a figure
	// that flies apart. Here a joint that holds is corrected at no more than
	// af_enhancedMaxJointCorrection. A joint pulled visibly apart (a body
	// stopped by geometry while the rest of its figure moved on) gets legacy's
	// bound back, rising between a quarter of af_enhancedJointStretch and the
	// whole of it: that velocity is what slows the figure when the stopped body
	// cannot follow. The energy guard still limits what it can add. What the
	// joint needs beyond the velocity is closed by position only
	// (EnhancedJointRecovery).
	float *jointExcess = (float *) _alloca16( Max( 1, primaryConstraints.Num() ) * 6 * sizeof( float ) );
	bool anyJointExcess = false;
	{
		const float maxCorrection = af_enhancedMaxJointCorrection.GetFloat();
		const float maxRecovery = Max( maxCorrection, af_enhancedMaxJointRecovery.GetFloat() );
		const float stretched = Max( 0.1f, af_enhancedJointStretch.GetFloat() );
		const float invTimeStep = 1.0f / timeStep;
		for ( int i = 0; i < primaryConstraints.Num(); i++ ) {
			idAFConstraint *constraint = primaryConstraints[i];
			idVecX &c1 = constraint->c1;
			// The joint's own error term stops at legacy's 256 units/s. The first
			// three rows of an anchored joint are its anchor error, so the whole of
			// it is read from the anchors again: half the gap per step, by position.
			idVec3 anchorError;
			bool anchored = true;
			switch ( constraint->GetType() ) {
				case CONSTRAINT_BALLANDSOCKETJOINT:
					anchorError = static_cast<idAFConstraint_BallAndSocketJoint *>( constraint )->GetAnchorOffset();
					break;
				case CONSTRAINT_UNIVERSALJOINT:
					anchorError = static_cast<idAFConstraint_UniversalJoint *>( constraint )->GetAnchorOffset();
					break;
				case CONSTRAINT_HINGE:
					anchorError = static_cast<idAFConstraint_Hinge *>( constraint )->GetAnchorOffset();
					break;
				default:
					anchored = false;
					break;
			}
			// A joint that is less than a quarter of the visible stretch apart is
			// left to the bounded velocity alone, as before: position-only motion
			// is not resisted by contact friction, so using it on the small errors
			// every sliding or settling figure carries would let it creep.
			const bool apart = !anchored || anchorError.Length() > 0.25f * stretched;
			float velocityCap = maxCorrection;
			if ( anchored && maxCorrection < ERROR_REDUCTION_MAX ) {
				const float ramp = idMath::ClampFloat( 0.0f, 1.0f, ( anchorError.Length() - 0.25f * stretched ) / ( 0.75f * stretched ) );
				velocityCap += ramp * ( ERROR_REDUCTION_MAX - maxCorrection );
			}
			for ( int j = 0; j < 6; j++ ) {
				float excess = 0.0f;
				if ( j < c1.GetSize() ) {
					const float full = ( anchored && j < 3 ) ? -( invTimeStep * ERROR_REDUCTION ) * anchorError[j] : c1[j];
					const float kept = idMath::ClampFloat( -velocityCap, velocityCap, c1[j] );
					if ( apart ) {
						excess = idMath::ClampFloat( -maxRecovery, maxRecovery, full ) - kept;
					}
					c1[j] = kept;
				}
				jointExcess[i * 6 + j] = excess;
				anyJointExcess |= idMath::Fabs( excess ) > 1e-3f;
			}
		}
	}

	ApplyFriction( timeStep, endTimeMSec );

	// warm-start friction rows the contacts just (re)created
	for ( int i = 0; i < contactConstraints.Num(); i++ ) {
		idAFConstraint_ContactFriction *fc = contactConstraints[i]->GetFrictionConstraint();
		idAFConstraint *c = contactConstraints[i];
		if ( fc && c->enhancedSolvedFrame != gameLocal.framenum - 1 ) {
			static_cast<idAFConstraint *>( fc )->enhancedSolvedFrame = 0;
		}
	}

	AddFrameConstraints();
	PrimaryFactor();
	for ( int i = 0; i < bodies.Num(); i++ ) {
		bodies[i]->enhancedPseudoVelocity.Zero();
	}
	if ( anyJointExcess ) {
		EnhancedJointRecovery( timeStep, jointExcess );
	}
	PrimaryForces( timeStep );
	AuxiliaryForcesEnhanced( timeStep );
	EvolveEnhanced( timeStep, energyBefore );

	DebugDraw();
	ClearExternalForce();
	ApplyContactForces();
	CacheEnhancedContacts();
	RemoveFrameConstraints();

	CheckForCollisionsEnhanced( timeStep );
	SwapStates();

	if ( selfCollision && !af_skipSelfCollision.GetBool() ) {
		DisableClip();
	}

	if ( ApplyCollisions( timeStep ) ) {
		current.atRest = gameLocal.time;
		comeToRest = true;
	}

	// quiet figures sleep (legacy's test also still applies)
	bool quiet = true;
	{
		const float linear = af_enhancedSleepSpeed.GetFloat();
		const float angular = af_enhancedSleepAngularSpeed.GetFloat();
		for ( int i = 0; i < bodies.Num() && quiet; i++ ) {
			quiet = bodies[i]->current->spatialVelocity.SubVec3( 0 ).LengthSqr() <= Square( linear ) &&
					bodies[i]->current->spatialVelocity.SubVec3( 1 ).LengthSqr() <= Square( angular );
		}
		enhancedQuietFrames = quiet ? enhancedQuietFrames + 1 : 0;
	}
	const bool longQuiet = enhancedQuietFrames >= af_enhancedSleepFrames.GetInteger();
	if ( comeToRest && ( TestIfAtRest( timeStep ) || longQuiet ) ) {
		Rest();
	} else if ( !quiet ) {
		ActivateContactEntities();
	}

	AddGravity();
	AddPushVelocity( current.pushVelocity );
	current.pushVelocity.Zero();

	if ( IsOutsideWorld() ) {
		if ( bodies.Num() && bodies[0] && bodies[0]->current && self ) {
			gameLocal.Warning( "articulated figure moved outside world bounds for entity '%s' type '%s' at (%s)",
							self->name.c_str(), self->GetType()->classname, bodies[0]->current->worldOrigin.ToString(0) );
		} else {
			gameLocal.Warning( "articulated figure moved outside world bounds for entity '%s' type '%s' -- no body",
							self->name.c_str(), self->GetType()->classname );
		}
		Rest();
	}

	return true;
}

/*
================
idPhysics_AF::ApplyImpulseEnhanced

  The impulse guard (PHYS-04). The figure receives the same linear momentum as
  legacy. When the struck body would gain more than af_enhancedImpulseGuard
  units/s, part of the impulse is applied at the hit point and the rest is
  spread over every body by mass, so a light hand or head cannot leave at the
  legacy J / m and drag the figure across the map.
================
*/
void idPhysics_AF::ApplyImpulseEnhanced( const int id, const idVec3 &point, const idVec3 &impulse ) {
	float totalMass = 0.0f;
	for ( int i = 0; i < bodies.Num(); i++ ) {
		totalMass += bodies[i]->mass;
	}
	idAFBody *struck = bodies[id];
	const float magnitude = impulse.Length();
	const OpenQ4Physics::impulseSplit_t split = OpenQ4Physics::SplitImpulse( magnitude, struck->mass, totalMass, af_enhancedImpulseGuard.GetFloat() );

	const idVec3 atPoint = split.struckFraction * impulse;
	const idMat3 invWorldInertiaTensor = struck->current->worldAxis.Transpose() * struck->inverseInertiaTensor * struck->current->worldAxis;
	struck->current->spatialVelocity.SubVec3( 0 ) += struck->invMass * atPoint;
	struck->current->spatialVelocity.SubVec3( 1 ) += invWorldInertiaTensor * ( point - struck->current->worldOrigin ).Cross( atPoint );

	if ( split.struckFraction < 1.0f && totalMass > 0.0f ) {
		const idVec3 spread = ( ( 1.0f - split.struckFraction ) / totalMass ) * impulse;
		for ( int i = 0; i < bodies.Num(); i++ ) {
			bodies[i]->current->spatialVelocity.SubVec3( 0 ) += spread;
		}
	}

	Activate();
	if ( OpenQ4Physics::GetTelemetry().IsRecording() ) {
		OpenQ4Physics::GetTelemetry().NoteImpulse( magnitude, split.struckDeltaV, split.clamped );
	}
}

/*
================
Joint anchor offset

  From body1's anchor to body2's, in world space: zero for a joint that holds,
  the stretch of one that does not. Read by the joint recovery and, in either
  mode, by physicsQuality.
================
*/
static idVec3 OpenQ4Physics_AnchorOffset( const idAFBody *body1, const idVec3 &anchor1, const idAFBody *master, const idVec3 &anchor2 ) {
	const idVec3 a1 = body1->GetWorldOrigin() + anchor1 * body1->GetWorldAxis();
	const idVec3 a2 = master ? master->GetWorldOrigin() + anchor2 * master->GetWorldAxis() : anchor2;
	return a2 - a1;
}

idVec3 idAFConstraint_BallAndSocketJoint::GetAnchorOffset( void ) const {
	return OpenQ4Physics_AnchorOffset( body1, anchor1, body2 ? body2 : physics->GetMasterBody(), anchor2 );
}

idVec3 idAFConstraint_UniversalJoint::GetAnchorOffset( void ) const {
	return OpenQ4Physics_AnchorOffset( body1, anchor1, body2 ? body2 : physics->GetMasterBody(), anchor2 );
}

idVec3 idAFConstraint_Hinge::GetAnchorOffset( void ) const {
	return OpenQ4Physics_AnchorOffset( body1, anchor1, body2 ? body2 : physics->GetMasterBody(), anchor2 );
}

/*
================
idPhysics_AF::MaxJointStretch

  The largest anchor separation over the figure's ball, universal and hinge
  joints. Telemetry records it per frame in either mode.
================
*/
float idPhysics_AF::MaxJointStretch( void ) const {
	float stretch = 0.0f;
	for ( int i = 0; i < constraints.Num(); i++ ) {
		idAFConstraint *constraint = constraints[i];
		switch ( constraint->GetType() ) {
			case CONSTRAINT_BALLANDSOCKETJOINT:
				stretch = Max( stretch, static_cast<idAFConstraint_BallAndSocketJoint *>( constraint )->GetAnchorOffset().Length() );
				break;
			case CONSTRAINT_UNIVERSALJOINT:
				stretch = Max( stretch, static_cast<idAFConstraint_UniversalJoint *>( constraint )->GetAnchorOffset().Length() );
				break;
			case CONSTRAINT_HINGE:
				stretch = Max( stretch, static_cast<idAFConstraint_Hinge *>( constraint )->GetAnchorOffset().Length() );
				break;
			default:
				break;
		}
	}
	return stretch;
}
