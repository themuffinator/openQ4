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




#include "../../framework/NativeInputDispatch.h"
#include <intrin.h>
#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <direct.h>
#include <io.h>
#include <conio.h>
#include <mapi.h>
#include <ShellAPI.h>
#include <ShlObj.h>

#ifndef __MRC__
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "../sys_local.h"
#include "../URLPolicy.h"
#include "win_local.h"
#include "win_crash.h"
#include "rc/CreateResourceIDs.h"
#include "../../renderer/tr_local.h"

#ifdef USE_SDL3
bool Sys_SDL_PumpEvents(void);
#endif

#if !defined(ID_DEDICATED)
// Ask hybrid-GPU Windows drivers to prefer the high-performance adapter.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0x00000001;
}
#endif

idCVar Win32Vars_t::sys_arch("sys_arch", "", CVAR_SYSTEM | CVAR_INIT, "");
idCVar Win32Vars_t::sys_cpustring("sys_cpustring", "detect", CVAR_SYSTEM | CVAR_INIT, "");
idCVar Win32Vars_t::in_mouse("in_mouse", "1", CVAR_SYSTEM | CVAR_BOOL, "enable mouse input");
idCVar Win32Vars_t::win_allowAltTab("win_allowAltTab", "0", CVAR_SYSTEM | CVAR_BOOL, "allow Alt-Tab when fullscreen");
idCVar Win32Vars_t::win_notaskkeys("win_notaskkeys", "0", CVAR_SYSTEM | CVAR_INTEGER, "disable windows task keys");
idCVar Win32Vars_t::win_username("win_username", "", CVAR_SYSTEM | CVAR_INIT, "windows user name");
idCVar Win32Vars_t::win_xpos("win_xpos", "3", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "horizontal position of window");
idCVar Win32Vars_t::win_ypos("win_ypos", "22", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "vertical position of window");
idCVar Win32Vars_t::win_outputDebugString("win_outputDebugString", "1", CVAR_SYSTEM | CVAR_BOOL, "");
idCVar Win32Vars_t::win_outputEditString("win_outputEditString", "1", CVAR_SYSTEM | CVAR_BOOL, "");
idCVar Win32Vars_t::win_viewlog("win_viewlog", "0", CVAR_SYSTEM | CVAR_INTEGER, "");
idCVar Win32Vars_t::win_timerUpdate("win_timerUpdate", "0", CVAR_SYSTEM | CVAR_BOOL, "allows the game to be updated while dragging the window");
idCVar Win32Vars_t::win_allowMultipleInstances("win_allowMultipleInstances", "0", CVAR_SYSTEM | CVAR_BOOL, "allow multiple instances running concurrently");
idCVar Win32Vars_t::win_printScreenToSystemTool("win_printScreenToSystemTool", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "yield PrintScreen to the Windows snipping UI instead of routing it into the engine");

Win32Vars_t	win32;

static char		sys_cmdline[MAX_STRING_CHARS];

static bool Sys_ReadProcessorRegistryString( const char *valueName, idStr &value ) {
	HKEY hKey;
	char buffer[ 256 ];
	DWORD type = 0;
	DWORD buflen = sizeof( buffer );
	LSTATUS ret;

	value.Clear();

	if ( RegOpenKeyExA( HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey ) != ERROR_SUCCESS ) {
		return false;
	}

	ret = RegQueryValueExA( hKey, valueName, NULL, &type, reinterpret_cast<LPBYTE>( buffer ), &buflen );
	RegCloseKey( hKey );

	if ( ret != ERROR_SUCCESS || ( type != REG_SZ && type != REG_EXPAND_SZ ) ) {
		return false;
	}

	buffer[ sizeof( buffer ) - 1 ] = '\0';
	value = buffer;
	value.StripTrailingWhitespace();

	return value.Length() > 0;
}

double Sys_GetApproximateProcessorFrequencyHz( void ) {
	HKEY hKey;
	DWORD procSpeedMHz = 0;
	DWORD buflen = sizeof( procSpeedMHz );
	LSTATUS ret;

	if ( RegOpenKeyEx( HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey ) != ERROR_SUCCESS ) {
		return 0.0;
	}

	ret = RegQueryValueEx( hKey, "~MHz", NULL, NULL, reinterpret_cast<LPBYTE>( &procSpeedMHz ), &buflen );
	if ( ret != ERROR_SUCCESS ) {
		buflen = sizeof( procSpeedMHz );
		ret = RegQueryValueEx( hKey, "~Mhz", NULL, NULL, reinterpret_cast<LPBYTE>( &procSpeedMHz ), &buflen );
	}
	if ( ret != ERROR_SUCCESS ) {
		buflen = sizeof( procSpeedMHz );
		ret = RegQueryValueEx( hKey, "~mhz", NULL, NULL, reinterpret_cast<LPBYTE>( &procSpeedMHz ), &buflen );
	}

	RegCloseKey( hKey );

	if ( ret != ERROR_SUCCESS || procSpeedMHz == 0 ) {
		return 0.0;
	}

	return static_cast<double>( procSpeedMHz ) * 1000000.0;
}

static const char *Sys_GetFallbackProcessorName( const cpuid_t cpuid ) {
	if ( cpuid & CPUID_AMD ) {
		return "AMD processor";
	}
	if ( cpuid & CPUID_INTEL ) {
		return "Intel processor";
	}
	if ( cpuid & CPUID_UNSUPPORTED ) {
		return "unsupported CPU";
	}
	return "generic CPU";
}

static const DWORD OPENQ4_MIN_WINDOWS_MAJOR_VERSION = 6;
static const DWORD OPENQ4_MIN_WINDOWS_MINOR_VERSION = 1;
static const DWORD OPENQ4_VALIDATED_WINDOWS_MAJOR_VERSION = 10;
static const DWORD OPENQ4_VALIDATED_WINDOWS_MINOR_VERSION = 0;

static void Sys_GetProcessorTopology( int &logicalCores, int &physicalCores, int &packages ) {
	SYSTEM_INFO systemInfo;

	logicalCores = 0;
	physicalCores = 0;
	packages = 0;

	Sys_CPUCount( logicalCores, physicalCores, packages );

	if ( logicalCores <= 0 ) {
		GetNativeSystemInfo( &systemInfo );
		logicalCores = static_cast<int>( systemInfo.dwNumberOfProcessors );
	}

	if ( physicalCores <= 0 ) {
		physicalCores = logicalCores;
	}

	if ( packages <= 0 && logicalCores > 0 ) {
		packages = 1;
	}
}

static bool Sys_QueryWindowsVersion( OSVERSIONINFOEXW &version ) {
	typedef LONG ( WINAPI *RtlGetVersionFn )( OSVERSIONINFOW * );

	HMODULE ntdllModule;
	RtlGetVersionFn rtlGetVersion;

	memset( &version, 0, sizeof( version ) );
	version.dwOSVersionInfoSize = sizeof( version );

	ntdllModule = GetModuleHandleA( "ntdll.dll" );
	if ( ntdllModule == NULL ) {
		return false;
	}

	rtlGetVersion = reinterpret_cast<RtlGetVersionFn>( GetProcAddress( ntdllModule, "RtlGetVersion" ) );
	if ( rtlGetVersion == NULL ) {
		return false;
	}

	return rtlGetVersion( reinterpret_cast<OSVERSIONINFOW *>( &version ) ) == 0;
}

static bool Sys_IsWindowsVersionOrGreater( const OSVERSIONINFOEXW &version, const DWORD major, const DWORD minor ) {
	if ( version.dwMajorVersion != major ) {
		return version.dwMajorVersion > major;
	}

	return version.dwMinorVersion >= minor;
}

static idStr Sys_FormatWindowsVersion( const OSVERSIONINFOEXW &version ) {
	idStr versionName;

	if ( version.dwMajorVersion > 10 || ( version.dwMajorVersion == 10 && version.dwBuildNumber >= 22000 ) ) {
		versionName = "Windows 11";
	} else if ( version.dwMajorVersion == 10 ) {
		versionName = "Windows 10";
	} else if ( version.dwMajorVersion == 6 && version.dwMinorVersion == 3 ) {
		versionName = "Windows 8.1";
	} else if ( version.dwMajorVersion == 6 && version.dwMinorVersion == 2 ) {
		versionName = "Windows 8";
	} else if ( version.dwMajorVersion == 6 && version.dwMinorVersion == 1 ) {
		versionName = "Windows 7";
	} else if ( version.dwMajorVersion == 6 && version.dwMinorVersion == 0 ) {
		versionName = "Windows Vista";
	} else {
		versionName = va( "Windows %lu.%lu", version.dwMajorVersion, version.dwMinorVersion );
	}

	versionName += va( " (build %lu)", version.dwBuildNumber );
	return versionName;
}

