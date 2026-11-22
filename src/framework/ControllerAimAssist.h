// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cmath>

namespace openq4 {

// Aim assistance for controller sticks (PAD-03). Two forms, and only these:
//
//   Slowdown   the stick turns the view more slowly while the crosshair is on
//              or beside a target, so a pass across an enemy is easier to stop.
//   Follow     while a stick is moving, the view takes a share of the target's
//              own motion across the screen, so a strafing enemy does not
//              slide out from under a crosshair that was on it.
//
// Neither moves the crosshair towards a target: follow adds only a fraction
// of how the target's direction is changing and never its offset, so it
// cannot snap, and with both sticks at rest nothing happens at all.
//
// The game module picks the target (a live enemy, in range and in sight) and
// calls these; the engine applies the result to the stick's look rate alone
// (idUsercmdGenLocal::StickLookRate), so the mouse and the gyro never get it,
// and the assisted angles travel in the usercmd like any others: no protocol,
// save or demo format changes.

enum AimAssistStrength {
	AIM_ASSIST_OFF = 0,
	AIM_ASSIST_LOW = 1,
	AIM_ASSIST_STANDARD = 2,
	AIM_ASSIST_HIGH = 3
};

struct AimAssistLevel {
	float slow = 1.0f;		// the stick's speed with the crosshair on the target; 1 = no slowdown
	float margin = 0.0f;	// degrees beside the target over which the slowdown fades in
	float follow = 0.0f;	// share of the target's motion across the view the stick's turn takes
	float range = 0.0f;		// game units; beyond it nothing is assisted
	float guiSlow = 1.0f;	// the stick's speed while the crosshair is the cursor of a world screen
};

inline AimAssistLevel AimAssistForStrength(int strength) {
	AimAssistLevel level;
	switch (strength) {
		case AIM_ASSIST_LOW:
			level.slow = 0.75f; level.margin = 2.0f; level.follow = 0.15f; level.range = 1536.0f; level.guiSlow = 0.75f;
			break;
		case AIM_ASSIST_STANDARD:
			level.slow = 0.60f; level.margin = 3.0f; level.follow = 0.30f; level.range = 2048.0f; level.guiSlow = 0.60f;
			break;
		case AIM_ASSIST_HIGH:
			level.slow = 0.45f; level.margin = 4.5f; level.follow = 0.45f; level.range = 2560.0f; level.guiSlow = 0.45f;
			break;
		default:
			break;
	}
	return level;
}

// The lower of the player's choice and what a server allows.
inline int AimAssistAllowed(int chosen, int serverCap) {
	const int strength = chosen < AIM_ASSIST_OFF ? AIM_ASSIST_OFF : (chosen > AIM_ASSIST_HIGH ? AIM_ASSIST_HIGH : chosen);
	const int cap = serverCap < AIM_ASSIST_OFF ? AIM_ASSIST_OFF : (serverCap > AIM_ASSIST_HIGH ? AIM_ASSIST_HIGH : serverCap);
	return strength < cap ? strength : cap;
}

// Signed shortest difference a - b in degrees, within [-180, 180).
inline float AimAssistAngleDelta(float a, float b) {
	const double d = std::fmod(double(a) - double(b), 360.0);
	return float(d >= 180.0 ? d - 360.0 : (d < -180.0 ? d + 360.0 : d));
}

// How far the crosshair is from a target, and how wide the target looks.
struct AimAssistTarget {
	float offset = 0.0f;	// degrees between the view's direction and the target's centre
	float radius = 0.0f;	// the target's angular radius, in degrees
	float distance = 0.0f;	// game units
};

// Whether the target can be assisted at all: in range, and the crosshair on
// it or within the margin beside it.
inline bool AimAssistInReach(const AimAssistLevel &level, const AimAssistTarget &target) {
	if (!(level.range > 0.0f) || !std::isfinite(target.offset) || !std::isfinite(target.radius) || !std::isfinite(target.distance)) {
		return false;
	}
	return target.distance <= level.range && target.offset <= target.radius + level.margin;
}

// The stick's speed scale for the target: the full slowdown with the
// crosshair on it, easing out to none across the margin beside it.
inline float AimAssistSlowdown(const AimAssistLevel &level, const AimAssistTarget &target) {
	if (!AimAssistInReach(level, target) || !(level.slow < 1.0f)) {
		return 1.0f;
	}
	const float slow = level.slow < 0.1f ? 0.1f : level.slow;
	if (target.offset <= target.radius || !(level.margin > 0.0f)) {
		return slow;
	}
	float t = (target.offset - target.radius) / level.margin;	// 0 at the edge, 1 at the end of the margin
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	t = t * t * (3.0f - 2.0f * t);
	return slow + (1.0f - slow) * t;
}

// The target's motion across the view, tracked from frame to frame.
struct AimAssistTrack {
	int target = -1;		// which target the last sample was of
	float yaw = 0.0f;		// the direction to it, as view angles
	float pitch = 0.0f;
};

// Degrees per second, in the engine's view angles.
struct AimAssistFollow {
	float yaw = 0.0f;
	float pitch = 0.0f;
};

// The follow rate for this frame. dirYaw and dirPitch are the direction from
// the eye to the target as view angles. A new target, a jump in its direction
// (a teleport, a respawn) or a bad interval follows nothing this frame.
inline AimAssistFollow UpdateAimAssistFollow(AimAssistTrack &track, const AimAssistLevel &level, int target,
	float dirYaw, float dirPitch, float seconds) {
	AimAssistFollow follow;
	const bool same = target >= 0 && target == track.target;
	const float yawStep = AimAssistAngleDelta(dirYaw, track.yaw);
	const float pitchStep = AimAssistAngleDelta(dirPitch, track.pitch);
	track.target = target;
	track.yaw = dirYaw;
	track.pitch = dirPitch;
	if (!same || !(level.follow > 0.0f) || !(seconds > 0.0f) || !std::isfinite(seconds) ||
			!std::isfinite(yawStep) || !std::isfinite(pitchStep)) {
		return follow;
	}
	const float maxRate = 120.0f;	// degrees per second: faster than this is not a target moving
	float yawRate = yawStep / seconds;
	float pitchRate = pitchStep / seconds;
	if (std::fabs(yawRate) > maxRate * 3.0f || std::fabs(pitchRate) > maxRate * 3.0f) {
		return follow;
	}
	yawRate = yawRate < -maxRate ? -maxRate : (yawRate > maxRate ? maxRate : yawRate);
	pitchRate = pitchRate < -maxRate ? -maxRate : (pitchRate > maxRate ? maxRate : pitchRate);
	const float share = level.follow > 1.0f ? 1.0f : level.follow;
	follow.yaw = yawRate * share;
	follow.pitch = pitchRate * share;
	return follow;
}

// Whether a stick is moving enough for follow to apply: the look stick at
// all, or the movement stick (strafing around a target counts).
inline bool AimAssistStickMoving(float lookX, float lookY, float moveX, float moveY) {
	return std::fabs(lookX) > 0.05f || std::fabs(lookY) > 0.05f || std::fabs(moveX) > 0.2f || std::fabs(moveY) > 0.2f;
}

} // namespace openq4
