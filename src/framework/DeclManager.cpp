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




// jmarshall - Raven Decl Support
#include "../renderer/tr_local.h"
#include "../bse/BSE_API.h"
// jmarshall end

/*

GUIs and script remain separately parsed

Following a parse, all referenced media (and other decls) will have been touched.

sinTable and cosTable are required for the rotate material keyword to function

A new FindType on a purged decl will cause it to be reloaded, but a stale pointer to a purged
decl will look like a defaulted decl.

Moving a decl from one file to another will not be handled correctly by a reload, the material
will be defaulted.

NULL or empty decl names will always return NULL
	Should probably make a default decl for this

Decls are initially created without a textSource
A parse without textSource set should always just call MakeDefault()
A parse that has an error should internally call MakeDefault()
A purge does nothing to a defaulted decl

Should we have a "purged" media state separate from the "defaulted" media state?

reloading over a decl name that was defaulted

reloading over a decl name that was valid

missing reload over a previously explicit definition

*/

#define USE_COMPRESSED_DECLS
//#define GET_HUFFMAN_FREQUENCIES

static idDecl *OpenQ4_AllocEffectDecl( void ) {
	if ( bseAllocDeclEffect == NULL ) {
		common->FatalError( "DECL_EFFECT allocator is not installed. AttachBSE must run before declManager->Init()." );
	}

	idDecl *decl = bseAllocDeclEffect();
	if ( decl == NULL ) {
		common->FatalError( "DECL_EFFECT allocator returned NULL." );
	}

	if ( !OpenQ4_IsIntegratedBSEDeclEffect( decl ) ) {
		delete decl;
		common->FatalError( "DECL_EFFECT allocator returned a non-BSE decl instance." );
	}

	return decl;
}

static void OpenQ4_VerifyEffectDeclAllocator( void ) {
	idDecl *decl = OpenQ4_AllocEffectDecl();
	delete decl;
}

enum declSingleFileWriteMode_t {
	DECL_SINGLEFILE_WRITE_OPENQ4 = 0,
	DECL_SINGLEFILE_WRITE_RETAIL
};

class idDeclType {
public:
	idStr						typeName;
	declType_t					type;
	idDecl *					(*allocator)( void );
};

class idDeclFolder {
public:
	idStr						folder;
	idStr						extension;
	declType_t					defaultType;
};

static const int DECL_GUIDE_FILE_LEXER_FLAGS =	LEXFL_NOSTRINGCONCAT |
												LEXFL_NOSTRINGESCAPECHARS |
												LEXFL_ALLOWPATHNAMES;
static const char *DECL_GUIDE_FOLDER = "guides";
static const char *DECL_GUIDE_EXTENSION = ".guide";
static const char *DECL_GUIDE_PATH_PREFIX = "guides/";
static const char *DECL_WRITE_PROGRAM_IMAGES_CVAR = "image_writeProgramImages";

class idDeclFile;

class idDeclLocal : public idDeclBase {
	friend class idDeclFile;
	friend class idDeclManagerLocal;

public:
								idDeclLocal();
	virtual 					~idDeclLocal() {};
	virtual const char *		GetName( void ) const;
	virtual declType_t			GetType( void ) const;
	virtual declState_t			GetState( void ) const;
	virtual bool				IsImplicit( void ) const;
	virtual bool				IsValid( void ) const;
	virtual void				Invalidate( void );
	virtual void				Reload( void );
	virtual void				EnsureNotPurged( void );
	virtual int					Index( void ) const;
	virtual int					GetLineNum( void ) const;
	virtual const char *		GetFileName( void ) const;
	virtual size_t				Size( void ) const;
	virtual void				GetText( char *text ) const;
	virtual int					GetTextLength( void ) const;
	virtual int					GetCompressedLength( void ) const;
	virtual void				SetText( const char *text );
	virtual bool				ReplaceSourceFileText( void );
	virtual bool				SourceFileChanged( void ) const;
	virtual void				MakeDefault( void );
	virtual bool				EverReferenced( void ) const;
	virtual void				SetReferencedThisLevel( void );
	virtual bool				Validate( const char *psText, int iLength, idStr &strReportTo ) const;

protected:
	virtual bool				SetDefaultText( void );
	virtual const char *		DefaultDefinition( void ) const;
	virtual bool				Parse( const char *text, const int textLength, bool cache );
	virtual void				FreeData( void );
	virtual void				List( void ) const;
	virtual void				Print( void ) const;

protected:
	void						AllocateSelf( void );

								// Parses the decl definition.
								// After calling parse, a decl will be guaranteed usable.
	void						ParseLocal( bool noCaching = false );

								// Does a MakeDefualt, but flags the decl so that it
								// will Parse() the next time the decl is found.
	void						Purge( void );

								// Set textSource possible with compression.
	void						SetTextLocal( const char *text, const int length );

private:
	idDecl *					self;
	bool						insideLevelLoad;
	idStr						name;					// name of the decl
	char *						textSource;				// decl text definition
	int							textLength;				// length of textSource
	int							compressedLength;		// compressed length
	idDeclFile *				sourceFile;				// source file in which the decl was defined
	int							sourceTextOffset;		// offset in source file to decl text
	int							sourceTextLength;		// length of decl text in source file
	int							sourceLine;				// this is where the actual declaration token starts
	int							checksum;				// checksum of the decl text
	declType_t					type;					// decl type
	declState_t					declState;				// decl state
	int							index;					// index in the per-type list

	bool						parsedOutsideLevelLoad;	// these decls will never be purged
	bool						everReferenced;			// set to true if the decl was ever used
	bool						referencedThisLevel;	// set to true when the decl is used for the current level
	bool						redefinedInReload;		// used during file reloading to make sure a decl that has
														// its source removed will be defaulted
	bool						needsPrecache;			// packed decl stubs expand source text lazily when first parsed
	idDeclLocal *				nextInFile;				// next decl in the decl file
};

class idDeclFile {
public:
								idDeclFile();
								idDeclFile( const char *fileName, declType_t defaultType );

	void						Reload( bool force );
	int							LoadAndParse( bool unique = false );
	int							LoadAndParse( idFile *file );

public:
	idStr						fileName;
	declType_t					defaultType;

	ID_TIME_T						timestamp;
	int							checksum;
	int							fileSize;
	int							numLines;

	idDeclLocal *				decls;
};

class idDeclManagerLocal : public idDeclManager {
	friend class idDeclLocal;

public:
	virtual void				SetInsideLoad( bool var );
	virtual bool				GetInsideLoad( void );
	virtual void				Init( void );
	virtual void				Shutdown( void );
	virtual void				Reload( bool force );

	// RAVEN BEGIN
	// jscott: precache any guide (template) files
	virtual void				ParseGuides(void);
	virtual	void				ShutdownGuides(void);
	virtual bool				EvaluateGuide(idStr& name, idLexer* src, idStr& definition);
	virtual bool				EvaluateInlineGuide(idStr& name, idStr& definition);
	// RAVEN END

	virtual void				BeginLevelLoad();
	virtual void				EndLevelLoad();
	virtual void				RegisterDeclType( const char *typeName, declType_t type, idDecl *(*allocator)( void ) );
	virtual void				StartLoadingDecls();
	virtual void				FinishLoadingDecls();
	virtual void				LoadDeclsFromFile();
	virtual void				WriteDeclFile();
	virtual void				FlushDecls();
	virtual void				RegisterDeclFolderWrapper( const char *folder, const char *extension, declType_t defaultType, bool unique = false, bool norecurse = false );
	virtual void				RegisterDeclFolder( const char *folder, const char *extension, declType_t defaultType );
	virtual int					GetChecksum( void ) const;
	virtual int					GetNumDeclTypes( void ) const;
	virtual int					GetNumDecls( declType_t type );
	virtual const char *		GetDeclNameFromType( declType_t type ) const;
	virtual declType_t			GetDeclTypeFromName( const char *typeName ) const;
	virtual const idDecl *		FindType( declType_t type, const char *name, bool makeDefault = true, bool noCaching = false);
	virtual const idDecl *		DeclByIndex( declType_t type, int index, bool forceParse = true );

	virtual const idDecl*		FindDeclWithoutParsing( declType_t type, const char *name, bool makeDefault = true );
	virtual void				ReloadFile( const char* filename, bool force );

	virtual void				ListType( const idCmdArgs &args, declType_t type );
	virtual void				PrintType( const idCmdArgs &args, declType_t type );

	virtual idDecl *			CreateNewDecl( declType_t type, const char *name, const char *fileName );

	//BSM Added for the material editors rename capabilities
	virtual bool				RenameDecl( declType_t type, const char* oldName, const char* newName );

	virtual void				MediaPrint( const char *fmt, ... ) id_attribute((format(printf,2,3)));
	virtual void				WritePrecacheCommands( idFile *f );

									// Convenience functions for specific types.
	virtual	const idMaterial *		FindMaterial( const char *name, bool makeDefault = true );
	virtual const idDeclTable *		FindTable( const char *name, bool makeDefault = true );
	virtual const idDeclSkin *		FindSkin( const char *name, bool makeDefault = true );
	virtual const idSoundShader *	FindSound( const char *name, bool makeDefault = true );
// RAVEN BEGIN
// jscott: for new Raven decls
	virtual const rvDeclMatType *	FindMaterialType( const char *name, bool makeDefault = true );
	virtual	const rvDeclLipSync *	FindLipSync( const char *name, bool makeDefault = true );
	virtual	const rvDeclPlayback *	FindPlayback( const char *name, bool makeDefault = true );
	virtual	const rvDeclEffect *	FindEffect( const char *name, bool makeDefault = true );
// RAVEN END

	virtual const idMaterial *		MaterialByIndex( int index, bool forceParse = true );
	virtual const idDeclTable *		TableByIndex( int index, bool forceParse = true );
	virtual const idDeclSkin *		SkinByIndex( int index, bool forceParse = true );
	virtual const idSoundShader *	SoundByIndex( int index, bool forceParse = true );
// RAVEN BEGIN
// jscott: for new Raven decls
	virtual const rvDeclMatType *	MaterialTypeByIndex( int index, bool forceParse = true );
	virtual const rvDeclLipSync *	LipSyncByIndex( int index, bool forceParse = true );
	virtual	const rvDeclPlayback *	PlaybackByIndex( int index, bool forceParse = true );
	virtual const rvDeclEffect *	EffectByIndex( int index, bool forceParse = true );

	virtual void					StartPlaybackRecord( rvDeclPlayback *playback );
	virtual bool					SetPlaybackData( rvDeclPlayback *playback, int now, int control, class rvDeclPlaybackData *pbd );
	virtual bool					GetPlaybackData( const rvDeclPlayback *playback, int control, int now, int last, class rvDeclPlaybackData *pbd );
	virtual bool					FinishPlayback( rvDeclPlayback *playback );
	virtual idStr					GetNewName( declType_t type, const char *base );
	virtual const char *			GetDeclTypeName( declType_t type );
	virtual size_t					ListDeclSummary( const idCmdArgs &args );
	virtual void					RemoveDeclFile( const char *file );
	virtual bool					Validate( declType_t type, int iIndex, idStr &strReportTo );
	virtual idDecl *				AllocateDecl( declType_t type );

public:
	static void					MakeNameCanonical( const char *name, char *result, int maxLength );
	idDeclLocal *				FindTypeWithoutParsing( declType_t type, const char *name, bool makeDefault = true, int indexToStoreAt = -1 );

	idDeclType *				GetDeclType( int type ) const { return declTypes[type]; }
	const idDeclFile *			GetImplicitDeclFile( void ) const { return &implicitDecls; }

	idHashTable<rvDeclGuide *>	guideTable;
private:
	void						RegisterDeclFolder( const char *folder, const char *extension, declType_t defaultType, bool unique, bool norecurse );
	idDeclFile *				FindLoadedDeclFile( const char *fileName );
	idDeclFile *				FindOrCreateLoadedDeclFile( const char *fileName, declType_t defaultType );
	idDeclFolder *				FindOrCreateDeclFolder( const char *folder, const char *extension, declType_t defaultType );
	bool						TypeListContains( const idList<declType_t> &types, declType_t type ) const;
	int							GetTotalTextMemory( declType_t type );
	int							NumWritableDecls( idDeclFile *declFile, const idList<declType_t> &typesToWrite, bool writeStubs );
	void						BuildPackedDeclText( idDeclLocal *decl, idStr &declText );
	void						WriteDeclFileWithMode( declSingleFileWriteMode_t writeMode );
	void						WriteDecls( idFile *file, idDeclFile *declFile, const idList<declType_t> &typesToWrite, bool writeStubs );
	void						WriteSingleDeclSection( idFile *file, const idList<declType_t> &typesToWrite, bool writeStubs );
	void						CheckDecls( void );
	void						DeleteLocalDecl( idDeclLocal *decl );
	rvDeclGuide *				GetNewGuide( idLexer *src, idStr &file );

	idList<idDeclType *>		declTypes;
	idList<idDeclFolder *>		declFolders;

	idList<idDeclFile *>		loadedFiles;
	idHashIndex					hashTables[DECL_MAX_TYPES];
	idList<idDeclLocal *>		linearLists[DECL_MAX_TYPES];
	idDeclFile					implicitDecls;	// this holds all the decls that were created because explicit
												// text definitions were not found. Decls that became default
												// because of a parse error are not in this list.
	int							checksum;		// checksum of all loaded decl text
	int							indent;			// for MediaPrint
	bool						insideLevelLoad;
	idFile *					singleDeclFile;

	static idCVar				decl_show;

private:
	static void					ListAllDecls_f( const idCmdArgs &args );
	static void					ListDecls_f( const idCmdArgs &args );
	static void					ReloadDecls_f( const idCmdArgs &args );
	static void					ReloadFile_f( const idCmdArgs &args );
	static void					ResaveDecl_f( const idCmdArgs &args );
	static void					WriteDeclFile_f( const idCmdArgs &args );
	static void					FlushDecls_f( const idCmdArgs &args );
	static void					CheckDecls_f( const idCmdArgs &args );
	static void					TouchDecl_f( const idCmdArgs &args );
};

