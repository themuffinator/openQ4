/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/



#include "tr_local.h"

//#define WRITE_GUIS

typedef struct {
	int		version;
	int		sizeofRenderEntity;
	int		sizeofRenderLight;
	char	mapname[256];
} demoHeader_t;

namespace {

void R_FreeRenderDemoDecalChain( idRenderModelDecal *decal ) {
	while ( decal != NULL ) {
		idRenderModelDecal *next = decal->Next();
		idRenderModelDecal::Free( decal );
		decal = next;
	}
}

void R_WriteDemoRenderViewData( idDemoFile *demo, const renderView_t *renderView ) {
	demo->WriteInt( renderView->viewID );
	demo->WriteInt( renderView->x );
	demo->WriteInt( renderView->y );
	demo->WriteInt( renderView->width );
	demo->WriteInt( renderView->height );
	demo->WriteFloat( renderView->fov_x );
	demo->WriteFloat( renderView->fov_y );
	demo->WriteVec3( renderView->vieworg );
	demo->WriteMat3( renderView->viewaxis );
	demo->WriteBool( renderView->cramZNear );
	demo->WriteBool( renderView->forceUpdate );
	// binary compatibility with old win32 version writing padded structures directly to disk
	demo->WriteUnsignedChar( 0 );
	demo->WriteUnsignedChar( 0 );
	demo->WriteInt( renderView->time );
	for ( int i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ ) {
		demo->WriteFloat( renderView->shaderParms[i] );
	}
	demo->WriteBool( renderView->globalMaterial != NULL );
	if ( renderView->globalMaterial != NULL ) {
		demo->WriteHashString( renderView->globalMaterial->GetName() );
	}
}

bool R_ReadDemoRenderViewData( idDemoFile *demo, renderView_t *renderView, int renderdemoVersion ) {
	renderView->globalMaterial = NULL;

	demo->ReadInt( renderView->viewID );
	demo->ReadInt( renderView->x );
	demo->ReadInt( renderView->y );
	demo->ReadInt( renderView->width );
	demo->ReadInt( renderView->height );
	demo->ReadFloat( renderView->fov_x );
	demo->ReadFloat( renderView->fov_y );
	demo->ReadVec3( renderView->vieworg );
	demo->ReadMat3( renderView->viewaxis );
	demo->ReadBool( renderView->cramZNear );
	demo->ReadBool( renderView->forceUpdate );

	char tmp;
	demo->ReadChar( tmp );
	demo->ReadChar( tmp );
	demo->ReadInt( renderView->time );
	for ( int i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ ) {
		demo->ReadFloat( renderView->shaderParms[i] );
	}

	if ( renderdemoVersion >= OPENQ4_RENDERDEMO_RENDER_VIEW_DECLS_VERSION ) {
		bool hasGlobalMaterial = false;
		if ( !demo->ReadBool( hasGlobalMaterial ) ) {
			return false;
		}
		if ( hasGlobalMaterial ) {
			renderView->globalMaterial = declManager->FindMaterial( demo->ReadHashString() );
		}
	} else {
		int legacyGlobalMaterial = 0;
		if ( !demo->ReadInt( legacyGlobalMaterial ) ) {
			return false;
		}
		// Older render demos only persisted the raw pointer value here, which
		// is meaningless once replayed in a later process.
		renderView->globalMaterial = NULL;
	}

	return true;
}

void R_WriteDemoEffectData( idDemoFile *demo, const rvRenderEffectLocal *effectDef ) {
	const renderEffect_t &effect = effectDef->parms;
	const bool stopped = bse->IsEffectStopped( effectDef );

	demo->WriteInt( effectDef->gameTime );
	demo->WriteFloat( effect.startTime );
	demo->WriteInt( effect.suppressSurfaceInViewID );
	demo->WriteInt( effect.allowSurfaceInViewID );
	demo->WriteInt( effect.groupID );
	demo->WriteVec3( effect.origin );
	demo->WriteMat3( effect.axis );
	demo->WriteVec3( effect.gravity );
	demo->WriteVec3( effect.endOrigin );
	demo->WriteFloat( effect.attenuation );
	demo->WriteBool( effect.hasEndOrigin );
	demo->WriteBool( effect.loop );
	demo->WriteBool( effect.ambient );
	demo->WriteBool( effect.inConnectedArea );
	demo->WriteInt( effect.weaponDepthHackInViewID );
	demo->WriteFloat( effect.modelDepthHack );
	demo->WriteInt( effect.referenceSoundHandle );
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; ++i ) {
		demo->WriteFloat( effect.shaderParms[i] );
	}
	demo->WriteBool( stopped );
	demo->WriteBool( effect.declEffect != NULL );
	if ( effect.declEffect != NULL ) {
		demo->WriteHashString( effect.declEffect->GetName() );
	}
}

