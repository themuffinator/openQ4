// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "Document.h"

namespace openq4::ui {

using PropertyKey = std::pair<std::string, std::string>;
using PropertyValues = std::map<PropertyKey, Value>;
enum class CancelPolicy { Hold, RestoreBase };
struct MotionPlayback {
	std::string owner, node, property;
	Value from;
	double elapsedMs = 0, durationMs = 0;
	bool paused = false, reduced = false;
};
struct MotionSnapshot {
	PropertyValues values;
	std::vector<MotionPlayback> playing;
	bool reducedMotion = false;
};

// Shared presentation evaluator for runtime and editor. All times are absolute
// monotonic seconds; sampling gaps are never clamped to simulation/frame ticks.
class Motion {
public:
	void Reset(const DocumentModel& model);
	bool Play(const std::string& id, double seconds);
	void Advance(double seconds);
	void Pause(const std::string& id, double seconds);
	void Resume(const std::string& id, double seconds);
	void Cancel(const std::string& id, CancelPolicy policy, double seconds);
	void SetReducedMotion(bool enabled, double seconds);
	bool ReducedMotion() const { return reducedMotion; }
	const PropertyValues& Values() const { return values; }
	bool IsPlaying(const std::string& id) const;
	// Pure authored-time sampling for editor scrubbing. Does not mutate playback,
	// acquire ownership, or dispatch any future action/event notifications.
	PropertyValues Scrub(const std::string& id, double milliseconds) const;
	MotionSnapshot Capture(double seconds) const;
	bool Restore(const MotionSnapshot& snapshot, double seconds, std::string& error, bool allowStaticValues = false);
	// External presentation writes change current values without cancelling any
	// track. An existing animation can write again on its next sample.
	bool WriteValues(const PropertyValues& changes, std::string& error);
private:
	struct Playing {
		std::string owner;
		Track track;
		Value repeatStart;
		double start = 0, durationMs = 0, pauseAt = 0;
		unsigned iterations = 1;
		bool paused = false, essential = false, reduced = false;
	};
	static Value Sample(const Track& track, double milliseconds);
	std::map<std::string, Timeline> timelines;
	PropertyValues base, values;
	std::map<PropertyKey, Playing> playing;
	double clock = 0;
	bool reducedMotion = false;
};

} // namespace openq4::ui
