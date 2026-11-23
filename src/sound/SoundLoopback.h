// First-party openQ4 sound output for video capture. GPL-3.0-or-later.
#ifndef OPENQ4_SOUND_LOOPBACK_H
#define OPENQ4_SOUND_LOOPBACK_H

/*
Video capture records exactly what the mixer produces by moving the sound
system onto an OpenAL loopback device (ALC_SOFT_loopback) for the length of
the recording. A loopback device plays nothing on its own: each video frame
renders the samples that belong to it, so picture and sound cannot drift, and
the capture hands the same samples to the speakers so the player still hears
the game.

Moving between the playback device and the loopback device is an ordinary
output restart (Sound_RequestOutputRestart): the samples the level and menus
hold are reloaded once at the start and once at the end of a recording.
*/

enum soundVideoLoopback_t {
	SOUND_VIDEO_LOOPBACK_OFF,		// the playback device, or no device
	SOUND_VIDEO_LOOPBACK_PENDING,	// a restart onto or off the loopback device is waiting for the next sound frame
	SOUND_VIDEO_LOOPBACK_ACTIVE	// the loopback device is open; Sound_RenderVideoLoopback mixes
};

// Asks for (enable) or releases the loopback device at rate Hz, stereo float.
// False, requesting nothing, when enabling is impossible now: sound off, the
// device not open, or the OpenAL runtime without ALC_SOFT_loopback. Main thread only.
bool Sound_SetVideoLoopback( bool enable, int rate );
soundVideoLoopback_t Sound_VideoLoopbackState( void );

// Mixes the next frames sample frames into interleaved stereo floats. False,
// with the buffer zeroed, when the loopback device is not active.
bool Sound_RenderVideoLoopback( float *interleavedStereo, int frames );

#endif
