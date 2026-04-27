// DeclMatType.cpp
//

#include "../renderer/Image.h"

void R_StaticFree( void *data );

static idCVar mat_useHitMaterials( "mat_useHitMaterials", "1", CVAR_SYSTEM | CVAR_BOOL, "use cached .hit material type maps when available" );
static idCVar mat_writeHitMaterials( "mat_writeHitMaterials", "0", CVAR_SYSTEM | CVAR_BOOL, "write generated .hit material type maps to fs_savepath" );

static int MT_PackTint( const byte *tint ) {
	return static_cast<int>(
		static_cast<unsigned int>( tint[0] ) |
		( static_cast<unsigned int>( tint[1] ) << 8 ) |
		( static_cast<unsigned int>( tint[2] ) << 16 ) |
		( static_cast<unsigned int>( tint[3] ) << 24 ) );
}

static const rvDeclMatType* MT_FindMaterialTypeByTint( const byte *tint ) {
	const int count = declManager->GetNumDecls( DECL_MATERIALTYPE );
	const int tintPacked = MT_PackTint( tint );

	for ( int i = 0; i < count; ++i ) {
		const rvDeclMatType *materialType = declManager->MaterialTypeByIndex( i, true );
		if ( materialType == NULL ) {
			continue;
		}

		if ( materialType->GetTint() == tintPacked ) {
			return materialType;
		}
	}

	return NULL;
}

static const rvDeclMatType* MT_FindMaterialTypeByClosestTint( const byte *tint ) {
	const int count = declManager->GetNumDecls( DECL_MATERIALTYPE );
	const rvDeclMatType *bestMaterialType = NULL;
	int bestDistance = 0x7fffffff;

	for ( int i = 0; i < count; ++i ) {
		const rvDeclMatType *materialType = declManager->MaterialTypeByIndex( i, true );
		if ( materialType == NULL ) {
			continue;
		}

		const int packedTint = materialType->GetTint();
		const int deltaRed = tint[0] - ( packedTint & 0xFF );
		const int deltaGreen = tint[1] - ( ( packedTint >> 8 ) & 0xFF );
		const int deltaBlue = tint[2] - ( ( packedTint >> 16 ) & 0xFF );
		const int distance = ( deltaRed * deltaRed ) + ( deltaGreen * deltaGreen ) + ( deltaBlue * deltaBlue );
		if ( distance < bestDistance ) {
			bestDistance = distance;
			bestMaterialType = materialType;
		}
	}

	return bestMaterialType;
}

byte *MT_GetMaterialTypeArray( idStr image, int &width, int &height ) {
	idStr hitImage = image;
	hitImage.SetFileExtension( ".hit" );

	if ( mat_useHitMaterials.GetBool() ) {
		if ( idFile *file = fileSystem->OpenFileRead( hitImage.c_str(), true ) ) {
			int cachedHeight = 0;
			int cachedWidth = 0;
			file->ReadInt( cachedHeight );
			file->ReadInt( cachedWidth );

			if ( cachedWidth > 0 && cachedHeight > 0 ) {
				const int pixelCount = cachedWidth * cachedHeight;
				byte *array = static_cast<byte *>( Mem_Alloc( pixelCount ) );
				const int bytesRead = file->Read( array, pixelCount );
				fileSystem->CloseFile( file );

				if ( bytesRead == pixelCount ) {
					width = cachedWidth;
					height = cachedHeight;
					return array;
				}

				Mem_Free( array );
			} else {
				fileSystem->CloseFile( file );
			}
		}
	}

	image.StripFileExtension();

	byte *pic = NULL;
	R_LoadImage( image.c_str(), &pic, &width, &height, NULL, false );
	if ( pic == NULL || width <= 0 || height <= 0 ) {
		common->Warning( "Failed to load hit material image %s", image.c_str() );
		return NULL;
	}

	const int pixelCount = width * height;
	byte *array = static_cast<byte *>( Mem_Alloc( pixelCount ) );
	for ( int i = 0; i < pixelCount; ++i ) {
		const byte *pixel = pic + ( i * 4 );
		const rvDeclMatType *materialType = MT_FindMaterialTypeByTint( pixel );
		if ( materialType == NULL ) {
			materialType = MT_FindMaterialTypeByClosestTint( pixel );
		}
		array[i] = materialType != NULL ? static_cast<byte>( materialType->Index() ) : 0;
	}

	R_StaticFree( pic );

	if ( mat_writeHitMaterials.GetBool() ) {
		hitImage = image;
		hitImage.SetFileExtension( ".hit" );
		if ( idFile *file = fileSystem->OpenFileWrite( hitImage.c_str() ) ) {
			file->WriteInt( height );
			file->WriteInt( width );
			file->Write( array, pixelCount );
			fileSystem->CloseFile( file );
		}
	}

	return array;
}

/*
=======================
rvDeclMatType::DefaultDefinition
=======================
*/
const char* rvDeclMatType::DefaultDefinition(void) const {
	return "{ description \"<DEFAULTED>\" rgb 0,0,0 }";
}

/*
=======================
rvDeclMatType::Parse
=======================
*/
bool rvDeclMatType::Parse(const char* text, const int textLength) {
	return Parse(text, textLength, false);
}

/*
=======================
rvDeclMatType::Parse
=======================
*/
bool rvDeclMatType::Parse(const char* text, const int textLength, bool noCaching) {
	idLexer src;
	idToken	token, token2;
	bool success = false;

	mDescription.Clear();
	memset( mTint, 0, sizeof( mTint ) );
	mTint[3] = 0xFF;

	src.LoadMemory(text, textLength, GetFileName(), GetLineNum());
	src.SetFlags(DECL_LEXER_FLAGS);
	src.SkipUntilString("{");

	while (1) {
		if (!src.ReadToken(&token)) {
			break;
		}

		if (!token.Icmp("}")) {
			success = true;
			break;
		}
		else if (token == "rgb")
		{
			mTint[0] = src.ParseInt();
			src.ExpectTokenString(",");
			mTint[1] = src.ParseInt();
			src.ExpectTokenString(",");
			mTint[2] = src.ParseInt();
			mTint[3] = 0xFF;
		}
		else if (token == "description")
		{
			src.ReadToken(&token);
			mDescription = token;
			continue;
		}
	}
	(void)token2;
	(void)noCaching;
	return success;
}

/*
=======================
rvDeclMatType::FreeData
=======================
*/
void rvDeclMatType::FreeData(void) {
	mDescription.Clear();
}

/*
=======================
rvDeclMatType::Size
=======================
*/
size_t rvDeclMatType::Size(void) const {
	return sizeof(rvDeclMatType) + mDescription.Allocated();
}

/*
===================
rvDeclMatType::Validate
===================
*/
bool rvDeclMatType::Validate( const char *psText, int iTextLength, idStr &strReportTo ) const {
	(void)strReportTo;

	idDecl *decl = declManager->AllocateDecl( DECL_MATERIALTYPE );
	const bool valid = DeclManager_ValidateParsedDecl( decl, DECL_MATERIALTYPE, decl != NULL && decl->Parse( psText, iTextLength, false ) );
	if ( decl != NULL ) {
		decl->FreeData();
	}
	DeclManager_FreeAllocatedDecl( decl );
	return valid;
}
