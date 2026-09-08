#!/usr/bin/env python3
"""Static regression contract for cross-platform IPv4 networking safety."""

from __future__ import annotations

import ast
import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")).resolve()


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


def python_function_body(source: str, name: str, context: str) -> str:
    tree = ast.parse(source)
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name == name:
            if node.end_lineno is None:
                raise AssertionError(f"Missing end line for Python function {name!r} in {context}")
            lines = source.splitlines(keepends=True)
            return "".join(lines[node.lineno - 1 : node.end_lineno])
    raise AssertionError(f"Missing Python function {name!r} in {context}")


def called_c_helper_with_token(
    source: str,
    caller: str,
    before: str,
    token: str,
    context: str,
) -> tuple[str, str]:
    """Resolve a checked static helper called before a sensitive operation."""
    require(caller, before, context)
    corridor = caller[: caller.index(before)]
    definition = re.compile(r"(?m)^(?:static\s+)?(?:bool|void|int)\s+([A-Za-z_]\w*)\s*\(")
    for match in definition.finditer(source):
        name = match.group(1)
        checked_call = re.compile(rf"if\s*\([^{{}};]*\b{re.escape(name)}\s*\(")
        if checked_call.search(corridor) is None:
            continue
        helper = function_body(source[match.start() :], match.group(0), context)
        if token in helper:
            return name, helper
    raise AssertionError(f"Missing checked helper containing {token!r} in {context}")


def preprocessor_stack_at(source: str, offset: int) -> list[str]:
    stack: list[str] = []
    for line in source[:offset].splitlines():
        directive = line.strip()
        if directive.startswith(("#if ", "#ifdef ", "#ifndef ")):
            stack.append(directive)
        elif directive.startswith(("#elif ", "#else")):
            if stack:
                stack[-1] = f"{stack[-1]} | {directive}"
        elif directive.startswith("#endif") and stack:
            stack.pop()
    return stack


def validate_shared_endpoint_parser() -> None:
    source = read("src/sys/NetworkEndpoint.h")
    public = read("src/sys/sys_public.h")

    for token in (
        "*cursor < '0' || *cursor > '9'",
        "parsedPort = parsedPort * 10",
        "parsedPort > 6553 || ( parsedPort == 6553 && digit > 5 )",
        "parts.hasPort = false;",
        "parts.port = 0;",
        "host[0] = '\\0';",
    ):
        require(source, token, "shared endpoint parser")
    require_before(
        source,
        "parsedPort > 6553 || ( parsedPort == 6553 && digit > 5 )",
        "parsedPort = parsedPort * 10 + digit",
        "overflow-safe endpoint port parser",
    )
    require(public, "Init( const char *host, int port );", "full-range TCP port API")
    reject(public, "Init( const char *host, short port );", "signed-short TCP port API")


def validate_cross_platform_network_cvar_defaults() -> None:
    windows = read("src/sys/win32/win_net.cpp")
    posix = read("src/sys/posix/posix_net.cpp")
    default = 'idCVar net_port( "net_port", "0", CVAR_SYSTEM | CVAR_INTEGER, "local IP port number" );'
    require(windows, default, "Windows automatic net_port default")
    require(posix, default, "POSIX automatic net_port default")
    reject(posix, 'idCVar net_port( "net_port", "",', "POSIX empty net_port default")


def validate_network_contract_wiring() -> None:
    test_paths = (
        "tools/tests/network_ipv4_support.py",
        "tools/tests/network_ipv6_support.py",
        "tools/tests/posix_network_resolution.py",
        "tools/tests/game_type_module_selection.py",
    )
    for relative_path in (
        ".github/workflows/commit-validation.yml",
        ".github/workflows/push-verification.yml",
    ):
        workflow = read(relative_path)
        for test_path in test_paths:
            require(
                workflow,
                f"{test_path} \\",
                f"{relative_path} focused regression syntax-check wiring",
            )
            require(
                workflow,
                f"python {test_path}",
                f"{relative_path} focused regression execution wiring",
            )

    validator = read("tools/validation/openq4_validate.py")
    for test_path in test_paths:
        filename = Path(test_path).name
        require(
            validator,
            f'"tools" / "tests" / "{filename}"',
            "local validation focused regression wiring",
        )


def validate_dual_stack_bind_contract(body: str, platform: str) -> None:
    """Keep a resolved IPv4 bind failure from silently becoming IPv6-only.

    IPv4 is the baseline transport for discovery, LAN scans, masters, and
    ordinary clients. Callers scan a port range and stop on the first success,
    so handing them an IPv6-only endpoint after the requested IPv4 port failed
    would strand every IPv4 peer on an address they cannot reach.
    """
    for token in (
        "idNetworkEndpoint::PlanBind( ipv4Text, ipv6Text, net_enableIPv4.GetBool(), net_enableIPv6.GetBool(), plan );",
        "if ( !plan.bindIPv4 && !plan.bindIPv6 )",
        "bool ipv4Resolved = false;",
        "AF_INET, &bound4, true, &ipv4Resolved",
        "AF_INET6, &bound6, true )",
        "if ( idNetworkEndpoint::IsAnyInterfaceName( ipv6Interface ) )",
        "ipv6Interface = plan.ipv4Interface;",
    ):
        require(body, token, f"{platform} dual-stack bind plan")

    fail_closed = function_body(body, "if ( ipv4Resolved )", f"{platform} resolved IPv4 bind failure")
    require(fail_closed, "return false;", f"{platform} resolved IPv4 bind failure")
    reject(fail_closed, "ipv6Interface", f"{platform} resolved IPv4 bind failure")

    both_disabled = function_body(body, "if ( !plan.bindIPv4 && !plan.bindIPv6 )", f"{platform} disabled-family bind")
    require(both_disabled, "return false;", f"{platform} disabled-family bind")

    # Both families share one port number, so a dual-stack server is reached
    # the same way over either transport.
    require(body, "const int ipv6Port = ( portNumber == PORT_ANY && socket4", f"{platform} shared dual-stack port")

    plan_index = body.index("idNetworkEndpoint::PlanBind(")
    resolved_index = body.index("bool ipv4Resolved = false;")
    first_bind = body.index("AF_INET, &bound4, true, &ipv4Resolved")
    fail_closed_index = body.index("if ( ipv4Resolved )")
    second_bind = body.index("AF_INET6, &bound6, true )")
    if not (plan_index < resolved_index < first_bind < fail_closed_index < second_bind):
        raise AssertionError(
            f"{platform} resolved IPv4 bind failures must fail closed before the IPv6 socket is opened"
        )


def validate_windows_dual_stack_bind_contract(body: str) -> None:
    validate_dual_stack_bind_contract(body, "Windows")
    for token in (
        "socket4 = INVALID_SOCKET;",
        "socket6 = INVALID_SOCKET;",
        "if ( socket4 == INVALID_SOCKET && socket6 == INVALID_SOCKET )",
    ):
        require(body, token, "Windows dual-stack bind sentinels")


def validate_posix_dual_stack_bind_contract(body: str) -> None:
    validate_dual_stack_bind_contract(body, "POSIX")
    for token in (
        "socket4 = 0;",
        "socket6 = 0;",
        "if ( socket4 <= 0 && socket6 <= 0 )",
    ):
        require(body, token, "POSIX dual-stack bind sentinels")


def validate_windows_per_socket_lag_contract(source: str) -> None:
    """Keep latency queues private to the idPort instance that owns them."""
    if re.search(r"\bidUDPLag\s*\*\s*udpPorts\b|\budpPorts\s*\[", source):
        raise AssertionError("Windows latency state is globally indexed by bound port")

    constructor = function_body(source, "idPort::idPort()", "Windows UDP socket construction")
    require(constructor, "platformData = NULL;", "Windows per-socket latency construction")

    port_init = function_body(source, "bool idPort::InitForPort", "Windows UDP socket initialization")
    require(port_init, "platformData = new idUDPLag;", "Windows per-socket latency allocation")
    require_before(port_init, "Close();", "platformData = new idUDPLag;", "Windows latency-state replacement")
    require_before(
        port_init,
        "if ( !Net_BindDualStack(",
        "platformData = new idUDPLag;",
        "Windows latency allocation after successful bind",
    )
    failed_bind = function_body(
        port_init,
        "if ( !Net_BindDualStack(",
        "Windows failed UDP initialization",
    )
    reject(failed_bind, "new idUDPLag", "Windows failed-bind latency allocation")

    close = function_body(source, "void idPort::Close", "Windows UDP socket close")
    release = function_body(close, "if ( platformData != NULL )", "Windows per-socket latency release")
    require(release, "delete static_cast<idUDPLag *>( platformData );", "Windows exact latency-state release")
    require(release, "platformData = NULL;", "Windows latency-state close sentinel")
    if release.index("delete static_cast<idUDPLag *>( platformData );") > release.index("platformData = NULL;"):
        raise AssertionError("Windows idPort clears its latency pointer before deleting the owned state")

    for signature, context, queue_tokens in (
        (
            "bool idPort::GetPacket(",
            "Windows delayed UDP receive",
            ("lag->recieveFirst", "lag->udpMsgAllocator"),
        ),
        (
            "void idPort::SendPacket",
            "Windows delayed UDP send",
            ("lag->sendFirst", "lag->udpMsgAllocator"),
        ),
    ):
        body = function_body(source, signature, context)
        lag_declaration = "idUDPLag *lag = static_cast<idUDPLag *>( platformData );"
        require(body, lag_declaration, context)
        require(body, "lag == NULL", f"{context} closed-port guard")
        if body.index(lag_declaration) > body.index("lag == NULL"):
            raise AssertionError(f"{context} tests its private latency state before loading it")
        for token in queue_tokens:
            require(body, token, f"{context} private queue")
        reject(body, "bound_to.port", f"{context} port-keyed latency lookup")


