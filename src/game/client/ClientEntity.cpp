//----------------------------------------------------------------
// ClientEntity.cpp
//
// Copyright 2002-2004 Raven Software
//----------------------------------------------------------------




#include "../Game_local.h"

ABSTRACT_DECLARATION( idClass, rvClientEntity )
END_CLASS

/*
================
rvClientEntity::rvClientEntity
================
*/
rvClientEntity::rvClientEntity( void ) {
	// Restore constructs client entities without Spawn(). Their nodes join the
	// spawned and bind lists during load, and those lists are walked through
	// node owners: a NULL owner ends the per-frame Think loop and makes
	// idEntity::Save write fewer bound objects than its count.
	spawnNode.SetOwner( this );
	bindNode.SetOwner( this );
	presentationNode.SetOwner( this );

	bindMaster = NULL;
	entityNumber = -1;

	bindOrigin.Zero();
	bindAxis.Identity();
	bindJoint = INVALID_JOINT;
	bindOrientated = false;
	
	memset( &refSound, 0, sizeof(refSound) );
	refSound.referenceSoundHandle = -1;
}

/*
================
rvClientEntity::~rvClientEntity
================
*/
rvClientEntity::~rvClientEntity( void ) {
	Unbind();
	gameLocal.UnregisterClientEntity( this );

	// Free sound emitter
	soundSystem->FreeSoundEmitter( SOUNDWORLD_GAME, refSound.referenceSoundHandle, true );
	refSound.referenceSoundHandle = -1;
}

/*
================
rvClientEntity::Spawn
================
*/
void rvClientEntity::Spawn( void ) {
	idVec3	origin;
	idMat3	axis;

	gameLocal.RegisterClientEntity( this );

	spawnNode.SetOwner( this );
	bindNode.SetOwner( this );
	presentationNode.SetOwner( this );

	origin = spawnArgs.GetVector( "origin", "0 0 0" );
	axis = spawnArgs.GetMatrix( "axis", "1 0 0 0 1 0 0 0 1" );

	InitDefaultPhysics( origin, axis );

	SetOrigin( origin );
	SetAxis( axis );
}

/*
================
rvClientEntity::Present
================
*/
void rvClientEntity::Present ( void ) {
}

/*
================
rvClientEntity::FreeEntityDef
================
*/
void rvClientEntity::FreeEntityDef ( void ) {
}

/*
================
rvClientEntity::Think
================
*/
void rvClientEntity::Think ( void ) {
	UpdateBind();
	UpdateSound();
	Present();
}

/*
================
rvClientEntity::UpdatePresentationTransform

Presentation-only re-anchor.  The caller has already moved the bind master to
the interpolated pose it is about to be drawn at, so re-deriving the bind
transform here keeps attached client entities locked to what is actually on
screen instead of to the last authoritative tic.  Lifetime, sound and
simulation deliberately stay in Think() on the game clock.
================
*/
void rvClientEntity::UpdatePresentationTransform ( int presentationTime ) {
	if ( !bindMaster ) {
		return;
	}

	UpdateBind( presentationTime );
	Present();
}

/*
================
rvClientEntity::Bind
================
*/
void rvClientEntity::Bind ( idEntity* master, jointHandle_t joint, bool isOrientated ) {
	Unbind();

	if ( joint != INVALID_JOINT && !dynamic_cast<idAnimatedEntity*>(master) ) {
		gameLocal.Warning( "rvClientEntity::Bind: entity '%s' cannot support skeletal models.", master->GetName() );
		joint = INVALID_JOINT;
	}

	bindMaster = master;
	bindJoint  = joint;
	bindOrigin = worldOrigin;
	bindAxis   = worldAxis;

	bindNode.AddToEnd ( bindMaster->clientEntities );
	
	bindOrientated = isOrientated;
	if( physics ) {
		physics->SetMaster( bindMaster, bindOrientated );
	}

	UpdateBind();
}

/*
================
rvClientEntity::Bind
================
*/
void rvClientEntity::Unbind	( void ) {
	if ( !bindMaster ) {
		return;
	}

	bindMaster = NULL;
	bindNode.Remove ( );
}

/*
================
rvClientEntity::SetOrigin
================
*/
void rvClientEntity::SetOrigin( const idVec3& origin ) {
	if ( bindMaster ) {
		bindOrigin = origin;
	} else {
		worldOrigin = origin;
	}
}

