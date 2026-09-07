#!/usr/bin/env python3
"""Static regression contract for cross-platform IPv6 networking safety."""

from __future__ import annotations

import ast
import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")).resolve()

WINDOWS_NET = "src/sys/win32/win_net.cpp"
POSIX_NET = "src/sys/posix/posix_net.cpp"


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"Missing {token!r} in {context}")


def reject(text: str, token: str, context: str) -> None:
    if token in text:
        raise AssertionError(f"Unexpected {token!r} in {context}")


def expect_contract_rejection(validator, candidate: str, context: str) -> None:
    try:
        validator(candidate)
    except AssertionError:
        return
    raise AssertionError(f"Validation contract accepted mutation: {context}")


def require_before(text: str, first: str, second: str, context: str) -> None:
    require(text, first, context)
    require(text, second, context)
    if text.index(first) >= text.index(second):
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    if opening < 0:
        raise AssertionError(f"Missing body for {signature!r} in {context}")

    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"Unbalanced body for {signature!r} in {context}")


def unique_signature(source: str, signature: str, context: str) -> None:
    """A signature that is also a prefix of another definition slices the wrong body."""
    if source.count(signature) != 1:
        raise AssertionError(f"Ambiguous signature {signature!r} in {context}")


def validate_shared_ipv6_text() -> None:
    """The address text is an identity key, so it has to be canonical and shared."""
    source = read("src/sys/NetworkEndpoint.h")

    for token in (
        "const size_t IPV6_TEXT_SIZE = 46;",
        "const size_t IPV6_ENDPOINT_TEXT_SIZE = IPV6_TEXT_SIZE + 19;",
        "inline bool FormatIPv6( const unsigned char *ip6, char *out, size_t outSize )",
        "inline bool FormatIPv6Endpoint( const unsigned char *ip6, unsigned int scopeId, unsigned short port, char *out, size_t outSize )",
        "inline bool IPv6PrefixMatches( const unsigned char *a, const unsigned char *b, unsigned int prefixLength )",
        "inline bool IsIPv4Mapped( const unsigned char *ip6 )",
        "inline bool IsIPv6Loopback( const unsigned char *ip6 )",
        "inline bool IsIPv6Unspecified( const unsigned char *ip6 )",
        "inline bool IsIPv6LinkLocal( const unsigned char *ip6 )",
        "inline bool IsIPv6UniqueLocal( const unsigned char *ip6 )",
        "inline bool IsIPv6SiteLocal( const unsigned char *ip6 )",
        "inline bool IsIPv6Multicast( const unsigned char *ip6 )",
        "inline bool IsAnyInterfaceName( const char *name )",
        "inline bool LooksLikeIPv6Literal( const char *name )",
        "inline void PlanBind( const char *ipv4Text, const char *ipv6Text, bool enableIPv4, bool enableIPv6, bindPlan_t &plan )",
        "inline bool FormatLocalServerEndpoint( const char *netIP, int netPort, char *out, size_t outSize )",
    ):
        require(source, token, "shared IPv6 text helpers")

    # The header is included by both platform socket layers, the framework, and
    # the native tests, so it must stay free of engine dependencies.
    for token in ("idStr", "common->", "idCVar", "#include \"", "cvarSystem"):
        reject(source, token, "engine-free shared endpoint header")

    formatter = function_body(source, "inline bool FormatIPv6(", "RFC 5952 formatter")
    for token in (
        "if ( runLength > bestLength )",
        "if ( bestLength < 2 )",
        "const bool mapped = IsIPv4Mapped( ip6 );",
        "const int textGroups = mapped ? 6 : 8;",
    ):
        require(formatter, token, "RFC 5952 formatter")
    # A truncated literal would alias two distinct hosts onto one browser key
    # and one ban-list entry, so an undersized buffer must fail closed.
    formatter_overflow = function_body(formatter, "if ( buffer.overflow )", "RFC 5952 overflow rejection")
    require(formatter_overflow, "out[0] = '\\0';", "RFC 5952 overflow reset")
    require(formatter_overflow, "return false;", "RFC 5952 overflow result")
    # A strict comparison is what keeps the leftmost of two equally long zero
    # runs; ">=" would silently pick the rightmost and break canonical form.
    reject(formatter, "runLength >= bestLength", "RFC 5952 leftmost-run tie-break")
    # A single zero group must never be compressed.
    reject(formatter, "if ( bestLength < 1 )", "RFC 5952 single-group compression")

    endpoint = function_body(source, "inline bool FormatIPv6Endpoint(", "IPv6 endpoint formatter")
    for token in (
        "char address[IPV6_TEXT_SIZE];",
        "if ( port != 0 )",
        "TextAppendChar( buffer, '[' );",
        "TextAppendChar( buffer, ']' );",
        "TextAppendChar( buffer, '%' );",
    ):
        require(endpoint, token, "IPv6 endpoint formatter")
    # A truncated address is worse than none: it would alias two distinct
    # servers onto one browser key and one ban-list entry.
    require(endpoint, "if ( buffer.overflow )", "IPv6 endpoint overflow rejection")
    overflow = function_body(endpoint, "if ( buffer.overflow )", "IPv6 endpoint overflow rejection")
    require(overflow, "out[0] = '\\0';", "IPv6 endpoint overflow reset")
    require(overflow, "return false;", "IPv6 endpoint overflow result")

    text_append = function_body(source, "inline void TextAppendChar(", "bounded text append")
    require(text_append, "if ( buffer.length + 1 >= buffer.size )", "bounded text append capacity")
    require(text_append, "buffer.overflow = true;", "bounded text append overflow flag")

    prefix = function_body(source, "inline bool IPv6PrefixMatches(", "IPv6 prefix comparison")
    for token in (
        "prefixLength > 128",
        "const unsigned int wholeBytes = prefixLength / 8;",
        "const unsigned int remainingBits = prefixLength % 8;",
        "static_cast<unsigned char>( 0xff << ( 8 - remainingBits ) )",
    ):
        require(prefix, token, "IPv6 prefix comparison")


