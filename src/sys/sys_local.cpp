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

const char * sysLanguageNames[] = {
	"english", "spanish", "italian", "german", "french", "russian", 
	"polish", "korean", "japanese", "chinese", NULL
};

idCVar sys_lang( "sys_lang", "english", CVAR_SYSTEM | CVAR_ARCHIVE,  "", sysLanguageNames, idCmdSystem::ArgCompletion_String<sysLanguageNames> );

idSysLocal			sysLocal;
idSys *				sys = &sysLocal;

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
	// arch-tagged openQ4 module names (game-sp_x64, renderer-vk_x64) are used
	// as-is; everything else keeps the legacy CPUSTRING suffix behavior
	const bool explicitGameModuleName =
		idStr::Icmpn( baseName, "game_", 5 ) == 0 ||
		idStr::Icmpn( baseName, "game-", 5 ) == 0 ||
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