bool R_ReadDemoEffectData( idDemoFile *demo, renderEffect_t *effect, int *gameTime, bool *stopped ) {
	effect->declEffect = NULL;

	if ( !demo->ReadInt( *gameTime ) ) {
		return false;
	}
	demo->ReadFloat( effect->startTime );
	demo->ReadInt( effect->suppressSurfaceInViewID );
	demo->ReadInt( effect->allowSurfaceInViewID );
	demo->ReadInt( effect->groupID );
	demo->ReadVec3( effect->origin );
	demo->ReadMat3( effect->axis );
	demo->ReadVec3( effect->gravity );
	demo->ReadVec3( effect->endOrigin );
	demo->ReadFloat( effect->attenuation );
	demo->ReadBool( effect->hasEndOrigin );
	demo->ReadBool( effect->loop );
	demo->ReadBool( effect->ambient );
	demo->ReadBool( effect->inConnectedArea );
	demo->ReadInt( effect->weaponDepthHackInViewID );
	demo->ReadFloat( effect->modelDepthHack );
	demo->ReadInt( effect->referenceSoundHandle );
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; ++i ) {
		demo->ReadFloat( effect->shaderParms[i] );
	}
	if ( !demo->ReadBool( *stopped ) ) {
		return false;
	}
	bool hasDeclEffect = false;
	if ( !demo->ReadBool( hasDeclEffect ) ) {
		return false;
	}
	if ( hasDeclEffect ) {
		effect->declEffect = declManager->FindType( DECL_EFFECT, demo->ReadHashString() );
	}

	return true;
}

}


/*
==============
StartWritingDemo
==============
*/
void		idRenderWorldLocal::StartWritingDemo( idDemoFile *demo ) {
	int		i;

	// FIXME: we should track the idDemoFile locally, instead of snooping into session for it

	WriteLoadMap();

	// write the door portal state
	for ( i = 0 ; i < numInterAreaPortals ; i++ ) {
		if ( doublePortals[i].blockingBits ) {
			SetPortalState( i+1, doublePortals[i].blockingBits );
		}
	}

	// clear the archive counter on all defs
	for ( i = 0 ; i < lightDefs.Num() ; i++ ) {
		if ( lightDefs[i] ) {
			lightDefs[i]->archived = false;
		}
	}
	for ( i = 0 ; i < entityDefs.Num() ; i++ ) {
		if ( entityDefs[i] ) {
			entityDefs[i]->archived = false;
		}
	}
	for ( i = 0 ; i < effectsDef.Num() ; i++ ) {
		if ( effectsDef[i] ) {
			effectsDef[i]->archived = false;
		}
	}
}

void idRenderWorldLocal::StopWritingDemo() {
//	writeDemo = NULL;
}

