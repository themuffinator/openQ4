/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <errno.h>
#include <sys/select.h>
#include <net/if.h>
// getifaddrs is the only portable way to see IPv6 interface addresses; there is
// no SIOCGIFCONF equivalent for them.
#include <ifaddrs.h>

#include "../../idlib/precompiled.h"
#include "../NetworkEndpoint.h"
#include "../NetworkImpairment.h"

idPort clientPort, serverPort;

// NOCHEAT as in retail Quake4.exe 1.4.2: a server spawn without cheats resets
// every other unarchived CVar, so the next port it opened (after an engine
// reload, or for a server started again) used every interface and port 0.
idCVar net_ip( "net_ip", "localhost", CVAR_SYSTEM | CVAR_NOCHEAT, "local IPv4 address" );
idCVar net_ip6( "net_ip6", "", CVAR_SYSTEM | CVAR_NOCHEAT, "local IPv6 address, empty binds every interface" );
idCVar net_port( "net_port", "0", CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT, "local IP port number" );
idCVar net_enableIPv4( "net_enableIPv4", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "bind an IPv4 socket" );
idCVar net_enableIPv6( "net_enableIPv6", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "bind an IPv6 socket" );
idCVar net_mcast6addr( "net_mcast6addr", "ff02::1", CVAR_SYSTEM | CVAR_ARCHIVE, "IPv6 multicast group used for LAN server discovery" );
idCVar net_mcast6iface( "net_mcast6iface", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "IPv6 interface index used for LAN server discovery, 0 selects the default route" );
// openQ4: simulated network conditions for testing on one machine (NetworkImpairment.h),
// the same set as win_net.cpp; this platform had none before.
idCVar net_forceLatency( "net_forceLatency", "0", CVAR_SYSTEM | CVAR_INTEGER, "milliseconds latency" );
idCVar net_forceDrop( "net_forceDrop", "0", CVAR_SYSTEM | CVAR_INTEGER, "percentage packet loss" );
idCVar net_forceJitter( "net_forceJitter", "0", CVAR_SYSTEM | CVAR_INTEGER, "random extra milliseconds of latency per packet; packets closer together than this arrive out of order" );
idCVar net_forceDuplicate( "net_forceDuplicate", "0", CVAR_SYSTEM | CVAR_INTEGER, "percentage of packets delivered twice" );
idCVar net_forceSeed( "net_forceSeed", "0", CVAR_SYSTEM | CVAR_INTEGER, "seed for simulated loss, jitter and duplication when a port opens, 0 picks a new one" );
idCVar net_forceRate( "net_forceRate", "0", CVAR_SYSTEM | CVAR_INTEGER, "simulated bottleneck in bytes per second, each way, shared by everything on the port; 0 for none" );
idCVar net_forceQueue( "net_forceQueue", "250", CVAR_SYSTEM | CVAR_INTEGER, "milliseconds of traffic the simulated bottleneck holds before it drops packets" );
idCVar net_forceDropBurst( "net_forceDropBurst", "0", CVAR_SYSTEM | CVAR_INTEGER, "mean number of packets lost in a row; 0 or 1 loses packets independently" );
idCVar net_forceMTU( "net_forceMTU", "0", CVAR_SYSTEM | CVAR_INTEGER, "simulated path MTU in bytes (576-65535), each way; a larger packet crosses in IP fragments, or is dropped with net_forceMTUDrop; 0 for none" );
idCVar net_forceMTUDrop( "net_forceMTUDrop", "0", CVAR_SYSTEM | CVAR_BOOL, "drop a packet larger than net_forceMTU instead of sending it in IP fragments, as a path that filters fragments does" );

typedef struct {
	uint32_t ip;
	uint32_t mask;
} net_interface;

// IPv6 has no netmask on the wire. Every SLAAC capable link uses a 64 bit
// interface identifier (RFC 4291), so the on-link prefix is the address'
// leading 64 bits.
#define			IPV6_ONLINK_PREFIX_BITS	64

typedef struct {
	unsigned char	ip6[16];
	unsigned int	scopeId;
} net_interface6;

#define 		MAX_INTERFACES	32
int				num_interfaces = 0;
net_interface	netint[MAX_INTERFACES];
// A separate table and counter: IPv6 hosts routinely carry several addresses
// per link, and sharing the IPv4 array would let them evict IPv4 entries and
// silently regress IPv4 LAN detection.
int				num_interfaces6 = 0;
net_interface6	netint6[MAX_INTERFACES];

static unsigned int Sys_SockaddrIPv4HostOrder( const struct sockaddr *address ) {
	const struct sockaddr_in *ipv4 = reinterpret_cast<const struct sockaddr_in *>( address );
	return ntohl( ipv4->sin_addr.s_addr );
}

static void Sys_PrintSockaddrIPv4( const struct sockaddr *address ) {
	const struct sockaddr_in *ipv4 = reinterpret_cast<const struct sockaddr_in *>( address );
	const unsigned char *ip = reinterpret_cast<const unsigned char *>( &ipv4->sin_addr.s_addr );
	common->Printf( "%u.%u.%u.%u",
		static_cast<unsigned int>( ip[0] ),
		static_cast<unsigned int>( ip[1] ),
		static_cast<unsigned int>( ip[2] ),
		static_cast<unsigned int>( ip[3] ) );
}

static int Sys_KeepSocketFdOutOfStdioRange( int socketFd ) {
	if ( socketFd < 0 || socketFd > STDERR_FILENO ) {
		return socketFd;
	}

	const int duplicateFd = fcntl( socketFd, F_DUPFD, STDERR_FILENO + 1 );
	const int duplicateError = errno;
	close( socketFd );
	if ( duplicateFd == -1 ) {
		common->Printf( "ERROR: socket fd duplicate failed: %s\n", strerror( duplicateError ) );
	}
	return duplicateFd;
}

static void Sys_SetSockaddrPort( struct sockaddr *address, int port ) {
	if ( address->sa_family == AF_INET ) {
		reinterpret_cast<struct sockaddr_in *>( address )->sin_port = htons( static_cast<unsigned short>( port ) );
	} else if ( address->sa_family == AF_INET6 ) {
		reinterpret_cast<struct sockaddr_in6 *>( address )->sin6_port = htons( static_cast<unsigned short>( port ) );
	}
}

/*
=============
Sys_ResolveSockaddrs

Resolves an endpoint into every usable sockaddr of the requested family, in
the order a connection should try them (idNetworkEndpoint::OrderCandidates).
AF_UNSPEC puts an A record first so the legacy IPv4 transports keep their
historical behaviour for dual-stack hostnames. Returns how many addresses
were written.
=============
*/
static int Sys_ResolveSockaddrs( const char *s, bool doDNSResolve, int family, int socktype, int defaultPort, struct sockaddr_storage *sadrs, socklen_t *sadrLens, int maxAddresses ) {
	char host[NI_MAXHOST];
	char service[NI_MAXSERV];
	idNetworkEndpoint::endpointParts_t endpoint;

	if ( sadrs == NULL || sadrLens == NULL || maxAddresses <= 0 ) {
		return 0;
	}
	if ( !idNetworkEndpoint::Split( s, host, sizeof( host ), endpoint ) ) {
		return 0;
	}

	// An explicit endpoint port owns the result. Validate the default only when
	// it is actually used so host:port can safely override an unused sentinel.
	const int effectivePort = endpoint.hasPort ? endpoint.port : defaultPort;
	if ( effectivePort < 0 || effectivePort > 65535 ) {
		return 0;
	}
	idStr::snPrintf( service, sizeof( service ), "%d", effectivePort );

	// A host that has switched a family off has no socket for it, so an
	// unconstrained lookup must not hand back an address it can never reach.
	// Without this an IPv6-only client resolves every hostname to its A record
	// and then silently drops the datagram.
	if ( family == AF_UNSPEC ) {
		const bool allowIPv4 = net_enableIPv4.GetBool();
		const bool allowIPv6 = net_enableIPv6.GetBool();
		if ( allowIPv4 != allowIPv6 ) {
			family = allowIPv6 ? AF_INET6 : AF_INET;
		}
	}

	struct addrinfo hints;
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = family;
	hints.ai_socktype = socktype;
	// The service text is always the decimal port built above, so keep the
	// resolver out of /etc/services. AI_ADDRCONFIG belongs only on the DNS
	// path: on the numeric path it would reject a literal ::1 on a host with no
	// configured IPv6 interface, which is a legitimate address.
	hints.ai_flags = AI_NUMERICSERV | ( doDNSResolve ? AI_ADDRCONFIG : AI_NUMERICHOST );

	struct addrinfo *results = NULL;
	const int error = getaddrinfo( host, service, &hints, &results );
	if ( error != 0 ) {
		// Without this the common "fe80::1%eth0:27650" mistake - an unbracketed
		// scoped literal with a port - fails with no explanation at all.
		common->DPrintf( "Sys_ResolveSockaddr: '%s' did not resolve: %s\n", host, gai_strerror( error ) );
		return 0;
	}

	const struct addrinfo *usable[idNetworkEndpoint::MAX_RESOLVER_RESULTS];
	int families[idNetworkEndpoint::MAX_RESOLVER_RESULTS];
	int usableCount = 0;
	for ( const struct addrinfo *result = results; result != NULL && usableCount < idNetworkEndpoint::MAX_RESOLVER_RESULTS; result = result->ai_next ) {
		if ( result->ai_addr == NULL ) {
			continue;
		}
		if ( result->ai_family != AF_INET && result->ai_family != AF_INET6 ) {
			continue;
		}
		if ( result->ai_addrlen > sizeof( *sadrs ) ) {
			continue;
		}
		// A hosts file can list one address twice; trying it twice only
		// spends another candidate's turn.
		bool duplicate = false;
		for ( int i = 0; i < usableCount && !duplicate; i++ ) {
			duplicate = usable[i]->ai_addrlen == result->ai_addrlen && memcmp( usable[i]->ai_addr, result->ai_addr, result->ai_addrlen ) == 0;
		}
		if ( duplicate ) {
			continue;
		}
		usable[usableCount] = result;
		families[usableCount] = result->ai_family == AF_INET ? idNetworkEndpoint::ADDRESS_FAMILY_IPV4 : idNetworkEndpoint::ADDRESS_FAMILY_IPV6;
		usableCount++;
	}

	// Quake 4's master and serialized-address protocols are IPv4, so AF_UNSPEC
	// puts an available A record first while retaining IPv6 literals and
	// AAAA-only hosts. An explicit family takes its records in resolver order.
	int order[idNetworkEndpoint::MAX_RESOLVER_RESULTS];
	const int maxOrder = maxAddresses < idNetworkEndpoint::MAX_RESOLVER_RESULTS ? maxAddresses : idNetworkEndpoint::MAX_RESOLVER_RESULTS;
	const int ordered = idNetworkEndpoint::OrderCandidates( families, usableCount, family == AF_UNSPEC, order, maxOrder );
	for ( int i = 0; i < ordered; i++ ) {
		const struct addrinfo *selected = usable[order[i]];
		memset( &sadrs[i], 0, sizeof( sadrs[i] ) );
		memcpy( &sadrs[i], selected->ai_addr, selected->ai_addrlen );
		sadrLens[i] = static_cast<socklen_t>( selected->ai_addrlen );
	}

	freeaddrinfo( results );
	return ordered;
}

