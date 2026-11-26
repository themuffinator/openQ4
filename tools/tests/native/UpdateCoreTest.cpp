// Copyright (C) 2026 DarkMatter Productions
//
// The in-game updater's core: versions, manifest validation and signature,
// asset names, the plan and result files, file hashing and zip extraction.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#define __PRECOMPILED_H__
#include "../../../src/idlib/CryptoHash.cpp"
#include "../../../src/idlib/Ed25519.cpp"
#include "../../../src/imagetools/PNG/Inflate.cpp"
#include "../../../src/updater/UpdateCore.cpp"
#include "UpdateCoreFixtures.inc"

namespace fs = std::filesystem;
using namespace openq4::update;

static int failures = 0;

#define CHECK( condition, ... ) \
	do { \
		if ( !( condition ) ) { \
			std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); \
			std::printf( __VA_ARGS__ ); \
			std::printf( "\n" ); \
			++failures; \
		} \
	} while ( 0 )

static std::string_view Bytes( const unsigned char *data, size_t size ) {
	return std::string_view( reinterpret_cast<const char *>( data ), size );
}

static void TestVersions() {
	Version a, b;
	CHECK( ParseVersion( "0.14.1", a ) && a.major == 0 && a.minor == 14 && a.patch == 1, "parse 0.14.1" );
	CHECK( ParseVersion( "1.0.0", b ) && CompareVersions( a, b ) < 0, "0.14.1 < 1.0.0" );
	CHECK( ParseVersion( "0.14.10", b ) && CompareVersions( a, b ) < 0, "numeric, not lexical, order" );
	CHECK( ParseVersion( "0.14.1", b ) && CompareVersions( a, b ) == 0, "equal versions" );
	CHECK( FormatVersion( a ) == "0.14.1", "format" );
	CHECK( ParseVersion( "0.14.000", b ) && b.patch == 0, "withdrawn-style tag version" );
	for ( const char *bad : { "", "0.14", "0.14.1.2", "v0.14.1", "0.14.1-nightly", "0.14.", "a.b.c", "0.14.1 ", "100000.0.0" } ) {
		CHECK( !ParseVersion( bad, a ), "rejects version '%s'", bad );
	}
}

static void TestNames() {
	CHECK( IsSafeAssetName( "openq4-0.15.0-windows-x64-update.zip" ), "release asset" );
	CHECK( IsSafeAssetName( "pak1.pk4" ), "pack asset" );
	for ( const char *bad : { "", "../x", "a/b", "a\\b", ".hidden", "a..b", "a b", "a?b", "-flag" } ) {
		CHECK( !IsSafeAssetName( bad ), "rejects asset '%s'", bad );
	}
	CHECK( IsSafeTag( "v0.15.0" ) && !IsSafeTag( "0.15.0" ) && !IsSafeTag( "v" ) && !IsSafeTag( "v1/2" ), "tags" );
	CHECK( ReleaseAssetURL( "https://github.com/o/r", "v1.2.3", "a.zip" ) == "https://github.com/o/r/releases/download/v1.2.3/a.zip", "asset URL" );
	CHECK( LatestManifestURL( "https://github.com/o/r", MANIFEST_ASSET ) == "https://github.com/o/r/releases/latest/download/openq4-update.json", "manifest URL" );

	CHECK( IsSafeRelativePath( "baseoq4/pak0.pk4" ) && IsSafeRelativePath( "openQ4-client_x64.exe" ), "relative paths" );
	for ( const char *bad : { "", "/abs", "a//b", "a/./b", "a/../b", "..", "c:/x", "a\\b", "a/b/", ".update/plan.txt", "x.", "x ", "a\tb" } ) {
		CHECK( !IsSafeRelativePath( bad ), "rejects path '%s'", bad );
	}
	CHECK( IsSafeRelativePath( "docs/.update" ), ".update is reserved only at the top" );
}

