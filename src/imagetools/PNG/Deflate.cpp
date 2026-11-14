// Copyright (C) 2026 DarkMatter Productions
//

#include "../../idlib/precompiled.h"
#include "ZlibStream.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>

#if defined( _MSC_VER )
#include <intrin.h>
#endif

/*
===============================================================================

	DEFLATE encoder

	The whole input is in memory, so match finding needs no sliding window.
	head4[] maps a 4-byte hash to the newest position and prev[] (indexed by
	position modulo the 32 KiB window) chains each position to the previous
	one with the same 4-byte hash, so chain walks visit few false candidates.
	A single-entry 3-byte table supplies the nearest 3-byte match, which the
	4-byte chains cannot see. Chain limits follow zlib's configuration table.

	Symbols are buffered per block; each block is then written as stored,
	fixed or dynamic Huffman data, whichever costs the fewest bits.

===============================================================================
*/

static const int MIN_MATCH			= 3;
static const int MAX_MATCH			= 258;
static const int WINDOW_SIZE		= 32768;
static const int WINDOW_MASK		= WINDOW_SIZE - 1;
static const int HASH_BITS			= 15;
static const int HASH_SIZE			= 1 << HASH_BITS;
static const int HASH3_BITS			= 14;
static const int HASH3_SIZE			= 1 << HASH3_BITS;
static const int TOO_FAR			= 4096;		// 3-byte matches farther than this cost more than literals
static const int BLOCK_SYMBOLS		= 1 << 15;
static const int MAX_STORED			= 65535;

static const int NUM_LITLEN			= 286;
static const int NUM_DIST			= 30;
static const int NUM_CODELEN		= 19;
static const int MAX_BITS			= 15;
static const int MAX_CODELEN_BITS	= 7;

static const uint32_t MATCH_FLAG	= 0x80000000u;

typedef struct {
	uint16_t	goodLength;		// reduce the chain search when the previous match is this long
	uint16_t	maxLazy;		// greedy: longest match whose positions are all hashed; lazy: no lazy search past this
	uint16_t	niceLength;		// stop searching once a match is this long
	uint16_t	maxChain;
	bool		lazy;
} deflateLevel_t;

static const deflateLevel_t deflateLevels[10] = {
	{ 0, 0, 0, 0, false },
	{ 4, 4, 8, 4, false },
	{ 4, 5, 16, 8, false },
	{ 4, 6, 32, 32, false },
	{ 4, 4, 16, 16, true },
	{ 8, 16, 32, 32, true },
	{ 8, 16, 128, 128, true },
	{ 8, 32, 128, 256, true },
	{ 32, 128, 258, 1024, true },
	{ 32, 258, 258, 4096, true }
};

static const uint8_t lengthExtraBits[29] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
	3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t lengthBaseValue[29] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
	35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t distExtraBits[30] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
	7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
