/*
===========================================================================

openQ4 Android platform layer: engine console output -> logcat.

===========================================================================
*/

// Every line the engine prints reaches the outside world through Sys_Printf,
// which on a normal POSIX host means fputs() to a terminal. Android has no
// terminal: stdout is /dev/null until the host app redirects it, and even the
// pipe-and-pump redirect in mobile/game_interface.cpp only recovers the bytes,
// not the lines - a read() returns whatever happened to be in flight, so single
// engine lines arrive cut in half or several at a time, and anything still
// sitting in the stdio buffer when the process dies is simply lost. That is
// exactly the output you most want after a crash.
//
// Writing to liblog directly from Sys_Printf fixes both: this file reassembles
// whole lines and emits one logcat record each, so "adb logcat -s openQ4" is
// greppable line-for-line, and nothing waits in a buffer for a flush that may
// never come. The pump in game_interface.cpp stays for third-party output
// (SDL, OpenAL, the C runtime), which still has nowhere else to go.

#include "../../idlib/precompiled.h"
#include "../posix/posix_public.h"

#include <android/log.h>

static const char *ANDROID_LOG_TAG = "openQ4";

// liblog truncates a record at roughly 4076 bytes including the tag and
// priority byte; stay well inside that rather than discover the exact limit.
static const int ANDROID_LOG_MAX_LINE = 1008;

// posix_public.h publishes these to the platform-independent callers so that
// posix_main.cpp and posix_signal.cpp need not include <android/log.h>.
file_scoped_compile_time_assert( SYS_ANDROID_LOG_DEBUG == ANDROID_LOG_DEBUG );
file_scoped_compile_time_assert( SYS_ANDROID_LOG_INFO == ANDROID_LOG_INFO );
file_scoped_compile_time_assert( SYS_ANDROID_LOG_WARN == ANDROID_LOG_WARN );
file_scoped_compile_time_assert( SYS_ANDROID_LOG_ERROR == ANDROID_LOG_ERROR );

struct androidLogLine_t {
	char	text[ ANDROID_LOG_MAX_LINE + 1 ];
	int		length;
};

// Per-thread, so a partial line from the async or sound thread cannot splice
// itself into the middle of a line the main thread is still building. It also
// keeps the buffered path lock-free.
static thread_local androidLogLine_t androidLogLine = { { '\0' }, 0 };

/*
================
Sys_AndroidLogLinePriority

idCommonLocal::Warning and ::Error route through the same Sys_Printf as every
other print, so without this "adb logcat openQ4:E" would show nothing at all.
The prefixes are the ones those two write; colour escapes have already been
stripped by the time a line gets here.
================
*/
static int Sys_AndroidLogLinePriority( int priority, const char *line ) {
	if ( idStr::Cmpn( line, "WARNING:", 8 ) == 0 ) {
		return ANDROID_LOG_WARN;
	}
	if ( idStr::Cmpn( line, "ERROR:", 6 ) == 0 ||
		 idStr::Cmpn( line, "FATAL:", 6 ) == 0 ||
		 idStr::Cmpn( line, "Sys_Error:", 10 ) == 0 ) {
		return ANDROID_LOG_ERROR;
	}
	return priority;
}

/*
================
Sys_AndroidLogEmitLine
================
*/
static void Sys_AndroidLogEmitLine( int priority ) {
	androidLogLine_t &line = androidLogLine;

	if ( line.length <= 0 ) {
		// the engine prints plenty of blank lines for on-screen layout; they
		// are pure noise as logcat records
		return;
	}

	line.text[ line.length ] = '\0';
	__android_log_write( Sys_AndroidLogLinePriority( priority, line.text ), ANDROID_LOG_TAG, line.text );
	line.length = 0;
}

/*
================
Sys_AndroidLogPrint

Accumulates until a newline, because callers hand over whatever fragment they
happened to format - the loading code in particular prints a run of dots one
call at a time.
================
*/
void Sys_AndroidLogPrint( int priority, const char *text ) {
	if ( text == NULL ) {
		return;
	}

	androidLogLine_t &line = androidLogLine;

	for ( const char *c = text; *c != '\0'; ) {
		// idTech4 colour escapes would reach logcat as literal "^3" and would
		// also hide the WARNING:/ERROR: prefixes from the priority mapping
		const int escapeLength = idStr::ColorEscapeLength( c );
		if ( escapeLength > 0 ) {
			c += escapeLength;
			continue;
		}

		if ( *c == '\n' ) {
			Sys_AndroidLogEmitLine( priority );
			c++;
			continue;
		}

		if ( *c == '\r' ) {
			c++;
			continue;
		}

		if ( line.length >= ANDROID_LOG_MAX_LINE ) {
			Sys_AndroidLogEmitLine( priority );
		}
		line.text[ line.length++ ] = *c;
		c++;
	}
}

/*
================
Sys_AndroidLogFlush

Pushes out a line the engine left unterminated. Worth calling before anything
that can end the process, so the last thing printed is not the one thing
missing from the log.
================
*/
void Sys_AndroidLogFlush( void ) {
	Sys_AndroidLogEmitLine( ANDROID_LOG_INFO );
}

/*
================
Sys_AndroidLogPrintRaw

The signal-handler sink. Deliberately does not touch the thread-local buffer
above: __tls_get_addr can allocate on a thread's first access to a shared
library's TLS, which is not something to do from a fatal signal handler. The
static buffer is safe here because the only caller is that handler, which
_exit()s as soon as it has finished writing.

__android_log_write itself takes a lock, so this can in principle deadlock if
the crash landed inside liblog. That is worth it: the alternative is writing to
stderr and hoping the pump thread in game_interface.cpp drains the pipe before
_exit() tears the process down, which loses the message far more often than
liblog deadlocks.
================
*/
static char	androidSignalLine[ ANDROID_LOG_MAX_LINE + 1 ];
static int	androidSignalLineLength = 0;

void Sys_AndroidLogPrintRaw( int priority, const char *text ) {
	if ( text == NULL ) {
		return;
	}

	for ( const char *c = text; *c != '\0'; c++ ) {
		if ( *c == '\n' || androidSignalLineLength >= ANDROID_LOG_MAX_LINE ) {
			if ( androidSignalLineLength > 0 ) {
				androidSignalLine[ androidSignalLineLength ] = '\0';
				__android_log_write( priority, ANDROID_LOG_TAG, androidSignalLine );
				androidSignalLineLength = 0;
			}
			if ( *c == '\n' ) {
				continue;
			}
		}
		androidSignalLine[ androidSignalLineLength++ ] = *c;
	}
}
