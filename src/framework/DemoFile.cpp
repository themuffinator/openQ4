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

#include "DemoBlockCodec.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <stdio.h>
#include <string.h>

idCVar idDemoFile::com_logDemos( "com_logDemos", "0", CVAR_SYSTEM | CVAR_BOOL, "Write demo.log with debug information in it" );
idCVar idDemoFile::com_compressDemos( "com_compressDemos", "1", CVAR_SYSTEM | CVAR_INTEGER | CVAR_ARCHIVE, "How a recording's blocks are written\n0: stored (large files, for tools that read the stream)\n1: packed (long-range matches, then DEFLATE)\nSee also: the 'compressDemo' command, which repacks a demo" );
idCVar idDemoFile::com_preloadDemos( "com_preloadDemos", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_ARCHIVE, "Load the whole demo in to RAM before running it" );
idCVar idDemoFile::demo_recordThread( "demo_recordThread", "1", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT, "Pack a recording's blocks on a worker thread, so recording costs the game no frame time" );

#define DEMO_MAGIC GAME_NAME " RDEMO"
#define DEMO_MAGIC_HISTORICAL "OpenQ4 RDEMO"
#define DEMO_MAGIC_QUAKE4 "Quake4 RDEMO"
#define MAX_DEMO_HASH_STRINGS 65536
#define MAX_DEMO_DICT_ENTRIES 4096

static const int DEMO_MAGIC_LENGTH = sizeof( DEMO_MAGIC ) / sizeof( DEMO_MAGIC[0] );
static const int DEMO_WRAPPER_BYTES = DEMO_MAGIC_LENGTH + static_cast<int>( sizeof( int ) );

// A recording closes its open block at the end of the first frame that fills
// it (idDemoBlock::TARGET_RAW_BYTES), or that ends this long after the block
// before it, so a crash costs about half a second of recording.
static const int DEMO_BLOCK_FLUSH_MSEC = 500;
// The worker falls this far behind before the game waits for it.
static const size_t DEMO_BLOCK_MAX_QUEUED_BYTES = 8 * 1024 * 1024;
static const int DEMO_BLOCK_PACK_LEVEL = 6;

/*
===============================================================================

	A demo's bytes at 64-bit offsets.

	idFile carries int lengths and long seeks, which end at 2 GiB wherever long
	is 32 bits, and a long recording passes that. A loose file is therefore
	also opened directly. A file inside a pack, or one preloaded into memory,
	is addressed through its idFile and is small enough for that.

===============================================================================
*/

static bool Demo_Seek64( FILE *file, int64 offset ) {
#if defined( _WIN32 )
	return _fseeki64( file, offset, SEEK_SET ) == 0;
#else
	return fseeko( file, static_cast<off_t>( offset ), SEEK_SET ) == 0;
#endif
}

static int64 Demo_Length64( FILE *file ) {
#if defined( _WIN32 )
	if ( _fseeki64( file, 0, SEEK_END ) != 0 ) {
		return -1;
	}
	const int64 length = _ftelli64( file );
#else
	if ( fseeko( file, 0, SEEK_END ) != 0 ) {
		return -1;
	}
	const int64 length = static_cast<int64>( ftello( file ) );
#endif
	return Demo_Seek64( file, 0 ) ? length : -1;
}

struct demoBytes_t {
	idFile *	file;		// reads the demo when it is not a loose file
	FILE *		direct;		// the loose file itself
	int64		length;
	int64		position;	// -1 when not known

	demoBytes_t() : file( NULL ), direct( NULL ), length( 0 ), position( -1 ) {}

	// Looks at an idFile the file system opened for reading, from its start.
	void Attach( idFile *source ) {
		file = source;
		direct = NULL;
		length = source != NULL ? source->Length() : 0;
		position = -1;
		if ( source == NULL ) {
			return;
		}
		FILE *loose = fopen( source->GetFullPath(), "rb" );
		if ( loose == NULL ) {
			return;
		}
		// The same file: its length agrees wherever idFile can express it.
		const int64 looseLength = Demo_Length64( loose );
		if ( looseLength > 0 && ( looseLength > idMath::INT_MAX || looseLength == length ) ) {
			direct = loose;
			length = looseLength;
			position = 0;
		} else {
			fclose( loose );
		}
	}

	void CloseDirect() {
		if ( direct != NULL ) {
			fclose( direct );
			direct = NULL;
		}
		position = -1;
	}

	bool Seek( int64 offset ) {
		if ( offset < 0 ) {
			return false;
		}
		if ( offset == position ) {
			return true;
		}
		position = -1;
		if ( direct != NULL ) {
			if ( !Demo_Seek64( direct, offset ) ) {
				return false;
			}
		} else if ( file == NULL || offset > idMath::INT_MAX ||
					file->Seek( static_cast<long>( offset ), FS_SEEK_SET ) != 0 ) {
			return false;
		}
		position = offset;
		return true;
	}

	int Read( void *buffer, int bytes ) {
		if ( bytes <= 0 ) {
			return 0;
		}
		int read = 0;
		if ( direct != NULL ) {
			read = static_cast<int>( fread( buffer, 1, static_cast<size_t>( bytes ), direct ) );
		} else if ( file != NULL ) {
			read = file->Read( buffer, bytes );
		}
		if ( read != bytes ) {
			position = -1;
		} else if ( position >= 0 ) {
			position += read;
		}
		return read < 0 ? 0 : read;
	}
};