static bool Sys_ResolveSockaddr( const char *s, bool doDNSResolve, int family, int socktype, int defaultPort, struct sockaddr_storage *sadr, socklen_t *sadrLen ) {
	if ( sadr == NULL || sadrLen == NULL ) {
		return false;
	}
	memset( sadr, 0, sizeof( *sadr ) );
	*sadrLen = 0;
	return Sys_ResolveSockaddrs( s, doDNSResolve, family, socktype, defaultPort, sadr, sadrLen, 1 ) == 1;
}

static bool Sys_IsAnyInterfaceName( const char *net_interface ) {
	return idNetworkEndpoint::IsAnyInterfaceName( net_interface );
}

/*
====================
Sys_MulticastGroupSockadr

Resolves the configured LAN discovery group. IPv6 has no broadcast address, so
a link-local all-nodes multicast send replaces the IPv4 broadcast scan.
====================
*/
static bool Sys_MulticastGroupSockadr( unsigned short port, struct sockaddr_in6 *group ) {
	if ( group == NULL ) {
		return false;
	}
	memset( group, 0, sizeof( *group ) );

	struct sockaddr_storage resolved;
	socklen_t resolvedLen = 0;
	if ( !Sys_ResolveSockaddr( net_mcast6addr.GetString(), false, AF_INET6, SOCK_DGRAM, 0, &resolved, &resolvedLen ) ) {
		return false;
	}
	if ( resolved.ss_family != AF_INET6 ) {
		return false;
	}

	memcpy( group, &resolved, sizeof( *group ) );
	group->sin6_family = AF_INET6;
	group->sin6_port = htons( port );

	const int configuredInterface = net_mcast6iface.GetInteger();
	if ( configuredInterface > 0 ) {
		group->sin6_scope_id = static_cast<unsigned int>( configuredInterface );
	}
	return true;
}

static const char *Sys_SocketFamilyName( int family ) {
	return family == AF_INET6 ? "IPv6" : "IPv4";
}

/*
=============
Sys_JoinDiscoveryGroup

Membership in the link-local all-nodes group is automatic, but any other
configured discovery group has to be joined explicitly on every link or a
server never receives a LAN scan sent to it.
=============
*/
static void Sys_JoinDiscoveryGroup( int newsocket ) {
	struct sockaddr_in6 group;
	if ( !Sys_MulticastGroupSockadr( 0, &group ) ) {
		return;
	}

	unsigned char groupAddress[16];
	memcpy( groupAddress, &group.sin6_addr, sizeof( groupAddress ) );
	if ( !idNetworkEndpoint::IsIPv6Multicast( groupAddress ) || idNetworkEndpoint::IsIPv6AllNodes( groupAddress ) ) {
		return;
	}

	struct ipv6_mreq request;
	memset( &request, 0, sizeof( request ) );
	memcpy( &request.ipv6mr_multiaddr, groupAddress, sizeof( groupAddress ) );

	int joins = 0;
	for ( int i = 0; i < num_interfaces6; i++ ) {
		if ( netint6[i].scopeId == 0 ) {
			continue;
		}
		bool duplicate = false;
		for ( int previous = 0; previous < i; previous++ ) {
			if ( netint6[previous].scopeId == netint6[i].scopeId ) {
				duplicate = true;
				break;
			}
		}
		if ( duplicate ) {
			continue;
		}
		request.ipv6mr_interface = netint6[i].scopeId;
		if ( setsockopt( newsocket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &request, sizeof( request ) ) != -1 ) {
			joins++;
		}
	}

	if ( joins == 0 ) {
		// No enumerated link, so let the routing table choose one.
		request.ipv6mr_interface = 0;
		if ( setsockopt( newsocket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &request, sizeof( request ) ) == -1 ) {
			common->DPrintf( "IPSocketForFamily: could not join the discovery group '%s': %s\n", net_mcast6addr.GetString(), strerror( errno ) );
		}
	}
}

/*
=============
NetadrToSockadr
=============
*/
static bool NetadrToSockadr( const netadr_t * a, struct sockaddr_storage *s, socklen_t *slen ) {
	if ( a == NULL || s == NULL || slen == NULL ) {
		return false;
	}

	memset( s, 0, sizeof( *s ) );

	if ( a->type == NA_BROADCAST ) {
		struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>( s );
		ipv4->sin_family = AF_INET;
		ipv4->sin_port = htons( static_cast<unsigned short>( a->port ) );
		ipv4->sin_addr.s_addr = INADDR_BROADCAST;
		*slen = sizeof( *ipv4 );
		return true;
	} else if ( a->type == NA_IP || a->type == NA_LOOPBACK ) {
		struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>( s );
		ipv4->sin_family = AF_INET;
		memcpy( &ipv4->sin_addr.s_addr, a->ip, sizeof( a->ip ) );
		if ( a->type == NA_LOOPBACK ) {
			ipv4->sin_addr.s_addr = htonl( INADDR_LOOPBACK );
		}
		ipv4->sin_port = htons( static_cast<unsigned short>( a->port ) );
		*slen = sizeof( *ipv4 );
		return true;
	} else if ( a->type == NA_IP6 ) {
		struct sockaddr_in6 *ipv6 = reinterpret_cast<struct sockaddr_in6 *>( s );
		ipv6->sin6_family = AF_INET6;
		memcpy( &ipv6->sin6_addr, a->ip6, sizeof( a->ip6 ) );
		ipv6->sin6_scope_id = a->scopeId;
		ipv6->sin6_port = htons( static_cast<unsigned short>( a->port ) );
		*slen = sizeof( *ipv6 );
		return true;
	} else if ( a->type == NA_MULTICAST6 ) {
		struct sockaddr_in6 group;
		if ( !Sys_MulticastGroupSockadr( a->port, &group ) ) {
			return false;
		}
		memcpy( s, &group, sizeof( group ) );
		*slen = sizeof( group );
		return true;
	}

	return false;
}

