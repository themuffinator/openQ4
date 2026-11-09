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



#include <iptypes.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#include "win_local.h"
#include "../NetworkEndpoint.h"
#include "../NetworkImpairment.h"

static WSADATA	winsockdata;
static bool	winsockInitialized = false;
static bool usingSocks = false;

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
idCVar net_forceLatency( "net_forceLatency", "0", CVAR_SYSTEM | CVAR_INTEGER, "milliseconds latency" );
idCVar net_forceDrop( "net_forceDrop", "0", CVAR_SYSTEM | CVAR_INTEGER, "percentage packet loss" );
// openQ4: the rest of a real link, for testing on one machine (NetworkImpairment.h).
idCVar net_forceJitter( "net_forceJitter", "0", CVAR_SYSTEM | CVAR_INTEGER, "random extra milliseconds of latency per packet; packets closer together than this arrive out of order" );
idCVar net_forceDuplicate( "net_forceDuplicate", "0", CVAR_SYSTEM | CVAR_INTEGER, "percentage of packets delivered twice" );
idCVar net_forceSeed( "net_forceSeed", "0", CVAR_SYSTEM | CVAR_INTEGER, "seed for simulated loss, jitter and duplication when a port opens, 0 picks a new one" );
idCVar net_forceRate( "net_forceRate", "0", CVAR_SYSTEM | CVAR_INTEGER, "simulated bottleneck in bytes per second, each way, shared by everything on the port; 0 for none" );
idCVar net_forceQueue( "net_forceQueue", "250", CVAR_SYSTEM | CVAR_INTEGER, "milliseconds of traffic the simulated bottleneck holds before it drops packets" );
idCVar net_forceDropBurst( "net_forceDropBurst", "0", CVAR_SYSTEM | CVAR_INTEGER, "mean number of packets lost in a row; 0 or 1 loses packets independently" );

// Retain the legacy CVars for configuration compatibility, but do not expose
// the inherited SOCKS UDP relay as supported. It assumes one global IPv4
// socket and does not satisfy idPort's current lifecycle or test contracts.
idCVar net_socksEnabled( "net_socksEnabled", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "" );
idCVar net_socksServer( "net_socksServer", "", CVAR_SYSTEM | CVAR_ARCHIVE, "" );
idCVar net_socksPort( "net_socksPort", "1080", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "" );
idCVar net_socksUsername( "net_socksUsername", "", CVAR_SYSTEM | CVAR_ARCHIVE, "" );
idCVar net_socksPassword( "net_socksPassword", "", CVAR_SYSTEM | CVAR_ARCHIVE, "" );


static struct sockaddr	socksRelayAddr;

static SOCKET	ip_socket = INVALID_SOCKET;
static SOCKET	socks_socket = INVALID_SOCKET;
static char		socksBuf[4096];

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
int				num_interfaces6 = 0;
net_interface6	netint6[MAX_INTERFACES];

//=============================================================================


/*
====================
NET_ErrorString
====================
*/
const char *NET_ErrorString( void ) {
	int		code;

	code = WSAGetLastError();
	switch( code ) {
	case WSAEINTR: return "WSAEINTR";
	case WSAEBADF: return "WSAEBADF";
	case WSAEACCES: return "WSAEACCES";
	case WSAEDISCON: return "WSAEDISCON";
	case WSAEFAULT: return "WSAEFAULT";
	case WSAEINVAL: return "WSAEINVAL";
	case WSAEMFILE: return "WSAEMFILE";
	case WSAEWOULDBLOCK: return "WSAEWOULDBLOCK";
	case WSAEINPROGRESS: return "WSAEINPROGRESS";
	case WSAEALREADY: return "WSAEALREADY";
	case WSAENOTSOCK: return "WSAENOTSOCK";
	case WSAEDESTADDRREQ: return "WSAEDESTADDRREQ";
	case WSAEMSGSIZE: return "WSAEMSGSIZE";
	case WSAEPROTOTYPE: return "WSAEPROTOTYPE";
	case WSAENOPROTOOPT: return "WSAENOPROTOOPT";
	case WSAEPROTONOSUPPORT: return "WSAEPROTONOSUPPORT";
	case WSAESOCKTNOSUPPORT: return "WSAESOCKTNOSUPPORT";
	case WSAEOPNOTSUPP: return "WSAEOPNOTSUPP";
	case WSAEPFNOSUPPORT: return "WSAEPFNOSUPPORT";
	case WSAEAFNOSUPPORT: return "WSAEAFNOSUPPORT";
	case WSAEADDRINUSE: return "WSAEADDRINUSE";
	case WSAEADDRNOTAVAIL: return "WSAEADDRNOTAVAIL";
	case WSAENETDOWN: return "WSAENETDOWN";
	case WSAENETUNREACH: return "WSAENETUNREACH";
	case WSAENETRESET: return "WSAENETRESET";
	case WSAECONNABORTED: return "WSAECONNABORTED";
	case WSAECONNRESET: return "WSAECONNRESET";
	case WSAENOBUFS: return "WSAENOBUFS";
	case WSAEISCONN: return "WSAEISCONN";
	case WSAENOTCONN: return "WSAENOTCONN";
	case WSAESHUTDOWN: return "WSAESHUTDOWN";
	case WSAETOOMANYREFS: return "WSAETOOMANYREFS";
	case WSAETIMEDOUT: return "WSAETIMEDOUT";
	case WSAECONNREFUSED: return "WSAECONNREFUSED";
	case WSAELOOP: return "WSAELOOP";
	case WSAENAMETOOLONG: return "WSAENAMETOOLONG";
	case WSAEHOSTDOWN: return "WSAEHOSTDOWN";
	case WSASYSNOTREADY: return "WSASYSNOTREADY";
	case WSAVERNOTSUPPORTED: return "WSAVERNOTSUPPORTED";
	case WSANOTINITIALISED: return "WSANOTINITIALISED";
	case WSAHOST_NOT_FOUND: return "WSAHOST_NOT_FOUND";
	case WSATRY_AGAIN: return "WSATRY_AGAIN";
	case WSANO_RECOVERY: return "WSANO_RECOVERY";
	case WSANO_DATA: return "WSANO_DATA";
	default: return "NO ERROR";
	}
}

/*
====================
Sys_SocketFamilyName
====================
*/
static const char *Sys_SocketFamilyName( int family ) {
	return family == AF_INET6 ? "IPv6" : "IPv4";
}

/*
====================
Sys_IsAnyInterfaceName

An empty net_ip, or the inherited "localhost" default, means "every local
interface" rather than a literal loopback bind.
====================
*/
static bool Sys_IsAnyInterfaceName( const char *net_interface ) {
	return idNetworkEndpoint::IsAnyInterfaceName( net_interface );
}