def validate_bind_plan() -> None:
    """One shared decision table so the platforms cannot drift apart."""
    source = read("src/sys/NetworkEndpoint.h")
    plan = function_body(source, "inline void PlanBind(", "dual-stack bind plan")
    for token in (
        "plan.bindIPv4 = enableIPv4;",
        "plan.bindIPv6 = enableIPv6;",
        "plan.legacyIPv6Interface = false;",
        "if ( IsAnyInterfaceName( ipv4Text ) || !LooksLikeIPv6Literal( ipv4Text ) )",
        "plan.legacyIPv6Interface = true;",
        "plan.bindIPv4 = false;",
        "plan.ipv4Interface = NULL;",
    ):
        require(plan, token, "dual-stack bind plan")

    literal = function_body(source, "inline bool LooksLikeIPv6Literal(", "IPv6 literal detection")
    # "1.2.3.4:27650" has exactly one colon and must stay an IPv4 endpoint.
    require(literal, "if ( *cursor == ':' && ++colons > 1 )", "IPv6 literal colon threshold")
    require(literal, "if ( name[0] == '[' )", "bracketed IPv6 literal detection")

    wildcard = function_body(source, "inline bool IsAnyInterfaceName(", "wildcard interface names")
    for token in ('EqualIgnoreCase( name, "localhost" )', 'EqualIgnoreCase( name, "0.0.0.0" )', 'EqualIgnoreCase( name, "::" )'):
        require(wildcard, token, "wildcard interface names")

    local = function_body(source, "inline bool FormatLocalServerEndpoint(", "local server endpoint text")
    for token in (
        "const bool wildcard = IsAnyInterfaceName( netIP );",
        'TextAppendString( buffer, ipv6Interface ? "[::1]" : "127.0.0.1" );',
        "} else if ( ipv6Interface && netIP[0] != '[' ) {",
        "if ( netPort > 0 )",
    ):
        require(local, token, "local server endpoint text")
    # net_port 0 means "choose automatically", so naming it would point players
    # at a port nothing listens on.
    reject(local, "TextAppendChar( buffer, '0' );", "local server endpoint automatic port")
    # A bare IPv6 literal must gain brackets before a port is appended, or the
    # result reads as a longer address with no port at all.
    require_before(local, "TextAppendChar( buffer, '[' );", "TextAppendChar( buffer, ':' );", "local server endpoint text")


def validate_platform_ipv6_transport(relative_path: str, platform: str) -> None:
    source = read(relative_path)

    # Both families must be reachable from one idPort.
    for token in ("netSocket6", "AF_INET6", "IPV6_V6ONLY", "NA_MULTICAST6", "NA_IP6"):
        require(source, token, f"{platform} dual-stack transport")

    # inet_ntop is unavailable at the Windows target version and would in any
    # case let the two platforms spell one address two different ways.
    reject(source, "inet_ntop(", f"{platform} platform-specific IPv6 formatting")

    address_string = function_body(source, "const char *Sys_NetAdrToString", f"{platform} address formatting")
    ipv6_string = function_body(
        address_string,
        "} else if ( a.type == NA_IP6 ) {",
        f"{platform} IPv6 address formatting",
    )
    require(
        ipv6_string,
        "idNetworkEndpoint::FormatIPv6Endpoint( a.ip6, a.scopeId, a.port, addressText, sizeof( addressText ) )",
        f"{platform} shared IPv6 address formatting",
    )
    require(
        ipv6_string,
        "char addressText[idNetworkEndpoint::IPV6_ENDPOINT_TEXT_SIZE];",
        f"{platform} IPv6 address buffer sizing",
    )
    reject(ipv6_string, "[ipv6]", f"{platform} placeholder IPv6 address text")
    multicast_string = function_body(
        address_string,
        "} else if ( a.type == NA_MULTICAST6 ) {",
        f"{platform} multicast address formatting",
    )
    require(multicast_string, '"multicast6', f"{platform} multicast address formatting")

    # A peer arriving through ::ffff:a.b.c.d is an IPv4 peer. Two identities for
    # one host would break challenge matching, client-slot reuse, and bans.
    sockadr = function_body(source, "SockadrToNetadr", f"{platform} sockaddr conversion")
    mapped = function_body(
        sockadr,
        "if ( idNetworkEndpoint::IsIPv4Mapped( a->ip6 ) )",
        f"{platform} IPv4-mapped normalization",
    )
    for token in (
        "memcpy( mapped, a->ip6 + 12, sizeof( mapped ) );",
        "memset( a, 0, sizeof( *a ) );",
        "memcpy( a->ip, mapped, sizeof( a->ip ) );",
        "a->type = ( ntohl( packedIP ) == INADDR_LOOPBACK ) ? NA_LOOPBACK : NA_IP;",
    ):
        require(mapped, token, f"{platform} IPv4-mapped normalization")
    require_before(
        mapped,
        "memcpy( mapped, a->ip6 + 12, sizeof( mapped ) );",
        "memset( a, 0, sizeof( *a ) );",
        f"{platform} IPv4-mapped normalization order",
    )

    # The IPv6 socket must stay off the mapped range so both families can hold
    # the same port and an IPv4 peer always lands on the IPv4 socket. Pin the
    # call, not the macro name, which a comment would also satisfy.
    require(
        source,
        "setsockopt( newsocket, IPPROTO_IPV6, IPV6_V6ONLY,",
        f"{platform} IPv6-only socket option",
    )
    require(
        source,
        "setsockopt( newsocket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,",
        f"{platform} multicast hop limit",
    )
    # Any discovery group other than the implicit all-nodes one has to be joined
    # or a server never receives a scan sent to it.
    require(source, "IPV6_JOIN_GROUP,", f"{platform} discovery group membership")
    require(source, "idNetworkEndpoint::IsIPv6AllNodes( groupAddress )", f"{platform} implicit all-nodes membership")

    # LAN classification has to recognize a global-unicast neighbour on the same
    # link, which the address scope rules alone cannot decide.
    lan = function_body(source, "bool Sys_IsLANAddress", f"{platform} LAN classification")
    ipv6_lan = function_body(lan, "adr.type == NA_IP6", f"{platform} IPv6 LAN classification")
    for token in (
        "idNetworkEndpoint::IsIPv6Loopback( adr.ip6 )",
        "idNetworkEndpoint::IsIPv6LinkLocal( adr.ip6 )",
        "idNetworkEndpoint::IsIPv6UniqueLocal( adr.ip6 )",
        "idNetworkEndpoint::IPv6PrefixMatches( adr.ip6, netint6[",
        "IPV6_ONLINK_PREFIX_BITS",
    ):
        require(ipv6_lan, token, f"{platform} IPv6 LAN classification")

    # A separate table: IPv6 hosts carry several addresses per link and would
    # otherwise evict IPv4 entries and regress IPv4 LAN detection.
    for token in (
        "int				num_interfaces6 = 0;",
        "net_interface6	netint6[MAX_INTERFACES];",
        "#define			IPV6_ONLINK_PREFIX_BITS	64",
    ):
        require(source, token, f"{platform} IPv6 interface table")
    reject(source, "netint[ num_interfaces ].ip6", f"{platform} shared interface table")

    # A link-local multicast datagram leaves through one interface, so a
    # multi-homed host has to repeat the send per link.
    unique_signature(source, "static void Net_SendMulticast6Packet(", f"{platform} multicast fan-out")
    fanout = function_body(source, "static void Net_SendMulticast6Packet(", f"{platform} multicast fan-out")
    for token in (
        "if ( group.sin6_scope_id != 0 )",
        "for ( int i = 0; i < num_interfaces6; i++ )",
        "group.sin6_scope_id = netint6[i].scopeId;",
        "if ( sends == 0 )",
    ):
        require(fanout, token, f"{platform} multicast fan-out")
    # Repeating one scope would scan the same link twice and double the replies.
    require(fanout, "duplicate = true;", f"{platform} multicast scope de-duplication")

    group = function_body(source, "MulticastGroupSockadr", f"{platform} multicast group resolution")
    for token in (
        "net_mcast6addr.GetString()",
        "AF_INET6",
        "group->sin6_port = htons( port );",
        "net_mcast6iface.GetInteger()",
    ):
        require(group, token, f"{platform} multicast group resolution")

    # Callers drain until this returns false, so a flood on one family would
    # otherwise stop the other from ever being read.
    receive = function_body(source, "idPort::GetPacket(", f"{platform} dual-stack receive")
    require(receive, "( packetsRead & 1 ) != 0", f"{platform} receive family rotation")
    require(receive, "ipv6First", f"{platform} receive family rotation")

    for token in (
        'idCVar net_ip6( "net_ip6", "", CVAR_SYSTEM, "local IPv6 address, empty binds every interface" );',
        'idCVar net_enableIPv4( "net_enableIPv4", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "bind an IPv4 socket" );',
        'idCVar net_enableIPv6( "net_enableIPv6", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "bind an IPv6 socket" );',
        'idCVar net_mcast6addr( "net_mcast6addr", "ff02::1", CVAR_SYSTEM | CVAR_ARCHIVE, "IPv6 multicast group used for LAN server discovery" );',
    ):
        require(source, token, f"{platform} IPv6 CVar defaults")


