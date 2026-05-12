// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDERER_UPLOAD_H__
#define __RENDERER_UPLOAD_H__

typedef struct rendererUploadStats_s {
	int		frameUploadBytes;
	int		frameStalls;
	int		ringSizeBytes;
	bool	persistentMapped;
	bool	mapRangeFallback;
	bool	legacyBridge;
} rendererUploadStats_t;

class idBufferAllocator {
public:
	idBufferAllocator();
	void Init( int bytes, bool persistent );
	void Shutdown( void );
	int Capacity( void ) const;
	bool IsPersistentMapped( void ) const;

private:
	int		capacityBytes;
	bool	persistentMapped;
};

class idRingBuffer {
public:
	idRingBuffer();
	void Init( int bytes, bool persistent );
	void Shutdown( void );
	int Allocate( int bytes, int alignment, bool &wrapped );
	void EndFrame( void );
	int Capacity( void ) const;
	int Used( void ) const;

private:
	int		capacityBytes;
	int		head;
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
	void BeginFrame( void );
	void EndFrame( void );
	void RecordLegacyUpload( int bytes );
	void RecordLegacyStall( void );
	const rendererUploadStats_t &Stats( void ) const;

private:
	idBufferAllocator	allocator;
	idRingBuffer			ring;
	idLegacyStreamBuffer	legacy;
	rendererUploadStats_t	stats;
};

void R_RendererUpload_Init( const renderBackendCaps_t &caps );
void R_RendererUpload_Shutdown( void );
void R_RendererUpload_BeginFrame( void );
void R_RendererUpload_EndFrame( void );
void R_RendererUpload_RecordLegacyUpload( int bytes );
void R_RendererUpload_RecordLegacyStall( void );
const rendererUploadStats_t &R_RendererUpload_Stats( void );

#endif /* !__RENDERER_UPLOAD_H__ */
