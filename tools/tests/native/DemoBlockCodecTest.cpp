// Render demo block container (src/framework/DemoBlockCodec.cpp), without the
// engine: the codec round-trips and never trusts its input, a stream that was
// cut at any byte or damaged anywhere plays exactly its intact blocks, the
// block headers alone give the timeline a seek needs, and a keyframe is passed
// by when read in order and handed out when a seek is sent to it.
//
// usage: openq4-demo-block-codec-test            (the checks)
//        openq4-demo-block-codec-test --bench <file> [blockBytes] [level]
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
typedef unsigned char byte;
#define __PRECOMPILED_H__
#include "../../../src/idlib/hashing/CRC32.cpp"
#include "../../../src/imagetools/PNG/Inflate.cpp"
#include "../../../src/imagetools/PNG/Deflate.cpp"
#include "../../../src/framework/DemoBlockCodec.cpp"

static int failures = 0;

#define CHECK( condition, ... ) \
	do { \
		if ( !( condition ) ) { \
			std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); \
			std::printf( __VA_ARGS__ ); \
			std::printf( "\n" ); \
			failures++; \
		} \
	} while ( 0 )

static uint32_t rngState = 0x13572468u;
static uint32_t NextRandom() {
	rngState ^= rngState << 13;
	rngState ^= rngState >> 17;
	rngState ^= rngState << 5;
	return rngState;
}

static void PutFloat( std::vector<uint8_t> &out, float value ) {
	uint8_t bytes[4];
	std::memcpy( bytes, &value, 4 );
	out.insert( out.end(), bytes, bytes + 4 );
}

static void PutInt( std::vector<uint8_t> &out, uint32_t value ) {
	for ( int shift = 0; shift < 32; shift += 8 ) {
		out.push_back( static_cast<uint8_t>( value >> shift ) );
	}
}

// One "frame" as a render demo writes it: a view, then records keyed by a
// handle whose fields mostly repeat the frame before and partly drift.
static void AppendFrame( std::vector<uint8_t> &out, int frame, int entities ) {
	PutInt( out, 1 );
	PutInt( out, 1 );
	for ( int i = 0; i < 16; i++ ) {
		PutFloat( out, 100.0f + i + frame * 0.37f );
	}
	for ( int entity = 0; entity < entities; entity++ ) {
		PutInt( out, 1 );
		PutInt( out, 2 );
		PutInt( out, static_cast<uint32_t>( entity ) );
		for ( int field = 0; field < 40; field++ ) {
			const bool moves = ( entity % 3 == 0 ) && field < 12;
			PutFloat( out, entity * 17.0f + field + ( moves ? frame * 0.013f * ( field + 1 ) : 0.0f ) );
		}
		const char *name = "models/monsters/strogg_marine/strogg_marine.md5mesh";
		out.insert( out.end(), name, name + std::strlen( name ) );
		if ( entity % 5 == 0 ) {
			for ( int i = 0; i < 24; i++ ) {
				out.push_back( static_cast<uint8_t>( NextRandom() ) );
			}
		}
	}
	PutInt( out, 4 );
	PutInt( out, 5 );
	PutInt( out, 16667 );
}

static std::vector<uint8_t> DemoLikeStream( int frames, int entities ) {
	std::vector<uint8_t> stream;
	for ( int frame = 0; frame < frames; frame++ ) {
		AppendFrame( stream, frame, entities );
	}
	return stream;
}

static std::vector<uint8_t> RandomBytes( size_t count ) {
	std::vector<uint8_t> data( count );
	for ( size_t i = 0; i < count; i++ ) {
		data[i] = static_cast<uint8_t>( NextRandom() >> 11 );
	}
	return data;
}

// ---------------------------------------------------------------------------

static void TestHeader() {
	idDemoBlock::header_t header;
	std::memset( &header, 0, sizeof( header ) );
	header.flags = idDemoBlock::FLAG_RESET | idDemoBlock::FLAG_SEGMENT_START | idDemoBlock::FLAG_FRAME_END;
	header.codec = idDemoBlock::CODEC_LONG_RANGE;
	header.rawBytes = 131072;
	header.packedBytes = 30001;
	header.segment = 3;
	header.startFrame = 123456;
	header.frameCount = 7;
	header.startTimeUS = UINT64_C( 0x0000001234567890 );
	header.durationUS = 116669;
	header.payloadCRC = 0xDEADBEEFu;
	uint8_t bytes[idDemoBlock::HEADER_BYTES];
	idDemoBlock::WriteHeader( header, bytes );
	CHECK( bytes[0] == 'O' && bytes[1] == 'Q' && bytes[2] == '4' && bytes[3] == 'B', "header magic" );

	idDemoBlock::header_t read;
	CHECK( idDemoBlock::ReadHeader( bytes, read ), "a written header reads back" );
	CHECK( read.flags == header.flags && read.codec == header.codec && read.rawBytes == header.rawBytes &&
		   read.packedBytes == header.packedBytes && read.segment == header.segment &&
		   read.startFrame == header.startFrame && read.frameCount == header.frameCount &&
		   read.startTimeUS == header.startTimeUS && read.durationUS == header.durationUS &&
		   read.payloadCRC == header.payloadCRC, "header fields round-trip" );

	// no single flipped bit survives the header's own checksum
	int accepted = 0;
	for ( int bit = 0; bit < idDemoBlock::HEADER_BYTES * 8; bit++ ) {
		uint8_t damaged[idDemoBlock::HEADER_BYTES];
		std::memcpy( damaged, bytes, sizeof( damaged ) );
		damaged[bit >> 3] ^= static_cast<uint8_t>( 1 << ( bit & 7 ) );
		accepted += idDemoBlock::ReadHeader( damaged, read ) ? 1 : 0;
	}
	CHECK( accepted == 0, "%d single-bit header flips were accepted", accepted );

	// well-formed checksums over values the reader must still refuse
	struct { const char *what; uint8_t flags; uint8_t codec; uint32_t raw; uint32_t packed; } bad[] = {
		{ "an unknown flag", 0x80, idDemoBlock::CODEC_STORED, 10, 10 },
		{ "a segment start without a reset", idDemoBlock::FLAG_SEGMENT_START, idDemoBlock::CODEC_STORED, 10, 10 },
		{ "a segment start that is a keyframe", idDemoBlock::FLAG_SEGMENT_START | idDemoBlock::FLAG_RESET | idDemoBlock::FLAG_KEYFRAME,
		  idDemoBlock::CODEC_STORED, 10, 10 },
		{ "an unknown codec", 0, 7, 10, 5 },
		{ "a block beyond the size limit", 0, idDemoBlock::CODEC_STORED, idDemoBlock::MAX_RAW_BYTES + 1, idDemoBlock::MAX_RAW_BYTES + 1 },
		{ "a stored block of another size", 0, idDemoBlock::CODEC_STORED, 10, 9 },
		{ "a packed block that did not shrink", 0, idDemoBlock::CODEC_LONG_RANGE, 10, 10 },
	};
	for ( const auto &entry : bad ) {
		idDemoBlock::header_t h;
		std::memset( &h, 0, sizeof( h ) );
		h.flags = entry.flags;
		h.codec = entry.codec;
		h.rawBytes = entry.raw;
		h.packedBytes = entry.packed;
		idDemoBlock::WriteHeader( h, bytes );
		CHECK( !idDemoBlock::ReadHeader( bytes, read ), "the reader accepted %s", entry.what );
	}
	std::printf( "header: round trip, %d bit flips and %d invalid headers refused\n",
		idDemoBlock::HEADER_BYTES * 8, static_cast<int>( sizeof( bad ) / sizeof( bad[0] ) ) );
}

