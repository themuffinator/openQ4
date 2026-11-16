// openQ4 additions to the SDK game sources; see src/game/LICENSE.
//
// Physics measurement commands (PHYS-01), included by both game trees'
// gamesys/SysCmds.cpp. They only read state, so neither needs cheats:
//
//   physicsStats start [label]   begin a per-frame capture
//   physicsStats stop            end the capture and print PHYSICS_STATS
//   physicsStats report          print PHYSICS_STATS for the capture so far
//   physicsStats frames          print every captured frame's physics time
//   physicsQuality <prefix>      print PHYSICS_QUALITY: joint stretch, joints
//                                pierced by geometry and embedded bodies
//   physicsDigest [verbose]      print PHYSICS_DIGEST: a hash of every rigid
//                                body and articulated figure state, and a
//                                separate hash of the workload's own phys_*
//                                entities (map scripts and AI can make
//                                map-placed bodies differ between runs)

static void OpenQ4Physics_PrintLine( const char *line ) {
	gameLocal.Printf( "%s", line );
}

static void Cmd_PhysicsStats_f( const idCmdArgs &args ) {
	OpenQ4Physics::Telemetry &telemetry = OpenQ4Physics::GetTelemetry();
	const char *action = args.Argc() > 1 ? args.Argv( 1 ) : "report";
	const char *mode = gameLocal.enhancedPhysics ? "enhanced" : "legacy";

	if ( !idStr::Icmp( action, "start" ) ) {
		telemetry.Start( args.Argc() > 2 ? args.Argv( 2 ) : "capture" );
		gameLocal.Printf( "PHYSICS_STATS_START label=%s mode=%s time=%d\n", telemetry.Label(), mode, gameLocal.time );
	} else if ( !idStr::Icmp( action, "stop" ) ) {
		telemetry.Stop();
		telemetry.Report( OpenQ4Physics_PrintLine, mode );
	} else if ( !idStr::Icmp( action, "report" ) ) {
		telemetry.Report( OpenQ4Physics_PrintLine, mode );
	} else if ( !idStr::Icmp( action, "frames" ) ) {
		telemetry.ReportFrames( OpenQ4Physics_PrintLine, mode );
	} else {
		gameLocal.Printf( "usage: physicsStats start [label] | stop | report | frames\n" );
	}
}

static void OpenQ4Physics_DigestPhysics( OpenQ4Physics::StateDigest &digest, const idPhysics *physics ) {
	const int numClipModels = Max( 1, physics->GetNumClipModels() );
	for ( int id = 0; id < numClipModels; id++ ) {
		digest.AddFloats( physics->GetOrigin( id ).ToFloatPtr(), 3 );
		digest.AddFloats( physics->GetAxis( id ).ToFloatPtr(), 9 );
		digest.AddFloats( physics->GetLinearVelocity( id ).ToFloatPtr(), 3 );
		digest.AddFloats( physics->GetAngularVelocity( id ).ToFloatPtr(), 3 );
	}
	const int atRest = physics->IsAtRest() ? 1 : 0;
	digest.Add( &atRest, sizeof( atRest ) );
}

static bool OpenQ4Physics_IsSimulated( const idPhysics *physics ) {
	return physics && ( physics->IsType( idPhysics_RigidBody::GetClassType() ) || physics->IsType( idPhysics_AF::GetClassType() ) );
}

