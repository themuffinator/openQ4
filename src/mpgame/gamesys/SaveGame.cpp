
#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"

#include "framework/BuildVersion.h"
#include "framework/SaveGameReleases.h"

#if defined(_WIN32)
	#if defined(__has_include)
		#if __has_include("TypeInfo.h")
			#include "TypeInfo.h"
		#else
			#include "NoGameTypeInfo.h"
		#endif
	#else
		#include "TypeInfo.h"
	#endif
#else
	#include "NoGameTypeInfo.h"
#endif

// The generated stamp hashes every save-relevant game source, so it changes
// whenever one of them does. Keep it out of SaveGame.h: through Game_local.h it
// would reach the game PCH, and every such edit would rebuild the whole game.
#if defined( __has_include )
#if __has_include( "openq4_savegame_compat_generated.h" )
#include "openq4_savegame_compat_generated.h"
#endif
#endif

#ifndef OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH
#define OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH "standalone-openq4-game"
#endif

#ifndef OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT
#define OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT 0
#endif

/*
Save game related helper classes.

Save games are implemented in two classes, idSaveGame and idRestoreGame, that implement write/read functions for 
common types.  They're passed in to each entity and object for them to archive themselves.  Each class
implements save/restore functions for it's own data.  When restoring, all the objects are instantiated,
then the restore function is called on each, superclass first, then subclasses.

Pointers are restored by saving out an object index for each unique object pointer and adding them to a list of
objects that are to be saved.  Restore instantiates all the objects in the list before calling the Restore function
on each object so that the pointers returned are valid.  No object's restore function should rely on any other objects
being fully instantiated until after the restore process is complete.  Post restore fixup should be done by posting
events with 0 delay.

The savegame header will have the Game Name, Version, Map Name, and Player Persistent Info.

Changes in version make savegames incompatible, and the game will start from the beginning of the level with
the player's persistent info.

Changes to classes that don't need to break compatibilty can use the build number as the savegame version.
Later versions are responsible for restoring from previous versions by ignoring any unused data and initializing
variables that weren't in previous versions with safe information.

At the head of the save game is enough information to restore the player to the beginning of the level should the
file be unloadable in some way (for example, due to script changes).
*/

// RAVEN BEGIN
// jscott: sanity length check for strings
#define MAX_PRINT_MSG		4096
// RAVEN END

const char *OpenQ4SaveGameWireABI( void ) {
#if defined( _WIN32 )
	#define OPENQ4_SAVEGAME_ABI_OS "windows"
	#define OPENQ4_SAVEGAME_ABI_COMPILER "msvcabi"
#elif defined( __APPLE__ )
	#define OPENQ4_SAVEGAME_ABI_OS "macos"
	#define OPENQ4_SAVEGAME_ABI_COMPILER "itaniumabi"
#elif defined( __linux__ )
	#define OPENQ4_SAVEGAME_ABI_OS "linux"
	#define OPENQ4_SAVEGAME_ABI_COMPILER "itaniumabi"
#else
	#define OPENQ4_SAVEGAME_ABI_OS "unknownos"
	#define OPENQ4_SAVEGAME_ABI_COMPILER "unknownabi"
#endif
#if defined( _M_X64 ) || defined( __x86_64__ )
	#define OPENQ4_SAVEGAME_ABI_ARCH "x64"
#elif defined( _M_ARM64 ) || defined( __aarch64__ )
	#define OPENQ4_SAVEGAME_ABI_ARCH "arm64"
#elif defined( _M_IX86 ) || defined( __i386__ )
	#define OPENQ4_SAVEGAME_ABI_ARCH "x86"
#elif defined( _M_ARM ) || defined( __arm__ )
	#define OPENQ4_SAVEGAME_ABI_ARCH "arm32"
#else
	#define OPENQ4_SAVEGAME_ABI_ARCH "unknownarch"
#endif
#if defined( __BYTE_ORDER__ ) && defined( __ORDER_BIG_ENDIAN__ ) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	#define OPENQ4_SAVEGAME_ABI_ENDIAN "be"
#else
	#define OPENQ4_SAVEGAME_ABI_ENDIAN "le"
#endif
	return OPENQ4_SAVEGAME_ABI_OS "-" OPENQ4_SAVEGAME_ABI_COMPILER "-" OPENQ4_SAVEGAME_ABI_ARCH "-" OPENQ4_SAVEGAME_ABI_ENDIAN "-raw1";
#undef OPENQ4_SAVEGAME_ABI_ENDIAN
#undef OPENQ4_SAVEGAME_ABI_ARCH
#undef OPENQ4_SAVEGAME_ABI_COMPILER
#undef OPENQ4_SAVEGAME_ABI_OS
}

struct openQ4SaveGameSnapshot_t {
	int build;
	const char *sourceHash;
	int sourceFileCount;
	const char *wireABI;
};

// The two player liquid fields were added by different builds, so a save can
// legitimately carry the first and not the second. These are the engine build
// numbers of the commits that introduced each write, and a payload is only read
// as carrying a field when its layout build (the build it carries, or for a
// release that stamped build 1, that release's; see SaveGameReleases.h) is at
// or after that build:
//   swimSpeed                  openQ4-game 2cc5a61, 2026-08-13, build 661
//   nextLiquidSurfaceSoundTime openQ4-game d06a09d, 2026-08-19, build 721
// One boolean used to gate both against a single hard-coded v0.10 tuple, so every
// save written between those builds read a field the file does not contain and
// desynced the remainder of the restore.
static const int OPENQ4_SAVEGAME_BUILD_WITH_PLAYER_SWIM_SPEED = 661;
static const int OPENQ4_SAVEGAME_BUILD_WITH_PLAYER_LIQUID_SOUND = 721;

static const int MAX_SAVEGAME_OBJECTS = MAX_GENTITIES + MAX_CENTITIES + 4096;
static const int MAX_SAVEGAME_DICT_ENTRIES = 16384;

static int SaveGame_ObjectHashKey( const idClass *obj ) {
	const size_t value = reinterpret_cast<size_t>( obj );
	return static_cast<int>( value ^ ( value >> 4 ) ^ ( value >> ( sizeof( value ) * 4 ) ) );
}

static int SaveGame_FindObjectIndex( const idList<const idClass *> &objects, const idHashIndex &objectHash, const idClass *obj ) {
	if ( obj == NULL ) {
		return 0;
	}

	const int key = SaveGame_ObjectHashKey( obj );
	for ( int index = objectHash.First( key ); index >= 0; index = objectHash.Next( index ) ) {
		if ( index < objects.Num() && objects[index] == obj ) {
			return index;
		}
	}

	return -1;
}

static bool SaveGame_IsValidRenderBounds( const idBounds &bounds ) {
	for ( int i = 0; i < 3; i++ ) {
		const float boundsMin = bounds[0][i];
		const float boundsMax = bounds[1][i];
		if ( FLOAT_IS_NAN( boundsMin ) || FLOAT_IS_NAN( boundsMax ) ) {
			return false;
		}
		if ( bounds[0][i] > bounds[1][i] ) {
			return false;
		}
		if ( bounds[1][i] - bounds[0][i] >= MAX_BOUND_SIZE ) {
			return false;
		}
	}
	return true;
}

class idScopedSaveMemoryFile {
public:
	idScopedSaveMemoryFile( void ) {
		file = fileSystem->GetNewFileMemory();
	}

	~idScopedSaveMemoryFile( void ) {
		if ( file != NULL ) {
			fileSystem->CloseFile( file );
		}
	}

	idFile *GetFile( void ) const {
		return file;
	}

private:
	idScopedSaveMemoryFile( const idScopedSaveMemoryFile & );
	idScopedSaveMemoryFile &operator=( const idScopedSaveMemoryFile & );

	idFile *file;
};

static void SaveGame_DeleteUnrestoredObjects( idList<idClass *> &objectList ) {
	for ( int i = 1; i < objectList.Num(); i++ ) {
		idClass *object = objectList[ i ];
		objectList[ i ] = NULL;
		delete object;
	}
	objectList.Clear();
}

class idScopedUnrestoredObjectCleanup {
public:
	explicit idScopedUnrestoredObjectCleanup( idList<idClass *> &objectList ) : objects( objectList ), released( false ) {
	}

	~idScopedUnrestoredObjectCleanup( void ) {
		if ( !released ) {
			SaveGame_DeleteUnrestoredObjects( objects );
		}
	}

	void Release( void ) {
		released = true;
	}

private:
	idScopedUnrestoredObjectCleanup( const idScopedUnrestoredObjectCleanup & );
	idScopedUnrestoredObjectCleanup &operator=( const idScopedUnrestoredObjectCleanup & );

	idList<idClass *> &objects;
	bool released;
};

/*
================
idSaveGame::idSaveGame()
================
*/
idSaveGame::idSaveGame( idFile *savefile ) {

	file = savefile;
	openQ4SaveGameNextSyncId = 0;
	openQ4SaveGameSyncMarkersEnabled = false;

	// Put NULL at the start of the list so we can skip over it.
	objects.Clear();
	objects.Append( NULL );
	objectHash.Clear();
}

/*
================
idSaveGame::WriteChecked
================
*/
void idSaveGame::WriteChecked( int bytesWritten, int expected, const char *detail, int offset ) {
	if ( expected < 0 ) {
		common->Error( "idSaveGame: invalid negative write length %d while writing %s",
			expected, detail ? detail : "data" );
	}
	if ( bytesWritten != expected ) {
		common->Error( "idSaveGame: failed to write %s at offset %d (wrote %d of %d)",
			detail ? detail : "data", offset, bytesWritten, expected );
	}
}

/*
================
idSaveGame::~idSaveGame()
================
*/
idSaveGame::~idSaveGame( void ) {
	// SaveGame callers close successful writes explicitly.  Never attempt to
	// serialize from here: an error can unwind this object after the map has
	// already released the objects kept in the save registry.
}

/*
================
idSaveGame::Close
================
*/
void idSaveGame::Close( void ) {
	int i;
	if ( objects.Num() == 0 ) {
		return;
	}
	const int numObjects = objects.Num() - 1;

	WriteSoundCommands();

	// read trace models
	idClipModel::SaveTraceModels( this );

	for( i = 1; i < objects.Num(); i++ ) {
// RAVEN BEGIN
		WriteSyncId();
// RAVEN END
		CallSave_r( objects[ i ]->GetType(), objects[ i ] );
	}

	WriteSaveGameFooter( numObjects );

	objects.Clear();
	objectHash.Clear();

#ifdef ID_DEBUG_MEMORY
// RAVEN BEGIN
// jscott: don't use type info
//	idStr gameState = file->GetName();
//	gameState.StripFileExtension();
//	WriteGameState_f( idCmdArgs( va( "test %s_save", gameState.c_str() ), false ) );
// RAVEN END
#endif
}

/*
================
idSaveGame::WriteObjectList
================
*/
void idSaveGame::WriteObjectList( void ) {
	int i;

	if ( objects.Num() - 1 > MAX_SAVEGAME_OBJECTS ) {
		common->Error( "idSaveGame::WriteObjectList: invalid object count %d (max %d)", objects.Num() - 1, MAX_SAVEGAME_OBJECTS );
	}
	WriteInt( objects.Num() - 1 );
	for( i = 1; i < objects.Num(); i++ ) {
		if ( objects[ i ] == NULL ) {
			common->Error( "idSaveGame::WriteObjectList: NULL object at index %d", i );
		}
		WriteString( objects[ i ]->GetClassname() );
	}
}

/*
================
idSaveGame::CallSave_r
================
*/
void idSaveGame::CallSave_r( const idTypeInfo *cls, const idClass *obj ) {
	if ( cls->super ) {
		CallSave_r( cls->super, obj );
	}
	if ( !cls->saveDeclaredHere ) {
		// The class inherits its implementation, so its superclass frame already owns the payload.
		return;
	}

	WriteSyncId();
	( obj->*cls->Save )( this );
	WriteSyncId();
}

