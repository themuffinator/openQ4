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

// The records a DS_SESSION token introduces. Every record is a dictionary.
typedef enum {
	DSC_HEADER = 1,		// once, after DS_VERSION: game directory, campaign, mode, tic rate
	DSC_SEGMENT,		// a level (map) begins; both sides then reset the hash-string table
	DSC_SEGMENT_READY,	// the segment's world and sound state are complete
	DSC_END				// a clean stop; nothing after it is part of the stream
} demoSessionRecord_t;

// A raw summary appended after the compressed stream of a cleanly stopped
// recording, so the demo library can describe a long campaign demo without
// decoding it: payload | int payloadLength | uint crc32( payload ) | magic.
#define DEMO_TRAILER_MAGIC			"OQ4DSUM1"
const int DEMO_TRAILER_MAGIC_LENGTH	= 8;
const int DEMO_TRAILER_MAX_PAYLOAD	= 256 * 1024;

class idDemoFile : public idFile {
public:
					idDemoFile();
					~idDemoFile();

	const char *	GetName( void ) { return (f?f->GetName():""); }
	const char *	GetFullPath( void ) { return (f?f->GetFullPath():""); }

	void			SetLog( bool b, const char *p );
	void			Log( const char *p );
	bool			OpenForReading( const char *fileName, bool allowPreload = true, bool quiet = false );
	bool			OpenForWriting( const char *fileName );
	// Virtual for renderer modules, which must be able to invalidate a corrupt
	// stream without linking directly against the engine implementation.
	virtual void	Close();
	bool			IsOpen() const { return compressor != NULL; }

	// virtual so the renderer module reaches them across the DLL boundary
	// (Phase B8, docs/dev/archive/2026-10-18/plans/2026-07-16-vulkan-renderer-phase-b.md)
	virtual const char *	ReadHashString();
	virtual void			WriteHashString( const char *str );

	void			ReadDict( idDict &dict );
	void			WriteDict( const idDict &dict );

	// Starts a new hash-string table. A campaign segment does this on both
	// sides, so each level's table is bounded and depends on nothing before it.
	void			ResetHashStrings();

	// Ends the compressed stream, appends the raw trailer (see
	// DEMO_TRAILER_MAGIC) and closes the file. False if the stream was not
	// being written or the trailer could not be written.
	bool			CloseWithTrailer( const idDict &summary );
	// Reads a closed recording's trailer without decoding its stream. False
	// when the file has no intact trailer, as an interrupted recording has not.
	static bool		ReadTrailer( const char *fileName, idDict &summary );

	int				Read( void *buffer, int len );
	int				Write( const void *buffer, int len );

private:
	static idCompressor *AllocCompressor( int type );

	bool			writing;
	byte *			fileImage;
	idFile *		f;
	idCompressor *	compressor;

	idList<idStr*>	demoStrings;
	idHashIndex		demoStringHash;		// writer lookup into demoStrings
	idFile *		fLog;
	bool			log;
	idStr			logStr;

	static idCVar	com_logDemos;
	static idCVar	com_compressDemos;
	static idCVar	com_preloadDemos;
};

#endif /* !__DEMOFILE_H__ */
