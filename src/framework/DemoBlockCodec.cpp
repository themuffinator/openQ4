/*
===========================================================================

openQ4 render demo block container

Copyright (C) 2026 openQ4 contributors

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This is an original openQ4 implementation; see DemoBlockCodec.h.

===========================================================================
*/

#include "DemoBlockCodec.h"

#include <cstring>

#include "../idlib/hashing/CRC32.h"
#include "../imagetools/PNG/ZlibStream.h"

namespace idDemoBlock {
namespace {

// The long-range stage indexes one position in ANCHOR_STEP of its history by
// the ANCHOR_BYTES that begin there and probes every position of a new block,
// so any run of ANCHOR_BYTES + ANCHOR_STEP - 1 repeated bytes is found. A
// match shorter than MIN_MATCH costs about as much to describe as to store.
const size_t	ANCHOR_BYTES	= 16;
const size_t	ANCHOR_STEP		= 8;
const size_t	MIN_MATCH		= 24;
const size_t	REP_MIN_MATCH	= 8;		// at the last distance a match is cheap to describe
const int		ANCHOR_BITS		= 19;
const size_t	HISTORY_LIMIT	= 2 * static_cast<size_t>( WINDOW_BYTES ) + MAX_RAW_BYTES;
const int		VARINT_MAX_BYTES = 5;

inline uint32_t Get32( const uint8_t *in ) {
	return static_cast<uint32_t>( in[0] ) | ( static_cast<uint32_t>( in[1] ) << 8 ) |
		( static_cast<uint32_t>( in[2] ) << 16 ) | ( static_cast<uint32_t>( in[3] ) << 24 );
}

inline uint64_t Get64( const uint8_t *in ) {
	return static_cast<uint64_t>( Get32( in ) ) | ( static_cast<uint64_t>( Get32( in + 4 ) ) << 32 );
}

inline void Put16( uint8_t *out, uint32_t value ) {
	out[0] = static_cast<uint8_t>( value );
	out[1] = static_cast<uint8_t>( value >> 8 );
}

inline void Put32( uint8_t *out, uint32_t value ) {
	out[0] = static_cast<uint8_t>( value );
	out[1] = static_cast<uint8_t>( value >> 8 );
	out[2] = static_cast<uint8_t>( value >> 16 );
	out[3] = static_cast<uint8_t>( value >> 24 );
}

inline void Put64( uint8_t *out, uint64_t value ) {
	Put32( out, static_cast<uint32_t>( value ) );
	Put32( out + 4, static_cast<uint32_t>( value >> 32 ) );
}

inline uint64_t Load64( const uint8_t *in ) {
	uint64_t value;
	memcpy( &value, in, sizeof( value ) );
	return value;
}

// Only ever compared with itself on one machine, so byte order does not matter.
inline uint32_t AnchorHash( const uint8_t *in ) {
	uint64_t hash = Load64( in ) * UINT64_C( 0x9E3779B185EBCA87 );
	hash ^= hash >> 32;
	hash += Load64( in + 8 ) * UINT64_C( 0xC2B2AE3D27D4EB4F );
	hash ^= hash >> 29;
	hash *= UINT64_C( 0x165667B19E3779F9 );
	return static_cast<uint32_t>( hash >> ( 64 - ANCHOR_BITS ) );
}

inline size_t MatchForward( const uint8_t *a, const uint8_t *b, size_t limit ) {
	size_t length = 0;
	while ( length + 8 <= limit && Load64( a + length ) == Load64( b + length ) ) {
		length += 8;
	}
	while ( length < limit && a[length] == b[length] ) {
		length++;
	}
	return length;
}

inline void PutVarint( std::vector<uint8_t> &out, uint32_t value ) {
	while ( value >= 0x80u ) {
		out.push_back( static_cast<uint8_t>( ( value & 0x7Fu ) | 0x80u ) );
		value >>= 7;
	}
	out.push_back( static_cast<uint8_t>( value ) );
}

inline bool GetVarint( const uint8_t *&cursor, const uint8_t *end, uint32_t &value ) {
	value = 0;
	for ( int shift = 0; shift < 7 * VARINT_MAX_BYTES; shift += 7 ) {
		if ( cursor >= end ) {
			return false;
		}
		const uint8_t next = *cursor++;
		if ( shift == 28 && next > 0x0Fu ) {
			return false;
		}
		value |= static_cast<uint32_t>( next & 0x7Fu ) << shift;
		if ( ( next & 0x80u ) == 0 ) {
			return true;
		}
	}
	return false;
}

// Makes room for one more block after the window, keeping the newest
// WINDOW_BYTES. Returns how far the history moved down.
size_t MakeRoom( std::vector<uint8_t> &history, size_t &filled, size_t bytes ) {
	size_t shift = 0;
	if ( filled + bytes > HISTORY_LIMIT ) {
		shift = ( filled - WINDOW_BYTES ) & ~( ANCHOR_STEP - 1 );
		memmove( history.data(), history.data() + shift, filled - shift );
		filled -= shift;
	}
	if ( history.size() < filled + bytes ) {
		size_t wanted = history.size() < ( 1u << 20 ) ? ( 1u << 20 ) : history.size() * 2;
		if ( wanted < filled + bytes ) {
			wanted = filled + bytes;
		}
		if ( wanted > HISTORY_LIMIT ) {
			wanted = HISTORY_LIMIT;
		}
		history.resize( wanted );
	}
	return shift;
}

}	// namespace

/*
================
WriteHeader
================
*/
void WriteHeader( const header_t &header, uint8_t *out ) {
	Put32( out, MAGIC );
	Put16( out + 4, HEADER_BYTES );
	out[6] = header.flags;
	out[7] = header.codec;
	Put32( out + 8, header.rawBytes );
	Put32( out + 12, header.packedBytes );
	Put32( out + 16, header.segment );
	Put32( out + 20, header.startFrame );
	Put32( out + 24, header.frameCount );
	Put64( out + 28, header.startTimeUS );
	Put64( out + 36, header.durationUS );
	Put32( out + 44, header.payloadCRC );
	Put32( out + 48, CRC32_BlockChecksum( out, HEADER_BYTES - 4 ) );
}

/*
================
ReadHeader
================
*/
bool ReadHeader( const uint8_t *in, header_t &header ) {
	if ( Get32( in ) != MAGIC ||
		 ( static_cast<uint32_t>( in[4] ) | ( static_cast<uint32_t>( in[5] ) << 8 ) ) != static_cast<uint32_t>( HEADER_BYTES ) ||
		 Get32( in + 48 ) != CRC32_BlockChecksum( in, HEADER_BYTES - 4 ) ) {
		return false;
	}
	header.flags = in[6];
	header.codec = in[7];
	header.rawBytes = Get32( in + 8 );
	header.packedBytes = Get32( in + 12 );
	header.segment = Get32( in + 16 );
	header.startFrame = Get32( in + 20 );
	header.frameCount = Get32( in + 24 );
	header.startTimeUS = Get64( in + 28 );
	header.durationUS = Get64( in + 36 );
	header.payloadCRC = Get32( in + 44 );
	if ( ( header.flags & ~FLAG_KNOWN_MASK ) != 0 || header.rawBytes > MAX_RAW_BYTES ) {
		return false;
	}
	if ( ( header.flags & FLAG_SEGMENT_START ) != 0 &&
		 ( ( header.flags & FLAG_RESET ) == 0 || ( header.flags & FLAG_KEYFRAME ) != 0 ) ) {
		return false;
	}
	if ( header.codec == CODEC_STORED ) {
		return header.packedBytes == header.rawBytes;
	}
	// a packed payload is only kept when it is smaller than the bytes it holds
	return header.codec == CODEC_LONG_RANGE && header.packedBytes < header.rawBytes;
}

/*
================
PayloadChecksum
================
*/
uint32_t PayloadChecksum( const uint8_t *data, size_t bytes ) {
	return CRC32_BlockChecksum( data, static_cast<int>( bytes ) );
}

/*
================
encoder_t::encoder_t
================
*/
encoder_t::encoder_t() : filled( 0 ), inserted( 0 ), lastDistance( 0 ) {
}

/*
================
encoder_t::Reset
================
*/
void encoder_t::Reset() {
	filled = 0;
	inserted = 0;
	lastDistance = 0;
	if ( !anchors.empty() ) {
		memset( anchors.data(), 0, anchors.size() * sizeof( anchors[0] ) );
	}
}

/*
================
encoder_t::Slide

The history moved down by shift bytes: move what the anchors point at too.
================
*/
void encoder_t::Slide() {
	const size_t shift = MakeRoom( history, filled, MAX_RAW_BYTES );
	if ( shift == 0 ) {
		return;
	}
	inserted = inserted > shift ? inserted - shift : 0;
	for ( size_t i = 0; i < anchors.size(); i++ ) {
		const uint32_t offset = anchors[i];
		anchors[i] = offset > shift ? static_cast<uint32_t>( offset - shift ) : 0;
	}
}

/*
================
encoder_t::InsertUpTo

Indexes every sampled position below end whose anchor bytes are all present.
================
*/
void encoder_t::InsertUpTo( size_t end ) {
	const uint8_t *data = history.data();
	size_t position = inserted;
	for ( ; position < end && position + ANCHOR_BYTES <= filled; position += ANCHOR_STEP ) {
		anchors[AnchorHash( data + position )] = static_cast<uint32_t>( position + 1 );
	}
	inserted = position;
}

/*
================
encoder_t::PutLiterals

Ends a sequence's literal run [from, to): its count and its bytes, as they are
or as the XOR with the bytes rep before them, whichever leaves more zeros.
================
*/
void encoder_t::PutLiterals( const uint8_t *data, size_t from, size_t to, size_t rep ) {
	const size_t count = to - from;
	bool delta = false;
	if ( rep != 0 && count > 0 ) {
		size_t plainZeros = 0;
		size_t deltaZeros = 0;
		for ( size_t i = from; i < to; i++ ) {
			plainZeros += data[i] == 0;
			deltaZeros += data[i] == data[i - rep];
		}
		delta = deltaZeros > plainZeros;
	}
	PutVarint( layer, static_cast<uint32_t>( ( count << 1 ) | ( delta ? 1u : 0u ) ) );
	if ( delta ) {
		const size_t first = literals.size();
		literals.resize( first + count );
		for ( size_t i = 0; i < count; i++ ) {
			literals[first + i] = static_cast<uint8_t>( data[from + i] ^ data[from + i - rep] );
		}
	} else {
		literals.insert( literals.end(), data + from, data + to );
	}
}

/*
================
encoder_t::Pack
================
*/
uint8_t encoder_t::Pack( const uint8_t *raw, size_t rawBytes, int level, std::vector<uint8_t> &packed ) {
	packed.clear();
	if ( rawBytes == 0 || rawBytes > MAX_RAW_BYTES ) {
		return CODEC_STORED;
	}
	if ( anchors.empty() ) {
		anchors.assign( static_cast<size_t>( 1 ) << ANCHOR_BITS, 0 );
	}
	Slide();
	const size_t start = filled;
	const size_t end = start + rawBytes;
	memcpy( history.data() + start, raw, rawBytes );
	filled = end;

	if ( level <= 0 ) {
		packed.assign( raw, raw + rawBytes );
		return CODEC_STORED;
	}

	// The sequences, then the literals of every sequence:
	//   varint literal count * 2 + 1 when the literals are stored as the XOR
	//          with the bytes one "last distance" before them
	//   varint match length (0: none)
	//   varint match distance, when the length is not 0 (0: the last distance)
	// A record that differs from the frame before in a few fields lines up
	// with it at one distance: the run resumes there after each changed field
	// for a byte, and the changed bytes mostly keep their high bits.
	const uint8_t *data = history.data();
	layer.clear();
	layer.resize( 4 );
	literals.clear();
	size_t literalStart = start;
	size_t position = start;
	size_t rep = lastDistance;
	while ( position < end ) {
		size_t matchBytes = 0;
		size_t matchBack = 0;
		size_t distance = 0;
		if ( rep != 0 && end - position >= REP_MIN_MATCH ) {
			const size_t forward = MatchForward( data + position - rep, data + position, end - position );
			if ( forward >= REP_MIN_MATCH ) {
				matchBytes = forward;
				distance = rep;
			}
		}
		if ( matchBytes == 0 && position + ANCHOR_BYTES <= end ) {
			InsertUpTo( position );
			const uint32_t entry = anchors[AnchorHash( data + position )];
			if ( entry != 0 ) {
				const size_t candidate = entry - 1;
				if ( candidate < position && position - candidate <= WINDOW_BYTES ) {
					const size_t forward = MatchForward( data + candidate, data + position, end - position );
					if ( forward >= ANCHOR_BYTES ) {
						size_t back = 0;
						const size_t maxBack = position - literalStart < candidate ? position - literalStart : candidate;
						while ( back < maxBack && data[candidate - back - 1] == data[position - back - 1] ) {
							back++;
						}
						if ( forward + back >= MIN_MATCH ) {
							matchBytes = forward + back;
							matchBack = back;
							distance = position - candidate;
						}
					}
				}
			}
		}
		if ( matchBytes == 0 ) {
			position++;
			continue;
		}
		PutLiterals( data, literalStart, position - matchBack, rep );
		PutVarint( layer, static_cast<uint32_t>( matchBytes ) );
		PutVarint( layer, distance == rep ? 0u : static_cast<uint32_t>( distance ) );
		rep = distance;
		position += matchBytes - matchBack;
		literalStart = position;
	}
	if ( literalStart < end ) {
		PutLiterals( data, literalStart, end, rep );
		PutVarint( layer, 0 );
	}
	lastDistance = rep;
	Put32( layer.data(), static_cast<uint32_t>( layer.size() - 4 ) );
	layer.insert( layer.end(), literals.begin(), literals.end() );

	packed.resize( 4 + Zlib_CompressBound( layer.size() ) );
	Put32( packed.data(), static_cast<uint32_t>( layer.size() ) );
	const size_t zlibBytes = Zlib_Compress( layer.data(), layer.size(), packed.data() + 4, level > 9 ? 9 : level );
	if ( zlibBytes == 0 || 4 + zlibBytes >= rawBytes ) {
		packed.assign( raw, raw + rawBytes );
		return CODEC_STORED;
	}
	packed.resize( 4 + zlibBytes );
	return CODEC_LONG_RANGE;
}

/*
================
decoder_t::decoder_t
================
*/
decoder_t::decoder_t() : filled( 0 ), lastDistance( 0 ) {
}

/*
================
decoder_t::Reset
================
*/
void decoder_t::Reset() {
	filled = 0;
	lastDistance = 0;
}

/*
================
decoder_t::Unpack
================
*/
bool decoder_t::Unpack( uint8_t codec, const uint8_t *packed, size_t packedBytes, uint8_t *raw, size_t rawBytes ) {
	if ( rawBytes > MAX_RAW_BYTES ) {
		return false;
	}
	if ( rawBytes == 0 ) {
		return codec == CODEC_STORED && packedBytes == 0;
	}
	MakeRoom( history, filled, MAX_RAW_BYTES );
	uint8_t *out = history.data() + filled;
	size_t newDistance = lastDistance;

	if ( codec == CODEC_STORED ) {
		if ( packedBytes != rawBytes ) {
			return false;
		}
		memcpy( out, packed, rawBytes );
	} else if ( codec == CODEC_LONG_RANGE ) {
		if ( packedBytes < 4 ) {
			return false;
		}
		// every sequence but the last describes at least one byte in at most
		// three varints, so a valid layer is never larger than this
		const size_t layerBytes = Get32( packed );
		if ( layerBytes < 4 || layerBytes > 4 + rawBytes + 3 * VARINT_MAX_BYTES * ( rawBytes + 1 ) ) {
			return false;
		}
		layer.resize( layerBytes );
		if ( Zlib_Inflate( packed + 4, packedBytes - 4, layer.data(), layerBytes, true ) != ZLIB_OK ) {
			return false;
		}
		const size_t sequenceBytes = Get32( layer.data() );
		if ( sequenceBytes > layerBytes - 4 ) {
			return false;
		}
		const uint8_t *cursor = layer.data() + 4;
		const uint8_t *sequenceEnd = cursor + sequenceBytes;
		const uint8_t *literal = sequenceEnd;
		const uint8_t *literalEnd = layer.data() + layerBytes;
		size_t produced = 0;
		size_t rep = lastDistance;
		while ( cursor < sequenceEnd ) {
			uint32_t literalCode = 0;
			uint32_t matchBytes = 0;
			if ( !GetVarint( cursor, sequenceEnd, literalCode ) || !GetVarint( cursor, sequenceEnd, matchBytes ) ) {
				return false;
			}
			const size_t literalBytes = literalCode >> 1;
			if ( literalBytes > rawBytes - produced || literalBytes > static_cast<size_t>( literalEnd - literal ) ) {
				return false;
			}
			if ( ( literalCode & 1u ) != 0 ) {
				// stored as the difference from the bytes one last distance back
				if ( rep == 0 || rep > filled + produced ) {
					return false;
				}
				uint8_t *to = out + produced;
				for ( size_t i = 0; i < literalBytes; i++ ) {
					to[i] = static_cast<uint8_t>( literal[i] ^ to[i - rep] );
				}
			} else {
				memcpy( out + produced, literal, literalBytes );
			}
			literal += literalBytes;
			produced += literalBytes;
			if ( matchBytes == 0 ) {
				continue;
			}
			uint32_t distanceCode = 0;
			if ( !GetVarint( cursor, sequenceEnd, distanceCode ) ) {
				return false;
			}
			const size_t distance = distanceCode != 0 ? distanceCode : rep;
			if ( matchBytes > rawBytes - produced || distance == 0 || distance > WINDOW_BYTES ||
				 distance > filled + produced ) {
				return false;
			}
			rep = distance;
			const uint8_t *source = out + produced - distance;
			if ( distance >= matchBytes ) {
				memcpy( out + produced, source, matchBytes );
			} else {
				// the run repeats itself
				for ( uint32_t i = 0; i < matchBytes; i++ ) {
					out[produced + i] = source[i];
				}
			}
			produced += matchBytes;
		}
		if ( produced != rawBytes || literal != literalEnd ) {
			return false;
		}
		newDistance = rep;
	} else {
		return false;
	}

	memcpy( raw, out, rawBytes );
	filled += rawBytes;
	lastDistance = newDistance;
	return true;
}

/*
================
PackJob
================
*/
void PackJob( encoder_t &encoder, int level, job_t &job ) {
	if ( ( job.header.flags & FLAG_RESET ) != 0 ) {
		encoder.Reset();
	}
	job.header.rawBytes = static_cast<uint32_t>( job.raw.size() );
	job.header.codec = encoder.Pack( job.raw.data(), job.raw.size(), level, job.packed );
	job.header.packedBytes = static_cast<uint32_t>( job.packed.size() );
	job.header.payloadCRC = PayloadChecksum( job.packed.data(), job.packed.size() );
	std::vector<uint8_t>().swap( job.raw );
}

/*
================
builder_t::builder_t
================
*/
builder_t::builder_t() : untimed( false ), nextReset( true ), nextSegmentStart( false ), inKeyframe( false ),
	streamEnded( false ), segmentsBegun( 0 ), startFrame( 0 ), frameCount( 0 ), startTimeUS( 0 ), durationUS( 0 ),
	streamBytes( 0 ) {
}

/*
================
builder_t::SetUntimed
================
*/
void builder_t::SetUntimed( bool value ) {
	untimed = value;
}

/*
================
builder_t::Close

Ends the open block and starts the next one where its timeline ends.
================
*/
void builder_t::Close( bool frameEnd, bool streamEnd, std::vector<job_t *> &closed ) {
	job_t *job = new job_t;
	memset( &job->header, 0, sizeof( job->header ) );
	job->header.flags = static_cast<uint8_t>(
		( nextReset ? FLAG_RESET : 0 ) |
		( nextSegmentStart ? FLAG_SEGMENT_START : 0 ) |
		( frameEnd ? FLAG_FRAME_END : 0 ) |
		( streamEnd ? FLAG_STREAM_END : 0 ) |
		( untimed ? FLAG_UNTIMED : 0 ) |
		( inKeyframe ? FLAG_KEYFRAME : 0 ) );
	job->header.segment = segmentsBegun > 0 ? segmentsBegun - 1 : 0;
	job->header.startFrame = startFrame;
	job->header.frameCount = frameCount;
	job->header.startTimeUS = startTimeUS;
	job->header.durationUS = durationUS;
	job->raw.swap( pending );
	pending.reserve( TARGET_RAW_BYTES + TARGET_RAW_BYTES / 2 );
	streamBytes += job->raw.size();
	nextReset = false;
	nextSegmentStart = false;
	startFrame += frameCount;
	startTimeUS += durationUS;
	frameCount = 0;
	durationUS = 0;
	closed.push_back( job );
}

/*
================
builder_t::Append
================
*/
void builder_t::Append( const void *data, size_t bytes, std::vector<job_t *> &closed ) {
	const uint8_t *in = static_cast<const uint8_t *>( data );
	while ( bytes > 0 ) {
		const size_t room = MAX_RAW_BYTES - pending.size();
		const size_t take = bytes < room ? bytes : room;
		pending.insert( pending.end(), in, in + take );
		in += take;
		bytes -= take;
		if ( pending.size() >= MAX_RAW_BYTES ) {
			// full in the middle of a frame, or of a level's first state
			Close( false, false, closed );
		}
	}
}

/*
================
builder_t::EndFrame
================
*/
void builder_t::EndFrame( uint64_t deltaUS, bool closeBlock, std::vector<job_t *> &closed ) {
	frameCount++;
	durationUS += deltaUS;
	if ( closeBlock || pending.size() >= TARGET_RAW_BYTES ) {
		Close( true, false, closed );
	}
}

/*
================
builder_t::BeginSegment
================
*/
void builder_t::BeginSegment( std::vector<job_t *> &closed ) {
	if ( !pending.empty() || frameCount > 0 ) {
		Close( true, false, closed );
	}
	nextReset = true;
	nextSegmentStart = true;
	segmentsBegun++;
}

/*
================
builder_t::BeginKeyframe

A keyframe lies between two frames and starts the packer's history again, so
it decodes with nothing before it and the blocks after it lean on it alone.
================
*/
void builder_t::BeginKeyframe( std::vector<job_t *> &closed ) {
	if ( !pending.empty() || frameCount > 0 ) {
		Close( true, false, closed );
	}
	nextReset = true;
	inKeyframe = true;
}

/*
================
builder_t::EndKeyframe
================
*/
void builder_t::EndKeyframe( std::vector<job_t *> &closed ) {
	if ( !inKeyframe ) {
		return;
	}
	// an empty keyframe still leaves its block, which is where a reader enters
	Close( true, false, closed );
	inKeyframe = false;
}

/*
================
builder_t::Finish
================
*/
void builder_t::Finish( bool streamEnd, std::vector<job_t *> &closed ) {
	EndKeyframe( closed );
	if ( !pending.empty() || frameCount > 0 || ( streamEnd && !streamEnded ) ) {
		Close( true, streamEnd, closed );
		streamEnded = streamEnded || streamEnd;
	}
}

/*
================
builder_t::ContinueAfter
================
*/
void builder_t::ContinueAfter( const header_t &copied ) {
	streamBytes += copied.rawBytes;
	startFrame = copied.startFrame + copied.frameCount;
	startTimeUS = copied.startTimeUS + copied.durationUS;
	frameCount = 0;
	durationUS = 0;
	if ( ( copied.flags & FLAG_SEGMENT_START ) != 0 || copied.segment > 0 ) {
		segmentsBegun = copied.segment + 1;
	}
	untimed = ( copied.flags & FLAG_UNTIMED ) != 0;
	streamEnded = ( copied.flags & FLAG_STREAM_END ) != 0;
	nextReset = false;
	nextSegmentStart = false;
}

/*
================
ReadTimeline
================
*/
void ReadTimeline( source_t &source, uint64_t offset, uint64_t length, timeline_t &timeline ) {
	timeline.timed = true;
	timeline.cleanEnd = false;
	timeline.frames = 0;
	timeline.durationUS = 0;
	timeline.validBytes = offset;
	timeline.blocks = 0;
	timeline.segments = 0;
	timeline.entries.clear();
	while ( offset + HEADER_BYTES <= length ) {
		uint8_t head[HEADER_BYTES];
		header_t header;
		if ( !source.Seek( offset ) || source.Read( head, HEADER_BYTES ) != static_cast<size_t>( HEADER_BYTES ) ||
			 !ReadHeader( head, header ) || offset + HEADER_BYTES + header.packedBytes > length ) {
			break;
		}
		if ( ( header.flags & FLAG_UNTIMED ) != 0 ) {
			timeline.timed = false;
		}
		const bool keyframe = ( header.flags & ( FLAG_KEYFRAME | FLAG_RESET ) ) == ( FLAG_KEYFRAME | FLAG_RESET );
		if ( ( header.flags & FLAG_SEGMENT_START ) != 0 || keyframe ) {
			entry_t entry;
			entry.offset = offset;
			entry.startFrame = header.startFrame;
			entry.startTimeUS = header.startTimeUS;
			entry.segment = header.segment;
			entry.keyframe = keyframe;
			timeline.entries.push_back( entry );
			timeline.segments += keyframe ? 0 : 1;
		}
		timeline.frames = header.startFrame + header.frameCount;
		timeline.durationUS = header.startTimeUS + header.durationUS;
		offset += HEADER_BYTES + header.packedBytes;
		timeline.validBytes = offset;
		timeline.blocks++;
		if ( ( header.flags & FLAG_STREAM_END ) != 0 ) {
			timeline.cleanEnd = true;
			break;
		}
	}
}

/*
================
reader_t::reader_t
================
*/
reader_t::reader_t() : source( NULL ), next( 0 ), sourceOffset( ~static_cast<uint64_t>( 0 ) ), cursor( 0 ),
	needReset( false ), enteredKeyframe( false ), everyBlock( false ), lastBlock( false ), ended( true ), cleanEnd( false ),
	problem( NULL ), problemOffset( 0 ) {
	memset( &header, 0, sizeof( header ) );
}

/*
================
reader_t::Open
================
*/
void reader_t::Open( source_t *from, uint64_t offset ) {
	source = from;
	sourceOffset = ~static_cast<uint64_t>( 0 );
	decoder.Reset();
	// a stream's first block stands alone like any level's
	SeekToBlock( offset );
}

/*
================
reader_t::SeekToBlock
================
*/
void reader_t::SeekToBlock( uint64_t offset ) {
	next = offset;
	raw.clear();
	cursor = 0;
	needReset = true;
	enteredKeyframe = false;
	lastBlock = false;
	ended = source == NULL;
	cleanEnd = false;
	problem = NULL;
	problemOffset = 0;
}

/*
================
reader_t::Fail
================
*/
bool reader_t::Fail( uint64_t offset, const char *reason ) {
	ended = true;
	problem = reason;
	problemOffset = offset;
	raw.clear();
	cursor = 0;
	sourceOffset = ~static_cast<uint64_t>( 0 );
	return false;
}

/*
================
reader_t::NextBlock
================
*/
bool reader_t::NextBlock() {
	for ( ;; ) {
		raw.clear();
		cursor = 0;
		if ( ended ) {
			return false;
		}
		if ( lastBlock ) {
			ended = true;
			cleanEnd = true;
			return false;
		}
		const uint64_t offset = next;
		if ( sourceOffset != offset ) {
			sourceOffset = ~static_cast<uint64_t>( 0 );
			if ( !source->Seek( offset ) ) {
				return Fail( offset, "the file cannot be read there" );
			}
		}
		uint8_t head[HEADER_BYTES];
		const size_t headBytes = source->Read( head, HEADER_BYTES );
		if ( headBytes == 0 ) {
			// the file stops between two blocks: a recording that was cut short
			ended = true;
			sourceOffset = ~static_cast<uint64_t>( 0 );
			return false;
		}
		if ( headBytes != static_cast<size_t>( HEADER_BYTES ) ) {
			return Fail( offset, "a block header is cut short" );
		}
		if ( !ReadHeader( head, header ) ) {
			return Fail( offset, "a block header is not valid" );
		}
		if ( needReset && ( header.flags & FLAG_RESET ) == 0 ) {
			return Fail( offset, "the block does not begin a level or a keyframe" );
		}
		packed.resize( header.packedBytes );
		if ( header.packedBytes > 0 && source->Read( packed.data(), header.packedBytes ) != header.packedBytes ) {
			return Fail( offset, "a block is cut short" );
		}
		if ( PayloadChecksum( packed.data(), packed.size() ) != header.payloadCRC ) {
			return Fail( offset, "a block does not match its checksum" );
		}
		if ( ( header.flags & FLAG_RESET ) != 0 ) {
			decoder.Reset();
		}
		raw.resize( header.rawBytes );
		if ( !decoder.Unpack( header.codec, packed.data(), packed.size(), raw.data(), raw.size() ) ) {
			return Fail( offset, "a block does not decode" );
		}
		const bool keyframe = ( header.flags & FLAG_KEYFRAME ) != 0;
		if ( needReset ) {
			// sent here: a keyframe's bytes are what the reader came for
			enteredKeyframe = keyframe;
		} else if ( !keyframe ) {
			enteredKeyframe = false;
		}
		needReset = false;
		next = offset + HEADER_BYTES + header.packedBytes;
		sourceOffset = next;
		lastBlock = ( header.flags & FLAG_STREAM_END ) != 0;
		if ( keyframe && !enteredKeyframe && !everyBlock ) {
			// met in order: the blocks after it lean on its bytes, which are
			// now in the history, but the stream goes on without them
			continue;
		}
		return true;
	}
}

/*
================
reader_t::Read
================
*/
size_t reader_t::Read( void *buffer, size_t bytes ) {
	size_t done = 0;
	while ( done < bytes ) {
		if ( cursor >= raw.size() ) {
			if ( !NextBlock() ) {
				break;
			}
			continue;
		}
		const size_t available = raw.size() - cursor;
		const size_t take = available < bytes - done ? available : bytes - done;
		memcpy( static_cast<uint8_t *>( buffer ) + done, raw.data() + cursor, take );
		cursor += take;
		done += take;
	}
	return done;
}

}	// namespace idDemoBlock
