/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2013 Robert Beckebans
Copyright (C) 1997-2012 Sam Lantinga <slouken@libsdl.org>  (MS ADPCM decoder)
Copyright (c) 2011 Chris Robinson <chris.kcat@gmail.com> (OpenAL helpers)

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "../snd_local.h"
#include "../../framework/ParallelJobSystem.h"
#include <cstdlib>
#include <memory>
#include <stdint.h>
#include "../SoundVorbisStream.h"
#include "../SoundMemoryBudget.h"
#include "../SoundQueue.h"
static_assert( SoundVorbisStream::ChunkFrames == SOUND_QUEUE_DECODE_FRAMES,
	"voice queues fill a buffer in decoder-sized reads" );
static_assert( SoundVorbisStream::ChunkFrames * 6 * sizeof( short ) == SOUND_QUEUE_BUFFER_BYTES,
	"stream reservations count three SOUND_QUEUE_BUFFER_BYTES queue buffers" );

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

extern idCVar s_useCompression;
extern idCVar s_noSound;
extern idCVar s_releaseSamplePayload;
extern idCVar s_debugHardware;

#define GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( x ) x

const uint32 SOUND_MAGIC_IDMSA = 0x6D7A7274;
static const int ROQ_FILE_MAGIC = 0x1084;
static const int ROQ_SOUND_MONO = 0x1020;
static const int ROQ_SOUND_STEREO = 0x1021;
static const int ROQ_AUDIO_SAMPLE_RATE = 22050;
static const int ROQ_AUDIO_MIN_SAMPLE = -32768;
static const int ROQ_AUDIO_MAX_SAMPLE = 32767;

// Some OpenAL implementations expose a small, process-wide buffer pool.  A
// failed allocation must not turn a successfully loaded map into a fatal
// error; keep the decoded sample available for the voice streaming path and
// avoid repeating the same warning for every remaining sample in the map.
static std::atomic<bool> openQ4_openALBufferAllocationWarningIssued( false );

extern idCVar sys_lang;

idCVar s_audioMemoryMB( "s_audioMemoryMB", "256", CVAR_SOUND | CVAR_ARCHIVE | CVAR_INTEGER,
	"MiB cap for managed audio payloads, decoders and conservative queue/scratch reservations; excludes provider internals and metadata", 32, 2048 );
static SoundMemoryBudget soundMemory;
// All 96 hardware voices can retain three queue buffers of one 4096-frame 5.1
// block each (SOUND_QUEUE_BUFFER_BYTES; mono and stereo fit more frames), and the
// deep centre-routed slots their larger buffers plus routing scratch (asserted
// below). The rest covers voice-chat rings, decode/probe scratch and headers.
static const uint64 SOUND_MEMORY_FIXED_BYTES = 20 * 1024 * 1024;
static_assert( MAX_HARDWARE_VOICES * 2ULL * MAX_QUEUED_BUFFERS * 4096 * 6 * sizeof( short ) + 2 * 1024 * 1024 <= SOUND_MEMORY_FIXED_BYTES,
	"Update the fixed audio reservation when voice queue capacity changes" );
static uint64 soundMemoryRejected = 0;
static bool soundMemoryReclaiming = false;
static void SoundMemoryReclaim();
static bool SoundMemoryAcquire( uint64 bytes ) {
	const uint64 limit = static_cast<uint64>( idMath::ClampInt( 32, 2048, s_audioMemoryMB.GetInteger() ) ) * 1024 * 1024 - SOUND_MEMORY_FIXED_BYTES;
	if( soundMemory.Acquire( bytes, limit ) ) return true;
	if( !soundMemoryReclaiming ) {
		soundMemoryReclaiming = true;
		SoundMemoryReclaim();
		soundMemoryReclaiming = false;
		if( soundMemory.Acquire( bytes, limit ) ) return true;
	}
	if( soundMemoryRejected++ == 0 ) idLib::Warning( "Audio payload budget exhausted (s_audioMemoryMB=%d); retaining active audio and refusing new allocations. Further refusals are counted by listSounds.", s_audioMemoryMB.GetInteger() );
	return false;
}
static void SoundMemoryRelease( uint64 bytes ) { soundMemory.Release( bytes ); }

class SoundMemoryReservation {
public:
	SoundMemoryReservation() = default;
	SoundMemoryReservation( const SoundMemoryReservation& ) = delete;
	SoundMemoryReservation& operator=( const SoundMemoryReservation& ) = delete;
	~SoundMemoryReservation() { SoundMemoryRelease( bytes ); }
	bool Reserve( uint64 count ) {
		if( count <= bytes ) return true;
		if( !SoundMemoryAcquire( count - bytes ) ) return false;
		bytes = count; return true;
	}
	uint64 Transfer() { const uint64 result = bytes; bytes = 0; return result; }
private:
	uint64 bytes = 0;
};

idCVar s_streamThresholdKB( "s_streamThresholdKB", "512", CVAR_SOUND | CVAR_ARCHIVE | CVAR_INTEGER,
	"stream Ogg samples whose decoded PCM exceeds this many KiB (0 disables streaming; applies on load)", 0, 16384 );
static_assert( MAX_HARDWARE_VOICES * 2ULL * MAX_QUEUED_BUFFERS * SOUND_QUEUE_BUFFER_BYTES +
	SOUND_QUEUE_DEEP_ROUTED_VOICES * static_cast<uint64>( MAX_QUEUED_BUFFERS ) * ( SOUND_QUEUE_DEEP_ROUTED_BUFFER_BYTES - SOUND_QUEUE_BUFFER_BYTES ) +
	SOUND_QUEUE_MAX_FRAMES * 7ULL * sizeof( short ) + 2 * 1024 * 1024 <= SOUND_MEMORY_FIXED_BYTES,
	"the fixed audio reservation must cover every voice queue, the deep routed slots and their scratch" );
idCVar s_streamMemoryMB( "s_streamMemoryMB", "32", CVAR_SOUND | CVAR_ARCHIVE | CVAR_INTEGER,
	"MiB reserved for active Vorbis decoders, input caches and three PCM queue buffers per voice", 8, 256 );
static uint64 soundStreamBytes = 0, soundStreamPeakBytes = 0;
static unsigned int soundStreamRejected = 0;
// Includes the decode/routing scratch, the three OpenAL queue buffers (each at
// most one 4096-frame 5.1 block, SOUND_QUEUE_BUFFER_BYTES) and conservative VFS
// zip/inflate bookkeeping. OpenAL implementation-private overhead is separate.
static const uint64 SOUND_STREAM_BYTES = SoundVorbisStream::ArenaBytes + sizeof( SoundVorbisStream ) +
	SoundVorbisStream::ChunkFrames * ( 2 + 4 * 6 ) * sizeof( short ) + 128 * 1024 +
	idSoundOggStream::BLOCK_SAMPLES * sizeof( short );
static_assert( idSoundOggStream::BLOCK_SAMPLES >= SOUND_QUEUE_BUFFER_BYTES / static_cast<int>( sizeof( short ) ) &&
	idSoundOggStream::BLOCK_SAMPLES >= SOUND_QUEUE_MAX_FRAMES, "a stream block holds one queue buffer, interleaved or centre-routed mono" );

idCVar s_asyncStreamDecode( "s_asyncStreamDecode", "1", CVAR_SOUND | CVAR_BOOL,
	"open, seek and decode streamed Ogg blocks on job workers instead of the main thread" );
// Streams released with a block still decoding; the worker owns them until joined.
static idList<idSoundOggStream*> soundStreamReleased;
static unsigned int soundStreamStarts = 0, soundStreamFirstBlocks = 0, soundStreamInlineBlocks = 0, soundStreamWorkerBlocks = 0;
static uint64 soundStreamStartWaitTotalMs = 0, soundStreamStartDecodeTotalMs = 0;
static int soundStreamStartWaitMaxMs = 0, soundStreamStartDecodeMaxMs = 0;
static idStr soundStreamSlowestStart;

static int SoundStream_ReadAt( void* user, unsigned int offset, void* data, int bytes ) {
	idFile* file = static_cast<idFile*>( user );
	if( offset > static_cast<unsigned int>( idMath::INT_MAX ) ||
		( file->Tell() != static_cast<int>( offset ) && file->Seek( offset, FS_SEEK_SET ) != 0 ) ) return 0;
	return file->Read( data, bytes );
}

idSoundOggStream::idSoundOggStream() : file( NULL ), decoder( NULL ), list( NULL ), state( BLOCK_IDLE ), fileLength( 0 ), requestTime( 0 ),
	decoderOpen( false ), outcome( DECODE_NONE ), decoderError( 0 ), position( 0 ), requestFrame( 0 ), requestFrames( 0 ), decodedFrames( 0 ), decodeMs( 0 ) {}
idSoundOggStream::~idSoundOggStream() { Close(); }
void idSoundOggStream::Close() {
	if( list != NULL ) {
		// Release parks streams with an outstanding block, so this waits only for
		// a block a worker is finishing at shutdown.
		list->Cancel();
		list->Wait();
		delete list;
		list = NULL;
	}
	if( decoder != NULL ) {
		delete decoder;
		decoder = NULL;
		soundStreamBytes -= SOUND_STREAM_BYTES;
		SoundMemoryRelease( SOUND_STREAM_BYTES );
	}
	if( file != NULL ) { fileSystem->CloseFile( file ); file = NULL; }
	state = BLOCK_IDLE; decoderOpen = false; outcome = DECODE_NONE; position = 0; decodedFrames = 0; requestTime = 0;
}
bool idSoundOggStream::Open( const char* filename, const idSoundOggStreamShape& expected ) {
	Close();
	name = filename;
	shape = expected;
	state = BLOCK_FAILED;
	if( shape.channels < 1 || shape.channels > 2 || shape.rate <= 0 || shape.frames == 0 ) return false;
	const uint64 budget = static_cast<uint64>( idMath::ClampInt( 8, 256, s_streamMemoryMB.GetInteger() ) ) * 1024 * 1024;
	if( soundStreamBytes > budget || SOUND_STREAM_BYTES > budget - soundStreamBytes ) {
		++soundStreamRejected;
		return false;
	}
	// The VFS open stays on the main thread: it walks the shared search paths and
	// pak handles. Reads of the opened file are independent and run with the decode.
	file = fileSystem->OpenFileRead( filename );
	if( file == NULL || file->Length() <= 0 ) { Close(); state = BLOCK_FAILED; return false; }
	if( !SoundMemoryAcquire( SOUND_STREAM_BYTES ) ) { Close(); state = BLOCK_FAILED; ++soundStreamRejected; return false; }
	decoder = new SoundVorbisStream;
	soundStreamBytes += SOUND_STREAM_BYTES;
	soundStreamPeakBytes = Max( soundStreamPeakBytes, soundStreamBytes );
	fileLength = static_cast<unsigned int>( file->Length() );
	state = BLOCK_IDLE;
	++soundStreamStarts;
	return true;
}
// Runs on a job worker, or inline. Touches only this stream's decoder, file and block.
void idSoundOggStream::Decode( const idJobContext* context ) {
	const int began = Sys_Milliseconds();
	DecodeBlock( context );
	decodeMs = Sys_Milliseconds() - began;
}
void idSoundOggStream::DecodeBlock( const idJobContext* context ) {
	outcome = DECODE_NONE;
	decodedFrames = 0;
	if( context != NULL && context->IsCancellationRequested() ) return;
	if( !decoderOpen ) {
		if( !decoder->Open( SoundStream_ReadAt, file, fileLength ) ) {
			decoderError = decoder->Error(); outcome = DECODE_OPEN_FAILED; return;
		}
		if( shape.countedLength ) {
			const stb_vorbis_openq4_granule_page page = { shape.pageStart, shape.pageEnd, shape.pageGranule };
			if( !decoder->SetCountedLength( shape.frames, page ) ) { outcome = DECODE_SHAPE_MISMATCH; return; }
		}
		if( decoder->Channels() != shape.channels || decoder->Rate() != shape.rate || decoder->Frames() != shape.frames ) {
			outcome = DECODE_SHAPE_MISMATCH; return;
		}
		decoderOpen = true;
		position = 0;
	}
	if( position != requestFrame ) {
		if( context != NULL && context->IsCancellationRequested() ) return;
		if( !decoder->Seek( requestFrame ) ) { outcome = DECODE_SEEK_FAILED; return; }
		position = requestFrame;
	}
	int frames = 0;
	while( frames < requestFrames ) {
		// A cancelled block keeps an exact cursor; the next request seeks from it.
		if( context != NULL && context->IsCancellationRequested() ) return;
		const int count = decoder->Read( block + frames * shape.channels, Min( static_cast<int>( SoundVorbisStream::ChunkFrames ), requestFrames - frames ) );
		if( count <= 0 ) break;
		frames += count;
		position += count;
	}
	decodedFrames = frames;
	outcome = frames > 0 ? DECODE_READY : DECODE_EMPTY;
}
void idSoundOggStream::DecodeJob( const idJobContext& context ) {
	static_cast<idSoundOggStream*>( context.data )->Decode( &context );
}
// Main thread, once the decode has finished or did not run.
void idSoundOggStream::FinishBlock() {
	switch( outcome ) {
		case DECODE_READY:
			state = BLOCK_READY;
			if( requestTime != 0 ) {
				// The wait covers the job queue and the frame that polls it; the decode
				// covers the worker's header parse, seek and first block.
				const int waited = Max( 0, Sys_Milliseconds() - requestTime );
				++soundStreamFirstBlocks;
				soundStreamStartWaitTotalMs += static_cast<uint64>( waited );
				soundStreamStartDecodeTotalMs += static_cast<uint64>( Max( 0, decodeMs ) );
				if( waited > soundStreamStartWaitMaxMs ) {
					soundStreamStartWaitMaxMs = waited;
					soundStreamSlowestStart = va( "%s at frame %u (decode %d ms)", name.c_str(), requestFrame, decodeMs );
				}
				soundStreamStartDecodeMaxMs = Max( soundStreamStartDecodeMaxMs, decodeMs );
				if( s_debugHardware.GetBool() ) idLib::Printf( "Vorbis stream start: %s at frame %u waited %d ms (decode %d ms)\n", name.c_str(), requestFrame, waited, decodeMs );
				requestTime = 0;
			}
			break;
		case DECODE_NONE:
			// Cancelled before or between codec blocks: the cursor is exact, ask again.
			state = BLOCK_IDLE;
			break;
		case DECODE_OPEN_FAILED:
			idLib::Warning( "Vorbis stream '%s' cannot open within its fixed decoder arena (error %d)", name.c_str(), decoderError );
			state = BLOCK_FAILED;
			break;
		default:
			if( s_debugHardware.GetBool() ) idLib::Printf( "Vorbis stream '%s' failed at frame %u (%d)\n", name.c_str(), requestFrame, static_cast<int>( outcome ) );
			state = BLOCK_FAILED;
			break;
	}
}
bool idSoundOggStream::RequestBlock( unsigned int frame, int frames ) {
	if( state == BLOCK_PENDING || state == BLOCK_FAILED || decoder == NULL ) return false;
	if( frames < 1 || frames > BLOCK_SAMPLES / Max( 1, shape.channels ) || frame >= shape.frames ) { state = BLOCK_FAILED; return false; }
	requestFrame = frame;
	requestFrames = frames;
	// A stream's first block is its voice's start: measure how long it waits.
	if( !decoderOpen && requestTime == 0 ) requestTime = Max( 1, Sys_Milliseconds() );
	outcome = DECODE_NONE;
	if( s_asyncStreamDecode.GetBool() && jobSystem.IsInitialized() && !jobSystem.IsSynchronous() ) {
		std::unique_ptr<idJobList> created = jobSystem.CreateJobList( "sound-stream-block", 1, 0, idJobPriority::HIGH );
		if( created != nullptr && created->AddJob( &DecodeJob, this ) ) {
			const idJobSubmitResult submitted = created->Submit();
			if( submitted == idJobSubmitResult::ACCEPTED ) {
				list = created.release();
				state = BLOCK_PENDING;
				++soundStreamWorkerBlocks;
				return true;
			}
			if( submitted == idJobSubmitResult::EXECUTED_SYNCHRONOUSLY ) {
				++soundStreamInlineBlocks;
				FinishBlock();
				return state != BLOCK_FAILED;
			}
		}
	}
	// No worker accepted it (disabled, synchronous, queue full or shutting down).
	Decode( NULL );
	++soundStreamInlineBlocks;
	FinishBlock();
	return state != BLOCK_FAILED;
}
idSoundOggStream::blockState_t idSoundOggStream::PollBlock() {
	if( state == BLOCK_PENDING && list != NULL && list->TryWait() ) {
		delete list;
		list = NULL;
		FinishBlock();
	}
	return state;
}
void idSoundOggStream::ConsumeBlock() {
	if( state == BLOCK_READY ) state = BLOCK_IDLE;
}
void idSoundOggStream::Release( idSoundOggStream* stream ) {
	if( stream == NULL ) return;
	if( stream->list != NULL && !stream->list->TryWait() ) {
		// Never wait for the worker here: an unstarted block is skipped, a running
		// one stops at its next codec block, and ReapReleased frees the stream.
		stream->list->Cancel();
		soundStreamReleased.Append( stream );
		return;
	}
	delete stream;
}
void idSoundOggStream::ReapReleased( bool wait ) {
	for( int i = soundStreamReleased.Num() - 1; i >= 0; --i ) {
		idSoundOggStream* stream = soundStreamReleased[i];
		if( !wait && stream->list != NULL && !stream->list->TryWait() ) continue;
		soundStreamReleased.RemoveIndex( i );
		delete stream;
	}
}
int idSoundOggStream::ReleasedCount() { return soundStreamReleased.Num(); }
void idSoundOggStream::PrintMemoryInfo() {
	idLib::Printf( "Vorbis streaming: reserved=%llu KiB peak=%llu KiB budget=%d MiB rejected=%u decode=%d frames\n",
		static_cast<unsigned long long>( soundStreamBytes / 1024 ), static_cast<unsigned long long>( soundStreamPeakBytes / 1024 ),
		s_streamMemoryMB.GetInteger(), soundStreamRejected, SoundVorbisStream::ChunkFrames );
	idLib::Printf( "Vorbis stream blocks: worker=%u inline=%u opens=%u first-block wait avg=%.1f ms max=%d ms, decode avg=%.1f ms max=%d ms, over %u starts, released-pending=%d\n",
		soundStreamWorkerBlocks, soundStreamInlineBlocks, soundStreamStarts,
		soundStreamFirstBlocks ? static_cast<double>( soundStreamStartWaitTotalMs ) / soundStreamFirstBlocks : 0.0, soundStreamStartWaitMaxMs,
		soundStreamFirstBlocks ? static_cast<double>( soundStreamStartDecodeTotalMs ) / soundStreamFirstBlocks : 0.0, soundStreamStartDecodeMaxMs,
		soundStreamFirstBlocks, soundStreamReleased.Num() );
	if( soundStreamSlowestStart.Length() ) idLib::Printf( "Vorbis slowest stream start: %s\n", soundStreamSlowestStart.c_str() );
}