/*
===============================================================================

	Block container, reading

	idDemoBlock::reader_t checks and decodes the blocks; this supplies the
	file's bytes and says, once, why a stream stopped early.

===============================================================================
*/

struct demoBlockReader_t : public idDemoBlock::source_t {
	demoBytes_t				bytes;
	idDemoBlock::reader_t	stream;
	bool					quiet;
	bool					reported;
	idStr					name;

	demoBlockReader_t() : quiet( false ), reported( false ) {}

	virtual bool Seek( uint64_t offset ) {
		return offset <= static_cast<uint64_t>( 0x7FFFFFFFFFFFFFFFLL ) && bytes.Seek( static_cast<int64>( offset ) );
	}

	virtual size_t Read( void *buffer, size_t count ) {
		size_t done = 0;
		while ( done < count ) {
			const int want = static_cast<int>( Min( count - done, static_cast<size_t>( 1 << 30 ) ) );
			const int got = bytes.Read( static_cast<byte *>( buffer ) + done, want );
			done += static_cast<size_t>( got );
			if ( got != want ) {
				break;
			}
		}
		return done;
	}

	void Report() {
		if ( !stream.Damaged() || reported ) {
			return;
		}
		reported = true;
		if ( !quiet ) {
			common->Warning( "Render demo '%s' is damaged at byte %lld: %s",
				name.c_str(), static_cast<long long>( stream.ProblemOffset() ), stream.Problem() );
		}
	}

	int ReadStream( void *buffer, int count ) {
		const int read = static_cast<int>( stream.Read( buffer, static_cast<size_t>( count ) ) );
		if ( read != count ) {
			Report();
		}
		return read;
	}
};

/*
===============================================================================

	Block container, writing

	The game thread fills blocks (idDemoBlock::builder_t) and, as each closes,
	hands it to a worker that packs it; the game thread writes the packed
	blocks to the file in order as they come back. Only the game thread
	touches the file.

===============================================================================
*/

struct demoBlockWriter_t {
	FILE *					file;
	int						level;
	idDemoBlock::builder_t	builder;
	std::vector<idDemoBlock::job_t *>	closed;		// blocks the builder just closed
	int						lastCloseMsec;

	// the worker
	bool					threaded;
	std::thread				worker;
	std::mutex				mutex;
	std::condition_variable	wake;
	std::condition_variable	idle;
	std::deque<idDemoBlock::job_t *>	input;
	std::deque<idDemoBlock::job_t *>	output;
	size_t					inputBytes;
	bool					working;
	bool					quit;
	idDemoBlock::encoder_t	encoder;

	// the outcome
	bool					failed;
	idStr					error;
	int64					fileBytes;

	demoBlockWriter_t() : file( NULL ), level( DEMO_BLOCK_PACK_LEVEL ), lastCloseMsec( 0 ), threaded( false ),
		inputBytes( 0 ), working( false ), quit( false ), failed( false ), fileBytes( 0 ) {
	}

	void Run() {
		std::unique_lock<std::mutex> lock( mutex );
		for ( ;; ) {
			while ( input.empty() && !quit ) {
				wake.wait( lock );
			}
			if ( input.empty() ) {
				return;
			}
			idDemoBlock::job_t *job = input.front();
			input.pop_front();
			inputBytes -= job->raw.size();
			working = true;
			lock.unlock();
			idDemoBlock::PackJob( encoder, level, *job );
			lock.lock();
			working = false;
			output.push_back( job );
			idle.notify_all();
		}
	}

	void StartWorker() {
		try {
			worker = std::thread( &demoBlockWriter_t::Run, this );
			threaded = true;
		} catch ( ... ) {
			// no thread to be had: pack as the blocks close instead
			threaded = false;
		}
	}

	void Fail( const char *reason ) {
		if ( !failed ) {
			failed = true;
			error = reason;
		}
	}

	// Writes one packed block. Nothing more is written after a failure, so the
	// file ends with the last block that went out whole or with a cut one,
	// which a reader rejects by its header or checksum.
	void WriteJob( idDemoBlock::job_t *job ) {
		if ( !failed ) {
			uint8_t head[idDemoBlock::HEADER_BYTES];
			idDemoBlock::WriteHeader( job->header, head );
			if ( fwrite( head, 1, sizeof( head ), file ) != sizeof( head ) ||
				 ( !job->packed.empty() && fwrite( job->packed.data(), 1, job->packed.size(), file ) != job->packed.size() ) ) {
				Fail( "a block could not be written; the disk may be full" );
			} else {
				fileBytes += static_cast<int64>( sizeof( head ) + job->packed.size() );
			}
		}
		delete job;
	}

	void Flush() {
		// what the process has written survives it from here
		if ( !failed && fflush( file ) != 0 ) {
			Fail( "the file could not be flushed; the disk may be full" );
		}
	}

	// Writes the blocks the worker has finished; all of them when waiting.
	void WriteCompleted( bool wait ) {
		std::deque<idDemoBlock::job_t *> ready;
		if ( threaded ) {
			std::unique_lock<std::mutex> lock( mutex );
			if ( wait ) {
				while ( !input.empty() || working ) {
					idle.wait( lock );
				}
			}
			ready.swap( output );
		}
		if ( ready.empty() ) {
			return;
		}
		while ( !ready.empty() ) {
			WriteJob( ready.front() );
			ready.pop_front();
		}
		Flush();
	}

