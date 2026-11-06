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
//   - joint error correction is bounded far below legacy's 256 units/s
//   - only bodies that move further than their speculative reach are swept
//   - an energy guard keeps the solve from adding kinetic energy beyond the
//     work of gravity and external forces
//   - quiet figures sleep, and a quiet figure does not keep waking neighbours
// External impulses keep their linear momentum but a hit on a light limb is
// spread over the figure by mass (the impulse guard, PHYS-04).

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
		float vertices[MAX_TRACEMODEL_VERTS * 3];
		int numVertices = 0;
		const idTraceModel *trm = body->clipModel->GetTraceModel();
		if ( trm ) {
			numVertices = Min( trm->numVerts, MAX_TRACEMODEL_VERTS );
			for ( int v = 0; v < numVertices; v++ ) {
				const idVec3 world = body->current->worldOrigin + trm->verts[v] * body->current->worldAxis;
				vertices[v * 3 + 0] = world.x;
				vertices[v * 3 + 1] = world.y;
				vertices[v * 3 + 2] = world.z;
			}
		}
		const float reach = body->clipModel->GetBounds().GetRadius();

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
				contacts.Append( contactInfo[j] );
				contactBodies.Append( i );
				enhancedSeparation.Append( numVertices ? OpenQ4Physics::SweptContactSeparation( vertices, numVertices,
						OpenQ4Physics::Vec3( contactInfo[j].normal.ToFloatPtr() ), OpenQ4Physics::Vec3( contactInfo[j].point.ToFloatPtr() ),
						OpenQ4Physics::Vec3( dir.SubVec3( 0 ).ToFloatPtr() ), reach ) : 0.0f );
				enhancedContactAge.Append( 0 );
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
			OpenQ4Physics::Vec3 point;
			float separation;
			if ( !OpenQ4Physics::RenewPersistentContact( vertices, numVertices, cached.normal, cached.point, margin, 8.0f, point, separation ) ) {
				continue;
			}
			contactInfo_t info;
			memset( &info, 0, sizeof( info ) );
			info.type = CONTACT_TRMVERTEX;
			info.point.Set( point.x, point.y, point.z );
			info.normal = cachedNormal;
			info.dist = info.normal * info.point;
			info.entityNum = cached.entityNum;
			info.id = cached.id;
			contacts.Append( info );
			contactBodies.Append( i );
			enhancedSeparation.Append( separation );
			enhancedContactAge.Append( cached.age + 1 );
		}
	}

	AddContactEntitiesForContacts();

	return ( contacts.Num() != 0 );
}

