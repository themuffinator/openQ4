# Server and remote-console security

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

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
retail server. openQ4 uses network protocol 2.41 and retail Quake 4 1.4.2 uses
2.85, and the two differ in more than the version number. Each side is told
why instead of being left waiting:

- A LAN scan leaves retail servers out of the server list. Connecting to one
  directly shows **Network protocol mismatch** with both protocol versions.
  openQ4 checks this before following the server into its `q4mp` folder, so you
  see the real reason rather than a missing-mod error.
- A retail client that tries an openQ4 server is told `server uses protocol
  2.41`, the same message a retail server sends for a protocol mismatch.

Joining retail servers would need a separate project, not a version change.

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
