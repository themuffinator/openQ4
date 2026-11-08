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

static ID_INLINE void R_ClearGuiTextureAxes( idVec3 &origin, idVec3 axis[3] ) {
	origin.Zero();
	axis[0].Zero();
	axis[1].Zero();
	axis[2].Zero();
}

static ID_INLINE bool R_GetGuiVertexPosition( const srfTriangles_t *tri, int index, idVec3 &position ) {
	if ( tri == NULL || index < 0 || index >= tri->numVerts ) {
		position.Zero();
		return false;
	}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->silTraceVerts != NULL ) {
		position = reinterpret_cast<const rvSilTraceVertT *>( tri->silTraceVerts )[index].xyzw.ToVec3();
		return true;
	}
#endif

	if ( tri->verts == NULL ) {
		position.Zero();
		return false;
	}

	position = tri->verts[index].xyz;
	return true;
}

/*
==========================================================================================

GUI SHADERS

==========================================================================================
*/

/*
================
R_SurfaceToTextureAxis

Calculates two axis for the surface sutch that a point dotted against
the axis will give a 0.0 to 1.0 range in S and T when inside the gui surface
================
*/
void R_SurfaceToTextureAxis( const srfTriangles_t *tri, idVec3 &origin, idVec3 axis[3] ) {
	float		area, inva;
	float		d0[5], d1[5];
	idDrawVert	*a, *b, *c;
	float		bounds[2][2];
	float		boundsOrg[2];
	int			i, j;
	float		v;
	idVec3		aPos, bPos, cPos;

	if ( tri == NULL || tri->verts == NULL || tri->numVerts <= 0 || tri->numIndexes < 3 ) {
		R_ClearGuiTextureAxes( origin, axis );
		return;
	}
	
	// find the bounds of the texture
	bounds[0][0] = bounds[0][1] = 999999;
	bounds[1][0] = bounds[1][1] = -999999;
	for ( i = 0 ; i < tri->numVerts ; i++ ) {
		for ( j = 0 ; j < 2 ; j++ ) {
			v = tri->verts[i].st[j];
			if ( v < bounds[0][j] ) {
				bounds[0][j] = v;
			}
			if ( v > bounds[1][j] ) {
				bounds[1][j] = v;
			}
		}
	}

	// use the floor of the midpoint as the origin of the
	// surface, which will prevent a slight misalignment
	// from throwing it an entire cycle off
	boundsOrg[0] = floor( ( bounds[0][0] + bounds[1][0] ) * 0.5 );
	boundsOrg[1] = floor( ( bounds[0][1] + bounds[1][1] ) * 0.5 );


	// determine the world S and T vectors from the first drawSurf triangle
	a = tri->verts + tri->indexes[0];
	b = tri->verts + tri->indexes[1];
	c = tri->verts + tri->indexes[2];
	if ( !R_GetGuiVertexPosition( tri, tri->indexes[0], aPos )
		|| !R_GetGuiVertexPosition( tri, tri->indexes[1], bPos )
		|| !R_GetGuiVertexPosition( tri, tri->indexes[2], cPos ) ) {
		R_ClearGuiTextureAxes( origin, axis );
		return;
	}

	VectorSubtract( bPos, aPos, d0 );
	d0[3] = b->st[0] - a->st[0];
	d0[4] = b->st[1] - a->st[1];
	VectorSubtract( cPos, aPos, d1 );
	d1[3] = c->st[0] - a->st[0];
	d1[4] = c->st[1] - a->st[1];

	area = d0[3] * d1[4] - d0[4] * d1[3];
	if ( area == 0.0 ) {
		R_ClearGuiTextureAxes( origin, axis );
		return;	// degenerate
	}
	inva = 1.0 / area;

    axis[0][0] = (d0[0] * d1[4] - d0[4] * d1[0]) * inva;
    axis[0][1] = (d0[1] * d1[4] - d0[4] * d1[1]) * inva;
    axis[0][2] = (d0[2] * d1[4] - d0[4] * d1[2]) * inva;
    
    axis[1][0] = (d0[3] * d1[0] - d0[0] * d1[3]) * inva;
    axis[1][1] = (d0[3] * d1[1] - d0[1] * d1[3]) * inva;
    axis[1][2] = (d0[3] * d1[2] - d0[2] * d1[3]) * inva;

	idPlane plane;
	plane.FromPoints( aPos, bPos, cPos );
	axis[2][0] = plane[0];
	axis[2][1] = plane[1];
	axis[2][2] = plane[2];

	// take point 0 and project the vectors to the texture origin
	origin = aPos + ( boundsOrg[0] - a->st[0] ) * axis[0];
	origin += ( boundsOrg[1] - a->st[1] ) * axis[1];
}

