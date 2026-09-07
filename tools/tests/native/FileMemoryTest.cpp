#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

// Minimal engine services; the generated include contains the actual class
// declaration and every memory-file method from File.h / File.cpp.
using idStr = std::string;
using ID_TIME_T = std::int64_t;
enum { FS_READ, FS_WRITE };
enum fsOrigin_t { FS_SEEK_CUR, FS_SEEK_END, FS_SEEK_SET };
class idFile {
public:
	virtual ~idFile() = default;
	virtual int Seek( long offset, fsOrigin_t origin ) = 0;
	void Rewind() { Seek( 0, FS_SEEK_SET ); }
};
template<typename T> T Min( T a, T b ) { return std::min( a, b ); }
template<typename T> T Max( T a, T b ) { return std::max( a, b ); }
#ifdef INT_MAX
#undef INT_MAX
#endif
struct idMath { static constexpr int INT_MAX = std::numeric_limits<int>::max(); };
struct TestError : std::runtime_error { using std::runtime_error::runtime_error; };
struct TestCommon {
	[[noreturn]] void Error( const char *message, ... ) { throw TestError( message ); }
	[[noreturn]] void FatalError( const char *message, ... ) { throw TestError( message ); }
};
static TestCommon testCommon;
static TestCommon *common = &testCommon;
static std::unordered_map<void *, std::size_t> allocations;
static std::size_t allocationCount = 0;
static std::size_t allocatedBytes = 0;
static bool failAllocation = false;
static int requestedAllocation = 0;
static constexpr std::size_t guardBytes = 32;

static void Check( bool condition, const char *message ) {
	if ( !condition ) { throw std::runtime_error( message ); }
}

static void CheckGuards() {
	for ( const auto &entry : allocations ) {
		const unsigned char *bytes = static_cast<unsigned char *>( entry.first );
		for ( std::size_t i = 0; i < guardBytes; ++i ) {
			Check( bytes[entry.second + i] == 0xa5, "memory-file write crossed allocation boundary" );
		}
	}
}

static void *Mem_Alloc( int bytes ) {
	requestedAllocation = bytes;
	if ( failAllocation ) { throw TestError( "simulated allocation failure" ); }
	Check( bytes > 0 && bytes < 128 * 1024 * 1024, "invalid or unexpectedly large allocation" );
	void *result = std::malloc( static_cast<std::size_t>( bytes ) + guardBytes );
	Check( result != nullptr, "test allocation failed" );
	std::memset( result, 0xa5, static_cast<std::size_t>( bytes ) + guardBytes );
	allocations[result] = bytes;
	++allocationCount;
	allocatedBytes += bytes;
	return result;
}

static void Mem_Free( void *data ) {
	if ( data == nullptr ) { return; }
	Check( allocations.count( data ) == 1, "attempted to free borrowed memory" );
	CheckGuards();
	allocations.erase( data );
	std::free( data );
}

#include "FileMemoryUnderTest.inc"

static void CheckContents( idFile_Memory &file, const std::string &expected ) {
	Check( file.Length() == static_cast<int>( expected.size() ), "incorrect logical file length" );
	Check( std::memcmp( file.GetDataPtr(), expected.data(), expected.size() ) == 0, "incorrect file bytes" );
	Check( file.GetDataPtr()[file.Length()] == 0, "missing trailing terminator" );
	CheckGuards();
}

template<typename F> static void ExpectError( F operation ) {
	bool rejected = false;
	try { operation(); } catch ( const TestError & ) { rejected = true; }
	Check( rejected, "invalid operation was accepted" );
}

static void TestOverwrite() {
	idFile_Memory file;
	file.SetGranularity( 8 );
	Check( file.Write( "abcdef", 6 ) == 6, "initial write" );
	file.Seek( 1, FS_SEEK_SET );
	file.Write( "XY", 2 );
	CheckContents( file, "aXYdef" );
	Check( file.Tell() == 3, "overwrite cursor" );
	file.Seek( 5, FS_SEEK_SET );
	file.Write( "12345", 5 );
	CheckContents( file, "aXYde12345" );
}

