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




#include "win_local.h"
#include <lmerr.h>
#include <lmcons.h>
#include <lmwksta.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <direct.h>
#include <io.h>
#include <dxgi.h>
#include <bcrypt.h>
#include <psapi.h>

/*
================
Sys_GetSecureRandomBytes
================
*/
bool Sys_GetSecureRandomBytes( void *buffer, int bytes ) {
	if ( bytes < 0 || ( bytes > 0 && buffer == NULL ) ) {
		return false;
	}
	if ( bytes == 0 ) {
		return true;
	}
	return BCryptGenRandom( NULL, static_cast<PUCHAR>( buffer ),
		static_cast<ULONG>( bytes ), BCRYPT_USE_SYSTEM_PREFERRED_RNG ) == 0;
}

namespace {

static const int OPENQ4_WIN32_FALLBACK_VIDEO_RAM_MB = 2048;
static const int OPENQ4_WIN32_MAX_REASONABLE_VIDEO_RAM_MB = 256 * 1024;

typedef HRESULT ( WINAPI *openQ4_CreateDXGIFactoryFn )( REFIID riid, void **factory );

bool Sys_TryGetDriveFreeSpaceBytes( const char *path, DWORDLONG &freeBytes ) {
	DWORDLONG bytesAvailable;
	DWORDLONG totalBytes;
	DWORDLONG totalFreeBytes;

	if ( path == NULL || path[0] == '\0' ) {
		return false;
	}

	if ( ::GetDiskFreeSpaceExA(
			path,
			(PULARGE_INTEGER)&bytesAvailable,
			(PULARGE_INTEGER)&totalBytes,
			(PULARGE_INTEGER)&totalFreeBytes ) ) {
		freeBytes = bytesAvailable;
		return true;
	}

	return false;
}

int Sys_QueryDXGIVideoRamMB( void ) {
	HMODULE dxgiModule = LoadLibraryA( "dxgi.dll" );
	if ( dxgiModule == NULL ) {
		return 0;
	}

	openQ4_CreateDXGIFactoryFn createDXGIFactory = reinterpret_cast<openQ4_CreateDXGIFactoryFn>( GetProcAddress( dxgiModule, "CreateDXGIFactory" ) );
	if ( createDXGIFactory == NULL ) {
		FreeLibrary( dxgiModule );
		return 0;
	}

	IDXGIFactory *factory = NULL;
	if ( FAILED( createDXGIFactory( __uuidof( IDXGIFactory ), reinterpret_cast<void **>( &factory ) ) ) || factory == NULL ) {
		FreeLibrary( dxgiModule );
		return 0;
	}

	unsigned long long maxDedicatedBytes = 0;
	for ( UINT adapterIndex = 0; ; ++adapterIndex ) {
		IDXGIAdapter *adapter = NULL;
		const HRESULT enumResult = factory->EnumAdapters( adapterIndex, &adapter );
		if ( enumResult == DXGI_ERROR_NOT_FOUND ) {
			break;
		}
		if ( FAILED( enumResult ) || adapter == NULL ) {
			break;
		}

		DXGI_ADAPTER_DESC desc;
		if ( SUCCEEDED( adapter->GetDesc( &desc ) ) &&
			 static_cast<unsigned long long>( desc.DedicatedVideoMemory ) > maxDedicatedBytes ) {
			maxDedicatedBytes = static_cast<unsigned long long>( desc.DedicatedVideoMemory );
		}
		adapter->Release();
	}

	factory->Release();
	FreeLibrary( dxgiModule );

	if ( maxDedicatedBytes == 0 ) {
		return 0;
	}

	const unsigned long long megabytes = ( maxDedicatedBytes + ( 1024ULL * 1024ULL - 1ULL ) ) / ( 1024ULL * 1024ULL );
	if ( megabytes == 0 || megabytes > OPENQ4_WIN32_MAX_REASONABLE_VIDEO_RAM_MB ) {
		return 0;
	}
	return static_cast<int>( megabytes );
}

void Sys_NormalizeDriveProbePath( char *path ) {
	if ( path == NULL ) {
		return;
	}

	for ( char *p = path; *p != '\0'; p++ ) {
		if ( *p == '/' ) {
			*p = '\\';
		}
	}

	const size_t len = strlen( path );
	if ( len >= 2 && path[0] == '"' && path[len - 1] == '"' ) {
		memmove( path, path + 1, len - 2 );
		path[len - 2] = '\0';
	}
}

bool Sys_ResolveDriveProbePath( const char *path, char *resolvedPath, size_t resolvedPathSize ) {
	char normalizedPath[MAX_OSPATH];
	char fullPath[MAX_OSPATH];
	char volumePath[MAX_OSPATH];
	DWORD fullPathLen;

	if ( path == NULL || path[0] == '\0' ) {
		return false;
	}
	const int resolvedPathCapacity = idLib::SizeToInt( resolvedPathSize, "Sys_ResolveDriveProbePath" );

	idStr::Copynz( normalizedPath, path, sizeof( normalizedPath ) );
	Sys_NormalizeDriveProbePath( normalizedPath );

	fullPathLen = ::GetFullPathNameA( normalizedPath, sizeof( fullPath ), fullPath, NULL );
	if ( fullPathLen > 0 && fullPathLen < sizeof( fullPath ) ) {
		Sys_NormalizeDriveProbePath( fullPath );

		if ( ::GetVolumePathNameA( fullPath, volumePath, sizeof( volumePath ) ) ) {
			idStr::Copynz( resolvedPath, volumePath, resolvedPathCapacity );
			return true;
		}

		idStr::Copynz( resolvedPath, fullPath, resolvedPathCapacity );
		return true;
	}

	if ( isalpha( (unsigned char)normalizedPath[0] ) && normalizedPath[1] == ':' ) {
		idStr::snPrintf( resolvedPath, resolvedPathCapacity, "%c:\\", normalizedPath[0] );
		return true;
	}

	return false;
}

}
#include <conio.h>

