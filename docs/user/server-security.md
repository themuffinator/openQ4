# Server and remote-console security

> This guide matches openQ4 0.15.0. Anything new in 0.15.0 is marked **New in 0.15.0**. See [release status](release-status.md) for what each download includes.

> Part of the [multiplayer guide](multiplayer.md), which covers joining, game types and match rules and lists every multiplayer guide.

This guide is for server hosts. It covers administering your server remotely
and safely, how pure servers and mods behave, and what openQ4 refuses to
accept. For starting and configuring a server, see the
[server setup guide](server-setup.md).

openQ4's remote console (rcon) checks your password with its own method,
`rcon2`, by default. Quake 4's original rcon sent the password over the
network as plain text; `rcon2` proves you know it without sending it. It
changes nothing about gameplay or the Quake 4 game files.

## Configure a strong password

Set the same password on the server and on the administrator's game:

```text
net_serverRemoteConsolePassword "a-long-random-password"
net_clientRemoteConsolePassword "a-long-random-password"
```

Then send commands from your game's console as usual:

```text
rcon status
```

The password must be at least 12 bytes long. For a server on the internet, use a unique, randomly generated password of 24 or more characters, and never reuse a login, referee, database or service password.

Type the password at a trusted local console, or put the server's setting in a
configuration file that only the account running the server can read. Do not
commit that file to version control, ship it in a package, or put the password
on the command line. In particular, never launch with `+set net_serverRemoteConsolePassword ...` or `+set net_clientRemoteConsolePassword ...`: other
users on the machine, process monitors, crash reports and service logs can see
launch arguments.

openQ4 treats both password settings as private. It leaves them out of console
listings and queries, settings lists it builds or sends, command history,
completion, start-up reports and event logs, and does not substitute them into
other commands. That
guards against accidental leaks; it does not make an unprotected config file
safe.

## The in-match Admin page

The multiplayer menu's **Admin** page asks for the same password, checks it
with the server and then opens its player, server and console controls; a
wrong password returns you to **Join Team**. Type the password into the page's
field, or set `net_clientRemoteConsolePassword` at the console first. The
field takes 16 characters, so for a longer password set the console variable
first, then open **Admin** and choose **Accept** without typing in the field.

## Bans, mutes and chat limits

> **After 0.15.0.** This section describes the development build. 0.15.0 does
> not have these commands; see [release status](release-status.md).

In Quake 4, and in openQ4 up to 0.15.0, a ban never kept anyone out: the ban
list stored an ID that the game never sends, and the Admin page's **Ban**
button only kicked. Bans now work, by network address.

