//----------------------------------------------------------------
// HitSounds.h
//
// openQ4 hit sounds by damage (MP-15). hud_hitSound picks the set: off, Quake
// 4's one sound, or that sound at four pitches that fall or rise with how much
// the hit was worth. Every set keeps the armour variant, so a hit on armour
// still sounds like one.
//
// The amount comes from the hit-info message, which the server may withhold
// (g_hitFeedback) and the network may lose, so the snapshot's hit pulse stays
// as the fallback: it waits a moment for the amount and sounds Quake 4's cue
// when none arrives.
//
// The core below owns no engine types, so tools/tests/native can compile it
// alone.
//----------------------------------------------------------------

#ifndef __GAME_MP_HITSOUNDS_H__
#define __GAME_MP_HITSOUNDS_H__

// hud_hitSound
typedef enum {
	HITSOUND_OFF = 0,
	HITSOUND_STOCK,			// Quake 4: one sound, one armour sound
	HITSOUND_FALLING,		// four tones, lower as the damage grows
	HITSOUND_RISING,		// four tones, higher as the damage grows
	HITSOUND_SET_COUNT
} hitSoundSet_t;

static const int MP_HITSOUND_BUCKETS = 4;

// How long the snapshot's pulse waits for the hit-info message that names the
// amount, and how long a tone covers the pulse that describes the same hit.
static const int MP_HITSOUND_PULSE_WAIT_MSEC = 60;
static const int MP_HITSOUND_COVER_MSEC = 250;

// 1 to 25, 26 to 50, 51 to 75, and anything above: the four bands the damage
// numbers already colour. An unknown or empty amount has no bucket.
inline int MPHitSoundBucket( int damage ) {
	if ( damage <= 0 ) {
		return -1;
	}
	if ( damage <= 25 ) {
		return 0;
	}
	if ( damage <= 50 ) {
		return 1;
	}
	if ( damage <= 75 ) {
		return 2;
	}
	return 3;
}

// The pitch step a bucket plays in a set: 0 is the highest tone.
inline int MPHitSoundStep( int set, int bucket ) {
	if ( bucket < 0 || bucket >= MP_HITSOUND_BUCKETS ) {
		return -1;
	}
	if ( set == HITSOUND_FALLING ) {
		return bucket;
	}
	if ( set == HITSOUND_RISING ) {
		return MP_HITSOUND_BUCKETS - 1 - bucket;
	}
	return -1;
}

// The pitch a hit plays Quake 4's hit sound at, 1 being the sound as shipped,
// or 0 when the set has no tone for the hit and Quake 4's own cue applies.
// The engine does not apply a sound shader's own frequency shift, so the tone
// is a pitch on the one play; no audio is added.
inline float MPHitSoundPitch( int set, int damage ) {
	static const float pitches[ MP_HITSOUND_BUCKETS ] = { 1.30f, 1.10f, 0.92f, 0.76f };
	const int step = MPHitSoundStep( set, MPHitSoundBucket( damage ) );
	return ( step < 0 ) ? 0.0f : pitches[ step ];
}

// The two paths one hit arrives by, reconciled. Times are the client's real
// clock in milliseconds.
struct mpHitSoundState_t {
	int		toneTime;		// when a tone last played, 0 for never
	int		pulseTime;		// when a pulse started waiting, 0 for none
	bool	pulseArmor;
};

typedef enum {
	MP_HITSOUND_PLAY_NONE = 0,
	MP_HITSOUND_PLAY_TONE,		// the bucketed tone for the amount
	MP_HITSOUND_PLAY_STOCK		// Quake 4's cue
} mpHitSoundPlay_t;

inline void MPHitSoundClear( mpHitSoundState_t &state ) {
	state.toneTime = 0;
	state.pulseTime = 0;
	state.pulseArmor = false;
}

// A hit whose amount is known. It answers any pulse that was waiting for it.
inline mpHitSoundPlay_t MPHitSoundOnAmount( mpHitSoundState_t &state, int set, int damage, int now ) {
	if ( set < HITSOUND_FALLING || set >= HITSOUND_SET_COUNT || MPHitSoundBucket( damage ) < 0 ) {
		return MP_HITSOUND_PLAY_NONE;
	}
	state.toneTime = ( now != 0 ) ? now : 1;
	state.pulseTime = 0;
	return MP_HITSOUND_PLAY_TONE;
}

// The snapshot's pulse: a hit landed, amount unknown. amountsExpected says the
// server sends amounts (g_hitFeedback 2), which is when waiting is worth it.
inline mpHitSoundPlay_t MPHitSoundOnPulse( mpHitSoundState_t &state, int set, bool amountsExpected, bool armor, int now ) {
	if ( set <= HITSOUND_OFF || set >= HITSOUND_SET_COUNT ) {
		return MP_HITSOUND_PLAY_NONE;
	}
	if ( set == HITSOUND_STOCK || !amountsExpected ) {
		return MP_HITSOUND_PLAY_STOCK;
	}
	if ( state.toneTime != 0 && now - state.toneTime >= 0 && now - state.toneTime < MP_HITSOUND_COVER_MSEC ) {
		// the tone for this hit has already played
		return MP_HITSOUND_PLAY_NONE;
	}
	if ( state.pulseTime == 0 ) {
		state.pulseTime = ( now != 0 ) ? now : 1;
		state.pulseArmor = armor;
	}
	return MP_HITSOUND_PLAY_NONE;
}

// Once a frame: a pulse nothing answered in time sounds Quake 4's cue.
inline mpHitSoundPlay_t MPHitSoundFrame( mpHitSoundState_t &state, int now, bool &armor ) {
	if ( state.pulseTime == 0 || now - state.pulseTime < MP_HITSOUND_PULSE_WAIT_MSEC ) {
		return MP_HITSOUND_PLAY_NONE;
	}
	armor = state.pulseArmor;
	state.pulseTime = 0;
	return MP_HITSOUND_PLAY_STOCK;
}

#ifndef MP_CLIENT_OPTIONS_CORE_ONLY
class idPlayer;

/*
===============================================================================

	rvHitSounds

	The local client's hit sound. One record, like the hit marker it sits
	beside: a second hit re-arms it instead of stacking.

===============================================================================
*/
class rvHitSounds {
public:
	// map load and respawn
	static void				Clear( void );

	// hit-info pipeline: the amount is known (0 when the server withheld it)
	static void				OnHitInfo( idPlayer *player, int damage, int hitFlags );

	// snapshot pulse (idPlayer::TriggerHitSound): armour is all it knows
	static void				OnPulse( idPlayer *player, bool armor );

	// once a client frame
	static void				Frame( void );

	// validation: how many tones and stock cues have played since the last
	// Clear, and the pitch of the last one
	static void				GetCounts( int &tones, int &stock, float &lastPitch );
};
#endif

#endif	/* !__GAME_MP_HITSOUNDS_H__ */