def validate_windows_ipv6_specifics() -> None:
    source = read(WINDOWS_NET)

    # GetAdaptersAddresses is the only way to see this host's IPv6 addresses,
    # and only the fields present at the build's Windows target may be read.
    scan = function_body(source, "static void Sys_ScanIPv6Adapters", "Windows IPv6 adapter scan")
    for token in (
        "adapter->OperStatus != IfOperStatusUp",
        "unicast->Address.lpSockaddr",
        "netint6[num_interfaces6].scopeId = static_cast<unsigned int>( ipv6->sin6_scope_id );",
        "if ( num_interfaces6 >= MAX_INTERFACES )",
    ):
        require(scan, token, "Windows IPv6 adapter scan")
    # Only fields present at the build's Windows target version may be read.
    for token in ("OnLinkPrefixLength", "inet_pton", "if_indextoname", "free("):
        reject(scan, token, "Windows IPv6 adapter scan")

    enumeration = function_body(source, "static void Sys_InitIPv6Interfaces", "Windows IPv6 interface discovery")
    for token in (
        "GetAdaptersAddresses( AF_INET6, flags, NULL, adapterAddresses, &bufferLength )",
        "ERROR_BUFFER_OVERFLOW",
        "Sys_ScanIPv6Adapters( adapterAddresses );",
    ):
        require(enumeration, token, "Windows IPv6 interface discovery")
    # The scan lives in its own function so every way out of this one passes
    # through a single free. The only returns here precede a live allocation.
    scan_call = enumeration.index("Sys_ScanIPv6Adapters( adapterAddresses );")
    if enumeration.rindex("free( adapterAddresses );") < scan_call:
        raise AssertionError("Windows IPv6 interface discovery must free the adapter list after scanning it")
    after_scan = enumeration[scan_call:]
    if "return;" in after_scan:
        raise AssertionError("Windows IPv6 interface discovery must not return between the scan and its free")
    for match in re.finditer(r"return;", enumeration):
        if "adapterAddresses == NULL" not in enumeration[max(0, match.start() - 200) : match.start()]:
            raise AssertionError("Windows IPv6 interface discovery may only return early on a failed allocation")

    require(source, "Sys_InitIPv6Interfaces();", "Windows IPv6 interface discovery call")

    routing = function_body(source, "static SOCKET Net_SocketForAddress", "Windows address family routing")
    for token in (
        "if ( to.type == NA_IP6 || to.type == NA_MULTICAST6 )",
        "return netSocket6;",
        "if ( to.type == NA_IP || to.type == NA_LOOPBACK || to.type == NA_BROADCAST )",
        "return netSocket;",
        "return INVALID_SOCKET;",
    ):
        require(routing, token, "Windows address family routing")

    # A queued datagram is routed by its own family, which may differ from the
    # datagram that filled the latency queue.
    send = function_body(source, "void idPort::SendPacket", "Windows delayed send routing")
    require(
        send,
        "Net_SendUDPPacket( Net_SocketForAddress( netSocket, netSocket6, msg->address ), msg->size, msg->data, msg->address );",
        "Windows delayed send routing",
    )
    require(
        send,
        "to.type != NA_IP && to.type != NA_LOOPBACK && to.type != NA_BROADCAST && to.type != NA_IP6 && to.type != NA_MULTICAST6",
        "Windows supported send address types",
    )

    wait = function_body(source, "bool Net_WaitForUDPPacket", "Windows dual-stack select")
    for token in (
        "if ( netSocket != INVALID_SOCKET )",
        "if ( netSocket6 != INVALID_SOCKET )",
        "FD_SET( netSocket6, &set );",
    ):
        require(wait, token, "Windows dual-stack select")


