// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Controller aim (PAD-02): the production stick transform, exercised without
// the engine. It proves the inner and outer dead zones, the axial form, the
// classic values' unchanged response, the turn boost's ramp and its stand-down,
// that walking never enters the look rate, and that the late-look path, which
// reads the boost without advancing it, shows what the usercmd will apply.
#include "src/framework/ControllerLook.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); std::exit(1); } } while (0)

static bool Near(double a, double b, double epsilon = 0.0001) { return std::fabs(a - b) < epsilon; }

// The shaping every release up to 0.15.0 applied in the SDL backend.
static void ClassicShape(float x, float y, float deadZone, float curve, float &outX, float &outY) {
	const float length = std::sqrt(x * x + y * y);
	outX = outY = 0.0f;
	if (length <= deadZone) {
		return;
	}
	float adjusted = ((length > 1.0f ? 1.0f : length) - deadZone) / (1.0f - deadZone);
	adjusted = adjusted < 0.0f ? 0.0f : (adjusted > 1.0f ? 1.0f : adjusted);
	if (curve != 1.0f && adjusted > 0.0f) {
		adjusted = std::pow(adjusted, curve);
	}
	outX = x * adjusted / length;
	outY = y * adjusted / length;
}

static void DeadZones() {
	openq4::StickShape shape;
	shape.innerDeadZone = 0.2f;
	shape.outerDeadZone = 0.1f;

	// nothing inside the inner dead zone, in any direction
	CHECK(openq4::ShapeStick(0.2f, 0.0f, shape).x == 0.0f);
	CHECK(openq4::ShapeStick(0.1f, 0.1f, shape).y == 0.0f);
	CHECK(openq4::ShapeStick(0.0f, 0.0f, shape).x == 0.0f);

	// a worn stick that stops at 90% of its travel still reaches full output
	CHECK(Near(openq4::ShapeStick(0.9f, 0.0f, shape).x, 1.0));
	CHECK(Near(openq4::ShapeStick(-0.95f, 0.0f, shape).x, -1.0));
	CHECK(Near(openq4::ShapeStick(0.0f, 1.0f, shape).y, 1.0));

	// linear between the two
	CHECK(Near(openq4::ShapeStick(0.55f, 0.0f, shape).x, 0.5));

	// the radial form keeps the direction and never exceeds length 1
	const openq4::StickValue diagonal = openq4::ShapeStick(1.0f, 1.0f, shape);
	CHECK(Near(diagonal.x, diagonal.y));
	CHECK(Near(std::sqrt(diagonal.x * diagonal.x + diagonal.y * diagonal.y), 1.0));
	const openq4::StickValue angled = openq4::ShapeStick(0.3f, 0.6f, shape);
	CHECK(Near(angled.y / angled.x, 2.0, 0.001));

	// without an outer dead zone only the rim itself is full output
	shape.outerDeadZone = 0.0f;
	CHECK(openq4::ShapeStick(0.9f, 0.0f, shape).x < 0.9f);
	CHECK(Near(openq4::ShapeStick(1.0f, 0.0f, shape).x, 1.0));

	// the curve bends the middle and leaves both ends alone
	shape.curve = 2.0f;
	CHECK(Near(openq4::ShapeStick(0.6f, 0.0f, shape).x, 0.25));
	CHECK(Near(openq4::ShapeStick(1.0f, 0.0f, shape).x, 1.0));
	CHECK(openq4::ShapeStick(0.2f, 0.0f, shape).x == 0.0f);

	// bad input and bad settings stay in range
	CHECK(openq4::ShapeStick(std::nanf(""), 0.5f, shape).y == 0.0f);
	shape.innerDeadZone = 5.0f;
	shape.outerDeadZone = 5.0f;
	const openq4::StickValue clamped = openq4::ShapeStick(1.0f, 0.0f, shape);
	CHECK(clamped.x >= 0.0f && clamped.x <= 1.0f);
	CHECK(Near(openq4::ShapeStick(3.0f, 0.0f, openq4::StickShape()).x, 1.0));
}

