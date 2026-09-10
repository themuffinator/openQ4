// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDERER_UPLOAD_H__
#define __RENDERER_UPLOAD_H__
#include <stdint.h>

// Actual GL upload storage created by the current manager. This process-local
// allocation observation is not upload completion or a portable recovery plan.
struct rendererUploadStorage_t {
	uint64_t generation;
	int requestedMegs, requestedBuffers;
	int bytesPerBuffer, bufferCount;
	unsigned int path; // 1 subdata, 2 map-range, 3 persistent.
	bool persistentFallback;
};

typedef struct rendererUploadStats_s {
	int		frameUploadBytes;
	int		frameStaticUploadBytes;
	int		frameStalls;
	int		frameAllocations;
	int		frameStaticAllocations;
	int		frameRingUsedBytes;
	int		frameRingHighWaterBytes;
	int		frameOverflowBytes;
	int		framePersistentWrites;
	int		frameMapRangeWrites;
	int		frameSubDataWrites;
	int		frameFencesSubmitted;
	int		frameFencesRetired;
	int		frameFenceSubmissionFailures;
	int		frameFenceWaits;
	int		frameFenceTimeouts;
	int		frameFenceFallbacks;
	int		frameFenceWaitFailures;
	int		frameBufferIndex;
	int		staticBuffersLive;
	int		staticBytesLive;
	int		staticBuffersPooled;
	int		staticBytesPooled;
	int		ringSizeBytes;
	int		ringBufferCount;
	bool	persistentMapped;
	bool	lowOverheadPersistentDefault;
	bool	mapRangeFallback;
	bool	legacyBridge;
	bool	dynamicFrameBridge;
	bool	staticBufferAllocator;
	bool	fenceSyncAvailable;
	bool	driverQuirkPersistentDisabled;
} rendererUploadStats_t;

typedef struct rendererUploadAllocation_s {
	unsigned int	vbo;
	int				offset;
	int				size;
	bool			persistentMapped;
	bool			mapRange;
} rendererUploadAllocation_t;

class idBufferAllocator {
public:
	idBufferAllocator();
	void Init( int bytes, bool persistent );
	void Shutdown( void );
	void BeginFrame( void );
	bool AllocStaticBuffer( void *data, int bytes, bool indexBuffer, bool streamDraw, unsigned int &vbo );
	void FreeStaticBuffer( unsigned int &vbo, int bytes, bool indexBuffer );
	int Capacity( void ) const;
	bool IsPersistentMapped( void ) const;
	int FrameStaticUploadBytes( void ) const;
	int FrameStaticAllocations( void ) const;
	int StaticBuffersLive( void ) const;
	int StaticBytesLive( void ) const;
	int StaticBuffersPooled( void ) const;
	int StaticBytesPooled( void ) const;

private:
	bool	PoolEnabled( void ) const;
	void	DrainPool( void );

	// recycled GL buffer names: per-frame regenerated geometry (animated
	// models, interaction tris) frees and re-creates static buffers every
	// frame, and gen/data/delete trios are driver-side churn; recycling the
	// name and re-specifying storage with glBufferData (orphaning) matches
	// the original idTech4 behavior. Segregated by bind target so a name is
	// never re-specified under a different target (legal, but a historical
	// driver slow path); [0] = GL_ARRAY_BUFFER, [1] = GL_ELEMENT_ARRAY_BUFFER.
	idList<unsigned int>	pooledNames[2];
	idList<int>				pooledSizes[2];
	int		pooledBytes;

	int		capacityBytes;
	int		frameStaticUploadBytes;
	int		frameStaticAllocations;
	int		staticBuffersLive;
	int		staticBytesLive;
	bool	persistentMapped;
};

class idRingBuffer {
public:
	idRingBuffer();
	void Init( int bytes, bool persistent );
	void Shutdown( void );
	void BeginFrame( void );
	int Allocate( int bytes, int alignment, bool &wrapped );
	void EndFrame( void );
	int Capacity( void ) const;
	int Used( void ) const;
	int HighWater( void ) const;
	int OverflowBytes( void ) const;

private:
	int		capacityBytes;
	int		head;
	int		highWater;
	int		overflowBytes;
	bool	persistentMapped;
};

