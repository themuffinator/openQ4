/*
sys_event.cpp

Event are used for scheduling tasks and for linking script commands.

*/




#include "../Game_local.h"
#include <memory>

#define MAX_EVENTSPERFRAME			8192		// Upped from 4096
//#define CREATE_EVENT_CODE

/***********************************************************************

  idEventDef

***********************************************************************/

idEventDef *idEventDef::eventDefList[MAX_EVENTS];
int idEventDef::numEventDefs = 0;

static bool eventError = false;
static char eventErrorMsg[ 128 ];

// Event data is allocated from pointer-aligned blocks. Keep every argument on
// that alignment and add explicit padding before trace_t, whose old bool +
// trace layout placed the trace at an unaligned address on 64-bit targets.
static const size_t EVENT_ARG_ALIGNMENT = alignof( trace_t ) > alignof( intptr_t ) ? alignof( trace_t ) : alignof( intptr_t );
static_assert( EVENT_ARG_ALIGNMENT <= 16, "event data allocator alignment is insufficient" );
static const size_t EVENT_TRACE_DATA_OFFSET =
	( sizeof( bool ) + alignof( trace_t ) - 1 ) & ~( alignof( trace_t ) - 1 );
static const size_t EVENT_TRACE_MATERIAL_OFFSET = EVENT_TRACE_DATA_OFFSET + sizeof( trace_t );
static const size_t EVENT_TRACE_ARG_SIZE = EVENT_TRACE_MATERIAL_OFFSET + MAX_STRING_LEN;

static size_t idEvent_GetLegacyArgLayout( const idEventDef *eventdef, size_t offsets[ D_EVENT_MAXARGS ] ) {
	const char *formatspec = eventdef->GetArgFormat();
	size_t size = 0;

	for ( int i = 0; i < eventdef->GetNumArgs(); i++ ) {
		offsets[ i ] = size;
		switch ( formatspec[ i ] ) {
		case D_EVENT_FLOAT:
		case D_EVENT_INTEGER:
		case D_EVENT_INTEGER64bit:
		case D_EVENT_ENTITY:
		case D_EVENT_ENTITY_NULL:
			size += sizeof( intptr_t );
			break;
		case D_EVENT_VECTOR:
			size += E_EVENT_SIZEOF_VEC;
			break;
		case D_EVENT_STRING:
			size += MAX_STRING_LEN;
			break;
		case D_EVENT_TRACE:
			size += sizeof( bool ) + sizeof( trace_t ) + MAX_STRING_LEN;
			break;
		default:
			return 0;
		}
	}
	return size;
}

static void idEvent_ConvertLegacyRawArgs( idRestoreGame *savefile, const idEventDef *eventdef, byte *dest, const byte *source,
		const size_t legacyOffsets[ D_EVENT_MAXARGS ] ) {
	const char *formatspec = eventdef->GetArgFormat();

	for ( int i = 0; i < eventdef->GetNumArgs(); i++ ) {
		byte *destPtr = dest + eventdef->GetArgOffset( i );
		const byte *sourcePtr = source + legacyOffsets[ i ];
		size_t copySize = 0;

		switch ( formatspec[ i ] ) {
		case D_EVENT_FLOAT:
		case D_EVENT_INTEGER:
		case D_EVENT_INTEGER64bit:
		case D_EVENT_ENTITY:
		case D_EVENT_ENTITY_NULL:
			copySize = sizeof( intptr_t );
			break;
		case D_EVENT_VECTOR:
			copySize = E_EVENT_SIZEOF_VEC;
			break;
		case D_EVENT_STRING:
			if ( memchr( sourcePtr, '\0', MAX_STRING_LEN ) == NULL ) {
				savefile->Error( "idEvent::Restore: unterminated legacy string on arg %d of event '%s'", i, eventdef->GetName() );
			}
			copySize = MAX_STRING_LEN;
			break;
		case D_EVENT_TRACE: {
			if ( *sourcePtr > 1 ) {
				savefile->Error( "idEvent::Restore: invalid legacy trace flag on arg %d of event '%s'", i, eventdef->GetName() );
			}
			const bool hasTrace = *sourcePtr != 0;
			*reinterpret_cast<bool *>( destPtr ) = hasTrace;
			if ( hasTrace ) {
				memcpy( destPtr + EVENT_TRACE_DATA_OFFSET, sourcePtr + sizeof( bool ), sizeof( trace_t ) );
				memcpy( destPtr + EVENT_TRACE_MATERIAL_OFFSET,
					sourcePtr + sizeof( bool ) + sizeof( trace_t ), MAX_STRING_LEN );
				trace_t *trace = reinterpret_cast<trace_t *>( destPtr + EVENT_TRACE_DATA_OFFSET );
				if ( trace->c.material != NULL ) {
					const char *materialName = reinterpret_cast<const char *>( destPtr + EVENT_TRACE_MATERIAL_OFFSET );
					if ( memchr( materialName, '\0', MAX_STRING_LEN ) == NULL ) {
						savefile->Error( "idEvent::Restore: unterminated legacy trace material on arg %d of event '%s'", i, eventdef->GetName() );
					}
					// Repair the saved address before an immediate resave can use it.
					trace->c.material = declManager->FindMaterial( materialName, true );
				}
				// The raw format saved no material-type name or hit-map coordinates.
				// Its old address cannot survive a load; use the material's base type.
				trace->c.materialType = trace->c.material != NULL ? trace->c.material->GetMaterialType() : NULL;
			}
			break;
		}
		default:
			break;
		}

		if ( copySize ) {
			memcpy( destPtr, sourcePtr, copySize );
		}
	}
}