def validate_idport_platform_data_mirror() -> None:
    engine_header = read("src/sys/sys_public.h")
    engine_port = function_body(engine_header, "class idPort", "engine idPort layout")
    require(engine_port, "void *\t\tplatformData;\t\t// OS specific per-socket state", "engine idPort private state")
    require_before(engine_port, "idSocketHandle_t\tnetSocket6;", "void *\t\tplatformData;", "engine idPort private layout")

    companion_header_path = GAME_LIBS_ROOT / "src" / "sys" / "sys_public.h"
    if not companion_header_path.is_file():
        print(f"network_ipv4_support: skipped idPort mirror check (no GameLibs header at {companion_header_path})")
        return
    companion_header = companion_header_path.read_text(encoding="utf-8", errors="strict")
    companion_port = function_body(companion_header, "class idPort", "GameLibs idPort layout")
    if re.sub(r"\s+", " ", engine_port).strip() != re.sub(r"\s+", " ", companion_port).strip():
        raise AssertionError("Engine and GameLibs idPort layouts are not synchronized")

    game_api = GAME_LIBS_ROOT / "src" / "game" / "Game.h"
    if game_api.is_file():
        require(
            game_api.read_text(encoding="utf-8", errors="strict"),
            "const int GAME_API_VERSION\t\t= 47;",
            "idPort-layout game API revision",
        )


