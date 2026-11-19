// Seeded BC6H test blocks shared by Bc6hCodecTest.cpp and Bc6hGpuReference.cpp,
// so the reference table and the test see the same bits.
#ifndef BC6H_TEST_BLOCKS_H
#define BC6H_TEST_BLOCKS_H

#include <cstdint>

static const int BC6H_TEST_BLOCKS_PER_MODE = 4096;
static const int BC6H_TEST_RESERVED_BLOCKS = 64;
// groups 0-13 are modes 1-14; group 14 cycles through the four reserved modes
static const int BC6H_TEST_GROUPS = 15;
static const uint32_t BC6H_TEST_SEED = 0x13579BDFu;

static const unsigned char bc6hTestModeValues[14] = {
	0x00, 0x01, 0x02, 0x06, 0x0a, 0x0e, 0x12, 0x16, 0x1a, 0x1e, 0x03, 0x07, 0x0b, 0x0f
};
static const unsigned char bc6hTestReservedValues[4] = { 0x13, 0x17, 0x1b, 0x1f };

struct bc6hTestRandom_t {
	uint32_t state;
	explicit bc6hTestRandom_t( uint32_t seed ) : state( seed ) {}
	uint32_t Next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}
};

static inline int BC6H_TestGroupBlocks( int group ) {
	return group < 14 ? BC6H_TEST_BLOCKS_PER_MODE : BC6H_TEST_RESERVED_BLOCKS;
}

// random bits with the group's mode in the low bits of the first byte
static inline void BC6H_TestMakeBlock( bc6hTestRandom_t &rng, int group, int index, unsigned char block[16] ) {
	for ( int word = 0; word < 4; word++ ) {
		const uint32_t value = rng.Next();
		block[ word * 4 + 0 ] = (unsigned char)( value & 0xff );
		block[ word * 4 + 1 ] = (unsigned char)( ( value >> 8 ) & 0xff );
		block[ word * 4 + 2 ] = (unsigned char)( ( value >> 16 ) & 0xff );
		block[ word * 4 + 3 ] = (unsigned char)( ( value >> 24 ) & 0xff );
	}
	if ( group < 2 ) {
		block[0] = (unsigned char)( ( block[0] & ~0x03 ) | bc6hTestModeValues[ group ] );
	} else if ( group < 14 ) {
		block[0] = (unsigned char)( ( block[0] & ~0x1f ) | bc6hTestModeValues[ group ] );
	} else {
		block[0] = (unsigned char)( ( block[0] & ~0x1f ) | bc6hTestReservedValues[ index & 3 ] );
	}
}

#endif