	void SubmitJob( idDemoBlock::job_t *job ) {
		if ( failed ) {
			delete job;
			return;
		}
		if ( !threaded ) {
			idDemoBlock::PackJob( encoder, level, *job );
			WriteJob( job );
			Flush();
			return;
		}
		bool behind = false;
		{
			std::lock_guard<std::mutex> lock( mutex );
			inputBytes += job->raw.size();
			input.push_back( job );
			behind = inputBytes > DEMO_BLOCK_MAX_QUEUED_BYTES;
		}
		wake.notify_one();
		// the game waits only when the worker has fallen far behind
		WriteCompleted( behind );
	}

	// Hands on the blocks the builder closed.
	void Dispatch() {
		if ( closed.empty() ) {
			return;
		}
		lastCloseMsec = Sys_Milliseconds();
		for ( size_t i = 0; i < closed.size(); i++ ) {
			SubmitJob( closed[i] );
		}
		closed.clear();
	}

	void Append( const void *buffer, int length ) {
		if ( failed ) {
			return;
		}
		builder.Append( buffer, static_cast<size_t>( length ), closed );
		Dispatch();
	}

	void EndFrame( int deltaUS ) {
		const bool overdue = Sys_Milliseconds() - lastCloseMsec >= DEMO_BLOCK_FLUSH_MSEC;
		builder.EndFrame( static_cast<uint64_t>( Max( deltaUS, 0 ) ), overdue, closed );
		if ( closed.empty() ) {
			WriteCompleted( false );
		} else {
			Dispatch();
		}
	}

	void BeginSegment() {
		builder.BeginSegment( closed );
		Dispatch();
	}

	void BeginKeyframe() {
		builder.BeginKeyframe( closed );
		Dispatch();
	}

	void EndKeyframe() {
		builder.EndKeyframe( closed );
		Dispatch();
	}

	// Ends the stream: every block reaches the file and the worker stops.
	void Finish( bool streamEnd ) {
		builder.Finish( streamEnd, closed );
		Dispatch();
		WriteCompleted( true );
		if ( threaded ) {
			{
				std::lock_guard<std::mutex> lock( mutex );
				quit = true;
			}
			wake.notify_one();
			worker.join();
			threaded = false;
		}
	}
};

struct demoBlockState_t {
	demoBlockReader_t *	reader;
	demoBlockWriter_t *	writer;

	demoBlockState_t() : reader( NULL ), writer( NULL ) {}
	~demoBlockState_t() {
		if ( reader != NULL ) {
			reader->bytes.CloseDirect();
			delete reader;
		}
		if ( writer != NULL ) {
			if ( writer->file != NULL ) {
				fclose( writer->file );
			}
			delete writer;
		}
	}
};

/*
================
idDemoFile::idDemoFile
================
*/
idDemoFile::idDemoFile() {
	f = NULL;
	fLog = NULL;
	log = false;
	fileImage = NULL;
	compressor = NULL;
	blocks = NULL;
	writing = false;
	retailRenderStream = false;
	closedBytes = 0;
	closedStreamBytes = 0;
}

/*
================
idDemoFile::~idDemoFile
================
*/
idDemoFile::~idDemoFile() {
	Close();
}

/*
================
idDemoFile::AllocCompressor
================
*/
idCompressor *idDemoFile::AllocCompressor( int type ) {
	switch ( type ) {
	case DEMO_SCHEME_NONE: return idCompressor::AllocNoCompression();
	case DEMO_SCHEME_LZW: return idCompressor::AllocLZW();
	case DEMO_SCHEME_LZSS: return idCompressor::AllocLZSS();
	case DEMO_SCHEME_HUFFMAN: return idCompressor::AllocHuffman();
	default: return NULL;
	}
}

