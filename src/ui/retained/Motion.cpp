// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Motion.h"
#include <algorithm>
#include <cmath>

namespace openq4::ui {
namespace {
void BaseValues(const Node& node, PropertyValues& values) {
	for (const auto& [name,value] : node.properties) values[{node.id,name}] = value;
	for (const auto& child : node.children) BaseValues(child,values);
}
}
void Motion::Reset(const DocumentModel& model) {
	timelines.clear(); playing.clear(); base.clear();
	for (const auto& timeline : model.timelines) timelines.emplace(timeline.id,timeline);
	BaseValues(model.root,base);
	values = base;
	clock = 0;
}
Value Motion::Sample(const Track& track, double milliseconds) {
	if (milliseconds <= track.keys.front().atMs) return track.keys.front().value;
	for (size_t i = 1; i < track.keys.size(); ++i) {
		const auto& a = track.keys[i-1];
		const auto& b = track.keys[i];
		if (milliseconds < b.atMs) return a.value.Interpolate(b.value,a.easing.Evaluate((milliseconds-a.atMs)/(b.atMs-a.atMs)));
	}
	return track.keys.back().value;
}
void Motion::Advance(double seconds) {
	if (std::isfinite(seconds)) clock = std::max(clock,seconds);
	for (auto it = playing.begin(); it != playing.end();) {
		auto& item = it->second;
		const double elapsedMs = std::max(0.0,((item.paused ? item.pauseAt : clock)-item.start)*1000);
		const bool done = item.iterations != 0 && elapsedMs >= item.durationMs*item.iterations;
		if (elapsedMs >= item.durationMs) item.track.keys.front().value = item.repeatStart;
		values[it->first] = Sample(item.track,done ? item.durationMs : std::fmod(elapsedMs,item.durationMs));
		if (done) it = playing.erase(it); else ++it;
	}
}
bool Motion::Play(const std::string& id, double seconds) {
	const auto found = timelines.find(id);
	if (found == timelines.end()) return false;
	Advance(seconds);
	const auto& timeline = found->second;
	for (const auto& track : timeline.tracks) {
		const PropertyKey key{track.node,track.property};
		Playing item{id,track,track.keys.front().value,clock,timeline.durationMs,0,timeline.iterations,false,timeline.essential};
		// Retarget from the value sampled at this exact clock, not the previous
		// frame or the authored beginning. The newest play owns this property.
		item.track.keys.front().value = values.at(key);
		if (reducedMotion && !item.essential) {
			item.iterations = 1;
			if (track.property != "opacity") {
				values[key] = track.keys.back().value;
				playing.erase(key);
				continue;
			}
			item.durationMs = std::min(80.0,item.durationMs);
			// Reduced motion is one bounded opacity interpolation. Discard
			// stagger/key oscillations as well as decorative spatial motion.
			item.track.keys = {{0,values.at(key),{}},{item.durationMs,track.keys.back().value,{}}};
		}
		playing[key] = std::move(item);
	}
	return true;
}
void Motion::Pause(const std::string& id, double seconds) {
	Advance(seconds);
	for (auto& [key,item] : playing) if (item.owner == id && !item.paused) { item.paused = true; item.pauseAt = clock; }
}
void Motion::Resume(const std::string& id, double seconds) {
	Advance(seconds);
	for (auto& [key,item] : playing) if (item.owner == id && item.paused) { item.start += clock-item.pauseAt; item.paused = false; }
}
void Motion::Cancel(const std::string& id, CancelPolicy policy, double seconds) {
	Advance(seconds);
	for (auto it = playing.begin(); it != playing.end();) {
		if (it->second.owner != id) { ++it; continue; }
		if (policy == CancelPolicy::RestoreBase) values[it->first] = base.at(it->first);
		it = playing.erase(it);
	}
}
void Motion::SetReducedMotion(bool enabled, double seconds) {
	Advance(seconds);
	if (reducedMotion == enabled) return;
	reducedMotion = enabled;
	if (!enabled) return; // Never resurrect cancelled decorative movement.
	for (auto it = playing.begin(); it != playing.end();) {
		auto& item = it->second;
		if (item.essential) { ++it; continue; }
		if (it->first.second != "opacity") {
			values[it->first] = item.track.keys.back().value;
			it = playing.erase(it); continue;
		}
		const double remaining = item.durationMs-std::fmod(std::max(0.0,((item.paused ? item.pauseAt : clock)-item.start)*1000),item.durationMs);
		item.durationMs = std::min(80.0,remaining);
		item.track.keys = {{0,values.at(it->first),{}},{item.durationMs,item.track.keys.back().value,{}}};
		item.start = clock; item.pauseAt = clock; item.iterations = 1;
		++it;
	}
}
bool Motion::IsPlaying(const std::string& id) const {
	for (const auto& [key,item] : playing) if (item.owner == id) return true;
	return false;
}
PropertyValues Motion::Scrub(const std::string& id, double milliseconds) const {
	PropertyValues result;
	auto found = timelines.find(id);
	if (found == timelines.end() || !std::isfinite(milliseconds)) return result;
	for (const auto& track : found->second.tracks) result[{track.node,track.property}] = Sample(track,std::clamp(milliseconds,0.0,found->second.durationMs));
	return result;
}
} // namespace openq4::ui
