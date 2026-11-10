// Copyright (C) 2026 DarkMatter Productions
#include "tr_local.h"
#include "GpuSkinning.h"
#include "RendererMetrics.h"
#include "FrameSnapshotCopy.h"
#include "FrameSnapshotRetain.h"
#include <unordered_map>
#include <unordered_set>
#include <new>

struct frameLightReceipt_s {
	frameLightReceipt_s *next;
	idRenderLightLocal *source;
	idRenderLightLocal *snapshot;
};

namespace {
// Main-thread diagnostics, enabled explicitly for a measured window. Ordinary
// play pays no clock reads or per-copy accounting.
struct snapshotStats_t {
	unsigned long long copiedBytes, reusedBytes, cacheBytes, geometryBytes, copies, views, frames, microseconds;
	int lastFrame;
};
snapshotStats_t snapshotStats = {};
bool snapshotProfiling = false;
idCVar r_vkSnapshotRetain( "r_vkSnapshotRetain", "1", CVAR_RENDERER | CVAR_BOOL, "retain validated immutable index copies across overlap frames" );
idFrameSnapshotRetainedIndexes retainedIndexes;
std::vector<idFrameSnapshotRetainedIndexes::lease_t> retainedLeases[2];

// The command arena owns these copies. No model/def/cache destructor runs on
// them: they are render inputs, with all mutable streams copied below.
template<class T> T *Copy( const T *source, size_t count = 1 ) {
	if ( source == NULL || count == 0 ) return NULL;
	if ( count > ( idMath::INT_MAX - 16 ) / sizeof( T ) ) {
		common->FatalError( "render snapshot size overflow" );
	}
	T *result = static_cast<T *>( R_FrameAlloc( static_cast<int>( count * sizeof( T ) ) ) );
	memcpy( result, source, count * sizeof( T ) );
	if ( snapshotProfiling ) {
		snapshotStats.copiedBytes += count * sizeof( T );
		snapshotStats.copies++;
	}
	return result;
}

class viewSnapshot_t {
	idFrameSnapshotCopies streamCopies;
	std::unordered_map<const srfTriangles_t *, srfTriangles_t *> geometry;
	std::unordered_map<const vertCache_t *, vertCache_t *> caches;
	std::unordered_set<const drawSurf_t *> surfaces;
	std::unordered_set<const viewEntity_t *> entities;
	template<class T> T *CopyStream( const T *source, size_t count ) {
		if ( source == NULL || count == 0 ) return NULL;
		if ( count > ( idMath::INT_MAX - 16 ) / sizeof( T ) ) {
			common->FatalError( "render snapshot stream size overflow" );
		}
		const size_t bytes = count * sizeof( T );
		const auto result = streamCopies.Copy( source, bytes,
			[]( size_t size ) { return R_FrameAlloc( static_cast<int>( size ) ); } );
		if ( snapshotProfiling ) {
			if ( result.reused ) snapshotStats.reusedBytes += bytes;
			else { snapshotStats.copiedBytes += bytes; snapshotStats.copies++; }
		}
		return static_cast<T *>( result.data );
	}
public:
	template<class T> T *Indexes( const T *source, size_t count ) {
		if ( source == NULL || count == 0 ) return NULL;
		if ( count > ( idMath::INT_MAX - 16 ) / sizeof( T ) ) common->FatalError( "retained index size overflow" );
		if ( r_vkSnapshotRetain.GetBool() ) {
			const auto result = retainedIndexes.Acquire( source, count * sizeof( T ) );
			if ( result.lease ) {
				retainedLeases[frameData->slot].push_back( result.lease );
				if ( snapshotProfiling ) {
					if ( result.reused ) snapshotStats.reusedBytes += count * sizeof( T );
					else { snapshotStats.copiedBytes += count * sizeof( T ); snapshotStats.copies++; }
				}
				return reinterpret_cast<T *>( const_cast<std::uint32_t *>( result.lease->data() ) );
			}
		}
		return CopyStream( source, count );
	}
	vertCache_t *Cache( const vertCache_t *source, bool indexStream = false ) {
		if ( source == NULL ) return NULL;
		auto found = caches.find( source );
		if ( found != caches.end() ) return found->second;
		vertCache_t *copy = Copy( source );
		caches.emplace( source, copy );
		// Vulkan consumes CPU staging bytes. Owning the bytes as well as the
		// header permits skinning fallback to repair the submitted stream.
		if ( source->virtMem != NULL ) {
			const unsigned long long before = snapshotStats.copiedBytes;
			const byte *bytes = static_cast<const byte *>( source->virtMem ) + source->offset;
			copy->virtMem = indexStream ? Indexes( bytes, source->size ) : CopyStream( bytes, source->size );
			if ( snapshotProfiling ) snapshotStats.cacheBytes += snapshotStats.copiedBytes - before;
			copy->offset = 0;
		}
		copy->tag = TAG_TEMP;
		copy->user = NULL;
		copy->next = copy->prev = NULL;
		return copy;
	}

