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
#include "Model_local.h"
#include "ModernGLExecutor.h"
#include "RendererMetrics.h"
#include "RendererUpload.h"
#include "ScenePackets.h"
#include "TemporalPresentation.h"
#include "smaa/AreaTex.h"
#include "smaa/SearchTex.h"

idRenderSystemLocal	tr;
idRenderSystem	*renderSystem = &tr;

/*
==========================
R_GetBackEndRendererName
==========================
*/
static const char *R_GetBackEndRendererName( backEndName_t renderer ) {
	switch ( renderer ) {
		case BE_ARB2:
			return "ARB2";
		case BE_MODERN:
			return "Modern";
		case BE_GLES_D3:
			return "GLESD3";
		default:
			return "BAD";
	}
}

/*
==============================
R_IsLegacyBackEndRequest
==============================
*/
static bool R_IsLegacyBackEndRequest( const char *rendererName ) {
	if ( rendererName == NULL || rendererName[0] == '\0' ) {
		return false;
	}

	return idStr::Icmp( rendererName, "arb" ) == 0
		|| idStr::Icmp( rendererName, "nv10" ) == 0
		|| idStr::Icmp( rendererName, "nv20" ) == 0
		|| idStr::Icmp( rendererName, "r200" ) == 0
		|| idStr::Icmp( rendererName, "Cg" ) == 0
		|| idStr::Icmp( rendererName, "exp" ) == 0;
}

/*
==============================
R_RequestBackEndRenderer
==============================
*/
static bool R_ModernBackEndAvailable( void ) {
	return R_ModernGLExecutor_Stats().available;
}

static backEndName_t R_RequestBackEndRenderer( const char *rendererName ) {
	if ( rendererName == NULL || rendererName[0] == '\0' || idStr::Icmp( rendererName, "best" ) == 0 ) {
		return BE_BAD;
	}

	if ( idStr::Icmp( rendererName, "arb2" ) == 0 ) {
		return glConfig.allowARB2Path ? BE_ARB2 : BE_BAD;
	}

	if ( idStr::Icmp( rendererName, "modern" ) == 0 ) {
		return R_ModernBackEndAvailable() ? BE_MODERN : BE_BAD;
	}

#ifdef OPENQ4_RENDERER_GLES_MODULE
	// Only the renderer-gles module compiles the GLES_D3 translation units, so
	// the name resolves nowhere else.
	if ( idStr::Icmp( rendererName, "glesd3" ) == 0 ) {
		return BE_GLES_D3;
	}
#endif

	return BE_BAD;
}

/*
===============================
R_PickBestBackEndRenderer
===============================
*/
static backEndName_t R_PickBestBackEndRenderer() {
	// ARB2 stays the automatic choice wherever it exists. The modern executor
	// is layered over it there and gives up passes it cannot own, which is a
	// strictly better frame than the standalone backend can produce today.
	if ( glConfig.allowARB2Path ) {
		return BE_ARB2;
	}

#ifdef OPENQ4_RENDERER_GLES_MODULE
	// An ES profile: gles_d3 is the back end that renders here. The modern
	// executor's shader library targets desktop GLSL and does not compile as
	// GLSL ES, so BE_MODERN on ES draws nothing -- picking it would hand back a
	// black frame rather than a renderer.
	if ( glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_ES ) {
		return BE_GLES_D3;
	}
#endif

	// No ARB2 -- a desktop core profile. Standalone modern is the only backend
	// left, and it is still better than failing to start.
	if ( R_ModernBackEndAvailable() ) {
		return BE_MODERN;
	}

	return BE_BAD;
}

/*
=========================
R_IsMD5RRuntimeAvailable
=========================
*/
bool R_IsMD5RRuntimeAvailable( void ) {
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	return true;
#else
	return false;
#endif
}

/*
=======================
R_IsMD5RWriteAvailable

openQ4's text/binary MD5R model writer is now implemented in the parser-backed
rvRenderModelMD5R path, so it no longer depends on the original retail
_MD5R_WRITE_SUPPORT macro. World export now round-trips packed MD5RProc
companions when that shared world-buffer state is loaded, while classic .proc
worlds still fall back to the interim classic-payload export until the actual
conversion path is finished.
=======================
*/
bool R_IsMD5RWriteAvailable( void ) {
#if defined( Q4SDK )
	return false;
#else
	return true;
#endif
}

/*
===============================
R_DisableUnavailableMD5RCVar
===============================
*/
void R_DisableUnavailableMD5RCVar( idCVar &cvar, const char *capabilityName ) {
	if ( !cvar.GetBool() || R_IsMD5RRuntimeAvailable() ) {
		return;
	}

	common->Warning(
		"%s requires %s, but this build was compiled without it; forcing %s back to 0",
		cvar.GetName(),
		capabilityName,
		cvar.GetName() );
	cvar.SetBool( false );
	cvar.ClearModified();
}

/*
===============================
idRenderSystemLocal::ExportMD5R
===============================
*/
void idRenderSystemLocal::ExportMD5R( bool compressed ) {
	if ( !R_IsMD5RWriteAvailable() ) {
		common->Warning( "idRenderSystemLocal::ExportMD5R: MD5R export is not available in this build" );
		return;
	}

	for ( int i = 0; i < worlds.Num(); ++i ) {
		if ( worlds[i] == NULL || worlds[i]->mapName.Length() == 0 || worlds[i]->mapName == "<FREED>" ) {
			continue;
		}

		worlds[i]->WriteMD5R( compressed );
	}

	rvRenderModelMD5R::WriteAll( compressed );
}

#ifdef Q4SDK_MD5R
/*
===========================================
idRenderSystemLocal::CopyPrimBatchTriangles
===========================================
*/
void idRenderSystemLocal::CopyPrimBatchTriangles( idDrawVert *destDrawVerts, glIndex_t *destIndices, void *primBatchMesh, void *silTraceVerts ) {
	if ( !R_MD5R_CopyPrimBatchTriangles(
		destDrawVerts,
		destIndices,
		reinterpret_cast<const rvMesh *>( primBatchMesh ),
		reinterpret_cast<const rvSilTraceVertT *>( silTraceVerts ) ) ) {
		common->Error( "idRenderSystemLocal::CopyPrimBatchTriangles: invalid packed MD5R prim-batch mesh state" );
	}
}
#else
#if defined( _MD5R_SUPPORT )
/*
===========================================
idRenderSystemLocal::CopyPrimBatchTriangles
===========================================
*/
void idRenderSystemLocal::CopyPrimBatchTriangles( idDrawVert *destDrawVerts, glIndex_t *destIndices, rvMesh *primBatchMesh, const rvSilTraceVertT *silTraceVerts ) {
	if ( !R_MD5R_CopyPrimBatchTriangles( destDrawVerts, destIndices, primBatchMesh, silTraceVerts ) ) {
		common->Error( "idRenderSystemLocal::CopyPrimBatchTriangles: invalid packed MD5R prim-batch mesh state" );
	}
}
#endif
#endif


/*
=====================
R_PerformanceCounters

This prints both front and back end counters, so it should
only be called when the back end thread is idle.
=====================
*/
static void R_PerformanceCounters( void ) {
	if ( r_showDynamic.GetBool() ) {
		common->Printf( "callback:%i md5:%i dfrmVerts:%i dfrmTris:%i tangTris:%i guis:%i decals:%i\n",
			tr.pc.c_entityDefCallbacks,
			tr.pc.c_generateMd5,
			tr.pc.c_deformedVerts,
			tr.pc.c_deformedIndexes/3,
			tr.pc.c_tangentIndexes/3,
			tr.pc.c_guiSurfs,
			tr.pc.c_numDecalIndexes/3
			); 
	}

	if ( r_showCull.GetBool() ) {
		common->Printf( "%i sin %i sclip  %i sout %i bin %i bout\n",
			tr.pc.c_sphere_cull_in, tr.pc.c_sphere_cull_clip, tr.pc.c_sphere_cull_out, 
			tr.pc.c_box_cull_in, tr.pc.c_box_cull_out );
	}
	
	if ( r_showAlloc.GetBool() ) {
		common->Printf( "alloc:%i free:%i\n", tr.pc.c_alloc, tr.pc.c_free );
	}

	if ( r_showInteractions.GetBool() ) {
		common->Printf( "createInteractions:%i createLightTris:%i createShadowVolumes:%i\n",
			tr.pc.c_createInteractions, tr.pc.c_createLightTris, tr.pc.c_createShadowVolumes );
 	}
	if ( r_showDefs.GetBool() ) {
		common->Printf( "viewEntities:%i  shadowEntities:%i  viewLights:%i\n", tr.pc.c_visibleViewEntities,
			tr.pc.c_shadowViewEntities, tr.pc.c_viewLights );
	}
	if ( r_showUpdates.GetBool() ) {
		common->Printf( "entityUpdates:%i  entityRefs:%i  lightUpdates:%i  lightRefs:%i  snapshotsReused:%i  updatesElided:%i\n",
			tr.pc.c_entityUpdates, tr.pc.c_entityReferences,
			tr.pc.c_lightUpdates, tr.pc.c_lightReferences,
			tr.pc.c_entitySnapshotsReused, tr.pc.c_entityUpdatesElided );
	}
	if ( r_showMemory.GetBool() ) {
		int	m1 = frameData ? frameData->memoryHighwater : 0;
		common->Printf( "frameData: %i (%i)\n", R_CountFrameData(), m1 );
	}
	if ( r_showLightScale.GetBool() ) {
		common->Printf( "lightScale: %f\n", backEnd.pc.maxLightValue );
	}

	memset( &tr.pc, 0, sizeof( tr.pc ) );
	memset( &backEnd.pc, 0, sizeof( backEnd.pc ) );
}



/*
====================
R_IssueRenderCommands

Called by R_EndFrame each frame
====================
*/
static emptyCommand_t *r_deferredCommandHead = NULL;
static emptyCommand_t *r_deferredCommandTail = NULL;

static void *R_GetCommandBufferDeferred( int bytes ) {
	emptyCommand_t *cmd = (emptyCommand_t *)R_FrameAlloc( bytes );
	cmd->next = NULL;

	if ( r_deferredCommandTail != NULL ) {
		r_deferredCommandTail->next = &cmd->commandId;
	} else {
		r_deferredCommandHead = cmd;
	}
	r_deferredCommandTail = cmd;

	return (void *)cmd;
}

static void R_SubmitDeferredCommands( void ) {
	if ( r_deferredCommandHead == NULL ) {
		return;
	}

	frameData->cmdTail->next = &r_deferredCommandHead->commandId;
	frameData->cmdTail = r_deferredCommandTail;
	r_deferredCommandHead = NULL;
	r_deferredCommandTail = NULL;
}

static void R_IssueRenderCommands( void ) {
	R_SubmitDeferredCommands();

	if ( frameData->cmdHead->commandId == RC_NOP
		&& !frameData->cmdHead->next ) {
		// nothing to issue
		return;
	}

	// r_skipBackEnd allows the entire time of the back end
	// to be removed from performance measurements, although
	// nothing will be drawn to the screen.  If the prints
	// are going to a file, or r_skipBackEnd is later disabled,
	// usefull data can be received.

	// r_skipRender is usually more usefull, because it will still
	// draw 2D graphics
	if ( !r_skipBackEnd.GetBool() ) {
		const int submitStart = Sys_Milliseconds();
		RB_ExecuteBackEndCommands( frameData->cmdHead );
		R_RendererMetrics_RecordSubmitMsec( Sys_Milliseconds() - submitStart );
	}

	R_ClearCommandChain();
}

/*
============
R_GetCommandBuffer

Returns memory for a command buffer (stretchPicCommand_t, 
drawSurfsCommand_t, etc) and links it to the end of the
current command chain.
============
*/
void *R_GetCommandBuffer( int bytes ) {
	emptyCommand_t	*cmd;

	cmd = (emptyCommand_t *)R_FrameAlloc( bytes );
	cmd->next = NULL;
	frameData->cmdTail->next = &cmd->commandId;
	frameData->cmdTail = cmd;

	return (void *)cmd;
}


