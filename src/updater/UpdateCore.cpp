/*
===========================================================================

openQ4 update core
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#include "UpdateCore.h"
#include "UpdatePlan.h"

#include "../idlib/CryptoHash.h"
#include "../idlib/Ed25519.h"
#include "../imagetools/PNG/ZlibStream.h"

#include <json/json.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <system_error>

namespace openq4 {
namespace update {

namespace {

bool IsHexChar( char c ) {
	return ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' );
}

int HexValue( char c ) {
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	if ( c >= 'a' && c <= 'f' ) {
		return c - 'a' + 10;
	}
	return c - 'A' + 10;
}

bool HexDecode( std::string_view text, std::uint8_t *out, std::size_t bytes ) {
	if ( text.size() != bytes * 2 ) {
		return false;
	}
	for ( std::size_t i = 0; i < bytes; ++i ) {
		if ( !IsHexChar( text[ i * 2 ] ) || !IsHexChar( text[ i * 2 + 1 ] ) ) {
			return false;
		}
		out[ i ] = static_cast<std::uint8_t>( HexValue( text[ i * 2 ] ) * 16 + HexValue( text[ i * 2 + 1 ] ) );
	}
	return true;
}

std::string Lowercase( std::string text ) {
	for ( char &c : text ) {
		if ( c >= 'A' && c <= 'Z' ) {
			c = static_cast<char>( c - 'A' + 'a' );
		}
	}
	return text;
}

bool ReadString( const Json::Value &object, const char *key, std::string &out ) {
	const Json::Value &value = object[ key ];
	if ( !value.isString() ) {
		return false;
	}
	out = value.asString();
	return true;
}

bool ReadSize( const Json::Value &object, const char *key, std::uint64_t &out ) {
	const Json::Value &value = object[ key ];
	if ( !value.isUInt64() ) {
		return false;
	}
	out = value.asUInt64();
	return out <= MAX_DOWNLOAD_BYTES;
}

bool IsPackageKey( std::string_view key ) {
	if ( key.empty() || key.size() > 32 ) {
		return false;
	}
	for ( const char c : key ) {
		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '-' ) ) {
			return false;
		}
	}
	return true;
}

bool IsPackName( std::string_view name ) {
	return name.size() == 8 && name.substr( 0, 3 ) == "pak" && name[ 3 ] >= '0' && name[ 3 ] <= '9' && name.substr( 4 ) == ".pk4";
}

std::uint32_t Crc32( std::uint32_t crc, const std::uint8_t *data, std::size_t bytes ) {
	static const std::array<std::uint32_t, 256> table = [] {
		std::array<std::uint32_t, 256> built {};
		for ( std::uint32_t i = 0; i < 256; ++i ) {
			std::uint32_t c = i;
			for ( int k = 0; k < 8; ++k ) {
				c = ( c & 1 ) ? 0xedb88320u ^ ( c >> 1 ) : c >> 1;
			}
			built[ i ] = c;
		}
		return built;
	}();
	crc = ~crc;
	for ( std::size_t i = 0; i < bytes; ++i ) {
		crc = table[ ( crc ^ data[ i ] ) & 0xff ] ^ ( crc >> 8 );
	}
	return ~crc;
}

std::uint16_t Read16( const std::uint8_t *p ) {
	return static_cast<std::uint16_t>( p[ 0 ] | ( p[ 1 ] << 8 ) );
}

std::uint32_t Read32( const std::uint8_t *p ) {
	return static_cast<std::uint32_t>( p[ 0 ] ) | ( static_cast<std::uint32_t>( p[ 1 ] ) << 8 ) |
		( static_cast<std::uint32_t>( p[ 2 ] ) << 16 ) | ( static_cast<std::uint32_t>( p[ 3 ] ) << 24 );
}

bool Continue( const ContinueFn &keepGoing ) {
	return !keepGoing || keepGoing();
}

} // namespace

/*
===============================================================================

	Versions

===============================================================================
*/