/*
========================
Sys_SetProcessDpiAwarenessForEarlyWindows

SDL3 selects a high-DPI video mode during video initialization. The startup
splash is a plain Win32 window created before that, so set process DPI
awareness before any HWND exists to keep Windows from rescaling/repositioning
the splash when SDL starts.
========================
*/
static void Sys_SetProcessDpiAwarenessForEarlyWindows( void ) {
#ifdef USE_SDL3
	HMODULE user32 = GetModuleHandleA( "user32.dll" );
	if ( user32 != NULL ) {
		typedef BOOL ( WINAPI *SetProcessDpiAwarenessContextFn )( HANDLE );
		SetProcessDpiAwarenessContextFn setProcessDpiAwarenessContext =
			reinterpret_cast<SetProcessDpiAwarenessContextFn>( GetProcAddress( user32, "SetProcessDpiAwarenessContext" ) );
		if ( setProcessDpiAwarenessContext != NULL ) {
			static const HANDLE PER_MONITOR_AWARE_V2 = reinterpret_cast<HANDLE>( static_cast<INT_PTR>( -4 ) );
			static const HANDLE PER_MONITOR_AWARE = reinterpret_cast<HANDLE>( static_cast<INT_PTR>( -3 ) );
			if ( setProcessDpiAwarenessContext( PER_MONITOR_AWARE_V2 ) ||
				 setProcessDpiAwarenessContext( PER_MONITOR_AWARE ) ||
				 GetLastError() == ERROR_ACCESS_DENIED ) {
				return;
			}
		}
	}

	HMODULE shcore = LoadLibraryA( "shcore.dll" );
	if ( shcore != NULL ) {
		typedef HRESULT ( WINAPI *SetProcessDpiAwarenessFn )( int );
		SetProcessDpiAwarenessFn setProcessDpiAwareness =
			reinterpret_cast<SetProcessDpiAwarenessFn>( GetProcAddress( shcore, "SetProcessDpiAwareness" ) );
		if ( setProcessDpiAwareness != NULL ) {
			static const int PROCESS_PER_MONITOR_DPI_AWARE = 2;
			static const int PROCESS_SYSTEM_DPI_AWARE = 1;
			const HRESULT perMonitorResult = setProcessDpiAwareness( PROCESS_PER_MONITOR_DPI_AWARE );
			if ( perMonitorResult == S_OK || perMonitorResult == E_ACCESSDENIED ||
				 setProcessDpiAwareness( PROCESS_SYSTEM_DPI_AWARE ) == S_OK ) {
				FreeLibrary( shcore );
				return;
			}
		}
		FreeLibrary( shcore );
	}

	if ( user32 != NULL ) {
		typedef BOOL ( WINAPI *SetProcessDPIAwareFn )( void );
		SetProcessDPIAwareFn setProcessDPIAware =
			reinterpret_cast<SetProcessDPIAwareFn>( GetProcAddress( user32, "SetProcessDPIAware" ) );
		if ( setProcessDPIAware != NULL ) {
			(void)setProcessDPIAware();
		}
	}
#endif
}

bool Sys_HandlePrintScreenHotkey( bool pressed ) {
	if ( !win32.win_printScreenToSystemTool.GetBool() ) {
		return false;
	}

	if ( !pressed ) {
		return true;
	}

	// Give the Windows snipping UI a short window to take foreground before we
	// consider recapturing the mouse again.
	win32.printScreenFocusReleaseUntil = GetTickCount() + 1500u;
	win32.mouseReleased = true;
	IN_DeactivateMouse();
	idKeyInput::ClearStates();

	if ( win32.hWnd != NULL ) {
		ReleaseCapture();
		SendMessage( win32.hWnd, WM_CANCELMODE, 0, 0 );
	}

	return true;
}

static int Sys_openQ4ProtocolHexValue( const char c ) {
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	if ( c >= 'a' && c <= 'f' ) {
		return 10 + ( c - 'a' );
	}
	if ( c >= 'A' && c <= 'F' ) {
		return 10 + ( c - 'A' );
	}
	return -1;
}

static void Sys_DecodeopenQ4ProtocolComponent( const idStr &source, idStr &decoded ) {
	decoded.Clear();

	for ( int i = 0; i < source.Length(); ++i ) {
		const char c = source[ i ];
		if ( c == '%' && ( i + 2 ) < source.Length() ) {
			const int hi = Sys_openQ4ProtocolHexValue( source[ i + 1 ] );
			const int lo = Sys_openQ4ProtocolHexValue( source[ i + 2 ] );
			if ( hi >= 0 && lo >= 0 ) {
				decoded.Append( static_cast<char>( ( hi << 4 ) | lo ) );
				i += 2;
				continue;
			}
		}
		decoded.Append( c );
	}
}

static bool Sys_IsValidopenQ4ConnectTarget( const idStr &target ) {
	if ( target.Length() <= 0 || target.Length() > 255 ) {
		return false;
	}

	for ( int i = 0; i < target.Length(); ++i ) {
		const char c = target[ i ];
		const bool isAlphaNumeric =
			( c >= 'a' && c <= 'z' ) ||
			( c >= 'A' && c <= 'Z' ) ||
			( c >= '0' && c <= '9' );
		if ( isAlphaNumeric ) {
			continue;
		}

		switch ( c ) {
		case '.':
		case ':':
		case '-':
		case '_':
		case '[':
		case ']':
			break;
		default:
			return false;
		}
	}

	return true;
}

static bool Sys_TryReadopenQ4ProtocolQueryValue( const idStr &query, const char *key, idStr &value ) {
	int start = 0;

	value.Clear();

	while ( start <= query.Length() ) {
		const int end = query.Find( '&', start );
		const int pairLength = ( end >= 0 ) ? end - start : query.Length() - start;
		if ( pairLength > 0 ) {
			const idStr pair = query.Mid( start, pairLength );
			const int equalsPos = pair.Find( '=' );
			if ( equalsPos > 0 ) {
				idStr pairKey = pair.Left( equalsPos );
				if ( pairKey.Icmp( key ) == 0 ) {
					value = pair.Mid( equalsPos + 1, pair.Length() - equalsPos - 1 );
					return true;
				}
			}
		}

		if ( end < 0 ) {
			break;
		}
		start = end + 1;
	}

	return false;
}

static bool Sys_TryTranslateopenQ4ProtocolCommandLine( const char *rawCmdLine, idStr &translatedCmdLine ) {
	idCmdArgs args;
	idStr uriPayload;
	idStr path;
	idStr query;
	idStr encodedTarget;
	idStr decodedTarget;
	idStr verb;

	translatedCmdLine.Clear();

	if ( rawCmdLine == NULL || rawCmdLine[ 0 ] == '\0' ) {
		return false;
	}

	args.TokenizeString( rawCmdLine, true );
	if ( args.Argc() <= 0 ) {
		return false;
	}

	if ( idStr::Icmpn( args.Argv( 0 ), "openq4://", 9 ) != 0 ) {
		return false;
	}

	uriPayload = args.Argv( 0 );
	uriPayload = uriPayload.Mid( 9, uriPayload.Length() - 9 );
	uriPayload.StripLeading( '/' );

	if ( uriPayload.Length() == 0 ) {
		return true;
	}

	const int queryPos = uriPayload.Find( '?' );
	if ( queryPos >= 0 ) {
		path = uriPayload.Left( queryPos );
		query = uriPayload.Mid( queryPos + 1, uriPayload.Length() - queryPos - 1 );
	} else {
		path = uriPayload;
		query.Clear();
	}

	const int slashPos = path.Find( '/' );
	if ( slashPos >= 0 ) {
		verb = path.Left( slashPos );
		encodedTarget = path.Mid( slashPos + 1, path.Length() - slashPos - 1 );
	} else {
		verb = path;
	}

	if ( encodedTarget.Length() == 0 ) {
		if ( !Sys_TryReadopenQ4ProtocolQueryValue( query, "server", encodedTarget ) ) {
			if ( !Sys_TryReadopenQ4ProtocolQueryValue( query, "target", encodedTarget ) ) {
				Sys_TryReadopenQ4ProtocolQueryValue( query, "address", encodedTarget );
			}
		}
	}

	if ( slashPos < 0 && query.Length() == 0 ) {
		encodedTarget = path;
		verb = "connect";
	}

	if ( verb.Icmp( "connect" ) != 0 &&
		verb.Icmp( "join" ) != 0 &&
		verb.Icmp( "server" ) != 0 ) {
		return true;
	}

	Sys_DecodeopenQ4ProtocolComponent( encodedTarget, decodedTarget );
	if ( !Sys_IsValidopenQ4ConnectTarget( decodedTarget ) ) {
		return true;
	}

	translatedCmdLine = "+connect \"";
	translatedCmdLine.Append( decodedTarget );
	translatedCmdLine.Append( '"' );
	return true;
}

static bool Sys_ConvertWideToUtf8( const wchar_t *wideText, idStr &utf8Text ) {
	int requiredBytes;
	char *utf8Buffer;

	utf8Text.Clear();

	if ( wideText == NULL ) {
		return false;
	}

	requiredBytes = WideCharToMultiByte( CP_UTF8, 0, wideText, -1, NULL, 0, NULL, NULL );
	if ( requiredBytes <= 0 ) {
		return false;
	}

	utf8Buffer = new char[ requiredBytes ];
	if ( WideCharToMultiByte( CP_UTF8, 0, wideText, -1, utf8Buffer, requiredBytes, NULL, NULL ) <= 0 ) {
		delete[] utf8Buffer;
		return false;
	}

	utf8Text = utf8Buffer;
	delete[] utf8Buffer;
	return true;
}