def validate_posix_ipv6_specifics() -> None:
    source = read(POSIX_NET)

    require(source, "#include <ifaddrs.h>", "POSIX IPv6 interface discovery header")
    # The old include sat behind a macOS guard; IPv6 enumeration needs it on
    # every POSIX target because SIOCGIFCONF has no IPv6 equivalent.
    guarded_include = re.search(
        r"#if defined\( MACOS_X \)[^\n]*\n#include <ifaddrs\.h>",
        source,
    )
    if guarded_include is not None:
        raise AssertionError("POSIX ifaddrs.h must not be restricted to macOS")

    enumeration = function_body(source, "static void Sys_InitIPv6Interfaces", "POSIX IPv6 interface discovery")
    for token in (
        "getifaddrs( &ifap )",
        "freeifaddrs( ifap );",
        "ifp->ifa_flags & IFF_UP",
        "ifp->ifa_addr->sa_family != AF_INET6",
        "if ( num_interfaces6 >= MAX_INTERFACES )",
    ):
        require(enumeration, token, "POSIX IPv6 interface discovery")

    # KAME derived stacks report a link-local address with the interface index
    # embedded in bytes 2 and 3 and leave sin6_scope_id zero. Left in place
    # those bytes make every fe80:: prefix comparison fail.
    kame = function_body(
        enumeration,
        "if ( idNetworkEndpoint::IsIPv6LinkLocal( address ) && ( address[2] != 0 || address[3] != 0 ) )",
        "POSIX KAME embedded scope",
    )
    for token in (
        "scopeId = ( static_cast<unsigned int>( address[2] ) << 8 ) | static_cast<unsigned int>( address[3] );",
        "address[2] = 0;",
        "address[3] = 0;",
    ):
        require(kame, token, "POSIX KAME embedded scope")
    require_before(
        enumeration,
        "if ( idNetworkEndpoint::IsIPv6LinkLocal( address ) && ( address[2] != 0 || address[3] != 0 ) )",
        "memcpy( netint6[num_interfaces6].ip6, address, sizeof( netint6[num_interfaces6].ip6 ) );",
        "POSIX KAME normalization before storage",
    )

    require(source, "Sys_InitIPv6Interfaces();", "POSIX IPv6 interface discovery call")

    send = function_body(source, "void idPort::SendPacket", "POSIX multicast send")
    multicast = function_body(send, "if ( to.type == NA_MULTICAST6 )", "POSIX multicast send")
    for token in (
        "if ( !netSocket6 )",
        "Net_SendMulticast6Packet( netSocket6, packetData, size, to );",
        "packetsWritten++;",
    ):
        require(multicast, token, "POSIX multicast send")
    require_before(
        send,
        "if ( to.type == NA_MULTICAST6 )",
        "if ( !NetadrToSockadr( &to, &addr, &addrLen ) )",
        "POSIX multicast send precedes single-destination conversion",
    )

    # SO_REUSEADDR on a UDP socket would let a second instance bind a port the
    # first already holds, which silently breaks the caller's port scan.
    reject(source, "SO_REUSEADDR", "POSIX UDP address reuse")
    reject(source, "SO_REUSEPORT", "POSIX UDP port reuse")

    resolver = function_body(source, "static bool Sys_ResolveSockaddr", "POSIX address resolver")
    require(
        resolver,
        "hints.ai_flags = AI_NUMERICSERV | ( doDNSResolve ? AI_ADDRCONFIG : AI_NUMERICHOST );",
        "POSIX resolver flags",
    )
    require(resolver, "gai_strerror( error )", "POSIX resolver diagnostic")


def validate_ipv6_fragment_size() -> None:
    """IPv6 forbids router fragmentation and guarantees only a 1280 byte MTU."""
    source = read("src/framework/async/MsgChannel.cpp")
    for token in (
        "#define	MAX_PACKETLEN6			1232",
        "#define	FRAGMENT_SIZE6			(MAX_PACKETLEN6 - 100)",
        "static int Net_FragmentSize( const netadr_t &adr ) {",
        "return adr.type == NA_IP6 ? FRAGMENT_SIZE6 : FRAGMENT_SIZE;",
    ):
        require(source, token, "IPv6 fragment sizing")

    # Every fragment decision must use the family-aware size. The receiver
    # detects the final fragment by comparing against it, so a mismatch between
    # the two sides stalls the connection instead of failing loudly.
    for signature, context in (
        ("void idMsgChannel::SendNextFragment", "IPv6 fragment emission"),
        ("int idMsgChannel::SendMessage", "IPv6 fragment threshold"),
        ("bool idMsgChannel::Process", "IPv6 final-fragment detection"),
    ):
        body = function_body(source, signature, context)
        require(body, "Net_FragmentSize( remoteAddress )", context)
        if re.search(r"\bFRAGMENT_SIZE\b", body):
            raise AssertionError(f"Family-blind FRAGMENT_SIZE still decides {context}")


def validate_lan_discovery() -> None:
    source = read("src/framework/async/AsyncClient.cpp")
    scan = function_body(source, "void idAsyncClient::GetLANServers", "LAN server discovery")

    # Only .type and .port were ever assigned, leaving the address bytes and the
    # scope id holding stack garbage that the IPv6 sweep reads.
    require(scan, "memset( &broadcastAddress, 0, sizeof( broadcastAddress ) );", "zero-initialized broadcast address")
    require_before(
        scan,
        "memset( &broadcastAddress, 0, sizeof( broadcastAddress ) );",
        "broadcastAddress.type = NA_BROADCAST;",
        "zero-initialized broadcast address",
    )
    for token in (
        "memset( &multicastAddress, 0, sizeof( multicastAddress ) );",
        "multicastAddress.type = NA_MULTICAST6;",
        "multicastAddress.port = PORT_SERVER + i;",
        "clientPort.SendPacket( multicastAddress, msg.GetData(), msg.GetSize() );",
    ):
        require(scan, token, "IPv6 LAN discovery sweep")
    # The IPv4 sweep must survive alongside it.
    require(scan, "clientPort.SendPacket( broadcastAddress, msg.GetData(), msg.GetSize() );", "IPv4 LAN discovery sweep")


