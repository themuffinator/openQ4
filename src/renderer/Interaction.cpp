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
#include "ShadowMapClassification.h"

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
bool R_MD5R_CreateLightTris( const srfTriangles_t &sourceTri, srfTriangles_t *destTri, int &c_backfaced, int &c_distance, const byte *facing, const byte *cullBits, bool includeBackFaces );
#endif

/*
===========================================================================

idInteraction implementation

===========================================================================
*/

// FIXME: use private allocator for srfCullInfo_t

static const double SHADOW_LOD_RANDOM_UNIT = 0.000030518509;
static const double SHADOW_LOD_MAX_FRAME_DELAY = 1000.0;
static const int SHADOW_LOD_RETAIL_RAND_MASK = 0x7fff;

static bool R_SurfaceShaderCreatesLightTris( const idMaterial *surfaceShader, const idMaterial *lightShader ) {
	// Retail Quake 4 uses `noFog` / ReceivesFog() for fog and blend lights
	// instead of the normal lighting-stage contract.
	if ( lightShader->IsFogLight() || lightShader->IsBlendLight() ) {
		return surfaceShader->ReceivesFog();
	}

	return surfaceShader->ReceivesLighting();
}

static bool R_ShouldCreateInteractionShadow( idRenderEntityLocal *entityDef ) {
	// Quake 4 adds a screen-coverage / distance gate on top of the usual
	// light/material shadow eligibility checks. The randomized frame hold keeps
	// crowds from all toggling shadow state on the same frame.
	if ( entityDef == NULL || entityDef->parms.shadowLODDistance < 1.0f || !r_lod_shadows.GetBool() ) {
		return true;
	}

	viewEntity_t *viewEntity = entityDef->viewEntity;
	if ( viewEntity == NULL ) {
		return true;
	}

	if ( entityDef->parms.suppressLOD == 1
		|| entityDef->LODModificationFrame > idLib::frameNumber
		|| viewEntity->screenCoverage >= r_lod_shadows_percent.GetFloat()
		|| viewEntity->distanceToCamera < entityDef->parms.shadowLODDistance ) {
		if ( entityDef->LODModificationFrame < idLib::frameNumber ) {
			// Deterministic stand-in for the retail rand(): identical demo
			// playback must drop/hold entity shadows on identical frames, and
			// the per-entity hash still staggers crowds across the hold window.
			const unsigned int hash = ( static_cast<unsigned int>( entityDef->index ) * 2654435761u ) ^ ( static_cast<unsigned int>( idLib::frameNumber ) * 40503u );
			const int retailRand = static_cast<int>( ( hash >> 8 ) & SHADOW_LOD_RETAIL_RAND_MASK );
			entityDef->LODModificationFrame = idLib::frameNumber + static_cast<int>( static_cast<double>( retailRand ) * SHADOW_LOD_RANDOM_UNIT * SHADOW_LOD_MAX_FRAME_DELAY );
		}
		return true;
	}

	return false;
}

static bool R_CachedInteractionShadowLODAdmitted( surfaceInteraction_t *sint, idRenderEntityLocal *entityDef ) {
	if ( sint == NULL ) {
		return R_ShouldCreateInteractionShadow( entityDef );
	}
	// Re-evaluate once per frame: a creation-time-only decision freezes the
	// first verdict for the surface's lifetime, permanently dropping shadows
	// for static entities first seen at distance (and never shedding them for
	// entities first seen close). The check is a handful of compares.
	if ( !sint->shadowLODDecisionValid || sint->shadowLODDecisionFrame != idLib::frameNumber ) {
		sint->shadowLODAdmitted = R_ShouldCreateInteractionShadow( entityDef );
		sint->shadowLODDecisionValid = true;
		sint->shadowLODDecisionFrame = idLib::frameNumber;
	}
	return sint->shadowLODAdmitted;
}

static bool R_TranslucentShadowMapMomentsSupportedForLight( const idRenderLightLocal *lightDef ) {
	// The Vulkan backend does not own the experimental moment attachments or
	// their translucent caster chains. Reject that tier explicitly so stale
	// OpenGL capability state can never admit casters Vulkan would omit; its
	// stock translucent shadow casters instead use binary stencil-parity depth.
	// r_actualRenderApi is CVAR_ROM, set once at renderer-module selection;
	// re-resolve the by-name lookup at most once per frame instead of per call.
	static int cachedApiFrame = -1;
	static bool cachedVulkanApi = false;
	if ( cachedApiFrame != idLib::frameNumber ) {
		const char *activeRenderApi =
			cvarSystem != NULL
				? cvarSystem->GetCVarString( "r_actualRenderApi" )
				: "";
		cachedVulkanApi = idStr::Icmp( activeRenderApi, "vulkan" ) == 0;
		cachedApiFrame = idLib::frameNumber;
	}
	if ( cachedVulkanApi ) {
		return false;
	}
	return r_shadowMapTranslucentMoments.GetBool() &&
		glConfig.GLSLProgramAvailable &&
		glConfig.maxTextureUnits >= 9 &&
		glConfig.maxTextureImageUnits >= 9 &&
		glConfig.maxDrawBuffers >= 3 &&
		glConfig.maxColorAttachments >= 3 &&
		( lightDef == NULL || !lightDef->parms.pointLight || lightDef->parms.parallel || glConfig.cubeMapAvailable );
}

/*
=================
R_ShadowMapsNeedPerSurfaceStencilVolumes

Optimized prelights combine every static-world caster for a light into one
volume. A partial filtered shadow map needs only S\M as its stencil
supplement, so the combined volume cannot be used without hardening casters
already represented in the map. Keep the stock per-surface volume path for
every backend light that can actually use mapped shadows; stencil-only paths
retain the optimized prelight behavior.
=================
*/
bool R_ShadowMapsNeedPerSurfaceStencilVolumes(
		const idRenderLightLocal *lightDef ) {
	if ( lightDef == NULL || !r_shadows.GetBool() ||
		!r_useShadowMap.GetBool() ) {
		return false;
	}
	if ( lightDef->parms.pointLight && !lightDef->parms.parallel &&
		!r_shadowMapPointLights.GetBool() ) {
		return false;
	}
	return true;
}

typedef struct {
	bool				shaderPresent;
	bool				dedicatedCollision;
	materialCoverage_t	coverage;
	bool				surfaceCastsShadow;
	bool				explicitlyDisablesShadows;
	bool				hasGui;
	bool				hasSubview;
} shadowMapMaterialCasterPolicy_t;

static shadowMapMaterialCasterPolicy_t R_ShadowMapMaterialCasterPolicyForShader( const idMaterial *shader ) {
	shadowMapMaterialCasterPolicy_t policy;
	memset( &policy, 0, sizeof( policy ) );
	policy.coverage = MC_BAD;

	if ( shader == NULL ) {
		return policy;
	}

	policy.shaderPresent = true;
	policy.dedicatedCollision = shader->IsDedicatedCollisionSurface();
	policy.coverage = shader->Coverage();
	policy.surfaceCastsShadow = shader->SurfaceCastsShadow();
	policy.explicitlyDisablesShadows = shader->ExplicitlyDisablesShadows();
	policy.hasGui = shader->HasGui();
	policy.hasSubview = shader->HasSubview();
	return policy;
}

static bool R_ShadowMapMaterialPolicyCanCastOpaque( const shadowMapMaterialCasterPolicy_t &policy ) {
	// Shadow maps share interaction ownership and material policy with stencil
	// shadows, but render ambient triangles into depth/moment maps instead of
	// consuming stencil-volume geometry. Perforated materials stay in this path
	// so their alpha-tested stages can be sampled by the shadow caster shader.
	return policy.shaderPresent &&
		!policy.dedicatedCollision &&
		policy.coverage != MC_TRANSLUCENT &&
		policy.surfaceCastsShadow &&
		!policy.hasGui &&
		!policy.hasSubview;
}

static bool R_ShadowMapMaterialPolicyCanCastTranslucent( const shadowMapMaterialCasterPolicy_t &policy ) {
	// Translucent moment casters are opt-in at the light/resource level. The
	// material-side rule is deliberately narrower: GUI and subview surfaces stay
	// excluded, while ordinary translucent effect stages may contribute moments
	// despite the coverage-implied no-shadow default. Authored opt-outs remain.
	return policy.shaderPresent &&
		policy.coverage == MC_TRANSLUCENT &&
		!policy.explicitlyDisablesShadows &&
		!policy.hasGui &&
		!policy.hasSubview;
}

static shadowMapCasterRejectReason_t R_ShadowMapMaterialPolicyRejectReason( const shadowMapMaterialCasterPolicy_t &policy, const bool translucentMomentsEnabled, const bool translucentShadowMapSupported ) {
	if ( !policy.shaderPresent ) {
		return SHADOWMAP_CASTER_REJECT_UNKNOWN;
	}
	if ( policy.dedicatedCollision ) {
		return SHADOWMAP_CASTER_REJECT_DEDICATED_COLLISION;
	}
	if ( policy.hasGui ) {
		return SHADOWMAP_CASTER_REJECT_GUI;
	}
	if ( policy.hasSubview ) {
		return SHADOWMAP_CASTER_REJECT_SUBVIEW;
	}
	if ( policy.explicitlyDisablesShadows ) {
		return SHADOWMAP_CASTER_REJECT_SURFACE_NO_SHADOW;
	}
	if ( policy.coverage == MC_TRANSLUCENT ) {
		if ( !translucentMomentsEnabled ) {
			return SHADOWMAP_CASTER_REJECT_TRANSLUCENT_DISABLED;
		}
		if ( !translucentShadowMapSupported ) {
			return SHADOWMAP_CASTER_REJECT_TRANSLUCENT_UNSUPPORTED;
		}
		return SHADOWMAP_CASTER_REJECT_UNKNOWN;
	}
	if ( !policy.surfaceCastsShadow ) {
		return SHADOWMAP_CASTER_REJECT_SURFACE_NO_SHADOW;
	}
	return SHADOWMAP_CASTER_REJECT_UNKNOWN;
}

static bool R_ShadowMapCasterAdmissionCheck( const char *name, const shadowMapMaterialCasterPolicy_t &policy, const bool expectedOpaque, const bool expectedTranslucent, const shadowMapCasterRejectReason_t expectedDisabledReason, const shadowMapCasterRejectReason_t expectedUnsupportedReason, const shadowMapCasterRejectReason_t expectedSupportedReason ) {
	const bool opaque = R_ShadowMapMaterialPolicyCanCastOpaque( policy );
	const bool translucent = R_ShadowMapMaterialPolicyCanCastTranslucent( policy );
	const shadowMapCasterRejectReason_t disabledReason = R_ShadowMapMaterialPolicyRejectReason( policy, false, false );
	const shadowMapCasterRejectReason_t unsupportedReason = R_ShadowMapMaterialPolicyRejectReason( policy, true, false );
	const shadowMapCasterRejectReason_t supportedReason = R_ShadowMapMaterialPolicyRejectReason( policy, true, true );

	if ( opaque == expectedOpaque
		&& translucent == expectedTranslucent
		&& disabledReason == expectedDisabledReason
		&& unsupportedReason == expectedUnsupportedReason
		&& supportedReason == expectedSupportedReason ) {
		return true;
	}

	common->Printf(
		"ShadowMap caster admission self-test failed: %s opaque=%d/%d translucent=%d/%d reasons(disabled=%d/%d unsupported=%d/%d supported=%d/%d)\n",
		name,
		opaque ? 1 : 0,
		expectedOpaque ? 1 : 0,
		translucent ? 1 : 0,
		expectedTranslucent ? 1 : 0,
		disabledReason,
		expectedDisabledReason,
		unsupportedReason,
		expectedUnsupportedReason,
		supportedReason,
		expectedSupportedReason );
	return false;
}

static bool R_ShadowMapExplicitNoShadowsSelfTest( void );