def validate_windows_networking() -> None:
    source = read("src/sys/win32/win_net.cpp")

    validate_windows_per_socket_lag_contract(source)

    for token in (
        "static SOCKET\tip_socket = INVALID_SOCKET;",
        "static SOCKET\tsocks_socket = INVALID_SOCKET;",
    ):
        require(source, token, "Windows global socket sentinels")

    resolver = function_body(source, "static bool Net_StringToSockaddr", "Windows IPv4 resolver")
    for token in (
        "idNetworkEndpoint::Split",
        "getaddrinfo",
        "freeaddrinfo",
        "hints.ai_family = family;",
        "hints.ai_socktype = socketType;",
        "hints.ai_flags = AI_NUMERICSERV | ( doDNSResolve ? AI_ADDRCONFIG : AI_NUMERICHOST );",
    ):
        require(resolver, token, "Windows IPv4 resolver")
    # An explicit family must still take the first usable record, and AF_UNSPEC
    # must keep preferring an A record so the legacy IPv4 transports behave
    # exactly as before for a dual-stack hostname.
    require(
        resolver,
        "if ( selected == NULL || ( family == AF_UNSPEC && selected->ai_family != AF_INET && result->ai_family == AF_INET ) )",
        "Windows IPv4 A-record preference",
    )
    require(
        resolver,
        "if ( family != AF_UNSPEC || result->ai_family == AF_INET )",
        "Windows IPv4 resolver selection break",
    )
    require(resolver, "result->ai_addrlen > sizeof( *address )", "Windows resolver copy bound")
    require(
        resolver,
        "endpoint.hasPort ? endpoint.port : defaultPort",
        "Windows explicit TCP endpoint port precedence",
    )
    require(resolver, "effectivePort < 0 || effectivePort > 65535", "Windows TCP default port validation")
    for token in ("gethostbyname", "inet_addr", "Net_ExtractPort"):
        reject(source, token, "Windows legacy address parser")

    resolver_port_guard = function_body(
        resolver,
        "if ( effectivePort < 0 || effectivePort > 65535 )",
        "Windows resolver port guard",
    )
    require(resolver_port_guard, "return false;", "Windows resolver invalid-port result")

    socket_body = function_body(source, "SOCKET NET_IPSocket", "Windows UDP bind")
    socket_port_guard = function_body(
        socket_body,
        "if ( port != PORT_ANY && ( port < 0 || port > 65535 ) )",
        "Windows UDP bind port guard",
    )
    require(socket_port_guard, "return INVALID_SOCKET;", "Windows UDP bind invalid-port result")
    reject(socket_body, "return 0;", "Windows UDP socket failure sentinel")
    require(socket_body, "memset( bound_to, 0, sizeof( *bound_to ) );", "Windows bound-address initialization")
    require(socket_body, "bound_to->type = NA_BAD;", "Windows failed-bind address sentinel")
    require_before(
        socket_body,
        "if ( port != PORT_ANY && ( port < 0 || port > 65535 ) )",
        "socket( family, SOCK_DGRAM, IPPROTO_UDP )",
        "Windows UDP bind port validation",
    )
    require(
        socket_body,
        "const int bindPort = port == PORT_ANY ? 0 : port;",
        "Windows automatic UDP bind port",
    )
    require(
        socket_body,
        "htons( static_cast<unsigned short>( bindPort ) )",
        "Windows unsigned UDP bind port conversion",
    )
    # An interface string may carry its own port, which must never override the
    # port the caller asked to bind.
    for token in (
        "reinterpret_cast<struct sockaddr_in6 *>( &address )->sin6_port = htons( static_cast<unsigned short>( bindPort ) );",
        "reinterpret_cast<struct sockaddr_in *>( &address )->sin_port = htons( static_cast<unsigned short>( bindPort ) );",
    ):
        require(socket_body, token, "Windows explicit-interface bind port override")
    require(socket_body, "if ( family != AF_INET && family != AF_INET6 )", "Windows UDP bind family guard")

    bind_failure = function_body(
        socket_body,
        "} else if ( !Net_StringToSockaddr( net_interface, &address, &addressLen, true, family, bindPort ) )",
        "checked Windows explicit-interface bind",
    )
    require(bind_failure, "return INVALID_SOCKET;", "failed Windows explicit-interface bind result")
    reject(bind_failure, "closesocket(", "Windows explicit-interface bind precedes socket creation")

    for condition, cleanup, context in (
        ("if( ( newsocket = socket( family, SOCK_DGRAM, IPPROTO_UDP ) ) == INVALID_SOCKET )", None, "Windows UDP socket creation failure"),
        ("if( ioctlsocket( newsocket, FIONBIO, &_true ) == SOCKET_ERROR )", "closesocket( newsocket );", "Windows UDP nonblocking setup failure"),
        ("if( setsockopt( newsocket, SOL_SOCKET, SO_BROADCAST", "closesocket( newsocket );", "Windows UDP broadcast setup failure"),
        ("if( setsockopt( newsocket, IPPROTO_IPV6, IPV6_V6ONLY", "closesocket( newsocket );", "Windows UDP IPv6-only setup failure"),
        ("if( bind( newsocket", "closesocket( newsocket );", "Windows UDP bind failure"),
        ("if ( getsockname( newsocket", "closesocket( newsocket );", "Windows UDP bound-address query failure"),
    ):
        failure = function_body(socket_body, condition, context)
        if cleanup is not None:
            require(failure, cleanup, context)
        require(failure, "return INVALID_SOCKET;", context)

    address_body = function_body(source, "bool Sys_StringToNetAdr", "Windows address conversion")
    require(address_body, "if ( a == NULL )", "Windows address output guard")
    require(address_body, "memset( a, 0, sizeof( *a ) );", "Windows address output initialization")
    require(address_body, "a->type = NA_BAD;", "Windows failed-address sentinel")

    packet_body = function_body(source, "bool Net_GetUDPPacket", "Windows UDP receive")
    require(packet_body, "size = 0;", "Windows packet-size initialization")
    require(packet_body, "memset( &net_from, 0, sizeof( net_from ) );", "Windows packet-address initialization")
    require(packet_body, "net_from.type = NA_BAD;", "Windows packet-address sentinel")
    message_too_large = function_body(packet_body, "if ( err == WSAEMSGSIZE )", "Windows truncated datagram")
    require(message_too_large, "Net_SockadrToNetadr(", "Windows truncated-datagram source")
    require(message_too_large, "memset( &net_from, 0, sizeof( net_from ) );", "Windows truncated source reset")
    require(message_too_large, "net_from.type = NA_BAD;", "Windows truncated source sentinel")
    require(message_too_large, "return false;", "Windows truncated-datagram result")
    if not (
        message_too_large.index("Net_SockadrToNetadr")
        < message_too_large.index("memset( &net_from")
        < message_too_large.index("net_from.type = NA_BAD;")
        < message_too_large.index("return false;")
    ):
        raise AssertionError("Windows truncated datagrams must clear their source before returning")
    reject(packet_body, "ret == maxSize", "Windows exact-capacity datagram handling")
    packet_socket_guard = function_body(
        packet_body,
        "if( netSocket == INVALID_SOCKET )",
        "Windows invalid UDP receive socket",
    )
    require(packet_socket_guard, "return false;", "Windows invalid UDP receive socket")

    public_receive = function_body(source, "bool idPort::GetPacket(", "Windows public packet output")
    require(public_receive, "size = 0;", "Windows public packet-size initialization")
    require(public_receive, "memset( &from, 0, sizeof( from ) );", "Windows public packet-address initialization")
    require(public_receive, "from.type = NA_BAD;", "Windows public packet-address sentinel")
    public_receive_guard = "if ( ( netSocket == INVALID_SOCKET && netSocket6 == INVALID_SOCKET ) || lag == NULL || data == NULL || maxSize <= 0 )"
    require(public_receive, public_receive_guard, "Windows public packet input guard")
    if public_receive.index("from.type = NA_BAD;") > public_receive.index(public_receive_guard):
        raise AssertionError("Windows packet source must be initialized before early returns")

    receive_queue_guard = function_body(
        public_receive,
        "if ( size > MAX_UDP_MSG_SIZE )",
        "Windows delayed-receive queue capacity",
    )
    require(receive_queue_guard, "continue;", "Windows oversized delayed-receive result")
    require_before(
        public_receive,
        "if ( size > MAX_UDP_MSG_SIZE )",
        "udpMsgAllocator.Alloc()",
        "Windows delayed-receive queue allocation",
    )

    queued_guard_token = "if ( msg->size > maxSize )"
    queued_copy_token = "memcpy( data, msg->data, msg->size );"
    if queued_guard_token in public_receive:
        queued_copy_guard = function_body(
            public_receive,
            queued_guard_token,
            "Windows delayed-receive caller capacity",
        )
        require_before(
            public_receive,
            queued_guard_token,
            queued_copy_token,
            "Windows delayed-receive caller capacity",
        )
    else:
        helper_name, helper_body = called_c_helper_with_token(
            source,
            public_receive,
            queued_copy_token,
            queued_guard_token,
            "Windows delayed-receive capacity helper",
        )
        queued_copy_guard = function_body(
            helper_body,
            queued_guard_token,
            "Windows delayed-receive helper capacity",
        )
        require_before(
            public_receive,
            f"{helper_name}(",
            queued_copy_token,
            "Windows delayed-receive capacity helper call",
        )
        if queued_copy_token in helper_body:
            require_before(
                helper_body,
                queued_guard_token,
                queued_copy_token,
                "Windows delayed-receive helper capacity",
            )
    for token in (
        "recieveFirst = msg->next;",
        "recieveLast = NULL;",
        "udpMsgAllocator.Free( msg );",
        "size = 0;",
        "memset( &from, 0, sizeof( from ) );",
        "from.type = NA_BAD;",
        "return false;",
    ):
        require(queued_copy_guard, token, "Windows delayed-receive caller capacity")
    reject(queued_copy_guard, "memcpy( data", "Windows rejected delayed-receive copy")

    send_packet = function_body(source, "void idPort::SendPacket", "Windows public UDP send")
    public_send_socket_guard = function_body(
        send_packet,
        "if ( ( netSocket == INVALID_SOCKET && netSocket6 == INVALID_SOCKET ) || lag == NULL )",
        "Windows public UDP send socket guard",
    )
    require(public_send_socket_guard, "return;", "Windows public UDP send socket guard")
    send_queue_guard = function_body(
        send_packet,
        "if ( size > MAX_UDP_MSG_SIZE )",
        "Windows delayed-send queue capacity",
    )
    require(send_queue_guard, "return;", "Windows oversized delayed-send result")
    require_before(
        send_packet,
        "if ( size > MAX_UDP_MSG_SIZE )",
        "udpMsgAllocator.Alloc()",
        "Windows delayed-send queue allocation",
    )

    public_init = function_body(source, "bool idPort::InitForPort", "Windows public UDP initialization")
    public_port_guard = function_body(
        public_init,
        "if ( portNumber != PORT_ANY && ( portNumber < 0 || portNumber > 65535 ) )",
        "Windows public UDP port guard",
    )
    require(public_port_guard, "return false;", "Windows public UDP invalid-port result")
    require_before(
        public_init,
        "if ( portNumber != PORT_ANY && ( portNumber < 0 || portNumber > 65535 ) )",
        "Net_BindDualStack(",
        "Windows public UDP port validation",
    )
    public_init_failure = function_body(
        public_init,
        "if ( !Net_BindDualStack(",
        "Windows public UDP initialization failure",
    )
    for token in (
        "netSocket = INVALID_SOCKET;",
        "netSocket6 = INVALID_SOCKET;",
        "memset( &bound_to, 0, sizeof( bound_to ) );",
        "return false;",
    ):
        require(public_init_failure, token, "Windows public UDP failure sentinel")

    validate_windows_dual_stack_bind_contract(
        function_body(source, "static bool Net_BindDualStack", "Windows dual-stack bind")
    )

    port_constructor = function_body(source, "idPort::idPort()", "Windows UDP socket construction")
    require(port_constructor, "netSocket = INVALID_SOCKET;", "Windows UDP socket construction")
    require(port_constructor, "netSocket6 = INVALID_SOCKET;", "Windows UDP socket construction")
    require(port_constructor, "platformData = NULL;", "Windows UDP socket construction")
    port_close = function_body(source, "void idPort::Close", "Windows UDP socket close")
    for socket_name in ("netSocket", "netSocket6"):
        close_branch = function_body(
            port_close,
            f"if ( {socket_name} != INVALID_SOCKET )",
            f"Windows {socket_name} close",
        )
        require(close_branch, f"closesocket( {socket_name} );", f"Windows {socket_name} close")
        require(close_branch, f"{socket_name} = INVALID_SOCKET;", f"Windows {socket_name} close sentinel")

    blocking_receive = function_body(
        source,
        "bool idPort::GetPacketBlocking",
        "Windows blocking UDP receive",
    )
    for token in (
        "size = 0;",
        "memset( &from, 0, sizeof( from ) );",
        "from.type = NA_BAD;",
    ):
        require(blocking_receive, token, "Windows blocking UDP output initialization")
        require_before(
            blocking_receive,
            token,
            "if ( timeout < 0 )",
            "Windows nonblocking UDP output initialization",
        )
    negative_timeout = function_body(
        blocking_receive,
        "if ( timeout < 0 )",
        "Windows nonblocking UDP poll",
    )
    require(
        negative_timeout,
        "return GetPacket( from, data, size, maxSize );",
        "Windows nonblocking UDP poll result",
    )
    require_before(
        blocking_receive,
        "if ( timeout < 0 )",
        "Net_WaitForUDPPacket( netSocket, netSocket6, timeout );",
        "Windows nonblocking UDP poll",
    )

    tcp_init = function_body(source, "bool idTCP::Init( const char *host, int port )", "Windows TCP initialization")
    reject(tcp_init, "if ( port < 0 )", "Windows pre-parse TCP default-port rejection")
    require(
        tcp_init,
        "!Net_StringToSockaddr( host, &sadr, &sadrLen, true, AF_UNSPEC, port, SOCK_STREAM ) || !Net_SockadrToNetadr(",
        "Windows TCP checked resolver",
    )
    require(tcp_init, "socket( sadr.ss_family, SOCK_STREAM, IPPROTO_TCP )", "Windows TCP socket protocol")
    require_before(tcp_init, "Net_StringToSockaddr(", "socket( sadr.ss_family", "Windows TCP resolution")
    require_before(tcp_init, "Net_StringToSockaddr(", "if ( fd != INVALID_SOCKET )", "Windows TCP replacement validation")
    existing_connection = function_body(tcp_init, "if ( fd != INVALID_SOCKET )", "Windows TCP existing connection")
    require(existing_connection, "Close();", "Windows TCP existing-connection cleanup")

    tcp_socket_failure = function_body(
        tcp_init,
        "if ( ( fd = socket( sadr.ss_family, SOCK_STREAM, IPPROTO_TCP ) ) == INVALID_SOCKET )",
        "Windows TCP socket creation failure",
    )
    require(tcp_socket_failure, "fd = INVALID_SOCKET;", "Windows TCP socket creation sentinel")
    require(tcp_socket_failure, "return false;", "Windows TCP socket creation failure")

    tcp_connect_failure = function_body(
        tcp_init,
        "if ( connect( fd, reinterpret_cast<const struct sockaddr *>( &sadr ), sadrLen ) == SOCKET_ERROR )",
        "Windows TCP connect failure",
    )
    for token in ("closesocket( fd );", "fd = INVALID_SOCKET;", "return false;"):
        require(tcp_connect_failure, token, "Windows TCP connect failure")

    tcp_nonblocking_failure = function_body(
        tcp_init,
        "if( ioctlsocket( fd, FIONBIO, &_true ) == SOCKET_ERROR )",
        "Windows TCP nonblocking setup failure",
    )
    for token in ("closesocket( fd );", "fd = INVALID_SOCKET;", "return false;"):
        require(tcp_nonblocking_failure, token, "Windows TCP nonblocking setup failure")

    tcp_constructor = function_body(source, "idTCP::idTCP()", "Windows TCP construction")
    require(tcp_constructor, "fd = INVALID_SOCKET;", "Windows TCP construction sentinel")
    tcp_close = function_body(source, "void idTCP::Close", "Windows TCP close")
    tcp_close_guard = function_body(tcp_close, "if ( fd != INVALID_SOCKET )", "Windows TCP close")
    require(tcp_close_guard, "closesocket( fd );", "Windows TCP close cleanup")
    require(tcp_close, "fd = INVALID_SOCKET;", "Windows TCP close sentinel")

    tcp_read = function_body(source, "int idTCP::Read", "Windows TCP read")
    for condition, result, context in (
        ("if ( fd == INVALID_SOCKET )", "return -1;", "Windows TCP read uninitialized guard"),
        ("if ( size <= 0 )", "return 0;", "Windows TCP read empty-buffer guard"),
        ("if ( data == NULL )", "return -1;", "Windows TCP read null-buffer guard"),
    ):
        guard = function_body(tcp_read, condition, context)
        require(guard, result, context)
        require_before(tcp_read, condition, "recv( fd", context)
    tcp_read_error = function_body(
        tcp_read,
        "if ( ( nbytes = recv( fd, (char *)data, size, 0 ) ) == SOCKET_ERROR )",
        "Windows TCP read error",
    )
    tcp_read_would_block = function_body(
        tcp_read_error,
        "if ( WSAGetLastError() == WSAEWOULDBLOCK )",
        "Windows TCP read would-block",
    )
    require(tcp_read_would_block, "return 0;", "Windows TCP read would-block result")
    require(tcp_read_error, "Close();", "Windows TCP read fatal-error cleanup")
    require(tcp_read_error, "return -1;", "Windows TCP read fatal-error result")
    tcp_eof = function_body(tcp_read, "if ( nbytes == 0 )", "Windows TCP EOF")
    require(tcp_eof, "Close();", "Windows TCP EOF cleanup")
    require(tcp_eof, "return -1;", "Windows TCP EOF result")

    tcp_write = function_body(source, "int idTCP::Write", "Windows TCP write")
    for condition, result, context in (
        ("if ( fd == INVALID_SOCKET )", "return -1;", "Windows TCP write uninitialized guard"),
        ("if ( size <= 0 )", "return 0;", "Windows TCP write empty-buffer guard"),
        ("if ( data == NULL )", "return -1;", "Windows TCP write null-buffer guard"),
    ):
        guard = function_body(tcp_write, condition, context)
        require(guard, result, context)
        require_before(tcp_write, condition, "send( fd", context)
    tcp_write_error = function_body(
        tcp_write,
        "if ( ( nbytes = send( fd, (char *)data, size, 0 ) ) == SOCKET_ERROR )",
        "Windows TCP write error",
    )
    tcp_write_would_block = function_body(
        tcp_write_error,
        "if ( WSAGetLastError() == WSAEWOULDBLOCK )",
        "Windows TCP write would-block",
    )
    require(tcp_write_would_block, "return 0;", "Windows TCP write would-block result")
    require(tcp_write_error, "Close();", "Windows TCP write fatal-error cleanup")
    require(tcp_write_error, "return -1;", "Windows TCP write fatal-error result")
    wait_body = function_body(source, "bool Net_WaitForUDPPacket", "Windows UDP wait")
    wait_socket_guard = function_body(
        wait_body,
        "if ( netSocket == INVALID_SOCKET && netSocket6 == INVALID_SOCKET )",
        "Windows invalid UDP wait socket",
    )
    require(wait_socket_guard, "return false;", "Windows invalid UDP wait socket")
    require(wait_body, "ret == SOCKET_ERROR", "Windows select failure sentinel")
    require(wait_body, "NET_ErrorString()", "Windows select Winsock diagnostics")
    reject(wait_body, "strerror( errno )", "POSIX errno use in Windows select diagnostics")

    raw_send = function_body(source, "void Net_SendUDPPacket", "Windows raw UDP send")
    raw_send_socket_guard = function_body(
        raw_send,
        "if( netSocket == INVALID_SOCKET )",
        "Windows invalid UDP send socket",
    )
    require(raw_send_socket_guard, "return;", "Windows invalid UDP send socket")

    require(public_init, "Intentionally unsupported; see the compatibility note above the CVars.", "disabled Windows SOCKS UDP path")
    require(public_init, "NET_OpenSocks( portNumber );", "legacy Windows SOCKS UDP call")
    socks_call = source.index("NET_OpenSocks( portNumber );")
    if not any(directive == "#if 0" for directive in preprocessor_stack_at(source, socks_call)):
        raise AssertionError("Legacy Windows SOCKS UDP initialization must remain compile-time disabled")
    if source.count("NET_OpenSocks(") != 2:
        raise AssertionError("Windows SOCKS UDP initialization must have no enabled call sites")


