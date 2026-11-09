// Native checks for the bit-stream compressors behind the network channel
// (zero-run, three-bit words) and demo files (LZW), compiled from the sections
// of Compressor.cpp that generate_compressor_test.py copies out.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <vector>

#define ID_INLINE inline
#define ID_TIME_T time_t
using byte = unsigned char;

typedef enum {
	FS_SEEK_CUR,
	FS_SEEK_END,
	FS_SEEK_SET
} fsOrigin_t;

template<class T> static inline T Min( T a, T b ) { return a < b ? a : b; }
template<class T> static inline T Max( T a, T b ) { return a > b ? a : b; }

struct TestCommon {
	int warnings = 0;
	[[noreturn]] void FatalError( const char *, ... ) { throw std::runtime_error( "fatal error" ); }
	[[noreturn]] void Error( const char *, ... ) { throw std::runtime_error( "error" ); }
	void Printf( const char *, ... ) {}
	void Warning( const char *, ... ) { ++warnings; }
};
static TestCommon testCommon;
static TestCommon *common = &testCommon;

class idFile {
public:
	virtual					~idFile( void ) {}
	virtual const char *	GetName( void ) { return ""; }
	virtual const char *	GetFullPath( void ) { return ""; }
	virtual int				Read( void *, int ) { return 0; }
	virtual int				Write( const void *, int ) { return 0; }
	virtual int				Length( void ) { return 0; }
	virtual ID_TIME_T		Timestamp( void ) { return 0; }
	virtual int				Tell( void ) { return 0; }
	virtual void			ForceFlush( void ) {}
	virtual void			Flush( void ) {}
	virtual int				Seek( long, fsOrigin_t ) { return -1; }
};

// The chained index idCompressor_LZW looks its dictionary up through.
class idHashIndex {
public:
	idHashIndex( void ) : head( 1024, -1 ), chain( 4096, -1 ) {}
	void Clear( void ) {
		head.assign( head.size(), -1 );
		chain.assign( chain.size(), -1 );
	}
	void Add( int key, int index ) {
		if ( index >= static_cast<int>( chain.size() ) ) {
			chain.resize( index + 1, -1 );
		}
		chain[index] = head[key & 1023];
		head[key & 1023] = index;
	}
	int First( int key ) const { return head[key & 1023]; }
	int Next( int index ) const { return chain[index]; }
private:
	std::vector<int> head, chain;
};

#include "src/framework/Compressor.h"
#include "CompressorUnderTest.inc"

static int failures = 0;
static void Check( bool condition, const char *label ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAILED: %s\n", label );
		++failures;
	}
}

// Reads like idFile_BitMsg over an idBitMsg: what it cannot supply it zero-fills.
class MemoryFile : public idFile {
public:
	std::vector<byte>	data;
	size_t				readPos = 0;
	long long			bytesRequested = 0;
	int					readCalls = 0;

	int Read( void *buffer, int len ) override {
		++readCalls;
		bytesRequested += len;
		const size_t remaining = data.size() - readPos;
		const size_t take = Min( remaining, static_cast<size_t>( len ) );
		if ( take > 0 ) {
			memcpy( buffer, data.data() + readPos, take );
		}
		memset( static_cast<byte *>( buffer ) + take, 0, static_cast<size_t>( len ) - take );
		readPos += take;
		return static_cast<int>( take );
	}
	int Write( const void *buffer, int len ) override {
		const byte *bytes = static_cast<const byte *>( buffer );
		data.insert( data.end(), bytes, bytes + len );
		return len;
	}
};

static std::uint32_t randomState = 0x12345678u;
static byte NextRandomByte( void ) {
	randomState = randomState * 1664525u + 1013904223u;
	return static_cast<byte>( randomState >> 24 );
}

template<class Compressor>
static std::vector<byte> Pack( const std::vector<byte> &input, int wordLength ) {
	MemoryFile file;
	Compressor compressor;
	compressor.Init( &file, true, wordLength );
	compressor.Write( input.data(), static_cast<int>( input.size() ) );
	compressor.FinishCompress();
	return file.data;
}