/*
=============
Net_ResolveSockaddrs

Resolves an endpoint into every usable sockaddr of the requested family, in
the order a connection should try them (idNetworkEndpoint::OrderCandidates).
AF_UNSPEC keeps the caller's grammar intact while putting an A record first so
the legacy IPv4 transports keep their historical behaviour for dual-stack
hostnames. Returns how many addresses were written.
=============
*/
static int Net_ResolveSockaddrs( const char *text, struct sockaddr_storage *addresses, int *addressLens, int maxAddresses, bool doDNSResolve, int family = AF_UNSPEC, int defaultPort = 0, int socketType = SOCK_DGRAM ) {
	if ( addresses == NULL || addressLens == NULL || maxAddresses <= 0 ) {
		return 0;
	}

	char host[NI_MAXHOST];
	idNetworkEndpoint::endpointParts_t endpoint;
	if ( !idNetworkEndpoint::Split( text, host, sizeof( host ), endpoint ) ) {
		return 0;
	}

	// An explicit endpoint port owns the result. Validate the default only when
	// it is actually used so host:port can safely override an unused sentinel.
	const int effectivePort = endpoint.hasPort ? endpoint.port : defaultPort;
	if ( effectivePort < 0 || effectivePort > 65535 ) {
		return 0;
	}
	char service[NI_MAXSERV];
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
	hints.ai_socktype = socketType;
	hints.ai_protocol = socketType == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
	// The service text is always the decimal port built above, so keep the
	// resolver out of the services database. AI_ADDRCONFIG belongs only on the
	// DNS path: on the numeric path it would reject a literal ::1 on a host
	// with no configured IPv6 interface, which is a legitimate address.
	hints.ai_flags = AI_NUMERICSERV | ( doDNSResolve ? AI_ADDRCONFIG : AI_NUMERICHOST );

	struct addrinfo *results = NULL;
	const int error = getaddrinfo( host, service, &hints, &results );
	if ( error != 0 ) {
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
		if ( result->ai_addrlen > sizeof( *addresses ) ) {
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

	// An explicit family takes its records in resolver order; AF_UNSPEC puts
	// the first A record ahead of any AAAA record.
	int order[idNetworkEndpoint::MAX_RESOLVER_RESULTS];
	const int maxOrder = maxAddresses < idNetworkEndpoint::MAX_RESOLVER_RESULTS ? maxAddresses : idNetworkEndpoint::MAX_RESOLVER_RESULTS;
	const int ordered = idNetworkEndpoint::OrderCandidates( families, usableCount, family == AF_UNSPEC, order, maxOrder );
	for ( int i = 0; i < ordered; i++ ) {
		const struct addrinfo *selected = usable[order[i]];
		memset( &addresses[i], 0, sizeof( addresses[i] ) );
		memcpy( &addresses[i], selected->ai_addr, selected->ai_addrlen );
		addressLens[i] = static_cast<int>( selected->ai_addrlen );
	}

	freeaddrinfo( results );
	return ordered;
}

/*
=============
Net_StringToSockaddr

Resolves an endpoint into the one sockaddr a single-address caller uses: the
first of Net_ResolveSockaddrs' candidates.
=============
*/
static bool Net_StringToSockaddr( const char *text, struct sockaddr_storage *address, int *addressLen, bool doDNSResolve, int family = AF_UNSPEC, int defaultPort = 0, int socketType = SOCK_DGRAM ) {
	if ( address == NULL || addressLen == NULL ) {
		return false;
	}
	memset( address, 0, sizeof( *address ) );
	*addressLen = 0;
	return Net_ResolveSockaddrs( text, address, addressLen, 1, doDNSResolve, family, defaultPort, socketType ) == 1;
}

/*
====================
Net_MulticastGroupSockadr

Resolves the configured LAN discovery group. IPv6 has no broadcast address, so
a link-local all-nodes multicast send replaces the IPv4 broadcast scan.
====================
*/
static bool Net_MulticastGroupSockadr( unsigned short port, struct sockaddr_in6 *group ) {
	if ( group == NULL ) {
		return false;
	}
	memset( group, 0, sizeof( *group ) );

	struct sockaddr_storage resolved;
	int resolvedLen = 0;
	if ( !Net_StringToSockaddr( net_mcast6addr.GetString(), &resolved, &resolvedLen, false, AF_INET6 ) ) {
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
		group->sin6_scope_id = static_cast<unsigned long>( configuredInterface );
	}
	return true;
}

/*
====================
Net_JoinDiscoveryGroup

Windows hands a socket only the multicast of groups that socket has joined,
the link-local all-nodes group included: the host's own membership of ff02::1
is not enough, unlike on Linux. So the discovery group, even the default
ff02::1, is joined explicitly on every link, or a server never receives a LAN
scan sent to it.
====================
*/
static void Net_JoinDiscoveryGroup( SOCKET newsocket ) {
	struct sockaddr_in6 group;
	if ( !Net_MulticastGroupSockadr( 0, &group ) ) {
		return;
	}

	unsigned char groupAddress[16];
	memcpy( groupAddress, &group.sin6_addr, sizeof( groupAddress ) );
	if ( !idNetworkEndpoint::IsIPv6Multicast( groupAddress ) ) {
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
		if ( setsockopt( newsocket, IPPROTO_IPV6, IPV6_JOIN_GROUP, (const char *)&request, sizeof( request ) ) != SOCKET_ERROR ) {
			joins++;
		}
	}

	if ( joins == 0 ) {
		// No enumerated link, so let the routing table choose one.
		request.ipv6mr_interface = 0;
		if ( setsockopt( newsocket, IPPROTO_IPV6, IPV6_JOIN_GROUP, (const char *)&request, sizeof( request ) ) == SOCKET_ERROR ) {
			common->DPrintf( "UDP_OpenSocket: could not join the discovery group '%s': %s\n", net_mcast6addr.GetString(), NET_ErrorString() );
		}
	}
}

/*
====================
Net_NetadrToSockadr
====================
*/
static bool Net_NetadrToSockadr( const netadr_t *a, struct sockaddr_storage *s, int *slen ) {
	if ( a == NULL || s == NULL || slen == NULL ) {
		return false;
	}

	memset( s, 0, sizeof( *s ) );
	*slen = 0;

	if ( a->type == NA_BROADCAST ) {
		struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>( s );
		ipv4->sin_family = AF_INET;
		ipv4->sin_port = htons( a->port );
		ipv4->sin_addr.s_addr = INADDR_BROADCAST;
		*slen = sizeof( *ipv4 );
		return true;
	}
	if ( a->type == NA_IP || a->type == NA_LOOPBACK ) {
		struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>( s );
		ipv4->sin_family = AF_INET;
		ipv4->sin_port = htons( a->port );
		if ( a->type == NA_LOOPBACK ) {
			ipv4->sin_addr.s_addr = htonl( INADDR_LOOPBACK );
		} else {
			memcpy( &ipv4->sin_addr.s_addr, a->ip, sizeof( a->ip ) );
		}
		*slen = sizeof( *ipv4 );
		return true;
	}
	if ( a->type == NA_IP6 ) {
		struct sockaddr_in6 *ipv6 = reinterpret_cast<struct sockaddr_in6 *>( s );
		ipv6->sin6_family = AF_INET6;
		ipv6->sin6_port = htons( a->port );
		memcpy( &ipv6->sin6_addr, a->ip6, sizeof( a->ip6 ) );
		ipv6->sin6_scope_id = a->scopeId;
		*slen = sizeof( *ipv6 );
		return true;
	}
	if ( a->type == NA_MULTICAST6 ) {
		struct sockaddr_in6 group;
		if ( !Net_MulticastGroupSockadr( a->port, &group ) ) {
			return false;
		}
		memcpy( s, &group, sizeof( group ) );
		*slen = sizeof( group );
		return true;
	}

	return false;
}


/*
====================
Net_SockadrToNetadr
====================
*/
static bool Net_SockadrToNetadr( const struct sockaddr *s, netadr_t *a ) {
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
		a->scopeId = static_cast<unsigned int>( ipv6->sin6_scope_id );
		a->port = ntohs( ipv6->sin6_port );
		a->type = NA_IP6;
		// A dual-stack peer that reaches us through ::ffff:a.b.c.d is an IPv4
		// peer. Normalizing here keeps address comparisons, bans, and server
		// lists from holding two identities for one host.
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
====================
NET_IPSocketForFamily
====================
*/
static SOCKET NET_IPSocketForFamily( const char *net_interface, int port, int family, netadr_t *bound_to = NULL, bool quiet = false, bool *addressResolved = NULL ) {
	SOCKET				newsocket;
	struct sockaddr_storage	address;
	int					addressLen = 0;
	unsigned long		_true = 1;
	int					i = 1;
	int					err;

	if ( addressResolved != NULL ) {
		*addressResolved = false;
	}
	if ( bound_to != NULL ) {
		memset( bound_to, 0, sizeof( *bound_to ) );
		bound_to->type = NA_BAD;
	}
	if ( family != AF_INET && family != AF_INET6 ) {
		return INVALID_SOCKET;
	}
	if ( port != PORT_ANY && ( port < 0 || port > 65535 ) ) {
		if ( !quiet ) {
			common->Printf( "WARNING: UDP_OpenSocket: invalid port %d\n", port );
		}
		return INVALID_SOCKET;
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
		common->DPrintf( "Opening %s UDP socket: %s\n", Sys_SocketFamilyName( family ), endpointText );
	}

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
	} else if ( !Net_StringToSockaddr( net_interface, &address, &addressLen, true, family, bindPort ) ) {
		if ( !quiet ) {
			common->Printf( "WARNING: UDP_OpenSocket: invalid %s interface address '%s'\n", Sys_SocketFamilyName( family ), net_interface );
		}
		return INVALID_SOCKET;
	} else {
		if ( addressResolved != NULL ) {
			*addressResolved = true;
		}
		// Net_StringToSockaddr honours a port inside the interface text, which
		// must not be allowed to override the requested bind port.
		if ( address.ss_family == AF_INET6 ) {
			reinterpret_cast<struct sockaddr_in6 *>( &address )->sin6_port = htons( static_cast<unsigned short>( bindPort ) );
		} else {
			reinterpret_cast<struct sockaddr_in *>( &address )->sin_port = htons( static_cast<unsigned short>( bindPort ) );
		}
	}

	if( ( newsocket = socket( family, SOCK_DGRAM, IPPROTO_UDP ) ) == INVALID_SOCKET ) {
		err = WSAGetLastError();
		if( err != WSAEAFNOSUPPORT && !quiet ) {
			common->Printf( "WARNING: UDP_OpenSocket: socket: %s\n", NET_ErrorString() );
		}
		return INVALID_SOCKET;
	}

	// make it non-blocking
	if( ioctlsocket( newsocket, FIONBIO, &_true ) == SOCKET_ERROR ) {
		if ( !quiet ) {
			common->Printf( "WARNING: UDP_OpenSocket: ioctl FIONBIO: %s\n", NET_ErrorString() );
		}
		closesocket( newsocket );
		return INVALID_SOCKET;
	}

	if ( family == AF_INET ) {
		// make it broadcast capable
		if( setsockopt( newsocket, SOL_SOCKET, SO_BROADCAST, (char *)&i, sizeof(i) ) == SOCKET_ERROR ) {
			if ( !quiet ) {
				common->Printf( "WARNING: UDP_OpenSocket: setsockopt SO_BROADCAST: %s\n", NET_ErrorString() );
			}
			closesocket( newsocket );
			return INVALID_SOCKET;
		}
	} else {
		// Keep the IPv6 socket off the IPv4 mapped range so both families can
		// hold the same port and so an IPv4 peer always arrives on the socket
		// whose broadcast and interface configuration matches it.
		if( setsockopt( newsocket, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&i, sizeof(i) ) == SOCKET_ERROR ) {
			if ( !quiet ) {
				common->Printf( "WARNING: UDP_OpenSocket: setsockopt IPV6_V6ONLY: %s\n", NET_ErrorString() );
			}
			closesocket( newsocket );
			return INVALID_SOCKET;
		}
		// Link-local discovery only ever needs one hop; a larger default would
		// leak scan traffic past the local segment.
		const int multicastHops = 1;
		if( setsockopt( newsocket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const char *)&multicastHops, sizeof( multicastHops ) ) == SOCKET_ERROR ) {
			common->DPrintf( "UDP_OpenSocket: setsockopt IPV6_MULTICAST_HOPS: %s\n", NET_ErrorString() );
		}
		const int multicastInterface = net_mcast6iface.GetInteger();
		if ( multicastInterface > 0 ) {
			const DWORD interfaceIndex = static_cast<DWORD>( multicastInterface );
			if( setsockopt( newsocket, IPPROTO_IPV6, IPV6_MULTICAST_IF, (const char *)&interfaceIndex, sizeof( interfaceIndex ) ) == SOCKET_ERROR ) {
				common->DPrintf( "UDP_OpenSocket: setsockopt IPV6_MULTICAST_IF: %s\n", NET_ErrorString() );
			}
		}
		Net_JoinDiscoveryGroup( newsocket );
	}

	if( bind( newsocket, (const struct sockaddr *)&address, addressLen ) == SOCKET_ERROR ) {
		if ( !quiet ) {
			common->Printf( "WARNING: UDP_OpenSocket: bind: %s\n", NET_ErrorString() );
		}
		closesocket( newsocket );
		return INVALID_SOCKET;
	}

	if ( quiet ) {
		common->DPrintf( "Opening %s UDP socket: %s\n", Sys_SocketFamilyName( family ), endpointText );
	}

	// if the port was PORT_ANY, we need to query again to know the real port we got bound to
	// ( this used to be in idPort::InitForPort )
	if ( bound_to ) {
		struct sockaddr_storage boundAddress;
		memset( &boundAddress, 0, sizeof( boundAddress ) );
		int len = sizeof( boundAddress );
		if ( getsockname( newsocket, reinterpret_cast<sockaddr *>( &boundAddress ), &len ) == SOCKET_ERROR || !Net_SockadrToNetadr( reinterpret_cast<const sockaddr *>( &boundAddress ), bound_to ) ) {
			common->Printf( "WARNING: UDP_OpenSocket: getsockname: %s\n", NET_ErrorString() );
			closesocket( newsocket );
			return INVALID_SOCKET;
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
static bool Net_BindDualStack( const char *ipv4Text, const char *ipv6Text, int portNumber, SOCKET &socket4, SOCKET &socket6, netadr_t &bound_to ) {
	socket4 = INVALID_SOCKET;
	socket6 = INVALID_SOCKET;
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
		socket4 = NET_IPSocketForFamily( plan.ipv4Interface, portNumber, AF_INET, &bound4, true, &ipv4Resolved );
		if ( socket4 == INVALID_SOCKET ) {
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
		const int ipv6Port = ( portNumber == PORT_ANY && socket4 != INVALID_SOCKET ) ? bound4.port : portNumber;
		socket6 = NET_IPSocketForFamily( ipv6Interface, ipv6Port, AF_INET6, &bound6, true );

		// The kernel picked that ephemeral port for IPv4 alone, so another
		// process may already hold it on IPv6. Clients bind with PORT_ANY, and
		// accepting the half-bound result would leave them quietly unable to
		// reach any IPv6 server. Try a few other ephemeral ports before
		// settling for IPv4 only. An explicit port is the operator's choice and
		// is never retried.
		for ( int attempt = 0; socket6 == INVALID_SOCKET && attempt < 3 && portNumber == PORT_ANY &&
				plan.bindIPv4 && socket4 != INVALID_SOCKET; attempt++ ) {
			netadr_t retryBound;
			memset( &retryBound, 0, sizeof( retryBound ) );
			// Open the replacement before releasing the old socket so a failed
			// retry cannot cost us the IPv4 socket we already hold.
			const SOCKET retrySocket4 = NET_IPSocketForFamily( plan.ipv4Interface, PORT_ANY, AF_INET, &retryBound, true );
			if ( retrySocket4 == INVALID_SOCKET ) {
				break;
			}
			const SOCKET retrySocket6 = NET_IPSocketForFamily( ipv6Interface, retryBound.port, AF_INET6, &bound6, true );
			if ( retrySocket6 == INVALID_SOCKET ) {
				closesocket( retrySocket4 );
				continue;
			}
			closesocket( socket4 );
			socket4 = retrySocket4;
			bound4 = retryBound;
			bound_to = bound4;
			socket6 = retrySocket6;
		}

		if ( socket6 != INVALID_SOCKET && socket4 == INVALID_SOCKET ) {
			bound_to = bound6;
		}
	}

	if ( socket4 == INVALID_SOCKET && socket6 == INVALID_SOCKET ) {
		memset( &bound_to, 0, sizeof( bound_to ) );
		return false;
	}
	return true;
}

/*
====================
Net_SocketForAddress

Routes an outgoing address to the socket of its own family. There is no
fallback: sending an IPv6 datagram down the IPv4 socket cannot work, and
silently dropping to the wrong family would hide a misconfigured host.
====================
*/
static SOCKET Net_SocketForAddress( SOCKET netSocket, SOCKET netSocket6, const netadr_t to ) {
	if ( to.type == NA_IP6 || to.type == NA_MULTICAST6 ) {
		return netSocket6;
	}
	if ( to.type == NA_IP || to.type == NA_LOOPBACK || to.type == NA_BROADCAST ) {
		return netSocket;
	}
	return INVALID_SOCKET;
}

/*
====================
NET_OpenSocks
====================
*/
void NET_OpenSocks( int port ) {
	struct sockaddr_storage	address;
	int					addressLen = 0;
	int					err;
	int					len;
	bool			rfc1929;
	unsigned char		buf[64];

	usingSocks = false;

	common->Printf( "Opening connection to SOCKS server.\n" );

	if ( ( socks_socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP ) ) == INVALID_SOCKET ) {
		err = WSAGetLastError();
		common->Printf( "WARNING: NET_OpenSocks: socket: %s\n", NET_ErrorString() );
		return;
	}

	if ( !Net_StringToSockaddr( net_socksServer.GetString(), &address, &addressLen, true, AF_INET, net_socksPort.GetInteger(), SOCK_STREAM ) ) {
		common->Printf( "WARNING: NET_OpenSocks: could not resolve IPv4 server '%s'\n", net_socksServer.GetString() );
		closesocket( socks_socket );
		socks_socket = INVALID_SOCKET;
		return;
	}

	if ( connect( socks_socket, (struct sockaddr *)&address, addressLen ) == SOCKET_ERROR ) {
		err = WSAGetLastError();
		common->Printf( "NET_OpenSocks: connect: %s\n", NET_ErrorString() );
		return;
	}

	// send socks authentication handshake
	if ( *net_socksUsername.GetString() || *net_socksPassword.GetString() ) {
		rfc1929 = true;
	}
	else {
		rfc1929 = false;
	}

	buf[0] = 5;		// SOCKS version
	// method count
	if ( rfc1929 ) {
		buf[1] = 2;
		buf[2] = 0;		// method #1 - method id #00: no authentication
		buf[3] = 2;		// method #2 - method id #02: username/password
		len = 4;
	}
	else {
		buf[1] = 1;
		buf[2] = 0;		// method #1 - method id #00: no authentication
		len = 3;
	}
	if ( send( socks_socket, (const char *)buf, len, 0 ) == SOCKET_ERROR ) {
		err = WSAGetLastError();
		common->Printf( "NET_OpenSocks: send: %s\n", NET_ErrorString() );
		return;
	}

	// get the response
	len = recv( socks_socket, (char *)buf, 64, 0 );
	if ( len == SOCKET_ERROR ) {
		err = WSAGetLastError();
		common->Printf( "NET_OpenSocks: recv: %s\n", NET_ErrorString() );
		return;
	}
	if ( len != 2 || buf[0] != 5 ) {
		common->Printf( "NET_OpenSocks: bad response\n" );
		return;
	}
	switch( buf[1] ) {
	case 0:	// no authentication
		break;
	case 2: // username/password authentication
		break;
	default:
		common->Printf( "NET_OpenSocks: request denied\n" );
		return;
	}

	// do username/password authentication if needed
	if ( buf[1] == 2 ) {
		size_t	ulen;
		size_t	plen;

		// build the request
		ulen = strlen( net_socksUsername.GetString() );
		plen = strlen( net_socksPassword.GetString() );
		if ( ulen > 255 || plen > 255 || 3 + ulen + plen > sizeof( buf ) ) {
			common->Printf( "NET_OpenSocks: username/password too long\n" );
			return;
		}

		buf[0] = 1;		// username/password authentication version
		buf[1] = (unsigned char)ulen;
		if ( ulen ) {
			memcpy( &buf[2], net_socksUsername.GetString(), ulen );
		}
		buf[2 + ulen] = (unsigned char)plen;
		if ( plen ) {
			memcpy( &buf[3 + ulen], net_socksPassword.GetString(), plen );
		}

		// send it
		if ( send( socks_socket, (const char *)buf, (int)( 3 + ulen + plen ), 0 ) == SOCKET_ERROR ) {
			err = WSAGetLastError();
			common->Printf( "NET_OpenSocks: send: %s\n", NET_ErrorString() );
			return;
		}

		// get the response
		len = recv( socks_socket, (char *)buf, 64, 0 );
		if ( len == SOCKET_ERROR ) {
			err = WSAGetLastError();
			common->Printf( "NET_OpenSocks: recv: %s\n", NET_ErrorString() );
			return;
		}
		if ( len != 2 || buf[0] != 1 ) {
			common->Printf( "NET_OpenSocks: bad response\n" );
			return;
		}
		if ( buf[1] != 0 ) {
			common->Printf( "NET_OpenSocks: authentication failed\n" );
			return;
		}
	}

	// send the UDP associate request
	buf[0] = 5;		// SOCKS version
	buf[1] = 3;		// command: UDP associate
	buf[2] = 0;		// reserved
	buf[3] = 1;		// address type: IPV4
	const unsigned int anyAddress = INADDR_ANY;
	const unsigned short networkPort = htons( static_cast<unsigned short>( port ) );
	memcpy( &buf[4], &anyAddress, sizeof( anyAddress ) );
	memcpy( &buf[8], &networkPort, sizeof( networkPort ) );
	if ( send( socks_socket, (const char *)buf, 10, 0 ) == SOCKET_ERROR ) {
		err = WSAGetLastError();
		common->Printf( "NET_OpenSocks: send: %s\n", NET_ErrorString() );
		return;
	}

	// get the response
	len = recv( socks_socket, (char *)buf, 64, 0 );
	if( len == SOCKET_ERROR ) {
		err = WSAGetLastError();
		common->Printf( "NET_OpenSocks: recv: %s\n", NET_ErrorString() );
		return;
	}
	if( len < 2 || buf[0] != 5 ) {
		common->Printf( "NET_OpenSocks: bad response\n" );
		return;
	}
	// check completion code
	if( buf[1] != 0 ) {
		common->Printf( "NET_OpenSocks: request denied: %i\n", buf[1] );
		return;
	}
	if( buf[3] != 1 ) {
		common->Printf( "NET_OpenSocks: relay address is not IPV4: %i\n", buf[3] );
		return;
	}
	((struct sockaddr_in *)&socksRelayAddr)->sin_family = AF_INET;
	memcpy( &((struct sockaddr_in *)&socksRelayAddr)->sin_addr.s_addr, &buf[4], sizeof( ((struct sockaddr_in *)&socksRelayAddr)->sin_addr.s_addr ) );
	memcpy( &((struct sockaddr_in *)&socksRelayAddr)->sin_port, &buf[8], sizeof( ((struct sockaddr_in *)&socksRelayAddr)->sin_port ) );
	memset( ((struct sockaddr_in *)&socksRelayAddr)->sin_zero, 0, 8 );

	usingSocks = true;
}

/*
==================
Net_WaitForUDPPacket

Waits on either family. A host may be bound to one of them, both, or neither.
==================
*/
bool Net_WaitForUDPPacket( SOCKET netSocket, SOCKET netSocket6, int timeout ) {
	int					ret;
	fd_set				set;
	struct timeval		tv;

	if ( netSocket == INVALID_SOCKET && netSocket6 == INVALID_SOCKET ) {
		return false;
	}

	if ( timeout <= 0 ) {
		return true;
	}

	FD_ZERO( &set );
	if ( netSocket != INVALID_SOCKET ) {
		FD_SET( netSocket, &set );
	}
	if ( netSocket6 != INVALID_SOCKET ) {
		FD_SET( netSocket6, &set );
	}

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = ( timeout % 1000 ) * 1000;

	ret = select( 0, &set, NULL, NULL, &tv );

	if ( ret == SOCKET_ERROR ) {
		common->DPrintf( "Net_WaitForUDPPacket select(): %s\n", NET_ErrorString() );
		return false;
	}

	// timeout with no data
	if ( ret == 0 ) {
		return false;
	}

	return true;
}

/*
==================
Net_GetUDPPacket
==================
*/
bool Net_GetUDPPacket( SOCKET netSocket, netadr_t &net_from, char *data, int &size, int maxSize ) {
	int 			ret;
	struct sockaddr_storage from;
	int				fromlen;
	int				err;

	size = 0;
	memset( &net_from, 0, sizeof( net_from ) );
	net_from.type = NA_BAD;

	if( netSocket == INVALID_SOCKET ) {
		return false;
	}
	if ( data == NULL || maxSize <= 0 ) {
		return false;
	}

	memset( &from, 0, sizeof( from ) );
	fromlen = sizeof( from );
	ret = recvfrom( netSocket, data, maxSize, 0, reinterpret_cast<struct sockaddr *>( &from ), &fromlen );
	if ( ret == SOCKET_ERROR ) {
		err = WSAGetLastError();

		if( err == WSAEWOULDBLOCK || err == WSAECONNRESET ) {
			return false;
		}
		if ( err == WSAEMSGSIZE ) {
			if ( Net_SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &from ), &net_from ) ) {
				char buf[1024];
				idStr::snPrintf( buf, sizeof( buf ), "Net_GetUDPPacket: oversize packet from %s\n", Sys_NetAdrToString( net_from ) );
				OutputDebugString( buf );
			}
			memset( &net_from, 0, sizeof( net_from ) );
			net_from.type = NA_BAD;
			return false;
		}
		char	buf[1024];
		idStr::snPrintf( buf, sizeof( buf ), "Net_GetUDPPacket: %s\n", NET_ErrorString() );
		OutputDebugString( buf );
		return false;
	}

	if ( usingSocks && netSocket == ip_socket && from.ss_family == AF_INET && memcmp( &from, &socksRelayAddr, sizeof( struct sockaddr_in ) ) == 0 ) {
		if ( ret < 10 || data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 1 ) {
			return false;
		}
		net_from.type = NA_IP;
		net_from.ip[0] = data[4];
		net_from.ip[1] = data[5];
		net_from.ip[2] = data[6];
		net_from.ip[3] = data[7];
		unsigned short networkPort;
		memcpy( &networkPort, &data[8], sizeof( networkPort ) );
		net_from.port = ntohs( networkPort );
		memmove( data, &data[10], ret - 10 );
		ret -= 10;
	} else if ( !Net_SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &from ), &net_from ) ) {
		return false;
	}

	size = ret;

	return true;
}

