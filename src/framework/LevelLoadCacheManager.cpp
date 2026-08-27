/*
===========================================================================

openQ4 learned level-load manifest and generated-cache coordinator

Copyright (C) 2026 openQ4 contributors

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This is an original openQ4 implementation.  Cache and manifest files are
private derived data: the selected VFS source is opened and identified before
any cached payload can be accepted.

===========================================================================
*/

#include "../idlib/precompiled.h"
#include "LevelLoadCacheManager.h"
#include "LevelLoadCacheFormat.h"
#include "LevelLoadPipeline.h"
#include "File.h"

// Android NDK libc++'s <sstream>/<locale> internals call vsnprintf/snprintf
// and use the INT_MAX macro. idlib breaks both inside standard headers:
// Str.h redirects the printf names by macro, and math/Math.h #undefs
// INT_MAX/INT_MIN to reuse the names as idMath members. Undo the redirect and
// restore the macro before the standard includes. Safe in this one TU: the
// idlib headers are fully included above, and this file uses neither the
// printf names nor idMath::INT_MAX (which the macro would mangle) itself.
#if defined( snprintf )
	#undef snprintf
#endif
#if defined( vsnprintf )
	#undef vsnprintf
#endif
#ifndef INT_MAX
	#define INT_MAX 2147483647
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined( min )
	#undef min
#endif
#if defined( max )
	#undef max
#endif

