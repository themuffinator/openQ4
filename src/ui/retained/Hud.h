// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace openq4::ui {
// An evaluated HUD draw, in the engine's 640x480 submission space. The quad
// already includes GUI anchoring, clipping and transforms. UVs retain the
// original unclipped artwork extent, including mirrored and tiled artwork.
struct HudPrimitive {
	std::string material, artwork;
	std::array<float,8> quad{};
	std::array<float,4> uv{0,0,1,1}, colour{1,1,1,1};
	bool glyph = false;
};

// Compare the displayed values, rather than the inventory pickup pulse:
// reloading increases the clip, while a pickup can affect only the reserve.
// Joining, changing weapon, or entering/leaving unlimited ammo never pops.
struct HudAmmoReadout {
	std::string weapon;
	int clip = -1, total = -1;
	bool clipped = false;
};
enum HudAmmoGain { HudAmmoGainNone = 0, HudAmmoGainClip = 1, HudAmmoGainReserve = 2, HudAmmoGainTotal = 4 };
inline int HudAmmoGains(const HudAmmoReadout& before, const HudAmmoReadout& after) {
	if (before.weapon.empty() || before.weapon != after.weapon || before.clipped != after.clipped ||
		before.total < 0 || after.total < 0) return HudAmmoGainNone;
	int gains = HudAmmoGainNone;
	if (after.clipped && before.clip >= 0 && after.clip > before.clip) gains |= HudAmmoGainClip;
	if (after.total > before.total) gains |= after.clipped ? HudAmmoGainReserve : HudAmmoGainTotal;
	return gains;
}

// The Remastered HUD's live anchors (ui-visual-design.md 14.4) in the canvas
// dp of its documents: the view, and each edge anchor's distance from its
// edge. `ratio` is the documents' pixels per dp (their 720 dp view-height
// canvas, 960 dp wide at least, fits the window's safe area) and `safe` the
// window's safe-area insets in pixels, left, top, right and bottom. An edge
// anchor keeps the larger of its own distance (the side screen beyond the HUD
// width cap, 0 16:9, 1 21:9 or 2 Full, plus the player's Safe Area Inset, a
// percentage of the view) and the window's inset, so readouts keep clear of
// cutouts, rounded corners and system bars while the root, and every
// full-screen effect filling it, stays full-bleed. With no inset (every
// desktop window) the anchors are the player's alone.
struct HudLayout {
	float width = 1280, height = 720, left = 0, top = 0, right = 0, bottom = 0, scale = 1, centerScale = 1;
};
inline HudLayout HudLayoutFor(float viewWidth, float viewHeight, float ratio, const std::array<float,4>& safe, int hudWidth,
	float insetPercent, float scale) {
	HudLayout layout;
	layout.width = viewWidth/ratio; layout.height = viewHeight/ratio;
	const float cap = hudWidth == 0 ? 1280.f : hudWidth == 1 ? 720.f*64.f/27.f : layout.width;
	const float side = (std::max)(0.f,(layout.width-(std::min)(layout.width,cap))*.5f);
	const float inset = std::clamp(insetPercent,0.f,10.f)*.01f;
	const auto window = [&](float pixels) { return std::isfinite(pixels) && pixels > 0 ? pixels/ratio : 0.f; };
	layout.left = (std::max)(side+inset*layout.width,window(safe[0]));
	layout.right = (std::max)(side+inset*layout.width,window(safe[2]));
	layout.top = (std::max)(inset*layout.height,window(safe[1]));
	layout.bottom = (std::max)(inset*layout.height,window(safe[3]));
	layout.scale = std::clamp(scale,.5f,2.f);
	// Narrow views fit the center column (the 438 u status bar and its strip)
	// without changing the saved preference. Center groups stay centered, so
	// the column fits between the deeper side inset and its mirror.
	const float span = layout.width-2*(std::max)(layout.left,layout.right);
	layout.centerScale = (std::min)(layout.scale,(std::max)(.25f,span/690.f));
	return layout;
}
}
