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
#include "SoundRestart.h"
#include "SoundMemoryTrim.h"
#include "VoiceChat.h"

// NOCHEAT as in retail Quake4.exe 1.4.2: a multiplayer spawn or join without
// cheats resets every other unarchived CVar, which switched a dedicated server's
// silence (and a player's own s_noSound) back off.
idCVar s_noSound( "s_noSound", "0", CVAR_SOUND | CVAR_BOOL | CVAR_NOCHEAT, "returns NULL for all sounds loaded and does not update the sound rendering" );
idCVar s_volume( "s_volume", "0.5", CVAR_ARCHIVE | CVAR_FLOAT, "master volume (0-1)", 0.0f, 1.0f );
idCVar s_musicVolume( "s_musicVolume", "0.5", CVAR_ARCHIVE | CVAR_FLOAT, "music volume (0-1)", 0.0f, 1.0f );
idCVar s_dialogueVolume( "s_dialogueVolume", "1", CVAR_ARCHIVE | CVAR_FLOAT, "spoken dialogue volume (0-1), independent of music and multiplayer voice chat", 0.0f, 1.0f );
idCVar ui_subtitles( "ui_subtitles", "0", CVAR_GUI | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT, "show installed dialogue transcripts during playback" );
idCVar ui_subtitleScale( "ui_subtitleScale", "1", CVAR_GUI | CVAR_ARCHIVE | CVAR_FLOAT | CVAR_NOCHEAT, "subtitle text size", 1.0f, 2.0f );
idCVar ui_subtitleBackground( "ui_subtitleBackground", "0.6", CVAR_GUI | CVAR_ARCHIVE | CVAR_FLOAT | CVAR_NOCHEAT, "subtitle backing opacity", 0.0f, 1.0f );
idCVar ui_subtitleSpeakers( "ui_subtitleSpeakers", "1", CVAR_GUI | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT, "show available subtitle speaker names" );
idCVar s_speakerFraction( "s_speakerFraction", "0.65", CVAR_ARCHIVE | CVAR_FLOAT, "speaker attenuation fraction" );
idCVar s_radioChatterFraction( "s_radioChatterFraction", "0.9", CVAR_ARCHIVE | CVAR_FLOAT, "radio chatter attenuation fraction" );
idCVar s_frequencyShift( "s_frequencyShift", "1", CVAR_BOOL, "enable sound shader frequency shift playback" );
idCVar s_useOpenAL( "s_useOpenAL", "1", CVAR_ARCHIVE | CVAR_BOOL, "use OpenAL audio backend" );
idCVar s_deviceName( "s_deviceName", "", CVAR_ARCHIVE, "OpenAL device name override" );
idCVar s_useEAXReverb( "s_useEAXReverb", "1", CVAR_SOUND | CVAR_ARCHIVE | CVAR_BOOL, "use EAX reverb if available" );
idCVar s_openALHRTF( "s_openALHRTF", "0", CVAR_ARCHIVE | CVAR_INTEGER, "OpenAL Soft HRTF mode: 0 = auto, 1 = off, 2 = on", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar s_openALEfxDebugMode( "s_openALEfxDebugMode", "0", CVAR_INTEGER, "OpenAL wet/dry debug mode (0=normal, 1=wet-only, 2=dry-only)" );
// 0 leaves the layout to the output device, so headphones get stereo (and OpenAL
// Soft's automatic HRTF) and a 5.1 or 7.1 system gets its own channels. Requesting
// 5.1 everywhere made Windows downmix it on every stereo endpoint.
idCVar s_numberOfSpeakers( "s_numberOfSpeakers", "0", CVAR_ARCHIVE | CVAR_INTEGER, "speaker layout: 0 = the output device's own layout, 2 = stereo, 6 = 5.1 surround" );
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
idCVar s_trimIdleSeconds( "s_trimIdleSeconds", "30", CVAR_SOUND | CVAR_INTEGER,
	"on a low-memory notice, also free this level's samples that no sound has started for this many seconds; they reload when next played (-1 keeps them)", -1, 3600 );

idSoundSystemLocal soundSystemLocal;
idSoundSystem* soundSystem = &soundSystemLocal;

// Engine/platform lifecycle hook; no change to the game-facing sound ABI.
void S_SetDevicePaused( bool paused )
{
	soundSystemLocal.hardware.SetDevicePaused( paused );
}

// Engine/platform lifecycle hook for an operating-system low-memory notice
// (SDL_EVENT_LOW_MEMORY, delivered on Android and iOS).
void S_TrimMemory( const char* reason )
{
	soundSystemLocal.TrimMemory( reason, s_trimIdleSeconds.GetInteger() );
}

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
	int64 totalMemory = 0;
	int64 totalCompressedMemory = 0;
	int64 totalPCMMemory = 0;
	int64 totalResidentMemory = 0;
	int64 totalStreamedMemory = 0;

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
		const char* state = !loaded ? "(PURGED)" : ( sample->IsDefault() ? "(DEFAULTED)" : ( sample->IsStreamed() ? "(STREAMED)" : "" ) );
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
			if( sample->IsStreamed() ) totalStreamedMemory += sampleBytes;
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
	idLib::Printf( "%8lld kB OGG samples loaded\n", static_cast<long long>( totalCompressedMemory / 1024 ) );
	idLib::Printf( "%8lld kB PCM samples loaded\n", static_cast<long long>( totalPCMMemory / 1024 ) );
	idLib::Printf( "%8lld kB total sample data\n", static_cast<long long>( totalMemory / 1024 ) );
	// Sample data is not the same as memory held. OpenAL keeps its own copy of
	// everything uploaded to it, so ours is released; report what is actually
	// still resident rather than letting the decoded size stand in for it.
	idLib::Printf( "%8lld kB held in CPU memory (%lld kB released to OpenAL)\n",
				   static_cast<long long>( totalResidentMemory / 1024 ), static_cast<long long>( ( totalMemory - totalResidentMemory - totalStreamedMemory ) / 1024 ) );
	idLib::Printf( "%8lld kB decoded track size streamed without full PCM residency\n", static_cast<long long>( totalStreamedMemory / 1024 ) );
#if defined(USE_OPENAL)
	idSoundSample_OpenAL::PrintDecodeMemoryInfo();
#endif
	// The whole process, which the payload figures above exclude: compared with a
	// run under s_noSound 1, it measures what audio costs beyond those payloads,
	// such as the provider's mixer, effects and device buffers.
	unsigned long long processPrivate = 0, processResident = 0;
	if( Sys_GetProcessMemoryUsage( processPrivate, processResident ) )
	{
		idLib::Printf( "Process memory: private=%llu KiB resident=%llu KiB\n", processPrivate / 1024, processResident / 1024 );
	}
}