namespace {

static idCVar com_levelLoadModernization(
	"com_levelLoadModernization", "0", CVAR_BOOL | CVAR_ARCHIVE | CVAR_SYSTEM,
	"enable the experimental level-load cache, preload, and generated-animation paths" );
static idCVar com_levelLoadCache(
	"com_levelLoadCache", "1", CVAR_BOOL | CVAR_ARCHIVE | CVAR_SYSTEM,
	"enable versioned source-authoritative level-load caches and learned manifests" );
static idCVar com_levelLoadCacheWrite(
	"com_levelLoadCacheWrite", "1", CVAR_BOOL | CVAR_ARCHIVE | CVAR_SYSTEM,
	"atomically write learned manifests and generated model/world/collision caches" );
static idCVar com_levelLoadPreload(
	"com_levelLoadPreload", "1", CVAR_BOOL | CVAR_ARCHIVE | CVAR_SYSTEM,
	"replay a matching learned manifest through the bounded level-load job pipeline" );
static idCVar com_levelLoadCacheReport(
	"com_levelLoadCacheReport", "0", CVAR_BOOL | CVAR_ARCHIVE | CVAR_SYSTEM,
	"print per-map manifest, cancellation, byte, and generated-cache diagnostics" );
static idCVar com_levelLoadPreloadMaxEntries(
	"com_levelLoadPreloadMaxEntries", "64", CVAR_INTEGER | CVAR_ARCHIVE | CVAR_SYSTEM,
	"maximum independently opened learned sources admitted per map generation", 1, 64,
	idCmdSystem::ArgCompletion_Integer<1,64> );
static idCVar com_levelLoadPreloadMaxFileMB(
	"com_levelLoadPreloadMaxFileMB", "128", CVAR_INTEGER | CVAR_ARCHIVE | CVAR_SYSTEM,
	"maximum size of one learned preload source in MiB", 1, 512,
	idCmdSystem::ArgCompletion_Integer<1,512> );
static idCVar com_levelLoadPreloadMaxStagingMB(
	"com_levelLoadPreloadMaxStagingMB", "384", CVAR_INTEGER | CVAR_ARCHIVE | CVAR_SYSTEM,
	"maximum aggregate learned preload staging memory in MiB", 8, 2048,
	idCmdSystem::ArgCompletion_Integer<8,2048> );
static idCVar com_levelLoadPreloadMaxDecodeMB(
	"com_levelLoadPreloadMaxDecodeMB", "384", CVAR_INTEGER | CVAR_ARCHIVE | CVAR_SYSTEM,
	"maximum aggregate decoded preload result bytes in MiB", 8, 2048,
	idCmdSystem::ArgCompletion_Integer<8,2048> );
static idCVar com_levelLoadPreloadReadChunkKB(
	"com_levelLoadPreloadReadChunkKB", "256", CVAR_INTEGER | CVAR_ARCHIVE | CVAR_SYSTEM,
	"cooperative cancellation/read accounting chunk size in KiB", 16, 4096,
	idCmdSystem::ArgCompletion_Integer<16,4096> );
static idCVar com_levelLoadPreloadDecodeChunkKB(
	"com_levelLoadPreloadDecodeChunkKB", "256", CVAR_INTEGER | CVAR_ARCHIVE | CVAR_SYSTEM,
	"cooperative cancellation/decode integrity chunk size in KiB", 16, 4096,
	idCmdSystem::ArgCompletion_Integer<16,4096> );

static constexpr std::uint32_t MANIFEST_PRODUCER_VERSION = 2;
static constexpr std::size_t MAX_GENERATED_FILE_BYTES =
	idLevelLoadCache::DEFAULT_MAX_ENVELOPE_BYTES;

std::atomic<std::uint64_t> stagedFileSerial( 1 );

std::string HashHex( const idLevelLoadCache::Hash &hash ) {
	std::ostringstream stream;
	stream << std::hex << std::setfill( '0' );
	for ( const std::uint8_t value : hash ) {
		stream << std::setw( 2 ) << static_cast<unsigned int>( value );
	}
	return stream.str();
}

idLevelLoadCache::Hash HashString( const char *text ) {
	const char *safe = text != nullptr ? text : "";
	return idLevelLoadCache::ComputeHash( safe, std::strlen( safe ) );
}

bool NormalizePath( const char *path, std::string &normalized ) {
	if ( path == nullptr || path[ 0 ] == '\0' ) {
		return false;
	}
	return idLevelLoadCache::NormalizeVirtualPath( path, normalized ).Ok();
}

idLevelLoadCache::CacheKind ToFormatKind( const generatedCacheKind_t kind ) {
	switch ( kind ) {
		case GENERATED_CACHE_RENDER_WORLD:
			return idLevelLoadCache::CacheKind::RENDER_WORLD;
		case GENERATED_CACHE_COLLISION_MODEL:
			return idLevelLoadCache::CacheKind::COLLISION_MODEL;
		case GENERATED_CACHE_RENDER_MODEL:
		default:
			return idLevelLoadCache::CacheKind::RENDER_MODEL;
	}
}

bool IsValidCacheKind( const generatedCacheKind_t kind ) {
	return kind == GENERATED_CACHE_RENDER_MODEL ||
		kind == GENERATED_CACHE_RENDER_WORLD ||
		kind == GENERATED_CACHE_COLLISION_MODEL;
}

idLevelLoadCache::ManifestEntryType ToFormatType( const levelLoadResourceType_t type ) {
	const unsigned int value = static_cast<unsigned int>( type );
	if ( value < static_cast<unsigned int>( LEVEL_LOAD_RESOURCE_RENDER_MODEL ) ||
		value > static_cast<unsigned int>( LEVEL_LOAD_RESOURCE_RAW ) ) {
		return idLevelLoadCache::ManifestEntryType::RAW_FILE;
	}
	return static_cast<idLevelLoadCache::ManifestEntryType>( value );
}

levelLoadResourceType_t ClassifyPath( const std::string &path ) {
	const std::size_t dot = path.find_last_of( '.' );
	const std::string extension = dot == std::string::npos ? std::string() : path.substr( dot );
	if ( extension == ".proc" || extension == ".procc" ||
		extension == ".md5rproc" || extension == ".md5rprocc" ) {
		return LEVEL_LOAD_RESOURCE_WORLD;
	}
	if ( extension == ".cm" || extension == ".cmc" ) {
		return LEVEL_LOAD_RESOURCE_COLLISION;
	}
	if ( extension == ".md5anim" || extension == ".md5animc" ) {
		return LEVEL_LOAD_RESOURCE_ANIMATION;
	}
	if ( extension == ".ase" || extension == ".lwo" || extension == ".ma" ||
		extension == ".flt" || extension == ".md5mesh" || extension == ".md5r" ||
		extension == ".md5rc" || extension == ".md5rmesh" || extension == ".md5rmeshc" ) {
		return LEVEL_LOAD_RESOURCE_RENDER_MODEL;
	}
	if ( extension == ".tga" || extension == ".dds" || extension == ".jpg" ||
		extension == ".jpeg" || extension == ".png" || extension == ".bmp" ) {
		return LEVEL_LOAD_RESOURCE_IMAGE;
	}
	if ( extension == ".wav" || extension == ".ogg" ) {
		return LEVEL_LOAD_RESOURCE_SOUND;
	}
	if ( extension == ".gui" ) {
		return LEVEL_LOAD_RESOURCE_GUI;
	}
	if ( extension == ".skin" ) {
		return LEVEL_LOAD_RESOURCE_SKIN;
	}
	if ( extension == ".mtr" || extension == ".def" || extension == ".snd" ||
		extension == ".fx" || extension == ".af" || extension == ".pda" ) {
		return LEVEL_LOAD_RESOURCE_DECL;
	}
	return LEVEL_LOAD_RESOURCE_RAW;
}

unsigned int DefaultPriority( const levelLoadResourceType_t type ) {
	switch ( type ) {
		case LEVEL_LOAD_RESOURCE_WORLD:
		case LEVEL_LOAD_RESOURCE_COLLISION:
			return static_cast<unsigned int>( idLevelLoadCache::ManifestPriority::CRITICAL );
		case LEVEL_LOAD_RESOURCE_RENDER_MODEL:
		case LEVEL_LOAD_RESOURCE_ANIMATION:
			return static_cast<unsigned int>( idLevelLoadCache::ManifestPriority::HIGH );
		default:
			return static_cast<unsigned int>( idLevelLoadCache::ManifestPriority::NORMAL );
	}
}

bool SemanticTypeUsesCompiledSuffix( const levelLoadResourceType_t type ) {
	return type == LEVEL_LOAD_RESOURCE_ANIMATION ||
		type == LEVEL_LOAD_RESOURCE_RENDER_MODEL || type == LEVEL_LOAD_RESOURCE_WORLD;
}

bool SemanticTypeMatchesByStem( const levelLoadResourceType_t type ) {
	return type == LEVEL_LOAD_RESOURCE_IMAGE || type == LEVEL_LOAD_RESOURCE_SOUND;
}

std::string SemanticCompiledKey( const std::string &semanticName ) {
	std::string compiledName = semanticName;
	compiledName += Lexer::sCompiledFileSuffix.c_str();
	return compiledName;
}

std::string SemanticStemKey( const std::string &path ) {
	const std::size_t dot = path.find_last_of( '.' );
	return dot == std::string::npos ? path : path.substr( 0, dot );
}

bool SemanticPathMatchesSource( const levelLoadResourceType_t type,
		const std::string &semanticName, const std::string &sourcePath ) {
	if ( semanticName == sourcePath ) {
		return true;
	}
	if ( SemanticTypeUsesCompiledSuffix( type ) &&
		SemanticCompiledKey( semanticName ) == sourcePath ) {
		return true;
	}
	if ( !SemanticTypeMatchesByStem( type ) ) {
		return false;
	}
	return SemanticStemKey( semanticName ) == SemanticStemKey( sourcePath );
}

bool BuildSourceIdentity( const std::string &normalizedPath, idFile *file,
		idLevelLoadCache::SourceIdentity &identity ) {
	if ( file == nullptr || file->Length() < 0 ) {
		return false;
	}
	identity.normalizedPath = normalizedPath;
	identity.size = static_cast<std::uint64_t>( file->Length() );
	const int checksum = file->GetContainerChecksum();
	if ( checksum != 0 ) {
		identity.containerKind = idLevelLoadCache::SourceContainerKind::PK4_ARCHIVE;
		identity.containerPk4Checksum = static_cast<std::uint32_t>( checksum );
		identity.timestamp = 0;
	} else {
		identity.containerKind = idLevelLoadCache::SourceContainerKind::LOOSE_FILE;
		identity.containerPk4Checksum = 0;
		identity.timestamp = static_cast<std::uint64_t>( file->Timestamp() );
	}
	return true;
}

int ReadPipelineSource( idFile *file, void *buffer, const int byteCount, void * ) {
	// For PK4 sources the VFS idFile::Read implementation performs archive
	// inflate before returning these member bytes. This is therefore the fused
	// read/decompress stage; DecodePipelineSource below performs worker-safe
	// framing and integrity decode without invoking renderer/game parsers.
	return file != nullptr ? file->Read( buffer, byteCount ) : 0;
}

std::uint64_t UpdatePipelineTransportChecksum( std::uint64_t checksum,
		const unsigned char *bytes, const std::size_t byteCount ) {
	static constexpr std::uint64_t FNV1A64_PRIME = 1099511628211ull;
	for ( std::size_t index = 0; index < byteCount; ++index ) {
		checksum ^= bytes[index];
		checksum *= FNV1A64_PRIME;
	}
	return checksum;
}

idLevelLoadDecodeStatus DecodePipelineSource(
		const idLevelLoadPipelineSource &source, const unsigned char *bytes,
		const std::size_t byteCount, const idLevelLoadDecodeContext &context,
		idLevelLoadDecodeOutput &output, void * ) {
	if ( bytes == nullptr || byteCount == 0 || source.type == 0 ||
		context.generation == 0 || context.chunkBytes == 0 ) {
		return idLevelLoadDecodeStatus::MALFORMED;
	}
	const idLevelLoadDecodeStatus frameStatus = idLevelLoadDecodeSourceFrame(
		source, bytes, byteCount, output, &context );
	if ( frameStatus != idLevelLoadDecodeStatus::COMPLETE ) {
		return frameStatus;
	}

	static constexpr unsigned char HASH_DOMAIN[] = {
		'o', 'p', 'e', 'n', 'Q', '4', '-', 'l', 'e', 'v', 'e', 'l', '-',
		'l', 'o', 'a', 'd', '-', 'd', 'e', 'c', 'o', 'd', 'e', '-', 'v', '1'
	};
	idLevelLoadCache::Hash chain = idLevelLoadCache::ComputeHash(
		HASH_DOMAIN, sizeof( HASH_DOMAIN ) );
	std::uint64_t transportChecksum = 14695981039346656037ull;
	std::size_t offset = 0;
	while ( offset < byteCount ) {
		if ( context.IsCancellationRequested() ) {
			return idLevelLoadDecodeStatus::CANCELLED;
		}
		const std::size_t chunkBytes = std::min( context.chunkBytes, byteCount - offset );
		const idLevelLoadCache::Hash chunkHash = idLevelLoadCache::ComputeHash(
			bytes + offset, chunkBytes );
		std::array<unsigned char, idLevelLoadCache::HASH_BYTES * 2> chained;
		std::copy( chain.begin(), chain.end(), chained.begin() );
		std::copy( chunkHash.begin(), chunkHash.end(),
			chained.begin() + idLevelLoadCache::HASH_BYTES );
		chain = idLevelLoadCache::ComputeHash( chained.data(), chained.size() );
		transportChecksum = UpdatePipelineTransportChecksum(
			transportChecksum, bytes + offset, chunkBytes );
		if ( !context.ReportDecodedBytes( chunkBytes ) ) {
			return context.IsCancellationRequested()
				? idLevelLoadDecodeStatus::CANCELLED
				: idLevelLoadDecodeStatus::MALFORMED;
		}
		offset += chunkBytes;
	}

	std::array<unsigned char, idLevelLoadCache::HASH_BYTES + 20> finalRecord;
	std::copy( chain.begin(), chain.end(), finalRecord.begin() );
	std::size_t cursor = idLevelLoadCache::HASH_BYTES;
	const std::uint64_t decodedByteCount = static_cast<std::uint64_t>( byteCount );
	for ( std::size_t index = 0; index < 8; ++index ) {
		finalRecord[cursor++] = static_cast<unsigned char>(
			decodedByteCount >> ( index * 8 ) );
	}
	for ( std::size_t index = 0; index < 8; ++index ) {
		finalRecord[cursor++] = static_cast<unsigned char>( context.generation >> ( index * 8 ) );
	}
	const std::uint32_t type = source.type;
	for ( std::size_t index = 0; index < 4; ++index ) {
		finalRecord[cursor++] = static_cast<unsigned char>( type >> ( index * 8 ) );
	}
	output.transportChecksum = transportChecksum;
	output.contentIntegrity = idLevelLoadCache::ComputeHash(
		finalRecord.data(), finalRecord.size() );
	return idLevelLoadDecodeStatus::COMPLETE;
}

// Immutable view of bytes owned by a completed pipeline item. The ordinary
// VFS lookup has already run before this object is created, so it retains the
// authoritative file's diagnostics and identity without becoming a second
// source-resolution path.
class idFile_LevelLoadPreloaded final : public idFile {
public:
	idFile_LevelLoadPreloaded( const std::shared_ptr<const std::vector<unsigned char> > &sourceBytes,
			idFile *authoritativeSource )
		: bytes( sourceBytes )
		, position( 0 )
		, timestamp( authoritativeSource != nullptr ? authoritativeSource->Timestamp() : 0 )
		, containerChecksum( authoritativeSource != nullptr
			? authoritativeSource->GetContainerChecksum() : 0 ) {
		if ( authoritativeSource != nullptr ) {
			const char *sourceName = authoritativeSource->GetName();
			const char *sourceFullPath = authoritativeSource->GetFullPath();
			name = sourceName != nullptr ? sourceName : "";
			fullPath = sourceFullPath != nullptr ? sourceFullPath : name.c_str();
		}
	}

