// Copyright (C) 2026 DarkMatter Productions
#include "tr_local.h"
#include "GpuSkinning.h"
#include <unordered_map>
#include <unordered_set>
#include <new>

struct frameLightReceipt_s {
	frameLightReceipt_s *next;
	idRenderLightLocal *source;
	idRenderLightLocal *snapshot;
};

namespace {
// The command arena owns these copies. No model/def/cache destructor runs on
// them: they are render inputs, with all mutable streams copied below.
template<class T> T *Copy( const T *source, size_t count = 1 ) {
	if ( source == NULL || count == 0 ) return NULL;
	if ( count > ( idMath::INT_MAX - 16 ) / sizeof( T ) ) {
		common->FatalError( "render snapshot size overflow" );
	}
	T *result = static_cast<T *>( R_FrameAlloc( static_cast<int>( count * sizeof( T ) ) ) );
	memcpy( result, source, count * sizeof( T ) );
	return result;
}

class viewSnapshot_t {
	std::unordered_map<const srfTriangles_t *, srfTriangles_t *> geometry;
	std::unordered_map<const vertCache_t *, vertCache_t *> caches;
	std::unordered_set<const drawSurf_t *> surfaces;
	std::unordered_set<const viewEntity_t *> entities;
public:
	vertCache_t *Cache( const vertCache_t *source ) {
		if ( source == NULL ) return NULL;
		auto found = caches.find( source );
		if ( found != caches.end() ) return found->second;
		vertCache_t *copy = Copy( source );
		caches.emplace( source, copy );
		// Vulkan consumes CPU staging bytes. Owning the bytes as well as the
		// header permits skinning fallback to repair the submitted stream.
		if ( source->virtMem != NULL ) {
			copy->virtMem = Copy( static_cast<const byte *>( source->virtMem ) + source->offset, source->size );
			copy->offset = 0;
		}
		copy->tag = TAG_TEMP;
		copy->user = NULL;
		copy->next = copy->prev = NULL;
		return copy;
	}

	srfTriangles_t *Geometry( const srfTriangles_t *source ) {
		if ( source == NULL ) return NULL;
		auto found = geometry.find( source );
		if ( found != geometry.end() ) return found->second;
		// Complete any deferred CPU fallback before the worker sees this stream.
		// In overlap mode new poses already take the complete CPU path, but a
		// cached pose can have been produced before the mode changed.
		srfTriangles_t *prepared = const_cast<srfTriangles_t *>( source );
		if ( prepared->gpuSkinningDeferred ) {
			prepared->gpuSkinningDeferred = false;
			if ( prepared->verts != NULL && prepared->numVerts > 0 && !prepared->tangentsCalculated ) R_DeriveTangents( prepared );
			vertCache_t *cache = prepared->ambientCache;
			const size_t bytes = static_cast<size_t>( prepared->numVerts ) * sizeof( idDrawVert );
			if ( cache != NULL && cache->virtMem != NULL && cache->size >= bytes && prepared->verts != NULL ) {
				memcpy( static_cast<byte *>( cache->virtMem ) + cache->offset, prepared->verts, bytes );
			}
		}
		// Shadow casters and debug draws used to create these caches during
		// recording. Preparing them here keeps the shared geometry allocators
		// and cache bookkeeping on the producer.
		if ( R_TriHasPrimBatchMesh( prepared ) ) {
			R_CreatePackedSurfaceFrameCaches( prepared, true, true );
		} else if ( prepared->ambientCache == NULL && prepared->verts != NULL && prepared->numVerts > 0 ) {
			R_CreateAmbientCache( prepared, false );
		}
		srfTriangles_t *copy = Copy( source );
		geometry.emplace( source, copy );
		copy->verts = Copy( source->verts, source->numVerts );
		copy->indexes = Copy( source->indexes, source->numIndexes );
		copy->silIndexes = Copy( source->silIndexes, source->numIndexes );
		copy->silEdges = Copy( source->silEdges, source->numSilEdges );
		copy->shadowVertexes = Copy( source->shadowVertexes, source->numVerts );
		copy->facePlanes = Copy( source->facePlanes, source->numIndexes / 3 );
		copy->dominantTris = Copy( source->dominantTris, source->numVerts );
		copy->mirroredVerts = Copy( source->mirroredVerts, source->numMirroredVerts );
		copy->dupVerts = Copy( source->dupVerts, source->numDupVerts * 2 );
		copy->ambientCache = Cache( source->ambientCache );
		copy->indexCache = Cache( source->indexCache );
		copy->shadowCache = Cache( source->shadowCache );
		copy->lightingCache = Cache( source->lightingCache );
		copy->gpuSkinningBindPoseVerts = Copy( source->gpuSkinningBindPoseVerts, source->numGpuSkinningVerts );
		copy->gpuSkinningVerts = Copy( source->gpuSkinningVerts, source->numGpuSkinningVerts );
		copy->gpuSkinningJointPalette = Copy( source->gpuSkinningJointPalette, source->numGpuSkinningJoints * GPU_SKINNING_JOINT_FLOATS );
		copy->gpuSkinningJointPaletteAlloc = NULL;
		copy->previousPositions = Copy( source->previousPositions, source->numPreviousPositions );
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		copy->skinToModelTransforms = Copy( source->skinToModelTransforms, source->numSkinToModelTransforms * 16 );
		copy->skinToModelTransformsAlloc = NULL;
#endif
		copy->ambientSurface = Geometry( source->ambientSurface );
		copy->topAmbientSurface = Geometry( source->topAmbientSurface );
		copy->nextDeferredFree = NULL;
		return copy;
	}

