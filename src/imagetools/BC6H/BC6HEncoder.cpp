// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../../idlib/precompiled.h"
#include "BC6HCodec.h"
#include "BC6HLayout.h"

/*
================================================================================================
BC6H encoding.

Work happens in the domain the format interpolates in: a texel's half bit
pattern, read as a signed magnitude, scaled by 64/31 (UF16) or 32/31 (SF16)
so it lines up with the 16-bit unquantized endpoints. Half bit patterns are
close to logarithmic in the value they encode, so a squared error there
weighs a dark texel's error like a bright one's, which is what BC6H is built
around.

For each candidate (a mode and, for the two-region modes, a partition) the
encoder fits each region's endpoints along its principal axis, quantizes them
to the mode's precision, packs the deltas the transformed modes need, picks
every texel's index against the endpoints the decoder will actually rebuild,
then refines the endpoints by least squares and tries again. The candidate
with the smallest error in half bit patterns wins.
================================================================================================
*/

static const int BC6H_MAX_PARTITION_CANDIDATES = 32;

struct bc6hTexels_t {
	float	target[16][3];	// interpolation domain
	int		halfBits[16][3];	// signed magnitude of the clamped input half
	bool	isSigned;
};

struct bc6hCandidate_t {
	int		modeIndex;
	int		partition;
	int		fields[12];
	int		indices[16];
	long long error;
};

// clamped signed magnitude of a half, as the format can store it
static int BC6H_HalfToSignedBits( unsigned short half, bool isSigned ) {
	int magnitude = half & 0x7fff;
	if ( magnitude > 0x7bff ) {
		magnitude = 0x7bff;	// infinity and NaN become the largest finite value
	}
	if ( ( half & 0x8000 ) != 0 ) {
		return isSigned ? -magnitude : 0;
	}
	return magnitude;
}

static int BC6H_DecodedSignedBits( unsigned short half ) {
	return ( half & 0x8000 ) != 0 ? -( half & 0x7fff ) : half;
}

static float BC6H_DomainMax( bool isSigned ) {
	return isSigned ? 32767.0f : 65535.0f;
}

static float BC6H_DomainMin( bool isSigned ) {
	return isSigned ? -32767.0f : 0.0f;
}

static float BC6H_ClampDomain( float value, bool isSigned ) {
	const float lo = BC6H_DomainMin( isSigned );
	const float hi = BC6H_DomainMax( isSigned );
	return value < lo ? lo : ( value > hi ? hi : value );
}

static int BC6H_RoundToInt( float value ) {
	return value >= 0.0f ? (int)( value + 0.5f ) : -(int)( -value + 0.5f );
}

/*
================
BC6H_QuantizeEndpoint

The endpoint integer of `bits` precision whose unquantized value is closest
to `value`.
================
*/
static int BC6H_QuantizeEndpoint( float value, int bits, bool isSigned ) {
	int lo, hi;
	if ( isSigned ) {
		// -2^(bits-1) unquantizes like -(2^(bits-1)-1), and at 16 bits it
		// would interpolate to -32768, one past the largest finite magnitude
		hi = ( 1 << ( bits - 1 ) ) - 1;
		lo = -hi;
	} else {
		lo = 0;
		hi = ( 1 << bits ) - 1;
	}
	int guess;
	if ( ( !isSigned && bits >= 15 ) || ( isSigned && bits >= 16 ) ) {
		guess = BC6H_RoundToInt( value );
	} else {
		const float step = (float)( 1 << ( 16 - bits ) );
		guess = BC6H_RoundToInt( value / step - ( value >= 0.0f ? 0.5f : -0.5f ) );
	}
	int best = 0;
	float bestError = -1.0f;
	for ( int q = guess - 1; q <= guess + 1; q++ ) {
		const int clamped = q < lo ? lo : ( q > hi ? hi : q );
		const float diff = (float)BC6H_Unquantize( clamped, bits, isSigned ) - value;
		const float error = diff * diff;
		if ( bestError < 0.0f || error < bestError ) {
			bestError = error;
			best = clamped;
		}
	}
	return best;
}

