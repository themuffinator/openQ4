/*
===========================================================================

openQ4 console input native tests

Copyright (C) 2026 DarkMatter Productions
SPDX-License-Identifier: GPL-3.0-or-later

Drives src/framework/ConsoleInput.h: the scrollback smoothing, the scrollbar
hover, the caret blink and the command-history walk of the pull-down console.
The frame loop and the key handlers around them are modelled here as
idConsoleLocal runs them. tools/tests/console_input_contract.py pins that the
engine is wired to the functions tested here.

===========================================================================
*/

#include "src/framework/ConsoleInput.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace console = oq4console;

static int failures = 0;

static void Expect( const bool condition, const std::string &label ) {
	if ( !condition ) {
		std::fprintf( stderr, "console input failed: %s\n", label.c_str() );
		failures++;
	}
}

static const float SCROLL_FLOOR = 24.0f;		// con_scrollSmoothSpeed's default
static const int SCROLLBACK_LINES = 768;		// CON_MAX_TOTAL_LINES
static const int SCROLL_LINES = 6;				// con_scrollLines' default
static const int COMMAND_HISTORY = 64;

// idConsoleLocal::Draw at a steady frame rate: seconds until the smoothed row
// is on the target, or -1 if it is not there within `limit` seconds.
static float SettleSeconds( float line, const float target, const float hz, const float limit ) {
	const float frame = 1.0f / hz;
	float elapsed = 0.0f;
	while ( elapsed < limit ) {
		if ( line == target ) {
			return elapsed;
		}
		const float before = line;
		line = console::SmoothScrollAdvance( line, target, frame, SCROLL_FLOOR );
		Expect( target >= before ? ( line >= before && line <= target ) : ( line <= before && line >= target ),
			"the smoothed row moves towards the target and never passes it" );
		elapsed += frame;
	}
	return line == target ? elapsed : -1.0f;
}

static void TestSmoothScroll() {
	const float rates[] = { 30.0f, 60.0f, 144.0f, 240.0f, 1000.0f, 4000.0f };

	for ( const float hz : rates ) {
		const std::string at = " at " + std::to_string( static_cast<int>( hz ) ) + " Hz";

		// Issue #214: Page Up held on a full log. Wherever the target has got
		// to when the key comes up, the view is on it within half a second.
		const float whole = SettleSeconds( static_cast<float>( SCROLLBACK_LINES ), 0.0f, hz, 5.0f );
		Expect( whole >= 0.0f && whole <= 0.5f, "a jump across the whole scrollback settles within 0.5 s" + at );

		const float back = SettleSeconds( 0.0f, static_cast<float>( SCROLLBACK_LINES ), hz, 5.0f );
		Expect( back >= 0.0f && back <= 0.5f, "the jump back down settles within 0.5 s" + at );

		const float notch = SettleSeconds( 100.0f, 100.0f - SCROLL_LINES, hz, 5.0f );
		Expect( notch >= 0.0f && notch <= 0.2f, "one wheel notch settles within 0.2 s" + at );

		const float single = SettleSeconds( 100.0f, 101.0f, hz, 5.0f );
		Expect( single >= 0.0f && single <= 0.1f, "a one-line step settles within 0.1 s" + at );
	}

	// The constant speed this replaced: 768 rows at 24 rows a second.
	Expect( SCROLLBACK_LINES / SCROLL_FLOOR > 30.0f, "the constant speed alone took over 30 s for the same jump" );

	// Elapsed time decides the position, not how many frames it was split into.
	float coarse = 500.0f;
	float fine = 500.0f;
	for ( int i = 0; i < 6; ++i ) {
		coarse = console::SmoothScrollAdvance( coarse, 0.0f, 1.0f / 60.0f, SCROLL_FLOOR );
	}
	for ( int i = 0; i < 24; ++i ) {
		fine = console::SmoothScrollAdvance( fine, 0.0f, 1.0f / 240.0f, SCROLL_FLOOR );
	}
	Expect( std::fabs( coarse - fine ) < 0.5f, "0.1 s of scrolling lands on the same row at 60 Hz and 240 Hz" );

	// No time, no movement; no distance, no movement.
	Expect( console::SmoothScrollAdvance( 40.0f, 10.0f, 0.0f, SCROLL_FLOOR ) == 40.0f, "a zero-length frame holds the row" );
	Expect( console::SmoothScrollAdvance( 40.0f, 10.0f, -1.0f, SCROLL_FLOOR ) == 40.0f, "a negative frame holds the row" );
	Expect( console::SmoothScrollAdvance( 10.0f, 10.0f, 0.016f, SCROLL_FLOOR ) == 10.0f, "a row on its target stays there" );
	Expect( console::SmoothScrollAdvance( 10.004f, 10.0f, 0.000001f, SCROLL_FLOOR ) == 10.0f, "a row within the snap distance lands" );

	// The floor finishes short moves at the configured speed, and a floor
	// below one row a second is raised to it.
	const float slow = console::SmoothScrollAdvance( 10.0f, 10.5f, 0.001f, SCROLL_FLOOR );
	Expect( std::fabs( slow - 10.024f ) < 0.0005f, "a short move travels at the floor speed" );
	const float floorless = console::SmoothScrollAdvance( 10.0f, 10.04f, 0.01f, 0.0f );
	Expect( floorless > 10.0f && floorless <= 10.04f, "a zero floor speed still moves" );

	// A long frame, as after a hitch, lands on the target instead of passing it.
	Expect( console::SmoothScrollAdvance( 0.0f, 300.0f, 10.0f, SCROLL_FLOOR ) == 300.0f, "a long frame lands on the target" );
}

