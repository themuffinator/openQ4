/*

Base class for all game objects.  Provides fast run-time type checking and run-time
instancing of objects.

*/

#ifndef __SYS_CLASS_H__
#define __SYS_CLASS_H__

class idClass;
class idTypeInfo;

// RAVEN BEGIN
extern const idEventDef EV_PostRestore;
// RAVEN END
extern const idEventDef EV_Remove;
extern const idEventDef EV_SafeRemove;

typedef void ( idClass::*eventCallback_t )( void );

template< class Type >
struct idEventFunc {
	const idEventDef	*event;
	eventCallback_t		function;
};

// The declaring type is stable even when a release linker folds identical
// empty member functions to the same address.
template< class Left, class Right >
struct idTypesAreSame {
	enum { value = false };
};

template< class Type >
struct idTypesAreSame<Type, Type> {
	enum { value = true };
};

template< class MemberPointer >
struct idMemberFunctionOwner;

template< class ReturnType, class Owner, class... Args >
struct idMemberFunctionOwner<ReturnType ( Owner::* )( Args... )> {
	typedef Owner type;
};

template< class ReturnType, class Owner, class... Args >
struct idMemberFunctionOwner<ReturnType ( Owner::* )( Args... ) const> {
	typedef Owner type;
};

template< class ExpectedOwner, class MemberPointer >
struct idMemberFunctionDeclaredHere {
	enum { value = idTypesAreSame<ExpectedOwner, typename idMemberFunctionOwner<MemberPointer>::type>::value };
};

// added & so gcc could compile this
#define EVENT( event, function )	{ &( event ), ( void ( idClass::* )( void ) )( &function ) },
#define END_CLASS					{ NULL, NULL } };

class idEventArg {
public:
	int			type;
	intptr_t	value;

	idEventArg() { type = D_EVENT_INTEGER; value = 0; };
	idEventArg(int data) { type = D_EVENT_INTEGER; value = data; };
	idEventArg(float data) { type = D_EVENT_FLOAT; value = *reinterpret_cast<int*>(&data); };
	idEventArg(const idVec3& data) { type = D_EVENT_VECTOR; value = reinterpret_cast<intptr_t>(&data); };
	idEventArg(const idStr& data) { type = D_EVENT_STRING; value = reinterpret_cast<intptr_t>(data.c_str()); };
	idEventArg(const char* data) { type = D_EVENT_STRING; value = reinterpret_cast<intptr_t>(data); };
	idEventArg(const class idEntity* data) { type = D_EVENT_ENTITY; value = reinterpret_cast<intptr_t>(data); };
	idEventArg(const trace_t* data) { type = D_EVENT_TRACE; value = reinterpret_cast<intptr_t>(data); };

// jmarshall - this can't be called from doomscript!
#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
	idEventArg(intptr_t data) { type = D_EVENT_INTEGER64bit; value = data; };
#else
	idEventArg(long long data) { type = D_EVENT_INTEGER64bit; value = static_cast<intptr_t>(data); };
#endif
// jmarshall end
};

class idAllocError : public idException {
public:
	idAllocError( const char *text = "" ) : idException( text ) {}
};

/***********************************************************************

  idClass

***********************************************************************/

/***********************************************************************

  idClassRegistrar

  Every CLASS_DECLARATION and ABSTRACT_DECLARATION leaves one of these at
  static initialization. idClass::RegisterClasses() registers the classes it
  names explicitly, then every class with a registrar, so classes a
  game-library layer links into the module exist as well.

***********************************************************************/

class idClassRegistrar {
public:
								idClassRegistrar( void ( *registerClass )( void ) );
	static void					RegisterAll( void );

private:
	void						( *registerClass )( void );
	idClassRegistrar *			next;

	static idClassRegistrar *	list;
};

/*
================
CLASS_PROTOTYPE

This macro must be included in the definition of any subclass of idClass.
It prototypes variables used in class instanciation and type checking.
Use this on single inheritance concrete classes only.
================
*/
#ifdef USE_STATIC_CLASS_CONSTRUCTION

#define CLASS_PROTOTYPE( nameofclass )									\
private:																\
	static	idTypeInfo						Type;						\
