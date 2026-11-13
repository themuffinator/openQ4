/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2012-2016 Robert Beckebans
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
#include "SoundDialogue.h"
#include "../framework/declLipSync.h"

idCVar s_singleEmitter( "s_singleEmitter", "0", CVAR_INTEGER, "mute all sounds but this emitter" );
idCVar s_showStartSound( "s_showStartSound", "0", CVAR_BOOL, "print a message every time a sound starts/stops" );
idCVar s_logSpeech( "s_logSpeech", "0", CVAR_SOUND | CVAR_BOOL, "print SOUND_SPEECH for every start of a sample that may be speech: its dialogue routing, radio channel and subtitle transcript status (identifiers only, never the text)" );
idCVar s_skipStartSound( "s_skipStartSound", "0", CVAR_BOOL, "skip starting sounds" );
idCVar s_useOcclusion( "s_useOcclusion", "1", CVAR_BOOL, "Attenuate sounds based on walls" );
idCVar s_centerFractionVO( "s_centerFractionVO", "0.75", CVAR_FLOAT, "Portion of VO sounds routed to the center channel" );

extern idCVar s_playDefaultSound;
extern idCVar s_noSound;
extern idCVar s_volume;
extern idCVar s_musicVolume;
extern idCVar s_dialogueVolume;
extern idCVar s_constantAmplitude;
extern idCVar s_speakerFraction;
extern idCVar s_radioChatterFraction;
extern idCVar s_quadraticFalloff;
extern idCVar s_frequencyShift;
extern idCVar s_useEAXOcclusion;

static const int SOUND_SHADER_SHAKE_RATE_HZ = 30;
static const float SOUND_SHADER_SCREEN_SHAKE_SCALE = 600.0f;
static const float SOUND_SHADER_MATERIAL_SHAKE_SCALE = 2800.0f;
static const float SOUND_SHADER_SHAKE_NORMALIZE = 1.0f / 32768.0f;
static const float SOUND_WORLD_VOLUME_EPSILON = 1.0f / 1024.0f;
static const int Q4_SOUND_CHANNEL_SPECIAL_ATTENUATION_EXEMPT_0 = 6;
static const int Q4_SOUND_CHANNEL_SPECIAL_ATTENUATION_EXEMPT_1 = 7;
static const int Q4_SOUND_CHANNEL_RADIO_CHATTER = 10;
static const float SOUND_FREQUENCY_SHIFT_MIN = 0.25f;
static const float SOUND_FREQUENCY_SHIFT_MAX = 4.0f;
static const float SOUND_ENVIROSUIT_OCCLUSION = 0.5f;
// openQ4: heavier than the enviro suit - water swallows high frequencies far more than a helmet
static const float SOUND_UNDERWATER_OCCLUSION = 0.75f;
// openQ4: sound crossing the water surface. Most of the energy hitting an air/water boundary is
// reflected rather than transmitted, which is why a world above the surface goes distant and dull
// the moment your head goes under - and why a pump running underwater is suddenly the loudest
// thing you can hear.
static const float SOUND_LIQUID_BOUNDARY_OCCLUSION = 0.65f;
static const int SOUND_FADE_MAX_MSEC = 24 * 60 * 60 * 1000;
static const float SOUND_FADE_MAX_DB = 24.0f;
static const int SOUND_UNMUTABLE_PRIORITY_BOOST = 100000;
static const int SOUND_CHANNEL_COMPLETION_GRACE_MSEC = 100;

static ID_INLINE float SoundSanitizeUnitValue( const float value, const float fallback )
{
	if( FLOAT_IS_NAN( value ) )
	{
		return fallback;
	}
	return idMath::ClampFloat( 0.0f, 1.0f, value );
}

static ID_INLINE float SoundSanitizePositiveValue( const float value, const float fallback )
{
	if( FLOAT_IS_NAN( value ) || value <= 0.0f )
	{
		return fallback;
	}
	return value;
}

static ID_INLINE float SoundSanitizeGainScale( const float value, const float fallback )
{
	return idMath::ClampFloat( 0.0f, 16.0f, SoundSanitizePositiveValue( value, fallback ) );
}

static ID_INLINE float SoundSanitizeFadeScale( const float value, const float fallback )
{
	if( FLOAT_IS_NAN( value ) )
	{
		return fallback;
	}
	return idMath::ClampFloat( 0.0f, 16.0f, value );
}

static ID_INLINE int SoundSanitizeFadeMsec( const int length )
{
	return idMath::ClampInt( 0, SOUND_FADE_MAX_MSEC, length );
}

static ID_INLINE int SoundFadeSecondsToMsec( const float seconds )
{
	if( FLOAT_IS_NAN( seconds ) || seconds <= 0.0f )
	{
		return 0;
	}
	const float clampedSeconds = Min( seconds, MS2SEC( SOUND_FADE_MAX_MSEC ) );
	return SEC2MS( clampedSeconds );
}

static ID_INLINE float SoundFadeDBToScale( const float db )
{
	if( FLOAT_IS_NAN( db ) )
	{
		return 0.0f;
	}
	return SoundSanitizeFadeScale( idMath::dBToScale( Min( db, SOUND_FADE_MAX_DB ) ), 0.0f );
}

static ID_INLINE float VolumeScaleToDB( const float volumeScale )
{
	if( volumeScale <= 0.0f )
	{
		return DB_SILENCE;
	}
	return LinearToDB( volumeScale );
}

static ID_INLINE float SoundChannelFrequencyShift( const idSoundChannel* chan )
{
	if( !s_frequencyShift.GetBool() )
	{
		return 1.0f;
	}

	const float frequencyShift = SoundSanitizePositiveValue( chan->parms.frequencyShift, 1.0f );
	return idMath::ClampFloat( SOUND_FREQUENCY_SHIFT_MIN, SOUND_FREQUENCY_SHIFT_MAX, frequencyShift );
}

static bool SoundUsesMusicVolume( const soundShaderParms_t& parms );

/*
========================
SoundChannelPlaybackRate

The rate a channel's voice plays at: the shader's frequency shift, com_timescale and
the world's slow motion (the weapon wheel runs the game at 0.18x). Music keeps its
tempo through slow motion; at 0.18x it dropped some thirty semitones.
========================
*/
static float SoundChannelPlaybackRate( const idSoundChannel* chan )
{
	extern idCVar com_timescale;
	float rate = SoundChannelFrequencyShift( chan );
	rate *= idMath::ClampFloat( 0.2f, 5.0f, SoundSanitizePositiveValue( com_timescale.GetFloat(), 1.0f ) );
	const idSoundWorldLocal* soundWorld = ( chan->emitter != NULL ) ? chan->emitter->soundWorld : NULL;
	if( soundWorld != NULL && !SoundUsesMusicVolume( chan->parms ) )
	{
		rate *= SoundSanitizePositiveValue( soundWorld->slowmoSpeed, 1.0f );
	}
	return rate;
}

static float SoundChannelPlaybackTimeMsec( idSoundChannel* chan, const int currentTime )
{
	if( chan == NULL || currentTime <= chan->startTime )
	{
		return 0.0f;
	}

	// Retail keeps a shifted sample clock per live channel instead of deriving every offset from wall time.
	// It runs at the voice's whole playback rate: wall time alone stopped a line of dialogue started
	// before slow motion about a third of the way in.
	const float frequencyShift = SoundChannelFrequencyShift( chan );
	const float playbackRate = SoundChannelPlaybackRate( chan );
	if( chan->lastFrequencyShiftTime != 0 || playbackRate != 1.0f )
	{
		float elapsed = 0.0f;
		if( chan->lastFrequencyShiftTime != 0 )
		{
			elapsed = chan->elapsedFrequencyShiftTime + Max( 0, currentTime - chan->lastFrequencyShiftTime ) * chan->lastFrequencyShift;
		}
		else
		{
			// until now only the shader's own shift applied, or the clock would be running already
			elapsed = Max( 0, currentTime - chan->startTime ) * frequencyShift;
		}

		chan->lastFrequencyShift = playbackRate;
		chan->elapsedFrequencyShiftTime = Max( 0.0f, elapsed );
		chan->lastFrequencyShiftTime = currentTime;
		return chan->elapsedFrequencyShiftTime;
	}

	return currentTime - chan->startTime;
}

// The channel's envelope at its playback position: the shader's own shakeData (Raven's
// 30 Hz letter envelope, the one sound-driven lights read) when it has one for this
// variant, else the level measured from the sample.
static float SoundChannelSampleAmplitude( idSoundChannel* chan, const int currentTime )
{
	if( chan == NULL || chan->leadinSample == NULL )
	{
		return 0.0f;
	}
	const int relativeTime = idMath::FtoiFast( SoundChannelPlaybackTimeMsec( chan, currentTime ) );
	const char* shakeData = ( chan->soundShader != NULL ) ? chan->soundShader->GetShakeData( chan->choice ) : NULL;
	if( shakeData != NULL && shakeData[0] != '\0' )
	{
		const int shakeDataLength = idStr::Length( shakeData );
		int shakeIndex = SOUND_SHADER_SHAKE_RATE_HZ * relativeTime / 1000;
		if( ( chan->parms.soundShaderFlags & SSF_LOOPING ) != 0 )
		{
			shakeIndex %= shakeDataLength;
		}
		if( shakeIndex < 0 || shakeIndex >= shakeDataLength )
		{
			return 0.0f;
		}
		const float high = Max( 0, shakeData[shakeIndex] - 'a' ) * SOUND_SHADER_MATERIAL_SHAKE_SCALE;
		return idMath::ATan( high * SOUND_SHADER_SHAKE_NORMALIZE, 1.0f ) / DEG2RAD( 45.0f );
	}
	const int leadinLength = chan->leadinSample->LengthInMsec();
	if( relativeTime < leadinLength )
	{
		return chan->leadinSample->GetAmplitude( relativeTime );
	}
	if( chan->loopingSample != NULL && chan->loopingSample->LengthInMsec() > 0 )
	{
		return chan->loopingSample->GetAmplitude( ( relativeTime - leadinLength ) % chan->loopingSample->LengthInMsec() );
	}
	return 0.0f;
}

