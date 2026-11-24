//----------------------------------------------------------------
// ClientEffect.cpp
//
// Copyright 2002-2004 Raven Software
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "ClientEffect.h"

/*
===============================================================================

rvClientEffect

===============================================================================
*/

CLASS_DECLARATION( rvClientEntity, rvClientEffect )
END_CLASS

/*
================
rvClientEffect::rvClientEffect
================
*/
rvClientEffect::rvClientEffect ( void ) {
	Init( NULL );
	Spawn();
}

rvClientEffect::rvClientEffect ( const idDecl *effect ) {
	Init( effect );
	Spawn();
}

void rvClientEffect::Init ( const idDecl *effect ) {
	memset( &renderEffect, 0, sizeof( renderEffect ) );
	
	renderEffect.declEffect	= effect;
	renderEffect.startTime	= -1.0f;
	renderEffect.inConnectedArea = true;
	renderEffect.referenceSoundHandle = -1;
	renderEffect.shaderParms[SHADERPARM_RED] = 1.0f;
	renderEffect.shaderParms[SHADERPARM_GREEN] = 1.0f;
	renderEffect.shaderParms[SHADERPARM_BLUE] = 1.0f;
	renderEffect.shaderParms[SHADERPARM_ALPHA] = 1.0f;
	renderEffect.shaderParms[SHADERPARM_BRIGHTNESS] = 1.0f;
	renderEffect.shaderParms[SHADERPARM_TIMEOFFSET] = MS2SEC( gameLocal.time );
	effectDefHandle = -1;
	endOriginJoint	= INVALID_JOINT;
}

/*
================
rvClientEffect::~rvClientEffect
================
*/
rvClientEffect::~rvClientEffect( void ) {
	FreeEffectDef( );
	// The client effect allocates the reference sound emitter and keeps it across effect
	// restarts, so it frees it here; BSE never does.
	soundSystem->FreeSoundEmitter( SOUNDWORLD_GAME, renderEffect.referenceSoundHandle, true );
	renderEffect.referenceSoundHandle = -1;
}

/*
================
rvClientEffect::GetEffectIndex
================
*/
int rvClientEffect::GetEffectIndex( void ) 
{ 
	if( renderEffect.declEffect ) {
		return( renderEffect.declEffect->Index() ); 
	}
	return( -1 );
}
	
/*
================
rvClientEffect::GetEffectName
================
*/	
const char *rvClientEffect::GetEffectName( void ) 
{ 
	if( renderEffect.declEffect ) {
		return( renderEffect.declEffect->GetName() ); 
	}
	return( "unknown" );
}

/*
================
rvClientEffect::FreeEffectDef
================
*/
void rvClientEffect::FreeEffectDef ( void ) {
	if ( effectDefHandle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEffectDef( effectDefHandle );
	}
	effectDefHandle = -1;
}

/*
================
rvClientEffect::UpdateBind
================
*/
void rvClientEffect::UpdateBind ( int presentationTime ) {
	rvClientEntity::UpdateBind ( presentationTime );

	renderEffect.origin = worldOrigin;
	
	if ( endOriginJoint != INVALID_JOINT && bindMaster ) {
		idMat3 axis;
		idVec3 endOrigin;		
		idVec3 dir;

		idAnimatedEntity *animatedMaster = static_cast<idAnimatedEntity*>( bindMaster.GetEntity() );
		if ( presentationTime < 0 ||
			 !animatedMaster->GetPresentationJointWorldTransform( endOriginJoint, endOrigin, axis ) ) {
			animatedMaster->GetJointWorldTransform( endOriginJoint, gameLocal.time, endOrigin, axis );
		}
		SetEndOrigin ( endOrigin );
		
		dir = (endOrigin - worldOrigin);
		dir.Normalize ();		
		renderEffect.axis = dir.ToMat3 ( );
	} else {
		renderEffect.axis = worldAxis;
	}
	
	if ( bindMaster ) {
		renderEffect.groupID = bindMaster->entityNumber + 1;
	} else {
		renderEffect.groupID = 0;
	}
}

