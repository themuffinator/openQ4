/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2014-2016 Robert Beckebans
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
#include "snd_local.h"

static const int SOUND_SAVEGAME_MAX_EMITTERS = 8192;
static const int SOUND_SAVEGAME_MAX_TOTAL_CHANNELS = 8192;
static const int SOUND_DEMO_MAX_CACHED_SOUNDS = 4096;

namespace
{

static bool StopMalformedSoundDemo( idDemoFile* demo, const char* commandName, const char* detail )
{
	if( demo != NULL && demo->IsOpen() )
	{
		demo->Close();
	}
	common->Warning( "Malformed render demo: %s %s; playback stopped safely",
		commandName != NULL ? commandName : "sound command",
		detail != NULL ? detail : "is invalid" );
	return false;
}

static const char* SoundDemoCommandName( int command )
{
	switch( command )
	{
		case SCMD_STATE:				return "SCMD_STATE";
		case SCMD_PLACE_LISTENER:	return "SCMD_PLACE_LISTENER";
		case SCMD_ALLOC_EMITTER:		return "SCMD_ALLOC_EMITTER";
		case SCMD_FREE:				return "SCMD_FREE";
		case SCMD_UPDATE:			return "SCMD_UPDATE";
		case SCMD_START:				return "SCMD_START";
		case SCMD_MODIFY:			return "SCMD_MODIFY";
		case SCMD_STOP:				return "SCMD_STOP";
		case SCMD_FADE:				return "SCMD_FADE";
		case SCMD_CACHESOUNDSHADER:	return "SCMD_CACHESOUNDSHADER";
		default:					return "sound command";
	}
}

class idSoundDemoCommandReader
{
public:
	idSoundDemoCommandReader( idDemoFile* demoFile, const char* command ) :
		demo( demoFile ),
		commandName( command )
	{
	}

	bool ReadInt( int& value, const char* fieldName )
	{
		return ReadChecked( demo->ReadInt( value ), sizeof( value ), fieldName );
	}

	bool ReadFloat( float& value, const char* fieldName )
	{
		return ReadChecked( demo->ReadFloat( value ), sizeof( value ), fieldName );
	}

	bool ReadVec3( idVec3& value, const char* fieldName )
	{
		return ReadChecked( demo->ReadVec3( value ), sizeof( value ), fieldName );
	}

	bool ReadMat3( idMat3& value, const char* fieldName )
	{
		return ReadChecked( demo->ReadMat3( value ), sizeof( value ), fieldName );
	}

	bool ReadHashString( idStr& value, const char* fieldName )
	{
		const char* stringValue = demo->ReadHashString();
		if( !demo->IsOpen() )
		{
			// idDemoFile::ReadHashString already closed the file and reported the
			// malformed hash-table entry.
			return false;
		}
		if( stringValue == NULL )
		{
			return Fail( va( "contains a NULL %s", fieldName ) );
		}
		value = stringValue;
		return true;
	}

	bool Fail( const char* detail )
	{
		return StopMalformedSoundDemo( demo, commandName, detail );
	}

private:
	bool ReadChecked( int bytesRead, int expectedBytes, const char* fieldName )
	{
		if( bytesRead == expectedBytes )
		{
			return true;
		}
		return Fail( va( "has a truncated %s (read %d of %d bytes)",
			fieldName, bytesRead, expectedBytes ) );
	}

	idDemoFile*	demo;
	const char*	commandName;
};

static bool ReadDemoShaderParms( idSoundDemoCommandReader& reader, soundShaderParms_t& parms )
{
	if( !reader.ReadFloat( parms.minDistance, "minimum distance" ) ||
		!reader.ReadFloat( parms.maxDistance, "maximum distance" ) ||
		!reader.ReadFloat( parms.volume, "volume" ) ||
		!reader.ReadFloat( parms.attenuatedVolume, "attenuated volume" ) ||
		!reader.ReadFloat( parms.shakes, "shake amplitude" ) ||
		!reader.ReadInt( parms.soundShaderFlags, "sound shader flags" ) ||
		!reader.ReadInt( parms.soundClass, "sound class" ) ||
		!reader.ReadFloat( parms.frequencyShift, "frequency shift" ) ||
		!reader.ReadFloat( parms.wetLevel, "wet level" ) ||
		!reader.ReadFloat( parms.dryLevel, "dry level" ) )
	{
		return false;
	}
	if( parms.soundClass < 0 || parms.soundClass >= SOUND_MAX_CLASSES )
	{
		return reader.Fail( va( "contains invalid sound class %d", parms.soundClass ) );
	}
	return true;
}

class idSoundStateReader
{
public:
	idSoundStateReader( idFile* sourceFile, idDemoFile* sourceDemo ) :
		file( sourceFile ),
		demo( sourceDemo )
	{
	}

	bool ReadInt( int& value, const char* fieldName )
	{
		return ReadChecked( file->ReadInt( value ), sizeof( value ), fieldName );
	}

	bool ReadFloat( float& value, const char* fieldName )
	{
		return ReadChecked( file->ReadFloat( value ), sizeof( value ), fieldName );
	}

	bool ReadBool( bool& value, const char* fieldName )
	{
		return ReadChecked( file->ReadBool( value ), sizeof( byte ), fieldName );
	}

	bool ReadVec3( idVec3& value, const char* fieldName )
	{
		return ReadChecked( file->ReadVec3( value ), sizeof( value ), fieldName );
	}

	bool ReadMat3( idMat3& value, const char* fieldName )
	{
		return ReadChecked( file->ReadMat3( value ), sizeof( value ), fieldName );
	}

	bool ReadString( idStr& value, const char* fieldName )
	{
		int length = 0;
		if( !ReadInt( length, fieldName ) )
		{
			return false;
		}

		if( length < 0 || length > MAX_STRING_CHARS )
		{
			return Fail( va( "contains invalid %s length %d", fieldName, length ) );
		}
		if( demo == NULL )
		{
			const int remainingBytes = Max( 0, file->Length() - file->Tell() );
			if( length > remainingBytes )
			{
				return Fail( va( "contains invalid %s length %d (remaining %d)",
					fieldName, length, remainingBytes ) );
			}
		}

		value.Clear();
		if( length == 0 )
		{
			return true;
		}
		value.Fill( ' ', length );
		const int bytesRead = file->Read( &value[0], length );
		if( bytesRead != length )
		{
			return Fail( va( "has a truncated %s payload (read %d of %d bytes)",
				fieldName, bytesRead, length ) );
		}
		return true;
	}

	bool ReadSoundFade( idSoundFade& fade, int timeDelta )
	{
		if( !ReadInt( fade.fadeStartTime, "sound fade start time" ) ||
			!ReadInt( fade.fadeEndTime, "sound fade end time" ) ||
			!ReadFloat( fade.fadeStartVolume, "sound fade start volume" ) ||
			!ReadFloat( fade.fadeEndVolume, "sound fade end volume" ) )
		{
			return false;
		}
		fade.Sanitize();
		if( fade.fadeEndTime > 0 )
		{
			if( !AddTimeDelta( fade.fadeStartTime, timeDelta, "sound fade start time" ) ||
				!AddTimeDelta( fade.fadeEndTime, timeDelta, "sound fade end time" ) )
			{
				return false;
			}
		}
		return true;
	}

	bool ReadShaderParms( soundShaderParms_t& parms )
	{
		if( !ReadFloat( parms.minDistance, "sound min distance" ) ||
			!ReadFloat( parms.maxDistance, "sound max distance" ) ||
			!ReadFloat( parms.volume, "sound volume" ) ||
			!ReadFloat( parms.attenuatedVolume, "sound attenuated volume" ) ||
			!ReadFloat( parms.shakes, "sound shakes" ) ||
			!ReadInt( parms.soundShaderFlags, "sound shader flags" ) ||
			!ReadInt( parms.soundClass, "sound class" ) ||
			!ReadFloat( parms.frequencyShift, "sound frequency shift" ) ||
			!ReadFloat( parms.wetLevel, "sound wet level" ) ||
			!ReadFloat( parms.dryLevel, "sound dry level" ) )
		{
			return false;
		}
		if( parms.soundClass < 0 || parms.soundClass >= SOUND_MAX_CLASSES )
		{
			return Fail( va( "contains invalid sound class %d", parms.soundClass ) );
		}
		return true;
	}

	bool AddTimeDelta( int& value, int timeDelta, const char* fieldName )
	{
		const int64 adjusted = static_cast<int64>( value ) + static_cast<int64>( timeDelta );
		if( adjusted < idMath::INT_MIN || adjusted > idMath::INT_MAX )
		{
			return Fail( va( "contains overflowing %s", fieldName ) );
		}
		value = static_cast<int>( adjusted );
		return true;
	}

