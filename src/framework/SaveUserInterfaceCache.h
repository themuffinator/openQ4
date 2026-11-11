// Copyright (C) 2026 DarkMatter Productions
#ifndef OPENQ4_SAVE_USER_INTERFACE_CACHE_H
#define OPENQ4_SAVE_USER_INTERFACE_CACHE_H

#include <array>
#include <cstddef>
#include <vector>

// Owned by one synchronous game snapshot. GUI instances remain alive while the
// save graph is visited; any Save method that changes a GUI must invalidate it
// first. Never reuse records across snapshots or for wall-clock-driven retained UI.
class SaveUserInterfaceCache {
public:
	static constexpr std::size_t MaxEntries = 256;
	static constexpr std::size_t MaxRecordBytes = 1024 * 1024;
	static constexpr std::size_t MaxBytes = 8 * 1024 * 1024;

	const std::vector<unsigned char>* Find( const void* key ) const {
		for ( const auto& entry : entries ) if ( key && entry.key == key ) return &entry.bytes;
		return nullptr;
	}
	void Invalidate( const void* key ) {
		for ( auto& entry : entries ) if ( key && entry.key == key ) {
			residentBytes -= entry.bytes.size();
			entry.key = nullptr;
			std::vector<unsigned char>().swap( entry.bytes );
		}
	}
	void Store( const void* key, const unsigned char* data, std::size_t size ) {
		if ( !key || !data || !size || size > MaxRecordBytes || Find( key ) || size > MaxBytes - residentBytes ) return;
		for ( auto& entry : entries ) if ( !entry.key ) {
			entry.bytes.assign( data, data + size );
			entry.key = key;
			residentBytes += size;
			return;
		}
	}
	std::size_t ResidentBytes() const { return residentBytes; }
	unsigned hits = 0, misses = 0, verified = 0;
private:
	struct Entry { const void* key = nullptr; std::vector<unsigned char> bytes; };
	std::array<Entry, MaxEntries> entries;
	std::size_t residentBytes = 0;
};

#endif