/*
========================
RoQAudioDelta
========================
*/
static ID_INLINE int RoQAudioDelta( byte sampleCode )
{
	const int magnitude = sampleCode & 0x7F;
	const int delta = magnitude * magnitude;
	return ( sampleCode & 0x80 ) ? -delta : delta;
}

/*
========================
AllocBuffer
========================
*/
struct alignas(16) SoundAllocationHeader { uint64 bytes; uint64 padding; };
static void* AllocBuffer( int size, const char* name )
{
	(void)name;
	if( size <= 0 || size > idMath::INT_MAX - static_cast<int>( sizeof( SoundAllocationHeader ) ) ) return NULL;
	const uint64 bytes = static_cast<uint64>( size ) + sizeof( SoundAllocationHeader );
	if( !SoundMemoryAcquire( bytes ) ) return NULL;
	auto* allocation = static_cast<SoundAllocationHeader*>( Mem_Alloc( static_cast<int>( bytes ) ) );
	if( allocation == NULL ) { SoundMemoryRelease( bytes ); return NULL; }
	allocation->bytes = bytes;
	return allocation + 1;
}

/*
========================
FreeBuffer
========================
*/
static void FreeBuffer( void* p )
{
	if( p == NULL ) return;
	auto* allocation = static_cast<SoundAllocationHeader*>( p ) - 1;
	const uint64 bytes = allocation->bytes;
	Mem_Free( allocation );
	SoundMemoryRelease( bytes );
}

static bool SoundResizeAmplitude( idList<byte>& out, uint64& reserved, int count ) {
	out.Clear();
	SoundMemoryRelease( reserved ); reserved = 0;
	if( count <= 0 ) return count == 0;
	const uint64 bytes = ( static_cast<uint64>( count ) + 15 ) / 16 * 16;
	if( !SoundMemoryAcquire( bytes ) ) return false;
	reserved = bytes;
	out.SetGranularity( 16 );
	out.SetNum( count );
	return true;
}

template<class type>
static bool openQ4_ReadBigExact( idFile* file, type& value )
{
	return file != NULL && file->ReadBig( value ) == sizeof( value );
}

static bool openQ4_ReadExact( idFile* file, void* buffer, const int size )
{
	if( size < 0 || ( size > 0 && buffer == NULL ) )
	{
		return false;
	}
	return size == 0 || ( file != NULL && file->Read( buffer, size ) == size );
}

static bool openQ4_ReadWaveExact( idWaveFile& wave, void* buffer, const int size )
{
	if( size <= 0 || buffer == NULL )
	{
		return false;
	}
	return wave.Read( buffer, size ) == ( size_t )size;
}

/*
========================
openQ4_GetMSADPCMDecodedSize

Validates the MS ADPCM block geometry before any arithmetic is narrowed to
the engine's legacy int-sized allocation and sample-count interfaces.
========================
*/
static bool openQ4_GetMSADPCMDecodedSize( const uint32_t encodedBytes, const uint16_t blockSize,
	const uint16_t samplesPerBlock, const uint16_t channels, const uint16_t numCoefficients,
	size_t& decodedBytes )
{
	decodedBytes = 0;
	if( encodedBytes == 0 || blockSize == 0 || ( channels != 1 && channels != 2 ) ||
		samplesPerBlock < 2 || numCoefficients == 0 || numCoefficients > 7 || encodedBytes % blockSize != 0 )
	{
		return false;
	}

	const uint64_t blockHeaderBytes = static_cast<uint64_t>( channels ) * 7u;
	const uint64_t samplesAfterHeader =
		( static_cast<uint64_t>( samplesPerBlock ) - 2u ) * static_cast<uint64_t>( channels );
	if( ( samplesAfterHeader & 1u ) != 0 )
	{
		return false;
	}

	const uint64_t encodedSampleBytes = samplesAfterHeader / 2u;
	if( blockHeaderBytes + encodedSampleBytes != static_cast<uint64_t>( blockSize ) )
	{
		return false;
	}

	const uint64_t blockCount = static_cast<uint64_t>( encodedBytes ) / blockSize;
	const uint64_t decodedByteCount = blockCount * static_cast<uint64_t>( samplesPerBlock ) *
		static_cast<uint64_t>( channels ) * sizeof( int16_t );
	if( decodedByteCount == 0 || decodedByteCount > static_cast<uint64_t>( idMath::INT_MAX ) )
	{
		return false;
	}

	// idMath::INT_MAX is no larger than SIZE_MAX on every supported target.
	decodedBytes = static_cast<size_t>( decodedByteCount );
	return true;
}

/*
========================
SoundSample_AppendUniqueSampleVariant
========================
*/
static void SoundSample_AppendUniqueSampleVariant( idList< idStr >& variants, const idStr& name )
{
	for( int i = 0; i < variants.Num(); i++ )
	{
		// Icmp so the probe list does not gain a duplicate entry on
		// case-insensitive hosts while still being correct on Linux
		if( variants[ i ].Icmp( name ) == 0 )
		{
			return;
		}
	}
	variants.Append( name );
}

/*
========================
SoundSample_AppendLocalizedVOVariants

Sound shaders reference the unlocalized 'sound/vo/...' path, but the retail
data only ships voice-over under a per-language tree. Add both spellings retail
used for a given language.
========================
*/
static void SoundSample_AppendLocalizedVOVariants( idList< idStr >& variants, const idStr& baseName, const char* language )
{
	if( language == NULL || language[ 0 ] == '\0' )
	{
		return;
	}

	idStr localized = baseName;
	if( localized.Replace( "/vo/", va( "/vo_%s/", language ) ) )
	{
		SoundSample_AppendUniqueSampleVariant( variants, localized );
	}

	localized = baseName;
	if( localized.Replace( "/vo/", va( "/vo/%s/", language ) ) )
	{
		SoundSample_AppendUniqueSampleVariant( variants, localized );
	}
}

/*
========================
SoundSample_AppendMissingQ4StockFallback

The retail ambient_water_splash_big shader names a sample that was omitted
from the shipped PK4s. Probe the authored path first so mods can supply it,
then fall back to the shipped splash from the same sound family.
========================
*/
static void SoundSample_AppendMissingQ4StockFallback( idList< idStr >& variants, const idStr& baseName )
{
	idStr canonicalName = baseName;
	canonicalName.BackSlashesToSlashes();
	canonicalName.StripFileExtension();
	if( canonicalName.Icmp( "sound/ambience/water/splash_big" ) == 0 )
	{
		SoundSample_AppendUniqueSampleVariant( variants, "sound/ambience/water/splash_small02" );
	}
}

static bool openQ4_CanUploadSampleToOpenAL()
{
	ALCcontext* const expectedContext = soundSystemLocal.hardware.GetOpenALContext();
	if( expectedContext == NULL )
	{
		// Sound samples can be parsed before the OpenAL device is initialized.
		return false;
	}

	ALCcontext* const currentContext = alcGetCurrentContext();
	if( currentContext == expectedContext )
	{
		return true;
	}

	if( currentContext == NULL )
	{
		return alcMakeContextCurrent( expectedContext ) != 0;
	}

	return false;
}

/*
========================
idSoundSample_OpenAL::idSoundSample_OpenAL
========================
*/
idSoundSample_OpenAL::idSoundSample_OpenAL()
{
	pendingDecode = NULL;
	timestamp = FILE_NOT_FOUND_TIMESTAMP;
	loaded = false;
	neverPurge = false;
	levelLoadReferenced = false;

	memset( &format, 0, sizeof( format ) );

	totalBufferSize = 0;

	playBegin = 0;
	playLength = 0;

	lastPlayedTime = 0;

	payloadReleased = false;
	// deliberately not cleared by FreeData: LoadResource frees before it
	// reloads, and the whole point of the flag is to survive that
	keepPayload = false;
	openalBuffer = 0;
	openalBufferUploadFailed = false;
	openalReserved = 0;
	amplitudeReserved = 0;
	streamCountedLength = false;
	streamPageStart = 0;
	streamPageEnd = 0;
	streamPageGranule = 0;
}

/*
========================
idSoundSample_OpenAL::~idSoundSample_OpenAL
========================
*/
idSoundSample_OpenAL::~idSoundSample_OpenAL()
{
	FreeData();
}

/*
========================
idSoundSample_OpenAL::WriteGeneratedSample
========================
*/
void idSoundSample_OpenAL::WriteGeneratedSample( idFile* fileOut )
{
	fileOut->WriteBig( SOUND_MAGIC_IDMSA );
	fileOut->WriteBig( timestamp );
	fileOut->WriteBig( loaded );
	fileOut->WriteBig( playBegin );
	fileOut->WriteBig( playLength );
	idWaveFile::WriteWaveFormatDirect( format, fileOut );
	fileOut->WriteBig( ( int )amplitude.Num() );
	fileOut->Write( amplitude.Ptr(), amplitude.Num() );
	fileOut->WriteBig( totalBufferSize );
	fileOut->WriteBig( ( int )buffers.Num() );
	for( int i = 0; i < buffers.Num(); i++ )
	{
		fileOut->WriteBig( buffers[ i ].numSamples );
		fileOut->WriteBig( buffers[ i ].bufferSize );
		fileOut->Write( buffers[ i ].buffer, buffers[ i ].bufferSize );
	};
}
/*
========================
idSoundSample_OpenAL::WriteAllSamples
========================
*/
void idSoundSample_OpenAL::WriteAllSamples( const idStr& sampleName )
{
	idSoundSample_OpenAL* samplePC = new idSoundSample_OpenAL();
	{
		idStr inName = sampleName;
		inName.Append( ".msadpcm" );
		idStr inName2 = sampleName;
		inName2.Append( ".wav" );

		idStr outName = "generated/";
		outName.Append( sampleName );
		outName.Append( ".idwav" );

		if( samplePC->LoadWav( inName ) || samplePC->LoadWav( inName2 ) )
		{
			// the generated/ tree is regenerable cache, and fs_basepath is
			// commonly the read-only game install (SAF-backed storage on
			// Android, Program Files on Windows); fs_cachepath is always
			// writable and falls back to fs_savepath when the host leaves it unset
			idFile* fileOut = fileSystem->OpenFileWrite( outName, "fs_cachepath" );
			samplePC->WriteGeneratedSample( fileOut );
			delete fileOut;
		}
	}
	delete samplePC;
}

/*
========================
idSoundSample_OpenAL::LoadGeneratedSound
========================
*/
bool idSoundSample_OpenAL::LoadGeneratedSample( const idStr& filename )
{
#if 1
	idFileLocal fileIn( fileSystem->OpenFileRead( filename ) );
	if( fileIn != NULL )
	{
		FreeData();

		const int fileLength = fileIn->Length();
		uint32 magic;
		if( !openQ4_ReadBigExact( fileIn, magic ) || magic != SOUND_MAGIC_IDMSA )
		{
			return false;
		}
		if( !openQ4_ReadBigExact( fileIn, timestamp ) ||
			!openQ4_ReadBigExact( fileIn, loaded ) ||
			!openQ4_ReadBigExact( fileIn, playBegin ) ||
			!openQ4_ReadBigExact( fileIn, playLength ) ||
			playBegin < 0 ||
			playLength < 0 )
		{
			return false;
		}
		if( !idWaveFile::ReadWaveFormatDirect( format, fileIn ) )
		{
			return false;
		}
		int num;
		if( !openQ4_ReadBigExact( fileIn, num ) || num < 0 || num > fileLength )
		{
			return false;
		}
		if( !SoundResizeAmplitude( amplitude, amplitudeReserved, num ) ) return false;
		if( !openQ4_ReadExact( fileIn, amplitude.Ptr(), amplitude.Num() ) )
		{
			SoundResizeAmplitude( amplitude, amplitudeReserved, 0 );
			return false;
		}
		if( !openQ4_ReadBigExact( fileIn, totalBufferSize ) || totalBufferSize <= 0 )
		{
			return false;
		}
		if( !openQ4_ReadBigExact( fileIn, num ) || num <= 0 || num > totalBufferSize || num > 16384 )
		{
			return false;
		}
		buffers.SetNum( num );
		int remainingBufferBytes = totalBufferSize;
		for( int i = 0; i < num; i++ )
		{
			buffers[ i ].buffer = NULL;
			if( !openQ4_ReadBigExact( fileIn, buffers[ i ].numSamples ) ||
				!openQ4_ReadBigExact( fileIn, buffers[ i ].bufferSize ) ||
				buffers[ i ].numSamples < 0 ||
				buffers[ i ].bufferSize <= 0 ||
				buffers[ i ].bufferSize > remainingBufferBytes )
			{
				for( int j = 0; j < i; j++ )
				{
					FreeBuffer( buffers[ j ].buffer );
				}
				buffers.Clear();
				return false;
			}
			buffers[ i ].buffer = AllocBuffer( buffers[ i ].bufferSize, GetName() );
			if( buffers[ i ].buffer == NULL || !openQ4_ReadExact( fileIn, buffers[ i ].buffer, buffers[ i ].bufferSize ) )
			{
				for( int j = 0; j <= i; j++ )
				{
					FreeBuffer( buffers[ j ].buffer );
				}
				buffers.Clear();
				return false;
			}
			buffers[ i ].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[ i ].buffer );
			remainingBufferBytes -= buffers[ i ].bufferSize;
		}
		if( remainingBufferBytes != 0 )
		{
			for( int i = 0; i < buffers.Num(); i++ )
			{
				FreeBuffer( buffers[ i ].buffer );
			}
			buffers.Clear();
			return false;
		}
		return true;
	}
