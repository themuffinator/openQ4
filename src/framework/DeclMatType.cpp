// DeclMatType.cpp
//

#include "../renderer/Image.h"

void R_StaticFree( void *data );

static const rvDeclMatType* MT_FindMaterialTypeByTint( const byte *tint ) {
	const int count = declManager->GetNumDecls( DECL_MATERIALTYPE );

	for ( int i = 0; i < count; ++i ) {
		const rvDeclMatType *materialType = declManager->MaterialTypeByIndex( i, true );
		if ( materialType == NULL ) {
			continue;
		}

		const int packedTint = materialType->GetTint();
		if ( ( packedTint & 0xFF ) == tint[0]
			&& ( ( packedTint >> 8 ) & 0xFF ) == tint[1]
			&& ( ( packedTint >> 16 ) & 0xFF ) == tint[2] ) {
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
	idLexer src;
	idToken	token, token2;

	src.LoadMemory(text, textLength, GetFileName(), GetLineNum());
	src.SetFlags(DECL_LEXER_FLAGS);
	src.SkipUntilString("{");

	while (1) {
		if (!src.ReadToken(&token)) {
			break;
		}

		if (!token.Icmp("}")) {
			break;
		}
		else if (token == "rgb")
		{
			mTint[0] = src.ParseInt();
			src.ExpectTokenString(",");
			mTint[1] = src.ParseInt();
			src.ExpectTokenString(",");
			mTint[2] = src.ParseInt();
		}
		else if (token == "description")
		{
			src.ReadToken(&token);
			mDescription = token;
			continue;
		}
		else
		{
			src.Error("rvDeclMatType::Parse: Invalid or unexpected token %s\n", token.c_str());
			return false;
		}
	}
	return true;
}

/*
=======================
rvDeclMatType::FreeData
=======================
*/
void rvDeclMatType::FreeData(void) {

}

/*
=======================
rvDeclMatType::Size
=======================
*/
size_t rvDeclMatType::Size(void) const {
	return sizeof(rvDeclMatType);
}
