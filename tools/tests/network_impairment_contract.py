#!/usr/bin/env python3
"""Guards how both socket layers use the simulated network conditions.

src/sys/NetworkImpairment.h is exercised directly by the native core safety test.
This checks the wiring the native test cannot see: both win_net.cpp and
posix_net.cpp define the same net_force* CVars, open one pair of delay lines per
bound port and release it on close, queue every received datagram and every
unicast send while conditions are set or a line is still draining, flush due
sends while polling for packets, and report through netImpairment, including
the bottleneck (net_forceRate, net_forceQueue), burst loss
(net_forceDropBurst) and the path MTU (net_forceMTU, net_forceMTUDrop), which
counts each datagram's own address family.

Single-reversal controls then mutate the platform sources and require each
check to fail.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

CVARS = ("net_forceLatency", "net_forceDrop", "net_forceJitter", "net_forceDuplicate", "net_forceSeed",
         "net_forceRate", "net_forceQueue", "net_forceDropBurst", "net_forceMTU", "net_forceMTUDrop")


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    require(haystack, first, context)
    require(haystack, second, context)
    if haystack.index(first) > haystack.index(second):
        raise AssertionError(f"{first!r} must come before {second!r} in {context}")


def body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def cvar_definitions(source: str) -> dict[str, str]:
    found = {}
    for name in CVARS:
        match = re.search(rf'^idCVar {name}\( "{name}", [^\n]*\);$', source, re.M)
        if match is None:
            raise AssertionError(f"Missing CVar {name}")
        found[name] = match.group(0)
    return found


def validate_platform(source: str, platform: str) -> None:
    cvar_definitions(source)
    require(source, '#include "../NetworkImpairment.h"', f"{platform} delay-line header")
    require(source, "typedef idNetworkImpairment::port_t<netadr_t> netImpairmentPort_t;", f"{platform} delay-line type")
    settings = body(source, "static idNetworkImpairment::settings_t Net_ImpairmentSettings(", f"{platform} settings")
    for name in ("net_forceLatency", "net_forceJitter", "net_forceDrop", "net_forceDuplicate", "net_forceRate",
                 "net_forceQueue", "net_forceDropBurst"):
        require(settings, f"{name}.GetInteger()", f"{platform} settings")
    require(settings, "net_forceMTU.GetInteger(),", f"{platform} settings")
    require(settings, "net_forceMTUDrop.GetBool() ? 1 : 0", f"{platform} settings")
    family = body(source, "static bool Net_ImpairmentIPv6(", f"{platform} datagram family")
    require(family, "adr.type == NA_IP6 || adr.type == NA_MULTICAST6", f"{platform} datagram family")
    require(settings, "idNetworkImpairment::Sanitize(", f"{platform} settings clamp")
    opening = body(source, "static netImpairmentPort_t *Net_OpenImpairment(", f"{platform} delay-line open")
    require(opening, "net_forceSeed.GetInteger()", f"{platform} seed")
    require(opening, "impairment->Seed( seed );", f"{platform} seed")

    init = body(source, "bool idPort::InitForPort", f"{platform} port initialization")
    require_before(init, "Net_BindDualStack(", "platformData = Net_OpenImpairment( bound_to.port );",
                   f"{platform} delay lines after a successful bind")
    close = body(source, "void idPort::Close", f"{platform} port close")
    require_before(close, "Net_CloseImpairment( static_cast<netImpairmentPort_t *>( platformData ) );",
                   "platformData = NULL;", f"{platform} delay-line release")

    receive = body(source, "bool idPort::GetPacket(", f"{platform} receive")
    require(receive, "impairment->SendsQueued( settings )", f"{platform} flushes due sends while polling")
    require(receive, "impairment->FlushSends( now,", f"{platform} flushes due sends while polling")
    require(receive, "impairment->ReceivesQueued( settings )", f"{platform} receive queueing decision")
    require(receive, "impairment->receive.Offer( settings, now,", f"{platform} received datagrams wait in line")
    require(receive, "data, size, Net_ImpairmentIPv6(", f"{platform} received datagrams carry their family")
    require(receive, "impairment->receive.Pop( now,", f"{platform} received datagrams leave when due")

    send = body(source, "void idPort::SendPacket", f"{platform} send")
    require(send, "impairment->SendsQueued( settings )", f"{platform} send queueing decision")
    require_before(send, "impairment->send.Offer( settings, now, to,", "impairment->FlushSends( now,",
                   f"{platform} sends wait in line")
    require(send, "size, Net_ImpairmentIPv6( to ) );", f"{platform} sends carry their family")

    report = body(source, "void Sys_NetImpairmentReport(", f"{platform} report")
    require(report, "NET_IMPAIR_SETTINGS", f"{platform} report")
    for field in ("settings.rateBytes", "settings.queueMsec", "settings.burstLength", "settings.mtuBytes",
                  "settings.mtuDrop"):
        require(report, field, f"{platform} reports the bottleneck and burst settings")
    line = body(source, "static void Net_PrintImpairmentLine(", f"{platform} report line")
    for field in ("stats.offeredBytes", "stats.bursts", "stats.congested", "stats.maxWaitMsec", "stats.oversized",
                  "stats.ipFragments", "stats.maxSize"):
        require(line, field, f"{platform} reports bursts, congestion and the longest wait")
    # Harnesses parse the line by its leading fields, so the new ones come last.
    require(line, '"NET_IMPAIR port=%d dir=%s offered=%llu dropped=%llu duplicated=%llu released=%llu reordered=%llu "',
            f"{platform} keeps the report's leading fields")
    require(report, '"receive"', f"{platform} report")
    require(report, '"send"', f"{platform} report")


def validate(windows: str, posix: str) -> None:
    validate_platform(windows, "Windows")
    validate_platform(posix, "POSIX")
    if cvar_definitions(windows) != cvar_definitions(posix):
        raise AssertionError("Windows and POSIX define the net_force* CVars differently")
    # The inherited Windows lag queue held at most 1400 bytes and only flushed on the next send.
    for stale in ("idUDPLag", "MAX_UDP_MSG_SIZE", "udpMsgAllocator"):
        if stale in windows or stale in posix:
            raise AssertionError(f"Stale lag queue remains: {stale}")


def validate_wiring() -> None:
    network = read("src/framework/async/AsyncNetwork.cpp")
    command = body(network, "static void AsyncNetwork_NetImpairment_f(", "netImpairment command")
    require(command, "Sys_NetImpairmentReport();", "netImpairment command")
    require(network, 'cmdSystem->AddCommand( "netImpairment", AsyncNetwork_NetImpairment_f,', "netImpairment command")
    header = read("src/sys/sys_public.h")
    require(header, "void\t\t\tSys_NetImpairmentReport( void );", "Sys_NetImpairmentReport declaration")
    native = read("tools/tests/native/CoreSafetyTest.cpp")
    require(native, '#include "tools/tests/native/NetworkImpairmentCases.h"', "native delay-line test")
    require(native, "failures += ExerciseNetworkImpairment();", "native delay-line test")
    cases = read("tools/tests/native/NetworkImpairmentCases.h")
    require(cases, '#include "src/sys/NetworkImpairment.h"', "native delay-line cases")


def expect_failure(windows: str, posix: str, label: str) -> None:
    try:
        validate(windows, posix)
    except AssertionError:
        return
    raise AssertionError(f"control did not fail: {label}")


def main() -> int:
    windows = read("src/sys/win32/win_net.cpp")
    posix = read("src/sys/posix/posix_net.cpp")
    try:
        validate(windows, posix)
        validate_wiring()
        controls = (
            ("POSIX reads straight past the receive line", "posix",
             "impairment->receive.Offer( settings, now, net_from, data, size, Net_ImpairmentIPv6( net_from ) )", "false"),
            ("Windows sends skip the send line", "windows",
             "impairment->send.Offer( settings, now, to, data, size, Net_ImpairmentIPv6( to ) );", ""),
            ("Windows sends count every datagram as IPv4", "windows",
             "impairment->send.Offer( settings, now, to, data, size, Net_ImpairmentIPv6( to ) );",
             "impairment->send.Offer( settings, now, to, data, size );"),
            ("POSIX ignores the path MTU", "posix",
             "net_forceDropBurst.GetInteger(), net_forceMTU.GetInteger(),", "net_forceDropBurst.GetInteger(), 0,"),
            ("Windows drops the oversized count from its report", "windows",
             "static_cast<unsigned long long>( stats.oversized ),", "0ull,"),
            ("Windows no longer flushes due sends while polling", "windows",
             "impairment->FlushSends( now, [ipv4Socket, ipv6Socket]( const netadr_t &to,", "impairment->Unused( now, [ipv4Socket, ipv6Socket]( const netadr_t &to,"),
            ("POSIX close leaks the delay lines", "posix",
             "Net_CloseImpairment( static_cast<netImpairmentPort_t *>( platformData ) );", ""),
            ("platforms disagree on a CVar", "posix",
             '"percentage of packets delivered twice"', '"percentage of duplicated packets"'),
            ("Windows ignores the bottleneck rate", "windows",
             "net_forceDuplicate.GetInteger(), net_forceRate.GetInteger(),", "net_forceDuplicate.GetInteger(), 0,"),
            ("POSIX drops the congestion count from its report", "posix",
             "static_cast<unsigned long long>( stats.bursts ), static_cast<unsigned long long>( stats.congested ), stats.maxWaitMsec,",
             "static_cast<unsigned long long>( stats.bursts ), 0ull, stats.maxWaitMsec,"),
            ("Windows reorders the report's leading fields", "windows",
             '"NET_IMPAIR port=%d dir=%s offered=%llu dropped=%llu duplicated=%llu released=%llu reordered=%llu "',
             '"NET_IMPAIR port=%d dir=%s offered=%llu dropped=%llu bursts=%llu duplicated=%llu released=%llu "'),
        )
        for label, platform, original, mutated in controls:
            source = posix if platform == "posix" else windows
            if source.count(original) != 1:
                raise AssertionError(f"control anchor is not unique: {label}")
            mutated_source = source.replace(original, mutated, 1)
            if platform == "posix":
                expect_failure(windows, mutated_source, label)
            else:
                expect_failure(mutated_source, posix, label)
    except AssertionError as error:
        print(f"network_impairment_contract: FAILED - {error}")
        return 1
    print(f"network_impairment_contract: ok ({len(controls)} single-reversal controls fail)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
