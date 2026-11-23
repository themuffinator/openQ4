// Copyright (C) 2026 openQ4 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_SOUND_CAPTURE_H
#define OPENQ4_SOUND_CAPTURE_H

// openQ4 MP-22: the sound track of a demo export.
//
// An export moves the match on by a fixed step a frame, whatever the time the
// frame took, so its sound cannot come from the speakers: the sound system
// switches to an OpenAL loopback device for the export and mixes exactly the
// step's worth of samples each frame, on a sound clock that follows the
// export's steps instead of real time. The track is a PCM WAV at the mixing
// rate of the device it replaced, in that device's speaker layout. When the
// export ends the device it replaced opens again.
//
// The helpers below are the arithmetic and the file layout; they have no
// engine dependencies (tools/tests/native/SoundCaptureTest.cpp).

#include <cstdint>
#include <cstring>

// The longest stretch mixed between two voice updates. A stream keeps about a
// second queued, so a one-frame-a-second export mixes its frame in pieces, the
// streams refilled between them, as a live game would at ten frames a second.
static const int SOUND_CAPTURE_SLICE_MSEC = 100;

// The match time of an export's frame `index` at `fps` frames a second: whole
// milliseconds that add up to index * 1000 / fps (33, 33, 34 at 30 a second),
// so pictures encoded at fps play at the match's speed and n of them last as
// long as n steps of the sound track.
inline int DemoExportStepMsec( int64_t index, int fps )
{
	if( index < 0 || fps <= 0 )
	{
		return 0;
	}
	return static_cast<int>( ( index + 1 ) * 1000 / fps - index * 1000 / fps );
}

// The sample frames a track of msec milliseconds holds. Each mixed step writes
// the difference from the step before, so the track never drifts from the
// exported time by more than one sample frame at 44.1 kHz, however many steps.
inline int64_t SoundCaptureFramesFor( int64_t msec, int rate )
{
	if( msec <= 0 || rate <= 0 )
	{
		return 0;
	}
	return msec * static_cast<int64_t>( rate ) / 1000;
}

// The WAVE_FORMAT_EXTENSIBLE speaker mask for the channel orders OpenAL Soft
// renders (ALC_MONO_SOFT ... ALC_7POINT1_SOFT); 0 for a count it has no
// layout for.
inline uint32_t SoundCaptureChannelMask( int channels )
{
	switch( channels )
	{
		case 1: return 0x4;		// FC
		case 2: return 0x3;		// FL FR
		case 4: return 0x33;	// FL FR BL BR
		case 6: return 0x60F;	// FL FR FC LFE SL SR
		case 7: return 0x70F;	// FL FR FC LFE BC SL SR
		case 8: return 0x63F;	// FL FR FC LFE BL BR SL SR
		default: return 0;
	}
}

// A plain PCM header for mono and stereo, WAVE_FORMAT_EXTENSIBLE for more.
static const int SOUND_CAPTURE_WAV_HEADER_PCM = 44;
static const int SOUND_CAPTURE_WAV_HEADER_EXTENSIBLE = 68;
static const int SOUND_CAPTURE_WAV_HEADER_MAX = SOUND_CAPTURE_WAV_HEADER_EXTENSIBLE;

inline int SoundCaptureWavHeaderSize( int channels )
{
	return channels > 2 ? SOUND_CAPTURE_WAV_HEADER_EXTENSIBLE : SOUND_CAPTURE_WAV_HEADER_PCM;
}

