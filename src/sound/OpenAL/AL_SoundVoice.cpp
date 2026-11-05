/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2013 Robert Beckebans

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
#include "../SoundSurround.h"
#include "../SoundQueue.h"
#include "../SoundOutputRecovery.h"

extern idCVar s_warnOnMissingSamples;
extern idCVar s_openALEfxDebugMode;

#if defined( AL_AUXILIARY_SEND_FILTER ) && defined( AL_DIRECT_FILTER ) && defined( AL_FILTER_LOWPASS ) && defined( AL_EFFECTSLOT_NULL ) && defined( AL_FILTER_NULL )
	#define OPENQ4_OPENAL_EFX_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_EFX_SUPPORTED 0
#endif

#if OPENQ4_OPENAL_EFX_SUPPORTED
typedef void ( AL_APIENTRY *openq4_alGenFilters_t )( ALsizei n, ALuint *filters );
typedef void ( AL_APIENTRY *openq4_alDeleteFilters_t )( ALsizei n, const ALuint *filters );
typedef void ( AL_APIENTRY *openq4_alFilteri_t )( ALuint filter, ALenum param, ALint iValue );
typedef void ( AL_APIENTRY *openq4_alFilterf_t )( ALuint filter, ALenum param, ALfloat flValue );

static openq4_alGenFilters_t qalGenFilters = NULL;
static openq4_alDeleteFilters_t qalDeleteFilters = NULL;
static openq4_alFilteri_t qalFilteri = NULL;
static openq4_alFilterf_t qalFilterf = NULL;

static bool openQ4_LoadVoiceEfxProcs() {
	static bool initialized = false;
	static bool available = false;

	if ( initialized ) {
		return available;
	}
	initialized = true;

	qalGenFilters = reinterpret_cast<openq4_alGenFilters_t>( alGetProcAddress( "alGenFilters" ) );
	qalDeleteFilters = reinterpret_cast<openq4_alDeleteFilters_t>( alGetProcAddress( "alDeleteFilters" ) );
	qalFilteri = reinterpret_cast<openq4_alFilteri_t>( alGetProcAddress( "alFilteri" ) );
	qalFilterf = reinterpret_cast<openq4_alFilterf_t>( alGetProcAddress( "alFilterf" ) );

	available =
		( qalGenFilters != NULL ) &&
		( qalDeleteFilters != NULL ) &&
		( qalFilteri != NULL ) &&
		( qalFilterf != NULL );
	return available;
}
#endif

#if defined( AL_SEC_OFFSET_LATENCY_SOFT )
	#define OPENQ4_OPENAL_SOURCE_LATENCY_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_SOURCE_LATENCY_SUPPORTED 0
#endif

// Centre routing submits 5.1 buffers played through direct channels.
#if defined( AL_FORMAT_51CHN16 ) && defined( AL_DIRECT_CHANNELS_SOFT )
	#define OPENQ4_OPENAL_CENTER_ROUTING_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_CENTER_ROUTING_SUPPORTED 0
#endif

// Voices are created, started and refilled on the sound (main) thread only, so
// the deep routed-queue slot count and the routing scratch are plain statics.
// AL_SoundSample.cpp's fixed reservation counts the slots and the scratch.
static int soundDeepRoutedQueueVoices = 0;
#if OPENQ4_OPENAL_CENTER_ROUTING_SUPPORTED
static short soundRoutedQueueScratch[SOUND_QUEUE_MAX_FRAMES * 6];
#endif

#if OPENQ4_OPENAL_SOURCE_LATENCY_SUPPORTED
static LPALGETSOURCEDVSOFT qalGetSourcedvSOFT = NULL;
#endif

idCVar s_skipHardwareSets( "s_skipHardwareSets", "0", CVAR_BOOL, "Do all calculation, but skip XA2 calls" );
idCVar s_debugHardware( "s_debugHardware", "0", CVAR_BOOL, "Print a message any time a hardware voice changes" );
idCVar s_sourceRadiusScale( "s_sourceRadiusScale", "0.5", CVAR_ARCHIVE | CVAR_FLOAT, "size of a sound as a fraction of its minDistance: a sound you stand next to surrounds you instead of panning hard to one side; 0 = point sources", 0.0f, 1.0f );
idCVar s_airAbsorption( "s_airAbsorption", "1", CVAR_ARCHIVE | CVAR_BOOL, "distant sounds lose high frequencies by the air absorption of the room's reverb preset" );
idCVar s_directStereo( "s_directStereo", "1", CVAR_ARCHIVE | CVAR_BOOL, "play stereo sounds such as music straight to the output channels instead of through virtual speakers or HRTF" );

// The whole system runs at this sample rate
static int SYSTEM_SAMPLE_RATE = 44100;
static float ONE_OVER_SYSTEM_SAMPLE_RATE = 1.0f / SYSTEM_SAMPLE_RATE;
static const int OPENAL_RESTART_SAMPLE_ALIGNMENT = 128;

static const float OPENQ4_OPENAL_PORTAL_DIRECT_ATTENUATION_DB = -8.0f;
static const float OPENQ4_OPENAL_PORTAL_DIRECT_HF_ATTENUATION_DB = -24.0f;
static const float OPENQ4_OPENAL_PORTAL_WET_ATTENUATION_DB = -3.0f;
static const float OPENQ4_OPENAL_PORTAL_WET_HF_ATTENUATION_DB = -10.0f;
static const float OPENQ4_OPENAL_ENV_DIRECT_ATTENUATION_DB = -2.0f;
static const float OPENQ4_OPENAL_ENV_DIRECT_HF_ATTENUATION_DB = -16.0f;
static const float OPENQ4_OPENAL_ENV_WET_ATTENUATION_DB = -1.0f;
static const float OPENQ4_OPENAL_ENV_WET_HF_ATTENUATION_DB = -12.0f;
static const float OPENQ4_OPENAL_MAX_SOURCE_GAIN = 16.0f;

struct openQ4OcclusionFilter_t
{
	float directGain;
	float directGainHF;
	float wetGain;
	float wetGainHF;
};

static float OpenQ4_DBToClampedGain( const float db )
{
	if( FLOAT_IS_NAN( db ) )
	{
		return 0.0f;
	}
	return idMath::ClampFloat( 0.0f, 1.0f, DBtoLinear( db ) );
}

static float OpenQ4_SanitizeUnitValue( const float value )
{
	if( FLOAT_IS_NAN( value ) || value <= 0.0f )
	{
		return 0.0f;
	}
	if( value >= 1.0f )
	{
		return 1.0f;
	}
	return value;
}

static float OpenQ4_SanitizeSourceGain( const float sourceGain )
{
	if( FLOAT_IS_NAN( sourceGain ) || sourceGain <= 0.0f )
	{
		return 0.0f;
	}
	return idMath::ClampFloat( 0.0f, OPENQ4_OPENAL_MAX_SOURCE_GAIN, sourceGain );
}

static openQ4OcclusionFilter_t OpenQ4_BuildOcclusionFilter( const float occlusion, const float environmentMuffle )
{
	const float clampedOcclusion = OpenQ4_SanitizeUnitValue( occlusion );
	const float clampedEnvironmentMuffle = OpenQ4_SanitizeUnitValue( environmentMuffle );

	openQ4OcclusionFilter_t filter;
	filter.directGain = OpenQ4_DBToClampedGain(
		OPENQ4_OPENAL_PORTAL_DIRECT_ATTENUATION_DB * clampedOcclusion +
		OPENQ4_OPENAL_ENV_DIRECT_ATTENUATION_DB * clampedEnvironmentMuffle );
	filter.directGainHF = OpenQ4_DBToClampedGain(
		OPENQ4_OPENAL_PORTAL_DIRECT_HF_ATTENUATION_DB * clampedOcclusion +
		OPENQ4_OPENAL_ENV_DIRECT_HF_ATTENUATION_DB * clampedEnvironmentMuffle );
	filter.wetGain = OpenQ4_DBToClampedGain(
		OPENQ4_OPENAL_PORTAL_WET_ATTENUATION_DB * clampedOcclusion +
		OPENQ4_OPENAL_ENV_WET_ATTENUATION_DB * clampedEnvironmentMuffle );
	filter.wetGainHF = OpenQ4_DBToClampedGain(
		OPENQ4_OPENAL_PORTAL_WET_HF_ATTENUATION_DB * clampedOcclusion +
		OPENQ4_OPENAL_ENV_WET_HF_ATTENUATION_DB * clampedEnvironmentMuffle );
	return filter;
}

/*
========================
idSoundVoice_OpenAL::idSoundVoice_OpenAL
========================
*/
idSoundVoice_OpenAL::idSoundVoice_OpenAL()
	:
	openalSource( 0 ),
	soundSettingsSourceGeneration( 0 ),
	soundSettingsDeleteFilters( NULL ),
	openalDirectFilter( 0 ),
	openalAuxFilter( 0 ),
	openalAreaAuxFilter( 0 ),
	nextQueuedSample( NULL ),
	oggStream( NULL ),
	oggStreamSample( NULL ),
	nextQueuedBuffer( 0 ),
	nextQueuedOffset( 0 ),
	queuedBufferPlaybackActive( false ),
	leadinSample( NULL ),
	loopingSample( NULL ),
	formatTag( 0 ),
	numChannels( 0 ),
	sampleRate( 0 ),
	hasVUMeter( false ),
	paused( true ),
	centerRouting( false ),
	outputRecovery( false ),
	outputSerial( 0 ),
	startFlags( 0 ),
	deepRoutedQueue( false ),
	appliedSourceRadius( -1.0f )
{
	openalStreamingBuffer[0] = 0;
	openalStreamingBuffer[1] = 0;
	openalStreamingBuffer[2] = 0;
	openalStreamingBufferQueued[0] = false;
	openalStreamingBufferQueued[1] = false;
	openalStreamingBufferQueued[2] = false;
}

/*
========================
idSoundVoice_OpenAL::~idSoundVoice_OpenAL
========================
*/
idSoundVoice_OpenAL::~idSoundVoice_OpenAL()
{
	DestroyInternal();
}