/*
================
idSaveGame::AddObject
================
*/
void idSaveGame::AddObject( const idClass *obj ) {
	if ( obj == NULL ) {
		return;
	}
	if ( SaveGame_FindObjectIndex( objects, objectHash, obj ) >= 0 ) {
		return;
	}
	const int index = objects.Append( obj );
	objectHash.Add( SaveGame_ObjectHashKey( obj ), index );
	if ( objects.Num() - 1 > MAX_SAVEGAME_OBJECTS ) {
		common->Error( "idSaveGame::AddObject: too many savegame objects (%d, max %d)", objects.Num() - 1, MAX_SAVEGAME_OBJECTS );
	}
}

/*
================
idSaveGame::WriteSyncId
================
*/
void idSaveGame::WriteSyncId( void ) {
	if ( !openQ4SaveGameSyncMarkersEnabled ) {
		return;
	}

	WriteInt( OPENQ4_SAVEGAME_SYNC_MAGIC );
	WriteInt( openQ4SaveGameNextSyncId++ );
}

/*
================
idSaveGame::Write
================
*/
void idSaveGame::Write( const void *buffer, int len ) {
	if ( len < 0 ) {
		common->Error( "idSaveGame::Write: invalid negative write length %d", len );
	}
	if ( len == 0 ) {
		return;
	}
	if ( buffer == NULL ) {
		common->Error( "idSaveGame::Write: null source for %d byte write", len );
	}
	const int offset = file->Tell();
	WriteChecked( file->Write( buffer, len ), len, "raw data", offset );
}

/*
================
idSaveGame::WriteInt
================
*/
void idSaveGame::WriteInt( const int value ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteInt( value ), static_cast<int>( sizeof( value ) ), "int", offset );
}

/*
================
idSaveGame::WriteJoint
================
*/
void idSaveGame::WriteJoint( const jointHandle_t value ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteInt( value ), static_cast<int>( sizeof( int ) ), "joint", offset );
}

/*
================
idSaveGame::WriteShort
================
*/
void idSaveGame::WriteShort( const short value ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteShort( value ), static_cast<int>( sizeof( value ) ), "short", offset );
}

/*
================
idSaveGame::WriteByte
================
*/
void idSaveGame::WriteByte( const byte value ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteUnsignedChar( value ), static_cast<int>( sizeof( value ) ), "byte", offset );
}

/*
================
idSaveGame::WriteSignedChar
================
*/
void idSaveGame::WriteSignedChar( const signed char value ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteChar( value ), static_cast<int>( sizeof( value ) ), "signed char", offset );
}

/*
================
idSaveGame::WriteFloat
================
*/
void idSaveGame::WriteFloat( const float value ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteFloat( value ), static_cast<int>( sizeof( value ) ), "float", offset );
}

/*
================
idSaveGame::WriteBool
================
*/
void idSaveGame::WriteBool( const bool value ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteBool( value ), static_cast<int>( sizeof( byte ) ), "bool", offset );
}

/*
================
idSaveGame::WriteString
================
*/
void idSaveGame::WriteString( const char *string ) {
	int len;

	if ( string == NULL ) {
		string = "";
	}

	len = idLib::SizeToInt( strlen( string ), "idSaveGame::WriteString" );

// RAVEN BEGIN
// jscott: added safety check for silly length strings
	if( len < 0 || len >= MAX_PRINT_MSG ) {

		common->Error( "idSaveGame::WriteString invalid string length (%d)", len );
	}
// RAVEN END

	WriteInt( len );
	if ( len > 0 ) {
		const int offset = file->Tell();
		WriteChecked( file->Write( string, len ), len, "string", offset );
	}
}

/*
================
idSaveGame::WriteVec2
================
*/
void idSaveGame::WriteVec2( const idVec2 &vec ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteVec2( vec ), static_cast<int>( sizeof( vec ) ), "vec2", offset );
}

/*
================
idSaveGame::WriteVec3
================
*/
void idSaveGame::WriteVec3( const idVec3 &vec ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteVec3( vec ), static_cast<int>( sizeof( vec ) ), "vec3", offset );
}

/*
================
idSaveGame::WriteVec4
================
*/
void idSaveGame::WriteVec4( const idVec4 &vec ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteVec4( vec ), static_cast<int>( sizeof( vec ) ), "vec4", offset );
}

/*
================
idSaveGame::WriteVec5
================
*/
void idSaveGame::WriteVec5( const idVec5 &vec ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteVec5( vec ), static_cast<int>( sizeof( vec ) ), "vec5", offset );
}

/*
================
idSaveGame::WriteVec6
================
*/
void idSaveGame::WriteVec6( const idVec6 &vec ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteVec6( vec ), static_cast<int>( sizeof( vec ) ), "vec6", offset );
}

/*
================
idSaveGame::WriteBounds
================
*/
void idSaveGame::WriteBounds( const idBounds &bounds ) {
	WriteVec3( bounds[0] );
	WriteVec3( bounds[1] );
}

/*
================
idSaveGame::WriteBounds
================
*/
void idSaveGame::WriteWinding( const idWinding &w )
{
	int i, num;
	num = w.GetNumPoints();
	if ( num < 0 || num > MAX_POINTS_ON_WINDING ) {
		common->Error( "idSaveGame::WriteWinding: invalid point count %d", num );
	}
	WriteInt( num );
	for ( i = 0; i < num; i++ ) {
		WriteVec5( w[i] );
	}
}


/*
================
idSaveGame::WriteMat3
================
*/
void idSaveGame::WriteMat3( const idMat3 &mat ) {
	const int offset = file->Tell();
	WriteChecked( file->WriteMat3( mat ), static_cast<int>( sizeof( mat ) ), "mat3", offset );
}

/*
================
idSaveGame::WriteAngles
================
*/
void idSaveGame::WriteAngles( const idAngles &angles ) {
	WriteFloat( angles.pitch );
	WriteFloat( angles.yaw );
	WriteFloat( angles.roll );
}

/*
================
idSaveGame::WriteObject
================
*/
void idSaveGame::WriteObject( const idClass *obj ) {
	int index;

	index = SaveGame_FindObjectIndex( objects, objectHash, obj );
	if ( index < 0 ) {
		// Match the retail serializer: transient references which are not part of
		// the saved object graph are restored as NULL.
		gameLocal.DPrintf( "idSaveGame::WriteObject - WriteObject FindIndex failed; writing NULL reference\n" );
		index = 0;
	}

	WriteInt( index );
}

/*
================
idSaveGame::WriteStaticObject
================
*/
void idSaveGame::WriteStaticObject( const idClass &obj ) {
// RAVEN BEGIN
	WriteSyncId();
// RAVEN END
	CallSave_r( obj.GetType(), &obj );
}

/*
================
idSaveGame::WriteDict
================
*/
void idSaveGame::WriteDict( const idDict *dict ) {
	int num;
	int i;
	const idKeyValue *kv;

// RAVEN BEGIN
	WriteSyncId();
// RAVEN END

	if ( !dict ) {
		WriteInt( -1 );
	} else {
		num = dict->GetNumKeyVals();
		if ( num > MAX_SAVEGAME_DICT_ENTRIES ) {
			common->Error( "idSaveGame::WriteDict: invalid key/value count %d (max %d)",
				num, MAX_SAVEGAME_DICT_ENTRIES );
		}
		WriteInt( num );
		for( i = 0; i < num; i++ ) {
			kv = dict->GetKeyVal( i );
			WriteString( kv->GetKey() );
			WriteString( kv->GetValue() );
		}
	}
}

/*
================
idSaveGame::WriteMaterial
================
*/
void idSaveGame::WriteMaterial( const idMaterial *material ) {
	if ( !material ) {
		WriteString( "" );
	} else {
		WriteString( material->GetName() );
	}
}

// RAVEN BEGIN
// bdube: material type
/*
================
idSaveGame::WriteMaterial
================
*/
void idSaveGame::WriteMaterialType ( const rvDeclMatType* materialType ) {
	if ( !materialType ) {
		WriteString( "" );
	} else {
		WriteString( materialType->GetName() );
	}
}

/*
================
idSaveGame::WriteTable
================
*/
void idSaveGame::WriteTable ( const idDeclTable* table ) {
	if ( !table ) {
		WriteString( "" );
	} else {
		WriteString( table->GetName() );
	}
}
// RAVEN END

/*
================
idSaveGame::WriteSkin
================
*/
void idSaveGame::WriteSkin( const idDeclSkin *skin ) {
	if ( !skin ) {
		WriteString( "" );
	} else {
		WriteString( skin->GetName() );
	}
}

/*
================
idSaveGame::WriteModelDef
================
*/
void idSaveGame::WriteModelDef( const idDeclModelDef *modelDef ) {
	if ( !modelDef ) {
		WriteString( "" );
	} else {
		WriteString( modelDef->GetName() );
	}
}

/*
================
idSaveGame::WriteSoundShader
================
*/
void idSaveGame::WriteSoundShader( const idSoundShader *shader ) {
	const char *name;

	if ( !shader ) {
		WriteString( "" );
	} else {
		name = shader->GetName();
		WriteString( name );
	}
}

/*
================
idSaveGame::WriteModel
================
*/
void idSaveGame::WriteModel( const idRenderModel *model ) {
	const char *name;

	if ( !model ) {
		WriteString( "" );
	} else {
		name = model->Name();
		WriteString( name );
	}
}

/*
================
idSaveGame::WriteUserInterface
================
*/
void idSaveGame::WriteUserInterface( const idUserInterface *ui, bool unique ) {
	const char *name;

// RAVEN BEGIN
	WriteSyncId();
// RAVEN END

	if ( !ui ) {
		WriteString( "" );
	} else {
		name = ui->Name();
		WriteString( name );
		WriteBool( unique );
		if ( ui->WriteToSaveGame( file ) == false ) {
			gameLocal.Error( "idSaveGame::WriteUserInterface: ui failed to write properly\n" );
		}
	}
}

// RAVEN BEGIN
// abahr
/*
================
idSaveGame::WriteExtrapolate
================
*/
void idSaveGame::WriteExtrapolate( const idExtrapolate<int>& extrap ) {
	WriteInt( (int)extrap.GetExtrapolationType() );
	WriteFloat(	extrap.GetStartTime() );
	WriteFloat(	extrap.GetDuration() );

	WriteInt( extrap.GetStartValue() );
	WriteInt( extrap.GetBaseSpeed() );
	WriteInt( extrap.GetSpeed() );
}

/*
================
idSaveGame::WriteExtrapolate
================
*/
void idSaveGame::WriteExtrapolate( const idExtrapolate<float>& extrap ) {
	WriteInt( (int)extrap.GetExtrapolationType() );
	WriteFloat(	extrap.GetStartTime() );
	WriteFloat(	extrap.GetDuration() );

	WriteFloat( extrap.GetStartValue() );
	WriteFloat( extrap.GetBaseSpeed() );
	WriteFloat( extrap.GetSpeed() );
}

/*
================
idSaveGame::WriteExtrapolate
================
*/
void idSaveGame::WriteExtrapolate( const idExtrapolate<idVec3>& extrap ) {
	WriteInt( (int)extrap.GetExtrapolationType() );
	WriteFloat(	extrap.GetStartTime() );
	WriteFloat(	extrap.GetDuration() );

	WriteVec3( extrap.GetStartValue() );
	WriteVec3( extrap.GetBaseSpeed() );
	WriteVec3( extrap.GetSpeed() );
}

