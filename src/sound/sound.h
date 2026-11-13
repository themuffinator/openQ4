/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.

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

#ifndef __SOUND__
#define __SOUND__

#define SOUNDWORLD_ANY		-1
#define SOUNDWORLD_NONE		0
#define SOUNDWORLD_GAME		1
#define SOUNDWORLD_MENU		2
#define SOUNDWORLD_EDITOR	3
#define SOUNDWORLD_MAX		4

/*
===============================================================================

	SOUND SHADER DECL

===============================================================================
*/

// unfortunately, our minDistance / maxDistance is specified in meters, and
// we have far too many of them to change at this time.
// 
// jmarshall - quake 4 doesn't use these
const float DOOM_TO_METERS = 1.0f;					// doom to meters
const float METERS_TO_DOOM = 1.0f;	// meters to doom
// jmarshall end

const float DB_SILENCE = -60.0f;

class idSoundSample;

// sound shader flags
static const int	SSF_PRIVATE_SOUND =		BIT( 0 );	// only plays for the current listenerId
static const int	SSF_ANTI_PRIVATE_SOUND = BIT( 1 );	// plays for everyone but the current listenerId
static const int	SSF_NO_OCCLUSION =		BIT( 2 );	// don't flow through portals, only use straight line
static const int	SSF_GLOBAL =			BIT( 3 );	// play full volume to all speakers and all listeners
static const int	SSF_OMNIDIRECTIONAL =	BIT( 4 );	// fall off with distance, but play same volume in all speakers
static const int	SSF_LOOPING =			BIT( 5 );	// repeat the sound continuously
static const int	SSF_PLAY_ONCE =			BIT( 6 );	// never restart if already playing on any channel of a given emitter
static const int	SSF_UNCLAMPED =			BIT( 7 );	// don't clamp calculated volumes at 1.0
static const int	SSF_NO_FLICKER =		BIT( 8 );	// always return 1.0 for volume queries
static const int	SSF_NO_DUPS =			BIT( 9 );	// try not to play the same sound twice in a row

// RAVEN BEGIN
static const int	SSF_USEDOPPLER = BIT(10);	// allow doppler pitch shifting effects
static const int	SSF_NO_RANDOMSTART = BIT(11);	// don't offset the start position for looping sounds
static const int	SSF_VO_FOR_PLAYER = BIT(12);	// Notifies a funcRadioChatter that this shader is directed at the player
static const int	SSF_IS_VO = BIT(13);	// this sound is VO
static const int	SSF_CAUSE_RUMBLE = BIT(14);	// causes joystick rumble
static const int	SSF_CENTER = BIT(15);	// sound through center channel only
static const int	SSF_HILITE = BIT(16);	// display debug info for this emitter
// RAVEN END

// OpenQ4-only runtime classifications. Keep these out of the retail flag range
// because game modules and shipped Q4 data use bits 10/11 for Doppler/no-random-start.
static const int	SSF_VO =				BIT( 22 ); // VO - direct a portion of the sound through the center channel (set automatically on shaders that contain files that start with "sound/vo/")
static const int	SSF_MUSIC =				BIT( 23 ); // Music - Muted when the player is playing his own music

// these options can be overriden from sound shader defaults on a per-emitter and per-channel basis
typedef struct
{
	float					minDistance;
	float					maxDistance;
	float					volume;					// linear scale (1.0 = nominal)
	float					attenuatedVolume;		// Quake 4 archived field; retained for retail ABI/save-demo layout
	float					shakes;
	int						soundShaderFlags;		// SSF_* bit flags
	int						soundClass;				// for global fading of sounds

// RAVEN BEGIN
// bdube: frequency shift
// jmarshall: implement
	float					frequencyShift;
	float					wetLevel;
	float					dryLevel;
// RAVEN END	
} soundShaderParms_t;