/*
========================
idSoundVoice_OpenAL::CompatibleFormat
========================
*/
bool idSoundVoice_OpenAL::CompatibleFormat( idSoundSample_OpenAL* s )
{
	if( s == NULL )
	{
		return false;
	}

	return !IsPlaying();
}

bool idSoundVoice_OpenAL::UsesStreamingBuffers() const
{
	return openalStreamingBuffer[0] != 0 || openalStreamingBuffer[1] != 0 || openalStreamingBuffer[2] != 0;
}

bool idSoundVoice_OpenAL::UsesBoundedQueue() const
{
	return centerRouting || ( leadinSample != NULL && leadinSample->openalBuffer == 0 ) ||
		( loopingSample != NULL && loopingSample->openalBuffer == 0 );
}

bool idSoundVoice_OpenAL::RecoversAtClock() const
{
	return outputRecovery || UsesBoundedQueue();
}

bool idSoundVoice_OpenAL::WantsCenterRouting() const
{
#if OPENQ4_OPENAL_CENTER_ROUTING_SUPPORTED
	return SoundUseCenterChannel( soundSystemLocal.hardware.HasCenterChannelOutput(),
		numChannels == 1 && ( loopingSample == NULL || loopingSample->NumChannels() == 1 ),
		( startFlags & SSF_CENTER ) != 0, centerChannel, position.LengthSqr() );
#else
	return false;
#endif
}

/*
========================
idSoundVoice_OpenAL::CheckOutput

The output serial moves when the device is reopened, reset in place, gains or
loses its centre speaker, or returns after it was lost. A source stopped across
that, or routed for a layout the output no longer has, hands itself back so its
channel restarts it at the scene clock; one that kept playing on an equivalent
layout carries on. A paused voice keeps its serial: UnPause decides.
========================
*/
bool idSoundVoice_OpenAL::CheckOutput()
{
	const idSoundHardware_OpenAL& hardware = soundSystemLocal.hardware;
	const unsigned int serial = hardware.GetOutputSerial();
	if( serial == outputSerial || paused )
	{
		return true;
	}
	ALint state = AL_INITIAL;
	alGetSourcei( openalSource, AL_SOURCE_STATE, &state );
	const bool stopped = CheckALErrors() == AL_NO_ERROR && state == AL_STOPPED;
	const bool wantsRouting = WantsCenterRouting();
	switch( SoundOutputVoiceCheck( outputSerial, serial, hardware.IsOutputConnected(), stopped, centerRouting, wantsRouting ) )
	{
		case SoundOutputVoiceAction::Recover:
			if( s_debugHardware.GetBool() )
			{
				idLib::Printf( "Output change recovery: %s (%s, routed %d -> %d)\n", leadinSample->GetName(),
					stopped ? "stopped" : "playing", centerRouting ? 1 : 0, wantsRouting ? 1 : 0 );
			}
			outputRecovery = true;
			return false;
		case SoundOutputVoiceAction::Adopt:
			outputSerial = serial;
			return true;
		default:
			return true;
	}
}

int idSoundVoice_OpenAL::StreamingBufferIndex( const ALuint buffer ) const
{
	if( buffer == 0 )
	{
		return -1;
	}
	for( int i = 0; i < MAX_QUEUED_BUFFERS; i++ )
	{
		if( openalStreamingBuffer[i] == buffer )
		{
			return i;
		}
	}
	return -1;
}

bool idSoundVoice_OpenAL::IsStreamingBuffer( const ALuint buffer ) const
{
	return StreamingBufferIndex( buffer ) >= 0;
}

void idSoundVoice_OpenAL::ResetStreamingBufferQueueState()
{
	for( int i = 0; i < MAX_QUEUED_BUFFERS; i++ )
	{
		openalStreamingBufferQueued[i] = false;
	}
}

bool idSoundVoice_OpenAL::HasQueuedBufferState() const
{
	return queuedBufferPlaybackActive || nextQueuedSample != NULL;
}

bool idSoundVoice_OpenAL::SampleHasPlayableOpenALPayload( const idSoundSample_OpenAL* sample )
{
	if( sample == NULL )
	{
		return false;
	}
	if( sample->playLength <= 0 || sample->buffers.Num() <= 0 || sample->format.basic.samplesPerSec <= 0 )
	{
		return false;
	}
	if( sample->format.basic.numChannels <= 0 || sample->format.basic.numChannels > 2 )
	{
		return false;
	}
	if( sample->openalBuffer != 0 )
	{
		return true;
	}
	return sample->format.basic.formatTag == idWaveFile::FORMAT_PCM &&
		   sample->format.basic.blockSize > 0;
}

bool idSoundVoice_OpenAL::SourceHasPlayableBuffer() const
{
	if( !alIsSource( openalSource ) )
	{
		return false;
	}

	ALint sourceType = AL_UNDETERMINED;
	alGetSourcei( openalSource, AL_SOURCE_TYPE, &sourceType );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	if( sourceType == AL_STATIC )
	{
		ALint buffer = 0;
		alGetSourcei( openalSource, AL_BUFFER, &buffer );
		return CheckALErrors() == AL_NO_ERROR && buffer != 0;
	}

	if( sourceType == AL_STREAMING )
	{
		ALint queuedBuffers = 0;
		alGetSourcei( openalSource, AL_BUFFERS_QUEUED, &queuedBuffers );
		if( CheckALErrors() != AL_NO_ERROR || queuedBuffers <= 0 )
		{
			return false;
		}

		ALint processedBuffers = 0;
		alGetSourcei( openalSource, AL_BUFFERS_PROCESSED, &processedBuffers );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			return false;
		}

		return queuedBuffers > Max( 0, processedBuffers );
	}

	return false;
}

void idSoundVoice_OpenAL::ResetQueuedBufferState()
{
	// A block still decoding stays with its worker; the stream is freed once it lets go.
	idSoundOggStream::Release( oggStream );
	oggStream = NULL;
	oggStreamSample = NULL;
	nextQueuedSample = NULL;
	nextQueuedBuffer = 0;
	nextQueuedOffset = 0;
	queuedBufferPlaybackActive = false;
}

void idSoundVoice_OpenAL::DeleteStreamingBuffers()
{
	ALuint buffersToDelete[MAX_QUEUED_BUFFERS];
	ALsizei numBuffersToDelete = 0;
	for( int i = 0; i < MAX_QUEUED_BUFFERS; i++ )
	{
		if( openalStreamingBuffer[i] != 0 )
		{
			buffersToDelete[numBuffersToDelete++] = openalStreamingBuffer[i];
		}
	}
	if( numBuffersToDelete > 0 )
	{
		CheckALErrors();
		alDeleteBuffers( numBuffersToDelete, buffersToDelete );
		CheckALErrors();
	}
	for( int i = 0; i < MAX_QUEUED_BUFFERS; i++ )
	{
		openalStreamingBuffer[i] = 0;
		openalStreamingBufferQueued[i] = false;
	}
	// The deep slot pays for these buffers' storage; it ends with them.
	if( deepRoutedQueue )
	{
		deepRoutedQueue = false;
		--soundDeepRoutedQueueVoices;
	}
}

int idSoundVoice_OpenAL::RoutedQueueFrames( int sampleRate )
{
	if( !deepRoutedQueue && soundDeepRoutedQueueVoices < SOUND_QUEUE_DEEP_ROUTED_VOICES )
	{
		deepRoutedQueue = true;
		++soundDeepRoutedQueueVoices;
	}
	return SoundQueueFrames( sampleRate, SOUND_QUEUE_ROUTED_FRAME_BYTES,
		deepRoutedQueue ? SOUND_QUEUE_DEEP_ROUTED_BUFFER_BYTES : SOUND_QUEUE_BUFFER_BYTES );
}

bool idSoundVoice_OpenAL::EnsureStreamingBuffers()
{
	if( UsesStreamingBuffers() )
	{
		return true;
	}

	CheckALErrors();
	alGenBuffers( MAX_QUEUED_BUFFERS, openalStreamingBuffer );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		DeleteStreamingBuffers();
		return false;
	}
	ResetStreamingBufferQueueState();
	return true;
}

void idSoundVoice_OpenAL::FailCreate()
{
	DestroyInternal();
	leadinSample = NULL;
	loopingSample = NULL;
	formatTag = 0;
	numChannels = 0;
	sampleRate = 0;
	sourceVoiceRate = 0;
	hasVUMeter = false;
	paused = true;
}

void idSoundVoice_OpenAL::AdvanceQueuedBuffer()
{
	if( nextQueuedSample == NULL )
	{
		return;
	}

	idSoundSample_OpenAL* const advancedSample = nextQueuedSample;
	for( nextQueuedBuffer++; nextQueuedBuffer < advancedSample->buffers.Num(); nextQueuedBuffer++ )
	{
		int bufferStart = 0;
		int bufferSamples = 0;
		int playableOffset = 0;
		int playableSamples = 0;
		if( GetPlayableBufferRange( advancedSample, nextQueuedBuffer, bufferStart, bufferSamples, playableOffset, playableSamples ) )
		{
			nextQueuedOffset = playableOffset;
			return;
		}
	}

	if( advancedSample == leadinSample && loopingSample != NULL )
	{
		SetNextQueuedSampleStart( loopingSample );
		return;
	}

	if( advancedSample == loopingSample && loopingSample != NULL )
	{
		SetNextQueuedSampleStart( loopingSample );
		return;
	}

	nextQueuedSample = NULL;
	nextQueuedBuffer = 0;
	nextQueuedOffset = 0;
}