/*
================
idEventDef::idEventDef
================
*/
idEventDef::idEventDef( const char *command, const char *formatspec, char returnType ) {
	idEventDef		*ev;
	int				i;
	unsigned int	bits;

	assert( command );
	assert( !idEvent::initialized );

	// Allow NULL to indicate no args, but always store it as ""
	// so we don't have to check for it.
	if ( !formatspec ) {
		formatspec = "";
	}
	
	this->name = command;
	this->formatspec = formatspec;
	this->returnType = returnType;

	const size_t formatLength = strlen( formatspec );
	assert( formatLength <= D_EVENT_MAXARGS );
	if ( formatLength > D_EVENT_MAXARGS ) {
		eventError = true;
		sprintf( eventErrorMsg, "idEventDef::idEventDef : Too many args for '%s' event.", name );
		return;
	}
	numargs = static_cast<int>( formatLength );

	// make sure the format for the args is valid, calculate the formatspecindex, and the offsets for each arg
	bits = 0;
	argsize = 0;
	memset( argOffset, 0, sizeof( argOffset ) );
	for( i = 0; i < numargs; i++ ) {
		argsize = ( argsize + EVENT_ARG_ALIGNMENT - 1 ) & ~( EVENT_ARG_ALIGNMENT - 1 );
		argOffset[ i ] = argsize;
		switch( formatspec[ i ] ) {
		case D_EVENT_FLOAT :
			bits |= 1 << i;
			argsize += sizeof(intptr_t);
			break;

		case D_EVENT_INTEGER :
			argsize += sizeof(intptr_t);
			break;

		case D_EVENT_INTEGER64bit :
			argsize += sizeof(intptr_t);
			break;

		case D_EVENT_VECTOR :
			argsize += E_EVENT_SIZEOF_VEC;
			break;

		case D_EVENT_STRING :
			argsize += MAX_STRING_LEN;
			break;

		case D_EVENT_ENTITY :
			argsize += sizeof( intptr_t);
			break;

		case D_EVENT_ENTITY_NULL :
			argsize += sizeof(intptr_t);
			break;

		case D_EVENT_TRACE :
			argsize += EVENT_TRACE_ARG_SIZE;
			break;

		default :
			eventError = true;
			sprintf( eventErrorMsg, "idEventDef::idEventDef : Invalid arg format '%s' string for '%s' event.", formatspec, name );
			return;
			break;
		}
	}

	// calculate the formatspecindex
	formatspecIndex = ( 1 << ( numargs + D_EVENT_MAXARGS ) ) | bits;

	// go through the list of defined events and check for duplicates
	// and mismatched format strings
	eventnum = numEventDefs;
	for( i = 0; i < eventnum; i++ ) {
		ev = eventDefList[ i ];
		if ( idStr::Cmp( command, ev->name ) == 0 ) {
			if ( idStr::Cmp( formatspec, ev->formatspec ) != 0 ) {
				eventError = true;
				sprintf( eventErrorMsg, "idEvent '%s' defined twice with same name but differing format strings ('%s'!='%s').",
					command, formatspec, ev->formatspec );
				return;
			}

			if ( ev->returnType != returnType ) {
				eventError = true;
				sprintf( eventErrorMsg, "idEvent '%s' defined twice with same name but differing return types ('%c'!='%c').",
					command, returnType, ev->returnType );
				return;
			}
			// Don't bother putting the duplicate event in list.
			eventnum = ev->eventnum;
			return;
		}
	}

	ev = this;

	if ( numEventDefs >= MAX_EVENTS ) {
		eventError = true;
		sprintf( eventErrorMsg, "numEventDefs >= MAX_EVENTS" );
		return;
	}
	eventDefList[numEventDefs] = ev;
	numEventDefs++;
}

/*
================
idEventDef::NumEventCommands
================
*/
int	idEventDef::NumEventCommands( void ) {
	return numEventDefs;
}

/*
================
idEventDef::GetEventCommand
================
*/
const idEventDef *idEventDef::GetEventCommand( int eventnum ) {
	return eventDefList[ eventnum ];
}

/*
================
idEventDef::FindEvent
================
*/
const idEventDef *idEventDef::FindEvent( const char *name ) {
	idEventDef	*ev;
	int			num;
	int			i;

	assert( name );

	num = numEventDefs;
	for( i = 0; i < num; i++ ) {
		ev = eventDefList[ i ];
		if ( idStr::Cmp( name, ev->name ) == 0 ) {
			return ev;
		}
	}

	return NULL;
}

/***********************************************************************

  idEvent

***********************************************************************/