/*
====================
R_ClearCommandChain

Called after every buffer submission
and by R_ToggleSmpFrame
====================
*/
void R_ClearCommandChain( void ) {
	R_ScenePackets_EndFrame();
	r_deferredCommandHead = NULL;
	r_deferredCommandTail = NULL;

	// clear the command chain
	frameData->cmdHead = frameData->cmdTail = (emptyCommand_t *)R_FrameAlloc( sizeof( *frameData->cmdHead ) );
	frameData->cmdHead->commandId = RC_NOP;
	frameData->cmdHead->next = NULL;
}

/*
=================
R_ViewStatistics
=================
*/
static void R_ViewStatistics( viewDef_t *parms ) {
	// report statistics about this view
	if ( !r_showSurfaces.GetBool() ) {
		return;
	}
	int portalSkySurfs = 0;
	int skyboxSurfs = 0;
	int postProcessSurfs = 0;
	for ( int i = 0; i < parms->numDrawSurfs; i++ ) {
		const drawSurf_t *surf = parms->drawSurfs[i];
		if ( surf == NULL || surf->material == NULL ) {
			continue;
		}
		if ( surf->material->IsPortalSky() ) {
			portalSkySurfs++;
		}
		const texgen_t texgen = surf->material->Texgen();
		if ( texgen == TG_SKYBOX_CUBE || texgen == TG_WOBBLESKY_CUBE ) {
			skyboxSurfs++;
		}
		if ( surf->material->GetSort() >= SS_POST_PROCESS ) {
			postProcessSurfs++;
		}
	}
	common->Printf(
		"view:%p flags=0x%x surfs:%i portalSky:%i skybox:%i post:%i subview=%d mirror=%d xray=%d super=%p surface=%p viewID=%d area=%d org=%s fov=%.1f/%.1f viewport=%d,%d %dx%d scissor=%d,%d %dx%d entities=%d lights=%d\n",
		parms,
		parms->renderFlags,
		parms->numDrawSurfs,
		portalSkySurfs,
		skyboxSurfs,
		postProcessSurfs,
		parms->isSubview ? 1 : 0,
		parms->isMirror ? 1 : 0,
		parms->isXraySubview ? 1 : 0,
		parms->superView,
		parms->subviewSurface,
		parms->renderView.viewID,
		parms->areaNum,
		parms->renderView.vieworg.ToString( 0 ),
		parms->renderView.fov_x,
		parms->renderView.fov_y,
		parms->viewport.x1,
		parms->viewport.y1,
		parms->viewport.x2 - parms->viewport.x1 + 1,
		parms->viewport.y2 - parms->viewport.y1 + 1,
		parms->scissor.x1,
		parms->scissor.y1,
		parms->scissor.x2 - parms->scissor.x1 + 1,
		parms->scissor.y2 - parms->scissor.y1 + 1,
		parms->viewEntitys != NULL ? 1 : 0,
		parms->viewLights != NULL ? 1 : 0 );
}

/*
=============
R_AddDrawViewCmd

This is the main 3D rendering command.  A single scene may
have multiple views if a mirror, portal, or dynamic texture is present.
=============
*/
void	R_AddDrawViewCmd( viewDef_t *parms ) {
	drawSurfsCommand_t	*cmd;

	if ( ( parms->renderFlags & RF_DEFER_COMMAND_SUBMIT ) != 0 ) {
		cmd = (drawSurfsCommand_t *)R_GetCommandBufferDeferred( sizeof( *cmd ) );
	} else {
		R_SubmitDeferredCommands();
		cmd = (drawSurfsCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	}
	cmd->commandId = RC_DRAW_VIEW;

	cmd->viewDef = parms;

	if ( parms->viewEntitys ) {
		// save the command for r_lockSurfaces debugging
		tr.lockSurfacesCmd = *cmd;
	}

	tr.pc.c_numViews++;

	R_ViewStatistics( parms );
}

/*
=============
R_AddSpecialEffects
=============
*/
void R_AddSpecialEffects( viewDef_t *parms ) {
	drawSurfsCommand_t *cmd;
	int activeMask;

	if ( parms == NULL ) {
		return;
	}

	activeMask = tr.specialEffectsEnabled;
	if ( r_forceSpecialEffects.GetInteger() > 0 ) {
		activeMask = r_forceSpecialEffects.GetInteger();
	}

	if ( ( activeMask & ( SPECIAL_EFFECT_BLUR | SPECIAL_EFFECT_AL ) ) == 0 ) {
		return;
	}
	if ( ( activeMask & SPECIAL_EFFECT_AL ) != 0
			&& tr.specialALLightImage == NULL && globalImages != NULL ) {
		tr.specialALLightImage = globalImages->ImageFromFile(
				"gfx/lights/round.tga", TF_LINEAR, TR_CLAMP, TD_DEFAULT );
	}

	// Portal-sky cameras and other negative view IDs submit their own scene renders and
	// should never inherit fullscreen player special effects.
	if ( parms->renderView.viewID < 0 ) {
		return;
	}

	if ( ( parms->renderFlags & RF_DEFER_COMMAND_SUBMIT ) != 0 ) {
		cmd = (drawSurfsCommand_t *)R_GetCommandBufferDeferred(
				sizeof( *cmd ) );
	} else {
		// Keep this controller command on the same side of the deferred-view
		// flush as its RC_DRAW_VIEW partner. Without the flush, a pending
		// portal-sky/subview draw can land between the special command and the
		// primary view it captures.
		R_SubmitDeferredCommands();
		cmd = (drawSurfsCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	}
	cmd->commandId = RC_DRAW_SPECIAL_EFFECTS;
	cmd->viewDef = parms;
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddSpecialEffects( parms, activeMask );
	}
}


//=================================================================================


/*
======================
R_LockSurfaceScene

r_lockSurfaces allows a developer to move around
without changing the composition of the scene, including
culling.  The only thing that is modified is the
view position and axis, no front end work is done at all


Add the stored off command again, so the new rendering will use EXACTLY
the same surfaces, including all the culling, even though the transformation
matricies have been changed.  This allow the culling tightness to be
evaluated interactively.
======================
*/
void R_LockSurfaceScene( viewDef_t *parms ) {
	drawSurfsCommand_t	*cmd;
	viewEntity_t			*vModel;

	// set the matrix for world space to eye space
	R_SetViewMatrix( parms );
	tr.lockSurfacesCmd.viewDef->worldSpace = parms->worldSpace;
	
	// update the view origin and axis, and all
	// the entity matricies
	for( vModel = tr.lockSurfacesCmd.viewDef->viewEntitys ; vModel ; vModel = vModel->next ) {
		myGlMultMatrix( vModel->modelMatrix, 
			tr.lockSurfacesCmd.viewDef->worldSpace.modelViewMatrix,
			vModel->modelViewMatrix );
	}

	// add the stored off surface commands again
	if ( ( parms->renderFlags & RF_DEFER_COMMAND_SUBMIT ) != 0 ) {
		cmd = (drawSurfsCommand_t *)R_GetCommandBufferDeferred( sizeof( *cmd ) );
	} else {
		R_SubmitDeferredCommands();
		cmd = (drawSurfsCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	}
	*cmd = tr.lockSurfacesCmd;
}

/*
=============
R_CheckCvars

See if some cvars that we watch have changed
=============
*/
static void R_CheckCvars( void ) {
	// texture reduction cvars only reach the GPU through a reload
	globalImages->CheckCvars();

	// gamma stuff
	if ( r_gamma.IsModified() || r_brightness.IsModified() ) {
		r_gamma.ClearModified();
		r_brightness.ClearModified();
		R_SetColorMappings();
	}

	// check for changes to logging state
	GLimp_EnableLogging( r_logFile.GetInteger() != 0 );
}

/*
=============
idRenderSystemLocal::idRenderSystemLocal
=============
*/
idRenderSystemLocal::idRenderSystemLocal( void ) {
	Clear();
}

/*
=============
idRenderSystemLocal::~idRenderSystemLocal
=============
*/
idRenderSystemLocal::~idRenderSystemLocal( void ) {
}

/*
=============
idRenderSystemLocal::ResetSpecialEffects
=============
*/
void idRenderSystemLocal::ResetSpecialEffects( void ) {
	specialEffectsEnabled = 0;
	memset( specialEffectParms, 0, sizeof( specialEffectParms ) );

	// Stock Quake 4 blur defaults from the legacy rvspecial pipeline.
	specialEffectParms[ SPECIAL_EFFECT_BLUR ][0] = 0.694f;
	specialEffectParms[ SPECIAL_EFFECT_BLUR ][1] = 0.694f;
	specialEffectParms[ SPECIAL_EFFECT_BLUR ][2] = 0.694f;
	specialEffectParms[ SPECIAL_EFFECT_BLUR ][3] = 1.0f;
	specialEffectParms[ SPECIAL_EFFECT_BLUR ][4] = 4.0f;
	specialEffectParms[ SPECIAL_EFFECT_BLUR ][5] = 0.31f;
	specialEffectParms[ SPECIAL_EFFECT_BLUR ][6] = 0.5f;
	specialEffectParms[ SPECIAL_EFFECT_BLUR ][7] = 500.0f;
}

/*
=============
idRenderSystemLocal::SetSpecialEffect
=============
*/
void idRenderSystemLocal::SetSpecialEffect( ESpecialEffectType Which, bool Enabled ) {
	if ( Which <= SPECIAL_EFFECT_NONE || Which >= SPECIAL_EFFECT_MAX ) {
		return;
	}

	if ( Enabled ) {
		specialEffectsEnabled |= Which;
		// Alpha Labs' projected overlay is controller-owned rather than a
		// normal world material, so make its stock sprite resident while this
		// call is still on the front end. Vulkan can then consume the queued
		// command without attempting an image load during command recording.
		if ( Which == SPECIAL_EFFECT_AL && specialALLightImage == NULL
				&& globalImages != NULL ) {
			specialALLightImage = globalImages->ImageFromFile(
					"gfx/lights/round.tga", TF_LINEAR, TR_CLAMP, TD_DEFAULT );
		}
	} else {
		specialEffectsEnabled &= ~Which;
	}
}

/*
=============
idRenderSystemLocal::SetSpecialEffectParm
=============
*/
void idRenderSystemLocal::SetSpecialEffectParm( ESpecialEffectType Which, int Parm, float Value ) {
	if ( Which <= SPECIAL_EFFECT_NONE || Which >= SPECIAL_EFFECT_MAX ) {
		return;
	}
	if ( Parm < 0 || Parm >= MAX_ENTITY_SHADER_PARMS ) {
		return;
	}

	specialEffectParms[ Which ][ Parm ] = Value;
}

void idRenderSystemLocal::SetPortalSkyCaptureViewCallback( renderPortalSkyCaptureViewCallback_t callback ) {
	portalSkyCaptureViewCallback = callback;
}

/*
=============
idRenderSystemLocal::ShutdownSpecialEffects
=============
*/
void idRenderSystemLocal::ShutdownSpecialEffects( void ) {
	if ( specialBlurDepthRenderTexture != NULL ) {
		DestroyRenderTexture( specialBlurDepthRenderTexture );
		specialBlurDepthRenderTexture = NULL;
	}
	if ( specialBlurRenderTexture != NULL ) {
		DestroyRenderTexture( specialBlurRenderTexture );
		specialBlurRenderTexture = NULL;
	}
	if ( specialALDepthRenderTexture != NULL ) {
		DestroyRenderTexture( specialALDepthRenderTexture );
		specialALDepthRenderTexture = NULL;
	}

	specialBlurDepthImage = NULL;
	specialBlurDepthStencilImage = NULL;
	specialBlurImage = NULL;
	specialALDepthImage = NULL;
	specialALDepthStencilImage = NULL;
	specialALLightImage = NULL;

	ResetSpecialEffects();
}

/*
=============
idRenderSystemLocal::CaptureDepthRenderToImage
=============
*/
void idRenderSystemLocal::CaptureDepthRenderToImage( const char *imageName ) {
	if ( !glConfig.isInitialized ) {
		return;
	}

	guiModel->EmitFullScreen();
	guiModel->Clear();

	idImage *image = globalImages->GetImage( imageName );
	if ( image == NULL ) {
		idImageOpts opts;
		memset( &opts, 0, sizeof( opts ) );
		opts.textureType = TT_2D;
		opts.format = FMT_DEPTH_STENCIL;
		opts.width = Max( 1, renderCrops[ currentRenderCrop ].width );
		opts.height = Max( 1, renderCrops[ currentRenderCrop ].height );
		opts.numLevels = 1;
		opts.numMSAASamples = 0;
		opts.isPersistant = true;
		image = CreateImage( imageName, &opts, TF_NEAREST );
	}
	if ( image == NULL ) {
		return;
	}

	renderCrop_t *rc = &renderCrops[currentRenderCrop];

	copyRenderCommand_t *cmd = (copyRenderCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_COPY_RENDER;
	cmd->x = rc->x;
	cmd->y = rc->y;
	cmd->imageWidth = rc->width;
	cmd->imageHeight = rc->height;
	cmd->image = image;
	cmd->cubeFace = 0;
	cmd->copyDepth = true;
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddCopyRender( image, cmd->x, cmd->y,
			cmd->imageWidth, cmd->imageHeight, cmd->cubeFace, cmd->copyDepth );
	}

	guiModel->Clear();
}

/*
=============
idRenderSystemLocal::EmitFullscreenSpecialEffects
=============
*/
void idRenderSystemLocal::EmitFullscreenSpecialEffects( const renderView_t *renderView ) {
	if ( !glConfig.isInitialized || renderView == NULL || guiModel == NULL ) {
		return;
	}

	// Portal-sky cameras submit their own RenderScene calls and should never inherit
	// fullscreen player post-process.
	if ( renderView->viewID < 0 ) {
		return;
	}

	if ( ( specialEffectsEnabled & SPECIAL_EFFECT_BLUR ) == 0 ) {
		return;
	}

	const idMaterial *blurMaterial = declManager->FindMaterial( "postprocess/blur", false );
	if ( blurMaterial == NULL || blurMaterial->TestMaterialFlag( MF_DEFAULTED ) || !blurMaterial->IsDrawn() ) {
		return;
	}

	const bool previousUIViewportMode = GetUseUIViewportFor2D();
	SetUseUIViewportFor2D( false );

	CaptureRenderToImage( "_postProcessAlbedo0" );
	CaptureDepthRenderToImage( "_forwardRenderResolvedDepth" );
	ClearRenderTarget( false, true, 1.0f, 0.0f, 0.0f, 0.0f );

	float savedShaderParms[ MAX_GLOBAL_SHADER_PARMS ];
	memcpy( savedShaderParms, primaryRenderView.shaderParms, sizeof( savedShaderParms ) );
	memcpy( primaryRenderView.shaderParms, specialEffectParms[ SPECIAL_EFFECT_BLUR ], sizeof( primaryRenderView.shaderParms ) );

	DrawStretchPic(
		0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT,
		0.0f, 1.0f, 1.0f, 0.0f,
		blurMaterial );
	guiModel->EmitFullScreen();
	guiModel->Clear();

	memcpy( primaryRenderView.shaderParms, savedShaderParms, sizeof( savedShaderParms ) );
	SetUseUIViewportFor2D( previousUIViewportMode );
}

/*
=============
SetColor

This can be used to pass general information to the current material, not
just colors
=============
*/
void idRenderSystemLocal::SetColor( const idVec4 &rgba ) {
	guiModel->SetColor( rgba[0], rgba[1], rgba[2], rgba[3] );
}


/*
=============
SetColor4
=============
*/
void idRenderSystemLocal::SetColor4( float r, float g, float b, float a ) {
	guiModel->SetColor( r, g, b, a );
}

/*
=============
DrawStretchPic
=============
*/
void idRenderSystemLocal::DrawStretchPic( const idDrawVert *verts, const glIndex_t *indexes, int vertCount, int indexCount, const idMaterial *material, 
									   bool clip, float min_x, float min_y, float max_x, float max_y ) {
	guiModel->DrawStretchPic( verts, indexes, vertCount, indexCount, material,
		clip, min_x, min_y, max_x, max_y );
}

/*
=============
DrawStretchPic

x/y/w/h are in the 0,0 to 640,480 range
=============
*/
void idRenderSystemLocal::DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *material ) {
	guiModel->DrawStretchPic( x, y, w, h, s1, t1, s2, t2, material );
}

/*
=============
GetMaterialStageImageInfo

Binds the stage's image and reports its uploaded format facts so engine-side
self-tests never need renderer-internal image types.
=============
*/
bool idRenderSystemLocal::GetMaterialStageImageInfo( const idMaterial *material, int stageIndex, materialImageInfo_t &info ) {
	memset( &info, 0, sizeof( info ) );
	if ( material == NULL || stageIndex < 0 || stageIndex >= material->GetNumStages() ) {
		return false;
	}
	const shaderStage_t *stage = material->GetStage( stageIndex );
	if ( stage == NULL || stage->texture.image == NULL ) {
		return false;
	}
	idImage *image = stage->texture.image;
	image->Bind();
	const idImageOpts &opts = image->GetOpts();
	info.numLevels = opts.numLevels;
	info.isDXT1Compressed = ( opts.format == FMT_DXT1 );
	info.usesGreenAlphaColorFormat = ( opts.colorFormat == CFM_GREEN_ALPHA );
	return true;
}

/*
=============
UploadMaterialStageScratchImage

Uploads raw RGBA8 pixels into a material stage's image for dynamically
generated GUI content (player stat graphs).
=============
*/
bool idRenderSystemLocal::UploadMaterialStageScratchImage( const idMaterial *material, int stageIndex, const byte *data, int width, int height ) {
	if ( material == NULL || data == NULL || stageIndex < 0 || stageIndex >= material->GetNumStages() ) {
		return false;
	}
	const shaderStage_t *stage = material->GetStage( stageIndex );
	if ( stage == NULL || stage->texture.image == NULL ) {
		return false;
	}
	stage->texture.image->UploadScratch( data, width, height );
	return true;
}

/*
=============
SetLoadingScreenSwapIntervalBypass
=============
*/
void idRenderSystemLocal::SetLoadingScreenSwapIntervalBypass( bool active ) {
	R_SetLoadingScreenSwapIntervalBypass( active );
}

/*
=============
AllocMaterialDecl

The decl manager registers DECL_MATERIAL with a trampoline through this
virtual so framework code never instantiates renderer-owned types directly.
=============
*/
idDecl *idRenderSystemLocal::AllocMaterialDecl( void ) {
	return new idMaterial;
}

/*
=============
PreloadImage

Uploads an image file ahead of first use so menu backgrounds present without
a first-frame load hitch.
=============
*/
void idRenderSystemLocal::PreloadImage( const char *name ) {
	if ( globalImages == NULL || name == NULL || name[ 0 ] == '\0' ) {
		return;
	}
	globalImages->ImageFromFile( name, TF_DEFAULT, TR_CLAMP, TD_DEFAULT, CF_2D, false );
}

/*
=============
GetDefaultLightGridBakeOptions
=============
*/
void idRenderSystemLocal::GetDefaultLightGridBakeOptions( lightGridBakeOptions_t &options ) {
	R_SetDefaultLightGridBakeOptions( options );
}

/*
=============
HasPrimaryRenderView
=============
*/
bool idRenderSystemLocal::HasPrimaryRenderView( void ) {
	return primaryWorld != NULL && primaryView != NULL;
}

/*
=============
GetCurrentLightGridBakeInfo

Reports the primary world's map name and, after setting up the per-area
grids under the given options, which portal areas hold valid grid points.
The grid setup mutates the world's light-grid layout exactly as the
session-side implementation previously did.
=============
*/
bool idRenderSystemLocal::GetCurrentLightGridBakeInfo( const lightGridBakeOptions_t &options, char *mapName, int mapNameLength, int *validAreaIndices, int maxAreaIndices, int &numValidAreaIndices ) {
	numValidAreaIndices = 0;
	if ( mapName != NULL && mapNameLength > 0 ) {
		mapName[ 0 ] = '\0';
	}
	if ( primaryWorld == NULL ) {
		return false;
	}

	idRenderWorldLocal *world = primaryWorld;
	if ( mapName != NULL && mapNameLength > 0 ) {
		idStr::Copynz( mapName, world->mapName.c_str(), mapNameLength );
	}

	for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
		world->portalAreas[ areaIndex ].lightGrid.SetupGrid(
			world->portalAreas[ areaIndex ].globalBounds,
			world,
			options.gridSize,
			areaIndex,
			world->numPortalAreas,
			options.maxProbes,
			true );
		if ( world->portalAreas[ areaIndex ].lightGrid.CountValidGridPoints() > 0 ) {
			if ( validAreaIndices != NULL && numValidAreaIndices < maxAreaIndices ) {
				validAreaIndices[ numValidAreaIndices ] = areaIndex;
			}
			numValidAreaIndices++;
		}
	}
	return true;
}