idCVar idDeclManagerLocal::decl_show( "decl_show", "0", CVAR_SYSTEM, "set to 1 to print parses, 2 to also print references", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar com_SingleDeclFile( "com_SingleDeclFile", "0", CVAR_SYSTEM | CVAR_BOOL, "load decls from a packed single .decls file instead of scanning loose decl folders" );
static idCVar com_singleDeclFileName( "com_singleDeclFileName", "", CVAR_SYSTEM, "override packed decl file used by com_SingleDeclFile and writeDeclFile" );
static idCVar com_singleDeclFileWriteMode( "com_singleDeclFileWriteMode", "0", CVAR_SYSTEM | CVAR_INTEGER, "packed .decls writer policy: 0 = OpenQ4 extended game types, 1 = exact retail game types", 0, 1, idCmdSystem::ArgCompletion_Integer<0,1> );

static const declType_t declSingleFileFrameworkTypes[] = {
	DECL_TABLE,
	DECL_MATERIAL,
	DECL_SKIN,
	DECL_SOUND,
	DECL_MATERIALTYPE,
	DECL_LIPSYNC,
	DECL_PLAYBACK,
	DECL_EFFECT,
	DECL_PDA,
	DECL_VIDEO,
	DECL_AUDIO,
	DECL_EMAIL,
	DECL_MAPDEF
};

static const declType_t declSingleFileOpenQ4GameTypes[] = {
	DECL_ENTITYDEF,
	DECL_MODELDEF,
	DECL_MAPDEF,
	DECL_CAMERADEF,
	DECL_AF,
	DECL_MODELEXPORT,
	DECL_PLAYER_MODEL
};

static const declType_t declSingleFileRetailGameTypes[] = {
	DECL_ENTITYDEF,
	DECL_MAPDEF,
	DECL_CAMERADEF,
	DECL_AF,
	DECL_MODELEXPORT
};

static void DeclManager_GetSingleDeclFileName( idStr &fileName ) {
	const char *overrideName = com_singleDeclFileName.GetString();
	if ( overrideName != NULL && overrideName[0] != '\0' ) {
		fileName = overrideName;
		return;
	}

	const char *assetLogName = fileSystem->GetAssetLogName();
	if ( assetLogName != NULL && assetLogName[0] != '\0' ) {
		const char *baseName = assetLogName;
		const char *slash = strchr( assetLogName, '/' );
		if ( slash != NULL ) {
			baseName = slash + 1;
		}
		fileName = baseName;
		fileName.SetFileExtension( ".decls" );
		return;
	}

	fileName = "default.decls";
}

static void DeclManager_ShowReloadProgress( int fileIndex, int fileCount, const char *fileName ) {
	OpenQ4_ToolPrint( va( "%d/%d: %s\n", fileIndex + 1, fileCount, fileName ) );
}

idDeclManagerLocal	declManagerLocal;
idDeclManager *		declManager = &declManagerLocal;

static rvDeclGuide *DeclManager_FindGuide( const char *name ) {
	rvDeclGuide **guide;
	if ( declManagerLocal.guideTable.Get( name, &guide ) ) {
		return *guide;
	}
	return NULL;
}

static void DeclManager_SetGuide( rvDeclGuide *guide ) {
	if ( guide == NULL ) {
		return;
	}

	rvDeclGuide *storedGuide = guide;
	declManagerLocal.guideTable.Set( guide->GetName(), storedGuide );
}

static void DeclManager_ClearGuides( void ) {
	declManagerLocal.guideTable.DeleteContents();
}

/*
====================================================================================

 decl text huffman compression

====================================================================================
*/

const int MAX_HUFFMAN_SYMBOLS	= 256;

typedef struct huffmanNode_s {
	int						symbol;
	int						frequency;
	struct huffmanNode_s *	next;
	struct huffmanNode_s *	children[2];
} huffmanNode_t;

typedef struct huffmanCode_s {
	unsigned long			bits[8];
	int						numBits;
} huffmanCode_t;

// compression ratio = 64%
static int huffmanFrequencies[] = {
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00078fb6, 0x000352a7, 0x00000002, 0x00000001, 0x0002795e, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00049600, 0x000000dd, 0x00018732, 0x0000005a, 0x00000007, 0x00000092, 0x0000000a, 0x00000919,
    0x00002dcf, 0x00002dda, 0x00004dfc, 0x0000039a, 0x000058be, 0x00002d13, 0x00014d8c, 0x00023c60,
    0x0002ddb0, 0x0000d1fc, 0x000078c4, 0x00003ec7, 0x00003113, 0x00006b59, 0x00002499, 0x0000184a,
    0x0000250b, 0x00004e38, 0x000001ca, 0x00000011, 0x00000020, 0x000023da, 0x00000012, 0x00000091,
    0x0000000b, 0x00000b14, 0x0000035d, 0x0000137e, 0x000020c9, 0x00000e11, 0x000004b4, 0x00000737,
    0x000006b8, 0x00001110, 0x000006b3, 0x000000fe, 0x00000f02, 0x00000d73, 0x000005f6, 0x00000be4,
    0x00000d86, 0x0000014d, 0x00000d89, 0x0000129b, 0x00000db3, 0x0000015a, 0x00000167, 0x00000375,
    0x00000028, 0x00000112, 0x00000018, 0x00000678, 0x0000081a, 0x00000677, 0x00000003, 0x00018112,
    0x00000001, 0x000441ee, 0x000124b0, 0x0001fa3f, 0x00026125, 0x0005a411, 0x0000e50f, 0x00011820,
    0x00010f13, 0x0002e723, 0x00003518, 0x00005738, 0x0002cc26, 0x0002a9b7, 0x0002db81, 0x0003b5fa,
    0x000185d2, 0x00001299, 0x00030773, 0x0003920d, 0x000411cd, 0x00018751, 0x00005fbd, 0x000099b0,
    0x00009242, 0x00007cf2, 0x00002809, 0x00005a1d, 0x00000001, 0x00005a1d, 0x00000001, 0x00000001,

    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
};

static huffmanCode_t huffmanCodes[MAX_HUFFMAN_SYMBOLS];
static huffmanNode_t *huffmanTree = NULL;
static int totalUncompressedLength = 0;
static int totalCompressedLength = 0;
static int maxHuffmanBits = 0;


/*
================
ClearHuffmanFrequencies
================
*/
void ClearHuffmanFrequencies( void ) {
	int i;

	for( i = 0; i < MAX_HUFFMAN_SYMBOLS; i++ ) {
		huffmanFrequencies[i] = 1;
	}
}

/*
================
InsertHuffmanNode
================
*/
huffmanNode_t *InsertHuffmanNode( huffmanNode_t *firstNode, huffmanNode_t *node ) {
	huffmanNode_t *n, *lastNode;

	lastNode = NULL;
	for ( n = firstNode; n; n = n->next ) {
		if ( node->frequency <= n->frequency ) {
			break;
		}
		lastNode = n;
	}
	if ( lastNode ) {
		node->next = lastNode->next;
		lastNode->next = node;
	} else {
		node->next = firstNode;
		firstNode = node;
	}
	return firstNode;
}

/*
================
BuildHuffmanCode_r
================
*/
void BuildHuffmanCode_r( huffmanNode_t *node, huffmanCode_t code, huffmanCode_t codes[MAX_HUFFMAN_SYMBOLS] ) {
	if ( node->symbol == -1 ) {
		huffmanCode_t newCode = code;
		assert( code.numBits < sizeof( codes[0].bits ) * 8 );
		newCode.numBits++;
		if ( code.numBits > maxHuffmanBits ) {
			maxHuffmanBits = newCode.numBits;
		}
		BuildHuffmanCode_r( node->children[0], newCode, codes );
		newCode.bits[code.numBits >> 5] |= 1 << ( code.numBits & 31 );
		BuildHuffmanCode_r( node->children[1], newCode, codes );
	} else {
		assert( code.numBits <= sizeof( codes[0].bits ) * 8 );
		codes[node->symbol] = code;
	}
}

/*
================
FreeHuffmanTree_r
================
*/
void FreeHuffmanTree_r( huffmanNode_t *node ) {
	if ( node->symbol == -1 ) {
		FreeHuffmanTree_r( node->children[0] );
		FreeHuffmanTree_r( node->children[1] );
	}
	delete node;
}

/*
================
HuffmanHeight_r
================
*/
int HuffmanHeight_r( huffmanNode_t *node ) {
	if ( node == NULL ) {
		return -1;
	}
	int left = HuffmanHeight_r( node->children[0] );
	int right = HuffmanHeight_r( node->children[1] );
	if ( left > right ) {
		return left + 1;
	}
	return right + 1;
}

/*
================
SetupHuffman
================
*/
void SetupHuffman( void ) {
	int i, height;
	huffmanNode_t *firstNode, *node;
	huffmanCode_t code;

	firstNode = NULL;
	for( i = 0; i < MAX_HUFFMAN_SYMBOLS; i++ ) {
		node = new huffmanNode_t;
		node->symbol = i;
		node->frequency = huffmanFrequencies[i];
		node->next = NULL;
		node->children[0] = NULL;
		node->children[1] = NULL;
		firstNode = InsertHuffmanNode( firstNode, node );
	}

	for( i = 1; i < MAX_HUFFMAN_SYMBOLS; i++ ) {
		node = new huffmanNode_t;
		node->symbol = -1;
		node->frequency = firstNode->frequency + firstNode->next->frequency;
		node->next = NULL;
		node->children[0] = firstNode;
		node->children[1] = firstNode->next;
		firstNode = InsertHuffmanNode( firstNode->next->next, node );
	}

	maxHuffmanBits = 0;
	memset( &code, 0, sizeof( code ) );
	BuildHuffmanCode_r( firstNode, code, huffmanCodes );

	huffmanTree = firstNode;

	height = HuffmanHeight_r( firstNode );
	assert( maxHuffmanBits == height );
}

/*
================
ShutdownHuffman
================
*/
void ShutdownHuffman( void ) {
	if ( huffmanTree ) {
		FreeHuffmanTree_r( huffmanTree );
	}
}

/*
================
HuffmanCompressText
================
*/
int HuffmanCompressText( const char *text, int textLength, byte *compressed, int maxCompressedSize ) {
	int i, j;
	idBitMsg msg;

	totalUncompressedLength += textLength;

	msg.Init( compressed, maxCompressedSize );
	msg.BeginWriting();
	for ( i = 0; i < textLength; i++ ) {
		const huffmanCode_t &code = huffmanCodes[(unsigned char)text[i]];
		for ( j = 0; j < ( code.numBits >> 5 ); j++ ) {
			msg.WriteBits( code.bits[j], 32 );
		}
		if ( code.numBits & 31 ) {
			msg.WriteBits( code.bits[j], code.numBits & 31 );
		}
	}

	totalCompressedLength += msg.GetSize();

	return msg.GetSize();
}

/*
================
HuffmanDecompressText
================
*/
int HuffmanDecompressText( char *text, int textLength, const byte *compressed, int compressedSize ) {
	int i, bit;
	idBitMsg msg;
	huffmanNode_t *node;

	msg.Init( compressed, compressedSize );
	msg.SetSize( compressedSize );
	msg.BeginReading();
	for ( i = 0; i < textLength; i++ ) {
		node = huffmanTree;
		do {
			bit = msg.ReadBits( 1 );
			node = node->children[bit];
		} while( node->symbol == -1 );
		text[i] = node->symbol;
	}
	text[i] = '\0';
	return msg.GetReadCount();
}

/*
================
ListHuffmanFrequencies_f
================
*/
void ListHuffmanFrequencies_f( const idCmdArgs &args ) {
	int		i;
	float compression;
	compression = !totalUncompressedLength ? 100 : 100 * totalCompressedLength / totalUncompressedLength;
	common->Printf( "// compression ratio = %d%%\n", (int)compression );
	common->Printf( "static int huffmanFrequencies[] = {\n" );
	for( i = 0; i < MAX_HUFFMAN_SYMBOLS; i += 8 ) {
		common->Printf( "\t0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x,\n",
							huffmanFrequencies[i+0], huffmanFrequencies[i+1],
							huffmanFrequencies[i+2], huffmanFrequencies[i+3],
							huffmanFrequencies[i+4], huffmanFrequencies[i+5],
							huffmanFrequencies[i+6], huffmanFrequencies[i+7]);
	}
	common->Printf( "}\n" );
}

/*
====================================================================================

 idDeclFile

====================================================================================
*/

/*
================
idDeclFile::idDeclFile
================
*/
idDeclFile::idDeclFile( const char *fileName, declType_t defaultType ) {
	this->fileName = fileName;
	this->defaultType = defaultType;
	this->timestamp = 0;
	this->checksum = 0;
	this->fileSize = 0;
	this->numLines = 0;
	this->decls = NULL;
}

/*
================
idDeclFile::idDeclFile
================
*/
idDeclFile::idDeclFile() {
	this->fileName = "<implicit file>";
	this->defaultType = DECL_MAX_TYPES;
	this->timestamp = 0;
	this->checksum = 0;
	this->fileSize = 0;
	this->numLines = 0;
	this->decls = NULL;
}

/*
================
idDeclFile::Reload

ForceReload will cause it to reload even if the timestamp hasn't changed
================
*/
void idDeclFile::Reload( bool force ) {
	// check for an unchanged timestamp
	if ( !force && timestamp != 0 ) {
		ID_TIME_T	testTimeStamp;
		fileSystem->ReadFile( fileName, NULL, &testTimeStamp );

		if ( testTimeStamp == timestamp ) {
			return;
		}
	}

	// parse the text
	LoadAndParse();
}
/*
================
idDeclFile::LoadAndParse

This is used during both the initial load, and any reloads
================
*/
int c_savedMemory = 0;

static bool ReadPackedDeclLine( idFile *file, idStr &line ) {
	line.Empty();

	char ch;
	while ( file->Read( &ch, 1 ) == 1 ) {
		if ( ch == '\n' ) {
			return true;
		}
		if ( ch != '\r' ) {
			line.Append( ch );
		}
	}

	return line.Length() > 0;
}

static void MakeDeclTypeList( idList<declType_t> &list, const declType_t *types, int numTypes ) {
	list.SetNum( numTypes );
	for ( int i = 0; i < numTypes; i++ ) {
		list[i] = types[i];
	}
}

static const char *DeclManager_GetSingleDeclWriteModeName( declSingleFileWriteMode_t writeMode ) {
	switch ( writeMode ) {
		case DECL_SINGLEFILE_WRITE_RETAIL:
			return "retail";

		case DECL_SINGLEFILE_WRITE_OPENQ4:
		default:
			return "openq4";
	}
}

static declSingleFileWriteMode_t DeclManager_GetConfiguredSingleDeclWriteMode( void ) {
	if ( com_singleDeclFileWriteMode.GetInteger() == DECL_SINGLEFILE_WRITE_RETAIL ) {
		return DECL_SINGLEFILE_WRITE_RETAIL;
	}

	return DECL_SINGLEFILE_WRITE_OPENQ4;
}

static bool DeclManager_ParseSingleDeclWriteMode( const char *modeName, declSingleFileWriteMode_t &writeMode ) {
	if ( modeName == NULL || modeName[0] == '\0' ) {
		return false;
	}

	if ( !idStr::Icmp( modeName, "openq4" ) || !idStr::Icmp( modeName, "0" ) ) {
		writeMode = DECL_SINGLEFILE_WRITE_OPENQ4;
		return true;
	}

	if ( !idStr::Icmp( modeName, "retail" ) || !idStr::Icmp( modeName, "1" ) ) {
		writeMode = DECL_SINGLEFILE_WRITE_RETAIL;
		return true;
	}

	return false;
}

static void DeclManager_MakeGameDeclTypeList( idList<declType_t> &list, declSingleFileWriteMode_t writeMode ) {
	if ( writeMode == DECL_SINGLEFILE_WRITE_RETAIL ) {
		MakeDeclTypeList( list, declSingleFileRetailGameTypes, sizeof( declSingleFileRetailGameTypes ) / sizeof( declSingleFileRetailGameTypes[0] ) );
		return;
	}

	MakeDeclTypeList( list, declSingleFileOpenQ4GameTypes, sizeof( declSingleFileOpenQ4GameTypes ) / sizeof( declSingleFileOpenQ4GameTypes[0] ) );
}

static bool DeclManager_WriteProgramImagesEnabled( void ) {
	return cvarSystem != NULL &&
		cvarSystem->GetCVarBool( DECL_WRITE_PROGRAM_IMAGES_CVAR ) &&
		renderSystem != NULL &&
		renderSystem->IsOpenGLRunning();
}

int idDeclFile::LoadAndParse( bool unique ) {
	int			i, numTypes;
	idLexer		src;
	idToken		token;
	int			startMarker;
	char *		buffer;
	int			length, size;
	int			sourceLine;
	idStr		name;
	idStr		strippedName;
	idDeclLocal *newDecl;
	bool		referencedThisLevel;

	// load the text
	common->DPrintf( "...loading '%s'\n", fileName.c_str() );
	length = fileSystem->ReadFile( fileName, (void **)&buffer, &timestamp );
	if ( length == -1 ) {
		common->FatalError( "couldn't load %s", fileName.c_str() );
		return 0;
	}
	idStr finalPreprocessedBuffer = buffer;
	Mem_Free(buffer);

	strippedName = fileName;
	strippedName.StripFileExtension();

	if ( !src.LoadMemory(finalPreprocessedBuffer.c_str(), finalPreprocessedBuffer.Length(), fileName ) ) {
		common->Error( "Couldn't parse %s", fileName.c_str() );
		return 0;
	}

	// mark all the defs that were from the last reload of this file
	for ( idDeclLocal *decl = decls; decl; decl = decl->nextInFile ) {
		decl->redefinedInReload = false;
	}

	src.SetFlags( DECL_LEXER_FLAGS );
	checksum = MD5_BlockChecksum(finalPreprocessedBuffer.c_str(), finalPreprocessedBuffer.Length());
	fileSize = length;

	// scan through, identifying each individual declaration
	while( 1 ) {

		startMarker = src.GetFileOffset();
		sourceLine = src.GetLineNum();

		// parse the decl type name
		if ( !src.ReadToken( &token ) ) {
			break;
		}

		bool guide = false;
		if ( token.Icmp( "guide" ) == 0 ) {
			guide = true;
			if ( !src.ReadToken( &token ) ) {
				src.Warning( "Type without definition at end of file" );
				break;
			}
		}

		declType_t identifiedType = DECL_MAX_TYPES;

		// get the decl type from the type name
		numTypes = declManagerLocal.GetNumDeclTypes();
		for ( i = 0; i < numTypes; i++ ) {
			idDeclType *typeInfo = declManagerLocal.GetDeclType( i );
			if ( typeInfo && typeInfo->typeName.Icmp( token ) == 0 ) {
				identifiedType = (declType_t) typeInfo->type;
				break;
			}
		}

		if ( i >= numTypes ) {

			if ( token.Icmp( "{" ) == 0 ) {

				// if we ever see an open brace, we somehow missed the [type] <name> prefix
				src.Warning( "Missing decl name" );
				src.SkipBracedSection( false );
				continue;

			} else {

				if ( defaultType == DECL_MAX_TYPES ) {
					src.Warning( "No type" );
					continue;
				}
				src.UnreadToken( &token );
				// use the default type
				identifiedType = defaultType;
			}
		}

		// now parse the name
		if ( !src.ReadToken( &token ) ) {
			src.Warning( "Type without definition at end of file" );
			break;
		}

		if ( !token.Icmp( "{" ) ) {
			// if we ever see an open brace, we somehow missed the [type] <name> prefix
			src.Warning( "Missing decl name" );
			src.SkipBracedSection( false );
			continue;
		}

		// FIXME: export decls are only used by the model exporter, they are skipped here for now
		if ( identifiedType == DECL_MODELEXPORT ) {
			src.SkipBracedSection();
			continue;
		}

		name = token;
		if ( unique && strippedName.Cmp( name ) != 0 ) {
			src.Warning( "%s must be in a file of the same name", name.c_str() );
		}

		idStr declDefinition;
		if ( guide ) {
			declManagerLocal.EvaluateGuide( name, &src, declDefinition );
		} else {
			src.ParseBracedSectionExact( declDefinition, -1 );
		}
		declManagerLocal.EvaluateInlineGuide( name, declDefinition );
		size = src.GetFileOffset() - startMarker;

		// look it up, possibly getting a newly created default decl
		referencedThisLevel = false;
		newDecl = declManagerLocal.FindTypeWithoutParsing( identifiedType, name, false );
		if ( newDecl ) {
			// update the existing copy
			if ( newDecl->sourceFile == this && !newDecl->redefinedInReload ) {
				referencedThisLevel = newDecl->referencedThisLevel;
			} else {
				src.Warning( "%s '%s' previously defined at %s:%i", declManagerLocal.GetDeclNameFromType( identifiedType ),
								name.c_str(), newDecl->sourceFile->fileName.c_str(), newDecl->sourceLine );
				continue;
			}
		} else {
			// allow it to be created as a default, then add it to the per-file list
			newDecl = declManagerLocal.FindTypeWithoutParsing( identifiedType, name, true );
			newDecl->nextInFile = this->decls;
			this->decls = newDecl;
		}

		newDecl->redefinedInReload = true;

		if ( newDecl->textSource ) {
			Mem_Free( newDecl->textSource );
			newDecl->textSource = NULL;
		}

		newDecl->SetTextLocal( declDefinition.c_str(), declDefinition.Length() );
		newDecl->sourceFile = this;
		newDecl->sourceTextOffset = startMarker;
		newDecl->sourceTextLength = size;
		newDecl->sourceLine = sourceLine;
		newDecl->declState = DS_UNPARSED;

		// if it is currently in use, or the program-image writer needs every material parsed, reparse it immediately
		if ( referencedThisLevel || DeclManager_WriteProgramImagesEnabled() ) {
			newDecl->ParseLocal();
		}
	}

	numLines = src.GetLineNum();

	// any defs that weren't redefinedInReload should now be defaulted
	for ( idDeclLocal *decl = decls ; decl ; decl = decl->nextInFile ) {
		if ( decl->redefinedInReload == false ) {
			decl->MakeDefault();
			decl->sourceTextOffset = decl->sourceFile->fileSize;
			decl->sourceTextLength = 0;
			decl->sourceLine = decl->sourceFile->numLines;
		}
	}

	return checksum;
}

/*
================
idDeclFile::LoadAndParse

Reads one packed decl-file entry from a single .decls stream.
================
*/
int idDeclFile::LoadAndParse( idFile *file ) {
	idStr declCountString;
	if ( !ReadPackedDeclLine( file, declCountString ) ) {
		common->Warning( "Missing decl count while loading packed decl file '%s'", fileName.c_str() );
		return 0;
	}

	const int declCount = atoi( declCountString.c_str() );
	for ( idDeclLocal *decl = decls; decl; decl = decl->nextInFile ) {
		decl->redefinedInReload = false;
	}

	for ( int packedDeclIndex = 0; packedDeclIndex < declCount; packedDeclIndex++ ) {
		idStr indexString;
		idStr sizeString;

		if ( !ReadPackedDeclLine( file, indexString ) || !ReadPackedDeclLine( file, sizeString ) ) {
			common->Warning( "Truncated packed decl metadata in '%s'", fileName.c_str() );
			break;
		}

		const int indexToStoreAt = atoi( indexString.c_str() );
		const int packedTextLength = atoi( sizeString.c_str() );
		if ( packedTextLength <= 0 ) {
			common->Warning( "Invalid packed decl length %d in '%s'", packedTextLength, fileName.c_str() );
			continue;
		}

		char *buffer = (char *)Mem_Alloc( packedTextLength + 1 );
		const int readBytes = file->Read( buffer, packedTextLength );
		if ( readBytes <= 0 ) {
			Mem_Free( buffer );
			common->Warning( "Could not read packed decl text in '%s'", fileName.c_str() );
			break;
		}
		buffer[readBytes] = '\0';

		idStr packedText;
		packedText.Append( buffer, readBytes );
		Mem_Free( buffer );

		checksum = MD5_BlockChecksum( packedText.c_str(), readBytes );
		fileSize = readBytes;

		idLexer src;
		idToken token;
		if ( !src.LoadMemory( packedText.c_str(), packedText.Length(), fileName.c_str() ) ) {
			common->Warning( "Couldn't parse packed decl text in '%s'", fileName.c_str() );
			continue;
		}
		src.SetFlags( DECL_LEXER_FLAGS );

		const int startMarker = src.GetFileOffset();
		const int sourceLine = src.GetLineNum();

		if ( !src.ReadToken( &token ) ) {
			break;
		}

		bool guide = false;
		if ( token.Icmp( "guide" ) == 0 ) {
			guide = true;
			if ( !src.ReadToken( &token ) ) {
				src.Warning( "Type without definition at end of packed decl" );
				break;
			}
		}

		declType_t identifiedType = DECL_MAX_TYPES;
		const int numTypes = declManagerLocal.GetNumDeclTypes();
		for ( int i = 0; i < numTypes; i++ ) {
			idDeclType *typeInfo = declManagerLocal.GetDeclType( i );
			if ( typeInfo && typeInfo->typeName.Icmp( token ) == 0 ) {
				identifiedType = (declType_t)typeInfo->type;
				break;
			}
		}

		if ( identifiedType == DECL_MAX_TYPES ) {
			if ( token.Icmp( "{" ) == 0 ) {
				src.Warning( "Missing decl name" );
				src.SkipBracedSection( false );
				continue;
			}
			if ( defaultType == DECL_MAX_TYPES ) {
				src.Warning( "No type" );
				continue;
			}
			src.UnreadToken( &token );
			identifiedType = defaultType;
		}

		if ( !src.ReadToken( &token ) ) {
			src.Warning( "Type without definition at end of packed decl" );
			break;
		}

		if ( token.Icmp( "{" ) == 0 ) {
			src.Warning( "Missing decl name" );
			src.SkipBracedSection( false );
			continue;
		}

		if ( identifiedType == DECL_MODELEXPORT ) {
			src.SkipBracedSection();
			continue;
		}

		idStr name = token;
		idStr declDefinition;

		if ( guide ) {
			declManagerLocal.EvaluateGuide( name, &src, declDefinition );
		} else {
			src.ParseBracedSectionExact( declDefinition, -1 );
		}
		declManagerLocal.EvaluateInlineGuide( name, declDefinition );
		int sourceTextLength = src.GetFileOffset() - startMarker;

		idDeclLocal *decl = declManagerLocal.FindTypeWithoutParsing( identifiedType, name, false );
		if ( decl ) {
			if ( decl->sourceFile != this || decl->redefinedInReload ) {
				src.Warning( "%s '%s' previously defined at %s:%i",
					declManagerLocal.GetDeclNameFromType( identifiedType ),
					name.c_str(),
					decl->sourceFile ? decl->sourceFile->fileName.c_str() : "*unknown*",
					decl->sourceLine );
				continue;
			}
		} else {
			decl = declManagerLocal.FindTypeWithoutParsing( identifiedType, name, true, indexToStoreAt );
			decl->nextInFile = this->decls;
			this->decls = decl;
		}

		decl->declState = DS_UNPARSED;
		decl->redefinedInReload = true;
		if ( decl->textSource ) {
			if ( decl->needsPrecache ) {
				continue;
			}
			Mem_Free( decl->textSource );
			decl->textSource = NULL;
		}

		if ( !decl->needsPrecache ) {
			decl->SetTextLocal( declDefinition.c_str(), declDefinition.Length() );
			decl->sourceFile = this;
			decl->sourceTextOffset = startMarker;
			decl->sourceTextLength = sourceTextLength;
			decl->sourceLine = sourceLine;
		}
	}

	for ( idDeclLocal *decl = decls; decl; decl = decl->nextInFile ) {
		if ( decl->redefinedInReload == false ) {
			decl->MakeDefault();
			decl->sourceTextOffset = decl->sourceFile->fileSize;
			decl->sourceTextLength = 0;
			decl->sourceLine = decl->sourceFile->numLines;
		}
	}

	return checksum;
}

/*
====================================================================================

 idDeclManagerLocal

====================================================================================
*/

const char *listDeclStrings[] = { "current", "all", "ever", NULL };

/*
===================
idDeclManagerLocal::Init
===================
*/
void idDeclManagerLocal::Init( void ) {

	common->Printf( "----- Initializing Decls -----\n" );

	checksum = 0;

#ifdef USE_COMPRESSED_DECLS
	SetupHuffman();
#endif

#ifdef GET_HUFFMAN_FREQUENCIES
	ClearHuffmanFrequencies();
#endif

// jmarshall - template(guide) Support
	ParseGuides();
// jmarshall end

	// decls used throughout the engine
	RegisterDeclType( "table",				DECL_TABLE,			idDeclAllocator<idDeclTable> );
	RegisterDeclType( "material",			DECL_MATERIAL,		idDeclAllocator<idMaterial> );
	RegisterDeclType( "skin",				DECL_SKIN,			idDeclAllocator<idDeclSkin> );
	RegisterDeclType( "sound",				DECL_SOUND,			idDeclAllocator<idSoundShader> );
	RegisterDeclType( "entityDef",			DECL_ENTITYDEF,		idDeclAllocator<idDeclEntityDef> );
	RegisterDeclType( "mapDef",				DECL_MAPDEF,		idDeclAllocator<idDeclEntityDef> );

// jmarshall: Raven Decl Support
	RegisterDeclType(  "materialType",		DECL_MATERIALTYPE,  idDeclAllocator<rvDeclMatType>);
	RegisterDeclType(  "lipSync",			DECL_LIPSYNC,		idDeclAllocator<rvDeclLipSync>);
	RegisterDeclType(  "playback",			DECL_PLAYBACK,		idDeclAllocator<rvDeclPlayback>);
	OpenQ4_VerifyEffectDeclAllocator();
	RegisterDeclType(	"effect",			DECL_EFFECT,		OpenQ4_AllocEffectDecl);
// jmarshall end

// jmarshall: Raven Decl Support
	//RegisterDeclType( "fx",					DECL_FX,			idDeclAllocator<idDeclFX> );
	//RegisterDeclType( "particle",			DECL_PARTICLE,		idDeclAllocator<idDeclParticle> );
// jmarshall end
	RegisterDeclType( "articulatedFigure",	DECL_AF,			idDeclAllocator<idDeclAF> );
	RegisterDeclType( "pda",				DECL_PDA,			idDeclAllocator<idDeclPDA> );
	RegisterDeclType( "email",				DECL_EMAIL,			idDeclAllocator<idDeclEmail> );
	RegisterDeclType( "video",				DECL_VIDEO,			idDeclAllocator<idDeclVideo> );
	RegisterDeclType( "audio",				DECL_AUDIO,			idDeclAllocator<idDeclAudio> );
	RegisterDeclType( "playerModel",			DECL_PLAYER_MODEL,	idDeclAllocator<rvDeclPlayerModel> );

	if ( com_SingleDeclFile.GetBool() ) {
		StartLoadingDecls();
		LoadDeclsFromFile();
		cmdSystem->AddCommand( "flushDecls", FlushDecls_f, CMD_FL_SYSTEM, "deallocates current decl data" );
		cmdSystem->AddCommand( "checkDecls", CheckDecls_f, CMD_FL_SYSTEM, "parses every loaded decl" );
	} else {
		cmdSystem->AddCommand( "writeDeclFile", WriteDeclFile_f, CMD_FL_SYSTEM, "writes parsed decls to a packed .decls file (optional mode: openq4 or retail)" );

		RegisterDeclFolderWrapper( "materials",		".mtr",			DECL_MATERIAL );
		RegisterDeclFolderWrapper( "skins",			".skin",		DECL_SKIN );
		RegisterDeclFolderWrapper( "sound",			".sndshd",		DECL_SOUND, false, true );

		// jmarshall: Raven Decl Support
		RegisterDeclFolderWrapper( "materials/types",	".mtt",			DECL_MATERIALTYPE );
		RegisterDeclFolderWrapper( "lipsync",			".lipsync",		DECL_LIPSYNC );
		RegisterDeclFolderWrapper( "playbacks",			".playback",	DECL_PLAYBACK, true );
		RegisterDeclFolderWrapper( "effects",			".fx",			DECL_EFFECT, true );
// jmarshall end
	}

	// add console commands
	cmdSystem->AddCommand( "listDecls", ListDecls_f, CMD_FL_SYSTEM, "lists all decls" );
	cmdSystem->AddCommand( "listAllDecls", ListAllDecls_f, CMD_FL_SYSTEM, "lists every decl name grouped by type" );

	cmdSystem->AddCommand( "reloadDecls", ReloadDecls_f, CMD_FL_SYSTEM, "reloads decls" );
	cmdSystem->AddCommand( "reloadFile", ReloadFile_f, CMD_FL_SYSTEM, "reloads a single decl file" );
	cmdSystem->AddCommand( "resaveDecl", ResaveDecl_f, CMD_FL_SYSTEM, "resaves one decl or every decl of a type" );
	cmdSystem->AddCommand( "touch", TouchDecl_f, CMD_FL_SYSTEM, "touches a decl" );

	cmdSystem->AddCommand( "listTables", idListDecls_f<DECL_TABLE>, CMD_FL_SYSTEM, "lists tables", idCmdSystem::ArgCompletion_String<listDeclStrings> );
	cmdSystem->AddCommand( "listMaterials", idListDecls_f<DECL_MATERIAL>, CMD_FL_SYSTEM, "lists materials", idCmdSystem::ArgCompletion_String<listDeclStrings> );
	cmdSystem->AddCommand( "listSkins", idListDecls_f<DECL_SKIN>, CMD_FL_SYSTEM, "lists skins", idCmdSystem::ArgCompletion_String<listDeclStrings> );
	cmdSystem->AddCommand( "listSoundShaders", idListDecls_f<DECL_SOUND>, CMD_FL_SYSTEM, "lists sound shaders", idCmdSystem::ArgCompletion_String<listDeclStrings> );

	cmdSystem->AddCommand( "listEntityDefs", idListDecls_f<DECL_ENTITYDEF>, CMD_FL_SYSTEM, "lists entity defs", idCmdSystem::ArgCompletion_String<listDeclStrings> );
	//cmdSystem->AddCommand( "listFX", idListDecls_f<DECL_FX>, CMD_FL_SYSTEM, "lists FX systems", idCmdSystem::ArgCompletion_String<listDeclStrings> );
	//cmdSystem->AddCommand( "listParticles", idListDecls_f<DECL_PARTICLE>, CMD_FL_SYSTEM, "lists particle systems", idCmdSystem::ArgCompletion_String<listDeclStrings> //);
	cmdSystem->AddCommand( "listAF", idListDecls_f<DECL_AF>, CMD_FL_SYSTEM, "lists articulated figures", idCmdSystem::ArgCompletion_String<listDeclStrings>);
	cmdSystem->AddCommand( "listPDAs", idListDecls_f<DECL_PDA>, CMD_FL_SYSTEM, "lists PDAs", idCmdSystem::ArgCompletion_String<listDeclStrings> );
	cmdSystem->AddCommand( "listEmails", idListDecls_f<DECL_EMAIL>, CMD_FL_SYSTEM, "lists Emails", idCmdSystem::ArgCompletion_String<listDeclStrings> );
	cmdSystem->AddCommand( "listVideos", idListDecls_f<DECL_VIDEO>, CMD_FL_SYSTEM, "lists Videos", idCmdSystem::ArgCompletion_String<listDeclStrings> );
	cmdSystem->AddCommand( "listAudios", idListDecls_f<DECL_AUDIO>, CMD_FL_SYSTEM, "lists Audios", idCmdSystem::ArgCompletion_String<listDeclStrings> );
	cmdSystem->AddCommand( "listMaterialTypes", idListDecls_f<DECL_MATERIALTYPE>, CMD_FL_SYSTEM, "lists material types", idCmdSystem::ArgCompletion_String<listDeclStrings> );
	cmdSystem->AddCommand( "listLipsyncs", idListDecls_f<DECL_LIPSYNC>, CMD_FL_SYSTEM, "lists lip sync decls", idCmdSystem::ArgCompletion_String<listDeclStrings> );
	cmdSystem->AddCommand( "listPlaybacks", idListDecls_f<DECL_PLAYBACK>, CMD_FL_SYSTEM, "lists playback decls", idCmdSystem::ArgCompletion_String<listDeclStrings> );
	cmdSystem->AddCommand( "listEffects", idListDecls_f<DECL_EFFECT>, CMD_FL_SYSTEM, "lists effects", idCmdSystem::ArgCompletion_String<listDeclStrings> );

	cmdSystem->AddCommand( "printTable", idPrintDecls_f<DECL_TABLE>, CMD_FL_SYSTEM, "prints a table", idCmdSystem::ArgCompletion_Decl<DECL_TABLE> );
	cmdSystem->AddCommand( "printMaterial", idPrintDecls_f<DECL_MATERIAL>, CMD_FL_SYSTEM, "prints a material", idCmdSystem::ArgCompletion_Decl<DECL_MATERIAL> );
	cmdSystem->AddCommand( "printSkin", idPrintDecls_f<DECL_SKIN>, CMD_FL_SYSTEM, "prints a skin", idCmdSystem::ArgCompletion_Decl<DECL_SKIN> );
	cmdSystem->AddCommand( "printSoundShader", idPrintDecls_f<DECL_SOUND>, CMD_FL_SYSTEM, "prints a sound shader", idCmdSystem::ArgCompletion_Decl<DECL_SOUND> );

	cmdSystem->AddCommand( "printEntityDef", idPrintDecls_f<DECL_ENTITYDEF>, CMD_FL_SYSTEM, "prints an entity def", idCmdSystem::ArgCompletion_Decl<DECL_ENTITYDEF> );
	//cmdSystem->AddCommand( "printFX", idPrintDecls_f<DECL_FX>, CMD_FL_SYSTEM, "prints an FX system", idCmdSystem::ArgCompletion_Decl<DECL_FX> );
//	cmdSystem->AddCommand( "printParticle", idPrintDecls_f<DECL_PARTICLE>, CMD_FL_SYSTEM, "prints a particle system", idCmdSystem::ArgCompletion_Decl<DECL_PARTICLE> );
	cmdSystem->AddCommand( "printAF", idPrintDecls_f<DECL_AF>, CMD_FL_SYSTEM, "prints an articulated figure", idCmdSystem::ArgCompletion_Decl<DECL_AF> );
	cmdSystem->AddCommand( "printPDA", idPrintDecls_f<DECL_PDA>, CMD_FL_SYSTEM, "prints an PDA", idCmdSystem::ArgCompletion_Decl<DECL_PDA> );
	cmdSystem->AddCommand( "printEmail", idPrintDecls_f<DECL_EMAIL>, CMD_FL_SYSTEM, "prints an Email", idCmdSystem::ArgCompletion_Decl<DECL_EMAIL> );
	cmdSystem->AddCommand( "printVideo", idPrintDecls_f<DECL_VIDEO>, CMD_FL_SYSTEM, "prints a Audio", idCmdSystem::ArgCompletion_Decl<DECL_VIDEO> );
	cmdSystem->AddCommand( "printAudio", idPrintDecls_f<DECL_AUDIO>, CMD_FL_SYSTEM, "prints an Video", idCmdSystem::ArgCompletion_Decl<DECL_AUDIO> );
	cmdSystem->AddCommand( "printMaterialTypes", idPrintDecls_f<DECL_MATERIALTYPE>, CMD_FL_SYSTEM, "prints material types", idCmdSystem::ArgCompletion_Decl<DECL_MATERIALTYPE> );
	cmdSystem->AddCommand( "printLipsyncs", idPrintDecls_f<DECL_LIPSYNC>, CMD_FL_SYSTEM, "prints lip syncs", idCmdSystem::ArgCompletion_Decl<DECL_LIPSYNC> );
	cmdSystem->AddCommand( "printPlaybacks", idPrintDecls_f<DECL_PLAYBACK>, CMD_FL_SYSTEM, "prints playbacks", idCmdSystem::ArgCompletion_Decl<DECL_PLAYBACK> );
	cmdSystem->AddCommand( "printEffects", idPrintDecls_f<DECL_EFFECT>, CMD_FL_SYSTEM, "prints effects", idCmdSystem::ArgCompletion_Decl<DECL_EFFECT> );

	cmdSystem->AddCommand( "listHuffmanFrequencies", ListHuffmanFrequencies_f, CMD_FL_SYSTEM, "lists decl text character frequencies" );

	common->Printf( "------------------------------\n" );
}

/*
===================
idDeclManagerLocal::Shutdown
===================
*/
void idDeclManagerLocal::Shutdown( void ) {
	int			i, j;
	idDeclLocal *decl;

	FinishLoadingDecls();
	ShutdownGuides();

	// free decls
	for ( i = 0; i < DECL_MAX_TYPES; i++ ) {
		for ( j = 0; j < linearLists[i].Num(); j++ ) {
			decl = linearLists[i][j];
			if ( decl->self != NULL ) {
				decl->self->FreeData();
				delete decl->self;
			}
			if ( decl->textSource ) {
				Mem_Free( decl->textSource );
				decl->textSource = NULL;
			}
			delete decl;
		}
		linearLists[i].Clear();
		hashTables[i].Free();
	}

	// free decl files
	loadedFiles.DeleteContents( true );

	// free the decl types and folders
	declTypes.DeleteContents( true );
	declFolders.DeleteContents( true );

#ifdef USE_COMPRESSED_DECLS
	ShutdownHuffman();
#endif
}

// jmarshall: Quake 4 Guide Support
/*
=========================
idDeclManagerLocal::GetNewGuide
=========================
*/
rvDeclGuide *idDeclManagerLocal::GetNewGuide( idLexer *src, idStr &file ) {
	idToken guideName;
	if ( !src->ReadToken( &guideName ) ) {
		common->Printf( "Guide file '%s' has unknown token '%s'\n", file.c_str(), guideName.c_str() );
		return NULL;
	}

	if ( DeclManager_FindGuide( guideName.c_str() ) != NULL ) {
		common->Printf( "Guide file '%s' contains duplicate guide '%s'\n", file.c_str(), guideName.c_str() );
		src->SkipBracedSection( true );
		return NULL;
	}

	rvDeclGuide *guide = new rvDeclGuide( guideName );
	guide->Parse( src );
	return guide;
}

/*
=========================
idDeclManagerLocal::ParseGuides
=========================
*/
void idDeclManagerLocal::ParseGuides(void) {
	common->Printf( "Loading guides.... " );
	ShutdownGuides();

	idFileList *fileList = fileSystem->ListFiles( DECL_GUIDE_FOLDER, DECL_GUIDE_EXTENSION, true );
	for ( int i = 0; fileList != NULL && i < fileList->GetNumFiles(); i++ ) {
		idLexer src;
		idToken	token;
		idStr fileName = fileList->GetList()[i];
		idStr fullPath = DECL_GUIDE_PATH_PREFIX;
		fullPath += fileName;

		if ( !src.LoadFile( fullPath.c_str() ) ) {
			fileSystem->FreeFileList( fileList );
			common->Printf( "\n" );
			common->FatalError( "Couldn't load %s", fullPath.c_str() );
			return;
		}
		src.SetFlags( DECL_GUIDE_FILE_LEXER_FLAGS );
		
		while ( src.ReadToken( &token ) ) {
			if ( token.Icmp( "guide" ) == 0 ) {
				rvDeclGuide *guide = GetNewGuide( &src, fullPath );
				if ( guide != NULL ) {
					DeclManager_SetGuide( guide );
				}
				continue;
			}
			if ( token.Icmp( "inlineGuide" ) == 0 ) {
				rvDeclGuide *guide = GetNewGuide( &src, fullPath );
				if ( guide != NULL ) {
					guide->RemoveOuterBracing();
					DeclManager_SetGuide( guide );
				}
			}
		}
	}

	if ( fileList != NULL ) {
		fileSystem->FreeFileList( fileList );
	}
	common->Printf( "%d loaded\n", guideTable.Num() );
}

/*
=========================
idDeclManagerLocal::ShutdownGuides
=========================
*/
void idDeclManagerLocal::ShutdownGuides( void ) {
	DeclManager_ClearGuides();
}

/*
=========================
rvDeclGuide::rvDeclGuide
=========================
*/
rvDeclGuide::rvDeclGuide( idStr &name ) {
	mName = name;
	mNumParms = 0;
}

/*
=========================
rvDeclGuide::~rvDeclGuide
=========================
*/
rvDeclGuide::~rvDeclGuide( void ) {
}

/*
=========================
rvDeclGuide::SetParm
=========================
*/
void rvDeclGuide::SetParm( int index, const char *value ) {
	assert( index >= 0 && index < MAX_GUIDE_PARMS );
	if ( index < 0 || index >= MAX_GUIDE_PARMS ) {
		return;
	}
	mParms[index] = value;
	if ( index >= mNumParms ) {
		mNumParms = index + 1;
	}
}

/*
=========================
rvDeclGuide::Parse
=========================
*/
void rvDeclGuide::Parse( idLexer *src ) {
	idToken token;
	int commaCount = 0;

	src->ExpectTokenString( "(" );
	mNumParms = 0;

	while ( src->ReadToken( &token ) && token.Cmp( ")" ) != 0 ) {
		if ( token.Cmp( "," ) != 0 ) {
			if ( mNumParms < MAX_GUIDE_PARMS ) {
				mParms[mNumParms] = token;
			}
			mNumParms++;
		} else {
			commaCount++;
		}
	}

	if ( commaCount != mNumParms - 1 ) {
		src->Warning( "Guide name '%s' only contains %d commas for %d args, expecting %d! Typo?\n",
			mName.c_str(), commaCount, mNumParms, mNumParms - 1 );
	}

	src->ParseBracedSectionExact( mDefinition, -1 );
}

/*
=========================
rvDeclGuide::RemoveOuterBracing
=========================
*/
void rvDeclGuide::RemoveOuterBracing( void ) {
	const int closeBrace = mDefinition.Last( '}' );
	if ( closeBrace > 0 ) {
		mDefinition = mDefinition.Left( closeBrace - 1 );
	}

	const int openBrace = mDefinition.Find( '{' );
	if ( openBrace >= 0 ) {
		mDefinition = mDefinition.Mid( openBrace + 1, mDefinition.Length() - openBrace - 1 );
	}
}

/*
=========================
rvDeclGuide::Evaluate
=========================
*/
bool rvDeclGuide::Evaluate( idLexer *src, idStr &definition ) {
	idToken token;
	idStr arguments[MAX_GUIDE_PARMS];
	int numArgs = 0;

	definition.Empty();
	src->ExpectTokenString( "(" );
	while ( numArgs < MAX_GUIDE_PARMS && src->ReadToken( &token ) ) {
		if ( token.Cmp( ")" ) == 0 ) {
			break;
		}
		if ( token.Cmp( "," ) == 0 ) {
			continue;
		}
		arguments[numArgs++] = token;
	}

	if ( numArgs != mNumParms ) {
		return false;
	}

	definition = mDefinition;

	for ( int i = 0; i < mNumParms && i < MAX_GUIDE_PARMS; i++ ) {
		const int parmLength = mParms[i].Length();
		if ( parmLength <= 0 ) {
			continue;
		}

		int searchOffset = 0;
		while ( searchOffset <= definition.Length() ) {
			const char *match = strstr( definition.c_str() + searchOffset, mParms[i].c_str() );
			if ( match == NULL ) {
				break;
			}

			const int matchOffset = (int)( match - definition.c_str() );
			idStr prefix = definition.Mid( 0, matchOffset );
			idStr suffix = definition.Mid( matchOffset + parmLength, definition.Length() - matchOffset - parmLength );
			definition = prefix + arguments[i] + suffix;
			searchOffset = matchOffset + arguments[i].Length();
		}
	}

	return true;
}

// jmarshall end

bool idDeclManagerLocal::EvaluateGuide( idStr &name, idLexer *src, idStr &definition ) {
	idToken guideName;
	definition.Empty();
	if ( !src->ReadToken( &guideName ) ) {
		src->Warning( "Missing guide name in '%s'", name.c_str() );
		return false;
	}

	rvDeclGuide *guide = DeclManager_FindGuide( guideName.c_str() );
	if ( !guide ) {
		src->Warning( "Guide name '%s' not found in '%s'", guideName.c_str(), name.c_str() );
		return false;
	}

	guide->Evaluate( src, definition );
	return true;
}

bool idDeclManagerLocal::EvaluateInlineGuide( idStr &name, idStr &definition ) {
	idStr store = definition;
	idLexer lexer;
	if ( !lexer.LoadMemory( store.c_str(), store.Length(), name.c_str() ) ) {
		return false;
	}
	lexer.SetFlags( DECL_LEXER_FLAGS );

	idToken token;
	int defOffset = 0;
	while ( !lexer.EndOfFile() ) {
		const int inlineStart = lexer.GetFileOffset();
		if ( !lexer.ReadToken( &token ) ) {
			break;
		}
		if ( token.Icmp( "inlineGuide" ) != 0 ) {
			continue;
		}

		idToken guideName;
		if ( !lexer.ReadToken( &guideName ) ) {
			return false;
		}

		rvDeclGuide *guide = DeclManager_FindGuide( guideName.c_str() );
		if ( !guide ) {
			return false;
		}

		idStr expanded;
		guide->Evaluate( &lexer, expanded );

		const int inlineLength = lexer.GetFileOffset() - inlineStart;
		const int replaceOffset = defOffset + inlineStart;
		idStr prefix = definition.Mid( 0, replaceOffset );
		idStr suffix = definition.Mid( replaceOffset + inlineLength, definition.Length() - replaceOffset - inlineLength );
		definition = prefix + expanded + suffix;
		defOffset += expanded.Length() - inlineLength;
	}

	return true;
}

/*
===================
idDeclManagerLocal::SetInsideLoad
===================
*/
void idDeclManagerLocal::SetInsideLoad( bool var ) {
	insideLevelLoad = var;
}

/*
===================
idDeclManagerLocal::GetInsideLoad
===================
*/
bool idDeclManagerLocal::GetInsideLoad( void ) {
	return insideLevelLoad;
}

/*
===================
idDeclManagerLocal::Reload
===================
*/
void idDeclManagerLocal::Reload( bool force ) {
	for ( int i = 0; i < loadedFiles.Num(); i++ ) {
		DeclManager_ShowReloadProgress( i, loadedFiles.Num(), loadedFiles[i]->fileName.c_str() );
		loadedFiles[i]->Reload( force );
	}
}

/*
===================
idDeclManagerLocal::BeginLevelLoad
===================
*/
void idDeclManagerLocal::BeginLevelLoad() {
	insideLevelLoad = true;

	// clear all the referencedThisLevel flags and purge all the data
	// so the next reference will cause a reparse
	for ( int i = 0; i < DECL_MAX_TYPES; i++ ) {
		int	num = linearLists[i].Num();
		for ( int j = 0 ; j < num ; j++ ) {
			idDeclLocal *decl = linearLists[i][j];
			decl->Purge();
		}
	}

	for ( int i = 0; i < linearLists[DECL_MATERIAL].Num(); i++ ) {
		idDeclLocal *decl = linearLists[DECL_MATERIAL][i];
		if ( decl->self != NULL ) {
			static_cast<idMaterial *>( decl->self )->ClearUseCount();
		}
	}
}

/*
===================
idDeclManagerLocal::EndLevelLoad
===================
*/
void idDeclManagerLocal::EndLevelLoad() {
	insideLevelLoad = false;

	for ( int i = 0; i < linearLists[DECL_MATERIAL].Num(); i++ ) {
		idDeclLocal *decl = linearLists[DECL_MATERIAL][i];
		if ( decl->self != NULL ) {
			static_cast<idMaterial *>( decl->self )->ResolveUse();
		}
	}
}

/*
===================
idDeclManagerLocal::RegisterDeclType
===================
*/
void idDeclManagerLocal::RegisterDeclType( const char *typeName, declType_t type, idDecl *(*allocator)( void ) ) {
	idDeclType *declType;

	if ( type < declTypes.Num() && declTypes[(int)type] ) {
		common->Warning( "idDeclManager::RegisterDeclType: type '%s' already exists", typeName );
		return;
	}

	declType = new idDeclType;
	declType->typeName = typeName;
	declType->type = type;
	declType->allocator = allocator;

	if ( (int)type + 1 > declTypes.Num() ) {
		declTypes.AssureSize( (int)type + 1, NULL );
	}
	declTypes[type] = declType;
}

/*
===================
idDeclManagerLocal::StartLoadingDecls
===================
*/
void idDeclManagerLocal::StartLoadingDecls() {
	if ( singleDeclFile != NULL ) {
		FinishLoadingDecls();
	}

	idStr singleDeclFileName;
	DeclManager_GetSingleDeclFileName( singleDeclFileName );
	singleDeclFile = fileSystem->OpenFileRead( singleDeclFileName.c_str() );
	if ( singleDeclFile == NULL ) {
		common->Warning( "Could not open packed decl file '%s'", singleDeclFileName.c_str() );
	}
}

/*
===================
idDeclManagerLocal::FinishLoadingDecls
===================
*/
void idDeclManagerLocal::FinishLoadingDecls() {
	if ( singleDeclFile != NULL ) {
		fileSystem->CloseFile( singleDeclFile );
		singleDeclFile = NULL;
	}
}

/*
===================
idDeclManagerLocal::LoadDeclsFromFile
===================
*/
void idDeclManagerLocal::LoadDeclsFromFile() {
	if ( singleDeclFile == NULL ) {
		return;
	}

	idStr singleDeclFileName;
	DeclManager_GetSingleDeclFileName( singleDeclFileName );

	idStr countString;
	if ( !ReadPackedDeclLine( singleDeclFile, countString ) ) {
		common->Warning( "Packed decl file '%s' ended before a section header", singleDeclFileName.c_str() );
		return;
	}

	const int fileCount = atoi( countString.c_str() );
	for ( int fileIndex = 0; fileIndex < fileCount; fileIndex++ ) {
		idStr packedFileName;
		idStr defaultTypeString;

		if ( !ReadPackedDeclLine( singleDeclFile, packedFileName ) || !ReadPackedDeclLine( singleDeclFile, defaultTypeString ) ) {
			common->Warning( "Packed decl file '%s' ended inside a section header", singleDeclFileName.c_str() );
			return;
		}

		declType_t defaultType = (declType_t)atoi( defaultTypeString.c_str() );
		idStr extension;
		idStr folder;
		packedFileName.ExtractFileExtension( extension );
		extension.Insert( ".", 0 );
		packedFileName.ExtractFilePath( folder );
		folder.StripTrailing( '/' );
		folder.StripTrailing( '\\' );

		FindOrCreateDeclFolder( folder.c_str(), extension.c_str(), defaultType );
		idDeclFile *declFile = FindOrCreateLoadedDeclFile( packedFileName.c_str(), defaultType );
		declFile->LoadAndParse( singleDeclFile );
	}
}

/*
===================
idDeclManagerLocal::WriteDeclFile
===================
*/
void idDeclManagerLocal::WriteDeclFile() {
	WriteDeclFileWithMode( DeclManager_GetConfiguredSingleDeclWriteMode() );
}

/*
===================
idDeclManagerLocal::WriteDeclFileWithMode
===================
*/
void idDeclManagerLocal::WriteDeclFileWithMode( declSingleFileWriteMode_t writeMode ) {
	idStr singleDeclFileName;
	DeclManager_GetSingleDeclFileName( singleDeclFileName );

	idFile *file = fileSystem->OpenFileWrite( singleDeclFileName.c_str(), "fs_savepath" );
	if ( file == NULL ) {
		common->Warning( "Could not open packed decl file '%s' for writing", singleDeclFileName.c_str() );
		return;
	}

	idList<declType_t> frameworkTypes;
	idList<declType_t> gameTypes;
	MakeDeclTypeList( frameworkTypes, declSingleFileFrameworkTypes, sizeof( declSingleFileFrameworkTypes ) / sizeof( declSingleFileFrameworkTypes[0] ) );
	DeclManager_MakeGameDeclTypeList( gameTypes, writeMode );

	WriteSingleDeclSection( file, frameworkTypes, false );
	WriteSingleDeclSection( file, gameTypes, false );

	fileSystem->CloseFile( file );
	common->Printf( "Wrote packed decl file '%s' using %s game-type coverage\n", singleDeclFileName.c_str(), DeclManager_GetSingleDeclWriteModeName( writeMode ) );
}

/*
===================
idDeclManagerLocal::FlushDecls
===================
*/
void idDeclManagerLocal::FlushDecls() {
	for ( int i = loadedFiles.Num() - 1; i >= 0; i-- ) {
		for ( idDeclLocal *decl = loadedFiles[i]->decls; decl; decl = decl->nextInFile ) {
			decl->Purge();
		}
	}
}

/*
===================
idDeclManagerLocal::RegisterDeclFolder
===================
*/
void idDeclManagerLocal::RegisterDeclFolder( const char *folder, const char *extension, declType_t defaultType ) {
	RegisterDeclFolderWrapper( folder, extension, defaultType, false, false );
}

/*
===================
idDeclManagerLocal::RegisterDeclFolder
===================
*/
void idDeclManagerLocal::RegisterDeclFolder( const char *folder, const char *extension, declType_t defaultType, bool unique, bool norecurse ) {
	idDeclFolder *declFolder = FindOrCreateDeclFolder( folder, extension, defaultType );
	if ( declFolder == NULL ) {
		return;
	}

	idFileList *fileList = fileSystem->ListFiles( declFolder->folder.c_str(), declFolder->extension.c_str(), true );
	if ( fileList != NULL ) {
		for ( int fileIndex = 0; fileIndex < fileList->GetNumFiles(); fileIndex++ ) {
			idStr fileName = declFolder->folder + "/" + fileList->GetFile( fileIndex );
			idDeclFile *declFile = FindOrCreateLoadedDeclFile( fileName.c_str(), defaultType );
			declFile->LoadAndParse( unique );
		}
		fileSystem->FreeFileList( fileList );
	}

	if ( norecurse ) {
		return;
	}

	idFileList *folderList = fileSystem->ListFiles( folder, "/", true );
	if ( folderList != NULL ) {
		for ( int folderIndex = 0; folderIndex < folderList->GetNumFiles(); folderIndex++ ) {
			const char *childFolder = folderList->GetFile( folderIndex );
			if ( childFolder[0] == '\0' || childFolder[0] == '.' ) {
				continue;
			}

			idStr childPath = idStr( folder ) + "/" + childFolder;
			RegisterDeclFolder( childPath.c_str(), extension, defaultType, unique, false );
		}
		fileSystem->FreeFileList( folderList );
	}
}

/*
===================
idDeclManagerLocal::RegisterDeclFolderWrapper
===================
*/
void idDeclManagerLocal::RegisterDeclFolderWrapper( const char *folder, const char *extension, declType_t defaultType, bool unique, bool norecurse ) {
	const int start = Sys_Milliseconds();

	RegisterDeclFolder( folder, extension, defaultType, unique, norecurse );

	common->Printf( "%dms to load %dk of %s\n",
		Sys_Milliseconds() - start,
		( GetTotalTextMemory( defaultType ) + 1023 ) / 1024,
		GetDeclTypeName( defaultType ) );
}

/*
===================
idDeclManagerLocal::FindLoadedDeclFile
===================
*/
idDeclFile *idDeclManagerLocal::FindLoadedDeclFile( const char *fileName ) {
	for ( int i = 0; i < loadedFiles.Num(); i++ ) {
		if ( loadedFiles[i]->fileName.Icmp( fileName ) == 0 ) {
			return loadedFiles[i];
		}
	}
	return NULL;
}

/*
===================
idDeclManagerLocal::FindOrCreateLoadedDeclFile
===================
*/
idDeclFile *idDeclManagerLocal::FindOrCreateLoadedDeclFile( const char *fileName, declType_t defaultType ) {
	idDeclFile *declFile = FindLoadedDeclFile( fileName );
	if ( declFile != NULL ) {
		return declFile;
	}

	declFile = new idDeclFile( fileName, defaultType );
	loadedFiles.Append( declFile );
	return declFile;
}

/*
===================
idDeclManagerLocal::FindOrCreateDeclFolder
===================
*/
idDeclFolder *idDeclManagerLocal::FindOrCreateDeclFolder( const char *folder, const char *extension, declType_t defaultType ) {
	for ( int i = 0; i < declFolders.Num(); i++ ) {
		if ( declFolders[i]->folder.Icmp( folder ) == 0 && declFolders[i]->extension.Icmp( extension ) == 0 ) {
			return declFolders[i];
		}
	}

	idDeclFolder *declFolder = new idDeclFolder;
	declFolder->folder = folder;
	declFolder->extension = extension;
	declFolder->defaultType = defaultType;
	declFolders.Append( declFolder );
	return declFolder;
}

/*
===================
idDeclManagerLocal::TypeListContains
===================
*/
bool idDeclManagerLocal::TypeListContains( const idList<declType_t> &types, declType_t type ) const {
	for ( int i = 0; i < types.Num(); i++ ) {
		if ( types[i] == type ) {
			return true;
		}
	}
	return false;
}

/*
===================
idDeclManagerLocal::GetTotalTextMemory
===================
*/
int idDeclManagerLocal::GetTotalTextMemory( declType_t type ) {
	const int typeIndex = (int)type;
	if ( typeIndex < 0 || typeIndex >= declTypes.Num() || declTypes[typeIndex] == NULL ) {
		common->FatalError( "idDeclManager::GetTotalTextMemory: bad type: %i", typeIndex );
	}

	int totalTextMemory = 0;
	const int numDecls = GetNumDecls( type );
	for ( int i = 0; i < numDecls; i++ ) {
		const idDecl *decl = DeclByIndex( type, i, false );
		totalTextMemory += decl->GetCompressedLength();
	}
	return totalTextMemory;
}

/*
===================
idDeclManagerLocal::NumWritableDecls
===================
*/
int idDeclManagerLocal::NumWritableDecls( idDeclFile *declFile, const idList<declType_t> &typesToWrite, bool writeStubs ) {
	int count = 0;
	for ( idDeclLocal *decl = declFile->decls; decl; decl = decl->nextInFile ) {
		if ( TypeListContains( typesToWrite, decl->type ) && ( decl->declState == DS_PARSED || writeStubs ) ) {
			count++;
		}
	}
	return count;
}

/*
===================
idDeclManagerLocal::BuildPackedDeclText
===================
*/
void idDeclManagerLocal::BuildPackedDeclText( idDeclLocal *decl, idStr &declText ) {
	declText.Empty();
	declText += declTypes[decl->type]->typeName;
	declText += " ";
	declText += decl->GetName();
	declText += "\n";

	if ( decl->declState == DS_PARSED && decl->textSource != NULL ) {
		const int textLength = decl->GetTextLength();
		char *text = (char *)_alloca( textLength + 1 );
		decl->GetText( text );
		declText.Append( text, textLength );
		return;
	}

	declText += "\n{ STUB: ";
	declText += va( "%d %d", decl->sourceTextOffset, decl->sourceTextLength );
	declText += " }";
}

/*
===================
idDeclManagerLocal::WriteDecls
===================
*/
void idDeclManagerLocal::WriteDecls( idFile *file, idDeclFile *declFile, const idList<declType_t> &typesToWrite, bool writeStubs ) {
	const int writableDecls = NumWritableDecls( declFile, typesToWrite, writeStubs );
	if ( writableDecls <= 0 ) {
		return;
	}

	file->Printf( "%s\n", declFile->fileName.c_str() );
	file->Printf( "%d\n", (int)declFile->defaultType );
	file->Printf( "%d\n", writableDecls );

	for ( idDeclLocal *decl = declFile->decls; decl; decl = decl->nextInFile ) {
		if ( !TypeListContains( typesToWrite, decl->type ) ) {
			continue;
		}
		if ( decl->declState != DS_PARSED && !writeStubs ) {
			continue;
		}

		idStr declText;
		BuildPackedDeclText( decl, declText );
		int linearIndex = linearLists[(int)decl->type].FindIndex( decl );
		if ( linearIndex < 0 ) {
			linearIndex = decl->index;
		}
		file->Printf( "%d\n", linearIndex );
		file->Printf( "%d\n", declText.Length() + 1 );
		file->Write( declText.c_str(), declText.Length() );
		file->Write( "\n", 1 );
	}
}

/*
===================
idDeclManagerLocal::WriteSingleDeclSection
===================
*/
void idDeclManagerLocal::WriteSingleDeclSection( idFile *file, const idList<declType_t> &typesToWrite, bool writeStubs ) {
	int writableFiles = 0;
	for ( int i = 0; i < loadedFiles.Num(); i++ ) {
		if ( NumWritableDecls( loadedFiles[i], typesToWrite, writeStubs ) > 0 ) {
			writableFiles++;
		}
	}

	file->Printf( "%d\n", writableFiles );
	for ( int i = 0; i < loadedFiles.Num(); i++ ) {
		WriteDecls( file, loadedFiles[i], typesToWrite, writeStubs );
	}
}

/*
===================
idDeclManagerLocal::CheckDecls
===================
*/
void idDeclManagerLocal::CheckDecls( void ) {
	for ( int i = 0; i < loadedFiles.Num(); i++ ) {
		for ( idDeclLocal *decl = loadedFiles[i]->decls; decl; decl = decl->nextInFile ) {
			if ( decl->declState != DS_PARSED ) {
				decl->ParseLocal();
			}
		}
	}
}

/*
===================
idDeclManagerLocal::DeleteLocalDecl
===================
*/
void idDeclManagerLocal::DeleteLocalDecl( idDeclLocal *decl ) {
	if ( decl == NULL ) {
		return;
	}
	if ( decl->self != NULL ) {
		decl->self->FreeData();
		delete decl->self;
	}
	if ( decl->textSource != NULL ) {
		Mem_Free( decl->textSource );
		decl->textSource = NULL;
	}
	delete decl;
}

/*
===================
idDeclManagerLocal::GetChecksum
===================
*/
int idDeclManagerLocal::GetChecksum( void ) const {
	int i, j, total, num;
	int *checksumData;

	// get the total number of decls
	total = 0;
	for ( i = 0; i < DECL_MAX_TYPES; i++ ) {
		total += linearLists[i].Num();
	}

	const int checksumCapacity = total > 0 ? total * 2 : 2;
	checksumData = (int *) _alloca16( checksumCapacity * sizeof( int ) );

	total = 0;
	for ( i = 0; i < DECL_MAX_TYPES; i++ ) {
		declType_t type = (declType_t) i;

		// FIXME: not particularly pretty but PDAs and associated decls are localized and should not be checksummed
		if ( type == DECL_PDA || type == DECL_VIDEO || type == DECL_AUDIO || type == DECL_EMAIL ) {
			continue;
		}
 
		num = linearLists[i].Num();
		for ( j = 0; j < num; j++ ) {
			idDeclLocal *decl = linearLists[i][j];

			if ( decl->sourceFile == &implicitDecls ) {
				continue;
			}

			checksumData[total*2+0] = total;
			checksumData[total*2+1] = decl->checksum;
			total++;
		}
	}

	LittleRevBytes( checksumData, sizeof(int), total * 2 );
	return MD5_BlockChecksum( checksumData, total * 2 * sizeof( int ) );
}

/*
===================
idDeclManagerLocal::GetNumDeclTypes
===================
*/
int idDeclManagerLocal::GetNumDeclTypes( void ) const {
	return declTypes.Num();
}

/*
===================
idDeclManagerLocal::GetDeclNameFromType
===================
*/
const char * idDeclManagerLocal::GetDeclNameFromType( declType_t type ) const {
	int typeIndex = (int)type;

	if ( typeIndex < 0 || typeIndex >= declTypes.Num() || declTypes[typeIndex] == NULL ) {
		common->FatalError( "idDeclManager::GetDeclNameFromType: bad type: %i", typeIndex );
	}
	return declTypes[typeIndex]->typeName;
}

/*
===================
idDeclManagerLocal::GetDeclTypeFromName
===================
*/
declType_t idDeclManagerLocal::GetDeclTypeFromName( const char *typeName ) const {
	int i;

	for ( i = 0; i < declTypes.Num(); i++ ) {
		if ( declTypes[i] && declTypes[i]->typeName.Icmp( typeName ) == 0 ) {
			return (declType_t)declTypes[i]->type;
		}
	}
	return DECL_MAX_TYPES;
}

/*
=================
idDeclManagerLocal::FindType

External users will always cause the decl to be parsed before returning
=================
*/
const idDecl *idDeclManagerLocal::FindType( declType_t type, const char *name, bool makeDefault, bool noCaching) {
	idDeclLocal *decl;

	if ( !name || !name[0] ) {
		name = "_emptyName";
		//common->Warning( "idDeclManager::FindType: empty %s name", GetDeclType( (int)type )->typeName.c_str() );
	}

	decl = FindTypeWithoutParsing( type, name, makeDefault );
	if ( !decl ) {
		return NULL;
	}

	decl->AllocateSelf();

	// if it hasn't been parsed yet, parse it now
	if ( decl->declState == DS_UNPARSED ) {
		decl->ParseLocal( noCaching );
		if ( noCaching ) {
			decl->self->List();
		}
	}

	// mark it as referenced
	decl->referencedThisLevel = true;
	decl->everReferenced = true;
	if ( insideLevelLoad ) {
		decl->parsedOutsideLevelLoad = false;
	}
	if ( type == DECL_MATERIAL && insideLevelLoad ) {
		idMaterial *material = static_cast<idMaterial *>( decl->self );
		material->AddLevelLoadReference();
	}

	return decl->self;
}

/*
===============
idDeclManagerLocal::FindDeclWithoutParsing
===============
*/
const idDecl* idDeclManagerLocal::FindDeclWithoutParsing( declType_t type, const char *name, bool makeDefault) {
	idDeclLocal* decl;
	decl = FindTypeWithoutParsing(type, name, makeDefault);
	if(decl) {
		return decl->self;
	}
	return NULL;
}

/*
===============
idDeclManagerLocal::ReloadFile
===============
*/
void idDeclManagerLocal::ReloadFile( const char* filename, bool force ) {
	for ( int i = 0; i < loadedFiles.Num(); i++ ) {
		if(!loadedFiles[i]->fileName.Icmp(filename)) {
			checksum ^= loadedFiles[i]->checksum;
			loadedFiles[i]->Reload( force );
			checksum ^= loadedFiles[i]->checksum;
		}
	}
}

/*
===================
idDeclManagerLocal::GetNumDecls
===================
*/
int idDeclManagerLocal::GetNumDecls( declType_t type ) {
	int typeIndex = (int)type;

	if ( typeIndex < 0 || typeIndex >= declTypes.Num() || declTypes[typeIndex] == NULL ) {
		common->FatalError( "idDeclManager::GetNumDecls: bad type: %i", typeIndex );
	}
	return linearLists[ typeIndex ].Num();
}

/*
===================
idDeclManagerLocal::DeclByIndex
===================
*/
const idDecl *idDeclManagerLocal::DeclByIndex( declType_t type, int index, bool forceParse ) {
	int typeIndex = (int)type;

	if ( typeIndex < 0 || typeIndex >= declTypes.Num() || declTypes[typeIndex] == NULL ) {
		common->FatalError( "idDeclManager::DeclByIndex: bad type: %i", typeIndex );
	}
	if ( index < 0 || index >= linearLists[ typeIndex ].Num() ) {
		common->Error( "idDeclManager::DeclByIndex: out of range for type %d (index desired: %d, max: %d)",
			typeIndex, index, linearLists[typeIndex].Num() );
	}
	idDeclLocal *decl = linearLists[ typeIndex ][ index ];

	decl->AllocateSelf();

	if ( forceParse && decl->declState == DS_UNPARSED ) {
		decl->ParseLocal();
	}

	return decl->self;
}

/*
===================
idDeclManagerLocal::ListType

list*
Lists decls currently referenced

list* ever
Lists decls that have been referenced at least once since app launched

list* all
Lists every decl declared, even if it hasn't been referenced or parsed

FIXME: alphabetized, wildcards?
===================
*/
void idDeclManagerLocal::ListType( const idCmdArgs &args, declType_t type ) {
	bool all, ever;

	if ( !idStr::Icmp( args.Argv( 1 ), "all" ) ) {
		all = true;
	} else {
		all = false;
	}
	if ( !idStr::Icmp( args.Argv( 1 ), "ever" ) ) {
		ever = true;
	} else {
		ever = false;
	}

	common->Printf( "--------------------\n" );
	int printed = 0;
	int	count = linearLists[ (int)type ].Num();
	for ( int i = 0 ; i < count ; i++ ) {
		idDeclLocal *decl = linearLists[ (int)type ][ i ];

		if ( !all && decl->declState == DS_UNPARSED ) {
			continue;
		}

		if ( !all && !ever && !decl->referencedThisLevel ) {
			continue;
		}

		if ( decl->referencedThisLevel ) {
			common->Printf( "*" );
		} else if ( decl->everReferenced ) {
			common->Printf( "." );
		} else {
			common->Printf( " " );
		}
		if ( decl->declState == DS_DEFAULTED ) {
			common->Printf( "D" );
		} else {
			common->Printf( " " );
		}
		common->Printf( "%4i: ", decl->index );
		printed++;
		if ( decl->declState == DS_UNPARSED ) {
			// doesn't have any type specific data yet
			common->Printf( "%s\n", decl->GetName() );
		} else {
			decl->self->List();
		}
	}

	common->Printf( "--------------------\n" );
	common->Printf( "%i of %i %s\n", printed, count, declTypes[type]->typeName.c_str() );
}

/*
===================
idDeclManagerLocal::PrintType
===================
*/
void idDeclManagerLocal::PrintType( const idCmdArgs &args, declType_t type ) {
	// individual decl types may use additional command parameters
	if ( args.Argc() < 2 ) {
		common->Printf( "USAGE: Print<decl type> <decl name> [type specific parms]\n" );
		return;
	}

	// look it up, skipping the public path so it won't parse or reference
	idDeclLocal *decl = FindTypeWithoutParsing( type, args.Argv( 1 ), false );
	if ( !decl ) {
		common->Printf( "%s '%s' not found.\n", declTypes[ type ]->typeName.c_str(), args.Argv( 1 ) );
		return;
	}

	// print information common to all decls
	common->Printf( "%s %s:\n", declTypes[ type ]->typeName.c_str(), decl->name.c_str() );
	common->Printf( "source: %s:%i\n", decl->sourceFile->fileName.c_str(), decl->sourceLine );
	common->Printf( "----------\n" );
	if ( decl->textSource != NULL ) {
		char *declText = (char *)_alloca( decl->textLength + 1 );
		decl->GetText( declText );
		common->Printf( "%s\n", declText );
	} else {
		common->Printf( "NO SOURCE\n" );
	}
	common->Printf( "----------\n" );
	switch( decl->declState ) {
		case DS_UNPARSED:
			common->Printf( "Unparsed.\n" );
			break;
		case DS_DEFAULTED:
			common->Printf( "<DEFAULTED>\n" );
			break;
		case DS_PARSED:
			common->Printf( "Parsed.\n" );
			break;
	}

	if ( decl->referencedThisLevel ) {
		common->Printf( "Currently referenced this level.\n" );
	} else if ( decl->everReferenced ) {
		common->Printf( "Referenced in a previous level.\n" );
	} else {
		common->Printf( "Never referenced.\n" );
	}

	// allow type-specific data to be printed
	if ( decl->self != NULL ) {
		decl->self->Print();
	}
}

/*
===================
idDeclManagerLocal::CreateNewDecl
===================
*/
idDecl *idDeclManagerLocal::CreateNewDecl( declType_t type, const char *name, const char *_fileName ) {
	int typeIndex = (int)type;
	int i, hash;

	if ( typeIndex < 0 || typeIndex >= declTypes.Num() || declTypes[typeIndex] == NULL ) {
		common->FatalError( "idDeclManager::CreateNewDecl: bad type: %i", typeIndex );
	}

	char  canonicalName[MAX_STRING_CHARS];

	MakeNameCanonical( name, canonicalName, sizeof( canonicalName ) );

	idStr fileName;
	if ( _fileName != NULL ) {
		fileName = _fileName;
	}
	fileName.BackSlashesToSlashes();

	// see if it already exists
	hash = hashTables[typeIndex].GenerateKey( canonicalName, false );
	for ( i = hashTables[typeIndex].First( hash ); i >= 0; i = hashTables[typeIndex].Next( i ) ) {
		if ( linearLists[typeIndex][i]->name.Icmp( canonicalName ) == 0 ) {
			linearLists[typeIndex][i]->AllocateSelf();
			return linearLists[typeIndex][i]->self;
		}
	}

	idDeclFile *sourceFile;

	// find existing source file or create a new one
	for ( i = 0; i < loadedFiles.Num(); i++ ) {
		if ( loadedFiles[i]->fileName.Icmp( fileName ) == 0 ) {
			break;
		}
	}
	if ( i < loadedFiles.Num() ) {
		sourceFile = loadedFiles[i];
	} else {
		sourceFile = new idDeclFile( fileName, type );
		loadedFiles.Append( sourceFile );
	}

	idDeclLocal *decl = new idDeclLocal;
	decl->name = canonicalName;
	decl->type = type;
	decl->declState = DS_UNPARSED;
	decl->AllocateSelf();
	idStr header = declTypes[typeIndex]->typeName;
	idStr defaultText;
	const char *defaultDefinition = decl->self->DefaultDefinition();
	if ( defaultDefinition != NULL ) {
		defaultText = defaultDefinition;
	}


	int size = header.Length() + 1 + idStr::Length( canonicalName ) + 1 + defaultText.Length();
	char *declText = ( char * ) _alloca( size + 1 );

	memcpy( declText, header, header.Length() );
	declText[header.Length()] = ' ';
	memcpy( declText + header.Length() + 1, canonicalName, idStr::Length( canonicalName ) );
	declText[header.Length() + 1 + idStr::Length( canonicalName )] = ' ';
	memcpy( declText + header.Length() + 1 + idStr::Length( canonicalName ) + 1, defaultText, defaultText.Length() + 1 );

	decl->SetTextLocal( declText, size );
	decl->sourceFile = sourceFile;
	decl->sourceTextOffset = sourceFile->fileSize;
	decl->sourceTextLength = 0;
	decl->sourceLine = sourceFile->numLines;

	decl->ParseLocal();

	// add this decl to the source file list
	decl->nextInFile = sourceFile->decls;
	sourceFile->decls = decl;

	// add it to the hash table and linear list
	decl->index = linearLists[typeIndex].Num();
	hashTables[typeIndex].Add( hash, linearLists[typeIndex].Append( decl ) );

	return decl->self;
}

/*
===============
idDeclManagerLocal::RenameDecl
===============
*/
bool idDeclManagerLocal::RenameDecl( declType_t type, const char* oldName, const char* newName ) {

	char canonicalOldName[MAX_STRING_CHARS];
	MakeNameCanonical( oldName, canonicalOldName, sizeof( canonicalOldName ));

	char canonicalNewName[MAX_STRING_CHARS];
	MakeNameCanonical( newName, canonicalNewName, sizeof( canonicalNewName ) );

	idDeclLocal	*decl = NULL;

	// make sure it already exists
	int typeIndex = (int)type;
	int i, hash;
	hash = hashTables[typeIndex].GenerateKey( canonicalOldName, false );
	for ( i = hashTables[typeIndex].First( hash ); i >= 0; i = hashTables[typeIndex].Next( i ) ) {
		if ( linearLists[typeIndex][i]->name.Icmp( canonicalOldName ) == 0 ) {
			decl = linearLists[typeIndex][i];
			break;
		}
	}
	if(!decl)
		return false;

	//if ( !hashTables[(int)type].Get( canonicalOldName, &declPtr ) )
	//	return false;

	//decl = *declPtr;

	//Change the name
	decl->name = canonicalNewName;


	// add it to the hash table
	//hashTables[(int)decl->type].Set( decl->name, decl );
	int newhash = hashTables[typeIndex].GenerateKey( canonicalNewName, false );
	hashTables[typeIndex].Add( newhash, decl->index );

	//Remove the old hash item
	hashTables[typeIndex].Remove(hash, decl->index);

	return true;
}

/*
===================
idDeclManagerLocal::MediaPrint

This is just used to nicely indent media caching prints
===================
*/
void idDeclManagerLocal::MediaPrint( const char *fmt, ... ) {
	if ( !decl_show.GetInteger() ) {
		return;
	}
	for ( int i = 0 ; i < indent ; i++ ) {
		common->Printf( "    " );
	}
	va_list		argptr;
	char		buffer[1024];
	va_start (argptr,fmt);
	idStr::vsnPrintf( buffer, sizeof(buffer), fmt, argptr );
	va_end (argptr);
	buffer[sizeof(buffer)-1] = '\0';

	common->Printf( "%s", buffer );
}

/*
===================
idDeclManagerLocal::WritePrecacheCommands
===================
*/
void idDeclManagerLocal::WritePrecacheCommands( idFile *f ) {
	for ( int i = 0; i < declTypes.Num(); i++ ) {
		int num;

		if ( declTypes[i] == NULL ) {
			continue;
		}

		num = linearLists[i].Num();

		for ( int j = 0 ; j < num ; j++ ) {
			idDeclLocal *decl = linearLists[i][j];

			if ( !decl->referencedThisLevel ) {
				continue;
			}

			char	str[1024];
			sprintf( str, "touch %s %s\n", declTypes[i]->typeName.c_str(), decl->GetName() );
			common->Printf( "%s", str );
			f->Printf( "%s", str );
		}
	}
}

/********************************************************************/

const idMaterial *idDeclManagerLocal::FindMaterial( const char *name, bool makeDefault ) {
	return static_cast<const idMaterial *>( FindType( DECL_MATERIAL, name, makeDefault ) );
}

const idMaterial *idDeclManagerLocal::MaterialByIndex( int index, bool forceParse ) {
	const idMaterial *material = static_cast<const idMaterial *>( DeclByIndex( DECL_MATERIAL, index, forceParse ) );
	if ( material != NULL && insideLevelLoad ) {
		const_cast<idMaterial *>( material )->AddLevelLoadReference();
	}
	return material;
}

/********************************************************************/

const idDeclSkin *idDeclManagerLocal::FindSkin( const char *name, bool makeDefault ) {
	return static_cast<const idDeclSkin *>( FindType( DECL_SKIN, name, makeDefault ) );
}

const idDeclSkin *idDeclManagerLocal::SkinByIndex( int index, bool forceParse ) {
	return static_cast<const idDeclSkin *>( DeclByIndex( DECL_SKIN, index, forceParse ) );
}

/********************************************************************/

const idSoundShader *idDeclManagerLocal::FindSound( const char *name, bool makeDefault ) {
	return static_cast<const idSoundShader *>( FindType( DECL_SOUND, name, makeDefault ) );
}

const idSoundShader *idDeclManagerLocal::SoundByIndex( int index, bool forceParse ) {
	return static_cast<const idSoundShader *>( DeclByIndex( DECL_SOUND, index, forceParse ) );
}

// RAVEN BEGIN
// jscott: for new Raven decls
static const float DECL_MSEC_TO_SEC = 0.001f;

const rvDeclMatType* idDeclManagerLocal::FindMaterialType(const char* name, bool makeDefault) {
	return static_cast<const rvDeclMatType*>(FindType(DECL_MATERIALTYPE, name, makeDefault));
}

const rvDeclLipSync* idDeclManagerLocal::FindLipSync(const char* name, bool makeDefault) {
	return static_cast<const rvDeclLipSync*>(FindType(DECL_LIPSYNC, name, makeDefault));
}

const rvDeclPlayback* idDeclManagerLocal::FindPlayback(const char* name, bool makeDefault) {
	return static_cast<const rvDeclPlayback*>(FindType(DECL_PLAYBACK, name, makeDefault));
}
const rvDeclEffect* idDeclManagerLocal::FindEffect(const char* name, bool makeDefault) {
	return (const rvDeclEffect*)FindType(DECL_EFFECT, name, makeDefault);
}

const idDeclTable* idDeclManagerLocal::FindTable(const char* name, bool makeDefault) {
	return static_cast<const idDeclTable*>(FindType(DECL_TABLE, name, makeDefault));
}

const rvDeclMatType* idDeclManagerLocal::MaterialTypeByIndex(int index, bool forceParse) {
	return static_cast<const rvDeclMatType*>(DeclByIndex(DECL_MATERIALTYPE, index, forceParse));
}

const rvDeclLipSync* idDeclManagerLocal::LipSyncByIndex(int index, bool forceParse) {
	return static_cast<const rvDeclLipSync*>(DeclByIndex(DECL_LIPSYNC, index, forceParse));
}

const rvDeclPlayback* idDeclManagerLocal::PlaybackByIndex(int index, bool forceParse) {
	return static_cast<const rvDeclPlayback*>(DeclByIndex(DECL_PLAYBACK, index, forceParse));
}

void idDeclManagerLocal::StartPlaybackRecord(rvDeclPlayback* playback) {
	if (playback == NULL) {
		return;
	}
	playback->Start();
}

bool idDeclManagerLocal::SetPlaybackData(rvDeclPlayback* playback, int now, int control, rvDeclPlaybackData* pbd) {
	if (playback == NULL) {
		return false;
	}
	return playback->SetCurrentData(now * DECL_MSEC_TO_SEC, control, pbd);
}

bool idDeclManagerLocal::GetPlaybackData(const rvDeclPlayback* playback, int control, int now, int last, rvDeclPlaybackData* pbd) {
	if (playback == NULL) {
		return true;
	}
	return playback->GetCurrentData(control, now * DECL_MSEC_TO_SEC, last * DECL_MSEC_TO_SEC, pbd);
}

bool idDeclManagerLocal::FinishPlayback(rvDeclPlayback* playback) {
	if (playback == NULL) {
		return false;
	}
	return playback->Finish(-1.0f);
}

/*
===================
idDeclManagerLocal::GetNewName
===================
*/
idStr idDeclManagerLocal::GetNewName( declType_t type, const char *base ) {
	idStr name;
	for ( int suffix = 1; suffix < 1024; suffix++ ) {
		name = va( "%s%d", base, suffix );
		if ( FindTypeWithoutParsing( type, name.c_str(), false ) == NULL ) {
			return name;
		}
	}
	return "*unknown*";
}

/*
===================
idDeclManagerLocal::GetDeclTypeName
===================
*/
const char *idDeclManagerLocal::GetDeclTypeName( declType_t type ) {
	return GetDeclNameFromType( type );
}

/*
===================
idDeclManagerLocal::ListDeclSummary
===================
*/
size_t idDeclManagerLocal::ListDeclSummary( const idCmdArgs &args ) {
	(void)args;
	int totalDecls = 0;
	size_t totalStructBytes = 0;

	for ( int typeIndex = 0; typeIndex < declTypes.Num(); typeIndex++ ) {
		if ( declTypes[typeIndex] == NULL ) {
			continue;
		}

		totalDecls += linearLists[typeIndex].Num();
		for ( int declIndex = 0; declIndex < linearLists[typeIndex].Num(); declIndex++ ) {
			idDeclLocal *decl = linearLists[typeIndex][declIndex];
			totalStructBytes += decl->Size();
			if ( decl->self != NULL ) {
				totalStructBytes += decl->self->Size();
			}
		}
	}

	common->Printf( "Decls           - %dK in %d decl structs\n", (int)( totalStructBytes >> 10 ), totalDecls );
	return totalStructBytes >> 10;
}

/*
===================
idDeclManagerLocal::RemoveDeclFile
===================
*/
void idDeclManagerLocal::RemoveDeclFile( const char *file ) {
	for ( int i = 0; i < loadedFiles.Num(); i++ ) {
		if ( loadedFiles[i]->fileName.Icmp( file ) != 0 ) {
			continue;
		}
		loadedFiles.RemoveIndex( i );
		return;
	}
}

/*
===================
idDeclManagerLocal::Validate
===================
*/
bool idDeclManagerLocal::Validate( declType_t type, int iIndex, idStr &strReportTo ) {
	const idDecl *decl = DeclByIndex( type, iIndex, false );
	if ( decl == NULL ) {
		strReportTo += va( "Invalid decl index %d for type %s\n", iIndex, GetDeclNameFromType( type ) );
		return false;
	}

	const int textLength = decl->GetTextLength();
	char *declText = (char *)_alloca( textLength + 1 );
	decl->GetText( declText );
	declText[textLength] = '\0';

	struct declValidationToolState_t {
		declValidationToolState_t() : oldEditors( com_editors ) {
			com_editors |= EDITOR_DECL | EDITOR_DECL_VALIDATING;
		}
		~declValidationToolState_t() {
			com_editors = oldEditors;
		}
		int oldEditors;
	} validationToolState;

	return decl->Validate( declText, textLength, strReportTo );
}

/*
===================
idDeclManagerLocal::AllocateDecl
===================
*/
idDecl *idDeclManagerLocal::AllocateDecl( declType_t type ) {
	const int typeIndex = (int)type;
	if ( typeIndex < 0 || typeIndex >= declTypes.Num() || declTypes[typeIndex] == NULL ) {
		common->FatalError( "idDeclManager::AllocateDecl: bad type: %i", typeIndex );
	}

	idDeclLocal *declBase = new idDeclLocal;
	declBase->self = NULL;
	declBase->type = type;
	declBase->sourceFile = &implicitDecls;
	declBase->parsedOutsideLevelLoad = !insideLevelLoad;

	idDecl *decl = declTypes[typeIndex]->allocator();
	decl->base = declBase;
	declBase->self = decl;
	return decl;
}

const rvDeclEffect* idDeclManagerLocal::EffectByIndex(int index, bool forceParse) {
	return (const rvDeclEffect*)DeclByIndex(DECL_EFFECT, index, forceParse);
}
const idDeclTable* idDeclManagerLocal::TableByIndex(int index, bool forceParse) {
	return static_cast<const idDeclTable*>(DeclByIndex(DECL_TABLE, index, forceParse));
}
// RAVEN END

/*
===================
idDeclManagerLocal::MakeNameCanonical
===================
*/
void idDeclManagerLocal::MakeNameCanonical( const char *name, char *result, int maxLength ) {
	int i, lastDot;

	lastDot = -1;
	for ( i = 0; i < maxLength && name[i] != '\0'; i++ ) {
		int c = name[i];
		if ( c == '\\' ) {
			result[i] = '/';
		} else if ( c == '.' ) {
			lastDot = i;
			result[i] = c;
		} else {
			result[i] = idStr::ToLower( c );
		}
	}
	if ( lastDot != -1 ) {
		result[lastDot] = '\0';
	} else {
		result[i] = '\0';
	}
}

/*
================
idDeclManagerLocal::ListAllDecls_f
================
*/
void idDeclManagerLocal::ListAllDecls_f( const idCmdArgs &args ) {
	(void)args;
	for ( int typeIndex = 0; typeIndex < declManagerLocal.declTypes.Num(); typeIndex++ ) {
		idDeclType *declType = declManagerLocal.declTypes[typeIndex];
		if ( declType == NULL ) {
			continue;
		}

		common->Printf( "Starting %s\n----\n", declType->typeName.c_str() );
		for ( int declIndex = 0; declIndex < declManagerLocal.linearLists[typeIndex].Num(); declIndex++ ) {
			common->Printf( "%s\n", declManagerLocal.linearLists[typeIndex][declIndex]->GetName() );
		}
		common->Printf( "----\n" );
	}
}

/*
================
idDeclManagerLocal::ListDecls_f
================
*/
void idDeclManagerLocal::ListDecls_f( const idCmdArgs &args ) {
	int		i, j;
	int		totalDecls = 0;
	int		totalText = 0;
	int		totalStructs = 0;

	for ( i = 0; i < declManagerLocal.declTypes.Num(); i++ ) {
		int size, num;

		if ( declManagerLocal.declTypes[i] == NULL ) {
			continue;
		}

		num = declManagerLocal.linearLists[i].Num();
		totalDecls += num;

		size = 0;
		for ( j = 0; j < num; j++ ) {
			size += declManagerLocal.linearLists[i][j]->Size();
			if ( declManagerLocal.linearLists[i][j]->self != NULL ) {
				size += declManagerLocal.linearLists[i][j]->self->Size();
			}
		}
		totalStructs += size;

		common->Printf( "%4ik %4i %s\n", size >> 10, num, declManagerLocal.declTypes[i]->typeName.c_str() );
	}

	for ( i = 0 ; i < declManagerLocal.loadedFiles.Num() ; i++ ) {
		idDeclFile	*df = declManagerLocal.loadedFiles[i];
		totalText += df->fileSize;
	}

	common->Printf( "%i total decls is %i decl files\n", totalDecls, declManagerLocal.loadedFiles.Num() );
	common->Printf( "%iKB in text, %iKB in structures\n", totalText >> 10, totalStructs >> 10 );
}

/*
===================
idDeclManagerLocal::ReloadDecls_f

Reload will not find any new files created in the directories, it
will only reload existing files.

A reload will never cause anything to be purged.
===================
*/
void idDeclManagerLocal::ReloadDecls_f( const idCmdArgs &args ) {
	bool	force;

	fileSystem->SetIsFileLoadingAllowed( true );
	declManagerLocal.ParseGuides();

	if ( !idStr::Icmp( args.Argv( 1 ), "all" ) ) {
		force = true;
		common->Printf( "reloading all decl files:\n" );
	} else {
		force = false;
		common->Printf( "reloading changed decl files:\n" );
	}

	soundSystem->SetMute( true );

	declManagerLocal.Reload( force );

	soundSystem->SetMute( false );
	fileSystem->SetIsFileLoadingAllowed( false );
}

/*
===================
idDeclManagerLocal::ReloadFile_f
===================
*/
void idDeclManagerLocal::ReloadFile_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 || args.Argv( 1 )[0] == '\0' ) {
		common->Printf( "usage: reloadFile <fileName>\n" );
		return;
	}

	fileSystem->SetIsFileLoadingAllowed( true );
	declManagerLocal.ParseGuides();

	soundSystem->SetMute( true );
	declManagerLocal.ReloadFile( args.Argv( 1 ), true );
	soundSystem->SetMute( false );
	fileSystem->SetIsFileLoadingAllowed( false );
}