static idLinkList<idEvent> FreeEvents;
static idLinkList<idEvent> EventQueue;
static idEvent EventPool[ MAX_EVENTS ];

bool idEvent::initialized = false;

idDynamicBlockAlloc<byte, 16 * 1024, 256, MA_EVENT>	idEvent::eventDataAllocator;

static const int EVENT_SAVE_FORMAT_TYPED = -2;

static void idEvent_WriteIntPtr( idSaveGame *savefile, const intptr_t value ) {
	const uint64_t bits = static_cast<uint64_t>( static_cast<int64_t>( value ) );
	savefile->WriteInt( static_cast<int>( bits & 0xffffffffu ) );
	savefile->WriteInt( static_cast<int>( ( bits >> 32 ) & 0xffffffffu ) );
}

static intptr_t idEvent_ReadIntPtr( idRestoreGame *savefile ) {
	int low;
	int high;

	savefile->ReadInt( low );
	savefile->ReadInt( high );

	const uint64_t bits = static_cast<uint32_t>( low ) | ( static_cast<uint64_t>( static_cast<uint32_t>( high ) ) << 32 );
	return static_cast<intptr_t>( static_cast<int64_t>( bits ) );
}

static void idEvent_StoreTraceMaterialName( byte *dataPtr ) {
	trace_t *trace = reinterpret_cast<trace_t *>( dataPtr + EVENT_TRACE_DATA_OFFSET );
	char *materialName = reinterpret_cast<char *>( dataPtr + EVENT_TRACE_MATERIAL_OFFSET );

	if ( trace->c.material ) {
		idStr::Copynz( materialName, trace->c.material->GetName(), MAX_STRING_LEN );
	} else {
		materialName[0] = '\0';
	}
}

static void idEvent_SaveTypedArgs( idSaveGame *savefile, const idEventDef *eventdef, byte *eventData ) {
	const char *formatspec = eventdef->GetArgFormat();
	const int numargs = eventdef->GetNumArgs();

	for ( int i = 0; i < numargs; i++ ) {
		const size_t offset = eventdef->GetArgOffset( i );
		byte *dataPtr = eventData + offset;

		switch( formatspec[ i ] ) {
		case D_EVENT_FLOAT :
			savefile->WriteFloat( *reinterpret_cast<float *>( dataPtr ) );
			break;

		case D_EVENT_INTEGER :
			savefile->WriteInt( static_cast<int>( *reinterpret_cast<intptr_t *>( dataPtr ) ) );
			break;

		case D_EVENT_INTEGER64bit :
			idEvent_WriteIntPtr( savefile, *reinterpret_cast<intptr_t *>( dataPtr ) );
			break;

		case D_EVENT_VECTOR :
			savefile->WriteVec3( *reinterpret_cast<idVec3 *>( dataPtr ) );
			break;

		case D_EVENT_STRING :
			savefile->WriteString( reinterpret_cast<const char *>( dataPtr ) );
			break;

		case D_EVENT_ENTITY :
		case D_EVENT_ENTITY_NULL :
			reinterpret_cast< idEntityPtr<idEntity> * >( dataPtr )->Save( savefile );
			break;

		case D_EVENT_TRACE : {
			const bool hasTrace = *reinterpret_cast<bool *>( dataPtr );
			savefile->WriteBool( hasTrace );
			if ( hasTrace ) {
				savefile->WriteTrace( *reinterpret_cast<trace_t *>( dataPtr + EVENT_TRACE_DATA_OFFSET ) );
			}
			break;
		}

		default :
			gameLocal.Error( "idEvent::Save : Invalid arg format '%s' string for '%s' event.", formatspec, eventdef->GetName() );
			break;
		}
	}
}

static void idEvent_RestoreTypedArgs( idRestoreGame *savefile, const idEventDef *eventdef, byte *eventData ) {
	const char *formatspec = eventdef->GetArgFormat();
	const int numargs = eventdef->GetNumArgs();

	for ( int i = 0; i < numargs; i++ ) {
		const size_t offset = eventdef->GetArgOffset( i );
		byte *dataPtr = eventData + offset;

		switch( formatspec[ i ] ) {
		case D_EVENT_FLOAT :
			savefile->ReadFloat( *reinterpret_cast<float *>( dataPtr ) );
			break;

		case D_EVENT_INTEGER : {
			int value;
			savefile->ReadInt( value );
			*reinterpret_cast<intptr_t *>( dataPtr ) = value;
			break;
		}

		case D_EVENT_INTEGER64bit :
			*reinterpret_cast<intptr_t *>( dataPtr ) = idEvent_ReadIntPtr( savefile );
			break;

		case D_EVENT_VECTOR :
			savefile->ReadVec3( *reinterpret_cast<idVec3 *>( dataPtr ) );
			break;

		case D_EVENT_STRING : {
			idStr string;
			savefile->ReadString( string );
			idStr::Copynz( reinterpret_cast<char *>( dataPtr ), string.c_str(), MAX_STRING_LEN );
			break;
		}

		case D_EVENT_ENTITY :
		case D_EVENT_ENTITY_NULL :
			reinterpret_cast< idEntityPtr<idEntity> * >( dataPtr )->Restore( savefile );
			break;

		case D_EVENT_TRACE : {
			bool hasTrace;
			savefile->ReadBool( hasTrace );
			*reinterpret_cast<bool *>( dataPtr ) = hasTrace;
			if ( hasTrace ) {
				savefile->ReadTrace( *reinterpret_cast<trace_t *>( dataPtr + EVENT_TRACE_DATA_OFFSET ) );
				idEvent_StoreTraceMaterialName( dataPtr );
			}
			break;
		}

		default :
			savefile->Error( "idEvent::Restore : Invalid arg format '%s' string for '%s' event.", formatspec, eventdef->GetName() );
			break;
		}
	}
}