/*
=============
LightGridFileMatchesBakeOptions
=============
*/
bool idRenderSystemLocal::LightGridFileMatchesBakeOptions( const char *name, const lightGridBakeOptions_t &options ) {
	return primaryWorld != NULL && R_LightGridFileMatchesBakeOptions( name, options, primaryWorld );
}

/*
=============
LightGridPackFileMatchesBakeOptions
=============
*/
bool idRenderSystemLocal::LightGridPackFileMatchesBakeOptions( const char *name, const lightGridBakeOptions_t &options ) {
	return primaryWorld != NULL && R_LightGridPackFileMatchesBakeOptions( name, options, primaryWorld );
}

/*
=============
BakeCurrentLightGrids
=============
*/
bool idRenderSystemLocal::BakeCurrentLightGrids( const lightGridBakeOptions_t &options, const char *jobName ) {
	return R_BakeCurrentLightGrids( options, jobName );
}

void idRenderSystemLocal::SetUseUIViewportFor2D( bool enable ) {
	if ( useUIViewportFor2D == enable ) {
		return;
	}

	// Viewport selection for 2D is applied when guiModel is emitted.
	// Flush pending 2D so already-queued surfaces keep the previous mode.
	FlushGui();

	useUIViewportFor2D = enable;
}

bool idRenderSystemLocal::GetUseUIViewportFor2D( void ) const {
	return useUIViewportFor2D;
}

/*
=============
DrawStretchTri

x/y/w/h are in the 0,0 to 640,480 range
=============
*/
void idRenderSystemLocal::DrawStretchTri( idVec2 p1, idVec2 p2, idVec2 p3, idVec2 t1, idVec2 t2, idVec2 t3, const idMaterial *material ) {
	tr.guiModel->DrawStretchTri( p1, p2, p3, t1, t2, t3, material );
}

void idRenderSystemLocal::FlushGui() {
	if ( !glConfig.isInitialized || guiModel == NULL ) {
		return;
	}

	guiModel->EmitFullScreen();
	guiModel->Clear();
}

/*
=============
GlobalToNormalizedDeviceCoordinates
=============
*/
void idRenderSystemLocal::GlobalToNormalizedDeviceCoordinates( const idVec3 &global, idVec3 &ndc ) {
	R_GlobalToNormalizedDeviceCoordinates( global, ndc );
}

/*
=============
GlobalToNormalizedDeviceCoordinates
=============
*/
void idRenderSystemLocal::GetGLSettings( int& width, int& height ) {
	width = glConfig.vidWidth;
	height = glConfig.vidHeight;
}

/*
=====================
idRenderSystemLocal::DrawSmallChar

small chars are drawn at native screen resolution
=====================
*/
void idRenderSystemLocal::DrawSmallChar( int x, int y, int ch, const idMaterial *material ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -SMALLCHAR_HEIGHT ) {
		return;
	}

	row = ch >> 4;
	col = ch & 15;

	frow = row * 0.0625f;
	fcol = col * 0.0625f;
	size = 0.0625f;

	DrawStretchPic( x, y, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT,
					   fcol, frow, 
					   fcol + size, frow + size, 
					   material );
}

