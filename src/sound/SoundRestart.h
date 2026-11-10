// First-party openQ4 settings view of the sound device. GPL-3.0-or-later.
#ifndef OPENQ4_SOUND_RESTART_H
#define OPENQ4_SOUND_RESTART_H

// What a settings apply can see of the output device. The sound system reads
// s_numberOfSpeakers and s_useEAXReverb when it opens the device, so a change
// to either takes effect through a restart of that device.
enum soundOutputState_t {
	SOUND_OUTPUT_OFF,			// no device by choice (s_noSound): the settings wait for the next start of sound
	SOUND_OUTPUT_RESTARTING,	// a restart is requested and the next sound frame runs it
	SOUND_OUTPUT_READY,			// the device is open
	SOUND_OUTPUT_FAILED			// the device did not open; the sound system retries on its own
};

soundOutputState_t Sound_OutputState( void );

// Asks the next sound frame to close and reopen the output device under the
// settings in effect then, reloading the samples the level and menus hold.
// Returns false, requesting nothing, unless the device is open now. Main thread only.
bool Sound_RequestOutputRestart( void );

#endif