/*
=============
SockadrToNetadr
=============
*/
static bool SockadrToNetadr( const struct sockaddr *s, netadr_t * a ) {
	if ( s == NULL || a == NULL ) {
		return false;
	}

	memset( a, 0, sizeof( *a ) );
	a->type = NA_BAD;

	if ( s->sa_family == AF_INET ) {
		const struct sockaddr_in *ipv4 = reinterpret_cast<const struct sockaddr_in *>( s );
		const unsigned int ip = ipv4->sin_addr.s_addr;
		memcpy( a->ip, &ip, sizeof( a->ip ) );
		a->port = ntohs( ipv4->sin_port );
		a->type = ( ntohl( ip ) == INADDR_LOOPBACK ) ? NA_LOOPBACK : NA_IP;
		return true;
	}

	if ( s->sa_family == AF_INET6 ) {
		const struct sockaddr_in6 *ipv6 = reinterpret_cast<const struct sockaddr_in6 *>( s );
		memcpy( a->ip6, &ipv6->sin6_addr, sizeof( a->ip6 ) );
		a->scopeId = ipv6->sin6_scope_id;
		a->port = ntohs( ipv6->sin6_port );
		a->type = NA_IP6;
		// A dual-stack peer that reaches us through ::ffff:a.b.c.d is an IPv4
		// peer. Normalizing here keeps address comparisons, bans, and server
		// lists from holding two identities for one host, and stays correct on
		// any platform whose IPV6_V6ONLY default differs.
		if ( idNetworkEndpoint::IsIPv4Mapped( a->ip6 ) ) {
			const unsigned short port = a->port;
			unsigned char mapped[4];
			memcpy( mapped, a->ip6 + 12, sizeof( mapped ) );
			memset( a, 0, sizeof( *a ) );
			memcpy( a->ip, mapped, sizeof( a->ip ) );
			a->port = port;
			unsigned int packedIP;
			memcpy( &packedIP, a->ip, sizeof( packedIP ) );
			a->type = ( ntohl( packedIP ) == INADDR_LOOPBACK ) ? NA_LOOPBACK : NA_IP;
		}
		return true;
	}

	return false;
}

/*
=============
Sys_StringToAdr
=============
*/
bool Sys_StringToNetAdr( const char *s, netadr_t * a, bool doDNSResolve ) {
	if ( a == NULL ) {
		return false;
	}
	memset( a, 0, sizeof( *a ) );
	a->type = NA_BAD;

	struct sockaddr_storage sadr;
	socklen_t sadrLen;

	if ( !Sys_ResolveSockaddr( s, doDNSResolve, AF_UNSPEC, SOCK_DGRAM, 0, &sadr, &sadrLen ) ) {
		return false;
	}

	return SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &sadr ), a );
}

/*
=============
Sys_StringToNetAdrCandidates
=============
*/
int Sys_StringToNetAdrCandidates( const char *s, netadr_t *candidates, int maxCandidates, bool doDNSResolve ) {
	if ( candidates == NULL || maxCandidates <= 0 ) {
		return 0;
	}
	if ( maxCandidates > idNetworkEndpoint::MAX_RESOLVED_CANDIDATES ) {
		maxCandidates = idNetworkEndpoint::MAX_RESOLVED_CANDIDATES;
	}

	struct sockaddr_storage sadrs[idNetworkEndpoint::MAX_RESOLVED_CANDIDATES];
	socklen_t sadrLens[idNetworkEndpoint::MAX_RESOLVED_CANDIDATES];
	const int resolved = Sys_ResolveSockaddrs( s, doDNSResolve, AF_UNSPEC, SOCK_DGRAM, 0, sadrs, sadrLens, maxCandidates );

	int count = 0;
	for ( int i = 0; i < resolved; i++ ) {
		netadr_t &candidate = candidates[count];
		memset( &candidate, 0, sizeof( candidate ) );
		candidate.type = NA_BAD;
		if ( !SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &sadrs[i] ), &candidate ) ) {
			continue;
		}
		// Distinct sockaddrs can still name one endpoint, an IPv4-mapped AAAA
		// record beside its A record for instance.
		bool duplicate = false;
		for ( int j = 0; j < count && !duplicate; j++ ) {
			duplicate = Sys_CompareNetAdrBase( candidates[j], candidate ) && candidates[j].port == candidate.port;
		}
		if ( !duplicate ) {
			count++;
		}
	}
	return count;
}

/*
=============
Sys_NetAdrToString
=============
*/
const char *Sys_NetAdrToString( const netadr_t a ) {
	static char s[4][128];
	static int index = 0;
	char *buffer = s[index++ & 3];

	if ( a.type == NA_LOOPBACK ) {
		if ( a.port ) {
			idStr::snPrintf( buffer, sizeof( s[0] ), "localhost:%i", a.port );
		} else {
			idStr::snPrintf( buffer, sizeof( s[0] ), "localhost" );
		}
	} else if ( a.type == NA_IP ) {
		idStr::snPrintf( buffer, sizeof( s[0] ), "%i.%i.%i.%i:%i",
			a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port );
	} else if ( a.type == NA_IP6 ) {
		// The shared formatter, rather than inet_ntop, so a given address has
		// exactly one spelling on every platform. That text is the identity key
		// for the server browser and the game module's ban list.
		char addressText[idNetworkEndpoint::IPV6_ENDPOINT_TEXT_SIZE];
		if ( !idNetworkEndpoint::FormatIPv6Endpoint( a.ip6, a.scopeId, a.port, addressText, sizeof( addressText ) ) ) {
			idStr::Copynz( addressText, "::", sizeof( addressText ) );
		}
		idStr::Copynz( buffer, addressText, sizeof( s[0] ) );
	} else if ( a.type == NA_BROADCAST ) {
		if ( a.port ) {
			idStr::snPrintf( buffer, sizeof( s[0] ), "broadcast:%i", a.port );
		} else {
			idStr::Copynz( buffer, "broadcast", sizeof( s[0] ) );
		}
	} else if ( a.type == NA_MULTICAST6 ) {
		if ( a.port ) {
			idStr::snPrintf( buffer, sizeof( s[0] ), "multicast6:%i", a.port );
		} else {
			idStr::Copynz( buffer, "multicast6", sizeof( s[0] ) );
		}
	} else if ( a.type == NA_BOT ) {
		idStr::Copynz( buffer, "bot", sizeof( s[0] ) );
	} else {
		idStr::Copynz( buffer, "bad", sizeof( s[0] ) );
	}
	return buffer;
}

/*
==================
Sys_IsLANAddress
==================
*/
bool Sys_IsLANAddress( const netadr_t adr ) {
	int i;
	uint32_t ip;

#if ID_NOLANADDRESS
	common->Printf( "Sys_IsLANAddress: ID_NOLANADDRESS\n" );
	return false;
#endif

	if ( adr.type == NA_LOOPBACK ) {
		return true;
	}

	if ( adr.type == NA_IP6 ) {
		if ( idNetworkEndpoint::IsIPv6Loopback( adr.ip6 ) || idNetworkEndpoint::IsIPv6LinkLocal( adr.ip6 ) ||
				idNetworkEndpoint::IsIPv6UniqueLocal( adr.ip6 ) || idNetworkEndpoint::IsIPv6SiteLocal( adr.ip6 ) ) {
			return true;
		}
		// The scope rules alone would reject the ordinary SLAAC case, where a
		// neighbour on this very link carries a global unicast address. Match
		// it against the on-link prefix of each local address instead.
		for ( int scan = 0; scan < num_interfaces6; scan++ ) {
			if ( idNetworkEndpoint::IPv6PrefixMatches( adr.ip6, netint6[scan].ip6, IPV6_ONLINK_PREFIX_BITS ) ) {
				return true;
			}
		}
		return false;
	}

	if ( adr.type != NA_IP ) {
		return false;
	}

	if ( !num_interfaces ) {
		return false;	// well, if there's no networking, there are no LAN addresses, right
	}

	for ( i = 0; i < num_interfaces; i++ ) {
		unsigned int packedIP;
		memcpy( &packedIP, adr.ip, sizeof( packedIP ) );
		ip = ntohl( packedIP );
		if( ( netint[i].ip & netint[i].mask ) == ( ip & netint[i].mask ) ) {
			return true;
		}
	}

	return false;
}

/*
==================
Sys_GetLocalIPAddress

This machine's first IPv4 interface address that is not a loopback one, as
the stock JOIN IP ADDRESS popup shows the player ("your ip address:"; retail's
getClientIP showed its first interface's). False when it has none.
==================
*/
bool Sys_GetLocalIPAddress( netadr_t &out ) {
	for ( int i = 0; i < num_interfaces; i++ ) {
		const unsigned int ip = netint[i].ip;	// host byte order
		if ( ip == 0 || ( ip & 0xff000000u ) == 0x7f000000u ) {
			continue;
		}
		memset( &out, 0, sizeof( out ) );
		out.type = NA_IP;
		out.ip[0] = static_cast<unsigned char>( ip >> 24 );
		out.ip[1] = static_cast<unsigned char>( ip >> 16 );
		out.ip[2] = static_cast<unsigned char>( ip >> 8 );
		out.ip[3] = static_cast<unsigned char>( ip );
		return true;
	}
	return false;
}

