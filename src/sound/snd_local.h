/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2013-2016 Robert Beckebans
Copyright (C) 2014-2016 Kot in Action Creative Artel

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

#ifndef __SND_LOCAL_H__
#define __SND_LOCAL_H__

#include "WaveFile.h"
#include "SoundReverbCore.h"

// Maximum number of voices we can have allocated
#define MAX_HARDWARE_VOICES 48

// A single voice can play multiple channels (up to 5.1, but most commonly stereo)
// This is the maximum number of channels which can play simultaneously
// This is limited primarily by seeking on the optical drive, secondarily by memory consumption, and tertiarily by CPU time spent mixing
#define MAX_HARDWARE_CHANNELS 64

// We may need up to 3 buffers for each hardware voice if they are all long sounds
#define MAX_SOUND_BUFFERS ( MAX_HARDWARE_VOICES * 3 )

// Maximum number of channels in a sound sample
#define MAX_CHANNELS_PER_VOICE	8

/*
========================
MsecToSamples
SamplesToMsec
========================
*/
ID_INLINE_EXTERN uint32 MsecToSamples( uint32 msec, uint32 sampleRate )
{
	if( sampleRate == 0 )
	{
		return 0;
	}
	const uint64 samples = ( static_cast<uint64>( msec ) * static_cast<uint64>( sampleRate ) ) / 1000ULL;
	return samples > static_cast<uint64>( idMath::INT_MAX ) ? static_cast<uint32>( idMath::INT_MAX ) : static_cast<uint32>( samples );
}
ID_INLINE_EXTERN uint32 SamplesToMsec( uint32 samples, uint32 sampleRate )
{
	if( sampleRate == 0 )
	{
		return 0;
	}
	const uint64 msec = ( static_cast<uint64>( samples ) * 1000ULL ) / static_cast<uint64>( sampleRate );
	return msec > static_cast<uint64>( idMath::INT_MAX ) ? static_cast<uint32>( idMath::INT_MAX ) : static_cast<uint32>( msec );
}

/*
========================
DBtoLinear
LinearToDB
========================
*/
ID_INLINE_EXTERN float DBtoLinear( float db )
{
	return idMath::Pow( 2.0f, db * ( 1.0f / 6.0f ) );
}
ID_INLINE_EXTERN float LinearToDB( float linear )
{
	return ( linear > 0.0f ) ? ( idMath::Log( linear ) * ( 6.0f / 0.693147181f ) ) : -999.0f;
}

// demo sound commands
typedef enum
{
	SCMD_STATE,				// followed by a load game state
	SCMD_PLACE_LISTENER,
	SCMD_ALLOC_EMITTER,
	SCMD_FREE,
	SCMD_UPDATE,
	SCMD_START,
	SCMD_MODIFY,
	SCMD_STOP,
	SCMD_FADE,
	SCMD_CACHESOUNDSHADER,
} soundDemoCommand_t;

#include "SoundVoice.h"

#if defined(USE_OPENAL)

#define AL_ALEXT_PROTOTYPES

// SRS - Added check on OSX for OpenAL Soft headers vs macOS SDK headers
#if defined(__APPLE__) && !defined(USE_OPENAL_SOFT_INCLUDES)
	#include <OpenAL/al.h>
	#include <OpenAL/alc.h>
#else
	#include <AL/al.h>
	#include <AL/alc.h>
	#include <AL/alext.h>
#endif

#include "OpenAL/AL_SoundSample.h"
#include "OpenAL/AL_SoundVoice.h"
#include "OpenAL/AL_SoundHardware.h"

ID_INLINE_EXTERN ALenum CheckALErrors_( const char* filename, int line )
{
	ALenum err = alGetError();
	if( err != AL_NO_ERROR )
	{
		idLib::Printf( "OpenAL Error: %s (0x%x), @ %s %d\n", alGetString( err ), err, filename, line );
	}
	return err;
}
#define CheckALErrors() CheckALErrors_(__FILE__, __LINE__)