/*
==============
ProcessDemoCommand
==============
*/
bool		idRenderWorldLocal::ProcessDemoCommand( idDemoFile *readDemo, renderView_t *renderView, int *demoTimeOffset ) {
	if ( !readDemo ) {
		return false;
	}

	demoCommand_t	dc;
	qhandle_t		h;

	if ( !readDemo->ReadInt( (int&)dc ) ) {
		// a demoShot may not have an endFrame, but it is still valid
		return false;
	}

	switch( dc ) {
	case DC_LOADMAP:
		// read the initial data
		demoHeader_t	header;

		readDemo->ReadInt( header.version );
		readDemo->ReadInt( header.sizeofRenderEntity );
		readDemo->ReadInt( header.sizeofRenderLight );
		for ( int i = 0; i < 256; i++ )
			readDemo->ReadChar( header.mapname[i] );
		// the internal version value got replaced by DS_VERSION at toplevel
		if ( header.version < 4 || header.version > OPENQ4_RENDERDEMO_CURRENT_VERSION ) {
				common->Error( "Demo version mismatch.\n" );
		}

		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_LOADMAP: %s\n", header.mapname );
		}
		InitFromMap( header.mapname );

		pendingDemoTimeOffset = true;		// the first render view after the map load sets the playback offset

		break;

	case DC_RENDERVIEW:
		if ( !R_ReadDemoRenderViewData( readDemo, renderView, session->renderdemoVersion ) ) {
			return false;
		}
												 
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_RENDERVIEW: %i\n", renderView->time );
		}

		// possibly change the time offset if this is from a new map
		if ( pendingDemoTimeOffset && demoTimeOffset ) {
			*demoTimeOffset = renderView->time - eventLoop->Milliseconds();
		}
		pendingDemoTimeOffset = false;
		return false;

	case DC_UPDATE_ENTITYDEF:
		ReadRenderEntity();
		break;
	case DC_DELETE_ENTITYDEF:
		if ( !readDemo->ReadInt( h ) ) {
			return false;
		}
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_DELETE_ENTITYDEF: %i\n", h );
		}
		FreeEntityDef( h );
		break;
	case DC_UPDATE_LIGHTDEF:
		ReadRenderLight();
		break;
	case DC_DELETE_LIGHTDEF:
		if ( !readDemo->ReadInt( h ) ) {
			return false;
		}
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_DELETE_LIGHTDEF: %i\n", h );
		}
		FreeLightDef( h );
		break;
	case DC_UPDATE_EFFECTDEF:
		ReadRenderEffect();
		break;
	case DC_STOP_EFFECTDEF:
		if ( !readDemo->ReadInt( h ) ) {
			return false;
		}
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_STOP_EFFECTDEF: %i\n", h );
		}
		StopEffectDef( h );
		break;
	case DC_DELETE_EFFECTDEF:
		if ( !readDemo->ReadInt( h ) ) {
			return false;
		}
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_DELETE_EFFECTDEF: %i\n", h );
		}
		FreeEffectDef( h );
		break;

	case DC_CAPTURE_RENDER:
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_CAPTURE_RENDER\n" );
		}
		renderSystem->CaptureRenderToImage( readDemo->ReadHashString() );
		break;

	case DC_CROP_RENDER:
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_CROP_RENDER\n" );
		}
		int	size[3];
		readDemo->ReadInt( size[0] );
		readDemo->ReadInt( size[1] );
		readDemo->ReadInt( size[2] );
		renderSystem->CropRenderSize( size[0], size[1], size[2] != 0 );
		break;

	case DC_UNCROP_RENDER:
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_UNCROP\n" );
		}
		renderSystem->UnCrop();
		break;

	case DC_GUI_MODEL:
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_GUI_MODEL\n" );
		}
		tr.demoGuiModel->ReadFromDemo( readDemo );
		break;

	case DC_DEFINE_MODEL:
		{
	// jmarshall: demos broken
		//idRenderModel	*model = renderModelManager->AllocModel();
		//model->ReadFromDemoFile( session->readDemo );
		//// add to model manager, so we can find it
		//renderModelManager->AddModel( model );
		//
		//// save it in the list to free when clearing this map
		//localModels.Append( model );
		//
		//if ( r_showDemo.GetBool() ) {
		//	common->Printf( "DC_DEFINE_MODEL\n" );
		//}
	// jmarshall end
		break;
		}
	case DC_SET_PORTAL_STATE:
		{
			int		data[2];
			readDemo->ReadInt( data[0] );
			readDemo->ReadInt( data[1] );
			SetPortalState( data[0], data[1] );
			if ( r_showDemo.GetBool() ) {
				common->Printf( "DC_SET_PORTAL_STATE: %i %i\n", data[0], data[1] );
			}
		}
		
		break;
	case DC_END_FRAME:
		return true;

	default:
		common->Error( "Bad token in demo stream" );
	}

	return false;
}

/*
================
WriteLoadMap
================
*/
void	idRenderWorldLocal::WriteLoadMap() {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_LOADMAP );

	demoHeader_t	header;
	strncpy( header.mapname, mapName.c_str(), sizeof( header.mapname ) - 1 );
	header.version = OPENQ4_RENDERDEMO_CURRENT_VERSION;
	header.sizeofRenderEntity = sizeof( renderEntity_t );
	header.sizeofRenderLight = sizeof( renderLight_t );
	session->writeDemo->WriteInt( header.version );
	session->writeDemo->WriteInt( header.sizeofRenderEntity );
	session->writeDemo->WriteInt( header.sizeofRenderLight );
	for ( int i = 0; i < 256; i++ )
		session->writeDemo->WriteChar( header.mapname[i] );
	
	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_LOADMAP: %s\n", mapName.c_str() );
	}
}