/*
===================
Sys_CompareNetAdrBase

Compares without the port
===================
*/
bool Sys_CompareNetAdrBase( const netadr_t a, const netadr_t b ) {
	if ( a.type != b.type ) {
		return false;
	}

	if ( a.type == NA_LOOPBACK ) {
		return true;
	}

	if ( a.type == NA_IP ) {
		if ( a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] && a.ip[2] == b.ip[2] && a.ip[3] == b.ip[3] ) {
			return true;
		}
		return false;
	}

	if ( a.type == NA_IP6 ) {
		return memcmp( a.ip6, b.ip6, sizeof( a.ip6 ) ) == 0 && a.scopeId == b.scopeId;
	}

	common->Printf( "Sys_CompareNetAdrBase: bad address type\n" );
	return false;
}

/*
====================
Sys_InitIPv6Interfaces

Records every local IPv6 unicast address so Sys_IsLANAddress can recognize a
global-unicast neighbour on the same link, which the address scope rules alone
cannot decide.
====================
*/
static void Sys_InitIPv6Interfaces( void ) {
	struct ifaddrs *ifap, *ifp;

	num_interfaces6 = 0;

	if ( getifaddrs( &ifap ) < 0 ) {
		common->Printf( "Sys_InitNetworking: getifaddrs failed - %s\n", strerror( errno ) );
		return;
	}

	for ( ifp = ifap; ifp; ifp = ifp->ifa_next ) {
		if ( !( ifp->ifa_flags & IFF_UP ) ) {
			continue;
		}
		if ( ifp->ifa_addr == NULL || ifp->ifa_addr->sa_family != AF_INET6 ) {
			continue;
		}
		if ( num_interfaces6 >= MAX_INTERFACES ) {
			common->Printf( "Sys_InitNetworking: MAX_INTERFACES(%d) hit for IPv6.\n", MAX_INTERFACES );
			break;
		}

		const struct sockaddr_in6 *ipv6 = reinterpret_cast<const struct sockaddr_in6 *>( ifp->ifa_addr );
		unsigned char address[16];
		memcpy( address, &ipv6->sin6_addr, sizeof( address ) );
		unsigned int scopeId = ipv6->sin6_scope_id;

		// KAME derived stacks (Darwin and the BSDs) report a link-local address
		// with the interface index embedded in bytes 2 and 3 and leave
		// sin6_scope_id zero. Left in place those bytes make every fe80::
		// prefix comparison against a kernel-normalized peer address fail.
		if ( idNetworkEndpoint::IsIPv6LinkLocal( address ) && ( address[2] != 0 || address[3] != 0 ) ) {
			if ( scopeId == 0 ) {
				scopeId = ( static_cast<unsigned int>( address[2] ) << 8 ) | static_cast<unsigned int>( address[3] );
			}
			address[2] = 0;
			address[3] = 0;
		}

		memcpy( netint6[num_interfaces6].ip6, address, sizeof( netint6[num_interfaces6].ip6 ) );
		netint6[num_interfaces6].scopeId = scopeId;

		char text[idNetworkEndpoint::IPV6_ENDPOINT_TEXT_SIZE];
		if ( idNetworkEndpoint::FormatIPv6Endpoint( address, scopeId, 0, text, sizeof( text ) ) ) {
			common->Printf( "IPv6: %s/%d\n", text, IPV6_ONLINK_PREFIX_BITS );
		}
		num_interfaces6++;
	}

	freeifaddrs( ifap );
}

/*
====================
NET_InitNetworking
====================
*/
void Sys_InitNetworking(void)
{
	// haven't been able to clearly pinpoint which standards or RFCs define SIOCGIFCONF, SIOCGIFADDR, SIOCGIFNETMASK ioctls
	// it seems fairly widespread, in Linux kernel ioctl, and in BSD .. so let's assume it's always available on our targets

#if defined( MACOS_X ) || defined( __APPLE__ )
	unsigned int ip, mask;
	struct ifaddrs *ifap, *ifp;
	
	num_interfaces = 0;
	
	if( getifaddrs( &ifap ) < 0 ) {
		common->FatalError( "InitNetworking: SIOCGIFCONF error - %s\n", strerror( errno ) );
		return;
	}
	
	for( ifp = ifap; ifp; ifp = ifp->ifa_next ) {
		if ( !( ifp->ifa_flags & IFF_UP ) )
			continue;

		if ( !ifp->ifa_addr )
			continue;

		if ( ifp->ifa_addr->sa_family != AF_INET )
			continue;

		if ( !ifp->ifa_netmask )
			continue;
		
		ip = Sys_SockaddrIPv4HostOrder( ifp->ifa_addr );
		mask = Sys_SockaddrIPv4HostOrder( ifp->ifa_netmask );
		if ( mask == 0 ) {
			common->Printf( "Sys_InitNetworking: interface %s has a zero IPv4 netmask - skipped\n",
				ifp->ifa_name != NULL ? ifp->ifa_name : "<unnamed>" );
			continue;
		}
		
		if ( ip == INADDR_LOOPBACK ) {
			common->Printf( "loopback\n" );
		} else {
			common->Printf( "IP: " );
			Sys_PrintSockaddrIPv4( ifp->ifa_addr );
			common->Printf( "\nNetMask: " );
			Sys_PrintSockaddrIPv4( ifp->ifa_netmask );
			common->Printf( "\n" );
		}
		if ( num_interfaces < MAX_INTERFACES ) {
			netint[ num_interfaces ].ip = ip;
			netint[ num_interfaces ].mask = mask;
			num_interfaces++;
		} else {
			common->Printf( "Sys_InitNetworking: MAX_INTERFACES(%d) hit.\n", MAX_INTERFACES );
		}
	}
	freeifaddrs( ifap );
#else
	int		s;
	char	buf[ MAX_INTERFACES*sizeof( ifreq ) ];
	ifconf	ifc;
	ifreq	*ifr;
	int		ifindex;
	unsigned int ip, mask;

	num_interfaces = 0;

	s = socket( AF_INET, SOCK_DGRAM, 0 );
	if ( s == -1 ) {
		// An IPv6-only host may have no AF_INET support at all, so its IPv6
		// interfaces still have to be enumerated.
		common->Printf( "Sys_InitNetworking: socket failed - %s\n", strerror( errno ) );
		Sys_InitIPv6Interfaces();
		return;
	}
	ifc.ifc_len = MAX_INTERFACES*sizeof( ifreq );
	ifc.ifc_buf = buf;
	if ( ioctl( s, SIOCGIFCONF, &ifc ) < 0 ) {
		close( s );
		common->FatalError( "InitNetworking: SIOCGIFCONF error - %s\n", strerror( errno ) );
		return;
	}
	ifindex = 0;
	while ( ifindex < ifc.ifc_len ) {
		common->Printf( "found interface %s - ", ifc.ifc_buf + ifindex );
		// find the type - ignore interfaces for which we can find we can't get IP and mask ( not configured )
		ifr = (ifreq*)( ifc.ifc_buf + ifindex );
		if ( ioctl( s, SIOCGIFADDR, ifr ) < 0 ) {
			common->Printf( "SIOCGIFADDR failed: %s\n", strerror( errno ) );			
		} else {
			if ( ifr->ifr_addr.sa_family != AF_INET ) {
				common->Printf( "not AF_INET\n" );
			} else {
				ip = Sys_SockaddrIPv4HostOrder( &ifr->ifr_addr );
				if ( ip == INADDR_LOOPBACK ) {
					common->Printf( "loopback\n" );
				} else {
					Sys_PrintSockaddrIPv4( &ifr->ifr_addr );
				}
				if ( ioctl( s, SIOCGIFNETMASK, ifr ) < 0 ) {
					common->Printf( " SIOCGIFNETMASK failed: %s\n", strerror( errno ) );
				} else {
					mask = Sys_SockaddrIPv4HostOrder( &ifr->ifr_addr );
					if ( mask == 0 ) {
						common->Printf( " zero IPv4 netmask - skipped\n" );
					} else {
						if ( ip != INADDR_LOOPBACK ) {
							common->Printf( "/" );
							Sys_PrintSockaddrIPv4( &ifr->ifr_addr );
							common->Printf( "\n" );
						}
						if ( num_interfaces < MAX_INTERFACES ) {
							netint[ num_interfaces ].ip = ip;
							netint[ num_interfaces ].mask = mask;
							num_interfaces++;
						} else {
							common->Printf( "Sys_InitNetworking: MAX_INTERFACES(%d) hit.\n", MAX_INTERFACES );
						}
					}
				}
			}
		}
		ifindex += sizeof( ifreq );
	}
	close( s );
#endif

	Sys_InitIPv6Interfaces();
}

