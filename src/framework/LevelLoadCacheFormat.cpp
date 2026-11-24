/*
===========================================================================

openQ4 hardened level-load cache and preload-manifest format

Copyright (C) 2026 openQ4 contributors

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This is an original openQ4 implementation.  No engine structures are written
to disk: the wire representation is explicit, little endian, bounded, and
protected by SHA-256.

===========================================================================
*/

#include "LevelLoadCacheFormat.h"
#include "../idlib/CryptoHash.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

// The engine's Windows PCH includes windows.h.  Its legacy min/max macros are
// incompatible with numeric_limits<T>::max() and std::min/std::max.
#if defined( min )
	#undef min
#endif
#if defined( max )
	#undef max
#endif

namespace idLevelLoadCache {
namespace {

constexpr std::uint32_t MakeMagic( const char a, const char b, const char c, const char d ) {
	return static_cast<std::uint32_t>( static_cast<unsigned char>( a ) ) |
		( static_cast<std::uint32_t>( static_cast<unsigned char>( b ) ) << 8 ) |
		( static_cast<std::uint32_t>( static_cast<unsigned char>( c ) ) << 16 ) |
		( static_cast<std::uint32_t>( static_cast<unsigned char>( d ) ) << 24 );
}

static constexpr std::uint32_t CACHE_MAGIC = MakeMagic( 'O', 'Q', '4', 'C' );
static constexpr std::uint32_t CACHE_END_MAGIC = MakeMagic( 'E', 'C', '4', 'O' );
static constexpr std::uint32_t MANIFEST_MAGIC = MakeMagic( 'O', 'Q', '4', 'M' );
static constexpr std::uint32_t MANIFEST_END_MAGIC = MakeMagic( 'E', 'M', '4', 'O' );
static constexpr std::size_t TRAILER_BYTES = HASH_BYTES + sizeof( std::uint32_t );
static constexpr std::size_t MIN_MANIFEST_ENTRY_BYTES = 62;

Result Success() {
	return Result{};
}

Result Failure( const Status status, const std::size_t offset, const char *diagnostic ) {
	Result result;
	result.status = status;
	result.offset = offset;
	result.diagnostic = diagnostic;
	return result;
}

bool HashIsZero( const Hash &hash ) {
	std::uint8_t combined = 0;
	for ( const std::uint8_t value : hash ) {
		combined |= value;
	}
	return combined == 0;
}

bool HashesEqual( const Hash &left, const Hash &right ) {
	std::uint8_t difference = 0;
	for ( std::size_t index = 0; index < HASH_BYTES; ++index ) {
		difference |= static_cast<std::uint8_t>( left[index] ^ right[index] );
	}
	return difference == 0;
}

bool AddWithoutOverflow( const std::size_t left, const std::size_t right,
		std::size_t &sum ) {
	if ( right > std::numeric_limits<std::size_t>::max() - left ) {
		return false;
	}
	sum = left + right;
	return true;
}

class Writer {
public:
	explicit Writer( const std::size_t maximumBytes ) : maximum( maximumBytes ) {
	}

	bool WriteU32( const std::uint32_t value ) {
		const std::uint8_t bytes[4] = {
			static_cast<std::uint8_t>( value ),
			static_cast<std::uint8_t>( value >> 8 ),
			static_cast<std::uint8_t>( value >> 16 ),
			static_cast<std::uint8_t>( value >> 24 )
		};
		return WriteBytes( bytes, sizeof( bytes ) );
	}

	bool WriteU64( const std::uint64_t value ) {
		std::uint8_t bytes[8];
		for ( unsigned int index = 0; index < 8; ++index ) {
			bytes[index] = static_cast<std::uint8_t>( value >> ( index * 8u ) );
		}
		return WriteBytes( bytes, sizeof( bytes ) );
	}

	bool WriteHash( const Hash &hash ) {
		return WriteBytes( hash.data(), hash.size() );
	}

	bool WriteString( const std::string &value ) {
		if ( value.size() > std::numeric_limits<std::uint32_t>::max() ) {
			return false;
		}
		return WriteU32( static_cast<std::uint32_t>( value.size() ) ) &&
			WriteBytes( value.data(), value.size() );
	}

	bool WriteBytes( const void *source, const std::size_t bytes ) {
		if ( bytes > maximum - std::min( maximum, data.size() ) ) {
			return false;
		}
		if ( bytes == 0 ) {
			return true;
		}
		const auto *begin = static_cast<const std::uint8_t *>( source );
		data.insert( data.end(), begin, begin + bytes );
		return true;
	}

	std::size_t Size() const {
		return data.size();
	}

	const std::vector<std::uint8_t> &Data() const {
		return data;
	}

	std::vector<std::uint8_t> Take() {
		return std::move( data );
	}

private:
	std::size_t maximum;
	std::vector<std::uint8_t> data;
};

class Reader {
public:
	Reader( const void *source, const std::size_t sourceBytes ) :
		data( static_cast<const std::uint8_t *>( source ) ), size( sourceBytes ) {
	}

	bool ReadU32( std::uint32_t &value, const char *field ) {
		if ( !Require( 4, field ) ) {
			return false;
		}
		value = static_cast<std::uint32_t>( data[position] ) |
			( static_cast<std::uint32_t>( data[position + 1] ) << 8 ) |
			( static_cast<std::uint32_t>( data[position + 2] ) << 16 ) |
			( static_cast<std::uint32_t>( data[position + 3] ) << 24 );
		position += 4;
		return true;
	}

	bool ReadU64( std::uint64_t &value, const char *field ) {
		if ( !Require( 8, field ) ) {
			return false;
		}
		value = 0;
		for ( unsigned int index = 0; index < 8; ++index ) {
			value |= static_cast<std::uint64_t>( data[position + index] ) << ( index * 8u );
		}
		position += 8;
		return true;
	}

	bool ReadHash( Hash &hash, const char *field ) {
		if ( !Require( hash.size(), field ) ) {
			return false;
		}
		std::memcpy( hash.data(), data + position, hash.size() );
		position += hash.size();
		return true;
	}

