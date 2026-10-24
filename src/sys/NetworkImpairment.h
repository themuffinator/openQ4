/*
===========================================================================

openQ4 simulated network impairment.

A delay line for UDP datagrams that both platform socket layers use when
net_forceLatency, net_forceJitter, net_forceDrop or net_forceDuplicate is set,
so multiplayer can be measured on one machine under the delay, jitter,
reordering, loss and duplication a real link adds. Datagrams leave in the order
they fall due, so jitter larger than their spacing reorders them. Every random
choice comes from a seeded generator, so a run can be repeated exactly.

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

struct settings_t {
	int latencyMsec;		// fixed one-way delay added to every datagram
	int jitterMsec;			// further delay drawn uniformly from 0..jitterMsec per copy
	int dropPercent;		// chance a datagram is lost
	int duplicatePercent;	// chance a datagram arrives twice, each copy delayed on its own
};

inline int ClampInt( int value, int low, int high ) {
	return value < low ? low : ( value > high ? high : value );
}

inline settings_t Sanitize( int latencyMsec, int jitterMsec, int dropPercent, int duplicatePercent ) {
	settings_t settings;
	settings.latencyMsec = ClampInt( latencyMsec, 0, MAX_LATENCY_MSEC );
	settings.jitterMsec = ClampInt( jitterMsec, 0, MAX_JITTER_MSEC );
	settings.dropPercent = ClampInt( dropPercent, 0, 100 );
	settings.duplicatePercent = ClampInt( duplicatePercent, 0, 100 );
	return settings;
}

inline bool Active( const settings_t &settings ) {
	return settings.latencyMsec > 0 || settings.jitterMsec > 0 || settings.dropPercent > 0 ||
		settings.duplicatePercent > 0;
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

private:
	uint32_t	state;
};

struct stats_t {
	uint64_t	offered;		// datagrams handed to the delay line
	uint64_t	dropped;		// lost on purpose
	uint64_t	duplicated;		// datagrams queued twice
	uint64_t	released;		// copies delivered
	uint64_t	reordered;		// copies delivered after a later-offered datagram
	uint64_t	overflowed;		// refused: queue full, or not a valid datagram
	uint64_t	discarded;		// due, but larger than the caller's buffer
	int			maxDepth;		// most copies queued at once
};

template <typename Address>
class queue_t {
public:
				queue_t() : nextOffer( 0 ), nextArrival( 0 ), highestReleasedOffer( 0 ), anyReleased( false ), queuedBytes( 0 ) {
					memset( &stats, 0, sizeof( stats ) );
				}

	// Hands one datagram to the delay line at time now. Returns how many copies were
	// queued: 0 when it was dropped or refused, 2 when it was duplicated.
	int			Offer( const settings_t &settings, int now, const Address &address, const void *data, int size ) {
		stats.offered++;
		const uint64_t offer = nextOffer++;
		if ( size < 0 || size > MAX_DATAGRAM_BYTES || ( size > 0 && data == NULL ) ) {
			stats.overflowed++;
			return 0;
		}
		if ( random.Chance( settings.dropPercent ) ) {
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
			entry.due = static_cast<int32_t>( static_cast<uint32_t>( now ) + static_cast<uint32_t>( settings.latencyMsec ) +
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
	}

	random_t	random;
	stats_t		stats;

private:
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
