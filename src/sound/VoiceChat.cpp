// Copyright (C) 2026 openQ4 contributors. SPDX-License-Identifier: GPL-3.0-or-later
#include "snd_local.h"
#include "VoiceChat.h"
#include "VoicePacket.h"
extern idCVar s_volume;
extern idCVar s_noSound;

#if defined(USE_SDL3) && !defined(ID_DEDICATED)
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#endif

// Separate consent from the legacy Send setting: an existing archived Send=1
// must never silently opt an upgraded installation into microphone capture.
idCVar s_voiceChatCapture( "s_voiceChatCapture", "0", CVAR_SOUND | CVAR_ARCHIVE | CVAR_BOOL, "allow microphone capture while push-to-talk or the local microphone test is active" );
idCVar s_voiceChatTest( "s_voiceChatTest", "0", CVAR_SOUND | CVAR_BOOL | CVAR_NOCHEAT, "local microphone test; never sent over the network" );
idCVar s_voiceChatStatus( "s_voiceChatStatus", "0", CVAR_SOUND | CVAR_INTEGER | CVAR_ROM, "voice status: 0 unavailable, 1 capture off, 2 ready, 3 recording, 4 input unavailable" );
idCVar s_voiceChatLevel( "s_voiceChatLevel", "0", CVAR_SOUND | CVAR_FLOAT | CVAR_ROM, "current microphone peak (0-1)" );
// Test only: stands in for a machine whose clock runs fast or slow, so receivers'
// drift tracking can be qualified between real peers on one host.
idCVar s_voiceTestClockRate( "s_voiceTestClockRate", "1", CVAR_SOUND | CVAR_FLOAT | CVAR_CHEAT,
	"test only: rate at which this sender's voice timestamps advance against real time", 0.98f, 1.02f );

namespace {
constexpr int VoiceClients = 32;
constexpr int LocalTestClient = VoiceClients;
constexpr int PlaybackChannels = 3;
bool sessionActive = false, recording = false, microphoneTest = false;
uint32_t lastUpdate = 0, sequence = 0, retryAt = 0;
uint32_t syntheticEnd = 0, syntheticNext = 0;
float microphoneLevel = 0;
uint32_t captured = 0, played = 0, rejected = 0, playbackDrops = 0, captureOpens = 0;
oq4voice::CaptureQueue captureQueue;
oq4voice::Frame partial;
int partialSamples = 0;
oq4voice::Admission incoming[VoiceClients + 1];
oq4voice::AdmissionStats incomingStats[VoiceClients + 1];
unsigned int generations[VoiceClients + 1] = {};
double stampClock = 0;
uint32_t stampSource = 0;
bool stampSkewed = false;

uint32_t VoiceNow() {
#if defined(USE_SDL3) && !defined(ID_DEDICATED)
	return uint32_t( SDL_GetTicks() );
#else
	return uint32_t( Sys_Milliseconds() );
#endif
}
// The timestamp a frame is sent with. At rate 1 it is the frame's own time; under
// s_voiceTestClockRate it advances at that rate from the last stamp, with the
// fraction carried, so a 20 ms frame is never truncated and a change never jumps.
uint32_t VoiceStamp( uint32_t time ) {
	const double rate = idMath::ClampFloat( 0.98f, 1.02f, s_voiceTestClockRate.GetFloat() );
	if ( rate == 1.0 && !stampSkewed ) {
		stampSource = time;
		stampClock = time;
		return time;
	}
	stampClock += double( int32_t( time - stampSource ) ) * rate;
	stampSource = time;
	stampSkewed = rate != 1.0;
	return uint32_t( int64_t( stampClock ) );
}

bool SyntheticActive( uint32_t now ) {
	return syntheticEnd != 0 && syntheticEnd - now < 0x80000000u;
}

#if defined(USE_SDL3) && !defined(ID_DEDICATED)
SDL_AudioStream* captureStream = NULL;
bool ownsAudioSubsystem = false;
bool captureErrorReported = false;
uint32_t lastCapture = 0;

void CaptureError( const char* stage ) {
	if ( !captureErrorReported ) common->Printf( "VOICE_CAPTURE_ERROR %s: %s\n", stage, SDL_GetError() );
	captureErrorReported = true;
}

void SDLCALL Capture( void*, SDL_AudioStream* stream, int, int ) {
	// SDL holds this stream's lock. Drain immediately into our fixed ring even
	// while the game thread is blocked; SDL's input queue cannot grow with it.
	int available = SDL_GetAudioStreamAvailable( stream );
	if ( available > oq4voice::Samples * 2 * oq4voice::MaxQueuedFrames ) {
		SDL_ClearAudioStream( stream );
		partialSamples = 0;
		++captureQueue.dropped;
		return;
	}
	while ( available >= 2 ) {
		const uint32_t now = VoiceNow();
		// A partially assembled frame must not carry old speech across a callback
		// stall and then acquire a fresh timestamp when its final samples arrive.
		if ( partialSamples && now - partial.time > oq4voice::MaxAgeMsec ) {
			partialSamples = 0;
			++captureQueue.dropped;
		}
		if ( !partialSamples ) partial.time = now;
		const int bytes = (std::min)( available, ( oq4voice::Samples - partialSamples ) * 2 );
		const int got = SDL_GetAudioStreamData( stream, partial.pcm + partialSamples, bytes );
		if ( got <= 0 ) break;
		lastCapture = VoiceNow();
		partialSamples += got / 2;
		if ( partialSamples == oq4voice::Samples ) {
			partial.time = lastCapture;
			captureQueue.Push( partial );
			partialSamples = 0;
		}
		available = SDL_GetAudioStreamAvailable( stream );
	}
}

void CloseCapture() {
	if ( captureStream ) {
		// Destroy waits for the callback before any shared capture state is reset.
		SDL_DestroyAudioStream( captureStream );
		captureStream = NULL;
	}
	captureQueue = {};
	partialSamples = 0;
	recording = false;
	microphoneLevel = 0;
	s_voiceChatLevel.SetFloat( 0 );
}

bool OpenCapture( uint32_t now ) {
	if ( captureStream ) return true;
	if ( retryAt && now - retryAt < 1000 ) return false;
	retryAt = now;
	if ( !ownsAudioSubsystem ) {
		if ( !SDL_InitSubSystem( SDL_INIT_AUDIO ) ) { CaptureError( "initialize" ); return false; }
		ownsAudioSubsystem = true;
	}
	SDL_AudioSpec format = { SDL_AUDIO_S16, 1, oq4voice::Rate };
	captureStream = SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &format, NULL, NULL );
	if ( !captureStream ) { CaptureError( "open default input" ); return false; }
	if ( !SDL_SetAudioStreamPutCallback( captureStream, Capture, NULL ) ) { CaptureError( "callback" ); CloseCapture(); return false; }
	lastCapture = now;
	if ( !SDL_ResumeAudioStreamDevice( captureStream ) ) { CaptureError( "resume" ); CloseCapture(); return false; }
	retryAt = 0;
	captureErrorReported = false;
	++captureOpens;
	return true;
}
#else
void CloseCapture() { recording = false; microphoneLevel = 0; }
#endif