/*
================
rvClientEntity::SetAxis
================
*/
void rvClientEntity::SetAxis( const idMat3& axis ) {
	if ( bindMaster ) {
		bindAxis = axis * bindMaster->GetRenderEntity()->axis.Transpose();		
	} else {
		worldAxis = axis;
	}
}

/*
================
rvClientEntity::UpdateBind
================
*/
void rvClientEntity::UpdateBind ( int presentationTime ) {
	if ( !bindMaster ) {
		return;
	}

	if ( bindJoint != INVALID_JOINT ) {
		idAnimatedEntity *animatedMaster = static_cast<idAnimatedEntity*>( bindMaster.GetEntity() );
		if ( presentationTime < 0 ||
			 !animatedMaster->GetPresentationJointWorldTransform( bindJoint, worldOrigin, worldAxis ) ) {
			animatedMaster->GetJointWorldTransform( bindJoint, gameLocal.time, worldOrigin, worldAxis );
		}
	} else {
		bindMaster->GetPosition( worldOrigin, worldAxis );
		//if ( !bindMaster->GetPhysicsToVisualTransform( worldOrigin, worldAxis ) ) {
		//	bindMaster->GetPosition( worldOrigin, worldAxis );
		//}
	}

	worldOrigin += (bindOrigin * worldAxis);
	worldAxis    = bindAxis * worldAxis;
}

/*
================
rvClientEntity::IsClient
================
*/
bool rvClientEntity::IsClient ( void ) const {
	return true;
}

/*
================
rvClientEntity::DrawDebugInfo
================
*/
void rvClientEntity::DrawDebugInfo ( void ) const {
	idBounds bounds ( idVec3(-8,-8,-8), idVec3(8,8,8) );
	
	gameRenderWorld->DebugBounds ( colorGreen, bounds, worldOrigin );

	if ( gameLocal.GetLocalPlayer() ) {	
		gameRenderWorld->DrawText ( GetClassname ( ), worldOrigin, 0.1f, colorWhite, gameLocal.GetLocalPlayer()->viewAngles.ToMat3(), 1 );
	}
}

/*
================
rvClientEntity::UpdateSound
================
*/
void rvClientEntity::UpdateSound( void ) {
	idSoundEmitter *emitter = soundSystem->EmitterForIndex( SOUNDWORLD_GAME, refSound.referenceSoundHandle );
	if ( emitter ) {
		refSound.origin = worldOrigin;
		refSound.velocity = vec3_origin;
		emitter->UpdateEmitter( refSound.origin, refSound.velocity, refSound.listenerId, &refSound.parms );
	}
}

/*
================
rvClientEntity::SetSoundVolume
================
*/
void rvClientEntity::SetSoundVolume( float volume ) {
	refSound.parms.volume = volume;
}


/*
================
rvClientEntity::StartSound
================
*/
bool rvClientEntity::StartSound( const char *soundName, const s_channelType channel, int soundShaderFlags, bool broadcast, int *length ) {
	const idSoundShader *shader;
	const char *sound;

	if ( length ) {
		*length = 0;
	}

	idStr soundNameStr = soundName;
	if( soundNameStr.CmpPrefix( "snd_" ) && soundNameStr.CmpPrefix( "lipsync_" ) ) {
		common->Warning( "Non precached sound \'%s\'", soundName );
	}

	if ( !spawnArgs.GetString( soundName, "", &sound ) ) {
		return false;
	}

	if ( *sound == '\0' ) {
		return false;
	}

	if ( !gameLocal.isNewFrame ) {
		// don't play the sound, but don't report an error
		return true;
	}

	shader = declManager->FindSound( sound );
	return StartSoundShader( shader, channel, soundShaderFlags );
}


/*
================
rvClientEntity::StartSoundShader
================
*/
int rvClientEntity::StartSoundShader ( const idSoundShader* shader, const s_channelType channel, int soundShaderFlags )  {
	if ( !shader ) {
		return 0;
	}
	
	idSoundEmitter *emitter = soundSystem->EmitterForIndex( SOUNDWORLD_GAME, refSound.referenceSoundHandle );
	if ( !emitter ) {
		refSound.referenceSoundHandle = soundSystem->AllocSoundEmitter( SOUNDWORLD_GAME );
	}

	UpdateSound();
	
	emitter = soundSystem->EmitterForIndex( SOUNDWORLD_GAME, refSound.referenceSoundHandle );
	if( !emitter ) { 
		return( 0 );
	}

	emitter->UpdateEmitter( refSound.origin, refSound.velocity, refSound.listenerId, &refSound.parms );
	return emitter->StartSound( shader, channel, gameLocal.random.RandomFloat(), soundShaderFlags  );
}

