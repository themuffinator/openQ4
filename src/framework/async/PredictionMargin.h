/*
===========================================================================

openQ4 client prediction margin
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#ifndef __PREDICTIONMARGIN_H__
#define __PREDICTIONMARGIN_H__

// How much further than its configured target a network client predicts, so that
// a long frame does not make its commands late.
//
// The client makes its commands once per frame, so a frame of N milliseconds holds
// back the ones that fell due during it by up to N. idAsyncClient steps its target
// one millisecond per snapshot to keep the median of the server's aheadOfServer
// report at net_clientPrediction, one tic by default. That median covers a frame of
// about two tics: the target itself plus the tic the median already spans. A longer
// frame, or a client that runs slower than 30 fps, reaches the server after the
// frame that needed its command, and the server repeats the last command it has: a
// shot fired on that frame takes the old aim.
//
// The margin is the longest recent frame beyond that cover. Each frame longer than
// the cover holds its own length for HOLD_MSEC of client time and then falls one
// millisecond per DECAY_MSEC, and the margin follows the longest of them. A shorter
// long frame keeps only its own length: a single long frame at a join must not hold
// a large margin for as long as milder ones keep coming. A frame the 100 ms clamp of
// idAsyncClient::UpdateTime cut is a hitch, not a tail: covering it would hold every
// command back by most of that clamp for the next ten seconds, for a stall too rare
// to be worth it, and it is left alone. A frame that fits the cover costs nothing,
// so a client whose frames stay under about two tics never gets a margin.
//
// It has no engine dependencies: tools/tests/native/AsyncClientPredictionFixture.cpp
// drives it with the production controller.
class idPredictionMargin {
public:
	static const int HOLD_MSEC = 10000;		// client time a long frame keeps its length
	static const int DECAY_MSEC = 250;		// client time per millisecond of decay after that
	static const int MAX_FRAMES = 8;		// long frames remembered at once

	idPredictionMargin() { Clear(); }

	void Clear() {
		count = 0;
		clock = 0;
		appliedPeakMsec = 0;
	}

	// One client frame. clientTime is the client clock after it, msec the time
	// UpdateTime gave it, clampMsec the most UpdateTime gives and coverMsec the
	// frame length the configured target already covers.
	void Frame( int clientTime, int msec, int clampMsec, int coverMsec ) {
		clock = clientTime;
		if ( msec > coverMsec && msec < clampMsec ) {
			// A remembered frame no longer than this one can never be the longest
			// again: it started no higher and decays sooner.
			while ( count > 0 && frames[count - 1].msec <= msec ) {
				count--;
			}
			if ( count == MAX_FRAMES ) {
				count--;	// the newest, and so shortest, gives way
			}
			frames[count].msec = msec;
			frames[count].heldUntil = clientTime + HOLD_MSEC;
			count++;
		}
		// The oldest is the longest; it goes once it has decayed to the next.
		while ( count > 0 && ( Length( 0 ) <= 0 || ( count > 1 && Length( 0 ) <= Length( 1 ) ) ) ) {
			for ( int i = 1; i < count; i++ ) {
				frames[i - 1] = frames[i];
			}
			count--;
		}
	}

	// The longest recent frame, as decayed.
	int PeakMsec() const {
		return count > 0 ? Length( 0 ) : 0;
	}

	// Milliseconds the client should predict beyond its configured target.
	int Margin( int coverMsec ) const {
		const int peak = PeakMsec();
		return peak > coverMsec ? peak - coverMsec : 0;
	}

	// How much a longer frame grew the margin since the last call. The controller adds
	// it to its target at once, so the next long frame is covered without first waiting
	// for one-millisecond steps; a shrinking margin is left to those steps. Only the
	// peak's growth counts: the cover follows the exact tic ladder, 16 or 17 ms at
	// 60 Hz, and its own swings must not push the target forward.
	int Rise( int coverMsec ) {
		const int before = appliedPeakMsec > coverMsec ? appliedPeakMsec - coverMsec : 0;
		const int after = Margin( coverMsec );
		appliedPeakMsec = PeakMsec();
		return after > before ? after - before : 0;
	}

private:
	struct longFrame_t {
		int msec;			// the frame's length
		int heldUntil;		// client time its length starts to decay
	};

	int Length( int index ) const {
		const int decaying = clock - frames[index].heldUntil;
		return decaying > 0 ? frames[index].msec - decaying / DECAY_MSEC : frames[index].msec;
	}

	longFrame_t	frames[MAX_FRAMES];	// longest and oldest first
	int			count;
	int			clock;				// client time of the last frame
	int			appliedPeakMsec;	// the peak the controller last applied
};

#endif /* !__PREDICTIONMARGIN_H__ */