/*
================
idEvent::~idEvent()
================
*/
idEvent::~idEvent() {
	Free();
}


void idEvent::WriteDebugInfo( void ) {
	idEvent	*event;
	int		count = 0;

	idFile *FH = fileSystem->OpenFileAppend( "idEvents.txt" );

	FH->Printf( "Num Events = %d\n", EventQueue.Num() );

	event = EventQueue.Next();
	while( event != NULL ) {
		count++;

		FH->Printf( "%d. %d - %s - %s - %s\n", count, event->time, event->eventdef->GetName(), event->typeinfo->classname, event->object->GetClassname() );

		event = event->eventNode.Next();
	}

	FH->Printf( "\n\n" );
	fileSystem->CloseFile( FH );
}

/*
================
idEvent::Alloc
================
*/
idEvent *idEvent::Alloc( const idEventDef *evdef, int numargs, va_list args ) {
	idEvent		*ev;
	size_t		size;
	const char	*format;
	idEventArg	*arg;
	byte		*dataPtr;
	int			i;
	const char	*materialName;

	if ( FreeEvents.IsListEmpty() ) {
		// Map shutdown clears the whole queue, so an event posted while the map
		// is torn down would never run. Dropping it keeps a full queue, which a
		// refused load can leave behind, from raising a second error in teardown.
		if ( gameLocal.GameState() == GAMESTATE_SHUTDOWN ) {
			return NULL;
		}
		WriteDebugInfo( );
		gameLocal.Error( "idEvent::Alloc : No more free events for '%s' event.", evdef->GetName() );
	}

	ev = FreeEvents.Next();
	ev->eventNode.Remove();

	ev->eventdef = evdef;

	if ( numargs != evdef->GetNumArgs() ) {
		gameLocal.Error( "idEvent::Alloc : Wrong number of args for '%s' event.", evdef->GetName() );
	}

	size = evdef->GetArgSize();
	if ( size ) {
		if ( size > ID_HEAP_MAX_SIZE ) {
			gameLocal.Error( "idEvent::Alloc : Event data is too large (%zu bytes) for '%s'.", size, evdef->GetName() );
			throw idAllocError( "idEvent data exceeds the 32-bit heap limit" );
		}
		ev->data = eventDataAllocator.Alloc( static_cast<int>( size ) );
		if ( ev->data == NULL ) {
			gameLocal.Error( "idEvent::Alloc : Failed to allocate %zu bytes for '%s'.", size, evdef->GetName() );
			throw idAllocError( "idEvent data allocation failed" );
		}
		memset( ev->data, 0, size );
	} else {
		ev->data = NULL;
	}

	format = evdef->GetArgFormat();
	for( i = 0; i < numargs; i++ ) {
		arg = va_arg( args, idEventArg * );
		if ( format[ i ] != arg->type ) {
// RAVEN BEGIN
// abahr: type checking change as per Jim D.
			//if ( ( format[ i ] == D_EVENT_ENTITY_NULL ) && ( arg->type == D_EVENT_ENTITY ) ) {
			//// these types are identical, so allow them
			//} else if ( ( arg->type == D_EVENT_INTEGER ) && ( arg->value == 0 ) ) {
			//	if ( ( format[ i ] == D_EVENT_ENTITY ) || ( format[ i ] == D_EVENT_ENTITY_NULL ) || ( format[ i ] == D_EVENT_TRACE ) ) {
			//	 // when NULL is passed in for an entity or trace, it gets cast as an integer 0, so don't give an error when it happens
			//	} else {
			//		 gameLocal.Error( "idEvent::Alloc : Wrong type passed in for arg # %d on '%s' event.", i, evdef->GetName() );
			//	}
			//} else {
			//	gameLocal.Error( "idEvent::Alloc : Wrong type passed in for arg # %d on '%s' event.", i, evdef->GetName() );
			//}
// RAVEN END
		}

		dataPtr = &ev->data[ evdef->GetArgOffset( i ) ];

		switch( format[ i ] ) {
		case D_EVENT_FLOAT :
			*reinterpret_cast<int *>( dataPtr ) = static_cast<int>( arg->value );
			break;

		case D_EVENT_INTEGER :
		case D_EVENT_INTEGER64bit :
			*reinterpret_cast<intptr_t *>( dataPtr ) = arg->value;
			break;

		case D_EVENT_VECTOR :
			if ( arg->value ) {
				*reinterpret_cast<idVec3 *>( dataPtr ) = *reinterpret_cast<const idVec3 *>( arg->value );
			}
			break;

		case D_EVENT_STRING :
			if ( arg->value ) {
				idStr::Copynz( reinterpret_cast<char *>( dataPtr ), reinterpret_cast<const char *>( arg->value ), MAX_STRING_LEN );
			}
			break;

// RAVEN BEGIN
// abahr: type checking change as per Jim D.
// jshepard: TODO FIXME HACK this never ever produces desired, positive results. Events should be built to prepare for null entities, especially when dealing with 
//							 script events. This will throw a warning, and events should be prepared to deal with null entities. 
		case D_EVENT_ENTITY :
			if ( reinterpret_cast<idEntity *>( arg->value ) == NULL ) {
				gameLocal.Warning( "idEvent::Alloc : NULL entity passed in to event function that expects a non-NULL pointer on arg # %d on '%s' event.", i, evdef->GetName() );
			}
			*reinterpret_cast< idEntityPtr<idEntity> * >( dataPtr ) = reinterpret_cast<idEntity *>( arg->value );
			break;
 
		case D_EVENT_ENTITY_NULL :
			*reinterpret_cast< idEntityPtr<idEntity> * >( dataPtr ) = reinterpret_cast<idEntity *>( arg->value );
			break;
//RAVEN END

		case D_EVENT_TRACE :
			if ( arg->value ) {
				*reinterpret_cast<bool *>( dataPtr ) = true;
				*reinterpret_cast<trace_t *>( dataPtr + EVENT_TRACE_DATA_OFFSET ) = *reinterpret_cast<const trace_t *>( arg->value );

				// save off the material as a string since the pointer won't be valid in save games.
				// since we save off the entire trace_t structure, if the material is NULL here,
				// it will be NULL when we process it, so we don't need to save off anything in that case.
				if ( reinterpret_cast<const trace_t *>( arg->value )->c.material ) {
					materialName = reinterpret_cast<const trace_t *>( arg->value )->c.material->GetName();
					idStr::Copynz( reinterpret_cast<char *>( dataPtr + EVENT_TRACE_MATERIAL_OFFSET ), materialName, MAX_STRING_LEN );
				}
			} else {
				*reinterpret_cast<bool *>( dataPtr ) = false;
			}
			break;

		default :
			gameLocal.Error( "idEvent::Alloc : Invalid arg format '%s' string for '%s' event.", format, evdef->GetName() );
			break;
		}
	}

	return ev;
}