/*
================
WriteVisibleDefs

================
*/
void	idRenderWorldLocal::WriteVisibleDefs( const viewDef_t *viewDef ) {
	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	// make sure all necessary entities and lights are updated
	for ( viewEntity_t *viewEnt = viewDef->viewEntitys ; viewEnt ; viewEnt = viewEnt->next ) {
		idRenderEntityLocal *ent = viewEnt->entityDef;

		if ( ent->archived ) {
			// still up to date
			continue;
		}

		// write it out
		WriteRenderEntity( ent->index, &ent->parms );
		ent->archived = true;
	}

	for ( viewLight_t *viewLight = viewDef->viewLights ; viewLight ; viewLight = viewLight->next ) {
		idRenderLightLocal *light = viewLight->lightDef;

		if ( light->archived ) {
			// still up to date
			continue;
		}
		// write it out
		WriteRenderLight( light->index, &light->parms );
		light->archived = true;
	}

	for ( int i = 0; i < effectsDef.Num(); ++i ) {
		rvRenderEffectLocal *effectDef = effectsDef[i];
		if ( effectDef == NULL ) {
			continue;
		}
		if ( effectDef->visibleCount != tr.viewCount || effectDef->archived ) {
			continue;
		}

		WriteRenderEffect( effectDef->index, effectDef );
		effectDef->archived = true;
	}
}


/*
================
WriteRenderView
================
*/
void	idRenderWorldLocal::WriteRenderView( const renderView_t *renderView ) {
	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}
	
	// write the actual view command
	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_RENDERVIEW );
	R_WriteDemoRenderViewData( session->writeDemo, renderView );
	
	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_RENDERVIEW: %i\n", renderView->time );
	}
}

/*
================
WriteFreeEntity
================
*/
void	idRenderWorldLocal::WriteFreeEntity( qhandle_t handle ) {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_DELETE_ENTITYDEF );
	session->writeDemo->WriteInt( handle );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_DELETE_ENTITYDEF: %i\n", handle );
	}
}

/*
================
WriteFreeLightEntity
================
*/
void	idRenderWorldLocal::WriteFreeLight( qhandle_t handle ) {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_DELETE_LIGHTDEF );
	session->writeDemo->WriteInt( handle );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_DELETE_LIGHTDEF: %i\n", handle );
	}
}

/*
================
WriteFreeEffect
================
*/
void	idRenderWorldLocal::WriteFreeEffect( qhandle_t handle ) {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_DELETE_EFFECTDEF );
	session->writeDemo->WriteInt( handle );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_DELETE_EFFECTDEF: %i\n", handle );
	}
}

/*
================
WriteStopEffect
================
*/
void	idRenderWorldLocal::WriteStopEffect( qhandle_t handle ) {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_STOP_EFFECTDEF );
	session->writeDemo->WriteInt( handle );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_STOP_EFFECTDEF: %i\n", handle );
	}
}

/*
================
WriteRenderLight
================
*/
void	idRenderWorldLocal::WriteRenderLight( qhandle_t handle, const renderLight_t *light ) {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_UPDATE_LIGHTDEF );
	session->writeDemo->WriteInt( handle );

	session->writeDemo->WriteMat3( light->axis );
	session->writeDemo->WriteVec3( light->origin );
	session->writeDemo->WriteInt( light->suppressLightInViewID );
	session->writeDemo->WriteInt( light->allowLightInViewID );
	session->writeDemo->WriteBool( light->noShadows );
	session->writeDemo->WriteBool( light->noSpecular );
	session->writeDemo->WriteBool( light->pointLight );
	session->writeDemo->WriteBool( light->parallel );
	session->writeDemo->WriteVec3( light->lightRadius );
	session->writeDemo->WriteVec3( light->lightCenter );
	session->writeDemo->WriteVec3( light->target );
	session->writeDemo->WriteVec3( light->right );
	session->writeDemo->WriteVec3( light->up );
	session->writeDemo->WriteVec3( light->start );
	session->writeDemo->WriteVec3( light->end );
	idRenderModel *prelightModel = R_SanitizePrelightModelPointer( light->prelightModel );
	session->writeDemo->WriteBool( prelightModel != NULL );
	session->writeDemo->WriteInt( light->lightId );
	session->writeDemo->WriteBool( light->shader != NULL );
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; i++)
		session->writeDemo->WriteFloat( light->shaderParms[i] );
	session->writeDemo->WriteInt( light->referenceSoundHandle );
	session->writeDemo->WriteInt( light->referenceSound );

	if ( prelightModel ) {
		session->writeDemo->WriteHashString( prelightModel->Name() );
	}
	if ( light->shader ) {
		session->writeDemo->WriteHashString( light->shader->GetName() );
	}
	if ( light->referenceSound ) {
		//int	index = light->referenceSound->Index();
		//session->writeDemo->WriteInt( index );
	}

	// Preserve Quake 4-era light flags without disturbing the older serialized prefix.
	session->writeDemo->WriteBool( light->noDynamicShadows );
	session->writeDemo->WriteBool( light->globalLight );
	session->writeDemo->WriteFloat( light->detailLevel );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_UPDATE_LIGHTDEF: %i\n", handle );
	}
}