static bool Sys_ConvertWideToWindowsPath( const wchar_t *widePath, idStr &path ) {
	int requiredBytes;
	char *pathBuffer;

	path.Clear();
	if ( widePath == NULL || widePath[0] == L'\0' ) {
		return false;
	}

	// The filesystem still reaches the Windows CRT through narrow path APIs.
	// Keep the Known Folder result in that API's active code page until those
	// callers are migrated together to wide-character or UTF-8-aware I/O.
	requiredBytes = WideCharToMultiByte( CP_ACP, 0, widePath, -1, NULL, 0, NULL, NULL );
	if ( requiredBytes <= 0 ) {
		return false;
	}

	pathBuffer = new char[ requiredBytes ];
	if ( WideCharToMultiByte( CP_ACP, 0, widePath, -1, pathBuffer, requiredBytes, NULL, NULL ) <= 0 ) {
		delete[] pathBuffer;
		return false;
	}

	path = pathBuffer;
	delete[] pathBuffer;
	path.SlashesToBackSlashes();
	return !path.IsEmpty();
}

static bool Sys_GetKnownFolderPath( REFKNOWNFOLDERID folderId, idStr &path ) {
	PWSTR widePath = NULL;
	const HRESULT result = SHGetKnownFolderPath( folderId, KF_FLAG_DEFAULT, NULL, &widePath );
	if ( FAILED( result ) || widePath == NULL ) {
		if ( widePath != NULL ) {
			CoTaskMemFree( widePath );
		}
		path.Clear();
		return false;
	}

	const bool converted = Sys_ConvertWideToWindowsPath( widePath, path );
	CoTaskMemFree( widePath );
	return converted;
}

static bool Sys_IsAbsoluteWindowsPath( const idStr &path ) {
	if ( path.Length() < 3 ) {
		return false;
	}

	const char *text = path.c_str();
	const bool drivePath =
		( ( text[0] >= 'A' && text[0] <= 'Z' ) || ( text[0] >= 'a' && text[0] <= 'z' ) ) &&
		text[1] == ':' && text[2] == '\\';
	const bool uncPath = text[0] == '\\' && text[1] == '\\' && text[2] != '\0';
	return drivePath || uncPath;
}

static bool Sys_BuildWindowsProductSavePath( const char *root, idStr &savePath ) {
	if ( root == NULL || root[0] == '\0' ) {
		savePath.Clear();
		return false;
	}

	savePath = root;
	savePath.SlashesToBackSlashes();
	if ( !Sys_IsAbsoluteWindowsPath( savePath ) ) {
		savePath.Clear();
		return false;
	}
	savePath.StripTrailing( '\\' );
	savePath.AppendPath( "openQ4" );
	savePath.SlashesToBackSlashes();
	return true;
}

static bool Sys_BuildWindowsUtf8Argv( idList<idStr> &utf8Args, idList<const char *> &argvArgs ) {
	int wideArgc = 0;
	LPWSTR *wideArgv;

	utf8Args.Clear();
	argvArgs.Clear();

	wideArgv = CommandLineToArgvW( GetCommandLineW(), &wideArgc );
	if ( wideArgv == NULL ) {
		return false;
	}

	for ( int i = 1; i < wideArgc; ++i ) {
		idStr utf8Arg;

		if ( !Sys_ConvertWideToUtf8( wideArgv[ i ], utf8Arg ) ) {
			LocalFree( wideArgv );
			utf8Args.Clear();
			argvArgs.Clear();
			return false;
		}

		utf8Args.Append( utf8Arg );
	}

	LocalFree( wideArgv );

	for ( int i = 0; i < utf8Args.Num(); ++i ) {
		argvArgs.Append( utf8Args[ i ].c_str() );
	}

	return true;
}

int g_thread_count = 0;

static sysMemoryStats_t exeLaunchMemoryStats;
// not a hard limit, just what we keep track of for debugging
xthreadInfo* g_threads[MAX_THREADS];


static	xthreadInfo	threadInfo;
static	HANDLE		hTimer;

/*
================
Sys_GetExeLaunchMemoryStatus
================
*/
void Sys_GetExeLaunchMemoryStatus(sysMemoryStats_t& stats) {
	stats = exeLaunchMemoryStats;
}

/*
==================
Sys_Createthread
==================
*/
void Sys_CreateThread(xthread_t function, void* parms, xthreadPriority priority, xthreadInfo& info, const char* name, xthreadInfo* threads[MAX_THREADS], int* thread_count) {
	DWORD threadId = 0;
	HANDLE temp = CreateThread(NULL,	// LPSECURITY_ATTRIBUTES lpsa,
		0,		// DWORD cbStack,
		(LPTHREAD_START_ROUTINE)function,	// LPTHREAD_START_ROUTINE lpStartAddr,
		parms,	// LPVOID lpvThreadParm,
		0,		//   DWORD fdwCreate,
		&threadId);
	info.threadHandle = reinterpret_cast<uintptr_t>(temp);
	info.threadId = static_cast<uint32_t>( threadId );
	info.stopRequested = false;
	if (priority == THREAD_HIGHEST) {
		SetThreadPriority(reinterpret_cast<HANDLE>(info.threadHandle), THREAD_PRIORITY_HIGHEST);		//  we better sleep enough to do this
	}
	else if (priority == THREAD_ABOVE_NORMAL) {
		SetThreadPriority(reinterpret_cast<HANDLE>(info.threadHandle), THREAD_PRIORITY_ABOVE_NORMAL);
	}
	info.name = name;
	// Publishing the slot and bumping the count are two separate stores. The
	// POSIX backend already serializes the registry against its readers, and
	// ARM64's weak memory model makes the unsynchronized version genuinely
	// reorderable: a worker polling Sys_IsCurrentThreadStopRequested could
	// observe the incremented count before the slot store lands and
	// dereference an uninitialized pointer.
	Sys_EnterCriticalSection();
	if (*thread_count < MAX_THREADS) {
		threads[(*thread_count)++] = &info;
		Sys_LeaveCriticalSection();
	}
	else {
		Sys_LeaveCriticalSection();
		common->DPrintf("WARNING: MAX_THREADS reached\n");
	}
}

/*
==================
Sys_RemoveThreadInfo
==================
*/
static void Sys_RemoveThreadInfo( xthreadInfo& info ) {
	Sys_EnterCriticalSection();
	for( int i = 0 ; i < g_thread_count ; i++ ) {
		if ( &info == g_threads[ i ] ) {
			g_threads[ i ] = NULL;
			int j;
			for( j = i+1 ; j < g_thread_count ; j++ ) {
				g_threads[ j-1 ] = g_threads[ j ];
			}
			g_threads[ j-1 ] = NULL;
			g_thread_count--;
			break;
		}
	}
	Sys_LeaveCriticalSection();
}

/*
==================
Sys_RequestThreadStop
==================
*/
void Sys_RequestThreadStop( xthreadInfo& info ) {
	info.stopRequested = true;
	if ( win32.backgroundDownloadSemaphore ) {
		Sys_TriggerEvent();
	}
}

/*
==================
Sys_IsThreadStopRequested
==================
*/
bool Sys_IsThreadStopRequested( const xthreadInfo& info ) {
	return info.stopRequested;
}

/*
==================
Sys_IsCurrentThreadStopRequested
==================
*/
bool Sys_IsCurrentThreadStopRequested( void ) {
	const DWORD id = GetCurrentThreadId();
	bool stopRequested = false;

	Sys_EnterCriticalSection();
	for (int i = 0; i < g_thread_count; i++) {
		if ( g_threads[i] != NULL && id == g_threads[i]->threadId ) {
			stopRequested = g_threads[i]->stopRequested;
			break;
		}
	}
	Sys_LeaveCriticalSection();
	return stopRequested;
}

/*
==================
Sys_DestroyThread
==================
*/
void Sys_DestroyThread(xthreadInfo& info) {
	Sys_RequestThreadStop( info );
	WaitForSingleObject(reinterpret_cast<HANDLE>(info.threadHandle), INFINITE);
	CloseHandle(reinterpret_cast<HANDLE>(info.threadHandle));
	info.threadHandle = 0;
	info.threadId = 0;
	info.stopRequested = false;
	Sys_RemoveThreadInfo( info );
}

/*
==================
Sys_Sentry
==================
*/
void Sys_Sentry() {
}

/*
==================
Sys_GetThreadName
==================
*/
const char* Sys_GetThreadName(int* index) {
	const DWORD id = GetCurrentThreadId();
	const char* name = "main";
	int foundIndex = -1;

	Sys_EnterCriticalSection();
	for (int i = 0; i < g_thread_count; i++) {
		// Sys_RemoveThreadInfo can leave a NULL slot behind, so this has to be
		// guarded the same way Sys_IsCurrentThreadStopRequested already is.
		if (g_threads[i] != NULL && id == g_threads[i]->threadId) {
			foundIndex = i;
			name = g_threads[i]->name;
			break;
		}
	}
	Sys_LeaveCriticalSection();

	if (index) {
		*index = foundIndex;
	}
	return name;
}


/*
==================
Sys_EnterCriticalSection
==================
*/
void Sys_EnterCriticalSection(int index) {
	assert(index >= 0 && index < MAX_CRITICAL_SECTIONS);
	if (TryEnterCriticalSection(&win32.criticalSections[index]) == 0) {
		EnterCriticalSection(&win32.criticalSections[index]);
		//		Sys_DebugPrintf( "busy lock '%s' in thread '%s'\n", lock->name, Sys_GetThreadName() );
	}
}

/*
==================
Sys_LeaveCriticalSection
==================
*/
void Sys_LeaveCriticalSection(int index) {
	assert(index >= 0 && index < MAX_CRITICAL_SECTIONS);
	LeaveCriticalSection(&win32.criticalSections[index]);
}