template<class Compressor>
static std::vector<byte> Unpack( const std::vector<byte> &packed, int size, int wordLength, int *produced = nullptr,
		MemoryFile *fileOut = nullptr ) {
	MemoryFile file;
	file.data = packed;
	Compressor compressor;
	compressor.Init( &file, false, wordLength );
	std::vector<byte> output( size + 16, 0xcc );
	const int count = compressor.Read( output.data(), size );
	if ( produced != nullptr ) {
		*produced = count;
	}
	if ( fileOut != nullptr ) {
		*fileOut = file;
	}
	Check( output[size] == 0xcc && output[size + 15] == 0xcc, "decompression stays inside its output" );
	output.resize( size );
	return output;
}

// The channel's message coder: MsgChannel.cpp uses three-bit words.
static void TestZeroRun( void ) {
	const int sizes[] = { 1, 6, 7, 300, 1300, 16384 };
	for ( int size : sizes ) {
		for ( int pattern = 0; pattern < 4; pattern++ ) {
			std::vector<byte> input( size );
			for ( int i = 0; i < size; i++ ) {
				switch ( pattern ) {
					case 0: input[i] = 0; break;
					case 1: input[i] = NextRandomByte(); break;
					case 2: input[i] = 0x88; break;
					default: input[i] = ( i % 7 ) == 0 ? NextRandomByte() : 0; break;
				}
			}
			const std::vector<byte> packed = Pack<idCompressor_RunLength_ZeroBased>( input, 3 );
			int produced = 0;
			const std::vector<byte> output = Unpack<idCompressor_RunLength_ZeroBased>( packed, size, 3, &produced );
			Check( produced >= size, "zero-run round trip reports the whole message" );
			Check( output == input, "zero-run round trip reproduces the message" );
			// A lone zero word costs six bits for three, so the coder can grow
			// its input but never past double: idMsgChannel packs into a buffer
			// sized on that bound and refuses what would not fit a packet.
			Check( packed.size() <= input.size() * 2 + 64, "zero-run output stays within twice its input" );
			if ( pattern == 2 && size == 16384 ) {
				Check( packed.size() > input.size(), "0x88 bytes expand under the zero-run coder" );
			}
		}
	}
}

// A packet that claims a full message and carries nothing: the decoder must
// not go back to the source for a whole buffer for every byte it consumes.
static void TestZeroRunDrySource( void ) {
	const int size = 16384;
	int produced = 0;
	MemoryFile file;
	const std::vector<byte> output = Unpack<idCompressor_RunLength_ZeroBased>( std::vector<byte>(), size, 3, &produced, &file );
	Check( produced >= size, "an empty source still fills the message" );
	bool allZero = true;
	for ( byte value : output ) {
		allZero = allZero && value == 0;
	}
	Check( allZero, "an empty source decodes as zeros" );
	// two whole-buffer requests, then one byte per byte consumed
	Check( file.bytesRequested < 4 * 65536, "a dry source is asked for a byte at a time" );

	// the same, running dry part way through real data
	std::vector<byte> input( size );
	for ( byte &value : input ) {
		value = NextRandomByte() | 1;
	}
	std::vector<byte> packed = Pack<idCompressor_RunLength_ZeroBased>( input, 3 );
	packed.resize( packed.size() / 4 );
	MemoryFile partial;
	const std::vector<byte> truncated = Unpack<idCompressor_RunLength_ZeroBased>( packed, size, 3, &produced, &partial );
	Check( partial.bytesRequested < 4 * 65536, "a source that runs dry part way is asked for a byte at a time" );
	Check( memcmp( truncated.data(), input.data(), packed.size() / 2 ) == 0, "the data before the source ran dry is intact" );
}