/*
================
idSaveGame::WriteInterpolate
================
*/
void idSaveGame::WriteInterpolate( const idInterpolateAccelDecelLinear<int>& lerp ) {
	WriteFloat( lerp.GetStartTime() );
	WriteFloat( lerp.GetDuration() );
	WriteFloat( lerp.GetAcceleration() );
	WriteFloat( lerp.GetDeceleration() );
	
	WriteInt( lerp.GetStartValue() );
	WriteInt( lerp.GetEndValue() );
}

/*
================
idSaveGame::WriteInterpolate
================
*/
void idSaveGame::WriteInterpolate( const idInterpolateAccelDecelLinear<float>& lerp ) {
	WriteFloat( lerp.GetStartTime() );
	WriteFloat( lerp.GetDuration() );
	WriteFloat( lerp.GetAcceleration() );
	WriteFloat( lerp.GetDeceleration() );
	
	WriteFloat( lerp.GetStartValue() );
	WriteFloat( lerp.GetEndValue() );
}

/*
================
idSaveGame::WriteInterpolate
================
*/
void idSaveGame::WriteInterpolate( const idInterpolateAccelDecelLinear<idVec3>& lerp ) {
	WriteFloat( lerp.GetStartTime() );
	WriteFloat( lerp.GetDuration() );
	WriteFloat( lerp.GetAcceleration() );
	WriteFloat( lerp.GetDeceleration() );
	
	WriteVec3( lerp.GetStartValue() );
	WriteVec3( lerp.GetEndValue() );
}

/*
================
idSaveGame::WriteInterpolate
================
*/
void idSaveGame::WriteInterpolate( const idInterpolate<int>& lerp ) {
	WriteFloat( lerp.GetStartTime() );
	WriteFloat( lerp.GetDuration() );

	WriteInt( lerp.GetStartValue() );
	WriteInt( lerp.GetEndValue() );
}

/*
================
idSaveGame::WriteInterpolate
================
*/
void idSaveGame::WriteInterpolate( const idInterpolate<float>& lerp ) {
	WriteFloat( lerp.GetStartTime() );
	WriteFloat( lerp.GetDuration() );

	WriteFloat( lerp.GetStartValue() );
	WriteFloat( lerp.GetEndValue() );
}

/*
================
idSaveGame::WriteInterpolate
================
*/
void idSaveGame::WriteInterpolate( const idInterpolate<idVec3>& lerp ) {
	WriteFloat( lerp.GetStartTime() );
	WriteFloat( lerp.GetDuration() );

	WriteVec3( lerp.GetStartValue() );
	WriteVec3( lerp.GetEndValue() );
}

/*
================
idSaveGame::WriteRenderEffect
================
*/
void idSaveGame::WriteRenderEffect( const renderEffect_t &renderEffect ) {
	WriteSyncId();

	WriteFloat( renderEffect.startTime );
	WriteInt( renderEffect.suppressSurfaceInViewID );
	WriteInt( renderEffect.allowSurfaceInViewID );
	WriteInt( renderEffect.groupID );

	WriteVec3( renderEffect.origin );
	WriteMat3( renderEffect.axis );

	WriteVec3( renderEffect.gravity );
	WriteVec3( renderEffect.endOrigin );

	WriteFloat( renderEffect.attenuation );
	WriteBool( renderEffect.hasEndOrigin );
	WriteBool( renderEffect.loop );
	WriteBool( renderEffect.ambient );
	WriteBool( renderEffect.inConnectedArea );
	WriteInt( renderEffect.weaponDepthHackInViewID );
	WriteFloat( renderEffect.modelDepthHack );

	WriteInt( renderEffect.referenceSoundHandle );

	for( int ix = 0; ix < MAX_ENTITY_SHADER_PARMS; ++ix ) {
		WriteFloat( renderEffect.shaderParms[ ix ] );
	}

	if( renderEffect.declEffect ) {
		WriteString( renderEffect.declEffect->GetName() );
	} else {
		WriteString( "" );
	}
}

/*
================
idSaveGame::WriteFrustum
================
*/
void idSaveGame::WriteFrustum( const idFrustum& frustum ) {
// RAVEN BEGIN
	WriteSyncId();
// RAVEN END
	WriteVec3( frustum.GetOrigin() );
	WriteMat3( frustum.GetAxis() );
	WriteFloat( frustum.GetNearDistance() );
	WriteFloat( frustum.GetFarDistance() );
	WriteFloat( frustum.GetLeft() );
	WriteFloat( frustum.GetUp() );
}

/*
================
idSaveGame::WriteRenderEntity
================
*/
void idSaveGame::WriteRenderEntity( const renderEntity_t &renderEntity ) {
	int i;

	WriteSyncId();

	WriteModel( renderEntity.hModel );

	WriteInt( renderEntity.entityNum );
	WriteInt( renderEntity.bodyId );

	assert( renderEntity.bounds[0][0] <= renderEntity.bounds[1][0] ); 
	assert( renderEntity.bounds[0][1] <= renderEntity.bounds[1][1] );
	assert( renderEntity.bounds[0][2] <= renderEntity.bounds[1][2] );

	assert( renderEntity.bounds[1][0] - renderEntity.bounds[0][0] < MAX_BOUND_SIZE );
	assert( renderEntity.bounds[1][1] - renderEntity.bounds[0][1] < MAX_BOUND_SIZE );
	assert( renderEntity.bounds[1][2] - renderEntity.bounds[0][2] < MAX_BOUND_SIZE );

	WriteBounds( renderEntity.bounds );

	// callback is set by class's Restore function

	WriteInt( renderEntity.suppressSurfaceInViewID );
	WriteInt( renderEntity.suppressShadowInViewID );
	WriteInt( renderEntity.suppressShadowInLightID );
	WriteInt( renderEntity.allowSurfaceInViewID );

	WriteInt( renderEntity.suppressSurfaceMask );

	WriteVec3( renderEntity.origin );
	WriteMat3( renderEntity.axis );

	WriteMaterial( renderEntity.customShader );
	WriteMaterial( renderEntity.referenceShader );
	WriteMaterial( renderEntity.overlayShader );
	WriteSkin( renderEntity.customSkin );

	WriteInt( renderEntity.referenceSoundHandle );

	for( i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ ) {
		WriteFloat( renderEntity.shaderParms[ i ] );
	}

	for( i = 0; i < MAX_RENDERENTITY_GUI; i++ ) {
		WriteUserInterface( renderEntity.gui[ i ], renderEntity.gui[ i ] ? renderEntity.gui[ i ]->IsUniqued() : false );
	}

	WriteFloat( renderEntity.modelDepthHack );

	WriteBool( renderEntity.noSelfShadow );
	WriteBool( renderEntity.noShadow );
	WriteBool( renderEntity.noDynamicInteractions );
	WriteBool( renderEntity.forceUpdate );

	WriteInt( renderEntity.weaponDepthHackInViewID );
	WriteFloat( renderEntity.shadowLODDistance );
	WriteInt( renderEntity.suppressLOD );
}
// RAVEN END

/*
================
idSaveGame::WriteRenderLight
================
*/
void idSaveGame::WriteRenderLight( const renderLight_t &renderLight ) {
	int i;

// RAVEN BEGIN
	WriteSyncId();
// RAVEN END

	WriteMat3( renderLight.axis );
	WriteVec3( renderLight.origin );

	WriteInt( renderLight.suppressLightInViewID );
	WriteInt( renderLight.allowLightInViewID );
	WriteBool( renderLight.noShadows );
	WriteBool( renderLight.noSpecular );
	WriteBool( renderLight.noDynamicShadows );
	WriteBool( renderLight.pointLight );
	WriteBool( renderLight.parallel );
	WriteBool( renderLight.globalLight );

// RAVEN BEGIN
// dluetscher: added detail levels to render lights
	WriteFloat( renderLight.detailLevel );
// RAVEN END

	WriteVec3( renderLight.lightRadius );
	WriteVec3( renderLight.lightCenter );

	WriteVec3( renderLight.target );
	WriteVec3( renderLight.right );
	WriteVec3( renderLight.up );
	WriteVec3( renderLight.start );
	WriteVec3( renderLight.end );

	// only idLight has a prelightModel and it's always based on the entityname, so we'll restore it there
	// WriteModel( renderLight.prelightModel );

	WriteInt( renderLight.lightId );

	WriteMaterial( renderLight.shader );

	for( i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ ) {
		WriteFloat( renderLight.shaderParms[ i ] );
	}

// RAVEN BEGIN
	WriteInt( renderLight.referenceSoundHandle );
// RAVEN END
}

/*
================
idSaveGame::WriteRefSound
================
*/
void idSaveGame::WriteRefSound( const refSound_t &refSound ) {
// RAVEN BEGIN
	WriteSyncId();

	WriteInt( refSound.referenceSoundHandle );
// RAVEN END
	WriteVec3( refSound.origin );
// RAVEN BEGIN
	WriteVec3( refSound.velocity );
// RAVEN END
	WriteInt( refSound.listenerId );
	WriteSoundShader( refSound.shader );
	WriteFloat( refSound.diversity );
	WriteBool( refSound.waitfortrigger );

	WriteFloat( refSound.parms.minDistance );
	WriteFloat( refSound.parms.maxDistance );
	WriteFloat( refSound.parms.volume );
	WriteFloat( refSound.parms.shakes );
	WriteInt( refSound.parms.soundShaderFlags );
	WriteInt( refSound.parms.soundClass );
}

/*
================
idSaveGame::WriteRenderView
================
*/
void idSaveGame::WriteRenderView( const renderView_t &view ) {
	int i;

// RAVEN BEGIN
	WriteSyncId();
// RAVEN END

	WriteInt( view.viewID );
	WriteInt( view.x );
	WriteInt( view.y );
	WriteInt( view.width );
	WriteInt( view.height );

	WriteFloat( view.fov_x );
	WriteFloat( view.fov_y );
	WriteVec3( view.vieworg );
	WriteMat3( view.viewaxis );

	WriteBool( view.cramZNear );

	WriteInt( view.time );

	for( i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ ) {
		WriteFloat( view.shaderParms[ i ] );
	}
}

/*
===================
idSaveGame::WriteUsercmd
===================
*/
void idSaveGame::WriteUsercmd( const usercmd_t &usercmd ) {
// RAVEN BEGIN
	WriteSyncId();
// RAVEN END
	WriteInt( usercmd.gameFrame );
	WriteInt( usercmd.gameTime );
	WriteInt( usercmd.duplicateCount );
// RAVEN BEGIN
// ddynerman: larger button bitfield
	WriteShort( usercmd.buttons );
// RAVEN END
	WriteSignedChar( usercmd.forwardmove );
	WriteSignedChar( usercmd.rightmove );
	WriteSignedChar( usercmd.upmove );
	WriteShort( usercmd.angles[0] );
	WriteShort( usercmd.angles[1] );
	WriteShort( usercmd.angles[2] );
	WriteShort( usercmd.mx );
	WriteShort( usercmd.my );
	WriteSignedChar( usercmd.impulse );
	WriteByte( usercmd.flags );
	WriteInt( usercmd.sequence );
}

/*
===================
idSaveGame::WriteContactInfo
===================
*/
void idSaveGame::WriteContactInfo( const contactInfo_t &contactInfo ) {
	WriteInt( (int)contactInfo.type );
	WriteVec3( contactInfo.point );
	WriteVec3( contactInfo.normal );
	WriteFloat( contactInfo.dist );
	WriteInt( contactInfo.contents );
	WriteMaterial( contactInfo.material );
	WriteInt( contactInfo.modelFeature );
	WriteInt( contactInfo.trmFeature );
	WriteInt( contactInfo.entityNum );
	WriteInt( contactInfo.id );
	WriteMaterialType( contactInfo.materialType );
}

