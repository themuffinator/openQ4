// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace openq4 {

// Observes loss/explicit invalidation of an event stream, including an empty
// flush after the final native record was removed. This is not a queue lock,
// event identity, native provenance, or permission to replay a recorded token.
class EventQueueContinuity {
public:
	explicit EventQueueContinuity(std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)())
		: maximum(maximum), token(maximum ? 1 : 0) {}
	std::uint64_t Token() const { return token.load(); }
	bool Matches(std::uint64_t expected) const { return expected != 0 && expected == Token(); }
	void Invalidate() {
		auto current = token.load();
		while (current != 0 && !token.compare_exchange_weak(current, current == maximum ? 0 : current + 1)) {}
	}
private:
	const std::uint64_t maximum;
	std::atomic<std::uint64_t> token;
};

} // namespace openq4

// One engine-process stream spans both the platform and pushed-event queues.
// Capture before collection; compare again before each native mutation and ACK.
// A changed token requires retiring native delivery and an explicit fresh owner
// reconciliation. Zero means exhausted for this process and is never usable.
std::uint64_t Sys_EventQueueToken();
void Sys_InvalidateEventQueue();