/*
================
ReadRenderLight
================
*/
void	idRenderWorldLocal::ReadRenderLight( ) {
	renderLight_t	light = {};
	int				index;
	bool			hasPrelightModel = false;
	bool			hasShader = false;

	light.detailLevel = DEFAULT_LIGHT_DETAIL_LEVEL;

	session->readDemo->ReadInt( index );
	if ( index < 0 ) {
		common->Error( "ReadRenderLight: index < 0 " );
	}

	session->readDemo->ReadMat3( light.axis );
	session->readDemo->ReadVec3( light.origin );
	session->readDemo->ReadInt( light.suppressLightInViewID );
	session->readDemo->ReadInt( light.allowLightInViewID );
	session->readDemo->ReadBool( light.noShadows );
	session->readDemo->ReadBool( light.noSpecular );
	session->readDemo->ReadBool( light.pointLight );
	session->readDemo->ReadBool( light.parallel );
	session->readDemo->ReadVec3( light.lightRadius );
	session->readDemo->ReadVec3( light.lightCenter );
	session->readDemo->ReadVec3( light.target );
	session->readDemo->ReadVec3( light.right );
	session->readDemo->ReadVec3( light.up );
	session->readDemo->ReadVec3( light.start );
	session->readDemo->ReadVec3( light.end );
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
		session->readDemo->ReadBool( hasPrelightModel );
	} else {
		session->readDemo->ReadInt( (int&)light.prelightModel );
		hasPrelightModel = ( light.prelightModel != NULL );
	}
	session->readDemo->ReadInt( light.lightId );
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
		session->readDemo->ReadBool( hasShader );
	} else {
		session->readDemo->ReadInt( (int&)light.shader );
		hasShader = ( light.shader != NULL );
	}
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; i++)
		session->readDemo->ReadFloat( light.shaderParms[i] );
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_ENTITY_EXTRAS_VERSION ) {
		session->readDemo->ReadInt( light.referenceSoundHandle );
	}
	session->readDemo->ReadInt( light.referenceSound );
	if ( hasPrelightModel ) {
		light.prelightModel = renderModelManager->FindModel( session->readDemo->ReadHashString() );
	}
	if ( hasShader ) {
		light.shader = declManager->FindMaterial( session->readDemo->ReadHashString() );
	}
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_LIGHT_EXTRAS_VERSION ) {
		session->readDemo->ReadBool( light.noDynamicShadows );
		session->readDemo->ReadBool( light.globalLight );
		session->readDemo->ReadFloat( light.detailLevel );
	}
	UpdateLightDef( index, &light );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "DC_UPDATE_LIGHTDEF: %i\n", index );
	}
}

/*
================
WriteRenderEffect
================
*/
void	idRenderWorldLocal::WriteRenderEffect( qhandle_t handle, const rvRenderEffectLocal *effectDef ) {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw || effectDef == NULL ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_UPDATE_EFFECTDEF );
	session->writeDemo->WriteInt( handle );
	R_WriteDemoEffectData( session->writeDemo, effectDef );

	if ( r_showDemo.GetBool() ) {
		const char *effectName = effectDef->parms.declEffect ? effectDef->parms.declEffect->GetName() : "NULL";
		common->Printf( "write DC_UPDATE_EFFECTDEF: %i = %s\n", handle, effectName );
	}
}

/*
================
ReadRenderEffect
================
*/
void	idRenderWorldLocal::ReadRenderEffect() {
	renderEffect_t		effect = {};
	int					index = -1;
	int					effectGameTime = 0;
	bool				stopped = false;

	if ( !session->readDemo->ReadInt( index ) ) {
		return;
	}
	if ( index < 0 ) {
		common->Error( "ReadRenderEffect: index < 0" );
	}
	if ( !R_ReadDemoEffectData( session->readDemo, &effect, &effectGameTime, &stopped ) ) {
		return;
	}

	UpdateEffectDef( index, &effect, effectGameTime );

	rvRenderEffectLocal *def = ( index >= 0 && index < effectsDef.Num() ) ? effectsDef[index] : NULL;
	if ( def != NULL ) {
		bse->SetEffectStopped( def, stopped );
	}

	if ( r_showDemo.GetBool() ) {
		const char *effectName = effect.declEffect ? effect.declEffect->GetName() : "NULL";
		common->Printf( "DC_UPDATE_EFFECTDEF: %i = %s\n", index, effectName );
	}
}