	bool Fail( const char* detail )
	{
		if( demo != NULL )
		{
			return StopMalformedSoundDemo( demo, "SCMD_STATE", detail );
		}
		common->Error( "idSoundWorldLocal::ReadFromSaveGame: %s", detail );
		return false;
	}

private:
	bool ReadChecked( int bytesRead, int expectedBytes, const char* fieldName )
	{
		if( bytesRead == expectedBytes )
		{
			return true;
		}
		return Fail( va( "has a truncated %s (read %d of %d bytes)",
			fieldName, bytesRead, expectedBytes ) );
	}

	idFile*		file;
	idDemoFile*	demo;
};

}

idCVar s_lockListener( "s_lockListener", "0", CVAR_BOOL, "lock listener updates" );
idCVar s_constantAmplitude( "s_constantAmplitude", "-1", CVAR_FLOAT, "" );
idCVar s_maxEmitterChannels( "s_maxEmitterChannels", "48", CVAR_INTEGER | CVAR_ARCHIVE, "Can be set lower than the absolute max of MAX_HARDWARE_VOICES" );
idCVar s_cushionFadeChannels( "s_cushionFadeChannels", "2", CVAR_INTEGER, "Ramp currentCushionDB so this many emitter channels should be silent" );
idCVar s_cushionFadeRate( "s_cushionFadeRate", "60", CVAR_FLOAT, "DB / second change to currentCushionDB" );
idCVar s_cushionFadeLimit( "s_cushionFadeLimit", "-30", CVAR_FLOAT, "Never cushion fade beyond this level" );
idCVar s_cushionFadeOver( "s_cushionFadeOver", "10", CVAR_FLOAT, "DB above s_cushionFadeLimit to start ramp to silence" );
idCVar s_unpauseFadeInTime( "s_unpauseFadeInTime", "250", CVAR_INTEGER, "When unpausing a sound world, milliseconds to fade sounds in over" );
idCVar s_doorDistanceAdd( "s_doorDistanceAdd", "150", CVAR_FLOAT, "reduce sound volume with this distance when going through a door" );
idCVar s_quadraticFalloff( "s_quadraticFalloff", "1", CVAR_ARCHIVE | CVAR_BOOL, "use quadratic sound distance falloff" );
idCVar s_drawSounds( "s_drawSounds", "0", CVAR_INTEGER, "", 0, 2, idCmdSystem::ArgCompletion_Integer<0, 2> );
idCVar s_showVoices( "s_showVoices", "0", CVAR_BOOL, "show active voices" );
idCVar s_volume_dB( "s_volume_dB", "0", CVAR_ARCHIVE | CVAR_FLOAT, "volume in dB" );
extern idCVar s_noSound;

extern void WriteDeclCache( idDemoFile* f, int demoCategory, int demoCode, declType_t  declType );

/*
========================
idSoundWorldLocal::idSoundWorldLocal
========================
*/
idSoundWorldLocal::idSoundWorldLocal()
{
	volumeFade.Clear();
	for( int i = 0; i < SOUND_MAX_CLASSES; i++ )
	{
		soundClassFade[i].Clear();
	}
	renderWorld = NULL;
	writeDemo = NULL;

	listener.axis.Identity();
	listener.pos.Zero();
	listener.id = -1;
	listener.area = 0;

	shakeAmp = 0.0f;
	rumbleAmp = 0.0f;
	currentCushionDB = DB_SILENCE;

	localSound = AllocSoundEmitter();

	pauseFade.Clear();
	pausedTime = 0;
	accumulatedPauseTime = 0;
	isPaused = false;

	slowmoSpeed = 1.0f;
	enviroSuitActive = false;
	underwaterActive = false;
	liquidTest = NULL;
}

/*
========================
idSoundWorldLocal::~idSoundWorldLocal
========================
*/
idSoundWorldLocal::~idSoundWorldLocal()
{

	if( soundSystemLocal.currentSoundWorld == this )
	{
		soundSystemLocal.currentSoundWorld = NULL;
	}

	for( int i = 0; i < emitters.Num(); i++ )
	{
		emitters[i]->Reset();
		emitterAllocator.Free( emitters[i] );
	}

	// Make sure we aren't leaking emitters or channels
	assert( emitterAllocator.GetAllocCount() == 0 );
	assert( channelAllocator.GetAllocCount() == 0 );

	emitterAllocator.Shutdown();
	channelAllocator.Shutdown();

	renderWorld = NULL;
	localSound = NULL;
}

/*
========================
idSoundWorldLocal::AllocSoundEmitter

This is called from the main thread.
========================
*/
idSoundEmitter* idSoundWorldLocal::AllocSoundEmitter()
{
	for( int i = 1; i < emitters.Num(); i++ )
	{
		idSoundEmitterLocal* emitter = emitters[i];
		if( emitter != NULL && emitter->canFree && emitter->channels.Num() == 0 )
		{
			emitter->Reset();
			return emitter;
		}
	}

	idSoundEmitterLocal* emitter = emitterAllocator.Alloc();
	emitter->Init( emitters.Append( emitter ), this );
	return emitter;
}

/*
========================
idSoundWorldLocal::AllocSoundChannel
========================
*/
idSoundChannel* idSoundWorldLocal::AllocSoundChannel()
{
	return channelAllocator.Alloc();
}

/*
========================
idSoundWorldLocal::FreeSoundChannel
========================
*/
void idSoundWorldLocal::FreeSoundChannel( idSoundChannel* channel )
{
	channel->Mute();
	channelAllocator.Free( channel );
}

/*
========================
idSoundWorldLocal::CurrentShakeAmplitude
========================
*/
float idSoundWorldLocal::CurrentShakeAmplitude()
{
	if( s_constantAmplitude.GetFloat() >= 0.0f )
	{
		return s_constantAmplitude.GetFloat();
	}
	return shakeAmp;
}

/*
========================
idSoundWorldLocal::PlaceListener
========================
*/
void idSoundWorldLocal::PlaceListener( const idVec3& origin, const idMat3& axis, const int id )
{
	if( writeDemo )
	{
		writeDemo->WriteInt( DS_SOUND );
		writeDemo->WriteInt( SCMD_PLACE_LISTENER );
		writeDemo->WriteVec3( origin );
		writeDemo->WriteMat3( axis );
		writeDemo->WriteInt( id );
	}

	if( s_lockListener.GetBool() )
	{
		return;
	}

	listener.axis = axis;
	listener.pos = origin;
	listener.id = id;

	if( renderWorld )
	{
		listener.area = renderWorld->PointInArea( origin );	// where are we?
	}
	else
	{
		listener.area = 0;
	}
}

/*
========================
idSoundWorldLocal::WriteSoundShaderLoad
========================
*/
void idSoundWorldLocal::WriteSoundShaderLoad( const idSoundShader* snd )
{
	if( writeDemo )
	{
		writeDemo->WriteInt( DS_SOUND );
		writeDemo->WriteInt( SCMD_CACHESOUNDSHADER );
		writeDemo->WriteInt( 1 );
		writeDemo->WriteHashString( snd->GetName() );
	}
}

/*
========================
idActiveChannel
========================
*/
class idActiveChannel
{
public:
	idActiveChannel() :
		channel( NULL ),
		sortKey( 0 ) {}
	idActiveChannel( idSoundChannel* channel_, int sortKey_ ) :
		channel( channel_ ),
		sortKey( sortKey_ ) {}

	idSoundChannel* 	channel;
	int					sortKey;
};

static const int MAX_ACTIVE_EMITTER_CHANNEL_CANDIDATES = MAX_HARDWARE_VOICES + 1;

/*
========================
MapVolumeFromFadeDB

Ramp down volumes that are close to fadeDB so that fadeDB is DB_SILENCE
========================
*/
float MapVolumeFromFadeDB( const float volumeDB, const float fadeDB )
{
	if( FLOAT_IS_NAN( volumeDB ) || FLOAT_IS_NAN( fadeDB ) )
	{
		return DB_SILENCE;
	}
	if( volumeDB <= fadeDB )
	{
		return DB_SILENCE;
	}

	const float fadeOver = s_cushionFadeOver.GetFloat();
	if( FLOAT_IS_NAN( fadeOver ) || fadeOver <= 0.0f )
	{
		return volumeDB;
	}
	const float fadeFrom = fadeDB + fadeOver;

	if( volumeDB >= fadeFrom )
	{
		// unchanged
		return volumeDB;
	}
	const float fadeFraction = ( volumeDB - fadeDB ) / fadeOver;

	const float mappedDB = DB_SILENCE + ( fadeFrom - DB_SILENCE ) * fadeFraction;
	return mappedDB;
}

