/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#ifndef __BINARYIMAGE_H__
#define __BINARYIMAGE_H__

#include "BinaryImageData.h"
#include "ImageContentIdentity.h"

class idFile;

/*
================================================
idBinaryImage is used by the idImage class for constructing mipmapped 
textures and for loading and saving generated files by idImage.
Also used in a memory-mapped form for imageCPU for offline megatexture
generation.
================================================
*/
class idBinaryImage {
public:
	idBinaryImage( const char * name ) : imgName( name ), loadedFileData( NULL ) { memset(&fileData,0,sizeof(fileData)); }
	~idBinaryImage() { Clear(); }

	const char *		GetName() const { return imgName.c_str(); }
	void				SetName( const char *_name ) { imgName = _name; }

	bool				Load2DFromMemory( int width, int height, const byte * pic_const, int numLevels, textureFormat_t & textureFormat, textureColor_t & colorFormat, bool gammaMips, bool filterNeutralAlpha = false );
	void				Load2DFromOwnedCompressedData( int width, int height, int numLevels, textureFormat_t textureFormat, textureColor_t colorFormat, byte *fileBuffer, const int *levelOffsets, const int *levelSizes );
	bool				LoadCubeFromMemory( int width, const byte * pics[6], int numLevels, textureFormat_t & textureFormat, bool gammaMips );

	void				Clear();
    bool GetContentIdentity(imageBinaryContent_t& out) const;
    const imageFileContent_t& GetFileContent() const { return fileContent; }
    // Internal loader publication from the SAME owned read, never a re-open.
    void ObserveFileContent(const imageFileContent_t& value) { fileContent = value; }
    void SwapContent(idBinaryImage& other) noexcept;
    bool LoadExactContentFile(const imageFileContent_t& expected);

	ID_TIME_T			LoadFromGeneratedFile( ID_TIME_T sourceFileTime );
	ID_TIME_T			LoadFromGeneratedFileUnchecked();
	ID_TIME_T			LoadFromCompactGeneratedFileUnchecked();
	ID_TIME_T			WriteGeneratedFile( ID_TIME_T sourceFileTime );
	bool				LoadFromFile( idFile *file, int dataBytes = -1 );
	bool				WriteToFile( idFile *file, ID_TIME_T sourceFileTime );

	const bimageFile_t &	GetFileHeader() const { return fileData; }

	int					NumImages() const { return images.Num(); }
	const bimageImage_t &	GetImageHeader( int i ) const { return images[i]; }
	const byte *			GetImageData( int i ) const { return images[i].data; }
	static void			GetGeneratedFileName( idStr & gfn, const char *imageName );
private:
	idStr				imgName;			// game path, including extension (except for cube maps), may be an image program
	bimageFile_t		fileData;

	class idBinaryImageData : public bimageImage_t {
	public:
		byte * data;
		bool ownsData;

		idBinaryImageData() : data( NULL ), ownsData( false ) { }
		~idBinaryImageData() { Free(); }
		idBinaryImageData & operator=( const idBinaryImageData & other ) {
			if ( this == &other ) {
				return *this;
			}

			level = other.level;
			destZ = other.destZ;
			width = other.width;
			height = other.height;
			if ( other.dataSize > 0 && other.data != NULL ) {
				Alloc( other.dataSize );
				memcpy( data, other.data, other.dataSize );
			} else {
				Free();
			}
			return *this;
		}
		void Free() {
			if ( data != NULL && ownsData ) {
				Mem_Free( data );
			}
			data = NULL;
			dataSize = 0;
			ownsData = false;
		}
		void Alloc( int size ) {
			Free();
			if ( size <= 0 ) {
				return;
			}
			dataSize = size;
			data = (byte *)Mem_Alloc( size );
			ownsData = true;
		}
		void SetExternalData( byte *externalData, int size ) {
			Free();
			data = externalData;
			dataSize = size;
			ownsData = false;
		}
	};

	idList< idBinaryImageData> images;
	byte *				loadedFileData;
    int loadedFileBytes = 0;
    imageFileContent_t fileContent{};

private:
	void				MakeGeneratedFileName( idStr & gfn );
	bool				LoadFromGeneratedFile( idFile * f, ID_TIME_T sourceFileTime, bool validateSourceFileTime, int dataBytes = -1, const imageFileContent_t* expected = NULL );
};

#endif // __BINARYIMAGE_H__