// Packs data as blocks of blockBytes and checks every block decodes to it.
static size_t RoundTrip( const char *label, const std::vector<uint8_t> &data, size_t blockBytes, int level ) {
	idDemoBlock::encoder_t encoder;
	idDemoBlock::decoder_t decoder;
	std::vector<uint8_t> packed;
	std::vector<uint8_t> out( blockBytes + 64, 0xCD );
	size_t packedTotal = 0;
	for ( size_t offset = 0; offset < data.size(); offset += blockBytes ) {
		const size_t count = data.size() - offset < blockBytes ? data.size() - offset : blockBytes;
		const uint8_t codec = encoder.Pack( data.data() + offset, count, level, packed );
		packedTotal += packed.size();
		CHECK( codec == idDemoBlock::CODEC_STORED ? packed.size() == count : packed.size() < count,
			   "%s: block at %zu is %zu bytes for %zu", label, offset, packed.size(), count );
		std::memset( out.data(), 0xCD, out.size() );
		const bool ok = decoder.Unpack( codec, packed.data(), packed.size(), out.data(), count );
		CHECK( ok && std::memcmp( out.data(), data.data() + offset, count ) == 0, "%s: block at %zu does not round-trip", label, offset );
		bool overrun = false;
		for ( size_t i = count; i < out.size(); i++ ) {
			overrun = overrun || out[i] != 0xCD;
		}
		CHECK( !overrun, "%s: block at %zu wrote past its size", label, offset );
		if ( !ok ) {
			break;
		}
	}
	return packedTotal;
}

static void TestRoundTrips() {
	const std::vector<uint8_t> demo = DemoLikeStream( 400, 60 );
	const size_t packed = RoundTrip( "demo-like", demo, 131072, 6 );
	CHECK( packed * 4 < demo.size(), "demo-like data packed only %zu of %zu bytes", packed, demo.size() );
	std::printf( "demo-like stream: %zu bytes in %zu (%.1f:1)\n", demo.size(), packed, static_cast<double>( demo.size() ) / packed );

	// the same stream at every level, and in the awkward block sizes
	for ( int level = 0; level <= 9; level += 3 ) {
		RoundTrip( "levels", demo, 65536, level );
	}
	RoundTrip( "tiny blocks", demo, 37, 6 );
	RoundTrip( "one byte blocks", std::vector<uint8_t>( demo.begin(), demo.begin() + 300 ), 1, 6 );
	RoundTrip( "largest blocks", demo, idDemoBlock::MAX_RAW_BYTES, 6 );

	// data with nothing to find is stored, not grown
	const std::vector<uint8_t> noise = RandomBytes( 300000 );
	const size_t noisePacked = RoundTrip( "noise", noise, 65536, 6 );
	CHECK( noisePacked == noise.size(), "noise grew or shrank: %zu of %zu", noisePacked, noise.size() );

	// runs that overlap themselves, and nothing but zeros
	std::vector<uint8_t> runs( 200000, 0 );
	for ( size_t i = 100000; i < runs.size(); i++ ) {
		runs[i] = static_cast<uint8_t>( "abcdefg"[i % 7] );
	}
	RoundTrip( "runs", runs, 50000, 6 );

	// history beyond the window slides away on both sides in step
	std::vector<uint8_t> longStream;
	const std::vector<uint8_t> chunk = DemoLikeStream( 30, 40 );
	while ( longStream.size() < 3 * static_cast<size_t>( idDemoBlock::WINDOW_BYTES ) + idDemoBlock::MAX_RAW_BYTES ) {
		longStream.insert( longStream.end(), chunk.begin(), chunk.end() );
		const std::vector<uint8_t> gap = RandomBytes( 50000 + NextRandom() % 200000 );
		longStream.insert( longStream.end(), gap.begin(), gap.end() );
	}
	RoundTrip( "past the window", longStream, 262144, 6 );
	RoundTrip( "past the window, small blocks", longStream, 9973, 1 );
	std::printf( "round trips: levels, block sizes, noise, runs and %zu bytes past the window\n", longStream.size() );
}

static void TestReset() {
	const std::vector<uint8_t> demo = DemoLikeStream( 60, 40 );
	const size_t half = demo.size() / 2;
	idDemoBlock::encoder_t encoder;
	std::vector<uint8_t> first, second, alone;
	const uint8_t firstCodec = encoder.Pack( demo.data(), half, 6, first );
	const uint8_t secondCodec = encoder.Pack( demo.data() + half, demo.size() - half, 6, second );
	encoder.Reset();
	const uint8_t aloneCodec = encoder.Pack( demo.data() + half, demo.size() - half, 6, alone );
	CHECK( second.size() < alone.size(), "the second block gained nothing from its history (%zu vs %zu)", second.size(), alone.size() );

	std::vector<uint8_t> out( demo.size() );
	idDemoBlock::decoder_t fresh;
	CHECK( fresh.Unpack( aloneCodec, alone.data(), alone.size(), out.data(), demo.size() - half ) &&
		   std::memcmp( out.data(), demo.data() + half, demo.size() - half ) == 0, "a block packed after a reset does not stand alone" );

	// a block that leans on history must not decode without it
	idDemoBlock::decoder_t noHistory;
	const bool decoded = secondCodec == idDemoBlock::CODEC_LONG_RANGE &&
		noHistory.Unpack( secondCodec, second.data(), second.size(), out.data(), demo.size() - half ) &&
		std::memcmp( out.data(), demo.data() + half, demo.size() - half ) == 0;
	CHECK( !decoded, "a block decoded correctly without the history it refers to" );

	// a failed block leaves the decoder where it was
	idDemoBlock::decoder_t decoder;
	CHECK( decoder.Unpack( firstCodec, first.data(), first.size(), out.data(), half ), "first block" );
	std::vector<uint8_t> damaged = second;
	damaged[damaged.size() / 2] ^= 0x10;
	CHECK( !decoder.Unpack( secondCodec, damaged.data(), damaged.size(), out.data(), demo.size() - half ), "a damaged block decoded" );
	CHECK( decoder.Unpack( secondCodec, second.data(), second.size(), out.data(), demo.size() - half ) &&
		   std::memcmp( out.data(), demo.data() + half, demo.size() - half ) == 0, "the intact block no longer decodes after a failed one" );
	std::printf( "reset: blocks stand alone after it, lean on history before it\n" );
}