class idLegacyStreamBuffer {
public:
	idLegacyStreamBuffer();
	void Init( bool useMapRange );
	void RecordUpload( int bytes );
	void RecordStall( void );
	void EndFrame( void );
	const rendererUploadStats_t &Stats( void ) const;

private:
	rendererUploadStats_t stats;
};

class idUploadManager {
public:
	idUploadManager();
	void Init( const renderBackendCaps_t &caps );
	void Shutdown( void );
	void BeginFrame( int frameCount );
	void EndFrame( void );
	bool AllocFrameTemp( void *data, int bytes, int alignment, rendererUploadAllocation_t &allocation );
	bool AllocStaticBuffer( void *data, int bytes, bool indexBuffer, bool streamDraw, unsigned int &vbo );
	void FreeStaticBuffer( unsigned int &vbo, int bytes, bool indexBuffer );
	void RecordLegacyUpload( int bytes );
	void RecordLegacyStall( void );
	const rendererUploadStats_t &Stats( void ) const;
	bool DynamicFrameBridgeAvailable( void ) const;
	bool StaticBufferAllocatorAvailable( void ) const;
	int FrameCapacity( void ) const;
	bool QueryStorage( rendererUploadStorage_t &output ) const;

private:
	enum uploadPath_t {
		UPLOAD_PATH_DISABLED = 0,
		UPLOAD_PATH_SUBDATA,
		UPLOAD_PATH_MAP_RANGE,
		UPLOAD_PATH_PERSISTENT
	};

	enum {
		RENDERER_UPLOAD_MAX_FRAME_BUFFERS = 8
	};

	struct frameBuffer_t {
		unsigned int	vbo;
		byte			*mapped;
		GLsync			fence;
	};

	bool CreateFrameBuffers( uploadPath_t requestedPath );
	void ShutdownFrameBuffers( void );
	bool RetireFrameFence( frameBuffer_t &frame, bool allowBlocking );
	bool SelectFrameBufferForFrame( int preferredFrameBuffer );
	void FenceCurrentFrame( void );
	void UpdateAllocatorStats( void );
	const char *PathName( void ) const;

	idBufferAllocator	allocator;
	idRingBuffer			ring;
	idLegacyStreamBuffer	legacy;
	rendererUploadStats_t	stats;
	frameBuffer_t			frameBuffers[RENDERER_UPLOAD_MAX_FRAME_BUFFERS];
	uploadPath_t			path;
	int						currentFrameBuffer;
	int						frameBufferCount;
	bool					initialized;
	bool					hasSync;
	rendererUploadStorage_t storage{};
	bool storageReady = false;
};

void R_RendererUpload_Init( const renderBackendCaps_t &caps );
void R_RendererUpload_Shutdown( void );
void R_RendererUpload_BeginFrame( int frameCount );
void R_RendererUpload_EndFrame( void );
bool R_RendererUpload_AllocFrameTemp( void *data, int bytes, int alignment, rendererUploadAllocation_t &allocation );
bool R_RendererUpload_AllocStaticBuffer( void *data, int bytes, bool indexBuffer, bool streamDraw, unsigned int &vbo );
void R_RendererUpload_FreeStaticBuffer( unsigned int &vbo, int bytes, bool indexBuffer );
void R_RendererUpload_RecordLegacyUpload( int bytes );
void R_RendererUpload_RecordLegacyStall( void );
const rendererUploadStats_t &R_RendererUpload_Stats( void );
bool R_RendererUpload_DynamicFrameBridgeAvailable( void );
bool R_RendererUpload_StaticBufferAllocatorAvailable( void );
int R_RendererUpload_FrameCapacity( void );
// Renderer thread only, no native calls. False preserves output. Vulkan has a
// different staging allocator and refuses this GL-specific storage observation.
bool R_RendererUpload_QueryStorage( rendererUploadStorage_t &output );
bool RendererUpload_RunSelfTest( void );

#endif /* !__RENDERER_UPLOAD_H__ */
