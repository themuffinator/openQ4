/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2013 Robert Beckebans
Copyright (c) 2010 by Chris Robinson <chris.kcat@gmail.com> (OpenAL Info Utility)

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
#include "../VoiceChat.h"
#include "../SoundSettings.h"
#if defined(USE_DOOMCLASSIC)
	#include "../../../doomclassic/doom/i_sound.h"
#endif

idCVar s_showLevelMeter( "s_showLevelMeter", "0", CVAR_BOOL, "Show VU meter" );
idCVar s_meterTopTime( "s_meterTopTime", "1000", CVAR_INTEGER | CVAR_ARCHIVE, "How long (in milliseconds) peaks are displayed on the VU meter" );
idCVar s_meterPosition( "s_meterPosition", "100 100 20 200", CVAR_ARCHIVE, "VU meter location (x y w h)" );
idCVar s_device( "s_device", "-1", CVAR_INTEGER | CVAR_ARCHIVE, "Which audio device to use (listDevices to list, -1 for default)" );
idCVar s_showPerfData( "s_showPerfData", "0", CVAR_BOOL, "Show sound backend performance data" );
idCVar s_outputLimiter( "s_outputLimiter", "1", CVAR_ARCHIVE | CVAR_BOOL, "soft-limit the final mix so loud scenes compress instead of clipping" );
idCVar s_resampler( "s_resampler", "auto", CVAR_ARCHIVE, "OpenAL resampler for every voice: auto (an 11th order sinc when the runtime has one), default (the runtime's own), or a name or number from listResamplers; takes effect on s_restart" );
extern idCVar s_useEAXReverb;
extern idCVar s_deviceName;
extern idCVar s_openALHRTF;
extern idCVar s_numberOfSpeakers;
extern idCVar s_noSound;

static const char* OPENQ4_AUDIO_DEVICE_DEFAULT_CHOICE = "__OPENQ4_DEFAULT_AUDIO_DEVICE__";
static const int OPENQ4_OPENAL_HRTF_AUTO = 0;
static const int OPENQ4_OPENAL_HRTF_OFF = 1;
static const int OPENQ4_OPENAL_HRTF_ON = 2;
static const int OPENQ4_OPENAL_SPEAKERS_DEVICE = 0;
static const int OPENQ4_OPENAL_SPEAKERS_STEREO = 2;
static const int OPENQ4_OPENAL_SPEAKERS_SURROUND = 6;

static bool openQ4_UseEnumerateAllDevices( ALCdevice* device ) {
#if defined( ALC_ALL_DEVICES_SPECIFIER ) && defined( ALC_DEFAULT_ALL_DEVICES_SPECIFIER )
	return alcIsExtensionPresent( device, "ALC_ENUMERATE_ALL_EXT" ) != AL_FALSE;
#else
	return false;
#endif
}

static ALCenum openQ4_GetPlaybackDevicesToken( ALCdevice* device ) {
#if defined( ALC_ALL_DEVICES_SPECIFIER )
	if( openQ4_UseEnumerateAllDevices( device ) ) {
		return ALC_ALL_DEVICES_SPECIFIER;
	}
#endif
	return ALC_DEVICE_SPECIFIER;
}

static ALCenum openQ4_GetDefaultPlaybackDeviceToken( ALCdevice* device ) {
#if defined( ALC_DEFAULT_ALL_DEVICES_SPECIFIER )
	if( openQ4_UseEnumerateAllDevices( device ) ) {
		return ALC_DEFAULT_ALL_DEVICES_SPECIFIER;
	}
#endif
	return ALC_DEFAULT_DEVICE_SPECIFIER;
}

#if defined( ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT ) && defined( ALC_EVENT_TYPE_DEVICE_ADDED_SOFT ) && defined( ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT ) && defined( ALC_PLAYBACK_DEVICE_SOFT ) && defined( ALC_EVENT_SUPPORTED_SOFT )
	#define OPENQ4_OPENAL_SYSTEM_EVENTS_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_SYSTEM_EVENTS_SUPPORTED 0
#endif

static const int OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED = BIT( 0 );
static const int OPENQ4_OPENAL_DEVICE_EVENT_ADDED = BIT( 1 );
static const int OPENQ4_OPENAL_DEVICE_EVENT_REMOVED = BIT( 2 );

#if OPENQ4_OPENAL_SYSTEM_EVENTS_SUPPORTED
typedef ALCenum ( ALC_APIENTRY *openq4_alcEventIsSupportedSOFT_t )( ALCenum eventType, ALCenum deviceType );
typedef ALCboolean ( ALC_APIENTRY *openq4_alcEventControlSOFT_t )( ALCsizei count, const ALCenum* events, ALCboolean enable );
typedef void ( ALC_APIENTRY *openq4_alcEventCallbackSOFT_t )( ALCEVENTPROCTYPESOFT callback, void* userParam );

static openq4_alcEventIsSupportedSOFT_t qalcEventIsSupportedSOFT = NULL;
static openq4_alcEventControlSOFT_t qalcEventControlSOFT = NULL;
static openq4_alcEventCallbackSOFT_t qalcEventCallbackSOFT = NULL;
static std::atomic<int> openQ4_PendingOpenALDeviceEvents( 0 );

static int openQ4_DeviceEventFlagForType( const ALCenum eventType )
{
	switch( eventType )
	{
		case ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT:
			return OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED;
		case ALC_EVENT_TYPE_DEVICE_ADDED_SOFT:
			return OPENQ4_OPENAL_DEVICE_EVENT_ADDED;
		case ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT:
			return OPENQ4_OPENAL_DEVICE_EVENT_REMOVED;
		default:
			return 0;
	}
}

static ALCenum openQ4_DeviceEventTypeForFlag( const int flag )
{
	switch( flag )
	{
		case OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED:
			return ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT;
		case OPENQ4_OPENAL_DEVICE_EVENT_ADDED:
			return ALC_EVENT_TYPE_DEVICE_ADDED_SOFT;
		case OPENQ4_OPENAL_DEVICE_EVENT_REMOVED:
			return ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT;
		default:
			return 0;
	}
}

static const char* openQ4_DeviceEventFlagName( const int flag )
{
	switch( flag )
	{
		case OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED:
			return "default changed";
		case OPENQ4_OPENAL_DEVICE_EVENT_ADDED:
			return "device added";
		case OPENQ4_OPENAL_DEVICE_EVENT_REMOVED:
			return "device removed";
		default:
			return "unknown";
	}
}

static void openQ4_AppendDeviceEventName( idStr& eventNames, const int flag )
{
	if( eventNames.Length() > 0 )
	{
		eventNames += ", ";
	}
	eventNames += openQ4_DeviceEventFlagName( flag );
}

static void ALC_APIENTRY openQ4_OpenALDeviceEventCallback( ALCenum eventType, ALCenum deviceType, ALCdevice* device, ALCsizei length, const ALCchar* message, void* userParam ) ALC_API_NOEXCEPT
{
	(void)device;
	(void)length;
	(void)message;
	(void)userParam;

	if( deviceType != ALC_PLAYBACK_DEVICE_SOFT )
	{
		return;
	}

	const int flag = openQ4_DeviceEventFlagForType( eventType );
	if( flag != 0 )
	{
		SoundSettings_DeviceEvent();
		openQ4_PendingOpenALDeviceEvents.fetch_or( flag, std::memory_order_relaxed );
	}
}

static bool openQ4_LoadSystemEventProcs( ALCdevice* device )
{
	qalcEventIsSupportedSOFT = reinterpret_cast<openq4_alcEventIsSupportedSOFT_t>( alcGetProcAddress( device, "alcEventIsSupportedSOFT" ) );
	qalcEventControlSOFT = reinterpret_cast<openq4_alcEventControlSOFT_t>( alcGetProcAddress( device, "alcEventControlSOFT" ) );
	qalcEventCallbackSOFT = reinterpret_cast<openq4_alcEventCallbackSOFT_t>( alcGetProcAddress( device, "alcEventCallbackSOFT" ) );
	return qalcEventIsSupportedSOFT != NULL && qalcEventControlSOFT != NULL && qalcEventCallbackSOFT != NULL;
}

static int openQ4_ConsumePendingDeviceEventFlags()
{
	return openQ4_PendingOpenALDeviceEvents.exchange( 0, std::memory_order_relaxed );
}
#else
static int openQ4_ConsumePendingDeviceEventFlags()
{
	return 0;
}
#endif

#if defined( ALC_SOFT_reopen_device )
	#define OPENQ4_OPENAL_REOPEN_DEVICE_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_REOPEN_DEVICE_SUPPORTED 0
#endif

#if OPENQ4_OPENAL_REOPEN_DEVICE_SUPPORTED
typedef ALCboolean ( ALC_APIENTRY *openq4_alcReopenDeviceSOFT_t )( ALCdevice* device, const ALCchar* deviceName, const ALCint* attribs );
static openq4_alcReopenDeviceSOFT_t qalcReopenDeviceSOFT = NULL;

static bool openQ4_LoadReopenDeviceProc( ALCdevice* device )
{
	qalcReopenDeviceSOFT = reinterpret_cast<openq4_alcReopenDeviceSOFT_t>( alcGetProcAddress( device, "alcReopenDeviceSOFT" ) );
	return qalcReopenDeviceSOFT != NULL;
}
#else
static bool openQ4_LoadReopenDeviceProc( ALCdevice* device )
{
	(void)device;
	return false;
}
#endif

#if defined( AL_DEFERRED_UPDATES_SOFT )
	#define OPENQ4_OPENAL_DEFERRED_UPDATES_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_DEFERRED_UPDATES_SUPPORTED 0
#endif

#if OPENQ4_OPENAL_DEFERRED_UPDATES_SUPPORTED
typedef void ( AL_APIENTRY *openq4_alDeferUpdatesSOFT_t )( void );
typedef void ( AL_APIENTRY *openq4_alProcessUpdatesSOFT_t )( void );

static openq4_alDeferUpdatesSOFT_t qalDeferUpdatesSOFT = NULL;
static openq4_alProcessUpdatesSOFT_t qalProcessUpdatesSOFT = NULL;

static bool openQ4_LoadDeferredUpdateProcs()
{
	qalDeferUpdatesSOFT = reinterpret_cast<openq4_alDeferUpdatesSOFT_t>( alGetProcAddress( "alDeferUpdatesSOFT" ) );
	qalProcessUpdatesSOFT = reinterpret_cast<openq4_alProcessUpdatesSOFT_t>( alGetProcAddress( "alProcessUpdatesSOFT" ) );
	return qalDeferUpdatesSOFT != NULL && qalProcessUpdatesSOFT != NULL;
}
#else
static bool openQ4_LoadDeferredUpdateProcs()
{
	return false;
}
#endif

#if defined( AL_EFFECTSLOT_EFFECT ) && defined( AL_EFFECT_NULL ) && defined( AL_AUXILIARY_SEND_FILTER )
	#define OPENQ4_OPENAL_EFX_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_EFX_SUPPORTED 0
#endif

#if OPENQ4_OPENAL_EFX_SUPPORTED
typedef void ( AL_APIENTRY *openq4_alGenEffects_t )( ALsizei n, ALuint *effects );
typedef void ( AL_APIENTRY *openq4_alDeleteEffects_t )( ALsizei n, const ALuint *effects );
typedef void ( AL_APIENTRY *openq4_alEffecti_t )( ALuint effect, ALenum param, ALint iValue );
typedef void ( AL_APIENTRY *openq4_alGenAuxiliaryEffectSlots_t )( ALsizei n, ALuint *effectslots );
typedef void ( AL_APIENTRY *openq4_alDeleteAuxiliaryEffectSlots_t )( ALsizei n, const ALuint *effectslots );
typedef void ( AL_APIENTRY *openq4_alAuxiliaryEffectSloti_t )( ALuint effectslot, ALenum param, ALint iValue );
typedef void ( AL_APIENTRY *openq4_alEffectf_t )( ALuint effect, ALenum param, ALfloat flValue );
typedef void ( AL_APIENTRY *openq4_alEffectfv_t )( ALuint effect, ALenum param, const ALfloat *pflValues );
typedef void ( AL_APIENTRY *openq4_alGetEffecti_t )( ALuint effect, ALenum param, ALint *piValue );

static openq4_alGenEffects_t qalGenEffects = NULL;
static openq4_alDeleteEffects_t qalDeleteEffects = NULL;
static openq4_alEffecti_t qalEffecti = NULL;
static openq4_alGenAuxiliaryEffectSlots_t qalGenAuxiliaryEffectSlots = NULL;
static openq4_alDeleteAuxiliaryEffectSlots_t qalDeleteAuxiliaryEffectSlots = NULL;
static openq4_alAuxiliaryEffectSloti_t qalAuxiliaryEffectSloti = NULL;
static openq4_alEffectf_t qalEffectf = NULL;
static openq4_alEffectfv_t qalEffectfv = NULL;
static openq4_alGetEffecti_t qalGetEffecti = NULL;

static bool openQ4_LoadHardwareEfxProcs() {
	static bool initialized = false;
	static bool available = false;

	if ( initialized ) {
		return available;
	}
	initialized = true;

	qalGenEffects = reinterpret_cast<openq4_alGenEffects_t>( alGetProcAddress( "alGenEffects" ) );
	qalDeleteEffects = reinterpret_cast<openq4_alDeleteEffects_t>( alGetProcAddress( "alDeleteEffects" ) );
	qalEffecti = reinterpret_cast<openq4_alEffecti_t>( alGetProcAddress( "alEffecti" ) );
	qalGenAuxiliaryEffectSlots = reinterpret_cast<openq4_alGenAuxiliaryEffectSlots_t>( alGetProcAddress( "alGenAuxiliaryEffectSlots" ) );
	qalDeleteAuxiliaryEffectSlots = reinterpret_cast<openq4_alDeleteAuxiliaryEffectSlots_t>( alGetProcAddress( "alDeleteAuxiliaryEffectSlots" ) );
	qalAuxiliaryEffectSloti = reinterpret_cast<openq4_alAuxiliaryEffectSloti_t>( alGetProcAddress( "alAuxiliaryEffectSloti" ) );
	qalEffectf = reinterpret_cast<openq4_alEffectf_t>( alGetProcAddress( "alEffectf" ) );
	qalEffectfv = reinterpret_cast<openq4_alEffectfv_t>( alGetProcAddress( "alEffectfv" ) );
	qalGetEffecti = reinterpret_cast<openq4_alGetEffecti_t>( alGetProcAddress( "alGetEffecti" ) );

	available =
		( qalGenEffects != NULL ) &&
		( qalDeleteEffects != NULL ) &&
		( qalEffecti != NULL ) &&
		( qalGenAuxiliaryEffectSlots != NULL ) &&
		( qalDeleteAuxiliaryEffectSlots != NULL ) &&
		( qalAuxiliaryEffectSloti != NULL ) &&
		( qalEffectf != NULL ) &&
		( qalEffectfv != NULL ) &&
		( qalGetEffecti != NULL );
	return available;
}
#endif