// Demo files: com_compressDemos 1.
static void TestLZW( void ) {
	const int sizes[] = { 1, 2, 255, 5000, 40000, 200000 };
	for ( int size : sizes ) {
		for ( int pattern = 0; pattern < 3; pattern++ ) {
			std::vector<byte> input( size );
			for ( int i = 0; i < size; i++ ) {
				switch ( pattern ) {
					case 0: input[i] = NextRandomByte(); break;
					case 1: input[i] = static_cast<byte>( 'a' + ( i % 3 ) ); break;
					default: input[i] = ( i % 11 ) < 6 ? static_cast<byte>( i / 97 ) : NextRandomByte() & 3; break;
				}
			}
			testCommon.warnings = 0;
			const std::vector<byte> packed = Pack<idCompressor_LZW>( input, 8 );
			int produced = 0;
			const std::vector<byte> output = Unpack<idCompressor_LZW>( packed, size, 8, &produced );
			Check( produced == size, "LZW round trip reports the whole stream" );
			Check( output == input, "LZW round trip reproduces the stream" );
			Check( testCommon.warnings == 0, "a valid LZW stream raises no warning" );

			// A demo's trailer is written raw behind its stream, and blocks are
			// decoded ahead of need: the bytes that are not codes must neither
			// be followed nor reported while the reader has all it asked for.
			std::vector<byte> withTrailer = packed;
			for ( int i = 0; i < 96; i++ ) {
				withTrailer.push_back( static_cast<byte>( 0xff - ( i % 5 ) ) );
			}
			const std::vector<byte> ahead = Unpack<idCompressor_LZW>( withTrailer, size, 8, &produced );
			Check( produced == size && ahead == input, "a raw trailer behind the stream leaves the stream intact" );
			Check( testCommon.warnings == 0, "a raw trailer behind the stream is not reported" );
		}
	}
}

static std::vector<byte> PackCodes( const std::vector<int> &codes, int bits ) {
	std::vector<byte> packed;
	int bitCount = 0;
	for ( int code : codes ) {
		for ( int i = 0; i < bits; i++, bitCount++ ) {
			if ( ( bitCount & 7 ) == 0 ) {
				packed.push_back( 0 );
			}
			if ( code & ( 1 << i ) ) {
				packed.back() |= static_cast<byte>( 1 << ( bitCount & 7 ) );
			}
		}
	}
	// the decoder stops at the end of its input, so give it bytes to spare
	packed.insert( packed.end(), 64, 0xff );
	return packed;
}

// Codes the dictionary cannot hold used to be followed through entries nobody
// had written: out of the table, or round a loop off the end of a stack buffer.
static void TestLZWCorrupt( void ) {
	const std::vector<std::vector<int>> streams = {
		{ 65, 257, 65, 257 },		// past the next free entry, then the self-link it left
		{ 300 },					// a first code that is not a literal
		{ 65, 66, 511 },
		{ 65, 256, 256, 400 },
	};
	for ( const std::vector<int> &codes : streams ) {
		testCommon.warnings = 0;
		int produced = 0;
		const std::vector<byte> output = Unpack<idCompressor_LZW>( PackCodes( codes, 9 ), 4096, 8, &produced );
		Check( produced < 4096, "a corrupt LZW stream ends early" );
		Check( testCommon.warnings == 1, "a corrupt LZW stream is reported once to a reader left short" );
	}

	// the legal use of the next free entry still decodes: "AAA" is 65, 256
	testCommon.warnings = 0;
	int produced = 0;
	std::vector<byte> triple = PackCodes( { 65, 256 }, 9 );
	triple.resize( 3 );		// exactly the two codes
	const std::vector<byte> output = Unpack<idCompressor_LZW>( triple, 3, 8, &produced );
	Check( produced == 3 && output[0] == 65 && output[1] == 65 && output[2] == 65, "the next free entry decodes as its own prefix" );
	Check( testCommon.warnings == 0, "the next free entry is not reported as corrupt" );
}

int main( void ) {
	try {
		TestZeroRun();
		TestZeroRunDrySource();
		TestLZW();
		TestLZWCorrupt();
	} catch ( const std::exception &error ) {
		std::fprintf( stderr, "FAILED: unexpected %s\n", error.what() );
		return 1;
	}
	if ( failures != 0 ) {
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "compressor: PASS\n" );
	return 0;
}
