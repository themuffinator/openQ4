#!/usr/bin/env python3
"""Check the level-load lookup shortcuts in the filesystem search.

Length/timestamp-only reads: idFileSystemLocal::ReadFile with a null buffer
only reports a length and a timestamp. For a pk4 member both are known without
opening it: the length is in the central directory the pak keeps indexed, and
idFile_InZip::Timestamp is always 0. ReadFileContentStamp's probe likewise
answers a member's content stamp from the indexed CRC-32, and must agree with
the stamp a full open reports, or a generated cache written after a decode
would never match the next load's probe. Image staleness checks issue ~1,700 of
these per level load, so the probe must keep every observable side effect of
the old open (search order, pure filtering, the referenced flag, the asset log)
while skipping the member open and the level-load source manifest, which would
otherwise schedule preloads of payloads nobody reads.

Loose search directories: every packed-asset lookup first fails an fopen in
each loose search directory ahead of its pak. During a level load the
existence of each directory on the way is checked once, top-down, and a lookup
under a directory known to be missing skips the open. CreateOSPath must forget
every negative answer, so files written mid-load are always found.

The real ReadFile, OpenFileReadSearch, CreateOSPath and memo bodies are
compiled against stubs.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT_HEAD = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
typedef long long ID_TIME_T;
typedef unsigned char byte;
static const ID_TIME_T FILE_NOT_FOUND_TIMESTAMP = static_cast<ID_TIME_T>( -1 );
/*CONTENT_STAMP_HELPERS*/
#define PATHSEPERATOR_STR "/"
#define PATHSEPERATOR_CHAR '/'
#define FILE_HASH_SIZE 1024
#define FSFLAG_SEARCH_DIRS ( 1 << 0 )
#define FSFLAG_SEARCH_PAKS ( 1 << 1 )
#define FSFLAG_PURE_NOREF ( 1 << 2 )
#define FSFLAG_BINARY_ONLY ( 1 << 3 )
#define FSFLAG_SEARCH_ADDONS ( 1 << 4 )
#define BINARY_CONFIG "binary.conf"
enum { FS_READ = 0 };
enum { PURE_UNKNOWN = 0, PURE_ALWAYS, PURE_NEVER };
enum { BINARY_UNKNOWN = 0, BINARY_YES, BINARY_NO };

class idStr {
public:
	idStr() {}
	idStr( const char *text ) : s( text ? text : "" ) {}
	idStr &operator=( const char *text ) { s = text ? text : ""; return *this; }
	idStr &operator+=( const char *text ) { s += text; return *this; }
	idStr &operator+=( const idStr &text ) { s += text.s; return *this; }
	bool operator!=( const char *text ) const { return s != text; }
	char &operator[]( int index ) { return s[ static_cast<size_t>( index ) ]; }
	operator const char *() const { return s.c_str(); }
	const char *c_str() const { return s.c_str(); }
	int Length() const { return static_cast<int>( strlen( s.c_str() ) ); }
	int Cmp( const char *text ) const { return s.compare( text ); }
	int Icmp( const char *text ) const { return Icmp( s.c_str(), text ); }
	static int Icmp( const char *a, const char *b ) {
		for ( ;; ++a, ++b ) {
			const int ca = tolower( static_cast<unsigned char>( *a ) );
			const int cb = tolower( static_cast<unsigned char>( *b ) );
			if ( ca != cb || ca == 0 ) { return ca - cb; }
		}
	}
	static int Icmpn( const char *a, const char *b, int n ) {
		for ( int i = 0; i < n; ++i ) {
			const int ca = tolower( static_cast<unsigned char>( a[i] ) );
			const int cb = tolower( static_cast<unsigned char>( b[i] ) );
			if ( ca != cb || ca == 0 ) { return ca - cb; }
		}
		return 0;
	}
	void ExtractFileName( idStr &out ) const {
		const size_t slash = s.find_last_of( '/' );
		out.s = slash == std::string::npos ? s : s.substr( slash + 1 );
	}
	void StripFilename() {
		const size_t slash = s.find_last_of( '/' );
		s = slash == std::string::npos ? std::string() : s.substr( 0, slash );
	}
	std::string s;
};

struct idCVarStub {
	int value = 0;
	const char *text = "";
	bool GetBool() const { return value != 0; }
	int GetInteger() const { return value; }
	const char *GetString() const { return text; }
};
static idCVarStub fs_restrict, fs_debug, fs_copyfiles, fs_savepath, fs_cdpath, fs_basepath;
static idCVarStub fs_caseSensitiveOS, fs_cacheLooseDirectories{ 1 };

struct CommonStub {
	void FatalError( const char *, ... ) { throw std::runtime_error( "FatalError" ); }
	void Printf( const char *, ... ) {}
	void DPrintf( const char *, ... ) {}
	void DWarning( const char *, ... ) {}
	void Warning( const char *, ... ) {}
};
static CommonStub commonStub;
static CommonStub *common = &commonStub;

struct JournalFileStub {
	int Read( void *, int ) { return 0; }
	int Write( const void *, int ) { return 0; }
	void Flush() {}
};
struct EventLoopStub {
	JournalFileStub journal;
	JournalFileStub *com_journalDataFile = &journal;
	int JournalLevel() const { return 0; }
};
static EventLoopStub *eventLoop = nullptr;

static void *Mem_ClearedAlloc( size_t bytes ) { return calloc( bytes, 1 ); }

class idFile {
public:
	static int live;
	idFile() { ++live; }
	virtual ~idFile() { --live; }
	virtual int Length() { return fileSize; }
	virtual ID_TIME_T Timestamp() { return 0; }
	virtual ID_TIME_T ContentStamp() { return Timestamp(); }
	virtual int Read( void *buffer, int len ) { memset( buffer, 'x', static_cast<size_t>( len ) ); return len; }
	int fileSize = 0;
};
int idFile::live = 0;

class idFile_Permanent : public idFile {
public:
	ID_TIME_T Timestamp() override { return 1234; }
	FILE *o = nullptr;
	idStr name, fullPath;
	int mode = 0;
};
class idFile_InZip : public idFile {
public:
	ID_TIME_T ContentStamp() override;
	uint32_t contentCrc = 0;
};

static ID_TIME_T Sys_FileTimeStamp( FILE * ) { return 0; }

// The OS directory tree the memo observes: lower-case-insensitive like Windows.
static std::set<std::string> existingDirectories;
static int directoryStats = 0;
static std::string FoldPath( const char *path ) {
	std::string folded;
	for ( const char *c = path; *c; ++c ) {
		folded += ( *c == '\\' ) ? '/' : static_cast<char>( tolower( static_cast<unsigned char>( *c ) ) );
	}
	return folded;
}
static bool FS_IsOSDirectory( const char *osPath ) {
	++directoryStats;
	return existingDirectories.count( FoldPath( osPath ) ) != 0;
}
static void Sys_Mkdir( const char *path ) { existingDirectories.insert( FoldPath( path ) ); }
static bool FS_HasParentOSPathSegment( const char *path ) { return strstr( path, ".." ) != nullptr; }
'''

SUPPORT_TAIL = r'''
struct directory_t { idStr path, gamedir; };
struct pack_t {
	idStr pakFilename;
	fileInPack_t *hashTable[FILE_HASH_SIZE] = {};
	bool referenced = false;
	int pureStatus = PURE_ALWAYS;
	int binary = BINARY_UNKNOWN;
};
struct searchpath_t { pack_t *pack = nullptr; directory_t *dir = nullptr; searchpath_t *next = nullptr; };

template< class T > struct ListStub {
	std::vector<T> items;
	int Num() const { return static_cast<int>( items.size() ); }
	const T *Find( const T &value ) const {
		for ( const T &item : items ) { if ( item == value ) { return &item; } }
		return nullptr;
	}
};

static int HashFileName( const char * ) { return 7; }
static int FilenameCompare( const idStr &a, const char *b ) { return idStr::Icmp( a.c_str(), b ); }

class idFileSystemLocal {
public:
	searchpath_t *searchPaths = nullptr;
	searchpath_t *addonPaks = nullptr;
	ListStub<pack_t *> serverPaks;
	ListStub<int> restartChecksums;
	idStr gameFolder = "baseoq4";
	bool loadedFileFromDir = false;
	bool loadedImageCacheFromDir = false;
	int loadCount = 0, loadStack = 0;
	std::vector<std::string> looseFiles;		// relative names a directory search path can open
	std::vector<std::string> assetLog;
	int zipOpens = 0, osOpens = 0, osOpenAttempts = 0, recordedSources = 0;
	std::mutex looseDirectoryLock;
	std::unordered_map<std::string, bool> looseDirectoryExists;
	std::atomic<bool> looseDirectoryCacheActive{ false };
	int looseDirectoryChecks = 0;
	int looseDirectorySkips = 0;

	int ReadFile( const char *relativePath, void **buffer, ID_TIME_T *timestamp );
	int ReadFileContentStamp( const char *relativePath, void **buffer, ID_TIME_T *contentStamp );
	int ReadFileStamped( const char *relativePath, void **buffer, ID_TIME_T *timestamp, bool contentStamp );
	idFile *OpenFileReadSearch( const char *relativePath, int searchFlags, pack_t **foundInPak, bool allowCopyFiles, const char *gamedir, packMemberProbe_t *probe );
	void SetLooseDirectoryCacheActive( bool active );
	bool LooseDirectoryMissing( const directory_t *dir, const char *relativePath );
	bool LooseDirectoryMissingLocked( const std::string &key, const std::string &osPath );
	void CreateOSPath( const char *OSPath );
	idFile *OpenFileRead( const char *relativePath, bool allowCopyFiles ) {
		return OpenFileReadSearch( relativePath, FSFLAG_SEARCH_DIRS | FSFLAG_SEARCH_PAKS, nullptr, allowCopyFiles, nullptr, nullptr );
	}
	void CloseFile( idFile *f ) { delete f; }
	idStr BuildOSPath( const idStr &, const idStr &, const char *relativePath ) { return idStr( relativePath ); }
	FILE *OpenOSFileCorrectName( idStr &path, const char * ) {
		++osOpenAttempts;
		for ( const std::string &name : looseFiles ) {
			// any real stream will do: the stubs never read or close it
			if ( name == path.s ) { ++osOpens; return stdin; }
		}
		return nullptr;
	}
	FILE *OpenOSFile( const idStr &, const char * ) { return nullptr; }
	int DirectFileLength( FILE * ) { return 99; }
	bool FileAllowedFromDir( const char * ) { return true; }
	void GetPackStatus( pack_t * ) {}
	bool IsGameDirPack( const pack_t *, const char * ) { return false; }
	void CopyFile( const idStr &, const idStr & ) {}
	idFile_InZip *ReadFileFromZip( pack_t *, fileInPack_t *pakFile, const char * ) {
		++zipOpens;
		idFile_InZip *file = new idFile_InZip();
		file->fileSize = static_cast<int>( pakFile->size );
		file->contentCrc = pakFile->crc;
		return file;
	}
	void AddAssetLogEntry( const char *relativePath ) { assetLog.push_back( relativePath ); }
	void RecordOpenedLevelLoadSource( const char *, idFile * ) { ++recordedSources; }
	idFile *UsePreloadedLevelLoadSource( const char *, idFile *file ) { return file; }
};
'''

MAIN = r'''
struct Fixture {
	fileInPack_t member;
	pack_t pak;
	searchpath_t dirPath, pakPath;
	directory_t dir;
	idFileSystemLocal fs;
	Fixture() {
		member.name = "textures/base/wall.tga";
		member.pos = 4096;
		member.size = 70000;
		member.crc = 0x1234abcdu;
		member.next = nullptr;
		pak.pakFilename = "pak001.pk4";
		pak.hashTable[7] = &member;
		dir.path = "C:/Game/";
		dir.gamedir = "baseoq4";
		dirPath.dir = &dir;
		dirPath.next = &pakPath;
		pakPath.pack = &pak;
		fs.searchPaths = &dirPath;
		existingDirectories.clear();
		directoryStats = 0;
	}
};

static void LengthProbes() {
	{
		// a packed member: length from the index, timestamp 0, nothing opened
		Fixture f;
		ID_TIME_T timestamp = 55;
		assert( f.fs.ReadFile( "textures/base/wall.tga", nullptr, &timestamp ) == 70000 );
		assert( timestamp == 0 );
		assert( f.fs.zipOpens == 0 && f.fs.osOpens == 0 && idFile::live == 0 );
		// the old open's side effects survive, except the preload manifest
		assert( f.pak.referenced );
		assert( f.fs.assetLog.size() == 1 && f.fs.assetLog[0] == "textures/base/wall.tga" );
		assert( f.fs.recordedSources == 0 );
		// a leading slash and a null timestamp are accepted as before
		assert( f.fs.ReadFile( "/textures/base/wall.tga", nullptr, nullptr ) == 70000 );
		assert( f.fs.zipOpens == 0 && idFile::live == 0 );
		// the content stamp comes from the indexed CRC, still without an open
		ID_TIME_T stamp = 55;
		assert( f.fs.ReadFileContentStamp( "textures/base/wall.tga", nullptr, &stamp ) == 70000 );
		assert( stamp == FS_PakMemberContentStamp( 0x1234abcdu, 70000 ) );
		assert( FS_IsPakMemberContentStamp( stamp ) );
		assert( f.fs.zipOpens == 0 && idFile::live == 0 );
		// and changes with the member's CRC under the same name and length
		f.member.crc = 0x1234abceu;
		ID_TIME_T changed = 55;
		assert( f.fs.ReadFileContentStamp( "textures/base/wall.tga", nullptr, &changed ) == 70000 );
		assert( changed != stamp && FS_IsPakMemberContentStamp( changed ) );
		// ReadFile's timestamp is untouched by the content stamp
		assert( f.fs.ReadFile( "textures/base/wall.tga", nullptr, &timestamp ) == 70000 && timestamp == 0 );
	}
	{
		// a payload read still opens the member and records it for preload
		Fixture f;
		void *buffer = nullptr;
		ID_TIME_T timestamp = 55;
		assert( f.fs.ReadFile( "textures/base/wall.tga", &buffer, &timestamp ) == 70000 );
		assert( buffer != nullptr && timestamp == 0 );
		assert( f.fs.zipOpens == 1 && f.fs.recordedSources == 1 && idFile::live == 0 );
		free( buffer );
		// a full open reports the content stamp the probe does
		buffer = nullptr;
		ID_TIME_T probed = 55, opened = 55;
		assert( f.fs.ReadFileContentStamp( "textures/base/wall.tga", nullptr, &probed ) == 70000 );
		assert( f.fs.ReadFileContentStamp( "textures/base/wall.tga", &buffer, &opened ) == 70000 );
		assert( buffer != nullptr && opened == probed && FS_IsPakMemberContentStamp( opened ) );
		assert( f.fs.zipOpens == 2 && idFile::live == 0 );
		free( buffer );
	}
	{
		// a loose file earlier in the search order still wins, and is opened
		Fixture f;
		f.fs.looseFiles.push_back( "textures/base/wall.tga" );
		ID_TIME_T timestamp = 55;
		assert( f.fs.ReadFile( "textures/base/wall.tga", nullptr, &timestamp ) == 99 );
		assert( timestamp == 1234 );
		assert( f.fs.osOpens == 1 && f.fs.zipOpens == 0 && idFile::live == 0 );
		assert( !f.pak.referenced );
		// a loose file's content stamp is its timestamp
		ID_TIME_T stamp = 55;
		assert( f.fs.ReadFileContentStamp( "textures/base/wall.tga", nullptr, &stamp ) == 99 );
		assert( stamp == 1234 && f.fs.osOpens == 2 && idFile::live == 0 );
	}
	{
		// a pak excluded by the pure list is not answered from its index
		Fixture f;
		pack_t other;
		f.fs.serverPaks.items.push_back( &other );
		ID_TIME_T timestamp = 55;
		assert( f.fs.ReadFile( "textures/base/wall.tga", nullptr, &timestamp ) == -1 );
		assert( timestamp == FILE_NOT_FOUND_TIMESTAMP );
		assert( !f.pak.referenced && f.fs.assetLog.empty() && idFile::live == 0 );
	}
	{
		// a missing file reports -1 and the not-found timestamp
		Fixture f;
		ID_TIME_T timestamp = 55;
		assert( f.fs.ReadFile( "textures/base/missing.tga", nullptr, &timestamp ) == -1 );
		assert( timestamp == FILE_NOT_FOUND_TIMESTAMP && idFile::live == 0 );
		assert( f.fs.ReadFile( "../escape.tga", nullptr, &timestamp ) == -1 );
	}
}

static void LooseDirectoryMemo() {
	{
		// outside a level load every lookup still tries the open
		Fixture f;
		existingDirectories.insert( "c:/game/baseoq4" );
		assert( !f.fs.LooseDirectoryMissing( &f.dir, "textures/base/wall.tga" ) );
		assert( directoryStats == 0 );
		void *buffer = nullptr;
		assert( f.fs.ReadFile( "textures/base/wall.tga", &buffer, nullptr ) == 70000 );
		assert( f.fs.osOpenAttempts == 1 );
		free( buffer );
	}
	{
		// in a level load a missing directory is checked once and then skipped
		Fixture f;
		existingDirectories.insert( "c:/game/baseoq4" );
		f.fs.SetLooseDirectoryCacheActive( true );
		void *buffer = nullptr;
		assert( f.fs.ReadFile( "textures/base/wall.tga", &buffer, nullptr ) == 70000 );
		free( buffer );
		assert( f.fs.osOpenAttempts == 0 );
		const int statsAfterFirst = directoryStats;
		assert( statsAfterFirst == 2 );	// the root, then textures/
		// other files under the same missing directory need no new check, in any case
		assert( f.fs.LooseDirectoryMissing( &f.dir, "Textures/Other/x.tga" ) );
		assert( f.fs.LooseDirectoryMissing( &f.dir, "textures\\deep\\er\\y.tga" ) );
		assert( directoryStats == statsAfterFirst );
		assert( f.fs.looseDirectorySkips == 3 && f.fs.looseDirectoryChecks == 2 );
		// a file directly in the root needs only the root
		assert( !f.fs.LooseDirectoryMissing( &f.dir, "autoexec.cfg" ) );
		assert( directoryStats == statsAfterFirst );
		f.fs.SetLooseDirectoryCacheActive( false );
		assert( f.fs.looseDirectoryExists.empty() && !f.fs.looseDirectoryCacheActive );
	}
	{
		// an existing chain still opens, and is checked only once per directory
		Fixture f;
		existingDirectories.insert( "c:/game/baseoq4" );
		existingDirectories.insert( "c:/game/baseoq4/textures" );
		existingDirectories.insert( "c:/game/baseoq4/textures/base" );
		f.fs.looseFiles.push_back( "textures/base/wall.tga" );
		f.fs.SetLooseDirectoryCacheActive( true );
		assert( f.fs.ReadFile( "textures/base/wall.tga", nullptr, nullptr ) == 99 );
		assert( f.fs.ReadFile( "textures/base/wall.tga", nullptr, nullptr ) == 99 );
		assert( f.fs.osOpens == 2 && directoryStats == 3 );
	}
	{
		// a missing search root skips everything beneath it
		Fixture f;
		f.fs.SetLooseDirectoryCacheActive( true );
		assert( f.fs.LooseDirectoryMissing( &f.dir, "a/b.tga" ) );
		assert( f.fs.LooseDirectoryMissing( &f.dir, "c.tga" ) );
		assert( directoryStats == 1 );
	}
	{
		// a directory the engine creates mid-load is seen by the next lookup
		Fixture f;
		existingDirectories.insert( "c:/game/baseoq4" );
		f.fs.SetLooseDirectoryCacheActive( true );
		assert( f.fs.LooseDirectoryMissing( &f.dir, "generated/images/a.bimage" ) );
		f.fs.CreateOSPath( "C:/Game/baseoq4/generated/images/a.bimage" );
		assert( existingDirectories.count( "c:/game/baseoq4/generated/images" ) == 1 );
		assert( !f.fs.LooseDirectoryMissing( &f.dir, "generated/images/a.bimage" ) );
		// positive answers survive the invalidation
		assert( f.fs.looseDirectoryExists.at( "c:/game/baseoq4" ) );
	}
	{
		// opted out, or on a case-sensitive filesystem, the memo stays off
		Fixture f;
		fs_cacheLooseDirectories.value = 0;
		f.fs.SetLooseDirectoryCacheActive( true );
		assert( !f.fs.looseDirectoryCacheActive && !f.fs.LooseDirectoryMissing( &f.dir, "a/b.tga" ) );
		fs_cacheLooseDirectories.value = 1;
		fs_caseSensitiveOS.value = 1;
		f.fs.SetLooseDirectoryCacheActive( true );
		assert( !f.fs.looseDirectoryCacheActive && directoryStats == 0 );
		fs_caseSensitiveOS.value = 0;
	}
}

int main() {
	LengthProbes();
	LooseDirectoryMemo();
	puts( "filesystem_length_probe_contract: PASS (pk4 lengths from the index, loose-directory memo)" );
	return 0;
}
'''


def type_definition(source: str, name: str) -> str:
    match = re.search(r"typedef struct " + name + r"_s \{.*?\} " + name + r"_t;", source, re.S)
    if match is None:
        raise AssertionError(f"Missing typedef {name}_t")
    return match.group(0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "src/framework/FileSystem.cpp")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    header = (ROOT / "src/framework/FileSystem.h").read_text(encoding="utf-8")
    stamp_start = header.index("inline ID_TIME_T FS_FoldContentIdentity(")
    combine = function_body(header, "inline ID_TIME_T FS_CombineContentStamps(")
    stamp_helpers = header[stamp_start:header.index(combine) + len(combine)]
    file_source = (ROOT / "src/framework/File.cpp").read_text(encoding="utf-8")
    zip_stamp = function_body(file_source, "ID_TIME_T idFile_InZip::ContentStamp(")
    types = "\n".join(type_definition(source, name) for name in ("fileInPack", "packMemberProbe"))
    helpers = "\n".join(function_body(source, signature) for signature in (
        "static void FS_AppendLooseDirectoryKey(",
        "static bool FS_IsGeneratedImageCachePath(",
        "static bool FS_IsGeneratedLoadscreenPath(",
    ))
    bodies = "\n".join(function_body(source, signature) for signature in (
        "int idFileSystemLocal::ReadFile(",
        "int idFileSystemLocal::ReadFileContentStamp(",
        "int idFileSystemLocal::ReadFileStamped(",
        "idFile *idFileSystemLocal::OpenFileReadSearch(",
        "void idFileSystemLocal::SetLooseDirectoryCacheActive(",
        "bool idFileSystemLocal::LooseDirectoryMissingLocked(",
        "bool idFileSystemLocal::LooseDirectoryMissing(",
        "void idFileSystemLocal::CreateOSPath(",
    ))
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
             "-Wno-unused-variable", "-Wno-unused-but-set-variable"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="length-probe-", dir=ROOT / ".tmp") as directory:
        program, binary = Path(directory) / "probe.cpp", Path(directory) / "probe.exe"
        head = SUPPORT_HEAD.replace("/*CONTENT_STAMP_HELPERS*/", stamp_helpers)
        program.write_text(head + types + SUPPORT_TAIL + zip_stamp + "\n" + helpers + "\n" + bodies + MAIN,
                           encoding="utf-8")
        subprocess.run([compiler, *flags, str(program), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