/*
==================
Sys_WaitForEvent
==================
*/
void Sys_WaitForEvent(int index) {
	assert(index == 0);
	if (!win32.backgroundDownloadSemaphore) {
		win32.backgroundDownloadSemaphore = CreateEvent(NULL, TRUE, FALSE, NULL);
	}
	WaitForSingleObject(win32.backgroundDownloadSemaphore, INFINITE);
	ResetEvent(win32.backgroundDownloadSemaphore);
}

/*
==================
Sys_TriggerEvent
==================
*/
void Sys_TriggerEvent(int index) {
	assert(index == 0);
	SetEvent(win32.backgroundDownloadSemaphore);
}


/*
================
Sys_IsGameWindowFocused
================
*/
bool Sys_IsGameWindowFocused(void) {
	if (!IsWindowVisible(win32.hWnd))
		return false;

	if (GetForegroundWindow() != win32.hWnd)
		return false;

	return true;
}

#pragma optimize( "", on )

#ifdef DEBUG


static unsigned int debug_total_alloc = 0;
static unsigned int debug_total_alloc_count = 0;
static unsigned int debug_current_alloc = 0;
static unsigned int debug_current_alloc_count = 0;
static unsigned int debug_frame_alloc = 0;
static unsigned int debug_frame_alloc_count = 0;

idCVar sys_showMallocs("sys_showMallocs", "0", CVAR_SYSTEM, "");

// _HOOK_ALLOC, _HOOK_REALLOC, _HOOK_FREE

typedef struct CrtMemBlockHeader
{
	struct _CrtMemBlockHeader* pBlockHeaderNext;	// Pointer to the block allocated just before this one:
	struct _CrtMemBlockHeader* pBlockHeaderPrev;	// Pointer to the block allocated just after this one
	char* szFileName;    // File name
	int nLine;           // Line number
	size_t nDataSize;    // Size of user block
	int nBlockUse;       // Type of block
	long lRequest;       // Allocation number
	byte		gap[4];								// Buffer just before (lower than) the user's memory:
} CrtMemBlockHeader;

#include <crtdbg.h>

/*
==================
Sys_AllocHook

	called for every malloc/new/free/delete
==================
*/
int Sys_AllocHook(int nAllocType, void* pvData, size_t nSize, int nBlockUse, long lRequest, const unsigned char* szFileName, int nLine)
{
	CrtMemBlockHeader* pHead;
	byte* temp;

	if (nBlockUse == _CRT_BLOCK)
	{
		return(TRUE);
	}

	// get a pointer to memory block header
	temp = (byte*)pvData;
	temp -= 32;
	pHead = (CrtMemBlockHeader*)temp;

	switch (nAllocType) {
	case	_HOOK_ALLOC:
		debug_total_alloc += nSize;
		debug_current_alloc += nSize;
		debug_frame_alloc += nSize;
		debug_total_alloc_count++;
		debug_current_alloc_count++;
		debug_frame_alloc_count++;
		break;

	case	_HOOK_FREE:
		assert(pHead->gap[0] == 0xfd && pHead->gap[1] == 0xfd && pHead->gap[2] == 0xfd && pHead->gap[3] == 0xfd);

		debug_current_alloc -= pHead->nDataSize;
		debug_current_alloc_count--;
		debug_total_alloc_count++;
		debug_frame_alloc_count++;
		break;

	case	_HOOK_REALLOC:
		assert(pHead->gap[0] == 0xfd && pHead->gap[1] == 0xfd && pHead->gap[2] == 0xfd && pHead->gap[3] == 0xfd);

		debug_current_alloc -= pHead->nDataSize;
		debug_total_alloc += nSize;
		debug_current_alloc += nSize;
		debug_frame_alloc += nSize;
		debug_total_alloc_count++;
		debug_current_alloc_count--;
		debug_frame_alloc_count++;
		break;
	}
	return(TRUE);
}

/*
==================
Sys_DebugMemory_f
==================
*/
void Sys_DebugMemory_f(void) {
	common->Printf("Total allocation %8dk in %d blocks\n", debug_total_alloc / 1024, debug_total_alloc_count);
	common->Printf("Current allocation %8dk in %d blocks\n", debug_current_alloc / 1024, debug_current_alloc_count);
}

/*
==================
Sys_MemFrame
==================
*/
void Sys_MemFrame(void) {
	if (sys_showMallocs.GetInteger()) {
		common->Printf("Frame: %8dk in %5d blocks\n", debug_frame_alloc / 1024, debug_frame_alloc_count);
	}

	debug_frame_alloc = 0;
	debug_frame_alloc_count = 0;
}

#endif

/*
==================
Sys_FlushCacheMemory

On windows, the vertex buffers are write combined, so they
don't need to be flushed from the cache
==================
*/
void Sys_FlushCacheMemory(void* base, int bytes) {
}

/*
=============
Sys_Error

Show the early console as an error dialog
=============
*/
void Sys_Error(const char* error, ...) {
    // Freeze borrowed diagnostics before retirement can release their owner.
    // Formatting touches no GUI, native window, or message-dispatch boundary.
    va_list argptr;
    char text[4096];
    va_start(argptr, error);
    idStr::vsnPrintf(text, sizeof(text), error ? error : "Unknown error", argptr);
    va_end(argptr);
    if (!NativeInput_FatalRetire()) {
        // An already-fatal unresolved native lease cannot enter the console or
        // dispatch DLL/CRT shutdown callbacks. Preserve bounded raw diagnostics.
        const char* message = text;
        DWORD length = 0, written = 0;
        while (length < 2048 && message[length]) ++length;
        (void)WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, length, &written, NULL);
        (void)TerminateProcess(GetCurrentProcess(), 1);
        __fastfail(FAST_FAIL_FATAL_APP_EXIT);
    }
	MSG        msg;

	Conbuf_AppendText(text);
	Conbuf_AppendText("\n");

	Win_SetErrorText(text);
	Sys_ShowConsole(1, true);

	timeEndPeriod(1);

	Sys_ShutdownInput();

	// Common::Shutdown may already have torn down the renderer path.
	// Avoid forcing GL shutdown when there is no active GL context.
#ifndef OPENQ4_RENDERER_MODULE_ONLY
	if ( renderSystem->IsOpenGLRunning() ) {
		GLimp_Shutdown();
	}
#else
	// module-only build: GLimp lives inside the renderer module; this is a
	// fatal-exit path and the OS reclaims the context with the process
