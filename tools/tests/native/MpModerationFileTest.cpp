// Native test of src/mpgame/mp/ModerationFile.h (MP-18): banlist.txt read and
// written through a stand-in for the engine's file system. The header needs only
// a handful of engine types; the stand-ins below give them the behaviour the
// reader depends on, and let a test cut a read short or fill the disk.
// Built and run by tools/tests/mp_moderation.py.

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK( condition ) \
	do { \
		if ( !( condition ) ) { \
			std::printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition ); \
			failures++; \
		} \
	} while ( 0 )

//----------------------------------------------------------------
// stand-ins for the engine
//----------------------------------------------------------------

struct idStr {
	std::string s;
	idStr() {}
	idStr( const char *text ) : s( text != 0 ? text : "" ) {}
	void Fill( char c, int n ) { s.assign( static_cast<size_t>( n ), c ); }
	char &operator[]( int i ) { return s[ static_cast<size_t>( i ) ]; }
	const char *c_str() const { return s.c_str(); }
	void Append( const char *text, int n ) { s.append( text, static_cast<size_t>( n ) ); }
	static int Cmp( const char *a, const char *b ) { return std::strcmp( a, b ); }
	static int Cmpn( const char *a, const char *b, int n ) { return std::strncmp( a, b, static_cast<size_t>( n ) ); }
};

struct idStrList {
	std::vector<idStr> lines;
	void Clear() { lines.clear(); }
	int Num() const { return static_cast<int>( lines.size() ); }
	void Append( const idStr &line ) { lines.push_back( line ); }
	const idStr &operator[]( int i ) const { return lines[ static_cast<size_t>( i ) ]; }
};

static const char *va( const char *fmt, ... ) {
	static char buffer[ 4096 ];
	va_list args;
	va_start( args, fmt );
	std::vsnprintf( buffer, sizeof( buffer ), fmt, args );
	va_end( args );
	return buffer;
}

struct MockFileSystem;

struct idFile {
	MockFileSystem *owner;
	std::string		name;		// what is being written
	std::string		data;		// what is being read
	size_t			at;
	int Length();
	int Read( void *buffer, int len );
	int Printf( const char *fmt, ... );
};

struct MockFileSystem {
	std::map<std::string, std::string> files;
	bool	shortRead;		// a read returns one byte less than was asked for
	bool	diskFull;		// writes report success and keep nothing past the first 20 bytes
	bool	promoteFails;
	int		promotes;

	MockFileSystem() : shortRead( false ), diskFull( false ), promoteFails( false ), promotes( 0 ) {}

	const char *RelativePathToOSPath( const char *relative, const char * ) {
		static std::string path;
		path = std::string( "save/" ) + relative;
		return path.c_str();
	}
	idFile *OpenExplicitFileRead( const char *osPath ) {
		const std::string relative = std::string( osPath ).substr( 5 );
		if ( files.find( relative ) == files.end() ) {
			return 0;
		}
		idFile *file = new idFile;
		file->owner = this;
		file->data = files[ relative ];
		file->at = 0;
		return file;
	}
	idFile *OpenFileWrite( const char *relative, const char * ) {
		files[ relative ] = "";
		idFile *file = new idFile;
		file->owner = this;
		file->name = relative;
		file->at = 0;
		return file;
	}
	void CloseFile( idFile *file ) { delete file; }
	bool PromoteFile( const char *from, const char *to, const char * ) {
		if ( promoteFails || files.find( from ) == files.end() ) {
			return false;
		}
		files[ to ] = files[ from ];
		files.erase( from );
		promotes++;
		return true;
	}
	void RemoveFileChecked( const char *relative, const char * ) { files.erase( relative ); }
};

int idFile::Length() {
	return static_cast<int>( data.size() );
}

int idFile::Read( void *buffer, int len ) {
	size_t count = static_cast<size_t>( len );
	if ( count > data.size() - at ) {
		count = data.size() - at;
	}
	if ( owner->shortRead && count > 0 ) {
		count--;
	}
	std::memcpy( buffer, data.data() + at, count );
	at += count;
	return static_cast<int>( count );
}

