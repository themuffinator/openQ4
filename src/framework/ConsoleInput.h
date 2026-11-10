// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __CONSOLE_INPUT_H__
#define __CONSOLE_INPUT_H__

#include <cmath>

/*
===============================================================================

	Console scrolling, caret and command-history rules.

	The pull-down console animates its scrollback, blinks its caret and walks
	its command history from these rules. They take plain numbers and strings
	and use no idlib types, so tools/tests/native/ConsoleInputTest.cpp drives
	this same code, and tools/tests/console_input_contract.py pins that
	Console.cpp is wired to it.

===============================================================================
*/

namespace oq4console {

// How fast the smoothed scrollback row closes on the row asked for, as the
// fraction of the remaining distance covered per second. The animation is a
// cue for the direction of a jump, not a queue: whatever the distance, it has
// to be over almost as soon as the key or wheel is let go.
constexpr float SCROLL_CATCHUP_RATE = 20.0f;

// Closer than this to the target and the row snaps onto it.
constexpr float SCROLL_SNAP_DISTANCE = 0.01f;

// Seconds the caret shows, then hides. 16 tics of the stock 60 Hz blink.
constexpr double CARET_BLINK_HALF_PERIOD = 16.0 / 60.0;

/*
====================
SmoothScrollAdvance

Returns the smoothed scrollback row after `seconds`, moving from `line` towards
`target`.

The step is the larger of two: an exponential approach that covers a fixed
fraction of the remaining distance per second, and the constant `floorSpeed`
(rows per second) that finishes the last rows. A constant speed alone leaves the
view travelling for as long as the distance takes, which is half a minute for a
full scrollback at the default 24 rows per second: the view kept scrolling long
after Page Up was released. With the approach the whole buffer settles in under
half a second, and a wheel notch in about a tenth.

The result never passes `target`, and depends on elapsed time only, not on how
many frames the time was split into.
====================
*/
inline float SmoothScrollAdvance( float line, float target, float seconds, float floorSpeed ) {
	const float delta = target - line;
	const float distance = std::fabs( delta );

	if ( !( seconds > 0.0f ) ) {
		return line;
	}
	if ( distance < SCROLL_SNAP_DISTANCE ) {
		return target;
	}

	const float approach = distance * ( 1.0f - std::exp( -SCROLL_CATCHUP_RATE * seconds ) );
	const float floorStep = ( floorSpeed > 1.0f ? floorSpeed : 1.0f ) * seconds;
	const float step = approach > floorStep ? approach : floorStep;
	if ( step >= distance ) {
		return target;
	}
	return delta > 0.0f ? line + step : line - step;
}

/*
====================
HoverAdvance

Moves a 0..1 hover fraction towards 1 while `hot` and towards 0 otherwise, at
`unitsPerSecond`, so a scrollbar grows over the same time at any frame rate.
====================
*/
inline float HoverAdvance( float hover, bool hot, float seconds, float unitsPerSecond ) {
	if ( !( seconds > 0.0f ) ) {
		return hover;
	}
	const float step = unitsPerSecond * seconds;
	if ( hot ) {
		hover += step;
		return hover > 1.0f ? 1.0f : hover;
	}
	hover -= step;
	return hover < 0.0f ? 0.0f : hover;
}

/*
====================
CaretVisible

The caret blinks on the wall clock, and the blink restarts in its visible half
whenever the edit line or the caret changes, so the caret is never hidden at
the moment it moves.
====================
*/
inline bool CaretVisible( double secondsSinceActivity ) {
	if ( !( secondsSinceActivity > 0.0 ) ) {
		return true;
	}
	return std::fmod( secondsSinceActivity, CARET_BLINK_HALF_PERIOD * 2.0 ) < CARET_BLINK_HALF_PERIOD;
}

/*
====================
HistoryLineIsBlank
====================
*/
inline bool HistoryLineIsBlank( const char *line ) {
	if ( line == nullptr ) {
		return true;
	}
	for ( ; *line != '\0'; ++line ) {
		if ( static_cast<unsigned char>( *line ) > ' ' ) {
			return false;
		}
	}
	return true;
}

/*
====================
ShouldRecordHistoryLine

A submitted line joins the command history unless it is blank or repeats the
entry before it. `previous` is the newest entry, or NULL when there is none.
Recording those made the Up key step through empty lines and through the same
command once per time it was run.
====================
*/
inline bool ShouldRecordHistoryLine( const char *line, const char *previous ) {
	if ( HistoryLineIsBlank( line ) ) {
		return false;
	}
	if ( previous == nullptr ) {
		return true;
	}
	const char *a = line;
	const char *b = previous;
	while ( *a != '\0' && *a == *b ) {
		++a;
		++b;
	}
	return *a != *b;
}

/*
====================
HistoryOlder / HistoryNewer

History lines are numbered from 0 and never reused; `next` is the number the
next submitted line will take, and is also where the line being typed lives.
Only the newest `capacity` lines are kept.

HistoryOlder returns the line the Up key moves to, or `line` itself when there
is nothing older. HistoryNewer returns the line the Down key moves to, at most
`next`: the caller shows its saved draft there, not a history slot, because
slot `next` of the ring still holds the oldest command.
====================
*/
inline int HistoryOlder( int line, int next, int capacity ) {
	if ( line > 0 && next - line < capacity ) {
		return line - 1;
	}
	return line;
}

inline int HistoryNewer( int line, int next ) {
	return line < next ? line + 1 : next;
}

} // namespace oq4console

#endif /* !__CONSOLE_INPUT_H__ */
