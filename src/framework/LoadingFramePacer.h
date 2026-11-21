// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <algorithm>
#include <cmath>

namespace openq4 {

// Paces the frames presented while blocking work owns the main thread: the
// startup initialization screen and level loading screens. The work offers a
// frame at checkpoints; this decides which offers present.
//
// Frames are scheduled on a deadline cadence, start to start, at the display's
// refresh rate or the largest even divisor of it that is at most MaxRateHz
// (144 Hz presents 72, 120 Hz presents 60), so animation advances by equal
// steps and lands on the same refresh phase. A lower presentation cap is
// honoured. The cadence slows by whole refresh periods only when the measured
// cost of a frame would take more than MaxPresentationShare of wall time, which
// bounds what presentation can take from the work on any device; below
// MinRateHz the interval follows the cost instead. The cost is the median of
// the last CostWindow frames, so one slow present (a cold driver path, a
// blocked swap) never slows the cadence. It speeds up again only once
// the cost fits the faster cadence with a margin (SpeedUpMargin), so a cost
// near a boundary cannot make the cadence alternate between two rates.
//
// A frame that starts late keeps the cadence when it is less than a quarter
// period late, and otherwise starts a new one: late checkpoints never produce
// catch-up bursts. A frame presented ahead of the cadence (a startup stage
// change shows its text at once) leaves it unchanged.
//
// Times are milliseconds on one monotonic clock.
class LoadingFramePacer {
public:
	static constexpr double MaxRateHz = 90.0;
	static constexpr double MinRateHz = 20.0;
	static constexpr double MaxPresentationShare = 0.4;
	static constexpr double SpeedUpMargin = 0.9;
	static constexpr double DefaultRefreshHz = 60.0;

	// The display's refresh (<= 0 when unknown) and the presentation cap the
	// frame limiter paces to (<= 0 when uncapped).
	void Configure( double refreshHz, int presentationCap ) {
		refresh = refreshHz > 1.0 ? refreshHz : DefaultRefreshHz;
		cap = presentationCap;
		interval = Ladder( MaxPresentationShare );
	}
	// A new blocking period: the next offer presents at once, and the cost of
	// the previous period's frames is forgotten.
	void Reset() {
		started = false;
		cost = 0;
		frames = 0;
		costCount = 0;
		interval = Ladder( MaxPresentationShare );
	}
	bool Due( double now ) const {
		return !started || now >= next - Slack;
	}
	// A frame was submitted at start and its present returned at end.
	void Presented( double start, double end ) {
		const double spent = ( std::max )( 0.0, end - start );
		// The first frame of a period pays for one-time work: building the
		// screen's view and uploading its pictures. It must not slow the rest.
		if ( frames++ > 0 ) {
			costs[ costCount++ % CostWindow ] = spent;
			cost = MedianCost();
			const double slower = Ladder( MaxPresentationShare );
			const double faster = Ladder( MaxPresentationShare * SpeedUpMargin );
			if ( slower > interval + Epsilon ) {
				interval = slower;
			} else if ( faster < interval - Epsilon ) {
				interval = faster;
			}
		}
		if ( !started ) {
			next = start + interval;
		} else if ( start < next - Slack ) {
			// Ahead of the cadence: the cadence is unchanged, so no frame is
			// skipped after this one.
		} else if ( start - next < interval * 0.25 ) {
			next += interval;
		} else {
			next = start + interval;
		}
		// Whatever the cadence, the work always gets time between frames.
		next = ( std::max )( next, end + interval * MinWorkShare );
		started = true;
	}
	double Interval() const { return interval; }
	double Cost() const { return cost; }

private:
	static constexpr double Slack = 0.5;
	static constexpr double MinWorkShare = 0.4;
	static constexpr double Epsilon = 1e-6;
	static constexpr int CostWindow = 5;
	static constexpr int MinCostSamples = 3;

	// Until a few frames have been measured the cost is unknown, and a cold
	// first measurement must not set the cadence for the rest of the period.
	double MedianCost() const {
		const int count = ( std::min )( costCount, CostWindow );
		if ( count < MinCostSamples ) {
			return 0;
		}
		double sorted[ CostWindow ];
		std::copy( costs, costs + count, sorted );
		std::sort( sorted, sorted + count );
		return sorted[ count / 2 ];
	}

	// The fastest interval, in whole refresh periods from the display's own
	// rate, whose presentation share at the current cost is within share.
	double Ladder( double share ) const {
		const double period = 1000.0 / refresh;
		const int divisor = ( std::max )( 1, static_cast<int>( std::ceil( refresh / MaxRateHz - Epsilon ) ) );
		double value = period * divisor;
		if ( cap > 0 ) {
			const double capInterval = 1000.0 / cap;
			while ( value < capInterval - Epsilon ) {
				value += period;
			}
		}
		const double slowest = 1000.0 / MinRateHz;
		while ( cost > value * share && value + period <= slowest + Epsilon ) {
			value += period;
		}
		if ( cost > value * share ) {
			value = cost / share;
		}
		return value;
	}

	double refresh = DefaultRefreshHz;
	int cap = 0;
	double cost = 0;
	double costs[ CostWindow ] = {};
	int costCount = 0;
	double next = 0;
	double interval = 1000.0 / DefaultRefreshHz;
	int frames = 0;
	bool started = false;
};

}