#ifndef	ID_DEDICATED
#ifndef INT_MAX
#define INT_MAX 2147483647
#endif
#ifndef INT_MIN
#define INT_MIN (-2147483647 - 1)
#endif
#include <comdef.h>
#include <comutil.h>
#include <Wbemidl.h>

#pragma comment (lib, "wbemuuid.lib")
#endif

#include <stdint.h>

/*
================
Sys_Milliseconds
================
*/
int Sys_Milliseconds(void) {
	int sys_curtime;
	static int sys_timeBase;
	static bool	initialized = false;

	if (!initialized) {
		sys_timeBase = timeGetTime();
		initialized = true;
	}
	sys_curtime = timeGetTime() - sys_timeBase;

	return sys_curtime;
}

/*
========================
Sys_Microseconds
========================
*/
uint64_t Sys_Microseconds(void) {
	static uint64_t ticksPerMicrosecondTimes1024 = 0;

	if (ticksPerMicrosecondTimes1024 == 0) {
		ticksPerMicrosecondTimes1024 = ((uint64_t)Sys_ClockTicksPerSecond() << 10) / 1000000;
		assert(ticksPerMicrosecondTimes1024 > 0);
	}

	return ((uint64_t)((int64_t)Sys_GetClockTicks() << 10)) / ticksPerMicrosecondTimes1024;
}


/*
================
Sys_GetSystemRam

	returns amount of physical memory in MB
================
*/
int Sys_GetSystemRam(void) {
	MEMORYSTATUSEX statex;
	statex.dwLength = sizeof(statex);
	GlobalMemoryStatusEx(&statex);
	int physRam = statex.ullTotalPhys / (1024 * 1024);
	// HACK: For some reason, ullTotalPhys is sometimes off by a meg or two, so we round up to the nearest 16 megs
	physRam = (physRam + 8) & ~15;
	return physRam;
}