/*
==================
Net_SendMulticast6Packet

A link-local multicast datagram leaves through exactly one interface. The
kernel picks it from the routing table when the scope is zero, so a multi-homed
host would only ever scan one link. Repeating the send once per local IPv6
scope covers every attached link without touching socket options between sends.
==================
*/
static void Net_SendMulticast6Packet( SOCKET netSocket, int length, const char *data, const netadr_t to ) {
	struct sockaddr_in6 group;
	if ( !Net_MulticastGroupSockadr( to.port, &group ) ) {
		common->DPrintf( "Net_SendUDPPacket: could not resolve the multicast group '%s'\n", net_mcast6addr.GetString() );
		return;
	}

	// An explicit net_mcast6iface, or a group whose own scope is already set,
	// names the one link the operator asked for.
	if ( group.sin6_scope_id != 0 ) {
		sendto( netSocket, data, length, 0, reinterpret_cast<const struct sockaddr *>( &group ), sizeof( group ) );
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
		sendto( netSocket, data, length, 0, reinterpret_cast<const struct sockaddr *>( &group ), sizeof( group ) );
		sends++;
	}

	if ( sends == 0 ) {
		// No enumerated scope, so let the routing table choose.
		group.sin6_scope_id = 0;
		sendto( netSocket, data, length, 0, reinterpret_cast<const struct sockaddr *>( &group ), sizeof( group ) );
	}
}

