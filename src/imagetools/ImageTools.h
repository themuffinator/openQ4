// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __IMAGETOOLS_H__
#define __IMAGETOOLS_H__

#include <stddef.h>
#include <string>
#include <vector>

/*
===============================================================================

	CPU-side image utilities shared between the engine and the renderer.

	Everything in src/imagetools reads, decodes, resamples, compresses, and
	writes pixel data without touching GPU state or renderer globals. The
	code builds as the openq4_imagetools static library, linked by the engine
	executables today and by the renderer modules once the Phase B seam
	completes (docs/dev/archive/2026-10-18/plans/2026-07-16-vulkan-renderer-phase-b.md, B2/B8).

	Renderer-owned facts cross this boundary through explicit setters:
	- compression capabilities gate precompressed-DDS selection and are
	  pushed by the renderer after its capability probe (defaults keep the
	  decoded/source fallback, which is always correct);
	- static-allocation counters are optional hooks the renderer installs so
	  its performance counters keep working without this library referencing
	  renderer globals.

===============================================================================
*/

/*
===============================================================================

	Image file tape: the file reads of one image source, recorded on the main
	thread and replayed on a worker.

	The file system is the main thread's (an open updates the pure and cd-path
	bookkeeping and the level-load cache), but decoding a source, running its
	image program and compressing the result need nothing from it except the
	bytes. A tape in RECORD mode lets each read (and each dds/ replacement
	lookup) go to the file system and keeps a copy of what it returned, misses
	included. In REPLAY mode the same reads are answered from the copy and
	nothing touches the file system; a read the tape does not hold sets missed
	and answers "not found", so a replay that wanders off the recorded path
	fails instead of guessing, and its owner loads the image the ordinary way.

	The scope is per thread and nests; with no scope the reads go straight to
	the file system as before.

===============================================================================
*/
enum imageFileTapeMode_t {
	IMAGE_TAPE_RECORD = 1,
	IMAGE_TAPE_REPLAY = 2
};

class idImageFileTape {
public:
	struct file_t {
		std::string					name;
		ID_TIME_T					timestamp = FILE_NOT_FOUND_TIMESTAMP;
		int							length = -1;		// -1: not found
		std::vector<unsigned char>	bytes;
	};
	struct resolve_t {
		std::string					name;
		bool						allowPrecompressed = false;
		bool						result = false;
		std::string					ddsName;
		ID_TIME_T					timestamp = FILE_NOT_FOUND_TIMESTAMP;
		bool						precompressed = false;
		bool						superseded = false;
	};

	std::vector<file_t>				files;
	std::vector<resolve_t>			resolves;
	bool							missed = false;		// a replay asked for something not recorded
	size_t							bytes = 0;

	const file_t *					FindFile( const char *name ) const;
	const resolve_t *				FindResolve( const char *name, bool allowPrecompressed ) const;
};

class idScopedImageFileTape {
public:
									idScopedImageFileTape( idImageFileTape *tape, imageFileTapeMode_t mode );
									~idScopedImageFileTape();
private:
	idImageFileTape *				previousTape;
	int								previousMode;
};

// true on a thread replaying a tape: an image worker, which must not touch the
// file system or the renderer's per-frame counters
bool	R_ImageFileTapeReplaying();

// loads an image file into a static-allocated RGBA8 buffer; *pic receives
// NULL when the image could not be loaded
void	R_LoadImage( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool makePowerOf2 );

// Returns exactly outwidth*outheight RGBA pixels, or NULL without touching in.
// Each axis is bounded to 32768; each input/output byte extent to 256 MiB.
byte *	R_ResampleTexture( const byte *in, int inwidth, int inheight, int outwidth, int outheight );

// default arguments stay on the renderer-internal declaration in Image.h
// until the module split completes; callers of this header pass all arguments
bool	R_WriteTGA( const char *filename, const byte *data, int width, int height, bool flipVertical, const char *basePath );

// PNG at image_pngCompressionLevel; data is width x height pixels of channels
// (3 = RGB, 4 = RGBA) bytes, top row first unless flipVertical is set
bool	R_WritePNG( const char *filename, const byte *data, int width, int height, int channels, bool flipVertical, const char *basePath );

// RGBA data as PNG when filename ends in .png, otherwise as TGA
bool	R_WriteImageFile( const char *filename, const byte *data, int width, int height, bool flipVertical, const char *basePath );

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