static void TestManifest() {
	Manifest manifest;
	std::string error;
	CHECK( ParseManifest( Bytes( FIXTURE_MANIFEST, FIXTURE_MANIFEST_SIZE ), manifest, error ), "good manifest: %s", error.c_str() );
	CHECK( manifest.versionText == "0.15.0" && manifest.tag == "v0.15.0", "version and tag" );
	CHECK( manifest.packages.size() == 2 && manifest.packages.count( "windows-x64" ) == 1, "packages" );
	std::string lowered;
	for ( int i = 0; i < 32; ++i ) {
		lowered += "cd";
	}
	CHECK( manifest.packages[ "linux-x64" ].sha256 == lowered, "digests are lowercased" );
	CHECK( manifest.packs.size() == 2 && manifest.packs[ 1 ].bulk && !manifest.packs[ 0 ].bulk, "packs" );

	const struct { const unsigned char *data; size_t size; const char *what; } bad[] = {
		{ FIXTURE_MANIFEST_BAD_TAG, FIXTURE_MANIFEST_BAD_TAG_SIZE, "tag that does not match the version" },
		{ FIXTURE_MANIFEST_BAD_ASSET, FIXTURE_MANIFEST_BAD_ASSET_SIZE, "asset name with a path" },
		{ FIXTURE_MANIFEST_BAD_PRODUCT, FIXTURE_MANIFEST_BAD_PRODUCT_SIZE, "another product" },
		{ FIXTURE_MANIFEST_BAD_VERSION, FIXTURE_MANIFEST_BAD_VERSION_SIZE, "prerelease version" },
	};
	for ( const auto &entry : bad ) {
		CHECK( !ParseManifest( Bytes( entry.data, entry.size ), manifest, error ), "rejects a manifest with a %s", entry.what );
	}
	CHECK( !ParseManifest( "{\"schema\": 1, \"schema\": 1}", manifest, error ), "rejects duplicate keys" );
	CHECK( !ParseManifest( "not json", manifest, error ), "rejects text" );
	CHECK( !ParseManifest( std::string( MAX_MANIFEST_BYTES + 1, ' ' ), manifest, error ), "rejects an oversized manifest" );

	std::uint8_t key[ 32 ];
	CHECK( DecodePublicKey( FIXTURE_PUBLIC_KEY, key ), "public key decodes" );
	CHECK( !DecodePublicKey( "", key ) && !DecodePublicKey( "zz", key ), "empty or bad key does not decode" );
	const std::string_view good = Bytes( FIXTURE_MANIFEST, FIXTURE_MANIFEST_SIZE );
	CHECK( VerifyManifestSignature( good, FIXTURE_SIGNATURE, key ), "signature verifies" );
	CHECK( VerifyManifestSignature( good, std::string( FIXTURE_SIGNATURE ) + "\r\n", key ), "trailing newline is fine" );
	CHECK( !VerifyManifestSignature( Bytes( FIXTURE_MANIFEST_TAMPERED, FIXTURE_MANIFEST_TAMPERED_SIZE ), FIXTURE_SIGNATURE, key ), "tampered manifest fails" );
	CHECK( !VerifyManifestSignature( good, FIXTURE_OTHER_SIGNATURE, key ), "another key's signature fails" );
	CHECK( !VerifyManifestSignature( good, "", key ) && !VerifyManifestSignature( good, "00", key ), "missing signature fails" );
	std::string flipped = FIXTURE_SIGNATURE;
	flipped[ 10 ] = flipped[ 10 ] == '0' ? '1' : '0';
	CHECK( !VerifyManifestSignature( good, flipped, key ), "altered signature fails" );
}

static void TestPlan() {
	UpdatePlan plan;
	plan.version = "0.15.0";
	plan.fromVersion = "0.14.1";
	plan.installDir = "C:/Games/openQ4 \xc3\xa9t\xc3\xa9";
	plan.relaunch = plan.installDir + "/openQ4-client_x64.exe";
	plan.workingDir = plan.installDir;
	plan.arguments = { "+set", "fs_savepath", "with\ttab\nand newline\\" };
	plan.commandTail = "+set com_skipIntro 1";
	plan.files = { "openQ4-client_x64.exe", "baseoq4/pak0.pk4" };
	plan.removals = { "baseoq4/pak7.pk4" };
	plan.restartFailedMessage = "Start openQ4 again.";
	UpdatePlan parsed;
	std::string error;
	CHECK( ParsePlan( FormatPlan( plan ), parsed, error ), "plan round trip: %s", error.c_str() );
	CHECK( parsed.installDir == plan.installDir && parsed.arguments == plan.arguments && parsed.files == plan.files &&
		parsed.removals == plan.removals && parsed.commandTail == plan.commandTail &&
		parsed.restartFailedMessage == plan.restartFailedMessage, "plan fields survive" );

	std::string truncated = FormatPlan( plan );
	truncated.resize( truncated.size() - 4 );
	CHECK( !ParsePlan( truncated, parsed, error ), "a plan without its end line is not applied" );
	plan.files.push_back( "../outside.dll" );
	CHECK( !ParsePlan( FormatPlan( plan ), parsed, error ), "a plan with an unsafe path is refused" );
	CHECK( !ParsePlan( "openq4-update-plan 2\nend\n", parsed, error ), "another format is refused" );

	UpdateResult result;
	result.succeeded = true;
	result.version = "0.15.0";
	result.fromVersion = "0.14.1";
	result.detail = "installed";
	UpdateResult read;
	CHECK( ParseResult( FormatResult( result ), read, error ) && read.succeeded && read.version == "0.15.0", "result round trip" );
	result.succeeded = false;
	CHECK( ParseResult( FormatResult( result ), read, error ) && !read.succeeded, "failed result round trip" );
}

