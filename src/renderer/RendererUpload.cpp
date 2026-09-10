// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RendererUpload.h"
#include "RendererMetrics.h"
#include "GLStateCache.h"
#include "RendererResourceSettings.h"
#include <atomic>
#include <limits>

static const int RENDERER_UPLOAD_MIN_FRAME_BUFFERS = 3;
static const int RENDERER_UPLOAD_MIN_MEGS = 1;
static const int RENDERER_UPLOAD_MAX_MEGS = 128;
static const int RENDERER_UPLOAD_FENCE_WAIT_RETRIES = 2;
static const GLuint64 RENDERER_UPLOAD_FENCE_WAIT_NS = 1000000ull;

// buffer-name pool limits: per-frame regenerated surfaces are tens to a few
// hundred KB, so a generous per-buffer cap keeps PurgeAll's world-sized
// buffers from pinning storage in the pool
static const int RENDERER_UPLOAD_POOL_MAX_NAMES = 256;
static const int RENDERER_UPLOAD_POOL_MAX_BUFFER_BYTES = 4 << 20;
static const int RENDERER_UPLOAD_POOL_MAX_TOTAL_BYTES = 32 << 20;

static idUploadManager rg_uploadManager;
static std::atomic<uint64_t> rg_uploadStorageGeneration{1};

static uint64_t R_RendererUpload_NextStorageGeneration() {
	uint64_t value = rg_uploadStorageGeneration.load();
	while (value != (std::numeric_limits<uint64_t>::max)()) {
		if (rg_uploadStorageGeneration.compare_exchange_weak(value, value + 1)) return value;
	}
	return 0;
}

// Drain a bounded number of native errors. They are failures for this storage
// operation, including pre-existing errors; none may turn into a ready receipt.
static bool R_RendererUpload_StorageErrorsClear() {
	bool clean = true;
	for (int i = 0; i < 32; ++i) {
		if (glGetError() == GL_NO_ERROR) return clean;
		clean = false;
	}
	return false;
}

static bool R_RendererUpload_BoundStorageMatches(int expectedBytes) {
	if (glGetBufferParameterivARB == NULL || expectedBytes <= 0) return false;
	GLint actualBytes = 0;
	glGetBufferParameterivARB(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &actualBytes);
	return R_RendererUpload_StorageErrorsClear() && actualBytes == expectedBytes;
}

static void R_RendererUpload_DeleteBufferName( unsigned int &vbo ) {
	if ( vbo == 0 ) {
		return;
	}
	if ( glDeleteBuffersARB != NULL ) {
		glDeleteBuffersARB( 1, &vbo );
	}
	vbo = 0;
}

idBufferAllocator::idBufferAllocator()
	: pooledBytes( 0 )
	, capacityBytes( 0 )
	, frameStaticUploadBytes( 0 )
	, frameStaticAllocations( 0 )
	, staticBuffersLive( 0 )
	, staticBytesLive( 0 )
	, persistentMapped( false ) {
}

void idBufferAllocator::Init( int bytes, bool persistent ) {
	capacityBytes = bytes;
	frameStaticUploadBytes = 0;
	frameStaticAllocations = 0;
	staticBuffersLive = 0;
	staticBytesLive = 0;
	persistentMapped = persistent;
	// drop (do NOT glDelete) any leftover pooled names: a fresh Init may run
	// under a new GL context where the old names are meaningless
	for ( int i = 0; i < 2; i++ ) {
		pooledNames[i].SetNum( 0, false );
		pooledSizes[i].SetNum( 0, false );
	}
	pooledBytes = 0;
}

void idBufferAllocator::Shutdown( void ) {
	// the GL context is still current here (upload shutdown precedes
	// GLimp_Shutdown in both the vid_restart and full shutdown paths)
	DrainPool();
	capacityBytes = 0;
	frameStaticUploadBytes = 0;
	frameStaticAllocations = 0;
	staticBuffersLive = 0;
	staticBytesLive = 0;
	persistentMapped = false;
}

bool idBufferAllocator::PoolEnabled( void ) const {
	return r_rendererUploadBufferPool.GetBool();
}

void idBufferAllocator::DrainPool( void ) {
	bool deletedAny = false;
	for ( int i = 0; i < 2; i++ ) {
		for ( int j = 0; j < pooledNames[i].Num(); j++ ) {
			unsigned int vbo = pooledNames[i][j];
			R_RendererUpload_DeleteBufferName( vbo );
			deletedAny = true;
		}
		pooledNames[i].SetNum( 0, false );
		pooledSizes[i].SetNum( 0, false );
	}
	if ( deletedAny ) {
		// deleting bound buffers implicitly resets the GL binding to zero
		idVertexCache::InvalidateBufferBindings();
		R_GLStateCache_InvalidateBufferBinding( GL_ARRAY_BUFFER, "renderer upload pool drain" );
		R_GLStateCache_InvalidateBufferBinding( GL_ELEMENT_ARRAY_BUFFER, "renderer upload pool drain" );
	}
	pooledBytes = 0;
}

void idBufferAllocator::BeginFrame( void ) {
	frameStaticUploadBytes = 0;
	frameStaticAllocations = 0;
	// eager drain when the pool cvar was switched off, so the rollback /
	// A-B toggle takes effect immediately instead of waiting for the next
	// static allocation (the GL context is current here)
	if ( !PoolEnabled() && ( pooledNames[0].Num() + pooledNames[1].Num() ) > 0 ) {
		DrainPool();
	}
}

