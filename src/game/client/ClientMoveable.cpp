//----------------------------------------------------------------
// ClientMoveable.cpp
//
// Copyright 2002-2004 Raven Software
//----------------------------------------------------------------




#include "../Game_local.h"

/*
===============================================================================

rvClientMoveable

===============================================================================
*/
const idEventDef CL_FadeOut( "<fadeOut>", "d" );
const idEventDef CL_ClearDepthHack ( "<clearDepthHack>" );

static const float BOUNCE_SOUND_MIN_VELOCITY	= 100.0f;
static const float BOUNCE_SOUND_MAX_VELOCITY	= 200.0f;
static const int   BOUNCE_SOUND_DELAY			= 200;

CLASS_DECLARATION( rvClientEntity, rvClientMoveable )
	EVENT( CL_FadeOut,			rvClientMoveable::Event_FadeOut )
	EVENT( CL_ClearDepthHack,	rvClientMoveable::Event_ClearDepthHack )
END_CLASS


/*
================
rvClientMoveable::rvClientMoveable
================
*/
rvClientMoveable::rvClientMoveable ( void ) {
	memset ( &renderEntity, 0, sizeof(renderEntity) );
	entityDefHandle = -1;
	scale.Init( 0, 0, 1.0f, 1.0f );
	presentationPoseTime = -1;
	presentationPoseCanInterpolate = false;
	presentationPosePushed = false;
	presentationPrevOrigin.Zero();
	presentationPrevAxis.Identity();
	presentationCurOrigin.Zero();
	presentationCurAxis.Identity();
}

/*
================
rvClientMoveable::~rvClientMoveable
================
*/
rvClientMoveable::~rvClientMoveable ( void ) {
	FreeEntityDef ( );

	// Remove any trail effect if there is one
	if ( trailEffect ) {
		trailEffect->Stop ( );
	}
}

/*
================
rvClientMoveable::FreeEntityDef
================
*/
void rvClientMoveable::FreeEntityDef ( void ) {
	if ( entityDefHandle >= 0 ) {
		gameRenderWorld->FreeEntityDef ( entityDefHandle );
		entityDefHandle = -1;
	}	
}

idVec3 simpleTri[3] =
{
	idVec3( -1.0, -1.0, 0.0 ),
	idVec3( 0.0, 2.0, 0.0 ),
	idVec3( 2.0, 0.0, 0.0 )
};