/*
===================
idDeclManagerLocal::ResaveDecl_f
===================
*/
static void DeclManager_PrintValidTypes() {
	common->Printf( "valid types: " );
	for ( int i = 0; i < declManagerLocal.GetNumDeclTypes(); i++ ) {
		const idDeclType *declType = declManagerLocal.GetDeclType( i );
		if ( declType ) {
			common->Printf( "%s ", declType->typeName.c_str() );
		}
	}
	common->Printf( "\n" );
}

void idDeclManagerLocal::ResaveDecl_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 ) {
		common->Printf( "usage: resaveDecl <type> [name]\n" );
		DeclManager_PrintValidTypes();
		return;
	}

	const declType_t type = declManagerLocal.GetDeclTypeFromName( args.Argv( 1 ) );
	if ( type == DECL_MAX_TYPES ) {
		common->Printf( "unknown decl type '%s'\n", args.Argv( 1 ) );
		return;
	}

	if ( args.Argc() <= 2 ) {
		for ( int declIndex = 0; declIndex < declManagerLocal.GetNumDecls( type ); declIndex++ ) {
			const idDecl *decl = declManagerLocal.DeclByIndex( type, declIndex );
			common->Printf( "...resaving: %s\n", decl->GetName() );
			const_cast<idDecl *>( decl )->RebuildTextSource();
			const_cast<idDecl *>( decl )->ReplaceSourceFileText();
		}
		return;
	}

	const idDecl *decl = declManagerLocal.FindType( type, args.Argv( 2 ), true );
	if ( decl == NULL ) {
		common->Printf( "%s '%s' not found\n", declManagerLocal.GetDeclNameFromType( type ), args.Argv( 2 ) );
		return;
	}

	common->Printf( "...resaving: %s\n", decl->GetName() );
	const_cast<idDecl *>( decl )->RebuildTextSource();
	const_cast<idDecl *>( decl )->ReplaceSourceFileText();
}

