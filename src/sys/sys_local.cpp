/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/



#include "sys_local.h"
#include "../framework/KeyInput.h"

const char * sysLanguageNames[] = {
	"english", "spanish", "italian", "german", "french", "russian",
	"polish", "korean", "japanese", "chinese",
	// openQ4 additions, text only: no retail media archive exists for these.
	"brazilian", "czech", "hungarian", "turkish", "ukrainian", NULL
};

idCVar sys_lang( "sys_lang", "english", CVAR_SYSTEM | CVAR_ARCHIVE,  "", sysLanguageNames, idCmdSystem::ArgCompletion_String<sysLanguageNames> );

idSysLocal			sysLocal;
idSys *				sys = &sysLocal;

/*
==============================================================

	Controller rumble mix

	The sound system sends the rumble of what the player hears every frame;
	gameplay pulses (a shot, a hit) ride on top of it until they end. Sending
	either alone would cancel the other a frame later, which is why a weapon's
	own fire never rumbled (#244).

==============================================================
*/

idCVar in_joystickRumbleLog( "in_joystickRumbleLog", "0", CVAR_SYSTEM | CVAR_BOOL, "print each gameplay rumble pulse and the mixed level it sends to the controller" );

static const float SYS_RUMBLE_STOP_THRESHOLD = 0.01f;

static float	sysRumbleSoundLow = 0.0f;
static float	sysRumbleSoundHigh = 0.0f;
static int		sysRumbleSoundDuration = 0;
static float	sysRumblePulseLow = 0.0f;
static float	sysRumblePulseHigh = 0.0f;
static int		sysRumblePulseEnd = 0;

static float Sys_ClampRumble( float value ) {
	// NaN compares false and becomes 0
	return value > 0.0f ? ( value < 1.0f ? value : 1.0f ) : 0.0f;
}

static void Sys_SendMixedJoystickRumble( const char *reason ) {
	const int now = Sys_Milliseconds();
	const bool pulseActive = now < sysRumblePulseEnd;

	float low = sysRumbleSoundLow;
	float high = sysRumbleSoundHigh;
	int duration = sysRumbleSoundDuration;
	if ( pulseActive ) {
		if ( sysRumblePulseLow > low ) {
			low = sysRumblePulseLow;
		}
		if ( sysRumblePulseHigh > high ) {
			high = sysRumblePulseHigh;
		}
		// While the pulse is the stronger, it ends at its own time even if the
		// next frame is late; the next update sends the sound's level again.
		if ( low > sysRumbleSoundLow || high > sysRumbleSoundHigh ) {
			duration = sysRumblePulseEnd - now;
		}
	}

	if ( ( low <= SYS_RUMBLE_STOP_THRESHOLD && high <= SYS_RUMBLE_STOP_THRESHOLD ) || duration <= 0 ) {
		Sys_SetJoystickRumble( 0.0f, 0.0f, 0 );
		return;
	}

	const bool sent = Sys_SetJoystickRumble( low, high, duration );
	if ( reason != NULL && in_joystickRumbleLog.GetBool() ) {
		const char *outcome = "sent";
		if ( !sent ) {
			outcome = ( !cvarSystem->GetCVarBool( "in_joystick" ) || !cvarSystem->GetCVarBool( "in_joystickRumble" ) ) ?
				"not sent, rumble is off" : "not sent, no controller";
		}
		common->Printf( "rumble: %s low %.2f high %.2f for %d ms (sound %.2f/%.2f), %s\n",
			reason, low, high, duration, sysRumbleSoundLow, sysRumbleSoundHigh, outcome );
	}
}