/*
================
BC6H_PackFields

Quantizes float endpoints for a mode into the raw field values the block
stores. For the transformed modes the deltas are clamped to what the mode can
hold, and so is every rebuilt endpoint, so the block always decodes to the
endpoints the fields describe.
================
*/
static void BC6H_PackFields( const bc6hMode_t &mode, bool isSigned, const float endpoints[4][3], int fields[12] ) {
	const int numEndpoints = mode.regions * 2;
	const int bits = mode.endpointBits;
	const int mask = ( 1 << bits ) - 1;
	int lo, hi;
	if ( isSigned ) {
		hi = ( 1 << ( bits - 1 ) ) - 1;
		lo = -hi;
	} else {
		lo = 0;
		hi = mask;
	}
	for ( int channel = 0; channel < 3; channel++ ) {
		const int base = BC6H_QuantizeEndpoint( endpoints[0][channel], bits, isSigned );
		fields[ channel ] = base & mask;
		for ( int e = 1; e < numEndpoints; e++ ) {
			const int q = BC6H_QuantizeEndpoint( endpoints[e][channel], bits, isSigned );
			if ( !mode.transformed ) {
				fields[ e * 3 + channel ] = q & mask;
				continue;
			}
			const int deltaBits = mode.deltaBits[ channel ];
			const int deltaMax = ( 1 << ( deltaBits - 1 ) ) - 1;
			const int deltaMin = -( 1 << ( deltaBits - 1 ) );
			int delta = q - base;
			delta = delta < deltaMin ? deltaMin : ( delta > deltaMax ? deltaMax : delta );
			int rebuilt = base + delta;
			if ( rebuilt < lo || rebuilt > hi ) {
				rebuilt = rebuilt < lo ? lo : hi;
				delta = rebuilt - base;
			}
			fields[ e * 3 + channel ] = delta & ( ( 1 << deltaBits ) - 1 );
		}
	}
	for ( int e = numEndpoints; e < 4; e++ ) {
		fields[ e * 3 + 0 ] = fields[ e * 3 + 1 ] = fields[ e * 3 + 2 ] = 0;
	}
}

/*
================
BC6H_ChooseIndices

Picks each texel's index against the endpoints the decoder rebuilds from
`fields`, scoring exactly what the decoder will output. A region's anchor
texel may only use the lower half of the indices.
================
*/
static long long BC6H_ChooseIndices( const bc6hMode_t &mode, int partition, const int fields[12], const bc6hTexels_t &texels, int indices[16] ) {
	int endpoints[12];
	BC6H_ResolveEndpoints( mode, texels.isSigned, fields, endpoints );

	const int indexCount = mode.regions == 2 ? 8 : 16;
	const int *weights = mode.regions == 2 ? bc6hWeights3 : bc6hWeights4;
	int palette[2][16][3];
	for ( int region = 0; region < mode.regions; region++ ) {
		const int *a = endpoints + region * 6;
		const int *b = a + 3;
		for ( int i = 0; i < indexCount; i++ ) {
			for ( int channel = 0; channel < 3; channel++ ) {
				palette[ region ][ i ][ channel ] = BC6H_DecodedSignedBits(
					BC6H_FinishUnquantize( BC6H_Interpolate( a[ channel ], b[ channel ], weights[i] ), texels.isSigned ) );
			}
		}
	}

	const int anchor1 = mode.regions == 2 ? bc6hAnchor2[ partition ] : -1;
	long long total = 0;
	for ( int texel = 0; texel < 16; texel++ ) {
		const int region = mode.regions == 2 ? BC6H_PartitionRegion( partition, texel ) : 0;
		const int limit = ( texel == 0 || texel == anchor1 ) ? indexCount / 2 : indexCount;
		long long bestError = -1;
		int best = 0;
		for ( int i = 0; i < limit; i++ ) {
			long long error = 0;
			for ( int channel = 0; channel < 3; channel++ ) {
				const long long diff = palette[ region ][ i ][ channel ] - texels.halfBits[ texel ][ channel ];
				error += diff * diff;
			}
			if ( bestError < 0 || error < bestError ) {
				bestError = error;
				best = i;
			}
		}
		indices[ texel ] = best;
		total += bestError;
	}
	return total;
}

