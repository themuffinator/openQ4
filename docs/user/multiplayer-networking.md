# Multiplayer Networking Guide

> **Experimental.** Multiplayer and the single-player Arena Campaign are
> under active development and are not yet considered stable. Expect rough
> edges, changing behaviour between builds, and bugs. They are not
> representative of the finished feature.

This guide covers openQ4 multiplayer networking behavior and the cvars used to tune or revert prediction/lag-comp behavior.

## Quick Summary

- The server browser lists discovered servers, keeps your selection through sorting, and remembers favorite server addresses.
- Direct connections accept numeric IPv4 addresses, IPv6 addresses, and hostnames with either kind of DNS record.
- IPv4 ports use the complete unsigned 16-bit range, including high ports above `32767`.
- IPv6 is enabled by default. A server binds both families on the same port, so one address works for every client.
- LAN scanning finds IPv6 servers through link-local multicast, which replaces the IPv4 broadcast that IPv6 does not have.
- Server-side hitscan lag compensation is enabled by default.
- Remote-client prediction runs in enhanced mode by default.
- Both systems can be switched back to legacy behavior with cvars.

## Find and join a server

Open **Multiplayer > Browse**, then choose **Internet** or **LAN**. The list
shows the server name, ping, player count, game type and map, with the stock
icons for favorites, passwords and dedicated servers. Select a row to see its
address and map preview, then choose **Join**. Passworded servers use the
existing password prompt.

Click a column heading to sort. Filters can hide full or empty servers,
passworded servers, game types or mods. When a filter hides the selected
server, Join is disabled until you select a visible server. Refresh updates
the currently known addresses; Get New List runs discovery again.

The favorite control saves the selected server's address locally. Favorites
are queried alongside Internet discovery, so a saved address can still appear
when it is absent from the master list. Removing a favorite leaves the server
available in ordinary discovery results.

LAN discovery probes ports `28004` through `28011`. Use a direct connection
for a server on another port. Internet results depend on the configured master
service and reachable servers; direct connections remain available below.

## IPv4 Connections

Use `connect <address>:<port>` for a direct IPv4 connection. These forms are supported:

```text
connect 192.168.1.50:28004
connect 203.0.113.25:32000
connect play.example.net:28004
```

The first form is suitable for a local network, the second illustrates an internet-facing IPv4 address, and the third uses a hostname with an IPv4 DNS record. If the port is omitted, openQ4 uses the default server port `28004`.

Endpoint ports are parsed as unsigned 16-bit values from `0` through `65535`. For a direct connection, an omitted port or literal `:0` selects `28004`; ports from `1` through `65535` select that exact destination. Internet server-list entries also preserve the full range, so servers using ports `32768` through `65535` remain connectable.

Malformed addresses, non-numeric port text, and ports outside the valid range are rejected. An unresolved remote-console address is rejected as well, rather than sending a command to an unintended wildcard address.

Server operators should configure `net_ip`, `net_port`, host firewall rules, and any router port forward as described in the [Server Setup Guide](server-setup.md#ipv4-binding-and-ports).

## IPv6 Connections

An IPv6 address contains colons, so it must be wrapped in brackets whenever a port follows it. Without the brackets the last colon would be read as the port separator and eat part of the address.

```text
connect [2001:db8::1]:27650
connect [fe80::1%12]:27650
connect 2001:db8::1
connect play.example.net:28004
```

The first form is an ordinary global IPv6 address. The second adds a *zone index*, which a link-local `fe80::/10` address needs so the system knows which network interface to use; openQ4 prints and accepts the numeric index. The third omits the port, which selects the default server port `28004` — brackets are optional when there is no port. The fourth resolves a hostname: if it has both an A and a AAAA record, openQ4 prefers the IPv4 address, so use the bracketed literal to force IPv6.

Addresses are displayed in the canonical [RFC 5952](https://www.rfc-editor.org/rfc/rfc5952) form — lowercase, with the longest run of zero groups collapsed to `::` — so `[2001:0db8:0000:0000:0000:0000:0000:0001]:27650` is shown as `[2001:db8::1]:27650`. The same text is used by the server browser and by server-side ban lists, so one address always has exactly one spelling.

A client reaching a dual-stack server over IPv4 is recorded as an IPv4 client even though the server also listens on IPv6, so bans and client-slot handling see a single identity per host.

Run `netIPv6SelfTest` from the console to check IPv6 parsing, canonical formatting, and loopback transport on the local machine. It reports `IPv6 network self-test: passed`, or skips the transport checks on a host with no IPv6 configured. `netIPv4SelfTest` does the same for IPv4.

### Why IPv6 packets are smaller

IPv6 guarantees only a 1280-byte path MTU and forbids routers from splitting a datagram in transit, so openQ4 fragments its own messages more aggressively on an IPv6 connection than on IPv4. This is automatic and needs no configuration; it prevents the failure where a client connects and then stalls forever waiting for a snapshot that no router will deliver.

## CVar Reference

| Setting | Default | Range | Scope | What it does |
|---|---:|---:|---|---|
| `net_ip` | `localhost` | address | Client and server | IPv4 interface to bind. `localhost` or empty binds every interface. |
| `net_ip6` | *(empty)* | address | Client and server | IPv6 interface to bind. Empty binds every interface. |
| `net_enableIPv4` | `1` | `0..1` | Client and server | Opens the IPv4 socket. |
| `net_enableIPv6` | `1` | `0..1` | Client and server | Opens the IPv6 socket. Set `net_enableIPv4 0` for an IPv6-only host. |
| `net_mcast6addr` | `ff02::1` | address | Client and server | IPv6 multicast group used by the LAN server scan. |
| `net_mcast6iface` | `0` | index | Client and server | IPv6 interface index for the LAN scan. `0` scans every attached link. |
| `net_mpLagCompensation` | `1` | `0..1` | Server gameplay | Enables server-side lag compensation for multiplayer hitscan traces. |
| `net_mpLagCompMaxMS` | `200` | `0..1000` | Server gameplay | Caps rewind window in milliseconds used by lag compensation. |
| `net_mpLagCompBiasMS` | `0` | `-200..200` | Server gameplay | Adds/subtracts additional rewind bias in milliseconds. |
| `net_mpLagCompDebug` | `0` | `0..2` | Server gameplay | Debug logging for lag compensation (`0` off, `1` summary, `2` verbose). |
| `net_mpPredictMode` | `1` | `0..1` | MP client prediction | Selects remote-player prediction mode (`0` legacy limited, `1` enhanced per-frame). |

## Legacy Compatibility Switch

Use this to restore legacy multiplayer behavior:

```cfg
seta net_mpLagCompensation 0
seta net_mpPredictMode 0
```

## Recommended Starting Presets

### Default Internet Play

```cfg
seta net_mpLagCompensation 1
seta net_mpLagCompMaxMS 200
seta net_mpLagCompBiasMS 0
seta net_mpPredictMode 1
```

### Low-Latency/LAN

```cfg
seta net_mpLagCompensation 1
seta net_mpLagCompMaxMS 80
seta net_mpLagCompBiasMS 0
seta net_mpPredictMode 1
```

## Notes

- Lag compensation applies to authoritative multiplayer hitscan traces on the server.
- `net_mpLagCompDebug` output is intended for server diagnostics and tuning.
- Tune `net_mpLagCompMaxMS` before using large `net_mpLagCompBiasMS` offsets.