static void TestBoundsAndSeeks() {
	idFile_Memory file;
	Check( file.Write( nullptr, 5 ) == 0 && file.Write( "x", -1 ) == 0, "invalid write changed file" );
	file.Write( "abcdef", 6 );
	const int maximum = std::numeric_limits<int>::max();
	ExpectError( [&] { file.Write( "x", maximum ); } );
	CheckContents( file, "abcdef" );
	Check( file.Tell() == 6, "overflow changed cursor" );
	const long high = std::numeric_limits<long>::max();
	const long low = std::numeric_limits<long>::min();
	for ( fsOrigin_t origin : { FS_SEEK_SET, FS_SEEK_CUR, FS_SEEK_END } ) {
		file.Seek( 3, FS_SEEK_SET );
		Check( file.Seek( high, origin ) == -1, "large positive seek accepted" );
		Check( file.Tell() == ( origin == FS_SEEK_END ? 0 : 6 ), "positive seek clamp" );
		file.Seek( 3, FS_SEEK_SET );
		Check( file.Seek( low, origin ) == -1, "large negative seek accepted" );
		Check( file.Tell() == ( origin == FS_SEEK_END ? 6 : 0 ), "negative seek clamp" );
	}
	Check( file.Seek( 2, FS_SEEK_END ) == 0 && file.Tell() == 4, "legacy positive end-relative seek" );
	Check( file.Seek( -2, FS_SEEK_CUR ) == 0 && file.Tell() == 2, "relative backward seek" );
	file.MakeReadOnly();
	char readback[8] = {};
	Check( file.Read( readback, maximum ) == 6, "oversized read did not stop at EOF" );
	Check( std::memcmp( readback, "abcdef", 6 ) == 0, "readback bytes" );
	Check( file.Read( readback, maximum ) == 0, "EOF read" );
	Check( file.Read( nullptr, 3 ) == 0 && file.Read( readback, -1 ) == 0, "invalid read" );
	idFile_Memory empty;
	Check( empty.Seek( 0, FS_SEEK_END ) == 0 && empty.Seek( 1, FS_SEEK_SET ) == -1, "empty seek" );
}

static void TestAliasedWrites() {
	idFile_Memory file;
	file.SetGranularity( 8 );
	file.Write( "abcdef", 6 );
	file.Write( file.GetDataPtr(), 6 );
	CheckContents( file, "abcdefabcdef" );
	file.Seek( 2, FS_SEEK_SET );
	file.Write( file.GetDataPtr(), 6 );
	CheckContents( file, "ababcdefcdef" );
	file.Seek( 0, FS_SEEK_SET );
	file.Write( file.GetDataPtr() + 2, 6 );
	CheckContents( file, "abcdefefcdef" );
	ExpectError( [&] { file.Write( file.GetDataPtr(), 1024 ); } );
	CheckContents( file, "abcdefefcdef" );
}

static void TestAllocationLimits() {
	idFile_Memory file;
	const int maximum = std::numeric_limits<int>::max();
	failAllocation = true;
	file.SetGranularity( maximum );
	ExpectError( [&] { file.Write( "x", 1 ); } );
	Check( requestedAllocation == maximum, "large granularity overflowed allocation" );
	file.SetGranularity( 3 );
	ExpectError( [&] { file.Write( "x", maximum - 1 ); } );
	Check( requestedAllocation == maximum, "rounding overflowed allocation" );
	Check( file.Length() == 0 && file.Tell() == 0 && file.GetDataPtr() == nullptr, "allocation failure modified file" );
	failAllocation = false;
	file.SetGranularity( 8 );
	file.Write( "abcdef", 6 );
	const char *storage = file.GetDataPtr();
	failAllocation = true;
	ExpectError( [&] { file.Write( "0123456789", 10 ); } );
	Check( file.GetDataPtr() == storage && file.Tell() == 6, "failed growth changed storage or cursor" );
	CheckContents( file, "abcdef" );
	failAllocation = false;
}

