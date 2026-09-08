// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __IMAGETOOLS_H__
#define __IMAGETOOLS_H__

#include <stddef.h>

/*
===============================================================================

	CPU-side image utilities shared between the engine and the renderer.

	Everything in src/imagetools reads, decodes, resamples, compresses, and
	writes pixel data without touching GPU state or renderer globals. The
	code builds as the openq4_imagetools static library, linked by the engine
	executables today and by the renderer modules once the Phase B seam
	completes (docs/dev/plans/2026-07-16-vulkan-renderer-phase-b.md, B2/B8).

	Renderer-owned facts cross this boundary through explicit setters:
	- compression capabilities gate precompressed-DDS selection and are
	  pushed by the renderer after its capability probe (defaults keep the
	  decoded/source fallback, which is always correct);
	- static-allocation counters are optional hooks the renderer installs so
	  its performance counters keep working without this library referencing
	  renderer globals.

===============================================================================
*/

// loads an image file into a static-allocated RGBA8 buffer; *pic receives
// NULL when the image could not be loaded
void	R_LoadImage( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool makePowerOf2 );

byte *	R_ResampleTexture( const byte *in, int inwidth, int inheight, int outwidth, int outheight );

// default arguments stay on the renderer-internal declaration in Image.h
// until the module split completes; callers of this header pass all arguments
bool	R_WriteTGA( const char *filename, const byte *data, int width, int height, bool flipVertical, const char *basePath );

// malloc with error checking; pairs with R_StaticFree
void *	R_StaticAlloc( size_t bytes );
void *	R_ClearedStaticAlloc( size_t bytes );
void	R_StaticFree( void *data );

// GPU compression support pushed by the renderer after capability probing;
// with the zeroed defaults every load takes the decoded/source path
typedef struct imageToolsCompressionCaps_s {
	bool	textureCompressionAvailable;		// S3TC/DXT sampling support
	bool	bptcTextureCompressionAvailable;	// BC7 sampling support
	bool	etc2TextureCompressionAvailable;	// ETC2/EAC sampling support
} imageToolsCompressionCaps_t;

void	ImageTools_SetCompressionCaps( const imageToolsCompressionCaps_t &caps );
const imageToolsCompressionCaps_t &ImageTools_GetCompressionCaps( void );

// optional static-allocation counters (renderer performance counters);
// zeroed hooks disable the callbacks
typedef struct imageToolsAllocHooks_s {
	void	( *onStaticAlloc )( size_t bytes );
	void	( *onStaticFree )( void );
} imageToolsAllocHooks_t;

void	ImageTools_SetStaticAllocHooks( const imageToolsAllocHooks_t &hooks );

#endif /* !__IMAGETOOLS_H__ */