bool idSoundVoice_OpenAL::GetPlayableBufferRange( const idSoundSample_OpenAL* sample, int bufferNumber, int& bufferStart, int& bufferSamples, int& playableOffset, int& playableSamples )
{
	bufferStart = 0;
	bufferSamples = 0;
	playableOffset = 0;
	playableSamples = 0;

	if( sample == NULL || bufferNumber < 0 || bufferNumber >= sample->buffers.Num() )
	{
		return false;
	}
	if( sample->playBegin < 0 || sample->playLength <= 0 || sample->playLength > idMath::INT_MAX - sample->playBegin )
	{
		return false;
	}

	const idSoundSample_OpenAL::sampleBuffer_t& sampleBuffer = sample->buffers[bufferNumber];
	bufferStart = bufferNumber > 0 ? sample->buffers[bufferNumber - 1].numSamples : 0;
	const int bufferEnd = sampleBuffer.numSamples;
	if( bufferStart < 0 || bufferEnd <= bufferStart )
	{
		return false;
	}

	bufferSamples = bufferEnd - bufferStart;
	const int playEnd = sample->playBegin + sample->playLength;
	const int playableStart = Max( bufferStart, sample->playBegin );
	const int playableEnd = Min( bufferEnd, playEnd );
	if( playableEnd <= playableStart )
	{
		return false;
	}

	playableOffset = playableStart - bufferStart;
	playableSamples = playableEnd - playableStart;
	return playableOffset >= 0 && playableSamples > 0 && playableOffset + playableSamples <= bufferSamples;
}

bool idSoundVoice_OpenAL::SetNextQueuedSampleStart( idSoundSample_OpenAL* sample )
{
	if( sample == NULL || sample->buffers.Num() <= 0 )
	{
		nextQueuedSample = NULL;
		nextQueuedBuffer = 0;
		nextQueuedOffset = 0;
		return false;
	}

	for( int i = 0; i < sample->buffers.Num(); i++ )
	{
		int bufferStart = 0;
		int bufferSamples = 0;
		int playableOffset = 0;
		int playableSamples = 0;
		if( GetPlayableBufferRange( sample, i, bufferStart, bufferSamples, playableOffset, playableSamples ) )
		{
			nextQueuedSample = sample;
			nextQueuedBuffer = i;
			nextQueuedOffset = playableOffset;
			return true;
		}
	}

	nextQueuedSample = NULL;
	nextQueuedBuffer = 0;
	nextQueuedOffset = 0;
	return false;
}

/*
========================
idSoundVoice_OpenAL::Create
========================
*/
void idSoundVoice_OpenAL::Create( const idSoundSample* leadinSample_, const idSoundSample* loopingSample_ )
{
	if( IsPlaying() )
	{
		Stop();
	}

	leadinSample = ( idSoundSample_OpenAL* )leadinSample_;
	loopingSample = ( idSoundSample_OpenAL* )loopingSample_;
	ResetQueuedBufferState();

	if( leadinSample == NULL )
	{
		return;
	}

	if( leadinSample->openalBuffer == 0 && leadinSample->format.basic.formatTag != idWaveFile::FORMAT_XMA2 )
	{
		leadinSample->CreateOpenALBuffer();
	}
	if( loopingSample != NULL && loopingSample->openalBuffer == 0 && loopingSample->format.basic.formatTag != idWaveFile::FORMAT_XMA2 )
	{
		loopingSample->CreateOpenALBuffer();
	}

	if( !SampleHasPlayableOpenALPayload( leadinSample ) ||
			( loopingSample != NULL && !SampleHasPlayableOpenALPayload( loopingSample ) ) )
	{
		if( s_debugHardware.GetBool() )
		{
			idLib::Printf( "%dms: rejected unplayable OpenAL payload for %s%s%s\n",
				Sys_Milliseconds(),
				leadinSample ? leadinSample->GetName() : "<null>",
				loopingSample != NULL ? " and " : "",
				loopingSample != NULL ? loopingSample->GetName() : "" );
		}
		FailCreate();
		return;
	}

	if( alIsSource( openalSource ) )
	{
		FlushSourceBuffers();
	}
	else
	{
		DestroyInternal();

		CheckALErrors();

		alGenSources( 1, &openalSource );
		if( CheckALErrors() != AL_NO_ERROR )
			//if( pSourceVoice == NULL )
		{
			// If this hits, then we are most likely passing an invalid sample format, which should have been caught by the loader (and the sample defaulted)
			FailCreate();
			return;
		}

		soundSettingsSourceGeneration = SoundSettings_SourceCreated();
		alSourcef( openalSource, AL_ROLLOFF_FACTOR, 0.0f );
		// AL_MAX_GAIN defaults to 1, which flattened every s_unclamped sound (57 stock
		// map speakers are authored 1-12 dB hot) back to unity before the listener gain.
		alSourcef( openalSource, AL_MAX_GAIN, OPENQ4_OPENAL_MAX_SOURCE_GAIN );
	}

	formatTag = leadinSample->format.basic.formatTag;
	numChannels = leadinSample->format.basic.numChannels;
	sampleRate = leadinSample->format.basic.samplesPerSec;

	const bool needsStreamingBuffers = leadinSample->openalBuffer == 0 || ( loopingSample != NULL && loopingSample->openalBuffer == 0 );
	if( needsStreamingBuffers )
	{
		if( !EnsureStreamingBuffers() )
		{
			FailCreate();
			return;
		}
	}
	else
	{
		DeleteStreamingBuffers();
	}

	if( s_debugHardware.GetBool() )
	{
		if( loopingSample == NULL || loopingSample == leadinSample )
		{
			idLib::Printf( "%dms: %i created for %s\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>" );
		}
		else
		{
			idLib::Printf( "%dms: %i created for %s and %s\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>", loopingSample ? loopingSample->GetName() : "<null>" );
		}
	}

	sourceVoiceRate = sampleRate;
	//pSourceVoice->SetSourceSampleRate( sampleRate );
	//pSourceVoice->SetVolume( 0.0f );

	ResetSourceMixState();
	CreateWetDryFilters();
	ApplyWetDryRouting();

	//OnBufferStart( leadinSample, 0 );
}

/*
========================
idSoundVoice_OpenAL::DestroyInternal
========================
*/
void idSoundVoice_OpenAL::DestroyInternal()
{
	if( alIsSource( openalSource ) )
	{
		if( s_debugHardware.GetBool() )
		{
			idLib::Printf( "%dms: %i destroyed\n", Sys_Milliseconds(), openalSource );
		}

		FlushSourceBuffers();
		alDeleteSources( 1, &openalSource );
		openalSource = 0;
		soundSettingsSourceGeneration = 0;
		hasVUMeter = false;
	}
	DeleteStreamingBuffers();
	ResetQueuedBufferState();
	DestroyWetDryFilters();
}

/*
========================
idSoundVoice_OpenAL::Start
========================
*/
bool idSoundVoice_OpenAL::Start( int offsetMS, int ssFlags )
{
	if( s_debugHardware.GetBool() )
	{
		idLib::Printf( "%dms: %i starting %s @ %dms\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>", offsetMS );
	}

	if( !leadinSample )
	{
		return false;
	}

	if( !alIsSource( openalSource ) )
	{
		return false;
	}
	// The output this voice plays on; CheckOutput compares it every update.
	outputSerial = soundSystemLocal.hardware.GetOutputSerial();
	outputRecovery = false;
	startFlags = ssFlags;
#if OPENQ4_OPENAL_CENTER_ROUTING_SUPPORTED
	centerRouting = WantsCenterRouting();
	if( centerRouting ) {
		if( !EnsureStreamingBuffers() ) return false;
		alSourcei( openalSource, AL_DIRECT_CHANNELS_SOFT, soundSystemLocal.hardware.GetDirectChannelsMode() );
		if( s_debugHardware.GetBool() ) idLib::Printf( "Centre routing: %s fraction=%.3f\n", leadinSample->GetName(), centerChannel );
	}
#else
	centerRouting = false;
#endif

	if( leadinSample->IsDefault() )
	{
		if ( s_warnOnMissingSamples.GetBool() ) {
			idLib::Warning( "Starting defaulted sound sample %s", leadinSample->GetName() );
		}
	}

	bool flicker = ( ssFlags & SSF_NO_FLICKER ) == 0;

	if( flicker != hasVUMeter )
	{
		hasVUMeter = flicker;

		/*
		if( flicker )
		{
			IUnknown* vuMeter = NULL;

			if( XAudio2CreateVolumeMeter( &vuMeter, 0 ) == S_OK )
			{

				XAUDIO2_EFFECT_DESCRIPTOR descriptor;
				descriptor.InitialState = true;
				descriptor.OutputChannels = leadinSample->NumChannels();
				descriptor.pEffect = vuMeter;

				XAUDIO2_EFFECT_CHAIN chain;
				chain.EffectCount = 1;
				chain.pEffectDescriptors = &descriptor;

				pSourceVoice->SetEffectChain( &chain );

				vuMeter->Release();
			}
		}
		else
		{
			pSourceVoice->SetEffectChain( NULL );
		}
		*/
	}

	offsetMS = Max( 0, offsetMS );
	int offsetSamples = MsecToSamples( offsetMS, leadinSample->SampleRate() );
	if( loopingSample == NULL && offsetSamples >= leadinSample->playLength )
	{
		return false;
	}

	if( RestartAt( offsetSamples ) <= 0 )
	{
		return false;
	}
	if( !Update() )
	{
		return false;
	}
	return UnPause();
}