/*
===================
idDeclManagerLocal::WriteDeclFile_f
===================
*/
void idDeclManagerLocal::WriteDeclFile_f( const idCmdArgs &args ) {
	if ( args.Argc() > 2 ) {
		common->Printf( "usage: writeDeclFile [openq4|retail]\n" );
		return;
	}

	if ( args.Argc() == 2 ) {
		declSingleFileWriteMode_t writeMode;
		if ( !DeclManager_ParseSingleDeclWriteMode( args.Argv( 1 ), writeMode ) ) {
			common->Printf( "Unknown packed decl write mode '%s'. Use 'openq4' or 'retail'.\n", args.Argv( 1 ) );
			return;
		}

		declManagerLocal.WriteDeclFileWithMode( writeMode );
		return;
	}

	declManagerLocal.WriteDeclFile();
}

/*
===================
idDeclManagerLocal::FlushDecls_f
===================
*/
void idDeclManagerLocal::FlushDecls_f( const idCmdArgs &args ) {
	(void)args;
	declManagerLocal.FlushDecls();
}

/*
===================
idDeclManagerLocal::CheckDecls_f
===================
*/
void idDeclManagerLocal::CheckDecls_f( const idCmdArgs &args ) {
	(void)args;
	declManagerLocal.CheckDecls();
}

