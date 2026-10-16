
#ifndef __SAVEGAME_H__
#define __SAVEGAME_H__

/*

Save game related helper classes.

*/

const int INITIAL_RELEASE_BUILD_NUMBER = 1262;
const int OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'S' << 24 );
const int OPENQ4_SAVEGAME_COMPATIBILITY_VERSION = 4;
const int OPENQ4_SAVEGAME_PREVIOUS_COMPATIBILITY_VERSION = 3;
// Packed flags, typed fields and integrity trailers were introduced in v3.
const int OPENQ4_SAVEGAME_TYPED_COMPATIBILITY_VERSION = 3;
const int OPENQ4_SAVEGAME_SYNC_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'Y' << 24 );
const int OPENQ4_SAVEGAME_FOOTER_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'F' << 24 );
const int OPENQ4_SAVEGAME_FOOTER_VERSION = 1;
const int OPENQ4_SAVEGAME_INTEGRITY_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'I' << 24 );
const int OPENQ4_SAVEGAME_INTEGRITY_VERSION = 1;

const char *OpenQ4SaveGameWireABI( void );

class idSaveGame {
public:
	friend	void Cmd_CheckSave_f( const idCmdArgs &args );

							idSaveGame( idFile *savefile );
							~idSaveGame();

	void					Close( void );

	void					AddObject( const idClass *obj );
	void					WriteObjectList( void );

	void					Write( const void *buffer, int len );
	void					WriteInt( const int value );
	void					WriteJoint( const jointHandle_t value );
	void					WriteShort( const short value );
	void					WriteByte( const byte value );
	void					WriteSignedChar( const signed char value );
	void					WriteFloat( const float value );
	void					WriteBool( const bool value );
	void					WriteString( const char *string );
	void					WriteVec2( const idVec2 &vec );
	void					WriteVec3( const idVec3 &vec );
	void					WriteVec4( const idVec4 &vec );
	void					WriteVec5( const idVec5 &vec );
	void					WriteVec6( const idVec6 &vec );
	void					WriteWinding( const idWinding &winding );
	void					WriteBounds( const idBounds &bounds );
	void					WriteMat3( const idMat3 &mat );
	void					WriteAngles( const idAngles &angles );
	void					WriteObject( const idClass *obj );
	void					WriteStaticObject( const idClass &obj );
	void					WriteDict( const idDict *dict );
	void					WriteMaterial( const idMaterial *material );
	void					WriteSkin( const idDeclSkin *skin );
// RAVEN BEGIN
// jscott: not using
//	void					WriteParticle( const idDeclParticle *particle );
//	void					WriteFX( const idDeclFX *fx );
// RAVEN END
	void					WriteSoundShader( const idSoundShader *shader );
	void					WriteModelDef( const class idDeclModelDef *modelDef );
	void					WriteModel( const idRenderModel *model );
// RAVEN BEGIN
// bdube: material type
	void					WriteMaterialType ( const rvDeclMatType* matType );
	void					WriteTable ( const idDeclTable* table );
// abahr
	void					WriteExtrapolate( const idExtrapolate<int>& extrap );
	void					WriteExtrapolate( const idExtrapolate<float>& extrap );
	void					WriteExtrapolate( const idExtrapolate<idVec3>& extrap );
	void					WriteInterpolate( const idInterpolateAccelDecelLinear<int>& lerp );
	void					WriteInterpolate( const idInterpolateAccelDecelLinear<float>& lerp );
	void					WriteInterpolate( const idInterpolateAccelDecelLinear<idVec3>& lerp );
	void					WriteInterpolate( const idInterpolate<int>& lerp ); 
	void					WriteInterpolate( const idInterpolate<float>& lerp ); 
	void					WriteInterpolate( const idInterpolate<idVec3>& lerp ); 
	void					WriteRenderEffect( const renderEffect_t &renderEffect );
	void					WriteFrustum( const idFrustum& frustum );
	void					WriteSyncId( void );
// RAVEN END		
	void					WriteUserInterface( const idUserInterface *ui, bool unique );
	void					WriteRenderEntity( const renderEntity_t &renderEntity );
	void					WriteRenderLight( const renderLight_t &renderLight );
	void					WriteRefSound( const refSound_t &refSound );
	void					WriteRenderView( const renderView_t &view );
	void					WriteUsercmd( const usercmd_t &usercmd );
	void					WriteContactInfo( const contactInfo_t &contactInfo );
	void					WriteTrace( const trace_t &trace );
	void					WriteClipModel( const class idClipModel *clipModel );
	void					WriteSoundCommands( void );

	void					WriteBuildNumber( const int value );

protected:
	idFile *				file;

	idList<const idClass *>	objects;
	idHashIndex				objectHash;
	int						openQ4SaveGameNextSyncId;
	bool					openQ4SaveGameSyncMarkersEnabled;

	void					WriteChecked( int bytesWritten, int expected, const char *detail, int offset );
	void					CallSave_r( const idTypeInfo *cls, const idClass *obj );
	void					WriteSaveGameFooter( int numObjects );
};

class idRestoreGame {
public:
	friend	void Cmd_CheckSave_f( const idCmdArgs &args );

							idRestoreGame( idFile *savefile );
							~idRestoreGame();

	void					CreateObjects( void );
	void					RestoreObjects( void );
	void					DeleteObjects( void );

	void					Error( const char *fmt, ... );