bool idBufferAllocator::AllocStaticBuffer( void *data, int bytes, bool indexBuffer, bool streamDraw, unsigned int &vbo ) {
	if ( bytes <= 0 ) {
		return false;
	}

	if ( vbo == 0 ) {
		const int pool = indexBuffer ? 1 : 0;
		if ( !PoolEnabled() ) {
			DrainPool();	// cvar was toggled off with names still pooled
		}
		const int numPooled = pooledNames[pool].Num();
		if ( numPooled > 0 ) {
			// recycle the name; glBufferData below re-specifies the storage
			// (orphaning), which is safe against in-flight GPU reads
			vbo = pooledNames[pool][numPooled - 1];
			pooledBytes -= pooledSizes[pool][numPooled - 1];
			pooledNames[pool].SetNum( numPooled - 1, false );
			pooledSizes[pool].SetNum( numPooled - 1, false );
		} else {
			glGenBuffersARB( 1, &vbo );
			if ( vbo == 0 ) {
				return false;
			}
		}
		staticBuffersLive++;
	}

	const GLenum target = indexBuffer ? GL_ELEMENT_ARRAY_BUFFER_ARB : GL_ARRAY_BUFFER_ARB;
	const GLenum usage = streamDraw ? GL_STREAM_DRAW_ARB : GL_STATIC_DRAW_ARB;
	if ( indexBuffer ) {
		idVertexCache::BindIndexBuffer( vbo );
	} else {
		idVertexCache::BindArrayBuffer( vbo );
	}
	glBufferDataARB( target, (GLsizeiptrARB)bytes, data, usage );
	R_GLStateCache_InvalidateBufferBinding( target, "renderer upload static buffer" );

	frameStaticUploadBytes += bytes;
	frameStaticAllocations++;
	staticBytesLive += bytes;
	return true;
}

void idBufferAllocator::FreeStaticBuffer( unsigned int &vbo, int bytes, bool indexBuffer ) {
	if ( vbo == 0 ) {
		return;
	}

	const int pool = indexBuffer ? 1 : 0;
	if ( PoolEnabled()
			&& bytes > 0
			&& bytes <= RENDERER_UPLOAD_POOL_MAX_BUFFER_BYTES
			&& pooledBytes + bytes <= RENDERER_UPLOAD_POOL_MAX_TOTAL_BYTES
			&& pooledNames[pool].Num() < RENDERER_UPLOAD_POOL_MAX_NAMES ) {
		// keep the name (and its orphan-ready storage) for the next alloc;
		// no GL calls and no binding invalidation: the buffer still exists,
		// so any binding shadow that references it remains valid
		pooledNames[pool].Append( vbo );
		pooledSizes[pool].Append( bytes );
		pooledBytes += bytes;
		vbo = 0;
	} else {
		R_RendererUpload_DeleteBufferName( vbo );
		// deleting a bound buffer implicitly unbinds the name from EVERY
		// target it is bound to, so invalidate both modern shadows (matches
		// DrainPool; the legacy call already covers both legacy shadows)
		idVertexCache::InvalidateBufferBindings();
		R_GLStateCache_InvalidateBufferBinding( GL_ARRAY_BUFFER, "renderer upload static buffer free" );
		R_GLStateCache_InvalidateBufferBinding( GL_ELEMENT_ARRAY_BUFFER, "renderer upload static buffer free" );
		vbo = 0;
	}

	if ( staticBuffersLive > 0 ) {
		staticBuffersLive--;
	}
	if ( bytes > 0 ) {
		staticBytesLive -= bytes;
		if ( staticBytesLive < 0 ) {
			staticBytesLive = 0;
		}
	}
}

int idBufferAllocator::Capacity( void ) const {
	return capacityBytes;
}

bool idBufferAllocator::IsPersistentMapped( void ) const {
	return persistentMapped;
}

int idBufferAllocator::FrameStaticUploadBytes( void ) const {
	return frameStaticUploadBytes;
}

int idBufferAllocator::FrameStaticAllocations( void ) const {
	return frameStaticAllocations;
}

int idBufferAllocator::StaticBuffersLive( void ) const {
	return staticBuffersLive;
}

int idBufferAllocator::StaticBytesLive( void ) const {
	return staticBytesLive;
}

int idBufferAllocator::StaticBuffersPooled( void ) const {
	return pooledNames[0].Num() + pooledNames[1].Num();
}

int idBufferAllocator::StaticBytesPooled( void ) const {
	return pooledBytes;
}

idRingBuffer::idRingBuffer()
	: capacityBytes( 0 )
	, head( 0 )
	, highWater( 0 )
	, overflowBytes( 0 )
	, persistentMapped( false ) {
}

void idRingBuffer::Init( int bytes, bool persistent ) {
	capacityBytes = bytes;
	head = 0;
	highWater = 0;
	overflowBytes = 0;
	persistentMapped = persistent;
}

void idRingBuffer::Shutdown( void ) {
	capacityBytes = 0;
	head = 0;
	highWater = 0;
	overflowBytes = 0;
	persistentMapped = false;
}

void idRingBuffer::BeginFrame( void ) {
	head = 0;
	highWater = 0;
	overflowBytes = 0;
}

int idRingBuffer::Allocate( int bytes, int alignment, bool &wrapped ) {
	wrapped = false;
	if ( bytes <= 0 || capacityBytes <= 0 || bytes > capacityBytes ) {
		if ( bytes > 0 ) {
			overflowBytes += bytes;
		}
		return -1;
	}
	if ( alignment <= 0 ) {
		alignment = 1;
	}

	const int alignedHead = ( head + alignment - 1 ) & ~( alignment - 1 );
	if ( alignedHead + bytes > capacityBytes ) {
		overflowBytes += bytes;
		wrapped = true;
		return -1;
	} else {
		head = alignedHead;
	}

	const int offset = head;
	head += bytes;
	if ( head > highWater ) {
		highWater = head;
	}
	return offset;
}