#if defined( ALC_HRTF_SOFT ) && defined( ALC_HRTF_STATUS_SOFT ) && defined( ALC_HRTF_DISABLED_SOFT ) && defined( ALC_HRTF_ENABLED_SOFT )
	#define OPENQ4_OPENAL_HRTF_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_HRTF_SUPPORTED 0
#endif

static int openQ4_GetHrtfMode()
{
	return idMath::ClampInt( OPENQ4_OPENAL_HRTF_AUTO, OPENQ4_OPENAL_HRTF_ON, s_openALHRTF.GetInteger() );
}

static const char* openQ4_HrtfModeName( const int mode )
{
	switch( mode )
	{
		case OPENQ4_OPENAL_HRTF_OFF:
			return "off";
		case OPENQ4_OPENAL_HRTF_ON:
			return "on";
		case OPENQ4_OPENAL_HRTF_AUTO:
		default:
			return "auto";
	}
}

static int openQ4_GetSpeakerCount()
{
	const int speakers = s_numberOfSpeakers.GetInteger();
	return ( speakers == OPENQ4_OPENAL_SPEAKERS_STEREO || speakers == OPENQ4_OPENAL_SPEAKERS_SURROUND ) ? speakers : OPENQ4_OPENAL_SPEAKERS_DEVICE;
}

static const char* openQ4_SpeakerCountName( const int speakerCount )
{
	switch( speakerCount )
	{
		case OPENQ4_OPENAL_SPEAKERS_STEREO:
			return "stereo";
		case OPENQ4_OPENAL_SPEAKERS_SURROUND:
			return "5.1 surround";
		default:
			return "device layout";
	}
}

#if defined( ALC_OUTPUT_MODE_SOFT ) && defined( ALC_ANY_SOFT ) && defined( ALC_STEREO_SOFT ) && defined( ALC_SURROUND_5_1_SOFT )
	#define OPENQ4_OPENAL_OUTPUT_MODE_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_OUTPUT_MODE_SUPPORTED 0
#endif

#if OPENQ4_OPENAL_OUTPUT_MODE_SUPPORTED
static const char* openQ4_OutputModeName( const ALCint outputMode )
{
	switch( outputMode )
	{
		case ALC_ANY_SOFT:
			return "any";
#if defined( ALC_MONO_SOFT )
		case ALC_MONO_SOFT:
			return "mono";
#endif
		case ALC_STEREO_SOFT:
			return "stereo";
#if defined( ALC_STEREO_BASIC_SOFT )
		case ALC_STEREO_BASIC_SOFT:
			return "basic stereo";
#endif
#if defined( ALC_STEREO_UHJ_SOFT )
		case ALC_STEREO_UHJ_SOFT:
			return "UHJ stereo";
#endif
#if defined( ALC_STEREO_HRTF_SOFT )
		case ALC_STEREO_HRTF_SOFT:
			return "HRTF stereo";
#endif
#if defined( ALC_QUAD_SOFT )
		case ALC_QUAD_SOFT:
			return "quad";
#endif
		case ALC_SURROUND_5_1_SOFT:
			return "5.1 surround";
#if defined( ALC_SURROUND_6_1_SOFT )
		case ALC_SURROUND_6_1_SOFT:
			return "6.1 surround";
#endif
#if defined( ALC_SURROUND_7_1_SOFT )
		case ALC_SURROUND_7_1_SOFT:
			return "7.1 surround";
#endif
		default:
			return "unknown";
	}
}

static ALCint openQ4_GetRequestedOutputMode()
{
#if defined( ALC_STEREO_HRTF_SOFT )
	if( openQ4_GetHrtfMode() == OPENQ4_OPENAL_HRTF_ON )
	{
		return ALC_STEREO_HRTF_SOFT;
	}
#endif
	switch( openQ4_GetSpeakerCount() )
	{
		case OPENQ4_OPENAL_SPEAKERS_SURROUND:
			return ALC_SURROUND_5_1_SOFT;
		case OPENQ4_OPENAL_SPEAKERS_STEREO:
			return ALC_STEREO_SOFT;
		default:
			return ALC_ANY_SOFT;
	}
}

static void openQ4_ReportOutputMode( ALCdevice* device )
{
	if( device == NULL || alcIsExtensionPresent( device, "ALC_SOFT_output_mode" ) != AL_TRUE )
	{
		common->Printf( "OpenAL output mode: unavailable\n" );
		return;
	}

	ALCint outputMode = ALC_ANY_SOFT;
	alcGetIntegerv( device, ALC_OUTPUT_MODE_SOFT, 1, &outputMode );
	if( CheckALCErrors( device ) == ALC_NO_ERROR )
	{
		common->Printf( "OpenAL output mode active: %s\n", openQ4_OutputModeName( outputMode ) );
	}
}
#else
static void openQ4_ReportOutputMode( ALCdevice* device )
{
	(void)device;
	common->Printf( "OpenAL output mode: unavailable\n" );
}
#endif

#if OPENQ4_OPENAL_HRTF_SUPPORTED
typedef const ALCchar* ( ALC_APIENTRY *openq4_alcGetStringiSOFT_t )( ALCdevice* device, ALCenum paramName, ALCsizei index );
typedef ALCboolean ( ALC_APIENTRY *openq4_alcResetDeviceSOFT_t )( ALCdevice* device, const ALCint* attribs );

static openq4_alcGetStringiSOFT_t qalcGetStringiSOFT = NULL;
static openq4_alcResetDeviceSOFT_t qalcResetDeviceSOFT = NULL;

static const char* openQ4_HrtfStatusName( const ALCint status )
{
	switch( status )
	{
		case ALC_HRTF_DISABLED_SOFT:
			return "disabled";
		case ALC_HRTF_ENABLED_SOFT:
			return "enabled";
#if defined( ALC_HRTF_DENIED_SOFT )
		case ALC_HRTF_DENIED_SOFT:
			return "denied";
#endif
#if defined( ALC_HRTF_REQUIRED_SOFT )
		case ALC_HRTF_REQUIRED_SOFT:
			return "required";
#endif
#if defined( ALC_HRTF_HEADPHONES_DETECTED_SOFT )
		case ALC_HRTF_HEADPHONES_DETECTED_SOFT:
			return "headphones-detected";
#endif
#if defined( ALC_HRTF_UNSUPPORTED_FORMAT_SOFT )
		case ALC_HRTF_UNSUPPORTED_FORMAT_SOFT:
			return "unsupported-format";
#endif
		default:
			return "unknown";
	}
}

static bool openQ4_LoadHrtfProcs( ALCdevice* device )
{
	qalcGetStringiSOFT = reinterpret_cast<openq4_alcGetStringiSOFT_t>( alcGetProcAddress( device, "alcGetStringiSOFT" ) );
	qalcResetDeviceSOFT = reinterpret_cast<openq4_alcResetDeviceSOFT_t>( alcGetProcAddress( device, "alcResetDeviceSOFT" ) );
	return qalcResetDeviceSOFT != NULL;
}

static void openQ4_ReportHrtfPreference( ALCdevice* device )
{
	if( device == NULL )
	{
		return;
	}

	const int mode = openQ4_GetHrtfMode();
	if( alcIsExtensionPresent( device, "ALC_SOFT_HRTF" ) != AL_TRUE )
	{
		if( mode != OPENQ4_OPENAL_HRTF_AUTO )
		{
			common->Warning( "OpenAL HRTF requested '%s', but ALC_SOFT_HRTF is not available.", openQ4_HrtfModeName( mode ) );
		}
		return;
	}

	// alcGetStringiSOFT lists the HRTFs; the preference itself is applied
	// through openQ4_BuildDeviceAttributes when the context is created.
	openQ4_LoadHrtfProcs( device );
	common->Printf( "OpenAL HRTF requested mode: %s\n", openQ4_HrtfModeName( mode ) );
}

static void openQ4_ReportHrtfStatus( ALCdevice* device )
{
	if( device == NULL || alcIsExtensionPresent( device, "ALC_SOFT_HRTF" ) != AL_TRUE )
	{
		common->Printf( "OpenAL HRTF: unavailable\n" );
		return;
	}

	ALCint hrtfStatus = ALC_HRTF_DISABLED_SOFT;
	alcGetIntegerv( device, ALC_HRTF_STATUS_SOFT, 1, &hrtfStatus );
	if( CheckALCErrors( device ) == ALC_NO_ERROR )
	{
		common->Printf( "OpenAL HRTF status: %s\n", openQ4_HrtfStatusName( hrtfStatus ) );
	}

#if defined( ALC_NUM_HRTF_SPECIFIERS_SOFT ) && defined( ALC_HRTF_SPECIFIER_SOFT )
	if( qalcGetStringiSOFT != NULL )
	{
		ALCint numSpecifiers = 0;
		alcGetIntegerv( device, ALC_NUM_HRTF_SPECIFIERS_SOFT, 1, &numSpecifiers );
		if( CheckALCErrors( device ) == ALC_NO_ERROR && numSpecifiers > 0 )
		{
			common->Printf( "OpenAL HRTF specifiers:\n" );
			for( ALCint i = 0; i < numSpecifiers; ++i )
			{
				const ALCchar* specifier = qalcGetStringiSOFT( device, ALC_HRTF_SPECIFIER_SOFT, i );
				if( specifier != NULL && specifier[0] != '\0' )
				{
					common->Printf( "    %s\n", specifier );
				}
			}
		}
	}
#endif
}
#else
static void openQ4_ReportHrtfPreference( ALCdevice* device )
{
	(void)device;
	if( openQ4_GetHrtfMode() != OPENQ4_OPENAL_HRTF_AUTO )
	{
		// Providers without ALC_SOFT_HRTF (e.g. Apple's OpenAL framework)
		// cannot honor the preference; status line, not a warning.
		common->Printf( "OpenAL HRTF '%s' requested, but this build does not expose ALC_SOFT_HRTF; the runtime keeps its default behavior.\n", openQ4_HrtfModeName( openQ4_GetHrtfMode() ) );
	}
}

static void openQ4_ReportHrtfStatus( ALCdevice* device )
{
	(void)device;
	common->Printf( "OpenAL HRTF: unavailable\n" );
}
#endif


/*
========================
openQ4_BuildDeviceAttributes

Every device preference travels in one attribute list. alcCreateContext and
alcReopenDeviceSOFT rebuild the device's parameters from the list they are
given, so a preference applied any other way (the old HRTF reset before the
context existed) silently reverts to the runtime default: s_openALHRTF 1 still
heard HRTF, and a hot-plug reopen dropped the speaker layout.
========================
*/
static const int OPENQ4_OPENAL_DEVICE_ATTRIBUTE_CAPACITY = 7;	// three pairs and the terminator

static const ALCint* openQ4_BuildDeviceAttributes( ALCdevice* device, ALCint attributes[ OPENQ4_OPENAL_DEVICE_ATTRIBUTE_CAPACITY ], const bool report )
{
	int count = 0;
	attributes[0] = 0;
	if( device == NULL )
	{
		return NULL;
	}

#if OPENQ4_OPENAL_HRTF_SUPPORTED
	const int hrtfMode = openQ4_GetHrtfMode();
	if( hrtfMode != OPENQ4_OPENAL_HRTF_AUTO && alcIsExtensionPresent( device, "ALC_SOFT_HRTF" ) == AL_TRUE )
	{
		attributes[count++] = ALC_HRTF_SOFT;
		attributes[count++] = ( hrtfMode == OPENQ4_OPENAL_HRTF_ON ) ? ALC_TRUE : ALC_FALSE;
	}
#endif

#if OPENQ4_OPENAL_OUTPUT_MODE_SUPPORTED
	if( alcIsExtensionPresent( device, "ALC_SOFT_output_mode" ) == AL_TRUE )
	{
		const ALCint outputMode = openQ4_GetRequestedOutputMode();
		attributes[count++] = ALC_OUTPUT_MODE_SOFT;
		attributes[count++] = outputMode;
		if( report )
		{
			common->Printf( "OpenAL output mode requested: %s\n", openQ4_OutputModeName( outputMode ) );
		}
	}
	else if( report && openQ4_GetSpeakerCount() == OPENQ4_OPENAL_SPEAKERS_SURROUND )
	{
		common->Warning( "OpenAL output mode requested '%s', but ALC_SOFT_output_mode is not available.", openQ4_SpeakerCountName( openQ4_GetSpeakerCount() ) );
	}
#else
	if( report && openQ4_GetSpeakerCount() == OPENQ4_OPENAL_SPEAKERS_SURROUND )
	{
		// Expected with default cvars on providers without ALC_SOFT_output_mode
		// (e.g. Apple's OpenAL framework); status line, not a warning.
		common->Printf( "OpenAL output mode '%s' requested, but this build does not expose ALC_SOFT_output_mode; using the runtime default.\n", openQ4_SpeakerCountName( openQ4_GetSpeakerCount() ) );
	}
#endif

#if defined( ALC_OUTPUT_LIMITER_SOFT )
	// OpenAL Soft only limits integer output by default, and shared-mode Windows
	// endpoints mix in float, so a big firefight could clip; ask for it outright.
	if( alcIsExtensionPresent( device, "ALC_SOFT_output_limiter" ) == AL_TRUE )
	{
		attributes[count++] = ALC_OUTPUT_LIMITER_SOFT;
		attributes[count++] = s_outputLimiter.GetBool() ? ALC_TRUE : ALC_FALSE;
	}
#endif

	attributes[count] = 0;
	return ( count > 0 ) ? attributes : NULL;
}

static void openQ4_ReportOutputLimiter( ALCdevice* device )
{
#if defined( ALC_OUTPUT_LIMITER_SOFT )
	if( device == NULL || alcIsExtensionPresent( device, "ALC_SOFT_output_limiter" ) != AL_TRUE )
	{
		return;
	}
	ALCint limiter = ALC_FALSE;
	alcGetIntegerv( device, ALC_OUTPUT_LIMITER_SOFT, 1, &limiter );
	if( CheckALCErrors( device ) == ALC_NO_ERROR )
	{
		common->Printf( "OpenAL output limiter: %s\n", limiter == ALC_TRUE ? "on" : "off" );
	}
#else
	(void)device;
#endif
}

/*
========================
idSoundHardware_OpenAL::idSoundHardware_OpenAL
========================
*/
idSoundHardware_OpenAL::idSoundHardware_OpenAL()
{
	openalDevice = NULL;
	openalContext = NULL;
	efxFiltersAvailable = false;
	efxEnabled = false;
	auxEffectSlot = 0;
	auxReverbEffect = 0;
	for( int i = 0; i < SOUND_REVERB_SLOTS - 1; i++ )
	{
		areaReverbSlots[i] = 0;
		areaReverbEffects[i] = 0;
	}
	maxAuxiliarySends = 0;
	reverbSlotGeneration = 0;
	ResetReverbSlotState();
	deviceEventsEnabled = false;
	enabledDeviceEventFlags = 0;
	reopenDeviceAvailable = false;
	deferredUpdatesAvailable = false;
	deferredUpdatesActive = false;
	initFailed = false;
	lastResetTime = 0;
	lastDeviceCheckTime = 0;
	lastPerfPrintTime = 0;
	openedWithDefaultFallback = false;
	openedHrtfMode = openQ4_GetHrtfMode();
	openedSpeakerCount = openQ4_GetSpeakerCount();
	// soundSystemLocal is built during static initialization, before cvars from
	// other files exist; CaptureOpenedDeviceState records the real value
	openedOutputLimiter = true;
	sourceRadiusAvailable = false;
	directChannelsAvailable = false;
	directChannelsMode = AL_FALSE;
	sourceResampler = -1;
	outputSerial = 1;
	outputCenterObserved = false;
	centerRoutingAvailable = false;
	disconnectQueryAvailable = false;
	simulatedInterruptionEnd = 0;

	//vuMeterRMS = NULL;
	//vuMeterPeak = NULL;

	//outputChannels = 0;
	//channelMask = 0;

	voices.SetNum( 0 );
	zombieVoices.SetNum( 0 );
	freeVoices.SetNum( 0 );

	lastResetTime = 0;
}