static const uint16_t distBaseValue[30] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
	257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t codeLengthWriteOrder[NUM_CODELEN] = {
	16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static inline uint32_t ReverseCode( uint32_t code, int length ) {
	uint32_t reversed = 0;
	for ( int i = 0; i < length; i++ ) {
		reversed = ( reversed << 1 ) | ( code & 1 );
		code >>= 1;
	}
	return reversed;
}

// canonical codes for lengths[], bit-reversed for LSB-first output
static void AssignCodes( const uint8_t *lengths, int numSymbols, uint16_t *codes ) {
	int count[MAX_BITS + 1];
	uint32_t next[MAX_BITS + 2];
	memset( count, 0, sizeof( count ) );
	for ( int i = 0; i < numSymbols; i++ ) {
		count[lengths[i]]++;
	}
	count[0] = 0;
	uint32_t code = 0;
	for ( int bits = 1; bits <= MAX_BITS; bits++ ) {
		code = ( code + count[bits - 1] ) << 1;
		next[bits] = code;
	}
	for ( int i = 0; i < numSymbols; i++ ) {
		const int length = lengths[i];
		codes[i] = length != 0 ? (uint16_t)ReverseCode( next[length]++, length ) : 0;
	}
}

typedef struct {
	uint8_t		lengthCode[MAX_MATCH - MIN_MATCH + 1];	// match length - 3 -> length code index
	uint8_t		distCode[WINDOW_SIZE];					// distance - 1 -> distance code
	uint8_t		fixedLitlenLengths[288];
	uint8_t		fixedDistLengths[NUM_DIST];
	uint16_t	fixedLitlenCodes[288];
	uint16_t	fixedDistCodes[NUM_DIST];
} deflateTables_t;

static const deflateTables_t *BuildDeflateTables() {
	static deflateTables_t tables;
	for ( int code = 0; code < 28; code++ ) {
		for ( int i = 0; i < ( 1 << lengthExtraBits[code] ); i++ ) {
			tables.lengthCode[lengthBaseValue[code] - MIN_MATCH + i] = (uint8_t)code;
		}
	}
	tables.lengthCode[MAX_MATCH - MIN_MATCH] = 28;
	for ( int code = 0; code < NUM_DIST; code++ ) {
		for ( int i = 0; i < ( 1 << distExtraBits[code] ); i++ ) {
			tables.distCode[distBaseValue[code] - 1 + i] = (uint8_t)code;
		}
	}
	for ( int i = 0; i < 288; i++ ) {
		tables.fixedLitlenLengths[i] = i < 144 ? 8 : ( i < 256 ? 9 : ( i < 280 ? 7 : 8 ) );
	}
	for ( int i = 0; i < NUM_DIST; i++ ) {
		tables.fixedDistLengths[i] = 5;
	}
	AssignCodes( tables.fixedLitlenLengths, 288, tables.fixedLitlenCodes );
	AssignCodes( tables.fixedDistLengths, NUM_DIST, tables.fixedDistCodes );
	return &tables;
}

static const deflateTables_t &DeflateTables() {
	static const deflateTables_t *tables = BuildDeflateTables();
	return *tables;
}

/*
================
BuildLengths

Length-limited Huffman code lengths. Minimum-redundancy lengths come from
Moffat and Katajainen's in-place algorithm over the sorted frequencies; any
lengths over maxBits are then folded back by rebalancing the Kraft sum, and
the final lengths go to the symbols in frequency order (the fold follows
Rich Geldreich's miniz). A code always has at least two symbols, so every
emitted tree is complete.
================
*/
typedef struct {
	uint32_t	freq;
	uint16_t	symbol;
} symbolFreq_t;

static bool SymbolFreqLess( const symbolFreq_t &a, const symbolFreq_t &b ) {
	return a.freq != b.freq ? a.freq < b.freq : a.symbol < b.symbol;
}

static void MinimumRedundancy( uint32_t *A, int n ) {
	if ( n == 1 ) {
		A[0] = 1;
		return;
	}
	A[0] += A[1];
	int root = 0;
	int leaf = 2;
	for ( int next = 1; next < n - 1; next++ ) {
		if ( leaf >= n || A[root] < A[leaf] ) {
			A[next] = A[root];
			A[root++] = (uint32_t)next;
		} else {
			A[next] = A[leaf++];
		}
		if ( leaf >= n || ( root < next && A[root] < A[leaf] ) ) {
			A[next] += A[root];
			A[root++] = (uint32_t)next;
		} else {
			A[next] += A[leaf++];
		}
	}
	A[n - 2] = 0;
	for ( int next = n - 3; next >= 0; next-- ) {
		A[next] = A[A[next]] + 1;
	}
	int available = 1;
	int used = 0;
	uint32_t depth = 0;
	root = n - 2;
	int next = n - 1;
	while ( available > 0 ) {
		while ( root >= 0 && A[root] == depth ) {
			used++;
			root--;
		}
		while ( available > used ) {
			A[next--] = depth;
			available--;
		}
		available = 2 * used;
		depth++;
		used = 0;
	}
}

static void BuildLengths( const uint32_t *freq, int numSymbols, int maxBits, uint8_t *lengths ) {
	symbolFreq_t used[NUM_LITLEN];
	uint32_t work[NUM_LITLEN];
	int numUsed = 0;

	memset( lengths, 0, numSymbols );
	for ( int i = 0; i < numSymbols; i++ ) {
		if ( freq[i] != 0 ) {
			used[numUsed].freq = freq[i];
			used[numUsed].symbol = (uint16_t)i;
			numUsed++;
		}
	}
	if ( numUsed == 0 ) {
		lengths[0] = 1;
		lengths[1] = 1;
		return;
	}
	if ( numUsed == 1 ) {
		lengths[used[0].symbol] = 1;
		lengths[used[0].symbol == 0 ? 1 : 0] = 1;
		return;
	}

	std::sort( used, used + numUsed, SymbolFreqLess );
	for ( int i = 0; i < numUsed; i++ ) {
		work[i] = used[i].freq;
	}
	MinimumRedundancy( work, numUsed );

	int numCodes[64];
	memset( numCodes, 0, sizeof( numCodes ) );
	for ( int i = 0; i < numUsed; i++ ) {
		numCodes[work[i] < 63 ? work[i] : 63]++;
	}
	for ( int i = maxBits + 1; i < 64; i++ ) {
		numCodes[maxBits] += numCodes[i];
		numCodes[i] = 0;
	}
	uint32_t total = 0;
	for ( int i = maxBits; i > 0; i-- ) {
		total += (uint32_t)numCodes[i] << ( maxBits - i );
	}
	while ( total != ( 1u << maxBits ) ) {
		numCodes[maxBits]--;
		for ( int i = maxBits - 1; i > 0; i-- ) {
			if ( numCodes[i] != 0 ) {
				numCodes[i]--;
				numCodes[i + 1] += 2;
				break;
			}
		}
		total--;
	}

	int index = numUsed - 1;	// most frequent first: they take the shortest codes
	for ( int bits = 1; bits <= maxBits; bits++ ) {
		for ( int n = numCodes[bits]; n > 0; n-- ) {
			lengths[used[index--].symbol] = (uint8_t)bits;
		}
	}
}

/*
================
Bit writer
================
*/
typedef struct {
	uint8_t *	out;
	uint64_t	bits;
	uint32_t	count;
} bitWriter_t;

static inline void PutBits( bitWriter_t &bw, uint32_t value, uint32_t count ) {
	bw.bits |= (uint64_t)value << bw.count;
	bw.count += count;
	if ( bw.count >= 32 ) {
		const uint32_t word = (uint32_t)bw.bits;
		bw.out[0] = (uint8_t)word;
		bw.out[1] = (uint8_t)( word >> 8 );
		bw.out[2] = (uint8_t)( word >> 16 );
		bw.out[3] = (uint8_t)( word >> 24 );
		bw.out += 4;
		bw.bits >>= 32;
		bw.count -= 32;
	}
}

// pads to a byte boundary and writes every pending byte
static inline void FlushBytes( bitWriter_t &bw ) {
	bw.count = ( bw.count + 7 ) & ~7u;
	while ( bw.count > 0 ) {
		*bw.out++ = (uint8_t)bw.bits;
		bw.bits >>= 8;
		bw.count -= 8;
	}
	bw.bits = 0;
}

/*
================
Encoder state
================
*/
typedef struct {
	uint8_t		symbol;
	uint8_t		extra;
} codeLengthOp_t;

typedef struct {
	const uint8_t *			data;
	int32_t					size;
	const deflateLevel_t *	config;
	const deflateTables_t *	tables;
	int32_t *				head4;
	int32_t *				head3;
	int32_t *				prev;
	int32_t					candidate3;		// head3 entry the last insert replaced
	uint32_t *				symbols;
	int						numSymbols;
	int32_t					blockStart;
	int32_t					blockBytes;
	uint32_t				litlenFreq[NUM_LITLEN];
	uint32_t				distFreq[NUM_DIST];
	bitWriter_t				bw;
} deflateState_t;

static inline uint32_t Hash3( const uint8_t *p ) {
	const uint32_t v = (uint32_t)p[0] | ( (uint32_t)p[1] << 8 ) | ( (uint32_t)p[2] << 16 );
	return ( v * 0x9E3779B1u ) >> ( 32 - HASH3_BITS );
}

static inline uint32_t Hash4( const uint8_t *p ) {
	uint32_t v;
	memcpy( &v, p, 4 );
	return ( v * 0x9E3779B1u ) >> ( 32 - HASH_BITS );
}

// pos + MIN_MATCH <= size
static inline void InsertPosition( deflateState_t &s, int32_t pos ) {
	const uint32_t h3 = Hash3( s.data + pos );
	s.candidate3 = s.head3[h3];
	s.head3[h3] = pos;
	if ( pos + 4 <= s.size ) {
		const uint32_t h4 = Hash4( s.data + pos );
		s.prev[pos & WINDOW_MASK] = s.head4[h4];
		s.head4[h4] = pos;
	} else {
		s.prev[pos & WINDOW_MASK] = -1;
	}
}

static inline uint32_t CountTrailingZeros64( uint64_t value ) {
#if defined( _MSC_VER ) && defined( _M_X64 )
	unsigned long index;
	_BitScanForward64( &index, value );
	return index;
#elif defined( __GNUC__ ) || defined( __clang__ )
	return (uint32_t)__builtin_ctzll( value );
#else
	uint32_t n = 0;
	while ( ( value & 1 ) == 0 ) {
		value >>= 1;
		n++;
	}
	return n;
#endif
}

static inline int MatchLength( const uint8_t *a, const uint8_t *b, int maxLength ) {
	int length = 0;
	while ( length + 8 <= maxLength ) {
		uint64_t x, y;
		memcpy( &x, a + length, 8 );
		memcpy( &y, b + length, 8 );
		const uint64_t diff = x ^ y;
		if ( diff != 0 ) {
#if defined( __BYTE_ORDER__ ) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
			return length + (int)( __builtin_clzll( diff ) >> 3 );
#else
			return length + (int)( CountTrailingZeros64( diff ) >> 3 );
#endif
		}
		length += 8;
	}
	while ( length < maxLength && a[length] == b[length] ) {
		length++;
	}
	return length;
}

// Longest match for pos (already inserted) that beats minLength; 0 if none.
static int LongestMatch( const deflateState_t &s, int32_t pos, int minLength, int chain, int32_t *distanceOut ) {
	const int maxLength = s.size - pos < MAX_MATCH ? s.size - pos : MAX_MATCH;
	if ( minLength >= maxLength ) {
		return 0;
	}
	const int32_t windowStart = pos > WINDOW_SIZE ? pos - WINDOW_SIZE : 0;
	const uint8_t *scan = s.data + pos;
	const int nice = s.config->niceLength;
	int best = minLength;
	int32_t bestDistance = 0;

	// the nearest 3-byte match, when a 3-byte match is still worth having
	const int32_t near3 = s.candidate3;
	if ( best < MIN_MATCH && near3 >= windowStart && pos - near3 <= TOO_FAR ) {
		const uint8_t *match = s.data + near3;
		if ( match[0] == scan[0] && match[1] == scan[1] && match[2] == scan[2] ) {
			best = MatchLength( match, scan, maxLength );
			bestDistance = pos - near3;
			if ( best >= nice || best >= maxLength ) {
				*distanceOut = bestDistance;
				return best;
			}
		}
	}

	int32_t candidate = s.prev[pos & WINDOW_MASK];

	while ( candidate >= windowStart && chain-- > 0 ) {
		const uint8_t *match = s.data + candidate;
		if ( match[best] == scan[best] && match[0] == scan[0] && match[1] == scan[1] ) {
			const int length = MatchLength( match, scan, maxLength );
			if ( length > best ) {
				best = length;
				bestDistance = pos - candidate;
				if ( length >= nice || length >= maxLength ) {
					break;
				}
			}
		}
		const int32_t next = s.prev[candidate & WINDOW_MASK];
		if ( next >= candidate ) {
			break;
		}
		candidate = next;
	}
	if ( bestDistance == 0 ) {
		return 0;
	}
	*distanceOut = bestDistance;
	return best;
}

/*
================
Block output
================
*/
static uint64_t StoredCost( uint32_t bitPosition, int32_t bytes ) {
	uint64_t cost = 0;
	do {
		const int32_t chunk = bytes < MAX_STORED ? bytes : MAX_STORED;
		const uint32_t afterHeader = ( bitPosition + 3 ) & 7;
		cost += 3 + ( afterHeader != 0 ? 8 - afterHeader : 0 ) + 32 + (uint64_t)chunk * 8;
		bitPosition = 0;
		bytes -= chunk;
	} while ( bytes > 0 );
	return cost;
}

static void WriteStored( bitWriter_t &bw, const uint8_t *data, int32_t bytes, bool final ) {
	do {
		const int32_t chunk = bytes < MAX_STORED ? bytes : MAX_STORED;
		bytes -= chunk;
		PutBits( bw, ( final && bytes == 0 ) ? 1 : 0, 1 );
		PutBits( bw, 0, 2 );
		FlushBytes( bw );
		const uint32_t inverse = ~(uint32_t)chunk;
		bw.out[0] = (uint8_t)chunk;
		bw.out[1] = (uint8_t)( chunk >> 8 );
		bw.out[2] = (uint8_t)inverse;
		bw.out[3] = (uint8_t)( inverse >> 8 );
		memcpy( bw.out + 4, data, (size_t)chunk );
		bw.out += 4 + chunk;
		data += chunk;
	} while ( bytes > 0 );
}

static int RunLengthCodes( const uint8_t *lengths, int count, codeLengthOp_t *ops ) {
	int numOps = 0;
	int i = 0;
	while ( i < count ) {
		const uint8_t value = lengths[i];
		int run = 1;
		while ( i + run < count && lengths[i + run] == value ) {
			run++;
		}
		i += run;
		if ( value == 0 ) {
			while ( run >= 11 ) {
				const int n = run < 138 ? run : 138;
				ops[numOps].symbol = 18;
				ops[numOps].extra = (uint8_t)( n - 11 );
				numOps++;
				run -= n;
			}
			if ( run >= 3 ) {
				ops[numOps].symbol = 17;
				ops[numOps].extra = (uint8_t)( run - 3 );
				numOps++;
				run = 0;
			}
		} else {
			ops[numOps].symbol = value;
			ops[numOps].extra = 0;
			numOps++;
			run--;
			while ( run >= 3 ) {
				const int n = run < 6 ? run : 6;
				ops[numOps].symbol = 16;
				ops[numOps].extra = (uint8_t)( n - 3 );
				numOps++;
				run -= n;
			}
		}
		while ( run-- > 0 ) {
			ops[numOps].symbol = value;
			ops[numOps].extra = 0;
			numOps++;
		}
	}
	return numOps;
}

static uint64_t DataCost( const deflateState_t &s, const uint8_t *litlenLengths, const uint8_t *distLengths ) {
	uint64_t cost = 0;
	for ( int i = 0; i < NUM_LITLEN; i++ ) {
		cost += (uint64_t)s.litlenFreq[i] * litlenLengths[i];
	}
	for ( int i = 0; i < 29; i++ ) {
		cost += (uint64_t)s.litlenFreq[257 + i] * lengthExtraBits[i];
	}
	for ( int i = 0; i < NUM_DIST; i++ ) {
		cost += (uint64_t)s.distFreq[i] * ( distLengths[i] + distExtraBits[i] );
	}
	return cost;
}

static void WriteSymbols( deflateState_t &s, const uint16_t *litlenCodes, const uint8_t *litlenLengths,
						  const uint16_t *distCodes, const uint8_t *distLengths ) {
	bitWriter_t &bw = s.bw;
	const deflateTables_t &t = *s.tables;
	for ( int i = 0; i < s.numSymbols; i++ ) {
		const uint32_t symbol = s.symbols[i];
		if ( ( symbol & MATCH_FLAG ) == 0 ) {
			PutBits( bw, litlenCodes[symbol], litlenLengths[symbol] );
			continue;
		}
		const uint32_t lengthMinus3 = ( symbol >> 16 ) & 0xFF;
		const uint32_t distMinus1 = symbol & 0xFFFF;
		const uint32_t lcode = t.lengthCode[lengthMinus3];
		const uint32_t lsym = 257 + lcode;
		PutBits( bw, litlenCodes[lsym] | ( ( lengthMinus3 + MIN_MATCH - lengthBaseValue[lcode] ) << litlenLengths[lsym] ),
				 litlenLengths[lsym] + lengthExtraBits[lcode] );
		const uint32_t dcode = t.distCode[distMinus1];
		PutBits( bw, distCodes[dcode] | ( ( distMinus1 + 1 - distBaseValue[dcode] ) << distLengths[dcode] ),
				 distLengths[dcode] + distExtraBits[dcode] );
	}
	PutBits( bw, litlenCodes[256], litlenLengths[256] );
}

static void FlushBlock( deflateState_t &s, bool final ) {
	s.litlenFreq[256] = 1;

	uint8_t litlenLengths[NUM_LITLEN];
	uint8_t distLengths[NUM_DIST];
	BuildLengths( s.litlenFreq, NUM_LITLEN, MAX_BITS, litlenLengths );
	BuildLengths( s.distFreq, NUM_DIST, MAX_BITS, distLengths );

	int numLitlen = NUM_LITLEN;
	while ( numLitlen > 257 && litlenLengths[numLitlen - 1] == 0 ) {
		numLitlen--;
	}
	int numDist = NUM_DIST;
	while ( numDist > 1 && distLengths[numDist - 1] == 0 ) {
		numDist--;
	}

	codeLengthOp_t ops[NUM_LITLEN + NUM_DIST];
	const int numLitlenOps = RunLengthCodes( litlenLengths, numLitlen, ops );
	const int numOps = numLitlenOps + RunLengthCodes( distLengths, numDist, ops + numLitlenOps );
	uint32_t codelenFreq[NUM_CODELEN];
	memset( codelenFreq, 0, sizeof( codelenFreq ) );
	for ( int i = 0; i < numOps; i++ ) {
		codelenFreq[ops[i].symbol]++;
	}
	uint8_t codelenLengths[NUM_CODELEN];
	BuildLengths( codelenFreq, NUM_CODELEN, MAX_CODELEN_BITS, codelenLengths );
	int numCodelen = NUM_CODELEN;
	while ( numCodelen > 4 && codelenLengths[codeLengthWriteOrder[numCodelen - 1]] == 0 ) {
		numCodelen--;
	}

	uint64_t dynamicCost = 3 + 5 + 5 + 4 + 3 * (uint64_t)numCodelen;
	for ( int i = 0; i < numOps; i++ ) {
		const int symbol = ops[i].symbol;
		dynamicCost += codelenLengths[symbol] + ( symbol == 16 ? 2 : ( symbol == 17 ? 3 : ( symbol == 18 ? 7 : 0 ) ) );
	}
	dynamicCost += DataCost( s, litlenLengths, distLengths );
	const uint64_t fixedCost = 3 + DataCost( s, s.tables->fixedLitlenLengths, s.tables->fixedDistLengths );
	const uint64_t storedCost = StoredCost( s.bw.count, s.blockBytes );

	bitWriter_t &bw = s.bw;
	if ( storedCost < dynamicCost && storedCost < fixedCost ) {
		WriteStored( bw, s.data + s.blockStart, s.blockBytes, final );
	} else if ( fixedCost <= dynamicCost ) {
		PutBits( bw, final ? 1 : 0, 1 );
		PutBits( bw, 1, 2 );
		WriteSymbols( s, s.tables->fixedLitlenCodes, s.tables->fixedLitlenLengths, s.tables->fixedDistCodes, s.tables->fixedDistLengths );
	} else {
		uint16_t litlenCodes[NUM_LITLEN];
		uint16_t distCodes[NUM_DIST];
		uint16_t codelenCodes[NUM_CODELEN];
		AssignCodes( litlenLengths, NUM_LITLEN, litlenCodes );
		AssignCodes( distLengths, NUM_DIST, distCodes );
		AssignCodes( codelenLengths, NUM_CODELEN, codelenCodes );

		PutBits( bw, final ? 1 : 0, 1 );
		PutBits( bw, 2, 2 );
		PutBits( bw, (uint32_t)( numLitlen - 257 ), 5 );
		PutBits( bw, (uint32_t)( numDist - 1 ), 5 );
		PutBits( bw, (uint32_t)( numCodelen - 4 ), 4 );
		for ( int i = 0; i < numCodelen; i++ ) {
			PutBits( bw, codelenLengths[codeLengthWriteOrder[i]], 3 );
		}
		for ( int i = 0; i < numOps; i++ ) {
			const int symbol = ops[i].symbol;
			PutBits( bw, codelenCodes[symbol], codelenLengths[symbol] );
			if ( symbol >= 16 ) {
				PutBits( bw, ops[i].extra, symbol == 16 ? 2 : ( symbol == 17 ? 3 : 7 ) );
			}
		}
		WriteSymbols( s, litlenCodes, litlenLengths, distCodes, distLengths );
	}

	s.blockStart += s.blockBytes;
	s.blockBytes = 0;
	s.numSymbols = 0;
	memset( s.litlenFreq, 0, sizeof( s.litlenFreq ) );
	memset( s.distFreq, 0, sizeof( s.distFreq ) );
}

static inline void EmitLiteral( deflateState_t &s, uint8_t literal ) {
	s.symbols[s.numSymbols++] = literal;
	s.litlenFreq[literal]++;
	s.blockBytes++;
	if ( s.numSymbols == BLOCK_SYMBOLS ) {
		FlushBlock( s, false );
	}
}

static inline void EmitMatch( deflateState_t &s, int length, int32_t distance ) {
	const uint32_t lengthMinus3 = (uint32_t)( length - MIN_MATCH );
	const uint32_t distMinus1 = (uint32_t)( distance - 1 );
	s.symbols[s.numSymbols++] = MATCH_FLAG | ( lengthMinus3 << 16 ) | distMinus1;
	s.litlenFreq[257 + s.tables->lengthCode[lengthMinus3]]++;
	s.distFreq[s.tables->distCode[distMinus1]]++;
	s.blockBytes += length;
	if ( s.numSymbols == BLOCK_SYMBOLS ) {
		FlushBlock( s, false );
	}
}

static void CompressGreedy( deflateState_t &s ) {
	const deflateLevel_t &config = *s.config;
	int32_t pos = 0;
	while ( pos < s.size ) {
		int length = 0;
		int32_t distance = 0;
		if ( pos + MIN_MATCH <= s.size ) {
			InsertPosition( s, pos );
			length = LongestMatch( s, pos, MIN_MATCH - 1, config.maxChain, &distance );
			if ( length == MIN_MATCH && distance > TOO_FAR ) {
				length = 0;
			}
		}
		if ( length >= MIN_MATCH ) {
			EmitMatch( s, length, distance );
			const int32_t end = pos + length;
			if ( length <= config.maxLazy ) {
				const int32_t lastInsert = end < s.size - MIN_MATCH + 1 ? end : s.size - MIN_MATCH + 1;
				for ( int32_t p = pos + 1; p < lastInsert; p++ ) {
					InsertPosition( s, p );
				}
			}
			pos = end;
		} else {
			EmitLiteral( s, s.data[pos] );
			pos++;
		}
	}
}

static void CompressLazy( deflateState_t &s ) {
	const deflateLevel_t &config = *s.config;
	int32_t pos = 0;
	int prevLength = 0;
	int32_t prevDistance = 0;
	bool havePrev = false;

	while ( pos < s.size ) {
		int length = 0;
		int32_t distance = 0;
		if ( pos + MIN_MATCH <= s.size ) {
			InsertPosition( s, pos );
			if ( prevLength < config.maxLazy ) {
				int chain = config.maxChain;
				if ( prevLength >= config.goodLength ) {
					chain >>= 2;
				}
				const int threshold = prevLength > MIN_MATCH - 1 ? prevLength : MIN_MATCH - 1;
				length = LongestMatch( s, pos, threshold, chain, &distance );
				if ( length == MIN_MATCH && distance > TOO_FAR ) {
					length = 0;
				}
			}
		}

		if ( havePrev && prevLength >= MIN_MATCH && length <= prevLength ) {
			// the match found one byte back is at least as good: take it
			EmitMatch( s, prevLength, prevDistance );
			const int32_t end = pos - 1 + prevLength;
			const int32_t lastInsert = end < s.size - MIN_MATCH + 1 ? end : s.size - MIN_MATCH + 1;
			for ( int32_t p = pos + 1; p < lastInsert; p++ ) {
				InsertPosition( s, p );
			}
			pos = end;
			havePrev = false;
			prevLength = 0;
			continue;
		}
		if ( havePrev ) {
			EmitLiteral( s, s.data[pos - 1] );
		}
		havePrev = true;
		prevLength = length;
		prevDistance = distance;
		pos++;
	}
	if ( havePrev ) {
		EmitLiteral( s, s.data[pos - 1] );
	}
}

/*
================
Zlib_CompressBound

Every block is written in whichever form is cheapest, so a block never costs
more than storing it: 5 bytes per stored chunk of up to 65535 bytes, and a
non-final block always covers at least BLOCK_SYMBOLS input bytes.
================
*/
size_t Zlib_CompressBound( size_t inSize ) {
	return inSize + ( inSize >> 11 ) + 64;
}

/*
================
Zlib_Compress
================
*/
size_t Zlib_Compress( const uint8_t *in, size_t inSize, uint8_t *out, int level ) {
	if ( inSize > 0x7FFF0000u ) {
		return 0;
	}
	if ( level < 0 ) {
		level = 6;
	}
	if ( level > 9 ) {
		level = 9;
	}

	const uint32_t cmf = 0x78;
	const uint32_t levelFlag = level < 2 ? 0 : ( level < 6 ? 1 : ( level == 6 ? 2 : 3 ) );
	uint32_t flg = levelFlag << 6;
	flg += 31 - ( ( cmf << 8 ) | flg ) % 31;
	out[0] = (uint8_t)cmf;
	out[1] = (uint8_t)flg;

	deflateState_t s;
	memset( &s, 0, sizeof( s ) );
	s.data = in;
	s.size = (int32_t)inSize;
	s.config = &deflateLevels[level];
	s.tables = &DeflateTables();
	s.bw.out = out + 2;

	if ( level == 0 || inSize == 0 ) {
		if ( inSize == 0 ) {
			// one empty fixed block
			PutBits( s.bw, 1, 1 );
			PutBits( s.bw, 1, 2 );
			PutBits( s.bw, 0, 7 );
		} else {
			WriteStored( s.bw, in, (int32_t)inSize, true );
		}
	} else {
		s.head4 = (int32_t *)malloc( sizeof( int32_t ) * HASH_SIZE );
		s.head3 = (int32_t *)malloc( sizeof( int32_t ) * HASH3_SIZE );
		s.prev = (int32_t *)malloc( sizeof( int32_t ) * WINDOW_SIZE );
		s.symbols = (uint32_t *)malloc( sizeof( uint32_t ) * BLOCK_SYMBOLS );
		if ( s.head4 == NULL || s.head3 == NULL || s.prev == NULL || s.symbols == NULL ) {
			free( s.head4 );
			free( s.head3 );
			free( s.prev );
			free( s.symbols );
			return 0;
		}
		// all bits set: every entry reads as position -1
		memset( s.head4, 0xFF, sizeof( int32_t ) * HASH_SIZE );
		memset( s.head3, 0xFF, sizeof( int32_t ) * HASH3_SIZE );
		memset( s.prev, 0xFF, sizeof( int32_t ) * WINDOW_SIZE );
		if ( s.config->lazy ) {
			CompressLazy( s );
		} else {
			CompressGreedy( s );
		}
		FlushBlock( s, true );
		free( s.head4 );
		free( s.head3 );
		free( s.prev );
		free( s.symbols );
	}
	FlushBytes( s.bw );

	const uint32_t adler = Zlib_Adler32( 1, in, inSize );
	s.bw.out[0] = (uint8_t)( adler >> 24 );
	s.bw.out[1] = (uint8_t)( adler >> 16 );
	s.bw.out[2] = (uint8_t)( adler >> 8 );
	s.bw.out[3] = (uint8_t)adler;
	s.bw.out += 4;
	return (size_t)( s.bw.out - out );
}