static bool WriteFile( const fs::path &path, const unsigned char *data, size_t size ) {
	std::ofstream out( path, std::ios::binary | std::ios::trunc );
	return out && out.write( reinterpret_cast<const char *>( data ), static_cast<std::streamsize>( size ) );
}

static void TestFiles( const fs::path &scratch ) {
	std::error_code ec;
	fs::remove_all( scratch, ec );
	fs::create_directories( scratch, ec );

	const fs::path zip = scratch / "good.zip";
	CHECK( WriteFile( zip, FIXTURE_ZIP_GOOD, FIXTURE_ZIP_GOOD_SIZE ), "write zip fixture" );
	// A non-ASCII destination: the engine's narrow paths could not open it.
	const fs::path out = scratch / PathFromUtf8( "stage-\xc3\xa9t\xc3\xa9" );
	std::vector<std::string> files;
	std::string error;
	CHECK( ExtractZip( zip, out, files, error ), "extract good zip: %s", error.c_str() );
	CHECK( ( files == std::vector<std::string> { "README.txt", "baseoq4/game-sp_x64.dll", "empty.txt", "launch.sh" } ), "extracted names" );
	std::string digest;
	CHECK( SHA256File( out / "baseoq4" / "game-sp_x64.dll", digest, error ) && digest == FIXTURE_PAYLOAD_SHA256, "deflated member is intact" );
	CHECK( fs::file_size( out / "empty.txt", ec ) == 0, "empty member" );
#if !defined( _WIN32 )
	CHECK( ( fs::status( out / "launch.sh" ).permissions() & fs::perms::owner_exec ) != fs::perms::none, "Unix exec bit restored" );
#endif
	std::vector<std::string> listed;
	CHECK( ListTreeFiles( out, listed, error ) && listed == files, "tree listing matches the archive" );

	const struct { const unsigned char *data; size_t size; const char *what; } bad[] = {
		{ FIXTURE_ZIP_TRAVERSAL, FIXTURE_ZIP_TRAVERSAL_SIZE, "a ../ member" },
		{ FIXTURE_ZIP_ABSOLUTE, FIXTURE_ZIP_ABSOLUTE_SIZE, "an absolute member" },
		{ FIXTURE_ZIP_RESERVED, FIXTURE_ZIP_RESERVED_SIZE, "a .update member" },
		{ FIXTURE_ZIP_BAD_CRC, FIXTURE_ZIP_BAD_CRC_SIZE, "a damaged member" },
	};
	for ( const auto &entry : bad ) {
		const fs::path badZip = scratch / "bad.zip";
		const fs::path badOut = scratch / "bad";
		fs::remove_all( badOut, ec );
		CHECK( WriteFile( badZip, entry.data, entry.size ), "write fixture" );
		CHECK( !ExtractZip( badZip, badOut, files, error ), "rejects %s", entry.what );
		CHECK( !fs::exists( scratch.parent_path() / "escape.txt" ), "nothing escaped for %s", entry.what );
	}
	CHECK( !ExtractZip( scratch / "missing.zip", out, files, error ), "missing archive" );

	CHECK( WriteFileAtomically( scratch / "plan.txt", "hello", error ), "atomic write" );
	std::string text;
	CHECK( ReadSmallFile( scratch / "plan.txt", 16, text ) && text == "hello", "read back" );
	CHECK( !fs::exists( scratch / "plan.txt.partial" ), "no partial file left" );

	// Streaming SHA-256 matches the one-shot digest across chunk boundaries.
	std::vector<std::uint8_t> data( 100000 );
	for ( size_t i = 0; i < data.size(); ++i ) {
		data[ i ] = static_cast<std::uint8_t>( i * 31 + 7 );
	}
	std::uint8_t oneShot[ 32 ], streamed[ 32 ];
	idCrypto::SHA256( data.data(), data.size(), oneShot );
	idCrypto::SHA256Stream stream;
	for ( size_t at = 0; at < data.size(); ) {
		const size_t piece = std::min<size_t>( data.size() - at, 1 + ( at % 97 ) );
		stream.Update( data.data() + at, piece );
		at += piece;
	}
	stream.Final( streamed );
	CHECK( std::memcmp( oneShot, streamed, 32 ) == 0, "streamed SHA-256" );

	fs::remove_all( scratch, ec );
}