// sound classes are used to fade most sounds down inside cinematics, leaving dialog
// flagged with a non-zero class full volume
const int		SOUND_MAX_CLASSES		= 4;
const int		SOUND_CLASS_MUSICAL		= 3;

// it is somewhat tempting to make this a virtual class to hide the private
// details here, but that doesn't fit easily with the decl manager at the moment.
class idSoundShader : public idDecl
{
public:
	idSoundShader();
	virtual					~idSoundShader();

	virtual size_t			Size() const;
	virtual bool			SetDefaultText();
	virtual const char* 	DefaultDefinition() const;
	virtual bool			Parse( const char* text, const int textLength ) override;
	virtual bool			Parse( const char* text, const int textLength, bool noCaching ) override;
	virtual void			FreeData();
	virtual void			List() const;
	virtual void			SetReferencedThisLevel() override;
	virtual bool			Validate( const char* psText, int iTextLength, idStr& strReportTo ) const override;
	virtual bool			RebuildTextSource() override;

// jmarshall: eval
	virtual bool			IsVO_ForPlayer(void) const;
// jmarshall end

	// so the editor can draw correct default sound spheres
	// this is currently defined as meters, which sucks, IMHO.
	virtual float			GetMinDistance() const;		// FIXME: replace this with a GetSoundShaderParms()
	virtual float			GetMaxDistance() const;

	// returns NULL if an AltSound isn't defined in the shader.
	// we use this for pairing a specific broken light sound with a normal light sound
	virtual const idSoundShader* GetAltSound() const;

	virtual bool			HasDefaultSound() const;

	virtual const soundShaderParms_t* GetParms() const;
	virtual int				GetNumSounds() const;
	virtual const char* 	GetSound( int index ) const;
	virtual float			GetTimeLength() const;

	float					GetLeadinVolume() const { return leadinVolume; }
	int						GetNumEntries() const { return entries.Num(); }
	idSoundSample*			GetLeadin( int index ) const { return ( index >= 0 && index < leadins.Num() ) ? leadins[index] : NULL; }
	idSoundSample*			GetEntry( int index ) const { return ( index >= 0 && index < entries.Num() ) ? entries[index] : NULL; }
	const char*				GetShakeData( int index ) const;
	void					SetShakeData( int index, const char* ampData );
	void					Purge( bool freeBaseBlocks );
	void					LoadSampleData( int langIndex = -1 );

	const char*				GetSampleName( int index ) const;
	int						GetSamplesPerSec( int index ) const;
	int						GetNumChannels( int index ) const;
	int						GetNumSamples( int index ) const;
	int						GetMemorySize( int index ) const;
	const byte*				GetNonCacheData( int index ) const;

	void					ExpandSmallOggs( bool force );

private:
	friend class idSoundWorldLocal;
	friend class idSoundEmitterLocal;
	friend class idSoundChannel;

	// options from sound shader text
	soundShaderParms_t		parms;			// can be overriden on a per-channel basis

	int						speakerMask;
	const idSoundShader* 	altSound;

	float					leadinVolume;	// allows light breaking leadin sounds to be much louder than the broken loop

	idList<idSoundSample*>	leadins;
	idList<idSoundSample*>	entries;
	idStrList				shakes;

	idStr					desc;
	float					minFrequencyShift;
	float					maxFrequencyShift;
	bool					noShakes;
	bool					frequentlyUsed;

private:
	void					Init();
	bool					ParseShader( idLexer& src );
	idSoundSample*			GetSampleByIndex( int index ) const;
};