void idRingBuffer::EndFrame( void ) {
}

int idRingBuffer::Capacity( void ) const {
	return capacityBytes;
}

int idRingBuffer::Used( void ) const {
	return head;
}

int idRingBuffer::HighWater( void ) const {
	return highWater;
}

int idRingBuffer::OverflowBytes( void ) const {
	return overflowBytes;
}

idLegacyStreamBuffer::idLegacyStreamBuffer() {
	memset( &stats, 0, sizeof( stats ) );
	stats.legacyBridge = true;
}

void idLegacyStreamBuffer::Init( bool useMapRange ) {
	memset( &stats, 0, sizeof( stats ) );
	stats.legacyBridge = true;
	stats.mapRangeFallback = useMapRange;
}

void idLegacyStreamBuffer::RecordUpload( int bytes ) {
	if ( bytes > 0 ) {
		stats.frameUploadBytes += bytes;
		R_RendererMetrics_AddUploadBytes( bytes );
	}
}

void idLegacyStreamBuffer::RecordStall( void ) {
	stats.frameStalls++;
	R_RendererMetrics_AddBufferStall();
}

void idLegacyStreamBuffer::EndFrame( void ) {
	stats.frameUploadBytes = 0;
	stats.frameStalls = 0;
}

const rendererUploadStats_t &idLegacyStreamBuffer::Stats( void ) const {
	return stats;
}

idUploadManager::idUploadManager() {
	memset( &stats, 0, sizeof( stats ) );
	memset( frameBuffers, 0, sizeof( frameBuffers ) );
	path = UPLOAD_PATH_DISABLED;
	currentFrameBuffer = 0;
	frameBufferCount = 0;
	initialized = false;
	hasSync = false;
}

void idUploadManager::Init( const renderBackendCaps_t &caps ) {
	Shutdown();
	storage.generation = R_RendererUpload_NextStorageGeneration();
	storage.requestedMegs = r_rendererUploadMegs.GetInteger();
	storage.requestedBuffers = r_rendererUploadFrameBuffers.GetInteger();

	const bool driverQuirkPersistentDisabled =
		( RendererDriverQuirks_LastReport().flags & RENDERER_DRIVER_QUIRK_DISABLE_PERSISTENT_UPLOADS ) != 0;
	const bool lowOverheadPersistentDefault =
		glConfig.renderFeatures.persistentMappedUploads &&
		r_rendererUploadPersistent.GetBool() &&
		!driverQuirkPersistentDisabled;
	const bool syncAvailable = caps.hasSync && glFenceSync != NULL && glClientWaitSync != NULL && glDeleteSync != NULL;
	// Persistent mappings reuse the same storage.  Without a complete fence
	// contract the CPU can overwrite bytes that the GPU is still consuming.
	const bool usePersistent = lowOverheadPersistentDefault && syncAvailable && caps.hasBufferStorage && caps.hasMapBufferRange && glBufferStorage != NULL && glMapBufferRange != NULL;
	const bool useMapRange = caps.hasMapBufferRange && glMapBufferRange != NULL;
	const int ringMegs = idMath::ClampInt( RENDERER_UPLOAD_MIN_MEGS, RENDERER_UPLOAD_MAX_MEGS, storage.requestedMegs );
	const int ringBytes = ringMegs * 1024 * 1024;
	frameBufferCount = idMath::ClampInt( RENDERER_UPLOAD_MIN_FRAME_BUFFERS, RENDERER_UPLOAD_MAX_FRAME_BUFFERS, storage.requestedBuffers );
	uploadPath_t requestedPath = UPLOAD_PATH_DISABLED;

	if ( caps.hasVBO ) {
		if ( usePersistent ) {
			requestedPath = UPLOAD_PATH_PERSISTENT;
		} else if ( useMapRange ) {
			requestedPath = UPLOAD_PATH_MAP_RANGE;
		} else {
			requestedPath = UPLOAD_PATH_SUBDATA;
		}
	}
	if ( requestedPath == UPLOAD_PATH_DISABLED ) {
		frameBufferCount = 0;
	}

	const int activeRingBytes = requestedPath == UPLOAD_PATH_DISABLED ? 0 : ringBytes;
	allocator.Init( activeRingBytes, requestedPath == UPLOAD_PATH_PERSISTENT );
	ring.Init( activeRingBytes, requestedPath == UPLOAD_PATH_PERSISTENT );
	legacy.Init( useMapRange );

	memset( &stats, 0, sizeof( stats ) );
	stats.ringSizeBytes = activeRingBytes;
	stats.ringBufferCount = frameBufferCount;
	stats.persistentMapped = requestedPath == UPLOAD_PATH_PERSISTENT;
	stats.lowOverheadPersistentDefault = lowOverheadPersistentDefault;
	stats.mapRangeFallback = requestedPath == UPLOAD_PATH_MAP_RANGE;
	stats.legacyBridge = true;
	stats.dynamicFrameBridge = false;
	stats.staticBufferAllocator = requestedPath != UPLOAD_PATH_DISABLED;
	// Only persistent storage reuses the same mapped backing and therefore
	// needs explicit proof that the GPU has released a frame slot.  The
	// map-range and subdata paths orphan their storage before reuse; carrying
	// persistent-path fences into those fallbacks can turn a recoverable busy
	// GPU (for example, while a capture application is sharing it) into an
	// unbounded driver wait.
	hasSync = requestedPath == UPLOAD_PATH_PERSISTENT && syncAvailable;
	stats.fenceSyncAvailable = hasSync;
	stats.driverQuirkPersistentDisabled = driverQuirkPersistentDisabled;
	if ( driverQuirkPersistentDisabled && glConfig.renderFeatures.persistentMappedUploads && r_rendererUploadPersistent.GetBool() ) {
		common->Printf(
			"Renderer upload manager: persistent mapped uploads disabled by driver quirk; using the orphaned map-range stream without persistent-path fences for capture interoperability\n" );
	}

	bool created = requestedPath != UPLOAD_PATH_DISABLED && storage.generation && CreateFrameBuffers(requestedPath);
	if (!created && requestedPath == UPLOAD_PATH_PERSISTENT && storage.generation) {
			common->Warning( "Renderer upload manager: persistent mapped stream failed, falling back to map-range streaming" );
			ShutdownFrameBuffers();
			requestedPath = useMapRange ? UPLOAD_PATH_MAP_RANGE : UPLOAD_PATH_SUBDATA;
			storage.persistentFallback = true;
			created = CreateFrameBuffers(requestedPath);
	}
	if (!created) {
		ShutdownFrameBuffers();
		path = UPLOAD_PATH_DISABLED;
		frameBufferCount = 0;
		ring.Shutdown();
		stats.ringSizeBytes = stats.ringBufferCount = 0;
		if (requestedPath != UPLOAD_PATH_DISABLED)
			common->Warning("Renderer upload manager: frame storage allocation failed; using legacy uploads");
	}
	stats.persistentMapped = path == UPLOAD_PATH_PERSISTENT;
	stats.mapRangeFallback = path == UPLOAD_PATH_MAP_RANGE;
	hasSync = path == UPLOAD_PATH_PERSISTENT && syncAvailable;
	stats.fenceSyncAvailable = hasSync;
	// Allocator bookkeeping must follow the actual fallback, too.
	allocator.Init(created ? activeRingBytes : 0, stats.persistentMapped);
	ring.Init(created ? activeRingBytes : 0, stats.persistentMapped);
	storage.bytesPerBuffer = created ? activeRingBytes : 0;
	storage.bufferCount = frameBufferCount;
	storage.path = static_cast<unsigned int>(path);
	storageReady = created;

	initialized = true;
	stats.dynamicFrameBridge = path != UPLOAD_PATH_DISABLED;
	stats.staticBufferAllocator = path != UPLOAD_PATH_DISABLED;
	UpdateAllocatorStats();

	const char *bridgeMode = "disabled";
	if ( path == UPLOAD_PATH_PERSISTENT ) {
		bridgeMode = "GL45 persistent-mapped capable";
	} else if ( path != UPLOAD_PATH_DISABLED ) {
		bridgeMode = "streaming";
	}

	common->Printf(
		"Renderer upload manager: %s legacy bridge, frameStream=%s, staticAllocator=%s, buffers=%d, ring=%dKB, sync=%s\n",
		bridgeMode,
		PathName(),
		stats.staticBufferAllocator ? "yes" : "no",
		frameBufferCount,
		stats.ringSizeBytes / 1024,
		hasSync ? "yes" : "no" );
}

