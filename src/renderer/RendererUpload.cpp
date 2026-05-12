// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RendererUpload.h"
#include "RendererMetrics.h"

static idUploadManager rg_uploadManager;

idBufferAllocator::idBufferAllocator()
	: capacityBytes( 0 )
	, persistentMapped( false ) {
}

void idBufferAllocator::Init( int bytes, bool persistent ) {
	capacityBytes = bytes;
	persistentMapped = persistent;
}

void idBufferAllocator::Shutdown( void ) {
	capacityBytes = 0;
	persistentMapped = false;
}

int idBufferAllocator::Capacity( void ) const {
	return capacityBytes;
}

bool idBufferAllocator::IsPersistentMapped( void ) const {
	return persistentMapped;
}

idRingBuffer::idRingBuffer()
	: capacityBytes( 0 )
	, head( 0 )
	, persistentMapped( false ) {
}

void idRingBuffer::Init( int bytes, bool persistent ) {
	capacityBytes = bytes;
	head = 0;
	persistentMapped = persistent;
}

void idRingBuffer::Shutdown( void ) {
	capacityBytes = 0;
	head = 0;
	persistentMapped = false;
}

int idRingBuffer::Allocate( int bytes, int alignment, bool &wrapped ) {
	wrapped = false;
	if ( bytes <= 0 || capacityBytes <= 0 || bytes > capacityBytes ) {
		return -1;
	}
	if ( alignment <= 0 ) {
		alignment = 1;
	}

	const int alignedHead = ( head + alignment - 1 ) & ~( alignment - 1 );
	if ( alignedHead + bytes > capacityBytes ) {
		head = 0;
		wrapped = true;
	} else {
		head = alignedHead;
	}

	const int offset = head;
	head += bytes;
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
}

void idUploadManager::Init( const renderBackendCaps_t &caps ) {
	const bool persistent = caps.hasBufferStorage && caps.hasMapBufferRange;
	const bool mapRange = caps.hasMapBufferRange;
	const int ringBytes = persistent ? ( 16 * 1024 * 1024 ) : ( 4 * 1024 * 1024 );

	allocator.Init( ringBytes, persistent );
	ring.Init( ringBytes, persistent );
	legacy.Init( mapRange );

	memset( &stats, 0, sizeof( stats ) );
	stats.ringSizeBytes = ringBytes;
	stats.persistentMapped = persistent;
	stats.mapRangeFallback = mapRange && !persistent;
	stats.legacyBridge = true;

	common->Printf(
		"Renderer upload manager: %s legacy bridge, ring=%dKB, mapRange=%s\n",
		persistent ? "persistent-mapped capable" : "streaming",
		ringBytes / 1024,
		mapRange ? "yes" : "no" );
}

void idUploadManager::Shutdown( void ) {
	allocator.Shutdown();
	ring.Shutdown();
	memset( &stats, 0, sizeof( stats ) );
}

void idUploadManager::BeginFrame( void ) {
	stats.frameUploadBytes = 0;
	stats.frameStalls = 0;
}

void idUploadManager::EndFrame( void ) {
	legacy.EndFrame();
	ring.EndFrame();
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

void R_RendererUpload_Init( const renderBackendCaps_t &caps ) {
	rg_uploadManager.Init( caps );
}

void R_RendererUpload_Shutdown( void ) {
	rg_uploadManager.Shutdown();
}

void R_RendererUpload_BeginFrame( void ) {
	rg_uploadManager.BeginFrame();
}

void R_RendererUpload_EndFrame( void ) {
	rg_uploadManager.EndFrame();
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