/*
====================
IPSocketForFamily
====================
*/
static int IPSocketForFamily( const char *net_interface, int port, int family,
		netadr_t *bound_to = NULL, bool quiet = false, bool *addressResolved = NULL ) {
	if ( addressResolved != NULL ) {
		*addressResolved = false;
	}
	if ( family != AF_INET && family != AF_INET6 ) {
		return 0;
	}
	if ( port != PORT_ANY && ( port < 0 || port > 65535 ) ) {
		if ( !quiet ) {
			common->Printf( "ERROR: IPSocketForFamily: invalid port %d\n", port );
		}
		return 0;
	}

	const int bindPort = port == PORT_ANY ? 0 : port;
	// An unbracketed IPv6 interface followed by ":port" reads as a longer
	// address, and PORT_ANY is a sentinel rather than a port, so neither is
	// printed raw.
	char endpointText[idNetworkEndpoint::IPV6_ENDPOINT_TEXT_SIZE + NI_MAXHOST];
	const char *interfaceText = Sys_IsAnyInterfaceName( net_interface ) ? "localhost" : net_interface;
	const bool bracketInterface = family == AF_INET6 && interfaceText[0] != '[' && strchr( interfaceText, ':' ) != NULL;
	if ( port == PORT_ANY ) {
		idStr::snPrintf( endpointText, sizeof( endpointText ), "%s%s%s:auto",
			bracketInterface ? "[" : "", interfaceText, bracketInterface ? "]" : "" );
	} else {
		idStr::snPrintf( endpointText, sizeof( endpointText ), "%s%s%s:%i",
			bracketInterface ? "[" : "", interfaceText, bracketInterface ? "]" : "", port );
	}
	if ( !quiet ) {
		common->Printf( "Opening %s UDP socket: %s\n", Sys_SocketFamilyName( family ), endpointText );
	}

	struct sockaddr_storage address;
	socklen_t addressLen = 0;
	memset( &address, 0, sizeof( address ) );

	if ( Sys_IsAnyInterfaceName( net_interface ) ) {
		if ( family == AF_INET ) {
			struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>( &address );
			ipv4->sin_family = AF_INET;
			ipv4->sin_addr.s_addr = INADDR_ANY;
			ipv4->sin_port = htons( static_cast<unsigned short>( bindPort ) );
			addressLen = sizeof( *ipv4 );
		} else {
			struct sockaddr_in6 *ipv6 = reinterpret_cast<struct sockaddr_in6 *>( &address );
			ipv6->sin6_family = AF_INET6;
			ipv6->sin6_addr = in6addr_any;
			ipv6->sin6_port = htons( static_cast<unsigned short>( bindPort ) );
			addressLen = sizeof( *ipv6 );
		}
		if ( addressResolved != NULL ) {
			*addressResolved = true;
		}
	} else {
		if ( !Sys_ResolveSockaddr( net_interface, true, family, SOCK_DGRAM, bindPort, &address, &addressLen ) ) {
			if ( !quiet ) {
				common->Printf( "ERROR: IPSocketForFamily: bad %s interface address '%s'\n", Sys_SocketFamilyName( family ), net_interface );
			}
			return 0;
		}
		if ( addressResolved != NULL ) {
			*addressResolved = true;
		}
		Sys_SetSockaddrPort( reinterpret_cast<struct sockaddr *>( &address ), bindPort );
	}

	int newsocket = socket( family, SOCK_DGRAM, IPPROTO_UDP );
	if ( newsocket == -1 ) {
		if ( !quiet ) {
			common->Printf( "ERROR: IPSocketForFamily: %s socket: %s\n", Sys_SocketFamilyName( family ), strerror( errno ) );
		}
		return 0;
	}
	newsocket = Sys_KeepSocketFdOutOfStdioRange( newsocket );
	if ( newsocket == -1 ) {
		return 0;
	}

	int on = 1;
	if ( ioctl( newsocket, FIONBIO, &on ) == -1 ) {
		if ( !quiet ) {
			common->Printf( "ERROR: IPSocketForFamily: %s ioctl FIONBIO: %s\n", Sys_SocketFamilyName( family ), strerror( errno ) );
		}
		close( newsocket );
		return 0;
	}

	if ( family == AF_INET ) {
		if ( setsockopt( newsocket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char *>( &on ), sizeof( on ) ) == -1 ) {
			if ( !quiet ) {
				common->Printf( "ERROR: IPSocketForFamily: setsockopt SO_BROADCAST: %s\n", strerror( errno ) );
			}
			close( newsocket );
			return 0;
		}
	}

	if ( family == AF_INET6 ) {
#ifdef IPV6_V6ONLY
		// Keep the IPv6 socket off the IPv4 mapped range so both families can
		// hold the same port and so an IPv4 peer always arrives on the socket
		// whose broadcast and interface configuration matches it. A platform
		// without the option is covered defensively by the IPv4-mapped
		// normalization in SockadrToNetadr.
		if ( setsockopt( newsocket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char *>( &on ), sizeof( on ) ) == -1 ) {
			if ( !quiet ) {
				common->Printf( "ERROR: IPSocketForFamily: setsockopt IPV6_V6ONLY: %s\n", strerror( errno ) );
			}
			close( newsocket );
			return 0;
		}
#endif
		// Link-local discovery only ever needs one hop; a larger default would
		// leak scan traffic past the local segment. Linux requires an int here,
		// unlike the IPv4 IP_MULTICAST_TTL convention.
		const int multicastHops = 1;
		if ( setsockopt( newsocket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &multicastHops, sizeof( multicastHops ) ) == -1 ) {
			common->DPrintf( "IPSocketForFamily: setsockopt IPV6_MULTICAST_HOPS: %s\n", strerror( errno ) );
		}
		const int multicastInterface = net_mcast6iface.GetInteger();
		if ( multicastInterface > 0 ) {
			const unsigned int interfaceIndex = static_cast<unsigned int>( multicastInterface );
			if ( setsockopt( newsocket, IPPROTO_IPV6, IPV6_MULTICAST_IF, &interfaceIndex, sizeof( interfaceIndex ) ) == -1 ) {
				common->DPrintf( "IPSocketForFamily: setsockopt IPV6_MULTICAST_IF: %s\n", strerror( errno ) );
			}
		}
		Sys_JoinDiscoveryGroup( newsocket );
	}

	if ( bind( newsocket, reinterpret_cast<const struct sockaddr *>( &address ), addressLen ) == -1 ) {
		if ( !quiet ) {
			common->Printf( "ERROR: IPSocketForFamily: %s bind: %s\n", Sys_SocketFamilyName( family ), strerror( errno ) );
		}
		close( newsocket );
		return 0;
	}

	if ( quiet ) {
		common->Printf( "Opening %s UDP socket: %s\n", Sys_SocketFamilyName( family ), endpointText );
	}

	if ( bound_to ) {
		struct sockaddr_storage boundAddress;
		memset( &boundAddress, 0, sizeof( boundAddress ) );
		socklen_t boundAddressLen = sizeof( boundAddress );
		if ( getsockname( newsocket, reinterpret_cast<struct sockaddr *>( &boundAddress ), &boundAddressLen ) == -1 ) {
			common->Printf( "ERROR: IPSocketForFamily: getsockname: %s\n", strerror( errno ) );
			close( newsocket );
			return 0;
		}
		if ( !SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &boundAddress ), bound_to ) ) {
			common->Printf( "ERROR: IPSocketForFamily: unsupported bound address family\n" );
			close( newsocket );
			return 0;
		}
	}

	return newsocket;
}

