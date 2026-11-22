// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cmath>

namespace openq4 {

// Controller aim (PAD-02, PAD-04): how a stick's travel and a gyro's rotation
// become view rotation. The platform layer shapes the sticks with ShapeStick,
// and the usercmd generator and the late-look presentation path both turn the
// shaped look stick into degrees per second with ComputeStickLookRate, so the
// drawn view never disagrees with the aim. Nothing here reads a cvar, the
// clock or a device: callers pass what they sampled.

inline float ControllerClamp(float value, float low, float high) {
	if (!std::isfinite(value)) {
		return low;
	}
	return value < low ? low : (value > high ? high : value);
}

struct StickShape {
	float innerDeadZone = 0.18f;	// share of the travel ignored at the centre
	float outerDeadZone = 0.0f;		// share of the travel ignored at the rim
	float curve = 1.0f;				// response exponent; 1 is linear
	bool axial = false;				// dead zones per axis instead of by distance
};

struct StickValue {
	float x = 0.0f;
	float y = 0.0f;
};

// One axis, or the stick's distance from its centre: nothing inside the inner
// dead zone, full output from the outer dead zone to the rim, the curve between.
inline float ShapeStickTravel(float travel, const StickShape &shape) {
	const float inner = ControllerClamp(shape.innerDeadZone, 0.0f, 0.95f);
	float rim = 1.0f - ControllerClamp(shape.outerDeadZone, 0.0f, 0.5f);
	if (rim < inner + 0.02f) {
		rim = inner + 0.02f;
	}
	if (!(travel > inner)) {
		return 0.0f;
	}
	float shaped = ControllerClamp((travel - inner) / (rim - inner), 0.0f, 1.0f);
	const float curve = ControllerClamp(shape.curve, 1.0f, 3.0f);
	if (curve != 1.0f && shaped > 0.0f) {
		shaped = std::pow(shaped, curve);
	}
	return shaped;
}

// A raw stick, each axis in [-1, 1], to a shaped deflection no longer than 1.
// The radial form keeps the stick's direction. The axial form treats each axis
// alone, which holds a straight line against a stick that wanders sideways, and
// suits movement more than aim.
inline StickValue ShapeStick(float rawX, float rawY, const StickShape &shape) {
	StickValue out;
	if (!std::isfinite(rawX) || !std::isfinite(rawY)) {
		return out;
	}
	const float x = ControllerClamp(rawX, -1.0f, 1.0f);
	const float y = ControllerClamp(rawY, -1.0f, 1.0f);
	if (shape.axial) {
		out.x = std::copysign(ShapeStickTravel(std::fabs(x), shape), x);
		out.y = std::copysign(ShapeStickTravel(std::fabs(y), shape), y);
		const float length = std::sqrt(out.x * out.x + out.y * out.y);
		if (length > 1.0f) {
			out.x /= length;
			out.y /= length;
		}
		return out;
	}
	const float length = std::sqrt(x * x + y * y);
	if (!(length > 0.0f)) {
		return out;
	}
	const float shaped = ShapeStickTravel(length > 1.0f ? 1.0f : length, shape);
	out.x = x * shaped / length;
	out.y = y * shaped / length;
	return out;
}

// Turn boost: extra yaw once the stick has been held near full sideways travel
// for a moment, so a half turn does not take a second. It ramps in, and it
// drops at once when the stick comes back or reverses.
struct TurnBoostSettings {
	float threshold = 0.92f;	// shaped sideways deflection that counts as held over
	float delay = 0.12f;		// seconds held before any boost
	float ramp = 0.30f;			// seconds from no boost to all of it
};

struct TurnBoostState {
	float held = 0.0f;			// seconds the stick has been held over, in one direction
	int direction = 0;			// -1 left, 1 right, 0 not held
};

inline float TurnBoostFraction(const TurnBoostState &state, const TurnBoostSettings &settings) {
	const float delay = ControllerClamp(settings.delay, 0.0f, 2.0f);
	const float ramp = ControllerClamp(settings.ramp, 0.0f, 2.0f);
	if (state.direction == 0 || !(state.held >= delay) || !(state.held > 0.0f)) {
		return 0.0f;
	}
	if (!(ramp > 0.0f) || state.held >= delay + ramp) {
		return 1.0f;
	}
	return ControllerClamp((state.held - delay) / ramp, 0.0f, 1.0f);
}

// Advances the boost over one usercmd and returns the share of it now applied.
// allowed is false while zoomed or while a radial selector owns the stick.
inline float AdvanceTurnBoost(TurnBoostState &state, float lookX, float seconds, bool allowed,
	const TurnBoostSettings &settings) {
	const float threshold = ControllerClamp(settings.threshold, 0.1f, 1.0f);
	const int direction = !allowed || !std::isfinite(lookX) ? 0 : (lookX >= threshold ? 1 : (lookX <= -threshold ? -1 : 0));
	if (direction == 0 || direction != state.direction) {
		state.held = 0.0f;
		state.direction = direction;
		return 0.0f;
	}
	if (std::isfinite(seconds) && seconds > 0.0f) {
		const float full = ControllerClamp(settings.delay, 0.0f, 2.0f) + ControllerClamp(settings.ramp, 0.0f, 2.0f);
		state.held = state.held + seconds > full ? full : state.held + seconds;
	}
	return TurnBoostFraction(state, settings);
}

struct StickLookSettings {
	float yawSpeed = 157.5f;	// degrees per second at full sideways travel
	float pitchSpeed = 157.5f;	// degrees per second at full vertical travel
	float scale = 1.0f;			// zoom scaling (pm_zoomedSlow); 1 when not zoomed
	float boostSpeed = 0.0f;	// extra yaw, degrees per second, at full boost
};

// Degrees per second in the engine's view angles: yaw grows to the left and
// pitch grows downwards, while the stick's x grows to the right and its y
// grows downwards (after any inversion).
struct StickLookRate {
	float yaw = 0.0f;
	float pitch = 0.0f;
};

// The whole stick-look transform. Walking and running do not enter it: only
// the keyboard turn keys keep retail's run scaling. boost is the share from
// AdvanceTurnBoost or TurnBoostFraction; zoom scales the base speed and the
// caller passes no boost while zoomed.
inline StickLookRate ComputeStickLookRate(float lookX, float lookY, const StickLookSettings &settings, float boost) {
	StickLookRate rate;
	if (!std::isfinite(lookX) || !std::isfinite(lookY)) {
		return rate;
	}
	const float x = ControllerClamp(lookX, -1.0f, 1.0f);
	const float y = ControllerClamp(lookY, -1.0f, 1.0f);
	const float scale = ControllerClamp(settings.scale, 0.0f, 4.0f);
	const float yawSpeed = ControllerClamp(settings.yawSpeed, 0.0f, 1440.0f);
	const float pitchSpeed = ControllerClamp(settings.pitchSpeed, 0.0f, 1440.0f);
	rate.yaw = -yawSpeed * scale * x;
	rate.pitch = pitchSpeed * scale * y;
	const float share = ControllerClamp(boost, 0.0f, 1.0f);
	if (share > 0.0f && x != 0.0f) {
		rate.yaw -= std::copysign(ControllerClamp(settings.boostSpeed, 0.0f, 1440.0f) * share, x);
	}
	return rate;
}

// Response presets set the curve, the dead zones and the boost together. A
// profile whose values match none of them shows Custom.
enum StickResponsePreset {
	STICK_RESPONSE_CUSTOM = 0,
	STICK_RESPONSE_CLASSIC = 1,		// the values every release so far shipped
	STICK_RESPONSE_PRECISE = 2,		// small dead zone, gentle centre, no boost
	STICK_RESPONSE_DYNAMIC = 3,		// gentle centre and a fast boosted turn
	STICK_RESPONSE_COUNT
};

struct StickResponseValues {
	float lookInnerDeadZone;
	float lookOuterDeadZone;
	float moveInnerDeadZone;
	float moveOuterDeadZone;
	float lookCurve;
	float boostSpeed;
	float boostRamp;
};

inline bool StickResponsePresetValues(int preset, StickResponseValues &values) {
	static const StickResponseValues table[STICK_RESPONSE_COUNT] = {
		{ 0.18f, 0.00f, 0.18f, 0.00f, 1.35f, 0.0f, 0.30f },		// custom: unused
		{ 0.18f, 0.00f, 0.18f, 0.00f, 1.35f, 0.0f, 0.30f },		// classic
		{ 0.10f, 0.04f, 0.14f, 0.04f, 1.80f, 0.0f, 0.30f },		// precise
		{ 0.12f, 0.05f, 0.14f, 0.05f, 2.00f, 200.0f, 0.25f },	// dynamic
	};
	if (preset <= STICK_RESPONSE_CUSTOM || preset >= STICK_RESPONSE_COUNT) {
		return false;
	}
	values = table[preset];
	return true;
}

inline bool StickResponseNear(float a, float b) {
	return std::fabs(a - b) <= 0.0005f;
}

// The preset the values are, or Custom.
inline int StickResponsePresetFor(const StickResponseValues &values) {
	for (int preset = STICK_RESPONSE_CLASSIC; preset < STICK_RESPONSE_COUNT; ++preset) {
		StickResponseValues want;
		StickResponsePresetValues(preset, want);
		if (StickResponseNear(values.lookInnerDeadZone, want.lookInnerDeadZone) &&
			StickResponseNear(values.lookOuterDeadZone, want.lookOuterDeadZone) &&
			StickResponseNear(values.moveInnerDeadZone, want.moveInnerDeadZone) &&
			StickResponseNear(values.moveOuterDeadZone, want.moveOuterDeadZone) &&
			StickResponseNear(values.lookCurve, want.lookCurve) &&
			StickResponseNear(values.boostSpeed, want.boostSpeed) &&
			( want.boostSpeed <= 0.0f || StickResponseNear(values.boostRamp, want.boostRamp) )) {
			return preset;
		}
	}
	return STICK_RESPONSE_CUSTOM;
}

} // namespace openq4
