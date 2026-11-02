// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// The shared exclusive fullscreen mode rule (src/sys/DisplayModeRule.h), and its
// agreement with SDL 3.4.18's refresh-0 choice: SDL_GetClosestFullscreenDisplayMode
// over a mode list sorted by SDL's cmpmodes, transcribed from SDL_video.c.
#include "src/sys/DisplayModeRule.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

using openq4::DisplayModeCandidate;
using openq4::DisplayRefreshHertz;
using openq4::SelectDisplayMode;

namespace {
int checks = 0;
void Check(bool condition, const char* what) {
	++checks;
	if (!condition) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
int Select(const std::vector<DisplayModeCandidate>& list, int width, int height, int refresh, float desktop) {
	return SelectDisplayMode(int(list.size()), [&](int i, DisplayModeCandidate& mode) {
		mode = list[size_t(i)]; return mode.pixelWidth != 0;
	}, width, height, refresh, desktop);
}

// SDL's mode, as far as its order and closest-mode loop read it.
struct SdlMode { int w, h, bits, bytes; float refresh; };
// cmpmodes (SDL_video.c): width, height, bits per pixel, refresh*100, all descending.
bool SdlBefore(const SdlMode& a, const SdlMode& b) {
	if (a.w != b.w) return a.w > b.w;
	if (a.h != b.h) return a.h > b.h;
	if (a.bits != b.bits) return a.bits > b.bits;
	return int(a.refresh * 100) > int(b.refresh * 100);
}
// SDL_GetClosestFullscreenDisplayMode's loop for refresh 0, density-1 modes.
int SdlClosest(const std::vector<SdlMode>& modes, int w, int h, float desktop) {
	const float aspect = float(w) / h, target = desktop;
	int closest = -1;
	for (int i = 0; i < int(modes.size()); ++i) {
		const SdlMode& mode = modes[size_t(i)];
		if (w > mode.w) break;
		if (h > mode.h) continue;
		if (closest >= 0) {
			const SdlMode& kept = modes[size_t(closest)];
			if (std::fabs(aspect - float(kept.w) / kept.h) < std::fabs(aspect - float(mode.w) / mode.h)) continue;
			if (mode.w == kept.w && mode.h == kept.h) {
				if (std::fabs(kept.refresh - target) < std::fabs(mode.refresh - target)) continue;
				if (kept.bytes > mode.bytes) continue;
			}
		}
		closest = i;
	}
	return closest;
}
std::uint32_t random32(std::uint64_t& state) {
	state = state * 6364136223846793005ull + 1442695040888963407ull;
	return std::uint32_t(state >> 33);
}
} // namespace

int main() {
	// The whole hertz a rate is listed and requested by.
	Check(DisplayRefreshHertz(59.94f) == 60 && DisplayRefreshHertz(143.86f) == 144 && DisplayRefreshHertz(59.49f) == 59, "rates round to whole hertz");

	const std::vector<DisplayModeCandidate> uhd{{3840, 2160, 144}, {3840, 2160, 60}, {3840, 2160, 59.94f}, {1920, 1080, 60}};
	// Auto: the rate nearest the desktop's.
	Check(Select(uhd, 3840, 2160, 0, 60) == 1, "Auto keeps a 60 Hz desktop's rate");
	Check(Select(uhd, 3840, 2160, 0, 59.94f) == 2, "Auto keeps a 59.94 Hz desktop's rate");
	Check(Select(uhd, 3840, 2160, 0, 144) == 0, "Auto keeps a 144 Hz desktop's rate");
	Check(Select(uhd, 3840, 2160, 0, 100) == 1, "Auto takes the nearest rate to the desktop's");
	// A whole-hertz choice: the highest rate in its bucket, whatever the desktop runs.
	Check(Select(uhd, 3840, 2160, 60, 144) == 1, "60 Hz takes the highest rate in its bucket");
	Check(Select(uhd, 3840, 2160, 75, 60) == -1 && Select(uhd, 2560, 1440, 0, 60) == -1, "no other size or rate stands in");
	// Of two equally near rates the lower wins, as in SDL.
	Check(Select({{1600, 900, 70}, {1600, 900, 50}}, 1600, 900, 0, 60) == 1, "equally near rates take the lower");
	// Equal rates keep the first mode, SDL's deepest format.
	Check(Select({{1920, 1080, 60}, {1920, 1080, 60}}, 1920, 1080, 0, 60) == 0, "equal rates keep the first mode");
	// An unknown desktop rate aims at 0, as SDL does.
	Check(Select({{1920, 1080, 144}, {1920, 1080, 60}}, 1920, 1080, 0, 0) == 1, "an unknown desktop rate takes the lowest");
	// Unusable modes never match.
	Check(Select({{0, 0, 60}, {1920, 1080, std::numeric_limits<float>::quiet_NaN()}, {1920, 1080, -1}, {1920, 1080, 60}}, 1920, 1080, 0, 60) == 3,
		"unusable modes never match");

	// Agreement with SDL's refresh-0 choice over random density-1 lists, with
	// a 16-bit copy of some modes at the same rate, as Windows lists them.
	const int sizes[][2]{{1280, 720}, {1920, 1080}, {1920, 1200}, {2560, 1440}};
	const float rates[]{50, 59.94f, 60, 75, 100, 119.88f, 120, 143.86f, 144, 165, 240};
	std::uint64_t state = 0x9e3779b97f4a7c15ull;
	int compared = 0;
	for (int round = 0; round < 20000; ++round) {
		std::vector<SdlMode> modes;
		for (const auto& size : sizes) {
			if (random32(state) % 4 == 0) continue;
			for (float rate : rates) {
				if (random32(state) % 3 == 0) continue;
				modes.push_back({size[0], size[1], 24, 4, rate});
				if (random32(state) % 4 == 0) modes.push_back({size[0], size[1], 16, 2, rate});
			}
		}
		if (modes.empty()) continue;
		std::sort(modes.begin(), modes.end(), SdlBefore);
		const float desktop = rates[random32(state) % (sizeof(rates) / sizeof(rates[0]))];
		std::vector<DisplayModeCandidate> published;
		for (const auto& mode : modes) published.push_back({mode.w, mode.h, mode.refresh});
		for (const auto& size : sizes) {
			const int ours = Select(published, size[0], size[1], 0, desktop);
			const bool offered = std::any_of(modes.begin(), modes.end(), [&](const SdlMode& m) { return m.w == size[0] && m.h == size[1]; });
			if (!offered) { Check(ours == -1, "an absent size selects nothing"); continue; }
			const int sdl = SdlClosest(modes, size[0], size[1], desktop);
			Check(ours >= 0 && sdl >= 0, "an offered size selects a mode");
			Check(modes[size_t(sdl)].w == size[0] && modes[size_t(sdl)].h == size[1], "SDL selects the exact size it offers");
			Check(modes[size_t(ours)].refresh == modes[size_t(sdl)].refresh, "Auto agrees with SDL's refresh-0 choice");
			Check(modes[size_t(ours)].bytes == 4, "Auto keeps the deepest format");
			++compared;
		}
	}
	Check(compared > 40000, "the differential compares many lists");
	// A documented divergence: SDL's depth gate keeps a deeper 60 Hz over a
	// shallower 75 Hz at a 75 Hz desktop; the published list carries no format.
	std::vector<SdlMode> gated{{1920, 1080, 24, 4, 144}, {1920, 1080, 24, 4, 60}, {1920, 1080, 16, 2, 75}};
	Check(gated[size_t(SdlClosest(gated, 1920, 1080, 75))].refresh == 60, "SDL's depth gate keeps the deeper mode");
	Check(Select({{1920, 1080, 144}, {1920, 1080, 60}, {1920, 1080, 75}}, 1920, 1080, 0, 75) == 2, "the published list takes the desktop rate");
	std::printf("Display mode rule: %d checks passed (%d SDL comparisons)\n", checks, compared);
	return 0;
}