static void Cmd_PhysicsDigest_f( const idCmdArgs &args ) {
	const bool verbose = args.Argc() > 1 && !idStr::Icmp( args.Argv( 1 ), "verbose" );
	OpenQ4Physics::StateDigest simulated;
	OpenQ4Physics::StateDigest everything;
	OpenQ4Physics::StateDigest workload;

	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		idEntity *ent = gameLocal.entities[i];
		if ( !ent ) {
			continue;
		}
		const idPhysics *physics = ent->GetPhysics();
		if ( !physics ) {
			continue;
		}
		everything.Add( &i, sizeof( i ) );
		OpenQ4Physics_DigestPhysics( everything, physics );
		if ( OpenQ4Physics_IsSimulated( physics ) ) {
			OpenQ4Physics::StateDigest one;
			one.Add( &i, sizeof( i ) );
			OpenQ4Physics_DigestPhysics( one, physics );
			simulated.Add( &i, sizeof( i ) );
			OpenQ4Physics_DigestPhysics( simulated, physics );
			simulated.CountEntity();
			if ( !idStr::Cmpn( ent->name.c_str(), "phys_", 5 ) ) {
				workload.Add( &i, sizeof( i ) );
				OpenQ4Physics_DigestPhysics( workload, physics );
				workload.CountEntity();
			}
			if ( verbose ) {
				const unsigned long long hash = (unsigned long long)one.Hash();
				gameLocal.Printf( "PHYSICS_DIGEST_ENTITY %d %s %s %08x%08x origin=%s rest=%d\n", i, ent->name.c_str(), physics->GetClassname(),
					(unsigned int)( hash >> 32 ), (unsigned int)( hash & 0xffffffffu ), physics->GetOrigin().ToString( 2 ), physics->IsAtRest() ? 1 : 0 );
			}
		}
	}

	// client-side debris and corpses (rvClientMoveable, rvClientAFEntity)
	for ( int i = 0; i < MAX_CENTITIES; i++ ) {
		rvClientEntity *cent = gameLocal.clientEntities[i];
		if ( !cent ) {
			continue;
		}
		const idPhysics *physics = cent->GetPhysics();
		if ( !OpenQ4Physics_IsSimulated( physics ) ) {
			continue;
		}
		const int key = MAX_GENTITIES + i;
		simulated.Add( &key, sizeof( key ) );
		OpenQ4Physics_DigestPhysics( simulated, physics );
		simulated.CountEntity();
	}

	const unsigned long long hash = (unsigned long long)simulated.Hash();
	const unsigned long long all = (unsigned long long)everything.Hash();
	const unsigned long long own = (unsigned long long)workload.Hash();
	gameLocal.Printf( "PHYSICS_DIGEST time=%d mode=%s bodies=%d hash=%08x%08x all=%08x%08x workload=%08x%08x workloadBodies=%d\n",
		gameLocal.time, gameLocal.enhancedPhysics ? "enhanced" : "legacy", simulated.Count(),
		(unsigned int)( hash >> 32 ), (unsigned int)( hash & 0xffffffffu ),
		(unsigned int)( all >> 32 ), (unsigned int)( all & 0xffffffffu ),
		(unsigned int)( own >> 32 ), (unsigned int)( own & 0xffffffffu ), workload.Count() );
}

// physicsReport <namePrefix>: one PHYSICS_BODY line per matching entity with
// rigid-body or articulated physics: total mass, mass-weighted centre, linear
// momentum and the fastest body's speed. The envelope test (PHYS-04) compares
// these before and after a hit in both physics modes.
static void Cmd_PhysicsReport_f( const idCmdArgs &args ) {
	const char *prefix = args.Argc() > 1 ? args.Argv( 1 ) : "phys_";
	const int prefixLength = idStr::Cmp( prefix, "*" ) ? idStr::Length( prefix ) : 0;	// "*": every entity
	gameLocal.Printf( "PHYSICS_REPORT prefix=%s time=%d\n", prefix, gameLocal.time );
	if ( !idStr::Icmp( prefix, "@debris" ) ) {
		// client-side debris and corpses (brass, gibs, chunks): named by slot
		for ( int i = 0; i < MAX_CENTITIES; i++ ) {
			rvClientEntity *cent = gameLocal.clientEntities[i];
			const idPhysics *physics = cent ? cent->GetPhysics() : NULL;
			if ( !OpenQ4Physics_IsSimulated( physics ) ) {
				continue;
			}
			const float mass = physics->GetMass();
			gameLocal.Printf( "PHYSICS_BODY debris_%d class=%s bodies=1 mass=%.2f centre=%s momentum=%s peak=%.2f rest=%d time=%d\n",
				i, physics->GetClassname(), mass, physics->GetOrigin().ToString( 2 ), ( mass * physics->GetLinearVelocity() ).ToString( 1 ),
				physics->GetLinearVelocity().Length(), physics->IsAtRest() ? 1 : 0, gameLocal.time );
		}
		return;
	}
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		idEntity *ent = gameLocal.entities[i];
		if ( !ent || idStr::Cmpn( ent->name.c_str(), prefix, prefixLength ) ) {
			continue;
		}
		const idPhysics *physics = ent->GetPhysics();
		if ( !OpenQ4Physics_IsSimulated( physics ) ) {
			continue;
		}
		const int count = Max( 1, physics->GetNumClipModels() );
		float mass = 0.0f;
		float peak = 0.0f;
		idVec3 centre = vec3_zero;
		idVec3 momentum = vec3_zero;
		for ( int id = 0; id < count; id++ ) {
			const float m = physics->GetMass( id );
			mass += m;
			centre += m * physics->GetOrigin( id );
			momentum += m * physics->GetLinearVelocity( id );
			peak = Max( peak, physics->GetLinearVelocity( id ).Length() );
		}
		if ( mass > 0.0f ) {
			centre /= mass;
		}
		gameLocal.Printf( "PHYSICS_BODY %s class=%s bodies=%d mass=%.2f centre=%s momentum=%s peak=%.2f rest=%d time=%d\n",
			ent->name.c_str(), physics->GetClassname(), count, mass, centre.ToString( 2 ), momentum.ToString( 1 ), peak,
			physics->IsAtRest() ? 1 : 0, gameLocal.time );
	}
}