bool R_ShadowMapCasterAdmissionSelfTest( void ) {
	if ( !R_ShadowMapExplicitNoShadowsSelfTest() ) {
		return false;
	}
	shadowMapMaterialCasterPolicy_t policy;
	memset( &policy, 0, sizeof( policy ) );
	policy.coverage = MC_BAD;

	if ( !R_ShadowMapCasterAdmissionCheck( "null", policy, false, false, SHADOWMAP_CASTER_REJECT_UNKNOWN, SHADOWMAP_CASTER_REJECT_UNKNOWN, SHADOWMAP_CASTER_REJECT_UNKNOWN ) ) {
		return false;
	}

	policy.shaderPresent = true;
	policy.coverage = MC_OPAQUE;
	policy.surfaceCastsShadow = true;
	if ( !R_ShadowMapCasterAdmissionCheck( "opaque", policy, true, false, SHADOWMAP_CASTER_REJECT_UNKNOWN, SHADOWMAP_CASTER_REJECT_UNKNOWN, SHADOWMAP_CASTER_REJECT_UNKNOWN ) ) {
		return false;
	}

	policy.coverage = MC_PERFORATED;
	if ( !R_ShadowMapCasterAdmissionCheck( "perforated", policy, true, false, SHADOWMAP_CASTER_REJECT_UNKNOWN, SHADOWMAP_CASTER_REJECT_UNKNOWN, SHADOWMAP_CASTER_REJECT_UNKNOWN ) ) {
		return false;
	}

	policy.coverage = MC_OPAQUE;
	policy.surfaceCastsShadow = false;
	if ( !R_ShadowMapCasterAdmissionCheck( "surfaceNoShadow", policy, false, false, SHADOWMAP_CASTER_REJECT_SURFACE_NO_SHADOW, SHADOWMAP_CASTER_REJECT_SURFACE_NO_SHADOW, SHADOWMAP_CASTER_REJECT_SURFACE_NO_SHADOW ) ) {
		return false;
	}

	policy.surfaceCastsShadow = true;
	policy.dedicatedCollision = true;
	if ( !R_ShadowMapCasterAdmissionCheck( "dedicatedCollision", policy, false, false, SHADOWMAP_CASTER_REJECT_DEDICATED_COLLISION, SHADOWMAP_CASTER_REJECT_DEDICATED_COLLISION, SHADOWMAP_CASTER_REJECT_DEDICATED_COLLISION ) ) {
		return false;
	}
	policy.dedicatedCollision = false;

	policy.hasGui = true;
	if ( !R_ShadowMapCasterAdmissionCheck( "gui", policy, false, false, SHADOWMAP_CASTER_REJECT_GUI, SHADOWMAP_CASTER_REJECT_GUI, SHADOWMAP_CASTER_REJECT_GUI ) ) {
		return false;
	}
	policy.hasGui = false;

	policy.hasSubview = true;
	if ( !R_ShadowMapCasterAdmissionCheck( "subview", policy, false, false, SHADOWMAP_CASTER_REJECT_SUBVIEW, SHADOWMAP_CASTER_REJECT_SUBVIEW, SHADOWMAP_CASTER_REJECT_SUBVIEW ) ) {
		return false;
	}
	policy.hasSubview = false;

	policy.coverage = MC_TRANSLUCENT;
	policy.surfaceCastsShadow = false;
	if ( !R_ShadowMapCasterAdmissionCheck( "translucent", policy, false, true, SHADOWMAP_CASTER_REJECT_TRANSLUCENT_DISABLED, SHADOWMAP_CASTER_REJECT_TRANSLUCENT_UNSUPPORTED, SHADOWMAP_CASTER_REJECT_UNKNOWN ) ) {
		return false;
	}
	policy.explicitlyDisablesShadows = true;
	if ( !R_ShadowMapCasterAdmissionCheck( "translucentNoShadows", policy, false, false, SHADOWMAP_CASTER_REJECT_SURFACE_NO_SHADOW, SHADOWMAP_CASTER_REJECT_SURFACE_NO_SHADOW, SHADOWMAP_CASTER_REJECT_SURFACE_NO_SHADOW ) ) {
		return false;
	}
	policy.explicitlyDisablesShadows = false;

	policy.hasGui = true;
	if ( !R_ShadowMapCasterAdmissionCheck( "translucentGui", policy, false, false, SHADOWMAP_CASTER_REJECT_GUI, SHADOWMAP_CASTER_REJECT_GUI, SHADOWMAP_CASTER_REJECT_GUI ) ) {
		return false;
	}
	policy.hasGui = false;

	policy.hasSubview = true;
	if ( !R_ShadowMapCasterAdmissionCheck( "translucentSubview", policy, false, false, SHADOWMAP_CASTER_REJECT_SUBVIEW, SHADOWMAP_CASTER_REJECT_SUBVIEW, SHADOWMAP_CASTER_REJECT_SUBVIEW ) ) {
		return false;
	}

	common->Printf( "ShadowMap caster admission self-test passed\n" );
	return true;
}

static void R_RecordShadowMapLODDecision( viewLight_t *vLight, const materialCoverage_t coverage, const bool translucentCaster, const bool admitted ) {
	if ( vLight == NULL ) {
		return;
	}

	vLight->shadowMapLODTestCount++;
	if ( admitted ) {
		return;
	}

	vLight->shadowMapLODRejectedCount++;
	if ( coverage == MC_PERFORATED ) {
		vLight->shadowMapLODAlphaRejectedCount++;
	}
	if ( translucentCaster || coverage == MC_TRANSLUCENT ) {
		vLight->shadowMapLODTranslucentRejectedCount++;
	}
}

bool R_ShadowMapLODAdmissionSelfTest( void ) {
	viewLight_t vLight;
	memset( &vLight, 0, sizeof( vLight ) );

	R_RecordShadowMapLODDecision( NULL, MC_OPAQUE, false, false );
	R_RecordShadowMapLODDecision( &vLight, MC_OPAQUE, false, true );
	R_RecordShadowMapLODDecision( &vLight, MC_PERFORATED, false, false );
	R_RecordShadowMapLODDecision( &vLight, MC_TRANSLUCENT, true, false );

	if ( vLight.shadowMapLODTestCount != 3
		|| vLight.shadowMapLODRejectedCount != 2
		|| vLight.shadowMapLODAlphaRejectedCount != 1
		|| vLight.shadowMapLODTranslucentRejectedCount != 1 ) {
		common->Printf(
			"ShadowMap LOD admission self-test failed: tests=%d rejected=%d alpha=%d translucent=%d\n",
			vLight.shadowMapLODTestCount,
			vLight.shadowMapLODRejectedCount,
			vLight.shadowMapLODAlphaRejectedCount,
			vLight.shadowMapLODTranslucentRejectedCount );
		return false;
	}

	common->Printf( "ShadowMap LOD admission self-test passed\n" );
	return true;
}

static bool R_ShadowMapShaderCanCastOpaque( const idMaterial *shader ) {
	return R_ShadowMapMaterialPolicyCanCastOpaque( R_ShadowMapMaterialCasterPolicyForShader( shader ) );
}

static bool R_ShadowMapShaderCanCastTranslucent( const idMaterial *shader ) {
	return R_ShadowMapMaterialPolicyCanCastTranslucent( R_ShadowMapMaterialCasterPolicyForShader( shader ) );
}

// Stencil parity: translucent materials that cast stencil volumes today
// (forceShadows, or the default-on r_stencilTranslucentShadows lighting
// class) must not silently lose their shadows when a light renders shadow
// maps. When the opt-in moments tier is unavailable they cast binary depth
// through the opaque caster chain - the same solid occlusion their stencil
// volumes produce in the shipping path.
static bool R_TranslucentStencilCasterEligible( const idMaterial *shader, const bool enabled ) {
	return enabled && shader != NULL && shader->Coverage() == MC_TRANSLUCENT &&
		shader->ReceivesLighting() && !shader->HasGui() && !shader->HasSubview() &&
		!shader->ExplicitlyDisablesShadows();
}

static bool R_ShadowMapShaderCanCastStencilParityTranslucent( const idMaterial *shader ) {
	if ( shader == NULL || shader->IsDedicatedCollisionSurface() || shader->HasGui() || shader->HasSubview() ) {
		return false;
	}
	if ( shader->Coverage() != MC_TRANSLUCENT ) {
		return false;
	}
	return shader->SurfaceCastsShadow() ||
		R_TranslucentStencilCasterEligible( shader, r_stencilTranslucentShadows.GetBool() );
}

static bool R_ShadowMapExplicitNoShadowsSelfTest( void ) {
	static const char *directives[] = {
		"translucent", "translucent noShadows", "translucent DECAL_MACRO",
		"translucent noShadows forceShadows", "translucent forceShadows noShadows"
	};
	for ( int test = 0; test < 5; ++test ) {
		idDecl *decl = declManager->AllocateDecl( DECL_MATERIAL );
		if ( decl == NULL ) {
			return false;
		}
		idMaterial *material = static_cast<idMaterial *>( decl );
		char source[256];
		idStr::snPrintf( source, sizeof( source ),
			"material _shadow_noShadows_selftest { %s { blend diffusemap map _white } }", directives[test] );
		const bool blocked = test == 1 || test == 2;
		const bool ok = material->Parse( source, static_cast<int>( strlen( source ) ) ) &&
			material->Coverage() == MC_TRANSLUCENT &&
			material->TestMaterialFlag( MF_NOSHADOWS ) &&
			material->ExplicitlyDisablesShadows() == blocked &&
			R_TranslucentStencilCasterEligible( material, true ) == !blocked &&
			!R_TranslucentStencilCasterEligible( material, false ) &&
			R_ShadowMapShaderCanCastTranslucent( material ) == !blocked &&
			material->SurfaceCastsShadow() == ( test >= 3 );
		DeclManager_FreeAllocatedDecl( decl );
		if ( !ok ) {
			common->Printf( "ShadowMap explicit noShadows self-test failed: %s\n", directives[test] );
			return false;
		}
	}
	common->Printf( "ShadowMap explicit noShadows self-test passed: 5 parsed material policies\n" );
	return true;
}

static bool R_ShadowMapShaderSpectrumMatchesLight( const idMaterial *shader, const idRenderLightLocal *lightDef ) {
	return shader != NULL &&
		( lightDef == NULL || lightDef->lightShader == NULL || shader->Spectrum() == lightDef->lightShader->Spectrum() );
}

static void R_LinkShadowMapCasterSurf( const drawSurf_t **link, const srfTriangles_t *tri, const viewEntity_t *space,
		const renderEntity_t *renderEntity, const idMaterial *shader, const idScreenRect &scissor ) {
	if ( !space ) {
		space = &tr.viewDef->worldSpace;
	}

	drawSurf_t *drawSurf = (drawSurf_t *)R_FrameAlloc( sizeof( *drawSurf ) );
	drawSurf->geo = tri;
	drawSurf->space = space;
	drawSurf->material = shader;
	drawSurf->sort = 0.0f;
	drawSurf->scissorRect = scissor;
	drawSurf->dsFlags = 0;
	drawSurf->dynamicTexCoords = NULL;
	drawSurf->texGenTransformAndViewOrg = NULL;
	drawSurf->decalColorCache = NULL;
	drawSurf->decalColorOffset = 0;
	drawSurf->decalColorStride = 0;
	drawSurf->decalColorStageCount = 0;
	drawSurf->area = NULL;

	drawSurf->shaderRegisters = R_SetupDrawSurfShaderRegisters( space, renderEntity, shader );
	R_FinalizeDrawSurf( drawSurf );

	drawSurf->nextOnLight = *link;
	*link = drawSurf;
}

static void R_TouchShadowMapCache( vertCache_t *cache ) {
	R_TouchVertexCache( cache );
}

static bool R_EnsureShadowMapCasterCaches( srfTriangles_t *casterTris, const idRenderEntityLocal *entityDef ) {
	if ( casterTris == NULL ) {
		return false;
	}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( casterTris->primBatchMesh != NULL ) {
		return R_CreatePackedSurfaceFrameCaches( casterTris, false, true );
	}
#endif

	if ( !casterTris->ambientCache && !R_CreateAmbientCache( casterTris, false ) ) {
		return false;
	}

	if ( !casterTris->indexCache && casterTris->numIndexes > 0 && R_StaticIndexCacheAllowed( entityDef ) ) {
		vertexCache.Alloc(
			casterTris->indexes,
			casterTris->numIndexes * sizeof( casterTris->indexes[0] ),
			&casterTris->indexCache,
			true );
	}

	return casterTris->ambientCache != NULL;
}

static bool R_ShadowMapConservativeCastersEnabled( void ) {
	return r_useShadowMap.GetBool() && r_shadowMapConservativeCasters.GetBool();
}

static bool R_ShadowMapEntityTouchesConnectedArea( const idRenderEntityLocal *entityDef ) {
	if ( entityDef == NULL || tr.viewDef == NULL || tr.viewDef->connectedAreas == NULL ) {
		return true;
	}

	bool sawArea = false;
	for ( const areaReference_t *ref = entityDef->entityRefs; ref != NULL; ref = ref->ownerNext ) {
		if ( ref->area == NULL ) {
			continue;
		}
		sawArea = true;
		const int areaNum = ref->area->areaNum;
		if ( areaNum >= 0 && areaNum < tr.viewDef->renderWorld->NumAreas() && tr.viewDef->connectedAreas[areaNum] ) {
			return true;
		}
	}

	// Entities without area refs are unusual transient cases; keep them conservative.
	return !sawArea;
}

static int R_ShadowMapHashInt( int hash, const int value ) {
	const unsigned int h = static_cast<unsigned int>( hash );
	const unsigned int v = static_cast<unsigned int>( value );
	return static_cast<int>( ( h ^ v ) * 16777619u );
}

static int R_ShadowMapHashString( int hash, const char *value ) {
	return R_ShadowMapHashInt( hash, value != NULL ? idStr::Hash( value ) : 0 );
}