def validate_master_server_ipv6_list() -> None:
    source = read("src/framework/async/AsyncClient.cpp")

    # The legacy untagged reply cannot carry an IPv6 address without
    # desynchronizing every reader, so the extended reply is a separate message.
    legacy = function_body(source, "void idAsyncClient::ProcessServersListMessage", "legacy master list")
    reject(legacy, "NA_IP6", "legacy master list")
    reject(legacy, "ip6", "legacy master list")

    extended = function_body(source, "void idAsyncClient::ProcessServersListExtMessage", "extended master list")
    for token in (
        "Sys_CompareNetAdrBase( idAsyncNetwork::GetMasterAddress(), from )",
        "const int separator = msg.ReadByte();",
        "if ( separator == '\\\\' )",
        "} else if ( separator == '/' )",
        "addressBytes = 4;",
        "addressBytes = 16;",
        "server.type = NA_IP;",
        "server.type = NA_IP6;",
    ):
        require(extended, token, "extended master list")
    # idBitMsg::ReadUShort is little-endian, but this format carries the port in
    # network byte order, so it has to be assembled from explicit bytes.
    for token in (
        "const int portHigh = msg.ReadByte();",
        "const int portLow = msg.ReadByte();",
        "server.port = static_cast<unsigned short>( ( portHigh << 8 ) | portLow );",
    ):
        require(extended, token, "extended master list port byte order")
    reject(extended, "msg.ReadUShort()", "extended master list little-endian port")
    # The legacy reply keeps its little-endian port, which is its actual format.
    require(legacy, "msg.ReadUShort()", "legacy master list port byte order")

    # A record that does not fit the remaining payload is truncated and must not
    # be read past the end of the message.
    bounds = function_body(
        extended,
        "if ( msg.GetRemaingData() < addressBytes + 2 )",
        "extended master list truncation guard",
    )
    require(bounds, "break;", "extended master list truncation guard")
    require_before(
        extended,
        "if ( msg.GetRemaingData() < addressBytes + 2 )",
        "netadr_t server;",
        "extended master list bounds before read",
    )
    require(extended, "memset( &server, 0, sizeof( server ) );", "extended master list address initialization")
    require(
        extended,
        "idNetworkEndpoint::IsIPv6Unspecified( server.ip6 )",
        "extended master list unspecified rejection",
    )

    # Unauthenticated senders must not be able to inject servers.
    reject_source = function_body(
        extended,
        "if ( !Sys_CompareNetAdrBase( idAsyncNetwork::GetMasterAddress(), from ) )",
        "extended master list source check",
    )
    require(reject_source, "return;", "extended master list source check")

    request = function_body(source, "void idAsyncClient::GetNETServers", "master list request")
    require(request, '"getServers"', "legacy master list request")
    require(request, '"getServersExt"', "extended master list request")
    require_before(request, '"getServers"', '"getServersExt"', "master list request order")

    dispatch = function_body(source, "void idAsyncClient::ConnectionlessMessage", "master reply dispatch")
    require(dispatch, 'idStr::Icmp( string, "serversExt" ) == 0', "extended master reply dispatch")
    require(dispatch, "ProcessServersListExtMessage( from, msg );", "extended master reply dispatch")


def validate_console_address_arguments() -> None:
    """An unquoted IPv6 endpoint reaches a command split across arguments.

    The command tokenizer treats '[', ']' and '%' as punctuation and only glues
    a ":port" suffix onto a dotted quad, so `connect [2001:db8::1]:27650` used
    to arrive as nine arguments and be rejected with a usage message.
    """
    source = read("src/framework/async/AsyncNetwork.cpp")
    joiner = function_body(source, "static idStr Net_AddressArgument", "console address rejoin")
    for token in (
        "for ( int i = 1; i < args.Argc(); i++ )",
        "address += args.Argv( i );",
    ):
        require(joiner, token, "console address rejoin")
    # Inserting the tokenizer's separators back would corrupt the address.
    reject(joiner, '" "', "console address rejoin separator")

    connect = function_body(source, "void idAsyncNetwork::Connect_f", "connect command")
    require(connect, "const idStr serverName = Net_AddressArgument( args );", "connect address rejoin")
    require(connect, "client.ConnectToServer( serverName.c_str() );", "connect address use")
    # A fixed argument count is exactly what rejected every bracketed literal.
    reject(connect, "args.Argc() != 2", "connect fixed argument count")
    reject(connect, "client.ConnectToServer( args.Argv( 1 ) )", "connect single-argument address")
    # The game-module reload replays this command, so it must carry one argument.
    require(connect, "reloadArgs.AppendArg( serverName.c_str() );", "connect reload address")

    server_info = function_body(source, "void idAsyncNetwork::GetServerInfo_f", "serverInfo command")
    require(server_info, "Net_AddressArgument( args )", "serverInfo address rejoin")
    reject(server_info, "args.Argv( 1 )", "serverInfo single-argument address")

    # The Join-by-IP box validates and canonicalizes the endpoint, then
    # supplies a typed argument so IPv6 punctuation never becomes command text.
    menu = read("src/framework/Session_menu.cpp")
    require(menu, 'connectArgs.AppendArg( Sys_NetAdrToString( address ) );', "GUI join-by-IP address")
    require(menu, 'BufferCommandArgs( CMD_EXEC_NOW, connectArgs )', "GUI typed connect command")
    reject(menu, 'va( "connect \\"%s\\"", s )', "GUI untrusted command interpolation")


def validate_family_scoped_resolution() -> None:
    """A disabled family must not be resolved to an address nothing can send."""
    for relative_path, platform in ((WINDOWS_NET, "Windows"), (POSIX_NET, "POSIX")):
        source = read(relative_path)
        resolver = function_body(
            source,
            "Net_StringToSockaddr" if platform == "Windows" else "static bool Sys_ResolveSockaddr",
            f"{platform} resolver",
        )
        for token in (
            "if ( family == AF_UNSPEC )",
            "const bool allowIPv4 = net_enableIPv4.GetBool();",
            "const bool allowIPv6 = net_enableIPv6.GetBool();",
            "if ( allowIPv4 != allowIPv6 )",
            "family = allowIPv6 ? AF_INET6 : AF_INET;",
        ):
            require(resolver, token, f"{platform} family-scoped resolution")
        require_before(
            resolver,
            "family = allowIPv6 ? AF_INET6 : AF_INET;",
            "hints.ai_family = family;",
            f"{platform} family-scoped resolution",
        )


def validate_server_list_deduplication() -> None:
    """One endpoint must be listed once, however many replies name it."""
    source = read("src/framework/async/ServerScan.cpp")
    add = function_body(source, "void idServerScan::AddServer", "server list de-duplication")
    for token in (
        "for ( int i = 0; i < net_servers.Num(); i++ )",
        "Sys_CompareNetAdrBase( net_servers[ i ].adr, s.adr ) && net_servers[ i ].adr.port == s.adr.port",
        "return;",
    ):
        require(add, token, "server list de-duplication")
    require_before(
        add,
        "Sys_CompareNetAdrBase( net_servers[ i ].adr, s.adr )",
        "net_servers.Append( s );",
        "server list de-duplication",
    )