/*
================
rvClientEntity::Size

Returns Returns memory size of an rvClientEntity.
================
*/

size_t rvClientEntity::Size ( void ) const {
	return sizeof( rvClientEntity );
}

/*
================
rvClientEntity::Save
================
*/
void rvClientEntity::Save( idSaveGame *savefile ) const {
	savefile->WriteInt( entityNumber );

	// idLinkList<rvClientEntity>	spawnNode;		- reconstructed in the master entity load
	// idLinkList<rvClientEntity>	bindNode;		- reconstructed in the master entity load

	savefile->WriteVec3( worldOrigin );
	savefile->WriteVec3( worldVelocity );
	savefile->WriteMat3( worldAxis );

	bindMaster.Save( savefile );
	savefile->WriteVec3( bindOrigin );
	savefile->WriteMat3( bindAxis );
	savefile->WriteJoint( bindJoint );
	
	savefile->WriteRefSound( refSound );
}

/*
================
rvClientEntity::Restore
================
*/
void rvClientEntity::Restore( idRestoreGame *savefile ) {
	int restoredEntityNumber;
	savefile->ReadInt( restoredEntityNumber );
	if ( restoredEntityNumber < -1 || restoredEntityNumber >= MAX_CENTITIES ) {
		savefile->Error( "rvClientEntity::Restore: invalid client entity number %d", restoredEntityNumber );
	}
	// A failed restore must leave the master table's identity safe to unregister.
	if ( entityNumber != -1 && entityNumber != restoredEntityNumber ) {
		savefile->Error( "rvClientEntity::Restore: client entity number %d does not match registered number %d", restoredEntityNumber, entityNumber );
	}
	if ( entityNumber == -1 && restoredEntityNumber != -1 ) {
		savefile->Error( "rvClientEntity::Restore: client entity number %d is not registered in the client entity table", restoredEntityNumber );
	}
	entityNumber = restoredEntityNumber;

	// idLinkList<rvClientEntity>	spawnNode;		- reconstructed in the master entity load
	// idLinkList<rvClientEntity>	bindNode;		- reconstructed in the master entity load

	savefile->ReadVec3( worldOrigin );
	savefile->ReadVec3( worldVelocity );
	savefile->ReadMat3( worldAxis );

	bindMaster.Restore( savefile );
	savefile->ReadVec3( bindOrigin );
	savefile->ReadMat3( bindAxis );
	savefile->ReadJoint( bindJoint );
	
	savefile->ReadRefSound( refSound );
}

/*
================
rvClientEntity::ValidateRestoredBinding

Runs once every object is restored. A bound client entity must sit in its
master's list, which binding and teardown walk, and a joint binding needs an
animated master: UpdateBind casts the master to idAnimatedEntity.
================
*/
void rvClientEntity::ValidateRestoredBinding( idRestoreGame *savefile ) const {
	const idEntity *master = bindMaster.GetEntity();
	if ( master == NULL ) {
		if ( bindNode.InList() ) {
			savefile->Error( "rvClientEntity::Restore: unbound client entity %d is listed by an entity", entityNumber );
		}
		return;
	}
	if ( bindNode.ListHead() != &master->clientEntities ) {
		savefile->Error( "rvClientEntity::Restore: client entity %d is missing from the list of its master '%s'", entityNumber, master->GetName() );
	}
	if ( bindJoint != INVALID_JOINT && !master->IsType( idAnimatedEntity::GetClassType() ) ) {
		savefile->Error( "rvClientEntity::Restore: client entity %d is bound to a joint of non-animated '%s'", entityNumber, master->GetName() );
	}
}

/*
================
rvClientEntity::RunPhysics
================
*/
void rvClientEntity::RunPhysics ( void ) {
	idPhysics* physics = GetPhysics( );
	if( !physics ) {
		return;
	}

	rvClientPhysics* clientPhysics = (rvClientPhysics*)gameLocal.entities[ENTITYNUM_CLIENT];
	static_cast<rvClientPhysics*>( clientPhysics )->currentEntityNumber = entityNumber;

	// order important: 1) set client physics bind master to client ent's bind master
	//					2) set physics to client ent's physics, which sets physics 
	//					   master to client ent's master
	//					3) set client physics origin to client ent origin, depends on
	//					   proper bind master from 1
	clientPhysics->PushBindInfo( bindMaster, bindJoint, bindOrientated );
	clientPhysics->SetPhysics( physics );
	clientPhysics->PushOriginInfo( bindMaster ? bindOrigin : worldOrigin, bindMaster ? bindAxis : worldAxis );

	physics->Evaluate ( gameLocal.time - gameLocal.previousTime, gameLocal.time );

	worldOrigin = physics->GetOrigin();
	worldVelocity = physics->GetLinearVelocity();
	worldAxis = physics->GetAxis();

	// order important: 1) restore previous bind master
	//					2) reset physics with previous bind master
	//					3) reset origin with previous bind master
	clientPhysics->PopBindInfo();
	clientPhysics->SetPhysics( NULL );
	clientPhysics->PopOriginInfo();

	UpdateAnimationControllers();
}