/*
================
idEvent::CopyArgs
================
*/
void idEvent::CopyArgs( const idEventDef *evdef, int numargs, va_list args, intptr_t data[ D_EVENT_MAXARGS ] ) {
	int			i;
	const char	*format;
	idEventArg	*arg;

	format = evdef->GetArgFormat();
	if ( numargs != evdef->GetNumArgs() ) {
		gameLocal.Error( "idEvent::CopyArgs : Wrong number of args for '%s' event.", evdef->GetName() );
	}

	for( i = 0; i < numargs; i++ ) {
		arg = va_arg( args, idEventArg * );
		if ( format[ i ] != arg->type ) {
// RAVEN BEGIN
// abahr: type checking change as per Jim D.
			//if ( ( format[ i ] == D_EVENT_ENTITY_NULL ) && ( arg->type == D_EVENT_ENTITY ) ) {
			//// these types are identical, so allow them
			//} else if ( ( arg->type == D_EVENT_INTEGER ) && ( arg->value == 0 ) ) {
			//	if ( ( format[ i ] == D_EVENT_ENTITY ) || ( format[ i ] == D_EVENT_ENTITY_NULL ) ) {
			//	// when NULL is passed in for an entity, it gets cast as an integer 0, so don't give an error when it happens
			//	} else {
			//		gameLocal.Error( "idEvent::Alloc : Wrong type passed in for arg # %d on '%s' event.", i, evdef->GetName() );
			//	}
			//} else {
			//	gameLocal.Error( "idEvent::Alloc : Wrong type passed in for arg # %d on '%s' event.", i, evdef->GetName() );
			//}
// RAVEN END
		}

		data[ i ] = arg->value;
	}
}

/*
================
idEvent::Free
================
*/
void idEvent::Free( void ) {
	if ( data ) {
		eventDataAllocator.Free( data );
		data = NULL;
	}

	eventdef	= NULL;
	time		= 0;
	object		= NULL;
	typeinfo	= NULL;

	eventNode.SetOwner( this );
	eventNode.AddToEnd( FreeEvents );
}

/*
================
idEvent::Schedule
================
*/
void idEvent::Schedule( idClass *obj, const idTypeInfo *type, int time ) {
	idEvent *event;

	assert( initialized );
	if ( !initialized ) {
		return;
	}

	object = obj;
	typeinfo = type;

	// wraps after 24 days...like I care. ;)
	this->time = gameLocal.time + time;

	eventNode.Remove();

	event = EventQueue.Next();
	while( ( event != NULL ) && ( this->time >= event->time ) ) {
		event = event->eventNode.Next();
	}

	if ( event ) {
		eventNode.InsertBefore( event->eventNode );
	} else {
		eventNode.AddToEnd( EventQueue );
	}
}