def validate_local_server_address_text() -> None:
    """The loading screen and the network system must not disagree."""
    for relative_path, signature, context in (
        ("src/framework/Session.cpp", "idStr serverAddress = networkSystem->GetServerAddress();", "session loading screen"),
        ("src/framework/async/NetworkSystem.cpp", "idStr serverAddress = GetServerAddress();", "network system loading screen"),
    ):
        source = read(relative_path)
        start = source.index(signature)
        window = source[start : start + 800]
        for token in (
            # An interface may be a host name, so the buffer is sized for text
            # rather than for an address literal; overflow fails closed and
            # would otherwise show nothing at all.
            "char localEndpoint[idNetworkEndpoint::LOCAL_ENDPOINT_TEXT_SIZE];",
            "idNetworkEndpoint::FormatLocalServerEndpoint( cvarSystem->GetCVarString( \"net_ip\" ),",
            "serverAddress = localEndpoint;",
        ):
            require(window, token, context)
        # The duplicated hand-rolled IPv4 literal is what let the two sites drift.
        reject(window, 'va( "127.0.0.1:%d", netPort )', context)
        reject(window, 'idStr::Icmp( netIP, "0.0.0.0" )', context)


def validate_runtime_ipv6_selftest() -> None:
    common = read("src/framework/Common.cpp")
    body = function_body(common, "static void Com_NetIPv6SelfTest_f", "runtime IPv6 self-test")

    for probe, reason in (
        (
            'if ( !Sys_StringToNetAdr( "[::1]:65535", &address, false ) || address.type != NA_IP6 ||',
            "bracketed IPv6 endpoint did not round-trip",
        ),
        (
            'if ( !Sys_StringToNetAdr( "::1", &address, false ) || address.type != NA_IP6 ||',
            "unbracketed IPv6 literal did not parse",
        ),
        (
            'if ( Sys_StringToNetAdr( "[::1]:65536", &address, false ) || address.type != NA_BAD )',
            "out-of-range IPv6 port was accepted",
        ),
        (
            'if ( Sys_StringToNetAdr( "[::1", &address, false ) || address.type != NA_BAD )',
            "unterminated IPv6 bracket was accepted",
        ),
        (
            'if ( !Sys_StringToNetAdr( "[::ffff:127.0.0.1]:65535", &address, false ) || address.type != NA_LOOPBACK ||',
            "IPv4-mapped address was not normalized",
        ),
    ):
        branch = function_body(body, probe, f"runtime IPv6 probe: {reason}")
        require(branch, f'Common_NetIPv6SelfTestFailure( passed, "{reason}" );', f"runtime IPv6 probe: {reason}")

    for token in (
        'idStr::Cmp( Sys_NetAdrToString( address ), "[2001:db8::1]:27650" ) != 0',
        'Common_NetIPv6SelfTestFailure( passed, "IPv6 text was not canonicalized" );',
        'Common_NetIPv6SelfTestFailure( passed, "IPv6 address text did not survive a round-trip" );',
        'Common_NetIPv6SelfTestFailure( passed, "distinct IPv6 addresses compared equal" );',
        'Common_NetIPv6SelfTestFailure( passed, "IPv6 loopback was not treated as a LAN address" );',
        'Common_NetIPv6SelfTestFailure( passed, "IPv6 link-local was not treated as a LAN address" );',
    ):
        require(body, token, "runtime IPv6 formatting and classification")

    # The transport section has to run over IPv6 alone, or an IPv4 fallback
    # could satisfy it without any IPv6 traffic being exchanged.
    require(body, 'cvarSystem->SetCVarString( "net_enableIPv4", "0" );', "runtime IPv6-only transport isolation")
    require_before(
        body,
        'cvarSystem->SetCVarString( "net_enableIPv4", "0" );',
        "receiver.InitForPort( candidatePort )",
        "runtime IPv6-only transport isolation",
    )
    require(body, 'va( "[::1]:%d", receiverPort )', "runtime IPv6 loopback destination")
    for token in (
        'Common_NetIPv6SelfTestFailure( passed, "IPv6 UDP loopback payload did not round-trip" );',
        'Common_NetIPv6SelfTestFailure( passed, "IPv6 UDP loopback reply did not round-trip" );',
        'Common_NetIPv6SelfTestFailure( passed, "exact-capacity IPv6 UDP datagram was rejected" );',
        'Common_NetIPv6SelfTestFailure( passed, "oversized IPv6 UDP datagram was not rejected" );',
        'Common_NetIPv6SelfTestFailure( passed, "packet after oversized IPv6 UDP datagram was not preserved" );',
        'Common_NetIPv6SelfTestFailure( passed, "IPv6 socket close/reopen lifecycle failed" );',
    ):
        require(body, token, "runtime IPv6 transport")

    oversize_send = body.index("sender.SendPacket( destination, oversizePacket")
    marker_send = body.index("sender.SendPacket( destination, markerPacket")
    if not oversize_send < marker_send:
        raise AssertionError("Runtime IPv6 self-test must reject oversize data before accepting its marker")

    # One endpoint serving both families at one port is what lets a dual-stack
    # server publish a single address to every client.
    for token in (
        'cvarSystem->SetCVarString( "net_enableIPv4", "1" );',
        'Common_NetIPv6SelfTestFailure( passed, "dual-stack port did not bind" );',
        'Common_NetIPv6SelfTestFailure( passed, "dual-stack port did not receive over IPv6" );',
        'Common_NetIPv6SelfTestFailure( passed, "dual-stack port did not receive over IPv4" );',
    ):
        require(body, token, "runtime dual-stack transport")

    # A host with IPv6 switched off is a supported configuration, so an
    # unavailable transport must not be reported as a failed contract.
    require(
        body,
        'common->Printf( "IPv6 network self-test: skipped transport checks - the host bound no IPv6 socket\\n" );',
        "runtime IPv6 unavailable transport",
    )

    for token in (
        '{ "net_ip", "net_ip6", "net_enableIPv4", "net_enableIPv6", "net_forceLatency", "net_forceDrop" }',
        '{ "127.0.0.1", "::1", "1", "1", "0", "0" }',
    ):
        require(body, token, "runtime IPv6 network-cvar isolation")

    restore_value = body.index("savedCVarValues[i].c_str()")
    restore_flags = body.index("savedCVarFlags[i]", restore_value)
    restore_modified = body.index("Common_RestorePerformancePresetModifiedFlags( savedModifiedFlags )")
    pass_marker = body.index('"IPv6 network self-test: passed\\n"')
    if not restore_value < restore_flags < restore_modified < pass_marker:
        raise AssertionError("Runtime IPv6 self-test must restore network cvars before reporting its result")
    if body.count('"IPv6 network self-test: passed\\n"') != 1 or body.count(
        '"IPv6 network self-test: FAILED\\n"'
    ) != 1:
        raise AssertionError("Runtime IPv6 self-test must expose one stable pass and failure marker")

    require(
        common,
        'cmdSystem->AddCommand( "netIPv6SelfTest", Com_NetIPv6SelfTest_f, CMD_FL_SYSTEM',
        "runtime IPv6 self-test command registration",
    )