static std::string Sha256Hex( const std::string &data ) {
	std::uint8_t digest[ 32 ];
	idCrypto::SHA256( data.data(), data.size(), digest );
	return HexEncode( digest, sizeof( digest ) );
}

// A release served from memory, in uneven chunks.
struct FakeRelease {
	std::map<std::string, std::string> assets;
	std::string failAsset;		// answer this one with a transport error
	bool padAssets = false;		// serve one byte more than the asset
	std::vector<std::string> fetched;

	FetchFn Fetch() {
		return [this]( const std::string &asset, std::uint64_t maxBytes,
				const std::function<bool( const void *, std::size_t )> &onData, bool &cancelled, std::string &error ) {
			fetched.push_back( asset );
			if ( asset == failAsset || assets.count( asset ) == 0 ) {
				error = "the server answered HTTP 404";
				return false;
			}
			std::string body = assets[ asset ];
			if ( padAssets ) {
				body += 'x';
			}
			if ( body.size() > maxBytes + 1 ) {
				error = "larger than expected";
				return false;
			}
			for ( std::size_t at = 0; at < body.size(); ) {
				const std::size_t piece = std::min<std::size_t>( body.size() - at, 1000 + at % 777 );
				if ( !onData( body.data() + at, piece ) ) {
					cancelled = true;
					error = "cancelled";
					return false;
				}
				at += piece;
			}
			return true;
		};
	}
};

