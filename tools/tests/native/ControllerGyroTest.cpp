// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Gyro aiming (PAD-04): the production gyro math without SDL or the engine.
// Directions in each turning space, Player Space however the controller is
// held, a hold-still calibration and the rest correction removing an injected
// bias, calibration refusing a moving controller, the small-motion filters,
// the activation modes and the ratio an existing profile starts from.
#include "src/framework/ControllerGyro.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); std::exit(1); } } while (0)

using openq4::GyroVector;

static bool Near(double a, double b, double epsilon = 0.001) { return std::fabs(a - b) < epsilon; }
static GyroVector V(float x, float y, float z) { GyroVector v; v.x = x; v.y = y; v.z = z; return v; }
static const float RAD = 0.017453292519943295f;

static openq4::GyroGravity Down(float x, float y, float z) {
	openq4::GyroGravity gravity;
	gravity.down = V(x, y, z);
	gravity.valid = true;
	return gravity;
}

static void Directions() {
	const openq4::GyroGravity none;
	// turning the controller left turns the view left (yaw grows)
	openq4::GyroTurn turn = openq4::ComputeGyroTurn(V(0, 90 * RAD, 0), none, openq4::GYRO_SPACE_YAW);
	CHECK(Near(turn.yaw, 90.0) && Near(turn.pitch, 0.0));
	// tipping the far edge up looks up (pitch falls)
	turn = openq4::ComputeGyroTurn(V(45 * RAD, 0, 0), none, openq4::GYRO_SPACE_YAW);
	CHECK(Near(turn.pitch, -45.0) && Near(turn.yaw, 0.0));
	// in Yaw, rolling does nothing; in Roll, steering left turns left and spinning does nothing
	turn = openq4::ComputeGyroTurn(V(0, 0, 60 * RAD), none, openq4::GYRO_SPACE_YAW);
	CHECK(Near(turn.yaw, 0.0));
	turn = openq4::ComputeGyroTurn(V(0, 30 * RAD, 60 * RAD), none, openq4::GYRO_SPACE_ROLL);
	CHECK(Near(turn.yaw, 60.0));
	// Player Space without an accelerometer falls back to Yaw
	turn = openq4::ComputeGyroTurn(V(0, 30 * RAD, 60 * RAD), none, openq4::GYRO_SPACE_PLAYER);
	CHECK(Near(turn.yaw, 30.0));
	// bad input turns nothing
	turn = openq4::ComputeGyroTurn(V(std::nanf(""), 1, 1), none, openq4::GYRO_SPACE_YAW);
	CHECK(turn.yaw == 0.0f && turn.pitch == 0.0f);
}

static void PlayerSpace() {
	// Lying flat, face up: down is -y. A turn about the real vertical is the
	// controller's own yaw, and steering it (roll) does not turn the view.
	const openq4::GyroGravity flat = Down(0, -1, 0);
	openq4::GyroTurn turn = openq4::ComputeGyroTurn(V(0, 90 * RAD, 0), flat, openq4::GYRO_SPACE_PLAYER);
	CHECK(Near(turn.yaw, 90.0));
	turn = openq4::ComputeGyroTurn(V(0, -90 * RAD, 0), flat, openq4::GYRO_SPACE_PLAYER);
	CHECK(Near(turn.yaw, -90.0));
	turn = openq4::ComputeGyroTurn(V(0, 0, 90 * RAD), flat, openq4::GYRO_SPACE_PLAYER);
	CHECK(Near(turn.yaw, 0.0));

	// Held upright, face towards the player: the far edge points up, so down
	// is +z. Turning left about the real vertical is now -z in the
	// controller's frame, and still turns the view left.
	const openq4::GyroGravity upright = Down(0, 0, 1);
	turn = openq4::ComputeGyroTurn(V(0, 0, -90 * RAD), upright, openq4::GYRO_SPACE_PLAYER);
	CHECK(Near(turn.yaw, 90.0));
	turn = openq4::ComputeGyroTurn(V(0, 90 * RAD, 0), upright, openq4::GYRO_SPACE_PLAYER);
	CHECK(Near(turn.yaw, 0.0));

	// Tilted 45 degrees towards the player, as a handheld is: a real turn of
	// 90 degrees per second about the vertical splits across y and z, and the
	// view still turns 90.
	const float s = std::sqrt(0.5f);
	const openq4::GyroGravity tilted = Down(0, -s, s);
	turn = openq4::ComputeGyroTurn(V(0, 90 * RAD * s, -90 * RAD * s), tilted, openq4::GYRO_SPACE_PLAYER);
	CHECK(Near(turn.yaw, 90.0, 0.01));
	// local Yaw alone would have lost part of that turn
	turn = openq4::ComputeGyroTurn(V(0, 90 * RAD * s, -90 * RAD * s), tilted, openq4::GYRO_SPACE_YAW);
	CHECK(turn.yaw < 70.0f);
	// a turn made only on the controller's own yaw axis is relaxed, never
	// more than the motion made
	turn = openq4::ComputeGyroTurn(V(0, 90 * RAD, 0), tilted, openq4::GYRO_SPACE_PLAYER);
	CHECK(turn.yaw > 85.0f && turn.yaw <= 90.001f);
	// pitch is the controller's own in every space
	turn = openq4::ComputeGyroTurn(V(30 * RAD, 0, 0), tilted, openq4::GYRO_SPACE_PLAYER);
	CHECK(Near(turn.pitch, -30.0) && Near(turn.yaw, 0.0));
}

