// Runs production match-result, series-report and recovery writers against
// an idFile/idFileSystem fault injector. No real filesystem state is mutated.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
static void Check( bool ok, const char *message ) {
	if ( !ok ) { std::fprintf( stderr, "FAILED: %s\n", message ); failures++; }
}

class idFile {
public:
	std::vector<unsigned char> data;
	int chunk = 7, failAfter = -1, writes = 0, syncs = 0;
	int error = 0;
	bool syncOk = true, overreport = false;
	int Write( const void *source, int bytes ) {
		writes++;
		if ( overreport ) { return bytes + 1; }
		if ( failAfter >= 0 && static_cast<int>( data.size() ) >= failAfter ) { return error; }
		int written = std::min( chunk, bytes );
		if ( failAfter >= 0 ) { written = std::min( written, failAfter - static_cast<int>( data.size() ) ); }
		const auto *p = static_cast<const unsigned char *>( source );
		data.insert( data.end(), p, p + written );
		return written;
	}
	bool Sync() { syncs++; return syncOk; }
};

class idFileSystem {
public:
	idFile file;
	int opens = 0, closes = 0, promotions = 0, removals = 0;
	bool openOk = true, promoteOk = true, removeOk = true;
	idFile *OpenFileWrite( const char *, const char *root ) {
		Check( std::strcmp( root, "fs_savepath" ) == 0, "write rooted at fs_savepath" );
		opens++; return openOk ? &file : nullptr;
	}
	void CloseFile( idFile *f ) { Check( f == &file, "close owns the opened file" ); closes++; }
	bool PromoteFile( const char *, const char *, const char *root ) {
		Check( std::strcmp( root, "fs_savepath" ) == 0, "promotion rooted at fs_savepath" );
		promotions++; return promoteOk;
	}
	bool RemoveFileChecked( const char *, const char *root ) {
		Check( std::strcmp( root, "fs_savepath" ) == 0, "cleanup rooted at fs_savepath" );
		removals++; return removeOk;
	}
};

// PRODUCTION_ADAPTERS

template<class Writer> static void TestAdapter( int maximum, const char *label ) {
	std::printf( "adapter %s\n", label );
	std::vector<unsigned char> payload( maximum + 1 );
	for ( int i = 0; i <= maximum; i++ ) { payload[i] = static_cast<unsigned char>( i * 31 ); }
	for ( int length : { 1, 257, maximum } ) {
		idFileSystem fs;
		fs.file.chunk = length > 257 ? 4096 : 7;
		Writer writer( &fs );
		Check( writer.WriteTemp( "canonical.pending", payload.data(), length ) == length, "all short writes completed" );
		Check( fs.file.data == std::vector<unsigned char>( payload.begin(), payload.begin() + length ), "short writes preserve every byte and offset" );
		Check( fs.file.syncs == 1 && fs.closes == 1, "successful write synced and closed exactly once" );
	}
	// Every possible partial-write boundary, including failure before byte zero.
	for ( int offset = 0; offset < 257; offset++ ) {
		for ( int error : { 0, -1 } ) {
			idFileSystem fs;
			fs.file.failAfter = offset; fs.file.error = error;
			Writer writer( &fs );
			Check( writer.WriteTemp( "canonical.pending", payload.data(), 257 ) == ( offset ? offset : -1 ), "failed write reports exactly the partial byte count" );
			Check( fs.file.syncs == 0 && fs.closes == 1, "failed write closes without syncing incomplete bytes" );
		}
	}
	for ( bool syncFailure : { false, true } ) {
		idFileSystem fs;
		fs.file.syncOk = !syncFailure; fs.file.overreport = !syncFailure;
		Writer writer( &fs );
		Check( writer.WriteTemp( "canonical.pending", payload.data(), 257 ) == -1, "sync failure or impossible write count cannot report success" );
		Check( fs.closes == 1, "failure still closes the handle" );
	}
	idFileSystem fs;
	Writer writer( &fs ), absent( nullptr );
	for ( int length : { -1, 0, maximum + 1 } ) {
		Check( writer.WriteTemp( "canonical.pending", payload.data(), length ) == -1, "invalid length refused before open" );
	}
	Check( writer.WriteTemp( "canonical.pending", nullptr, 1 ) == -1, "null payload refused" );
	Check( writer.WriteTemp( "invalid.pending", payload.data(), 1 ) == -1, "path gate applied before open" );
	Check( absent.WriteTemp( "canonical.pending", payload.data(), 1 ) == -1, "null backend refused" );
	Check( fs.opens == 0, "rejected input has no filesystem effects" );
	fs.openOk = false;
	Check( writer.WriteTemp( "canonical.pending", payload.data(), 1 ) == -1 && fs.closes == 0, "open failure does not close an absent handle" );
	Check( !writer.Promote( "invalid.pending", "canonical.json" ) && fs.promotions == 0, "promotion path gate" );
	Check( writer.Promote( "canonical.pending", "canonical.json" ), "successful promotion propagates" );
	fs.promoteOk = false;
	Check( !writer.Promote( "canonical.pending", "canonical.json" ), "failed promotion propagates" );
	Check( !writer.RemoveTemp( "invalid.pending" ) && fs.removals == 0, "cleanup path gate" );
	Check( writer.RemoveTemp( "canonical.pending" ), "successful cleanup propagates" );
	fs.removeOk = false;
	Check( !writer.RemoveTemp( "canonical.pending" ), "failed cleanup propagates" );
	Check( !absent.Promote( "canonical.pending", "canonical.json" ) && !absent.RemoveTemp( "canonical.pending" ), "null backend cannot mutate artifacts" );
}

int main() {
	// TEST_ADAPTERS
	return failures ? 1 : 0;
}
