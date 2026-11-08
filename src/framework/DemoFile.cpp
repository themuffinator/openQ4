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




idCVar idDemoFile::com_logDemos( "com_logDemos", "0", CVAR_SYSTEM | CVAR_BOOL, "Write demo.log with debug information in it" );
idCVar idDemoFile::com_compressDemos( "com_compressDemos", "1", CVAR_SYSTEM | CVAR_INTEGER | CVAR_ARCHIVE, "Compression scheme for demo files\n0: None    (Fast, large files)\n1: LZW     (Fast to compress, Fast to decompress, medium/small files)\n2: LZSS    (Slow to compress, Fast to decompress, small files)\n3: Huffman (Fast to compress, Slow to decompress, medium files)\nSee also: The 'CompressDemo' command" );
idCVar idDemoFile::com_preloadDemos( "com_preloadDemos", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_ARCHIVE, "Load the whole demo in to RAM before running it" );

#define DEMO_MAGIC GAME_NAME " RDEMO"
#define DEMO_MAGIC_HISTORICAL "OpenQ4 RDEMO"
#define DEMO_MAGIC_QUAKE4 "Quake4 RDEMO"
#define MAX_DEMO_HASH_STRINGS 65536
#define MAX_DEMO_DICT_ENTRIES 4096

/*
================
idDemoFile::idDemoFile
================
*/
idDemoFile::idDemoFile() {
	f = NULL;
	fLog = NULL;
	log = false;
	fileImage = NULL;
	compressor = NULL;
	writing = false;
}

/*
================
idDemoFile::~idDemoFile
================
*/
idDemoFile::~idDemoFile() {
	Close();
}

/*
================
idDemoFile::AllocCompressor
================
*/
idCompressor *idDemoFile::AllocCompressor( int type ) {
	switch ( type ) {
	case 0: return idCompressor::AllocNoCompression();
	case 1: return idCompressor::AllocLZW();
	case 2: return idCompressor::AllocLZSS();
	case 3: return idCompressor::AllocHuffman();
	default: return NULL;
	}
}

/*
================
idDemoFile::OpenForReading
================
*/
bool idDemoFile::OpenForReading( const char *fileName, bool allowPreload, bool quiet ) {
	static const int magicLen = sizeof(DEMO_MAGIC) / sizeof(DEMO_MAGIC[0]);
	char magicBuffer[magicLen];
	int compression;
	int fileLength;
	bool ambiguousQuake4Wrapper = false;

	Close();

	f = fileSystem->OpenFileRead( fileName );
	if ( !f ) {
		return false;
	}

	fileLength = f->Length();
	if ( fileLength <= 0 ) {
		Close();
		return false;
	}

	if ( allowPreload && com_preloadDemos.GetBool() ) {
		fileImage = (byte *)Mem_Alloc( fileLength );
		if ( f->Read( fileImage, fileLength ) != fileLength ) {
			Close();
			return false;
		}
		fileSystem->CloseFile( f );
		f = new idFile_Memory( va( "preloaded(%s)", fileName ), (const char *)fileImage, fileLength );
	}

	if ( com_logDemos.GetBool() ) {
		fLog = fileSystem->OpenFileWrite( "demoread.log" );
	}

	writing = false;

	if ( f->Read( magicBuffer, magicLen ) != magicLen ) {
		Close();
		return false;
	}
	if ( memcmp( magicBuffer, DEMO_MAGIC, magicLen ) == 0 ||
		 memcmp( magicBuffer, DEMO_MAGIC_HISTORICAL, magicLen ) == 0 ||
		 memcmp( magicBuffer, DEMO_MAGIC_QUAKE4, magicLen ) == 0 ) {
		ambiguousQuake4Wrapper =
			memcmp( magicBuffer, DEMO_MAGIC_QUAKE4, magicLen ) == 0;
		if ( f->ReadInt( compression ) != sizeof( compression ) ) {
			Close();
			return false;
		}
	} else {
		// Ideally we would error out if the magic string isn't there,
		// but for backwards compatibility we are going to assume it's just an uncompressed demo file
		compression = 0;
		f->Rewind();
	}

	compressor = AllocCompressor( compression );
	if ( compressor == NULL ) {
		Close();
		return false;
	}
	compressor->Init( f, false, 8 );

	// The earliest openQ4 builds and retail Quake 4 used the same wrapper
	// spelling.  Probe the decoded token before accepting it: openQ4 streams
	// begin with the 32-bit DS_VERSION token, while retail streams use a
	// byte-oriented command layout.  Reinitialize the decompressor afterward
	// so callers still see the stream from byte zero.
	if ( ambiguousQuake4Wrapper ) {
		int firstToken = DS_FINISHED;
		if ( ReadInt( firstToken ) != sizeof( firstToken ) ||
			 firstToken != DS_VERSION ) {
			Close();
			if ( !quiet ) {
				common->Warning( "Demo '%s' uses the unsupported retail Quake 4 render-demo stream", fileName );
			}
			return false;
		}
		delete compressor;
		compressor = NULL;
		if ( f->Seek( magicLen + static_cast<int>( sizeof( int ) ), FS_SEEK_SET ) != 0 ) {
			Close();
			return false;
		}
		compressor = AllocCompressor( compression );
		if ( compressor == NULL ) {
			Close();
			return false;
		}
		compressor->Init( f, false, 8 );
	}

	return true;
}