/*
========================
idSoundVoice_OpenAL::RestartAt
========================
*/
int idSoundVoice_OpenAL::RestartAt( int offsetSamples )
{
	offsetSamples = Max( 0, offsetSamples );
	offsetSamples &= ~( OPENAL_RESTART_SAMPLE_ALIGNMENT - 1 );
	ResetQueuedBufferState();

	idSoundSample_OpenAL* sample = leadinSample;
	if( sample == NULL )
	{
		return 0;
	}

	if( offsetSamples >= leadinSample->playLength )
	{
		if( loopingSample != NULL && loopingSample->playLength > 0 )
		{
			if( loopingSample == leadinSample )
			{
				offsetSamples %= loopingSample->playLength;
			}
			else
			{
				offsetSamples = ( offsetSamples - leadinSample->playLength ) % loopingSample->playLength;
			}
			sample = loopingSample;
		}
		else
		{
			return 0;
		}
	}

	if( sample == NULL || sample->playLength <= 0 )
	{
		return 0;
	}

	// Both direct playback and intro/loop queues can use an uploaded OpenAL
	// buffer without CPU bytes. Only the streaming fallback uploads PCM slices;
	// restore those bytes before taking any pointers into the sample buffers.
	if( ( sample->openalBuffer == 0 || centerRouting ) && !sample->IsStreamed() && !sample->EnsureCpuPayload() )
	{
		return 0;
	}

	const idSoundSample_OpenAL::sampleBuffer_t* sampleBuffers = sample->buffers.Ptr();
	const int numBuffers = sample->buffers.Num();
	if( sampleBuffers == NULL || numBuffers <= 0 || numBuffers > 16384 )
	{
		return 0;
	}

	const size_t sampleBuffersAddress = reinterpret_cast<size_t>( sampleBuffers );
	if( sampleBuffersAddress < 4096 || ( sampleBuffersAddress & ( sizeof( void* ) - 1 ) ) != 0 )
	{
		return 0;
	}

	int previousNumSamples = 0;
	for( int i = 0; i < numBuffers; i++ )
	{
		const idSoundSample_OpenAL::sampleBuffer_t& sampleBuffer = sampleBuffers[i];
		if( sampleBuffer.numSamples <= previousNumSamples )
		{
			return 0;
		}
		if( sampleBuffer.numSamples > sample->playBegin + offsetSamples )
		{
			int bufferStart = 0;
			int bufferSamples = 0;
			int playableOffset = 0;
			int playableSamples = 0;
			if( !GetPlayableBufferRange( sample, i, bufferStart, bufferSamples, playableOffset, playableSamples ) )
			{
				return 0;
			}

			const int startSample = sample->playBegin + offsetSamples;
			const int bufferOffset = startSample - bufferStart;
			if( bufferOffset < playableOffset || bufferOffset >= playableOffset + playableSamples )
			{
				return 0;
			}
			FlushSourceBuffers();
			if( !centerRouting && sample->openalBuffer != 0 && ( loopingSample == NULL || sample == loopingSample ) )
			{
				alSourcei( openalSource, AL_BUFFER, sample->openalBuffer );
				alSourcei( openalSource, AL_LOOPING, ( sample == loopingSample && loopingSample != NULL ? AL_TRUE : AL_FALSE ) );
				if( bufferOffset > 0 )
				{
					alSourcei( openalSource, AL_SAMPLE_OFFSET, startSample );
				}
				if( CheckALErrors() != AL_NO_ERROR )
				{
					return 0;
				}
				return sample->totalBufferSize;
			}

			if( ( sample->openalBuffer == 0 || centerRouting ) && !EnsureStreamingBuffers() )
			{
				return 0;
			}
			nextQueuedSample = sample;
			nextQueuedBuffer = i;
			nextQueuedOffset = bufferOffset;
			return 1;
		}
		previousNumSamples = sampleBuffer.numSamples;
	}

	return 0;
}

/*
========================
idSoundVoice_OpenAL::SubmitBuffer
========================
*/
int idSoundVoice_OpenAL::SubmitBuffer( idSoundSample_OpenAL* sample, int bufferNumber, int offset, ALuint streamBuffer )
{
	if( sample == NULL || !alIsSource( openalSource ) )
	{
		return 0;
	}

	const idSoundSample_OpenAL::sampleBuffer_t* sampleBuffers = sample->buffers.Ptr();
	const int numBuffers = sample->buffers.Num();
	if( sampleBuffers == NULL || numBuffers <= 0 || numBuffers > 16384 )
	{
		return 0;
	}

	const size_t sampleBuffersAddress = reinterpret_cast<size_t>( sampleBuffers );
	if( sampleBuffersAddress < 4096 || ( sampleBuffersAddress & ( sizeof( void* ) - 1 ) ) != 0 )
	{
		return 0;
	}

	if( ( bufferNumber < 0 ) || ( bufferNumber >= numBuffers ) )
	{
		return 0;
	}

	if( sample->openalBuffer != 0 && !centerRouting )
	{
		ALuint staticBuffer = sample->openalBuffer;
		alSourceQueueBuffers( openalSource, 1, &staticBuffer );
		if( offset > 0 )
		{
			alSourcei( openalSource, AL_SAMPLE_OFFSET, offset );
		}
		if( CheckALErrors() != AL_NO_ERROR )
		{
			return 0;
		}

		return Max( 1, sample->totalBufferSize );
	}

	if( streamBuffer == 0 || sample->format.basic.blockSize <= 0 )
	{
		return 0;
	}

	if( sample->format.basic.formatTag != idWaveFile::FORMAT_PCM )
	{
		return 0;
	}
	if( centerRouting && !sample->EnsureCpuPayload() ) return 0;

	const idSoundSample_OpenAL::sampleBuffer_t& sampleBuffer = sampleBuffers[bufferNumber];
	if( sampleBuffer.buffer == NULL || sampleBuffer.bufferSize <= 0 )
	{
		return 0;
	}

	const int bytesPerSample = sample->format.basic.blockSize;
	if( bytesPerSample <= 0 || sampleBuffer.bufferSize % bytesPerSample != 0 )
	{
		return 0;
	}

	const int previousNumSamples = bufferNumber > 0 ? sampleBuffers[bufferNumber - 1].numSamples : 0;
	const int bufferSamples = sampleBuffer.numSamples - previousNumSamples;
	if( bufferSamples <= 0 )
	{
		return 0;
	}
	int playableBufferStart = 0;
	int playableBufferSamples = 0;
	int playableOffset = 0;
	int playableSamples = 0;
	if( !GetPlayableBufferRange( sample, bufferNumber, playableBufferStart, playableBufferSamples, playableOffset, playableSamples ) )
	{
		return 0;
	}
	if( playableBufferStart != previousNumSamples || playableBufferSamples != bufferSamples )
	{
		return 0;
	}
	if( bufferSamples > sampleBuffer.bufferSize / bytesPerSample )
	{
		return 0;
	}

	if( offset < 0 )
	{
		offset = 0;
	}
	offset = Max( offset, playableOffset );
	const int playableEndOffset = playableOffset + playableSamples;
	if( offset >= playableEndOffset || offset >= sampleBuffer.bufferSize / bytesPerSample )
	{
		return 0;
	}

	const int byteOffset = offset * bytesPerSample;
	if( byteOffset < 0 || byteOffset >= sampleBuffer.bufferSize )
	{
		return 0;
	}

	const byte* audioData = reinterpret_cast<const byte*>( sampleBuffer.buffer ) + byteOffset;
	int queueFrames = SoundQueueChunkFrames( sample->SampleRate(), bytesPerSample );
#if OPENQ4_OPENAL_CENTER_ROUTING_SUPPORTED
	if( centerRouting )
	{
		queueFrames = RoutedQueueFrames( sample->SampleRate() );
	}
#endif
	const int submitSamples = Min( queueFrames, playableEndOffset - offset );
	if( submitSamples <= 0 || submitSamples > ( sampleBuffer.bufferSize / bytesPerSample ) - offset )
	{
		return 0;
	}
	const int audioBytes = submitSamples * bytesPerSample;
	if( audioBytes <= 0 )
	{
		return 0;
	}

#if OPENQ4_OPENAL_CENTER_ROUTING_SUPPORTED
	if( centerRouting ) {
		SoundRouteMonoToCenter( reinterpret_cast<const short*>( audioData ), submitSamples, centerChannel, soundRoutedQueueScratch );
		alBufferData( streamBuffer, AL_FORMAT_51CHN16, soundRoutedQueueScratch, submitSamples * 6 * sizeof( short ), sample->SampleRate() );
	} else
#endif
	{
		alBufferData( streamBuffer, sample->GetOpenALBufferFormat(), audioData, audioBytes, sample->SampleRate() );
	}
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return 0;
	}

	alSourceQueueBuffers( openalSource, 1, &streamBuffer );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return 0;
	}

	return audioBytes;
}

int idSoundVoice_OpenAL::StreamBlockFrames( idSoundSample_OpenAL* sample, int offset )
{
	// Three independent buffers of about a third of a second each, irrespective
	// of track length; the decoder hands out at most 4096 frames per call.
	const int channels = Max( 1, sample->NumChannels() );
	int queueFrames = SoundQueueChunkFrames( sample->SampleRate(), channels * static_cast<int>( sizeof( short ) ) );
	int decodeCapacity = static_cast<int>( SOUND_QUEUE_BUFFER_BYTES / sizeof( short ) ) / channels;
#if OPENQ4_OPENAL_CENTER_ROUTING_SUPPORTED
	if( centerRouting ) {
		queueFrames = RoutedQueueFrames( sample->SampleRate() );
		decodeCapacity = SOUND_QUEUE_MAX_FRAMES;
	}
#endif
	return Min( Min( queueFrames, decodeCapacity ), sample->playLength - offset );
}

bool idSoundVoice_OpenAL::PrepareStreamBlock( idSoundSample_OpenAL* sample, int offset )
{
	const int frames = StreamBlockFrames( sample, offset );
	if( frames <= 0 ) return false;
	if( oggStream == NULL || oggStreamSample != sample ) {
		idSoundOggStream::Release( oggStream );
		oggStream = new idSoundOggStream;
		oggStreamSample = sample;
		idSoundOggStreamShape shape;
		shape.channels = sample->NumChannels();
		shape.rate = sample->SampleRate();
		shape.frames = static_cast<unsigned int>( sample->playLength );
		shape.countedLength = sample->streamCountedLength;
		shape.pageStart = sample->streamPageStart;
		shape.pageEnd = sample->streamPageEnd;
		shape.pageGranule = sample->streamPageGranule;
		if( !oggStream->Open( sample->streamName, shape ) ) return false;
	}
	switch( oggStream->PollBlock() ) {
		case idSoundOggStream::BLOCK_PENDING:
			return true;
		case idSoundOggStream::BLOCK_FAILED:
			return false;
		case idSoundOggStream::BLOCK_READY:
			if( oggStream->BlockFrame() == static_cast<unsigned int>( offset ) && oggStream->BlockRequestFrames() == frames ) return true;
			// A block for another position (the routing changed its size): decode again.
			oggStream->ConsumeBlock();
			break;
		default:
			break;
	}
	return oggStream->RequestBlock( static_cast<unsigned int>( offset ), frames );
}

bool idSoundVoice_OpenAL::StreamBlockPending() const
{
	return nextQueuedSample != NULL && oggStream != NULL && oggStreamSample == nextQueuedSample && oggStream->IsBlockPending();
}

