/*
===========================================================================

openQ4 connect-response roster
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#include "ConnectRoster.h"

#include <cstring>

namespace idConnectRoster {

static bool ValidEntry( const entry_t &entry, std::size_t nameBytes ) {
	return entry.slot < MAX_ENTRIES && ( entry.flags & ~FLAGS_KNOWN ) == 0 && entry.team <= 1 &&
		nameBytes <= static_cast<std::size_t>( MAX_NAME_BYTES );
}

std::size_t Encode( const roster_t &roster, std::uint8_t *out, std::size_t capacity ) {
	if ( out == nullptr || roster.count < 0 || roster.count > MAX_ENTRIES || roster.connecting > MAX_ENTRIES ) {
		return 0;
	}
	std::size_t used = 0;
	const auto put = [&]( const void *data, std::size_t bytes ) {
		if ( bytes > capacity - used ) {
			return false;
		}
		std::memcpy( out + used, data, bytes );
		used += bytes;
		return true;
	};
	const std::uint8_t head[ 3 ] = { VERSION, roster.connecting, static_cast<std::uint8_t>( roster.count ) };
	if ( capacity < sizeof( head ) || !put( head, sizeof( head ) ) ) {
		return 0;
	}
	for ( int i = 0; i < roster.count; i++ ) {
		const entry_t &entry = roster.entries[ i ];
		const void *terminator = std::memchr( entry.name, '\0', sizeof( entry.name ) );
		if ( terminator == nullptr ) {
			return 0;
		}
		const std::size_t nameBytes = static_cast<std::size_t>( static_cast<const char *>( terminator ) - entry.name );
		const std::uint8_t fields[ 3 ] = { entry.slot, entry.flags, entry.team };
		if ( !ValidEntry( entry, nameBytes ) || !put( fields, sizeof( fields ) ) || !put( entry.name, nameBytes + 1 ) ) {
			return 0;
		}
	}
	return used;
}

std::size_t EncodeWithin( roster_t &roster, std::uint8_t *out, std::size_t capacity, std::size_t limit ) {
	if ( Encode( roster, out, capacity ) == 0 ) {
		return 0;
	}
	const std::size_t room = limit < capacity ? limit : capacity;
	while ( true ) {
		const std::size_t used = Encode( roster, out, room );
		if ( used > 0 || roster.count <= 0 ) {
			return used;
		}
		roster.count--;
	}
}

bool Decode( const std::uint8_t *data, std::size_t size, roster_t &roster ) {
	std::memset( &roster, 0, sizeof( roster ) );
	if ( data == nullptr || size < 3 || data[ 0 ] != VERSION || data[ 1 ] > MAX_ENTRIES || data[ 2 ] > MAX_ENTRIES ) {
		return false;
	}
	roster_t decoded;
	std::memset( &decoded, 0, sizeof( decoded ) );
	decoded.connecting = data[ 1 ];
	decoded.count = data[ 2 ];
	std::size_t at = 3;
	for ( int i = 0; i < decoded.count; i++ ) {
		entry_t &entry = decoded.entries[ i ];
		if ( size - at < 4 ) {
			return false;
		}
		entry.slot = data[ at ];
		entry.flags = data[ at + 1 ];
		entry.team = data[ at + 2 ];
		at += 3;
		const void *terminator = std::memchr( data + at, '\0', size - at );
		if ( terminator == nullptr ) {
			return false;
		}
		const std::size_t nameBytes = static_cast<std::size_t>( static_cast<const std::uint8_t *>( terminator ) - ( data + at ) );
		if ( !ValidEntry( entry, nameBytes ) ) {
			return false;
		}
		std::memcpy( entry.name, data + at, nameBytes );
		entry.name[ nameBytes ] = '\0';
		at += nameBytes + 1;
	}
	if ( at != size ) {
		return false;
	}
	roster = decoded;
	return true;
}

} // namespace idConnectRoster