/*
==================
Net_SendUDPPacket
==================
*/
void Net_SendUDPPacket( SOCKET netSocket, int length, const void *data, const netadr_t to ) {
	int				ret;
	struct sockaddr_storage addr;
	int				addrLen = 0;
	// openQ4: look the cvar up once, not by name for every packet sent
	static idCVar *	netVerbose = NULL;
	if ( netVerbose == NULL && cvarSystem != NULL ) {
		netVerbose = cvarSystem->Find( "net_verbose" );
	}
	const bool verbosePacket = netVerbose != NULL && netVerbose->GetInteger() >= 2;

	if( netSocket == INVALID_SOCKET ) {
		return;
	}
	if ( length < 0 || ( data == NULL && length > 0 ) ) {
		return;
	}

	const char emptyPacket = '\0';
	const char *packetData = data != NULL ? static_cast<const char *>( data ) : &emptyPacket;

	// The discovery group fans out over every attached link, so it does not go
	// through the single-destination path below.
	if ( to.type == NA_MULTICAST6 ) {
		Net_SendMulticast6Packet( netSocket, length, packetData, to );
		if ( verbosePacket ) {
			common->DPrintf( "Net_SendUDPPacket: scanned %d bytes to %s\n", length, Sys_NetAdrToString( to ) );
		}
		return;
	}

	if ( !Net_NetadrToSockadr( &to, &addr, &addrLen ) ) {
		return;
	}

	if( usingSocks && to.type == NA_IP ) {
		if ( length > (int)sizeof( socksBuf ) - 10 ) {
			char	buf[1024];
			idStr::snPrintf( buf, sizeof( buf ), "Net_SendUDPPacket: oversized SOCKS packet to %s\n", Sys_NetAdrToString( to ) );
			OutputDebugString( buf );
			return;
		}
		const struct sockaddr_in *ipv4 = reinterpret_cast<const struct sockaddr_in *>( &addr );
		socksBuf[0] = 0;	// reserved
		socksBuf[1] = 0;
		socksBuf[2] = 0;	// fragment (not fragmented)
		socksBuf[3] = 1;	// address type: IPV4
		memcpy( &socksBuf[4], &ipv4->sin_addr.s_addr, sizeof( ipv4->sin_addr.s_addr ) );
		memcpy( &socksBuf[8], &ipv4->sin_port, sizeof( ipv4->sin_port ) );
		memcpy( &socksBuf[10], packetData, length );
		ret = sendto( netSocket, socksBuf, length+10, 0, &socksRelayAddr, sizeof(socksRelayAddr) );
	} else {
		ret = sendto( netSocket, packetData, length, 0, reinterpret_cast<const struct sockaddr *>( &addr ), addrLen );
	}
	if( ret == SOCKET_ERROR ) {
		int err = WSAGetLastError();

		// wouldblock is silent
		if( err == WSAEWOULDBLOCK ) {
			return;
		}

		// some PPP links do not allow broadcasts and return an error
		if( ( err == WSAEADDRNOTAVAIL ) && ( to.type == NA_BROADCAST ) ) {
			return;
		}

		// a host with no route to the discovery group is not an error worth
		// reporting on every LAN scan
		if( ( err == WSAENETUNREACH || err == WSAEADDRNOTAVAIL || err == WSAEHOSTUNREACH ) && ( to.type == NA_MULTICAST6 ) ) {
			return;
		}

		const char *errorString = NET_ErrorString();
		char	buf[1024];
		idStr::snPrintf( buf, sizeof( buf ), "Net_SendUDPPacket: %s\n", errorString );
		OutputDebugString( buf );
		common->Printf( "Net_SendUDPPacket ERROR: to %s: %s\n", Sys_NetAdrToString( to ), errorString );
		return;
	}

	if ( verbosePacket ) {
		common->DPrintf( "Net_SendUDPPacket: sent %d bytes to %s\n", ret, Sys_NetAdrToString( to ) );
	}
}