/*
==================
idRenderSystemLocal::DrawSmallString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void idRenderSystemLocal::DrawSmallStringExt( int x, int y, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material ) {
	idVec4		color;
	const unsigned char	*s;
	int			xx;

	// draw the colored text
	s = (const unsigned char*)string;
	xx = x;
	SetColor( setColor );
	while ( *s ) {
		idVec4 parsedColor;
		bool resetToDefault = false;
		const int colorEscapeLength = idStr::ColorEscapeLength( reinterpret_cast<const char *>( s ), &parsedColor, &resetToDefault );
		if ( colorEscapeLength > 0 ) {
			if ( !forceColor ) {
				if ( resetToDefault ) {
					SetColor( setColor );
				} else {
					color = parsedColor;
					color[3] = setColor[3];
					SetColor( color );
				}
			}
			s += colorEscapeLength;
			continue;
		}
		DrawSmallChar( xx, y, *s, material );
		xx += SMALLCHAR_WIDTH;
		s++;
	}
	SetColor( colorWhite );
}

/*
=====================
idRenderSystemLocal::DrawBigChar
=====================
*/
void idRenderSystemLocal::DrawBigChar( int x, int y, int ch, const idMaterial *material ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -BIGCHAR_HEIGHT ) {
		return;
	}

	row = ch >> 4;
	col = ch & 15;

	frow = row * 0.0625f;
	fcol = col * 0.0625f;
	size = 0.0625f;

	DrawStretchPic( x, y, BIGCHAR_WIDTH, BIGCHAR_HEIGHT,
					   fcol, frow, 
					   fcol + size, frow + size, 
					   material );
}

/*
==================
idRenderSystemLocal::DrawBigString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void idRenderSystemLocal::DrawBigStringExt( int x, int y, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material ) {
	idVec4		color;
	const char	*s;
	int			xx;

	// draw the colored text
	s = string;
	xx = x;
	SetColor( setColor );
	while ( *s ) {
		idVec4 parsedColor;
		bool resetToDefault = false;
		const int colorEscapeLength = idStr::ColorEscapeLength( s, &parsedColor, &resetToDefault );
		if ( colorEscapeLength > 0 ) {
			if ( !forceColor ) {
				if ( resetToDefault ) {
					SetColor( setColor );
				} else {
					color = parsedColor;
					color[3] = setColor[3];
					SetColor( color );
				}
			}
			s += colorEscapeLength;
			continue;
		}
		DrawBigChar( xx, y, *s, material );
		xx += BIGCHAR_WIDTH;
		s++;
	}
	SetColor( colorWhite );
}

//======================================================================================

/*
==================
SetBackEndRenderer

Check for changes in the back end renderSystem, possibly invalidating cached data
==================
*/
void idRenderSystemLocal::SetBackEndRenderer() {
	if ( !r_renderer.IsModified() ) {
		return;
	}

	const char *requestedRendererName = r_renderer.GetString();

	if ( !glConfig.isInitialized ) {
		// idRenderSystemLocal::Init() still calls this before OpenGL capability
		// probing has run. Preserve the legacy optimistic ARB2 placeholder there
		// and defer the real request/fallback resolution until R_InitOpenGL().
		backEndRenderer = BE_ARB2;
		backEndRendererHasVertexPrograms = true;
		backEndRendererMaxLight = 999.0f;
		return;
	}

	bool oldVPstate = backEndRendererHasVertexPrograms;

	backEndRenderer = R_RequestBackEndRenderer( requestedRendererName );
	if ( backEndRenderer == BE_BAD ) {
		backEndRenderer = R_PickBestBackEndRenderer();
	}
	if ( backEndRenderer == BE_BAD ) {
		common->FatalError( "SetBackEndRenderer: no supported renderSystem back end is available" );
	}

	backEndRendererHasVertexPrograms = false;
	backEndRendererMaxLight = 1.0;

	switch( backEndRenderer ) {
	case BE_ARB2:
		common->Printf( "using ARB2 renderSystem\n" );
		backEndRendererHasVertexPrograms = true;
		backEndRendererMaxLight = 999;
		if ( !glConfig.preferSimpleLighting ) {
			r_lightDetailLevel.SetFloat( 0.0f );
		}
		break;
	case BE_MODERN:
		common->Printf( "using standalone modern renderSystem (no ARB2 bridge)\n" );
		// Vertex programs in the sense the legacy path means -- ARB assembly
		// vertex programs -- do not exist here. The flag drives interaction
		// data layout and vertex-cache invalidation, and the modern executor
		// builds its own vertex input, so it stays false.
		backEndRendererHasVertexPrograms = false;
		backEndRendererMaxLight = 999;
		break;
	case BE_GLES_D3:
		common->Printf( "using Doom 3-shaped GLES 3.0 renderSystem (gles_d3)\n" );
		// Same reasoning as BE_MODERN: no ARB assembly vertex programs exist on
		// an ES context, and this backend builds its own vertex input.
		backEndRendererHasVertexPrograms = false;
		backEndRendererMaxLight = 999;
		break;
	default:
		common->FatalError( "SetbackEndRenderer: bad back end" );
	}

	r_actualRenderer.SetString( R_GetBackEndRendererName( backEndRenderer ) );

	if ( requestedRendererName != NULL && requestedRendererName[0] != '\0' ) {
		if ( R_IsLegacyBackEndRequest( requestedRendererName ) ) {
			common->Warning(
				"r_renderer \"%s\" requested, but openQ4 only ships the ARB2 backend; using %s instead",
				requestedRendererName,
				r_actualRenderer.GetString() );
		} else if ( idStr::Icmp( requestedRendererName, "best" ) != 0 && idStr::Icmp( requestedRendererName, r_actualRenderer.GetString() ) != 0 ) {
			common->Warning(
				"r_renderer \"%s\" is unavailable on this renderer; using %s instead",
				requestedRendererName,
				r_actualRenderer.GetString() );
		}
	}

	// clear the vertex cache if we are changing between
	// using vertex programs and not, because specular and
	// shadows will be different data
	if ( oldVPstate != backEndRendererHasVertexPrograms ) {
		vertexCache.PurgeAll();
		if ( primaryWorld ) {
			primaryWorld->FreeInteractions();
		}
	}

	r_renderer.ClearModified();
}

/*
====================
BeginFrame
====================
*/
void idRenderSystemLocal::BeginFrame( int windowWidth, int windowHeight ) {
	setBufferCommand_t	*cmd;

	if ( !glConfig.isInitialized ) {
		return;
	}
	R_RendererMetrics_MarkCpuFrameBegin();

	// determine which back end we will use
	SetBackEndRenderer();

	// Interaction shadow state (stencil volume creation, shadow-map caster
	// admission, translucent caster policy) is baked into cached interactions
	// at creation time; toggling the cvars that drive it must rebuild them,
	// or the stale decisions persist until the entity or light is otherwise
	// modified ("shadows did not change after toggling the cvar" reports).
	if ( r_shadows.IsModified()
		|| r_useShadowMap.IsModified()
		|| r_useOptimizedShadows.IsModified()
		|| r_stencilTranslucentShadows.IsModified()
		|| r_lod_shadows.IsModified()
		|| r_lod_shadows_percent.IsModified()
		|| r_shadowMapTranslucentMoments.IsModified()
		|| r_shadowMapPointLights.IsModified()
		|| r_shadowMapConservativeCasters.IsModified()
		|| r_shadowMapSkipStencilShadows.IsModified() ) {
		r_shadows.ClearModified();
		r_useShadowMap.ClearModified();
		r_useOptimizedShadows.ClearModified();
		r_stencilTranslucentShadows.ClearModified();
		r_lod_shadows.ClearModified();
		r_lod_shadows_percent.ClearModified();
		r_shadowMapTranslucentMoments.ClearModified();
		r_shadowMapPointLights.ClearModified();
		r_shadowMapConservativeCasters.ClearModified();
		r_shadowMapSkipStencilShadows.ClearModified();
		if ( primaryWorld != NULL ) {
			primaryWorld->FreeInteractions();
		}
	}

	guiModel->Clear();
	useUIViewportFor2D = true;
	activeRenderTexture = NULL;

	// for the larger-than-window tiled rendering screenshots
	if ( tiledViewport[0] ) {
		windowWidth = tiledViewport[0];
		windowHeight = tiledViewport[1];
	}

	glConfig.vidWidth = windowWidth;
	glConfig.vidHeight = windowHeight;
	{
		const int safeWidth = Max( windowWidth, 1 );
		const int safeHeight = Max( windowHeight, 1 );
		postProcessTexelSize.Set(
			1.0f / static_cast<float>( safeWidth ),
			1.0f / static_cast<float>( safeHeight ),
			static_cast<float>( safeWidth ),
			static_cast<float>( safeHeight ) );
		postProcessSourceColorSpace.Set( 0.0f, 2.2f, 0.0f, 0.0f );
		postProcessSMAAQuality.Set( 0.0f, 0.10f, 8.0f, 2.0f );
	}

	// Keep the 2D viewport anchored to a valid region of the current framebuffer.
	// Platform backends can narrow this to a monitor sub-rect (top-left origin).
	if ( glConfig.uiViewportWidth <= 0 || glConfig.uiViewportHeight <= 0 ) {
		glConfig.uiViewportX = 0;
		glConfig.uiViewportY = 0;
		glConfig.uiViewportWidth = windowWidth;
		glConfig.uiViewportHeight = windowHeight;
	} else {
		int uiX = glConfig.uiViewportX;
		int uiY = glConfig.uiViewportY;
		int uiWidth = glConfig.uiViewportWidth;
		int uiHeight = glConfig.uiViewportHeight;

		if ( uiX < 0 ) {
			uiWidth += uiX;
			uiX = 0;
		}
		if ( uiY < 0 ) {
			uiHeight += uiY;
			uiY = 0;
		}
		if ( uiX >= windowWidth || uiY >= windowHeight || uiWidth <= 0 || uiHeight <= 0 ) {
			uiX = 0;
			uiY = 0;
			uiWidth = windowWidth;
			uiHeight = windowHeight;
		} else {
			if ( uiX + uiWidth > windowWidth ) {
				uiWidth = windowWidth - uiX;
			}
			if ( uiY + uiHeight > windowHeight ) {
				uiHeight = windowHeight - uiY;
			}
		}

		glConfig.uiViewportX = uiX;
		glConfig.uiViewportY = uiY;
		glConfig.uiViewportWidth = uiWidth;
		glConfig.uiViewportHeight = uiHeight;
	}

	renderCrops[0].x = 0;
	renderCrops[0].y = 0;
	renderCrops[0].width = windowWidth;
	renderCrops[0].height = windowHeight;
	currentRenderCrop = 0;

	// screenFraction mode 0 keeps the legacy cropped-viewport behavior used for
	// quick fill-rate testing below native resolution. Supersampling above native
	// is handled by the main-scene render target path so the back buffer stays at
	// the display size.
	//
	// Modes 1 and 2 on ES are NOT applied here. A crop pushed in BeginFrame is the
	// whole frame's coordinate system -- menu, HUD and scene alike -- so scaling it
	// costs the UI its native resolution, which is the one part of a phone frame
	// that cannot afford to be soft. PushSceneResolutionScale wraps the world
	// render instead; see idRenderWorldLocal::RenderScene.
	resolutionScaleCropActive = false;
	resolutionScaleWidth = 0;
	resolutionScaleHeight = 0;
	const int screenFraction = idMath::ClampInt( 10, 200, r_screenFraction.GetInteger() );
	if ( !R_TemporalPresentation_DynamicResolutionRequested()
			&& !R_TemporalPresentation_TemporalAARequested()
			&& !R_TemporalPresentation_ScreenSpaceEffectsRequested()
			&& screenFraction < 100 && r_resolutionScaleMode.GetInteger() == 0 ) {
		int	w = SCREEN_WIDTH * screenFraction / 100.0f;
		int h = SCREEN_HEIGHT * screenFraction / 100.0f;
		CropRenderSize( w, h );
	}


	// this is the ONLY place this is modified
	frameCount++;
	R_RendererMetrics_BeginFrame( frameCount );
	R_TemporalPresentation_BeginFrame( windowWidth, windowHeight, takingScreenshot );
	R_RendererUpload_BeginFrame( frameCount );
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_BeginFrame();
	}

	// just in case we did a common->Error while this
	// was set
	guiRecursionLevel = 0;

	// the first rendering will be used for commands like
	// screenshot, rather than a possible subsequent remote
	// or mirror render