/*
================
rvClientEntity::GetPhysics
================
*/
idPhysics* rvClientEntity::GetPhysics ( void ) const {
	return physics;
}

/*
================
rvClientEntity::Collide
================
*/
bool rvClientEntity::Collide ( const trace_t &collision, const idVec3 &velocity ) {
	return false;
}

/*
================
rvClientEntity::GetImpactInfo
================
*/
void rvClientEntity::GetImpactInfo( idEntity *ent, int id, const idVec3 &point, impactInfo_t *info ) {
	GetPhysics()->GetImpactInfo( id, point, info );
}

/*
================
rvClientEntity::ApplyImpulse
================
*/
void rvClientEntity::ApplyImpulse( idEntity *ent, int id, const idVec3 &point, const idVec3 &impulse, bool splash ) {
	GetPhysics()->ApplyImpulse( id, point, impulse );
}

/*
================
rvClientEntity::AddForce
================
*/
void rvClientEntity::AddForce( idEntity *ent, int id, const idVec3 &point, const idVec3 &force ) {
	GetPhysics()->AddForce( id, point, force );
}

/*
================
rvClientEntity::UpdateAnimationControllers
================
*/
bool rvClientEntity::UpdateAnimationControllers( void ) {
	return false;
}

/*
================
rvClientEntity::InitDefaultPhysics
================
*/
void rvClientEntity::InitDefaultPhysics( const idVec3 &origin, const idMat3 &axis ) {
	const char *temp;
	idClipModel *clipModel = NULL;

	// check if a clipmodel key/value pair is set
	if ( spawnArgs.GetString( "clipmodel", "", &temp ) ) {
		if ( temp != NULL && temp[ 0 ] == '\0' ) {
			temp = NULL;
		}
		if ( temp != NULL ) {
		// RAVEN BEGIN
		// mwhitlock: Dynamic memory consolidation
		RV_PUSH_HEAP_MEM(this);
		// RAVEN END
		clipModel = new idClipModel();
		if ( !clipModel->LoadModel( temp ) ) {
			delete clipModel;
			clipModel = NULL;
		}
		// RAVEN BEGIN
		// mwhitlock: Dynamic memory consolidation
		RV_POP_HEAP();
		// RAVEN END
		}
	}

	if ( !spawnArgs.GetBool( "noclipmodel", "0" ) ) {

		// check if mins/maxs or size key/value pairs are set
		if ( !clipModel ) {
			idVec3 size;
			idBounds bounds;
			bool setClipModel = false;

			if ( spawnArgs.GetVector( "mins", NULL, bounds[0] ) &&
				spawnArgs.GetVector( "maxs", NULL, bounds[1] ) ) {
					setClipModel = true;
					if ( bounds[0][0] > bounds[1][0] || bounds[0][1] > bounds[1][1] || bounds[0][2] > bounds[1][2] ) {
						gameLocal.Error( "Invalid bounds '%s'-'%s' on client entity '%d'", bounds[0].ToString(), bounds[1].ToString(), entityNumber );
					}
				} else if ( spawnArgs.GetVector( "size", NULL, size ) ) {
					if ( ( size.x < 0.0f ) || ( size.y < 0.0f ) || ( size.z < 0.0f ) ) {
						gameLocal.Error( "Invalid size '%s' on client entity '%d'", size.ToString(), entityNumber );
					}
					bounds[0].Set( size.x * -0.5f, size.y * -0.5f, 0.0f );
					bounds[1].Set( size.x * 0.5f, size.y * 0.5f, size.z );
					setClipModel = true;
				}

				if ( setClipModel ) {
					int numSides;
					idTraceModel trm;

					if ( spawnArgs.GetInt( "cylinder", "0", numSides ) && numSides > 0 ) {
						trm.SetupCylinder( bounds, numSides < 3 ? 3 : numSides );
					} else if ( spawnArgs.GetInt( "cone", "0", numSides ) && numSides > 0 ) {
						trm.SetupCone( bounds, numSides < 3 ? 3 : numSides );
						// RAVEN BEGIN
						// bdube: added dodecahedron
					} else if ( spawnArgs.GetInt( "dodecahedron", "0", numSides ) && numSides > 0 ) {
						trm.SetupDodecahedron ( bounds );
						// RAVEN END
					} else {
						trm.SetupBox( bounds );
					}
					// RAVEN BEGIN
					// mwhitlock: Dynamic memory consolidation
					RV_PUSH_HEAP_MEM(this);
					// RAVEN END
					clipModel = new idClipModel( trm );
					// RAVEN BEGIN
					// mwhitlock: Dynamic memory consolidation
					RV_POP_HEAP();
					// RAVEN END
				}
		}

		// check if the visual model can be used as collision model
		if ( !clipModel ) {
			temp = spawnArgs.GetString( "model" );
			if ( ( temp != NULL ) && ( *temp != 0 ) ) {
				// RAVEN BEGIN
				// jscott: slash problems
				idStr canonical = temp;
				canonical.BackSlashesToSlashes();
				// RAVEN END
				// RAVEN BEGIN
				// mwhitlock: Dynamic memory consolidation
				RV_PUSH_HEAP_MEM(this);
				// RAVEN END
				clipModel = new idClipModel();
				if ( !clipModel->LoadModel( canonical ) ) {
					delete clipModel;
					clipModel = NULL;
				}
				// RAVEN BEGIN
				// mwhitlock: Dynamic memory consolidation
				RV_POP_HEAP();
				// RAVEN END
			}
		}
	}

	idEntity *physicsOwner = gameLocal.entities[ENTITYNUM_CLIENT];
	if ( physicsOwner == NULL ) {
		physicsOwner = gameLocal.world;
	}
	if ( physicsOwner != NULL ) {
		defaultPhysicsObj.SetSelf( physicsOwner );
		defaultPhysicsObj.SetClipModel( clipModel, 1.0f );
	} else {
		delete clipModel;
		clipModel = NULL;
	}
	defaultPhysicsObj.SetOrigin( origin );
	defaultPhysicsObj.SetAxis( axis );

	physics = &defaultPhysicsObj;

	// by default no collision
	physics->SetContents( 0 );
}


