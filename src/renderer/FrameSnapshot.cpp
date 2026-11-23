// Copyright (C) 2026 DarkMatter Productions
#include "tr_local.h"
#include "GpuSkinning.h"
#include "RendererMetrics.h"
#include "FrameSnapshotCopy.h"
#include "FrameSnapshotRetain.h"
#include <cstdint>
#include <unordered_set>
#include <new>
#include <ctime>

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
	unsigned long long retainedVertexBytes;	// part of reusedBytes: static vertex-class bytes served across views
	unsigned long long temporaryIndexBytes;	// frame-temporary index bytes kept out of the retained cache
	unsigned long long omittedBytes;		// front-end-only stream bytes the snapshot did not take
	unsigned long long threadWork;		// SnapshotThreadWork() units spent freezing views
	int lastFrame;
};
snapshotStats_t snapshotStats = {};
// Work done on the calling thread rather than wall time: cycles on Windows,
// nanoseconds where POSIX has a thread CPU clock, otherwise 0. Time the
// thread spends preempted by other processes does not count, so builds can
// still be compared on a busy host; memory contention still does.
unsigned long long SnapshotThreadWork() {
#if defined( _WIN32 )
	ULONG64 cycles = 0;
	return QueryThreadCycleTime( GetCurrentThread(), &cycles ) ? static_cast<unsigned long long>( cycles ) : 0;
#elif defined( CLOCK_THREAD_CPUTIME_ID )
	timespec now;
	return clock_gettime( CLOCK_THREAD_CPUTIME_ID, &now ) == 0
		? static_cast<unsigned long long>( now.tv_sec ) * 1000000000ull + static_cast<unsigned long long>( now.tv_nsec ) : 0;
#else
	return 0;
#endif
}
const char *SnapshotThreadWorkUnit() {
#if defined( _WIN32 )
	return "cycles";
#elif defined( CLOCK_THREAD_CPUTIME_ID )
	return "ns";
#else
	return "none";
#endif
}
bool snapshotProfiling = false;
idCVar r_vkSnapshotRetain( "r_vkSnapshotRetain", "2", CVAR_RENDERER | CVAR_INTEGER,
	"retain validated immutable copies across overlap frames: 0 = per-view copies only, 1 = index streams, 2 = index streams and static vertex streams", 0, 2 );
idFrameSnapshotRetainedIndexes retainedIndexes;
idFrameSnapshotRetainedStreams retainedStreams;
// Either cache's versions; a submitted frame releases them at retirement.
// Each holder's epoch changes whenever it is emptied (see Lease).
std::vector<std::shared_ptr<const void>> retainedLeases[2];
std::uint64_t retainedLeaseEpochs[2] = { 1, 2 };
std::uint64_t retainedLeaseEpochCounter = 2;
void ReleaseRetainedLeases( int slot, bool shrink = false ) {
	retainedLeases[slot].clear();
	if ( shrink ) retainedLeases[slot].shrink_to_fit();
	retainedLeaseEpochs[slot] = ++retainedLeaseEpochCounter;
}

// One view's identities: geometry, caches, surfaces and entities. Reused by
// every view; only one view is frozen at a time.
idFrameSnapshotIdentities snapshotGeometry, snapshotCaches, snapshotSurfaces, snapshotEntities;
bool snapshotInProgress = false;

// The Vulkan consumer never reads these streams: silhouette indexes and
// edges, face planes, dominant triangles, shadow vertexes and the mirrored
// and duplicated vertex tables feed the front end's shadow volumes, culling
// and tangent derivation, which ran before the snapshot. The back end derives
// tangents only to repair GPU skinning a view deferred, and preparation has
// already finished that for every snapshot copy. The debug tools below read
// some of them, null-checked; a snapshot taken while one is enabled keeps
// them all. Omitting them leaves the counts and nulls the pointers, so a
// reader that was missed fails at once rather than reading a live stream.
bool SnapshotKeepsFrontEndStreams() {
	return r_showEdges.GetBool() || r_showDominantTri.GetBool() || r_showTextureVectors.GetFloat() != 0.0f;
}

