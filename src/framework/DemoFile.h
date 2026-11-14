/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __DEMOFILE_H__
#define __DEMOFILE_H__

/*
===============================================================================

	Demo file

===============================================================================
*/

typedef enum {
	DS_FINISHED,
	DS_RENDER,
	DS_SOUND,
	DS_VERSION,
	// openQ4 render demo v11+: session records owned by the engine session,
	// which frame the campaign segments of a recording (Session_demo.cpp).
	DS_SESSION
} demoSystem_t;

// The records a DS_SESSION token introduces. Every record is a dictionary,
// except DSC_FRAME_TIME, which one int follows.
typedef enum {
	DSC_HEADER = 1,		// once, after DS_VERSION: game directory, campaign, mode, tic rate
	DSC_SEGMENT,		// a level (map) begins; both sides then reset the hash-string table
	DSC_SEGMENT_READY,	// the segment's world and sound state are complete
	DSC_END,			// a clean stop; nothing after it is part of the stream
	DSC_FRAME_TIME,		// v13: the microseconds since the previous recorded frame,
						// written just before the frame's DC_END_FRAME
	DSC_RESYNC			// v14: a keyframe was written just before; both sides start
						// the hash-string table again with this record
} demoSessionRecord_t;

// A raw summary appended after the stream of a cleanly stopped recording, so
// the demo library can describe a long campaign demo without decoding it:
// payload | int payloadLength | uint crc32( payload ) | magic.
#define DEMO_TRAILER_MAGIC			"OQ4DSUM1"
const int DEMO_TRAILER_MAGIC_LENGTH	= 8;
const int DEMO_TRAILER_MAX_PAYLOAD	= 256 * 1024;

// How a demo's stream is carried, the int after the file's magic. 0 to 3 are
// the idCompressor streams every recording used before render demo v14; they
// still play. A recording now writes the block container (DemoBlockCodec.h):
// checked blocks that place themselves on the timeline.
const int DEMO_SCHEME_NONE		= 0;
const int DEMO_SCHEME_LZW		= 1;
const int DEMO_SCHEME_LZSS		= 2;
const int DEMO_SCHEME_HUFFMAN	= 3;
const int DEMO_SCHEME_BLOCKS	= 4;

// A block of a block-container demo that playback can be sent to: where a
// level segment begins, or a keyframe inside one.
struct demoTimelineEntry_t {
	int64			fileOffset;		// of the block
	int				startFrame;		// complete frames before it
	int64			startTimeUS;	// where the last frame before it lies on the timeline
	int				level;			// the level segment it belongs to
	bool			keyframe;		// false: the level's start
};

// A block-container demo as its block headers alone describe it, whether or
// not the recording was stopped cleanly.
struct demoTimeline_t {
	bool			timed;			// false for a stream repacked from an older recording
	bool			cleanEnd;		// stopped cleanly; false for a recording cut short
	int				frames;			// complete frames
	int64			durationUS;		// the last complete frame's time
	int64			validBytes;		// the file through its last intact block
	int				blocks;
	int				levels;			// level segments begun
	idList<demoTimelineEntry_t>	entries;	// in file order
};

class idDemoFile : public idFile {
public:
					idDemoFile();
					~idDemoFile();

	const char *	GetName( void ) { return name.c_str(); }
	const char *	GetFullPath( void ) { return fullPath.c_str(); }

	void			SetLog( bool b, const char *p );
	void			Log( const char *p );
	bool			OpenForReading( const char *fileName, bool allowPreload = true, bool quiet = false );
	// A recording: the block container, with a timeline the caller supplies
	// through BeginSegment and EndFrame. com_compressDemos 0 stores its blocks
	// unpacked.
	bool			OpenForWriting( const char *fileName );
	// A copy of another demo's stream in the given scheme. A block container
	// written this way has no timeline unless CopyBlocksFrom supplies one.
	bool			OpenForRepacking( const char *fileName, int scheme );
	// Virtual for renderer modules, which must be able to invalidate a corrupt
	// stream without linking directly against the engine implementation.
	virtual void	Close();
	bool			IsOpen() const { return compressor != NULL || blocks != NULL; }