	void					Read( void *buffer, int len );
	void					ReadInt( int &value );
	void					ReadJoint( jointHandle_t &value );
	void					ReadShort( short &value );
	void					ReadByte( byte &value );
	void					ReadSignedChar( signed char &value );
	void					ReadFloat( float &value );
	void					ReadBool( bool &value );
	void					ReadString( idStr &string );
	void					ReadVec2( idVec2 &vec );
	void					ReadVec3( idVec3 &vec );
	void					ReadVec4( idVec4 &vec );
	void					ReadVec5( idVec5 &vec );
	void					ReadVec6( idVec6 &vec );
	void					ReadWinding( idWinding &winding );
	void					ReadBounds( idBounds &bounds );
	void					ReadMat3( idMat3 &mat );
	void					ReadAngles( idAngles &angles );
	void					ReadObject( idClass *&obj );
	void					ReadObject( idClass *&obj, const idTypeInfo &expectedType, const char *detail );
	template< class T >
	void					ReadObject( T *&obj ) {
		obj = NULL;
		idClass *restoredObject = NULL;
		const idTypeInfo &expectedType = T::GetClassType();
		ReadObject( restoredObject, expectedType, expectedType.classname );
		obj = static_cast<T *>( restoredObject );
	}
	void					ReadStaticObject( idClass &obj );
	void					ReadDict( idDict *dict );
	void					ReadMaterial( const idMaterial *&material );
	void					ReadSkin( const idDeclSkin *&skin );
// RAVEN BEGIN
// bdube: not using
//	void					ReadParticle( const idDeclParticle *&particle );
//	void					ReadFX( const idDeclFX *&fx );
// RAVEN END
	void					ReadSoundShader( const idSoundShader *&shader );
	void					ReadModelDef( const idDeclModelDef *&modelDef );
	void					ReadModel( idRenderModel *&model );
// RAVEN BEGIN
	void					ReadUserInterface( idUserInterface *&ui, const idDict *args );
	void					ReadUserInterfaceWithLayout( idUserInterface *ui, const char *name, const char *layout );
// bdube: material type
	void					ReadMaterialType ( const rvDeclMatType* &matType );
	void					ReadTable ( const idDeclTable* &table );
// abahr
	void					ReadExtrapolate( idExtrapolate<int>& extrap );
	void					ReadExtrapolate( idExtrapolate<float>& extrap );
	void					ReadExtrapolate( idExtrapolate<idVec3>& extrap );
	void					ReadInterpolate( idInterpolateAccelDecelLinear<int>& lerp );
	void					ReadInterpolate( idInterpolateAccelDecelLinear<float>& lerp );
	void					ReadInterpolate( idInterpolateAccelDecelLinear<idVec3>& lerp );
	void					ReadInterpolate( idInterpolate<int>& lerp );
	void					ReadInterpolate( idInterpolate<float>& lerp );
	void					ReadInterpolate( idInterpolate<idVec3>& lerp );
	void					ReadRenderEffect( renderEffect_t &renderEffect );
	void					ReadFrustum( idFrustum& frustum );
	void					ReadSyncId( const char *detail = "unspecified", const char *classname = NULL );
	void					ReadRenderEntity( renderEntity_t &renderEntity, const idDict *args );
// RAVEN END		
	void					ReadRenderLight( renderLight_t &renderLight );
	void					ReadRefSound( refSound_t &refSound );
	void					ReadRenderView( renderView_t &view );
	void					ReadUsercmd( usercmd_t &usercmd );
	void					ReadContactInfo( contactInfo_t &contactInfo );
	void					ReadTrace( trace_t &trace );
	void					ReadClipModel( idClipModel *&clipModel );
	void					ReadSoundCommands( void );

	void					ReadBuildNumber( void );
	void					ReadSaveGameFooter( void );

	//						Used to retrieve the saved game buildNumber from within class Restore methods
	int						GetBuildNumber( void );
	int						GetOpenQ4SaveGameCompatibilityVersion( void ) const;
	bool					HasOpenQ4SaveGameCompatibilityStamp( void ) const;
	bool					IsOpenQ4SaveGameCompatible( void ) const;
	const char *			GetOpenQ4SaveGameCompatibilityError( void ) const;
	const char *			GetOpenQ4SaveGameCompatibilityStamp( void ) const;
	bool					HasOpenQ4PlayerSwimSpeedSaveField( void ) const;
	bool					HasOpenQ4PlayerLiquidSoundSaveField( void ) const;

private:
	bool					HasOpenQ4PlayerLiquidSaveFieldForBuild( int firstBuildWithField ) const;
	int						buildNumber;
	int						openQ4SaveGameCompatibilityVersion;
	int						openQ4SaveGameCompatibilitySourceFileCount;
	int						openQ4SaveGameNextSyncId;
	bool					openQ4SaveGameHasCompatibilityStamp;
	bool					openQ4SaveGameCompatible;
	bool					openQ4SaveGameSyncMarkersEnabled;
	idStr					openQ4SaveGameCompatibilityStamp;
	idStr					openQ4SaveGameCompatibilityError;

	idFile *				file;

	idList<idClass *>		objects;

	void					ReadChecked( void *buffer, int len, const char *detail );
	void					CallRestore_r( const idTypeInfo *cls, idClass *obj );
	bool					HasNextSerializedEmptyClassFrame( void );
};

#endif /* !__SAVEGAME_H__*/