/*
===================
idSaveGame::WriteTrace
===================
*/
void idSaveGame::WriteTrace( const trace_t &trace ) {
// RAVEN BEGIN
	WriteSyncId();
// RAVEN END
	WriteFloat( trace.fraction );
	WriteVec3( trace.endpos );
	WriteMat3( trace.endAxis );
	WriteContactInfo( trace.c );
}

/*
===================
idSaveGame::WriteClipModel
===================
*/
void idSaveGame::WriteClipModel( const idClipModel *clipModel ) {
	if ( clipModel != NULL ) {
		WriteBool( true );
		clipModel->Save( this );
	} else {
		WriteBool( false );
	}
}

/*
===================
idSaveGame::WriteSoundCommands
===================
*/
void idSaveGame::WriteSoundCommands( void ) {
	soundSystem->WriteToSaveGame( SOUNDWORLD_GAME, file );
}

/*
======================
idSaveGame::WriteBuildNumber
======================
*/
void idSaveGame::WriteBuildNumber( const int value ) {
	openQ4SaveGameSyncMarkersEnabled = true;
	openQ4SaveGameNextSyncId = 0;

	WriteInt( OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC );
	WriteInt( OPENQ4_SAVEGAME_COMPATIBILITY_VERSION );
	WriteInt( value );
	WriteString( OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH );
	WriteInt( OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT );
	WriteString( OpenQ4SaveGameWireABI() );
}

/*
======================
idSaveGame::WriteSaveGameFooter
======================
*/
void idSaveGame::WriteSaveGameFooter( int numObjects ) {
	if ( !openQ4SaveGameSyncMarkersEnabled ) {
		return;
	}

	const int footerOffset = file->Tell();
	WriteInt( OPENQ4_SAVEGAME_FOOTER_MAGIC );
	WriteInt( OPENQ4_SAVEGAME_FOOTER_VERSION );
	WriteInt( footerOffset );
	WriteInt( numObjects );
	WriteInt( openQ4SaveGameNextSyncId );
}






/***********************************************************************

	idRestoreGame
	
***********************************************************************/

/*
================
idRestoreGame::RestoreGame
================
*/
thread_local idRestoreGame *idRestoreGame::activeRestore = NULL;

idRestoreGame::idRestoreGame( idFile *savefile ) {
	file = savefile;
	ownsObjects = false;
	previousRestore = NULL;
	buildNumber = 0;
	openQ4SaveGameLayoutBuild = 0;
	openQ4SaveGameCompatibilityVersion = 0;
	openQ4SaveGameCompatibilitySourceFileCount = 0;
	openQ4SaveGameNextSyncId = 0;
	openQ4SaveGameHasCompatibilityStamp = false;
	openQ4SaveGameCompatible = false;
	openQ4SaveGameSyncMarkersEnabled = false;
	openQ4SaveGameCompatibilityStamp.Clear();
	openQ4SaveGameCompatibilityError = "savegame compatibility header has not been read";
}

/*
================
idRestoreGame::~idRestoreGame()
================
*/
idRestoreGame::~idRestoreGame() {
	if ( ownsObjects ) {
		// An exception can leave objects outside the master tables. Shut the map
		// down while their renderer/script dependencies are still available.
		gameLocal.MapShutdown();
	}
	ReleaseObjects();
}

void idRestoreGame::BeginObjectOwnership( void ) {
	if ( ownsObjects ) {
		Error( "idRestoreGame: object ownership already active" );
	}
	objectHash.Clear();
	ownsObjects = true;
	previousRestore = activeRestore;
	activeRestore = this;
}

void idRestoreGame::TrackObject( int index ) {
	objectHash.Add( SaveGame_ObjectHashKey( objects[index] ), index );
}

void idRestoreGame::ObjectDestroyed( const idClass *object ) {
	if ( activeRestore == NULL ) {
		return;
	}
	// Entity attachments and map shutdown may delete objects independently of
	// the restore list. Retire every such pointer before cleanup can visit it.
	const int key = SaveGame_ObjectHashKey( object );
	for ( idRestoreGame *restore = activeRestore; restore != NULL; restore = restore->previousRestore ) {
		for ( int index = restore->objectHash.First( key ); index >= 0; index = restore->objectHash.Next( index ) ) {
			if ( index < restore->objects.Num() && restore->objects[index] == object ) {
				restore->objects[index] = NULL;
				restore->objectHash.Remove( key, index );
				break;
			}
		}
	}
}

void idRestoreGame::ReleaseObjects( void ) {
	idRestoreGame **link = &activeRestore;
	while ( *link != NULL && *link != this ) {
		link = &( *link )->previousRestore;
	}
	if ( *link == this ) {
		*link = previousRestore;
	}
	previousRestore = NULL;
	ownsObjects = false;
	objectHash.Clear();
	objects.Clear();
}

void idRestoreGame::PrepareMapShutdown( void ) {
	for ( idRestoreGame *restore = activeRestore; restore != NULL; restore = restore->previousRestore ) {
		for ( int i = 1; i < restore->objects.Num(); i++ ) {
			idClass *object = restore->objects[i];
			if ( object != NULL && object == gameLocal.frameCommandThread ) {
				gameLocal.frameCommandThread = NULL;
			}
			if ( object != NULL && object->IsType( idEntity::GetClassType() ) ) {
				static_cast<idEntity *>( object )->PrepareFailedRestoreShutdown();
			}
		}
	}
}

void idRestoreGame::DeleteOrphanedObjects( void ) {
	// MapClear has already released registered entities. Delete the remaining
	// factory objects before the script program, render world and clips go away.
	while ( activeRestore != NULL ) {
		activeRestore->DeleteObjects();
	}
}

/*
================
idRestoreGame::ReadChecked
================
*/
void idRestoreGame::ReadChecked( void *buffer, int len, const char *detail ) {
	if ( len < 0 ) {
		Error( "idRestoreGame: invalid negative read length %d while reading %s", len, detail ? detail : "data" );
	}
	if ( len == 0 ) {
		return;
	}
	if ( buffer == NULL ) {
		Error( "idRestoreGame: null destination while reading %s", detail ? detail : "data" );
	}
	const int offset = file->Tell();
	const int bytesRead = file->Read( buffer, len );
	if ( bytesRead != len ) {
		Error( "idRestoreGame: unexpected end of savegame while reading %s at offset %d (read %d of %d)",
			detail ? detail : "data", offset, bytesRead, len );
	}
}

// RAVEN BEGIN
/*
================
void idRestoreGame::CreateObjects
================
*/
void idRestoreGame::CreateObjects( void ) {
	int i, num;
	idStr classname;
	idTypeInfo *type;
	idList<idTypeInfo *> objectTypes;

	ReadInt( num );
	if ( num < 0 || num > MAX_SAVEGAME_OBJECTS ) {
		Error( "idRestoreGame::CreateObjects: invalid object count %d at offset %d", num, file->Tell() );
	}

	// Validate the complete type table before allocating any objects. This keeps a
	// corrupt later class name from leaving a partially constructed object graph.
	objectTypes.SetNum( num + 1 );
	objectTypes[ 0 ] = NULL;
	for ( i = 1; i < objectTypes.Num(); i++ ) {
		ReadString( classname );
		if ( classname.IsEmpty() ) {
			Error( "idRestoreGame::CreateObjects: empty class name for object %d", i );
		}
		type = idClass::GetClass( classname );
		if ( !type ) {
			Error( "idRestoreGame::CreateObjects: Unknown class '%s'", classname.c_str() );
		}
		// GameLocal saves only these three object families. Other registered
		// classes (weapons, physics, etc.) need an initialized owner or embedded
		// state; a factory-created instance is not a valid standalone save object.
		if ( !type->IsType( idEntity::GetClassType() ) &&
			 !type->IsType( idThread::GetClassType() ) &&
			 !type->IsType( rvClientEntity::GetClassType() ) ) {
			Error( "idRestoreGame::CreateObjects: unsupported object-table class '%s' for object %d", classname.c_str(), i );
		}
		objectTypes[ i ] = type;
	}

	BeginObjectOwnership();
	objects.SetNum( num + 1 );
	memset( objects.Ptr(), 0, sizeof( objects[ 0 ] ) * objects.Num() );
	idScopedUnrestoredObjectCleanup cleanup( objects );

	for ( i = 1; i < objects.Num(); i++ ) {
		objects[ i ] = objectTypes[ i ]->CreateInstance();
		if ( objects[ i ] == NULL ) {
			Error( "idRestoreGame::CreateObjects: failed to create class '%s'", objectTypes[ i ]->classname );
		}
		TrackObject( i );
	}
	cleanup.Release();
}

/*
================
void idRestoreGame::RestoreObjects
================
*/
void idRestoreGame::RestoreObjects( void ) {
	int i;

	ReadSoundCommands();

	// read trace models
	idClipModel::RestoreTraceModels( this );

	// restore all the objects
	for( i = 1; i < objects.Num(); i++ ) {
		ReadSyncId( "Restore objects", objects[ i ]->GetClassname() );
		CallRestore_r( objects[ i ]->GetType(), objects[ i ] );
	}

	ReadSaveGameFooter();

	// All forward references are now populated. Validate the complete binding
	// teams before any visual refresh or gameplay can traverse them.
	for ( i = 1; i < objects.Num(); i++ ) {
		if ( objects[i]->IsType( idEntity::GetClassType() ) ) {
			static_cast<idEntity *>( objects[i] )->ValidateRestoredBindings( this );
		}
		if ( objects[i]->IsType( idThread::GetClassType() ) ) {
			static_cast<idThread *>( objects[i] )->ValidateRestoreOwnership( this );
		}
	}

	// regenerate render entities and render lights because are not saved
	for( i = 1; i < objects.Num(); i++ ) {
		if ( objects[ i ]->IsType( idEntity::GetClassType() ) ) {
			idEntity *ent = static_cast<idEntity *>( objects[ i ] );
			ent->UpdateVisuals();
			ent->Present();
		}
	}
}
// RAVEN END

/*
====================
void idRestoreGame::DeleteObjects
====================
*/
void idRestoreGame::DeleteObjects( void ) {
	PrepareMapShutdown();
	SaveGame_DeleteUnrestoredObjects( objects );
	ReleaseObjects();
}

/*
================
idRestoreGame::Error
================
*/
void idRestoreGame::Error( const char *fmt, ... ) {
	va_list	argptr;
	char	text[ 1024 ];

	va_start( argptr, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );
	text[ sizeof( text ) - 1 ] = '\0';

	// During a load, Common throws before closing the file or freeing the world.
	// The restore owner unwinds the partial map; ObjectDestroyed retires deleted
	// pointers and DeleteOrphanedObjects releases the remainder.

	gameLocal.Error( "%s", text );
}

/*
================
idRestoreGame::CallRestore_r
================
*/
void idRestoreGame::CallRestore_r( const idTypeInfo *cls, idClass *obj ) {
	if ( cls->super ) {
		CallRestore_r( cls->super, obj );
	}
	if ( !cls->restoreDeclaredHere ) {
		// The class inherits its implementation, so its superclass frame already consumed the payload.
		return;
	}
	if ( openQ4SaveGameSyncMarkersEnabled && idStr::Icmp( cls->classname, "idPhysics" ) == 0 &&
		 !HasNextSerializedEmptyClassFrame() ) {
		// Older optimized MSVC links folded the empty idPhysics and idClass Restore
		// functions together. Their writers consequently omitted this empty frame.
		return;
	}
	ReadSyncId( "Callrestore_r start ", cls->classname );
	( obj->*cls->Restore )( this );
	ReadSyncId( "Callrestore_r end ", cls->classname );
}

