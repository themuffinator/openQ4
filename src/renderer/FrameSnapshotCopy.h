// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef OPENQ4_FRAME_SNAPSHOT_COPY_H
#define OPENQ4_FRAME_SNAPSHOT_COPY_H

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <new>
#include <vector>

// The identities one view's snapshot has already frozen: producer object to
// its copy, or to nullptr for a mark. Every insertion succeeds, since aliasing
// decides which records share a copy. Storage persists across views and a view
// starts by advancing the generation, so a view pays neither node allocations
// nor a clear. Producer-only, one view at a time. A returned value slot is
// valid until the next insertion.
class idFrameSnapshotIdentities {
	struct entry_t {
		const void *key;
		void *value;
		std::uint32_t generation;		// live in the current view when equal
	};
	std::vector<entry_t> entries;
	std::size_t count = 0;
	std::uint32_t generation = 1;
	static std::size_t Hash( const void *key ) {
		std::size_t hash = reinterpret_cast<std::uintptr_t>( key ) >> 4;
		hash ^= hash >> 17;
		hash *= 0xed5ad4bbu;
		return hash ^ ( hash >> 11 );
	}
	std::size_t Slot( const void *key ) const {
		const std::size_t mask = entries.size() - 1;
		std::size_t slot = Hash( key ) & mask;
		while ( entries[slot].generation == generation && entries[slot].key != key ) slot = ( slot + 1 ) & mask;
		return slot;
	}
	void Grow() {
		std::vector<entry_t> previous( entries.size() < 256 ? 256 : entries.size() * 2, entry_t{ nullptr, nullptr, 0 } );
		previous.swap( entries );
		for ( const entry_t &entry : previous ) {
			if ( entry.generation == generation ) entries[Slot( entry.key )] = entry;
		}
	}
public:
	// Forget the previous view's identities.
	void Begin() {
		count = 0;
		if ( ++generation == 0 ) {
			for ( entry_t &entry : entries ) entry.generation = 0;
			generation = 1;
		}
	}
	// Drop the storage itself (video restart, world free, shutdown).
	void Release() {
		std::vector<entry_t>().swap( entries );
		count = 0;
	}
	std::size_t Size() const { return count; }
	std::size_t Capacity() const { return entries.size(); }
	bool Contains( const void *key ) const {
		return !entries.empty() && entries[Slot( key )].generation == generation;
	}
	// The value recorded for key in this view, or nullptr.
	void *Find( const void *key ) const {
		if ( entries.empty() ) return nullptr;
		const entry_t &entry = entries[Slot( key )];
		return entry.generation == generation ? entry.value : nullptr;
	}
	// The value slot for key; a key new to this view starts at nullptr.
	void *&Insert( const void *key, bool *inserted = nullptr ) {
		if ( ( count + 1 ) * 2 > entries.size() ) Grow();
		entry_t &entry = entries[Slot( key )];
		const bool fresh = entry.generation != generation;
		if ( fresh ) {
			entry = entry_t{ key, nullptr, generation };
			++count;
		}
		if ( inserted != nullptr ) *inserted = fresh;
		return entry.value;
	}
};

// A view is frozen atomically on the producer. Identical source ranges in
// that view can share their submitted copy. Never retain this lookup across
// views: a dynamic model may rebuild the same address for the next view.
class idFrameSnapshotCopies {
	struct entry_t {
		const void *source;
		std::size_t bytes;
		void *data;
	};
	entry_t *entries = nullptr;
	std::size_t capacity = 0, count = 0;
	static constexpr std::size_t MaxCapacity = 65536;
	static std::size_t Hash( const void *source, std::size_t bytes ) {
		std::size_t hash = ( reinterpret_cast<std::uintptr_t>( source ) >> 4 ) ^ bytes;
		hash ^= hash >> 17;
		hash *= 0xed5ad4bbu;
		return hash ^ ( hash >> 11 );
	}
	std::size_t Slot( const void *source, std::size_t bytes ) const {
		std::size_t slot = Hash( source, bytes ) & ( capacity - 1 );
		while ( entries[slot].source != nullptr ) {
			if ( entries[slot].source == source && entries[slot].bytes == bytes ) break;
			slot = ( slot + 1 ) & ( capacity - 1 );
		}
		return slot;
	}
	template<class Allocate> bool Grow( Allocate allocate ) {
		if ( capacity == MaxCapacity ) return false;
		const std::size_t previousCapacity = capacity;
		entry_t *previous = entries;
		capacity = capacity == 0 ? 64 : capacity * 2;
		entries = static_cast<entry_t *>( allocate( capacity * sizeof( entry_t ) ) );
		for ( std::size_t i = 0; i < capacity; ++i ) new ( entries + i ) entry_t{};
		for ( std::size_t i = 0; i < previousCapacity; ++i ) {
			if ( previous[i].source != nullptr ) entries[Slot( previous[i].source, previous[i].bytes )] = previous[i];
		}
		return true;
	}
public:
	struct result_t { void *data; bool reused; };
	// A range this view already froze, whoever owns its bytes, or nullptr.
	void *Find( const void *source, std::size_t bytes ) const {
		if ( source == nullptr || bytes == 0 || capacity == 0 ) return nullptr;
		const entry_t &entry = entries[Slot( source, bytes )];
		return entry.source != nullptr ? entry.data : nullptr;
	}
	// Record bytes owned elsewhere (a retained lease held by this frame) so the
	// same range is not validated again in this view. A full table only costs
	// the later lookups; the caller's data stays valid either way.
	template<class Allocate> void Remember( const void *source, std::size_t bytes, void *data, Allocate allocate ) {
		if ( source == nullptr || bytes == 0 || data == nullptr || Find( source, bytes ) != nullptr ) return;
		if ( count >= capacity / 2 && !Grow( allocate ) ) return;
		entries[Slot( source, bytes )] = { source, bytes, data };
		++count;
	}
	template<class Allocate> result_t Copy( const void *source, std::size_t bytes, Allocate allocate ) {
		if ( source == nullptr || bytes == 0 ) return { nullptr, false };
		if ( capacity != 0 ) {
			const entry_t &entry = entries[Slot( source, bytes )];
			if ( entry.source != nullptr ) return { entry.data, true };
		}
		void *copy = allocate( bytes );
		std::memcpy( copy, source, bytes );
		// Both payload and lookup storage belong to the caller's frame arena.
		// Keep at least half the table empty, bounding probes. Once the table
		// reaches its limit, new ranges simply take the ordinary copy path.
		if ( count >= capacity / 2 && !Grow( allocate ) ) return { copy, false };
		entries[Slot( source, bytes )] = { source, bytes, copy };
		++count;
		return { copy, false };
	}
};

#endif