#if defined(USE_OPENAL) && !defined(ID_DEDICATED)
struct Playback {
	ALuint source = 0, buffers[oq4voice::MaxQueuedFrames] = {};
	bool used[oq4voice::MaxQueuedFrames] = {};
	int client = -1;
	unsigned int generation = 0;
	uint32_t lastFrame = 0;
} playback[PlaybackChannels];

void Flush( Playback& channel ) {
	if ( channel.source ) {
		alSourceStop( channel.source );
		alSourcei( channel.source, AL_BUFFER, 0 );
	}
	for ( bool& used : channel.used ) used = false;
	channel.client = -1;
}

void Reclaim( Playback& channel ) {
	ALint processed = 0;
	alGetSourcei( channel.source, AL_BUFFERS_PROCESSED, &processed );
	while ( processed-- > 0 ) {
		ALuint buffer = 0;
		alSourceUnqueueBuffers( channel.source, 1, &buffer );
		for ( int i = 0; i < oq4voice::MaxQueuedFrames; ++i ) if ( channel.buffers[i] == buffer ) channel.used[i] = false;
	}
}

float ReceiveGain() {
	const float gain = cvarSystem->GetCVarFloat( "s_voiceVolume" ) * s_volume.GetFloat();
	return FLOAT_IS_NAN( gain ) ? 0.0f : idMath::ClampFloat( 0.0f, 1.0f, gain );
}

