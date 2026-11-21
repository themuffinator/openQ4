// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Motion.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <set>

namespace openq4::ui {
namespace {
// Shared by every Motion and its copies, so no two writes share a stamp.
std::atomic<std::uint64_t> motionRevisions{0};
bool SameValue(const Value& a, const Value& b) {
	return a.type == b.type && a.data == b.data && a.unit == b.unit && a.text == b.text;
}
void CollectNodes(const Node& node, std::vector<const Node*>& nodes) {
	nodes.push_back(&node);
	for (const auto& child : node.children) CollectNodes(child,nodes);
}
// Every node's authored values by (node, property). The nodes are taken in
// ID order, so each key follows the last and its insert appends instead of
// searching a document's tens of thousands of keys; among nodes sharing an
// ID the last in document order wins, as it did.
void BaseValues(const Node& root, PropertyValues& values) {
	std::vector<const Node*> nodes;
	CollectNodes(root,nodes);
	std::stable_sort(nodes.begin(),nodes.end(),[](const Node* a, const Node* b) { return a->id < b->id; });
	for (const auto* node : nodes)
		for (const auto& [name,value] : node->properties) values.insert_or_assign(values.end(),PropertyKey{node->id,name},value);
}
}
void Motion::Reset(const DocumentModel& model) { Reset(std::make_shared<const DocumentModel>(model)); }
void Motion::Reset(std::shared_ptr<const DocumentModel> shared) {
	playing.clear(); completed.clear();
	auto loaded = std::make_shared<std::map<std::string, const Timeline*>>();
	for (const auto& timeline : shared->timelines) loaded->emplace(timeline.id,&timeline);
	auto authored = std::make_shared<PropertyValues>();
	BaseValues(shared->root,*authored);
	// The live values are the authored ones until a write replaces one.
	model = std::move(shared); timelines = std::move(loaded);
	base = std::move(authored); values.reset();
	clock = 0; sampled = false;
	changedKeys.clear(); changedAll = true;
	Touch();
}
void Motion::Touch() { revision = ++motionRevisions; }
MotionValues Motion::Values() const {
	static const PropertyValues none; // a moved-from Motion holds none
	return {base ? *base : none,values ? *values : none};
}
// The written entries, private to this motion: every key written here must be
// an authored one (MotionValues reads them in the authored order).
PropertyValues& Motion::Writable() {
	if (!values) values = std::make_shared<PropertyValues>();
	else if (values.use_count() > 1) values = std::make_shared<PropertyValues>(*values);
	return *values;
}
void Motion::Changed(const PropertyKey& key) {
	// A frame writes the few values its playing tracks own; past this many the
	// next reader re-reads every value instead.
	if (changedAll || changedKeys.size() >= 512) { changedAll = true; changedKeys.clear(); return; }
	changedKeys.push_back(key);
}
Motion::Checkpoint Motion::AdvanceUndoable(double seconds) {
	Checkpoint saved;
	saved.playing = playing; saved.changedKeys = changedKeys; saved.completed = completed;
	saved.revision = revision; saved.clock = clock; saved.sampledClock = sampledClock;
	saved.changedAll = changedAll; saved.sampled = sampled;
	// A sample writes only the values its playing tracks own.
	for (const auto& [key,item] : playing) saved.previous.emplace(key,Values().at(key));
	Advance(seconds);
	return saved;
}
void Motion::Rollback(Checkpoint&& saved) {
	if (!saved.previous.empty()) {
		auto& live = Writable();
		for (auto& [key,value] : saved.previous) live[key] = std::move(value);
	}
	playing = std::move(saved.playing); changedKeys = std::move(saved.changedKeys); completed = std::move(saved.completed);
	revision = saved.revision; clock = saved.clock; sampledClock = saved.sampledClock;
	changedAll = saved.changedAll; sampled = saved.sampled;
}
bool Motion::TakeChanged(std::vector<PropertyKey>& keys) {
	keys.clear();
	const bool partial = !changedAll;
	if (partial) keys.swap(changedKeys);
	changedKeys.clear(); changedAll = false;
	return partial;
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
	if (sampled && sampledClock == clock) return;
	sampled = true; sampledClock = clock;
	if (!playing.empty()) Touch();
	std::vector<std::string> finished;
	for (auto it = playing.begin(); it != playing.end();) {
		auto& item = it->second;
		const double elapsedMs = std::max(0.0,((item.paused ? item.pauseAt : clock)-item.start)*1000);
		const bool done = item.iterations != 0 && elapsedMs >= item.durationMs*item.iterations;
		if (elapsedMs >= item.durationMs) item.track.keys.front().value = item.repeatStart;
		Writable()[it->first] = Sample(item.track,done ? item.durationMs : std::fmod(elapsedMs,item.durationMs));
		Changed(it->first);
		if (done) { finished.push_back(item.owner); it = playing.erase(it); } else ++it;
	}
	for (const auto& owner : finished) if (!IsPlaying(owner)) NoteCompleted(owner);
}
void Motion::NoteCompleted(const std::string& id) {
	const auto found = timelines->find(id);
	if (found == timelines->end() || found->second->complete.empty()) return;
	if (std::find(completed.begin(),completed.end(),id) == completed.end()) completed.push_back(id);
}
std::vector<std::string> Motion::TakeCompleted() {
	std::vector<std::string> result;
	result.swap(completed);
	return result;
}
bool Motion::Play(const std::string& id, double seconds) {
	const auto found = timelines->find(id);
	if (found == timelines->end()) return false;
	Advance(seconds);
	const auto& timeline = *found->second;
	for (const auto& track : timeline.tracks) {
		const PropertyKey key{track.node,track.property};
		Playing item{id,track,track.keys.front().value,clock,timeline.durationMs,0,timeline.iterations,false,timeline.essential};
		// Retarget from the value sampled at this exact clock, not the previous
		// frame or the authored beginning. The newest play owns this property.
		item.track.keys.front().value = Values().at(key);
		if (reducedMotion && !item.essential) {
			item.iterations = 1;
			if (track.property != "opacity") {
				Writable()[key] = track.keys.back().value;
				Changed(key);
				playing.erase(key);
				continue;
			}
			item.durationMs = std::min(80.0,item.durationMs);
			item.reduced = true;
			// Reduced motion is one bounded opacity interpolation. Discard
			// stagger/key oscillations as well as decorative spatial motion.
			item.track.keys = {{0,Values().at(key),{}},{item.durationMs,track.keys.back().value,{}}};
		}
		playing[key] = std::move(item);
	}
	// Reduced motion can jump every track to its end: the timeline is complete.
	if (!IsPlaying(id)) NoteCompleted(id);
	Touch();
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
		// A value already at its base needs no write (nor a copy of shared values).
		if (policy == CancelPolicy::RestoreBase && !SameValue(Values().at(it->first),base->at(it->first))) {
			Writable()[it->first] = base->at(it->first); Changed(it->first);
		}
		it = playing.erase(it);
		Touch();
	}
}
void Motion::SetReducedMotion(bool enabled, double seconds) {
	Advance(seconds);
	if (reducedMotion == enabled) return;
	reducedMotion = enabled;
	Touch();
	if (!enabled) return; // Never resurrect cancelled decorative movement.
	std::vector<std::string> jumped;
	for (auto it = playing.begin(); it != playing.end();) {
		auto& item = it->second;
		if (item.essential) { ++it; continue; }
		if (it->first.second != "opacity") {
			Writable()[it->first] = item.track.keys.back().value;
			Changed(it->first);
			jumped.push_back(item.owner);
			it = playing.erase(it); continue;
		}
		const double remaining = item.durationMs-std::fmod(std::max(0.0,((item.paused ? item.pauseAt : clock)-item.start)*1000),item.durationMs);
		item.durationMs = std::min(80.0,remaining);
		item.track.keys = {{0,Values().at(it->first),{}},{item.durationMs,item.track.keys.back().value,{}}};
		item.start = clock; item.pauseAt = clock; item.iterations = 1;
		item.reduced = true;
		++it;
	}
	for (const auto& owner : jumped) if (!IsPlaying(owner)) NoteCompleted(owner);
}
bool Motion::IsPlaying(const std::string& id) const {
	for (const auto& [key,item] : playing) if (item.owner == id) return true;
	return false;
}
PropertyValues Motion::Scrub(const std::string& id, double milliseconds) const {
	PropertyValues result;
	auto found = timelines->find(id);
	if (found == timelines->end() || !std::isfinite(milliseconds)) return result;
	for (const auto& track : found->second->tracks) result[{track.node,track.property}] = Sample(track,std::clamp(milliseconds,0.0,found->second->durationMs));
	return result;
}
MotionSnapshot Motion::Capture(double seconds) const {
	Motion candidate = *this;
	candidate.Advance(seconds);
	MotionSnapshot result;
	result.reducedMotion = candidate.reducedMotion;
	// Only a written entry can differ from its authored value.
	for (const auto& [key,value] : candidate.Values().Written()) {
		const auto& original = base->at(key);
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
bool Motion::WriteValues(const PropertyValues& changes, std::string& error) {
	error.clear();
	for (const auto& [key,value] : changes) {
		const auto original = base->find(key);
		if (original == base->end() || value.type != original->second.type || value.unit != original->second.unit || !ValidProperty(key.second,value)) {
			error = "Invalid presentation property '"+key.first+"."+key.second+"'"; return false;
		}
	}
	auto& live = Writable();
	for (const auto& [key,value] : changes) { live[key] = value; Changed(key); }
	sampled = false; // A playing track writes again on its next sample.
	Touch();
	return true;
}
bool Motion::Restore(const MotionSnapshot& snapshot, double seconds, std::string& error, bool allowStaticValues) {
	error.clear();
	auto reject = [&](const char* message) { error = message; return false; };
	if (!std::isfinite(seconds) || seconds < 0) return reject("Invalid restored presentation time");
	Motion candidate = *this;
	// Restore never replays a program: completions of the replaced playback go.
	candidate.values = std::make_shared<PropertyValues>(); candidate.playing.clear(); candidate.completed.clear(); candidate.clock = seconds;
	candidate.sampled = false;
	candidate.reducedMotion = snapshot.reducedMotion;
	std::set<PropertyKey> animated;
	for (const auto& [id,timeline] : *timelines) for (const auto& track : timeline->tracks) animated.emplace(track.node,track.property);
	for (const auto& [key,value] : snapshot.values) {
		const auto original = base->find(key);
		if ((!allowStaticValues && !animated.contains(key)) || original == base->end() ||
			value.type != original->second.type || value.unit != original->second.unit || !ValidProperty(key.second,value))
			return reject("Invalid restored presentation property");
		(*candidate.values)[key] = value;
	}
	for (const auto& saved : snapshot.playing) {
		const auto timeline = timelines->find(saved.owner);
		if (timeline == timelines->end()) return reject("Unknown restored timeline");
		const auto& authored = *timeline->second;
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
	changedKeys.clear(); changedAll = true;
	Touch();
	return true;
}
} // namespace openq4::ui