static void PutVarintTo( std::vector<uint8_t> &out, uint32_t value ) {
	while ( value >= 0x80u ) {
		out.push_back( static_cast<uint8_t>( ( value & 0x7Fu ) | 0x80u ) );
		value >>= 7;
	}
	out.push_back( static_cast<uint8_t>( value ) );
}

// A long-range payload with a valid zlib wrapper around a given layer.
static std::vector<uint8_t> WrapLayer( const std::vector<uint8_t> &sequences, const std::vector<uint8_t> &literals ) {
	std::vector<uint8_t> layer;
	PutInt( layer, static_cast<uint32_t>( sequences.size() ) );
	layer.insert( layer.end(), sequences.begin(), sequences.end() );
	layer.insert( layer.end(), literals.begin(), literals.end() );
	std::vector<uint8_t> payload( 4 + Zlib_CompressBound( layer.size() ) );
	const uint32_t layerBytes = static_cast<uint32_t>( layer.size() );
	payload[0] = static_cast<uint8_t>( layerBytes );
	payload[1] = static_cast<uint8_t>( layerBytes >> 8 );
	payload[2] = static_cast<uint8_t>( layerBytes >> 16 );
	payload[3] = static_cast<uint8_t>( layerBytes >> 24 );
	payload.resize( 4 + Zlib_Compress( layer.data(), layer.size(), payload.data() + 4, 6 ) );
	return payload;
}

static void TestHostileLayers() {
	const std::vector<uint8_t> history( 1000, 0x55 );
	const std::vector<uint8_t> literals( 64, 0x41 );
	struct layerCase_t {
		const char *what;
		std::vector<uint32_t> sequence;		// varints
		size_t literalBytes;
		size_t rawBytes;
		bool valid;
	};
	const layerCase_t cases[] = {
		{ "literals then a match", { 16 << 1, 32, 100 }, 16, 48, true },
		{ "a match that repeats itself", { 4 << 1, 60, 2 }, 4, 64, true },
		{ "the last distance again", { 8 << 1, 8, 500, 8 << 1, 8, 0 }, 16, 32, true },
		{ "delta literals", { 0, 8, 300, ( 8 << 1 ) | 1, 0 }, 8, 16, true },
		{ "a distance before the history", { 4 << 1, 16, 2000 }, 4, 20, false },
		{ "a distance of nothing, with no last distance", { 4 << 1, 16, 0 }, 4, 20, false },
		{ "delta literals with no last distance", { ( 8 << 1 ) | 1, 0 }, 8, 8, false },
		{ "a match past the block", { 4 << 1, 100, 10 }, 4, 20, false },
		{ "literals past the block", { 40 << 1, 0 }, 40, 20, false },
		{ "more literals than the layer holds", { 64 << 1, 0 }, 16, 64, false },
		{ "literals left over", { 4 << 1, 0 }, 16, 4, false },
		{ "a block left short", { 4 << 1, 0 }, 4, 20, false },
		{ "a distance beyond the window", { 4 << 1, 16, idDemoBlock::WINDOW_BYTES + 1 }, 4, 20, false },
		{ "a sequence cut after its match length", { 4 << 1, 16 }, 4, 20, false },
	};
	int refused = 0;
	for ( const layerCase_t &entry : cases ) {
		std::vector<uint8_t> sequences;
		for ( uint32_t value : entry.sequence ) {
			PutVarintTo( sequences, value );
		}
		const std::vector<uint8_t> payload = WrapLayer( sequences, std::vector<uint8_t>( literals.begin(), literals.begin() + entry.literalBytes ) );
		idDemoBlock::decoder_t decoder;
		std::vector<uint8_t> out( 4096, 0xCD );
		CHECK( decoder.Unpack( idDemoBlock::CODEC_STORED, history.data(), history.size(), out.data(), history.size() ), "history" );
		std::memset( out.data(), 0xCD, out.size() );
		const bool ok = decoder.Unpack( idDemoBlock::CODEC_LONG_RANGE, payload.data(), payload.size(), out.data(), entry.rawBytes );
		CHECK( ok == entry.valid, "%s: %s", entry.what, ok ? "accepted" : "refused" );
		bool overrun = false;
		for ( size_t i = entry.rawBytes; i < out.size(); i++ ) {
			overrun = overrun || out[i] != 0xCD;
		}
		CHECK( !overrun, "%s: wrote past the block", entry.what );
		refused += entry.valid ? 0 : 1;
	}

	// malformed varints and sizes the header of the layer lies about
	{
		idDemoBlock::decoder_t decoder;
		std::vector<uint8_t> out( 256 );
		const std::vector<uint8_t> overlong = WrapLayer( { 0x80, 0x80, 0x80, 0x80, 0x80, 0x01 }, {} );
		CHECK( !decoder.Unpack( idDemoBlock::CODEC_LONG_RANGE, overlong.data(), overlong.size(), out.data(), 16 ), "an overlong varint was accepted" );
		const std::vector<uint8_t> wide = WrapLayer( { 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00 }, {} );
		CHECK( !decoder.Unpack( idDemoBlock::CODEC_LONG_RANGE, wide.data(), wide.size(), out.data(), 16 ), "a varint wider than 32 bits was accepted" );
		std::vector<uint8_t> lying = WrapLayer( { 4 << 1, 0 }, { 1, 2, 3, 4 } );
		lying[0] ^= 0x01;
		CHECK( !decoder.Unpack( idDemoBlock::CODEC_LONG_RANGE, lying.data(), lying.size(), out.data(), 4 ), "a wrong layer size was accepted" );
		CHECK( !decoder.Unpack( idDemoBlock::CODEC_LONG_RANGE, lying.data(), 3, out.data(), 4 ), "a three byte payload was accepted" );
		CHECK( !decoder.Unpack( 9, lying.data(), lying.size(), out.data(), 4 ), "an unknown codec was accepted" );
		CHECK( !decoder.Unpack( idDemoBlock::CODEC_STORED, lying.data(), 5, out.data(), 4 ), "a stored block of the wrong size was accepted" );
		CHECK( !decoder.Unpack( idDemoBlock::CODEC_STORED, lying.data(), 4, out.data(), idDemoBlock::MAX_RAW_BYTES + 1 ), "a block over the limit was accepted" );
	}
	std::printf( "hostile payloads: %d invalid layers refused, none wrote past its block\n", refused );
}

