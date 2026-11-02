// Executes the production MVD reader, writer and interrupted-recording recovery
// (idMultiViewDemo::Recover) against an in-memory filesystem. The Python driver
// tools/tests/mvd_recovery_contract.py splices the functions and the format
// constants in verbatim from src/framework/async/MultiViewDemo.cpp.
//
// A synthetic recording is written with the production writer, cut at every
// byte, corrupted and given a stale index, and each staged .part is recovered.
// The result is read back with the production reader and checked against the
// clean-end rules the playback path enforces. Format 1.3 marker and keyframe
// records are rebuilt into their directories from the records that survived.

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/framework/async/MvdNavigation.h"

#ifndef NULL
#define NULL nullptr
#endif

typedef unsigned char byte;

static const int ASYNC_PROTOCOL_MAJOR = 2;
static const int ASYNC_PROTOCOL_MINOR = 41;
static const int MAX_ASYNC_CLIENTS = 32;
static const int SERVER_MVD_MARKER_KIND_COUNT = 9;
static const int FS_SEEK_SET = 0;

template<class T> static T Min( T a, T b ) { return a < b ? a : b; }
template<class T> static T Max( T a, T b ) { return a > b ? a : b; }
static int LittleLong( int value ) { return value; }
static unsigned int LittleLong( unsigned int value ) { return value; }
static unsigned short LittleShort( unsigned short value ) { return value; }

static unsigned int CRC32_BlockChecksum( const void *data, int length ) {
	static unsigned int table[256];
	static bool built = false;
	if ( !built ) {
		for ( unsigned int i = 0; i < 256; i++ ) {
			unsigned int c = i;
			for ( int k = 0; k < 8; k++ ) {
				c = ( c & 1 ) ? 0xedb88320u ^ ( c >> 1 ) : c >> 1;
			}
			table[i] = c;
		}
		built = true;
	}
	unsigned int crc = 0xffffffffu;
	const byte *bytes = static_cast<const byte *>( data );
	for ( int i = 0; i < length; i++ ) {
		crc = table[( crc ^ bytes[i] ) & 0xff] ^ ( crc >> 8 );
	}
	return crc ^ 0xffffffffu;
}

static const char *va( const char *format, ... ) {
	static char buffers[4][512];
	static int index = 0;
	char *buffer = buffers[index++ & 3];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, 512, format, args );
	va_end( args );
	return buffer;
}

class idStr {
public:
	std::string s;
	idStr() {}
	idStr( const char *text ) : s( text ? text : "" ) {}
	idStr( const std::string &text ) : s( text ) {}
	const char *c_str() const { return s.c_str(); }
	int Length() const { return static_cast<int>( s.size() ); }
	bool IsEmpty() const { return s.empty(); }
	void Clear() { s.clear(); }
	char operator[]( int index ) const { return s[index]; }
	idStr &operator=( const char *text ) { s = text ? text : ""; return *this; }
	idStr &operator+=( const char *text ) { s += text; return *this; }
	idStr &operator+=( const idStr &text ) { s += text.s; return *this; }
	friend idStr operator+( const char *a, const idStr &b ) { return idStr( std::string( a ) + b.s ); }
	bool operator==( const idStr &o ) const { return s == o.s; }
	void Append( char c ) { s.push_back( c ); }
	void BackSlashesToSlashes() { std::replace( s.begin(), s.end(), '\\', '/' ); }
	int Find( const char *text ) const { size_t at = s.find( text ); return at == std::string::npos ? -1 : static_cast<int>( at ); }
	void StripPath() { size_t at = s.find_last_of( '/' ); if ( at != std::string::npos ) s = s.substr( at + 1 ); }
	void StripFileExtension() {
		size_t dot = s.find_last_of( '.' ), slash = s.find_last_of( '/' );
		if ( dot != std::string::npos && ( slash == std::string::npos || dot > slash ) ) s.resize( dot );
	}
	void DefaultFileExtension( const char *extension ) {
		size_t dot = s.find_last_of( '.' ), slash = s.find_last_of( '/' );
		if ( dot == std::string::npos || ( slash != std::string::npos && dot < slash ) ) s += extension;
	}
	static int Icmp( const char *a, const char *b ) {
		for ( ;; a++, b++ ) {
			const int x = std::tolower( (unsigned char)*a ), y = std::tolower( (unsigned char)*b );
			if ( x != y || !x ) return x - y;
		}
	}
	static int Icmpn( const char *a, const char *b, int n ) {
		for ( int i = 0; i < n; i++, a++, b++ ) {
			const int x = std::tolower( (unsigned char)*a ), y = std::tolower( (unsigned char)*b );
			if ( x != y || !x ) return x - y;
		}
		return 0;
	}
};