#endif

	// wait for the user to quit
	while (1) {
		if (!GetMessage(&msg, NULL, 0, 0)) {
			common->Quit();
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	Sys_DestroyConsole();

	exit(1);
}

/*
==============
Sys_Quit
==============
*/
void Sys_Quit(void) {
	timeEndPeriod(1);
	Sys_ShutdownInput();
	Sys_DestroyConsole();
	ExitProcess(0);
}


/*
==============
Sys_Printf
==============
*/
#define MAXPRINTMSG 4096
void Sys_Printf(const char* fmt, ...) {
	char		msg[MAXPRINTMSG];

	va_list argptr;
	va_start(argptr, fmt);
	idStr::vsnPrintf(msg, MAXPRINTMSG - 1, fmt, argptr);
	va_end(argptr);
	msg[sizeof(msg) - 1] = '\0';

	if (win32.win_outputDebugString.GetBool()) {
		OutputDebugString(msg);
	}
	if (win32.win_outputEditString.GetBool()) {
		Conbuf_AppendText(msg);
	}
}

/*
==============
Sys_DebugPrintf
==============
*/
#define MAXPRINTMSG 4096
void Sys_DebugPrintf(const char* fmt, ...) {
	char msg[MAXPRINTMSG];

	va_list argptr;
	va_start(argptr, fmt);
	idStr::vsnPrintf(msg, MAXPRINTMSG - 1, fmt, argptr);
	msg[sizeof(msg) - 1] = '\0';
	va_end(argptr);

	OutputDebugString(msg);
}

/*
==============
Sys_DebugVPrintf
==============
*/
void Sys_DebugVPrintf(const char* fmt, va_list arg) {
	char msg[MAXPRINTMSG];

	idStr::vsnPrintf(msg, MAXPRINTMSG - 1, fmt, arg);
	msg[sizeof(msg) - 1] = '\0';

	OutputDebugString(msg);
}

/*
==============
Sys_Sleep
==============
*/
void Sys_Sleep(int msec) {
	Sleep(msec);
}

/*
==============
Sys_ShowWindow
==============
*/
void Sys_ShowWindow(bool show) {
	::ShowWindow(win32.hWnd, show ? SW_SHOW : SW_HIDE);
}

/*
==============
Sys_IsWindowVisible
==============
*/
bool Sys_IsWindowVisible(void) {
	return (::IsWindowVisible(win32.hWnd) != 0);
}

/*
==============
Sys_Mkdir
==============
*/
void Sys_Mkdir(const char* path) {
	_mkdir(path);
}

/*
=================
Sys_FileTimeStamp
=================
*/
ID_TIME_T Sys_FileTimeStamp(FILE* fp) {
	if ( fp == NULL ) {
		return static_cast<ID_TIME_T>( -1 );
	}

	struct __stat64 st;
	if ( _fstat64( _fileno( fp ), &st ) != 0 ) {
		return static_cast<ID_TIME_T>( -1 );
	}
	return static_cast<ID_TIME_T>( st.st_mtime );
}

/*
==============
Sys_Cwd
==============
*/
const char* Sys_Cwd(void) {
	static char cwd[MAX_OSPATH];

	_getcwd(cwd, sizeof(cwd) - 1);
	cwd[MAX_OSPATH - 1] = 0;

	return cwd;
}

/*
==============
Sys_DefaultRuntimePath
==============
*/
static const char* Sys_DefaultRuntimePath(void) {
	static idStr runtimePath;

	runtimePath = Sys_EXEPath();
	if ( runtimePath.Length() > 0 ) {
		runtimePath.StripFilename();
	}
	if ( runtimePath.Length() == 0 ) {
		runtimePath = Sys_Cwd();
	}

	return runtimePath.c_str();
}

/*
==============
Sys_DefaultCDPath
==============
*/
const char* Sys_DefaultCDPath(void) {
	return Sys_DefaultRuntimePath();
}

/*
==============
Sys_DefaultBasePath
==============
*/
const char* Sys_DefaultBasePath(void) {
	return Sys_DefaultRuntimePath();
}

/*
==============
Sys_DefaultSavePath
==============
*/
const char* Sys_DefaultSavePath(void) {
	static idStr savePath;
	idStr knownFolderPath;
	const char *localAppData = getenv( "LOCALAPPDATA" );
	const char *userProfile = getenv( "USERPROFILE" );

	if ( Sys_GetKnownFolderPath( FOLDERID_LocalAppData, knownFolderPath ) &&
			Sys_BuildWindowsProductSavePath( knownFolderPath.c_str(), savePath ) ) {
		return savePath.c_str();
	}

	// Preserve the environment fallback for unusual stripped-down Windows
	// sessions where the Shell Known Folder query is unavailable.
	if ( Sys_BuildWindowsProductSavePath( localAppData, savePath ) ) {
		return savePath.c_str();
	}

	if ( Sys_GetKnownFolderPath( FOLDERID_SavedGames, knownFolderPath ) &&
			Sys_BuildWindowsProductSavePath( knownFolderPath.c_str(), savePath ) ) {
		return savePath.c_str();
	}

	if ( userProfile && userProfile[0] ) {
		knownFolderPath = userProfile;
		knownFolderPath.AppendPath( "Saved Games" );
		if ( Sys_BuildWindowsProductSavePath( knownFolderPath.c_str(), savePath ) ) {
			return savePath.c_str();
		}
	}

	savePath = Sys_Cwd();
	savePath.SlashesToBackSlashes();
	if ( Sys_IsAbsoluteWindowsPath( savePath ) ) {
		return savePath.c_str();
	}

	return ".";
}

/*
==============
Sys_EXEPath
==============
*/
const char* Sys_EXEPath(void) {
	static char exe[MAX_OSPATH];
	GetModuleFileName(NULL, exe, sizeof(exe) - 1);
	return exe;
}

/*
==============
Sys_GetPackageRootDirectory
==============
*/
bool Sys_GetPackageRootDirectory(char* packageRoot, int packageRootSize) {
	// Windows packages stage modules and assets next to the executable; there
	// is no separate application-bundle package root.
	if (packageRoot != NULL && packageRootSize > 0) {
		packageRoot[0] = '\0';
	}
	return false;
}

bool Sys_GetGameModuleRootDirectory(char* moduleRoot, int moduleRootSize) {
	if (moduleRoot != NULL && moduleRootSize > 0) {
		moduleRoot[0] = '\0';
	}
	return false;
}

/*
==============
Sys_ListFiles
==============
*/
int Sys_ListFiles(const char* directory, const char* extension, idStrList& list) {
	idStr		search;
	struct _finddata_t findinfo;
	// RB: 64 bit fixes, changed int to intptr_t
	intptr_t	findhandle;
	// RB end
	int			flag;

	if (!extension) {
		extension = "";
	}

	// passing a slash as extension will find directories
	if (extension[0] == '/' && extension[1] == 0) {
		extension = "";
		flag = 0;
	}
	else {
		flag = _A_SUBDIR;
	}

	search = directory ? directory : "";
	search += "\\*";
	search += extension;
	for ( int i = 0; i < search.Length(); i++ ) {
		if ( search[ i ] == '/' ) {
			search[ i ] = '\\';
		}
	}

	// search
	list.Clear();

	findhandle = _findfirst(search, &findinfo);
	if (findhandle == -1) {
		return -1;
	}

	do {
		if (flag ^ (findinfo.attrib & _A_SUBDIR)) {
			list.Append(findinfo.name);
		}
	} while (_findnext(findhandle, &findinfo) != -1);

	_findclose(findhandle);

	return list.Num();
}



/*
================
Sys_GetClipboardData
================
*/
char* Sys_GetClipboardData(void) {
	char* data = NULL;
	char* cliptext;

	if (OpenClipboard(NULL) != 0) {
		HANDLE hClipboardData;

		if ((hClipboardData = GetClipboardData(CF_TEXT)) != 0) {
			if ((cliptext = (char*)GlobalLock(hClipboardData)) != 0) {
				const SIZE_T clipSize = GlobalSize(hClipboardData);
				SIZE_T textLength = 0;

				if (clipSize > 0 && clipSize <= static_cast<SIZE_T>(idMath::INT_MAX - 1)) {
					while (textLength < clipSize && cliptext[textLength] != '\0') {
						textLength++;
					}
					data = (char*)Mem_Alloc((int)textLength + 1);
					if (textLength > 0) {
						memcpy(data, cliptext, textLength);
					}
					data[textLength] = '\0';
				}
				GlobalUnlock(hClipboardData);

				if (data != NULL) {
					strtok(data, "\n\r\b");
				}
			}
		}
		CloseClipboard();
	}
	return data;
}

/*
================
Sys_SetClipboardData
================
*/
void Sys_SetClipboardData(const char* string) {
	HGLOBAL HMem;
	char* PMem;
	size_t stringLength;

	if ( string == NULL ) {
		return;
	}

	stringLength = strlen(string) + 1;
	if ( stringLength > static_cast<size_t>(idMath::INT_MAX) ) {
		return;
	}

	// allocate memory block
	HMem = (char*)::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, stringLength);
	if (HMem == NULL) {
		return;
	}
	// lock allocated memory and obtain a pointer
	PMem = (char*)::GlobalLock(HMem);
	if (PMem == NULL) {
		::GlobalFree(HMem);
		return;
	}
	// copy text into allocated memory block
	memcpy(PMem, string, stringLength);
	// unlock allocated memory
	::GlobalUnlock(HMem);
	// open Clipboard
	if (!OpenClipboard(0)) {
		::GlobalFree(HMem);
		return;
	}
	// remove current Clipboard contents
	EmptyClipboard();
	// supply the memory handle to the Clipboard
	if (SetClipboardData(CF_TEXT, HMem) != NULL) {
		HMem = 0;
	}
	// close Clipboard
	CloseClipboard();
	if (HMem != NULL) {
		::GlobalFree(HMem);
	}
}

/*
========================================================================

DLL Loading

========================================================================
*/

/*
=====================
Sys_DLL_Load
=====================
*/
INT_PTR Sys_DLL_Load(const char* dllName) {
	HINSTANCE	libHandle;
	libHandle = LoadLibrary(dllName);
	// jmarshall - removed
	//	if ( libHandle ) {
	//		// since we can't have LoadLibrary load only from the specified path, check it did the right thing
	//		char loadedPath[ MAX_OSPATH ];
	//		GetModuleFileName( libHandle, loadedPath, sizeof( loadedPath ) - 1 );
	//		if ( idStr::IcmpPath( dllName, loadedPath ) ) {
	//			Sys_Printf( "ERROR: LoadLibrary '%s' wants to load '%s'\n", dllName, loadedPath );
	//			Sys_DLL_Unload( (int)libHandle );
	//			return 0;
	//		}
	//	}
	// jmarshall end
	return (INT_PTR)libHandle;
}

/*
=====================
Sys_DLL_GetProcAddress
=====================
*/
void* Sys_DLL_GetProcAddress(INT_PTR dllHandle, const char* procName) {
	return GetProcAddress((HINSTANCE)dllHandle, procName);
}

/*
=====================
Sys_DLL_Unload
=====================
*/
void Sys_DLL_Unload(INT_PTR dllHandle) {
	if (!dllHandle) {
		return;
	}
	if (FreeLibrary((HMODULE)dllHandle) == 0) {
		int lastError = GetLastError();
		LPVOID lpMsgBuf;
		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER,
			NULL,
			lastError,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			(LPTSTR)&lpMsgBuf,
			0,
			NULL
		);
		Sys_Error("Sys_DLL_Unload: FreeLibrary failed - %s (%d)", lpMsgBuf, lastError);
	}
}

/*
========================================================================

EVENT LOOP

========================================================================
*/

#include "../EventQueueContinuity.h"
#include "../EventDisposition.h"
#include "../EventRetirement.h"

#define	MAX_QUED_EVENTS		256
#define	MASK_QUED_EVENTS	( MAX_QUED_EVENTS - 1 )

sysEvent_t	eventQue[MAX_QUED_EVENTS];
static sysEventDispositionTag_t eventDispositionTags[MAX_QUED_EVENTS];
static std::uint64_t eventRetirementSerials[MAX_QUED_EVENTS];
static std::uint64_t eventRetirementHighwater = 0; // Never reset by clear/rebind.
int			eventHead = 0;
int			eventTail = 0;

