// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef OPENQ4_FRAME_SNAPSHOT_RETAIN_H
#define OPENQ4_FRAME_SNAPSHOT_RETAIN_H
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

// Producer-only cache of immutable owned index streams. Source addresses are
// hints, never lifetime/version guarantees: validate the complete bytes on
// every acquisition. Submitted leases survive replacement and cache eviction.
//
// Eviction is least-recently-used and incremental: an insertion over the byte
// or entry budget drops only the oldest entries until the new stream fits.
// Clearing the whole cache under pressure freed up to 32,768 streams in one
// frame (a 15-20 ms stall at 1440p Quality) and then re-copied every stream
// the following frames. Streams that frame-temporary geometry uses once age
// out at the tail, while streams reused every frame stay at the front.
class idFrameSnapshotRetainedIndexes {
public:
	using block_t = std::vector<std::uint32_t>;
	using lease_t = std::shared_ptr<const block_t>;
	struct result_t { lease_t lease; bool reused; };
	static constexpr std::size_t maximumEntries = 32768;
private:
	struct entry_t { lease_t lease; std::list<const void *>::iterator recency; };
	std::unordered_map<const void *, entry_t> entries;
	std::list<const void *> recency;		// front: most recently acquired
	std::size_t residentBytes = 0;
	const std::size_t budget;
	void Erase( std::unordered_map<const void *, entry_t>::iterator found ) {
		residentBytes -= found->second.lease->size() * sizeof( std::uint32_t );
		recency.erase( found->second.recency );
		entries.erase( found );
	}
public:
	explicit idFrameSnapshotRetainedIndexes( std::size_t bytes = 64 * 1024 * 1024 ) : budget( bytes ) {}
	void Clear() { entries.clear(); recency.clear(); residentBytes = 0; }
	std::size_t Size() const { return entries.size(); }
	std::size_t ResidentBytes() const { return residentBytes; }
	result_t Acquire( const void *source, std::size_t bytes ) {
		if ( source == nullptr || bytes == 0 || bytes > budget || bytes % sizeof( std::uint32_t ) != 0 ) return {};
		auto found = entries.find( source );
		if ( found != entries.end() ) {
			const auto &old = found->second.lease;
			if ( old->size() * sizeof( std::uint32_t ) == bytes && std::memcmp( old->data(), source, bytes ) == 0 ) {
				recency.splice( recency.begin(), recency, found->second.recency );
				return { old, true };
			}
			Erase( found );
		}
		// Bound both payload memory and bookkeeping for tiny index ranges.
		while ( !recency.empty() && ( residentBytes + bytes > budget || entries.size() >= maximumEntries ) ) {
			Erase( entries.find( recency.back() ) );
		}
		auto copy = std::make_shared<block_t>( bytes / sizeof( std::uint32_t ) );
		std::memcpy( copy->data(), source, bytes );
		recency.push_front( source );
		entries.emplace( source, entry_t{ copy, recency.begin() } );
		residentBytes += bytes;
		return { copy, false };
	}
};
#endif