Run these on the server: at a dedicated server's console, in the host's console
on a server started from the game, or from your own game with `rcon` in front
(`rcon ban 3 2h spamming`). Typed on an ordinary player's console they do
nothing, unless that player has logged in as an [admin](#admins).

| Command | What it does |
|---|---|
| `status` | Lists who is connected: slot, ping, whether they are playing, whether they are muted, their address and their name |
| `ban <slot\|name\|address[/prefix]> [time] [reason]` | Bans an address or a range of addresses and removes every player in it |
| `unban <row\|address[/prefix]>` | Lifts a ban, by its row in `banlist` or by its address |
| `banlist` | Lists the bans with the time each has left; `banlist reload` reads the file again |
| `ban id:<identity> [time] [reason]`, `unban id:<identity>` | Bans or lifts a player identity (after 0.15.0) |
| `mute <slot\|name> [time]` | Stops a player's text chat and voice-menu lines reaching anyone |
| `unmute <slot\|name>` | Lifts a mute |
| `kick <slot>`, `gameKick <slot\|name>` | Removes a player without banning them |
| `serverShutdown <match\|now\|time\|cancel> [reason]` | Stops the server after telling the players; see [Stopping the server](#stopping-the-server) |

The Admin page's **Ban** button now bans the chosen player's address, and the
ban lists in the menus show addresses instead of IDs.

**Identity bans (after 0.15.0).** A player whose game proved an identity to
your server (see [Your identity](multiplayer.md#your-identity-and-coming-back-after-a-dropped-connection))
is banned by it as well: `ban`, the Admin page's **Ban** and an admin's ban all
add a second row, `id:<identity>`, beside the address range, so neither a new
address nor a fresh copy of the game on another network brings them back
while they keep their identity. `status` shows each player's identity, and
`ban id:3f9a0c1d22e4b781 1w cheating` bans one whether or not that player is
connected. An identity ban is applied when the player proves the identity, a
moment after they join. `unban id:<identity>` lifts one. A player can always
make a new identity (`identityReset`), so an identity ban is a strong hint,
not a lock; the address ban beside it still holds.

**What identities tell a server, and what it keeps.** A server learns only the
key a player's game made for that server. It cannot work out the player's key
on any other server, nor anything about their computer. Your server keeps
identities in four places, all in its save folder: `banlist.txt` (identity
bans), `ranking.txt` with `g_ranking 1` (an identity, the last name seen with
it and a rating; no addresses), the admin log (identity bans and their
removal) and, for five minutes after a player leaves, its memory of their
team and score. Nothing is sent anywhere else. A player who wants a fresh
start runs `identityReset confirm`; their old rows then belong to nobody, and
you can delete them. Under **View Ban List**,
**Ban Length** (10 minutes, an hour, a day, a week or permanent, the default)
and **Ban Reason** (none, cheating, abusive chat, spam, team killing or
griefing) set what the next **Ban** records. The Admin page's ban list shows
each ban's reason and the time it has left. On a server running 0.15.0 the
button still only kicks.

**Naming the target.** `ban` reads its first word as a slot number if it is
all digits, then as an address, then as a name. Banning a connected player by
slot or name bans the address they hold; over IPv6 that is their `/64` (see
**Ranges**), so a player cannot return by taking another address in it.

**Times** are a number and a unit: `45s`, `30m`, `2h`, `7d`, `1w`. A number
alone is minutes. `perm` never ends, and a ban without a time is permanent. A
mute without a time lasts until you lift it or the server stops.

**Ranges.** Add a prefix length to ban a whole block: `ban 203.0.113.0/24
perm ban evasion`. An IPv6 address can be written with or without square
brackets; it needs them only when a port follows it. A home connection usually
holds a whole IPv6 `/64`, so ban that rather than one IPv6 address:
`ban 2001:db8:12:34::/64`. To guard against
a typo that would turn away much of the Internet, a range can be no wider than
`/8` for IPv4 or `/16` for IPv6.

The commands read the line as you typed it, so an address, a time and a
reason arrive whole (`ban 4 3rd offence` is a permanent ban with that reason),
at the console and through `rcon` alike.

**What a ban can and cannot do.** A ban follows the address, not the person.
Someone whose address changes (a VPN, a new lease from their provider) can
come back, and banning a shared address (a household, a campus, some mobile
networks) turns away everyone behind it. Bans tied to the player rather than
the address are planned.

**The ban list** is `banlist.txt` in the server's save folder (next to its
configuration, under `baseoq4`), a text file with one ban a line. You can edit
it while the server is stopped, or edit it and type `banlist reload`; a ban
the file gained removes the connected players it covers. The list
holds up to 512 bans, and bans that have run out are removed for you. If the
folder holds a ban list from Quake 4, openQ4 keeps its entries as comment
lines the first time it reads it; they never banned anyone.

The server never writes over a list it could not use. If the file is too
large (over 512 KB), holds more than 512 bans, or is not text the server can
read (saved as UTF-16, for example), the console says what is wrong, the file
is left exactly as it is, and the bans already in memory stay in force. Bans
you make meanwhile hold until the server stops but are not saved. Correct the
file and type `banlist reload`.

**Mutes.** A muted player is told they are muted and can still play. The mute
follows their address (their `/64` over IPv6), so leaving and joining again
does not lift it. It covers text chat and the voice-menu lines, and it holds
the player to the name and clan tag they had when they were muted, so neither
can carry a message. A muted player can still ask for a map, but the request
is not announced. Microphone voice is separate: each player can silence
another player's voice for themselves.

Players can also vote to mute someone with `callvote mute <name>` where
voting is on (`si_allowVoting 1`). A passed vote mutes for
`g_voteMuteMinutes` (default `10`; `0` lasts until you lift it). To forbid
the vote, add 1048576 to `si_voteFlags`; forbidding kick votes (16) leaves
it available.

**Chat limits.** A player may send four chat lines in any four seconds. One
more silences them for ten seconds, and they are told why.

| Setting | What it controls |
|---|---|
| `g_chatFloodMessages` | Chat lines a player may send within `g_chatFloodSeconds` (default `4`); `0` turns the limit off |
| `g_chatFloodSeconds` | The length of that window in seconds (default `4`) |
| `g_chatFloodWait` | Seconds a player who passed the limit stays silenced (default `10`) |
| `g_reservedNames` | Names nobody may take, separated by `;` (default `admin;console;referee`). A player who asks for one gets it with `_` added |
| `net_serverMaxClientsPerAddress` | Most players one address may hold (default `0`, no limit). Players behind one router share an address, so choose a number that still fits a household. Over IPv6 one address means one `/64` |
| `g_voteMuteMinutes` | Minutes a player stays muted after a mute vote passes (default `10`); `0` until you lift it |
| `g_adminLog` | `1` (default) writes the admin log described below; `0` turns it off |
| `g_matchLog` | `1` keeps a record of every match in `logs/match.ndjson` (default `0`) |

**Names.** A player's name can hold at most 32 characters that show, never
control or invisible characters, and is never empty or only digits. Colour
codes are kept; icons are removed. Two players cannot share a name, even with
different colours, and `server` is always reserved. The clan tag follows the
same rules, and a reserved name cannot be used as a clan tag. Look-alike
letters from other alphabets are not detected.

**The admin log.** Every kick, ban, lifted ban, mute, chat-limit silence,
refused connection and shutdown is written to `logs/admin.ndjson` in the
server's save folder, one line of JSON each:

```text
{"time":"2026-11-22T18:04:11Z","event":"ban","by":"console","range":"203.0.113.7/32","name":"Spammer","reason":"spamming","expires":"2026-11-22T20:04:11Z"}
```

`by` says who acted: `console` (the console or rcon), `admin` (the Admin
page, or a logged-in [admin](#admins)), `vote` (a kick or mute vote), `match
control` (a referee or the host removing a player in Match Control), `menu`
(a ban lifted from a menu) or `server` (an automatic action). A line that
came through rcon also carries `rcon`, the address it was sent from. A banned
address that keeps trying is logged once a minute for each ban. The file only
grows, so move or delete it from time to time.
The log holds players' network addresses, so keep it as private as the rest
of the server's files.

**The match log.** Set `g_matchLog 1` and the server also keeps a record of
the matches it runs in `logs/match.ndjson`, in the same folder: who joined and
left, every kill, the scores, flag and powerup events, and the final
standings, one line of JSON each. It is off by default because a busy server
writes a line for every kill. The file only ever grows, so move or delete it
from time to time. Each map begins with a `stream` line that gives the map,
the game type, the players and the UTC time. It holds names and scores, and
no network addresses. The lines are the ones the
[live event stream](competitive-matches.md#feed-a-broadcast-overlay) writes
for broadcast overlays, so the same tools read both.

### Admins

An admin is a player you trust to keep order while you are away. Admins get
their own password, so you never hand out the rcon password, and they can do
far less than rcon can.

**Set it up.** Give the server an admin password before it starts a map, in
its configuration file or on its command line:

```text
set net_serverAdminPassword "a long password only your admins know"
```

The password must be at least 8 bytes long; make it much longer. The server
takes it at once, also while a match runs, and then forgets it, keeping only
what it needs to check a login. It is never written to the configuration
file and never shown. To change it, set it again: everyone who logged in with
the old password is logged out. Without one, nobody can log in as an admin.

**Logging in.** An admin stores the password in their own game, then asks to
log in:

```text
set net_clientAdminPassword "a long password only your admins know"
admin
```

Keep the password in the setting and type `admin` on its own. The setting is
private: it stays out of the console history, the log and the configuration
file, so it has to be set again each time the game is started. The password
itself is never sent. The server sends a one-time challenge and the game
answers with a proof, the same way a referee logs in. A login lasts until the
admin leaves, logs out with `admin logout`, or the host ends it; it carries on
across map changes. Five wrong passwords in a minute lock that connection out
of logging in for a minute. Typing the password after the command
(`admin secret`) does not log in; that line is kept out of the console
history all the same.

**What an admin can do**, from their own console, to players who are
connected:

| Command | What it does |
|---|---|
| `gameKick <slot\|name> [reason]` | Removes the player |
| `mute <slot\|name> [time]`, `unmute <slot\|name>` | Stops, or restores, the player's text chat |
| `ban <slot\|name> [time] [reason]` | Bans the player's address and removes them. The ban lasts at most `g_adminMaxBanMinutes`; asking for longer, or for a permanent ban, gives that limit |

The server answers each command in the chat area. Put a name that has spaces
in quotes. An admin who is watching rather than playing also gets the
referee's controls in Match Control for the current map; an admin who is
playing does not, for the same reason a player cannot log in as a referee. To
referee, move to spectators and type `admin` again.

**What an admin cannot do.** An admin cannot lift a ban, list the bans, see
anyone's address (`status`), ban an address or a range, or stop the server.
An admin cannot act on the host of a server started from the game, on another
admin, or on a bot. Bans and mutes follow an address, so an admin also cannot
ban or mute a player who shares an address with an admin (themselves
included), because it would reach that admin as well. All of that stays with
the console and rcon.

**The host's control.** At the server's console, or through rcon:

| Command | What it does |
|---|---|
| `admin` | Says whether admin logins are on and lists who is logged in |
| `admin revoke <slot\|name\|all>` | Ends an admin's login, or everyone's |
| `admin off` | Ends every login and forgets the password; set a new one to turn logins back on |

| Setting | What it controls |
|---|---|
| `net_serverAdminPassword` | On the server, the password admins log in with; none (the default) turns admin logins off |
| `net_clientAdminPassword` | In an admin's own game, the password `admin` logs in with |
| `g_adminMaxBanMinutes` | The longest ban an admin may give, in minutes (default `1440`, a day); `0` lets admins ban nobody |

Everything an admin does is in the admin log with `"by":"admin"` and the
admin's own slot, name and address (`admin_slot`, `admin_name`,
`admin_address`). Logins, wrong passwords and logouts are logged as
`admin_login`, `admin_login_failed` and `admin_logout`, and `admin off` as
`admin_logins_off`.

### Stopping the server

`serverShutdown` stops the server without cutting players off mid-match:

| Command | What happens |
|---|---|
| `serverShutdown match [reason]` | The server stops when the current match ends, or when anything would change or restart the map (a vote, a command, a new rule set, the next map of a series). With no human player connected it stops at once |
| `serverShutdown 5m [reason]` | The server stops after that time (`90s`, `5m`, `1h`, or a number of minutes) whatever the match is doing. Players are told again at one hour, thirty, ten, five and one minute, and at thirty and ten seconds |
| `serverShutdown now [reason]` | The server says goodbye and stops |
| `serverShutdown cancel` | Calls a waiting shutdown off |
| `serverShutdown` | Shows whether a shutdown is waiting |

Players see the announcement and your reason in chat. When the time comes a
dedicated server's program exits, so a service manager or script can update
and restart it; a server you host from the game returns you to the menu. A
match with no time, frag or score limit never ends, so give such a server a
time instead of `match`.

## Legacy compatibility mode

The old plain-text rcon is off on both sides by default. Turn it on only for an
old administration tool, or a server that understands nothing else, and only
for as long as you need it on a trusted network:

```text
// Server: accept legacy packets.
net_serverAllowLegacyRcon 1

// openQ4 client: send a legacy packet.
net_clientUseLegacyRcon 1
```

An openQ4 client talking to an openQ4 server needs both settings. A
third-party tool needs only the server setting, and an openQ4 client talking
to an old server needs only the client setting. Legacy mode sends the password
and command as plain text. Old plain-text tools get no reply until the server
turns this on. Set both back to `0` when you are done.

## Pure multiplayer and local game modules

A pure server (`si_pure 1`) makes every player use the same game packs (the
`.pk4` archives that hold Quake 4's and openQ4's content) as the server, in the
same order. openQ4 supports it and keeps it on once you select it. The server
can tell a player which packs are missing, but standard openQ4 packages cannot
download packs in the game; get any pack you need from a source you trust.
Pack downloads never include game code.

**Game code check.** The pure handshake also checks the game code. openQ4
sends a fixed value, `0x68fb90b1`, the checksum of the official Quake 4 1.4.2
`q4base/game300.pk4`, without using, downloading or opening that archive. The
server accepts it only when the player's `game_sp` or `game_mp` module came
from openQ4's own package folders. A missing module, an unsupported platform,
an empty pack list or a different value refuses the connection rather than
weakening pure mode or downloading code. The value is the same on Windows,
Linux and macOS and only works between openQ4 builds; retail Quake 4 servers
expect a different one. It is not a cryptographic check of the module and does
not make pure mode an anti-cheat system.

**Mods.** By default, `net_serverAllowServerMod 0` stops a mod from supplying
its own game module. Content-only mods still run on openQ4's own module. To
host a mod with its own code, set `net_serverAllowServerMod 1` and give players
the complete, trusted mod package yourself; the setting never makes game code
downloadable. openQ4 never loads a game module from the save folder or from
inside a `.pk4`.

openQ4 recognises a mod only when its `mod.json` is complete (`name`,
`version`, `releaseDate`, `website`, `author` and `requiredopenQ4Version`). If
a field is missing, the log says `Ignoring fs_game '<mod>'` and the server runs
the normal openQ4 game, so players without that mod can still join.

**Generated caches.** On a pure server, a player's game never trusts its own
generated image cache (`generated/images` in the save or cache folder),
because an edited copy can look identical to the original. When a player
connects, every image loaded from that cache is rebuilt from the original game
files, and images needed during the match are rebuilt rather than read. Only
those images are rebuilt; the engine does not restart. A loose file read from
a game folder before connecting, such as an extracted texture or map, still
makes the game restart into pure mode.

The experimental faster level loading (`com_levelLoadModernization 1`, off by
default) keeps similar caches of models, maps, collision data and animations
under `generated/`. Players on a pure server never read those either; anything
loaded from them before connecting is loaded again from the original game files
when the server's map loads.

**Wide loading screens.** In a window that is not 4:3, openQ4 widens each map's
loading screen to fit. It builds the picture in memory from the game files the
server allows and saves nothing, so players on a pure server see it too. Older
openQ4 versions saved these pictures under `guis/assets/generated/loadscreens/`
in the save folder; 0.14.1 no longer uses them, so you can delete that folder.

## Retail Quake 4 clients and servers

Retail Quake 4 clients cannot join an openQ4 server, and openQ4 cannot join a
retail server. openQ4 uses network protocol 2.42 and retail Quake 4 1.4.2 uses
2.85, and the two differ in more than the version number. Each side is told
why instead of being left waiting:

- A LAN scan leaves retail servers out of the server list. Connecting to one
  directly shows **Network protocol mismatch** with both protocol versions.
  openQ4 checks this before following the server into its `q4mp` folder, so you
  see the real reason rather than a missing-mod error.
- A retail client that tries an openQ4 server is told `server uses protocol
  2.42`, the same message a retail server sends for a protocol mismatch.

Joining retail servers would need a separate project, not a version change.

openQ4 builds before protocol 2.42 (0.14.1 and earlier) are refused the same
way, with `server uses protocol 2.42`, so a server and its players need the
same version ([networking guide](multiplayer-networking.md#older-openq4-versions)).
Since 2.42 every packet of a connection carries a tag made from a secret both
ends got at the join, so nobody can end a player's connection by forging
packets in their name or a "disconnect" in the server's.

## Download links from servers

A server can offer web links for packs a player is missing. openQ4 accepts only
well-formed `http://` or `https://` links to a valid host name or IP address.
It rejects everything else before asking you, including `file:` links, network
share paths, links with embedded user names or passwords, and malformed
addresses. The links your system opens from openQ4 follow the same rule on
Windows, Linux and macOS.

Standard openQ4 packages do not download packs inside the game. They can show a
checked web link from a server, but a direct download reports that it is
unavailable. That is intentional, not a broken downloader: get the pack from a
source you trust.

A build that deliberately turns in-game downloads on limits them to HTTP or
HTTPS, follows no redirects, gives up if it cannot connect within 15 seconds or
stays below 1 KiB/s for 30 seconds, and stops after one hour. Sizes, file
paths and checksums are still checked. These limits stop a server from
swapping in local files or other protocols; they do not make an untrusted
server trustworthy or vouch for what a pack contains.

## What rcon2 protects

The server hands out short-lived, single-use random challenges. Your game's
answer is tied to both sides' random values, your exact network address and
port, the server, and the exact command you sent. The server turns the
password into a deliberately slow-to-compute key (PBKDF2-HMAC-SHA-256 at
200,000 rounds), compares
answers in constant time, uses up each challenge whether the answer is right
or wrong, and limits how fast any one address, and everyone together, can
ask. Connection challenges, player and server IDs, download requests and
rcon2 values come from the operating system's secure random source; if that
is unavailable, the operation fails rather than fall back to something weaker.

`rcon2` proves who sent a command, but it is **not an encrypted transport**. Commands and the server's replies still travel as readable network packets. A captured rcon2 exchange also permits PBKDF2-throttled offline password guessing: the slowing makes each guess costly, but it does not save a short, reused or predictable password. Anyone who can watch or tamper with the network path
can read commands and replies and can disrupt the exchange. When that matters,
administer the server over a trusted network or an encrypted tunnel or VPN.

## Protection against bad network traffic

openQ4 checks incoming game messages before it uses them. Reading past the end
of a message is caught, sizes are checked, and player, entity, weapon,
spectator, tournament, projectile and hit-scan values are validated before
they change the game. A malformed update from the server ends that session
before the next frame is drawn.

A server also limits what one player can make it store or pass on:

- Player info is limited to 128 entries and 4 KB in total; a player who sends
  more is disconnected.
- Each player can send at most two emotes a second.
- Messages that follow a player's disconnect in the same packet are ignored.
- A message too big for a packet once compressed is dropped instead of
  stopping the server.

A server can change only the player settings that are meant to be shared over
the network, never private or unrelated ones, and a settings update that
arrives cut short changes nothing.

This hardens the known weak points; it is not proof that every message reader
is flawless. Keep running internet servers in isolation, keep the operating
system updated, and review future protocol changes with care.

## Standards and provenance

openQ4's implementation is original GPL code written from the public
specifications, not taken from the Quake 4 SDK:

- [FIPS 180-4: Secure Hash Standard (SHA-256)](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)
- [RFC 2104: HMAC](https://www.rfc-editor.org/rfc/rfc2104)
- [RFC 8018: PBKDF2](https://www.rfc-editor.org/rfc/rfc8018)