/*
================
rvClientMoveable::Spawn
================
*/
void rvClientMoveable::Spawn ( void ) {
	// openQ4: a full client entity table refuses the registration, leaving
	// this entity without its spawn args and already queued for removal.
	// Spawning debris with no model used to raise a fatal "cannot load
	// collision model" error and end the level; drop the piece instead.
	// SpawnClientEntityDef reports the refusal to the caller.
	if ( entityNumber == -1 ) {
		return;
	}

	// parse static models the same way the editor display does
	gameEdit->ParseSpawnArgsToRenderEntity( &spawnArgs, &renderEntity );

	idTraceModel	trm;
	int				clipShrink;
	idStr			clipModelName;

	// check if a clip model is set
	spawnArgs.GetString( "clipmodel", "", clipModelName );
	if ( !clipModelName.Length ()  ) {
		clipModelName = spawnArgs.GetString( "model" );		// use the visual model
	}

	if ( clipModelName == SIMPLE_TRI_NAME ) {
		trm.SetupPolygon( simpleTri, 3 );
	} else {
		clipModelName.BackSlashesToSlashes();

		if ( !collisionModelManager->TrmFromModel( gameLocal.GetMapName(), clipModelName, trm ) ) {
			gameLocal.Error( "rvClientMoveable '%d': cannot load collision model %s", entityNumber, clipModelName.c_str() );
			return;
		}
	}

	// if the model should be shrunk
	clipShrink = spawnArgs.GetInt( "clipshrink" );
	if ( clipShrink != 0 ) {
		trm.Shrink( clipShrink * CM_CLIP_EPSILON );
	}

	physicsObj.SetSelf ( gameLocal.entities[ENTITYNUM_CLIENT] );		
	physicsObj.SetClipModel ( new idClipModel( trm ), spawnArgs.GetFloat ( "density", "0.5" ), entityNumber );

	physicsObj.SetOrigin( GetOrigin() );
	physicsObj.SetAxis( GetAxis() );
	physicsObj.SetBouncyness( spawnArgs.GetFloat( "bouncyness", "0.6" ) );
	physicsObj.SetFriction( spawnArgs.GetFloat("linear_friction", "0.6"), spawnArgs.GetFloat( "angular_friction", "0.6"), spawnArgs.GetFloat("friction", "0.05") );
	physicsObj.SetGravity( gameLocal.GetCurrentGravity(this) );
	physicsObj.SetContents( 0 );
	// abahr: changed to MASK_SHOT_RENDERMODEL because brass was getting pinched between the player and the wall in some cases
	//			may want to try something cheaper.
	physicsObj.SetClipMask( CONTENTS_OPAQUE ); // MASK_SHOT_RENDERMODEL | CONTENTS_CORPSE | CONTENTS_MOVEABLECLIP | CONTENTS_WATER );
	physicsObj.Activate ( );

	trailEffect = gameLocal.PlayEffect ( spawnArgs, "fx_trail", physicsObj.GetCenterMass(), GetAxis(), true );	
	trailAttenuateSpeed = spawnArgs.GetFloat ( "trailAttenuateSpeed", "200" );
	
	bounceSoundShader = declManager->FindSound ( spawnArgs.GetString ( "snd_bounce" ), false );
	bounceSoundTime   = 0;
	mPlayBounceSoundOnce = spawnArgs.GetBool("bounce_sound_once");
	mHasBounced = false;
	
	scale.Init( gameLocal.GetTime(), SEC2MS(spawnArgs.GetFloat("scale_reset_duration", "0.2")), Max(VECTOR_EPSILON, spawnArgs.GetFloat("scale", "1.0f")), 1.0f );
}

/*
================
rvClientMoveable::SetOrigin
================
*/
void rvClientMoveable::SetOrigin( const idVec3& origin ) {
	rvClientEntity::SetOrigin( origin );
	physicsObj.SetOrigin( origin );
}

/*
================
rvClientMoveable::SetAxis
================
*/
void rvClientMoveable::SetAxis( const idMat3& axis ) {
	rvClientEntity::SetAxis( axis );
	physicsObj.SetAxis( axis );
}

/*
================
rvClientMoveable::SetOwner
================
*/
void rvClientMoveable::SetOwner( idEntity* owner ) {
	physicsObj.GetClipModel()->SetOwner( owner );
	physicsObj.GetClipModel()->SetEntity( owner );
}

/*
================
rvClientMoveable::Think
================
*/
void rvClientMoveable::Think ( void ) {
	if( bindMaster && (bindMaster->GetRenderEntity()->hModel && bindMaster->GetModelDefHandle() == -1) ) {
		return;
	}

	RunPhysics ( );	

	// Special case the sound update to use the center mass since the origin may be in an odd place
	idSoundEmitter *emitter = soundSystem->EmitterForIndex( SOUNDWORLD_GAME, refSound.referenceSoundHandle );
	if ( emitter ) {
		refSound.origin = worldOrigin;
		refSound.velocity = worldVelocity;
		emitter->UpdateEmitter( refSound.origin, refSound.velocity, refSound.listenerId, &refSound.parms );
	}

	// Keep the trail effect following
	if ( trailEffect ) {
		float speed;
		speed = idMath::ClampFloat ( 0, trailAttenuateSpeed, worldVelocity.LengthFast ( ) );
		if ( physicsObj.IsAtRest ( ) ) {
			trailEffect->Stop ( );
			trailEffect = NULL;
		} else {
			trailEffect->SetOrigin ( worldOrigin );
			trailEffect->SetAxis ( worldAxis );
			trailEffect->Attenuate ( speed / trailAttenuateSpeed );
		}
	}

	renderEntity.origin = worldOrigin;
	renderEntity.axis = worldAxis * scale.GetCurrentValue( gameLocal.GetTime() );

	// add to refresh list
	if ( entityDefHandle == -1 ) {
		entityDefHandle = gameRenderWorld->AddEntityDef( &renderEntity );
	} else {
		gameRenderWorld->UpdateEntityDef( entityDefHandle, &renderEntity );
	}		
	presentationPosePushed = false;
	SamplePresentationPose();
}