/*
================
idPhysics_AF::SetupContactConstraintsEnhanced

  Legacy setup, then each contact's right-hand side becomes speculative: a gap
  may close within the step but not past the slop, and a new touch above the
  bounce threshold bounces by the body's bouncyness. (Legacy wrote the bounce
  with the opposite sign, which let a bouncy body keep closing instead.)
================
*/
void idPhysics_AF::SetupContactConstraintsEnhanced( float timeStep ) {
	SetupContactConstraints();

	const float invTimeStep = 1.0f / timeStep;
	const float slop = af_enhancedSlop.GetFloat();
	const float bounceThreshold = Max( 2.0f * SUSPEND_LINEAR_VELOCITY, 2.0f * gravityVector.Length() * timeStep );

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
		if ( impact && !matched && enhancedContactAge[i] == 0 && bouncyness > 0.0f ) {
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

	for ( i = 0; i < bodies.Num(); i++ ) {
		bodies[i]->enhancedPseudoVelocity.Zero();
	}

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
	bool anyPseudo = false;

	for ( k = 0, i = 0; i < auxiliaryConstraints.Num(); i++ ) {
		auxiliaryConstraints[i]->firstIndex = k;
		k += auxiliaryConstraints[i]->J1.GetNumRows();
	}

	const float slop = af_enhancedSlop.GetFloat();
	const float maxRecovery = af_enhancedMaxRecovery.GetFloat();
	const float warmStart = af_enhancedWarmStart.GetFloat();
	const int previousFrame = gameLocal.framenum - 1;

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
			if ( isLimit ) {
				pseudoTarget[k] = Min( Max( 0.0f, -c1 ), maxRecovery );
				c1 = Max( c1, 0.0f );
			} else if ( contactIndex >= 0 && contactIndex < enhancedSeparation.Num() && enhancedSeparation[contactIndex] < -slop ) {
				pseudoTarget[k] = Min( af_enhancedRecoveryRate.GetFloat() * ( -enhancedSeparation[contactIndex] - slop ) * invStep, maxRecovery );
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
			OpenQ4Physics::GetTelemetry().NoteCounter( OpenQ4Physics::COUNTER_SOLVER_FALLBACKS );
			if ( !lcp->Solve( jmk, lm, rhs, lo, hi, boxIndex ) ) {
				lm.Zero();
			}
		}
	}

	// position-only recovery: overlap and violated limits are removed by a pseudo
	// velocity integrated into the positions this step and then discarded
	if ( anyPseudo ) {
		idVecX pseudoRhs, pseudoLo, pseudoHi, pseudoLm;
		pseudoRhs.SetData( numAuxConstraints, VECX_ALLOCA( numAuxConstraints ) );
		pseudoLo.SetData( numAuxConstraints, VECX_ALLOCA( numAuxConstraints ) );
		pseudoHi.SetData( numAuxConstraints, VECX_ALLOCA( numAuxConstraints ) );
		pseudoLm.SetData( numAuxConstraints, VECX_ALLOCA( numAuxConstraints ) );
		for ( k = 0; k < numAuxConstraints; k++ ) {
			// J v_pseudo = dt * ( jmk lambda ) must reach the target speed
			pseudoRhs[k] = pseudoTarget[k] * invStep;
			pseudoLo[k] = 0.0f;
			pseudoHi[k] = pseudoTarget[k] > 0.0f ? idMath::INFINITY : 0.0f;
			pseudoLm[k] = 0.0f;
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
			body->enhancedPseudoVelocity = timeStep * velocity.SubVec6( 0 );
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

	// energy guard: constraint and contact resolution may not add kinetic energy
	{
		const float energyAfter = EnhancedKineticEnergy( true );
		const float scale = OpenQ4Physics::EnergyScale( energyAfter, energyBefore + work + 1e-3f * ( energyBefore + 1.0f ) );
		if ( scale < 1.0f ) {
			for ( int i = 0; i < bodies.Num(); i++ ) {
				bodies[i]->next->spatialVelocity *= scale;
			}
			OpenQ4Physics::GetTelemetry().NoteCounter( OpenQ4Physics::COUNTER_ENERGY_CLAMPS );
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
  than that reach is swept.
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

	for ( int i = 0; i < bodies.Num(); i++ ) {
		idAFBody *body = bodies[i];

		if ( body->clipMask != 0 && !fastEval ) {
			const float displacement = ( body->next->worldOrigin - body->current->worldOrigin ).Length();
			TransposeMultiply( body->current->worldAxis, body->next->worldAxis, axis );
			rotation = axis.ToRotation();
			const float turn = DEG2RAD( idMath::Fabs( rotation.GetAngle() ) ) * body->clipModel->GetBounds().GetRadius();
			const float reach = i < enhancedReach.Num() ? enhancedReach[i] : 0.0f;

			if ( displacement > reach - af_enhancedMargin.GetFloat() * 0.5f || turn > af_enhancedRotationSweep.GetFloat() ) {
				idEntity *passEntity = SetupCollisionForBody( body );
				rotation.SetOrigin( body->current->worldOrigin );
				OpenQ4Physics::GetTelemetry().NoteCounter( OpenQ4Physics::COUNTER_SWEPT );
				if ( gameLocal.Motion( self, collision, body->current->worldOrigin, body->next->worldOrigin, rotation,
										body->clipModel, body->current->worldAxis, body->clipMask, passEntity ) ) {
					body->next->worldOrigin = collision.endpos;
					body->next->worldAxis = collision.endAxis;
					const int index = collisions.Num();
					collisions.SetNum( index + 1, false );
					collisions[index].trace = collision;
					collisions[index].body = body;
				}
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
		DebugDraw();
		return false;
	}

	AddPushVelocity( -current.pushVelocity );

	const float energyBefore = EnhancedKineticEnergy( false );

	EvaluateContactsEnhanced( timeStep );
	SetupContactConstraintsEnhanced( timeStep );

	EvaluateConstraints( timeStep );

	// bounded joint error correction: legacy allows 256 units/s, which turns a
	// limb stuck in a wall into a figure that flies apart
	{
		const float maxCorrection = af_enhancedMaxJointCorrection.GetFloat();
		for ( int i = 0; i < primaryConstraints.Num(); i++ ) {
			primaryConstraints[i]->c1.Clamp( -maxCorrection, maxCorrection );
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