/*
========================
ListSamples_f

Compatibility name retained for OpenQ4 scripts.
========================
*/
void ListReverbs_f( const idCmdArgs& args )
{
	soundSystemLocal.reverb.PrintInfo();
}

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
extern void SoundDialogueVoices_f( const idCmdArgs& args );

void ListPlayingSounds_f( const idCmdArgs& args )
{
	(void)args;

	soundSystemLocal.hardware.ListPlayingVoices();
}

static void ListSubtitles_f( const idCmdArgs& args ) {
	(void)args;
	idSoundWorld* world = soundSystemLocal.GetSoundWorldFromId(SOUNDWORLD_GAME);
	soundSubtitle_t cues[2];
	const int count = world ? world->GetSubtitles(cues, 2) : 0;
	idLib::Printf("Subtitles: %d\n", count);
	for (int i = 0; i < count; ++i) {
		idLib::Printf("Subtitle shader=%s emitter=%d speaker=%d time=%d duration=%d radio=%d textBytes=%d\n",
			cues[i].shader, cues[i].emitterIndex, cues[i].speakerId, cues[i].timeMsec,
			cues[i].durationMsec, cues[i].radio ? 1 : 0, idStr::Length(cues[i].text));
	}
}

// Read the last completed mix and the corresponding OpenAL state. This does
// not update a voice, move the listener or substitute diagnostic attenuation.
static void SoundDistances_f( const idCmdArgs& args )
{
	if( args.Argc() > 2 )
	{
		idLib::Printf( "Usage: s_soundDistances [exact shader name]\n" );
		return;
	}
	const idSoundWorldLocal* world = soundSystemLocal.currentSoundWorld;
	if( world == NULL )
	{
		idLib::Printf( "SOUND_DISTANCE_WORLD available=0\n" );
		return;
	}
	idLib::Printf( "SOUND_DISTANCE_WORLD available=1 listener=%d area=%d origin=%.4f,%.4f,%.4f\n",
		world->listener.id, world->listener.area, world->listener.pos.x, world->listener.pos.y, world->listener.pos.z );
	int count = 0;
	for( int i = 0; i < world->emitters.Num(); ++i )
	{
		const idSoundEmitterLocal* emitter = world->emitters[i];
		if( emitter == NULL ) continue;
		for( int c = 0; c < emitter->channels.Num(); ++c )
		{
			const idSoundChannel* channel = emitter->channels[c];
			if( channel == NULL || channel->soundShader == NULL ) continue;
			const char* name = channel->soundShader->GetName();
			if( args.Argc() == 2 && idStr::Icmp( name, args.Argv( 1 ) ) ) continue;
			idLib::Printf( "SOUND_DISTANCE emitter=%d channel=%d shader=\"%s\" flags=%d "
				"origin=%.4f,%.4f,%.4f direct=%.4f spatial=%.4f min=%.4f max=%.4f "
				"authoredGain=%.7f volumeDB=%.4f portals=%d\n",
				i, channel->logicalChannel, name, channel->parms.soundShaderFlags,
				emitter->origin.x, emitter->origin.y, emitter->origin.z,
				emitter->directDistance, emitter->spatializedDistance,
				channel->parms.minDistance, channel->parms.maxDistance, channel->parms.volume,
				channel->volumeDB, emitter->occludingPortalCount );
			soundSystemLocal.hardware.PrintDistanceInfo( channel->hardwareVoice );
			++count;
		}
	}
	idLib::Printf( "SOUND_DISTANCE_END channels=%d\n", count );
}

// Report the production sound clocks without changing focus, routing or playback.
// Together with listPlayingSounds this distinguishes a frozen world from a
// world that merely became inaudible when another world acquired the output.
// Both clocks derive from one stamp taken once per frame in Render(); stamp and
// now let a caller compare them with wall-clock waits across long frames.
static void SoundWorlds_f( const idCmdArgs& args ) {
	(void)args;
	const int worldIds[] = { SOUNDWORLD_MENU, SOUNDWORLD_GAME };
	for ( int id : worldIds ) {
		idSoundWorldLocal *world = static_cast<idSoundWorldLocal *>( soundSystemLocal.GetSoundWorldFromId( id ) );
		if ( world == NULL ) {
			continue;
		}
		int channels = 0;
		for ( int e = 0; e < world->emitters.Num(); ++e ) {
			channels += world->emitters[e]->channels.Num();
		}
		// underwater reports the listener muffle the game sets when the view is
		// in a liquid, so a liquid check can read it from the log.
		idLib::Printf( "SOUND_WORLD id=%d paused=%d time=%d active=%d channels=%d stamp=%d now=%d underwater=%d\n",
			id, world->IsPaused() ? 1 : 0, world->GetSoundTime(),
			soundSystemLocal.GetPlayingSoundWorld() == world ? 1 : 0, channels,
			soundSystemLocal.SoundTime(), Sys_Milliseconds(), world->underwaterActive ? 1 : 0 );
	}
}