/*
================
idDemoFile::OpenForReading
================
*/
bool idDemoFile::OpenForReading( const char *fileName, bool allowPreload, bool quiet ) {
	char magicBuffer[DEMO_MAGIC_LENGTH];
	int compression;
	bool ambiguousQuake4Wrapper = false;

	Close();
	retailRenderStream = false;

	f = fileSystem->OpenFileRead( fileName );
	if ( !f ) {
		return false;
	}
	name = f->GetName();
	fullPath = f->GetFullPath();

	// a recording can pass the 2 GiB an idFile can measure
	demoBytes_t bytes;
	bytes.Attach( f );
	if ( bytes.length <= 0 ) {
		bytes.CloseDirect();
		Close();
		return false;
	}

	if ( allowPreload && com_preloadDemos.GetBool() && bytes.length <= 512 * 1024 * 1024 ) {
		const int fileLength = static_cast<int>( bytes.length );
		bytes.CloseDirect();
		fileImage = (byte *)Mem_Alloc( fileLength );
		if ( f->Read( fileImage, fileLength ) != fileLength ) {
			Close();
			return false;
		}
		fileSystem->CloseFile( f );
		f = new idFile_Memory( va( "preloaded(%s)", fileName ), (const char *)fileImage, fileLength );
		bytes.Attach( f );
	}

	if ( com_logDemos.GetBool() ) {
		fLog = fileSystem->OpenFileWrite( "demoread.log" );
	}

	writing = false;

	if ( f->Read( magicBuffer, DEMO_MAGIC_LENGTH ) != DEMO_MAGIC_LENGTH ) {
		bytes.CloseDirect();
		Close();
		return false;
	}
	if ( memcmp( magicBuffer, DEMO_MAGIC, DEMO_MAGIC_LENGTH ) == 0 ||
		 memcmp( magicBuffer, DEMO_MAGIC_HISTORICAL, DEMO_MAGIC_LENGTH ) == 0 ||
		 memcmp( magicBuffer, DEMO_MAGIC_QUAKE4, DEMO_MAGIC_LENGTH ) == 0 ) {
		ambiguousQuake4Wrapper =
			memcmp( magicBuffer, DEMO_MAGIC_QUAKE4, DEMO_MAGIC_LENGTH ) == 0;
		if ( f->ReadInt( compression ) != sizeof( compression ) ) {
			bytes.CloseDirect();
			Close();
			return false;
		}
	} else {
		// Ideally we would error out if the magic string isn't there,
		// but for backwards compatibility we are going to assume it's just an uncompressed demo file
		compression = 0;
		f->Rewind();
	}

	if ( compression == DEMO_SCHEME_BLOCKS && !ambiguousQuake4Wrapper ) {
		if ( bytes.direct == NULL ) {
			// read through the idFile, which stands just past the wrapper: the
			// first block needs no seek, which a file inside a pack does slowly
			bytes.position = DEMO_WRAPPER_BYTES;
		}
		blocks = new demoBlockState_t;
		blocks->reader = new demoBlockReader_t;
		blocks->reader->bytes = bytes;
		blocks->reader->quiet = quiet;
		blocks->reader->name = fileName;
		blocks->reader->stream.Open( blocks->reader, DEMO_WRAPPER_BYTES );
		return true;
	}
	bytes.CloseDirect();

	compressor = AllocCompressor( compression );
	if ( compressor == NULL ) {
		Close();
		return false;
	}
	compressor->Init( f, false, 8 );

	// The earliest openQ4 builds and retail Quake 4 used the same wrapper
	// spelling. Probe the decoded bytes before accepting it: an openQ4 stream
	// begins with the 32-bit DS_VERSION token and a stream version, retail's
	// with the byte DS_VERSION, its 32-bit savegame version, then the byte
	// DS_SOUND and SCMD_STATE (0) that every retail recording starts with. The
	// first four bytes read as the int DS_VERSION in both, so only the stream
	// version tells them apart. Reinitialize the decompressor afterward so
	// callers still see the stream from byte zero.
	if ( ambiguousQuake4Wrapper ) {
		byte head[8] = {};
		const int headBytes = Read( head, sizeof( head ) );
		const int firstToken = head[0] | ( head[1] << 8 ) | ( head[2] << 16 ) | ( head[3] << 24 );
		const int streamVersion = head[4] | ( head[5] << 8 ) | ( head[6] << 16 ) | ( head[7] << 24 );
		const bool openQ4Stream = headBytes == static_cast<int>( sizeof( head ) ) &&
			firstToken == DS_VERSION && streamVersion >= 1 && streamVersion <= OPENQ4_RENDERDEMO_CURRENT_VERSION;
		const bool retailStream = !openQ4Stream && headBytes >= 7 &&
			head[0] == DS_VERSION && head[5] == DS_SOUND && head[6] == 0;
		if ( !openQ4Stream && !retailStream ) {
			Close();
			if ( !quiet ) {
				common->Warning( "Demo '%s' is not a render demo this build recognizes", fileName );
			}
			return false;
		}
		retailRenderStream = retailStream;
		delete compressor;
		compressor = NULL;
		if ( f->Seek( DEMO_WRAPPER_BYTES, FS_SEEK_SET ) != 0 ) {
			Close();
			return false;
		}
		compressor = AllocCompressor( compression );
		if ( compressor == NULL ) {
			Close();
			return false;
		}
		compressor->Init( f, false, 8 );
	}

	return true;
}

/*
================
idDemoFile::SetLog
================
*/
void idDemoFile::SetLog(bool b, const char *p) {
	log = b;
	if (p) {
		logStr = p;
	}
}

/*
================
idDemoFile::Log
================
*/
void idDemoFile::Log(const char *p) {
	if ( fLog && p && *p ) {
		fLog->Write( p, idLib::SizeToInt( strlen( p ), "idDemoFile::Log" ) );
	}
}

/*
================
idDemoFile::OpenForWriting

A recording. The file system creates the file and its directories; the blocks
then go to the file directly, which has no 2 GiB limit to run into.
================
*/
bool idDemoFile::OpenForWriting( const char *fileName ) {
	if ( !OpenForRepacking( fileName, DEMO_SCHEME_BLOCKS ) ) {
		return false;
	}
	// the caller places its frames and levels on the timeline
	blocks->writer->builder.SetUntimed( false );
	return true;
}