#endif

	return false;
}

/*
========================
SoundSample_BuildVariants

The files LoadResource probes for a sample, in order.
========================
*/
static void SoundSample_BuildVariants( const char* name, idList< idStr >& sampleVariants )
{
	idStr baseSampleName = name;
	if( baseSampleName.Find( "/vo/" ) >= 0 )
	{
		SoundSample_AppendLocalizedVOVariants( sampleVariants, baseSampleName, sys_lang.GetString() );

		// Retail Quake 4 resolves the voice-over language separately from the
		// text language (idSoundSample::Load -> SoundSample_SelectVOLanguage)
		// and falls back to English for any language that ships localized
		// subtitles but no localized voice track. Without this a text-only
		// language pack silences every line of dialogue while lip-sync, which
		// is driven from the decl rather than the sample, keeps animating.
		SoundSample_AppendLocalizedVOVariants( sampleVariants, baseSampleName, "english" );
	}
	SoundSample_AppendUniqueSampleVariant( sampleVariants, baseSampleName );
	SoundSample_AppendMissingQ4StockFallback( sampleVariants, baseSampleName );
}

/*
========================
idSoundSample_OpenAL::LoadResource
========================
*/
void idSoundSample_OpenAL::LoadResource()
{
	FreeData();
	const uint64 refusedAtStart = soundMemoryRejected;

	if( idStr::Icmpn( GetName(), "_default", 8 ) == 0 )
	{
		MakeDefault();
		return;
	}

	if( s_noSound.GetBool() )
	{
		MakeDefault();
		return;
	}

	loaded = false;

	idList< idStr > sampleVariants;
	SoundSample_BuildVariants( GetName(), sampleVariants );

	for( int i = 0; i < sampleVariants.Num(); i++ )
	{
		idStr sampleName = sampleVariants[ i ];
		idStr generatedName = "generated/";
		generatedName.Append( sampleName );

		{
			idStr ext;
			sampleName.ExtractFileExtension( ext );
			ext.ToLower();
			const bool preferRoQ = ( ext.Icmp( "roq" ) == 0 );

			idStr wavName = sampleName;
			wavName.SetFileExtension( ".wav" );

			idStr oggName = sampleName;
			oggName.SetFileExtension( ".ogg" );

			idStr roqName = sampleName;
			roqName.SetFileExtension( ".roq" );

			generatedName.SetFileExtension( ".idwav" );

			if( preferRoQ )
			{
				loaded = LoadRoQ( roqName );
			}
			else
			{
				loaded = false;
			}

			if( !loaded )
			{
				loaded = LoadOgg( oggName );
			}

			if( !loaded )
			{
				loaded = LoadGeneratedSample( generatedName ) || LoadWav( wavName );
			}

			if( !loaded && !preferRoQ )
			{
				loaded = LoadRoQ( roqName );
			}
		}

		if( loaded )
		{
			//if( cvarSystem->GetCVarBool( "fs_buildresources" ) )
			//{
			//	fileSystem->AddSamplePreload( GetName() );
			//	WriteAllSamples( GetName() );
			//
			//	if( sampleName.Find( "/vo/" ) >= 0 )
			//	{
			//		for( int i = 0; i < Sys_NumLangs(); i++ )
			//		{
			//			const char* lang = Sys_Lang( i );
			//			if( idStr::Icmp( lang, ID_LANG_ENGLISH ) == 0 )
			//			{
			//				continue;
			//			}
			//			idStrStatic< MAX_OSPATH > locName = GetName();
			//			locName.Replace( "/vo/", va( "/vo/%s/", Sys_Lang( i ) ) );
			//			WriteAllSamples( locName );
			//		}
			//	}
			//}

			// upload PCM data to OpenAL
			CreateOpenALBuffer();

			return;
		}
	}

	if( !loaded )
	{
		// Memory pressure is retryable, unlike a missing asset. Never cache a
		// replacement beep for a real file refused by the aggregate budget.
		if( soundMemoryRejected != refusedAtStart ) { FreeData(); return; }
		// Retail warns unconditionally here (idSoundSample::Load). Without it a
		// sample that resolves to nothing is completely invisible in the log,
		// which is how "no character audio at all" reports end up undiagnosable.
		// Name every path probed: for voice-over the probe list is rewritten
		// from the shader's unlocalized 'sound/vo/...' name, so the failing
		// filename is not the one the material author wrote.
		idStr probed;
		for( int i = 0; i < sampleVariants.Num(); i++ )
		{
			if( i > 0 )
			{
				probed += ", ";
			}
			probed += sampleVariants[ i ];
		}
		idLib::Warning( "Couldn't load sound '%s' using default (probed: %s)", GetName(), probed.c_str() );

		// make it default if everything else fails
		MakeDefault();
	}
	return;
}

/*
========================
SoundSample_BuildAmplitudeEnvelope

The peak of every 1/60 s of 16-bit PCM as 0-255, the rate GetAmplitude reads. Camera
shakes, controller rumble and sound-driven lights follow it. Retail measured the decoded
samples for this; the .amp files the BFG path expected never shipped with Quake 4.
========================
*/
static void SoundSample_BuildAmplitudeEnvelope( const int16* pcm, const uint32 bytes, const int channels, const int rate, idList<byte>& out, uint64& reserved )
{
	SoundResizeAmplitude( out, reserved, 0 );
	if( pcm == NULL || channels <= 0 || rate <= 0 )
	{
		return;
	}
	const int64 frames = bytes / ( sizeof( int16 ) * channels );
	const int64 entries = ( frames * 60 + rate - 1 ) / rate;
	if( frames <= 0 || entries <= 0 || entries > 60 * 60 * 60 )
	{
		return;
	}
	if( !SoundResizeAmplitude( out, reserved, static_cast<int>( entries ) ) ) return;
	for( int64 entry = 0; entry < entries; entry++ )
	{
		const int64 first = ( entry * rate / 60 ) * channels;
		const int64 last = Min( frames, ( entry + 1 ) * rate / 60 ) * channels;
		int peak = 0;
		for( int64 i = first; i < last; i++ )
		{
			const int value = pcm[i] < 0 ? -pcm[i] : pcm[i];
			peak = ( value > peak ) ? value : peak;
		}
		out[static_cast<int>( entry )] = static_cast<byte>( Min( 255, ( peak * 255 + 16383 ) / 32767 ) );
	}
}

void idSoundSample_OpenAL::CreateOpenALBuffer()
{
	if( IsStreamed() ) return;
	if( pendingDecode != NULL )
	{
		// uploads once the PCM is in
		FinishDecode();
		return;
	}
	if( openalBuffer != 0 || openalBufferUploadFailed )
	{
		return;
	}

	// FreeData() empties the buffer list; there is nothing to upload until the
	// sample is loaded again
	if( buffers.Num() <= 0 || buffers[0].buffer == NULL )
	{
		return;
	}

	if( !openQ4_CanUploadSampleToOpenAL() )
	{
		return;
	}

	// build OpenAL buffer
	CheckALErrors();
	alGenBuffers( 1, &openalBuffer );

	const ALenum allocationError = CheckALErrors();
	if( allocationError != AL_NO_ERROR || openalBuffer == 0 || !alIsBuffer( openalBuffer ) )
	{
		if( openalBuffer != 0 && alIsBuffer( openalBuffer ) )
		{
			alDeleteBuffers( 1, &openalBuffer );
			CheckALErrors();
		}
		openalBuffer = 0;
		openalBufferUploadFailed = true;
		if( !openQ4_openALBufferAllocationWarningIssued.exchange( true, std::memory_order_relaxed ) )
		{
			common->Warning(
				"OpenAL could not allocate another sample buffer (error 0x%x, sample '%s'); "
				"continuing with streaming fallback where resources permit. Further allocation failures are suppressed.",
				allocationError,
				GetName() );
		}
		return;
	}

	if( alIsBuffer( openalBuffer ) )
	{
		CheckALErrors();

		void* buffer = NULL;
		uint32 bufferSize = 0;

		if( format.basic.formatTag == idWaveFile::FORMAT_ADPCM )
		{
			// RB: decode idWaveFile::FORMAT_ADPCM to idWaveFile::FORMAT_PCM

			buffer = buffers[0].buffer;
			bufferSize = buffers[0].bufferSize;

			const uint64 refusedBeforeDecode = soundMemoryRejected;
			if( MS_ADPCM_decode( ( uint8** ) &buffer, &bufferSize ) < 0 )
			{
				if( soundMemoryRejected == refusedBeforeDecode ) idLib::Warning( "idSoundSample_OpenAL::CreateOpenALBuffer: could not decode ADPCM '%s' to 16 bit format", GetName() );
				alDeleteBuffers( 1, &openalBuffer );
				CheckALErrors();
				openalBuffer = 0;
				openalBufferUploadFailed = true;
				// A decode refused under pressure must be retried as a load,
				// not retained forever as unplayable encoded PCM-fallback data.
				FreeData();
				return;
			}

			buffers[0].buffer = buffer;
			buffers[0].bufferSize = bufferSize;

			totalBufferSize = bufferSize;
		}
		else if( format.basic.formatTag == idWaveFile::FORMAT_XMA2 || format.basic.formatTag == idWaveFile::FORMAT_EXTENSIBLE )
		{
			// LoadWav refuses XMA2 and turns extensible PCM into plain PCM, so this is only a guard
			idLib::Warning( "idSoundSample_OpenAL::CreateOpenALBuffer: '%s' is not 16-bit PCM", GetName() );
			alDeleteBuffers( 1, &openalBuffer );
			CheckALErrors();
			openalBuffer = 0;
			openalBufferUploadFailed = true;
			return;
		}
		else
		{
			// TODO concatenate buffers

			assert( buffers.Num() == 1 );

			buffer = buffers[0].buffer;
			bufferSize = buffers[0].bufferSize;
		}

#if 0 //#if defined(AL_SOFT_buffer_samples)
		if( alIsExtensionPresent( "AL_SOFT_buffer_samples" ) )
		{
			ALenum type = AL_SHORT_SOFT;

			if( format.basic.bitsPerSample != 16 )
			{
				//common->Error( "idSoundSample_OpenAL::LoadResource: '%s' not a 16 bit format", GetName() );
			}

			ALenum channels = NumChannels() == 1 ? AL_MONO_SOFT : AL_STEREO_SOFT;
			ALenum alFormat = GetOpenALSoftFormat( channels, type );

			alBufferSamplesSOFT( openalBuffer, format.basic.samplesPerSec, alFormat, BytesToFrames( bufferSize, channels, type ), channels, type, buffer );
		}
		else
#endif
		{
			if( amplitude.Num() == 0 && !IsDefault() )
			{
				SoundSample_BuildAmplitudeEnvelope( static_cast<const int16*>( buffer ), bufferSize, NumChannels(), format.basic.samplesPerSec, amplitude, amplitudeReserved );
			}
			if( !SoundMemoryAcquire( bufferSize ) ) {
				alDeleteBuffers( 1, &openalBuffer );
				openalBuffer = 0;
				openalBufferUploadFailed = true;
				return; // CPU bytes remain available to the bounded queue fallback.
			}
			openalReserved = bufferSize;
			alBufferData( openalBuffer, GetOpenALBufferFormat(), buffer, bufferSize, format.basic.samplesPerSec );
		}

		const ALenum uploadError = CheckALErrors();
		if( uploadError != AL_NO_ERROR )
		{
			SoundMemoryRelease( openalReserved ); openalReserved = 0;
			const ALuint failedBuffer = openalBuffer;
			openalBuffer = 0;
			if( alIsBuffer( failedBuffer ) )
			{
				alDeleteBuffers( 1, &failedBuffer );
				CheckALErrors();
			}
			openalBufferUploadFailed = true;
			common->Warning(
				"OpenAL could not upload sample '%s' (error 0x%x); continuing with streaming fallback where resources permit.",
				GetName(),
				uploadError );
		}

		// OpenAL owns a copy of these bytes now; ours is redundant
		ReleaseCpuPayload();
	}
}

/*
========================
idSoundSample_OpenAL::ReleaseCpuPayload

alBufferData copied every byte into OpenAL's own storage, so what is left in
buffers[] is a second resident copy of the same PCM. Free the payload but keep
the sampleBuffer_t entries: RestartAt and GetPlayableBufferRange navigate a
sample through numSamples and bufferSize, and only the streaming submit path
dereferences the bytes.

Not done for a sample split across several buffers. CreateOpenALBuffer only
uploads the single-buffer case -- it asserts as much -- so a multi-buffer sample
has no complete OpenAL copy to fall back on.
========================
*/
void idSoundSample_OpenAL::ReleaseCpuPayload()
{
	if( payloadReleased || keepPayload || !s_releaseSamplePayload.GetBool() )
	{
		return;
	}
	if( openalBuffer == 0 || buffers.Num() != 1 )
	{
		return;
	}

	FreeBuffer( buffers[0].buffer );
	buffers[0].buffer = NULL;
	payloadReleased = true;
}

/*
========================
idSoundSample_OpenAL::ReleaseCpuCopy

ReleaseCpuPayload for memory pressure: keepPayload and s_releaseSamplePayload
keep a copy only to save a reload, which a low-memory notice outweighs.
========================
*/
int idSoundSample_OpenAL::ReleaseCpuCopy()
{
	if( !HasCpuCopy() )
	{
		return 0;
	}
	const int bytes = buffers[0].bufferSize;
	FreeBuffer( buffers[0].buffer );
	buffers[0].buffer = NULL;
	payloadReleased = true;
	keepPayload = false;
	return bytes;
}

/*
========================
idSoundSample_OpenAL::EnsureCpuPayload

Restore through a temporary sample. Other voices may still be using our OpenAL
buffer, so reloading this object would delete an attached buffer and invalidate
their playback state. Only transfer matching CPU bytes; retain our metadata and
OpenAL handle.
========================
*/
bool idSoundSample_OpenAL::EnsureCpuPayload()
{
	// Whole-track access is unavailable for compressed streams; callers must
	// use the per-voice decoder instead of silently defeating the memory bound.
	if( IsStreamed() ) return false;
	FinishDecode();
	if( !payloadReleased )
	{
		return buffers.Num() > 0 && buffers[0].buffer != NULL;
	}

	// Set before the reload, not after: LoadResource ends by uploading to
	// OpenAL, which would otherwise release the payload we are restoring.
	keepPayload = true;

	// This costs a synchronous decode in the middle of starting a sound, so it
	// is worth seeing when it happens. It should be rare and it should never
	// repeat for the same sample -- keepPayload is sticky.
	if( s_debugHardware.GetBool() )
	{
		idLib::Printf( "%dms: reloading released payload for %s\n", Sys_Milliseconds(), GetName() );
	}
	idSoundSample_OpenAL restored;
	restored.SetName( GetName() );
	restored.keepPayload = true;
	restored.LoadResource();
	if( !restored.loaded || buffers.Num() != 1 || restored.buffers.Num() != 1 ||
		restored.buffers[0].buffer == NULL ||
		memcmp( &format, &restored.format, sizeof( format ) ) != 0 ||
		playBegin != restored.playBegin || playLength != restored.playLength ||
		buffers[0].numSamples != restored.buffers[0].numSamples ||
		buffers[0].bufferSize != restored.buffers[0].bufferSize )
	{
		// A missing or replaced source must not change already playing voices.
		restored.FreeData();
		return false;
	}
	buffers[0].buffer = restored.buffers[0].buffer;
	restored.buffers[0].buffer = NULL;
	payloadReleased = false;
	restored.FreeData();
	return true;
}

