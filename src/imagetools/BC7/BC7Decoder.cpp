// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../../idlib/precompiled.h"
#include "BC7Codec.h"

/*
================================================================================================
BC7 block layout, per mode: subsets, partition bits, rotation bits, index
selection bits, colour and alpha endpoint bits, per-endpoint and per-subset
p-bits, and the bit widths of the two index sets. The mode is the position of
the lowest set bit of the block.
================================================================================================
*/
struct bc7ModeInfo_t {
	int subsets;
	int partitionBits;
	int rotationBits;
	int indexSelectionBits;
	int colorBits;
	int alphaBits;
	int endpointPBits;
	int sharedPBits;
	int indexBits;
	int secondaryIndexBits;
};

static const bc7ModeInfo_t bc7Modes[8] = {
	{ 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
	{ 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
	{ 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
	{ 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
	{ 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
	{ 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
	{ 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
	{ 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 }
};

static const byte bc7Partitions2[64][16] = {
	{ 0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1 }, { 0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1 },
	{ 0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1 }, { 0,0,0,1,0,0,1,1,0,0,1,1,0,1,1,1 },
	{ 0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,1 }, { 0,0,1,1,0,1,1,1,0,1,1,1,1,1,1,1 },
	{ 0,0,0,1,0,0,1,1,0,1,1,1,1,1,1,1 }, { 0,0,0,0,0,0,0,1,0,0,1,1,0,1,1,1 },
	{ 0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,1 }, { 0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1 },
	{ 0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1 }, { 0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1 },
	{ 0,0,0,1,0,1,1,1,1,1,1,1,1,1,1,1 }, { 0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1 },
	{ 0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1 }, { 0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1 },
	{ 0,0,0,0,1,0,0,0,1,1,1,0,1,1,1,1 }, { 0,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0 },
	{ 0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,0 }, { 0,1,1,1,0,0,1,1,0,0,0,1,0,0,0,0 },
	{ 0,0,1,1,0,0,0,1,0,0,0,0,0,0,0,0 }, { 0,0,0,0,1,0,0,0,1,1,0,0,1,1,1,0 },
	{ 0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0 }, { 0,1,1,1,0,0,1,1,0,0,1,1,0,0,0,1 },
	{ 0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0 }, { 0,0,0,0,1,0,0,0,1,0,0,0,1,1,0,0 },
	{ 0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0 }, { 0,0,1,1,0,1,1,0,0,1,1,0,1,1,0,0 },
	{ 0,0,0,1,0,1,1,1,1,1,1,0,1,0,0,0 }, { 0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0 },
	{ 0,1,1,1,0,0,0,1,1,0,0,0,1,1,1,0 }, { 0,0,1,1,1,0,0,1,1,0,0,1,1,1,0,0 },
	{ 0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1 }, { 0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1 },
	{ 0,1,0,1,1,0,1,0,0,1,0,1,1,0,1,0 }, { 0,0,1,1,0,0,1,1,1,1,0,0,1,1,0,0 },
	{ 0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0 }, { 0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,0 },
	{ 0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1 }, { 0,1,0,1,1,0,1,0,1,0,1,0,0,1,0,1 },
	{ 0,1,1,1,0,0,1,1,1,1,0,0,1,1,1,0 }, { 0,0,0,1,0,0,1,1,1,1,0,0,1,0,0,0 },
	{ 0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0 }, { 0,0,1,1,1,0,1,1,1,1,0,1,1,1,0,0 },
	{ 0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0 }, { 0,0,1,1,1,1,0,0,1,1,0,0,0,0,1,1 },
	{ 0,1,1,0,0,1,1,0,1,0,0,1,1,0,0,1 }, { 0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0 },
	{ 0,1,0,0,1,1,1,0,0,1,0,0,0,0,0,0 }, { 0,0,1,0,0,1,1,1,0,0,1,0,0,0,0,0 },
	{ 0,0,0,0,0,0,1,0,0,1,1,1,0,0,1,0 }, { 0,0,0,0,0,1,0,0,1,1,1,0,0,1,0,0 },
	{ 0,1,1,0,1,1,0,0,1,0,0,1,0,0,1,1 }, { 0,0,1,1,0,1,1,0,1,1,0,0,1,0,0,1 },
	{ 0,1,1,0,0,0,1,1,1,0,0,1,1,1,0,0 }, { 0,0,1,1,1,0,0,1,1,1,0,0,0,1,1,0 },
	{ 0,1,1,0,1,1,0,0,1,1,0,0,1,0,0,1 }, { 0,1,1,0,0,0,1,1,0,0,1,1,1,0,0,1 },
	{ 0,1,1,1,1,1,1,0,1,0,0,0,0,0,0,1 }, { 0,0,0,1,1,0,0,0,1,1,1,0,0,1,1,1 },
	{ 0,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1 }, { 0,0,1,1,0,0,1,1,1,1,1,1,0,0,0,0 },
	{ 0,0,1,0,0,0,1,0,1,1,1,0,1,1,1,0 }, { 0,1,0,0,0,1,0,0,0,1,1,1,0,1,1,1 }
};

static const byte bc7Partitions3[64][16] = {
	{ 0,0,1,1,0,0,1,1,0,2,2,1,2,2,2,2 }, { 0,0,0,1,0,0,1,1,2,2,1,1,2,2,2,1 },
	{ 0,0,0,0,2,0,0,1,2,2,1,1,2,2,1,1 }, { 0,2,2,2,0,0,2,2,0,0,1,1,0,1,1,1 },
	{ 0,0,0,0,0,0,0,0,1,1,2,2,1,1,2,2 }, { 0,0,1,1,0,0,1,1,0,0,2,2,0,0,2,2 },
	{ 0,0,2,2,0,0,2,2,1,1,1,1,1,1,1,1 }, { 0,0,1,1,0,0,1,1,2,2,1,1,2,2,1,1 },
	{ 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2 }, { 0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2 },
	{ 0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2 }, { 0,0,1,2,0,0,1,2,0,0,1,2,0,0,1,2 },
	{ 0,1,1,2,0,1,1,2,0,1,1,2,0,1,1,2 }, { 0,1,2,2,0,1,2,2,0,1,2,2,0,1,2,2 },
	{ 0,0,1,1,0,1,1,2,1,1,2,2,1,2,2,2 }, { 0,0,1,1,2,0,0,1,2,2,0,0,2,2,2,0 },
	{ 0,0,0,1,0,0,1,1,0,1,1,2,1,1,2,2 }, { 0,1,1,1,0,0,1,1,2,0,0,1,2,2,0,0 },
	{ 0,0,0,0,1,1,2,2,1,1,2,2,1,1,2,2 }, { 0,0,2,2,0,0,2,2,0,0,2,2,1,1,1,1 },
	{ 0,1,1,1,0,1,1,1,0,2,2,2,0,2,2,2 }, { 0,0,0,1,0,0,0,1,2,2,2,1,2,2,2,1 },
	{ 0,0,0,0,0,0,1,1,0,1,2,2,0,1,2,2 }, { 0,0,0,0,1,1,0,0,2,2,1,0,2,2,1,0 },
	{ 0,1,2,2,0,1,2,2,0,0,1,1,0,0,0,0 }, { 0,0,1,2,0,0,1,2,1,1,2,2,2,2,2,2 },
	{ 0,1,1,0,1,2,2,1,1,2,2,1,0,1,1,0 }, { 0,0,0,0,0,1,1,0,1,2,2,1,1,2,2,1 },
	{ 0,0,2,2,1,1,0,2,1,1,0,2,0,0,2,2 }, { 0,1,1,0,0,1,1,0,2,0,0,2,2,2,2,2 },
	{ 0,0,1,1,0,1,2,2,0,1,2,2,0,0,1,1 }, { 0,0,0,0,2,0,0,0,2,2,1,1,2,2,2,1 },
	{ 0,0,0,0,0,0,0,2,1,1,2,2,1,2,2,2 }, { 0,2,2,2,0,0,2,2,0,0,1,2,0,0,1,1 },
	{ 0,0,1,1,0,0,1,2,0,0,2,2,0,2,2,2 }, { 0,1,2,0,0,1,2,0,0,1,2,0,0,1,2,0 },
	{ 0,0,0,0,1,1,1,1,2,2,2,2,0,0,0,0 }, { 0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0 },
	{ 0,1,2,0,2,0,1,2,1,2,0,1,0,1,2,0 }, { 0,0,1,1,2,2,0,0,1,1,2,2,0,0,1,1 },
	{ 0,0,1,1,1,1,2,2,2,2,0,0,0,0,1,1 }, { 0,1,0,1,0,1,0,1,2,2,2,2,2,2,2,2 },
	{ 0,0,0,0,0,0,0,0,2,1,2,1,2,1,2,1 }, { 0,0,2,2,1,1,2,2,0,0,2,2,1,1,2,2 },
	{ 0,0,2,2,0,0,1,1,0,0,2,2,0,0,1,1 }, { 0,2,2,0,1,2,2,1,0,2,2,0,1,2,2,1 },
	{ 0,1,0,1,2,2,2,2,2,2,2,2,0,1,0,1 }, { 0,0,0,0,2,1,2,1,2,1,2,1,2,1,2,1 },
	{ 0,1,0,1,0,1,0,1,0,1,0,1,2,2,2,2 }, { 0,2,2,2,0,1,1,1,0,2,2,2,0,1,1,1 },
	{ 0,0,0,2,1,1,1,2,0,0,0,2,1,1,1,2 }, { 0,0,0,0,2,1,1,2,2,1,1,2,2,1,1,2 },
	{ 0,2,2,2,0,1,1,1,0,1,1,1,0,2,2,2 }, { 0,0,0,2,1,1,1,2,1,1,1,2,0,0,0,2 },
	{ 0,1,1,0,0,1,1,0,0,1,1,0,2,2,2,2 }, { 0,0,0,0,0,0,0,0,2,1,1,2,2,1,1,2 },
	{ 0,1,1,0,0,1,1,0,2,2,2,2,2,2,2,2 }, { 0,0,2,2,0,0,1,1,0,0,1,1,0,0,2,2 },
	{ 0,0,2,2,1,1,2,2,1,1,2,2,0,0,2,2 }, { 0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,2 },
	{ 0,0,0,2,0,0,0,1,0,0,0,2,0,0,0,1 }, { 0,2,2,2,1,2,2,2,0,2,2,2,1,2,2,2 },
	{ 0,1,0,1,2,2,2,2,2,2,2,2,2,2,2,2 }, { 0,1,1,1,2,0,1,1,2,2,0,1,2,2,2,0 }
};

// texel index of the second subset's anchor, two-subset partitions
static const byte bc7Anchor2[64] = {
	15,15,15,15,15,15,15,15, 15,15,15,15,15,15,15,15,
	15, 2, 8, 2, 2, 8, 8,15,  2, 8, 2, 2, 8, 8, 2, 2,
	15,15, 6, 8, 2, 8,15,15,  2, 8, 2, 2, 2,15,15, 6,
	 6, 2, 6, 8,15,15, 2, 2, 15,15,15,15,15, 2, 2,15
};

// texel indices of the second and third subsets' anchors, three-subset partitions
static const byte bc7Anchor3Second[64] = {
	 3, 3,15,15, 8, 3,15,15,  8, 8, 6, 6, 6, 5, 3, 3,
	 3, 3, 8,15, 3, 3, 6,10,  5, 8, 8, 6, 8, 5,15,15,
	 8,15, 3, 5, 6,10, 8,15, 15, 3,15, 5,15,15,15,15,
	 3,15, 5, 5, 5, 8, 5,10,  5,10, 8,13,15,12, 3, 3
};

static const byte bc7Anchor3Third[64] = {
	15, 8, 8, 3,15,15, 3, 8, 15,15,15,15,15,15,15, 8,
	15, 8,15, 3,15, 8,15, 8,  3,15, 6,10,15,15,10, 8,
	15, 3,15,10,10, 8, 9,10,  6,15, 8,15, 3, 6, 6, 8,
	15, 3,15,15,15,15,15,15, 15,15,15,15, 3,15,15, 8
};

static const byte bc7Weights2[4] = { 0, 21, 43, 64 };
static const byte bc7Weights3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
static const byte bc7Weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

class idBC7BitReader {
public:
	explicit idBC7BitReader( const byte *block ) : data( block ), position( 0 ) {}

	int Read( int count ) {
		int value = 0;
		for ( int i = 0; i < count; i++, position++ ) {
			value |= ( ( data[ position >> 3 ] >> ( position & 7 ) ) & 1 ) << i;
		}
		return value;
	}

private:
	const byte *	data;
	int				position;
};

static int BC7_Weight( int indexBits, int index ) {
	switch ( indexBits ) {
		case 2: return bc7Weights2[ index ];
		case 3: return bc7Weights3[ index ];
		default: return bc7Weights4[ index ];
	}
}

static byte BC7_Interpolate( int e0, int e1, int weight ) {
	return (byte)( ( ( 64 - weight ) * e0 + weight * e1 + 32 ) >> 6 );
}

static int BC7_Unquantize( int value, int bits ) {
	value <<= 8 - bits;
	return value | ( value >> bits );
}

static int BC7_AnchorIndex( const bc7ModeInfo_t &mode, int partition, int subset ) {
	if ( subset == 0 ) {
		return 0;
	}
	if ( mode.subsets == 2 ) {
		return bc7Anchor2[ partition ];
	}
	return subset == 1 ? bc7Anchor3Second[ partition ] : bc7Anchor3Third[ partition ];
}

/*
================
BC7_DecodeBlock
================
*/
void BC7_DecodeBlock( const byte *block, byte *outRgba, int rowPitchBytes ) {
	int modeIndex = 0;
	while ( modeIndex < 8 && ( block[0] & ( 1 << modeIndex ) ) == 0 ) {
		modeIndex++;
	}
	if ( modeIndex == 8 ) {
		// no mode bit set: a reserved encoding that decodes to transparent black
		for ( int y = 0; y < 4; y++ ) {
			memset( outRgba + y * rowPitchBytes, 0, 16 );
		}
		return;
	}

	const bc7ModeInfo_t &mode = bc7Modes[ modeIndex ];
	idBC7BitReader bits( block );
	bits.Read( modeIndex + 1 );

	const int partition = bits.Read( mode.partitionBits );
	const int rotation = bits.Read( mode.rotationBits );
	const int indexSelection = bits.Read( mode.indexSelectionBits );

	// endpoints[subset * 2 + end][channel]
	int endpoints[6][4];
	const int numEndpoints = mode.subsets * 2;
	for ( int channel = 0; channel < 3; channel++ ) {
		for ( int e = 0; e < numEndpoints; e++ ) {
			endpoints[e][channel] = bits.Read( mode.colorBits );
		}
	}
	for ( int e = 0; e < numEndpoints; e++ ) {
		endpoints[e][3] = mode.alphaBits > 0 ? bits.Read( mode.alphaBits ) : 255;
	}

	int colorPrecision = mode.colorBits;
	int alphaPrecision = mode.alphaBits;
	if ( mode.endpointPBits ) {
		for ( int e = 0; e < numEndpoints; e++ ) {
			const int p = bits.Read( 1 );
			for ( int channel = 0; channel < 4; channel++ ) {
				if ( channel < 3 || mode.alphaBits > 0 ) {
					endpoints[e][channel] = ( endpoints[e][channel] << 1 ) | p;
				}
			}
		}
		colorPrecision++;
		if ( alphaPrecision > 0 ) {
			alphaPrecision++;
		}
	} else if ( mode.sharedPBits ) {
		for ( int s = 0; s < mode.subsets; s++ ) {
			const int p = bits.Read( 1 );
			for ( int e = s * 2; e < s * 2 + 2; e++ ) {
				for ( int channel = 0; channel < 3; channel++ ) {
					endpoints[e][channel] = ( endpoints[e][channel] << 1 ) | p;
				}
			}
		}
		colorPrecision++;
	}

	for ( int e = 0; e < numEndpoints; e++ ) {
		for ( int channel = 0; channel < 3; channel++ ) {
			endpoints[e][channel] = BC7_Unquantize( endpoints[e][channel], colorPrecision );
		}
		if ( alphaPrecision > 0 ) {
			endpoints[e][3] = BC7_Unquantize( endpoints[e][3], alphaPrecision );
		}
	}

	const byte *partitionTable = NULL;
	if ( mode.subsets == 2 ) {
		partitionTable = bc7Partitions2[ partition ];
	} else if ( mode.subsets == 3 ) {
		partitionTable = bc7Partitions3[ partition ];
	}

	// The anchor texel of each subset stores its index one bit short: the top
	// bit is implied zero. Read every primary index, then the secondary set.
	int primary[16];
	int secondary[16];
	for ( int texel = 0; texel < 16; texel++ ) {
		const int subset = partitionTable != NULL ? partitionTable[ texel ] : 0;
		const bool anchor = texel == BC7_AnchorIndex( mode, partition, subset );
		primary[ texel ] = bits.Read( mode.indexBits - ( anchor ? 1 : 0 ) );
	}
	if ( mode.secondaryIndexBits > 0 ) {
		for ( int texel = 0; texel < 16; texel++ ) {
			secondary[ texel ] = bits.Read( mode.secondaryIndexBits - ( texel == 0 ? 1 : 0 ) );
		}
	}

	for ( int texel = 0; texel < 16; texel++ ) {
		const int subset = partitionTable != NULL ? partitionTable[ texel ] : 0;
		const int *e0 = endpoints[ subset * 2 ];
		const int *e1 = endpoints[ subset * 2 + 1 ];

		int colorWeight;
		int alphaWeight;
		if ( mode.secondaryIndexBits > 0 ) {
			// mode 4's selection bit swaps which set drives colour and which
			// alpha; mode 5 has no selection bit
			if ( indexSelection ) {
				colorWeight = BC7_Weight( mode.secondaryIndexBits, secondary[ texel ] );
				alphaWeight = BC7_Weight( mode.indexBits, primary[ texel ] );
			} else {
				colorWeight = BC7_Weight( mode.indexBits, primary[ texel ] );
				alphaWeight = BC7_Weight( mode.secondaryIndexBits, secondary[ texel ] );
			}
		} else {
			colorWeight = alphaWeight = BC7_Weight( mode.indexBits, primary[ texel ] );
		}

		byte rgba[4];
		for ( int channel = 0; channel < 3; channel++ ) {
			rgba[ channel ] = BC7_Interpolate( e0[ channel ], e1[ channel ], colorWeight );
		}
		rgba[3] = BC7_Interpolate( e0[3], e1[3], alphaWeight );

		if ( rotation > 0 ) {
			const byte swap = rgba[3];
			rgba[3] = rgba[ rotation - 1 ];
			rgba[ rotation - 1 ] = swap;
		}

		byte *out = outRgba + ( texel >> 2 ) * rowPitchBytes + ( texel & 3 ) * 4;
		out[0] = rgba[0];
		out[1] = rgba[1];
		out[2] = rgba[2];
		out[3] = rgba[3];
	}
}

/*
================
BC7_DecodeImage
================
*/
void BC7_DecodeImage( const byte *blocks, byte *outRgba, int width, int height ) {
	const int rowPitch = width * 4;
	for ( int by = 0; by < height; by += 4 ) {
		for ( int bx = 0; bx < width; bx += 4 ) {
			BC7_DecodeBlock( blocks, outRgba + by * rowPitch + bx * 4, rowPitch );
			blocks += 16;
		}
	}
}