public:																	\
	static	void							RegisterClass( void );		\
	static	idClass							*CreateInstance( void );	\
	static	idTypeInfo						&GetClassType( void );		\
	virtual	idTypeInfo						*GetType( void ) const;		\
	static	idEventFunc<nameofclass>		eventCallbacks[]

#else

// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
#define CLASS_PROTOTYPE( nameofclass )									\
public:																\
	static	idTypeInfo						*Type;						\
public:																	\
	static	void							RegisterClass( void );		\
	static	idClass							*CreateInstance( void );	\
	static	idTypeInfo						&GetClassType( void );		\
	virtual	idTypeInfo						*GetType( void ) const;		\
	static	idEventFunc<nameofclass>		eventCallbacks[]
// RAVEN END

#endif // USE_STATIC_CLASS_CONSTRUCTION

/*
================
CLASS_DECLARATION

This macro must be included in the code to properly initialize variables
used in type checking and run-time instanciation.  It also defines the list
of events that the class responds to.  Take special care to ensure that the 
proper superclass is indicated or the run-time type information will be
incorrect.  Use this on concrete classes only.
================
*/

#ifdef USE_STATIC_CLASS_CONSTRUCTION

#define CLASS_DECLARATION( nameofsuperclass, nameofclass )											\
	idTypeInfo nameofclass::Type( #nameofclass, #nameofsuperclass,											\
			( idEventFunc<idClass> * )nameofclass::eventCallbacks,	nameofclass::CreateInstance, ( void ( idClass::* )( void ) )&nameofclass::Spawn,	\
			( rvStateFunc<idClass> * )nameofclass::stateCallbacks,																						\
			( void ( idClass::* )( idSaveGame * ) const )&nameofclass::Save, ( void ( idClass::* )( idRestoreGame * ) )&nameofclass::Restore,	\
			idMemberFunctionDeclaredHere<nameofclass, decltype( &nameofclass::Save )>::value,												\
			idMemberFunctionDeclaredHere<nameofclass, decltype( &nameofclass::Restore )>::value );										\
	void nameofclass::RegisterClass( void ) {														\
	}																								\
	void Register_##nameofclass( void ) {															\
		nameofclass::RegisterClass();																\
	}																								\
	idClass *nameofclass::CreateInstance( void ) {													\
		try {																						\
			RV_PUSH_SYS_HEAP_ID(RV_HEAP_ID_LEVEL);													\
			nameofclass *ptr = new nameofclass;														\
			RV_POP_HEAP();																			\
			ptr->FindUninitializedMemory();															\
			return ptr;																				\
		}																							\
		catch( idAllocError & ) {																	\
			return NULL;																			\
		}																							\
	}																								\
	idTypeInfo &nameofclass::GetClassType( void ) {													\
		return nameofclass::Type;																	\
	}																								\
	idTypeInfo *nameofclass::GetType( void ) const {												\
		return &nameofclass::Type;																	\
	}																								\
idEventFunc<nameofclass> nameofclass::eventCallbacks[] = {

#else

// RAVEN BEGIN
// bdube: Added states
// jnewquist: Use accessor for static class type
// mwhitlock: Dynamic memory consolidation
#define CLASS_DECLARATION( nameofsuperclass, nameofclass )											\
	idTypeInfo *nameofclass::Type = NULL;															\
	static idClassRegistrar nameofclass##_classRegistrar( &nameofclass::RegisterClass );			\
	void nameofclass::RegisterClass( void ) {														\
		static idTypeInfo type( #nameofclass, #nameofsuperclass,											\
			( idEventFunc<idClass> * )nameofclass::eventCallbacks,	nameofclass::CreateInstance, ( void ( idClass::* )( void ) )&nameofclass::Spawn,	\
			( rvStateFunc<idClass> * )nameofclass::stateCallbacks,																						\
			( void ( idClass::* )( idSaveGame * ) const )&nameofclass::Save, ( void ( idClass::* )( idRestoreGame * ) )&nameofclass::Restore,	\
			idMemberFunctionDeclaredHere<nameofclass, decltype( &nameofclass::Save )>::value,												\
			idMemberFunctionDeclaredHere<nameofclass, decltype( &nameofclass::Restore )>::value );										\
		nameofclass::Type = &type;																	\
	}																								\
	void Register_##nameofclass( void ) {															\
		nameofclass::RegisterClass();																\
	}																								\
	idClass *nameofclass::CreateInstance( void ) {													\
		try {																						\
			RV_PUSH_SYS_HEAP_ID(RV_HEAP_ID_LEVEL);													\
			nameofclass *ptr = new nameofclass;														\
			RV_POP_HEAP();																			\
			ptr->FindUninitializedMemory();															\
			return ptr;																				\
		}																							\
		catch( idAllocError & ) {																	\
			return NULL;																			\
		}																							\
	}																								\
	idTypeInfo &nameofclass::GetClassType( void ) {													\
		return *nameofclass::Type;																	\
	}																								\
	idTypeInfo *nameofclass::GetType( void ) const {												\
		return nameofclass::Type;																	\
	}																								\