/*
================
BC6H_FitRegion

Endpoints along the principal axis of a region's texels, ordered so the
region's anchor texel sits nearer the first one (its index must stay in the
lower half).
================
*/
static void BC6H_FitRegion( const bc6hTexels_t &texels, int partition, int regions, int region, int anchor, float outA[3], float outB[3] ) {
	float mean[3] = { 0.0f, 0.0f, 0.0f };
	int count = 0;
	for ( int texel = 0; texel < 16; texel++ ) {
		if ( regions == 2 && BC6H_PartitionRegion( partition, texel ) != region ) {
			continue;
		}
		for ( int c = 0; c < 3; c++ ) {
			mean[c] += texels.target[ texel ][ c ];
		}
		count++;
	}
	if ( count == 0 ) {
		for ( int c = 0; c < 3; c++ ) {
			outA[c] = outB[c] = 0.0f;
		}
		return;
	}
	for ( int c = 0; c < 3; c++ ) {
		mean[c] /= (float)count;
	}

	float cov[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };	// xx xy xz yy yz zz
	for ( int texel = 0; texel < 16; texel++ ) {
		if ( regions == 2 && BC6H_PartitionRegion( partition, texel ) != region ) {
			continue;
		}
		const float d0 = texels.target[ texel ][0] - mean[0];
		const float d1 = texels.target[ texel ][1] - mean[1];
		const float d2 = texels.target[ texel ][2] - mean[2];
		cov[0] += d0 * d0; cov[1] += d0 * d1; cov[2] += d0 * d2;
		cov[3] += d1 * d1; cov[4] += d1 * d2; cov[5] += d2 * d2;
	}

	// power iteration; renormalize by the largest component, which needs no sqrt
	float axis[3] = { 1.0f, 1.0f, 1.0f };
	for ( int iteration = 0; iteration < 8; iteration++ ) {
		const float x = cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2];
		const float y = cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2];
		const float z = cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2];
		float largest = x < 0.0f ? -x : x;
		const float ay = y < 0.0f ? -y : y;
		const float az = z < 0.0f ? -z : z;
		largest = ay > largest ? ay : largest;
		largest = az > largest ? az : largest;
		if ( largest <= 0.0f ) {
			axis[0] = axis[1] = axis[2] = 0.0f;
			break;
		}
		axis[0] = x / largest;
		axis[1] = y / largest;
		axis[2] = z / largest;
	}

	const float axisLengthSq = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
	float tMin = 0.0f, tMax = 0.0f;
	if ( axisLengthSq > 0.0f ) {
		bool first = true;
		for ( int texel = 0; texel < 16; texel++ ) {
			if ( regions == 2 && BC6H_PartitionRegion( partition, texel ) != region ) {
				continue;
			}
			const float t = ( ( texels.target[ texel ][0] - mean[0] ) * axis[0] +
							  ( texels.target[ texel ][1] - mean[1] ) * axis[1] +
							  ( texels.target[ texel ][2] - mean[2] ) * axis[2] ) / axisLengthSq;
			if ( first || t < tMin ) { tMin = t; }
			if ( first || t > tMax ) { tMax = t; }
			first = false;
		}
	}
	for ( int c = 0; c < 3; c++ ) {
		outA[c] = BC6H_ClampDomain( mean[c] + axis[c] * tMin, texels.isSigned );
		outB[c] = BC6H_ClampDomain( mean[c] + axis[c] * tMax, texels.isSigned );
	}

	// keep the anchor texel nearer A
	float toA = 0.0f, toB = 0.0f;
	for ( int c = 0; c < 3; c++ ) {
		const float da = texels.target[ anchor ][c] - outA[c];
		const float db = texels.target[ anchor ][c] - outB[c];
		toA += da * da;
		toB += db * db;
	}
	if ( toB < toA ) {
		for ( int c = 0; c < 3; c++ ) {
			const float t = outA[c];
			outA[c] = outB[c];
			outB[c] = t;
		}
	}
}