/*
================
rvClientEffect::Think
================
*/
void rvClientEffect::Think ( void ) {
	// If we are bound to an entity that is now hidden we can just not render if looping, otherwise stop the effect
	if( bindMaster && (bindMaster->GetRenderEntity()->hModel && bindMaster->GetModelDefHandle() == -1) ) {
		if ( renderEffect.loop ) {		
			return;
		}
		Stop ( );
	}

// RAVEN BEGIN
// jnewquist: Tag scope and callees to track allocations using "new".
	MEM_SCOPED_TAG(tag,MA_EFFECT);
// RAVEN END
	// If there is a valid effect handle and we havent started playing
	// and effect yet then see if its time
	if( effectDefHandle < 0 && renderEffect.declEffect ) {
		if( renderEffect.startTime >= 0.0f ) {
			// Make sure our origins are all straight before starting the effect
			UpdateBind();
			renderEffect.attenuation = 1.0f;
			
			// if the rendereffect needs sound give it an emitter.
			if( renderEffect.referenceSoundHandle <= 0 )	{ 
				if( gameRenderWorld->EffectDefHasSound( &renderEffect) )
				{
					renderEffect.referenceSoundHandle = soundSystem->AllocSoundEmitter( SOUNDWORLD_GAME );
				} else {
					renderEffect.referenceSoundHandle = -1;
				}
			}
			// Add the render effect
			effectDefHandle = gameRenderWorld->AddEffectDef( &renderEffect, gameLocal.time );
			if ( effectDefHandle < 0 ) {
				soundSystem->FreeSoundEmitter( SOUNDWORLD_GAME, renderEffect.referenceSoundHandle, true );
				renderEffect.referenceSoundHandle = -1;
				PostEventMS( &EV_Remove, 0 );
			}
		}
		
		return;
	} 

	// If we lost our effect def handle then just remove ourself
	if( effectDefHandle < 0 ) {
		PostEventMS ( &EV_Remove, 0 );
		return;
	}

	// Dont do anything else if its not a new client frame
	if( !gameLocal.isNewFrame ) {
		return;
	}

	// Check to see if the player can possibly see the effect or not
	renderEffect.inConnectedArea = true;
	if( bindMaster ) {
		renderEffect.inConnectedArea = gameLocal.InPlayerConnectedArea( bindMaster );
	}

	// Update the bind	
	UpdateBind();

	// Update the actual render effect now
	if( gameRenderWorld->UpdateEffectDef( effectDefHandle, &renderEffect, gameLocal.time ) ) {
		FreeEffectDef ( );
		PostEventMS( &EV_Remove, 0 );
		return;
	}
}

/*
================
rvClientEffect::UpdatePresentationTransform

rvClientEntity::Present() does nothing for effects, so push the re-anchored
transform straight at the effect def.  Unbound effects are accepted too:
UpdateBind() still copies worldOrigin into the render effect for them, which is
what lets a weapon reposition a beam it drives from an explicit origin.

gameLocal.time is deliberate: BSE only spawns and ages when the def's game time
advances past its service time, so repeating the authoritative time moves the
effect without respawning or ageing particles a second time.  The completion
result is ignored for the same reason -- Think() owns the effect's lifetime.
================
*/
void rvClientEffect::UpdatePresentationTransform ( int presentationTime ) {
	if( effectDefHandle < 0 || !renderEffect.declEffect ) {
		return;
	}

	UpdateBind( presentationTime );
	gameRenderWorld->UpdateEffectDef( effectDefHandle, &renderEffect, gameLocal.time );
}

/*
================
rvClientEffect::PresentAt

Presentation-only placement of an unbound effect that an owner moves itself,
such as a debris trail. Like UpdatePresentationTransform it repeats the
authoritative game time, so particles move without respawning or ageing, and
the "effect finished" result is ignored: lifetime stays in Think().
================
*/
void rvClientEffect::PresentAt ( const idVec3 &origin, const idMat3 &axis ) {
	if ( effectDefHandle < 0 || !renderEffect.declEffect || bindMaster ) {
		return;
	}
	const idVec3 authoritativeOrigin = renderEffect.origin;
	const idMat3 authoritativeAxis = renderEffect.axis;
	renderEffect.origin = origin;
	renderEffect.axis = axis;
	gameRenderWorld->UpdateEffectDef( effectDefHandle, &renderEffect, gameLocal.time );
	renderEffect.origin = authoritativeOrigin;
	renderEffect.axis = authoritativeAxis;
}

