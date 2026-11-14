// Copyright (C) 2026 DarkMatter Productions
//

#include "../../idlib/precompiled.h"
#include "PNGCodec.h"
#include "PNGFilters.h"
#include "ZlibStream.h"
#include "../../idlib/hashing/CRC32.h"

#include <stdlib.h>
#include <string.h>

/*
===============================================================================

	PNG encoder

	The file is assembled in one allocation sized for the worst case: the
	deflate stream is compressed straight into the single IDAT chunk's data,
	so no compressed byte is copied afterwards.

===============================================================================
*/

static const uint8_t PNG_FILE_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

static inline void WriteBE32( uint8_t *p, uint32_t value ) {
	p[0] = (uint8_t)( value >> 24 );
	p[1] = (uint8_t)( value >> 16 );
	p[2] = (uint8_t)( value >> 8 );
	p[3] = (uint8_t)value;
}

// writes length and type at p; the data follows at p + 8
static void BeginChunk( uint8_t *p, const char *type, uint32_t length ) {
	WriteBE32( p, length );
	memcpy( p + 4, type, 4 );
}

// appends the CRC of the chunk at p; returns the chunk's total size
static size_t EndChunk( uint8_t *p ) {
	const uint32_t length = ( (uint32_t)p[0] << 24 ) | ( (uint32_t)p[1] << 16 ) | ( (uint32_t)p[2] << 8 ) | p[3];
	WriteBE32( p + 8 + length, CRC32_BlockChecksum( p + 4, (int)( length + 4 ) ) );
	return 12 + (size_t)length;
}

static void PackRow( const uint8_t *src, int width, int channels, int colorType, uint8_t *dst ) {
	switch ( colorType ) {
	case PNG_COLOR_GRAY:
		for ( int x = 0; x < width; x++, src += channels ) {
			*dst++ = src[0];
		}
		break;
	case PNG_COLOR_GRAY_ALPHA:
		for ( int x = 0; x < width; x++, src += channels ) {
			*dst++ = src[0];
			*dst++ = src[3];
		}
		break;
	case PNG_COLOR_RGB:
		if ( channels == 3 ) {
			memcpy( dst, src, (size_t)width * 3 );
			break;
		}
		for ( int x = 0; x < width; x++, src += 4 ) {
			*dst++ = src[0];
			*dst++ = src[1];
			*dst++ = src[2];
		}
		break;
	default:
		if ( channels == 4 ) {
			memcpy( dst, src, (size_t)width * 4 );
			break;
		}
		for ( int x = 0; x < width; x++, src += 3 ) {
			*dst++ = src[0];
			*dst++ = src[1];
			*dst++ = src[2];
			*dst++ = 255;
		}
		break;
	}
}

// the smallest colour type that keeps every pixel exactly
static int ChooseColorType( const uint8_t *pixels, int width, int height, int channels, size_t stride, pngWriteFormat_t format ) {
	if ( format == PNG_WRITE_RGB ) {
		return PNG_COLOR_RGB;
	}
	if ( format == PNG_WRITE_RGBA ) {
		return PNG_COLOR_RGBA;
	}
	bool opaque = true;
	bool gray = true;
	for ( int y = 0; y < height && ( opaque || gray ); y++ ) {
		const uint8_t *p = pixels + stride * (size_t)y;
		for ( int x = 0; x < width; x++, p += channels ) {
			gray = gray && p[0] == p[1] && p[1] == p[2];
			opaque = opaque && ( channels == 3 || p[3] == 255 );
		}
	}
	if ( gray ) {
		return opaque ? PNG_COLOR_GRAY : PNG_COLOR_GRAY_ALPHA;
	}
	return opaque ? PNG_COLOR_RGB : PNG_COLOR_RGBA;
}

static int BytesPerPixel( int colorType ) {
	switch ( colorType ) {
	case PNG_COLOR_GRAY:		return 1;
	case PNG_COLOR_GRAY_ALPHA:	return 2;
	case PNG_COLOR_RGB:			return 3;
	}
	return 4;
}