bool ParseVersion( std::string_view text, Version &version ) {
	int parts[ 3 ] = { 0, 0, 0 };
	std::size_t position = 0;
	for ( int index = 0; index < 3; ++index ) {
		const std::size_t start = position;
		long value = 0;
		while ( position < text.size() && text[ position ] >= '0' && text[ position ] <= '9' ) {
			value = value * 10 + ( text[ position ] - '0' );
			if ( value > 99999 ) {
				return false;
			}
			++position;
		}
		if ( position == start ) {
			return false;
		}
		parts[ index ] = static_cast<int>( value );
		if ( index < 2 ) {
			if ( position >= text.size() || text[ position ] != '.' ) {
				return false;
			}
			++position;
		}
	}
	if ( position != text.size() ) {
		return false;
	}
	version.major = parts[ 0 ];
	version.minor = parts[ 1 ];
	version.patch = parts[ 2 ];
	return true;
}

int CompareVersions( const Version &a, const Version &b ) {
	if ( a.major != b.major ) {
		return a.major < b.major ? -1 : 1;
	}
	if ( a.minor != b.minor ) {
		return a.minor < b.minor ? -1 : 1;
	}
	if ( a.patch != b.patch ) {
		return a.patch < b.patch ? -1 : 1;
	}
	return 0;
}

std::string FormatVersion( const Version &version ) {
	return std::to_string( version.major ) + "." + std::to_string( version.minor ) + "." + std::to_string( version.patch );
}

/*
===============================================================================

	Names and URLs

===============================================================================
*/

bool IsSafeAssetName( std::string_view name ) {
	if ( name.empty() || name.size() > 128 ) {
		return false;
	}
	const char first = name.front();
	if ( !( ( first >= 'a' && first <= 'z' ) || ( first >= 'A' && first <= 'Z' ) || ( first >= '0' && first <= '9' ) ) ) {
		return false;
	}
	for ( const char c : name ) {
		const bool allowed = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ||
			c == '.' || c == '_' || c == '+' || c == '-';
		if ( !allowed ) {
			return false;
		}
	}
	return name.find( ".." ) == std::string_view::npos;
}

bool IsSafeTag( std::string_view tag ) {
	return tag.size() >= 2 && tag.front() == 'v' && IsSafeAssetName( tag ) && tag.find( '+' ) == std::string_view::npos;
}

bool IsHexDigest( std::string_view text, std::size_t bytes ) {
	if ( text.size() != bytes * 2 ) {
		return false;
	}
	return std::all_of( text.begin(), text.end(), IsHexChar );
}

std::string LatestManifestURL( std::string_view repository, std::string_view asset ) {
	return std::string( repository ) + "/releases/latest/download/" + std::string( asset );
}

std::string ReleaseAssetURL( std::string_view repository, std::string_view tag, std::string_view asset ) {
	return std::string( repository ) + "/releases/download/" + std::string( tag ) + "/" + std::string( asset );
}

std::string ReleasePageURL( std::string_view repository, std::string_view tag ) {
	return std::string( repository ) + "/releases/tag/" + std::string( tag );
}

std::string HexEncode( const std::uint8_t *bytes, std::size_t count ) {
	static const char digits[] = "0123456789abcdef";
	std::string text;
	text.reserve( count * 2 );
	for ( std::size_t i = 0; i < count; ++i ) {
		text += digits[ bytes[ i ] >> 4 ];
		text += digits[ bytes[ i ] & 15 ];
	}
	return text;
}

/*
===============================================================================

	Manifest

===============================================================================
*/

