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

// Returns how many cases failed.
static int ExerciseNetworkImpairment() {
	using namespace idNetworkImpairment;
	impairmentFailures = 0;

	ExpectImpairment( !Active( Sanitize( 0, 0, 0, 0 ) ), "zero settings are inactive" );
	ExpectImpairment( Active( Sanitize( 0, 0, 1, 0 ) ), "loss alone is active" );
	const settings_t clamped = Sanitize( -5, 99999, 250, -1 );
	ExpectImpairment( clamped.latencyMsec == 0 && clamped.jitterMsec == MAX_JITTER_MSEC &&
		clamped.dropPercent == 100 && clamped.duplicatePercent == 0, "settings are clamped" );

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
