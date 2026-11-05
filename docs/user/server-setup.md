# Server Setup Guide

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

> **Experimental.** Multiplayer and the single-player Arena Campaign are
> under active development and are not yet considered stable. Expect rough
> edges, changing behaviour between builds, and bugs. They are not
> representative of the finished feature.

This guide covers a simple way to host an openQ4 dedicated server.

## Hosting from the Game

The multiplayer menu's **Create Server** page hosts a listen server: you play
on it while it serves the other players. **Server Type** decides who can join.
**Internet** advertises the server to the master servers and accepts players
from anywhere; **LAN** keeps it off the master servers and accepts only players
on your local network.

Set **Dedicated** to **Yes** to host without playing, as in the original game.
When you start the server, the game window closes and the openQ4 console window
takes its place. The server runs there and accepts console commands such as
`spawnServer mp/q4dm2`, `kick` and `quit`; close the console window to stop
the server. To play on it from the same computer, start openQ4 again and join
it from the multiplayer server browser.

To run a server without starting the game first, start openQ4 with
`+set net_serverDedicated 1`, as you could the original game. No game window
opens, and the console window runs the server as above:

```text
openQ4-client_x64 +set net_serverDedicated 1 +set si_map mp/q4dm1 +set si_gameType DM +spawnServer
```

On a machine without a display, use the dedicated server executable below.

## What You Need

- A working openQ4 install
- Access to the original Quake 4 retail assets
- The `openQ4-ded_<arch>` executable from your openQ4 release or local build
- A 64-bit host that matches the package architecture

## Dedicated Server Requirements

The dedicated server does not need a GPU or an OpenGL-capable desktop session, but it still needs the same retail `q4base/` assets and matching openQ4 game modules as the client.

Of openQ4's own content packs, a dedicated server needs only `baseoq4/pak0.pk4`. The light grids (`pak1.pk4`) and loadscreens (`pak2.pk4`) are for clients, so a server-only install can delete them and save about 800 MB; the server log then notes that it does not carry them. Clients keep using their own copies on your server, pure or not: those packs never enter a server's pure list.