bool idSoundHardware_OpenAL::IsDefaultDeviceChoiceValue( const char* deviceName ) {
	return deviceName != NULL && idStr::Icmp( deviceName, OPENQ4_AUDIO_DEVICE_DEFAULT_CHOICE ) == 0;
}

idStr idSoundHardware_OpenAL::NormalizeRequestedDeviceName( const char* deviceName ) {
	if( deviceName == NULL || IsDefaultDeviceChoiceValue( deviceName ) ) {
		return "";
	}

	return deviceName;
}

idStr idSoundHardware_OpenAL::SanitizeDeviceLabel( const char* deviceName ) {
	idStr sanitized = ( deviceName != NULL ) ? deviceName : "";
	sanitized.Replace( "\r", " " );
	sanitized.Replace( "\n", " " );
	sanitized.Replace( "\t", " " );
	sanitized.StripLeading( ' ' );
	sanitized.StripTrailingWhitespace();
	return sanitized;
}

void idSoundHardware_OpenAL::AppendChoiceItem( idStr& list, const char* item ) {
	if( list.Length() > 0 ) {
		list += ";";
	}

	list += "\"";
	for( const char* it = item; it != NULL && *it != '\0'; ++it ) {
		if( *it == '\\' || *it == '"' ) {
			list += "\\";
		}
		list += *it;
	}
	list += "\"";
}

bool idSoundHardware_OpenAL::DeviceListContains( const idStrList& deviceNames, const char* deviceName ) {
	if( deviceName == NULL || deviceName[0] == '\0' ) {
		return false;
	}

	for( int i = 0; i < deviceNames.Num(); ++i ) {
		if( deviceNames[i].Icmp( deviceName ) == 0 ) {
			return true;
		}
	}

	return false;
}

void idSoundHardware_OpenAL::GetAvailablePlaybackDevices( idStrList& deviceNames, idStr& defaultDeviceName ) {
	deviceNames.Clear();
	defaultDeviceName.Clear();

	const ALCchar* defaultDevice = alcGetString( NULL, openQ4_GetDefaultPlaybackDeviceToken( NULL ) );
	if( defaultDevice != NULL && defaultDevice[0] != '\0' ) {
		defaultDeviceName = reinterpret_cast<const char*>( defaultDevice );
	}

	const ALCchar* deviceList = alcGetString( NULL, openQ4_GetPlaybackDevicesToken( NULL ) );
	if( deviceList == NULL || deviceList[0] == '\0' ) {
		return;
	}

	for( const ALCchar* it = deviceList; *it != '\0'; it += strlen( it ) + 1 ) {
		idStr deviceName = reinterpret_cast<const char*>( it );
		deviceName.StripLeading( ' ' );
		deviceName.StripTrailingWhitespace();
		if( !deviceName.Length() || DeviceListContains( deviceNames, deviceName.c_str() ) ) {
			continue;
		}

		deviceNames.Append( deviceName );
	}
}

idStr idSoundHardware_OpenAL::GetActivePlaybackDeviceName( ALCdevice* device ) {
	idStr activeDeviceName;
	if( device == NULL ) {
		return activeDeviceName;
	}

	const ALCchar* activeDevice = NULL;
#if defined( ALC_ALL_DEVICES_SPECIFIER )
	if( openQ4_UseEnumerateAllDevices( device ) ) {
		activeDevice = alcGetString( device, ALC_ALL_DEVICES_SPECIFIER );
	}
#endif
	if( CheckALCErrors( device ) != ALC_NO_ERROR || activeDevice == NULL || activeDevice[0] == '\0' ) {
		activeDevice = alcGetString( device, ALC_DEVICE_SPECIFIER );
		CheckALCErrors( device );
	}

	if( activeDevice != NULL && activeDevice[0] != '\0' ) {
		activeDeviceName = reinterpret_cast<const char*>( activeDevice );
	}

	return activeDeviceName;
}

void idSoundHardware_OpenAL::BuildDeviceChoiceStrings( const char* requestedDeviceName, idStr& choiceNames, idStr& choiceValues ) {
	const idStr normalizedRequestedDeviceName = NormalizeRequestedDeviceName( requestedDeviceName );
	const idStr defaultLabel = common->GetLanguageDict()->GetString( "#str_229913" );
	const idStr unavailableLabel = common->GetLanguageDict()->GetString( "#str_41105" );

	idStrList deviceNames;
	idStr defaultDeviceName;
	GetAvailablePlaybackDevices( deviceNames, defaultDeviceName );

	idStr displayDefaultLabel = defaultLabel;
	const idStr sanitizedDefaultDeviceName = SanitizeDeviceLabel( defaultDeviceName.c_str() );
	if( sanitizedDefaultDeviceName.Length() ) {
		displayDefaultLabel = va( "%s (%s)", defaultLabel.c_str(), sanitizedDefaultDeviceName.c_str() );
	}

	choiceNames.Clear();
	choiceValues.Clear();
	AppendChoiceItem( choiceNames, displayDefaultLabel.c_str() );
	AppendChoiceItem( choiceValues, OPENQ4_AUDIO_DEVICE_DEFAULT_CHOICE );

	bool requestedDeviceFound = normalizedRequestedDeviceName.IsEmpty();
	for( int i = 0; i < deviceNames.Num(); ++i ) {
		const idStr& deviceName = deviceNames[i];
		const idStr deviceLabel = SanitizeDeviceLabel( deviceName.c_str() );
		if( !deviceLabel.Length() ) {
			continue;
		}

		AppendChoiceItem( choiceNames, deviceLabel.c_str() );
		AppendChoiceItem( choiceValues, deviceName.c_str() );
		if( deviceName.Icmp( normalizedRequestedDeviceName ) == 0 ) {
			requestedDeviceFound = true;
		}
	}

	if( !requestedDeviceFound && normalizedRequestedDeviceName.Length() ) {
		idStr requestedLabel = SanitizeDeviceLabel( normalizedRequestedDeviceName.c_str() );
		if( unavailableLabel.Length() ) {
			requestedLabel = va( "%s (%s)", requestedLabel.c_str(), unavailableLabel.c_str() );
		}

		AppendChoiceItem( choiceNames, requestedLabel.c_str() );
		AppendChoiceItem( choiceValues, normalizedRequestedDeviceName.c_str() );
	}
}

void idSoundHardware_OpenAL::CaptureOpenedDeviceState( const char* requestedDeviceName ) {
	idStrList deviceNames;
	GetAvailablePlaybackDevices( deviceNames, openedDefaultDeviceName );

	openedRequestedDeviceName = NormalizeRequestedDeviceName( requestedDeviceName );
	openedActiveDeviceName = GetActivePlaybackDeviceName( openalDevice );
	openedWithDefaultFallback = openedRequestedDeviceName.Length() > 0 && openedActiveDeviceName.Icmp( openedRequestedDeviceName ) != 0;
	openedHrtfMode = openQ4_GetHrtfMode();
	openedSpeakerCount = openQ4_GetSpeakerCount();
	openedOutputLimiter = s_outputLimiter.GetBool();
	lastDeviceCheckTime = Sys_Milliseconds();
}

void idSoundHardware_OpenAL::EnableDeviceEventMonitoring() {
	DisableDeviceEventMonitoring();

#if OPENQ4_OPENAL_SYSTEM_EVENTS_SUPPORTED
	if( openalDevice == NULL )
	{
		return;
	}

	if( alcIsExtensionPresent( openalDevice, "ALC_SOFT_system_events" ) != ALC_TRUE || !openQ4_LoadSystemEventProcs( openalDevice ) )
	{
		return;
	}

	ALCenum events[ 3 ];
	int eventCount = 0;
	enabledDeviceEventFlags = 0;
	const int eventFlags[] = {
		OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED,
		OPENQ4_OPENAL_DEVICE_EVENT_ADDED,
		OPENQ4_OPENAL_DEVICE_EVENT_REMOVED
	};
	for( int i = 0; i < sizeof( eventFlags ) / sizeof( eventFlags[ 0 ] ); ++i )
	{
		const ALCenum eventType = openQ4_DeviceEventTypeForFlag( eventFlags[ i ] );
		if( eventType != 0 && qalcEventIsSupportedSOFT( eventType, ALC_PLAYBACK_DEVICE_SOFT ) == ALC_EVENT_SUPPORTED_SOFT )
		{
			events[ eventCount++ ] = eventType;
			enabledDeviceEventFlags |= eventFlags[ i ];
		}
	}

	if( eventCount == 0 )
	{
		enabledDeviceEventFlags = 0;
		return;
	}

	openQ4_ConsumePendingDeviceEventFlags();
	qalcEventCallbackSOFT( openQ4_OpenALDeviceEventCallback, NULL );
	if( qalcEventControlSOFT( eventCount, events, ALC_TRUE ) != ALC_TRUE )
	{
		common->Warning( "OpenAL playback device event monitoring could not be enabled; falling back to polling." );
		qalcEventCallbackSOFT( NULL, NULL );
		enabledDeviceEventFlags = 0;
		return;
	}

	deviceEventsEnabled = true;

	idStr eventNames;
	for( int i = 0; i < sizeof( eventFlags ) / sizeof( eventFlags[ 0 ] ); ++i )
	{
		if( ( enabledDeviceEventFlags & eventFlags[ i ] ) != 0 )
		{
			openQ4_AppendDeviceEventName( eventNames, eventFlags[ i ] );
		}
	}
	common->Printf( "OpenAL playback device event monitoring enabled: %s\n", eventNames.c_str() );
#endif
}

void idSoundHardware_OpenAL::DisableDeviceEventMonitoring() {
#if OPENQ4_OPENAL_SYSTEM_EVENTS_SUPPORTED
	if( qalcEventControlSOFT != NULL && deviceEventsEnabled && enabledDeviceEventFlags != 0 )
	{
		ALCenum events[ 3 ];
		int eventCount = 0;
		const int eventFlags[] = {
			OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED,
			OPENQ4_OPENAL_DEVICE_EVENT_ADDED,
			OPENQ4_OPENAL_DEVICE_EVENT_REMOVED
		};
		for( int i = 0; i < sizeof( eventFlags ) / sizeof( eventFlags[ 0 ] ); ++i )
		{
			if( ( enabledDeviceEventFlags & eventFlags[ i ] ) != 0 )
			{
				const ALCenum eventType = openQ4_DeviceEventTypeForFlag( eventFlags[ i ] );
				if( eventType != 0 )
				{
					events[ eventCount++ ] = eventType;
				}
			}
		}
		if( eventCount > 0 )
		{
			qalcEventControlSOFT( eventCount, events, ALC_FALSE );
		}
	}
	if( qalcEventCallbackSOFT != NULL && deviceEventsEnabled )
	{
		qalcEventCallbackSOFT( NULL, NULL );
	}
	openQ4_ConsumePendingDeviceEventFlags();
#endif
	deviceEventsEnabled = false;
	enabledDeviceEventFlags = 0;
}

bool idSoundHardware_OpenAL::TryReopenDevice( const char* requestedDeviceName, const char* reason ) {
	const idStr normalizedRequestedDeviceName = NormalizeRequestedDeviceName( requestedDeviceName );

#if OPENQ4_OPENAL_REOPEN_DEVICE_SUPPORTED
	if( openalDevice == NULL || !reopenDeviceAvailable || qalcReopenDeviceSOFT == NULL )
	{
		return false;
	}

	idStr reopenTargetDeviceName = normalizedRequestedDeviceName;
	if( reopenTargetDeviceName.Length() > 0 )
	{
		idStrList currentDeviceNames;
		idStr currentDefaultDeviceName;
		GetAvailablePlaybackDevices( currentDeviceNames, currentDefaultDeviceName );
		if( !DeviceListContains( currentDeviceNames, reopenTargetDeviceName.c_str() ) )
		{
			reopenTargetDeviceName.Clear();
		}
	}

	const char* reopenDeviceName = reopenTargetDeviceName.Length() > 0 ? reopenTargetDeviceName.c_str() : NULL;
	if( normalizedRequestedDeviceName.Length() > 0 && reopenDeviceName == NULL )
	{
		common->Printf( "OpenAL %s; requested device '%s' is unavailable, attempting in-place device reopen to system default.\n", reason != NULL ? reason : "device change detected", normalizedRequestedDeviceName.c_str() );
	}
	else
	{
		common->Printf( "OpenAL %s; attempting in-place device reopen to %s.\n", reason != NULL ? reason : "device change detected", reopenDeviceName != NULL ? reopenDeviceName : "system default" );
	}

	(void)alcGetError( openalDevice );
	// The reopened device is rebuilt from these attributes alone; NULL would drop the
	// HRTF preference, the speaker layout and the limiter.
	ALCint reopenAttributes[ OPENQ4_OPENAL_DEVICE_ATTRIBUTE_CAPACITY ];
	const bool reopened = qalcReopenDeviceSOFT( openalDevice, reopenDeviceName, openQ4_BuildDeviceAttributes( openalDevice, reopenAttributes, false ) ) == ALC_TRUE;
	const ALCenum reopenError = CheckALCErrors( openalDevice );
	if( !reopened || reopenError != ALC_NO_ERROR )
	{
		common->Warning( "OpenAL in-place device reopen failed; falling back to sound system restart." );
		return false;
	}

	CaptureOpenedDeviceState( normalizedRequestedDeviceName.c_str() );

	if( openedDefaultDeviceName.Length() > 0 )
	{
		common->Printf( "OpenAL default device: %s\n", openedDefaultDeviceName.c_str() );
	}
	if( openedActiveDeviceName.Length() > 0 )
	{
		common->Printf( "OpenAL active device: %s\n", openedActiveDeviceName.c_str() );
	}
	if( openedWithDefaultFallback )
	{
		common->Warning( "OpenAL requested device '%s' is unavailable; using '%s' until it returns.", openedRequestedDeviceName.c_str(), openedActiveDeviceName.c_str() );
	}
	openQ4_ReportHrtfStatus( openalDevice );
	openQ4_ReportOutputMode( openalDevice );
	openQ4_ReportOutputLimiter( openalDevice );
	// A disconnect left every source stopped, and the new endpoint can have
	// another layout: voices restart or re-route at the scene clock.
	NoteOutputChanged( reason != NULL ? reason : "device reopened" );
	return true;
#else
	(void)normalizedRequestedDeviceName;
	(void)reason;
	return false;
#endif
}