/*
========================
AdjustForCushionChannels

In the very common case of having more sounds that would contribute to the
mix than there are available hardware voices, it can be an audible discontinuity
when a channel initially gets a voice or loses a voice.
To avoid this, make sure that the last few hardware voices are mixed with a volume
of zero, so they won't make a difference as they come and go.
It isn't obvious what the exact best volume ramping method should be, just that
it smoothly change frame to frame.
========================
*/
static float AdjustForCushionChannels( const idStaticList< idActiveChannel, MAX_ACTIVE_EMITTER_CHANNEL_CANDIDATES >& activeEmitterChannels,
									   const int uncushionedChannels, const float currentCushionDB, const float driftRate )
{

	const int firstCushionedChannel = idMath::ClampInt( 0, activeEmitterChannels.Num(), uncushionedChannels );
	float	targetCushionDB;
	if( activeEmitterChannels.Num() <= firstCushionedChannel )
	{
		// we should be able to hear all of them
		targetCushionDB = DB_SILENCE;
	}
	else
	{
		// we should be able to hear all of them
		targetCushionDB = activeEmitterChannels[firstCushionedChannel].channel->volumeDB;
		const float requestedCushionFadeLimit = s_cushionFadeLimit.GetFloat();
		const float cushionFadeLimit = FLOAT_IS_NAN( requestedCushionFadeLimit )
									   ? -30.0f
									   : idMath::ClampFloat( DB_SILENCE, 0.0f, requestedCushionFadeLimit );
		if( FLOAT_IS_NAN( targetCushionDB ) )
		{
			targetCushionDB = DB_SILENCE;
		}
		if( targetCushionDB < DB_SILENCE )
		{
			targetCushionDB = DB_SILENCE;
		}
		else if( targetCushionDB > cushionFadeLimit )
		{
			targetCushionDB = cushionFadeLimit;
		}
	}

	// linearly drift the currentTargetCushionDB towards targetCushionDB
	float	driftedDB = FLOAT_IS_NAN( currentCushionDB ) ? DB_SILENCE : currentCushionDB;
	const float clampedDriftRate = ( FLOAT_IS_NAN( driftRate ) || driftRate <= 0.0f ) ? 0.0f : driftRate;
	if( driftedDB < targetCushionDB )
	{
		driftedDB += clampedDriftRate;
		if( driftedDB > targetCushionDB )
		{
			driftedDB = targetCushionDB;
		}
	}
	else
	{
		driftedDB -= clampedDriftRate;
		if( driftedDB < targetCushionDB )
		{
			driftedDB = targetCushionDB;
		}
	}

	// ramp the lower sound volumes down
	for( int i = 0; i < activeEmitterChannels.Num(); i++ )
	{
		idSoundChannel* chan = activeEmitterChannels[i].channel;
		chan->volumeDB = MapVolumeFromFadeDB( chan->volumeDB, driftedDB );
	}

	return driftedDB;
}

static int SoundChannelHardwareWidth( const idSoundChannel* channel )
{
	if( channel == NULL || channel->leadinSample == NULL )
	{
		return 1;
	}
	return idMath::ClampInt( 1, MAX_CHANNELS_PER_VOICE, channel->leadinSample->NumChannels() );
}

static int SoundChannelSortKey( const idSoundChannel* channel, const int currentTime )
{
	if( channel == NULL )
	{
		return idMath::Ftoi( DB_SILENCE * 100.0f );
	}
	return channel->MixPrioritySortKey( currentTime );
}