/*
================
idDemoFile::OpenForRepacking
================
*/
bool idDemoFile::OpenForRepacking( const char *fileName, int scheme ) {
	Close();

	f = fileSystem->OpenFileWrite( fileName );
	if ( f == NULL ) {
		return false;
	}
	name = f->GetName();
	fullPath = f->GetFullPath();

	if ( com_logDemos.GetBool() ) {
		fLog = fileSystem->OpenFileWrite( "demowrite.log" );
	}

	writing = true;

	if ( scheme != DEMO_SCHEME_BLOCKS ) {
		f->Write( DEMO_MAGIC, DEMO_MAGIC_LENGTH );
		compressor = AllocCompressor( scheme );
		if ( compressor == NULL ) {
			Close();
			return false;
		}
		f->WriteInt( scheme );
		f->Flush();
		compressor->Init( f, true, 8 );
		return true;
	}

	fileSystem->CloseFile( f );
	f = NULL;
	FILE *file = fopen( fullPath.c_str(), "wb" );
	if ( file == NULL ) {
		Close();
		return false;
	}
	byte wrapper[DEMO_WRAPPER_BYTES];
	memcpy( wrapper, DEMO_MAGIC, DEMO_MAGIC_LENGTH );
	wrapper[DEMO_MAGIC_LENGTH + 0] = static_cast<byte>( DEMO_SCHEME_BLOCKS );
	wrapper[DEMO_MAGIC_LENGTH + 1] = 0;
	wrapper[DEMO_MAGIC_LENGTH + 2] = 0;
	wrapper[DEMO_MAGIC_LENGTH + 3] = 0;
	if ( fwrite( wrapper, 1, sizeof( wrapper ), file ) != sizeof( wrapper ) || fflush( file ) != 0 ) {
		fclose( file );
		Close();
		return false;
	}

	blocks = new demoBlockState_t;
	blocks->writer = new demoBlockWriter_t;
	demoBlockWriter_t &writer = *blocks->writer;
	writer.file = file;
	writer.level = com_compressDemos.GetInteger() > 0 ? DEMO_BLOCK_PACK_LEVEL : 0;
	writer.builder.SetUntimed( true );
	writer.fileBytes = sizeof( wrapper );
	writer.lastCloseMsec = Sys_Milliseconds();
	if ( demo_recordThread.GetBool() ) {
		writer.StartWorker();
	}
	return true;
}

/*
================
idDemoFile::Close
================
*/
void idDemoFile::Close() {
	if ( writing && compressor ) {
		compressor->FinishCompress();
	}
	if ( blocks != NULL ) {
		if ( blocks->writer != NULL ) {
			// a stream left without its clean end: every block still reaches the file
			blocks->writer->Finish( false );
			closedBytes = blocks->writer->fileBytes;
			closedStreamBytes = static_cast<int64>( blocks->writer->builder.StreamBytes() );
		}
		delete blocks;
		blocks = NULL;
	}

	if ( f ) {
		fileSystem->CloseFile( f );
		f = NULL;
	}
	if ( fLog ) {
		fileSystem->CloseFile( fLog );
		fLog = NULL;
	}
	if ( fileImage ) {
		Mem_Free( fileImage );
		fileImage = NULL;
	}
	if ( compressor ) {
		delete compressor;
		compressor = NULL;
	}
	writing = false;

	ResetHashStrings();
}

/*
================
idDemoFile::ResetHashStrings
================
*/
void idDemoFile::ResetHashStrings() {
	demoStrings.DeleteContents( true );
	demoStringHash.Clear();
}

/*
================
Demo_BuildTrailer

payload | int payloadLength | uint crc32( payload ) | magic, little endian.
================
*/
static bool Demo_BuildTrailer( const idDict &summary, idList<byte> &trailer ) {
	idFile_Memory payload( "demoTrailer" );
	const int count = Min( summary.GetNumKeyVals(), MAX_DEMO_DICT_ENTRIES );
	payload.WriteInt( count );
	for ( int i = 0; i < count; i++ ) {
		const idKeyValue *kv = summary.GetKeyVal( i );
		payload.WriteString( kv->GetKey() );
		payload.WriteString( kv->GetValue() );
	}
	const int length = payload.Length();
	if ( length <= 0 || length > DEMO_TRAILER_MAX_PAYLOAD ) {
		return false;
	}
	const unsigned int crc = CRC32_BlockChecksum( payload.GetDataPtr(), length );
	trailer.SetNum( length + 8 + DEMO_TRAILER_MAGIC_LENGTH );
	byte *out = trailer.Ptr();
	memcpy( out, payload.GetDataPtr(), length );
	out += length;
	for ( int shift = 0; shift < 32; shift += 8 ) {
		*out++ = static_cast<byte>( static_cast<unsigned int>( length ) >> shift );
	}
	for ( int shift = 0; shift < 32; shift += 8 ) {
		*out++ = static_cast<byte>( crc >> shift );
	}
	memcpy( out, DEMO_TRAILER_MAGIC, DEMO_TRAILER_MAGIC_LENGTH );
	return true;
}

/*
================
idDemoFile::CloseWithTrailer

The trailer lies outside the stream, so it is written raw after the last
block (or after the compressor has flushed). Readers stop at the block that
ends the stream, or at its DSC_END record, and never decode these bytes.
================
*/
bool idDemoFile::CloseWithTrailer( const idDict &summary ) {
	idList<byte> trailer;
	const bool built = Demo_BuildTrailer( summary, trailer );

	if ( writing && blocks != NULL && blocks->writer != NULL ) {
		demoBlockWriter_t &writer = *blocks->writer;
		writer.Finish( true );
		bool written = !writer.failed && built;
		if ( written ) {
			written = fwrite( trailer.Ptr(), 1, trailer.Num(), writer.file ) == static_cast<size_t>( trailer.Num() ) &&
				fflush( writer.file ) == 0;
			if ( written ) {
				writer.fileBytes += trailer.Num();
			}
		}
		written = fclose( writer.file ) == 0 && written;
		writer.file = NULL;
		Close();
		return written;
	}

	if ( !writing || compressor == NULL || f == NULL ) {
		Close();
		return false;
	}
	compressor->FinishCompress();
	delete compressor;
	compressor = NULL;

	const bool written = built && f->Write( trailer.Ptr(), trailer.Num() ) == trailer.Num();
	writing = false;
	Close();
	return written;
}