class rvSoundShaderEdit {
public:
	virtual					~rvSoundShaderEdit() {}
	virtual const char*		GetSampleName( const idSoundShader* sound, int index ) const = 0;
	virtual int				GetSamplesPerSec( const idSoundShader* sound, int index ) const = 0;
	virtual int				GetNumChannels( const idSoundShader* sound, int index ) const = 0;
	virtual int				GetNumSamples( const idSoundShader* sound, int index ) const = 0;
	virtual int				GetMemorySize( const idSoundShader* sound, int index ) const = 0;
	virtual const byte*		GetNonCacheData( const idSoundShader* sound, int index ) const = 0;
	virtual void			LoadSampleData( idSoundShader* sound, int langIndex = -1 ) = 0;
	virtual void			ExpandSmallOggs( idSoundShader* sound, bool force ) = 0;
	virtual const char*		GetShakeData( idSoundShader* sound, int index ) = 0;
	virtual void			SetShakeData( idSoundShader* sound, int index, const char* ampData ) = 0;
	virtual void			Purge( idSoundShader* sound, bool freeBaseBlocks ) = 0;
};

extern rvSoundShaderEdit* soundShaderEdit;

/*
===============================================================================

	SOUND EMITTER

===============================================================================
*/

// sound channels
static const int SCHANNEL_ANY = 0;	// used in queries and commands to effect every channel at once, in
// startSound to have it not override any other channel
static const int SCHANNEL_ONE = 1;	// any following integer can be used as a channel number
typedef int s_channelType;	// the game uses its own series of enums, and we don't want to require casts


class idSoundEmitter
{
public:
	virtual					~idSoundEmitter() {}

	// a non-immediate free will let all currently playing sounds complete
	// soundEmitters are not actually deleted, they are just marked as
	// reusable by the soundWorld
	virtual void			Free( bool immediate ) = 0;

	// the parms specified will be the default overrides for all sounds started on this emitter.
	// NULL is acceptable for parms
	virtual void			UpdateEmitter( const idVec3& origin, int listenerId, const soundShaderParms_t* parms ) {
		UpdateEmitter( origin, vec3_zero, listenerId, parms );
	}
	virtual void			UpdateEmitter( const idVec3& origin, const idVec3& velocity, int listenerId, const soundShaderParms_t* parms ) = 0;

	// returns the length of the started sound in msec
	virtual int				StartSound( const idSoundShader* shader, const s_channelType channel, float diversity = 0, int shaderFlags = 0, bool allowSlow = true ) = 0;

	// pass SCHANNEL_ANY to effect all channels
	virtual void			ModifySound( const s_channelType channel, const soundShaderParms_t* parms ) = 0;
	virtual void			StopSound( const s_channelType channel ) = 0;
	// to is in Db, over is in seconds
	virtual void			FadeSound( const s_channelType channel, float to, float over ) = 0;

	// returns true if there are any sounds playing from this emitter.  There is some conservative
	// slop at the end to remove inconsistent race conditions with the sound thread updates.
	// FIXME: network game: on a dedicated server, this will always be false
	virtual bool			CurrentlyPlaying( const s_channelType channel = SCHANNEL_ANY ) const = 0;

	// returns a 0.0 to 1.0 value based on the current sound amplitude, allowing
	// graphic effects to be modified in time with the audio.
	// just samples the raw wav file, it doesn't account for volume overrides in the
	virtual	float			CurrentAmplitude() = 0;

	// Actual decoded sample peaks, oldest to newest, at 60 Hz ending at the
	// channel's playback cursor. Missing, stopped and silent audio is zero.
	// Pre-volume metering; never uses authored shake data or no_flicker.
	virtual void			AmplitudeHistory( s_channelType channel, float *peaks, int count ) = 0;

	// for save games.  Index will always be > 0
	virtual	int				Index() const = 0;
};

/*
===============================================================================

	SOUND WORLD

There can be multiple independent sound worlds, just as there can be multiple
independent render worlds.  The prime example is the editor sound preview
option existing simultaniously with a live game.
===============================================================================
*/

// A borrowed presentation snapshot, valid until the next decl/language reload
// or the next GetSubtitles call. Timing follows the sample clock, including pause
// and frequency/time shifts. text is NULL for speech without a transcript, which
// is reported only while ui_subtitleUntranscribed asks for it; authored marks text
// from a subtitle/ declaration rather than the installed lip-sync data.
struct soundSubtitle_t {
	const char* text;
	const char* shader;
	int emitterIndex, speakerId, startTime, timeMsec, durationMsec;
	bool radio;
	bool transcribed;
	bool authored;
};

