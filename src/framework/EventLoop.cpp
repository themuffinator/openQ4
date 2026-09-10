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
#include <cstddef>
#include "../sys/KeyEventMetadata.h"
#include "../sys/EventQueueContinuity.h"

idCVar idEventLoop::com_journal( "com_journal", "0", CVAR_INIT|CVAR_SYSTEM, "1 = record journal, 2 = play back journal", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );

idEventLoop eventLoopLocal;
idEventLoop *eventLoop = &eventLoopLocal;

static bool EventLoop_IsPrivateConsoleEvent( const sysEvent_t &event ) {
	if ( event.evType != SE_CONSOLE || event.evPtr == NULL || event.evPtrLength <= 0 ||
		cvarSystem == NULL || !cvarSystem->IsInitialized() ) {
		return false;
	}
	if ( memchr( event.evPtr, '\0', static_cast<size_t>( event.evPtrLength ) ) == NULL ) {
		// A malformed console event is not safe to persist as text.
		return true;
	}
	return cvarSystem->CommandContainsPrivateCVar( static_cast<const char *>( event.evPtr ) );
}

// Journals retain the historical native sysEvent_t layout and payload order.
// They are not portable between ABIs and remain trusted command recordings.
// The pointer bytes are historical padding only: never deserialize an address.
static const int MAX_JOURNAL_EVENT_PAYLOAD = 1024 * 1024;
static_assert( sizeof( sysEventType_t ) == sizeof( int ), "Historical journal event type width changed" );

static int EventLoop_EventType( const sysEvent_t &ev ) {
	int type;
	memcpy( &type, &ev.evType, sizeof( type ) );
	return type;
}

static const char *EventLoop_ValidateHeader( int type, int length ) {
	if ( length < 0 || length > MAX_JOURNAL_EVENT_PAYLOAD ) {
		return "Invalid journal event payload length";
	}
	switch ( type ) {
	case SE_KEY:
		return length == 0 || length == openq4::KeyEventMetadataBytes ? NULL : "Unexpected journal key metadata length";
	case SE_NONE:
	case SE_CHAR:
	case SE_MOUSE:
	case SE_JOYSTICK_AXIS:
		return length == 0 ? NULL : "Unexpected journal event payload";
	case SE_CONSOLE:
	case SE_RETAINED_UI:
		return length > 0 ? NULL : "Missing journal event payload";
	default:
		return "Invalid journal event type";
	}
}

static const char *EventLoop_ValidatePayload( const sysEvent_t &ev ) {
	if ( ( ev.evPtrLength > 0 ) != ( ev.evPtr != NULL ) ) {
		return "Invalid event payload ownership";
	}
	if ( ev.evType == SE_KEY && ev.evPtrLength ) {
		openq4::KeyEventMetadata metadata;
		if ( !openq4::DecodeKeyEventMetadata( ev.evPtr, static_cast<size_t>( ev.evPtrLength ), metadata ) ) return "Invalid journal key metadata";
	}
	if ( ev.evType == SE_CONSOLE && memchr( ev.evPtr, '\0', static_cast<size_t>( ev.evPtrLength ) ) == NULL ) {
		return "Unterminated console event payload";
	}
	return NULL;
}

// Reader allocations and live queue payloads are owned exactly once. Explicit
// Reset precedes FatalError (which can exit without unwinding); the destructor
// also covers exceptions from file operations or dispatched engine callbacks.
class idScopedEventPayload {
public:
	idScopedEventPayload( sysEvent_t &event, bool clearConsole ) : ev( event ), clear( clearConsole ), owned( true ) {}
	~idScopedEventPayload() { Reset(); }
	void Release() { owned = false; }
	void Reset() {
		if ( owned && ev.evPtr ) {
			if ( clear && ev.evType == SE_CONSOLE && ev.evPtrLength > 0 ) {
				memset( ev.evPtr, 0, ev.evPtrLength );
			}
			Mem_Free( ev.evPtr );
			ev.evPtr = NULL;
			ev.evPtrLength = 0;
		}
		owned = false;
	}
private:
	idScopedEventPayload( const idScopedEventPayload & ) = delete;
	idScopedEventPayload &operator=( const idScopedEventPayload & ) = delete;
	sysEvent_t &ev;
	bool clear, owned;
};

