// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ScenePackets.h"
#include "RendererBootstrap.h"

static idScenePacketFrame rg_frontEndScenePacketFrame;
static bool rg_frontEndScenePacketFrameOpen = false;
// The front end emits RC_COPY_RENDER immediately after a capture-backed
// subview's RC_DRAW_VIEW. Preserve that producer while assembling packet data;
// tr.viewDef has already been restored to the parent when CaptureRenderToImage
// queues its copy command.
static const viewDef_t *rg_frontEndLastDrawView = NULL;

typedef struct scenePacketTemporalInstanceHistory_s {
	unsigned long long viewIdentity;
	unsigned int historyGeneration;
	int frameNumber;
	int entityIndex;
	float modelMatrix[16];
} scenePacketTemporalInstanceHistory_t;

static idList<scenePacketTemporalInstanceHistory_t>
	rg_scenePacketTemporalInstanceHistory;
static idList<scenePacketTemporalInstanceHistory_t>
	rg_scenePacketTemporalNextInstanceHistory;

// Hash acceleration for the (viewIdentity, entityIndex) history lookups.
// Each idHashIndex always mirrors the list its pointer is paired with; the
// pointers swap in lockstep with the list Swap() in Commit. These are only
// touched from the runtime Find/Store/Commit paths, never from
// idScenePacketFrame::Clear(), preserving the static-constructor constraint
// documented above the lookup-slot table.
static idHashIndex rg_scenePacketTemporalHistoryLookupA;
static idHashIndex rg_scenePacketTemporalHistoryLookupB;
static idHashIndex *rg_scenePacketTemporalHistoryLookup =
	&rg_scenePacketTemporalHistoryLookupA;
static idHashIndex *rg_scenePacketTemporalNextHistoryLookup =
	&rg_scenePacketTemporalHistoryLookupB;

static int R_ScenePackets_TemporalHistoryLookupKey(
		unsigned long long viewIdentity, int entityIndex ) {
	return static_cast<int>(
		static_cast<unsigned int>( viewIdentity )
		^ static_cast<unsigned int>( viewIdentity >> 32 )
		^ ( static_cast<unsigned int>( entityIndex ) * 0x9e3779b9u ) );
}

static bool R_ScenePackets_FindTemporalInstanceHistory(
		unsigned long long viewIdentity, int entityIndex,
		float previousModelMatrix[16], int &ageFrames ) {
	ageFrames = 0;
	if ( viewIdentity == 0 || entityIndex < 0 ) {
		return false;
	}
	const unsigned int historyGeneration =
		R_TemporalPresentation_HistoryGeneration();
	const int lookupKey =
		R_ScenePackets_TemporalHistoryLookupKey( viewIdentity, entityIndex );
	for ( int i = rg_scenePacketTemporalHistoryLookup->First( lookupKey );
			i >= 0; i = rg_scenePacketTemporalHistoryLookup->Next( i ) ) {
		const scenePacketTemporalInstanceHistory_t &entry =
			rg_scenePacketTemporalInstanceHistory[i];
		if ( entry.viewIdentity != viewIdentity
				|| entry.entityIndex != entityIndex
				|| entry.historyGeneration != historyGeneration ) {
			continue;
		}
		const unsigned int age = static_cast<unsigned int>( tr.frameCount )
			- static_cast<unsigned int>( entry.frameNumber );
		if ( age == 0u || age > 2u ) {
			return false;
		}
		memcpy( previousModelMatrix, entry.modelMatrix,
			sizeof( entry.modelMatrix ) );
		ageFrames = static_cast<int>( age );
		return true;
	}
	return false;
}

static void R_ScenePackets_StoreTemporalInstanceHistory(
		const instanceRecord_t &record ) {
	if ( record.entityIndex < 0 || !record.hasModelMatrix
			|| record.temporalViewIdentity == 0
			|| record.temporalCaptureFrame ) {
		return;
	}
	const int lookupKey = R_ScenePackets_TemporalHistoryLookupKey(
		record.temporalViewIdentity, record.entityIndex );
	for ( int i = rg_scenePacketTemporalNextHistoryLookup->First( lookupKey );
			i >= 0; i = rg_scenePacketTemporalNextHistoryLookup->Next( i ) ) {
		const scenePacketTemporalInstanceHistory_t &existing =
			rg_scenePacketTemporalNextInstanceHistory[i];
		if ( existing.viewIdentity == record.temporalViewIdentity
				&& existing.entityIndex == record.entityIndex ) {
			return;
		}
	}
	scenePacketTemporalInstanceHistory_t &entry =
		rg_scenePacketTemporalNextInstanceHistory.Alloc();
	entry.viewIdentity = record.temporalViewIdentity;
	entry.historyGeneration = record.temporalHistoryGeneration;
	entry.frameNumber = tr.frameCount;
	entry.entityIndex = record.entityIndex;
	memcpy( entry.modelMatrix, record.modelMatrix, sizeof( entry.modelMatrix ) );
	rg_scenePacketTemporalNextHistoryLookup->Add( lookupKey,
		rg_scenePacketTemporalNextInstanceHistory.Num() - 1 );
}

static void R_ScenePackets_CommitTemporalInstanceHistory(
		const idScenePacketFrame &frame ) {
	// SetNum( 0, false ) keeps the allocations (bounded at
	// SCENE_PACKET_MAX_INSTANCE_RECORDS entries) so steady-state capture
	// frames reuse the buffers instead of freeing and regrowing them.
	rg_scenePacketTemporalNextInstanceHistory.SetNum( 0, false );
	rg_scenePacketTemporalNextHistoryLookup->Clear();
	for ( int i = 0; i < frame.NumInstanceRecords(); ++i ) {
		R_ScenePackets_StoreTemporalInstanceHistory( frame.InstanceRecord( i ) );
	}
	rg_scenePacketTemporalInstanceHistory.Swap(
		rg_scenePacketTemporalNextInstanceHistory );
	idSwap( rg_scenePacketTemporalHistoryLookup,
		rg_scenePacketTemporalNextHistoryLookup );
	rg_scenePacketTemporalNextInstanceHistory.SetNum( 0, false );
	rg_scenePacketTemporalNextHistoryLookup->Clear();
}

// A negative view id alone is not a render demo: portal-sky cameras use the
// same convention. The session's active demo reader and its exact render world
// make playback provenance explicit before command buffers are submitted.
static bool R_ScenePackets_IsRenderDemoPlaybackView( const viewDef_t *viewDef ) {
	return viewDef != NULL && viewDef->viewEntitys != NULL
		&& viewDef->renderWorld != NULL && !viewDef->isSubview
		&& session != NULL && session->readDemo != NULL
		&& session->rw == viewDef->renderWorld;
}

// A command-stream reconstruction has no controller payload. Reproduce the
// exact admission mask at the point the backend consumes the command so a
// backend-derived packet retains the same special-frame provenance as the
// front-end packet.
static int R_ScenePackets_ActiveSpecialEffectsMask( void ) {
	int activeMask = tr.specialEffectsEnabled;
	if ( r_forceSpecialEffects.GetInteger() > 0 ) {
		activeMask = r_forceSpecialEffects.GetInteger();
	}
	return activeMask & ( SPECIAL_EFFECT_BLUR | SPECIAL_EFFECT_AL );
}

static bool R_ScenePackets_ModernPipelineRequested( void ) {
	const bool modernVisibleRequested = r_rendererModernVisible.GetBool() || RendererBootstrap_ShouldAutoPromoteModernVisible();
	const bool advancedLightingLeafRequested = r_rendererModernQuality.GetBool()
		&& ( r_rendererReflectionProbes.GetBool() || r_rendererClusteredDecals.GetBool() );
	const bool shadowMapSidecarRequested =
		r_useShadowMap.GetBool()
		&& r_shadows.GetBool()
		&& ( r_shadowMapDebugOverlay.GetInteger() > 0 || r_shadowMapReport.GetInteger() > 0 );

	return r_rendererModernExecutor.GetBool()
		|| r_rendererModernSubmit.GetBool()
		|| r_rendererGpuValidation.GetBool()
		|| r_rendererBindless.GetBool()
		|| modernVisibleRequested
		|| advancedLightingLeafRequested
		|| r_rendererSharedGui.GetBool()
		|| r_rendererSharedInWorldGui.GetBool()
		|| r_rendererSharedCinematicPost.GetBool()
		|| r_rendererSharedSpecialFrame.GetBool()
		|| r_rendererSharedWorldAmbient.GetBool()
		|| r_rendererSharedWorldInteraction.GetBool()
		|| r_rendererSharedWorldFogBlend.GetBool()
		|| r_rendererSharedSubview.GetBool()
		|| r_rendererSharedDeform.GetBool()
		|| r_rendererModernVisibleDepth.GetBool()
		|| r_rendererModernDepthDebug.GetInteger() > 0
		|| r_rendererModernOpaque.GetBool()
		|| r_rendererModernGBufferDebug.GetInteger() > 0
		|| r_rendererModernDeferred.GetBool()
		|| r_rendererModernDeferredDebug.GetInteger() > 0
		|| r_rendererForwardPlus.GetBool()
		|| r_rendererClusterDebug.GetInteger() > 0
		|| shadowMapSidecarRequested;
}

class rendererSelfTestSkipPostProcessRestore_t {
public:
	rendererSelfTestSkipPostProcessRestore_t()
		: oldValue( r_skipPostProcess.GetBool() ) {
		r_skipPostProcess.SetBool( true );
	}
	~rendererSelfTestSkipPostProcessRestore_t() {
		r_skipPostProcess.SetBool( oldValue );
	}

private:
	bool oldValue;
};

bool R_ScenePackets_FrontEndCaptureRequired( void ) {
	// The shared subview transaction has to retain the exact RC_DRAW_VIEW ->
	// RC_COPY_RENDER edge on the front end. Backend reconstruction cannot infer
	// it when an image capture occurs after tr.viewDef returns to the parent.
	return r_rendererSharedSubview.GetBool()
		|| R_TemporalPresentation_TemporalAARequested()
		|| r_rendererMetrics.GetInteger() >= 2;
}

bool R_ScenePackets_SidePipelineRequired( void ) {
	// gles_d3 renders the view itself and shares no state with the modern
	// executor. Leaving the side pipeline armed under BE_GLES_D3 does not just
	// waste work: R_ModernGLExecutor_ComposeVisibleFrame and SubmitModernGui
	// run on RC_SWAP_BUFFERS and paint an empty composite over the finished
	// frame -- the same failure mode the legacy post chain has on this
	// context. This is the single gate that keeps the two backends from
	// touching each other.
	if ( tr.backEndRenderer == BE_GLES_D3 ) {
		return false;
	}

	return R_ScenePackets_ModernPipelineRequested()
		|| R_TemporalPresentation_TemporalAARequested()
		|| r_rendererMetrics.GetInteger() >= 2;
}

// Side tables accelerating the FindOrAdd* record dedup scans.  They are keyed
// on the owning frame instead of living inside idScenePacketFrame; a frame
// claims a slot in Clear() and falls back to the linear scan if its slot was
// stolen by another live frame.  Clear() also runs from static constructors in
// other translation units, so the claim/release paths must only touch
// zero-initialized POD state, never the idHashIndex objects; the deferred
// dirty wipe keeps the hash resets on the capture path only.
// SCENE_PACKET_LOOKUP_SLOTS must stay a power of two for the steal mask.
static const int SCENE_PACKET_LOOKUP_SLOTS = 4;
static const idScenePacketFrame *rg_scenePacketLookupOwner[SCENE_PACKET_LOOKUP_SLOTS];
static bool rg_scenePacketLookupDirty[SCENE_PACKET_LOOKUP_SLOTS];
static int rg_scenePacketLookupNextSteal;
static idHashIndex rg_scenePacketMaterialLookup[SCENE_PACKET_LOOKUP_SLOTS];
static idHashIndex rg_scenePacketGeometryLookup[SCENE_PACKET_LOOKUP_SLOTS];
static idHashIndex rg_scenePacketInstanceLookup[SCENE_PACKET_LOOKUP_SLOTS];

static int R_ScenePackets_PointerLookupKey( const void *pointer ) {
	const size_t address = reinterpret_cast<size_t>( pointer );
	return static_cast<int>( ( address >> 4 ) ^ ( address >> 14 ) );
}

static classicDeformRole_t R_ScenePackets_ClassicDeformRoleForPass(
		renderPassCategory_t category ) {
	switch ( category ) {
	case RENDER_PASS_ARB2_INTERACTION:
		return CLASSIC_DEFORM_ROLE_INTERACTION_RECEIVER;
	case RENDER_PASS_FOG_BLEND:
		return CLASSIC_DEFORM_ROLE_FOG_RECEIVER;
	case RENDER_PASS_STENCIL_SHADOW:
		return CLASSIC_DEFORM_ROLE_SHADOW_VOLUME;
	default:
		// Ambient, depth, shadow-map caster, and forward/post classifications
		// originate from the finalized view draw rather than receiver chains.
		return CLASSIC_DEFORM_ROLE_FINALIZED_DRAW;
	}
}

static int R_ScenePackets_GeometryLookupKey( const srfTriangles_t *geometry,
		const classicDeformRecord_t &classicDeform ) {
	const std::uint64_t semantic = classicDeform.semanticHash;
	const std::uint64_t frame = classicDeform.frameToken;
	const unsigned int mixed =
		static_cast<unsigned int>( R_ScenePackets_PointerLookupKey( geometry ) )
		^ static_cast<unsigned int>( semantic )
		^ static_cast<unsigned int>( semantic >> 32 )
		^ static_cast<unsigned int>( frame )
		^ static_cast<unsigned int>( frame >> 32 )
		^ ( static_cast<unsigned int>( classicDeform.role ) * 0x9e3779b9u );
	return static_cast<int>( mixed );
}

static void R_ScenePackets_ClaimLookupSlot( const idScenePacketFrame *frame ) {
	int slot = -1;
	for ( int i = 0; i < SCENE_PACKET_LOOKUP_SLOTS; ++i ) {
		if ( rg_scenePacketLookupOwner[i] == frame ) {
			slot = i;
			break;
		}
		if ( slot < 0 && rg_scenePacketLookupOwner[i] == NULL ) {
			slot = i;
		}
	}
	if ( slot < 0 ) {
		slot = rg_scenePacketLookupNextSteal++ & ( SCENE_PACKET_LOOKUP_SLOTS - 1 );
	}
	rg_scenePacketLookupOwner[slot] = frame;
	rg_scenePacketLookupDirty[slot] = true;
}

static int R_ScenePackets_PreparedLookupSlot( const idScenePacketFrame *frame ) {
	for ( int i = 0; i < SCENE_PACKET_LOOKUP_SLOTS; ++i ) {
		if ( rg_scenePacketLookupOwner[i] != frame ) {
			continue;
		}
		if ( rg_scenePacketLookupDirty[i] ) {
			rg_scenePacketMaterialLookup[i].Clear();
			rg_scenePacketGeometryLookup[i].Clear();
			rg_scenePacketInstanceLookup[i].Clear();
			rg_scenePacketLookupDirty[i] = false;
		}
		return i;
	}
	return -1;
}

// transient frames (selftests, backend fallback) must hand their slot back, or
// the table fills with dangling owners and long-lived frames get stolen from
static void R_ScenePackets_ReleaseLookupSlot( const idScenePacketFrame *frame ) {
	for ( int i = 0; i < SCENE_PACKET_LOOKUP_SLOTS; ++i ) {
		if ( rg_scenePacketLookupOwner[i] == frame ) {
			rg_scenePacketLookupOwner[i] = NULL;
			rg_scenePacketLookupDirty[i] = true;
		}
	}
}

idScenePacketFrame::idScenePacketFrame() {
	Clear();
}

idScenePacketFrame::~idScenePacketFrame() {
	R_ScenePackets_ReleaseLookupSlot( this );
}

void idScenePacketFrame::Clear( void ) {
	// The packet/record arrays are intentionally NOT wiped here: every slot is
	// memset at allocation time (AddScene/AddPass/AddDrawPacket/FindOrAdd*) and
	// every consumer iterates count-bounded via stats, so wiping ~1 MB of arrays
	// per Clear() is pure waste on the default path where capture never opens.
#ifdef _DEBUG
	memset( scenes, 0, sizeof( scenes ) );
	memset( passes, 0, sizeof( passes ) );
	memset( drawPackets, 0, sizeof( drawPackets ) );
	memset( materialRecords, 0, sizeof( materialRecords ) );
	memset( geometryRecords, 0, sizeof( geometryRecords ) );
	memset( instanceRecords, 0, sizeof( instanceRecords ) );
	memset( subviewCaptures, 0, sizeof( subviewCaptures ) );
#endif
	memset( &stats, 0, sizeof( stats ) );
	activeScene = -1;
	activePass = -1;
	activePassLastSortKey = 0;
	activePassSortKeyValid = false;
	R_ScenePackets_ClaimLookupSlot( this );
}

void idScenePacketFrame::SetOverflow( scenePacketOverflowCause_t cause ) {
	stats.overflow = true;
	if ( stats.overflowCause == SCENE_PACKET_OVERFLOW_NONE ) {
		stats.overflowCause = cause;
	}
}

void idScenePacketFrame::CountCategory( scenePacketCategory_t category ) {
	switch ( category ) {
	case SCENE_PACKET_CATEGORY_WORLD:
		stats.worldPackets++;
		break;
	case SCENE_PACKET_CATEGORY_SUBVIEW:
		stats.subviewPackets++;
		break;
	case SCENE_PACKET_CATEGORY_REMOTE_CAMERA:
		stats.remoteCameraPackets++;
		break;
	case SCENE_PACKET_CATEGORY_SPECIAL_EFFECTS:
		stats.specialEffectPackets++;
		break;
	case SCENE_PACKET_CATEGORY_VIEWMODEL:
		stats.viewmodelPackets++;
		break;
	case SCENE_PACKET_CATEGORY_RENDER_DEMO:
		stats.renderDemoPackets++;
		break;
	case SCENE_PACKET_CATEGORY_GUI:
		stats.guiPackets++;
		break;
	case SCENE_PACKET_CATEGORY_POST_PROCESS:
		stats.postProcessPackets++;
		break;
	case SCENE_PACKET_CATEGORY_PRESENT:
		stats.presentPackets++;
		break;
	case SCENE_PACKET_CATEGORY_COMMAND:
		stats.commandOnlyPackets++;
		break;
	case SCENE_PACKET_CATEGORY_UNKNOWN:
	default:
		break;
	}
}