/*
================
idDemoFile::SetLog
================
*/
void idDemoFile::SetLog(bool b, const char *p) {
	log = b;
	if (p) {
		logStr = p;
	}
}

/*
================
idDemoFile::Log
================
*/
void idDemoFile::Log(const char *p) {
	if ( fLog && p && *p ) {
		fLog->Write( p, idLib::SizeToInt( strlen( p ), "idDemoFile::Log" ) );
	}
}

/*
================
idDemoFile::OpenForWriting
================
*/
bool idDemoFile::OpenForWriting( const char *fileName ) {
	Close();

	f = fileSystem->OpenFileWrite( fileName );
	if ( f == NULL ) {
		return false;
	}

	if ( com_logDemos.GetBool() ) {
		fLog = fileSystem->OpenFileWrite( "demowrite.log" );
	}

	writing = true;

	f->Write(DEMO_MAGIC, sizeof(DEMO_MAGIC));
	int compression = com_compressDemos.GetInteger();
	compressor = AllocCompressor( compression );
	if ( compressor == NULL ) {
		Close();
		return false;
	}
	f->WriteInt( compression );
	f->Flush();

	compressor->Init( f, true, 8 );

	return true;
}

/*
================
idDemoFile::Close
================
*/
void idDemoFile::Close() {
	if ( writing && compressor ) {
		compressor->FinishCompress();
	}

	if ( f ) {
		fileSystem->CloseFile( f );
		f = NULL;
	}
	if ( fLog ) {
		fileSystem->CloseFile( fLog );
		fLog = NULL;
	}
	if ( fileImage ) {
		Mem_Free( fileImage );
		fileImage = NULL;
	}
	if ( compressor ) {
		delete compressor;
		compressor = NULL;
	}

	ResetHashStrings();
}

/*
================
idDemoFile::ResetHashStrings
================
*/
void idDemoFile::ResetHashStrings() {
	demoStrings.DeleteContents( true );
	demoStringHash.Clear();
}