//	primaryWorld = NULL;

	// set the time for shader effects in 2D rendering
	SetFrameShaderTime( eventLoop->Milliseconds() );

	//
	// draw buffer stuff
	//
	cmd = (setBufferCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_SET_BUFFER;
	cmd->frameCount = frameCount;

	if ( r_frontBuffer.GetBool() ) {
		cmd->buffer = (int)GL_FRONT;
	} else {
		cmd->buffer = (int)GL_BACK;
	}
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddCommandOnly();
	}
}

void idRenderSystemLocal::WriteDemoPics() {
	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_GUI_MODEL );
	guiModel->WriteToDemo( session->writeDemo );
}

void idRenderSystemLocal::DrawDemoPics() {
	demoGuiModel->EmitFullScreen();
}

void idRenderSystemLocal::SetFrameShaderTime( int timeMsec ) {
	frameShaderTimeMsec = timeMsec;
	frameShaderTime = static_cast<float>( timeMsec ) * 0.001f;
}

/*
=============
EndFrame

Returns the number of msec spent in the back end
=============
*/
void idRenderSystemLocal::EndFrame( int *frontEndMsec, int *backEndMsec ) {
	emptyCommand_t *cmd;

	if ( !glConfig.isInitialized ) {
		return;
	}

	// close any gui drawing
	FlushGui();

	// check for dynamic changes that require some initialization
	R_CheckCvars();

    // check for errors
	GL_CheckErrors();

	// add the swapbuffers command
	cmd = (emptyCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_SWAP_BUFFERS;
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddPresent();
	}

	// start the back end up again with the new command list
	R_IssueRenderCommands();
	ProcessPendingRenderTextureDeletes();

	// save out timing information after the backend has consumed the frame.
	if ( frontEndMsec ) {
		*frontEndMsec = pc.frontEndMsec;
	}
	if ( backEndMsec ) {
		*backEndMsec = backEnd.pc.msec;
	}

	R_RendererMetrics_EndFrame(
		pc.frontEndMsec,
		backEnd.pc.msec,
		pc.c_numViews,
		pc.c_visibleViewEntities,
		pc.c_viewLights,
		backEnd.pc.c_drawElements,
		backEnd.pc.c_surfaces,
		backEnd.pc.c_vertexes,
		backEnd.pc.c_indexes );

	// print any other statistics and clear all of them
	R_PerformanceCounters();

	// use the other buffers next frame, because another CPU
	// may still be rendering into the current buffers
	R_ToggleSmpFrame();

	// we can now release the vertexes used this frame
	vertexCache.EndFrame();
	R_RendererUpload_EndFrame();

	if ( session->writeDemo ) {
		session->writeDemo->WriteInt( DS_RENDER );
		session->writeDemo->WriteInt( DC_END_FRAME );
		if ( r_showDemo.GetBool() ) {
			common->Printf( "write DC_END_FRAME\n" );
		}
	}

}

/*
===============
idRenderSystemLocal::ProcessPendingRenderTextureDeletes
===============
*/
void idRenderSystemLocal::ProcessPendingRenderTextureDeletes( void ) {
	if ( pendingRenderTextureDeletes.Num() <= 0 ) {
		return;
	}

	for ( int i = 0; i < pendingRenderTextureDeletes.Num(); i++ ) {
		delete pendingRenderTextureDeletes[i];
	}

	pendingRenderTextureDeletes.Clear();
}

/*
=====================
RenderViewToViewport

Converts from SCREEN_WIDTH / SCREEN_HEIGHT coordinates to current cropped pixel coordinates
=====================
*/
void idRenderSystemLocal::RenderViewToViewport( const renderView_t *renderView, idScreenRect *viewport ) {
	renderCrop_t	*rc = &renderCrops[currentRenderCrop];

	float wRatio = (float)rc->width / SCREEN_WIDTH;
	float hRatio = (float)rc->height / SCREEN_HEIGHT;

	viewport->x1 = idMath::Ftoi( rc->x + renderView->x * wRatio );
	viewport->x2 = idMath::Ftoi( rc->x + floor( ( renderView->x + renderView->width ) * wRatio + 0.5f ) - 1 );
	viewport->y1 = idMath::Ftoi( ( rc->y + rc->height ) - floor( ( renderView->y + renderView->height ) * hRatio + 0.5f ) );
	viewport->y2 = idMath::Ftoi( ( rc->y + rc->height ) - floor( renderView->y * hRatio + 0.5f ) - 1 );
}

static int RoundDownToPowerOfTwo( int v ) {
	int	i;

	for ( i = 0 ; i < 20 ; i++ ) {
		if ( ( 1 << i ) == v ) {
			return v;
		}
		if ( ( 1 << i ) > v ) {
			return 1 << ( i-1 );
		}
	}
	return 1<<i;
}

/*
================
idRenderSystemLocal::PushSceneResolutionScale

r_screenFraction below native, scoped to the world render.

The saving comes from the crop: everything downstream of RenderViewToViewport --
the view's viewport, its scissor, every drawSurf, light and entity scissor rect,
and the viewports of any mirror or portal subviews derived from it -- is computed
in the smaller space by front-end maths that already exists. Nothing in the back
end has to rescale anything.

Scoping it to RenderScene rather than the frame is what keeps the HUD and the
menus at native resolution. A crop pushed in BeginFrame reaches
idGuiModel::EmitFullScreen too, and every fullscreen 2D draw in the frame lands
inside it.

Returns whether a crop was pushed; the caller must pop exactly when it did.
================
*/
bool idRenderSystemLocal::PushSceneResolutionScale( void ) {
	resolutionScaleCropActive = false;
	resolutionScaleWidth = 0;
	resolutionScaleHeight = 0;

	if ( !glConfig.isInitialized ) {
		return false;
	}

	const int screenFraction = idMath::ClampInt( 10, 200, r_screenFraction.GetInteger() );
	if ( screenFraction >= 100 ) {
		return false;
	}

	// Mode 0 is the legacy whole-frame crop, still pushed in BeginFrame, and it
	// deliberately has no upscale. Modes 1 and 2 are implemented in
	// draw_common.cpp for the desktop path; this is the ES half of them.
	if ( r_resolutionScaleMode.GetInteger() == 0 ) {
		return false;
	}
	if ( glConfig.backendCaps.profile != RENDERER_CONTEXT_PROFILE_ES ) {
		return false;
	}

	// the back end has told us it cannot resolve the crop on this context
	if ( resolutionScaleSuppressed ) {
		return false;
	}

	// Something else already owns the frame's coordinate system -- a levelshot,
	// or the legacy mode 0 crop. Do not layer a second one under it.
	if ( currentRenderCrop != 0 ) {
		return false;
	}

	// A larger-than-window capture re-renders the scene once per tile with
	// tr.viewportOffset moved, and resolving a crop inside one tile would stretch
	// that tile over the whole window. Ordinary same-size screenshots are just a
	// re-render of the live frame and keep the scaling, so a capture shows what
	// the display shows.
	if ( tiledViewport[0] > glConfig.vidWidth || tiledViewport[1] > glConfig.vidHeight ) {
		return false;
	}

	// The scene may be rendering into an offscreen target rather than the back
	// buffer -- that is the normal path once MSAA or post AA is on, and it is
	// what an Android build with r_multiSamples set does. That is fine: the back
	// end resolves the crop inside whatever target is bound, before the game's
	// fullscreen material samples it at texcoords 0..1.
	//
	// The one case it cannot handle is a multisampled target, which ES 3.0
	// refuses as a blit destination. Catch that here rather than letting the back
	// end discover it, so no frame is ever presented with the scene left in the
	// target's corner.
	if ( activeRenderTexture != NULL ) {
		idImage *sceneColor = ( activeRenderTexture->GetNumColorImages() > 0 )
			? activeRenderTexture->GetColorImage( 0 )
			: NULL;
		if ( sceneColor == NULL || sceneColor->GetOpts().numMSAASamples > 1 ) {
			static bool reported = false;
			if ( !reported ) {
				reported = true;
				common->Printf( "r_screenFraction: inactive -- the scene target is multisampled, which cannot receive a blit on ES.\n" );
			}
			return false;
		}
	}

	CropRenderSize( SCREEN_WIDTH * screenFraction / 100.0f, SCREEN_HEIGHT * screenFraction / 100.0f );

	// CropRenderSize converts to physical pixels and can halve what it was asked
	// for, so the rect it actually pushed is the one the back end has to resolve
	// -- not the percentage it was derived from.
	resolutionScaleCropActive = true;
	resolutionScaleWidth = renderCrops[currentRenderCrop].width;
	resolutionScaleHeight = renderCrops[currentRenderCrop].height;
	return true;
}

/*
================
idRenderSystemLocal::PopSceneResolutionScale
================
*/
void idRenderSystemLocal::PopSceneResolutionScale( void ) {
	if ( !resolutionScaleCropActive ) {
		return;
	}
	resolutionScaleCropActive = false;
	UnCrop();
}

/*
================
CropRenderSize

This automatically halves sizes until it fits in the current window size,
so if you specify a power of two size for a texture copy, it may be shrunk
down, but still valid.
================
*/
void	idRenderSystemLocal::CropRenderSize( int width, int height, bool makePowerOfTwo, bool forceDimensions ) {
	if ( !glConfig.isInitialized ) {
		return;
	}

	// close any gui drawing before changing the size
	guiModel->EmitFullScreen();
	guiModel->Clear();

	if ( width < 1 || height < 1 ) {
		common->Error( "CropRenderSize: bad sizes" );
	}

	if ( session->writeDemo ) {
		session->writeDemo->WriteInt( DS_RENDER );
		session->writeDemo->WriteInt( DC_CROP_RENDER );
		session->writeDemo->WriteInt( width );
		session->writeDemo->WriteInt( height );
		session->writeDemo->WriteInt( makePowerOfTwo );

		if ( r_showDemo.GetBool() ) {
			common->Printf( "write DC_CROP_RENDER\n" );
		}
	}

	// convert from virtual SCREEN_WIDTH/SCREEN_HEIGHT coordinates to physical OpenGL pixels
	renderView_t renderView;
	renderView.x = 0;
	renderView.y = 0;
	renderView.width = width;
	renderView.height = height;

	idScreenRect	r;
	RenderViewToViewport( &renderView, &r );

	width = r.x2 - r.x1 + 1;
	height = r.y2 - r.y1 + 1;

	if ( forceDimensions ) {
		// just give exactly what we ask for
		width = renderView.width;
		height = renderView.height;
	}

	// if makePowerOfTwo, drop to next lower power of two after scaling to physical pixels
	if ( makePowerOfTwo ) {
		width = RoundDownToPowerOfTwo( width );
		height = RoundDownToPowerOfTwo( height );
		// FIXME: megascreenshots with offset viewports don't work right with this yet
	}

	renderCrop_t	*rc = &renderCrops[currentRenderCrop];

	// we might want to clip these to the crop window instead
	while ( width > glConfig.vidWidth ) {
		width >>= 1;
	}
	while ( height > glConfig.vidHeight ) {
		height >>= 1;
	}

	if ( currentRenderCrop >= MAX_RENDER_CROPS - 1 ) {
		common->Error( "idRenderSystemLocal::CropRenderSize: MAX_RENDER_CROPS exceeded" );
	}

	currentRenderCrop++;

	rc = &renderCrops[currentRenderCrop];

	rc->x = 0;
	rc->y = 0;
	rc->width = width;
	rc->height = height;
}

/*
================
UnCrop
================
*/
void idRenderSystemLocal::UnCrop() {
	if ( !glConfig.isInitialized ) {
		return;
	}

	if ( currentRenderCrop < 1 ) {
		common->Error( "idRenderSystemLocal::UnCrop: currentRenderCrop < 1" );
	}

	// close any gui drawing
	guiModel->EmitFullScreen();
	guiModel->Clear();

	currentRenderCrop--;

	if ( session->writeDemo ) {
		session->writeDemo->WriteInt( DS_RENDER );
		session->writeDemo->WriteInt( DC_UNCROP_RENDER );

		if ( r_showDemo.GetBool() ) {
			common->Printf( "write DC_UNCROP\n" );
		}
	}
}