static idScreenRect R_ScreenRectFromDrawSurf( const drawSurf_t *drawSurf ) {
	idScreenRect rect;
	rect.Clear();

	if ( drawSurf == NULL || drawSurf->geo == NULL ) {
		return rect;
	}

	const srfTriangles_t *geo = drawSurf->geo;
	for ( int i = 0; i < geo->numVerts; ++i ) {
		idVec3 local;
		idVec3 global;
		idVec3 ndc;

		if ( !R_GetGuiVertexPosition( geo, i, local ) ) {
			continue;
		}

		R_LocalPointToGlobal( drawSurf->space->modelMatrix, local, global );
		R_GlobalToNormalizedDeviceCoordinates( global, ndc );

		const float x = static_cast<float>( tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 ) * ( ndc[0] + 1.0f ) * 0.5f;
		const float y = static_cast<float>( tr.viewDef->viewport.y2 ) -
			static_cast<float>( tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 ) * ( ndc[1] + 1.0f ) * 0.5f;

		rect.AddPoint( x, y );
	}

	return rect;
}

static void R_ConvertScreenToHudSpace( float &x, float &y ) {
	if ( !cvarSystem->GetCVarBool( "ui_aspectCorrection" ) ) {
		return;
	}

	float windowWidth = static_cast<float>( glConfig.uiViewportWidth );
	float windowHeight = static_cast<float>( glConfig.uiViewportHeight );
	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		windowWidth = static_cast<float>( glConfig.vidWidth );
		windowHeight = static_cast<float>( glConfig.vidHeight );
	}

	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		return;
	}

	const float targetAspect = static_cast<float>( SCREEN_WIDTH ) / static_cast<float>( SCREEN_HEIGHT );
	const float windowAspect = windowWidth / windowHeight;
	const float uniformPhysicalScale = ( windowAspect >= targetAspect ) ?
		( windowHeight / static_cast<float>( SCREEN_HEIGHT ) ) :
		( windowWidth / static_cast<float>( SCREEN_WIDTH ) );
	const float drawWidth = static_cast<float>( SCREEN_WIDTH ) * uniformPhysicalScale;
	const float drawHeight = static_cast<float>( SCREEN_HEIGHT ) * uniformPhysicalScale;
	const float virtualPerPhysicalX = static_cast<float>( SCREEN_WIDTH ) / windowWidth;
	const float virtualPerPhysicalY = static_cast<float>( SCREEN_HEIGHT ) / windowHeight;
	const float xScale = uniformPhysicalScale * virtualPerPhysicalX;
	const float yScale = uniformPhysicalScale * virtualPerPhysicalY;
	const float xOffset = ( windowWidth - drawWidth ) * 0.5f * virtualPerPhysicalX;
	const float yOffset = ( windowHeight - drawHeight ) * 0.5f * virtualPerPhysicalY;

	if ( xScale != 0.0f ) {
		x = ( x - xOffset ) / xScale;
	}
	if ( yScale != 0.0f ) {
		y = ( y - yOffset ) / yScale;
	}
}

static void R_UpdateGuiScreenRectState( idUserInterface *gui, const drawSurf_t *drawSurf ) {
	if ( gui == NULL || drawSurf == NULL || !gui->State().GetBool( "2d_calc" ) ) {
		return;
	}

	const idScreenRect rect = R_ScreenRectFromDrawSurf( drawSurf );
	const float viewportWidth = Max( 1.0f, static_cast<float>( tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 ) );
	const float viewportHeight = Max( 1.0f, static_cast<float>( tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 ) );
	const float ratioX = static_cast<float>( SCREEN_WIDTH ) / viewportWidth;
	const float ratioY = static_cast<float>( SCREEN_HEIGHT ) / viewportHeight;
	float minX = rect.x1 * ratioX;
	float minY = rect.y1 * ratioY;
	float maxX = rect.x2 * ratioX;
	float maxY = rect.y2 * ratioY;

	// Retail Quake 4 writes raw 640x480 HUD coords here. openQ4's optional HUD aspect
	// correction remaps those coords into a centered 4:3 region, so undo that transform
	// first or the bracket box will be squeezed inward on widescreen displays.
	R_ConvertScreenToHudSpace( minX, minY );
	R_ConvertScreenToHudSpace( maxX, maxY );

	gui->SetStateFloat( "2d_min_x", minX );
	gui->SetStateFloat( "2d_min_y", minY );
	gui->SetStateFloat( "2d_max_x", maxX );
	gui->SetStateFloat( "2d_max_y", maxY );
	gui->SetStateBool( "2d_calc", false );
}