/*
================
idRestoreGame::HasNextSerializedEmptyClassFrame
================
*/
bool idRestoreGame::HasNextSerializedEmptyClassFrame( void ) {
	const int offset = file->Tell();
	int startMarker = 0;
	int startSyncId = 0;
	int endMarker = 0;
	int endSyncId = 0;

	const bool complete =
		file->ReadInt( startMarker ) == static_cast<int>( sizeof( startMarker ) ) &&
		file->ReadInt( startSyncId ) == static_cast<int>( sizeof( startSyncId ) ) &&
		file->ReadInt( endMarker ) == static_cast<int>( sizeof( endMarker ) ) &&
		file->ReadInt( endSyncId ) == static_cast<int>( sizeof( endSyncId ) );
	if ( file->Seek( offset, FS_SEEK_SET ) == -1 ) {
		Error( "idRestoreGame: failed to restore the save stream position after class-frame lookahead at offset %d", offset );
	}

	return complete &&
		startMarker == OPENQ4_SAVEGAME_SYNC_MAGIC &&
		startSyncId == openQ4SaveGameNextSyncId &&
		endMarker == OPENQ4_SAVEGAME_SYNC_MAGIC &&
		endSyncId == openQ4SaveGameNextSyncId + 1;
}

/*
================
idRestoreGame::ReadSyncId
================
*/
void idRestoreGame::ReadSyncId( const char *detail, const char *classname ) {
	if ( !openQ4SaveGameSyncMarkersEnabled ) {
		return;
	}

	const int offset = file->Tell();
	int marker;
	int syncId;
	ReadInt( marker );
	ReadInt( syncId );

	if ( marker != OPENQ4_SAVEGAME_SYNC_MAGIC ) {
		Error( "idRestoreGame::ReadSyncId: marker mismatch while reading %s%s%s at offset %d (got 0x%08x, expected 0x%08x)",
			detail ? detail : "data",
			classname ? " for " : "",
			classname ? classname : "",
			offset,
			marker,
			OPENQ4_SAVEGAME_SYNC_MAGIC );
	}
	if ( syncId != openQ4SaveGameNextSyncId ) {
		Error( "idRestoreGame::ReadSyncId: sequence mismatch while reading %s%s%s at offset %d (got %d, expected %d)",
			detail ? detail : "data",
			classname ? " for " : "",
			classname ? classname : "",
			offset,
			syncId,
			openQ4SaveGameNextSyncId );
	}

	openQ4SaveGameNextSyncId++;
}

/*
================
idRestoreGame::Read
================
*/
void idRestoreGame::Read( void *buffer, int len ) {
	ReadChecked( buffer, len, "raw data" );
}

/*
================
idRestoreGame::ReadInt
================
*/
void idRestoreGame::ReadInt( int &value ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadInt( value );
	if ( bytesRead != sizeof( value ) ) {
		Error( "idRestoreGame: unexpected end of savegame while reading int at offset %d (read %d of %d)",
			offset, bytesRead, static_cast<int>( sizeof( value ) ) );
	}
}

/*
================
idRestoreGame::ReadJoint
================
*/
void idRestoreGame::ReadJoint( jointHandle_t &value ) {
	int joint;
	ReadInt( joint );
	value = static_cast<jointHandle_t>( joint );
}

/*
================
idRestoreGame::ReadShort
================
*/
void idRestoreGame::ReadShort( short &value ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadShort( value );
	if ( bytesRead != sizeof( value ) ) {
		Error( "idRestoreGame: unexpected end of savegame while reading short at offset %d (read %d of %d)",
			offset, bytesRead, static_cast<int>( sizeof( value ) ) );
	}
}

/*
================
idRestoreGame::ReadByte
================
*/
void idRestoreGame::ReadByte( byte &value ) {
	ReadChecked( &value, sizeof( value ), "byte" );
}

/*
================
idRestoreGame::ReadSignedChar
================
*/
void idRestoreGame::ReadSignedChar( signed char &value ) {
	ReadChecked( &value, sizeof( value ), "signed char" );
}

/*
================
idRestoreGame::ReadFloat
================
*/
void idRestoreGame::ReadFloat( float &value ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadFloat( value );
	if ( bytesRead != sizeof( value ) ) {
		Error( "idRestoreGame: unexpected end of savegame while reading float at offset %d (read %d of %d)",
			offset, bytesRead, static_cast<int>( sizeof( value ) ) );
	}
}

/*
================
idRestoreGame::ReadBool
================
*/
void idRestoreGame::ReadBool( bool &value ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadBool( value );
	if ( bytesRead != sizeof( byte ) ) {
		Error( "idRestoreGame: unexpected end of savegame while reading bool at offset %d (read %d of %d)",
			offset, bytesRead, static_cast<int>( sizeof( byte ) ) );
	}
}

/*
================
idRestoreGame::ReadString
================
*/
void idRestoreGame::ReadString( idStr &string ) {
	int len;

	ReadInt( len );
// RAVEN BEGIN
// jscott: added max check - should be big enough
	if ( len < 0 || len >= MAX_PRINT_MSG ) {
		Error( "idRestoreGame::ReadString: invalid length (%d)", len );
// RAVEN END
	}

	const int stringOffset = file->Tell();
	const int fileLength = file->Length();
	if ( fileLength > 0 && stringOffset >= 0 && len > fileLength - stringOffset ) {
		Error( "idRestoreGame::ReadString: length %d exceeds remaining savegame bytes %d at offset %d",
			len, fileLength - stringOffset, stringOffset );
	}

	string.Fill( ' ', len );
	if ( len > 0 ) {
		ReadChecked( &string[0], len, "string" );
	}
}

/*
================
idRestoreGame::ReadVec2
================
*/
void idRestoreGame::ReadVec2( idVec2 &vec ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadVec2( vec );
	if ( bytesRead != sizeof( vec ) ) {
		Error( "idRestoreGame: unexpected end of savegame while reading vec2 at offset %d (read %d of %d)",
			offset, bytesRead, static_cast<int>( sizeof( vec ) ) );
	}
}

/*
================
idRestoreGame::ReadVec3
================
*/
void idRestoreGame::ReadVec3( idVec3 &vec ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadVec3( vec );
	if ( bytesRead != sizeof( vec ) ) {
		Error( "idRestoreGame: unexpected end of savegame while reading vec3 at offset %d (read %d of %d)",
			offset, bytesRead, static_cast<int>( sizeof( vec ) ) );
	}
}

/*
================
idRestoreGame::ReadVec4
================
*/
void idRestoreGame::ReadVec4( idVec4 &vec ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadVec4( vec );
	if ( bytesRead != sizeof( vec ) ) {
		Error( "idRestoreGame: unexpected end of savegame while reading vec4 at offset %d (read %d of %d)",
			offset, bytesRead, static_cast<int>( sizeof( vec ) ) );
	}
}

/*
================
idRestoreGame::ReadVec5
================
*/
void idRestoreGame::ReadVec5( idVec5 &vec ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadVec5( vec );
	if ( bytesRead != sizeof( vec ) ) {
		Error( "idRestoreGame: unexpected end of savegame while reading vec5 at offset %d (read %d of %d)",
			offset, bytesRead, static_cast<int>( sizeof( vec ) ) );
	}
}

/*
================
idRestoreGame::ReadVec6
================
*/
void idRestoreGame::ReadVec6( idVec6 &vec ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadVec6( vec );
	if ( bytesRead != sizeof( vec ) ) {
		Error( "idRestoreGame: unexpected end of savegame while reading vec6 at offset %d (read %d of %d)",
			offset, bytesRead, static_cast<int>( sizeof( vec ) ) );
	}
}

/*
================
idRestoreGame::ReadBounds
================
*/
void idRestoreGame::ReadBounds( idBounds &bounds ) {
	ReadVec3( bounds[0] );
	ReadVec3( bounds[1] );
}

/*
================
idRestoreGame::ReadWinding
================
*/
void idRestoreGame::ReadWinding( idWinding &w )
{
	int i, num;
	ReadInt( num );
	if ( num < 0 || num > MAX_POINTS_ON_WINDING ) {
		Error( "idRestoreGame::ReadWinding: invalid point count %d at offset %d", num, file->Tell() );
	}
	w.SetNumPoints( num );
	for ( i = 0; i < num; i++ ) {
		ReadVec5( w[i] );
	}
}

/*
================
idRestoreGame::ReadMat3
================
*/
void idRestoreGame::ReadMat3( idMat3 &mat ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadMat3( mat );
	if ( bytesRead != sizeof( mat ) ) {
		Error( "idRestoreGame: unexpected end of savegame while reading mat3 at offset %d (read %d of %d)",
			offset, bytesRead, static_cast<int>( sizeof( mat ) ) );
	}
}

/*
================
idRestoreGame::ReadAngles
================
*/
void idRestoreGame::ReadAngles( idAngles &angles ) {
	ReadFloat( angles.pitch );
	ReadFloat( angles.yaw );
	ReadFloat( angles.roll );
}

/*
================
idRestoreGame::ReadObject
================
*/
void idRestoreGame::ReadObject( idClass *&obj ) {
	ReadObject( obj, idClass::GetClassType(), "idClass" );
}

/*
================
idRestoreGame::ReadObject
================
*/
void idRestoreGame::ReadObject( idClass *&obj, const idTypeInfo &expectedType, const char *detail ) {
	int index;
	const int offset = file->Tell();

	obj = NULL;
	ReadInt( index );
	if ( ( index < 0 ) || ( index >= objects.Num() ) ) {
		Error( "idRestoreGame::ReadObject: invalid object index %d (count %d, offset %d)",
			index, objects.Num(), offset );
	}
	obj = objects[ index ];
	if ( index != 0 && obj == NULL ) {
		Error( "idRestoreGame::ReadObject: unresolved object index %d while restoring %s (offset %d)",
			index, detail ? detail : expectedType.classname, offset );
	}
	if ( obj != NULL && !obj->IsType( expectedType ) ) {
		const char *actualClass = obj->GetClassname();
		obj = NULL;
		Error( "idRestoreGame::ReadObject: object index %d has type '%s', expected '%s' while restoring %s (offset %d)",
			index, actualClass, expectedType.classname, detail ? detail : expectedType.classname, offset );
	}
}

/*
================
idRestoreGame::ReadStaticObject
================
*/
void idRestoreGame::ReadStaticObject( idClass &obj ) {
// RAVEN BEGIN
	ReadSyncId( "ReadStaticObject", obj.GetClassname() );
// RAVEN END

	CallRestore_r( obj.GetType(), &obj );

// RAVEN BEGIN
	obj.PostEventMS( &EV_PostRestore, 0 );
// RAVEN END
}

/*
================
idRestoreGame::ReadDict
================
*/
void idRestoreGame::ReadDict( idDict *dict ) {
	int num;
	int i;
	idStr key;
	idStr value;

// RAVEN BEGIN
	ReadSyncId( "ReadDict" );
// RAVEN END

	ReadInt( num );

	if ( num == -1 ) {
		if ( dict != NULL ) {
			dict->Clear();
		}
		return;
	}
	if ( num < -1 || num > MAX_SAVEGAME_DICT_ENTRIES ) {
		Error( "idRestoreGame::ReadDict: invalid key/value count %d at offset %d", num, file->Tell() );
	}
	if ( dict == NULL ) {
		Error( "idRestoreGame::ReadDict: NULL dictionary for %d key/value pairs", num );
	}

	dict->Clear();
	for( i = 0; i < num; i++ ) {
		ReadString( key );
		ReadString( value );
		dict->Set( key, value );
	}
}

/*
================
idRestoreGame::ReadMaterial
================
*/
void idRestoreGame::ReadMaterial( const idMaterial *&material ) {
	idStr name;

	ReadString( name );
	if ( !name.Length() ) {
		material = NULL;
	} else {
		material = declManager->FindMaterial( name );
	}
}

// RAVEN BEGIN
// bdube: material type
/*
================
idRestoreGame::ReadMaterialType
================
*/
void idRestoreGame::ReadMaterialType ( const rvDeclMatType* &materialType ) {
	idStr name;

	ReadString( name );
	if ( !name.Length() ) {
		materialType = NULL;
	} else {
		materialType = declManager->FindMaterialType ( name );
	}
}