	const char *GetName() override { return name.c_str(); }
	const char *GetFullPath() override { return fullPath.c_str(); }
	int Read( void *buffer, int length ) override {
		if ( buffer == nullptr || length <= 0 || bytes == nullptr || position >= bytes->size() ) {
			return 0;
		}
		const std::size_t requested = static_cast<std::size_t>( length );
		const std::size_t available = bytes->size() - position;
		const std::size_t copied = std::min( requested, available );
		memcpy( buffer, bytes->data() + position, copied );
		position += copied;
		return static_cast<int>( copied );
	}
	int Write( const void *, int ) override { return 0; }
	int Length() override {
		return bytes != nullptr ? static_cast<int>( bytes->size() ) : 0;
	}
	ID_TIME_T Timestamp() override { return timestamp; }
	int GetContainerChecksum() const override { return containerChecksum; }
	int Tell() override { return static_cast<int>( position ); }
	int Seek( long offset, fsOrigin_t origin ) override {
		const std::int64_t length = bytes != nullptr
			? static_cast<std::int64_t>( bytes->size() ) : 0;
		std::int64_t requested = 0;
		switch ( origin ) {
			case FS_SEEK_CUR:
				requested = static_cast<std::int64_t>( position ) + offset;
				break;
			case FS_SEEK_END:
				requested = length - offset;
				break;
			case FS_SEEK_SET:
				requested = offset;
				break;
			default:
				return -1;
		}
		if ( requested < 0 ) {
			position = 0;
			return -1;
		}
		if ( requested > length ) {
			position = static_cast<std::size_t>( length );
			return -1;
		}
		position = static_cast<std::size_t>( requested );
		return 0;
	}
	const char *GetDataPtr() const override {
		return bytes != nullptr && !bytes->empty()
			? reinterpret_cast<const char *>( bytes->data() ) : nullptr;
	}

private:
	std::shared_ptr<const std::vector<unsigned char> > bytes;
	std::size_t position;
	idStr name;
	idStr fullPath;
	ID_TIME_T timestamp;
	int containerChecksum;
};

bool ReadWholeFile( idFileSystem *fileSystem, const char *qpath,
		std::vector<std::uint8_t> &bytes, const std::size_t maximumBytes,
		bool *invalidFile = nullptr ) {
	bytes.clear();
	if ( invalidFile != nullptr ) {
		*invalidFile = false;
	}
	if ( fileSystem == nullptr || qpath == nullptr ) {
		return false;
	}
	idStr osPath = fileSystem->RelativePathToOSPath( qpath, "fs_savepath" );
	idFile *file = fileSystem->OpenExplicitFileRead( osPath.c_str() );
	if ( file == nullptr ) {
		return false;
	}
	const int length = file->Length();
	if ( length <= 0 || static_cast<std::size_t>( length ) > maximumBytes ) {
		fileSystem->CloseFile( file );
		if ( invalidFile != nullptr ) {
			*invalidFile = true;
		}
		return false;
	}
	try {
		bytes.resize( static_cast<std::size_t>( length ) );
	} catch ( ... ) {
		fileSystem->CloseFile( file );
		return false;
	}
	const int read = file->Read( bytes.data(), length );
	fileSystem->CloseFile( file );
	if ( read != length ) {
		bytes.clear();
		if ( invalidFile != nullptr ) {
			*invalidFile = true;
		}
		return false;
	}
	return true;
}

bool WriteAtomic( idFileSystem *fileSystem, const std::string &finalPath,
		const std::vector<std::uint8_t> &bytes ) {
	if ( fileSystem == nullptr || bytes.empty() ||
		bytes.size() > static_cast<std::size_t>( std::numeric_limits<int>::max() ) ) {
		return false;
	}
	const std::uint64_t serial = stagedFileSerial.fetch_add( 1, std::memory_order_relaxed );
	std::ostringstream stage;
	stage << finalPath << '.' << serial << ".tmp";
	const std::string stagedPath = stage.str();
	idFile *file = fileSystem->OpenFileWrite( stagedPath.c_str(), "fs_savepath" );
	if ( file == nullptr ) {
		return false;
	}
	const int requested = static_cast<int>( bytes.size() );
	const bool complete = file->Write( bytes.data(), requested ) == requested;
	const bool synced = complete && file->Sync();
	fileSystem->CloseFile( file );
	if ( !synced || !fileSystem->PromoteFile( stagedPath.c_str(), finalPath.c_str(), "fs_savepath" ) ) {
		fileSystem->RemoveFileChecked( stagedPath.c_str(), "fs_savepath" );
		return false;
	}
	return true;
}

std::string ManifestPath( const idLevelLoadCache::ManifestExpectation &expected ) {
	std::ostringstream key;
	key << expected.producerVersion << '|' << expected.mapKey << '|'
		<< HashHex( expected.gameMode ) << '|' << HashHex( expected.entityFilter ) << '|'
		<< HashHex( expected.contentSignature ) << '|' << HashHex( expected.settingsSignature );
	return std::string( "generated/manifests/" ) + HashHex( HashString( key.str().c_str() ) ) + ".oqpm";
}

std::string CachePath( const idLevelLoadCache::EnvelopeExpectation &expected ) {
	const char *subdirectory = "models";
	if ( expected.kind == idLevelLoadCache::CacheKind::RENDER_WORLD ) {
		subdirectory = "worlds";
	} else if ( expected.kind == idLevelLoadCache::CacheKind::COLLISION_MODEL ) {
		subdirectory = "collision";
	}
	std::ostringstream key;
	key << static_cast<unsigned int>( expected.kind ) << '|' << expected.parserVersion << '|'
		<< expected.source.normalizedPath << '|' << expected.source.size << '|'
		<< expected.source.timestamp << '|' << static_cast<unsigned int>( expected.source.containerKind ) << '|'
		<< expected.source.containerPk4Checksum << '|' << HashHex( expected.contentSignature ) << '|'
		<< HashHex( expected.settingsSignature );
	return std::string( "generated/" ) + subdirectory + '/' +
		HashHex( HashString( key.str().c_str() ) ) + ".oqgc";
}

} // namespace