bool idSoundVoice_OpenAL::QueueNextBuffer( ALuint streamBuffer )
{
	if( nextQueuedSample == NULL )
	{
		return false;
	}
	if( ( nextQueuedSample->openalBuffer == 0 || centerRouting ) && streamBuffer == 0 )
	{
		return false;
	}
	if( nextQueuedSample->IsStreamed() ) {
		idSoundSample_OpenAL* sample = nextQueuedSample;
		// Opening, seeking and decoding run on a job worker. Until the block is
		// ready nothing is queued; Update keeps the voice and queues it then.
		if( !PrepareStreamBlock( sample, nextQueuedOffset ) ) { ResetQueuedBufferState(); return false; }
		if( oggStream->BlockState() != idSoundOggStream::BLOCK_READY ) return false;
		const short* pcm = oggStream->Block();
		const int frames = oggStream->BlockFrames();
		const int channels = sample->NumChannels();
		if( pcm == NULL || frames <= 0 ) { ResetQueuedBufferState(); return false; }
#if OPENQ4_OPENAL_CENTER_ROUTING_SUPPORTED
		if( centerRouting ) {
			SoundRouteMonoToCenter( pcm, frames, centerChannel, soundRoutedQueueScratch );
			alBufferData( streamBuffer, AL_FORMAT_51CHN16, soundRoutedQueueScratch, frames * 6 * sizeof( short ), sample->SampleRate() );
		} else
#endif
		{
			alBufferData( streamBuffer, sample->GetOpenALBufferFormat(), pcm, frames * channels * sizeof( short ), sample->SampleRate() );
		}
		if( CheckALErrors() != AL_NO_ERROR ) { ResetQueuedBufferState(); return false; }
		alSourceQueueBuffers( openalSource, 1, &streamBuffer );
		if( CheckALErrors() != AL_NO_ERROR ) { ResetQueuedBufferState(); return false; }
		oggStream->ConsumeBlock();
		openalStreamingBufferQueued[StreamingBufferIndex( streamBuffer )] = true;
		queuedBufferPlaybackActive = true;
		nextQueuedOffset += frames;
		if( nextQueuedOffset >= sample->playLength ) AdvanceQueuedBuffer();
		if( nextQueuedSample != NULL && nextQueuedSample->IsStreamed() ) {
			// Decode the following block while this one plays. A failure here
			// surfaces at the next queue attempt, after the queued audio.
			PrepareStreamBlock( nextQueuedSample, nextQueuedOffset );
		} else {
			// Ended, or looping on a resident buffer: free the decoder now.
			idSoundOggStream::Release( oggStream );
			oggStream = NULL;
			oggStreamSample = NULL;
		}
		return true;
	}

	const int submittedBytes = SubmitBuffer( nextQueuedSample, nextQueuedBuffer, nextQueuedOffset, streamBuffer );
	if( submittedBytes <= 0 )
	{
		nextQueuedSample = NULL;
		nextQueuedBuffer = 0;
		nextQueuedOffset = 0;
		return false;
	}
	if( streamBuffer != 0 )
	{
		const int streamBufferIndex = StreamingBufferIndex( streamBuffer );
		if( streamBufferIndex >= 0 )
		{
			openalStreamingBufferQueued[streamBufferIndex] = true;
		}
	}

	queuedBufferPlaybackActive = true;
	if( centerRouting || nextQueuedSample->openalBuffer == 0 ) {
		nextQueuedOffset += submittedBytes / nextQueuedSample->format.basic.blockSize;
		int bufferStart = 0, bufferFrames = 0, playableOffset = 0, playableFrames = 0;
		if( GetPlayableBufferRange( nextQueuedSample, nextQueuedBuffer, bufferStart, bufferFrames, playableOffset, playableFrames ) &&
			nextQueuedOffset < playableOffset + playableFrames ) return true;
	}
	AdvanceQueuedBuffer();
	return true;
}

/*
========================
idSoundVoice_OpenAL::Update
========================
*/
bool idSoundVoice_OpenAL::Update()
{
	if( !alIsSource( openalSource ) || leadinSample == NULL )
	{
		return false;
	}
	// The output this voice started on was lost, replaced or re-laid out.
	if( !CheckOutput() )
	{
		return false;
	}

	if( !HasQueuedBufferState() )
	{
		// A static buffer plays on its own, so only its state can say it stopped.
		// OpenAL Soft stops every source when the device disconnects and leaves them
		// stopped after alcReopenDeviceSOFT, which left looping ambience and music
		// silent after a headset was unplugged. Hand the voice back: the channel
		// restarts a looping sound at its current offset and ends a finished one.
		// A stop by a lost endpoint is not the end of the sound: dialogue and other
		// one-shots restart at the scene clock too once the output returns.
		if( !paused )
		{
			ALint state = AL_PLAYING;
			alGetSourcei( openalSource, AL_SOURCE_STATE, &state );
			if( CheckALErrors() == AL_NO_ERROR && state == AL_STOPPED )
			{
				if( !soundSystemLocal.hardware.IsOutputConnected() )
				{
					outputRecovery = true;
				}
				return false;
			}
		}
		return true;
	}

	const bool hadQueuedState = HasQueuedBufferState();
	bool submittedAny = false;
	if( queuedBufferPlaybackActive && !paused && UsesBoundedQueue() ) {
		ALint state = AL_INITIAL;
		alGetSourcei( openalSource, AL_SOURCE_STATE, &state );
		if( CheckALErrors() != AL_NO_ERROR ) return false;
		if( state == AL_STOPPED ) {
			// The game channel will recreate this voice at its logical playback
			// time. Never replay stale queued speech after a main-thread stall.
			if( s_debugHardware.GetBool() ) idLib::Printf( "Compressed stream underrun: %s\n", leadinSample->GetName() );
			return false;
		}
	}

	ALint processedBuffers = 0;
	alGetSourcei( openalSource, AL_BUFFERS_PROCESSED, &processedBuffers );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	while( processedBuffers > 0 )
	{
		ALuint unqueuedBuffers[MAX_QUEUED_BUFFERS];
		const int unqueueCount = Min( processedBuffers, MAX_QUEUED_BUFFERS );
		if( unqueueCount <= 0 )
		{
			break;
		}
		alSourceUnqueueBuffers( openalSource, unqueueCount, unqueuedBuffers );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			ResetQueuedBufferState();
			return false;
		}
		for( int i = 0; i < unqueueCount; i++ )
		{
			// Static leadins may share a queue with streamed loops; only voice-owned buffers are scratch.
			const int streamBufferIndex = StreamingBufferIndex( unqueuedBuffers[i] );
			if( streamBufferIndex >= 0 )
			{
				openalStreamingBufferQueued[streamBufferIndex] = false;
			}
		}
		processedBuffers -= unqueueCount;
	}

	ALint queuedBuffers = 0;
	alGetSourcei( openalSource, AL_BUFFERS_QUEUED, &queuedBuffers );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	if( UsesStreamingBuffers() )
	{
		ALuint buffersToFill[MAX_QUEUED_BUFFERS];
		int buffersToFillCount = 0;

		for( int i = 0; i < MAX_QUEUED_BUFFERS; i++ )
		{
			if( openalStreamingBuffer[i] != 0 && !openalStreamingBufferQueued[i] )
			{
				buffersToFill[buffersToFillCount++] = openalStreamingBuffer[i];
			}
		}

		int bufferToFillIndex = 0;
		while( queuedBuffers < MAX_QUEUED_BUFFERS && nextQueuedSample != NULL )
		{
			ALuint streamBuffer = 0;
			if( nextQueuedSample->openalBuffer == 0 || centerRouting )
			{
				if( bufferToFillIndex >= buffersToFillCount )
				{
					break;
				}
				streamBuffer = buffersToFill[bufferToFillIndex++];
			}
			if( QueueNextBuffer( streamBuffer ) )
			{
				submittedAny = true;
				queuedBuffers++;
				continue;
			}
			break;
		}
	}
	else
	{
		while( queuedBuffers < MAX_QUEUED_BUFFERS && nextQueuedSample != NULL )
		{
			if( !QueueNextBuffer( 0 ) )
			{
				break;
			}
			submittedAny = true;
			queuedBuffers++;
		}
	}

	alGetSourcei( openalSource, AL_BUFFERS_QUEUED, &queuedBuffers );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	ALint state = AL_INITIAL;
	alGetSourcei( openalSource, AL_SOURCE_STATE, &state );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	if( !paused && queuedBuffers > 0 && state != AL_PLAYING )
	{
		alSourcePlay( openalSource );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			return false;
		}

		alGetSourcei( openalSource, AL_SOURCE_STATE, &state );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			return false;
		}
	}

	if( queuedBuffers <= 0 && nextQueuedSample == NULL && state != AL_PLAYING )
	{
		queuedBufferPlaybackActive = false;
	}

	// A stream whose first or next block is still decoding is starting, not finished.
	return queuedBuffers > 0 || state == AL_PLAYING || !hadQueuedState || submittedAny || StreamBlockPending();
}

/*
========================
idSoundVoice_OpenAL::SourceLatencyQueriesAvailable
========================
*/
bool idSoundVoice_OpenAL::SourceLatencyQueriesAvailable()
{
#if OPENQ4_OPENAL_SOURCE_LATENCY_SUPPORTED
	if( alIsExtensionPresent( "AL_SOFT_source_latency" ) != AL_TRUE )
	{
		return false;
	}
	if( qalGetSourcedvSOFT == NULL )
	{
		qalGetSourcedvSOFT = reinterpret_cast<LPALGETSOURCEDVSOFT>( alGetProcAddress( "alGetSourcedvSOFT" ) );
	}
	return qalGetSourcedvSOFT != NULL;
#else
	return false;
#endif
}