def validate_posix_networking() -> None:
    source = read("src/sys/posix/posix_net.cpp")

    port_constructor = function_body(source, "idPort::idPort()", "POSIX UDP socket construction")
    require(port_constructor, "platformData = NULL;", "POSIX per-socket platform-state construction")
    port_close = function_body(source, "void idPort::Close", "POSIX UDP socket close")
    require(port_close, "platformData = NULL;", "POSIX per-socket platform-state close sentinel")

    network_init = function_body(source, "void Sys_InitNetworking(void)", "POSIX interface discovery")
    mac_branch_start = network_init.index("#if defined( MACOS_X ) || defined( __APPLE__ )")
    mac_branch_end = network_init.index("#else", mac_branch_start)
    mac_branch = network_init[mac_branch_start:mac_branch_end]
    mac_mask = "mask = Sys_SockaddrIPv4HostOrder( ifp->ifa_netmask );"
    mac_guard_token = "if ( mask == 0 )"
    mac_guard = function_body(mac_branch, mac_guard_token, "macOS zero-netmask rejection")
    require(mac_guard, "zero IPv4 netmask - skipped", "macOS zero-netmask diagnostic")
    require(mac_guard, "continue;", "macOS zero-netmask rejection")
    reject(mac_guard, "netint[", "macOS rejected zero-netmask storage")
    require_before(mac_branch, mac_mask, mac_guard_token, "macOS netmask validation")
    require_before(
        mac_branch,
        mac_guard_token,
        "netint[ num_interfaces ].mask = mask;",
        "macOS netmask validation",
    )

    linux_branch = network_init[mac_branch_end:network_init.rindex("#endif")]
    linux_mask = "mask = Sys_SockaddrIPv4HostOrder( &ifr->ifr_addr );"
    linux_guard_token = "if ( mask == 0 )"
    require_before(linux_branch, linux_mask, linux_guard_token, "Linux zero-netmask rejection")
    linux_guard_start = linux_branch.index(linux_guard_token)
    linux_guard = function_body(
        linux_branch[linux_guard_start:],
        linux_guard_token,
        "Linux zero-netmask rejection",
    )
    require(linux_guard, "zero IPv4 netmask - skipped", "Linux zero-netmask diagnostic")
    reject(linux_guard, "netint[", "Linux rejected zero-netmask storage")
    after_linux_guard = linux_branch[linux_guard_start + len(linux_guard) :].lstrip()
    if not after_linux_guard.startswith("else {"):
        raise AssertionError("Linux interface storage must be the else-path of the zero-netmask guard")
    linux_storage = function_body(after_linux_guard, "else", "Linux valid-netmask storage")
    require(linux_storage, "netint[ num_interfaces ].ip = ip;", "Linux valid-netmask storage")
    require(linux_storage, "netint[ num_interfaces ].mask = mask;", "Linux valid-netmask storage")

    resolver = function_body(source, "static bool Sys_ResolveSockaddr", "POSIX address resolver")
    require(
        resolver,
        "endpoint.hasPort ? endpoint.port : defaultPort",
        "POSIX explicit TCP endpoint port precedence",
    )
    require(resolver, "effectivePort < 0 || effectivePort > 65535", "POSIX TCP default port validation")

    socket_helper = function_body(source, "static int IPSocketForFamily", "POSIX UDP family bind")
    for token in (
        "bool *addressResolved = NULL",
        "*addressResolved = false;",
        "*addressResolved = true;",
    ):
        require(socket_helper, token, "POSIX preferred-family resolution result")
    require_before(
        socket_helper,
        "*addressResolved = false;",
        "Sys_ResolveSockaddr(",
        "POSIX preferred-family resolution initialization",
    )

    for signature, guard, failure, before, context in (
        (
            "static int IPSocketForFamily",
            "if ( port != PORT_ANY && ( port < 0 || port > 65535 ) )",
            "return 0;",
            "socket( family",
            "POSIX UDP socket port guard",
        ),
        (
            "bool idPort::InitForPort",
            "if ( portNumber != PORT_ANY && ( portNumber < 0 || portNumber > 65535 ) )",
            "return false;",
            "Net_BindDualStack(",
            "POSIX public port guard",
        ),
    ):
        port_body = function_body(source, signature, context)
        guard_body = function_body(port_body, guard, context)
        require(guard_body, failure, context)
        require_before(port_body, guard, before, context)

    port_init = function_body(source, "bool idPort::InitForPort", "POSIX public UDP initialization")
    port_init_failure = function_body(
        port_init,
        "if ( !Net_BindDualStack(",
        "POSIX public UDP initialization failure",
    )
    for token in (
        "netSocket = 0;",
        "netSocket6 = 0;",
        "memset( &bound_to, 0, sizeof( bound_to ) );",
        "return false;",
    ):
        require(port_init_failure, token, "POSIX public UDP failure sentinel")

    validate_posix_dual_stack_bind_contract(
        function_body(source, "static bool Net_BindDualStack", "POSIX dual-stack bind")
    )

    sockaddr_conversion = function_body(source, "static bool SockadrToNetadr", "POSIX sockaddr conversion")
    require(sockaddr_conversion, "memset( a, 0, sizeof( *a ) );", "POSIX sockaddr output initialization")
    require(sockaddr_conversion, "a->type = NA_BAD;", "POSIX sockaddr failed-address sentinel")
    require_before(
        sockaddr_conversion,
        "a->type = NA_BAD;",
        "if ( s->sa_family == AF_INET )",
        "POSIX sockaddr output initialization",
    )

    address_string = function_body(source, "const char *Sys_NetAdrToString", "POSIX address formatting")
    broadcast_string = function_body(
        address_string,
        "else if ( a.type == NA_BROADCAST )",
        "POSIX broadcast address formatting",
    )
    for token in ('"broadcast:%i"', 'idStr::Copynz( buffer, "broadcast"'):
        require(broadcast_string, token, "POSIX broadcast address formatting")
    bot_string = function_body(address_string, "else if ( a.type == NA_BOT )", "POSIX bot address formatting")
    require(bot_string, 'idStr::Copynz( buffer, "bot"', "POSIX bot address formatting")

    address_body = function_body(source, "bool Sys_StringToNetAdr", "POSIX address conversion")
    require(address_body, "if ( a == NULL )", "POSIX address output guard")
    require(address_body, "memset( a, 0, sizeof( *a ) );", "POSIX address output initialization")
    require(address_body, "a->type = NA_BAD;", "POSIX failed-address sentinel")

    packet_body = function_body(source, "static bool Net_GetPacketFromSocket", "POSIX UDP receive")
    for token in (
        "size = 0;",
        "memset( &net_from, 0, sizeof( net_from ) );",
        "net_from.type = NA_BAD;",
    ):
        require(packet_body, token, "POSIX internal packet output initialization")
    invalid_receive = function_body(
        packet_body,
        "if ( socketFd <= 0 || data == NULL || maxSize <= 0 )",
        "POSIX invalid packet receive arguments",
    )
    require(invalid_receive, "return false;", "POSIX invalid packet receive result")
    for token in (
        "size = 0;",
        "net_from.type = NA_BAD;",
        "if ( socketFd <= 0 || data == NULL || maxSize <= 0 )",
    ):
        require_before(packet_body, token, "static_cast<size_t>( maxSize )", "POSIX receive capacity validation")
    require(packet_body, "recvmsg( socketFd", "POSIX datagram receive")
    receive_error = function_body(packet_body, "if ( ret == -1 )", "POSIX UDP receive error")
    require(
        receive_error,
        "errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED",
        "POSIX nonblocking UDP receive errors",
    )
    require(receive_error, '"%s recvmsg(): %s\\n"', "POSIX recvmsg diagnostic")
    reject(receive_error, "recvfrom()", "POSIX stale receive diagnostic")
    truncation = function_body(
        packet_body,
        "if ( ( message.msg_flags & MSG_TRUNC ) != 0 || ret > maxSize )",
        "POSIX truncated-datagram rejection",
    )
    require(truncation, "return false;", "POSIX truncated-datagram result")
    require(truncation, "memset( &net_from, 0, sizeof( net_from ) );", "POSIX truncated source reset")
    require(truncation, "net_from.type = NA_BAD;", "POSIX truncated source sentinel")
    if not (
        truncation.index("memset( &net_from")
        < truncation.index("net_from.type = NA_BAD;")
        < truncation.index("return false;")
    ):
        raise AssertionError("POSIX truncated datagrams must clear their source before returning")
    require_before(
        packet_body,
        "if ( ( message.msg_flags & MSG_TRUNC ) != 0 || ret > maxSize )",
        "size = static_cast<int>( ret );",
        "POSIX truncated-datagram rejection",
    )
    reject(packet_body, "ret == maxSize", "POSIX exact-capacity datagram handling")

    for signature, context in (
        ("bool idPort::GetPacket(", "POSIX nonblocking packet output"),
        ("bool idPort::GetPacketBlocking(", "POSIX blocking packet output"),
    ):
        public_receive = function_body(source, signature, context)
        require(public_receive, "size = 0;", context)
        require(public_receive, "memset( &net_from, 0, sizeof( net_from ) );", context)
        require(public_receive, "net_from.type = NA_BAD;", context)
        if public_receive.index("size = 0;") > public_receive.index("if ( !netSocket"):
            raise AssertionError(f"{context} must initialize size before early returns")
        if public_receive.index("net_from.type = NA_BAD;") > public_receive.index("if ( !netSocket"):
            raise AssertionError(f"{context} must initialize the source address before early returns")

    blocking_receive = function_body(
        source,
        "bool idPort::GetPacketBlocking(",
        "POSIX blocking UDP receive",
    )
    negative_timeout = function_body(
        blocking_receive,
        "if ( timeout < 0 )",
        "POSIX nonblocking UDP poll",
    )
    require(
        negative_timeout,
        "return GetPacket( net_from, data, size, maxSize );",
        "POSIX nonblocking UDP poll result",
    )
    require_before(
        blocking_receive,
        "if ( timeout < 0 )",
        "FD_ZERO( &set );",
        "POSIX nonblocking UDP poll",
    )
    for token in (
        "size = 0;",
        "memset( &net_from, 0, sizeof( net_from ) );",
        "net_from.type = NA_BAD;",
    ):
        require_before(
            blocking_receive,
            token,
            "if ( timeout < 0 )",
            "POSIX nonblocking UDP output initialization",
        )

    send_packet = function_body(source, "void idPort::SendPacket", "POSIX UDP send")
    send_buffer_guard = function_body(
        send_packet,
        "if ( size < 0 || ( data == NULL && size > 0 ) )",
        "POSIX UDP send buffer validation",
    )
    require(send_buffer_guard, "return;", "POSIX invalid UDP send buffer result")
    for token in (
        "const char emptyPacket = '\\0';",
        "const void *packetData = data != NULL ? data : &emptyPacket;",
        "sendto( socketFd, packetData, size, 0",
    ):
        require(send_packet, token, "POSIX zero-length UDP send")
    if not (
        send_packet.index("if ( size < 0 || ( data == NULL && size > 0 ) )")
        < send_packet.index("const char emptyPacket = '\\0';")
        < send_packet.index("sendto( socketFd, packetData, size, 0")
    ):
        raise AssertionError("POSIX zero-length UDP sends must validate then substitute a non-null buffer")
    reject(send_packet, "sendto( socketFd, data, size", "POSIX nullable zero-length UDP buffer")

    tcp = function_body(source, "bool idTCP::Init( const char *host, int port )", "POSIX TCP initialization")
    require(tcp, "Sys_ResolveSockaddr( host, true, AF_UNSPEC, SOCK_STREAM, port", "POSIX TCP resolver")
    reject(tcp, "if ( port < 0 )", "POSIX pre-parse TCP default-port rejection")


