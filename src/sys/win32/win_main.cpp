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




#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <direct.h>
#include <io.h>
#include <conio.h>
#include <mapi.h>
#include <ShellAPI.h>

#ifndef __MRC__
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "../sys_local.h"
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

static double Sys_GetApproximateProcessorFrequencyHz( void ) {
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

static int Sys_OpenQ4ProtocolHexValue( const char c ) {
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

static void Sys_DecodeOpenQ4ProtocolComponent( const idStr &source, idStr &decoded ) {
	decoded.Clear();

	for ( int i = 0; i < source.Length(); ++i ) {
		const char c = source[ i ];
		if ( c == '%' && ( i + 2 ) < source.Length() ) {
			const int hi = Sys_OpenQ4ProtocolHexValue( source[ i + 1 ] );
			const int lo = Sys_OpenQ4ProtocolHexValue( source[ i + 2 ] );
			if ( hi >= 0 && lo >= 0 ) {
				decoded.Append( static_cast<char>( ( hi << 4 ) | lo ) );
				i += 2;
				continue;
			}
		}
		decoded.Append( c );
	}
}

static bool Sys_IsValidOpenQ4ConnectTarget( const idStr &target ) {
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

static bool Sys_TryReadOpenQ4ProtocolQueryValue( const idStr &query, const char *key, idStr &value ) {
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

static bool Sys_TryTranslateOpenQ4ProtocolCommandLine( const char *rawCmdLine, idStr &translatedCmdLine ) {
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
		if ( !Sys_TryReadOpenQ4ProtocolQueryValue( query, "server", encodedTarget ) ) {
			if ( !Sys_TryReadOpenQ4ProtocolQueryValue( query, "target", encodedTarget ) ) {
				Sys_TryReadOpenQ4ProtocolQueryValue( query, "address", encodedTarget );
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

	Sys_DecodeOpenQ4ProtocolComponent( encodedTarget, decodedTarget );
	if ( !Sys_IsValidOpenQ4ConnectTarget( decodedTarget ) ) {
		return true;
	}

	translatedCmdLine = "+connect \"";
	translatedCmdLine.Append( decodedTarget );
	translatedCmdLine.Append( '"' );
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
	HANDLE temp = CreateThread(NULL,	// LPSECURITY_ATTRIBUTES lpsa,
		0,		// DWORD cbStack,
		(LPTHREAD_START_ROUTINE)function,	// LPTHREAD_START_ROUTINE lpStartAddr,
		parms,	// LPVOID lpvThreadParm,
		0,		//   DWORD fdwCreate,
		&info.threadId);
	info.threadHandle = (int)temp;
	if (priority == THREAD_HIGHEST) {
		SetThreadPriority((HANDLE)info.threadHandle, THREAD_PRIORITY_HIGHEST);		//  we better sleep enough to do this
	}
	else if (priority == THREAD_ABOVE_NORMAL) {
		SetThreadPriority((HANDLE)info.threadHandle, THREAD_PRIORITY_ABOVE_NORMAL);
	}
	info.name = name;
	if (*thread_count < MAX_THREADS) {
		threads[(*thread_count)++] = &info;
	}
	else {
		common->DPrintf("WARNING: MAX_THREADS reached\n");
	}
}

/*
==================
Sys_DestroyThread
==================
*/
void Sys_DestroyThread(xthreadInfo& info) {
	WaitForSingleObject((HANDLE)info.threadHandle, INFINITE);
	CloseHandle((HANDLE)info.threadHandle);
	info.threadHandle = 0;
}

/*
==================
Sys_Sentry
==================
*/
void Sys_Sentry() {
	int j = 0;
}

/*
==================
Sys_GetThreadName
==================
*/
const char* Sys_GetThreadName(int* index) {
	int id = GetCurrentThreadId();
	for (int i = 0; i < g_thread_count; i++) {
		if (id == g_threads[i]->threadId) {
			if (index) {
				*index = i;
			}
			return g_threads[i]->name;
		}
	}
	if (index) {
		*index = -1;
	}
	return "main";
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
	va_list		argptr;
	char		text[4096];
	MSG        msg;

	va_start(argptr, error);
	vsprintf(text, error, argptr);
	va_end(argptr);

	Conbuf_AppendText(text);
	Conbuf_AppendText("\n");

	Win_SetErrorText(text);
	Sys_ShowConsole(1, true);

	timeEndPeriod(1);

	Sys_ShutdownInput();

	// Common::Shutdown may already have torn down the renderer path.
	// Avoid forcing GL shutdown when there is no active GL context.
	if ( renderSystem->IsOpenGLRunning() ) {
		GLimp_Shutdown();
	}

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
	struct _stat st;
	_fstat(_fileno(fp), &st);
	return (long)st.st_mtime;
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
	const char *localAppData = getenv( "LOCALAPPDATA" );
	const char *userProfile = getenv( "USERPROFILE" );

	if ( localAppData && localAppData[0] ) {
		savePath = localAppData;
		savePath.AppendPath( "OpenQ4" );
		return savePath.c_str();
	}

	if ( userProfile && userProfile[0] ) {
		savePath = userProfile;
		savePath.AppendPath( "Saved Games" );
		savePath.AppendPath( "OpenQ4" );
		return savePath.c_str();
	}

	return Sys_Cwd();
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

	sprintf(search, "%s\\*%s", directory, extension);

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
				data = (char*)Mem_Alloc(GlobalSize(hClipboardData) + 1);
				strcpy(data, cliptext);
				GlobalUnlock(hClipboardData);

				strtok(data, "\n\r\b");
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

	// allocate memory block
	HMem = (char*)::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, strlen(string) + 1);
	if (HMem == NULL) {
		return;
	}
	// lock allocated memory and obtain a pointer
	PMem = (char*)::GlobalLock(HMem);
	if (PMem == NULL) {
		return;
	}
	// copy text into allocated memory block
	lstrcpy(PMem, string);
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
	SetClipboardData(CF_TEXT, HMem);
	HMem = 0;
	// close Clipboard
	CloseClipboard();
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

#define	MAX_QUED_EVENTS		256
#define	MASK_QUED_EVENTS	( MAX_QUED_EVENTS - 1 )

sysEvent_t	eventQue[MAX_QUED_EVENTS];
int			eventHead = 0;
int			eventTail = 0;

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
		common->Printf("Sys_QueEvent: overflow\n");
		// we are discarding an event, but don't leak memory
		if (ev->evPtr) {
			Mem_Free(ev->evPtr);
		}
		eventTail++;
	}

	eventHead++;

	ev->evType = type;
	ev->evValue = value;
	ev->evValue2 = value2;
	ev->evPtrLength = ptrLength;
	ev->evPtr = ptr;
}

/*
=============
Sys_PumpEvents

This allows windows to be moved during renderbump
=============
*/
void Sys_PumpEvents(void) {
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

		len = strlen(s) + 1;
		b = (char*)Mem_Alloc(len);
		strcpy(b, s);
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
	SetThreadAffinityMask((HANDLE)threadInfo.threadHandle, 2);
#endif

	if (!threadInfo.threadHandle) {
		common->Error("Sys_StartAsyncThread: failed");
	}
}

/*
================
Sys_AlreadyRunning

returns true if there is a copy of D3 running already
================
*/
bool Sys_AlreadyRunning(void) {
#ifndef DEBUG
	if (!win32.win_allowMultipleInstances.GetBool()) {
		HANDLE hMutexOneInstance = ::CreateMutex(NULL, FALSE, "OpenQ4");
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
			"WARNING: %s is outside OpenQ4's actively validated Windows support matrix.\n",
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

	Sys_SetPhysicalWorkMemory(192 << 20, 1024 << 20);

	Sys_GetCurrentMemoryStatus(exeLaunchMemoryStats);

	win32.hInstance = hInstance;
	if ( Sys_TryTranslateOpenQ4ProtocolCommandLine( lpCmdLine, translatedCmdLine ) ) {
		effectiveCmdLine = translatedCmdLine.c_str();
	}
	idStr::Copynz(sys_cmdline, effectiveCmdLine, sizeof(sys_cmdline));

	// done before Com/Sys_Init since we need this for error output
	Sys_CreateConsole();

	// no abort/retry/fail errors
	SetErrorMode(SEM_FAILCRITICALERRORS);

	// debug builds write crash logs + minidumps for unhandled exceptions.
	Sys_InstallDebugCrashHandler();

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

	common->Init(0, NULL, effectiveCmdLine);

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

	if (doexit_spamguard) {
		common->DPrintf("OpenURL: already in an exit sequence, ignoring %s\n", url);
		return;
	}

	common->Printf("Open URL: %s\n", url);

	if (!ShellExecute(NULL, "open", url, NULL, NULL, SW_RESTORE)) {
		common->Error("Could not open url: '%s' ", url);
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