/*
========================
idSoundHardware_OpenAL::TryResetDevice

Applies a changed speaker layout, HRTF mode or limiter to the open device in
place. alcResetDeviceSOFT keeps the context, its sources, buffers and effect
slots, so nothing is reloaded; the restart this replaces freed and decoded every
sample again. Playing voices re-route at their scene clock when the layout's
centre speaker comes or goes.
========================
*/
bool idSoundHardware_OpenAL::TryResetDevice( const char* reason )
{
#if OPENQ4_OPENAL_HRTF_SUPPORTED
	if( openalDevice == NULL || alcIsExtensionPresent( openalDevice, "ALC_SOFT_HRTF" ) != ALC_TRUE )
	{
		return false;
	}
	if( qalcResetDeviceSOFT == NULL && !openQ4_LoadHrtfProcs( openalDevice ) )
	{
		return false;
	}
	ALCint resetAttributes[ OPENQ4_OPENAL_DEVICE_ATTRIBUTE_CAPACITY ];
	const ALCint* attributes = openQ4_BuildDeviceAttributes( openalDevice, resetAttributes, true );
	(void)alcGetError( openalDevice );
	const bool reset = qalcResetDeviceSOFT( openalDevice, attributes ) == ALC_TRUE;
	if( CheckALCErrors( openalDevice ) != ALC_NO_ERROR || !reset )
	{
		common->Warning( "OpenAL in-place device reset failed; falling back to sound system restart." );
		return false;
	}
	openedHrtfMode = openQ4_GetHrtfMode();
	openedSpeakerCount = openQ4_GetSpeakerCount();
	openedOutputLimiter = s_outputLimiter.GetBool();
	// An owned settings transaction must see that the device changed under it.
	SoundSettings_DeviceEvent();
	common->Printf( "OpenAL %s; device reset in place.\n", reason );
	openQ4_ReportHrtfStatus( openalDevice );
	openQ4_ReportOutputMode( openalDevice );
	openQ4_ReportOutputLimiter( openalDevice );
	NoteOutputChanged( reason );
	return true;
#else
	(void)reason;
	return false;
#endif
}

/*
========================
idSoundHardware_OpenAL::NoteOutputChanged
========================
*/
void idSoundHardware_OpenAL::NoteOutputChanged( const char* reason )
{
	++outputSerial;
	outputCenterObserved = HasCenterChannelOutput();
	common->Printf( "OpenAL output changed (%s): serial %u, centre speaker %s\n", reason, outputSerial, outputCenterObserved ? "routed" : "not routed" );
}

/*
========================
idSoundHardware_OpenAL::UpdateOutputState

Every frame, before the throttled device checks: ends a simulated interruption,
and notices a centre speaker appearing or going away under the open context.
========================
*/
void idSoundHardware_OpenAL::UpdateOutputState()
{
	if( simulatedInterruptionEnd != 0 && Sys_Milliseconds() - simulatedInterruptionEnd >= 0 )
	{
		simulatedInterruptionEnd = 0;
		NoteOutputChanged( "simulated interruption ended" );
		return;
	}
	if( HasCenterChannelOutput() != outputCenterObserved )
	{
		NoteOutputChanged( outputCenterObserved ? "centre speaker lost" : "centre speaker available" );
	}
}

/*
========================
idSoundHardware_OpenAL::IsOutputConnected
========================
*/
bool idSoundHardware_OpenAL::IsOutputConnected() const
{
	if( openalDevice == NULL || simulatedInterruptionEnd != 0 )
	{
		return false;
	}
#if defined( ALC_CONNECTED )
	if( disconnectQueryAvailable )
	{
		ALCint connected = ALC_TRUE;
		alcGetIntegerv( openalDevice, ALC_CONNECTED, 1, &connected );
		return connected != ALC_FALSE;
	}
#endif
	return true;
}

/*
========================
idSoundHardware_OpenAL::SimulateOutputInterruption

What OpenAL Soft does to a context when its endpoint disconnects, without a
physical device: every source stops, whatever it was doing, and stays stopped.
The voices then follow the production recovery path; the outage ends with the
same output change notice a reopen gives.
========================
*/
void idSoundHardware_OpenAL::SimulateOutputInterruption( int msec )
{
	if( openalDevice == NULL )
	{
		common->Printf( "No active OpenAL device.\n" );
		return;
	}
	for( int i = 0; i < voices.Num(); i++ )
	{
		if( alIsSource( voices[i].openalSource ) )
		{
			alSourceStop( voices[i].openalSource );
		}
	}
	SoundVoiceChat_OutputInterrupted();
	CheckALErrors();
	msec = idMath::ClampInt( 1, 60000, msec );
	simulatedInterruptionEnd = Sys_Milliseconds() + msec;
	if( simulatedInterruptionEnd == 0 )
	{
		simulatedInterruptionEnd = 1;
	}
	common->Printf( "SOUND_OUTPUT_INTERRUPTION msec=%d serial=%u\n", msec, outputSerial );
}

static void testDeviceInterruption_f( const idCmdArgs& args )
{
	soundSystemLocal.hardware.SimulateOutputInterruption( args.Argc() > 1 ? atoi( args.Argv( 1 ) ) : 1000 );
}

void idSoundHardware_OpenAL::BeginDeferredUpdates() {
#if OPENQ4_OPENAL_DEFERRED_UPDATES_SUPPORTED
	if( !deferredUpdatesAvailable || deferredUpdatesActive || openalContext == NULL || alcGetCurrentContext() != openalContext )
	{
		return;
	}

	qalDeferUpdatesSOFT();
	if( CheckALErrors() == AL_NO_ERROR )
	{
		deferredUpdatesActive = true;
	}
	else
	{
		deferredUpdatesAvailable = false;
	}
#endif
}

void idSoundHardware_OpenAL::EndDeferredUpdates() {
#if OPENQ4_OPENAL_DEFERRED_UPDATES_SUPPORTED
	if( !deferredUpdatesActive )
	{
		return;
	}

	qalProcessUpdatesSOFT();
	deferredUpdatesActive = false;
	if( CheckALErrors() != AL_NO_ERROR )
	{
		deferredUpdatesAvailable = false;
	}
#endif
}

bool idSoundHardware_OpenAL::UpdateDeviceMonitoring() {
	// The owned settings lease queries drift and keeps recovery authority.
	if (SoundSettings_BlockAutomaticRestart()) return false;
	UpdateOutputState();
	const int nowTime = Sys_Milliseconds();
	const int pendingDeviceEventFlags = openQ4_ConsumePendingDeviceEventFlags();
	if( pendingDeviceEventFlags != 0 )
	{
		idStr eventNames;
#if OPENQ4_OPENAL_SYSTEM_EVENTS_SUPPORTED
		const int eventFlags[] = {
			OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED,
			OPENQ4_OPENAL_DEVICE_EVENT_ADDED,
			OPENQ4_OPENAL_DEVICE_EVENT_REMOVED
		};
		for( int i = 0; i < sizeof( eventFlags ) / sizeof( eventFlags[ 0 ] ); ++i )
		{
			if( ( pendingDeviceEventFlags & eventFlags[ i ] ) != 0 )
			{
				openQ4_AppendDeviceEventName( eventNames, eventFlags[ i ] );
			}
		}
#endif
		common->Printf( "OpenAL playback device event received: %s\n", eventNames.Length() > 0 ? eventNames.c_str() : "unknown" );
	}

	if( pendingDeviceEventFlags == 0 && lastDeviceCheckTime + 1000 > nowTime ) {
		return false;
	}
	lastDeviceCheckTime = nowTime;

	const idStr requestedDeviceName = NormalizeRequestedDeviceName( s_deviceName.GetString() );
	if( requestedDeviceName.Icmp( openedRequestedDeviceName ) != 0 ) {
		if( requestedDeviceName.Length() == 0 ) {
			common->Printf( "OpenAL requested device changed to system default.\n" );
		} else if( openedRequestedDeviceName.Length() == 0 ) {
			common->Printf( "OpenAL requested device changed to '%s'.\n", requestedDeviceName.c_str() );
		} else {
			common->Printf( "OpenAL requested device changed from '%s' to '%s'.\n", openedRequestedDeviceName.c_str(), requestedDeviceName.c_str() );
		}
		if( TryReopenDevice( requestedDeviceName.c_str(), "requested device changed" ) )
		{
			return false;
		}
		common->Printf( "OpenAL requested device change requires sound system restart.\n" );
		soundSystemLocal.SetNeedsRestart();
		return true;
	}

	// Layout, HRTF and limiter changes apply to the open device in place; a full
	// restart, which reloads every sample, is only the fallback.
	const int hrtfMode = openQ4_GetHrtfMode();
	if( hrtfMode != openedHrtfMode ) {
		common->Printf( "OpenAL HRTF mode changed from %s to %s.\n", openQ4_HrtfModeName( openedHrtfMode ), openQ4_HrtfModeName( hrtfMode ) );
		if( TryResetDevice( "HRTF mode changed" ) )
		{
			return false;
		}
		common->Printf( "OpenAL HRTF mode change requires sound system restart.\n" );
		soundSystemLocal.SetNeedsRestart();
		return true;
	}

	const int speakerCount = openQ4_GetSpeakerCount();
	if( speakerCount != openedSpeakerCount ) {
		common->Printf( "OpenAL speaker mode changed from %s to %s.\n", openQ4_SpeakerCountName( openedSpeakerCount ), openQ4_SpeakerCountName( speakerCount ) );
		if( TryResetDevice( "speaker mode changed" ) )
		{
			return false;
		}
		common->Printf( "OpenAL speaker mode change requires sound system restart.\n" );
		soundSystemLocal.SetNeedsRestart();
		return true;
	}

	if( s_outputLimiter.GetBool() != openedOutputLimiter ) {
		common->Printf( "OpenAL output limiter turned %s.\n", s_outputLimiter.GetBool() ? "on" : "off" );
		if( TryResetDevice( "output limiter changed" ) )
		{
			return false;
		}
		common->Printf( "OpenAL output limiter change requires sound system restart.\n" );
		soundSystemLocal.SetNeedsRestart();
		return true;
	}

#if defined( ALC_CONNECTED )
	if( alcIsExtensionPresent( openalDevice, "ALC_EXT_disconnect" ) == AL_TRUE ) {
		ALCint connected = ALC_TRUE;
		alcGetIntegerv( openalDevice, ALC_CONNECTED, 1, &connected );
		if( CheckALCErrors( openalDevice ) == ALC_NO_ERROR && connected == ALC_FALSE ) {
			common->Warning( "OpenAL device '%s' disconnected.", openedActiveDeviceName.c_str() );
			if( TryReopenDevice( openedRequestedDeviceName.c_str(), "active device disconnected" ) )
			{
				return false;
			}
			common->Printf( "OpenAL disconnected device recovery requires sound system restart.\n" );
			soundSystemLocal.SetNeedsRestart();
			return true;
		}
	}
#endif

	idStrList deviceNames;
	idStr defaultDeviceName;
	GetAvailablePlaybackDevices( deviceNames, defaultDeviceName );

	if( openedWithDefaultFallback && DeviceListContains( deviceNames, openedRequestedDeviceName.c_str() ) ) {
		common->Printf( "OpenAL requested device '%s' is available again.\n", openedRequestedDeviceName.c_str() );
		if( TryReopenDevice( openedRequestedDeviceName.c_str(), "requested device became available" ) )
		{
			return false;
		}
		common->Printf( "OpenAL requested device return requires sound system restart.\n" );
		soundSystemLocal.SetNeedsRestart();
		return true;
	}

	if( ( openedRequestedDeviceName.IsEmpty() || openedWithDefaultFallback ) && defaultDeviceName.Length() > 0 && defaultDeviceName.Icmp( openedDefaultDeviceName ) != 0 ) {
		common->Printf( "OpenAL system default device changed from '%s' to '%s'.\n", openedDefaultDeviceName.c_str(), defaultDeviceName.c_str() );
		if( TryReopenDevice( openedRequestedDeviceName.c_str(), "system default device changed" ) )
		{
			return false;
		}
		common->Printf( "OpenAL default device change requires sound system restart.\n" );
		soundSystemLocal.SetNeedsRestart();
		return true;
	}

	return false;
}

void idSoundHardware_OpenAL::PrintDeviceList( const char* list )
{
	if( !list || *list == '\0' )
	{
		idLib::Printf( "    !!! none !!!\n" );
	}
	else
	{
		do
		{
			idLib::Printf( "    %s\n", list );
			list += strlen( list ) + 1;
		}
		while( *list != '\0' );
	}
}

void idSoundHardware_OpenAL::PrintALCInfo( ALCdevice* device )
{
	ALCint major, minor;

	if( device )
	{
		idLib::Printf( "\n" );
		const idStr devname = GetActivePlaybackDeviceName( device );
		idLib::Printf( "** Info for device \"%s\" **\n", devname.Length() ? devname.c_str() : "<unknown>" );
	}
	alcGetIntegerv( device, ALC_MAJOR_VERSION, 1, &major );
	alcGetIntegerv( device, ALC_MINOR_VERSION, 1, &minor );

	if( CheckALCErrors( device ) == ALC_NO_ERROR )
	{
		idLib::Printf( "ALC version: %d.%d\n", major, minor );
	}

	if( device )
	{
		idLib::Printf( "OpenAL extensions: %s", alGetString( AL_EXTENSIONS ) );

		//idLib::Printf("ALC extensions:");
		//printList(alcGetString(device, ALC_EXTENSIONS), ' ');
		CheckALCErrors( device );
	}
}

void idSoundHardware_OpenAL::PrintALInfo()
{
	idLib::Printf( "OpenAL vendor string: %s\n", alGetString( AL_VENDOR ) );
	idLib::Printf( "OpenAL renderer string: %s\n", alGetString( AL_RENDERER ) );
	idLib::Printf( "OpenAL version string: %s\n", alGetString( AL_VERSION ) );
	idLib::Printf( "OpenAL extensions: %s", alGetString( AL_EXTENSIONS ) );
	//PrintList(alGetString(AL_EXTENSIONS), ' ');
	CheckALErrors();
}

#if defined( AL_SOURCE_RESAMPLER_SOFT ) && defined( AL_NUM_RESAMPLERS_SOFT ) && defined( AL_DEFAULT_RESAMPLER_SOFT ) && defined( AL_RESAMPLER_NAME_SOFT )
	#define OPENQ4_OPENAL_RESAMPLER_SUPPORTED 1
typedef const ALchar* ( AL_APIENTRY *openq4_alGetStringiSOFT_t )( ALenum pname, ALsizei index );

static const char* openQ4_ResamplerName( openq4_alGetStringiSOFT_t getStringi, const ALint index )
{
	const ALchar* name = getStringi != NULL ? getStringi( AL_RESAMPLER_NAME_SOFT, index ) : NULL;
	return name != NULL ? name : "";
}