bool ParseManifest( std::string_view json, Manifest &manifest, std::string &error ) {
	manifest = Manifest();
	if ( json.empty() || json.size() > MAX_MANIFEST_BYTES ) {
		error = "manifest is empty or too large";
		return false;
	}

	Json::CharReaderBuilder builder;
	Json::CharReaderBuilder::strictMode( &builder.settings_ );
	builder.settings_[ "rejectDupKeys" ] = true;
	std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
	Json::Value root;
	std::string parseErrors;
	if ( !reader->parse( json.data(), json.data() + json.size(), &root, &parseErrors ) || !root.isObject() ) {
		error = "manifest is not valid JSON";
		return false;
	}

	if ( !root[ "schema" ].isInt() || root[ "schema" ].asInt() != MANIFEST_SCHEMA ) {
		error = "unsupported manifest schema";
		return false;
	}
	std::string product;
	if ( !ReadString( root, "product", product ) || product != "openQ4" ) {
		error = "manifest is not for openQ4";
		return false;
	}
	if ( !ReadString( root, "version", manifest.versionText ) || !ParseVersion( manifest.versionText, manifest.version ) ) {
		error = "manifest version is not MAJOR.MINOR.PATCH";
		return false;
	}
	if ( !ReadString( root, "tag", manifest.tag ) || !IsSafeTag( manifest.tag ) || manifest.tag != "v" + manifest.versionText ) {
		error = "manifest tag does not match its version";
		return false;
	}

	const Json::Value &packages = root[ "packages" ];
	if ( !packages.isObject() ) {
		error = "manifest has no packages";
		return false;
	}
	for ( const std::string &key : packages.getMemberNames() ) {
		const Json::Value &entry = packages[ key ];
		PackageEntry package;
		if ( !IsPackageKey( key ) || !entry.isObject() ||
				!ReadString( entry, "archive", package.archive ) || !IsSafeAssetName( package.archive ) ||
				!ReadSize( entry, "size", package.size ) || package.size == 0 ||
				!ReadString( entry, "sha256", package.sha256 ) || !IsHexDigest( package.sha256, 32 ) ) {
			error = "manifest package entry is invalid: " + key.substr( 0, 32 );
			return false;
		}
		package.sha256 = Lowercase( package.sha256 );
		manifest.packages[ key ] = package;
	}

	const Json::Value &packs = root[ "packs" ];
	if ( !packs.isArray() ) {
		error = "manifest has no pack list";
		return false;
	}
	for ( const Json::Value &entry : packs ) {
		PackEntry pack;
		if ( !entry.isObject() ||
				!ReadString( entry, "name", pack.name ) || !IsPackName( pack.name ) ||
				!ReadString( entry, "md5", pack.md5 ) || !IsHexDigest( pack.md5, 16 ) ||
				!ReadString( entry, "sha256", pack.sha256 ) || !IsHexDigest( pack.sha256, 32 ) ||
				!ReadSize( entry, "size", pack.size ) || !entry[ "bulk" ].isBool() ) {
			error = "manifest pack entry is invalid";
			return false;
		}
		pack.bulk = entry[ "bulk" ].asBool();
		pack.md5 = Lowercase( pack.md5 );
		pack.sha256 = Lowercase( pack.sha256 );
		for ( const PackEntry &existing : manifest.packs ) {
			if ( existing.name == pack.name ) {
				error = "manifest lists a pack twice";
				return false;
			}
		}
		manifest.packs.push_back( pack );
	}
	return true;
}

bool DecodePublicKey( std::string_view publicKeyHex, std::uint8_t publicKey[ 32 ] ) {
	return HexDecode( publicKeyHex, publicKey, 32 );
}

bool VerifyManifestSignature( std::string_view manifestBytes, std::string_view signatureText,
		const std::uint8_t publicKey[ 32 ] ) {
	while ( !signatureText.empty() && ( signatureText.back() == '\n' || signatureText.back() == '\r' ||
			signatureText.back() == ' ' || signatureText.back() == '\t' ) ) {
		signatureText.remove_suffix( 1 );
	}
	std::uint8_t signature[ idCrypto::ED25519_SIGNATURE_BYTES ];
	if ( !HexDecode( signatureText, signature, sizeof( signature ) ) ) {
		return false;
	}
	return idCrypto::Ed25519Verify( signature, manifestBytes.data(), manifestBytes.size(), publicKey );
}