def validate_async_ipv4_protocol() -> None:
    client = read("src/framework/async/AsyncClient.cpp")
    common = read("src/framework/Common.cpp")
    network = read("src/framework/async/AsyncNetwork.cpp")
    scan = read("src/framework/async/ServerScan.cpp")
    server = read("src/framework/async/AsyncServer.cpp")

    server_port = function_body(server, "bool idAsyncServer::InitPort", "server UDP port setup")
    require(
        server_port,
        'const int configuredPort = cvarSystem->GetCVarInteger( "net_port" );',
        "configured server port validation",
    )
    configured_port_guard = function_body(
        server_port,
        "if ( configuredPort < 0 || configuredPort > 65535 )",
        "configured server port validation",
    )
    require(configured_port_guard, "return false;", "configured server port validation")
    require(configured_port_guard, "expected 0 (automatic) or 1..65535", "configured server port range")
    require_before(
        server_port,
        "if ( configuredPort < 0 || configuredPort > 65535 )",
        "serverPort.InitForPort( configuredPort )",
        "configured server port validation",
    )
    reject(server_port, "serverPort.InitForPort( PORT_ANY )", "configured server port setup")

    server_list = function_body(
        client,
        "void idAsyncClient::ProcessServersListMessage",
        "master-server IPv4 list parser",
    )
    require(server_list, "msg.GetRemaingData() >= 6", "six-byte master-server entry framing")
    if server_list.count("msg.ReadByte()") != 4:
        raise AssertionError("Master-server entries must read exactly four IPv4 octets")
    require(server_list, "msg.ReadUShort()", "unsigned master-server port decoding")
    reject(server_list, "msg.ReadShort()", "high master-server port decoding")
    require(server_list, "msg.GetRemaingData() != 0", "trailing master-server entry rejection")

    rcon = function_body(client, "void idAsyncClient::RemoteConsole", "remote console send")
    require(rcon, "if ( !Sys_StringToNetAdr(", "remote console resolution guard")
    require(rcon, "lastRconAddress = adr;", "remote console reply address")
    require(rcon, "clientPort.SendPacket( adr", "remote console send")
    rcon_failure = function_body(rcon, "if ( !Sys_StringToNetAdr(", "remote console resolution failure")
    require(rcon_failure, "return;", "remote console unresolved-address result")
    if not rcon.index("if ( !Sys_StringToNetAdr(") < rcon.index("lastRconAddress = adr;") < rcon.index(
        "clientPort.SendPacket( adr"
    ):
        raise AssertionError("Remote console must validate and remember its address before sending")

    info_response = function_body(scan, "int idServerScan::InfoResponse", "server scan duplicate detection")
    require(info_response, "Sys_CompareNetAdrBase", "semantic server-address comparison")
    require(info_response, ".adr.port == server.adr.port", "server-address port comparison")
    reject(info_response, "memcmp(", "server-address semantic comparison")

    for signature, context in (
        ("void idAsyncNetwork::Connect_f", "direct IPv4 connect setup"),
        ("void idAsyncNetwork::Reconnect_f", "direct IPv4 reconnect setup"),
    ):
        connect = function_body(network, signature, context)
        require(connect, 'GetCVarString( "si_gameType" )', context)
        require(connect, '"singleplayer"', context)
        require(connect, 'SetCVarString( "si_gameType", "DM" )', context)
        if connect.index('SetCVarString( "si_gameType", "DM" )') > connect.index(
            'GetCVarString( "com_activeGameModule" )'
        ):
            raise AssertionError(f"{context} must normalize multiplayer state before module routing")

    module_load = function_body(common, "void idCommonLocal::LoadGameDLL", "multiplayer module load")
    require(
        module_load,
        "openQ4_NormalizeGameTypeForModule( gameModuleBaseName );",
        "pre-load direct-connect game type normalization",
    )
    if module_load.index("openQ4_NormalizeGameTypeForModule") > module_load.index("sys->DLL_Load"):
        raise AssertionError("Direct-connect game type must be normalized before loading game_mp")

    game_init = function_body(common, "void idCommonLocal::InitGame", "game initialization")
    for token in (
        "const idStr pendingGameModule = openQ4_SelectGameModuleBaseName();",
        "openQ4_NormalizeGameTypeForModule( pendingGameModule.c_str() );",
    ):
        require(game_init, token, "pre-declaration direct-connect game type normalization")
    if not (
        game_init.index("const idStr pendingGameModule")
        < game_init.index("openQ4_NormalizeGameTypeForModule( pendingGameModule.c_str() );")
        < game_init.index("declManager->Init")
    ):
        raise AssertionError("Direct-connect game type must be normalized before declaration initialization")

    reject(common, "openQ4_StartupRequiresMultiplayerModule", "startup command preselection")
    common_init = function_body(common, "void idCommonLocal::Init( int argc", "common startup")
    for token in (
        'SetCVarString( "com_nextGameModule", "game_mp" )',
        'openQ4_NormalizeGameTypeForModule( "game_mp" );',
    ):
        reject(common_init, token, "startup command preselection")

    normalize = function_body(
        common,
        "static void openQ4_NormalizeGameTypeForModule",
        "cross-build multiplayer game type normalization",
    )
    canonicalize = function_body(
        common,
        "static const char *openQ4_CanonicalMultiplayerGameType",
        "canonical multiplayer game type lookup",
    )
    require(
        canonicalize,
        "return route.name;",
        "canonical multiplayer game type table value",
    )
    for token in (
        "const bool selectableOnly",
        "( !selectableOnly || route.selectable )",
    ):
        require(canonicalize, token, "reserved multiplayer game type routing")
    route_probe = function_body(
        common,
        "static bool openQ4_IsMultiplayerGameType",
        "multiplayer game module routing",
    )
    require(
        route_probe,
        "openQ4_CanonicalMultiplayerGameType( gameType, false )",
        "reserved multiplayer game module routing",
    )
    require(
        normalize,
        "openQ4_CanonicalMultiplayerGameType( currentGameType, true )",
        "reserved multiplayer game type normalization",
    )
    case_compare = normalize.index("idStr::Cmp( currentGameType, canonicalGameType )")
    trampoline = normalize.index('SetCVarString( "si_gameType", "singleplayer" )')
    canonical_write = normalize.index('SetCVarString( "si_gameType", canonicalGameType )')
    if not case_compare < trampoline < canonical_write:
        raise AssertionError("Case-only multiplayer modes must cross a distinct value before canonicalization")
    for token in (
        "unsupported si_gameType '%s'",
        "using DM for multiplayer module initialization",
        "currentGameType",
    ):
        require(normalize, token, "unsupported multiplayer game type fallback")
    reject(normalize, "until serverInfo", "generic multiplayer game type fallback")

    for token, context in (
        ("static const char *openQ4_CanonicalMultiplayerGameType", "canonical gametype helper"),
        ("static void openQ4_NormalizeGameTypeForModule", "gametype normalization helper"),
        (
            "openQ4_NormalizeGameTypeForModule( pendingGameModule.c_str() );",
            "pre-declaration normalization call",
        ),
        ("openQ4_NormalizeGameTypeForModule( gameModuleBaseName );", "pre-DLL normalization call"),
    ):
        position = common.index(token)
        if any("ID_DEDICATED" in directive for directive in preprocessor_stack_at(common, position)):
            raise AssertionError(f"{context} must compile for dedicated and client builds")