template<class T> class idList {
public:
	std::vector<T> v;
	void SetNum( int n ) { v.resize( n ); }
	int Num() const { return static_cast<int>( v.size() ); }
	T *Ptr() { return v.data(); }
	const T *Ptr() const { return v.data(); }
	void Append( const T &value ) { v.push_back( value ); }
	void Clear() { v.clear(); }
	T &operator[]( int i ) { return v[i]; }
	const T &operator[]( int i ) const { return v[i]; }
};

class idFile {
public:
	std::vector<byte> *data = nullptr;
	int position = 0;
	bool syncOK = true;
	int Read( void *out, int length ) {
		const int available = Max( 0, static_cast<int>( data->size() ) - position );
		const int count = Min( length, available );
		if ( count > 0 ) {
			memcpy( out, data->data() + position, count );
		}
		position += count;
		return count;
	}
	int Write( const void *in, int length ) {
		if ( static_cast<int>( data->size() ) < position + length ) {
			data->resize( position + length );
		}
		if ( length > 0 ) {
			memcpy( data->data() + position, in, length );
		}
		position += length;
		return length;
	}
	int Tell() const { return position; }
	int Seek( long offset, int origin ) { assert( origin == FS_SEEK_SET ); position = static_cast<int>( offset ); return 0; }
	int Length() const { return static_cast<int>( data->size() ); }
	bool Sync() { return syncOK; }
};

class idFile_Memory {
public:
	std::vector<byte> data;
	int position = 0;
	explicit idFile_Memory( const char * ) {}
	idFile_Memory( const char *, const char *bytes, int length ) : data( bytes, bytes + length ) {}
	int Write( const void *in, int length ) { const byte *b = static_cast<const byte *>( in ); data.insert( data.end(), b, b + length ); return length; }
	template<class T> int Put( T value ) { return Write( &value, sizeof( value ) ); }
	int WriteUnsignedInt( unsigned int value ) { return Put( value ); }
	int WriteUnsignedShort( unsigned short value ) { return Put( value ); }
	int WriteInt( int value ) { return Put( value ); }
	int Read( void *out, int length ) {
		const int count = Min( length, static_cast<int>( data.size() ) - position );
		if ( count > 0 ) memcpy( out, data.data() + position, count );
		position += Max( 0, count );
		return Max( 0, count );
	}
	template<class T> int Get( T &value ) { return Read( &value, sizeof( value ) ); }
	int ReadUnsignedInt( unsigned int &value ) { return Get( value ); }
	int ReadUnsignedShort( unsigned short &value ) { return Get( value ); }
	int ReadInt( int &value ) { return Get( value ); }
	const byte *GetDataPtr() const { return data.data(); }
	int Length() const { return static_cast<int>( data.size() ); }
};

class idCmdArgs {
public:
	std::vector<std::string> args;
	void AppendArg( const char *text ) { args.push_back( text ); }
	int Argc() const { return static_cast<int>( args.size() ); }
	const char *Argv( int n ) const { return n >= 0 && n < Argc() ? args[n].c_str() : ""; }
};

struct FileSystem {
	std::map<std::string, std::vector<byte>> files;
	std::vector<std::unique_ptr<idFile>> handles;
	bool promoteOK = true;
	idFile *Open( const char *path, bool create ) {
		auto found = files.find( path );
		if ( found == files.end() ) {
			if ( !create ) return nullptr;
			found = files.emplace( path, std::vector<byte>() ).first;
		}
		if ( create ) found->second.clear();
		handles.emplace_back( new idFile() );
		handles.back()->data = &found->second;
		return handles.back().get();
	}
	idFile *OpenFileRead( const char *path, bool = true ) { return Open( path, false ); }
	idFile *OpenFileWrite( const char *path, const char * ) { return Open( path, true ); }
	void CloseFile( idFile * ) {}
	int ReadFile( const char *path, void **, unsigned int * ) {
		auto found = files.find( path );
		return found == files.end() ? -1 : static_cast<int>( found->second.size() );
	}
	bool PromoteFile( const char *staged, const char *final, const char * ) {
		if ( !promoteOK || files.count( final ) ) return false;
		files[final] = files[staged];
		files.erase( staged );
		return true;
	}
	void RemoveFile( const char *path, const char * ) { files.erase( path ); }
} fs, *fileSystem = &fs;