/*
===============================================================================

	Files

===============================================================================
*/

std::filesystem::path PathFromUtf8( std::string_view text ) {
	return std::filesystem::path( std::u8string( reinterpret_cast<const char8_t *>( text.data() ), text.size() ) );
}

std::string PathToUtf8( const std::filesystem::path &path ) {
	const std::u8string text = path.u8string();
	return std::string( reinterpret_cast<const char *>( text.data() ), text.size() );
}

bool SHA256File( const std::filesystem::path &path, std::string &hexDigest, std::string &error, const ContinueFn &keepGoing ) {
	std::ifstream stream( path, std::ios::binary );
	if ( !stream ) {
		error = "cannot open " + PathToUtf8( path.filename() );
		return false;
	}
	idCrypto::SHA256Stream hash;
	std::vector<char> buffer( 1 << 20 );
	while ( stream ) {
		stream.read( buffer.data(), static_cast<std::streamsize>( buffer.size() ) );
		const std::streamsize got = stream.gcount();
		if ( got > 0 ) {
			hash.Update( buffer.data(), static_cast<std::size_t>( got ) );
		}
		if ( !Continue( keepGoing ) ) {
			error = "cancelled";
			return false;
		}
	}
	if ( stream.bad() ) {
		error = "cannot read " + PathToUtf8( path.filename() );
		return false;
	}
	std::uint8_t digest[ idCrypto::SHA256_DIGEST_BYTES ];
	hash.Final( digest );
	hexDigest = HexEncode( digest, sizeof( digest ) );
	return true;
}

bool ReadSmallFile( const std::filesystem::path &path, std::size_t maxBytes, std::string &contents ) {
	std::ifstream stream( path, std::ios::binary );
	if ( !stream ) {
		return false;
	}
	contents.assign( std::istreambuf_iterator<char>( stream ), std::istreambuf_iterator<char>() );
	return !stream.bad() && contents.size() <= maxBytes;
}

bool WriteFileAtomically( const std::filesystem::path &path, std::string_view contents, std::string &error ) {
	std::filesystem::path temporary = path;
	temporary += ".partial";
	{
		std::ofstream stream( temporary, std::ios::binary | std::ios::trunc );
		if ( !stream ) {
			error = "cannot write " + PathToUtf8( temporary.filename() );
			return false;
		}
		stream.write( contents.data(), static_cast<std::streamsize>( contents.size() ) );
		stream.flush();
		if ( !stream ) {
			error = "cannot write " + PathToUtf8( temporary.filename() );
			return false;
		}
	}
	std::error_code ec;
	std::filesystem::rename( temporary, path, ec );
	if ( ec ) {
		error = "cannot replace " + PathToUtf8( path.filename() ) + ": " + ec.message();
		std::filesystem::remove( temporary, ec );
		return false;
	}
	return true;
}

bool ListTreeFiles( const std::filesystem::path &root, std::vector<std::string> &files, std::string &error ) {
	files.clear();
	std::error_code ec;
	std::filesystem::recursive_directory_iterator it( root, std::filesystem::directory_options::none, ec ), end;
	if ( ec ) {
		error = "cannot list " + PathToUtf8( root.filename() ) + ": " + ec.message();
		return false;
	}
	for ( ; it != end; it.increment( ec ) ) {
		if ( ec ) {
			error = "cannot list " + PathToUtf8( root.filename() ) + ": " + ec.message();
			return false;
		}
		const std::filesystem::file_status status = it->symlink_status( ec );
		if ( std::filesystem::is_symlink( status ) ) {
			error = "unexpected link in the update: " + PathToUtf8( it->path().filename() );
			return false;
		}
		if ( !std::filesystem::is_regular_file( status ) ) {
			continue;
		}
		const std::u8string generic = it->path().lexically_relative( root ).generic_u8string();
		const std::string relative( reinterpret_cast<const char *>( generic.data() ), generic.size() );
		if ( !IsSafeRelativePath( relative ) ) {
			error = "unsafe file name in the update: " + relative.substr( 0, 128 );
			return false;
		}
		files.push_back( relative );
	}
	std::sort( files.begin(), files.end() );
	return true;
}

