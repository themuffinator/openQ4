// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef __BC6HLAYOUT_H__
#define __BC6HLAYOUT_H__

/*
================================================================================================
BC6H block layout, shared by the decoder and the encoder so the two cannot
disagree about where a bit lives.

A block starts with a 2-bit mode (modes 1 and 2) or a 5-bit one (the rest).
The endpoint bits that follow are scattered: each mode interleaves the high
bits of one endpoint with the low bits of another. Each mode below lists that
order as segments; a segment reads `count` bits, lowest first, into bits
`lsb` up of one endpoint channel. Modes 13 and 14 store the top bits of the
base endpoint in reverse order, which the tables spell out one bit at a time.

Endpoint channels are named as in the Direct3D 11 functional specification:
W and X are region 0's endpoints, Y and Z region 1's.
================================================================================================
*/

enum bc6hField_t {
	BC6H_RW, BC6H_GW, BC6H_BW,
	BC6H_RX, BC6H_GX, BC6H_BX,
	BC6H_RY, BC6H_GY, BC6H_BY,
	BC6H_RZ, BC6H_GZ, BC6H_BZ
};

struct bc6hSegment_t {
	unsigned char	field;
	unsigned char	lsb;
	unsigned char	count;
};

static const int BC6H_MAX_SEGMENTS = 24;

struct bc6hMode_t {
	int				modeBits;		// 2 or 5
	int				modeValue;
	int				regions;		// 1 or 2
	bool			transformed;	// X, Y and Z are signed deltas from W
	int				endpointBits;	// precision of W, and of every endpoint once rebuilt
	int				deltaBits[3];	// precision of X, Y and Z per channel
	int				numSegments;
	bc6hSegment_t	segments[BC6H_MAX_SEGMENTS];
};

#define S( f, l, c ) { BC6H_##f, l, c }