struct idLevelLoadCacheManager::Impl {
	struct SemanticHint {
		levelLoadResourceType_t type;
		std::string normalizedName;
		std::vector<std::uint8_t> options;
		unsigned int flags;
		unsigned int priority;
	};

	explicit Impl( idFileSystem *owner )
		: fileSystem( owner )
		, generation( 0 )
		, firstUseOrder( 0 )
		, recording( false )
		, completedGeneration( false )
		, manifestMatched( false )
		, manifestRemoved( false )
		, generatedHits( 0 )
		, generatedMisses( 0 )
		, generatedWrites( 0 )
		, generatedCorruptions( 0 ) {
	}

	idFileSystem *fileSystem;
	idLevelLoadPipeline pipeline;
	std::uint64_t generation;
	std::uint64_t firstUseOrder;
	bool recording;
	bool completedGeneration;
	bool manifestMatched;
	bool manifestRemoved;
	std::atomic<std::uint64_t> generatedHits;
	std::atomic<std::uint64_t> generatedMisses;
	std::atomic<std::uint64_t> generatedWrites;
	std::atomic<std::uint64_t> generatedCorruptions;
	idLevelLoadCache::Manifest learned;
	idLevelLoadCache::ManifestExpectation expectation;
	std::vector<SemanticHint> hints;
	// Every opened source used to be matched against the whole hint vector,
	// and each recorded hint re-opened its own source, so recording a level's
	// media was quadratic in the hint count.  These indexes answer the same
	// three match rules directly.  A key always maps to the newest hint that
	// owns it, and lookup takes the highest matching hint index, which is the
	// hint the old reverse scan would have stopped on.
	std::unordered_map<std::string, std::size_t> hintExactIndex;
	std::unordered_map<std::string, std::size_t> hintCompiledIndex;
	std::unordered_map<std::string, std::size_t> hintStemIndex;

	// Returns true when this exact semantic name had already been indexed
	// during this generation, meaning its source identity is already learned.
	bool IndexHint( const std::size_t hintIndex ) {
		const SemanticHint &hint = hints[ hintIndex ];
		const bool alreadyIndexed = hintExactIndex.find( hint.normalizedName ) != hintExactIndex.end();
		hintExactIndex[ hint.normalizedName ] = hintIndex;
		if ( SemanticTypeUsesCompiledSuffix( hint.type ) ) {
			hintCompiledIndex[ SemanticCompiledKey( hint.normalizedName ) ] = hintIndex;
		}
		if ( SemanticTypeMatchesByStem( hint.type ) ) {
			hintStemIndex[ SemanticStemKey( hint.normalizedName ) ] = hintIndex;
		}
		return alreadyIndexed;
	}

	void ClearHints() {
		hints.clear();
		hintExactIndex.clear();
		hintCompiledIndex.clear();
		hintStemIndex.clear();
	}