/*
================
CaptureRenderToImage
================
*/
void idRenderSystemLocal::CaptureRenderToImage( const char *imageName ) {
	if ( !glConfig.isInitialized ) {
		return;
	}
	guiModel->EmitFullScreen();
	guiModel->Clear();

	if ( session->writeDemo ) {
		session->writeDemo->WriteInt( DS_RENDER );
		session->writeDemo->WriteInt( DC_CAPTURE_RENDER );
		session->writeDemo->WriteHashString( imageName );

		if ( r_showDemo.GetBool() ) {
			common->Printf( "write DC_CAPTURE_RENDER: %s\n", imageName );
		}
	}

	// look up the image before we create the render command, because it
	// may need to sync to create the image
	idImage	*image = globalImages->ImageFromFile(imageName, TF_DEFAULT, TR_REPEAT, TD_DEFAULT);

	renderCrop_t *rc = &renderCrops[currentRenderCrop];

	copyRenderCommand_t *cmd = (copyRenderCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_COPY_RENDER;
	cmd->x = rc->x;
	cmd->y = rc->y;
	cmd->imageWidth = rc->width;
	cmd->imageHeight = rc->height;
	cmd->image = image;
	cmd->cubeFace = 0;
	cmd->copyDepth = false;
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddCopyRender( image, cmd->x, cmd->y,
			cmd->imageWidth, cmd->imageHeight, cmd->cubeFace, cmd->copyDepth );
	}

	guiModel->Clear();
}

/*
==============
CaptureRenderToFile

==============
*/
void idRenderSystemLocal::CaptureRenderToFile( const char *fileName, bool fixAlpha ) {
	CaptureRenderToFile( fileName, fixAlpha, 0, 0 );
}

static const int MAX_CAPTURE_DIMENSION = 16384;
static const int64 MAX_CAPTURE_PIXELS = 33554432;

/*
==============
R_ResampleCaptureToAspectRGBA

Preserves the bottom-up row order returned by glReadPixels.  R_WriteTGA's
flipVertical argument then describes those rows exactly as it does for an
unscaled capture.
==============
*/
static byte *R_ResampleCaptureToAspectRGBA( const byte *source, int sourceWidth, int sourceHeight,
	int outputWidth, int outputHeight ) {
	int cropX = 0;
	int cropY = 0;
	int cropWidth = sourceWidth;
	int cropHeight = sourceHeight;

	const int64 sourceAspectProduct = (int64)sourceWidth * outputHeight;
	const int64 outputAspectProduct = (int64)sourceHeight * outputWidth;
	if ( sourceAspectProduct > outputAspectProduct ) {
		cropWidth = (int)( (int64)sourceHeight * outputWidth / outputHeight );
		cropWidth = Max( cropWidth, 1 );
		cropX = ( sourceWidth - cropWidth ) / 2;
	} else if ( sourceAspectProduct < outputAspectProduct ) {
		cropHeight = (int)( (int64)sourceWidth * outputHeight / outputWidth );
		cropHeight = Max( cropHeight, 1 );
		cropY = ( sourceHeight - cropHeight ) / 2;
	}

	const size_t outputPixels = (size_t)outputWidth * (size_t)outputHeight;
	byte *output = (byte *)R_StaticAlloc( outputPixels * 4 );
	const int cropMaxX = cropX + cropWidth - 1;
	const int cropMaxY = cropY + cropHeight - 1;

	for ( int y = 0; y < outputHeight; y++ ) {
		const double sourceY = cropY + ( ( y + 0.5 ) * cropHeight / outputHeight ) - 0.5;
		int y0 = (int)floor( sourceY );
		double yFraction = sourceY - y0;
		if ( y0 < cropY ) {
			y0 = cropY;
			yFraction = 0.0;
		} else if ( y0 >= cropMaxY ) {
			y0 = cropMaxY;
			yFraction = 0.0;
		}
		const int y1 = Min( y0 + 1, cropMaxY );
		byte *outputRow = output + (size_t)y * (size_t)outputWidth * 4;

		for ( int x = 0; x < outputWidth; x++ ) {
			const double sourceX = cropX + ( ( x + 0.5 ) * cropWidth / outputWidth ) - 0.5;
			int x0 = (int)floor( sourceX );
			double xFraction = sourceX - x0;
			if ( x0 < cropX ) {
				x0 = cropX;
				xFraction = 0.0;
			} else if ( x0 >= cropMaxX ) {
				x0 = cropMaxX;
				xFraction = 0.0;
			}
			const int x1 = Min( x0 + 1, cropMaxX );

			const byte *topLeft = source + ( (size_t)y0 * sourceWidth + x0 ) * 4;
			const byte *topRight = source + ( (size_t)y0 * sourceWidth + x1 ) * 4;
			const byte *bottomLeft = source + ( (size_t)y1 * sourceWidth + x0 ) * 4;
			const byte *bottomRight = source + ( (size_t)y1 * sourceWidth + x1 ) * 4;
			byte *destination = outputRow + x * 4;

			for ( int channel = 0; channel < 4; channel++ ) {
				const double top = topLeft[channel] + ( topRight[channel] - topLeft[channel] ) * xFraction;
				const double bottom = bottomLeft[channel] + ( bottomRight[channel] - bottomLeft[channel] ) * xFraction;
				destination[channel] = (byte)( top + ( bottom - top ) * yFraction + 0.5 );
			}
		}
	}

	return output;
}

void idRenderSystemLocal::CaptureRenderToFile( const char *fileName, bool fixAlpha,
	int outputWidth, int outputHeight ) {
	if ( !glConfig.isInitialized ) {
		return;
	}

	renderCrop_t *rc = &renderCrops[currentRenderCrop];
	const bool resample = outputWidth != 0 || outputHeight != 0;
	const int64 sourcePixelCount = (int64)rc->width * rc->height;
	if ( rc->width < 1 || rc->height < 1 ||
			rc->width > MAX_CAPTURE_DIMENSION || rc->height > MAX_CAPTURE_DIMENSION ||
			sourcePixelCount > MAX_CAPTURE_PIXELS ) {
		common->Warning( "CaptureRenderToFile: invalid source dimensions %d x %d", rc->width, rc->height );
		return;
	}
	const int64 outputPixelCount = (int64)outputWidth * outputHeight;
	if ( resample && ( outputWidth < 1 || outputHeight < 1 ||
			outputWidth > MAX_CAPTURE_DIMENSION || outputHeight > MAX_CAPTURE_DIMENSION ||
			outputPixelCount > MAX_CAPTURE_PIXELS ) ) {
		common->Warning( "CaptureRenderToFile: invalid output dimensions %d x %d", outputWidth, outputHeight );
		return;
	}

	guiModel->EmitFullScreen();
	guiModel->Clear();
	const bool wasTakingScreenshot = tr.takingScreenshot;
	tr.takingScreenshot = true;
	R_TemporalPresentation_MarkCurrentFrameCapture( "synchronous readback" );
	R_IssueRenderCommands();

	glReadBuffer( GL_BACK );

	// GL_RGB readback rows use the default four-byte pack alignment.  Convert
	// each row independently so the padding never becomes pixel data.
	const int sourceStride = ( rc->width * 3 + 3 ) & ~3;
	const size_t sourceBytes = (size_t)sourceStride * (size_t)rc->height;
	byte *data = (byte *)R_StaticAlloc( sourceBytes );
	memset( data, 0, sourceBytes );
	
	glReadPixels( rc->x, rc->y, rc->width, rc->height, GL_RGB, GL_UNSIGNED_BYTE, data ); 
	tr.takingScreenshot = wasTakingScreenshot;

	byte *data2 = (byte *)R_StaticAlloc( (size_t)sourcePixelCount * 4 );

	for ( int y = 0; y < rc->height; y++ ) {
		const byte *sourceRow = data + (size_t)y * sourceStride;
		byte *destinationRow = data2 + (size_t)y * rc->width * 4;
		for ( int x = 0; x < rc->width; x++ ) {
			destinationRow[ x * 4 ] = sourceRow[ x * 3 ];
			destinationRow[ x * 4 + 1 ] = sourceRow[ x * 3 + 1 ];
			destinationRow[ x * 4 + 2 ] = sourceRow[ x * 3 + 2 ];
			destinationRow[ x * 4 + 3 ] = 0xff;
		}
	}
	R_StaticFree( data );
	data = NULL;

	byte *outputData = data2;
	int capturedWidth = rc->width;
	int capturedHeight = rc->height;
	if ( resample ) {
		outputData = R_ResampleCaptureToAspectRGBA( data2, rc->width, rc->height,
			outputWidth, outputHeight );
		R_StaticFree( data2 );
		data2 = NULL;
		capturedWidth = outputWidth;
		capturedHeight = outputHeight;
	}

	R_WriteTGA( fileName, outputData, capturedWidth, capturedHeight, true );
	R_StaticFree( outputData );
}


/*
==============
AllocRenderWorld
==============
*/
idRenderWorld *idRenderSystemLocal::AllocRenderWorld() {
	idRenderWorldLocal *rw;
	rw = new idRenderWorldLocal;
	worlds.Append( rw );
	return rw;
}

/*
==============
FreeRenderWorld
==============
*/
void idRenderSystemLocal::FreeRenderWorld( idRenderWorld *rw ) {
	if ( primaryWorld == rw ) {
		primaryWorld = NULL;
	}
	worlds.Remove( static_cast<idRenderWorldLocal *>(rw) );
	delete rw;
}

/*
==============
PrintMemInfo
==============
*/
void idRenderSystemLocal::PrintMemInfo( MemInfo_t *mi ) {
	// sum up image totals
	globalImages->PrintMemInfo( mi );

	// sum up model totals
//	renderModelManager->PrintMemInfo( mi );

	// compute render totals

}

/*
===============
idRenderSystemLocal::UploadImage
===============
*/
bool idRenderSystemLocal::UploadImage( const char *imageName, const byte *data, int width, int height  ) {
	idImage *image = globalImages->GetImage( imageName );
	if ( !image ) {
		return false;
	}
	image->UploadScratch( data, width, height );
	//image->SetImageFilterAndRepeat();
	return true;
}


/*
===============
idRenderSystemLocal::BindRenderTexture
===============
*/
void idRenderSystemLocal::BindRenderTexture(idRenderTexture* renderTexture, idRenderTexture* feedbackRenderTexture) {
	setRenderTargetCommand_t* cmd;

	// DrawStretchPic batches into guiModel; flush pending surfaces before
	// switching FBO targets so post-process passes execute on the intended target.
	guiModel->EmitFullScreen();
	guiModel->Clear();

	cmd = (setRenderTargetCommand_t*)R_GetCommandBuffer(sizeof(*cmd));
	cmd->commandId = RC_SET_RENDERTEXTURE;

	cmd->renderTexture = renderTexture;
	cmd->feedbackRenderTexture = feedbackRenderTexture;
	activeRenderTexture = renderTexture;
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddRenderTargetOp();
	}
}

/*
===============
idRenderSystemLocal::ClearRenderTarget
===============
*/
void idRenderSystemLocal::ClearRenderTarget(bool clearColor, bool clearDepth, float depthValue, float red, float green, float blue) {
	renderClearBufferCommand_t* cmd;

	cmd = (renderClearBufferCommand_t*)R_GetCommandBuffer(sizeof(*cmd));
	cmd->commandId = RC_CLEAR_RENDERTARGET;

	cmd->clearColor = clearColor;
	cmd->clearDepth = clearDepth;

	cmd->clearDepthValue = depthValue;
	cmd->clearColorValue = idVec4(red, green, blue, 1.0);
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddRenderTargetOp();
	}
}

/*
===============
idRenderSystemLocal::ResolveMSAA
===============
*/
void idRenderSystemLocal::ResolveMSAA(idRenderTexture* msaaRenderTexture, idRenderTexture* destRenderTexture, bool resolveDepth) {
	resolveRenderTargetCommand_t* cmd;
	if ( msaaRenderTexture == NULL || destRenderTexture == NULL ) {
		return;
	}

	cmd = (resolveRenderTargetCommand_t*)R_GetCommandBuffer(sizeof(*cmd));
	cmd->commandId = RC_RESOLVE_MSAA;

	cmd->msaaRenderTexture = msaaRenderTexture;
	cmd->destRenderTexture = destRenderTexture;
	cmd->resolveDepth = resolveDepth;
	cmd->frameNumber = tr.frameCount;
	cmd->historyGeneration = R_TemporalPresentation_HistoryGeneration();
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddRenderTargetOp();
	}
}

