// Copyright (C) 2026 openQ4 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_SOUND_SAMPLE_REPEAT_H
#define OPENQ4_SOUND_SAMPLE_REPEAT_H

// Quake 4's own mixer ran at 44.1 kHz and brought its 11.025 and 22.05 kHz
// sounds up to that rate by repeating each sample four times or twice
// (idSIMD_Generic::UpSamplePCMTo44kHz and UpSampleOGGTo44kHz in the SDK). That
// keeps the treble, and adds mirror images of the sound above its own
// bandwidth: brighter and grittier than a filter that reproduces the file.
//
// s_retailUpsampling does the same where a sound's PCM is handed to OpenAL:
// the buffer holds the repeated samples at 44.1 kHz, and the resampler then
// only takes 44.1 kHz to the output rate. Every other rate is left to the
// resampler, as retail accepted no other. Sample positions given to OpenAL
// for such a buffer are multiplied by the same factor; everything the engine
// keeps (lengths, offsets, envelopes, streams) stays at the sound's own rate.
inline int SoundRetailRepeat( const int sampleRate ) {
	return sampleRate == 11025 ? 4 : sampleRate == 22050 ? 2 : 1;
}

// Interleaved 16-bit frames, each written `repeat` times in a row. `out` holds
// frames * channels * repeat samples and may not overlap `in`.
inline void SoundRepeatFrames( const short* in, const int frames, const int channels, const int repeat, short* out ) {
	for ( int frame = 0; frame < frames; ++frame ) {
		for ( int copy = 0; copy < repeat; ++copy ) {
			for ( int channel = 0; channel < channels; ++channel ) {
				*out++ = in[frame * channels + channel];
			}
		}
	}
}
#endif