// As idFile::Printf does: "\n" becomes "\r\n", and the count is what a buffered
// write reports, which on a full disk is all of it.
int idFile::Printf( const char *fmt, ... ) {
	char buffer[ 4096 ];
	va_list args;
	va_start( args, fmt );
	std::vsnprintf( buffer, sizeof( buffer ), fmt, args );
	va_end( args );
	std::string text;
	for ( const char *c = buffer; *c != '\0'; c++ ) {
		if ( *c == '\n' ) {
			text += '\r';
		}
		text += *c;
	}
	std::string &target = owner->files[ name ];
	if ( !owner->diskFull || target.size() < 20 ) {
		target += text;
	}
	return static_cast<int>( text.size() );
}

static MockFileSystem mockFileSystem;
static MockFileSystem *fileSystem = &mockFileSystem;

#include "../../../src/mpgame/mp/ModerationFile.h"

//----------------------------------------------------------------

static mpModBanList_t list;			// 120 KB: not on the stack
static idStrList keptLines;
static mpModBanFileResult_t result;

static void Load( const std::string &content ) {
	mockFileSystem = MockFileSystem();
	mockFileSystem.files[ MP_MOD_BANLIST_FILENAME ] = content;
	MPModLoadBanFile( list, keptLines, result );
}

static std::string File() {
	return mockFileSystem.files[ MP_MOD_BANLIST_FILENAME ];
}

static std::string BanLines( int count, int first = 0 ) {
	std::string text;
	for ( int i = first; i < first + count; i++ ) {
		char line[ 128 ];
		std::snprintf( line, sizeof( line ), "ban address 10.%d.%d.%d 1795000000 0 \"n%d\" \"\"\n", ( i >> 16 ) & 255, ( i >> 8 ) & 255, i & 255, i );
		text += line;
	}
	return text;
}

static void TestRead( void ) {
	// no file is an empty list that may be saved
	mockFileSystem = MockFileSystem();
	MPModLoadBanFile( list, keptLines, result );
	CHECK( !result.found && !result.unreadable && list.count == 0 && !MPModBanFileBlocked() );
	CHECK( MPModSaveBanFile( list, keptLines ) && mockFileSystem.promotes == 1 );
	CHECK( File().compare( 0, std::strlen( MP_MOD_BANLIST_HEADER ) + 2, std::string( MP_MOD_BANLIST_HEADER ) + "\r\n" ) == 0 );
	CHECK( mockFileSystem.files.count( MP_MOD_BANLIST_STAGED ) == 0 );

	// bans, a comment and a line of a kind this build does not know
	Load( std::string( MP_MOD_BANLIST_HEADER ) + "\n#: help\n# a note\nban identity abc 0 0 \"x\" \"\"\n" + BanLines( 2 ) );
	CHECK( result.found && !result.unreadable && result.bans == 2 && list.count == 2 && result.kept == 1 && keptLines.Num() == 2 );
	CHECK( !result.rewrite && !MPModBanFileBlocked() );
	// what it does not understand is written back as it was
	CHECK( MPModSaveBanFile( list, keptLines ) );
	CHECK( File().find( "# a note\r\n" ) != std::string::npos && File().find( "ban identity abc 0 0 \"x\" \"\"\r\n" ) != std::string::npos );
	const std::string saved = File();
	Load( saved );
	CHECK( !result.unreadable && result.bans == 2 && keptLines.Num() == 2 );

	// a tab after the first word is still a ban, not a file of another kind
	Load( "ban\taddress 192.0.2.7 0 0 \"t\" \"\"\n" );
	CHECK( !result.unreadable && !result.rewrite && result.bans == 1 && File() == "ban\taddress 192.0.2.7 0 0 \"t\" \"\"\n" );
	// any first line: a stray word is kept, and the bans after it are read
	Load( "hello\n" + BanLines( 3 ) );
	CHECK( !result.unreadable && !result.rewrite && result.bans == 3 && result.kept == 1 );
	// line ends of every kind
	Load( "ban address 192.0.2.1 0 0 \"a\" \"\"\rban address 192.0.2.2 0 0 \"b\" \"\"\r" );
	CHECK( !result.unreadable && result.bans == 2 );
	Load( "ban address 192.0.2.1 0 0 \"a\" \"\"\r\nban address 192.0.2.2 0 0 \"b\" \"\"\r\n" );
	CHECK( !result.unreadable && result.bans == 2 && keptLines.Num() == 0 );
	// an editor's byte order mark
	Load( "\xef\xbb\xbf" "ban address 192.0.2.1 0 0 \"a\" \"\"\n" );
	CHECK( !result.unreadable && result.bans == 1 );
	// the same range twice is one ban, the later line
	Load( "ban address 192.0.2.1 0 0 \"first\" \"\"\nban address 192.0.2.1 0 0 \"second\" \"\"\n" );
	CHECK( list.count == 1 && std::string( list.bans[ 0 ].name ) == "second" );
	// an empty file
	Load( "" );
	CHECK( result.found && !result.unreadable && list.count == 0 && !MPModBanFileBlocked() );
}