/*
========================
idSoundVoice_OpenAL::GetPlaybackLatencyMS
========================
*/
bool idSoundVoice_OpenAL::GetPlaybackLatencyMS( float& offsetMS, float& latencyMS ) const
{
	offsetMS = 0.0f;
	latencyMS = 0.0f;

#if OPENQ4_OPENAL_SOURCE_LATENCY_SUPPORTED
	if( !alIsSource( openalSource ) || !SourceLatencyQueriesAvailable() )
	{
		return false;
	}

	ALdouble offsetAndLatencySeconds[2] = { 0.0, 0.0 };
	(void)alGetError();
	qalGetSourcedvSOFT( openalSource, AL_SEC_OFFSET_LATENCY_SOFT, offsetAndLatencySeconds );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	offsetMS = Max( 0.0f, static_cast<float>( offsetAndLatencySeconds[0] * 1000.0 ) );
	latencyMS = Max( 0.0f, static_cast<float>( offsetAndLatencySeconds[1] * 1000.0 ) );
	return true;
#else
	return false;
#endif
}

/*
========================
idSoundVoice_OpenAL::IsPlaying
========================
*/
bool idSoundVoice_OpenAL::IsPlaying()
{
	if( !alIsSource( openalSource ) )
	{
		return false;
	}

	ALint state = AL_INITIAL;

	alGetSourcei( openalSource, AL_SOURCE_STATE, &state );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	if( state == AL_PLAYING || state == AL_PAUSED )
	{
		return true;
	}

	return HasQueuedBufferState() && ( SourceHasPlayableBuffer() || StreamBlockPending() );

	//XAUDIO2_VOICE_STATE state;
	//pSourceVoice->GetState( &state );

	//return ( state.BuffersQueued != 0 );
}

/*
========================
idSoundVoice_OpenAL::FlushSourceBuffers
========================
*/
void idSoundVoice_OpenAL::FlushSourceBuffers()
{
	if( !alIsSource( openalSource ) )
	{
		ResetStreamingBufferQueueState();
		ResetQueuedBufferState();
		return;
	}

	alSourceStop( openalSource );

	ALint sourceType = AL_UNDETERMINED;
	alGetSourcei( openalSource, AL_SOURCE_TYPE, &sourceType );
	CheckALErrors();
	if( sourceType == AL_STREAMING )
	{
		ALint queuedBuffers = 0;
		alGetSourcei( openalSource, AL_BUFFERS_QUEUED, &queuedBuffers );
		CheckALErrors();
		while( queuedBuffers > 0 )
		{
			ALuint unqueuedBuffers[MAX_QUEUED_BUFFERS];
			const int unqueueCount = Min( queuedBuffers, MAX_QUEUED_BUFFERS );
			alSourceUnqueueBuffers( openalSource, unqueueCount, unqueuedBuffers );
			if( CheckALErrors() != AL_NO_ERROR )
			{
				break;
			}
			queuedBuffers -= unqueueCount;
		}
	}

	alSourcei( openalSource, AL_BUFFER, 0 );
	alSourcei( openalSource, AL_LOOPING, AL_FALSE );
	// A source that has played stays AL_STOPPED, and OpenAL counts every buffer
	// queued on a stopped source as processed. A stream started on a recycled
	// voice was unqueued as fast as it was queued and never played, once the
	// voice pool had cycled. Back to AL_INITIAL, as a new source starts.
	alSourceRewind( openalSource );
	CheckALErrors();

	paused = true;
	ResetStreamingBufferQueueState();
	ResetQueuedBufferState();
}

/*
========================
idSoundVoice_OpenAL::Pause
========================
*/
void idSoundVoice_OpenAL::Pause()
{
	if( !alIsSource( openalSource ) || paused )
	{
		return;
	}

	if( s_debugHardware.GetBool() )
	{
		idLib::Printf( "%dms: %i pausing %s\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>" );
	}

	ALint state = AL_INITIAL;
	alGetSourcei( openalSource, AL_SOURCE_STATE, &state );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return;
	}

	if( state == AL_PLAYING )
	{
		alSourcePause( openalSource );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			return;
		}
	}
	//pSourceVoice->Stop( 0, OPERATION_SET );
	paused = true;
}

/*
========================
idSoundVoice_OpenAL::UnPause
========================
*/
bool idSoundVoice_OpenAL::UnPause()
{
	if( !alIsSource( openalSource ) )
	{
		return false;
	}
	if( !paused )
	{
		return true;
	}

	if( s_debugHardware.GetBool() )
	{
		idLib::Printf( "%dms: %i unpausing %s\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>" );
	}

	// OpenAL plays a paused source that lost its voice to an output change from its
	// first sample. Leave it stopped instead: the next update hands it back and the
	// channel restarts it at the scene clock.
	if( !SoundOutputResumePaused( outputSerial, soundSystemLocal.hardware.GetOutputSerial(), soundSystemLocal.hardware.IsOutputConnected() ) )
	{
		alSourceStop( openalSource );
		CheckALErrors();
		paused = false;
		outputRecovery = true;
		return false;
	}

	if( HasQueuedBufferState() && !SourceHasPlayableBuffer() )
	{
		if( !Update() )
		{
			return false;
		}
	}

	if( !SourceHasPlayableBuffer() )
	{
		// A stream still decoding its block plays from Update once it is queued.
		paused = false;
		return StreamBlockPending();
	}

	alSourcePlay( openalSource );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}
	//pSourceVoice->Start( 0, OPERATION_SET );
	paused = false;
	return true;
}

/*
========================
idSoundVoice_OpenAL::Stop
========================
*/
void idSoundVoice_OpenAL::Stop()
{
	if( !alIsSource( openalSource ) )
	{
		return;
	}

	if( !paused && s_debugHardware.GetBool() )
	{
		idLib::Printf( "%dms: %i stopping %s\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>" );
	}

	FlushSourceBuffers();
	// The decoder reservation covers these buffers only while the stream is
	// alive. A free voice must not retain unaccounted copies of the last blocks.
	if( UsesBoundedQueue() ) {
		DeleteStreamingBuffers();
	}
}

/*
========================
idSoundVoice_OpenAL::GetAmplitude
========================
*/
float idSoundVoice_OpenAL::GetAmplitude()
{
	// TODO
	return 1.0f;

	/*
	if( !hasVUMeter )
	{
		return 1.0f;
	}

	float peakLevels[ MAX_CHANNELS_PER_VOICE ];
	float rmsLevels[ MAX_CHANNELS_PER_VOICE ];

	XAUDIO2FX_VOLUMEMETER_LEVELS levels;
	levels.ChannelCount = leadinSample->NumChannels();
	levels.pPeakLevels = peakLevels;
	levels.pRMSLevels = rmsLevels;

	if( levels.ChannelCount > MAX_CHANNELS_PER_VOICE )
	{
		levels.ChannelCount = MAX_CHANNELS_PER_VOICE;
	}

	if( pSourceVoice->GetEffectParameters( 0, &levels, sizeof( levels ) ) != S_OK )
	{
		return 0.0f;
	}

	if( levels.ChannelCount == 1 )
	{
		return rmsLevels[0];
	}

	float rms = 0.0f;
	for( uint32 i = 0; i < levels.ChannelCount; i++ )
	{
		rms += rmsLevels[i];
	}

	return rms / ( float )levels.ChannelCount;
	*/
}

/*
========================
idSoundVoice_OpenAL::ResetSampleRate
========================
*/
void idSoundVoice_OpenAL::SetSampleRate( uint32 newSampleRate, uint32 operationSet )
{
	/*
	if( pSourceVoice == NULL || leadinSample == NULL )
	{
		return;
	}

	sampleRate = newSampleRate;

	XAUDIO2_FILTER_PARAMETERS filter;
	filter.Type = LowPassFilter;
	filter.OneOverQ = 1.0f;			// [0.0f, XAUDIO2_MAX_FILTER_ONEOVERQ]
	float cutoffFrequency = 1000.0f / Max( 0.01f, occlusion );
	if( cutoffFrequency * 6.0f >= ( float )sampleRate )
	{
		filter.Frequency = XAUDIO2_MAX_FILTER_FREQUENCY;
	}
	else
	{
		filter.Frequency = 2.0f * idMath::Sin( idMath::PI * cutoffFrequency / ( float )sampleRate );
	}
	assert( filter.Frequency >= 0.0f && filter.Frequency <= XAUDIO2_MAX_FILTER_FREQUENCY );
	filter.Frequency = idMath::ClampFloat( 0.0f, XAUDIO2_MAX_FILTER_FREQUENCY, filter.Frequency );

	pSourceVoice->SetFilterParameters( &filter, operationSet );

	float freqRatio = pitch * ( float )sampleRate / ( float )sourceVoiceRate;
	assert( freqRatio >= XAUDIO2_MIN_FREQ_RATIO && freqRatio <= XAUDIO2_MAX_FREQ_RATIO );
	freqRatio = idMath::ClampFloat( XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_MAX_FREQ_RATIO, freqRatio );

	// if the value specified for maxFreqRatio is too high for the specified format, the call to CreateSourceVoice will fail
	if( numChannels == 1 )
	{
		assert( freqRatio * ( float )SYSTEM_SAMPLE_RATE <= XAUDIO2_MAX_RATIO_TIMES_RATE_XMA_MONO );
	}
	else
	{
		assert( freqRatio * ( float )SYSTEM_SAMPLE_RATE <= XAUDIO2_MAX_RATIO_TIMES_RATE_XMA_MULTICHANNEL );
	}

	pSourceVoice->SetFrequencyRatio( freqRatio, operationSet );
	*/
}

