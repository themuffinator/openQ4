// Cases for src/sys/NetworkImpairment.h, shared by CoreSafetyTest.cpp and
// tools/tests/network_impairment_state.py, which also runs them against
// single-reversal copies of the header.

#ifndef OPENQ4_NETWORK_IMPAIRMENT_CASES_H
#define OPENQ4_NETWORK_IMPAIRMENT_CASES_H

#include "src/sys/NetworkImpairment.h"

#include <climits>
#include <cstdio>
#include <vector>

static int impairmentFailures = 0;

static void ExpectImpairment( const bool condition, const char *label ) {
	if ( !condition ) {
		std::fprintf( stderr, "Network impairment: %s\n", label );
		impairmentFailures++;
	}
}

typedef idNetworkImpairment::queue_t<int> impairmentQueue_t;

// Offers count one-byte datagrams numbered from 0, one per spacingMsec, then drains the
// queue a millisecond at a time and returns the payloads in release order.
static std::vector<int> DrainImpairment( impairmentQueue_t &queue, const idNetworkImpairment::settings_t &settings,
		const int count, const int spacingMsec, const int start, std::vector<int> *releaseTimes = nullptr ) {
	std::vector<int> released;
	int offered = 0;
	for ( int step = 0; step < count * spacingMsec + 4000; step++ ) {
		// step is the elapsed time; the clock itself may wrap past INT_MAX.
		const int now = static_cast<int32_t>( static_cast<uint32_t>( start ) + static_cast<uint32_t>( step ) );
		while ( offered < count && step >= offered * spacingMsec ) {
			const unsigned char payload = static_cast<unsigned char>( offered );
			queue.Offer( settings, now, offered, &payload, 1 );
			offered++;
		}
		int address = -1;
		unsigned char buffer[4];
		int size = 0;
		while ( queue.Pop( now, address, buffer, sizeof( buffer ), size ) ) {
			released.push_back( buffer[0] );
			if ( releaseTimes != nullptr ) {
				releaseTimes->push_back( step - address * spacingMsec );
			}
		}
	}
	return released;
}

// Offers each datagram of sizes[] (payload byte = its index) at offerTimes[i], drains
// a millisecond at a time from start, and returns each delivered index with the time
// it was released.
struct impairmentRelease_t {
	int		index;
	int		time;
};

static std::vector<impairmentRelease_t> PaceImpairment( impairmentQueue_t &queue, const idNetworkImpairment::settings_t &settings,
		const std::vector<int> &sizes, const std::vector<int> &offerTimes, const int start, const int durationMsec ) {
	std::vector<impairmentRelease_t> released;
	std::vector<unsigned char> payload( 4096 );
	std::vector<unsigned char> buffer( 4096 );
	size_t offered = 0;
	for ( int step = 0; step <= durationMsec; step++ ) {
		const int now = static_cast<int>( static_cast<unsigned int>( start ) + static_cast<unsigned int>( step ) );
		while ( offered < sizes.size() && offerTimes[offered] <= step ) {
			payload[0] = static_cast<unsigned char>( offered );
			queue.Offer( settings, now, static_cast<int>( offered ), payload.data(), sizes[offered] );
			offered++;
		}
		int address = -1;
		int size = 0;
		while ( queue.Pop( now, address, buffer.data(), static_cast<int>( buffer.size() ), size ) ) {
			impairmentRelease_t release = { buffer[0], step };
			released.push_back( release );
		}
	}
	return released;
}