/*
========================
idSoundSample_OpenAL::LoadWav
========================
*/
// A failed attempt frees what it read and returns false; LoadResource tries the next
// loader and language variant and only falls back to the default sound when all fail.
// Making the default here uploaded the beep, and a later variant that loaded found the
// OpenAL buffer taken and played the beep in its place.
bool idSoundSample_OpenAL::LoadWav( const idStr& filename )
{

	// load the wave
	idWaveFile wave;
	if( !wave.Open( filename ) )
	{
		return false;
	}

	idStr sampleName = filename;
	sampleName.SetFileExtension( "amp" );
	LoadAmplitude( sampleName );

	const char* formatError = wave.ReadWaveFormat( format );
	if( formatError != NULL )
	{
		idLib::Warning( "LoadWav( %s ) : %s", filename.c_str(), formatError );
		FreeData();
		return false;
	}
	timestamp = wave.Timestamp();

	const uint32 dataChunkSize = wave.SeekToChunk( 'data' );
	if( dataChunkSize == 0 || dataChunkSize > ( uint32 )idMath::INT_MAX || format.basic.blockSize == 0 )
	{
		idLib::Warning( "LoadWav( %s ) : invalid data chunk", filename.c_str() );
		FreeData();
		return false;
	}
	totalBufferSize = ( int )dataChunkSize;

	if( format.basic.formatTag == idWaveFile::FORMAT_PCM || format.basic.formatTag == idWaveFile::FORMAT_EXTENSIBLE )
	{

		if( format.basic.bitsPerSample != 16 )
		{
			idLib::Warning( "LoadWav( %s ) : %s", filename.c_str(), "Not a 16 bit PCM wav file" );
			FreeData();
			return false;
		}
		// the OpenAL upload is mono or stereo 16-bit; anything else played as noise
		if( format.basic.numChannels < 1 || format.basic.numChannels > 2 || format.basic.blockSize != format.basic.numChannels * 2 )
		{
			idLib::Warning( "LoadWav( %s ) : %d channels with %d byte frames; only mono and stereo 16-bit PCM play", filename.c_str(), format.basic.numChannels, format.basic.blockSize );
			FreeData();
			return false;
		}
		if( totalBufferSize % format.basic.blockSize != 0 )
		{
			idLib::Warning( "LoadWav( %s ) : %s", filename.c_str(), "PCM data is not block aligned" );
			FreeData();
			return false;
		}

		playBegin = 0;
		playLength = ( totalBufferSize ) / format.basic.blockSize;

		buffers.SetNum( 1 );
		buffers[0].bufferSize = totalBufferSize;
		buffers[0].numSamples = playLength;
		buffers[0].buffer = AllocBuffer( totalBufferSize, GetName() );

		if( buffers[0].buffer == NULL ) { FreeData(); return false; }
		if( !openQ4_ReadWaveExact( wave, buffers[0].buffer, totalBufferSize ) )
		{
			idLib::Warning( "LoadWav( %s ) : could not read PCM data", filename.c_str() );
			FreeData();
			return false;
		}

		if( format.basic.bitsPerSample == 16 )
		{
			idSwap::LittleArray( ( short* )buffers[0].buffer, totalBufferSize / sizeof( short ) );
		}

		buffers[0].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[0].buffer );

	}
	else if( format.basic.formatTag == idWaveFile::FORMAT_ADPCM )
	{
		size_t decodedBytes = 0;
		if( !openQ4_GetMSADPCMDecodedSize( dataChunkSize, format.basic.blockSize,
			format.extra.adpcm.samplesPerBlock, format.basic.numChannels,
			format.extra.adpcm.numCoef, decodedBytes ) )
		{
			idLib::Warning( "LoadWav( %s ) : invalid ADPCM block layout", filename.c_str() );
			FreeData();
			return false;
		}

		playBegin = 0;
		playLength = static_cast<int>( decodedBytes /
			( static_cast<size_t>( format.basic.numChannels ) * sizeof( int16_t ) ) );

		buffers.SetNum( 1 );
		buffers[0].bufferSize = totalBufferSize;
		buffers[0].numSamples = playLength;
		buffers[0].buffer  = AllocBuffer( totalBufferSize, GetName() );

		if( buffers[0].buffer == NULL ) { FreeData(); return false; }
		if( !openQ4_ReadWaveExact( wave, buffers[0].buffer, totalBufferSize ) )
		{
			idLib::Warning( "LoadWav( %s ) : could not read ADPCM data", filename.c_str() );
			FreeData();
			return false;
		}
		// The decoder refuses a block whose coefficient selector is out of range, and that
		// refusal at upload ended the session; refuse the file here instead.
		const uint8* adpcm = static_cast<const uint8*>( buffers[0].buffer );
		for( uint32 blockOffset = 0; blockOffset < dataChunkSize; blockOffset += format.basic.blockSize )
		{
			for( int channel = 0; channel < format.basic.numChannels; channel++ )
			{
				if( adpcm[ blockOffset + channel ] >= format.extra.adpcm.numCoef || adpcm[ blockOffset + channel ] >= 7 )
				{
					idLib::Warning( "LoadWav( %s ) : ADPCM block at byte %u selects coefficient %d of %d", filename.c_str(), blockOffset, adpcm[ blockOffset + channel ], format.extra.adpcm.numCoef );
					FreeData();
					return false;
				}
			}
		}

		buffers[0].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[0].buffer );

	}
	else if( format.basic.formatTag == idWaveFile::FORMAT_XMA2 )
	{
		// BFG's Xbox 360 format. Nothing on PC decodes it, and accepting it ended the
		// session at upload time.
		idLib::Warning( "LoadWav( %s ) : XMA2 is an Xbox 360 format and cannot be played", filename.c_str() );
		FreeData();
		return false;
	}
	else
	{
		idLib::Warning( "LoadWav( %s ) : Unsupported wave format %d", filename.c_str(), format.basic.formatTag );
		FreeData();
		return false;
	}

	wave.Close();

	if( format.basic.formatTag == idWaveFile::FORMAT_EXTENSIBLE )
	{
		// HACK: XAudio2 doesn't really support FORMAT_EXTENSIBLE so we convert it to a basic format after extracting the channel mask
		format.basic.formatTag = format.extra.extensible.subFormat.data1;
	}

	// sanity check...
	assert( buffers[buffers.Num() - 1].numSamples == playBegin + playLength );

	return true;
}

/*
========================
idSoundSampleDecode

One Ogg sample decoding on a job worker. The main thread opened the stream, so the
sample's length, rate and channels are already known and only the PCM is outstanding;
the job writes nothing but the pcm frames and decodedFrames. Whoever waits on the list
owns the rest again.
========================
*/
struct idSoundSampleDecode
{
	stb_vorbis*					vorbis = NULL;		// opened on the main thread, or NULL for the job to open
	std::unique_ptr<char[]>	arena;
	byte*						fileData = NULL;
	int							fileLen = 0;
	short*						pcm = NULL;
	unsigned int				capacityFrames = 0;
	int							channels = 0;
	int							sampleRate = 0;
	unsigned int				decodedFrames = 0;
	bool						headerMismatch = false;	// the job's open disagreed with SoundSample_PeekOggInfo
	uint64					reservedBytes = 0;	// main-thread admission, held until joined/cancelled
	std::unique_ptr<idJobList>	list;
	// A streamed track: the job builds only its 60 Hz envelope, into the sample's
	// amplitude list sized before submission, and keeps no PCM.
	byte*						envelope = NULL;
	int							envelopeEntries = 0;
};

// The peak of each 1/60 s of PCM as 0-255, accumulated a block at a time for a
// stream that is never resident: the same values SoundSample_BuildAmplitudeEnvelope
// gives a whole sample. Touches only the envelope it is given.
static void SoundSample_AccumulateEnvelope( const short* pcm, int count, int channels, int rate, uint64 firstFrame, byte* envelope, int entries )
{
	for( int frame = 0; frame < count; ++frame ) {
		int peak = 0;
		for( int channel = 0; channel < channels; ++channel ) {
			const int sample = pcm[frame * channels + channel];
			peak = Max( peak, sample < 0 ? -sample : sample );
		}
		const int entry = static_cast<int>( ( ( firstFrame + frame + 1 ) * 60 - 1 ) / rate );
		if( entry < entries ) envelope[entry] = Max( envelope[entry], static_cast<byte>( Min( 255, ( peak * 255 + 16383 ) / 32767 ) ) );
	}
}

idCVar s_asyncSampleDecode( "s_asyncSampleDecode", "1", CVAR_BOOL, "decode the Ogg samples a level load references on job workers, while the rest of the level loads" );
// The largest installed retail/Awakening input+PCM pair is below 48 MiB.
// A lower mobile default must wait for compressed streaming of those tracks.
idCVar s_decodeMemoryMB( "s_decodeMemoryMB", "64", CVAR_SOUND | CVAR_ARCHIVE | CVAR_INTEGER,
	"maximum MiB of compressed input, pending Vorbis PCM and fixed decoder arenas; excludes resident/OpenAL data", 16, 1024 );

// Decodes this many samples at once at most; beyond it LoadSample decodes on the
// main thread, which keeps the job queue open for the rest of the level load. A
// finished decode stops counting once LoadSample uploads it (FinishSampleDecodes).
static const int SOUND_SAMPLE_MAX_PENDING_DECODES = 48;
static int soundSamplePendingDecodes = 0;
static uint64 soundSamplePendingDecodeBytes = 0;
static uint64 soundSampleDecodePeakBytes = 0;
static uint64 soundSampleDecodeWaits = 0;

// All admission and release happens on the main thread. Worker completion alone
// cannot release the reservation: its PCM still belongs to an unjoined sample.
static bool SoundSample_AdmitDecode( const uint64 bytes )
{
	const uint64 budget = static_cast<uint64>( idMath::ClampInt( 16, 1024, s_decodeMemoryMB.GetInteger() ) ) * 1024 * 1024;
	if( bytes > budget )
	{
		return false;
	}
	if( soundSamplePendingDecodeBytes > budget - bytes )
	{
		++soundSampleDecodeWaits;
		// Do not mutate decodingSamples here. A failed decode can synchronously
		// probe another format while FinishSampleDecodes is already iterating it.
		for( int i = 0; i < soundSystemLocal.samples.Num() && soundSamplePendingDecodeBytes > budget - bytes; ++i )
		{
			soundSystemLocal.samples[i]->FinishDecode();
		}
	}
	if( soundSamplePendingDecodeBytes > budget - bytes )
	{
		return false;
	}
	soundSampleDecodePeakBytes = Max( soundSampleDecodePeakBytes, soundSamplePendingDecodeBytes + bytes );
	return true;
}

class SoundSampleDecodeReservation
{
public:
	SoundSampleDecodeReservation() = default;
	SoundSampleDecodeReservation( const SoundSampleDecodeReservation& ) = delete;
	SoundSampleDecodeReservation& operator=( const SoundSampleDecodeReservation& ) = delete;
	~SoundSampleDecodeReservation() { soundSamplePendingDecodeBytes -= bytes; }
	bool Grow( uint64 requested ) {
		if( requested <= bytes ) return true;
		if( !SoundSample_AdmitDecode( requested - bytes ) ) return false;
		if( !arena.Reserve( SoundVorbisStream::ArenaBytes ) ) return false;
		soundSamplePendingDecodeBytes += requested - bytes;
		bytes = requested;
		return true;
	}
	uint64 Transfer() { arena.Transfer(); const uint64 result = bytes; bytes = 0; return result; }
private:
	uint64 bytes = 0;
	SoundMemoryReservation arena;
};

void idSoundSample_OpenAL::PrintDecodeMemoryInfo()
{
	idLib::Printf( "Audio admission: reserved=%llu KiB peak=%llu KiB budget=%d MiB refused=%llu fixed=%llu KiB (managed payloads; excludes provider internals and metadata)\n",
		static_cast<unsigned long long>( ( soundMemory.Used() + SOUND_MEMORY_FIXED_BYTES ) / 1024 ),
		static_cast<unsigned long long>( ( soundMemory.Peak() + SOUND_MEMORY_FIXED_BYTES ) / 1024 ), s_audioMemoryMB.GetInteger(),
		static_cast<unsigned long long>( soundMemoryRejected ), static_cast<unsigned long long>( SOUND_MEMORY_FIXED_BYTES / 1024 ) );
	uint64 cpu = 0, uploaded = 0, compressedPending = 0, envelopes = 0, decoderArenas = 0;
	for( int i = 0; i < soundSystemLocal.samples.Num(); ++i )
	{
		const idSoundSample_OpenAL* sample = soundSystemLocal.samples[i];
		cpu += sample->ResidentBufferSize();
		envelopes += sample->amplitude.Allocated();
		if( sample->openalBuffer != 0 )
		{
			ALint bytes = 0;
			alGetBufferi( sample->openalBuffer, AL_SIZE, &bytes );
			if( CheckALErrors() == AL_NO_ERROR && bytes > 0 ) uploaded += bytes;
		}
		if( sample->pendingDecode != NULL ) {
			compressedPending += sample->pendingDecode->fileLen;
			decoderArenas += SoundVorbisStream::ArenaBytes;
		}
	}
	const uint64 queued = soundSystemLocal.hardware.QueuedBufferBytes();
	// Pending reservations include input, decoder arenas AND not-yet-published
	// PCM. Stream queues may be counted again here: this is a conservative sum
	// of owned payloads/reservations, not an estimate of provider process memory.
	idLib::Printf( "Audio memory: CPU=%llu KiB OpenAL=%llu KiB pending-input<=%llu KiB aggregate<=%llu KiB\n",
		static_cast<unsigned long long>( cpu / 1024 ), static_cast<unsigned long long>( uploaded / 1024 ),
		static_cast<unsigned long long>( compressedPending / 1024 ),
		static_cast<unsigned long long>( ( cpu + uploaded + soundSamplePendingDecodeBytes + soundStreamBytes + envelopes + queued ) / 1024 ) );
	idLib::Printf( "Audio queues: %llu KiB (included conservatively; stream reservations also include their queues)\n",
		static_cast<unsigned long long>( queued / 1024 ) );
	idLib::Printf( "Audio auxiliary: envelopes=%llu KiB pending-arenas<=%llu KiB stream-reserved=%llu KiB (included above)\n",
		static_cast<unsigned long long>( envelopes / 1024 ), static_cast<unsigned long long>( decoderArenas / 1024 ),
		static_cast<unsigned long long>( soundStreamBytes / 1024 ) );
	idLib::Printf( "Vorbis admission: pending=%llu KiB peak=%llu KiB budget=%d MiB waits=%llu (includes decoder arenas; excludes OpenAL internals)\n",
		static_cast<unsigned long long>( soundSamplePendingDecodeBytes / 1024 ), static_cast<unsigned long long>( soundSampleDecodePeakBytes / 1024 ),
		s_decodeMemoryMB.GetInteger(), static_cast<unsigned long long>( soundSampleDecodeWaits ) );
	idSoundOggStream::PrintMemoryInfo();
}

// One Vorbis frame per call, straight into place: what stb_vorbis_decode_memory loops
// on. Thread-safe: it touches only the stream and the output it is given.
static void SoundSample_DecodeFrames( stb_vorbis* vorbis, const int channels, short* pcm, const unsigned int capacityFrames, unsigned int& decodedFrames, const idJobContext* context )
{
	while( decodedFrames < capacityFrames )
	{
		if( context != NULL && context->IsCancellationRequested() ) return;
		const int frames = stb_vorbis_get_frame_short_interleaved( vorbis, channels, pcm + ( size_t )decodedFrames * channels,
			static_cast<int>( ( capacityFrames - decodedFrames ) * channels ) );
		if( frames <= 0 )
		{
			break;
		}
		decodedFrames += static_cast<unsigned int>( frames );
	}
}

