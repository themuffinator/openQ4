# Server setup guide

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

This guide is for anyone hosting openQ4 multiplayer games, either from the
game's menus while you play or as a dedicated server that runs on its own.
Multiplayer is **experimental**: expect rough edges and changes between
versions.

Everyone on a server must run openQ4, and the same version as the server.
Retail Quake 4 1.4.2 clients and servers (network protocol 2.85) cannot join
openQ4 games, and openQ4 cannot join theirs; openQ4 refuses them with an
explanation instead of leaving anyone waiting. See
[Retail Quake 4 clients and servers](server-security.md#retail-quake-4-clients-and-servers).

## Hosting from the game

Open **Multiplayer → Create Server** to host a listen server: you play on it
while it serves the other players.

- **Server Type** decides who can join. **Internet** lists the server on the
  master servers and accepts players from anywhere; **LAN** keeps it off the
  master servers and accepts only players on your local network.
- **Dedicated** set to **Yes** hosts without playing, as in the original game.
  When the server starts, the game window closes and the openQ4 console window
  takes its place. Type server commands there, such as `spawnServer mp/q4dm2`,
  `kick` and `quit`, and close the window to stop the server. To play on it
  from the same computer, start openQ4 again and join from the server browser.
- **Advanced Server Options** holds the match rules and bots. In the new
  interface its **Match** tab has **Fill With Bots**, **Bot Skill** and
  **Shuffle Teams**, and its **Rules** tab has **Overtime**, **Mercy Lead** and
  **Round Limit**. See [Bots](#bots).

## Running a dedicated server

### What you need

- A complete openQ4 package and your original Quake 4 game files (`q4base`).
  The server uses the same game files and game modules as the client.
- A 64-bit machine that matches the package (x64 for most downloads).
- The dedicated server program, `openQ4-ded_x64` (`openQ4-ded_<arch>` on other
  architectures). It needs no graphics card or desktop session.

For light use, a modern 64-bit machine with 2 GB of memory free for the system
and server is enough, plus the disk space listed in the
[Getting Started system requirements](getting-started.md#system-requirements).
For a public server, prefer 4 GB or more, a wired connection and enough upload
bandwidth for the player count you advertise.

Of openQ4's own content packs, a dedicated server needs only
`baseoq4/pak0.pk4`. A server-only install can delete `pak1.pk4` (light grids)
and `pak2.pk4` (loading screens) to save about 800 MB; the server log then
notes that they are missing. Players keep using their own copies on your
server, pure or not, because those packs never enter a server's pure list.

### Start the server

1. Make sure openQ4 can find your Quake 4 game files.
2. Start the dedicated server with a name, map and game type, ending with
   `+spawnServer`:

   ```text
   openQ4-ded_x64 +set net_ip 0.0.0.0 +set net_port 28004 +set si_name "My openQ4 Server" +set si_map mp/q4dm1 +set si_gameType DM +spawnServer
   ```

3. Open the UDP port in your firewall and router; see
   [Firewall and router](#firewall-and-router).

On a computer with a display you can also start the normal game as a
dedicated server, as you could the original game. No game window opens and
the console window runs the server:

```text
openQ4-client_x64 +set net_serverDedicated 1 +set si_map mp/q4dm1 +set si_gameType DM +spawnServer
```

To keep your settings together, put them in a config file in `baseoq4/` and
load it with `+exec yourserver.cfg` before `+spawnServer`. For remote
administration, follow the [server and remote-console security
guide](server-security.md). Never put a remote-console (rcon) password in
launch arguments.

## IPv4 binding and ports

Set the network interface and UDP port before the server starts. These are
console variables (cvars), set with `+set` on the command line or in your
config file.

| Setting | What it controls |
|---|---|
| `net_ip` | The local network address to listen on. The default, `localhost`, listens on every local interface. `0.0.0.0` says the same explicitly; a local address such as `192.168.1.50` listens on that adapter only. |
| `net_port` | The server's UDP port. `0`, the default, tries the default server ports, `28004` to `28007`; `1` to `65535` uses that exact port. |

`net_ip` is an address on the server machine, not the public address your
router reports. Do not put a public internet address here unless that address
really belongs to one of the server's network adapters. If the address you set
cannot be found or used, the server stops with an error instead of quietly
listening everywhere; correct it or use `0.0.0.0`.

With `net_port 0`, check the server console or log for the port it picked
before you set up your firewall or router.

## IPv6

IPv6 is on by default. The server listens for IPv6 on the **same** port as
IPv4, so you publish a single port number and players of either kind reach it
there.

| Setting | What it controls |
|---|---|
| `net_ip6` | The local IPv6 address to listen on. Empty, the default, listens on every interface. |
| `net_enableIPv4` | `1` listens for IPv4, `0` does not. |
| `net_enableIPv6` | `1` listens for IPv6, `0` does not. |
| `net_mcast6addr` | The IPv6 group that answers a LAN scan. The default `ff02::1` reaches every machine on the local link. |
| `net_mcast6iface` | The network interface number used for the IPv6 LAN scan. `0`, the default, scans every link. |

IPv4 stays the baseline. With both enabled, the server refuses to start if it
cannot open the IPv4 port, rather than run a server that IPv4 players cannot
reach. If IPv6 fails, the server carries on with IPv4 only, so a host without
IPv6 works as before. For an IPv6-only server, set `net_enableIPv4 0`; an IPv6
failure then stops startup instead.

Older configs that put an IPv6 address in `net_ip` still work: openQ4 treats
it as the IPv6 address and listens on IPv6 only. Use `net_ip6` for new
configs.

Enter `netIPv6SelfTest` in the server console to check IPv6 before you open
firewall rules. It prints `IPv6 network self-test: passed`, or says it skipped
the network checks when the host has no IPv6.

## Firewall and router

For dependable hosting, use direct IPv4 UDP with the firewall and router rules
below. openQ4 does not route games through a proxy: the legacy `net_socks*` CVars remain visible for configuration compatibility, but openQ4 does not enable or support SOCKS UDP relay, and setting them does not route multiplayer traffic through a SOCKS proxy.

- Allow inbound UDP on the server's `net_port` in the host's firewall. Games
  need no inbound TCP port.
- Behind a router, forward the same external UDP port to the server's local
  IPv4 address and port. Give the server a fixed local address (a DHCP
  reservation or static address) so the rule keeps working.
- For IPv6, allow inbound UDP on the same port for the server's IPv6 address.
  IPv6 usually needs no port forward, only that firewall rule. Publish the
  server's global IPv6 address, not an `fe80::` address, which only works on
  the same local network.
- Test from outside your own network. Some routers cannot connect back to
  their own public address.
- If your provider puts you behind its own address sharing (carrier-grade
  NAT), players may not reach you even with the router set up correctly. Ask
  the provider for a public IPv4 address or a port-forwarding service.

## How players connect

Players on your local network use the server's local address; internet
players use your public address or a host name. Add the port when the server
is not on `28004`. Without a port, `28004` is used, and `:0` means the same.

```text
connect 192.168.1.50:28004
connect play.example.net:28004
connect [2001:db8::1]:28004
connect [fe80::1%12]:28004
```

Put an IPv6 address in square brackets when a port follows it. A local
`fe80::` address also needs its interface number after `%`. A host name with
both IPv4 and IPv6 addresses connects over IPv4; use the bracketed IPv6
address to force IPv6.

## Server settings

| Setting | What it controls |
|---|---|
| `si_name` | Server name shown to players |
| `si_motd` | A message players read on the loading screen. Up to 256 bytes, wrapped to three lines; colour codes are dropped |
| `si_map` | Starting map |
| `si_gameType` | Game type. In the console, type `si_gameType ` and press Tab to list them |
| `si_maxPlayers` | Most players allowed (default 12, up to 16) |
| `si_fragLimit` | Frag limit |
| `si_timeLimit` | Time limit in minutes |
| `si_captureLimit` | Score limit for Capture the Flag |
| `si_roundLimit` | Rounds a team must win in a round-based match (**Round Limit**) |
| `si_mercyLimit` | End a team match early once one team leads by this many points; `0` turns it off (**Mercy Lead**) |
| `si_overtime` | Length of each overtime period in seconds; `0` uses Quake 4's sudden death (**Overtime**) |
| `si_warmup` | Whether matches start with a warmup |
| `si_usePass`, `g_password` | `si_usePass 1` admits only players who give the password set in `g_password` |
| `g_mapCycle` | Map cycle script |

Bot settings are listed under [Bots](#bots).

## Console commands

| Command | What it does |
|---|---|
| `spawnServer` | Starts the server |
| `disconnect` | Shuts the server down |
| `serverMapRestart` | Restarts the current map |
| `serverNextMap` | Moves to the next map |
| `kick` | Kicks a player by slot number |
| `gameKick` | Kicks a player by name |
| `addbot` | Adds one bot, optionally by name and skill |
| `removebot` | Removes one bot, optionally by name |
| `kickbots` | Removes every bot |
| `botlist` | Lists the bots and their characters |

## Bots

openQ4's bots play any multiplayer map with no per-map setup, so your server
stays busy when it is quiet. Set **Fill With Bots** (`bot_minPlayers`) to the
number of players you want and the server adds bots to reach it, freeing a
slot each time a real player joins. Both bot settings are on
**Create Server → Advanced Server Options**; **Bot Skill** uses the Arena
campaign's names: Training, Rookie, Veteran, Nightmare and Impossible.

Bots run entirely on the server. Players need nothing extra and see them as
ordinary players. Each bot has a name, a play style and its own chat lines. It
answers common phrases from people or other bots, and addressing a bot by name
makes it the one that replies. Team chat stays inside the team, and bots never
set off reply chains. A bot waits a moment before each message, longer for
longer lines, and Quake 4's typing icon shows above it until it sends.

| Setting | What it controls |
|---|---|
| `bot_minPlayers` | Keep the match topped up to this many players with bots (`0` turns bots off) |
| `bot_skill` | Bot difficulty, 1 (harmless) to 5 (unpleasant); default 3 |
| `bot_skillVariance` | Spread bot skill this many levels either side of `bot_skill`, so a match is not all one difficulty |
| `bot_characters` | `1` gives bots named characters with their own play style and voice; `0` uses anonymous bots on the plain skill curve |
| `bot_chat` | Bot chat: `0` silent, `1` normal, `2` chatty |
| `bot_chatCPM` | Bot typing speed in visible characters per minute (default `900`) |

Bot settings are saved with your configuration, so a value you pick in the
menu or set once in your server config is still there next time. For the full
command list, the character file format and how to add your own characters,
see [Multiplayer bots](../dev/mp-bots.md).

### Giving bots orders

In a team game you can tell the bots on your side what to do in team chat.
Start with a bot's name, or `everyone` for all of them, and follow it with the
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

## Competitive matches (preview)

Managed competitive matches, with server-run ready checks, tactical timeouts,
match records and automatic multi-view recording, are still in development.
They are not ready for tournaments, and several captain, referee, series and
spectator screens are unfinished. Casual servers are unaffected unless you
choose a competitive profile. See [Competitive matches](competitive-matches.md)
for what works today.

## More information

- [Server and remote-console security](server-security.md): rcon passwords,
  the in-match Admin page, pure servers and mods.
- [Multiplayer networking](multiplayer-networking.md): connection behaviour,
  prediction and lag compensation.
- openQ4 runs its own engine and game modules, so mods built for the original
  Quake 4 game DLLs do not load.
- [TECHNICAL.md](../../TECHNICAL.md): advanced configuration, file layout and
  search paths.
