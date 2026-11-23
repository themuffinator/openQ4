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
//
// Leases. Acquire returns a shared owner from every acquisition. Lease returns
// the bytes and appends an owner to the caller's holder only the first time a
// version is leased under an epoch. A frame names its holder with an epoch
// that changes whenever the holder is emptied, so the holder already owns any
// version it leased under the current epoch. A replaced version starts
// unleased. One owner per version per frame replaces one per acquisition: the
// atomic count updates and holder growth were paid for every reused range.
class idFrameSnapshotRetainedIndexes {
public:
	using block_t = std::vector<std::uint32_t>;
	using lease_t = std::shared_ptr<const block_t>;
	struct result_t { lease_t lease; bool reused; };
	static constexpr std::size_t maximumEntries = 32768;
private:
	struct entry_t { lease_t lease; std::list<const void *>::iterator recency; std::uint64_t leaseEpoch; };
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
private:
	entry_t *AcquireEntry( const void *source, std::size_t bytes, bool &reused ) {
		reused = false;
		if ( source == nullptr || bytes == 0 || bytes > budget || bytes % sizeof( std::uint32_t ) != 0 ) return nullptr;
		auto found = entries.find( source );
		if ( found != entries.end() ) {
			const auto &old = found->second.lease;
			if ( old->size() * sizeof( std::uint32_t ) == bytes && std::memcmp( old->data(), source, bytes ) == 0 ) {
				recency.splice( recency.begin(), recency, found->second.recency );
				reused = true;
				return &found->second;
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
		residentBytes += bytes;
		return &entries.emplace( source, entry_t{ std::move( copy ), recency.begin(), 0 } ).first->second;
	}
public:
	result_t Acquire( const void *source, std::size_t bytes ) {
		bool reused;
		const entry_t *entry = AcquireEntry( source, bytes, reused );
		return entry != nullptr ? result_t{ entry->lease, reused } : result_t{};
	}
	// The bytes stay valid while holder keeps the owners it was given. epoch
	// is nonzero and changes whenever the holder is emptied.
	template<class Holder> const std::uint32_t *Lease( const void *source, std::size_t bytes, std::uint64_t epoch, Holder &holder, bool &reused ) {
		entry_t *entry = AcquireEntry( source, bytes, reused );
		if ( entry == nullptr ) return nullptr;
		if ( entry->leaseEpoch != epoch ) {
			holder.push_back( entry->lease );
			entry->leaseEpoch = epoch;
		}
		return entry->lease->data();
	}
};

// Producer-only cache of immutable owned vertex-class streams: vertices, CPU
// vertex-cache bytes, shadow vertices and the static per-vertex/per-face tables
// of geometry the caller classified as static (world areas and static models).
// The classification only selects candidates. A stream is immutable because
// the complete range compared equal on this acquisition: the source address and
// length are lookup hints, never a version or lifetime guarantee. Submitted
// leases own their bytes and survive replacement, refusal, eviction and Clear.
//
// A range that compares unequal was rewritten in place, so it is posed or
// deformed data that reached the static path. Its payload is dropped and the
// range is refused (the caller takes its ordinary per-view copy) for a doubling
// number of acquisitions, up to maximumRefusals, before it is tried again.
// A stream that derives its tangents once therefore costs two per-view copies,
// while one animated in place costs a comparison every 256 acquisitions rather
// than a comparison, an allocation and a release on every view.
//
// Eviction is least-recently-used and incremental, and leasing works as for
// the index cache.
class idFrameSnapshotRetainedStreams {
public:
	// Vertex consumers may load 16-byte aligned vectors from these bytes.
	struct alignas( 16 ) cell_t { unsigned char bytes[16]; };
	class block_t {
		std::unique_ptr<cell_t[]> cells;
		std::size_t length;
	public:
		// Default-initialized: the caller overwrites the complete range.
		explicit block_t( std::size_t bytes ) : cells( new cell_t[( bytes + sizeof( cell_t ) - 1 ) / sizeof( cell_t )] ), length( bytes ) {}
		const void *data() const { return cells.get(); }
		void *data() { return cells.get(); }
		std::size_t size() const { return length; }
	};
	using lease_t = std::shared_ptr<const block_t>;
	struct result_t { lease_t lease; bool reused; };
	static constexpr std::size_t maximumEntries = 32768;
	static constexpr unsigned maximumRefusals = 256;
private:
	struct key_t {
		const void *source;
		std::size_t bytes;
		bool operator==( const key_t &other ) const { return source == other.source && bytes == other.bytes; }
	};
	struct hash_t {
		std::size_t operator()( const key_t &key ) const {
			std::size_t hash = ( reinterpret_cast<std::uintptr_t>( key.source ) >> 4 ) ^ ( key.bytes * 0x9e3779b1u );
			hash ^= hash >> 17;
			hash *= 0xed5ad4bbu;
			return hash ^ ( hash >> 11 );
		}
	};
	struct entry_t {
		lease_t lease;				// empty while the range is refused
		std::list<key_t>::iterator recency;
		unsigned refusals;			// acquisitions still to refuse
		unsigned nextRefusals;		// length of the next refusal run
		std::uint64_t leaseEpoch;	// epoch this version was last leased under
	};
	using map_t = std::unordered_map<key_t, entry_t, hash_t>;
	map_t entries;
	std::list<key_t> recency;		// front: most recently acquired
	std::size_t residentBytes = 0;
	const std::size_t budget;
	void Erase( map_t::iterator found ) {
		if ( found->second.lease ) residentBytes -= found->second.lease->size();
		recency.erase( found->second.recency );
		entries.erase( found );
	}
	static lease_t Own( const void *source, std::size_t bytes ) {
		auto copy = std::make_shared<block_t>( bytes );
		std::memcpy( copy->data(), source, bytes );
		return copy;
	}
public:
	explicit idFrameSnapshotRetainedStreams( std::size_t bytes = 128 * 1024 * 1024 ) : budget( bytes ) {}
	void Clear() { entries.clear(); recency.clear(); residentBytes = 0; }
	std::size_t Size() const { return entries.size(); }
	std::size_t ResidentBytes() const { return residentBytes; }
private:
	entry_t *AcquireEntry( const void *source, std::size_t bytes, bool &reused ) {
		reused = false;
		if ( source == nullptr || bytes == 0 || bytes > budget ) return nullptr;
		const key_t key{ source, bytes };
		auto found = entries.find( key );
		if ( found != entries.end() ) {
			entry_t &entry = found->second;
			recency.splice( recency.begin(), recency, entry.recency );
			if ( entry.refusals > 0 ) {
				--entry.refusals;
				return nullptr;
			}
			if ( entry.lease ) {
				if ( std::memcmp( entry.lease->data(), source, bytes ) == 0 ) {
					reused = true;
					return &entry;
				}
				// Rewritten in place. The submitted version stays with its leases.
				residentBytes -= bytes;
				entry.lease.reset();
				entry.leaseEpoch = 0;
				entry.nextRefusals = entry.nextRefusals >= maximumRefusals / 2 ? maximumRefusals : entry.nextRefusals * 2;
				entry.refusals = entry.nextRefusals - 1;	// this acquisition is the first
				return nullptr;
			}
			// The refusal run ended: take a new version. This entry holds no
			// bytes and is the most recent, so pressure never evicts it here.
			while ( residentBytes + bytes > budget ) Erase( entries.find( recency.back() ) );
			entry.lease = Own( source, bytes );
			entry.leaseEpoch = 0;
			residentBytes += bytes;
			return &entry;
		}
		// Bound both payload memory and bookkeeping for tiny ranges.
		while ( !recency.empty() && ( residentBytes + bytes > budget || entries.size() >= maximumEntries ) ) {
			Erase( entries.find( recency.back() ) );
		}
		recency.push_front( key );
		residentBytes += bytes;
		return &entries.emplace( key, entry_t{ Own( source, bytes ), recency.begin(), 0, 1, 0 } ).first->second;
	}
public:
	result_t Acquire( const void *source, std::size_t bytes ) {
		bool reused;
		const entry_t *entry = AcquireEntry( source, bytes, reused );
		return entry != nullptr ? result_t{ entry->lease, reused } : result_t{};
	}
	// As idFrameSnapshotRetainedIndexes::Lease.
	template<class Holder> const void *Lease( const void *source, std::size_t bytes, std::uint64_t epoch, Holder &holder, bool &reused ) {
		entry_t *entry = AcquireEntry( source, bytes, reused );
		if ( entry == nullptr ) return nullptr;
		if ( entry->leaseEpoch != epoch ) {
			holder.push_back( entry->lease );
			entry->leaseEpoch = epoch;
		}
		return entry->lease->data();
	}
};
#endif