	const SemanticHint *FindHintForSource( const std::string &sourcePath ) const {
		bool found = false;
		std::size_t best = 0;
		const auto consider = [&]( const std::unordered_map<std::string, std::size_t> &index,
				const std::string &key ) {
			const auto it = index.find( key );
			if ( it == index.end() ) {
				return;
			}
			if ( !found || it->second > best ) {
				found = true;
				best = it->second;
			}
		};
		consider( hintExactIndex, sourcePath );
		consider( hintCompiledIndex, sourcePath );
		consider( hintStemIndex, SemanticStemKey( sourcePath ) );
		return found ? &hints[ best ] : nullptr;
	}
	std::mutex mutex;

	void ClosePipelineFiles() {
		std::vector<idFile *> files;
		pipeline.DrainOpenFiles( files );
		for ( idFile *file : files ) {
			if ( file != nullptr ) {
				fileSystem->CloseFile( file );
			}
		}
	}

	void StopPipeline( const bool cancel ) {
		if ( cancel ) {
			pipeline.CancelAndWait();
		} else {
			pipeline.Wait();
		}
		ClosePipelineFiles();
	}

	bool WriteLearnedManifest() {
		if ( !com_levelLoadModernization.GetBool() || !com_levelLoadCache.GetBool() ||
			!com_levelLoadCacheWrite.GetBool() ||
			learned.mapKey.empty() ) {
			return false;
		}
		idLevelLoadCache::Manifest snapshot;
		{
			std::lock_guard<std::mutex> lock( mutex );
			snapshot = learned;
		}
		std::vector<std::uint8_t> encoded;
		const idLevelLoadCache::Result result = idLevelLoadCache::EncodeManifest( snapshot, encoded );
		if ( !result ) {
			common->Warning( "Could not encode level-load manifest: %s (%s)",
				idLevelLoadCache::StatusName( result.status ), result.diagnostic.c_str() );
			return false;
		}
		return WriteAtomic( fileSystem, ManifestPath( expectation ), encoded );
	}

	idLevelLoadCache::EnvelopeExpectation BuildExpectation(
			const generatedCacheKind_t kind, const std::string &normalizedPath,
			const unsigned int parserVersion, const char *settingsKey,
			const char *contentKey, idFile *source ) const {
		idLevelLoadCache::EnvelopeExpectation result;
		result.kind = ToFormatKind( kind );
		result.parserVersion = parserVersion;
		BuildSourceIdentity( normalizedPath, source, result.source );
		result.contentSignature = HashString( contentKey );
		result.settingsSignature = HashString( settingsKey );
		return result;
	}
};

idLevelLoadCacheManager::idLevelLoadCacheManager( idFileSystem *fileSystem )
	: impl( new Impl( fileSystem ) ) {
}

idLevelLoadCacheManager::~idLevelLoadCacheManager() {
	Cancel();
}

void idLevelLoadCacheManager::Begin( const char *mapKey, const char *gameMode,
		const char *entityFilter, const char *contentKey, const char *settingsKey ) {
	Cancel();
	if ( impl->fileSystem == nullptr || !com_levelLoadModernization.GetBool() ||
			!com_levelLoadCache.GetBool() ) {
		return;
	}

	std::string normalizedMap;
	if ( !NormalizePath( mapKey, normalizedMap ) ) {
		common->Warning( "Level-load cache rejected invalid map key '%s'", mapKey != nullptr ? mapKey : "<null>" );
		return;
	}

	impl->generation++;
	impl->firstUseOrder = 0;
	impl->completedGeneration = false;
	impl->learned = idLevelLoadCache::Manifest();
	impl->learned.producerVersion = MANIFEST_PRODUCER_VERSION;
	impl->learned.mapKey = normalizedMap;
	impl->learned.gameMode = HashString( gameMode );
	impl->learned.entityFilter = HashString( entityFilter );
	impl->learned.contentSignature = HashString( contentKey );
	impl->learned.settingsSignature = HashString( settingsKey );
	impl->expectation.producerVersion = impl->learned.producerVersion;
	impl->expectation.mapKey = impl->learned.mapKey;
	impl->expectation.gameMode = impl->learned.gameMode;
	impl->expectation.entityFilter = impl->learned.entityFilter;
	impl->expectation.contentSignature = impl->learned.contentSignature;
	impl->expectation.settingsSignature = impl->learned.settingsSignature;
	impl->ClearHints();
	impl->manifestMatched = false;
	impl->manifestRemoved = false;
	impl->generatedHits.store( 0, std::memory_order_relaxed );
	impl->generatedMisses.store( 0, std::memory_order_relaxed );
	impl->generatedWrites.store( 0, std::memory_order_relaxed );
	impl->generatedCorruptions.store( 0, std::memory_order_relaxed );

	const std::size_t replayMaxEntries =
		static_cast<std::size_t>( com_levelLoadPreloadMaxEntries.GetInteger() );
	const std::uint64_t replayMaxSourceBytes =
		static_cast<std::uint64_t>( com_levelLoadPreloadMaxFileMB.GetInteger() ) * 1024ull * 1024ull;
	const std::uint64_t replayMaxTotalBytes =
		static_cast<std::uint64_t>( com_levelLoadPreloadMaxStagingMB.GetInteger() ) * 1024ull * 1024ull;
	const std::uint64_t replayMaxDecodedBytes =
		static_cast<std::uint64_t>( com_levelLoadPreloadMaxDecodeMB.GetInteger() ) * 1024ull * 1024ull;
	std::uint64_t replayAdmittedBytes = 0;

	std::vector<idLevelLoadPipelineSource> sources;
	const std::string manifestPath = ManifestPath( impl->expectation );
	if ( com_levelLoadPreload.GetBool() ) {
		std::vector<std::uint8_t> encoded;
		bool invalidManifestFile = false;
		if ( ReadWholeFile( impl->fileSystem, manifestPath.c_str(), encoded,
				idLevelLoadCache::DEFAULT_MAX_MANIFEST_BYTES, &invalidManifestFile ) ) {
			idLevelLoadCache::Manifest replay;
			const idLevelLoadCache::Result decoded = idLevelLoadCache::DecodeManifestForKey(
				encoded, impl->expectation, replay );
			if ( !decoded ) {
				impl->fileSystem->RemoveFileChecked( manifestPath.c_str(), "fs_savepath" );
				impl->manifestRemoved = true;
				if ( com_levelLoadCacheReport.GetBool() ) {
					common->Printf( "Level-load manifest ignored and removed (%s at %u): %s\n",
						idLevelLoadCache::StatusName( decoded.status ),
						static_cast<unsigned int>( decoded.offset ), decoded.diagnostic.c_str() );
				}
			} else {
				impl->manifestMatched = true;
				std::set<std::string> admittedIdentities;
				for ( const idLevelLoadCache::ManifestEntry &entry : replay.entries ) {
					// Decode canonicalizes entries into replay priority/first-use order.
					// Apply all admission budgets before opening a handle so an otherwise
					// valid but very large manifest cannot exhaust process resources.
					if ( sources.size() >= replayMaxEntries ) {
						break;
					}
					if ( entry.source.size == 0 || entry.source.size > replayMaxSourceBytes ||
						entry.source.size > replayMaxTotalBytes - replayAdmittedBytes ) {
						continue;
					}
					std::ostringstream identityKey;
					identityKey << entry.source.normalizedPath << '|' << entry.source.size << '|'
						<< entry.source.timestamp << '|' << entry.source.containerPk4Checksum;
					if ( !admittedIdentities.insert( identityKey.str() ).second ) {
						continue;
					}
					idFile *file = impl->fileSystem->OpenFileRead(
						entry.source.normalizedPath.c_str(), false );
					if ( file == nullptr ) {
						continue;
					}
					idLevelLoadCache::SourceIdentity current;
					if ( !BuildSourceIdentity( entry.source.normalizedPath, file, current ) ||
						current != entry.source ) {
						impl->fileSystem->CloseFile( file );
						continue;
					}
					idLevelLoadPipelineSource source;
					source.normalizedPath = entry.source.normalizedPath;
					source.type = static_cast<std::uint32_t>( entry.type );
					source.priority = static_cast<std::uint32_t>( entry.priority );
					source.firstUseOrder = entry.firstUseOrder > std::numeric_limits<std::uint32_t>::max()
						? std::numeric_limits<std::uint32_t>::max()
						: static_cast<std::uint32_t>( entry.firstUseOrder );
					source.file = file;
					source.sourceBytes = entry.source.size;
					source.sourceTimestamp = entry.source.timestamp;
					source.containerChecksum = entry.source.containerPk4Checksum;
					sources.push_back( std::move( source ) );
					replayAdmittedBytes += entry.source.size;
				}
			}
		} else if ( invalidManifestFile ) {
			impl->fileSystem->RemoveFileChecked( manifestPath.c_str(), "fs_savepath" );
			impl->manifestRemoved = true;
		}
	}

	idLevelLoadPipelineConfig config;
	config.maxEntries = replayMaxEntries;
	config.maxSourceBytes = replayMaxSourceBytes;
	config.maxTotalBytes = replayMaxTotalBytes;
	config.maxDecodedBytes = replayMaxDecodedBytes;
	config.readChunkBytes = static_cast<std::size_t>( com_levelLoadPreloadReadChunkKB.GetInteger() ) * 1024u;
	config.decodeChunkBytes = static_cast<std::size_t>( com_levelLoadPreloadDecodeChunkKB.GetInteger() ) * 1024u;
	if ( !impl->pipeline.Begin( impl->generation, config, std::move( sources ),
			&ReadPipelineSource, nullptr, &DecodePipelineSource ) ) {
		impl->ClosePipelineFiles();
		common->Warning( "Could not start level-load preload generation %llu",
			static_cast<unsigned long long>( impl->generation ) );
	}
	impl->recording = true;
}

