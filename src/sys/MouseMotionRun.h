// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cmath>
#include <cstdint>

namespace openq4 {

// One run of mouse motion reports, joined into a single report (HF-09).
//
// SDL queues one motion event for every report a mouse sends: on Windows its
// raw input thread does so at the mouse's full polling rate, up to 8000 a
// second, and the X11, Wayland and Cocoa drivers do the same. The platform
// layer turned each into an SE_MOUSE event and two mouse-poll entries, so an
// 8 kHz mouse filled the 256-entry event queue in any frame longer than 32 ms
// and the 512-entry mouse ring in any frame longer than 64 ms. The queue then
// evicted its oldest entries, key presses among them, and printed a line for
// every event it dropped; the ring dropped motion.
//
// Reports queued one straight after another carry no order anything uses:
// the usercmd sums relative motion over its tick, a menu cursor follows the
// summed steps, and the retained interface and the routed menu cursor take the
// latest position. A run therefore joins while every report is finite, comes
// from the same window and device, and keeps each axis's summed motion within
// MOUSE_MOTION_RUN_MAX_COUNTS, below the per-event clamp of the platform layer
// (32767 counts), so joining never shortens what the mouse moved. The next
// report then starts a run of its own. Any other event ends a run, so presses,
// releases, wheel steps and focus changes keep their place among the motion.
// It holds no SDL types, so the rule is tested without SDL
// (tools/tests/native/MouseMotionRunTest.cpp).
static const float MOUSE_MOTION_RUN_MAX_COUNTS = 16384.0f;
static const int MOUSE_MOTION_RUN_MAX_REPORTS = 4096;

struct MouseMotionReport {
	std::uint32_t window = 0;
	std::uint32_t device = 0;
	std::uint64_t timestamp = 0;
	std::uint32_t buttons = 0;
	float x = 0.0f;			// position in the window
	float y = 0.0f;
	float xrel = 0.0f;		// motion since the report before
	float yrel = 0.0f;
};

inline bool MouseMotionReportFinite( const MouseMotionReport &report ) {
	return std::isfinite( report.x ) && std::isfinite( report.y ) &&
		std::isfinite( report.xrel ) && std::isfinite( report.yrel );
}

// Whether `next`, queued straight after the run's `reports` reports, joins it.
inline bool MouseMotionRunAccepts( const MouseMotionReport &run, int reports, const MouseMotionReport &next ) {
	if ( reports < 1 || reports >= MOUSE_MOTION_RUN_MAX_REPORTS ) {
		return false;
	}
	if ( next.window != run.window || next.device != run.device ) {
		return false;
	}
	if ( !MouseMotionReportFinite( run ) || !MouseMotionReportFinite( next ) ) {
		return false;
	}
	return std::fabs( run.xrel + next.xrel ) <= MOUSE_MOTION_RUN_MAX_COUNTS &&
		std::fabs( run.yrel + next.yrel ) <= MOUSE_MOTION_RUN_MAX_COUNTS;
}

// The run now ends with `next`: the motion adds up; the position, the time
// and the held buttons are the latest report's.
inline void MouseMotionRunJoin( MouseMotionReport &run, const MouseMotionReport &next ) {
	run.xrel += next.xrel;
	run.yrel += next.yrel;
	run.x = next.x;
	run.y = next.y;
	run.timestamp = next.timestamp;
	run.buttons = next.buttons;
}

} // namespace openq4
