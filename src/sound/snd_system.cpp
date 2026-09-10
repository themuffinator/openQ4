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

#include "snd_local.h"
#include "SoundSettings.h"

idCVar s_noSound( "s_noSound", "0", CVAR_BOOL, "returns NULL for all sounds loaded and does not update the sound rendering" );
idCVar s_volume( "s_volume", "0.5", CVAR_ARCHIVE | CVAR_FLOAT, "master volume (0-1)", 0.0f, 1.0f );
idCVar s_musicVolume( "s_musicVolume", "0.5", CVAR_ARCHIVE | CVAR_FLOAT, "music volume (0-1)", 0.0f, 1.0f );
idCVar s_speakerFraction( "s_speakerFraction", "0.65", CVAR_ARCHIVE | CVAR_FLOAT, "speaker attenuation fraction" );
idCVar s_radioChatterFraction( "s_radioChatterFraction", "0.5", CVAR_ARCHIVE | CVAR_FLOAT, "radio chatter attenuation fraction" );
idCVar s_frequencyShift( "s_frequencyShift", "1", CVAR_BOOL, "enable sound shader frequency shift playback" );
idCVar s_useOpenAL( "s_useOpenAL", "1", CVAR_ARCHIVE | CVAR_BOOL, "use OpenAL audio backend" );
idCVar s_deviceName( "s_deviceName", "", CVAR_ARCHIVE, "OpenAL device name override" );
idCVar s_useEAXReverb( "s_useEAXReverb", "1", CVAR_ARCHIVE | CVAR_BOOL, "use EAX reverb if available" );
idCVar s_openALHRTF( "s_openALHRTF", "0", CVAR_ARCHIVE | CVAR_INTEGER, "OpenAL Soft HRTF mode: 0 = auto, 1 = off, 2 = on", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar s_openALEfxDebugMode( "s_openALEfxDebugMode", "0", CVAR_ARCHIVE | CVAR_INTEGER, "OpenAL wet/dry debug mode (0=normal, 1=wet-only, 2=dry-only)" );
idCVar s_numberOfSpeakers( "s_numberOfSpeakers", "6", CVAR_ARCHIVE | CVAR_INTEGER, "number of speakers (2 or 6)" );
idCVar s_warnOnMissingSamples( "s_warnOnMissingSamples", "0", CVAR_ARCHIVE | CVAR_BOOL, "warn when falling back to default sound samples" );
idCVar s_controllerRumble( "s_controllerRumble", "1", CVAR_ARCHIVE | CVAR_BOOL, "sound-side controller rumble master switch; input menu uses in_joystickRumble" );
// alBufferData copies a sample's PCM into OpenAL's own storage, so holding on to
// our copy means every uploaded sample is resident twice. A loaded Quake 4 map
// carries ~218MB of PCM, so the duplicate is one of the largest single items in
// the process. Off restores the old behaviour for A/B testing an audio problem.
idCVar s_releaseSamplePayload( "s_releaseSamplePayload", "1", CVAR_ARCHIVE | CVAR_BOOL, "free the CPU copy of a sound sample once OpenAL has it" );

#ifdef ID_RETAIL
	idCVar s_useCompression( "s_useCompression", "1", CVAR_BOOL, "Use compressed sound files (mp3/xma)" );
	idCVar s_playDefaultSound( "s_playDefaultSound", "0", CVAR_BOOL, "play a beep for missing sounds" );
	idCVar s_maxSoundsPerShader( "s_maxSoundsPerShader", "0", CVAR_ARCHIVE | CVAR_INTEGER, "max samples to load per shader, 0 loads all" );
#else
	idCVar s_useCompression( "s_useCompression", "1", CVAR_BOOL, "Use compressed sound files (mp3/xma)" );
	idCVar s_playDefaultSound( "s_playDefaultSound", "1", CVAR_BOOL, "play a beep for missing sounds" );
	idCVar s_maxSoundsPerShader( "s_maxSoundsPerShader", "0", CVAR_ARCHIVE | CVAR_INTEGER, "max samples to load per shader, 0 loads all" );
#endif

idCVar preLoad_Samples( "preLoad_Samples", "1", CVAR_SYSTEM | CVAR_BOOL, "preload samples during beginlevelload" );

idSoundSystemLocal soundSystemLocal;
idSoundSystem* soundSystem = &soundSystemLocal;

static const int SOUND_RUMBLE_DURATION_MSEC = 120;
static const float SOUND_RUMBLE_STOP_THRESHOLD = 0.01f;
static const float SOUND_RUMBLE_HIGH_MOTOR_SCALE = 0.75f;

static void Sound_UpdateControllerRumble( float amplitude )
{
	if( !s_controllerRumble.GetBool() )
	{
		Sys_SetJoystickRumble( 0.0f, 0.0f, 0 );
		return;
	}

	const float strength = idMath::ClampFloat( 0.0f, 1.0f, amplitude );
	if( strength <= SOUND_RUMBLE_STOP_THRESHOLD )
	{
		Sys_SetJoystickRumble( 0.0f, 0.0f, 0 );
		return;
	}

	Sys_SetJoystickRumble( strength, strength * SOUND_RUMBLE_HIGH_MOTOR_SCALE, SOUND_RUMBLE_DURATION_MSEC );
}

/*
================================================================================================

idSoundSystemLocal

================================================================================================
*/

/*
========================
TestSound_f

This is called from the main thread.
========================
*/
void TestSound_f( const idCmdArgs& args )
{
	if( args.Argc() != 2 )
	{
		idLib::Printf( "Usage: testSound <file>\n" );
		return;
	}
	if( soundSystemLocal.currentSoundWorld )
	{
		soundSystemLocal.currentSoundWorld->PlayShaderDirectly( args.Argv( 1 ) );
	}
}

/*
========================
RestartSound_f
========================
*/
void RestartSound_f( const idCmdArgs& args )
{
	idLib::Printf( "Sound System Restart...\n" );
	soundSystemLocal.Restart();
}

/*
========================
ReloadSounds_f
========================
*/
void ReloadSounds_f( const idCmdArgs& args )
{
	soundSystemLocal.Restart();
	idLib::Printf( "sound: changed sounds reloaded\n" );
}

/*
========================
ListSounds_f

========================
*/
void ListSounds_f( const idCmdArgs& args )
{
	const char* filter = args.Argc() > 1 ? args.Argv( 1 ) : NULL;
	int totalSounds = 0;
	int totalLoaded = 0;
	int totalMemory = 0;
	int totalCompressedMemory = 0;
	int totalPCMMemory = 0;
	int totalResidentMemory = 0;

	idLib::Printf( "Sound samples\n-------------\n" );
	for( int i = 0; i < soundSystemLocal.samples.Num(); i++ )
	{
		const idSoundSample* sample = soundSystemLocal.samples[ i ];
		const char* name = sample->GetName();

		if( filter != NULL && idStr::FindText( name, filter, false ) < 0 )
		{
			continue;
		}

		const bool loaded = sample->IsLoaded();
		const bool compressed = loaded && sample->IsCompressed();
		const char* channels = sample->NumChannels() == 2 ? "ST" : "  ";
		const char* format = compressed ? "OGG" : "WAV";
		const char* state = !loaded ? "(PURGED)" : ( sample->IsDefault() ? "(DEFAULTED)" : "" );
		const int sampleRateKHz = sample->SampleRate() > 0 ? sample->SampleRate() / 1000 : 0;
		const int sampleBytes = sample->BufferSize();

		idLib::Printf( "%s %2dkHz %6dms %5dkB %4s %s%s\n",
					   channels,
					   sampleRateKHz,
					   loaded ? sample->LengthInMsec() : 0,
					   sampleBytes / 1024,
					   format,
					   name,
					   state );

		totalSounds++;
		if( loaded )
		{
			totalLoaded++;
			totalMemory += sampleBytes;
			totalResidentMemory += sample->ResidentBufferSize();
			if( compressed )
			{
				totalCompressedMemory += sampleBytes;
			}
			else
			{
				totalPCMMemory += sampleBytes;
			}
		}
	}

	idLib::Printf( "%8d total sounds\n", totalSounds );
	idLib::Printf( "%8d total samples loaded\n", totalLoaded );
	idLib::Printf( "%8d kB OGG samples loaded\n", totalCompressedMemory / 1024 );
	idLib::Printf( "%8d kB PCM samples loaded\n", totalPCMMemory / 1024 );
	idLib::Printf( "%8d kB total sample data\n", totalMemory / 1024 );
	// Sample data is not the same as memory held. OpenAL keeps its own copy of
	// everything uploaded to it, so ours is released; report what is actually
	// still resident rather than letting the decoded size stand in for it.
	idLib::Printf( "%8d kB held in CPU memory (%d kB released to OpenAL)\n",
				   totalResidentMemory / 1024, ( totalMemory - totalResidentMemory ) / 1024 );
}

/*
========================
ListSamples_f

Compatibility name retained for OpenQ4 scripts.
========================
*/
void ListSamples_f( const idCmdArgs& args )
{
	ListSounds_f( args );
}

/*
========================
ListSoundDecoders_f

OpenQ4's OpenAL path decodes through hardware voices rather than retail's persistent idSampleDecoder objects.
========================
*/
void ListSoundDecoders_f( const idCmdArgs& args )
{
	int numActiveDecoders = 0;

	for( int w = 0; w < soundSystemLocal.soundWorlds.Num(); w++ )
	{
		const idSoundWorldLocal* soundWorld = soundSystemLocal.soundWorlds[ w ];
		if( soundWorld != soundSystemLocal.currentSoundWorld )
		{
			continue;
		}

		for( int e = 0; e < soundWorld->emitters.Num(); e++ )
		{
			const idSoundEmitterLocal* emitter = soundWorld->emitters[ e ];
			if( emitter == NULL )
			{
				continue;
			}

			for( int c = 0; c < emitter->channels.Num(); c++ )
			{
				const idSoundChannel* channel = emitter->channels[ c ];
				if( channel == NULL || channel->hardwareVoice == NULL || channel->leadinSample == NULL )
				{
					continue;
				}

				const idSoundSample* sample = channel->leadinSample;
				const int elapsedMS = Max( 0, soundSystemLocal.SoundTime() - channel->startTime );
				const int durationMS = Max( 1, sample->LengthInMsec() );
				const int percent = channel->IsLooping() ? ( 100 * ( elapsedMS % durationMS ) / durationMS ) : Min( 100, 100 * elapsedMS / durationMS );
				const char* format = sample->IsCompressed() ? "OGG" : "WAV";

				float offsetMS = 0.0f;
				float latencyMS = 0.0f;
				if( channel->hardwareVoice->GetPlaybackLatencyMS( offsetMS, latencyMS ) )
				{
					idLib::Printf( "%3d decoding %3d%% %s latency %5.1fms offset %8.1fms: %s\n", numActiveDecoders, percent, format, latencyMS, offsetMS, sample->GetName() );
				}
				else
				{
					idLib::Printf( "%3d decoding %3d%% %s: %s\n", numActiveDecoders, percent, format, sample->GetName() );
				}
				numActiveDecoders++;
			}
		}
	}

	idLib::Printf( "%d decoders\n", numActiveDecoders );
	idLib::Printf( "0 waiting decoders\n" );
	idLib::Printf( "%d active decoders\n", numActiveDecoders );
	idLib::Printf( "0 kB decoder memory in 0 blocks\n" );
}

/*
========================
ListPlayingSounds_f

Lists OpenAL sources that are actually playing with non-zero effective gain.
========================
*/
void ListPlayingSounds_f( const idCmdArgs& args )
{
	(void)args;

	soundSystemLocal.hardware.ListPlayingVoices();
}

/*
========================
idSoundSystemLocal::Restart
========================
*/
void idSoundSystemLocal::Restart()
{
	if (SoundSettings_BlockAutomaticRestart()) return;
	const bool wasMuted = IsMuted();
	SetMute( true );

	// Mute all channels in all worlds
	for( int i = 0; i < soundWorlds.Num(); i++ )
	{
		idSoundWorldLocal* sw = soundWorlds[i];
		for( int e = 0; e < sw->emitters.Num(); e++ )
		{
			idSoundEmitterLocal* emitter = sw->emitters[e];
			for( int c = 0; c < emitter->channels.Num(); c++ )
			{
				emitter->channels[c]->Mute();
			}
		}
	}
	// Free sample buffers while OpenAL is still active
	for( int i = 0; i < samples.Num(); i++ )
	{
		samples[i]->FreeData();
	}
	FreeStreamBuffers();
	// Shutdown sound hardware
	hardware.Shutdown();
	// Reinitialize sound hardware
	if( !s_noSound.GetBool() )
	{
		hardware.Init();
	}

	InitStreamBuffers();

	if( !s_noSound.GetBool() )
	{
		int reloaded = 0;
		for( int i = 0; i < samples.Num(); i++ )
		{
			samples[i]->LoadResource();
			if( samples[i]->IsLoaded() )
			{
				reloaded++;
			}
		}
		idLib::Printf( "%d sound samples reloaded\n", reloaded );
	}

	SetMute( wasMuted );
}

/*
========================
idSoundSystemLocal::Init

Initialize the SoundSystem.
========================
*/
void idSoundSystemLocal::Init()
{

	idLib::Printf( "----- Initializing Sound System ------\n" );

	soundTime = Sys_Milliseconds();
	random.SetSeed( soundTime );

	if( !s_noSound.GetBool() )
	{
		hardware.Init();
		InitStreamBuffers();
	}

	cmdSystem->AddCommand( "testSound", TestSound_f, CMD_FL_SOUND, "tests a sound", idCmdSystem::ArgCompletion_SoundName );
	cmdSystem->AddCommand( "listSounds", ListSounds_f, CMD_FL_SOUND, "lists all sounds" );
	cmdSystem->AddCommand( "listPlayingSounds", ListPlayingSounds_f, CMD_FL_SOUND, "lists currently playing sounds" );
	cmdSystem->AddCommand( "listSoundDecoders", ListSoundDecoders_f, CMD_FL_SOUND, "list active sound decoders" );
	cmdSystem->AddCommand( "reloadSounds", ReloadSounds_f, CMD_FL_SOUND | CMD_FL_CHEAT, "reloads all sounds" );
	cmdSystem->AddCommand( "s_restart", RestartSound_f, CMD_FL_SOUND, "restarts the sound system" );
	cmdSystem->AddCommand( "listSamples", ListSamples_f, CMD_FL_SOUND, "lists all loaded sound samples" );

	idLib::Printf( "sound system initialized.\n" );
	idLib::Printf( "--------------------------------------\n" );
}

/*
========================
idSoundSystemLocal::InitStreamBuffers
========================
*/
void idSoundSystemLocal::InitStreamBuffers()
{
//	streamBufferMutex.Lock();
	const bool empty = ( bufferContexts.Num() == 0 );
	if( empty )
	{
		bufferContexts.SetNum( MAX_SOUND_BUFFERS );
		for( int i = 0; i < MAX_SOUND_BUFFERS; i++ )
		{
			freeStreamBufferContexts.Append( &( bufferContexts[ i ] ) );
		}
	}
	else
	{
		for( int i = 0; i < activeStreamBufferContexts.Num(); i++ )
		{
			freeStreamBufferContexts.Append( activeStreamBufferContexts[ i ] );
		}
		activeStreamBufferContexts.Clear();
	}
	assert( bufferContexts.Num() == MAX_SOUND_BUFFERS );
	assert( freeStreamBufferContexts.Num() == MAX_SOUND_BUFFERS );
	assert( activeStreamBufferContexts.Num() == 0 );
//	streamBufferMutex.Unlock();
}

/*
========================
idSoundSystemLocal::FreeStreamBuffers
========================
*/
void idSoundSystemLocal::FreeStreamBuffers()
{
//	streamBufferMutex.Lock();
	bufferContexts.Clear();
	freeStreamBufferContexts.Clear();
	activeStreamBufferContexts.Clear();
//	streamBufferMutex.Unlock();
}

/*
========================
idSoundSystemLocal::Shutdown
========================
*/
void idSoundSystemLocal::Shutdown()
{
	samples.DeleteContents( true );
	sampleHash.Free();
	FreeStreamBuffers();
	hardware.Shutdown();
}

/*
========================
idSoundSystemLocal::ObtainStreamBuffer

Get a stream buffer from the free pool, returns NULL if none are available
========================
*/
idSoundSystemLocal::bufferContext_t* idSoundSystemLocal::ObtainStreamBufferContext()
{
	bufferContext_t* bufferContext = NULL;
//	streamBufferMutex.Lock();
	if( freeStreamBufferContexts.Num() != 0 )
	{
		bufferContext = freeStreamBufferContexts[ freeStreamBufferContexts.Num() - 1 ];
		freeStreamBufferContexts.SetNum( freeStreamBufferContexts.Num() - 1 );
		activeStreamBufferContexts.Append( bufferContext );
	}
//	streamBufferMutex.Unlock();
	return bufferContext;
}

/*
========================
idSoundSystemLocal::ReleaseStreamBuffer

Releases a stream buffer back to the free pool
========================
*/
void idSoundSystemLocal::ReleaseStreamBufferContext( bufferContext_t* bufferContext )
{
//	streamBufferMutex.Lock();
	if( activeStreamBufferContexts.Remove( bufferContext ) )
	{
		freeStreamBufferContexts.Append( bufferContext );
	}
//	streamBufferMutex.Unlock();
}

/*
========================
idSoundSystemLocal::AllocSoundWorld
========================
*/
idSoundWorld* idSoundSystemLocal::AllocSoundWorld( idRenderWorld* rw )
{
	idSoundWorldLocal* local = new idSoundWorldLocal;
	local->renderWorld = rw;
	soundWorlds.Append( local );
	return local;
}

/*
========================
idSoundSystemLocal::FreeSoundWorld
========================
*/
void idSoundSystemLocal::FreeSoundWorld( idSoundWorld* sw )
{
	idSoundWorldLocal* local = static_cast<idSoundWorldLocal*>( sw );
	soundWorlds.Remove( local );
	delete local;
}

/*
========================
idSoundSystemLocal::SetPlayingSoundWorld

Specifying NULL will cause silence to be played.
========================
*/
void idSoundSystemLocal::SetPlayingSoundWorld( idSoundWorld* soundWorld )
{
	if( currentSoundWorld == soundWorld )
	{
		return;
	}
	idSoundWorldLocal* oldSoundWorld = currentSoundWorld;

	currentSoundWorld = static_cast<idSoundWorldLocal*>( soundWorld );

	if( oldSoundWorld != NULL )
	{
		oldSoundWorld->Update();
	}
}

/*
========================
idSoundSystemLocal::GetPlayingSoundWorld
========================
*/
idSoundWorld* idSoundSystemLocal::GetPlayingSoundWorld()
{
	return currentSoundWorld;
}

/*
========================
idSoundSystemLocal::Render
========================
*/
void idSoundSystemLocal::Render()
{

	if( s_noSound.GetBool() )
	{
		Sound_UpdateControllerRumble( 0.0f );
		if( hardware.InitFailed() )
		{
			// The engine silenced itself after a device/context failure; keep
			// the hardware's periodic re-init retry alive so audio recovers
			// when a device becomes available (it clears s_noSound and
			// schedules a restart on success). User-set s_noSound stays quiet.
			hardware.Update();
		}
		return;
	}

	if( needsRestart && !SoundSettings_BlockAutomaticRestart() )
	{
		needsRestart = false;
		Restart();
	}

//	SCOPED_PROFILE_EVENT( "SoundSystem::Render" );
	SoundSettingsNormalUpdateScope settingsUpdate;

	if( currentSoundWorld != NULL )
	{
		currentSoundWorld->Update();
	}

	const float controllerRumble = ( currentSoundWorld != NULL && session != NULL && currentSoundWorld == session->sw ) ?
		currentSoundWorld->CurrentRumbleAmplitude() : 0.0f;
	Sound_UpdateControllerRumble( controllerRumble );

	hardware.Update();

	// The sound system doesn't use game time or anything like that because the sounds are decoded in real time.
	soundTime = Sys_Milliseconds();
	settingsUpdate.completed = true;
}

/*
========================
idSoundSystemLocal::OnReloadSound
========================
*/
void idSoundSystemLocal::OnReloadSound( const idDecl* sound )
{
	for( int i = 0; i < soundWorlds.Num(); i++ )
	{
		soundWorlds[i]->OnReloadSound( sound );
	}
}

/*
========================
idSoundSystemLocal::StopAllSounds
========================
*/
void idSoundSystemLocal::StopAllSounds()
{
	for( int i = 0; i < soundWorlds.Num(); i++ )
	{
		idSoundWorld* sw = soundWorlds[i];
		if( sw )
		{
			sw->StopAllSounds();
		}
	}
	hardware.Update();
}

/*
========================
idSoundSystemLocal::GetIXAudio2
========================
*/
void* idSoundSystemLocal::GetIXAudio2() const
{
	// RB begin
#if defined(USE_OPENAL)
	return NULL;
#else
	return ( void* )hardware.GetIXAudio2();
#endif
	// RB end
}

/*
========================
idSoundSystemLocal::GetOpenALDevice
========================
*/
// RB begin
void* idSoundSystemLocal::GetOpenALDevice() const
{
#if defined(USE_OPENAL)
	return ( void* )hardware.GetOpenALDevice();
#else
	return ( void* )hardware.GetIXAudio2();
#endif
}
// RB end

/*
========================
idSoundSystemLocal::IsEAXAvailable
========================
*/
int idSoundSystemLocal::IsEAXAvailable() const
{
	if( !s_useEAXReverb.GetBool() || !s_useOpenAL.GetBool() )
	{
		return -1;
	}

#if defined(USE_OPENAL)
	ALCdevice* device = hardware.GetOpenALDevice();
	if( device == NULL )
	{
		return 2;
	}

	ALCint major = 0;
	ALCint minor = 0;
	alcGetIntegerv( device, ALC_MAJOR_VERSION, 1, &major );
	alcGetIntegerv( device, ALC_MINOR_VERSION, 1, &minor );
	if( CheckALCErrors( device ) != ALC_NO_ERROR )
	{
		return 0;
	}
	if( major < 1 || ( major == 1 && minor < 1 ) )
	{
		return 0;
	}
	if( alcIsExtensionPresent( device, "ALC_EXT_EFX" ) != AL_TRUE )
	{
		return 0;
	}

	return hardware.HasEFX() ? 1 : 0;
#else
	return 0;
#endif
}

/*
========================
idSoundSystemLocal::SoundTime
========================
*/
int idSoundSystemLocal::SoundTime() const
{
	return soundTime;
}

/*
========================
idSoundSystemLocal::AllocateVoice
========================
*/
idSoundVoice* idSoundSystemLocal::AllocateVoice( const idSoundSample* leadinSample, const idSoundSample* loopingSample )
{
	return hardware.AllocateVoice( leadinSample, loopingSample );
}

/*
========================
idSoundSystemLocal::FreeVoice
========================
*/
void idSoundSystemLocal::FreeVoice( idSoundVoice* voice )
{
	hardware.FreeVoice( voice );
}

/*
========================
idSoundSystemLocal::LoadSample
========================
*/
idSoundSample* idSoundSystemLocal::LoadSample( const char* name )
{
	fileSystem->RecordLevelLoadResource( LEVEL_LOAD_RESOURCE_SOUND, name,
		va( "language=%s", cvarSystem->GetCVarString( "sys_lang" ) ), 0, 1 );
	idStr canonical = name;
	canonical.ToLower();
	canonical.BackSlashesToSlashes();
	idStr extension;
	canonical.ExtractFileExtension( extension );
	extension.ToLower();
	if( extension.Icmp( "roq" ) != 0 )
	{
		canonical.StripFileExtension();
	}
	int hashKey = idStr::Hash( canonical );
	for( int i = sampleHash.First( hashKey ); i != -1; i = sampleHash.Next( i ) )
	{
		if( idStr::Cmp( samples[i]->GetName(), canonical ) == 0 )
		{
			samples[i]->SetLevelLoadReferenced();
			return samples[i];
		}
	}
	idSoundSample* sample = new idSoundSample;
	sample->SetName( canonical );
	sampleHash.Add( hashKey, samples.Append( sample ) );
	//if( !insideLevelLoad )
	//{
		// Sound sample referenced before any map is loaded
		sample->SetNeverPurge();
		sample->LoadResource();
	//}
	//else
	//{
		sample->SetLevelLoadReferenced();
	//}

	if( cvarSystem->GetCVarBool( "fs_buildgame" ) )
	{
//		fileSystem->AddSamplePreload( canonical );
	}

	return sample;
}

/*
========================
idSoundSystemLocal::StopVoicesWithSample

A sample is about to be freed, make sure the hardware isn't mixing from it.
========================
*/
void idSoundSystemLocal::StopVoicesWithSample( const idSoundSample* const sample )
{
	for( int w = 0; w < soundWorlds.Num(); w++ )
	{
		idSoundWorldLocal* sw = soundWorlds[w];
		if( sw == NULL )
		{
			continue;
		}
		for( int e = 0; e < sw->emitters.Num(); e++ )
		{
			idSoundEmitterLocal* emitter = sw->emitters[e];
			if( emitter == NULL )
			{
				continue;
			}
			for( int i = 0; i < emitter->channels.Num(); i++ )
			{
				if( emitter->channels[i]->leadinSample == sample || emitter->channels[i]->loopingSample == sample )
				{
					emitter->channels[i]->Mute();
				}
			}
		}
	}
}

/*
========================
idSoundSystemLocal::FreeVoice
========================
*/
cinData_t idSoundSystemLocal::ImageForTime( const int milliseconds, const bool waveform )
{
	cinData_t cd;
	memset( &cd, 0, sizeof( cd ) );
	cd.status = FMV_IDLE;
	return cd;
}

/*
========================
idSoundSystemLocal::BeginLevelLoad
========================
*/
void idSoundSystemLocal::BeginLevelLoad()
{
	insideLevelLoad = true;
	for( int i = 0; i < samples.Num(); i++ )
	{
		if( samples[i]->GetNeverPurge() )
		{
			continue;
		}
		samples[i]->FreeData();
		samples[i]->ResetLevelLoadReferenced();
	}
}

/*
========================
idSoundSystemLocal::EndLevelLoad
========================
*/
void idSoundSystemLocal::EndLevelLoad()
{

	insideLevelLoad = false;
/*
	common->Printf( "----- idSoundSystemLocal::EndLevelLoad -----\n" );
	int		start = Sys_Milliseconds();
	int		keepCount = 0;
	int		loadCount = 0;

	idList< preloadSort_t > preloadSort;
	preloadSort.Resize( samples.Num() );

	for( int i = 0; i < samples.Num(); i++ )
	{
		common->UpdateLevelLoadPacifier();


		if( samples[i]->GetNeverPurge() )
		{
			continue;
		}
		if( samples[i]->IsLoaded() )
		{
			keepCount++;
			continue;
		}
		if( samples[i]->GetLevelLoadReferenced() )
		{
			idStrStatic< MAX_OSPATH > filename  = "generated/";
			filename += samples[ i ]->GetName();
			filename.SetFileExtension( "idwav" );
			preloadSort_t ps = {};
			ps.idx = i;
			idResourceCacheEntry rc;
			if( fileSystem->GetResourceCacheEntry( filename, rc ) )
			{
				ps.ofs = rc.offset;
			}
			else
			{
				ps.ofs = 0;
			}
			preloadSort.Append( ps );
			loadCount++;
		}
	}
	preloadSort.SortWithTemplate( idSort_Preload() );
	for( int i = 0; i < preloadSort.Num(); i++ )
	{
		common->UpdateLevelLoadPacifier();


		samples[ preloadSort[ i ].idx ]->LoadResource();
	}
	int	end = Sys_Milliseconds();

	common->Printf( "%5i sounds loaded in %5.1f seconds\n", loadCount, ( end - start ) * 0.001 );
	common->Printf( "----------------------------------------\n" );
*/
}



/*
========================
idSoundSystemLocal::FreeVoice
========================
*/
void idSoundSystemLocal::PrintMemInfo( MemInfo_t* mi )
{
}

// jmarshall: Quake 4 specific code
idSoundWorld* idSoundSystemLocal::GetSoundWorldFromId(int worldId) {
	switch (worldId)
	{
	case SOUNDWORLD_GAME:
	case SOUNDWORLD_ANY:
		return session->sw;
	case SOUNDWORLD_MENU:
		return session->menuSoundWorld;

	default:
		return session->sw;
	}
}

void idSoundSystemLocal::FreeSoundEmitter(int worldId, int handle, bool immediate)
{
	if( handle <= 0 )
	{
		return;
	}

	idSoundWorldLocal* soundWorld = static_cast<idSoundWorldLocal*>( GetSoundWorldFromId( worldId ) );
	if( soundWorld == NULL )
	{
		return;
	}
	idSoundEmitterLocal* emitter = static_cast<idSoundEmitterLocal*>( soundWorld->EmitterForIndex( handle ) );
	if( emitter == NULL )
	{
		return;
	}

	emitter->Free( immediate );
}
// jmarshall