// The key path of issue #214 as it was and as it is, for half a second of
// Page Up on a full log with the system's key repeat starting after 500 ms.
static void TestHeldPageKey() {
	const int holdMsec = 500;
	const int hz = 240;
	const int engineRepeatDelayMsec = 200;		// the removed idConsoleLocal::Scroll

	// Before: the key-down event, then one step per drawn frame from 200 ms on.
	const int framesRepeating = ( holdMsec - engineRepeatDelayMsec ) * hz / 1000;
	const int linesBefore = SCROLL_LINES * ( 1 + framesRepeating );
	Expect( linesBefore > 400, "the per-frame repeat asked for hundreds of lines in a half-second hold" );
	Expect( linesBefore / SCROLL_FLOOR > 15.0f, "which the constant-speed smoothing then took over 15 s to show" );

	// Now: one step per key-down event, and none has repeated yet.
	const int linesNow = SCROLL_LINES;
	const float settle = SettleSeconds( 700.0f, 700.0f - linesNow, static_cast<float>( hz ), 5.0f );
	Expect( settle >= 0.0f && settle <= 0.2f, "one step per key event is on screen within 0.2 s of the press" );
}

static void TestHover() {
	const float speed = 12.0f;	// CON_SCROLLBAR_LERP_SPEED

	for ( const float hz : { 30.0f, 60.0f, 240.0f, 1000.0f } ) {
		float hover = 0.0f;
		float elapsed = 0.0f;
		while ( hover < 1.0f && elapsed < 1.0f ) {
			hover = console::HoverAdvance( hover, true, 1.0f / hz, speed );
			elapsed += 1.0f / hz;
		}
		Expect( hover == 1.0f && elapsed >= 1.0f / speed - 0.001f && elapsed <= 1.0f / speed + 1.0f / hz + 0.001f,
			"the scrollbar grows in the same time at " + std::to_string( static_cast<int>( hz ) ) + " Hz" );
	}

	Expect( console::HoverAdvance( 0.5f, false, 10.0f, speed ) == 0.0f, "a cold scrollbar shrinks to nothing and stops" );
	Expect( console::HoverAdvance( 0.5f, true, 10.0f, speed ) == 1.0f, "a hot scrollbar grows to full and stops" );
	Expect( console::HoverAdvance( 0.5f, true, 0.0f, speed ) == 0.5f, "a zero-length frame holds the hover" );
}

static void TestCaret() {
	Expect( console::CaretVisible( 0.0 ), "the caret shows the moment the line changes" );
	Expect( console::CaretVisible( -1.0 ), "a negative clock shows the caret" );
	Expect( console::CaretVisible( console::CARET_BLINK_HALF_PERIOD * 0.5 ), "the caret shows through the first half period" );
	Expect( !console::CaretVisible( console::CARET_BLINK_HALF_PERIOD * 1.5 ), "the caret hides through the second" );
	Expect( console::CaretVisible( console::CARET_BLINK_HALF_PERIOD * 2.5 ), "and shows again in the third" );
	Expect( !console::CaretVisible( console::CARET_BLINK_HALF_PERIOD * 1001.5 ), "the blink keeps its phase over a long idle" );
	Expect( std::fabs( console::CARET_BLINK_HALF_PERIOD - 0.2667 ) < 0.001, "the half period is the stock 16 tics" );
}

// idConsoleLocal's history: the ring, the line being browsed and the draft.
struct History {
	std::string	ring[COMMAND_HISTORY];
	int			next = 0;
	int			line = 0;
	std::string	draft;
	std::string	field;

	void Submit() {
		const char *previous = next > 0 ? ring[( next - 1 ) % COMMAND_HISTORY].c_str() : nullptr;
		if ( console::ShouldRecordHistoryLine( field.c_str(), previous ) ) {
			ring[next % COMMAND_HISTORY] = field;
			next++;
		}
		line = next;
		draft.clear();
		field.clear();
	}

	void Recall( const int target ) {
		if ( target == line ) {
			return;
		}
		if ( line == next ) {
			draft = field;
		}
		line = target;
		field = ( line == next ) ? draft : ring[line % COMMAND_HISTORY];
	}