/*
================
Sys_GetDriveFreeSpace
returns in megabytes
================
*/
int Sys_GetDriveFreeSpace(const char* path) {
	DWORDLONG freeBytes;
	char probePath[MAX_OSPATH];

	if ( Sys_TryGetDriveFreeSpaceBytes( path, freeBytes ) ) {
		return (double)freeBytes / ( 1024.0 * 1024.0 );
	}

	if ( Sys_ResolveDriveProbePath( path, probePath, sizeof( probePath ) ) &&
			Sys_TryGetDriveFreeSpaceBytes( probePath, freeBytes ) ) {
		return (double)freeBytes / ( 1024.0 * 1024.0 );
	}

	return 26;
}


/*
================
Sys_GetVideoRam
returns in megabytes
================
*/
int Sys_GetVideoRam(void) {
	static int cachedVideoRam = 0;
	if ( cachedVideoRam > 0 ) {
		return cachedVideoRam;
	}

	cachedVideoRam = Sys_QueryDXGIVideoRamMB();
	if ( cachedVideoRam > 0 ) {
		return cachedVideoRam;
	}

	cachedVideoRam = OPENQ4_WIN32_FALLBACK_VIDEO_RAM_MB;
	return cachedVideoRam;
}

/*
================
Sys_GetDesktopResolution
================
*/
bool Sys_GetDesktopResolution( int *width, int *height ) {
	if ( width == NULL || height == NULL ) {
		return false;
	}

	int desktopWidth = win32.desktopWidth;
	int desktopHeight = win32.desktopHeight;

	if ( desktopWidth <= 0 || desktopHeight <= 0 ) {
		HDC hDC = GetDC( GetDesktopWindow() );
		if ( hDC == NULL ) {
			return false;
		}
		desktopWidth = GetDeviceCaps( hDC, HORZRES );
		desktopHeight = GetDeviceCaps( hDC, VERTRES );
		ReleaseDC( GetDesktopWindow(), hDC );
	}

	if ( desktopWidth <= 0 || desktopHeight <= 0 ) {
		return false;
	}

	win32.desktopWidth = desktopWidth;
	win32.desktopHeight = desktopHeight;
	*width = desktopWidth;
	*height = desktopHeight;
	return true;
}

/*
================
Sys_GetCurrentMemoryStatus

	returns OS mem info
	all values are in kB except the memoryload
================
*/
void Sys_GetCurrentMemoryStatus(sysMemoryStats_t& stats) {
	MEMORYSTATUSEX statex;
	unsigned __int64 work;

	memset(&statex, 0, sizeof(statex));
	statex.dwLength = sizeof(statex);
	GlobalMemoryStatusEx(&statex);

	memset(&stats, 0, sizeof(stats));

	stats.memoryLoad = statex.dwMemoryLoad;

	work = statex.ullTotalPhys >> 20;
	stats.totalPhysical = *(int*)&work;

	work = statex.ullAvailPhys >> 20;
	stats.availPhysical = *(int*)&work;

	work = statex.ullAvailPageFile >> 20;
	stats.availPageFile = *(int*)&work;

	work = statex.ullTotalPageFile >> 20;
	stats.totalPageFile = *(int*)&work;

	work = statex.ullTotalVirtual >> 20;
	stats.totalVirtual = *(int*)&work;

	work = statex.ullAvailVirtual >> 20;
	stats.availVirtual = *(int*)&work;

	work = statex.ullAvailExtendedVirtual >> 20;
	stats.availExtendedVirtual = *(int*)&work;
}

/*
================
Sys_GetProcessMemoryUsage
================
*/
bool Sys_GetProcessMemoryUsage( unsigned long long &privateBytes, unsigned long long &residentBytes ) {
	privateBytes = residentBytes = 0;
	// K32GetProcessMemoryInfo is exported by kernel32 since Windows 7; looking it up
	// keeps psapi.lib out of the link.
	typedef BOOL ( WINAPI *processMemoryInfo_t )( HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD );
	static const processMemoryInfo_t query = reinterpret_cast<processMemoryInfo_t>(
		reinterpret_cast<void *>( GetProcAddress( GetModuleHandleA( "kernel32.dll" ), "K32GetProcessMemoryInfo" ) ) );
	PROCESS_MEMORY_COUNTERS_EX counters;
	memset( &counters, 0, sizeof( counters ) );
	counters.cb = sizeof( counters );
	if ( query == NULL || !query( GetCurrentProcess(), reinterpret_cast<PPROCESS_MEMORY_COUNTERS>( &counters ), sizeof( counters ) ) ) {
		return false;
	}
	privateBytes = counters.PrivateUsage;
	residentBytes = counters.WorkingSetSize;
	return true;
}