/*
====================
Sys_InitIPv6Interfaces

Records every local IPv6 unicast address so Sys_IsLANAddress can recognize a
global-unicast neighbour on the same link, which the address scope rules alone
cannot decide.
====================
*/
static void Sys_ScanIPv6Adapters( const IP_ADAPTER_ADDRESSES *adapterAddresses ) {
	for ( const IP_ADAPTER_ADDRESSES *adapter = adapterAddresses; adapter != NULL; adapter = adapter->Next ) {
		if ( adapter->OperStatus != IfOperStatusUp ) {
			continue;
		}
		for ( const IP_ADAPTER_UNICAST_ADDRESS *unicast = adapter->FirstUnicastAddress; unicast != NULL; unicast = unicast->Next ) {
			const struct sockaddr *address = unicast->Address.lpSockaddr;
			if ( address == NULL || address->sa_family != AF_INET6 ) {
				continue;
			}
			if ( num_interfaces6 >= MAX_INTERFACES ) {
				common->Printf( "Sys_InitNetworking: MAX_INTERFACES(%d) hit for IPv6.\n", MAX_INTERFACES );
				return;
			}

			const struct sockaddr_in6 *ipv6 = reinterpret_cast<const struct sockaddr_in6 *>( address );
			memcpy( netint6[num_interfaces6].ip6, &ipv6->sin6_addr, sizeof( netint6[num_interfaces6].ip6 ) );
			netint6[num_interfaces6].scopeId = static_cast<unsigned int>( ipv6->sin6_scope_id );

			char text[idNetworkEndpoint::IPV6_ENDPOINT_TEXT_SIZE];
			if ( idNetworkEndpoint::FormatIPv6Endpoint( netint6[num_interfaces6].ip6, netint6[num_interfaces6].scopeId, 0, text, sizeof( text ) ) ) {
				common->Printf( "IPv6: %s/%d\n", text, IPV6_ONLINK_PREFIX_BITS );
			}
			num_interfaces6++;
		}
	}
}