void idUploadManager::Shutdown( void ) {
	storageReady = false;
	ShutdownFrameBuffers();
	allocator.Shutdown();
	ring.Shutdown();
	memset( &stats, 0, sizeof( stats ) );
	path = UPLOAD_PATH_DISABLED;
	currentFrameBuffer = 0;
	frameBufferCount = 0;
	initialized = false;
	hasSync = false;
	storage = {};
}

void idUploadManager::BeginFrame( int frameCount ) {
	stats.frameUploadBytes = 0;
	stats.frameStaticUploadBytes = 0;
	stats.frameStalls = 0;
	stats.frameAllocations = 0;
	stats.frameStaticAllocations = 0;
	stats.frameRingUsedBytes = 0;
	stats.frameRingHighWaterBytes = 0;
	stats.frameOverflowBytes = 0;
	stats.framePersistentWrites = 0;
	stats.frameMapRangeWrites = 0;
	stats.frameSubDataWrites = 0;
	stats.frameFencesSubmitted = 0;
	stats.frameFencesRetired = 0;
	stats.frameFenceSubmissionFailures = 0;
	stats.frameFenceWaits = 0;
	stats.frameFenceTimeouts = 0;
	stats.frameFenceFallbacks = 0;
	stats.frameFenceWaitFailures = 0;
	stats.frameBufferIndex = 0;
	allocator.BeginFrame();
	UpdateAllocatorStats();
	ring.BeginFrame();

	if ( path == UPLOAD_PATH_DISABLED || frameBufferCount <= 0 ) {
		return;
	}

	const int preferredFrameBuffer = frameCount % frameBufferCount;
	currentFrameBuffer = preferredFrameBuffer;
	SelectFrameBufferForFrame( preferredFrameBuffer );
	stats.frameBufferIndex = currentFrameBuffer;
	frameBuffer_t &frame = frameBuffers[currentFrameBuffer];

	if ( path != UPLOAD_PATH_PERSISTENT ) {
		storageReady = false;
		// The modern executor may have rebound GL_ARRAY_BUFFER through its state
		// cache since the legacy vertex-cache shadow was last updated. Force the
		// real buffer bind before orphaning this frame slot so the orphan cannot
		// accidentally apply to the most recently submitted geometry VBO.
		idVertexCache::InvalidateBufferBindings();
		idVertexCache::BindArrayBuffer( frame.vbo );
		const bool clean = R_RendererUpload_StorageErrorsClear();
		if (clean) {
			glBufferDataARB( GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)stats.ringSizeBytes, NULL, GL_STREAM_DRAW_ARB );
			storageReady = R_RendererUpload_BoundStorageMatches(stats.ringSizeBytes);
		}
		R_GLStateCache_InvalidateBufferBinding( GL_ARRAY_BUFFER, "renderer upload frame orphan" );
		if (!storageReady) {
			// Preserve already allocated static geometry; only the failed dynamic
			// stream retires and ordinary callers fall back to legacy uploads.
			ShutdownFrameBuffers();
			path = UPLOAD_PATH_DISABLED;
			frameBufferCount = 0;
			ring.Shutdown();
			stats.ringSizeBytes = stats.ringBufferCount = 0;
			stats.dynamicFrameBridge = stats.persistentMapped = stats.mapRangeFallback = false;
		}
	}
}