/*
================
idEvent::CancelEvents
================
*/
void idEvent::CancelEvents( const idClass *obj, const idEventDef *evdef ) {
	idEvent *event;
	idEvent *next;

	if ( !initialized ) {
		return;
	}

	for( event = EventQueue.Next(); event != NULL; event = next ) {
		next = event->eventNode.Next();
		if ( event->object == obj ) {
			if ( !evdef || ( evdef == event->eventdef ) ) {
				event->Free();
			}
		}
	}
}

// RAVEN BEGIN
// abahr:
/*
================
idEvent::EventIsPosted
================
*/
bool idEvent::EventIsPosted( const idClass *obj, const idEventDef *evdef ) {
	idEvent *event;
	idEvent *next;

	if ( !initialized ) {
		return false;
	}

	for( event = EventQueue.Next(); event != NULL; event = next ) {
		next = event->eventNode.Next();
		if( event->object == obj && evdef == event->eventdef ) {
			return true;
		}
	}

	return false;
}
// RAVEN END

/*
================
idEvent::ClearEventList
================
*/
void idEvent::ClearEventList( void ) {
	int i;

	//
	// initialize lists
	//
	FreeEvents.Clear();
	EventQueue.Clear();
   
	// 
	// add the events to the free list
	//
	for( i = 0; i < MAX_EVENTS; i++ ) {
		EventPool[ i ].Free();
	}
}

/*
================
idEvent::ServiceEvents
================
*/
void idEvent::ServiceEvents( void ) {
	gameLocal.NotePresentationGameplay( "idEvent::ServiceEvents" );
	idEvent		*event;
	int			num;
	intptr_t	args[ D_EVENT_MAXARGS ];
	size_t		offset;
	int			i;
	int			numargs;
	const char	*formatspec;
	trace_t		**tracePtr;
	const idEventDef *ev;
	byte		*data;
	const char  *materialName;

	num = 0;
	while( !EventQueue.IsListEmpty() ) {
		
#ifdef _XENON
		session->PacifierUpdate();
#endif
		
		event = EventQueue.Next();
		assert( event );

		if ( event->time > gameLocal.time ) {
			break;
		}

		// copy the data into the local args array and set up pointers
		ev = event->eventdef;
		formatspec = ev->GetArgFormat();
		numargs = ev->GetNumArgs();
		for( i = 0; i < numargs; i++ ) {
			offset = ev->GetArgOffset( i );
			data = event->data;
			switch( formatspec[ i ] ) {
			case D_EVENT_FLOAT :
				args[ i ] = *reinterpret_cast<int *>( &data[ offset ] );
				break;

			case D_EVENT_INTEGER :
			case D_EVENT_INTEGER64bit :
				args[ i ] = *reinterpret_cast<intptr_t *>( &data[ offset ] );
				break;

			case D_EVENT_VECTOR :
				*reinterpret_cast<idVec3 **>( &args[ i ] ) = reinterpret_cast<idVec3 *>( &data[ offset ] );
				break;

			case D_EVENT_STRING :
				*reinterpret_cast<const char **>( &args[ i ] ) = reinterpret_cast<const char *>( &data[ offset ] );
				break;
// RAVEN BEGIN
// abahr: type checking change as per Jim D.
			case D_EVENT_ENTITY :
				*reinterpret_cast<idEntity **>( &args[ i ] ) = reinterpret_cast< idEntityPtr<idEntity> * >( &data[ offset ] )->GetEntity();
				if ( *reinterpret_cast<idEntity **>( &args[ i ] ) == NULL ) {
					gameLocal.Warning( "idEvent::ServiceEvents : NULL entity passed in to event function that expects a non-NULL pointer on arg # %d on '%s' event.", i, ev->GetName() );
				}
				break;
 
			case D_EVENT_ENTITY_NULL :
				*reinterpret_cast<idEntity **>( &args[ i ] ) = reinterpret_cast< idEntityPtr<idEntity> * >( &data[ offset ] )->GetEntity();
				break;
// RAVEN END
			case D_EVENT_TRACE :
				tracePtr = reinterpret_cast<trace_t **>( &args[ i ] );
				if ( *reinterpret_cast<bool *>( &data[ offset ] ) ) {
					*tracePtr = reinterpret_cast<trace_t *>( &data[ offset + EVENT_TRACE_DATA_OFFSET ] );

					if ( ( *tracePtr )->c.material != NULL ) {
						// look up the material name to get the material pointer
						materialName = reinterpret_cast<const char *>( &data[ offset + EVENT_TRACE_MATERIAL_OFFSET ] );
						( *tracePtr )->c.material = declManager->FindMaterial( materialName, true );
					}
				} else {
					*tracePtr = NULL;
				}
				break;

			default:
				gameLocal.Error( "idEvent::ServiceEvents : Invalid arg format '%s' string for '%s' event.", formatspec, ev->GetName() );
			}
		}

		// the event is removed from its list so that if then object
		// is deleted, the event won't be freed twice
		event->eventNode.Remove();
		assert( event->object );
		
		// savegames can trash the object, so do this for safety
		if ( event->object ) {
			event->object->ProcessEventArgPtr( ev, args );
		}

#if 0
		// event functions may never leave return values on the FPU stack
		// enable this code to check if any event call left values on the FPU stack
		if ( !sys->FPU_StackIsEmpty() ) {
			gameLocal.Error( "idEvent::ServiceEvents %d: %s left a value on the FPU stack\n", num, ev->GetName() );
		}
#endif

		// return the event to the free list
		event->Free();

		// Don't allow ourselves to stay in here too long.  An abnormally high number
		// of events being processed is evidence of an infinite loop of events.
		num++;
		if ( num > MAX_EVENTSPERFRAME ) {
			gameLocal.Error( "Event overflow.  Possible infinite loop in script." );
		}
	}
}