/*
================
idRestoreGame::ReadTable
================
*/
void idRestoreGame::ReadTable  ( const idDeclTable* &table ) {
	idStr name;

	ReadString( name );
	if ( !name.Length() ) {
		table = NULL;
	} else {
		table = declManager->FindTable( name );
	}
}

// RAVEN END

/*
================
idRestoreGame::ReadSkin
================
*/
void idRestoreGame::ReadSkin( const idDeclSkin *&skin ) {
	idStr name;

	ReadString( name );
	if ( !name.Length() ) {
		skin = NULL;
	} else {
		skin = declManager->FindSkin( name );
	}
}

/*
================
idRestoreGame::ReadSoundShader
================
*/
void idRestoreGame::ReadSoundShader( const idSoundShader *&shader ) {
	idStr name;

	ReadString( name );
	if ( !name.Length() ) {
		shader = NULL;
	} else {
		shader = declManager->FindSound( name );
	}
}

/*
================
idRestoreGame::ReadModelDef
================
*/
void idRestoreGame::ReadModelDef( const idDeclModelDef *&modelDef ) {
	idStr name;

	ReadString( name );
	if ( !name.Length() ) {
		modelDef = NULL;
	} else {
		modelDef = static_cast<const idDeclModelDef *>( declManager->FindType( DECL_MODELDEF, name, false ) );
	}
}

/*
================
idRestoreGame::ReadModel
================
*/
void idRestoreGame::ReadModel( idRenderModel *&model ) {
	idStr name;

	ReadString( name );
	if ( !name.Length() ) {
		model = NULL;
	} else {
		model = renderModelManager->FindModel( name );
	}
}

/*
================
idRestoreGame::ReadUserInterface
================
*/
// RAVEN BEGIN
void idRestoreGame::ReadUserInterface( idUserInterface *&ui, const idDict *args ) {
// RAVEN END
	idStr name;

// RAVEN BEGIN
	ReadSyncId( "ReadUserInterface" );
// RAVEN END

	ReadString( name );
	if ( !name.Length() ) {
		ui = NULL;
	} else {
		bool unique;
		ReadBool( unique );
		ui = uiManager->FindGui( name, true, unique );
		if ( ui == NULL ) {
			// This record owns an unframed GUI state payload. Reject it here;
			// optional menu loads during map preparation may still fall back.
			Error( "idRestoreGame::ReadUserInterface: could not load serialized GUI '%s'; aborting before its positional state payload can desynchronize the stream",
				name.c_str() );
		}
		const openQ4SaveGameGuiRevision_t *revision = OpenQ4_FindSaveGameGuiRevision( name, openQ4SaveGameLayoutBuild );
		if ( ui && revision != NULL && !GuiRecordNamesWindow( revision->currentWindow ) ) {
			ReadUserInterfaceRevision( ui, *revision );
			UpdateGuiParms( ui, args );
			return;
		}
		if ( ui ) {
			if ( ui->ReadFromSaveGame( file ) == false ) {
				Error( "idSaveGame::ReadUserInterface: ui failed to read properly\n" );
			} else {
// RAVEN BEGIN
				UpdateGuiParms( ui, args );
// RAVEN END
			}
		}
	}
}

/*
================
idRestoreGame::GuiRecordNamesWindow

Whether the GUI record starting here saves a window of this name. GUI records
hold no sync markers, so the next one bounds the search; the file position is
left where it was.
================
*/
bool idRestoreGame::GuiRecordNamesWindow( const char *windowName ) {
	static const int SCAN_CHUNK_BYTES = 64 * 1024;
	static const int SCAN_MAX_BYTES = 16 * 1024 * 1024;
	if ( !openQ4SaveGameSyncMarkersEnabled || windowName == NULL || windowName[0] == '\0' ) {
		return false;
	}
	// The window name as idWindow saves it: its length, then its characters.
	char pattern[ 4 + MAX_STRING_CHARS ];
	const int nameLength = idStr::Length( windowName );
	if ( nameLength >= MAX_STRING_CHARS ) {
		return false;
	}
	const int lengthWord = LittleLong( nameLength );
	memcpy( pattern, &lengthWord, 4 );
	memcpy( pattern + 4, windowName, nameLength );
	const int patternLength = 4 + nameLength;
	const int syncWord = LittleLong( OPENQ4_SAVEGAME_SYNC_MAGIC );
	const char *sync = reinterpret_cast<const char *>( &syncWord );

	const int start = file->Tell();
	const int end = Min( file->Length(), start + SCAN_MAX_BYTES );
	const int overlap = patternLength - 1;
	char *chunk = new char[ SCAN_CHUNK_BYTES ];
	bool found = false;
	for ( int offset = start; offset < end && !found; offset += SCAN_CHUNK_BYTES - overlap ) {
		const int chunkBytes = Min( SCAN_CHUNK_BYTES, end - offset );
		if ( file->Read( chunk, chunkBytes ) != chunkBytes ) {
			break;
		}
		bool bounded = false;
		for ( int i = 0; i < chunkBytes; i++ ) {
			if ( i + 4 <= chunkBytes && memcmp( chunk + i, sync, 4 ) == 0 ) {
				bounded = true;
				break;
			}
			if ( i + patternLength <= chunkBytes && memcmp( chunk + i, pattern, patternLength ) == 0 ) {
				found = true;
				break;
			}
		}
		if ( bounded || chunkBytes < SCAN_CHUNK_BYTES ) {
			break;
		}
		file->Seek( offset + SCAN_CHUNK_BYTES - overlap, FS_SEEK_SET );
	}
	delete[] chunk;
	file->Seek( start, FS_SEEK_SET );
	return found;
}

/*
================
idRestoreGame::ReadUserInterfaceRevision

A GUI record written for an earlier revision of its file (SaveGameReleases.h)
follows that revision's structure, so it is read into a copy built from that
revision. Its state carries over to the current GUI, whose windows start from
their definition.
================
*/
void idRestoreGame::ReadUserInterfaceRevision( idUserInterface *ui, const openQ4SaveGameGuiRevision_t &revision ) {
	idUserInterface *saved = uiManager->FindGui( revision.savedDefinition, true, true, true );
	if ( saved == NULL ) {
		Error( "idRestoreGame::ReadUserInterface: '%s' needs its earlier definition '%s'", revision.name, revision.savedDefinition );
	}
	if ( saved->ReadFromSaveGame( file ) == false ) {
		uiManager->DeAlloc( saved );
		Error( "idSaveGame::ReadUserInterface: ui failed to read properly\n" );
	}
	const idDict &state = saved->State();
	for ( int i = 0; i < state.GetNumKeyVals(); i++ ) {
		const idKeyValue *kv = state.GetKeyVal( i );
		ui->SetStateString( kv->GetKey(), kv->GetValue() );
	}
	uiManager->DeAlloc( saved );
	common->DPrintf( "Restored '%s' from a save written for its earlier definition\n", revision.name );
}

// RAVEN BEGIN
// abahr
/*
================
idRestoreGame::ReadExtrapolate
================
*/
void idRestoreGame::ReadExtrapolate( idExtrapolate<int>& extrap ) {
	int		extrapType;
	float	startTime;
	float	duration;
	int		startValue;
	int		baseSpeed;
	int		speed;

	ReadInt( extrapType );
	ReadFloat( startTime );
	ReadFloat( duration );

	ReadInt( startValue );
	ReadInt( baseSpeed );
	ReadInt( speed );

	extrap.Init( startTime, duration, startValue, baseSpeed, speed, (extrapolation_t)extrapType );
}

/*
================
idRestoreGame::ReadExtrapolate
================
*/
void idRestoreGame::ReadExtrapolate( idExtrapolate<float>& extrap ) {
	int		extrapType;
	float	startTime;
	float	duration;
	float	startValue;
	float	baseSpeed;
	float	speed;

	ReadInt( extrapType );
	ReadFloat( startTime );
	ReadFloat( duration );

	ReadFloat( startValue );
	ReadFloat( baseSpeed );
	ReadFloat( speed );

	extrap.Init( startTime, duration, startValue, baseSpeed, speed, (extrapolation_t)extrapType );
}

/*
================
idRestoreGame::ReadExtrapolate
================
*/
void idRestoreGame::ReadExtrapolate( idExtrapolate<idVec3>& extrap ) {
	int		extrapType;
	float	startTime;
	float	duration;
	idVec3	startValue;
	idVec3	baseSpeed;
	idVec3	speed;

	ReadInt( extrapType );
	ReadFloat( startTime );
	ReadFloat( duration );

	ReadVec3( startValue );
	ReadVec3( baseSpeed );
	ReadVec3( speed );

	extrap.Init( startTime, duration, startValue, baseSpeed, speed, (extrapolation_t)extrapType );
}

/*
================
idRestoreGame::ReadInterpolate
================
*/
void idRestoreGame::ReadInterpolate( idInterpolateAccelDecelLinear<int>& lerp ) {
	float	startTime;
	float	duration;
	float	accelTime;
	float	decelTime;
	
	int		startValue;
	int		endValue;
	
	ReadFloat( startTime );
	ReadFloat( duration );
	ReadFloat( accelTime );
	ReadFloat( decelTime );

	ReadInt( startValue );
	ReadInt( endValue );

	lerp.Init( startTime, accelTime, decelTime, duration, startValue, endValue );
}

/*
================
idRestoreGame::ReadInterpolate
================
*/
void idRestoreGame::ReadInterpolate( idInterpolateAccelDecelLinear<float>& lerp ) {
	float	startTime;
	float	duration;
	float	accelTime;
	float	decelTime;
	
	float	startValue;
	float	endValue;
	
	ReadFloat( startTime );
	ReadFloat( duration );
	ReadFloat( accelTime );
	ReadFloat( decelTime );

	ReadFloat( startValue );
	ReadFloat( endValue );

	lerp.Init( startTime, accelTime, decelTime, duration, startValue, endValue );
}

/*
================
idRestoreGame::ReadInterpolate
================
*/
void idRestoreGame::ReadInterpolate( idInterpolateAccelDecelLinear<idVec3>& lerp ) {
	float	startTime;
	float	duration;
	float	accelTime;
	float	decelTime;
	
	idVec3	startValue;
	idVec3	endValue;
	
	ReadFloat( startTime );
	ReadFloat( duration );
	ReadFloat( accelTime );
	ReadFloat( decelTime );

	ReadVec3( startValue );
	ReadVec3( endValue );

	lerp.Init( startTime, accelTime, decelTime, duration, startValue, endValue );
}

/*
================
idRestoreGame::ReadInterpolate
================
*/
void idRestoreGame::ReadInterpolate( idInterpolate<int>& lerp ) {
	float	startTime;
	float	duration;

	int		startValue;
	int		endValue;

	ReadFloat( startTime );
	ReadFloat( duration );

	ReadInt( startValue );
	ReadInt( endValue );

	lerp.Init( startTime, duration, startValue, endValue );
}

/*
================
idRestoreGame::ReadInterpolate
================
*/
void idRestoreGame::ReadInterpolate( idInterpolate<float>& lerp ) {
	float	startTime;
	float	duration;

	float	startValue;
	float	endValue;

	ReadFloat( startTime );
	ReadFloat( duration );

	ReadFloat( startValue );
	ReadFloat( endValue );

	lerp.Init( startTime, duration, startValue, endValue );
}