/*
================
WriteRenderEntity
================
*/
void	idRenderWorldLocal::WriteRenderEntity( qhandle_t handle, const renderEntity_t *ent ) {
	idRenderEntityLocal *localEnt;

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	localEnt = ( handle >= 0 && handle < entityDefs.Num() ) ? entityDefs[handle] : NULL;

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_UPDATE_ENTITYDEF );
	session->writeDemo->WriteInt( handle );
	
	session->writeDemo->WriteBool( ent->hModel != NULL );
	session->writeDemo->WriteInt( ent->entityNum );
	session->writeDemo->WriteInt( ent->bodyId );
	session->writeDemo->WriteVec3( ent->bounds[0] );
	session->writeDemo->WriteVec3( ent->bounds[1] );
	session->writeDemo->WriteBool( false );
	session->writeDemo->WriteBool( false );
	session->writeDemo->WriteInt( ent->suppressSurfaceInViewID );
	session->writeDemo->WriteInt( ent->suppressShadowInViewID );
	session->writeDemo->WriteInt( ent->suppressShadowInLightID );
	session->writeDemo->WriteInt( ent->allowSurfaceInViewID );
	session->writeDemo->WriteVec3( ent->origin );
	session->writeDemo->WriteMat3( ent->axis );
	session->writeDemo->WriteBool( ent->customShader != NULL );
	session->writeDemo->WriteBool( ent->referenceShader != NULL );
	session->writeDemo->WriteBool( ent->customSkin != NULL );
	session->writeDemo->WriteInt( ent->referenceSound );
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ )
		session->writeDemo->WriteFloat( ent->shaderParms[i] );
	for ( int i = 0; i < MAX_RENDERENTITY_GUI; i++ )
		session->writeDemo->WriteBool( ent->gui[i] != NULL );
	session->writeDemo->WriteBool( ent->remoteRenderView != NULL );
	if ( ent->remoteRenderView != NULL ) {
		R_WriteDemoRenderViewData( session->writeDemo, ent->remoteRenderView );
	}
	session->writeDemo->WriteInt( ent->numJoints );
	session->writeDemo->WriteFloat( ent->modelDepthHack );
	session->writeDemo->WriteBool( ent->noSelfShadow );
	session->writeDemo->WriteBool( ent->noShadow );
	session->writeDemo->WriteBool( ent->noDynamicInteractions );
	//session->writeDemo->WriteBool( ent->weaponDepthHack );
	session->writeDemo->WriteInt( ent->forceUpdate );

	if ( ent->customShader ) {
		session->writeDemo->WriteHashString( ent->customShader->GetName() );
	}
	if ( ent->customSkin ) {
		session->writeDemo->WriteHashString( ent->customSkin->GetName() );
	}
	if ( ent->hModel ) {
		session->writeDemo->WriteHashString( ent->hModel->Name() );
	}
	if ( ent->referenceShader ) {
		session->writeDemo->WriteHashString( ent->referenceShader->GetName() );
	}
	if ( ent->numJoints ) {
		for ( int i = 0; i < ent->numJoints; i++) {
			float *data = ent->joints[i].ToFloatPtr();
			for ( int j = 0; j < 12; ++j)
				session->writeDemo->WriteFloat( data[j] );
		}
	}

	session->writeDemo->WriteInt( ent->weaponDepthHackInViewID );
	session->writeDemo->WriteFloat( ent->shadowLODDistance );
	session->writeDemo->WriteInt( ent->suppressLOD );
	session->writeDemo->WriteInt( ent->referenceSoundHandle );
	session->writeDemo->WriteBool( ent->overlayShader != NULL );
	if ( ent->overlayShader != NULL ) {
		session->writeDemo->WriteHashString( ent->overlayShader->GetName() );
	}

	session->writeDemo->WriteBool( localEnt != NULL && localEnt->decals != NULL );
	if ( localEnt != NULL && localEnt->decals != NULL ) {
		localEnt->decals->WriteToDemoFile( session->writeDemo );
	}
	session->writeDemo->WriteBool( localEnt != NULL && localEnt->overlay != NULL );
	if ( localEnt != NULL && localEnt->overlay != NULL ) {
		localEnt->overlay->WriteToDemoFile( session->writeDemo );
	}

#ifdef WRITE_GUIS
	if ( ent->gui ) {
		ent->gui->WriteToDemoFile( session->writeDemo );
	}
	if ( ent->gui2 ) {
		ent->gui2->WriteToDemoFile( session->writeDemo );
	}
	if ( ent->gui3 ) {
		ent->gui3->WriteToDemoFile( session->writeDemo );
	}
#endif

	// RENDERDEMO_VERSION >= 2 ( legacy engine 1.2 )
	//session->writeDemo->WriteInt( ent->timeGroup );
	//session->writeDemo->WriteInt( ent->xrayIndex );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_UPDATE_ENTITYDEF: %i = %s\n", handle, ent->hModel ? ent->hModel->Name() : "NULL" );
	}
}