static void Sys_InitIPv6Interfaces( void ) {
	num_interfaces6 = 0;

	ULONG bufferLength = 16384;
	IP_ADAPTER_ADDRESSES *adapterAddresses = (IP_ADAPTER_ADDRESSES *)malloc( bufferLength );
	if ( adapterAddresses == NULL ) {
		common->Printf( "Sys_InitNetworking: could not allocate the IPv6 adapter list\n" );
		return;
	}

	const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME;
	ULONG result = GetAdaptersAddresses( AF_INET6, flags, NULL, adapterAddresses, &bufferLength );
	if ( result == ERROR_BUFFER_OVERFLOW ) {
		free( adapterAddresses );
		adapterAddresses = (IP_ADAPTER_ADDRESSES *)malloc( bufferLength );
		if ( adapterAddresses == NULL ) {
			common->Printf( "Sys_InitNetworking: could not allocate the IPv6 adapter list\n" );
			return;
		}
		result = GetAdaptersAddresses( AF_INET6, flags, NULL, adapterAddresses, &bufferLength );
	}

	// The scan runs in its own function so every way out of this one passes
	// through the single free below.
	if ( result == NO_ERROR ) {
		Sys_ScanIPv6Adapters( adapterAddresses );
	} else {
		// happens on a host with IPv6 disabled
		common->DPrintf( "Sys_InitNetworking: GetAdaptersAddresses(AF_INET6) failed (%lu).\n", result );
	}

	free( adapterAddresses );
}

/*
====================
Sys_InitNetworking
====================
*/
void Sys_InitNetworking( void ) {
	int		r;

	if ( winsockInitialized ) {
		return;
	}

	r = WSAStartup( MAKEWORD( 2, 2 ), &winsockdata );
	if( r ) {
		common->Printf( "WARNING: Winsock initialization failed, returned %d\n", r );
		return;
	}
	if ( LOBYTE( winsockdata.wVersion ) != 2 || HIBYTE( winsockdata.wVersion ) != 2 ) {
		common->Printf( "WARNING: Winsock 2.2 is unavailable (received %u.%u)\n", LOBYTE( winsockdata.wVersion ), HIBYTE( winsockdata.wVersion ) );
		WSACleanup();
		return;
	}

	winsockInitialized = true;
	common->Printf( "Winsock Initialized\n" );

	PIP_ADAPTER_INFO pAdapterInfo;
	PIP_ADAPTER_INFO pAdapter = NULL;
	DWORD dwRetVal = 0;
	PIP_ADDR_STRING pIPAddrString;
	ULONG ulOutBufLen;
	bool foundloopback;

	num_interfaces = 0;
	foundloopback = false;

	pAdapterInfo = (IP_ADAPTER_INFO *)malloc( sizeof( IP_ADAPTER_INFO ) );
	if( !pAdapterInfo ) {
		common->FatalError( "Sys_InitNetworking: Couldn't malloc( %zu )", sizeof( IP_ADAPTER_INFO ) );
	}
	ulOutBufLen = sizeof( IP_ADAPTER_INFO );

	// Make an initial call to GetAdaptersInfo to get
	// the necessary size into the ulOutBufLen variable
	if( GetAdaptersInfo( pAdapterInfo, &ulOutBufLen ) == ERROR_BUFFER_OVERFLOW ) {
		free( pAdapterInfo );
		pAdapterInfo = (IP_ADAPTER_INFO *)malloc( ulOutBufLen );
		if( !pAdapterInfo ) {
			common->FatalError( "Sys_InitNetworking: Couldn't malloc( %lu )", ulOutBufLen );
		}
	}

	if( ( dwRetVal = GetAdaptersInfo( pAdapterInfo, &ulOutBufLen) ) != NO_ERROR ) {
		// happens if you have no network connection
		common->Printf( "Sys_InitNetworking: GetAdaptersInfo failed (%lu).\n", dwRetVal );
	} else {
		pAdapter = pAdapterInfo;
		while( pAdapter ) {
			common->Printf( "Found interface: %s %s - ", pAdapter->AdapterName, pAdapter->Description );
			pIPAddrString = &pAdapter->IpAddressList;
			while( pIPAddrString ) {
				uint32_t ip_a, ip_m;
				struct sockaddr_storage interfaceAddress;
				struct sockaddr_storage interfaceMask;
				int interfaceAddressLen = 0;
				int interfaceMaskLen = 0;
				if ( !Net_StringToSockaddr( pIPAddrString->IpAddress.String, &interfaceAddress, &interfaceAddressLen, false, AF_INET ) ||
						!Net_StringToSockaddr( pIPAddrString->IpMask.String, &interfaceMask, &interfaceMaskLen, false, AF_INET ) ) {
					common->Printf( "%s/%s invalid IPv4 data - skipped\n", pIPAddrString->IpAddress.String, pIPAddrString->IpMask.String );
					pIPAddrString = pIPAddrString->Next;
					continue;
				}
				ip_a = ntohl( reinterpret_cast<struct sockaddr_in *>( &interfaceAddress )->sin_addr.s_addr );
				ip_m = ntohl( reinterpret_cast<struct sockaddr_in *>( &interfaceMask )->sin_addr.s_addr );
				if ( ( ip_a & 0xff000000u ) == 0x7f000000u ) {
					foundloopback = true;
				}
				//skip null netmasks
				if( !ip_m ) {
					common->Printf( "%s NULL netmask - skipped\n", pIPAddrString->IpAddress.String );
					pIPAddrString = pIPAddrString->Next;
					continue;
				}
				common->Printf( "%s/%s\n", pIPAddrString->IpAddress.String, pIPAddrString->IpMask.String );
				netint[num_interfaces].ip = ip_a;
				netint[num_interfaces].mask = ip_m;
				num_interfaces++;
				if( num_interfaces >= MAX_INTERFACES ) {
					common->Printf( "Sys_InitNetworking: MAX_INTERFACES(%d) hit.\n", MAX_INTERFACES );
					free( pAdapterInfo );
					Sys_InitIPv6Interfaces();
					return;
				}
				pIPAddrString = pIPAddrString->Next;
			}
			pAdapter = pAdapter->Next;
		}
	}
	// for some retarded reason, win32 doesn't count loopback as an adapter...
	if( !foundloopback && num_interfaces < MAX_INTERFACES ) {
		common->Printf( "Sys_InitNetworking: adding loopback interface\n" );
		netint[num_interfaces].ip = 0x7f000001u;
		netint[num_interfaces].mask = 0xff000000u;
		num_interfaces++;
	}
	free( pAdapterInfo );

	Sys_InitIPv6Interfaces();
}


/*
====================
Sys_ShutdownNetworking
====================
*/
void Sys_ShutdownNetworking( void ) {
	if ( !winsockInitialized ) {
		return;
	}
	WSACleanup();
	winsockInitialized = false;
}