/*
========================
openQ4_SelectResampler

Quake 4 recorded 4762 of its 5737 sounds at 22.05 kHz, so nearly every voice is
upsampled about 2.2x to a 48 kHz device. A short polyphase sinc keeps the images
above 11 kHz out of the mix; the runtime's own default is a 4-point
interpolator. Returns -1 to leave the runtime default in place.
========================
*/
static ALint openQ4_SelectResampler( idStr& chosenName )
{
	chosenName = "runtime default";
	if( alIsExtensionPresent( "AL_SOFT_source_resampler" ) != AL_TRUE )
	{
		chosenName = "runtime default (no AL_SOFT_source_resampler)";
		return -1;
	}
	const openq4_alGetStringiSOFT_t getStringi = reinterpret_cast<openq4_alGetStringiSOFT_t>( alGetProcAddress( "alGetStringiSOFT" ) );
	const ALint count = alGetInteger( AL_NUM_RESAMPLERS_SOFT );
	const ALint defaultIndex = alGetInteger( AL_DEFAULT_RESAMPLER_SOFT );
	if( CheckALErrors() != AL_NO_ERROR || getStringi == NULL || count <= 0 )
	{
		return -1;
	}
	if( defaultIndex >= 0 && defaultIndex < count )
	{
		chosenName = openQ4_ResamplerName( getStringi, defaultIndex );
	}

	const idStr request = s_resampler.GetString();
	ALint chosen = -1;
	if( request.Length() == 0 || request.Icmp( "default" ) == 0 )
	{
		return -1;
	}
	if( request.Icmp( "auto" ) == 0 )
	{
		for( ALint i = 0; i < count && chosen < 0; i++ )
		{
			const idStr name = openQ4_ResamplerName( getStringi, i );
			if( name.Find( "11th order Sinc", false ) >= 0 && name.Find( "fast", false ) < 0 )
			{
				chosen = i;
			}
		}
		for( ALint i = 0; i < count && chosen < 0; i++ )
		{
			if( idStr( openQ4_ResamplerName( getStringi, i ) ).Find( "Sinc", false ) >= 0 )
			{
				chosen = i;
			}
		}
	}
	else if( request.IsNumeric() )
	{
		const int index = atoi( request.c_str() );
		if( index >= 0 && index < count )
		{
			chosen = index;
		}
	}
	else
	{
		for( ALint i = 0; i < count && chosen < 0; i++ )
		{
			if( request.Icmp( openQ4_ResamplerName( getStringi, i ) ) == 0 )
			{
				chosen = i;
			}
		}
	}
	if( chosen < 0 )
	{
		if( request.Icmp( "auto" ) != 0 )
		{
			common->Warning( "s_resampler '%s' names no OpenAL resampler; keeping the runtime default (listResamplers lists them).", request.c_str() );
		}
		return -1;
	}
	chosenName = openQ4_ResamplerName( getStringi, chosen );
	return chosen;
}

static void listResamplers_f( const idCmdArgs& args )
{
	(void)args;
	if( soundSystem->GetOpenALDevice() == NULL || alIsExtensionPresent( "AL_SOFT_source_resampler" ) != AL_TRUE )
	{
		idLib::Printf( "OpenAL resamplers: unavailable\n" );
		return;
	}
	const openq4_alGetStringiSOFT_t getStringi = reinterpret_cast<openq4_alGetStringiSOFT_t>( alGetProcAddress( "alGetStringiSOFT" ) );
	const ALint count = alGetInteger( AL_NUM_RESAMPLERS_SOFT );
	const ALint defaultIndex = alGetInteger( AL_DEFAULT_RESAMPLER_SOFT );
	const ALint active = soundSystemLocal.hardware.GetSourceResampler();
	idLib::Printf( "OpenAL resamplers (s_resampler accepts a name or number):\n" );
	for( ALint i = 0; i < count; i++ )
	{
		const bool isActive = ( active >= 0 ) ? ( i == active ) : ( i == defaultIndex );
		idLib::Printf( "  %c %d: %s%s\n", isActive ? '*' : ' ', i, openQ4_ResamplerName( getStringi, i ), i == defaultIndex ? " (runtime default)" : "" );
	}
	CheckALErrors();
}
#else
	#define OPENQ4_OPENAL_RESAMPLER_SUPPORTED 0
static ALint openQ4_SelectResampler( idStr& chosenName )
{
	chosenName = "runtime default";
	return -1;
}

static void listResamplers_f( const idCmdArgs& args )
{
	(void)args;
	idLib::Printf( "OpenAL resamplers: unavailable\n" );
}
#endif

/*
========================
idSoundHardware_OpenAL::GetAirAbsorptionGainHF
========================
*/
float idSoundHardware_OpenAL::GetAirAbsorptionGainHF() const
{
	if( !efxEnabled || primaryReverbSlot < 0 || primaryReverbSlot >= SOUND_REVERB_SLOTS )
	{
		return 1.0f;
	}
	const float gain = appliedReverb[primaryReverbSlot].airAbsorptionGainHF;
	return ( FLOAT_IS_NAN( gain ) || gain <= 0.0f || gain > 1.0f ) ? 1.0f : gain;
}

float idSoundHardware_OpenAL::GetReverbSlotRoomRolloff( int slot ) const
{
	if( !efxEnabled || slot < 0 || slot >= SOUND_REVERB_SLOTS || appliedReverbSlot[slot] == 0 )
	{
		return 0.0f;
	}
	const float rolloff = appliedReverb[slot].roomRolloffFactor;
	return ( FLOAT_IS_NAN( rolloff ) || rolloff <= 0.0f ) ? 0.0f : rolloff;
}

/*
========================
idSoundHardware_OpenAL::HasCenterChannelOutput

True when the context renders a 5.1/6.1/7.1 layout and can play 5.1 buffers
through direct channels, the only case in which centre routing is used.
========================
*/
bool idSoundHardware_OpenAL::HasCenterChannelOutput() const
{
#if defined( ALC_OUTPUT_MODE_SOFT ) && defined( AL_FORMAT_51CHN16 ) && defined( AL_DIRECT_CHANNELS_SOFT )
	// The extensions are the context's (DetectVoiceFeatures); the layout is queried
	// live, since a reopen or an in-place reset can change it under every voice.
	if( openalDevice == NULL || !centerRoutingAvailable ) return false;
	ALCint mode = 0;
	alcGetError( openalDevice );
	alcGetIntegerv( openalDevice, ALC_OUTPUT_MODE_SOFT, 1, &mode );
	if( alcGetError( openalDevice ) != ALC_NO_ERROR ) return false;
	return mode == ALC_SURROUND_5_1_SOFT || mode == ALC_SURROUND_6_1_SOFT || mode == ALC_SURROUND_7_1_SOFT;
#else
	return false;
#endif
}

uint64 idSoundHardware_OpenAL::QueuedBufferBytes() const {
	uint64 total = 0;
	for (int i = 0; i < voices.Num(); ++i) {
		for (int j = 0; j < 3; ++j) {
			const ALuint buffer = voices[i].openalStreamingBuffer[j];
			if (buffer == 0) continue;
			ALint bytes = 0;
			alGetBufferi(buffer, AL_SIZE, &bytes);
			if (CheckALErrors() == AL_NO_ERROR && bytes > 0) total += bytes;
		}
	}
	return total;
}

/*
========================
idSoundHardware_OpenAL::DetectVoiceFeatures

What every voice can use on this context. Called with the context current.
========================
*/
void idSoundHardware_OpenAL::DetectVoiceFeatures()
{
	sourceRadiusAvailable = false;
	directChannelsAvailable = false;
	directChannelsMode = AL_FALSE;
	sourceResampler = -1;

#if defined( AL_SOURCE_RADIUS )
	sourceRadiusAvailable = alIsExtensionPresent( "AL_EXT_SOURCE_RADIUS" ) == AL_TRUE;
#endif
#if defined( AL_DIRECT_CHANNELS_SOFT )
	if( alIsExtensionPresent( "AL_SOFT_direct_channels" ) == AL_TRUE )
	{
		directChannelsAvailable = true;
		directChannelsMode = AL_TRUE;
#if defined( AL_REMIX_UNMATCHED_SOFT )
		// remix rather than drop channels the output does not have
		if( alIsExtensionPresent( "AL_SOFT_direct_channels_remix" ) == AL_TRUE )
		{
			directChannelsMode = AL_REMIX_UNMATCHED_SOFT;
		}
#endif
	}
#endif
	centerRoutingAvailable = directChannelsAvailable && openalDevice != NULL &&
		alIsExtensionPresent( "AL_EXT_MCFORMATS" ) == AL_TRUE &&
		alcIsExtensionPresent( openalDevice, "ALC_SOFT_output_mode" ) == ALC_TRUE;
	idStr resamplerName;
	sourceResampler = openQ4_SelectResampler( resamplerName );

	// Doom units are inches, and OpenAL reads AL_SPEED_OF_SOUND in the units of the
	// velocities it is given, so its 343.3 default meant 343 inches a second.
	alSpeedOfSound( 343.3f / 0.0254f );
	alDopplerFactor( 1.0f );
	CheckALErrors();

	common->Printf( "OpenAL voices: source radius %s, direct stereo %s, resampler %s\n",
		sourceRadiusAvailable ? "on" : "unavailable",
		directChannelsAvailable ? ( directChannelsMode == AL_TRUE ? "on" : "on (remixed)" ) : "unavailable",
		resamplerName.c_str() );
}

void listDevices_f( const idCmdArgs& args )
{
	idStrList deviceNames;
	idStr defaultDeviceName;
	idSoundHardware_OpenAL::GetAvailablePlaybackDevices( deviceNames, defaultDeviceName );

	idLib::Printf( "Available playback devices:\n" );
	if( deviceNames.Num() == 0 ) {
		idLib::Printf( "    !!! none !!!\n" );
	} else {
		for( int i = 0; i < deviceNames.Num(); ++i ) {
			idLib::Printf( "    %s\n", deviceNames[i].c_str() );
		}
	}

	//idLib::Printf("Available capture devices:\n");
	//printDeviceList(alcGetString(NULL, ALC_CAPTURE_DEVICE_SPECIFIER));

	idLib::Printf( "Default playback device: %s\n", defaultDeviceName.Length() ? defaultDeviceName.c_str() : "<unknown>" );
	const idStr requestedDeviceName = idSoundHardware_OpenAL::IsDefaultDeviceChoiceValue( s_deviceName.GetString() ) ? "" : s_deviceName.GetString();
	idLib::Printf( "Requested playback device: %s\n", requestedDeviceName.Length() ? requestedDeviceName.c_str() : "<system default>" );

	//idLib::Printf("Default capture device: %s\n", alcGetString(NULL, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER));

	idSoundHardware_OpenAL::PrintALCInfo( NULL );

	idSoundHardware_OpenAL::PrintALCInfo( ( ALCdevice* )soundSystem->GetOpenALDevice() );
}