// Each voice's output, routing and position after a device change (listSoundVoices).
static void ListSoundVoices_f( const idCmdArgs& args )
{
	(void)args;
	soundSystemLocal.hardware.ListVoiceOutputState();
}

// The low-memory response on demand, for checking it without an operating-system notice.
static void TrimAudioMemory_f( const idCmdArgs& args )
{
	const int idleSeconds = args.Argc() > 1 ? atoi( args.Argv( 1 ) ) : s_trimIdleSeconds.GetInteger();
	soundSystemLocal.TrimMemory( "console", idleSeconds );
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
		// Only what the current level and the menus hold; samples purged at an earlier
		// level end stay purged until something references them again.
		int reloaded = 0;
		for( int i = 0; i < samples.Num(); i++ )
		{
			if( !samples[i]->GetNeverPurge() && !samples[i]->GetLevelLoadReferenced() )
			{
				continue;
			}
			samples[i]->LoadResource();
			if( samples[i]->IsLoaded() )
			{
				reloaded++;
			}
		}
		idLib::Printf( "%d sound samples reloaded\n", reloaded );
	}
	else
	{
		// Every sample was just freed. Keep the restart pending so turning sound back
		// on reloads them, instead of the device's periodic re-init starting voices on
		// empty samples.
		needsRestart = true;
	}

	SetMute( wasMuted );
}

/*
========================
Sound_OutputState
========================
*/
soundOutputState_t Sound_OutputState( void )
{
	// A failed open also sets s_noSound, so ask the device first.
	if( soundSystemLocal.hardware.InitFailed() )
	{
		return SOUND_OUTPUT_FAILED;
	}
	if( s_noSound.GetBool() )
	{
		return SOUND_OUTPUT_OFF;
	}
	if( soundSystemLocal.needsRestart )
	{
		return SOUND_OUTPUT_RESTARTING;
	}
	return soundSystemLocal.hardware.GetOpenALDevice() != NULL ? SOUND_OUTPUT_READY : SOUND_OUTPUT_OFF;
}

/*
========================
Sound_RequestOutputRestart
========================
*/
bool Sound_RequestOutputRestart( void )
{
	if( Sound_OutputState() != SOUND_OUTPUT_READY )
	{
		return false;
	}
	soundSystemLocal.SetNeedsRestart();
	return true;
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
	SoundVoiceChat_InitCommands();
	cmdSystem->AddCommand( "listSounds", ListSounds_f, CMD_FL_SOUND, "lists all sounds" );
	cmdSystem->AddCommand( "listPlayingSounds", ListPlayingSounds_f, CMD_FL_SOUND, "lists currently playing sounds" );
	cmdSystem->AddCommand( "s_dialogueVoices", SoundDialogueVoices_f, CMD_FL_SOUND, "reports current game-channel dialogue routing and OpenAL gain without printing transcripts" );
	cmdSystem->AddCommand( "listSubtitles", ListSubtitles_f, CMD_FL_SOUND, "lists active subtitle identifiers and sample times without printing transcripts" );
	cmdSystem->AddCommand( "s_soundDistances", SoundDistances_f, CMD_FL_SOUND,
		"reports authored ranges, mixed distances and OpenAL spatial state", idCmdSystem::ArgCompletion_SoundName );
	cmdSystem->AddCommand( "s_soundWorlds", SoundWorlds_f, CMD_FL_SOUND, "reports game/menu sound clocks and pause state" );
	cmdSystem->AddCommand( "listSoundVoices", ListSoundVoices_f, CMD_FL_SOUND, "reports each voice's output, routing and playback position" );
	cmdSystem->AddCommand( "s_trimAudioMemory", TrimAudioMemory_f, CMD_FL_SOUND,
		"frees what a low-memory notice frees: earlier levels' samples, retained CPU copies and this level's samples idle for [seconds] (default s_trimIdleSeconds; -1 keeps them)" );
	cmdSystem->AddCommand( "listSoundDecoders", ListSoundDecoders_f, CMD_FL_SOUND, "list active sound decoders" );
	cmdSystem->AddCommand( "reloadSounds", ReloadSounds_f, CMD_FL_SOUND | CMD_FL_CHEAT, "reloads all sounds" );
	cmdSystem->AddCommand( "s_restart", RestartSound_f, CMD_FL_SOUND, "restarts the sound system" );
	cmdSystem->AddCommand( "listSamples", ListSamples_f, CMD_FL_SOUND, "lists all loaded sound samples" );
	cmdSystem->AddCommand( "listReverbs", ListReverbs_f, CMD_FL_SOUND, "lists the reverb presets, the map's area reverbs and the active reverb slots" );

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
	decodingSamples.Clear();
	samples.DeleteContents( true );
	sampleHash.Free();
	FreeStreamBuffers();
	hardware.Shutdown();
	reverb.Clear();
}

