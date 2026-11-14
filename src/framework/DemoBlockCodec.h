/*
===========================================================================

openQ4 render demo block container

Copyright (C) 2026 openQ4 contributors

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This is an original openQ4 format and implementation. It uses only fixed-width
values and C++ standard-library types, so the container is tested without the
engine (tools/tests/native/DemoBlockCodecTest.cpp).

===========================================================================
*/

#ifndef __DEMO_BLOCK_CODEC_H__
#define __DEMO_BLOCK_CODEC_H__

#include <cstddef>
#include <cstdint>
#include <vector>

/*
===============================================================================

	A render demo's command stream is carried as a sequence of blocks:

		header (HEADER_BYTES, little endian, its own CRC-32) | packed payload

	Every block is checked before a byte of it is used, so a recording that was
	cut short or damaged plays up to its last intact block and stops there. The
	header also places the block on the demo's timeline (frames and time before
	it, frames and time inside it, the level segment it belongs to), so a file
	without its closing summary is described by walking the headers alone, and
	a seek opens the file at a block a level or a keyframe begins with.

	Payloads are compressed in two stages. A render demo repeats itself at a
	distance of one frame, tens of kilobytes back, which DEFLATE's 32 KiB window
	cannot see. The long-range stage replaces runs that already occurred in the
	last WINDOW_BYTES of the stream with (length, distance) pairs; what is left
	is literals and short descriptors, which DEFLATE then packs. The history
	runs across blocks and starts again at a block flagged FLAG_RESET: the
	file's first block and the first block of every level segment and of
	every keyframe, which therefore decode with nothing before them.

	A keyframe is a second way into a level: blocks flagged FLAG_KEYFRAME hold
	everything a player needs to carry on from that point with nothing before
	it. A reader arriving in order decodes them, since the blocks after them
	lean on their bytes, but hands none of those bytes out; a reader that was
	sent to the keyframe hands them out and carries on into the blocks after.

===============================================================================
*/

namespace idDemoBlock {

const uint32_t	MAGIC			= 0x4234514Fu;		// "OQ4B"
const int		HEADER_BYTES	= 52;
const uint32_t	MAX_RAW_BYTES	= 1u << 20;			// the most one block may decode to
const uint32_t	WINDOW_BYTES	= 2u << 20;			// how far back a long-range match may reach

enum {
	CODEC_STORED		= 0,	// the payload is the raw bytes
	CODEC_LONG_RANGE	= 1		// long-range matches, then a zlib stream
};

enum {
	FLAG_RESET			= 1 << 0,	// decodes with no history
	FLAG_SEGMENT_START	= 1 << 1,	// the first block of a level segment; always with FLAG_RESET
	FLAG_FRAME_END		= 1 << 2,	// its last byte ends a frame (or the stream)
	FLAG_STREAM_END		= 1 << 3,	// the recording was stopped cleanly after this block
	FLAG_UNTIMED		= 1 << 4,	// repacked from an older stream: the timeline fields are not set
	FLAG_KEYFRAME		= 1 << 5,	// part of a keyframe; its first block also carries FLAG_RESET
	FLAG_KNOWN_MASK		= ( 1 << 6 ) - 1
};

struct header_t {
	uint8_t		flags;
	uint8_t		codec;
	uint32_t	rawBytes;
	uint32_t	packedBytes;
	uint32_t	segment;		// the level segment the block belongs to
	uint32_t	startFrame;		// complete frames in the blocks before it
	uint32_t	frameCount;		// frames that end inside it
	uint64_t	startTimeUS;	// where the last frame before it lies on the timeline
	uint64_t	durationUS;		// the time the frames inside it add
	uint32_t	payloadCRC;		// CRC-32 of the packed payload
};

// Serialises a header, its CRC included, into HEADER_BYTES bytes.
void		WriteHeader( const header_t &header, uint8_t *out );

// False unless the bytes are a header this reader can decode: magic, size,
// CRC, known flags and codec, and sizes within the block limits.
bool		ReadHeader( const uint8_t *in, header_t &header );

uint32_t	PayloadChecksum( const uint8_t *data, size_t bytes );

/*
===============================================================================

	Packs the stream's blocks in order. Not thread safe; one writer owns it.

===============================================================================
*/
class encoder_t {
public:
					encoder_t();