class idSoundWorld
{
public:
	virtual					~idSoundWorld() {}

	// call at each map start
	virtual void			ClearAllSoundEmitters() = 0;
	virtual void			StopAllSounds() = 0;

	// get a new emitter that can play sounds in this world
	virtual idSoundEmitter* AllocSoundEmitter() = 0;

	// for load games, index 0 will return NULL
	virtual idSoundEmitter* EmitterForIndex( int index ) = 0;

	// query sound samples from all emitters reaching a given listener
	virtual float			CurrentShakeAmplitude() = 0;

	// where is the camera/microphone
	// listenerId allows listener-private and antiPrivate sounds to be filtered
	virtual void			PlaceListener( const idVec3& origin, const idMat3& axis, const int listenerId ) = 0;

	// fade all sounds in the world with a given shader soundClass
	// to is in Db, over is in seconds
	virtual void			FadeSoundClasses( const int soundClass, const float to, const float over ) = 0;

	// menu sounds
	virtual	int				PlayShaderDirectly( const char* name, int channel = -1 ) = 0;

	// dumps the current state and begins archiving commands
	virtual void			StartWritingDemo( idDemoFile* demo ) = 0;
	virtual void			StopWritingDemo() = 0;

	// read a sound command from a demo file
	// returns false when malformed or truncated input stops demo playback
	virtual bool			ProcessDemoCommand( idDemoFile* demo ) = 0;

	// when cinematics are skipped, we need to advance sound time this much
	virtual void			Skip( int time ) = 0;

	// pause and unpause the sound world
	virtual void			Pause() = 0;
	virtual void			UnPause() = 0;
	virtual bool			IsPaused() = 0;

	// Write the sound output to multiple wav files.  Note that this does not use the
	// work done by AsyncUpdate, it mixes explicitly in the foreground every PlaceOrigin(),
	// under the assumption that we are rendering out screenshots and the gameTime is going
	// much slower than real time.
	// path should not include an extension, and the generated filenames will be:
	// <path>_left.raw, <path>_right.raw, or <path>_51left.raw, <path>_51right.raw,
	// <path>_51center.raw, <path>_51lfe.raw, <path>_51backleft.raw, <path>_51backright.raw,
	// If only two channel mixing is enabled, the left and right .raw files will also be
	// combined into a stereo .wav file.
	virtual void			AVIOpen( const char* path, const char* name ) = 0;
	virtual void			AVIClose() = 0;

	// SaveGame / demo Support
	virtual void			WriteToSaveGame( idFile* savefile ) = 0;
	virtual void			ReadFromSaveGame( idFile* savefile ) = 0;

	virtual void			SetSlowmoSpeed( float speed ) = 0;
	virtual void			SetEnviroSuit( bool active ) = 0;

	// openQ4: muffles everything while the listener's head is under a liquid surface. Derived from
	// the eye position every frame, so it is deliberately not part of the savegame.
	virtual void			SetUnderwater( bool active ) = 0;

	// openQ4: lets the sound world ask whether a point is inside a liquid, which only the game
	// knows - liquid lives in the collision world, not the render world. Used to occlude sounds
	// that have to cross the surface to reach the listener. Called from the same per-emitter
	// spatialisation path that already does portal tracing, so it must be cheap and main-thread.
	typedef bool ( *liquidTest_t )( const idVec3 &point );
	virtual void			SetLiquidTest( liquidTest_t test ) = 0;
	virtual int				GetSubtitles( soundSubtitle_t* cues, int capacity ) = 0;
};


/*
===============================================================================

	SOUND SYSTEM

===============================================================================
*/