static bool SoundChannelHasCompleted( idSoundChannel* chan, const int currentTime )
{
	if( chan == NULL || chan->leadinSample == NULL )
	{
		return true;
	}
	if( chan->endTime == 1 )
	{
		return true;
	}
	if( currentTime < chan->startTime )
	{
		return false;
	}
	if( chan->IsLooping() )
	{
		return false;
	}

	const int length = chan->leadinSample->LengthInMsec();
	if( length <= 0 )
	{
		return chan->endTime > 0 && chan->endTime < currentTime;
	}
	if( chan->lastFrequencyShiftTime == 0 )
	{
		return chan->endTime > 0 && chan->endTime < currentTime;
	}

	return SoundChannelPlaybackTimeMsec( chan, currentTime ) > length + SOUND_CHANNEL_COMPLETION_GRACE_MSEC;
}

static void SoundWorldApplyDistanceFalloff( float& volumeScale, const float distance, const float minDistance, const float maxDistance )
{
	if( FLOAT_IS_NAN( volumeScale ) )
	{
		volumeScale = 0.0f;
		return;
	}
	if( FLOAT_IS_NAN( distance ) || FLOAT_IS_NAN( minDistance ) || FLOAT_IS_NAN( maxDistance ) || maxDistance <= 0.0f )
	{
		return;
	}

	if( distance >= maxDistance )
	{
		volumeScale = 0.0f;
	}
	else if( ( distance > minDistance ) && ( maxDistance > minDistance ) )
	{
		float frac = 1.0f - ( distance - minDistance ) / ( maxDistance - minDistance );
		frac = idMath::ClampFloat( 0.0f, 1.0f, frac );
		if( s_quadraticFalloff.GetBool() )
		{
			frac *= frac;
		}
		volumeScale *= frac;
	}
}

static bool IsRadioChatterChannel( const idSoundChannel* chan )
{
	return chan->logicalChannel == Q4_SOUND_CHANNEL_RADIO_CHATTER;
}

static bool IsVoicePlaybackChannel( const idSoundChannel* chan )
{
	return IsRadioChatterChannel( chan ) && ( chan->parms.soundShaderFlags & SSF_GLOBAL ) != 0;
}

static bool SoundParmsAreVoicePlayback( const soundShaderParms_t& parms, const int logicalChannel )
{
	const int voiceFlags = SSF_VO | SSF_IS_VO | SSF_VO_FOR_PLAYER;
	return ( parms.soundShaderFlags & voiceFlags ) != 0 ||
		   ( logicalChannel == Q4_SOUND_CHANNEL_RADIO_CHATTER && ( parms.soundShaderFlags & SSF_GLOBAL ) != 0 );
}

static int SoundPriorityFromVolumeDB( float volumeDB )
{
	if( FLOAT_IS_NAN( volumeDB ) )
	{
		volumeDB = DB_SILENCE;
	}
	volumeDB = idMath::ClampFloat( DB_SILENCE, 24.0f, volumeDB );
	return idMath::Ftoi( volumeDB * 100.0f );
}

static bool ReceivesQuake4ExtraAttenuation( const idSoundChannel* chan )
{
	return chan->logicalChannel != Q4_SOUND_CHANNEL_SPECIAL_ATTENUATION_EXEMPT_0 &&
		   chan->logicalChannel != Q4_SOUND_CHANNEL_SPECIAL_ATTENUATION_EXEMPT_1 &&
		   chan->parms.soundClass != SOUND_CLASS_MUSICAL;
}

static bool SoundUsesMusicVolume( const soundShaderParms_t& parms )
{
	return ( parms.soundShaderFlags & SSF_MUSIC ) != 0 || parms.soundClass == SOUND_CLASS_MUSICAL;
}

static int SoundSampleChoiceFromDiversity( const int numEntries, const float diversity )
{
	if( numEntries <= 0 || FLOAT_IS_NAN( diversity ) )
	{
		return 0;
	}

	const int choice = static_cast<int>( diversity * numEntries );
	if( choice < 0 || choice >= numEntries )
	{
		return 0;
	}
	return choice;
}

static int SoundLoopStartOffsetFromDiversity( const int lengthMsec, const float diversity )
{
	if( lengthMsec <= 0 || FLOAT_IS_NAN( diversity ) )
	{
		return 0;
	}

	const int offset = static_cast<int>( lengthMsec * diversity );
	return Max( 0, offset );
}

/*
================================================================================================

	idSoundFade

================================================================================================
*/

/*
========================
idSoundFade::Clear
========================
*/
void idSoundFade::Clear()
{
	fadeStartTime = 0;
	fadeEndTime = 0;
	fadeStartVolume = 1.0f;
	fadeEndVolume = 1.0f;
}

/*
========================
idSoundFade::SetVolume
========================
*/
void idSoundFade::SetVolume( float to )
{
	to = SoundSanitizeFadeScale( to, 1.0f );
	fadeStartVolume = to;
	fadeEndVolume = to;
	fadeStartTime = 0;
	fadeEndTime = 0;
}

/*
========================
idSoundFade::Fade
========================
*/
void idSoundFade::Fade( float to, int length, int soundTime )
{
	to = SoundSanitizeFadeScale( to, 0.0f );
	length = SoundSanitizeFadeMsec( length );
	int startTime = soundTime;
	// if it is already fading to this volume at this rate, don't change it
	if( fadeEndTime == startTime + length && fadeEndVolume == to )
	{
		return;
	}
	fadeStartVolume = GetVolume( soundTime );
	fadeEndVolume = to;
	fadeStartTime = startTime;
	fadeEndTime = startTime + length;
}

/*
========================
idSoundFade::FadeDB
========================
*/
void idSoundFade::FadeDB( float toDB, float overSeconds, int soundTime )
{
	Fade( SoundFadeDBToScale( toDB ), SoundFadeSecondsToMsec( overSeconds ), soundTime );
}

/*
========================
idSoundFade::Sanitize
========================
*/
void idSoundFade::Sanitize()
{
	fadeStartVolume = SoundSanitizeFadeScale( fadeStartVolume, 1.0f );
	fadeEndVolume = SoundSanitizeFadeScale( fadeEndVolume, fadeStartVolume );
	if( fadeEndTime < fadeStartTime )
	{
		fadeEndTime = fadeStartTime;
	}
}

/*
========================
idSoundFade::GetVolume
========================
*/
float idSoundFade::GetVolume( const int soundTime ) const
{
	const float fadeDuration = ( fadeEndTime - fadeStartTime );
	const int currentTime = soundTime;
	const float playTime = ( currentTime - fadeStartTime );
	const float startVolume = SoundSanitizeFadeScale( fadeStartVolume, 1.0f );
	const float endVolume = SoundSanitizeFadeScale( fadeEndVolume, startVolume );
	if( fadeDuration <= 0.0f )
	{
		return endVolume;
	}
	else if( currentTime >= fadeEndTime )
	{
		return endVolume;
	}
	else if( currentTime > fadeStartTime )
	{
		return startVolume + ( endVolume - startVolume ) * playTime / fadeDuration;
	}
	else
	{
		return startVolume;
	}
}

/*
========================
idSoundChannel::idSoundChannel
========================
*/
idSoundChannel::idSoundChannel()
{
	hardwareVoice = NULL;
	Reset();
}

/*
========================
idSoundChannel::Reset
========================
*/
void idSoundChannel::Reset()
{
	emitter = NULL;
	hardwareVoice = NULL;

	startTime = 0;
	endTime = 0;
	lastFrequencyShiftTime = 0;
	leadinSample = NULL;
	loopingSample = NULL;
	logicalChannel = SCHANNEL_ANY;
	choice = 0;
	allowSlow = false;
	soundShader = NULL;

	volumeFade.Clear();

	volumeDB = DB_SILENCE;
	currentAmplitude = 0.0f;
	lastFrequencyShift = 1.0f;
	elapsedFrequencyShiftTime = 0.0f;
}

/*
========================
idSoundChannel::~idSoundChannel
========================
*/
idSoundChannel::~idSoundChannel()
{
}

/*
========================
idSoundChannel::CanMute
Never actually mute VO because we can't restart them precisely enough for lip syncing to not fuck up
========================
*/
bool idSoundChannel::CanMute() const
{
	return !IsVoicePlayback();
}

/*
========================
idSoundChannel::IsVoicePlayback
========================
*/
bool idSoundChannel::IsVoicePlayback() const
{
	return SoundParmsAreVoicePlayback( parms, logicalChannel ) || IsVoicePlaybackChannel( this );
}

/*
========================
idSoundChannel::MixPrioritySortKey
========================
*/
int idSoundChannel::MixPrioritySortKey( const int currentTime ) const
{
	(void)currentTime;
	return SoundPriorityFromVolumeDB( volumeDB ) + ( CanMute() ? 0 : SOUND_UNMUTABLE_PRIORITY_BOOST );
}

/*
========================
idSoundChannel::Mute

A muted sound is considered still running, and can restart when a listener
gets close enough.
========================
*/
void idSoundChannel::Mute()
{
	if( hardwareVoice != NULL )
	{
		soundSystemLocal.FreeVoice( hardwareVoice );
		hardwareVoice = NULL;
	}
}

/*
========================
idSoundChannel::IsLooping
========================
*/
bool idSoundChannel::IsLooping() const
{
	return ( parms.soundShaderFlags & SSF_LOOPING ) != 0;
}

/*
========================
idSoundChannel::CheckForCompletion
========================
*/
bool idSoundChannel::CheckForCompletion( int currentTime )
{
	return SoundChannelHasCompleted( this, currentTime );
}

/*
========================
idSoundChannel::UpdateVolume
========================
*/
// A lipSync some content declares. The game makes up an empty default when it
// looks up a name nothing declares (actor speech and map precache do), which
// would otherwise hide a sample-path transcript behind the shader's name.
static const rvDeclLipSync* SoundDeclaredLipSync( const char* name ) {
	const rvDeclLipSync* lip = static_cast<const rvDeclLipSync*>( declManager->FindType( DECL_LIPSYNC, name, false ) );
	return lip != NULL && !lip->IsImplicit() ? lip : NULL;
}

