// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RendererUpload.h"
#include "RendererMetrics.h"

static const int RENDERER_UPLOAD_FRAME_BUFFERS = 3;
static const int RENDERER_UPLOAD_MIN_MEGS = 1;
static const int RENDERER_UPLOAD_MAX_MEGS = 128;

static idUploadManager rg_uploadManager;

idBufferAllocator::idBufferAllocator()
	: capacityBytes( 0 )
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
}

void idBufferAllocator::Shutdown( void ) {
	capacityBytes = 0;
	frameStaticUploadBytes = 0;
	frameStaticAllocations = 0;
	staticBuffersLive = 0;
	staticBytesLive = 0;
	persistentMapped = false;
}

void idBufferAllocator::BeginFrame( void ) {
	frameStaticUploadBytes = 0;
	frameStaticAllocations = 0;
}

bool idBufferAllocator::AllocStaticBuffer( void *data, int bytes, bool indexBuffer, bool streamDraw, unsigned int &vbo ) {
	if ( bytes <= 0 ) {
		return false;
	}

	if ( vbo == 0 ) {
		glGenBuffersARB( 1, &vbo );
		if ( vbo == 0 ) {
			return false;
		}
		staticBuffersLive++;
	}

	const GLenum target = indexBuffer ? GL_ELEMENT_ARRAY_BUFFER_ARB : GL_ARRAY_BUFFER_ARB;
	const GLenum usage = streamDraw ? GL_STREAM_DRAW_ARB : GL_STATIC_DRAW_ARB;
	glBindBufferARB( target, vbo );
	glBufferDataARB( target, (GLsizeiptrARB)bytes, data, usage );

	frameStaticUploadBytes += bytes;
	frameStaticAllocations++;
	staticBytesLive += bytes;
	return true;
}

void idBufferAllocator::FreeStaticBuffer( unsigned int &vbo, int bytes ) {
	if ( vbo == 0 ) {
		return;
	}

	glDeleteBuffersARB( 1, &vbo );
	vbo = 0;
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
	initialized = false;
	hasSync = false;
}

void idUploadManager::Init( const renderBackendCaps_t &caps ) {
	Shutdown();

	const bool usePersistent = caps.hasBufferStorage && caps.hasMapBufferRange && r_rendererUploadPersistent.GetBool() && glBufferStorage != NULL && glMapBufferRange != NULL;
	const bool useMapRange = caps.hasMapBufferRange && glMapBufferRange != NULL;
	const int ringMegs = idMath::ClampInt( RENDERER_UPLOAD_MIN_MEGS, RENDERER_UPLOAD_MAX_MEGS, r_rendererUploadMegs.GetInteger() );
	const int ringBytes = ringMegs * 1024 * 1024;
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

	allocator.Init( ringBytes, requestedPath == UPLOAD_PATH_PERSISTENT );
	ring.Init( ringBytes, requestedPath == UPLOAD_PATH_PERSISTENT );
	legacy.Init( useMapRange );

	memset( &stats, 0, sizeof( stats ) );
	stats.ringSizeBytes = ringBytes;
	stats.ringBufferCount = RENDERER_UPLOAD_FRAME_BUFFERS;
	stats.persistentMapped = requestedPath == UPLOAD_PATH_PERSISTENT;
	stats.mapRangeFallback = requestedPath == UPLOAD_PATH_MAP_RANGE;
	stats.legacyBridge = true;
	stats.dynamicFrameBridge = false;
	stats.staticBufferAllocator = requestedPath != UPLOAD_PATH_DISABLED;
	hasSync = caps.hasSync && glFenceSync != NULL && glClientWaitSync != NULL && glDeleteSync != NULL;

	if ( requestedPath != UPLOAD_PATH_DISABLED ) {
		if ( !CreateFrameBuffers( requestedPath ) && requestedPath == UPLOAD_PATH_PERSISTENT ) {
			common->Warning( "Renderer upload manager: persistent mapped stream failed, falling back to map-range streaming" );
			ShutdownFrameBuffers();
			requestedPath = useMapRange ? UPLOAD_PATH_MAP_RANGE : UPLOAD_PATH_SUBDATA;
			stats.persistentMapped = false;
			stats.mapRangeFallback = requestedPath == UPLOAD_PATH_MAP_RANGE;
			CreateFrameBuffers( requestedPath );
		}
	}

	initialized = true;
	stats.dynamicFrameBridge = path != UPLOAD_PATH_DISABLED;
	stats.staticBufferAllocator = path != UPLOAD_PATH_DISABLED;
	UpdateAllocatorStats();

	common->Printf(
		"Renderer upload manager: %s legacy bridge, frameStream=%s, staticAllocator=%s, buffers=%d, ring=%dKB, sync=%s\n",
		path == UPLOAD_PATH_PERSISTENT ? "persistent-mapped capable" : "streaming",
		PathName(),
		stats.staticBufferAllocator ? "yes" : "no",
		RENDERER_UPLOAD_FRAME_BUFFERS,
		ringBytes / 1024,
		hasSync ? "yes" : "no" );
}