/*
================
rvClientEffect::Play
================
*/
bool rvClientEffect::Play ( int _startTime, bool _loop, const idVec3& endOrigin ) {
	if ( !renderEffect.declEffect ) {
		return false;
	}

	// Initialize the render entity
	if ( bindMaster ) {
		renderEntity_t* renderEnt = bindMaster->GetRenderEntity ( );
		assert( renderEnt );
		
		// Copy suppress values from parent entity
		renderEffect.allowSurfaceInViewID	 = renderEnt->allowSurfaceInViewID;
		renderEffect.suppressSurfaceInViewID = renderEnt->suppressSurfaceInViewID;
		renderEffect.weaponDepthHackInViewID = renderEnt->weaponDepthHackInViewID;
  	}

	renderEffect.shaderParms[SHADERPARM_RED] = 1.0f;
	renderEffect.shaderParms[SHADERPARM_GREEN] = 1.0f;
	renderEffect.shaderParms[SHADERPARM_BLUE] = 1.0f;
	renderEffect.shaderParms[SHADERPARM_ALPHA] = 1.0f;
	renderEffect.shaderParms[SHADERPARM_BRIGHTNESS] = 1.0f; 
	renderEffect.shaderParms[SHADERPARM_TIMEOFFSET] = MS2SEC( gameLocal.time ); 
	renderEffect.hasEndOrigin = ( endOrigin != vec3_origin );
	renderEffect.endOrigin	  = endOrigin;		
	renderEffect.loop		  = _loop;

	assert( effectDefHandle < 0 );

	renderEffect.startTime = MS2SEC( _startTime );

	return true;	
}

/*
================
rvClientEffect::Stop
================
*/
void rvClientEffect::Stop ( bool destroyParticles ) {
	if( effectDefHandle < 0 ) {
		renderEffect.startTime = -1.0f;
		renderEffect.declEffect = NULL;
		return;
	}
	
	if ( destroyParticles ) {
		// Clear the effect index to make sure the effect isnt started again.  This is 
		// an indirect way of making the effect not think
		renderEffect.declEffect = NULL;
		
		FreeEffectDef ( );
		PostEventMS( &EV_Remove, 0 );
	} else {
		gameRenderWorld->StopEffectDef( effectDefHandle );
		// this will ensure the effect doesn't re-up when loaded from a save.
		renderEffect.startTime = -1.0f;
		Unbind ( );
	}
}

/*
================
rvClientEffect::Restart
================
*/
void rvClientEffect::Restart ( void ) {
	FreeEffectDef ( );
	
	if ( renderEffect.loop ) {
		Play ( gameLocal.time, true, renderEffect.endOrigin );
	}
}

/*
================
rvClientEffect::Attenuate
================
*/
void rvClientEffect::Attenuate ( float attenuation ) {
	renderEffect.attenuation = attenuation;
}

/*
================
rvClientEffect::GetDuration
================
*/
float rvClientEffect::GetDuration( void ) const {
	if( effectDefHandle < 0 ) {
		return 0.0f;
	}
	
	return bse->EffectDuration( gameRenderWorld->GetEffectDef( effectDefHandle ) );
}

/*
================
rvClientEffect::DrawDebugInfo
================
*/
void rvClientEffect::DrawDebugInfo ( void ) const {
	rvClientEntity::DrawDebugInfo ( );

	if ( !gameLocal.GetLocalPlayer() ) {	
		return;
	}
		
	if( !renderEffect.declEffect ) {
		return;
	}

	idMat3 axis = gameLocal.GetLocalPlayer()->viewAngles.ToMat3();
	idVec3 up = axis[ 2 ] * 5.0f;

	gameRenderWorld->DrawText ( renderEffect.declEffect->GetName(), worldOrigin + up, 0.1f, colorWhite, axis, 1 );
}

/*
================
rvClientEffect::Save
================
*/
void rvClientEffect::Save( idSaveGame *savefile ) const {
	savefile->WriteRenderEffect( renderEffect );
	savefile->WriteJoint( endOriginJoint );
}

/*
================
rvClientEffect::Restore
================
*/
void rvClientEffect::Restore( idRestoreGame *savefile ) {
	savefile->ReadRenderEffect( renderEffect );
	effectDefHandle = -1;
	savefile->ReadJoint( endOriginJoint );
}

/*
================
rvClientEffect::FreeEntityDef
================
*/
void rvClientEffect::FreeEntityDef( void ) {
	FreeEffectDef();
}

/*
===============================================================================

rvClientCrawlEffect

===============================================================================
*/

CLASS_DECLARATION( rvClientEffect, rvClientCrawlEffect )
END_CLASS

/*
================
rvClientCrawlEffect::rvClientCrawlEffect
================
*/
rvClientCrawlEffect::rvClientCrawlEffect ( void ) {
}