	void PrepareGeometry( const srfTriangles_t *source ) {
		if ( source == NULL || !geometry.emplace( source, nullptr ).second ) return;
		// Finish every potentially mutating preparation before taking any stream
		// copy. Two triangle records can share vertices while only one still
		// needs tangents or a CPU skinning fallback.
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
		if ( R_TriHasPrimBatchMesh( prepared ) ) {
			R_CreatePackedSurfaceFrameCaches( prepared, true, true );
		} else if ( prepared->ambientCache == NULL && prepared->verts != NULL && prepared->numVerts > 0 ) {
			R_CreateAmbientCache( prepared, false );
		}
		PrepareGeometry( source->ambientSurface );
		PrepareGeometry( source->topAmbientSurface );
	}

	srfTriangles_t *Geometry( const srfTriangles_t *source ) {
		if ( source == NULL ) return NULL;
		auto found = geometry.find( source );
		if ( found != geometry.end() && found->second != NULL ) return found->second;
		srfTriangles_t *copy = Copy( source );
		geometry[source] = copy;
		const unsigned long long beforeStreams = snapshotStats.copiedBytes;
		copy->verts = CopyStream( source->verts, source->numVerts );
		copy->indexes = Indexes( source->indexes, source->numIndexes );
		copy->silIndexes = Indexes( source->silIndexes, source->numIndexes );
		copy->silEdges = CopyStream( source->silEdges, source->numSilEdges );
		copy->shadowVertexes = CopyStream( source->shadowVertexes, source->numVerts );
		copy->facePlanes = CopyStream( source->facePlanes, source->numIndexes / 3 );
		copy->dominantTris = CopyStream( source->dominantTris, source->numVerts );
		copy->mirroredVerts = CopyStream( source->mirroredVerts, source->numMirroredVerts );
		copy->dupVerts = CopyStream( source->dupVerts, source->numDupVerts * 2 );
		if ( snapshotProfiling ) snapshotStats.geometryBytes += snapshotStats.copiedBytes - beforeStreams;
		copy->ambientCache = Cache( source->ambientCache );
		copy->indexCache = Cache( source->indexCache, true );
		copy->shadowCache = Cache( source->shadowCache );
		copy->lightingCache = Cache( source->lightingCache );
		copy->gpuSkinningBindPoseVerts = CopyStream( source->gpuSkinningBindPoseVerts, source->numGpuSkinningVerts );
		copy->gpuSkinningVerts = CopyStream( source->gpuSkinningVerts, source->numGpuSkinningVerts );
		copy->gpuSkinningJointPalette = CopyStream( source->gpuSkinningJointPalette, source->numGpuSkinningJoints * GPU_SKINNING_JOINT_FLOATS );
		copy->gpuSkinningJointPaletteAlloc = NULL;
		copy->previousPositions = CopyStream( source->previousPositions, source->numPreviousPositions );
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		copy->skinToModelTransforms = CopyStream( source->skinToModelTransforms, source->numSkinToModelTransforms * 16 );
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
		view->entityDef->parms.joints = CopyStream( def->parms.joints, def->parms.numJoints );
		view->entityDef->parms.remoteRenderView = Copy( def->parms.remoteRenderView );
	}

	void Surface( const drawSurf_t *source, bool finalized = false, bool prepare = false ) {
		if ( source != NULL && prepare ) {
			PrepareGeometry( source->geo );
			PrepareGeometry( source->pbrLightGeo );
			if ( finalized && source->classicDeform.initialized && source->classicDeform.valid ) {
				PrepareGeometry( source->classicDeform.sourceGeometry.geometry );
			}
			return;
		}
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
	void Chain( const drawSurf_t *surf, bool prepare = false ) {
		for ( ; surf != NULL; surf = surf->nextOnLight ) Surface( surf, false, prepare );
	}
};
}

static void R_SnapshotRenderViewInternal( viewDef_t *view ) {
	if ( view == NULL || view->frameSnapshot ) return;
	view->frameSnapshot = true;
	view->lightGridAvailable = view->renderWorld != NULL && view->renderWorld->AnyLightGridAvailable();
	viewSnapshot_t snapshot;
	// Preparation may mutate aliased streams. Freeze only after the complete
	// view has been prepared, including geometry reached through light chains.
	for ( int i = 0; i < view->numDrawSurfs; ++i ) snapshot.Surface( view->drawSurfs[i], true, true );
	for ( int i = 0; i < view->numOutlineDrawSurfs; ++i ) snapshot.Surface( view->outlineDrawSurfs[i], false, true );
	for ( const viewLight_t *light = view->viewLights; light; light = light->next ) {
		if ( light->lightDef != NULL ) snapshot.PrepareGeometry( light->lightDef->frustumTris );
		snapshot.PrepareGeometry( light->frustumTris );
		snapshot.Chain( light->globalShadows, true );
		snapshot.Chain( light->localShadows, true );
		snapshot.Chain( light->localInteractions, true );
		snapshot.Chain( light->globalInteractions, true );
		snapshot.Chain( light->translucentInteractions, true );
		snapshot.Chain( light->globalShadowMapStencilSupplements, true );
		snapshot.Chain( light->localShadowMapStencilSupplements, true );
		snapshot.Chain( light->localShadowMapCasters, true );
		snapshot.Chain( light->globalShadowMapCasters, true );
		snapshot.Chain( light->localShadowMapDynamicCasters, true );
		snapshot.Chain( light->globalShadowMapDynamicCasters, true );
		snapshot.Chain( light->localTranslucentShadowMapCasters, true );
		snapshot.Chain( light->globalTranslucentShadowMapCasters, true );
	}
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
	if ( !R_RenderThread_OverlapRequested() || view == NULL || view->frameSnapshot ) return;
	const unsigned long long begin = snapshotProfiling ? R_RendererMetrics_CpuClock() : 0;
	R_SnapshotRenderViewInternal( view );
	if ( snapshotProfiling ) {
		snapshotStats.microseconds += R_RendererMetrics_CpuClock() - begin;
		snapshotStats.views++;
		if ( snapshotStats.lastFrame != tr.frameCount ) {
			snapshotStats.lastFrame = tr.frameCount;
			snapshotStats.frames++;
		}
	}
}

void R_FrameSnapshotStats_f( const idCmdArgs &args ) {
	R_RenderThread_Sync( "snapshot stats" );
	if ( args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "reset" ) == 0 ) {
		snapshotStats = {};
		snapshotStats.lastFrame = -1;
		snapshotProfiling = true;
		return;
	}
	common->Printf( "Frame snapshot stats %s: frames=%llu views=%llu copies=%llu bytes=%llu geometryBytes=%llu cacheBytes=%llu cpuUs=%llu reusedBytes=%llu\n",
		args.Argc() > 1 ? args.Argv( 1 ) : "total", snapshotStats.frames, snapshotStats.views,
		snapshotStats.copies, snapshotStats.copiedBytes, snapshotStats.geometryBytes,
		snapshotStats.cacheBytes, snapshotStats.microseconds, snapshotStats.reusedBytes );
	snapshotProfiling = false;
}