void idLevelLoadCacheManager::Finish( const bool successful ) {
	if ( impl == nullptr ) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock( impl->mutex );
		impl->recording = false;
	}
	impl->StopPipeline( !successful );
	const idLevelLoadPipelineMetrics metrics = impl->pipeline.GetMetrics();

	const bool wroteManifest = successful && impl->WriteLearnedManifest();

	if ( com_levelLoadCacheReport.GetBool() && !impl->learned.mapKey.empty() ) {
		common->Printf(
			"Level-load cache: map=%s learned=%u manifestMatch=%d manifestRemoved=%d replayed=%llu rejected=%llu failed=%llu cancelled=%llu readInflatedBytes=%llu peakStaging=%llu decodeStarted=%llu decoded=%llu decodeFailed=%llu decodeCancelled=%llu decodeBudgetRejected=%llu decodeBytes=%llu decodeAdmitted=%llu decodeBudget=%llu peakDecoded=%llu acquired=%llu generatedHits=%llu generatedMisses=%llu generatedWrites=%llu generatedCorrupt=%llu syncFallback=%d wrote=%d\n",
			impl->learned.mapKey.c_str(),
			static_cast<unsigned int>( impl->learned.entries.size() ),
			impl->manifestMatched ? 1 : 0,
			impl->manifestRemoved ? 1 : 0,
			static_cast<unsigned long long>( metrics.completedEntries ),
			static_cast<unsigned long long>( metrics.rejectedEntries ),
			static_cast<unsigned long long>( metrics.failedEntries ),
			static_cast<unsigned long long>( metrics.cancelledEntries ),
			static_cast<unsigned long long>( metrics.bytesRead ),
			static_cast<unsigned long long>( metrics.peakStagingBytes ),
			static_cast<unsigned long long>( metrics.decodeStartedEntries ),
			static_cast<unsigned long long>( metrics.decodeCompletedEntries ),
			static_cast<unsigned long long>( metrics.decodeFailedEntries ),
			static_cast<unsigned long long>( metrics.decodeCancelledEntries ),
			static_cast<unsigned long long>( metrics.decodeBudgetRejectedEntries ),
			static_cast<unsigned long long>( metrics.bytesDecoded ),
			static_cast<unsigned long long>( metrics.admittedDecodedBytes ),
			static_cast<unsigned long long>( metrics.decodeBudgetBytes ),
			static_cast<unsigned long long>( metrics.peakDecodedBytes ),
			static_cast<unsigned long long>( metrics.cacheHits ),
			static_cast<unsigned long long>( impl->generatedHits.load( std::memory_order_relaxed ) ),
			static_cast<unsigned long long>( impl->generatedMisses.load( std::memory_order_relaxed ) ),
			static_cast<unsigned long long>( impl->generatedWrites.load( std::memory_order_relaxed ) ),
			static_cast<unsigned long long>( impl->generatedCorruptions.load( std::memory_order_relaxed ) ),
			metrics.synchronousFallback ? 1 : 0, wroteManifest ? 1 : 0 );
	}
	if ( successful ) {
		// Continue learning source-backed resources that first become visible or
		// are demanded after map publication. Unload writes that completed
		// generation once more before teardown.
		std::lock_guard<std::mutex> lock( impl->mutex );
		impl->completedGeneration = true;
		impl->recording = true;
	} else {
		impl->ClearHints();
		impl->learned = idLevelLoadCache::Manifest();
		impl->completedGeneration = false;
	}
}

void idLevelLoadCacheManager::Release() {
	if ( impl != nullptr ) {
		impl->pipeline.Reset();
	}
}