// Text a subtitle may show: a resolved string, never a missing localization key.
static bool SoundSubtitleTextUsable( const char* text ) {
	return text != NULL && text[0] && idStr::Icmpn( text, "#str_", 5 ) && idStr::Icmp( text, "<DEFAULTED>" );
}

// The transcript a playing sample may show. Authored caption declarations come
// first: a lipSync named "subtitle/<sample path without extension>" or, for a
// single-sample shader's leadin, "subtitle/<shader>". A caption pack uses them to
// fill lines the installed game has no text for, or to correct one, without
// redefining a declaration actor lip sync reads. One whose text does not resolve
// falls back to the installed data. There, one shader can choose different
// utterances: only a single-sample shader has an unambiguous same-name
// transcript, for its leadin; variants need an explicit full sample-path
// lipSync declaration. Descriptions, filenames and missing localization keys
// are never transcripts. status, when asked for, says why there is no text.
static const char* SoundTranscriptText( const idSoundShader* shader, const idSoundSample* sample, bool leadin, SoundTranscript* status ) {
	const bool single = shader->GetNumEntries() == 1 && leadin;
	idStr key = sample->GetName(); key.StripFileExtension();
	idStr authoredSample( "subtitle/" ); authoredSample += key;
	idStr authoredShader( "subtitle/" ); authoredShader += shader->GetName();
	const char* authoredNames[2] = { authoredSample.c_str(), single ? authoredShader.c_str() : NULL };
	for( const char* name : authoredNames ) {
		const rvDeclLipSync* authored = name != NULL ? SoundDeclaredLipSync( name ) : NULL;
		const char* text = authored != NULL && authored->GetRawTranscribeText()[0] ? authored->GetSubtitleText() : NULL;
		if( SoundSubtitleTextUsable( text ) ) {
			if( status != NULL ) *status = SoundTranscript::Authored;
			return text;
		}
	}
	const rvDeclLipSync* lip = NULL;
	if( single )
		lip = SoundDeclaredLipSync( shader->GetName() );
	if( lip == NULL )
		lip = SoundDeclaredLipSync( key.c_str() );
	if( lip == NULL ) {
		if( status != NULL )
			*status = !single && SoundDeclaredLipSync( shader->GetName() ) != NULL ? SoundTranscript::Variant : SoundTranscript::Undeclared;
		return NULL;
	}
	if( !lip->GetRawTranscribeText()[0] ) {
		if( status != NULL ) *status = SoundTranscript::NoText;
		return NULL;
	}
	const char* text = lip->GetSubtitleText();
	if( !SoundSubtitleTextUsable( text ) ) {
		if( status != NULL ) *status = SoundTranscript::Unresolved;
		return NULL;
	}
	if( status != NULL ) *status = SoundTranscript::Text;
	return text;
}

// The caption a channel shows now, if any. Captions follow authored playback,
// not the user's volume or hardware-voice allocation, so they remain useful with
// dialogue muted or a lost audio device. A line that has reached its end shows
// its last words until the mixer frees the channel. Speech without a transcript
// gives a cue with no text; the caller decides whether to show it.
static bool SoundChannelSubtitle( idSoundWorldLocal* world, idSoundEmitterLocal* emitter, idSoundChannel* channel, const int now, soundSubtitle_t& cue ) {
	if( channel == NULL || channel->soundShader == NULL || channel->leadinSample == NULL || now < channel->startTime ) return false;
	const int flags = channel->parms.soundShaderFlags;
	const bool self = emitter->emitterId == world->listener.id;
	if( ( ( flags & SSF_PRIVATE_SOUND ) && !self ) || ( ( flags & SSF_ANTI_PRIVATE_SOUND ) && self ) ) return false;
	int cursor = idMath::FtoiFast( SoundChannelPlaybackTimeMsec( channel, now ) );
	const idSoundSample* sample = channel->leadinSample;
	if( cursor >= sample->LengthInMsec() ) {
		// Looping shaders always carry their loop sample; anything else has ended.
		if( channel->loopingSample != NULL && channel->loopingSample->LengthInMsec() > 0 ) {
			cursor -= sample->LengthInMsec();
			sample = channel->loopingSample;
			cursor %= sample->LengthInMsec();
		} else {
			cursor = sample->LengthInMsec() - 1;
		}
	}
	if( sample->IsDefault() || sample->LengthInMsec() <= 0 ||
		!SoundIsDialogue( SoundUsesMusicVolume( channel->parms ),
			( flags & ( SSF_IS_VO | SSF_VO_FOR_PLAYER ) ) != 0, sample->GetName() ) ) return false;
	float audible = channel->parms.volume * channel->volumeFade.GetVolume( now );
	if( channel->parms.soundClass >= 0 && channel->parms.soundClass < SOUND_MAX_CLASSES )
		audible *= world->soundClassFade[channel->parms.soundClass].GetVolume( now );
	if( !( flags & SSF_GLOBAL ) && !self ) {
		const bool direct = ( flags & SSF_NO_OCCLUSION ) || !s_useOcclusion.GetBool();
		SoundWorldApplyDistanceFalloff( audible, direct ? ( emitter->origin - world->listener.pos ).Length() : emitter->spatializedDistance,
			channel->parms.minDistance, channel->parms.maxDistance );
	}
	if( !std::isfinite( audible ) || audible <= SOUND_WORLD_VOLUME_EPSILON ) return false;
	SoundTranscript transcript = SoundTranscript::Undeclared;
	const char* text = SoundTranscriptText( channel->soundShader, sample, sample == channel->leadinSample, &transcript );
	cue = { text, channel->soundShader->GetName(), emitter->Index(), emitter->emitterId,
		channel->startTime, cursor, sample->LengthInMsec(), IsRadioChatterChannel( channel ),
		text != NULL, transcript == SoundTranscript::Authored };
	return true;
}

// Keep the newest cues, newest first. Stable source order breaks simultaneous ties.
static void SoundKeepNewestSubtitle( soundSubtitle_t* cues, int& count, const int capacity, const soundSubtitle_t& cue ) {
	int at = 0;
	while( at < count && cues[at].startTime >= cue.startTime ) ++at;
	if( at >= capacity ) return;
	for( int j = Min( count, capacity - 1 ); j > at; --j ) cues[j] = cues[j - 1];
	cues[at] = cue;
	count = Min( capacity, count + 1 );
}

// A line ended on its own: hold its last words (or its untranscribed marker) for
// SOUND_SUBTITLE_TAIL_MSEC, and a short line until it has been up
// SOUND_SUBTITLE_MIN_DISPLAY_MSEC. Stops and replacements free channels without
// passing here, so an interrupted line still clears at once.
void idSoundWorldLocal::RememberSubtitleTail( idSoundEmitterLocal* emitter, idSoundChannel* channel, const int now ) {
	soundSubtitle_t cue;
	if( emitter == NULL || !SoundChannelSubtitle( this, emitter, channel, now, cue ) ) return;
	const int shown = Max( 0, now - cue.startTime );
	const int until = now + Min( SOUND_SUBTITLE_MIN_DISPLAY_MSEC, Max( SOUND_SUBTITLE_TAIL_MSEC, SOUND_SUBTITLE_MIN_DISPLAY_MSEC - shown ) );
	// Replace this emitter's older tail, else the one that ends first.
	int slot = numSubtitleTails < SOUND_SUBTITLE_TAILS ? numSubtitleTails : 0;
	for( int i = 0; i < numSubtitleTails; ++i ) {
		if( subtitleTails[i].emitterIndex == cue.emitterIndex ) { slot = i; break; }
		if( numSubtitleTails == SOUND_SUBTITLE_TAILS && subtitleTails[i].until < subtitleTails[slot].until ) slot = i;
	}
	soundSubtitleTail_t& tail = subtitleTails[slot];
	tail.text = cue.text != NULL ? cue.text : "";
	tail.shader = channel->soundShader;
	tail.emitterIndex = cue.emitterIndex;
	tail.logicalChannel = channel->logicalChannel;
	tail.speakerId = cue.speakerId;
	tail.startTime = cue.startTime;
	tail.durationMsec = cue.durationMsec;
	tail.until = until;
	tail.radio = cue.radio;
	tail.transcribed = cue.transcribed;
	tail.authored = cue.authored;
	if( slot == numSubtitleTails ) ++numSubtitleTails;
}

void idSoundWorldLocal::ForgetSubtitleTails( const int emitterIndex, const int logicalChannel ) {
	for( int i = numSubtitleTails - 1; i >= 0; --i ) {
		if( emitterIndex >= 0 && subtitleTails[i].emitterIndex != emitterIndex ) continue;
		if( logicalChannel != SCHANNEL_ANY && subtitleTails[i].logicalChannel != logicalChannel ) continue;
		subtitleTails[i] = subtitleTails[numSubtitleTails - 1];
		--numSubtitleTails;
	}
}

