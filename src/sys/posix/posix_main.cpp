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
#include "../../idlib/precompiled.h"
#include "../sys_local.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/statvfs.h>
#include <pwd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <termios.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>

#if defined( USE_SDL3 )
#include <SDL3/SDL.h>
#endif

#if defined( __linux__ )
#include <sys/sysinfo.h>
#include <sys/random.h>
#endif

#if defined( MACOS_X )
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "posix_public.h"

#if !defined( MACOS_X ) || defined( USE_SDL3 )
#define OPENQ4_POSIX_OWNS_COMMON_SYS 1
#endif

#if defined( USE_SDL3 )
bool Sys_SDL_PumpEvents( void );
void IN_Frame( void );
#endif

#define					MAX_OSPATH PATH_MAX
#define					COMMAND_HISTORY 64

static int				input_hide = 0;

idEditField				input_field;
static char				input_ret[256];

static idStr			history[ COMMAND_HISTORY ];	// cycle buffer
static int				history_count = 0;			// buffer fill up
static int				history_start = 0;			// current history start
static int				history_current = 0;			// goes back in history
idEditField				history_backup;				// the base edit line

// terminal support
idCVar in_tty( "in_tty", "1", CVAR_BOOL | CVAR_INIT | CVAR_SYSTEM, "terminal tab-completion and history" );

static bool				tty_enabled = false;
static struct termios	tty_tc;

// pid - useful when you attach to gdb..
idCVar com_pid( "com_pid", "0", CVAR_INTEGER | CVAR_INIT | CVAR_SYSTEM, "process id" );
idCVar sys_allowMultipleInstances( "sys_allowMultipleInstances", "0", CVAR_SYSTEM | CVAR_BOOL, "allow multiple instances running concurrently" );

// exit - quit - error --------------------------------------------------------

static int set_exit = 0;
static const int POSIX_EXIT_SPAWN_SIZE = 4096;
static char exit_spawn[ POSIX_EXIT_SPAWN_SIZE ];
// Keep the descriptor open for the life of the process so the advisory lock stays held.
static int posix_instanceLockFd = -1;

static const char *Posix_InstanceLockPath( void ) {
	static char lockPath[ 1024 ];
	const char *runtimeDir = getenv( "XDG_RUNTIME_DIR" );

	if ( runtimeDir == NULL || runtimeDir[0] == '\0' ) {
		runtimeDir = getenv( "TMPDIR" );
	}
	if ( runtimeDir == NULL || runtimeDir[0] == '\0' ) {
		runtimeDir = "/tmp";
	}

	idStr::snPrintf( lockPath, sizeof( lockPath ), "%s/openq4-%u.lock", runtimeDir, static_cast<unsigned int>( geteuid() ) );
	return lockPath;
}

static void Posix_ReleaseInstanceLock( void ) {
	if ( posix_instanceLockFd != -1 ) {
		close( posix_instanceLockFd );
		posix_instanceLockFd = -1;
	}
}

static void Posix_WriteInstanceLockPid( const int fd ) {
	char pidBuffer[ 32 ];
	const int pidLength = idStr::snPrintf( pidBuffer, sizeof( pidBuffer ), "%ld\n", static_cast<long>( getpid() ) );

	if ( pidLength <= 0 ) {
		return;
	}
	if ( ftruncate( fd, 0 ) == -1 ) {
		return;
	}
	if ( lseek( fd, 0, SEEK_SET ) == -1 ) {
		return;
	}

	write( fd, pidBuffer, pidLength );
}

/*
================
Posix_Exit
================
*/
void Posix_Exit(int ret) {
#if defined( __ANDROID__ )
	// nothing below is guaranteed to print again, so do not let an
	// unterminated line leave with the process
	Sys_AndroidLogFlush();
#endif
	if ( !Posix_IsMainThread() ) {
		// A worker-thread fatal error exits through here. Joining the async
		// thread from itself would recurse through common->Error until the
		// stack overflows, and the SDL console teardown below is
		// main-thread-only; exit immediately instead.
		Sys_Printf( "Posix_Exit: called off the main thread; skipping shutdown teardown\n" );
		_exit( set_exit ? set_exit : ret );
	}
	if ( tty_enabled ) {
		Sys_Printf( "shutdown terminal support\n" );
		if ( tcsetattr( 0, TCSADRAIN, &tty_tc ) == -1 ) {
			Sys_Printf( "tcsetattr failed: %s\n", strerror( errno ) );
		}
	}
	// at this point, too late to catch signals
	Posix_ClearSigs();
	if ( asyncThread.threadHandle ) {
		Sys_DestroyThread( asyncThread );
	}
	Posix_ShutdownConsole();
	Posix_ReleaseInstanceLock();
	// process spawning. it's best when it happens after everything has shut down
	Posix_RunExitSpawn();
	// in case of signal, handler tries a common->Quit
	// we use set_exit to maintain a correct exit code
	if ( set_exit ) {
		exit( set_exit );
	}
	exit( ret );
}

/*
================
Posix_SetExit
================
*/
void Posix_SetExit(int ret) {
	set_exit = ret;
}

/*
===============
Posix_SetExitSpawn
set the process to be spawned when we quit
===============
*/
void Posix_SetExitSpawn( const char *exeName ) {
	if ( exeName == NULL || exeName[0] == '\0' ) {
		exit_spawn[0] = '\0';
		return;
	}
	idStr::Copynz( exit_spawn, exeName, sizeof( exit_spawn ) );
}

void Posix_RunExitSpawn( void ) {
	if ( exit_spawn[0] == '\0' ) {
		return;
	}

	char spawnCommand[ POSIX_EXIT_SPAWN_SIZE ];
	idStr::Copynz( spawnCommand, exit_spawn, sizeof( spawnCommand ) );
	exit_spawn[0] = '\0';
	Sys_DoStartProcess( spawnCommand, false );
}

/*
==================
idSysLocal::StartProcess
if !quit, start the process asap
otherwise, push it for execution at exit
(i.e. let complete shutdown of the game and freeing of resources happen)
NOTE: might even want to add a small delay?
==================
*/
void idSysLocal::StartProcess( const char *exeName, bool quit ) {
	if ( exeName == NULL || exeName[0] == '\0' ) {
		common->Printf( "Sys_StartProcess: empty command\n" );
		return;
	}

	if ( quit ) {
		common->DPrintf( "Sys_StartProcess %s (delaying until final exit)\n", exeName );
		Posix_SetExitSpawn( exeName );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
		return;
	}

	common->DPrintf( "Sys_StartProcess %s\n", exeName );
	Sys_DoStartProcess( exeName );
}