/*
===================
idDeclManagerLocal::TouchDecl_f
===================
*/
void idDeclManagerLocal::TouchDecl_f( const idCmdArgs &args ) {
	if ( args.Argc() != 3 ) {
		common->Printf( "usage: touch <type> <name>\n" );
		DeclManager_PrintValidTypes();
		return;
	}

	const declType_t type = declManagerLocal.GetDeclTypeFromName( args.Argv( 1 ) );
	if ( type == DECL_MAX_TYPES ) {
		common->Printf( "unknown decl type '%s'\n", args.Argv( 1 ) );
		return;
	}

	const idDecl *decl = declManagerLocal.FindType( type, args.Argv( 2 ), true );
	if ( !decl ) {
		common->Printf( "%s '%s' not found\n", declManagerLocal.GetDeclNameFromType( type ), args.Argv( 2 ) );
	}
}

/*
===================
idDeclManagerLocal::FindTypeWithoutParsing

This finds or creats the decl, but does not cause a parse.  This is only used internally.
===================
*/
idDeclLocal *idDeclManagerLocal::FindTypeWithoutParsing( declType_t type, const char *name, bool makeDefault, int indexToStoreAt ) {
	int typeIndex = (int)type;
	int i, hash;

	if ( typeIndex < 0 || typeIndex >= declTypes.Num() || declTypes[typeIndex] == NULL ) {
		common->FatalError( "idDeclManager::FindTypeWithoutParsing: bad type: %i", typeIndex );
	}

	char canonicalName[MAX_STRING_CHARS];

	MakeNameCanonical( name, canonicalName, sizeof( canonicalName ) );

	// see if it already exists
	hash = hashTables[typeIndex].GenerateKey( canonicalName, false );
	for ( i = hashTables[typeIndex].First( hash ); i >= 0; i = hashTables[typeIndex].Next( i ) ) {
		if ( linearLists[typeIndex][i]->name.Icmp( canonicalName ) == 0 ) {
			// only print these when decl_show is set to 2, because it can be a lot of clutter
			if ( decl_show.GetInteger() > 1 ) {
				MediaPrint( "referencing %s %s\n", declTypes[ type ]->typeName.c_str(), name );
			}
			return linearLists[typeIndex][i];
		}
	}

	if ( !makeDefault ) {
		return NULL;
	}

	idDeclLocal *decl = new idDeclLocal;
	decl->self = NULL;
	decl->name = canonicalName;
	decl->type = type;
	decl->declState = DS_UNPARSED;
	decl->textSource = NULL;
	decl->textLength = 0;
	decl->sourceFile = &implicitDecls;
	decl->referencedThisLevel = false;
	decl->everReferenced = false;
	decl->parsedOutsideLevelLoad = !insideLevelLoad;

	if ( indexToStoreAt >= 0 ) {
		while ( linearLists[typeIndex].Num() < indexToStoreAt ) {
			idDeclLocal *placeholder = new idDeclLocal;
			placeholder->name = "";
			placeholder->type = type;
			placeholder->sourceFile = &implicitDecls;
			placeholder->parsedOutsideLevelLoad = !insideLevelLoad;
			placeholder->index = linearLists[typeIndex].Num();
			linearLists[typeIndex].Append( placeholder );
		}

		decl->index = indexToStoreAt;
		if ( indexToStoreAt < linearLists[typeIndex].Num() ) {
			idDeclLocal *oldDecl = linearLists[typeIndex][indexToStoreAt];
			if ( oldDecl != NULL && oldDecl->name.Length() > 0 ) {
				int oldHash = hashTables[typeIndex].GenerateKey( oldDecl->name.c_str(), false );
				hashTables[typeIndex].Remove( oldHash, indexToStoreAt );
			}
			DeleteLocalDecl( oldDecl );
			linearLists[typeIndex][indexToStoreAt] = decl;
		} else {
			linearLists[typeIndex].Append( decl );
		}
		hashTables[typeIndex].Add( hash, indexToStoreAt );
		return decl;
	}

	// add it to the linear list and hash table
	decl->index = linearLists[typeIndex].Num();
	hashTables[typeIndex].Add( hash, linearLists[typeIndex].Append( decl ) );

	return decl;
}


