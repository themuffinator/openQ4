# Server and Remote-Console Security

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

openQ4 uses an authenticated remote-console protocol, `rcon2`, by default.
It replaces Quake 4's legacy packet, which sent the administration password
verbatim over UDP. Gameplay, server discovery, the connection handshake's wire
layout, and the retail Quake 4 asset format are unchanged.

## Configure a strong password

Set the same password on the server and on the administrator's client:

```text
net_serverRemoteConsolePassword "a-long-random-password"
net_clientRemoteConsolePassword "a-long-random-password"
```

Then issue a command from the client in the usual form:

```text
rcon status
```

`rcon2` requires at least 12 password bytes. Use a unique, randomly generated
password of 24 or more characters for an Internet-facing server. Do not reuse a
login, referee, database, or service password.

Enter the password at a trusted local console, or put the server-side setting
in a configuration file readable only by the account that runs the server.
Do not commit that file, share it with a package, or use the password on the
process command line. In particular, never launch with
`+set net_serverRemoteConsolePassword ...` or
`+set net_clientRemoteConsolePassword ...`: launch arguments can be visible to
other local users, process-monitoring tools, crash reports, and service logs.

Private CVars are redacted from broad console listings and direct console
queries, omitted from generic CVar serialization and dictionaries, blocked
from `$` CVar expansion, and suppressed from console echo, command history,
completion previews, startup-command reporting, and event journals. These
protections reduce accidental disclosure; they do not make an insecure
configuration file safe.

Server-originated userinfo and synchronized-CVar messages have separate,
explicit authority. Each can update only registered CVars carrying the matching
network flag; unrelated and private settings are ignored. Dictionary decoding
is all-or-nothing, so a truncated update does not leave a partially changed
settings set behind.

## The in-match Admin page

The multiplayer menu's Admin page asks for the same password, checks it with
the server and then opens its player, server and console controls; a wrong
password returns to Join Team. Type the password into the page's field, or set
`net_clientRemoteConsolePassword` at the console first. The field takes 16
characters, so for a longer password set the CVar at the console, then open
Admin and choose Accept without typing in the field.

## Server-provided package links

Pure-server redirects and PK4 download entries are accepted only as bounded
`http://` or `https://` URLs. Their authority must contain a syntactically valid
DNS name, IPv4 literal, or bracketed IPv6 literal; malformed labels, ports,
credentials, ambiguous authorities, control characters, and every other URL
scheme (including `file:` and SMB-style paths) are rejected before prompting or
queueing. The generic background downloader applies the same check again.

Standard Meson packages do not enable libcurl. They can show a validated web
redirect offered by a server, but in-process direct PK4 transfer reports
unavailable; obtain any required package separately from a source you trust.
This is intentional and should not be diagnosed as a broken downloader.

If a separately integrated build deliberately enables libcurl, package
transfers are limited to HTTP(S), do not follow redirects, time out if a
connection cannot be established within 15 seconds, stop after 30 seconds below
1 KiB/s, and have a one-hour absolute cap. Advertised and received sizes,
destination paths, and package checksums retain their separate validation.
These restrictions bound that optional legacy path and prevent
local-file/protocol substitution; they do not prove DNS ownership, make an
untrusted server trustworthy, or certify arbitrary package content.

The platform URL opener follows the same HTTP(S)-only syntax policy on Windows,
Linux, and macOS. In particular, macOS no longer accepts local `file:` URLs.

## What rcon2 protects

The server issues short-lived, one-use random challenges. The proof binds the
client and server nonces, the exact UDP endpoint, a server binding value, and a
SHA-256 digest of the requested command. The server derives its proof verifier
with PBKDF2-HMAC-SHA-256 at 200,000 iterations, compares proofs without an
early-exit timing leak, consumes a challenge before accepting or rejecting the
proof, and rate-limits both individual sources and global unauthenticated reply
traffic. Connection challenges, client IDs, server IDs, download-request IDs,
and rcon2 nonces use the operating system's cryptographic random source; an
operation fails closed if that source is unavailable.

`rcon2` authenticates the command sender to the server, but it is **not an encrypted transport**.
The command and server output still travel in cleartext
UDP packets. A captured rcon2 exchange also permits PBKDF2-throttled offline password guessing.
PBKDF2 makes each guess more expensive; it does not rescue a
short, reused, or predictable password. Anyone able to observe or alter the
network path can read commands and output and can disrupt the exchange. Use a
trusted network or an encrypted tunnel/VPN when command or output confidentiality
matters.

## Pure multiplayer and local game modules

`si_pure 1` is supported and remains enabled when selected; openQ4 no longer
silently turns it off at server startup. Pure mode compares the ordered PK4
asset list used by the server and client. The protocol can identify missing
asset PK4s, but standard packages do not enable in-process direct transfer. Any
separately integrated curl-enabled package path remains asset-only and never
supplies executable game code.

The pure handshake also carries a game-code checksum. openQ4 sends a fixed
token, `0x68fb90b1`, which is the checksum of the official Quake 4 1.4.2
`q4base/game300.pk4`. It does not require, mount, download, extract, or execute
that archive.
The engine accepts the token only when the required `game_sp` or `game_mp`
module was already loaded from trusted local openQ4 package/module roots. A missing
module, an unsupported platform ID, an empty pure asset list, or a different
token fails closed instead of weakening pure mode or starting a code download.