def enum_body(source: str, typedef_name: str, context: str) -> str:
    """Slice a `typedef enum { ... } name;` by its trailing name, not its opening."""
    closing = f"}} {typedef_name};"
    end = source.find(closing)
    if end < 0:
        raise AssertionError(f"Missing enumeration {typedef_name!r} in {context}")
    start = source.rfind("typedef enum", 0, end)
    if start < 0:
        raise AssertionError(f"Missing opening for enumeration {typedef_name!r} in {context}")
    return source[start : end + len(closing)]


def validate_address_type_abi() -> None:
    """A new address type must not renumber the values the game module holds."""
    engine = read("src/sys/sys_public.h")
    enum_body_text = enum_body(engine, "netadrtype_t", "address type enumeration")
    require(enum_body_text, "NA_MULTICAST6", "IPv6 discovery address type")
    order = [
        "NA_BAD",
        "NA_LOOPBACK",
        "NA_BROADCAST",
        "NA_IP",
        "NA_IP6",
        "NA_BOT",
        "NA_MULTICAST6",
    ]
    positions = [enum_body_text.index(name) for name in order]
    if positions != sorted(positions):
        raise AssertionError("netadrtype_t values must keep their existing order, appending new types last")

    companion_path = GAME_LIBS_ROOT / "src" / "sys" / "sys_public.h"
    if not companion_path.is_file():
        print(f"network_ipv6_support: skipped netadrtype_t mirror check (no GameLibs header at {companion_path})")
        return
    companion = companion_path.read_text(encoding="utf-8", errors="strict")
    companion_enum = enum_body(companion, "netadrtype_t", "GameLibs address type enumeration")
    if re.sub(r"\s+", " ", enum_body_text).strip() != re.sub(r"\s+", " ", companion_enum).strip():
        raise AssertionError("Engine and GameLibs netadrtype_t declarations are not synchronized")


def validate_native_endpoint_coverage() -> None:
    """Pin the grammar and the canonical text in the native test."""
    source = read("tools/tests/native/CoreSafetyTest.cpp")
    for token in (
        '"fe80::1%eth0"',
        '"[fe80::1%eth0]:27650"',
        '"::ffff:1.2.3.4"',
        '"2001:db8::1"',
        '"[2001:db8::1]"',
        '"fe80::1%eth0:27650"',
        "ExpectIPv6Text",
        "ExpectLocalServerEndpoint",
    ):
        require(source, token, "native IPv6 endpoint coverage")
    # Anchor each canonical-text case on its hex input, so a Split-grammar line
    # that merely mentions the same address text cannot satisfy the pin.
    for hex_input, expected in (
        ("00000000000000000000000000000000", '"::"'),
        ("00000000000000000000000000000001", '"::1"'),
        ("20010db8000000000000000000000001", '"2001:db8::1"'),
        ("20010000000100010001000100010001", '"2001:0:1:1:1:1:1:1"'),
        ("00010000000000020000000000030004", '"1::2:0:0:3:4"'),
        ("00010000000000020000000000000003", '"1:0:0:2::3"'),
        ("00000000000000000000ffff01020304", '"::ffff:1.2.3.4"'),
        ("ffffffffffffffffffffffff01020304", '"ffff:ffff:ffff:ffff:ffff:ffff:102:304"'),
    ):
        require(
            source,
            f'ExpectIPv6Text( "{hex_input}", {expected}',
            "native canonical IPv6 text coverage",
        )
    for token in (
        'ExpectIPv6Endpoint( "fe800000000000000000000000000001", 12, 27650, "[fe80::1%12]:27650"',
        'ExpectIPv6Endpoint( "00000000000000000000000000000001", 0, 0, "::1"',
    ):
        require(source, token, "native canonical IPv6 endpoint coverage")


def validate_network_contract_wiring() -> None:
    test_paths = (
        "tools/tests/network_ipv4_support.py",
        "tools/tests/network_ipv6_support.py",
    )
    for relative_path in (
        ".github/workflows/commit-validation.yml",
        ".github/workflows/push-verification.yml",
    ):
        workflow = read(relative_path)
        for test_path in test_paths:
            require(workflow, f"{test_path} \\", f"{relative_path} focused regression syntax-check wiring")
            require(workflow, f"python {test_path}", f"{relative_path} focused regression execution wiring")

    validator = read("tools/validation/openq4_validate.py")
    for test_path in test_paths:
        filename = Path(test_path).name
        require(validator, f'"tools" / "tests" / "{filename}"', "local validation focused regression wiring")


def validate_ipv6_delivery_docs() -> None:
    networking = read("docs/user/multiplayer-networking.md")
    for token in (
        "IPv6",
        "`net_ip6`",
        "`net_enableIPv4`",
        "`net_enableIPv6`",
        "`net_mcast6addr`",
        "netIPv6SelfTest",
        "[2001:db8::1]:27650",
    ):
        require(networking, token, "IPv6 multiplayer networking guide")

    server_guide = read("docs/user/server-setup.md")
    for token in ("net_ip6", "IPv6", "netIPv6SelfTest"):
        require(server_guide, token, "IPv6 server setup guide")

    building = read("BUILDING.md")
    for token in ("netIPv6SelfTest", "IPv6"):
        require(building, token, "IPv6 self-test build guidance")


