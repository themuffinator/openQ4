// Copyright (C) 2026 openQ4 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_SOUND_RESAMPLER_H
#define OPENQ4_SOUND_RESAMPLER_H

// Most Quake 4 sounds are recorded at 22.05 kHz and every voice is resampled to
// the output rate, so the resampler's passband is the game's treble. OpenAL
// Soft's band-limited sincs end their transition band at the source's Nyquist
// frequency, so a shorter filter starts rolling off lower. Measured through the
// engine at 22.05 kHz into a 48 kHz mix (tools/tests/sound_resampler.py checks
// the same on the OpenAL library):
//
//   resampler          8 kHz    9 kHz   10 kHz   images   48 voices
//   11th order Sinc   -9.4 dB  -18 dB   -34 dB    none    1% of a core
//   23rd order Sinc   -0.2 dB  -3.8 dB  -17 dB    none    1-3%
//   47th order Sinc    0.0 dB  -0.3 dB   -9 dB    none    5-7%
//   Cubic Spline      -2.4 dB  -3.8 dB  -5.4 dB  -15 dB   0.4%
//
// The 11th order sinc cost a random 371 stock sounds a median 2.9 dB of
// everything above 5 kHz (5.5 dB for the brightest tenth) and made them dull.
// The 23rd order keeps all but 0.5 dB. The 47th adds 0.3 dB for several times
// the mixing cost, so it stays an explicit choice. A runtime with neither keeps
// its own default: its interpolators lose less treble than a short sinc does.
inline int SoundResamplerAutoChoice( const char* const* names, const int count ) {
	static const char* const preferred[] = { "23rd order Sinc", "23rd order Sinc (fast)", "47th order Sinc", "47th order Sinc (fast)" };
	for ( const char* wanted : preferred ) {
		for ( int i = 0; i < count; ++i ) {
			// idlib redefines strcmp, and the tests compile this header without idlib
			const char* name = names[i];
			const char* match = wanted;
			while ( name != nullptr && *name != 0 && *name == *match ) {
				++name;
				++match;
			}
			if ( name != nullptr && *name == 0 && *match == 0 ) {
				return i;
			}
		}
	}
	return -1;
}
#endif
