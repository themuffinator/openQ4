// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDERER_UPLOAD_H__
#define __RENDERER_UPLOAD_H__

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
	int		staticBuffersLive;
	int		staticBytesLive;
	int		ringSizeBytes;
	int		ringBufferCount;
	bool	persistentMapped;
	bool	mapRangeFallback;
	bool	legacyBridge;
	bool	dynamicFrameBridge;
	bool	staticBufferAllocator;
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
	void FreeStaticBuffer( unsigned int &vbo, int bytes );
	int Capacity( void ) const;
	bool IsPersistentMapped( void ) const;
	int FrameStaticUploadBytes( void ) const;
	int FrameStaticAllocations( void ) const;
	int StaticBuffersLive( void ) const;
	int StaticBytesLive( void ) const;

private:
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
	void FreeStaticBuffer( unsigned int &vbo, int bytes );
	void RecordLegacyUpload( int bytes );
	void RecordLegacyStall( void );
	const rendererUploadStats_t &Stats( void ) const;
	bool DynamicFrameBridgeAvailable( void ) const;
	bool StaticBufferAllocatorAvailable( void ) const;
	int FrameCapacity( void ) const;

private:
	enum uploadPath_t {
		UPLOAD_PATH_DISABLED = 0,
		UPLOAD_PATH_SUBDATA,
		UPLOAD_PATH_MAP_RANGE,
		UPLOAD_PATH_PERSISTENT
	};

	struct frameBuffer_t {
		unsigned int	vbo;
		byte			*mapped;
		GLsync			fence;
	};

	bool CreateFrameBuffers( uploadPath_t requestedPath );
	void ShutdownFrameBuffers( void );
	void RetireFrameFence( frameBuffer_t &frame );
	void FenceCurrentFrame( void );
	void UpdateAllocatorStats( void );
	const char *PathName( void ) const;

	idBufferAllocator	allocator;
	idRingBuffer			ring;
	idLegacyStreamBuffer	legacy;
	rendererUploadStats_t	stats;
	frameBuffer_t			frameBuffers[3];
	uploadPath_t			path;
	int						currentFrameBuffer;
	bool					initialized;
	bool					hasSync;
};

void R_RendererUpload_Init( const renderBackendCaps_t &caps );
void R_RendererUpload_Shutdown( void );
void R_RendererUpload_BeginFrame( int frameCount );
void R_RendererUpload_EndFrame( void );
bool R_RendererUpload_AllocFrameTemp( void *data, int bytes, int alignment, rendererUploadAllocation_t &allocation );
bool R_RendererUpload_AllocStaticBuffer( void *data, int bytes, bool indexBuffer, bool streamDraw, unsigned int &vbo );
void R_RendererUpload_FreeStaticBuffer( unsigned int &vbo, int bytes );
void R_RendererUpload_RecordLegacyUpload( int bytes );
void R_RendererUpload_RecordLegacyStall( void );
const rendererUploadStats_t &R_RendererUpload_Stats( void );
bool R_RendererUpload_DynamicFrameBridgeAvailable( void );
bool R_RendererUpload_StaticBufferAllocatorAvailable( void );
int R_RendererUpload_FrameCapacity( void );
bool RendererUpload_RunSelfTest( void );

#endif /* !__RENDERER_UPLOAD_H__ */