static bool R_ShadowMapCasterIsDynamic( const idRenderEntityLocal *entityDef, const idMaterial *shader ) {
	// Cutout coverage can change through stage conditions, image animation,
	// or texture matrices without an entity update. Keep that depth live while
	// allowing the opaque static scene to remain resident.
	if ( shader != NULL && shader->Coverage() == MC_PERFORATED ) {
		return true;
	}
	if ( entityDef == NULL || entityDef->parms.hModel == NULL ) {
		return true;
	}
	// An entity only defeats static shadow-map caching while it is actually
	// changing: a transform/parms update (lastModifiedFrameNum) or a dynamic
	// model regeneration (dynamicModelFrameCount). Skeletal and AF entities
	// keep their callback and cached dynamic model forever, so classifying on
	// those alone made every settled ragdoll/corpse permanently dynamic and
	// forced a full shadow-map re-render of every containing light each
	// frame. A resting entity's pose cannot change without one of the two
	// frame counters advancing, and either advance also changes the caster
	// signature hash, so reuse stays correct when it wakes.
	const int recentFrame = tr.frameCount - 1;
	if ( entityDef->lastModifiedFrameNum >= recentFrame ) {
		return true;
	}
	const bool hasDynamicGeometry =
		entityDef->parms.callback != NULL ||
		entityDef->dynamicModel != NULL ||
		entityDef->parms.hModel->IsDynamicModel() != DM_STATIC;
	return hasDynamicGeometry && entityDef->dynamicModelFrameCount >= recentFrame;
}

static void R_RecordShadowMapCaster( viewLight_t *vLight,
		const idRenderEntityLocal *entityDef, const idMaterial *shader,
		const srfTriangles_t *casterTris, const bool translucent,
		const bool expandedCaster ) {
	if ( vLight == NULL ) {
		return;
	}

	const bool dynamicCaster = R_ShadowMapCasterIsDynamic( entityDef, shader );
	vLight->shadowMapCasterCount++;
	if ( shader != NULL && shader->Coverage() == MC_PERFORATED ) {
		vLight->shadowMapAlphaCasterCount++;
	}
	if ( translucent ) {
		vLight->shadowMapTranslucentCasterCount++;
	}
	if ( dynamicCaster ) {
		vLight->shadowMapDynamicCasterCount++;
	} else {
		vLight->shadowMapStaticCasterCount++;
	}
	if ( expandedCaster ) {
		vLight->shadowMapExpandedCasterCount++;
	}

	// Dynamic casters are excluded from the signature: they are kept out of
	// cached static tiles and composed over them per frame, so their motion
	// must not invalidate the cache. Static casters hash transform and
	// modification state so any change still forces a fresh static render.
	if ( dynamicCaster ) {
		return;
	}
	int hash = ( vLight->shadowMapCasterSignature != 0 ) ? vLight->shadowMapCasterSignature : static_cast<int>( 2166136261u );
	hash = R_ShadowMapHashInt( hash, entityDef != NULL ? entityDef->index : -1 );
	hash = R_ShadowMapHashInt( hash, shader != NULL ? static_cast<int>( shader->Coverage() ) : -1 );
	hash = R_ShadowMapHashString( hash, shader != NULL ? shader->GetName() : NULL );
	hash = R_ShadowMapHashInt( hash, shader != NULL ? shader->GetNumStages() : 0 );
	hash = R_ShadowMapHashInt( hash,
		shader != NULL ? static_cast<int>( shader->GetCullType() ) : -1 );
	hash = R_ShadowMapHashInt( hash, translucent ? 1 : 0 );
	hash = R_ShadowMapHashInt( hash, expandedCaster ? 1 : 0 );
	if ( casterTris != NULL ) {
		// Automatic caster culling depends on these live geometry properties.
		// Include them in the physical cache identity so model/decl reloads or
		// topology changes cannot reuse depth rendered under another policy.
		hash = R_ShadowMapHashInt( hash, casterTris->perfectHull ? 1 : 0 );
		hash = R_ShadowMapHashInt( hash, casterTris->numVerts );
		hash = R_ShadowMapHashInt( hash, casterTris->numIndexes );
		for ( int corner = 0; corner < 2; ++corner ) {
			for ( int component = 0; component < 3; ++component ) {
				hash = R_ShadowMapHashFloat( hash,
					casterTris->bounds[ corner ][ component ] );
			}
		}
	}
	if ( entityDef != NULL ) {
		hash = R_ShadowMapHashInt( hash, entityDef->lastModifiedFrameNum );
		hash = R_ShadowMapHashInt( hash, entityDef->dynamicModelFrameCount );
		for ( int i = 0; i < 16; i++ ) {
			hash = R_ShadowMapHashFloat( hash, entityDef->modelMatrix[i] );
		}
	}
	vLight->shadowMapCasterSignature = hash;
}

static void R_RecordShadowMapRejectedCaster( viewLight_t *vLight, const shadowMapCasterRejectReason_t reason ) {
	if ( vLight != NULL ) {
		vLight->shadowMapRejectedCasterCount++;
		if ( reason >= 0 && reason < SHADOWMAP_CASTER_REJECT_COUNT ) {
			vLight->shadowMapRejectedCasterReasons[reason]++;
		} else {
			vLight->shadowMapRejectedCasterReasons[SHADOWMAP_CASTER_REJECT_UNKNOWN]++;
		}
	}
}

static shadowMapCasterRejectReason_t R_ClassifyShadowMapCasterReject( const idRenderEntityLocal *entityDef, const viewEntity_t *vEntity, const surfaceInteraction_t *sint, const idMaterial *shadowShader, const bool isViewOnlyEntity, const bool translucentShadowMapSupported, const bool skipPointLightEmitterCaster, const bool sameSpectrumShadowMapCaster, const bool shadowLODAdmitted ) {
	if ( entityDef == NULL || vEntity == NULL || sint == NULL || shadowShader == NULL ) {
		return SHADOWMAP_CASTER_REJECT_UNKNOWN;
	}
	if ( entityDef->parms.noShadow ) {
		return SHADOWMAP_CASTER_REJECT_NO_SHADOW;
	}
	if ( isViewOnlyEntity ) {
		return SHADOWMAP_CASTER_REJECT_VIEW_ONLY;
	}
	if ( vEntity->modelDepthHack != 0.0f ) {
		return SHADOWMAP_CASTER_REJECT_DEPTH_HACK;
	}
	if ( sint->ambientTris == NULL ) {
		return SHADOWMAP_CASTER_REJECT_NO_GEOMETRY;
	}
	if ( skipPointLightEmitterCaster ) {
		return SHADOWMAP_CASTER_REJECT_POINT_LIGHT_EMITTER;
	}
	const shadowMapCasterRejectReason_t materialReason = R_ShadowMapMaterialPolicyRejectReason(
		R_ShadowMapMaterialCasterPolicyForShader( shadowShader ),
		r_shadowMapTranslucentMoments.GetBool(),
		translucentShadowMapSupported );
	if ( materialReason != SHADOWMAP_CASTER_REJECT_UNKNOWN ) {
		return materialReason;
	}
	if ( !sameSpectrumShadowMapCaster ) {
		return SHADOWMAP_CASTER_REJECT_SPECTRUM_MISMATCH;
	}
	if ( !shadowLODAdmitted ) {
		return SHADOWMAP_CASTER_REJECT_LOD;
	}
	return SHADOWMAP_CASTER_REJECT_UNKNOWN;
}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
static ID_INLINE const rvSilTraceVertT *R_GetInteractionSilTraceVerts( const srfTriangles_t *tri ) {
	return ( tri != NULL && tri->silTraceVerts != NULL )
		? reinterpret_cast<const rvSilTraceVertT *>( tri->silTraceVerts )
		: NULL;
}

static bool R_MaterializeInteractionPackedTriangles( const srfTriangles_t *tri, idDrawVert *&tempVerts, glIndex_t *&tempIndexes ) {
	tempVerts = NULL;
	tempIndexes = NULL;

	if ( tri == NULL || tri->primBatchMesh == NULL || tri->numVerts <= 0 || tri->numIndexes <= 0 ) {
		return false;
	}

	tempVerts = (idDrawVert *)R_FrameAlloc( tri->numVerts * sizeof( tempVerts[0] ) );
	tempIndexes = (glIndex_t *)R_FrameAlloc( tri->numIndexes * sizeof( tempIndexes[0] ) );
	renderSystem->CopyPrimBatchTriangles( tempVerts, tempIndexes, tri->primBatchMesh, tri->silTraceVerts );
	return true;
}
#endif

static bool R_GetInteractionSurfacePoint( const srfTriangles_t *tri, const idDrawVert *fallbackVerts, int index, idVec3 &point ) {
	if ( tri == NULL || index < 0 || index >= tri->numVerts ) {
		return false;
	}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( const rvSilTraceVertT *silTraceVerts = R_GetInteractionSilTraceVerts( tri ) ) {
		point = silTraceVerts[index].xyzw.ToVec3();
		return true;
	}
#endif

	const idDrawVert *sourceVerts = ( fallbackVerts != NULL ) ? fallbackVerts : tri->verts;
	if ( sourceVerts == NULL ) {
		return false;
	}

	point = sourceVerts[index].xyz;
	return true;
}

static bool R_ShouldSkipPointLightEmitterCaster( const idMaterial *shadowShader, const srfTriangles_t *ambientTris, const idVec3 &localLightOrigin, const idVec3 &localLightRadius ) {
	if ( shadowShader == NULL || ambientTris == NULL ) {
		return false;
	}
	if ( shadowShader->GetName() == NULL || idStr::Icmpn( shadowShader->GetName(), "textures/common_lights/", 23 ) != 0 ) {
		return false;
	}
	if ( ambientTris->numIndexes < 3 ) {
		return false;
	}

	const glIndex_t *sourceIndexes = ambientTris->indexes;
	idDrawVert *tempVerts = NULL;
	glIndex_t *tempIndexes = NULL;

	if ( sourceIndexes == NULL ) {
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		if ( !R_MaterializeInteractionPackedTriangles( ambientTris, tempVerts, tempIndexes ) ) {
			return false;
		}
		sourceIndexes = tempIndexes;
#else
		return false;
#endif
	}

	idPlane plane;
	bool havePlane = false;
	for ( int i = 0; i + 2 < ambientTris->numIndexes; i += 3 ) {
		const int i0 = sourceIndexes[i + 0];
		const int i1 = sourceIndexes[i + 1];
		const int i2 = sourceIndexes[i + 2];

		idVec3 p0;
		idVec3 p1;
		idVec3 p2;
		if ( !R_GetInteractionSurfacePoint( ambientTris, tempVerts, i0, p0 )
			|| !R_GetInteractionSurfacePoint( ambientTris, tempVerts, i1, p1 )
			|| !R_GetInteractionSurfacePoint( ambientTris, tempVerts, i2, p2 ) ) {
			continue;
		}

		idVec3 normal = ( p1 - p0 ).Cross( p2 - p0 );
		if ( normal.LengthSqr() <= Square( 1.0e-6f ) ) {
			continue;
		}

		normal.Normalize();
		plane.SetNormal( normal );
		plane[3] = -( normal * p0 );
		havePlane = true;
		break;
	}

	if ( !havePlane ) {
		return false;
	}

	const float minLightRadius = Max( 1.0f, Min( localLightRadius.x, Min( localLightRadius.y, localLightRadius.z ) ) );
	const float emitterPlaneThreshold = idMath::ClampFloat( 24.0f, 192.0f, minLightRadius * 0.75f );
	const float emitterBoundsPad = idMath::ClampFloat( 16.0f, 64.0f, minLightRadius * 0.25f );
	if ( idMath::Fabs( plane.Distance( localLightOrigin ) ) > emitterPlaneThreshold ) {
		return false;
	}

	return ambientTris->bounds.Expand( emitterBoundsPad ).ContainsPoint( localLightOrigin );
}

// Lifetime-cached wrapper: the emitter-panel test walks the surface geometry
// building a plane and bounds, and its inputs are invariant for the surface
// interaction's lifetime, so evaluate it at most once per interaction instead
// of per visible frame. Only point (non-parallel) lights can skip.
static bool R_CachedShouldSkipPointLightEmitterCaster( surfaceInteraction_t *sint, const viewLight_t *vLight,
		const idMaterial *shadowShader, const idVec3 &localLightOrigin, const idVec3 &localLightRadius ) {
	if ( sint == NULL ) {
		return vLight->pointLight && !vLight->parallel &&
			R_ShouldSkipPointLightEmitterCaster( shadowShader, NULL, localLightOrigin, localLightRadius );
	}
	if ( !( vLight->pointLight && !vLight->parallel ) ) {
		return false;
	}
	if ( !sint->pointEmitterCasterVerdictValid ) {
		sint->pointEmitterCasterSkip = R_ShouldSkipPointLightEmitterCaster(
			shadowShader, sint->ambientTris, localLightOrigin, localLightRadius ) &&
			// The geometric heuristic also catches real blockers near a light
			// (for example Storage 2's lift panel). A nonempty surface volume
			// proves that this surface must participate in the depth map too.
			!( sint->shadowStencilEligible && !sint->shadowStencilUsesPrelight &&
				sint->shadowTris != NULL );
		sint->pointEmitterCasterVerdictValid = true;
	}
	return sint->pointEmitterCasterSkip;
}

