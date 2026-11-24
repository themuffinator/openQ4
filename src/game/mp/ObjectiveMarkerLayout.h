// Copyright (C) 2026 DarkMatter Productions. Quake 4 SDK EULA.
#pragma once
#include <algorithm>
#include <cmath>

namespace objectiveMarker {
struct Placement {
	float x = 0, y = 0, directionX = 0, directionY = 0, emphasis = 1;
	bool edge = false;
};

// Camera-space right/up/forward, on a height-normalised, aspect-correct canvas.
// Behind-camera targets retain their bearing instead of mirroring across the
// view. Directly behind has a deterministic downward turn cue. No near-plane
// division, NaNs or unbounded coordinates reach the HUD.
inline bool Project(float right, float up, float forward, float tanX, float tanY,
	float width, float height, float marginX, float marginY, Placement& out, float centerUpTangent = 0) {
	if (!std::isfinite(right) || !std::isfinite(up) || !std::isfinite(forward) ||
		!std::isfinite(tanX) || !std::isfinite(tanY) || tanX <= 0 || tanY <= 0 ||
		!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(marginX) || !std::isfinite(marginY) ||
		!std::isfinite(centerUpTangent) || marginX < 0 || marginY < 0 ||
		width <= 2 * marginX || height <= 2 * marginY) return false;
	const float cx = width * .5f, cy = height * .5f;
	const float rx = cx - marginX, ry = cy - marginY;
	const float depth = (std::max)(std::abs(forward), .001f);
	float dx = right * (cx / tanX) / depth;
	float dy = -up * (cy / tanY) / depth;
	const float gazeDistance = std::hypot(dx, dy);
	// The VR compositor can lower the HUD below the head's forward ray. Map
	// that ray onto the displaced plane, keeping behind-head bearings intact.
	if (forward > .01f) dy += centerUpTangent * (cy / tanY);
	if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
	out.edge = forward <= .01f || std::abs(dx) > rx || std::abs(dy) > ry;
	if (out.edge) {
		if (std::abs(dx) + std::abs(dy) < .001f) dy = 1;
		const float fit = (std::max)(std::abs(dx) / rx, std::abs(dy) / ry);
		dx /= fit; dy /= fit;
	}
	const float length = std::hypot(dx, dy);
	out.directionX = length > .001f ? dx / length : 0;
	out.directionY = length > .001f ? dy / length : -1;
	out.x = cx + dx; out.y = cy + dy;
	// Leave the aim area quiet without moving the actual world anchor.
	out.emphasis = .22f + .78f * std::clamp(((out.edge ? length : gazeDistance) - 18.f) / 48.f, 0.f, 1.f);
	return true;
}

// A head-relative bearing intersects the same finite plane submitted to both
// OpenXR eyes. Its physical size, not either eye's FOV or the aiming hand,
// determines the canvas coordinates. Distances and dimensions are in metres.
inline bool ProjectOnHud(float right, float up, float forward, float distance, float physicalWidth, float offsetUp,
	float width, float height, float marginX, float marginY, Placement& out) {
	if (!std::isfinite(distance) || !std::isfinite(physicalWidth) ||
		!std::isfinite(offsetUp) || distance <= 0 || physicalWidth <= 0 || width <= 0) return false;
	const float tanX = physicalWidth / (2 * distance);
	return Project(right, up, forward, tanX, tanX * height / width,
		width, height, marginX, marginY, out, offsetUp / distance);
}

// Compact symbols stay attached to their projected point. A distance appears
// only when its full footprint fits without covering other markers or text.
inline bool Overlaps(float x, float y, float halfWidth, float halfHeight,
	float otherX, float otherY, float otherHalfWidth, float otherHalfHeight) {
	return std::abs(x-otherX) < halfWidth+otherHalfWidth &&
		std::abs(y-otherY) < halfHeight+otherHalfHeight;
}
}