/*
========================
idSoundSystemLocal::ShutdownHW

SpawnServer_f calls this when a client turns into a dedicated server, and on every
spawn of openQ4-ded. The engine keeps running: sound shaders, emitters and BSE
effects keep pointers to the sample objects, so unlike Shutdown() this keeps every
one of them and frees only what the device and the decoded audio hold.
========================
*/
void idSoundSystemLocal::ShutdownHW()
{
	// LoadResource makes every sample loaded from now on a short silent default,
	// as on a dedicated server, and Render no longer drives the device.
	s_noSound.SetBool( true );

	// Release each channel's voice before the device destroys the voices.
	for( int i = 0; i < soundWorlds.Num(); i++ )
	{
		idSoundWorldLocal* sw = soundWorlds[i];
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
			for( int c = 0; c < emitter->channels.Num(); c++ )
			{
				emitter->channels[c]->Mute();
			}
		}
	}
	// Free sample buffers while OpenAL is still current; FreeData also cancels
	// a decode still running on a job worker.
	for( int i = 0; i < samples.Num(); i++ )
	{
		samples[i]->FreeData();
	}
	decodingSamples.Clear();
	FreeStreamBuffers();
	if( hardware.GetOpenALDevice() != NULL )
	{
		hardware.Shutdown();
	}
	// After a failed init the device is retried every second while s_noSound
	// holds, and a device it found would lift this silence again.
	hardware.ClearInitFailure();

	// Turning s_noSound off again reloads what the menus and the level hold.
	needsRestart = true;
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
	SoundVoiceChat_Render();

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
idSoundSystemLocal::RefreshSoundTime
========================
*/
int idSoundSystemLocal::RefreshSoundTime()
{
	soundTime = Sys_Milliseconds();
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
			if( !insideLevelLoad )
			{
				// referenced from the menus or mid-level: keep it from now on
				samples[i]->SetNeverPurge();
			}
			if( !samples[i]->IsLoaded() )
			{
				// purged when an earlier level ended
				if( insideLevelLoad )
				{
					FinishSampleDecodes( false );
					samples[i]->LoadResourceAsync();
					if( samples[i]->IsDecoding() )
					{
						decodingSamples.Append( samples[i] );
					}
				}
				else
				{
					samples[i]->LoadResource();
				}
			}
			return samples[i];
		}
	}
	idSoundSample* sample = new idSoundSample;
	sample->SetName( canonical );
	sample->SetLevelLoadReferenced();
	sampleHash.Add( hashKey, samples.Append( sample ) );
	// Doom 3's sound cache: a sample first referenced outside a level load (the menus,
	// the HUD, a sound a script reaches for mid-level) is never purged; one a level
	// load references is kept while some level references it. Marking every sample
	// never-purge kept the decoded audio of every map visited, about 218 MB a map.
	if( !insideLevelLoad )
	{
		sample->SetNeverPurge();
		sample->LoadResource();
	}
	else
	{
		// its PCM decodes on a job worker while the level keeps loading; it uploads
		// once the worker is done, or at EndLevelLoad
		FinishSampleDecodes( false );
		sample->LoadResourceAsync();
		if( sample->IsDecoding() )
		{
			decodingSamples.Append( sample );
		}
	}
	if( cvarSystem->GetCVarBool( "fs_buildgame" ) )
	{
//		fileSystem->AddSamplePreload( canonical );
	}

	return sample;
}