/*
================
PNG_Encode
================
*/
pngResult_t PNG_Encode( const uint8_t *pixels, int width, int height, int channels, size_t rowStride,
						const pngEncodeOptions_t *options, uint8_t **outData, size_t *outSize ) {
	if ( outData == NULL || outSize == NULL ) {
		return PNG_ERR_ARGUMENT;
	}
	*outData = NULL;
	*outSize = 0;
	if ( pixels == NULL || width <= 0 || height <= 0 || ( channels != 3 && channels != 4 ) ) {
		return PNG_ERR_ARGUMENT;
	}
	const size_t packedStride = (size_t)width * channels;
	if ( rowStride == 0 ) {
		rowStride = packedStride;
	}
	if ( rowStride < packedStride ) {
		return PNG_ERR_ARGUMENT;
	}

	const int level = options != NULL ? options->compressionLevel : -1;
	const pngWriteFormat_t format = options != NULL ? options->format : PNG_WRITE_AUTO;
	const bool flip = options != NULL && options->flipVertical;

	const int colorType = ChooseColorType( pixels, width, height, channels, rowStride, format );
	const uint32_t bpp = (uint32_t)BytesPerPixel( colorType );
	const uint64_t rowBytes = (uint64_t)width * bpp;
	const uint64_t filteredBytes = (uint64_t)height * ( rowBytes + 1 );
	if ( filteredBytes > 0x7FFF0000u ) {
		return PNG_ERR_TOO_LARGE;
	}
	const size_t compressBound = Zlib_CompressBound( (size_t)filteredBytes );
	if ( compressBound > 0x7FFFFFFFu ) {
		return PNG_ERR_TOO_LARGE;
	}

	// filter every row into one buffer: each row tries all five filters and
	// keeps the one with the smallest sum of signed magnitudes
	uint8_t *filtered = (uint8_t *)malloc( (size_t)filteredBytes );
	uint8_t *rows = (uint8_t *)malloc( (size_t)rowBytes * 7 );
	if ( filtered == NULL || rows == NULL ) {
		free( filtered );
		free( rows );
		return PNG_ERR_NO_MEMORY;
	}
	uint8_t *current = rows;
	uint8_t *prior = rows + rowBytes;
	uint8_t *candidates = rows + rowBytes * 2;
	memset( prior, 0, (size_t)rowBytes );

	uint8_t *out = filtered;
	for ( int y = 0; y < height; y++ ) {
		const int sourceRow = flip ? height - 1 - y : y;
		PackRow( pixels + rowStride * (size_t)sourceRow, width, channels, colorType, current );

		if ( level == 0 ) {
			*out++ = PNG_FILTER_NONE;
			memcpy( out, current, (size_t)rowBytes );
		} else {
			int best = PNG_FILTER_NONE;
			uint64_t bestSum = ~(uint64_t)0;
			for ( int filter = PNG_FILTER_NONE; filter <= PNG_FILTER_PAETH; filter++ ) {
				uint8_t *candidate = candidates + rowBytes * filter;
				PNG_FilterRow( filter, candidate, current, prior, (size_t)rowBytes, bpp );
				const uint64_t sum = PNG_FilterRowSum( candidate, (size_t)rowBytes );
				if ( sum < bestSum ) {
					bestSum = sum;
					best = filter;
				}
			}
			*out++ = (uint8_t)best;
			memcpy( out, candidates + rowBytes * best, (size_t)rowBytes );
		}
		out += rowBytes;

		uint8_t *swap = prior;
		prior = current;
		current = swap;
	}
	free( rows );

	// signature + IHDR + IDAT header .. data .. CRC + IEND
	const size_t idatOffset = sizeof( PNG_FILE_SIGNATURE ) + 25;
	const size_t capacity = idatOffset + 8 + compressBound + 4 + 12;
	uint8_t *file = (uint8_t *)malloc( capacity );
	if ( file == NULL ) {
		free( filtered );
		return PNG_ERR_NO_MEMORY;
	}

	const size_t compressedBytes = Zlib_Compress( filtered, (size_t)filteredBytes, file + idatOffset + 8, level );
	free( filtered );
	if ( compressedBytes == 0 ) {
		free( file );
		return PNG_ERR_NO_MEMORY;
	}

	memcpy( file, PNG_FILE_SIGNATURE, sizeof( PNG_FILE_SIGNATURE ) );
	uint8_t *ihdr = file + sizeof( PNG_FILE_SIGNATURE );
	BeginChunk( ihdr, "IHDR", 13 );
	WriteBE32( ihdr + 8, (uint32_t)width );
	WriteBE32( ihdr + 12, (uint32_t)height );
	ihdr[16] = 8;					// bit depth
	ihdr[17] = (uint8_t)colorType;
	ihdr[18] = 0;					// deflate
	ihdr[19] = 0;					// adaptive filtering
	ihdr[20] = 0;					// not interlaced
	EndChunk( ihdr );

	uint8_t *idat = file + idatOffset;
	BeginChunk( idat, "IDAT", (uint32_t)compressedBytes );
	size_t size = idatOffset + EndChunk( idat );

	BeginChunk( file + size, "IEND", 0 );
	size += EndChunk( file + size );

	*outData = file;
	*outSize = size;
	return PNG_OK;
}

void PNG_FreeEncoded( uint8_t *data ) {
	free( data );
}
