// Copyright (C) 2026 openQ4 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The arithmetic and the file layout of a demo export's sound track
// (src/sound/SoundCapture.h): every step's share of samples adds up to the
// exported time, and the WAV header is what a player reads.
#include "src/sound/SoundCapture.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

static int checks = 0;
static void Check( bool ok, const char* message ) {
	++checks;
	if ( !ok ) { std::fprintf( stderr, "%s\n", message ); std::exit( 1 ); }
}

static uint32_t U32( const uint8_t* p ) { return p[0] | ( p[1] << 8 ) | ( p[2] << 16 ) | ( uint32_t( p[3] ) << 24 ); }
static uint32_t U16( const uint8_t* p ) { return p[0] | ( p[1] << 8 ); }

int main() {
	// Mixing a step at a time adds up to the track a single mix of the whole
	// time would give, at every rate and frame rate an export can use.
	for ( int rate : { 8000, 11025, 22050, 44100, 48000, 96000 } ) {
		for ( int fps : { 1, 7, 10, 24, 30, 60, 120 } ) {
			const int step = 1000 / fps;
			int64_t msec = 0, frames = 0;
			for ( int frame = 0; frame < 5000; ++frame ) {
				// the sound frame mixes a step in slices
				for ( int left = step; left > 0; ) {
					const int slice = left < SOUND_CAPTURE_SLICE_MSEC ? left : SOUND_CAPTURE_SLICE_MSEC;
					left -= slice;
					msec += slice;
					const int64_t target = SoundCaptureFramesFor( msec, rate );
					Check( target >= frames, "a slice never takes samples back" );
					frames = target;
				}
			}
			Check( msec == 5000LL * step, "the track covers every exported step" );
			Check( frames == 5000LL * step * rate / 1000, "the track's length is the exported time at the mixing rate" );
		}
	}
	// the export's steps: whole milliseconds that add up to the frames' time
	for ( int fps = 1; fps <= 120; ++fps ) {
		int64_t total = 0;
		for ( int64_t frame = 0; frame < 10000; ++frame ) {
			const int step = DemoExportStepMsec( frame, fps );
			Check( step == 1000 / fps || step == 1000 / fps + 1, "a step is 1000/fps rounded down or up" );
			total += step;
			Check( total == ( frame + 1 ) * 1000 / fps, "n steps last n * 1000/fps milliseconds" );
		}
	}
	Check( DemoExportStepMsec( 0, 30 ) == 33 && DemoExportStepMsec( 1, 30 ) == 33 && DemoExportStepMsec( 2, 30 ) == 34,
		   "30 a second steps 33, 33, 34" );
	Check( DemoExportStepMsec( -1, 30 ) == 0 && DemoExportStepMsec( 0, 0 ) == 0, "no step out of range" );
	Check( SoundCaptureFramesFor( 33, 44100 ) == 1455, "a 30 fps step at 44.1 kHz is 1455 sample frames" );
	Check( SoundCaptureFramesFor( 66, 44100 ) == 2910, "two of them are 2910" );
	Check( SoundCaptureFramesFor( 0, 48000 ) == 0 && SoundCaptureFramesFor( 100, 0 ) == 0, "nothing for no time or no rate" );

	// a stereo track: the plain PCM header
	uint8_t header[SOUND_CAPTURE_WAV_HEADER_MAX];
	Check( SoundCaptureWriteWavHeader( header, 2, 48000, 4800 ) == SOUND_CAPTURE_WAV_HEADER_PCM, "stereo writes a 44-byte header" );
	Check( std::memcmp( header, "RIFF", 4 ) == 0 && std::memcmp( header + 8, "WAVEfmt ", 8 ) == 0, "RIFF/WAVE/fmt" );
	Check( U32( header + 4 ) == 36 + 4800 * 4, "the RIFF size covers the header and the data" );
	Check( U32( header + 16 ) == 16 && U16( header + 20 ) == 1, "16-byte WAVE_FORMAT_PCM" );
	Check( U16( header + 22 ) == 2 && U32( header + 24 ) == 48000, "two channels at 48 kHz" );
	Check( U32( header + 28 ) == 48000 * 4 && U16( header + 32 ) == 4 && U16( header + 34 ) == 16, "16-bit frames of four bytes" );
	Check( std::memcmp( header + 36, "data", 4 ) == 0 && U32( header + 40 ) == 4800 * 4, "the data chunk holds every frame" );
	Check( SoundCaptureWriteWavHeader( header, 1, 22050, 0 ) == SOUND_CAPTURE_WAV_HEADER_PCM && U32( header + 40 ) == 0 &&
		   U16( header + 32 ) == 2, "an empty mono track is still a valid file" );

	// surround: WAVE_FORMAT_EXTENSIBLE with the channel order OpenAL Soft renders
	for ( int channels : { 4, 6, 7, 8 } ) {
		Check( SoundCaptureWriteWavHeader( header, channels, 48000, 1000 ) == SOUND_CAPTURE_WAV_HEADER_EXTENSIBLE,
			   "more than two channels write the extensible header" );
		Check( U32( header + 16 ) == 40 && U16( header + 20 ) == 0xFFFE, "40-byte WAVE_FORMAT_EXTENSIBLE" );
		Check( U16( header + 22 ) == static_cast<uint32_t>( channels ) && U16( header + 32 ) == static_cast<uint32_t>( channels * 2 ),
			   "the block holds a sample of every channel" );
		Check( U16( header + 36 ) == 22 && U16( header + 38 ) == 16, "the extension: 22 bytes, 16 valid bits" );
		Check( U32( header + 40 ) == SoundCaptureChannelMask( channels ), "the speaker mask of the layout" );
		Check( header[44] == 0x01 && header[45] == 0 && header[50] == 0x10 && header[59] == 0x71, "the PCM subformat" );
		Check( std::memcmp( header + 60, "data", 4 ) == 0 && U32( header + 64 ) == 1000u * channels * 2, "then the data" );
		Check( U32( header + 4 ) == 60 + 1000u * channels * 2, "and the RIFF size" );
	}
	Check( SoundCaptureChannelMask( 6 ) == 0x60F, "5.1 is OpenAL Soft's side-channel layout" );
	Check( SoundCaptureChannelMask( 8 ) == 0x63F, "7.1 has back and side pairs" );
	for ( int channels : { 0, 3, 5, 9 } ) {
		Check( SoundCaptureChannelMask( channels ) == 0 && SoundCaptureWriteWavHeader( header, channels, 48000, 1 ) == 0,
			   "a layout OpenAL cannot render is refused" );
	}
	Check( SoundCaptureWriteWavHeader( header, 2, 48000, -1 ) == 0 && SoundCaptureWriteWavHeader( nullptr, 2, 48000, 1 ) == 0,
		   "no header for a negative length or no buffer" );
	// a RIFF size is 32 bits: a track too long for one is refused, not wrapped
	Check( SoundCaptureWriteWavHeader( header, 2, 48000, 0x40000000LL ) == 0, "a track past 4 GiB is refused" );
	Check( SoundCaptureWriteWavHeader( header, 2, 48000, ( 0xFFFFFFFFLL - 36 ) / 4 ) == SOUND_CAPTURE_WAV_HEADER_PCM,
		   "the longest track a RIFF size can hold is written" );

	// levels: the loudest sample, including the most negative one
	soundCaptureLevel_t level;
	const int16_t quiet[4] = { 0, 0, 0, 0 };
	SoundCaptureMeasure( level, quiet, 4 );
	Check( level.peak == 0 && level.sumSquares == 0.0 && level.samples == 4, "silence measures as silence" );
	const int16_t loud[4] = { 100, -32768, 200, 0 };
	SoundCaptureMeasure( level, loud, 4 );
	Check( level.peak == 32768 && level.samples == 8, "the peak of -32768 is 32768" );
	Check( level.sumSquares == 100.0 * 100 + 32768.0 * 32768 + 200.0 * 200, "the sum of squares" );

	std::printf( "SoundCaptureTest: %d checks passed\n", checks );
	return 0;
}
