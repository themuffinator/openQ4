// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "TextInputBroker.h"

namespace openq4::ui {

inline constexpr std::uint16_t TextInputBrokerWireVersion = 1;
inline constexpr std::size_t TextInputBrokerWireHeaderBytes = 80;
inline constexpr std::size_t TextInputBrokerWireMaxFrameBytes = TextInputBrokerWireHeaderBytes + TextInputMaxBytes;

// One exact owned sysEvent payload, explicitly little-endian; never a native
// C++ struct, pointer, editor identity, callback or delivery receipt. V1 layout:
//   0 magic "Q4TB"; 4 version:u16; 6 headerBytes:u16; 8 frameBytes:u32;
//  12 kind:u8; 13 origin:u8; 14 inputKind:u8; 15 offsetPresence:u8;
//  16 reserved:u32 (=0); 20 textBytes:u32;
//  24 sequence:u64; 32 window:u64; 40 session:u64; 48 composition:u64;
//  56 echoOf:u64; 64 selectionStart:u64; 72 selectionLength:u64;
//  80 exactly textBytes UTF-8 bytes, without a terminator or trailing padding.
// Presence bits 0/1 mean known start/length; absent offset slots must be zero.
// Offsets are normalized UTF-8 scalar boundaries, never native UTF-16 offsets.
// Explicit wire kinds 1..11: SessionBegin, SessionEnd, Begin, Preedit, Result,
// End, Cancel, Commit, WindowEnd, Echo, Poison. Origins 0..3: Unknown,
// UnmarkedCharacter, ObservedComposition, ProvenDirect. Input kinds 0..2:
// CancelComposition, Commit, Preedit. All other values/bits are rejected.
// Result carries metadata only. Unknown provenance remains Unknown; encoding
// ProvenDirect neither authenticates a provider nor authorizes text delivery.
// Unknown composition metadata may retain a zero native session; it is consumed
// as unscoped metadata by the broker and never adopted by the current editor.
//
// Native lifecycle IDs are process-local. Valid framing is NOT journal replay
// authority. EventLoop must prohibit replayed native text from entering a live
// broker until an explicit replay design exists; never relabel saved IDs to the
// current editor/window. This codec performs no sequence or ownership admission.
//
// Both outputs remain unchanged on failure. Input/output storage may alias;
// complete staged values are published only after all validation succeeds.
// Error storage must not alias any input/output storage or a record member.
// On success error is cleared. The caller owns/frees its copied event payload
// and must enforce the separate aggregate event-queue budget before allocation.
bool EncodeTextInputBrokerRecord(const TextNativeRecord& record,
	std::string& outBytes, std::string& error);
bool DecodeTextInputBrokerRecord(std::string_view bytes,
	TextNativeRecord& out, std::string& error);

} // namespace openq4::ui
