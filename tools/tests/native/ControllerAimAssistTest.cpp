// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Aim assistance (PAD-03): the production policy without the engine or a game
// module. Strength bounds, the server cap, the slowdown's shape, follow taking
// only a share of a target's motion and never its offset (so it cannot snap),
// and the cases that follow nothing.
#include "src/framework/ControllerAimAssist.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); std::exit(1); } } while (0)

static bool Near(double a, double b, double epsilon = 0.0001) { return std::fabs(a - b) < epsilon; }

static openq4::AimAssistTarget Target(float offset, float radius, float distance) {
	openq4::AimAssistTarget target;
	target.offset = offset;
	target.radius = radius;
	target.distance = distance;
	return target;
}

static void Strengths() {
	using namespace openq4;
	// Off assists nothing, whatever the target
	const AimAssistLevel off = AimAssistForStrength(AIM_ASSIST_OFF);
	CHECK(off.slow == 1.0f && off.follow == 0.0f && off.range == 0.0f && off.guiSlow == 1.0f);
	CHECK(AimAssistSlowdown(off, Target(0.0f, 2.0f, 100.0f)) == 1.0f);
	CHECK(!AimAssistInReach(off, Target(0.0f, 2.0f, 100.0f)));
	// each step is stronger than the last, and all stay in bounds
	AimAssistLevel previous = off;
	for (int strength = AIM_ASSIST_LOW; strength <= AIM_ASSIST_HIGH; ++strength) {
		const AimAssistLevel level = AimAssistForStrength(strength);
		CHECK(level.slow < previous.slow && level.slow >= 0.4f);
		CHECK(level.follow > previous.follow && level.follow <= 0.5f);	// never more than half the target's motion
		CHECK(level.margin > previous.margin && level.margin <= 5.0f);
		CHECK(level.range > previous.range);
		CHECK(level.guiSlow < previous.guiSlow && level.guiSlow >= 0.4f);
		previous = level;
	}
	// out of range strengths are Off
	CHECK(AimAssistForStrength(-1).range == 0.0f && AimAssistForStrength(4).range == 0.0f);

	// the server's cap: the lower of the two, off on a server that allows none
	CHECK(AimAssistAllowed(AIM_ASSIST_HIGH, AIM_ASSIST_OFF) == AIM_ASSIST_OFF);
	CHECK(AimAssistAllowed(AIM_ASSIST_HIGH, AIM_ASSIST_LOW) == AIM_ASSIST_LOW);
	CHECK(AimAssistAllowed(AIM_ASSIST_LOW, AIM_ASSIST_HIGH) == AIM_ASSIST_LOW);
	CHECK(AimAssistAllowed(AIM_ASSIST_OFF, AIM_ASSIST_HIGH) == AIM_ASSIST_OFF);
	CHECK(AimAssistAllowed(99, 99) == AIM_ASSIST_HIGH && AimAssistAllowed(-5, 3) == AIM_ASSIST_OFF && AimAssistAllowed(3, -5) == AIM_ASSIST_OFF);
}

static void Slowdown() {
	using namespace openq4;
	const AimAssistLevel level = AimAssistForStrength(AIM_ASSIST_STANDARD);	// 0.60 on target, 3 degrees of margin
	// on the target: the full slowdown, anywhere within its radius
	CHECK(Near(AimAssistSlowdown(level, Target(0.0f, 2.0f, 500.0f)), 0.60));
	CHECK(Near(AimAssistSlowdown(level, Target(2.0f, 2.0f, 500.0f)), 0.60));
	// beside it: easing out across the margin, with no step at either end
	const float inside = AimAssistSlowdown(level, Target(2.01f, 2.0f, 500.0f));
	const float middle = AimAssistSlowdown(level, Target(3.5f, 2.0f, 500.0f));
	const float outside = AimAssistSlowdown(level, Target(4.99f, 2.0f, 500.0f));
	CHECK(inside > 0.60f && inside < 0.61f);
	CHECK(Near(middle, 0.80));
	CHECK(outside > 0.99f && outside <= 1.0f);
	float last = 0.0f;
	for (int i = 0; i <= 60; ++i) {
		const float scale = AimAssistSlowdown(level, Target(i * 0.1f, 2.0f, 500.0f));
		CHECK(scale >= last && scale <= 1.0f && scale >= 0.60f);	// never a speed-up, never below the level's
		last = scale;
	}
	// past the margin, or out of range: nothing
	CHECK(AimAssistSlowdown(level, Target(5.01f, 2.0f, 500.0f)) == 1.0f);
	CHECK(AimAssistSlowdown(level, Target(0.0f, 2.0f, level.range + 1.0f)) == 1.0f);
	CHECK(AimAssistSlowdown(level, Target(0.0f, 2.0f, level.range)) < 1.0f);
	// bad numbers: nothing
	CHECK(AimAssistSlowdown(level, Target(std::nanf(""), 2.0f, 500.0f)) == 1.0f);
	CHECK(AimAssistSlowdown(level, Target(0.0f, std::nanf(""), 500.0f)) == 1.0f);
}

