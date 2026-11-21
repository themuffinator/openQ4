// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "Document.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>

namespace openq4::ui {

using PropertyKey = std::pair<std::string, std::string>;
using PropertyValues = std::map<PropertyKey, Value>;
enum class CancelPolicy { Hold, RestoreBase };

// openQ4: a motion's live values, read as one map: the authored values with
// the entries writes have replaced over them. A write only ever replaces an
// authored value, so both maps hold their keys in the same order; a lookup
// takes a written entry before the authored one, and a walk visits every
// authored key once, in order, with its live value. A document's first
// animated sample therefore writes the few values it animates instead of
// copying all of its authored values first.
class MotionValues {
public:
	using value_type = PropertyValues::value_type;
	class const_iterator {
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = PropertyValues::value_type;
		using difference_type = std::ptrdiff_t;
		using pointer = const value_type*;
		using reference = const value_type&;
		const_iterator() = default;
		reference operator*() const { return written ? *over : *base; }
		pointer operator->() const { return &**this; }
		const_iterator& operator++() { if (written) ++over; ++base; Settle(); return *this; }
		const_iterator operator++(int) { auto copy = *this; ++*this; return copy; }
		bool operator==(const const_iterator& other) const { return base == other.base; }
	private:
		friend class MotionValues;
		const_iterator(PropertyValues::const_iterator authored, PropertyValues::const_iterator authoredEnd,
			PropertyValues::const_iterator replaced, PropertyValues::const_iterator replacedEnd)
			: base(authored), baseEnd(authoredEnd), over(replaced), overEnd(replacedEnd) { Settle(); }
		void Settle() { written = base != baseEnd && over != overEnd && over->first == base->first; }
		PropertyValues::const_iterator base, baseEnd, over, overEnd;
		bool written = false;
	};
	using iterator = const_iterator;
	MotionValues(const PropertyValues& authored, const PropertyValues& written) : base(&authored), over(&written) {}
	const_iterator begin() const { return {base->begin(),base->end(),over->begin(),over->end()}; }
	const_iterator end() const { return {base->end(),base->end(),over->end(),over->end()}; }
	const_iterator find(const PropertyKey& key) const {
		const auto authored = base->find(key);
		if (authored == base->end()) return end();
		return {authored,base->end(),over->lower_bound(key),over->end()};
	}
	bool contains(const PropertyKey& key) const { return base->contains(key); }
	const Value& at(const PropertyKey& key) const {
		const auto found = find(key);
		if (found == end()) throw std::out_of_range("Missing motion value");
		return found->second;
	}
	std::size_t size() const { return base->size(); }
	bool empty() const { return base->empty(); }
	// The entries writes replaced (a subset of the keys, each with its live value).
	const PropertyValues& Written() const { return *over; }
private:
	const PropertyValues* base;
	const PropertyValues* over;
};
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
	// Keeps a copy of the model's timelines and authored values.
	void Reset(const DocumentModel& model);
	// References the shared model's timelines and reads its authored values
	// once: the model outlives this motion and its copies.
	void Reset(std::shared_ptr<const DocumentModel> model);
	bool Play(const std::string& id, double seconds);
	void Advance(double seconds);
	void Pause(const std::string& id, double seconds);
	void Resume(const std::string& id, double seconds);
	void Cancel(const std::string& id, CancelPolicy policy, double seconds);
	void SetReducedMotion(bool enabled, double seconds);
	bool ReducedMotion() const { return reducedMotion; }
	// The live values: a view that is valid until this motion next changes.
	MotionValues Values() const;
	// The authored values, in Values()' key order.
	const PropertyValues& Base() const { return *base; }
	bool IsPlaying(const std::string& id) const;
	// Pure authored-time sampling for editor scrubbing. Does not mutate playback,
	// acquire ownership, or dispatch any future action/event notifications.
	PropertyValues Scrub(const std::string& id, double milliseconds) const;
	MotionSnapshot Capture(double seconds) const;
	bool Restore(const MotionSnapshot& snapshot, double seconds, std::string& error, bool allowStaticValues = false);
	// External presentation writes change current values without cancelling any
	// track. An existing animation can write again on its next sample.
	bool WriteValues(const PropertyValues& changes, std::string& error);
	// Timelines with a completion program whose playback reached its end since
	// the last call, in completion order: the last owned track finished while
	// advancing, or reduced motion jumped every track at once. Cancellation,
	// replay and losing every track to another timeline never complete.
	std::vector<std::string> TakeCompleted();
	// A stamp that changes with every write that can change Values(): a
	// sample, a play, a cancel, a reduced-motion jump, an external write, a
	// restore or a reset. Stamps are unique across copies, so an unchanged
	// stamp means the values are the ones read when it was taken.
	std::uint64_t Revision() const { return revision; }
	// The keys whose values a write may have changed since the last call, or
	// false when every value may have (a reset, a restore, or more changes
	// than are tracked): the caller then re-reads all of them.
	bool TakeChanged(std::vector<PropertyKey>& keys);
	// An event's entry samples its time in place, remembering everything the
	// sample changed, so a rejected event puts the motion back exactly as it
	// was (values, clock and stamp) while its transactional copies share the
	// sampled values instead of duplicating them.
	struct Checkpoint;
	Checkpoint AdvanceUndoable(double seconds);
	void Rollback(Checkpoint&& checkpoint);
private:
	void Touch();
	std::uint64_t revision = 0;
	void Changed(const PropertyKey& key);
	std::vector<PropertyKey> changedKeys;
	bool changedAll = true;
	void NoteCompleted(const std::string& id);
	std::vector<std::string> completed;
	struct Playing {
		std::string owner;
		Track track;
		Value repeatStart;
		double start = 0, durationMs = 0, pauseAt = 0;
		unsigned iterations = 1;
		bool paused = false, essential = false, reduced = false;
	};
	static Value Sample(const Track& track, double milliseconds);
	// The document's timelines and authored values are fixed after Reset and
	// shared by copies: a transactional copy duplicates only live values. The
	// timelines are the model's, which `model` keeps.
	std::shared_ptr<const DocumentModel> model;
	std::shared_ptr<const std::map<std::string, const Timeline*>> timelines = std::make_shared<const std::map<std::string, const Timeline*>>();
	std::shared_ptr<const PropertyValues> base = std::make_shared<const PropertyValues>();
	// The values writes replaced over the authored ones (see MotionValues),
	// shared by copies until one of them writes: an event's transactional copy
	// clones only these, and a reset starts with none, so no write ever copies
	// a document's thousands of authored values. Null reads as none.
	std::shared_ptr<PropertyValues> values;
	PropertyValues& Writable();
	std::map<PropertyKey, Playing> playing;
	double clock = 0;
	bool reducedMotion = false;
	// Advance samples every playing track. Sampling again at the same clock
	// reproduces the same values (a play, pause, resume or cancel there starts
	// each track at its current value), so a burst of plays at one clock
	// samples once; only values written from outside need a fresh sample.
	bool sampled = false;
	double sampledClock = 0;
};

struct Motion::Checkpoint {
	PropertyValues previous; // the values the sample overwrote
	std::map<PropertyKey, Playing> playing;
	std::vector<PropertyKey> changedKeys;
	std::vector<std::string> completed;
	std::uint64_t revision = 0;
	double clock = 0, sampledClock = 0;
	bool changedAll = true, sampled = false;
};

} // namespace openq4::ui