void Sys_UpdateJoystickRumble( float soundLowFrequency, float soundHighFrequency, int soundDurationMsec ) {
	sysRumbleSoundLow = Sys_ClampRumble( soundLowFrequency );
	sysRumbleSoundHigh = Sys_ClampRumble( soundHighFrequency );
	sysRumbleSoundDuration = soundDurationMsec > 0 ? soundDurationMsec : 0;
	if ( sysRumbleSoundDuration == 0 ) {
		sysRumbleSoundLow = sysRumbleSoundHigh = 0.0f;
	}
	Sys_SendMixedJoystickRumble( NULL );
}

void Sys_PulseJoystickRumble( float lowFrequency, float highFrequency, int durationMsec ) {
	const float low = Sys_ClampRumble( lowFrequency );
	const float high = Sys_ClampRumble( highFrequency );
	if ( durationMsec <= 0 || ( low <= SYS_RUMBLE_STOP_THRESHOLD && high <= SYS_RUMBLE_STOP_THRESHOLD ) ) {
		return;
	}
	// a pulse over one still running takes the stronger of each and the later end
	const int now = Sys_Milliseconds();
	const int end = now + ( durationMsec < 2000 ? durationMsec : 2000 );
	if ( now < sysRumblePulseEnd ) {
		sysRumblePulseLow = low > sysRumblePulseLow ? low : sysRumblePulseLow;
		sysRumblePulseHigh = high > sysRumblePulseHigh ? high : sysRumblePulseHigh;
		sysRumblePulseEnd = end > sysRumblePulseEnd ? end : sysRumblePulseEnd;
	} else {
		sysRumblePulseLow = low;
		sysRumblePulseHigh = high;
		sysRumblePulseEnd = end;
	}
	Sys_SendMixedJoystickRumble( "pulse" );
}

/*
==============================================================

	Haptics beyond the two motors (PAD-08)

==============================================================
*/

idCVar in_joystickDirectionalRumble( "in_joystickDirectionalRumble", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "a hit rumbles more on the side it came from" );

/*
==============
Sys_PulseJoystickRumbleDirected

A hit felt from its side. The heavy low-frequency motor sits in the left grip
and the light high-frequency one in the right, so a hit from the left keeps
the left motor whole and quiets the right, and the other way round. A hit
from straight ahead or behind, or with in_joystickDirectionalRumble off,
rumbles both alike, as every hit did before.
==============
*/
void Sys_PulseJoystickRumbleDirected( float strength, float direction, int durationMsec ) {
	float low = strength;
	float high = strength;
	if ( in_joystickDirectionalRumble.GetBool() && direction == direction ) {
		const float side = direction < -1.0f ? -1.0f : ( direction > 1.0f ? 1.0f : direction );
		if ( side > 0.0f ) {
			low *= 1.0f - 0.75f * side;
		} else {
			high *= 1.0f + 0.75f * side;
		}
	}
	Sys_PulseJoystickRumble( low, high, durationMsec );
}

/*
==============
Sys_PulseJoystickTriggerRumble

A shot in the trigger that fired it: whichever trigger is bound to attack, so
a player who fires with the left trigger feels it there. Controllers without
trigger motors, and in_joystickTriggerRumble off, do nothing.
==============
*/
void Sys_PulseJoystickTriggerRumble( float strength, int durationMsec ) {
	const float level = Sys_ClampRumble( strength );
	if ( durationMsec <= 0 || level <= SYS_RUMBLE_STOP_THRESHOLD ) {
		return;
	}
	const bool left = idStr::Icmp( idKeyInput::GetBinding( K_JOY16 ), "_attack" ) == 0;
	const bool right = idStr::Icmp( idKeyInput::GetBinding( K_JOY15 ), "_attack" ) == 0;
	if ( !left && !right ) {
		return;
	}
	const bool sent = Sys_SetJoystickTriggerRumble( left ? level : 0.0f, right ? level : 0.0f, durationMsec < 2000 ? durationMsec : 2000 );
	if ( in_joystickRumbleLog.GetBool() ) {
		common->Printf( "rumble: trigger %s%s %.2f for %d ms, %s\n", left ? "left" : "", right ? "right" : "", level, durationMsec,
			sent ? "sent" : "not sent" );
	}
}