static rendererMaterialClass_t R_ScenePackets_MaterialClassForDrawSurf( const drawSurf_t *drawSurf ) {
	if ( drawSurf == NULL || drawSurf->material == NULL ) {
		return RENDER_MATERIAL_NONE;
	}

	const idMaterial *material = drawSurf->material;
	const float sort = material->GetSort();
	if ( sort == SS_SUBVIEW ) {
		return RENDER_MATERIAL_SUBVIEW;
	}
	if ( sort >= SS_POST_PROCESS ) {
		return RENDER_MATERIAL_POST_PROCESS;
	}
	if ( material->HasGui() || sort == SS_GUI || sort == SS_PREGUI ) {
		return RENDER_MATERIAL_GUI;
	}

	switch ( material->Coverage() ) {
	case MC_OPAQUE:
		return RENDER_MATERIAL_OPAQUE;
	case MC_PERFORATED:
		return RENDER_MATERIAL_PERFORATED;
	case MC_TRANSLUCENT:
		return RENDER_MATERIAL_TRANSLUCENT;
	default:
		break;
	}
	return material->IsDrawn() ? RENDER_MATERIAL_OPAQUE : RENDER_MATERIAL_SHADOW_ONLY;
}

static bool R_ScenePackets_DrawSurfIsDecalMaterialPass( const drawSurf_t *drawSurf ) {
	const idMaterial *material = drawSurf != NULL ? drawSurf->material : NULL;
	if ( material == NULL ) {
		return false;
	}
	return drawSurf->decalColorCache != NULL
		|| ( material->GetSort() >= SS_DECAL && material->GetSort() < SS_FAR );
}

static const idImage *R_ScenePackets_FirstStageImage( const idMaterial *material, stageLighting_t lighting ) {
	if ( material == NULL ) {
		return NULL;
	}
	const int stageCount = material->GetNumStages();
	for ( int i = 0; i < stageCount; ++i ) {
		const shaderStage_t *stage = material->GetStage( i );
		if ( stage != NULL && stage->lighting == lighting && stage->texture.image != NULL ) {
			return stage->texture.image;
		}
	}
	return NULL;
}

static const idImage *R_ScenePackets_FirstAmbientImage( const idMaterial *material ) {
	if ( material == NULL ) {
		return NULL;
	}
	// keep this band in sync with R_MaterialResourceTable_SortGroupForMaterial:
	// SS_FAR and up classify as translucent, not decal
	const bool preferClassicDecalStage = material->GetSort() >= SS_DECAL && material->GetSort() < SS_FAR;
	const idImage *firstAmbientImage = NULL;
	const int stageCount = material->GetNumStages();
	for ( int i = 0; i < stageCount; ++i ) {
		const shaderStage_t *stage = material->GetStage( i );
		if ( stage != NULL && stage->lighting == SL_AMBIENT && stage->texture.image != NULL ) {
			if ( firstAmbientImage == NULL ) {
				firstAmbientImage = stage->texture.image;
			}
			if ( !preferClassicDecalStage || stage->newStage == NULL ) {
				return stage->texture.image;
			}
		}
	}
	return firstAmbientImage;
}

static geometrySkinningMode_t R_ScenePackets_DrawSurfSkinningMode(
	const drawSurf_t *drawSurf, gpuSkinningSurface_t *surface = NULL ) {
	if ( surface != NULL ) {
		memset( surface, 0, sizeof( *surface ) );
	}
	if ( drawSurf == NULL || drawSurf->geo == NULL ) {
		return GEOMETRY_SKINNING_NONE;
	}
	gpuSkinningSurface_t candidate;
	if ( R_GpuSkinning_GetSurface( drawSurf->geo, candidate ) ) {
		if ( surface != NULL ) {
			*surface = candidate;
		}
		return GEOMETRY_SKINNING_GPU_PALETTE;
	}
	// A source-side exactness or eligibility rejection means the ordinary
	// deformed idDrawVert stream is authoritative for this snapshot.
	if ( R_GpuSkinning_IsCandidate( drawSurf->geo ) ) {
		return GEOMETRY_SKINNING_CPU;
	}
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( drawSurf->geo->numSkinToModelTransforms > 0 ) {
		return GEOMETRY_SKINNING_GPU_PALETTE;
	}
#endif
	return GEOMETRY_SKINNING_NONE;
}

static bool R_ScenePackets_MaterialUsesRemoteRender( const idMaterial *material ) {
	if ( material == NULL ) {
		return false;
	}
	const int stageCount = material->GetNumStages();
	for ( int i = 0; i < stageCount; ++i ) {
		const shaderStage_t *stage = material->GetStage( i );
		if ( stage != NULL && stage->texture.dynamic == DI_REMOTE_RENDER ) {
			return true;
		}
	}
	return false;
}

bool R_ScenePackets_TemporalRigidMotionEligible(
		const drawSurf_t *drawSurf ) {
	if ( drawSurf == NULL || drawSurf->geo == NULL
			|| drawSurf->material == NULL || drawSurf->space == NULL
			|| drawSurf->space->entityDef == NULL ) {
		return false;
	}

	const srfTriangles_t *geometry = drawSurf->geo;
	const idMaterial *material = drawSurf->material;
	const idRenderEntityLocal *entity = drawSurf->space->entityDef;
	if ( !material->IsDrawn() || !material->HasAmbient()
			|| material->IsPortalSky() || material->SuppressInSubview()
			|| material->Coverage() != MC_OPAQUE
			|| material->GetSort() >= SS_POST_PROCESS ) {
		return false;
	}
	if ( geometry->numIndexes <= 0 || R_TriHasPrimBatchMesh( geometry )
			|| geometry->deformedSurface || drawSurf->space->weaponDepthHack
			|| drawSurf->space->modelDepthHack != 0.0f ) {
		return false;
	}
	return entity->index >= 0 && entity->parms.callback == NULL
		&& entity->parms.hModel != NULL
		&& entity->parms.hModel->IsDynamicModel() == DM_STATIC
		&& entity->dynamicModel == NULL;
}

static scenePacketCategory_t R_ScenePackets_CategoryForDrawSurf( const viewDef_t *viewDef, const drawSurf_t *drawSurf, renderPassCategory_t passCategory, const materialResourceRecord_t *materialRecord = NULL ) {
	if ( passCategory == RENDER_PASS_SPECIAL_EFFECTS ) {
		return SCENE_PACKET_CATEGORY_SPECIAL_EFFECTS;
	}
	if ( passCategory == RENDER_PASS_PRESENT ) {
		return SCENE_PACKET_CATEGORY_PRESENT;
	}
	if ( passCategory == RENDER_PASS_SSAO
		|| passCategory == RENDER_PASS_MOTION_BLUR
		|| passCategory == RENDER_PASS_BLOOM
		|| passCategory == RENDER_PASS_AUTHORED_POST ) {
		return SCENE_PACKET_CATEGORY_POST_PROCESS;
	}
	if ( passCategory == RENDER_PASS_GUI ) {
		// A GUI pass in a 3D scene is the provenance-tagged output of
		// R_RenderGuiSurf, not a root-2D command list. Keep it in the world
		// domain so material compilation retains depth-test semantics.
		return viewDef != NULL && viewDef->viewEntitys != NULL
			? SCENE_PACKET_CATEGORY_WORLD : SCENE_PACKET_CATEGORY_GUI;
	}
	if ( R_ScenePackets_IsRenderDemoPlaybackView( viewDef ) ) {
		return SCENE_PACKET_CATEGORY_RENDER_DEMO;
	}
	if ( viewDef != NULL && viewDef->isSubview ) {
		return SCENE_PACKET_CATEGORY_SUBVIEW;
	}
	if ( viewDef != NULL && viewDef->renderView.viewID < 0 ) {
		return SCENE_PACKET_CATEGORY_RENDER_DEMO;
	}
	if ( drawSurf == NULL ) {
		return SCENE_PACKET_CATEGORY_COMMAND;
	}
	if ( drawSurf->space != NULL && drawSurf->space->weaponDepthHack ) {
		return SCENE_PACKET_CATEGORY_VIEWMODEL;
	}
	if ( drawSurf->space != NULL && drawSurf->space->entityDef != NULL && drawSurf->space->entityDef->parms.remoteRenderView != NULL ) {
		return SCENE_PACKET_CATEGORY_REMOTE_CAMERA;
	}
	const bool usesRemoteRender = materialRecord != NULL
		? materialRecord->usesRemoteRender
		: R_ScenePackets_MaterialUsesRemoteRender( drawSurf->material );
	if ( usesRemoteRender ) {
		return SCENE_PACKET_CATEGORY_REMOTE_CAMERA;
	}
	const rendererMaterialClass_t materialClass = materialRecord != NULL
		? static_cast<rendererMaterialClass_t>( materialRecord->permutation.materialClass )
		: R_ScenePackets_MaterialClassForDrawSurf( drawSurf );
	if ( materialClass == RENDER_MATERIAL_SUBVIEW ) {
		return SCENE_PACKET_CATEGORY_SUBVIEW;
	}
	return SCENE_PACKET_CATEGORY_WORLD;
}

static scenePacketCategory_t R_ScenePackets_CategoryForCommandPass( renderPassCategory_t passCategory, const viewDef_t *viewDef ) {
	switch ( passCategory ) {
	case RENDER_PASS_SPECIAL_EFFECTS:
		return SCENE_PACKET_CATEGORY_SPECIAL_EFFECTS;
	case RENDER_PASS_SSAO:
	case RENDER_PASS_MOTION_BLUR:
	case RENDER_PASS_BLOOM:
	case RENDER_PASS_AUTHORED_POST:
		return SCENE_PACKET_CATEGORY_POST_PROCESS;
	case RENDER_PASS_GUI:
		return viewDef != NULL && viewDef->viewEntitys != NULL
			? SCENE_PACKET_CATEGORY_WORLD : SCENE_PACKET_CATEGORY_GUI;
	case RENDER_PASS_PRESENT:
		return SCENE_PACKET_CATEGORY_PRESENT;
	default:
		break;
	}
	if ( R_ScenePackets_IsRenderDemoPlaybackView( viewDef ) ) {
		return SCENE_PACKET_CATEGORY_RENDER_DEMO;
	}
	if ( viewDef != NULL && viewDef->isSubview ) {
		return SCENE_PACKET_CATEGORY_SUBVIEW;
	}
	if ( viewDef != NULL && viewDef->renderView.viewID < 0 ) {
		return SCENE_PACKET_CATEGORY_RENDER_DEMO;
	}
	return SCENE_PACKET_CATEGORY_COMMAND;
}

static void R_ScenePackets_CopyIdentityMatrix( float matrix[16] ) {
	memset( matrix, 0, sizeof( float ) * 16 );
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = 1.0f;
	matrix[15] = 1.0f;
}

static bool R_ScenePackets_MatrixHasNegativeScale( const float matrix[16] ) {
	const float determinant =
		matrix[0] * ( matrix[5] * matrix[10] - matrix[9] * matrix[6] )
		- matrix[4] * ( matrix[1] * matrix[10] - matrix[9] * matrix[2] )
		+ matrix[8] * ( matrix[1] * matrix[6] - matrix[5] * matrix[2] );
	return determinant < 0.0f;
}

static void R_ScenePackets_AddGeometryFallback(
	geometryResourceRecord_t &record,
	geometryResourceFallbackReason_t reason,
	unsigned int flag ) {
	record.fallbackFlags |= flag;
	if ( record.fallbackReason == GEOMETRY_RESOURCE_FALLBACK_NONE ) {
		record.fallbackReason = reason;
	}
}

static geometryUploadLifetime_t R_ScenePackets_UploadLifetimeForCache( const vertCache_t *ambientCache, const vertCache_t *indexCache, const srfTriangles_t *geo ) {
	if ( ( ambientCache != NULL && ambientCache->tag == TAG_TEMP ) || ( indexCache != NULL && indexCache->tag == TAG_TEMP ) ) {
		return GEOMETRY_UPLOAD_LIFETIME_FRAME_TEMP;
	}
	if ( geo != NULL && ( geo->deformedSurface || geo->tempAmbientCache ) ) {
		return GEOMETRY_UPLOAD_LIFETIME_DYNAMIC_BRIDGE;
	}
	if ( ambientCache != NULL || indexCache != NULL ) {
		return GEOMETRY_UPLOAD_LIFETIME_STATIC;
	}
	if ( geo != NULL && ( geo->verts != NULL || geo->indexes != NULL ) ) {
		return GEOMETRY_UPLOAD_LIFETIME_CLIENT_MEMORY;
	}
	return GEOMETRY_UPLOAD_LIFETIME_UNKNOWN;
}

static int R_ScenePackets_InstanceVisibilityFlags( const drawSurf_t *drawSurf, scenePacketCategory_t packetCategory, bool legacyBridge ) {
	int flags = legacyBridge ? INSTANCE_VISIBILITY_LEGACY_BRIDGE : INSTANCE_VISIBILITY_NONE;
	switch ( packetCategory ) {
	case SCENE_PACKET_CATEGORY_GUI:
		flags |= INSTANCE_VISIBILITY_GUI;
		break;
	case SCENE_PACKET_CATEGORY_VIEWMODEL:
		flags |= INSTANCE_VISIBILITY_VIEWMODEL;
		break;
	case SCENE_PACKET_CATEGORY_SUBVIEW:
		flags |= INSTANCE_VISIBILITY_SUBVIEW;
		break;
	case SCENE_PACKET_CATEGORY_REMOTE_CAMERA:
		flags |= INSTANCE_VISIBILITY_REMOTE_CAMERA;
		break;
	case SCENE_PACKET_CATEGORY_RENDER_DEMO:
		flags |= INSTANCE_VISIBILITY_RENDER_DEMO;
		break;
	case SCENE_PACKET_CATEGORY_WORLD:
	default:
		flags |= INSTANCE_VISIBILITY_WORLD;
		break;
	}
	if ( drawSurf != NULL && drawSurf->space != NULL && drawSurf->space->weaponDepthHack ) {
		flags |= INSTANCE_VISIBILITY_VIEWMODEL;
	}
	return flags;
}

static unsigned int R_ScenePackets_LegacySortOrdinal( const drawSurf_t *drawSurf ) {
	float legacySort = drawSurf ? drawSurf->sort : 0.0f;
	unsigned int bits = 0;
	memcpy( &bits, &legacySort, sizeof( bits ) );
	if ( ( bits & 0x80000000u ) != 0 ) {
		bits = ~bits;
	} else {
		bits ^= 0x80000000u;
	}
	return bits;
}

static bool R_ScenePackets_PassUsesLegacySort( renderPassCategory_t category ) {
	switch ( category ) {
	case RENDER_PASS_DEPTH:
	case RENDER_PASS_LIGHT_GRID:
	case RENDER_PASS_AMBIENT:
	case RENDER_PASS_AUTHORED_POST:
		return true;
	default:
		return false;
	}
}

static unsigned long long R_ScenePackets_BuildSortKey( const drawSurf_t *drawSurf, renderPassCategory_t category, int drawIndex ) {
	const unsigned long long categoryBits = static_cast<unsigned long long>( category & 0xff ) << 56;
	const unsigned long long stableIndexBits = static_cast<unsigned int>( drawIndex ) & 0x00ffffffu;
	if ( !R_ScenePackets_PassUsesLegacySort( category ) ) {
		return categoryBits | stableIndexBits;
	}
	const unsigned long long sortBits = static_cast<unsigned long long>( R_ScenePackets_LegacySortOrdinal( drawSurf ) ) << 24;
	return categoryBits | sortBits | stableIndexBits;
}

