// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cmath>

namespace openq4 {

// Gyro aiming on its own terms (PAD-04): calibration, small-motion filtering,
// the turning space and activation. The platform layer feeds each sensor
// sample through these and hands the usercmd generator degrees of view, so no
// mouse setting touches gyro aim. Nothing here reads a cvar, a clock or a
// device.
//
// Axes are SDL's for a controller held in front of you: x to the right, y up
// out of the controller's face, z towards you. Rates are radians per second,
// positive counter-clockwise seen from the positive end of the axis: +x tips
// the far edge up, +y turns left, +z rolls the left side down. An
// accelerometer at rest reads +y, away from the ground.

struct GyroVector {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

inline bool GyroFinite(const GyroVector &v) {
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline float GyroLength(const GyroVector &v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// ---------------------------------------------------------------------------
// Calibration. A gyro at rest reports a small constant rate, its bias, which
// would drift the view. Two ways measure it: a hold-still calibration the
// player starts, and a slow correction whenever the controller has plainly
// been at rest for a moment (set down, or held very still).

struct GyroCalibrationSettings {
	float manualSeconds = 2.0f;		// how long the hold-still calibration averages
	float manualSteady = 0.06f;		// rad/s: more spread than this and the controller moved
	float restWindow = 1.0f;		// seconds a rest window lasts
	float restSteadyGyro = 0.012f;	// rad/s spread within a window that still counts as rest
	float restSteadyAccel = 0.35f;	// m/s^2 spread within a window that still counts as rest
	float restMaxBias = 0.12f;		// rad/s: a steady rate above this is a turn, not a bias
	float restCorrection = 0.5f;	// share of the difference each rest window corrects
};

enum class GyroCalibrationEvent {
	None,
	ManualDone,		// the hold-still calibration finished and set the offset
	ManualMoved,	// the controller moved; the hold-still calibration started again
	RestCorrected	// a rest window nudged the offset
};

struct GyroSpread {
	GyroVector low, high, sum;
	float seconds = 0.0f;
	int samples = 0;

	void Clear() { *this = GyroSpread(); }

	void Add(const GyroVector &v, float dt) {
		if (samples == 0) {
			low = high = v;
		} else {
			low.x = v.x < low.x ? v.x : low.x;
			low.y = v.y < low.y ? v.y : low.y;
			low.z = v.z < low.z ? v.z : low.z;
			high.x = v.x > high.x ? v.x : high.x;
			high.y = v.y > high.y ? v.y : high.y;
			high.z = v.z > high.z ? v.z : high.z;
		}
		sum.x += v.x;
		sum.y += v.y;
		sum.z += v.z;
		seconds += dt;
		++samples;
	}

	float Range() const {
		const float x = high.x - low.x, y = high.y - low.y, z = high.z - low.z;
		return x > y ? (x > z ? x : z) : (y > z ? y : z);
	}

	GyroVector Mean() const {
		GyroVector mean;
		if (samples > 0) {
			mean.x = sum.x / samples;
			mean.y = sum.y / samples;
			mean.z = sum.z / samples;
		}
		return mean;
	}
};

struct GyroCalibration {
	GyroVector offset;			// subtracted from every sample
	bool measured = false;		// an offset has been measured, by either way
	bool manual = false;		// a hold-still calibration is running
	GyroSpread manualSpread;
	GyroSpread restGyro;
	GyroSpread restAccel;
};

inline void BeginGyroCalibration(GyroCalibration &calibration) {
	calibration.manual = true;
	calibration.manualSpread.Clear();
}

// Every raw sample passes through here. accel may be null when the controller
// has no accelerometer; rest is then judged from the gyro alone.
inline GyroCalibrationEvent UpdateGyroCalibration(GyroCalibration &calibration, const GyroVector &raw,
	const GyroVector *accel, float dt, const GyroCalibrationSettings &settings) {
	if (!GyroFinite(raw) || !(dt > 0.0f) || !std::isfinite(dt)) {
		return GyroCalibrationEvent::None;
	}
	if (calibration.manual) {
		calibration.manualSpread.Add(raw, dt);
		if (calibration.manualSpread.Range() > settings.manualSteady) {
			calibration.manualSpread.Clear();
			return GyroCalibrationEvent::ManualMoved;
		}
		if (calibration.manualSpread.seconds >= settings.manualSeconds) {
			calibration.offset = calibration.manualSpread.Mean();
			calibration.measured = true;
			calibration.manual = false;
			calibration.restGyro.Clear();
			calibration.restAccel.Clear();
			return GyroCalibrationEvent::ManualDone;
		}
		return GyroCalibrationEvent::None;
	}

	calibration.restGyro.Add(raw, dt);
	if (accel != nullptr && GyroFinite(*accel)) {
		calibration.restAccel.Add(*accel, dt);
	}
	if (calibration.restGyro.seconds < settings.restWindow) {
		return GyroCalibrationEvent::None;
	}
	const GyroVector mean = calibration.restGyro.Mean();
	const bool steady = calibration.restGyro.Range() <= settings.restSteadyGyro &&
		(calibration.restAccel.samples == 0 || calibration.restAccel.Range() <= settings.restSteadyAccel) &&
		GyroLength(mean) <= settings.restMaxBias;
	calibration.restGyro.Clear();
	calibration.restAccel.Clear();
	if (!steady) {
		return GyroCalibrationEvent::None;
	}
	const float share = settings.restCorrection < 0.0f ? 0.0f : (settings.restCorrection > 1.0f ? 1.0f : settings.restCorrection);
	// the first measurement is taken whole; later ones correct slowly
	const float weight = calibration.measured ? share : 1.0f;
	calibration.offset.x += (mean.x - calibration.offset.x) * weight;
	calibration.offset.y += (mean.y - calibration.offset.y) * weight;
	calibration.offset.z += (mean.z - calibration.offset.z) * weight;
	calibration.measured = true;
	return GyroCalibrationEvent::RestCorrected;
}

inline GyroVector ApplyGyroCalibration(const GyroCalibration &calibration, const GyroVector &raw) {
	GyroVector v;
	v.x = raw.x - calibration.offset.x;
	v.y = raw.y - calibration.offset.y;
	v.z = raw.z - calibration.offset.z;
	return v;
}

// ---------------------------------------------------------------------------
// Small-motion filtering. Smoothing averages the slowest motion, where sensor
// noise shows, and leaves faster motion direct, so aim gains no lag.
// Tightening then scales motion below a threshold down towards nothing, a soft
// dead zone that still lets a deliberate slow turn through.

struct GyroFilterSettings {
	float tightening = 0.015f;		// rad/s: below this, motion is scaled down in proportion
	float smoothing = 0.0f;			// rad/s: below this, motion is smoothed; 0 = off
	float smoothingSeconds = 0.08f;	// the smoothing's time constant
};

struct GyroFilterState {
	GyroVector smoothed;
};

inline GyroVector FilterGyro(GyroFilterState &state, const GyroVector &rate, float dt, const GyroFilterSettings &settings) {
	GyroVector out = rate;
	if (!GyroFinite(rate)) {
		return GyroVector();
	}
	if (settings.smoothing > 0.0f && dt > 0.0f) {
		const float length = GyroLength(rate);
		const float half = settings.smoothing * 0.5f;
		float direct = (length - half) / half;
		direct = direct < 0.0f ? 0.0f : (direct > 1.0f ? 1.0f : direct);
		const float blend = settings.smoothingSeconds > 0.0f ? 1.0f - std::exp(-dt / settings.smoothingSeconds) : 1.0f;
		state.smoothed.x += (rate.x * (1.0f - direct) - state.smoothed.x) * blend;
		state.smoothed.y += (rate.y * (1.0f - direct) - state.smoothed.y) * blend;
		state.smoothed.z += (rate.z * (1.0f - direct) - state.smoothed.z) * blend;
		out.x = rate.x * direct + state.smoothed.x;
		out.y = rate.y * direct + state.smoothed.y;
		out.z = rate.z * direct + state.smoothed.z;
	} else {
		state.smoothed = GyroVector();
	}
	if (settings.tightening > 0.0f) {
		const float length = GyroLength(out);
		if (length < settings.tightening) {
			const float scale = length / settings.tightening;
			out.x *= scale;
			out.y *= scale;
			out.z *= scale;
		}
	}
	return out;
}

// ---------------------------------------------------------------------------
// Gravity, for Player Space: which way is down in the controller's frame. The
// gyro carries the estimate through a turn and the accelerometer corrects it
// slowly, so a shaken controller does not lose its sense of down.

struct GyroGravity {
	GyroVector down;		// unit vector towards the ground, controller frame
	bool valid = false;
};

inline void UpdateGyroGravity(GyroGravity &gravity, const GyroVector &rate, const GyroVector &accel, float dt) {
	if (!GyroFinite(rate) || !GyroFinite(accel) || !(dt > 0.0f) || !std::isfinite(dt)) {
		return;
	}
	const float accelLength = GyroLength(accel);
	if (!gravity.valid) {
		if (accelLength > 1.0f) {
			// at rest the accelerometer points away from the ground
			gravity.down.x = -accel.x / accelLength;
			gravity.down.y = -accel.y / accelLength;
			gravity.down.z = -accel.z / accelLength;
			gravity.valid = true;
		}
		return;
	}
	// a fixed direction seen from a turning frame moves by -(rate x down)
	GyroVector d = gravity.down;
	d.x -= (rate.y * gravity.down.z - rate.z * gravity.down.y) * dt;
	d.y -= (rate.z * gravity.down.x - rate.x * gravity.down.z) * dt;
	d.z -= (rate.x * gravity.down.y - rate.y * gravity.down.x) * dt;
	if (accelLength > 1.0f) {
		// trust the accelerometer most when it reads close to one gravity
		const float error = std::fabs(accelLength - 9.80665f) / 9.80665f;
		const float trust = error >= 0.5f ? 0.0f : (1.0f - error * 2.0f);
		const float blend = (1.0f - std::exp(-dt / 0.5f)) * trust;
		d.x += (-accel.x / accelLength - d.x) * blend;
		d.y += (-accel.y / accelLength - d.y) * blend;
		d.z += (-accel.z / accelLength - d.z) * blend;
	}
	const float length = GyroLength(d);
	if (length > 0.0001f) {
		gravity.down.x = d.x / length;
		gravity.down.y = d.y / length;
		gravity.down.z = d.z / length;
	}
}

// ---------------------------------------------------------------------------
// Turning space: which controller motion turns the view left and right.
//   Yaw     turning the controller about the axis through its face, as when
//           it lies flat on a table and spins.
//   Roll    tilting it like a steering wheel.
//   Player  turning it about the real vertical, however it is held, so a
//           controller tilted in the hands still turns the view level.
// Tipping the far edge up and down always looks up and down.

enum GyroSpace {
	GYRO_SPACE_YAW = 0,
	GYRO_SPACE_ROLL = 1,
	GYRO_SPACE_PLAYER = 2
};

// Degrees per second of controller motion along the view's axes, in the
// engine's view angles: yaw grows to the left, pitch grows downwards.
struct GyroTurn {
	float yaw = 0.0f;
	float pitch = 0.0f;
};

inline GyroTurn ComputeGyroTurn(const GyroVector &rate, const GyroGravity &gravity, int space) {
	const float degrees = 57.29577951308232f;
	GyroTurn turn;
	if (!GyroFinite(rate)) {
		return turn;
	}
	turn.pitch = -rate.x * degrees;
	float yaw = rate.y;
	if (space == GYRO_SPACE_ROLL) {
		yaw = rate.z;
	} else if (space == GYRO_SPACE_PLAYER && gravity.valid) {
		// the rate about the real vertical, from the two axes that can turn
		// the view; relaxed a little so a turn made slightly off the vertical
		// is not shortened, and never more than the motion that was made
		const float world = -(rate.y * gravity.down.y + rate.z * gravity.down.z);
		const float made = std::sqrt(rate.y * rate.y + rate.z * rate.z);
		const float relaxed = std::fabs(world) * 1.41f;
		yaw = std::copysign(relaxed < made ? relaxed : made, world);
	}
	turn.yaw = yaw * degrees;
	return turn;
}

struct GyroLookSettings {
	float yawRatio = 1.0f;		// degrees of view per degree the controller turns
	float pitchRatio = 1.0f;
	bool invertYaw = false;
	bool invertPitch = false;
};

// One sample's share of view rotation, in degrees.
inline GyroTurn ComputeGyroLook(const GyroTurn &turn, float dt, const GyroLookSettings &settings) {
	GyroTurn look;
	if (!(dt > 0.0f) || !std::isfinite(dt) || !std::isfinite(turn.yaw) || !std::isfinite(turn.pitch)) {
		return look;
	}
	const float yawRatio = settings.yawRatio < 0.0f ? 0.0f : (settings.yawRatio > 20.0f ? 20.0f : settings.yawRatio);
	const float pitchRatio = settings.pitchRatio < 0.0f ? 0.0f : (settings.pitchRatio > 20.0f ? 20.0f : settings.pitchRatio);
	look.yaw = turn.yaw * dt * yawRatio * (settings.invertYaw ? -1.0f : 1.0f);
	look.pitch = turn.pitch * dt * pitchRatio * (settings.invertPitch ? -1.0f : 1.0f);
	return look;
}

// ---------------------------------------------------------------------------
// Activation: when gyro motion aims. With the button modes, motion while the
// gyro is off is discarded, so lifting the thumb and bringing the controller
// back (a ratchet) does not turn the view.

enum GyroActivation {
	GYRO_ACTIVE_ALWAYS = 0,
	GYRO_ACTIVE_OFF_WHILE_HELD = 1,		// the gyro button pauses it
	GYRO_ACTIVE_ON_WHILE_HELD = 2,		// only while the gyro button is held
	GYRO_ACTIVE_WHILE_ZOOMED = 3		// only while zoomed
};

inline bool GyroActive(int activation, bool buttonHeld, bool zoomed) {
	switch (activation) {
		case GYRO_ACTIVE_OFF_WHILE_HELD: return !buttonHeld;
		case GYRO_ACTIVE_ON_WHILE_HELD: return buttonHeld;
		case GYRO_ACTIVE_WHILE_ZOOMED: return zoomed;
		default: return true;
	}
}

// What the mouse path made of gyro motion through 0.15.0, as a ratio: 2600
// counts a radian times the gyro sensitivity, turned by the mouse's own
// sensitivity and yaw scale. A profile that already aimed with gyro keeps its
// feel by starting from this.
inline float LegacyGyroRatio(float gyroSensitivity, float mouseSensitivity, float mouseScale) {
	const float ratio = 2600.0f * gyroSensitivity * mouseSensitivity * std::fabs(mouseScale) / 57.29577951308232f;
	if (!std::isfinite(ratio) || ratio <= 0.0f) {
		return 1.0f;
	}
	// the shipped Steam Deck values come to 0.998; call that the 1:1 it meant
	if (std::fabs(ratio - 1.0f) < 0.02f) {
		return 1.0f;
	}
	return ratio > 20.0f ? 20.0f : ratio;
}

} // namespace openq4