/*
====================================================================================

	idDeclLocal

====================================================================================
*/

/*
=================
idDeclLocal::idDeclLocal
=================
*/
idDeclLocal::idDeclLocal( void ) {
	self = NULL;
	insideLevelLoad = false;
	name = "unnamed";
	textSource = NULL;
	textLength = 0;
	compressedLength = 0;
	sourceFile = NULL;
	sourceTextOffset = 0;
	sourceTextLength = 0;
	sourceLine = 0;
	checksum = 0;
	type = DECL_ENTITYDEF;
	index = 0;
	declState = DS_UNPARSED;
	parsedOutsideLevelLoad = false;
	referencedThisLevel = false;
	everReferenced = false;
	redefinedInReload = false;
	needsPrecache = false;
	nextInFile = NULL;
}

/*
=================
idDeclLocal::GetName
=================
*/
const char *idDeclLocal::GetName( void ) const {
	return name.c_str();
}

/*
=================
idDeclLocal::GetType
=================
*/
declType_t idDeclLocal::GetType( void ) const {
	return type;
}

/*
=================
idDeclLocal::GetState
=================
*/
declState_t idDeclLocal::GetState( void ) const {
	return declState;
}

/*
=================
idDeclLocal::IsImplicit
=================
*/
bool idDeclLocal::IsImplicit( void ) const {
	return ( sourceFile == declManagerLocal.GetImplicitDeclFile() );
}