static void R_BoundInteractionSurface( const srfTriangles_t *tri, const glIndex_t *indexes, int numIndexes, idBounds &bounds ) {
	if ( numIndexes <= 0 ) {
		bounds.Clear();
		return;
	}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( const rvSilTraceVertT *silTraceVerts = R_GetInteractionSilTraceVerts( tri ) ) {
		const idVec3 firstPoint = silTraceVerts[indexes[0]].xyzw.ToVec3();
		bounds[0] = firstPoint;
		bounds[1] = firstPoint;
		for ( int i = 1; i < numIndexes; ++i ) {
			bounds.AddPoint( silTraceVerts[indexes[i]].xyzw.ToVec3() );
		}
		return;
	}
#endif

	SIMDProcessor->MinMax( bounds[0], bounds[1], tri->verts, indexes, numIndexes );
}




#define	MAX_CLIPPED_POINTS	20
typedef struct {
	int		numVerts;
	idVec3	verts[MAX_CLIPPED_POINTS];
} clipTri_t;

/*
=============
R_ChopWinding

Clips a triangle from one buffer to another, setting edge flags
The returned buffer may be the same as inNum if no clipping is done
If entirely clipped away, clipTris[returned].numVerts == 0

I have some worries about edge flag cases when polygons are clipped
multiple times near the epsilon.
=============
*/
static int R_ChopWinding( clipTri_t clipTris[2], int inNum, const idPlane plane ) {
	clipTri_t	*in, *out;
	float	dists[MAX_CLIPPED_POINTS];
	int		sides[MAX_CLIPPED_POINTS];
	int		counts[3];
	float	dot;
	int		i, j;
	idVec3	mid;
	bool	front;

	in = &clipTris[inNum];
	out = &clipTris[inNum^1];
	counts[0] = counts[1] = counts[2] = 0;

	// determine sides for each point
	front = false;
	for ( i = 0; i < in->numVerts; i++ ) {
		dot = in->verts[i] * plane.Normal() + plane[3];
		dists[i] = dot;
		if ( dot < LIGHT_CLIP_EPSILON ) {	// slop onto the back
			sides[i] = SIDE_BACK;
		} else {
			sides[i] = SIDE_FRONT;
			if ( dot > LIGHT_CLIP_EPSILON ) {
				front = true;
			}
		}
		counts[sides[i]]++;
	}

	// if none in front, it is completely clipped away
	if ( !front ) {
		in->numVerts = 0;
		return inNum;
	}
	if ( !counts[SIDE_BACK] ) {
		return inNum;		// inout stays the same
	}

	// avoid wrapping checks by duplicating first value to end
	sides[i] = sides[0];
	dists[i] = dists[0];
	in->verts[in->numVerts] = in->verts[0];

	out->numVerts = 0;
	for ( i = 0 ; i < in->numVerts ; i++ ) {
		idVec3 &p1 = in->verts[i];
		
		if ( sides[i] == SIDE_FRONT ) {
			out->verts[out->numVerts] = p1;
			out->numVerts++;
		}

		if ( sides[i+1] == sides[i] ) {
			continue;
		}
			
		// generate a split point
		idVec3 &p2 = in->verts[i+1];
		
		dot = dists[i] / ( dists[i] - dists[i+1] );
		for ( j = 0; j < 3; j++ ) {
			mid[j] = p1[j] + dot * ( p2[j] - p1[j] );
		}
			
		out->verts[out->numVerts] = mid;

		out->numVerts++;
	}

	return inNum ^ 1;
}

/*
===================
R_ClipTriangleToLight

Returns false if nothing is left after clipping
===================
*/
static bool	R_ClipTriangleToLight( const idVec3 &a, const idVec3 &b, const idVec3 &c, int planeBits, const idPlane frustum[6] ) {
	int			i;
	clipTri_t	pingPong[2];
	int			p;

	pingPong[0].numVerts = 3;
	pingPong[0].verts[0] = a;
	pingPong[0].verts[1] = b;
	pingPong[0].verts[2] = c;

	p = 0;
	for ( i = 0 ; i < 6 ; i++ ) {
		if ( planeBits & ( 1 << i ) ) {
			p = R_ChopWinding( pingPong, p, frustum[i] );
			if ( pingPong[p].numVerts < 1 ) {
				return false;
			}
		}
	}

	return true;
}

/*
====================
R_CreateLightTris

The resulting surface will be a subset of the original triangles,
it will never clip triangles, but it may cull on a per-triangle basis.
====================
*/
static srfTriangles_t *R_CreateLightTris( const idRenderEntityLocal *ent, 
									 const srfTriangles_t *tri, const idRenderLightLocal *light,
									 const idMaterial *shader, srfCullInfo_t &cullInfo ) {
	int			i;
	int			numIndexes;
	glIndex_t	*indexes;
	srfTriangles_t	*newTri;
	int			c_backfaced;
	int			c_distance;
	idBounds	bounds;
	bool		includeBackFaces;
	int			faceNum;

	tr.pc.c_createLightTris++;
	c_backfaced = 0;
	c_distance = 0;

	numIndexes = 0;
	indexes = NULL;

	// it is debatable if non-shadowing lights should light back faces. we aren't at the moment
	if ( r_lightAllBackFaces.GetBool() || light->lightShader->LightEffectsBackSides()
			|| shader->ReceivesLightingOnBackSides()
				|| ent->parms.noSelfShadow || ent->parms.noShadow  ) {
		includeBackFaces = true;
	} else {
		includeBackFaces = false;
	}

	// allocate a new surface for the lit triangles
	newTri = R_AllocStaticTriSurf();

	// save a reference to the original surface
	newTri->ambientSurface = const_cast<srfTriangles_t *>(tri);

	// the light surface references the verts of the ambient surface
	newTri->numVerts = tri->numVerts;
	R_ReferenceStaticTriSurfVerts( newTri, tri );

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->primBatchMesh != NULL ) {
		// Keep packed interaction surfaces tied to the source MD5R mesh and
		// sil-trace/transform state so the ARB2 interaction path can follow
		// retail's per-prim-batch light-tri submission when available.
		newTri->primBatchMesh = tri->primBatchMesh;
		newTri->silTraceVerts = tri->silTraceVerts;
		newTri->silEdges = tri->silEdges;
		newTri->numSilEdges = tri->numSilEdges;
		newTri->skinToModelTransforms = tri->skinToModelTransforms;
		newTri->skinToModelTransformsAlloc = NULL;
		newTri->numSkinToModelTransforms = tri->numSkinToModelTransforms;
	}
#endif

	// calculate cull information
	if ( !includeBackFaces ) {
		R_CalcInteractionFacing( ent, tri, light, cullInfo );
	}
	R_CalcInteractionCullBits( ent, tri, light, cullInfo );

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->primBatchMesh != NULL ) {
		const byte *facing = includeBackFaces ? NULL : cullInfo.facing;
		const byte *cullBits = ( cullInfo.cullBits == LIGHT_CULL_ALL_FRONT ) ? NULL : cullInfo.cullBits;
		if ( R_MD5R_CreateLightTris( *tri, newTri, c_backfaced, c_distance, facing, cullBits, includeBackFaces ) ) {
			if ( newTri->numIndexes == 0 ) {
				R_ReallyFreeStaticTriSurf( newTri );
				return NULL;
			}
			return newTri;
		}

		// If the retail packed builder rejects a surface, fall back to the
		// long-standing flat interaction build instead of dropping the light.
		if ( newTri->indexes != NULL ) {
			R_ResizeStaticTriSurfIndexes( newTri, 0 );
			newTri->numIndexes = 0;
		}
	}
#endif

	// if the surface is completely inside the light frustum
	if ( cullInfo.cullBits == LIGHT_CULL_ALL_FRONT ) {

		// if we aren't self shadowing, let back facing triangles get
		// through so the smooth shaded bump maps light all the way around
		if ( includeBackFaces ) {

			// the whole surface is lit so the light surface just references the indexes of the ambient surface
			R_ReferenceStaticTriSurfIndexes( newTri, tri );
			numIndexes = tri->numIndexes;
			bounds = tri->bounds;

		} else {

			// the light tris indexes are going to be a subset of the original indexes so we generally
			// allocate too much memory here but we decrease the memory block when the number of indexes is known
			R_AllocStaticTriSurfIndexes( newTri, tri->numIndexes );

			// back face cull the individual triangles
			indexes = newTri->indexes;
			const byte *facing = cullInfo.facing;
			for ( faceNum = i = 0; i < tri->numIndexes; i += 3, faceNum++ ) {
				if ( !facing[ faceNum ] ) {
					c_backfaced++;
					continue;
				}
				indexes[numIndexes+0] = tri->indexes[i+0];
				indexes[numIndexes+1] = tri->indexes[i+1];
				indexes[numIndexes+2] = tri->indexes[i+2];
				numIndexes += 3;
			}

			// get bounds for the surface
			R_BoundInteractionSurface( tri, indexes, numIndexes, bounds );

			// decrease the size of the memory block to the size of the number of used indexes
			R_ResizeStaticTriSurfIndexes( newTri, numIndexes );
		}

	} else {

		// the light tris indexes are going to be a subset of the original indexes so we generally
		// allocate too much memory here but we decrease the memory block when the number of indexes is known
		R_AllocStaticTriSurfIndexes( newTri, tri->numIndexes );

		// cull individual triangles
		indexes = newTri->indexes;
		const byte *facing = cullInfo.facing;
		const byte *cullBits = cullInfo.cullBits;
		for ( faceNum = i = 0; i < tri->numIndexes; i += 3, faceNum++ ) {
			int i1, i2, i3;

			// if we aren't self shadowing, let back facing triangles get
			// through so the smooth shaded bump maps light all the way around
			if ( !includeBackFaces ) {
				// back face cull
				if ( !facing[ faceNum ] ) {
					c_backfaced++;
					continue;
				}
			}

			i1 = tri->indexes[i+0];
			i2 = tri->indexes[i+1];
			i3 = tri->indexes[i+2];

			// fast cull outside the frustum
			// if all three points are off one plane side, it definately isn't visible
			if ( cullBits[i1] & cullBits[i2] & cullBits[i3] ) {
				c_distance++;
				continue;
			}

			if ( r_usePreciseTriangleInteractions.GetBool() ) {
				// do a precise clipped cull if none of the points is completely inside the frustum
				// note that we do not actually use the clipped triangle, which would have Z fighting issues.
				if ( cullBits[i1] && cullBits[i2] && cullBits[i3] ) {
					int cull = cullBits[i1] | cullBits[i2] | cullBits[i3];
					if ( !R_ClipTriangleToLight( tri->verts[i1].xyz, tri->verts[i2].xyz, tri->verts[i3].xyz, cull, cullInfo.localClipPlanes ) ) {
						continue;
					}
				}
			}

			// add to the list
			indexes[numIndexes+0] = i1;
			indexes[numIndexes+1] = i2;
			indexes[numIndexes+2] = i3;
			numIndexes += 3;
		}

		// get bounds for the surface
		R_BoundInteractionSurface( tri, indexes, numIndexes, bounds );

		// decrease the size of the memory block to the size of the number of used indexes
		R_ResizeStaticTriSurfIndexes( newTri, numIndexes );
	}

	if ( !numIndexes ) {
		R_ReallyFreeStaticTriSurf( newTri );
		return NULL;
	}

	newTri->numIndexes = numIndexes;

	newTri->bounds = bounds;

	return newTri;
}

/*
===================
R_EnsureInteractionShadowCache

Classic shadow interactions need either a private projected-vertex cache or a
reference to the ambient surface's shared vertex-program shadow cache.
Packed MD5R prim-batch shadows manage their own geometry stream, so they don't
go through the classic cache upload path here.
===================
*/
static bool R_EnsureInteractionShadowCache( surfaceInteraction_t *sint, const idRenderEntityLocal *entityDef ) {
	srfTriangles_t *shadowTris = sint->shadowTris;

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( shadowTris == NULL || shadowTris->primBatchMesh != NULL ) {
		return shadowTris != NULL;
	}
#else
	if ( shadowTris == NULL ) {
		return false;
	}
#endif

	// Vertex-program turbo shadows borrow the ambient surface's paired
	// front/back cache. Classic shadow volumes with explicit projected verts own
	// a private cache instead.
	if ( !shadowTris->shadowVertexes ) {
		shadowTris->shadowCache = sint->ambientTris->shadowCache;
	}

	if ( !shadowTris->shadowCache ) {
		if ( shadowTris->shadowVertexes ) {
			R_CreatePrivateShadowCache( shadowTris );
		} else {
			R_CreateVertexProgramShadowCache( sint->ambientTris );
			shadowTris->shadowCache = sint->ambientTris->shadowCache;
		}

		if ( !shadowTris->shadowCache ) {
			return false;
		}
	}

	R_TouchVertexCache( shadowTris->shadowCache );

	if ( !shadowTris->indexCache && shadowTris->numIndexes > 0 && R_StaticIndexCacheAllowed( entityDef ) ) {
		vertexCache.Alloc(
			shadowTris->indexes,
			shadowTris->numIndexes * sizeof( shadowTris->indexes[0] ),
			&shadowTris->indexCache,
			true );
	}
	if ( shadowTris->indexCache ) {
		R_TouchVertexCache( shadowTris->indexCache );
	}

	return true;
}