int idScenePacketFrame::FindOrAddMaterialRecord( const drawSurf_t *drawSurf ) {
	if ( drawSurf == NULL || drawSurf->material == NULL ) {
		return -1;
	}

	const idMaterial *material = drawSurf->material;
	const int lookupSlot = R_ScenePackets_PreparedLookupSlot( this );
	if ( lookupSlot >= 0 ) {
		const idHashIndex &lookup = rg_scenePacketMaterialLookup[lookupSlot];
		for ( int i = lookup.First( R_ScenePackets_PointerLookupKey( material ) ); i >= 0; i = lookup.Next( i ) ) {
			if ( materialRecords[i].material == material ) {
				return i;
			}
		}
	} else {
		for ( int i = 0; i < stats.materialRecords; ++i ) {
			if ( materialRecords[i].material == material ) {
				return i;
			}
		}
	}

	if ( stats.materialRecords >= SCENE_PACKET_MAX_MATERIAL_RECORDS ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_MATERIALS );
		return -1;
	}

	const int recordIndex = stats.materialRecords++;
	if ( lookupSlot >= 0 ) {
		rg_scenePacketMaterialLookup[lookupSlot].Add( R_ScenePackets_PointerLookupKey( material ), recordIndex );
	}
	materialResourceRecord_t &record = materialRecords[recordIndex];
	memset( &record, 0, sizeof( record ) );
	record.material = material;
	record.diffuseImage = R_ScenePackets_FirstStageImage( material, SL_DIFFUSE );
	if ( record.diffuseImage == NULL ) {
		record.diffuseImage = R_ScenePackets_FirstAmbientImage( material );
	}
	record.normalImage = R_ScenePackets_FirstStageImage( material, SL_BUMP );
	record.specularImage = R_ScenePackets_FirstStageImage( material, SL_SPECULAR );
	if ( material->HasPBR() ) {
		const pbrMaterialInfo_t &pbr = material->GetPBRInfo();
		record.hasPBR = true;
		record.pbrWorkflow = static_cast<int>( pbr.workflow );
		record.pbrNormalFormat = static_cast<int>( pbr.normalFormat );
		record.pbrAlbedoImage = pbr.albedo.present ? pbr.albedo.image : NULL;
		record.pbrNormalImage = pbr.normal.present ? pbr.normal.image : NULL;
		record.pbrORMImage = pbr.orm.present ? pbr.orm.image : NULL;
		record.pbrMetallicImage = pbr.metallic.present ? pbr.metallic.image : NULL;
		record.pbrRoughnessImage = pbr.roughness.present ? pbr.roughness.image : NULL;
		record.pbrAOImage = pbr.ao.present ? pbr.ao.image : NULL;
		record.pbrEmissiveImage = pbr.emissive.present ? pbr.emissive.image : NULL;
		record.pbrHasAuthoredClassicFallback = pbr.hasAuthoredClassicFallback;
		record.pbrHasExplicitLegacyFallback = pbr.hasExplicitLegacyFallback;
		record.pbrUsesGeneratedLegacyFallback = pbr.usesGeneratedLegacyFallback;
		record.pbrUsesApproximateLegacyFallback = pbr.usesApproximateLegacyFallback;
		record.pbrLegacyFallbackMissing = pbr.legacyFallbackMissing;
		record.pbrMetallicRegister = pbr.metallicRegister;
		record.pbrRoughnessRegister = pbr.roughnessRegister;
		record.pbrAORegister = pbr.aoRegister;
		record.pbrNormalScaleRegister = pbr.normalScaleRegister;
		memcpy( record.pbrEmissiveColorRegisters, pbr.emissiveColorRegisters, sizeof( record.pbrEmissiveColorRegisters ) );
	}
	record.resourceTableIndex = material->Index();
	record.usesRemoteRender = R_ScenePackets_MaterialUsesRemoteRender( material );
	record.permutation.materialClass = R_ScenePackets_MaterialClassForDrawSurf( drawSurf );
	record.permutation.lightingMode =
		( material->IsDrawn() ? 1u : 0u ) |
		( material->HasAmbient() ? 2u : 0u ) |
		( material->ReceivesLighting() ? 4u : 0u ) |
		( material->ReceivesFog() ? 8u : 0u );
	const bool materialCastsRenderShadow = !material->IsDedicatedCollisionSurface() && material->SurfaceCastsShadow();
	record.permutation.shadowMode =
		( materialCastsRenderShadow ? 1u : 0u ) |
		( material->TestMaterialFlag( MF_NOSELFSHADOW ) ? 2u : 0u ) |
		( material->TestMaterialFlag( MF_FORCESHADOWS ) ? 4u : 0u );
	record.permutation.alphaMode = material->Coverage();
	record.permutation.skinningMode =
		R_ScenePackets_DrawSurfSkinningMode( drawSurf ) != GEOMETRY_SKINNING_NONE ? 1u : 0u;
	// srfTriangles_t::deformedSurface is generated-model topology ownership,
	// not evidence of material deformation. Per-draw execution provenance is
	// sealed separately in classicDeformRecord_t.
	record.permutation.deformMode = material->Deform() != DFRM_NONE ? 1u : 0u;
	record.permutation.lightGridMode =
		( material->ReceivesLighting() ? 1u : 0u ) |
		( material->HasAmbient() ? 2u : 0u );
	record.permutation.fogMode = material->ReceivesFog() ? 1u : 0u;
	record.permutation.debugMode = 0u;
	record.permutation.tier = glConfig.rendererTier;
	return recordIndex;
}

int idScenePacketFrame::FindOrAddGeometryRecord( const drawSurf_t *drawSurf,
		const classicDeformRecord_t &classicDeform ) {
	if ( drawSurf == NULL || drawSurf->geo == NULL ) {
		return -1;
	}

	const srfTriangles_t *geo = drawSurf->geo;
	const int lookupKey = R_ScenePackets_GeometryLookupKey( geo, classicDeform );
	const int lookupSlot = R_ScenePackets_PreparedLookupSlot( this );
	if ( lookupSlot >= 0 ) {
		const idHashIndex &lookup = rg_scenePacketGeometryLookup[lookupSlot];
		for ( int i = lookup.First( lookupKey ); i >= 0; i = lookup.Next( i ) ) {
			if ( geometryRecords[i].legacyGeometry == geo
					&& R_ClassicDeformDomain_SameProvenance(
						geometryRecords[i].classicDeform, classicDeform ) ) {
				return i;
			}
		}
	} else {
		for ( int i = 0; i < stats.geometryRecords; ++i ) {
			if ( geometryRecords[i].legacyGeometry == geo
					&& R_ClassicDeformDomain_SameProvenance(
						geometryRecords[i].classicDeform, classicDeform ) ) {
				return i;
			}
		}
	}

	if ( stats.geometryRecords >= SCENE_PACKET_MAX_GEOMETRY_RECORDS ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_GEOMETRY_RECORDS );
		return -1;
	}

	const int recordIndex = stats.geometryRecords++;
	if ( lookupSlot >= 0 ) {
		rg_scenePacketGeometryLookup[lookupSlot].Add( lookupKey, recordIndex );
	}
	geometryResourceRecord_t &record = geometryRecords[recordIndex];
	memset( &record, 0, sizeof( record ) );
	record.legacyGeometry = geo;
	record.bounds = geo->bounds;
	record.recordIndex = recordIndex;
	record.vertexCount = geo->numVerts;
	record.indexCount = geo->numIndexes;
	record.firstVertex = 0;
	record.firstIndex = 0;
	record.vertexStride = sizeof( idDrawVert );
	record.indexType = GL_INDEX_TYPE;
	record.classicDeform = classicDeform;
	record.hasClassicDeformRecord =
		R_ClassicDeformDomain_ValidateRecordForFrame( classicDeform,
			classicDeform.frameToken );
	record.skinningMode = R_ScenePackets_DrawSurfSkinningMode(
		drawSurf, &record.gpuSkinningSurface );
	record.hasGpuSkinningContract = record.skinningMode == GEOMETRY_SKINNING_GPU_PALETTE
		&& record.gpuSkinningSurface.fallbackReason == GPU_SKINNING_FALLBACK_NONE
		&& record.gpuSkinningSurface.bindPoseVerts != NULL;
	// Generated/dynamic topology remains an explicit surface-ownership class;
	// it is never evidence that a material deform executed. Material deformation
	// is classified only from the sealed per-draw record.
	record.deformMode = geo->deformedSurface
		? GEOMETRY_DEFORM_SURFACE
		: ( classicDeform.kind != CLASSIC_DEFORM_KIND_NONE
			? GEOMETRY_DEFORM_MATERIAL : GEOMETRY_DEFORM_NONE );
	record.uploadLifetime = R_ScenePackets_UploadLifetimeForCache( geo->ambientCache, geo->indexCache, geo );
	record.fallbackReason = GEOMETRY_RESOURCE_FALLBACK_NONE;
	record.skinningPaletteOffset = record.hasGpuSkinningContract
		? static_cast<int>( record.gpuSkinningSurface.palette.buffer.offsetBytes ) : 0;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	record.skinningPaletteCount = record.hasGpuSkinningContract
		? record.gpuSkinningSurface.palette.numJoints : geo->numSkinToModelTransforms;
	record.hasPrimBatchMesh = geo->primBatchMesh != NULL;
#else
	record.skinningPaletteCount = 0;
	record.hasPrimBatchMesh = false;
#endif
	record.legacyIndexData = geo->indexes;
	record.hasClientIndexData = geo->indexes != NULL;
	record.hasBounds = true;
	if ( geo->ambientCache != NULL ) {
		record.ambientVertexBuffer = geo->ambientCache->vbo;
		record.ambientCacheOffset = geo->ambientCache->offset;
		record.ambientCacheBytes = geo->ambientCache->size;
		record.hasAmbientVertexBuffer = geo->ambientCache->vbo != 0 && !geo->ambientCache->indexBuffer;
	}
	if ( geo->indexCache != NULL ) {
		record.indexBuffer = geo->indexCache->vbo;
		record.indexCacheOffset = geo->indexCache->offset;
		record.indexCacheBytes = geo->indexCache->size;
		record.hasIndexBuffer = geo->indexCache->vbo != 0 && geo->indexCache->indexBuffer;
	}
	// GEOMETRY_DEFORM_SURFACE is admitted: the front end already ran the deform,
	// so the cache holds finished geometry at the normal idDrawVert stride and
	// the ordinary vertex/index binding covers it. Only GEOMETRY_DEFORM_MATERIAL
	// (a deform the material still has to execute) stays unsupported.
	//
	// Admitting it originally produced ~966 GL_INVALID_OPERATION per frame on
	// game/storage1, which read like a vertex-binding problem but was not. With
	// a debug context the driver named it: "State(s) are invalid: program
	// texture usage". These surfaces are the first to reach the transparent-
	// forward and clustered shadow-sampling programs, whose samplerCube shadow
	// uniforms sat on the same texture units as the sampler2D material texture
	// table. GL fails any draw whose active samplers disagree on a unit's type,
	// so the fix was to make those unit ranges disjoint (see
	// MODERN_GL_SHADOW_TEXTURE_UNIT_FIRST in ModernGLShaderLibrary.h), not to
	// change anything about the geometry these records describe.
	if ( record.deformMode == GEOMETRY_DEFORM_MATERIAL ) {
		R_ScenePackets_AddGeometryFallback(
			record,
			GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_DEFORM,
			GEOMETRY_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_DEFORM );
	}
	if ( record.skinningMode == GEOMETRY_SKINNING_GPU_PALETTE
		&& !record.hasGpuSkinningContract ) {
		R_ScenePackets_AddGeometryFallback(
			record,
			GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_GPU_SKINNING,
			GEOMETRY_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_GPU_SKINNING );
	}
	if ( !record.hasAmbientVertexBuffer ) {
		R_ScenePackets_AddGeometryFallback(
			record,
			GEOMETRY_RESOURCE_FALLBACK_MISSING_VERTEX_BUFFER,
			GEOMETRY_RESOURCE_FALLBACK_FLAG_MISSING_VERTEX_BUFFER );
	}
	if ( record.indexCount > 0 && !record.hasIndexBuffer && record.legacyIndexData == NULL ) {
		R_ScenePackets_AddGeometryFallback(
			record,
			GEOMETRY_RESOURCE_FALLBACK_MISSING_INDEX_DATA,
			GEOMETRY_RESOURCE_FALLBACK_FLAG_MISSING_INDEX_DATA );
	}
	return recordIndex;
}

int idScenePacketFrame::FindOrAddInstanceRecord( const drawSurf_t *drawSurf, scenePacketCategory_t packetCategory ) {
	if ( drawSurf == NULL || drawSurf->space == NULL ) {
		return -1;
	}

	const viewEntity_t *space = drawSurf->space;
	const float *shaderRegisters = drawSurf->shaderRegisters;
	const int lookupSlot = R_ScenePackets_PreparedLookupSlot( this );
	const int lookupKey = static_cast<int>( static_cast<unsigned int>( R_ScenePackets_PointerLookupKey( space ) )
		+ static_cast<unsigned int>( R_ScenePackets_PointerLookupKey( shaderRegisters ) ) );
	if ( lookupSlot >= 0 ) {
		const idHashIndex &lookup = rg_scenePacketInstanceLookup[lookupSlot];
		for ( int i = lookup.First( lookupKey ); i >= 0; i = lookup.Next( i ) ) {
			if ( instanceRecords[i].legacySpace == space && instanceRecords[i].legacyShaderRegisters == shaderRegisters ) {
				return i;
			}
		}
	} else {
		for ( int i = 0; i < stats.instanceRecords; ++i ) {
			if ( instanceRecords[i].legacySpace == space && instanceRecords[i].legacyShaderRegisters == shaderRegisters ) {
				return i;
			}
		}
	}

	if ( stats.instanceRecords >= SCENE_PACKET_MAX_INSTANCE_RECORDS ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_INSTANCE_RECORDS );
		return -1;
	}

	const int recordIndex = stats.instanceRecords++;
	if ( lookupSlot >= 0 ) {
		rg_scenePacketInstanceLookup[lookupSlot].Add( lookupKey, recordIndex );
	}
	instanceRecord_t &record = instanceRecords[recordIndex];
	memset( &record, 0, sizeof( record ) );
	record.legacySpace = space;
	record.legacyShaderRegisters = shaderRegisters;
	record.recordIndex = recordIndex;
	record.entityIndex = ( space->entityDef != NULL ) ? space->entityDef->index : -1;
	record.shaderRegisterBase = shaderRegisters != NULL ? 0 : -1;
	record.shaderRegisterCount = ( shaderRegisters != NULL && drawSurf->material != NULL ) ? drawSurf->material->GetNumRegisters() : 0;
	record.skinningPaletteOffset = 0;
	record.visibilityFlags = R_ScenePackets_InstanceVisibilityFlags( drawSurf, packetCategory, activeScene >= 0 && scenes[activeScene].legacyBridge );
	const viewDef_t *instanceView = activeScene >= 0
		? scenes[activeScene].viewDef : NULL;
	record.temporalViewIdentity = instanceView != NULL
		? ( instanceView->temporalViewIdentity != 0
			? instanceView->temporalViewIdentity
			: R_TemporalPresentation_ViewIdentity( instanceView ) ) : 0;
	record.temporalHistoryGeneration =
		R_TemporalPresentation_HistoryGeneration();
	record.temporalHistoryAgeFrames = 0;
	memcpy( record.modelMatrix, space->modelMatrix, sizeof( record.modelMatrix ) );
	record.hasPreviousModelMatrix = R_ScenePackets_FindTemporalInstanceHistory(
		record.temporalViewIdentity, record.entityIndex,
		record.previousModelMatrix, record.temporalHistoryAgeFrames );
	if ( !record.hasPreviousModelMatrix ) {
		memcpy( record.previousModelMatrix, space->modelMatrix,
			sizeof( record.previousModelMatrix ) );
	}
	memcpy( record.modelViewMatrix, space->modelViewMatrix, sizeof( record.modelViewMatrix ) );
	record.modelDepthHack = space->modelDepthHack;
	record.hasModelMatrix = true;
	record.hasShaderRegisters = shaderRegisters != NULL;
	record.weaponDepthHack = space->weaponDepthHack;
	record.negativeScale = R_ScenePackets_MatrixHasNegativeScale( space->modelMatrix );
	record.legacyBridge = activeScene >= 0 && scenes[activeScene].legacyBridge;
	record.temporalCaptureFrame = instanceView != NULL
		&& instanceView->temporalCaptureFrame;
	if ( shaderRegisters != NULL ) {
		record.entityColor[0] = shaderRegisters[EXP_REG_PARM0];
		record.entityColor[1] = shaderRegisters[EXP_REG_PARM1];
		record.entityColor[2] = shaderRegisters[EXP_REG_PARM2];
		record.entityColor[3] = shaderRegisters[EXP_REG_PARM3];
	} else if ( space->entityDef != NULL ) {
		record.entityColor[0] = space->entityDef->parms.shaderParms[SHADERPARM_RED];
		record.entityColor[1] = space->entityDef->parms.shaderParms[SHADERPARM_GREEN];
		record.entityColor[2] = space->entityDef->parms.shaderParms[SHADERPARM_BLUE];
		record.entityColor[3] = space->entityDef->parms.shaderParms[SHADERPARM_ALPHA];
	} else {
		record.entityColor[0] = 1.0f;
		record.entityColor[1] = 1.0f;
		record.entityColor[2] = 1.0f;
		record.entityColor[3] = 1.0f;
	}
	return recordIndex;
}

bool idScenePacketFrame::AddScene( const viewDef_t *viewDef, bool legacyBridge ) {
	if ( stats.scenePackets >= SCENE_PACKET_MAX_SCENES ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_SCENES );
		activeScene = -1;
		activePass = -1;
		return false;
	}

	scenePacket_t &scene = scenes[stats.scenePackets++];
	memset( &scene, 0, sizeof( scene ) );
	scene.viewDef = viewDef;
	scene.renderDemoPlayback = R_ScenePackets_IsRenderDemoPlaybackView( viewDef );
	scene.packetCategory = scene.renderDemoPlayback ? SCENE_PACKET_CATEGORY_RENDER_DEMO :
		( viewDef != NULL && viewDef->isSubview ? SCENE_PACKET_CATEGORY_SUBVIEW :
		( viewDef != NULL && viewDef->renderView.viewID < 0 ? SCENE_PACKET_CATEGORY_RENDER_DEMO : SCENE_PACKET_CATEGORY_WORLD ) );
	scene.firstPassPacket = stats.passPackets;
	scene.firstDrawPacket = stats.drawPackets;
	scene.legacyBridge = legacyBridge;
	if ( viewDef != NULL ) {
		scene.temporalViewIdentity = viewDef->temporalViewIdentity != 0
			? viewDef->temporalViewIdentity
			: R_TemporalPresentation_ViewIdentity( viewDef );
		scene.temporalHistoryGeneration = viewDef->temporalHistoryGeneration;
		scene.temporalJitterIndex = viewDef->temporalJitterIndex;
		scene.temporalHistoryResetReason =
			viewDef->temporalHistoryResetReason;
		scene.temporalHistoryValid = viewDef->temporalHistoryValid;
		scene.temporalJitterEnabled = viewDef->temporalJitterEnabled;
		scene.temporalCaptureFrame = viewDef->temporalCaptureFrame;
	}
	activeScene = stats.scenePackets - 1;
	activePass = -1;
	activePassLastSortKey = 0;
	activePassSortKeyValid = false;
	return true;
}

bool idScenePacketFrame::AddPass( renderPassCategory_t category, bool enabled, bool commandOnly ) {
	if ( activeScene < 0 ) {
		if ( !AddScene( NULL, true ) ) {
			return false;
		}
	}
	if ( stats.passPackets >= SCENE_PACKET_MAX_PASSES ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_PASSES );
		activePass = -1;
		return false;
	}

	passPacket_t &pass = passes[stats.passPackets++];
	memset( &pass, 0, sizeof( pass ) );
	pass.passCategory = category;
	pass.packetCategory = R_ScenePackets_CategoryForCommandPass( category, activeScene >= 0 ? scenes[activeScene].viewDef : NULL );
	pass.firstDrawPacket = stats.drawPackets;
	pass.enabled = enabled;
	pass.commandOnly = commandOnly;
	activePass = stats.passPackets - 1;
	activePassLastSortKey = 0;
	activePassSortKeyValid = false;
	scenes[activeScene].passPacketCount++;
	if ( commandOnly ) {
		CountCategory( pass.packetCategory );
	}
	return true;
}