static const float CLIENT_PRESENTATION_MAX_STEP			= 32.0f;
static const float CLIENT_PRESENTATION_STEP_TOLERANCE	= 4.0f;
static const float CLIENT_PRESENTATION_MAX_ANGLE		= 90.0f;

/*
================
rvClientMoveable::SamplePresentationPose

Debris, brass and gibs run their own rigid-body physics once per tic, so at
high refresh rates they stepped at 60 Hz under an interpolated camera. Take
this tic's authoritative sample after Think() has pushed it; draw frames then
present the previous-to-current pose on the camera's clock. Physics, clip
models, bounce sounds and lifetime stay in Think().
================
*/
void rvClientMoveable::SamplePresentationPose( void ) {
	if ( bindMaster || entityDefHandle == -1 || !g_presentationInterpolation.GetBool() ) {
		presentationPoseTime = -1;
		presentationPoseCanInterpolate = false;
		presentationNode.Remove();
		return;
	}

	const idVec3 simOrigin = worldOrigin;
	const idMat3 simAxis = worldAxis;
	if ( presentationPoseTime < 0 ) {
		presentationPoseTime = gameLocal.time;
		presentationPoseCanInterpolate = false;
		presentationPrevOrigin = simOrigin;
		presentationPrevAxis = simAxis;
		presentationCurOrigin = simOrigin;
		presentationCurAxis = simAxis;
		presentationNode.Remove();
		return;
	}
	if ( presentationPoseTime == gameLocal.time ) {
		return;
	}

	const int deltaTime = gameLocal.time - presentationPoseTime;
	const int maxSequentialDelta = static_cast<int>( idMath::Ceil( common->GetUserCmdMsecFloat() ) );
	const bool sequentialFrame = deltaTime > 0 && deltaTime <= maxSequentialDelta;
	const idVec3 lastStep = presentationCurOrigin - presentationPrevOrigin;
	const idVec3 originDelta = simOrigin - presentationCurOrigin;
	idAngles angleDelta = simAxis.ToAngles() - presentationCurAxis.ToAngles();
	angleDelta.Normalize180();
	const float maxStep = CLIENT_PRESENTATION_MAX_STEP + lastStep.Length() * CLIENT_PRESENTATION_STEP_TOLERANCE;
	const bool continuousPose =
		originDelta.LengthSqr() <= Square( maxStep ) &&
		angleDelta.Length() <= CLIENT_PRESENTATION_MAX_ANGLE;

	presentationPrevOrigin = presentationCurOrigin;
	presentationPrevAxis = presentationCurAxis;
	presentationCurOrigin = simOrigin;
	presentationCurAxis = simAxis;
	presentationPoseTime = gameLocal.time;
	const bool moved = presentationCurOrigin != presentationPrevOrigin || presentationCurAxis != presentationPrevAxis;
	presentationPoseCanInterpolate =
		gameLocal.GetMHz() == common->GetUserCmdHz() &&
		sequentialFrame &&
		continuousPose &&
		moved;

	if ( presentationPoseCanInterpolate ) {
		if ( !presentationNode.InList() ) {
			presentationNode.AddToEnd( gameLocal.presentationClientEntities );
		}
	} else {
		presentationPrevOrigin = presentationCurOrigin;
		presentationPrevAxis = presentationCurAxis;
		presentationNode.Remove();
	}
}