typedef struct
{
	idStr					name;
	idStr					format;
	int						numChannels;
	int						numSamplesPerSecond;
	int						num44kHzSamples;
	int						numBytes;
	bool					looping;
	float					lastVolume;
	int						start44kHzTime;
	int						current44kHzTime;
} soundDecoderInfo_t;


class idSoundSystem
{
public:
	virtual					~idSoundSystem() {}

	// All non-hardware initialization.
	virtual void			Init() = 0;

	// Shutdown routine.
	virtual	void			Shutdown() = 0;

	// The renderWorld is used for visualization and light amplitude sampling.
	virtual idSoundWorld* 	AllocSoundWorld( idRenderWorld* rw ) = 0;
	virtual void			FreeSoundWorld( idSoundWorld* sw ) = 0;

	// Specifying NULL will cause silence to be played.
	virtual void			SetPlayingSoundWorld( idSoundWorld* soundWorld ) = 0;

	// Some tools, like the sound dialog, may be used in both the game and the editor
	// This can return NULL, so check!
	virtual idSoundWorld* 	GetPlayingSoundWorld() = 0;

	// Sends the current playing sound world information to the sound hardware.
	virtual void			Render() = 0;

	virtual void			MuteBackgroundMusic( bool mute ) = 0;

	// Sets the final output volume to 0.
	virtual void			SetMute( bool mute ) = 0;
	virtual bool			IsMuted() = 0;

	// Called by the decl system when a sound decl is reloaded
	virtual void			OnReloadSound( const idDecl* sound ) = 0;

	// Called before freeing any sound sample resources
	virtual void			StopAllSounds() = 0;

	// May be called to free memory for level loads
	virtual void			InitStreamBuffers() = 0;
	virtual void			FreeStreamBuffers() = 0;

	// video playback needs to get this
	virtual void* 			GetIXAudio2() const = 0; // FIXME: stupid name if we have other backends

#if defined(USE_OPENAL)
	virtual void*			GetOpenALDevice() const = 0;
#endif
	virtual int				IsEAXAvailable() const = 0;

	// for the sound level meter window
	virtual cinData_t		ImageForTime( const int milliseconds, const bool waveform ) = 0;

	// Free all sounds loaded during the last map load
	virtual	void			BeginLevelLoad() = 0;

	// Load all sounds marked as used this level. mapName is the session's
	// "maps/<name>" path, which also selects the map's reverb tables.
	virtual	void			EndLevelLoad( const char* mapName ) = 0;

//	virtual void			Preload( idPreloadManifest& preload ) = 0;

	// prints memory info
	virtual void			PrintMemInfo( MemInfo_t* mi ) = 0;

// jmarshall: Quake 4 specific code
// RAVEN BEGIN
	// get a new emitter that can play sounds in this world
	virtual idSoundWorld* GetSoundWorldFromId(int worldId) = 0;

	// The sound worlds these helpers reach through belong to the session, which
	// allocates them in idSessionLocal::Init and drops them in Shutdown. Engine
	// teardown runs outside that window: idCommonLocal::FatalError shuts the
	// session down from anywhere in startup, so a renderer that fails to bring
	// up its device tears down before any world exists. A missing world is
	// therefore ordinary state, not a programming error, and matches the NULL
	// check idSoundSystemLocal::FreeSoundEmitter already makes on the same
	// accessor (issue #96).
	idSoundEmitter* EmitterForIndex(int worldId, int index) {
		idSoundWorld* soundWorld = GetSoundWorldFromId(worldId);
		if (soundWorld == NULL) {
			return NULL;
		}
		return soundWorld->EmitterForIndex(index);
	}

	void			FadeSoundClasses(int worldId, const int soundClass, const float to, const float over) {
		idSoundWorld* soundWorld = GetSoundWorldFromId(worldId);
		if (soundWorld == NULL) {
			return;
		}
		soundWorld->FadeSoundClasses(soundClass, to, over);
	}