static void Axial() {
	openq4::StickShape shape;
	shape.innerDeadZone = 0.2f;
	shape.axial = true;

	// a stick pushed forward and wandering sideways walks in a straight line
	const openq4::StickValue forward = openq4::ShapeStick(0.15f, 0.9f, shape);
	CHECK(forward.x == 0.0f && forward.y > 0.8f);
	// the radial form would let the sideways part through
	shape.axial = false;
	CHECK(openq4::ShapeStick(0.15f, 0.9f, shape).x > 0.0f);
	// corners are no faster than the rim
	shape.axial = true;
	const openq4::StickValue corner = openq4::ShapeStick(1.0f, -1.0f, shape);
	CHECK(Near(std::sqrt(corner.x * corner.x + corner.y * corner.y), 1.0));
	CHECK(corner.x > 0.0f && corner.y < 0.0f);
}

static void ClassicUnchanged() {
	// The classic preset is the response of every release so far, sample for sample.
	openq4::StickResponseValues classic;
	CHECK(openq4::StickResponsePresetValues(openq4::STICK_RESPONSE_CLASSIC, classic));
	CHECK(classic.lookInnerDeadZone == 0.18f && classic.moveInnerDeadZone == 0.18f);
	CHECK(classic.lookOuterDeadZone == 0.0f && classic.moveOuterDeadZone == 0.0f);
	CHECK(classic.lookCurve == 1.35f && classic.boostSpeed == 0.0f);

	openq4::StickShape look;
	look.innerDeadZone = classic.lookInnerDeadZone;
	look.outerDeadZone = classic.lookOuterDeadZone;
	look.curve = classic.lookCurve;
	for (int ix = -20; ix <= 20; ++ix) {
		for (int iy = -20; iy <= 20; ++iy) {
			const float x = ix / 20.0f, y = iy / 20.0f;
			float wantX, wantY;
			ClassicShape(x, y, 0.18f, 1.35f, wantX, wantY);
			const openq4::StickValue got = openq4::ShapeStick(x, y, look);
			CHECK(Near(got.x, wantX, 0.00001) && Near(got.y, wantY, 0.00001));
		}
	}

	// Classic full-stick speed: in_yawspeed 140 x Look Sensitivity 0.75 x the run scale 1.5.
	openq4::StickLookSettings settings;
	CHECK(Near(settings.yawSpeed, 140.0 * 0.75 * 1.5) && Near(settings.pitchSpeed, 140.0 * 0.75 * 1.5));
	const openq4::StickLookRate rate = openq4::ComputeStickLookRate(1.0f, 0.0f, settings, 0.0f);
	CHECK(Near(rate.yaw, -157.5) && rate.pitch == 0.0f);
}

static void Presets() {
	for (int preset = openq4::STICK_RESPONSE_CLASSIC; preset < openq4::STICK_RESPONSE_COUNT; ++preset) {
		openq4::StickResponseValues values;
		CHECK(openq4::StickResponsePresetValues(preset, values));
		CHECK(openq4::StickResponsePresetFor(values) == preset);
		// any edit shows Custom
		openq4::StickResponseValues edited = values;
		edited.lookCurve += 0.05f;
		CHECK(openq4::StickResponsePresetFor(edited) == openq4::STICK_RESPONSE_CUSTOM);
		edited = values;
		edited.moveOuterDeadZone += 0.01f;
		CHECK(openq4::StickResponsePresetFor(edited) == openq4::STICK_RESPONSE_CUSTOM);
		edited = values;
		edited.boostSpeed += 10.0f;
		CHECK(openq4::StickResponsePresetFor(edited) == openq4::STICK_RESPONSE_CUSTOM);
	}
	openq4::StickResponseValues none;
	CHECK(!openq4::StickResponsePresetValues(openq4::STICK_RESPONSE_CUSTOM, none));
	CHECK(!openq4::StickResponsePresetValues(openq4::STICK_RESPONSE_COUNT, none));
	// the ramp matters only where there is a boost
	openq4::StickResponseValues classic;
	openq4::StickResponsePresetValues(openq4::STICK_RESPONSE_CLASSIC, classic);
	classic.boostRamp = 1.0f;
	CHECK(openq4::StickResponsePresetFor(classic) == openq4::STICK_RESPONSE_CLASSIC);
}