/*
===============================================================================

	Zip extraction

===============================================================================
*/

bool ExtractZip( const std::filesystem::path &archive, const std::filesystem::path &destination,
		std::vector<std::string> &files, std::string &error, const ContinueFn &keepGoing ) {
	files.clear();
	std::error_code ec;
	const std::uintmax_t archiveSize = std::filesystem::file_size( archive, ec );
	if ( ec || archiveSize < 22 || archiveSize > 0xffffffffull ) {
		error = "the update archive is missing or not a zip";
		return false;
	}
	std::vector<std::uint8_t> data( static_cast<std::size_t>( archiveSize ) );
	{
		std::ifstream stream( archive, std::ios::binary );
		if ( !stream || !stream.read( reinterpret_cast<char *>( data.data() ), static_cast<std::streamsize>( data.size() ) ) ) {
			error = "cannot read the update archive";
			return false;
		}
	}

	// The end-of-central-directory record is the last 22 bytes plus a comment of
	// at most 65535 bytes.
	std::size_t eocd = std::string::npos;
	const std::size_t searchStart = data.size() - 22;
	const std::size_t searchEnd = data.size() > 22 + 65535 ? data.size() - 22 - 65535 : 0;
	for ( std::size_t at = searchStart + 1; at-- > searchEnd; ) {
		if ( Read32( &data[ at ] ) == 0x06054b50u ) {
			eocd = at;
			break;
		}
	}
	if ( eocd == std::string::npos ) {
		error = "the update archive has no zip directory";
		return false;
	}
	const std::uint16_t entryCount = Read16( &data[ eocd + 10 ] );
	const std::uint32_t directorySize = Read32( &data[ eocd + 12 ] );
	const std::uint32_t directoryOffset = Read32( &data[ eocd + 16 ] );
	if ( Read16( &data[ eocd + 4 ] ) != 0 || Read16( &data[ eocd + 6 ] ) != 0 || entryCount == 0xffff ||
			directoryOffset == 0xffffffffu || static_cast<std::uint64_t>( directoryOffset ) + directorySize > eocd ) {
		error = "the update archive is a multi-part or ZIP64 archive";
		return false;
	}

	std::size_t cursor = directoryOffset;
	for ( std::uint32_t index = 0; index < entryCount; ++index ) {
		if ( !Continue( keepGoing ) ) {
			error = "cancelled";
			return false;
		}
		if ( cursor + 46 > eocd || Read32( &data[ cursor ] ) != 0x02014b50u ) {
			error = "the update archive directory is damaged";
			return false;
		}
		const std::uint8_t *header = &data[ cursor ];
		const std::uint16_t flags = Read16( header + 8 );
		const std::uint16_t method = Read16( header + 10 );
		const std::uint32_t crc = Read32( header + 16 );
		const std::uint32_t packedSize = Read32( header + 20 );
		const std::uint32_t size = Read32( header + 24 );
		const std::uint16_t nameLength = Read16( header + 28 );
		const std::uint16_t extraLength = Read16( header + 30 );
		const std::uint16_t commentLength = Read16( header + 32 );
		const std::uint32_t externalAttributes = Read32( header + 38 );
		const std::uint32_t localOffset = Read32( header + 42 );
		if ( cursor + 46 + nameLength > eocd ) {
			error = "the update archive directory is damaged";
			return false;
		}
		const std::string name( reinterpret_cast<const char *>( header + 46 ), nameLength );
		cursor += 46u + nameLength + extraLength + commentLength;

		if ( flags & 0x0001 ) {
			error = "the update archive is encrypted";
			return false;
		}
		if ( packedSize == 0xffffffffu || size == 0xffffffffu || localOffset == 0xffffffffu ) {
			error = "the update archive needs ZIP64";
			return false;
		}
		if ( !name.empty() && name.back() == '/' ) {
			continue;	// a directory entry; files create their own directories
		}
		if ( !IsSafeRelativePath( name ) ) {
			error = "unsafe file name in the update: " + name.substr( 0, 128 );
			return false;
		}
		if ( localOffset + 30ull > data.size() || Read32( &data[ localOffset ] ) != 0x04034b50u ) {
			error = "the update archive is damaged at " + name;
			return false;
		}
		const std::uint64_t payload = localOffset + 30ull + Read16( &data[ localOffset + 26 ] ) + Read16( &data[ localOffset + 28 ] );
		if ( payload + packedSize > data.size() ) {
			error = "the update archive is truncated at " + name;
			return false;
		}

		std::vector<std::uint8_t> contents( size );
		if ( method == 0 ) {
			if ( packedSize != size ) {
				error = "the update archive is damaged at " + name;
				return false;
			}
			if ( size != 0 ) {
				std::memcpy( contents.data(), &data[ static_cast<std::size_t>( payload ) ], size );
			}
		} else if ( method == 8 ) {
			std::uint8_t empty = 0;
			std::size_t written = 0;
			const zlibResult_t result = Zlib_InflateRaw( &data[ static_cast<std::size_t>( payload ) ], packedSize,
				size != 0 ? contents.data() : &empty, size, &written, nullptr );
			if ( result != ZLIB_OK || written != size ) {
				error = "the update archive is damaged at " + name + " (" + Zlib_ResultString( result ) + ")";
				return false;
			}
		} else {
			error = "the update archive uses an unsupported compression method";
			return false;
		}
		if ( Crc32( 0, contents.data(), contents.size() ) != crc ) {
			error = "the update archive fails its checksum at " + name;
			return false;
		}

		const std::filesystem::path target = destination / PathFromUtf8( name );
		std::filesystem::create_directories( target.parent_path(), ec );
		std::ofstream out( target, std::ios::binary | std::ios::trunc );
		if ( !out || !out.write( reinterpret_cast<const char *>( contents.data() ), static_cast<std::streamsize>( contents.size() ) ) ) {
			error = "cannot write " + name;
			return false;
		}
		out.close();
#if !defined( _WIN32 )
		// Unix permissions travel in the high half of the external attributes
		// when the archive was made on Unix (version-made-by host 3).
		if ( ( Read16( header + 4 ) >> 8 ) == 3 && ( ( externalAttributes >> 16 ) & 0111 ) != 0 ) {
			std::filesystem::permissions( target,
				std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
				std::filesystem::perm_options::add, ec );
		}
#else
		(void)externalAttributes;
#endif
		files.push_back( name );
	}
	std::sort( files.begin(), files.end() );
	return true;
}