/*
========================
idSoundWorldLocal::Update
========================
*/
void idSoundWorldLocal::Update()
{

	if( s_noSound.GetBool() )
	{
		return;
	}

	// ------------------
	// Update emitters
	//
	// Only loop through the list once to avoid extra cache misses
	// ------------------

	// The naming convention is weird here because we reuse the name "channel"
	// An idSoundChannel is a channel on an emitter, which may have an explicit channel assignment or SND_CHANNEL_ANY
	// A hardware channel is a channel from the sound file itself (IE: left, right, LFE)
	// We only allow MAX_HARDWARE_CHANNELS channels, which may wind up being a smaller number of idSoundChannels
	idStaticList< idActiveChannel, MAX_ACTIVE_EMITTER_CHANNEL_CANDIDATES > activeEmitterChannels;
	const int maxEmitterChannels = idMath::ClampInt( 1, MAX_HARDWARE_VOICES, s_maxEmitterChannels.GetInteger() );

	int activeHardwareChannels = 0;
	int	totalHardwareChannels = 0;
	int	totalEmitterChannels = 0;

	int currentTime = GetSoundTime();
	for( int e = emitters.Num() - 1; e >= 0; e-- )
	{
		// Keep emitter indices stable for Quake 4 game/render handles. Freed
		// emitters remain as reusable slots instead of collapsing the list.
		if( emitters[e]->CheckForCompletion( currentTime ) )
		{
			continue;
		}

		emitters[e]->Update( currentTime );

		totalEmitterChannels += emitters[e]->channels.Num();

		// sort the active channels into the hardware list
		for( int i = 0; i < emitters[e]->channels.Num(); i++ )
		{
			idSoundChannel* channel = emitters[e]->channels[i];

			// Retail drops inaudible contributions before sorting, so silent channels
			// cannot consume the dense-scene OpenAL voice budget.
			if( channel->volumeDB <= DB_SILENCE )
			{
				if( channel->CanMute() )
				{
					channel->Mute();
				}
				continue;
			}

			const int sortKey = SoundChannelSortKey( channel, currentTime );

			// Keep track of the total number of hardware channels.
			// This is done after calculating the sort key to avoid a load-hit-store that
			// would occur when using the sort key in the loop below after the Ftoi above.
			const int sampleChannels = SoundChannelHardwareWidth( channel );
			totalHardwareChannels += sampleChannels;

			// Find the location to insert this channel based on the sort key.
			int insertIndex = 0;
			for( insertIndex = 0; insertIndex < activeEmitterChannels.Num(); insertIndex++ )
			{
				if( sortKey > activeEmitterChannels[insertIndex].sortKey )
				{
					break;
				}
			}

			// Only insert at the end if there is room.
			if( insertIndex == activeEmitterChannels.Num() )
			{
				// Always leave one spot free in activeEmitterChannels so a louder later sound can insert before sorting.
				if( activeEmitterChannels.Num() >= maxEmitterChannels || activeHardwareChannels + sampleChannels > MAX_HARDWARE_CHANNELS )
				{
					// We don't have enough voices to play this, so mute it if it was playing.
					channel->Mute();
					continue;
				}
			}

			// We want to insert the sound at this point.
			activeEmitterChannels.Insert( idActiveChannel( channel, sortKey ), insertIndex );
			activeHardwareChannels += sampleChannels;

			// If we are over our voice limit or at our channel limit, mute sounds until it fits.
			// Drop the tail when the temporary sort candidate pushes us past the real voice budget.
			while( activeEmitterChannels.Num() > maxEmitterChannels || activeHardwareChannels > MAX_HARDWARE_CHANNELS )
			{
				const int indexToRemove = activeEmitterChannels.Num() - 1;
				idSoundChannel* const channelToMute = activeEmitterChannels[ indexToRemove ].channel;
				channelToMute->Mute();
				activeHardwareChannels -= SoundChannelHardwareWidth( channelToMute );
				activeEmitterChannels.RemoveIndex( indexToRemove );
			}
		}
	}

	const float secondsPerFrame = 1.0f / 60.0f;

	// ------------------
	// In the very common case of having more sounds that would contribute to the
	// mix than there are available hardware voices, it can be an audible discontinuity
	// when a channel initially gets a voice or loses a voice.
	// To avoid this, make sure that the last few hardware voices are mixed with a volume
	// of zero, so they won't make a difference as they come and go.
	// It isn't obvious what the exact best volume ramping method should be, just that
	// it smoothly change frame to frame.
	// ------------------
	const int cushionFadeChannels = Max( 0, s_cushionFadeChannels.GetInteger() );
	const int uncushionedChannels = idMath::ClampInt( 0, maxEmitterChannels, maxEmitterChannels - cushionFadeChannels );
	currentCushionDB = AdjustForCushionChannels( activeEmitterChannels, uncushionedChannels,
					   currentCushionDB, s_cushionFadeRate.GetFloat() * secondsPerFrame );

	// ------------------
	// Update Hardware
	// ------------------
	shakeAmp = 0.0f;
	rumbleAmp = 0.0f;

	idStr showVoiceTable;
	bool showVoices = s_showVoices.GetBool();
	if( showVoices )
	{
		showVoiceTable = va( "currentCushionDB: %5.1f  freeVoices: %i zombieVoices: %i buffers:%i/%i\n", currentCushionDB,
							   soundSystemLocal.hardware.GetNumFreeVoices(), soundSystemLocal.hardware.GetNumZombieVoices(),
							   soundSystemLocal.activeStreamBufferContexts.Num(), soundSystemLocal.freeStreamBufferContexts.Num() );
	}
	soundSystemLocal.hardware.BeginDeferredUpdates();
	for( int i = 0; i < activeEmitterChannels.Num(); i++ )
	{
		idSoundChannel* chan = activeEmitterChannels[i].channel;
		chan->UpdateHardware( 0.0f, currentTime );

		if( showVoices )
		{
			idStr voiceLine;
			voiceLine = va( "%5.1f db [%3i:%2i] %s", chan->volumeDB, chan->emitter->index, chan->logicalChannel, chan->CanMute() ? "" : " <CANT MUTE>\n" );
			idSoundSample* leadinSample = chan->leadinSample;
			idSoundSample* loopingSample = chan->loopingSample;
			if( loopingSample == NULL )
			{
				voiceLine.Append( va( "%ikhz*%i %s\n", leadinSample->SampleRate() / 1000, leadinSample->NumChannels(), leadinSample->GetName() ) );
			}
			else if( loopingSample == leadinSample )
			{
				voiceLine.Append( va( "%ikhz*%i <LOOPING> %s\n", leadinSample->SampleRate() / 1000, leadinSample->NumChannels(), leadinSample->GetName() ) );
			}
			else
			{
				voiceLine.Append( va( "%ikhz*%i %s | %ikhz*%i %s\n", leadinSample->SampleRate() / 1000, leadinSample->NumChannels(), leadinSample->GetName(), loopingSample->SampleRate() / 1000, loopingSample->NumChannels(), loopingSample->GetName() ) );
			}
			showVoiceTable += voiceLine;
		}

		// Calculate shakes
		if( chan->hardwareVoice == NULL )
		{
			continue;
		}

		const float channelAmplitude = chan->hardwareVoice->GetGain() * chan->currentAmplitude;
		shakeAmp += chan->parms.shakes * channelAmplitude;

		float channelRumble = idMath::Fabs( chan->parms.shakes ) * channelAmplitude;
		if( ( chan->parms.soundShaderFlags & SSF_CAUSE_RUMBLE ) != 0 )
		{
			const float flaggedRumble = 0.35f * channelAmplitude;
			if( channelRumble < flaggedRumble )
			{
				channelRumble = flaggedRumble;
			}
		}
		rumbleAmp += channelRumble;
	}
	soundSystemLocal.hardware.EndDeferredUpdates();
	if( showVoices )
	{
		// The overlay console this used to draw into does not exist in openQ4,
		// which left s_showVoices building a table every frame and throwing it
		// away - i.e. the cvar produced no output at all. Print it instead so
		// the voice list is actually usable for diagnosing silent sounds.
//		static idOverlayHandle handle;
//		console->PrintOverlay( handle, JUSTIFY_LEFT, showVoiceTable.c_str() );
		idLib::Printf( "%s", showVoiceTable.c_str() );
	}

	//if( s_drawSounds.GetBool() && renderWorld != NULL )
	//{
	//	for( int e = 0; e < emitters.Num(); e++ )
	//	{
	//		idSoundEmitterLocal* emitter = emitters[e];
	//		bool audible = false;
	//		float maxGain = 0.0f;
	//		for( int c = 0; c < emitter->channels.Num(); c++ )
	//		{
	//			if( emitter->channels[c]->hardwareVoice != NULL )
	//			{
	//				audible = true;
	//				maxGain = Max( maxGain, emitter->channels[c]->hardwareVoice->GetGain() );
	//			}
	//		}
	//		if( !audible )
	//		{
	//			continue;
	//		}
	//
	//		static const int lifetime = 20;
	//
	//		idBounds ref;
	//		ref.Clear();
	//		ref.AddPoint( idVec3( -10.0f ) );
	//		ref.AddPoint( idVec3( 10.0f ) );
	//
	//		// draw a box
	//		renderWorld->DebugBounds( idVec4( maxGain, maxGain, 1.0f, 1.0f ), ref, emitter->origin, lifetime );
	//		if( emitter->origin != emitter->spatializedOrigin )
	//		{
	//			renderWorld->DebugLine( idVec4( 1.0f, 0.0f, 0.0f, 1.0f ), emitter->origin, emitter->spatializedOrigin, lifetime );
	//		}
	//
	//		// draw the index
	//		idVec3 textPos = emitter->origin;
	//		textPos.z -= 8;
	//		renderWorld->DrawText( va( "%i", e ), textPos, 0.1f, idVec4( 1, 0, 0, 1 ), listener.axis, 1, lifetime );
	//		textPos.z += 8;
	//
	//		// run through all the channels
	//		for( int k = 0; k < emitter->channels.Num(); k++ )
	//		{
	//			idSoundChannel* chan = emitter->channels[k];
	//			float	min = chan->parms.minDistance;
	//			float	max = chan->parms.maxDistance;
	//			const char* defaulted = chan->leadinSample->IsDefault() ? " *DEFAULTED*" : "";
	//			idStr text;
	//			text.Format( "%s (%i %i/%i)%s", chan->soundShader->GetName(), idMath::Ftoi( emitter->spatializedDistance ), idMath::Ftoi( min ), idMath::Ftoi( max ), defaulted );
	//			renderWorld->DrawText( text, textPos, 0.1f, idVec4( 1, 0, 0, 1 ), listener.axis, 1, lifetime );
	//			textPos.z += 8;
	//		}
	//	}
	//}
}

/*
========================
idSoundWorldLocal::OnReloadSound
========================
*/
void idSoundWorldLocal::OnReloadSound( const idDecl* shader )
{
	for( int i = 0; i < emitters.Num(); i++ )
	{
		emitters[i]->OnReloadSound( shader );
	}
}

/*
========================
idSoundWorldLocal::EmitterForIndex
========================
*/
idSoundEmitter* idSoundWorldLocal::EmitterForIndex( int index )
{
	// This is only used by save/load code which assumes index = 0 is invalid
	// Which is fine since we use index 0 for the local sound emitter anyway
	if( index <= 0 )
	{
		return NULL;
	}
	if( index >= emitters.Num() )
	{
		return NULL;
	}
	return emitters[index];
}

/*
========================
idSoundWorldLocal::ClearAllSoundEmitters
========================
*/
void idSoundWorldLocal::ClearAllSoundEmitters()
{
	shakeAmp = 0.0f;
	rumbleAmp = 0.0f;
	for( int i = 0; i < emitters.Num(); i++ )
	{
		emitters[i]->Reset();
		emitterAllocator.Free( emitters[i] );
	}
	emitters.Clear();
	localSound = AllocSoundEmitter();
}

/*
========================
idSoundWorldLocal::StopAllSounds

This is called from the main thread.
========================
*/
void idSoundWorldLocal::StopAllSounds()
{
	shakeAmp = 0.0f;
	rumbleAmp = 0.0f;
	for( int i = 0; i < emitters.Num(); i++ )
	{
		const bool wasReusable = emitters[i]->canFree && emitters[i]->channels.Num() == 0;
		emitters[i]->Reset();
		if( wasReusable )
		{
			emitters[i]->canFree = true;
		}
	}
}

/*
========================
idSoundWorldLocal::PlayShaderDirectly
========================
*/
int idSoundWorldLocal::PlayShaderDirectly( const char* name, int channel )
{
	if( name == NULL || name[0] == 0 )
	{
		localSound->StopSound( channel );
		return 0;
	}
	const idSoundShader* shader = declManager->FindSound( name );
	if( shader == NULL )
	{
		localSound->StopSound( channel );
		return 0;
	}
	else
	{
		return localSound->StartSound( shader, channel, soundSystemLocal.random.RandomFloat(), SSF_GLOBAL, true );
	}
}

/*
========================
idSoundWorldLocal::Skip
========================
*/
void idSoundWorldLocal::Skip( int time )
{
	accumulatedPauseTime -= time;
	pauseFade.SetVolume( 0.0f );
	pauseFade.Fade( 1.0f, s_unpauseFadeInTime.GetInteger(), GetSoundTime() );
}