bool idScenePacketFrame::AddDrawPacket( const drawSurf_t *drawSurf,
		renderPassCategory_t category, int drawIndex,
		classicDeformRole_t deformRole ) {
	if ( activePass < 0 ) {
		if ( !AddPass( category, true ) ) {
			return false;
		}
	}
	if ( stats.drawPackets >= SCENE_PACKET_MAX_DRAWS ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_DRAWS );
		stats.clippedDrawPackets++;
		return false;
	}

	drawPacket_t &packet = drawPackets[stats.drawPackets++];
	memset( &packet, 0, sizeof( packet ) );
	const int materialRecordIndex = FindOrAddMaterialRecord( drawSurf );
	const materialResourceRecord_t *cachedMaterialRecord =
		materialRecordIndex >= 0 ? &materialRecords[materialRecordIndex] : NULL;
	const scenePacketCategory_t packetCategory = R_ScenePackets_CategoryForDrawSurf( activeScene >= 0 ? scenes[activeScene].viewDef : NULL, drawSurf, category, cachedMaterialRecord );
	classicDeformRecord_t classicDeform;
	const std::uint64_t deformFrameToken =
		R_ClassicDeformDomain_CurrentFrameToken();
	if ( deformRole == CLASSIC_DEFORM_ROLE_UNKNOWN ) {
		deformRole = R_ScenePackets_ClassicDeformRoleForPass( category );
	}
	R_ClassicDeformDomain_SnapshotDrawSurf( drawSurf,
		deformRole, deformFrameToken, classicDeform );
	const int geometryRecordIndex = FindOrAddGeometryRecord(
		drawSurf, classicDeform );
	const int instanceRecordIndex = FindOrAddInstanceRecord( drawSurf, packetCategory );
	packet.legacyDrawSurf = drawSurf;
	packet.viewDef = activeScene >= 0 ? scenes[activeScene].viewDef : NULL;
	packet.space = drawSurf ? drawSurf->space : NULL;
	packet.materialRecord = materialRecordIndex >= 0 ? &materialRecords[materialRecordIndex] : NULL;
	packet.geometryRecord = geometryRecordIndex >= 0 ? &geometryRecords[geometryRecordIndex] : NULL;
	packet.instanceRecord = instanceRecordIndex >= 0 ? &instanceRecords[instanceRecordIndex] : NULL;
	packet.sortKey.value = R_ScenePackets_BuildSortKey( drawSurf, category, drawIndex );
	packet.passCategory = category;
	packet.packetCategory = packetCategory;
	packet.legacySort = drawSurf ? drawSurf->sort : 0.0f;
	packet.materialRecordIndex = materialRecordIndex;
	packet.geometryRecordIndex = geometryRecordIndex;
	packet.instanceRecordIndex = instanceRecordIndex;
	packet.vertexCount = ( drawSurf && drawSurf->geo ) ? drawSurf->geo->numVerts : 0;
	packet.firstIndex = 0;
	packet.indexCount = ( drawSurf && drawSurf->geo ) ? drawSurf->geo->numIndexes : 0;
	packet.vertexOffset = 0;
	packet.instanceOffset = 0;
	packet.instanceCount = drawSurf ? 1 : 0;
	temporalMotionInput_t temporalInput = {};
	temporalInput.hasEntity = packet.instanceRecord != NULL
		&& packet.instanceRecord->entityIndex >= 0;
	temporalInput.hasPreviousTransform = packet.instanceRecord != NULL
		&& packet.instanceRecord->hasPreviousModelMatrix;
	temporalInput.skinned = packet.geometryRecord != NULL
		&& packet.geometryRecord->skinningMode != GEOMETRY_SKINNING_NONE;
	// Previous joint palettes and previous material-deformed vertices are not
	// silently approximated as rigid motion. Until a backend consumes those
	// explicit streams, the shared policy marks the surfaces reactive.
	temporalInput.hasPreviousSkinningPalette = false;
	temporalInput.particle = drawSurf != NULL
		&& ( drawSurf->dsFlags & DSF_BSE_EFFECT ) != 0;
	temporalInput.deform = packet.geometryRecord != NULL
		&& packet.geometryRecord->deformMode != GEOMETRY_DEFORM_NONE;
	temporalInput.hasPreviousDeformedVertices = false;
	temporalInput.subview = cachedMaterialRecord != NULL
		? ( cachedMaterialRecord->usesRemoteRender
			|| static_cast<rendererMaterialClass_t>(
				cachedMaterialRecord->permutation.materialClass )
				== RENDER_MATERIAL_SUBVIEW )
		: ( drawSurf != NULL
			&& ( R_ScenePackets_MaterialUsesRemoteRender( drawSurf->material )
				|| R_ScenePackets_MaterialClassForDrawSurf( drawSurf )
					== RENDER_MATERIAL_SUBVIEW ) );
	temporalInput.inWorldGui = drawSurf != NULL
		&& ( drawSurf->dsFlags & DSF_IN_WORLD_GUI ) != 0;
	temporalInput.viewModel = drawSurf != NULL && drawSurf->space != NULL
		&& drawSurf->space->weaponDepthHack;
	temporalInput.translucent = drawSurf != NULL && drawSurf->material != NULL
		&& drawSurf->material->Coverage() != MC_OPAQUE;
	packet.temporalMotion = TemporalHistoryCore_ClassifyMotion( temporalInput );
	packet.temporalExactRigidEligible =
		packet.temporalMotion.domain == TEMPORAL_MOTION_DOMAIN_RIGID
		&& R_ScenePackets_TemporalRigidMotionEligible( drawSurf );
	if ( activeScene >= 0 && scenes[activeScene].viewDef != NULL
			&& scenes[activeScene].viewDef->isSubview ) {
		packet.temporalMotion.flags |=
			TEMPORAL_MOTION_OWNERSHIP_SEPARATE_HISTORY;
	}
	packet.classicDeformRecord = packet.geometryRecord != NULL
		? &packet.geometryRecord->classicDeform : NULL;
	packet.hasClassicDeformRecord = packet.classicDeformRecord != NULL
		&& packet.geometryRecord->hasClassicDeformRecord
		&& R_ClassicDeformDomain_ValidateRecordForFrame(
			*packet.classicDeformRecord, deformFrameToken )
		&& R_ClassicDeformDomain_SameProvenance(
			*packet.classicDeformRecord, classicDeform );
	R_ClassicDeformDomain_RecordPacket( classicDeform );
	if ( drawSurf != NULL ) {
		packet.scissorX1 = drawSurf->scissorRect.x1;
		packet.scissorY1 = drawSurf->scissorRect.y1;
		packet.scissorX2 = drawSurf->scissorRect.x2;
		packet.scissorY2 = drawSurf->scissorRect.y2;
	}
	packet.hasGeometry = drawSurf != NULL && drawSurf->geo != NULL && drawSurf->geo->numIndexes > 0;
	packet.hasShaderRegisters = drawSurf != NULL && drawSurf->shaderRegisters != NULL;
	packet.hasIndexCache = drawSurf != NULL && drawSurf->geo != NULL && drawSurf->geo->indexCache != NULL;
	packet.hasAmbientCache = drawSurf != NULL && drawSurf->geo != NULL && drawSurf->geo->ambientCache != NULL;

	if ( activePassSortKeyValid && packet.sortKey.value < activePassLastSortKey ) {
		stats.sortKeyValidationFailures++;
	}
	activePassLastSortKey = packet.sortKey.value;
	activePassSortKeyValid = true;
	passes[activePass].packetCategory = packetCategory;
	passes[activePass].drawPacketCount++;
	scenes[activeScene].drawPacketCount++;
	CountCategory( packetCategory );
	if ( packet.temporalMotion.domain >= TEMPORAL_MOTION_DOMAIN_STATIC_WORLD
			&& packet.temporalMotion.domain < TEMPORAL_MOTION_DOMAIN_COUNT ) {
		stats.temporalMotionDomainPackets[packet.temporalMotion.domain]++;
	}
	if ( ( packet.temporalMotion.flags
			& TEMPORAL_MOTION_OWNERSHIP_REACTIVE ) != 0 ) {
		stats.temporalReactivePackets++;
	}
	if ( ( packet.temporalMotion.flags
			& TEMPORAL_MOTION_OWNERSHIP_HAS_PREVIOUS_TRANSFORM ) != 0 ) {
		stats.temporalPreviousTransformPackets++;
	}
	if ( ( packet.temporalMotion.flags
			& TEMPORAL_MOTION_OWNERSHIP_SEPARATE_HISTORY ) != 0 ) {
		stats.temporalSeparateHistoryPackets++;
	}
	if ( drawSurf != NULL && drawSurf->material != NULL ) {
		stats.drawPacketsWithMaterial++;
	}
	if ( packet.materialRecord != NULL ) {
		stats.drawPacketsWithResourceRecord++;
	}
	if ( packet.geometryRecord != NULL ) {
		stats.drawPacketsWithGeometryRecord++;
	}
	if ( packet.instanceRecord != NULL ) {
		stats.drawPacketsWithInstanceRecord++;
	}
	if ( packet.hasGeometry ) {
		stats.drawPacketsWithGeometry++;
	}
	if ( packet.hasShaderRegisters ) {
		stats.drawPacketsWithShaderRegisters++;
	}
	if ( packet.hasIndexCache ) {
		stats.drawPacketsWithIndexCache++;
	}
	if ( packet.hasAmbientCache ) {
		stats.drawPacketsWithAmbientCache++;
	}
	if ( packet.hasClassicDeformRecord ) {
		stats.drawPacketsWithClassicDeformRecord++;
	}
	if ( classicDeform.kind != CLASSIC_DEFORM_KIND_NONE ) {
		stats.materialDeformDrawPackets++;
		switch ( classicDeform.role ) {
		case CLASSIC_DEFORM_ROLE_FINALIZED_DRAW:
			stats.deformFinalizedDrawPackets++;
			break;
		case CLASSIC_DEFORM_ROLE_INTERACTION_RECEIVER:
			stats.deformInteractionReceiverPackets++;
			break;
		case CLASSIC_DEFORM_ROLE_FOG_RECEIVER:
			stats.deformFogReceiverPackets++;
			break;
		case CLASSIC_DEFORM_ROLE_SHADOW_VOLUME:
			stats.deformShadowVolumePackets++;
			break;
		default:
			stats.deformOtherRolePackets++;
			break;
		}
		switch ( classicDeform.outcome ) {
		case CLASSIC_DEFORM_OUTCOME_COMPLETED:
			stats.deformCompletedPackets++;
			break;
		case CLASSIC_DEFORM_OUTCOME_EMPTY:
			stats.deformEmptyPackets++;
			break;
		case CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE:
			stats.deformNotApplicablePackets++;
			break;
		case CLASSIC_DEFORM_OUTCOME_SKIPPED:
			stats.deformSkippedPackets++;
			break;
		case CLASSIC_DEFORM_OUTCOME_FAILED:
			stats.deformFailedPackets++;
			break;
		case CLASSIC_DEFORM_OUTCOME_UNSUPPORTED:
			stats.deformUnsupportedPackets++;
			break;
		default:
			break;
		}
		if ( ( packet.geometryRecord != NULL
					&& ( packet.geometryRecord->fallbackFlags
						& GEOMETRY_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_DEFORM ) != 0 )
				|| R_ClassicDeformDomain_IsFailClosed( classicDeform ) ) {
			stats.deformFallbackPackets++;
		}
	}
	return true;
}

void idScenePacketFrame::SetLastSceneSpecialEffectsMask(
		int specialEffectsMask ) {
	const int sceneIndex = stats.scenePackets - 1;
	if ( sceneIndex < 0 || sceneIndex >= stats.scenePackets ) {
		return;
	}
	scenes[ sceneIndex ].specialEffectsMask = specialEffectsMask
		& ( SPECIAL_EFFECT_BLUR | SPECIAL_EFFECT_AL );
}

bool idScenePacketFrame::AddInteractionDrawPacket( const drawSurf_t *drawSurf,
		int drawIndex, const viewLight_t *viewLight, int lightOrdinal,
		sceneInteractionReceiverClass_t receiverClass, int receiverOrdinal ) {
	if ( !AddDrawPacket( drawSurf, RENDER_PASS_ARB2_INTERACTION, drawIndex ) ) {
		return false;
	}
	drawPacket_t &packet = drawPackets[stats.drawPackets - 1];
	packet.interactionLight = viewLight;
	packet.interactionLightOrdinal = lightOrdinal;
	packet.interactionReceiverClass = receiverClass;
	packet.interactionReceiverOrdinal = receiverOrdinal;
	packet.interactionSourceOrdinal = drawIndex;
	return true;
}

bool idScenePacketFrame::AddFogBlendDrawPacket( const drawSurf_t *drawSurf,
		int drawIndex, const viewLight_t *viewLight, int lightOrdinal,
		sceneFogBlendReceiverClass_t receiverClass, int receiverOrdinal ) {
	if ( receiverClass <= SCENE_FOG_BLEND_RECEIVER_NONE
			|| receiverClass >= SCENE_FOG_BLEND_RECEIVER_COUNT
			|| !AddDrawPacket( drawSurf, RENDER_PASS_FOG_BLEND, drawIndex ) ) {
		return false;
	}
	drawPacket_t &packet = drawPackets[stats.drawPackets - 1];
	packet.fogBlendLight = viewLight;
	packet.fogBlendLightOrdinal = lightOrdinal;
	packet.fogBlendReceiverClass = receiverClass;
	packet.fogBlendReceiverOrdinal = receiverOrdinal;
	packet.fogBlendSourceOrdinal = drawIndex;
	return true;
}

bool idScenePacketFrame::AddShadowDrawPacket( const drawSurf_t *drawSurf,
		renderPassCategory_t category, int drawIndex,
		const viewLight_t *viewLight, int lightOrdinal,
		sceneShadowCasterClass_t casterClass, int chainOrdinal ) {
	if ( ( category != RENDER_PASS_STENCIL_SHADOW
			&& category != RENDER_PASS_SHADOW_MAP )
			|| casterClass <= SCENE_SHADOW_CASTER_NONE
			|| casterClass >= SCENE_SHADOW_CASTER_COUNT ) {
		return false;
	}
	const bool stencilGeometry = category == RENDER_PASS_STENCIL_SHADOW
		|| casterClass == SCENE_SHADOW_CASTER_SUPPLEMENT_GLOBAL
		|| casterClass == SCENE_SHADOW_CASTER_SUPPLEMENT_LOCAL;
	if ( !AddDrawPacket( drawSurf, category, drawIndex,
			stencilGeometry ? CLASSIC_DEFORM_ROLE_SHADOW_VOLUME
				: CLASSIC_DEFORM_ROLE_FINALIZED_DRAW ) ) {
		return false;
	}
	drawPacket_t &packet = drawPackets[stats.drawPackets - 1];
	packet.shadowLight = viewLight;
	packet.shadowLightOrdinal = lightOrdinal;
	packet.shadowCasterClass = casterClass;
	packet.shadowChainOrdinal = chainOrdinal;
	packet.shadowSourceOrdinal = drawIndex;
	return true;
}

void idScenePacketFrame::FinishScene( void ) {
	activeScene = -1;
	activePass = -1;
	activePassLastSortKey = 0;
	activePassSortKeyValid = false;
}

void idScenePacketFrame::AddCommandPacket( scenePacketCategory_t category ) {
	stats.commandPackets++;
	CountCategory( category );
}

bool idScenePacketFrame::AddSubviewCapture( const viewDef_t *viewDef,
		idImage *image, int x, int y, int width, int height, int cubeFace,
		bool copyDepth ) {
	if ( viewDef == NULL || image == NULL || width <= 0 || height <= 0 ) {
		return false;
	}
	if ( stats.subviewCaptures >= SCENE_PACKET_MAX_SUBVIEW_CAPTURES ) {
		SetOverflow( SCENE_PACKET_OVERFLOW_SUBVIEW_CAPTURES );
		return false;
	}
	int scenePacketIndex = -1;
	for ( int i = stats.scenePackets - 1; i >= 0; --i ) {
		if ( scenes[ i ].viewDef == viewDef ) {
			scenePacketIndex = i;
			break;
		}
	}
	if ( scenePacketIndex < 0 ) {
		return false;
	}
	sceneSubviewCapture_t &capture = subviewCaptures[ stats.subviewCaptures++ ];
	memset( &capture, 0, sizeof( capture ) );
	capture.viewDef = viewDef;
	capture.image = image;
	capture.viewScenePacketIndex = scenePacketIndex;
	capture.x = x;
	capture.y = y;
	capture.width = width;
	capture.height = height;
	capture.cubeFace = cubeFace;
	capture.copyDepth = copyDepth;
	return true;
}

void idScenePacketFrame::AddLegacyDrawView( void ) {
	stats.legacyDrawViews++;
}

void idScenePacketFrame::AddClippedDrawPackets( int count ) {
	if ( count > 0 ) {
		stats.clippedDrawPackets += count;
		SetOverflow( SCENE_PACKET_OVERFLOW_DRAWS );
	}
}

void idScenePacketFrame::MarkFrontEndDerived( void ) {
	stats.frontEndDerived = true;
}

void idScenePacketFrame::MarkBackendDerived( void ) {
	stats.backendDerived = true;
}

int idScenePacketFrame::NumScenes( void ) const {
	return stats.scenePackets;
}

int idScenePacketFrame::NumPasses( void ) const {
	return stats.passPackets;
}

int idScenePacketFrame::NumDrawPackets( void ) const {
	return stats.drawPackets;
}

int idScenePacketFrame::NumMaterialRecords( void ) const {
	return stats.materialRecords;
}

int idScenePacketFrame::NumGeometryRecords( void ) const {
	return stats.geometryRecords;
}