static const char *EventLoop_ReadJournalEvent( idFile *file, sysEvent_t &out ) {
	if ( file == NULL ) return "Journal file is unavailable";
	unsigned char header[sizeof( sysEvent_t )];
	if ( file->Read( header, sizeof( header ) ) != static_cast<int>( sizeof( header ) ) ) {
		return "Error reading journal event header";
	}
	// Decode into integers before forming an enum, so even an invalid recorded
	// enum representation is rejected without evaluating it as a C++ enum.
	int type, length;
	memcpy( &type, header + offsetof( sysEvent_t, evType ), sizeof( type ) );
	memcpy( &length, header + offsetof( sysEvent_t, evPtrLength ), sizeof( length ) );
	const char *error = EventLoop_ValidateHeader( type, length );
	if ( error != NULL ) return error;
	sysEvent_t candidate = {};
	candidate.evType = static_cast<sysEventType_t>( type );
	candidate.evPtrLength = length;
	memcpy( &candidate.evValue, header + offsetof( sysEvent_t, evValue ), sizeof( candidate.evValue ) );
	memcpy( &candidate.evValue2, header + offsetof( sysEvent_t, evValue2 ), sizeof( candidate.evValue2 ) );
	idScopedEventPayload payload( candidate, true );
	if ( length > 0 ) {
		candidate.evPtr = Mem_ClearedAlloc( length );
		if ( candidate.evPtr == NULL ) return "Unable to allocate journal event payload";
		if ( file->Read( candidate.evPtr, length ) != length ) return "Error reading journal event payload";
	}
	error = EventLoop_ValidatePayload( candidate );
	if ( error != NULL ) return error;
	out = candidate;
	payload.Release();
	return NULL;
}

static const char *EventLoop_WriteJournalEvent( idFile *file, const sysEvent_t &ev ) {
	if ( file == NULL ) return "Journal file is unavailable";
	const char *error = EventLoop_ValidateHeader( EventLoop_EventType( ev ), ev.evPtrLength );
	if ( error != NULL ) return error;
	error = EventLoop_ValidatePayload( ev );
	if ( error != NULL ) return error;
	static const char PRIVATE_EVENT_TEXT[] = "";
	sysEvent_t journalEvent;
	memset( &journalEvent, 0, sizeof( journalEvent ) );
	journalEvent.evType = ev.evType;
	journalEvent.evValue = ev.evValue;
	journalEvent.evValue2 = ev.evValue2;
	journalEvent.evPtrLength = ev.evPtrLength;
	journalEvent.evPtr = NULL;
	const void *journalData = ev.evPtr;
	if ( EventLoop_IsPrivateConsoleEvent( ev ) ) {
		journalEvent.evPtrLength = sizeof( PRIVATE_EVENT_TEXT );
		journalData = PRIVATE_EVENT_TEXT;
	}
	if ( file->Write( &journalEvent, sizeof( journalEvent ) ) != static_cast<int>( sizeof( journalEvent ) ) ) {
		return "Error writing journal event header";
	}
	if ( journalEvent.evPtrLength > 0 && file->Write( journalData, journalEvent.evPtrLength ) != journalEvent.evPtrLength ) {
		return "Error writing journal event payload";
	}
	return NULL;
}


/*
=================
idEventLoop::idEventLoop
=================
*/
idEventLoop::idEventLoop( void ) {
	com_journalFile = NULL;
	com_journalDataFile = NULL;
	initialTimeOffset = 0;
}

/*
=================
idEventLoop::~idEventLoop
=================
*/
idEventLoop::~idEventLoop( void ) {
}

/*
=================
idEventLoop::GetRealEvent
=================
*/
sysEvent_t	idEventLoop::GetRealEvent( void ) {
	sysEvent_t ev = {};

	// either get an event from the system or the journal file
	if ( com_journal.GetInteger() == 2 ) {
		const char *error = EventLoop_ReadJournalEvent( com_journalFile, ev );
		if ( error != NULL ) {
			common->FatalError( "%s", error );
			return sysEvent_t{};
		}
	} else {
		ev = Sys_GetEvent();
		const char *error = EventLoop_ValidateHeader( EventLoop_EventType( ev ), ev.evPtrLength );
		idScopedEventPayload payload( ev, error == NULL );
		if ( error == NULL ) error = EventLoop_ValidatePayload( ev );
		if ( error != NULL ) {
			payload.Reset();
			common->FatalError( "%s", error );
			return sysEvent_t{};
		}

		// write the journal value out if needed
		if ( com_journal.GetInteger() == 1 ) {
			error = EventLoop_WriteJournalEvent( com_journalFile, ev );
			if ( error != NULL ) {
				payload.Reset();
				common->FatalError( "%s", error );
				return sysEvent_t{};
			}
		}
		payload.Release();
	}

	return ev;
}