/*
===============================================================================

	Staging

===============================================================================
*/

namespace {

bool EndsWith( const std::string &text, const char *suffix ) {
	const std::size_t length = std::strlen( suffix );
	return text.size() >= length && text.compare( text.size() - length, length, suffix ) == 0;
}

// Fetches one asset into target, checking its size and digest as it arrives.
bool StageAsset( const FetchFn &fetch, const ProgressFn &progress, const std::string &asset, std::uint64_t size,
		const std::string &sha256, const std::filesystem::path &target, std::uint64_t &done, std::uint64_t total,
		StageResult &result ) {
	std::error_code ec;
	std::filesystem::create_directories( target.parent_path(), ec );
	std::filesystem::path partial = target;
	partial += ".partial";
	std::ofstream out( partial, std::ios::binary | std::ios::trunc );
	if ( !out ) {
		result.error = "cannot write " + asset;
		return false;
	}
	idCrypto::SHA256Stream hash;
	std::uint64_t received = 0;
	bool writeFailed = false;
	bool stopped = false;
	bool tooLarge = false;
	const std::uint64_t base = done;
	bool cancelled = false;
	std::string error;
	const bool fetched = fetch( asset, size, [&]( const void *data, std::size_t bytes ) {
		if ( received + bytes > size ) {
			tooLarge = true;	// more than the manifest says: not this asset
			return false;
		}
		out.write( static_cast<const char *>( data ), static_cast<std::streamsize>( bytes ) );
		if ( !out ) {
			writeFailed = true;
			return false;
		}
		hash.Update( data, bytes );
		received += bytes;
		if ( progress && !progress( base + received, total ) ) {
			stopped = true;
			return false;
		}
		return true;
	}, cancelled, error );
	out.close();
	if ( !fetched || stopped || writeFailed || tooLarge ) {
		if ( writeFailed ) {
			result.error = "cannot write " + asset + " (is the disk full?)";
		} else if ( tooLarge ) {
			result.error = asset + " does not match the signed manifest";
		} else if ( stopped || cancelled ) {
			result.cancelled = true;
			result.error = "cancelled";
		} else {
			result.error = asset + ": " + error;
		}
		std::filesystem::remove( partial, ec );
		return false;
	}
	std::uint8_t digest[ idCrypto::SHA256_DIGEST_BYTES ];
	hash.Final( digest );
	if ( received != size || HexEncode( digest, sizeof( digest ) ) != sha256 ) {
		result.error = asset + " does not match the signed manifest";
		std::filesystem::remove( partial, ec );
		return false;
	}
	std::filesystem::rename( partial, target, ec );
	if ( ec ) {
		result.error = "cannot keep " + asset + ": " + ec.message();
		return false;
	}
	done = base + size;
	return true;
}

bool StageInto( const StageRequest &request, UpdatePlan &plan, const FetchFn &fetch, const ProgressFn &progress,
		const ExtractTarFn &extractTar, StageResult &result ) {
	std::error_code ec;
	const std::filesystem::path updateDir = request.install / UPDATE_DIRECTORY;
	const std::filesystem::path download = updateDir / DOWNLOAD_DIRECTORY;
	const std::filesystem::path stage = updateDir / STAGE_DIRECTORY;
	for ( const char *part : { DOWNLOAD_DIRECTORY, STAGE_DIRECTORY, BACKUP_DIRECTORY } ) {
		std::filesystem::remove_all( updateDir / part, ec );
	}
	std::filesystem::remove( updateDir / PLAN_FILE, ec );
	std::filesystem::remove( updateDir / RESULT_FILE, ec );
	std::filesystem::create_directories( stage, ec );

	const auto package = request.manifest.packages.find( request.platformKey );
	if ( package == request.manifest.packages.end() ) {
		result.error = "the release has no update archive for " + request.platformKey;
		return false;
	}
	const PackageEntry &entry = package->second;
	const std::uint64_t total = StageBytes( request );
	std::uint64_t done = 0;
	const std::filesystem::path archive = download / PathFromUtf8( entry.archive );
	if ( !StageAsset( fetch, progress, entry.archive, entry.size, entry.sha256, archive, done, total, result ) ) {
		return false;
	}
	for ( const PackEntry &pack : request.packs ) {
		if ( !StageAsset( fetch, progress, pack.name, pack.size, pack.sha256, download / pack.name, done, total, result ) ) {
			return false;
		}
	}

	std::vector<std::string> files;
	if ( EndsWith( entry.archive, ".zip" ) ) {
		const ContinueFn keepGoing = [&progress, &done, total] { return !progress || progress( done, total ); };
		if ( !ExtractZip( archive, stage, files, result.error, keepGoing ) ) {
			result.cancelled = result.error == "cancelled";
			return false;
		}
	} else if ( EndsWith( entry.archive, ".tar.xz" ) && extractTar ) {
		if ( !extractTar( archive, stage, result.error ) ) {
			return false;
		}
	} else {
		result.error = "unsupported update archive " + entry.archive;
		return false;
	}
	for ( const PackEntry &pack : request.packs ) {
		std::filesystem::create_directories( stage / "baseoq4", ec );
		std::filesystem::rename( download / pack.name, stage / "baseoq4" / pack.name, ec );
		if ( ec ) {
			result.error = "cannot stage " + pack.name + ": " + ec.message();
			return false;
		}
	}
	if ( !ListTreeFiles( stage, files, result.error ) ) {
		return false;
	}
	for ( const std::string &required : request.requiredFiles ) {
		if ( !std::binary_search( files.begin(), files.end(), required ) ) {
			result.error = "the update archive has no " + required;
			return false;
		}
	}

	// Retire numbered openQ4 packs the release no longer lists: the engine
	// would skip them, and they only take space.
	plan.files = files;
	plan.removals.clear();
	for ( int index = 0; index <= 9; ++index ) {
		const std::string name = "pak" + std::to_string( index ) + ".pk4";
		const bool listed = std::any_of( request.manifest.packs.begin(), request.manifest.packs.end(),
			[&name]( const PackEntry &pack ) { return pack.name == name; } );
		if ( !listed && std::filesystem::is_regular_file( request.install / "baseoq4" / name, ec ) ) {
			plan.removals.push_back( "baseoq4/" + name );
		}
	}
	// One game module, game_<arch>, replaced the split game-sp_<arch> and
	// game-mp_<arch> modules (CORE-07). The engine never loads them, and an
	// update planned by 0.15.0 left them in place, so retire them and their
	// debug files unless this update itself carries the name.
	std::vector<std::string> retiredModules;
	for ( std::filesystem::directory_iterator it( request.install / "baseoq4", ec ), end; !ec && it != end; it.increment( ec ) ) {
		const std::string name = PathToUtf8( it->path().filename() );
		const bool split = name.compare( 0, 8, "game-sp_" ) == 0 || name.compare( 0, 8, "game-mp_" ) == 0;
		if ( split && it->is_regular_file( ec ) && !std::binary_search( files.begin(), files.end(), "baseoq4/" + name ) ) {
			retiredModules.push_back( "baseoq4/" + name );
		}
	}
	ec.clear();
	std::sort( retiredModules.begin(), retiredModules.end() );
	plan.removals.insert( plan.removals.end(), retiredModules.begin(), retiredModules.end() );

	std::filesystem::remove_all( download, ec );
	return WriteFileAtomically( updateDir / PLAN_FILE, FormatPlan( plan ), result.error );
}

} // namespace

