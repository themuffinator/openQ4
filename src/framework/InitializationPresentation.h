// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <algorithm>

namespace openq4 {
// Cooperative startup presentation: no sleeping, catch-up frames or worker
// rendering. Frames run at up to 60 Hz while their smoothed cost stays inside
// a share of wall time, so expensive presentation cannot consume
// initialization. The entrance, which only reads as motion when it is smooth,
// may take half the time for its first EntranceMs; afterwards a fifth.
// Times are monotonic milliseconds.
class InitializationPresentation {
public:
	static constexpr double EntranceMs = 1400; // the entrance timeline plus the frame before it
	bool Due(double now, int presentationCap) const {
		const double interval = 1000.0 / (presentationCap > 0 ? (std::min)(60, presentationCap) : 60);
		const double reserve = now - first < EntranceMs ? 1 : 4; // loading time per unit of presentation
		return ready && now - completed >= (std::max)(interval - cost, cost * reserve);
	}
	void Presented(double start, double end) {
		// The first frame includes one-time document/geometry creation. Its cost
		// must not freeze an already warm screen for the rest of startup. Later
		// costs are smoothed: one blocked present must not stall the animation,
		// while a consistently slow device still converges on its real cost.
		const double spent = (std::max)(0.0, end - start);
		cost = ready ? (cost > 0 ? cost * .75 + spent * .25 : spent) : 0;
		if (!ready) first = end;
		completed = end;
		ready = true;
	}
private:
	bool ready = false;
	double completed = 0, cost = 0, first = 0;
};
}