void idUploadManager::EndFrame( void ) {
	FenceCurrentFrame();
	stats.frameRingUsedBytes = ring.Used();
	stats.frameRingHighWaterBytes = ring.HighWater();
	// ring overflow is already accumulated per-allocation into
	// stats.frameOverflowBytes in AllocFrameTemp (which also counts the
	// frame.vbo==0 bytes the ring counter misses), so adding
	// ring.OverflowBytes() here would double-count regardless of read order;
	// the per-allocation site must stay the canonical counter because the
	// metrics are read before EndFrame runs
	UpdateAllocatorStats();
	legacy.EndFrame();
	ring.EndFrame();
}

bool idUploadManager::AllocFrameTemp( void *data, int bytes, int alignment, rendererUploadAllocation_t &allocation ) {
	memset( &allocation, 0, sizeof( allocation ) );

	if ( !initialized || path == UPLOAD_PATH_DISABLED || bytes <= 0 || data == NULL ) {
		return false;
	}

	bool wrapped = false;
	const int offset = ring.Allocate( bytes, alignment, wrapped );
	if ( offset < 0 ) {
		stats.frameOverflowBytes += bytes;
		return false;
	}

	frameBuffer_t &frame = frameBuffers[currentFrameBuffer];
	if ( frame.vbo == 0 ) {
		stats.frameOverflowBytes += bytes;
		return false;
	}

	// A persistent+coherent mapping is written through the pointer - no GL call
	// touches GL_ARRAY_BUFFER on that branch, so the real binding and both
	// binding shadows stay untouched. The map-range/subdata branches DO target
	// GL_ARRAY_BUFFER and can be interleaved with modern submissions that bind
	// it outside idVertexCache's redundant-bind shadow, so for those the
	// ownership transfer stays explicit before writing a range in the stream.
	const bool persistentWrite = path == UPLOAD_PATH_PERSISTENT && frame.mapped != NULL;
	if ( !persistentWrite ) {
		idVertexCache::InvalidateBufferBindings();
		idVertexCache::BindArrayBuffer( frame.vbo );
	}

	if ( persistentWrite ) {
		SIMDProcessor->Memcpy( frame.mapped + offset, data, bytes );
		stats.framePersistentWrites++;
	} else if ( path == UPLOAD_PATH_MAP_RANGE && glMapBufferRange != NULL ) {
		void *mapped = glMapBufferRange(
			GL_ARRAY_BUFFER,
			offset,
			bytes,
			GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
		if ( mapped != NULL ) {
			SIMDProcessor->Memcpy( mapped, data, bytes );
			glUnmapBuffer( GL_ARRAY_BUFFER );
			stats.frameMapRangeWrites++;
		 } else {
			glBufferSubDataARB( GL_ARRAY_BUFFER_ARB, offset, (GLsizeiptrARB)bytes, data );
			stats.frameSubDataWrites++;
		}
	} else {
		glBufferSubDataARB( GL_ARRAY_BUFFER_ARB, offset, (GLsizeiptrARB)bytes, data );
		stats.frameSubDataWrites++;
	}

	allocation.vbo = frame.vbo;
	allocation.offset = offset;
	allocation.size = bytes;
	allocation.persistentMapped = path == UPLOAD_PATH_PERSISTENT;
	allocation.mapRange = path == UPLOAD_PATH_MAP_RANGE;

	stats.frameAllocations++;
	stats.frameUploadBytes += bytes;
	stats.frameRingUsedBytes = ring.Used();
	stats.frameRingHighWaterBytes = ring.HighWater();
	R_RendererMetrics_AddUploadBytes( bytes );
	if ( !persistentWrite ) {
		// the legacy bind above changed GL_ARRAY_BUFFER behind the modern cache
		R_GLStateCache_InvalidateBufferBinding( GL_ARRAY_BUFFER, "renderer upload frame stream" );
	}
	return true;
}

bool idUploadManager::AllocStaticBuffer( void *data, int bytes, bool indexBuffer, bool streamDraw, unsigned int &vbo ) {
	if ( !initialized || !stats.staticBufferAllocator || bytes <= 0 ) {
		return false;
	}

	if ( !allocator.AllocStaticBuffer( data, bytes, indexBuffer, streamDraw, vbo ) ) {
		return false;
	}

	stats.frameUploadBytes += bytes;
	UpdateAllocatorStats();
	R_RendererMetrics_AddUploadBytes( bytes );
	return true;
}

void idUploadManager::FreeStaticBuffer( unsigned int &vbo, int bytes, bool indexBuffer ) {
	if ( vbo == 0 ) {
		return;
	}

	if ( !initialized || !stats.staticBufferAllocator ) {
		R_RendererUpload_DeleteBufferName( vbo );
		return;
	}

	allocator.FreeStaticBuffer( vbo, bytes, indexBuffer );
	UpdateAllocatorStats();
}

void idUploadManager::RecordLegacyUpload( int bytes ) {
	legacy.RecordUpload( bytes );
	if ( bytes > 0 ) {
		stats.frameUploadBytes += bytes;
	}
}

void idUploadManager::RecordLegacyStall( void ) {
	legacy.RecordStall();
	stats.frameStalls++;
}

const rendererUploadStats_t &idUploadManager::Stats( void ) const {
	return stats;
}

bool idUploadManager::DynamicFrameBridgeAvailable( void ) const {
	return initialized && path != UPLOAD_PATH_DISABLED;
}

bool idUploadManager::StaticBufferAllocatorAvailable( void ) const {
	return initialized && stats.staticBufferAllocator;
}

int idUploadManager::FrameCapacity( void ) const {
	return ring.Capacity();
}

bool idUploadManager::CreateFrameBuffers( uploadPath_t requestedPath ) {
	storageReady = false;
	path = requestedPath;
	if ( path == UPLOAD_PATH_DISABLED ) {
		return false;
	}

	const GLbitfield persistentFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT;
	const GLbitfield mapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
	if (!R_RendererUpload_StorageErrorsClear()) return false;

	for ( int i = 0; i < frameBufferCount; ++i ) {
		glGenBuffersARB( 1, &frameBuffers[i].vbo );
		if (!R_RendererUpload_StorageErrorsClear() || frameBuffers[i].vbo == 0) return false;
		idVertexCache::InvalidateBufferBindings();
		idVertexCache::BindArrayBuffer( frameBuffers[i].vbo );
		if (!R_RendererUpload_StorageErrorsClear()) return false;

		if ( path == UPLOAD_PATH_PERSISTENT ) {
			glBufferStorage( GL_ARRAY_BUFFER, (GLsizeiptr)stats.ringSizeBytes, NULL, persistentFlags );
			if (!R_RendererUpload_BoundStorageMatches(stats.ringSizeBytes)) return false;
			frameBuffers[i].mapped = static_cast<byte *>( glMapBufferRange( GL_ARRAY_BUFFER, 0, stats.ringSizeBytes, mapFlags ) );
			if ( !R_RendererUpload_StorageErrorsClear() || frameBuffers[i].mapped == NULL ) {
				return false;
			}
		} else {
			glBufferDataARB( GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)stats.ringSizeBytes, NULL, GL_STREAM_DRAW_ARB );
			if (!R_RendererUpload_BoundStorageMatches(stats.ringSizeBytes)) return false;
		}
	}

	idVertexCache::BindArrayBuffer( 0 );
	R_GLStateCache_InvalidateBufferBinding( GL_ARRAY_BUFFER, "renderer upload init" );
	return R_RendererUpload_StorageErrorsClear();
}