/*
========================
idSoundWorldLocal::Pause
========================
*/
void idSoundWorldLocal::Pause()
{
	if( !isPaused )
	{
		pausedTime = soundSystemLocal.SoundTime();
		isPaused = true;
		// just pause all unmutable voices (normally just voice overs)
		for( int e = emitters.Num() - 1; e > 0; e-- )
		{
			for( int i = 0; i < emitters[e]->channels.Num(); i++ )
			{
				idSoundChannel* channel = emitters[e]->channels[i];
				if( !channel->CanMute() && channel->hardwareVoice != NULL )
				{
					channel->hardwareVoice->Pause();
				}
			}
		}
	}
}

/*
========================
idSoundWorldLocal::UnPause
========================
*/
void idSoundWorldLocal::UnPause()
{
	if( isPaused )
	{
		isPaused = false;
		accumulatedPauseTime += soundSystemLocal.SoundTime() - pausedTime;
		pauseFade.SetVolume( 0.0f );
		pauseFade.Fade( 1.0f, s_unpauseFadeInTime.GetInteger(), GetSoundTime() );

		// just unpause all unmutable voices (normally just voice overs)
		for( int e = emitters.Num() - 1; e > 0; e-- )
		{
			for( int i = 0; i < emitters[e]->channels.Num(); i++ )
			{
				idSoundChannel* channel = emitters[e]->channels[i];
				if( !channel->CanMute() && channel->hardwareVoice != NULL )
				{
					channel->hardwareVoice->UnPause();
				}
			}
		}
	}
}

/*
========================
idSoundWorldLocal::GetSoundTime
========================
*/
int idSoundWorldLocal::GetSoundTime()
{
	if( isPaused )
	{
		return pausedTime - accumulatedPauseTime;
	}
	else
	{
		return soundSystemLocal.SoundTime() - accumulatedPauseTime;
	}
}

/*
===================
idSoundWorldLocal::ResolveOrigin

Find out of the sound is completely occluded by a closed door portal, or
the virtual sound origin position at the portal closest to the listener.
  this is called by the main thread

dist is the distance from the orignial sound origin to the current portal that enters soundArea
def->distance is the distance we are trying to reduce.

If there is no path through open portals from the sound to the listener, def->spatializedDistance will remain
set at maxDistance
===================
*/
static const int MAX_PORTAL_TRACE_DEPTH = 10;

static bool SoundPortalBlocksAudio( const exitPortal_t& portal )
{
	return ( portal.blockingBits & ( PS_BLOCK_VIEW | PS_BLOCK_AIR ) ) != 0;
}

void idSoundWorldLocal::ResolveOrigin( const int stackDepth, const soundPortalTrace_t* prevStack, const int soundArea, const float dist, const int occludingPortals, const idVec3& soundOrigin, idSoundEmitterLocal* def )
{

	if( dist >= def->spatializedDistance )
	{
		// we can't possibly hear the sound through this chain of portals
		return;
	}

	if( soundArea == listener.area )
	{
		float fullDist = dist + ( soundOrigin - listener.pos ).LengthFast();
		if( fullDist < def->spatializedDistance )
		{
			def->spatializedDistance = fullDist;
			def->spatializedOrigin = soundOrigin;
			def->occludingPortalCount = occludingPortals;
		}
		return;
	}

	if( stackDepth == MAX_PORTAL_TRACE_DEPTH )
	{
		// don't spend too much time doing these calculations in big maps
		return;
	}

	soundPortalTrace_t newStack;
	newStack.portalArea = soundArea;
	newStack.prevStack = prevStack;

	int numPortals = renderWorld->NumPortalsInArea( soundArea );
	for( int p = 0; p < numPortals; p++ )
	{
		exitPortal_t re = renderWorld->GetPortal( soundArea, p );

		float occlusionDistance = 0;

		// air blocking windows will block sound like closed doors
		const bool portalBlocksAudio = SoundPortalBlocksAudio( re );
		if( portalBlocksAudio )
		{
			// we could just completely cut sound off, but reducing the volume works better
			// continue;
			occlusionDistance = s_doorDistanceAdd.GetFloat();
		}

		// what area are we about to go look at
		int otherArea = re.areas[0];
		if( re.areas[0] == soundArea )
		{
			otherArea = re.areas[1];
		}

		// if this area is already in our portal chain, don't bother looking into it
		const soundPortalTrace_t* prev;
		for( prev = prevStack ; prev ; prev = prev->prevStack )
		{
			if( prev->portalArea == otherArea )
			{
				break;
			}
		}
		if( prev )
		{
			continue;
		}

		// pick a point on the portal to serve as our virtual sound origin
		idVec3	source;

		idPlane	pl;
		re.w->GetPlane( pl );

		float	scale;
		idVec3	dir = listener.pos - soundOrigin;
		if( !pl.RayIntersection( soundOrigin, dir, scale ) )
		{
			source = re.w->GetCenter();
		}
		else
		{
			source = soundOrigin + scale * dir;

			// if this point isn't inside the portal edges, slide it in
			for( int i = 0 ; i < re.w->GetNumPoints() ; i++ )
			{
				int j = ( i + 1 ) % re.w->GetNumPoints();
				idVec3	edgeDir = ( *( re.w ) )[j].ToVec3() - ( *( re.w ) )[i].ToVec3();
				idVec3	edgeNormal;

				edgeNormal.Cross( pl.Normal(), edgeDir );

				idVec3	fromVert = source - ( *( re.w ) )[j].ToVec3();

				float d = edgeNormal * fromVert;
				if( d > 0 )
				{
					// move it in
					float div = edgeNormal.Normalize();
					d /= div;

					source -= d * edgeNormal;
				}
			}
		}

		idVec3 tlen = source - soundOrigin;
		float tlenLength = tlen.LengthFast();

		const int childOccludingPortals = occludingPortals + ( portalBlocksAudio ? 1 : 0 );
		ResolveOrigin( stackDepth + 1, &newStack, otherArea, dist + tlenLength + occlusionDistance, childOccludingPortals, source, def );
	}
}

/*
========================
idSoundWorldLocal::StartWritingDemo
========================
*/
void idSoundWorldLocal::StartWritingDemo( idDemoFile* demo )
{
	writeDemo = demo;

//	WriteDeclCache( writeDemo, DS_SOUND, SCMD_CACHESOUNDSHADER, DECL_SOUND );

	writeDemo->WriteInt( DS_SOUND );
	writeDemo->WriteInt( SCMD_STATE );

	// use the normal save game code to archive all the emitters
	WriteToSaveGame( writeDemo );
}

/*
========================
idSoundWorldLocal::StopWritingDemo
========================
*/
void idSoundWorldLocal::StopWritingDemo()
{
	writeDemo = NULL;
}