/*
================
rvClientMoveable::UpdatePresentationPose

Presentation-only. Re-push a copy of the render entity, and any trail effect,
at the camera's interpolated time. Never creates a def: a debris piece that
has not been added yet is drawn by its next Think().
================
*/
void rvClientMoveable::UpdatePresentationPose( void ) {
	if ( entityDefHandle == -1 || !presentationPoseCanInterpolate || presentationPoseTime != gameLocal.time ) {
		return;
	}

	const float fraction = gameLocal.GetPresentationInterpolationFraction();
	idVec3 origin;
	origin.Lerp( presentationPrevOrigin, presentationCurOrigin, fraction );
	const idMat3 axis = gameLocal.InterpolatePresentationAxis( presentationPrevAxis, presentationCurAxis, fraction );

	renderEntity_t presentationRenderEntity = renderEntity;
	presentationRenderEntity.origin = origin;
	presentationRenderEntity.axis = axis * scale.GetCurrentValue( gameLocal.GetTime() );
	gameRenderWorld->UpdateEntityDef( entityDefHandle, &presentationRenderEntity );
	if ( trailEffect ) {
		trailEffect->PresentAt( origin, axis );
	}
	presentationPosePushed = true;
}

/*
================
rvClientMoveable::RestoreAuthoritativePresentationPose

Hand the renderer back this tic's pose when the scene leaves the
presentation clock between Think()s (a cinematic or private camera starts).
================
*/
void rvClientMoveable::RestoreAuthoritativePresentationPose( void ) {
	if ( !presentationPosePushed ) {
		return;
	}
	presentationPosePushed = false;
	if ( entityDefHandle != -1 ) {
		gameRenderWorld->UpdateEntityDef( entityDefHandle, &renderEntity );
	}
	if ( trailEffect ) {
		trailEffect->PresentAt( worldOrigin, worldAxis );
	}
}

/*
================
rvClientMoveable::GetPhysics
================
*/
idPhysics* rvClientMoveable::GetPhysics ( void ) const {
	return (idPhysics*)&physicsObj;
}

/*
================
rvClientMoveable::Collide
================
*/
bool rvClientMoveable::Collide ( const trace_t &collision, const idVec3 &velocity ) {	
	if (mPlayBounceSoundOnce && mHasBounced)
	{
		return false;
	}
	if ( bounceSoundShader && gameLocal.time > bounceSoundTime ) {
		float speed;
		speed = velocity.LengthFast ( );
		if ( speed > BOUNCE_SOUND_MIN_VELOCITY ) {
			StartSoundShader ( bounceSoundShader, SND_CHANNEL_BODY, 0 );
			bounceSoundTime = BOUNCE_SOUND_DELAY;
			mHasBounced = true;
		}
	}	
		
	return false;
}

/*
================
rvClientMoveable::Save
================
*/
void rvClientMoveable::Save( idSaveGame *savefile ) const {
	savefile->WriteRenderEntity( renderEntity );
	savefile->WriteInt( entityDefHandle );

	trailEffect.Save( savefile );
	savefile->WriteFloat( trailAttenuateSpeed );
		
	savefile->WriteStaticObject( physicsObj );
	
	savefile->WriteInt( bounceSoundTime );
	savefile->WriteSoundShader( bounceSoundShader );

	savefile->WriteBool(mPlayBounceSoundOnce);
	savefile->WriteBool(mHasBounced);

	// TOSAVE: idInterpolate<float>	scale;
}