/*
========================
SoundSample_PeekOggInfo

The channels and rate from the identification header and the length from the
end-of-stream page's granule position, without stb_vorbis's codebook setup (about
0.3 ms a file, most of what LoadSample spent on the main thread). The job's own open
checks all three, so a stream this misreads loads the ordinary way.
========================
*/
static uint32 SoundSample_ReadLE32( const byte* p )
{
	return ( uint32 )p[0] | ( ( uint32 )p[1] << 8 ) | ( ( uint32 )p[2] << 16 ) | ( ( uint32 )p[3] << 24 );
}

static bool SoundSample_PeekOggInfo( const byte* data, const int length, int& channels, int& sampleRate, unsigned int& frames )
{
	if( data == NULL || length < 58 || memcmp( data, "OggS", 4 ) != 0 )
	{
		return false;
	}
	const int firstPacket = 27 + data[26];
	if( firstPacket + 16 > length || data[firstPacket] != 0x01 || memcmp( data + firstPacket + 1, "vorbis", 6 ) != 0 )
	{
		return false;
	}
	channels = data[firstPacket + 11];
	sampleRate = static_cast<int>( SoundSample_ReadLE32( data + firstPacket + 12 ) );

	// the last page: flagged end-of-stream and ending exactly at the end of the file
	const int earliest = Max( 0, length - 65536 - 27 );
	for( int page = length - 27; page >= earliest; page-- )
	{
		if( data[page] != 'O' || memcmp( data + page, "OggS", 4 ) != 0 || data[page + 4] != 0 || ( data[page + 5] & 0x04 ) == 0 )
		{
			continue;
		}
		const int segments = data[page + 26];
		if( page + 27 + segments > length )
		{
			continue;
		}
		int pageLength = 27 + segments;
		for( int segment = 0; segment < segments; segment++ )
		{
			pageLength += data[page + 27 + segment];
		}
		if( page + pageLength != length )
		{
			continue;
		}
		const uint32 granuleLow = SoundSample_ReadLE32( data + page + 6 );
		const uint32 granuleHigh = SoundSample_ReadLE32( data + page + 10 );
		if( granuleHigh != 0 || granuleLow == 0 || granuleLow == 0xffffffff )
		{
			return false;
		}
		frames = granuleLow;
		return true;
	}
	return false;
}

// The envelope of a stream counted as it decodes: grows about a minute of entries
// at a time, every entry reserved against the audio budget like the fixed case.
static bool SoundGrowAmplitude( idList<byte>& out, uint64& reserved, int count )
{
	if( count <= out.Num() ) return true;
	if( count > out.NumAllocated() ) {
		const int capacity = Max( count, out.NumAllocated() + 60 * 60 );
		const uint64 bytes = ( static_cast<uint64>( capacity ) + 15 ) / 16 * 16;
		if( bytes > reserved ) {
			if( !SoundMemoryAcquire( bytes - reserved ) ) return false;
			reserved = bytes;
		}
		out.Resize( static_cast<int>( bytes ) );
	}
	const int old = out.Num();
	out.SetNum( count, false );
	memset( out.Ptr() + old, 0, count - old );
	return true;
}

/*
========================
SoundSample_ProbeOggStream

The decoder's own view of a stream SoundSample_PeekOggInfo could not read: the
peek needs a final end-of-stream page that ends the file, while stb_vorbis also
finds the length of a truncated stream or one followed by other bytes. frames is
0 for a stream that has no length at all. Reads only the headers and final pages.
========================
*/
static bool SoundSample_ProbeOggStream( idFile* file, int& channels, int& rate, unsigned int& frames )
{
	SoundSampleDecodeReservation reservation;
	if( !reservation.Grow( SOUND_STREAM_BYTES ) ) return false;
	SoundVorbisStream stream;
	const bool opened = stream.Open( SoundStream_ReadAt, file, file->Length() );
	if( opened ) {
		channels = stream.Channels();
		rate = stream.Rate();
		frames = stream.Frames();
	}
	stream.Close();
	file->Rewind();
	return opened;
}

bool idSoundSample_OpenAL::LoadOggStream( const idStr& filename )
{
	// Loading metadata/envelopes must not consume the live-voice pool: a newly
	// requested line can be loaded while every playback decoder is occupied.
	SoundSampleDecodeReservation reservation;
	if( !reservation.Grow( SOUND_STREAM_BYTES ) ) return false;
	idFileLocal input( fileSystem->OpenFileRead( filename ) );
	SoundVorbisStream stream;
	if( input == NULL || input->Length() <= 0 || !stream.Open( SoundStream_ReadAt, input, input->Length() ) ) return false;
	const int channels = stream.Channels(), rate = stream.Rate();
	unsigned int frames = stream.Frames();
	// A stream without length metadata is counted by the envelope pass below. Its
	// decoders can seek once they know that length and the last page that carries
	// a position; a stream with no positioned page at all decodes whole instead.
	const bool counted = frames == 0;
	stb_vorbis_openq4_granule_page lastPage = {};
	if( counted && !stream.FindLastGranulePage( lastPage ) ) return false;
	// The decoded size must fit the sample's int-sized PCM bookkeeping: about three
	// hours of 48 kHz stereo, more at the stock rates. The envelope is 60 bytes a second.
	if( !counted && static_cast<uint64>( frames ) * channels * sizeof( short ) > idMath::INT_MAX ) return false;

	// Build the same 60 Hz envelope as resident PCM, with one bounded block.
	// Virtualized speech, shaking and flicker can query any offset afterwards.
	const int entries = counted ? 0 : static_cast<int>( ( static_cast<uint64>( frames ) * 60 + rate - 1 ) / rate );
	if( !SoundResizeAmplitude( amplitude, amplitudeReserved, entries ) ) return false;
	if( entries > 0 ) memset( amplitude.Ptr(), 0, entries );
	short pcm[SoundVorbisStream::ChunkFrames * 2];
	unsigned int decoded = 0;
	while( counted || decoded < frames ) {
		const int count = stream.Read( pcm, counted ? SoundVorbisStream::ChunkFrames : Min( SoundVorbisStream::ChunkFrames, static_cast<int>( frames - decoded ) ) );
		if( count <= 0 && counted && decoded > 0 ) break;
		if( count <= 0 ) { SoundResizeAmplitude( amplitude, amplitudeReserved, 0 ); return false; }
		if( counted ) {
			const uint64 total = static_cast<uint64>( decoded ) + count;
			if( total * channels * sizeof( short ) > idMath::INT_MAX ||
				!SoundGrowAmplitude( amplitude, amplitudeReserved, static_cast<int>( ( total * 60 + rate - 1 ) / rate ) ) ) {
				SoundResizeAmplitude( amplitude, amplitudeReserved, 0 );
				return false;
			}
		}
		SoundSample_AccumulateEnvelope( pcm, count, channels, rate, decoded, amplitude.Ptr(), amplitude.Num() );
		decoded += count;
	}
	if( counted ) {
		// Every decoder of this stream gets the same length; the first proves it works.
		if( !stream.SetCountedLength( decoded, lastPage ) ) { SoundResizeAmplitude( amplitude, amplitudeReserved, 0 ); return false; }
		frames = decoded;
		streamCountedLength = true;
		streamPageStart = lastPage.page_start;
		streamPageEnd = lastPage.page_end;
		streamPageGranule = lastPage.granule;
	}
	buffers.SetNum( 1 );
	buffers[0].buffer = NULL;
	FinishOggLoad( channels, rate, frames );
	streamName = filename;
	if( s_debugHardware.GetBool() ) idLib::Printf( "Compressed stream: %s, %d Hz, %d channels, %u frames%s\n", filename.c_str(), rate, channels, frames, counted ? " (counted; no length metadata)" : "" );
	return true;
}

/*
========================
idSoundSample_OpenAL::BeginOggStreamEnvelope

LoadOggStream for a level load. The track streams from its file, so only its
60 Hz envelope is outstanding: the compressed bytes go to a job worker, which
fills the envelope sized here while the rest of the level loads (the full pass
took the main thread a tenth to a third of a second per music track). Until
FinishDecode joins the job the envelope reads silent; voices can already stream.
False leaves the sample untouched for the synchronous path.
========================
*/
static void SoundSample_RunDecode( idSoundSampleDecode& decode, const idJobContext* context = NULL );
static void SoundSample_DecodeJob( const idJobContext& context );

bool idSoundSample_OpenAL::BeginOggStreamEnvelope( const idStr& filename, idFile* file, int channels, int rate, unsigned int frames, idSoundSampleDecode** asyncDecode )
{
	if( asyncDecode == NULL || frames == 0 || channels < 1 || channels > 2 || rate <= 0 ||
		soundSamplePendingDecodes >= SOUND_SAMPLE_MAX_PENDING_DECODES || !jobSystem.IsInitialized() || jobSystem.IsSynchronous() ||
		static_cast<uint64>( frames ) * channels * sizeof( short ) > idMath::INT_MAX )
	{
		return false;
	}
	const int fileLen = file->Length();
	SoundSampleDecodeReservation reservation;
	if( fileLen <= 0 || !reservation.Grow( static_cast<uint64>( fileLen ) + SoundVorbisStream::ArenaBytes ) )
	{
		return false;
	}
	byte* fileData = static_cast<byte*>( AllocBuffer( fileLen, filename.c_str() ) );
	if( fileData == NULL )
	{
		return false;
	}
	file->Rewind();
	const bool read = file->Read( fileData, fileLen ) == fileLen;
	file->Rewind();
	const int entries = static_cast<int>( ( static_cast<uint64>( frames ) * 60 + rate - 1 ) / rate );
	if( !read || !SoundResizeAmplitude( amplitude, amplitudeReserved, entries ) )
	{
		FreeBuffer( fileData );
		return false;
	}
	memset( amplitude.Ptr(), 0, entries );

	idSoundSampleDecode* decode = new idSoundSampleDecode;
	decode->fileData = fileData;
	decode->fileLen = fileLen;
	decode->capacityFrames = frames;
	decode->channels = channels;
	decode->sampleRate = rate;
	decode->envelope = amplitude.Ptr();
	decode->envelopeEntries = entries;
	decode->list = jobSystem.CreateJobList( "sound-stream-envelope", 1, 0, idJobPriority::LOW );
	const bool submitted = decode->list != nullptr && decode->list->AddJob( &SoundSample_DecodeJob, decode ) &&
		decode->list->Submit() == idJobSubmitResult::ACCEPTED;
	if( !submitted && decode->list != nullptr )
	{
		decode->list->Wait();
	}
	buffers.SetNum( 1 );
	buffers[0].buffer = NULL;
	FinishOggLoad( channels, rate, frames );
	streamName = filename;
	if( !submitted )
	{
		// rejected or run inline by the job system: finish it here
		SoundSample_RunDecode( *decode );
		const bool damaged = decode->headerMismatch;
		delete decode;
		if( damaged )
		{
			FreeData();
			return false;
		}
		*asyncDecode = NULL;
		return true;
	}
	soundSamplePendingDecodes++;
	decode->reservedBytes = reservation.Transfer();
	*asyncDecode = decode;
	if( s_debugHardware.GetBool() ) idLib::Printf( "Compressed stream: %s, %d Hz, %d channels, %u frames (envelope on a job worker)\n", filename.c_str(), rate, channels, frames );
	return true;
}

// Opens the stream if the main thread did not, decodes it and releases the stream and
// the file bytes. Runs on a worker, or inline.
static void SoundSample_RunDecode( idSoundSampleDecode& decode, const idJobContext* context )
{
	// Cancellation stops between bounded codec blocks, including before setup.
	// Keep the cursor, input and reservation until the main thread joins: FreeData
	// discards them, while FinishDecode can resume a globally cancelled job inline.
	if( context != NULL && context->IsCancellationRequested() ) return;
	if( decode.vorbis == NULL && decode.fileData != NULL && !decode.headerMismatch )
	{
		int vorbisError = 0;
		decode.arena.reset( new (std::nothrow) char[SoundVorbisStream::ArenaBytes] );
		stb_vorbis_alloc alloc = { decode.arena.get(), SoundVorbisStream::ArenaBytes };
		if( decode.arena ) decode.vorbis = stb_vorbis_open_memory( decode.fileData, decode.fileLen, &vorbisError, &alloc );
		if( decode.vorbis != NULL )
		{
			const stb_vorbis_info info = stb_vorbis_get_info( decode.vorbis );
			if( info.channels != decode.channels || static_cast<int>( info.sample_rate ) != decode.sampleRate ||
					stb_vorbis_stream_length_in_samples( decode.vorbis ) != decode.capacityFrames )
			{
				decode.headerMismatch = true;
				stb_vorbis_close( decode.vorbis );
				decode.vorbis = NULL;
			}
		}
		else
		{
			decode.headerMismatch = true;
		}
	}
	if( decode.vorbis != NULL && decode.envelope != NULL )
	{
		// A streamed track: only its envelope, a block at a time; no PCM is kept.
		// One that decodes short is damaged and loads the ordinary way.
		short block[SoundVorbisStream::ChunkFrames * 2];
		while( decode.decodedFrames < decode.capacityFrames )
		{
			if( context != NULL && context->IsCancellationRequested() ) return;
			const int count = stb_vorbis_get_samples_short_interleaved( decode.vorbis, decode.channels, block,
				Min( SoundVorbisStream::ChunkFrames, static_cast<int>( decode.capacityFrames - decode.decodedFrames ) ) * decode.channels );
			if( count <= 0 )
			{
				break;
			}
			SoundSample_AccumulateEnvelope( block, count, decode.channels, decode.sampleRate, decode.decodedFrames, decode.envelope, decode.envelopeEntries );
			decode.decodedFrames += count;
		}
		decode.headerMismatch = decode.decodedFrames != decode.capacityFrames;
		stb_vorbis_close( decode.vorbis );
		decode.vorbis = NULL;
	}
	if( decode.vorbis != NULL )
	{
		SoundSample_DecodeFrames( decode.vorbis, decode.channels, decode.pcm, decode.capacityFrames, decode.decodedFrames, context );
		if( context != NULL && context->IsCancellationRequested() ) return;
		stb_vorbis_close( decode.vorbis );
		decode.vorbis = NULL;
	}
	if( decode.fileData != NULL )
	{
		FreeBuffer( decode.fileData );
		decode.fileData = NULL;
	}
}

static void SoundSample_DecodeJob( const idJobContext& context )
{
	SoundSample_RunDecode( *static_cast<idSoundSampleDecode*>( context.data ), &context );
}

