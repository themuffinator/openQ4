// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstddef>

namespace openq4 {
// Optional SE_KEY payload. A fixed byte encoding preserves historical event
// layout and journals, while binding modifier/repeat state to this event.
// Other producers may omit it; no process identity or native authority exists.
struct KeyEventMetadata {
	bool control = false, shift = false, alt = false, repeated = false;
};
inline constexpr std::size_t KeyEventMetadataBytes = 8;
inline std::array<unsigned char,KeyEventMetadataBytes> EncodeKeyEventMetadata(const KeyEventMetadata& value) {
	return {'Q','4','K','1',static_cast<unsigned char>((value.control ? 1 : 0) | (value.shift ? 2 : 0) | (value.alt ? 4 : 0)),
		static_cast<unsigned char>(value.repeated ? 1 : 0),0,0};
}
inline bool DecodeKeyEventMetadata(const void* bytes, std::size_t length, KeyEventMetadata& out) {
	if (!bytes || length != KeyEventMetadataBytes) return false;
	const auto* data = static_cast<const unsigned char*>(bytes);
	if (data[0] != 'Q' || data[1] != '4' || data[2] != 'K' || data[3] != '1' || data[4] > 7 || data[5] > 1 || data[6] || data[7]) return false;
	out = {(data[4]&1) != 0,(data[4]&2) != 0,(data[4]&4) != 0,data[5] != 0};
	return true;
}
} // namespace openq4