rvClientCrawlEffect::rvClientCrawlEffect ( const idDecl *effect, idEntity* ent, int _crawlTime, idList<jointHandle_t>* joints ) : rvClientEffect ( effect ) {
	int i;
	
	// Crawl effects require an animated entity
	crawlEnt = dynamic_cast<idAnimatedEntity*>(ent);
	if ( !crawlEnt) {
		return;
	}
	
	// Specific joint list provided?
	if ( joints && joints->Num () ) {
		crawlJoints.Clear ( );
		for ( i = 0; i < joints->Num(); i ++ ) {
			crawlJoints.Append ( (*joints)[i] );
		}
	} else {
		// Use only parent joints and skip joint zero which is presumed to be the origin
		for ( i = ent->GetAnimator()->NumJoints() - 1; i > 0; i -- ) {
			if ( i != ent->GetAnimator()->GetFirstChild ( (jointHandle_t)i ) ) {
				crawlJoints.Append ( (jointHandle_t)i );
			}
		}
	}
	
	//no joints?  abort!
	if ( !crawlJoints.Num() ) {
		return;
	}

	jointStart = gameLocal.random.RandomInt ( crawlJoints.Num() );
	crawlDir   = gameLocal.random.RandomInt ( 2 ) > 0 ? 1 : -1;		
	jointEnd   = (jointStart + crawlDir + crawlJoints.Num() ) % crawlJoints.Num();
	crawlTime  = _crawlTime;	
	nextCrawl  = gameLocal.time + crawlTime;
}

/*
================
rvClientCrawlEffect::Think
================
*/
void rvClientCrawlEffect::Think ( void ) {
	
	// If there is no crawl entity or no crawl joints then just free ourself
	if ( !crawlEnt || !crawlJoints.Num() ) {
		PostEventMS ( &EV_Remove, 0 );
		return;
	}
			
	// Move to the next joint if its time
	if ( gameLocal.time > nextCrawl ) {
		jointStart = (jointStart + crawlDir + crawlJoints.Num() ) % crawlJoints.Num();
		jointEnd   = (jointStart + crawlDir + crawlJoints.Num() ) % crawlJoints.Num();
		nextCrawl  = gameLocal.time + crawlTime;
	}

	idVec3 offsetStart;
	idVec3 offsetEnd;
	idVec3 dir;
	idMat3 axis;
	
	// Get the start origin
	crawlEnt->GetJointWorldTransform ( crawlJoints[jointStart], gameLocal.time, offsetStart, axis );
	SetOrigin ( offsetStart );

	// Get the end origin
	crawlEnt->GetJointWorldTransform ( crawlJoints[jointEnd], gameLocal.time, offsetEnd, axis );
	SetEndOrigin ( offsetEnd );

	// Update the axis to point at the bone
	dir = offsetEnd - offsetStart;
	dir.Normalize();
	SetAxis ( dir.ToMat3( ) );
		
	rvClientEffect::Think ( );
}

/*
================
rvClientCrawlEffect::Save
================
*/
void rvClientCrawlEffect::Save( idSaveGame *savefile ) const {
	savefile->WriteInt( crawlJoints.Num() );
	for( int ix = 0; ix < crawlJoints.Num(); ++ix ) {
		savefile->WriteJoint( crawlJoints[ix] );
	}
	
	savefile->WriteInt( crawlTime );
	savefile->WriteInt( nextCrawl );
	savefile->WriteInt( jointStart );
	savefile->WriteInt( jointEnd );
	savefile->WriteInt( crawlDir );
	crawlEnt.Save( savefile );
}

/*
================
rvClientCrawlEffect::Restore
================
*/
void rvClientCrawlEffect::Restore( idRestoreGame *savefile ) {
	int numJoints = 0;
	savefile->ReadInt( numJoints );
	if ( numJoints < 0 || numJoints > MAX_SAVEGAME_CLIENT_CRAWL_JOINTS ) {
		savefile->Error( "rvClientCrawlEffect::Restore: invalid crawl joint count %d", numJoints );
	}
	crawlJoints.SetNum( numJoints );
	for( int ix = 0; ix < numJoints; ++ix ) {
		savefile->ReadJoint( crawlJoints[ix] );
	}
	
	savefile->ReadInt( crawlTime );
	savefile->ReadInt( nextCrawl );
	savefile->ReadInt( jointStart );
	savefile->ReadInt( jointEnd );
	savefile->ReadInt( crawlDir );
	if ( numJoints > 0 ) {
		if ( jointStart < 0 || jointStart >= numJoints ) {
			savefile->Error( "rvClientCrawlEffect::Restore: invalid crawl start joint index %d for %d joints", jointStart, numJoints );
		}
		if ( jointEnd < 0 || jointEnd >= numJoints ) {
			savefile->Error( "rvClientCrawlEffect::Restore: invalid crawl end joint index %d for %d joints", jointEnd, numJoints );
		}
		if ( crawlDir != -1 && crawlDir != 1 ) {
			savefile->Error( "rvClientCrawlEffect::Restore: invalid crawl direction %d", crawlDir );
		}
	}
	crawlEnt.Restore( savefile );
}