/*
=================
Stereo GUI sharing (VR-04)

Both eyes of a stereo frame draw a world GUI at the same game time, and the
quads a GUI emits are in its own 640x480 space, so they do not depend on the
eye. The first eye keeps them; the second eye emits those instead of running
the GUI again. A GUI that rendered a view of its own while drawing (a
renderDef window) is drawn again, because its quads alone would drop that
view from the second eye. Flat frames, subviews and nested GUIs never share.
=================
*/
static const int STEREO_GUI_SHARE_SLOTS = 32;

typedef struct {
	const idUserInterface *	gui;
	int						frameCount;
	int						time;
	idGuiModel *			model;
} stereoGuiShare_t;

static stereoGuiShare_t	stereoGuiShare[STEREO_GUI_SHARE_SLOTS];

idCVar r_vrStereoGuiShare( "r_vrStereoGuiShare", "1", CVAR_RENDERER | CVAR_BOOL,
	"the second eye of a stereo VR frame emits the world-GUI quads the first eye drew instead of running each GUI again" );

static bool R_StereoGuiShareEligible( void ) {
	return r_vrStereoGuiShare.GetBool() && R_VR_FrontEndEye() >= 0 && tr.guiRecursionLevel == 1 && tr.viewDef != NULL && !tr.viewDef->isSubview;
}

static stereoGuiShare_t *R_StereoGuiShareFind( const idUserInterface *gui ) {
	for ( int i = 0; i < STEREO_GUI_SHARE_SLOTS; i++ ) {
		stereoGuiShare_t &slot = stereoGuiShare[i];
		if ( slot.gui == gui && slot.frameCount == tr.frameCount && slot.time == tr.viewDef->renderView.time
				&& slot.model != NULL ) {
			return &slot;
		}
	}
	return NULL;
}

// The first eye's quads for this GUI, when the second eye can emit them.
static idGuiModel *R_StereoGuiShareLookup( const idUserInterface *gui ) {
	if ( R_VR_FrontEndEye() != 1 || !R_StereoGuiShareEligible() ) {
		return NULL;
	}
	stereoGuiShare_t *slot = R_StereoGuiShareFind( gui );
	return slot != NULL ? slot->model : NULL;
}

// Moves the GUI's freshly drawn quads (tr.guiModel) into a slot the second eye
// can find, and returns the model holding them now.
static idGuiModel *R_StereoGuiShareKeep( const idUserInterface *gui ) {
	if ( R_VR_FrontEndEye() != 0 || !R_StereoGuiShareEligible() ) {
		return tr.guiModel;
	}
	stereoGuiShare_t *slot = R_StereoGuiShareFind( gui );
	if ( slot == NULL ) {
		for ( int i = 0; i < STEREO_GUI_SHARE_SLOTS && slot == NULL; i++ ) {
			if ( stereoGuiShare[i].frameCount != tr.frameCount || stereoGuiShare[i].gui == NULL ) {
				slot = &stereoGuiShare[i];
			}
		}
	}
	if ( slot == NULL ) {
		return tr.guiModel;	// more GUIs in view than slots: the rest draw per eye
	}
	if ( slot->model == NULL ) {
		slot->model = new idGuiModel;
	}
	slot->gui = gui;
	slot->frameCount = tr.frameCount;
	slot->time = tr.viewDef->renderView.time;
	slot->model->Swap( *tr.guiModel );
	return slot->model;
}

// A GUI that rendered a view while drawing cannot be shared this frame.
static void R_StereoGuiShareForget( const idUserInterface *gui ) {
	for ( int i = 0; i < STEREO_GUI_SHARE_SLOTS; i++ ) {
		if ( stereoGuiShare[i].gui == gui ) {
			stereoGuiShare[i].gui = NULL;
			stereoGuiShare[i].frameCount = -1;
		}
	}
}

void R_ShutdownStereoGuiShare( void ) {
	for ( int i = 0; i < STEREO_GUI_SHARE_SLOTS; i++ ) {
		delete stereoGuiShare[i].model;
		stereoGuiShare[i].model = NULL;
		stereoGuiShare[i].gui = NULL;
		stereoGuiShare[i].frameCount = -1;
	}
}