// Candidates for cross-view retention are the vertex-class streams of static
// geometry: a world area or a static model drawn through its own persistent
// triangle record. Everything posed or deformed keeps its per-view copy:
// - an entity with a dynamic model, joints or a non-static model class
//   (animated meshes, effects, liquids, beams, GUI and other generated models);
// - frame-allocated triangle records: material deforms, decals, overlays and
//   dynamic light or shadow tris, whatever entity draws them;
// - surfaces with a GPU-skinning sidecar, captured previous positions or a
//   packed MD5R mesh, and every frame-temporary vertex cache.
// The classification selects candidates only; idFrameSnapshotRetainedStreams
// still compares the complete range before it reuses a version.
bool StableSpace( const viewEntity_t *space ) {
	const idRenderEntityLocal *def = space != NULL ? space->entityDef : NULL;
	return def != NULL && def->dynamicModel == NULL && def->parms.joints == NULL
		&& def->parms.hModel != NULL && def->parms.hModel->IsDynamicModel() == DM_STATIC;
}

bool StableGeometry( const srfTriangles_t *tri ) {
	return !tri->deformedSurface && !tri->gpuSkinningDeferred && tri->numGpuSkinningVerts == 0
		&& tri->gpuSkinningJointPalette == NULL && tri->previousPositions == NULL
		&& !R_TriHasPrimBatchMesh( tri ) && !R_FrameMemoryContains( tri );
}

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
	idFrameSnapshotIdentities &geometry = snapshotGeometry;
	idFrameSnapshotIdentities &caches = snapshotCaches;
	idFrameSnapshotIdentities &surfaces = snapshotSurfaces;
	idFrameSnapshotIdentities &entities = snapshotEntities;
	const bool retainStreams = r_vkSnapshotRetain.GetInteger() >= 2;
	const bool frontEndStreams = SnapshotKeepsFrontEndStreams();
	const std::uint64_t leaseEpoch = retainedLeaseEpochs[frameData->slot];
	std::vector<std::shared_ptr<const void>> &leases = retainedLeases[frameData->slot];
	viewSnapshot_t( const viewSnapshot_t & ) = delete;
	viewSnapshot_t &operator=( const viewSnapshot_t & ) = delete;
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
	// A vertex-class stream of geometry classified as static. A range this view
	// already froze is taken from the view's lookup, so light tris sharing their
	// ambient surface's vertices validate them once per view.
	template<class T> T *StableStream( const T *source, size_t count, bool stable ) {
		if ( !stable || !retainStreams ) return CopyStream( source, count );
		if ( source == NULL || count == 0 ) return NULL;
		if ( count > ( idMath::INT_MAX - 16 ) / sizeof( T ) ) common->FatalError( "retained stream size overflow" );
		const size_t bytes = count * sizeof( T );
		if ( void *known = streamCopies.Find( source, bytes ) ) {
			if ( snapshotProfiling ) snapshotStats.reusedBytes += bytes;
			return static_cast<T *>( known );
		}
		bool reused = false;
		const void *leased = retainedStreams.Lease( source, bytes, leaseEpoch, leases, reused );
		if ( leased == nullptr ) return CopyStream( source, count );
		void *data = const_cast<void *>( leased );
		streamCopies.Remember( source, bytes, data,
			[]( size_t size ) { return R_FrameAlloc( static_cast<int>( size ) ); } );
		if ( snapshotProfiling ) {
			if ( reused ) { snapshotStats.reusedBytes += bytes; snapshotStats.retainedVertexBytes += bytes; }
			else { snapshotStats.copiedBytes += bytes; snapshotStats.copies++; }
		}
		return static_cast<T *>( data );
	}