/*
================
idDemoFile::CloseWithTrailer

The trailer lies outside the compressed stream, so it is written raw after
the compressor has flushed. Readers stop at the stream's DSC_END record and
never decode these bytes.
================
*/
bool idDemoFile::CloseWithTrailer( const idDict &summary ) {
	if ( !writing || compressor == NULL || f == NULL ) {
		Close();
		return false;
	}
	compressor->FinishCompress();
	delete compressor;
	compressor = NULL;

	idFile_Memory payload( "demoTrailer" );
	const int count = Min( summary.GetNumKeyVals(), MAX_DEMO_DICT_ENTRIES );
	payload.WriteInt( count );
	for ( int i = 0; i < count; i++ ) {
		const idKeyValue *kv = summary.GetKeyVal( i );
		payload.WriteString( kv->GetKey() );
		payload.WriteString( kv->GetValue() );
	}
	const int length = payload.Length();
	bool written = length > 0 && length <= DEMO_TRAILER_MAX_PAYLOAD;
	if ( written ) {
		const unsigned int crc = CRC32_BlockChecksum( payload.GetDataPtr(), length );
		written =
			f->Write( payload.GetDataPtr(), length ) == length &&
			f->WriteInt( length ) == static_cast<int>( sizeof( length ) ) &&
			f->WriteUnsignedInt( crc ) == static_cast<int>( sizeof( crc ) ) &&
			f->Write( DEMO_TRAILER_MAGIC, DEMO_TRAILER_MAGIC_LENGTH ) == DEMO_TRAILER_MAGIC_LENGTH;
	}
	writing = false;
	Close();
	return written;
}

/*
================
idDemoFile::ReadTrailer
================
*/
bool idDemoFile::ReadTrailer( const char *fileName, idDict &summary ) {
	summary.Clear();
	idFile *file = fileSystem->OpenFileRead( fileName, false );
	if ( file == NULL ) {
		return false;
	}
	const int footerBytes = static_cast<int>( sizeof( int ) + sizeof( unsigned int ) ) + DEMO_TRAILER_MAGIC_LENGTH;
	const int fileLength = file->Length();
	bool valid = fileLength > footerBytes &&
		file->Seek( fileLength - footerBytes, FS_SEEK_SET ) == 0;
	int length = 0;
	unsigned int crc = 0;
	char magic[DEMO_TRAILER_MAGIC_LENGTH];
	valid = valid &&
		file->ReadInt( length ) == static_cast<int>( sizeof( length ) ) &&
		file->ReadUnsignedInt( crc ) == static_cast<int>( sizeof( crc ) ) &&
		file->Read( magic, DEMO_TRAILER_MAGIC_LENGTH ) == DEMO_TRAILER_MAGIC_LENGTH &&
		memcmp( magic, DEMO_TRAILER_MAGIC, DEMO_TRAILER_MAGIC_LENGTH ) == 0 &&
		length > 0 && length <= DEMO_TRAILER_MAX_PAYLOAD &&
		length <= fileLength - footerBytes &&
		file->Seek( fileLength - footerBytes - length, FS_SEEK_SET ) == 0;
	idList<byte> payload;
	if ( valid ) {
		payload.SetNum( length );
		valid = file->Read( payload.Ptr(), length ) == length &&
			CRC32_BlockChecksum( payload.Ptr(), length ) == crc;
	}
	fileSystem->CloseFile( file );
	if ( !valid ) {
		return false;
	}

	idFile_Memory reader( "demoTrailer", reinterpret_cast<const char *>( payload.Ptr() ), length );
	int count = 0;
	if ( reader.ReadInt( count ) != static_cast<int>( sizeof( count ) ) ||
		 count < 0 || count > MAX_DEMO_DICT_ENTRIES ) {
		return false;
	}
	for ( int i = 0; i < count; i++ ) {
		idStr key, value;
		if ( reader.ReadString( key ) <= 0 || reader.ReadString( value ) < 0 ) {
			summary.Clear();
			return false;
		}
		summary.Set( key, value );
	}
	return true;
}