def validate_legacy_ipv4_wire_format() -> None:
    source = read("src/idlib/BitMsg.cpp")
    write_body = function_body(source, "void idBitMsg::WriteNetadr", "legacy IPv4 address writer")
    read_body = function_body(source, "void idBitMsg::ReadNetadr", "legacy IPv4 address reader")

    for token in (
        "GetByteSpace( 4 )",
        "adr.type == NA_IP || adr.type == NA_LOOPBACK",
        "memcpy( dataPtr, adr.ip, 4 )",
        "WriteUShort( adr.port )",
    ):
        require(write_body, token, "legacy four-octet IPv4 wire writer")
    for token in (
        "memset( adr, 0, sizeof( *adr ) );",
        "adr->type = NA_IP;",
        "i < 4",
        "adr->ip[ i ] = ReadByte();",
        "adr->port = ReadUShort();",
    ):
        require(read_body, token, "legacy four-octet IPv4 wire reader")
    for body, context in (
        (write_body, "legacy IPv4 address writer"),
        (read_body, "legacy IPv4 address reader"),
    ):
        reject(body, "NA_IP6", context)
        reject(body, "ip6", context)

    if write_body.count("GetByteSpace(") != 1 or write_body.count("WriteUShort(") != 1:
        raise AssertionError("Legacy IPv4 writer must emit exactly four address bytes and one unsigned port")
    for token in ("WriteByte(", "WriteShort(", "WriteLong(", "WriteBits(", "WriteData("):
        reject(write_body, token, "legacy six-byte IPv4 wire writer")
    if read_body.count("ReadByte()") != 1 or read_body.count("ReadUShort()") != 1:
        raise AssertionError("Legacy IPv4 reader must consume four looped address bytes and one unsigned port")
    for token in ("ReadShort(", "ReadLong(", "ReadBits(", "ReadData("):
        reject(read_body, token, "legacy six-byte IPv4 wire reader")


def validate_runtime_dns_ipv4_contract(hostname_resolution: str) -> None:
    for token in (
        "address.type != NA_LOOPBACK",
        "address.type != NA_IP",
        "address.port != 65535",
        'Common_NetIPv4SelfTestFailure( passed, "DNS-enabled localhost did not resolve to IPv4" );',
    ):
        require(hostname_resolution, token, "runtime DNS-enabled hostname resolution")
    require(
        hostname_resolution,
        "( address.type != NA_LOOPBACK && address.type != NA_IP )",
        "runtime DNS-enabled IPv4-family requirement",
    )
    reject(
        hostname_resolution,
        "NA_IP6",
        "runtime DNS-enabled IPv6-only rejection",
    )


def validate_runtime_ipv4_selftest() -> None:
    common = read("src/framework/Common.cpp")
    body = function_body(common, "static void Com_NetIPv4SelfTest_f", "runtime IPv4 self-test")

    numeric_endpoint = function_body(
        body,
        'if ( !Sys_StringToNetAdr( "127.0.0.1:65535", &address, false )',
        "runtime numeric IPv4 endpoint probe",
    )
    for token in (
        "address.ip[0] != 127",
        "address.ip[1] != 0",
        "address.ip[2] != 0",
        "address.ip[3] != 1",
        "address.port != 65535",
        'Common_NetIPv4SelfTestFailure( passed, "numeric IPv4 endpoint did not round-trip" );',
    ):
        require(numeric_endpoint, token, "runtime numeric IPv4 endpoint probe")

    broadcast_endpoint = function_body(
        body,
        'if ( !Sys_StringToNetAdr( "255.255.255.255:65535", &address, false )',
        "runtime IPv4 broadcast probe",
    )
    for token in (
        "address.type != NA_IP",
        "address.ip[0] != 255",
        "address.ip[1] != 255",
        "address.ip[2] != 255",
        "address.ip[3] != 255",
        "address.port != 65535",
        'Common_NetIPv4SelfTestFailure( passed, "IPv4 broadcast literal did not parse" );',
    ):
        require(broadcast_endpoint, token, "runtime IPv4 broadcast probe")

    hostname_rejection = function_body(
        body,
        'if ( Sys_StringToNetAdr( "localhost", &address, false ) || address.type != NA_BAD )',
        "runtime numeric-only hostname rejection",
    )

    hostname_resolution = function_body(
        body,
        'if ( !Sys_StringToNetAdr( "localhost:65535", &address, true )',
        "runtime DNS-enabled hostname resolution",
    )
    validate_runtime_dns_ipv4_contract(hostname_resolution)

    require(
        hostname_rejection,
        'Common_NetIPv4SelfTestFailure( passed, "numeric-only resolution accepted a hostname" );',
        "runtime numeric-only hostname rejection",
    )

    port_rejection = function_body(
        body,
        'if ( Sys_StringToNetAdr( "127.0.0.1:65536", &address, false ) || address.type != NA_BAD )',
        "runtime out-of-range endpoint rejection",
    )
    require(
        port_rejection,
        'Common_NetIPv4SelfTestFailure( passed, "out-of-range port was accepted" );',
        "runtime out-of-range endpoint rejection",
    )

    require(
        body,
        '{ "net_ip", "net_forceLatency", "net_forceDrop" }',
        "runtime hostile network-cvar isolation",
    )
    require(body, '{ "127.0.0.1", "0", "0" }', "runtime deterministic network-cvar values")
    require_before(
        body,
        "const int savedModifiedFlags = cvarSystem->GetModifiedFlags();",
        "cvarSystem->SetCVarString( testCVarNames[i], testCVarValues[i] );",
        "runtime network-cvar preservation",
    )

    cvar_override_loop = function_body(
        body,
        "for ( int i = 0; i < static_cast<int>( sizeof( testCVarNames ) / sizeof( testCVarNames[0] ) ); ++i )",
        "runtime network-cvar override",
    )
    for token in (
        "savedCVars[i] = true;",
        "savedCVarValues[i] = cvar->GetString();",
        "savedCVarFlags[i] = cvar->GetFlags();",
        "cvarSystem->SetCVarString( testCVarNames[i], testCVarValues[i] );",
    ):
        require(cvar_override_loop, token, "runtime network-cvar override")

    invalid_low_port = function_body(
        body,
        "if ( invalidPort.InitForPort( -2 ) || invalidPort.GetPort() != 0 )",
        "runtime invalid negative UDP port",
    )
    require(
        invalid_low_port,
        'Common_NetIPv4SelfTestFailure( passed, "idPort accepted port -2" );',
        "runtime invalid negative UDP port",
    )
    invalid_high_port = function_body(
        body,
        "if ( invalidPort.InitForPort( 65536 ) || invalidPort.GetPort() != 0 )",
        "runtime out-of-range UDP port",
    )
    require(
        invalid_high_port,
        'Common_NetIPv4SelfTestFailure( passed, "idPort accepted port 65536" );',
        "runtime out-of-range UDP port",
    )
    high_port_scan = function_body(
        body,
        "for ( int candidatePort = 65535; candidatePort >= 65472; --candidatePort )",
        "runtime high UDP port bind scan",
    )
    require(high_port_scan, "receiver.InitForPort( candidatePort )", "runtime high UDP port bind scan")
    require(high_port_scan, "receiverPort = candidatePort;", "runtime high UDP port bind scan")
    require(high_port_scan, "break;", "runtime high UDP port bind scan")

    socket_init = function_body(
        body,
        "if ( receiverPort == 0 || receiver.GetPort() != receiverPort || !sender.InitForPort( PORT_ANY ) )",
        "runtime loopback socket initialization",
    )
    require(
        socket_init,
        'Common_NetIPv4SelfTestFailure( passed, "could not open IPv4 loopback sockets" );',
        "runtime loopback socket initialization",
    )
    require_before(
        body,
        "cvarSystem->SetCVarString( testCVarNames[i], testCVarValues[i] );",
        "receiver.InitForPort( candidatePort )",
        "runtime isolated socket initialization",
    )

    counter_check = function_body(
        body,
        "if ( sender.packetsWritten != 0 || sender.bytesWritten != 0 || receiver.packetsRead != 0 || receiver.bytesRead != 0 )",
        "runtime socket counter initialization",
    )
    require(
        counter_check,
        'Common_NetIPv4SelfTestFailure( passed, "socket counters were not initialized" );',
        "runtime socket counter initialization",
    )

    destination_check = function_body(
        body,
        'if ( !Sys_StringToNetAdr( va( "127.0.0.1:%d", receiverPort ), &destination, false ) )',
        "runtime loopback destination",
    )
    require(
        destination_check,
        'Common_NetIPv4SelfTestFailure( passed, "could not create the loopback destination" );',
        "runtime loopback destination",
    )

    payload_receive = function_body(
        body,
        "if ( !receiver.GetPacketBlocking( source, received, receivedSize, sizeof( received ), 1000 )",
        "runtime UDP payload receive",
    )
    for token in (
        "receivedSize != sizeof( payload )",
        "memcmp( received, payload, sizeof( payload ) ) != 0",
        "!Sys_CompareNetAdrBase( source, destination )",
        'Common_NetIPv4SelfTestFailure( passed, "UDP loopback payload did not round-trip" );',
    ):
        require(payload_receive, token, "runtime UDP payload receive")
    require_before(
        body,
        "sender.SendPacket( destination, payload",
        "receiver.GetPacketBlocking( source, received",
        "runtime UDP payload round-trip",
    )

    reply_receive = function_body(
        body,
        "if ( !sender.GetPacketBlocking( replySource, received, receivedSize, sizeof( received ), 1000 )",
        "runtime UDP reply receive",
    )
    for token in (
        "receivedSize != sizeof( reply )",
        "memcmp( received, reply, sizeof( reply ) ) != 0",
        "!Sys_CompareNetAdrBase( replySource, destination )",
        "replySource.port != destination.port",
        'Common_NetIPv4SelfTestFailure( passed, "UDP loopback reply did not round-trip" );',
    ):
        require(reply_receive, token, "runtime UDP reply receive")
    require_before(
        body,
        "receiver.SendPacket( source, reply",
        "sender.GetPacketBlocking( replySource, received",
        "runtime UDP reply round-trip",
    )

    exact_receive = function_body(
        body,
        "if ( !receiver.GetPacketBlocking( source, exactReceived, receivedSize, sizeof( exactReceived ), 1000 )",
        "runtime exact-capacity datagram receive",
    )
    for token in (
        "receivedSize != sizeof( exactPacket )",
        "memcmp( exactReceived, exactPacket, sizeof( exactPacket ) ) != 0",
        'Common_NetIPv4SelfTestFailure( passed, "exact-capacity UDP datagram was rejected" );',
    ):
        require(exact_receive, token, "runtime exact-capacity datagram receive")
    require_before(
        body,
        "sender.SendPacket( destination, exactPacket",
        "receiver.GetPacketBlocking( source, exactReceived",
        "runtime exact-capacity datagram",
    )

    oversize_receive = function_body(
        body,
        "if ( receiver.GetPacketBlocking( source, exactReceived, receivedSize, sizeof( exactReceived ), 1000 ) || receivedSize != 0 )",
        "runtime oversized datagram rejection",
    )
    require(
        oversize_receive,
        'Common_NetIPv4SelfTestFailure( passed, "oversized UDP datagram was not rejected" );',
        "runtime oversized datagram rejection",
    )

    # The first match is the exact-capacity branch. Find and scope the later
    # marker branch from its unique size check instead.
    marker_start = body.index("receivedSize != sizeof( markerPacket )")
    marker_condition = body.rfind("if (", 0, marker_start)
    marker_receive = function_body(body[marker_condition:], "if (", "runtime post-oversize marker receive")
    for token in (
        "receivedSize != sizeof( markerPacket )",
        "memcmp( exactReceived, markerPacket, sizeof( markerPacket ) ) != 0",
        'Common_NetIPv4SelfTestFailure( passed, "packet after oversized UDP datagram was not preserved" );',
    ):
        require(marker_receive, token, "runtime post-oversize marker receive")

    oversize_send = body.index("sender.SendPacket( destination, oversizePacket")
    marker_send = body.index("sender.SendPacket( destination, markerPacket")
    oversize_read = body.index("if ( receiver.GetPacketBlocking( source, exactReceived", marker_send)
    marker_read = marker_condition
    if not oversize_send < marker_send < oversize_read < marker_read:
        raise AssertionError("Runtime IPv4 self-test must reject oversize data before accepting its marker")

    lifecycle = function_body(
        body,
        "if ( receiver.GetPort() != 0 || !receiver.InitForPort( PORT_ANY ) || receiver.GetPort() == 0 || previousPort == 0 )",
        "runtime UDP socket lifecycle",
    )
    require(
        lifecycle,
        'Common_NetIPv4SelfTestFailure( passed, "socket close/reopen lifecycle failed" );',
        "runtime UDP socket lifecycle",
    )
    close_position = body.index("receiver.Close();")
    reopen_position = body.index("!receiver.InitForPort( PORT_ANY )", close_position)
    if close_position >= reopen_position:
        raise AssertionError("Runtime UDP socket must close before reopening")

    restore_value = body.index("savedCVarValues[i].c_str()")
    restore_flags = body.index("savedCVarFlags[i]", restore_value)
    restore_modified = body.index("Common_RestorePerformancePresetModifiedFlags( savedModifiedFlags )")
    pass_marker = body.index('"IPv4 network self-test: passed\\n"')
    if not restore_value < restore_flags < restore_modified < pass_marker:
        raise AssertionError("Runtime IPv4 self-test must restore network cvars before reporting its result")
    if body.count('"IPv4 network self-test: passed\\n"') != 1 or body.count(
        '"IPv4 network self-test: FAILED\\n"'
    ) != 1:
        raise AssertionError("Runtime IPv4 self-test must expose one stable pass and failure marker")

    require(
        common,
        'cmdSystem->AddCommand( "netIPv4SelfTest", Com_NetIPv4SelfTest_f, CMD_FL_SYSTEM',
        "runtime IPv4 self-test command registration",
    )


