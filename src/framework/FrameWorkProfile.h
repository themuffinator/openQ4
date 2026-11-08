// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef OPENQ4_FRAME_WORK_PROFILE_H
#define OPENQ4_FRAME_WORK_PROFILE_H
#include <array>
#include <cmath>
#include <vector>

namespace openq4 {
enum class FrameWorkPhase { Gap, Wait, Events, Configuration, Commands, Session, Draw, Tail, Count };
struct FrameWorkSample {
	int frame = 0;
	// When the frame's Wait phase began, on the clock passed to Begin (the
	// presentation clock in milliseconds: QueryPerformanceCounter on Windows),
	// so external samplers on the same clock can attribute work to frames.
	double startMs = 0;
	std::array<double, static_cast<int>(FrameWorkPhase::Count)> milliseconds{};
	double Total() const { double total=0; for (double value:milliseconds) total+=value; return total; }
};
// Main-thread, bounded diagnostic capture. Starting/restarting from the command
// phase discards that partial frame. No allocation or clock reads are needed
// after capture completes, and no sample logging happens during capture.
class FrameWorkProfile {
public:
	bool Reset(int count) {
		if (count<0 || count>60000) return false;
		samples.clear(); samples.reserve(count); remaining=count; recording=false; previousEnd=-1;
		return true;
	}
	bool Active() const { return remaining>0; }
	void Begin(double now) {
		recording=Active() && std::isfinite(now);
		if (!recording) return;
		current={}; current.startMs=now; next=static_cast<int>(FrameWorkPhase::Wait); last=now;
		current.milliseconds[0]=previousEnd>=0 && now>=previousEnd ? now-previousEnd : 0;
	}
	void Mark(FrameWorkPhase phase, double now) {
		if (!recording) return;
		if (static_cast<int>(phase)!=next || !std::isfinite(now) || now<last) { Abort(); return; }
		current.milliseconds[next++]=now-last; last=now;
	}
	bool End(int frame, double now) {
		Mark(FrameWorkPhase::Tail,now);
		if (!recording || next!=static_cast<int>(FrameWorkPhase::Count)) return false;
		current.frame=frame; samples.push_back(current); previousEnd=now; recording=false;
		return --remaining==0;
	}
	void Abort() { recording=false; previousEnd=-1; }
	// Ends an incomplete capture early, keeping the frames recorded so far.
	bool Stop() { remaining=0; recording=false; previousEnd=-1; return !samples.empty(); }
	const std::vector<FrameWorkSample>& Samples() const { return samples; }
private:
	std::vector<FrameWorkSample> samples;
	FrameWorkSample current;
	int remaining=0, next=0;
	bool recording=false;
	double last=0, previousEnd=-1;
};
}
#endif
