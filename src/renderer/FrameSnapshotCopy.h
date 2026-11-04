// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef OPENQ4_FRAME_SNAPSHOT_COPY_H
#define OPENQ4_FRAME_SNAPSHOT_COPY_H

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <new>

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