/*
========================
idSoundWorldLocal::ProcessDemoCommand
========================
*/
bool idSoundWorldLocal::ProcessDemoCommand( idDemoFile* readDemo )
{
	if( readDemo == NULL || !readDemo->IsOpen() )
	{
		return false;
	}

	int command = 0;
	if( readDemo->ReadInt( command ) != sizeof( command ) )
	{
		return StopMalformedSoundDemo( readDemo, "sound command", "has a truncated command identifier" );
	}

	idSoundDemoCommandReader reader( readDemo, SoundDemoCommandName( command ) );
	switch( command )
	{
		case SCMD_CACHESOUNDSHADER:
		{
			int numCaches = 0;
			if( !reader.ReadInt( numCaches, "cache count" ) )
			{
				return false;
			}
			if( numCaches < 0 || numCaches > SOUND_DEMO_MAX_CACHED_SOUNDS )
			{
				return reader.Fail( va( "contains invalid cache count %d", numCaches ) );
			}
			for( int i = 0; i < numCaches; ++i )
			{
				idStr declName;
				if( !reader.ReadHashString( declName, "sound shader name" ) )
				{
					return false;
				}
				declManager->FindSound( declName );
			}
			return true;
		}
		case SCMD_STATE:
		{
			if( !ReadSoundState( readDemo, readDemo ) )
			{
				return false;
			}
			UnPause();
			return true;
		}
		case SCMD_PLACE_LISTENER:
		{
			idVec3	origin;
			idMat3	axis;
			int		listenerId;

			if( !reader.ReadVec3( origin, "listener origin" ) ||
				!reader.ReadMat3( axis, "listener axis" ) ||
				!reader.ReadInt( listenerId, "listener id" ) )
			{
				return false;
			}
			PlaceListener( origin, axis, listenerId );
			return true;
		}
		case SCMD_ALLOC_EMITTER:
		{
			int index = 0;
			if( !reader.ReadInt( index, "emitter index" ) )
			{
				return false;
			}
			if( index <= 0 || index >= SOUND_SAVEGAME_MAX_EMITTERS )
			{
				return reader.Fail( va( "contains invalid emitter index %d", index ) );
			}

			while( emitters.Num() <= index )
			{
				idSoundEmitterLocal* emitter = emitterAllocator.Alloc();
				if( emitter == NULL )
				{
					return reader.Fail( "could not allocate an emitter" );
				}
				emitter->Init( emitters.Append( emitter ), this );
			}
			if( emitters[index] == NULL )
			{
				return reader.Fail( va( "references unavailable emitter %d", index ) );
			}
			emitters[index]->Reset();
			return true;
		}
		case SCMD_FREE:
		{
			int index = 0;
			int immediate = 0;
			if( !reader.ReadInt( index, "emitter index" ) ||
				!reader.ReadInt( immediate, "immediate flag" ) )
			{
				return false;
			}
			idSoundEmitter* emitter = EmitterForIndex( index );
			if( emitter == NULL )
			{
				return reader.Fail( va( "references unavailable emitter %d", index ) );
			}
			emitter->Free( immediate != 0 );
			return true;
		}
		case SCMD_UPDATE:
		{
			int index = 0;
			idVec3 origin;
			idVec3 velocity;
			int listenerId = 0;
			soundShaderParms_t parms;

			if( !reader.ReadInt( index, "emitter index" ) ||
				!reader.ReadVec3( origin, "emitter origin" ) ||
				!reader.ReadVec3( velocity, "emitter velocity" ) ||
				!reader.ReadInt( listenerId, "listener id" ) ||
				!ReadDemoShaderParms( reader, parms ) )
			{
				return false;
			}
			idSoundEmitter* emitter = EmitterForIndex( index );
			if( emitter == NULL )
			{
				return reader.Fail( va( "references unavailable emitter %d", index ) );
			}
			emitter->UpdateEmitter( origin, velocity, listenerId, &parms );
			return true;
		}
		case SCMD_START:
		{
			int index = 0;
			idStr shaderName;
			int channel = 0;
			float diversity = 0.0f;
			int shaderFlags = 0;

			if( !reader.ReadInt( index, "emitter index" ) ||
				!reader.ReadHashString( shaderName, "sound shader name" ) ||
				!reader.ReadInt( channel, "channel" ) ||
				!reader.ReadFloat( diversity, "diversity" ) ||
				!reader.ReadInt( shaderFlags, "sound shader flags" ) )
			{
				return false;
			}
			if( FLOAT_IS_NAN( diversity ) || diversity < 0.0f || diversity > 1.0f )
			{
				return reader.Fail( va( "contains invalid diversity %g", diversity ) );
			}
			idSoundEmitter* emitter = EmitterForIndex( index );
			if( emitter == NULL )
			{
				return reader.Fail( va( "references unavailable emitter %d", index ) );
			}
			const idSoundShader* shader = declManager->FindSound( shaderName );
			emitter->StartSound( shader, ( s_channelType )channel, diversity, shaderFlags );
			return true;
		}
		case SCMD_MODIFY:
		{
			int index = 0;
			int channel = 0;
			soundShaderParms_t parms;

			if( !reader.ReadInt( index, "emitter index" ) ||
				!reader.ReadInt( channel, "channel" ) ||
				!ReadDemoShaderParms( reader, parms ) )
			{
				return false;
			}
			idSoundEmitter* emitter = EmitterForIndex( index );
			if( emitter == NULL )
			{
				return reader.Fail( va( "references unavailable emitter %d", index ) );
			}
			emitter->ModifySound( ( s_channelType )channel, &parms );
			return true;
		}
		case SCMD_STOP:
		{
			int index = 0;
			int channel = 0;
			if( !reader.ReadInt( index, "emitter index" ) ||
				!reader.ReadInt( channel, "channel" ) )
			{
				return false;
			}
			idSoundEmitter* emitter = EmitterForIndex( index );
			if( emitter == NULL )
			{
				return reader.Fail( va( "references unavailable emitter %d", index ) );
			}
			emitter->StopSound( ( s_channelType )channel );
			return true;
		}
		case SCMD_FADE:
		{
			int index = 0;
			int channel = 0;
			float to = 0.0f;
			float over = 0.0f;
			if( !reader.ReadInt( index, "emitter index" ) ||
				!reader.ReadInt( channel, "channel" ) ||
				!reader.ReadFloat( to, "target volume" ) ||
				!reader.ReadFloat( over, "fade duration" ) )
			{
				return false;
			}
			idSoundEmitter* emitter = EmitterForIndex( index );
			if( emitter == NULL )
			{
				return reader.Fail( va( "references unavailable emitter %d", index ) );
			}
			emitter->FadeSound( ( s_channelType )channel, to, over );
			return true;
		}
		default:
			return reader.Fail( va( "has unknown command identifier %d", command ) );
	}
}

/*
=================
idSoundWorldLocal::AVIOpen
=================
*/
void idSoundWorldLocal::AVIOpen( const char*, const char* )
{
}

/*
=================
idSoundWorldLocal::AVIClose
=================
*/
void idSoundWorldLocal::AVIClose()
{
}