	// Forgets the history: the next block packed must be flagged FLAG_RESET.
	void			Reset();

	// Packs rawBytes (at most MAX_RAW_BYTES) into packed and returns the codec
	// used. level 0 stores; 1-9 are DEFLATE's levels behind the long-range stage.
	uint8_t			Pack( const uint8_t *raw, size_t rawBytes, int level, std::vector<uint8_t> &packed );

private:
	void			Slide();
	void			InsertUpTo( size_t end );
	void			PutLiterals( const uint8_t *data, size_t from, size_t to, size_t rep );

	std::vector<uint8_t>	history;		// the window, then the block being packed
	size_t					filled;			// bytes of history in use
	size_t					inserted;		// anchors are indexed below this offset
	size_t					lastDistance;	// the distance of the last match, 0 before the first
	std::vector<uint32_t>	anchors;		// hash of ANCHOR_BYTES -> offset + 1
	std::vector<uint8_t>	layer;			// the long-range stage's output
	std::vector<uint8_t>	literals;
};

/*
===============================================================================

	Unpacks the stream's blocks in order, from a FLAG_RESET block on.

===============================================================================
*/
class decoder_t {
public:
					decoder_t();

	void			Reset();

	// Decodes one block's payload into raw[0..rawBytes). False when the payload
	// is not a valid encoding of exactly that many bytes; nothing is trusted
	// before it is checked, and the history is then left as it was.
	bool			Unpack( uint8_t codec, const uint8_t *packed, size_t packedBytes, uint8_t *raw, size_t rawBytes );

private:
	std::vector<uint8_t>	history;
	size_t					filled;
	size_t					lastDistance;
	std::vector<uint8_t>	layer;
};

/*
===============================================================================

	The stream of blocks, without the engine: where the bytes come from and go
	to is the caller's business.

===============================================================================
*/

// A recording closes its open block at the end of the first frame that fills
// it this far, so playback decodes in small steps.
const size_t	TARGET_RAW_BYTES = 128 * 1024;

// A block on its way from the thread that fills it to the file.
struct job_t {
	header_t				header;
	std::vector<uint8_t>	raw;
	std::vector<uint8_t>	packed;
};

// Packs a closed block: its codec, sizes and checksum. Blocks are packed in
// the order they closed, by the one thread that owns the encoder.
void		PackJob( encoder_t &encoder, int level, job_t &job );

/*
===============================================================================

	Fills blocks and places them on the timeline. The caller owns the jobs it
	is handed, packs them in order and writes each as header then payload.

===============================================================================
*/
class builder_t {
public:
					builder_t();

	// A stream with no timeline of its own: one repacked from an older recording.
	void			SetUntimed( bool untimed );

	// Appends stream bytes; a block that fills up closes in mid-frame.
	void			Append( const void *data, size_t bytes, std::vector<job_t *> &closed );
	// A frame ended and adds deltaUS to the timeline. The open block closes
	// when it reached TARGET_RAW_BYTES or the caller asks.
	void			EndFrame( uint64_t deltaUS, bool closeBlock, std::vector<job_t *> &closed );
	// The bytes appended next begin a level segment.
	void			BeginSegment( std::vector<job_t *> &closed );
	// The bytes appended until EndKeyframe are a keyframe: a way into the
	// level at this point, between two frames.
	void			BeginKeyframe( std::vector<job_t *> &closed );
	void			EndKeyframe( std::vector<job_t *> &closed );
	// Closes the open block; with streamEnd, as the block that ends a
	// recording cleanly (an empty one when nothing is open).
	void			Finish( bool streamEnd, std::vector<job_t *> &closed );
	// Carries on after a block copied from another stream with its header.
	void			ContinueAfter( const header_t &copied );