/*
========================
idSoundSample_OpenAL::BeginOggLoad

Reads the file, opens the stream and sizes the sample from it. With asyncDecode the
PCM is decoded on a job worker and *asyncDecode receives the job (NULL when it was
decoded here after all); without it the whole sample is decoded here.
========================
*/
bool idSoundSample_OpenAL::BeginOggLoad( const idStr& filename, idSoundSampleDecode** asyncDecode )
{
	if( asyncDecode != NULL )
	{
		*asyncDecode = NULL;
	}
	idFileLocal fileIn( fileSystem->OpenFileRead( filename ) );
	if( fileIn == NULL )
	{
		return false;
	}

	const int fileLen = fileIn->Length();
	if( fileLen <= 0 )
	{
		return false;
	}
	// Probe only the identification and final granule pages. Large compressed
	// files never enter the complete-input/complete-PCM allocation path.
	if( !keepPayload && s_streamThresholdKB.GetInteger() > 0 ) {
		byte peek[512 + 65563];
		const int prefix = Min( fileLen, 512 );
		const int tail = Min( fileLen - prefix, 65563 );
		int channels = 0, rate = 0;
		unsigned int frames = 0;
		const bool read = fileIn->Read( peek, prefix ) == prefix &&
			fileIn->Seek( fileLen - tail, FS_SEEK_SET ) == 0 && fileIn->Read( peek + prefix, tail ) == tail;
		fileIn->Rewind();
		bool known = read && SoundSample_PeekOggInfo( peek, prefix + tail, channels, rate, frames );
		uint64 decodedBytes = static_cast<uint64>( frames ) * channels * sizeof( short );
		// A truncated stream, one followed by other bytes or one whose last page has
		// no position defeats the peek, not the decoder: ask it, and with no length
		// at all judge by the compressed size. Vorbis keeps speech and music to a
		// quarter or less of their 16-bit PCM, so a long track never decodes whole.
		if( !known && SoundSample_ProbeOggStream( fileIn, channels, rate, frames ) ) {
			known = true;
			decodedBytes = frames != 0 ? static_cast<uint64>( frames ) * channels * sizeof( short ) : static_cast<uint64>( fileLen ) * 4;
		}
		if( known && channels >= 1 && channels <= 2 && rate >= 8000 && rate <= 192000 &&
			decodedBytes > static_cast<uint64>( s_streamThresholdKB.GetInteger() ) * 1024 ) {
			timestamp = fileIn->Timestamp();
			const bool streamed = BeginOggStreamEnvelope( filename, fileIn, channels, rate, frames, asyncDecode ) || LoadOggStream( filename );
			// A stream with no positioned page at all cannot seek: decode it whole.
			if( streamed || frames != 0 ) return streamed;
			FreeData();
		}
	}
	SoundSampleDecodeReservation reservation;
	if( !reservation.Grow( static_cast<uint64>( fileLen ) + SoundVorbisStream::ArenaBytes ) )
	{
		idLib::Warning( "LoadOgg( %s ) : compressed input exceeds s_decodeMemoryMB", filename.c_str() );
		return false;
	}

	byte* fileData = ( byte* )AllocBuffer( fileLen, filename.c_str() );
	if( fileData == NULL )
	{
		return false;
	}

	const int bytesRead = fileIn->Read( fileData, fileLen );
	if( bytesRead != fileLen )
	{
		FreeBuffer( fileData );
		return false;
	}

	timestamp = fileIn->Timestamp();

	idStr ampName = filename;
	ampName.SetFileExtension( "amp" );
	LoadAmplitude( ampName );

	// Off the main thread entirely when the headers say enough to size the sample.
	int peekChannels = 0;
	int peekRate = 0;
	unsigned int peekFrames = 0;
	if( asyncDecode != NULL && soundSamplePendingDecodes < SOUND_SAMPLE_MAX_PENDING_DECODES &&
			jobSystem.IsInitialized() && !jobSystem.IsSynchronous() &&
			SoundSample_PeekOggInfo( fileData, fileLen, peekChannels, peekRate, peekFrames ) &&
			peekChannels >= 1 && peekChannels <= 2 && peekRate > 0 &&
			( uint64 )peekFrames * peekChannels * sizeof( int16 ) <= ( uint64 )idMath::INT_MAX )
	{
		const int peekBytes = static_cast<int>( ( uint64 )peekFrames * peekChannels * sizeof( int16 ) );
		if( !reservation.Grow( static_cast<uint64>( fileLen ) + peekBytes + SoundVorbisStream::ArenaBytes ) )
		{
			FreeBuffer( fileData );
			idLib::Warning( "LoadOgg( %s ) : input and PCM exceed s_decodeMemoryMB", filename.c_str() );
			return false;
		}
		buffers.SetNum( 1 );
		buffers[0].buffer = AllocBuffer( peekBytes, GetName() );
		if( buffers[0].buffer != NULL )
		{
			FinishOggLoad( peekChannels, peekRate, peekFrames );
			idSoundSampleDecode* decode = new idSoundSampleDecode;
			decode->fileData = fileData;
			decode->fileLen = fileLen;
			decode->pcm = static_cast<short*>( buffers[0].buffer );
			decode->capacityFrames = peekFrames;
			decode->channels = peekChannels;
			decode->sampleRate = peekRate;
			decode->list = jobSystem.CreateJobList( "sound-sample-decode", 1, 0, idJobPriority::LOW );
			if( decode->list != nullptr && decode->list->AddJob( &SoundSample_DecodeJob, decode ) &&
					decode->list->Submit() == idJobSubmitResult::ACCEPTED )
			{
				soundSamplePendingDecodes++;
				decode->reservedBytes = reservation.Transfer();
				*asyncDecode = decode;
				return true;
			}
			if( decode->list != nullptr )
			{
				decode->list->Wait();
			}
			SoundSample_RunDecode( *decode );
			const bool mismatch = decode->headerMismatch;
			const unsigned int decodedFrames = decode->decodedFrames;
			delete decode;
			if( mismatch )
			{
				FreeData();
				return false;
			}
			return FinishOggDecode( filename, decodedFrames );
		}
		buffers.Clear();
	}

	// Open the stream, size the buffer from its length and decode straight into it.
	// stb_vorbis_decode_memory grew a scratch buffer by doubling and then copied it: two
	// to three times a sound's PCM at peak, and a full copy of every sound loaded.
	int vorbisError = 0;
	std::unique_ptr<char[]> arena( new (std::nothrow) char[SoundVorbisStream::ArenaBytes] );
	stb_vorbis_alloc alloc = { arena.get(), SoundVorbisStream::ArenaBytes };
	stb_vorbis* vorbis = arena ? stb_vorbis_open_memory( fileData, fileLen, &vorbisError, &alloc ) : NULL;
	if( vorbis == NULL )
	{
		FreeBuffer( fileData );
		idLib::Warning( "LoadOgg( %s ) : failed to open Ogg Vorbis (stb_vorbis error %d)", filename.c_str(), vorbisError );
		FreeData();
		return false;
	}
	const stb_vorbis_info info = stb_vorbis_get_info( vorbis );
	const int channels = info.channels;
	const int sampleRate = static_cast<int>( info.sample_rate );
	unsigned int streamFrames = stb_vorbis_stream_length_in_samples( vorbis );
	if( streamFrames == 0 && channels >= 1 && channels <= 2 && sampleRate > 0 )
	{
		// Missing granule length: count with fixed scratch, then rewind and decode
		// once into the final allocation. Never use stb's unbounded doubling helper.
		short scratch[8192];
		const uint64 budget = static_cast<uint64>( idMath::ClampInt( 16, 1024, s_decodeMemoryMB.GetInteger() ) ) * 1024 * 1024;
		uint64 frames = 0;
		bool valid = reservation.Grow( static_cast<uint64>( fileLen ) + sizeof( scratch ) + SoundVorbisStream::ArenaBytes );
		while( valid )
		{
			const int count = stb_vorbis_get_samples_short_interleaved( vorbis, channels, scratch, 8192 );
			if( count <= 0 ) break;
			frames += count;
			valid = frames * channels * sizeof( short ) <= budget - fileLen - SoundVorbisStream::ArenaBytes;
		}
		if( !valid || !stb_vorbis_seek_start( vorbis ) )
		{
			stb_vorbis_close( vorbis );
			FreeBuffer( fileData );
			idLib::Warning( "LoadOgg( %s ) : unlengthened stream exceeds s_decodeMemoryMB or cannot rewind", filename.c_str() );
			FreeData();
			return false;
		}
		streamFrames = static_cast<unsigned int>( frames );
	}
	if( channels < 1 || channels > 2 || sampleRate <= 0 || streamFrames == 0 ||
			( uint64 )streamFrames * channels * sizeof( int16 ) > ( uint64 )idMath::INT_MAX )
	{
		stb_vorbis_close( vorbis );
		FreeBuffer( fileData );
		idLib::Warning( "LoadOgg( %s ) : unsupported stream (%d channels, %d Hz, %u frames)", filename.c_str(), channels, sampleRate, streamFrames );
		FreeData();
		return false;
	}

	const int capacityBytes = static_cast<int>( ( uint64 )streamFrames * channels * sizeof( int16 ) );
	if( !reservation.Grow( static_cast<uint64>( fileLen ) + capacityBytes + SoundVorbisStream::ArenaBytes ) )
	{
		stb_vorbis_close( vorbis );
		FreeBuffer( fileData );
		idLib::Warning( "LoadOgg( %s ) : input and PCM exceed s_decodeMemoryMB", filename.c_str() );
		FreeData();
		return false;
	}
	buffers.SetNum( 1 );
	buffers[0].buffer = AllocBuffer( capacityBytes, GetName() );
	if( buffers[0].buffer == NULL )
	{
		stb_vorbis_close( vorbis );
		FreeBuffer( fileData );
		idLib::Warning( "LoadOgg( %s ) : could not allocate decoded audio", filename.c_str() );
		FreeData();
		return false;
	}
	// Sized from the stream, so the length, rate and channels hold from here on.
	FinishOggLoad( channels, sampleRate, streamFrames );

	idSoundSampleDecode* decode = new idSoundSampleDecode;
	decode->vorbis = vorbis;
	decode->arena = std::move( arena );
	decode->fileData = fileData;
	decode->fileLen = fileLen;
	decode->sampleRate = sampleRate;
	decode->pcm = static_cast<short*>( buffers[0].buffer );
	decode->capacityFrames = streamFrames;
	decode->channels = channels;

	if( asyncDecode != NULL && soundSamplePendingDecodes < SOUND_SAMPLE_MAX_PENDING_DECODES &&
			jobSystem.IsInitialized() && !jobSystem.IsSynchronous() )
	{
		decode->list = jobSystem.CreateJobList( "sound-sample-decode", 1, 0, idJobPriority::LOW );
		if( decode->list != nullptr && decode->list->AddJob( &SoundSample_DecodeJob, decode ) &&
				decode->list->Submit() == idJobSubmitResult::ACCEPTED )
		{
			soundSamplePendingDecodes++;
			decode->reservedBytes = reservation.Transfer();
			*asyncDecode = decode;
			return true;
		}
		// rejected or run inline by the job system: finish below
		if( decode->list != nullptr )
		{
			decode->list->Wait();
		}
	}

	SoundSample_RunDecode( *decode );
	const unsigned int decodedFrames = decode->decodedFrames;
	delete decode;
	return FinishOggDecode( filename, decodedFrames );
}

/*
========================
idSoundSample_OpenAL::FinishOggDecode

The stream's PCM is in buffers[0]. A stream that decodes short keeps what it had.
========================
*/
bool idSoundSample_OpenAL::FinishOggDecode( const idStr& filename, const unsigned int decodedFrames )
{
	if( decodedFrames == 0 )
	{
		idLib::Warning( "LoadOgg( %s ) : failed to decode Ogg Vorbis", filename.c_str() );
		FreeData();
		return false;
	}
	if( decodedFrames < static_cast<unsigned int>( playLength ) )
	{
		// stb_vorbis stops at the first page it cannot decode and keeps what it has
		idLib::Warning( "LoadOgg( %s ) : decoded %u of %d frames; the file is damaged and plays short", filename.c_str(), decodedFrames, playLength );
		return FinishOggLoad( NumChannels(), SampleRate(), decodedFrames );
	}
	return true;
}

/*
========================
idSoundSample_OpenAL::LoadOgg
========================
*/
bool idSoundSample_OpenAL::LoadOgg( const idStr& filename )
{
	return BeginOggLoad( filename, NULL );
}

/*
========================
idSoundSample_OpenAL::LoadResourceAsync

LoadResource for a level load: when the first sample the probe would try is an Ogg,
the file is read and opened here and its PCM decodes on a job worker while the level
keeps loading. Length, rate and channels hold at once; FinishDecode waits for the PCM
and uploads it, and every reader of the PCM calls it first. Anything else, and any
failure, takes the normal synchronous probe.
========================
*/
void idSoundSample_OpenAL::LoadResourceAsync()
{
	FinishDecode();
	if( !s_asyncSampleDecode.GetBool() || s_noSound.GetBool() || idStr::Icmpn( GetName(), "_default", 8 ) == 0 )
	{
		LoadResource();
		return;
	}

	idList< idStr > sampleVariants;
	SoundSample_BuildVariants( GetName(), sampleVariants );
	idStr oggName = sampleVariants.Num() > 0 ? sampleVariants[0] : idStr( GetName() );
	idStr ext;
	oggName.ExtractFileExtension( ext );
	if( ext.Icmp( "roq" ) == 0 )
	{
		LoadResource();
		return;
	}
	oggName.SetFileExtension( ".ogg" );

	FreeData();
	idSoundSampleDecode* decode = NULL;
	if( !BeginOggLoad( oggName, &decode ) )
	{
		// no Ogg first, or a broken one: the normal probe tries every loader and variant
		LoadResource();
		return;
	}
	loaded = true;
	if( decode == NULL )
	{
		CreateOpenALBuffer();
		return;
	}
	pendingDecode = decode;
	pendingDecodeName = oggName;
}

/*
========================
idSoundSample_OpenAL::FinishDecode

Waits for an outstanding decode and uploads the sample. Does nothing without one.
========================
*/
void idSoundSample_OpenAL::FinishDecode()
{
	if( pendingDecode == NULL )
	{
		return;
	}
	idSoundSampleDecode* decode = pendingDecode;
	pendingDecode = NULL;
	soundSamplePendingDecodes--;
	if( decode->list != nullptr )
	{
		decode->list->Wait();
	}
	// A cancelled job may be unstarted or partially decoded: finish it here.
	SoundSample_RunDecode( *decode );
	const bool mismatch = decode->headerMismatch;
	const unsigned int decodedFrames = decode->decodedFrames;
	const bool envelopeOnly = decode->envelope != NULL;
	soundSamplePendingDecodeBytes -= decode->reservedBytes;
	delete decode;
	SoundMemoryRelease( SoundVorbisStream::ArenaBytes );

	if( mismatch )
	{
		// the peeked header disagreed with the stream: load it the ordinary way
		LoadResource();
		return;
	}
	// a streamed track's envelope is complete; it has no PCM to upload
	if( envelopeOnly )
	{
		return;
	}
	if( !FinishOggDecode( pendingDecodeName, decodedFrames ) )
	{
		// the normal probe falls back to other loaders, variants and the default
		LoadResource();
		return;
	}
	CreateOpenALBuffer();
}

/*
========================
idSoundSample_OpenAL::IsDecodeComplete
========================
*/
bool idSoundSample_OpenAL::IsDecodeComplete() const
{
	return pendingDecode != NULL && ( pendingDecode->list == nullptr || pendingDecode->list->TryWait() );
}

/*
========================
idSoundSample_OpenAL::CancelDecode

Waits for an outstanding decode and drops it; the caller frees the sample.
========================
*/
void idSoundSample_OpenAL::CancelDecode()
{
	if( pendingDecode == NULL )
	{
		return;
	}
	idSoundSampleDecode* decode = pendingDecode;
	pendingDecode = NULL;
	soundSamplePendingDecodes--;
	if( decode->list != nullptr )
	{
		decode->list->Cancel();
		decode->list->Wait();
	}
	if( decode->vorbis != NULL )
	{
		stb_vorbis_close( decode->vorbis );
		decode->vorbis = NULL;
	}
	if( decode->fileData != NULL )
	{
		FreeBuffer( decode->fileData );
		decode->fileData = NULL;
	}
	soundSamplePendingDecodeBytes -= decode->reservedBytes;
	delete decode;
	SoundMemoryRelease( SoundVorbisStream::ArenaBytes );
}

/*
========================
idSoundSample_OpenAL::FinishOggLoad

buffers[0] holds frames of interleaved 16-bit PCM.
========================
*/
bool idSoundSample_OpenAL::FinishOggLoad( const int channels, const int sampleRate, const unsigned int frames )
{
	memset( &format, 0, sizeof( format ) );
	format.basic.formatTag = idWaveFile::FORMAT_PCM;
	format.basic.numChannels = ( uint16 )channels;
	format.basic.samplesPerSec = sampleRate;
	format.basic.bitsPerSample = 16;
	format.basic.blockSize = format.basic.numChannels * format.basic.bitsPerSample / 8;
	format.basic.avgBytesPerSec = format.basic.samplesPerSec * format.basic.blockSize;

	playBegin = 0;
	playLength = static_cast<int>( frames );
	totalBufferSize = static_cast<int>( ( uint64 )frames * channels * sizeof( int16 ) );
	buffers[0].bufferSize = totalBufferSize;
	buffers[0].numSamples = playLength;
	buffers[0].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[0].buffer );

	return true;
}