/*
===============
idInteraction::idInteraction
===============
*/
idInteraction::idInteraction( void ) {
	numSurfaces				= 0;
	surfaces				= NULL;
	entityDef				= NULL;
	lightDef				= NULL;
	lightNext				= NULL;
	lightPrev				= NULL;
	entityNext				= NULL;
	entityPrev				= NULL;
	dynamicModelFrameCount	= 0;
	frustumState			= FRUSTUM_UNINITIALIZED;
	frustumAreas			= NULL;
}

/*
===============
idInteraction::AllocAndLink
===============
*/
idInteraction *idInteraction::AllocAndLink( idRenderEntityLocal *edef, idRenderLightLocal *ldef ) {
	if ( !edef || !ldef ) {
		common->Error( "idInteraction::AllocAndLink: NULL parm" );
	}

	idRenderWorldLocal *renderWorld = edef->world;

	idInteraction *interaction = renderWorld->interactionAllocator.Alloc();

	// link and initialize
	interaction->dynamicModelFrameCount = 0;

	interaction->lightDef = ldef;
	interaction->entityDef = edef;

	interaction->numSurfaces = -1;		// not checked yet
	interaction->surfaces = NULL;

	interaction->frustumState = idInteraction::FRUSTUM_UNINITIALIZED;
	interaction->frustumAreas = NULL;

	// link at the start of the entity's list
	interaction->lightNext = ldef->firstInteraction;
	interaction->lightPrev = NULL;
	ldef->firstInteraction = interaction;
	if ( interaction->lightNext != NULL ) {
		interaction->lightNext->lightPrev = interaction;
	} else {
		ldef->lastInteraction = interaction;
	}

	// link at the start of the light's list
	interaction->entityNext = edef->firstInteraction;
	interaction->entityPrev = NULL;
	edef->firstInteraction = interaction;
	if ( interaction->entityNext != NULL ) {
		interaction->entityNext->entityPrev = interaction;
	} else {
		edef->lastInteraction = interaction;
	}

	// update the interaction table
	if ( renderWorld->interactionTable ) {
		int index = ldef->index * renderWorld->interactionTableWidth + edef->index;
		if ( renderWorld->interactionTable[index] != NULL ) {
			common->Error( "idInteraction::AllocAndLink: non NULL table entry" );
		}
		renderWorld->interactionTable[ index ] = interaction;
	}

	return interaction;
}

/*
===============
idInteraction::FreeSurfaces

Frees the surfaces, but leaves the interaction linked in, so it
will be regenerated automatically
===============
*/
void idInteraction::FreeSurfaces( void ) {
	if ( this->surfaces ) {
		for ( int i = 0 ; i < this->numSurfaces ; i++ ) {
			surfaceInteraction_t *sint = &this->surfaces[i];

			if ( sint->lightTris ) {
				if ( sint->lightTris != LIGHT_TRIS_DEFERRED ) {
					R_FreeStaticTriSurf( sint->lightTris );
				}
				sint->lightTris = NULL;
			}
			if ( sint->shadowTris ) {
				// if it doesn't have an entityDef, it is part of a prelight
				// model, not a generated interaction
				if ( this->entityDef ) {
					R_FreeStaticTriSurf( sint->shadowTris );
					sint->shadowTris = NULL;
				}
			}
			R_FreeInteractionCullInfo( sint->cullInfo );
		}

		R_StaticFree( this->surfaces );
		this->surfaces = NULL;
	}
	this->numSurfaces = -1;
}

/*
===============
idInteraction::Unlink
===============
*/
void idInteraction::Unlink( void ) {

	// unlink from the entity's list
	if ( this->entityPrev ) {
		this->entityPrev->entityNext = this->entityNext;
	} else {
		this->entityDef->firstInteraction = this->entityNext;
	}
	if ( this->entityNext ) {
		this->entityNext->entityPrev = this->entityPrev;
	} else {
		this->entityDef->lastInteraction = this->entityPrev;
	}
	this->entityNext = this->entityPrev = NULL;

	// unlink from the light's list
	if ( this->lightPrev ) {
		this->lightPrev->lightNext = this->lightNext;
	} else {
		this->lightDef->firstInteraction = this->lightNext;
	}
	if ( this->lightNext ) {
		this->lightNext->lightPrev = this->lightPrev;
	} else {
		this->lightDef->lastInteraction = this->lightPrev;
	}
	this->lightNext = this->lightPrev = NULL;
}

/*
===============
idInteraction::UnlinkAndFree

Removes links and puts it back on the free list.
===============
*/
void idInteraction::UnlinkAndFree( void ) {

	// clear the table pointer
	idRenderWorldLocal *renderWorld = this->lightDef->world;
	if ( renderWorld->interactionTable ) {
		int index = this->lightDef->index * renderWorld->interactionTableWidth + this->entityDef->index;
		if ( renderWorld->interactionTable[index] != this ) {
			common->Error( "idInteraction::UnlinkAndFree: interactionTable wasn't set" );
		}
		renderWorld->interactionTable[index] = NULL;
	}

	Unlink();

	FreeSurfaces();

	// free the interaction area references
	areaNumRef_t *area, *nextArea;
	for ( area = frustumAreas; area; area = nextArea ) {
		nextArea = area->next;
		renderWorld->areaNumRefAllocator.Free( area );
	}

	// put it back on the free list
	renderWorld->interactionAllocator.Free( this );
}

/*
===============
idInteraction::MakeEmpty

Makes the interaction empty and links it at the end of the entity's and light's interaction lists.
===============
*/
void idInteraction::MakeEmpty( void ) {

	// an empty interaction has no surfaces
	numSurfaces = 0;

	Unlink();

	// relink at the end of the entity's list
	this->entityNext = NULL;
	this->entityPrev = this->entityDef->lastInteraction;
	this->entityDef->lastInteraction = this;
	if ( this->entityPrev ) {
		this->entityPrev->entityNext = this;
	} else {
		this->entityDef->firstInteraction = this;
	}

	// relink at the end of the light's list
	this->lightNext = NULL;
	this->lightPrev = this->lightDef->lastInteraction;
	this->lightDef->lastInteraction = this;
	if ( this->lightPrev ) {
		this->lightPrev->lightNext = this;
	} else {
		this->lightDef->firstInteraction = this;
	}
}

/*
===============
idInteraction::HasShadows
===============
*/
ID_INLINE bool idInteraction::HasShadows( void ) const {
	return ( !lightDef->parms.noShadows
		&& !lightDef->parms.noDynamicShadows
		&& !entityDef->parms.noShadow
		&& lightDef->lightShader->LightCastsShadows() );
}

/*
===============
idInteraction::MemoryUsed

Counts up the memory used by all the surfaceInteractions, which
will be used to determine when we need to start purging old interactions.
===============
*/
int idInteraction::MemoryUsed( void ) {
	int		total = 0;

	for ( int i = 0 ; i < numSurfaces ; i++ ) {
		surfaceInteraction_t *inter = &surfaces[i];

		total += R_TriSurfMemory( inter->lightTris );
		total += R_TriSurfMemory( inter->shadowTris );
	}

	return total;
}

/*
==================
idInteraction::CalcInteractionScissorRectangle
==================
*/
idScreenRect idInteraction::CalcInteractionScissorRectangle( const idFrustum &viewFrustum ) {
	idBounds		projectionBounds;
	idScreenRect	portalRect;
	idScreenRect	scissorRect;

	if ( r_useInteractionScissors.GetInteger() == 0 ) {
		return lightDef->viewLight->scissorRect;
	}

	if ( r_useInteractionScissors.GetInteger() < 0 ) {
		// this is the code from Cass at nvidia, it is more precise, but slower
		return R_CalcIntersectionScissor( lightDef, entityDef, tr.viewDef );
	}

	// the following is Mr.E's code

	// frustum must be initialized and valid
	if ( frustumState == idInteraction::FRUSTUM_UNINITIALIZED || frustumState == idInteraction::FRUSTUM_INVALID ) {
		return lightDef->viewLight->scissorRect;
	}

	// calculate scissors for the portals through which the interaction is visible
	if ( r_useInteractionScissors.GetInteger() > 1 ) {
		areaNumRef_t *area;

		if ( frustumState == idInteraction::FRUSTUM_VALID ) {
			// retrieve all the areas the interaction frustum touches
			for ( areaReference_t *ref = entityDef->entityRefs; ref; ref = ref->ownerNext ) {
				area = entityDef->world->areaNumRefAllocator.Alloc();
				area->areaNum = ref->area->areaNum;
				area->next = frustumAreas;
				frustumAreas = area;
			}
			frustumAreas = tr.viewDef->renderWorld->FloodFrustumAreas( frustum, frustumAreas );
			frustumState = idInteraction::FRUSTUM_VALIDAREAS;
		}

		portalRect.Clear();
		for ( area = frustumAreas; area; area = area->next ) {
			portalRect.Union( entityDef->world->GetAreaScreenRect( area->areaNum ) );
		}
		portalRect.Intersect( lightDef->viewLight->scissorRect );
	} else {
		portalRect = lightDef->viewLight->scissorRect;
	}

	// early out if the interaction is not visible through any portals
	if ( portalRect.IsEmpty() ) {
		return portalRect;
	}

	// calculate bounds of the interaction frustum projected into the view frustum
	if ( lightDef->parms.pointLight ) {
		viewFrustum.ClippedProjectionBounds( frustum, idBox( lightDef->parms.origin, lightDef->parms.lightRadius, lightDef->parms.axis ), projectionBounds );
	} else {
		viewFrustum.ClippedProjectionBounds( frustum, idBox( lightDef->frustumTris->bounds ), projectionBounds );
	}

	if ( projectionBounds.IsCleared() ) {
		return portalRect;
	}

	// derive a scissor rectangle from the projection bounds
	scissorRect = R_ScreenRectFromViewFrustumBounds( projectionBounds );

	// intersect with the portal crossing scissor rectangle
	scissorRect.Intersect( portalRect );

	if ( r_showInteractionScissors.GetInteger() > 0 ) {
		R_ShowColoredScreenRect( scissorRect, lightDef->index );
	}

	return scissorRect;
}

/*
===================
idInteraction::CullInteractionByViewFrustum
===================
*/
bool idInteraction::CullInteractionByViewFrustum( const idFrustum &viewFrustum ) {

	if ( !r_useInteractionCulling.GetBool() ) {
		return false;
	}

	if ( frustumState == idInteraction::FRUSTUM_INVALID ) {
		return false;
	}

	if ( frustumState == idInteraction::FRUSTUM_UNINITIALIZED ) {

		frustum.FromProjection( idBox( entityDef->referenceBounds, entityDef->parms.origin, entityDef->parms.axis ), lightDef->globalLightOrigin, MAX_WORLD_SIZE );

		if ( !frustum.IsValid() ) {
			frustumState = idInteraction::FRUSTUM_INVALID;
			return false;
		}

		if ( lightDef->parms.pointLight ) {
			frustum.ConstrainToBox( idBox( lightDef->parms.origin, lightDef->parms.lightRadius, lightDef->parms.axis ) );
		} else {
			frustum.ConstrainToBox( idBox( lightDef->frustumTris->bounds ) );
		}

		frustumState = idInteraction::FRUSTUM_VALID;
	}

	if ( !viewFrustum.IntersectsFrustum( frustum ) ) {
		return true;
	}

	if ( r_showInteractionFrustums.GetInteger() ) {
		static idVec4 colors[] = { colorRed, colorGreen, colorBlue, colorYellow, colorMagenta, colorCyan, colorWhite, colorPurple };
		tr.viewDef->renderWorld->DebugFrustum( colors[lightDef->index & 7], frustum, ( r_showInteractionFrustums.GetInteger() > 1 ) );
		if ( r_showInteractionFrustums.GetInteger() > 2 ) {
			tr.viewDef->renderWorld->DebugBox( colorWhite, idBox( entityDef->referenceBounds, entityDef->parms.origin, entityDef->parms.axis ) );
		}
	}

	return false;
}