// True when a trace or contents test stopped at geometry that never moves by
// physics: the world, or an entity on static or scripted (mover) physics.
static bool OpenQ4Physics_IsStaticEntity( int entityNum ) {
	if ( entityNum == ENTITYNUM_WORLD ) {
		return true;
	}
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		return false;
	}
	const idEntity *hit = gameLocal.entities[entityNum];
	const idPhysics *hitPhysics = hit ? hit->GetPhysics() : NULL;
	return hitPhysics && ( hitPhysics->IsType( idPhysics_Static::GetClassType() ) ||
			hitPhysics->IsType( idPhysics_StaticMulti::GetClassType() ) || hitPhysics->IsType( idPhysics_Parametric::GetClassType() ) );
}

// static geometry between two points, ignoring a hit within `tolerance` units of the end
static bool OpenQ4Physics_StaticBetween( const idEntity *self, const idVec3 &start, const idVec3 &end, float tolerance ) {
	const float length = ( end - start ).Length();
	if ( length <= tolerance ) {
		return false;
	}
	trace_t tr;
	gameLocal.TracePoint( self, tr, start, end, MASK_SOLID, self );
	return tr.fraction < 1.0f && tr.fraction * length < length - tolerance && OpenQ4Physics_IsStaticEntity( tr.c.entityNum );
}

static bool OpenQ4Physics_PointEmbedded( const idEntity *self, const idVec3 &point ) {
	idEntity *touched = NULL;
	if ( !( gameLocal.Contents( self, point, NULL, mat3_identity, CONTENTS_SOLID, self, &touched ) & CONTENTS_SOLID ) ) {
		return false;
	}
	return !touched || OpenQ4Physics_IsStaticEntity( touched->entityNumber );
}

// Whether static geometry between a body's centre and one of its joints is a
// limb through a wall or floor. A joint often lies outside the boxes it joins:
// a strogg marine's neck joint is 10.6 units from the centre of a head whose box
// reaches 7.6 at most. A surface in that gap touches neither body, and a ridge
// of terrain that pokes a unit into it under a corpse lying face down is not a
// defect. Such a surface counts only when it separates the two bodies (the
// other body's centre is behind it) or reaches more than two units past the
// joint. A surface inside the body's own box always counts.
static bool OpenQ4Physics_JointPierced( const idEntity *self, const idAFBody *body, const idAFBody *other, const idVec3 &centre, const idVec3 &anchor ) {
	trace_t tr;
	gameLocal.TracePoint( self, tr, centre, anchor, MASK_SOLID, self );
	if ( tr.fraction >= 1.0f ) {
		return false;
	}
	idVec3 dir = anchor - centre;
	const float length = dir.Normalize();
	const float hit = tr.fraction * length;

	// where the line leaves the body's own trace model
	const idClipModel *clip = body->GetClipModel();
	const idTraceModel *trm = clip ? clip->GetTraceModel() : NULL;
	if ( trm ) {
		const idMat3 toLocal = clip->GetAxis().Transpose();
		const idVec3 localCentre = ( centre - clip->GetOrigin() ) * toLocal;
		const idVec3 localDir = dir * toLocal;
		float exit = idMath::INFINITY;
		for ( int i = 0; i < trm->numPolys; i++ ) {
			const float along = trm->polys[i].normal * localDir;
			if ( along > 1e-6f ) {
				exit = Min( exit, ( trm->polys[i].dist - trm->polys[i].normal * localCentre ) / along );
			}
		}
		if ( hit <= exit ) {
			return true;
		}
	} else {
		return true;
	}

	// in the gap between the body and the joint
	const float anchorBehind = -( tr.c.normal * ( anchor - tr.c.point ) );
	if ( anchorBehind > 2.0f ) {
		return true;
	}
	if ( other && other->GetClipModel() ) {
		const idVec3 otherCentre = other->GetClipModel()->GetAbsBounds().GetCenter();
		return tr.c.normal * ( otherCentre - tr.c.point ) < 0.0f;
	}
	return false;
}