// Only pending queue entries own their payload. Dequeued slots may still hold
// a pointer whose ownership has already transferred to the event consumer.
static void Sys_DiscardQueuedEvent( sysEvent_t &event ) {
	if ( event.evPtr != NULL ) {
		// Discarded console input can contain private CVar values. Wipe all
		// bounded console bytes without parsing text during queue teardown.
		if ( event.evType == SE_CONSOLE && event.evPtrLength > 0 ) {
			memset( event.evPtr, 0, static_cast<size_t>( event.evPtrLength ) );
		}
		Mem_Free( event.evPtr );
	}
	event.evPtr = NULL;
	event.evPtrLength = 0;
}

/*
================
Sys_QueEvent

Ptr should either be null, or point to a block of data that can
be freed by the game later.
================
*/
void Sys_QueEvent(int time, sysEventType_t type, int value, int value2, int ptrLength, void* ptr) {
	sysEvent_t* ev;

	ev = &eventQue[eventHead & MASK_QUED_EVENTS];

	if (eventHead - eventTail >= MAX_QUED_EVENTS) {
		Sys_InvalidateEventQueue();
		common->Printf("Sys_QueEvent: overflow\n");
		// we are discarding an event, but don't leak memory
		Sys_DiscardQueuedEvent( *ev );
		eventTail++;
	}

	eventHead++;

	ev->evType = type;
	ev->evValue = value;
	ev->evValue2 = value2;
	ev->evPtrLength = ptrLength;
	ev->evPtr = ptr;
	eventDispositionTags[ev - eventQue] = {};
	eventRetirementSerials[ev - eventQue] = 0;
}

bool Sys_QueTrackedEvent(sysEvent_t& event, sysEventDispositionTag_t& tag) noexcept {
	int type = 0;
	memcpy(&type, &event.evType, sizeof(type));
	if (!Sys_EventDispositionTagCurrent(tag) || type <= SE_NONE || type > SE_RETAINED_UI ||
		event.evPtrLength < 0 || event.evPtrLength > 1024 * 1024 ||
		((event.evPtrLength != 0) != (event.evPtr != NULL))) return false;
	if (eventHead - eventTail >= MAX_QUED_EVENTS) {
		Sys_InvalidateEventQueue();
		return false; // No eviction, ownership transfer or callback on failed admission.
	}
	if (eventRetirementHighwater == (std::numeric_limits<std::uint64_t>::max)()) return false;
	const int slot = eventHead & MASK_QUED_EVENTS;
	eventQue[slot] = event;
	eventDispositionTags[slot] = tag;
	eventRetirementSerials[slot] = ++eventRetirementHighwater;
	++eventHead;
	event = {}; tag = {};
	return true;
}


sysEventTransfer_t Sys_PeekEventDispositionTag(sysEventDispositionTag_t& out) noexcept {
    if (!Sys_EventDispositionBoundThread()) return sysEventTransfer_t::Refused;
    if (eventHead <= eventTail) return sysEventTransfer_t::Empty;
    out = eventDispositionTags[eventTail & MASK_QUED_EVENTS];
    return sysEventTransfer_t::Ready;
}
sysEventTransfer_t Sys_PeekEventForRetirement(openq4::NativeInputHead& out) noexcept {
    if (!Sys_EventDispositionBoundThread()) return sysEventTransfer_t::Refused;
    if (eventHead <= eventTail) return sysEventTransfer_t::Empty;
    const unsigned slot = static_cast<unsigned>(eventTail & MASK_QUED_EVENTS);
    if (!eventRetirementSerials[slot] || !eventDispositionTags[slot].ShapeValid()) return sysEventTransfer_t::Refused;
    out = Sys_EventRetirementHead(openq4::NativeInputLane::Platform, eventRetirementSerials[slot], slot,
        eventQue[slot], eventDispositionTags[slot]);
    return sysEventTransfer_t::Ready;
}
sysEventTransfer_t Sys_TakeEventForRetirement(openq4::NativeInputRoute& route,
    const openq4::NativeInputRoute::CancellationPermit& permit, sysEvent_t& event, sysEventDispositionTag_t& tag) noexcept {
    openq4::NativeInputHead head;
    const auto status = Sys_PeekEventForRetirement(head);
    if (status != sysEventTransfer_t::Ready) return status;
    if (!route.AllowsCancellation(permit, head)) return sysEventTransfer_t::Refused;
    // Serialized original event thread; no foreign call between comparison and transfer.
    const auto ownedEvent = eventQue[head.slot]; const auto ownedTag = eventDispositionTags[head.slot];
    eventQue[head.slot] = {}; eventDispositionTags[head.slot] = {}; eventRetirementSerials[head.slot] = 0;
    ++eventTail;
    event = ownedEvent; tag = ownedTag;
    return sysEventTransfer_t::Ready;
}

sysEventTransfer_t Sys_TakeEventWithDisposition(sysEvent_t& event, sysEventDispositionTag_t& tag) noexcept {
	if (!Sys_EventDispositionEpoch()) return sysEventTransfer_t::Refused;
	if (eventHead <= eventTail) return sysEventTransfer_t::Empty;
	const int slot = eventTail & MASK_QUED_EVENTS;
	const auto ownedTag = eventDispositionTags[slot];
	if (!ownedTag.Empty() && !Sys_EventDispositionTagCurrent(ownedTag)) return sysEventTransfer_t::Refused;
	const auto ownedEvent = eventQue[slot];
	eventQue[slot] = {}; eventDispositionTags[slot] = {};
	eventRetirementSerials[slot] = 0;
	++eventTail;
	event = ownedEvent; tag = ownedTag;
	return sysEventTransfer_t::Ready;
}