void idUploadManager::ShutdownFrameBuffers( void ) {
	storageReady = false;
	// the shadow may be stale at teardown (the modern executor binds the real
	// GL_ARRAY_BUFFER behind it), so force the unmap binds below to be real
	idVertexCache::InvalidateBufferBindings();
	for ( int i = 0; i < RENDERER_UPLOAD_MAX_FRAME_BUFFERS; ++i ) {
		if ( frameBuffers[i].fence != NULL && glDeleteSync != NULL ) {
			glDeleteSync( frameBuffers[i].fence );
			frameBuffers[i].fence = NULL;
		}
		if ( frameBuffers[i].vbo != 0 ) {
			if ( frameBuffers[i].mapped != NULL ) {
				idVertexCache::BindArrayBuffer( frameBuffers[i].vbo );
				glUnmapBuffer( GL_ARRAY_BUFFER );
				frameBuffers[i].mapped = NULL;
			}
			R_RendererUpload_DeleteBufferName( frameBuffers[i].vbo );
		}
	}
	idVertexCache::InvalidateBufferBindings();
	idVertexCache::BindArrayBuffer( 0 );
	R_GLStateCache_InvalidateBufferBinding( GL_ARRAY_BUFFER, "renderer upload shutdown" );
}

bool idUploadManager::QueryStorage(rendererUploadStorage_t& output) const {
	if (!R_ImagePolicyRendererThread() || !initialized || !storageReady || !storage.generation ||
		path == UPLOAD_PATH_DISABLED || storage.path != static_cast<unsigned int>(path) ||
		storage.bufferCount != frameBufferCount || storage.bytesPerBuffer != ring.Capacity() ||
		storage.bytesPerBuffer <= 0 || frameBufferCount < RENDERER_UPLOAD_MIN_FRAME_BUFFERS ||
		frameBufferCount > RENDERER_UPLOAD_MAX_FRAME_BUFFERS) return false;
	for (int i = 0; i < frameBufferCount; ++i) {
		if (!frameBuffers[i].vbo || (path == UPLOAD_PATH_PERSISTENT && !frameBuffers[i].mapped)) return false;
	}
	output = storage;
	return true;
}

bool R_RendererUpload_QueryStorage(rendererUploadStorage_t& output) {
	return rg_uploadManager.QueryStorage(output);
}

static bool R_RendererUpload_FenceSignaled( GLenum result ) {
	return result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED;
}

static void R_RendererUpload_FailSafeFenceWait( rendererUploadStats_t &stats ) {
	stats.frameFenceWaitFailures++;
	stats.frameStalls++;
	R_RendererMetrics_AddBufferStall();
	// A failed sync wait gives us no proof that the mapped range is idle.
	// Finish is deliberately conservative and only used on this driver-error
	// path; continuing to write would risk GPU-visible corruption.
	glFinish();
}

