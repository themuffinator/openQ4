// Copyright (C) 2026 openQ4 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_SOUND_MEMORY_CLASS_H
#define OPENQ4_SOUND_MEMORY_CLASS_H

// Audio memory budgets by device class (AUD-03). Each budget is enforced on its
// own: the managed-payload budget (s_audioMemoryMB) bounds CPU and OpenAL sample
// data, envelopes, decoder arenas and the fixed voice queues together whether or
// not anything streams, so a phone's bound never depends on streaming. The
// decode budget (s_decodeMemoryMB) bounds compressed input and pending PCM while
// level loads decode, and the stream budget (s_streamMemoryMB) bounds active
// streams. The provider allowance is not enforced: it is what the OpenAL mixer,
// effects, device buffers and sample metadata may add beyond the payloads, which
// the scene audit's silent baseline measures against it.
//
// The figures come from measurements (docs/dev/audio-voice-evidence.md): stock and
// Awakening openings keep up to 242 MiB of payloads and peak at 255.7 MiB across a
// level change, and a Galaxy S25 held 253 MiB at mcc_landing, so a phone with room
// keeps the desktop's 256 MiB. Decoding at a 16 MiB budget refuses no track while
// long tracks stream; phones keep 48 MiB, above the largest stock or Awakening
// input and PCM pair, so that bound still admits every track with streaming off.
// Openings hold at most seven streams (17.1 MiB of reservations). A phone with
// 4 GiB of memory or less, the class whose detected preset is Low Power, gives up
// the largest levels' least-used sounds instead: their samples are refused and
// retried when played, and nothing playing is ever taken.
enum class SoundMemoryClass { Desktop, Mobile, ConstrainedMobile };

struct SoundMemoryBudgets {
	int payloadMB;		// s_audioMemoryMB
	int decodeMB;		// s_decodeMemoryMB
	int streamMB;		// s_streamMemoryMB
	int providerMB;		// allowance beyond the payloads, measured, not enforced
};

// System memory at or below which a phone is constrained, as PerformancePreset's
// Low Power tier draws the line. Unknown memory on a phone counts as constrained.
constexpr int SOUND_MEMORY_CONSTRAINED_RAM_MB = 4096;

constexpr SoundMemoryClass SoundMemoryClassify( bool mobile, int systemRamMB )
{
	return !mobile ? SoundMemoryClass::Desktop
		: systemRamMB <= 0 || systemRamMB <= SOUND_MEMORY_CONSTRAINED_RAM_MB ? SoundMemoryClass::ConstrainedMobile
		: SoundMemoryClass::Mobile;
}

constexpr SoundMemoryBudgets SoundMemoryClassBudgets( SoundMemoryClass memoryClass )
{
	return memoryClass == SoundMemoryClass::ConstrainedMobile ? SoundMemoryBudgets{ 192, 48, 24, 48 }
		: memoryClass == SoundMemoryClass::Mobile ? SoundMemoryBudgets{ 256, 48, 32, 48 }
		: SoundMemoryBudgets{ 256, 64, 32, 64 };
}

constexpr const char* SoundMemoryClassName( SoundMemoryClass memoryClass )
{
	return memoryClass == SoundMemoryClass::ConstrainedMobile ? "constrained-mobile"
		: memoryClass == SoundMemoryClass::Mobile ? "mobile" : "desktop";
}

// s_audioMemoryClass: "auto" (or anything unrecognised) follows the device.
constexpr bool SoundMemoryClassFromName( const char* name, SoundMemoryClass& memoryClass )
{
	auto same = []( const char* a, const char* b ) {
		while( *a && *a == *b ) { ++a; ++b; }
		return *a == *b;
	};
	if( name == nullptr ) return false;
	if( same( name, "desktop" ) ) { memoryClass = SoundMemoryClass::Desktop; return true; }
	if( same( name, "mobile" ) ) { memoryClass = SoundMemoryClass::Mobile; return true; }
	if( same( name, "constrained-mobile" ) ) { memoryClass = SoundMemoryClass::ConstrainedMobile; return true; }
	return false;
}

// A budget cvar of 0 follows the class; any other value is the player's, clamped.
constexpr int SoundMemoryBudgetMB( int configured, int automatic, int minimum, int maximum )
{
	return configured <= 0 ? automatic : configured < minimum ? minimum : configured > maximum ? maximum : configured;
}

#endif
