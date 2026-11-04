// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef OPENQ4_FRAME_SNAPSHOT_RETAIN_H
#define OPENQ4_FRAME_SNAPSHOT_RETAIN_H
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

// Producer-only cache of immutable owned index streams. Source addresses are
// hints, never lifetime/version guarantees: validate the complete bytes on
// every acquisition. Submitted leases survive replacement and cache eviction.
class idFrameSnapshotRetainedIndexes {
public:
	using block_t = std::vector<std::uint32_t>;
	using lease_t = std::shared_ptr<const block_t>;
	struct result_t { lease_t lease; bool reused; };
private:
	std::unordered_map<const void *, lease_t> entries;
	std::size_t residentBytes = 0;
	const std::size_t budget;
public:
	explicit idFrameSnapshotRetainedIndexes( std::size_t bytes = 64 * 1024 * 1024 ) : budget( bytes ) {}
	void Clear() { entries.clear(); residentBytes = 0; }
	result_t Acquire( const void *source, std::size_t bytes ) {
		if ( source == nullptr || bytes == 0 || bytes > budget || bytes % sizeof( std::uint32_t ) != 0 ) return {};
		auto found = entries.find( source );
		if ( found != entries.end() ) {
			const auto &old = found->second;
			if ( old->size() * sizeof( std::uint32_t ) == bytes && std::memcmp( old->data(), source, bytes ) == 0 ) return { old, true };
			residentBytes -= old->size() * sizeof( std::uint32_t );
			entries.erase( found );
		}
		// Bound both payload memory and bookkeeping for tiny index ranges.
		if ( residentBytes + bytes > budget || entries.size() >= 32768 ) Clear();
		auto copy = std::make_shared<block_t>( bytes / sizeof( std::uint32_t ) );
		std::memcpy( copy->data(), source, bytes );
		entries.emplace( source, copy );
		residentBytes += bytes;
		return { copy, false };
	}
};
#endif