bool idUploadManager::RetireFrameFence( frameBuffer_t &frame, bool allowBlocking ) {
	if ( frame.fence == NULL || !hasSync ) {
		return true;
	}

	const GLenum quickWait = glClientWaitSync( frame.fence, 0, 0 );
	if ( R_RendererUpload_FenceSignaled( quickWait ) ) {
		glDeleteSync( frame.fence );
		frame.fence = NULL;
		stats.frameFencesRetired++;
		return true;
	}
	if ( quickWait == GL_WAIT_FAILED ) {
		R_RendererUpload_FailSafeFenceWait( stats );
		glDeleteSync( frame.fence );
		frame.fence = NULL;
		stats.frameFencesRetired++;
		return true;
	}
	if ( quickWait != GL_TIMEOUT_EXPIRED ) {
		R_RendererUpload_FailSafeFenceWait( stats );
		glDeleteSync( frame.fence );
		frame.fence = NULL;
		stats.frameFencesRetired++;
		return true;
	}

	stats.frameFenceTimeouts++;
	if ( !allowBlocking ) {
		return false;
	}

	stats.frameFenceWaits++;
	for ( int retry = 0; retry < RENDERER_UPLOAD_FENCE_WAIT_RETRIES; ++retry ) {
		const GLenum boundedWait = glClientWaitSync( frame.fence, GL_SYNC_FLUSH_COMMANDS_BIT, RENDERER_UPLOAD_FENCE_WAIT_NS );
		if ( R_RendererUpload_FenceSignaled( boundedWait ) ) {
			glDeleteSync( frame.fence );
			frame.fence = NULL;
			stats.frameFencesRetired++;
			return true;
		}
		if ( boundedWait == GL_WAIT_FAILED ) {
			R_RendererUpload_FailSafeFenceWait( stats );
			glDeleteSync( frame.fence );
			frame.fence = NULL;
			stats.frameFencesRetired++;
			return true;
		}
		if ( boundedWait == GL_TIMEOUT_EXPIRED ) {
			stats.frameFenceTimeouts++;
		}
	}

	stats.frameStalls++;
	R_RendererMetrics_AddBufferStall();
	const GLenum finalWait = glClientWaitSync( frame.fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED );
	if ( !R_RendererUpload_FenceSignaled( finalWait ) ) {
		stats.frameFenceWaitFailures++;
		glFinish();
	}
	glDeleteSync( frame.fence );
	frame.fence = NULL;
	stats.frameFencesRetired++;
	return true;
}

bool idUploadManager::SelectFrameBufferForFrame( int preferredFrameBuffer ) {
	if ( frameBufferCount <= 0 ) {
		return false;
	}
	preferredFrameBuffer = idMath::ClampInt( 0, frameBufferCount - 1, preferredFrameBuffer );
	currentFrameBuffer = preferredFrameBuffer;
	if ( RetireFrameFence( frameBuffers[currentFrameBuffer], false ) ) {
		return true;
	}

	// A ring slot does not have to be reused in modulo order.  Prefer any
	// already-idle slot before making the CPU wait for the nominal one.
	for ( int i = 1; i < frameBufferCount; ++i ) {
		const int candidate = ( preferredFrameBuffer + i ) % frameBufferCount;
		if ( RetireFrameFence( frameBuffers[candidate], false ) ) {
			currentFrameBuffer = candidate;
			stats.frameFenceFallbacks++;
			return true;
		}
	}

	currentFrameBuffer = preferredFrameBuffer;
	return RetireFrameFence( frameBuffers[currentFrameBuffer], true );
}

void idUploadManager::FenceCurrentFrame( void ) {
	if ( path == UPLOAD_PATH_DISABLED || !hasSync ) {
		return;
	}
	frameBuffer_t &frame = frameBuffers[currentFrameBuffer];
	if ( frame.vbo == 0 || frame.fence != NULL ) {
		return;
	}
	frame.fence = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
	if ( frame.fence != NULL ) {
		stats.frameFencesSubmitted++;
	} else {
		// Persistent storage must never be considered reusable without proof
		// that the GPU has finished consuming it. A failed fence allocation is
		// rare and already an error path, so finish conservatively here.
		stats.frameFenceSubmissionFailures++;
		stats.frameStalls++;
		R_RendererMetrics_AddBufferStall();
		glFinish();
	}
}

void idUploadManager::UpdateAllocatorStats( void ) {
	stats.frameStaticUploadBytes = allocator.FrameStaticUploadBytes();
	stats.frameStaticAllocations = allocator.FrameStaticAllocations();
	stats.staticBuffersLive = allocator.StaticBuffersLive();
	stats.staticBytesLive = allocator.StaticBytesLive();
	stats.staticBuffersPooled = allocator.StaticBuffersPooled();
	stats.staticBytesPooled = allocator.StaticBytesPooled();
}

const char *idUploadManager::PathName( void ) const {
	switch ( path ) {
	case UPLOAD_PATH_PERSISTENT:
		return "persistent";
	case UPLOAD_PATH_MAP_RANGE:
		return "map-range";
	case UPLOAD_PATH_SUBDATA:
		return "subdata";
	case UPLOAD_PATH_DISABLED:
	default:
		return "disabled";
	}
}

void R_RendererUpload_Init( const renderBackendCaps_t &caps ) {
	R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_RENDERER_UPLOAD_INIT );
	rg_uploadManager.Init( caps );
}

void R_RendererUpload_Shutdown( void ) {
	rg_uploadManager.Shutdown();
}

void R_RendererUpload_BeginFrame( int frameCount ) {
	rg_uploadManager.BeginFrame( frameCount );
}

void R_RendererUpload_EndFrame( void ) {
	rg_uploadManager.EndFrame();
}

