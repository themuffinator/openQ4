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
#include <cstdlib>
#include <stdint.h>

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
static void* AllocBuffer( int size, const char* name )
{
	return Mem_Alloc( size );
}

/*
========================
FreeBuffer
========================
*/
static void FreeBuffer( void* p )
{
	return Mem_Free( p );
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
		amplitude.Clear();
		amplitude.SetNum( num );
		if( !openQ4_ReadExact( fileIn, amplitude.Ptr(), amplitude.Num() ) )
		{
			amplitude.Clear();
			return false;
		}
		if( !openQ4_ReadBigExact( fileIn, totalBufferSize ) || totalBufferSize <= 0 )
		{
			return false;
		}
		if( !openQ4_ReadBigExact( fileIn, num ) || num <= 0 || num > totalBufferSize )
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
idSoundSample_OpenAL::Load
========================
*/
void idSoundSample_OpenAL::LoadResource()
{
	FreeData();

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
	idStr baseSampleName = GetName();
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

void idSoundSample_OpenAL::CreateOpenALBuffer()
{
	if( openalBuffer != 0 || openalBufferUploadFailed )
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

			if( MS_ADPCM_decode( ( uint8** ) &buffer, &bufferSize ) < 0 )
			{
				common->Error( "idSoundSample_OpenAL::CreateOpenALBuffer: could not decode ADPCM '%s' to 16 bit format", GetName() );
			}

			buffers[0].buffer = buffer;
			buffers[0].bufferSize = bufferSize;

			totalBufferSize = bufferSize;
		}
		else if( format.basic.formatTag == idWaveFile::FORMAT_XMA2 )
		{
			// RB: not used in the PC version of the BFG edition
			common->Error( "idSoundSample_OpenAL::CreateOpenALBuffer: could not decode XMA2 '%s' to 16 bit format", GetName() );
		}
		else if( format.basic.formatTag == idWaveFile::FORMAT_EXTENSIBLE )
		{
			// RB: not used in the PC version of the BFG edition
			common->Error( "idSoundSample_OpenAL::CreateOpenALBuffer: could not decode extensible WAV format '%s' to 16 bit format", GetName() );
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
			alBufferData( openalBuffer, GetOpenALBufferFormat(), buffer, bufferSize, format.basic.samplesPerSec );
		}

		const ALenum uploadError = CheckALErrors();
		if( uploadError != AL_NO_ERROR )
		{
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
idSoundSample_OpenAL::EnsureCpuPayload

Restore through a temporary sample. Other voices may still be using our OpenAL
buffer, so reloading this object would delete an attached buffer and invalidate
their playback state. Only transfer matching CPU bytes; retain our metadata and
OpenAL handle.
========================
*/
bool idSoundSample_OpenAL::EnsureCpuPayload()
{
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
		MakeDefault();
		return false;
	}
	timestamp = wave.Timestamp();

	const uint32 dataChunkSize = wave.SeekToChunk( 'data' );
	if( dataChunkSize == 0 || dataChunkSize > ( uint32 )idMath::INT_MAX || format.basic.blockSize == 0 )
	{
		idLib::Warning( "LoadWav( %s ) : invalid data chunk", filename.c_str() );
		MakeDefault();
		return false;
	}
	totalBufferSize = ( int )dataChunkSize;

	if( format.basic.formatTag == idWaveFile::FORMAT_PCM || format.basic.formatTag == idWaveFile::FORMAT_EXTENSIBLE )
	{

		if( format.basic.bitsPerSample != 16 )
		{
			idLib::Warning( "LoadWav( %s ) : %s", filename.c_str(), "Not a 16 bit PCM wav file" );
			MakeDefault();
			return false;
		}
		if( totalBufferSize % format.basic.blockSize != 0 )
		{
			idLib::Warning( "LoadWav( %s ) : %s", filename.c_str(), "PCM data is not block aligned" );
			MakeDefault();
			return false;
		}

		playBegin = 0;
		playLength = ( totalBufferSize ) / format.basic.blockSize;

		buffers.SetNum( 1 );
		buffers[0].bufferSize = totalBufferSize;
		buffers[0].numSamples = playLength;
		buffers[0].buffer = AllocBuffer( totalBufferSize, GetName() );

		if( buffers[0].buffer == NULL || !openQ4_ReadWaveExact( wave, buffers[0].buffer, totalBufferSize ) )
		{
			idLib::Warning( "LoadWav( %s ) : could not read PCM data", filename.c_str() );
			MakeDefault();
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
			MakeDefault();
			return false;
		}

		playBegin = 0;
		playLength = static_cast<int>( decodedBytes /
			( static_cast<size_t>( format.basic.numChannels ) * sizeof( int16_t ) ) );

		buffers.SetNum( 1 );
		buffers[0].bufferSize = totalBufferSize;
		buffers[0].numSamples = playLength;
		buffers[0].buffer  = AllocBuffer( totalBufferSize, GetName() );

		if( buffers[0].buffer == NULL || !openQ4_ReadWaveExact( wave, buffers[0].buffer, totalBufferSize ) )
		{
			idLib::Warning( "LoadWav( %s ) : could not read ADPCM data", filename.c_str() );
			MakeDefault();
			return false;
		}

		buffers[0].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[0].buffer );

	}
	else if( format.basic.formatTag == idWaveFile::FORMAT_XMA2 )
	{

		if( format.extra.xma2.blockCount == 0 )
		{
			idLib::Warning( "LoadWav( %s ) : %s", filename.c_str(), "No data blocks in file" );
			MakeDefault();
			return false;
		}

		if( format.extra.xma2.bytesPerBlock == 0 || format.extra.xma2.bytesPerBlock > ( uint32 )idMath::INT_MAX )
		{
			idLib::Warning( "LoadWav( %s ) : invalid XMA2 block size", filename.c_str() );
			MakeDefault();
			return false;
		}
		const int bytesPerBlock = ( int )format.extra.xma2.bytesPerBlock;
		const uint64 requiredBlocks = ( ( uint64 )totalBufferSize + bytesPerBlock - 1 ) / bytesPerBlock;
		if( format.extra.xma2.blockCount != requiredBlocks )
		{
			idLib::Warning( "LoadWav( %s ) : invalid XMA2 block layout", filename.c_str() );
			MakeDefault();
			return false;
		}
		//assert( format.extra.xma2.blockCount == ALIGN( totalBufferSize, bytesPerBlock ) / bytesPerBlock );
		//assert( format.extra.xma2.blockCount * bytesPerBlock >= totalBufferSize );
		//assert( format.extra.xma2.blockCount * bytesPerBlock < totalBufferSize + bytesPerBlock );

		buffers.SetNum( ( int )format.extra.xma2.blockCount );
		for( int i = 0; i < buffers.Num(); i++ )
		{
			if( i == buffers.Num() - 1 )
			{
				buffers[i].bufferSize = totalBufferSize - ( i * bytesPerBlock );
			}
			else
			{
				buffers[i].bufferSize = bytesPerBlock;
			}

			buffers[i].buffer = AllocBuffer( buffers[i].bufferSize, GetName() );
			if( buffers[i].buffer == NULL || !openQ4_ReadWaveExact( wave, buffers[i].buffer, buffers[i].bufferSize ) )
			{
				for( int j = 0; j <= i; j++ )
				{
					FreeBuffer( buffers[j].buffer );
				}
				buffers.Clear();
				idLib::Warning( "LoadWav( %s ) : could not read XMA2 data", filename.c_str() );
				MakeDefault();
				return false;
			}
			buffers[i].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[i].buffer );
		}

		int seekTableSize = wave.SeekToChunk( 'seek' );
		if( seekTableSize != 4 * buffers.Num() )
		{
			idLib::Warning( "LoadWav( %s ) : %s", filename.c_str(), "Wrong number of entries in seek table" );
			MakeDefault();
			return false;
		}

		for( int i = 0; i < buffers.Num(); i++ )
		{
			if( !openQ4_ReadWaveExact( wave, &buffers[i].numSamples, sizeof( buffers[i].numSamples ) ) )
			{
				idLib::Warning( "LoadWav( %s ) : could not read XMA2 seek table", filename.c_str() );
				MakeDefault();
				return false;
			}
			idSwap::Big( buffers[i].numSamples );
			if( buffers[i].numSamples < 0 )
			{
				idLib::Warning( "LoadWav( %s ) : invalid XMA2 seek entry", filename.c_str() );
				MakeDefault();
				return false;
			}
		}

		if( format.extra.xma2.loopBegin > ( uint32 )idMath::INT_MAX || format.extra.xma2.loopLength > ( uint32 )idMath::INT_MAX )
		{
			idLib::Warning( "LoadWav( %s ) : invalid XMA2 loop range", filename.c_str() );
			MakeDefault();
			return false;
		}

		playBegin = ( int )format.extra.xma2.loopBegin;
		playLength = ( int )format.extra.xma2.loopLength;
		if( playLength < 0 || playLength > idMath::INT_MAX - playBegin )
		{
			idLib::Warning( "LoadWav( %s ) : invalid XMA2 loop range", filename.c_str() );
			MakeDefault();
			return false;
		}

		if( buffers[buffers.Num() - 1].numSamples < playBegin + playLength )
		{
			if( buffers[buffers.Num() - 1].numSamples < playBegin )
			{
				idLib::Warning( "LoadWav( %s ) : invalid XMA2 loop range", filename.c_str() );
				MakeDefault();
				return false;
			}
			// This shouldn't happen, but it's not fatal if it does
			playLength = buffers[buffers.Num() - 1].numSamples - playBegin;
		}
		else
		{
			// Discard samples beyond playLength
			for( int i = 0; i < buffers.Num(); i++ )
			{
				if( buffers[i].numSamples > playBegin + playLength )
				{
					buffers[i].numSamples = playBegin + playLength;
					// Ideally, the following loop should always have 0 iterations because playBegin + playLength ends in the last block already
					// But there is no guarantee for that, so to be safe, discard all buffers beyond this one
					for( int j = i + 1; j < buffers.Num(); j++ )
					{
						FreeBuffer( buffers[j].buffer );
					}
					buffers.SetNum( i + 1 );
					break;
				}
			}
		}

	}
	else
	{
		idLib::Warning( "LoadWav( %s ) : Unsupported wave format %d", filename.c_str(), format.basic.formatTag );
		MakeDefault();
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
idSoundSample_OpenAL::LoadOgg
========================
*/
bool idSoundSample_OpenAL::LoadOgg( const idStr& filename )
{
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

	byte* fileData = ( byte* )Mem_Alloc( fileLen );
	if( fileData == NULL )
	{
		return false;
	}

	const int bytesRead = fileIn->Read( fileData, fileLen );
	if( bytesRead != fileLen )
	{
		Mem_Free( fileData );
		return false;
	}

	timestamp = fileIn->Timestamp();

	idStr ampName = filename;
	ampName.SetFileExtension( "amp" );
	LoadAmplitude( ampName );

	int channels = 0;
	int sampleRate = 0;
	short* decoded = NULL;
	const int samplesPerChannel = stb_vorbis_decode_memory( fileData, fileLen, &channels, &sampleRate, &decoded );

	Mem_Free( fileData );

	if( samplesPerChannel <= 0 || decoded == NULL )
	{
		if( decoded != NULL )
		{
			free( decoded );
		}
		idLib::Warning( "LoadOgg( %s ) : failed to decode Ogg Vorbis", filename.c_str() );
		MakeDefault();
		return false;
	}

	if( channels < 1 || channels > 2 )
	{
		free( decoded );
		idLib::Warning( "LoadOgg( %s ) : unsupported channel count %d", filename.c_str(), channels );
		MakeDefault();
		return false;
	}
	if( sampleRate <= 0 )
	{
		free( decoded );
		idLib::Warning( "LoadOgg( %s ) : invalid sample rate %d", filename.c_str(), sampleRate );
		MakeDefault();
		return false;
	}

	const uint64 decodedBytes = ( uint64 )samplesPerChannel * channels * sizeof( int16 );
	if( decodedBytes == 0 || decodedBytes > ( uint64 )idMath::INT_MAX )
	{
		free( decoded );
		idLib::Warning( "LoadOgg( %s ) : decoded data is too large", filename.c_str() );
		MakeDefault();
		return false;
	}

	memset( &format, 0, sizeof( format ) );
	format.basic.formatTag = idWaveFile::FORMAT_PCM;
	format.basic.numChannels = ( uint16 )channels;
	format.basic.samplesPerSec = sampleRate;
	format.basic.bitsPerSample = 16;
	format.basic.blockSize = format.basic.numChannels * format.basic.bitsPerSample / 8;
	format.basic.avgBytesPerSec = format.basic.samplesPerSec * format.basic.blockSize;

	playBegin = 0;
	playLength = samplesPerChannel;

	totalBufferSize = ( int )decodedBytes;

	buffers.SetNum( 1 );
	buffers[0].bufferSize = totalBufferSize;
	buffers[0].numSamples = playLength;
	buffers[0].buffer = AllocBuffer( totalBufferSize, GetName() );
	if( buffers[0].buffer == NULL )
	{
		free( decoded );
		idLib::Warning( "LoadOgg( %s ) : could not allocate decoded audio", filename.c_str() );
		MakeDefault();
		return false;
	}

	memcpy( buffers[0].buffer, decoded, totalBufferSize );
	buffers[0].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[0].buffer );

	free( decoded );

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
	if( fileIn == NULL )
	{
		return false;
	}

	const int fileLen = fileIn->Length();
	if( fileLen < 16 )
	{
		return false;
	}

	byte* fileData = ( byte* )Mem_Alloc( fileLen );
	if( fileData == NULL )
	{
		return false;
	}

	const int bytesRead = fileIn->Read( fileData, fileLen );
	if( bytesRead != fileLen )
	{
		Mem_Free( fileData );
		return false;
	}

	const int roqMagic = fileData[0] | ( fileData[1] << 8 );
	if( roqMagic != ROQ_FILE_MAGIC )
	{
		Mem_Free( fileData );
		return false;
	}

	timestamp = fileIn->Timestamp();

	idStr ampName = filename;
	ampName.SetFileExtension( "amp" );
	LoadAmplitude( ampName );

	int channels = 0;
	idList<int16> pcmSamples;
	pcmSamples.SetGranularity( 4096 );

	int offset = 8;
	while( offset + 8 <= fileLen )
	{
		const int chunkId = fileData[offset + 0] | ( fileData[offset + 1] << 8 );
		const int chunkSize = fileData[offset + 2] | ( fileData[offset + 3] << 8 ) | ( fileData[offset + 4] << 16 );
		const int chunkArg = fileData[offset + 6] | ( fileData[offset + 7] << 8 );

		offset += 8;

		if( chunkSize > fileLen - offset )
		{
			Mem_Free( fileData );
			idLib::Warning( "LoadRoQ( %s ) : truncated audio chunk", filename.c_str() );
			return false;
		}

		const byte* chunkData = fileData + offset;

		if( chunkId == ROQ_SOUND_MONO )
		{
			if( channels == 0 )
			{
				channels = 1;
			}
			else if( channels != 1 )
			{
				Mem_Free( fileData );
				idLib::Warning( "LoadRoQ( %s ) : mixed mono/stereo RoQ audio chunks", filename.c_str() );
				return false;
			}

			const int oldSamples = pcmSamples.Num();
			if( chunkSize > idMath::INT_MAX - oldSamples )
			{
				Mem_Free( fileData );
				idLib::Warning( "LoadRoQ( %s ) : decoded audio is too large", filename.c_str() );
				return false;
			}
			pcmSamples.SetNum( oldSamples + chunkSize );

			int32 predictor = static_cast<int16>( chunkArg );
			for( int i = 0; i < chunkSize; ++i )
			{
				predictor += RoQAudioDelta( chunkData[i] );
				predictor = idMath::ClampInt( ROQ_AUDIO_MIN_SAMPLE, ROQ_AUDIO_MAX_SAMPLE, predictor );
				pcmSamples[ oldSamples + i ] = static_cast<int16>( predictor );
			}
		}
		else if( chunkId == ROQ_SOUND_STEREO )
		{
			if( ( chunkSize & 1 ) != 0 )
			{
				Mem_Free( fileData );
				idLib::Warning( "LoadRoQ( %s ) : malformed stereo audio chunk", filename.c_str() );
				return false;
			}
			if( channels == 0 )
			{
				channels = 2;
			}
			else if( channels != 2 )
			{
				Mem_Free( fileData );
				idLib::Warning( "LoadRoQ( %s ) : mixed mono/stereo RoQ audio chunks", filename.c_str() );
				return false;
			}

			const int stereoBytes = chunkSize & ~1;
			const int samplePairs = stereoBytes >> 1;
			const int oldSamples = pcmSamples.Num();
			if( stereoBytes > idMath::INT_MAX - oldSamples )
			{
				Mem_Free( fileData );
				idLib::Warning( "LoadRoQ( %s ) : decoded audio is too large", filename.c_str() );
				return false;
			}
			pcmSamples.SetNum( oldSamples + samplePairs * 2 );

			int32 leftPredictor = static_cast<int16>( chunkArg & 0xFF00 );
			int32 rightPredictor = static_cast<int16>( ( chunkArg & 0x00FF ) << 8 );
			int outIndex = oldSamples;

			for( int i = 0; i < stereoBytes; i += 2 )
			{
				leftPredictor += RoQAudioDelta( chunkData[i + 0] );
				rightPredictor += RoQAudioDelta( chunkData[i + 1] );
				leftPredictor = idMath::ClampInt( ROQ_AUDIO_MIN_SAMPLE, ROQ_AUDIO_MAX_SAMPLE, leftPredictor );
				rightPredictor = idMath::ClampInt( ROQ_AUDIO_MIN_SAMPLE, ROQ_AUDIO_MAX_SAMPLE, rightPredictor );

				pcmSamples[ outIndex++ ] = static_cast<int16>( leftPredictor );
				pcmSamples[ outIndex++ ] = static_cast<int16>( rightPredictor );
			}
		}

		offset += chunkSize;
	}

	Mem_Free( fileData );

	if( channels == 0 || pcmSamples.Num() == 0 )
	{
		return false;
	}

	memset( &format, 0, sizeof( format ) );
	format.basic.formatTag = idWaveFile::FORMAT_PCM;
	format.basic.numChannels = static_cast<uint16>( channels );
	format.basic.samplesPerSec = ROQ_AUDIO_SAMPLE_RATE;
	format.basic.bitsPerSample = 16;
	format.basic.blockSize = static_cast<uint16>( format.basic.numChannels * format.basic.bitsPerSample / 8 );
	format.basic.avgBytesPerSec = format.basic.samplesPerSec * format.basic.blockSize;

	playBegin = 0;
	playLength = pcmSamples.Num() / channels;
	if( pcmSamples.Num() > idMath::INT_MAX / ( int )sizeof( int16 ) )
	{
		idLib::Warning( "LoadRoQ( %s ) : decoded audio is too large", filename.c_str() );
		return false;
	}
	totalBufferSize = pcmSamples.Num() * sizeof( int16 );

	buffers.SetNum( 1 );
	buffers[0].bufferSize = totalBufferSize;
	buffers[0].numSamples = playLength;
	buffers[0].buffer = AllocBuffer( totalBufferSize, GetName() );
	if( buffers[0].buffer == NULL )
	{
		idLib::Warning( "LoadRoQ( %s ) : could not allocate decoded audio", filename.c_str() );
		return false;
	}
	memcpy( buffers[0].buffer, pcmSamples.Ptr(), totalBufferSize );
	buffers[0].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[0].buffer );

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
void idSoundSample_OpenAL::FreeData()
{
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
	amplitude.Clear();

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
	amplitude.Clear();
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
	amplitude.SetNum( fileLength );
	if( !openQ4_ReadExact( f, amplitude.Ptr(), amplitude.Num() ) )
	{
		amplitude.Clear();
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

	uint8* const decodedBuffer = static_cast<uint8*>( Mem_Alloc( decodedBytes ) );
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
		Mem_Free( decodedBuffer );
		return -1;
	}

	Mem_Free( originalBuffer );
	*audio_buf = decodedBuffer;
	*audio_len = static_cast<uint32_t>( decodedBytes );

	return 0;
}