// engine-owned window state; written by the platform window layer, read by
// GUI/session code (see sys_public.h)
engineWindowState_t	engineWindowState;

static idStr Sys_FormatDecimalUnit( double value, const char *unit, int decimals ) {
	char buffer[64];
	idStr result;

	idStr::snPrintf( buffer, sizeof( buffer ), "%.*f", decimals, value );
	result = buffer;
	if ( result.Find( '.' ) >= 0 ) {
		result.StripTrailing( "0" );
		result.StripTrailing( "." );
	}
	result += " ";
	result += unit;

	return result;
}

static void Sys_NormalizeWhitespace( const char *text, idStr &normalized ) {
	bool pendingSpace = false;

	normalized.Clear();

	if ( text == NULL ) {
		return;
	}

	for ( int i = 0; text[ i ] != '\0'; ++i ) {
		const char c = text[ i ];
		const bool isWhitespace = c == ' ' || c == '\t' || c == '\r' || c == '\n';

		if ( isWhitespace ) {
			if ( normalized.Length() > 0 ) {
				pendingSpace = true;
			}
			continue;
		}

		if ( pendingSpace ) {
			normalized.Append( ' ' );
			pendingSpace = false;
		}

		normalized.Append( c );
	}
}

static void Sys_AppendProcessorSummaryDetail( idStr &details, bool &hasDetail, const char *detail ) {
	if ( detail == NULL || detail[ 0 ] == '\0' ) {
		return;
	}

	if ( hasDetail ) {
		details += ", ";
	}

	details += detail;
	hasDetail = true;
}

idStr Sys_FormatFrequency( double hertz ) {
	if ( hertz >= 1000000000.0 ) {
		return Sys_FormatDecimalUnit( hertz / 1000000000.0, "GHz", 2 );
	}
	if ( hertz >= 1000000.0 ) {
		return Sys_FormatDecimalUnit( hertz / 1000000.0, "MHz", 2 );
	}
	if ( hertz >= 1000.0 ) {
		return Sys_FormatDecimalUnit( hertz / 1000.0, "kHz", 2 );
	}
	return Sys_FormatDecimalUnit( hertz, "Hz", 0 );
}

idStr Sys_FormatMemoryMB( int megabytes ) {
	if ( megabytes >= ( 1024 * 1024 ) ) {
		return Sys_FormatDecimalUnit( static_cast<double>( megabytes ) / ( 1024.0 * 1024.0 ), "TB", 2 );
	}
	if ( megabytes >= 1024 ) {
		return Sys_FormatDecimalUnit( static_cast<double>( megabytes ) / 1024.0, "GB", 2 );
	}
	return Sys_FormatDecimalUnit( static_cast<double>( megabytes ), "MB", 0 );
}