/*
===============================================================================

rvClientPhysics

===============================================================================
*/

CLASS_DECLARATION( idEntity, rvClientPhysics )
END_CLASS

/*
=====================
rvClientPhysics::Spawn
=====================
*/
void rvClientPhysics::Spawn( void ) {
	pushedOrientated = false;
}

/*
=====================
rvClientPhysics::Collide
=====================
*/
bool rvClientPhysics::Collide( const trace_t &collision, const idVec3 &velocity ) {
	assert ( currentEntityNumber >= 0 && currentEntityNumber < MAX_CENTITIES );
	
	rvClientEntity* cent;
	cent = gameLocal.clientEntities [ currentEntityNumber ];
	if ( cent ) {
		return cent->Collide ( collision, velocity );
	}
	
	return false;
}

/*
=====================
rvClientPhysics::PushBindInfo
=====================
*/
void rvClientPhysics::PushBindInfo( idEntity* master, jointHandle_t joint, bool orientated ) {
	pushedBindJoint = joint;
	pushedBindMaster = master;
	pushedOrientated = fl.bindOrientated;

	bindMaster = master;
	bindJoint = joint;
	fl.bindOrientated = orientated;
}

/*
=====================
rvClientPhysics::PopBindInfo
=====================
*/
void rvClientPhysics::PopBindInfo( void ) {
	bindMaster = pushedBindMaster;
	bindJoint = pushedBindJoint;
	fl.bindOrientated = pushedOrientated;
}

/*
=====================
rvClientPhysics::PushOriginInfo
=====================
*/
void rvClientPhysics::PushOriginInfo( const idVec3& origin, const idMat3& axis ) {
	if( !GetPhysics() ) {
		return;
	}

	pushedOrigin = GetPhysics()->GetOrigin();
	pushedAxis = GetPhysics()->GetAxis();

	GetPhysics()->SetOrigin( origin );
	GetPhysics()->SetAxis( axis );
}

/*
=====================
rvClientPhysics::PopOriginInfo
=====================
*/
void rvClientPhysics::PopOriginInfo( void ) {
	if( !GetPhysics() ) {
		return;
	}

	GetPhysics()->SetOrigin( pushedOrigin );
	GetPhysics()->SetAxis( pushedAxis );
}