/*
================
idRestoreGame::ReadInterpolate
================
*/
void idRestoreGame::ReadInterpolate( idInterpolate<idVec3>& lerp ) {
	float	startTime;
	float	duration;

	idVec3	startValue;
	idVec3	endValue;

	ReadFloat( startTime );
	ReadFloat( duration );

	ReadVec3( startValue );
	ReadVec3( endValue );

	lerp.Init( startTime, duration, startValue, endValue );
}
/*
================
idRestoreGame::ReadRenderEffect
================
*/
void idRestoreGame::ReadRenderEffect( renderEffect_t &renderEffect ) {
	idStr	name;

	ReadSyncId( "ReadRenderEffect" );

	renderEffect.declEffect = NULL;

	ReadFloat( renderEffect.startTime );
	ReadInt( renderEffect.suppressSurfaceInViewID );
	ReadInt( renderEffect.allowSurfaceInViewID );
	ReadInt( renderEffect.groupID );

	ReadVec3( renderEffect.origin );
	ReadMat3( renderEffect.axis );

	ReadVec3( renderEffect.gravity );
	ReadVec3( renderEffect.endOrigin );

	ReadFloat( renderEffect.attenuation );
	ReadBool( renderEffect.hasEndOrigin );
	ReadBool( renderEffect.loop );
	ReadBool( renderEffect.ambient );
	ReadBool( renderEffect.inConnectedArea );
	ReadInt( renderEffect.weaponDepthHackInViewID );
	ReadFloat( renderEffect.modelDepthHack );

	ReadInt( renderEffect.referenceSoundHandle );

	for( int ix = 0; ix < MAX_ENTITY_SHADER_PARMS; ++ix ) {
		ReadFloat( renderEffect.shaderParms[ ix ] );
	}

	ReadString( name );
	if( name.Length() ) {
		renderEffect.declEffect = declManager->FindType( DECL_EFFECT, name );
	}
}

/*
================
idRestoreGame::ReadFrustum
================
*/
void idRestoreGame::ReadFrustum( idFrustum& frustum ) {
	idVec3 origin;
	idMat3 axis;
	float dNear = 0.0f, dFar = 0.0f, dLeft = 0.0f, dUp = 0.0f;
// RAVEN BEGIN
	ReadSyncId( "ReadFrustum" );
// RAVEN END
	ReadVec3( origin );
	frustum.SetOrigin( origin );

	ReadMat3( axis );
	frustum.SetAxis( axis );

	ReadFloat( dNear );
	ReadFloat( dFar );
	ReadFloat( dLeft );
	ReadFloat( dUp );
	frustum.SetSize( dNear, dFar, dLeft, dUp ); 
}

/*
================
idRestoreGame::ReadRenderEntity
================
*/
// RAVEN BEGIN
void idRestoreGame::ReadRenderEntity( renderEntity_t &renderEntity, const idDict *args ) {
// RAVEN END
	int i;

	ReadSyncId( "ReadRenderEntity" );

	ReadModel( renderEntity.hModel );

	ReadInt( renderEntity.entityNum );
	ReadInt( renderEntity.bodyId );

	ReadBounds( renderEntity.bounds );
	if ( !SaveGame_IsValidRenderBounds( renderEntity.bounds ) ) {
		Error( "idRestoreGame::ReadRenderEntity: invalid render bounds" );
	}

	assert( renderEntity.bounds[0][0] <= renderEntity.bounds[1][0] ); 
	assert( renderEntity.bounds[0][1] <= renderEntity.bounds[1][1] );
	assert( renderEntity.bounds[0][2] <= renderEntity.bounds[1][2] );

	assert( renderEntity.bounds[1][0] - renderEntity.bounds[0][0] < MAX_BOUND_SIZE );
	assert( renderEntity.bounds[1][1] - renderEntity.bounds[0][1] < MAX_BOUND_SIZE );
	assert( renderEntity.bounds[1][2] - renderEntity.bounds[0][2] < MAX_BOUND_SIZE );

	// callback is set by class's Restore function
	renderEntity.callback = NULL;
	renderEntity.callbackData = NULL;

	ReadInt( renderEntity.suppressSurfaceInViewID );
	ReadInt( renderEntity.suppressShadowInViewID );
	ReadInt( renderEntity.suppressShadowInLightID );
	ReadInt( renderEntity.allowSurfaceInViewID );

	ReadInt( renderEntity.suppressSurfaceMask );

	ReadVec3( renderEntity.origin );
	ReadMat3( renderEntity.axis );

	ReadMaterial( renderEntity.customShader );
	ReadMaterial( renderEntity.referenceShader );
	ReadMaterial( renderEntity.overlayShader );
	ReadSkin( renderEntity.customSkin );

	ReadInt( renderEntity.referenceSoundHandle );

	for( i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ ) {
		ReadFloat( renderEntity.shaderParms[ i ] );
	}

	for( i = 0; i < MAX_RENDERENTITY_GUI; i++ ) {
// RAVEN BEGIN
		ReadUserInterface( renderEntity.gui[ i ], args );
// RAVEN END
	}

	// idEntity will restore "cameraTarget", which will be used in idEntity::Present to restore the remoteRenderView
	renderEntity.remoteRenderView = NULL;

	renderEntity.numJoints = 0;
	renderEntity.joints = NULL;

	ReadFloat( renderEntity.modelDepthHack );

	ReadBool( renderEntity.noSelfShadow );
	ReadBool( renderEntity.noShadow );
	ReadBool( renderEntity.noDynamicInteractions );
	ReadBool( renderEntity.forceUpdate );

	ReadInt( renderEntity.weaponDepthHackInViewID );
	ReadFloat( renderEntity.shadowLODDistance );
	ReadInt( renderEntity.suppressLOD );
}
// RAVEN END

/*
================
idRestoreGame::ReadRenderLight
================
*/
void idRestoreGame::ReadRenderLight( renderLight_t &renderLight ) {
	int i;

	ReadSyncId( "ReadRenderLight" );

	ReadMat3( renderLight.axis );
	ReadVec3( renderLight.origin );

	ReadInt( renderLight.suppressLightInViewID );
	ReadInt( renderLight.allowLightInViewID );
	ReadBool( renderLight.noShadows );
	ReadBool( renderLight.noSpecular );
	ReadBool( renderLight.noDynamicShadows );
	ReadBool( renderLight.pointLight );
	ReadBool( renderLight.parallel );
	ReadBool( renderLight.globalLight );

// RAVEN BEGIN
// dluetscher: added detail levels to render lights
	ReadFloat( renderLight.detailLevel );
// RAVEN END

	ReadVec3( renderLight.lightRadius );
	ReadVec3( renderLight.lightCenter );

	ReadVec3( renderLight.target );
	ReadVec3( renderLight.right );
	ReadVec3( renderLight.up );
	ReadVec3( renderLight.start );
	ReadVec3( renderLight.end );

	// only idLight has a prelightModel and it's always based on the entityname, so we'll restore it there
	// ReadModel( renderLight.prelightModel );
	renderLight.prelightModel = NULL;

	ReadInt( renderLight.lightId );

	ReadMaterial( renderLight.shader );

	for( i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ ) {
		ReadFloat( renderLight.shaderParms[ i ] );
	}

// RAVEN BEGIN
	ReadInt( renderLight.referenceSoundHandle );
// RAVEN END
}

/*
================
idRestoreGame::ReadRefSound
================
*/
void idRestoreGame::ReadRefSound( refSound_t &refSound ) {
// RAVEN BEGIN
	ReadSyncId( "ReadRefSound" );
// RAVEN END

	ReadInt( refSound.referenceSoundHandle );
	ReadVec3( refSound.origin );
// RAVEN BEGIN
	ReadVec3( refSound.velocity );
// RAVEN END
	ReadInt( refSound.listenerId );
	ReadSoundShader( refSound.shader );
	ReadFloat( refSound.diversity );
	ReadBool( refSound.waitfortrigger );

	ReadFloat( refSound.parms.minDistance );
	ReadFloat( refSound.parms.maxDistance );
	ReadFloat( refSound.parms.volume );
	ReadFloat( refSound.parms.shakes );
	ReadInt( refSound.parms.soundShaderFlags );
	ReadInt( refSound.parms.soundClass );
}

/*
================
idRestoreGame::ReadRenderView
================
*/
void idRestoreGame::ReadRenderView( renderView_t &view ) {
	int i;

// RAVEN BEGIN
	ReadSyncId( "ReadRenderView" );
// RAVEN END

	ReadInt( view.viewID );
	ReadInt( view.x );
	ReadInt( view.y );
	ReadInt( view.width );
	ReadInt( view.height );

	ReadFloat( view.fov_x );
	ReadFloat( view.fov_y );
	ReadVec3( view.vieworg );
	ReadMat3( view.viewaxis );

	ReadBool( view.cramZNear );

	ReadInt( view.time );

	for( i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ ) {
		ReadFloat( view.shaderParms[ i ] );
	}
}

/*
=================
idRestoreGame::ReadUsercmd
=================
*/
void idRestoreGame::ReadUsercmd( usercmd_t &usercmd ) {
// RAVEN BEGIN
	ReadSyncId( "ReadUsercmd" );
// RAVEN END
	ReadInt( usercmd.gameFrame );
	ReadInt( usercmd.gameTime );
	ReadInt( usercmd.duplicateCount );
// RAVEN BEGIN
// ddynerman: larger button bitfield
	ReadShort( usercmd.buttons );
// RAVEN END
	ReadSignedChar( usercmd.forwardmove );
	ReadSignedChar( usercmd.rightmove );
	ReadSignedChar( usercmd.upmove );
	ReadShort( usercmd.angles[0] );
	ReadShort( usercmd.angles[1] );
	ReadShort( usercmd.angles[2] );
	ReadShort( usercmd.mx );
	ReadShort( usercmd.my );
	ReadSignedChar( usercmd.impulse );
	ReadByte( usercmd.flags );
	ReadInt( usercmd.sequence );
}

/*
===================
idRestoreGame::ReadContactInfo
===================
*/
void idRestoreGame::ReadContactInfo( contactInfo_t &contactInfo ) {
	ReadInt( (int &)contactInfo.type );
	ReadVec3( contactInfo.point );
	ReadVec3( contactInfo.normal );
	ReadFloat( contactInfo.dist );
	ReadInt( contactInfo.contents );
	ReadMaterial( contactInfo.material );
	ReadInt( contactInfo.modelFeature );
	ReadInt( contactInfo.trmFeature );
	ReadInt( contactInfo.entityNum );
	ReadInt( contactInfo.id );
	ReadMaterialType( contactInfo.materialType );
}

/*
===================
idRestoreGame::ReadTrace
===================
*/
void idRestoreGame::ReadTrace( trace_t &trace ) {
// RAVEN BEGIN
	ReadSyncId( "ReadTrace" );
// RAVEN END
	ReadFloat( trace.fraction );
	ReadVec3( trace.endpos );
	ReadMat3( trace.endAxis );
	ReadContactInfo( trace.c );
}

/*
=====================
idRestoreGame::ReadClipModel
=====================
*/
void idRestoreGame::ReadClipModel( idClipModel *&clipModel ) {
	bool restoreClipModel;

	ReadBool( restoreClipModel );
	if ( restoreClipModel ) {
		clipModel = new idClipModel();
		clipModel->Restore( this );
	} else {
		clipModel = NULL;
	}
}

/*
=====================
idRestoreGame::ReadSoundCommands
=====================
*/
void idRestoreGame::ReadSoundCommands( void ) {
	soundSystem->StopAllSounds( SOUNDWORLD_GAME );
	soundSystem->ReadFromSaveGame( SOUNDWORLD_GAME, file );
}