// physicsQuality <namePrefix>: one PHYSICS_QUALITY line per matching entity with
// rigid-body or articulated physics. The defect repros (PHYS-01) read these in
// both modes:
//   stretch    largest distance between the two anchors of a joint
//   pierced    joints with static geometry between a body's centre and the
//              joint (a limb through a wall or floor); a hit within one unit
//              of the joint does not count, and a surface in the gap between
//              the body's box and the joint counts only when it separates the
//              two bodies or reaches more than two units past the joint
//   embedded   bodies whose centre is inside solid static geometry
//   awake      1 while the physics object is not at rest
//   speed      linear speed of the fastest body
//   hidden     with a second argument "eye": 1 when static geometry lies
//              between the local player's eyes and the object (a body that
//              ended up behind a wall it was fired at)
static void Cmd_PhysicsQuality_f( const idCmdArgs &args ) {
	const idPlayer *viewer = ( args.Argc() > 2 && !idStr::Icmp( args.Argv( 2 ), "eye" ) ) ? gameLocal.GetLocalPlayer() : NULL;
	const char *prefix = args.Argc() > 1 ? args.Argv( 1 ) : "phys_";
	const int prefixLength = idStr::Cmp( prefix, "*" ) ? idStr::Length( prefix ) : 0;	// "*": every entity
	gameLocal.Printf( "PHYSICS_QUALITY_REPORT prefix=%s time=%d mode=%s\n", prefix, gameLocal.time, gameLocal.enhancedPhysics ? "enhanced" : "legacy" );
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		idEntity *ent = gameLocal.entities[i];
		if ( !ent || idStr::Cmpn( ent->name.c_str(), prefix, prefixLength ) ) {
			continue;
		}
		idPhysics *physics = ent->GetPhysics();
		if ( !OpenQ4Physics_IsSimulated( physics ) ) {
			continue;
		}
		const int count = Max( 1, physics->GetNumClipModels() );
		float speed = 0.0f;
		float stretch = 0.0f;
		int pierced = 0;
		int embedded = 0;
		int joints = 0;
		idVec3 lowest = physics->GetOrigin();
		for ( int id = 0; id < count; id++ ) {
			speed = Max( speed, physics->GetLinearVelocity( id ).Length() );
			const idClipModel *clip = physics->GetClipModel( id );
			const idVec3 centre = clip ? clip->GetAbsBounds().GetCenter() : physics->GetOrigin( id );
			if ( centre.z < lowest.z ) {
				lowest = centre;
			}
			if ( OpenQ4Physics_PointEmbedded( ent, centre ) ) {
				embedded++;
			}
		}
		if ( physics->IsType( idPhysics_AF::GetClassType() ) ) {
			idPhysics_AF *af = static_cast<idPhysics_AF *>( physics );
			for ( int c = 0; c < af->GetNumConstraints(); c++ ) {
				idAFConstraint *constraint = af->GetConstraint( c );
				if ( !constraint || !constraint->GetBody1() ) {
					continue;
				}
				float separation;
				switch ( constraint->GetType() ) {
					case CONSTRAINT_BALLANDSOCKETJOINT:
						separation = static_cast<idAFConstraint_BallAndSocketJoint *>( constraint )->GetAnchorOffset().Length();
						break;
					case CONSTRAINT_UNIVERSALJOINT:
						separation = static_cast<idAFConstraint_UniversalJoint *>( constraint )->GetAnchorOffset().Length();
						break;
					case CONSTRAINT_HINGE:
						separation = static_cast<idAFConstraint_Hinge *>( constraint )->GetAnchorOffset().Length();
						break;
					default:
						continue;
				}
				joints++;
				stretch = Max( stretch, separation );
				idVec3 anchor;
				constraint->GetCenter( anchor );
				bool blocked = false;
				idAFBody *pair[2] = { constraint->GetBody1(), constraint->GetBody2() };
				for ( int b = 0; b < 2 && !blocked; b++ ) {
					if ( pair[b] && pair[b]->GetClipModel() ) {
						const idVec3 centre = pair[b]->GetClipModel()->GetAbsBounds().GetCenter();
						blocked = OpenQ4Physics_StaticBetween( ent, centre, anchor, 1.0f ) &&
									OpenQ4Physics_JointPierced( ent, pair[b], pair[1 - b], centre, anchor );
						if ( blocked ) {
							// which joint, and how far along the line from the body's centre to
							// the joint the surface is met from each end
							trace_t tr, back;
							gameLocal.TracePoint( ent, tr, centre, anchor, MASK_SOLID, ent );
							gameLocal.TracePoint( ent, back, anchor, centre, MASK_SOLID, ent );
							const float length = ( anchor - centre ).Length();
							gameLocal.Printf( "PHYSICS_PIERCED %s joint=%s body=%s length=%.2f fromCentre=%.2f fromJoint=%.2f normal=%s\n", ent->name.c_str(),
								constraint->GetName().c_str(), pair[b]->GetName().c_str(), length, tr.fraction * length, back.fraction * length,
								tr.c.normal.ToString( 2 ) );
						}
					}
				}
				if ( blocked ) {
					pierced++;
				}
			}
		}
		const idClipModel *firstClip = physics->GetClipModel( 0 );
		const idVec3 firstCentre = firstClip ? firstClip->GetAbsBounds().GetCenter() : physics->GetOrigin();
		const int hidden = ( viewer && OpenQ4Physics_StaticBetween( viewer, viewer->GetEyePosition(), firstCentre, 1.0f ) ) ? 1 : 0;
		gameLocal.Printf( "PHYSICS_QUALITY %s class=%s bodies=%d joints=%d stretch=%.3f pierced=%d embedded=%d awake=%d speed=%.2f origin=%s lowest=%s hidden=%d time=%d\n",
			ent->name.c_str(), physics->GetClassname(), count, joints, stretch, pierced, embedded, physics->IsAtRest() ? 0 : 1, speed,
			firstCentre.ToString( 2 ), lowest.ToString( 2 ), hidden, gameLocal.time );
	}
}