idStr Sys_FormatProcessorSummary( const char *modelName, const char *architecture, int physicalCores, int logicalCores, int packages, double hertz ) {
	idStr normalizedName;
	idStr details;
	idStr summary;
	bool hasDetail = false;
	const char *effectiveArchitecture = architecture;

	if ( effectiveArchitecture == NULL || effectiveArchitecture[ 0 ] == '\0' ) {
		effectiveArchitecture = CPUSTRING;
	}

	Sys_NormalizeWhitespace( modelName, normalizedName );

	if ( normalizedName.Length() > 0 ) {
		summary = normalizedName;
		if ( effectiveArchitecture != NULL && effectiveArchitecture[ 0 ] != '\0' ) {
			Sys_AppendProcessorSummaryDetail( details, hasDetail, effectiveArchitecture );
		}
	} else if ( effectiveArchitecture != NULL && effectiveArchitecture[ 0 ] != '\0' ) {
		summary = effectiveArchitecture;
		summary += " CPU";
	} else {
		summary = "generic CPU";
	}

	if ( packages > 1 ) {
		Sys_AppendProcessorSummaryDetail( details, hasDetail, va( "%d package%s", packages, packages == 1 ? "" : "s" ) );
	}

	if ( physicalCores > 0 ) {
		if ( logicalCores > physicalCores ) {
			Sys_AppendProcessorSummaryDetail(
				details,
				hasDetail,
				va( "%d core%s / %d thread%s",
					physicalCores,
					physicalCores == 1 ? "" : "s",
					logicalCores,
					logicalCores == 1 ? "" : "s" ) );
		} else {
			Sys_AppendProcessorSummaryDetail(
				details,
				hasDetail,
				va( "%d core%s", physicalCores, physicalCores == 1 ? "" : "s" ) );
		}
	} else if ( logicalCores > 0 ) {
		Sys_AppendProcessorSummaryDetail(
			details,
			hasDetail,
			va( "%d thread%s", logicalCores, logicalCores == 1 ? "" : "s" ) );
	}

	if ( hasDetail ) {
		summary += " (";
		summary += details;
		summary += ")";
	}

	if ( hertz > 0.0 ) {
		summary += " @ ";
		summary += Sys_FormatFrequency( hertz );
	}

	return summary;
}

void idSysLocal::DebugPrintf( const char *fmt, ... ) {
	va_list argptr;

	va_start( argptr, fmt );
	Sys_DebugVPrintf( fmt, argptr );
	va_end( argptr );
}

void idSysLocal::DebugVPrintf( const char *fmt, va_list arg ) {
	Sys_DebugVPrintf( fmt, arg );
}

double idSysLocal::GetClockTicks( void ) {
	return Sys_GetClockTicks();
}

double idSysLocal::ClockTicksPerSecond( void ) {
	return Sys_ClockTicksPerSecond();
}

cpuid_t idSysLocal::GetProcessorId( void ) {
	return Sys_GetProcessorId();
}

const char *idSysLocal::GetProcessorString( void ) {
	return Sys_GetProcessorString();
}

const char *idSysLocal::FPU_GetState( void ) {
	return Sys_FPU_GetState();
}

bool idSysLocal::FPU_StackIsEmpty( void ) {
	return Sys_FPU_StackIsEmpty();
}

void idSysLocal::FPU_SetFTZ( bool enable ) {
	Sys_FPU_SetFTZ( enable );
}

void idSysLocal::FPU_SetDAZ( bool enable ) {
	Sys_FPU_SetDAZ( enable );
}

bool idSysLocal::LockMemory( void *ptr, int bytes ) {
	return Sys_LockMemory( ptr, bytes );
}

bool idSysLocal::UnlockMemory( void *ptr, int bytes ) {
	return Sys_UnlockMemory( ptr, bytes );
}

void idSysLocal::GetCallStack( address_t *callStack, const int callStackSize ) {
	//Sys_GetCallStack( callStack, callStackSize );
}

const char * idSysLocal::GetCallStackStr( const address_t *callStack, const int callStackSize ) {
	//return Sys_GetCallStackStr( callStack, callStackSize );
	return "";
}

const char * idSysLocal::GetCallStackCurStr( int depth ) {
	//return Sys_GetCallStackCurStr( depth );
	return "";
}

void idSysLocal::ShutdownSymbols( void ) {
	Sys_ShutdownSymbols();
}

intptr_t idSysLocal::DLL_Load( const char *dllName ) {
	return Sys_DLL_Load( dllName );
}

void *idSysLocal::DLL_GetProcAddress(intptr_t dllHandle, const char *procName ) {
	return Sys_DLL_GetProcAddress( dllHandle, procName );
}

void idSysLocal::DLL_Unload(intptr_t dllHandle ) {
	Sys_DLL_Unload( dllHandle );
}