ID_INLINE_EXTERN ALCenum CheckALCErrors_( ALCdevice* device, const char* filename, int linenum )
{
	ALCenum err = alcGetError( device );
	if( err != ALC_NO_ERROR )
	{
		idLib::Printf( "ALC Error: %s (0x%x), @ %s %d\n", alcGetString( device, err ), err, filename, linenum );
	}
	return err;
}
#define CheckALCErrors(x) CheckALCErrors_((x), __FILE__, __LINE__)

#elif defined(_MSC_VER) // DG: stub out xaudio for MinGW etc

#define OPERATION_SET 1

// RB: not available on Windows 8 SDK
#if defined(USE_WINRT) // (_WIN32_WINNT < 0x0602 /*_WIN32_WINNT_WIN8*/)
#include <mmdeviceapi.h>
#include <initguid.h> // For the pkey defines to be properly instantiated.
#include <propkeydef.h>
#include "functiondiscoverykeys_devpkey.h"
#include <string>
#include <vector>

DEFINE_PROPERTYKEY( PKEY_AudioEndpoint_Path, 0x9c119480, 0xddc2, 0x4954, 0xa1, 0x50, 0x5b, 0xd2, 0x40, 0xd4, 0x54, 0xad, 1 );

#pragma comment(lib,"xaudio2.lib")

struct AudioDevice
{
	std::wstring name;
	std::wstring id;
};
#else
#include <dxsdkver.h>
#endif
// RB end

#include <xaudio2.h>
#include <xaudio2fx.h>
#include <X3DAudio.h>

// RB: not available on Windows 8 SDK
#if !defined(USE_WINRT) // (_WIN32_WINNT < 0x0602 /*_WIN32_WINNT_WIN8*/)
	#include <xma2defs.h>
#endif
// RB end

#include "XAudio2/XA2_SoundSample.h"
#include "XAudio2/XA2_SoundVoice.h"
#include "XAudio2/XA2_SoundHardware.h"

#else // not _MSC_VER => MinGW, GCC, ...
// just a stub for now
#include "stub/SoundStub.h"
#endif // _MSC_VER ; DG end

//------------------------
// Listener data
//------------------------
struct listener_t
{
	idMat3	axis;		// orientation of the listener
	idVec3	pos;		// position in meters
	int		id;			// the entity number, used to detect when a sound is local
	int		area;		// area number the listener is in
};

class idSoundFade
{
public:
	int		fadeStartTime;
	int		fadeEndTime;
	float	fadeStartVolume;
	float	fadeEndVolume;


public:
	idSoundFade()
	{
		Clear();
	}

	void	Clear();
	void	SetVolume( float to );
	void	Fade( float to, int length, int soundTime );
	void	FadeDB( float toDB, float overSeconds, int soundTime );
	void	Sanitize();

	float	GetVolume( int soundTime ) const;
};

/*
================================================
idSoundChannel
================================================
*/
class idSoundChannel
{
public:
	bool	CanMute() const;
	bool	IsVoicePlayback() const;

	void	Mute();
	bool	CheckForCompletion( int currentTime );

	void	UpdateVolume( int currentTime );
	void	UpdateHardware( float volumeAdd, int currentTime );
	int		MixPrioritySortKey( int currentTime ) const;

	// returns true if this channel is marked as looping
	bool	IsLooping() const;

	// back to the freshly constructed state
	void	Reset();

	class idSoundEmitterLocal* 	emitter;

	int						startTime;
	int						endTime;
	int						lastFrequencyShiftTime;
	int						logicalChannel;
	int						choice;
	bool					allowSlow;

	soundShaderParms_t		parms;				// combines shader parms and per-channel overrides
	const idSoundShader* 	soundShader;
	idSoundSample* 			leadinSample;
	idSoundSample* 			loopingSample;
	idSoundFade				volumeFade;

	float					volumeDB;			// last volume at which this channel will play (calculated in UpdateVolume)
	float					currentAmplitude;	// the sample's envelope at the current playback position
	float					lastFrequencyShift;
	float					elapsedFrequencyShiftTime;

	// hardwareVoice will be freed and NULL'd when a sound is out of range,
	// and reallocated when it comes back in range
	idSoundVoice* 			hardwareVoice;

	// only allocated by the soundWorld block allocator
	idSoundChannel();
	~idSoundChannel();
};