// Workload tools for the physics harness (cheats): they stage repeatable scenes
// without any player input.
//
//   physicsSpawn <entityDef> <count> <ring|line|grid|stack|wall> <distance> [height] [step] [key value]...
//       spawns entities named phys_<n> around or ahead of the local player
//       ("wall": <distance> short of the surface ahead, <step> degrees apart)
//   physicsLaunch <entityDef> <speed> [yaw] [pitch] [key value]...
//       spawns one entity ahead of the player's eyes and launches it
//   physicsRadiusDamage <damageDef> <forward> [up] [power]
//       radius damage and push at a point ahead of the local player
//   physicsImpulse <entity|prefix*> <x> <y> <z> [clipModelId]
//       applies an impulse at the centre of an entity's clip model

static int openq4PhysicsSpawnSerial = 0;

static bool OpenQ4Physics_WallAhead( const idEntity *pass, const idVec3 &start, const idVec3 &dir, float &distance, float &thickness, idVec3 &normal );

static void Cmd_PhysicsSpawn_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk( false ) ) {
		return;
	}
	if ( args.Argc() < 5 ) {
		gameLocal.Printf( "usage: physicsSpawn <entityDef> <count> <ring|line|grid|stack|wall> <distance> [height] [step]\n" );
		return;
	}
	const char *defName = args.Argv( 1 );
	if ( !gameLocal.FindEntityDef( defName, false ) ) {
		gameLocal.Printf( "entityDef '%s' not found\n", defName );
		return;
	}
	const int count = idMath::ClampInt( 1, 64, atoi( args.Argv( 2 ) ) );
	const char *layout = args.Argv( 3 );
	const float distance = atof( args.Argv( 4 ) );
	const float height = args.Argc() > 5 ? atof( args.Argv( 5 ) ) : 0.0f;
	const float step = args.Argc() > 6 ? atof( args.Argv( 6 ) ) : 48.0f;

	const float yaw = player->viewAngles.yaw;
	const idVec3 forward = idAngles( 0.0f, yaw, 0.0f ).ToForward();
	const idVec3 right = idAngles( 0.0f, yaw - 90.0f, 0.0f ).ToForward();
	const idVec3 up( 0.0f, 0.0f, 1.0f );
	const idVec3 base = player->GetPhysics()->GetOrigin();
	const int side = Max( 1, (int)idMath::Ceil( idMath::Sqrt( (float)count ) ) );

	for ( int i = 0; i < count; i++ ) {
		idVec3 origin;
		float facing = yaw + 180.0f;
		if ( !idStr::Icmp( layout, "ring" ) ) {
			const float angle = idMath::TWO_PI * (float)i / (float)count;
			origin = base + distance * ( idMath::Cos( angle ) * forward + idMath::Sin( angle ) * right ) + height * up;
			facing = RAD2DEG( idMath::ATan( base.y - origin.y, base.x - origin.x ) );
		} else if ( !idStr::Icmp( layout, "line" ) ) {
			origin = base + ( distance + step * (float)i ) * forward + height * up;
		} else if ( !idStr::Icmp( layout, "grid" ) ) {
			const int row = i / side;
			const int column = i % side;
			origin = base + ( distance + step * (float)row ) * forward + step * ( (float)column - 0.5f * (float)( side - 1 ) ) * right + height * up;
		} else if ( !idStr::Icmp( layout, "stack" ) ) {
			origin = base + distance * forward + ( height + step * (float)i ) * up;
		} else if ( !idStr::Icmp( layout, "wall" ) ) {
			// <distance> units short of the first static surface ahead, each body
			// <step> degrees of yaw from the next, its back to the wall
			const float turn = step * ( (float)i - 0.5f * (float)( count - 1 ) );
			const idVec3 toward = idAngles( 0.0f, yaw + turn, 0.0f ).ToForward();
			float wallDistance, thickness;
			idVec3 wallNormal;
			if ( !OpenQ4Physics_WallAhead( player, base + idVec3( 0.0f, 0.0f, Max( height, 24.0f ) ), toward, wallDistance, thickness, wallNormal ) ) {
				gameLocal.Printf( "PHYSICS_SPAWN_SKIPPED %s yaw=%.1f no wall\n", defName, turn );
				continue;
			}
			origin = base + ( wallDistance - distance ) * toward + height * up;
			facing = yaw + turn + 180.0f;
		} else {
			gameLocal.Printf( "unknown layout '%s'\n", layout );
			return;
		}
		// keep placements in the open: one behind a wall (seen from the player) or
		// inside another body is skipped, never moved, so no two workload bodies
		// start embedded in each other or in the world
		if ( idStr::Icmp( layout, "stack" ) ) {
			trace_t tr;
			const idVec3 start = base + idVec3( 0.0f, 0.0f, Max( height, 24.0f ) );
			idVec3 end = origin;
			end.z = start.z;
			gameLocal.TracePoint( player, tr, start, end, MASK_SOLID, player );
			// only the static world blocks: bodies this command spawned a moment ago
			// (or monsters walking past) are not walls
			bool blocked = tr.fraction < 1.0f;
			if ( blocked && tr.c.entityNum != ENTITYNUM_WORLD ) {
				const idEntity *hit = gameLocal.entities[tr.c.entityNum];
				const idPhysics *hitPhysics = hit ? hit->GetPhysics() : NULL;
				blocked = !hitPhysics || hitPhysics->IsType( idPhysics_Static::GetClassType() ) ||
							hitPhysics->IsType( idPhysics_StaticMulti::GetClassType() ) || hitPhysics->IsType( idPhysics_Parametric::GetClassType() );
			}
			if ( blocked ) {
				gameLocal.Printf( "PHYSICS_SPAWN_SKIPPED %s %s blocked\n", defName, origin.ToString( 1 ) );
				continue;
			}
		}
		idDict dict;
		const idStr name = va( "phys_%d", openq4PhysicsSpawnSerial++ );
		dict.Set( "classname", defName );
		dict.Set( "name", name.c_str() );
		dict.Set( "origin", origin.ToString() );
		dict.SetFloat( "angle", facing );
		// trailing "key value" pairs become spawn keys ("nodrop 1" lets a stack fall into place)
		for ( int arg = 7; arg + 1 < args.Argc(); arg += 2 ) {
			dict.Set( args.Argv( arg ), args.Argv( arg + 1 ) );
		}
		idEntity *ent = NULL;
		if ( gameLocal.SpawnEntityDef( dict, &ent ) && ent ) {
			gameLocal.Printf( "PHYSICS_SPAWN %s %s %s\n", name.c_str(), defName, origin.ToString( 1 ) );
		}
	}
}