idEventFunc<nameofclass> nameofclass::eventCallbacks[] = {
// RAVEN END

#endif // USE_STATIC_CLASS_CONSTRUCTION


/*
================
ABSTRACT_PROTOTYPE

This macro must be included in the definition of any abstract subclass of idClass.
It prototypes variables used in class instanciation and type checking.
Use this on single inheritance abstract classes only.
================
*/

#ifdef USE_STATIC_CLASS_CONSTRUCTION

#define ABSTRACT_PROTOTYPE( nameofclass )								\
private:																\
	static	idTypeInfo						Type;						\
public:																	\
	static	void							RegisterClass( void );		\
	static	idClass							*CreateInstance( void );	\
	static	idTypeInfo						&GetClassType( void );		\
	virtual	idTypeInfo						*GetType( void ) const;		\
	static	idEventFunc<nameofclass>		eventCallbacks[]

#else

// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
#define ABSTRACT_PROTOTYPE( nameofclass )								\
private:																\
	static	idTypeInfo						*Type;						\
public:																	\
	static	void							RegisterClass( void );		\
	static	idClass							*CreateInstance( void );	\
	static	idTypeInfo						&GetClassType( void );		\
	virtual	idTypeInfo						*GetType( void ) const;		\
	static	idEventFunc<nameofclass>		eventCallbacks[]
// RAVEN END

#endif // USE_STATIC_CLASS_CONSTRUCTION

/*
================
ABSTRACT_DECLARATION

This macro must be included in the code to properly initialize variables
used in type checking.  It also defines the list of events that the class
responds to.  Take special care to ensure that the proper superclass is
indicated or the run-time tyep information will be incorrect.  Use this
on abstract classes only.
================
*/

#ifdef USE_STATIC_CLASS_CONSTRUCTION

#define ABSTRACT_DECLARATION( nameofsuperclass, nameofclass )										\
	idTypeInfo nameofclass::Type( #nameofclass, #nameofsuperclass,									\
			( idEventFunc<idClass> * )nameofclass::eventCallbacks, nameofclass::CreateInstance, ( void ( idClass::* )( void ) )&nameofclass::Spawn,	\
			( rvStateFunc<idClass> * )nameofclass::stateCallbacks,																					\
			( void ( idClass::* )( idSaveGame * ) const )&nameofclass::Save, ( void ( idClass::* )( idRestoreGame * ) )&nameofclass::Restore,	\
			idMemberFunctionDeclaredHere<nameofclass, decltype( &nameofclass::Save )>::value,												\
			idMemberFunctionDeclaredHere<nameofclass, decltype( &nameofclass::Restore )>::value );										\
	void nameofclass::RegisterClass( void ) {														\
	}																								\
	void Register_##nameofclass( void ) {															\
		nameofclass::RegisterClass();																\
	}																								\
	idClass *nameofclass::CreateInstance( void ) {													\
		gameLocal.Error( "Cannot instanciate abstract class %s.", #nameofclass );					\
		return NULL;																				\
	}																								\
	idTypeInfo &nameofclass::GetClassType( void ) {													\
		return nameofclass::Type;																	\
	}																								\
	idTypeInfo *nameofclass::GetType( void ) const {												\
		return &nameofclass::Type;																	\
	}																								\
	idEventFunc<nameofclass> nameofclass::eventCallbacks[] = {

#else // CLASS_STATES_DECLARATION

// RAVEN BEGIN
// bdube: added states
// jnewquist: Use accessor for static class type 
#define ABSTRACT_DECLARATION( nameofsuperclass, nameofclass )										\
	idTypeInfo *nameofclass::Type = NULL;															\
	static idClassRegistrar nameofclass##_classRegistrar( &nameofclass::RegisterClass );			\
	void nameofclass::RegisterClass( void ) {														\
		static idTypeInfo type( #nameofclass, #nameofsuperclass,									\
			( idEventFunc<idClass> * )nameofclass::eventCallbacks, nameofclass::CreateInstance, ( void ( idClass::* )( void ) )&nameofclass::Spawn,	\
			( rvStateFunc<idClass> * )nameofclass::stateCallbacks,																					\
			( void ( idClass::* )( idSaveGame * ) const )&nameofclass::Save, ( void ( idClass::* )( idRestoreGame * ) )&nameofclass::Restore,	\
			idMemberFunctionDeclaredHere<nameofclass, decltype( &nameofclass::Save )>::value,												\
			idMemberFunctionDeclaredHere<nameofclass, decltype( &nameofclass::Restore )>::value );										\
		nameofclass::Type = &type;																	\
	}																								\
	void Register_##nameofclass( void ) {															\
		nameofclass::RegisterClass();																\
	}																								\
	idClass *nameofclass::CreateInstance( void ) {													\
		gameLocal.Error( "Cannot instanciate abstract class %s.", #nameofclass );					\
		return NULL;																				\
	}																								\
	idTypeInfo &nameofclass::GetClassType( void ) {													\
		return *nameofclass::Type;																	\
	}																								\
	idTypeInfo *nameofclass::GetType( void ) const {												\
		return nameofclass::Type;																	\
	}																								\
	idEventFunc<nameofclass> nameofclass::eventCallbacks[] = {

// RAVEN END

#endif // USE_STATIC_CLASS_CONSTRUCTION

typedef void ( idClass::*classSpawnFunc_t )( void );

class idSaveGame;
class idRestoreGame;

class idClass {
public:
	ABSTRACT_PROTOTYPE( idClass );

#ifdef ID_REDIRECT_NEWDELETE
#undef new
#endif
	void *						operator new( size_t );
	void *						operator new( size_t s, int, int, char *, int );
	void						operator delete( void * );
	void						operator delete( void *, int, int, char *, int );
#ifdef ID_REDIRECT_NEWDELETE
#define new ID_DEBUG_NEW
#endif

	virtual						~idClass();

	void						Spawn( void );
	void						CallSpawn( void );
	bool						IsType( const idTypeInfo &c ) const;
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	bool						IsType( const idTypeInfo *c ) const { return IsType(*c); }
// RAVEN END

// jmarshall
	virtual void			StateThreadChanged(void) { };
// jmarshall end

	const char *				GetClassname( void ) const;
	const char *				GetSuperclass( void ) const;
	void						FindUninitializedMemory( void );

	void						Save( idSaveGame *savefile ) const {};
	void						Restore( idRestoreGame *savefile ) {};

	bool						RespondsTo( const idEventDef &ev ) const;

// RAVEN BEGIN
// bdube: states
	stateResult_t				ProcessState			( const rvStateFunc<idClass>* state, const stateParms_t& parms );
	stateResult_t				ProcessState			( const char* name, const stateParms_t& parms );
	const rvStateFunc<idClass>*	FindState				( const char* name ) const;

// bdube: client entities
	virtual bool				IsClient ( void ) const;

// jnewquist: Register subclasses explicitly so they aren't dead-stripped
	static void					RegisterClasses( void );
// RAVEN END

	bool						PostEventMS( const idEventDef *ev, int time );
	bool						PostEventMS( const idEventDef *ev, int time, idEventArg arg1 );
	bool						PostEventMS( const idEventDef *ev, int time, idEventArg arg1, idEventArg arg2 );
	bool						PostEventMS( const idEventDef *ev, int time, idEventArg arg1, idEventArg arg2, idEventArg arg3 );
	bool						PostEventMS( const idEventDef *ev, int time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4 );
	bool						PostEventMS( const idEventDef *ev, int time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5 );
	bool						PostEventMS( const idEventDef *ev, int time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6 );
	bool						PostEventMS( const idEventDef *ev, int time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6, idEventArg arg7 );
	bool						PostEventMS( const idEventDef *ev, int time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6, idEventArg arg7, idEventArg arg8 );

	bool						PostEventSec( const idEventDef *ev, float time );
	bool						PostEventSec( const idEventDef *ev, float time, idEventArg arg1 );
	bool						PostEventSec( const idEventDef *ev, float time, idEventArg arg1, idEventArg arg2 );
	bool						PostEventSec( const idEventDef *ev, float time, idEventArg arg1, idEventArg arg2, idEventArg arg3 );
	bool						PostEventSec( const idEventDef *ev, float time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4 );
	bool						PostEventSec( const idEventDef *ev, float time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5 );
	bool						PostEventSec( const idEventDef *ev, float time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6 );
	bool						PostEventSec( const idEventDef *ev, float time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6, idEventArg arg7 );
	bool						PostEventSec( const idEventDef *ev, float time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6, idEventArg arg7, idEventArg arg8 );

	bool						ProcessEvent( const idEventDef *ev );
	bool						ProcessEvent( const idEventDef *ev, idEventArg arg1 );
	bool						ProcessEvent( const idEventDef *ev, idEventArg arg1, idEventArg arg2 );
	bool						ProcessEvent( const idEventDef *ev, idEventArg arg1, idEventArg arg2, idEventArg arg3 );
	bool						ProcessEvent( const idEventDef *ev, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4 );
	bool						ProcessEvent( const idEventDef *ev, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5 );
	bool						ProcessEvent( const idEventDef *ev, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6 );
	bool						ProcessEvent( const idEventDef *ev, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6, idEventArg arg7 );
	bool						ProcessEvent( const idEventDef *ev, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6, idEventArg arg7, idEventArg arg8 );

	bool						ProcessEventArgPtr( const idEventDef *ev, intptr_t *data );
	void						CancelEvents( const idEventDef *ev );
// RAVEN BEGIN
// abahr:
	bool						EventIsPosted( const idEventDef *ev ) const;

	void						Event_PostRestore( void ) {}
// RAVEN END
	void						Event_Remove( void );

	// Static functions
	static void					Init( void );
	static void					Shutdown( void );
	static idTypeInfo *			GetClass( const char *name );
	static void					DisplayInfo_f( const idCmdArgs &args );
	static void					ListClasses_f( const idCmdArgs &args );
	static idClass *			CreateInstance( const char *name );
	static int					GetNumTypes( void ) { return types.Num(); }
	static int					GetTypeNumBits( void ) { return typeNumBits; }
	static idTypeInfo *			GetType( int num );

// RAVEN BEGIN
// jscott: for memory profiling
	static size_t				GetUsedMemory( void ) { return( memused ); }

// bdube: debug info
	virtual void				GetDebugInfo		( debugInfoProc_t proc, void* userData );
// RAVEN END

private:
	classSpawnFunc_t			CallSpawnFunc( idTypeInfo *cls );

	bool						PostEventArgs( const idEventDef *ev, int time, int numargs, ... );
	bool						ProcessEventArgs( const idEventDef *ev, int numargs, ... );

	void						Event_SafeRemove( void );

	static bool					initialized;
	static idList<idTypeInfo *>	types;
	static idList<idTypeInfo *>	typenums;
	static int					typeNumBits;
	static size_t				memused;
	static int					numobjects;
	
// RAVEN BEGIN
// bdube: states
	CLASS_STATES_PROTOTYPE(idClass);
// RAVEN END	
};

// The object as a T, or NULL when it is NULL or not a T. A free function
// because a member call through a null pointer is undefined behaviour, and
// GCC and clang fold a `this` null check to true.
template< typename T >
ID_INLINE T *idCast( idClass *object ) {
	return ( object != NULL && object->IsType( T::Type ) ) ? static_cast<T *>( object ) : NULL;
}

template< typename T >
ID_INLINE const T *idCast( const idClass *object ) {
	return ( object != NULL && object->IsType( T::Type ) ) ? static_cast<const T *>( object ) : NULL;
}

/***********************************************************************

  idTypeInfo

***********************************************************************/

class idTypeInfo {
public:
	const char *				classname;
	const char *				superclass;
	idClass *					( *CreateInstance )( void );
	void						( idClass::*Spawn )( void );
	void						( idClass::*Save )( idSaveGame *savefile ) const;
	void						( idClass::*Restore )( idRestoreGame *savefile );
	bool						saveDeclaredHere;
	bool						restoreDeclaredHere;

// RAVEN BEGIN
// bdube: added
	rvStateFunc<idClass> *		stateCallbacks;
// RAVEN END

	idEventFunc<idClass> *		eventCallbacks;
	eventCallback_t *			eventMap;
	idTypeInfo *				super;
	idTypeInfo *				next;
	bool						freeEventMap;
	int							typeNum;
	int							lastChild;

	idHierarchy<idTypeInfo>		node;

								idTypeInfo( const char *classname, const char *superclass, 
												idEventFunc<idClass> *eventCallbacks, idClass *( *CreateInstance )( void ), void ( idClass::*Spawn )( void ),
// RAVEN BEGIN
// bdube: added
												rvStateFunc<idClass> *stateCallbacks,
// RAVEN END												
												void ( idClass::*Save )( idSaveGame *savefile ) const, void	( idClass::*Restore )( idRestoreGame *savefile ),
												bool saveDeclaredHere, bool restoreDeclaredHere );
								~idTypeInfo();

	void						Init( void );
	void						Shutdown( void );

	bool						IsType( const idTypeInfo &superclass ) const;
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	bool						IsType( const idTypeInfo *superclass ) const { return IsType(*superclass); }
// RAVEN END
	bool						RespondsTo( const idEventDef &ev ) const;
};

/*
================
idTypeInfo::IsType

Checks if the object's class is a subclass of the class defined by the 
passed in idTypeInfo.
================
*/
ID_INLINE bool idTypeInfo::IsType( const idTypeInfo &type ) const {
	return ( ( typeNum >= type.typeNum ) && ( typeNum <= type.lastChild ) );
}

/*
================
idTypeInfo::RespondsTo
================
*/
ID_INLINE bool idTypeInfo::RespondsTo( const idEventDef &ev ) const {
	assert( idEvent::initialized );
	if ( !eventMap[ ev.GetEventNum() ] ) {
		// we don't respond to this event
		return false;
	}

	return true;
}

/*
================
idClass::IsType

Checks if the object's class is a subclass of the class defined by the 
passed in idTypeInfo.
================
*/
ID_INLINE bool idClass::IsType( const idTypeInfo &superclass ) const {
	idTypeInfo *subclass;

	subclass = GetType();
	return subclass->IsType( superclass );
}

/*
================
idClass::RespondsTo
================
*/
ID_INLINE bool idClass::RespondsTo( const idEventDef &ev ) const {
	const idTypeInfo *c;

	assert( idEvent::initialized );
	c = GetType();
	return c->RespondsTo( ev );
}

/***********************************************************************

  idClassSubstitution

  Spawn-time class substitution for game-library layers (see openQ4's
  tools/build/game_layer.py). A layer that must change a stock class that
  content spawns by name registers a subclass as its replacement:

	SPAWNCLASS_SUBSTITUTION( rvMonsterGrunt, riMonsterGrunt )

  SPAWNCLASS_SUBSTITUTION_FOR_GAME scopes a replacement to the active
  fs_game. Installing a campaign never changes stock class spawning.

  Every entity spawned as the original class is then created as the
  replacement, which must derive from it. Only spawning consults the table;
  type queries, savegames and GetClass() keep seeing the real classes.

***********************************************************************/

class idClassSubstitution {
public:
								idClassSubstitution( const char *original, const char *replacement, const char *requiredGameDir = NULL );

	// the class to create when content asks for type
	static idTypeInfo *			Resolve( idTypeInfo *type );
	// called once the type system is up; an unknown or unrelated pair is fatal
	static void					Validate( void );

private:
	const char *				original;
	const char *				replacement;
	const char *				requiredGameDir;
	idClassSubstitution *		next;

	static idClassSubstitution *list;
};

#define SPAWNCLASS_SUBSTITUTION( original, replacement ) \
	static idClassSubstitution original##_##replacement##_substitution( #original, #replacement );

#define SPAWNCLASS_SUBSTITUTION_FOR_GAME( gameDir, original, replacement ) \
	static idClassSubstitution original##_##replacement##_substitution( #original, #replacement, gameDir );

#endif /* !__SYS_CLASS_H__ */
