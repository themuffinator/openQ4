// Copyright (C) 2026 DarkMatter Productions
//

#include "../../idlib/precompiled.h"
#include "ZlibStream.h"

#include <string.h>

/*
===============================================================================

	DEFLATE decoder

	Table layout. Every decode table entry is 32 bits:

		[31:16]	value: literal byte, length/distance base, or subtable offset
		[15]	literal
		[14]	subtable link
		[13]	end of block
		[12]	invalid code
		[11:8]	extra bits that follow the code (length/distance only)
		[7:0]	bits the code occupies in this table level

	A primary table indexes the next LITLEN_TABLE_BITS (or DIST_TABLE_BITS)
	stream bits. Codes longer than that land on a subtable link whose value is
	the subtable offset and whose extra-bits field is the subtable index width.

===============================================================================
*/

static const uint32_t ENTRY_LITERAL	= 1u << 15;
static const uint32_t ENTRY_SUBTABLE	= 1u << 14;
static const uint32_t ENTRY_EOB		= 1u << 13;
static const uint32_t ENTRY_INVALID	= 1u << 12;

static const int LITLEN_TABLE_BITS	= 11;
static const int DIST_TABLE_BITS	= 8;
static const int CODELEN_TABLE_BITS	= 7;
static const int MAX_CODE_BITS		= 15;

static const int NUM_LITLEN_SYMBOLS	= 288;
static const int NUM_DIST_SYMBOLS	= 32;
static const int NUM_CODELEN_SYMBOLS	= 19;

// primary table plus room for one maximal subtable per long code
static const int LITLEN_TABLE_SIZE	= ( 1 << LITLEN_TABLE_BITS ) + NUM_LITLEN_SYMBOLS * ( 1 << ( MAX_CODE_BITS - LITLEN_TABLE_BITS ) );
static const int DIST_TABLE_SIZE	= ( 1 << DIST_TABLE_BITS ) + NUM_DIST_SYMBOLS * ( 1 << ( MAX_CODE_BITS - DIST_TABLE_BITS ) );
static const int CODELEN_TABLE_SIZE	= 1 << CODELEN_TABLE_BITS;

static const uint16_t lengthBase[29] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
	35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t lengthExtra[29] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
	3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t distBase[30] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
	257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t distExtra[30] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
	7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