	void Entity( const viewEntity_t *source ) {
		if ( source == NULL || !entities.insert( source ).second || source->entityDef == NULL ) return;
		viewEntity_t *view = const_cast<viewEntity_t *>( source );
		const idRenderEntityLocal *def = source->entityDef;
		view->entityDef = new ( R_FrameAlloc( sizeof( *def ) ) ) idRenderEntityLocal( *def );
		view->entityDef->parms.joints = Copy( def->parms.joints, def->parms.numJoints );
		view->entityDef->parms.remoteRenderView = Copy( def->parms.remoteRenderView );
	}

	void Surface( const drawSurf_t *source, bool finalized = false ) {
		if ( source == NULL || !surfaces.insert( source ).second ) return;
		drawSurf_t *surf = const_cast<drawSurf_t *>( source );
		Entity( surf->space );
		// Interaction/volume surfaces do not initialize the embedded deform
		// record. Only finalized ambient draws may carry a source to rebase.
		const srfTriangles_t *deformSource = finalized && surf->classicDeform.initialized && surf->classicDeform.valid
			? Geometry( surf->classicDeform.sourceGeometry.geometry ) : NULL;
		surf->geo = Geometry( surf->geo );
		if ( finalized ) R_ClassicDeformDomain_RebaseFrameCopy( surf->classicDeform, deformSource, surf->geo );
		surf->pbrLightGeo = Geometry( surf->pbrLightGeo );
		surf->dynamicTexCoords = Cache( surf->dynamicTexCoords );
		surf->decalColorCache = Cache( surf->decalColorCache );
		surf->previousPositionCache = Cache( surf->previousPositionCache );
	}
	void Chain( const drawSurf_t *surf ) {
		for ( ; surf != NULL; surf = surf->nextOnLight ) Surface( surf );
	}
};
}

static void R_SnapshotRenderViewInternal( viewDef_t *view ) {
	if ( view == NULL || view->frameSnapshot ) return;
	view->frameSnapshot = true;
	view->lightGridAvailable = view->renderWorld != NULL && view->renderWorld->AnyLightGridAvailable();
	viewSnapshot_t snapshot;
	for ( viewEntity_t *entity = view->viewEntitys; entity; entity = entity->next ) snapshot.Entity( entity );
	for ( int i = 0; i < view->numDrawSurfs; ++i ) snapshot.Surface( view->drawSurfs[i], true );
	for ( int i = 0; i < view->numOutlineDrawSurfs; ++i ) snapshot.Surface( view->outlineDrawSurfs[i] );
	for ( viewLight_t *light = view->viewLights; light; light = light->next ) {
		if ( light->lightDef != NULL ) {
			frameLightReceipt_s *receipt = static_cast<frameLightReceipt_s *>( R_FrameAlloc( sizeof( *receipt ) ) );
			receipt->source = light->lightDef;
			light->lightDef = new ( R_FrameAlloc( sizeof( idRenderLightLocal ) ) ) idRenderLightLocal( *light->lightDef );
			light->lightDef->frustumTris = snapshot.Geometry( light->lightDef->frustumTris );
			receipt->snapshot = light->lightDef;
			receipt->next = frameData->lightReceipts;
			frameData->lightReceipts = receipt;
		}
		light->frustumTris = snapshot.Geometry( light->frustumTris );
		snapshot.Chain( light->globalShadows );
		snapshot.Chain( light->localShadows );
		snapshot.Chain( light->localInteractions );
		snapshot.Chain( light->globalInteractions );
		snapshot.Chain( light->translucentInteractions );
		snapshot.Chain( light->globalShadowMapStencilSupplements );
		snapshot.Chain( light->localShadowMapStencilSupplements );
		snapshot.Chain( light->localShadowMapCasters );
		snapshot.Chain( light->globalShadowMapCasters );
		snapshot.Chain( light->localShadowMapDynamicCasters );
		snapshot.Chain( light->globalShadowMapDynamicCasters );
		snapshot.Chain( light->localTranslucentShadowMapCasters );
		snapshot.Chain( light->globalTranslucentShadowMapCasters );
	}
}