static void TestUnusable( void ) {
	// More bans than the list holds: the first are enforced, and the file is not written over.
	const std::string many = BanLines( MP_MOD_MAX_BANS + 7 );
	Load( many );
	CHECK( result.unreadable && result.overflow == 7 && list.count == MP_MOD_MAX_BANS && MPModBanFileBlocked() );
	CHECK( !MPModSaveBanFile( list, keptLines ) && File() == many && mockFileSystem.promotes == 0 );
	CHECK( mockFileSystem.files.count( MP_MOD_BANLIST_STAGED ) == 0 );

	// more lines that are not bans than can be carried back
	std::string notes;
	for ( int i = 0; i < MP_MOD_BANLIST_MAX_KEPT_LINES + 1; i++ ) {
		notes += "# note\n";
	}
	Load( notes + BanLines( 1 ) );
	CHECK( result.unreadable && list.count == 1 && MPModBanFileBlocked() && !MPModSaveBanFile( list, keptLines ) );

	// a line too long to be written back as it was
	Load( "# " + std::string( 600, 'x' ) + "\n" + BanLines( 1 ) );
	CHECK( result.unreadable && list.count == 1 && MPModBanFileBlocked() );

	// too large to read
	const std::string huge( static_cast<size_t>( MP_MOD_BANLIST_MAX_BYTES ) + 1, '#' );
	Load( huge );
	CHECK( result.found && result.unreadable && list.count == 0 && MPModBanFileBlocked() );
	CHECK( !MPModSaveBanFile( list, keptLines ) && File().size() == huge.size() );

	// cut short while reading
	mockFileSystem = MockFileSystem();
	mockFileSystem.files[ MP_MOD_BANLIST_FILENAME ] = BanLines( 2 );
	mockFileSystem.shortRead = true;
	MPModLoadBanFile( list, keptLines, result );
	CHECK( result.found && result.unreadable && list.count == 0 && MPModBanFileBlocked() );
	mockFileSystem.shortRead = false;
	CHECK( !MPModSaveBanFile( list, keptLines ) && File() == BanLines( 2 ) );

	// text an editor saved as UTF-16 is neither form: left alone, no copy made of it
	std::string wide;
	for ( const char *c = "ban address 192.0.2.7 0 0 \"a\" \"\"\n"; *c != '\0'; c++ ) {
		wide += *c;
		wide += '\0';
	}
	Load( wide );
	CHECK( result.unreadable && !result.rewrite && list.count == 0 && MPModBanFileBlocked() );
	CHECK( File() == wide && mockFileSystem.files.size() == 1 );

	// a load that works lifts the refusal
	Load( BanLines( 2 ) );
	CHECK( !result.unreadable && !MPModBanFileBlocked() && MPModSaveBanFile( list, keptLines ) );
}