/*
================
idEvent::Init
================
*/
void idEvent::Init( void ) {
	gameLocal.Printf( "Initializing event system\n" );

	if ( eventError ) {
		gameLocal.Error( "%s", eventErrorMsg );
	}

#ifdef CREATE_EVENT_CODE
	void CreateEventCallbackHandler();
	CreateEventCallbackHandler();
	gameLocal.Error( "Wrote event callback handler" );
#endif

	if ( initialized ) {
		gameLocal.Printf( "...already initialized\n" );
		ClearEventList();
		return;
	}

	ClearEventList();

	eventDataAllocator.Init();

	gameLocal.Printf( "...%i event definitions\n", idEventDef::NumEventCommands() );

	// the event system has started
	initialized = true;
}

/*
================
idEvent::Shutdown
================
*/
void idEvent::Shutdown( void ) {
	gameLocal.Printf( "Shutdown event system\n" );

	if ( !initialized ) {
		gameLocal.Printf( "...not started\n" );
		return;
	}

	ClearEventList();
	
	eventDataAllocator.Shutdown();

	// say it is now shutdown
	initialized = false;
}

/*
================
idEvent::Save
================
*/
void idEvent::Save( idSaveGame *savefile ) {
	idEvent	*event;

	savefile->WriteInt( EventQueue.Num() );

	event = EventQueue.Next();
	while( event != NULL ) {
		savefile->WriteInt( event->time );
		savefile->WriteString( event->eventdef->GetName() );
		savefile->WriteString( event->typeinfo->classname );
		savefile->WriteObject( event->object );
		savefile->WriteInt( EVENT_SAVE_FORMAT_TYPED );
		idEvent_SaveTypedArgs( savefile, event->eventdef, event->data );

		event = event->eventNode.Next();
	}
}