/*
========================
idSoundHardware_OpenAL::Init
========================
*/
void idSoundHardware_OpenAL::Init()
{
	if (SoundSettings_BlockAutomaticRestart()) return;
	SoundSettings_DeviceInitialized();
	static bool listDevicesCommandAdded = false;
	if( !listDevicesCommandAdded )
	{
		listDevicesCommandAdded = true;
		cmdSystem->AddCommand( "listDevices", listDevices_f, 0, "Lists the connected sound devices", NULL );
		cmdSystem->AddCommand( "listResamplers", listResamplers_f, 0, "Lists the OpenAL resamplers s_resampler can pick", NULL );
		cmdSystem->AddCommand( "s_testDeviceInterruption", testDeviceInterruption_f, CMD_FL_SOUND | CMD_FL_CHEAT,
			"stops every sound as a lost audio device does and refuses new ones for [msec] (default 1000), then reports the device returned", NULL );
	}

	// Periodic re-init retries after a failure run silently so a missing
	// device does not spam the console once per second.
	const bool quietRetry = initFailed;

	if( !quietRetry )
	{
		common->Printf( "Setup OpenAL device and context... " );
	}

	if( IsDefaultDeviceChoiceValue( s_deviceName.GetString() ) ) {
		s_deviceName.SetString( "" );
	}

	const idStr requestedDeviceName = NormalizeRequestedDeviceName( s_deviceName.GetString() );
	if( requestedDeviceName.Length() > 0 )
	{
		openalDevice = alcOpenDevice( requestedDeviceName.c_str() );
		if( openalDevice == NULL )
		{
			common->Warning( "OpenAL device '%s' unavailable, falling back to the system default device.", requestedDeviceName.c_str() );
		}
	}

	if( openalDevice == NULL )
	{
		openalDevice = alcOpenDevice( NULL );
	}
	if( openalDevice == NULL )
	{
		if( !quietRetry )
		{
			common->Printf( "Failed.\n" );
			common->Warning( "idSoundHardware_OpenAL::Init: alcOpenDevice() failed; continuing without sound (s_noSound 1); retrying periodically." );
		}
		initFailed = true;
		s_noSound.SetBool( true );
		s_noSound.ClearModified();
		return;
	}

	openQ4_ReportHrtfPreference( openalDevice );

	ALCint openalContextAttributes[ OPENQ4_OPENAL_DEVICE_ATTRIBUTE_CAPACITY ];
	const ALCint* requestedContextAttributes = openQ4_BuildDeviceAttributes( openalDevice, openalContextAttributes, true );
	openalContext = alcCreateContext( openalDevice, requestedContextAttributes );
	if( openalContext == NULL && requestedContextAttributes != NULL )
	{
		CheckALCErrors( openalDevice );
		common->Warning( "idSoundHardware_OpenAL::Init: alcCreateContext() rejected the requested OpenAL device attributes; retrying with runtime default." );
		openalContext = alcCreateContext( openalDevice, NULL );
	}
	if( openalContext == NULL )
	{
		if( !quietRetry )
		{
			common->Printf( "Failed.\n" );
			common->Warning( "idSoundHardware_OpenAL::Init: alcCreateContext() failed; continuing without sound (s_noSound 1); retrying periodically." );
		}
		alcCloseDevice( openalDevice );
		openalDevice = NULL;
		initFailed = true;
		s_noSound.SetBool( true );
		s_noSound.ClearModified();
		return;
	}

	if( alcMakeContextCurrent( openalContext ) == 0 )
	{
		if( !quietRetry )
		{
			common->Printf( "Failed.\n" );
			common->Warning( "idSoundHardware_OpenAL::Init: alcMakeContextCurrent() failed; continuing without sound (s_noSound 1); retrying periodically." );
		}
		alcDestroyContext( openalContext );
		openalContext = NULL;
		alcCloseDevice( openalDevice );
		openalDevice = NULL;
		initFailed = true;
		s_noSound.SetBool( true );
		s_noSound.ClearModified();
		return;
	}

	// A restart or a device retry must retain an outstanding app suspension.
	SetDevicePaused( devicePauseRequested );
	if( !quietRetry )
	{
		common->Printf( "Done.\n" );
	}

	if( initFailed )
	{
		// A previous Init() failure silenced the engine and the periodic
		// retry just recovered the device. The engine's own s_noSound write
		// cleared the modified flag, so a set flag here means the user
		// changed the cvar since the failure; respect that and only lift
		// engine-set silence.
		initFailed = false;
		if( s_noSound.GetBool() && !s_noSound.IsModified() )
		{
			s_noSound.SetBool( false );
			s_noSound.ClearModified();
		}
		soundSystemLocal.SetNeedsRestart();
		common->Printf( "OpenAL device recovered; scheduling sound system restart.\n" );
	}

	ALCint alcMajor = 0;
	ALCint alcMinor = 0;
	bool openALVersionSupported = true;
	alcGetIntegerv( openalDevice, ALC_MAJOR_VERSION, 1, &alcMajor );
	alcGetIntegerv( openalDevice, ALC_MINOR_VERSION, 1, &alcMinor );
	if( CheckALCErrors( openalDevice ) == ALC_NO_ERROR )
	{
		common->Printf( "OpenAL ALC version: %d.%d\n", alcMajor, alcMinor );
		if( alcMajor < 1 || ( alcMajor == 1 && alcMinor < 1 ) )
		{
			openALVersionSupported = false;
			common->Warning( "OpenAL runtime reports unsupported ALC version %d.%d (expected 1.1+).", alcMajor, alcMinor );
		}
	}

	common->Printf( "OpenAL vendor: %s\n", alGetString( AL_VENDOR ) );
	common->Printf( "OpenAL renderer: %s\n", alGetString( AL_RENDERER ) );
	common->Printf( "OpenAL version: %s\n", alGetString( AL_VERSION ) );
	common->Printf( "OpenAL extensions: %s\n", alGetString( AL_EXTENSIONS ) );
	// What the device granted rather than what was asked for: a mobile provider
	// (OpenSL ES/AAudio on Android) can choose its own mixing rate, update
	// period and source limits (AND-02 qualifies the provider on devices).
	{
		ALCint attributeCount = 0;
		alcGetIntegerv( openalDevice, ALC_ATTRIBUTES_SIZE, 1, &attributeCount );
		if( CheckALCErrors( openalDevice ) == ALC_NO_ERROR && attributeCount > 0 && attributeCount <= 256 )
		{
			ALCint attributes[256] = {};
			alcGetIntegerv( openalDevice, ALC_ALL_ATTRIBUTES, attributeCount, attributes );
			if( CheckALCErrors( openalDevice ) == ALC_NO_ERROR )
			{
				int frequency = 0, refresh = 0, monoSources = 0, stereoSources = 0;
				for( int i = 0; i + 1 < attributeCount && attributes[i] != 0; i += 2 )
				{
					switch( attributes[i] )
					{
						case ALC_FREQUENCY: frequency = attributes[i + 1]; break;
						case ALC_REFRESH: refresh = attributes[i + 1]; break;
						case ALC_MONO_SOURCES: monoSources = attributes[i + 1]; break;
						case ALC_STEREO_SOURCES: stereoSources = attributes[i + 1]; break;
						default: break;
					}
				}
				common->Printf( "OpenAL device attributes: frequency=%d refresh=%d monoSources=%d stereoSources=%d\n",
					frequency, refresh, monoSources, stereoSources );
			}
		}
	}

	deferredUpdatesAvailable = alIsExtensionPresent( "AL_SOFT_deferred_updates" ) == AL_TRUE && openQ4_LoadDeferredUpdateProcs();
	deferredUpdatesActive = false;
	if( deferredUpdatesAvailable )
	{
		common->Printf( "OpenAL deferred source updates enabled.\n" );
	}
	if( idSoundVoice_OpenAL::SourceLatencyQueriesAvailable() )
	{
		common->Printf( "OpenAL source latency queries enabled.\n" );
	}
	if( alIsExtensionPresent( "AL_SOFT_callback_buffer" ) == AL_TRUE )
	{
		common->Printf( "OpenAL callback buffers available.\n" );
	}
	DetectVoiceFeatures();
#if defined( ALC_CONNECTED )
	disconnectQueryAvailable = alcIsExtensionPresent( openalDevice, "ALC_EXT_disconnect" ) == ALC_TRUE;
#endif
	simulatedInterruptionEnd = 0;
	NoteOutputChanged( "device opened" );

	CaptureOpenedDeviceState( requestedDeviceName.c_str() );
	reopenDeviceAvailable = alcIsExtensionPresent( openalDevice, "ALC_SOFT_reopen_device" ) == ALC_TRUE && openQ4_LoadReopenDeviceProc( openalDevice );
	if( reopenDeviceAvailable )
	{
		common->Printf( "OpenAL in-place device reopen available.\n" );
	}
	EnableDeviceEventMonitoring();
	if( openedRequestedDeviceName.Length() == 0 ) {
		common->Printf( "OpenAL requested device: system default\n" );
	} else {
		common->Printf( "OpenAL requested device: %s\n", openedRequestedDeviceName.c_str() );
	}
	if( openedDefaultDeviceName.Length() > 0 ) {
		common->Printf( "OpenAL default device: %s\n", openedDefaultDeviceName.c_str() );
	}
	if( openedActiveDeviceName.Length() > 0 ) {
		common->Printf( "OpenAL active device: %s\n", openedActiveDeviceName.c_str() );
	}
	if( openedWithDefaultFallback ) {
		common->Warning( "OpenAL requested device '%s' is unavailable; using '%s' until it returns.", openedRequestedDeviceName.c_str(), openedActiveDeviceName.c_str() );
	}
	openQ4_ReportHrtfStatus( openalDevice );
	openQ4_ReportOutputMode( openalDevice );
	openQ4_ReportOutputLimiter( openalDevice );

	efxEnabled = false;
	efxFiltersAvailable = false;
	auxEffectSlot = 0;
	auxReverbEffect = 0;
	maxAuxiliarySends = 0;
	ResetReverbSlotState();
#if OPENQ4_OPENAL_EFX_SUPPORTED
	const bool efxExtensionPresent = openALVersionSupported && alcIsExtensionPresent( openalDevice, "ALC_EXT_EFX" ) == AL_TRUE;
	efxFiltersAvailable = efxExtensionPresent;
	if( efxFiltersAvailable )
	{
		common->Printf( "OpenAL EFX filters available.\n" );
#if defined( ALC_MAX_AUXILIARY_SENDS )
		// retail EAX gave every source two active sends: its own area and the listener's
		ALCint sends = 0;
		alcGetIntegerv( openalDevice, ALC_MAX_AUXILIARY_SENDS, 1, &sends );
		if( CheckALCErrors( openalDevice ) == ALC_NO_ERROR && sends > 0 )
		{
			maxAuxiliarySends = sends;
		}
#endif
	}
	if( s_useEAXReverb.GetBool() && efxExtensionPresent && openQ4_LoadHardwareEfxProcs() )
	{
		qalGenEffects( 1, &auxReverbEffect );
		if( CheckALErrors() == AL_NO_ERROR && auxReverbEffect != 0 )
		{
#if defined( AL_EFFECT_EAXREVERB )
			qalEffecti( auxReverbEffect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB );
			if( CheckALErrors() != AL_NO_ERROR )
#endif
			{
#if defined( AL_EFFECT_REVERB )
				qalEffecti( auxReverbEffect, AL_EFFECT_TYPE, AL_EFFECT_REVERB );
				CheckALErrors();
#endif
			}

			qalGenAuxiliaryEffectSlots( 1, &auxEffectSlot );
			if( CheckALErrors() == AL_NO_ERROR && auxEffectSlot != 0 )
			{
				qalAuxiliaryEffectSloti( auxEffectSlot, AL_EFFECTSLOT_EFFECT, auxReverbEffect );
				if( CheckALErrors() == AL_NO_ERROR )
				{
					efxEnabled = true;
					common->Printf( "OpenAL EFX reverb send enabled (%d auxiliary sends).\n", maxAuxiliarySends );
				}
			}
		}

		if( !efxEnabled )
		{
			if( auxEffectSlot != 0 )
			{
				qalDeleteAuxiliaryEffectSlots( 1, &auxEffectSlot );
				auxEffectSlot = 0;
			}
			if( auxReverbEffect != 0 )
			{
				qalDeleteEffects( 1, &auxReverbEffect );
				auxReverbEffect = 0;
			}
			CheckALErrors();
			common->Warning( "OpenAL EFX requested but unavailable; wet send disabled." );
		}
	}
	else if( s_useEAXReverb.GetBool() && !openALVersionSupported )
	{
		common->Warning( "OpenAL EFX requested, but the runtime OpenAL version is older than 1.1; wet send disabled." );
	}
	else if( s_useEAXReverb.GetBool() && efxExtensionPresent )
	{
		common->Warning( "OpenAL EFX extension reported but required EFX entry points are missing; wet send disabled." );
	}
	else if( s_useEAXReverb.GetBool() )
	{
		common->Warning( "OpenAL EFX extension not reported by device; wet send disabled." );
	}
#else
	if( s_useEAXReverb.GetBool() )
	{
		// Expected with default cvars on providers without EFX (e.g. Apple's
		// OpenAL framework); status line, not a warning.
		common->Printf( "OpenAL EFX is not available in this build; reverb wet send disabled.\n" );
	}
#endif

	//outputChannels = deviceDetails.OutputFormat.Format.nChannels;
	//channelMask = deviceDetails.OutputFormat.dwChannelMask;

	//idSoundVoice::InitSurround( outputChannels, channelMask );

#if defined(USE_DOOMCLASSIC)
	// ---------------------
	// Initialize the Doom classic sound system.
	// ---------------------
	I_InitSoundHardware( voices.Max(), 0 );
#endif

	// ---------------------
	// Create VU Meter Effect
	// ---------------------
	/*
	IUnknown* vuMeter = NULL;
	XAudio2CreateVolumeMeter( &vuMeter, 0 );

	XAUDIO2_EFFECT_DESCRIPTOR descriptor;
	descriptor.InitialState = true;
	descriptor.OutputChannels = outputChannels;
	descriptor.pEffect = vuMeter;

	XAUDIO2_EFFECT_CHAIN chain;
	chain.EffectCount = 1;
	chain.pEffectDescriptors = &descriptor;

	pMasterVoice->SetEffectChain( &chain );

	vuMeter->Release();
	*/

	// ---------------------
	// Create VU Meter Graph
	// ---------------------

	/*
	vuMeterRMS = console->CreateGraph( outputChannels );
	vuMeterPeak = console->CreateGraph( outputChannels );
	vuMeterRMS->Enable( false );
	vuMeterPeak->Enable( false );

	memset( vuMeterPeakTimes, 0, sizeof( vuMeterPeakTimes ) );

	vuMeterPeak->SetFillMode( idDebugGraph::GRAPH_LINE );
	vuMeterPeak->SetBackgroundColor( idVec4( 0.0f, 0.0f, 0.0f, 0.0f ) );

	vuMeterRMS->AddGridLine( 0.500f, idVec4( 0.5f, 0.5f, 0.5f, 1.0f ) );
	vuMeterRMS->AddGridLine( 0.250f, idVec4( 0.5f, 0.5f, 0.5f, 1.0f ) );
	vuMeterRMS->AddGridLine( 0.125f, idVec4( 0.5f, 0.5f, 0.5f, 1.0f ) );

	const char* channelNames[] = { "L", "R", "C", "S", "Lb", "Rb", "Lf", "Rf", "Cb", "Ls", "Rs" };
	for( int i = 0, ci = 0; ci < sizeof( channelNames ) / sizeof( channelNames[0] ); ci++ )
	{
		if( ( channelMask & BIT( ci ) ) == 0 )
		{
			continue;
		}
		vuMeterRMS->SetLabel( i, channelNames[ ci ] );
		i++;
	}
	*/

	// OpenAL doesn't really impose a maximum number of sources
	voices.SetNum( voices.Max() );
	freeVoices.SetNum( voices.Max() );
	zombieVoices.SetNum( 0 );
	for( int i = 0; i < voices.Num(); i++ )
	{
		freeVoices[i] = &voices[i];
	}
}

// SDL can park the engine loop while OpenAL keeps mixing. Silence the owned
// context immediately and, when both extension functions exist, stop the device.
// Keep the resume function for this exact device so partial extension support
// cannot leave it paused. The requested state survives device replacement.
void idSoundHardware_OpenAL::SetDevicePaused( bool paused )
{
	devicePauseRequested = paused;
	if( openalContext != NULL && alcGetCurrentContext() == openalContext )
	{
		alListenerf( AL_GAIN, ( paused || soundSystem->IsMuted() ) ? 0.0f : 1.0f );
		EndDeferredUpdates();
	}
	if( openalDevice == NULL )
	{
		return;
	}
	if( !paused )
	{
		if( deviceResume != NULL )
		{
			deviceResume( openalDevice );
			deviceResume = NULL;
		}
		return;
	}
	if( deviceResume == NULL && alcIsExtensionPresent( openalDevice, "ALC_SOFT_pause_device" ) == ALC_TRUE )
	{
		const devicePauseFunction_t pauseDevice = reinterpret_cast<devicePauseFunction_t>(
			alcGetProcAddress( openalDevice, "alcDevicePauseSOFT" ) );
		const devicePauseFunction_t resumeDevice = reinterpret_cast<devicePauseFunction_t>(
			alcGetProcAddress( openalDevice, "alcDeviceResumeSOFT" ) );
		if( pauseDevice != NULL && resumeDevice != NULL )
		{
			pauseDevice( openalDevice );
			deviceResume = resumeDevice;
		}
	}
}