/*
================
idDemoFile::ReadHashString
================
*/
const char *idDemoFile::ReadHashString() {
	int		index;

	if ( log && fLog ) {
		const char *text = va( "%s > Reading hash string\n", logStr.c_str() );
		fLog->Write( text, idLib::SizeToInt( strlen( text ), "idDemoFile::ReadHashString" ) );
	} 

	if ( ReadInt( index ) != sizeof( index ) ) {
		Close();
		common->Warning( "Malformed render demo: truncated hash index; playback stopped safely" );
		return "";
	}

	if ( index == -1 ) {
		// read a new string for the table
		if ( demoStrings.Num() >= MAX_DEMO_HASH_STRINGS ) {
			Close();
			common->Warning( "Malformed render demo: hash table exceeds %d entries; playback stopped safely", MAX_DEMO_HASH_STRINGS );
			return "";
		}

		int length = 0;
		if ( ReadInt( length ) != sizeof( length ) || length < 0 || length >= MAX_STRING_CHARS ) {
			Close();
			common->Warning( "Malformed render demo: invalid hash string length %d; playback stopped safely", length );
			return "";
		}

		idStr data;
		if ( length > 0 ) {
			data.Fill( ' ', length );
			if ( Read( &data[0], length ) != length ) {
				Close();
				common->Warning( "Malformed render demo: truncated hash string payload; playback stopped safely" );
				return "";
			}
		}

		idStr	*str = new idStr;
		*str = data;
		
		demoStrings.Append( str );

		return *str;
	}

	if ( index < -1 || index >= demoStrings.Num() ) {
		Close();
		common->Warning( "Malformed render demo: hash index %d is out of range; playback stopped safely", index );
		return "";
	}

	return demoStrings[index]->c_str();
}

/*
================
idDemoFile::WriteHashString
================
*/
void idDemoFile::WriteHashString( const char *str ) {
	if ( str == NULL ) {
		str = "";
	}

	if ( log && fLog ) {
		const char *text = va( "%s > Writing hash string\n", logStr.c_str() );
		fLog->Write( text, idLib::SizeToInt( strlen( text ), "idDemoFile::WriteHashString" ) );
	}
	// see if it is already in the table; a campaign recording sees tens of
	// thousands of names, so this must not be a linear search
	const int key = demoStringHash.GenerateKey( str, true );
	for ( int i = demoStringHash.First( key ); i != -1; i = demoStringHash.Next( i ) ) {
		if ( !strcmp( demoStrings[i]->c_str(), str ) ) {
			WriteInt( i );
			return;
		}
	}

	// add it to our table and the demo table
	idStr	*copy = new idStr( str );
//common->Printf( "hash:%i = %s\n", demoStrings.Num(), str );
	demoStringHash.Add( key, demoStrings.Num() );
	demoStrings.Append( copy );
	int cmd = -1;	
	WriteInt( cmd );
	WriteString( str );
}

/*
================
idDemoFile::ReadDict
================
*/
void idDemoFile::ReadDict( idDict &dict ) {
	int i, c;
	idStr key, val;

	dict.Clear();
	if ( ReadInt( c ) != sizeof( c ) || c < 0 || c > MAX_DEMO_DICT_ENTRIES ) {
		Close();
		common->Warning( "Malformed render demo: dictionary count is out of range; playback stopped safely" );
		return;
	}
	for ( i = 0; i < c; i++ ) {
		key = ReadHashString();
		if ( !IsOpen() ) {
			return;
		}
		val = ReadHashString();
		if ( !IsOpen() ) {
			return;
		}
		dict.Set( key, val );
	}
}

/*
================
idDemoFile::WriteDict
================
*/
void idDemoFile::WriteDict( const idDict &dict ) {
	int i, c;

	c = dict.GetNumKeyVals();
	WriteInt( c );
	for ( i = 0; i < c; i++ ) {
		WriteHashString( dict.GetKeyVal( i )->GetKey() );
		WriteHashString( dict.GetKeyVal( i )->GetValue() );
	}
}

/*
 ================
 idDemoFile::Read
 ================
 */
int idDemoFile::Read( void *buffer, int len ) {
	if ( compressor == NULL ) {
		return 0;
	}
	int read = compressor->Read( buffer, len );
	if ( read == 0 && len >= 4 ) {
		*(demoSystem_t *)buffer = DS_FINISHED;
	}
	return read;
}

/*
 ================
 idDemoFile::Write
 ================
 */
int idDemoFile::Write( const void *buffer, int len ) {
	if ( compressor == NULL || buffer == NULL || len <= 0 ) {
		return 0;
	}
	return compressor->Write( buffer, len );
}