bool R_RendererUpload_AllocFrameTemp( void *data, int bytes, int alignment, rendererUploadAllocation_t &allocation ) {
	return rg_uploadManager.AllocFrameTemp( data, bytes, alignment, allocation );
}

bool R_RendererUpload_AllocStaticBuffer( void *data, int bytes, bool indexBuffer, bool streamDraw, unsigned int &vbo ) {
	return rg_uploadManager.AllocStaticBuffer( data, bytes, indexBuffer, streamDraw, vbo );
}

void R_RendererUpload_FreeStaticBuffer( unsigned int &vbo, int bytes, bool indexBuffer ) {
	rg_uploadManager.FreeStaticBuffer( vbo, bytes, indexBuffer );
}

void R_RendererUpload_RecordLegacyUpload( int bytes ) {
	rg_uploadManager.RecordLegacyUpload( bytes );
}

void R_RendererUpload_RecordLegacyStall( void ) {
	rg_uploadManager.RecordLegacyStall();
}

const rendererUploadStats_t &R_RendererUpload_Stats( void ) {
	return rg_uploadManager.Stats();
}

bool R_RendererUpload_DynamicFrameBridgeAvailable( void ) {
	return rg_uploadManager.DynamicFrameBridgeAvailable();
}

bool R_RendererUpload_StaticBufferAllocatorAvailable( void ) {
	return rg_uploadManager.StaticBufferAllocatorAvailable();
}

int R_RendererUpload_FrameCapacity( void ) {
	return rg_uploadManager.FrameCapacity();
}

bool RendererUpload_RunSelfTest( void ) {
	idRingBuffer ring;
	bool wrapped = false;

	ring.Init( 1024, false );
	ring.BeginFrame();
	if ( ring.Allocate( 1, 16, wrapped ) != 0 || wrapped ) {
		common->Printf( "RendererUpload self-test failed: first aligned allocation\n" );
		return false;
	}
	if ( ring.Allocate( 15, 16, wrapped ) != 16 || wrapped ) {
		common->Printf( "RendererUpload self-test failed: second aligned allocation\n" );
		return false;
	}
	if ( ring.HighWater() != 31 ) {
		common->Printf( "RendererUpload self-test failed: high-water tracking\n" );
		return false;
	}
	if ( ring.Allocate( 2048, 16, wrapped ) != -1 ) {
		common->Printf( "RendererUpload self-test failed: oversize allocation\n" );
		return false;
	}
	ring.BeginFrame();
	if ( ring.Used() != 0 || ring.HighWater() != 0 || ring.OverflowBytes() != 0 ) {
		common->Printf( "RendererUpload self-test failed: begin-frame reset\n" );
		return false;
	}
	if ( ring.Allocate( 1024, 1, wrapped ) != 0 || wrapped || ring.Used() != 1024 ) {
		common->Printf( "RendererUpload self-test failed: exact allocation\n" );
		return false;
	}
	if ( ring.Allocate( 1, 1, wrapped ) != -1 || !wrapped ) {
		common->Printf( "RendererUpload self-test failed: overflow detection\n" );
		return false;
	}
	ring.Shutdown();

	idBufferAllocator allocator;
	allocator.Init( 2048, false );
	allocator.BeginFrame();
	if ( allocator.Capacity() != 2048 || allocator.FrameStaticUploadBytes() != 0 || allocator.FrameStaticAllocations() != 0 ) {
		common->Printf( "RendererUpload self-test failed: allocator begin-frame state\n" );
		return false;
	}
	allocator.Shutdown();

	if ( R_RendererUpload_StaticBufferAllocatorAvailable() ) {
		byte data[16];
		memset( data, 0x5a, sizeof( data ) );
		unsigned int vbo = 0;
		if ( !R_RendererUpload_AllocStaticBuffer( data, sizeof( data ), false, false, vbo ) || vbo == 0 ) {
			common->Printf( "RendererUpload self-test failed: static buffer allocation\n" );
			return false;
		}
		const rendererUploadStats_t &stats = R_RendererUpload_Stats();
		if ( stats.frameStaticUploadBytes < static_cast<int>( sizeof( data ) ) || stats.frameStaticAllocations <= 0 || stats.staticBuffersLive <= 0 ) {
			common->Printf( "RendererUpload self-test failed: static buffer stats\n" );
			R_RendererUpload_FreeStaticBuffer( vbo, sizeof( data ), false );
			return false;
		}
		R_RendererUpload_FreeStaticBuffer( vbo, sizeof( data ), false );
		if ( vbo != 0 ) {
			common->Printf( "RendererUpload self-test failed: static buffer free\n" );
			return false;
		}
	}

	common->Printf(
		"RendererUpload self-test passed (ring, allocator, static buffers, buffers=%d persistent=%d lowOverheadDefault=%d fences=%d/%d submitFailures=%d waits=%d timeouts=%d fallbacks=%d waitFailures=%d sync=%d)\n",
		R_RendererUpload_Stats().ringBufferCount,
		R_RendererUpload_Stats().persistentMapped ? 1 : 0,
		R_RendererUpload_Stats().lowOverheadPersistentDefault ? 1 : 0,
		R_RendererUpload_Stats().frameFencesSubmitted,
		R_RendererUpload_Stats().frameFencesRetired,
		R_RendererUpload_Stats().frameFenceSubmissionFailures,
		R_RendererUpload_Stats().frameFenceWaits,
		R_RendererUpload_Stats().frameFenceTimeouts,
		R_RendererUpload_Stats().frameFenceFallbacks,
		R_RendererUpload_Stats().frameFenceWaitFailures,
		R_RendererUpload_Stats().fenceSyncAvailable ? 1 : 0 );
	return true;
}