/*
================
Sys_ReleaseFreeHeapMemory

The Windows heap decommits free memory itself; level loads level off without help.
================
*/
void Sys_ReleaseFreeHeapMemory(void) {
}

/*
================
Sys_LockMemory
================
*/
bool Sys_LockMemory(void* ptr, int bytes) {
	return (VirtualLock(ptr, (SIZE_T)bytes) != FALSE);
}

/*
================
Sys_UnlockMemory
================
*/
bool Sys_UnlockMemory(void* ptr, int bytes) {
	return (VirtualUnlock(ptr, (SIZE_T)bytes) != FALSE);
}

/*
================
Sys_SetPhysicalWorkMemory
================
*/
void Sys_SetPhysicalWorkMemory(int minBytes, int maxBytes) {
	::SetProcessWorkingSetSize(GetCurrentProcess(), minBytes, maxBytes);
}

/*
================
Sys_GetCurrentUser
================
*/
char* Sys_GetCurrentUser(void) {
	static char s_userName[1024];
	unsigned long size = sizeof(s_userName);


	if (!GetUserName(s_userName, &size)) {
		strcpy(s_userName, "player");
	}

	if (!s_userName[0]) {
		strcpy(s_userName, "player");
	}

	return s_userName;
}


/*
===============================================================================

	Call stack

===============================================================================
*/


#define PROLOGUE_SIGNATURE 0x00EC8B55

#include <dbghelp.h>

const int UNDECORATE_FLAGS = UNDNAME_NO_MS_KEYWORDS |
UNDNAME_NO_ACCESS_SPECIFIERS |
UNDNAME_NO_FUNCTION_RETURNS |
UNDNAME_NO_ALLOCATION_MODEL |
UNDNAME_NO_ALLOCATION_LANGUAGE |
UNDNAME_NO_MEMBER_TYPE;

static void Sym_FormatAddress( address_t addr, idStr &funcName ) {
	char addressString[ 32 ];
	idStr::snPrintf( addressString, sizeof( addressString ), "0x%llx", (unsigned long long)addr );
	funcName = addressString;
}

#if defined(_DEBUG) && 1

typedef struct symbol_s {
	address_t			address;
	char* name;
	struct symbol_s* next;
} symbol_t;

typedef struct module_s {
	address_t			address;
	char* name;
	symbol_t* symbols;
	struct module_s* next;
} module_t;

module_t* modules;

/*
==================
SkipRestOfLine
==================
*/
void SkipRestOfLine(const char** ptr) {
	while ((**ptr) != '\0' && (**ptr) != '\n' && (**ptr) != '\r') {
		(*ptr)++;
	}
	while ((**ptr) == '\n' || (**ptr) == '\r') {
		(*ptr)++;
	}
}

/*
==================
SkipWhiteSpace
==================
*/
void SkipWhiteSpace(const char** ptr) {
	while ((**ptr) == ' ') {
		(*ptr)++;
	}
}

/*
==================
ParseHexNumber
==================
*/
address_t ParseHexNumber(const char** ptr) {
	address_t n = 0;
	while (true) {
		const char c = **ptr;
		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
			n <<= 4;
		}
		else {
			break;
		}

		if (c >= '0' && c <= '9') {
			n |= (address_t)(c - '0');
		}
		else if (c >= 'a' && c <= 'f') {
			n |= (address_t)(10 + c - 'a');
		}
		else {
			n |= (address_t)(10 + c - 'A');
		}
		(*ptr)++;
	}
	return n;
}