// The two newest transcribed lines, with untranscribed speech marked in any slot
// they leave while ui_subtitleUntranscribed is set. No stale event queue survives
// stops or restore; held tails yield to a new line from the same emitter.
int idSoundWorldLocal::GetSubtitles( soundSubtitle_t* cues, int capacity ) {
	if( cues == NULL || capacity <= 0 ) return 0;
	capacity = Min( capacity, 2 );
	const int now = GetSoundTime();
	const bool untranscribed = ui_subtitleUntranscribed.GetBool();
	soundSubtitle_t spoken[2], marked[2];
	int numSpoken = 0, numMarked = 0;
	int liveEmitters[64], liveStarts[64], numLive = 0;
	for( int e = 0; e < emitters.Num(); ++e ) {
		idSoundEmitterLocal* emitter = emitters[e];
		if( emitter == NULL ) continue;
		for( int c = 0; c < emitter->channels.Num(); ++c ) {
			soundSubtitle_t cue;
			if( !SoundChannelSubtitle( this, emitter, emitter->channels[c], now, cue ) ) continue;
			if( numLive < 64 ) { liveEmitters[numLive] = cue.emitterIndex; liveStarts[numLive] = cue.startTime; ++numLive; }
			if( cue.transcribed ) SoundKeepNewestSubtitle( spoken, numSpoken, capacity, cue );
			else if( untranscribed ) SoundKeepNewestSubtitle( marked, numMarked, capacity, cue );
		}
	}
	for( int i = numSubtitleTails - 1; i >= 0; --i ) {
		const soundSubtitleTail_t& tail = subtitleTails[i];
		bool replaced = now >= tail.until || now < tail.startTime;
		for( int j = 0; j < numLive && !replaced; ++j )
			replaced = liveEmitters[j] == tail.emitterIndex && liveStarts[j] >= tail.startTime;
		if( replaced ) {
			subtitleTails[i] = subtitleTails[numSubtitleTails - 1];
			--numSubtitleTails;
			continue;
		}
		const soundSubtitle_t cue = { tail.transcribed ? tail.text.c_str() : NULL, tail.shader->GetName(), tail.emitterIndex, tail.speakerId,
			tail.startTime, tail.durationMsec - 1, tail.durationMsec, tail.radio, tail.transcribed, tail.authored };
		if( cue.transcribed ) SoundKeepNewestSubtitle( spoken, numSpoken, capacity, cue );
		else if( untranscribed ) SoundKeepNewestSubtitle( marked, numMarked, capacity, cue );
	}
	int count = 0;
	for( int i = 0; i < numSpoken; ++i ) SoundKeepNewestSubtitle( cues, count, capacity, spoken[i] );
	for( int i = 0; i < numMarked && count < capacity; ++i ) SoundKeepNewestSubtitle( cues, count, capacity, marked[i] );
	return count;
}

// Report routing for the channel playing now. A sample can be dialogue on one
// channel and an effect on another; a map-wide list of speech samples cannot
// establish whether the Dialogue Volume control muted the right voices.
void SoundDialogueVoices_f( const idCmdArgs& args )
{
	(void)args;
	idSoundWorldLocal* world = static_cast<idSoundWorldLocal*>( soundSystemLocal.GetSoundWorldFromId( SOUNDWORLD_GAME ) );
	int count = 0;
	if( world != NULL ) {
		const int now = world->GetSoundTime();
		for( int e = 0; e < world->emitters.Num(); ++e ) {
			idSoundEmitterLocal* emitter = world->emitters[e];
			if( emitter == NULL ) continue;
			for( int c = 0; c < emitter->channels.Num(); ++c ) {
				idSoundChannel* channel = emitter->channels[c];
				// Include a logically completed channel until the next mix retires
				// its hardware voice: OpenAL may still report it playing now.
				if( channel == NULL || channel->soundShader == NULL || channel->leadinSample == NULL ||
					channel->hardwareVoice == NULL ) continue;
				const idSoundSample* sample = channel->leadinSample;
				if( channel->loopingSample != NULL && SoundChannelPlaybackTimeMsec( channel, now ) >= sample->LengthInMsec() ) {
					sample = channel->loopingSample;
				}
				const bool authored = ( channel->parms.soundShaderFlags & ( SSF_IS_VO | SSF_VO_FOR_PLAYER ) ) != 0;
				const bool dialogue = SoundIsDialogue( SoundUsesMusicVolume( channel->parms ), authored, sample->GetName() );
				SoundTranscript transcript = SoundTranscript::Undeclared;
				if( dialogue ) SoundTranscriptText( channel->soundShader, sample, sample == channel->leadinSample, &transcript );
				idLib::Printf( "SOUND_DIALOGUE emitter=%d channel=%d shader=%s sample=%s dialogue=%d transcript=%s\n",
					emitter->Index(), channel->logicalChannel, channel->soundShader->GetName(), sample->GetName(),
					dialogue ? 1 : 0, SoundTranscriptName( transcript ) );
				soundSystemLocal.hardware.PrintDistanceInfo( channel->hardwareVoice );
				++count;
			}
		}
	}
	idLib::Printf( "SOUND_DIALOGUE_END channels=%d\n", count );
}

void idSoundChannel::UpdateVolume( int currentTime )
{
	idSoundWorldLocal* soundWorld = emitter->soundWorld;

	volumeDB = DB_SILENCE;
	currentAmplitude = 0.0f;

	if( leadinSample == NULL )
	{
		return;
	}
	if( startTime > currentTime )
	{
		return;
	}
	if( SoundChannelHasCompleted( this, currentTime ) )
	{
		return;
	}

	// if you don't want to hear all the beeps from missing sounds
	if( leadinSample->IsDefault() && !s_playDefaultSound.GetBool() )
	{
		return;
	}

	bool emitterIsListener = ( emitter->emitterId == soundWorld->listener.id );

	// if it is a private sound, set the volume to zero unless we match the listener.id
	if( parms.soundShaderFlags & SSF_PRIVATE_SOUND )
	{
		if( !emitterIsListener )
		{
			return;
		}
	}
	if( parms.soundShaderFlags & SSF_ANTI_PRIVATE_SOUND )
	{
		if( emitterIsListener )
		{
			return;
		}
	}

	float volumeScale = parms.volume;
	if( soundShader != NULL && leadinSample != NULL && soundShader->leadinVolume != 1.0f )
	{
		const int leadinLength = leadinSample->LengthInMsec();
		const int relativeTime = idMath::FtoiFast( SoundChannelPlaybackTimeMsec( this, currentTime ) );
		if( relativeTime >= 0 && relativeTime < leadinLength )
		{
			volumeScale = soundShader->leadinVolume;
		}
	}

	volumeScale *= volumeFade.GetVolume( currentTime );
	volumeScale *= soundWorld->volumeFade.GetVolume( currentTime );
	volumeScale *= soundWorld->pauseFade.GetVolume( currentTime );
	if( parms.soundClass >= 0 && parms.soundClass < SOUND_MAX_CLASSES )
	{
		volumeScale *= soundWorld->soundClassFade[parms.soundClass].GetVolume( currentTime );
	}

	bool global = ( parms.soundShaderFlags & SSF_GLOBAL ) != 0;

	// attenuation
	if( !global && !emitterIsListener )
	{
		const bool noOcclusion = ( parms.soundShaderFlags & SSF_NO_OCCLUSION ) != 0 || !s_useOcclusion.GetBool();
		const float distance = noOcclusion ? emitter->directDistance : emitter->spatializedDistance;
		SoundWorldApplyDistanceFalloff( volumeScale, distance, parms.minDistance, parms.maxDistance );

		// OpenQ4 keeps the SDK's SSF_IS_VO name for bit 13; retail uses the same bit to bypass
		// this extra speaker/radio attenuation pass.
		if( ( parms.soundShaderFlags & SSF_IS_VO ) == 0 && ReceivesQuake4ExtraAttenuation( this ) )
		{
			if( IsRadioChatterChannel( this ) )
			{
				volumeScale *= SoundSanitizeUnitValue( s_radioChatterFraction.GetFloat(), 1.0f );
			}
			else
			{
				volumeScale *= SoundSanitizeUnitValue( s_speakerFraction.GetFloat(), 1.0f );
			}
		}
	}

	if( SoundUsesMusicVolume( parms ) )
	{
		volumeScale *= SoundSanitizeUnitValue( s_musicVolume.GetFloat(), 1.0f );
	}
	else
	{
		// Classify the sample actually playing, including a leadin/loop change.
		// This also survives save/restore and localized fallback without storing
		// a new flag in saves or changing retail attenuation/voice priority.
		const idSoundSample* sample = leadinSample;
		if( loopingSample != NULL && SoundChannelPlaybackTimeMsec( this, currentTime ) >= leadinSample->LengthInMsec() )
		{
			sample = loopingSample;
		}
		const bool authoredSpeech = ( parms.soundShaderFlags & ( SSF_IS_VO | SSF_VO_FOR_PLAYER ) ) != 0;
		if( SoundIsDialogue( false, authoredSpeech, sample->GetName() ) )
		{
			volumeScale *= SoundSanitizeUnitValue( s_dialogueVolume.GetFloat(), 1.0f );
		}
	}

	if( soundSystemLocal.musicMuted && SoundUsesMusicVolume( parms ) )
	{
		volumeScale = 0.0f;
	}

	volumeScale = SoundSanitizeGainScale( volumeScale, 0.0f );
	volumeScale = SoundSanitizeGainScale( volumeScale * SoundSanitizeGainScale( s_volume.GetFloat(), 0.0f ), 0.0f );

	// store the new volume on the channel
	volumeDB = volumeScale < SOUND_WORLD_VOLUME_EPSILON ? DB_SILENCE : VolumeScaleToDB( volumeScale );

	// The voice cannot report its level (OpenAL has no meter), so read the sample's envelope.
	if( hardwareVoice != NULL )
	{
		currentAmplitude = ( parms.soundShaderFlags & SSF_NO_FLICKER ) != 0 ? 1.0f : SoundChannelSampleAmplitude( this, currentTime );
	}
}

