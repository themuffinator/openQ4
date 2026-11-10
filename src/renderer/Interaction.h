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

#ifndef __INTERACTION_H__
#define __INTERACTION_H__

/*
===============================================================================

	Interaction between entityDef surfaces and a lightDef.

	Interactions with no lightTris and no shadowTris are still
	valid, because they show that a given entityDef / lightDef
	do not interact, even though they share one or more areas.

===============================================================================
*/

// LIGHT_CULL_ALL_FRONT, LIGHT_CLIP_EPSILON, and srfCullInfo_t are defined by
// the shared render-geometry library
#include "../rendergeo/RenderGeometry.h"


typedef struct {		
	// if lightTris == LIGHT_TRIS_DEFERRED, then the calculation of the
	// lightTris has been deferred, and must be done if ambientTris is visible
	srfTriangles_t *		lightTris;
	// Native PBR evaluates the interpolated normal per fragment. Keep its
	// light-volume-clipped triangles separate from the classic facing subset.
	// Owned for this interaction's lifetime; NULL means the classic set suffices.
	srfTriangles_t *		pbrLightTris;

	// shadow volume triangle surface
	srfTriangles_t *		shadowTris;

	// so we can check ambientViewCount before adding lightTris, and get
	// at the shared vertex and possibly shadowVertex caches
	srfTriangles_t *		ambientTris;

	const idMaterial *		shader;

	// openQ4's shadow-map paths draw ambient triangles instead of stencil volumes,
	// but they must still honor Quake 4's entity-level shadow LOD. Keep validity
	// separate so cvar-gated shadow-map features can ask for the decision lazily;
	// the decision is refreshed for each renderer view's coverage and distance.
	bool					shadowLODDecisionValid;
	bool					shadowLODAdmitted;
	int						shadowLODDecisionView;
	// The classic volume path admitted this surface. A static-world surface
	// may be represented by the light's combined prelight volume instead of
	// carrying per-interaction shadowTris.
	bool					shadowStencilEligible;
	bool					shadowStencilUsesPrelight;

	// Lifetime cache of the point-light emitter-panel caster verdict. Its inputs
	// (shadow shader, ambientTris, the interaction's local light origin/radius)
	// are all fixed for the surface interaction's lifetime; any change frees and
	// rebuilds these surfaces, clearing the flag, so it never goes stale.
	bool					pointEmitterCasterVerdictValid;
	bool					pointEmitterCasterSkip;

	int						expCulled;			// only for the experimental shadow buffer renderer

	srfCullInfo_t			cullInfo;
} surfaceInteraction_t;


typedef struct areaNumRef_s {
	struct areaNumRef_s *	next;
	int						areaNum;
} areaNumRef_t;


class idRenderEntityLocal;
class idRenderLightLocal;

class idInteraction {
public:
	// this may be 0 if the light and entity do not actually intersect
	// -1 = an untested interaction
	int						numSurfaces;

	// if there is a whole-entity optimized shadow hull, it will
	// be present as a surfaceInteraction_t with a NULL ambientTris, but
	// possibly having a shader to specify the shadow sorting order
	surfaceInteraction_t *	surfaces;
	
	// get space from here, if NULL, it is a pre-generated shadow volume from dmap
	idRenderEntityLocal *	entityDef;
	idRenderLightLocal *	lightDef;

	idInteraction *			lightNext;				// for lightDef chains
	idInteraction *			lightPrev;
	idInteraction *			entityNext;				// for entityDef chains
	idInteraction *			entityPrev;

	// Vulkan shadow-map admission (R_ShadowMapLightWillUseShadowMaps): whether
	// a receiver surface of this interaction uses authored GLSL lighting, which
	// only the light's stencil chain can shadow. Cached for the current surface
	// set; a generation of -1 means not computed yet. While CreateInteraction
	// fills the slots, buildModel names the model being built so the verdict
	// can cover every surface the interaction may still fill.
	mutable int				stencilReceiverGeneration;
	mutable bool			stencilReceiverNeeded;
	const idRenderModel *	buildModel;
	// The view in which AddActiveInteraction returned with this interaction still
	// unbuilt. It links no receivers in that view, so the receiver scan need not
	// wait for it; -1 once its surfaces are freed.
	int						unbuiltVisitView;
	// CreateInteraction left out a dynamic model's stencil volumes because the
	// light was going to render shadow maps. The surfaces are built again, with
	// their volumes, when the light returns to stencil.
	bool					shadowVolumesSuppressed;

public:
							idInteraction( void );

	// because these are generated and freed each game tic for active elements all
	// over the world, we use a custom pool allocater to avoid memory allocation overhead
	// and fragmentation
	static idInteraction *	AllocAndLink( idRenderEntityLocal *edef, idRenderLightLocal *ldef );

	// unlinks from the entity and light, frees all surfaceInteractions,
	// and puts it back on the free list
	void					UnlinkAndFree( void );

	// free the interaction surfaces
	void					FreeSurfaces( void );

	// makes the interaction empty for when the light and entity do not actually intersect
	// all empty interactions are linked at the end of the light's and entity's interaction list
	void					MakeEmpty( void );

	// returns true if the interaction is empty
	bool					IsEmpty( void ) const { return ( numSurfaces == 0 ); }

	// returns true if the interaction is not yet completely created
	bool					IsDeferred( void ) const { return ( numSurfaces == -1 ); }

	// returns true if the interaction has shadows
	bool					HasShadows( void ) const;

	// counts up the memory used by all the surfaceInteractions, which
	// will be used to determine when we need to start purging old interactions
	int						MemoryUsed( void );

	// makes sure all necessary light surfaces and shadow surfaces are created, and
	// calls R_LinkLightSurf() for each one
	void					AddActiveInteraction( void );

private:
	enum {
		FRUSTUM_UNINITIALIZED,
		FRUSTUM_INVALID,
		FRUSTUM_VALID,
		FRUSTUM_VALIDAREAS,
	}						frustumState;
	idFrustum				frustum;				// frustum which contains the interaction
	areaNumRef_t *			frustumAreas;			// numbers of the areas the frustum touches

	int						dynamicModelFrameCount;	// so we can tell if a callback model animated

private:
	// actually create the interaction
	void					CreateInteraction( const idRenderModel *model );

	// unlink from entity and light lists
	void					Unlink( void );

	// try to determine if the entire interaction, including shadows, is guaranteed
	// to be outside the view frustum
	bool					CullInteractionByViewFrustum( const idFrustum &viewFrustum );

	// determine the minimum scissor rect that will include the interaction shadows
	// projected to the bounds of the light
	idScreenRect			CalcInteractionScissorRectangle( const idFrustum &viewFrustum );
};


void R_CalcInteractionFacing( const idRenderEntityLocal *ent, const srfTriangles_t *tri, const idRenderLightLocal *light, srfCullInfo_t &cullInfo );
void R_CalcInteractionCullBits( const idRenderEntityLocal *ent, const srfTriangles_t *tri, const idRenderLightLocal *light, srfCullInfo_t &cullInfo );
void R_FreeInteractionCullInfo( srfCullInfo_t &cullInfo );

void R_ShowInteractionMemory_f( const idCmdArgs &args );

#endif /* !__INTERACTION_H__ */