def validate_postinit_connect_runtime_lane() -> None:
    source = read("tools/tests/renderer_gameplay_benchmark.py")

    for token in (
        "POSTINIT_CONNECT_WAIT_FRAMES = 30",
        "POSTINIT_RECONNECT_WAIT_FRAMES = 30",
        "MP_SERVER_CLIENT_GRACE_MSEC = 90000",
    ):
        require(source, token, "post-init IPv4 benchmark delay")
    scene_start = source.index('"mp-q4dm1-postinit-connect": {')
    scene_end = source.index("\n    },", scene_start)
    scene = source[scene_start:scene_end]
    for token in (
        '"mode": "MP"',
        '"map": "mp/q4dm1"',
        '"path": "postinit-connect"',
    ):
        require(scene, token, "post-init IPv4 benchmark case")
    require(
        source,
        '"mp-postinit-connect": {\n        "cases": ("mp-q4dm1-postinit-connect",)',
        "post-init IPv4 benchmark profile",
    )

    writer = python_function_body(
        source,
        "write_postinit_connect_cfg",
        "post-init IPv4 connect configuration",
    )
    require(
        writer,
        'payload = f"wait {POSTINIT_CONNECT_WAIT_FRAMES}\\nconnect 127.0.0.1:{port}\\n"',
        "post-init IPv4 connect commands",
    )
    require(writer, 'for game_dir in ("baseoq4", "q4base"):', "post-init IPv4 game directories")
    require(writer, "cfg_path.write_text(payload", "post-init IPv4 connect configuration")

    reconnect_writer = python_function_body(
        source,
        "write_postinit_reconnect_cfg",
        "post-init IPv4 reconnect configuration",
    )
    for token in (
        'f"wait {POSTINIT_RECONNECT_WAIT_FRAMES}\\n"',
        'f\'set g_autoExecAfterMapLoad "{capture_cfg}"\\n\'',
        'f"set g_autoExecAfterMapLoadDelayMs {max(0, autoexec_delay_ms)}\\n"',
        '"reconnect\\n"',
        'for game_dir in ("baseoq4", "q4base"):',
    ):
        require(reconnect_writer, token, "post-init IPv4 reconnect configuration")

    runner = python_function_body(source, "run_mp_spec", "MP gameplay benchmark runner")
    condition = 'if spec.path_name == "postinit-connect":'
    condition_start = runner.index(condition)
    branch_start = condition_start + len(condition)
    else_marker = "\n    else:"
    else_start = runner.index(else_marker, branch_start)
    delayed_branch = runner[branch_start:else_start]
    for token in (
        'append_set(client_args, "si_gameType", "singleplayer")',
        "write_postinit_connect_cfg(client_savepath, port)",
        'append_command(client_args, "exec", client_postinit_connect_cfg)',
    ):
        require(delayed_branch, token, "post-init IPv4 delayed-connect branch")
    reject(
        delayed_branch,
        'append_command(client_args, "connect"',
        "post-init IPv4 delayed-connect branch",
    )
    require_before(
        delayed_branch,
        "write_postinit_connect_cfg(client_savepath, port)",
        'append_command(client_args, "exec", client_postinit_connect_cfg)',
        "post-init IPv4 delayed-connect branch",
    )

    normal_end = runner.index("\n\n    if args.dry_run:", else_start)
    normal_branch = runner[else_start:normal_end]
    require(
        normal_branch,
        'append_command(client_args, "connect", f"127.0.0.1:{port}")',
        "normal MP benchmark connect branch",
    )
    require(
        runner,
        '"clientPostInitConnectCfg": client_postinit_connect_cfg',
        "post-init IPv4 dry-run evidence",
    )
    for token in (
        'f"waitMsec {MP_SERVER_CLIENT_GRACE_MSEC}",',
        'client_capture_index = 1 if spec.path_name == "postinit-connect" else 0',
        "write_postinit_reconnect_cfg(",
        "client_initial_autoexec_cfg = client_reconnect_cfg",
        '"clientInitialAutoexecCfg": client_initial_autoexec_cfg',
        '"clientReconnectCfg": client_reconnect_cfg',
        "server_log_text = read_text(find_log(server_savepath, server_log))",
        "client_log_text = read_text(find_log(client_savepath, client_log))",
        'server_text = server_log_text or "\\n".join(',
        'client_text = client_log_text or "\\n".join(',
        'server_text.count("sending connect response to ")',
        'client_text.count("received connect response from ")',
        'client_text.count("TTF font: console sheet rebuilt at ")',
        "if server_response_count < 2:",
        "if client_response_count < 2:",
        "if client_ttf_rebuild_count < 1:",
        '"postInitConnectResponses": postinit_connect_responses',
        '"postInitTTFRebuilds": postinit_ttf_rebuilds',
    ):
        require(runner, token, "post-init IPv4 connect/reconnect evidence")
    require_before(
        runner,
        'f"waitMsec {MP_SERVER_CLIENT_GRACE_MSEC}",',
        "server_autoexec_cfg, server_screenshot = write_autoexec_cfg(",
        "post-init IPv4 server grace",
    )
    server_writer_call_start = runner.index("server_autoexec_cfg, server_screenshot = write_autoexec_cfg(")
    server_writer_call_end = runner.index("\n    )", server_writer_call_start)
    server_writer_call = runner[server_writer_call_start:server_writer_call_end]
    require(server_writer_call, "server_exec_commands", "post-init IPv4 server grace")
    reject(server_writer_call, "args.exec_commands", "post-init IPv4 server grace")
    client_writer_call_start = runner.index("client_autoexec_cfg, client_screenshot = write_autoexec_cfg(")
    client_writer_call_end = runner.index("\n    )", client_writer_call_start)
    client_writer_call = runner[client_writer_call_start:client_writer_call_end]
    require(client_writer_call, "args.exec_commands", "post-init IPv4 client commands")
    reject(client_writer_call, "server_exec_commands", "post-init IPv4 client commands")
    reject(
        runner,
        "for path in (find_log(server_savepath, server_log), server_stdout, server_stderr)",
        "deduplicated server connect-response evidence",
    )
    reject(
        runner,
        "for path in (find_log(client_savepath, client_log), client_stdout, client_stderr)",
        "deduplicated client connect-response evidence",
    )