/*
===============
idRenderSystemLocal::CreateImage
===============
*/
idImage* idRenderSystemLocal::CreateImage(const char* name, idImageOpts* opts, textureFilter_t textureFilter) {
	// Check to see if the image already exists.
	idImage* image = globalImages->GetImage(name);
	if (image != nullptr) {
		image->AllocImage(*opts, textureFilter, TR_CLAMP);
		return image;
	}

	return globalImages->ScratchImage(name, opts, textureFilter, TR_CLAMP, TD_DEFAULT);
}

/*
===============
idRenderSystemLocal::FindImage
===============
*/
idImage* idRenderSystemLocal::FindImage(const char* name) {
	return globalImages->ImageFromFile(name, TF_DEFAULT, TR_REPEAT, TD_DEFAULT);
}

/*
===============
idRenderSystemLocal::ValidateMaterialArbPrograms
===============
*/
bool idRenderSystemLocal::ValidateMaterialArbPrograms( const idMaterial* material ) {
	if ( material == NULL ) {
		return false;
	}

	const int stageCount = material->GetNumStages();
	for ( int stageIndex = 0; stageIndex < stageCount; ++stageIndex ) {
		const shaderStage_t* stage = material->GetStage( stageIndex );
		if ( stage == NULL || stage->newStage == NULL ) {
			continue;
		}

		const newShaderStage_t* newStage = stage->newStage;
		if ( newStage->vertexProgram != 0 &&
			!R_IsARBProgramValid( GL_VERTEX_PROGRAM_ARB, newStage->vertexProgram ) ) {
			return false;
		}

		if ( newStage->fragmentProgram != 0 &&
			!R_IsARBProgramValid( GL_FRAGMENT_PROGRAM_ARB, newStage->fragmentProgram ) ) {
			return false;
		}

		if ( newStage->glslProgram && !R_ValidateGLSLProgram( const_cast<newShaderStage_t *>( newStage ) ) ) {
			return false;
		}
	}

	return true;
}

/*
===============
idRenderSystemLocal::ValidateSMAALookupTextures
===============
*/
static const char *R_TextureFilterName( const textureFilter_t filter ) {
	switch ( filter ) {
	case TF_LINEAR:
		return "linear";
	case TF_NEAREST:
		return "nearest";
	case TF_DEFAULT:
		return "default";
	default:
		return "unknown";
	}
}

static const char *R_TextureRepeatName( const textureRepeat_t repeat ) {
	switch ( repeat ) {
	case TR_REPEAT:
		return "repeat";
	case TR_MIRRORED_REPEAT:
		return "mirroredRepeat";
	case TR_CLAMP:
		return "clamp";
	case TR_CLAMP_TO_BORDER:
		return "clampToBorder";
	case TR_CLAMP_TO_ZERO:
		return "clampToZero";
	case TR_CLAMP_TO_ZERO_ALPHA:
		return "clampToZeroAlpha";
	default:
		return "unknown";
	}
}

static bool R_ValidateSMAALookupImage( const idImage *image, const char *imageName, const int expectedWidth, const int expectedHeight ) {
	if ( image == NULL ) {
		common->Warning( "SMAA lookup image '%s' is unavailable.", imageName );
		return false;
	}

	if ( !image->IsLoaded() ) {
		common->Warning( "SMAA lookup image '%s' exists but is not loaded.", imageName );
		return false;
	}

	if ( image->GetUploadWidth() != expectedWidth || image->GetUploadHeight() != expectedHeight ) {
		common->Warning(
			"SMAA lookup image '%s' has unexpected dimensions %d x %d (expected %d x %d).",
			imageName,
			image->GetUploadWidth(),
			image->GetUploadHeight(),
			expectedWidth,
			expectedHeight );
		return false;
	}

	if ( image->GetOpts().format != FMT_RGBA8 ) {
		common->Warning(
			"SMAA lookup image '%s' has unexpected format %d (expected %d).",
			imageName,
			image->GetOpts().format,
			FMT_RGBA8 );
		return false;
	}

	if ( image->GetFilter() != TF_LINEAR ) {
		common->Warning(
			"SMAA lookup image '%s' has unexpected filter %s (expected linear).",
			imageName,
			R_TextureFilterName( image->GetFilter() ) );
		return false;
	}

	if ( image->GetRepeat() != TR_CLAMP ) {
		common->Warning(
			"SMAA lookup image '%s' has unexpected repeat mode %s (expected clamp).",
			imageName,
			R_TextureRepeatName( image->GetRepeat() ) );
		return false;
	}

	return true;
}

bool idRenderSystemLocal::ValidateSMAALookupTextures( void ) {
#ifndef OPENQ4_RENDERER_VK_MODULE
	if ( !glConfig.GLSLProgramAvailable || !glConfig.GLSL130Available ) {
		return false;
	}
#endif
	const idImage *areaImage = globalImages->GetImage( "_smaaArea" );
	const idImage *searchImage = globalImages->GetImage( "_smaaSearch" );

	if ( !R_ValidateSMAALookupImage( areaImage, "_smaaArea", AREATEX_WIDTH, AREATEX_HEIGHT ) ||
		!R_ValidateSMAALookupImage( searchImage, "_smaaSearch", SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT ) ) {
		return false;
	}

	static bool loggedValid = false;
	static uint64_t loggedAreaGeneration = 0;
	static uint64_t loggedSearchGeneration = 0;
	const uint64_t areaGeneration = areaImage->GetStorageGeneration();
	const uint64_t searchGeneration = searchImage->GetStorageGeneration();
	if ( !loggedValid ||
		loggedAreaGeneration != areaGeneration ||
		loggedSearchGeneration != searchGeneration ) {
		common->Printf(
			"SMAA lookup textures validated: _smaaArea=%dx%d format=%d filter=%s repeat=%s, _smaaSearch=%dx%d format=%d filter=%s repeat=%s\n",
			areaImage->GetUploadWidth(),
			areaImage->GetUploadHeight(),
			areaImage->GetOpts().format,
			R_TextureFilterName( areaImage->GetFilter() ),
			R_TextureRepeatName( areaImage->GetRepeat() ),
			searchImage->GetUploadWidth(),
			searchImage->GetUploadHeight(),
			searchImage->GetOpts().format,
			R_TextureFilterName( searchImage->GetFilter() ),
			R_TextureRepeatName( searchImage->GetRepeat() ) );
		loggedValid = true;
		loggedAreaGeneration = areaGeneration;
		loggedSearchGeneration = searchGeneration;
	}

	return true;
}

/*
===============
idRenderSystemLocal::ResizeImage
===============
*/
void idRenderSystemLocal::ResizeImage(idImage* image, int width, int height) {
	image->Resize(width, height);
}

/*
===============
idRenderSystemLocal::GetImageSize
===============
*/
void idRenderSystemLocal::GetImageSize(idImage* image, int& imageWidth, int& imageHeight) {
	imageWidth = image->GetOpts().width;
	imageHeight = image->GetOpts().height;
}

/*
===============
idRenderSystemLocal::GetImageMSAASamples
===============
*/
int idRenderSystemLocal::GetImageMSAASamples( idImage* image ) {
	return image != NULL ? image->GetOpts().numMSAASamples : 0;
}

/*
===============
idRenderSystemLocal::GetGLConfig
===============
*/
const glconfig_t &idRenderSystemLocal::GetGLConfig( void ) const {
	return glConfig;
}

/*
====================
idRenderSystemLocal::GetGpuFrameTiming

The query objects stay backend-private. This POD snapshot is the sole
cross-module timing surface and is safe to consume from engine/game code.
====================
*/
void idRenderSystemLocal::GetGpuFrameTiming( renderGpuFrameTiming_t &timing ) const {
	R_RendererMetrics_GetGpuFrameTiming( timing );
}

void idRenderSystemLocal::ResetGpuFrameTiming( const char *reason ) {
	R_RendererMetrics_ResetGpuFrameTiming( reason );
	// Session/map/backend discontinuities invalidate both delayed timing samples
	// and native-resolution image history. The controller maintains independent
	// generations so rejecting one never aliases an unrelated sample stream.
	R_TemporalPresentation_InvalidateHistory( reason );
}

/*
====================
idRenderSystemLocal::GetPresentationState

Publishes the scene extent selected once at BeginFrame. Game render targets
consume this POD while the final composite and UI continue to use the native
output extent. Calls made before the first BeginFrame receive the active video
mode as a safe native-resolution fallback.
====================
*/
void idRenderSystemLocal::GetPresentationState( renderPresentationState_t &state ) const {
	const temporalPresentationFrameState_t &frame =
		R_TemporalPresentation_GetFrameState();
	const int fallbackWidth = Max( 0, glConfig.vidWidth );
	const int fallbackHeight = Max( 0, glConfig.vidHeight );

	memset( &state, 0, sizeof( state ) );
	state.frameNumber = frame.frameNumber;
	state.outputWidth = frame.nativeWidth > 0 ? frame.nativeWidth : fallbackWidth;
	state.outputHeight = frame.nativeHeight > 0 ? frame.nativeHeight : fallbackHeight;
	state.sceneWidth = frame.sceneWidth > 0 ? frame.sceneWidth : state.outputWidth;
	state.sceneHeight = frame.sceneHeight > 0 ? frame.sceneHeight : state.outputHeight;
	state.effectiveScalePercent = frame.effectiveScalePercent > 0
		? frame.effectiveScalePercent : 100;
	state.historyGeneration = R_TemporalPresentation_HistoryGeneration();
	state.dynamicResolutionRequested = frame.nativeWidth > 0
		? frame.dynamicResolutionRequested
		: R_TemporalPresentation_DynamicResolutionRequested();
	state.dynamicResolutionActive = frame.dynamicResolutionActive;
	state.temporalAARequested = frame.nativeWidth > 0
		? frame.temporalAARequested
		: R_TemporalPresentation_TemporalAARequested();
	state.captureFrozen = frame.captureFrozen;
	state.captureForcedNative = frame.captureForcedNative;
}

/*
====================
idRenderSystemLocal::ResolveTemporalPresentation

Queues a backend-neutral temporal resolve after the game has completed its
scene-sized post chain. The immutable command snapshot is the only temporal
state consumed by an asynchronous backend. Runtime execution still observes
tr.takingScreenshot so a save-preview readback that starts after enqueue can
reject both history reads and history writes.
====================
*/
bool idRenderSystemLocal::ResolveTemporalPresentation(
		idRenderTexture *sceneColorTarget,
		idRenderTexture *sceneDepthTarget,
		idRenderTexture *historyReadTarget,
		idRenderTexture *historyWriteTarget ) {
	if ( sceneColorTarget == NULL || sceneDepthTarget == NULL
			|| tr.primaryView == NULL ) {
		return false;
	}

	renderPresentationState_t presentation;
	GetPresentationState( presentation );
	const temporalPresentationFrameState_t &temporalFrame =
		R_TemporalPresentation_GetFrameState();
	const unsigned int historyGeneration =
		R_TemporalPresentation_HistoryGeneration();
	const viewDef_t *primaryView = tr.primaryView;
	const bool screenSpaceRequested = AdvancedScreenSpaceCore_Requested(
		temporalFrame.advancedScreenSpace );
	const bool captureFrame = tr.takingScreenshot
		|| presentation.captureFrozen || presentation.captureForcedNative
		|| primaryView->temporalCaptureFrame;
	if ( ( !presentation.temporalAARequested && !screenSpaceRequested )
			|| presentation.frameNumber != tr.frameCount
			|| presentation.outputWidth <= 0 || presentation.outputHeight <= 0
			|| presentation.sceneWidth <= 0 || presentation.sceneHeight <= 0
			|| !primaryView->temporalPrepared
			|| ( presentation.temporalAARequested && !captureFrame
				&& primaryView->temporalHistoryGeneration != historyGeneration ) ) {
		return false;
	}

	if ( sceneColorTarget->GetWidth() != presentation.sceneWidth
			|| sceneColorTarget->GetHeight() != presentation.sceneHeight
			|| sceneDepthTarget->GetWidth() != presentation.sceneWidth
			|| sceneDepthTarget->GetHeight() != presentation.sceneHeight
			|| ( historyWriteTarget != NULL
				&& ( historyWriteTarget->GetWidth() != presentation.outputWidth
					|| historyWriteTarget->GetHeight() != presentation.outputHeight
					|| historyWriteTarget == sceneColorTarget
					|| historyWriteTarget == sceneDepthTarget ) ) ) {
		return false;
	}
	if ( historyReadTarget != NULL
			&& ( historyReadTarget == historyWriteTarget
				|| historyReadTarget == sceneColorTarget
				|| historyReadTarget == sceneDepthTarget
				|| historyReadTarget->GetWidth() != presentation.outputWidth
				|| historyReadTarget->GetHeight() != presentation.outputHeight ) ) {
		return false;
	}

	// Full-screen post submissions are batched in guiModel. Seal them before
	// appending the resolve so it remains the last scene operation before UI.
	if ( guiModel != NULL ) {
		guiModel->EmitFullScreen();
		guiModel->Clear();
	}

	resolveTemporalPresentationCommand_t *cmd =
		reinterpret_cast<resolveTemporalPresentationCommand_t *>(
			R_GetCommandBuffer( sizeof( *cmd ) ) );
	cmd->commandId = RC_RESOLVE_TEMPORAL_PRESENTATION;
	cmd->sceneColorTarget = sceneColorTarget;
	cmd->sceneDepthTarget = sceneDepthTarget;
	cmd->historyReadTarget = historyReadTarget;
	cmd->historyWriteTarget = historyWriteTarget;
	cmd->viewDef = primaryView;
	cmd->presentation = presentation;
	cmd->historyGeneration = historyGeneration;
	cmd->historyValid = !captureFrame && historyReadTarget != NULL
		&& historyWriteTarget != NULL
		&& primaryView->temporalHistoryValid;
	cmd->captureFrame = captureFrame;
	cmd->feedback = temporalFrame.temporalFeedback;
	cmd->reactiveScale = temporalFrame.temporalReactiveScale;
	cmd->debugMode = temporalFrame.temporalDebugMode;
	cmd->advancedScreenSpace = temporalFrame.advancedScreenSpace;
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddRenderTargetOp();
	}
	return true;
}