/*
====================
idInteraction::CreateInteraction

Called when a entityDef and a lightDef are both present in a
portalArea, and might be visible.  Performs cull checking before doing the expensive
computations.

References tr.viewCount so lighting surfaces will only be created if the ambient surface is visible,
otherwise it will be marked as deferred.

The results of this are cached and valid until the light or entity change.
====================
*/
void idInteraction::CreateInteraction( const idRenderModel *model ) {
	const idMaterial *	lightShader = lightDef->lightShader;
	const idMaterial*	shader;
	bool				interactionGenerated;
	idBounds			bounds;

	tr.pc.c_createInteractions++;

	bounds = model->Bounds( &entityDef->parms );

	// if it doesn't contact the light frustum, none of the surfaces will
	if ( R_CullLocalBox( bounds, entityDef->modelMatrix, 6, lightDef->frustum ) ) {
		MakeEmpty();
		return;
	}

	// use the turbo shadow path
	shadowGen_t shadowGen = SG_DYNAMIC;

	// really large models, like outside terrain meshes, should use
	// the more exactly culled static shadow path instead of the turbo shadow path.
	// FIXME: this is a HACK, we should probably have a material flag.
	if ( bounds[1][0] - bounds[0][0] > 3000 ) {
		shadowGen = SG_STATIC;
	}

	//
	// create slots for each of the model's surfaces
	//
	numSurfaces = model->NumSurfaces();
	surfaces = (surfaceInteraction_t *)R_ClearedStaticAlloc( sizeof( *surfaces ) * numSurfaces );

	interactionGenerated = false;
	const bool interactionHasShadows = HasShadows();
	const bool shadowMapsEnabledForInteraction = interactionHasShadows && r_shadows.GetBool() && r_useShadowMap.GetBool();
	const bool translucentShadowMapsEnabledForInteraction =
		shadowMapsEnabledForInteraction &&
		R_TranslucentShadowMapMomentsSupportedForLight( lightDef );
	idVec3 shadowMapLocalLightOrigin;
	R_GlobalPointToLocal( entityDef->modelMatrix,
		lightDef->globalLightOrigin, shadowMapLocalLightOrigin );

	// check each surface in the model
	for ( int c = 0 ; c < numSurfaces ; c++ ) {
		const modelSurface_t	*surf;
		srfTriangles_t	*tri;
	
		surf = model->Surface( c );

		tri = surf->geometry;
		if ( !tri ) {
			continue;
		}

		// determine the shader for this surface, possibly by skinning
		shader = surf->shader;
		shader = R_RemapShaderBySkin( shader, entityDef->parms.customSkin, entityDef->parms.customShader );

		if ( !shader ) {
			continue;
		}

		// try to cull each surface
		if ( R_CullLocalBox( tri->bounds, entityDef->modelMatrix, 6, lightDef->frustum ) ) {
			continue;
		}

		surfaceInteraction_t *sint = &surfaces[c];

		sint->shader = shader;

		// save the ambient tri pointer so we can reject lightTri interactions
		// when the ambient surface isn't in view, and we can get shared vertex
		// and shadow data from the source surface
		sint->ambientTris = tri;

		// "invisible ink" lights and shaders
		if ( shader->Spectrum() != lightShader->Spectrum() ) {
			continue;
		}

		// Fog and blend lights follow the separate `noFog` interaction path.
		if ( R_SurfaceShaderCreatesLightTris( shader, lightShader ) ) {
			if ( tri->ambientViewCount == tr.viewCount ) {
				sint->lightTris = R_CreateLightTris( entityDef, tri, lightDef, shader, sint->cullInfo );
			} else {
				// this will be calculated when sint->ambientTris is actually in view
				sint->lightTris = LIGHT_TRIS_DEFERRED;
			}
			interactionGenerated = true;
		}

		const bool allowTranslucentStencilShadowCaster =
			R_TranslucentStencilCasterEligible( shader, r_stencilTranslucentShadows.GetBool() );
		const bool dedicatedCollisionSurface = shader->IsDedicatedCollisionSurface();
		const bool surfaceCanCastInteractionShadow =
			interactionHasShadows &&
			!dedicatedCollisionSurface &&
			( shader->SurfaceCastsShadow() || allowTranslucentStencilShadowCaster );
		const bool surfaceCanCastDedicatedShadowMap =
			shadowMapsEnabledForInteraction &&
			( R_ShadowMapShaderCanCastOpaque( shader ) ||
				( !translucentShadowMapsEnabledForInteraction && R_ShadowMapShaderCanCastStencilParityTranslucent( shader ) ) );
		const bool surfaceCanCastTranslucentShadowMap =
			translucentShadowMapsEnabledForInteraction &&
			R_ShadowMapShaderCanCastTranslucent( shader );
		const bool surfaceCanCastStencilShadowVolume =
			surfaceCanCastInteractionShadow &&
			tri->silEdges != NULL;
		const bool surfaceNeedsShadowLODDecision =
			surfaceCanCastStencilShadowVolume ||
			surfaceCanCastDedicatedShadowMap ||
			surfaceCanCastTranslucentShadowMap;
		const bool shadowLODAdmitted =
			surfaceNeedsShadowLODDecision &&
			R_CachedInteractionShadowLODAdmitted( sint, entityDef );
		sint->shadowStencilEligible =
			surfaceCanCastStencilShadowVolume && shadowLODAdmitted;
		sint->shadowStencilUsesPrelight =
			sint->shadowStencilEligible &&
			R_LightHasRealPrelightModel( lightDef->parms ) &&
			model->IsStaticWorldModel() &&
			r_useOptimizedShadows.GetBool() &&
			!R_ShadowMapsNeedPerSurfaceStencilVolumes( lightDef );

		// A thin panel surrounding its owning point-light origin is excluded
		// from the point depth map. Probe its retail stencil path even when
		// dynamic map use would normally elide volume generation; AddActiveInteraction
		// makes the light sticky only if the probe produces a real volume.
		const bool pointMapPolicyActive =
			shadowMapsEnabledForInteraction &&
			lightDef->parms.pointLight &&
			!lightDef->parms.parallel &&
			r_shadowMapPointLights.GetBool();
		const bool forcePointEmitterStencilGeneration =
			pointMapPolicyActive &&
			sint->shadowStencilEligible &&
			R_ShouldSkipPointLightEmitterCaster( shader, tri,
				shadowMapLocalLightOrigin, lightDef->parms.lightRadius );

		if ( surfaceCanCastDedicatedShadowMap || surfaceCanCastTranslucentShadowMap ) {
			R_RecordShadowMapLODDecision(
				lightDef != NULL ? lightDef->viewLight : NULL,
				shader->Coverage(),
				surfaceCanCastTranslucentShadowMap,
				shadowLODAdmitted );
		}

		if ( ( surfaceCanCastDedicatedShadowMap || surfaceCanCastTranslucentShadowMap ) && shadowLODAdmitted ) {
			// Dedicated shadow maps can consume ambient triangles even when the
			// retail stencil-volume path has no silhouette volume to submit.
			// Translucent moment casters are openQ4-specific, but they are still
			// shadow output and need the same entity-level retail LOD admission.
			interactionGenerated = true;
		}

		// Dynamic-model volumes are regenerated every frame, so they can be
		// elided while the light renders shadow maps; the next regeneration
		// restores them if the backend flags a fallback. Static volumes are
		// built once and kept so the per-light stencil fallback stays instant.
		const bool suppressDynamicShadowVolume =
			surfaceCanCastStencilShadowVolume && shadowLODAdmitted &&
			( surfaceCanCastDedicatedShadowMap ||
				surfaceCanCastTranslucentShadowMap ) &&
			model->IsDynamicModel() != DM_STATIC &&
			!forcePointEmitterStencilGeneration &&
			R_ShadowMapLightWillUseShadowMaps( lightDef );
		if ( suppressDynamicShadowVolume ) {
			// shadow ownership stays with this interaction even without volumes
			interactionGenerated = true;
		} else if ( sint->shadowStencilEligible ) {

			// if the light has an optimized shadow volume, don't create shadows for any models that are part of the base areas
			if ( !sint->shadowStencilUsesPrelight ) {

				// this is the only place during gameplay (outside the utilities) that R_CreateShadowVolume() is called
				sint->shadowTris = R_CreateShadowVolume( entityDef, tri, lightDef, shadowGen, sint->cullInfo );
				if ( sint->shadowTris ) {
					if ( shader->Coverage() != MC_OPAQUE || ( !r_skipSuppress.GetBool() && entityDef->parms.suppressSurfaceInViewID ) ) {
						// if any surface is a shadow-casting perforated or translucent surface, or the
						// base surface is suppressed in the view (world weapon shadows) we can't use
						// the external shadow optimizations because we can see through some of the faces.
						sint->shadowTris->numShadowIndexesNoCaps = sint->shadowTris->numIndexes;
						sint->shadowTris->numShadowIndexesNoFrontCaps = sint->shadowTris->numIndexes;
					}
					interactionGenerated = true;
				}
			}
		}

		// free the cull information when it's no longer needed
		if ( sint->lightTris != LIGHT_TRIS_DEFERRED ) {
			R_FreeInteractionCullInfo( sint->cullInfo );
		}
	}

	// if none of the surfaces generated anything, don't even bother checking?
	if ( !interactionGenerated ) {
		MakeEmpty();
	}
}

/*
======================
R_PotentiallyInsideInfiniteShadow

If we know that we are "off to the side" of an infinite shadow volume,
we can draw it without caps in zpass mode
======================
*/
static bool R_PotentiallyInsideInfiniteShadow( const srfTriangles_t *occluder,
											  const idVec3 &localView, const idVec3 &localLight ) {
	idBounds	exp;

	// expand the bounds to account for the near clip plane, because the
	// view could be mathematically outside, but if the near clip plane
	// chops a volume edge, the zpass rendering would fail.
	float	znear = r_znear.GetFloat();
	if ( tr.viewDef->renderView.cramZNear ) {
		znear *= 0.25f;
	}
	float	stretch = znear * 2;	// in theory, should vary with FOV
	exp[0][0] = occluder->bounds[0][0] - stretch;
	exp[0][1] = occluder->bounds[0][1] - stretch;
	exp[0][2] = occluder->bounds[0][2] - stretch;
	exp[1][0] = occluder->bounds[1][0] + stretch;
	exp[1][1] = occluder->bounds[1][1] + stretch;
	exp[1][2] = occluder->bounds[1][2] + stretch;

	if ( exp.ContainsPoint( localView ) ) {
		return true;
	}
	if ( exp.ContainsPoint( localLight ) ) {
		return true;
	}

	// if the ray from localLight to localView intersects a face of the
	// expanded bounds, we will be inside the projection

	idVec3	ray = localView - localLight;

	// intersect the ray from the view to the light with the near side of the bounds
	for ( int axis = 0; axis < 3; axis++ ) {
		float	d, frac;
		idVec3	hit;

		if ( localLight[axis] < exp[0][axis] ) {
			if ( localView[axis] < exp[0][axis] ) {
				continue;
			}
			d = exp[0][axis] - localLight[axis];
			frac = d / ray[axis];
			hit = localLight + frac * ray;
			hit[axis] = exp[0][axis];
		} else if ( localLight[axis] > exp[1][axis] ) {
			if ( localView[axis] > exp[1][axis] ) {
				continue;
			}
			d = exp[1][axis] - localLight[axis];
			frac = d / ray[axis];
			hit = localLight + frac * ray;
			hit[axis] = exp[1][axis];
		} else {
			continue;
		}

		if ( exp.ContainsPoint( hit ) ) {
			return true;
		}
	}

	// the view is definitely not inside the projected shadow
	return false;
}