/*
================
idDemoFile::ReadTrailer
================
*/
bool idDemoFile::ReadTrailer( const char *fileName, idDict &summary ) {
	summary.Clear();
	idFile *file = fileSystem->OpenFileRead( fileName, false );
	if ( file == NULL ) {
		return false;
	}
	demoBytes_t bytes;
	bytes.Attach( file );
	const int footerBytes = 8 + DEMO_TRAILER_MAGIC_LENGTH;
	const int64 fileLength = bytes.length;
	byte footer[8 + DEMO_TRAILER_MAGIC_LENGTH];
	bool valid = fileLength > footerBytes &&
		bytes.Seek( fileLength - footerBytes ) &&
		bytes.Read( footer, footerBytes ) == footerBytes &&
		memcmp( footer + 8, DEMO_TRAILER_MAGIC, DEMO_TRAILER_MAGIC_LENGTH ) == 0;
	int length = 0;
	unsigned int crc = 0;
	if ( valid ) {
		unsigned int storedLength = 0;
		for ( int i = 3; i >= 0; i-- ) {
			storedLength = ( storedLength << 8 ) | footer[i];
			crc = ( crc << 8 ) | footer[4 + i];
		}
		length = storedLength <= static_cast<unsigned int>( DEMO_TRAILER_MAX_PAYLOAD ) ? static_cast<int>( storedLength ) : 0;
		valid = length > 0 &&
			length <= fileLength - footerBytes &&
			bytes.Seek( fileLength - footerBytes - length );
	}
	idList<byte> payload;
	if ( valid ) {
		payload.SetNum( length );
		valid = bytes.Read( payload.Ptr(), length ) == length &&
			CRC32_BlockChecksum( payload.Ptr(), length ) == crc;
	}
	bytes.CloseDirect();
	fileSystem->CloseFile( file );
	if ( !valid ) {
		return false;
	}

	idFile_Memory reader( "demoTrailer", reinterpret_cast<const char *>( payload.Ptr() ), length );
	int count = 0;
	if ( reader.ReadInt( count ) != static_cast<int>( sizeof( count ) ) ||
		 count < 0 || count > MAX_DEMO_DICT_ENTRIES ) {
		return false;
	}
	for ( int i = 0; i < count; i++ ) {
		idStr key, value;
		if ( reader.ReadString( key ) <= 0 || reader.ReadString( value ) < 0 ) {
			summary.Clear();
			return false;
		}
		summary.Set( key, value );
	}
	return true;
}

/*
================
idDemoFile::FileInfo
================
*/
bool idDemoFile::FileInfo( const char *fileName, int64 &bytes, ID_TIME_T &timestamp ) {
	bytes = 0;
	timestamp = 0;
	idFile *file = fileSystem->OpenFileRead( fileName, false );
	if ( file == NULL ) {
		return false;
	}
	demoBytes_t source;
	source.Attach( file );
	bytes = source.length;
	timestamp = file->Timestamp();
	source.CloseDirect();
	fileSystem->CloseFile( file );
	return bytes >= 0;
}

/*
================
idDemoFile::ReadTimeline

Walks the block headers without decoding a payload: where the levels begin,
how many frames the file holds and how long they last, and whether it was
stopped cleanly. A recording that was cut short is described up to its last
whole block.
================
*/
bool idDemoFile::ReadTimeline( const char *fileName, demoTimeline_t &timeline ) {
	timeline.timed = false;
	timeline.cleanEnd = false;
	timeline.frames = 0;
	timeline.durationUS = 0;
	timeline.validBytes = 0;
	timeline.blocks = 0;
	timeline.levels = 0;
	timeline.entries.Clear();

	idFile *file = fileSystem->OpenFileRead( fileName, false );
	if ( file == NULL ) {
		return false;
	}
	demoBlockReader_t reader;
	reader.bytes.Attach( file );
	byte wrapper[DEMO_WRAPPER_BYTES];
	const bool blockStream = reader.bytes.length > DEMO_WRAPPER_BYTES && reader.bytes.Seek( 0 ) &&
		reader.bytes.Read( wrapper, DEMO_WRAPPER_BYTES ) == DEMO_WRAPPER_BYTES &&
		memcmp( wrapper, DEMO_MAGIC, DEMO_MAGIC_LENGTH ) == 0 &&
		wrapper[DEMO_MAGIC_LENGTH] == DEMO_SCHEME_BLOCKS && wrapper[DEMO_MAGIC_LENGTH + 1] == 0 &&
		wrapper[DEMO_MAGIC_LENGTH + 2] == 0 && wrapper[DEMO_MAGIC_LENGTH + 3] == 0;
	if ( blockStream ) {
		idDemoBlock::timeline_t walked;
		idDemoBlock::ReadTimeline( reader, DEMO_WRAPPER_BYTES, static_cast<uint64_t>( reader.bytes.length ), walked );
		timeline.timed = walked.timed;
		timeline.cleanEnd = walked.cleanEnd;
		timeline.frames = static_cast<int>( Min( walked.frames, static_cast<uint32_t>( idMath::INT_MAX ) ) );
		timeline.durationUS = static_cast<int64>( walked.durationUS );
		timeline.validBytes = static_cast<int64>( walked.validBytes );
		timeline.blocks = static_cast<int>( Min( walked.blocks, static_cast<uint32_t>( idMath::INT_MAX ) ) );
		timeline.levels = static_cast<int>( Min( walked.segments, static_cast<uint32_t>( idMath::INT_MAX ) ) );
		for ( size_t i = 0; i < walked.entries.size(); i++ ) {
			demoTimelineEntry_t entry;
			entry.fileOffset = static_cast<int64>( walked.entries[i].offset );
			entry.startFrame = static_cast<int>( Min( walked.entries[i].startFrame, static_cast<uint32_t>( idMath::INT_MAX ) ) );
			entry.startTimeUS = static_cast<int64>( walked.entries[i].startTimeUS );
			entry.level = static_cast<int>( Min( walked.entries[i].segment, static_cast<uint32_t>( idMath::INT_MAX ) ) );
			entry.keyframe = walked.entries[i].keyframe;
			timeline.entries.Append( entry );
		}
	}
	reader.bytes.CloseDirect();
	fileSystem->CloseFile( file );
	return blockStream;
}