/*
=================
idDeclLocal::IsValid
=================
*/
bool idDeclLocal::IsValid( void ) const {
	return ( declState != DS_UNPARSED );
}

/*
=================
idDeclLocal::Invalidate
=================
*/
void idDeclLocal::Invalidate( void ) {
	declState = DS_UNPARSED;
}

/*
=================
idDeclLocal::EnsureNotPurged
=================
*/
void idDeclLocal::EnsureNotPurged( void ) {
	if ( declState == DS_UNPARSED ) {
		ParseLocal();
	}
}

/*
=================
idDeclLocal::Index
=================
*/
int idDeclLocal::Index( void ) const {
	return index;
}

/*
=================
idDeclLocal::GetLineNum
=================
*/
int idDeclLocal::GetLineNum( void ) const {
	return sourceLine;
}

/*
=================
idDeclLocal::GetFileName
=================
*/
const char *idDeclLocal::GetFileName( void ) const {
	if ( sourceFile == NULL ) {
		return "";
	}
	return sourceFile->fileName.c_str();
}

/*
=================
idDeclLocal::Size
=================
*/
size_t idDeclLocal::Size( void ) const {
	return sizeof( idDecl ) + name.Allocated();
}

/*
=================
idDeclLocal::GetText
=================
*/
void idDeclLocal::GetText( char *text ) const {
#ifdef USE_COMPRESSED_DECLS
	HuffmanDecompressText( text, textLength, (byte *)textSource, compressedLength );
#else
	memcpy( text, textSource, textLength+1 );
#endif
}

