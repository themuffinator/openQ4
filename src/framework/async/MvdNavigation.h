#ifndef OPENQ4_MVD_NAVIGATION_H
#define OPENQ4_MVD_NAVIGATION_H

// MVD 1.3 navigation payloads: structured markers, independently restorable
// keyframes and the two trailing directories that list them. Every parser is
// bounded and rejects trailing bytes; nothing here allocates per snapshot.

#include <cstdint>
#include <cstring>
#include <vector>

namespace openq4_mvd {

static const int MARKER_TEXT_BYTES = 120;
static const int MAX_MARKERS = 20000;
static const int MAX_KEYFRAMES = 20000;
static const int MARKER_FIXED_BYTES = 22;
static const int KEYFRAME_FIXED_BYTES = 20;
static const int KEYFRAME_TABLE_ENTRY_BYTES = 12;
static const int BASELINE_VERSION = 1;

struct Marker {
	int gameTime = 0;
	int kind = 0;
	int client = -1;
	int instance = -1;
	int value0 = 0;
	int value1 = 0;
	char text[ MARKER_TEXT_BYTES + 1 ] = {};
};

struct Keyframe {
	int gameTime = 0;
	int fileOffset = 0;
	int sequence = 0;
};

// The fixed part of a keyframe record, followed by three length-delimited
// blocks: engine map state, game network info and the game's delta baseline.
struct KeyframeHeader {
	int gameFrame = 0;
	int gameTime = 0;
	int sequence = 0;
	int reliableCount = 0;
	int recordCount = 0;
	int mapStateOffset = 0;
	int mapStateBytes = 0;
	int networkInfoOffset = 0;
	int networkInfoBytes = 0;
	int baselineOffset = 0;
	int baselineBytes = 0;
};

inline std::uint32_t ReadU32( const std::uint8_t *data ) {
	return static_cast<std::uint32_t>( data[0] ) |
		( static_cast<std::uint32_t>( data[1] ) << 8 ) |
		( static_cast<std::uint32_t>( data[2] ) << 16 ) |
		( static_cast<std::uint32_t>( data[3] ) << 24 );
}

inline std::uint16_t ReadU16( const std::uint8_t *data ) {
	return static_cast<std::uint16_t>( data[0] | ( data[1] << 8 ) );
}

inline int ReadI32( const std::uint8_t *data ) {
	return static_cast<int>( ReadU32( data ) );
}

inline void AppendU32( std::vector<std::uint8_t> &out, std::uint32_t value ) {
	out.push_back( static_cast<std::uint8_t>( value ) );
	out.push_back( static_cast<std::uint8_t>( value >> 8 ) );
	out.push_back( static_cast<std::uint8_t>( value >> 16 ) );
	out.push_back( static_cast<std::uint8_t>( value >> 24 ) );
}

inline void AppendU16( std::vector<std::uint8_t> &out, std::uint16_t value ) {
	out.push_back( static_cast<std::uint8_t>( value ) );
	out.push_back( static_cast<std::uint8_t>( value >> 8 ) );
}

// Text is display data. Control bytes would let a recording drive console
// colour escapes or line breaks, so they never survive either direction.
inline bool MarkerTextValid( const std::uint8_t *text, int length ) {
	if ( length < 0 || length > MARKER_TEXT_BYTES ) { return false; }
	for ( int i = 0; i < length; ++i ) {
		if ( text[i] < 0x20 || text[i] == 0x7f ) { return false; }
	}
	return true;
}

inline bool MarkerFieldsValid( const Marker &marker, int kindCount, int maxClients, int maxInstances ) {
	return marker.kind > 0 && marker.kind < kindCount &&
		marker.client >= -1 && marker.client < maxClients &&
		marker.instance >= -1 && marker.instance < maxInstances;
}

// Marker record and directory entry share one body:
// time, kind, client, instance, value0, value1, text length, text.
inline void AppendMarkerBody( std::vector<std::uint8_t> &out, const Marker &marker ) {
	const int length = static_cast<int>( strnlen( marker.text, MARKER_TEXT_BYTES ) );
	AppendU32( out, static_cast<std::uint32_t>( marker.gameTime ) );
	AppendU16( out, static_cast<std::uint16_t>( marker.kind ) );
	out.push_back( static_cast<std::uint8_t>( static_cast<std::int8_t>( marker.client ) ) );
	out.push_back( static_cast<std::uint8_t>( static_cast<std::int8_t>( marker.instance ) ) );
	AppendU32( out, static_cast<std::uint32_t>( marker.value0 ) );
	AppendU32( out, static_cast<std::uint32_t>( marker.value1 ) );
	AppendU16( out, static_cast<std::uint16_t>( length ) );
	out.insert( out.end(), marker.text, marker.text + length );
}

inline bool ParseMarkerBody( const std::uint8_t *data, int available, Marker &marker, int &consumed,
		int kindCount, int maxClients, int maxInstances ) {
	const int fixed = 18;
	if ( data == nullptr || available < fixed ) { return false; }
	marker = Marker();
	marker.gameTime = ReadI32( data );
	marker.kind = ReadU16( data + 4 );
	marker.client = static_cast<std::int8_t>( data[6] );
	marker.instance = static_cast<std::int8_t>( data[7] );
	marker.value0 = ReadI32( data + 8 );
	marker.value1 = ReadI32( data + 12 );
	const int length = ReadU16( data + 16 );
	if ( length > MARKER_TEXT_BYTES || fixed + length > available ||
		 !MarkerTextValid( data + fixed, length ) ||
		 !MarkerFieldsValid( marker, kindCount, maxClients, maxInstances ) ) {
		return false;
	}
	memcpy( marker.text, data + fixed, static_cast<size_t>( length ) );
	marker.text[length] = '\0';
	consumed = fixed + length;
	return true;
}

// A marker record is the game frame followed by the marker body.
inline bool ParseMarkerRecord( const std::uint8_t *data, int length, Marker &marker, int &gameFrame,
		int kindCount, int maxClients, int maxInstances ) {
	int consumed = 0;
	if ( data == nullptr || length < 4 ||
		 !ParseMarkerBody( data + 4, length - 4, marker, consumed, kindCount, maxClients, maxInstances ) ||
		 4 + consumed != length ) {
		return false;
	}
	gameFrame = ReadI32( data );
	return true;
}

inline bool ParseMarkerTable( const std::uint8_t *data, int length, std::vector<Marker> &markers,
		int kindCount, int maxClients, int maxInstances ) {
	markers.clear();
	if ( data == nullptr || length < 4 ) { return false; }
	const std::uint32_t count = ReadU32( data );
	if ( count > static_cast<std::uint32_t>( MAX_MARKERS ) ) { return false; }
	int cursor = 4;
	int previousTime = 0;
	for ( std::uint32_t i = 0; i < count; ++i ) {
		Marker marker;
		int consumed = 0;
		if ( !ParseMarkerBody( data + cursor, length - cursor, marker, consumed, kindCount, maxClients, maxInstances ) ||
			 ( i > 0 && marker.gameTime < previousTime ) ) {
			markers.clear();
			return false;
		}
		previousTime = marker.gameTime;
		markers.push_back( marker );
		cursor += consumed;
	}
	if ( cursor != length ) {
		markers.clear();
		return false;
	}
	return true;
}

inline bool ParseKeyframeTable( const std::uint8_t *data, int length, int streamStart, int tableOffset,
		std::vector<Keyframe> &keyframes ) {
	keyframes.clear();
	if ( data == nullptr || length < 4 ) { return false; }
	const std::uint32_t count = ReadU32( data );
	if ( count > static_cast<std::uint32_t>( MAX_KEYFRAMES ) ||
		 static_cast<std::int64_t>( count ) * KEYFRAME_TABLE_ENTRY_BYTES + 4 != length ) {
		return false;
	}
	for ( std::uint32_t i = 0; i < count; ++i ) {
		const std::uint8_t *entry = data + 4 + i * KEYFRAME_TABLE_ENTRY_BYTES;
		Keyframe keyframe;
		keyframe.gameTime = ReadI32( entry );
		keyframe.fileOffset = ReadI32( entry + 4 );
		keyframe.sequence = ReadI32( entry + 8 );
		if ( keyframe.fileOffset < streamStart || keyframe.fileOffset >= tableOffset || keyframe.sequence <= 0 ||
			 ( !keyframes.empty() &&
			   ( keyframe.gameTime <= keyframes.back().gameTime ||
				 keyframe.fileOffset <= keyframes.back().fileOffset ||
				 keyframe.sequence <= keyframes.back().sequence ) ) ) {
			keyframes.clear();
			return false;
		}
		keyframes.push_back( keyframe );
	}
	return true;
}

inline void AppendKeyframeTable( std::vector<std::uint8_t> &out, const std::vector<Keyframe> &keyframes ) {
	AppendU32( out, static_cast<std::uint32_t>( keyframes.size() ) );
	for ( const Keyframe &keyframe : keyframes ) {
		AppendU32( out, static_cast<std::uint32_t>( keyframe.gameTime ) );
		AppendU32( out, static_cast<std::uint32_t>( keyframe.fileOffset ) );
		AppendU32( out, static_cast<std::uint32_t>( keyframe.sequence ) );
	}
}

inline bool ParseKeyframeBlock( const std::uint8_t *data, int length, int &cursor, int &offset, int &bytes ) {
	if ( cursor + 4 > length ) { return false; }
	const std::uint32_t blockBytes = ReadU32( data + cursor );
	cursor += 4;
	if ( blockBytes == 0 || blockBytes > static_cast<std::uint32_t>( length - cursor ) ) { return false; }
	offset = cursor;
	bytes = static_cast<int>( blockBytes );
	cursor += bytes;
	return true;
}

inline bool ParseKeyframeHeader( const std::uint8_t *data, int length, KeyframeHeader &header ) {
	header = KeyframeHeader();
	if ( data == nullptr || length < KEYFRAME_FIXED_BYTES ) { return false; }
	header.gameFrame = ReadI32( data );
	header.gameTime = ReadI32( data + 4 );
	header.sequence = ReadI32( data + 8 );
	header.reliableCount = ReadI32( data + 12 );
	header.recordCount = ReadI32( data + 16 );
	int cursor = 20;
	if ( header.sequence <= 0 || header.reliableCount < 0 || header.recordCount < header.sequence + header.reliableCount ||
		 !ParseKeyframeBlock( data, length, cursor, header.mapStateOffset, header.mapStateBytes ) ||
		 !ParseKeyframeBlock( data, length, cursor, header.networkInfoOffset, header.networkInfoBytes ) ||
		 !ParseKeyframeBlock( data, length, cursor, header.baselineOffset, header.baselineBytes ) ||
		 cursor != length ) {
		header = KeyframeHeader();
		return false;
	}
	return true;
}

// Game baseline grammar version 1: version, sequence, instance flag, entry count,
// entries of (entity number, state bytes, state), then the entity PVS words.
// Entity numbers ascend strictly; ENTITYNUM_NONE carries the game state.
inline bool ValidateBaseline( const std::uint8_t *data, int length, int sequence, int maxEntities,
		int maxStateBytes, int pvsWords ) {
	if ( data == nullptr || length < 13 || ReadI32( data ) != BASELINE_VERSION ||
		 ReadI32( data + 4 ) != sequence || data[8] > 1 ) {
		return false;
	}
	const std::uint32_t count = ReadU32( data + 9 );
	if ( count > static_cast<std::uint32_t>( maxEntities ) ) { return false; }
	int cursor = 13;
	int previous = -1;
	for ( std::uint32_t i = 0; i < count; ++i ) {
		if ( cursor + 4 > length ) { return false; }
		const int entity = ReadU16( data + cursor );
		const int bytes = ReadU16( data + cursor + 2 );
		cursor += 4;
		if ( entity <= previous || entity >= maxEntities || bytes <= 0 || bytes > maxStateBytes ||
			 bytes > length - cursor ) {
			return false;
		}
		previous = entity;
		cursor += bytes;
	}
	return static_cast<std::int64_t>( cursor ) + static_cast<std::int64_t>( pvsWords ) * 4 == length;
}

// The latest keyframe at or before a target, or -1. Keyframes ascend in time.
inline int KeyframeAtOrBefore( const std::vector<Keyframe> &keyframes, int targetTime ) {
	int found = -1;
	for ( int i = 0; i < static_cast<int>( keyframes.size() ); ++i ) {
		if ( keyframes[i].gameTime > targetTime ) { break; }
		found = i;
	}
	return found;
}

// Marker navigation. Seeking to a marker lands leadMS before it. Stepping
// continues from the marker last selected while playback is still near it, so
// repeated next/previous requests walk the list instead of reselecting one.
inline bool MarkerMatches( const Marker &marker, int kindFilter ) {
	return kindFilter <= 0 || marker.kind == kindFilter;
}

inline int FindMarker( const std::vector<Marker> &markers, int nowTime, int direction, int kindFilter,
		int leadMS, int lastIndex ) {
	const int count = static_cast<int>( markers.size() );
	if ( count == 0 || direction == 0 ) { return -1; }
	if ( lastIndex >= 0 && lastIndex < count ) {
		const int landed = markers[lastIndex].gameTime - leadMS;
		if ( nowTime >= landed - 1000 && nowTime <= markers[lastIndex].gameTime + 1000 ) {
			for ( int i = lastIndex + direction; i >= 0 && i < count; i += direction ) {
				if ( MarkerMatches( markers[i], kindFilter ) ) { return i; }
			}
			return -1;
		}
	}
	if ( direction > 0 ) {
		for ( int i = 0; i < count; ++i ) {
			if ( MarkerMatches( markers[i], kindFilter ) && markers[i].gameTime - leadMS > nowTime + 250 ) { return i; }
		}
		return -1;
	}
	for ( int i = count - 1; i >= 0; --i ) {
		if ( MarkerMatches( markers[i], kindFilter ) && markers[i].gameTime - leadMS < nowTime - 1000 ) { return i; }
	}
	return -1;
}

} // namespace openq4_mvd

#endif