// Where static geometry first blocks a line from `start` along `dir`, its
// surface normal there and how thick it is (sampled a unit at a time, up to
// 64 units; a surface with no solid brush behind it reads 1).
static bool OpenQ4Physics_WallAhead( const idEntity *pass, const idVec3 &start, const idVec3 &dir, float &distance, float &thickness, idVec3 &normal ) {
	const float range = 4096.0f;
	trace_t tr;
	gameLocal.TracePoint( pass, tr, start, start + range * dir, MASK_SOLID, pass );
	distance = tr.fraction * range;
	thickness = 0.0f;
	normal = tr.fraction < 1.0f ? tr.c.normal : -dir;
	if ( tr.fraction >= 1.0f || !OpenQ4Physics_IsStaticEntity( tr.c.entityNum ) ) {
		return false;
	}
	for ( thickness = 1.0f; thickness < 64.0f; thickness += 1.0f ) {
		if ( !OpenQ4Physics_PointEmbedded( pass, start + ( distance + thickness ) * dir ) ) {
			break;
		}
	}
	return true;
}

// physicsRest <namePrefix>: puts every simulated entity the prefix names to
// rest where it is. A map's own figures settle from the pose the map gave them
// when it starts, for a time that differs by compiler in either mode (Air
// Defense 1's nine: 131 moving steps in legacy on Linux, about 900 on Windows);
// a workload that is to measure only what it stages rests them first.
static void Cmd_PhysicsRest_f( const idCmdArgs &args ) {
	if ( !gameLocal.CheatsOk( false ) ) {
		return;
	}
	if ( args.Argc() < 2 ) {
		gameLocal.Printf( "usage: physicsRest <namePrefix>\n" );
		return;
	}
	const char *prefix = args.Argv( 1 );
	const int prefixLength = idStr::Length( prefix );
	int count = 0;
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		idEntity *ent = gameLocal.entities[i];
		if ( !ent || idStr::Cmpn( ent->name.c_str(), prefix, prefixLength ) || !OpenQ4Physics_IsSimulated( ent->GetPhysics() ) ) {
			continue;
		}
		if ( !ent->GetPhysics()->IsAtRest() ) {
			ent->GetPhysics()->PutToRest();
			count++;
		}
	}
	gameLocal.Printf( "PHYSICS_REST prefix=%s rested=%d time=%d\n", prefix, count, gameLocal.time );
}