/*
================
idEvent::Restore
================
*/
void idEvent::Restore( idRestoreGame *savefile ) {
	int		i;
	int		num;
	int		argsize;
	idStr	name;
	idEvent	*event;
	int		previousTime = 0;

	savefile->ReadInt( num );
	if ( num < 0 || num > MAX_EVENTS ) {
		savefile->Error( "idEvent::Restore: invalid event count %d", num );
	}

	for ( i = 0; i < num; i++ ) {
		if ( FreeEvents.IsListEmpty() ) {
			gameLocal.Error( "idEvent::Restore : No more free events" );
		}

		event = FreeEvents.Next();
		event->eventNode.Remove();
		event->eventNode.AddToEnd( EventQueue );

		savefile->ReadInt( event->time );
		// ServiceEvents stops at the first future event, so preserve the
		// chronological queue invariant maintained by Schedule.
		if ( i > 0 && event->time < previousTime ) {
			savefile->Error( "idEvent::Restore: event %d is out of order (%d after %d)", i, event->time, previousTime );
		}
		previousTime = event->time;

		// read the event name
		savefile->ReadString( name );
		event->eventdef = idEventDef::FindEvent( name );
		if ( !event->eventdef ) {
			savefile->Error( "idEvent::Restore: unknown event '%s'", name.c_str() );
		}

		// read the classtype
		savefile->ReadString( name );
		event->typeinfo = idClass::GetClass( name );
		if ( !event->typeinfo ) {
			savefile->Error( "idEvent::Restore: unknown class '%s' on event '%s'", name.c_str(), event->eventdef->GetName() );
		}

		savefile->ReadObject( event->object, *event->typeinfo, event->eventdef->GetName() );
		if ( event->object == NULL ) {
			savefile->Error( "idEvent::Restore: event '%s' has a NULL target object", event->eventdef->GetName() );
		}
		if ( !event->object->RespondsTo( *event->eventdef ) ) {
			savefile->Error( "idEvent::Restore: target class '%s' does not support event '%s'", event->typeinfo->classname, event->eventdef->GetName() );
		}

		// read the args
		savefile->ReadInt( argsize );
		if ( argsize == EVENT_SAVE_FORMAT_TYPED ) {
			const size_t eventArgSize = event->eventdef->GetArgSize();
			if ( eventArgSize ) {
				if ( eventArgSize > ID_HEAP_MAX_SIZE ) {
					savefile->Error( "idEvent::Restore: event data is too large (%zu bytes) on event '%s'", eventArgSize, event->eventdef->GetName() );
					throw idAllocError( "restored idEvent data exceeds the 32-bit heap limit" );
				}
				event->data = eventDataAllocator.Alloc( static_cast<int>( eventArgSize ) );
				if ( event->data == NULL ) {
					savefile->Error( "idEvent::Restore: failed to allocate %zu bytes on event '%s'", eventArgSize, event->eventdef->GetName() );
					throw idAllocError( "restored idEvent data allocation failed" );
				}
				memset( event->data, 0, eventArgSize );
				idEvent_RestoreTypedArgs( savefile, event->eventdef, event->data );
			} else {
				event->data = NULL;
			}
			continue;
		}
		if ( argsize < 0 ) {
			savefile->Error( "idEvent::Restore: invalid arg size %d on event '%s'", argsize, event->eventdef->GetName() );
		}
		const size_t eventArgSize = event->eventdef->GetArgSize();
		size_t legacyOffsets[ D_EVENT_MAXARGS ] = {};
		const size_t legacyArgSize = idEvent_GetLegacyArgLayout( event->eventdef, legacyOffsets );
		if ( static_cast<size_t>( argsize ) != legacyArgSize ) {
			savefile->Error( "idEvent::Restore: legacy arg size (%zu) doesn't match saved arg size(%d) on event '%s'", legacyArgSize, argsize, event->eventdef->GetName() );
		}
		if ( argsize ) {
			if ( eventArgSize > ID_HEAP_MAX_SIZE ) {
				savefile->Error( "idEvent::Restore: event data is too large (%zu bytes) on event '%s'", eventArgSize, event->eventdef->GetName() );
				throw idAllocError( "restored legacy idEvent data exceeds the 32-bit heap limit" );
			}
			// Queue cleanup owns event->data. The legacy conversion buffer must
			// also unwind if reading or allocating the final arguments fails.
			const auto freeLegacyData = []( byte *data ) { Mem_Free( data ); };
			std::unique_ptr<byte, decltype( freeLegacyData )> legacyData(
				static_cast<byte *>( Mem_Alloc( static_cast<size_t>( argsize ), MA_EVENT ) ), freeLegacyData );
			if ( legacyData == NULL ) {
				savefile->Error( "idEvent::Restore: failed to allocate %d bytes for legacy event '%s'", argsize, event->eventdef->GetName() );
				throw idAllocError( "legacy idEvent data allocation failed" );
			}
			savefile->Read( legacyData.get(), argsize );
			event->data = eventDataAllocator.Alloc( static_cast<int>( eventArgSize ) );
			if ( event->data == NULL ) {
				savefile->Error( "idEvent::Restore: failed to allocate %zu bytes on legacy event '%s'", eventArgSize, event->eventdef->GetName() );
				throw idAllocError( "restored legacy idEvent data allocation failed" );
			}
			memset( event->data, 0, eventArgSize );
			idEvent_ConvertLegacyRawArgs( savefile, event->eventdef, event->data, legacyData.get(), legacyOffsets );
		} else {
			event->data = NULL;
		}
	}
}

#ifdef CREATE_EVENT_CODE
/*
================
CreateEventCallbackHandler
================
*/
void CreateEventCallbackHandler( void ) {
	int num;
	int count;
	int i, j, k;
	char argString[ D_EVENT_MAXARGS + 1 ];
	idStr string1;
	idStr string2;
	idFile *file;

	file = fileSystem->OpenFileWrite( "Callbacks.cpp" );

	file->Printf( "// generated file - see CREATE_EVENT_CODE\n\n" );

	for( i = 1; i <= D_EVENT_MAXARGS; i++ ) {

		file->Printf( "\t/*******************************************************\n\n\t\t%d args\n\n\t*******************************************************/\n\n", i );

		for ( j = 0; j < ( 1 << i ); j++ ) {
			for ( k = 0; k < i; k++ ) {
				argString[ k ] = j & ( 1 << k ) ? 'f' : 'i';
			}
			argString[ i ] = '\0';
			
			string1.Empty();
			string2.Empty();

			for( k = 0; k < i; k++ ) {
				if ( j & ( 1 << k ) ) {
					string1 += "const float";
					string2 += va( "*( float * )&data[ %d ]", k );
				} else {
					string1 += "const intptr_t";
					string2 += va( "data[ %d ]", k );
				}

				if ( k < i - 1 ) {
					string1 += ", ";
					string2 += ", ";
				}
			}

			file->Printf( "\tcase %d :\n\t\ttypedef void ( idClass::*eventCallback_%s_t )( %s );\n", ( 1 << ( i + D_EVENT_MAXARGS ) ) + j, argString, string1.c_str() );
			file->Printf( "\t\t( this->*( eventCallback_%s_t )callback )( %s );\n\t\tbreak;\n\n", argString, string2.c_str() );

		}
	}

	fileSystem->CloseFile( file );
}

#endif
