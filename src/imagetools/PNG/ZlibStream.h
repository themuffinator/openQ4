// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __ZLIBSTREAM_H__
#define __ZLIBSTREAM_H__

#include <stddef.h>
#include <stdint.h>

/*
===============================================================================

	Self-contained zlib (RFC 1950) / DEFLATE (RFC 1951) codec used by the PNG
	reader and writer.

	The decoder works on whole buffers: the caller knows the decompressed size
	(PNG does), so inflate writes straight into the destination and uses it as
	the LZ77 window. Huffman decoding is table driven (11-bit literal/length and
	8-bit distance primary tables with subtables for longer codes), the bit
	reader refills 64 bits at a time, and matches copy in 8-byte words.

	The encoder finds matches with hash chains over the whole input, uses lazy
	matching from level 4 up, and emits each block as stored, fixed or dynamic
	Huffman data, whichever is smallest.

	Neither side allocates through engine heaps or touches engine state, so the
	code is also compiled directly into the native codec tests.

===============================================================================
*/

typedef enum {
	ZLIB_OK = 0,
	ZLIB_OK_EXCESS,			// the stream holds more data than the destination; the destination is complete
	ZLIB_ERR_TRUNCATED,		// the input ended before the stream did
	ZLIB_ERR_CORRUPT,		// invalid header, block, code or distance
	ZLIB_ERR_ADLER,			// the zlib trailer checksum does not match
	ZLIB_ERR_SHORT,			// the stream ended before filling the destination
	ZLIB_ERR_DICTIONARY,	// preset dictionaries are not supported
	ZLIB_ERR_NO_MEMORY
} zlibResult_t;

// Decompresses a complete zlib stream into exactly outSize bytes.
// ZLIB_OK means the stream ended exactly at outSize with a valid checksum.
zlibResult_t	Zlib_Inflate( const uint8_t *in, size_t inSize, uint8_t *out, size_t outSize, bool verifyChecksum );

// Raw DEFLATE data with no zlib wrapper; *outWritten receives the produced byte count.
zlibResult_t	Zlib_InflateRaw( const uint8_t *in, size_t inSize, uint8_t *out, size_t outSize, size_t *outWritten, size_t *inConsumed );

const char *	Zlib_ResultString( zlibResult_t result );

uint32_t		Zlib_Adler32( uint32_t adler, const uint8_t *data, size_t length );

// Worst-case zlib stream size for inSize bytes of input at any level.
size_t			Zlib_CompressBound( size_t inSize );

// Compresses in[0..inSize) as one zlib stream into out, which must hold
// Zlib_CompressBound( inSize ) bytes. Level 0 stores, 1-3 match greedily,
// 4-9 match lazily with progressively longer hash chains. Returns the
// stream size, or 0 when the work memory could not be allocated.
size_t			Zlib_Compress( const uint8_t *in, size_t inSize, uint8_t *out, int level );

#endif /* !__ZLIBSTREAM_H__ */