/*
========================
idSoundChannel::UpdateHardware
========================
*/
void idSoundChannel::UpdateHardware( float volumeAdd, int currentTime )
{
	idSoundWorldLocal* soundWorld = emitter->soundWorld;

	if( soundWorld == NULL )
	{
		return;
	}
	if( leadinSample == NULL )
	{
		return;
	}
	if( startTime > currentTime )
	{
		return;
	}
	if( SoundChannelHasCompleted( this, currentTime ) )
	{
		return;
	}
	const int playbackTime = idMath::FtoiFast( SoundChannelPlaybackTimeMsec( this, currentTime ) );

	// convert volumes from decibels to linear
	const float mixedVolumeDB = volumeDB + volumeAdd;
	float volume = mixedVolumeDB <= DB_SILENCE ? 0.0f : Max( 0.0f, DBtoLinear( mixedVolumeDB ) );
	if( FLOAT_IS_NAN( volume ) )
	{
		volume = 0.0f;
	}
	volume = SoundSanitizeGainScale( volume, 0.0f );

	if( ( parms.soundShaderFlags & SSF_UNCLAMPED ) == 0 )
	{
		volume = Min( 1.0f, volume );
	}

	bool global = ( parms.soundShaderFlags & SSF_GLOBAL ) != 0;
	bool omni = ( parms.soundShaderFlags & SSF_OMNIDIRECTIONAL ) != 0;
	bool emitterIsListener = ( emitter->emitterId == soundWorld->listener.id );

	int startOffset = 0;
	bool issueStart = false;

	if( hardwareVoice == NULL )
	{
		if( volume <= 0.00001f )
		{
			return;
		}

		hardwareVoice = soundSystemLocal.AllocateVoice( leadinSample, loopingSample );

		if( hardwareVoice == NULL )
		{
			return;
		}

		issueStart = true;
		startOffset = playbackTime;
	}

	if( omni || global || emitterIsListener )
	{
		hardwareVoice->SetPosition( vec3_zero );
		hardwareVoice->SetVelocity( vec3_zero );
	}
	else
	{
		hardwareVoice->SetPosition( ( emitter->spatializedOrigin - soundWorld->listener.pos ) * soundWorld->listener.axis.Transpose() );
		if( ( parms.soundShaderFlags & SSF_USEDOPPLER ) != 0 )
		{
			// OpenAL expects relative sources to provide velocity in listener space too.
			hardwareVoice->SetVelocity( emitter->velocity * soundWorld->listener.axis.Transpose() );
		}
		else
		{
			hardwareVoice->SetVelocity( vec3_zero );
		}
	}
	if( parms.soundShaderFlags & SSF_CENTER )
	{
		hardwareVoice->SetCenterChannel( 1.0f );
	}
	else if( ( parms.soundShaderFlags & ( SSF_VO | SSF_IS_VO | SSF_VO_FOR_PLAYER ) ) || IsRadioChatterChannel( this ) ||
		SoundIsDialogue( SoundUsesMusicVolume( parms ), false, leadinSample->GetName() ) )
	{
		hardwareVoice->SetCenterChannel( s_centerFractionVO.GetFloat() );
	}
	else
	{
		hardwareVoice->SetCenterChannel( 0.0f );
	}

	hardwareVoice->SetGain( volume );
	hardwareVoice->SetInnerRadius( parms.minDistance * METERS_TO_DOOM );
	const float wetLevel = SoundSanitizeUnitValue( parms.wetLevel, 0.0f );
	const float dryLevel = SoundSanitizeUnitValue( parms.dryLevel, 1.0f );
	// A generic room tail surviving more strongly than the muffled direct path makes submerged
	// audio read as a large hall. Suppress only liquid-related wet sends; ordinary portal
	// occlusion, room acoustics, global announcements, UI and music retain their authored mix.
	const bool liquidAcoustics = !global &&
		( soundWorld->underwaterActive ||
		  ( emitter->crossesLiquidBoundary && ( parms.soundShaderFlags & SSF_NO_OCCLUSION ) == 0 ) );
	hardwareVoice->SetWetLevel( wetLevel * ( liquidAcoustics ? 0.125f : 1.0f ) );
	hardwareVoice->SetDryLevel( dryLevel );
	hardwareVoice->SetPitch( SoundChannelPlaybackRate( this ) );

	// Retail EAX source state (Quake4.exe 1.4.2): every mono voice feeds the reverb slot of its
	// own area and the listener's slot. The radio channel stays out of the room and voice-over
	// is pulled back; while EAX reverb is on, each blocking portal on the traced path occludes
	// the direct sound and the listener-room send by 1500 mB.
	int reverbRoomMB = 0;
	if( IsRadioChatterChannel( this ) )
	{
		reverbRoomMB = SOUND_REVERB_ROOM_RADIO_MB;
	}
	else if( ( parms.soundShaderFlags & SSF_IS_VO ) != 0 )
	{
		reverbRoomMB = SOUND_REVERB_ROOM_VOICEOVER_MB;
	}
	int reverbOcclusionMB = 0;
	if( soundSystemLocal.hardware.HasEFX() && s_useEAXOcclusion.GetBool() && s_useOcclusion.GetBool() &&
		( parms.soundShaderFlags & SSF_NO_OCCLUSION ) == 0 && emitter->occludingPortalCount > 0 )
	{
		reverbOcclusionMB = SOUND_REVERB_OCCLUSION_PER_PORTAL_MB * emitter->occludingPortalCount;
	}
	hardwareVoice->SetReverbSource( soundSystemLocal.reverb.SlotForArea( emitter->lastValidPortalArea ), reverbRoomMB,
		reverbOcclusionMB, leadinSample->NumChannels() == 1 );

	// openQ4: a surface between the emitter and the ear is a far stronger barrier than a doorway
	float liquidOcclusion = 0.0f;
	if( emitter->crossesLiquidBoundary && ( parms.soundShaderFlags & SSF_NO_OCCLUSION ) == 0 )
	{
		liquidOcclusion = SOUND_LIQUID_BOUNDARY_OCCLUSION;
	}

	hardwareVoice->SetOcclusion( liquidOcclusion );
	// openQ4: the enviro suit and being underwater both muffle the mix; take whichever is heavier
	// rather than letting one cancel the other out
	float environmentMuffle = soundWorld->enviroSuitActive ? SOUND_ENVIROSUIT_OCCLUSION : 0.0f;
	if( !global && soundWorld->underwaterActive && SOUND_UNDERWATER_OCCLUSION > environmentMuffle )
	{
		environmentMuffle = SOUND_UNDERWATER_OCCLUSION;
	}
	hardwareVoice->SetEnvironmentMuffle( environmentMuffle );
	hardwareVoice->CommitMix();

	if( issueStart )
	{
		if( !hardwareVoice->Start( startOffset, parms.soundShaderFlags | ( parms.shakes == 0.0f ? SSF_NO_FLICKER : 0 ) ) )
		{
			const bool recoverAtClock = hardwareVoice->RecoversAtClock();
			soundSystemLocal.FreeVoice( hardwareVoice );
			hardwareVoice = NULL;
			if( !IsLooping() && !recoverAtClock )
			{
				endTime = 1;
			}
		}
	}
	else
	{
		if( !hardwareVoice->Update() )
		{
			const bool recoverAtClock = hardwareVoice->RecoversAtClock();
			soundSystemLocal.FreeVoice( hardwareVoice );
			hardwareVoice = NULL;
			if( !IsLooping() && !recoverAtClock )
			{
				endTime = 1;
			}
		}
	}
}

/*
================================================================================================

	idSoundEmitterLocal

================================================================================================
*/

/*
========================
idSoundEmitterLocal::idSoundEmitterLocal
========================
*/
idSoundEmitterLocal::idSoundEmitterLocal()
{
	Init( 0, NULL );
}

/*
========================
idSoundEmitterLocal::~idSoundEmitterLocal
========================
*/
idSoundEmitterLocal::~idSoundEmitterLocal()
{
	assert( channels.Num() == 0 );
}

/*
========================
idSoundEmitterLocal::Clear
========================
*/
void idSoundEmitterLocal::Init( int i, idSoundWorldLocal* sw )
{
	index = i;
	soundWorld = sw;

	// Init should only be called on a freshly constructed sound emitter or in a Reset()
	assert( channels.Num() == 0 );

	canFree = false;
	origin.Zero();
	velocity.Zero();
	emitterId = 0;

	directDistance = 0.0f;
	lastValidPortalArea = -1;
	spatializedDistance = 0.0f;
	spatializedOrigin.Zero();
	occludingPortalCount = 0;
	crossesLiquidBoundary = false;

	memset( &parms, 0, sizeof( parms ) );
	memset( recentShaders, 0, sizeof( recentShaders ) );
	memset( recentSamples, 0, sizeof( recentSamples ) );
	recentNext = 0;

	if( soundWorld && soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_ALLOC_EMITTER );
		soundWorld->writeDemo->WriteInt( index );
	}
}

/*
========================
idSoundEmitterLocal::Reset
========================
*/
void idSoundEmitterLocal::Reset()
{
	for( int i = 0; i < channels.Num(); i++ )
	{
		soundWorld->FreeSoundChannel( channels[i] );
	}
	channels.Clear();
	Init( index, soundWorld );
}

/*
==================
idSoundEmitterLocal::OverrideParms
==================
*/
void idSoundEmitterLocal::OverrideParms( const soundShaderParms_t* base, const soundShaderParms_t* over, soundShaderParms_t* out )
{
	if( !over )
	{
		*out = *base;
		return;
	}
	if( over->minDistance )
	{
		out->minDistance = over->minDistance;
	}
	else
	{
		out->minDistance = base->minDistance;
	}
	if( over->maxDistance )
	{
		out->maxDistance = over->maxDistance;
	}
	else
	{
		out->maxDistance = base->maxDistance;
	}
	if( over->shakes )
	{
		out->shakes = over->shakes;
	}
	else
	{
		out->shakes = base->shakes;
	}
	if( over->volume )
	{
		out->volume = over->volume;
	}
	else
	{
		out->volume = base->volume;
	}
	if( over->attenuatedVolume )
	{
		out->attenuatedVolume = over->attenuatedVolume;
	}
	else
	{
		out->attenuatedVolume = base->attenuatedVolume;
	}
	if( over->soundClass )
	{
		out->soundClass = over->soundClass;
	}
	else
	{
		out->soundClass = base->soundClass;
	}
	if( over->frequencyShift )
	{
		out->frequencyShift = over->frequencyShift;
	}
	else
	{
		out->frequencyShift = base->frequencyShift;
	}
	if( over->wetLevel )
	{
		out->wetLevel = over->wetLevel;
	}
	else
	{
		out->wetLevel = base->wetLevel;
	}
	if( over->dryLevel )
	{
		out->dryLevel = over->dryLevel;
	}
	else
	{
		out->dryLevel = base->dryLevel;
	}
	out->soundShaderFlags = base->soundShaderFlags | over->soundShaderFlags;
}