static void TestDamagedPayloads() {
	const std::vector<uint8_t> demo = DemoLikeStream( 40, 40 );
	idDemoBlock::encoder_t encoder;
	std::vector<uint8_t> packed;
	const uint8_t codec = encoder.Pack( demo.data(), demo.size(), 6, packed );
	CHECK( codec == idDemoBlock::CODEC_LONG_RANGE, "the sample block was not packed" );
	std::vector<uint8_t> out( demo.size() + 64 );

	// cut at every length: never the block
	int cutAccepted = 0;
	for ( size_t cut = 0; cut < packed.size(); cut += 1 + packed.size() / 331 ) {
		idDemoBlock::decoder_t decoder;
		cutAccepted += decoder.Unpack( codec, packed.data(), cut, out.data(), demo.size() ) ? 1 : 0;
	}
	CHECK( cutAccepted == 0, "%d cut payloads decoded", cutAccepted );

	// flipped bits: refused, or (the checksum colliding) still the same bytes
	int wrong = 0;
	for ( int trial = 0; trial < 600; trial++ ) {
		std::vector<uint8_t> damaged = packed;
		const int flips = 1 + trial % 3;
		for ( int i = 0; i < flips; i++ ) {
			damaged[NextRandom() % damaged.size()] ^= static_cast<uint8_t>( 1 << ( NextRandom() % 8 ) );
		}
		idDemoBlock::decoder_t decoder;
		std::memset( out.data(), 0xCD, out.size() );
		if ( decoder.Unpack( codec, damaged.data(), damaged.size(), out.data(), demo.size() ) &&
			 std::memcmp( out.data(), demo.data(), demo.size() ) != 0 ) {
			wrong++;
		}
		for ( size_t i = demo.size(); i < out.size(); i++ ) {
			if ( out[i] != 0xCD ) {
				CHECK( false, "a damaged payload wrote past the block" );
				break;
			}
		}
	}
	CHECK( wrong == 0, "%d damaged payloads decoded to other bytes", wrong );
	std::printf( "damaged payloads: cuts and 600 bit-flip trials never produced wrong bytes\n" );
}

// ---------------------------------------------------------------------------
// The stream: a recording written through builder_t, read back by reader_t.

struct memorySource_t : public idDemoBlock::source_t {
	const std::vector<uint8_t> *file;
	size_t position;
	int seeks;
	memorySource_t() : file( NULL ), position( 0 ), seeks( 0 ) {}
	virtual bool Seek( uint64_t offset ) {
		seeks++;
		if ( offset > file->size() ) {
			return false;
		}
		position = static_cast<size_t>( offset );
		return true;
	}
	virtual size_t Read( void *buffer, size_t bytes ) {
		const size_t left = file->size() - position;
		const size_t take = bytes < left ? bytes : left;
		std::memcpy( buffer, file->data() + position, take );
		position += take;
		return take;
	}
};

struct recording_t {
	std::vector<uint8_t>	file;			// a prefix, then the blocks
	std::vector<uint8_t>	stream;			// what the blocks carry to a reader in order
	std::vector<size_t>		blockEnds;		// file offset after each block
	std::vector<uint8_t>	blockFlags;
	std::vector<size_t>		streamEnds;		// stream bytes through each block
	std::vector<uint32_t>	framesThrough;	// complete frames through each block
	std::vector<size_t>		segmentStream;	// stream offset each segment begins at
	std::vector<uint32_t>	segmentFrame;
	std::vector<uint64_t>	segmentTimeUS;
	// keyframes: what each holds, and where in the stream a reader sent to it carries on
	std::vector<std::vector<uint8_t>>	keyframeBytes;
	std::vector<size_t>		keyframeStream;
	std::vector<uint32_t>	keyframeFrame;
	std::vector<uint64_t>	keyframeTimeUS;
	std::vector<uint32_t>	keyframeSegment;
	uint32_t				frames;
	uint64_t				timeUS;
};

static const size_t PREFIX_BYTES = 17;

static void Emit( recording_t &recording, idDemoBlock::encoder_t &encoder, std::vector<idDemoBlock::job_t *> &closed, int level ) {
	for ( idDemoBlock::job_t *job : closed ) {
		const size_t rawBytes = job->raw.size();
		idDemoBlock::PackJob( encoder, level, *job );
		uint8_t head[idDemoBlock::HEADER_BYTES];
		idDemoBlock::WriteHeader( job->header, head );
		recording.file.insert( recording.file.end(), head, head + sizeof( head ) );
		recording.file.insert( recording.file.end(), job->packed.begin(), job->packed.end() );
		recording.blockEnds.push_back( recording.file.size() );
		recording.blockFlags.push_back( job->header.flags );
		// a keyframe's bytes are not part of the stream a reader in order sees
		recording.streamEnds.push_back( ( recording.streamEnds.empty() ? 0 : recording.streamEnds.back() ) +
			( ( job->header.flags & idDemoBlock::FLAG_KEYFRAME ) != 0 ? 0 : rawBytes ) );
		recording.framesThrough.push_back( job->header.startFrame + job->header.frameCount );
		delete job;
	}
	closed.clear();
}

