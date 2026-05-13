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
#include "RendererMetrics.h"
#include "RendererUpload.h"
#include "ScenePackets.h"
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
static backEndName_t R_RequestBackEndRenderer( const char *rendererName ) {
	if ( rendererName == NULL || rendererName[0] == '\0' || idStr::Icmp( rendererName, "best" ) == 0 ) {
		return BE_BAD;
	}

	if ( idStr::Icmp( rendererName, "arb2" ) == 0 ) {
		return glConfig.allowARB2Path ? BE_ARB2 : BE_BAD;
	}

	return BE_BAD;
}

/*
===============================
R_PickBestBackEndRenderer
===============================
*/
static backEndName_t R_PickBestBackEndRenderer() {
	if ( glConfig.allowARB2Path ) {
		return BE_ARB2;
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

OpenQ4's text/binary MD5R model writer is now implemented in the parser-backed
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
		common->Printf( "entityUpdates:%i  entityRefs:%i  lightUpdates:%i  lightRefs:%i\n", 
			tr.pc.c_entityUpdates, tr.pc.c_entityReferences,
			tr.pc.c_lightUpdates, tr.pc.c_lightReferences );
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
static void R_IssueRenderCommands( void ) {
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
	common->Printf( "view:%p surfs:%i\n", parms, parms->numDrawSurfs );
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

	cmd = (drawSurfsCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
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

	// Portal-sky cameras and other negative view IDs submit their own scene renders and
	// should never inherit fullscreen player special effects.
	if ( parms->renderView.viewID < 0 ) {
		return;
	}

	cmd = (drawSurfsCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_DRAW_SPECIAL_EFFECTS;
	cmd->viewDef = parms;
	R_ScenePackets_AddSpecialEffects( parms );
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
	cmd = (drawSurfsCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	*cmd = tr.lockSurfacesCmd;
}

/*
=============
R_CheckCvars

See if some cvars that we watch have changed
=============
*/
static void R_CheckCvars( void ) {
	//globalImages->CheckCvars();

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
	R_ScenePackets_AddCopyRender();

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

void idRenderSystemLocal::SetUseUIViewportFor2D( bool enable ) {
	if ( useUIViewportFor2D == enable ) {
		return;
	}

	// Viewport selection for 2D is applied when guiModel is emitted.
	// Flush pending 2D so already-queued surfaces keep the previous mode.
	if ( glConfig.isInitialized && guiModel != NULL ) {
		guiModel->EmitFullScreen();
		guiModel->Clear();
	}

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
	default:
		common->FatalError( "SetbackEndRenderer: bad back end" );
	}

	r_actualRenderer.SetString( R_GetBackEndRendererName( backEndRenderer ) );

	if ( requestedRendererName != NULL && requestedRendererName[0] != '\0' ) {
		if ( R_IsLegacyBackEndRequest( requestedRendererName ) ) {
			common->Warning(
				"r_renderer \"%s\" requested, but OpenQ4 only ships the ARB2 backend; using %s instead",
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

	// determine which back end we will use
	SetBackEndRenderer();

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
	// quick fill-rate testing. Modes 1/2 render full-size and apply a final
	// fullscreen resolution-scale pass in the backend.
	if ( r_screenFraction.GetInteger() != 100 && r_resolutionScaleMode.GetInteger() == 0 ) {
		int	w = SCREEN_WIDTH * r_screenFraction.GetInteger() / 100.0f;
		int h = SCREEN_HEIGHT * r_screenFraction.GetInteger() / 100.0f;
		CropRenderSize( w, h );
	}


	// this is the ONLY place this is modified
	frameCount++;
	R_RendererMetrics_BeginFrame( frameCount );
	R_RendererUpload_BeginFrame( frameCount );
	R_ScenePackets_BeginFrame();

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
	R_ScenePackets_AddCommandOnly();
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
	guiModel->EmitFullScreen();
	guiModel->Clear();

	// save out timing information
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

	// check for dynamic changes that require some initialization
	R_CheckCvars();

    // check for errors
	GL_CheckErrors();

	// add the swapbuffers command
	cmd = (emptyCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	cmd->commandId = RC_SWAP_BUFFERS;
	R_ScenePackets_AddPresent();

	// start the back end up again with the new command list
	R_IssueRenderCommands();
	ProcessPendingRenderTextureDeletes();

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

	if ( currentRenderCrop == MAX_RENDER_CROPS ) {
		common->Error( "idRenderSystemLocal::CropRenderSize: currentRenderCrop == MAX_RENDER_CROPS" );
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
	R_ScenePackets_AddCopyRender();

	guiModel->Clear();
}

/*
==============
CaptureRenderToFile

==============
*/
void idRenderSystemLocal::CaptureRenderToFile( const char *fileName, bool fixAlpha ) {
	if ( !glConfig.isInitialized ) {
		return;
	}

	renderCrop_t *rc = &renderCrops[currentRenderCrop];

	guiModel->EmitFullScreen();
	guiModel->Clear();
	R_IssueRenderCommands();

	glReadBuffer( GL_BACK );

	// include extra space for OpenGL padding to word boundaries
	int	c = ( rc->width + 3 ) * rc->height;
	byte *data = (byte *)R_StaticAlloc( c * 3 );
	
	glReadPixels( rc->x, rc->y, rc->width, rc->height, GL_RGB, GL_UNSIGNED_BYTE, data ); 

	byte *data2 = (byte *)R_StaticAlloc( c * 4 );

	for ( int i = 0 ; i < c ; i++ ) {
		data2[ i * 4 ] = data[ i * 3 ];
		data2[ i * 4 + 1 ] = data[ i * 3 + 1 ];
		data2[ i * 4 + 2 ] = data[ i * 3 + 2 ];
		data2[ i * 4 + 3 ] = 0xff;
	}

	R_WriteTGA( fileName, data2, rc->width, rc->height, true );

	R_StaticFree( data );
	R_StaticFree( data2 );
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
	R_ScenePackets_AddRenderTargetOp();
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
	R_ScenePackets_AddRenderTargetOp();
}

/*
===============
idRenderSystemLocal::ResolveMSAA
===============
*/
void idRenderSystemLocal::ResolveMSAA(idRenderTexture* msaaRenderTexture, idRenderTexture* destRenderTexture, bool resolveDepth) {
	resolveRenderTargetCommand_t* cmd;

	cmd = (resolveRenderTargetCommand_t*)R_GetCommandBuffer(sizeof(*cmd));
	cmd->commandId = RC_RESOLVE_MSAA;

	cmd->msaaRenderTexture = msaaRenderTexture;
	cmd->destRenderTexture = destRenderTexture;
	cmd->resolveDepth = resolveDepth;
	R_ScenePackets_AddRenderTargetOp();
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

	for ( int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex ) {
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
bool idRenderSystemLocal::ValidateSMAALookupTextures( void ) {
	const idImage *areaImage = globalImages->GetImage( "_smaaArea" );
	if ( areaImage == NULL ) {
		common->Warning( "SMAA lookup image '_smaaArea' is unavailable." );
		return false;
	}

	if ( !areaImage->IsLoaded() ) {
		common->Warning( "SMAA lookup image '_smaaArea' exists but is not loaded." );
		return false;
	}

	if ( areaImage->GetUploadWidth() != AREATEX_WIDTH || areaImage->GetUploadHeight() != AREATEX_HEIGHT ) {
		common->Warning(
			"SMAA lookup image '_smaaArea' has unexpected dimensions %d x %d (expected %d x %d).",
			areaImage->GetUploadWidth(),
			areaImage->GetUploadHeight(),
			AREATEX_WIDTH,
			AREATEX_HEIGHT );
		return false;
	}

	const idImage *searchImage = globalImages->GetImage( "_smaaSearch" );
	if ( searchImage == NULL ) {
		common->Warning( "SMAA lookup image '_smaaSearch' is unavailable." );
		return false;
	}

	if ( !searchImage->IsLoaded() ) {
		common->Warning( "SMAA lookup image '_smaaSearch' exists but is not loaded." );
		return false;
	}

	if ( searchImage->GetUploadWidth() != SEARCHTEX_WIDTH || searchImage->GetUploadHeight() != SEARCHTEX_HEIGHT ) {
		common->Warning(
			"SMAA lookup image '_smaaSearch' has unexpected dimensions %d x %d (expected %d x %d).",
			searchImage->GetUploadWidth(),
			searchImage->GetUploadHeight(),
			SEARCHTEX_WIDTH,
			SEARCHTEX_HEIGHT );
		return false;
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
idRenderSystemLocal::ResizeRenderTexture
===============
*/
void idRenderSystemLocal::ResizeRenderTexture(idRenderTexture* renderTexture, int width, int height) {
	renderTexture->Resize(width, height);
}

/*
===============
idRenderSystemLocal::CreateRenderTexture
===============
*/
idRenderTexture* idRenderSystemLocal::CreateRenderTexture(idImage* albedoImage, idImage* depthImage, idImage* albedoImage2, idImage* albedoImage3) {
	idRenderTexture* renderTexture = new idRenderTexture(albedoImage, depthImage);

	if (albedoImage2)
	{
		renderTexture->AddRenderImage(albedoImage2);
	}

	if (albedoImage3)
	{
		renderTexture->AddRenderImage(albedoImage3);
	}

	renderTexture->InitRenderTexture();

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
	if ( activeRenderTexture == renderTexture ) {
		activeRenderTexture = NULL;
	}

	pendingRenderTextureDeletes.Append( renderTexture );
}