void Play( int client, unsigned int generation, const int16_t* pcm ) {
	if ( !soundSystemLocal.hardware.GetOpenALContext() ) return;
	// A lost endpoint plays nothing: drop the frame rather than queue speech that
	// would be late when it returns. A stopped source restarts with the next frame.
	if ( !soundSystemLocal.hardware.IsOutputConnected() ) { ++playbackDrops; return; }
	Playback* target = NULL;
	for ( Playback& channel : playback ) if ( channel.client == client && channel.generation == generation ) target = &channel;
	if ( !target ) {
		for ( Playback& channel : playback ) {
			if ( channel.client < 0 || VoiceNow() - channel.lastFrame > 200 ) { target = &channel; Flush( channel ); break; }
		}
	}
	if ( !target ) { ++playbackDrops; return; }
	if ( !target->source ) {
		// Isolate allocation errors from unrelated OpenAL users, and leave no
		// partial source/buffer allocation behind if the device is exhausted.
		alGetError();
		alGenSources( 1, &target->source );
		alGenBuffers( oq4voice::MaxQueuedFrames, target->buffers );
		if ( alGetError() != AL_NO_ERROR || !target->source ) {
			if ( target->source ) alDeleteSources( 1, &target->source );
			for ( ALuint buffer : target->buffers ) if ( buffer ) alDeleteBuffers( 1, &buffer );
			*target = {};
			++playbackDrops;
			return;
		}
		alSourcei( target->source, AL_SOURCE_RELATIVE, AL_TRUE );
		alSource3f( target->source, AL_POSITION, 0, 0, 0 );
		alSourcef( target->source, AL_ROLLOFF_FACTOR, 0 );
	}
	Reclaim( *target );
	int freeBuffer = -1;
	for ( int i = 0; i < oq4voice::MaxQueuedFrames; ++i ) if ( !target->used[i] ) { freeBuffer = i; break; }
	if ( freeBuffer < 0 ) {
		// Recover toward live speech, never enlarge the queue or stretch playback.
		Flush( *target );
		freeBuffer = 0;
		++playbackDrops;
	}
	alBufferData( target->buffers[freeBuffer], AL_FORMAT_MONO16, pcm, oq4voice::Samples * 2, oq4voice::Rate );
	alSourceQueueBuffers( target->source, 1, &target->buffers[freeBuffer] );
	target->used[freeBuffer] = true;
	target->client = client;
	target->generation = generation;
	target->lastFrame = VoiceNow();
	alSourcef( target->source, AL_GAIN, ReceiveGain() );
	ALint state = 0;
	alGetSourcei( target->source, AL_SOURCE_STATE, &state );
	if ( state != AL_PLAYING ) alSourcePlay( target->source );
	if ( alGetError() == AL_NO_ERROR ) ++played;
	else { Flush( *target ); ++playbackDrops; }
}
#endif

void ListVoice( const idCmdArgs& ) {
	common->Printf( "VOICE_STATUS active=%d recording=%d status=%d capture_opens=%u captured=%u played=%u rejected=%u playback_drops=%u max_queue_ms=%d\n",
		sessionActive, recording, s_voiceChatStatus.GetInteger(), captureOpens, captured, played, rejected, playbackDrops,
		oq4voice::FrameMsec * oq4voice::MaxQueuedFrames );
	// The most recently encoded frame's source clock and packet timestamp. Unlike
	// a receiver's latency baseline, their difference contains no network jitter.
	common->Printf( "VOICE_CLOCK source_ms=%u stamp_ms=%u\n", stampSource, uint32_t( int64_t( stampClock ) ) );
#if defined(USE_SDL3) && !defined(ID_DEDICATED)
	if ( ownsAudioSubsystem ) common->Printf( "VOICE_CAPTURE_DRIVER %s\n", SDL_GetCurrentAudioDriver() );
#endif
#if defined(USE_OPENAL) && !defined(ID_DEDICATED)
	for ( const Playback& channel : playback ) if ( channel.source && channel.client >= 0 ) {
		ALint state = 0, queued = 0;
		ALfloat gain = 0, pitch = 0;
		alGetSourcei( channel.source, AL_SOURCE_STATE, &state );
		alGetSourcei( channel.source, AL_BUFFERS_QUEUED, &queued );
		alGetSourcef( channel.source, AL_GAIN, &gain );
		alGetSourcef( channel.source, AL_PITCH, &pitch );
		common->Printf( "VOICE_PLAYBACK client=%d generation=%u playing=%d queued=%d gain=%.4f pitch=%.4f\n", channel.client, channel.generation, state == AL_PLAYING, queued, gain, pitch );
	}
#endif
	// Per-sender admission for the current connection generation. The
	// interval maximum covers the time since the previous report.
	for ( int client = 0; client <= VoiceClients; ++client ) {
		oq4voice::AdmissionStats& stats = incomingStats[client];
		if ( !stats.accepted && !stats.rejected ) continue;
		common->Printf( "VOICE_ADMISSION client=%d generation=%u accepted=%u rejected=%u added_ms=%u max_added_ms=%u interval_max_added_ms=%u baseline_shift_ms=%d\n",
			client, generations[client], stats.accepted, stats.rejected, stats.lastAdded, stats.maxAdded, stats.intervalMaxAdded,
			int( stats.BaselineShift( incoming[client] ) ) );
		stats.intervalMaxAdded = 0;
	}
}