	size_t			PendingBytes() const { return pending.size(); }
	// Every byte appended, a keyframe's included.
	uint64_t		StreamBytes() const { return streamBytes + pending.size(); }

private:
	void			Close( bool frameEnd, bool streamEnd, std::vector<job_t *> &closed );

	std::vector<uint8_t>	pending;
	bool					untimed;
	bool					nextReset;
	bool					nextSegmentStart;
	bool					inKeyframe;
	bool					streamEnded;		// a block copied in already ended the stream
	uint32_t				segmentsBegun;
	uint32_t				startFrame;
	uint32_t				frameCount;
	uint64_t				startTimeUS;
	uint64_t				durationUS;
	uint64_t				streamBytes;
};

// Where a reader's bytes come from.
class source_t {
public:
	virtual			~source_t() {}
	virtual bool	Seek( uint64_t offset ) = 0;
	// Reads up to bytes at the current position; fewer only at the end.
	virtual size_t	Read( void *buffer, size_t bytes ) = 0;
};

// A block a reader can be sent to: the first of a level or of a keyframe.
struct entry_t {
	uint64_t	offset;			// of the block
	uint32_t	startFrame;		// complete frames before it
	uint64_t	startTimeUS;	// where the last frame before it lies on the timeline
	uint32_t	segment;		// the level it belongs to
	bool		keyframe;		// false: the level's start
};

struct timeline_t {
	bool		timed;			// false for a stream repacked from an older recording
	bool		cleanEnd;		// stopped cleanly; false for a stream that was cut short
	uint32_t	frames;			// complete frames
	uint64_t	durationUS;		// the last complete frame's time
	uint64_t	validBytes;		// the offset after the last intact block
	uint32_t	blocks;
	uint32_t	segments;		// levels begun
	std::vector<entry_t>	entries;	// in file order
};

// Walks the block headers from offset to length, decoding no payload: what a
// stream holds up to its last whole block, stopped cleanly or not.
void		ReadTimeline( source_t &source, uint64_t offset, uint64_t length, timeline_t &timeline );

/*
===============================================================================

	Reads the stream's blocks in order and hands out their bytes. Every block
	is checked before it is used: the stream ends, with the reason, at the
	first one that is cut short, fails its checksum or does not decode.

===============================================================================
*/
class reader_t {
public:
					reader_t();

	void			Open( source_t *source, uint64_t offset );
	// Continues at a block that must decode with no history: the first of a
	// level, or of a keyframe, whose bytes are then handed out.
	void			SeekToBlock( uint64_t offset );

	// For a copy of the stream: every block is handed out, the keyframes a
	// reader in order would pass by included.
	void			HandOutEveryBlock( bool every ) { everyBlock = every; }

	// Decodes the next block. False at the end of the stream, clean or not.
	bool			NextBlock();
	// Up to bytes of the stream; fewer only at its end.
	size_t			Read( void *buffer, size_t bytes );

	const header_t &		Header() const { return header; }
	const std::vector<uint8_t> &Block() const { return raw; }

	bool			Ended() const { return ended; }
	bool			CleanEnd() const { return cleanEnd; }
	// The stream ended at a block that could not be used.
	bool			Damaged() const { return problem != NULL; }
	const char *	Problem() const { return problem != NULL ? problem : ""; }
	uint64_t		ProblemOffset() const { return problemOffset; }

private:
	bool			Fail( uint64_t offset, const char *reason );

	source_t *				source;
	uint64_t				next;			// offset of the next block's header
	uint64_t				sourceOffset;	// where the source stands, or ~0 when unknown
	decoder_t				decoder;
	header_t				header;
	std::vector<uint8_t>	raw;
	size_t					cursor;
	std::vector<uint8_t>	packed;
	bool					needReset;
	bool					enteredKeyframe;	// sent to the keyframe being read: hand it out
	bool					everyBlock;
	bool					lastBlock;
	bool					ended;
	bool					cleanEnd;
	const char *			problem;
	uint64_t				problemOffset;
};

}	// namespace idDemoBlock

#endif /* !__DEMO_BLOCK_CODEC_H__ */