// Writes the header of a 16-bit PCM track of `frames` sample frames into
// `out` (SOUND_CAPTURE_WAV_HEADER_MAX bytes) and returns its size, or 0 when
// the layout or the length cannot be written. Little-endian throughout.
inline int SoundCaptureWriteWavHeader( uint8_t* out, int channels, int rate, int64_t frames )
{
	if( out == nullptr || SoundCaptureChannelMask( channels ) == 0 || rate <= 0 || frames < 0 )
	{
		return 0;
	}
	const int64_t blockAlign = channels * 2;
	const int headerSize = SoundCaptureWavHeaderSize( channels );
	const int64_t dataBytes = frames * blockAlign;
	if( dataBytes + headerSize - 8 > 0xFFFFFFFFLL )
	{
		return 0;
	}
	int at = 0;
	auto bytes = [&]( const char* text ) { std::memcpy( out + at, text, 4 ); at += 4; };
	auto u16 = [&]( uint32_t value ) { out[at++] = value & 0xFF; out[at++] = ( value >> 8 ) & 0xFF; };
	auto u32 = [&]( uint32_t value ) { u16( value & 0xFFFF ); u16( value >> 16 ); };
	bytes( "RIFF" );
	u32( static_cast<uint32_t>( dataBytes + headerSize - 8 ) );
	bytes( "WAVE" );
	bytes( "fmt " );
	if( channels > 2 )
	{
		u32( 40 );
		u16( 0xFFFE );	// WAVE_FORMAT_EXTENSIBLE
	}
	else
	{
		u32( 16 );
		u16( 1 );		// WAVE_FORMAT_PCM
	}
	u16( static_cast<uint32_t>( channels ) );
	u32( static_cast<uint32_t>( rate ) );
	u32( static_cast<uint32_t>( rate * blockAlign ) );
	u16( static_cast<uint32_t>( blockAlign ) );
	u16( 16 );
	if( channels > 2 )
	{
		u16( 22 );		// extension size
		u16( 16 );		// valid bits
		u32( SoundCaptureChannelMask( channels ) );
		// KSDATAFORMAT_SUBTYPE_PCM: 00000001-0000-0010-8000-00aa00389b71
		static const uint8_t pcmGuid[16] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
			0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
		std::memcpy( out + at, pcmGuid, sizeof( pcmGuid ) );
		at += sizeof( pcmGuid );
	}
	bytes( "data" );
	u32( static_cast<uint32_t>( dataBytes ) );
	return at;
}

// The loudest sample and the sum of squares of a mixed stretch, for the
// export's report and its check that the track is not silent.
struct soundCaptureLevel_t
{
	int		peak = 0;
	double	sumSquares = 0.0;
	int64_t	samples = 0;
};

inline void SoundCaptureMeasure( soundCaptureLevel_t& level, const int16_t* pcm, int64_t samples )
{
	for( int64_t i = 0; i < samples; i++ )
	{
		const int value = pcm[i] < 0 ? -static_cast<int>( pcm[i] ) : pcm[i];
		if( value > level.peak )
		{
			level.peak = value;
		}
		level.sumSquares += static_cast<double>( pcm[i] ) * pcm[i];
	}
	level.samples += samples;
}

// What the sound track came to, for the export's report.
struct soundCaptureResult_t
{
	bool		written = false;	// a WAV file was finished
	bool		complete = false;	// every stretch came from the loopback mixer
	int			rate = 0;
	int			channels = 0;
	int64_t		frames = 0;			// sample frames in the file
	int64_t		msec = 0;			// exported time the track covers
	int			peak = 0;			// loudest sample, 0-32768
	double		rms = 0.0;			// over every channel, 0-32768
	char		path[256] = {};
	char		reason[160] = {};	// why there is no track, or why it is not complete
};

// The engine side (snd_system.cpp). Main thread only.

// Switches the sound system to a loopback mixer that writes `path`, a
// relative path under fs_savepath. Returns false, with the reason, and leaves
// the device as it was when that cannot be done: sound is off, the OpenAL
// runtime has no ALC_SOFT_loopback, the file cannot be written, a settings
// change holds the device.
bool	Sound_BeginCapture( const char* path, char* reason, int reasonSize );
// The next sound frame mixes msec more of the track, and the sound clock
// moves on by as much.
void	Sound_CaptureAdvance( int msec );
// Mixes what was asked for and not yet mixed, finishes the file and opens the
// device the capture replaced. Returns false when no capture was running.
bool	Sound_EndCapture( soundCaptureResult_t& result );
bool	Sound_CaptureActive();

#endif