/*
=====================
idRestoreGame::ReadBuildNumber
=====================
*/
void idRestoreGame::ReadBuildNumber( void ) {
	int marker;
	ReadInt( marker );

	openQ4SaveGameLayoutBuild = 0;
	openQ4SaveGameCompatibilityVersion = 0;
	openQ4SaveGameCompatibilitySourceFileCount = 0;
	openQ4SaveGameNextSyncId = 0;
	openQ4SaveGameHasCompatibilityStamp = false;
	openQ4SaveGameCompatible = false;
	openQ4SaveGameSyncMarkersEnabled = false;
	openQ4SaveGameCompatibilityStamp.Clear();
	openQ4SaveGameCompatibilityError.Clear();

	if ( marker != OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC ) {
		buildNumber = marker;
		openQ4SaveGameLayoutBuild = buildNumber;
		if ( buildNumber == BUILD_NUMBER &&
			 idStr::Icmp( OpenQ4SaveGameWireABI(), "windows-msvcabi-x64-le-raw1" ) == 0 ) {
			openQ4SaveGameCompatible = true;
		} else {
			openQ4SaveGameCompatibilityError = va(
				"legacy save payload build/ABI %d/%s is not supported by current build/ABI %d/%s",
				buildNumber,
				OpenQ4SaveGameWireABI(),
				BUILD_NUMBER,
				OpenQ4SaveGameWireABI() );
		}
		return;
	}

	openQ4SaveGameHasCompatibilityStamp = true;
	ReadInt( openQ4SaveGameCompatibilityVersion );
	ReadInt( buildNumber );
	ReadString( openQ4SaveGameCompatibilityStamp );
	ReadInt( openQ4SaveGameCompatibilitySourceFileCount );
	// A release that stamped build 1 is read as its release's build.
	openQ4SaveGameLayoutBuild = OpenQ4_SaveGameLayoutBuild( openQ4SaveGameCompatibilityVersion, buildNumber,
		openQ4SaveGameCompatibilityStamp.c_str(), openQ4SaveGameCompatibilitySourceFileCount );

	if ( openQ4SaveGameCompatibilityVersion != OPENQ4_SAVEGAME_COMPATIBILITY_VERSION &&
		 openQ4SaveGameCompatibilityVersion != OPENQ4_SAVEGAME_PREVIOUS_COMPATIBILITY_VERSION &&
		 openQ4SaveGameCompatibilityVersion != OPENQ4_SAVEGAME_V2_VERSION ) {
		openQ4SaveGameCompatibilityError = va(
			"payload format version %d is not supported (supported %d to %d)",
			openQ4SaveGameCompatibilityVersion,
			OPENQ4_SAVEGAME_V2_VERSION,
			OPENQ4_SAVEGAME_COMPATIBILITY_VERSION );
		return;
	}

	{
		// Version 2 predates the wire-ABI stamp; it is the layout v2 shipped.
		idStr savedWireABI = OPENQ4_SAVEGAME_V2_WIRE_ABI;
		if ( openQ4SaveGameCompatibilityVersion >= OPENQ4_SAVEGAME_TYPED_COMPATIBILITY_VERSION ) {
			ReadString( savedWireABI );
		}
		if ( savedWireABI.Icmp( OpenQ4SaveGameWireABI() ) != 0 ) {
			openQ4SaveGameCompatibilityError = va(
				"wire ABI %s does not match current ABI %s",
				savedWireABI.c_str(), OpenQ4SaveGameWireABI() );
			return;
		}
		if ( openQ4SaveGameCompatibilitySourceFileCount < 0 || openQ4SaveGameCompatibilityStamp.IsEmpty() ) {
			openQ4SaveGameCompatibilityError = "invalid payload source metadata";
			return;
		}
		if ( buildNumber != BUILD_NUMBER ||
			 openQ4SaveGameCompatibilityStamp.Icmp( OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH ) != 0 ||
			 openQ4SaveGameCompatibilitySourceFileCount != OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT ) {
			common->DPrintf( "Schema-compatible save source differs: saved %d/%s (%d files), current %d/%s (%d files)\n",
				buildNumber, openQ4SaveGameCompatibilityStamp.c_str(), openQ4SaveGameCompatibilitySourceFileCount,
				BUILD_NUMBER, OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH, OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT );
		}
	}

	openQ4SaveGameCompatible = true;
	openQ4SaveGameSyncMarkersEnabled = true;
	openQ4SaveGameNextSyncId = 0;
}

/*
=====================
idRestoreGame::ReadSaveGameFooter
=====================
*/
void idRestoreGame::ReadSaveGameFooter( void ) {
	if ( !openQ4SaveGameSyncMarkersEnabled ) {
		return;
	}

	const int footerOffset = file->Tell();
	int marker;
	int footerVersion;
	int savedFooterOffset;
	int savedObjectCount;
	int savedSyncCount;

	ReadInt( marker );
	ReadInt( footerVersion );
	ReadInt( savedFooterOffset );
	ReadInt( savedObjectCount );
	ReadInt( savedSyncCount );

	if ( marker != OPENQ4_SAVEGAME_FOOTER_MAGIC ) {
		Error( "idRestoreGame::ReadSaveGameFooter: marker mismatch at offset %d (got 0x%08x, expected 0x%08x)",
			footerOffset, marker, OPENQ4_SAVEGAME_FOOTER_MAGIC );
	}
	if ( footerVersion != OPENQ4_SAVEGAME_FOOTER_VERSION ) {
		Error( "idRestoreGame::ReadSaveGameFooter: footer version %d does not match current version %d",
			footerVersion, OPENQ4_SAVEGAME_FOOTER_VERSION );
	}
	if ( savedFooterOffset != footerOffset ) {
		Error( "idRestoreGame::ReadSaveGameFooter: saved footer offset %d does not match actual offset %d",
			savedFooterOffset, footerOffset );
	}
	if ( savedObjectCount != objects.Num() - 1 ) {
		Error( "idRestoreGame::ReadSaveGameFooter: saved object count %d does not match restored object count %d",
			savedObjectCount, objects.Num() - 1 );
	}
	if ( savedSyncCount != openQ4SaveGameNextSyncId ) {
		Error( "idRestoreGame::ReadSaveGameFooter: saved sync marker count %d does not match restored count %d",
			savedSyncCount, openQ4SaveGameNextSyncId );
	}

	if ( openQ4SaveGameCompatibilityVersion >= OPENQ4_SAVEGAME_TYPED_COMPATIBILITY_VERSION ) {
		const int integrityOffset = file->Tell();
		int integrityMarker;
		int integrityVersion;
		int protectedLength;
		int checksumBits;
		ReadInt( integrityMarker );
		ReadInt( integrityVersion );
		ReadInt( protectedLength );
		ReadInt( checksumBits );
		if ( integrityMarker != OPENQ4_SAVEGAME_INTEGRITY_MAGIC ) {
			Error( "idRestoreGame::ReadSaveGameFooter: invalid integrity marker 0x%08x", integrityMarker );
		}
		if ( integrityVersion != OPENQ4_SAVEGAME_INTEGRITY_VERSION ) {
			Error( "idRestoreGame::ReadSaveGameFooter: integrity version %d does not match current version %d",
				integrityVersion, OPENQ4_SAVEGAME_INTEGRITY_VERSION );
		}
		if ( protectedLength != integrityOffset ) {
			Error( "idRestoreGame::ReadSaveGameFooter: protected length %d does not match integrity offset %d",
				protectedLength, integrityOffset );
		}
		(void)checksumBits; // The engine preflight verifies the checksum before map teardown.
	}

	const int fileLength = file->Length();
	const int endOffset = file->Tell();
	if ( fileLength > 0 && endOffset >= 0 && endOffset != fileLength ) {
		Error( "idRestoreGame::ReadSaveGameFooter: %d unexpected trailing bytes after savegame payload",
			fileLength - endOffset );
	}
}

/*
=====================
idRestoreGame::GetBuildNumber
=====================
*/
int idRestoreGame::GetBuildNumber( void ) {
	return buildNumber;
}

/*
=====================
idRestoreGame::RemainingBytes
=====================
*/
int idRestoreGame::RemainingBytes( void ) const {
	const int length = file->Length();
	const int offset = file->Tell();
	if ( length <= 0 || offset < 0 || offset > length ) {
		return -1;
	}
	return length - offset;
}

/*
=====================
idRestoreGame::GetOpenQ4SaveGameCompatibilityVersion
=====================
*/
int idRestoreGame::GetOpenQ4SaveGameCompatibilityVersion( void ) const {
	return openQ4SaveGameCompatibilityVersion;
}

/*
=====================
idRestoreGame::HasOpenQ4SaveGameCompatibilityStamp
=====================
*/
bool idRestoreGame::HasOpenQ4SaveGameCompatibilityStamp( void ) const {
	return openQ4SaveGameHasCompatibilityStamp;
}

/*
=====================
idRestoreGame::IsOpenQ4SaveGameCompatible
=====================
*/
bool idRestoreGame::IsOpenQ4SaveGameCompatible( void ) const {
	return openQ4SaveGameCompatible;
}

/*
=====================
idRestoreGame::GetOpenQ4SaveGameCompatibilityError
=====================
*/
const char *idRestoreGame::GetOpenQ4SaveGameCompatibilityError( void ) const {
	return openQ4SaveGameCompatibilityError.c_str();
}

/*
=====================
idRestoreGame::GetOpenQ4SaveGameCompatibilityStamp
=====================
*/
const char *idRestoreGame::GetOpenQ4SaveGameCompatibilityStamp( void ) const {
	return openQ4SaveGameCompatibilityStamp.c_str();
}

/*
=====================
idRestoreGame::HasOpenQ4PlayerSwimSpeedSaveField
=====================
*/
bool idRestoreGame::HasOpenQ4PlayerSwimSpeedSaveField( void ) const {
	return HasOpenQ4PlayerLiquidSaveFieldForBuild( OPENQ4_SAVEGAME_BUILD_WITH_PLAYER_SWIM_SPEED );
}

/*
=====================
idRestoreGame::HasOpenQ4PlayerLiquidSoundSaveField
=====================
*/
bool idRestoreGame::HasOpenQ4PlayerLiquidSoundSaveField( void ) const {
	return HasOpenQ4PlayerLiquidSaveFieldForBuild( OPENQ4_SAVEGAME_BUILD_WITH_PLAYER_LIQUID_SOUND );
}

/*
=====================
idRestoreGame::HasOpenQ4InterpolatorCacheSaveLayout

Whether raw records that contain an idExtrapolate carry its cache flag. An
unstamped payload is only ever read by the build that wrote it.
=====================
*/
bool idRestoreGame::HasOpenQ4InterpolatorCacheSaveLayout( void ) const {
	return OpenQ4_SaveGameHasInterpolatorCacheFlag( openQ4SaveGameLayoutBuild );
}

/*
=====================
idRestoreGame::HasOpenQ4PlayerLiquidSaveFieldForBuild
=====================
*/
bool idRestoreGame::HasOpenQ4PlayerLiquidSaveFieldForBuild( int firstBuildWithField ) const {
	if ( !openQ4SaveGameHasCompatibilityStamp ||
		 openQ4SaveGameCompatibilityVersion < OPENQ4_SAVEGAME_TYPED_COMPATIBILITY_VERSION ) {
		return false;
	}

	return openQ4SaveGameLayoutBuild >= firstBuildWithField;
}



void Cmd_CheckSave_f( const idCmdArgs &args )
{
	idPlayer	*lp = gameLocal.GetLocalPlayer();
	if ( lp == NULL ) {
		common->Printf( "checkSave: no local player to test\n" );
		return;
	}

	idScopedSaveMemoryFile memoryFile;
	idFile		*mp = memoryFile.GetFile();
	if ( mp == NULL ) {
		common->Printf( "checkSave: failed to allocate memory file\n" );
		return;
	}

	{
		idSaveGame	sg( mp );
		sg.CallSave_r( lp->GetType(), lp );
		sg.Close();
	}

	mp->Rewind();
	idPlayer		test;
	idRestoreGame	rg( mp );

	rg.CallRestore_r( test.GetType(), &test );

	common->Printf( "checkSave: save/restore round trip completed\n" );
}

