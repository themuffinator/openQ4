// First-party openQ4 sound output for video capture. GPL-3.0-or-later.
#include "snd_local.h"
#include "SoundLoopback.h"
#include "SoundRestart.h"
#include "SoundCapture.h"

#include <vector>

/*
Video capture borrows the OpenAL backend's loopback mixer, the one a demo
export records its WAV track from (SoundCapture.h): RequestLoopback names the
format, an ordinary output restart opens it, RenderLoopback mixes. The two
never share it: a recording does not start while an export holds the mixer,
and renders nothing if one were to begin.
*/

static bool videoLoopbackRequested = false;
static int videoLoopbackRate = 48000;

bool Sound_SetVideoLoopback( bool enable, int rate ) {
	idSoundHardware &hardware = soundSystemLocal.hardware;
	if ( !enable ) {
		if ( videoLoopbackRequested ) {
			videoLoopbackRequested = false;
			hardware.EndLoopback();
			if ( hardware.IsLoopback() ) {
				Sound_RequestOutputRestart();
			}
		}
		return true;
	}
	if ( videoLoopbackRequested ) {
		return rate == videoLoopbackRate;
	}
	if ( Sound_CaptureActive() || rate < 8000 || rate > 192000 || !idSoundHardware_OpenAL::LoopbackAvailable() ) {
		return false;
	}
	int playbackRate = 48000, channels = 2;
	bool hrtf = false;
	hardware.GetOutputFormat( playbackRate, channels, hrtf );
	// the recording is stereo; HRTF follows the player's headphones setting
	hardware.RequestLoopback( rate, 2, hrtf );
	if ( !Sound_RequestOutputRestart() ) {
		hardware.EndLoopback();
		return false;
	}
	videoLoopbackRequested = true;
	videoLoopbackRate = rate;
	return true;
}

soundVideoLoopback_t Sound_VideoLoopbackState( void ) {
	if ( !videoLoopbackRequested ) {
		return SOUND_VIDEO_LOOPBACK_OFF;
	}
	if ( Sound_OutputState() == SOUND_OUTPUT_RESTARTING ) {
		return SOUND_VIDEO_LOOPBACK_PENDING;
	}
	idSoundHardware &hardware = soundSystemLocal.hardware;
	if ( !hardware.IsLoopback() ) {
		// Init went back to the playback device
		return SOUND_VIDEO_LOOPBACK_OFF;
	}
	int rate = 0, channels = 0;
	bool hrtf = false;
	hardware.GetOutputFormat( rate, channels, hrtf );
	return rate == videoLoopbackRate && channels == 2 ? SOUND_VIDEO_LOOPBACK_ACTIVE : SOUND_VIDEO_LOOPBACK_OFF;
}

bool Sound_RenderVideoLoopback( float *interleavedStereo, int frames ) {
	if ( interleavedStereo == NULL || frames <= 0 ) {
		return false;
	}
	idSoundHardware &hardware = soundSystemLocal.hardware;
	static std::vector<short> pcm;
	if ( videoLoopbackRequested && !Sound_CaptureActive() && hardware.IsLoopback() ) {
		pcm.resize( (size_t)frames * 2 );
		if ( hardware.RenderLoopback( pcm.data(), frames ) ) {
			for ( size_t i = 0; i < pcm.size(); ++i ) {
				interleavedStereo[i] = pcm[i] * ( 1.0f / 32768.0f );
			}
			return true;
		}
	}
	memset( interleavedStereo, 0, sizeof( float ) * 2 * frames );
	return false;
}