/*
========================
idSoundVoice_OpenAL::ResetSourceMixState
========================
*/
void idSoundVoice_OpenAL::ResetSourceMixState()
{
	position.Zero();
	velocity.Zero();
	gain = 0.0f;
	centerChannel = 0.0f;
	centerRouting = false;
	pitch = 1.0f;
	wetLevel = 0.0f;
	dryLevel = 1.0f;
	innerRadius = 32.0f;
	occlusion = 0.0f;
	environmentMuffle = 0.0f;
	reverbAreaSlot = -1;
	eaxDirectGain = 1.0f;
	eaxDirectGainHF = 1.0f;
	eaxPrimaryGain = 0.0f;
	eaxPrimaryGainHF = 1.0f;
	eaxAreaGain = 0.0f;
	eaxAreaGainHF = 1.0f;
	channelMask = 0;
	innerSampleRangeSqr = 0.0f;
	outerSampleRangeSqr = 0.0f;

	if( !alIsSource( openalSource ) )
	{
		return;
	}

	alSourcei( openalSource, AL_SOURCE_RELATIVE, AL_TRUE );
	alSource3f( openalSource, AL_POSITION, 0.0f, 0.0f, 0.0f );
	alSource3f( openalSource, AL_VELOCITY, 0.0f, 0.0f, 0.0f );
	alSourcef( openalSource, AL_GAIN, 0.0f );
	alSourcef( openalSource, AL_PITCH, 1.0f );
	alSourcef( openalSource, AL_REFERENCE_DISTANCE, Max( innerRadius, 0.001f ) );
	alSourcei( openalSource, AL_LOOPING, AL_FALSE );
	const idSoundHardware_OpenAL& hardware = soundSystemLocal.hardware;
#if defined( AL_SOURCE_RADIUS )
	if( hardware.HasSourceRadius() )
	{
		alSourcef( openalSource, AL_SOURCE_RADIUS, 0.0f );
	}
#endif
	appliedSourceRadius = 0.0f;
#if defined( AL_DIRECT_CHANNELS_SOFT )
	// BFG and retail Quake 4 played stereo sounds (music, the menus) straight to the
	// left and right outputs; OpenAL would otherwise render them as virtual speakers,
	// through HRTF on headphones.
	if( hardware.HasDirectChannels() )
	{
		alSourcei( openalSource, AL_DIRECT_CHANNELS_SOFT, ( numChannels > 1 && s_directStereo.GetBool() ) ? hardware.GetDirectChannelsMode() : AL_FALSE );
	}
#endif
#if defined( AL_SOURCE_RESAMPLER_SOFT )
	if( hardware.GetSourceResampler() >= 0 )
	{
		alSourcei( openalSource, AL_SOURCE_RESAMPLER_SOFT, hardware.GetSourceResampler() );
	}
#endif
#if OPENQ4_OPENAL_EFX_SUPPORTED
	if( soundSystemLocal.hardware.HasEFXFilters() && openQ4_LoadVoiceEfxProcs() )
	{
		alSourcei( openalSource, AL_DIRECT_FILTER, AL_FILTER_NULL );
		DetachReverbSends();
	}
#endif
	CheckALErrors();
}

/*
========================
idSoundVoice_OpenAL::ApplySourceRadius

OpenAL Soft spreads a source over the arc it subtends, and over the whole sphere
once the listener is inside it. With half of minDistance as the radius the
directional part of the panning follows BFG's linear blend to omni within about
a tenth up to minDistance, and settles to a point source beyond it.
========================
*/
void idSoundVoice_OpenAL::ApplySourceRadius()
{
#if defined( AL_SOURCE_RADIUS )
	if( !soundSystemLocal.hardware.HasSourceRadius() || !alIsSource( openalSource ) )
	{
		return;
	}
	float scale = s_sourceRadiusScale.GetFloat();
	scale = FLOAT_IS_NAN( scale ) ? 0.0f : idMath::ClampFloat( 0.0f, 1.0f, scale );
	float radius = innerRadius * scale;
	// Global, omnidirectional and listener-owned sounds sit at the listener. With no
	// radius OpenAL Soft renders that as a point straight ahead, where retail and BFG
	// played them equally from every speaker; any radius makes them surround.
	if( position.LengthSqr() < 0.0001f )
	{
		radius = Max( radius, 1.0f );
	}
	if( radius == appliedSourceRadius )
	{
		return;
	}
	alSourcef( openalSource, AL_SOURCE_RADIUS, radius );
	appliedSourceRadius = radius;
	CheckALErrors();
#endif
}

/*
========================
idSoundVoice_OpenAL::DetachReverbSends
========================
*/
void idSoundVoice_OpenAL::DetachReverbSends()
{
#if OPENQ4_OPENAL_EFX_SUPPORTED
	if( !alIsSource( openalSource ) || !soundSystemLocal.hardware.HasEFXFilters() )
	{
		return;
	}
	const int sends = soundSystemLocal.hardware.GetMaxAuxiliarySends();
	for( int send = 0; send < sends && send < 2; send++ )
	{
		alSource3i( openalSource, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, send, AL_FILTER_NULL );
	}
	CheckALErrors();
#endif
}