/*
========================
idSoundHardware_OpenAL::Shutdown
========================
*/
void idSoundHardware_OpenAL::Shutdown()
{
	// Closing a paused device releases it; never reuse its extension pointer.
	deviceResume = NULL;
	SoundVoiceChat_Shutdown();
	SoundSettings_DeviceDestroyed();
	EndDeferredUpdates();
	DisableDeviceEventMonitoring();

	for( int i = 0; i < voices.Num(); i++ )
	{
		voices[ i ].DestroyInternal();
	}
	voices.Clear();
	freeVoices.Clear();
	zombieVoices.Clear();
	// Voices released streams whose blocks were still decoding: join them before
	// s_restart or shutdown closes their files and reservations.
	idSoundOggStream::ReapReleased( true );

	#if OPENQ4_OPENAL_EFX_SUPPORTED
		// the sources are gone, so nothing still feeds the area slots
		for( int i = 0; i < SOUND_REVERB_SLOTS - 1; i++ )
		{
			if( areaReverbSlots[i] != 0 )
			{
				if ( qalAuxiliaryEffectSloti != NULL ) {
					qalAuxiliaryEffectSloti( areaReverbSlots[i], AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );
				}
				if ( qalDeleteAuxiliaryEffectSlots != NULL ) {
					qalDeleteAuxiliaryEffectSlots( 1, &areaReverbSlots[i] );
				}
				areaReverbSlots[i] = 0;
			}
			if( areaReverbEffects[i] != 0 )
			{
				if ( qalDeleteEffects != NULL ) {
					qalDeleteEffects( 1, &areaReverbEffects[i] );
				}
				areaReverbEffects[i] = 0;
			}
		}
		if( auxEffectSlot != 0 )
		{
			if ( qalAuxiliaryEffectSloti != NULL ) {
				qalAuxiliaryEffectSloti( auxEffectSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );
			}
			if ( qalDeleteAuxiliaryEffectSlots != NULL ) {
				qalDeleteAuxiliaryEffectSlots( 1, &auxEffectSlot );
			}
			auxEffectSlot = 0;
		}
		if( auxReverbEffect != 0 )
		{
			if ( qalDeleteEffects != NULL ) {
				qalDeleteEffects( 1, &auxReverbEffect );
			}
			auxReverbEffect = 0;
		}
	#endif
	efxEnabled = false;
	efxFiltersAvailable = false;
	maxAuxiliarySends = 0;
	ResetReverbSlotState();

#if defined(USE_DOOMCLASSIC)
	// ---------------------
	// Shutdown the Doom classic sound system.
	// ---------------------
	I_ShutdownSoundHardware();
#endif

	alcMakeContextCurrent( NULL );

	alcDestroyContext( openalContext );
	openalContext = NULL;

	alcCloseDevice( openalDevice );
	openalDevice = NULL;
	lastDeviceCheckTime = 0;
	lastPerfPrintTime = 0;
	reopenDeviceAvailable = false;
	deferredUpdatesAvailable = false;
	deferredUpdatesActive = false;
	openedWithDefaultFallback = false;
	openedHrtfMode = OPENQ4_OPENAL_HRTF_AUTO;
	openedSpeakerCount = OPENQ4_OPENAL_SPEAKERS_DEVICE;
	openedOutputLimiter = true;
	sourceRadiusAvailable = false;
	directChannelsAvailable = false;
	directChannelsMode = AL_FALSE;
	sourceResampler = -1;
	centerRoutingAvailable = false;
	outputCenterObserved = false;
	disconnectQueryAvailable = false;
	simulatedInterruptionEnd = 0;
	openedRequestedDeviceName.Clear();
	openedActiveDeviceName.Clear();
	openedDefaultDeviceName.Clear();

	/*
	if( vuMeterRMS != NULL )
	{
		console->DestroyGraph( vuMeterRMS );
		vuMeterRMS = NULL;
	}
	if( vuMeterPeak != NULL )
	{
		console->DestroyGraph( vuMeterPeak );
		vuMeterPeak = NULL;
	}
	*/
}

/*
========================
idSoundHardware_OpenAL::AllocateVoice
========================
*/
idSoundVoice* idSoundHardware_OpenAL::AllocateVoice( const idSoundSample* leadinSample, const idSoundSample* loopingSample )
{
	if( leadinSample == NULL )
	{
		return NULL;
	}
	// A lost endpoint stops every source it is given. Channels keep their clocks
	// and get voices again once it returns or another device is opened.
	if( !IsOutputConnected() )
	{
		return NULL;
	}
	if( loopingSample != NULL )
	{
		if( ( leadinSample->format.basic.formatTag != loopingSample->format.basic.formatTag ) ||
			( leadinSample->format.basic.numChannels != loopingSample->format.basic.numChannels ) ||
			( leadinSample->format.basic.samplesPerSec != loopingSample->format.basic.samplesPerSec ) )
		{
			idLib::Warning( "Leadin/looping format mismatch: %s & %s", leadinSample->GetName(), loopingSample->GetName() );
			loopingSample = NULL;
		}
	}

	// Try to find a free voice that matches the format
	// But fallback to the last free voice if none match the format
	idSoundVoice* voice = NULL;
	for( int i = 0; i < freeVoices.Num(); i++ )
	{
		if( freeVoices[i]->IsPlaying() )
		{
			continue;
		}
		voice = ( idSoundVoice* )freeVoices[i];
		if( voice->CompatibleFormat( ( idSoundSample_OpenAL* )leadinSample ) )
		{
			break;
		}
	}
	if( voice != NULL )
	{
		voice->Create( leadinSample, loopingSample );
		idSoundVoice_OpenAL* openalVoice = ( idSoundVoice_OpenAL* )voice;
		if( !alIsSource( openalVoice->openalSource ) )
		{
			return NULL;
		}
		freeVoices.Remove( openalVoice );
		return voice;
	}

	return NULL;
}

/*
========================
idSoundHardware_OpenAL::FreeVoice
========================
*/
void idSoundHardware_OpenAL::FreeVoice( idSoundVoice* voice )
{
	if( voice == NULL )
	{
		return;
	}

	idSoundVoice_OpenAL* openalVoice = ( idSoundVoice_OpenAL* )voice;
	voice->Stop();
	zombieVoices.Remove( openalVoice );

	if( !openalVoice->IsPlaying() )
	{
		freeVoices.AddUnique( openalVoice );
		return;
	}

	zombieVoices.AddUnique( openalVoice );
}

/*
========================
OpenQ4_DiagnosticSampleName
========================
*/
static void OpenQ4_DiagnosticSampleName( const idSoundSample_OpenAL* sample, idStr& name )
{
	if( sample == NULL )
	{
		name = "<none>";
		return;
	}

	name = sample->GetName();

	idStr extension;
	name.ExtractFileExtension( extension );
	if( extension.Length() == 0 && sample->IsLoaded() )
	{
		name += sample->IsCompressed() ? ".ogg" : ".wav";
	}
}

/*
========================
idSoundHardware_OpenAL::PrintDistanceInfo
========================
*/
void idSoundHardware_OpenAL::PrintDistanceInfo( const idSoundVoice* voice ) const
{
	if( openalDevice == NULL || voice == NULL )
	{
		idLib::Printf( "SOUND_DISTANCE_BACKEND available=0\n" );
		return;
	}
	const idSoundVoice_OpenAL* source = static_cast<const idSoundVoice_OpenAL*>( voice );
	if( !alIsSource( source->openalSource ) )
	{
		idLib::Printf( "SOUND_DISTANCE_BACKEND available=0\n" );
		return;
	}
	ALfloat position[3] = {}, gain = 0, listenerGain = 0, rolloff = 0, reference = 0, radius = -1;
	ALint state = AL_INITIAL;
	alGetSourcefv( source->openalSource, AL_POSITION, position );
	alGetSourcef( source->openalSource, AL_GAIN, &gain );
	alGetSourcef( source->openalSource, AL_ROLLOFF_FACTOR, &rolloff );
	alGetSourcef( source->openalSource, AL_REFERENCE_DISTANCE, &reference );
	alGetSourcei( source->openalSource, AL_SOURCE_STATE, &state );
	alGetListenerf( AL_GAIN, &listenerGain );
#if defined( AL_SOURCE_RADIUS )
	if( HasSourceRadius() ) alGetSourcef( source->openalSource, AL_SOURCE_RADIUS, &radius );
#endif
	if( CheckALErrors() != AL_NO_ERROR )
	{
		idLib::Printf( "SOUND_DISTANCE_BACKEND available=0 error=1\n" );
		return;
	}
	idLib::Printf( "SOUND_DISTANCE_BACKEND available=1 source=%u playing=%d gain=%.7f listenerGain=%.7f "
		"position=%.4f,%.4f,%.4f rolloff=%.4f reference=%.4f innerRadius=%.4f sourceRadius=%.4f\n",
		source->openalSource, state == AL_PLAYING ? 1 : 0, gain, listenerGain,
		position[0], position[1], position[2], rolloff, reference, source->innerRadius, radius );
}

/*
========================
idSoundHardware_OpenAL::ListPlayingVoices
========================
*/
int idSoundHardware_OpenAL::ListPlayingVoices() const
{
	if( openalDevice == NULL )
	{
		idLib::Printf( "No active OpenAL device.\n" );
		return 0;
	}

	int numPlaying = 0;
	ALfloat listenerGain = 1.0f;
	alGetListenerf( AL_GAIN, &listenerGain );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		listenerGain = 1.0f;
	}

	idLib::Printf( "Audible OpenAL voices\n" );
	idLib::Printf( " # source   gain  pitch sample\n" );
	idLib::Printf( "--- ------ ------ ------ ----------------\n" );

	for( int i = 0; i < voices.Num(); i++ )
	{
		const idSoundVoice_OpenAL& voice = voices[i];
		if( !alIsSource( voice.openalSource ) )
		{
			continue;
		}

		ALint state = AL_INITIAL;
		alGetSourcei( voice.openalSource, AL_SOURCE_STATE, &state );
		if( CheckALErrors() != AL_NO_ERROR || state != AL_PLAYING )
		{
			continue;
		}

		ALfloat sourceGain = 0.0f;
		alGetSourcef( voice.openalSource, AL_GAIN, &sourceGain );
		if( CheckALErrors() != AL_NO_ERROR || sourceGain <= 0.00001f )
		{
			continue;
		}
		const float effectiveGain = static_cast<float>( sourceGain * listenerGain );
		if( effectiveGain <= 0.00001f )
		{
			continue;
		}

		ALfloat sourcePitch = 1.0f;
		alGetSourcef( voice.openalSource, AL_PITCH, &sourcePitch );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			sourcePitch = voice.pitch;
		}

		ALint sourceType = AL_UNDETERMINED;
		alGetSourcei( voice.openalSource, AL_SOURCE_TYPE, &sourceType );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			sourceType = AL_UNDETERMINED;
		}

		const idSoundSample_OpenAL* sample = NULL;
		if( sourceType == AL_STATIC )
		{
			ALint openalBuffer = 0;
			alGetSourcei( voice.openalSource, AL_BUFFER, &openalBuffer );
			if( CheckALErrors() == AL_NO_ERROR )
			{
				if( voice.leadinSample != NULL && voice.leadinSample->openalBuffer == static_cast<ALuint>( openalBuffer ) )
				{
					sample = voice.leadinSample;
				}
				else if( voice.loopingSample != NULL && voice.loopingSample->openalBuffer == static_cast<ALuint>( openalBuffer ) )
				{
					sample = voice.loopingSample;
				}
			}
		}
		if( sample == NULL )
		{
			sample = voice.loopingSample != NULL ? voice.loopingSample : voice.leadinSample;
		}

		idStr sampleName;
		OpenQ4_DiagnosticSampleName( sample, sampleName );

		idLib::Printf( "%3d %6u %6.3f %6.3f %s\n",
					   numPlaying,
					   static_cast<unsigned int>( voice.openalSource ),
					   effectiveGain,
					   static_cast<float>( sourcePitch ),
					   sampleName.c_str() );
		numPlaying++;
	}

	idLib::Printf( "%d audible OpenAL voices\n", numPlaying );
	return numPlaying;
}

/*
========================
idSoundHardware_OpenAL::ListVoiceOutputState

Each voice in use with the output it started on, its routing and its playback
position, for checking recovery after an output change without a device.
========================
*/
void idSoundHardware_OpenAL::ListVoiceOutputState() const
{
	idLib::Printf( "SOUND_OUTPUT serial=%u connected=%d centre=%d interrupted=%d\n", outputSerial,
		IsOutputConnected() ? 1 : 0, HasCenterChannelOutput() ? 1 : 0, simulatedInterruptionEnd != 0 ? 1 : 0 );
	int count = 0;
	for( int i = 0; i < voices.Num(); i++ )
	{
		const idSoundVoice_OpenAL& voice = voices[i];
		if( !alIsSource( voice.openalSource ) || voice.leadinSample == NULL || freeVoices.FindIndex( const_cast<idSoundVoice_OpenAL*>( &voice ) ) >= 0 )
		{
			continue;
		}
		ALint state = AL_INITIAL;
		ALfloat seconds = 0.0f;
		alGetSourcei( voice.openalSource, AL_SOURCE_STATE, &state );
		alGetSourcef( voice.openalSource, AL_SEC_OFFSET, &seconds );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			continue;
		}
		const char* stateName = state == AL_PLAYING ? "playing" : state == AL_PAUSED ? "paused" : state == AL_STOPPED ? "stopped" : "initial";
		idStr sampleName;
		OpenQ4_DiagnosticSampleName( voice.leadinSample, sampleName );
		idLib::Printf( "SOUND_VOICE source=%u state=%s routed=%d queued=%d serial=%u offsetMs=%.1f sample=%s\n",
			static_cast<unsigned int>( voice.openalSource ), stateName, voice.centerRouting ? 1 : 0,
			voice.UsesBoundedQueue() ? 1 : 0, voice.outputSerial, seconds * 1000.0f, sampleName.c_str() );
		count++;
	}
	idLib::Printf( "SOUND_VOICES_END voices=%d\n", count );
}

/*
========================
idSoundHardware_OpenAL::PrintPerformanceData
========================
*/
void idSoundHardware_OpenAL::PrintPerformanceData()
{
	if( !s_showPerfData.GetBool() )
	{
		return;
	}

	const int nowTime = Sys_Milliseconds();
	if( lastPerfPrintTime + 1000 > nowTime )
	{
		return;
	}
	lastPerfPrintTime = nowTime;

	int activeVoices = 0;
	int latencyVoices = 0;
	float totalLatencyMS = 0.0f;
	float maxLatencyMS = 0.0f;

	for( int i = 0; i < voices.Num(); i++ )
	{
		if( !voices[i].IsPlaying() )
		{
			continue;
		}

		activeVoices++;
		float offsetMS = 0.0f;
		float latencyMS = 0.0f;
		if( voices[i].GetPlaybackLatencyMS( offsetMS, latencyMS ) )
		{
			latencyVoices++;
			totalLatencyMS += latencyMS;
			maxLatencyMS = Max( maxLatencyMS, latencyMS );
		}
	}

	if( latencyVoices > 0 )
	{
		common->Printf( "OpenAL perf: active voices %d/%d, free %d, zombies %d, output latency avg %.2f ms max %.2f ms\n",
			activeVoices,
			voices.Num(),
			freeVoices.Num(),
			zombieVoices.Num(),
			totalLatencyMS / latencyVoices,
			maxLatencyMS );
	}
	else
	{
		common->Printf( "OpenAL perf: active voices %d/%d, free %d, zombies %d, source latency unavailable\n",
			activeVoices,
			voices.Num(),
			freeVoices.Num(),
			zombieVoices.Num() );
	}
}

