/*
===========================================================================

openQ4 simulated network impairment.

A delay line for UDP datagrams that both platform socket layers use when
net_forceLatency, net_forceJitter, net_forceDrop, net_forceDuplicate or
net_forceRate is set, so multiplayer can be measured on one machine under the
delay, jitter, reordering, loss, duplication and congestion a real link adds.
Datagrams leave in the order they fall due, so jitter larger than their spacing
reorders them. A rate turns the line into a bottleneck: datagrams queue behind
each other at that many bytes a second, and one that would wait longer than the
queue holds is dropped, as a full router buffer drops it. Losses can come in
bursts (a two-state Gilbert-Elliott model) instead of one at a time. A path MTU
(net_forceMTU) either drops each datagram larger than the path, as a path that
filters IP fragments or black-holes Packet Too Big does, or carries it in IP
fragments that are each lost on their own. Every random choice comes from a
seeded generator, so a run can be repeated exactly.

It has no engine dependencies, so the native tests drive it directly.

===========================================================================
*/

#ifndef __NETWORK_IMPAIRMENT_H__
#define __NETWORK_IMPAIRMENT_H__

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace idNetworkImpairment {

const int MAX_LATENCY_MSEC = 2000;
const int MAX_JITTER_MSEC = 1000;
const int MAX_QUEUED_DATAGRAMS = 8192;
const size_t MAX_QUEUED_BYTES = 32u * 1024u * 1024u;
// A UDP payload can never exceed this, so any datagram a socket hands over fits.
const int MAX_DATAGRAM_BYTES = 65535;
// A bottleneck from 1 KB/s to 100 MB/s, holding 1 ms to 5 s of traffic.
const int MIN_RATE_BYTES = 1000;
const int MAX_RATE_BYTES = 100000000;
const int MAX_QUEUE_MSEC = 5000;
const int DEFAULT_QUEUE_MSEC = 250;
const int MAX_BURST_LENGTH = 64;
// The IPv4 and UDP headers a datagram also carries across the bottleneck.
const int LINK_OVERHEAD_BYTES = 28;
const int UDP_HEADER_BYTES = 8;
const int IPV4_HEADER_BYTES = 20;
const int IPV6_HEADER_BYTES = 40;
// Each IPv6 fragment also carries a Fragment extension header.
const int IPV6_FRAGMENT_HEADER_BYTES = 8;
// A path MTU from the IPv4 minimum reassembly size to the largest IP packet.
const int MIN_MTU_BYTES = 576;
const int MAX_MTU_BYTES = 65535;

struct settings_t {
	int latencyMsec;		// fixed one-way delay added to every datagram
	int jitterMsec;			// further delay drawn uniformly from 0..jitterMsec per copy
	int dropPercent;		// chance a datagram is lost
	int duplicatePercent;	// chance a datagram arrives twice, each copy delayed on its own
	int rateBytes;			// bottleneck capacity in bytes a second, 0 for none
	int queueMsec;			// longest a datagram waits at the bottleneck before it is dropped
	int burstLength;		// mean datagrams lost in a row; 0 or 1 loses them independently
	int mtuBytes;			// largest IP packet the path carries whole, 0 for no limit
	bool mtuDrop;			// a larger datagram is dropped instead of sent in IP fragments
};

inline int ClampInt( int value, int low, int high ) {
	return value < low ? low : ( value > high ? high : value );
}

inline settings_t Sanitize( int latencyMsec, int jitterMsec, int dropPercent, int duplicatePercent,
							int rateBytes = 0, int queueMsec = DEFAULT_QUEUE_MSEC, int burstLength = 0,
							int mtuBytes = 0, int mtuDrop = 0 ) {
	settings_t settings;
	settings.latencyMsec = ClampInt( latencyMsec, 0, MAX_LATENCY_MSEC );
	settings.jitterMsec = ClampInt( jitterMsec, 0, MAX_JITTER_MSEC );
	settings.dropPercent = ClampInt( dropPercent, 0, 100 );
	settings.duplicatePercent = ClampInt( duplicatePercent, 0, 100 );
	settings.rateBytes = rateBytes > 0 ? ClampInt( rateBytes, MIN_RATE_BYTES, MAX_RATE_BYTES ) : 0;
	settings.queueMsec = ClampInt( queueMsec, 1, MAX_QUEUE_MSEC );
	settings.burstLength = ClampInt( burstLength, 0, MAX_BURST_LENGTH );
	settings.mtuBytes = mtuBytes > 0 ? ClampInt( mtuBytes, MIN_MTU_BYTES, MAX_MTU_BYTES ) : 0;
	settings.mtuDrop = settings.mtuBytes > 0 && mtuDrop != 0;
	return settings;
}

inline bool Active( const settings_t &settings ) {
	return settings.latencyMsec > 0 || settings.jitterMsec > 0 || settings.dropPercent > 0 ||
		settings.duplicatePercent > 0 || settings.rateBytes > 0 || settings.mtuBytes > 0;
}

// How many IP packets a UDP datagram of payloadBytes needs on a path of mtuBytes:
// 1 when it fits whole. Every fragment but the last carries a multiple of 8 bytes
// of the UDP datagram, behind its own IP header (and, for IPv6, a Fragment header).
inline int IpPackets( int mtuBytes, int payloadBytes, bool ipv6 ) {
	const int ipHeader = ipv6 ? IPV6_HEADER_BYTES : IPV4_HEADER_BYTES;
	const int datagram = payloadBytes + UDP_HEADER_BYTES;
	if ( mtuBytes <= 0 || datagram + ipHeader <= mtuBytes ) {
		return 1;
	}
	const int perFragment = ( ( mtuBytes - ipHeader - ( ipv6 ? IPV6_FRAGMENT_HEADER_BYTES : 0 ) ) / 8 ) * 8;
	return perFragment > 0 ? ( datagram + perFragment - 1 ) / perFragment : 1;
}

// The bytes a datagram occupies on the link: its UDP datagram and every IP header.
inline int WireBytes( int packets, int payloadBytes, bool ipv6 ) {
	const int ipHeader = ipv6 ? IPV6_HEADER_BYTES : IPV4_HEADER_BYTES;
	const int perPacket = ipHeader + ( ipv6 && packets > 1 ? IPV6_FRAGMENT_HEADER_BYTES : 0 );
	return payloadBytes + UDP_HEADER_BYTES + packets * perPacket;
}

// Whether time a is at or before time b on a millisecond clock that may wrap.
inline bool AtOrBefore( int a, int b ) {
	return static_cast<int32_t>( static_cast<uint32_t>( a ) - static_cast<uint32_t>( b ) ) <= 0;
}

// xorshift32: small, fast and reproducible from a seed.
class random_t {
public:
				random_t() : state( 0x9E3779B9u ) {}

	void		Seed( uint32_t seed ) { state = seed != 0 ? seed : 0x9E3779B9u; }
	uint32_t	Next() {
		uint32_t x = state;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		state = x;
		return x;
	}
	// Uniform in 0..bound inclusive.
	int			UpTo( int bound ) {
		return bound > 0 ? static_cast<int>( Next() % ( static_cast<uint32_t>( bound ) + 1u ) ) : 0;
	}
	bool		Chance( int percent ) {
		if ( percent <= 0 ) {
			return false;
		}
		return percent >= 100 || static_cast<int>( Next() % 100u ) < percent;
	}
	// True with probability ppm in a million.
	bool		ChancePpm( uint32_t ppm ) {
		return ppm >= 1000000u || Next() % 1000000u < ppm;
	}

private:
	uint32_t	state;
};

struct stats_t {
	uint64_t	offered;		// datagrams handed to the delay line
	uint64_t	offeredBytes;	// and their payload bytes, for measuring a data rate
	uint64_t	dropped;		// lost on purpose
	uint64_t	bursts;			// runs of consecutive losses (dropped / bursts is the mean run)
	uint64_t	congested;		// dropped because the bottleneck's queue was full
	uint64_t	duplicated;		// datagrams queued twice
	uint64_t	released;		// copies delivered
	uint64_t	reordered;		// copies delivered after a later-offered datagram
	uint64_t	overflowed;		// refused: queue full, or not a valid datagram
	uint64_t	discarded;		// due, but larger than the caller's buffer
	int			maxDepth;		// most copies queued at once
	int			maxWaitMsec;	// longest a datagram waited at the bottleneck
	uint64_t	oversized;		// larger than the path MTU: dropped, or sent in IP fragments
	uint64_t	ipFragments;	// IP fragments the oversized datagrams that were sent took
	int			maxSize;		// largest payload offered, in bytes
};

template <typename Address>
class queue_t {
public:
				queue_t() : nextOffer( 0 ), nextArrival( 0 ), highestReleasedOffer( 0 ), anyReleased( false ), queuedBytes( 0 ),
					linkBusy( false ), linkFreeMsec( 0 ), linkFreeUsec( 0 ), lossBurst( false ), lastLost( false ) {
					memset( &stats, 0, sizeof( stats ) );
				}

	// Hands one datagram to the delay line at time now. Returns how many copies were
	// queued: 0 when it was dropped or refused, 2 when it was duplicated. ipv6 picks
	// the headers the path MTU and the bottleneck count.
	int			Offer( const settings_t &settings, int now, const Address &address, const void *data, int size, bool ipv6 = false ) {
		stats.offered++;
		const uint64_t offer = nextOffer++;
		if ( size < 0 || size > MAX_DATAGRAM_BYTES || ( size > 0 && data == NULL ) ) {
			stats.overflowed++;
			return 0;
		}
		stats.offeredBytes += static_cast<uint64_t>( size );
		if ( size > stats.maxSize ) {
			stats.maxSize = size;
		}
		// A datagram larger than the path either never arrives or crosses it in IP
		// fragments, every one of which must arrive for the datagram to.
		const int packets = IpPackets( settings.mtuBytes, size, ipv6 );
		if ( packets > 1 ) {
			stats.oversized++;
			if ( settings.mtuDrop ) {
				return 0;
			}
			stats.ipFragments += static_cast<uint64_t>( packets );
		}
		// The bottleneck comes first: a datagram lost further along the path still
		// took its turn on the link.
		int departure = now;
		if ( settings.rateBytes > 0 && !Reserve( settings, now, WireBytes( packets, size, ipv6 ), departure ) ) {
			stats.congested++;
			return 0;
		}
		bool lost = false;
		for ( int packet = 0; packet < packets; packet++ ) {
			lost = Lost( settings ) || lost;
		}
		if ( lost ) {
			stats.dropped++;
			return 0;
		}
		const int copies = random.Chance( settings.duplicatePercent ) ? 2 : 1;
		const unsigned char *bytes = static_cast<const unsigned char *>( data );
		int queued = 0;
		for ( int copy = 0; copy < copies; copy++ ) {
			if ( static_cast<int>( entries.size() ) >= MAX_QUEUED_DATAGRAMS ||
				 queuedBytes + static_cast<size_t>( size ) > MAX_QUEUED_BYTES ) {
				stats.overflowed++;
				break;
			}
			entries.push_back( entry_t() );
			entry_t &entry = entries.back();
			// The clock may wrap, so add as unsigned; signed overflow is undefined.
			entry.due = static_cast<int32_t>( static_cast<uint32_t>( departure ) + static_cast<uint32_t>( settings.latencyMsec ) +
				static_cast<uint32_t>( random.UpTo( settings.jitterMsec ) ) );
			entry.offer = offer;
			entry.arrival = nextArrival++;
			entry.address = address;
			if ( size > 0 ) {
				entry.data.assign( bytes, bytes + size );
			}
			queuedBytes += static_cast<size_t>( size );
			queued++;
		}
		if ( queued == 2 ) {
			stats.duplicated++;
		}
		if ( static_cast<int>( entries.size() ) > stats.maxDepth ) {
			stats.maxDepth = static_cast<int>( entries.size() );
		}
		return queued;
	}

	// Releases the earliest copy due at or before now, ties in the order they were
	// queued. A due copy larger than maxSize is discarded and the next one tried.
	// Returns false when nothing is due.
	bool		Pop( int now, Address &address, void *buffer, int maxSize, int &size ) {
		size = 0;
		while ( true ) {
			size_t best = entries.size();
			for ( size_t i = 0; i < entries.size(); i++ ) {
				if ( !AtOrBefore( entries[i].due, now ) ) {
					continue;
				}
				if ( best == entries.size() ) {
					best = i;
				} else if ( entries[i].due != entries[best].due ) {
					if ( AtOrBefore( entries[i].due, entries[best].due ) ) {
						best = i;
					}
				} else if ( entries[i].arrival < entries[best].arrival ) {
					best = i;
				}
			}
			if ( best == entries.size() ) {
				return false;
			}
			entry_t entry;
			Take( best, entry );
			const int length = static_cast<int>( entry.data.size() );
			if ( buffer == NULL || length > maxSize ) {
				stats.discarded++;
				continue;
			}
			if ( length > 0 ) {
				memcpy( buffer, entry.data.data(), entry.data.size() );
			}
			size = length;
			address = entry.address;
			stats.released++;
			if ( anyReleased && entry.offer < highestReleasedOffer ) {
				stats.reordered++;
			}
			if ( !anyReleased || entry.offer > highestReleasedOffer ) {
				highestReleasedOffer = entry.offer;
			}
			anyReleased = true;
			return true;
		}
	}

	bool		Empty() const { return entries.empty(); }
	int			Depth() const { return static_cast<int>( entries.size() ); }
	void		Clear() {
		entries.clear();
		queuedBytes = 0;
		linkBusy = false;
		lossBurst = false;
		lastLost = false;
	}

	random_t	random;
	stats_t		stats;

private:
	// Queues the datagram's wireBytes behind whatever the bottleneck is still sending
	// and sets departure to when its last byte leaves. An idle link always takes it,
	// so a datagram bigger than the queue still passes; otherwise one that would
	// finish later than the queue holds is refused, and the link keeps its state.
	bool		Reserve( const settings_t &settings, int now, int wireBytes, int &departure ) {
		int64_t waitUsec = 0;
		if ( linkBusy ) {
			const int32_t aheadMsec = static_cast<int32_t>( static_cast<uint32_t>( linkFreeMsec ) - static_cast<uint32_t>( now ) );
			waitUsec = static_cast<int64_t>( aheadMsec ) * 1000 + linkFreeUsec;
			if ( waitUsec < 0 ) {
				waitUsec = 0;
			}
		}
		const int64_t sendUsec = static_cast<int64_t>( wireBytes ) * 1000000 / settings.rateBytes;
		const int64_t finishUsec = waitUsec + sendUsec;
		if ( waitUsec > 0 && finishUsec > static_cast<int64_t>( settings.queueMsec ) * 1000 ) {
			return false;
		}
		linkBusy = true;
		linkFreeMsec = static_cast<int32_t>( static_cast<uint32_t>( now ) + static_cast<uint32_t>( finishUsec / 1000 ) );
		linkFreeUsec = static_cast<int>( finishUsec % 1000 );
		departure = static_cast<int32_t>( static_cast<uint32_t>( now ) + static_cast<uint32_t>( ( finishUsec + 999 ) / 1000 ) );
		const int waitMsec = static_cast<int>( waitUsec / 1000 );
		if ( waitMsec > stats.maxWaitMsec ) {
			stats.maxWaitMsec = waitMsec;
		}
		return true;
	}

	// Whether the next datagram is lost. Independent unless burstLength is above 1:
	// then a two-state chain stays in its losing state for burstLength datagrams on
	// average and spends dropPercent of the time there. A loss rate above
	// burstLength / ( burstLength + 1 ) cannot be reached and settles at that.
	bool		Lost( const settings_t &settings ) {
		bool lost;
		if ( settings.burstLength <= 1 || settings.dropPercent <= 0 || settings.dropPercent >= 100 ) {
			lossBurst = false;
			lost = random.Chance( settings.dropPercent );
		} else {
			const uint32_t leave = 1000000u / static_cast<uint32_t>( settings.burstLength );
			const uint64_t enter = static_cast<uint64_t>( settings.dropPercent ) * leave /
				static_cast<uint64_t>( 100 - settings.dropPercent );
			if ( lossBurst ) {
				lossBurst = !random.ChancePpm( leave );
			} else {
				lossBurst = random.ChancePpm( enter > 1000000u ? 1000000u : static_cast<uint32_t>( enter ) );
			}
			lost = lossBurst;
		}
		if ( lost && !lastLost ) {
			stats.bursts++;
		}
		lastLost = lost;
		return lost;
	}

	struct entry_t {
		int							due;
		uint64_t					offer;
		uint64_t					arrival;
		Address						address;
		std::vector<unsigned char>	data;
	};

	void		Take( size_t index, entry_t &out ) {
		queuedBytes -= entries[index].data.size();
		out.due = entries[index].due;
		out.offer = entries[index].offer;
		out.arrival = entries[index].arrival;
		out.address = entries[index].address;
		out.data.swap( entries[index].data );
		if ( index + 1 != entries.size() ) {
			entries[index].due = entries.back().due;
			entries[index].offer = entries.back().offer;
			entries[index].arrival = entries.back().arrival;
			entries[index].address = entries.back().address;
			entries[index].data.swap( entries.back().data );
		}
		entries.pop_back();
	}

	std::vector<entry_t>	entries;
	uint64_t				nextOffer;
	uint64_t				nextArrival;
	uint64_t				highestReleasedOffer;
	bool					anyReleased;
	size_t					queuedBytes;
	bool					linkBusy;		// the bottleneck is still sending until linkFree
	int						linkFreeMsec;
	int						linkFreeUsec;	// and this many microseconds
	bool					lossBurst;		// the loss chain is in its losing state
	bool					lastLost;		// the previous datagram was lost
};

// One socket's two delay lines, as the platform layers use them: received datagrams
// wait in receive before the engine reads them, sent ones wait in send before they
// reach the socket. A line keeps draining after the settings return to zero.
template <typename Address>
class port_t {
public:
	void		Seed( uint32_t seed ) {
		send.random.Seed( seed * 2u + 1u );
		receive.random.Seed( seed * 2u + 2u );
	}
	bool		SendsQueued( const settings_t &settings ) const { return Active( settings ) || !send.Empty(); }
	bool		ReceivesQueued( const settings_t &settings ) const { return Active( settings ) || !receive.Empty(); }

	// Hands every send due by now to sender( address, data, size ).
	template <typename Sender>
	void		FlushSends( int now, Sender sender ) {
		if ( scratch.size() < static_cast<size_t>( MAX_DATAGRAM_BYTES ) ) {
			scratch.resize( MAX_DATAGRAM_BYTES );
		}
		Address address;
		int size = 0;
		while ( send.Pop( now, address, scratch.data(), MAX_DATAGRAM_BYTES, size ) ) {
			sender( address, scratch.data(), size );
		}
	}

	queue_t<Address>			send;
	queue_t<Address>			receive;
	int							port = 0;

private:
	std::vector<unsigned char>	scratch;
};

} // namespace idNetworkImpairment

#endif /* !__NETWORK_IMPAIRMENT_H__ */