// Maximum number of SoundChannels for a single SoundEmitter.
// This is probably excessive...
const int MAX_CHANNELS_PER_EMITTER = 16;

/*
===================================================================================

idSoundWorldLocal

===================================================================================
*/

class idSoundWorldLocal : public idSoundWorld
{
public:
	idSoundWorldLocal();
	virtual					~idSoundWorldLocal();

	//------------------------
	// Functions from idSoundWorld, implemented in SoundWorld.cpp
	//------------------------

	// Called at map start
	virtual void			ClearAllSoundEmitters();

	// stop all playing sounds
	virtual void			StopAllSounds();

	// get a new emitter that can play sounds in this world
	virtual idSoundEmitter* AllocSoundEmitter();

	// for load games
	virtual idSoundEmitter* EmitterForIndex( int index );
	// a render demo's emitter index, where 0 is localSound
	idSoundEmitter*		DemoEmitterForIndex( int index );

	// query data from all emitters in the world
	virtual float			CurrentShakeAmplitude();
	float					ShakeAmplitudeForPosition( const idVec3& listenerPosition );
	float					CurrentRumbleAmplitude() const { return rumbleAmp; }
	virtual int				GetSubtitles( soundSubtitle_t* cues, int capacity );

	// where is the camera
	virtual void			PlaceListener( const idVec3& origin, const idMat3& axis, const int listenerId );

	virtual void			WriteSoundShaderLoad( const idSoundShader* snd );

	// fade all sounds in the world with a given shader soundClass
	// to is in Db, over is in seconds
	virtual void			FadeSoundClasses( const int soundClass, const float to, const float over );

	// dumps the current state and begins archiving commands
	virtual void			StartWritingDemo( idDemoFile* demo );
	virtual void			StopWritingDemo();

	// read a sound command from a demo file
	virtual bool			ProcessDemoCommand( idDemoFile* readDemo );

	// menu sounds
	virtual int				PlayShaderDirectly( const char* name, int channel = -1 );

	virtual void			Skip( int time );

	virtual void			Pause();
	virtual void			UnPause();
	virtual bool			IsPaused()
	{
		return isPaused;
	}

	virtual int				GetSoundTime();

	// avidump
	virtual void			AVIOpen( const char* path, const char* name );
	virtual void			AVIClose();

	// SaveGame Support
	virtual void			WriteToSaveGame( idFile* savefile );
	virtual void			ReadFromSaveGame( idFile* savefile );

	virtual void			SetSlowmoSpeed( float speed );
	virtual void			SetEnviroSuit( bool active );
	virtual void			SetUnderwater( bool active );
	virtual void			SetLiquidTest( liquidTest_t test );

	//=======================================

	//------------------------
	// Random stuff that's not exposed outside the sound system
	//------------------------
	void			Update();
	void			OnReloadSound( const idDecl* decl );

	idSoundChannel* 	AllocSoundChannel();
	void				FreeSoundChannel( idSoundChannel* );

private:
	// Shared save/demo state decoder. demoFile is non-NULL only for the
	// non-fatal render-demo path.
	bool			ReadSoundState( idFile* savefile, idDemoFile* demoFile );

public:
	// even though all these variables are public, nobody outside the sound system includes SoundWorld_local.h
	// so this is equivalent to making it private and friending all the other classes in the sound system

	idSoundFade			volumeFade;						// master volume knob for the entire world
	idSoundFade			soundClassFade[SOUND_MAX_CLASSES];

	idRenderWorld* 		renderWorld;	// for debug visualization and light amplitude sampling
	idDemoFile* 		writeDemo;		// if not NULL, archive commands here

	float				currentCushionDB;	// channels at or below this level will be faded to 0
	int					lastCushionTime;	// sound time of the last cushion update, 0 = none yet
	float				shakeAmp;			// last calculated shake amplitude
	float				rumbleAmp;			// last calculated controller rumble amplitude

	listener_t			listener;
	idList<idSoundEmitterLocal*>	emitters;

	idSoundEmitter* 	localSound;			// for PlayShaderDirectly()

	idBlockAlloc<idSoundEmitterLocal, 16, 0>	emitterAllocator;
	idBlockAlloc<idSoundChannel, 16, 0>		channelAllocator;