public:
	viewSnapshot_t() {
		if ( snapshotInProgress ) common->FatalError( "render snapshot re-entered" );
		snapshotInProgress = true;
		geometry.Begin(); caches.Begin(); surfaces.Begin(); entities.Begin();
	}
	~viewSnapshot_t() { snapshotInProgress = false; }
	// temporary: the bytes are frame-temporary (frame memory or a temporary
	// vertex cache). Their addresses are refilled every frame, so retaining
	// them only displaced the persistent streams: the working set exceeded the
	// cache's entry cap and least-recently-used eviction then missed on nearly
	// every acquisition. They take the ordinary per-view copy.
	template<class T> T *Indexes( const T *source, size_t count, bool temporary = false ) {
		if ( source == NULL || count == 0 ) return NULL;
		if ( count > ( idMath::INT_MAX - 16 ) / sizeof( T ) ) common->FatalError( "retained index size overflow" );
		const size_t bytes = count * sizeof( T );
		const bool retain = r_vkSnapshotRetain.GetBool();
		if ( retain && ( temporary || R_FrameMemoryContains( source ) ) ) {
			if ( snapshotProfiling ) snapshotStats.temporaryIndexBytes += bytes;
		} else if ( retain ) {
			if ( void *known = streamCopies.Find( source, bytes ) ) {
				if ( snapshotProfiling ) snapshotStats.reusedBytes += bytes;
				return static_cast<T *>( known );
			}
			bool reused = false;
			const std::uint32_t *leased = retainedIndexes.Lease( source, bytes, leaseEpoch, leases, reused );
			if ( leased != nullptr ) {
				void *data = const_cast<std::uint32_t *>( leased );
				streamCopies.Remember( source, bytes, data,
					[]( size_t size ) { return R_FrameAlloc( static_cast<int>( size ) ); } );
				if ( snapshotProfiling ) {
					if ( reused ) snapshotStats.reusedBytes += bytes;
					else { snapshotStats.copiedBytes += bytes; snapshotStats.copies++; }
				}
				return static_cast<T *>( data );
			}
		}
		return CopyStream( source, count );
	}
	vertCache_t *Cache( const vertCache_t *source, bool indexStream = false, bool stable = false ) {
		if ( source == NULL ) return NULL;
		if ( void *found = caches.Find( source ) ) return static_cast<vertCache_t *>( found );
		vertCache_t *copy = Copy( source );
		caches.Insert( source ) = copy;
		// Vulkan consumes CPU staging bytes. Owning the bytes as well as the
		// header permits skinning fallback to repair the submitted stream.
		if ( source->virtMem != NULL ) {
			const unsigned long long before = snapshotStats.copiedBytes;
			const byte *bytes = static_cast<const byte *>( source->virtMem ) + source->offset;
			copy->virtMem = indexStream ? Indexes( bytes, source->size, source->tag == TAG_TEMP )
				: StableStream( bytes, source->size, stable && source->tag != TAG_TEMP );
			if ( snapshotProfiling ) snapshotStats.cacheBytes += snapshotStats.copiedBytes - before;
			copy->offset = 0;
		}
		copy->tag = TAG_TEMP;
		copy->user = NULL;
		copy->next = copy->prev = NULL;
		return copy;
	}

	void PrepareGeometry( const srfTriangles_t *source ) {
		bool inserted = false;
		if ( source == NULL ) return;
		geometry.Insert( source, &inserted );
		if ( !inserted ) return;
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

	// stableSpace: the drawing entity is static. A light tri or a deform source
	// passes it on to the ambient surface it references.
	srfTriangles_t *Geometry( const srfTriangles_t *source, bool stableSpace = false ) {
		if ( source == NULL ) return NULL;
		if ( void *found = geometry.Find( source ) ) return static_cast<srfTriangles_t *>( found );
		srfTriangles_t *copy = Copy( source );
		geometry.Insert( source ) = copy;
		const bool stable = stableSpace && retainStreams && StableGeometry( source );
		const unsigned long long beforeStreams = snapshotStats.copiedBytes;
		copy->verts = StableStream( source->verts, source->numVerts, stable );
		copy->indexes = Indexes( source->indexes, source->numIndexes );
		// A copy still deferring its skinning would be repaired on the render
		// thread, which derives tangents from these tables. Preparation
		// finishes that first, so this only guards a geometry it never saw.
		if ( frontEndStreams || source->gpuSkinningDeferred ) {
			copy->silIndexes = Indexes( source->silIndexes, source->numIndexes );
			copy->silEdges = StableStream( source->silEdges, source->numSilEdges, stable );
			copy->shadowVertexes = StableStream( source->shadowVertexes, source->numVerts, stable );
			copy->facePlanes = StableStream( source->facePlanes, source->numIndexes / 3, stable );
			copy->dominantTris = StableStream( source->dominantTris, source->numVerts, stable );
			copy->mirroredVerts = StableStream( source->mirroredVerts, source->numMirroredVerts, stable );
			copy->dupVerts = StableStream( source->dupVerts, source->numDupVerts * 2, stable );
		} else {
			if ( snapshotProfiling ) {
				auto bytes = []( const void *stream, size_t size ) { return stream != NULL ? static_cast<unsigned long long>( size ) : 0ull; };
				snapshotStats.omittedBytes += bytes( source->silIndexes, source->numIndexes * sizeof( glIndex_t ) )
					+ bytes( source->silEdges, source->numSilEdges * sizeof( silEdge_t ) )
					+ bytes( source->shadowVertexes, source->numVerts * sizeof( shadowCache_t ) )
					+ bytes( source->facePlanes, ( source->numIndexes / 3 ) * sizeof( idPlane ) )
					+ bytes( source->dominantTris, source->numVerts * sizeof( dominantTri_t ) )
					+ bytes( source->mirroredVerts, source->numMirroredVerts * sizeof( int ) )
					+ bytes( source->dupVerts, source->numDupVerts * 2 * sizeof( int ) );
			}
			copy->silIndexes = NULL;
			copy->silEdges = NULL;
			copy->shadowVertexes = NULL;
			copy->facePlanes = NULL;
			copy->dominantTris = NULL;
			copy->mirroredVerts = NULL;
			copy->dupVerts = NULL;
		}
		if ( snapshotProfiling ) snapshotStats.geometryBytes += snapshotStats.copiedBytes - beforeStreams;
		copy->ambientCache = Cache( source->ambientCache, false, stable );
		copy->indexCache = Cache( source->indexCache, true );
		copy->shadowCache = Cache( source->shadowCache, false, stable );
		copy->lightingCache = Cache( source->lightingCache, false, stable );
		copy->gpuSkinningBindPoseVerts = CopyStream( source->gpuSkinningBindPoseVerts, source->numGpuSkinningVerts );
		copy->gpuSkinningVerts = CopyStream( source->gpuSkinningVerts, source->numGpuSkinningVerts );
		copy->gpuSkinningJointPalette = CopyStream( source->gpuSkinningJointPalette, source->numGpuSkinningJoints * GPU_SKINNING_JOINT_FLOATS );
		copy->gpuSkinningJointPaletteAlloc = NULL;
		copy->previousPositions = CopyStream( source->previousPositions, source->numPreviousPositions );
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		copy->skinToModelTransforms = CopyStream( source->skinToModelTransforms, source->numSkinToModelTransforms * 16 );
		copy->skinToModelTransformsAlloc = NULL;
#endif
		copy->ambientSurface = Geometry( source->ambientSurface, stableSpace );
		copy->topAmbientSurface = Geometry( source->topAmbientSurface, stableSpace );
		copy->nextDeferredFree = NULL;
		return copy;
	}

	void Entity( const viewEntity_t *source ) {
		bool inserted = false;
		if ( source == NULL ) return;
		entities.Insert( source, &inserted );
		if ( !inserted || source->entityDef == NULL ) return;
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
		bool inserted = false;
		if ( source == NULL ) return;
		surfaces.Insert( source, &inserted );
		if ( !inserted ) return;
		drawSurf_t *surf = const_cast<drawSurf_t *>( source );
		const bool stableSpace = retainStreams && StableSpace( surf->space );
		Entity( surf->space );
		// Interaction/volume surfaces do not initialize the embedded deform
		// record. Only finalized ambient draws may carry a source to rebase.
		const srfTriangles_t *deformSource = finalized && surf->classicDeform.initialized && surf->classicDeform.valid
			? Geometry( surf->classicDeform.sourceGeometry.geometry, stableSpace ) : NULL;
		surf->geo = Geometry( surf->geo, stableSpace );
		if ( finalized ) R_ClassicDeformDomain_RebaseFrameCopy( surf->classicDeform, deformSource, surf->geo );
		surf->pbrLightGeo = Geometry( surf->pbrLightGeo, stableSpace );
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
	const unsigned long long beginWork = snapshotProfiling ? SnapshotThreadWork() : 0;
	R_SnapshotRenderViewInternal( view );
	if ( snapshotProfiling ) {
		snapshotStats.microseconds += R_RendererMetrics_CpuClock() - begin;
		snapshotStats.threadWork += SnapshotThreadWork() - beginWork;
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
	common->Printf( "Frame snapshot stats %s: frames=%llu views=%llu copies=%llu bytes=%llu geometryBytes=%llu cacheBytes=%llu cpuUs=%llu threadWork=%llu threadWorkUnit=%s reusedBytes=%llu"
		" retainedVertexBytes=%llu temporaryIndexBytes=%llu omittedBytes=%llu indexEntries=%llu indexResidentBytes=%llu vertexEntries=%llu vertexResidentBytes=%llu leases=%llu\n",
		args.Argc() > 1 ? args.Argv( 1 ) : "total", snapshotStats.frames, snapshotStats.views,
		snapshotStats.copies, snapshotStats.copiedBytes, snapshotStats.geometryBytes,
		snapshotStats.cacheBytes, snapshotStats.microseconds, snapshotStats.threadWork, SnapshotThreadWorkUnit(), snapshotStats.reusedBytes,
		snapshotStats.retainedVertexBytes, snapshotStats.temporaryIndexBytes, snapshotStats.omittedBytes,
		static_cast<unsigned long long>( retainedIndexes.Size() ), static_cast<unsigned long long>( retainedIndexes.ResidentBytes() ),
		static_cast<unsigned long long>( retainedStreams.Size() ), static_cast<unsigned long long>( retainedStreams.ResidentBytes() ),
		static_cast<unsigned long long>( retainedLeases[0].size() + retainedLeases[1].size() ) );
	snapshotProfiling = false;
}

void R_RetireRenderSnapshots( frameData_t *frame ) {
	ReleaseRetainedLeases( frame->slot );
	// Lowering the selection drops the cached versions; submitted leases stay.
	const int retain = r_vkSnapshotRetain.GetInteger();
	if ( retain < 1 ) retainedIndexes.Clear();
	if ( retain < 2 ) retainedStreams.Clear();
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

// Frame shutdown, and every teardown that frees or purges what the cached
// versions were keyed by (vertex-cache purge on a video restart, world free):
// those entries could only age out under pressure. Submitted leases stay.
void R_ClearRetainedSnapshotStreams() {
	for ( int slot = 0; slot < 2; ++slot ) ReleaseRetainedLeases( slot, true );
	retainedIndexes.Clear();
	retainedStreams.Clear();
	snapshotGeometry.Release(); snapshotCaches.Release(); snapshotSurfaces.Release(); snapshotEntities.Release();
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
	// Static geometry keeps one validated version across views while its bytes
	// are unchanged. A rewrite must reach neither that version nor a later
	// view's bytes: the later view takes its own copy of the new contents.
	bool retainedPassed = true;
	if ( r_vkSnapshotRetain.GetInteger() >= 2 ) {
		// Stack fixtures reuse their addresses between runs of this command, and
		// the cache remembers a rewritten range. Start and end without them;
		// versions leased to a submitted frame are unaffected.
		retainedStreams.Clear();
		idDrawVert staticVerts[2] = {};
		staticVerts[0].xyz.Set( 21, 22, 23 );
		byte staticCacheBytes[sizeof( staticVerts )];
		memcpy( staticCacheBytes, staticVerts, sizeof( staticVerts ) );
		vertCache_t staticCache = {};
		staticCache.virtMem = staticCacheBytes;
		staticCache.size = sizeof( staticVerts );
		staticCache.tag = TAG_USED;
		glIndex_t staticIndexes[3] = { 0, 1, 0 };
		srfTriangles_t staticGeometry = {};
		staticGeometry.verts = staticVerts;
		staticGeometry.numVerts = 2;
		staticGeometry.indexes = staticIndexes;
		staticGeometry.numIndexes = 3;
		staticGeometry.ambientCache = &staticCache;
		idRenderEntityLocal staticEntity;
		staticEntity.parms.hModel = renderModelManager->DefaultModel();
		drawSurf_t staticSurfs[4] = {};
		viewEntity_t staticSpaces[4] = {};
		auto freeze = [&]( int index ) {
			staticSpaces[index].entityDef = &staticEntity;
			staticSurfs[index].geo = &staticGeometry;
			staticSurfs[index].space = &staticSpaces[index];
			drawSurf_t *list[1] = { &staticSurfs[index] };
			viewDef_t staticView = {};
			staticView.drawSurfs = list;
			staticView.numDrawSurfs = 1;
			staticView.viewEntitys = &staticSpaces[index];
			R_SnapshotRenderViewInternal( &staticView );
			return staticSurfs[index].geo;
		};
		const srfTriangles_t *first = freeze( 0 );
		const srfTriangles_t *second = freeze( 1 );
		staticVerts[0].xyz.Zero();
		memset( staticCacheBytes, 0, sizeof( staticCacheBytes ) );
		const srfTriangles_t *rewritten = freeze( 2 );
		// The same static entity drawing frame-temporary (deformed) geometry.
		srfTriangles_t *deformed = static_cast<srfTriangles_t *>( R_ClearedFrameAlloc( sizeof( srfTriangles_t ) ) );
		deformed->verts = staticVerts;
		deformed->numVerts = 2;
		staticSpaces[3].entityDef = &staticEntity;
		retainedPassed = staticEntity.parms.hModel != NULL && StableSpace( &staticSpaces[3] )
			&& StableGeometry( &staticGeometry ) && !StableGeometry( deformed )
			&& first != &staticGeometry && second != first
			&& first->verts != staticVerts && second->verts == first->verts
			&& first->ambientCache != second->ambientCache
			&& first->ambientCache->virtMem == second->ambientCache->virtMem
			&& first->ambientCache->virtMem != staticCacheBytes
			&& first->verts[0].xyz.x == 21
			&& static_cast<const idDrawVert *>( first->ambientCache->virtMem )[0].xyz.x == 21
			&& rewritten->verts != first->verts && rewritten->verts != staticVerts
			&& rewritten->verts[0].xyz.x == 0
			&& rewritten->ambientCache->virtMem != first->ambientCache->virtMem
			&& static_cast<const idDrawVert *>( rewritten->ambientCache->virtMem )[0].xyz.x == 0;
		retainedStreams.Clear();
	}
	// These fixtures are stack-owned, and are never eligible for light receipts.
	frameData->lightReceipts = savedReceipts;
	common->Printf( "Renderer frame ownership self-test %s: geometry, cache bytes, stream aliases, later-view isolation, palettes, previous positions and defs\n", passed ? "passed" : "FAILED" );
	common->Printf( "Renderer frame ownership self-test %s: static vertex streams shared across views, rewrites isolated, deformed geometry excluded (r_vkSnapshotRetain %d)\n",
		!retainedPassed ? "FAILED" : r_vkSnapshotRetain.GetInteger() >= 2 ? "passed" : "skipped", r_vkSnapshotRetain.GetInteger() );
	if ( !passed || !retainedPassed ) common->Error( "Renderer frame ownership self-test failed" );
}