/*
==================
Sym_Init
==================
*/
void Sym_Init(address_t addr) {
	TCHAR moduleName[MAX_STRING_CHARS];
	MEMORY_BASIC_INFORMATION mbi;

	VirtualQuery((void*)addr, &mbi, sizeof(mbi));

	GetModuleFileName((HMODULE)mbi.AllocationBase, moduleName, sizeof(moduleName));

	char* ext = moduleName + strlen(moduleName);
	while (ext > moduleName && *ext != '.') {
		ext--;
	}
	if (ext == moduleName) {
		strcat(moduleName, ".map");
	}
	else {
		strcpy(ext, ".map");
	}

	module_t* module = (module_t*)malloc(sizeof(module_t));
	module->name = (char*)malloc(strlen(moduleName) + 1);
	strcpy(module->name, moduleName);
	module->address = (address_t)mbi.AllocationBase;
	module->symbols = NULL;
	module->next = modules;
	modules = module;

	FILE* fp = fopen(moduleName, "rb");
	if (fp == NULL) {
		return;
	}

	int pos = ftell(fp);
	fseek(fp, 0, SEEK_END);
	int length = ftell(fp);
	fseek(fp, pos, SEEK_SET);

	char* text = (char*)malloc(length + 1);
	fread(text, 1, length, fp);
	text[length] = '\0';
	fclose(fp);

	const char* ptr = text;

	// skip up to " Address" on a new line
	while (*ptr != '\0') {
		SkipWhiteSpace(&ptr);
		if (idStr::Cmpn(ptr, "Address", 7) == 0) {
			SkipRestOfLine(&ptr);
			break;
		}
		SkipRestOfLine(&ptr);
	}

	address_t symbolAddress;
	int symbolLength;
	char symbolName[MAX_STRING_CHARS];
	symbol_t* symbol;

	// parse symbols
	while (*ptr != '\0') {

		SkipWhiteSpace(&ptr);

		ParseHexNumber(&ptr);
		if (*ptr == ':') {
			ptr++;
		}
		else {
			break;
		}
		ParseHexNumber(&ptr);

		SkipWhiteSpace(&ptr);

		// parse symbol name
		symbolLength = 0;
		while (*ptr != '\0' && *ptr != ' ') {
			symbolName[symbolLength++] = *ptr++;
			if (symbolLength >= sizeof(symbolName) - 1) {
				break;
			}
		}
		symbolName[symbolLength++] = '\0';

		SkipWhiteSpace(&ptr);

		// parse symbol address
		symbolAddress = ParseHexNumber(&ptr);

		SkipRestOfLine(&ptr);

		symbol = (symbol_t*)malloc(sizeof(symbol_t));
		symbol->name = (char*)malloc(symbolLength);
		strcpy(symbol->name, symbolName);
		symbol->address = symbolAddress;
		symbol->next = module->symbols;
		module->symbols = symbol;
	}

	free(text);
}

/*
==================
Sym_Shutdown
==================
*/
void Sym_Shutdown(void) {
	module_t* m;
	symbol_t* s;

	for (m = modules; m != NULL; m = modules) {
		modules = m->next;
		for (s = m->symbols; s != NULL; s = m->symbols) {
			m->symbols = s->next;
			free(s->name);
			free(s);
		}
		free(m->name);
		free(m);
	}
	modules = NULL;
}

/*
==================
Sym_GetFuncInfo
==================
*/
void Sym_GetFuncInfo(address_t addr, idStr& module, idStr& funcName) {
	MEMORY_BASIC_INFORMATION mbi;
	module_t* m;
	symbol_t* s;

	VirtualQuery((void*)addr, &mbi, sizeof(mbi));

	for (m = modules; m != NULL; m = m->next) {
		if (m->address == (address_t)mbi.AllocationBase) {
			break;
		}
	}
	if (!m) {
		Sym_Init(addr);
		m = modules;
	}

	for (s = m->symbols; s != NULL; s = s->next) {
		if (s->address == addr) {

			char undName[MAX_STRING_CHARS];
			if (UnDecorateSymbolName(s->name, undName, sizeof(undName), UNDECORATE_FLAGS)) {
				funcName = undName;
			}
			else {
				funcName = s->name;
			}
			for (int i = 0; i < funcName.Length(); i++) {
				if (funcName[i] == '(') {
					funcName.CapLength(i);
					break;
				}
			}
			module = m->name;
			return;
		}
	}

	Sym_FormatAddress(addr, funcName);
	module = "";
}