/*
=================
idSoundWorldLocal::WriteToSaveGame
=================
*/
void idSoundWorldLocal::WriteToSaveGame( idFile* savefile )
{
	struct helper
	{
		static void WriteChecked( idFile* savefile, int bytesWritten, int expected, const char* fieldName, int offset )
		{
			if( bytesWritten != expected )
			{
				common->Error( "idSoundWorldLocal::WriteToSaveGame: failed to write %s at offset %d (wrote %d of %d)",
					fieldName, offset, bytesWritten, expected );
			}
		}
		static void WriteInt( idFile* savefile, int value, const char* fieldName )
		{
			const int offset = savefile->Tell();
			WriteChecked( savefile, savefile->WriteInt( value ), static_cast<int>( sizeof( value ) ), fieldName, offset );
		}
		static void WriteFloat( idFile* savefile, float value, const char* fieldName )
		{
			const int offset = savefile->Tell();
			WriteChecked( savefile, savefile->WriteFloat( value ), static_cast<int>( sizeof( value ) ), fieldName, offset );
		}
		static void WriteBool( idFile* savefile, bool value, const char* fieldName )
		{
			const int offset = savefile->Tell();
			WriteChecked( savefile, savefile->WriteBool( value ), static_cast<int>( sizeof( byte ) ), fieldName, offset );
		}
		static void WriteVec3( idFile* savefile, const idVec3& value, const char* fieldName )
		{
			const int offset = savefile->Tell();
			WriteChecked( savefile, savefile->WriteVec3( value ), static_cast<int>( sizeof( value ) ), fieldName, offset );
		}
		static void WriteMat3( idFile* savefile, const idMat3& value, const char* fieldName )
		{
			const int offset = savefile->Tell();
			WriteChecked( savefile, savefile->WriteMat3( value ), static_cast<int>( sizeof( value ) ), fieldName, offset );
		}
		static void WriteString( idFile* savefile, const char* value, const char* fieldName )
		{
			if( value == NULL )
			{
				value = "";
			}
			const int len = idStr::Length( value );
			if( len < 0 || len > MAX_STRING_CHARS )
			{
				common->Error( "idSoundWorldLocal::WriteToSaveGame: invalid %s length %d", fieldName, len );
			}
			WriteInt( savefile, len, fieldName );
			if( len > 0 )
			{
				const int offset = savefile->Tell();
				WriteChecked( savefile, savefile->Write( value, len ), len, fieldName, offset );
			}
		}
		static void WriteSoundFade( idFile* savefile, idSoundFade& sf )
		{
			WriteInt( savefile, sf.fadeStartTime, "sound fade start time" );
			WriteInt( savefile, sf.fadeEndTime, "sound fade end time" );
			WriteFloat( savefile, sf.fadeStartVolume, "sound fade start volume" );
			WriteFloat( savefile, sf.fadeEndVolume, "sound fade end volume" );
		}
		static void WriteShaderParms( idFile* savefile, soundShaderParms_t& parms )
		{
			if( parms.soundClass < 0 || parms.soundClass >= SOUND_MAX_CLASSES )
			{
				common->Error( "idSoundWorldLocal::WriteToSaveGame: invalid sound class %d", parms.soundClass );
			}
			WriteFloat( savefile, parms.minDistance, "sound min distance" );
			WriteFloat( savefile, parms.maxDistance, "sound max distance" );
			WriteFloat( savefile, parms.volume, "sound volume" );
			WriteFloat( savefile, parms.attenuatedVolume, "sound attenuated volume" );
			WriteFloat( savefile, parms.shakes, "sound shakes" );
			WriteInt( savefile, parms.soundShaderFlags, "sound shader flags" );
			WriteInt( savefile, parms.soundClass, "sound class" );
			WriteFloat( savefile, parms.frequencyShift, "sound frequency shift" );
			WriteFloat( savefile, parms.wetLevel, "sound wet level" );
			WriteFloat( savefile, parms.dryLevel, "sound dry level" );
		}
	};
	helper::WriteInt( savefile, GetSoundTime(), "sound time" );

	helper::WriteSoundFade( savefile, volumeFade );
	for( int c = 0; c < SOUND_MAX_CLASSES; c++ )
	{
		helper::WriteSoundFade( savefile, soundClassFade[c] );
	}
	helper::WriteFloat( savefile, slowmoSpeed, "slowmo speed" );
	helper::WriteBool( savefile, enviroSuitActive, "enviro suit state" );

	helper::WriteMat3( savefile, listener.axis, "listener axis" );
	helper::WriteVec3( savefile, listener.pos, "listener position" );
	helper::WriteInt( savefile, listener.id, "listener id" );
	helper::WriteInt( savefile, listener.area, "listener area" );

	helper::WriteFloat( savefile, shakeAmp, "shake amplitude" );

	int num = emitters.Num();
	while( num > 1 )
	{
		idSoundEmitterLocal* emitter = emitters[num - 1];
		if( emitter == NULL || !emitter->canFree || emitter->channels.Num() != 0 )
		{
			break;
		}
		num--;
	}
	if( num < 1 || num > SOUND_SAVEGAME_MAX_EMITTERS )
	{
		common->Error( "idSoundWorldLocal::WriteToSaveGame: bad emitter count %d", num );
	}
	helper::WriteInt( savefile, num, "sound emitter count" );
	int totalChannels = 0;
	// Start at 1 because the local sound emitter is not saved
	for( int e = 1; e < num; e++ )
	{
		idSoundEmitterLocal* emitter = emitters[e];
		if( emitter == NULL )
		{
			common->Error( "idSoundWorldLocal::WriteToSaveGame: NULL emitter at index %d", e );
		}
		const int numChannels = emitter->channels.Num();
		if( numChannels < 0 || numChannels > MAX_CHANNELS_PER_EMITTER || totalChannels > SOUND_SAVEGAME_MAX_TOTAL_CHANNELS - numChannels )
		{
			common->Error( "idSoundWorldLocal::WriteToSaveGame: invalid channel count %d for emitter %d", numChannels, e );
		}
		totalChannels += numChannels;
		helper::WriteBool( savefile, emitter->canFree, "emitter can-free flag" );
		helper::WriteVec3( savefile, emitter->origin, "emitter origin" );
		helper::WriteInt( savefile, emitter->emitterId, "emitter listener id" );
		helper::WriteShaderParms( savefile, emitter->parms );
		helper::WriteInt( savefile, numChannels, "emitter channel count" );
		for( int c = 0; c < numChannels; c++ )
		{
			idSoundChannel* channel = emitter->channels[c];
			if( channel == NULL || channel->soundShader == NULL )
			{
				common->Error( "idSoundWorldLocal::WriteToSaveGame: invalid channel %d for emitter %d", c, e );
			}
			helper::WriteInt( savefile, channel->startTime, "channel start time" );
			helper::WriteInt( savefile, channel->endTime, "channel end time" );
			helper::WriteInt( savefile, channel->logicalChannel, "channel logical channel" );
			helper::WriteInt( savefile, channel->choice, "channel sample choice" );
			helper::WriteBool( savefile, channel->allowSlow, "channel allow-slow flag" );
			helper::WriteShaderParms( savefile, channel->parms );
			helper::WriteSoundFade( savefile, channel->volumeFade );
			helper::WriteString( savefile, channel->soundShader->GetName(), "sound shader name" );
			int leadin = -1;
			int looping = -1;
			for( int i = 0; i < channel->soundShader->entries.Num(); i++ )
			{
				if( channel->soundShader->entries[i] == channel->leadinSample )
				{
					leadin = i;
				}
				if( channel->soundShader->entries[i] == channel->loopingSample )
				{
					looping = i;
				}
			}
			if( leadin == -1 )
			{
				for( int i = 0; i < channel->soundShader->leadins.Num(); i++ )
				{
					if( channel->soundShader->leadins[i] == channel->leadinSample )
					{
						// Negative values below -1 encode a leadin-list index.
						leadin = -2 - i;
						break;
					}
				}
			}
			helper::WriteInt( savefile, leadin, "channel leadin index" );
			helper::WriteInt( savefile, looping, "channel looping index" );
			helper::WriteInt( savefile, channel->lastFrequencyShiftTime, "channel frequency shift time" );
			helper::WriteFloat( savefile, channel->lastFrequencyShift, "channel last frequency shift" );
			helper::WriteFloat( savefile, channel->elapsedFrequencyShiftTime, "channel elapsed frequency shift time" );
		}
	}
}

/*
=================
idSoundWorldLocal::ReadFromSaveGame
=================
*/
void idSoundWorldLocal::ReadFromSaveGame( idFile* savefile )
{
	ReadSoundState( savefile, NULL );
}