For light testing, plan on a modern 64-bit host with at least 2 GB RAM available to the operating system and server, plus the same package and Quake 4 asset storage described in the [Getting Started system requirements](getting-started.md#system-requirements). For public servers, prefer 4 GB+ RAM, a stable wired connection, and enough upload bandwidth for the player count you advertise.

## Quick Start

1. Make sure openQ4 can see your Quake 4 assets.
2. Launch the dedicated server executable.
3. Set a server name, map, and game type.
4. Start the server with `spawnServer`.

Example startup flow:

```text
openQ4-ded_x64 +set net_ip 0.0.0.0 +set net_port 28004 +set si_name "My openQ4 Server" +set si_map mp/q4dm1 +set si_gameType DM +spawnServer
```

For remote administration, follow the [server and remote-console security
guide](server-security.md). Never put an rcon password in launch arguments.

## IPv4 Binding and Ports

For predictable hosting, set the interface and UDP port before starting the server:

| Variable | What it controls |
|---|---|
| `net_ip` | Local interface to bind. The default `localhost` value means all local interfaces for compatibility. Use `0.0.0.0` to state the IPv4 wildcard explicitly, or use a local IPv4 address such as `192.168.1.50` to bind only that adapter. |
| `net_port` | Server UDP port. `0` asks openQ4 to try the default server range, `28004` through `28007`; a value from `1` through `65535` selects that exact port. |

`net_ip` is a local bind address, not the public address reported by your router. Do not put a public WAN address here unless that address is actually assigned to an interface on the server host.

An explicit `net_ip` that cannot be resolved or bound now stops network startup instead of silently opening the server on every interface. Correct the address or select `0.0.0.0`, then start the server again.

Network endpoint ports use the full unsigned 16-bit range, `0` through `65535`. Port `0` has a special server configuration meaning: openQ4 selects an available default port rather than listening on literal port zero. The internal `PORT_ANY` value asks the operating system for an ephemeral port; clients and developer diagnostics use it, but it is not a normal public-server setting.

If `net_port` is `0`, check the server console or log to learn which port was selected before configuring a firewall or router.

## Connecting over IPv4

Clients can connect to a numeric IPv4 address or a hostname with an IPv4 DNS record. Include the port when the server is not using `28004`:

```text
connect 192.168.1.50:28004
connect 203.0.113.25:28004
connect play.example.net:28004
```

Omitting the port selects `28004`. A literal `:0` also selects that default for direct connections; it does not connect to a server listening on port zero.

## IPv6 Binding and Ports

IPv6 is on by default. The server opens an IPv6 socket alongside the IPv4 one on the **same** `net_port`, so a dual-stack server publishes a single port number and clients of either family reach it there.

| Variable | What it controls |
|---|---|
| `net_ip6` | Local IPv6 interface to bind. Empty, the default, binds every interface. Set a local IPv6 address to bind only that adapter. |
| `net_enableIPv4` | `1` opens the IPv4 socket, `0` leaves it closed. |
| `net_enableIPv6` | `1` opens the IPv6 socket, `0` leaves it closed. |
| `net_mcast6addr` | IPv6 multicast group answered during a LAN scan. The default `ff02::1` is the link-local all-nodes group. |
| `net_mcast6iface` | IPv6 interface index used for the LAN scan. `0`, the default, scans every attached link. |

IPv4 remains the baseline transport: when both families are enabled, a failure to bind the requested IPv4 port fails the whole startup rather than quietly leaving an IPv6-only server that IPv4 players cannot reach. A failure to bind the IPv6 socket is not fatal, so a host without IPv6 keeps working unchanged.

For an IPv6-only server, set `net_enableIPv4 0`. The IPv6 bind then owns the outcome and its failure stops startup the same way.

Older configurations that put an IPv6 literal in `net_ip` keep working: openQ4 treats it as the IPv6 interface and opens no IPv4 socket, exactly as before. Prefer `net_ip6` for new configurations.

## Connecting over IPv6

Bracket the address whenever a port follows it, or the last colon is read as the port separator:

```text
connect [2001:db8::1]:28004
connect [fe80::1%12]:28004
connect 2001:db8::1
```

The zone index in the second form (`%12`) selects the network interface for a link-local `fe80::/10` address, and is required for those addresses. Omitting the port selects `28004`.

A hostname with both A and AAAA records resolves to the IPv4 address; use the bracketed literal to force IPv6.

Run `netIPv6SelfTest` on the server console to confirm IPv6 parsing and loopback transport before opening firewall rules. It prints `IPv6 network self-test: passed`, or reports that it skipped the transport checks if the host has no IPv6 configured.

## Firewall and NAT

For predictable hosting, use direct IPv4 UDP with the firewall and port-forward
rules below. The legacy `net_socks*` CVars remain visible for configuration
compatibility, but openQ4 does not enable or support SOCKS UDP relay; setting
them does not route multiplayer traffic through a SOCKS proxy.

- Allow inbound UDP on the selected `net_port` in the server host's firewall. Normal gameplay connections do not require an inbound TCP port forward.
- If the server is behind a router, forward the same external UDP port to the server's local IPv4 address and selected port. Give the host a stable DHCP reservation or static local address so that rule does not drift.
- Players on the same LAN should connect to the server's local IPv4 address. Internet players should use the router's public IPv4 address or a hostname with an IPv4 DNS record.
- Test public reachability from outside the server's LAN. Some routers do not support connecting back through their own public address.
- For IPv6, allow inbound UDP on the same `net_port` for the server's IPv6 address. IPv6 hosts are usually globally routable with no NAT and no port forward, so the router's firewall rule is normally the only change required. Publish the server's global IPv6 address, not a `fe80::` link-local one, which only reaches the same physical link.
- Carrier-grade NAT or another upstream NAT can prevent unsolicited inbound connections even when the local router is configured correctly; in that case, ask the network provider for a public IPv4 address or a suitable port-forwarding service.

## Common Server Variables

| Variable | What it controls |
|---|---|
| `si_name` | Server name shown to players |
| `si_motd` | A message players read on the loading screen while they load the map. Up to 256 bytes; the screen wraps it to three lines, and color codes are dropped |
| `si_map` | Starting map |
| `si_gameType` | Multiplayer game type |
| `si_fragLimit` | Frag limit |
| `si_timeLimit` | Time limit |
| `si_warmup` | Whether warmup is used |
| `g_mapCycle` | Map cycle script |
| `bot_minPlayers` | Keep the match topped up to this many players with bots (`0` disables) |
| `bot_skill` | Bot difficulty, 1 (harmless) to 5 (unpleasant) |
| `bot_skillVariance` | Spread bot skill this many levels either side of `bot_skill`, so a match is not all one difficulty |
| `bot_characters` | Give bots named characters with their own play style and voice (`0` for plain skill-curve bots) |
| `bot_chat` | Bot chat: `0` silent, `1` normal, `2` chatty |
| `bot_chatCPM` | Bot typing speed in visible characters per minute (`900` by default) |

Default multiplayer values are seeded from `content/baseoq4/pak0/default.cfg`.

## Useful Console Commands

| Command | What it does |
|---|---|
| `spawnServer` | Starts the server |
| `disconnect` | Shuts the server down |
| `serverMapRestart` | Restarts the current map |
| `serverNextMap` | Advances to the next map |
| `kick` | Kicks a client by slot number |
| `gameKick` | Kicks a client by player name |
| `addbot` | Adds one bot, optionally by name and skill |
| `kickbots` | Removes every bot |
| `botlist` | Lists the bots and their characters |

## Bots

openQ4 ships bots that navigate any multiplayer map with no per-map setup, so a
server can stay populated while it is quiet. Set `bot_minPlayers` to the player
count you want the match held at and the server fills the rest, releasing the
slots again as real players connect.

Both settings are also on the multiplayer create-server screen, under **Advanced
Server Options**: **Fill With Bots** is `bot_minPlayers` and **Bot Skill** is
`bot_skill`, named with the same ladder the Arena campaign uses - Training,
Rookie, Veteran, Nightmare, Impossible. They are archived like every other bot
cvar, so a value picked in the menu is still there the next time you host.

Bots are entirely server-side; clients need nothing installed and see them as
ordinary players. Each one gets a name, a play style and its own chat lines.
They can answer common conversational phrases from people or other bots, and
addressing one by name makes that character the preferred responder. Team chat
stays inside the team, and replies cannot trigger reply chains. Set
`bot_characters 0` for anonymous bots on the plain skill curve, or `bot_chat 0`
to keep them quiet. Messages wait briefly according to their visible length,
and the stock Quake 4 typing icon appears above the bot until the line is sent.

### Giving bots orders

In a team game you can tell the bots on your side what to do in team chat.
Start with a bot's name - or `everyone` for all of them - and follow it with the
order:

| Type | The bot |
| --- | --- |
| `kane follow me` | stays with you and fights beside you |
| `everyone defend` | holds your team's base in Capture the Flag |
| `sorg hold here` | holds the area where you are standing |
| `kane attack` | goes for the enemy flag, or escorts whoever has it |
| `everyone dismissed` | goes back to playing on its own |

`escort me`, `cover me`, `guard`, `get the flag` and `free` work too, and you
can name several bots at once: `sorg and bagby defend`. The bots answer in team
chat so you know the order landed. An order lasts until you give another one or
dismiss them, and it ends if the bot changes team or the map changes. A bot
carrying the flag still takes it home, and a badly hurt bot still grabs health
on the way.

The bot cvars are archived, so setting them once in your server config is
enough. For the full command list, the character file format, and how to add
your own characters, see [Multiplayer bots](../dev/mp-bots.md).

## Competitive Match Management (Development Preview)

Managed competitive profiles, server-owned readiness, tactical timeouts,
structured match evidence and automatic multi-view recording are under active
development. They are not tournament-qualified yet, and several captain,
referee, series and spectator workflows do not have their finished interface.

See [Competitive Matches](competitive-matches.md) for the currently usable
profile and readiness path, the exact unfinished boundaries, and the operator
test checklist. Casual servers do not opt into managed-match policy unless an
operator selects a competitive profile.

## Multiplayer Tuning

For IPv4 connection behavior and prediction or lag compensation tuning, see [Multiplayer Networking](multiplayer-networking.md).

## Notes

- openQ4 uses its own engine and game modules.
- openQ4 is not a drop-in runtime for the original proprietary Quake 4 DLL mods.
- For advanced configuration, file layout, and path behavior, see [TECHNICAL.md](../../TECHNICAL.md).