By default, `net_serverAllowServerMod 0` also prevents a selected mod from
supplying its own game module. Content-only mods can continue to use the base
openQ4 module. An administrator who intentionally operates a code-bearing mod
must set `net_serverAllowServerMod 1` and distribute a complete, trusted package
to clients separately; the setting does not make module code downloadable.
openQ4 never loads a game module from the save folder or from inside a PK4.

A mod is recognised only with a complete `mod.json` (`name`, `version`,
`releaseDate`, `website`, `author` and `requiredopenQ4Version`). If a field is
missing, the log says `Ignoring fs_game '<mod>'` and the server runs the normal
openQ4 game, so clients without that mod can join it.

The token is the same on Windows, Linux, and macOS, and it only works between
openQ4 builds. Retail multiplayer servers expect the checksum of a `q4mp` game
pak instead. The token is not a cryptographic measurement of the loaded module
and does not turn pure mode into an anti-cheat system.

On a pure server a client never uses its own generated image cache
(`generated/images` in the save or cache folder). A cache entry names the game
file it came from, but an edited copy looks the same, and the only way to tell
them apart is to rebuild the image. So when a client connects, any image it
loaded from that cache is rebuilt from the original game files, and images
needed during the match are rebuilt instead of read. Before 9 November this
restarted the whole engine on the first pure connect of each session; now only
those images are rebuilt. A loose file read from a game folder before
connecting, such as an extracted texture or map, still makes the client restart
into pure mode.

The experimental level-load option (`com_levelLoadModernization 1`, off by
default) keeps similar caches of decoded models, maps, collision data and
animations under `generated/`. A pure server's clients never read those either.
Anything a client loaded from them before connecting is loaded again from the
original game files when the server's map loads.

In a window that is not 4:3, openQ4 widens each map's loading screen to fit.
It builds that picture in memory from the game files the server allows and
saves nothing, so a pure server's clients see the wide loading screen too.
Versions before 9 November saved it in the save folder
(`guis/assets/generated/loadscreens/`), which a pure server cannot trust: their
clients showed the map's standard picture there, and at first restarted the
whole engine on connect. Those saved files are no longer used and can be
deleted; one that is somehow still read before connecting is dropped on
connect rather than restarting the engine.

## Malformed network traffic

The shared bit-message reader records attempts to read beyond the received
payload, including through delta messages. Covered queue and user-command
decoders check their encoded sizes, while audited SP and MP leaf readers stage
decoded fields until their payload is valid and check referenced entity/player
types and bounds, spectator and weapon values, tournament instances, projectile
owners, PVS and game state, and hit-scan fields. A late malformed top-level
snapshot tears down the affected session before another game or presentation
frame. The legacy entity lifecycle is not a whole-snapshot rollback
transaction.

A server also limits what one client can make it store or relay. A client's
player info is held to 128 entries and 4 KB in total, and a client that sends
more is disconnected. Emotes are limited to two a second for each player.
Messages that follow a client's disconnect in the same packet are ignored. A
message that would no longer fit a packet once compressed is not sent, where it
used to stop the server.

This is targeted hardening of the audited legacy paths. It is not a claim that
every parser is formally verified, and malformed-packet regression tests do not
replace normal Internet-server isolation, operating-system updates, or careful
review of future protocol changes.

## Legacy compatibility mode

Legacy plaintext rcon is disabled on both sides by default. It remains only as
an explicit compatibility escape hatch for an old client or administration
tool:

```text
// Server: accept legacy packets.
net_serverAllowLegacyRcon 1

// openQ4 client: send a legacy packet.
net_clientUseLegacyRcon 1
```

An openQ4 client talking to an openQ4 server needs both settings to use the
legacy path. A third-party legacy tool needs the server setting; an openQ4
client talking to an old server needs the client setting. Legacy mode sends the
password and command in plaintext and should be enabled only temporarily on a
trusted network. Return both variables to `0` when compatibility testing ends.

Old plaintext-rcon tools receive no response until the server operator
explicitly enables the legacy path.

## Retail Quake 4 clients and servers

Retail Quake 4 clients cannot join an openQ4 server, and openQ4 cannot join a
retail server. openQ4 uses network protocol 2.41, while retail Quake 4 1.4.2
uses 2.85. The two protocols also differ beyond the version number, including
the reliable message numbering and the player input format.

Each side is told why instead of being left waiting:

- A LAN scan leaves retail servers out of the server list. Connecting to one
  directly shows **Network protocol mismatch** with both protocol versions.
  openQ4 checks the protocol before it would switch to the retail server's
  `q4mp` folder, so it no longer stops with a message about a missing mod.
- A retail client that tries an openQ4 server is answered with `server uses
  protocol 2.41`, the same message a retail server sends for a protocol
  mismatch.

Joining retail servers would be a separate project, not a version-number
change.

## Standards and provenance

The engine implementation is original GPL code based on the public algorithm
specifications, not on the Quake 4 SDK game-library implementation:

- [FIPS 180-4: Secure Hash Standard (SHA-256)](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)
- [RFC 2104: HMAC](https://www.rfc-editor.org/rfc/rfc2104)
- [RFC 8018: PBKDF2](https://www.rfc-editor.org/rfc/rfc8018)