#elif defined(_DEBUG)

DWORD64 lastAllocationBase = 0;
HANDLE processHandle;
idStr lastModule;

/*
==================
Sym_Init
==================
*/
void Sym_Init(address_t addr) {
	TCHAR moduleName[MAX_STRING_CHARS];
	TCHAR modShortNameBuf[MAX_STRING_CHARS];
	MEMORY_BASIC_INFORMATION mbi;

	if (lastAllocationBase != 0) {
		Sym_Shutdown();
	}

	VirtualQuery((void*)addr, &mbi, sizeof(mbi));

	GetModuleFileName((HMODULE)mbi.AllocationBase, moduleName, sizeof(moduleName));
	_splitpath(moduleName, NULL, NULL, modShortNameBuf, NULL);
	lastModule = modShortNameBuf;

	processHandle = GetCurrentProcess();
	if (!SymInitialize(processHandle, NULL, FALSE)) {
		return;
	}
	DWORD64 moduleBase = (DWORD64)(uintptr_t)mbi.AllocationBase;
	if (!SymLoadModule64(processHandle, NULL, moduleName, NULL, moduleBase, 0)) {
		SymCleanup(processHandle);
		return;
	}

	SymSetOptions(SymGetOptions() & ~SYMOPT_UNDNAME);

	lastAllocationBase = moduleBase;
}

/*
==================
Sym_Shutdown
==================
*/
void Sym_Shutdown(void) {
	if (lastAllocationBase != 0) {
		SymUnloadModule64(GetCurrentProcess(), lastAllocationBase);
		SymCleanup(GetCurrentProcess());
		lastAllocationBase = 0;
	}
}

/*
==================
Sym_GetFuncInfo
==================
*/
void Sym_GetFuncInfo(address_t addr, idStr& module, idStr& funcName) {
	MEMORY_BASIC_INFORMATION mbi;

	VirtualQuery((void*)addr, &mbi, sizeof(mbi));

	DWORD64 moduleBase = (DWORD64)(uintptr_t)mbi.AllocationBase;
	if (moduleBase != lastAllocationBase) {
		Sym_Init(addr);
	}

	BYTE symbolBuffer[sizeof(IMAGEHLP_SYMBOL64) + MAX_STRING_CHARS];
	PIMAGEHLP_SYMBOL64 pSymbol = (PIMAGEHLP_SYMBOL64)&symbolBuffer[0];
	pSymbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
	pSymbol->MaxNameLength = 1023;
	pSymbol->Address = 0;
	pSymbol->Flags = 0;
	pSymbol->Size = 0;

	DWORD64 symDisplacement = 0;
	if (SymGetSymFromAddr64(processHandle, (DWORD64)addr, &symDisplacement, pSymbol)) {
		// clean up name, throwing away decorations that don't affect uniqueness
		char undName[MAX_STRING_CHARS];
		if (UnDecorateSymbolName(pSymbol->Name, undName, sizeof(undName), UNDECORATE_FLAGS)) {
			funcName = undName;
		}
		else {
			funcName = pSymbol->Name;
		}
		module = lastModule;
	}
	else {
		LPVOID lpMsgBuf;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			(LPTSTR)&lpMsgBuf,
			0,
			NULL
		);
		LocalFree(lpMsgBuf);

		// Couldn't retrieve symbol (no debug info?, can't load dbghelp.dll?)
		Sym_FormatAddress(addr, funcName);
		module = "";
	}
}

#else

/*
==================
Sym_Init
==================
*/
void Sym_Init(address_t addr) {
}

/*
==================
Sym_Shutdown
==================
*/
void Sym_Shutdown(void) {
}

/*
==================
Sym_GetFuncInfo
==================
*/
void Sym_GetFuncInfo(address_t addr, idStr& module, idStr& funcName) {
	module = "";
	Sym_FormatAddress(addr, funcName);
}

#endif

/*
==================
Sys_ShutdownSymbols
==================
*/
void Sys_ShutdownSymbols(void) {
	Sym_Shutdown();
}
