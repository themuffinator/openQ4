// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __PNGCODEC_H__
#define __PNGCODEC_H__

#include <stddef.h>
#include <stdint.h>

/*
===============================================================================

	PNG reader and writer

	The reader accepts every image the PNG specification (ISO/IEC 15948)
	defines: greyscale, truecolour, indexed, greyscale+alpha and
	truecolour+alpha at every legal bit depth (1, 2, 4, 8, 16), Adam7
	interlacing, and tRNS transparency. Output is always 8-bit RGBA, top row
	first; 16-bit samples are rounded to 8 bits. Gamma, colour-profile and
	text chunks are ignored, as are APNG animation chunks (the default image
	is decoded). Critical chunks are CRC-checked; ancillary chunks are skipped
	unread.

	The writer encodes 8-bit RGB or RGBA input as the smallest of greyscale,
	greyscale+alpha, truecolour and truecolour+alpha that represents it
	losslessly, choosing a filter per row by the minimum-sum-of-absolute-
	differences heuristic.

	The codec allocates its own scratch memory with malloc and never touches
	engine state, so it can be called from any thread.

===============================================================================
*/

typedef enum {
	PNG_OK = 0,
	PNG_ERR_SIGNATURE,		// not a PNG file
	PNG_ERR_TRUNCATED,		// the file ends inside a chunk or before IEND
	PNG_ERR_CRC,			// a critical chunk's CRC does not match
	PNG_ERR_HEADER,			// missing or invalid IHDR
	PNG_ERR_FORMAT,			// invalid chunk order, palette, filter or compressed data
	PNG_ERR_UNSUPPORTED,	// an unknown critical chunk
	PNG_ERR_TOO_LARGE,		// dimensions exceed the caller's limit
	PNG_ERR_NO_MEMORY,
	PNG_ERR_ARGUMENT
} pngResult_t;

typedef enum {
	PNG_COLOR_GRAY			= 0,
	PNG_COLOR_RGB			= 2,
	PNG_COLOR_PALETTE		= 3,
	PNG_COLOR_GRAY_ALPHA	= 4,
	PNG_COLOR_RGBA			= 6
} pngColorType_t;

typedef struct {
	uint32_t	width;
	uint32_t	height;
	uint8_t		bitDepth;
	uint8_t		colorType;		// pngColorType_t
	uint8_t		interlaced;		// Adam7
	bool		hasAlpha;		// an alpha channel or tRNS transparency
} pngInfo_t;

typedef struct {
	uint64_t	maxPixels;		// 0 = no limit beyond addressable memory
	bool		skipCRC;		// trust chunk CRCs (still checks the zlib Adler-32)
} pngDecodeOptions_t;

typedef enum {
	PNG_WRITE_AUTO = 0,		// smallest lossless colour type
	PNG_WRITE_RGB,			// drop any alpha
	PNG_WRITE_RGBA			// always keep alpha
} pngWriteFormat_t;

typedef struct {
	int					compressionLevel;	// 0 (store) .. 9 (smallest); negative selects 6
	pngWriteFormat_t	format;
	bool				flipVertical;		// input rows are bottom-up (glReadPixels order)
} pngEncodeOptions_t;

const char *	PNG_ResultString( pngResult_t result );

// true when data starts with the PNG signature
bool			PNG_HasSignature( const uint8_t *data, size_t size );

// Parses the header only.
pngResult_t		PNG_ReadInfo( const uint8_t *data, size_t size, pngInfo_t &info );

// Decodes the whole image into rgba, which holds info.width * info.height * 4
// bytes. options may be NULL.
pngResult_t		PNG_DecodeRGBA8( const uint8_t *data, size_t size, uint8_t *rgba, size_t rgbaBytes, const pngDecodeOptions_t *options );

// Encodes width x height pixels of 3 (RGB) or 4 (RGBA) bytes each, rowStride
// bytes apart (0 = tightly packed). *outData receives a malloc'd PNG file the
// caller releases with PNG_FreeEncoded. options may be NULL.
pngResult_t		PNG_Encode( const uint8_t *pixels, int width, int height, int channels, size_t rowStride,
							const pngEncodeOptions_t *options, uint8_t **outData, size_t *outSize );
void			PNG_FreeEncoded( uint8_t *data );

#endif /* !__PNGCODEC_H__ */