/*
====================
Net_BindDualStack

Opens the sockets an idPort should own. IPv4 stays the baseline transport for
discovery, LAN scans, masters, and ordinary clients, so a resolved IPv4 bind
failure fails the whole endpoint rather than silently handing the caller an
IPv6-only port that its peers cannot reach.
====================
*/
static bool Net_BindDualStack( const char *ipv4Text, const char *ipv6Text, int portNumber, int &socket4, int &socket6, netadr_t &bound_to ) {
	socket4 = 0;
	socket6 = 0;
	memset( &bound_to, 0, sizeof( bound_to ) );

	idNetworkEndpoint::bindPlan_t plan;
	idNetworkEndpoint::PlanBind( ipv4Text, ipv6Text, net_enableIPv4.GetBool(), net_enableIPv6.GetBool(), plan );
	// An IPv6 literal in net_ip suppresses the IPv4 socket, so this case has to
	// be reported before the both-families-disabled test below or the only
	// diagnostic would name net_enableIPv4, which the admin never touched.
	if ( plan.legacyIPv6Interface && !plan.bindIPv6 ) {
		common->Warning( "idPort::InitForPort: net_ip names an IPv6 interface but net_enableIPv6 is 0" );
		return false;
	}
	if ( !plan.bindIPv4 && !plan.bindIPv6 ) {
		common->Warning( "idPort::InitForPort: net_enableIPv4 and net_enableIPv6 are both disabled" );
		return false;
	}
	if ( plan.legacyIPv6Interface ) {
		common->DPrintf( "net_ip holds an IPv6 literal - binding it as the IPv6 interface, prefer net_ip6\n" );
	}

	netadr_t bound4;
	netadr_t bound6;
	memset( &bound4, 0, sizeof( bound4 ) );
	memset( &bound6, 0, sizeof( bound6 ) );

	const char *ipv6Interface = plan.ipv6Interface;

	if ( plan.bindIPv4 ) {
		bool ipv4Resolved = false;
		socket4 = IPSocketForFamily( plan.ipv4Interface, portNumber, AF_INET, &bound4, true, &ipv4Resolved );
		if ( socket4 <= 0 ) {
			socket4 = 0;
			// Once the interface resolved to an A record, a later setup or bind
			// failure belongs to that endpoint - for example EADDRINUSE during
			// a port scan - so fail closed instead of falling through.
			if ( ipv4Resolved ) {
				return false;
			}
			// The name produced no A record at all, so it may still name an
			// IPv6-only interface. Reuse it unless net_ip6 already chose one.
			if ( idNetworkEndpoint::IsAnyInterfaceName( ipv6Interface ) ) {
				ipv6Interface = plan.ipv4Interface;
			}
		} else {
			bound_to = bound4;
		}
	}

	if ( plan.bindIPv6 ) {
		// Both families share one port number so a dual-stack server is reached
		// the same way over either transport.
		const int ipv6Port = ( portNumber == PORT_ANY && socket4 > 0 ) ? bound4.port : portNumber;
		socket6 = IPSocketForFamily( ipv6Interface, ipv6Port, AF_INET6, &bound6, true );
		if ( socket6 <= 0 ) {
			socket6 = 0;
		}

		// The kernel picked that ephemeral port for IPv4 alone, so another
		// process may already hold it on IPv6. Clients bind with PORT_ANY, and
		// accepting the half-bound result would leave them quietly unable to
		// reach any IPv6 server. Try a few other ephemeral ports before
		// settling for IPv4 only. An explicit port is the operator's choice and
		// is never retried.
		for ( int attempt = 0; socket6 <= 0 && attempt < 3 && portNumber == PORT_ANY &&
				plan.bindIPv4 && socket4 > 0; attempt++ ) {
			netadr_t retryBound;
			memset( &retryBound, 0, sizeof( retryBound ) );
			// Open the replacement before releasing the old socket so a failed
			// retry cannot cost us the IPv4 socket we already hold.
			const int retrySocket4 = IPSocketForFamily( plan.ipv4Interface, PORT_ANY, AF_INET, &retryBound, true );
			if ( retrySocket4 <= 0 ) {
				break;
			}
			const int retrySocket6 = IPSocketForFamily( ipv6Interface, retryBound.port, AF_INET6, &bound6, true );
			if ( retrySocket6 <= 0 ) {
				close( retrySocket4 );
				continue;
			}
			close( socket4 );
			socket4 = retrySocket4;
			bound4 = retryBound;
			bound_to = bound4;
			socket6 = retrySocket6;
		}

		if ( socket6 > 0 && socket4 <= 0 ) {
			bound_to = bound6;
		}
	}

	if ( socket4 <= 0 && socket6 <= 0 ) {
		socket4 = 0;
		socket6 = 0;
		memset( &bound_to, 0, sizeof( bound_to ) );
		return false;
	}
	return true;
}

/*
==================
Simulated network conditions

openQ4: each port's two delay lines (NetworkImpairment.h), as in win_net.cpp.
==================
*/
typedef idNetworkImpairment::port_t<netadr_t> netImpairmentPort_t;

static const int NET_MAX_IMPAIRED_PORTS = 8;
static netImpairmentPort_t *net_impairedPorts[NET_MAX_IMPAIRED_PORTS];

static idNetworkImpairment::settings_t Net_ImpairmentSettings( void ) {
	return idNetworkImpairment::Sanitize( net_forceLatency.GetInteger(), net_forceJitter.GetInteger(),
		net_forceDrop.GetInteger(), net_forceDuplicate.GetInteger(), net_forceRate.GetInteger(),
		net_forceQueue.GetInteger(), net_forceDropBurst.GetInteger(), net_forceMTU.GetInteger(),
		net_forceMTUDrop.GetBool() ? 1 : 0 );
}

// The path MTU and the bottleneck count each family's own IP header.
static bool Net_ImpairmentIPv6( const netadr_t &adr ) {
	return adr.type == NA_IP6 || adr.type == NA_MULTICAST6;
}

static netImpairmentPort_t *Net_OpenImpairment( int port ) {
	netImpairmentPort_t *impairment = new netImpairmentPort_t;
	impairment->port = port;
	const unsigned int seed = net_forceSeed.GetInteger() != 0 ? static_cast<unsigned int>( net_forceSeed.GetInteger() ) :
		static_cast<unsigned int>( Sys_Milliseconds() ) ^ ( static_cast<unsigned int>( port ) * 2654435761u );
	impairment->Seed( seed );
	for ( int i = 0; i < NET_MAX_IMPAIRED_PORTS; i++ ) {
		if ( net_impairedPorts[i] == NULL ) {
			net_impairedPorts[i] = impairment;
			break;
		}
	}
	return impairment;
}

static void Net_CloseImpairment( netImpairmentPort_t *impairment ) {
	for ( int i = 0; i < NET_MAX_IMPAIRED_PORTS; i++ ) {
		if ( net_impairedPorts[i] == impairment ) {
			net_impairedPorts[i] = NULL;
		}
	}
	delete impairment;
}

static void Net_PrintImpairmentLine( int port, const char *direction, const idNetworkImpairment::queue_t<netadr_t> &queue ) {
	const idNetworkImpairment::stats_t &stats = queue.stats;
	// New fields go at the end: harnesses parse the line by its leading fields.
	common->Printf( "NET_IMPAIR port=%d dir=%s offered=%llu dropped=%llu duplicated=%llu released=%llu reordered=%llu "
		"overflowed=%llu discarded=%llu queued=%d maxQueued=%d bytes=%llu bursts=%llu congested=%llu maxWait=%d "
		"oversized=%llu ipFragments=%llu maxSize=%d\n", port,
		direction, static_cast<unsigned long long>( stats.offered ), static_cast<unsigned long long>( stats.dropped ),
		static_cast<unsigned long long>( stats.duplicated ), static_cast<unsigned long long>( stats.released ),
		static_cast<unsigned long long>( stats.reordered ), static_cast<unsigned long long>( stats.overflowed ),
		static_cast<unsigned long long>( stats.discarded ), queue.Depth(), stats.maxDepth,
		static_cast<unsigned long long>( stats.offeredBytes ),
		static_cast<unsigned long long>( stats.bursts ), static_cast<unsigned long long>( stats.congested ), stats.maxWaitMsec,
		static_cast<unsigned long long>( stats.oversized ), static_cast<unsigned long long>( stats.ipFragments ), stats.maxSize );
}

/*
==================
Sys_NetImpairmentReport
==================
*/
void Sys_NetImpairmentReport( void ) {
	const idNetworkImpairment::settings_t settings = Net_ImpairmentSettings();
	common->Printf( "NET_IMPAIR_SETTINGS latency=%d jitter=%d drop=%d burst=%d duplicate=%d rate=%d queue=%d active=%d mtu=%d mtuDrop=%d\n",
		settings.latencyMsec, settings.jitterMsec, settings.dropPercent, settings.burstLength, settings.duplicatePercent,
		settings.rateBytes, settings.queueMsec, idNetworkImpairment::Active( settings ) ? 1 : 0, settings.mtuBytes,
		settings.mtuDrop ? 1 : 0 );
	for ( int i = 0; i < NET_MAX_IMPAIRED_PORTS; i++ ) {
		if ( net_impairedPorts[i] != NULL ) {
			Net_PrintImpairmentLine( net_impairedPorts[i]->port, "receive", net_impairedPorts[i]->receive );
			Net_PrintImpairmentLine( net_impairedPorts[i]->port, "send", net_impairedPorts[i]->send );
		}
	}
}

/*
==================
Net_SendDatagram

One datagram to one unicast or broadcast address, through the socket of its family.
==================
*/
static bool Net_SendDatagram( int ipv4Socket, int ipv6Socket, const netadr_t &to, const void *data, int size ) {
	struct sockaddr_storage addr;
	socklen_t addrLen;
	if ( !NetadrToSockadr( &to, &addr, &addrLen ) ) {
		common->Warning( "idPort::SendPacket: bad address type - ignored" );
		return false;
	}

	const int socketFd = addr.ss_family == AF_INET6 ? ipv6Socket : ipv4Socket;
	if ( !socketFd ) {
		// A disabled family is a supported configuration - net_enableIPv4 0
		// makes every IPv4 broadcast and master heartbeat land here - so this
		// is a developer diagnostic rather than a per-datagram warning.
		common->DPrintf( "idPort::SendPacket: no %s socket for %s - ignored\n", Sys_SocketFamilyName( addr.ss_family ), Sys_NetAdrToString( to ) );
		return false;
	}

	const int ret = sendto( socketFd, data, size, 0, reinterpret_cast<struct sockaddr *>( &addr ), addrLen );
	if ( ret == -1 ) {
		common->Printf( "idPort::SendPacket ERROR: to %s: %s\n", Sys_NetAdrToString( to ), strerror( errno ) );
		return false;
	}
	return true;
}