static void Gravity() {
	openq4::GyroGravity gravity;
	// no estimate until the accelerometer speaks
	openq4::UpdateGyroGravity(gravity, V(0, 0, 0), V(0, 0, 0), 0.004f);
	CHECK(!gravity.valid);
	// at rest, face up: the accelerometer reads +y, down is -y
	openq4::UpdateGyroGravity(gravity, V(0, 0, 0), V(0, 9.80665f, 0), 0.004f);
	CHECK(gravity.valid && Near(gravity.down.y, -1.0) && Near(gravity.down.x, 0.0) && Near(gravity.down.z, 0.0));

	// Tip the far edge up by 90 degrees over a second with the accelerometer
	// agreeing all the way: the face ends towards the player, down along +z.
	const float dt = 0.004f;
	const float rate = 90 * RAD;
	for (int i = 0; i < 250; ++i) {
		const float angle = rate * dt * (i + 1);
		openq4::UpdateGyroGravity(gravity, V(rate, 0, 0), V(0, 9.80665f * std::cos(angle), -9.80665f * std::sin(angle)), dt);
	}
	CHECK(Near(gravity.down.z, 1.0, 0.02) && Near(gravity.down.y, 0.0, 0.02));

	// the gyro alone carries it through a turn: the same tip with an
	// accelerometer that reads nothing usable (free fall)
	openq4::GyroGravity carried;
	openq4::UpdateGyroGravity(carried, V(0, 0, 0), V(0, 9.80665f, 0), dt);
	for (int i = 0; i < 250; ++i) {
		openq4::UpdateGyroGravity(carried, V(rate, 0, 0), V(0, 0, 0), dt);
	}
	CHECK(Near(carried.down.z, 1.0, 0.02) && Near(carried.down.y, 0.0, 0.02));

	// a hard shake (three gravities sideways) does not throw the estimate
	openq4::GyroGravity shaken;
	openq4::UpdateGyroGravity(shaken, V(0, 0, 0), V(0, 9.80665f, 0), dt);
	for (int i = 0; i < 50; ++i) {
		openq4::UpdateGyroGravity(shaken, V(0, 0, 0), V(29.0f, 9.80665f, 0), dt);
	}
	CHECK(Near(shaken.down.y, -1.0, 0.001));
	// and the unit length holds
	CHECK(Near(openq4::GyroLength(gravity.down), 1.0) && Near(openq4::GyroLength(carried.down), 1.0));
}

// a small deterministic noise source
static float Noise(unsigned &seed) {
	seed = seed * 1664525u + 1013904223u;
	return (static_cast<float>((seed >> 8) & 0xffff) / 65535.0f - 0.5f) * 2.0f;
}