std::vector<PackEntry> SelectPacks( const Manifest &manifest, const std::function<bool( const PackEntry & )> &isCurrent ) {
	std::vector<PackEntry> selected;
	for ( const PackEntry &pack : manifest.packs ) {
		// Packs that are not bulk travel inside the update archive.
		if ( pack.bulk && !( isCurrent && isCurrent( pack ) ) ) {
			selected.push_back( pack );
		}
	}
	return selected;
}

std::uint64_t StageBytes( const StageRequest &request ) {
	std::uint64_t total = 0;
	const auto package = request.manifest.packages.find( request.platformKey );
	if ( package != request.manifest.packages.end() ) {
		total += package->second.size;
	}
	for ( const PackEntry &pack : request.packs ) {
		total += pack.size;
	}
	return total;
}

StageResult StageRelease( const StageRequest &request, UpdatePlan &plan, const FetchFn &fetch,
		const ProgressFn &progress, const ExtractTarFn &extractTar ) {
	StageResult result;
	result.ok = StageInto( request, plan, fetch, progress, extractTar, result );
	if ( !result.ok ) {
		const std::filesystem::path updateDir = request.install / UPDATE_DIRECTORY;
		std::error_code ec;
		std::filesystem::remove_all( updateDir / DOWNLOAD_DIRECTORY, ec );
		std::filesystem::remove_all( updateDir / STAGE_DIRECTORY, ec );
		std::filesystem::remove( updateDir / PLAN_FILE, ec );
	}
	return result;
}

} // namespace update
} // namespace openq4