int idScenePacketFrame::NumInstanceRecords( void ) const {
	return stats.instanceRecords;
}

const scenePacket_t &idScenePacketFrame::Scene( int index ) const {
	return scenes[index];
}

const passPacket_t &idScenePacketFrame::Pass( int index ) const {
	return passes[index];
}

const drawPacket_t &idScenePacketFrame::DrawPacket( int index ) const {
	return drawPackets[index];
}

const materialResourceRecord_t &idScenePacketFrame::MaterialRecord( int index ) const {
	return materialRecords[index];
}

const geometryResourceRecord_t &idScenePacketFrame::GeometryRecord( int index ) const {
	return geometryRecords[index];
}

const instanceRecord_t &idScenePacketFrame::InstanceRecord( int index ) const {
	return instanceRecords[index];
}

int idScenePacketFrame::NumSubviewCaptures( void ) const {
	return stats.subviewCaptures;
}

const sceneSubviewCapture_t &idScenePacketFrame::SubviewCapture( int index ) const {
	return subviewCaptures[index];
}

const scenePacketFrameStats_t &idScenePacketFrame::Stats( void ) const {
	return stats;
}

bool idScenePacketFrame::ValidateSortKeys( void ) const {
	for ( int passIndex = 0; passIndex < stats.passPackets; ++passIndex ) {
		const passPacket_t &pass = passes[passIndex];
		unsigned long long previous = 0;
		bool havePrevious = false;
		for ( int drawIndex = 0; drawIndex < pass.drawPacketCount; ++drawIndex ) {
			const int packetIndex = pass.firstDrawPacket + drawIndex;
			if ( packetIndex < 0 || packetIndex >= stats.drawPackets ) {
				return false;
			}
			const drawPacket_t &packet = drawPackets[packetIndex];
			if ( packet.passCategory != pass.passCategory ) {
				return false;
			}
			if ( havePrevious && packet.sortKey.value < previous ) {
				return false;
			}
			previous = packet.sortKey.value;
			havePrevious = true;
		}
	}
	return stats.sortKeyValidationFailures == 0;
}

static bool R_ScenePackets_TemporalVisiblePass(
		renderPassCategory_t category ) {
	switch ( category ) {
	case RENDER_PASS_ARB2_INTERACTION:
	case RENDER_PASS_LIGHT_GRID:
	case RENDER_PASS_AMBIENT:
	case RENDER_PASS_DEFERRED_RESOLVE:
	case RENDER_PASS_FORWARD_PLUS:
	case RENDER_PASS_FOG_BLEND:
	case RENDER_PASS_GUI:
		return true;
	default:
		// Depth, shadow, and fullscreen post packets are not independently
		// visible surfaces in the color image consumed by temporal resolve.
		return false;
	}
}

bool idScenePacketFrame::BuildTemporalViewMotionPolicy(
		const viewDef_t *viewDef, unsigned int backendExactMotionDomainMask,
		temporalViewMotionPolicy_t &policy ) const {
	policy = TemporalHistoryCore_BeginViewMotionPolicy();
	if ( viewDef == NULL ) {
		return false;
	}

	const int viewWidth = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int viewHeight = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	if ( viewWidth <= 0 || viewHeight <= 0 ) {
		return false;
	}
	const float inverseWidth = 1.0f / static_cast<float>( viewWidth );
	const float inverseHeight = 1.0f / static_cast<float>( viewHeight );
	bool foundScene = false;
	for ( int sceneIndex = 0; sceneIndex < stats.scenePackets; ++sceneIndex ) {
		const scenePacket_t &scene = scenes[sceneIndex];
		if ( scene.viewDef != viewDef ) {
			continue;
		}
		foundScene = true;
		for ( int drawIndex = 0; drawIndex < scene.drawPacketCount; ++drawIndex ) {
			const int packetIndex = scene.firstDrawPacket + drawIndex;
			if ( packetIndex < 0 || packetIndex >= stats.drawPackets ) {
				continue;
			}
			const drawPacket_t &packet = drawPackets[packetIndex];
			if ( !R_ScenePackets_TemporalVisiblePass( packet.passCategory )
					|| !packet.hasGeometry ) {
				continue;
			}
			unsigned int packetExactMotionDomainMask =
				backendExactMotionDomainMask;
			if ( packet.temporalMotion.domain == TEMPORAL_MOTION_DOMAIN_RIGID
					&& !packet.temporalExactRigidEligible ) {
				packetExactMotionDomainMask &=
					~TemporalHistoryCore_MotionDomainBit(
						TEMPORAL_MOTION_DOMAIN_RIGID );
			}
			TemporalHistoryCore_AddMotionOwnership( policy,
				packet.temporalMotion, packetExactMotionDomainMask,
				static_cast<float>( packet.scissorX1 ) * inverseWidth,
				static_cast<float>( packet.scissorY1 ) * inverseHeight,
				static_cast<float>( packet.scissorX2 + 1 ) * inverseWidth,
				static_cast<float>( packet.scissorY2 + 1 ) * inverseHeight );
		}
	}

	if ( foundScene && stats.overflow ) {
		// Packet clipping makes a partial policy unsafe. Reject history over the
		// full view for every object-motion domain rather than leaving clipped
		// particles, characters, or view models with unowned stale history.
		unsigned int overflowDomains = 0u;
		for ( int domain = TEMPORAL_MOTION_DOMAIN_RIGID;
				domain < TEMPORAL_MOTION_DOMAIN_COUNT; ++domain ) {
			overflowDomains |= TemporalHistoryCore_MotionDomainBit(
				static_cast<temporalMotionDomain_t>( domain ) );
		}
		policy.presentDomainMask |= overflowDomains;
		policy.reactiveDomainMask |= overflowDomains;
		TemporalHistoryCore_AddReactiveRegion( policy, overflowDomains,
			0.0f, 0.0f, 1.0f, 1.0f );
	}
	return foundScene;
}

const char *RenderPassCategory_Name( renderPassCategory_t category ) {
	switch ( category ) {
	case RENDER_PASS_DEPTH:
		return "depth";
	case RENDER_PASS_STENCIL_SHADOW:
		return "stencilShadow";
	case RENDER_PASS_SHADOW_MAP:
		return "shadowMap";
	case RENDER_PASS_ARB2_INTERACTION:
		return "arb2Interaction";
	case RENDER_PASS_LIGHT_GRID:
		return "lightGrid";
	case RENDER_PASS_AMBIENT:
		return "ambient";
	case RENDER_PASS_DEFERRED_RESOLVE:
		return "deferredResolve";
	case RENDER_PASS_FORWARD_PLUS:
		return "forwardPlus";
	case RENDER_PASS_FOG_BLEND:
		return "fogBlend";
	case RENDER_PASS_SSAO:
		return "ssao";
	case RENDER_PASS_MOTION_BLUR:
		return "motionBlur";
	case RENDER_PASS_BLOOM:
		return "bloom";
	case RENDER_PASS_AUTHORED_POST:
		return "authoredPost";
	case RENDER_PASS_SPECIAL_EFFECTS:
		return "specialEffects";
	case RENDER_PASS_GUI:
		return "gui";
	case RENDER_PASS_PRESENT:
		return "present";
	default:
		return "unknown";
	}
}

const char *ScenePacketCategory_Name( scenePacketCategory_t category ) {
	switch ( category ) {
	case SCENE_PACKET_CATEGORY_WORLD:
		return "world";
	case SCENE_PACKET_CATEGORY_SUBVIEW:
		return "subview";
	case SCENE_PACKET_CATEGORY_REMOTE_CAMERA:
		return "remoteCamera";
	case SCENE_PACKET_CATEGORY_SPECIAL_EFFECTS:
		return "specialEffects";
	case SCENE_PACKET_CATEGORY_VIEWMODEL:
		return "viewmodel";
	case SCENE_PACKET_CATEGORY_RENDER_DEMO:
		return "renderDemo";
	case SCENE_PACKET_CATEGORY_GUI:
		return "gui";
	case SCENE_PACKET_CATEGORY_POST_PROCESS:
		return "postProcess";
	case SCENE_PACKET_CATEGORY_PRESENT:
		return "present";
	case SCENE_PACKET_CATEGORY_COMMAND:
		return "command";
	case SCENE_PACKET_CATEGORY_UNKNOWN:
	default:
		return "unknown";
	}
}

const char *ScenePacketOverflowCause_Name( scenePacketOverflowCause_t cause ) {
	switch ( cause ) {
	case SCENE_PACKET_OVERFLOW_NONE:
		return "none";
	case SCENE_PACKET_OVERFLOW_SCENES:
		return "scenes";
	case SCENE_PACKET_OVERFLOW_PASSES:
		return "passes";
	case SCENE_PACKET_OVERFLOW_DRAWS:
		return "draws";
	case SCENE_PACKET_OVERFLOW_MATERIALS:
		return "materials";
	case SCENE_PACKET_OVERFLOW_GEOMETRY_RECORDS:
		return "geometryRecords";
	case SCENE_PACKET_OVERFLOW_INSTANCE_RECORDS:
		return "instanceRecords";
	case SCENE_PACKET_OVERFLOW_SUBVIEW_CAPTURES:
		return "subviewCaptures";
	default:
		return "unknown";
	}
}

const char *RendererMaterialClass_Name( rendererMaterialClass_t materialClass ) {
	switch ( materialClass ) {
	case RENDER_MATERIAL_NONE:
		return "none";
	case RENDER_MATERIAL_SHADOW_ONLY:
		return "shadowOnly";
	case RENDER_MATERIAL_OPAQUE:
		return "opaque";
	case RENDER_MATERIAL_PERFORATED:
		return "perforated";
	case RENDER_MATERIAL_TRANSLUCENT:
		return "translucent";
	case RENDER_MATERIAL_GUI:
		return "gui";
	case RENDER_MATERIAL_SUBVIEW:
		return "subview";
	case RENDER_MATERIAL_POST_PROCESS:
		return "postProcess";
	default:
		return "unknown";
	}
}

const char *GeometryResourceFallbackReason_Name( geometryResourceFallbackReason_t reason ) {
	switch ( reason ) {
	case GEOMETRY_RESOURCE_FALLBACK_NONE:
		return "none";
	case GEOMETRY_RESOURCE_FALLBACK_MISSING_GEOMETRY:
		return "missingGeometry";
	case GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_DEFORM:
		return "unsupportedDeform";
	case GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_GPU_SKINNING:
		return "unsupportedGpuSkinning";
	case GEOMETRY_RESOURCE_FALLBACK_MISSING_VERTEX_BUFFER:
		return "missingVertexBuffer";
	case GEOMETRY_RESOURCE_FALLBACK_MISSING_INDEX_DATA:
		return "missingIndexData";
	default:
		return "unknown";
	}
}

static void R_ScenePackets_EnsureFrontEndFrame( void ) {
	if ( !rg_frontEndScenePacketFrameOpen ) {
		rg_frontEndScenePacketFrame.Clear();
		rg_frontEndScenePacketFrameOpen = true;
		rg_frontEndLastDrawView = NULL;
		R_ClassicDeformDomain_BeginPacketFrame(
			R_ClassicDeformDomain_CurrentFrameToken() );
	}
	rg_frontEndScenePacketFrame.MarkFrontEndDerived();
}

void R_ScenePackets_BeginFrame( void ) {
	rg_frontEndScenePacketFrame.Clear();
	rg_frontEndScenePacketFrame.MarkFrontEndDerived();
	rg_frontEndScenePacketFrameOpen = true;
	rg_frontEndLastDrawView = NULL;
	R_ClassicDeformDomain_BeginPacketFrame(
		R_ClassicDeformDomain_CurrentFrameToken() );
}

void R_ScenePackets_EndFrame( void ) {
	if ( !rg_frontEndScenePacketFrameOpen ) {
		// Frame was never opened (capture off): last operation was already a
		// Clear() from the constructor or a prior EndFrame, nothing to do.
		return;
	}
	R_ScenePackets_CommitTemporalInstanceHistory(
		rg_frontEndScenePacketFrame );
	rg_frontEndScenePacketFrame.Clear();
	rg_frontEndScenePacketFrameOpen = false;
	rg_frontEndLastDrawView = NULL;
}

typedef bool (*scenePacketDrawSurfFilter_t)( const viewDef_t *viewDef, const drawSurf_t *drawSurf );

static bool R_ScenePackets_DrawSurfHasGeometry( const drawSurf_t *drawSurf ) {
	return drawSurf != NULL && drawSurf->geo != NULL && drawSurf->geo->numIndexes > 0;
}

static bool R_ScenePackets_DrawSurfHasMaterialGeometry( const drawSurf_t *drawSurf ) {
	return R_ScenePackets_DrawSurfHasGeometry( drawSurf )
		&& drawSurf->space != NULL
		&& drawSurf->material != NULL;
}

static bool R_ScenePackets_LightGridIsUsable( const LightGrid &candidate ) {
	if ( candidate.GridPointCount() <= 0 || !candidate.HasImage() ) {
		return false;
	}
	if ( candidate.lightGridBounds[0] <= 0 || candidate.lightGridBounds[1] <= 0 || candidate.lightGridBounds[2] <= 0 ) {
		return false;
	}
	return true;
}

static int R_ScenePackets_CurrentViewLightGridArea( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->renderWorld == NULL ) {
		return -1;
	}

	idRenderWorldLocal *world = viewDef->renderWorld;
	int areaNum = viewDef->areaNum;
	if ( areaNum < 0 || areaNum >= world->numPortalAreas ) {
		areaNum = world->PointInArea( viewDef->initialViewAreaOrigin );
	}
	if ( areaNum < 0 || areaNum >= world->numPortalAreas ) {
		areaNum = world->PointInArea( viewDef->renderView.vieworg );
	}
	return ( areaNum >= 0 && areaNum < world->numPortalAreas ) ? areaNum : -1;
}

static const LightGrid *R_ScenePackets_CurrentViewLightGrid( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->renderWorld == NULL ) {
		return NULL;
	}
	const int areaNum = R_ScenePackets_CurrentViewLightGridArea( viewDef );
	if ( areaNum < 0 ) {
		return NULL;
	}

	const LightGrid &candidate = viewDef->renderWorld->portalAreas[areaNum].lightGrid;
	return R_ScenePackets_LightGridIsUsable( candidate ) ? &candidate : NULL;
}

static bool R_ScenePackets_LightGridMaterialStageIsActive( const shaderStage_t *stage, const float *regs ) {
	return stage != NULL && ( regs == NULL || regs[ stage->conditionRegister ] != 0.0f );
}

static int R_ScenePackets_LightGridStageBlendBits( const shaderStage_t *stage ) {
	return stage != NULL ? ( stage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) : 0;
}

static bool R_ScenePackets_LightGridMaterialHasActiveColorMaskStage( const idMaterial *material, const float *regs ) {
	if ( material == NULL ) {
		return false;
	}

	const int stageCount = material->GetNumStages();
	for ( int stageIndex = 0; stageIndex < stageCount; stageIndex++ ) {
		const shaderStage_t *stage = material->GetStage( stageIndex );
		if ( R_ScenePackets_LightGridMaterialStageIsActive( stage, regs ) && ( stage->drawStateBits & GLS_COLORMASK ) != 0 ) {
			return true;
		}
	}

	return false;
}

static bool R_ScenePackets_LightGridAmbientStageCanProvideAlbedo( const idMaterial *material, const shaderStage_t *stage, const float *regs ) {
	if ( material == NULL || stage == NULL ) {
		return false;
	}
	if ( stage->lighting != SL_AMBIENT || !R_ScenePackets_LightGridMaterialStageIsActive( stage, regs ) ) {
		return false;
	}
	if ( material->IsPortalSky() || material->TestMaterialFlag( MF_SKY ) || material->GetSort() >= SS_FAR ) {
		return false;
	}
	if ( stage->texture.image == NULL || stage->newStage != NULL ) {
		return false;
	}
	if ( stage->texture.texgen != TG_EXPLICIT && stage->texture.texgen != TG_POT_CORRECTION ) {
		return false;
	}
	return R_ScenePackets_LightGridStageBlendBits( stage ) == 0;
}

static bool R_ScenePackets_LightGridStageCanProvideAlbedo( const idMaterial *material, const shaderStage_t *stage, const float *regs ) {
	if ( stage == NULL ) {
		return false;
	}
	if ( stage->lighting == SL_DIFFUSE && R_ScenePackets_LightGridMaterialStageIsActive( stage, regs ) ) {
		return true;
	}
	return R_ScenePackets_LightGridAmbientStageCanProvideAlbedo( material, stage, regs );
}

static bool R_ScenePackets_LightGridHasActiveAlbedoStage( const idMaterial *material, const float *regs ) {
	if ( material == NULL ) {
		return false;
	}

	const int stageCount = material->GetNumStages();
	for ( int stageIndex = 0; stageIndex < stageCount; stageIndex++ ) {
		const shaderStage_t *stage = material->GetStage( stageIndex );
		if ( R_ScenePackets_LightGridStageCanProvideAlbedo( material, stage, regs ) ) {
			return true;
		}
	}

	return false;
}

static bool R_ScenePackets_SurfaceCanReceiveLightGrid( const drawSurf_t *drawSurf ) {
	if ( !R_ScenePackets_DrawSurfHasMaterialGeometry( drawSurf ) ) {
		return false;
	}
	const idMaterial *material = drawSurf->material;
	if ( !material->IsDrawn() || material->IsPortalSky() ) {
		return false;
	}
	if ( drawSurf->decalColorCache != NULL ) {
		return false;
	}
	if ( material->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		return false;
	}
	return true;
}

static bool R_ScenePackets_DrawSurfDepthEligible( const viewDef_t *viewDef, const drawSurf_t *drawSurf ) {
	if ( !R_ScenePackets_DrawSurfHasMaterialGeometry( drawSurf ) ) {
		return false;
	}
	const idMaterial *material = drawSurf->material;
	if ( !material->IsDrawn() ) {
		return false;
	}
	if ( material->Coverage() == MC_TRANSLUCENT ) {
		return false;
	}
	return true;
}