/*
========================
idSoundVoice_OpenAL::CreateWetDryFilters
========================
*/
void idSoundVoice_OpenAL::CreateWetDryFilters()
{
#if OPENQ4_OPENAL_EFX_SUPPORTED
	if( !soundSystemLocal.hardware.HasEFXFilters() || !openQ4_LoadVoiceEfxProcs() || !alIsSource( openalSource ) )
	{
		return;
	}
	if( openalDirectFilter == 0 )
	{
		qalGenFilters( 1, &openalDirectFilter );
		if( CheckALErrors() == AL_NO_ERROR && openalDirectFilter != 0 )
		{
			qalFilteri( openalDirectFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
			qalFilterf( openalDirectFilter, AL_LOWPASS_GAIN, 1.0f );
			qalFilterf( openalDirectFilter, AL_LOWPASS_GAINHF, 1.0f );
			CheckALErrors();
		}
		else
		{
			openalDirectFilter = 0;
		}
	}
	if( openalAuxFilter == 0 )
	{
		qalGenFilters( 1, &openalAuxFilter );
		if( CheckALErrors() == AL_NO_ERROR && openalAuxFilter != 0 )
		{
			qalFilteri( openalAuxFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
			qalFilterf( openalAuxFilter, AL_LOWPASS_GAIN, 1.0f );
			qalFilterf( openalAuxFilter, AL_LOWPASS_GAINHF, 1.0f );
			CheckALErrors();
		}
		else
		{
			openalAuxFilter = 0;
		}
	}
	if( openalAreaAuxFilter == 0 && soundSystemLocal.hardware.GetMaxAuxiliarySends() >= 2 )
	{
		qalGenFilters( 1, &openalAreaAuxFilter );
		if( CheckALErrors() == AL_NO_ERROR && openalAreaAuxFilter != 0 )
		{
			qalFilteri( openalAreaAuxFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
			qalFilterf( openalAreaAuxFilter, AL_LOWPASS_GAIN, 1.0f );
			qalFilterf( openalAreaAuxFilter, AL_LOWPASS_GAINHF, 1.0f );
			CheckALErrors();
		}
		else
		{
			openalAreaAuxFilter = 0;
		}
	}
#endif
}

/*
========================
idSoundVoice_OpenAL::DestroyWetDryFilters
========================
*/
void idSoundVoice_OpenAL::DestroyWetDryFilters()
{
#if OPENQ4_OPENAL_EFX_SUPPORTED
	if( openalDirectFilter != 0 )
	{
		if( qalDeleteFilters != NULL )
		{
			qalDeleteFilters( 1, &openalDirectFilter );
		}
		else if( soundSettingsDeleteFilters != NULL )
		{
			soundSettingsDeleteFilters( 1, &openalDirectFilter );
		}
		openalDirectFilter = 0;
	}
	if( openalAuxFilter != 0 )
	{
		if( qalDeleteFilters != NULL )
		{
			qalDeleteFilters( 1, &openalAuxFilter );
		}
		else if( soundSettingsDeleteFilters != NULL )
		{
			soundSettingsDeleteFilters( 1, &openalAuxFilter );
		}
		openalAuxFilter = 0;
	}
	if( openalAreaAuxFilter != 0 )
	{
		// only the normal routing path allocates this filter, through qalGenFilters
		if( qalDeleteFilters != NULL )
		{
			qalDeleteFilters( 1, &openalAreaAuxFilter );
		}
		openalAreaAuxFilter = 0;
	}
	soundSettingsDeleteFilters = NULL;
#endif
}

/*
========================
idSoundVoice_OpenAL::ApplyWetDryRouting
========================
*/
void idSoundVoice_OpenAL::ApplyWetDryRouting()
{
	if( !alIsSource( openalSource ) )
	{
		return;
	}

	float effectiveDry = OpenQ4_SanitizeUnitValue( dryLevel );
	float effectiveWet = OpenQ4_SanitizeUnitValue( wetLevel );

	switch( s_openALEfxDebugMode.GetInteger() )
	{
		case 1:
			effectiveDry = 0.0f;
			effectiveWet = 1.0f;
			break;
		case 2:
			effectiveDry = 1.0f;
			effectiveWet = 0.0f;
			break;
		default:
			break;
	}

	const float effectiveGain = OpenQ4_SanitizeSourceGain( gain );
	const openQ4OcclusionFilter_t occlusionFilter = OpenQ4_BuildOcclusionFilter( occlusion, environmentMuffle );
	// Every stock reverb preset sets an air absorption of -5 mB per metre, a high-frequency
	// loss over distance. OpenAL applies it from the distance it attenuates by, and openQ4
	// attenuates itself (AL_ROLLOFF_FACTOR 0), so apply it here: -1.3 dB at 5 kHz across
	// 1000 units, -3.8 dB across 3000. The position is the spatialized one, so a sound heard
	// round a corner travels the portal path.
	float airGainHF = 1.0f;
	if( s_airAbsorption.GetBool() )
	{
		const float meters = position.Length() * 0.0254f;
		const float perMeter = soundSystemLocal.hardware.GetAirAbsorptionGainHF();
		if( perMeter < 1.0f && meters > 0.0f && !FLOAT_IS_NAN( meters ) )
		{
			airGainHF = idMath::ClampFloat( 0.0f, 1.0f, idMath::Pow( perMeter, meters ) );
		}
	}
	// A mod reverb's room rolloff: OpenAL attenuates a mono voice's sends with distance
	// itself, but does not spatialize the direct 5.1 channels of a centre-routed voice,
	// so that voice kept its full reverb however far away it was.
	float primaryRolloffGain = 1.0f;
	float areaRolloffGain = 1.0f;
	if( centerRouting )
	{
		const idSoundHardware_OpenAL& hardware = soundSystemLocal.hardware;
		const float reference = Max( innerRadius, 0.001f );
		primaryRolloffGain = SoundReverb_RoomRolloffGain( position.Length(), reference, hardware.GetReverbSlotRoomRolloff( hardware.GetPrimaryReverbSlot() ) );
		areaRolloffGain = SoundReverb_RoomRolloffGain( position.Length(), reference, hardware.GetReverbSlotRoomRolloff( reverbAreaSlot ) );
	}
	// openQ4's liquid and suit muffling, then retail's EAX source occlusion and room level
	const float directFilterGain = effectiveDry * occlusionFilter.directGain * eaxDirectGain;
	const float directFilterGainHF = occlusionFilter.directGainHF * eaxDirectGainHF * airGainHF;
	const float wetFilterGain = effectiveWet * occlusionFilter.wetGain * eaxPrimaryGain * primaryRolloffGain;
	const float wetFilterGainHF = occlusionFilter.wetGainHF * eaxPrimaryGainHF * airGainHF;
	const float areaFilterGain = effectiveWet * occlusionFilter.wetGain * eaxAreaGain * areaRolloffGain;
	const float areaFilterGainHF = occlusionFilter.wetGainHF * eaxAreaGainHF * airGainHF;

#if OPENQ4_OPENAL_EFX_SUPPORTED
	const bool hasEfxFilters = soundSystemLocal.hardware.HasEFXFilters() && openQ4_LoadVoiceEfxProcs();
	if( hasEfxFilters )
	{
		CreateWetDryFilters();
	}

	if( hasEfxFilters && openalDirectFilter != 0 )
	{
		qalFilterf( openalDirectFilter, AL_LOWPASS_GAIN, directFilterGain );
		qalFilterf( openalDirectFilter, AL_LOWPASS_GAINHF, directFilterGainHF );
		alSourcei( openalSource, AL_DIRECT_FILTER, openalDirectFilter );
		alSourcef( openalSource, AL_GAIN, effectiveGain );
	}
	else
	{
		alSourcef( openalSource, AL_GAIN, effectiveGain * directFilterGain );
	}

	// send 0 feeds the reverb slot holding the listener's area, send 1 the slot
	// holding the sound's own area when that is a different one
	const ALuint primarySlot = soundSystemLocal.hardware.HasEFX() ? soundSystemLocal.hardware.GetPrimaryAuxEffectSlot() : 0;
	if( hasEfxFilters && openalAuxFilter != 0 && primarySlot != 0 && wetFilterGain > 0.0f )
	{
		qalFilterf( openalAuxFilter, AL_LOWPASS_GAIN, wetFilterGain );
		qalFilterf( openalAuxFilter, AL_LOWPASS_GAINHF, wetFilterGainHF );
		alSource3i( openalSource, AL_AUXILIARY_SEND_FILTER, primarySlot, 0, openalAuxFilter );
	}
	else if( hasEfxFilters )
	{
		alSource3i( openalSource, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
	}

	if( hasEfxFilters && soundSystemLocal.hardware.GetMaxAuxiliarySends() >= 2 )
	{
		ALuint areaSlot = 0;
		if( primarySlot != 0 && reverbAreaSlot >= 0 && reverbAreaSlot != soundSystemLocal.hardware.GetPrimaryReverbSlot() )
		{
			areaSlot = soundSystemLocal.hardware.GetReverbSlotHandle( reverbAreaSlot );
		}
		if( openalAreaAuxFilter != 0 && areaSlot != 0 && areaSlot != primarySlot && areaFilterGain > 0.0f )
		{
			qalFilterf( openalAreaAuxFilter, AL_LOWPASS_GAIN, areaFilterGain );
			qalFilterf( openalAreaAuxFilter, AL_LOWPASS_GAINHF, areaFilterGainHF );
			alSource3i( openalSource, AL_AUXILIARY_SEND_FILTER, areaSlot, 1, openalAreaAuxFilter );
		}
		else
		{
			alSource3i( openalSource, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 1, AL_FILTER_NULL );
		}
	}
#else
	alSourcef( openalSource, AL_GAIN, effectiveGain * directFilterGain );
#endif
	CheckALErrors();
}

/*
========================
idSoundVoice_OpenAL::OnBufferStart
========================
*/
// Kept separate from the legacy best-effort path: no consumed warning/fallback
// can turn a failed filter/source operation into a successful settings receipt.
bool idSoundVoice_OpenAL::ApplyWetDryRoutingChecked(bool filters, bool wet, ALuint slot, SoundSettingsSourceReceipt& out)
{
	const ALuint source=openalSource;
	const auto lifetime=soundSettingsSourceGeneration;
	auto sameSource=[&] {return source==openalSource && lifetime==soundSettingsSourceGeneration;};
	auto current=[&] {return SoundSettings_NativeOperationCurrent() && sameSource();};
	auto call=[&](auto&& f) {if(!current())return false;const bool result=f();return current() && result;};
#define SOUND_VOICE_TEST(expr) do {if (!call([&] {return bool(expr);})) return false;} while(false)
#define SOUND_VOICE_CALL(expr) SOUND_VOICE_TEST(((expr),true))
	if (!source || !lifetime) return false;
	SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR); SOUND_VOICE_TEST(alIsSource(source));
	float dry=OpenQ4_SanitizeUnitValue(dryLevel), send=OpenQ4_SanitizeUnitValue(wetLevel);
	if (s_openALEfxDebugMode.GetInteger()==1) {dry=0.0f;send=1.0f;}
	if (s_openALEfxDebugMode.GetInteger()==2) {dry=1.0f;send=0.0f;}
	const auto occluded=OpenQ4_BuildOcclusionFilter(occlusion,environmentMuffle);
	// Same direct path and primary send as ApplyWetDryRouting; send 1 stays with the normal path,
	// and so does air absorption, which the next normal update applies.
	const float direct=dry*occluded.directGain*eaxDirectGain, auxiliary=send*occluded.wetGain*eaxPrimaryGain;
	const float directHF=occluded.directGainHF*eaxDirectGainHF, auxiliaryHF=occluded.wetGainHF*eaxPrimaryGainHF;
	const float effectiveGain=OpenQ4_SanitizeSourceGain(gain);
	SoundSettingsSourceReceipt receipt;receipt.source=source;receipt.lifetime=lifetime;
#if OPENQ4_OPENAL_EFX_SUPPORTED
	if (filters) {
		LPALGENFILTERS gen=nullptr;LPALDELETEFILTERS del=nullptr;LPALFILTERI filteri=nullptr;LPALFILTERF filterf=nullptr;
		SOUND_VOICE_CALL(gen=reinterpret_cast<LPALGENFILTERS>(alGetProcAddress("alGenFilters")));
		SOUND_VOICE_CALL(del=reinterpret_cast<LPALDELETEFILTERS>(alGetProcAddress("alDeleteFilters")));
		SOUND_VOICE_CALL(filteri=reinterpret_cast<LPALFILTERI>(alGetProcAddress("alFilteri")));
		SOUND_VOICE_CALL(filterf=reinterpret_cast<LPALFILTERF>(alGetProcAddress("alFilterf")));
		if (!gen || !del || !filteri || !filterf || (wet && !slot)) return false;
		SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
		if (!openalDirectFilter) {
			ALuint allocated=0;const bool result=call([&] {gen(1,&allocated);return true;});
			if (sameSource()) {openalDirectFilter=allocated;soundSettingsDeleteFilters=del;}
			if (!result) return false;
			SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
			if (!allocated) return false;
		}
		if (!openalAuxFilter) {
			ALuint allocated=0;const bool result=call([&] {gen(1,&allocated);return true;});
			if (sameSource()) {openalAuxFilter=allocated;soundSettingsDeleteFilters=del;}
			if (!result) return false;
			SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
			if (!allocated) return false;
		}
		SOUND_VOICE_CALL(filteri(openalDirectFilter,AL_FILTER_TYPE,AL_FILTER_LOWPASS));SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
		SOUND_VOICE_CALL(filterf(openalDirectFilter,AL_LOWPASS_GAIN,direct));SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
		SOUND_VOICE_CALL(filterf(openalDirectFilter,AL_LOWPASS_GAINHF,directHF));SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
		SOUND_VOICE_CALL(alSourcei(source,AL_DIRECT_FILTER,openalDirectFilter));SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
		SOUND_VOICE_CALL(alSourcef(source,AL_GAIN,effectiveGain));SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
		const bool route=wet && auxiliary>0.0f;
		if (route) {
			SOUND_VOICE_CALL(filteri(openalAuxFilter,AL_FILTER_TYPE,AL_FILTER_LOWPASS));SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
			SOUND_VOICE_CALL(filterf(openalAuxFilter,AL_LOWPASS_GAIN,auxiliary));SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
			SOUND_VOICE_CALL(filterf(openalAuxFilter,AL_LOWPASS_GAINHF,auxiliaryHF));SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
		}
		SOUND_VOICE_CALL(alSource3i(source,AL_AUXILIARY_SEND_FILTER,route?slot:AL_EFFECTSLOT_NULL,0,route?openalAuxFilter:AL_FILTER_NULL));SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
		receipt.directFilter=openalDirectFilter;receipt.auxiliaryFilter=route?openalAuxFilter:0;receipt.slot=route?slot:0;
	} else
#endif
	{
		if (filters || wet || slot) return false;
		SOUND_VOICE_CALL(alSourcef(source,AL_GAIN,effectiveGain*direct));SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
	}
	SOUND_VOICE_TEST(alIsSource(source));SOUND_VOICE_TEST(alGetError()==AL_NO_ERROR);
	out=receipt;return true;
#undef SOUND_VOICE_TEST
#undef SOUND_VOICE_CALL
}

void idSoundVoice_OpenAL::OnBufferStart( idSoundSample_OpenAL* sample, int bufferNumber )
{
	//SetSampleRate( sample->SampleRate(), XAUDIO2_COMMIT_NOW );

	if( sample == NULL || bufferNumber < 0 || bufferNumber >= sample->buffers.Num() )
	{
		ResetQueuedBufferState();
		return;
	}

	nextQueuedSample = sample;
	nextQueuedBuffer = bufferNumber;
	nextQueuedOffset = 0;
	AdvanceQueuedBuffer();
	Update();
}