def validate_critical_mutation_sensitivity() -> None:
    """Prove the contracts above actually reject the regressions they describe."""
    header = read("src/sys/NetworkEndpoint.h")

    tie_break = "if ( runLength > bestLength )"
    if header.count(tie_break) != 1:
        raise AssertionError("RFC 5952 tie-break mutation anchor is not unique")
    expect_contract_rejection(
        lambda candidate: _validate_shared_ipv6_text_body(candidate),
        header.replace(tie_break, "if ( runLength >= bestLength )", 1),
        "the rightmost zero run is compressed instead of the leftmost",
    )

    single_group = "if ( bestLength < 2 )"
    if header.count(single_group) != 1:
        raise AssertionError("RFC 5952 single-group mutation anchor is not unique")
    expect_contract_rejection(
        lambda candidate: _validate_shared_ipv6_text_body(candidate),
        header.replace(single_group, "if ( bestLength < 1 )", 1),
        "a single zero group is compressed to ::",
    )

    for relative_path, platform in ((WINDOWS_NET, "Windows"), (POSIX_NET, "POSIX")):
        source = read(relative_path)
        anchor = "if ( idNetworkEndpoint::IsIPv4Mapped( a->ip6 ) )"
        if source.count(anchor) != 1:
            raise AssertionError(f"{platform} IPv4-mapped mutation anchor is not unique")
        expect_contract_rejection(
            lambda candidate, path=relative_path, name=platform: _validate_mapped_normalization(candidate, name),
            source.replace(anchor, "if ( false )", 1),
            f"{platform} leaves an IPv4 peer with a second IPv6 identity",
        )

        # Dropping the per-link fan-out would silently scan one link only.
        fanout = function_body(source, "static void Net_SendMulticast6Packet(", f"{platform} multicast fan-out")
        for original, replacement, context in (
            (
                "for ( int i = 0; i < num_interfaces6; i++ ) {",
                "for ( int i = 0; i < 0; i++ ) {",
                "the per-link multicast fan-out is skipped",
            ),
            (
                "duplicate = true;",
                "duplicate = false;",
                "one link is scanned once per address it carries",
            ),
        ):
            if fanout.count(original) != 1:
                raise AssertionError(f"{platform} fan-out anchor is not unique: {original!r}")
            expect_contract_rejection(
                lambda candidate, name=platform: _validate_multicast_fanout(candidate, name),
                fanout.replace(original, replacement, 1),
                f"{platform}: {context}",
            )

    channel = read("src/framework/async/MsgChannel.cpp")
    fragment_anchor = "return adr.type == NA_IP6 ? FRAGMENT_SIZE6 : FRAGMENT_SIZE;"
    if channel.count(fragment_anchor) != 1:
        raise AssertionError("IPv6 fragment-size mutation anchor is not unique")
    # A family-blind fragment size would put 1400 byte datagrams on a path that
    # only guarantees 1280, where they are dropped rather than fragmented.
    expect_contract_rejection(
        _validate_fragment_size_body,
        channel.replace(fragment_anchor, "return FRAGMENT_SIZE;", 1),
        "the fragment size ignores the address family",
    )
    # Deriving it once and reusing the IPv4 constant elsewhere would desynchronise
    # the two ends of the fragment protocol.
    send_next = function_body(channel, "void idMsgChannel::SendNextFragment", "IPv6 fragment emission")
    if send_next.count("Net_FragmentSize( remoteAddress )") < 1:
        raise AssertionError("IPv6 fragment emission mutation anchor is missing")
    expect_contract_rejection(
        _validate_fragment_size_body,
        channel.replace("fragLength = Net_FragmentSize( remoteAddress );", "fragLength = FRAGMENT_SIZE;", 1),
        "the emitted fragment length ignores the address family",
    )


def _validate_shared_ipv6_text_body(candidate: str) -> None:
    formatter = function_body(candidate, "inline bool FormatIPv6(", "RFC 5952 formatter")
    require(formatter, "if ( runLength > bestLength )", "RFC 5952 formatter")
    require(formatter, "if ( bestLength < 2 )", "RFC 5952 formatter")
    reject(formatter, "runLength >= bestLength", "RFC 5952 leftmost-run tie-break")
    reject(formatter, "if ( bestLength < 1 )", "RFC 5952 single-group compression")


def _validate_multicast_fanout(candidate: str, platform: str) -> None:
    for token in (
        "for ( int i = 0; i < num_interfaces6; i++ )",
        "group.sin6_scope_id = netint6[i].scopeId;",
        "duplicate = true;",
    ):
        require(candidate, token, f"{platform} multicast fan-out")


def _validate_fragment_size_body(candidate: str) -> None:
    require(candidate, "#define	MAX_PACKETLEN6			1232", "IPv6 fragment sizing")
    require(
        candidate,
        "return adr.type == NA_IP6 ? FRAGMENT_SIZE6 : FRAGMENT_SIZE;",
        "IPv6 fragment sizing",
    )
    for signature, context in (
        ("void idMsgChannel::SendNextFragment", "IPv6 fragment emission"),
        ("int idMsgChannel::SendMessage", "IPv6 fragment threshold"),
        ("bool idMsgChannel::Process", "IPv6 final-fragment detection"),
    ):
        body = function_body(candidate, signature, context)
        require(body, "Net_FragmentSize( remoteAddress )", context)
        if re.search(r"\bFRAGMENT_SIZE\b", body):
            raise AssertionError(f"Family-blind FRAGMENT_SIZE still decides {context}")


def _validate_mapped_normalization(candidate: str, platform: str) -> None:
    sockadr = function_body(candidate, "SockadrToNetadr", f"{platform} sockaddr conversion")
    function_body(
        sockadr,
        "if ( idNetworkEndpoint::IsIPv4Mapped( a->ip6 ) )",
        f"{platform} IPv4-mapped normalization",
    )


def main() -> None:
    validate_shared_ipv6_text()
    validate_bind_plan()
    validate_platform_ipv6_transport(WINDOWS_NET, "Windows")
    validate_platform_ipv6_transport(POSIX_NET, "POSIX")
    validate_windows_ipv6_specifics()
    validate_posix_ipv6_specifics()
    validate_ipv6_fragment_size()
    validate_lan_discovery()
    validate_master_server_ipv6_list()
    validate_console_address_arguments()
    validate_family_scoped_resolution()
    validate_server_list_deduplication()
    validate_local_server_address_text()
    validate_runtime_ipv6_selftest()
    validate_address_type_abi()
    validate_native_endpoint_coverage()
    validate_network_contract_wiring()
    validate_ipv6_delivery_docs()
    validate_critical_mutation_sensitivity()
    print("network_ipv6_support: ok")


if __name__ == "__main__":
    main()