void idLevelLoadCacheManager::Cancel() {
	if ( impl == nullptr ) {
		return;
	}
	bool hadGeneration = false;
	{
		std::lock_guard<std::mutex> lock( impl->mutex );
		hadGeneration = impl->recording || !impl->learned.mapKey.empty();
		impl->recording = false;
	}
	impl->StopPipeline( true );
	const bool wroteFinalManifest = impl->completedGeneration && impl->WriteLearnedManifest();
	if ( hadGeneration && com_levelLoadCacheReport.GetBool() ) {
		const idLevelLoadPipelineMetrics metrics = impl->pipeline.GetMetrics();
		common->Printf( "Level-load cache generation %llu closed and joined (%llu completed, %llu cancelled, finalManifest=%d)\n",
			static_cast<unsigned long long>( impl->generation ),
			static_cast<unsigned long long>( metrics.completedEntries ),
			static_cast<unsigned long long>( metrics.cancelledEntries ),
			wroteFinalManifest ? 1 : 0 );
	}
	impl->pipeline.Reset();
	impl->ClearHints();
	impl->learned = idLevelLoadCache::Manifest();
	impl->completedGeneration = false;
}

void idLevelLoadCacheManager::RecordSemantic( const levelLoadResourceType_t type,
		const char *name, const char *options, const unsigned int flags,
		const unsigned int priority ) {
	std::string normalized;
	if ( impl == nullptr || !NormalizePath( name, normalized ) ) {
		return;
	}
	bool alreadyResolved = false;
	{
		std::lock_guard<std::mutex> lock( impl->mutex );
		if ( !impl->recording || impl->hints.size() >= 65536 ) {
			return;
		}
		Impl::SemanticHint hint;
		hint.type = type;
		hint.normalizedName = normalized;
		hint.flags = flags;
		hint.priority = std::min<unsigned int>( priority,
			static_cast<unsigned int>( idLevelLoadCache::ManifestPriority::CRITICAL ) );
		if ( options != nullptr ) {
			const std::size_t length = std::min<std::size_t>( std::strlen( options ),
				idLevelLoadCache::DEFAULT_MAX_ENTRY_OPTIONS_BYTES );
			hint.options.assign( options, options + length );
		}
		impl->hints.push_back( std::move( hint ) );
		alreadyResolved = impl->IndexHint( impl->hints.size() - 1 );
	}

	// A semantic lookup may be satisfied by an already resident manager object
	// and therefore perform no source I/O this generation. Resolve only the
	// identity (without reading bytes) so those real uses are still learned.
	// Media is requested far more often than it is unique, and on a cold file
	// cache each speculative open is a real disk touch, so a name that has
	// already been resolved this generation is not resolved again: the second
	// open would yield the identity the first one already learned.
	if ( alreadyResolved ) {
		return;
	}
	idFile *source = impl->fileSystem->OpenFileRead( normalized.c_str(), false );
	if ( source != nullptr ) {
		impl->fileSystem->CloseFile( source );
	}
}

void idLevelLoadCacheManager::RecordOpenedSource( const char *path, idFile *source ) {
	std::string normalized;
	if ( impl == nullptr || source == nullptr || !NormalizePath( path, normalized ) ||
		normalized.rfind( "generated/", 0 ) == 0 ) {
		return;
	}
	idLevelLoadCache::SourceIdentity identity;
	if ( !BuildSourceIdentity( normalized, source, identity ) ) {
		return;
	}

	std::lock_guard<std::mutex> lock( impl->mutex );
	if ( !impl->recording || impl->learned.entries.size() >= idLevelLoadCache::DEFAULT_MAX_MANIFEST_ENTRIES ) {
		return;
	}
	levelLoadResourceType_t type = ClassifyPath( normalized );
	unsigned int priority = DefaultPriority( type );
	unsigned int flags = 0;
	std::vector<std::uint8_t> options;
	if ( const Impl::SemanticHint *hint = impl->FindHintForSource( normalized ) ) {
		type = hint->type;
		priority = hint->priority;
		flags = hint->flags;
		options = hint->options;
	}
	idLevelLoadCache::ManifestEntry entry;
	entry.type = ToFormatType( type );
	entry.priority = static_cast<idLevelLoadCache::ManifestPriority>(
		std::min<unsigned int>( priority,
			static_cast<unsigned int>( idLevelLoadCache::ManifestPriority::CRITICAL ) ) );
	entry.firstUseOrder = impl->firstUseOrder++;
	entry.useCount = 1;
	entry.flags = flags;
	entry.normalizedName = normalized;
	entry.source = identity;
	entry.options = std::move( options );
	impl->learned.entries.push_back( std::move( entry ) );
}

idFile *idLevelLoadCacheManager::OpenPreloadedSource( const char *path,
		idFile *authoritativeSource ) {
	std::string normalized;
	idLevelLoadCache::SourceIdentity identity;
	if ( impl == nullptr || authoritativeSource == nullptr ||
		!NormalizePath( path, normalized ) ||
		!BuildSourceIdentity( normalized, authoritativeSource, identity ) ||
		identity.size > static_cast<std::uint64_t>( ( std::numeric_limits<int>::max )() ) ) {
		return nullptr;
	}

	const std::uint32_t expectedType = static_cast<std::uint32_t>( ClassifyPath( normalized ) );
	std::shared_ptr<const idLevelLoadDecodedSource> decoded = impl->pipeline.Acquire(
		impl->generation, normalized.c_str(), expectedType, identity.size, identity.timestamp,
		identity.containerPk4Checksum );
	if ( decoded == nullptr || decoded->bytes == nullptr ||
		decoded->bytes->size() != identity.size || decoded->generation != impl->generation ||
		decoded->type != expectedType ) {
		return nullptr;
	}
	try {
		return new idFile_LevelLoadPreloaded( decoded->bytes, authoritativeSource );
	} catch ( ... ) {
		return nullptr;
	}
}