	void Up() { Recall( console::HistoryOlder( line, next, COMMAND_HISTORY ) ); }
	void Down() { Recall( console::HistoryNewer( line, next ) ); }
};

static void TestHistory() {
	Expect( console::HistoryLineIsBlank( nullptr ) && console::HistoryLineIsBlank( "" ) && console::HistoryLineIsBlank( " \t " ),
		"null, empty and whitespace lines are blank" );
	Expect( !console::HistoryLineIsBlank( " map " ), "a command is not blank" );
	Expect( !console::HistoryLineIsBlank( "\xE9" ), "an 8-bit character is not blank" );

	Expect( !console::ShouldRecordHistoryLine( "", "map q4dm1" ), "a blank line is not recorded" );
	Expect( !console::ShouldRecordHistoryLine( "   ", nullptr ), "a whitespace line is not recorded" );
	Expect( console::ShouldRecordHistoryLine( "map q4dm1", nullptr ), "the first command is recorded" );
	Expect( !console::ShouldRecordHistoryLine( "map q4dm1", "map q4dm1" ), "a repeat of the newest entry is not recorded" );
	Expect( console::ShouldRecordHistoryLine( "map q4dm1", "map q4dm2" ), "a different command is recorded" );
	Expect( console::ShouldRecordHistoryLine( "map q4dm1", "map q4dm" ), "a longer command is recorded" );
	Expect( console::ShouldRecordHistoryLine( "map q4dm", "map q4dm1" ), "a shorter command is recorded" );
	Expect( console::ShouldRecordHistoryLine( "Map q4dm1", "map q4dm1" ), "the comparison keeps case" );

	// Empty history: Up used to load blank slot 0 over whatever was typed.
	{
		History history;
		history.field = "listim";
		history.Up();
		Expect( history.field == "listim" && history.line == 0, "Up on an empty history keeps the typed line" );
		history.Down();
		Expect( history.field == "listim", "Down on an empty history keeps the typed line" );
	}

	// Blank lines and repeats.
	{
		History history;
		const char *lines[] = { "listImages", "", "listImages", "   ", "listImages", "vid_restart", "listImages" };
		for ( const char *line : lines ) {
			history.field = line;
			history.Submit();
		}
		Expect( history.next == 3, "blank lines and repeats leave three entries of seven submitted" );
		history.Up();
		Expect( history.field == "listImages", "Up recalls the newest entry" );
		history.Up();
		Expect( history.field == "vid_restart", "then the one before" );
		history.Up();
		Expect( history.field == "listImages", "then the oldest" );
		history.Up();
		Expect( history.field == "listImages" && history.line == 0, "and stops there" );
	}

	// The draft survives a round trip, before and after the ring has wrapped.
	for ( const int submitted : { 3, COMMAND_HISTORY, COMMAND_HISTORY + 9, COMMAND_HISTORY * 3 } ) {
		History history;
		for ( int i = 0; i < submitted; ++i ) {
			history.field = "cmd" + std::to_string( i );
			history.Submit();
		}
		const std::string label = " after " + std::to_string( submitted ) + " commands";

		history.field = "r_sha";
		history.Up();
		Expect( history.field == "cmd" + std::to_string( submitted - 1 ), "Up recalls the newest command" + label );
		history.Down();
		// Slot next of the ring holds the oldest command once it has wrapped,
		// and that is what Down used to show here.
		Expect( history.field == "r_sha", "Down returns to the typed line" + label );
		history.Down();
		Expect( history.field == "r_sha" && history.line == history.next, "Down at the typed line stays there" + label );

		const int reachable = submitted < COMMAND_HISTORY ? submitted : COMMAND_HISTORY;
		for ( int i = 0; i < reachable + 5; ++i ) {
			history.Up();
		}
		Expect( history.field == "cmd" + std::to_string( submitted - reachable ),
			"Up stops at the oldest command still kept" + label );
		for ( int i = 0; i < reachable + 5; ++i ) {
			history.Down();
		}
		Expect( history.field == "r_sha", "and the typed line is still there on the way back" + label );
	}

	Expect( console::HistoryOlder( 0, 0, COMMAND_HISTORY ) == 0, "nothing is older than line 0" );
	Expect( console::HistoryOlder( 100, 100, COMMAND_HISTORY ) == 99, "the newest entry is one back from the typed line" );
	Expect( console::HistoryOlder( 37, 100, COMMAND_HISTORY ) == 36, "the oldest kept entry is reachable" );
	Expect( console::HistoryOlder( 36, 100, COMMAND_HISTORY ) == 36, "an overwritten entry is not" );
	Expect( console::HistoryNewer( 99, 100 ) == 100 && console::HistoryNewer( 100, 100 ) == 100, "Down stops at the typed line" );
}

int main() {
	TestSmoothScroll();
	TestHeldPageKey();
	TestHover();
	TestCaret();
	TestHistory();

	if ( failures != 0 ) {
		std::fprintf( stderr, "console input: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "console input: ok\n" );
	return 0;
}