	idSoundFade				pauseFade;
	int						pausedTime;
	int						accumulatedPauseTime;
	bool					isPaused;

	float					slowmoSpeed;
	bool					enviroSuitActive;
	bool					underwaterActive;
	liquidTest_t			liquidTest;

public:
	struct soundPortalTrace_t
	{
		int		portalArea;
		const soundPortalTrace_t* prevStack;
	};

	void			ResolveOrigin( const int stackDepth, const soundPortalTrace_t* prevStack, const int soundArea, const float dist, const int occludingPortals, const idVec3& soundOrigin, idSoundEmitterLocal* def );
};


/*
================================================
idSoundEmitterLocal
================================================
*/
class idSoundEmitterLocal : public idSoundEmitter
{
public:
	virtual void	Free( bool immediate );

	virtual void	Reset();

	virtual void	UpdateEmitter( const idVec3& origin, int listenerId, const soundShaderParms_t* parms );
	virtual void	UpdateEmitter( const idVec3& origin, const idVec3& velocity, int listenerId, const soundShaderParms_t* parms );

	virtual int		StartSound( const idSoundShader* shader, const s_channelType channel, float diversity = 0, int shaderFlags = 0, bool allowSlow = true );

	virtual void	ModifySound( const s_channelType channel, const soundShaderParms_t* parms );
	virtual void	StopSound( const s_channelType channel );

	virtual void	FadeSound( const s_channelType channel, float to, float over );

	virtual bool	CurrentlyPlaying( const s_channelType channel = SCHANNEL_ANY ) const;

	virtual	float	CurrentAmplitude();
	float			ShakeAmplitudeForPosition( const int currentTime, const idVec3& listenerPosition );
	virtual void	AmplitudeHistory( s_channelType channel, float *peaks, int count );

	virtual	int		Index() const;

	//----------------------------------------------

	void			Init( int i, idSoundWorldLocal* sw );

	// Returns true if the emitter should be freed.
	bool			CheckForCompletion( int currentTime );

	void			OverrideParms( const soundShaderParms_t* base, const soundShaderParms_t* over, soundShaderParms_t* out );

	void			Update( int currentTime );
	void			OnReloadSound( const idDecl* decl );

	//----------------------------------------------

	idSoundWorldLocal* 		soundWorld;						// the world that holds this emitter

	int			index;							// in world emitter list
	bool		canFree;						// if true, this emitter can be canFree (once channels.Num() == 0)

	// a single soundEmitter can have many channels playing from the same point
	idStaticList<idSoundChannel*, MAX_CHANNELS_PER_EMITTER> channels;

	//----- set by UpdateEmitter -----
	idVec3				origin;
	idVec3				velocity;
	soundShaderParms_t	parms;
	int					emitterId;						// sounds will be full volume when emitterId == listenerId

	//----- set by Update -----
	int			lastValidPortalArea;
	float		directDistance;
	float		spatializedDistance;
	idVec3		spatializedOrigin;
	int			occludingPortalCount;
	bool		crossesLiquidBoundary;	// openQ4: emitter and listener are in different media

	// The shaders and samples this emitter started most recently. Retail's fixed channel
	// slots kept a finished sound's sample, so no_dups also skipped the variant that had
	// just ended; openQ4 frees finished channels and needs to remember it here.
	static const int	RECENT_SAMPLES = 8;
	const idSoundShader*	recentShaders[ RECENT_SAMPLES ];
	const idSoundSample*	recentSamples[ RECENT_SAMPLES ];
	int					recentNext;

	// sound emitters are only allocated by the soundWorld block allocator
	idSoundEmitterLocal();
	virtual			~idSoundEmitterLocal();
};


/*
================================================
idSoundReverb

Retail Quake 4 environmental reverb (Quake4.exe 1.4.2): the efxs/<name>.efx preset
file, the map's maps/<map>.reverb area table, and the four reverb slots that
follow the listener's area and the three areas nearest it through its portals.
Slot planning and the EAX to EFX arithmetic live in SoundReverbCore.h.
================================================
*/
class idSoundReverb
{
public:
						idSoundReverb();

