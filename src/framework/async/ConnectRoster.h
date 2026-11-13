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

#ifndef __CONNECTROSTER_H__
#define __CONNECTROSTER_H__

#include <cstddef>
#include <cstdint>

// The players already in a game, sent after the server info of a connect
// response so a joining client's loading screen can list them. The block
// follows a tag and a length: a client that does not know it stops reading
// before it, and a server that does not send it leaves the roster unknown.
// It reaches only a client that has passed the challenge, and adds names and
// teams the client receives anyway once it is in the game.
namespace idConnectRoster {

static constexpr std::uint32_t TAG = 0x4f513452;		// "OQ4R"
static constexpr std::uint8_t VERSION = 1;
static constexpr int MAX_ENTRIES = 32;					// MAX_ASYNC_CLIENTS
static constexpr int MAX_NAME_BYTES = 47;
static constexpr std::size_t MAX_BYTES = 3 + MAX_ENTRIES * ( 3 + MAX_NAME_BYTES + 1 );

enum : std::uint8_t {
	FLAG_BOT		= 1,
	FLAG_SPECTATOR	= 2,
	FLAGS_KNOWN		= FLAG_BOT | FLAG_SPECTATOR
};

struct entry_t {
	std::uint8_t	slot;
	std::uint8_t	flags;
	std::uint8_t	team;							// 0 Marine, 1 Strogg
	char			name[ MAX_NAME_BYTES + 1 ];		// NUL-terminated, unsanitized
};

struct roster_t {
	std::uint8_t	connecting;						// players still connecting
	int				count;
	entry_t			entries[ MAX_ENTRIES ];
};

// The version, the connecting count and the entry count, then for each entry
// its slot, flags, team and NUL-terminated name. Returns the bytes written,
// or 0 when the roster is malformed or does not fit.
std::size_t Encode( const roster_t &roster, std::uint8_t *out, std::size_t capacity );

// As Encode, for a reply held to limit bytes for the block: drops entries from
// the end until the rest fits, and leaves 'roster' holding those it wrote.
// Returns 0 when the roster is malformed or not even its header fits.
std::size_t EncodeWithin( roster_t &roster, std::uint8_t *out, std::size_t capacity, std::size_t limit );

// Fills 'roster' from a whole block and returns true, or returns false with
// an empty roster for any other version, a count, slot, team or flag out of
// range, a name that is unterminated or too long, or bytes left over.
bool Decode( const std::uint8_t *data, std::size_t size, roster_t &roster );

} // namespace idConnectRoster

#endif /* !__CONNECTROSTER_H__ */