/*
==================
idInteraction::AddActiveInteraction

Create and add any necessary light and shadow triangles

If the model doesn't have any surfaces that need interactions
with this type of light, it can be skipped, but we might need to
instantiate the dynamic model to find out
==================
*/
void idInteraction::AddActiveInteraction( void ) {
	viewLight_t *	vLight;
	viewEntity_t *	vEntity;
	idScreenRect	shadowScissor;
	idScreenRect	lightScissor;
	idVec3			localLightOrigin;
	idVec3			localViewOrigin;
	const bool		interactionHasShadows = HasShadows();
	bool			shadowMapViewFrustumCulled = false;

	vLight = lightDef->viewLight;
	vEntity = entityDef->viewEntity;
	const bool		shadowMapConservativeCandidate =
		interactionHasShadows &&
		R_ShadowMapConservativeCastersEnabled() &&
		// parallel (sun) lights render through the projected path and depend on
		// off-screen casters more than any other light class
		( vLight == NULL || !vLight->pointLight || vLight->parallel || r_shadowMapPointLights.GetBool() );

	if ( shadowMapConservativeCandidate && !R_ShadowMapEntityTouchesConnectedArea( entityDef ) ) {
		R_RecordShadowMapRejectedCaster( vLight, SHADOWMAP_CASTER_REJECT_AREA_DISCONNECTED );
		return;
	}

	// do not waste time culling the interaction frustum if there will be no shadows
	if ( !interactionHasShadows ) {

		// use the entity scissor rectangle
		shadowScissor = vEntity->scissorRect;

	// culling does not seem to be worth it for static world models
	} else if ( entityDef->parms.hModel->IsStaticWorldModel() ) {

		// use the light scissor rectangle
		shadowScissor = vLight->scissorRect;

	} else {

		// try to cull the interaction
		// this will also cull the case where the light origin is inside the
		// view frustum and the entity bounds are outside the view frustum
		if ( CullInteractionByViewFrustum( tr.viewDef->viewFrustum ) ) {
			if ( !shadowMapConservativeCandidate ) {
				return;
			}
			shadowMapViewFrustumCulled = true;
			shadowScissor.Clear();
		} else {
			// calculate the shadow scissor rectangle
			shadowScissor = CalcInteractionScissorRectangle( tr.viewDef->viewFrustum );
		}
	}

	// Shadow maps need caster lists that are not purely receiver-visible.  An
	// off-screen object can still shadow a visible receiver for the same light,
	// so keep building a caster-only interaction when the regular shadow scissor
	// rejected the visible shadow volume.
	const bool shadowScissorWasEmpty = shadowScissor.IsEmpty();
	const bool shadowMapCasterOnly = ( shadowScissorWasEmpty || shadowMapViewFrustumCulled ) && shadowMapConservativeCandidate;
	if ( shadowScissorWasEmpty ) {
		if ( !shadowMapCasterOnly ) {
			return;
		}
		shadowScissor = vLight->scissorRect;
		if ( shadowScissor.IsEmpty() ) {
			shadowScissor = vEntity->scissorRect;
		}
	}

	// We will need the dynamic surface created to make interactions, even if the
	// model itself wasn't visible.  This just returns a cached value after it
	// has been generated once in the view.
	idRenderModel *model = R_EntityDefDynamicModel( entityDef );
	if ( model == NULL || model->NumSurfaces() <= 0 ) {
		return;
	}

	// the dynamic model may have changed since we built the surface list
	if ( !IsDeferred() && entityDef->dynamicModelFrameCount != dynamicModelFrameCount ) {
		FreeSurfaces();
	}
	dynamicModelFrameCount = entityDef->dynamicModelFrameCount;

	// actually create the interaction if needed, building light and shadow surfaces as needed
	if ( IsDeferred() ) {
		CreateInteraction( model );
	}

	R_GlobalPointToLocal( vEntity->modelMatrix, lightDef->globalLightOrigin, localLightOrigin );
	R_GlobalPointToLocal( vEntity->modelMatrix, tr.viewDef->renderView.vieworg, localViewOrigin );

	// calculate the scissor as the intersection of the light and model rects
	// this is used for light triangles, but not for shadow triangles
	lightScissor = vLight->scissorRect;
	lightScissor.Intersect( vEntity->scissorRect );

	bool lightScissorsEmpty = lightScissor.IsEmpty();

	// Shadow-map admission policy that is invariant across this interaction's
	// surfaces, hoisted out of the per-surface loop (mirrors CreateInteraction).
	// translucentShadowMapSupported is only consumed when the caster policy is
	// active and the interaction has shadows, so the extra guards keep the
	// default stencil-only configuration from paying the by-name cvar lookup.
	// Disabling shadows intentionally leaves the map chains empty. Do not treat
	// that as failed admission and poison the light's sticky fallback state.
	const bool shadowMapCasterPolicyActive =
		r_shadows.GetBool() && r_useShadowMap.GetBool() &&
		( !vLight->pointLight || vLight->parallel || r_shadowMapPointLights.GetBool() );
	const bool isViewOnlyEntity =
		( entityDef->parms.allowSurfaceInViewID != 0 &&
			entityDef->parms.allowSurfaceInViewID == tr.viewDef->renderView.viewID ) ||
		( entityDef->parms.weaponDepthHackInViewID != 0 &&
			entityDef->parms.weaponDepthHackInViewID == tr.viewDef->renderView.viewID );
	const bool shadowMapsEnabled = r_shadows.GetBool() && r_useShadowMap.GetBool();
	const bool translucentShadowMapSupported =
		interactionHasShadows &&
		shadowMapCasterPolicyActive &&
		shadowMapsEnabled &&
		R_TranslucentShadowMapMomentsSupportedForLight( lightDef );

	// for each surface of this entity / light interaction
	for ( int i = 0; i < numSurfaces; i++ ) {
		surfaceInteraction_t *sint = &surfaces[i];

		// see if the base surface is visible, we may still need to add shadows even if empty
		if ( !lightScissorsEmpty && sint->ambientTris && sint->ambientTris->ambientViewCount == tr.viewCount ) {

			// make sure we have created this interaction, which may have been deferred
			// on a previous use that only needed the shadow
			if ( sint->lightTris == LIGHT_TRIS_DEFERRED ) {
				sint->lightTris = R_CreateLightTris( vEntity->entityDef, sint->ambientTris, vLight->lightDef, sint->shader, sint->cullInfo );
				R_FreeInteractionCullInfo( sint->cullInfo );
			}

			srfTriangles_t *lightTris = sint->lightTris;

			if ( lightTris ) {

				// try to cull before adding
				// FIXME: this may not be worthwhile. We have already done culling on the ambient,
				// but individual surfaces may still be cropped somewhat more
				if ( lightDef->parms.globalLight || R_ShouldDisableEntityCullingForLevelshot() || !R_CullLocalBox( lightTris->bounds, vEntity->modelMatrix, 5, tr.viewDef->frustum ) ) {

					// make sure the original surface has its ambient cache created
					srfTriangles_t *ambientTris = sint->ambientTris;
					bool canAddLightSurf = true;

					const bool packedAmbientSurface =
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
						( ambientTris->primBatchMesh != NULL );
#else
						false;
#endif

					if ( ambientTris->verts == NULL && !packedAmbientSurface ) {
						canAddLightSurf = false;
					} else {
						if ( packedAmbientSurface ) {
							if ( !R_CreatePackedSurfaceFrameCaches( ambientTris, sint->shader->ReceivesLighting(), true ) ) {
								canAddLightSurf = false;
							}
						} else if ( !ambientTris->ambientCache ) {
							if ( !R_CreateAmbientCache( ambientTris, sint->shader->ReceivesLighting() ) ) {
								canAddLightSurf = false;
							}
						}

						if ( canAddLightSurf ) {
							lightTris->ambientCache = ambientTris->ambientCache;
							lightTris->tempAmbientCache = ambientTris->tempAmbientCache;
							R_TouchVertexCache( ambientTris->ambientCache );

							// regenerate the lighting cache (for non-vertex program cards) if it has been purged
							if ( !lightTris->lightingCache && !R_CreateLightingCache( entityDef, lightDef, lightTris ) ) {
								canAddLightSurf = false;
							}
						}

						if ( canAddLightSurf ) {
							// touch the light surface so it won't get purged
							// (vertex program cards won't have a light cache at all)
							if ( lightTris->lightingCache ) {
								R_TouchVertexCache( lightTris->lightingCache );
							}

							if ( packedAmbientSurface ) {
								// confine the frame-temp caches to a frame-local copy of
								// the light tris and link that instead (the BSE frame-submit
								// pattern): temp blocks must never be stored across frames
								// on the heap-owned sint->lightTris, because AllocFrameTemp
								// headers (including its static-overflow fallback blocks)
								// are recycled and a stale pointer can alias another
								// surface's live cache when the interaction is later freed
								srfTriangles_t *frameTris = (srfTriangles_t *)R_FrameAlloc( sizeof( *frameTris ) );
								*frameTris = *lightTris;
								frameTris->gpuSkinningJointPaletteAlloc = NULL;
								frameTris->numGpuSkinningJointPaletteAllocJoints = 0;
								lightTris->ambientCache = NULL;
								lightTris->tempAmbientCache = false;
								lightTris->indexCache = NULL;
								lightTris = frameTris;
								if ( r_useIndexBuffers.GetBool() && lightTris->indexes != NULL && lightTris->numIndexes > 0 ) {
									lightTris->indexCache = vertexCache.AllocFrameTemp(
										lightTris->indexes,
										lightTris->numIndexes * sizeof( lightTris->indexes[0] ),
										true );
								} else {
									lightTris->indexCache = NULL;
								}
							} else if ( !lightTris->indexCache && lightTris->indexes != NULL && lightTris->numIndexes > 0 && R_StaticIndexCacheAllowed( entityDef ) ) {
								vertexCache.Alloc(
									lightTris->indexes,
									lightTris->numIndexes * sizeof( lightTris->indexes[0] ),
									&lightTris->indexCache,
									true );
							}
							if ( lightTris->indexCache ) {
								R_TouchVertexCache( lightTris->indexCache );
							}
						}
					}

					if ( !canAddLightSurf ) {
						// skip if we were out of vertex memory
						continue;
					}

					// add the surface to the light list

					const idMaterial *surfaceShader = sint->shader;
					const bool materialNoSelfShadow = surfaceShader->TestMaterialFlag( MF_NOSELFSHADOW );
					const materialCoverage_t surfaceCoverage = surfaceShader->Coverage();
					const idMaterial *shader = surfaceShader;
					R_GlobalShaderOverride( &shader );
					if ( shader == NULL || !shader->IsDrawn() ) {
						continue;
					}

					// Retail Quake 4 keeps the interaction ownership split anchored to
					// the original surface material, even if a global shader override
					// swaps the shader that will actually be drawn for the light pass.
					if ( surfaceCoverage == MC_TRANSLUCENT ) {
						R_LinkLightSurf( &vLight->translucentInteractions, lightTris, 
							vEntity, lightDef, shader, lightScissor, false );
					} else if ( !lightDef->parms.noShadows && materialNoSelfShadow ) {
						R_LinkLightSurf( &vLight->localInteractions, lightTris, 
							vEntity, lightDef, shader, lightScissor, false );
					} else {
						R_LinkLightSurf( &vLight->globalInteractions, lightTris, 
							vEntity, lightDef, shader, lightScissor, false );
					}
				}
			}
		}

		if ( !interactionHasShadows ) {
			continue;
		}

		bool shadowSuppressed = false;
		if ( !r_skipSuppress.GetBool() ) {
			if ( entityDef->parms.suppressShadowInViewID &&
				entityDef->parms.suppressShadowInViewID == tr.viewDef->renderView.viewID ) {
				shadowSuppressed = true;
			}
			if ( entityDef->parms.suppressShadowInLightID &&
				entityDef->parms.suppressShadowInLightID == lightDef->parms.lightId ) {
				shadowSuppressed = true;
			}
		}
		if ( shadowSuppressed ) {
			continue;
		}

		if ( sint->shader == NULL || sint->ambientTris == NULL ) {
			continue;
		}

		const idMaterial *shadowShader = sint->shader;

		// Shadow ownership and shadow-admission policy stay tied to the original
		// interaction material. Retail does not let global shader overrides change
		// whether a surface casts or locally routes its shadows.
		const bool materialNoSelfShadow = shadowShader->TestMaterialFlag( MF_NOSELFSHADOW );
		const int shadowReceiverMask = materialNoSelfShadow
			? SHADOWMAP_RECEIVER_MASK_GLOBAL
			: ( SHADOWMAP_RECEIVER_MASK_LOCAL | SHADOWMAP_RECEIVER_MASK_GLOBAL );
		bool admittedShadowMapCaster = false;
		bool linkedShadowMapCaster = false;

		// the shadow-map caster policy (spectrum match, emitter-panel geometry
		// check, LOD admission) is per-surface work that only matters when the
		// shadow-map path is active; the default stencil-only configuration
		// skips it entirely, as do point lights the backend will refuse
		// (r_shadowMapPointLights 0 previously built full caster chains the
		// backend never consumed)
		if ( shadowMapCasterPolicyActive ) {
			const bool shadowMapNoSelfShadow = materialNoSelfShadow;
			const bool skipPointLightEmitterCaster =
				R_CachedShouldSkipPointLightEmitterCaster( sint, vLight, shadowShader, localLightOrigin, lightDef->parms.lightRadius );
			const bool sameSpectrumShadowMapCaster =
				R_ShadowMapShaderSpectrumMatchesLight( shadowShader, lightDef );
			const bool allowShadowMapCaster =
				shadowMapsEnabled &&
				!entityDef->parms.noShadow &&
				!isViewOnlyEntity &&
				vEntity->modelDepthHack == 0.0f &&
				sint->ambientTris != NULL &&
				sameSpectrumShadowMapCaster &&
				// Thin emissive panels can sit directly on their owning point-light origin,
				// which creates long bogus wedge occluders. Reject only that geometric case
				// instead of blanketing the entire textures/common_lights family.
				!skipPointLightEmitterCaster &&
				( R_ShadowMapShaderCanCastOpaque( shadowShader ) ||
					( !translucentShadowMapSupported && R_ShadowMapShaderCanCastStencilParityTranslucent( shadowShader ) ) ) &&
				R_CachedInteractionShadowLODAdmitted( sint, entityDef );
			const bool allowTranslucentShadowMapCaster =
				translucentShadowMapSupported &&
				!entityDef->parms.noShadow &&
				!isViewOnlyEntity &&
				vEntity->modelDepthHack == 0.0f &&
				sint->ambientTris != NULL &&
				sameSpectrumShadowMapCaster &&
				R_ShadowMapShaderCanCastTranslucent( shadowShader ) &&
				R_CachedInteractionShadowLODAdmitted( sint, entityDef );
			admittedShadowMapCaster =
				allowShadowMapCaster ||
				allowTranslucentShadowMapCaster;

			if ( sint->ambientTris != NULL && !allowShadowMapCaster && !allowTranslucentShadowMapCaster ) {
				R_RecordShadowMapRejectedCaster( vLight, R_ClassifyShadowMapCasterReject( entityDef, vEntity, sint, shadowShader, isViewOnlyEntity, translucentShadowMapSupported, skipPointLightEmitterCaster, sameSpectrumShadowMapCaster, R_CachedInteractionShadowLODAdmitted( sint, entityDef ) ) );
			}

			if ( allowShadowMapCaster ) {
				srfTriangles_t *casterTris = sint->ambientTris;
				const bool haveCasterGeometry = R_EnsureShadowMapCasterCaches( casterTris, entityDef );

				if ( haveCasterGeometry ) {
					R_TouchShadowMapCache( casterTris->ambientCache );
					R_TouchShadowMapCache( casterTris->indexCache );
					R_RecordShadowMapCaster( vLight, entityDef, shadowShader,
						casterTris, false, shadowMapCasterOnly );

					// dynamic casters go to their own chains so cached static
					// tiles stay valid while they move (composed per frame)
					const bool dynamicCasterSurf = R_ShadowMapCasterIsDynamic( entityDef, shadowShader );
					if ( shadowMapNoSelfShadow ) {
						R_LinkShadowMapCasterSurf( dynamicCasterSurf ? &vLight->localShadowMapDynamicCasters : &vLight->localShadowMapCasters,
							casterTris, vEntity, &entityDef->parms, shadowShader, shadowScissor );
					} else {
						R_LinkShadowMapCasterSurf( dynamicCasterSurf ? &vLight->globalShadowMapDynamicCasters : &vLight->globalShadowMapCasters,
							casterTris, vEntity, &entityDef->parms, shadowShader, shadowScissor );
					}
					linkedShadowMapCaster = true;
				}
			}

			if ( allowTranslucentShadowMapCaster ) {
				srfTriangles_t *casterTris = sint->ambientTris;
				const bool haveCasterGeometry = R_EnsureShadowMapCasterCaches( casterTris, entityDef );

				if ( haveCasterGeometry ) {
					R_TouchShadowMapCache( casterTris->ambientCache );
					R_TouchShadowMapCache( casterTris->indexCache );
					R_RecordShadowMapCaster( vLight, entityDef, shadowShader,
						casterTris, true, shadowMapCasterOnly );

					if ( shadowMapNoSelfShadow ) {
						R_LinkShadowMapCasterSurf( &vLight->localTranslucentShadowMapCasters,
							casterTris, vEntity, &entityDef->parms, shadowShader, shadowScissor );
					} else {
						R_LinkShadowMapCasterSurf( &vLight->globalTranslucentShadowMapCasters,
							casterTris, vEntity, &entityDef->parms, shadowShader, shadowScissor );
					}
					linkedShadowMapCaster = true;
				}
			}
		}

		srfTriangles_t *shadowTris = sint->shadowTris;
		// Use this surface's cached stencil eligibility as provenance rather than
		// the presence of a generated volume: eligible dynamic volumes may have
		// been suppressed for mapped rendering, and generation/cache setup can
		// fail. Conservative caster-only interactions must retain that same
		// surface's stencil representation when map admission fails, without
		// pulling unrelated or ineligible volumes into the hybrid chains.
		const bool mapMissingCasterNeedsStencil =
			shadowMapCasterPolicyActive &&
			!sint->shadowStencilUsesPrelight &&
			!linkedShadowMapCaster &&
			( admittedShadowMapCaster ||
				( sint->shadowStencilEligible &&
					// Emitter panels are deliberately absent from the depth map.
					// CreateInteraction probes their stencil volume even when
					// volumes would be elided. An empty probe has no occlusion
					// to recover and must not invalidate every other caster.
					( !sint->pointEmitterCasterSkip || shadowTris != NULL ) ) );
		const bool prelightMapMissingCasterNeedsStencil =
			shadowMapCasterPolicyActive &&
			sint->shadowStencilUsesPrelight &&
			!linkedShadowMapCaster;
		if ( ( mapMissingCasterNeedsStencil ||
				prelightMapMissingCasterNeedsStencil ) &&
			lightDef != NULL ) {
			if ( r_shadowMapReport.GetInteger() >= 2 && !lightDef->shadowMapStencilFallbackSticky ) {
				common->Printf( "SM missing caster: light=%d shader=%s entity=%d model=%s material=%s admitted=%d eligible=%d volume=%d prelight=%d viewOnly=%d depthHack=%g\n",
					lightDef->index, lightDef->lightShader->GetName(), entityDef->index,
					model->Name(), shadowShader->GetName(), admittedShadowMapCaster,
					sint->shadowStencilEligible, shadowTris != NULL,
					sint->shadowStencilUsesPrelight, isViewOnlyEntity, vEntity->modelDepthHack );
			}
			// Keep this light's volume links resident: Vulkan can combine the
			// partial ownership map with per-surface missing-caster volumes,
			// or fall back to the combined prelight volume.
			lightDef->shadowMapStencilFallbackSticky = true;
		}
		const bool volumeElidedForShadowMaps =
			shadowTris != NULL &&
			!shadowMapCasterOnly &&
			R_ShadowMapLightWillUseShadowMaps( lightDef );
		bool mapMissingNeedsVisibleVolume = false;
		bool mappedCasterNeedsVisibleFallbackVolume = false;

		if ( shadowMapCasterPolicyActive ) {
			if ( sint->shadowStencilUsesPrelight ) {
				// Static-world per-surface volumes are replaced by one combined
				// prelight volume. R_AddOptimizedPrelightShadows resolves these
				// pending masks after its whole-volume cull/resource checks.
				vLight->shadowMapPrelightStencilRequiredMask |=
					shadowReceiverMask;
				if ( !linkedShadowMapCaster ) {
					vLight->shadowMapPrelightMapMissingMask |=
						shadowReceiverMask;
				}
			} else if ( linkedShadowMapCaster ) {
				// A mapped-only/expanded caster cannot be recovered by a
				// different surface's volume. Treat a backend map miss as
				// fail-closed even when this light has other stencil surfs.
				if ( shadowTris == NULL || volumeElidedForShadowMaps ) {
					vLight->shadowMapIncompleteStencilMask |=
						shadowReceiverMask;
				} else {
					mappedCasterNeedsVisibleFallbackVolume = true;
				}
			} else if ( mapMissingCasterNeedsStencil ) {
				// If the usable volume survives the per-view cull below, this
				// stock stencil caster is absent from the ownership map. An
				// admitted mapped caster whose geometry-cache setup failed
				// follows the same path so it cannot silently disappear from
				// both representations.
				if ( shadowTris == NULL || volumeElidedForShadowMaps ) {
					vLight->shadowMapIncompleteMapMask |=
						shadowReceiverMask;
					vLight->shadowMapIncompleteStencilMask |=
						shadowReceiverMask;
					vLight->shadowMapHybridIncompleteMask |=
						shadowReceiverMask;
				} else {
					mapMissingNeedsVisibleVolume = true;
				}
			}
		}

		// the shadows will always have to be added, unless we can tell they
		// are from a surface in an unconnected area, or the light renders
		// shadow maps this frame and its stencil volumes would go unused
		const bool shouldLinkShadowVolume =
			shadowTris != NULL &&
			( !shadowMapCasterOnly ||
				mappedCasterNeedsVisibleFallbackVolume ||
				mapMissingNeedsVisibleVolume );
		if ( shouldLinkShadowVolume &&
			!volumeElidedForShadowMaps ) {

			// cull static shadows that have a non-empty bounds
			// dynamic shadows that use the turboshadow code will not have valid
			// bounds, because the perspective projection extends them to infinity
			if ( !shadowMapCasterOnly &&
				r_useShadowCulling.GetBool() &&
				!R_ShouldDisableEntityCullingForLevelshot() &&
				!shadowTris->bounds.IsCleared() ) {
				if ( R_CullLocalBox( shadowTris->bounds, vEntity->modelMatrix, 5, tr.viewDef->frustum ) ) {
					continue;
				}
			}

			if ( mapMissingNeedsVisibleVolume ) {
				vLight->shadowMapIncompleteMapMask |= shadowReceiverMask;
			}
			if ( !R_EnsureInteractionShadowCache( sint, entityDef ) ) {
				if ( shadowMapCasterPolicyActive &&
					( linkedShadowMapCaster ||
						mapMissingNeedsVisibleVolume ) ) {
					vLight->shadowMapIncompleteStencilMask |=
						shadowReceiverMask;
					if ( mapMissingNeedsVisibleVolume ) {
						vLight->shadowMapHybridIncompleteMask |=
							shadowReceiverMask;
					}
				}
				continue;
			}

			// see if we can avoid using the shadow volume caps
			bool inside = R_PotentiallyInsideInfiniteShadow( sint->ambientTris, localViewOrigin, localLightOrigin );

			const drawSurf_t **fullShadowChain = materialNoSelfShadow
				? &vLight->localShadows
				: &vLight->globalShadows;
			const bool linkedFullShadowVolume = R_LinkLightSurf(
				fullShadowChain, shadowTris, vEntity, lightDef, NULL,
				shadowScissor, inside );
			if ( shadowMapCasterPolicyActive &&
				( linkedShadowMapCaster ||
					mapMissingNeedsVisibleVolume ) &&
				!linkedFullShadowVolume ) {
				vLight->shadowMapIncompleteStencilMask |=
					shadowReceiverMask;
			}

			if ( mapMissingNeedsVisibleVolume ) {
				const drawSurf_t **supplementChain = materialNoSelfShadow
					? &vLight->localShadowMapStencilSupplements
					: &vLight->globalShadowMapStencilSupplements;
				const bool linkedStencilSupplement = R_LinkLightSurf(
					supplementChain, shadowTris, vEntity, lightDef, NULL,
					shadowScissor, inside );
				if ( !linkedStencilSupplement ) {
					vLight->shadowMapHybridIncompleteMask |=
						shadowReceiverMask;
				}
			}
		}
	}
}