	// retail idSoundSystemLocal::EndLevelLoad: efxs/<map>.efx, else efxs/default.efx,
	// then <map>.reverb against the areas of the game render world
	void				LoadLevel( const char* mapName, const idRenderWorld* renderWorld );
	void				Clear();

	// once per sound world update, before the voices take their sends
	void				Update( idSoundWorldLocal* world );

	// the reverb slot holding an area, -1 when no slot holds it
	int					SlotForArea( int area ) const;

	// retail reverb editor interface
	const char*			GetReverbName( int reverb ) const;
	int					GetNumAreas() const;
	int					GetReverb( int area ) const;
	bool				SetReverb( int area, const char* reverbName, const char* fileName );

	void				PrintInfo() const;

private:
	struct effect_t
	{
		idStr				name;
		soundReverbEAX_t	properties;
	};

	idList<effect_t>	effects;				// efxs/*.efx in file order
	bool				efxLoaded;
	idStr				efxFileName;
	idList<int>			areaReverbs;			// effect index per portal area
	idStr				reverbFileName;			// empty when the map has no table
	int					slotToArea[SOUND_REVERB_SLOTS];
	int					numSlots;				// slots the hardware provided at the last update
	int					primarySlot;
	unsigned int		slotGeneration;
	idList<soundReverbPortal_t>	portals;		// scratch, the listener area's portals
	idStr				lastShownState;			// s_showReverb

	bool				LoadEffectFile( const char* fileName );
	bool				ReadEffect( idLexer& src, effect_t& effect );
	void				LoadAreaTable( const char* mapName, int numAreas );
	int					FindEffect( const char* name ) const;
	void				ResetSlots();
	void				ShowState( const listener_t& listener );
	static int			EffectForArea( void* context, int area );
};


/*
===================================================================================

idSoundSystemLocal

===================================================================================
*/
class idSoundSystemLocal : public idSoundSystem
{
public:
	// all non-hardware initialization
	virtual void			Init();

	// shutdown routine
	virtual	void			Shutdown();

	virtual idSoundWorld* 	AllocSoundWorld( idRenderWorld* rw );
	virtual void			FreeSoundWorld( idSoundWorld* sw );

	// specifying NULL will cause silence to be played
	virtual void			SetPlayingSoundWorld( idSoundWorld* soundWorld );

	// some tools, like the sound dialog, may be used in both the game and the editor
	// This can return NULL, so check!
	virtual idSoundWorld* 	GetPlayingSoundWorld();

	virtual idSoundWorld* GetSoundWorldFromId(int worldId);
	virtual float			CurrentShakeAmplitudeForPosition( int worldId, const int time, const idVec3& listenerPosition );
	virtual void			FreeSoundEmitter(int worldId, int handle, bool immediate);

	// sends the current playing sound world information to the sound hardware
	virtual void			Render();

	// Mutes the SSG_MUSIC group
	virtual void			MuteBackgroundMusic( bool mute )
	{
		musicMuted = mute;
	}

	// sets the final output volume to 0
	// This should only be used when the app is deactivated
	// Since otherwise there will be problems with different subsystems muting and unmuting at different times
	virtual void			SetMute( bool mute )
	{
		muted = mute;
	}
	virtual bool			IsMuted()
	{
		return muted;
	}

	virtual void			OnReloadSound( const idDecl* sound );

	virtual void			StopAllSounds();

	virtual void			InitStreamBuffers();
	virtual void			FreeStreamBuffers();

	virtual void* 			GetIXAudio2() const; // FIXME: stupid name; get rid of this? not sure if it's really needed..

	// RB begin
	virtual void*			GetOpenALDevice() const;
	// RB end
	virtual int				IsEAXAvailable() const;

	// for the sound level meter window
	virtual cinData_t		ImageForTime( const int milliseconds, const bool waveform );

	// Free all sounds loaded during the last map load
	virtual	void			BeginLevelLoad();

	// We might want to defer the loading of new sounds to this point
	virtual	void			EndLevelLoad( const char* mapName );

	// prints memory info
	virtual void			PrintMemInfo( MemInfo_t* mi );

