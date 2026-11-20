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
while it serves the other players. Pick a map on the left; the settings on the
right are in four tabs (switch with Q and E, or a controller's shoulder
buttons). Before the 20 November 2026 development build the page showed a few
of them and kept the rest behind **Advanced Server Options**.

- **Match** starts with the **Gametype**, then its limits, **Max Players** and
  **Min Players**, **Fill With Bots** and **Bot Skill** (see [Bots](#bots)),
  **Warmup**, **Ready Up**, and what follows a match: **Map Rotation**
  (**Same Map**, **In Order**, **Shuffled** or your **Map Cycle Script**) and
  **Map Requests**, which lets players queue a map with `mymap`. See
  [Maps, rotation and the map queue](#maps-rotation-and-the-map-queue). **Not in 0.14.1**
- **Rules** holds how the match plays: the **Ruleset** and its grappling hook,
  the team settings (**Auto Balance**, **Shuffle Teams**, **Team Damage** and
  **Timeouts per Team**, which dim outside team modes), **Weapon Stay**,
  **Buying**, the **Overtime Period (Seconds)**, the **Mercy Lead**, **Item
  Timers** (who sees respawn timers), **Highlight Limit** and **Allow Forced
  Models** (what players may show each other), **Allow Spectators** and the
  **Spectator Lock**. **Not in 0.14.1**
- **Modifiers** (**Not in 0.14.1**) changes how any game type plays:
  a **Preset**, **Instagib**, **Quad Hog**, **Vampiric Damage**, **Weapons
  Frenzy**, a **Weapon Arena**, the **Powerups** switch, weapon respawn and
  the self-damage, knockback and fall-damage scales. See
  [Modifiers and presets](#modifiers-and-presets).
- **Server** holds the server's name, who can join and how it runs, below.
- **Buying** (**Not in 0.14.1**), on the **Rules** tab, replaces the weapon,
  armour and ammunition pickups with a buy menu: **Off**, **Quake 4**'s rules or
  **The Awakening**'s, with its prices, weapon mods and FC Armor Regen. See
  [Buying in multiplayer](buying.md).

On the **Server** tab:

- **Server Type** decides who can join. **Internet** lists the server on the
  master servers and accepts players from anywhere; **LAN** keeps it off the
  master servers and accepts only players on your local network.
- **Dedicated** set to **Yes** hosts without playing, as in the original game.
  When the server starts, the game window closes and the openQ4 console window
  takes its place. Type server commands there, such as `spawnServer mp/q4dm2`,
  `kick` and `quit`, and close the window to stop the server. To play on it
  from the same computer, start openQ4 again and join from the server browser.
- **Idle Timeout** (**Not in 0.14.1**) moves a player who sends no input for
  one, two, five or ten minutes to the spectators (`g_inactivity`).
- The passwords, **Allow Voting**, **Voice Chat**, **Pure Server**, **Max
  Rate** and **View Local Server Ban List** are here too.

## Instagib

**Not in 0.14.1.** Instagib works with any game type: Deathmatch, Team Deathmatch, Capture the
Flag, Duel, Clan Arena, Freeze Tag and the rest, and with the
[Classic ruleset](classic-ruleset.md).

- Everyone spawns with a railgun that never runs out, and the gauntlet. Under
  the Classic ruleset, which swaps the gauntlet for the blaster, you get the
  railgun alone; the grappling hook still works if the server allows it.
- Any hit from another player kills at once and gibs the body. Armour saves
  nothing. In Freeze Tag a hit freezes instead.
- Falls do no damage. Lava, pits and other map hazards still kill.
- The map's weapons, ammo, armour, health and powerups don't spawn, and nobody
  drops a weapon. Flags and other objectives stay.
- Buying is off, and team damage still follows the server's **Team Damage**
  setting.

Turn it on with **Instagib** on Create Server's **Modifiers** tab, or with `si_instagib 1` on a dedicated server. The match name shows
it, for example "Instagib Deathmatch". If you change `si_instagib` during a
match, it takes effect at the next map or restart. Run `rescanSI` to restart
the match with the new setting straight away.

## Modifiers and presets

**Not in 0.14.1.** A modifier changes the rules of whatever game type you are
playing. Turn them on from Create Server's **Modifiers** tab, with the
settings below, or with the `modifier` command on a
running server. The match name shows what is on, for example "Quad Hog
Deathmatch" or "Deathmatch (Rocket Launcher)", and so do the server browser
and its details card.

| Modifier | Setting | How it plays |
|---|---|---|
| [Instagib](#instagib) | `si_instagib 1` | Railguns only, and one hit kills |
| Quad Hog | `si_quadHog 1` | Deathmatch only. One Quad Damage is in play. Whoever takes it is the Quad Hog and keeps it until they die; it then lies where they fell. Only the hog's frags, and the frag on the hog, score. A quad nobody reaches for 20 seconds moves somewhere else. The map's other powerups don't spawn |
| Vampiric Damage | `si_vampiric 1` | Hurting an enemy heals you by half the health you took, up to 200 (100 under the Classic ruleset). The map's health pickups don't spawn, so damage is the only way to heal. `g_vampiricPercent` changes the share |
| Weapons Frenzy | `si_weaponsFrenzy 1` | Weapons and ammo come back in a quarter of their usual time, and the ammo you carry regenerates |
| Weapon Arena | `si_weaponArena <weapon>` | Everyone carries that one weapon, with ammo that never runs out, and the gauntlet. Weapon and ammo pickups don't spawn; health, armour and powerups stay. The weapons are `machinegun`, `shotgun`, `hyperblaster`, `grenadelauncher`, `nailgun`, `rocketlauncher`, `railgun`, `lightninggun`, `dmg` and `napalmgun`; `none` turns it off |

Some combinations cannot both mean something, and the server settles them
the same way every time:

- Instagib switches off the weapon arena, Quad Hog, Vampiric Damage and
  Weapons Frenzy.
- Quad Hog only applies in Deathmatch.
- Weapons Frenzy stands aside for a weapon arena and for Clan Arena, where
  nobody picks weapons up.
- Buying is off under Instagib and in a weapon arena.

These settings adjust the match without being modifiers of their own:

| Setting | What it does |
|---|---|
| `si_powerups` | `0` removes the map's powerups (**Powerups**). Arena CTF keeps its runes |
| `si_selfDamage` | Damage your own weapons do to you, as a percentage: `100` is normal, `0` makes rocket jumps free (**Self Damage**) |
| `si_knockback` | How hard hits push players, as a percentage (**Knockback**) |
| `si_fallDamage` | Falling damage, as a percentage; `0` turns it off (**Fall Damage**) |
| `si_weaponRespawn` | Seconds before a picked-up weapon comes back; `0` keeps each weapon's own time (**Weapon Respawn**) |

The modifiers and `si_powerups` change which pickups are on the map, so a
change restarts the match. The `modifier` and `weaponArena` commands do that
at once; after setting one directly, such as `si_quadHog 1`, run `rescanSI`.
The percentages and the weapon respawn time need no restart: they apply from
the next map, restart or `rescanSI`.

### Presets

A preset sets a game type, the ruleset, modifiers and limits together. Pick
one with **Preset** on the **Modifiers** tab, or on a running or dedicated
server with `preset <name>`, which applies it and restarts the match.
`listPresets` shows them all.

| Preset | What it sets |
|---|---|
| `standard` | Quake 4's rules: every modifier off and every percentage at 100 |
| `instagib` | Instagib over the current game type |
| `quadhog` | Deathmatch with Quad Hog |
| `vampiric` | Vampiric Damage over the current game type |
| `frenzy` | Weapons Frenzy over the current game type |
| `rocketarena` | Rocket launchers only, no self or fall damage, no powerups |
| `railarena` | Railguns only with normal damage, no powerups |
| `nadefest` | Grenade launchers only, half self damage |
| `classic` | The [Classic ruleset](classic-ruleset.md) over the current game type |
| `instactf` | Capture the Flag with Instagib, first to 8 captures |
| `classicctf` | Capture the Flag under the Classic ruleset with the grappling hook, first to 8 captures |

Every preset starts from `standard`, so choosing one clears whatever the last
one set. `si_preset` shows the preset in force. If you then change one of its
settings by hand, the server clears `si_preset` at the next map or restart and
the menu shows **Custom**; changing a limit such as the frag limit does not.

To add your own, put a text file in a `presets` folder inside `baseoq4`, for
example `baseoq4/presets/friday.cfg`:

```
// Friday night
si_gameType "Team DM"
si_vampiric 1
si_weaponArena lightninggun
si_fragLimit 60
```

Then run `preset friday`. The file name is the preset's name: lower-case
letters, digits, `-` and `_`. A preset file is read as a list of settings and
is never run as a script. It may set the game type, the ruleset and grappling
hook, the modifiers and settings on this page, the limits (`si_fragLimit`,
`si_timeLimit`, `si_captureLimit`, `si_roundLimit`, `si_roundTimeLimit`,
`si_scoreLimit`, `si_tourneyLimit`, `si_controlTime`, `si_overtime`,
`si_mercyLimit`) and `si_teamDamage`, `si_weaponStay`, `si_warmupWeapons`,
`si_isBuyingEnabled` and `si_buyingRules`. A line that names anything else
makes the server refuse the whole preset and change nothing.

Presets and the `modifier` command are refused during a
[managed competitive match](competitive-matches.md), which owns its rules.

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
| `si_ruleset` | `1` plays the [Classic ruleset](classic-ruleset.md), inspired by Quake 2; `0` is Quake 4's (**Ruleset**). It takes effect when the map next loads or restarts (`serverMapRestart`). **Not in 0.14.1** |
| `si_classicGrapple` | Under the Classic ruleset, where the grappling hook works: `0` nowhere, `1` flag modes (default), `2` every mode (**Grappling Hook**). **Not in 0.14.1** |
| `si_warmup` | Whether matches start with a warmup |
| `si_isBuyingEnabled`, `si_buyingRules` | Buying on (`1`) and its rules: `0` Quake 4, `1` The Awakening (**Not in 0.14.1**; see [Buying](buying.md)) |
| `si_instagib` | `1` plays any game type as [Instagib](#instagib): railguns only, and one hit kills (**Not in 0.14.1**) |
| `si_quadHog`, `si_vampiric`, `si_weaponsFrenzy`, `si_weaponArena`, `si_powerups` | The other [modifiers](#modifiers-and-presets) (**Not in 0.14.1**) |
| `si_selfDamage`, `si_knockback`, `si_fallDamage`, `si_weaponRespawn` | Damage and push percentages and the weapon respawn time; see [Modifiers and presets](#modifiers-and-presets) (**Not in 0.14.1**) |
| `si_preset` | The [preset](#presets) in force, set by the `preset` command (**Not in 0.14.1**) |
| `si_usePass`, `g_password` | `si_usePass 1` admits only players who give the password set in `g_password` |
| `si_mapCycle` | The rotation's maps, in order, separated by semicolons; see [Maps, rotation and the map queue](#maps-rotation-and-the-map-queue) |
| `g_mapRotation` | What follows a match: `0` the same map, `1` the rotation (default), `2` a shuffled rotation, `3` the `g_mapCycle` script (**Not in 0.14.1**) |
| `g_mapQueue` | `1` lets players queue maps with `mymap` (default); `0` turns the queue off (**Not in 0.14.1**) |
| `g_mapPick` | `1` offers three maps to pick from when a match ends (**Not in 0.14.1**) |
| `g_mapDatabase` | The file that adjusts the map database, `mapdb.txt` by default (**Not in 0.14.1**) |
| `g_mapCycle` | Map cycle script. In 0.14.1 it runs whenever `si_mapCycle` is empty; after 0.14.1 only with `g_mapRotation 3` |
| `si_allowVoting`, `si_voteFlags` | `si_allowVoting 1` lets players call votes; `si_voteFlags` is a bit mask of the votes you forbid (type `si_voteFlags` for the bits) |
| `g_votePercent` | Percentage of the players eligible when a vote starts that must vote yes for it to pass (default 51, a plain majority). **Not in 0.14.1** |
| `g_voteLimit` | Most votes one player may call on a map; `0` for no limit. **Not in 0.14.1** |
| `si_spectatorVote` | `1` lets spectators call and cast votes. A player who is out for the round or on the scoreboard is not a spectator and can always vote. **Not in 0.14.1** |
| `si_timeouts`, `si_timeoutLength` | Team timeouts each side may call in a team match (`0` for none) and how long one lasts, in seconds. See [Timeouts on casual servers](competitive-matches.md#timeouts-on-casual-servers). **Not in 0.14.1** |
| `si_spectatorLock` | During live play, `1` keeps spectators, and anyone who connects, from joining the game until it ends and `2` keeps players from leaving it too. **Not in 0.14.1** |
| `g_inactivity` | Seconds a playing human may send no input before being moved to spectators (or dropped when `si_spectators` is off); `0` turns it off. **Not in 0.14.1** |
| `g_pauseOnDisconnect` | Seconds a live team match pauses when a playing human loses their connection, so they can reconnect; `0` turns it off. One pause a side a match; it ends when the player is back, and a kick or an idle drop does not start one. **Not in 0.14.1** |
| `g_precachePlayerModels` | `1` (default) loads every player model with a multiplayer map, so a player or bot arriving with a model nobody has used yet does not hitch the match; `0` loads only your own and the defaults, for a machine short of memory. Set on each machine; it is not a server rule. **Not in 0.14.1** |

Bot settings are listed under [Bots](#bots).

### What players may see and show

**Not in 0.14.1.** `si_itemTimers` chooses who sees item respawn timers (`0` nobody, `1` spectators, `2` everyone),
`si_maxBrightskin` caps brightskins, rimlights and outlines, and `si_forcedModels 0` turns forced player models off.
On **Create Server → Rules** they are **Item Timers**, **Highlight Limit** (Off, Low, Medium or High, which is
no limit) and **Allow Forced Models**.
See [Multiplayer Client Options](multiplayer-client-options.md#for-server-operators).

## Maps, rotation and the map queue

**Not in 0.14.1.** openQ4 keeps one list of the multiplayer maps your server
can play: the map database. It finds every installed map by itself, so a map
pack you add needs no listing. The rotation, map votes, the in-game Vote and
Admin pages, the map queue and the pick at the end of a match all read it.

### What follows a match

`g_mapRotation` decides what the server loads when a match ends:

| Value | What happens |
|---|---|
| `0` | The same map again |
| `1` | The next map of the rotation that suits the game type and the number of players (default) |
| `2` | A random map of the rotation that suits them, avoiding the maps played most recently |
| `3` | Quake 4's map cycle script, named by `g_mapCycle` |

Before this, a server with no `si_mapCycle` list ran the map cycle script. If
you depend on your own script, set `g_mapRotation 3`.

A map a player queued comes first, then the map players picked at the end of
the match, then the rotation. `serverNextMap` follows the same order.

### The rotation's order

The server takes the first of these that you have set:

1. `si_mapCycle`, a list such as `"mp/q4dm1;mp/q4dm3;mp/q4ctf1"`.
2. The `rotation` block of `mapdb.txt` (below).
3. Every installed map, in the order `mapdb list` prints.

Maps that cannot play the current game type are passed over. A listed map
that is not installed is left out with a warning, and a map listed twice
takes one place.

### Player bounds

Each map can have a smallest and a largest number of players. The rotation
passes over a map the server is too empty or too full for. Everyone connected
counts, bots included. If no map fits, the rotation carries on in order
rather than stop.

Without anything from you, the tourney arenas (and the `v1` duel cut of a
larger map) take two to four players, and a map with no free-for-all mode,
such as a Capture the Flag map, needs four. Every other map has no bounds. A
map's author can set `minPlayers` and `maxPlayers` in its `mapDef`; your
`mapdb.txt` overrides both.

### mapdb.txt

Put a text file named `mapdb.txt` in the server's `baseoq4` folder to change
what the database holds. `g_mapDatabase` names a different file. For example:

```text
defaults { players 2 12 }

map mp/q4dm1 {
	players 2 8
	set si_fragLimit 30
}
map mp/q4dm11 {
	modes "DM, Team DM"
	vote 0
}
map mp/q4tourney1 { queue 0 rotation 0 }

rotation { mp/q4dm1 mp/q4dm2 mp/q4dm3 mp/q4ctf1 }
```

| Statement | Where | What it does |
|---|---|---|
| `players <least> <most>` | `map`, `defaults` | Player bounds, `0` for no bound |
| `rotation 0` or `1` | `map`, `defaults` | Whether the map takes part when the rotation is every installed map |
| `vote 0` or `1` | `map`, `defaults` | Whether players may call a vote for the map, and whether the pick offers it |
| `queue 0` or `1` | `map`, `defaults` | Whether players may queue the map |
| `modes "<game types>"` | `map` | The game types the map plays, replacing what the map itself says. Use `si_gameType` names, separated by commas |
| `name "<text>"` | `map` | The name shown in the database's own messages |
| `set <setting> <value>` | `map` | A setting applied while this map is played (below) |
| `rotation { ... }` | file | The rotation's maps, in order |

Statements apply in the order you write them, and `defaults` reaches every
installed map, so put it first. `//` starts a comment.

The server reads the file when it starts and at every change of map, so an
edit takes effect at the next map. `mapdb check` reads it at once and reports
without changing anything; `mapdb reload` applies it now. If the file has a
mistake, the console names the line and the server ignores the whole file
until you fix it.

### Settings for one map

A `set` line gives a map its own value for a setting. The server applies it
just before the map loads and puts the old value back when the map ends, so
it never leaks into the next map. If you change the setting by hand while the
map is running, your value stays. Quitting on such a map puts the old values
back before the configuration is saved.

Save `mapdb.txt` and preset files as plain text or UTF-8; a byte order mark,
which some Windows editors add, is ignored.

A map can set game rules: `si_` and `g_` settings such as `si_fragLimit`,
`si_timeLimit` or `si_instagib`. It cannot change the map, the game type, the
rotation, `si_pure`, `si_maxPlayers`, the server's name or any password, and
it cannot set read-only or cheat settings. `mapdb settings` lists each map's
settings and says which ones are refused.

### The map queue

Any player can ask for a map with `mymap <map>`, for example `mymap q4dm3`.
The server plays queued maps in the order they were asked for, before the
rotation continues. Each player has one place in the queue: asking again
replaces their map and keeps the place. `mymap` on its own shows the queue
and `mymap clear` gives the place up. A player who leaves takes their map
out of the queue.

The map must suit the current game type, and it cannot be the map being
played or one another player has already queued. Queued maps ignore the
player bounds: a player asked for them. `g_mapQueue 0` turns the queue off.

### Picking the next map

With `g_mapPick 1`, the server offers three maps in chat when a match ends.
Players choose with `mappick 1`, `mappick 2` or `mappick 3` while the
scoreboard is up, and can change their mind. The most picked map is next; a
tie is settled at random between the leaders, and if nobody picks, the first
map, which is the rotation's own, is played. Give players time with
`g_gameReviewPause` (seconds).

The pick is not offered when a map is queued, in Tourney, or when fewer than
two maps can be offered.

Managed matches and series choose their maps and set their own rules; see
the [competitive matches guide](competitive-matches.md). While one runs, the
queue and the pick are off and a map's `set` lines are not applied.

## Console commands

| Command | What it does |
|---|---|
| `spawnServer` | Starts the server |
| `disconnect` | Shuts the server down |
| `serverMapRestart` | Restarts the current map |
| `serverNextMap` | Moves to the next map |
| `mapdb` | Lists the map database: `mapdb list`, `rotation`, `queue`, `next`, `settings`, `check`, `reload` (**Not in 0.14.1**) |
| `mymap` | Queues a map for the next change, for the player who types it: `mymap <map>`, `mymap`, `mymap clear` (**Not in 0.14.1**) |
| `mappick` | Chooses a map of the end-of-match pick: `mappick <1-3>` (**Not in 0.14.1**) |
| `kick` | Kicks a player by slot number |
| `gameKick` | Kicks a player by name |
| `addbot` | Adds one bot, optionally by name and skill |
| `removebot` | Removes one bot, optionally by name |
| `kickbots` | Removes every bot |
| `botlist` | Lists the bots and their characters |
| `callvote <vote> [value]`, `cv` | Calls a vote; `vote yes` or `vote no` casts yours. The votes are listed under [Voting on casual servers](competitive-matches.md#voting-on-casual-servers). **Not in 0.14.1** |
| `timeout [marine\|strogg]` | Calls a team timeout; players call one for their own side, the dedicated console names the side. **Not in 0.14.1** |
| `timein`, `unpause` | Ends the running timeout or pause early. **Not in 0.14.1** |
| `pause` | Pauses the match with no timeout charged (host, referee or dedicated console); `timein` resumes. **Not in 0.14.1** |
| `modifier <name> <0\|1>` | Turns `instagib`, `quadhog`, `vampiric` or `frenzy` on or off and restarts the match; alone, it lists them (**Not in 0.14.1**) |
| `weaponArena <weapon\|none>` | Starts or ends a [weapon arena](#modifiers-and-presets) and restarts the match (**Not in 0.14.1**) |
| `preset <name>` | Applies a [preset](#presets) and restarts the match (**Not in 0.14.1**) |
| `listPresets` | Lists the built-in presets and your own (**Not in 0.14.1**) |

## Bots

openQ4's bots play any multiplayer map with no per-map setup, so your server
stays busy when it is quiet. Set **Fill With Bots** (`bot_minPlayers`) to the
number of players you want and the server adds bots to reach it, freeing a
slot each time a real player joins. Both bot settings are on Create Server's
**Match** tab, beside the player counts; **Bot Skill** uses the Arena
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
| `bot_grapple` | `1` (default) lets bots use the [Classic ruleset](classic-ruleset.md)'s grappling hook wherever the server allows it; `0` keeps them off it. **Not in 0.14.1** |

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