// physicsWallProbe <yawFrom> <yawTo> <step> [pitch]: for each view yaw offset,
// prints how far the first static surface is from the player's eyes and how
// thick it is, so a tunnelling repro can be aimed at a thin wall. Reads only.
static void Cmd_PhysicsWallProbe_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( !player || args.Argc() < 4 ) {
		gameLocal.Printf( "usage: physicsWallProbe <yawFrom> <yawTo> <step> [pitch]\n" );
		return;
	}
	const float from = atof( args.Argv( 1 ) );
	const float to = atof( args.Argv( 2 ) );
	const float step = Max( 0.5f, (float)atof( args.Argv( 3 ) ) );
	const float pitch = args.Argc() > 4 ? atof( args.Argv( 4 ) ) : 0.0f;
	const idVec3 eye = player->GetEyePosition();
	for ( float yaw = from; yaw <= to; yaw += step ) {
		const idVec3 dir = idAngles( pitch, player->viewAngles.yaw + yaw, 0.0f ).ToForward();
		float distance, thickness;
		idVec3 normal;
		const bool wall = OpenQ4Physics_WallAhead( player, eye, dir, distance, thickness, normal );
		// incidence: 0 degrees is a line straight into the surface
		gameLocal.Printf( "PHYSICS_WALL yaw=%.1f pitch=%.1f static=%d distance=%.1f thickness=%.0f incidence=%.0f\n", yaw, pitch, wall ? 1 : 0, distance, thickness,
			RAD2DEG( idMath::ACos( idMath::ClampFloat( -1.0f, 1.0f, -( dir * normal ) ) ) ) );
	}
}

// physicsLaunch <entityDef> <speed> [yaw] [pitch] [key value]...: spawns one
// entity named phys_<n> 40 units ahead of the player's eyes and sends it off
// at <speed> units/s along the view direction turned by yaw and pitch degrees.
// Prints where static geometry first blocks that line and how thick it is, so a
// test can tell a body that stopped at the wall from one that ended beyond it.
static void Cmd_PhysicsLaunch_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk( false ) ) {
		return;
	}
	if ( args.Argc() < 3 ) {
		gameLocal.Printf( "usage: physicsLaunch <entityDef> <speed> [yaw] [pitch] [key value]...\n" );
		return;
	}
	const char *defName = args.Argv( 1 );
	if ( !gameLocal.FindEntityDef( defName, false ) ) {
		gameLocal.Printf( "entityDef '%s' not found\n", defName );
		return;
	}
	const float speed = atof( args.Argv( 2 ) );
	const float yaw = args.Argc() > 3 ? atof( args.Argv( 3 ) ) : 0.0f;
	const float pitch = args.Argc() > 4 ? atof( args.Argv( 4 ) ) : 0.0f;
	const idVec3 dir = idAngles( pitch, player->viewAngles.yaw + yaw, 0.0f ).ToForward();
	const idVec3 origin = player->GetEyePosition() + 40.0f * dir;

	float distance, thickness;
	idVec3 normal;
	const bool wall = OpenQ4Physics_WallAhead( player, origin, dir, distance, thickness, normal );

	idDict dict;
	const idStr name = va( "phys_%d", openq4PhysicsSpawnSerial++ );
	dict.Set( "classname", defName );
	dict.Set( "name", name.c_str() );
	dict.Set( "origin", origin.ToString() );
	dict.Set( "nodrop", "1" );
	for ( int arg = 5; arg + 1 < args.Argc(); arg += 2 ) {
		dict.Set( args.Argv( arg ), args.Argv( arg + 1 ) );
	}
	idEntity *ent = NULL;
	if ( !gameLocal.SpawnEntityDef( dict, &ent ) || !ent || !ent->GetPhysics() ) {
		gameLocal.Printf( "PHYSICS_LAUNCH_FAILED %s\n", defName );
		return;
	}
	ent->GetPhysics()->SetLinearVelocity( speed * dir );
	ent->GetPhysics()->Activate();
	gameLocal.Printf( "PHYSICS_LAUNCH %s %s origin=%s dir=%s speed=%.0f static=%d wall=%.1f thickness=%.0f normal=%s time=%d\n", name.c_str(), defName,
		origin.ToString( 2 ), dir.ToString( 4 ), speed, wall ? 1 : 0, distance, thickness, normal.ToString( 4 ), gameLocal.time );
}

static void Cmd_PhysicsRadiusDamage_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk( false ) ) {
		return;
	}
	if ( args.Argc() < 3 ) {
		gameLocal.Printf( "usage: physicsRadiusDamage <damageDef> <forward> [up] [power]\n" );
		return;
	}
	const char *damageDefName = args.Argv( 1 );
	if ( !gameLocal.FindEntityDefDict( damageDefName, false ) ) {
		gameLocal.Printf( "damage def '%s' not found\n", damageDefName );
		return;
	}
	const idVec3 forward = idAngles( 0.0f, player->viewAngles.yaw, 0.0f ).ToForward();
	const float up = args.Argc() > 3 ? atof( args.Argv( 3 ) ) : 0.0f;
	const float power = args.Argc() > 4 ? atof( args.Argv( 4 ) ) : 1.0f;
	const idVec3 origin = player->GetPhysics()->GetOrigin() + atof( args.Argv( 2 ) ) * forward + idVec3( 0.0f, 0.0f, up );
	gameLocal.RadiusDamage( origin, gameLocal.world, gameLocal.world, NULL, player, damageDefName, power );
	gameLocal.Printf( "PHYSICS_RADIUS_DAMAGE %s at %s time=%d\n", damageDefName, origin.ToString( 1 ), gameLocal.time );
}