/*
================
rvClientMoveable::Restore
================
*/
void rvClientMoveable::Restore( idRestoreGame *savefile ) {
	savefile->ReadRenderEntity( renderEntity, NULL );
	// Saved handles only describe presence; cleanup must own current renderer IDs.
	int savedEntityDefHandle;
	savefile->ReadInt( savedEntityDefHandle );

	trailEffect.Restore( savefile );
	savefile->ReadFloat( trailAttenuateSpeed );
		
	savefile->ReadPhysicsObject( physicsObj, gameLocal.entities[ENTITYNUM_CLIENT] );
	
	savefile->ReadInt( bounceSoundTime );
	savefile->ReadSoundShader( bounceSoundShader );

	savefile->ReadBool(mPlayBounceSoundOnce);
	savefile->ReadBool(mHasBounced);

	// restore must retrieve entityDefHandle from the renderer
	if ( savedEntityDefHandle != -1 ) {
 		entityDefHandle = gameRenderWorld->AddEntityDef( &renderEntity );
 	}

	// TORESTORE: idInterpolate<float>	scale;
}

/*
================
rvClientMoveable::Event_FadeOut
================
*/
void rvClientMoveable::Event_FadeOut ( int duration ) {
	renderEntity.noShadow = !G_ShadowMapCorpseShadowsEnabled();
	renderEntity.shaderParms[ SHADERPARM_TIME_OF_DEATH ] = gameLocal.time * 0.001f;
	PostEventMS ( &EV_Remove, duration );	
}

/*
================
rvClientMoveable::Event_ClearDepthHack
================
*/
void rvClientMoveable::Event_ClearDepthHack ( void ) {
	renderEntity.weaponDepthHackInViewID = 0;
}

/*
================
rvClientMoveable::SpawnClientMoveables
================
*/
void rvClientMoveable::SpawnClientMoveables( idEntity* ent, const char *type, idList<rvClientMoveable *>* list ) {
	const idKeyValue *kv;
	idVec3 origin;
	idMat3 axis;
	
	if( list == NULL || type == NULL ) {
		return;
	}

	// drop all items
	kv = ent->spawnArgs.MatchPrefix( va( "def_%s", type ), NULL );
	while ( kv ) {
		origin = ent->GetPhysics()->GetOrigin();
		axis = ent->GetPhysics()->GetAxis();

// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if( ent->IsType( idAnimatedEntity::GetClassType() ) ) {
// RAVEN END
			idAnimatedEntity* animEnt = static_cast<idAnimatedEntity*>(ent);
			jointHandle_t clientMoveableJoint;
	
			const char* clientMoveableJointName = ent->spawnArgs.GetString( va( "%s_joint", kv->GetKey().c_str() + 4 ) );
		
			// use a joint if specified
			if ( idStr::Icmp( clientMoveableJointName, "") ) {
				clientMoveableJoint = animEnt->GetAnimator()->GetJointHandle( clientMoveableJointName );

				if ( !animEnt->GetJointWorldTransform( clientMoveableJoint, gameLocal.time, origin, axis ) ) {
					gameLocal.Warning( "%s refers to invalid joint '%s' on entity '%s'\n", va( "%s_joint", kv->GetKey().c_str() + 4 ), clientMoveableJointName, ent->name.c_str() );
					origin = ent->GetPhysics()->GetOrigin();
					axis = ent->GetPhysics()->GetAxis();
				}
			} 
		} 

		// spawn the entity
		const idDict* entityDef = gameLocal.FindEntityDefDict ( kv->GetValue().c_str(), false );
	
		if ( entityDef == NULL ) {
			gameLocal.Warning( "%s refers to invalid entity def '%s' on entity '%s'\n", kv->GetKey().c_str(), kv->GetValue().c_str(), ent->name.c_str() );
			break;
		}
	
		rvClientMoveable* newModel = NULL;
		// force spawnclass to rvClientMoveable
		gameLocal.SpawnClientEntityDef( *entityDef, (rvClientEntity**)(&newModel), false, "rvClientMoveable" );

		if( !newModel ) {
			gameLocal.Warning( "error spawning client moveable (invalid entity def '%s' on entity '%s')\n", kv->GetValue().c_str(), ent->name.c_str() );
			break;
		}
		newModel->SetOrigin ( origin );
		newModel->SetAxis( axis );

		list->Append( newModel );
		kv = ent->spawnArgs.MatchPrefix( va( "def_%s", type ), kv );
	}
}