static void Follow() {
	using namespace openq4;
	const AimAssistLevel level = AimAssistForStrength(AIM_ASSIST_STANDARD);	// follows 30%
	const float dt = 1.0f / 60.0f;
	AimAssistTrack track;

	// the first sight of a target follows nothing: there is no motion yet,
	// and above all no pull across the gap to it
	AimAssistFollow follow = UpdateAimAssistFollow(track, level, 7, 40.0f, -5.0f, dt);
	CHECK(follow.yaw == 0.0f && follow.pitch == 0.0f);

	// a target crossing at 60 degrees per second: the stick's turn takes 30% of it
	follow = UpdateAimAssistFollow(track, level, 7, 41.0f, -5.0f, dt);
	CHECK(Near(follow.yaw, 18.0, 0.01) && Near(follow.pitch, 0.0));
	// and the other way, and vertically
	follow = UpdateAimAssistFollow(track, level, 7, 40.0f, -4.5f, dt);
	CHECK(Near(follow.yaw, -18.0, 0.01) && Near(follow.pitch, 9.0, 0.01));

	// a still target follows nothing, however far the crosshair is from it:
	// follow never reduces the offset, so it cannot snap
	for (int i = 0; i < 30; ++i) {
		follow = UpdateAimAssistFollow(track, level, 7, 40.0f, -4.5f, dt);
		CHECK(follow.yaw == 0.0f && follow.pitch == 0.0f);
	}

	// another target: nothing this frame, even though its direction is far away
	follow = UpdateAimAssistFollow(track, level, 9, -120.0f, 10.0f, dt);
	CHECK(follow.yaw == 0.0f && follow.pitch == 0.0f);
	// losing the target and seeing it again: nothing on the frame it returns
	follow = UpdateAimAssistFollow(track, level, -1, 0.0f, 0.0f, dt);
	CHECK(follow.yaw == 0.0f);
	follow = UpdateAimAssistFollow(track, level, 9, -119.0f, 10.0f, dt);
	CHECK(follow.yaw == 0.0f);

	// across the wrap the motion is the short way round
	(void)UpdateAimAssistFollow(track, level, 9, 179.5f, 0.0f, dt);
	follow = UpdateAimAssistFollow(track, level, 9, -179.5f, 0.0f, dt);
	CHECK(Near(follow.yaw, 18.0, 0.01));

	// a teleporting or respawning target (its direction jumps) follows nothing
	follow = UpdateAimAssistFollow(track, level, 9, -100.0f, 0.0f, dt);
	CHECK(follow.yaw == 0.0f);
	// a fast but real crossing is followed only up to 120 degrees per second's share
	(void)UpdateAimAssistFollow(track, level, 9, 0.0f, 0.0f, dt);
	follow = UpdateAimAssistFollow(track, level, 9, 4.0f, 0.0f, dt);	// 240 degrees per second
	CHECK(Near(follow.yaw, 120.0 * 0.30, 0.01));

	// Off follows nothing; a zero interval follows nothing
	const AimAssistLevel off = AimAssistForStrength(AIM_ASSIST_OFF);
	(void)UpdateAimAssistFollow(track, off, 9, 4.0f, 0.0f, dt);
	follow = UpdateAimAssistFollow(track, off, 9, 5.0f, 0.0f, dt);
	CHECK(follow.yaw == 0.0f);
	follow = UpdateAimAssistFollow(track, level, 9, 6.0f, 0.0f, 0.0f);
	CHECK(follow.yaw == 0.0f);

	// Never a snap, as a whole: a target strafing past a still crosshair for
	// a second moves 60 degrees; the view follows 18 and is left 42 behind.
	AimAssistTrack strafe;
	double view = 0.0;
	float direction = 0.0f;
	(void)UpdateAimAssistFollow(strafe, level, 3, direction, 0.0f, dt);
	for (int i = 0; i < 60; ++i) {
		direction += 1.0f;
		view += UpdateAimAssistFollow(strafe, level, 3, direction, 0.0f, dt).yaw * dt;
	}
	CHECK(Near(view, 18.0, 0.05) && direction - view > 40.0);
}

static void StickMoving() {
	using namespace openq4;
	// with both sticks at rest nothing is followed: the view never moves by itself
	CHECK(!AimAssistStickMoving(0.0f, 0.0f, 0.0f, 0.0f));
	CHECK(!AimAssistStickMoving(0.04f, -0.04f, 0.15f, -0.15f));
	// the look stick at all, or the movement stick (strafing around a target)
	CHECK(AimAssistStickMoving(0.06f, 0.0f, 0.0f, 0.0f));
	CHECK(AimAssistStickMoving(0.0f, -0.5f, 0.0f, 0.0f));
	CHECK(AimAssistStickMoving(0.0f, 0.0f, -0.3f, 0.0f));
	CHECK(AimAssistStickMoving(0.0f, 0.0f, 0.0f, 0.9f));
}

int main() {
	Strengths();
	Slowdown();
	Follow();
	StickMoving();
	std::puts("controller aim assist: ok");
	return 0;
}