void R_RetireRenderSnapshots( frameData_t *frame ) {
	retainedLeases[frame->slot].clear();
	if ( !r_vkSnapshotRetain.GetBool() ) retainedIndexes.Clear();
	for ( frameLightReceipt_s *receipt = frame->lightReceipts; receipt; receipt = receipt->next ) {
		idRenderLightLocal *source = receipt->source;
		const idRenderLightLocal *snapshot = receipt->snapshot;
		// Deletion of live light defs is deferred until this receipt retires.
		// Ignore a reused handle or a light whose parameters changed meanwhile.
		// A snapshot carries the flag the light had when it was taken, so the
		// flag alone says nothing new, and copied back it would undo a release
		// the front end decided meanwhile. Only a failure the back end stamped
		// on the snapshot after that is news: it marks the light and restarts
		// its wait.
		if ( snapshot->shadowMapStencilFallbackSticky && snapshot->world != NULL
			&& snapshot->index >= 0 && snapshot->index < snapshot->world->lightDefs.Num()
			&& snapshot->world->lightDefs[snapshot->index] == source
			&& source->lastModifiedFrameNum == snapshot->lastModifiedFrameNum
			&& snapshot->shadowMapStencilFallbackFrame > source->shadowMapStencilFallbackFrame ) {
			source->shadowMapStencilFallbackSticky = true;
			source->shadowMapStencilFallbackFrame = snapshot->shadowMapStencilFallbackFrame;
			source->shadowMapStencilFallbackCleanViews = 0;
		}
	}
	frame->lightReceipts = NULL;
}

void R_ClearRetainedSnapshotIndexes() {
	for ( auto &leases : retainedLeases ) { leases.clear(); leases.shrink_to_fit(); }
	retainedIndexes.Clear();
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
	// Distinct interaction/ambient records can point at identical streams.
	srfTriangles_t aliasGeometry = geometry;
	drawSurf_t aliasSurf = {};
	aliasSurf.geo = &aliasGeometry;
	aliasSurf.space = &space;
	drawSurf_t *draws[3] = { &surf, &surf, &aliasSurf };
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
	view.numDrawSurfs = 3;
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
	// The same addresses may already contain a new pose when another view is
	// assembled. Its lookup must not reuse the preceding view's frozen bytes.
	drawSurf_t laterSurf = {};
	laterSurf.geo = &geometry;
	drawSurf_t *laterDraws[1] = { &laterSurf };
	viewDef_t laterView = {};
	laterView.drawSurfs = laterDraws;
	laterView.numDrawSurfs = 1;
	R_SnapshotRenderViewInternal( &laterView );
	const bool passed = surf.geo == submitted && submitted != &geometry
		&& aliasSurf.geo != submitted && aliasSurf.geo->verts == submitted->verts
		&& aliasSurf.geo->indexes == submitted->indexes
		&& laterSurf.geo->verts != submitted->verts && laterSurf.geo->verts[0].xyz.x == 0
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
	common->Printf( "Renderer frame ownership self-test %s: geometry, cache bytes, stream aliases, later-view isolation, palettes, previous positions and defs\n", passed ? "passed" : "FAILED" );
	if ( !passed ) common->Error( "Renderer frame ownership self-test failed" );
}