/*
========================
idSoundEmitterLocal::CheckForCompletion

Checks to see if any of the channels have completed, removing them as they do

This will also play any postSounds on the same channel as their owner.

Returns true if the emitter should be freed.
========================
*/
bool idSoundEmitterLocal::CheckForCompletion( int currentTime )
{
	for( int i = channels.Num() - 1; i >= 0 ; i-- )
	{
		idSoundChannel* chan = channels[i];

		if( chan->CheckForCompletion( currentTime ) )
		{
			soundWorld->RememberSubtitleTail( this, chan, currentTime );
			channels.RemoveIndex( i );
			soundWorld->FreeSoundChannel( chan );
		}
	}
	return ( canFree && channels.Num() == 0 );
}

/*
========================
idSoundEmitterLocal::Update
========================
*/
void idSoundEmitterLocal::Update( int currentTime )
{
	if( channels.Num() == 0 )
	{
		return;
	}

	directDistance = ( soundWorld->listener.pos - origin ).LengthFast() * DOOM_TO_METERS;

	spatializedDistance = directDistance;
	spatializedOrigin = origin;
	occludingPortalCount = 0;
	crossesLiquidBoundary = false;

	// Initialize all channels to silence
	for( int i = 0; i < channels.Num(); i++ )
	{
		channels[i]->volumeDB = DB_SILENCE;
	}

	if( s_singleEmitter.GetInteger() > 0 && s_singleEmitter.GetInteger() != index )
	{
		return;
	}
	if( soundWorld->listener.area == -1 )
	{
		// listener is outside the world
		return;
	}
	if( soundSystemLocal.muted || soundWorld != soundSystemLocal.currentSoundWorld )
	{
		return;
	}
	float maxDistance = 0.0f;
	bool maxDistanceValid = false;
	bool useOcclusion = false;
	// Retail traces portals for every playing channel, global ones included: their
	// EAX occlusion follows the blocking portals between emitter and listener.
	bool globalOcclusion = false;
	float occlusionDistance = 0.0f;
	if( emitterId != soundWorld->listener.id )
	{
		for( int i = 0; i < channels.Num(); i++ )
		{
			idSoundChannel* chan = channels[i];
			const bool occludes = ( chan->parms.soundShaderFlags & SSF_NO_OCCLUSION ) == 0;
			if( occludes && occlusionDistance < chan->parms.maxDistance )
			{
				occlusionDistance = chan->parms.maxDistance;
			}
			if( ( chan->parms.soundShaderFlags & SSF_GLOBAL ) != 0 )
			{
				globalOcclusion = globalOcclusion || occludes;
				continue;
			}
			useOcclusion = useOcclusion || occludes;
			maxDistanceValid = true;
			if( maxDistance < channels[i]->parms.maxDistance )
			{
				maxDistance = channels[i]->parms.maxDistance;
			}
		}
	}
	if( maxDistanceValid && directDistance >= maxDistance )
	{
		// Too far away to hear any positional channel. maxDistance only counts those, and
		// a global channel on the same emitter plays at any distance, so it still updates.
		for( int j = 0; j < channels.Num(); j++ )
		{
			if( ( channels[j]->parms.soundShaderFlags & SSF_GLOBAL ) != 0 )
			{
				channels[j]->UpdateVolume( currentTime );
			}
		}
		return;
	}
	if( soundWorld->renderWorld != NULL )
	{
		// Retail Spatialize: every emitter in range keeps the area it is in, which
		// the area reverb routing reads even when occlusion is off.
		int soundInArea = soundWorld->renderWorld->PointInArea( origin );
		if( soundInArea == -1 )
		{
			soundInArea = lastValidPortalArea;
		}
		else
		{
			lastValidPortalArea = soundInArea;
		}

		// work out virtual origin and distance, which may be from a portal instead of the actual origin
		if( ( useOcclusion || globalOcclusion ) && s_useOcclusion.GetBool() )
		{
			if( soundInArea != -1 && soundInArea != soundWorld->listener.area )
			{
				spatializedDistance = Max( maxDistance, occlusionDistance ) * METERS_TO_DOOM;
				soundWorld->ResolveOrigin( 0, NULL, soundInArea, 0.0f, 0, origin, this );
				spatializedDistance *= DOOM_TO_METERS;
				if( !useOcclusion )
				{
					// traced only for the global channels' occlusion; positional
					// channels keep the direct path
					spatializedDistance = directDistance;
					spatializedOrigin = origin;
				}
			}
		}
		if( useOcclusion && s_useOcclusion.GetBool() )
		{
			// openQ4: does this sound have to cross a liquid surface to reach the ear? Asked here
			// because this is already the per-emitter geometry step, and it is the same thread.
			if( soundWorld->liquidTest != NULL )
			{
				const bool emitterSubmerged = soundWorld->liquidTest( origin );
				crossesLiquidBoundary = ( emitterSubmerged != soundWorld->underwaterActive );
			}
			else
			{
				crossesLiquidBoundary = false;
			}
		}
	}

	for( int j = 0; j < channels.Num(); j++ )
	{
		channels[j]->UpdateVolume( currentTime );
	}

	return;
}

/*
========================
idSoundEmitterLocal::Index
========================
*/
int idSoundEmitterLocal::Index() const
{
	assert( soundWorld );
	assert( soundWorld->emitters[this->index] == this );

	return index;
}

/*
========================
idSoundEmitterLocal::Free

Doesn't free it until the next update.
========================
*/
void idSoundEmitterLocal::Free( bool immediate )
{
	assert( soundWorld );
	assert( soundWorld->emitters[this->index] == this );

	if( canFree )
	{
		// Double free
		return;
	}
	if( soundWorld && soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_FREE );
		soundWorld->writeDemo->WriteInt( index );
		soundWorld->writeDemo->WriteInt( immediate );
	}

	if( immediate )
	{
		soundWorld->ForgetSubtitleTails( index );
		Reset();
	}

	canFree = true;
}

/*
========================
idSoundEmitterLocal::UpdateEmitter
========================
*/
void idSoundEmitterLocal::UpdateEmitter( const idVec3& origin, int listenerId, const soundShaderParms_t* parms )
{
	UpdateEmitter( origin, vec3_zero, listenerId, parms );
}

/*
========================
idSoundEmitterLocal::UpdateEmitter
========================
*/
void idSoundEmitterLocal::UpdateEmitter( const idVec3& origin, const idVec3& velocity, int listenerId, const soundShaderParms_t* parms )
{
	assert( soundWorld != NULL );
	assert( soundWorld->emitters[this->index] == this );

	if( soundWorld && soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_UPDATE );
		soundWorld->writeDemo->WriteInt( index );
		soundWorld->writeDemo->WriteVec3( origin );
		soundWorld->writeDemo->WriteVec3( velocity );
		soundWorld->writeDemo->WriteInt( listenerId );
		soundWorld->writeDemo->WriteFloat( parms->minDistance );
		soundWorld->writeDemo->WriteFloat( parms->maxDistance );
		soundWorld->writeDemo->WriteFloat( parms->volume );
		soundWorld->writeDemo->WriteFloat( parms->attenuatedVolume );
		soundWorld->writeDemo->WriteFloat( parms->shakes );
		soundWorld->writeDemo->WriteInt( parms->soundShaderFlags );
		soundWorld->writeDemo->WriteInt( parms->soundClass );
		soundWorld->writeDemo->WriteFloat( parms->frequencyShift );
		soundWorld->writeDemo->WriteFloat( parms->wetLevel );
		soundWorld->writeDemo->WriteFloat( parms->dryLevel );
	}

	this->origin = origin;
	this->velocity = velocity;
	this->emitterId = listenerId;
	this->parms = *parms;
}

/*
========================
idSoundEmitterLocal::StartSound

in most cases play sounds immediately, however
  intercept sounds using SSF_FINITE_SPEED_OF_SOUND
  and schedule them for playback later

return: int	- the length of the started sound in msec.
========================
*/
/*
========================
SoundLogSpeechStart

s_logSpeech: a start that may be speech, being classified dialogue, flagged
voice-over, on the radio channel or declared by a lipSync, with the routing the
mixer gives it and the subtitle it can show. Speech that is not dialogue would
escape the Dialogue Volume control. Identifiers only, never the transcript.
========================
*/
static void SoundLogSpeechStart( const idSoundChannel* chan )
{
	const idSoundShader* shader = chan->soundShader;
	const idSoundSample* sample = chan->leadinSample;
	const int flags = chan->parms.soundShaderFlags;
	const bool music = SoundUsesMusicVolume( chan->parms );
	const bool authored = ( flags & ( SSF_IS_VO | SSF_VO_FOR_PLAYER ) ) != 0;
	const bool dialogue = SoundIsDialogue( music, authored, sample->GetName() );
	const bool radio = IsRadioChatterChannel( chan );
	idStr key = sample->GetName();
	key.StripFileExtension();
	const bool declared = SoundDeclaredLipSync( shader->GetName() ) != NULL || SoundDeclaredLipSync( key.c_str() ) != NULL;
	if( !dialogue && !authored && !radio && !declared && ( flags & SSF_VO ) == 0 )
	{
		return;
	}
	SoundTranscript transcript = SoundTranscript::Undeclared;
	SoundTranscriptText( shader, sample, true, &transcript );
	idSoundWorldLocal* world = chan->emitter->soundWorld;
	idLib::Printf( "SOUND_SPEECH game=%d time=%d emitter=%d speaker=%d channel=%d shader=%s sample=%s dialogue=%d authored=%d vo=%d radio=%d music=%d global=%d private=%d declared=%d transcript=%s\n",
		world == soundSystemLocal.GetSoundWorldFromId( SOUNDWORLD_GAME ) ? 1 : 0, world->GetSoundTime(), chan->emitter->Index(),
		chan->emitter->emitterId, static_cast<int>( chan->logicalChannel ), shader->GetName(), sample->GetName(), dialogue ? 1 : 0,
		authored ? 1 : 0, ( flags & SSF_VO ) != 0 ? 1 : 0, radio ? 1 : 0, music ? 1 : 0, ( flags & SSF_GLOBAL ) != 0 ? 1 : 0,
		( flags & ( SSF_PRIVATE_SOUND | SSF_ANTI_PRIVATE_SOUND ) ) != 0 ? 1 : 0, declared ? 1 : 0, SoundTranscriptName( transcript ) );
}

