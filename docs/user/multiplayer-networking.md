# Multiplayer networking

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

> **Experimental.** Multiplayer is still under active development. Expect rough
> edges, bugs and behaviour that changes between versions.

This guide shows you how to find and join online games, connect to a server by
address over IPv4 or IPv6, and choose how other players' movement and your own
shots are shown on a connection with delay. To host a server, see the
[server setup guide](server-setup.md).

## Before you play

- **Use the same openQ4 version everywhere.** The server and every client need
  openQ4 0.14.1 and the same game files. Different openQ4 versions are not
  guaranteed to play together: 0.13.0 and 0.14.1, for example, refuse each
  other, with a pure server reporting missing packages and other servers
  reporting that data is out of sync.
- **Retail Quake 4 cannot play with openQ4.** Retail Quake 4 1.4.2 clients and
  servers use a different network protocol (2.85), so neither side can join the
  other. openQ4 refuses them with an explanation instead of hanging; see
  [Retail Quake 4 servers](#retail-quake-4-servers).

## Find and join a server

Open **Multiplayer → Join Game**, then choose **Internet** or **LAN**. The list
shows each server's name, ping, player count, game type and map, with icons for
favourites, passwords, dedicated servers and Q4TV repeaters. Select a row to
see its map preview, then choose **Join Server** to play or, in the new menus,
**Spectate** to join it watching. A passworded server asks for its password.

In the new menus, the layout button beside **Source** switches the list between
rows and larger tiles that show each server's map picture. The game remembers
your choice.

**Sorting and filtering**

- Choose a column heading to sort, and **Clear Sorting** to list servers in the
  order they answered.
- **Mod** shows one mod's servers. The `gui_filter_*` console variables can
  also hide full, empty or passworded servers, or game types. When a filter
  hides the selected server, **Join Server** is unavailable until you select a
  visible one, and the list tells you when the filters hide every server.
- **Refresh List** asks the servers you already know for fresh details; **Get
  New List** searches for servers again. The bar under the list fills as
  servers answer.
- The bars beside each ping show connection quality: four bars up to 60 ms,
  three up to 120 ms, two up to 200 ms and one above that. Two bars turn amber
  and one turns red.
- Point at a server, or move to it with the keyboard or a controller, to see its
  address, players and settings in a card beside it.

**With a controller,** the left face button refreshes the list and the top face
button sorts by the next column. Press the left stick to spectate the selected
server and the right stick to add it to or remove it from your favourites, as
the stick symbols on screen show.

**Join IP Address** connects to an address you type, in any of the forms under
[Connect by address](#connect-by-address), and shows your own IP address to
pass on to friends. If the address cannot be joined, the dialog stays open and
says why.

**Find Player/Clan** shows only the listed servers with a player whose name
contains what you type, ignoring colours and case. A clan tag counts as part of
the name. The search shows as a tag below the list: select the tag, or choose
**Get New List**, to end it.

**Favourites.** The favourite control saves the selected server's address on
your computer. Favourites are asked for details along with the Internet search,
so a saved server can appear even when the master server does not list it.
Removing a favourite does not hide the server from ordinary results.

**LAN games.** The LAN scan looks for servers on ports `28004` to `28011`. For a
server on any other port, connect by address. Over IPv6 the scan finds servers
on the same local network, including IPv6-only servers on Windows. A server
that listens only on the IPv6 loopback address `::1` cannot be found by a scan;
join it with `connect [::1]:<port>`.

### Retail Quake 4 servers

Retail Quake 4 1.4.2 servers use network protocol 2.85 and openQ4 uses 2.41, so
the two cannot play together:

- A LAN scan leaves retail servers out of the list. The console notes each one
  as `ignored - protocol 2.85`.
- Connecting to one directly shows **Network protocol mismatch** with both
  versions, before openQ4 would switch to the retail server's `q4mp` folder.
- A retail player who tries an openQ4 server sees `server uses protocol 2.41`.

See [server security](server-security.md#retail-quake-4-clients-and-servers)
for more.

## Connect by address

Open the console and use `connect <address>`, or type the address into **Join
IP Address**. If you leave out the port, openQ4 uses the default server port
`28004`.

### IPv4 connections

```text
connect 192.168.1.50:28004
connect 203.0.113.25:32000
connect play.example.net:28004
```

The first is a typical local-network address, the second an internet address
on a different port, and the third a hostname.

Any port from `1` to `65535` works, including ports above `32767`, and servers
on those ports stay joinable from the Internet list. A port of `0` means the
default port. openQ4 rejects malformed addresses, non-numeric ports and ports
out of range rather than guessing.

Server operators set the address and port with `net_ip` and `net_port`, and
need a firewall rule and possibly a router port forward; see
[IPv4 binding and ports](server-setup.md#ipv4-binding-and-ports).

### IPv6 connections

IPv6 is on by default. A server listens on IPv4 and IPv6 at the same port, so
one server address works for every client.

An IPv6 address contains colons, so wrap it in square brackets whenever a port
follows it. Without the brackets, the last colon would be read as the start of
the port.

```text
connect [2001:db8::1]:27650
connect [fe80::1%12]:27650
connect 2001:db8::1
connect play.example.net:28004
```

- The first is an ordinary IPv6 address.
- The second is a local-network (link-local, `fe80::`) address, which needs the
  number of your network interface after a `%` so the system knows which
  connection to use.
- The third leaves out the port, so it uses `28004`. Brackets are optional when
  there is no port.
- The fourth is a hostname. If it has both an IPv4 and an IPv6 address, openQ4
  tries IPv4 first; type the bracketed IPv6 address to go straight to IPv6.

A hostname can have several addresses. openQ4 connects to the first one, and if
three attempts a second apart go unanswered, it tries the host's other addresses
in turn: up to eight of them, alternating between IPv4 and IPv6, starting over
after the last. The console names each address as it tries it, and
the first server to answer is the one you join. A numeric address or a
server-list entry is a single address, so openQ4 keeps retrying it until you
cancel.

openQ4 always writes an IPv6 address in its shortest standard form
([RFC 5952](https://www.rfc-editor.org/rfc/rfc5952)): lower case, with the
longest run of zeros shortened to `::`. So
`[2001:0db8:0000:0000:0000:0000:0000:0001]:27650` appears as
`[2001:db8::1]:27650`. The server browser and server ban lists use the same
spelling, so one address is never listed twice. A player who reaches a server
over IPv4 is treated as an IPv4 player, even though the server also listens on
IPv6.

### IPv6 and packet size

Every network carries packets up to a certain size in one piece. IPv6
guarantees only 1,280 bytes and does not let routers split larger packets on
the way, so on an IPv6 connection openQ4 sends its game messages in smaller
pieces than on IPv4. This is automatic. It stops the problem where a player
connects and then waits forever for a game update that never arrives.

- **If an IPv6 server never answers when you join, or never appears in the
  list,** your network may be blocking split packets (some VPNs and firewalls
  do). In 0.14.1 the server's replies to a join or a server-list request are
  larger than 1,280 bytes, so your operating system has to split them. Join
  that server over IPv4 instead.
- **Not in 0.14.1:** those replies now fit one packet, and a game message that
  grew when compressed is split like any other, so joining and listing work on
  such networks when you and the server both run the next release. A server
  list reply may then leave out settings the browser never shows.

## How other players and your shots are shown

Every update from the server takes time to reach you, so the game fills the gap
in a few ways:

- **Your own movement is predicted.** Your game works out where you are going
  without waiting for the server, so movement responds at once.
- **Your own instant-hit shots** (**Not in 0.14.1**). Machinegun, shotgun and
  railgun tracers and their marks on walls appear the moment you fire, from your
  gun and along your crosshair, as the lightning gun's beam and the gauntlet's
  sparks always did. The server fires the same shotgun pellets, because both
  sides use the same spread. Hits on players are still the server's decision:
  the impact on a body, the hit marker and damage numbers arrive with the
  server's update. Rockets, grenades and other projectiles appear when the
  server launches them. In 0.14.1, all of your shots appear when the server's
  copy arrives. Set `net_mpPredictHitscan 0` to keep that behaviour.
- **Other players** are drawn from the server's last update, which is slightly
  old by the time it arrives:
  - **Legacy prediction** (`net_mpPredictMode 0`, the default) shows them about
    one round trip behind where the server has them.
  - **Enhanced prediction** (`net_mpPredictMode 1`) replays their latest inputs
    to draw them up to `net_mpPredictMaxFrames` frames closer to the present.
- **Lag compensation** (`net_mpLagCompensation 1`, set on the server) makes
  aiming at what you see count. When your instant-hit weapon, lightning gun or
  gauntlet attack is checked, the server briefly moves the other players back
  to where your screen showed them.

The two settings work together without correcting the same delay twice. Your
game tells the server how far ahead it draws other players
(`net_mpRemoteExtrapolation`), and the server rewinds a shot only by the delay
your screen really had. The server caps every figure at the round trip it
measures itself, so a player cannot claim a longer rewind. Damage, blood,
impact effects and knockback always happen where the target is now.

### Try enhanced prediction or lag compensation

Both features are experimental and off by default while they are tested on
real connections with delay, jitter and packet loss.

On a client, to draw other players closer to the present:

```cfg
seta net_mpPredictMode 1
```

On a server, to rewind shots to what each player saw:

```cfg
seta net_mpLagCompensation 1
seta net_mpLagCompMaxMS 200
seta net_mpLagCompBiasMS 0
```

On a LAN, a smaller `net_mpLagCompMaxMS` such as `80` keeps the rewind short.
Adjust `net_mpLagCompMaxMS` before reaching for large `net_mpLagCompBiasMS`
offsets. Clients may use either prediction mode on a lag-compensated server;
the rewind follows each client's own setting.

To go back to the defaults:

```cfg
seta net_mpLagCompensation 0
seta net_mpPredictMode 0
```

## Settings reference

Type these into the console, or put them in your configuration file.

### Connection

| Setting | Default | Set on | What it does |
|---|---|---|---|
| `net_ip` | `localhost` | Client and server | IPv4 address to listen on. `localhost` or empty listens on every network interface. |
| `net_ip6` | *(empty)* | Client and server | IPv6 address to listen on. Empty listens on every network interface. |
| `net_enableIPv4` | `1` | Client and server | Uses IPv4. |
| `net_enableIPv6` | `1` | Client and server | Uses IPv6. Set `net_enableIPv4 0` as well for an IPv6-only host. |
| `net_mcast6addr` | `ff02::1` | Client and server | The IPv6 group address the LAN scan uses. |
| `net_mcast6iface` | `0` | Client and server | Network interface number for the IPv6 LAN scan. `0` scans every connected network. |
| `net_clientMaxRate` | `128000` | Client | The most a server may send you each second, in bytes. Lower it below your real download speed if matches stutter on a slow or shared line. |
| `net_serverMaxClientRate` | `128000` | Server | The most the server sends any one player each second, in bytes, whatever they ask for. |
| `ui_browserView` | `0` | Client | Server browser layout in the new menus: `0` rows, `1` tiles. |

### Prediction and lag compensation

| Setting | Default | Set on | What it does |
|---|---|---|---|
| `net_mpPredictMode` | `0` | Client | How other players are drawn: `0` legacy, `1` enhanced. |
| `net_mpPredictMaxFrames` | `16` | Client | With enhanced prediction, the most game frames (16 ms each) another player is drawn ahead, from `1` to `64`. |
| `net_mpRemoteExtrapolation` | automatic | Client (read-only) | How far ahead this client tells servers it draws other players, in frames: `0` in legacy mode, otherwise `net_mpPredictMaxFrames`. |
| `net_mpPredictHitscan` | `1` | Client | **Not in 0.14.1:** draws your own machinegun, shotgun and railgun tracers and wall marks the moment you fire. `0` waits for the server's copy of each shot. |
| `net_mpLagCompensation` | `0` | Server | Turns on lag compensation for instant-hit weapons, the lightning gun and the gauntlet. |
| `net_mpLagCompMaxMS` | `200` | Server | The longest rewind, in milliseconds (`0` to `1000`). |
| `net_mpLagCompBiasMS` | `0` | Server | Adds to or takes from every rewind, in milliseconds (`-200` to `200`). |
| `net_mpLagCompSlackMS` | `48` | Server | How far past the measured round trip a rewind may reach, in milliseconds (`0` to `200`). |

### Troubleshooting and diagnostics

| Command or setting | What it does |
|---|---|
| `netIPv6SelfTest` | Checks IPv6 address handling and a local IPv6 connection on this computer. It reports `IPv6 network self-test: passed`, or skips the connection checks when the computer has no IPv6. |
| `netIPv4SelfTest` | The same for IPv4. |
| `lagCompStatus` | On a server, shows for each player the lead they report, the round trip the server measured, how far ahead they draw others and the rewind their shot would get. Works whether or not lag compensation is on. |
| `net_mpLagCompDebug` | On a server, logs those figures for each compensated shot: `0` off, `1` summary, `2` detailed. |
| `net_mpPredictDebug 1` | On a client, logs how old each other player was on screen when a server update arrived. |
| `net_verbose 2` | On a client, also logs for each server update how far ahead the client is predicting and how early the server says your commands arrive. |
| `net_mpHitscanDebug 1` | **Not in 0.14.1:** logs every instant-hit shot: the server's, the ones your client drew itself and the server's copies it received, with how far each started from your gun and aim. |

## Testing under simulated network conditions

openQ4 can add the delay, jitter, loss and duplication of a real connection to
its own packets, so a server and client on one computer behave like players on
the internet. Each setting applies to the computer that sets it, in both
directions of each connection:

| Setting | Effect |
| --- | --- |
| `net_forceLatency` | Milliseconds added to every packet, each way. |
| `net_forceJitter` | Up to this many further milliseconds, chosen at random per packet. Packets sent closer together than this can arrive out of order, as on real connections. |
| `net_forceDrop` | Percentage of packets lost. |
| `net_forceDuplicate` | Percentage of packets delivered twice. |
| `net_forceDropBurst` | Loses packets in runs of about this many instead of one at a time, keeping the `net_forceDrop` rate. |
| `net_forceRate` | Limits the connection to this many bytes a second each way, like a slow line. Packets queue behind each other and are dropped when the queue is full. `0` for no limit. |
| `net_forceQueue` | How many milliseconds of packets that slow line holds before it drops them (default `250`). |
| `net_forceMTU` | **Not in 0.14.1:** the largest packet, in bytes, the connection carries in one piece, like a VPN with a small packet size (`1280` is the IPv6 minimum). Larger packets are split as your operating system would split them, and are lost if any piece is lost. `0` for no limit. |
| `net_forceMTUDrop` | **Not in 0.14.1:** `1` loses every packet larger than `net_forceMTU` instead, like a network that blocks split packets. |
| `net_forceSeed` | Makes the random choices repeat exactly when a connection opens; `0` picks new ones each time. |

For example, on the client:

```cfg
set net_forceLatency 50
set net_forceJitter 30
set net_forceDrop 5
```

`netImpairment` prints the settings and, for each open connection, how many
packets were delayed, lost, duplicated and delivered out of order, the bytes
sent and how many packets the slow line dropped.

A match with three bots needs about 6 KB a second from the server. On a slower
line the game stutters and falls behind; setting `net_clientMaxRate` a little
below the line's speed before you connect keeps it smooth, with fewer updates a
second.

These settings count as cheats and are for testing only. Hosting or joining a
server that does not allow cheats (`net_allowCheats 1` or `sv_cheats 1`) resets
them to `0`, and they cannot be changed during such a match.