/*
==================
idPort::idPort
==================
*/
idPort::idPort() {
	netSocket = 0;
	netSocket6 = 0;
	platformData = NULL;
	packetsRead = 0;
	bytesRead = 0;
	packetsWritten = 0;
	bytesWritten = 0;
	memset( &bound_to, 0, sizeof( bound_to ) );
}

/*
==================
idPort::~idPort
==================
*/
idPort::~idPort() {
	Close();
}

/*
==================
idPort::Close
==================
*/
void idPort::Close() {
	if ( netSocket ) {
		close( netSocket );
		netSocket = 0;
	}
	if ( netSocket6 ) {
		close( netSocket6 );
		netSocket6 = 0;
	}
	if ( platformData != NULL ) {
		Net_CloseImpairment( static_cast<netImpairmentPort_t *>( platformData ) );
	}
	platformData = NULL;
	memset( &bound_to, 0, sizeof( bound_to ) );
}

/*
==================
idPort::GetPacket
==================
*/
static bool Net_GetPacketFromSocket( int socketFd, const char *context, netadr_t &net_from, void *data, int &size, int maxSize ) {
	size = 0;
	memset( &net_from, 0, sizeof( net_from ) );
	net_from.type = NA_BAD;
	struct sockaddr_storage from;
	memset( &from, 0, sizeof( from ) );

	if ( socketFd <= 0 || data == NULL || maxSize <= 0 ) {
		return false;
	}

	struct iovec iov;
	iov.iov_base = data;
	iov.iov_len = static_cast<size_t>( maxSize );
	struct msghdr message;
	memset( &message, 0, sizeof( message ) );
	message.msg_name = &from;
	message.msg_namelen = sizeof( from );
	message.msg_iov = &iov;
	message.msg_iovlen = 1;

	const ssize_t ret = recvmsg( socketFd, &message, 0 );

	if ( ret == -1 ) {
		if ( errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED ) {
			// those commonly happen, don't verbose
			return false;
		}
		common->DPrintf( "%s recvmsg(): %s\n", context, strerror( errno ) );
		return false;
	}

	if ( !SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &from ), &net_from ) ) {
		common->DPrintf( "%s: unsupported address family\n", context );
		return false;
	}
	if ( ( message.msg_flags & MSG_TRUNC ) != 0 || ret > maxSize ) {
		common->DPrintf( "%s: oversize packet from %s\n", context, Sys_NetAdrToString( net_from ) );
		memset( &net_from, 0, sizeof( net_from ) );
		net_from.type = NA_BAD;
		return false;
	}

	size = static_cast<int>( ret );
	return true;
}

bool idPort::GetPacket( netadr_t &net_from, void *data, int &size, int maxSize ) {
	size = 0;
	memset( &net_from, 0, sizeof( net_from ) );
	net_from.type = NA_BAD;
	if ( !netSocket && !netSocket6 ) {
		return false;
	}
	if ( data == NULL || maxSize <= 0 ) {
		size = 0;
		return false;
	}

	// Alternate which family is drained first. Callers loop until this returns
	// false, so a flood that keeps one socket permanently readable would
	// otherwise stop the other family from ever being read. Seeding from this
	// port's own read counter keeps the rotation per-instance without widening
	// idPort's shared layout.
	netImpairmentPort_t *impairment = static_cast<netImpairmentPort_t *>( platformData );
	const idNetworkImpairment::settings_t settings = Net_ImpairmentSettings();
	const int now = Sys_Milliseconds();
	const bool queueReceives = impairment != NULL && impairment->ReceivesQueued( settings );
	if ( impairment != NULL && impairment->SendsQueued( settings ) ) {
		// Sends that fell due since the last send leave now.
		const int ipv4Socket = netSocket;
		const int ipv6Socket = netSocket6;
		impairment->FlushSends( now, [ipv4Socket, ipv6Socket]( const netadr_t &to, const unsigned char *bytes, int length ) {
			Net_SendDatagram( ipv4Socket, ipv6Socket, to, bytes, length );
		} );
	}

	bool ipv6First = ( packetsRead & 1 ) != 0;
	for ( ;; ipv6First = !ipv6First ) {
		const int firstSocket = ipv6First ? netSocket6 : netSocket;
		const int secondSocket = ipv6First ? netSocket : netSocket6;
		const char *firstContext = ipv6First ? "idPort::GetPacket IPv6" : "idPort::GetPacket IPv4";
		const char *secondContext = ipv6First ? "idPort::GetPacket IPv4" : "idPort::GetPacket IPv6";

		if ( !Net_GetPacketFromSocket( firstSocket, firstContext, net_from, data, size, maxSize ) &&
				!Net_GetPacketFromSocket( secondSocket, secondContext, net_from, data, size, maxSize ) ) {
			break;
		}
		if ( !queueReceives ) {
			packetsRead++;
			bytesRead += size;
			return true;
		}
		// Simulated conditions: every received datagram waits in the receive line.
		if ( impairment->receive.Offer( settings, now, net_from, data, size, Net_ImpairmentIPv6( net_from ) ) > 0 ) {
			packetsRead++;
			bytesRead += size;
		}
	}

	if ( !queueReceives ) {
		return false;
	}
	memset( &net_from, 0, sizeof( net_from ) );
	net_from.type = NA_BAD;
	return impairment->receive.Pop( now, net_from, data, maxSize, size );
}

/*
==================
idPort::GetPacketBlocking
==================
*/
bool idPort::GetPacketBlocking( netadr_t &net_from, void *data, int &size, int maxSize, int timeout ) {
	fd_set				set;
	struct timeval		tv;
	int					ret;
	
	size = 0;
	memset( &net_from, 0, sizeof( net_from ) );
	net_from.type = NA_BAD;
	if ( !netSocket && !netSocket6 ) {
		return false;
	}
	if ( data == NULL || maxSize <= 0 ) {
		size = 0;
		return false;
	}

	if ( timeout < 0 ) {
		return GetPacket( net_from, data, size, maxSize );
	}

	FD_ZERO( &set );
	int maxSocket = -1;
	if ( netSocket ) {
		FD_SET( netSocket, &set );
		maxSocket = netSocket;
	}
	if ( netSocket6 ) {
		FD_SET( netSocket6, &set );
		if ( netSocket6 > maxSocket ) {
			maxSocket = netSocket6;
		}
	}

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = ( timeout % 1000 ) * 1000;
	ret = select( maxSocket + 1, &set, NULL, NULL, &tv );
	if ( ret == -1 ) {
		if ( errno == EINTR ) {
			common->DPrintf( "idPort::GetPacketBlocking: select EINTR\n" );
			return false;
		} else {
			common->Error( "idPort::GetPacketBlocking: select failed: %s\n", strerror( errno ) );
		}
	}

	if ( ret == 0 ) {
		// timed out
		return false;
	}

	// Callers re-enter this in a loop, so it needs the same rotation GetPacket
	// uses: reading IPv4 first every time lets a flood on one family keep the
	// other from ever being serviced.
	const bool ipv6First = ( packetsRead & 1 ) != 0;
	const int firstSocket = ipv6First ? netSocket6 : netSocket;
	const int secondSocket = ipv6First ? netSocket : netSocket6;
	const char *firstContext = ipv6First ? "idPort::GetPacketBlocking IPv6" : "idPort::GetPacketBlocking IPv4";
	const char *secondContext = ipv6First ? "idPort::GetPacketBlocking IPv4" : "idPort::GetPacketBlocking IPv6";

	if ( firstSocket && FD_ISSET( firstSocket, &set ) ) {
		if ( Net_GetPacketFromSocket( firstSocket, firstContext, net_from, data, size, maxSize ) ) {
			packetsRead++;
			bytesRead += size;
			return true;
		}
	}
	if ( secondSocket && FD_ISSET( secondSocket, &set ) ) {
		if ( Net_GetPacketFromSocket( secondSocket, secondContext, net_from, data, size, maxSize ) ) {
			packetsRead++;
			bytesRead += size;
			return true;
		}
	}
	return false;
}