static void TestStage( const fs::path &scratch ) {
	std::error_code ec;
	const fs::path install = scratch / PathFromUtf8( "install-\xc3\xa9" );
	const std::string zip( reinterpret_cast<const char *>( FIXTURE_ZIP_GOOD ), FIXTURE_ZIP_GOOD_SIZE );
	const std::string pak1 = std::string( 50000, 'L' ) + "light grids";

	auto reset = [&]() {
		fs::remove_all( install, ec );
		fs::create_directories( install / "baseoq4", ec );
		std::ofstream( install / "baseoq4" / "pak7.pk4", std::ios::binary ) << "retired overlay";
		std::ofstream( install / "baseoq4" / "pak0.pk4", std::ios::binary ) << "current pak0";
		// CORE-07: the split modules an update planned by 0.15.0 left behind;
		// game-sp_x64.dll stays because the fixture archive carries that name
		std::ofstream( install / "baseoq4" / "game-mp_x64.pdb", std::ios::binary ) << "retired symbols";
		std::ofstream( install / "baseoq4" / "game-mp_x64.dll", std::ios::binary ) << "retired module";
		std::ofstream( install / "baseoq4" / "game-sp_x64.dll", std::ios::binary ) << "replaced module";
		std::ofstream( install / "baseoq4" / "game_x64.dll", std::ios::binary ) << "current module";
	};
	auto request = [&]() {
		StageRequest r;
		r.manifest.versionText = "0.15.0";
		ParseVersion( r.manifest.versionText, r.manifest.version );
		r.manifest.tag = "v0.15.0";
		r.manifest.packages[ "windows-x64" ] = { "openq4-0.15.0-windows-x64-update.zip", zip.size(), Sha256Hex( zip ) };
		r.manifest.packs = {
			{ "pak0.pk4", std::string( 32, '0' ), std::string( 64, '1' ), 12, false },
			{ "pak1.pk4", std::string( 32, 'a' ), Sha256Hex( pak1 ), pak1.size(), true },
			{ "pak2.pk4", std::string( 32, 'b' ), std::string( 64, '2' ), 999, true },
		};
		r.platformKey = "windows-x64";
		r.install = install;
		r.requiredFiles = { "README.txt", "launch.sh" };
		// pak2 is current here; pak1 has to come down.
		r.packs = SelectPacks( r.manifest, []( const PackEntry &pack ) { return pack.name == "pak2.pk4"; } );
		return r;
	};
	FakeRelease release;
	release.assets[ "openq4-0.15.0-windows-x64-update.zip" ] = zip;
	release.assets[ "pak1.pk4" ] = pak1;

	// Success.
	reset();
	StageRequest r = request();
	CHECK( r.packs.size() == 1 && r.packs[ 0 ].name == "pak1.pk4", "only the changed bulk pack is selected" );
	CHECK( StageBytes( r ) == zip.size() + pak1.size(), "download size counts the archive and the selected pack" );
	UpdatePlan plan;
	plan.version = "0.15.0";
	plan.installDir = PathToUtf8( install );
	plan.relaunch = PathToUtf8( install / "openQ4-client_x64.exe" );
	std::uint64_t lastDone = 0, lastTotal = 0;
	bool monotonic = true;
	StageResult result = StageRelease( r, plan, release.Fetch(), [&]( std::uint64_t done, std::uint64_t total ) {
		monotonic = monotonic && done >= lastDone;
		lastDone = done;
		lastTotal = total;
		return true;
	}, ExtractTarFn() );
	CHECK( result.ok, "stage succeeds: %s", result.error.c_str() );
	CHECK( monotonic && lastDone == lastTotal && lastTotal == zip.size() + pak1.size(), "progress reaches the total" );
	const fs::path updateDir = install / UPDATE_DIRECTORY;
	CHECK( ( plan.files == std::vector<std::string> { "README.txt", "baseoq4/game-sp_x64.dll", "baseoq4/pak1.pk4", "empty.txt", "launch.sh" } ),
		"plan lists the archive and the pack" );
	CHECK( ( plan.removals == std::vector<std::string> { "baseoq4/pak7.pk4", "baseoq4/game-mp_x64.dll", "baseoq4/game-mp_x64.pdb" } ),
		"an unlisted numbered pack and the retired split game modules are retired" );
	std::string digest, planText;
	CHECK( SHA256File( updateDir / STAGE_DIRECTORY / "baseoq4" / "pak1.pk4", digest, result.error ) && digest == Sha256Hex( pak1 ), "pack staged intact" );
	UpdatePlan written;
	CHECK( ReadSmallFile( updateDir / PLAN_FILE, 1 << 20, planText ) && ParsePlan( planText, written, result.error ) &&
		written.files == plan.files, "the plan on disk is the plan" );
	CHECK( !fs::exists( updateDir / DOWNLOAD_DIRECTORY ), "downloads are cleared once staged" );

	// Failures leave nothing a helper could apply.
	const auto failed = [&]( const char *what, StageRequest req, FakeRelease &source, const ProgressFn &progress,
			bool expectCancelled, const char *expectError ) {
		reset();
		UpdatePlan p = plan;
		const StageResult outcome = StageRelease( req, p, source.Fetch(), progress, ExtractTarFn() );
		CHECK( !outcome.ok, "%s fails", what );
		CHECK( outcome.cancelled == expectCancelled, "%s: cancelled is %d", what, outcome.cancelled ? 1 : 0 );
		CHECK( outcome.error.find( expectError ) != std::string::npos, "%s: error '%s'", what, outcome.error.c_str() );
		CHECK( !fs::exists( updateDir / PLAN_FILE ) && !fs::exists( updateDir / STAGE_DIRECTORY ) &&
			!fs::exists( updateDir / DOWNLOAD_DIRECTORY ), "%s leaves no plan, stage or download", what );
	};
	StageRequest wrongDigest = request();
	wrongDigest.manifest.packages[ "windows-x64" ].sha256 = std::string( 64, 'e' );
	failed( "a wrong digest", wrongDigest, release, ProgressFn(), false, "does not match" );
	FakeRelease padded = release;
	padded.padAssets = true;
	failed( "an asset longer than the manifest", request(), padded, ProgressFn(), false, "does not match" );
	FakeRelease missingPack = release;
	missingPack.failAsset = "pak1.pk4";
	failed( "a pack the server lacks", request(), missingPack, ProgressFn(), false, "HTTP 404" );
	failed( "a cancelled download", request(), release, []( std::uint64_t done, std::uint64_t ) { return done < 2000; }, true, "cancelled" );
	StageRequest needsClient = request();
	needsClient.requiredFiles.push_back( "openQ4-client_x64.exe" );
	failed( "an archive without the program to restart", needsClient, release, ProgressFn(), false, "has no openQ4-client_x64.exe" );
	StageRequest otherPlatform = request();
	otherPlatform.platformKey = "linux-arm64";
	failed( "a platform without an archive", otherPlatform, release, ProgressFn(), false, "no update archive" );

	fs::remove_all( install, ec );
}

int main( int argc, char **argv ) {
	const fs::path scratch = argc > 1 ? fs::path( argv[ 1 ] ) : fs::temp_directory_path() / "openq4-update-core-test";
	TestVersions();
	TestNames();
	TestManifest();
	TestPlan();
	TestFiles( scratch );
	TestStage( scratch );
	if ( failures != 0 ) {
		std::printf( "%d update core checks failed\n", failures );
		return 1;
	}
	std::printf( "update core: all checks passed\n" );
	return 0;
}