idFile *idLevelLoadCacheManager::OpenGeneratedCacheRead(
		const generatedCacheKind_t kind, const char *sourcePath,
		const unsigned int parserVersion, const char *settingsKey,
		const char *contentKey ) {
	if ( impl == nullptr || !com_levelLoadModernization.GetBool() ||
		!com_levelLoadCache.GetBool() || !IsValidCacheKind( kind ) ||
		parserVersion == 0 ) {
		return nullptr;
	}
	std::string normalized;
	if ( !NormalizePath( sourcePath, normalized ) ) {
		return nullptr;
	}

	// Resolve the authoritative retail/mod source first.  The generated file is
	// never searched through VFS and therefore cannot affect pure negotiation.
	idFile *source = impl->fileSystem->OpenFileRead( normalized.c_str(), false );
	if ( source == nullptr ) {
		impl->generatedMisses.fetch_add( 1, std::memory_order_relaxed );
		return nullptr;
	}
	const idLevelLoadCache::EnvelopeExpectation expected = impl->BuildExpectation(
		kind, normalized, parserVersion, settingsKey, contentKey, source );
	impl->fileSystem->CloseFile( source );
	const std::string cachePath = CachePath( expected );
	std::vector<std::uint8_t> encoded;
	bool invalidCacheFile = false;
	if ( !ReadWholeFile( impl->fileSystem, cachePath.c_str(), encoded,
			MAX_GENERATED_FILE_BYTES, &invalidCacheFile ) ) {
		impl->generatedMisses.fetch_add( 1, std::memory_order_relaxed );
		if ( invalidCacheFile ) {
			impl->fileSystem->RemoveFileChecked( cachePath.c_str(), "fs_savepath" );
			impl->generatedCorruptions.fetch_add( 1, std::memory_order_relaxed );
		}
		return nullptr;
	}

	idLevelLoadCache::CacheEnvelope envelope;
	idLevelLoadCache::Result result = idLevelLoadCache::DecodeEnvelopeForKey( encoded, expected, envelope );
	if ( result && envelope.codec == idLevelLoadCache::CompressionCodec::NONE ) {
		result = idLevelLoadCache::ValidateDecodedPayload( envelope,
			envelope.payload.data(), envelope.payload.size() );
	} else if ( result ) {
		result.status = idLevelLoadCache::Status::INVALID_ENUM_VALUE;
		result.diagnostic = "runtime does not support this generated-cache codec";
	}
	if ( !result || envelope.payload.size() > static_cast<std::size_t>( std::numeric_limits<int>::max() ) ) {
		impl->fileSystem->RemoveFileChecked( cachePath.c_str(), "fs_savepath" );
		impl->generatedMisses.fetch_add( 1, std::memory_order_relaxed );
		impl->generatedCorruptions.fetch_add( 1, std::memory_order_relaxed );
		if ( com_levelLoadCacheReport.GetBool() ) {
			common->Printf( "Generated cache ignored and removed for %s (%s): %s\n",
				normalized.c_str(), idLevelLoadCache::StatusName( result.status ),
				result.diagnostic.c_str() );
		}
		return nullptr;
	}

	idFile_Memory *memory = new idFile_Memory( cachePath.c_str() );
	if ( !envelope.payload.empty() && memory->Write( envelope.payload.data(),
			static_cast<int>( envelope.payload.size() ) ) != static_cast<int>( envelope.payload.size() ) ) {
		delete memory;
		return nullptr;
	}
	memory->MakeReadOnly();
	impl->generatedHits.fetch_add( 1, std::memory_order_relaxed );
	return memory;
}

bool idLevelLoadCacheManager::WriteGeneratedCache( const generatedCacheKind_t kind,
		const char *sourcePath, const unsigned int parserVersion,
		const char *settingsKey, const void *payload, const unsigned int payloadBytes,
		const char *contentKey ) {
	if ( impl == nullptr || !com_levelLoadModernization.GetBool() ||
		!com_levelLoadCache.GetBool() || !com_levelLoadCacheWrite.GetBool() ||
		!IsValidCacheKind( kind ) || parserVersion == 0 ||
		( payload == nullptr && payloadBytes != 0 ) || payloadBytes > idLevelLoadCache::DEFAULT_MAX_PAYLOAD_BYTES ) {
		return false;
	}
	std::string normalized;
	if ( !NormalizePath( sourcePath, normalized ) ) {
		return false;
	}
	idFile *source = impl->fileSystem->OpenFileRead( normalized.c_str(), false );
	if ( source == nullptr ) {
		return false;
	}
	const idLevelLoadCache::EnvelopeExpectation expected = impl->BuildExpectation(
		kind, normalized, parserVersion, settingsKey, contentKey, source );
	impl->fileSystem->CloseFile( source );

	idLevelLoadCache::CacheEnvelope envelope;
	envelope.kind = expected.kind;
	envelope.parserVersion = expected.parserVersion;
	envelope.codec = idLevelLoadCache::CompressionCodec::NONE;
	envelope.source = expected.source;
	envelope.contentSignature = expected.contentSignature;
	envelope.settingsSignature = expected.settingsSignature;
	envelope.decodedPayloadBytes = payloadBytes;
	envelope.decodedPayloadHash = idLevelLoadCache::ComputeHash( payload, payloadBytes );
	try {
		const std::uint8_t *begin = static_cast<const std::uint8_t *>( payload );
		if ( payloadBytes != 0 ) {
			envelope.payload.assign( begin, begin + payloadBytes );
		}
	} catch ( ... ) {
		return false;
	}
	std::vector<std::uint8_t> encoded;
	const idLevelLoadCache::Result result = idLevelLoadCache::EncodeEnvelope( envelope, encoded );
	if ( !result ) {
		common->Warning( "Could not encode generated cache for %s: %s (%s)",
			normalized.c_str(), idLevelLoadCache::StatusName( result.status ),
			result.diagnostic.c_str() );
		return false;
	}
	const bool wrote = WriteAtomic( impl->fileSystem, CachePath( expected ), encoded );
	if ( wrote ) {
		impl->generatedWrites.fetch_add( 1, std::memory_order_relaxed );
	}
	if ( com_levelLoadCacheReport.GetBool() ) {
		common->Printf( "Generated cache %s for %s (%u payload bytes)\n",
			wrote ? "stored" : "write failed", normalized.c_str(), payloadBytes );
	}
	return wrote;
}

void idLevelLoadCacheManager::DiscardGeneratedCache( const generatedCacheKind_t kind,
		const char *sourcePath, const unsigned int parserVersion,
		const char *settingsKey, const char *contentKey ) {
	if ( impl == nullptr || !IsValidCacheKind( kind ) || parserVersion == 0 ) {
		return;
	}
	std::string normalized;
	if ( !NormalizePath( sourcePath, normalized ) ) {
		return;
	}
	idFile *source = impl->fileSystem->OpenFileRead( normalized.c_str(), false );
	if ( source == nullptr ) {
		return;
	}
	const idLevelLoadCache::EnvelopeExpectation expected = impl->BuildExpectation(
		kind, normalized, parserVersion, settingsKey, contentKey, source );
	impl->fileSystem->CloseFile( source );
	const std::string cachePath = CachePath( expected );
	impl->fileSystem->RemoveFileChecked( cachePath.c_str(), "fs_savepath" );
	impl->generatedCorruptions.fetch_add( 1, std::memory_order_relaxed );
	if ( com_levelLoadCacheReport.GetBool() ) {
		common->Printf( "Generated cache owner payload rejected and removed for %s\n",
			normalized.c_str() );
	}
}