// A recording of several levels: a preamble, then per level a large first
// state and frames, as the session writes them, with a keyframe after every
// keyframeEvery frames of a level (0: none).
static recording_t Record( int levels, int framesPerLevel, bool cleanEnd, int level, int keyframeEvery ) {
	recording_t recording;
	recording.frames = 0;
	recording.timeUS = 0;
	recording.file.assign( PREFIX_BYTES, 0x7E );
	idDemoBlock::builder_t builder;
	idDemoBlock::encoder_t encoder;
	std::vector<idDemoBlock::job_t *> closed;
	auto append = [&]( const std::vector<uint8_t> &bytes ) {
		recording.stream.insert( recording.stream.end(), bytes.begin(), bytes.end() );
		builder.Append( bytes.data(), bytes.size(), closed );
		Emit( recording, encoder, closed, level );
	};
	append( RandomBytes( 200 ) );		// the version and header records
	for ( int segment = 0; segment < levels; segment++ ) {
		builder.BeginSegment( closed );
		Emit( recording, encoder, closed, level );
		recording.segmentStream.push_back( recording.stream.size() );
		recording.segmentFrame.push_back( recording.frames );
		recording.segmentTimeUS.push_back( recording.timeUS );
		// a level's first state is larger than one block
		append( DemoLikeStream( 1, 9000 + segment * 1500 ) );
		for ( int frame = 0; frame < framesPerLevel; frame++ ) {
			std::vector<uint8_t> bytes;
			AppendFrame( bytes, frame + segment * 1000, 30 + ( frame % 7 ) * 12 );
			append( bytes );
			const uint64_t deltaUS = recording.frames == 0 ? 0 : 16667 + ( frame % 3 );
			recording.frames++;
			recording.timeUS += deltaUS;
			builder.EndFrame( deltaUS, frame % 11 == 10, closed );
			Emit( recording, encoder, closed, level );
			if ( keyframeEvery > 0 && frame % keyframeEvery == keyframeEvery - 1 ) {
				// the first is larger than one block, the others share much with the stream
				const std::vector<uint8_t> content = DemoLikeStream( 1, recording.keyframeBytes.empty() ? 8000 : 900 + frame );
				builder.BeginKeyframe( closed );
				Emit( recording, encoder, closed, level );
				builder.Append( content.data(), content.size(), closed );
				Emit( recording, encoder, closed, level );
				builder.EndKeyframe( closed );
				Emit( recording, encoder, closed, level );
				recording.keyframeBytes.push_back( content );
				recording.keyframeStream.push_back( recording.stream.size() );
				recording.keyframeFrame.push_back( recording.frames );
				recording.keyframeTimeUS.push_back( recording.timeUS );
				recording.keyframeSegment.push_back( static_cast<uint32_t>( segment ) );
			}
		}
	}
	builder.Finish( cleanEnd, closed );
	Emit( recording, encoder, closed, level );
	return recording;
}

static void TestStream() {
	const recording_t recording = Record( 3, 90, true, 6, 25 );
	CHECK( recording.keyframeBytes.size() == 9 && recording.keyframeBytes[0].size() > idDemoBlock::MAX_RAW_BYTES,
		   "the test recording has %zu keyframes", recording.keyframeBytes.size() );
	memorySource_t source;
	source.file = &recording.file;

	// the headers alone
	idDemoBlock::timeline_t timeline;
	idDemoBlock::ReadTimeline( source, PREFIX_BYTES, recording.file.size(), timeline );
	CHECK( timeline.timed && timeline.cleanEnd, "timeline: timed %d clean %d", timeline.timed, timeline.cleanEnd );
	CHECK( timeline.frames == recording.frames && timeline.durationUS == recording.timeUS,
		   "timeline: %u frames, %llu us; recorded %u, %llu", timeline.frames, static_cast<unsigned long long>( timeline.durationUS ),
		   recording.frames, static_cast<unsigned long long>( recording.timeUS ) );
	CHECK( timeline.validBytes == recording.file.size() && timeline.blocks == recording.blockEnds.size(),
		   "timeline: %llu bytes in %u blocks", static_cast<unsigned long long>( timeline.validBytes ), timeline.blocks );
	std::vector<idDemoBlock::entry_t> levels, keyframes;
	uint64_t lastOffset = 0;
	for ( const idDemoBlock::entry_t &entry : timeline.entries ) {
		CHECK( entry.offset > lastOffset, "timeline entries are not in file order" );
		lastOffset = entry.offset;
		( entry.keyframe ? keyframes : levels ).push_back( entry );
	}
	CHECK( levels.size() == recording.segmentStream.size() && timeline.segments == levels.size(), "timeline: %zu levels", levels.size() );
	for ( size_t i = 0; i < levels.size() && i < recording.segmentFrame.size(); i++ ) {
		CHECK( levels[i].startFrame == recording.segmentFrame[i] && levels[i].startTimeUS == recording.segmentTimeUS[i] &&
			   levels[i].segment == i, "timeline: level %zu starts at frame %u", i, levels[i].startFrame );
	}
	CHECK( keyframes.size() == recording.keyframeBytes.size(), "timeline: %zu keyframes", keyframes.size() );
	for ( size_t i = 0; i < keyframes.size() && i < recording.keyframeFrame.size(); i++ ) {
		CHECK( keyframes[i].startFrame == recording.keyframeFrame[i] && keyframes[i].startTimeUS == recording.keyframeTimeUS[i] &&
			   keyframes[i].segment == recording.keyframeSegment[i],
			   "timeline: keyframe %zu is at frame %u of level %u", i, keyframes[i].startFrame, keyframes[i].segment );
	}

	// in order, in reads of awkward sizes
	idDemoBlock::reader_t reader;
	source.seeks = 0;
	reader.Open( &source, PREFIX_BYTES );
	std::vector<uint8_t> played;
	std::vector<uint8_t> buffer( 70001 );
	for ( size_t want = 1; ; want = want * 3 % 70001 + 1 ) {
		const size_t got = reader.Read( buffer.data(), want );
		played.insert( played.end(), buffer.begin(), buffer.begin() + got );
		if ( got != want ) {
			break;
		}
	}
	CHECK( played == recording.stream, "the stream read back differs (%zu of %zu bytes)", played.size(), recording.stream.size() );
	CHECK( reader.Ended() && reader.CleanEnd() && !reader.Damaged(), "a clean recording did not end cleanly: %s", reader.Problem() );
	CHECK( source.seeks <= 2, "reading in order took %d seeks", source.seeks );

	// opened at each level: exactly the stream from there on, keyframes left out
	for ( size_t i = 0; i < levels.size(); i++ ) {
		reader.SeekToBlock( levels[i].offset );
		std::vector<uint8_t> tail( recording.stream.size() - recording.segmentStream[i] + 16 );
		const size_t got = reader.Read( tail.data(), tail.size() );
		CHECK( got == recording.stream.size() - recording.segmentStream[i] &&
			   std::memcmp( tail.data(), recording.stream.data() + recording.segmentStream[i], got ) == 0,
			   "level %zu does not play from its own block", i );
	}
	// opened at each keyframe: what it holds, then the stream from that point,
	// later keyframes left out
	for ( size_t i = 0; i < keyframes.size() && i < recording.keyframeBytes.size(); i++ ) {
		reader.SeekToBlock( keyframes[i].offset );
		std::vector<uint8_t> expected = recording.keyframeBytes[i];
		expected.insert( expected.end(), recording.stream.begin() + recording.keyframeStream[i], recording.stream.end() );
		std::vector<uint8_t> got( expected.size() + 16 );
		const size_t count = reader.Read( got.data(), got.size() );
		CHECK( count == expected.size() && std::memcmp( got.data(), expected.data(), count ) == 0,
			   "keyframe %zu: %zu bytes read, %zu expected", i, count, expected.size() );
		CHECK( reader.CleanEnd() && !reader.Damaged(), "keyframe %zu: the stream did not end cleanly after it", i );
	}
	// a copy, block for block with every keyframe, is the same file
	{
		reader.Open( &source, PREFIX_BYTES );
		reader.HandOutEveryBlock( true );
		idDemoBlock::builder_t copier;
		idDemoBlock::encoder_t encoder;
		std::vector<idDemoBlock::job_t *> closed;
		std::vector<uint8_t> copy( recording.file.begin(), recording.file.begin() + PREFIX_BYTES );
		while ( reader.NextBlock() ) {
			idDemoBlock::job_t job;
			job.header = reader.Header();
			job.raw = reader.Block();
			copier.ContinueAfter( job.header );
			idDemoBlock::PackJob( encoder, 6, job );
			uint8_t head[idDemoBlock::HEADER_BYTES];
			idDemoBlock::WriteHeader( job.header, head );
			copy.insert( copy.end(), head, head + sizeof( head ) );
			copy.insert( copy.end(), job.packed.begin(), job.packed.end() );
		}
		// the copy ended with the source's last block: closing it adds nothing
		copier.Finish( true, closed );
		CHECK( closed.empty(), "closing a copy that already ended added %zu blocks", closed.size() );
		CHECK( copy == recording.file, "a block-for-block copy differs from its source (%zu and %zu bytes)", copy.size(), recording.file.size() );
		reader.HandOutEveryBlock( false );
	}
	// but not at a block that leans on the ones before it
	size_t leaning = 1;
	while ( leaning < recording.blockFlags.size() && ( recording.blockFlags[leaning] & idDemoBlock::FLAG_RESET ) != 0 ) {
		leaning++;
	}
	reader.SeekToBlock( recording.blockEnds[leaning - 1] );
	uint8_t one;
	const size_t gotMid = reader.Read( &one, 1 );
	CHECK( gotMid == 0 && reader.Damaged(), "a seek into the middle of a level was accepted" );
	std::printf( "stream: %zu blocks, %u frames over %zu levels read back in order, from each level and from each of %zu keyframes\n",
		recording.blockEnds.size(), recording.frames, levels.size(), keyframes.size() );
}