	bool ReadString( std::string &value, const std::size_t maximumBytes,
			std::size_t &aggregateBytes, const std::size_t maximumAggregateBytes,
			const char *field ) {
		std::uint32_t length = 0;
		if ( !ReadU32( length, field ) ) {
			return false;
		}
		if ( length == 0 ) {
			Fail( Status::INVALID_STRING, field );
			return false;
		}
		if ( static_cast<std::size_t>( length ) > maximumBytes ) {
			Fail( Status::SIZE_LIMIT_EXCEEDED, field );
			return false;
		}
		if ( static_cast<std::size_t>( length ) > maximumAggregateBytes -
				std::min( maximumAggregateBytes, aggregateBytes ) ) {
			Fail( Status::SIZE_LIMIT_EXCEEDED, "aggregate variable-length data" );
			return false;
		}
		if ( !Require( length, field ) ) {
			return false;
		}
		value.assign( reinterpret_cast<const char *>( data + position ), length );
		position += length;
		aggregateBytes += length;
		return true;
	}

	bool ReadByteVector( std::vector<std::uint8_t> &value,
			const std::size_t maximumBytes, std::size_t &aggregateBytes,
			const std::size_t maximumAggregateBytes, const char *field ) {
		std::uint32_t length = 0;
		if ( !ReadU32( length, field ) ) {
			return false;
		}
		if ( static_cast<std::size_t>( length ) > maximumBytes ) {
			Fail( Status::SIZE_LIMIT_EXCEEDED, field );
			return false;
		}
		if ( static_cast<std::size_t>( length ) > maximumAggregateBytes -
				std::min( maximumAggregateBytes, aggregateBytes ) ) {
			Fail( Status::SIZE_LIMIT_EXCEEDED, "aggregate variable-length data" );
			return false;
		}
		if ( !Require( length, field ) ) {
			return false;
		}
		value.assign( data + position, data + position + length );
		position += length;
		aggregateBytes += length;
		return true;
	}

	bool Skip( const std::size_t bytes, const char *field ) {
		if ( !Require( bytes, field ) ) {
			return false;
		}
		position += bytes;
		return true;
	}

	const std::uint8_t *At( const std::size_t offset ) const {
		return data + offset;
	}

	std::size_t Position() const {
		return position;
	}

	std::size_t Remaining() const {
		return size - position;
	}

	const Result &Error() const {
		return error;
	}

	void Fail( const Status status, const char *field ) {
		if ( error.Ok() ) {
			error.status = status;
			error.offset = position;
			error.diagnostic = field;
		}
	}

private:
	bool Require( const std::size_t bytes, const char *field ) {
		if ( bytes > Remaining() ) {
			Fail( Status::TRUNCATED, field );
			return false;
		}
		return true;
	}