static void LookRate() {
	openq4::StickLookSettings settings;
	settings.yawSpeed = 240.0f;
	settings.pitchSpeed = 120.0f;

	// the two axes have their own speeds; stick right turns right (yaw falls),
	// stick down looks down (pitch grows)
	openq4::StickLookRate rate = openq4::ComputeStickLookRate(1.0f, 1.0f, settings, 0.0f);
	CHECK(Near(rate.yaw, -240.0) && Near(rate.pitch, 120.0));
	rate = openq4::ComputeStickLookRate(-0.5f, -0.25f, settings, 0.0f);
	CHECK(Near(rate.yaw, 120.0) && Near(rate.pitch, -30.0));

	// zoom scales the base speed
	settings.scale = 0.5f;
	rate = openq4::ComputeStickLookRate(1.0f, 1.0f, settings, 0.0f);
	CHECK(Near(rate.yaw, -120.0) && Near(rate.pitch, 60.0));
	settings.scale = 1.0f;

	// the boost adds yaw only, in the stick's direction
	settings.boostSpeed = 180.0f;
	rate = openq4::ComputeStickLookRate(1.0f, 0.5f, settings, 1.0f);
	CHECK(Near(rate.yaw, -420.0) && Near(rate.pitch, 60.0));
	rate = openq4::ComputeStickLookRate(-1.0f, 0.0f, settings, 0.5f);
	CHECK(Near(rate.yaw, 330.0));
	rate = openq4::ComputeStickLookRate(0.0f, 1.0f, settings, 1.0f);
	CHECK(rate.yaw == 0.0f);

	// out-of-range input cannot exceed the configured speed
	rate = openq4::ComputeStickLookRate(4.0f, -4.0f, settings, 7.0f);
	CHECK(Near(rate.yaw, -420.0) && Near(rate.pitch, -120.0));
	rate = openq4::ComputeStickLookRate(std::nanf(""), 0.0f, settings, 0.0f);
	CHECK(rate.yaw == 0.0f && rate.pitch == 0.0f);
}

static void TurnBoost() {
	const openq4::TurnBoostSettings settings; // 0.92 threshold, 0.12 s delay, 0.30 s ramp
	const float tick = 1.0f / 60.0f;
	openq4::TurnBoostState state;

	// nothing while the stick is short of the threshold
	for (int i = 0; i < 120; ++i) {
		CHECK(openq4::AdvanceTurnBoost(state, 0.9f, tick, true, settings) == 0.0f);
	}
	CHECK(state.direction == 0 && state.held == 0.0f);

	// held over: nothing through the delay, then a ramp to all of it
	float last = 0.0f;
	int firstBoosted = -1, full = -1;
	for (int i = 0; i < 60; ++i) {
		const float share = openq4::AdvanceTurnBoost(state, 1.0f, tick, true, settings);
		CHECK(share >= last);
		if (share > 0.0f && firstBoosted < 0) {
			firstBoosted = i;
		}
		if (share >= 1.0f && full < 0) {
			full = i;
		}
		last = share;
	}
	// the first sample over arms it, 0.12 s is 7.2 further ticks, 0.42 s is 25.2
	CHECK(firstBoosted == 8);
	CHECK(full == 26);
	CHECK(state.direction == 1);
	// the late-look path reads the same share without advancing it
	CHECK(openq4::TurnBoostFraction(state, settings) == 1.0f);
	const openq4::TurnBoostState before = state;
	(void)openq4::TurnBoostFraction(state, settings);
	CHECK(state.held == before.held && state.direction == before.direction);

	// letting the stick back drops it at once
	CHECK(openq4::AdvanceTurnBoost(state, 0.5f, tick, true, settings) == 0.0f);
	CHECK(openq4::TurnBoostFraction(state, settings) == 0.0f);

	// reversing starts again from nothing
	for (int i = 0; i < 60; ++i) {
		(void)openq4::AdvanceTurnBoost(state, 1.0f, tick, true, settings);
	}
	CHECK(openq4::TurnBoostFraction(state, settings) == 1.0f);
	CHECK(openq4::AdvanceTurnBoost(state, -1.0f, tick, true, settings) == 0.0f);
	CHECK(state.direction == -1);

	// zooming (or a radial selector) stands it down, however long it was held
	for (int i = 0; i < 60; ++i) {
		(void)openq4::AdvanceTurnBoost(state, -1.0f, tick, true, settings);
	}
	CHECK(openq4::TurnBoostFraction(state, settings) == 1.0f);
	CHECK(openq4::AdvanceTurnBoost(state, -1.0f, tick, false, settings) == 0.0f);
	CHECK(openq4::TurnBoostFraction(state, settings) == 0.0f);

	// no ramp: all of it as soon as the delay has passed
	openq4::TurnBoostSettings instant;
	instant.ramp = 0.0f;
	openq4::TurnBoostState quick;
	float share = 0.0f;
	for (int i = 0; i < 10; ++i) {
		share = openq4::AdvanceTurnBoost(quick, 1.0f, tick, true, instant);
	}
	CHECK(share == 1.0f);
}