static bool R_ScenePackets_DrawSurfAmbientEligible( const viewDef_t *viewDef, const drawSurf_t *drawSurf ) {
	if ( !R_ScenePackets_DrawSurfHasMaterialGeometry( drawSurf ) ) {
		return false;
	}
	const idMaterial *material = drawSurf->material;
	// Clustered PBR lighting uses the ambient packet as the one stable surface
	// owner for the complete light list.  A valid PBR material is not required
	// to author a legacy ambient stage, so retain it here even when HasAmbient()
	// is false.  The modern draw plan still fail-closes on PBR readiness and the
	// classic renderer remains authoritative whenever that admission fails.
	if ( ( !material->HasAmbient() && !material->HasPBR() ) || material->IsPortalSky() || material->SuppressInSubview() ) {
		return false;
	}
	if ( r_skipDecals.GetBool() && R_ScenePackets_DrawSurfIsDecalMaterialPass( drawSurf ) ) {
		return false;
	}
	return material->GetSort() < SS_POST_PROCESS;
}

static bool R_ScenePackets_DrawSurfAuthoredPostEligible( const viewDef_t *viewDef, const drawSurf_t *drawSurf ) {
	if ( !R_ScenePackets_DrawSurfHasMaterialGeometry( drawSurf ) ) {
		return false;
	}
	const idMaterial *material = drawSurf->material;
	if ( !material->HasAmbient() || material->IsPortalSky() || material->SuppressInSubview() ) {
		return false;
	}
	return material->GetSort() >= SS_POST_PROCESS;
}

static bool R_ScenePackets_DrawSurfLightGridEligible( const viewDef_t *viewDef, const drawSurf_t *drawSurf ) {
	if ( !r_useLightGrid.GetBool() || r_skipDiffuse.GetBool() || !glConfig.GLSLProgramAvailable ) {
		return false;
	}
	if ( !R_ScenePackets_SurfaceCanReceiveLightGrid( drawSurf ) ) {
		return false;
	}
	if ( drawSurf->material->GetSort() >= SS_POST_PROCESS || drawSurf->material->SuppressInSubview() ) {
		return false;
	}
	if ( !R_ScenePackets_LightGridHasActiveAlbedoStage( drawSurf->material, drawSurf->shaderRegisters ) ) {
		return false;
	}
	if ( drawSurf->space->weaponDepthHack ) {
		// Keep scene-packet planning in lockstep with the backend: viewmodel
		// scope/glass mask stages are authored outside the light-grid pass.
		if ( R_ScenePackets_LightGridMaterialHasActiveColorMaskStage( drawSurf->material, drawSurf->shaderRegisters ) ) {
			return false;
		}
		return R_ScenePackets_CurrentViewLightGrid( viewDef ) != NULL;
	}
	if ( drawSurf->space->modelDepthHack != 0.0f ) {
		return false;
	}
	if ( drawSurf->area == NULL ) {
		return false;
	}
	return R_ScenePackets_LightGridIsUsable( drawSurf->area->lightGrid );
}

static bool R_ScenePackets_DrawSurfGUIEligible( const viewDef_t *viewDef, const drawSurf_t *drawSurf ) {
	if ( !R_ScenePackets_DrawSurfHasMaterialGeometry( drawSurf ) ) {
		return false;
	}
	const idMaterial *material = drawSurf->material;
	return material->HasAmbient() && !material->IsPortalSky();
}

static bool R_ScenePackets_DrawSurfInWorldGUIEligible( const viewDef_t *viewDef,
		const drawSurf_t *drawSurf ) {
	return drawSurf != NULL && ( drawSurf->dsFlags & DSF_IN_WORLD_GUI ) != 0
		&& R_ScenePackets_DrawSurfGUIEligible( viewDef, drawSurf );
}

// Preserve an empty GUI pass only when the front end actually emitted a
// provenance-tagged surface.  That lets the ownership domain diagnose a
// malformed or otherwise ineligible tagged surface and fall back as a whole,
// without adding a synthetic GUI pass to every ordinary world view.
static bool R_ScenePackets_HasInWorldGUIDrawSurfs( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->drawSurfs == NULL || viewDef->numDrawSurfs <= 0 ) {
		return false;
	}

	for ( int i = 0; i < viewDef->numDrawSurfs; ++i ) {
		const drawSurf_t *drawSurf = viewDef->drawSurfs[i];
		if ( drawSurf != NULL && ( drawSurf->dsFlags & DSF_IN_WORLD_GUI ) != 0 ) {
			return true;
		}
	}
	return false;
}

static bool R_ScenePackets_DrawSurfInteractionEligible( const drawSurf_t *drawSurf ) {
	if ( !R_ScenePackets_DrawSurfHasMaterialGeometry( drawSurf ) ) {
		return false;
	}
	const idMaterial *material = drawSurf->material;
	return material->ReceivesLighting() && !material->IsPortalSky();
}

static bool R_ScenePackets_DrawSurfShadowEligible( const drawSurf_t *drawSurf ) {
	if ( !R_ScenePackets_DrawSurfHasGeometry( drawSurf ) ) {
		return false;
	}

	const idMaterial *material = drawSurf->material;
	if ( material == NULL ) {
		return true;
	}
	if ( material->IsDedicatedCollisionSurface() ) {
		return false;
	}
	if ( material->HasGui() || material->HasSubview() ) {
		return false;
	}
	return material->Coverage() == MC_TRANSLUCENT || material->SurfaceCastsShadow();
}

static int R_ScenePackets_CountFilteredDrawSurfs( const viewDef_t *viewDef, int firstDrawSurf, scenePacketDrawSurfFilter_t filter ) {
	if ( viewDef == NULL || viewDef->drawSurfs == NULL || viewDef->numDrawSurfs <= 0 || filter == NULL ) {
		return 0;
	}

	int count = 0;
	for ( int i = Max( 0, firstDrawSurf ); i < viewDef->numDrawSurfs; ++i ) {
		if ( filter( viewDef, viewDef->drawSurfs[i] ) ) {
			count++;
		}
	}
	return count;
}

static int R_ScenePackets_CountDrawSurfChain( const drawSurf_t *drawSurf, bool ( *filter )( const drawSurf_t *drawSurf ) ) {
	int count = 0;
	for ( const drawSurf_t *cursor = drawSurf; cursor != NULL; cursor = cursor->nextOnLight ) {
		if ( filter == NULL || filter( cursor ) ) {
			count++;
		}
	}
	return count;
}

static bool R_ScenePackets_AppendInteractionChain( idScenePacketFrame &packetFrame,
		const viewLight_t *viewLight, int lightOrdinal,
		const drawSurf_t *drawSurf, sceneInteractionReceiverClass_t receiverClass,
		bool ( *filter )( const drawSurf_t *drawSurf ), int &drawIndex ) {
	int receiverOrdinal = 0;
	for ( const drawSurf_t *cursor = drawSurf; cursor != NULL;
			cursor = cursor->nextOnLight, ++receiverOrdinal ) {
		if ( filter != NULL && !filter( cursor ) ) {
			continue;
		}
		if ( !packetFrame.AddInteractionDrawPacket( cursor, drawIndex++, viewLight,
				lightOrdinal, receiverClass, receiverOrdinal ) ) {
			packetFrame.AddClippedDrawPackets(
				R_ScenePackets_CountDrawSurfChain( cursor->nextOnLight, filter ) );
			return false;
		}
	}
	return true;
}

static bool R_ScenePackets_AppendFogBlendChain(
		idScenePacketFrame &packetFrame, const viewLight_t *viewLight,
		int lightOrdinal, const drawSurf_t *drawSurf,
		sceneFogBlendReceiverClass_t receiverClass, int &drawIndex ) {
	int receiverOrdinal = 0;
	for ( const drawSurf_t *cursor = drawSurf; cursor != NULL;
			cursor = cursor->nextOnLight, ++receiverOrdinal ) {
		if ( !packetFrame.AddFogBlendDrawPacket( cursor, drawIndex++, viewLight,
				lightOrdinal, receiverClass, receiverOrdinal ) ) {
			packetFrame.AddClippedDrawPackets(
				R_ScenePackets_CountDrawSurfChain( cursor->nextOnLight, NULL ) );
			return false;
		}
	}
	return true;
}

static bool R_ScenePackets_AppendShadowChainLazyPass(
		idScenePacketFrame &packetFrame, const viewLight_t *viewLight,
		int lightOrdinal, const drawSurf_t *drawSurf,
		renderPassCategory_t category, sceneShadowCasterClass_t casterClass,
		bool ( *filter )( const drawSurf_t *drawSurf ), int &drawIndex,
		bool &passAdded ) {
	int chainOrdinal = 0;
	for ( const drawSurf_t *cursor = drawSurf; cursor != NULL;
			cursor = cursor->nextOnLight, ++chainOrdinal ) {
		if ( filter != NULL && !filter( cursor ) ) {
			continue;
		}
		if ( !passAdded ) {
			if ( !packetFrame.AddPass( category, true ) ) {
				return false;
			}
			passAdded = true;
		}
		if ( !packetFrame.AddShadowDrawPacket( cursor, category, drawIndex++,
				viewLight, lightOrdinal, casterClass, chainOrdinal ) ) {
			packetFrame.AddClippedDrawPackets(
				R_ScenePackets_CountDrawSurfChain( cursor->nextOnLight, filter ) );
			return false;
		}
	}
	return true;
}

static void R_ScenePackets_AddFilteredDrawSurfPass( idScenePacketFrame &packetFrame, const viewDef_t *viewDef, renderPassCategory_t category, scenePacketDrawSurfFilter_t filter ) {
	if ( !packetFrame.AddPass( category, true ) ) {
		return;
	}
	if ( viewDef == NULL || viewDef->drawSurfs == NULL || viewDef->numDrawSurfs <= 0 || filter == NULL ) {
		return;
	}

	int drawIndex = 0;
	for ( int i = 0; i < viewDef->numDrawSurfs; ++i ) {
		const drawSurf_t *drawSurf = viewDef->drawSurfs[i];
		if ( !filter( viewDef, drawSurf ) ) {
			continue;
		}
		if ( !packetFrame.AddDrawPacket( drawSurf, category, drawIndex++ ) ) {
			packetFrame.AddClippedDrawPackets( R_ScenePackets_CountFilteredDrawSurfs( viewDef, i + 1, filter ) );
			break;
		}
	}
}

static bool R_ScenePackets_ViewLightHasMaterialInteractions( const viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->lightShader == NULL ) {
		return false;
	}
	if ( vLight->lightShader->IsFogLight() || vLight->lightShader->IsBlendLight() ) {
		return false;
	}
	return vLight->localInteractions != NULL || vLight->globalInteractions != NULL || vLight->translucentInteractions != NULL;
}

static bool R_ScenePackets_ViewLightCanCastShadows( const viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->lightShader == NULL ) {
		return false;
	}
	if ( vLight->lightShader->IsFogLight() || vLight->lightShader->IsBlendLight() || vLight->lightShader->IsAmbientLight() ) {
		return false;
	}
	if ( !r_shadows.GetBool() || !vLight->lightShader->LightCastsShadows() ) {
		return false;
	}
	if ( vLight->lightDef != NULL && vLight->lightDef->parms.noShadows ) {
		return false;
	}
	return true;
}

static bool R_ScenePackets_ViewLightCanUseShadowMaps( const viewLight_t *vLight ) {
	if ( !R_ScenePackets_ViewLightCanCastShadows( vLight ) ) {
		return false;
	}
	// noDynamicShadows still permits dmap's optimized static/prelight stencil
	// volumes. It only excludes the dynamic/map caster path.
	return vLight->lightDef == NULL
		|| !vLight->lightDef->parms.noDynamicShadows;
}

static bool R_ScenePackets_ViewLightIsFogOrBlend( const viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->lightShader == NULL ) {
		return false;
	}
	return vLight->lightShader->IsFogLight()
		|| vLight->lightShader->IsBlendLight();
}

static void R_ScenePackets_AddInteractionPass( idScenePacketFrame &packetFrame, const viewDef_t *viewDef ) {
	if ( !packetFrame.AddPass( RENDER_PASS_ARB2_INTERACTION, true ) || viewDef == NULL ) {
		return;
	}

	int drawIndex = 0;
	int lightOrdinal = 0;
	for ( const viewLight_t *vLight = viewDef->viewLights; vLight != NULL;
			vLight = vLight->next, ++lightOrdinal ) {
		if ( !R_ScenePackets_ViewLightHasMaterialInteractions( vLight ) ) {
			continue;
		}
		if ( !R_ScenePackets_AppendInteractionChain( packetFrame, vLight,
				lightOrdinal, vLight->localInteractions,
				SCENE_INTERACTION_RECEIVER_LOCAL,
				R_ScenePackets_DrawSurfInteractionEligible, drawIndex ) ) {
			return;
		}
		if ( !R_ScenePackets_AppendInteractionChain( packetFrame, vLight,
				lightOrdinal, vLight->globalInteractions,
				SCENE_INTERACTION_RECEIVER_GLOBAL,
				R_ScenePackets_DrawSurfInteractionEligible, drawIndex ) ) {
			return;
		}
		if ( !r_skipTranslucent.GetBool()
				&& !R_ScenePackets_AppendInteractionChain( packetFrame, vLight,
					lightOrdinal, vLight->translucentInteractions,
					SCENE_INTERACTION_RECEIVER_TRANSLUCENT,
					R_ScenePackets_DrawSurfInteractionEligible, drawIndex ) ) {
			return;
		}
	}
}

static void R_ScenePackets_AddShadowMapPass( idScenePacketFrame &packetFrame, const viewDef_t *viewDef ) {
	if ( !r_useShadowMap.GetBool() || viewDef == NULL ) {
		return;
	}

	bool passAdded = false;
	int drawIndex = 0;
	int lightOrdinal = 0;
	for ( const viewLight_t *vLight = viewDef->viewLights; vLight != NULL;
			vLight = vLight->next, ++lightOrdinal ) {
		if ( !R_ScenePackets_ViewLightHasMaterialInteractions( vLight )
				|| !R_ScenePackets_ViewLightCanUseShadowMaps( vLight ) ) {
			continue;
		}
		if ( !R_ScenePackets_AppendShadowChainLazyPass( packetFrame, vLight,
				lightOrdinal, vLight->globalShadowMapCasters,
				RENDER_PASS_SHADOW_MAP, SCENE_SHADOW_CASTER_MAP_GLOBAL_STATIC,
				R_ScenePackets_DrawSurfShadowEligible, drawIndex, passAdded ) ) {
			return;
		}
		if ( !R_ScenePackets_AppendShadowChainLazyPass( packetFrame, vLight,
				lightOrdinal, vLight->localShadowMapCasters,
				RENDER_PASS_SHADOW_MAP, SCENE_SHADOW_CASTER_MAP_LOCAL_STATIC,
				R_ScenePackets_DrawSurfShadowEligible, drawIndex, passAdded ) ) {
			return;
		}
		if ( !R_ScenePackets_AppendShadowChainLazyPass( packetFrame, vLight,
				lightOrdinal, vLight->globalShadowMapDynamicCasters,
				RENDER_PASS_SHADOW_MAP, SCENE_SHADOW_CASTER_MAP_GLOBAL_DYNAMIC,
				R_ScenePackets_DrawSurfShadowEligible, drawIndex, passAdded ) ) {
			return;
		}
		if ( !R_ScenePackets_AppendShadowChainLazyPass( packetFrame, vLight,
				lightOrdinal, vLight->localShadowMapDynamicCasters,
				RENDER_PASS_SHADOW_MAP, SCENE_SHADOW_CASTER_MAP_LOCAL_DYNAMIC,
				R_ScenePackets_DrawSurfShadowEligible, drawIndex, passAdded ) ) {
			return;
		}
		if ( !R_ScenePackets_AppendShadowChainLazyPass( packetFrame, vLight,
				lightOrdinal, vLight->globalTranslucentShadowMapCasters,
				RENDER_PASS_SHADOW_MAP,
				SCENE_SHADOW_CASTER_MAP_GLOBAL_TRANSLUCENT,
				R_ScenePackets_DrawSurfShadowEligible, drawIndex, passAdded ) ) {
			return;
		}
		if ( !R_ScenePackets_AppendShadowChainLazyPass( packetFrame, vLight,
				lightOrdinal, vLight->localTranslucentShadowMapCasters,
				RENDER_PASS_SHADOW_MAP,
				SCENE_SHADOW_CASTER_MAP_LOCAL_TRANSLUCENT,
				R_ScenePackets_DrawSurfShadowEligible, drawIndex, passAdded )
				|| !R_ScenePackets_AppendShadowChainLazyPass( packetFrame, vLight,
					lightOrdinal, vLight->globalShadowMapStencilSupplements,
					RENDER_PASS_SHADOW_MAP, SCENE_SHADOW_CASTER_SUPPLEMENT_GLOBAL,
					R_ScenePackets_DrawSurfShadowEligible, drawIndex, passAdded )
				|| !R_ScenePackets_AppendShadowChainLazyPass( packetFrame, vLight,
					lightOrdinal, vLight->localShadowMapStencilSupplements,
					RENDER_PASS_SHADOW_MAP, SCENE_SHADOW_CASTER_SUPPLEMENT_LOCAL,
					R_ScenePackets_DrawSurfShadowEligible, drawIndex, passAdded ) ) {
			return;
		}
	}
}

static void R_ScenePackets_AddStencilShadowPass( idScenePacketFrame &packetFrame, const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return;
	}

	bool passAdded = false;
	int drawIndex = 0;
	int lightOrdinal = 0;
	for ( const viewLight_t *vLight = viewDef->viewLights; vLight != NULL;
			vLight = vLight->next, ++lightOrdinal ) {
		if ( !R_ScenePackets_ViewLightHasMaterialInteractions( vLight )
				|| !R_ScenePackets_ViewLightCanCastShadows( vLight ) ) {
			continue;
		}
		if ( !R_ScenePackets_AppendShadowChainLazyPass( packetFrame, vLight,
				lightOrdinal, vLight->globalShadows, RENDER_PASS_STENCIL_SHADOW,
				SCENE_SHADOW_CASTER_STENCIL_GLOBAL,
				R_ScenePackets_DrawSurfShadowEligible, drawIndex, passAdded ) ) {
			return;
		}
		if ( !R_ScenePackets_AppendShadowChainLazyPass( packetFrame, vLight,
				lightOrdinal, vLight->localShadows, RENDER_PASS_STENCIL_SHADOW,
				SCENE_SHADOW_CASTER_STENCIL_LOCAL,
				R_ScenePackets_DrawSurfShadowEligible, drawIndex, passAdded ) ) {
			return;
		}
	}
}