/*
===============
idRenderSystemLocal::SetUnderwaterView

Publishes the underwater state for this frame and reports whether the effect will actually be
drawn. Only the GL back end has the post-process pass; the Vulkan module supports a fixed set of
material programs and no arbitrary GLSL, so it answers false and the caller falls back.
===============
*/
bool idRenderSystemLocal::SetUnderwaterView( float amount, const idVec3 &tint, float fogDistance ) {
	underwaterAmount = idMath::ClampFloat( 0.0f, 1.0f, amount );
	underwaterTint = tint;
	underwaterFogDistance = Max( 1.0f, fogDistance );

	if ( underwaterAmount <= 0.0f ) {
		return true;		// nothing to draw, and nothing for the caller to fall back to either
	}

	return RB_UnderwaterViewAvailable();
}

/*
===============
idRenderSystemLocal::GetRenderTextureSize
===============
*/
void idRenderSystemLocal::GetRenderTextureSize(idRenderTexture* renderTexture, int& renderTextureWidth, int& renderTextureHeight) {
	if ( renderTexture == NULL ) {
		renderTextureWidth = 0;
		renderTextureHeight = 0;
		return;
	}

	renderTextureWidth = renderTexture->GetWidth();
	renderTextureHeight = renderTexture->GetHeight();
}

/*
===============
idRenderSystemLocal::SetRenderTextureDebugName
===============
*/
void idRenderSystemLocal::SetRenderTextureDebugName(idRenderTexture* renderTexture, const char* label) {
	if ( renderTexture == NULL ) {
		return;
	}

	renderTexture->SetDebugLabel( label );
}

/*
===============
idRenderSystemLocal::SetPostProcessSourceSize
===============
*/
void idRenderSystemLocal::SetPostProcessSourceSize(int width, int height) {
	width = Max( width, 1 );
	height = Max( height, 1 );
	postProcessTexelSize.Set(
		1.0f / static_cast<float>( width ),
		1.0f / static_cast<float>( height ),
		static_cast<float>( width ),
		static_cast<float>( height ) );

	if ( guiModel != NULL ) {
		guiModel->EmitFullScreen();
		guiModel->Clear();
	}

	setPostProcessSourceSizeCommand_t* cmd =
		(setPostProcessSourceSizeCommand_t*)R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_SET_POSTPROCESS_SOURCE_SIZE;
	cmd->texelSize = postProcessTexelSize;
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddRenderTargetOp();
	}
}

/*
===============
idRenderSystemLocal::SetPostProcessSourceColorSpace
===============
*/
void idRenderSystemLocal::SetPostProcessSourceColorSpace(const idVec4& colorSpace) {
	postProcessSourceColorSpace = colorSpace;

	if ( guiModel != NULL ) {
		guiModel->EmitFullScreen();
		guiModel->Clear();
	}

	setPostProcessSourceColorSpaceCommand_t* cmd =
		(setPostProcessSourceColorSpaceCommand_t*)R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_SET_POSTPROCESS_SOURCE_COLOR_SPACE;
	cmd->colorSpace = postProcessSourceColorSpace;
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddRenderTargetOp();
	}
}

/*
===============
idRenderSystemLocal::SetPostProcessSMAAQuality
===============
*/
void idRenderSystemLocal::SetPostProcessSMAAQuality(const idVec4& quality) {
	postProcessSMAAQuality = quality;

	if ( guiModel != NULL ) {
		guiModel->EmitFullScreen();
		guiModel->Clear();
	}

	setPostProcessSMAAQualityCommand_t* cmd =
		(setPostProcessSMAAQualityCommand_t*)R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_SET_POSTPROCESS_SMAA_QUALITY;
	cmd->quality = postProcessSMAAQuality;
	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddRenderTargetOp();
	}
}

/*
===============
idRenderSystemLocal::ResizeRenderTexture
===============
*/
bool idRenderSystemLocal::ResizeRenderTexture(idRenderTexture*& renderTexture, int width, int height) {
	if ( renderTexture == NULL ) {
		return false;
	}
	if ( renderTexture->Resize( width, height ) ) {
		return true;
	}

	idRenderTexture* failedRenderTexture = renderTexture;
	renderTexture = NULL;
	DestroyRenderTexture( failedRenderTexture );
	return false;
}

struct renderTextureFailureKey_t {
	idImage*	attachments[4];
	uint64		storageGenerations[4];
	int			contextGeneration;
};

static idList<renderTextureFailureKey_t> renderTextureFailureKeys;
static const int MAX_RENDER_TEXTURE_FAILURE_KEYS = 64;

static renderTextureFailureKey_t R_BuildRenderTextureFailureKey(
	idImage* albedoImage,
	idImage* depthImage,
	idImage* albedoImage2,
	idImage* albedoImage3 ) {
	renderTextureFailureKey_t key;
	key.attachments[0] = albedoImage;
	key.attachments[1] = depthImage;
	key.attachments[2] = albedoImage2;
	key.attachments[3] = albedoImage3;
	for ( int i = 0; i < 4; i++ ) {
		key.storageGenerations[i] = key.attachments[i] != NULL
			? key.attachments[i]->GetStorageGeneration()
			: 0;
	}
	key.contextGeneration = tr.glContextGeneration;
	return key;
}

static bool R_RenderTextureFailureHasSameAttachments(
	const renderTextureFailureKey_t& lhs,
	const renderTextureFailureKey_t& rhs ) {
	for ( int i = 0; i < 4; i++ ) {
		if ( lhs.attachments[i] != rhs.attachments[i] ) {
			return false;
		}
	}
	return true;
}

static bool R_RenderTextureFailureKeysMatch(
	const renderTextureFailureKey_t& lhs,
	const renderTextureFailureKey_t& rhs ) {
	if ( lhs.contextGeneration != rhs.contextGeneration ||
		!R_RenderTextureFailureHasSameAttachments( lhs, rhs ) ) {
		return false;
	}
	for ( int i = 0; i < 4; i++ ) {
		if ( lhs.storageGenerations[i] != rhs.storageGenerations[i] ) {
			return false;
		}
	}
	return true;
}

static void R_PruneRenderTextureFailureKeys( int contextGeneration ) {
	for ( int i = renderTextureFailureKeys.Num() - 1; i >= 0; i-- ) {
		if ( renderTextureFailureKeys[i].contextGeneration != contextGeneration ) {
			renderTextureFailureKeys.RemoveIndex( i );
		}
	}
}

static bool R_IsRenderTextureFailureLatched( const renderTextureFailureKey_t& key ) {
	R_PruneRenderTextureFailureKeys( key.contextGeneration );
	for ( int i = 0; i < renderTextureFailureKeys.Num(); i++ ) {
		if ( R_RenderTextureFailureKeysMatch( renderTextureFailureKeys[i], key ) ) {
			return true;
		}
	}
	return false;
}

static void R_LatchRenderTextureFailure( const renderTextureFailureKey_t& key ) {
	R_PruneRenderTextureFailureKeys( key.contextGeneration );
	for ( int i = 0; i < renderTextureFailureKeys.Num(); i++ ) {
		if ( R_RenderTextureFailureHasSameAttachments( renderTextureFailureKeys[i], key ) ) {
			renderTextureFailureKeys[i] = key;
			return;
		}
	}
	if ( renderTextureFailureKeys.Num() >= MAX_RENDER_TEXTURE_FAILURE_KEYS ) {
		renderTextureFailureKeys.RemoveIndex( 0 );
	}
	renderTextureFailureKeys.Append( key );
}

static void R_ClearRenderTextureFailure( const renderTextureFailureKey_t& key ) {
	for ( int i = renderTextureFailureKeys.Num() - 1; i >= 0; i-- ) {
		if ( R_RenderTextureFailureHasSameAttachments( renderTextureFailureKeys[i], key ) ) {
			renderTextureFailureKeys.RemoveIndex( i );
		}
	}
}

/*
===============
idRenderSystemLocal::CreateRenderTexture
===============
*/
idRenderTexture* idRenderSystemLocal::CreateRenderTexture(idImage* albedoImage, idImage* depthImage, idImage* albedoImage2, idImage* albedoImage3) {
	// Creation runs on the active GL thread. Latch a rejected attachment/storage
	// set so helpers that keep a NULL pointer do not recreate and re-log the same
	// unsupported FBO every frame. Context changes and image reallocations retry.
	const renderTextureFailureKey_t failureKey = R_BuildRenderTextureFailureKey(
		albedoImage,
		depthImage,
		albedoImage2,
		albedoImage3 );
	if ( glConfig.isInitialized && R_IsRenderTextureFailureLatched( failureKey ) ) {
		return NULL;
	}

	idRenderTexture* renderTexture = new idRenderTexture(albedoImage, depthImage);

	if (albedoImage2)
	{
		renderTexture->AddRenderImage(albedoImage2);
	}

	if (albedoImage3)
	{
		renderTexture->AddRenderImage(albedoImage3);
	}

	if ( !renderTexture->InitRenderTexture() ) {
		if ( glConfig.isInitialized ) {
			R_LatchRenderTextureFailure( failureKey );
		}
		delete renderTexture;
		return NULL;
	}

	R_ClearRenderTextureFailure( failureKey );
	return renderTexture;
}

/*
===============
idRenderSystemLocal::DestroyRenderTexture
===============
*/
void idRenderSystemLocal::DestroyRenderTexture( idRenderTexture* renderTexture ) {
	if ( renderTexture == nullptr ) {
		return;
	}

	if ( !glConfig.isInitialized ) {
		delete renderTexture;
		return;
	}

	for ( int i = 0; i < pendingRenderTextureDeletes.Num(); i++ ) {
		if ( pendingRenderTextureDeletes[i] == renderTexture ) {
			return;
		}
	}

	if ( backEnd.renderTexture == renderTexture ) {
		backEnd.renderTexture = NULL;
	}
	if ( backEnd.feedbackRenderTexture == renderTexture ) {
		backEnd.feedbackRenderTexture = NULL;
	}
	if ( activeRenderTexture == renderTexture ) {
		activeRenderTexture = NULL;
	}

	pendingRenderTextureDeletes.Append( renderTexture );
}