static void ManualCalibration() {
	const openq4::GyroCalibrationSettings settings;
	openq4::GyroCalibration calibration;
	const GyroVector bias = V(0.031f, -0.022f, 0.012f);
	const float dt = 0.004f;
	unsigned seed = 7;

	openq4::BeginGyroCalibration(calibration);
	CHECK(calibration.manual);
	openq4::GyroCalibrationEvent last = openq4::GyroCalibrationEvent::None;
	int samples = 0;
	while (calibration.manual && samples < 2000) {
		const GyroVector raw = V(bias.x + Noise(seed) * 0.002f, bias.y + Noise(seed) * 0.002f, bias.z + Noise(seed) * 0.002f);
		last = openq4::UpdateGyroCalibration(calibration, raw, nullptr, dt, settings);
		++samples;
	}
	// two seconds of holding still
	CHECK(last == openq4::GyroCalibrationEvent::ManualDone && samples >= 499 && samples <= 502);
	CHECK(calibration.measured);
	CHECK(Near(calibration.offset.x, bias.x, 0.0005) && Near(calibration.offset.y, bias.y, 0.0005) && Near(calibration.offset.z, bias.z, 0.0005));
	// the injected bias is gone from what reaches the view
	const GyroVector still = openq4::ApplyGyroCalibration(calibration, bias);
	CHECK(openq4::GyroLength(still) < 0.0006f);
	// and a real turn comes through whole
	const GyroVector turning = openq4::ApplyGyroCalibration(calibration, V(bias.x, bias.y + 1.0f, bias.z));
	CHECK(Near(turning.y, 1.0, 0.0006));

	// moving the controller restarts the hold-still calibration instead of
	// taking the motion as bias
	openq4::GyroCalibration moved;
	openq4::BeginGyroCalibration(moved);
	bool sawMoved = false;
	for (int i = 0; i < 400; ++i) {
		const GyroVector raw = i == 200 ? V(0.5f, 0, 0) : bias;
		const openq4::GyroCalibrationEvent event = openq4::UpdateGyroCalibration(moved, raw, nullptr, dt, settings);
		sawMoved = sawMoved || event == openq4::GyroCalibrationEvent::ManualMoved;
		CHECK(event != openq4::GyroCalibrationEvent::ManualDone);
	}
	CHECK(sawMoved && moved.manual && !moved.measured);
	// then held still for the full time it completes
	for (int i = 0; i < 520 && moved.manual; ++i) {
		last = openq4::UpdateGyroCalibration(moved, bias, nullptr, dt, settings);
	}
	CHECK(!moved.manual && moved.measured && Near(moved.offset.x, bias.x, 0.0005));
}

static void RestCorrection() {
	const openq4::GyroCalibrationSettings settings;
	openq4::GyroCalibration calibration;
	const GyroVector bias = V(-0.018f, 0.027f, 0.009f);
	const GyroVector accel = V(0, 9.80665f, 0);
	const float dt = 0.004f;
	unsigned seed = 11;

	// set down on a table: the first rest window takes the bias whole
	int corrected = 0;
	for (int i = 0; i < 260; ++i) {
		const GyroVector raw = V(bias.x + Noise(seed) * 0.002f, bias.y + Noise(seed) * 0.002f, bias.z + Noise(seed) * 0.002f);
		corrected += openq4::UpdateGyroCalibration(calibration, raw, &accel, dt, settings) == openq4::GyroCalibrationEvent::RestCorrected;
	}
	CHECK(corrected == 1 && calibration.measured);
	CHECK(Near(calibration.offset.y, bias.y, 0.0006));

	// the bias drifts as the sensor warms; rest windows follow it slowly
	const GyroVector warmed = V(bias.x + 0.006f, bias.y - 0.004f, bias.z);
	for (int i = 0; i < 250 * 6; ++i) {
		(void)openq4::UpdateGyroCalibration(calibration, warmed, &accel, dt, settings);
	}
	CHECK(Near(calibration.offset.x, warmed.x, 0.0005) && Near(calibration.offset.y, warmed.y, 0.0005));

	// aiming is not rest: a slow steady turn with a hand's tremor, and a fast
	// smooth turn, both leave the offset alone
	const GyroVector before = calibration.offset;
	for (int i = 0; i < 250 * 4; ++i) {
		const GyroVector raw = V(warmed.x + Noise(seed) * 0.03f, warmed.y + 0.05f + Noise(seed) * 0.03f, warmed.z + Noise(seed) * 0.03f);
		CHECK(openq4::UpdateGyroCalibration(calibration, raw, &accel, dt, settings) == openq4::GyroCalibrationEvent::None);
	}
	for (int i = 0; i < 250 * 4; ++i) {
		CHECK(openq4::UpdateGyroCalibration(calibration, V(0, 0.8f, 0), &accel, dt, settings) == openq4::GyroCalibrationEvent::None);
	}
	CHECK(calibration.offset.x == before.x && calibration.offset.y == before.y && calibration.offset.z == before.z);

	// a steady gyro with the accelerometer swinging (carried while walking) is not rest either
	for (int i = 0; i < 250 * 3; ++i) {
		const GyroVector swinging = V(std::sin(i * 0.05f) * 2.0f, 9.80665f, 0);
		CHECK(openq4::UpdateGyroCalibration(calibration, V(0.05f, 0.05f, 0.05f), &swinging, dt, settings) == openq4::GyroCalibrationEvent::None);
	}
	// bad samples are ignored
	CHECK(openq4::UpdateGyroCalibration(calibration, V(std::nanf(""), 0, 0), &accel, dt, settings) == openq4::GyroCalibrationEvent::None);
	CHECK(openq4::UpdateGyroCalibration(calibration, warmed, &accel, 0.0f, settings) == openq4::GyroCalibrationEvent::None);
}