/*
===================
R_ShowInteractionMemory_f
===================
*/
void R_ShowInteractionMemory_f( const idCmdArgs &args ) {
	int total = 0;
	int entities = 0;
	int interactions = 0;
	int deferredInteractions = 0;
	int emptyInteractions = 0;
	int lightTris = 0;
	int lightTriVerts = 0;
	int lightTriIndexes = 0;
	int shadowTris = 0;
	int shadowTriVerts = 0;
	int shadowTriIndexes = 0;

	for ( int i = 0; i < tr.primaryWorld->entityDefs.Num(); i++ ) {
		idRenderEntityLocal	*def = tr.primaryWorld->entityDefs[i];
		if ( !def ) {
			continue;
		}
		if ( def->firstInteraction == NULL ) {
			continue;
		}
		entities++;

		for ( idInteraction *inter = def->firstInteraction; inter != NULL; inter = inter->entityNext ) {
			interactions++;
			total += inter->MemoryUsed();

			if ( inter->IsDeferred() ) {
				deferredInteractions++;
				continue;
			}
			if ( inter->IsEmpty() ) {
				emptyInteractions++;
				continue;
			}

			for ( int j = 0; j < inter->numSurfaces; j++ ) {
				surfaceInteraction_t *srf = &inter->surfaces[j];

				if ( srf->lightTris && srf->lightTris != LIGHT_TRIS_DEFERRED ) {
					lightTris++;
					lightTriVerts += srf->lightTris->numVerts;
					lightTriIndexes += srf->lightTris->numIndexes;
				}
				if ( srf->shadowTris ) {
					shadowTris++;
					shadowTriVerts += srf->shadowTris->numVerts;
					shadowTriIndexes += srf->shadowTris->numIndexes;
				}
			}
		}
	}

	common->Printf( "%i entities with %i total interactions totalling %ik\n", entities, interactions, total / 1024 );
	common->Printf( "%i deferred interactions, %i empty interactions\n", deferredInteractions, emptyInteractions );
	common->Printf( "%5i indexes %5i verts in %5i light tris\n", lightTriIndexes, lightTriVerts, lightTris );
	common->Printf( "%5i indexes %5i verts in %5i shadow tris\n", shadowTriIndexes, shadowTriVerts, shadowTris );
}