/*
================
idDemoFile::SeekToEntry
================
*/
bool idDemoFile::SeekToEntry( const demoTimelineEntry_t &entry ) {
	if ( blocks == NULL || blocks->reader == NULL || entry.fileOffset < DEMO_WRAPPER_BYTES ) {
		return false;
	}
	blocks->reader->stream.SeekToBlock( static_cast<uint64_t>( entry.fileOffset ) );
	blocks->reader->reported = false;
	// a level's records, and a keyframe's, define every string they use
	ResetHashStrings();
	return true;
}

/*
================
idDemoFile::DamagedAt
================
*/
int64 idDemoFile::DamagedAt() const {
	if ( blocks == NULL || blocks->reader == NULL || !blocks->reader->stream.Damaged() ) {
		return -1;
	}
	return static_cast<int64>( blocks->reader->stream.ProblemOffset() );
}

/*
================
idDemoFile::ReachedCleanEnd
================
*/
bool idDemoFile::ReachedCleanEnd() const {
	return blocks != NULL && blocks->reader != NULL && blocks->reader->stream.CleanEnd();
}

/*
================
idDemoFile::BeginSegment
================
*/
void idDemoFile::BeginSegment() {
	if ( blocks != NULL && blocks->writer != NULL ) {
		blocks->writer->BeginSegment();
	}
	ResetHashStrings();
}

/*
================
idDemoFile::BeginKeyframe
================
*/
void idDemoFile::BeginKeyframe() {
	if ( blocks != NULL && blocks->writer != NULL ) {
		blocks->writer->BeginKeyframe();
	}
	ResetHashStrings();
}

/*
================
idDemoFile::EndKeyframe
================
*/
void idDemoFile::EndKeyframe() {
	if ( blocks != NULL && blocks->writer != NULL ) {
		blocks->writer->EndKeyframe();
	}
	ResetHashStrings();
}

/*
================
idDemoFile::EndFrame
================
*/
void idDemoFile::EndFrame( int deltaUS ) {
	if ( blocks != NULL && blocks->writer != NULL ) {
		blocks->writer->EndFrame( deltaUS );
	}
}

/*
================
idDemoFile::WriteFailed
================
*/
bool idDemoFile::WriteFailed() const {
	return blocks != NULL && blocks->writer != NULL && blocks->writer->failed;
}

/*
================
idDemoFile::WriteError
================
*/
const char *idDemoFile::WriteError() const {
	return WriteFailed() ? blocks->writer->error.c_str() : "";
}

/*
================
idDemoFile::BytesWritten
================
*/
int64 idDemoFile::BytesWritten() const {
	return blocks != NULL && blocks->writer != NULL ? blocks->writer->fileBytes : closedBytes;
}

/*
================
idDemoFile::StreamBytesWritten
================
*/
int64 idDemoFile::StreamBytesWritten() const {
	if ( blocks == NULL || blocks->writer == NULL ) {
		return closedStreamBytes;
	}
	return static_cast<int64>( blocks->writer->builder.StreamBytes() );
}

/*
================
idDemoFile::CopyBlocksFrom

Repacks a block container block for block, so the copy keeps the timeline:
the frames, times and level starts its headers carry.
================
*/
bool idDemoFile::CopyBlocksFrom( idDemoFile &source ) {
	if ( blocks == NULL || blocks->writer == NULL || source.blocks == NULL || source.blocks->reader == NULL ) {
		return false;
	}
	demoBlockWriter_t &writer = *blocks->writer;
	demoBlockReader_t &reader = *source.blocks->reader;
	writer.builder.Finish( false, writer.closed );
	writer.Dispatch();
	// keyframes are blocks like any other to a copy
	reader.stream.HandOutEveryBlock( true );
	while ( reader.stream.NextBlock() ) {
		idDemoBlock::job_t *job = new idDemoBlock::job_t;
		job->header = reader.stream.Header();
		job->raw = reader.stream.Block();
		writer.builder.ContinueAfter( job->header );
		writer.SubmitJob( job );
		if ( writer.failed ) {
			return false;
		}
	}
	reader.Report();
	writer.WriteCompleted( true );
	return !writer.failed && !reader.stream.Damaged();
}