void R_SnapshotRenderView( viewDef_t *view ) {
	if ( R_RenderThread_OverlapRequested() ) R_SnapshotRenderViewInternal( view );
}

void R_RetireRenderSnapshots( frameData_t *frame ) {
	for ( frameLightReceipt_s *receipt = frame->lightReceipts; receipt; receipt = receipt->next ) {
		idRenderLightLocal *source = receipt->source;
		const idRenderLightLocal *snapshot = receipt->snapshot;
		// Deletion of live light defs is deferred until this receipt retires.
		// Ignore a reused handle or a light whose parameters changed meanwhile.
		if ( snapshot->shadowMapStencilFallbackSticky && snapshot->world != NULL
			&& snapshot->index >= 0 && snapshot->index < snapshot->world->lightDefs.Num()
			&& snapshot->world->lightDefs[snapshot->index] == source
			&& source->lastModifiedFrameNum == snapshot->lastModifiedFrameNum ) {
			source->shadowMapStencilFallbackSticky = true;
		}
	}
	frame->lightReceipts = NULL;
}

// Device uploads and light-grid residency changes run after joining the prior
// consumer and before publishing this frame. Ordinary stock worlds have no grid.
void R_PrepareRenderFrameResources( const emptyCommand_t *commands ) {
	std::unordered_set<const idMaterial *> materials;
	auto prepareImage = []( idImage *image ) {
		if ( image != NULL && !image->IsLoaded() ) image->ActuallyLoadImage( false );
	};
	auto prepareMaterial = [&]( const idMaterial *material ) {
		if ( material == NULL || !materials.insert( material ).second ) return;
		for ( int i = 0; i < material->GetNumStages(); ++i ) {
			const shaderStage_t *stage = material->GetStage( i );
			prepareImage( stage->texture.image );
			newShaderStage_t *program = stage->newStage;
			if ( program == NULL ) continue;
			for ( int j = 0; j < program->numShaderTextures; ++j ) prepareImage( program->shaderTextureImages[j] );
			for ( int j = 0; j < program->numFragmentProgramImages; ++j ) prepareImage( program->fragmentProgramImages[j] );
			if ( program->glslProgram ) R_ValidateGLSLProgram( program );
		}
	};
	auto prepareChain = [&]( const drawSurf_t *surf ) {
		for ( ; surf != NULL; surf = surf->nextOnLight ) prepareMaterial( surf->material );
	};
	std::unordered_set<idRenderWorldLocal *> worlds;
	for ( const emptyCommand_t *cmd = commands; cmd != NULL; cmd = reinterpret_cast<const emptyCommand_t *>( cmd->next ) ) {
		if ( cmd->commandId != RC_DRAW_VIEW ) continue;
		viewDef_t *view = reinterpret_cast<const drawSurfsCommand_t *>( cmd )->viewDef;
		if ( view == NULL ) continue;
		for ( int i = 0; i < view->numDrawSurfs; ++i ) prepareMaterial( view->drawSurfs[i]->material );
		for ( int i = 0; i < view->numOutlineDrawSurfs; ++i ) prepareMaterial( view->outlineDrawSurfs[i]->material );
		for ( const viewLight_t *light = view->viewLights; light; light = light->next ) {
			prepareMaterial( light->lightShader );
			prepareImage( light->falloffImage );
			prepareChain( light->globalInteractions ); prepareChain( light->localInteractions );
			prepareChain( light->translucentInteractions );
			prepareChain( light->globalShadowMapCasters ); prepareChain( light->localShadowMapCasters );
			prepareChain( light->globalShadowMapDynamicCasters ); prepareChain( light->localShadowMapDynamicCasters );
			prepareChain( light->globalTranslucentShadowMapCasters ); prepareChain( light->localTranslucentShadowMapCasters );
		}
		if ( view->renderWorld == NULL || !worlds.insert( view->renderWorld ).second ) continue;
		idRenderWorldLocal *world = view->renderWorld;
		if ( !world->AnyLightGridAvailable() ) continue;
		for ( int area = 0; area < world->numPortalAreas; ++area ) {
			if ( world->portalAreas[area].lightGrid.IsUsable() ) world->EnsureLightGridAreaImages( area );
		}
	}
}

