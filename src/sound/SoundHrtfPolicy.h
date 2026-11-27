// Copyright (C) 2026 openQ4 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_SOUND_HRTF_POLICY_H
#define OPENQ4_SOUND_HRTF_POLICY_H

// Headphone HRTF is a choice on the desktop (s_openALHRTF: 0 auto, 1 off, 2 on;
// off by default, because the runtime's automatic mode trusts an endpoint that
// only claims to be headphones). A VR headset is headphones, and there a sound
// has to come from where the tracked head hears it, so a headset session asks
// for HRTF by itself (vr_hrtf, on by default) whatever the desktop choice is.
//
// Changing the mode resets the output device in place. A session that stops for
// a moment (the headset comes off, the runtime's own menu opens, the session
// restarts) keeps the mode for a while instead of resetting the device twice.
const int SOUND_VR_HRTF_HOLD_MSEC = 5000;

struct soundVrHrtfState_t {
	bool	seen = false;		// a headset session ran, and its hold has not lapsed
	int		lastMsec = 0;		// when it last ran
};

// True while a headset session runs, and for the hold after it stops.
inline bool SoundVrHrtfEngaged( soundVrHrtfState_t &state, const bool headsetSession, const int nowMsec ) {
	if ( headsetSession ) {
		state.seen = true;
		state.lastMsec = nowMsec;
		return true;
	}
	// unsigned, so the millisecond clock may wrap between the two readings
	if ( state.seen && static_cast<unsigned int>( nowMsec ) - static_cast<unsigned int>( state.lastMsec ) < static_cast<unsigned int>( SOUND_VR_HRTF_HOLD_MSEC ) ) {
		return true;
	}
	state.seen = false;
	return false;
}

// The mode to ask the output for: 0 auto, 1 off, 2 on.
inline int SoundRequestedHrtfMode( int chosen, const bool vrHrtf, const bool vrEngaged ) {
	chosen = chosen < 0 ? 0 : chosen > 2 ? 2 : chosen;
	return ( vrHrtf && vrEngaged ) ? 2 : chosen;
}
#endif