/*
========================
idSoundSample_OpenAL::LoadRoQ
========================
*/
bool idSoundSample_OpenAL::LoadRoQ( const idStr& filename )
{
	idFileLocal fileIn( fileSystem->OpenFileRead( filename ) );
	if( fileIn == NULL || fileIn->Length() < 16 ) return false;
	const int fileLen = fileIn->Length();
	byte header[8];
	if( !openQ4_ReadExact( fileIn, header, 8 ) || ( header[0] | ( header[1] << 8 ) ) != ROQ_FILE_MAGIC ) return false;

	// Count audio values from chunk headers, then decode directly into the one
	// admitted PCM allocation. Video payloads and complete encoded RoQ files
	// never enter memory; the second pass uses a fixed 4096-byte input block.
	auto scan = [&]( int16* output, int capacity, int& channels, int& values ) {
		channels = values = 0;
		int offset = 8;
		while( offset <= fileLen - 8 ) {
			if( fileIn->Seek( offset, FS_SEEK_SET ) != 0 || !openQ4_ReadExact( fileIn, header, 8 ) ) return false;
			const int chunkId = header[0] | ( header[1] << 8 );
			const int chunkSize = header[2] | ( header[3] << 8 ) | ( header[4] << 16 );
			const int argument = header[6] | ( header[7] << 8 );
			offset += 8;
			if( chunkSize > fileLen - offset ) return false;
			const int chunkChannels = chunkId == ROQ_SOUND_MONO ? 1 : chunkId == ROQ_SOUND_STEREO ? 2 : 0;
			if( chunkChannels != 0 ) {
				if( ( channels && channels != chunkChannels ) || chunkSize % chunkChannels != 0 ||
					chunkSize > idMath::INT_MAX / static_cast<int>( sizeof( int16 ) ) - values ) return false;
				channels = chunkChannels;
				if( output != NULL ) {
					if( chunkSize > capacity - values ) return false;
					int32 left = static_cast<int16>( channels == 1 ? argument : argument & 0xff00 );
					int32 right = static_cast<int16>( ( argument & 0xff ) << 8 );
					byte input[4096];
					for( int done = 0; done < chunkSize; ) {
						const int count = Min( static_cast<int>( sizeof( input ) ), chunkSize - done );
						if( !openQ4_ReadExact( fileIn, input, count ) ) return false;
						for( int i = 0; i < count; ++i ) {
							int32& predictor = channels == 2 && ( i & 1 ) ? right : left;
							predictor = idMath::ClampInt( ROQ_AUDIO_MIN_SAMPLE, ROQ_AUDIO_MAX_SAMPLE, predictor + RoQAudioDelta( input[i] ) );
							output[values + done + i] = static_cast<int16>( predictor );
						}
						done += count;
					}
				}
				values += chunkSize;
			}
			offset += chunkSize;
		}
		return channels != 0 && values != 0;
	};
	int channels = 0, values = 0;
	if( !scan( NULL, 0, channels, values ) ) return false;
	const int bytes = values * sizeof( int16 );
	std::unique_ptr<void, decltype( &FreeBuffer )> pcm( AllocBuffer( bytes, GetName() ), &FreeBuffer );
	if( !pcm ) return false;
	int decodedChannels = 0, decodedValues = 0;
	if( !scan( static_cast<int16*>( pcm.get() ), values, decodedChannels, decodedValues ) ||
		decodedChannels != channels || decodedValues != values ) return false;

	timestamp = fileIn->Timestamp();
	idStr ampName = filename;
	ampName.SetFileExtension( "amp" );
	LoadAmplitude( ampName );
	memset( &format, 0, sizeof( format ) );
	format.basic.formatTag = idWaveFile::FORMAT_PCM;
	format.basic.numChannels = static_cast<uint16>( channels );
	format.basic.samplesPerSec = ROQ_AUDIO_SAMPLE_RATE;
	format.basic.bitsPerSample = 16;
	format.basic.blockSize = static_cast<uint16>( channels * sizeof( int16 ) );
	format.basic.avgBytesPerSec = format.basic.samplesPerSec * format.basic.blockSize;
	playBegin = 0;
	playLength = values / channels;
	totalBufferSize = bytes;
	buffers.SetNum( 1 );
	buffers[0].bufferSize = bytes;
	buffers[0].numSamples = playLength;
	buffers[0].buffer = pcm.release();
	return true;
}


/*
========================
idSoundSample_OpenAL::MakeDefault
========================
*/
void idSoundSample_OpenAL::MakeDefault()
{
	FreeData();

	static const int DEFAULT_NUM_SAMPLES = 4096;

	timestamp = FILE_NOT_FOUND_TIMESTAMP;
	loaded = true;

	memset( &format, 0, sizeof( format ) );
	format.basic.formatTag = idWaveFile::FORMAT_PCM;
	format.basic.numChannels = 1;
	format.basic.bitsPerSample = 16;
	format.basic.samplesPerSec = 22050; //44100; //XAUDIO2_MIN_SAMPLE_RATE;
	format.basic.blockSize = format.basic.numChannels * format.basic.bitsPerSample / 8;
	format.basic.avgBytesPerSec = format.basic.samplesPerSec * format.basic.blockSize;

	assert( format.basic.blockSize == 2 );

	totalBufferSize = DEFAULT_NUM_SAMPLES * 2;// * sizeof( short );

	short* defaultBuffer = ( short* )AllocBuffer( totalBufferSize, GetName() );
	if( defaultBuffer == NULL ) { totalBufferSize = 0; return; }
	for( int i = 0; i < DEFAULT_NUM_SAMPLES; i += 2 )
	{
		float v = sin( idMath::PI * 2 * i / 64 );
		int sample = v * 0x4000;
		defaultBuffer[i + 0] = sample;
		defaultBuffer[i + 1] = sample;

		//defaultBuffer[i + 0] = SHRT_MIN;
		//defaultBuffer[i + 1] = SHRT_MAX;
	}

	buffers.SetNum( 1 );
	buffers[0].buffer = defaultBuffer;
	buffers[0].bufferSize = totalBufferSize;
	buffers[0].numSamples = DEFAULT_NUM_SAMPLES;
	buffers[0].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[0].buffer );

	playBegin = 0;
	playLength = DEFAULT_NUM_SAMPLES;

	CreateOpenALBuffer();
}

/*
========================
idSoundSample_OpenAL::FreeData

Called before deleting the object and at the start of LoadResource()
========================
*/
static void SoundMemoryReclaim() {
	// Only old-map cache entries may be reclaimed. Keep every current-map,
	// never-purge and active-channel reference, including inaudible/paused ones.
	for( int i = 0; i < soundSystemLocal.samples.Num(); ++i ) {
		idSoundSample* sample = soundSystemLocal.samples[i];
		if( !sample->IsLoaded() || sample->GetNeverPurge() || sample->GetLevelLoadReferenced() ) continue;
		bool active = false;
		for( int w = 0; w < soundSystemLocal.soundWorlds.Num() && !active; ++w ) {
			const idSoundWorldLocal* world = soundSystemLocal.soundWorlds[w];
			if( world == NULL ) continue;
			for( int e = 0; e < world->emitters.Num() && !active; ++e ) {
				const idSoundEmitterLocal* emitter = world->emitters[e];
				if( emitter == NULL ) continue;
				for( int c = 0; c < emitter->channels.Num(); ++c ) {
					const idSoundChannel* channel = emitter->channels[c];
					if( channel != NULL && ( channel->leadinSample == sample || channel->loopingSample == sample ) ) { active = true; break; }
				}
			}
		}
		if( !active ) sample->FreeData();
	}
	// Cancel discarded jobs before joining retained work: uploading a discarded
	// sample would spend the headroom this reclaim is meant to recover. Do not edit
	// decodingSamples here; this can run while its owner is iterating it.
	for( int i = 0; i < soundSystemLocal.samples.Num(); ++i ) soundSystemLocal.samples[i]->FinishDecode();
}

void idSoundSample_OpenAL::FreeData()
{
	// a worker may still be writing into buffers[0]
	CancelDecode();
	if( buffers.Num() > 0 || openalBuffer != 0 )
	{
		soundSystemLocal.StopVoicesWithSample( ( idSoundSample* )this );
	}
	if( buffers.Num() > 0 )
	{
		for( int i = 0; i < buffers.Num(); i++ )
		{
			FreeBuffer( buffers[i].buffer );
		}
		buffers.Clear();
	}
	SoundResizeAmplitude( amplitude, amplitudeReserved, 0 );
	streamName.Clear();
	streamCountedLength = false;
	streamPageStart = streamPageEnd = streamPageGranule = 0;
	SoundMemoryRelease( openalReserved ); openalReserved = 0;

	timestamp = FILE_NOT_FOUND_TIMESTAMP;
	memset( &format, 0, sizeof( format ) );
	loaded = false;
	totalBufferSize = 0;
	payloadReleased = false;
	playBegin = 0;
	playLength = 0;
	openalBufferUploadFailed = false;

	if( openalBuffer != 0 )
	{
		if( soundSystemLocal.hardware.openalContext == NULL || alcGetCurrentContext() != soundSystemLocal.hardware.openalContext )
		{
			openalBuffer = 0;
			return;
		}

		alGetError(); // clear any existing error
		if( alIsBuffer( openalBuffer ) )
		{
			alDeleteBuffers( 1, &openalBuffer );
			ALenum err = alGetError();
			if( err != AL_NO_ERROR && err != AL_INVALID_NAME )
			{
				common->Warning( "idSoundSample_OpenAL::FreeData: error unloading OpenAL buffer (0x%x)", err );
			}
		}
		openalBuffer = 0;
	}
}

/*
========================
idSoundSample_OpenAL::LoadAmplitude
========================
*/
bool idSoundSample_OpenAL::LoadAmplitude( const idStr& name )
{
	SoundResizeAmplitude( amplitude, amplitudeReserved, 0 );
	idFileLocal f( fileSystem->OpenFileRead( name ) );
	if( f == NULL )
	{
		return false;
	}
	const int fileLength = f->Length();
	if( fileLength < 0 )
	{
		return false;
	}
	if( !SoundResizeAmplitude( amplitude, amplitudeReserved, fileLength ) ) return false;
	if( !openQ4_ReadExact( f, amplitude.Ptr(), amplitude.Num() ) )
	{
		SoundResizeAmplitude( amplitude, amplitudeReserved, 0 );
		return false;
	}
	return true;
}

/*
========================
idSoundSample_OpenAL::GetAmplitude
========================
*/
float idSoundSample_OpenAL::GetAmplitude( int timeMS ) const
{
	if( timeMS < 0 || timeMS > LengthInMsec() )
	{
		return 0.0f;
	}
	if( IsDefault() )
	{
		return 1.0f;
	}
	// A job worker may still be filling a streamed track's envelope.
	if( pendingDecode != NULL )
	{
		return 0.0f;
	}
	int index = timeMS * 60 / 1000;
	if( index < 0 || index >= amplitude.Num() )
	{
		return 0.0f;
	}
	return ( float )amplitude[index] / 255.0f;
}


#if 0 //defined(AL_SOFT_buffer_samples)
const char* idSoundSample_OpenAL::OpenALSoftChannelsName( ALenum chans ) const
{
	switch( chans )
	{
		case AL_MONO_SOFT:
			return "Mono";
		case AL_STEREO_SOFT:
			return "Stereo";
		case AL_REAR_SOFT:
			return "Rear";
		case AL_QUAD_SOFT:
			return "Quadraphonic";
		case AL_5POINT1_SOFT:
			return "5.1 Surround";
		case AL_6POINT1_SOFT:
			return "6.1 Surround";
		case AL_7POINT1_SOFT:
			return "7.1 Surround";
	}

	return "Unknown Channels";
}

const char* idSoundSample_OpenAL::OpenALSoftTypeName( ALenum type ) const
{
	switch( type )
	{
		case AL_BYTE_SOFT:
			return "S8";
		case AL_UNSIGNED_BYTE_SOFT:
			return "U8";
		case AL_SHORT_SOFT:
			return "S16";
		case AL_UNSIGNED_SHORT_SOFT:
			return "U16";
		case AL_INT_SOFT:
			return "S32";
		case AL_UNSIGNED_INT_SOFT:
			return "U32";
		case AL_FLOAT_SOFT:
			return "Float32";
		case AL_DOUBLE_SOFT:
			return "Float64";
	}

	return "Unknown Type";
}

ALsizei idSoundSample_OpenAL::FramesToBytes( ALsizei size, ALenum channels, ALenum type ) const
{
	switch( channels )
	{
		case AL_MONO_SOFT:
			size *= 1;
			break;
		case AL_STEREO_SOFT:
			size *= 2;
			break;
		case AL_REAR_SOFT:
			size *= 2;
			break;
		case AL_QUAD_SOFT:
			size *= 4;
			break;
		case AL_5POINT1_SOFT:
			size *= 6;
			break;
		case AL_6POINT1_SOFT:
			size *= 7;
			break;
		case AL_7POINT1_SOFT:
			size *= 8;
			break;
	}

	switch( type )
	{
		case AL_BYTE_SOFT:
			size *= sizeof( ALbyte );
			break;
		case AL_UNSIGNED_BYTE_SOFT:
			size *= sizeof( ALubyte );
			break;
		case AL_SHORT_SOFT:
			size *= sizeof( ALshort );
			break;
		case AL_UNSIGNED_SHORT_SOFT:
			size *= sizeof( ALushort );
			break;
		case AL_INT_SOFT:
			size *= sizeof( ALint );
			break;
		case AL_UNSIGNED_INT_SOFT:
			size *= sizeof( ALuint );
			break;
		case AL_FLOAT_SOFT:
			size *= sizeof( ALfloat );
			break;
		case AL_DOUBLE_SOFT:
			size *= sizeof( ALdouble );
			break;
	}

	return size;
}

ALsizei idSoundSample_OpenAL::BytesToFrames( ALsizei size, ALenum channels, ALenum type ) const
{
	return size / FramesToBytes( 1, channels, type );
}

