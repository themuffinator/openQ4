# Multiplayer Networking Guide

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

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
- Server-side hitscan lag compensation is off by default (`net_mpLagCompensation 0`).
- Remote-client prediction uses legacy mode by default (`net_mpPredictMode 0`).
- Enhanced prediction and lag compensation are opt-in experiments. Each client tells the server how far ahead it draws other players, and the server rewinds only the rest, so the two settings no longer correct the same delay twice. Their combined behavior is still being measured on real connections.
- Retail Quake 4 1.4.2 clients and servers use protocol 2.85 and cannot play with openQ4. Each side is refused with a message naming the protocol; see [Retail Quake 4 servers](#retail-quake-4-servers).
- Use matching openQ4 releases and content on the server and clients. The voice-enabled candidate and release 0.13.0 refuse to join each other: pure servers report missing packages, while non-pure servers report data out of sync. Other release pairs need their own compatibility check. Voice packets are sent only to peers advertising the supported voice protocol.

## Find and join a server

Open **Multiplayer > Join Game**, then choose **Internet** or **LAN**. The
list shows the server name, ping, player count, game type and map, with the
stock icons for favorites, passwords, dedicated servers and Q4TV repeaters.
Select a row to see its map preview, then choose **Join Server** to play, or
in the new menus **Spectate** to join it watching. Passworded servers use the
existing password prompt.

Choose a column heading to sort, and **Clear Sorting** to list servers in the
order they answered. **Mod** shows one mod's servers; the `gui_filter_*`
console settings can also hide full or empty servers, passworded servers or
game types. When a filter hides the selected server, Join Server is disabled
until you select a visible server, and the list tells you when the filters
hide every server. **Refresh List** updates the currently known addresses;
**Get New List** runs discovery again. The bar under the list fills as
servers answer and names the step the scan is on. Bars beside each ping
show the connection's quality: four up to 60 ms, three to 120 ms, two to
200 ms and one above, turning amber at two bars and red at one. Point at a server, or
move to it with the keyboard or a controller, to see its address, players
and settings in a card beside it. With a controller, the left face button
refreshes the list and the top one sorts it by the next column; pressing the
left stick spectates the selected server and pressing the right stick adds
it to or removes it from your favorites, as the stick symbols beside
**Spectate** and the favorite control show.

**Join IP Address** connects to an address you type, in any form the direct
connections below accept, and shows your own IP address to pass on to
friends. An address that cannot be joined keeps the dialog open with the
reason.

**Find Player/Clan** shows only the listed servers with a player whose name
contains what you type, ignoring colors and case; a clan tag counts as part of
the name. The search shows as a tag below the list: select it to end the
search. Get New List ends it as well. The original game asked id's master
server for this search, which no longer answers.

The favorite control saves the selected server's address locally. Favorites
are queried alongside Internet discovery, so a saved address can still appear
when it is absent from the master list. Removing a favorite leaves the server
available in ordinary discovery results.

LAN discovery probes ports `28004` through `28011`. Use a direct connection
for a server on another port. Internet results depend on the configured master
service and reachable servers; direct connections remain available below.
Over IPv6 the scan reaches servers on the same network link, including
IPv6-only servers on Windows. A server that listens only on the IPv6 loopback
address `::1` has no link to scan, so join it with `connect [::1]:<port>`.

### Retail Quake 4 servers

Retail Quake 4 1.4.2 servers use network protocol 2.85; openQ4 uses 2.41, so
the two cannot play together. A LAN scan leaves retail servers out of the list
(the console notes each one as `ignored - protocol 2.85`). Connecting to one
directly shows **Network protocol mismatch** with both versions, before openQ4
would switch to the retail server's `q4mp` folder. A retail player who tries an
openQ4 server sees `server uses protocol 2.41`. See
[server security](server-security.md#retail-quake-4-clients-and-servers).

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

The first form is an ordinary global IPv6 address. The second adds a *zone index*, which a link-local `fe80::/10` address needs so the system knows which network interface to use; openQ4 prints and accepts the numeric index. The third omits the port, which selects the default server port `28004` — brackets are optional when there is no port. The fourth resolves a hostname: if it has both an A and a AAAA record, openQ4 tries the IPv4 address first, so use the bracketed literal to go straight to IPv6.

A hostname can have several addresses. openQ4 connects to the first one, and if
three connection attempts a second apart go unanswered it tries the host's other
addresses in turn: up to eight of them, alternating between IPv4 and IPv6, and
starting over after the last. The console names each address as it is tried, and
the first server to answer is the one you join. A numeric address and a server
list entry are a single address, so they keep retrying that address until you
cancel.

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
| `net_clientMaxRate` | `128000` | bytes/s | Client | The most a server may send you each second, sent when you connect. Lower it below your connection's real download speed if matches stutter on a slow or shared line. |
| `net_serverMaxClientRate` | `128000` | bytes/s | Server | The most the server sends any one player each second, whatever they ask for. |
| `net_mpLagCompensation` | `0` | `0..1` | Server gameplay | Enables server-side lag compensation for multiplayer hitscan traces. |
| `net_mpLagCompMaxMS` | `200` | `0..1000` | Server gameplay | Caps rewind window in milliseconds used by lag compensation. |
| `net_mpLagCompBiasMS` | `0` | `-200..200` | Server gameplay | Adds/subtracts additional rewind bias in milliseconds. |
| `net_mpLagCompDebug` | `0` | `0..2` | Server gameplay | Debug logging for lag compensation (`0` off, `1` summary, `2` verbose). |
| `net_mpLagCompSlackMS` | `48` | `0..200` | Server gameplay | How far past the round trip the server measured a rewind may reach. |
| `net_mpPredictMode` | `0` | `0..1` | MP client prediction | Selects remote-player prediction mode (`0` legacy limited, `1` enhanced per-frame). |
| `net_mpPredictMaxFrames` | `16` | `1..64` | MP client prediction | The most game frames (16 ms each) enhanced prediction draws another player ahead. |
| `net_mpRemoteExtrapolation` | derived | read-only | MP client prediction | What this client tells servers it draws other players ahead by, in frames: `0` in legacy mode, otherwise `net_mpPredictMaxFrames`. |

## How prediction and lag compensation fit together

Your own movement is predicted, so you see it at once. Other players are
shown from the last update the server sent, which is a little old by the time
it arrives.

- **Legacy prediction** (`net_mpPredictMode 0`) shows other players about one
  round trip behind where the server has them.
- **Enhanced prediction** (`net_mpPredictMode 1`) re-simulates them from their
  relayed inputs, drawing them up to `net_mpPredictMaxFrames` frames closer to
  the present.
- **Lag compensation** (`net_mpLagCompensation 1`, server only) briefly moves
  other players back to where your screen showed them when your hitscan,
  lightning gun or gauntlet attack lands.

Each client advertises how far ahead it draws other players
(`net_mpRemoteExtrapolation`). The server takes that off the rewind, so a shot
is rewound only by the delay the client really showed. Every figure the client
reports is capped by the round trip the server measured itself, so a client
cannot claim a longer rewind. Damage, blood, impact effects and knockback are
always applied where the target is now.

Server operators can run `lagCompStatus` to see, for each player, the reported
lead, the measured round trip, the advertised extrapolation and the rewind a
shot from that player would get. It works whether or not lag compensation is
on. `net_mpLagCompDebug 1` logs the same figures for each compensated shot,
and `net_mpPredictDebug 1` makes a client log how old each remote player was
on its screen when a server update arrived. `net_verbose 2` also logs, for
each server update, how far ahead the client is predicting, the target it is
steering towards and how early the server says its commands arrive.

## Legacy Compatibility Switch

Use this to restore legacy multiplayer behavior:

```cfg
seta net_mpLagCompensation 0
seta net_mpPredictMode 0
```

## Experimental Settings

Both features are still being measured on real connections with delay,
jitter and packet loss, so neither is on by default. To try them:

### Clients: enhanced prediction

```cfg
seta net_mpPredictMode 1
```

### Servers: lag compensation

```cfg
seta net_mpLagCompensation 1
seta net_mpLagCompMaxMS 200
seta net_mpLagCompBiasMS 0
```

On a LAN, a smaller `net_mpLagCompMaxMS` such as `80` keeps the rewind short.
Clients may use either prediction mode on a lag-compensated server; the rewind
follows each client's own setting.

## Testing under simulated network conditions

openQ4 can add the delay, jitter, loss and duplication of a real connection to
its own packets, so a server and client on one computer behave like players on
the internet. Every setting applies to the computer that sets it, in both
directions of each connection:

| CVar | Effect |
| --- | --- |
| `net_forceLatency` | Milliseconds added to every packet, each way. |
| `net_forceJitter` | Up to this many further milliseconds, chosen at random per packet. Packets sent closer together than this can arrive out of order, as they do on real links. |
| `net_forceDrop` | Percentage of packets lost. |
| `net_forceDuplicate` | Percentage of packets delivered twice. |
| `net_forceDropBurst` | Loses packets in runs of about this many instead of one at a time, keeping the `net_forceDrop` rate. |
| `net_forceRate` | Limits the connection to this many bytes a second each way, like a slow line. Packets queue behind each other and are dropped when the queue is full. `0` for no limit. |
| `net_forceQueue` | How many milliseconds of packets that slow line holds before it drops them (default `250`). |
| `net_forceSeed` | Makes the random choices repeat exactly when a connection opens; `0` picks new ones each time. |

For example, on the client:

```cfg
set net_forceLatency 50
set net_forceJitter 30
set net_forceDrop 5
```

`netImpairment` prints the settings and, for each open connection, how many
packets were delayed, lost, duplicated and delivered out of order, the bytes
sent through it, and how many packets the slow line dropped. A match with three
bots needs about 6 KB a second downstream. On a line slower than that the game
stutters and falls behind, and setting `net_clientMaxRate` a little below the
line's speed before connecting keeps it smooth, with fewer updates a second.
These settings count as cheats: hosting or joining a server that does not allow
cheats (`net_allowCheats 1` or `sv_cheats 1`) resets them to `0`, and they
cannot be changed during such a match. They are for testing only.

## Notes

- Lag compensation applies to authoritative multiplayer hitscan, lightning gun and gauntlet traces on the server.
- `net_mpLagCompDebug` output is intended for server diagnostics and tuning.
- Tune `net_mpLagCompMaxMS` before using large `net_mpLagCompBiasMS` offsets.
