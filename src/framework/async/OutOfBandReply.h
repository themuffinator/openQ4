/*
===========================================================================

openQ4 out-of-band reply budget
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#ifndef __OUTOFBANDREPLY_H__
#define __OUTOFBANDREPLY_H__

// The server's connectionless replies travel outside the message channel, so
// nothing fragments them but the IP layer, and a path that filters IP
// fragments or loses IPv6's Packet Too Big drops them whole. Every reply a
// client needs to list or join a server is therefore kept to what a
// 1280-byte path carries in one packet: 1232 bytes of UDP payload under IPv6's
// 48 bytes of headers, which also fits IPv4's 28.
//
// The server-info dictionary alone is about 1.5 KB at its defaults. An info
// reply keeps the keys the server browser and its details card read, then adds
// the rest in order while they fit; a connect reply to a client that can take
// it carries only what the loading screen needs, and the full dictionary
// follows in the channel's game-init message, which the channel fragments.
//
// It has no engine dependencies, so the native tests drive it directly.

namespace idOutOfBandReply {

static constexpr int BUDGET_BYTES = 1232;

// A connect request may end with this tag and a capability mask, after the
// field the server has never read. Servers that do not know it ignore it.
static constexpr int CAPABILITY_TAG = 0x4f513443;		// "OQ4C"
static constexpr int CAPABILITY_DEFERRED_SERVERINFO = 1;
static constexpr int CAPABILITIES = CAPABILITY_DEFERRED_SERVERINFO;

// The keys the server browser, its filters and its details card read, most
// important first. An info reply always carries those the server has.
static const char *const BROWSER_KEYS[] = {
	"si_name", "si_map", "si_gameType", "si_maxPlayers", "si_privatePlayers", "si_usePass", "si_pure",
	"fs_game", "fs_game_base", "net_serverDedicated", "si_repeater", "si_idleServer", "si_mapName",
	"si_entityFilter", "si_fragLimit", "si_timeLimit", "si_captureLimit", "si_controlTime", "si_roundLimit",
	"si_spectators", "si_teamDamage", "si_autobalance", "sv_punkbuster", "devmap",
};
static constexpr int BROWSER_KEY_COUNT = static_cast<int>( sizeof( BROWSER_KEYS ) / sizeof( BROWSER_KEYS[0] ) );

// What a deferred connect reply carries: enough to name the server and map
// while the game-init message is on its way.
static const char *const LOADING_KEYS[] = {
	"si_name", "si_map", "si_gameType", "si_maxPlayers", "si_pure", "fs_game", "net_serverDedicated",
};
static constexpr int LOADING_KEY_COUNT = static_cast<int>( sizeof( LOADING_KEYS ) / sizeof( LOADING_KEYS[0] ) );

// idlib redefines the C string functions as macros in engine code, so this
// header counts and compares on its own.
inline int Length( const char *text ) {
	int length = 0;
	while ( text != nullptr && text[ length ] != '\0' ) {
		length++;
	}
	return length;
}

inline bool Same( const char *a, const char *b ) {
	if ( a == nullptr || b == nullptr ) {
		return false;
	}
	while ( *a != '\0' && *a == *b ) {
		a++;
		b++;
	}
	return *a == *b;
}

// A NUL-terminated string as idBitMsg::WriteString writes it.
inline int StringBytes( const char *text ) {
	return Length( text ) + 1;
}

// One key and value of idBitMsg::WriteDeltaDict against no base.
inline int PairBytes( const char *key, const char *value ) {
	return StringBytes( key ) + StringBytes( value );
}

// A dictionary written against no base ends with two empty strings.
static constexpr int DICT_END_BYTES = 2;

inline bool InList( const char *key, const char *const *list, int count ) {
	for ( int i = 0; i < count; i++ ) {
		if ( Same( key, list[i] ) ) {
			return true;
		}
	}
	return false;
}

// Chooses which of count key/value pairs to write within budget bytes for the
// whole dictionary. Pairs named in required are always kept; the others are
// kept in order while they fit, so a small dictionary is written whole.
// Sets keep[i] and returns the bytes the chosen dictionary takes.
inline int ChooseKeys( const char *const *keys, const char *const *values, int count,
		const char *const *required, int requiredCount, int budget, bool *keep ) {
	int used = DICT_END_BYTES;
	for ( int i = 0; i < count; i++ ) {
		keep[i] = InList( keys[i], required, requiredCount );
		if ( keep[i] ) {
			used += PairBytes( keys[i], values[i] );
		}
	}
	for ( int i = 0; i < count; i++ ) {
		if ( !keep[i] && used + PairBytes( keys[i], values[i] ) <= budget ) {
			keep[i] = true;
			used += PairBytes( keys[i], values[i] );
		}
	}
	return used;
}

// The longest player name, in bytes, an info reply can carry for count players
// whose names are nameBytes[] long (without the NUL), when available bytes are
// left for the player list. Each player also takes a slot, ping and rate (seven
// bytes) and the name's NUL; the list ends with a byte. Returns -1 when every
// name fits whole, else a length to cut each longer name to (0 or more).
inline int NameLimit( const int *nameBytes, int count, int available ) {
	int whole = 1;
	int longest = 0;
	for ( int i = 0; i < count; i++ ) {
		whole += 8 + nameBytes[i];
		longest = nameBytes[i] > longest ? nameBytes[i] : longest;
	}
	if ( whole <= available ) {
		return -1;
	}
	for ( int limit = longest - 1; limit > 0; limit-- ) {
		int used = 1;
		for ( int i = 0; i < count; i++ ) {
			used += 8 + ( nameBytes[i] < limit ? nameBytes[i] : limit );
		}
		if ( used <= available ) {
			return limit;
		}
	}
	return 0;
}

} // namespace idOutOfBandReply

#endif /* !__OUTOFBANDREPLY_H__ */