/*
=============
Sys_PumpEvents

This allows windows to be moved during renderbump
=============
*/
void Sys_PumpEvents(void) {
    if (NativeInput_OwnsPump()) return; // Same owner also covers support windows.
	MSG msg;

#ifdef USE_SDL3
	(void)Sys_SDL_PumpEvents();
#endif

	// pump the message loop
	while (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
		if (!GetMessage(&msg, NULL, 0, 0)) {
			common->Quit();
		}

		// save the msg time, because wndprocs don't have access to the timestamp
		if (win32.sysMsgTime && win32.sysMsgTime > (int)msg.time) {
			// don't ever let the event times run backwards	
//			common->Printf( "Sys_PumpEvents: win32.sysMsgTime (%i) > msg.time (%i)\n", win32.sysMsgTime, msg.time );
		}
		else {
			win32.sysMsgTime = msg.time;
		}

#ifdef ID_ALLOW_TOOLS
		if (GUIEditorHandleMessage(&msg)) {
			continue;
		}
#endif

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

/*
================
Sys_GenerateEvents
================
*/
void Sys_GenerateEvents(void) {
	static int entered = false;
	char* s;

	if (entered) {
		return;
	}
	entered = true;

	// pump the message loop
	Sys_PumpEvents();

	// make sure mouse and joystick are only called once a frame
	IN_Frame();

	// check for console commands
	s = Sys_ConsoleInput();
	if (s) {
		char* b;
		int		len;
		const bool privateCommand = cvarSystem != NULL && cvarSystem->IsInitialized() &&
			cvarSystem->CommandContainsPrivateCVar( s );

		len = idLib::SizeToInt( strlen( s ) + 1, "Sys_GenerateEvents console command" );
		b = (char*)Mem_Alloc(len);
		strcpy(b, s);
		if ( privateCommand ) {
			memset( s, 0, len );
		}
		Sys_QueEvent(0, SE_CONSOLE, 0, 0, len, b);
	}

	entered = false;
}

/*
================
Sys_ClearEvents
================
*/
void Sys_ClearEvents(void) {
	Sys_InvalidateEventQueue();
	while ( eventHead > eventTail ) {
		Sys_DiscardQueuedEvent( eventQue[ eventTail & MASK_QUED_EVENTS ] );
		eventDispositionTags[eventTail & MASK_QUED_EVENTS] = {};
		eventRetirementSerials[eventTail & MASK_QUED_EVENTS] = 0;
		eventTail++;
	}
	eventHead = eventTail = 0;
}

/*
================
Sys_GetEvent
================
*/
sysEvent_t Sys_GetEvent(void) {
	sysEvent_t	ev;

	// return if we have data
	if (eventHead > eventTail) {
		if (!eventDispositionTags[eventTail & MASK_QUED_EVENTS].Empty()) {
			Sys_InvalidateEventQueue();
			return sysEvent_t{}; // Preserve original tagged ownership for explicit retirement.
		}
		eventTail++;
		return eventQue[(eventTail - 1) & MASK_QUED_EVENTS];
	}

	// return the empty event 
	memset(&ev, 0, sizeof(ev));

	return ev;
}

//================================================================

/*
=================
Sys_In_Restart_f

Restart the input subsystem
=================
*/
void Sys_In_Restart_f(const idCmdArgs& args) {
	Sys_ShutdownInput();
	Sys_InitInput();
}

/*
==================
Sys_AsyncThread
==================
*/
static void Sys_AsyncThread(void* parm) {
	int		wakeNumber;
	int		startTime;

	startTime = Sys_Milliseconds();
	wakeNumber = 0;

	while (1) {
#ifdef WIN32	
		// Wake frequently enough that common->Async can service the exact 60 Hz schedule.
		int r = WaitForSingleObject(hTimer, 100);
		if (r != WAIT_OBJECT_0) {
			OutputDebugString("idPacketServer::PacketServerInterrupt: bad wait return");
		}
#endif

#if 0
		wakeNumber++;
		int		msec = Sys_Milliseconds();
		int		deltaTime = msec - startTime;
		startTime = msec;

		char	str[1024];
		sprintf(str, "%i ", deltaTime);
		OutputDebugString(str);
#endif


		common->Async();
	}
}

/*
==============
Sys_StartAsyncThread

Start the thread that will call idCommon::Async()
==============
*/
void Sys_StartAsyncThread(void) {
	// Wake at a fine enough cadence that common->Async can hit the exact 60 Hz schedule.
	hTimer = CreateWaitableTimer(NULL, false, NULL);
	if (!hTimer) {
		common->Error("idPacketServer::Spawn: CreateWaitableTimer failed");
	}

	LARGE_INTEGER	t;
	t.HighPart = t.LowPart = 0;
	SetWaitableTimer(hTimer, &t, 1, NULL, NULL, TRUE);

	Sys_CreateThread((xthread_t)Sys_AsyncThread, NULL, THREAD_ABOVE_NORMAL, threadInfo, "Async", g_threads, &g_thread_count);

#ifdef SET_THREAD_AFFINITY 
	// give the async thread an affinity for the second cpu
	SetThreadAffinityMask(reinterpret_cast<HANDLE>(threadInfo.threadHandle), 2);
#endif

	if (!threadInfo.threadHandle) {
		common->Error("Sys_StartAsyncThread: failed");
	}
}

/*
================
Sys_AlreadyRunning

returns true if there is a copy of openQ4 running already
================
*/
bool Sys_AlreadyRunning(void) {
#ifndef DEBUG
	if (!win32.win_allowMultipleInstances.GetBool()) {
		::CreateMutex(NULL, FALSE, "openQ4");
		if (::GetLastError() == ERROR_ALREADY_EXISTS || ::GetLastError() == ERROR_ACCESS_DENIED) {
			return true;
		}
	}
#endif
	return false;
}

/*
================
Sys_Init

The cvar system must already be setup
================
*/
void Sys_Init(void) {

	CoInitialize(NULL);

	// make sure the timer is high precision, otherwise
	// NT gets 18ms resolution
	timeBeginPeriod(1);

	// get WM_TIMER messages pumped every millisecond
//	SetTimer( NULL, 0, 100, NULL );

	cmdSystem->AddCommand("in_restart", Sys_In_Restart_f, CMD_FL_SYSTEM, "restarts the input system");
#ifdef DEBUG
	cmdSystem->AddCommand("createResourceIDs", CreateResourceIDs_f, CMD_FL_TOOL, "assigns resource IDs in _resouce.h files");
#endif
#if 0
	cmdSystem->AddCommand("setAsyncSound", Sys_SetAsyncSound_f, CMD_FL_SYSTEM, "set the async sound option");
#endif

	//
	// Windows user name
	//
	win32.win_username.SetString(Sys_GetCurrentUser());

	//
	// Windows version
	//
	if ( !Sys_QueryWindowsVersion( win32.osversion ) ) {
		Sys_Error( "Couldn't query Windows version" );
	}

	if ( !Sys_IsWindowsVersionOrGreater( win32.osversion, OPENQ4_MIN_WINDOWS_MAJOR_VERSION, OPENQ4_MIN_WINDOWS_MINOR_VERSION ) ) {
		Sys_Error( GAME_NAME " requires Windows 7 or newer" );
	}

	win32.sys_arch.SetString( Sys_FormatWindowsVersion( win32.osversion ) );
	if ( !Sys_IsWindowsVersionOrGreater( win32.osversion, OPENQ4_VALIDATED_WINDOWS_MAJOR_VERSION, OPENQ4_VALIDATED_WINDOWS_MINOR_VERSION ) ) {
		common->Printf(
			"WARNING: %s is outside openQ4's actively validated Windows support matrix.\n",
			win32.sys_arch.GetString() );
	}

	//
	// CPU type
	//
	bool forcedCpuType = false;
	if (!idStr::Icmp(win32.sys_cpustring.GetString(), "detect")) {
		idStr string;
		idStr processorName;
		int logicalCores = 0;
		int physicalCores = 0;
		int cpuPackages = 0;
		const double processorFrequencyHz = Sys_GetApproximateProcessorFrequencyHz();

		win32.cpuid = Sys_GetCPUId();
		Sys_GetProcessorTopology( logicalCores, physicalCores, cpuPackages );

		if ( !Sys_ReadProcessorRegistryString( "ProcessorNameString", processorName ) ) {
			processorName = Sys_GetFallbackProcessorName( win32.cpuid );
		}

		string = Sys_FormatProcessorSummary( processorName.c_str(), CPUSTRING, physicalCores, logicalCores, cpuPackages, processorFrequencyHz );
		win32.sys_cpustring.SetString(string);
	}
	else {
		forcedCpuType = true;
		idLexer src(win32.sys_cpustring.GetString(), idStr::Length(win32.sys_cpustring.GetString()), "sys_cpustring");
		idToken token;

		int id = CPUID_NONE;
		while (src.ReadToken(&token)) {
			if (token.Icmp("generic") == 0) {
				id |= CPUID_GENERIC;
			}
			else if (token.Icmp("intel") == 0) {
				id |= CPUID_INTEL;
			}
			else if (token.Icmp("amd") == 0) {
				id |= CPUID_AMD;
			}
			else if (token.Icmp("mmx") == 0) {
				id |= CPUID_MMX;
			}
			else if (token.Icmp("3dnow") == 0) {
				id |= CPUID_3DNOW;
			}
			else if (token.Icmp("sse") == 0) {
				id |= CPUID_SSE;
			}
			else if (token.Icmp("sse2") == 0) {
				id |= CPUID_SSE2;
			}
			else if (token.Icmp("sse3") == 0) {
				id |= CPUID_SSE3;
			}
			else if (token.Icmp("htt") == 0) {
				id |= CPUID_HTT;
			}
		}
		if (id == CPUID_NONE) {
			common->Printf("WARNING: unknown sys_cpustring '%s'\n", win32.sys_cpustring.GetString());
			id = CPUID_GENERIC;
		}
		win32.cpuid = (cpuid_t)id;
	}

	idStr cpuSummary = win32.sys_cpustring.GetString();

	if ( forcedCpuType ) {
		common->Printf( "CPU type override: %s\n", cpuSummary.c_str() );
	} else {
		common->Printf( "CPU: %s\n", cpuSummary.c_str() );
	}

	common->Printf( "System memory: %s\n", Sys_FormatMemoryMB( Sys_GetSystemRam() ).c_str() );
	common->Printf( "Video memory: %s\n", Sys_FormatMemoryMB( Sys_GetVideoRam() ).c_str() );
}

/*
================
Sys_Shutdown
================
*/
void Sys_Shutdown(void) {
	CoUninitialize();
}

/*
================
Sys_GetProcessorId
================
*/
cpuid_t Sys_GetProcessorId(void) {
	return win32.cpuid;
}

/*
================
Sys_GetProcessorString
================
*/
const char* Sys_GetProcessorString(void) {
	return win32.sys_cpustring.GetString();
}

//=======================================================================

//#define SET_THREAD_AFFINITY


/*
====================
Win_Frame
====================
*/
void Win_Frame(void) {
	// if "viewlog" has been modified, show or hide the log console
	if (win32.win_viewlog.IsModified()) {
		if (!com_skipRenderer.GetBool() && idAsyncNetwork::serverDedicated.GetInteger() != 1) {
			Sys_ShowConsole(win32.win_viewlog.GetInteger(), false);
		}
		win32.win_viewlog.ClearModified();
	}
}

extern "C" { void _chkstk(int size); };
void clrstk(void);

/*
====================
TestChkStk
====================
*/
void TestChkStk(void) {
	int		buffer[0x1000];

	buffer[0] = 1;
}

/*
====================
HackChkStk
====================
*/
void HackChkStk(void) {

}

/*
====================
GetExceptionCodeInfo
====================
*/
const char* GetExceptionCodeInfo(UINT code) {
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION: return "The thread tried to read from or write to a virtual address for which it does not have the appropriate access.";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "The thread tried to access an array element that is out of bounds and the underlying hardware supports bounds checking.";
	case EXCEPTION_BREAKPOINT: return "A breakpoint was encountered.";
	case EXCEPTION_DATATYPE_MISALIGNMENT: return "The thread tried to read or write data that is misaligned on hardware that does not provide alignment. For example, 16-bit values must be aligned on 2-byte boundaries; 32-bit values on 4-byte boundaries, and so on.";
	case EXCEPTION_FLT_DENORMAL_OPERAND: return "One of the operands in a floating-point operation is denormal. A denormal value is one that is too small to represent as a standard floating-point value.";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "The thread tried to divide a floating-point value by a floating-point divisor of zero.";
	case EXCEPTION_FLT_INEXACT_RESULT: return "The result of a floating-point operation cannot be represented exactly as a decimal fraction.";
	case EXCEPTION_FLT_INVALID_OPERATION: return "This exception represents any floating-point exception not included in this list.";
	case EXCEPTION_FLT_OVERFLOW: return "The exponent of a floating-point operation is greater than the magnitude allowed by the corresponding type.";
	case EXCEPTION_FLT_STACK_CHECK: return "The stack overflowed or underflowed as the result of a floating-point operation.";
	case EXCEPTION_FLT_UNDERFLOW: return "The exponent of a floating-point operation is less than the magnitude allowed by the corresponding type.";
	case EXCEPTION_ILLEGAL_INSTRUCTION: return "The thread tried to execute an invalid instruction.";
	case EXCEPTION_IN_PAGE_ERROR: return "The thread tried to access a page that was not present, and the system was unable to load the page. For example, this exception might occur if a network connection is lost while running a program over the network.";
	case EXCEPTION_INT_DIVIDE_BY_ZERO: return "The thread tried to divide an integer value by an integer divisor of zero.";
	case EXCEPTION_INT_OVERFLOW: return "The result of an integer operation caused a carry out of the most significant bit of the result.";
	case EXCEPTION_INVALID_DISPOSITION: return "An exception handler returned an invalid disposition to the exception dispatcher. Programmers using a high-level language such as C should never encounter this exception.";
	case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "The thread tried to continue execution after a noncontinuable exception occurred.";
	case EXCEPTION_PRIV_INSTRUCTION: return "The thread tried to execute an instruction whose operation is not allowed in the current machine mode.";
	case EXCEPTION_SINGLE_STEP: return "A trace trap or other single-instruction mechanism signaled that one instruction has been executed.";
	case EXCEPTION_STACK_OVERFLOW: return "The thread used up its stack.";
	default: return "Unknown exception";
	}
}

/*
====================
EmailCrashReport

  emailer originally from Raven/Quake 4
====================
*/
void EmailCrashReport(LPSTR messageText) {
	LPMAPISENDMAIL	MAPISendMail;
	MapiMessage		message;
	static int lastEmailTime = 0;

	if (Sys_Milliseconds() < lastEmailTime + 10000) {
		return;
	}

	lastEmailTime = Sys_Milliseconds();

	HINSTANCE mapi = LoadLibrary("MAPI32.DLL");
	if (mapi) {
		MAPISendMail = (LPMAPISENDMAIL)GetProcAddress(mapi, "MAPISendMail");
		if (MAPISendMail) {
			MapiRecipDesc toProgrammers =
			{
				0,										// ulReserved
					MAPI_TO,							// ulRecipClass
					"DOOM 3 Crash",						// lpszName
					"SMTP:programmers@idsoftware.com",	// lpszAddress
					0,									// ulEIDSize
					0									// lpEntry
			};

			memset(&message, 0, sizeof(message));
			message.lpszSubject = "DOOM 3 Fatal Error";
			message.lpszNoteText = messageText;
			message.nRecipCount = 1;
			message.lpRecips = &toProgrammers;

			MAPISendMail(
				0,									// LHANDLE lhSession
				0,									// ULONG ulUIParam
				&message,							// lpMapiMessage lpMessage
				MAPI_DIALOG,						// FLAGS flFlags
				0									// ULONG ulReserved
			);
		}
		FreeLibrary(mapi);
	}
}

int Sys_FPU_PrintStateFlags(char* ptr, int ctrl, int stat, int tags, int inof, int inse, int opof, int opse);


#define TEST_FPU_EXCEPTIONS	/*	FPU_EXCEPTION_INVALID_OPERATION |		*/	\
							/*	FPU_EXCEPTION_DENORMALIZED_OPERAND |	*/	\
							/*	FPU_EXCEPTION_DIVIDE_BY_ZERO |			*/	\
							/*	FPU_EXCEPTION_NUMERIC_OVERFLOW |		*/	\
							/*	FPU_EXCEPTION_NUMERIC_UNDERFLOW |		*/	\
							/*	FPU_EXCEPTION_INEXACT_RESULT |			*/	\
								0

/*
==================
DoomMain
==================
*/
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	const HCURSOR hcurSave = ::SetCursor(LoadCursor(0, IDC_WAIT));
	idStr translatedCmdLine;
	const char *effectiveCmdLine = lpCmdLine;
	idList<idStr> utf8Args;
	idList<const char *> argvArgs;

	Sys_SetPhysicalWorkMemory(192 << 20, 1024 << 20);

	Sys_GetCurrentMemoryStatus(exeLaunchMemoryStats);

	win32.hInstance = hInstance;
	if ( Sys_TryTranslateopenQ4ProtocolCommandLine( lpCmdLine, translatedCmdLine ) ) {
		effectiveCmdLine = translatedCmdLine.c_str();
	}
	idStr::Copynz(sys_cmdline, effectiveCmdLine, sizeof(sys_cmdline));

	Sys_SetProcessDpiAwarenessForEarlyWindows();

	// done before Com/Sys_Init since we need this for error output
	Sys_CreateConsole();

	// no abort/retry/fail errors
	SetErrorMode(SEM_FAILCRITICALERRORS);

	// Write crash logs and minidumps for unhandled exceptions.
	Sys_InstallCrashHandler();

	for (int i = 0; i < MAX_CRITICAL_SECTIONS; i++) {
		InitializeCriticalSection(&win32.criticalSections[i]);
	}

	// get the initial time base
	Sys_Milliseconds();

#ifdef DEBUG
	// disable the painfully slow MS heap check every 1024 allocs
	_CrtSetDbgFlag(0);
#endif

	//	Sys_FPU_EnableExceptions( TEST_FPU_EXCEPTIONS );
	Sys_FPU_SetPrecision(FPU_PRECISION_DOUBLE_EXTENDED);

	if ( effectiveCmdLine != lpCmdLine ) {
		common->Init( 0, NULL, effectiveCmdLine );
	} else if ( Sys_BuildWindowsUtf8Argv( utf8Args, argvArgs ) ) {
		if ( argvArgs.Num() <= 0 ) {
			common->Init( 0, NULL, NULL );
		} else {
			common->Init( argvArgs.Num(), &argvArgs[ 0 ], NULL );
		}
	} else {
		common->Init( 0, NULL, effectiveCmdLine );
	}

#if TEST_FPU_EXCEPTIONS != 0
	common->Printf(Sys_FPU_GetState());
#endif

	Sys_StartAsyncThread();

	// hide or show the early console as necessary
	if (win32.win_viewlog.GetInteger() || com_skipRenderer.GetBool() || idAsyncNetwork::serverDedicated.GetInteger()) {
		Sys_ShowConsole(1, true);
	}
	else {
		Sys_ShowConsole(0, false);
	}

#ifdef SET_THREAD_AFFINITY 
	// give the main thread an affinity for the first cpu
	SetThreadAffinityMask(GetCurrentThread(), 1);
#endif

	::SetCursor(hcurSave);

	// Launch the script debugger
	if (strstr(lpCmdLine, "+debugger")) {
		// DebuggerClientInit( lpCmdLine );
		return 0;
	}

	::SetFocus(win32.hWnd);

	// main game loop
	while (1) {

		Win_Frame();

#ifdef DEBUG
		Sys_MemFrame();
#endif

		// set exceptions, even if some crappy syscall changes them!
		Sys_FPU_EnableExceptions(TEST_FPU_EXCEPTIONS);

#ifdef ID_ALLOW_TOOLS
		if (com_editors) {
			if (com_editors & EDITOR_GUI) {
				// GUI editor
				GUIEditorRun();
			}
			else if (com_editors & EDITOR_RADIANT) {
				// Level Editor
				RadiantRun();
			}
			else if (com_editors & EDITOR_MATERIAL) {
				//BSM Nerve: Add support for the material editor
				MaterialEditorRun();
			}
			else {
				if (com_editors & EDITOR_LIGHT) {
					// in-game Light Editor
					LightEditorRun();
				}
				if (com_editors & EDITOR_SOUND) {
					// in-game Sound Editor
					SoundEditorRun();
				}
				if (com_editors & EDITOR_DECL) {
					// in-game Declaration Browser
					DeclBrowserRun();
				}
				if (com_editors & EDITOR_AF) {
					// in-game Articulated Figure Editor
					AFEditorRun();
				}
				if (com_editors & EDITOR_PARTICLE) {
					// in-game Particle Editor
					//ParticleEditorRun();
				}
				if (com_editors & EDITOR_SCRIPT) {
					// in-game Script Editor
					ScriptEditorRun();
				}
			}
		}
#endif
		// run the game
		common->Frame();
	}

	// never gets here
	return 0;
}