// Returns how many cases failed.
static int ExerciseNetworkImpairment() {
	using namespace idNetworkImpairment;
	impairmentFailures = 0;

	ExpectImpairment( !Active( Sanitize( 0, 0, 0, 0 ) ), "zero settings are inactive" );
	ExpectImpairment( Active( Sanitize( 0, 0, 1, 0 ) ), "loss alone is active" );
	ExpectImpairment( Active( Sanitize( 0, 0, 0, 0, 5000 ) ), "a rate alone is active" );
	ExpectImpairment( !Active( Sanitize( 0, 0, 0, 0, 0, 100, 8 ) ), "a queue or burst length alone is inactive" );
	const settings_t clamped = Sanitize( -5, 99999, 250, -1 );
	ExpectImpairment( clamped.latencyMsec == 0 && clamped.jitterMsec == MAX_JITTER_MSEC &&
		clamped.dropPercent == 100 && clamped.duplicatePercent == 0 && clamped.rateBytes == 0 &&
		clamped.queueMsec == DEFAULT_QUEUE_MSEC && clamped.burstLength == 0, "settings are clamped" );
	const settings_t link = Sanitize( 0, 0, 0, 0, 1, 0, 999 );
	ExpectImpairment( link.rateBytes == MIN_RATE_BYTES && link.queueMsec == 1 && link.burstLength == MAX_BURST_LENGTH,
		"bottleneck and burst settings are clamped" );

	// A bottleneck paces a burst: 72 bytes plus 28 of headers take 10 ms at 10 KB/s, so the
	// i-th datagram offered at once leaves at ( i + 1 ) * 10 ms, in order, with no loss.
	{
		impairmentQueue_t queue;
		const std::vector<int> sizes( 50, 72 );
		const std::vector<int> times( 50, 0 );
		const std::vector<impairmentRelease_t> out = PaceImpairment( queue, Sanitize( 0, 0, 0, 0, 10000, 1000 ), sizes, times, 0, 1000 );
		bool paced = out.size() == 50;
		for ( size_t i = 0; paced && i < out.size(); i++ ) {
			paced = out[i].index == static_cast<int>( i ) && out[i].time == static_cast<int>( i + 1 ) * 10;
		}
		ExpectImpairment( paced, "the bottleneck sends one datagram after another at its rate" );
		ExpectImpairment( queue.stats.congested == 0 && queue.stats.maxWaitMsec == 490, "a deep enough queue holds the burst" );
		ExpectImpairment( queue.stats.offeredBytes == 50 * 72, "the line counts the payload bytes offered to it" );
	}

	// A full queue drops the tail: with 100 ms of queue only the first ten fit.
	{
		impairmentQueue_t queue;
		const std::vector<int> sizes( 50, 72 );
		const std::vector<int> times( 50, 0 );
		const std::vector<impairmentRelease_t> out = PaceImpairment( queue, Sanitize( 0, 0, 0, 0, 10000, 100 ), sizes, times, 0, 1000 );
		ExpectImpairment( out.size() == 10 && queue.stats.congested == 40 && out.back().index == 9 && out.back().time == 100,
			"a full bottleneck drops what its queue cannot hold" );
	}

	// An idle link takes a datagram bigger than its queue rather than starving it.
	{
		impairmentQueue_t queue;
		const std::vector<int> sizes( 1, 2000 );
		const std::vector<int> times( 1, 0 );
		const std::vector<impairmentRelease_t> out = PaceImpairment( queue, Sanitize( 0, 0, 0, 0, 1000, 10 ), sizes, times, 0, 3000 );
		ExpectImpairment( out.size() == 1 && out[0].time == 2028 && queue.stats.congested == 0,
			"an idle bottleneck carries a datagram larger than its queue" );
	}

	// Once the link has drained, a datagram waits for nothing; latency follows the
	// bottleneck rather than the offer.
	{
		impairmentQueue_t queue;
		std::vector<int> sizes;
		sizes.push_back( 72 );
		sizes.push_back( 72 );
		std::vector<int> times;
		times.push_back( 0 );
		times.push_back( 500 );
		const std::vector<impairmentRelease_t> out = PaceImpairment( queue, Sanitize( 40, 0, 0, 0, 10000, 250 ), sizes, times, 0, 1000 );
		ExpectImpairment( out.size() == 2 && out[0].time == 50 && out[1].time == 550 && queue.stats.maxWaitMsec == 0,
			"a drained bottleneck adds only the datagram's own time, then the latency" );
	}

	// The bottleneck keeps pacing across the millisecond clock wrapping.
	{
		impairmentQueue_t queue;
		const std::vector<int> sizes( 8, 72 );
		const std::vector<int> times( 8, 0 );
		const std::vector<impairmentRelease_t> out = PaceImpairment( queue, Sanitize( 0, 0, 0, 0, 10000, 1000 ), sizes, times, INT_MAX - 30, 200 );
		bool paced = out.size() == 8;
		for ( size_t i = 0; paced && i < out.size(); i++ ) {
			paced = out[i].index == static_cast<int>( i ) && out[i].time == static_cast<int>( i + 1 ) * 10;
		}
		ExpectImpairment( paced, "the bottleneck survives the clock wrapping" );
	}

	// Rate, latency and jitter must compose identically on either side of clock wrap.
	{
		impairmentQueue_t plain, wrapped;
		plain.random.Seed( 123 );
		wrapped.random.Seed( 123 );
		const std::vector<int> sizes( 12, 72 );
		const std::vector<int> times( 12, 0 );
		const settings_t settings = Sanitize( 25, 40, 0, 0, 10000, 1000 );
		const std::vector<impairmentRelease_t> a = PaceImpairment( plain, settings, sizes, times, 0, 300 );
		const std::vector<impairmentRelease_t> b = PaceImpairment( wrapped, settings, sizes, times, INT_MAX - 30, 300 );
		bool same = a.size() == sizes.size() && b.size() == a.size();
		for ( size_t i = 0; same && i < a.size(); i++ ) {
			const int departure = ( a[i].index + 1 ) * 10;
			same = a[i].index == b[i].index && a[i].time == b[i].time &&
				a[i].time >= departure + 25 && a[i].time <= departure + 65;
		}
		ExpectImpairment( same, "bottleneck, latency and jitter compose across clock wrap" );
	}

	// Burst loss keeps its rate but loses datagrams in runs of about burstLength;
	// independent loss at the same rate rarely loses two in a row.
	{
		impairmentQueue_t bursty, independent;
		bursty.random.Seed( 99 );
		independent.random.Seed( 99 );
		DrainImpairment( bursty, Sanitize( 0, 0, 20, 0, 0, 250, 4 ), 20000, 1, 0 );
		DrainImpairment( independent, Sanitize( 0, 0, 20, 0 ), 20000, 1, 0 );
		const double burstyRate = static_cast<double>( bursty.stats.dropped ) / 20000.0;
		const double burstyRun = bursty.stats.bursts ? static_cast<double>( bursty.stats.dropped ) / bursty.stats.bursts : 0.0;
		const double independentRun = independent.stats.bursts ?
			static_cast<double>( independent.stats.dropped ) / independent.stats.bursts : 0.0;
		ExpectImpairment( burstyRate > 0.17 && burstyRate < 0.23, "burst loss keeps the loss rate" );
		ExpectImpairment( burstyRun > 3.2 && burstyRun < 4.8, "burst loss loses about burstLength datagrams in a row" );
		ExpectImpairment( independentRun > 1.0 && independentRun < 1.5, "independent loss loses about one at a time" );
	}

	// With no rate and a burst length of 1 the line behaves exactly as without them.
	{
		impairmentQueue_t plain, extended;
		plain.random.Seed( 2024 );
		extended.random.Seed( 2024 );
		const std::vector<int> a = DrainImpairment( plain, Sanitize( 20, 60, 10, 10 ), 300, 3, 0 );
		const std::vector<int> b = DrainImpairment( extended, Sanitize( 20, 60, 10, 10, 0, 40, 1 ), 300, 3, 0 );
		ExpectImpairment( a == b, "the new settings change nothing while they are off" );
	}

	// Latency alone: every datagram leaves exactly latency later, in order.
	{
		impairmentQueue_t queue;
		std::vector<int> delays;
		const std::vector<int> order = DrainImpairment( queue, Sanitize( 40, 0, 0, 0 ), 50, 5, 1000, &delays );
		bool inOrder = order.size() == 50;
		for ( size_t i = 0; inOrder && i < order.size(); i++ ) {
			inOrder = order[i] == static_cast<int>( i ) && delays[i] == 40;
		}
		ExpectImpairment( inOrder, "fixed latency delivers every datagram 40 ms later, in order" );
		ExpectImpairment( queue.stats.reordered == 0 && queue.stats.released == 50, "fixed latency reorders nothing" );
	}

	// Jitter wider than the spacing reorders, and every delay stays in latency..latency+jitter.
	{
		impairmentQueue_t queue;
		queue.random.Seed( 7 );
		std::vector<int> delays;
		const std::vector<int> order = DrainImpairment( queue, Sanitize( 30, 40, 0, 0 ), 400, 4, 0, &delays );
		bool bounded = order.size() == 400;
		for ( size_t i = 0; bounded && i < delays.size(); i++ ) {
			bounded = delays[i] >= 30 && delays[i] <= 70;
		}
		ExpectImpairment( bounded, "jittered delays stay within latency..latency+jitter" );
		ExpectImpairment( queue.stats.reordered > 0, "jitter wider than the spacing reorders datagrams" );
	}

	// Loss and duplication rates follow their percentages; 100% duplicates everything.
	{
		impairmentQueue_t queue;
		queue.random.Seed( 11 );
		const std::vector<int> order = DrainImpairment( queue, Sanitize( 0, 0, 30, 0 ), 200, 1, 0 );
		const double lost = static_cast<double>( queue.stats.dropped ) / 200.0;
		ExpectImpairment( lost > 0.2 && lost < 0.4 && order.size() == 200 - queue.stats.dropped, "30% loss drops about 30%" );
	}
	{
		impairmentQueue_t queue;
		const std::vector<int> order = DrainImpairment( queue, Sanitize( 0, 0, 100, 0 ), 20, 1, 0 );
		ExpectImpairment( order.empty() && queue.stats.dropped == 20, "100% loss delivers nothing" );
	}
	{
		impairmentQueue_t queue;
		const std::vector<int> order = DrainImpairment( queue, Sanitize( 5, 0, 0, 100 ), 20, 2, 0 );
		ExpectImpairment( order.size() == 40 && queue.stats.duplicated == 20, "100% duplication delivers every datagram twice" );
	}

	// The same seed repeats a run exactly; another seed does not.
	{
		impairmentQueue_t first, second, third;
		first.random.Seed( 1234 );
		second.random.Seed( 1234 );
		third.random.Seed( 4321 );
		const settings_t settings = Sanitize( 20, 60, 10, 10 );
		const std::vector<int> a = DrainImpairment( first, settings, 300, 3, 0 );
		const std::vector<int> b = DrainImpairment( second, settings, 300, 3, 0 );
		const std::vector<int> c = DrainImpairment( third, settings, 300, 3, 0 );
		ExpectImpairment( a == b, "a seed repeats the run" );
		ExpectImpairment( a != c, "another seed changes the run" );
	}

	// A due datagram too large for the caller's buffer is discarded, not returned
	// truncated, and the next due one is still delivered.
	{
		impairmentQueue_t queue;
		const settings_t settings = Sanitize( 0, 0, 0, 0 );
		unsigned char big[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
		unsigned char small = 9;
		queue.Offer( settings, 0, 1, big, sizeof( big ) );
		queue.Offer( settings, 0, 2, &small, 1 );
		int address = 0;
		unsigned char buffer[4];
		int size = 0;
		const bool popped = queue.Pop( 0, address, buffer, sizeof( buffer ), size );
		ExpectImpairment( popped && address == 2 && size == 1 && buffer[0] == 9 && queue.stats.discarded == 1,
			"an oversize due datagram is discarded and the next one delivered" );
		ExpectImpairment( !queue.Pop( 0, address, buffer, sizeof( buffer ), size ) && queue.Empty(), "the queue drains" );
	}

	// Datagrams far beyond the old 1400-byte lag queue cap are carried intact.
	{
		impairmentQueue_t queue;
		std::vector<unsigned char> payload( 9000 );
		for ( size_t i = 0; i < payload.size(); i++ ) {
			payload[i] = static_cast<unsigned char>( i * 7 );
		}
		queue.Offer( Sanitize( 10, 0, 0, 0 ), 0, 3, payload.data(), static_cast<int>( payload.size() ) );
		std::vector<unsigned char> out( payload.size() );
		int address = 0;
		int size = 0;
		ExpectImpairment( !queue.Pop( 9, address, out.data(), static_cast<int>( out.size() ), size ), "not before it is due" );
		ExpectImpairment( queue.Pop( 10, address, out.data(), static_cast<int>( out.size() ), size ) &&
			size == static_cast<int>( payload.size() ) && out == payload, "a 9000-byte datagram is carried intact" );
		ExpectImpairment( queue.Offer( Sanitize( 0, 0, 0, 0 ), 0, 3, payload.data(), MAX_DATAGRAM_BYTES + 1 ) == 0 &&
			queue.stats.overflowed == 1, "a datagram larger than UDP allows is refused" );
	}

	// The millisecond clock wrapping past INT_MAX does not stall or reorder delivery.
	{
		impairmentQueue_t queue;
		std::vector<int> delays;
		const std::vector<int> order = DrainImpairment( queue, Sanitize( 25, 0, 0, 0 ), 40, 2, INT_MAX - 30, &delays );
		bool inOrder = order.size() == 40;
		for ( size_t i = 0; inOrder && i < order.size(); i++ ) {
			inOrder = order[i] == static_cast<int>( i );
		}
		ExpectImpairment( inOrder, "delivery survives the clock wrapping" );
	}

	// A path MTU: settings, then packets counted with each family's headers.
	ExpectImpairment( Active( Sanitize( 0, 0, 0, 0, 0, 250, 0, 1280 ) ), "a path MTU alone is active" );
	ExpectImpairment( Sanitize( 0, 0, 0, 0, 0, 250, 0, 100 ).mtuBytes == MIN_MTU_BYTES &&
		Sanitize( 0, 0, 0, 0, 0, 250, 0, 0, 1 ).mtuDrop == false, "the path MTU is clamped and needs a size to drop" );
	ExpectImpairment( IpPackets( 1280, 1252, false ) == 1 && IpPackets( 1280, 1253, false ) == 2 &&
		IpPackets( 1280, 1232, true ) == 1 && IpPackets( 1280, 1233, true ) == 2 && IpPackets( 0, 60000, false ) == 1,
		"a datagram fits a 1280-byte path up to 1252 bytes over IPv4 and 1232 over IPv6" );
	ExpectImpairment( IpPackets( 1280, 1310, false ) == 2 && IpPackets( 1280, 2504, false ) == 2 &&
		IpPackets( 1280, 2505, false ) == 3 && IpPackets( 1280, 2456, true ) == 2 && IpPackets( 1280, 2457, true ) == 3,
		"IP fragments carry multiples of 8 bytes behind their own headers" );
	ExpectImpairment( WireBytes( 1, 100, false ) == 100 + LINK_OVERHEAD_BYTES && WireBytes( 1, 100, true ) == 148 &&
		WireBytes( 2, 1310, false ) == 1358 && WireBytes( 2, 1310, true ) == 1414, "every IP packet's headers cross the link" );

	// A dropping path refuses only what it cannot carry whole.
	{
		impairmentQueue_t queue;
		const settings_t settings = Sanitize( 0, 0, 0, 0, 0, 250, 0, 1280, 1 );
		std::vector<unsigned char> payload( 1400, 5 );
		ExpectImpairment( queue.Offer( settings, 0, 1, payload.data(), 1252 ) == 1 &&
			queue.Offer( settings, 0, 2, payload.data(), 1253 ) == 0 &&
			queue.Offer( settings, 0, 3, payload.data(), 1232, true ) == 1 &&
			queue.Offer( settings, 0, 4, payload.data(), 1233, true ) == 0, "a dropping path loses each datagram larger than it" );
		ExpectImpairment( queue.stats.oversized == 2 && queue.stats.ipFragments == 0 && queue.stats.dropped == 0 &&
			queue.stats.maxSize == 1253 && queue.Depth() == 2, "dropped oversized datagrams are counted as such" );
	}

	// A fragmenting path carries an oversized datagram intact, but it is lost when any
	// of its fragments is: at 20% loss per packet a two-fragment datagram arrives 64%.
	{
		impairmentQueue_t whole, split;
		whole.random.Seed( 31 );
		split.random.Seed( 31 );
		std::vector<unsigned char> payload( 1310 );
		for ( size_t i = 0; i < payload.size(); i++ ) {
			payload[i] = static_cast<unsigned char>( i * 13 );
		}
		const settings_t mtu = Sanitize( 0, 0, 20, 0, 0, 250, 0, 1280 );
		for ( int i = 0; i < 4000; i++ ) {
			whole.Offer( mtu, 0, i, payload.data(), 1200 );
			split.Offer( mtu, 0, i, payload.data(), 1310 );
		}
		const double wholeRate = static_cast<double>( whole.Depth() ) / 4000.0;
		const double splitRate = static_cast<double>( split.Depth() ) / 4000.0;
		ExpectImpairment( wholeRate > 0.77 && wholeRate < 0.83 && splitRate > 0.61 && splitRate < 0.67,
			"a fragmented datagram is lost when any fragment is" );
		ExpectImpairment( split.stats.oversized == 4000 && split.stats.ipFragments == 8000 && whole.stats.oversized == 0,
			"fragmented datagrams count their IP fragments" );
		std::vector<unsigned char> out( 2000 );
		int address = 0;
		int size = 0;
		ExpectImpairment( split.Pop( 0, address, out.data(), static_cast<int>( out.size() ), size ) && size == 1310 &&
			std::vector<unsigned char>( out.begin(), out.begin() + size ) == payload, "a fragmented datagram arrives intact" );
	}

	// The bottleneck pays for every fragment's header: two IPv4 fragments of a
	// 1310-byte datagram are 1358 bytes on a 10 KB/s link, so 136 ms.
	{
		impairmentQueue_t queue;
		std::vector<unsigned char> payload( 1310, 1 );
		queue.Offer( Sanitize( 0, 0, 0, 0, 10000, 1000, 0, 1280 ), 0, 1, payload.data(), 1310 );
		std::vector<unsigned char> out( 2000 );
		int address = 0;
		int size = 0;
		ExpectImpairment( !queue.Pop( 135, address, out.data(), static_cast<int>( out.size() ), size ) &&
			queue.Pop( 136, address, out.data(), static_cast<int>( out.size() ), size ), "fragment headers cross the bottleneck" );
	}

	// A full queue refuses further copies instead of growing without bound.
	{
		impairmentQueue_t queue;
		const unsigned char payload = 0;
		for ( int i = 0; i < MAX_QUEUED_DATAGRAMS + 10; i++ ) {
			queue.Offer( Sanitize( 1000, 0, 0, 0 ), 0, i, &payload, 1 );
		}
		ExpectImpairment( queue.Depth() == MAX_QUEUED_DATAGRAMS && queue.stats.overflowed == 10 &&
			queue.stats.maxDepth == MAX_QUEUED_DATAGRAMS, "a full queue refuses the overflow" );
	}
	return impairmentFailures;
}

#endif