	const std::uint8_t *data;
	std::size_t size;
	std::size_t position = 0;
	Result error;
};

bool IsValidCacheKind( const CacheKind kind ) {
	return kind == CacheKind::RENDER_MODEL || kind == CacheKind::RENDER_WORLD ||
		kind == CacheKind::COLLISION_MODEL;
}

bool IsValidCodec( const CompressionCodec codec ) {
	return codec == CompressionCodec::NONE || codec == CompressionCodec::DEFLATE;
}

bool IsValidEntryType( const ManifestEntryType type ) {
	const auto value = static_cast<std::uint32_t>( type );
	return value >= static_cast<std::uint32_t>( ManifestEntryType::RENDER_MODEL ) &&
		value <= static_cast<std::uint32_t>( ManifestEntryType::RAW_FILE );
}

bool IsValidPriority( const ManifestPriority priority ) {
	return static_cast<std::uint32_t>( priority ) <=
		static_cast<std::uint32_t>( ManifestPriority::CRITICAL );
}

Result ValidateSource( const SourceIdentity &source, const DecodeLimits &limits ) {
	if ( source.normalizedPath.size() > limits.maxAggregateStringBytes ) {
		return Failure( Status::SIZE_LIMIT_EXCEEDED, 0, "source aggregate variable-length data" );
	}
	std::string normalized;
	Result result = NormalizeVirtualPath( source.normalizedPath, normalized, limits.maxPathBytes );
	if ( !result ) {
		return result;
	}
	if ( normalized != source.normalizedPath ) {
		return Failure( Status::PATH_NOT_NORMALIZED, 0, "source path is not canonical" );
	}
	if ( source.containerKind == SourceContainerKind::LOOSE_FILE ) {
		if ( source.containerPk4Checksum != 0 ) {
			return Failure( Status::INVALID_FIELD, 0, "loose source has a PK4 checksum" );
		}
	} else if ( source.containerKind == SourceContainerKind::PK4_ARCHIVE ) {
		if ( source.containerPk4Checksum == 0 ) {
			return Failure( Status::INVALID_FIELD, 0, "PK4 source has an unknown checksum" );
		}
	} else {
		return Failure( Status::INVALID_ENUM_VALUE, 0, "unknown source-container kind" );
	}
	return Success();
}

bool WriteSource( Writer &writer, const SourceIdentity &source ) {
	return writer.WriteString( source.normalizedPath ) &&
		writer.WriteU64( source.size ) && writer.WriteU64( source.timestamp ) &&
		writer.WriteU32( static_cast<std::uint32_t>( source.containerKind ) ) &&
		writer.WriteU32( source.containerPk4Checksum );
}

Result ReadSource( Reader &reader, SourceIdentity &source, const DecodeLimits &limits,
		std::size_t &aggregateBytes ) {
	std::uint32_t containerKind = 0;
	if ( !reader.ReadString( source.normalizedPath, limits.maxPathBytes,
			aggregateBytes, limits.maxAggregateStringBytes, "source path" ) ||
		!reader.ReadU64( source.size, "source size" ) ||
		!reader.ReadU64( source.timestamp, "source timestamp" ) ||
		!reader.ReadU32( containerKind, "source-container kind" ) ||
		!reader.ReadU32( source.containerPk4Checksum, "source PK4 checksum" ) ) {
		return reader.Error();
	}
	source.containerKind = static_cast<SourceContainerKind>( containerKind );
	Result result = ValidateSource( source, limits );
	if ( !result ) {
		result.offset = reader.Position();
	}
	return result;
}

bool SourceLess( const SourceIdentity &left, const SourceIdentity &right ) {
	if ( left.normalizedPath != right.normalizedPath ) {
		return left.normalizedPath < right.normalizedPath;
	}
	if ( left.containerKind != right.containerKind ) {
		return static_cast<std::uint32_t>( left.containerKind ) <
			static_cast<std::uint32_t>( right.containerKind );
	}
	if ( left.containerPk4Checksum != right.containerPk4Checksum ) {
		return left.containerPk4Checksum < right.containerPk4Checksum;
	}
	if ( left.size != right.size ) {
		return left.size < right.size;
	}
	return left.timestamp < right.timestamp;
}

bool EntryIdentityLess( const ManifestEntry &left, const ManifestEntry &right ) {
	if ( left.type != right.type ) {
		return static_cast<std::uint32_t>( left.type ) < static_cast<std::uint32_t>( right.type );
	}
	if ( left.normalizedName != right.normalizedName ) {
		return left.normalizedName < right.normalizedName;
	}
	if ( SourceLess( left.source, right.source ) ) {
		return true;
	}
	if ( SourceLess( right.source, left.source ) ) {
		return false;
	}
	if ( left.flags != right.flags ) {
		return left.flags < right.flags;
	}
	return left.options < right.options;
}

bool EntryIdentityEqual( const ManifestEntry &left, const ManifestEntry &right ) {
	return !EntryIdentityLess( left, right ) && !EntryIdentityLess( right, left );
}

bool EntryReplayLess( const ManifestEntry &left, const ManifestEntry &right ) {
	if ( left.priority != right.priority ) {
		return static_cast<std::uint32_t>( left.priority ) >
			static_cast<std::uint32_t>( right.priority );
	}
	if ( left.firstUseOrder != right.firstUseOrder ) {
		return left.firstUseOrder < right.firstUseOrder;
	}
	if ( EntryIdentityLess( left, right ) ) {
		return true;
	}
	if ( EntryIdentityLess( right, left ) ) {
		return false;
	}
	return left.useCount < right.useCount;
}

Result ValidateManifestFields( const Manifest &manifest, const DecodeLimits &limits ) {
	if ( manifest.producerVersion == 0 ) {
		return Failure( Status::INVALID_FIELD, 0, "manifest producer version is zero" );
	}
	if ( HashIsZero( manifest.gameMode ) || HashIsZero( manifest.entityFilter ) ||
		HashIsZero( manifest.contentSignature ) || HashIsZero( manifest.settingsSignature ) ) {
		return Failure( Status::INVALID_FIELD, 0, "manifest signature is unset" );
	}
	std::string normalized;
	Result result = NormalizeVirtualPath( manifest.mapKey, normalized, limits.maxMapKeyBytes );
	if ( !result ) {
		return result;
	}
	if ( normalized != manifest.mapKey ) {
		return Failure( Status::PATH_NOT_NORMALIZED, 0, "manifest map key is not canonical" );
	}
	if ( manifest.entries.size() > limits.maxManifestEntries ||
		manifest.entries.size() > std::numeric_limits<std::uint32_t>::max() ) {
		return Failure( Status::SIZE_LIMIT_EXCEEDED, 0, "manifest entry count" );
	}

	std::size_t aggregateBytes = manifest.mapKey.size();
	if ( aggregateBytes > limits.maxAggregateStringBytes ) {
		return Failure( Status::SIZE_LIMIT_EXCEEDED, 0, "manifest aggregate variable-length data" );
	}
	for ( const ManifestEntry &entry : manifest.entries ) {
		if ( !IsValidEntryType( entry.type ) || !IsValidPriority( entry.priority ) ) {
			return Failure( Status::INVALID_ENUM_VALUE, 0, "manifest entry type or priority" );
		}
		if ( entry.useCount == 0 ) {
			return Failure( Status::INVALID_FIELD, 0, "manifest entry use count is zero" );
		}
		result = NormalizeVirtualPath( entry.normalizedName, normalized, limits.maxEntryNameBytes );
		if ( !result ) {
			return result;
		}
		if ( normalized != entry.normalizedName ) {
			return Failure( Status::PATH_NOT_NORMALIZED, 0, "manifest entry name is not canonical" );
		}
		result = ValidateSource( entry.source, limits );
		if ( !result ) {
			return result;
		}
		if ( entry.options.size() > limits.maxEntryOptionsBytes ||
			entry.options.size() > std::numeric_limits<std::uint32_t>::max() ) {
			return Failure( Status::SIZE_LIMIT_EXCEEDED, 0, "manifest entry options" );
		}
		std::size_t added = 0;
		if ( !AddWithoutOverflow( entry.normalizedName.size(), entry.source.normalizedPath.size(), added ) ||
			!AddWithoutOverflow( added, entry.options.size(), added ) ) {
			return Failure( Status::INTEGER_OVERFLOW, 0, "manifest aggregate variable-length data" );
		}
		if ( added > limits.maxAggregateStringBytes -
				std::min( limits.maxAggregateStringBytes, aggregateBytes ) ) {
			return Failure( Status::SIZE_LIMIT_EXCEEDED, 0, "manifest aggregate variable-length data" );
		}
		aggregateBytes += added;
	}
	return Success();
}

bool WriteManifestEntry( Writer &writer, const ManifestEntry &entry ) {
	return writer.WriteU32( static_cast<std::uint32_t>( entry.type ) ) &&
		writer.WriteU32( static_cast<std::uint32_t>( entry.priority ) ) &&
		writer.WriteU64( entry.firstUseOrder ) && writer.WriteU32( entry.useCount ) &&
		writer.WriteU32( entry.flags ) && writer.WriteString( entry.normalizedName ) &&
		WriteSource( writer, entry.source ) &&
		writer.WriteU32( static_cast<std::uint32_t>( entry.options.size() ) ) &&
		writer.WriteBytes( entry.options.data(), entry.options.size() );
}

Result ReadManifestEntry( Reader &reader, ManifestEntry &entry, const DecodeLimits &limits,
		std::size_t &aggregateBytes ) {
	std::uint32_t type = 0;
	std::uint32_t priority = 0;
	if ( !reader.ReadU32( type, "manifest entry type" ) ||
		!reader.ReadU32( priority, "manifest entry priority" ) ||
		!reader.ReadU64( entry.firstUseOrder, "manifest entry first-use order" ) ||
		!reader.ReadU32( entry.useCount, "manifest entry use count" ) ||
		!reader.ReadU32( entry.flags, "manifest entry flags" ) ) {
		return reader.Error();
	}
	entry.type = static_cast<ManifestEntryType>( type );
	entry.priority = static_cast<ManifestPriority>( priority );
	if ( !IsValidEntryType( entry.type ) || !IsValidPriority( entry.priority ) ) {
		return Failure( Status::INVALID_ENUM_VALUE, reader.Position(),
			"unknown manifest entry type or priority" );
	}
	if ( entry.useCount == 0 ) {
		return Failure( Status::INVALID_FIELD, reader.Position(), "manifest entry use count is zero" );
	}
	if ( !reader.ReadString( entry.normalizedName, limits.maxEntryNameBytes,
			aggregateBytes, limits.maxAggregateStringBytes, "manifest entry name" ) ) {
		return reader.Error();
	}
	std::string normalized;
	Result result = NormalizeVirtualPath( entry.normalizedName, normalized, limits.maxEntryNameBytes );
	if ( !result ) {
		result.offset = reader.Position();
		return result;
	}
	if ( normalized != entry.normalizedName ) {
		return Failure( Status::PATH_NOT_NORMALIZED, reader.Position(),
			"manifest entry name is not canonical" );
	}
	result = ReadSource( reader, entry.source, limits, aggregateBytes );
	if ( !result ) {
		return result;
	}
	if ( !reader.ReadByteVector( entry.options, limits.maxEntryOptionsBytes,
			aggregateBytes, limits.maxAggregateStringBytes, "manifest entry options" ) ) {
		return reader.Error();
	}
	return Success();
}

Result AllocationFailure() {
	return Failure( Status::SIZE_LIMIT_EXCEEDED, 0, "allocation failed within configured format limits" );
}

} // namespace

Result NormalizeVirtualPath( const std::string_view input, std::string &output,
		const std::size_t maximumBytes ) {
	if ( input.empty() ) {
		return Failure( Status::INVALID_STRING, 0, "virtual path is empty" );
	}
	if ( maximumBytes == 0 ) {
		return Failure( Status::INVALID_ARGUMENT, 0, "invalid virtual-path limit" );
	}
	const std::size_t effectiveMaximum = std::min<std::size_t>( maximumBytes,
		std::numeric_limits<std::uint32_t>::max() );
	if ( input.front() == '/' || input.front() == '\\' ) {
		return Failure( Status::INVALID_STRING, 0, "virtual path is absolute" );
	}

	try {
		std::string normalized;
		normalized.reserve( std::min( input.size(), effectiveMaximum ) );
		std::size_t segmentStart = 0;
		for ( std::size_t position = 0; position <= input.size(); ++position ) {
			const bool atEnd = position == input.size();
			const bool separator = !atEnd && ( input[position] == '/' || input[position] == '\\' );
			if ( !atEnd && !separator ) {
				continue;
			}
			const std::size_t segmentBytes = position - segmentStart;
			if ( segmentBytes != 0 ) {
				const std::string_view segment = input.substr( segmentStart, segmentBytes );
				if ( segment == ".." ) {
					return Failure( Status::INVALID_STRING, segmentStart, "virtual path contains a parent segment" );
				}
				if ( segment != "." ) {
					const std::size_t separatorBytes = normalized.empty() ? 0 : 1;
					const std::size_t available = effectiveMaximum - normalized.size();
					if ( separatorBytes > available || segment.size() > available - separatorBytes ) {
						return Failure( Status::SIZE_LIMIT_EXCEEDED, segmentStart, "virtual path length" );
					}
					if ( separatorBytes != 0 ) {
						normalized.push_back( '/' );
					}
					for ( std::size_t index = 0; index < segment.size(); ++index ) {
						unsigned char value = static_cast<unsigned char>( segment[index] );
						if ( value == 0 || value < 0x20u || value == 0x7fu || value == ':' ) {
							return Failure( Status::INVALID_STRING, segmentStart + index,
								"virtual path contains a forbidden byte" );
						}
						if ( value >= 'A' && value <= 'Z' ) {
							value = static_cast<unsigned char>( value - 'A' + 'a' );
						}
						normalized.push_back( static_cast<char>( value ) );
					}
				}
			}
			segmentStart = position + 1;
		}
		if ( normalized.empty() ) {
			return Failure( Status::INVALID_STRING, 0, "virtual path has no name segments" );
		}
		output = std::move( normalized );
		return Success();
	} catch ( const std::bad_alloc & ) {
		return AllocationFailure();
	} catch ( const std::length_error & ) {
		return AllocationFailure();
	}
}

Hash ComputeHash( const void *data, const std::size_t bytes ) {
	if ( data == nullptr && bytes != 0 ) {
		return Hash{};
	}
	// The idlib SHA-256 uses the x86 SHA extensions when the CPU has them, or
	// the Armv8 SHA-256 instructions where the build baseline guarantees them,
	// and its portable compression otherwise (same digest everywhere). Every
	// generated-cache read and write hashes the whole payload twice, so a
	// private portable implementation was a visible share of a warm load.
	Hash hash{};
	static_assert( sizeof( hash ) == idCrypto::SHA256_DIGEST_BYTES, "digest size" );
	idCrypto::SHA256( bytes != 0 ? data : "", bytes, hash.data() );
	return hash;
}

const char *StatusName( const Status status ) {
	switch ( status ) {
		case Status::OK: return "ok";
		case Status::INVALID_ARGUMENT: return "invalid-argument";
		case Status::SIZE_LIMIT_EXCEEDED: return "size-limit-exceeded";
		case Status::INTEGER_OVERFLOW: return "integer-overflow";
		case Status::TRUNCATED: return "truncated";
		case Status::INVALID_MAGIC: return "invalid-magic";
		case Status::UNSUPPORTED_VERSION: return "unsupported-version";
		case Status::INVALID_END_MARKER: return "invalid-end-marker";
		case Status::INVALID_ENUM_VALUE: return "invalid-enum-value";
		case Status::INVALID_STRING: return "invalid-string";
		case Status::PATH_NOT_NORMALIZED: return "path-not-normalized";
		case Status::INVALID_FIELD: return "invalid-field";
		case Status::PAYLOAD_HASH_MISMATCH: return "payload-hash-mismatch";
		case Status::INTEGRITY_MISMATCH: return "integrity-mismatch";
		case Status::TRAILING_DATA: return "trailing-data";
		case Status::DUPLICATE_ENTRY: return "duplicate-entry";
		case Status::NON_CANONICAL_ORDER: return "non-canonical-order";
		case Status::KIND_MISMATCH: return "kind-mismatch";
		case Status::PARSER_VERSION_MISMATCH: return "parser-version-mismatch";
		case Status::SOURCE_MISMATCH: return "source-mismatch";
		case Status::CONTENT_SIGNATURE_MISMATCH: return "content-signature-mismatch";
		case Status::SETTINGS_SIGNATURE_MISMATCH: return "settings-signature-mismatch";
		case Status::MAP_KEY_MISMATCH: return "map-key-mismatch";
		case Status::GAME_MODE_MISMATCH: return "game-mode-mismatch";
		case Status::ENTITY_FILTER_MISMATCH: return "entity-filter-mismatch";
		case Status::PRODUCER_VERSION_MISMATCH: return "producer-version-mismatch";
	}
	return "unknown-status";
}

Result EncodeEnvelope( const CacheEnvelope &envelope, std::vector<std::uint8_t> &encoded,
		const DecodeLimits &limits ) {
	try {
		if ( !IsValidCacheKind( envelope.kind ) || !IsValidCodec( envelope.codec ) ) {
			return Failure( Status::INVALID_ENUM_VALUE, 0, "cache kind or compression codec" );
		}
		if ( envelope.parserVersion == 0 ) {
			return Failure( Status::INVALID_FIELD, 0, "cache parser version is zero" );
		}
		Result result = ValidateSource( envelope.source, limits );
		if ( !result ) {
			return result;
		}
		if ( HashIsZero( envelope.contentSignature ) || HashIsZero( envelope.settingsSignature ) ) {
			return Failure( Status::INVALID_FIELD, 0, "cache key signature is unset" );
		}
		if ( envelope.payload.size() > limits.maxPayloadBytes ) {
			return Failure( Status::SIZE_LIMIT_EXCEEDED, 0, "stored cache payload" );
		}

		std::uint64_t decodedPayloadBytes = envelope.decodedPayloadBytes;
		Hash decodedPayloadHash = envelope.decodedPayloadHash;
		if ( envelope.codec == CompressionCodec::NONE ) {
			decodedPayloadBytes = static_cast<std::uint64_t>( envelope.payload.size() );
			decodedPayloadHash = ComputeHash( envelope.payload.data(), envelope.payload.size() );
		} else {
			if ( decodedPayloadBytes > limits.maxDecodedPayloadBytes ) {
				return Failure( Status::SIZE_LIMIT_EXCEEDED, 0, "decoded cache payload" );
			}
			if ( HashIsZero( decodedPayloadHash ) ) {
				return Failure( Status::INVALID_FIELD, 0, "decoded payload hash is unset" );
			}
		}
		if ( decodedPayloadBytes > limits.maxDecodedPayloadBytes ) {
			return Failure( Status::SIZE_LIMIT_EXCEEDED, 0, "decoded cache payload" );
		}

		Writer writer( limits.maxEnvelopeBytes );
		if ( !writer.WriteU32( CACHE_MAGIC ) || !writer.WriteU32( CACHE_FORMAT_VERSION ) ||
			!writer.WriteU32( static_cast<std::uint32_t>( envelope.kind ) ) ||
			!writer.WriteU32( envelope.parserVersion ) ||
			!writer.WriteU32( static_cast<std::uint32_t>( envelope.codec ) ) ||
			!WriteSource( writer, envelope.source ) ||
			!writer.WriteHash( envelope.contentSignature ) ||
			!writer.WriteHash( envelope.settingsSignature ) ||
			!writer.WriteU64( static_cast<std::uint64_t>( envelope.payload.size() ) ) ||
			!writer.WriteU64( decodedPayloadBytes ) || !writer.WriteHash( decodedPayloadHash ) ||
			!writer.WriteBytes( envelope.payload.data(), envelope.payload.size() ) ) {
			return Failure( Status::SIZE_LIMIT_EXCEEDED, writer.Size(), "encoded cache envelope" );
		}
		const Hash integrityHash = ComputeHash( writer.Data().data(), writer.Data().size() );
		if ( !writer.WriteHash( integrityHash ) || !writer.WriteU32( CACHE_END_MAGIC ) ) {
			return Failure( Status::SIZE_LIMIT_EXCEEDED, writer.Size(), "cache integrity trailer" );
		}
		encoded = writer.Take();
		return Success();
	} catch ( const std::bad_alloc & ) {
		return AllocationFailure();
	} catch ( const std::length_error & ) {
		return AllocationFailure();
	}
}

Result DecodeEnvelope( const void *data, const std::size_t bytes, CacheEnvelope &envelope,
		const DecodeLimits &limits ) {
	if ( data == nullptr && bytes != 0 ) {
		return Failure( Status::INVALID_ARGUMENT, 0, "null cache data" );
	}
	if ( bytes > limits.maxEnvelopeBytes ) {
		return Failure( Status::SIZE_LIMIT_EXCEEDED, 0, "encoded cache envelope" );
	}
	try {
		Reader reader( data, bytes );
		std::uint32_t magic = 0;
		std::uint32_t version = 0;
		std::uint32_t kind = 0;
		std::uint32_t parserVersion = 0;
		std::uint32_t codec = 0;
		if ( !reader.ReadU32( magic, "cache magic" ) ) {
			return reader.Error();
		}
		if ( magic != CACHE_MAGIC ) {
			return Failure( Status::INVALID_MAGIC, 0, "cache magic" );
		}
		if ( !reader.ReadU32( version, "cache format version" ) ) {
			return reader.Error();
		}
		if ( version != CACHE_FORMAT_VERSION ) {
			return Failure( Status::UNSUPPORTED_VERSION, 4, "cache format version" );
		}
		if ( !reader.ReadU32( kind, "cache kind" ) ||
			!reader.ReadU32( parserVersion, "cache parser version" ) ||
			!reader.ReadU32( codec, "cache compression codec" ) ) {
			return reader.Error();
		}

		CacheEnvelope decoded;
		decoded.kind = static_cast<CacheKind>( kind );
		decoded.parserVersion = parserVersion;
		decoded.codec = static_cast<CompressionCodec>( codec );
		if ( !IsValidCacheKind( decoded.kind ) || !IsValidCodec( decoded.codec ) ) {
			return Failure( Status::INVALID_ENUM_VALUE, 8, "cache kind or compression codec" );
		}
		if ( decoded.parserVersion == 0 ) {
			return Failure( Status::INVALID_FIELD, 12, "cache parser version is zero" );
		}
		std::size_t aggregateBytes = 0;
		Result result = ReadSource( reader, decoded.source, limits, aggregateBytes );
		if ( !result ) {
			return result;
		}
		if ( !reader.ReadHash( decoded.contentSignature, "cache content signature" ) ||
			!reader.ReadHash( decoded.settingsSignature, "cache settings signature" ) ) {
			return reader.Error();
		}
		if ( HashIsZero( decoded.contentSignature ) || HashIsZero( decoded.settingsSignature ) ) {
			return Failure( Status::INVALID_FIELD, reader.Position(), "cache key signature is unset" );
		}
		std::uint64_t storedPayloadBytes = 0;
		if ( !reader.ReadU64( storedPayloadBytes, "stored payload length" ) ||
			!reader.ReadU64( decoded.decodedPayloadBytes, "decoded payload length" ) ||
			!reader.ReadHash( decoded.decodedPayloadHash, "decoded payload hash" ) ) {
			return reader.Error();
		}
		if ( storedPayloadBytes > std::numeric_limits<std::size_t>::max() ) {
			return Failure( Status::INTEGER_OVERFLOW, reader.Position(), "stored payload length" );
		}
		if ( storedPayloadBytes > limits.maxPayloadBytes ||
			decoded.decodedPayloadBytes > limits.maxDecodedPayloadBytes ) {
			return Failure( Status::SIZE_LIMIT_EXCEEDED, reader.Position(), "cache payload length" );
		}
		if ( decoded.codec == CompressionCodec::NONE &&
			decoded.decodedPayloadBytes != storedPayloadBytes ) {
			return Failure( Status::INVALID_FIELD, reader.Position(),
				"uncompressed payload lengths disagree" );
		}
		if ( HashIsZero( decoded.decodedPayloadHash ) ) {
			return Failure( Status::INVALID_FIELD, reader.Position(), "decoded payload hash is unset" );
		}

		const std::size_t payloadBytes = static_cast<std::size_t>( storedPayloadBytes );
		const std::size_t payloadOffset = reader.Position();
		if ( reader.Remaining() < TRAILER_BYTES ||
			payloadBytes > reader.Remaining() - TRAILER_BYTES ) {
			return Failure( Status::TRUNCATED, reader.Position(), "cache payload or integrity trailer" );
		}
		if ( !reader.Skip( payloadBytes, "cache payload" ) ) {
			return reader.Error();
		}
		const std::size_t protectedBytes = reader.Position();
		Hash storedIntegrityHash{};
		std::uint32_t endMagic = 0;
		if ( !reader.ReadHash( storedIntegrityHash, "cache integrity hash" ) ||
			!reader.ReadU32( endMagic, "cache end marker" ) ) {
			return reader.Error();
		}
		if ( endMagic != CACHE_END_MAGIC ) {
			return Failure( Status::INVALID_END_MARKER, reader.Position() - 4, "cache end marker" );
		}
		if ( reader.Remaining() != 0 ) {
			return Failure( Status::TRAILING_DATA, reader.Position(), "bytes after cache end marker" );
		}
		const Hash actualIntegrityHash = ComputeHash( data, protectedBytes );
		if ( !HashesEqual( storedIntegrityHash, actualIntegrityHash ) ) {
			return Failure( Status::INTEGRITY_MISMATCH, protectedBytes, "cache integrity hash" );
		}
		if ( decoded.codec == CompressionCodec::NONE ) {
			const Hash actualPayloadHash = ComputeHash( reader.At( payloadOffset ), payloadBytes );
			if ( !HashesEqual( decoded.decodedPayloadHash, actualPayloadHash ) ) {
				return Failure( Status::PAYLOAD_HASH_MISMATCH, payloadOffset, "cache payload hash" );
			}
		}
		decoded.payload.assign( reader.At( payloadOffset ), reader.At( payloadOffset ) + payloadBytes );
		envelope = std::move( decoded );
		return Success();
	} catch ( const std::bad_alloc & ) {
		return AllocationFailure();
	} catch ( const std::length_error & ) {
		return AllocationFailure();
	}
}

Result DecodeEnvelope( const std::vector<std::uint8_t> &encoded, CacheEnvelope &envelope,
		const DecodeLimits &limits ) {
	return DecodeEnvelope( encoded.data(), encoded.size(), envelope, limits );
}

Result ValidateEnvelopeKey( const CacheEnvelope &envelope, const EnvelopeExpectation &expected ) {
	if ( envelope.kind != expected.kind ) {
		return Failure( Status::KIND_MISMATCH, 0, "cache resource kind" );
	}
	if ( envelope.parserVersion != expected.parserVersion ) {
		return Failure( Status::PARSER_VERSION_MISMATCH, 0, "cache parser version" );
	}
	if ( !( envelope.source == expected.source ) ) {
		return Failure( Status::SOURCE_MISMATCH, 0, "cache source identity" );
	}
	if ( !HashesEqual( envelope.contentSignature, expected.contentSignature ) ) {
		return Failure( Status::CONTENT_SIGNATURE_MISMATCH, 0, "cache content signature" );
	}
	if ( !HashesEqual( envelope.settingsSignature, expected.settingsSignature ) ) {
		return Failure( Status::SETTINGS_SIGNATURE_MISMATCH, 0, "cache settings signature" );
	}
	return Success();
}

Result DecodeEnvelopeForKey( const void *data, const std::size_t bytes,
		const EnvelopeExpectation &expected, CacheEnvelope &envelope,
		const DecodeLimits &limits ) {
	CacheEnvelope decoded;
	Result result = DecodeEnvelope( data, bytes, decoded, limits );
	if ( !result ) {
		return result;
	}
	result = ValidateEnvelopeKey( decoded, expected );
	if ( !result ) {
		return result;
	}
	envelope = std::move( decoded );
	return Success();
}

Result DecodeEnvelopeForKey( const std::vector<std::uint8_t> &encoded,
		const EnvelopeExpectation &expected, CacheEnvelope &envelope,
		const DecodeLimits &limits ) {
	return DecodeEnvelopeForKey( encoded.data(), encoded.size(), expected, envelope, limits );
}

Result ValidateDecodedPayload( const CacheEnvelope &envelope,
		const void *decodedPayload, const std::size_t decodedBytes ) {
	if ( decodedPayload == nullptr && decodedBytes != 0 ) {
		return Failure( Status::INVALID_ARGUMENT, 0, "null decoded payload" );
	}
	if ( envelope.decodedPayloadBytes != decodedBytes ) {
		return Failure( Status::INVALID_FIELD, 0, "decoded payload length" );
	}
	const Hash actualHash = ComputeHash( decodedPayload, decodedBytes );
	if ( !HashesEqual( envelope.decodedPayloadHash, actualHash ) ) {
		return Failure( Status::PAYLOAD_HASH_MISMATCH, 0, "decoded payload hash" );
	}
	return Success();
}

Result CanonicalizeManifest( Manifest &manifest, const DecodeLimits &limits ) {
	try {
		Result result = ValidateManifestFields( manifest, limits );
		if ( !result ) {
			return result;
		}
		Manifest canonical = manifest;
		std::sort( canonical.entries.begin(), canonical.entries.end(), EntryIdentityLess );
		std::vector<ManifestEntry> merged;
		merged.reserve( canonical.entries.size() );
		for ( ManifestEntry &entry : canonical.entries ) {
			if ( !merged.empty() && EntryIdentityEqual( merged.back(), entry ) ) {
				ManifestEntry &previous = merged.back();
				if ( entry.useCount > std::numeric_limits<std::uint32_t>::max() - previous.useCount ) {
					return Failure( Status::INTEGER_OVERFLOW, 0, "merged manifest use count" );
				}
				previous.useCount += entry.useCount;
				if ( static_cast<std::uint32_t>( entry.priority ) >
						static_cast<std::uint32_t>( previous.priority ) ) {
					previous.priority = entry.priority;
				}
				previous.firstUseOrder = std::min( previous.firstUseOrder, entry.firstUseOrder );
			} else {
				merged.push_back( std::move( entry ) );
			}
		}
		std::sort( merged.begin(), merged.end(), EntryReplayLess );
		canonical.entries = std::move( merged );
		manifest = std::move( canonical );
		return Success();
	} catch ( const std::bad_alloc & ) {
		return AllocationFailure();
	} catch ( const std::length_error & ) {
		return AllocationFailure();
	}
}

Result EncodeManifest( const Manifest &manifest, std::vector<std::uint8_t> &encoded,
		const DecodeLimits &limits ) {
	try {
		Manifest canonical = manifest;
		Result result = CanonicalizeManifest( canonical, limits );
		if ( !result ) {
			return result;
		}
		Writer writer( limits.maxManifestBytes );
		if ( !writer.WriteU32( MANIFEST_MAGIC ) || !writer.WriteU32( MANIFEST_FORMAT_VERSION ) ||
			!writer.WriteU32( canonical.producerVersion ) ||
			!writer.WriteHash( canonical.gameMode ) ||
			!writer.WriteHash( canonical.entityFilter ) ||
			!writer.WriteHash( canonical.contentSignature ) ||
			!writer.WriteHash( canonical.settingsSignature ) ||
			!writer.WriteString( canonical.mapKey ) ||
			!writer.WriteU32( static_cast<std::uint32_t>( canonical.entries.size() ) ) ) {
			return Failure( Status::SIZE_LIMIT_EXCEEDED, writer.Size(), "encoded manifest header" );
		}
		for ( const ManifestEntry &entry : canonical.entries ) {
			if ( !WriteManifestEntry( writer, entry ) ) {
				return Failure( Status::SIZE_LIMIT_EXCEEDED, writer.Size(), "encoded manifest entry" );
			}
		}
		const Hash integrityHash = ComputeHash( writer.Data().data(), writer.Data().size() );
		if ( !writer.WriteHash( integrityHash ) || !writer.WriteU32( MANIFEST_END_MAGIC ) ) {
			return Failure( Status::SIZE_LIMIT_EXCEEDED, writer.Size(), "manifest integrity trailer" );
		}
		encoded = writer.Take();
		return Success();
	} catch ( const std::bad_alloc & ) {
		return AllocationFailure();
	} catch ( const std::length_error & ) {
		return AllocationFailure();
	}
}

Result DecodeManifest( const void *data, const std::size_t bytes, Manifest &manifest,
		const DecodeLimits &limits ) {
	if ( data == nullptr && bytes != 0 ) {
		return Failure( Status::INVALID_ARGUMENT, 0, "null manifest data" );
	}
	if ( bytes > limits.maxManifestBytes ) {
		return Failure( Status::SIZE_LIMIT_EXCEEDED, 0, "encoded manifest" );
	}
	try {
		Reader reader( data, bytes );
		std::uint32_t magic = 0;
		std::uint32_t version = 0;
		if ( !reader.ReadU32( magic, "manifest magic" ) ) {
			return reader.Error();
		}
		if ( magic != MANIFEST_MAGIC ) {
			return Failure( Status::INVALID_MAGIC, 0, "manifest magic" );
		}
		if ( !reader.ReadU32( version, "manifest format version" ) ) {
			return reader.Error();
		}
		if ( version != MANIFEST_FORMAT_VERSION ) {
			return Failure( Status::UNSUPPORTED_VERSION, 4, "manifest format version" );
		}

		Manifest decoded;
		if ( !reader.ReadU32( decoded.producerVersion, "manifest producer version" ) ||
			!reader.ReadHash( decoded.gameMode, "manifest game mode" ) ||
			!reader.ReadHash( decoded.entityFilter, "manifest entity filter" ) ||
			!reader.ReadHash( decoded.contentSignature, "manifest content signature" ) ||
			!reader.ReadHash( decoded.settingsSignature, "manifest settings signature" ) ) {
			return reader.Error();
		}
		if ( decoded.producerVersion == 0 ) {
			return Failure( Status::INVALID_FIELD, 8, "manifest producer version is zero" );
		}
		if ( HashIsZero( decoded.gameMode ) || HashIsZero( decoded.entityFilter ) ||
			HashIsZero( decoded.contentSignature ) || HashIsZero( decoded.settingsSignature ) ) {
			return Failure( Status::INVALID_FIELD, reader.Position(), "manifest signature is unset" );
		}
		std::size_t aggregateBytes = 0;
		if ( !reader.ReadString( decoded.mapKey, limits.maxMapKeyBytes,
				aggregateBytes, limits.maxAggregateStringBytes, "manifest map key" ) ) {
			return reader.Error();
		}
		std::string normalized;
		Result result = NormalizeVirtualPath( decoded.mapKey, normalized, limits.maxMapKeyBytes );
		if ( !result ) {
			result.offset = reader.Position();
			return result;
		}
		if ( normalized != decoded.mapKey ) {
			return Failure( Status::PATH_NOT_NORMALIZED, reader.Position(),
				"manifest map key is not canonical" );
		}

		std::uint32_t entryCount = 0;
		if ( !reader.ReadU32( entryCount, "manifest entry count" ) ) {
			return reader.Error();
		}
		if ( static_cast<std::size_t>( entryCount ) > limits.maxManifestEntries ) {
			return Failure( Status::SIZE_LIMIT_EXCEEDED, reader.Position(), "manifest entry count" );
		}
		if ( reader.Remaining() < TRAILER_BYTES ) {
			return Failure( Status::TRUNCATED, reader.Position(), "manifest entries or integrity trailer" );
		}
		const std::size_t availableForEntries = reader.Remaining() - TRAILER_BYTES;
		if ( static_cast<std::size_t>( entryCount ) > availableForEntries / MIN_MANIFEST_ENTRY_BYTES ) {
			return Failure( Status::TRUNCATED, reader.Position(), "manifest entry count exceeds remaining data" );
		}
		decoded.entries.reserve( entryCount );
		for ( std::uint32_t index = 0; index < entryCount; ++index ) {
			ManifestEntry entry;
			result = ReadManifestEntry( reader, entry, limits, aggregateBytes );
			if ( !result ) {
				return result;
			}
			decoded.entries.push_back( std::move( entry ) );
		}

		const std::size_t protectedBytes = reader.Position();
		Hash storedIntegrityHash{};
		std::uint32_t endMagic = 0;
		if ( !reader.ReadHash( storedIntegrityHash, "manifest integrity hash" ) ||
			!reader.ReadU32( endMagic, "manifest end marker" ) ) {
			return reader.Error();
		}
		if ( endMagic != MANIFEST_END_MAGIC ) {
			return Failure( Status::INVALID_END_MARKER, reader.Position() - 4, "manifest end marker" );
		}
		if ( reader.Remaining() != 0 ) {
			return Failure( Status::TRAILING_DATA, reader.Position(), "bytes after manifest end marker" );
		}
		const Hash actualIntegrityHash = ComputeHash( data, protectedBytes );
		if ( !HashesEqual( storedIntegrityHash, actualIntegrityHash ) ) {
			return Failure( Status::INTEGRITY_MISMATCH, protectedBytes, "manifest integrity hash" );
		}

		Manifest canonical = decoded;
		result = CanonicalizeManifest( canonical, limits );
		if ( !result ) {
			return result;
		}
		if ( canonical.entries.size() != decoded.entries.size() ) {
			return Failure( Status::DUPLICATE_ENTRY, protectedBytes, "duplicate manifest entry" );
		}
		if ( canonical.entries != decoded.entries ) {
			return Failure( Status::NON_CANONICAL_ORDER, protectedBytes,
				"manifest entries are not in canonical order" );
		}
		manifest = std::move( decoded );
		return Success();
	} catch ( const std::bad_alloc & ) {
		return AllocationFailure();
	} catch ( const std::length_error & ) {
		return AllocationFailure();
	}
}

Result DecodeManifest( const std::vector<std::uint8_t> &encoded, Manifest &manifest,
		const DecodeLimits &limits ) {
	return DecodeManifest( encoded.data(), encoded.size(), manifest, limits );
}

Result ValidateManifestKey( const Manifest &manifest, const ManifestExpectation &expected ) {
	if ( manifest.producerVersion != expected.producerVersion ) {
		return Failure( Status::PRODUCER_VERSION_MISMATCH, 0, "manifest producer version" );
	}
	if ( manifest.mapKey != expected.mapKey ) {
		return Failure( Status::MAP_KEY_MISMATCH, 0, "manifest map key" );
	}
	if ( manifest.gameMode != expected.gameMode ) {
		return Failure( Status::GAME_MODE_MISMATCH, 0, "manifest game mode" );
	}
	if ( manifest.entityFilter != expected.entityFilter ) {
		return Failure( Status::ENTITY_FILTER_MISMATCH, 0, "manifest entity filter" );
	}
	if ( !HashesEqual( manifest.contentSignature, expected.contentSignature ) ) {
		return Failure( Status::CONTENT_SIGNATURE_MISMATCH, 0, "manifest content signature" );
	}
	if ( !HashesEqual( manifest.settingsSignature, expected.settingsSignature ) ) {
		return Failure( Status::SETTINGS_SIGNATURE_MISMATCH, 0, "manifest settings signature" );
	}
	return Success();
}

Result DecodeManifestForKey( const void *data, const std::size_t bytes,
		const ManifestExpectation &expected, Manifest &manifest,
		const DecodeLimits &limits ) {
	Manifest decoded;
	Result result = DecodeManifest( data, bytes, decoded, limits );
	if ( !result ) {
		return result;
	}
	result = ValidateManifestKey( decoded, expected );
	if ( !result ) {
		return result;
	}
	manifest = std::move( decoded );
	return Success();
}

Result DecodeManifestForKey( const std::vector<std::uint8_t> &encoded,
		const ManifestExpectation &expected, Manifest &manifest,
		const DecodeLimits &limits ) {
	return DecodeManifestForKey( encoded.data(), encoded.size(), expected, manifest, limits );
}

} // namespace idLevelLoadCache