// What a reader makes of the file cut to `length` bytes.
static void CheckCut( const recording_t &recording, size_t length, int &warned ) {
	std::vector<uint8_t> cut( recording.file.begin(), recording.file.begin() + length );
	memorySource_t source;
	source.file = &cut;
	// the blocks wholly inside the cut
	size_t whole = 0;
	while ( whole < recording.blockEnds.size() && recording.blockEnds[whole] <= length ) {
		whole++;
	}
	const size_t expectStream = whole > 0 ? recording.streamEnds[whole - 1] : 0;
	const uint32_t expectFrames = whole > 0 ? recording.framesThrough[whole - 1] : 0;
	const bool everything = whole == recording.blockEnds.size();

	idDemoBlock::reader_t reader;
	reader.Open( &source, PREFIX_BYTES );
	std::vector<uint8_t> played( recording.stream.size() + 1024 );
	const size_t got = reader.Read( played.data(), played.size() );
	CHECK( got == expectStream && std::memcmp( played.data(), recording.stream.data(), got ) == 0,
		   "cut at %zu: read %zu stream bytes, %zu are whole", length, got, expectStream );
	CHECK( reader.Ended() && reader.CleanEnd() == everything, "cut at %zu: clean end %d", length, reader.CleanEnd() );
	const bool betweenBlocks = whole == 0 ? length == PREFIX_BYTES : length == recording.blockEnds[whole - 1];
	CHECK( reader.Damaged() == !betweenBlocks, "cut at %zu: damage %sreported (%s)", length, reader.Damaged() ? "" : "not ", reader.Problem() );
	warned += reader.Damaged() ? 1 : 0;

	idDemoBlock::timeline_t timeline;
	idDemoBlock::ReadTimeline( source, PREFIX_BYTES, cut.size(), timeline );
	CHECK( timeline.blocks == whole && timeline.frames == expectFrames && timeline.cleanEnd == everything,
		   "cut at %zu: timeline has %u blocks, %u frames; %zu and %u are whole", length, timeline.blocks, timeline.frames, whole, expectFrames );
	CHECK( timeline.validBytes == ( whole > 0 ? recording.blockEnds[whole - 1] : PREFIX_BYTES ), "cut at %zu: valid bytes %llu", length,
		   static_cast<unsigned long long>( timeline.validBytes ) );
}

static void TestCuts() {
	const recording_t recording = Record( 2, 24, true, 6, 10 );
	int warned = 0;
	int cuts = 0;
	// every byte around each block boundary and through each header, then a sweep
	for ( size_t block = 0; block < recording.blockEnds.size(); block++ ) {
		const size_t end = recording.blockEnds[block];
		const size_t start = block > 0 ? recording.blockEnds[block - 1] : PREFIX_BYTES;
		for ( size_t length = start; length <= start + idDemoBlock::HEADER_BYTES + 2 && length <= end; length++ ) {
			CheckCut( recording, length, warned );
			cuts++;
		}
		for ( size_t length = end > 3 ? end - 3 : 0; length <= end; length++ ) {
			CheckCut( recording, length, warned );
			cuts++;
		}
	}
	for ( size_t length = PREFIX_BYTES; length < recording.file.size(); length += 997 ) {
		CheckCut( recording, length, warned );
		cuts++;
	}
	CHECK( warned > 0 && warned < cuts, "cuts: %d of %d reported damage", warned, cuts );
	std::printf( "cuts: %d truncations, each played and described exactly its whole blocks\n", cuts );

	// a recording that was never ended: whole, but not clean
	const recording_t interrupted = Record( 2, 24, false, 6, 10 );
	memorySource_t source;
	source.file = &interrupted.file;
	idDemoBlock::reader_t reader;
	reader.Open( &source, PREFIX_BYTES );
	std::vector<uint8_t> played( interrupted.stream.size() + 16 );
	const size_t got = reader.Read( played.data(), played.size() );
	CHECK( got == interrupted.stream.size() && !reader.CleanEnd() && !reader.Damaged(), "an interrupted recording: %zu bytes, clean %d, %s",
		   got, reader.CleanEnd(), reader.Problem() );
	idDemoBlock::timeline_t timeline;
	idDemoBlock::ReadTimeline( source, PREFIX_BYTES, interrupted.file.size(), timeline );
	CHECK( !timeline.cleanEnd && timeline.frames == interrupted.frames && timeline.segments == 2 &&
		   timeline.entries.size() == 2 + interrupted.keyframeBytes.size(), "an interrupted timeline" );

	// bytes after a clean end (the summary trailer) are never read as blocks
	std::vector<uint8_t> withTrailer = recording.file;
	const std::vector<uint8_t> trailer = RandomBytes( 500 );
	withTrailer.insert( withTrailer.end(), trailer.begin(), trailer.end() );
	source.file = &withTrailer;
	reader.Open( &source, PREFIX_BYTES );
	played.assign( recording.stream.size() + 16, 0 );
	const size_t gotAll = reader.Read( played.data(), played.size() );
	CHECK( gotAll == recording.stream.size() && reader.CleanEnd() && !reader.Damaged(), "a trailer disturbed the stream's end" );
	idDemoBlock::ReadTimeline( source, PREFIX_BYTES, withTrailer.size(), timeline );
	CHECK( timeline.cleanEnd && timeline.validBytes == recording.file.size(), "a trailer disturbed the timeline" );
}