int idSoundEmitterLocal::StartSound( const idSoundShader* shader, const s_channelType channel, float diversity, int shaderFlags, bool allowSlow )
{
	assert( soundWorld != NULL );
	assert( soundWorld->emitters[this->index] == this );

	if( shader == NULL || s_skipStartSound.GetBool() )
	{
		return 0;
	}

	if( soundWorld && soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_START );
		soundWorld->writeDemo->WriteInt( index );

		soundWorld->writeDemo->WriteHashString( shader->GetName() );

		soundWorld->writeDemo->WriteInt( channel );
		soundWorld->writeDemo->WriteFloat( diversity );
		soundWorld->writeDemo->WriteInt( shaderFlags );
	}

	if( s_noSound.GetBool() )
	{
		return 0;
	}

	int currentTime = soundWorld->GetSoundTime();

	bool showStartSound = s_showStartSound.GetBool();
	if( showStartSound )
	{
		idLib::Printf( "%dms: StartSound(%d:%d): %s: ", currentTime, index, channel, shader->GetName() );
	}

	// build the channel parameters by taking the shader parms and optionally overriding
	soundShaderParms_t	chanParms;
	chanParms = shader->parms;
	OverrideParms( &chanParms, &parms, &chanParms );
	chanParms.soundShaderFlags |= shaderFlags;

	if( shader->entries.Num() == 0 )
	{
		if( showStartSound )
		{
			idLib::Printf( S_COLOR_RED "No Entries\n" );
		}
		return 0;
	}

	// PLAY_ONCE sounds will never be restarted while they are running
	if( chanParms.soundShaderFlags & SSF_PLAY_ONCE )
	{
		for( int i = 0; i < channels.Num(); i++ )
		{
			idSoundChannel* chan = channels[i];
			if( chan->soundShader == shader && !chan->CheckForCompletion( currentTime ) )
			{
				if( showStartSound )
				{
					idLib::Printf( S_COLOR_YELLOW "Not started because of playOnce\n" );
				}
				return 0;
			}
		}
	}

	// never play the same sound twice with the same starting time, even
	// if they are on different channels
	for( int i = 0; i < channels.Num(); i++ )
	{
		idSoundChannel* chan = channels[i];
		if( chan->soundShader == shader && chan->startTime == currentTime && chan->endTime != 1 )
		{
			if( showStartSound )
			{
				idLib::Printf( S_COLOR_RED "Already started this frame\n" );
			}
			return 0;
		}
	}

	idSoundSample* leadinSample = NULL;
	idSoundSample* loopingSample = NULL;
	int choice = SoundSampleChoiceFromDiversity( shader->entries.Num(), diversity );

	if( ( chanParms.soundShaderFlags & SSF_NO_DUPS ) && shader->entries.Num() > 1 )
	{
		idSoundSample* selectedSample = NULL;
		if( choice < shader->leadins.Num() && shader->leadins[choice] != NULL )
		{
			selectedSample = shader->leadins[choice];
		}
		else
		{
			selectedSample = shader->entries[choice];
		}

		bool duplicate = false;
		for( int i = 0; i < channels.Num() && !duplicate; i++ )
		{
			duplicate = ( channels[i]->leadinSample == selectedSample );
		}
		// the last sample this shader started here, even if it has finished
		for( int i = 1; i <= RECENT_SAMPLES && !duplicate; i++ )
		{
			const int slot = ( recentNext - i + RECENT_SAMPLES ) % RECENT_SAMPLES;
			if( recentShaders[slot] == shader )
			{
				duplicate = ( recentSamples[slot] == selectedSample );
				break;
			}
		}
		if( duplicate )
		{
			choice = ( choice + 1 ) % shader->entries.Num();
		}
	}

	// kill any sound that is currently playing on this channel after no_dups sees the outgoing sample
	if( channel != SCHANNEL_ANY )
	{
		for( int i = 0; i < channels.Num(); i++ )
		{
			idSoundChannel* chan = channels[i];
			if( chan->soundShader && chan->logicalChannel == channel )
			{
				if( showStartSound )
				{
					idLib::Printf( S_COLOR_YELLOW "OVERRIDE %s: ", chan->soundShader->GetName() );
				}
				channels.RemoveIndex( i );
				soundWorld->FreeSoundChannel( chan );
				break;
			}
		}
	}

	if( choice < shader->leadins.Num() && shader->leadins[choice] != NULL )
	{
		leadinSample = shader->leadins[choice];
	}
	else
	{
		leadinSample = shader->entries[choice];
	}
	// An earlier load may have been refused under memory pressure. Retry at an
	// explicit playback request; never substitute a permanently cached beep.
	if( !leadinSample->IsLoaded() ) leadinSample->LoadResource();
	if( !leadinSample->IsLoaded() ) return 0;
	// Wall-clock, like the low-memory trim that reads it: worlds keep their own clocks.
	leadinSample->SetLastPlayedTime( Max( 1, Sys_Milliseconds() ) );

	if( chanParms.soundShaderFlags & SSF_LOOPING )
	{
		loopingSample = shader->entries[choice];
		if( loopingSample == NULL )
		{
			loopingSample = leadinSample;
		}
		if( !loopingSample->IsLoaded() ) loopingSample->LoadResource();
		if( !loopingSample->IsLoaded() ) return 0;
	}

	// set all the channel parameters here,
	// a hardware voice will be allocated next update if the volume is high enough to be audible
	if( channels.Num() == channels.Max() )
	{
		CheckForCompletion( currentTime );	// as a last chance try to release finished sounds here
		if( channels.Num() == channels.Max() )
		{
			if( showStartSound )
			{
				idLib::Printf( S_COLOR_RED "No free emitter channels!\n" );
			}
			return 0;
		}
	}
	idSoundChannel* chan = soundWorld->AllocSoundChannel();
	if( chan == NULL )
	{
		if( showStartSound )
		{
			idLib::Printf( S_COLOR_RED "No free global channels!\n" );
		}
		return 0;
	}
	channels.Append( chan );
	chan->emitter = this;
	chan->parms = chanParms;
	chan->soundShader = shader;
	chan->logicalChannel = channel;
	chan->choice = choice;
	chan->leadinSample = leadinSample;
	chan->loopingSample = loopingSample;
	chan->allowSlow = allowSlow;
	recentShaders[recentNext] = shader;
	recentSamples[recentNext] = leadinSample;
	recentNext = ( recentNext + 1 ) % RECENT_SAMPLES;
	chan->lastFrequencyShiftTime = 0;
	chan->lastFrequencyShift = 1.0f;
	chan->elapsedFrequencyShiftTime = 0.0f;

	// return length of sound in milliseconds
	int length = chan->leadinSample->LengthInMsec();

	// adjust the start time based on diversity for looping sounds, so they don't all start at the same point
	int startOffset = 0;

	if( chan->IsLooping() && ( ( chanParms.soundShaderFlags & SSF_NO_RANDOMSTART ) == 0 ) )
	{
		// Retail applies diversity to every looping shader, including leadin-then-loop pairs.
		startOffset = SoundLoopStartOffsetFromDiversity( length, diversity );
	}

	chan->startTime = currentTime - startOffset;

	if( ( chanParms.soundShaderFlags & SSF_LOOPING ) != 0 )
	{
		// This channel will never end!
		chan->endTime = 0;
	}
	else
	{
		// This channel will automatically end at this time
		chan->endTime = chan->startTime + idMath::FtoiFast( length / SoundChannelPlaybackRate( chan ) ) + 100;
	}
	if( s_logSpeech.GetBool() )
	{
		SoundLogSpeechStart( chan );
	}
	if( showStartSound )
	{
		if( loopingSample == NULL || leadinSample == loopingSample )
		{
			idLib::Printf( "Playing %s @ %d\n", leadinSample->GetName(), startOffset );
		}
		else
		{
			idLib::Printf( "Playing %s then looping %s\n", leadinSample->GetName(), loopingSample->GetName() );
		}
	}

	return length;
}


/*
========================
idSoundEmitterLocal::OnReloadSound

This is a shortened version of StartSound, called whenever a sound shader is reloaded.
If the emitter is currently playing the given sound shader, restart it so
a change in the sound sample used for a given sound shader will be picked up.
========================
*/
void idSoundEmitterLocal::OnReloadSound( const idDecl* decl )
{
}

/*
========================
idSoundEmitterLocal::StopSound

Can pass SCHANNEL_ANY.
========================
*/
void idSoundEmitterLocal::StopSound( const s_channelType channel )
{
	assert( soundWorld != NULL );
	assert( soundWorld->emitters[this->index] == this );

	if( soundWorld && soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_STOP );
		soundWorld->writeDemo->WriteInt( index );
		soundWorld->writeDemo->WriteInt( channel );
	}

	for( int i = channels.Num() - 1; i >= 0; i-- )
	{
		idSoundChannel* chan = channels[i];

		if( channel != SCHANNEL_ANY && chan->logicalChannel != channel )
		{
			continue;
		}
		if( s_showStartSound.GetBool() )
		{
			idLib::Printf( "%dms: StopSound(%d:%d): %s\n", soundWorld->GetSoundTime(), index, channel, chan->soundShader != NULL ? chan->soundShader->GetName() : "<null>" );
		}

		channels.RemoveIndex( i );
		soundWorld->FreeSoundChannel( chan );
	}
	// A stopped speaker's finished line goes with it.
	soundWorld->ForgetSubtitleTails( index, channel );
}