/*
====================
clrstk

I tried to get the run time to call this at every function entry, but
====================
*/
static int	parmBytes;
void clrstk(void) {

}

/*
==================
idSysLocal::OpenURL
==================
*/
void idSysLocal::OpenURL(const char* url, bool doexit) {
	static bool doexit_spamguard = false;
	HWND wnd;

	if ( !idURLPolicy::IsAllowedHTTPURL( url ) ) {
		common->Printf( "OpenURL rejected: expected a bounded HTTP or HTTPS URL with a host\n" );
		return;
	}

	if (doexit_spamguard) {
		common->DPrintf("OpenURL: already in an exit sequence, ignoring request\n");
		return;
	}

	common->Printf("Open URL: %s\n", url);

	const HINSTANCE result = ShellExecute(NULL, "open", url, NULL, NULL, SW_RESTORE);
	if (reinterpret_cast<INT_PTR>(result) <= 32) {
		common->Printf("OpenURL failed after validation\n");
		return;
	}

	wnd = GetForegroundWindow();
	if (wnd) {
		ShowWindow(wnd, SW_MAXIMIZE);
	}

	if (doexit) {
		doexit_spamguard = true;
		cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "quit\n");
	}
}

/*
==================
Sys_SetFatalError
==================
*/
void Sys_SetFatalError(const char* error) {
}

/*
==================
Sys_DoPreferences
==================
*/
void Sys_DoPreferences(void) {
}