/*
=================
R_RenderGuiSurf

Create a texture space on the given surface and
call the GUI generator to create quads for it.
=================
*/
void R_RenderGuiSurf( idUserInterface *gui, drawSurf_t *drawSurf, int entityGui ) {
	idVec3	origin, axis[3];

	// for testing the performance hit
	if ( r_skipGuiShaders.GetInteger() == 1 ) {
		return;
	}

	// don't allow an infinite recursion loop
	if ( tr.guiRecursionLevel == 4 ) {
		return;
	}

	tr.pc.c_guiSurfs++;

	// create the new matrix to draw on this surface
	R_SurfaceToTextureAxis( drawSurf->geo, origin, axis );

	float	guiModelMatrix[16];
	float	modelMatrix[16];

	guiModelMatrix[0] = axis[0][0] / 640.0;
	guiModelMatrix[4] = axis[1][0] / 480.0;
	guiModelMatrix[8] = axis[2][0];
	guiModelMatrix[12] = origin[0];

	guiModelMatrix[1] = axis[0][1] / 640.0;
	guiModelMatrix[5] = axis[1][1] / 480.0;
	guiModelMatrix[9] = axis[2][1];
	guiModelMatrix[13] = origin[1];

	guiModelMatrix[2] = axis[0][2] / 640.0;
	guiModelMatrix[6] = axis[1][2] / 480.0;
	guiModelMatrix[10] = axis[2][2];
	guiModelMatrix[14] = origin[2];

	guiModelMatrix[3] = 0;
	guiModelMatrix[7] = 0;
	guiModelMatrix[11] = 0;
	guiModelMatrix[15] = 1;

	myGlMultMatrix( guiModelMatrix, drawSurf->space->modelMatrix, 
			modelMatrix );

	// A render demo (v11) carries an entity's in-world GUI as the geometry it
	// drew; playback has no live GUI behind the entity, so it draws that.
	const idRenderEntityLocal *entityDef = drawSurf->space != NULL ? drawSurf->space->entityDef : NULL;
	idRenderWorldLocal *world = tr.viewDef->renderWorld;
	const bool demoWorld = entityGui >= 0 && entityDef != NULL && world != NULL && world == session->rw;
	idGuiModel *recorded = demoWorld && session->readDemo != NULL
		? world->DemoGuiSurface( entityDef->index, entityGui ) : NULL;

	tr.guiRecursionLevel++;

	// call the gui, which will call the 2D drawing functions
	tr.guiModel->Clear();
	idGuiModel *emitted = recorded;
	if ( emitted == NULL ) {
		// the second eye of a stereo frame emits the first eye's quads
		emitted = R_StereoGuiShareLookup( gui );
		if ( emitted != NULL ) {
			tr.pc.c_guiSurfsShared++;
		}
	}
	if ( emitted == NULL ) {
		const int viewCountBeforeRedraw = tr.viewCount;
		gui->Redraw( tr.viewDef->renderView.time, false );
		if ( demoWorld && session->writeDemo != NULL && tr.guiRecursionLevel == 1 && !tr.viewDef->isSubview ) {
			world->WriteGuiSurface( entityDef->index, entityGui, *tr.guiModel );
		}
		if ( tr.viewCount == viewCountBeforeRedraw ) {
			emitted = R_StereoGuiShareKeep( gui );
		} else {
			R_StereoGuiShareForget( gui );
			emitted = tr.guiModel;
		}
	}
	// The generated quads become ordinary 3D drawSurfs. Preserve that origin
	// explicitly so shared in-world ownership cannot claim root UI by sort.
	tr.inWorldGuiEmissionDepth++;
	emitted->EmitToCurrentView( modelMatrix, drawSurf->space->weaponDepthHack );
	tr.inWorldGuiEmissionDepth--;
	tr.guiModel->Clear();
	R_UpdateGuiScreenRectState( gui, drawSurf );

	tr.guiRecursionLevel--;
}




/*
================,
R_ReloadGuis_f

Reloads any guis that have had their file timestamps changed.
An optional "all" parameter will cause all models to reload, even
if they are not out of date.

Should we also reload the map models?
================
*/
void R_ReloadGuis_f( const idCmdArgs &args ) {
	bool all;

	if ( !idStr::Icmp( args.Argv(1), "all" ) ) {
		all = true;
		common->Printf( "Reloading all gui files...\n" );
	} else {
		all = false;
		common->Printf( "Checking for changed gui files...\n" );
	}

	uiManager->Reload( all );
}

/*
================,
R_ListGuis_f

================
*/
void R_ListGuis_f( const idCmdArgs &args ) {
	uiManager->ListGuis();
}
