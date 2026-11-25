// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cmath>

// The one exclusive fullscreen mode rule. The SYSTEM request, the restore it
// records and its recovery replays (src/ui/application/SystemDisplay.cpp), the
// strict window service and legacy startup, vid_restart and Alt+Enter
// (src/sys/sdl3/sdl3_backend.cpp) all choose through it, so the rate a player
// applies is the rate the next start chooses.
namespace openq4 {

// A fullscreen mode as the rule reads it: its size in physical pixels (SDL's
// size times its pixel density, rounded) and SDL's finalized refresh rate.
struct DisplayModeCandidate {
	int pixelWidth = 0, pixelHeight = 0;
	float refresh = 0;
};

// The whole hertz r_displayRefresh and the SYSTEM lists name a rate by: 59.94 is 60.
inline int DisplayRefreshHertz(float refresh) {
	return static_cast<int>(std::floor(static_cast<double>(refresh) + 0.5));
}

// Whether a desktop fullscreen window of width x height physical pixels fits
// the display's desktop mode. The window system owns that window's size, and it
// can be smaller than the display: macOS keeps the strip beside the camera
// housing of a notched MacBook out of a fullscreen Space, so a 3456x2234
// display gives a 3456x2168 window (#260). It never exceeds the desktop mode.
inline bool DesktopFullscreenPixelsFit(int width, int height, int desktopWidth, int desktopHeight) {
	return width > 0 && height > 0 && width <= desktopWidth && height <= desktopHeight;
}

// The index of the mode an exclusive request for exactly width x height pixels
// selects, or -1. read(i, mode) fills mode i of count, in SDL's list order
// (SDL_GetFullscreenDisplayModes), and returns false for a mode the caller
// cannot use.
// - refresh > 0: the highest rate whose whole hertz is refresh.
// - refresh 0, Auto: the rate nearest desktopRefresh, the display's desktop
//   rate, which is the target SDL_GetClosestFullscreenDisplayMode uses for
//   refresh 0. Of two equally near rates the lower wins, as in SDL, whose list
//   runs from the highest rate down.
// - Equal rates keep the first mode in SDL's order: the most bits per pixel at
//   the lowest pixel density. Where Cocoa lists ARGB2101010 before ARGB8888,
//   SDL's own closest call takes the later 8-bit mode; this keeps the 10-bit
//   one, as the earlier macOS and strict searches did.
template <class Read>
int SelectDisplayMode(int count, Read&& read, int width, int height, int refresh, float desktopRefresh) {
	int chosen = -1;
	float best = 0, bestDistance = 0;
	for (int i = 0; i < count; ++i) {
		DisplayModeCandidate mode;
		if (!read(i, mode) || mode.pixelWidth != width || mode.pixelHeight != height ||
			!std::isfinite(mode.refresh) || mode.refresh < 0) continue;
		const float distance = std::fabs(mode.refresh - desktopRefresh);
		if (refresh > 0) {
			if (DisplayRefreshHertz(mode.refresh) != refresh || (chosen >= 0 && !(mode.refresh > best))) continue;
		} else if (chosen >= 0 && (distance > bestDistance || (distance == bestDistance && !(mode.refresh < best)))) {
			continue;
		}
		chosen = i; best = mode.refresh; bestDistance = distance;
	}
	return chosen;
}

} // namespace openq4