static const bc6hMode_t bc6hModes[14] = {
	// mode 1
	{ 2, 0x00, 2, true, 10, { 5, 5, 5 }, 19, {
		S(GY,4,1), S(BY,4,1), S(BZ,4,1), S(RW,0,10), S(GW,0,10), S(BW,0,10), S(RX,0,5), S(GZ,4,1), S(GY,0,4),
		S(GX,0,5), S(BZ,0,1), S(GZ,0,4), S(BX,0,5), S(BZ,1,1), S(BY,0,4), S(RY,0,5), S(BZ,2,1), S(RZ,0,5), S(BZ,3,1) } },
	// mode 2
	{ 2, 0x01, 2, true, 7, { 6, 6, 6 }, 23, {
		S(GY,5,1), S(GZ,4,1), S(GZ,5,1), S(RW,0,7), S(BZ,0,1), S(BZ,1,1), S(BY,4,1), S(GW,0,7), S(BY,5,1), S(BZ,2,1),
		S(GY,4,1), S(BW,0,7), S(BZ,3,1), S(BZ,5,1), S(BZ,4,1), S(RX,0,6), S(GY,0,4), S(GX,0,6), S(GZ,0,4), S(BX,0,6),
		S(BY,0,4), S(RY,0,6), S(RZ,0,6) } },
	// mode 3
	{ 5, 0x02, 2, true, 11, { 5, 4, 4 }, 18, {
		S(RW,0,10), S(GW,0,10), S(BW,0,10), S(RX,0,5), S(RW,10,1), S(GY,0,4), S(GX,0,4), S(GW,10,1), S(BZ,0,1),
		S(GZ,0,4), S(BX,0,4), S(BW,10,1), S(BZ,1,1), S(BY,0,4), S(RY,0,5), S(BZ,2,1), S(RZ,0,5), S(BZ,3,1) } },
	// mode 4
	{ 5, 0x06, 2, true, 11, { 4, 5, 4 }, 20, {
		S(RW,0,10), S(GW,0,10), S(BW,0,10), S(RX,0,4), S(RW,10,1), S(GZ,4,1), S(GY,0,4), S(GX,0,5), S(GW,10,1),
		S(GZ,0,4), S(BX,0,4), S(BW,10,1), S(BZ,1,1), S(BY,0,4), S(RY,0,4), S(BZ,0,1), S(BZ,2,1), S(RZ,0,4),
		S(GY,4,1), S(BZ,3,1) } },
	// mode 5
	{ 5, 0x0a, 2, true, 11, { 4, 4, 5 }, 20, {
		S(RW,0,10), S(GW,0,10), S(BW,0,10), S(RX,0,4), S(RW,10,1), S(BY,4,1), S(GY,0,4), S(GX,0,4), S(GW,10,1),
		S(BZ,0,1), S(GZ,0,4), S(BX,0,5), S(BW,10,1), S(BY,0,4), S(RY,0,4), S(BZ,1,1), S(BZ,2,1), S(RZ,0,4),
		S(BZ,4,1), S(BZ,3,1) } },
	// mode 6
	{ 5, 0x0e, 2, true, 9, { 5, 5, 5 }, 19, {
		S(RW,0,9), S(BY,4,1), S(GW,0,9), S(GY,4,1), S(BW,0,9), S(BZ,4,1), S(RX,0,5), S(GZ,4,1), S(GY,0,4),
		S(GX,0,5), S(BZ,0,1), S(GZ,0,4), S(BX,0,5), S(BZ,1,1), S(BY,0,4), S(RY,0,5), S(BZ,2,1), S(RZ,0,5), S(BZ,3,1) } },
	// mode 7
	{ 5, 0x12, 2, true, 8, { 6, 5, 5 }, 19, {
		S(RW,0,8), S(GZ,4,1), S(BY,4,1), S(GW,0,8), S(BZ,2,1), S(GY,4,1), S(BW,0,8), S(BZ,3,1), S(BZ,4,1),
		S(RX,0,6), S(GY,0,4), S(GX,0,5), S(BZ,0,1), S(GZ,0,4), S(BX,0,5), S(BZ,1,1), S(BY,0,4), S(RY,0,6), S(RZ,0,6) } },
	// mode 8
	{ 5, 0x16, 2, true, 8, { 5, 6, 5 }, 21, {
		S(RW,0,8), S(BZ,0,1), S(BY,4,1), S(GW,0,8), S(GY,5,1), S(GY,4,1), S(BW,0,8), S(GZ,5,1), S(BZ,4,1),
		S(RX,0,5), S(GZ,4,1), S(GY,0,4), S(GX,0,6), S(GZ,0,4), S(BX,0,5), S(BZ,1,1), S(BY,0,4), S(RY,0,5),
		S(BZ,2,1), S(RZ,0,5), S(BZ,3,1) } },
	// mode 9
	{ 5, 0x1a, 2, true, 8, { 5, 5, 6 }, 21, {
		S(RW,0,8), S(BZ,1,1), S(BY,4,1), S(GW,0,8), S(BY,5,1), S(GY,4,1), S(BW,0,8), S(BZ,5,1), S(BZ,4,1),
		S(RX,0,5), S(GZ,4,1), S(GY,0,4), S(GX,0,5), S(BZ,0,1), S(GZ,0,4), S(BX,0,6), S(BY,0,4), S(RY,0,5),
		S(BZ,2,1), S(RZ,0,5), S(BZ,3,1) } },
	// mode 10: six raw bits per endpoint, no transform
	{ 5, 0x1e, 2, false, 6, { 6, 6, 6 }, 23, {
		S(RW,0,6), S(GZ,4,1), S(BZ,0,1), S(BZ,1,1), S(BY,4,1), S(GW,0,6), S(GY,5,1), S(BY,5,1), S(BZ,2,1),
		S(GY,4,1), S(BW,0,6), S(GZ,5,1), S(BZ,3,1), S(BZ,5,1), S(BZ,4,1), S(RX,0,6), S(GY,0,4), S(GX,0,6),
		S(GZ,0,4), S(BX,0,6), S(BY,0,4), S(RY,0,6), S(RZ,0,6) } },
	// mode 11: one region, ten raw bits per endpoint
	{ 5, 0x03, 1, false, 10, { 10, 10, 10 }, 6, {
		S(RW,0,10), S(GW,0,10), S(BW,0,10), S(RX,0,10), S(GX,0,10), S(BX,0,10) } },
	// mode 12
	{ 5, 0x07, 1, true, 11, { 9, 9, 9 }, 9, {
		S(RW,0,10), S(GW,0,10), S(BW,0,10), S(RX,0,9), S(RW,10,1), S(GX,0,9), S(GW,10,1), S(BX,0,9), S(BW,10,1) } },
	// mode 13
	{ 5, 0x0b, 1, true, 12, { 8, 8, 8 }, 12, {
		S(RW,0,10), S(GW,0,10), S(BW,0,10), S(RX,0,8), S(RW,11,1), S(RW,10,1), S(GX,0,8), S(GW,11,1), S(GW,10,1),
		S(BX,0,8), S(BW,11,1), S(BW,10,1) } },
	// mode 14
	{ 5, 0x0f, 1, true, 16, { 4, 4, 4 }, 24, {
		S(RW,0,10), S(GW,0,10), S(BW,0,10),
		S(RX,0,4), S(RW,15,1), S(RW,14,1), S(RW,13,1), S(RW,12,1), S(RW,11,1), S(RW,10,1),
		S(GX,0,4), S(GW,15,1), S(GW,14,1), S(GW,13,1), S(GW,12,1), S(GW,11,1), S(GW,10,1),
		S(BX,0,4), S(BW,15,1), S(BW,14,1), S(BW,13,1), S(BW,12,1), S(BW,11,1), S(BW,10,1) } }
};

#undef S