struct Common {
	std::string last;
	void Printf( const char *format, ... ) {
		char buffer[1024];
		va_list args;
		va_start( args, format );
		std::vsnprintf( buffer, sizeof( buffer ), format, args );
		va_end( args );
		last = buffer;
	}
	void Warning( const char *, ... ) {}
	void DPrintf( const char *, ... ) {}
} commonValue, *common = &commonValue;

// PRODUCTION_CONSTANTS

class idMultiViewDemo {
public:
	enum state_t { MVD_IDLE, MVD_RECORDING, MVD_PLAYING };
	struct header_t {
		unsigned short formatMajor, formatMinor;
		unsigned int requiredFeatures, optionalFeatures;
		unsigned short protocolMajor, protocolMinor;
		unsigned int gameSchemaVersion, usercmdHz;
		int startGameFrame, startGameTime;
		unsigned int snapshotDelay, contentChecksum, indexOffset;
	};
	struct record_t {
		unsigned short type = 0, version = 0, flags = 0;
		idList<byte> payload;
	};
	enum readResult_t { MVD_READ_OK, MVD_READ_EOF, MVD_READ_ERROR };
	struct indexEntry_t { int gameTime, fileOffset, sequence; };

	state_t state = MVD_IDLE;
	idFile *file = nullptr;
	idStr fileName, tempFileName, lastError;
	header_t header = {};
	int lastSnapshotGameFrame = 0, lastSnapshotGameTime = 0, lastSnapshotFileOffset = 0;
	int snapshotSequence = 0, snapshotCount = 0, reliableCount = 0, recordCount = 0;
	idList<indexEntry_t> recordingIndex;
	std::vector<openq4_mvd::Marker> markers;
	std::vector<openq4_mvd::Keyframe> keyframes;
	int markersDropped = 0;

	void Clear() {
		state = MVD_IDLE;
		file = nullptr;
		fileName.Clear();
		tempFileName.Clear();
		lastError.Clear();
		header = header_t();
		lastSnapshotGameFrame = lastSnapshotGameTime = lastSnapshotFileOffset = 0;
		snapshotSequence = snapshotCount = reliableCount = recordCount = 0;
		recordingIndex.Clear();
		markers.clear();
		keyframes.clear();
		markersDropped = 0;
	}
	bool WouldExceedLimits( int ) const { return false; }
	bool WriteHeader();
	bool ReadHeader( idFile *source, header_t &outHeader );
	bool WriteRecord( unsigned short type, unsigned short version, unsigned short flags, const void *payload, int payloadLength, bool enforceLimits = true );
	readResult_t ReadRecord( idFile *source, record_t &record );
	bool WriteIndexRecord();
	bool WriteNavigationRecords();
	bool NoteNavigationRecord( const record_t &record, int fileOffset );
	idStr BuildRecordingName( const idCmdArgs &args ) const;
	bool Recover( const idCmdArgs &args );
};

// PRODUCTION_FUNCTIONS

static int failures = 0;
#define CHECK( condition ) do { if ( !( condition ) ) { std::fprintf( stderr, "FAILED line %d: %s\n", __LINE__, #condition ); failures++; } } while ( 0 )

struct Layout {
	std::vector<byte> bytes;
	std::vector<int> recordEnds;		// file offset after each record
	std::vector<int> recordTypes;
	std::vector<int> recordStarts;
};

// A marker record as RecordMarker writes it.
static std::vector<std::uint8_t> MarkerPayload( int frame, int time, int victim ) {
	openq4_mvd::Marker marker;
	marker.gameTime = time;
	marker.kind = 5;
	marker.client = 1;
	marker.value0 = victim;
	marker.value1 = 7;
	std::vector<std::uint8_t> payload;
	openq4_mvd::AppendU32( payload, static_cast<std::uint32_t>( frame ) );
	openq4_mvd::AppendMarkerBody( payload, marker );
	return payload;
}