// A half turn with the Dynamic preset against Classic: the model integrates the
// rate a usercmd at a time, as the generator does.
static double SecondsToTurn(const openq4::StickLookSettings &look, double degrees) {
	const openq4::TurnBoostSettings boost;
	openq4::TurnBoostState state;
	const float tick = 1.0f / 60.0f;
	double turned = 0.0;
	int ticks = 0;
	while (turned < degrees && ticks < 6000) {
		const float share = openq4::AdvanceTurnBoost(state, 1.0f, tick, true, boost);
		turned += -openq4::ComputeStickLookRate(1.0f, 0.0f, look, share).yaw * tick;
		++ticks;
	}
	return ticks * double(tick);
}

static void HalfTurn() {
	openq4::StickLookSettings classic;
	openq4::StickLookSettings dynamic = classic;
	openq4::StickResponseValues values;
	CHECK(openq4::StickResponsePresetValues(openq4::STICK_RESPONSE_DYNAMIC, values));
	dynamic.boostSpeed = values.boostSpeed;
	const double classicSeconds = SecondsToTurn(classic, 180.0);
	const double dynamicSeconds = SecondsToTurn(dynamic, 180.0);
	CHECK(classicSeconds > 1.1 && classicSeconds < 1.2);	// 180 / 157.5
	CHECK(dynamicSeconds < 0.75);
}

// The usercmd integrates the rate over a whole tick; the drawn view shows the
// share of that tick already elapsed. Both come from one function, so at the
// end of the tick the drawn view equals the usercmd.
static void LateLookEqualsUsercmd() {
	openq4::StickLookSettings settings;
	settings.yawSpeed = 300.0f;
	settings.pitchSpeed = 200.0f;
	settings.boostSpeed = 150.0f;
	const openq4::TurnBoostSettings boost;
	openq4::TurnBoostState state;
	const float tick = 1.0f / 60.0f;
	double yaw = 0.0, pitch = 0.0;
	for (int i = 0; i < 90; ++i) {
		const float lookX = i < 60 ? 1.0f : -0.4f;
		const float lookY = 0.3f;
		// what a frame drawn at the very end of this tick would show: the boost
		// as the last usercmd left it, advanced by nothing
		openq4::TurnBoostState preview = state;
		const float previewShare = openq4::AdvanceTurnBoost(preview, lookX, tick, true, boost);
		const openq4::StickLookRate drawn = openq4::ComputeStickLookRate(lookX, lookY, settings, previewShare);
		const double drawnYaw = yaw + drawn.yaw * tick, drawnPitch = pitch + drawn.pitch * tick;
		// the usercmd for the same tick
		const float share = openq4::AdvanceTurnBoost(state, lookX, tick, true, boost);
		const openq4::StickLookRate applied = openq4::ComputeStickLookRate(lookX, lookY, settings, share);
		yaw += applied.yaw * tick;
		pitch += applied.pitch * tick;
		CHECK(Near(drawnYaw, yaw, 0.000001) && Near(drawnPitch, pitch, 0.000001));
	}
}

int main() {
	DeadZones();
	Axial();
	ClassicUnchanged();
	Presets();
	LookRate();
	TurnBoost();
	HalfTurn();
	LateLookEqualsUsercmd();
	std::puts("controller look: ok");
	return 0;
}