// physicsDamage <namePrefix> <damageDef> [scale] [joint]: damages every entity whose
// name starts with the prefix, pushing away from the local player, through the
// normal Damage path (pain, death, death push and ragdoll start included)
static void Cmd_PhysicsDamage_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk( false ) ) {
		return;
	}
	if ( args.Argc() < 3 ) {
		gameLocal.Printf( "usage: physicsDamage <namePrefix> <damageDef> [scale] [joint]\n" );
		return;
	}
	const char *prefix = args.Argv( 1 );
	const char *damageDefName = args.Argv( 2 );
	if ( !gameLocal.FindEntityDefDict( damageDefName, false ) ) {
		gameLocal.Printf( "damage def '%s' not found\n", damageDefName );
		return;
	}
	const float scale = args.Argc() > 3 ? atof( args.Argv( 3 ) ) : 1.0f;
	const int prefixLength = idStr::Length( prefix );
	int count = 0;
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		idEntity *ent = gameLocal.entities[i];
		if ( !ent || ent == player || idStr::Cmpn( ent->name.c_str(), prefix, prefixLength ) ) {
			continue;
		}
		jointHandle_t joint = INVALID_JOINT;
		if ( args.Argc() > 4 && ent->GetAnimator() ) {
			joint = ent->GetAnimator()->GetJointHandle( args.Argv( 4 ) );
		}
		idVec3 dir = ent->GetPhysics()->GetAbsBounds().GetCenter() - player->GetPhysics()->GetOrigin();
		dir.z = Max( dir.z, 0.0f );
		dir.Normalize();
		ent->Damage( gameLocal.world, player, dir, damageDefName, scale, joint );
		count++;
	}
	gameLocal.Printf( "PHYSICS_DAMAGE %s %s scale=%.2f entities=%d time=%d\n", prefix, damageDefName, scale, count, gameLocal.time );
}

static void Cmd_PhysicsImpulse_f( const idCmdArgs &args ) {
	if ( !gameLocal.CheatsOk( false ) ) {
		return;
	}
	if ( args.Argc() < 5 ) {
		gameLocal.Printf( "usage: physicsImpulse <entity|prefix*> <x> <y> <z> [clipModelId]\n" );
		return;
	}
	// a name followed by '*' is a prefix: every simulated entity it matches takes the
	// impulse. The console splits "name*" into two arguments, so the star is its own.
	idStr name = args.Argv( 1 );
	bool byPrefix = name.Length() > 0 && name[name.Length() - 1] == '*';
	if ( byPrefix ) {
		name.CapLength( name.Length() - 1 );
	}
	int first = 2;
	if ( !idStr::Cmp( args.Argv( 2 ), "*" ) ) {
		byPrefix = true;
		first = 3;
	}
	if ( args.Argc() < first + 3 ) {
		gameLocal.Printf( "usage: physicsImpulse <entity|prefix*> <x> <y> <z> [clipModelId]\n" );
		return;
	}
	const idVec3 impulse( atof( args.Argv( first ) ), atof( args.Argv( first + 1 ) ), atof( args.Argv( first + 2 ) ) );
	const int wantedId = args.Argc() > first + 3 ? atoi( args.Argv( first + 3 ) ) : 0;
	int count = 0;
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		idEntity *ent = gameLocal.entities[i];
		if ( !ent || !ent->GetPhysics() ) {
			continue;
		}
		if ( byPrefix ? ( idStr::Cmpn( ent->name.c_str(), name.c_str(), name.Length() ) != 0 || !OpenQ4Physics_IsSimulated( ent->GetPhysics() ) )
					  : ( idStr::Icmp( ent->name.c_str(), name.c_str() ) != 0 ) ) {
			continue;
		}
		const int id = idMath::ClampInt( 0, Max( 1, ent->GetPhysics()->GetNumClipModels() ) - 1, wantedId );
		const idClipModel *clip = ent->GetPhysics()->GetClipModel( id );
		const idVec3 point = clip ? clip->GetAbsBounds().GetCenter() : ent->GetPhysics()->GetOrigin( id );
		ent->ApplyImpulse( gameLocal.world, id, point, impulse );
		gameLocal.Printf( "PHYSICS_IMPULSE %s id=%d impulse=%s time=%d\n", ent->name.c_str(), id, impulse.ToString( 1 ), gameLocal.time );
		count++;
		if ( !byPrefix ) {
			break;
		}
	}
	if ( !count ) {
		gameLocal.Printf( "entity not found\n" );
	}
}