/*
=================
idEventLoop::PushEvent
=================
*/
void idEventLoop::PushEvent( sysEvent_t *event ) {
	sysEvent_t		*ev;
	static			bool printedWarning;

	ev = &com_pushedEvents[ com_pushedEventsHead & (MAX_PUSHED_EVENTS-1) ];

	if ( com_pushedEventsHead - com_pushedEventsTail >= MAX_PUSHED_EVENTS ) {
		Sys_InvalidateEventQueue();

		// don't print the warning constantly, or it can give time for more...
		if ( !printedWarning ) {
			printedWarning = true;
			common->Printf( "WARNING: Com_PushEvent overflow\n" );
		}

		if ( ev->evPtr ) {
			if ( EventLoop_IsPrivateConsoleEvent( *ev ) ) {
				memset( ev->evPtr, 0, ev->evPtrLength );
			}
			Mem_Free( ev->evPtr );
		}
		com_pushedEventsTail++;
	} else {
		printedWarning = false;
	}

	*ev = *event;
	com_pushedEventsHead++;
}

/*
=================
idEventLoop::GetEvent
=================
*/
sysEvent_t idEventLoop::GetEvent( void ) {
	if ( com_pushedEventsHead > com_pushedEventsTail ) {
		com_pushedEventsTail++;
		return com_pushedEvents[ (com_pushedEventsTail-1) & (MAX_PUSHED_EVENTS-1) ];
	}
	return GetRealEvent();
}

/*
=================
idEventLoop::ProcessEvent
=================
*/
void idEventLoop::ProcessEvent( sysEvent_t ev ) {
	idScopedEventPayload payload( ev, true );
	// track key up / down states
	if ( ev.evType == SE_KEY ) {
		idKeyInput::PreliminaryKeyEvent( ev.evValue, ( ev.evValue2 != 0 ) );
	} else if ( ev.evType == SE_MOUSE ) {
		idKeyInput::PreliminaryMouseEvent( ev.evValue, ev.evValue2 );
	} else if ( ev.evType == SE_JOYSTICK_AXIS ) {
		idKeyInput::PreliminaryJoystickEvent( ev.evValue2 );
	}

	if ( ev.evType == SE_CONSOLE ) {
		// from a text console outside the game window
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, (char *)ev.evPtr );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "\n" );
	} else {
		session->ProcessEvent( &ev );
	}

	// The scope also releases payloads when a command/session callback throws.
}

/*
===============
idEventLoop::RunEventLoop
===============
*/
int idEventLoop::RunEventLoop( bool commandExecution ) {
	sysEvent_t	ev;

	while ( 1 ) {

		if ( commandExecution ) {
			// execute any bound commands before processing another event
			cmdSystem->ExecuteCommandBuffer();
		}

		ev = GetEvent();

		// if no more events are available
		if ( ev.evType == SE_NONE ) {
			return 0;
		}
		ProcessEvent( ev );
	}

	return 0;	// never reached
}

/*
=============
idEventLoop::Init
=============
*/
void idEventLoop::Init( void ) {
	Sys_InvalidateEventQueue();

	initialTimeOffset = Sys_Milliseconds();

	common->StartupVariable( "journal", false );

	if ( com_journal.GetInteger() == 1 ) {
		common->Printf( "Journaling events\n" );
		com_journalFile = fileSystem->OpenFileWrite( "journal.dat" );
		com_journalDataFile = fileSystem->OpenFileWrite( "journaldata.dat" );
	} else if ( com_journal.GetInteger() == 2 ) {
		common->Printf( "Replaying journaled events\n" );
		com_journalFile = fileSystem->OpenFileRead( "journal.dat" );
		com_journalDataFile = fileSystem->OpenFileRead( "journaldata.dat" );
	}

	if ( com_journal.GetInteger() != 0 && ( !com_journalFile || !com_journalDataFile ) ) {
		com_journal.SetInteger( 0 );
		// Opening the pair can succeed only partially. Retire any acquired
		// handle before clearing the pair and disabling this journal attempt.
		if ( com_journalFile ) fileSystem->CloseFile( com_journalFile );
		if ( com_journalDataFile ) fileSystem->CloseFile( com_journalDataFile );
		com_journalFile = 0;
		com_journalDataFile = 0;
		common->Printf( "Couldn't open journal files\n" );
	}
}

/*
=============
idEventLoop::Shutdown
=============
*/
void idEventLoop::Shutdown( void ) {
	Sys_InvalidateEventQueue();
	if ( com_journalFile ) {
		fileSystem->CloseFile( com_journalFile );
		com_journalFile = NULL;
	}
	if ( com_journalDataFile ) {
		fileSystem->CloseFile( com_journalDataFile );
		com_journalDataFile = NULL;
	}
}

/*
================
idEventLoop::JournalLevel
================
*/
int idEventLoop::JournalLevel( void ) const {
	return com_journal.GetInteger();
}