static void Filters() {
	openq4::GyroFilterSettings settings;
	settings.tightening = 0.02f;
	openq4::GyroFilterState state;
	const float dt = 0.004f;

	// above the threshold nothing changes
	GyroVector out = openq4::FilterGyro(state, V(0, 0.5f, 0), dt, settings);
	CHECK(out.y == 0.5f);
	// below it, motion is scaled down in proportion, continuous at the threshold
	out = openq4::FilterGyro(state, V(0, 0.02f, 0), dt, settings);
	CHECK(Near(out.y, 0.02));
	out = openq4::FilterGyro(state, V(0, 0.01f, 0), dt, settings);
	CHECK(Near(out.y, 0.005));
	out = openq4::FilterGyro(state, V(0, 0.002f, 0), dt, settings);
	CHECK(Near(out.y, 0.0002, 0.00001));
	// it keeps the direction, and a deliberate slow turn still moves
	out = openq4::FilterGyro(state, V(0.006f, -0.008f, 0), dt, settings);
	CHECK(out.x > 0.0f && out.y < 0.0f && Near(out.y / out.x, -8.0 / 6.0));
	// off: untouched
	settings.tightening = 0.0f;
	out = openq4::FilterGyro(state, V(0, 0.001f, 0), dt, settings);
	CHECK(out.y == 0.001f);

	// Smoothing: sensor noise around rest shrinks...
	openq4::GyroFilterSettings smooth;
	smooth.tightening = 0.0f;
	smooth.smoothing = 0.05f;
	openq4::GyroFilterState noisy;
	unsigned seed = 3;
	double rawTravel = 0.0, smoothedTravel = 0.0;
	for (int i = 0; i < 1000; ++i) {
		const float n = Noise(seed) * 0.01f;
		out = openq4::FilterGyro(noisy, V(0, n, 0), dt, smooth);
		rawTravel += std::fabs(n) * dt;
		smoothedTravel += std::fabs(out.y) * dt;
	}
	CHECK(smoothedTravel < rawTravel * 0.25);
	// ...a slow steady turn arrives whole once the average settles...
	openq4::GyroFilterState slow;
	double turned = 0.0;
	for (int i = 0; i < 2000; ++i) {
		turned += openq4::FilterGyro(slow, V(0, 0.01f, 0), dt, smooth).y * dt;
	}
	CHECK(Near(turned, 0.01 * 2000 * dt, 0.002));
	// ...and fast motion is direct: no lag on a flick
	openq4::GyroFilterState fast;
	out = openq4::FilterGyro(fast, V(0, 2.0f, 0), dt, smooth);
	CHECK(out.y == 2.0f);
	out = openq4::FilterGyro(fast, V(std::nanf(""), 0, 0), dt, smooth);
	CHECK(out.x == 0.0f && out.y == 0.0f);
}

