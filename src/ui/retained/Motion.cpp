// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Motion.h"
#include <algorithm>
#include <cmath>
#include <set>

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
			item.reduced = true;
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
		item.reduced = true;
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
MotionSnapshot Motion::Capture(double seconds) const {
	Motion candidate = *this;
	candidate.Advance(seconds);
	MotionSnapshot result;
	result.reducedMotion = candidate.reducedMotion;
	for (const auto& [key,value] : candidate.values) {
		const auto& original = base.at(key);
		if (value.type != original.type || value.data != original.data || value.unit != original.unit || value.text != original.text)
			result.values.emplace(key,value);
	}
	for (const auto& [key,item] : candidate.playing) {
		double elapsed = std::max(0.0,((item.paused ? item.pauseAt : candidate.clock)-item.start)*1000);
		// An unbounded repeat only needs its phase and whether retargeting's
		// first iteration has ended. Avoid accumulating years of elapsed time.
		if (!item.iterations && elapsed >= item.durationMs) elapsed = item.durationMs+std::fmod(elapsed,item.durationMs);
		result.playing.push_back({item.owner,key.first,key.second,item.track.keys.front().value,elapsed,item.durationMs,item.paused,item.reduced});
	}
	return result;
}
bool Motion::Restore(const MotionSnapshot& snapshot, double seconds, std::string& error) {
	error.clear();
	auto reject = [&](const char* message) { error = message; return false; };
	if (!std::isfinite(seconds) || seconds < 0) return reject("Invalid restored presentation time");
	Motion candidate = *this;
	candidate.values = base; candidate.playing.clear(); candidate.clock = seconds;
	candidate.reducedMotion = snapshot.reducedMotion;
	std::set<PropertyKey> animated;
	for (const auto& [id,timeline] : timelines) for (const auto& track : timeline.tracks) animated.emplace(track.node,track.property);
	for (const auto& [key,value] : snapshot.values) {
		const auto original = base.find(key);
		if (!animated.contains(key) || original == base.end() || !original->second.CanInterpolate(value) || !ValidProperty(key.second,value))
			return reject("Invalid restored presentation property");
		candidate.values[key] = value;
	}
	for (const auto& saved : snapshot.playing) {
		const auto timeline = timelines.find(saved.owner);
		if (timeline == timelines.end()) return reject("Unknown restored timeline");
		const auto& authored = timeline->second;
		if (snapshot.reducedMotion && !authored.essential && !saved.reduced) return reject("Restored playback conflicts with reduced motion");
		const auto track = std::find_if(authored.tracks.begin(),authored.tracks.end(),[&](const Track& t) {
			return t.node == saved.node && t.property == saved.property;
		});
		const PropertyKey key{saved.node,saved.property};
		if (track == authored.tracks.end() || candidate.playing.contains(key)) return reject("Invalid or duplicate restored timeline owner");
		if (!ValidProperty(saved.property,saved.from) || !track->keys.front().value.CanInterpolate(saved.from))
			return reject("Invalid restored timeline retarget value");
		if (!std::isfinite(saved.durationMs) || saved.durationMs <= 0 || !std::isfinite(saved.elapsedMs) || saved.elapsedMs < 0)
			return reject("Invalid restored timeline duration or progress");
		if (saved.reduced ? (authored.essential || saved.property != "opacity" || saved.durationMs > std::min(80.0,authored.durationMs)) : saved.durationMs != authored.durationMs)
			return reject("Restored timeline duration conflicts with authored playback");
		const unsigned iterations = saved.reduced ? 1 : authored.iterations;
		if (saved.elapsedMs >= saved.durationMs*(iterations ? iterations : 2.0)) return reject("Restored timeline progress exceeds active playback");
		Playing item{saved.owner,*track,track->keys.front().value,seconds-saved.elapsedMs/1000,saved.durationMs,seconds,iterations,saved.paused,authored.essential,saved.reduced};
		item.track.keys.front().value = saved.from;
		if (saved.reduced) item.track.keys = {{0,saved.from,{}},{saved.durationMs,track->keys.back().value,{}}};
		candidate.playing.emplace(key,std::move(item));
	}
	candidate.Advance(seconds);
	*this = std::move(candidate);
	return true;
}
} // namespace openq4::ui