/*
========================
idSoundSystemLocal::FinishSampleDecodes
========================
*/
void idSoundSystemLocal::FinishSampleDecodes( const bool all )
{
	for( int i = 0; i < decodingSamples.Num(); )
	{
		idSoundSample* sample = decodingSamples[i];
		if( all || !sample->IsDecoding() || sample->IsDecodeComplete() )
		{
			sample->FinishDecode();
			decodingSamples.RemoveIndex( i );
			continue;
		}
		i++;
	}
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
idSoundSystemLocal::TrimMemory

The response to an operating-system low-memory notice (SoundMemoryTrim.h):
earlier levels' samples, CPU copies OpenAL also holds, and with idleSeconds >= 0
this level's samples no sound has started for that long. Nothing a channel
references is touched, and a freed sample reloads when a sound starts it again.
========================
*/
void idSoundSystemLocal::TrimMemory( const char* reason, int idleSeconds )
{
	// Decide what to keep before joining work. Finishing every pending decode here
	// used to upload more PCM under memory pressure, even for samples about to be
	// freed. FreeData cancels and joins its own worker; retained jobs keep ownership
	// of their buffers and HasCpuCopy excludes unfinished decodes.
	idList<const idSoundSample*> active;
	for( int w = 0; w < soundWorlds.Num(); w++ )
	{
		const idSoundWorldLocal* world = soundWorlds[w];
		if( world == NULL )
		{
			continue;
		}
		for( int e = 0; e < world->emitters.Num(); e++ )
		{
			const idSoundEmitterLocal* emitter = world->emitters[e];
			if( emitter == NULL )
			{
				continue;
			}
			for( int c = 0; c < emitter->channels.Num(); c++ )
			{
				const idSoundChannel* channel = emitter->channels[c];
				if( channel == NULL )
				{
					continue;
				}
				if( channel->leadinSample != NULL )
				{
					active.AddUnique( channel->leadinSample );
				}
				if( channel->loopingSample != NULL )
				{
					active.AddUnique( channel->loopingSample );
				}
			}
		}
	}
	const int now = Sys_Milliseconds();
	const int idleLimitMsec = idleSeconds < 0 ? -1 : Min( idleSeconds, 24 * 60 * 60 ) * 1000;
	int freed = 0, copies = 0, kept = 0;
	int64 freedBytes = 0, copyBytes = 0;
	for( int i = 0; i < samples.Num(); i++ )
	{
		idSoundSample* sample = samples[i];
		if( !sample->IsLoaded() || sample->IsDefault() )
		{
			continue;
		}
		SoundTrimSample state;
		state.active = active.FindIndex( sample ) >= 0;
		state.neverPurge = sample->GetNeverPurge();
		state.levelReferenced = sample->GetLevelLoadReferenced();
		state.streamed = sample->IsStreamed();
		state.cpuCopy = sample->HasCpuCopy();
		state.played = sample->GetLastPlayedTime() != 0;
		state.idleMsec = now - static_cast<int>( sample->GetLastPlayedTime() );
		switch( SoundTrimDecide( state, idleLimitMsec, insideLevelLoad ) )
		{
			case SoundTrimAction::Free:
				freedBytes += sample->BufferSize();
				sample->FreeData();
				decodingSamples.Remove( sample );
				freed++;
				break;
			case SoundTrimAction::ReleaseCopy:
				copyBytes += sample->ReleaseCpuCopy();
				copies++;
				break;
			default:
				kept += state.active ? 1 : 0;
				break;
		}
	}
	common->Printf( "Audio memory trim (%s): freed %d samples (%.1f MB) and %d CPU copies (%.1f MB); %d samples in use kept\n",
		reason, freed, freedBytes / ( 1024.0 * 1024.0 ), copies, copyBytes / ( 1024.0 * 1024.0 ), kept );
	common->Printf( "AUDIO_TRIM reason=%s idle=%d freed=%d freedKiB=%lld copies=%d copyKiB=%lld active=%d\n", reason, idleSeconds,
		freed, static_cast<long long>( freedBytes / 1024 ), copies, static_cast<long long>( copyBytes / 1024 ), kept );
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
	// Clear the marks but keep the data, so samples the next level shares with this
	// one are not decoded again; EndLevelLoad frees the rest.
	for( int i = 0; i < samples.Num(); i++ )
	{
		if( samples[i]->GetNeverPurge() )
		{
			continue;
		}
		samples[i]->ResetLevelLoadReferenced();
	}
}

/*
========================
idSoundSystemLocal::EndLevelLoad
========================
*/
void idSoundSystemLocal::EndLevelLoad( const char* mapName )
{

	insideLevelLoad = false;

	// Retail loads the reverb presets and the map's area table here, once the
	// game render world (and so its portal areas) exists.
	reverb.LoadLevel( mapName, session != NULL ? session->rw : NULL );

	// Free the samples earlier levels loaded that this one did not reference.
	int purged = 0;
	int64 purgedBytes = 0;
	for( int i = 0; i < samples.Num(); i++ )
	{
		idSoundSample* sample = samples[i];
		if( sample->GetNeverPurge() || sample->GetLevelLoadReferenced() || !sample->IsLoaded() )
		{
			continue;
		}
		purgedBytes += sample->BufferSize();
		sample->FreeData();
		purged++;
	}
	if( purged > 0 )
	{
		common->Printf( "%d sound samples from earlier levels freed (%.1f MB)\n", purged, purgedBytes / ( 1024.0 * 1024.0 ) );
	}
	// Finish only retained work. FreeData above cancels an old level's unfinished
	// decode instead of decoding and uploading it just before throwing it away.
	FinishSampleDecodes( true );
	for( int i = 0; i < samples.Num(); i++ )
	{
		samples[i]->FinishDecode();
	}
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

/*
========================
idSoundSystemLocal reverb editor interface
========================
*/
const char* idSoundSystemLocal::GetReverbName( int reverbIndex )
{
	return reverb.GetReverbName( reverbIndex );
}

int idSoundSystemLocal::GetNumAreas()
{
	return reverb.GetNumAreas();
}

int idSoundSystemLocal::GetReverb( int area )
{
	return reverb.GetReverb( area );
}

bool idSoundSystemLocal::SetReverb( int area, const char* reverbName, const char* fileName )
{
	return reverb.SetReverb( area, reverbName, fileName );
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

/*
===============================================================================

	Retail environmental reverb

	The efxs/*.efx presets, the maps/<map>.reverb area tables and the four
	reverb slots that follow the listener (Quake4.exe 1.4.2). The slot planning
	and the EAX to EFX arithmetic live in SoundReverbCore.h; the design record is
	docs/dev/retail-audio-reverb.md.

===============================================================================
*/

idCVar s_useEAXOcclusion( "s_useEAXOcclusion", "1", CVAR_SOUND | CVAR_BOOL | CVAR_ARCHIVE, "use EAX occlusion" );
idCVar s_muteEAXReverb( "s_muteEAXReverb", "0", CVAR_SOUND | CVAR_BOOL, "mute EAX reverb" );
idCVar s_showReverb( "s_showReverb", "0", CVAR_SOUND | CVAR_BOOL, "print the reverb slots whenever the listener's areas or their reverbs change" );

static const char* const SOUND_REVERB_DEFAULT_EFX = "efxs/default.efx";
static const int SOUND_REVERB_EFX_VERSION = 1;

/*
========================
idSoundReverb::idSoundReverb
========================
*/
idSoundReverb::idSoundReverb()
{
	efxLoaded = false;
	numSlots = 0;
	slotGeneration = 0;
	ResetSlots();
}

/*
========================
idSoundReverb::Clear
========================
*/
void idSoundReverb::Clear()
{
	effects.Clear();
	efxLoaded = false;
	efxFileName.Clear();
	areaReverbs.Clear();
	reverbFileName.Clear();
	portals.Clear();
	ResetSlots();
}

/*
========================
idSoundReverb::ResetSlots
========================
*/
void idSoundReverb::ResetSlots()
{
	for( int i = 0; i < SOUND_REVERB_SLOTS; i++ )
	{
		slotToArea[i] = -1;
	}
	primarySlot = 0;
	lastShownState.Clear();
}

/*
========================
SoundReverb_UnsignedToken

Retail reads "environment" and "flags" as a raw number token; anything else is 0.
========================
*/
static unsigned int SoundReverb_UnsignedToken( idToken& token )
{
	if( token.type != TT_NUMBER )
	{
		return 0;
	}
	return token.GetUnsignedLongValue();
}

/*
========================
idSoundReverb::ReadEffect

Retail idEFXFile::ReadEffect: reverb "<name>" { "<property>" <value> ... }.
========================
*/
bool idSoundReverb::ReadEffect( idLexer& src, effect_t& effect )
{
	idToken token;
	if( !src.ReadToken( &token ) )
	{
		return false;
	}
	if( token != "reverb" )
	{
		src.Error( "idEFXFile::ReadEffect: Unknown effect definition" );
		return false;
	}

	idToken name;
	src.ReadTokenOnLine( &name );

	if( !src.ReadToken( &token ) )
	{
		return false;
	}
	if( token != "{" )
	{
		src.Error( "idEFXFile::ReadEffect: { not found, found %s", token.c_str() );
		return false;
	}

	soundReverbEAX_t& p = effect.properties;
	SoundReverb_DefaultEAX( p );
	while( src.ReadToken( &token ) )
	{
		if( token == "}" )
		{
			effect.name = name;
			return true;
		}

		if( token == "environment" )
		{
			src.ReadTokenOnLine( &token );
			p.environment = SoundReverb_UnsignedToken( token );
		}
		else if( token == "environment size" )
		{
			p.environmentSize = src.ParseFloat();
		}
		else if( token == "environment diffusion" )
		{
			p.environmentDiffusion = src.ParseFloat();
		}
		else if( token == "room" )
		{
			p.room = src.ParseInt();
		}
		else if( token == "room hf" )
		{
			p.roomHF = src.ParseInt();
		}
		else if( token == "room lf" )
		{
			p.roomLF = src.ParseInt();
		}
		else if( token == "decay time" )
		{
			p.decayTime = src.ParseFloat();
		}
		else if( token == "decay hf ratio" )
		{
			p.decayHFRatio = src.ParseFloat();
		}
		else if( token == "decay lf ratio" )
		{
			p.decayLFRatio = src.ParseFloat();
		}
		else if( token == "reflections" )
		{
			p.reflections = src.ParseInt();
		}
		else if( token == "reflections delay" )
		{
			p.reflectionsDelay = src.ParseFloat();
		}
		else if( token == "reflections pan" )
		{
			p.reflectionsPan[0] = src.ParseFloat();
			p.reflectionsPan[1] = src.ParseFloat();
			p.reflectionsPan[2] = src.ParseFloat();
		}
		else if( token == "reverb" )
		{
			p.reverb = src.ParseInt();
		}
		else if( token == "reverb delay" )
		{
			p.reverbDelay = src.ParseFloat();
		}
		else if( token == "reverb pan" )
		{
			p.reverbPan[0] = src.ParseFloat();
			p.reverbPan[1] = src.ParseFloat();
			p.reverbPan[2] = src.ParseFloat();
		}
		else if( token == "echo time" )
		{
			p.echoTime = src.ParseFloat();
		}
		else if( token == "echo depth" )
		{
			p.echoDepth = src.ParseFloat();
		}
		else if( token == "modulation time" )
		{
			p.modulationTime = src.ParseFloat();
		}
		else if( token == "modulation depth" )
		{
			p.modulationDepth = src.ParseFloat();
		}
		else if( token == "air absorption hf" )
		{
			p.airAbsorptionHF = src.ParseFloat();
		}
		else if( token == "hf reference" )
		{
			p.hfReference = src.ParseFloat();
		}
		else if( token == "lf reference" )
		{
			p.lfReference = src.ParseFloat();
		}
		else if( token == "room rolloff factor" )
		{
			p.roomRolloffFactor = src.ParseFloat();
		}
		else if( token == "flags" )
		{
			src.ReadTokenOnLine( &token );
			p.flags = SoundReverb_UnsignedToken( token );
		}
		else
		{
			src.ReadTokenOnLine( &token );
			src.Error( "idEFXFile::ReadEffect: Invalid parameter in reverb definition" );
			return false;
		}
	}

	src.Error( "idEFXFile::ReadEffect: EOF without closing brace" );
	return false;
}

/*
========================
idSoundReverb::LoadEffectFile

Retail idEFXFile::LoadFile. Quake4.exe 1.4.2 skips an effect that fails to
parse and keeps reading. Its lexer errors are fatal; openQ4 reports them as
warnings so a damaged mod file cannot end the session.
========================
*/
bool idSoundReverb::LoadEffectFile( const char* fileName )
{
	effects.Clear();

	idLexer src( LEXFL_NOSTRINGCONCAT | LEXFL_NOFATALERRORS );
	if( !src.LoadFile( fileName ) )
	{
		return false;
	}
	if( !src.ExpectTokenString( "Version" ) )
	{
		return false;
	}
	if( src.ParseInt() != SOUND_REVERB_EFX_VERSION )
	{
		src.Error( "idEFXFile::LoadFile: Unknown file version" );
		return false;
	}

	while( !src.EndOfFile() )
	{
		effect_t effect;
		if( ReadEffect( src, effect ) )
		{
			effects.Append( effect );
		}
	}
	return true;
}

/*
========================
idSoundReverb::LoadAreaTable

Retail idSoundSystemLocal::LoadReverbData: every area starts on the first
preset, then reverb { { <area> <preset> } ... } names the others.
========================
*/
void idSoundReverb::LoadAreaTable( const char* mapName, int numAreas )
{
	areaReverbs.SetNum( Max( numAreas, 0 ) );
	for( int i = 0; i < areaReverbs.Num(); i++ )
	{
		areaReverbs[i] = 0;
	}
	reverbFileName.Clear();

	if( mapName == NULL )
	{
		return;
	}
	idStr fileName = mapName;
	fileName.SetFileExtension( ".reverb" );
	if( fileSystem->ReadFile( fileName, NULL ) < 0 )
	{
		return;
	}

	idLexer parser( LEXFL_NOFATALERRORS );
	if( !parser.LoadFile( fileName ) )
	{
		return;
	}
	reverbFileName = fileName;

	idToken token;
	if( !parser.ReadToken( &token ) )
	{
		return;
	}
	if( token.Icmp( "reverb" ) )
	{
		common->Warning( "Malformed reverb file header in '%s'", fileName.c_str() );
		return;
	}
	parser.SkipUntilString( "{" );
	if( !parser.ReadToken( &token ) )
	{
		return;
	}

	bool wellFormed = true;
	while( token == "{" )
	{
		const int area = parser.ParseInt();
		if( !parser.ReadToken( &token ) )
		{
			wellFormed = false;
			break;
		}
		if( area < 0 || area >= areaReverbs.Num() )
		{
			// Five stock tables (hub1, mcc_landing, network1, process2, q4ctf6) still
			// name areas of an older compile of their map. Retail warns and skips them;
			// the warning is developer-only here because players cannot act on it.
			common->DWarning( "Invalid reverb area number %d [0,%d] in '%s'", area, areaReverbs.Num(), fileName.c_str() );
		}
		else
		{
			SetReverb( area, token.c_str(), fileName.c_str() );
		}
		if( !parser.ReadToken( &token ) || token != "}" || !parser.ReadToken( &token ) )
		{
			wellFormed = false;
			break;
		}
	}

	if( wellFormed && token == "}" )
	{
		common->Printf( "Loaded reverb file '%s'\n", fileName.c_str() );
	}
	else
	{
		common->Warning( "Malformed reverb file '%s' line %d", fileName.c_str(), parser.GetLineNum() );
	}
}

/*
========================
idSoundReverb::LoadLevel
========================
*/
void idSoundReverb::LoadLevel( const char* mapName, const idRenderWorld* renderWorld )
{
	idStr mapEfx = mapName != NULL ? mapName : "";
	mapEfx.SetFileExtension( ".efx" );
	mapEfx.StripPath();
	idStr efxName = "efxs/";
	efxName += mapEfx;

	efxLoaded = LoadEffectFile( efxName );
	if( efxLoaded )
	{
		efxFileName = efxName;
		common->Printf( "sound: found %s\n", efxName.c_str() );
	}
	else
	{
		efxLoaded = LoadEffectFile( SOUND_REVERB_DEFAULT_EFX );
		if( efxLoaded )
		{
			efxFileName = SOUND_REVERB_DEFAULT_EFX;
			common->Printf( "sound: found %s\n", SOUND_REVERB_DEFAULT_EFX );
		}
		else
		{
			efxFileName.Clear();
			common->Printf( "sound: missing %s and %s\n", efxName.c_str(), SOUND_REVERB_DEFAULT_EFX );
		}
	}

	LoadAreaTable( mapName, renderWorld != NULL ? renderWorld->NumAreas() : 0 );
	ResetSlots();
	soundSystemLocal.hardware.SetPrimaryReverbSlot( 0 );
}

/*
========================
idSoundReverb reverb editor interface
========================
*/
const char* idSoundReverb::GetReverbName( int reverb ) const
{
	if( !efxLoaded || reverb < 0 || reverb >= effects.Num() )
	{
		return NULL;
	}
	return effects[reverb].name.c_str();
}

int idSoundReverb::GetNumAreas() const
{
	return areaReverbs.Num();
}

int idSoundReverb::GetReverb( int area ) const
{
	if( area < 0 || area >= areaReverbs.Num() )
	{
		return 0;
	}
	return areaReverbs[area];
}

bool idSoundReverb::SetReverb( int area, const char* reverbName, const char* fileName )
{
	if( reverbName == NULL )
	{
		reverbName = "";
	}
	if( fileName == NULL )
	{
		fileName = "";
	}
	if( area < 0 || area >= areaReverbs.Num() )
	{
		common->Warning( "Invalid reverb area number %d [0,%d] in '%s'", area, areaReverbs.Num(), fileName );
		return false;
	}
	for( int i = 0;; i++ )
	{
		const char* name = GetReverbName( i );
		if( name == NULL )
		{
			break;
		}
		if( idStr::Icmp( name, reverbName ) )
		{
			continue;
		}
		areaReverbs[area] = i;
		return true;
	}
	common->Warning( "Unknown reverb type '%s' in '%s'", reverbName, fileName );
	return false;
}

/*
========================
idSoundReverb::FindEffect

Retail idEFXFile::FindEffect: the first effect with exactly this name.
========================
*/
int idSoundReverb::FindEffect( const char* name ) const
{
	for( int i = 0; i < effects.Num(); i++ )
	{
		if( !idStr::Cmp( effects[i].name, name ) )
		{
			return i;
		}
	}
	return -1;
}

/*
========================
idSoundReverb::EffectForArea

The effect a slot plays for an area: the area's preset looked up by name, as
retail does, else an effect named "default".
========================
*/
int idSoundReverb::EffectForArea( void* context, int area )
{
	const idSoundReverb* reverb = static_cast<const idSoundReverb*>( context );
	const char* name = reverb->GetReverbName( reverb->GetReverb( area ) );
	int effect = reverb->FindEffect( name != NULL ? name : "" );
	if( effect < 0 )
	{
		effect = reverb->FindEffect( "default" );
	}
	return effect;
}

/*
========================
idSoundReverb::SlotForArea
========================
*/
int idSoundReverb::SlotForArea( int area ) const
{
	if( area < 0 )
	{
		return -1;
	}
	return SoundReverb_SlotForArea( slotToArea, numSlots, area );
}

/*
========================
idSoundReverb::Update

Retail runs this from the mixer whenever EAX reverb is on and a preset file
loaded. Only the game sound world moves the slots; the menus keep them as the
game left them.
========================
*/
void idSoundReverb::Update( idSoundWorldLocal* world )
{
	idSoundHardware& hardware = soundSystemLocal.hardware;
	const int slots = hardware.SyncReverbSlots();
	if( slots != numSlots || hardware.GetReverbSlotGeneration() != slotGeneration )
	{
		// new, replaced or released slots start without area pins
		numSlots = slots;
		slotGeneration = hardware.GetReverbSlotGeneration();
		ResetSlots();
	}
	// Retail has one listener, which only the game places. The menu sound world
	// shares the game render world but never places its listener, so only the
	// game world moves the slots and the menus play with what the game left.
	if( numSlots <= 0 || !efxLoaded || world == NULL || world->renderWorld == NULL || session == NULL || world != session->sw )
	{
		return;
	}
	const listener_t& listener = world->listener;
	if( listener.area < 0 )
	{
		return;
	}

	idRenderWorld* renderWorld = world->renderWorld;
	portals.SetNum( 0 );
	if( listener.area < renderWorld->NumAreas() )
	{
		const int numPortals = renderWorld->NumPortalsInArea( listener.area );
		for( int p = 0; p < numPortals; p++ )
		{
			const exitPortal_t exitPortal = renderWorld->GetPortal( listener.area, p );
			if( exitPortal.w == NULL )
			{
				continue;
			}
			soundReverbPortal_t& portal = portals.Alloc();
			portal.area = exitPortal.areas[0] == listener.area ? exitPortal.areas[1] : exitPortal.areas[0];
			const idVec3 center = exitPortal.w->GetCenter();
			idBounds bounds;
			exitPortal.w->GetBounds( bounds );
			for( int i = 0; i < 3; i++ )
			{
				portal.center[i] = center[i];
				portal.mins[i] = bounds[0][i];
				portal.maxs[i] = bounds[1][i];
			}
		}
	}

	soundReverbListener_t planListener;
	for( int i = 0; i < 3; i++ )
	{
		planListener.origin[i] = listener.pos[i];
		for( int j = 0; j < 3; j++ )
		{
			planListener.axis[i][j] = listener.axis[i][j];
		}
	}
	planListener.area = listener.area;

	soundReverbSlotPlan_t plan;
	SoundReverb_PlanSlots( planListener, portals.Ptr(), portals.Num(), numSlots, slotToArea, EffectForArea, this, s_muteEAXReverb.GetBool(), plan );

	for( int slot = 0; slot < numSlots; slot++ )
	{
		if( !plan.configured[slot] )
		{
			continue;
		}
		soundReverbEAX_t properties = effects[plan.effect[slot]].properties;
		if( plan.muteRoom[slot] )
		{
			properties.room = SOUND_REVERB_MUTED_ROOM_MB;
		}
		for( int i = 0; i < 3; i++ )
		{
			properties.reflectionsPan[i] = plan.pan[slot][i];
			properties.reverbPan[i] = plan.pan[slot][i];
		}
		hardware.SetReverbSlotProperties( slot, properties );
	}
	if( plan.primarySlot >= 0 )
	{
		primarySlot = plan.primarySlot;
		hardware.SetPrimaryReverbSlot( primarySlot );
	}

	if( s_showReverb.GetBool() )
	{
		ShowState( listener );
	}
}

/*
========================
idSoundReverb::ShowState
========================
*/
void idSoundReverb::ShowState( const listener_t& listener )
{
	idStr state = va( "reverb: listener area %d, slots", listener.area );
	for( int slot = 0; slot < numSlots; slot++ )
	{
		state += va( " [%d%s] ", slot, slot == primarySlot ? "*" : "" );
		const int area = slotToArea[slot];
		if( area < 0 )
		{
			state += "-";
			continue;
		}
		const int effect = EffectForArea( this, area );
		state += va( "area %d ", area );
		state += effect >= 0 ? effects[effect].name.c_str() : "?";
	}
	if( state != lastShownState )
	{
		common->Printf( "%s\n", state.c_str() );
		lastShownState = state;
	}
}

/*
========================
idSoundReverb::PrintInfo
========================
*/
void idSoundReverb::PrintInfo() const
{
	if( !efxLoaded )
	{
		common->Printf( "Reverb presets: none loaded\n" );
	}
	else
	{
		common->Printf( "Reverb presets: %s, %d reverbs\n", efxFileName.c_str(), effects.Num() );
		for( int i = 0; i < effects.Num(); i++ )
		{
			common->Printf( "  %2d: %s\n", i, effects[i].name.c_str() );
		}
	}

	if( reverbFileName.Length() > 0 )
	{
		common->Printf( "Area reverbs: %s, %d areas\n", reverbFileName.c_str(), areaReverbs.Num() );
	}
	else
	{
		common->Printf( "Area reverbs: no table for this map, %d areas use reverb 0\n", areaReverbs.Num() );
	}
	for( int i = 0; i < effects.Num(); i++ )
	{
		int count = 0;
		for( int area = 0; area < areaReverbs.Num(); area++ )
		{
			count += ( areaReverbs[area] == i ) ? 1 : 0;
		}
		if( count > 0 )
		{
			common->Printf( "  %-16s %d areas\n", effects[i].name.c_str(), count );
		}
	}

	common->Printf( "Reverb slots: %d, primary %d\n", numSlots, primarySlot );
	for( int slot = 0; slot < numSlots; slot++ )
	{
		const int area = slotToArea[slot];
		const int effect = area >= 0 ? EffectForArea( const_cast<idSoundReverb*>( this ), area ) : -1;
		common->Printf( "  [%d]%s area %d %s\n", slot, slot == primarySlot ? "*" : " ", area,
			effect >= 0 ? effects[effect].name.c_str() : "-" );
	}
}