static void R_ScenePackets_AddFogBlendPass( idScenePacketFrame &packetFrame, const viewDef_t *viewDef ) {
	if ( !packetFrame.AddPass( RENDER_PASS_FOG_BLEND, true ) || viewDef == NULL ) {
		return;
	}

	int drawIndex = 0;
	int lightOrdinal = 0;
	for ( const viewLight_t *vLight = viewDef->viewLights; vLight != NULL;
			vLight = vLight->next, ++lightOrdinal ) {
		if ( !R_ScenePackets_ViewLightIsFogOrBlend( vLight ) ) {
			continue;
		}
		if ( !R_ScenePackets_AppendFogBlendChain( packetFrame, vLight,
				lightOrdinal, vLight->globalInteractions,
				SCENE_FOG_BLEND_RECEIVER_GLOBAL, drawIndex ) ) {
			return;
		}
		if ( !R_ScenePackets_AppendFogBlendChain( packetFrame, vLight,
				lightOrdinal, vLight->localInteractions,
				SCENE_FOG_BLEND_RECEIVER_LOCAL, drawIndex ) ) {
			return;
		}
	}
}

static bool R_ScenePackets_IsMainScenePostProcessView( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->viewEntitys == NULL ) {
		return false;
	}
	if ( ( viewDef->renderFlags & RF_PORTAL_SKY ) != 0 ) {
		return false;
	}
	if ( viewDef->isSubview
		|| viewDef->superView != NULL
		|| viewDef->subviewSurface != NULL
		|| viewDef->renderView.viewID < 0
		|| viewDef->isXraySubview ) {
		return false;
	}
	if ( viewDef->renderWorld != NULL && viewDef->renderWorld->mapName.Length() == 0 ) {
		return false;
	}
	return true;
}

static bool R_ScenePackets_PostProcessBloomOrToneMapRequested( void ) {
	return ( r_bloom.GetBool() && r_bloomIntensity.GetFloat() > 0.0001f )
		|| r_hdrToneMap.GetBool()
		|| r_hdrDebugView.GetInteger() > 0;
}

static bool R_ScenePackets_PostProcessMotionBlurRequested( void ) {
	if ( !r_motionBlur.GetBool() || r_jitter.GetBool() ) {
		return false;
	}
	if ( r_motionBlurDebug.GetBool() ) {
		return true;
	}
	return r_motionBlurStrength.GetFloat() > 0.0f
		&& r_motionBlurMaxPixels.GetFloat() > 0.0f
		&& r_motionBlurSamples.GetInteger() > 0;
}

static bool R_ScenePackets_PostProcessPassRequested( const viewDef_t *viewDef, renderPassCategory_t category ) {
	const bool mainPostView = R_ScenePackets_IsMainScenePostProcessView( viewDef );
	if ( r_skipPostProcess.GetBool() || !mainPostView ) {
		return false;
	}

	switch ( category ) {
	case RENDER_PASS_SSAO:
		if ( !glConfig.GLSLProgramAvailable ) {
			return false;
		}
		return r_ssao.GetBool() && r_ssaoRadius.GetFloat() > 0.0f && r_ssaoIntensity.GetFloat() > 0.0f;
	case RENDER_PASS_MOTION_BLUR:
		if ( !glConfig.GLSLProgramAvailable ) {
			return false;
		}
		return R_ScenePackets_PostProcessMotionBlurRequested();
	case RENDER_PASS_BLOOM:
		if ( !glConfig.GLSLProgramAvailable ) {
			return false;
		}
		return R_ScenePackets_PostProcessBloomOrToneMapRequested();
	default:
		return false;
	}
}

static void R_ScenePackets_AddPostProcessCommandPass( idScenePacketFrame &packetFrame, renderPassCategory_t category ) {
	packetFrame.AddCommandPacket();
	packetFrame.AddPass( category, true, true );
}

static void R_ScenePackets_AddRootPostProcessPasses( idScenePacketFrame &packetFrame, const viewDef_t *viewDef ) {
	static const renderPassCategory_t postPasses[] = {
		RENDER_PASS_SSAO,
		RENDER_PASS_MOTION_BLUR,
		RENDER_PASS_BLOOM
	};

	for ( int i = 0; i < static_cast<int>( sizeof( postPasses ) / sizeof( postPasses[0] ) ); ++i ) {
		if ( R_ScenePackets_PostProcessPassRequested( viewDef, postPasses[i] ) ) {
			R_ScenePackets_AddPostProcessCommandPass( packetFrame, postPasses[i] );
		}
	}
}

static void R_ScenePackets_AddDrawView( idScenePacketFrame &packetFrame, const viewDef_t *viewDef, bool legacyBridge ) {
	packetFrame.AddLegacyDrawView();
	if ( !packetFrame.AddScene( viewDef, legacyBridge ) ) {
		return;
	}

	const bool worldView = viewDef != NULL && viewDef->viewEntitys != NULL;
	if ( worldView ) {
		R_ScenePackets_AddFilteredDrawSurfPass( packetFrame, viewDef, RENDER_PASS_DEPTH, R_ScenePackets_DrawSurfDepthEligible );
		R_ScenePackets_AddShadowMapPass( packetFrame, viewDef );
		R_ScenePackets_AddStencilShadowPass( packetFrame, viewDef );
		R_ScenePackets_AddInteractionPass( packetFrame, viewDef );
		R_ScenePackets_AddFilteredDrawSurfPass( packetFrame, viewDef, RENDER_PASS_AMBIENT, R_ScenePackets_DrawSurfAmbientEligible );
		if ( r_rendererSharedInWorldGui.GetBool()
				&& R_ScenePackets_HasInWorldGUIDrawSurfs( viewDef ) ) {
			R_ScenePackets_AddFilteredDrawSurfPass( packetFrame, viewDef, RENDER_PASS_GUI, R_ScenePackets_DrawSurfInWorldGUIEligible );
		}
		R_ScenePackets_AddFilteredDrawSurfPass( packetFrame, viewDef, RENDER_PASS_LIGHT_GRID, R_ScenePackets_DrawSurfLightGridEligible );
		R_ScenePackets_AddFogBlendPass( packetFrame, viewDef );
		R_ScenePackets_AddRootPostProcessPasses( packetFrame, viewDef );
		R_ScenePackets_AddFilteredDrawSurfPass( packetFrame, viewDef, RENDER_PASS_AUTHORED_POST, R_ScenePackets_DrawSurfAuthoredPostEligible );
	} else {
		R_ScenePackets_AddFilteredDrawSurfPass( packetFrame, viewDef, RENDER_PASS_GUI, R_ScenePackets_DrawSurfGUIEligible );
	}

	packetFrame.FinishScene();
}

static void R_ScenePackets_AddCommandPass( idScenePacketFrame &packetFrame, renderPassCategory_t category, const viewDef_t *viewDef, bool legacyBridge ) {
	packetFrame.AddCommandPacket( SCENE_PACKET_CATEGORY_UNKNOWN );
	if ( !packetFrame.AddScene( viewDef, legacyBridge ) ) {
		return;
	}
	packetFrame.AddPass( category, true, true );
	packetFrame.FinishScene();
}

void R_ScenePackets_AddRenderView( const viewDef_t *viewDef ) {
	R_ScenePackets_EnsureFrontEndFrame();
	R_ScenePackets_AddDrawView( rg_frontEndScenePacketFrame, viewDef, false );
	rg_frontEndLastDrawView = viewDef;
}

void R_ScenePackets_AddSpecialEffects( const viewDef_t *viewDef,
		int specialEffectsMask ) {
	R_ScenePackets_EnsureFrontEndFrame();
	R_ScenePackets_AddCommandPass( rg_frontEndScenePacketFrame, RENDER_PASS_SPECIAL_EFFECTS, viewDef, false );
	rg_frontEndScenePacketFrame.SetLastSceneSpecialEffectsMask(
		specialEffectsMask );
	rg_frontEndLastDrawView = NULL;
}

void R_ScenePackets_AddRenderTargetOp( void ) {
	R_ScenePackets_EnsureFrontEndFrame();
	R_ScenePackets_AddCommandPass( rg_frontEndScenePacketFrame, RENDER_PASS_AUTHORED_POST, NULL, false );
	rg_frontEndLastDrawView = NULL;
}

void R_ScenePackets_AddCopyRender( idImage *image, int x, int y,
		int width, int height, int cubeFace, bool copyDepth ) {
	R_ScenePackets_EnsureFrontEndFrame();
	R_ScenePackets_AddCommandPass( rg_frontEndScenePacketFrame, RENDER_PASS_AUTHORED_POST, NULL, false );
	if ( rg_frontEndLastDrawView != NULL && rg_frontEndLastDrawView->isSubview ) {
		rg_frontEndScenePacketFrame.AddSubviewCapture( rg_frontEndLastDrawView,
			image, x, y, width, height, cubeFace, copyDepth );
	}
	// A capture can belong to exactly one immediately preceding subview draw.
	rg_frontEndLastDrawView = NULL;
}

void R_ScenePackets_AddPresent( void ) {
	R_ScenePackets_EnsureFrontEndFrame();
	R_ScenePackets_AddCommandPass( rg_frontEndScenePacketFrame, RENDER_PASS_PRESENT, NULL, false );
	rg_frontEndLastDrawView = NULL;
}

void R_ScenePackets_AddCommandOnly( void ) {
	R_ScenePackets_EnsureFrontEndFrame();
	rg_frontEndScenePacketFrame.AddCommandPacket();
	rg_frontEndLastDrawView = NULL;
}

const idScenePacketFrame &R_ScenePackets_FrontEndFrame( void ) {
	return rg_frontEndScenePacketFrame;
}

bool R_ScenePackets_FrontEndFrameAvailable( void ) {
	const scenePacketFrameStats_t &stats = rg_frontEndScenePacketFrame.Stats();
	return rg_frontEndScenePacketFrameOpen
		&& stats.frontEndDerived
		&& ( stats.scenePackets > 0 || stats.passPackets > 0 || stats.drawPackets > 0 || stats.commandPackets > 0 );
}

bool R_ScenePackets_BuildTemporalViewMotionPolicy( const viewDef_t *viewDef,
		unsigned int backendExactMotionDomainMask,
		temporalViewMotionPolicy_t &policy ) {
	policy = TemporalHistoryCore_BeginViewMotionPolicy();
	return R_ScenePackets_FrontEndFrameAvailable()
		&& rg_frontEndScenePacketFrame.BuildTemporalViewMotionPolicy( viewDef,
			backendExactMotionDomainMask, policy );
}

void R_ScenePackets_BuildLegacyCommandStream( const emptyCommand_t *cmds, idScenePacketFrame &packetFrame ) {
	packetFrame.Clear();
	packetFrame.MarkBackendDerived();
	const viewDef_t *lastDrawView = NULL;

	for ( const emptyCommand_t *cmd = cmds; cmd != NULL; cmd = reinterpret_cast<const emptyCommand_t *>( cmd->next ) ) {
		switch ( cmd->commandId ) {
		case RC_DRAW_VIEW:
			lastDrawView = reinterpret_cast<const drawSurfsCommand_t *>( cmd )->viewDef;
			R_ScenePackets_AddDrawView( packetFrame, lastDrawView, true );
			break;
		case RC_DRAW_SPECIAL_EFFECTS:
			R_ScenePackets_AddCommandPass( packetFrame, RENDER_PASS_SPECIAL_EFFECTS, reinterpret_cast<const drawSurfsCommand_t *>( cmd )->viewDef, true );
			packetFrame.SetLastSceneSpecialEffectsMask(
				R_ScenePackets_ActiveSpecialEffectsMask() );
			lastDrawView = NULL;
			break;
		case RC_SET_RENDERTEXTURE:
		case RC_RESOLVE_MSAA:
		case RC_CLEAR_RENDERTARGET:
		case RC_SET_POSTPROCESS_SOURCE_SIZE:
		case RC_SET_POSTPROCESS_SOURCE_COLOR_SPACE:
		case RC_SET_POSTPROCESS_SMAA_QUALITY:
			R_ScenePackets_AddCommandPass( packetFrame, RENDER_PASS_AUTHORED_POST, NULL, true );
			lastDrawView = NULL;
			break;
		case RC_COPY_RENDER:
			R_ScenePackets_AddCommandPass( packetFrame, RENDER_PASS_AUTHORED_POST, NULL, true );
			if ( lastDrawView != NULL && lastDrawView->isSubview ) {
				const copyRenderCommand_t *copy =
					reinterpret_cast<const copyRenderCommand_t *>( cmd );
				packetFrame.AddSubviewCapture( lastDrawView, copy->image,
					copy->x, copy->y, copy->imageWidth, copy->imageHeight,
					copy->cubeFace, copy->copyDepth );
			}
			lastDrawView = NULL;
			break;
		case RC_SET_BUFFER:
			packetFrame.AddCommandPacket();
			lastDrawView = NULL;
			break;
		case RC_SWAP_BUFFERS:
			R_ScenePackets_AddCommandPass( packetFrame, RENDER_PASS_PRESENT, NULL, true );
			lastDrawView = NULL;
			break;
		default:
			lastDrawView = NULL;
			break;
		}
	}
}

void R_ScenePackets_LogIfVerbose( const idScenePacketFrame &packetFrame ) {
	if ( r_rendererMetrics.GetInteger() < 2 ) {
		return;
	}

	const scenePacketFrameStats_t &stats = packetFrame.Stats();
	common->Printf(
		"scenePackets source=%s scenes=%d passes=%d draws=%d clipped=%d cmds=%d drawViews=%d subviewCaptures=%d materials=%d geometryRecords=%d instanceRecords=%d withMaterial=%d resources=%d geometryRecordRefs=%d instanceRefs=%d geometry=%d regs=%d indexCache=%d ambientCache=%d deformRecords=%d deform(material=%d finalized=%d interaction=%d fog=%d shadowVolume=%d other=%d completed=%d empty=%d notApplicable=%d skipped=%d failed=%d unsupported=%d fallback=%d) categories(world=%d subview=%d remote=%d fx=%d viewmodel=%d demo=%d gui=%d post=%d present=%d command=%d) sortFailures=%d overflow=%d cause=%s\n",
		stats.frontEndDerived ? "frontend" : ( stats.backendDerived ? "backend" : "unknown" ),
		stats.scenePackets,
		stats.passPackets,
		stats.drawPackets,
		stats.clippedDrawPackets,
		stats.commandPackets,
		stats.legacyDrawViews,
		stats.subviewCaptures,
		stats.materialRecords,
		stats.geometryRecords,
		stats.instanceRecords,
		stats.drawPacketsWithMaterial,
		stats.drawPacketsWithResourceRecord,
		stats.drawPacketsWithGeometryRecord,
		stats.drawPacketsWithInstanceRecord,
		stats.drawPacketsWithGeometry,
		stats.drawPacketsWithShaderRegisters,
		stats.drawPacketsWithIndexCache,
		stats.drawPacketsWithAmbientCache,
		stats.drawPacketsWithClassicDeformRecord,
		stats.materialDeformDrawPackets,
		stats.deformFinalizedDrawPackets,
		stats.deformInteractionReceiverPackets,
		stats.deformFogReceiverPackets,
		stats.deformShadowVolumePackets,
		stats.deformOtherRolePackets,
		stats.deformCompletedPackets,
		stats.deformEmptyPackets,
		stats.deformNotApplicablePackets,
		stats.deformSkippedPackets,
		stats.deformFailedPackets,
		stats.deformUnsupportedPackets,
		stats.deformFallbackPackets,
		stats.worldPackets,
		stats.subviewPackets,
		stats.remoteCameraPackets,
		stats.specialEffectPackets,
		stats.viewmodelPackets,
		stats.renderDemoPackets,
		stats.guiPackets,
		stats.postProcessPackets,
		stats.presentPackets,
		stats.commandOnlyPackets,
		stats.sortKeyValidationFailures,
		stats.overflow ? 1 : 0,
		ScenePacketOverflowCause_Name( stats.overflowCause ) );
	const int passCount = packetFrame.NumPasses();
	for ( int i = 0; i < passCount; ++i ) {
		const passPacket_t &pass = packetFrame.Pass( i );
		common->Printf(
			"scenePackets pass[%d]=%s category=%s draws=%d enabled=%d\n",
			i,
			RenderPassCategory_Name( pass.passCategory ),
			ScenePacketCategory_Name( pass.packetCategory ),
			pass.drawPacketCount,
			pass.enabled ? 1 : 0 );
	}
	const int materialRecordLogCount = Min( packetFrame.NumMaterialRecords(), 8 );
	for ( int i = 0; i < materialRecordLogCount; ++i ) {
		const materialResourceRecord_t &record = packetFrame.MaterialRecord( i );
		common->Printf(
			"scenePackets material[%d]=%s class=%s diffuse=%s normal=%s specular=%s pbr=%d workflow=%d table=%d\n",
			i,
			record.material ? record.material->GetName() : "<null>",
			RendererMaterialClass_Name( static_cast<rendererMaterialClass_t>( record.permutation.materialClass ) ),
			record.diffuseImage ? record.diffuseImage->GetName() : "<none>",
			record.normalImage ? record.normalImage->GetName() : "<none>",
			record.specularImage ? record.specularImage->GetName() : "<none>",
			record.hasPBR ? 1 : 0,
			record.pbrWorkflow,
			record.resourceTableIndex );
	}
	const int geometryRecordLogCount = Min( packetFrame.NumGeometryRecords(), 8 );
	for ( int i = 0; i < geometryRecordLogCount; ++i ) {
		const geometryResourceRecord_t &record = packetFrame.GeometryRecord( i );
		common->Printf(
			"scenePackets geometry[%d]=verts:%d indexes:%d vbo:%d@%d ibo:%d@%d lifetime=%d skin=%d deform=%d contract=%s/%s/%s fresh=%d hash=%llu fallback=%s flags=0x%x bounds=%s\n",
			i,
			record.vertexCount,
			record.indexCount,
			record.ambientVertexBuffer,
			record.ambientCacheOffset,
			record.indexBuffer,
			record.indexCacheOffset,
			record.uploadLifetime,
			record.skinningMode,
			record.deformMode,
			ClassicDeformRole_Name( record.classicDeform.role ),
			ClassicDeformKind_Name( record.classicDeform.kind ),
			ClassicDeformOutcome_Name( record.classicDeform.outcome ),
			record.hasClassicDeformRecord ? 1 : 0,
			static_cast<unsigned long long>( record.classicDeform.semanticHash ),
			GeometryResourceFallbackReason_Name( record.fallbackReason ),
			record.fallbackFlags,
			record.hasBounds ? "yes" : "no" );
	}
	const int instanceRecordLogCount = Min( packetFrame.NumInstanceRecords(), 8 );
	for ( int i = 0; i < instanceRecordLogCount; ++i ) {
		const instanceRecord_t &record = packetFrame.InstanceRecord( i );
		common->Printf(
			"scenePackets instance[%d]=entity:%d flags=0x%x regs=%d+%d color=(%.2f %.2f %.2f %.2f) legacy=%d\n",
			i,
			record.entityIndex,
			record.visibilityFlags,
			record.shaderRegisterBase,
			record.shaderRegisterCount,
			record.entityColor[0],
			record.entityColor[1],
			record.entityColor[2],
			record.entityColor[3],
			record.legacyBridge ? 1 : 0 );
	}
}