// the mode table index a block's first bits name, or -1 for a reserved mode
static inline int BC6H_ModeIndex( const byte *block ) {
	const int low2 = block[0] & 0x03;
	if ( low2 < 2 ) {
		return low2;
	}
	const int low5 = block[0] & 0x1f;
	for ( int i = 2; i < 14; i++ ) {
		if ( bc6hModes[i].modeValue == low5 ) {
			return i;
		}
	}
	return -1;	// 0x13, 0x17, 0x1b and 0x1f
}

// BC6H shares BC7's first 32 two-subset partitions; one bit per texel, row by
// row, texel 0 lowest
static const unsigned short bc6hPartitions[32] = {
	0xcccc, 0x8888, 0xeeee, 0xecc8, 0xc880, 0xfeec, 0xfec8, 0xec80,
	0xc800, 0xffec, 0xfe80, 0xe800, 0xffe8, 0xff00, 0xfff0, 0xf000,
	0xf710, 0x008e, 0x7100, 0x08ce, 0x008c, 0x7310, 0x3100, 0x8cce,
	0x088c, 0x3110, 0x6666, 0x366c, 0x17e8, 0x0ff0, 0x718e, 0x399c
};

// texel index of region 1's anchor
static const byte bc6hAnchor2[32] = {
	15,15,15,15,15,15,15,15, 15,15,15,15,15,15,15,15,
	15, 2, 8, 2, 2, 8, 8,15,  2, 8, 2, 2, 8, 8, 2, 2
};

static const int bc6hWeights3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
static const int bc6hWeights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

static inline int BC6H_PartitionRegion( int partition, int texel ) {
	return ( bc6hPartitions[ partition ] >> texel ) & 1;
}

static inline int BC6H_SignExtend( int value, int bits ) {
	const int shift = 32 - bits;
	return (int)( (unsigned int)value << shift ) >> shift;
}

// endpoint integer to the 16-bit interpolation domain (Direct3D's unquantize)
static inline int BC6H_Unquantize( int value, int bits, bool isSigned ) {
	if ( !isSigned ) {
		if ( bits >= 15 || value == 0 ) {
			return value;
		}
		if ( value == ( 1 << bits ) - 1 ) {
			return 0xffff;
		}
		return ( ( value << 16 ) + 0x8000 ) >> bits;
	}
	if ( bits >= 16 ) {
		return value;
	}
	const bool negative = value < 0;
	int magnitude = negative ? -value : value;
	int result;
	if ( magnitude == 0 ) {
		result = 0;
	} else if ( magnitude >= ( 1 << ( bits - 1 ) ) - 1 ) {
		result = 0x7fff;
	} else {
		result = ( ( magnitude << 15 ) + 0x4000 ) >> ( bits - 1 );
	}
	return negative ? -result : result;
}

static inline int BC6H_Interpolate( int a, int b, int weight ) {
	return ( a * ( 64 - weight ) + b * weight + 32 ) >> 6;
}

// interpolation domain to the half bit pattern the block decodes to
static inline unsigned short BC6H_FinishUnquantize( int value, bool isSigned ) {
	if ( !isSigned ) {
		return (unsigned short)( ( value * 31 ) >> 6 );
	}
	if ( value < 0 ) {
		// the sign goes on after the shift, so a magnitude that shifts to zero
		// is +0, not -0 (Direct3D and the hardware agree)
		const int magnitude = ( -value * 31 ) >> 5;
		return (unsigned short)( magnitude != 0 ? ( magnitude | 0x8000 ) : 0 );
	}
	return (unsigned short)( ( value * 31 ) >> 5 );
}

/*
Rebuilds the endpoints a block decodes with, from the raw field values: sign
extension for SF16, and for the transformed modes X, Y and Z turned from
deltas back into endpoints. fields[ endpoint * 3 + channel ] in, unquantized
endpoints out in the same order.
*/
static inline void BC6H_ResolveEndpoints( const bc6hMode_t &mode, bool isSigned, const int fields[12], int unquantized[12] ) {
	const int numEndpoints = mode.regions * 2;
	const int mask = ( 1 << mode.endpointBits ) - 1;
	for ( int channel = 0; channel < 3; channel++ ) {
		int base = fields[ channel ];
		if ( isSigned ) {
			base = BC6H_SignExtend( base, mode.endpointBits );
		}
		unquantized[ channel ] = BC6H_Unquantize( base, mode.endpointBits, isSigned );
		for ( int e = 1; e < numEndpoints; e++ ) {
			int value = fields[ e * 3 + channel ];
			if ( mode.transformed ) {
				value = ( BC6H_SignExtend( value, mode.deltaBits[ channel ] ) + fields[ channel ] ) & mask;
			}
			if ( isSigned ) {
				value = BC6H_SignExtend( value, mode.endpointBits );
			}
			unquantized[ e * 3 + channel ] = BC6H_Unquantize( value, mode.endpointBits, isSigned );
		}
	}
}

#endif // !__BC6HLAYOUT_H__