static void TestStreamDamage() {
	const recording_t recording = Record( 2, 24, true, 6, 10 );
	int stoppedEarly = 0;
	for ( int trial = 0; trial < 400; trial++ ) {
		std::vector<uint8_t> damaged = recording.file;
		const size_t at = PREFIX_BYTES + NextRandom() % ( damaged.size() - PREFIX_BYTES );
		damaged[at] ^= static_cast<uint8_t>( 1 << ( NextRandom() % 8 ) );
		// the block the damage lies in
		size_t block = 0;
		while ( recording.blockEnds[block] <= at ) {
			block++;
		}
		const size_t intact = block > 0 ? recording.streamEnds[block - 1] : 0;
		memorySource_t source;
		source.file = &damaged;
		idDemoBlock::reader_t reader;
		reader.Open( &source, PREFIX_BYTES );
		std::vector<uint8_t> played( recording.stream.size() + 16 );
		const size_t got = reader.Read( played.data(), played.size() );
		CHECK( got == intact && reader.Damaged() && !reader.CleanEnd(), "damage at %zu: %zu bytes played, %zu precede it (%s)", at, got, intact, reader.Problem() );
		CHECK( std::memcmp( played.data(), recording.stream.data(), got < intact ? got : intact ) == 0, "damage at %zu changed bytes before it", at );
		stoppedEarly += got == intact ? 1 : 0;
	}
	CHECK( stoppedEarly == 400, "%d of 400 damaged streams stopped at the damaged block", stoppedEarly );

	// what lies after the next way in is untouched: a reader sent there plays on
	idDemoBlock::timeline_t timeline;
	memorySource_t whole;
	whole.file = &recording.file;
	idDemoBlock::ReadTimeline( whole, PREFIX_BYTES, recording.file.size(), timeline );
	int resumed = 0;
	for ( size_t e = 1; e < timeline.entries.size(); e++ ) {
		// damage the block just before this entry
		std::vector<uint8_t> damaged = recording.file;
		damaged[static_cast<size_t>( timeline.entries[e].offset ) - 3] ^= 0x40;
		memorySource_t source;
		source.file = &damaged;
		idDemoBlock::timeline_t walked;
		idDemoBlock::ReadTimeline( source, PREFIX_BYTES, damaged.size(), walked );
		CHECK( walked.entries.size() == timeline.entries.size() && walked.frames == timeline.frames,
			   "damage in a payload hid %zu of the entries after it", timeline.entries.size() - walked.entries.size() );
		idDemoBlock::reader_t reader;
		reader.Open( &source, PREFIX_BYTES );
		std::vector<uint8_t> played( recording.stream.size() + ( 2u << 20 ) );
		reader.Read( played.data(), played.size() );
		CHECK( reader.Damaged() && reader.ProblemOffset() < timeline.entries[e].offset, "the damage before entry %zu was not met", e );
		reader.SeekToBlock( timeline.entries[e].offset );
		const size_t count = reader.Read( played.data(), played.size() );
		CHECK( count > 0 && reader.CleanEnd() && !reader.Damaged(), "the stream does not resume at entry %zu: %s", e, reader.Problem() );
		resumed += reader.CleanEnd() ? 1 : 0;
	}
	std::printf( "damage: 400 single-bit flips each stopped the stream at the block they hit; it resumed at each of %d later ways in\n", resumed );
}