void idSysLocal::DLL_GetFileName( const char *baseName, char *dllName, int maxLength ) {
	// arch-tagged openQ4 module names (game_x64, renderer-vk_x64) are used
	// as-is; everything else keeps the legacy CPUSTRING suffix behavior
	const bool explicitGameModuleName =
		idStr::Icmpn( baseName, "game_", 5 ) == 0 ||
		idStr::Icmpn( baseName, "renderer-", 9 ) == 0;
#ifdef _WIN32
	if ( explicitGameModuleName ) {
		idStr::snPrintf( dllName, maxLength, "%s.dll", baseName );
	} else {
		idStr::snPrintf( dllName, maxLength, "%s" CPUSTRING ".dll", baseName );
	}
#elif defined( __ANDROID__ )
	// Only lib*.so is packaged into the APK and extracted to the native library
	// directory, so every module carries the prefix there.
	if ( explicitGameModuleName ) {
		idStr::snPrintf( dllName, maxLength, "lib%s.so", baseName );
	} else {
		idStr::snPrintf( dllName, maxLength, "lib%s" CPUSTRING ".so", baseName );
	}
#elif defined( __linux__ )
	if ( explicitGameModuleName ) {
		idStr::snPrintf( dllName, maxLength, "%s.so", baseName );
	} else {
		idStr::snPrintf( dllName, maxLength, "%s" CPUSTRING ".so", baseName );
	}
#elif defined( MACOS_X )
	idStr::snPrintf( dllName, maxLength, "%s.dylib", baseName );
#else
#error OS define is required
#endif
}

sysEvent_t idSysLocal::GenerateMouseButtonEvent( int button, bool down ) {
	sysEvent_t ev;
	ev.evType = SE_KEY;
	ev.evValue = K_MOUSE1 + button - 1;
	ev.evValue2 = down;
	ev.evPtrLength = 0;
	ev.evPtr = NULL;
	return ev;
}

sysEvent_t idSysLocal::GenerateMouseMoveEvent( int deltax, int deltay ) {
	sysEvent_t ev;
	ev.evType = SE_MOUSE;
	ev.evValue = deltax;
	ev.evValue2 = deltay;
	ev.evPtrLength = 0;
	ev.evPtr = NULL;
	return ev;
}

void idSysLocal::FPU_EnableExceptions( int exceptions ) {
	Sys_FPU_EnableExceptions( exceptions );
}

/*
=================
Sys_TimeStampToStr
=================
*/
const char *Sys_TimeStampToStr( ID_TIME_T timeStamp ) {
	static char timeString[MAX_STRING_CHARS];
	timeString[0] = '\0';

	tm*	time = localtime( &timeStamp );
	idStr out;
	
	idStr lang = cvarSystem->GetCVarString( "sys_lang" );
	if ( lang.Icmp( "english" ) == 0 ) {
		// english gets "month/day/year  hour:min" + "am" or "pm"
		out = va( "%02d", time->tm_mon + 1 );
		out += "/";
		out += va( "%02d", time->tm_mday );
		out += "/";
		out += va( "%d", time->tm_year + 1900 );
		out += "\t";
		if ( time->tm_hour > 12 ) {
			out += va( "%02d", time->tm_hour - 12 );
		} else if ( time->tm_hour == 0 ) {
				out += "12";
		} else {
			out += va( "%02d", time->tm_hour );
		}
		out += ":";
		out +=va( "%02d", time->tm_min );
		if ( time->tm_hour >= 12 ) {
			out += "pm";
		} else {
			out += "am";
		}
	} else {
		// europeans get "day/month/year  24hour:min"
		out = va( "%02d", time->tm_mday );
		out += "/";
		out += va( "%02d", time->tm_mon + 1 );
		out += "/";
		out += va( "%d", time->tm_year + 1900 );
		out += "\t";
		out += va( "%02d", time->tm_hour );
		out += ":";
		out += va( "%02d", time->tm_min );
	}
	idStr::Copynz( timeString, out, sizeof( timeString ) );

	return timeString;
}