/*
================
ReadRenderEntity
================
*/
void	idRenderWorldLocal::ReadRenderEntity() {
	renderEntity_t		ent = {};
	idRenderModelDecal *demoDecals = NULL;
	idRenderModelOverlay *demoOverlay = NULL;
	renderView_t		demoRemoteRenderView = {};
	bool				hasRemoteRenderView = false;
	bool				hasModel = false;
	bool				hasCallback = false;
	bool				hasCallbackData = false;
	bool				hasCustomShader = false;
	bool				hasReferenceShader = false;
	bool				hasCustomSkin = false;
	bool				hasGui[ MAX_RENDERENTITY_GUI ] = { false };
	int					index, i;

	session->readDemo->ReadInt( index );
	if ( index < 0 ) {
		common->Error( "ReadRenderEntity: index < 0" );
	}

	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
		session->readDemo->ReadBool( hasModel );
	} else {
		session->readDemo->ReadInt( (int&)ent.hModel );
		hasModel = ( ent.hModel != NULL );
	}
	session->readDemo->ReadInt( ent.entityNum );
	session->readDemo->ReadInt( ent.bodyId );
	session->readDemo->ReadVec3( ent.bounds[0] );
	session->readDemo->ReadVec3( ent.bounds[1] );
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
		// Render callbacks are process-local and cannot be replayed safely.
		session->readDemo->ReadBool( hasCallback );
		session->readDemo->ReadBool( hasCallbackData );
	} else {
		session->readDemo->ReadInt( (int&)ent.callback );
		session->readDemo->ReadInt( (int&)ent.callbackData );
	}
	session->readDemo->ReadInt( ent.suppressSurfaceInViewID );
	session->readDemo->ReadInt( ent.suppressShadowInViewID );
	session->readDemo->ReadInt( ent.suppressShadowInLightID );
	session->readDemo->ReadInt( ent.allowSurfaceInViewID );
	session->readDemo->ReadVec3( ent.origin );
	session->readDemo->ReadMat3( ent.axis );
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
		session->readDemo->ReadBool( hasCustomShader );
		session->readDemo->ReadBool( hasReferenceShader );
		session->readDemo->ReadBool( hasCustomSkin );
	} else {
		session->readDemo->ReadInt( (int&)ent.customShader );
		session->readDemo->ReadInt( (int&)ent.referenceShader );
		session->readDemo->ReadInt( (int&)ent.customSkin );
		hasCustomShader = ( ent.customShader != NULL );
		hasReferenceShader = ( ent.referenceShader != NULL );
		hasCustomSkin = ( ent.customSkin != NULL );
	}
	session->readDemo->ReadInt( ent.referenceSound );
	for ( i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ ) {
		session->readDemo->ReadFloat( ent.shaderParms[i] );
	}
	for ( i = 0; i < MAX_RENDERENTITY_GUI; i++ ) {
		if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
			session->readDemo->ReadBool( hasGui[i] );
		} else {
			session->readDemo->ReadInt( (int&)ent.gui[i] );
			hasGui[i] = ( ent.gui[i] != NULL );
		}
	}
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_REMOTE_VIEW_VERSION ) {
		session->readDemo->ReadBool( hasRemoteRenderView );
		if ( hasRemoteRenderView && !R_ReadDemoRenderViewData( session->readDemo, &demoRemoteRenderView, session->renderdemoVersion ) ) {
			return;
		}
	} else {
		int legacyRemoteRenderView = 0;
		session->readDemo->ReadInt( legacyRemoteRenderView );
		hasRemoteRenderView = false;
	}
	session->readDemo->ReadInt( ent.numJoints );
	if ( session->renderdemoVersion < OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
		session->readDemo->ReadInt( (int&)ent.joints );
	}
	session->readDemo->ReadFloat( ent.modelDepthHack );
	session->readDemo->ReadBool( ent.noSelfShadow );
	session->readDemo->ReadBool( ent.noShadow );
	session->readDemo->ReadBool( ent.noDynamicInteractions );
	{
		int forceUpdate;
		session->readDemo->ReadInt( forceUpdate );
		ent.forceUpdate = ( forceUpdate != 0 );
	}
	ent.callback = NULL;
	if ( hasCustomShader ) {
		ent.customShader = declManager->FindMaterial( session->readDemo->ReadHashString() );
	}
	if ( hasCustomSkin ) {
		ent.customSkin = declManager->FindSkin( session->readDemo->ReadHashString() );
	}
	if ( hasModel ) {
		ent.hModel = renderModelManager->FindModel( session->readDemo->ReadHashString() );
	}
	if ( hasReferenceShader ) {
		ent.referenceShader = declManager->FindMaterial( session->readDemo->ReadHashString() );
	}
	if ( ent.numJoints ) {
		ent.joints = (idJointMat *)Mem_Alloc16( ent.numJoints * sizeof( ent.joints[0] ) ); 
		for ( int i = 0; i < ent.numJoints; i++) {
			float *data = ent.joints[i].ToFloatPtr();
			for ( int j = 0; j < 12; ++j)
				session->readDemo->ReadFloat( data[j] );
		}
	}

	if ( hasRemoteRenderView ) {
		idRenderEntityLocal *def = ( index >= 0 && index < entityDefs.Num() ) ? entityDefs[index] : NULL;
		if ( def != NULL ) {
			if ( def->demoRemoteRenderView == NULL ) {
				def->demoRemoteRenderView = new renderView_t;
			}
			*def->demoRemoteRenderView = demoRemoteRenderView;
			def->hasDemoRemoteRenderView = true;
			ent.remoteRenderView = def->demoRemoteRenderView;
		} else {
			ent.remoteRenderView = &demoRemoteRenderView;
		}
	} else {
		ent.remoteRenderView = NULL;
	}

	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_ENTITY_EXTRAS_VERSION ) {
		bool hasOverlayShader;
		bool hasDecals;
		bool hasOverlay;

		session->readDemo->ReadInt( ent.weaponDepthHackInViewID );
		session->readDemo->ReadFloat( ent.shadowLODDistance );
		session->readDemo->ReadInt( ent.suppressLOD );
		session->readDemo->ReadInt( ent.referenceSoundHandle );
		session->readDemo->ReadBool( hasOverlayShader );
		if ( hasOverlayShader ) {
			ent.overlayShader = declManager->FindMaterial( session->readDemo->ReadHashString() );
		}

		session->readDemo->ReadBool( hasDecals );
		if ( hasDecals ) {
			demoDecals = idRenderModelDecal::Alloc();
			demoDecals->ReadFromDemoFile( session->readDemo );
		}

		session->readDemo->ReadBool( hasOverlay );
		if ( hasOverlay ) {
			demoOverlay = idRenderModelOverlay::Alloc();
			demoOverlay->ReadFromDemoFile( session->readDemo );
		}
	}

	ent.callbackData = NULL;

	for ( i = 0; i < MAX_RENDERENTITY_GUI; i++ ) {
		if ( hasGui[ i ] ) {
			ent.gui[ i ] = uiManager->Alloc();
#ifdef WRITE_GUIS
			ent.gui[ i ]->ReadFromDemoFile( session->readDemo );
#endif
		}
	}

	// >= legacy engine v1.2 only
	//if ( session->renderdemoVersion >= 2 ) {
	//	session->readDemo->ReadInt( ent.timeGroup );
	//	session->readDemo->ReadInt( ent.xrayIndex );
	//} else {
	//	ent.timeGroup = 0;
	//	ent.xrayIndex = 0;
	//}

	UpdateEntityDef( index, &ent );

	idRenderEntityLocal *def = ( index >= 0 && index < entityDefs.Num() ) ? entityDefs[index] : NULL;
	if ( def != NULL ) {
		if ( hasRemoteRenderView ) {
			if ( def->demoRemoteRenderView == NULL ) {
				def->demoRemoteRenderView = new renderView_t;
			}
			*def->demoRemoteRenderView = demoRemoteRenderView;
			def->parms.remoteRenderView = def->demoRemoteRenderView;
			def->hasDemoRemoteRenderView = true;
		} else {
			if ( def->demoRemoteRenderView != NULL ) {
				delete def->demoRemoteRenderView;
				def->demoRemoteRenderView = NULL;
			}
			def->parms.remoteRenderView = NULL;
			def->hasDemoRemoteRenderView = false;
		}
	}

	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_ENTITY_EXTRAS_VERSION ) {
		if ( def != NULL ) {
			R_FreeRenderDemoDecalChain( def->decals );
			if ( def->overlay != NULL ) {
				idRenderModelOverlay::Free( def->overlay );
			}

			def->decals = demoDecals;
			def->overlay = demoOverlay;
			demoDecals = NULL;
			demoOverlay = NULL;
		}
	}

	if ( demoDecals != NULL ) {
		R_FreeRenderDemoDecalChain( demoDecals );
	}
	if ( demoOverlay != NULL ) {
		idRenderModelOverlay::Free( demoOverlay );
	}

	if ( r_showDemo.GetBool() ) {
		common->Printf( "DC_UPDATE_ENTITYDEF: %i = %s\n", index, ent.hModel ? ent.hModel->Name() : "NULL" );
	}
}