/*
================
Sys_Quit
================
*/
#if defined( OPENQ4_POSIX_OWNS_COMMON_SYS )
void Sys_Quit(void) {
	Posix_Exit( EXIT_SUCCESS );
}
#endif

/*
================
Sys_Milliseconds
================
*/
static uint64_t sys_timeBaseNs = 0;
static int sys_lastMilliseconds = 0;

int Sys_Milliseconds( void ) {
	struct timespec ts;

	if ( clock_gettime( CLOCK_MONOTONIC, &ts ) != 0 ) {
		return sys_lastMilliseconds;
	}

	const uint64_t nowNs = static_cast<uint64_t>( ts.tv_sec ) * 1000000000ull + static_cast<uint64_t>( ts.tv_nsec );

	if ( sys_timeBaseNs == 0 ) {
		sys_timeBaseNs = nowNs;
		sys_lastMilliseconds = 0;
		return sys_lastMilliseconds;
	}

	sys_lastMilliseconds = static_cast<int>( ( nowNs - sys_timeBaseNs ) / 1000000ull );
	return sys_lastMilliseconds;
}

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

#if defined( __ANDROID__ )
	// bionic only grew getrandom() at API 28; arc4random_buf has been there
	// since API 21 and is the same kernel entropy source.
	arc4random_buf( buffer, static_cast<size_t>( bytes ) );
	return true;
#elif defined( __linux__ )
	unsigned char *cursor = static_cast<unsigned char *>( buffer );
	int remaining = bytes;
	while ( remaining > 0 ) {
		const ssize_t received = getrandom( cursor, static_cast<size_t>( remaining ), 0 );
		if ( received > 0 ) {
			cursor += received;
			remaining -= static_cast<int>( received );
			continue;
		}
		if ( received < 0 && errno == EINTR ) {
			continue;
		}
		return false;
	}
	return true;
#elif defined( MACOS_X )
	arc4random_buf( buffer, static_cast<size_t>( bytes ) );
	return true;
#else
	int descriptor;
	do {
		descriptor = open( "/dev/urandom", O_RDONLY );
	} while ( descriptor < 0 && errno == EINTR );
	if ( descriptor < 0 ) {
		return false;
	}
	unsigned char *cursor = static_cast<unsigned char *>( buffer );
	int remaining = bytes;
	while ( remaining > 0 ) {
		const ssize_t received = read( descriptor, cursor,
			static_cast<size_t>( remaining ) );
		if ( received > 0 ) {
			cursor += received;
			remaining -= static_cast<int>( received );
			continue;
		}
		if ( received < 0 && errno == EINTR ) {
			continue;
		}
		close( descriptor );
		return false;
	}
	close( descriptor );
	return true;
#endif
}

/*
================
Sys_Mkdir
================
*/
void Sys_Mkdir( const char *path ) {
	if ( path == NULL || path[0] == '\0' ) {
		return;
	}
	if ( mkdir( path, 0777 ) == -1 && errno != EEXIST ) {
		common->DPrintf( "Sys_Mkdir: mkdir '%s' failed: %s\n", path, strerror( errno ) );
	}
}

/*
================
Sys_ListFiles
================
*/
int Sys_ListFiles( const char *directory, const char *extension, idStrList &list ) {
	struct dirent *d;
	DIR *fdir;
	bool dironly = false;
	struct stat st;
	bool debug;
	
	list.Clear();

	debug = cvarSystem->GetCVarBool( "fs_debug" );

	if ( directory == NULL || directory[0] == '\0' ) {
		if (debug) {
			common->Printf("Sys_ListFiles: empty directory\n");
		}
		return -1;
	}
	
	if (!extension)
		extension = "";
	
	// passing a slash as extension will find directories
	if (extension[0] == '/' && extension[1] == 0) {
		extension = "";
		dironly = true;
	}
	
	if ((fdir = opendir(directory)) == NULL) {
		if (debug) {
			common->Printf("Sys_ListFiles: opendir %s failed: %s\n", directory, strerror( errno ));
		}
		return -1;
	}
	
	while ((d = readdir(fdir)) != NULL) {
		// readdir() exposes navigation entries that FindFirstFile does not.
		// Never return them to recursive or case-recovery callers.
		if ( d->d_name[0] == '.' &&
			( d->d_name[1] == '\0' || ( d->d_name[1] == '.' && d->d_name[2] == '\0' ) ) ) {
			continue;
		}
		idStr search = directory;
		search.AppendPath( d->d_name );
		if (stat(search.c_str(), &st) == -1)
			continue;
		if (!dironly) {
			idStr look(search);
			idStr ext;
			look.ExtractFileExtension(ext);
			if (extension[0] != '\0' && ext.Icmp(&extension[1]) != 0) {
				continue;
			}
		}
		if ((dironly && !(st.st_mode & S_IFDIR)) ||
			(!dironly && (st.st_mode & S_IFDIR)))
			continue;

		list.Append(d->d_name);
	}

	closedir(fdir);
	
	if ( debug ) {
		common->Printf( "Sys_ListFiles: %d entries in %s\n", list.Num(), directory );
	}
	
	return list.Num();
}

/*
============================================================================
EVENT LOOP
============================================================================
*/

#include "../EventQueueContinuity.h"
#include "../EventDisposition.h"
#include "../EventRetirement.h"

#define	MAX_QUED_EVENTS		256
#define	MASK_QUED_EVENTS	( MAX_QUED_EVENTS - 1 )

static sysEvent_t eventQue[MAX_QUED_EVENTS];
static sysEventDispositionTag_t eventDispositionTags[MAX_QUED_EVENTS];
static std::uint64_t eventRetirementSerials[MAX_QUED_EVENTS];
static std::uint64_t eventRetirementHighwater = 0; // Never reset by clear/rebind.
static int eventHead, eventTail;

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
Posix_QueEvent