/*
================
idDemoFile::ReadHashString
================
*/
const char *idDemoFile::ReadHashString() {
	int		index;

	if ( log && fLog ) {
		const char *text = va( "%s > Reading hash string\n", logStr.c_str() );
		fLog->Write( text, idLib::SizeToInt( strlen( text ), "idDemoFile::ReadHashString" ) );
	}

	if ( ReadInt( index ) != sizeof( index ) ) {
		Close();
		common->Warning( "Malformed render demo: truncated hash index; playback stopped safely" );
		return "";
	}

	if ( index == -1 ) {
		// read a new string for the table
		if ( demoStrings.Num() >= MAX_DEMO_HASH_STRINGS ) {
			Close();
			common->Warning( "Malformed render demo: hash table exceeds %d entries; playback stopped safely", MAX_DEMO_HASH_STRINGS );
			return "";
		}

		int length = 0;
		if ( ReadInt( length ) != sizeof( length ) || length < 0 || length >= MAX_STRING_CHARS ) {
			Close();
			common->Warning( "Malformed render demo: invalid hash string length %d; playback stopped safely", length );
			return "";
		}

		idStr data;
		if ( length > 0 ) {
			data.Fill( ' ', length );
			if ( Read( &data[0], length ) != length ) {
				Close();
				common->Warning( "Malformed render demo: truncated hash string payload; playback stopped safely" );
				return "";
			}
		}

		idStr	*str = new idStr;
		*str = data;

		demoStrings.Append( str );

		return *str;
	}

	if ( index < -1 || index >= demoStrings.Num() ) {
		Close();
		common->Warning( "Malformed render demo: hash index %d is out of range; playback stopped safely", index );
		return "";
	}

	return demoStrings[index]->c_str();
}

/*
================
idDemoFile::WriteHashString
================
*/
void idDemoFile::WriteHashString( const char *str ) {
	if ( str == NULL ) {
		str = "";
	}

	if ( log && fLog ) {
		const char *text = va( "%s > Writing hash string\n", logStr.c_str() );
		fLog->Write( text, idLib::SizeToInt( strlen( text ), "idDemoFile::WriteHashString" ) );
	}
	// see if it is already in the table; a campaign recording sees tens of
	// thousands of names, so this must not be a linear search
	const int key = demoStringHash.GenerateKey( str, true );
	for ( int i = demoStringHash.First( key ); i != -1; i = demoStringHash.Next( i ) ) {
		if ( !strcmp( demoStrings[i]->c_str(), str ) ) {
			WriteInt( i );
			return;
		}
	}

	// add it to our table and the demo table
	idStr	*copy = new idStr( str );
//common->Printf( "hash:%i = %s\n", demoStrings.Num(), str );
	demoStringHash.Add( key, demoStrings.Num() );
	demoStrings.Append( copy );
	int cmd = -1;
	WriteInt( cmd );
	WriteString( str );
}

/*
================
idDemoFile::ReadDict
================
*/
void idDemoFile::ReadDict( idDict &dict ) {
	int i, c;
	idStr key, val;

	dict.Clear();
	if ( ReadInt( c ) != sizeof( c ) || c < 0 || c > MAX_DEMO_DICT_ENTRIES ) {
		Close();
		common->Warning( "Malformed render demo: dictionary count is out of range; playback stopped safely" );
		return;
	}
	for ( i = 0; i < c; i++ ) {
		key = ReadHashString();
		if ( !IsOpen() ) {
			return;
		}
		val = ReadHashString();
		if ( !IsOpen() ) {
			return;
		}
		dict.Set( key, val );
	}
}

/*
================
idDemoFile::WriteDict
================
*/
void idDemoFile::WriteDict( const idDict &dict ) {
	int i, c;

	c = dict.GetNumKeyVals();
	WriteInt( c );
	for ( i = 0; i < c; i++ ) {
		WriteHashString( dict.GetKeyVal( i )->GetKey() );
		WriteHashString( dict.GetKeyVal( i )->GetValue() );
	}
}

/*
 ================
 idDemoFile::Read
 ================
 */
int idDemoFile::Read( void *buffer, int len ) {
	int read = 0;
	if ( blocks != NULL ) {
		if ( blocks->reader == NULL || buffer == NULL || len <= 0 ) {
			return 0;
		}
		read = blocks->reader->ReadStream( buffer, len );
	} else if ( compressor != NULL ) {
		read = compressor->Read( buffer, len );
	} else {
		return 0;
	}
	if ( read == 0 && len >= 4 ) {
		*(demoSystem_t *)buffer = DS_FINISHED;
	}
	return read;
}

/*
 ================
 idDemoFile::Write
 ================
 */
int idDemoFile::Write( const void *buffer, int len ) {
	if ( buffer == NULL || len <= 0 ) {
		return 0;
	}
	if ( blocks != NULL ) {
		if ( blocks->writer == NULL || blocks->writer->failed ) {
			return 0;
		}
		blocks->writer->Append( buffer, len );
		return len;
	}
	if ( compressor == NULL ) {
		return 0;
	}
	return compressor->Write( buffer, len );
}