static const uint8_t codeLengthOrder[NUM_CODELEN_SYMBOLS] = {
	16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

typedef enum {
	TABLE_LITLEN,
	TABLE_DIST,
	TABLE_CODELEN
} tableKind_t;

static inline uint32_t MakeEntry( uint32_t value, uint32_t flags, uint32_t extra, uint32_t bits ) {
	return ( value << 16 ) | flags | ( extra << 8 ) | bits;
}

static uint32_t SymbolEntry( tableKind_t kind, int symbol, uint32_t bits ) {
	switch ( kind ) {
	case TABLE_LITLEN:
		if ( symbol < 256 ) {
			return MakeEntry( (uint32_t)symbol, ENTRY_LITERAL, 0, bits );
		}
		if ( symbol == 256 ) {
			return MakeEntry( 0, ENTRY_EOB, 0, bits );
		}
		if ( symbol < 286 ) {
			return MakeEntry( lengthBase[symbol - 257], 0, lengthExtra[symbol - 257], bits );
		}
		return MakeEntry( 0, ENTRY_INVALID, 0, bits );
	case TABLE_DIST:
		if ( symbol < 30 ) {
			return MakeEntry( distBase[symbol], 0, distExtra[symbol], bits );
		}
		return MakeEntry( 0, ENTRY_INVALID, 0, bits );
	default:
		return MakeEntry( (uint32_t)symbol, 0, 0, bits );
	}
}

static inline uint32_t ReverseBits( uint32_t code, int length ) {
	uint32_t reversed = 0;
	for ( int i = 0; i < length; i++ ) {
		reversed = ( reversed << 1 ) | ( code & 1 );
		code >>= 1;
	}
	return reversed;
}

/*
================
BuildDecodeTable

Builds the canonical Huffman decode table for lengths[0..numSymbols).
Over-subscribed codes are rejected. Incomplete codes are accepted only in the
cases zlib accepts: a single code of length one, or (distance codes only) no
codes at all. Unused slots decode as ENTRY_INVALID.
================
*/
static bool BuildDecodeTable( uint32_t *table, int tableBits, const uint8_t *lengths, int numSymbols, tableKind_t kind ) {
	int count[MAX_CODE_BITS + 1];
	int offsets[MAX_CODE_BITS + 2];
	uint16_t sorted[NUM_LITLEN_SYMBOLS];

	memset( count, 0, sizeof( count ) );
	for ( int i = 0; i < numSymbols; i++ ) {
		count[lengths[i]]++;
	}
	count[0] = 0;

	const uint32_t invalid = MakeEntry( 0, ENTRY_INVALID, 0, (uint32_t)tableBits );
	const int primarySize = 1 << tableBits;
	for ( int i = 0; i < primarySize; i++ ) {
		table[i] = invalid;
	}

	int used = 0;
	int maxLength = 0;
	int left = 1;
	for ( int length = 1; length <= MAX_CODE_BITS; length++ ) {
		left <<= 1;
		left -= count[length];
		if ( left < 0 ) {
			return false;		// over-subscribed
		}
		used += count[length];
		if ( count[length] != 0 ) {
			maxLength = length;
		}
	}
	if ( used == 0 ) {
		// every slot stays invalid; a distance code with no codes is legal
		// for blocks that hold only literals
		return kind == TABLE_DIST;
	}
	if ( left > 0 && !( used == 1 && maxLength == 1 ) ) {
		return false;			// incomplete
	}

	offsets[1] = 0;
	for ( int length = 1; length <= MAX_CODE_BITS; length++ ) {
		offsets[length + 1] = offsets[length] + count[length];
	}
	for ( int symbol = 0; symbol < numSymbols; symbol++ ) {
		if ( lengths[symbol] != 0 ) {
			sorted[offsets[lengths[symbol]]++] = (uint16_t)symbol;
		}
	}

	const int subBits = maxLength > tableBits ? maxLength - tableBits : 0;
	const int subSize = 1 << subBits;
	int nextSubtable = primarySize;
	uint32_t code = 0;
	int index = 0;
	for ( int length = 1; length <= maxLength; length++ ) {
		for ( int n = 0; n < count[length]; n++, index++ ) {
			const int symbol = sorted[index];
			const uint32_t reversed = ReverseBits( code, length );
			code++;
			if ( length <= tableBits ) {
				const uint32_t entry = SymbolEntry( kind, symbol, (uint32_t)length );
				for ( uint32_t slot = reversed; slot < (uint32_t)primarySize; slot += 1u << length ) {
					table[slot] = entry;
				}
				continue;
			}
			const uint32_t prefix = reversed & ( (uint32_t)primarySize - 1 );
			uint32_t link = table[prefix];
			if ( ( link & ENTRY_SUBTABLE ) == 0 ) {
				link = MakeEntry( (uint32_t)nextSubtable, ENTRY_SUBTABLE, (uint32_t)subBits, (uint32_t)tableBits );
				table[prefix] = link;
				const uint32_t subInvalid = MakeEntry( 0, ENTRY_INVALID, 0, (uint32_t)subBits );
				for ( int i = 0; i < subSize; i++ ) {
					table[nextSubtable + i] = subInvalid;
				}
				nextSubtable += subSize;
			}
			uint32_t *sub = table + ( link >> 16 );
			const int restBits = length - tableBits;
			const uint32_t entry = SymbolEntry( kind, symbol, (uint32_t)restBits );
			for ( uint32_t slot = reversed >> tableBits; slot < (uint32_t)subSize; slot += 1u << restBits ) {
				sub[slot] = entry;
			}
		}
		code <<= 1;
	}
	return true;
}

/*
================
Fixed tables

Built once; C++11 guarantees thread-safe initialisation of the static.
================
*/
typedef struct {
	uint32_t	litlen[LITLEN_TABLE_SIZE];
	uint32_t	dist[DIST_TABLE_SIZE];
} fixedTables_t;

static fixedTables_t *BuildFixedTables() {
	static fixedTables_t tables;
	uint8_t lengths[NUM_LITLEN_SYMBOLS];
	for ( int i = 0; i < 144; i++ ) {
		lengths[i] = 8;
	}
	for ( int i = 144; i < 256; i++ ) {
		lengths[i] = 9;
	}
	for ( int i = 256; i < 280; i++ ) {
		lengths[i] = 7;
	}
	for ( int i = 280; i < 288; i++ ) {
		lengths[i] = 8;
	}
	BuildDecodeTable( tables.litlen, LITLEN_TABLE_BITS, lengths, NUM_LITLEN_SYMBOLS, TABLE_LITLEN );
	for ( int i = 0; i < NUM_DIST_SYMBOLS; i++ ) {
		lengths[i] = 5;
	}
	BuildDecodeTable( tables.dist, DIST_TABLE_BITS, lengths, NUM_DIST_SYMBOLS, TABLE_DIST );
	return &tables;
}

static const fixedTables_t &FixedTables() {
	static const fixedTables_t *tables = BuildFixedTables();
	return *tables;
}

/*
================
Bit reader

bitBuffer holds bitCount valid bits, least significant first. Bits above
bitCount may hold copies of the following stream bits (the fast refill loads
whole words) and are never trusted. Past the end of the input the slow refill
supplies zero bytes and counts them in overrun; consuming any of those means
the stream was truncated.
================
*/

typedef struct {
	const uint8_t *	in;
	const uint8_t *	inEnd;
	uint64_t		bitBuffer;
	uint32_t		bitCount;
	uint32_t		overrun;
} bitReader_t;

static inline uint64_t LoadLE64( const uint8_t *p ) {
	uint64_t value;
	memcpy( &value, p, sizeof( value ) );
#if defined( __BYTE_ORDER__ ) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	value = __builtin_bswap64( value );
#endif
	return value;
}

static inline void RefillFast( bitReader_t &br ) {
	br.bitBuffer |= LoadLE64( br.in ) << br.bitCount;
	const uint32_t bytes = ( 63 - br.bitCount ) >> 3;
	br.in += bytes;
	br.bitCount += bytes << 3;
}

static inline void RefillSlow( bitReader_t &br ) {
	while ( br.bitCount < 56 ) {
		uint64_t next = 0;
		if ( br.in < br.inEnd ) {
			next = *br.in++;
		} else {
			br.overrun++;
		}
		br.bitBuffer &= ( (uint64_t)1 << br.bitCount ) - 1;
		br.bitBuffer |= next << br.bitCount;
		br.bitCount += 8;
	}
}

static inline void Refill( bitReader_t &br ) {
	if ( br.inEnd - br.in >= 8 ) {
		RefillFast( br );
	} else {
		RefillSlow( br );
	}
}

static inline uint32_t PeekBits( const bitReader_t &br, uint32_t count ) {
	return (uint32_t)( br.bitBuffer & ( ( (uint64_t)1 << count ) - 1 ) );
}

static inline void DropBits( bitReader_t &br, uint32_t count ) {
	br.bitBuffer >>= count;
	br.bitCount -= count;
}

static inline uint32_t ReadBits( bitReader_t &br, uint32_t count ) {
	const uint32_t value = PeekBits( br, count );
	DropBits( br, count );
	return value;
}

// true once the decoder has consumed virtual bytes past the end of the input
static inline bool Overrun( const bitReader_t &br ) {
	return br.overrun > ( br.bitCount >> 3 );
}

// Discards the bits up to the next byte boundary and hands the whole bytes
// still in the bit buffer back to the input, leaving the buffer empty.
static bool AlignToInput( bitReader_t &br ) {
	DropBits( br, br.bitCount & 7 );
	const uint32_t buffered = br.bitCount >> 3;
	if ( br.overrun > buffered ) {
		return false;
	}
	br.in -= buffered - br.overrun;
	br.overrun = 0;
	br.bitBuffer = 0;
	br.bitCount = 0;
	return true;
}

static inline uint32_t DecodeEntry( bitReader_t &br, const uint32_t *table, uint32_t tableBits ) {
	uint32_t entry = table[PeekBits( br, tableBits )];
	if ( entry & ENTRY_SUBTABLE ) {
		DropBits( br, tableBits );
		entry = table[( entry >> 16 ) + PeekBits( br, ( entry >> 8 ) & 0xF )];
	}
	DropBits( br, entry & 0xFF );
	return entry;
}

/*
================
CopyMatch

Fast form: needs 8 bytes of slack past out + length. Distances of 8 or more
copy whole words (each word reads only bytes already written); shorter
distances replicate the pattern bytewise.
================
*/
static inline uint8_t *CopyMatchFast( uint8_t *out, uint32_t distance, uint32_t length ) {
	const uint8_t *src = out - distance;
	uint8_t *end = out + length;
	if ( distance >= 8 ) {
		do {
			uint64_t word;
			memcpy( &word, src, 8 );
			memcpy( out, &word, 8 );
			out += 8;
			src += 8;
		} while ( out < end );
	} else if ( distance == 1 ) {
		memset( out, *src, length );
	} else {
		while ( out < end ) {
			*out++ = *src++;
		}
	}
	return end;
}

typedef struct {
	uint32_t	litlen[LITLEN_TABLE_SIZE];
	uint32_t	dist[DIST_TABLE_SIZE];
	uint32_t	codelen[CODELEN_TABLE_SIZE];
} dynamicTables_t;

static zlibResult_t ReadDynamicTables( bitReader_t &br, dynamicTables_t &tables ) {
	uint8_t lengths[NUM_LITLEN_SYMBOLS + NUM_DIST_SYMBOLS];
	uint8_t codeLengths[NUM_CODELEN_SYMBOLS];

	Refill( br );
	const uint32_t numLitlen = ReadBits( br, 5 ) + 257;
	const uint32_t numDist = ReadBits( br, 5 ) + 1;
	const uint32_t numCodelen = ReadBits( br, 4 ) + 4;
	if ( numLitlen > 286 || numDist > 30 ) {
		return ZLIB_ERR_CORRUPT;
	}

	memset( codeLengths, 0, sizeof( codeLengths ) );
	for ( uint32_t i = 0; i < numCodelen; i++ ) {
		if ( br.bitCount < 3 ) {
			Refill( br );
		}
		codeLengths[codeLengthOrder[i]] = (uint8_t)ReadBits( br, 3 );
	}
	if ( Overrun( br ) ) {
		return ZLIB_ERR_TRUNCATED;
	}
	if ( !BuildDecodeTable( tables.codelen, CODELEN_TABLE_BITS, codeLengths, NUM_CODELEN_SYMBOLS, TABLE_CODELEN ) ) {
		return ZLIB_ERR_CORRUPT;
	}

	const uint32_t total = numLitlen + numDist;
	uint32_t index = 0;
	while ( index < total ) {
		// worst case: 7-bit code plus 7 repeat bits
		if ( br.bitCount < 14 ) {
			Refill( br );
		}
		const uint32_t entry = DecodeEntry( br, tables.codelen, CODELEN_TABLE_BITS );
		if ( entry & ENTRY_INVALID ) {
			return ZLIB_ERR_CORRUPT;
		}
		const uint32_t symbol = entry >> 16;
		if ( symbol < 16 ) {
			lengths[index++] = (uint8_t)symbol;
			continue;
		}
		uint32_t repeat;
		uint8_t value = 0;
		if ( symbol == 16 ) {
			if ( index == 0 ) {
				return ZLIB_ERR_CORRUPT;
			}
			value = lengths[index - 1];
			repeat = 3 + ReadBits( br, 2 );
		} else if ( symbol == 17 ) {
			repeat = 3 + ReadBits( br, 3 );
		} else {
			repeat = 11 + ReadBits( br, 7 );
		}
		if ( index + repeat > total ) {
			return ZLIB_ERR_CORRUPT;
		}
		memset( lengths + index, value, repeat );
		index += repeat;
	}
	if ( Overrun( br ) ) {
		return ZLIB_ERR_TRUNCATED;
	}
	if ( lengths[256] == 0 ) {
		return ZLIB_ERR_CORRUPT;		// no end-of-block code
	}
	if ( !BuildDecodeTable( tables.litlen, LITLEN_TABLE_BITS, lengths, (int)numLitlen, TABLE_LITLEN ) ||
		 !BuildDecodeTable( tables.dist, DIST_TABLE_BITS, lengths + numLitlen, (int)numDist, TABLE_DIST ) ) {
		return ZLIB_ERR_CORRUPT;
	}
	return ZLIB_OK;
}

/*
================
DecodeHuffmanBlock

The fast loop runs while at least 16 input bytes (two word refills) and
258 + 8 output bytes remain, so neither the refills nor the word-wise match
copy need a bounds check. One refill (at least 56 bits) covers a full length/distance pair:
15 + 5 bits of length and 15 + 13 bits of distance.
================
*/
static zlibResult_t DecodeHuffmanBlock( bitReader_t &br, const uint32_t *litlen, const uint32_t *dist,
										uint8_t *outStart, uint8_t *&outRef, uint8_t *outEnd ) {
	uint8_t *out = outRef;
	static const ptrdiff_t FAST_OUT_MARGIN = 258 + 8;

	while ( br.inEnd - br.in >= 16 && outEnd - out >= FAST_OUT_MARGIN ) {
		RefillFast( br );
		uint32_t entry = DecodeEntry( br, litlen, LITLEN_TABLE_BITS );
		if ( entry & ENTRY_LITERAL ) {
			*out++ = (uint8_t)( entry >> 16 );
			// a second literal fits in the same refill (at most 30 bits so far)
			entry = DecodeEntry( br, litlen, LITLEN_TABLE_BITS );
			if ( entry & ENTRY_LITERAL ) {
				*out++ = (uint8_t)( entry >> 16 );
				continue;
			}
			if ( br.bitCount < 48 ) {
				RefillFast( br );
			}
		}
		if ( entry & ( ENTRY_EOB | ENTRY_INVALID ) ) {
			outRef = out;
			return ( entry & ENTRY_EOB ) ? ZLIB_OK : ZLIB_ERR_CORRUPT;
		}
		const uint32_t length = ( entry >> 16 ) + ReadBits( br, ( entry >> 8 ) & 0xF );
		entry = DecodeEntry( br, dist, DIST_TABLE_BITS );
		if ( entry & ENTRY_INVALID ) {
			outRef = out;
			return ZLIB_ERR_CORRUPT;
		}
		const uint32_t distance = ( entry >> 16 ) + ReadBits( br, ( entry >> 8 ) & 0xF );
		if ( distance > (size_t)( out - outStart ) ) {
			outRef = out;
			return ZLIB_ERR_CORRUPT;
		}
		out = CopyMatchFast( out, distance, length );
	}

	// careful loop near either end of the buffers
	for ( ;; ) {
		Refill( br );
		uint32_t entry = DecodeEntry( br, litlen, LITLEN_TABLE_BITS );
		if ( Overrun( br ) ) {
			outRef = out;
			return ZLIB_ERR_TRUNCATED;
		}
		if ( entry & ENTRY_LITERAL ) {
			if ( out == outEnd ) {
				outRef = out;
				return ZLIB_OK_EXCESS;
			}
			*out++ = (uint8_t)( entry >> 16 );
			continue;
		}
		if ( entry & ( ENTRY_EOB | ENTRY_INVALID ) ) {
			outRef = out;
			return ( entry & ENTRY_EOB ) ? ZLIB_OK : ZLIB_ERR_CORRUPT;
		}
		uint32_t length = ( entry >> 16 ) + ReadBits( br, ( entry >> 8 ) & 0xF );
		entry = DecodeEntry( br, dist, DIST_TABLE_BITS );
		if ( entry & ENTRY_INVALID ) {
			outRef = out;
			return ZLIB_ERR_CORRUPT;
		}
		const uint32_t distance = ( entry >> 16 ) + ReadBits( br, ( entry >> 8 ) & 0xF );
		if ( Overrun( br ) ) {
			outRef = out;
			return ZLIB_ERR_TRUNCATED;
		}
		if ( distance > (size_t)( out - outStart ) ) {
			outRef = out;
			return ZLIB_ERR_CORRUPT;
		}
		bool excess = false;
		if ( length > (size_t)( outEnd - out ) ) {
			length = (uint32_t)( outEnd - out );
			excess = true;
		}
		const uint8_t *src = out - distance;
		for ( uint32_t i = 0; i < length; i++ ) {
			out[i] = src[i];
		}
		out += length;
		if ( excess ) {
			outRef = out;
			return ZLIB_OK_EXCESS;
		}
	}
}

static zlibResult_t InflateBlocks( bitReader_t &br, uint8_t *out, size_t outSize, size_t *outWritten ) {
	uint8_t *outStart = out;
	uint8_t *outEnd = out + outSize;
	uint8_t *cursor = out;
	dynamicTables_t *dynamic = NULL;
	zlibResult_t result = ZLIB_OK;

	for ( ;; ) {
		Refill( br );
		const uint32_t isFinal = ReadBits( br, 1 );
		const uint32_t type = ReadBits( br, 2 );
		if ( Overrun( br ) ) {
			result = ZLIB_ERR_TRUNCATED;
			break;
		}

		if ( type == 0 ) {
			if ( !AlignToInput( br ) || br.inEnd - br.in < 4 ) {
				result = ZLIB_ERR_TRUNCATED;
				break;
			}
			const uint32_t length = br.in[0] | ( br.in[1] << 8 );
			const uint32_t inverse = br.in[2] | ( br.in[3] << 8 );
			br.in += 4;
			if ( ( length ^ 0xFFFF ) != inverse ) {
				result = ZLIB_ERR_CORRUPT;
				break;
			}
			if ( (size_t)( br.inEnd - br.in ) < length ) {
				result = ZLIB_ERR_TRUNCATED;
				break;
			}
			if ( length > (size_t)( outEnd - cursor ) ) {
				const size_t room = (size_t)( outEnd - cursor );
				memcpy( cursor, br.in, room );
				cursor += room;
				result = ZLIB_OK_EXCESS;
				break;
			}
			memcpy( cursor, br.in, length );
			cursor += length;
			br.in += length;
		} else if ( type == 1 ) {
			const fixedTables_t &fixed = FixedTables();
			result = DecodeHuffmanBlock( br, fixed.litlen, fixed.dist, outStart, cursor, outEnd );
		} else if ( type == 2 ) {
			if ( dynamic == NULL ) {
				dynamic = (dynamicTables_t *)malloc( sizeof( dynamicTables_t ) );
				if ( dynamic == NULL ) {
					result = ZLIB_ERR_NO_MEMORY;
					break;
				}
			}
			result = ReadDynamicTables( br, *dynamic );
			if ( result == ZLIB_OK ) {
				result = DecodeHuffmanBlock( br, dynamic->litlen, dynamic->dist, outStart, cursor, outEnd );
			}
		} else {
			result = ZLIB_ERR_CORRUPT;
		}

		if ( result != ZLIB_OK || isFinal ) {
			break;
		}
	}

	free( dynamic );
	*outWritten = (size_t)( cursor - outStart );
	return result;
}

/*
================
Zlib_InflateRaw
================
*/
zlibResult_t Zlib_InflateRaw( const uint8_t *in, size_t inSize, uint8_t *out, size_t outSize, size_t *outWritten, size_t *inConsumed ) {
	bitReader_t br;
	br.in = in;
	br.inEnd = in + inSize;
	br.bitBuffer = 0;
	br.bitCount = 0;
	br.overrun = 0;

	size_t written = 0;
	zlibResult_t result = InflateBlocks( br, out, outSize, &written );
	if ( outWritten != NULL ) {
		*outWritten = written;
	}
	if ( result == ZLIB_OK && !AlignToInput( br ) ) {
		result = ZLIB_ERR_TRUNCATED;
	}
	if ( inConsumed != NULL ) {
		*inConsumed = result == ZLIB_OK ? (size_t)( br.in - in ) : 0;
	}
	return result;
}

/*
================
Zlib_Inflate
================
*/
zlibResult_t Zlib_Inflate( const uint8_t *in, size_t inSize, uint8_t *out, size_t outSize, bool verifyChecksum ) {
	if ( inSize < 2 ) {
		return ZLIB_ERR_TRUNCATED;
	}
	const uint32_t cmf = in[0];
	const uint32_t flg = in[1];
	if ( ( cmf & 0x0F ) != 8 || ( cmf >> 4 ) > 7 || ( ( cmf << 8 ) | flg ) % 31 != 0 ) {
		return ZLIB_ERR_CORRUPT;
	}
	if ( flg & 0x20 ) {
		return ZLIB_ERR_DICTIONARY;
	}

	size_t written = 0;
	size_t consumed = 0;
	zlibResult_t result = Zlib_InflateRaw( in + 2, inSize - 2, out, outSize, &written, &consumed );
	if ( result != ZLIB_OK ) {
		return result;
	}
	if ( written != outSize ) {
		return ZLIB_ERR_SHORT;
	}
	if ( verifyChecksum ) {
		const uint8_t *trailer = in + 2 + consumed;
		if ( (size_t)( in + inSize - trailer ) < 4 ) {
			return ZLIB_ERR_TRUNCATED;
		}
		const uint32_t expected = ( (uint32_t)trailer[0] << 24 ) | ( (uint32_t)trailer[1] << 16 ) |
								  ( (uint32_t)trailer[2] << 8 ) | trailer[3];
		if ( Zlib_Adler32( 1, out, outSize ) != expected ) {
			return ZLIB_ERR_ADLER;
		}
	}
	return ZLIB_OK;
}

/*
================
Zlib_Adler32

Sums run in 5552-byte spans, the longest that cannot overflow 32 bits before
the modulo, with the inner loop unrolled by 16.
================
*/
uint32_t Zlib_Adler32( uint32_t adler, const uint8_t *data, size_t length ) {
	static const uint32_t BASE = 65521;
	static const size_t NMAX = 5552;
	uint32_t a = adler & 0xFFFF;
	uint32_t b = adler >> 16;

	while ( length > 0 ) {
		size_t span = length < NMAX ? length : NMAX;
		length -= span;
		while ( span >= 16 ) {
			a += data[0];  b += a;	a += data[1];  b += a;
			a += data[2];  b += a;	a += data[3];  b += a;
			a += data[4];  b += a;	a += data[5];  b += a;
			a += data[6];  b += a;	a += data[7];  b += a;
			a += data[8];  b += a;	a += data[9];  b += a;
			a += data[10]; b += a;	a += data[11]; b += a;
			a += data[12]; b += a;	a += data[13]; b += a;
			a += data[14]; b += a;	a += data[15]; b += a;
			data += 16;
			span -= 16;
		}
		while ( span-- > 0 ) {
			a += *data++;
			b += a;
		}
		a %= BASE;
		b %= BASE;
	}
	return ( b << 16 ) | a;
}

const char *Zlib_ResultString( zlibResult_t result ) {
	switch ( result ) {
	case ZLIB_OK:				return "ok";
	case ZLIB_OK_EXCESS:		return "stream holds more data than expected";
	case ZLIB_ERR_TRUNCATED:	return "compressed data is truncated";
	case ZLIB_ERR_CORRUPT:		return "compressed data is corrupt";
	case ZLIB_ERR_ADLER:		return "compressed data checksum mismatch";
	case ZLIB_ERR_SHORT:		return "compressed data ends early";
	case ZLIB_ERR_DICTIONARY:	return "compressed data needs a preset dictionary";
	case ZLIB_ERR_NO_MEMORY:	return "out of memory";
	}
	return "unknown error";
}