void R_FrameOwnershipSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	R_RenderThread_Sync( "frame ownership self-test" );
	idDrawVert verts[2] = {};
	verts[0].xyz.Set( 1, 2, 3 );
	verts[1].xyz.Set( 4, 5, 6 );
	glIndex_t indexes[3] = { 0, 1, 0 };
	byte cacheBytes[16 + sizeof( verts )] = {};
	memcpy( cacheBytes + 16, verts, sizeof( verts ) );
	vertCache_t cache = {};
	cache.virtMem = cacheBytes;
	cache.offset = 16;
	cache.size = sizeof( verts );
	cache.tag = TAG_USED;
	float palette[GPU_SKINNING_JOINT_FLOATS] = {};
	palette[0] = 7;
	idVec3 previous[2] = { idVec3( 8, 9, 10 ), idVec3( 11, 12, 13 ) };
	srfTriangles_t geometry = {};
	geometry.verts = verts;
	geometry.numVerts = 2;
	geometry.indexes = indexes;
	geometry.numIndexes = 3;
	geometry.ambientCache = &cache;
	geometry.gpuSkinningJointPalette = palette;
	geometry.numGpuSkinningJoints = 1;
	geometry.previousPositions = previous;
	geometry.numPreviousPositions = 2;
	idJointMat joint;
	memset( &joint, 0, sizeof( joint ) );
	idRenderEntityLocal entity;
	entity.parms.origin.Set( 14, 15, 16 );
	entity.parms.joints = &joint;
	entity.parms.numJoints = 1;
	viewEntity_t space = {};
	space.entityDef = &entity;
	drawSurf_t surf = {};
	surf.geo = &geometry;
	surf.space = &space;
	surf.previousPositionCache = &cache;
	drawSurf_t *draws[2] = { &surf, &surf };
	idRenderLightLocal light;
	light.parms.lightRadius.Set( 17, 18, 19 );
	viewLight_t viewLight = {};
	drawSurf_t volume = {};
	volume.geo = &geometry;
	volume.space = &space;
	// Such records are intentionally uninitialized in ordinary light chains.
	memset( &volume.classicDeform, 0xcd, sizeof( volume.classicDeform ) );
	viewLight.lightDef = &light;
	viewLight.globalInteractions = &surf;
	viewLight.globalShadows = &volume;
	viewDef_t view = {};
	view.drawSurfs = draws;
	view.numDrawSurfs = 2;
	view.viewEntitys = &space;
	view.viewLights = &viewLight;
	frameLightReceipt_s *savedReceipts = frameData->lightReceipts;
	R_SnapshotRenderViewInternal( &view );
	// Repeating submission must not clone already-owned streams a second time.
	const srfTriangles_t *submitted = surf.geo;
	R_SnapshotRenderViewInternal( &view );
	verts[0].xyz.Zero(); indexes[1] = 0; palette[0] = 0;
	previous[0].Zero(); memset( cacheBytes, 0, sizeof( cacheBytes ) );
	entity.parms.origin.Zero(); light.parms.lightRadius.Zero();
	const bool passed = surf.geo == submitted && submitted != &geometry
		&& submitted->verts[0].xyz.x == 1 && submitted->indexes[1] == 1
		&& submitted->gpuSkinningJointPalette[0] == 7 && submitted->previousPositions[0].x == 8
		&& submitted->ambientCache != &cache && submitted->ambientCache->offset == 0
		&& submitted->ambientCache->tag == TAG_TEMP
		&& static_cast<const idDrawVert *>( submitted->ambientCache->virtMem )[0].xyz.x == 1
		&& surf.previousPositionCache == submitted->ambientCache
		&& space.entityDef != &entity && space.entityDef->parms.origin.x == 14
		&& space.entityDef->parms.joints != &joint
		&& viewLight.lightDef != &light && viewLight.lightDef->parms.lightRadius.x == 17
		&& viewLight.globalInteractions == &surf && volume.geo == submitted;
	// These fixtures are stack-owned, and are never eligible for light receipts.
	frameData->lightReceipts = savedReceipts;
	common->Printf( "Renderer frame ownership self-test %s: geometry, cache bytes, aliases, palettes, previous positions and defs\n", passed ? "passed" : "FAILED" );
	if ( !passed ) common->Error( "Renderer frame ownership self-test failed" );
}
