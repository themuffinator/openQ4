// Shared SP/MP VR presentation. Quake 4 SDK EULA; see src/game/LICENSE.
#ifndef __GAME_VRWRISTCORE_H__
#define __GAME_VRWRISTCORE_H__

/*
===============================================================================

	What the wrist displays decide (VR-09, VRWristDisplay.h), free of the
	engine so tools/tests/native/VRWristCoreTest.cpp runs it directly.

===============================================================================
*/

#include <cmath>

// how the ammunition reads
enum vrWristTone_t {
	VR_WRIST_TONE_NORMAL,
	VR_WRIST_TONE_WARN,			// running low
	VR_WRIST_TONE_LOW			// empty: the magazine or everything
};

enum vrWristAmmoLabel_t {
	VR_WRIST_LABEL_NONE,
	VR_WRIST_LABEL_RELOAD,		// the magazine is empty, more is carried
	VR_WRIST_LABEL_NO_AMMO		// nothing left at all
};

struct vrWristAmmo_t {
	float	fill;				// the bar, 0..1
	int		tone;				// vrWristTone_t
	int		label;				// vrWristAmmoLabel_t
};

// The weapon hand's reading. A weapon with a magazine fills by its magazine
// and warns at a quarter of it; one without (and every weapon in
// multiplayer, which shows one count) fills by its maximum and warns at an
// eighth; one that needs no ammunition is always full.
inline vrWristAmmo_t VRWrist_Ammo( bool infinite, bool usesClip, int clip, int clipSize, int total, int maxAmmo ) {
	vrWristAmmo_t result = { 1.0f, VR_WRIST_TONE_NORMAL, VR_WRIST_LABEL_NONE };
	if ( infinite ) {
		return result;
	}
	if ( total <= 0 ) {
		result.fill = 0.0f;
		result.tone = VR_WRIST_TONE_LOW;
		result.label = VR_WRIST_LABEL_NO_AMMO;
		return result;
	}
	if ( usesClip ) {
		result.fill = clipSize > 0 ? static_cast<float>( clip ) / static_cast<float>( clipSize ) : 0.0f;
		if ( clip <= 0 ) {
			result.tone = VR_WRIST_TONE_LOW;
			result.label = VR_WRIST_LABEL_RELOAD;
		} else if ( clip * 4 <= clipSize ) {
			result.tone = VR_WRIST_TONE_WARN;
		}
	} else {
		result.fill = maxAmmo > 0 ? static_cast<float>( total ) / static_cast<float>( maxAmmo ) : 1.0f;
		if ( maxAmmo > 0 && total * 8 <= maxAmmo ) {
			result.tone = VR_WRIST_TONE_WARN;
		}
	}
	result.fill = result.fill < 0.0f ? 0.0f : ( result.fill > 1.0f ? 1.0f : result.fill );
	return result;
}

// Health reads low at a quarter of its maximum or under, while alive.
inline bool VRWrist_LowHealth( int health, int maxHealth ) {
	return health > 0 && health * 4 <= ( maxHealth > 1 ? maxHealth : 1 );
}

// A display nearer the eyes than start fades out by end, metres: a gun
// raised to the face never puts a panel over its sights.
const float VR_WRIST_NEAR_FADE_START = 0.30f;
const float VR_WRIST_NEAR_FADE_END = 0.18f;

inline float VRWrist_NearFade( float metres ) {
	const float t = ( metres - VR_WRIST_NEAR_FADE_END ) / ( VR_WRIST_NEAR_FADE_START - VR_WRIST_NEAR_FADE_END );
	return t < 0.0f ? 0.0f : ( t > 1.0f ? 1.0f : t );
}

// Where on the damage ring an attacker lies, radians clockwise from the top
// as seen: ahead and left are the direction toward it along the head's
// forward and left axes. Ahead reads at the top, right at the right, behind
// at the bottom.
inline float VRWrist_HitAngle( float ahead, float left ) {
	return std::atan2( -left, ahead );
}

// The ring's strength age milliseconds after a hit that shows for life
// milliseconds; reduced flashing caps it at 0.40.
inline float VRWrist_HitAlpha( int age, int life, bool reducedFlashing ) {
	if ( age < 0 || life <= 0 || age >= life ) {
		return 0.0f;
	}
	const float fade = 1.0f - static_cast<float>( age ) / static_cast<float>( life );
	float alpha = fade * fade * 0.9f;
	if ( reducedFlashing && alpha > 0.4f ) {
		alpha = 0.4f;
	}
	return alpha;
}

#endif /* !__GAME_VRWRISTCORE_H__ */