/*
=================
idDeclLocal::GetTextLength
=================
*/
int idDeclLocal::GetTextLength( void ) const {
	return textLength;
}

/*
=================
idDeclLocal::GetCompressedLength
=================
*/
int idDeclLocal::GetCompressedLength( void ) const {
	return compressedLength;
}

/*
=================
idDeclLocal::SetText
=================
*/
void idDeclLocal::SetText( const char *text ) {
	SetTextLocal( text, idStr::Length( text ) );
}

/*
=================
idDeclLocal::SetTextLocal
=================
*/
void idDeclLocal::SetTextLocal( const char *text, const int length ) {

	Mem_Free( textSource );

	checksum = MD5_BlockChecksum( text, length );

#ifdef GET_HUFFMAN_FREQUENCIES
	for( int i = 0; i < length; i++ ) {
		huffmanFrequencies[((const unsigned char *)text)[i]]++;
	}
#endif

#ifdef USE_COMPRESSED_DECLS
	int maxBytesPerCode = ( maxHuffmanBits + 7 ) >> 3;
	byte *compressed = (byte *)_alloca( length * maxBytesPerCode );
	compressedLength = HuffmanCompressText( text, length, compressed, length * maxBytesPerCode );
	textSource = (char *)Mem_Alloc( compressedLength );
	memcpy( textSource, compressed, compressedLength );
#else
	compressedLength = length;
	textSource = (char *) Mem_Alloc( length + 1 );
	memcpy( textSource, text, length );
	textSource[length] = '\0';
#endif
	textLength = length;
}

/*
=================
idDeclLocal::ReplaceSourceFileText
=================
*/
bool idDeclLocal::ReplaceSourceFileText( void ) {
	int oldFileLength, newFileLength;
	char *buffer;
	idFile *file;

	common->Printf( "Writing \'%s\' to \'%s\'...\n", GetName(), GetFileName() );

	if ( sourceFile == &declManagerLocal.implicitDecls ) {
		common->Warning( "Can't save implicit declaration %s.", GetName() );
		return false;
	}

	// get length and allocate buffer to hold the file
	oldFileLength = sourceFile->fileSize;
	newFileLength = oldFileLength - sourceTextLength + textLength;
	buffer = (char *) Mem_Alloc( Max( newFileLength, oldFileLength ) );

	// read original file
	if ( sourceFile->fileSize ) {

		file = fileSystem->OpenFileRead( GetFileName() );
		if ( !file ) {
			Mem_Free( buffer );
			common->Warning( "Couldn't open %s for reading.", GetFileName() );
			return false;
		}

		if ( file->Length() != sourceFile->fileSize || file->Timestamp() != sourceFile->timestamp ) {
			fileSystem->CloseFile( file );
			Mem_Free( buffer );
			common->Warning( "The file %s has been modified outside of the engine.", GetFileName() );
			return false;
		}

		file->Read( buffer, oldFileLength );
		fileSystem->CloseFile( file );

		if ( MD5_BlockChecksum( buffer, oldFileLength ) != sourceFile->checksum ) {
			Mem_Free( buffer );
			common->Warning( "The file %s has been modified outside of the engine.", GetFileName() );
			return false;
		}
	}

	// insert new text
	char *declText = (char *) _alloca( textLength + 1 );
	GetText( declText );
	memmove( buffer + sourceTextOffset + textLength, buffer + sourceTextOffset + sourceTextLength, oldFileLength - sourceTextOffset - sourceTextLength );
	memcpy( buffer + sourceTextOffset, declText, textLength );

	// write out new file
	file = fileSystem->OpenFileWrite( GetFileName(), "fs_devpath" );
	if ( !file ) {
		Mem_Free( buffer );
		common->Warning( "Couldn't open %s for writing.", GetFileName() );
		return false;
	}
	file->Write( buffer, newFileLength );
	fileSystem->CloseFile( file );

	// set new file size, checksum and timestamp
	sourceFile->fileSize = newFileLength;
	sourceFile->checksum = MD5_BlockChecksum( buffer, newFileLength );
	fileSystem->ReadFile( GetFileName(), NULL, &sourceFile->timestamp );

	// free buffer
	Mem_Free( buffer );

	// move all decls in the same file
	for ( idDeclLocal *decl = sourceFile->decls; decl; decl = decl->nextInFile ) {
		if (decl->sourceTextOffset > sourceTextOffset) {
			decl->sourceTextOffset += textLength - sourceTextLength;
		}
	}

	// set new size of text in source file
	sourceTextLength = textLength;

	return true;
}

/*
=================
idDeclLocal::SourceFileChanged
=================
*/
bool idDeclLocal::SourceFileChanged( void ) const {
	int newLength;
	ID_TIME_T newTimestamp;

	if ( com_SingleDeclFile.GetBool() ) {
		return false;
	}

	if ( sourceFile->fileSize <= 0 ) {
		return false;
	}

	newLength = fileSystem->ReadFile( GetFileName(), NULL, &newTimestamp );

	if ( newLength != sourceFile->fileSize || newTimestamp != sourceFile->timestamp ) {
		return true;
	}

	return false;
}

/*
=================
idDeclLocal::MakeDefault
=================
*/
void idDeclLocal::MakeDefault() {
	static int recursionLevel;
	const char *defaultText;

	declManagerLocal.MediaPrint( "DEFAULTED\n" );
	declState = DS_DEFAULTED;

	AllocateSelf();

	defaultText = self->DefaultDefinition();

	// a parse error inside a DefaultDefinition() string could
	// cause an infinite loop, but normal default definitions could
	// still reference other default definitions, so we can't
	// just dump out on the first recursion
	if ( ++recursionLevel > 100 ) {
		common->FatalError( "idDecl::MakeDefault: bad DefaultDefinition(): %s", defaultText );
	}

	// always free data before parsing
	self->FreeData();

	// parse
	self->Parse( defaultText, strlen( defaultText ), false );

	// we could still eventually hit the recursion if we have enough Error() calls inside Parse...
	--recursionLevel;
}

/*
=================
idDeclLocal::SetDefaultText
=================
*/
bool idDeclLocal::SetDefaultText( void ) {
	return false;
}

/*
=================
idDeclLocal::DefaultDefinition
=================
*/
const char *idDeclLocal::DefaultDefinition() const {
	return "{ }";
}

/*
=================
idDeclLocal::Parse
=================
*/
bool idDeclLocal::Parse( const char *text, const int textLength, bool cache ) {
	idLexer src;

	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SetFlags( DECL_LEXER_FLAGS );
	src.SkipUntilString( "{" );
	src.SkipBracedSection( false );
	return true;
}

/*
=================
idDeclLocal::FreeData
=================
*/
void idDeclLocal::FreeData() {
}

/*
=================
idDeclLocal::List
=================
*/
void idDeclLocal::List() const {
	common->Printf( "%s\n", GetName() );
}

/*
=================
idDeclLocal::Print
=================
*/
void idDeclLocal::Print() const {
}

/*
=================
idDeclLocal::Reload
=================
*/
void idDeclLocal::Reload( void ) {
	this->sourceFile->Reload( false );
}

/*
=================
idDeclLocal::AllocateSelf
=================
*/
void idDeclLocal::AllocateSelf( void ) {
	if ( self == NULL ) {
		self = declManagerLocal.GetDeclType( (int)type )->allocator();
		self->base = this;
	}
}

/*
=================
idDeclLocal::ParseLocal
=================
*/
void idDeclLocal::ParseLocal( bool noCaching ) {
	bool generatedDefaultText = false;

	AllocateSelf();

	// always free data before parsing
	self->FreeData();

	declManagerLocal.MediaPrint( "parsing %s %s\n", declManagerLocal.declTypes[type]->typeName.c_str(), name.c_str() );

	// if no text source try to generate default text
	if ( textSource == NULL ) {
		generatedDefaultText = self->SetDefaultText();
	}

	// indent for DEFAULTED or media file references
	declManagerLocal.indent++;

	// no text immediately causes a MakeDefault()
	if ( textSource == NULL ) {
		MakeDefault();
		declManagerLocal.indent--;
		return;
	}

	declState = DS_PARSED;

	// parse
	char *declText = (char *) _alloca( ( GetTextLength() + 1 ) * sizeof( char ) );
	GetText( declText );

	int parseTextLength = GetTextLength();
	char *parseText = declText;

	if ( idStr::Icmpn( parseText, "{ STUB:", 7 ) == 0 ) {
		idLexer stubLexer;
		stubLexer.LoadMemory( parseText, parseTextLength, GetFileName(), GetLineNum() );
		stubLexer.SetFlags( DECL_LEXER_FLAGS );

		if ( stubLexer.ExpectTokenString( "{" ) && stubLexer.ExpectTokenString( "STUB:" ) ) {
			const int stubOffset = stubLexer.ParseInt();
			const int stubLength = stubLexer.ParseInt();
			stubLexer.ExpectTokenString( "}" );

			needsPrecache = true;
			common->Warning( "You should precache: %s", GetName() );

			idFile *stubSource = fileSystem->OpenFileRead( sourceFile->fileName.c_str() );
			if ( stubSource != NULL ) {
				char *sourceText = (char *)_alloca( stubLength + 1 );
				stubSource->Seek( stubOffset, FS_SEEK_SET );
				const int readBytes = stubSource->Read( sourceText, stubLength );
				fileSystem->CloseFile( stubSource );

				if ( readBytes > 0 ) {
					sourceText[readBytes] = '\0';

					idStr definition;
					definition.Append( sourceText, readBytes );

					idLexer sourceLexer;
					idToken token;
					sourceLexer.LoadMemory( sourceText, readBytes, sourceFile->fileName.c_str(), sourceLine );
					sourceLexer.SetFlags( DECL_LEXER_FLAGS );
					if ( sourceLexer.ReadToken( &token ) && token.Icmp( "guide" ) == 0 ) {
						idToken guideDeclName;
						if ( sourceLexer.ReadToken( &guideDeclName ) ) {
							declManagerLocal.EvaluateGuide( guideDeclName, &sourceLexer, definition );
							declManagerLocal.EvaluateInlineGuide( guideDeclName, definition );
						}
					} else {
						declManagerLocal.EvaluateInlineGuide( name, definition );
					}

					SetTextLocal( definition.c_str(), definition.Length() );
					parseTextLength = definition.Length();
					parseText = (char *)_alloca( parseTextLength + 1 );
					memcpy( parseText, definition.c_str(), parseTextLength + 1 );
				}
			} else {
				common->Warning( "Couldn't open %s for reading packed decl stub.", sourceFile->fileName.c_str() );
			}
		}
	}

	if ( common->IsInitialized() && !declManagerLocal.GetInsideLoad() && !OpenQ4_IsAnyToolActive() ) {
		common->Warning( "Loading non pre-cached %s decl %s", declManagerLocal.GetDeclNameFromType( type ), name.c_str() );
	}

	self->Parse( parseText, parseTextLength, noCaching );

	// free generated text
	if ( generatedDefaultText || ( com_SingleDeclFile.GetBool() && !needsPrecache ) ) {
		Mem_Free( textSource );
		textSource = 0;
		textLength = 0;
		compressedLength = 0;
	}

	declManagerLocal.indent--;
}

/*
=================
idDeclLocal::Purge
=================
*/
void idDeclLocal::Purge( void ) {
	// never purge things that were referenced outside level load,
	// like the console and menu graphics
	if ( parsedOutsideLevelLoad ) {
		return;
	}

	referencedThisLevel = false;
	MakeDefault();

	// the next Find() for this will re-parse the real data
	declState = DS_UNPARSED;
}

/*
=================
idDeclLocal::EverReferenced
=================
*/
bool idDeclLocal::EverReferenced( void ) const {
	return everReferenced;
}

/*
=================
idDeclLocal::SetReferencedThisLevel
=================
*/
void idDeclLocal::SetReferencedThisLevel( void ) {
	referencedThisLevel = true;
	everReferenced = true;
}

/*
=================
idDeclLocal::Validate
=================
*/
bool idDeclLocal::Validate( const char *psText, int iLength, idStr &strReportTo ) const {
	(void)strReportTo;

	idDecl *decl = declManagerLocal.AllocateDecl( type );
	const bool valid = DeclManager_ValidateParsedDecl( decl, type, decl != NULL && decl->Parse( psText, iLength, false ) );
	DeclManager_FreeAllocatedDecl( decl );
	return valid;
}