static std::string Quake4Entry( const char *name, const char *guid ) {
	std::string data;
	for ( const char *field : { name, guid } ) {
		const unsigned int size = static_cast<unsigned int>( std::strlen( field ) );
		data += static_cast<char>( size & 255 );
		data += static_cast<char>( ( size >> 8 ) & 255 );
		data += static_cast<char>( ( size >> 16 ) & 255 );
		data += static_cast<char>( ( size >> 24 ) & 255 );
		data += field;
	}
	return data;
}

static void TestQuake4( void ) {
	Load( Quake4Entry( "OldCheater", "ABCDEFGHIJK" ) + Quake4Entry( "#hash", "LMNOP" ) );
	CHECK( !result.unreadable && result.legacy == 2 && result.rewrite && list.count == 0 && keptLines.Num() == 2 && !MPModBanFileBlocked() );
	CHECK( std::string( keptLines[ 0 ].c_str() ) == "# Quake 4 GUID ban, never enforced (clients send no GUID): \"OldCheater\" ABCDEFGHIJK" );
	CHECK( MPModSaveBanFile( list, keptLines ) );
	const std::string text = File();
	Load( text );
	CHECK( !result.unreadable && !result.rewrite && result.legacy == 0 && keptLines.Num() == 2 );
	// a Quake 4 list cut short is neither form
	const std::string cut = Quake4Entry( "OldCheater", "ABCDEFGHIJK" ).substr( 0, 12 );
	Load( cut );
	CHECK( result.unreadable && !result.rewrite && MPModBanFileBlocked() && File() == cut );
}

static void TestWrite( void ) {
	// A full disk: the write reports success and the staged file is short. The list in place stays.
	const std::string good = BanLines( 3 );
	Load( good );
	mockFileSystem.diskFull = true;
	CHECK( !MPModSaveBanFile( list, keptLines ) && File() == good && mockFileSystem.promotes == 0 );
	CHECK( mockFileSystem.files.count( MP_MOD_BANLIST_STAGED ) == 0 );
	mockFileSystem.diskFull = false;
	// the swap itself failing leaves the list in place and no staged file behind
	mockFileSystem.promoteFails = true;
	CHECK( !MPModSaveBanFile( list, keptLines ) && File() == good && mockFileSystem.files.count( MP_MOD_BANLIST_STAGED ) == 0 );
	mockFileSystem.promoteFails = false;
	CHECK( MPModSaveBanFile( list, keptLines ) && mockFileSystem.promotes == 1 );
	// what was written reads back as the same bans
	const std::string written = File();
	Load( written );
	CHECK( !result.unreadable && result.bans == 3 && keptLines.Num() == 0 );
	// a name and a reason with quotes, a backslash and characters past ASCII
	mockFileSystem = MockFileSystem();
	std::memset( &list, 0, sizeof( list ) );
	keptLines.Clear();
	mpModAddress_t address;
	int prefix = 0;
	CHECK( MPModParseAddress( "203.0.113.0/24", address, prefix ) );
	CHECK( MPModBanAdd( list, address, prefix, 1795000000, 600, "Qu\"ote\\d \xe2\x82\xac", "said \"hi\" 3rd time" ) == 0 );
	CHECK( MPModSaveBanFile( list, keptLines ) );
	Load( File() );
	CHECK( list.count == 1 && std::string( list.bans[ 0 ].name ) == "Qu\"ote\\d \xe2\x82\xac" && std::string( list.bans[ 0 ].reason ) == "said \"hi\" 3rd time" );
	CHECK( list.bans[ 0 ].expires == 1795000600 && list.bans[ 0 ].prefixBits == 24 );
}

int main( void ) {
	TestRead();
	TestUnusable();
	TestQuake4();
	TestWrite();
	if ( failures != 0 ) {
		std::printf( "%d ban file check(s) failed\n", failures );
		return 1;
	}
	std::printf( "ban file: all checks passed\n" );
	return 0;
}