bool RendererScenePacket_RunSelfTest( void ) {
	rendererSelfTestSkipPostProcessRestore_t skipPostProcessRestore;

	srfTriangles_t geo;
	memset( &geo, 0, sizeof( geo ) );
	geo.numVerts = 3;
	geo.numIndexes = 6;
	drawSurf_t drawSurfs[2];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	drawSurfs[0].geo = &geo;
	drawSurfs[1].geo = &geo;
	float shaderRegisters[MAX_EXPRESSION_REGISTERS];
	memset( shaderRegisters, 0, sizeof( shaderRegisters ) );
	shaderRegisters[EXP_REG_PARM0] = 0.25f;
	shaderRegisters[EXP_REG_PARM1] = 0.50f;
	shaderRegisters[EXP_REG_PARM2] = 0.75f;
	shaderRegisters[EXP_REG_PARM3] = 1.00f;
	if ( tr.defaultMaterial != NULL ) {
		drawSurfs[0].material = tr.defaultMaterial;
		drawSurfs[0].sort = tr.defaultMaterial->GetSort();
		drawSurfs[0].shaderRegisters = shaderRegisters;
		drawSurfs[1].material = tr.defaultMaterial;
		drawSurfs[1].sort = tr.defaultMaterial->GetSort() + 0.000001f;
		drawSurfs[1].shaderRegisters = shaderRegisters;
	}
	drawSurf_t *drawSurfPtrs[2] = { &drawSurfs[0], &drawSurfs[1] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	R_ScenePackets_CopyIdentityMatrix( viewEntity.modelMatrix );
	R_ScenePackets_CopyIdentityMatrix( viewEntity.modelViewMatrix );
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	R_ScenePackets_CopyIdentityMatrix( worldView.projectionMatrix );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 2;

	drawSurfsCommand_t drawCmd;
	memset( &drawCmd, 0, sizeof( drawCmd ) );
	drawCmd.commandId = RC_DRAW_VIEW;
	drawCmd.viewDef = &worldView;
	drawSurfsCommand_t fxCmd;
	memset( &fxCmd, 0, sizeof( fxCmd ) );
	fxCmd.commandId = RC_DRAW_SPECIAL_EFFECTS;
	fxCmd.viewDef = &worldView;
	emptyCommand_t swapCmd;
	memset( &swapCmd, 0, sizeof( swapCmd ) );
	swapCmd.commandId = RC_SWAP_BUFFERS;
	drawCmd.next = &fxCmd.commandId;
	fxCmd.next = &swapCmd.commandId;
	swapCmd.next = NULL;

	idScenePacketFrame packetFrame;
	R_ScenePackets_BuildLegacyCommandStream( reinterpret_cast<const emptyCommand_t *>( &drawCmd ), packetFrame );
	const scenePacketFrameStats_t &stats = packetFrame.Stats();
	const bool expectedDepthEligible = tr.defaultMaterial != NULL
		&& tr.defaultMaterial->IsDrawn()
		&& tr.defaultMaterial->Coverage() != MC_TRANSLUCENT;
	const bool expectedAmbientEligible = tr.defaultMaterial != NULL
		&& tr.defaultMaterial->HasAmbient()
		&& !tr.defaultMaterial->IsPortalSky()
		&& !tr.defaultMaterial->SuppressInSubview()
		&& tr.defaultMaterial->GetSort() < SS_POST_PROCESS;
	const int expectedDepthDraws = expectedDepthEligible ? 2 : 0;
	const int expectedAmbientDraws = expectedAmbientEligible ? 2 : 0;
	const int expectedWorldDraws = expectedDepthDraws + expectedAmbientDraws;
	const int expectedMaterialRecords = expectedWorldDraws > 0 && tr.defaultMaterial != NULL ? 1 : 0;
	const int expectedGeometryRecords = expectedWorldDraws > 0 ? 1 : 0;
	const int expectedInstanceRecords = expectedWorldDraws > 0 ? 1 : 0;
	const int expectedDrawsWithMaterial = tr.defaultMaterial != NULL ? expectedWorldDraws : 0;
	if ( stats.scenePackets != 3 || stats.passPackets != 8 || stats.drawPackets != expectedWorldDraws || stats.legacyDrawViews != 1 || stats.commandPackets != 2 || stats.materialRecords != expectedMaterialRecords || stats.geometryRecords != expectedGeometryRecords || stats.instanceRecords != expectedInstanceRecords || stats.drawPacketsWithResourceRecord != expectedDrawsWithMaterial || stats.drawPacketsWithGeometryRecord != expectedWorldDraws || stats.drawPacketsWithInstanceRecord != expectedWorldDraws || stats.drawPacketsWithGeometry != expectedWorldDraws || stats.worldPackets != expectedWorldDraws || stats.postProcessPackets != 0 || stats.specialEffectPackets != 1 || stats.presentPackets != 1 || stats.sortKeyValidationFailures != 0 || !stats.backendDerived || stats.frontEndDerived || stats.overflow || !packetFrame.ValidateSortKeys() ) {
		common->Printf(
			"RendererScenePacket self-test failed: scenes=%d passes=%d draws=%d views=%d cmds=%d materials=%d geometryRecords=%d instances=%d resources=%d geometryRefs=%d instanceRefs=%d geometry=%d world=%d post=%d special=%d present=%d sortFailures=%d overflow=%d cause=%s\n",
			stats.scenePackets,
			stats.passPackets,
			stats.drawPackets,
			stats.legacyDrawViews,
			stats.commandPackets,
			stats.materialRecords,
			stats.geometryRecords,
			stats.instanceRecords,
			stats.drawPacketsWithResourceRecord,
			stats.drawPacketsWithGeometryRecord,
			stats.drawPacketsWithInstanceRecord,
			stats.drawPacketsWithGeometry,
			stats.worldPackets,
			stats.postProcessPackets,
			stats.specialEffectPackets,
			stats.presentPackets,
			stats.sortKeyValidationFailures,
			stats.overflow ? 1 : 0,
			ScenePacketOverflowCause_Name( stats.overflowCause ) );
		return false;
	}
	if ( expectedWorldDraws > 0 ) {
		const drawPacket_t &packet = packetFrame.DrawPacket( 0 );
		if ( packet.materialRecord == NULL || packet.materialRecord->material != tr.defaultMaterial || packet.materialRecordIndex != 0 || packet.geometryRecord == NULL || packet.geometryRecordIndex != 0 || packet.instanceRecord == NULL || packet.instanceRecordIndex != 0 || packet.indexCount != 6 || packet.vertexCount != 3 ) {
			common->Printf(
				"RendererScenePacket self-test failed: bad packet records material=%d geometry=%d instance=%d indexCount=%d vertexCount=%d\n",
				packet.materialRecordIndex,
				packet.geometryRecordIndex,
				packet.instanceRecordIndex,
				packet.indexCount,
				packet.vertexCount );
			return false;
		}
		const instanceRecord_t &instance = packetFrame.InstanceRecord( packet.instanceRecordIndex );
		if ( instance.entityColor[0] != 0.25f || instance.entityColor[1] != 0.50f || instance.entityColor[2] != 0.75f || instance.entityColor[3] != 1.00f || !instance.hasShaderRegisters ) {
			common->Printf( "RendererScenePacket self-test failed: instance color/register capture mismatch\n" );
			return false;
		}
	}

	R_ScenePackets_BeginFrame();
	R_ScenePackets_AddRenderView( &worldView );
	R_ScenePackets_AddSpecialEffects( &worldView, SPECIAL_EFFECT_BLUR );
	R_ScenePackets_AddPresent();
	const idScenePacketFrame &frontEndPacketFrame = R_ScenePackets_FrontEndFrame();
	const scenePacketFrameStats_t &frontEndStats = frontEndPacketFrame.Stats();
	if ( frontEndStats.scenePackets != 3 || frontEndStats.passPackets != 8 || frontEndStats.drawPackets != expectedWorldDraws || frontEndStats.legacyDrawViews != 1 || frontEndStats.commandPackets != 2 || frontEndStats.materialRecords != expectedMaterialRecords || frontEndStats.geometryRecords != expectedGeometryRecords || frontEndStats.instanceRecords != expectedInstanceRecords || frontEndStats.drawPacketsWithResourceRecord != expectedDrawsWithMaterial || frontEndStats.drawPacketsWithGeometryRecord != expectedWorldDraws || frontEndStats.drawPacketsWithInstanceRecord != expectedWorldDraws || frontEndStats.drawPacketsWithGeometry != expectedWorldDraws || frontEndStats.worldPackets != expectedWorldDraws || frontEndStats.postProcessPackets != 0 || frontEndStats.specialEffectPackets != 1 || frontEndStats.presentPackets != 1 || frontEndStats.sortKeyValidationFailures != 0 || !frontEndStats.frontEndDerived || frontEndStats.backendDerived || frontEndStats.overflow || !frontEndPacketFrame.ValidateSortKeys() ) {
		common->Printf(
			"RendererScenePacket self-test failed: frontend scenes=%d passes=%d draws=%d views=%d cmds=%d materials=%d geometryRecords=%d instances=%d resources=%d geometryRefs=%d instanceRefs=%d geometry=%d source(frontend=%d backend=%d) world=%d post=%d special=%d present=%d sortFailures=%d overflow=%d cause=%s\n",
			frontEndStats.scenePackets,
			frontEndStats.passPackets,
			frontEndStats.drawPackets,
			frontEndStats.legacyDrawViews,
			frontEndStats.commandPackets,
			frontEndStats.materialRecords,
			frontEndStats.geometryRecords,
			frontEndStats.instanceRecords,
			frontEndStats.drawPacketsWithResourceRecord,
			frontEndStats.drawPacketsWithGeometryRecord,
			frontEndStats.drawPacketsWithInstanceRecord,
			frontEndStats.drawPacketsWithGeometry,
			frontEndStats.frontEndDerived ? 1 : 0,
			frontEndStats.backendDerived ? 1 : 0,
			frontEndStats.worldPackets,
			frontEndStats.postProcessPackets,
			frontEndStats.specialEffectPackets,
			frontEndStats.presentPackets,
			frontEndStats.sortKeyValidationFailures,
			frontEndStats.overflow ? 1 : 0,
			ScenePacketOverflowCause_Name( frontEndStats.overflowCause ) );
		R_ScenePackets_EndFrame();
		return false;
	}
	if ( frontEndPacketFrame.NumScenes() > 0 && frontEndPacketFrame.Scene( 0 ).legacyBridge ) {
		common->Printf( "RendererScenePacket self-test failed: frontend scene marked as legacy bridge\n" );
		R_ScenePackets_EndFrame();
		return false;
	}
	if ( frontEndPacketFrame.NumScenes() < 2
			|| frontEndPacketFrame.Scene( 1 ).specialEffectsMask
				!= SPECIAL_EFFECT_BLUR ) {
		common->Printf( "RendererScenePacket self-test failed: frontend special-effect mask capture mismatch\n" );
		R_ScenePackets_EndFrame();
		return false;
	}
	R_ScenePackets_EndFrame();

	idScenePacketFrame postPacketFrame;
	if ( !postPacketFrame.AddScene( &worldView, false ) ) {
		common->Printf( "RendererScenePacket self-test failed: could not add post scene\n" );
		return false;
	}
	postPacketFrame.AddCommandPacket();
	if ( !postPacketFrame.AddPass( RENDER_PASS_BLOOM, true, true ) ) {
		common->Printf( "RendererScenePacket self-test failed: could not add bloom post pass\n" );
		return false;
	}
	const scenePacketFrameStats_t &postStats = postPacketFrame.Stats();
	if ( postStats.scenePackets != 1 || postStats.passPackets != 1 || postStats.commandPackets != 1 || postStats.postProcessPackets != 1 || postStats.worldPackets != 0 || postStats.specialEffectPackets != 0 || postStats.presentPackets != 0 || postPacketFrame.Pass( 0 ).packetCategory != SCENE_PACKET_CATEGORY_POST_PROCESS ) {
		common->Printf(
			"RendererScenePacket self-test failed: post command categorization scenes=%d passes=%d cmds=%d post=%d world=%d special=%d present=%d pass0=%s pass1=%s\n",
			postStats.scenePackets,
			postStats.passPackets,
			postStats.commandPackets,
			postStats.postProcessPackets,
			postStats.worldPackets,
			postStats.specialEffectPackets,
			postStats.presentPackets,
			ScenePacketCategory_Name( postPacketFrame.Pass( 0 ).packetCategory ),
			"none" );
		return false;
	}

	// Exercise the actual material-record capture path with opt-in PBR metadata.
	// The ambient stage makes this a drawable synthetic surface while the absent
	// bump+diffuse interaction deliberately proves the missing-fallback bit.
	static const char pbrPacketDeclaration[] =
		"material _pbr_scene_packet_selftest {\n"
		" {\n"
		"  map _white\n"
		" }\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap _white\n"
		"  metallic 0.35\n"
		"  roughness 0.65\n"
		"  ao 0.85\n"
		"  autoLegacyFallback 0\n"
		" }\n"
		"}\n";
	idDecl *pbrPacketDecl = declManager->AllocateDecl( DECL_MATERIAL );
	if ( pbrPacketDecl == NULL ) {
		common->Printf( "RendererScenePacket self-test failed: PBR declaration allocation\n" );
		return false;
	}
	idMaterial *pbrPacketMaterial = static_cast<idMaterial *>( pbrPacketDecl );
	bool pbrPacketValid = pbrPacketMaterial->Parse(
		pbrPacketDeclaration,
		idLib::SizeToInt( sizeof( pbrPacketDeclaration ) - 1, "RendererScenePacket PBR self-test" ) );
	if ( pbrPacketValid ) {
		drawSurf_t pbrDrawSurf;
		memset( &pbrDrawSurf, 0, sizeof( pbrDrawSurf ) );
		pbrDrawSurf.material = pbrPacketMaterial;
		pbrDrawSurf.sort = pbrPacketMaterial->GetSort();
		{
			idScenePacketFrame pbrPacketFrame;
			pbrPacketValid = pbrPacketFrame.AddDrawPacket( &pbrDrawSurf, RENDER_PASS_AMBIENT, 0 );
			const drawPacket_t *pbrDrawPacket = pbrPacketValid && pbrPacketFrame.NumDrawPackets() == 1
				? &pbrPacketFrame.DrawPacket( 0 )
				: NULL;
			const materialResourceRecord_t *pbrRecord = pbrDrawPacket != NULL && pbrDrawPacket->materialRecordIndex >= 0
				? &pbrPacketFrame.MaterialRecord( pbrDrawPacket->materialRecordIndex )
				: NULL;
			const pbrMaterialInfo_t &pbr = pbrPacketMaterial->GetPBRInfo();
			const int registerCount = pbrPacketMaterial->GetNumRegisters();
			auto validRegister = [&]( int index ) -> bool { return index >= 0 && index < registerCount; };
			pbrPacketValid = pbrRecord != NULL
				&& pbrDrawPacket->materialRecord == pbrRecord
				&& pbrRecord->material == pbrPacketMaterial
				&& pbrRecord->hasPBR
				&& pbrRecord->pbrWorkflow == static_cast<int>( pbr.workflow )
				&& pbrRecord->pbrAlbedoImage == pbr.albedo.image
				&& pbrRecord->pbrLegacyFallbackMissing
				&& pbrRecord->pbrLegacyFallbackMissing == pbr.legacyFallbackMissing
				&& pbrRecord->pbrMetallicRegister == pbr.metallicRegister
				&& pbrRecord->pbrRoughnessRegister == pbr.roughnessRegister
				&& pbrRecord->pbrAORegister == pbr.aoRegister
				&& pbrRecord->pbrNormalScaleRegister == pbr.normalScaleRegister
				&& validRegister( pbrRecord->pbrMetallicRegister )
				&& validRegister( pbrRecord->pbrRoughnessRegister )
				&& validRegister( pbrRecord->pbrAORegister )
				&& validRegister( pbrRecord->pbrNormalScaleRegister );
		}
	}
	DeclManager_FreeAllocatedDecl( pbrPacketDecl );
	if ( !pbrPacketValid ) {
		common->Printf( "RendererScenePacket self-test failed: PBR packet metadata propagation\n" );
		return false;
	}

	common->Printf( "RendererScenePacket self-test passed (backend, frontend, post commands, PBR metadata)\n" );
	return true;
}