ptr should either be null, or point to a block of data that can be freed later
================
*/
void Posix_QueEvent( sysEventType_t type, int value, int value2,
				  int ptrLength, void *ptr ) {
	sysEvent_t *ev;

	ev = &eventQue[eventHead & MASK_QUED_EVENTS];
	if (eventHead - eventTail >= MAX_QUED_EVENTS) {
		Sys_InvalidateEventQueue();
		common->Printf( "Posix_QueEvent: overflow\n" );
		// we are discarding an event, but don't leak memory
		// TTimo: verbose dropped event types?
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

#if 0
	common->Printf( "Event %d: %d %d\n", ev->evType, ev->evValue, ev->evValue2 );
#endif
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
================
Sys_GetEvent
================
*/
sysEvent_t Sys_GetEvent(void) {
	static sysEvent_t ev;

	// return if we have data
	if (eventHead > eventTail) {
		if (!eventDispositionTags[eventTail & MASK_QUED_EVENTS].Empty()) {
			Sys_InvalidateEventQueue();
			return sysEvent_t{}; // Preserve original tagged ownership for explicit retirement.
		}
		eventTail++;
		return eventQue[(eventTail - 1) & MASK_QUED_EVENTS];
	}
	// return the empty event with the current time
	memset(&ev, 0, sizeof(ev));

	return ev;
}

/*
================
Sys_ClearEvents
================
*/
void Sys_ClearEvents( void ) {
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
Posix_Cwd
================
*/
const char *Posix_Cwd( void ) {
	static char cwd[MAX_OSPATH];

	if ( getcwd( cwd, sizeof( cwd ) - 1 ) == NULL ) {
		idStr::Copynz( cwd, ".", sizeof( cwd ) );
		return cwd;
	}
	cwd[MAX_OSPATH-1] = 0;

	return cwd;
}

/*
=================
Sys_GetMemoryStatus
=================
*/
void Sys_GetCurrentMemoryStatus( sysMemoryStats_t &stats );

void Sys_GetMemoryStatus( sysMemoryStats_t &stats ) {
	Sys_GetCurrentMemoryStatus( stats );
}

static int Sys_BytesToClampedMegabytes( const unsigned long long bytes ) {
	return static_cast<int>( Min( bytes >> 20, static_cast<unsigned long long>( idMath::INT_MAX ) ) );
}

static void Sys_SetMemoryStatsFromBytes(
	sysMemoryStats_t &stats,
	unsigned long long totalPhysical,
	unsigned long long availPhysical,
	unsigned long long totalPageFile,
	unsigned long long availPageFile ) {
	if ( totalPhysical > 0 && availPhysical > totalPhysical ) {
		availPhysical = totalPhysical;
	}
	if ( totalPageFile > 0 && availPageFile > totalPageFile ) {
		availPageFile = totalPageFile;
	}

	stats.totalPhysical = Sys_BytesToClampedMegabytes( totalPhysical );
	stats.availPhysical = Sys_BytesToClampedMegabytes( availPhysical );
	stats.totalPageFile = Sys_BytesToClampedMegabytes( totalPageFile );
	stats.availPageFile = Sys_BytesToClampedMegabytes( availPageFile );
	stats.totalVirtual = stats.totalPageFile;
	stats.availVirtual = stats.availPageFile;
	stats.availExtendedVirtual = 0;
	if ( totalPhysical > 0 ) {
		const unsigned long long availPercent = Min( ( availPhysical * 100ULL ) / totalPhysical, 100ULL );
		stats.memoryLoad = idMath::ClampInt( 0, 100, 100 - static_cast<int>( availPercent ) );
	}
}

void Sys_GetCurrentMemoryStatus( sysMemoryStats_t &stats ) {
	memset( &stats, 0, sizeof( stats ) );

#if defined( __linux__ )
	struct sysinfo info;
	if ( sysinfo( &info ) == -1 ) {
		common->DPrintf( "Sys_GetCurrentMemoryStatus: sysinfo failed: %s\n", strerror( errno ) );
		return;
	}

	const unsigned long long unit = info.mem_unit > 0 ? info.mem_unit : 1;
	const unsigned long long totalPhysical = static_cast<unsigned long long>( info.totalram ) * unit;
	const unsigned long long availPhysical =
		( static_cast<unsigned long long>( info.freeram ) + static_cast<unsigned long long>( info.bufferram ) ) * unit;
	const unsigned long long totalSwap = static_cast<unsigned long long>( info.totalswap ) * unit;
	const unsigned long long availSwap = static_cast<unsigned long long>( info.freeswap ) * unit;
	const unsigned long long totalVirtual = totalPhysical + totalSwap;
	const unsigned long long availVirtual = availPhysical + availSwap;

	Sys_SetMemoryStatsFromBytes( stats, totalPhysical, availPhysical, totalVirtual, availVirtual );
#elif defined( MACOS_X )
	unsigned long long totalPhysical = 0;
	size_t totalPhysicalSize = sizeof( totalPhysical );
	if ( sysctlbyname( "hw.memsize", &totalPhysical, &totalPhysicalSize, NULL, 0 ) == -1 || totalPhysical == 0 ) {
		common->DPrintf( "Sys_GetCurrentMemoryStatus: hw.memsize failed: %s\n", strerror( errno ) );
		return;
	}

	mach_port_t hostPort = mach_host_self();
	vm_size_t pageSize = 0;
	vm_statistics64_data_t vmStats;
	mach_msg_type_number_t vmStatsCount = HOST_VM_INFO64_COUNT;

	if ( host_page_size( hostPort, &pageSize ) != KERN_SUCCESS ||
		 host_statistics64( hostPort, HOST_VM_INFO64, reinterpret_cast<host_info64_t>( &vmStats ), &vmStatsCount ) != KERN_SUCCESS ||
		 pageSize == 0 ) {
		mach_port_deallocate( mach_task_self(), hostPort );
		Sys_SetMemoryStatsFromBytes( stats, totalPhysical, 0, totalPhysical, 0 );
		return;
	}

	const unsigned long long availablePages =
		static_cast<unsigned long long>( vmStats.free_count ) +
		static_cast<unsigned long long>( vmStats.inactive_count ) +
		static_cast<unsigned long long>( vmStats.speculative_count );
	const unsigned long long availPhysical = availablePages * static_cast<unsigned long long>( pageSize );

	mach_port_deallocate( mach_task_self(), hostPort );
	Sys_SetMemoryStatsFromBytes( stats, totalPhysical, availPhysical, totalPhysical, availPhysical );
#endif
}

void Sys_GetExeLaunchMemoryStatus( sysMemoryStats_t &stats ) {
	Sys_GetCurrentMemoryStatus( stats );
}

/*
=================
Sys_Init
Posix_EarlyInit/Posix_LateInit is better
=================
*/
#if defined( OPENQ4_POSIX_OWNS_COMMON_SYS )
void Sys_Init( void ) { }
#endif

/*
=================
Sys_Shutdown
=================
*/
#if defined( MACOS_X ) && defined( USE_SDL3 )
void Sys_Shutdown( void ) {
	Posix_Shutdown();
}
#endif

/*
=================
Posix_Shutdown
=================
*/
void Posix_Shutdown( void ) {
	Posix_ShutdownConsole();
	Posix_ReleaseInstanceLock();
	for ( int i = 0; i < COMMAND_HISTORY; i++ ) {
		history[ i ].Clear();
	}
}

/*
=================
Sys_DLL_Load
TODO: OSX - use the native API instead? NSModule
=================
*/
intptr_t Sys_DLL_Load( const char *path ) {
	if ( path == NULL || path[0] == '\0' ) {
		Sys_Printf( "dlopen failed: empty path\n" );
		return 0;
	}
#if defined( MACOS_X )
	char resolvedPath[PATH_MAX];
	if ( realpath( path, resolvedPath ) == NULL ) {
		Sys_Printf( "dlopen '%s' failed: %s\n", path, strerror( errno ) );
		return 0;
	}
	const char *loadPath = resolvedPath;
#else
	const char *loadPath = path;
#endif
	dlerror();
	void *handle = dlopen( loadPath, RTLD_NOW | RTLD_LOCAL );
	if ( !handle ) {
		const char *error = dlerror();
		Sys_Printf( "dlopen '%s' failed: %s\n", loadPath, error ? error : "unknown error" );
	}
	return (intptr_t)handle;
}

/*
=================
Sys_DLL_GetProcAddress
=================
*/
void* Sys_DLL_GetProcAddress( intptr_t handle, const char *sym ) {
	const char *error;
	if ( handle == 0 || sym == NULL || sym[0] == '\0' ) {
		Sys_Printf( "dlsym failed: invalid handle or symbol\n" );
		return NULL;
	}
	dlerror();
	void *ret = dlsym( (void *)handle, sym );
	if ((error = dlerror()) != NULL)  {
		Sys_Printf( "dlsym '%s' failed: %s\n", sym, error );
	}
	return ret;
}

/*
=================
Sys_DLL_Unload
=================
*/
void Sys_DLL_Unload( intptr_t handle ) {
	if ( handle == 0 ) {
		return;
	}
	dlclose( (void *)handle );
}

// ---------------------------------------------------------------------------

#if !defined( MACOS_X )
// Only relevant when specified on the command line. macOS provides a
// bundle-aware implementation in macosx_compat.mm so Finder launches can use
// the adjacent package root even when their process working directory differs.
const char *Sys_DefaultCDPath( void ) {
#if defined( __ANDROID__ )
	// The standalone host extracts packaged overlays into private storage.
	// Keep the retail install, writable saves and APK native modules separate.
	const char *contentRoot = getenv( "OPENQ4_CONTENT_ROOT" );
	if ( contentRoot != NULL && contentRoot[0] == '/' ) {
		return contentRoot;
	}
#endif
	return Posix_Cwd();
}
#endif

ID_TIME_T Sys_FileTimeStamp(FILE * fp) {
	if ( fp == NULL ) {
		return -1;
	}
	struct stat st;
	if ( fstat(fileno(fp), &st) == -1 ) {
		return -1;
	}
	return st.st_mtime;
}

#if defined( OPENQ4_POSIX_OWNS_COMMON_SYS )
void Sys_Sleep(int msec) {
	if ( msec <= 0 ) {
		return;
	}

	struct timespec requested;
	requested.tv_sec = msec / 1000;
	requested.tv_nsec = ( msec % 1000 ) * 1000000L;
	while ( nanosleep( &requested, &requested ) == -1 ) {
		if ( errno != EINTR ) {
			Sys_Printf( "nanosleep: %s\n", strerror( errno ) );
			return;
		}
	}
}
#endif

#if defined( OPENQ4_POSIX_OWNS_COMMON_SYS )
char *Sys_GetClipboardData(void) {
#if defined( USE_SDL3 )
	if ( !Posix_IsMainThread() ) {
		Sys_Printf( "Skipping SDL clipboard read from a non-main thread.\n" );
		return NULL;
	}
	char *clipboardText = SDL_GetClipboardText();
	if ( clipboardText == NULL || clipboardText[0] == '\0' ) {
		if ( clipboardText != NULL ) {
			SDL_free( clipboardText );
		}
		return NULL;
	}

	const size_t clipboardLength = strlen( clipboardText );
	if ( clipboardLength > static_cast<size_t>( idMath::INT_MAX - 1 ) ) {
		SDL_free( clipboardText );
		return NULL;
	}

	char *data = static_cast<char *>( Mem_Alloc( static_cast<int>( clipboardLength ) + 1 ) );
	if ( data == NULL ) {
		SDL_free( clipboardText );
		return NULL;
	}
	memcpy( data, clipboardText, clipboardLength + 1 );
	SDL_free( clipboardText );

	strtok( data, "\n\r\b" );
	return data;
#else
	Sys_Printf( "TODO: Sys_GetClipboardData\n" );
	return NULL;
#endif
}
#endif

#if defined( OPENQ4_POSIX_OWNS_COMMON_SYS )
void Sys_SetClipboardData( const char *string ) {
#if defined( USE_SDL3 )
	if ( !Posix_IsMainThread() ) {
		Sys_Printf( "Skipping SDL clipboard write from a non-main thread.\n" );
		return;
	}
	if ( !SDL_SetClipboardText( string != NULL ? string : "" ) ) {
		Sys_Printf( "SDL_SetClipboardText failed: %s\n", SDL_GetError() );
	}
#else
	Sys_Printf( "TODO: Sys_SetClipboardData\n" );
#endif
}
#endif
	

// stub pretty much everywhere - heavy calling
void Sys_FlushCacheMemory(void *base, int bytes)
{
//  Sys_Printf("Sys_FlushCacheMemory stub\n");
}

bool Sys_FPU_StackIsEmpty( void ) {
	return true;
}

void Sys_FPU_ClearStack( void ) {
}

const char *Sys_FPU_GetState( void ) {
	return "";
}

void Sys_FPU_SetPrecision( int precision ) {
}

/*
================
Sys_LockMemory
================
*/
bool Sys_LockMemory( void *ptr, int bytes ) {
	if ( ptr == NULL || bytes <= 0 ) {
		return false;
	}
	return mlock( ptr, static_cast<size_t>( bytes ) ) == 0;
}

/*
================
Sys_UnlockMemory
================
*/
bool Sys_UnlockMemory( void *ptr, int bytes ) {
	if ( ptr == NULL || bytes <= 0 ) {
		return false;
	}
	return munlock( ptr, static_cast<size_t>( bytes ) ) == 0;
}

/*
================
Sys_SetPhysicalWorkMemory
================
*/
void Sys_SetPhysicalWorkMemory( int minBytes, int maxBytes ) {
	common->DPrintf( "TODO: Sys_SetPhysicalWorkMemory\n" );
}

/*
===========
Sys_GetDriveFreeSpace
return in MegaBytes
===========
*/
int Sys_GetDriveFreeSpace( const char *path ) {
	char probePath[PATH_MAX];
	struct statvfs fsStats;

	if ( path == NULL || path[0] == '\0' ) {
		path = Posix_Cwd();
	}

	idStr::Copynz( probePath, path, sizeof( probePath ) );
	while ( probePath[0] != '\0' ) {
		if ( statvfs( probePath, &fsStats ) == 0 ) {
			const unsigned long long blockSize = fsStats.f_frsize != 0 ? fsStats.f_frsize : fsStats.f_bsize;
			const unsigned long long freeBytes = static_cast<unsigned long long>( fsStats.f_bavail ) * blockSize;
			return Sys_BytesToClampedMegabytes( freeBytes );
		}

		size_t length = strlen( probePath );
		while ( length > 1 && probePath[length - 1] == '/' ) {
			probePath[--length] = '\0';
		}
		char *lastSlash = strrchr( probePath, '/' );
		if ( lastSlash == NULL ) {
			break;
		}
		if ( lastSlash == probePath ) {
			probePath[1] = '\0';
			if ( statvfs( probePath, &fsStats ) == 0 ) {
				const unsigned long long blockSize = fsStats.f_frsize != 0 ? fsStats.f_frsize : fsStats.f_bsize;
				const unsigned long long freeBytes = static_cast<unsigned long long>( fsStats.f_bavail ) * blockSize;
				return Sys_BytesToClampedMegabytes( freeBytes );
			}
			break;
		}
		*lastSlash = '\0';
	}

	common->DPrintf( "Sys_GetDriveFreeSpace: statvfs failed for '%s': %s\n", path, strerror( errno ) );
	return 26;
}

/*
================
Sys_AlreadyRunning
return true if there is a copy of openQ4 running already
================
*/
bool Sys_AlreadyRunning( void ) {
#ifndef DEBUG
	if ( sys_allowMultipleInstances.GetBool() || posix_instanceLockFd != -1 ) {
		return false;
	}

	const char *lockPath = Posix_InstanceLockPath();
	int openFlags = O_RDWR | O_CREAT;
	struct flock lock;

#ifdef O_CLOEXEC
	openFlags |= O_CLOEXEC;
#endif

	posix_instanceLockFd = open( lockPath, openFlags, 0600 );
	if ( posix_instanceLockFd == -1 ) {
		common->Printf( "WARNING: failed to open instance lock '%s': %s\n", lockPath, strerror( errno ) );
		return false;
	}

	memset( &lock, 0, sizeof( lock ) );
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	if ( fcntl( posix_instanceLockFd, F_SETLK, &lock ) == -1 ) {
		const int lockError = errno;

		Posix_ReleaseInstanceLock();
		if ( lockError == EACCES || lockError == EAGAIN ) {
			common->Printf( "another openQ4 instance is already running\n" );
			return true;
		}

		common->Printf( "WARNING: failed to lock instance file '%s': %s\n", lockPath, strerror( lockError ) );
		return false;
	}

	Posix_WriteInstanceLockPid( posix_instanceLockFd );
#endif
	return false;
}

static pthread_t posix_mainThread;
static bool posix_mainThreadRecorded = false;

/*
===============
Posix_IsMainThread

Cocoa (and therefore SDL3 video on macOS) only tolerates window creation and
event pumping on the process main thread; callers use this to keep those
paths off worker threads such as the async timer thread.
===============
*/
bool Posix_IsMainThread( void ) {
	if ( !posix_mainThreadRecorded ) {
		// Before Posix_EarlyInit records the identity, only the main thread
		// can be running engine code.
		return true;
	}
	return pthread_equal( pthread_self(), posix_mainThread ) != 0;
}

/*
===============
Posix_EarlyInit
===============
*/
void Posix_EarlyInit( void ) {
	posix_mainThread = pthread_self();
	posix_mainThreadRecorded = true;
	memset( &asyncThread, 0, sizeof( asyncThread ) );
	exit_spawn[0] = '\0';
	Posix_InitSigs();
	// set the base time
	Sys_Milliseconds();
	Posix_InitPThreads();
}

/*
===============
Posix_LateInit
===============
*/
void Posix_LateInit( void ) {
	Posix_InitConsoleInput();
	Posix_InitFatalBreadcrumbPath();
	com_pid.SetInteger( getpid() );
	common->Printf( "pid: %d\n", com_pid.GetInteger() );
#ifdef __linux__
	common->Printf( "CPU: %s\n", Sys_GetProcessorString() );
#endif
	common->Printf( "System memory: %s\n", Sys_FormatMemoryMB( Sys_GetSystemRam() ).c_str() );
#ifndef ID_DEDICATED
	common->Printf( "Video memory: %s\n", Sys_FormatMemoryMB( Sys_GetVideoRam() ).c_str() );
#endif
	Posix_ConsoleLateInit();
	Posix_StartAsyncThread( );
}

/*
===============
Posix_InitConsoleInput
===============
*/
void Posix_InitConsoleInput( void ) {
	struct termios tc;

	if ( in_tty.GetBool() ) {
		if ( isatty( STDIN_FILENO ) != 1 ) {
			Sys_Printf( "terminal support disabled: stdin is not a tty\n" );
			in_tty.SetBool( false );
			return;
		}
		if ( tcgetattr( 0, &tty_tc ) == -1 ) {
			Sys_Printf( "tcgetattr failed. disabling terminal support: %s\n", strerror( errno ) );
			in_tty.SetBool( false );
			return;
		}
		// make the input non blocking
		if ( fcntl( STDIN_FILENO, F_SETFL, fcntl( STDIN_FILENO, F_GETFL, 0 ) | O_NONBLOCK ) == -1 ) {
			Sys_Printf( "fcntl STDIN non blocking failed.  disabling terminal support: %s\n", strerror( errno ) );
			in_tty.SetBool( false );
			return;
		}
		tc = tty_tc;
		/*
		  ECHO: don't echo input characters
		  ICANON: enable canonical mode.  This  enables  the  special
		  	characters  EOF,  EOL,  EOL2, ERASE, KILL, REPRINT,
		  	STATUS, and WERASE, and buffers by lines.
		  ISIG: when any of the characters  INTR,  QUIT,  SUSP,  or
		  	DSUSP are received, generate the corresponding signal
		*/              
		tc.c_lflag &= ~(ECHO | ICANON);
		/*
		  ISTRIP strip off bit 8
		  INPCK enable input parity checking
		*/
		tc.c_iflag &= ~(ISTRIP | INPCK);
		tc.c_cc[VMIN] = 1;
		tc.c_cc[VTIME] = 0;
		if ( tcsetattr( 0, TCSADRAIN, &tc ) == -1 ) {
			Sys_Printf( "tcsetattr failed: %s\n", strerror( errno ) );
			Sys_Printf( "terminal support may not work correctly. Use +set in_tty 0 to disable it\n" );
		}
#if 0
		// make the output non blocking
		if ( fcntl( STDOUT_FILENO, F_SETFL, fcntl( STDOUT_FILENO, F_GETFL, 0 ) | O_NONBLOCK ) == -1 ) {
			Sys_Printf( "fcntl STDOUT non blocking failed: %s\n", strerror( errno ) );
		}
#endif
		tty_enabled = true;
		// check the terminal type for the supported ones
		char *term = getenv( "TERM" );
		if ( term ) {
			if ( strcmp( term, "linux" ) && strcmp( term, "xterm" ) && strcmp( term, "xterm-color" ) && strcmp( term, "screen" ) ) {
				Sys_Printf( "WARNING: terminal type '%s' is unknown. terminal support may not work correctly\n", term );
			}
		}
		Sys_Printf( "terminal support enabled ( use +set in_tty 0 to disabled )\n" );
	} else {
		Sys_Printf( "terminal support disabled\n" );
	}
}

/*
================
terminal support utilities
================
*/

void tty_Del() {
	char key;
	key = '\b';
	write( STDOUT_FILENO, &key, 1 );
	key = ' ';
	write( STDOUT_FILENO, &key, 1 );
	key = '\b';
	write( STDOUT_FILENO, &key, 1 );
}

void tty_Left() {
	char key = '\b';
	write( STDOUT_FILENO, &key, 1 );
}

void tty_Right() {
	char key = 27;
	write( STDOUT_FILENO, &key, 1 );
	write( STDOUT_FILENO, "[C", 2 );
}

static const char *tty_InputState( int &inputLength, int &inputCursor ) {
	const char *buffer = input_field.GetBuffer();
	if ( buffer == NULL ) {
		buffer = "";
	}
	const size_t rawLength = strlen( buffer );
	inputLength = rawLength > static_cast<size_t>( idMath::INT_MAX ) ? idMath::INT_MAX : static_cast<int>( rawLength );
	inputCursor = idMath::ClampInt( 0, inputLength, input_field.GetCursor() );
	return buffer;
}

// clear the display of the line currently edited
// bring cursor back to beginning of line
void tty_Hide() {
	int len, buf_len;
	int cursor;
	if ( !tty_enabled ) {
		return;
	}
	if ( input_hide ) {
		input_hide++;
		return;
	}
	(void)tty_InputState( buf_len, cursor );
	// clear after cursor
	len = buf_len - cursor;
	while ( len > 0 ) {
		tty_Right();
		len--;
	}
	while ( buf_len > 0 ) {
		tty_Del();
		buf_len--;
	}
	input_hide++;
}

// show the current line
void tty_Show() {
	//	int i;
	if ( !tty_enabled ) {
		return;
	}
	assert( input_hide > 0 );
	input_hide--;
	if ( input_hide == 0 ) {
		int bufferLength = 0;
		int cursor = 0;
		const char *buf = tty_InputState( bufferLength, cursor );
		if ( buf[0] ) {
			write( STDOUT_FILENO, buf, bufferLength );
			int back = bufferLength - cursor;
			while ( back > 0 ) {
				tty_Left();
				back--;
			}
		}
	}
}

void tty_FlushIn() {
  char key;
  while ( read(0, &key, 1) != -1 ) {
	  Sys_Printf( "'%d' ", key );
  }
  Sys_Printf( "\n" );
}

/*
================
Posix_ConsoleInput
Checks for a complete line of text typed in at the console.
Return NULL if a complete line is not ready.
================
*/
char *Posix_ConsoleInput( void ) {
	if ( tty_enabled ) {
		int		ret;
		char	key;
		bool	hidden = false;
		while ( ( ret = read( STDIN_FILENO, &key, 1 ) ) > 0 ) {
			if ( !hidden ) {
				tty_Hide();
				hidden = true;
			}
			switch ( key ) {
			case 1:
				input_field.SetCursor( 0 );
				break;
			case 5:
				{
					int inputLength = 0;
					int inputCursor = 0;
					(void)tty_InputState( inputLength, inputCursor );
					input_field.SetCursor( inputLength );
				}
				break;
			case 127:
			case 8:
				input_field.CharEvent( K_BACKSPACE );
				break;
			case '\n':
				{
					int inputLength = 0;
					int inputCursor = 0;
					const char *inputBuffer = tty_InputState( inputLength, inputCursor );
					idStr::Copynz( input_ret, inputBuffer, sizeof( input_ret ) );
				}
				{
					const bool privateCommand = cvarSystem != NULL && cvarSystem->IsInitialized() &&
						cvarSystem->CommandContainsPrivateCVar( input_ret );
					assert( hidden );
					tty_Show();
					write( STDOUT_FILENO, &key, 1 );
					input_field.Clear();
					if ( !privateCommand ) {
						if ( history_count < COMMAND_HISTORY ) {
							history[ history_count ] = input_ret;
							history_count++;
						} else {
							history[ history_start ] = input_ret;
							history_start++;
							history_start %= COMMAND_HISTORY;
						}
					}
					history_current = 0;
					return input_ret;
				}
			case '\t':
				input_field.AutoComplete();
				break;
			case 27: {
				// enter escape sequence mode
				ret = read( STDIN_FILENO, &key, 1 );
				if ( ret <= 0 ) {
					Sys_Printf( "dropping sequence: '27' " );
					tty_FlushIn();
					assert( hidden );
					tty_Show();
					return NULL;
				}
				switch ( key ) {
				case 79:
					ret = read( STDIN_FILENO, &key, 1 );
					if ( ret <= 0 ) {
						Sys_Printf( "dropping sequence: '27' '79' " );
						tty_FlushIn();
						assert( hidden );
						tty_Show();
						return NULL;
					}
					switch ( key ) {
					case 72:
						// xterm only
						input_field.SetCursor( 0 );
						break;
					case 70:
						// xterm only
						{
							int inputLength = 0;
							int inputCursor = 0;
							(void)tty_InputState( inputLength, inputCursor );
							input_field.SetCursor( inputLength );
						}
						break;
					default:
						Sys_Printf( "dropping sequence: '27' '79' '%d' ", key );
						tty_FlushIn();
						assert( hidden );
						tty_Show();
						return NULL;
					}
					break;
				case 91: {
					ret = read( STDIN_FILENO, &key, 1 );
					if ( ret <= 0 ) {
						Sys_Printf( "dropping sequence: '27' '91' " );
						tty_FlushIn();
						assert( hidden );
						tty_Show();
						return NULL;
					}
					switch ( key ) {
					case 49: {
						ret = read( STDIN_FILENO, &key, 1 );
						if ( ret <= 0 || key != 126 ) {
							Sys_Printf( "dropping sequence: '27' '91' '49' '%d' ", key );
							tty_FlushIn();
							assert( hidden );
							tty_Show();
							return NULL;
						}
						// only screen and linux terms
						input_field.SetCursor( 0 );
						break;
					}
					case 50: {
						ret = read( STDIN_FILENO, &key, 1 );
						if ( ret <= 0 || key != 126 ) {
							Sys_Printf( "dropping sequence: '27' '91' '50' '%d' ", key );
							tty_FlushIn();
							assert( hidden );
							tty_Show();
							return NULL;
						}
						// all terms
						input_field.KeyDownEvent( K_INS );
						break;						
					}
					case 52: {
						ret = read( STDIN_FILENO, &key, 1 );
						if ( ret <= 0 || key != 126 ) {
							Sys_Printf( "dropping sequence: '27' '91' '52' '%d' ", key );
							tty_FlushIn();
							assert( hidden );
							tty_Show();
							return NULL;
						}
						// only screen and linux terms
						{
							int inputLength = 0;
							int inputCursor = 0;
							(void)tty_InputState( inputLength, inputCursor );
							input_field.SetCursor( inputLength );
						}
						break;
					}
					case 51: {
						ret = read( STDIN_FILENO, &key, 1 );
						if ( ret <= 0 ) {
							Sys_Printf( "dropping sequence: '27' '91' '51' " );
							tty_FlushIn();
							assert( hidden );
							tty_Show();
							return NULL;
						}
						if ( key == 126 ) {
							input_field.KeyDownEvent( K_DEL );
							break;
						}
						Sys_Printf( "dropping sequence: '27' '91' '51' '%d'", key );
						tty_FlushIn();
						assert( hidden );
						tty_Show();
						return NULL;						
					}
					case 65:
					case 66: {
						// history
						if ( history_current == 0 ) {
							history_backup = input_field;
						}
						if ( key == 65 ) {
							// up
							history_current++;
						} else {
							// down
							history_current--;
						}
						// history_current cycle:
						// 0: current edit
						// 1 .. Min( COMMAND_HISTORY, history_count ): back in history
						if ( history_current < 0 ) {
							history_current = Min( COMMAND_HISTORY, history_count );
						} else {
							history_current %= Min( COMMAND_HISTORY, history_count ) + 1;
						}
						int index = -1;
						if ( history_current == 0 ) {
							input_field = history_backup;
						} else {									
							index = history_start + Min( COMMAND_HISTORY, history_count ) - history_current;
							index %= COMMAND_HISTORY;
							assert( index >= 0 && index < COMMAND_HISTORY );
							input_field.SetBuffer( history[ index ] );
						}
						assert( hidden );
						tty_Show();
						return NULL;
					}
					case 67:
						input_field.KeyDownEvent( K_RIGHTARROW );
						break;
					case 68:
						input_field.KeyDownEvent( K_LEFTARROW );
						break;
					default:
						Sys_Printf( "dropping sequence: '27' '91' '%d' ", key );
						tty_FlushIn();
						assert( hidden );
						tty_Show();
						return NULL;
					}
					break;
				}
				default:
					Sys_Printf( "dropping sequence: '27' '%d' ", key );
					tty_FlushIn();
					assert( hidden );
					tty_Show();
					return NULL;
				}
				break;
			}
			default:
				if ( key >= ' ' ) {
					input_field.CharEvent( key );
					break;
				}
				Sys_Printf( "dropping sequence: '%d' ", key );
				tty_FlushIn();
				assert( hidden );
				tty_Show();
				return NULL;
			}
		}
		if ( hidden ) {
			tty_Show();
		}
		return NULL;
	} else {
		// disabled on OSX. works fine from a terminal, but launching from Finder is causing trouble
		// I'm pretty sure it could be re-enabled if needed, and just handling the Finder failure case right (TTimo)
#ifndef MACOS_X
		// no terminal support - read only complete lines
		int				len;
		fd_set			fdset;
		struct timeval	timeout;

		FD_ZERO( &fdset );
		FD_SET( STDIN_FILENO, &fdset );
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		if ( select( 1, &fdset, NULL, NULL, &timeout ) == -1 || !FD_ISSET( 0, &fdset ) ) {
			return NULL;
		}

		len = read( 0, input_ret, sizeof( input_ret ) );
		if ( len == 0 ) {
			// EOF
			return NULL;
		}

		if ( len < 1 ) {
			Sys_Printf( "read failed: %s\n", strerror( errno ) );	// something bad happened, cancel this line and print an error
			return NULL;
		}

		if ( len == sizeof( input_ret ) ) {
			Sys_Printf( "read overflow\n" );	// things are likely to break, as input will be cut into pieces
		}

		input_ret[ len-1 ] = '\0';		// rip off the \n and terminate
		return input_ret;
#endif
	}
	return NULL;
}

/*
called during frame loops, pacifier updates etc.
this polls console input and keeps SDL window/input state current
*/
void Sys_GenerateEvents( void ) {
	char *s;

#if defined( USE_SDL3 )
	(void)Sys_SDL_PumpEvents();
	IN_Frame();
#endif
	Posix_ConsoleFrame();

	if ( ( s = Posix_ConsoleInput() ) ) {
		char *b;
		size_t commandLength;
		int len;

		commandLength = strlen( s );
		if ( commandLength > idMath::INT_MAX - 1 ) {
			common->Printf( "Sys_GenerateEvents: console input is too long\n" );
			return;
		}
		len = (int)commandLength + 1;
		b = (char *)Mem_Alloc( len );
		if ( b == NULL ) {
			return;
		}
		idStr::Copynz( b, s, len );
		if ( cvarSystem != NULL && cvarSystem->IsInitialized() &&
			cvarSystem->CommandContainsPrivateCVar( s ) ) {
			memset( s, 0, len );
		}
		Posix_QueEvent( SE_CONSOLE, 0, 0, len, b );
	}
}

/*
===============
low level output
===============
*/

#define MAX_POSIX_PRINT_MSG 4096

void Sys_DebugPrintf( const char *fmt, ... ) {
	va_list argptr;

	if ( fmt == NULL ) {
		return;
	}
#if defined( __ANDROID__ )
	char text[MAX_POSIX_PRINT_MSG];
	va_start( argptr, fmt );
	idStr::vsnPrintf( text, sizeof( text ) - 1, fmt, argptr );
	va_end( argptr );
	text[sizeof( text ) - 1] = '\0';
	Sys_AndroidLogPrint( SYS_ANDROID_LOG_DEBUG, text );
#else
	tty_Hide();
	va_start( argptr, fmt );
	vprintf( fmt, argptr );
	va_end( argptr );
	tty_Show();
#endif
}

void Sys_DebugVPrintf( const char *fmt, va_list arg ) {
	if ( fmt == NULL ) {
		return;
	}
#if defined( __ANDROID__ )
	char text[MAX_POSIX_PRINT_MSG];
	idStr::vsnPrintf( text, sizeof( text ) - 1, fmt, arg );
	text[sizeof( text ) - 1] = '\0';
	Sys_AndroidLogPrint( SYS_ANDROID_LOG_DEBUG, text );
#else
	tty_Hide();
	vprintf( fmt, arg );
	tty_Show();
#endif
}

void Sys_Printf(const char *msg, ...) {
	char text[MAX_POSIX_PRINT_MSG];
	va_list argptr;

	if ( msg == NULL ) {
		msg = "";
	}
	va_start( argptr, msg );
	idStr::vsnPrintf( text, sizeof( text ) - 1, msg, argptr );
	va_end( argptr );
	text[sizeof( text ) - 1] = '\0';

	Posix_ConsoleAppendText( text );

#if defined( __ANDROID__ )
	// There is no terminal here, and stdout only reaches logcat through the
	// host app's pipe pump, which cannot preserve line boundaries. Go straight
	// to liblog instead - see sys/android/android_log.cpp.
	Sys_AndroidLogPrint( SYS_ANDROID_LOG_INFO, text );
#else
	tty_Hide();
	fputs( text, stdout );
	tty_Show();
#endif
}

void Sys_VPrintf(const char *msg, va_list arg) {
	char text[MAX_POSIX_PRINT_MSG];

	if ( msg == NULL ) {
		msg = "";
	}
	idStr::vsnPrintf( text, sizeof( text ) - 1, msg, arg );
	text[sizeof( text ) - 1] = '\0';

	Posix_ConsoleAppendText( text );

#if defined( __ANDROID__ )
	Sys_AndroidLogPrint( SYS_ANDROID_LOG_INFO, text );
#else
	tty_Hide();
	fputs( text, stdout );
	tty_Show();
#endif
}

static char posix_fatalBreadcrumbPath[ MAX_OSPATH ];

/*
================
Posix_InitFatalBreadcrumbPath

Resolves and caches <savepath>/<gamedir>/logs/fatal.txt (creating the
directories) so the fatal paths — including async-signal contexts that must
not call into path resolution — only need open/write/close afterwards.
================
*/
void Posix_InitFatalBreadcrumbPath( void ) {
	if ( posix_fatalBreadcrumbPath[0] != '\0' ) {
		return;
	}

	const char *savePath = Sys_DefaultSavePath();
	if ( savePath == NULL || savePath[0] == '\0' ) {
		return;
	}

	idStr directory = savePath;
	directory.AppendPath( OPENQ4_GAMEDIR );
	Sys_Mkdir( directory.c_str() );
	directory.AppendPath( "logs" );
	Sys_Mkdir( directory.c_str() );

	idStr path = directory;
	path.AppendPath( "fatal.txt" );
	if ( path.Length() < (int)sizeof( posix_fatalBreadcrumbPath ) ) {
		idStr::Copynz( posix_fatalBreadcrumbPath, path.c_str(), sizeof( posix_fatalBreadcrumbPath ) );
	}
}

/*
================
Posix_AppendFatalBreadcrumbRaw

Async-signal-safe once Posix_InitFatalBreadcrumbPath has run: only
open/write/close on the cached path, no allocation or locale work.
================
*/
void Posix_AppendFatalBreadcrumbRaw( const char *text ) {
	if ( text == NULL || text[0] == '\0' || posix_fatalBreadcrumbPath[0] == '\0' ) {
		return;
	}

	int fd = open( posix_fatalBreadcrumbPath, O_WRONLY | O_CREAT | O_APPEND, 0644 );
	if ( fd == -1 ) {
		return;
	}

	size_t length = 0;
	while ( text[ length ] != '\0' ) {
		length++;
	}
	(void)write( fd, text, length );
	(void)write( fd, "\n", 1 );
	close( fd );
}

/*
================
Posix_AppendFatalBreadcrumb

Appends timestamped fatal error text to a small always-writable file in the
save path so support tooling (including the macOS support collector) can
harvest fatal errors that never reach the regular session log: init-phase
errors, worker-thread errors, and signal deaths.
================
*/
void Posix_AppendFatalBreadcrumb( const char *text ) {
	if ( text == NULL || text[0] == '\0' ) {
		return;
	}

	Posix_InitFatalBreadcrumbPath();

	char stamped[ MAX_POSIX_PRINT_MSG + 64 ];
	char stamp[64];
	stamp[0] = '\0';
	time_t now = time( NULL );
	struct tm tmNow;
	if ( localtime_r( &now, &tmNow ) != NULL ) {
		strftime( stamp, sizeof( stamp ), "[%Y-%m-%d %H:%M:%S] ", &tmNow );
	}
	idStr::snPrintf( stamped, sizeof( stamped ), "%s%s", stamp, text );
	Posix_AppendFatalBreadcrumbRaw( stamped );
}

/*
================
Sys_Error
================
*/
#if defined( OPENQ4_POSIX_OWNS_COMMON_SYS )
void Sys_Error(const char *error, ...) {
	va_list argptr;
	char text[MAX_POSIX_PRINT_MSG];

	if ( error == NULL ) {
		idStr::Copynz( text, "Unknown error", sizeof( text ) );
	} else {
		va_start( argptr, error );
		idStr::vsnPrintf( text, sizeof( text ) - 1, error, argptr );
		va_end( argptr );
		text[sizeof( text ) - 1] = '\0';
	}
	text[sizeof( text ) - 1] = '\0';

	Sys_SetFatalError( text );
	Posix_AppendFatalBreadcrumb( text );
#if defined( __ANDROID__ )
	// whatever was mid-line when this happened is the context for the error,
	// so get it out before the error itself rather than after
	Sys_AndroidLogFlush();
#endif
	Sys_Printf( "Sys_Error: %s\n", text );
	Posix_ConsoleFatalErrorWait();

	Posix_Exit( EXIT_FAILURE );
}
#endif

/*
===============
Sys_FreeOpenAL
===============
*/
void Sys_FreeOpenAL( void ) { }