/*
=============
Sys_StringToNetAdr
=============
*/
bool Sys_StringToNetAdr( const char *s, netadr_t *a, bool doDNSResolve ) {
	if ( a == NULL ) {
		return false;
	}
	memset( a, 0, sizeof( *a ) );
	a->type = NA_BAD;

	struct sockaddr_storage sadr;
	int sadrLen = 0;
	if ( !Net_StringToSockaddr( s, &sadr, &sadrLen, doDNSResolve ) ) {
		return false;
	}

	return Net_SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &sadr ), a );
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

	struct sockaddr_storage addresses[idNetworkEndpoint::MAX_RESOLVED_CANDIDATES];
	int addressLens[idNetworkEndpoint::MAX_RESOLVED_CANDIDATES];
	const int resolved = Net_ResolveSockaddrs( s, addresses, addressLens, maxCandidates, doDNSResolve );

	int count = 0;
	for ( int i = 0; i < resolved; i++ ) {
		netadr_t &candidate = candidates[count];
		memset( &candidate, 0, sizeof( candidate ) );
		candidate.type = NA_BAD;
		if ( !Net_SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &addresses[i] ), &candidate ) ) {
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
	static int index = 0;
	static char buf[ 4 ][ 128 ];	// flip/flop
	char *s;

	s = buf[index];
	index = (index + 1) & 3;
	s[0] = '\0';

	if ( a.type == NA_LOOPBACK ) {
		if ( a.port ) {
			idStr::snPrintf( s, sizeof( buf[0] ), "localhost:%i", a.port );
		} else {
			idStr::snPrintf( s, sizeof( buf[0] ), "localhost" );
		}
	} else if ( a.type == NA_IP ) {
		idStr::snPrintf( s, sizeof( buf[0] ), "%i.%i.%i.%i:%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port );
	} else if ( a.type == NA_IP6 ) {
		char addressText[idNetworkEndpoint::IPV6_ENDPOINT_TEXT_SIZE];
		if ( !idNetworkEndpoint::FormatIPv6Endpoint( a.ip6, a.scopeId, a.port, addressText, sizeof( addressText ) ) ) {
			idStr::Copynz( addressText, "::", sizeof( addressText ) );
		}
		idStr::Copynz( s, addressText, sizeof( buf[0] ) );
	} else if ( a.type == NA_BROADCAST ) {
		if ( a.port ) {
			idStr::snPrintf( s, sizeof( buf[0] ), "broadcast:%i", a.port );
		} else {
			idStr::Copynz( s, "broadcast", sizeof( buf[0] ) );
		}
	} else if ( a.type == NA_MULTICAST6 ) {
		if ( a.port ) {
			idStr::snPrintf( s, sizeof( buf[0] ), "multicast6:%i", a.port );
		} else {
			idStr::Copynz( s, "multicast6", sizeof( buf[0] ) );
		}
	} else if ( a.type == NA_BOT ) {
		idStr::Copynz( s, "bot", sizeof( buf[0] ) );
	} else {
		idStr::Copynz( s, "bad", sizeof( buf[0] ) );
	}
	return s;
}

/*
==================
Sys_IsLANAddress
==================
*/
bool Sys_IsLANAddress( const netadr_t adr ) {
#if ID_NOLANADDRESS
	common->Printf( "Sys_IsLANAddress: ID_NOLANADDRESS\n" );
	return false;
#endif
	if( adr.type == NA_LOOPBACK ) {
		return true;
	}

	if( adr.type == NA_IP6 ) {
		if ( idNetworkEndpoint::IsIPv6Loopback( adr.ip6 ) || idNetworkEndpoint::IsIPv6LinkLocal( adr.ip6 ) ||
				idNetworkEndpoint::IsIPv6UniqueLocal( adr.ip6 ) || idNetworkEndpoint::IsIPv6SiteLocal( adr.ip6 ) ) {
			return true;
		}
		// A global unicast address still belongs to the LAN when it shares an
		// on-link prefix with one of this host's own addresses.
		for( int i = 0; i < num_interfaces6; i++ ) {
			if ( idNetworkEndpoint::IPv6PrefixMatches( adr.ip6, netint6[i].ip6, IPV6_ONLINK_PREFIX_BITS ) ) {
				return true;
			}
		}
		return false;
	}

	if( adr.type != NA_IP ) {
		return false;
	}

	if( num_interfaces ) {
		int i;
		uint32_t packedIP;
		memcpy( &packedIP, adr.ip, sizeof( packedIP ) );
		const uint32_t ip = ntohl( packedIP );

		for( i=0; i < num_interfaces; i++ ) {
			if( ( netint[i].ip & netint[i].mask ) == ( ip & netint[i].mask ) ) {
				return true;
			}
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

//=============================================================================


/*
==================
Simulated network conditions

openQ4: each port's two delay lines (NetworkImpairment.h) replace the inherited lag
queue, which held datagrams of at most 1400 bytes - a larger one was silently
dropped while net_forceLatency was set - released sends only when another send
came along, and had no jitter, reordering or duplication.
==================
*/
typedef idNetworkImpairment::port_t<netadr_t> netImpairmentPort_t;

static const int NET_MAX_IMPAIRED_PORTS = 8;
static netImpairmentPort_t *net_impairedPorts[NET_MAX_IMPAIRED_PORTS];

static idNetworkImpairment::settings_t Net_ImpairmentSettings( void ) {
	return idNetworkImpairment::Sanitize( net_forceLatency.GetInteger(), net_forceJitter.GetInteger(),
		net_forceDrop.GetInteger(), net_forceDuplicate.GetInteger(), net_forceRate.GetInteger(),
		net_forceQueue.GetInteger(), net_forceDropBurst.GetInteger() );
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
		"overflowed=%llu discarded=%llu queued=%d maxQueued=%d bytes=%llu bursts=%llu congested=%llu maxWait=%d\n", port,
		direction, static_cast<unsigned long long>( stats.offered ), static_cast<unsigned long long>( stats.dropped ),
		static_cast<unsigned long long>( stats.duplicated ), static_cast<unsigned long long>( stats.released ),
		static_cast<unsigned long long>( stats.reordered ), static_cast<unsigned long long>( stats.overflowed ),
		static_cast<unsigned long long>( stats.discarded ), queue.Depth(), stats.maxDepth,
		static_cast<unsigned long long>( stats.offeredBytes ),
		static_cast<unsigned long long>( stats.bursts ), static_cast<unsigned long long>( stats.congested ), stats.maxWaitMsec );
}

/*
==================
Sys_NetImpairmentReport
==================
*/
void Sys_NetImpairmentReport( void ) {
	const idNetworkImpairment::settings_t settings = Net_ImpairmentSettings();
	common->Printf( "NET_IMPAIR_SETTINGS latency=%d jitter=%d drop=%d burst=%d duplicate=%d rate=%d queue=%d active=%d\n",
		settings.latencyMsec, settings.jitterMsec, settings.dropPercent, settings.burstLength, settings.duplicatePercent,
		settings.rateBytes, settings.queueMsec, idNetworkImpairment::Active( settings ) ? 1 : 0 );
	for ( int i = 0; i < NET_MAX_IMPAIRED_PORTS; i++ ) {
		if ( net_impairedPorts[i] != NULL ) {
			Net_PrintImpairmentLine( net_impairedPorts[i]->port, "receive", net_impairedPorts[i]->receive );
			Net_PrintImpairmentLine( net_impairedPorts[i]->port, "send", net_impairedPorts[i]->send );
		}
	}
}

/*
==================
idPort::idPort
==================
*/
idPort::idPort() {
	netSocket = INVALID_SOCKET;
	netSocket6 = INVALID_SOCKET;
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
InitForPort
==================
*/
bool idPort::InitForPort( int portNumber ) {
	Close();
	if ( portNumber != PORT_ANY && ( portNumber < 0 || portNumber > 65535 ) ) {
		common->Warning( "idPort::InitForPort: invalid network port %d", portNumber );
		return false;
	}

	if ( !Net_BindDualStack( net_ip.GetString(), net_ip6.GetString(), portNumber, netSocket, netSocket6, bound_to ) ) {
		netSocket = INVALID_SOCKET;
		netSocket6 = INVALID_SOCKET;
		memset( &bound_to, 0, sizeof( bound_to ) );
		return false;
	}

#if 0
	// Intentionally unsupported; see the compatibility note above the CVars.
	if ( net_socksEnabled.GetBool() ) {
		NET_OpenSocks( portNumber );
	}
#endif

	platformData = Net_OpenImpairment( bound_to.port );

	return true;
}

/*
==================
idPort::Close
==================
*/
void idPort::Close() {
	if ( platformData != NULL ) {
		Net_CloseImpairment( static_cast<netImpairmentPort_t *>( platformData ) );
		platformData = NULL;
	}
	if ( netSocket != INVALID_SOCKET ) {
		closesocket( netSocket );
		netSocket = INVALID_SOCKET;
	}
	if ( netSocket6 != INVALID_SOCKET ) {
		closesocket( netSocket6 );
		netSocket6 = INVALID_SOCKET;
	}
	memset( &bound_to, 0, sizeof( bound_to ) );
}

/*
==================
idPort::GetPacket
==================
*/
bool idPort::GetPacket( netadr_t &from, void *data, int &size, int maxSize ) {
	bool ret;
	netImpairmentPort_t *impairment = static_cast<netImpairmentPort_t *>( platformData );
	size = 0;
	memset( &from, 0, sizeof( from ) );
	from.type = NA_BAD;
	if ( ( netSocket == INVALID_SOCKET && netSocket6 == INVALID_SOCKET ) || impairment == NULL || data == NULL || maxSize <= 0 ) {
		return false;
	}

	// Simulated conditions: sends that fell due since the last send leave now, so a
	// quiet sender's last datagrams are not held back until it sends again.
	const idNetworkImpairment::settings_t settings = Net_ImpairmentSettings();
	const int now = Sys_Milliseconds();
	if ( impairment->SendsQueued( settings ) ) {
		const SOCKET ipv4Socket = netSocket;
		const SOCKET ipv6Socket = netSocket6;
		impairment->FlushSends( now, [ipv4Socket, ipv6Socket]( const netadr_t &to, const unsigned char *bytes, int length ) {
			Net_SendUDPPacket( Net_SocketForAddress( ipv4Socket, ipv6Socket, to ), length, bytes, to );
		} );
	}
	const bool queueReceives = impairment->ReceivesQueued( settings );

	// Alternate which family is drained first so a busy socket cannot starve
	// the other one. Seeding from this port's own read counter keeps the
	// rotation per-instance without widening idPort's shared layout.
	bool ipv6First = ( packetsRead & 1 ) != 0;

	while( 1 ) {

		if ( ipv6First ) {
			ret = Net_GetUDPPacket( netSocket6, from, (char *)data, size, maxSize );
			if ( !ret ) {
				ret = Net_GetUDPPacket( netSocket, from, (char *)data, size, maxSize );
			}
		} else {
			ret = Net_GetUDPPacket( netSocket, from, (char *)data, size, maxSize );
			if ( !ret ) {
				ret = Net_GetUDPPacket( netSocket6, from, (char *)data, size, maxSize );
			}
		}
		ipv6First = !ipv6First;
		if ( !ret ) {
			break;
		}

		if ( !queueReceives ) {
			packetsRead++;
			bytesRead += size;
			return true;
		}
		if ( impairment->receive.Offer( settings, now, from, data, size ) > 0 ) {
			packetsRead++;
			bytesRead += size;
		}
	}

	if ( !queueReceives ) {
		return false;
	}
	memset( &from, 0, sizeof( from ) );
	from.type = NA_BAD;
	return impairment->receive.Pop( now, from, data, maxSize, size );
}

/*
==================
idPort::GetPacketBlocking
==================
*/
bool idPort::GetPacketBlocking( netadr_t &from, void *data, int &size, int maxSize, int timeout ) {
	size = 0;
	memset( &from, 0, sizeof( from ) );
	from.type = NA_BAD;
	if ( timeout < 0 ) {
		return GetPacket( from, data, size, maxSize );
	}

	Net_WaitForUDPPacket( netSocket, netSocket6, timeout );

	if ( GetPacket( from, data, size, maxSize ) ) {
		return true;
	}

	return false;
}

/*
==================
idPort::SendPacket
==================
*/
void idPort::SendPacket( const netadr_t to, const void *data, int size ) {
	netImpairmentPort_t *impairment = static_cast<netImpairmentPort_t *>( platformData );

	if ( ( netSocket == INVALID_SOCKET && netSocket6 == INVALID_SOCKET ) || impairment == NULL ) {
		return;
	}
	if ( to.type != NA_IP && to.type != NA_LOOPBACK && to.type != NA_BROADCAST && to.type != NA_IP6 && to.type != NA_MULTICAST6 ) {
		common->Warning( "idPort::SendPacket: unsupported address type - ignored" );
		return;
	}
	if ( size < 0 || ( data == NULL && size > 0 ) ) {
		common->Warning( "idPort::SendPacket: invalid packet buffer - ignored" );
		return;
	}
	const SOCKET socketForAddress = Net_SocketForAddress( netSocket, netSocket6, to );
	if ( socketForAddress == INVALID_SOCKET ) {
		common->DPrintf( "idPort::SendPacket: no socket for %s - ignored\n", Sys_NetAdrToString( to ) );
		return;
	}

	packetsWritten++;
	bytesWritten += size;

	const idNetworkImpairment::settings_t settings = Net_ImpairmentSettings();
	if ( impairment->SendsQueued( settings ) ) {
		const int now = Sys_Milliseconds();
		impairment->send.Offer( settings, now, to, data, size );
		// Each queued datagram is routed by its own family, which may
		// differ from the datagram that filled the queue.
		const SOCKET ipv4Socket = netSocket;
		const SOCKET ipv6Socket = netSocket6;
		impairment->FlushSends( now, [ipv4Socket, ipv6Socket]( const netadr_t &address, const unsigned char *bytes, int length ) {
			Net_SendUDPPacket( Net_SocketForAddress( ipv4Socket, ipv6Socket, address ), length, bytes, address );
		} );
		return;
	}

	Net_SendUDPPacket( socketForAddress, size, data, to );
}


//=============================================================================

/*
==================
idTCP::idTCP
==================
*/
idTCP::idTCP() {
	fd = INVALID_SOCKET;
	memset( &address, 0, sizeof( address ) );
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
	unsigned long	_true = 1;
	struct sockaddr_storage sadr;
	int				sadrLen = 0;

	// Net_StringToSockaddr validates the default only when the endpoint omits
	// its own port, so an explicit host:port correctly overrides this argument.
	if ( !Net_StringToSockaddr( host, &sadr, &sadrLen, true, AF_UNSPEC, port, SOCK_STREAM ) || !Net_SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &sadr ), &address ) ) {
		common->Printf( "Couldn't resolve server name \"%s\"\n", host != NULL ? host : "" );
		return false;
	}
	common->Printf( "\"%s\" resolved to %s\n", host != NULL ? host : "", Sys_NetAdrToString( address ) );

	if ( fd != INVALID_SOCKET ) {
		common->Warning( "idTCP::Init: already initialized - closing the previous connection" );
		Close();
	}

	if ( ( fd = socket( sadr.ss_family, SOCK_STREAM, IPPROTO_TCP ) ) == INVALID_SOCKET ) {
		fd = INVALID_SOCKET;
		common->Printf( "ERROR: idTCP::Init: socket: %s\n", NET_ErrorString() );
		return false;
	}

	if ( connect( fd, reinterpret_cast<const struct sockaddr *>( &sadr ), sadrLen ) == SOCKET_ERROR ) {
		common->Printf( "ERROR: idTCP::Init: connect: %s\n", NET_ErrorString() );
		closesocket( fd );
		fd = INVALID_SOCKET;
		return false;
	}

	// make it non-blocking
	if( ioctlsocket( fd, FIONBIO, &_true ) == SOCKET_ERROR ) {
		common->Printf( "ERROR: idTCP::Init: ioctl FIONBIO: %s\n", NET_ErrorString() );
		closesocket( fd );
		fd = INVALID_SOCKET;
		return false;
	}

	common->DPrintf( "Opened TCP connection\n" );
	return true;
}

/*
==================
idTCP::Close
==================
*/
void idTCP::Close() {
	if ( fd != INVALID_SOCKET ) {
		closesocket( fd );
	}
	fd = INVALID_SOCKET;
}

/*
==================
idTCP::Read
==================
*/
int idTCP::Read( void *data, int size ) {
	int nbytes;

	if ( fd == INVALID_SOCKET ) {
		common->Printf("idTCP::Read: not initialized\n");
		return -1;
	}
	if ( size <= 0 ) {
		return 0;
	}
	if ( data == NULL ) {
		common->Printf( "idTCP::Read: invalid buffer\n" );
		return -1;
	}

	if ( ( nbytes = recv( fd, (char *)data, size, 0 ) ) == SOCKET_ERROR ) {
		if ( WSAGetLastError() == WSAEWOULDBLOCK ) {
			return 0;
		}
		common->Printf( "ERROR: idTCP::Read: %s\n", NET_ErrorString() );
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
int idTCP::Write( void *data, int size ) {
	int nbytes;

	if ( fd == INVALID_SOCKET ) {
		common->Printf("idTCP::Write: not initialized\n");
		return -1;
	}
	if ( size <= 0 ) {
		return 0;
	}
	if ( data == NULL ) {
		common->Printf( "idTCP::Write: invalid buffer\n" );
		return -1;
	}

	if ( ( nbytes = send( fd, (char *)data, size, 0 ) ) == SOCKET_ERROR ) {
		if ( WSAGetLastError() == WSAEWOULDBLOCK ) {
			return 0;
		}
		common->Printf( "ERROR: idTCP::Write: %s\n", NET_ErrorString() );
		Close();
		return -1;
	}

	return nbytes;
}