/*
================
BC6H_RefineRegion

Least-squares endpoints for a region given its texels' indices.
Returns false when the indices give the system no unique solution.
================
*/
static bool BC6H_RefineRegion( const bc6hTexels_t &texels, const bc6hMode_t &mode, int partition, int region, const int indices[16], float outA[3], float outB[3] ) {
	const int *weights = mode.regions == 2 ? bc6hWeights3 : bc6hWeights4;
	float aa = 0.0f, ab = 0.0f, bb = 0.0f;
	float ap[3] = { 0.0f, 0.0f, 0.0f };
	float bp[3] = { 0.0f, 0.0f, 0.0f };
	for ( int texel = 0; texel < 16; texel++ ) {
		if ( mode.regions == 2 && BC6H_PartitionRegion( partition, texel ) != region ) {
			continue;
		}
		const float beta = (float)weights[ indices[ texel ] ] / 64.0f;
		const float alpha = 1.0f - beta;
		aa += alpha * alpha;
		ab += alpha * beta;
		bb += beta * beta;
		for ( int c = 0; c < 3; c++ ) {
			ap[c] += alpha * texels.target[ texel ][c];
			bp[c] += beta * texels.target[ texel ][c];
		}
	}
	const float det = aa * bb - ab * ab;
	if ( det < 1e-6f && det > -1e-6f ) {
		return false;
	}
	const float inv = 1.0f / det;
	for ( int c = 0; c < 3; c++ ) {
		outA[c] = BC6H_ClampDomain( ( ap[c] * bb - bp[c] * ab ) * inv, texels.isSigned );
		outB[c] = BC6H_ClampDomain( ( bp[c] * aa - ap[c] * ab ) * inv, texels.isSigned );
	}
	return true;
}

/*
================
BC6H_TryMode

Fits, quantizes and refines one mode and partition; keeps it in `best` when
it beats what is there.
================
*/
static void BC6H_TryMode( const bc6hTexels_t &texels, int modeIndex, int partition, int refinements, bc6hCandidate_t &best ) {
	const bc6hMode_t &mode = bc6hModes[ modeIndex ];
	float endpoints[4][3];
	for ( int region = 0; region < mode.regions; region++ ) {
		const int anchor = region == 0 ? 0 : bc6hAnchor2[ partition ];
		BC6H_FitRegion( texels, partition, mode.regions, region, anchor, endpoints[ region * 2 ], endpoints[ region * 2 + 1 ] );
	}

	bc6hCandidate_t candidate;
	candidate.modeIndex = modeIndex;
	candidate.partition = partition;
	for ( int pass = 0; pass <= refinements; pass++ ) {
		int fields[12];
		int indices[16];
		BC6H_PackFields( mode, texels.isSigned, endpoints, fields );
		const long long error = BC6H_ChooseIndices( mode, partition, fields, texels, indices );
		if ( pass == 0 || error < candidate.error ) {
			candidate.error = error;
			memcpy( candidate.fields, fields, sizeof( fields ) );
			memcpy( candidate.indices, indices, sizeof( indices ) );
		}
		if ( error == 0 || pass == refinements ) {
			break;
		}
		bool refined = false;
		for ( int region = 0; region < mode.regions; region++ ) {
			refined |= BC6H_RefineRegion( texels, mode, partition, region, indices, endpoints[ region * 2 ], endpoints[ region * 2 + 1 ] );
		}
		if ( !refined ) {
			break;
		}
	}

	if ( best.modeIndex < 0 || candidate.error < best.error ) {
		best = candidate;
	}
}

class idBC6HBitWriter {
public:
	explicit idBC6HBitWriter( byte *block ) : data( block ), position( 0 ) { memset( data, 0, 16 ); }

	void Write( int value, int count ) {
		for ( int i = 0; i < count; i++, position++ ) {
			data[ position >> 3 ] |= (byte)( ( ( value >> i ) & 1 ) << ( position & 7 ) );
		}
	}

private:
	byte *	data;
	int		position;
};

static void BC6H_WriteBlock( const bc6hCandidate_t &candidate, byte *outBlock ) {
	const bc6hMode_t &mode = bc6hModes[ candidate.modeIndex ];
	idBC6HBitWriter bits( outBlock );
	bits.Write( mode.modeValue, mode.modeBits );
	for ( int i = 0; i < mode.numSegments; i++ ) {
		const bc6hSegment_t &segment = mode.segments[i];
		bits.Write( candidate.fields[ segment.field ] >> segment.lsb, segment.count );
	}
	int indexBits = 4;
	int anchor1 = -1;
	if ( mode.regions == 2 ) {
		bits.Write( candidate.partition, 5 );
		indexBits = 3;
		anchor1 = bc6hAnchor2[ candidate.partition ];
	}
	for ( int texel = 0; texel < 16; texel++ ) {
		bits.Write( candidate.indices[ texel ], ( texel == 0 || texel == anchor1 ) ? indexBits - 1 : indexBits );
	}
}