def validate_ipv4_delivery_docs() -> None:
    release_notes = read("docs/dev/releases/v0.10.000.md")
    for token in (
        "Hosting and joining IPv4 games is now more dependable",
        "the full\n  port range through `65535` works end to end",
        "normal menu or single-player startup no longer fail with a false\n  data/decl mismatch",
        "`netIPv4SelfTest` for a headless loopback health check",
        "Legacy `net_socks*` settings remain visible\n  for configuration compatibility",
        "do not enable a supported SOCKS\n  UDP relay",
    ):
        require(release_notes, token, "curated IPv4 release note")

    server_guide = read("docs/user/server-setup.md")
    for token in (
        "use direct IPv4 UDP",
        "legacy `net_socks*` CVars remain visible for configuration",
        "does not enable or support SOCKS UDP relay",
        "does not route multiplayer traffic through a SOCKS proxy",
    ):
        require(server_guide, token, "server SOCKS compatibility note")

    building = read("BUILDING.md")
    for token in (
        "strict numeric IPv4 parsing, DNS-enabled\n`localhost`, invalid ports",
        "explicit `127.0.0.1` high port plus an ephemeral sender",
        "bidirectional payloads",
        "rejects\nan oversized datagram without losing the following marker",
        "socket\nclose/reopen",
        "does not prove public\nreachability",
        "external DNS records",
    ):
        require(building, token, "IPv4 self-test build guidance")
    reject(building, "does not prove public reachability, router NAT or firewall configuration, DNS behavior", "stale IPv4 self-test DNS guidance")


def validate_generated_font_decl_checksum_stability() -> None:
    source = read("src/renderer/tr_fontTTF.cpp")

    atlas = function_body(
        source,
        "static const idMaterial *R_TTFCreateAtlasMaterial",
        "generated TTF atlas material",
    )
    require(atlas, "declManager->FindMaterial( materialName, true )", "implicit generated TTF atlas material")
    # CreateNewDecl would give the declaration a real source file, which is what
    # puts it in the network checksum. FindMaterial keeps it implicit, and
    # GetChecksum skips implicit declarations outright, so the runtime text the
    # install below gives it stays process local.
    reject(atlas, "CreateNewDecl(", "generated TTF atlas declaration checksum")

    install = function_body(
        source,
        "static void R_TTFInstallAtlasStage",
        "generated TTF atlas stage install",
    )
    for token in (
        "material->SetText( source.c_str() );",
        "material->FreeData();",
        "material->Parse( source.c_str(), source.Length() );",
    ):
        require(install, token, "implicit generated TTF atlas material")
    reject(install, "CreateNewDecl(", "generated TTF atlas declaration checksum")
    require_before(
        install,
        "material->FreeData();",
        "material->Parse( source.c_str(), source.Length() );",
        "implicit generated TTF atlas material",
    )
    require_before(
        atlas,
        "declManager->FindMaterial( materialName, true )",
        "R_TTFInstallAtlasStage(",
        "implicit generated TTF atlas material cleanup",
    )

    checksum = function_body(
        read("src/framework/DeclManager.cpp"),
        "int idDeclManagerLocal::GetChecksum( void ) const",
        "declaration checksum",
    )
    require(checksum, "if ( decl->sourceFile == &implicitDecls )", "implicit declaration checksum exemption")
    require_before(
        checksum,
        "if ( decl->sourceFile == &implicitDecls )",
        "checksumData[total*2+1] = decl->checksum;",
        "implicit declaration checksum exemption",
    )

    console = function_body(source, "bool R_BuildConsoleFontAtlas", "runtime console font retarget")
    require(
        console,
        "declManager->FindMaterial( Q4_CONSOLE_FONT_MATERIAL, false )",
        "authored console font material lookup",
    )
    require(
        console,
        "material->OverrideStageImageForRuntime( 0, image )",
        "runtime console font image retarget",
    )
    for token in ("CreateNewDecl(", "SetText(", "material->Parse("):
        reject(console, token, "runtime console font declaration checksum")
    require_before(
        console,
        "declManager->FindMaterial( Q4_CONSOLE_FONT_MATERIAL, false )",
        "material->OverrideStageImageForRuntime( 0, image )",
        "runtime console font image retarget",
    )

    material_header = read("src/renderer/Material.h")
    override = function_body(
        material_header,
        "OverrideStageImageForRuntime(const int index",
        "runtime material-stage image override",
    )
    guard = function_body(
        override,
        "if (index < 0 || index >= numStages || image == NULL)",
        "runtime material-stage image validation",
    )
    require(guard, "return false;", "runtime material-stage image validation")
    stage_guard = function_body(
        override,
        "if (stage.newStage != NULL || stage.texture.cinematic != NULL || stage.texture.dynamic != DI_STATIC)",
        "runtime static image-stage validation",
    )
    require(stage_guard, "return false;", "runtime static image-stage validation")
    for token in (
        "shaderStage_t& stage = stages[index];",
        "stage.texture.image = image;",
        "return true;",
    ):
        require(override, token, "runtime material-stage image override")
    for token in (
        "SetText(",
        "Parse(",
        "stage.newStage =",
        "stage.texture.cinematic =",
        "stage.texture.dynamic =",
    ):
        reject(override, token, "runtime material-stage declaration preservation")
    if not (
        override.index("if (index < 0 || index >= numStages || image == NULL)")
        < override.index("shaderStage_t& stage = stages[index];")
        < override.index("if (stage.newStage != NULL")
        < override.index("stage.texture.image = image;")
        < override.index("return true;")
    ):
        raise AssertionError("Runtime material override must validate the stage before retargeting its image")


def validate_critical_mutation_sensitivity() -> None:
    windows = read("src/sys/win32/win_net.cpp")
    lag_allocation = "platformData = new idUDPLag;"
    if windows.count(lag_allocation) != 1:
        raise AssertionError("Windows per-socket latency allocation mutation anchor is not unique")
    expect_contract_rejection(
        validate_windows_per_socket_lag_contract,
        windows.replace(lag_allocation, "udpPorts[ bound_to.port ] = new idUDPLag;", 1),
        "Windows latency state is keyed globally by bound port",
    )

    lag_release = "delete static_cast<idUDPLag *>( platformData );"
    if windows.count(lag_release) != 1:
        raise AssertionError("Windows per-socket latency release mutation anchor is not unique")
    expect_contract_rejection(
        validate_windows_per_socket_lag_contract,
        windows.replace(lag_release, "delete static_cast<idUDPLag *>( NULL );", 1),
        "Windows close does not release the calling idPort's latency state",
    )

    receive_lag_guard = "( netSocket == INVALID_SOCKET && netSocket6 == INVALID_SOCKET ) || lag == NULL || data == NULL"
    if windows.count(receive_lag_guard) != 1:
        raise AssertionError("Windows closed-port receive mutation anchor is not unique")
    expect_contract_rejection(
        validate_windows_per_socket_lag_contract,
        windows.replace(
            receive_lag_guard,
            "( netSocket == INVALID_SOCKET && netSocket6 == INVALID_SOCKET ) || data == NULL",
            1,
        ),
        "Windows delayed receive dereferences missing per-socket state",
    )

    # The dual-stack bind policy is byte-identical on both platforms apart from
    # its invalid-socket sentinel, so the same mutations must be caught twice.
    posix = read("src/sys/posix/posix_net.cpp")
    mutations = (
        (
            "if ( ipv4Resolved ) {",
            "if ( false ) {",
            "resolved IPv4 bind failure falls through to IPv6",
        ),
        (
            "AF_INET, &bound4, true, &ipv4Resolved",
            "AF_INET, &bound4, true, NULL",
            "IPv4 resolution result is discarded",
        ),
        (
            "idNetworkEndpoint::PlanBind( ipv4Text, ipv6Text, net_enableIPv4.GetBool(), net_enableIPv6.GetBool(), plan );",
            "idNetworkEndpoint::PlanBind( ipv4Text, ipv6Text, true, true, plan );",
            "the family enable cvars are ignored",
        ),
    )
    for platform_source, validator, platform in (
        (windows, validate_windows_dual_stack_bind_contract, "Windows"),
        (posix, validate_posix_dual_stack_bind_contract, "POSIX"),
    ):
        bind_body = function_body(platform_source, "static bool Net_BindDualStack", f"{platform} dual-stack bind")
        for original, replacement, context in mutations:
            if bind_body.count(original) != 1:
                raise AssertionError(f"{platform} mutation anchor is not unique for {context}: {original!r}")
            expect_contract_rejection(
                validator,
                bind_body.replace(original, replacement, 1),
                f"{platform}: {context}",
            )

    common = read("src/framework/Common.cpp")
    selftest = function_body(common, "static void Com_NetIPv4SelfTest_f", "runtime IPv4 self-test")
    dns = function_body(
        selftest,
        'if ( !Sys_StringToNetAdr( "localhost:65535", &address, true )',
        "runtime DNS-enabled hostname resolution",
    )
    original_family_guard = "( address.type != NA_LOOPBACK && address.type != NA_IP )"
    for replacement, context in (
        (
            "( address.type != NA_LOOPBACK && address.type != NA_IP && address.type != NA_IP6 )",
            "IPv6-only DNS result is accepted",
        ),
        (
            "( address.type != NA_LOOPBACK || address.type != NA_IP )",
            "valid IPv4 DNS result is rejected by an always-true family guard",
        ),
    ):
        if dns.count(original_family_guard) != 1:
            raise AssertionError("Runtime DNS IPv4-family mutation anchor is not unique")
        expect_contract_rejection(
            validate_runtime_dns_ipv4_contract,
            dns.replace(original_family_guard, replacement, 1),
            context,
        )


def main() -> None:
    validate_shared_endpoint_parser()
    validate_cross_platform_network_cvar_defaults()
    validate_network_contract_wiring()
    validate_idport_platform_data_mirror()
    validate_windows_networking()
    validate_posix_networking()
    validate_async_ipv4_protocol()
    validate_legacy_ipv4_wire_format()
    validate_runtime_ipv4_selftest()
    validate_postinit_connect_runtime_lane()
    validate_ipv4_delivery_docs()
    validate_generated_font_decl_checksum_stability()
    validate_critical_mutation_sensitivity()
    print("network_ipv4_support: ok")


if __name__ == "__main__":
    main()