// A keyframe record's grammar: counts, then three non-empty blocks.
static std::vector<std::uint8_t> KeyframePayload( int frame, int time, int sequence, int reliables, int records ) {
	std::vector<std::uint8_t> payload;
	for ( int value : { frame, time, sequence, reliables, records } ) {
		openq4_mvd::AppendU32( payload, static_cast<std::uint32_t>( value ) );
	}
	for ( int block = 0; block < 3; block++ ) {
		openq4_mvd::AppendU32( payload, 3 );
		payload.insert( payload.end(), { 7, 8, 9 } );
	}
	return payload;
}

// A recording as StartRecording and CaptureServerFrame write it: header,
// metadata, map and network state, then reliable messages and snapshots every
// 100 ms of game time for three seconds, with no index or end record.
static Layout WriteSource( bool withStaleIndex ) {
	fs.files.clear();
	idMultiViewDemo writer;
	writer.file = fs.OpenFileWrite( "demos/source.mvd.part", "fs_savepath" );
	writer.header.formatMajor = MVD_FORMAT_MAJOR;
	writer.header.formatMinor = MVD_FORMAT_MINOR;
	writer.header.requiredFeatures = MVD_SUPPORTED_REQUIRED_FEATURES;
	writer.header.optionalFeatures = MVD_OPTIONAL_CLEAN_END | MVD_OPTIONAL_TIMELINE_INDEX;
	writer.header.protocolMajor = ASYNC_PROTOCOL_MAJOR;
	writer.header.protocolMinor = ASYNC_PROTOCOL_MINOR;
	writer.header.gameSchemaVersion = 0x00010000u;
	writer.header.usercmdHz = 60;
	writer.header.startGameFrame = 600;
	writer.header.startGameTime = 9600;
	writer.header.snapshotDelay = 100;
	writer.header.contentChecksum = 0x1234u;
	Layout layout;
	assert( writer.WriteHeader() );
	const auto note = [&]( int type, int start ) {
		layout.recordStarts.push_back( start );
		layout.recordEnds.push_back( writer.file->Tell() );
		layout.recordTypes.push_back( type );
	};
	const byte init[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	const unsigned short initTypes[3] = { MVD_RECORD_METADATA, MVD_RECORD_MAP_STATE, MVD_RECORD_NETWORK_STATE };
	for ( unsigned short type : initTypes ) {
		const int start = writer.file->Tell();
		assert( writer.WriteRecord( type, 1, type == MVD_RECORD_METADATA ? 0 : MVD_RECORD_FLAG_REQUIRED, init, sizeof( init ) ) );
		note( type, start );
	}
	int sequence = 0;
	for ( int step = 1; step <= 30; step++ ) {
		const int frame = 600 + step * 6;
		const int time = 9600 + step * 100;
		if ( step % 3 == 0 ) {
			idFile_Memory reliable( "reliable" );
			reliable.WriteInt( frame );
			reliable.WriteInt( time );
			reliable.WriteInt( step );
			reliable.Write( init, 5 );
			const int start = writer.file->Tell();
			assert( writer.WriteRecord( MVD_RECORD_RELIABLE, 2, MVD_RECORD_FLAG_REQUIRED, reliable.GetDataPtr(), reliable.Length() ) );
			note( MVD_RECORD_RELIABLE, start );
			writer.reliableCount++;
		}
		if ( step % 10 == 5 ) {
			const std::vector<std::uint8_t> marker = MarkerPayload( frame, time, step % 4 );
			const int start = writer.file->Tell();
			assert( writer.WriteRecord( MVD_RECORD_MARKER, 1, 0, marker.data(), static_cast<int>( marker.size() ) ) );
			note( MVD_RECORD_MARKER, start );
		}
		idFile_Memory snapshot( "snapshot" );
		snapshot.WriteInt( frame );
		snapshot.WriteInt( time );
		snapshot.WriteInt( ++sequence );
		snapshot.Write( init, 9 + step % 7 );
		int start = writer.file->Tell();
		assert( writer.WriteRecord( MVD_RECORD_SNAPSHOT, 2, MVD_RECORD_FLAG_REQUIRED, snapshot.GetDataPtr(), snapshot.Length() ) );
		note( MVD_RECORD_SNAPSHOT, start );
		if ( step % 10 == 0 ) {
			const std::vector<std::uint8_t> keyframe = KeyframePayload( frame, time, sequence, writer.reliableCount, writer.recordCount );
			start = writer.file->Tell();
			assert( writer.WriteRecord( MVD_RECORD_KEYFRAME, 1, 0, keyframe.data(), static_cast<int>( keyframe.size() ) ) );
			note( MVD_RECORD_KEYFRAME, start );
		}
	}
	if ( withStaleIndex ) {
		// A finalize that wrote its index and directories but died before the end record.
		const byte stale[8] = {};
		int start = writer.file->Tell();
		assert( writer.WriteRecord( MVD_RECORD_INDEX, 1, 0, stale, sizeof( stale ), false ) );
		note( MVD_RECORD_INDEX, start );
		start = writer.file->Tell();
		assert( writer.WriteRecord( MVD_RECORD_MARKER_TABLE, 1, 0, stale, 4, false ) );
		note( MVD_RECORD_MARKER_TABLE, start );
		start = writer.file->Tell();
		assert( writer.WriteRecord( MVD_RECORD_KEYFRAME_TABLE, 1, 0, stale, 4, false ) );
		note( MVD_RECORD_KEYFRAME_TABLE, start );
	}
	layout.bytes = fs.files["demos/source.mvd.part"];
	return layout;
}

struct Verdict {
	bool readable = false;
	int snapshots = 0, reliables = 0, records = 0, endRecords = 0, indexRecords = 0;
	bool endMatches = false, indexValid = false;
	int markerTables = 0, keyframeTables = 0;
	std::vector<openq4_mvd::Marker> markers;
	std::vector<openq4_mvd::Keyframe> keyframes;
	std::vector<int> keyframeRecordOffsets;
	bool directoriesValid = true;
};

// Read a published demo back with the production reader and apply the clean-end
// rules LoadNextPlaybackRecord and BuildPlaybackIndex enforce.
static Verdict ReadBack( const char *path ) {
	Verdict verdict;
	idMultiViewDemo reader;
	idFile *source = fs.OpenFileRead( path );
	if ( source == nullptr ) {
		return verdict;
	}
	idMultiViewDemo::header_t header;
	if ( !reader.ReadHeader( source, header ) ) {
		return verdict;
	}
	verdict.readable = true;
	int lastFrame = header.startGameFrame, lastTime = header.startGameTime;
	std::vector<int> snapshotOffsets;
	int indexOffset = -1;
	idMultiViewDemo::record_t indexRecord;
	for ( ;; ) {
		const int offset = source->Tell();
		idMultiViewDemo::record_t record;
		const idMultiViewDemo::readResult_t result = reader.ReadRecord( source, record );
		if ( result != idMultiViewDemo::MVD_READ_OK ) {
			verdict.readable = verdict.readable && result == idMultiViewDemo::MVD_READ_EOF;
			break;
		}
		if ( record.type == MVD_RECORD_END ) {
			verdict.endRecords++;
			const byte *p = record.payload.Ptr();
			verdict.endMatches = record.version == 1 && record.payload.Num() == 20 &&
				static_cast<int>( MVD_ReadLittleUInt( p ) ) >= lastFrame &&
				static_cast<int>( MVD_ReadLittleUInt( p + 4 ) ) >= lastTime &&
				static_cast<int>( MVD_ReadLittleUInt( p + 8 ) ) == verdict.snapshots &&
				static_cast<int>( MVD_ReadLittleUInt( p + 12 ) ) == verdict.reliables &&
				static_cast<int>( MVD_ReadLittleUInt( p + 16 ) ) == verdict.records;
			continue;
		}
		verdict.records++;
		if ( record.type == MVD_RECORD_SNAPSHOT ) {
			verdict.snapshots++;
			snapshotOffsets.push_back( offset );
		} else if ( record.type == MVD_RECORD_RELIABLE ) {
			verdict.reliables++;
		} else if ( record.type == MVD_RECORD_INDEX ) {
			verdict.indexRecords++;
			indexOffset = offset;
			indexRecord = record;
		} else if ( record.type == MVD_RECORD_KEYFRAME ) {
			verdict.keyframeRecordOffsets.push_back( offset );
		} else if ( record.type == MVD_RECORD_MARKER_TABLE ) {
			verdict.markerTables++;
			verdict.directoriesValid = verdict.directoriesValid && openq4_mvd::ParseMarkerTable(
				record.payload.Ptr(), record.payload.Num(), verdict.markers, SERVER_MVD_MARKER_KIND_COUNT,
				MAX_ASYNC_CLIENTS, 8 );
		} else if ( record.type == MVD_RECORD_KEYFRAME_TABLE ) {
			verdict.keyframeTables++;
			verdict.directoriesValid = verdict.directoriesValid && indexOffset >= 0 && openq4_mvd::ParseKeyframeTable(
				record.payload.Ptr(), record.payload.Num(), static_cast<int>( MVD_HEADER_BYTES ), indexOffset,
				verdict.keyframes );
		}
		if ( ( record.type == MVD_RECORD_SNAPSHOT || record.type == MVD_RECORD_RELIABLE ) && record.payload.Num() >= 8 ) {
			lastFrame = Max( lastFrame, static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() ) ) );
			lastTime = Max( lastTime, static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) ) );
		}
	}
	// The header points at the one index, whose entries name snapshot records.
	if ( indexOffset >= 0 && static_cast<int>( header.indexOffset ) == indexOffset && indexRecord.payload.Num() >= 8 ) {
		const byte *p = indexRecord.payload.Ptr();
		const int entries = static_cast<int>( MVD_ReadLittleUInt( p ) );
		bool valid = entries > 0 && indexRecord.payload.Num() == 4 + entries * 12 + 4;
		for ( int i = 0; valid && i < entries; i++ ) {
			const int fileOffset = static_cast<int>( MVD_ReadLittleUInt( p + 4 + i * 12 + 4 ) );
			valid = std::find( snapshotOffsets.begin(), snapshotOffsets.end(), fileOffset ) != snapshotOffsets.end();
		}
		verdict.indexValid = valid;
	}
	return verdict;
}