/*
========================
idSoundEmitterLocal::ModifySound
========================
*/
void idSoundEmitterLocal::ModifySound( const s_channelType channel, const soundShaderParms_t* parms )
{
	assert( soundWorld != NULL );
	assert( soundWorld->emitters[this->index] == this );

	if( soundWorld && soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_MODIFY );
		soundWorld->writeDemo->WriteInt( index );
		soundWorld->writeDemo->WriteInt( channel );
		soundWorld->writeDemo->WriteFloat( parms->minDistance );
		soundWorld->writeDemo->WriteFloat( parms->maxDistance );
		soundWorld->writeDemo->WriteFloat( parms->volume );
		soundWorld->writeDemo->WriteFloat( parms->attenuatedVolume );
		soundWorld->writeDemo->WriteFloat( parms->shakes );
		soundWorld->writeDemo->WriteInt( parms->soundShaderFlags );
		soundWorld->writeDemo->WriteInt( parms->soundClass );
		soundWorld->writeDemo->WriteFloat( parms->frequencyShift );
		soundWorld->writeDemo->WriteFloat( parms->wetLevel );
		soundWorld->writeDemo->WriteFloat( parms->dryLevel );
	}

	for( int i = channels.Num() - 1; i >= 0; i-- )
	{
		idSoundChannel* chan = channels[i];
		if( channel != SCHANNEL_ANY && chan->logicalChannel != channel )
		{
			continue;
		}
		if( s_showStartSound.GetBool() )
		{
			idLib::Printf( "%dms: ModifySound(%d:%d): %s\n", soundWorld->GetSoundTime(), index, channel, chan->soundShader->GetName() );
		}
		OverrideParms( &chan->parms, parms, &chan->parms );
	}
}

/*
========================
idSoundEmitterLocal::FadeSound
========================
*/
void idSoundEmitterLocal::FadeSound( const s_channelType channel, float to, float over )
{
	assert( soundWorld != NULL );
	assert( soundWorld->emitters[this->index] == this );

	if( soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_FADE );
		soundWorld->writeDemo->WriteInt( index );
		soundWorld->writeDemo->WriteInt( channel );
		soundWorld->writeDemo->WriteFloat( to );
		soundWorld->writeDemo->WriteFloat( over );
	}

	for( int i = 0; i < channels.Num(); i++ )
	{
		idSoundChannel* chan = channels[i];

		if( channel != SCHANNEL_ANY && chan->logicalChannel != channel )
		{
			continue;
		}
		if( s_showStartSound.GetBool() )
		{
			idLib::Printf( "%dms: FadeSound(%d:%d): %s to %.2fdb over %.2f seconds\n", soundWorld->GetSoundTime(), index, channel, chan->soundShader->GetName(), to, over );
		}

		// fade it
		chan->volumeFade.FadeDB( to, over, soundWorld->GetSoundTime() );
	}
}

/*
========================
idSoundEmitterLocal::CurrentlyPlaying
========================
*/
bool idSoundEmitterLocal::CurrentlyPlaying( const s_channelType channel ) const
{
	for( int i = 0; i < channels.Num(); ++i )
	{
		if( channels[i] == NULL || channels[i]->soundShader == NULL || channels[i]->endTime == 1 )
		{
			continue;
		}
		if( channel == SCHANNEL_ANY || channels[i]->logicalChannel == channel )
		{
			return true;
		}
	}

	return false;
}

/*
========================
idSoundEmitterLocal::CurrentAmplitude
========================
*/
float idSoundEmitterLocal::CurrentAmplitude()
{
	if( s_constantAmplitude.GetFloat() >= 0.0f )
	{
		return s_constantAmplitude.GetFloat();
	}

	float high = 0.0f;
	float sampleAmplitude = 0.0f;
	const int currentTime = soundWorld->GetSoundTime();
	for( int i = 0; i < channels.Num(); i++ )
	{
		idSoundChannel* chan = channels[i];
		if( chan == NULL || currentTime < chan->startTime || SoundChannelHasCompleted( chan, currentTime ) )
		{
			continue;
		}

		if( ( chan->parms.soundShaderFlags & SSF_NO_FLICKER ) != 0 )
		{
			high = Max( high, 32767.0f );
			continue;
		}

		const int relativeTime = idMath::FtoiFast( SoundChannelPlaybackTimeMsec( chan, currentTime ) );
		const char* shakeData = chan->soundShader != NULL ? chan->soundShader->GetShakeData( chan->choice ) : "";
		if( shakeData != NULL && shakeData[0] != '\0' )
		{
			int shakeDataLength = 0;
			while( shakeData[shakeDataLength] != '\0' )
			{
				shakeDataLength++;
			}

			int shakeIndex = SOUND_SHADER_SHAKE_RATE_HZ * relativeTime / 1000;
			if( ( chan->parms.soundShaderFlags & SSF_LOOPING ) != 0 )
			{
				shakeIndex %= shakeDataLength;
			}
			if( shakeIndex >= 0 && shakeIndex < shakeDataLength )
			{
				const int shakeValue = Max( 0, shakeData[shakeIndex] - 'a' );
				high = Max( high, shakeValue * SOUND_SHADER_MATERIAL_SHAKE_SCALE );
			}
			continue;
		}

		if( chan->leadinSample == NULL )
		{
			continue;
		}
		const int leadinLength = chan->leadinSample->LengthInMsec();
		if( relativeTime < leadinLength )
		{
			sampleAmplitude = Max( sampleAmplitude, chan->leadinSample->GetAmplitude( relativeTime ) );
		}
		else if( chan->loopingSample != NULL && chan->loopingSample->LengthInMsec() > 0 )
		{
			sampleAmplitude = Max( sampleAmplitude, chan->loopingSample->GetAmplitude( ( relativeTime - leadinLength ) % chan->loopingSample->LengthInMsec() ) );
		}
	}
	if( high > 0.0f )
	{
		return idMath::ATan( high * SOUND_SHADER_SHAKE_NORMALIZE, 1.0f ) / DEG2RAD( 45.0f );
	}
	return sampleAmplitude;
}

/*
========================
idSoundEmitterLocal::ShakeAmplitudeForPosition

Camera shake has its own authored scale and direct distance in Quake 4 map
units. Light flicker and the audio mix use different gains. Apply the volume
and shakes before the angle conversion, taking the strongest channel on each
emitter (retail Quake4.exe 1.4.2, FindAmplitude at 0x10173eb0).
========================
*/
float idSoundEmitterLocal::ShakeAmplitudeForPosition( const int currentTime, const idVec3& listenerPosition )
{
	const float distance = ( listenerPosition - origin ).Length();
	float high = 0.0f;
	for( int i = 0; i < channels.Num(); ++i )
	{
		idSoundChannel* chan = channels[i];
		if( chan == NULL || chan->soundShader == NULL || chan->parms.shakes <= 0.0f ||
			currentTime < chan->startTime || SoundChannelHasCompleted( chan, currentTime ) )
		{
			continue;
		}
		float volume = chan->parms.volume * chan->parms.shakes;
		if( ( chan->parms.soundShaderFlags & SSF_GLOBAL ) == 0 )
		{
			SoundWorldApplyDistanceFalloff( volume, distance, chan->parms.minDistance, chan->parms.maxDistance );
		}
		if( FLOAT_IS_NAN( volume ) || volume <= 0.0f )
		{
			continue;
		}
		// No PCM fallback or no_flicker override: only authored shakeData moves
		// the camera. In particular, a silent tail must stay still.
		const char* shakeData = chan->soundShader->GetShakeData( chan->choice );
		if( shakeData == NULL || shakeData[0] == '\0' )
		{
			continue;
		}
		const int length = idStr::Length( shakeData );
		const int relativeTime = idMath::FtoiFast( SoundChannelPlaybackTimeMsec( chan, currentTime ) );
		int64 index = static_cast<int64>( relativeTime ) * SOUND_SHADER_SHAKE_RATE_HZ / 1000;
		if( chan->IsLooping() )
		{
			index %= length;
		}
		if( index >= 0 && index < length )
		{
			high = Max( high, Max( 0, shakeData[index] - 'a' ) * SOUND_SHADER_SCREEN_SHAKE_SCALE * volume );
		}
	}
	return idMath::ATan( high / 32767.0f, 1.0f ) / DEG2RAD( 45.0f );
}

/*
========================
idSoundEmitterLocal::AmplitudeHistory

Read the cached PCM envelope at the playback cursor. Querying a history must
not rewind the channel's pitch/slow-motion clock or decode audio on the UI
thread. Separate logical channels and emitters keep radio and talkers isolated.
========================
*/
void idSoundEmitterLocal::AmplitudeHistory( s_channelType channel, float *peaks, int count )
{
	if( peaks == NULL || count <= 0 ) {
		return;
	}
	for( int i = 0; i < count; ++i ) {
		peaks[i] = 0.0f;
	}
	const int now = soundWorld->GetSoundTime();
	for( int c = 0; c < channels.Num(); ++c ) {
		idSoundChannel *chan = channels[c];
		if( chan == NULL || chan->soundShader == NULL || chan->endTime == 1 ||
			( channel != SCHANNEL_ANY && chan->logicalChannel != channel ) ||
			now < chan->startTime || SoundChannelHasCompleted( chan, now ) || chan->leadinSample == NULL ) {
			continue;
		}
		const int cursor = idMath::FtoiFast( SoundChannelPlaybackTimeMsec( chan, now ) );
		const int leadinLength = chan->leadinSample->LengthInMsec();
		// A finished one-shot must go quiet even during the completion grace period.
		if( cursor >= leadinLength && chan->loopingSample == NULL ) {
			continue;
		}
		for( int i = 0; i < count; ++i ) {
			int offset = cursor - ( count - 1 - i ) * 1000 / 60;
			if( offset < 0 ) {
				continue;
			}
			const idSoundSample *sample = chan->leadinSample;
			if( offset >= leadinLength ) {
				sample = chan->loopingSample;
				if( sample == NULL || sample->LengthInMsec() <= 0 ) {
					continue;
				}
				offset = ( offset - leadinLength ) % sample->LengthInMsec();
			}
			if( !sample->IsDefault() ) {
				peaks[i] = Max( peaks[i], sample->GetAmplitude( offset ) );
			}
		}
	}
}