/*
========================
idSoundHardware_OpenAL::Update
========================
*/
void idSoundHardware_OpenAL::Update()
{
	// Free streams whose cancelled blocks the workers have let go of; never waits.
	idSoundOggStream::ReapReleased( false );
	if( openalDevice == NULL )
	{
		int nowTime = Sys_Milliseconds();
		if( lastResetTime + 1000 < nowTime )
		{
			lastResetTime = nowTime;
			Init();
		}
		return;
	}

	if( UpdateDeviceMonitoring() ) {
		return;
	}

	if( devicePauseRequested || soundSystem->IsMuted() )
	{
		alListenerf( AL_GAIN, 0.0f );
	}
	else
	{
		alListenerf( AL_GAIN, 1.0f );
	}

	// Stop() has already been requested for every zombie voice. Keep retrying
	// until the backend reports that playback is fully drained.
	for( int i = 0; i < zombieVoices.Num(); i++ )
	{
		zombieVoices[i]->FlushSourceBuffers();
		if( !zombieVoices[i]->IsPlaying() )
		{
			freeVoices.AddUnique( zombieVoices[i] );
			zombieVoices.RemoveIndex( i );
			i--;
		}
		else
		{
			static int playingZombies;
			playingZombies++;
		}
	}

	PrintPerformanceData();

	/*
	if( s_showPerfData.GetBool() )
	{
		XAUDIO2_PERFORMANCE_DATA perfData;
		pXAudio2->GetPerformanceData( &perfData );
		idLib::Printf( "Voices: %d/%d CPU: %.2f%% Mem: %dkb\n", perfData.ActiveSourceVoiceCount, perfData.TotalSourceVoiceCount, perfData.AudioCyclesSinceLastQuery / ( float )perfData.TotalCyclesSinceLastQuery, perfData.MemoryUsageInBytes / 1024 );
	}
	*/

	/*
	if( vuMeterRMS == NULL )
	{
		// Init probably hasn't been called yet
		return;
	}

	vuMeterRMS->Enable( s_showLevelMeter.GetBool() );
	vuMeterPeak->Enable( s_showLevelMeter.GetBool() );

	if( !s_showLevelMeter.GetBool() )
	{
		pMasterVoice->DisableEffect( 0 );
		return;
	}
	else
	{
		pMasterVoice->EnableEffect( 0 );
	}

	float peakLevels[ 8 ];
	float rmsLevels[ 8 ];

	XAUDIO2FX_VOLUMEMETER_LEVELS levels;
	levels.ChannelCount = outputChannels;
	levels.pPeakLevels = peakLevels;
	levels.pRMSLevels = rmsLevels;

	if( levels.ChannelCount > 8 )
	{
		levels.ChannelCount = 8;
	}

	pMasterVoice->GetEffectParameters( 0, &levels, sizeof( levels ) );

	int currentTime = Sys_Milliseconds();
	for( int i = 0; i < outputChannels; i++ )
	{
		if( vuMeterPeakTimes[i] < currentTime )
		{
			vuMeterPeak->SetValue( i, vuMeterPeak->GetValue( i ) * 0.9f, colorRed );
		}
	}

	float width = 20.0f;
	float height = 200.0f;
	float left = 100.0f;
	float top = 100.0f;

	sscanf( s_meterPosition.GetString(), "%f %f %f %f", &left, &top, &width, &height );

	vuMeterRMS->SetPosition( left, top, width * levels.ChannelCount, height );
	vuMeterPeak->SetPosition( left, top, width * levels.ChannelCount, height );

	for( uint32 i = 0; i < levels.ChannelCount; i++ )
	{
		vuMeterRMS->SetValue( i, rmsLevels[ i ], idVec4( 0.5f, 1.0f, 0.0f, 1.00f ) );
		if( peakLevels[ i ] >= vuMeterPeak->GetValue( i ) )
		{
			vuMeterPeak->SetValue( i, peakLevels[ i ], colorRed );
			vuMeterPeakTimes[i] = currentTime + s_meterTopTime.GetInteger();
		}
	}
	*/
}

/*
========================
idSoundHardware_OpenAL reverb slots

The four retail area reverb slots (see idSoundReverb). Slot 0 is the
auxEffectSlot/auxReverbEffect pair whose lifetime the checked settings path
owns; slots 1-3 are created while slot 0 holds its reverb, and released before
slot 0 can be deleted.
========================
*/
void idSoundHardware_OpenAL::ResetReverbSlotState()
{
	areaReverbUnavailable = false;
	primaryReverbSlot = 0;
	trackedReverbSlot = 0;
	for( int i = 0; i < SOUND_REVERB_SLOTS; i++ )
	{
		memset( &appliedReverb[i], 0, sizeof( appliedReverb[i] ) );
		appliedReverbSlot[i] = 0;
		reverbEffectType[i] = 0;
	}
	reverbSlotGeneration++;
}

bool idSoundHardware_OpenAL::HasAreaReverbResources() const
{
	for( int i = 0; i < SOUND_REVERB_SLOTS - 1; i++ )
	{
		if( areaReverbSlots[i] != 0 || areaReverbEffects[i] != 0 )
		{
			return true;
		}
	}
	return false;
}

ALuint idSoundHardware_OpenAL::GetReverbSlotHandle( int slot ) const
{
	if( slot == 0 )
	{
		return auxEffectSlot;
	}
	if( slot > 0 && slot < SOUND_REVERB_SLOTS )
	{
		return areaReverbSlots[slot - 1];
	}
	return 0;
}

ALuint idSoundHardware_OpenAL::ReverbEffectHandle( int slot ) const
{
	if( slot == 0 )
	{
		return auxReverbEffect;
	}
	if( slot > 0 && slot < SOUND_REVERB_SLOTS )
	{
		return areaReverbEffects[slot - 1];
	}
	return 0;
}

ALuint idSoundHardware_OpenAL::GetPrimaryAuxEffectSlot() const
{
	const ALuint slot = GetReverbSlotHandle( primaryReverbSlot );
	return slot != 0 ? slot : auxEffectSlot;
}

void idSoundHardware_OpenAL::SetPrimaryReverbSlot( int slot )
{
	if( GetReverbSlotHandle( slot ) != 0 )
	{
		primaryReverbSlot = slot;
	}
}

int idSoundHardware_OpenAL::SyncReverbSlots()
{
#if OPENQ4_OPENAL_EFX_SUPPORTED
	if( auxEffectSlot != trackedReverbSlot )
	{
		// the checked settings path created, replaced or removed slot 0
		trackedReverbSlot = auxEffectSlot;
		primaryReverbSlot = 0;
		appliedReverbSlot[0] = 0;
		reverbEffectType[0] = 0;
		reverbSlotGeneration++;
	}
	const bool reverbUsable = efxEnabled && auxEffectSlot != 0 && auxReverbEffect != 0 && openalContext != NULL && openQ4_LoadHardwareEfxProcs();
	if( !reverbUsable )
	{
		if( HasAreaReverbResources() )
		{
			ReleaseAreaReverbSlots();
		}
		return 0;
	}
	if( !HasAreaReverbResources() && !areaReverbUnavailable && maxAuxiliarySends >= 2 )
	{
		CreateAreaReverbSlots();
	}
	return HasAreaReverbResources() ? SOUND_REVERB_SLOTS : 1;
#else
	return 0;
#endif
}

bool idSoundHardware_OpenAL::CreateAreaReverbSlots()
{
#if OPENQ4_OPENAL_EFX_SUPPORTED
	CheckALErrors();
	ALint type = 0;
	qalGetEffecti( auxReverbEffect, AL_EFFECT_TYPE, &type );
	if( alGetError() != AL_NO_ERROR || ( type != AL_EFFECT_EAXREVERB && type != AL_EFFECT_REVERB ) )
	{
		type = AL_EFFECT_EAXREVERB;
	}

	bool created = true;
	for( int i = 0; i < SOUND_REVERB_SLOTS - 1 && created; i++ )
	{
		qalGenEffects( 1, &areaReverbEffects[i] );
		created = alGetError() == AL_NO_ERROR && areaReverbEffects[i] != 0;
		if( created )
		{
			qalEffecti( areaReverbEffects[i], AL_EFFECT_TYPE, type );
			created = alGetError() == AL_NO_ERROR;
		}
		if( created )
		{
			qalGenAuxiliaryEffectSlots( 1, &areaReverbSlots[i] );
			created = alGetError() == AL_NO_ERROR && areaReverbSlots[i] != 0;
		}
		if( created )
		{
			qalAuxiliaryEffectSloti( areaReverbSlots[i], AL_EFFECTSLOT_EFFECT, areaReverbEffects[i] );
			created = alGetError() == AL_NO_ERROR;
		}
	}

	if( !created )
	{
		// no source feeds a slot created this frame, so they can go at once
		for( int i = 0; i < SOUND_REVERB_SLOTS - 1; i++ )
		{
			if( areaReverbSlots[i] != 0 )
			{
				qalDeleteAuxiliaryEffectSlots( 1, &areaReverbSlots[i] );
				areaReverbSlots[i] = 0;
			}
			if( areaReverbEffects[i] != 0 )
			{
				qalDeleteEffects( 1, &areaReverbEffects[i] );
				areaReverbEffects[i] = 0;
			}
		}
		alGetError();
		areaReverbUnavailable = true;
		common->Warning( "OpenAL EFX could not create the area reverb slots; only the listener's area reverb will play." );
		return false;
	}

	for( int i = 0; i < SOUND_REVERB_SLOTS; i++ )
	{
		reverbEffectType[i] = type;
		if( i > 0 )
		{
			appliedReverbSlot[i] = 0;
		}
	}
	reverbSlotGeneration++;
	return true;
#else
	return false;
#endif
}

void idSoundHardware_OpenAL::ReleaseAreaReverbSlots()
{
#if OPENQ4_OPENAL_EFX_SUPPORTED
	if( !HasAreaReverbResources() )
	{
		primaryReverbSlot = 0;
		return;
	}

	// OpenAL refuses to delete an effect slot that a source still feeds
	for( int i = 0; i < voices.Num(); i++ )
	{
		voices[i].DetachReverbSends();
	}
	for( int i = 0; i < SOUND_REVERB_SLOTS - 1; i++ )
	{
		if( areaReverbSlots[i] != 0 )
		{
			qalAuxiliaryEffectSloti( areaReverbSlots[i], AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );
			qalDeleteAuxiliaryEffectSlots( 1, &areaReverbSlots[i] );
			areaReverbSlots[i] = 0;
		}
		if( areaReverbEffects[i] != 0 )
		{
			qalDeleteEffects( 1, &areaReverbEffects[i] );
			areaReverbEffects[i] = 0;
		}
		appliedReverbSlot[i + 1] = 0;
		reverbEffectType[i + 1] = 0;
	}
	CheckALErrors();
	primaryReverbSlot = 0;
	reverbSlotGeneration++;
#endif
}

void idSoundHardware_OpenAL::SetReverbSlotProperties( int slot, const soundReverbEAX_t& properties )
{
#if OPENQ4_OPENAL_EFX_SUPPORTED
	if( slot < 0 || slot >= SOUND_REVERB_SLOTS || !efxEnabled || !openQ4_LoadHardwareEfxProcs() )
	{
		return;
	}
	const ALuint slotHandle = GetReverbSlotHandle( slot );
	const ALuint effect = ReverbEffectHandle( slot );
	if( slotHandle == 0 || effect == 0 )
	{
		return;
	}

	soundReverbEFX_t efx;
	SoundReverb_ConvertEAXToEFX( properties, efx );
	if( appliedReverbSlot[slot] == slotHandle && SoundReverb_EFXEqual( appliedReverb[slot], efx ) )
	{
		return;
	}

	CheckALErrors();
	if( reverbEffectType[slot] == 0 )
	{
		ALint type = 0;
		qalGetEffecti( effect, AL_EFFECT_TYPE, &type );
		reverbEffectType[slot] = alGetError() == AL_NO_ERROR ? type : AL_EFFECT_NULL;
	}

	if( reverbEffectType[slot] == AL_EFFECT_EAXREVERB )
	{
		qalEffectf( effect, AL_EAXREVERB_DENSITY, efx.density );
		qalEffectf( effect, AL_EAXREVERB_DIFFUSION, efx.diffusion );
		qalEffectf( effect, AL_EAXREVERB_GAIN, efx.gain );
		qalEffectf( effect, AL_EAXREVERB_GAINHF, efx.gainHF );
		qalEffectf( effect, AL_EAXREVERB_GAINLF, efx.gainLF );
		qalEffectf( effect, AL_EAXREVERB_DECAY_TIME, efx.decayTime );
		qalEffectf( effect, AL_EAXREVERB_DECAY_HFRATIO, efx.decayHFRatio );
		qalEffectf( effect, AL_EAXREVERB_DECAY_LFRATIO, efx.decayLFRatio );
		qalEffectf( effect, AL_EAXREVERB_REFLECTIONS_GAIN, efx.reflectionsGain );
		qalEffectf( effect, AL_EAXREVERB_REFLECTIONS_DELAY, efx.reflectionsDelay );
		qalEffectfv( effect, AL_EAXREVERB_REFLECTIONS_PAN, efx.reflectionsPan );
		qalEffectf( effect, AL_EAXREVERB_LATE_REVERB_GAIN, efx.lateReverbGain );
		qalEffectf( effect, AL_EAXREVERB_LATE_REVERB_DELAY, efx.lateReverbDelay );
		qalEffectfv( effect, AL_EAXREVERB_LATE_REVERB_PAN, efx.lateReverbPan );
		qalEffectf( effect, AL_EAXREVERB_ECHO_TIME, efx.echoTime );
		qalEffectf( effect, AL_EAXREVERB_ECHO_DEPTH, efx.echoDepth );
		qalEffectf( effect, AL_EAXREVERB_MODULATION_TIME, efx.modulationTime );
		qalEffectf( effect, AL_EAXREVERB_MODULATION_DEPTH, efx.modulationDepth );
		qalEffectf( effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, efx.airAbsorptionGainHF );
		qalEffectf( effect, AL_EAXREVERB_HFREFERENCE, efx.hfReference );
		qalEffectf( effect, AL_EAXREVERB_LFREFERENCE, efx.lfReference );
		qalEffectf( effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, efx.roomRolloffFactor );
		qalEffecti( effect, AL_EAXREVERB_DECAY_HFLIMIT, efx.decayHFLimit ? AL_TRUE : AL_FALSE );
	}
	else if( reverbEffectType[slot] == AL_EFFECT_REVERB )
	{
		// standard reverb has no low-frequency, echo, modulation or panning controls
		qalEffectf( effect, AL_REVERB_DENSITY, efx.density );
		qalEffectf( effect, AL_REVERB_DIFFUSION, efx.diffusion );
		qalEffectf( effect, AL_REVERB_GAIN, efx.gain );
		qalEffectf( effect, AL_REVERB_GAINHF, efx.gainHF );
		qalEffectf( effect, AL_REVERB_DECAY_TIME, efx.decayTime );
		qalEffectf( effect, AL_REVERB_DECAY_HFRATIO, efx.decayHFRatio );
		qalEffectf( effect, AL_REVERB_REFLECTIONS_GAIN, efx.reflectionsGain );
		qalEffectf( effect, AL_REVERB_REFLECTIONS_DELAY, efx.reflectionsDelay );
		qalEffectf( effect, AL_REVERB_LATE_REVERB_GAIN, efx.lateReverbGain );
		qalEffectf( effect, AL_REVERB_LATE_REVERB_DELAY, efx.lateReverbDelay );
		qalEffectf( effect, AL_REVERB_AIR_ABSORPTION_GAINHF, efx.airAbsorptionGainHF );
		qalEffectf( effect, AL_REVERB_ROOM_ROLLOFF_FACTOR, efx.roomRolloffFactor );
		qalEffecti( effect, AL_REVERB_DECAY_HFLIMIT, efx.decayHFLimit ? AL_TRUE : AL_FALSE );
	}
	else
	{
		return;
	}

	// a slot holds a copy of its effect; binding it again publishes the parameters
	qalAuxiliaryEffectSloti( slotHandle, AL_EFFECTSLOT_EFFECT, effect );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		appliedReverbSlot[slot] = 0;
		return;
	}
	appliedReverb[slot] = efx;
	appliedReverbSlot[slot] = slotHandle;
#endif
}