	// retail reverb editor interface
	virtual const char*		GetReverbName( int reverb );
	virtual int				GetNumAreas();
	virtual int				GetReverb( int area );
	virtual bool			SetReverb( int area, const char* reverbName, const char* fileName );

	virtual void			ShutdownHW();
	virtual bool			UpdateVoiceChat( bool active, bool transmit, bool localTest );
	virtual int				GetVoiceData( byte* data, int capacity );
	virtual void			PlayVoiceData( int clientNum, unsigned int generation, const byte* data, int size );
	virtual void			StopVoiceData( int clientNum );
	virtual void			ResetVoiceChat();
	virtual int				GetVoiceTalker() const;
	virtual float			GetVoiceLevel() const;

	//-------------------------

	// Before a sound is reloaded, any active voices using it must
	// be stopped.  Returns true if any were playing, and should be
	// restarted after the sound is reloaded.
	void					StopVoicesWithSample( const idSoundSample* const sample );

	void					Restart();
	void					SetNeedsRestart()
	{
		needsRestart = true;
	}

	int						SoundTime() const;
	// Brings the sound clock, otherwise advanced once per mixed frame, up to now: a
	// pause and an unpause inside one long blocking step such as a level load would
	// both read the frame before it and hold the clock for none of that time.
	int						RefreshSoundTime();

	// may return NULL if there are no more voices left
	idSoundVoice* 			AllocateVoice( const idSoundSample* leadinSample, const idSoundSample* loopingSample );
	void					FreeVoice( idSoundVoice* );

	idSoundSample* 			LoadSample( const char* name );

	struct bufferContext_t
	{
		bufferContext_t() :
			voice( NULL ),
			sample( NULL ),
			bufferNumber( 0 )
		{ }

#if defined(USE_OPENAL)
		idSoundVoice_OpenAL* 	voice;
		idSoundSample_OpenAL*	sample;
#elif defined(_MSC_VER) // XAudio backend
		// DG: because the inheritance is kinda strange (idSoundVoice is derived
		// from idSoundVoice_XAudio2), casting the latter to the former isn't possible
		// so we need this ugly #ifdef ..
		idSoundVoice_XAudio2* 	voice;
		idSoundSample_XAudio2* sample;
#else // not _MSC_VER
		// from stub or something..
		idSoundVoice* 	voice;
		idSoundSample* sample;
#endif // _MSC_VER ; DG end

		int bufferNumber;
	};

	// Get a stream buffer from the free pool, returns NULL if none are available
	bufferContext_t* 			ObtainStreamBufferContext();
	void						ReleaseStreamBufferContext( bufferContext_t* p );

//	idSysMutex					streamBufferMutex;
	idStaticList< bufferContext_t*, MAX_SOUND_BUFFERS > freeStreamBufferContexts;
	idStaticList< bufferContext_t*, MAX_SOUND_BUFFERS > activeStreamBufferContexts;
	idStaticList< bufferContext_t, MAX_SOUND_BUFFERS > bufferContexts;

	idSoundWorldLocal* 			currentSoundWorld;
	idStaticList<idSoundWorldLocal*, 32>	soundWorlds;

	idList<idSoundSample*>		samples;
	idHashIndex					sampleHash;

	idSoundHardware				hardware;

	idSoundReverb				reverb;

	idRandom2					random;

	int							soundTime;
	bool						muted;
	bool						musicMuted;
	bool						needsRestart;

	bool						insideLevelLoad;
	// samples whose PCM is decoding on a job worker (LoadResourceAsync)
	idList<idSoundSample*>		decodingSamples;
	// uploads the samples whose decode has finished, or all of them, waiting
	void						FinishSampleDecodes( bool all );
	// frees the audio a low-memory notice may take (SoundMemoryTrim.h); idleSeconds < 0
	// keeps the current level's samples
	void						TrimMemory( const char* reason, int idleSeconds );

	//-------------------------

	idSoundSystemLocal() :
		currentSoundWorld( NULL ),
		soundTime( 0 ),
		muted( false ),
		musicMuted( false ),
		needsRestart( false )
	{}
};

extern	idSoundSystemLocal	soundSystemLocal;

#endif /* !__SND_LOCAL_H__ */