/*
=================
idSoundWorldLocal::ReadSoundState

SCMD_STATE deliberately uses the savegame wire layout. Savegame failures keep
their historical fatal policy, while render-demo failures close the demo and
return false to the session.
=================
*/
bool idSoundWorldLocal::ReadSoundState( idFile* savefile, idDemoFile* demoFile )
{
	if( savefile == NULL )
	{
		if( demoFile != NULL )
		{
			return StopMalformedSoundDemo( demoFile, "SCMD_STATE", "has no input file" );
		}
		common->Error( "idSoundWorldLocal::ReadFromSaveGame: NULL input file" );
		return false;
	}

	idSoundStateReader reader( savefile, demoFile );
	bool rebuildingEmitters = false;
	const auto AbortRead = [&]() -> bool
	{
		if( demoFile != NULL && rebuildingEmitters )
		{
			// Never leave partially initialized emitters or channel slots live
			// after a malformed state command.
			ClearAllSoundEmitters();
		}
		return false;
	};

	int oldSoundTime = 0;
	if( !reader.ReadInt( oldSoundTime, "old sound time" ) )
	{
		return AbortRead();
	}
	const int64 timeDelta64 = static_cast<int64>( GetSoundTime() ) - static_cast<int64>( oldSoundTime );
	if( timeDelta64 < idMath::INT_MIN || timeDelta64 > idMath::INT_MAX )
	{
		reader.Fail( "contains an overflowing sound time delta" );
		return AbortRead();
	}
	const int timeDelta = static_cast<int>( timeDelta64 );

	idSoundFade restoredVolumeFade;
	idSoundFade restoredSoundClassFade[SOUND_MAX_CLASSES];
	float restoredSlowmoSpeed = 1.0f;
	bool restoredEnviroSuitActive = false;
	listener_t restoredListener;
	float restoredShakeAmp = 0.0f;

	if( !reader.ReadSoundFade( restoredVolumeFade, timeDelta ) )
	{
		return AbortRead();
	}
	for( int c = 0; c < SOUND_MAX_CLASSES; c++ )
	{
		if( !reader.ReadSoundFade( restoredSoundClassFade[c], timeDelta ) )
		{
			return AbortRead();
		}
	}
	if( !reader.ReadFloat( restoredSlowmoSpeed, "slowmo speed" ) ||
		!reader.ReadBool( restoredEnviroSuitActive, "enviro suit state" ) ||
		!reader.ReadMat3( restoredListener.axis, "listener axis" ) ||
		!reader.ReadVec3( restoredListener.pos, "listener position" ) ||
		!reader.ReadInt( restoredListener.id, "listener id" ) ||
		!reader.ReadInt( restoredListener.area, "listener area" ) ||
		!reader.ReadFloat( restoredShakeAmp, "shake amplitude" ) )
	{
		return AbortRead();
	}

	int numEmitters = 0;
	if( !reader.ReadInt( numEmitters, "sound emitter count" ) )
	{
		return AbortRead();
	}
	if( numEmitters < 1 || numEmitters > SOUND_SAVEGAME_MAX_EMITTERS )
	{
		reader.Fail( va( "contains invalid emitter count %d", numEmitters ) );
		return AbortRead();
	}

	ClearAllSoundEmitters();
	rebuildingEmitters = true;
	idStr shaderName;
	int totalChannels = 0;
	// Start at 1 because the local sound emitter is not saved
	for( int e = 1; e < numEmitters; e++ )
	{
		// Do not use AllocSoundEmitter here: a restored, free emitter with no
		// channels is immediately reusable and would collapse the serialized
		// index space before later emitters have been recreated.
		idSoundEmitterLocal* emitter = emitterAllocator.Alloc();
		if( emitter == NULL )
		{
			reader.Fail( va( "could not allocate emitter %d", e ) );
			return AbortRead();
		}
		const int restoredIndex = emitters.Append( emitter );
		emitter->Init( restoredIndex, this );
		if( restoredIndex != e || emitter->index != e || emitter->soundWorld != this || emitter->channels.Num() != 0 )
		{
			reader.Fail( va( "could not recreate emitter index %d (got %d)", e, restoredIndex ) );
			return AbortRead();
		}
		if( !reader.ReadBool( emitter->canFree, "emitter can-free flag" ) ||
			!reader.ReadVec3( emitter->origin, "emitter origin" ) ||
			!reader.ReadInt( emitter->emitterId, "emitter listener id" ) ||
			!reader.ReadShaderParms( emitter->parms ) )
		{
			return AbortRead();
		}

		int numChannels = 0;
		if( !reader.ReadInt( numChannels, "emitter channel count" ) )
		{
			return AbortRead();
		}
		if( numChannels < 0 || numChannels > MAX_CHANNELS_PER_EMITTER )
		{
			reader.Fail( va( "contains invalid channel count %d for emitter %d", numChannels, e ) );
			return AbortRead();
		}
		if( totalChannels > SOUND_SAVEGAME_MAX_TOTAL_CHANNELS - numChannels )
		{
			reader.Fail( va( "contains more than %d sound channels", SOUND_SAVEGAME_MAX_TOTAL_CHANNELS ) );
			return AbortRead();
		}
		totalChannels += numChannels;

		for( int c = 0; c < numChannels; c++ )
		{
			idSoundChannel* channel = AllocSoundChannel();
			if( channel == NULL )
			{
				reader.Fail( va( "could not allocate channel %d for emitter %d", c, e ) );
				return AbortRead();
			}
			emitter->channels.Append( channel );
			channel->emitter = emitter;
			channel->lastFrequencyShiftTime = 0;
			channel->lastFrequencyShift = 1.0f;
			channel->elapsedFrequencyShiftTime = 0.0f;

			if( !reader.ReadInt( channel->startTime, "channel start time" ) ||
				!reader.ReadInt( channel->endTime, "channel end time" ) ||
				!reader.ReadInt( channel->logicalChannel, "channel logical channel" ) ||
				!reader.ReadInt( channel->choice, "channel sample choice" ) ||
				!reader.ReadBool( channel->allowSlow, "channel allow-slow flag" ) ||
				!reader.ReadShaderParms( channel->parms ) ||
				!reader.ReadSoundFade( channel->volumeFade, timeDelta ) ||
				!reader.ReadString( shaderName, "sound shader name" ) )
			{
				return AbortRead();
			}

			int leadin = 0;
			int looping = 0;
			if( !reader.ReadInt( leadin, "channel leadin index" ) ||
				!reader.ReadInt( looping, "channel looping index" ) ||
				!reader.ReadInt( channel->lastFrequencyShiftTime, "channel frequency shift time" ) ||
				!reader.ReadFloat( channel->lastFrequencyShift, "channel last frequency shift" ) ||
				!reader.ReadFloat( channel->elapsedFrequencyShiftTime, "channel elapsed frequency shift time" ) )
			{
				return AbortRead();
			}

			channel->soundShader = declManager->FindSound( shaderName );
			if( channel->soundShader == NULL )
			{
				reader.Fail( va( "could not resolve sound shader '%s'", shaderName.c_str() ) );
				return AbortRead();
			}

			// If the leadin sample is not valid (possible if the shader changed after saving) then the looping entry can't be valid either.
			channel->leadinSample = NULL;
			channel->loopingSample = NULL;
			if( leadin >= 0 && leadin < channel->soundShader->entries.Num() )
			{
				channel->leadinSample = channel->soundShader->entries[ leadin ];
			}
			else if( leadin <= -2 )
			{
				const int64 leadinIndex = -2LL - static_cast<int64>( leadin );
				if( leadinIndex >= 0 && leadinIndex < channel->soundShader->leadins.Num() )
				{
					channel->leadinSample = channel->soundShader->leadins[ static_cast<int>( leadinIndex ) ];
				}
			}
			if( channel->leadinSample != NULL && looping >= 0 && looping < channel->soundShader->entries.Num() )
			{
				channel->loopingSample = channel->soundShader->entries[ looping ];
			}
			else if( channel->leadinSample != NULL && ( channel->parms.soundShaderFlags & SSF_LOOPING ) != 0 )
			{
				channel->loopingSample = channel->leadinSample;
			}

			if( channel->lastFrequencyShiftTime > 0 )
			{
				if( !reader.AddTimeDelta( channel->lastFrequencyShiftTime, timeDelta, "channel frequency shift time" ) )
				{
					return AbortRead();
				}
			}
			else
			{
				channel->lastFrequencyShiftTime = 0;
			}
			if( FLOAT_IS_NAN( channel->lastFrequencyShift ) || channel->lastFrequencyShift <= 0.0f )
			{
				channel->lastFrequencyShift = 1.0f;
			}
			else
			{
				channel->lastFrequencyShift = idMath::ClampFloat( 0.25f, 4.0f, channel->lastFrequencyShift );
			}
			if( FLOAT_IS_NAN( channel->elapsedFrequencyShiftTime ) || channel->elapsedFrequencyShiftTime < 0.0f )
			{
				channel->elapsedFrequencyShiftTime = 0.0f;
			}
			if( !reader.AddTimeDelta( channel->startTime, timeDelta, "channel start time" ) )
			{
				return AbortRead();
			}
			if( channel->endTime == 0 )
			{
				// Do nothing, endTime == 0 means loop forever
			}
			else if( channel->endTime <= oldSoundTime )
			{
				// Channel already stopped
				channel->endTime = 1;
			}
			else
			{
				if( !reader.AddTimeDelta( channel->endTime, timeDelta, "channel end time" ) )
				{
					return AbortRead();
				}
			}
		}
	}

	volumeFade = restoredVolumeFade;
	for( int c = 0; c < SOUND_MAX_CLASSES; c++ )
	{
		soundClassFade[c] = restoredSoundClassFade[c];
	}
	SetSlowmoSpeed( restoredSlowmoSpeed );
	enviroSuitActive = restoredEnviroSuitActive;
	listener = restoredListener;
	shakeAmp = restoredShakeAmp;
	rumbleAmp = 0.0f;
	return true;
}

/*
=================
idSoundWorldLocal::FadeSoundClasses

fade all sounds in the world with a given shader soundClass
to is in Db, over is in seconds
=================
*/
void idSoundWorldLocal::FadeSoundClasses( const int soundClass, const float to, const float over )
{
	if( soundClass < 0 || soundClass >= SOUND_MAX_CLASSES )
	{
		common->Error( "idSoundWorldLocal::FadeSoundClasses: bad soundClass %i", soundClass );
		return;
	}
	soundClassFade[ soundClass ].FadeDB( to, over, GetSoundTime() );
}

/*
=================
idSoundWorldLocal::SetSlowmoSpeed
=================
*/
void idSoundWorldLocal::SetSlowmoSpeed( float speed )
{
	if( FLOAT_IS_NAN( speed ) || speed <= 0.0f )
	{
		speed = 1.0f;
	}
	slowmoSpeed = idMath::ClampFloat( 0.01f, 64.0f, speed );
}

/*
=================
idSoundWorldLocal::SetEnviroSuit
=================
*/
void idSoundWorldLocal::SetEnviroSuit( bool active )
{
	enviroSuitActive = active;
}

/*
=================
idSoundWorldLocal::SetUnderwater

Muffles the whole mix while the listener's head is below a liquid surface. The game re-derives this
from the eye position every frame, so unlike the enviro suit it is not part of the savegame.
=================
*/
void idSoundWorldLocal::SetUnderwater( bool active )
{
	underwaterActive = active;
}

/*
=================
idSoundWorldLocal::SetLiquidTest
=================
*/
void idSoundWorldLocal::SetLiquidTest( liquidTest_t test )
{
	liquidTest = test;
}