static void TestBuilder() {
	// the untimed stream of a repack, and blocks copied with their timeline
	idDemoBlock::builder_t builder;
	builder.SetUntimed( true );
	std::vector<idDemoBlock::job_t *> closed;
	const std::vector<uint8_t> data = RandomBytes( 3 * idDemoBlock::MAX_RAW_BYTES + 77 );
	builder.Append( data.data(), data.size(), closed );
	builder.Finish( true, closed );
	CHECK( closed.size() == 4, "a repack of %zu bytes made %zu blocks", data.size(), closed.size() );
	size_t total = 0;
	for ( size_t i = 0; i < closed.size(); i++ ) {
		const idDemoBlock::header_t &header = closed[i]->header;
		CHECK( ( header.flags & idDemoBlock::FLAG_UNTIMED ) != 0, "block %zu of a repack claims a timeline", i );
		CHECK( ( ( header.flags & idDemoBlock::FLAG_RESET ) != 0 ) == ( i == 0 ), "block %zu reset flag", i );
		CHECK( ( ( header.flags & idDemoBlock::FLAG_STREAM_END ) != 0 ) == ( i + 1 == closed.size() ), "block %zu end flag", i );
		CHECK( closed[i]->raw.size() <= idDemoBlock::MAX_RAW_BYTES, "block %zu is %zu bytes", i, closed[i]->raw.size() );
		total += closed[i]->raw.size();
		delete closed[i];
	}
	CHECK( total == data.size() && builder.StreamBytes() == data.size(), "a repack carried %zu of %zu bytes", total, data.size() );
	closed.clear();

	// a clean end with nothing open is an empty block that still decodes
	idDemoBlock::builder_t empty;
	empty.Finish( true, closed );
	CHECK( closed.size() == 1 && closed[0]->raw.empty(), "an empty recording's end block" );
	if ( closed.size() == 1 ) {
		idDemoBlock::encoder_t encoder;
		idDemoBlock::PackJob( encoder, 6, *closed[0] );
		uint8_t head[idDemoBlock::HEADER_BYTES];
		idDemoBlock::WriteHeader( closed[0]->header, head );
		idDemoBlock::header_t read;
		CHECK( idDemoBlock::ReadHeader( head, read ) && read.rawBytes == 0 && read.packedBytes == 0, "the empty end block's header" );
		idDemoBlock::decoder_t decoder;
		CHECK( decoder.Unpack( read.codec, NULL, 0, NULL, 0 ), "the empty end block does not decode" );
		delete closed[0];
	}
	closed.clear();

	// frames that end in a later block than they began in are counted once
	idDemoBlock::builder_t frames;
	const std::vector<uint8_t> big = RandomBytes( idDemoBlock::MAX_RAW_BYTES + 5000 );
	frames.BeginSegment( closed );
	frames.Append( big.data(), big.size(), closed );
	frames.EndFrame( 0, false, closed );
	frames.Append( big.data(), 1000, closed );
	frames.EndFrame( 16667, true, closed );
	frames.Finish( true, closed );
	uint32_t counted = 0;
	uint64_t time = 0;
	for ( size_t i = 0; i < closed.size(); i++ ) {
		CHECK( closed[i]->header.startFrame == counted && closed[i]->header.startTimeUS == time, "block %zu starts at frame %u", i, closed[i]->header.startFrame );
		counted += closed[i]->header.frameCount;
		time += closed[i]->header.durationUS;
		CHECK( closed[i]->header.segment == 0, "block %zu belongs to level %u", i, closed[i]->header.segment );
		CHECK( ( ( closed[i]->header.flags & idDemoBlock::FLAG_SEGMENT_START ) != 0 ) == ( i == 0 ), "block %zu level start flag", i );
		delete closed[i];
	}
	CHECK( counted == 2 && time == 16667, "two frames were counted as %u over %llu us", counted, static_cast<unsigned long long>( time ) );
	closed.clear();

	// a keyframe: its own blocks, the first of which stands alone; the stream
	// after it does not start again
	idDemoBlock::builder_t keyed;
	keyed.BeginSegment( closed );
	keyed.Append( big.data(), 1000, closed );
	keyed.EndFrame( 0, false, closed );
	keyed.BeginKeyframe( closed );
	keyed.Append( big.data(), big.size(), closed );
	keyed.EndKeyframe( closed );
	keyed.Append( big.data(), 500, closed );
	keyed.EndFrame( 16667, false, closed );
	keyed.Finish( true, closed );
	const uint8_t K = idDemoBlock::FLAG_KEYFRAME, R = idDemoBlock::FLAG_RESET, S = idDemoBlock::FLAG_SEGMENT_START;
	const uint8_t expectedFlags[] = { static_cast<uint8_t>( R | S ), static_cast<uint8_t>( R | K ), K, 0 };
	CHECK( closed.size() == 4, "a level, a keyframe of two blocks and a frame made %zu blocks", closed.size() );
	for ( size_t i = 0; i < closed.size() && i < 4; i++ ) {
		CHECK( ( closed[i]->header.flags & ( K | R | S ) ) == expectedFlags[i], "block %zu has flags %u", i, closed[i]->header.flags );
		CHECK( closed[i]->header.startFrame == ( i == 0 ? 0u : 1u ) && closed[i]->header.frameCount == ( i == 0 || i == 3 ? 1u : 0u ),
			   "block %zu counts frames %u+%u", i, closed[i]->header.startFrame, closed[i]->header.frameCount );
	}
	for ( idDemoBlock::job_t *job : closed ) {
		delete job;
	}
	std::printf( "builder: repacks, the empty end block, frames across blocks and keyframe blocks\n" );
}

static double Now() {
	return std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

static int Bench( int argc, char **argv ) {
	if ( argc < 3 ) {
		std::printf( "usage: --bench <file> [blockBytes] [level]\n" );
		return 2;
	}
	const size_t blockBytes = argc > 3 ? static_cast<size_t>( std::atoi( argv[3] ) ) : idDemoBlock::TARGET_RAW_BYTES;
	const int level = argc > 4 ? std::atoi( argv[4] ) : 6;
	FILE *f = std::fopen( argv[2], "rb" );
	if ( f == NULL || blockBytes == 0 || blockBytes > idDemoBlock::MAX_RAW_BYTES ) {
		std::printf( "cannot read %s in blocks of %zu\n", argv[2], blockBytes );
		return 1;
	}
	std::vector<uint8_t> data;
	std::vector<uint8_t> chunk( 1 << 20 );
	for ( size_t got; ( got = std::fread( chunk.data(), 1, chunk.size(), f ) ) > 0; ) {
		data.insert( data.end(), chunk.begin(), chunk.begin() + got );
	}
	std::fclose( f );

	idDemoBlock::encoder_t encoder;
	std::vector<std::vector<uint8_t>> blocks;
	std::vector<uint8_t> codecs;
	size_t packedTotal = 0;
	double worstPack = 0.0;
	const double t0 = Now();
	for ( size_t offset = 0; offset < data.size(); offset += blockBytes ) {
		const size_t count = data.size() - offset < blockBytes ? data.size() - offset : blockBytes;
		std::vector<uint8_t> packed;
		const double b0 = Now();
		codecs.push_back( encoder.Pack( data.data() + offset, count, level, packed ) );
		worstPack = Now() - b0 > worstPack ? Now() - b0 : worstPack;
		packedTotal += packed.size() + idDemoBlock::HEADER_BYTES;
		blocks.push_back( std::move( packed ) );
	}
	const double t1 = Now();
	idDemoBlock::decoder_t decoder;
	std::vector<uint8_t> out( blockBytes );
	bool ok = true;
	double worstUnpack = 0.0;
	size_t offset = 0;
	for ( size_t i = 0; i < blocks.size() && ok; i++ ) {
		const size_t count = data.size() - offset < blockBytes ? data.size() - offset : blockBytes;
		const double b0 = Now();
		ok = decoder.Unpack( codecs[i], blocks[i].data(), blocks[i].size(), out.data(), count ) &&
			std::memcmp( out.data(), data.data() + offset, count ) == 0;
		worstUnpack = Now() - b0 > worstUnpack ? Now() - b0 : worstUnpack;
		offset += count;
	}
	const double t2 = Now();
	const double megabytes = data.size() / 1048576.0;
	std::printf( "%s: %.2f MiB in %.2f MiB (%.2f:1), blocks of %zu at level %d; pack %.0f MiB/s (worst block %.1f ms), "
		"unpack %.0f MiB/s (worst %.1f ms)%s\n", argv[2], megabytes, packedTotal / 1048576.0,
		static_cast<double>( data.size() ) / ( packedTotal > 0 ? packedTotal : 1 ), blockBytes, level,
		megabytes / ( t1 - t0 ), worstPack * 1000.0, megabytes / ( t2 - t1 ), worstUnpack * 1000.0, ok ? "" : " ROUND TRIP FAILED" );
	return ok ? 0 : 1;
}

int main( int argc, char **argv ) {
	if ( argc > 1 && std::strcmp( argv[1], "--bench" ) == 0 ) {
		return Bench( argc, argv );
	}
	TestHeader();
	TestRoundTrips();
	TestReset();
	TestHostileLayers();
	TestDamagedPayloads();
	TestBuilder();
	TestStream();
	TestCuts();
	TestStreamDamage();
	if ( failures != 0 ) {
		std::printf( "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "demo block codec: all checks passed\n" );
	return 0;
}