	void			PlayShaderDirectly(int worldId, const char* name, int channel = -1) {
		idSoundWorld* soundWorld = GetSoundWorldFromId(worldId);
		if (soundWorld == NULL) {
			return;
		}
		soundWorld->PlayShaderDirectly(name, channel);
	}

	virtual int				AllocSoundEmitter(int worldId) {
		idSoundWorld* soundWorld = GetSoundWorldFromId(worldId);
		if (soundWorld == NULL) {
			// index 0 is the reserved "no emitter" handle; EmitterForIndex and
			// FreeSoundEmitter both already treat it as invalid
			return 0;
		}
		return soundWorld->AllocSoundEmitter()->Index();
	}
	virtual void			FreeSoundEmitter(int worldId, int handle, bool immediate) {

	}

	virtual void StopAllSounds(int worldId) {
		idSoundWorld* soundWorld = GetSoundWorldFromId(worldId);
		if (soundWorld == NULL) {
			return;
		}
		soundWorld->StopAllSounds();
	}

	virtual void SetActiveSoundWorld(bool val) {  }

	virtual void			WriteToSaveGame(int worldId, idFile* savefile) {
		idSoundWorld* soundWorld = GetSoundWorldFromId(worldId);
		if (soundWorld == NULL) {
			return;
		}
		soundWorld->WriteToSaveGame(savefile);
	}
	virtual void			ReadFromSaveGame(int worldId, idFile* savefile) {
		idSoundWorld* soundWorld = GetSoundWorldFromId(worldId);
		if (soundWorld == NULL) {
			return;
		}
		soundWorld->ReadFromSaveGame(savefile);
	}

	void			PlaceListener(const idVec3& origin, const idMat3& axis, const int listenerId, const int gameTime, const idStr& areaName) {
		idSoundWorld* soundWorld = GetSoundWorldFromId(SOUNDWORLD_GAME);
		if (soundWorld == NULL) {
			return;
		}
		soundWorld->PlaceListener(origin, axis, listenerId);
	}

	// Query authored camera shake at this position, independently of the audio mix.
	virtual	float			CurrentShakeAmplitudeForPosition( int worldId, const int time, const idVec3& listenerPosition ) = 0;

	// RAVEN END
// jmarshall end

	// Quake 4 reverb editor interface: the map's portal areas and the efxs/*.efx
	// reverb each one plays (see idSoundReverb).
	virtual	const char*		GetReverbName( int reverb ) = 0;
	virtual	int				GetNumAreas() = 0;
	virtual	int				GetReverb( int area ) = 0;
	virtual	bool			SetReverb( int area, const char* reverbName, const char* fileName ) = 0;

	// Releases the audio device and every sample's audio data and sets s_noSound,
	// for a client that turns into a dedicated server (SpawnServer_f). Unlike
	// Shutdown() it keeps every sample object, because sound shaders keep
	// pointers to them; samples loaded later become silent defaults, as on a
	// dedicated server. Retail Quake4.exe 1.4.2 calls ShutdownHW there.
	virtual void			ShutdownHW() = 0;

	// Voice is session-owned, independent of map audio/time scale. Recording
	// additionally requires explicit capture consent and push-to-talk (or a
	// local microphone test); merely receiving never opens an input device.
	virtual bool			UpdateVoiceChat( bool active, bool transmit, bool localTest ) = 0;
	virtual int				GetVoiceData( byte* data, int capacity ) = 0;
	virtual void			PlayVoiceData( int clientNum, unsigned int generation, const byte* data, int size ) = 0;
	virtual void			StopVoiceData( int clientNum ) = 0;
	virtual void			ResetVoiceChat() = 0;
	virtual int				GetVoiceTalker() const = 0;
	virtual float			GetVoiceLevel() const = 0;
};

extern idSoundSystem*	soundSystem;

#define OPENQ4_SOUND_HAS_SOUNDWORLD_SKIP 1

#endif /* !__SOUND__ */