void TestTone( const idCmdArgs& args ) {
	const int duration = args.Argc() > 1 ? idMath::ClampInt( 0, 10, atoi( args.Argv( 1 ) ) ) : 2;
	syntheticNext = VoiceNow();
	syntheticEnd = duration ? syntheticNext + duration * 1000 : 0;
	common->Printf( "VOICE_SYNTHETIC seconds=%d microphone=off\n", duration );
}
}

void SoundVoiceChat_InitCommands() {
	cmdSystem->AddCommand( "listVoiceChat", ListVoice, CMD_FL_SOUND, "report bounded voice capture/playback state" );
	cmdSystem->AddCommand( "testVoiceChat", TestTone, CMD_FL_SOUND | CMD_FL_CHEAT, "send up to ten seconds of synthetic voice; never opens a microphone" );
}

void SoundVoiceChat_OutputInterrupted() {
#if defined(USE_OPENAL) && !defined(ID_DEDICATED)
	for ( Playback& channel : playback ) if ( channel.source ) alSourceStop( channel.source );
#endif
}

void SoundVoiceChat_Shutdown() {
	CloseCapture();
#if defined(USE_SDL3) && !defined(ID_DEDICATED)
	if ( ownsAudioSubsystem ) { SDL_QuitSubSystem( SDL_INIT_AUDIO ); ownsAudioSubsystem = false; }
	captureErrorReported = false;
#endif
#if defined(USE_OPENAL) && !defined(ID_DEDICATED)
	for ( Playback& channel : playback ) {
		Flush( channel );
		if ( channel.source ) alDeleteSources( 1, &channel.source );
		for ( ALuint buffer : channel.buffers ) if ( buffer ) alDeleteBuffers( 1, &buffer );
		channel = {};
	}
#endif
	sessionActive = false;
	microphoneTest = false;
	syntheticEnd = 0;
	for ( auto& admission : incoming ) admission = {};
	for ( auto& stats : incomingStats ) stats = {};
	for ( auto& generation : generations ) generation = 0;
	s_voiceChatTest.SetBool( false );
	s_voiceChatStatus.SetInteger( 0 );
}

void idSoundSystemLocal::ResetVoiceChat() { SoundVoiceChat_Shutdown(); }

bool idSoundSystemLocal::UpdateVoiceChat( bool active, bool transmit, bool localTest ) {
	const uint32_t now = VoiceNow();
	lastUpdate = now;
	sessionActive = active && !s_noSound.GetBool();
	const bool wasMicrophoneTest = microphoneTest;
	microphoneTest = sessionActive && localTest && s_voiceChatCapture.GetBool();
	if ( microphoneTest != wasMicrophoneTest ) {
		// A test frame must never become network speech when PTT is already down
		// on test exit. Closing joins the callback and discards SDL's resampler,
		// queued frames and the partial frame before opening the new capture mode.
		CloseCapture();
		StopVoiceData( LocalTestClient );
	}
	if ( localTest && !s_voiceChatCapture.GetBool() ) s_voiceChatTest.SetBool( false );
	if ( !sessionActive ) { SoundVoiceChat_Shutdown(); return false; }
	const bool synthetic = SyntheticActive( now );
	const bool capture = !synthetic && !IsMuted() && s_voiceChatCapture.GetBool() &&
		( ( transmit && cvarSystem->GetCVarBool( "s_voiceChatSend" ) ) || microphoneTest );
#if defined(USE_SDL3) && !defined(ID_DEDICATED)
	if ( capture ) {
		if ( captureStream ) {
			SDL_LockAudioStream( captureStream );
			const bool stalled = now - lastCapture > 2000;
			SDL_UnlockAudioStream( captureStream );
			if ( stalled ) CloseCapture();
		}
		recording = OpenCapture( now );
	} else CloseCapture();
	s_voiceChatStatus.SetInteger( capture ? ( recording ? 3 : 4 ) : ( s_voiceChatCapture.GetBool() ? 2 : 1 ) );
#else
	(void)capture;
	s_voiceChatStatus.SetInteger( 0 );
#endif
	// A microphone test is exclusively local, even with echo enabled.
	if ( microphoneTest ) {
		byte packet[oq4voice::PacketBytes];
		for ( int i = 0; i < 3; ++i ) {
			const int bytes = GetVoiceData( packet, sizeof( packet ) );
			if ( !bytes ) break;
			PlayVoiceData( LocalTestClient, 1, packet, bytes );
		}
	}
	return !microphoneTest && ( recording || synthetic );
}