/*
================
BC6H_EncodeBlock
================
*/
void BC6H_EncodeBlock( const unsigned short *rgbaHalf, int rowPitchHalves, bool isSigned, bc6hQuality_t quality, byte *outBlock ) {
	bc6hTexels_t texels;
	texels.isSigned = isSigned;
	const float scale = isSigned ? 32.0f / 31.0f : 64.0f / 31.0f;
	for ( int texel = 0; texel < 16; texel++ ) {
		const unsigned short *in = rgbaHalf + ( texel >> 2 ) * rowPitchHalves + ( texel & 3 ) * 4;
		for ( int c = 0; c < 3; c++ ) {
			texels.halfBits[ texel ][ c ] = BC6H_HalfToSignedBits( in[c], isSigned );
			texels.target[ texel ][ c ] = BC6H_ClampDomain( (float)texels.halfBits[ texel ][ c ] * scale, isSigned );
		}
	}

	const int refinements = quality == BC6H_QUALITY_HIGH ? 2 : 1;
	bc6hCandidate_t best;
	best.modeIndex = -1;
	best.error = 0;

	// one region: modes 11-14, from the most endpoint precision to the least
	for ( int modeIndex = 13; modeIndex >= 10; modeIndex-- ) {
		BC6H_TryMode( texels, modeIndex, 0, refinements, best );
	}

	if ( quality != BC6H_QUALITY_FAST && best.error > 0 ) {
		// rank the partitions by how well two unquantized lines fit them
		struct partitionScore_t { int partition; float error; };
		partitionScore_t scores[ BC6H_MAX_PARTITION_CANDIDATES ];
		for ( int partition = 0; partition < 32; partition++ ) {
			float error = 0.0f;
			for ( int region = 0; region < 2; region++ ) {
				float a[3], b[3];
				BC6H_FitRegion( texels, partition, 2, region, region == 0 ? 0 : bc6hAnchor2[ partition ], a, b );
				for ( int texel = 0; texel < 16; texel++ ) {
					if ( BC6H_PartitionRegion( partition, texel ) != region ) {
						continue;
					}
					float bestTexel = -1.0f;
					for ( int i = 0; i < 8; i++ ) {
						const float t = (float)bc6hWeights3[i] / 64.0f;
						float e = 0.0f;
						for ( int c = 0; c < 3; c++ ) {
							const float d = a[c] + ( b[c] - a[c] ) * t - texels.target[ texel ][ c ];
							e += d * d;
						}
						if ( bestTexel < 0.0f || e < bestTexel ) {
							bestTexel = e;
						}
					}
					error += bestTexel;
				}
			}
			scores[ partition ].partition = partition;
			scores[ partition ].error = error;
		}
		const int keep = quality == BC6H_QUALITY_HIGH ? 12 : 4;
		for ( int i = 0; i < keep; i++ ) {
			// selection: move the next best to position i
			int bestIndex = i;
			for ( int j = i + 1; j < 32; j++ ) {
				if ( scores[j].error < scores[ bestIndex ].error ) {
					bestIndex = j;
				}
			}
			const partitionScore_t swap = scores[i];
			scores[i] = scores[ bestIndex ];
			scores[ bestIndex ] = swap;

			for ( int modeIndex = 0; modeIndex < 10; modeIndex++ ) {
				BC6H_TryMode( texels, modeIndex, scores[i].partition, refinements, best );
			}
			if ( best.error == 0 ) {
				break;
			}
		}
	}

	BC6H_WriteBlock( best, outBlock );
}

/*
================
BC6H_EncodeImage
================
*/
void BC6H_EncodeImage( const unsigned short *rgbaHalf, int width, int height, bool isSigned, bc6hQuality_t quality, byte *outBlocks ) {
	const int blocksWide = ( width + 3 ) / 4;
	const int blocksHigh = ( height + 3 ) / 4;
	unsigned short texels[64];
	for ( int by = 0; by < blocksHigh; by++ ) {
		for ( int bx = 0; bx < blocksWide; bx++ ) {
			for ( int texel = 0; texel < 16; texel++ ) {
				int x = bx * 4 + ( texel & 3 );
				int y = by * 4 + ( texel >> 2 );
				x = x < width ? x : width - 1;
				y = y < height ? y : height - 1;
				memcpy( texels + texel * 4, rgbaHalf + ( (size_t)y * width + x ) * 4, 4 * sizeof( unsigned short ) );
			}
			BC6H_EncodeBlock( texels, 16, isSigned, quality, outBlocks + ( (size_t)by * blocksWide + bx ) * 16 );
		}
	}
}