/*
==================
idPort::SendPacket
==================
*/
/*
==================
Net_SendMulticast6Packet

A link-local multicast datagram leaves through exactly one interface. The
kernel picks it from the routing table when the scope is zero, so a multi-homed
host would only ever scan one link. Repeating the send once per local IPv6
scope covers every attached link without touching socket options between sends.
==================
*/
static void Net_SendMulticast6Packet( int socketFd, const void *data, int size, const netadr_t to ) {
	struct sockaddr_in6 group;
	if ( !Sys_MulticastGroupSockadr( to.port, &group ) ) {
		common->DPrintf( "idPort::SendPacket: could not resolve the multicast group '%s'\n", net_mcast6addr.GetString() );
		return;
	}

	// An explicit net_mcast6iface, or a group whose own scope is already set,
	// names the one link the operator asked for.
	if ( group.sin6_scope_id != 0 ) {
		sendto( socketFd, data, size, 0, reinterpret_cast<struct sockaddr *>( &group ), sizeof( group ) );
		return;
	}

	int sends = 0;
	for ( int i = 0; i < num_interfaces6; i++ ) {
		if ( netint6[i].scopeId == 0 ) {
			continue;
		}
		bool duplicate = false;
		for ( int previous = 0; previous < i; previous++ ) {
			if ( netint6[previous].scopeId == netint6[i].scopeId ) {
				duplicate = true;
				break;
			}
		}
		if ( duplicate ) {
			continue;
		}
		group.sin6_scope_id = netint6[i].scopeId;
		sendto( socketFd, data, size, 0, reinterpret_cast<struct sockaddr *>( &group ), sizeof( group ) );
		sends++;
	}

	if ( sends == 0 ) {
		// No enumerated scope, so let the routing table choose.
		group.sin6_scope_id = 0;
		sendto( socketFd, data, size, 0, reinterpret_cast<struct sockaddr *>( &group ), sizeof( group ) );
	}
}

void idPort::SendPacket( const netadr_t to, const void *data, int size ) {
	if ( to.type == NA_BAD ) {
		common->Warning( "idPort::SendPacket: bad address type NA_BAD - ignored" );
		return;
	}

	if ( size < 0 || ( data == NULL && size > 0 ) ) {
		common->Warning( "idPort::SendPacket: invalid packet buffer - ignored" );
		return;
	}
	const char emptyPacket = '\0';
	const void *packetData = data != NULL ? data : &emptyPacket;

	// The discovery group fans out over every attached link, so it does not go
	// through the single-destination path below.
	if ( to.type == NA_MULTICAST6 ) {
		if ( !netSocket6 ) {
			common->DPrintf( "idPort::SendPacket: no IPv6 socket for %s - ignored\n", Sys_NetAdrToString( to ) );
			return;
		}
		Net_SendMulticast6Packet( netSocket6, packetData, size, to );
		packetsWritten++;
		bytesWritten += size;
		return;
	}

	// Simulated conditions: the datagram waits in the send line, routed by its own
	// family when it falls due.
	netImpairmentPort_t *impairment = static_cast<netImpairmentPort_t *>( platformData );
	const idNetworkImpairment::settings_t settings = Net_ImpairmentSettings();
	if ( impairment != NULL && impairment->SendsQueued( settings ) ) {
		const int now = Sys_Milliseconds();
		impairment->send.Offer( settings, now, to, packetData, size, Net_ImpairmentIPv6( to ) );
		const int ipv4Socket = netSocket;
		const int ipv6Socket = netSocket6;
		impairment->FlushSends( now, [ipv4Socket, ipv6Socket]( const netadr_t &address, const unsigned char *bytes, int length ) {
			Net_SendDatagram( ipv4Socket, ipv6Socket, address, bytes, length );
		} );
		packetsWritten++;
		bytesWritten += size;
		return;
	}

	if ( Net_SendDatagram( netSocket, netSocket6, to, packetData, size ) ) {
		packetsWritten++;
		bytesWritten += size;
	}
}

/*
==================
idPort::InitForPort
==================
*/
bool idPort::InitForPort( int portNumber ) {
	Close();
	if ( portNumber != PORT_ANY && ( portNumber < 0 || portNumber > 65535 ) ) {
		common->Warning( "idPort::InitForPort: invalid network port %d", portNumber );
		return false;
	}

	if ( !Net_BindDualStack( net_ip.GetString(), net_ip6.GetString(), portNumber, netSocket, netSocket6, bound_to ) ) {
		netSocket = 0;
		netSocket6 = 0;
		memset( &bound_to, 0, sizeof( bound_to ) );
		return false;
	}

	platformData = Net_OpenImpairment( bound_to.port );

	return true;
}

//=============================================================================

/*
==================
idTCP::idTCP
==================
*/
idTCP::idTCP() {
	fd = 0;
	memset(&address, 0, sizeof(address));
}

/*
==================
idTCP::~idTCP
==================
*/
idTCP::~idTCP() {
	Close();
}

/*
==================
idTCP::Init
==================
*/
bool idTCP::Init( const char *host, int port ) {
	struct sockaddr_storage sadr;
	socklen_t sadrLen;

	if ( !Sys_ResolveSockaddr( host, true, AF_UNSPEC, SOCK_STREAM, port, &sadr, &sadrLen ) ) {
		common->Printf( "Couldn't resolve server name \"%s\"\n", host ? host : "" );
		return false;
	}
	if ( !SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &sadr ), &address ) ) {
		common->Printf( "Couldn't resolve server name \"%s\" to a supported address family\n", host ? host : "" );
		return false;
	}
	common->Printf( "\"%s\" resolved to %s\n", host ? host : "", Sys_NetAdrToString( address ) );

	if (fd) {
		common->Warning("idTCP::Init: already initialized?\n");
		Close();
	}
		
	if ((fd = socket( sadr.ss_family, SOCK_STREAM, IPPROTO_TCP )) == -1) {
		fd = 0;
		common->Printf("ERROR: idTCP::Init: socket: %s\n", strerror(errno));
		return false;
	}
	fd = Sys_KeepSocketFdOutOfStdioRange( fd );
	if ( fd == -1 ) {
		fd = 0;
		return false;
	}
	
	if ( connect( fd, reinterpret_cast<const sockaddr *>( &sadr ), sadrLen ) == -1 ) {
		common->Printf( "ERROR: idTCP::Init: connect: %s\n", strerror( errno ) );		
		close( fd );
		fd = 0;
		return false;
	}
	
	int status;
	if ((status = fcntl(fd, F_GETFL, 0)) != -1) {
	    status |= O_NONBLOCK; /* POSIX */
	    status = fcntl(fd, F_SETFL, status);
	}
	if (status == -1) {
		common->Printf("ERROR: idTCP::Init: fcntl / O_NONBLOCK: %s\n", strerror(errno));
		close(fd);
		fd = 0;
		return false;
	}
	
	common->DPrintf("Opened TCP connection\n");
	return true;
}

/*
==================
idTCP::Close
==================
*/
void idTCP::Close() {
	if (fd) {
		close(fd);
	}
	fd = 0;
}

/*
==================
idTCP::Read
==================
*/
int idTCP::Read(void *data, int size) {
	int nbytes;
	
	if (!fd) {
		common->Printf("idTCP::Read: not initialized\n");
		return -1;
	}
	if ( size <= 0 ) {
		return 0;
	}
	if ( data == NULL ) {
		common->Printf("idTCP::Read: invalid buffer\n");
		return -1;
	}

#if defined(_GNU_SOURCE)
	// handle EINTR interrupted system call with TEMP_FAILURE_RETRY -  this is probably GNU libc specific
	if ( ( nbytes = TEMP_FAILURE_RETRY( read( fd, data, size ) ) ) == -1 ) {
#else
	do {
	  nbytes = read( fd, data, size );
	} while ( nbytes == -1 && errno == EINTR );
	if ( nbytes == -1 ) {
#endif
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return 0;
		}
		common->Printf("ERROR: idTCP::Read: %s\n", strerror(errno));
		Close();
		return -1;
	}
	
	// a successful read of 0 bytes indicates remote has closed the connection
	if ( nbytes == 0 ) {
		common->DPrintf( "idTCP::Read: read 0 bytes - assume connection closed\n" );
		Close();
		return -1;
	}
	
	return nbytes;
}

/*
==================
idTCP::Write
==================
*/

int	idTCP::Write(void *data, int size) {
	int nbytes;
	
	if ( !fd ) {
		common->Printf( "idTCP::Write: not initialized\n");
		return -1;
	}
	if ( size <= 0 ) {
		return 0;
	}
	if ( data == NULL ) {
		common->Printf( "idTCP::Write: invalid buffer\n" );
		return -1;
	}

#if defined(_GNU_SOURCE)	
	// handle EINTR interrupted system call with TEMP_FAILURE_RETRY -  this is probably GNU libc specific
	#if defined( MSG_NOSIGNAL )
	if ( ( nbytes = TEMP_FAILURE_RETRY( send( fd, data, size, MSG_NOSIGNAL ) ) ) == -1 ) {
	#else
	if ( ( nbytes = TEMP_FAILURE_RETRY( write( fd, data, size ) ) ) == -1 ) {
	#endif
#else
	  do {
	#if defined( MSG_NOSIGNAL )
	    nbytes = send( fd, data, size, MSG_NOSIGNAL );
	#else
	    nbytes = write( fd, data, size );
	#endif
	  } while ( nbytes == -1 && errno == EINTR );
	  if ( nbytes == -1 ) {
#endif
		if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
			return 0;
		}
		common->Printf( "ERROR: idTCP::Write: %s\n", strerror( errno ) );
		Close();
		return -1;
	}

	return nbytes;	
}