int idSoundSystemLocal::GetVoiceData( byte* data, int capacity ) {
	if ( !sessionActive || !data || capacity < oq4voice::PacketBytes ) return 0;
	const uint32_t now = VoiceNow();
	oq4voice::Frame frame;
	if ( SyntheticActive( now ) ) {
		if ( now - syntheticNext >= 0x80000000u ) return 0;
		// Keep a 50 Hz cadence at arbitrary render rates; discard a long stall.
		if ( now - syntheticNext > 3 * oq4voice::FrameMsec ) syntheticNext = now;
		frame.time = syntheticNext;
		syntheticNext += oq4voice::FrameMsec;
		for ( int i = 0; i < oq4voice::Samples; ++i ) frame.pcm[i] = int16_t( 8000.0f * sinf( float( i ) * 6.283185307f * 400.0f / oq4voice::Rate ) );
	} else {
#if defined(USE_SDL3) && !defined(ID_DEDICATED)
		if ( !captureStream || !recording ) return 0;
		SDL_LockAudioStream( captureStream );
		const bool haveFrame = captureQueue.Pop( frame, now );
		SDL_UnlockAudioStream( captureStream );
		if ( !haveFrame ) return 0;
#else
		return 0;
#endif
		float gain = cvarSystem->GetCVarFloat( "s_micInputLevel" ) / 5.0f;
		gain = FLOAT_IS_NAN( gain ) ? 0.0f : idMath::ClampFloat( 0.0f, 2.0f, gain );
		for ( int16_t& sample : frame.pcm ) sample = int16_t( idMath::ClampFloat( -32768, 32767, sample * gain ) );
	}
	int peak = 0;
	for ( int16_t sample : frame.pcm ) peak = (std::max)( peak, abs( int( sample ) ) );
	microphoneLevel = peak / 32768.0f;
	s_voiceChatLevel.SetFloat( microphoneLevel );
	oq4voice::Encode( frame.pcm, ++sequence, VoiceStamp( frame.time ), data );
	++captured;
	return oq4voice::PacketBytes;
}

void idSoundSystemLocal::PlayVoiceData( int client, unsigned int generation, const byte* data, int size ) {
	if ( !sessionActive || client < 0 || client > LocalTestClient ||
		( client == LocalTestClient ? !microphoneTest : !cvarSystem->GetCVarBool( "s_voiceChatReceive" ) ) ) return;
	if ( generations[client] != generation ) {
		StopVoiceData( client );
		generations[client] = generation;
		incoming[client] = {};
		incomingStats[client] = {};
	}
	const uint32_t now = VoiceNow();
	const bool admitted = incoming[client].Accept( data, size, now );
	incomingStats[client].Record( incoming[client], data, size, now, admitted );
	if ( !admitted ) { ++rejected; return; }
	int16_t pcm[oq4voice::Samples];
	if ( !oq4voice::Decode( data, size, pcm ) ) return;
#if defined(USE_OPENAL) && !defined(ID_DEDICATED)
	Play( client, generation, pcm );
#endif
}

void idSoundSystemLocal::StopVoiceData( int client ) {
#if defined(USE_OPENAL) && !defined(ID_DEDICATED)
	for ( Playback& channel : playback ) if ( channel.client == client ) Flush( channel );
#endif
}

int idSoundSystemLocal::GetVoiceTalker() const {
#if defined(USE_OPENAL) && !defined(ID_DEDICATED)
	for ( const Playback& channel : playback ) {
		if ( channel.source && channel.client >= 0 && channel.client < VoiceClients ) {
			ALint state = 0;
			alGetSourcei( channel.source, AL_SOURCE_STATE, &state );
			if ( state == AL_PLAYING ) return channel.client;
		}
	}
#endif
	return -1;
}
float idSoundSystemLocal::GetVoiceLevel() const { return microphoneLevel; }

void SoundVoiceChat_Render() {
	if ( !sessionActive ) return;
	if ( VoiceNow() - lastUpdate > 250 || s_noSound.GetBool() ) { SoundVoiceChat_Shutdown(); return; }
#if defined(USE_OPENAL) && !defined(ID_DEDICATED)
	for ( Playback& channel : playback ) {
		if ( channel.source && channel.client >= 0 ) {
			if ( VoiceNow() - channel.lastFrame > 200 ||
				( channel.client == LocalTestClient ? !microphoneTest : !cvarSystem->GetCVarBool( "s_voiceChatReceive" ) ) ) Flush( channel );
			else alSourcef( channel.source, AL_GAIN, ReceiveGain() );
		}
	}
#endif
}