static void Look() {
	openq4::GyroLookSettings settings;
	openq4::GyroTurn turn;
	turn.yaw = 90.0f;
	turn.pitch = -30.0f;
	// 1:1: the view turns as far as the controller did
	openq4::GyroTurn look = openq4::ComputeGyroLook(turn, 0.01f, settings);
	CHECK(Near(look.yaw, 0.9) && Near(look.pitch, -0.3));
	// each axis has its own ratio and its own inversion
	settings.yawRatio = 3.0f;
	settings.pitchRatio = 1.5f;
	settings.invertPitch = true;
	look = openq4::ComputeGyroLook(turn, 0.01f, settings);
	CHECK(Near(look.yaw, 2.7) && Near(look.pitch, 0.45));
	settings.invertYaw = true;
	look = openq4::ComputeGyroLook(turn, 0.01f, settings);
	CHECK(Near(look.yaw, -2.7));
	// no time, no turn
	look = openq4::ComputeGyroLook(turn, 0.0f, settings);
	CHECK(look.yaw == 0.0f && look.pitch == 0.0f);

	// A full sample path: a quarter turn to the left in Player Space, held
	// tilted, integrates to 90 degrees of view at 1:1.
	const float s = std::sqrt(0.5f);
	const openq4::GyroGravity tilted = Down(0, -s, s);
	openq4::GyroLookSettings plain;
	double yaw = 0.0;
	for (int i = 0; i < 250; ++i) {
		const openq4::GyroTurn step = openq4::ComputeGyroTurn(V(0, 90 * RAD * s, -90 * RAD * s), tilted, openq4::GYRO_SPACE_PLAYER);
		yaw += openq4::ComputeGyroLook(step, 0.004f, plain).yaw;
	}
	CHECK(Near(yaw, 90.0, 0.05));
}

static void Activation() {
	using namespace openq4;
	CHECK(GyroActive(GYRO_ACTIVE_ALWAYS, false, false) && GyroActive(GYRO_ACTIVE_ALWAYS, true, true));
	// the ratchet: holding the gyro button pauses it
	CHECK(GyroActive(GYRO_ACTIVE_OFF_WHILE_HELD, false, false) && !GyroActive(GYRO_ACTIVE_OFF_WHILE_HELD, true, false));
	CHECK(!GyroActive(GYRO_ACTIVE_ON_WHILE_HELD, false, true) && GyroActive(GYRO_ACTIVE_ON_WHILE_HELD, true, false));
	CHECK(!GyroActive(GYRO_ACTIVE_WHILE_ZOOMED, true, false) && GyroActive(GYRO_ACTIVE_WHILE_ZOOMED, false, true));
	CHECK(GyroActive(99, false, false));
}

static void LegacyRatio() {
	// the shipped Steam Deck values (gyro 0.20 on mouse sensitivity 5, yaw scale 0.022) meant 1:1
	CHECK(openq4::LegacyGyroRatio(0.20f, 5.0f, 0.022f) == 1.0f);
	// the old default came to five times the controller's turn
	CHECK(Near(openq4::LegacyGyroRatio(1.0f, 5.0f, 0.022f), 4.992, 0.01));
	// a profile's own mouse sensitivity was part of its gyro feel
	CHECK(Near(openq4::LegacyGyroRatio(0.5f, 10.0f, 0.022f), 4.992, 0.01));
	// an inverted mouse did not invert it twice here: inversion is its own setting
	CHECK(Near(openq4::LegacyGyroRatio(0.5f, 10.0f, -0.022f), 4.992, 0.01));
	// nonsense starts from 1:1, and the ratio is bounded
	CHECK(openq4::LegacyGyroRatio(0.0f, 5.0f, 0.022f) == 1.0f);
	CHECK(openq4::LegacyGyroRatio(8.0f, 100.0f, 0.022f) == 20.0f);
}

int main() {
	Directions();
	PlayerSpace();
	Gravity();
	ManualCalibration();
	RestCorrection();
	Filters();
	Look();
	Activation();
	LegacyRatio();
	std::puts("controller gyro: ok");
	return 0;
}