ALenum idSoundSample_OpenAL::GetOpenALSoftFormat( ALenum channels, ALenum type ) const
{
	ALenum format = AL_NONE;

	/* If using AL_SOFT_buffer_samples, try looking through its formats */
	if( alIsExtensionPresent( "AL_SOFT_buffer_samples" ) )
	{
		/* AL_SOFT_buffer_samples is more lenient with matching formats. The
		 * specified sample type does not need to match the returned format,
		 * but it is nice to try to get something close. */
		if( type == AL_UNSIGNED_BYTE_SOFT || type == AL_BYTE_SOFT )
		{
			if( channels == AL_MONO_SOFT )
			{
				format = AL_MONO8_SOFT;
			}
			else if( channels == AL_STEREO_SOFT )
			{
				format = AL_STEREO8_SOFT;
			}
			else if( channels == AL_QUAD_SOFT )
			{
				format = AL_QUAD8_SOFT;
			}
			else if( channels == AL_5POINT1_SOFT )
			{
				format = AL_5POINT1_8_SOFT;
			}
			else if( channels == AL_6POINT1_SOFT )
			{
				format = AL_6POINT1_8_SOFT;
			}
			else if( channels == AL_7POINT1_SOFT )
			{
				format = AL_7POINT1_8_SOFT;
			}
		}
		else if( type == AL_UNSIGNED_SHORT_SOFT || type == AL_SHORT_SOFT )
		{
			if( channels == AL_MONO_SOFT )
			{
				format = AL_MONO16_SOFT;
			}
			else if( channels == AL_STEREO_SOFT )
			{
				format = AL_STEREO16_SOFT;
			}
			else if( channels == AL_QUAD_SOFT )
			{
				format = AL_QUAD16_SOFT;
			}
			else if( channels == AL_5POINT1_SOFT )
			{
				format = AL_5POINT1_16_SOFT;
			}
			else if( channels == AL_6POINT1_SOFT )
			{
				format = AL_6POINT1_16_SOFT;
			}
			else if( channels == AL_7POINT1_SOFT )
			{
				format = AL_7POINT1_16_SOFT;
			}
		}
		else if( type == AL_UNSIGNED_BYTE3_SOFT || type == AL_BYTE3_SOFT ||
				 type == AL_UNSIGNED_INT_SOFT || type == AL_INT_SOFT ||
				 type == AL_FLOAT_SOFT || type == AL_DOUBLE_SOFT )
		{
			if( channels == AL_MONO_SOFT )
			{
				format = AL_MONO32F_SOFT;
			}
			else if( channels == AL_STEREO_SOFT )
			{
				format = AL_STEREO32F_SOFT;
			}
			else if( channels == AL_QUAD_SOFT )
			{
				format = AL_QUAD32F_SOFT;
			}
			else if( channels == AL_5POINT1_SOFT )
			{
				format = AL_5POINT1_32F_SOFT;
			}
			else if( channels == AL_6POINT1_SOFT )
			{
				format = AL_6POINT1_32F_SOFT;
			}
			else if( channels == AL_7POINT1_SOFT )
			{
				format = AL_7POINT1_32F_SOFT;
			}
		}

		if( format != AL_NONE && !alIsBufferFormatSupportedSOFT( format ) )
		{
			format = AL_NONE;
		}

		/* A matching format was not found or supported. Try 32-bit float. */
		if( format == AL_NONE )
		{
			if( channels == AL_MONO_SOFT )
			{
				format = AL_MONO32F_SOFT;
			}
			else if( channels == AL_STEREO_SOFT )
			{
				format = AL_STEREO32F_SOFT;
			}
			else if( channels == AL_QUAD_SOFT )
			{
				format = AL_QUAD32F_SOFT;
			}
			else if( channels == AL_5POINT1_SOFT )
			{
				format = AL_5POINT1_32F_SOFT;
			}
			else if( channels == AL_6POINT1_SOFT )
			{
				format = AL_6POINT1_32F_SOFT;
			}
			else if( channels == AL_7POINT1_SOFT )
			{
				format = AL_7POINT1_32F_SOFT;
			}

			if( format != AL_NONE && !alIsBufferFormatSupportedSOFT( format ) )
			{
				format = AL_NONE;
			}
		}
		/* 32-bit float not supported. Try 16-bit int. */
		if( format == AL_NONE )
		{
			if( channels == AL_MONO_SOFT )
			{
				format = AL_MONO16_SOFT;
			}
			else if( channels == AL_STEREO_SOFT )
			{
				format = AL_STEREO16_SOFT;
			}
			else if( channels == AL_QUAD_SOFT )
			{
				format = AL_QUAD16_SOFT;
			}
			else if( channels == AL_5POINT1_SOFT )
			{
				format = AL_5POINT1_16_SOFT;
			}
			else if( channels == AL_6POINT1_SOFT )
			{
				format = AL_6POINT1_16_SOFT;
			}
			else if( channels == AL_7POINT1_SOFT )
			{
				format = AL_7POINT1_16_SOFT;
			}

			if( format != AL_NONE && !alIsBufferFormatSupportedSOFT( format ) )
			{
				format = AL_NONE;
			}
		}
		/* 16-bit int not supported. Try 8-bit int. */
		if( format == AL_NONE )
		{
			if( channels == AL_MONO_SOFT )
			{
				format = AL_MONO8_SOFT;
			}
			else if( channels == AL_STEREO_SOFT )
			{
				format = AL_STEREO8_SOFT;
			}
			else if( channels == AL_QUAD_SOFT )
			{
				format = AL_QUAD8_SOFT;
			}
			else if( channels == AL_5POINT1_SOFT )
			{
				format = AL_5POINT1_8_SOFT;
			}
			else if( channels == AL_6POINT1_SOFT )
			{
				format = AL_6POINT1_8_SOFT;
			}
			else if( channels == AL_7POINT1_SOFT )
			{
				format = AL_7POINT1_8_SOFT;
			}

			if( format != AL_NONE && !alIsBufferFormatSupportedSOFT( format ) )
			{
				format = AL_NONE;
			}
		}

		return format;
	}

	/* We use the AL_EXT_MCFORMATS extension to provide output of Quad, 5.1,
	 * and 7.1 channel configs, AL_EXT_FLOAT32 for 32-bit float samples, and
	 * AL_EXT_DOUBLE for 64-bit float samples. */
	if( type == AL_UNSIGNED_BYTE_SOFT )
	{
		if( channels == AL_MONO_SOFT )
		{
			format = AL_FORMAT_MONO8;
		}
		else if( channels == AL_STEREO_SOFT )
		{
			format = AL_FORMAT_STEREO8;
		}
		else if( alIsExtensionPresent( "AL_EXT_MCFORMATS" ) )
		{
			if( channels == AL_QUAD_SOFT )
			{
				format = alGetEnumValue( "AL_FORMAT_QUAD8" );
			}
			else if( channels == AL_5POINT1_SOFT )
			{
				format = alGetEnumValue( "AL_FORMAT_51CHN8" );
			}
			else if( channels == AL_6POINT1_SOFT )
			{
				format = alGetEnumValue( "AL_FORMAT_61CHN8" );
			}
			else if( channels == AL_7POINT1_SOFT )
			{
				format = alGetEnumValue( "AL_FORMAT_71CHN8" );
			}
		}
	}
	else if( type == AL_SHORT_SOFT )
	{
		if( channels == AL_MONO_SOFT )
		{
			format = AL_FORMAT_MONO16;
		}
		else if( channels == AL_STEREO_SOFT )
		{
			format = AL_FORMAT_STEREO16;
		}
		else if( alIsExtensionPresent( "AL_EXT_MCFORMATS" ) )
		{
			if( channels == AL_QUAD_SOFT )
			{
				format = alGetEnumValue( "AL_FORMAT_QUAD16" );
			}
			else if( channels == AL_5POINT1_SOFT )
			{
				format = alGetEnumValue( "AL_FORMAT_51CHN16" );
			}
			else if( channels == AL_6POINT1_SOFT )
			{
				format = alGetEnumValue( "AL_FORMAT_61CHN16" );
			}
			else if( channels == AL_7POINT1_SOFT )
			{
				format = alGetEnumValue( "AL_FORMAT_71CHN16" );
			}
		}
	}
	else if( type == AL_FLOAT_SOFT && alIsExtensionPresent( "AL_EXT_FLOAT32" ) )
	{
		if( channels == AL_MONO_SOFT )
		{
			format = alGetEnumValue( "AL_FORMAT_MONO_FLOAT32" );
		}
		else if( channels == AL_STEREO_SOFT )
		{
			format = alGetEnumValue( "AL_FORMAT_STEREO_FLOAT32" );
		}
		else if( alIsExtensionPresent( "AL_EXT_MCFORMATS" ) )
		{
			if( channels == AL_QUAD_SOFT )
			{
				format = alGetEnumValue( "AL_FORMAT_QUAD32" );
			}
			else if( channels == AL_5POINT1_SOFT )
			{
				format = alGetEnumValue( "AL_FORMAT_51CHN32" );
			}
			else if( channels == AL_6POINT1_SOFT )
			{
				format = alGetEnumValue( "AL_FORMAT_61CHN32" );
			}
			else if( channels == AL_7POINT1_SOFT )
			{
				format = alGetEnumValue( "AL_FORMAT_71CHN32" );
			}
		}
	}
	else if( type == AL_DOUBLE_SOFT && alIsExtensionPresent( "AL_EXT_DOUBLE" ) )
	{
		if( channels == AL_MONO_SOFT )
		{
			format = alGetEnumValue( "AL_FORMAT_MONO_DOUBLE" );
		}
		else if( channels == AL_STEREO_SOFT )
		{
			format = alGetEnumValue( "AL_FORMAT_STEREO_DOUBLE" );
		}
	}

	/* NOTE: It seems OSX returns -1 from alGetEnumValue for unknown enums, as
	 * opposed to 0. Correct it. */
	if( format == -1 )
	{
		format = 0;
	}

	return format;
}
#endif // #if defined(AL_SOFT_buffer_samples)

ALenum idSoundSample_OpenAL::GetOpenALBufferFormat() const
{
	ALenum alFormat;

	if( format.basic.formatTag == idWaveFile::FORMAT_PCM )
	{
		alFormat = NumChannels() == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
	}
	else if( format.basic.formatTag == idWaveFile::FORMAT_ADPCM )
	{
		//alFormat = NumChannels() == 1 ? AL_FORMAT_MONO8 : AL_FORMAT_STEREO8;
		alFormat = NumChannels() == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
		//alFormat = NumChannels() == 1 ? AL_FORMAT_MONO_IMA4 : AL_FORMAT_STEREO_IMA4;
	}
	else if( format.basic.formatTag == idWaveFile::FORMAT_XMA2 )
	{
		alFormat = NumChannels() == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
	}
	else
	{
		alFormat = NumChannels() == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
	}

	return alFormat;
}

int32 idSoundSample_OpenAL::MS_ADPCM_nibble( MS_ADPCM_decodeState_t* state, int8 nybble )
{
	const int32 max_audioval = ( ( 1 << ( 16 - 1 ) ) - 1 );
	const int32 min_audioval = -( 1 << ( 16 - 1 ) );
	const int32 adaptive[] =
	{
		230, 230, 230, 230, 307, 409, 512, 614,
		768, 614, 512, 409, 307, 230, 230, 230
	};

	int32 delta;

	int64_t widenedSample =
		( static_cast<int64_t>( state->iSamp1 ) * state->coef1 +
			static_cast<int64_t>( state->iSamp2 ) * state->coef2 ) / 256;

	if( nybble & 0x08 )
	{
		widenedSample += static_cast<int64_t>( state->iDelta ) * ( nybble - 0x10 );
	}
	else
	{
		widenedSample += static_cast<int64_t>( state->iDelta ) * nybble;
	}

	if( widenedSample < min_audioval )
	{
		widenedSample = min_audioval;
	}
	else if( widenedSample > max_audioval )
	{
		widenedSample = max_audioval;
	}
	const int32 new_sample = static_cast<int32>( widenedSample );

	delta = ( ( int32 ) state->iDelta * adaptive[nybble] ) / 256;
	if( delta < 16 )
	{
		delta = 16;
	}

	state->iDelta = ( uint16 ) delta;
	state->iSamp2 = state->iSamp1;
	state->iSamp1 = ( int16 ) new_sample;

	return ( new_sample );
}

int idSoundSample_OpenAL::MS_ADPCM_decode( uint8** audio_buf, uint32* audio_len )
{
	static MS_ADPCM_decodeState_t	states[2];
	MS_ADPCM_decodeState_t*			state[2];

	uint8* encoded;
	uint8* decoded;
	uint32_t encoded_len;
	int32 samplesleft;
	int8 nybble;
	int8 stereo;
	int32 new_sample;

	if( audio_buf == NULL || audio_len == NULL || *audio_buf == NULL )
	{
		return -1;
	}

	size_t decodedBytes = 0;
	if( !openQ4_GetMSADPCMDecodedSize( *audio_len, format.basic.blockSize,
		format.extra.adpcm.samplesPerBlock, format.basic.numChannels,
		format.extra.adpcm.numCoef, decodedBytes ) )
	{
		return -1;
	}

	uint8* const originalBuffer = *audio_buf;
	encoded_len = *audio_len;
	encoded = *audio_buf;

	// Validate every block's coefficient selector before allocating or decoding.
	for( uint32_t blockOffset = 0; blockOffset < encoded_len; blockOffset += format.basic.blockSize )
	{
		for( uint16_t channel = 0; channel < format.basic.numChannels; ++channel )
		{
			const uint8_t predictor = encoded[ blockOffset + channel ];
			if( predictor >= format.extra.adpcm.numCoef || predictor >= 7 )
			{
				return -1;
			}
		}
	}

	uint8* const decodedBuffer = static_cast<uint8*>( AllocBuffer( static_cast<int>( decodedBytes ), GetName() ) );
	if( decodedBuffer == NULL )
	{
		//SDL_Error( SDL_ENOMEM );
		return -1;
	}
	decoded = decodedBuffer;

	assert( format.basic.numChannels == 1 || format.basic.numChannels == 2 );

	// Get ready... Go!
	stereo = ( format.basic.numChannels == 2 ) ? 1 : 0;
	state[0] = &states[0];
	state[1] = &states[stereo];

	while( encoded_len >= format.basic.blockSize )
	{
		// Grab the initial information for this block
		state[0]->hPredictor = *encoded++;

		assert( state[0]->hPredictor < format.extra.adpcm.numCoef );
		state[0]->hPredictor = idMath::ClampInt( 0, 6, state[0]->hPredictor );

		state[0]->coef1 = format.extra.adpcm.aCoef[state[0]->hPredictor].coef1;
		state[0]->coef2 = format.extra.adpcm.aCoef[state[0]->hPredictor].coef2;

		if( stereo )
		{
			state[1]->hPredictor = *encoded++;

			assert( state[1]->hPredictor < format.extra.adpcm.numCoef );
			state[1]->hPredictor = idMath::ClampInt( 0, 6, state[1]->hPredictor );

			state[1]->coef1 = format.extra.adpcm.aCoef[state[1]->hPredictor].coef1;
			state[1]->coef2 = format.extra.adpcm.aCoef[state[1]->hPredictor].coef2;
		}

		state[0]->iDelta = ( ( encoded[1] << 8 ) | encoded[0] );
		encoded += sizeof( int16 );
		if( stereo )
		{
			state[1]->iDelta = ( ( encoded[1] << 8 ) | encoded[0] );
			encoded += sizeof( int16 );
		}

		state[0]->iSamp1 = ( ( encoded[1] << 8 ) | encoded[0] );
		encoded += sizeof( int16 );
		if( stereo )
		{
			state[1]->iSamp1 = ( ( encoded[1] << 8 ) | encoded[0] );
			encoded += sizeof( int16 );
		}

		state[0]->iSamp2 = ( ( encoded[1] << 8 ) | encoded[0] );
		encoded += sizeof( int16 );
		if( stereo )
		{
			state[1]->iSamp2 = ( ( encoded[1] << 8 ) | encoded[0] );
			encoded += sizeof( int16 );
		}



		// Store the two initial samples we start with
		decoded[0] = state[0]->iSamp2 & 0xFF;
		decoded[1] = ( state[0]->iSamp2 >> 8 ) & 0xFF;
		decoded += 2;
		if( stereo )
		{
			decoded[0] = state[1]->iSamp2 & 0xFF;
			decoded[1] = ( state[1]->iSamp2 >> 8 ) & 0xFF;
			decoded += 2;
		}

		decoded[0] = state[0]->iSamp1 & 0xFF;
		decoded[1] = ( state[0]->iSamp1 >> 8 ) & 0xFF;
		decoded += 2;
		if( stereo )
		{
			decoded[0] = state[1]->iSamp1 & 0xFF;
			decoded[1] = ( state[1]->iSamp1 >> 8 ) & 0xFF;
			decoded += 2;
		}

		// Decode and store the other samples in this block
		samplesleft = ( format.extra.adpcm.samplesPerBlock - 2 ) * format.basic.numChannels;

		while( samplesleft > 0 )
		{
			nybble = ( *encoded ) >> 4;
			new_sample = MS_ADPCM_nibble( state[0], nybble );

			decoded[0] = new_sample & 0xFF;
			decoded[1] = ( new_sample >> 8 ) & 0xFF;
			decoded += 2;

			nybble = ( *encoded ) & 0x0F;
			new_sample = MS_ADPCM_nibble( state[1], nybble );

			decoded[0] = new_sample & 0xFF;
			decoded[1] = ( new_sample >> 8 ) & 0xFF;
			decoded += 2;

			++encoded;
			samplesleft -= 2;
		}

		encoded_len -= format.basic.blockSize;
	}

	if( encoded_len != 0 || static_cast<size_t>( decoded - decodedBuffer ) != decodedBytes )
	{
		FreeBuffer( decodedBuffer );
		return -1;
	}

	FreeBuffer( originalBuffer );
	*audio_buf = decodedBuffer;
	*audio_len = static_cast<uint32_t>( decodedBytes );

	return 0;
}