static bool RunRecover( const char *argument ) {
	idMultiViewDemo demo;
	idCmdArgs args;
	args.AppendArg( "mvdRecover" );
	args.AppendArg( argument );
	return demo.Recover( args );
}

int main() {
	const Layout layout = WriteSource( false );
	const int headerBytes = static_cast<int>( MVD_HEADER_BYTES );

	// Cut the staged stream at every byte. Recovery keeps exactly the records
	// that ended before the cut, needs one snapshot, and never writes anything
	// when it cannot recover.
	for ( int cut = 0; cut <= static_cast<int>( layout.bytes.size() ); cut++ ) {
		fs.files.clear();
		fs.files["demos/source.mvd.part"] = std::vector<byte>( layout.bytes.begin(), layout.bytes.begin() + cut );
		int expectedSnapshots = 0, expectedReliables = 0, expectedRecords = 0;
		int expectedMarkers = 0, expectedKeyframes = 0;
		for ( size_t i = 0; i < layout.recordEnds.size() && layout.recordEnds[i] <= cut; i++ ) {
			expectedRecords++;
			expectedSnapshots += layout.recordTypes[i] == MVD_RECORD_SNAPSHOT;
			expectedReliables += layout.recordTypes[i] == MVD_RECORD_RELIABLE;
			expectedMarkers += layout.recordTypes[i] == MVD_RECORD_MARKER;
			expectedKeyframes += layout.recordTypes[i] == MVD_RECORD_KEYFRAME;
		}
		const bool recovered = RunRecover( "source.mvd.part" );
		const bool expectRecovered = cut >= headerBytes && expectedSnapshots > 0;
		CHECK( recovered == expectRecovered );
		CHECK( fs.files["demos/source.mvd.part"].size() == static_cast<size_t>( cut ) );
		if ( !recovered ) {
			CHECK( fs.files.count( "demos/source_recovered.mvd" ) == 0 );
			CHECK( fs.files.count( "demos/source_recovered.mvd.part" ) == 0 );
			continue;
		}
		const Verdict verdict = ReadBack( "demos/source_recovered.mvd" );
		CHECK( verdict.readable );
		CHECK( verdict.snapshots == expectedSnapshots );
		CHECK( verdict.reliables == expectedReliables );
		// Every copied record plus the rebuilt index and directories precede the end record.
		CHECK( verdict.records == expectedRecords + 1 + ( expectedMarkers > 0 ) + ( expectedKeyframes > 0 ) );
		CHECK( verdict.endRecords == 1 && verdict.endMatches );
		CHECK( verdict.indexRecords == 1 && verdict.indexValid );
		// The directories list exactly the surviving markers and keyframes, and
		// each keyframe entry names a keyframe record.
		CHECK( verdict.directoriesValid );
		CHECK( verdict.markerTables == ( expectedMarkers > 0 ) && static_cast<int>( verdict.markers.size() ) == expectedMarkers );
		CHECK( verdict.keyframeTables == ( expectedKeyframes > 0 ) && static_cast<int>( verdict.keyframes.size() ) == expectedKeyframes );
		CHECK( static_cast<int>( verdict.keyframeRecordOffsets.size() ) == expectedKeyframes );
		for ( size_t i = 0; i < verdict.keyframes.size() && i < verdict.keyframeRecordOffsets.size(); i++ ) {
			CHECK( verdict.keyframes[i].fileOffset == verdict.keyframeRecordOffsets[i] );
			CHECK( verdict.keyframes[i].sequence == static_cast<int>( ( i + 1 ) * 10 ) );
		}
		CHECK( fs.files.count( "demos/source_recovered.mvd.part" ) == 0 );
	}

	// The default extension is added, and a recovered name never overwrites.
	{
		const Layout whole = WriteSource( false );
		fs.files["demos/source_recovered.mvd"] = { 1, 2, 3 };
		CHECK( RunRecover( "source" ) );
		CHECK( fs.files["demos/source_recovered.mvd"].size() == 3 );
		CHECK( ReadBack( "demos/source_recovered_000.mvd" ).endMatches );
		CHECK( fs.files["demos/source.mvd.part"] == whole.bytes );
	}

	// A finalize that wrote an index and died is recovered with one fresh index.
	{
		const Layout stale = WriteSource( true );
		CHECK( RunRecover( "demos/source.mvd.part" ) );
		const Verdict verdict = ReadBack( "demos/source_recovered.mvd" );
		CHECK( verdict.indexRecords == 1 && verdict.indexValid && verdict.endMatches );
		CHECK( verdict.snapshots == 30 && verdict.reliables == 10 );
		CHECK( verdict.directoriesValid && verdict.markerTables == 1 && verdict.keyframeTables == 1 );
		CHECK( verdict.markers.size() == 3 && verdict.keyframes.size() == 3 );
		CHECK( fs.files["demos/source.mvd.part"] == stale.bytes );
	}

	// A corrupt record ends the salvage at the last intact record before it.
	{
		Layout corrupt = WriteSource( false );
		const int victim = 12;	// a record well inside the stream
		const int start = corrupt.recordEnds[victim - 1];
		corrupt.bytes[start + 30] ^= 0x5a;
		fs.files["demos/source.mvd.part"] = corrupt.bytes;
		CHECK( RunRecover( "source.mvd.part" ) );
		const Verdict verdict = ReadBack( "demos/source_recovered.mvd" );
		int snapshots = 0;
		for ( int i = 0; i < victim; i++ ) {
			snapshots += corrupt.recordTypes[i] == MVD_RECORD_SNAPSHOT;
		}
		CHECK( verdict.endMatches && verdict.indexValid && verdict.snapshots == snapshots );
		CHECK( commonValue.last.find( "checksum" ) != std::string::npos );
	}

	// A damaged header, a missing source, an unsafe path and a failed publish
	// leave nothing behind.
	{
		Layout damaged = WriteSource( false );
		damaged.bytes[20] ^= 0xff;
		fs.files["demos/source.mvd.part"] = damaged.bytes;
		CHECK( !RunRecover( "source.mvd.part" ) );
		CHECK( !RunRecover( "missing.mvd.part" ) );
		CHECK( !RunRecover( "../outside.mvd.part" ) );
		CHECK( !RunRecover( "c:/outside.mvd.part" ) );
		WriteSource( false );
		fs.promoteOK = false;
		CHECK( !RunRecover( "source.mvd.part" ) );
		fs.promoteOK = true;
		for ( const auto &entry : fs.files ) {
			CHECK( entry.first.find( "recovered" ) == std::string::npos );
		}
	}

	// Recovery never borrows a recorder that is busy.
	{
		WriteSource( false );
		idMultiViewDemo busy;
		busy.state = idMultiViewDemo::MVD_RECORDING;
		idCmdArgs args;
		args.AppendArg( "mvdRecover" );
		args.AppendArg( "source.mvd.part" );
		CHECK( !busy.Recover( args ) );
		CHECK( fs.files.count( "demos/source_recovered.mvd" ) == 0 );
	}

	if ( failures != 0 ) {
		std::fprintf( stderr, "mvd recovery fixture: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "mvd recovery fixture: ok (%zu cut points)\n", layout.bytes.size() + 1 );
	return 0;
}
