// openQ4 additions to the SDK game sources; see src/game/LICENSE.
//
// Physics measurement commands (PHYS-01), included by both game trees'
// gamesys/SysCmds.cpp. They only read state, so neither needs cheats:
//
//   physicsStats start [label]   begin a per-frame capture
//   physicsStats stop            end the capture and print PHYSICS_STATS
//   physicsStats report          print PHYSICS_STATS for the capture so far
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
	} else {
		gameLocal.Printf( "usage: physicsStats start [label] | stop | report\n" );
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
	const int prefixLength = idStr::Length( prefix );
	gameLocal.Printf( "PHYSICS_REPORT prefix=%s time=%d\n", prefix, gameLocal.time );
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

// Workload tools for the physics harness (cheats): they stage repeatable scenes
// without any player input.
//
//   physicsSpawn <entityDef> <count> <ring|line|grid|stack> <distance> [height] [step] [key value]...
//       spawns entities named phys_<n> around or ahead of the local player
//   physicsRadiusDamage <damageDef> <forward> [up] [power]
//       radius damage and push at a point ahead of the local player
//   physicsImpulse <entity> <x> <y> <z> [clipModelId]
//       applies an impulse at the centre of an entity's clip model

static int openq4PhysicsSpawnSerial = 0;

static void Cmd_PhysicsSpawn_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk( false ) ) {
		return;
	}
	if ( args.Argc() < 5 ) {
		gameLocal.Printf( "usage: physicsSpawn <entityDef> <count> <ring|line|grid|stack> <distance> [height] [step]\n" );
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
		gameLocal.Printf( "usage: physicsImpulse <entity> <x> <y> <z> [clipModelId]\n" );
		return;
	}
	idEntity *ent = gameLocal.FindEntity( args.Argv( 1 ) );
	if ( !ent || !ent->GetPhysics() ) {
		gameLocal.Printf( "entity not found\n" );
		return;
	}
	const idVec3 impulse( atof( args.Argv( 2 ) ), atof( args.Argv( 3 ) ), atof( args.Argv( 4 ) ) );
	const int id = args.Argc() > 5 ? atoi( args.Argv( 5 ) ) : 0;
	const idClipModel *clip = ent->GetPhysics()->GetClipModel( id );
	const idVec3 point = clip ? clip->GetAbsBounds().GetCenter() : ent->GetPhysics()->GetOrigin( id );
	ent->ApplyImpulse( gameLocal.world, id, point, impulse );
	gameLocal.Printf( "PHYSICS_IMPULSE %s id=%d impulse=%s time=%d\n", ent->name.c_str(), id, impulse.ToString( 1 ), gameLocal.time );
}