	// virtual so the renderer module reaches them across the DLL boundary
	// (Phase B8, docs/dev/archive/2026-10-18/plans/2026-07-16-vulkan-renderer-phase-b.md)
	virtual const char *	ReadHashString();
	virtual void			WriteHashString( const char *str );

	void			ReadDict( idDict &dict );
	void			WriteDict( const idDict &dict );

	// Starts a new hash-string table. A campaign segment does this on both
	// sides, so each level's table is bounded and depends on nothing before it.
	void			ResetHashStrings();

	// Ends the stream, appends the raw trailer (see DEMO_TRAILER_MAGIC) and
	// closes the file. False if the stream was not being written, a block
	// could not be written or the trailer could not be.
	bool			CloseWithTrailer( const idDict &summary );
	// Reads a closed recording's trailer without decoding its stream. False
	// when the file has no intact trailer, as an interrupted recording has not.
	static bool		ReadTrailer( const char *fileName, idDict &summary );
	// A demo file's size and time stamp. The size is right past 2 GiB, where
	// idFileSystem::ReadFile's int is not. False when the file cannot be read.
	static bool		FileInfo( const char *fileName, int64 &bytes, ID_TIME_T &timestamp );

	int				Read( void *buffer, int len );
	int				Write( const void *buffer, int len );

	// --- the block container (render demo v14) ---

	bool			IsBlockStream() const { return blocks != NULL; }

	// Recording. The bytes written next begin a level segment: the open block
	// ends, the next one decodes with nothing before it, and the hash-string
	// table starts again.
	void			BeginSegment();
	// Recording. The frame just written ended and adds deltaUS to the timeline.
	void			EndFrame( int deltaUS );
	// Recording, between two frames. What is written until EndKeyframe is a
	// keyframe: a second way into the level, with its own hash-string table.
	// Playback in order skips it; the table starts again after it.
	void			BeginKeyframe();
	void			EndKeyframe();
	// Recording. True once a block could not be written; the file then holds
	// every block written before it and nothing more is recorded.
	bool			WriteFailed() const;
	const char *	WriteError() const;
	// The file so far and the stream it carries; after the recording closed,
	// what they came to.
	int64			BytesWritten() const;
	int64			StreamBytesWritten() const;

	// Copies every block of source, a block container open for reading from
	// its start, keeping its timeline. False when source ended damaged.
	bool			CopyBlocksFrom( idDemoFile &source );

	// Walks a block container's headers. False when the file is not one.
	static bool		ReadTimeline( const char *fileName, demoTimeline_t &timeline );
	// Playback. Continues reading at a level's first block or at a keyframe;
	// the caller resets what it counts to that point. False when it cannot.
	bool			SeekToEntry( const demoTimelineEntry_t &entry );
	// Playback. Where the stream stopped at a block that could not be used
	// (its offset in the file), or -1.
	int64			DamagedAt() const;
	// Playback. Whether the stream ended at a clean stop rather than a cut.
	bool			ReachedCleanEnd() const;

private:
	static idCompressor *AllocCompressor( int type );

	bool			writing;
	byte *			fileImage;
	idFile *		f;
	idCompressor *	compressor;
	struct demoBlockState_t *blocks;	// the block container's reader or writer
	int64			closedBytes;		// what the last recording closed here came to
	int64			closedStreamBytes;

	idStr			name;
	idStr			fullPath;

	idList<idStr*>	demoStrings;
	idHashIndex		demoStringHash;		// writer lookup into demoStrings
	idFile *		fLog;
	bool			log;
	idStr			logStr;

	static idCVar	com_logDemos;
	static idCVar	com_compressDemos;
	static idCVar	com_preloadDemos;
	static idCVar	demo_recordThread;
};

#endif /* !__DEMOFILE_H__ */