static void TestOwnership() {
	char external[8] = {};
	{
		idFile_Memory file( "borrowed", external, sizeof( external ) );
		file.Write( "1234567", 7 );
		ExpectError( [&] { file.Write( "x", 1 ); } );
		CheckContents( file, "1234567" );
		file.Clear( false );
		Check( external[0] == 0 && file.Length() == 0, "retained clear did not terminate empty file" );
		file.Write( "ok", 2 );
		file.Clear();
		Check( file.Length() == 0 && file.Tell() == 0, "borrowed clear state" );
		file.Write( "reused", 6 );
		CheckContents( file, "reused" );
	}
	Check( std::strcmp( external, "ok" ) == 0, "borrowed clear modified caller data" );
	{
		idFile_Memory readOnly( "literal", static_cast<const char *>( "immutable" ), 9 );
		readOnly.Clear( false );
		readOnly.Clear();
	}
	{
		idFile_Memory zero( "zero-capacity", external, 0 );
		zero.Write( "dynamic", 7 );
		CheckContents( zero, "dynamic" );
	}
	{
		idFile_Memory file;
		file.Write( "owned", 5 );
		file.SetData( "replacement", 11 );
		Check( allocations.empty(), "SetData leaked previous owned buffer" );
		char result[12] = {};
		Check( file.Read( result, 12 ) == 11 && std::strcmp( result, "replacement" ) == 0, "SetData replacement" );
	}
	{
		idFile_Memory file;
		file.Write( "abcdef", 6 );
		ExpectError( [&] { file.SetData( file.GetDataPtr() + 2, std::numeric_limits<int>::max() ); } );
		CheckContents( file, "abcdef" );
		file.SetData( file.GetDataPtr() + 2, 4 );
		char result[5] = {};
		Check( file.Read( result, 5 ) == 4 && std::strcmp( result, "cdef" ) == 0, "SetData owned subrange" );
	}
}

static void TestGrowth() {
	constexpr int size = 8 * 1024 * 1024;
	constexpr int chunkSize = 64;
	char chunk[chunkSize];
	std::memset( chunk, 'q', sizeof( chunk ) );
	allocationCount = allocatedBytes = 0;
	idFile_Memory file;
	for ( int i = 0; i < size; i += chunkSize ) {
		Check( file.Write( chunk, chunkSize ) == chunkSize, "chunk write" );
	}
	std::printf( "8 MiB / 64-byte writes: %zu allocations, %zu total allocated bytes\n", allocationCount, allocatedBytes );
	Check( allocationCount <= 24, "growth is not amortized" );
	Check( allocatedBytes < static_cast<std::size_t>( size ) * 5, "excessive growth/copy work" );
	CheckContents( file, std::string( size, 'q' ) );
	const char *storage = file.GetDataPtr();
	file.Clear( false );
	file.Write( "reuse", 5 );
	Check( file.GetDataPtr() == storage, "retained clear discarded allocation" );
	CheckContents( file, "reuse" );
}

static void TestMixedOperations() {
	idFile_Memory file;
	file.SetGranularity( 7 );
	std::string model;
	int cursor = 0;
	std::uint32_t state = 0x512af;
	for ( int i = 0; i < 10000; ++i ) {
		state = state * 1664525u + 1013904223u;
		if ( ( state & 7 ) == 0 ) {
			cursor = static_cast<int>( ( state >> 8 ) % ( model.size() + 1 ) );
			file.Seek( cursor, FS_SEEK_SET );
		} else {
			const std::string bytes( 1 + ( ( state >> 8 ) % 31 ), static_cast<char>( 'a' + state % 26 ) );
			file.Write( bytes.data(), static_cast<int>( bytes.size() ) );
			if ( cursor + bytes.size() > model.size() ) { model.resize( cursor + bytes.size() ); }
			model.replace( cursor, bytes.size(), bytes );
			cursor += static_cast<int>( bytes.size() );
			CheckContents( file, model );
		}
		Check( file.Tell() == cursor, "mixed-operation cursor mismatch" );
	}
}

int main( int argc, char **argv ) {
	try {
		if ( argc > 1 && std::strcmp( argv[1], "--growth" ) == 0 ) { TestGrowth(); }
		else {
			TestOverwrite();
			TestBoundsAndSeeks();
			TestAliasedWrites();
			TestAllocationLimits();
			TestOwnership();
			TestMixedOperations();
			TestGrowth();
		}
		Check( allocations.empty(), "memory-file allocation leak" );
		std::puts( "Memory-file checks passed" );
		return 0;
	} catch ( const std::exception &error ) {
		std::fprintf( stderr, "Memory-file test failed: %s\n", error.what() );
		return 1;
	}
}