void idUploadManager::Shutdown( void ) {
	ShutdownFrameBuffers();
	allocator.Shutdown();
	ring.Shutdown();
	memset( &stats, 0, sizeof( stats ) );
	path = UPLOAD_PATH_DISABLED;
	currentFrameBuffer = 0;
	initialized = false;
	hasSync = false;
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
	allocator.BeginFrame();
	UpdateAllocatorStats();
	ring.BeginFrame();

	if ( path == UPLOAD_PATH_DISABLED ) {
		return;
	}

	currentFrameBuffer = frameCount % RENDERER_UPLOAD_FRAME_BUFFERS;
	frameBuffer_t &frame = frameBuffers[currentFrameBuffer];
	RetireFrameFence( frame );

	if ( path != UPLOAD_PATH_PERSISTENT ) {
		glBindBufferARB( GL_ARRAY_BUFFER_ARB, frame.vbo );
		glBufferDataARB( GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)stats.ringSizeBytes, NULL, GL_STREAM_DRAW_ARB );
	}
}

void idUploadManager::EndFrame( void ) {
	FenceCurrentFrame();
	stats.frameRingUsedBytes = ring.Used();
	stats.frameRingHighWaterBytes = ring.HighWater();
	stats.frameOverflowBytes += ring.OverflowBytes();
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

	glBindBufferARB( GL_ARRAY_BUFFER_ARB, frame.vbo );

	if ( path == UPLOAD_PATH_PERSISTENT && frame.mapped != NULL ) {
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

void idUploadManager::FreeStaticBuffer( unsigned int &vbo, int bytes ) {
	if ( vbo == 0 ) {
		return;
	}

	if ( !initialized || !stats.staticBufferAllocator ) {
		glDeleteBuffersARB( 1, &vbo );
		vbo = 0;
		return;
	}

	allocator.FreeStaticBuffer( vbo, bytes );
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
	path = requestedPath;
	if ( path == UPLOAD_PATH_DISABLED ) {
		return false;
	}

	const GLbitfield persistentFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT;
	const GLbitfield mapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

	for ( int i = 0; i < RENDERER_UPLOAD_FRAME_BUFFERS; ++i ) {
		glGenBuffersARB( 1, &frameBuffers[i].vbo );
		glBindBufferARB( GL_ARRAY_BUFFER_ARB, frameBuffers[i].vbo );

		if ( path == UPLOAD_PATH_PERSISTENT ) {
			glBufferStorage( GL_ARRAY_BUFFER, (GLsizeiptr)stats.ringSizeBytes, NULL, persistentFlags );
			frameBuffers[i].mapped = static_cast<byte *>( glMapBufferRange( GL_ARRAY_BUFFER, 0, stats.ringSizeBytes, mapFlags ) );
			if ( frameBuffers[i].mapped == NULL ) {
				return false;
			}
		} else {
			glBufferDataARB( GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)stats.ringSizeBytes, NULL, GL_STREAM_DRAW_ARB );
		}
	}

	glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
	return true;
}

void idUploadManager::ShutdownFrameBuffers( void ) {
	for ( int i = 0; i < RENDERER_UPLOAD_FRAME_BUFFERS; ++i ) {
		if ( frameBuffers[i].fence != NULL && glDeleteSync != NULL ) {
			glDeleteSync( frameBuffers[i].fence );
			frameBuffers[i].fence = NULL;
		}
		if ( frameBuffers[i].vbo != 0 ) {
			if ( frameBuffers[i].mapped != NULL ) {
				glBindBufferARB( GL_ARRAY_BUFFER_ARB, frameBuffers[i].vbo );
				glUnmapBuffer( GL_ARRAY_BUFFER );
				frameBuffers[i].mapped = NULL;
			}
			glDeleteBuffersARB( 1, &frameBuffers[i].vbo );
			frameBuffers[i].vbo = 0;
		}
	}
	glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
}

void idUploadManager::RetireFrameFence( frameBuffer_t &frame ) {
	if ( frame.fence == NULL || !hasSync ) {
		return;
	}

	const GLenum quickWait = glClientWaitSync( frame.fence, 0, 0 );
	if ( quickWait == GL_TIMEOUT_EXPIRED ) {
		stats.frameStalls++;
		R_RendererMetrics_AddBufferStall();
		glClientWaitSync( frame.fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED );
	}
	glDeleteSync( frame.fence );
	frame.fence = NULL;
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
}

void idUploadManager::UpdateAllocatorStats( void ) {
	stats.frameStaticUploadBytes = allocator.FrameStaticUploadBytes();
	stats.frameStaticAllocations = allocator.FrameStaticAllocations();
	stats.staticBuffersLive = allocator.StaticBuffersLive();
	stats.staticBytesLive = allocator.StaticBytesLive();
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

void R_RendererUpload_FreeStaticBuffer( unsigned int &vbo, int bytes ) {
	rg_uploadManager.FreeStaticBuffer( vbo, bytes );
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
			R_RendererUpload_FreeStaticBuffer( vbo, sizeof( data ) );
			return false;
		}
		R_RendererUpload_FreeStaticBuffer( vbo, sizeof( data ) );
		if ( vbo != 0 ) {
			common->Printf( "RendererUpload self-test failed: static buffer free\n" );
			return false;
		}
	}

	common->Printf( "RendererUpload self-test passed (ring, allocator, static buffers)\n" );
	return true;
}
