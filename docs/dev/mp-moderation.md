# Server administration and moderation (MP-18)

> **Development status:** This describes the current source tree, not the 0.15.0 package. See [release status](../user/release-status.md).

Roadmap item: [MP-18](roadmap.md#multiplayer-bots-arena-and-demos). The host's
guide is [server and remote-console security](../user/server-security.md#bans-mutes-and-chat-limits).

## What was wrong

Quake 4 keyed its ban list to a client GUID. No openQ4 client sends one
(`com_guid` is never written), so:

- `IsGuidBanned` compared an empty string with the list and matched nobody.
- The Admin page's **Ban** ran `kick <n> ban`. The engine's `kick` reads one
  word, so Ban only kicked, and nothing ever added an entry to the list.
- `idNetworkSystem::GetClientAddress` and `GetClientGUID` were stubs returning
  null, so the game module could not see who a connected client was.
- There was no address ban, text mute, configurable chat limit, per-address
  connection limit or record of who was removed and why.

## What exists now

| Piece | Where |
| --- | --- |
| Engine-free core: address and range parsing (IPv4, IPv6, mapped IPv4, prefixes), the ban list and its file lines, address mutes, the chat flood window, the name policy, admin-log lines | `src/mpgame/mp/Moderation.h` |
| `banlist.txt` reader and writer, shared by both game modules | `src/mpgame/mp/ModerationFile.h` |
| Enforcement, the commands and the admin log | `src/mpgame/Game_network.cpp` (the block that held Quake 4's ban list) |
| A client's address for the game module; the per-address connection limit | `src/framework/async/NetworkSystem.cpp`, `AsyncServer.cpp` |

### Bans

- A ban is an address range (`address/prefix`), a creation time, an expiry
  (0 for never), the player's name when banned (a label, never matched) and a
  reason. The list holds 512; expired bans are purged when the list is loaded,
  counted or full.
- `ServerAllowClient` and `RepeaterAllowClient` refuse a banned endpoint before
  any other check. The address is the one the connect packet came from, which
  has already proved its challenge there. The client is shown Quake 4's own
  "You have been banned from this server."
- `ban` on a connected player bans the player's address and drops every client
  inside the range. The listen server's own player and bots have no address
  and cannot be banned.
- A range may be no wider than `/8` (IPv4) or `/16` (IPv6).
- An IPv4 peer that arrives through the IPv4-mapped IPv6 range is its IPv4
  address, as the engine normalizes it.

### banlist.txt

Text, in the save folder, one ban a line:

```text
openq4-banlist 1
#: One ban a line. Times are seconds since 1970 (UTC); an expiry of 0 never ends.
#: ban address <address or address/prefix> <created> <expires> "<name>" "<reason>"
ban address 203.0.113.0/24 1795385700 0 "Spammer" "ban evasion"
```

- The second word names the kind, so MP-17's identity bans can join the file.
  Lines this build does not read (comments, later kinds, damaged lines) are
  kept and written back.
- **Migration.** Quake 4's file of the same name is binary: a name and a GUID,
  each a 32-bit length and its bytes. It is read once, each entry becomes a
  comment line, and the file is rewritten as text. A file in neither form is
  copied to `banlist.unreadable.txt` before it is replaced.
- **Pure servers.** A pure server refuses loose-file reads between map
  changes, so the file is opened by its OS path in the save folder, as
  `mapdb.txt` is.
- **Both game modules.** The Create Server page's ban list calls
  `PopulateBanList` and `RemoveGuidFromBanList` on whichever module the menu
  has loaded, usually `game_sp`. `src/game/Game_network.cpp` therefore reads
  and trims the same file through the shared header. The menus' "GUID" column
  now carries the range text, and `RemoveGuidFromBanList` takes that text.
  `AddGuidToBanList` stays in the engine's game interface and does nothing.
- The list is written whole to `banlist.txt.new` and then takes the list's
  place in one step (`idFileSystem::PromoteFile`), so a crash or a full disk
  during the write leaves the previous list as it was.

### Text mute

- `mute`/`unmute` by slot or name. A muted player's chat and voice-menu lines
  stop in `ServerProcessReliableMessage` (with a notice at most every ten
  seconds) and again in `ProcessChatMessage`, which also covers a bot's lines.
- The mute is also stored by address (64 entries, in memory), so a reconnect
  does not lift it. It ends when it expires, when it is lifted or when the
  server process stops.
- Microphone voice is not touched; players mute voices for themselves.
- **By vote.** `callvote mute <name>` is `VOTEFLAG_KICK | VOTEFLAG_MUTE` (bit
  20, `NUM_VOTES` 21). The mute flag has no payload: the player and their
  identity travel in the kick field, so the slot, host, identity and
  target-left checks of a kick vote all cover it, and the packed vote's
  layout is unchanged. A mute flag without the kick field is refused. Policy
  reads the mute bit alone, so `si_voteFlags 16` (no kick votes) still allows
  it. A passed vote mutes for `g_voteMuteMinutes` (10) and logs `by: vote`.
  A 0.15.0 client (same protocol 2.42) drops a vote state whose flags it
  does not know, so it neither sees nor casts this vote; it still counts in
  the electorate. The retained Vote page has no row for it yet.

### Chat flood window

Quake II's `flood_msgs`, `flood_persecond` and `flood_waitdelay` with the same
defaults: `g_chatFloodMessages 4` lines in any `g_chatFloodSeconds 4`, then
`g_chatFloodWait 10` seconds of silence. Lines sent while silenced are dropped
and do not extend the silence. The 500 ms per-line delay from the netcode
audit still runs first. The engine's budget for reliable game messages
(`gameMessageBudget`) is the command flood limit and is unchanged.

### Names

`SetUserInfo` cleans every server-side name before the older rules run: no
control characters, at most 32 characters that show (colour escapes kept and
not counted, UTF-8 never cut), never empty (`Player`), and none of
`g_reservedNames` (default `admin;console;referee`) whatever their case or
colours. The duplicate-name test now ignores colour escapes; `^1Bob` used to
pass beside `Bob`.

### Per-address connection limit

`net_serverMaxClientsPerAddress` (default 0, off). The 14 November netcode
audit recorded "one host can hold every slot" as a decision; the default keeps
that behaviour and the operator can now set a number. The count runs after the
challenge is proven and before the game is asked, and leaves out the slot a
reconnect would take over.

### Admin log

`logs/admin.ndjson` in the save folder, one JSON object a line, appended and
synced, on by default (`g_adminLog`). Events: `ban`, `unban`, `mute`,
`unmute`, `kick`, `flood`, `refused`, `shutdown_scheduled`,
`shutdown_cancelled`, `shutdown`, `admin_login`, `admin_login_failed`,
`admin_logout`. Fields as they apply: `time` (UTC),
`event`, `by` (`console`, `admin`, `vote`, `menu`, `server`), `slot`, `name`,
`address`, `range`, `reason`, `seconds`, `expires`, `when`, and for what a
logged-in admin asked for `admin_slot`, `admin_name`, `admin_address`. A value too long
for the 1,024-byte line is cut on a whole escape and the line stays valid
JSON.

**The engine's own kick.** `kick <slot>` is an engine command, and the game
hears of it only as `ServerClientDisconnect`, which carries no reason. The
engine now writes the slot and the untranslated reason to the read-only
`net_serverLastDrop` just before that call, and the game's disconnect handler
logs a kick when it reads the engine's kick reason there for a human whose
removal the game did not begin itself (`BeginServerDrop`). This needs no new
slot in the game interface, so `GAME_API_VERSION` stays 61.

### Match log

`g_matchLog 1` (off by default) appends the typed events of the
[live event stream](mp-live-event-stream.md#the-match-log) to
`logs/match.ndjson` on a server: joins and leaves, kills, scores, flag and
powerup events, phases and final standings. It is the same class as the live
stream in a second mode, so there is one event format and one set of
fixtures. It never truncates, begins a new stream with each map, and stamps
each stream's first line with the UTC time. It holds no addresses; those
stay in the admin log.

### Graceful shutdown

`serverShutdown <match|now|time|cancel> [reason]` (`idGameLocal::Shutdown_f`).
The state lives in `idGameLocal`, so it survives a map change.
`RunServerShutdown` runs every server frame from `idMultiplayerGame::Run`
(the timed deadline, its repeated notices, and a `match` shutdown on a server
with no human on it), from the game state's `NEXTGAME` step just before the
next map is chosen, and from `idGameLocal::MapRestart`, so a map change or a
restart that a vote or an operator forces also ends the last match. When it
fires it sends the
goodbye, arms a 1.5 second delay so the line can reach the players, and then
appends `quit` on a dedicated server or `disconnect` on a listen server; the
engine's own shutdown tells each client the server has shut down.

### Admins

A second password (`net_serverAdminPassword`) gives a player the right to ask the
server to kick, mute or ban a connected player. The user's decision, 22
November 2026: a referee's powers plus kick, mute and unmute, and bans of
connected players only, capped in length; no lifting of bans, no addresses
(`status`, `banlist`), no raw address or range bans, no shutdown.

**Login.** It is the referee's challenge and response
(`mp/match/MatchAuthentication`), unchanged: the same operation
(`MP_MATCH_OP_REF_AUTHENTICATE`), the same challenge message, the same
PBKDF2 proof. The admin kind has its own request word (`challenge:admin`)
and its own proof tag (`a1:` where a referee's is `v1:`), and is answered by
a second `mpRefereeAuthenticationService` with its own verifier, challenges
and lockout, so a referee proof never opens the admin grant or the reverse.
`ApplyMatchOperationContinuation` hands the admin kind to
`ApplyAdminAuthentication` before the referee's checks run, because a
referee login is refused to a player and an admin may be playing.
`InitializeAdminAuthentication` reads the cvar the way
`InitializeRefereeAuthentication` reads `g_refPassword`: turned into a
verifier, then cleared, with an unknowable verifier installed when there is
no password so the exchange does not say whether one exists.

**The grant** is `matchAdminConnection[slot] == matchConnectionId[slot]`: it
belongs to the connection and is cleared with the slot's other transport
state. The engine re-initialises every client on a map load, which gives each
a new connection id with no disconnect in between; the grant is carried
across that by `matchAdminCarry`, the address and port that logged in, set at
login and cleared on disconnect, logout and a change of password. So a login
lasts for the connection, not for one map, and a slot's next occupant never
inherits it. An admin who is watching with no roster seat also gets the
referee role for the current match session, by the same role mutation; a
playing admin does not. A logout takes back only a referee role the admin
login gave.

**The host** sees and ends logins from the console: `admin` lists them,
`admin revoke <slot|name|all>` ends them, `admin off` forgets the password. A
password is at least 8 bytes, is taken as soon as it is set (the server polls
the cvar each frame) and logs out everyone who used the old one.

**The request** is a new client-to-server reliable message,
`GAME_RELIABLE_MESSAGE_MODERATION` (appended), never a console line: a verb,
the target's slot and spawn identity, a length and a reason
(`idGameLocal::ServerModerationRequest`). `GAME_RELIABLE_MESSAGE_SERVER_ADMIN`
still refuses remote clients. The rules are in the core and natively tested
(`MPModAdminCheck`, `MPModAdminBanSeconds`): the target must be the player
the admin chose and not the admin, the host, another admin or a bot; a ban
needs `g_adminMaxBanMinutes` above 0 and is always cut to it; and a ban or a
mute is refused when the range it would cover (`MPModPlayerPrefixBits`) also
holds an admin, the asking one included, since both are of a range.
The only console line a request can run is `kick <slot>` for the slot the
server checked. The flood class is `RELIABLE_FLOOD_MODERATION` (500 ms).
The login operation is exempt from the match operation cooldown, so what a
refused login writes (one chat line, one log line) is paced to once in two
seconds for a connection.

**On the client** `ban`, `mute`, `unmute` and `gameKick` send the request
instead of printing that only the server runs them; they take a connected
player's slot or name and never an address. `admin` reads the password from
the client's own `net_clientAdminPassword`, never from its arguments. Both
password cvars are `CVAR_PRIVATE`, which the engine already keeps out of the
console history, the parsed command line in the log, cvar listings and the
config. They are declared in the engine (`AsyncNetwork.cpp`, beside the rcon
passwords) and only read by the game: a game cvar is not registered, and so
not yet private, while the other game module is loaded, and the main menu
runs the single-player module, which is where a host or an admin is likely
to type the password.

**The referee password had that gap.** `g_refPassword` was an ordinary game
cvar. A server started with it on the command line wrote it into its log's
parsed command line (with `developer` and `logFile` on), and one typed at a
console was saved in `consolehistory.dat`. It keeps its name, is
`CVAR_PRIVATE` in the game module, and is declared in the engine as well
(`idAsyncNetwork::serverRefereePassword`), so it is private before the module
loads; two declarations of one cvar merge their flags. The engine already
drops lines that name a private cvar when it next reads an existing history
file. Before the change a dedicated server started with
`+set g_refPassword <secret> +quit` left the secret in its log beside a
redacted admin password; after it, both lines are redacted and no file holds
either. `network_security.py` pins the three declarations, the moderation
smoke starts its server with both passwords and finds them in none of its
files, and `mp_control_smoke.py --role-matrix --net-ip 127.0.0.1` (a remote
referee's login) passes on the same build.

## Commands

On the server (its console, a listen host, or rcon): `ban`, `unban`,
`banlist [reload]`, `mute`, `unmute`, `status`, `serverShutdown`, and
`admin [list|revoke|off]`. `removeClientFromBanList` is kept as another name
for `unban`. `unban`, `banlist`, `status` and `serverShutdown` refuse to run
anywhere else. Typed on a client, `ban`, `mute`, `unmute` and `gameKick` are
a logged-in admin's request, and `admin` / `admin logout` log in and out.

**The line as typed.** `idCmdArgs` tokenizes with the script lexer, which is
wrong for these commands in ways no rejoining can undo: `192.0.2.0/24`
arrives as `192.0.2.0` and `/24` and `10m` as `10` and `m`; `3rd offence`
becomes the number 3 and a word, so the reason was read as a three-minute
ban; and inside `2001:1ef0::` the lexer reads `1e0` as a float and consumes
the `f` as its suffix, so a different range was banned. The engine now
publishes the line of a command registered with `CMD_FL_RAWTEXT` in the
private read-only cvar `cmd_rawText` while it runs, and the commands cut it at
spaces only (`MPModSplitWords`, natively tested; double quotes keep a name's
spaces, a word beginning `//` ends the line). The engine's `rcon` sends its
line the same way, so the server does not tokenize damaged text a second
time. Where there is no such line (an older engine, or a line with `$`, whose
cvar values only the tokens hold) the tokens are used and
`MPModJoinAddressArgs` / `MPModJoinDurationArgs` put back what they can.
`ban` reads its first word as a slot number if it is all digits, then as an
address, then as a name, so a player named `192.0.2.7` or `[` cannot take a
ban meant for an address.

## Tests

- `tools/tests/mp_moderation.py`: compiles and runs
  `tools/tests/native/MpModerationTest.cpp` against the core and
  `MpModerationFileTest.cpp` against the ban file reader (over a stand-in
  file system that can cut a read short and fill the disk), then pins the
  call sites, each command and its server-only guard, the file reader, the
  engine limit, the text in twelve languages and the guide. No assets.
- `tools/tests/network_security.py`: pins the caps and policies.
- `tools/tests/mp_moderation_smoke.py`: a pure dedicated server, two bots and
  seven hidden clients in turn over loopback (the player, one from the banned
  address, one after the ban is lifted, one sharing the address, one to
  kick, and two for an admin to kick and to ban). Needs a staged runtime and
  retail assets, so CI compiles it and does not run it. 22 November 2026,
  Windows x64, debugoptimized build of the audit's fixes over `d30280aa2`:
  133 checks pass and every process exits cleanly, the server by itself
  after its last match. The players an admin acts on bind 127.0.0.2, so the
  server sees a second address and the ban is a real one; `--skip-admin`
  leaves the stage out where only 127.0.0.1 exists. The hidden clients take
  their commands from scripts, so the console history's handling of the
  private cvar is the engine's own (pinned in `network_security.py`), not
  something this smoke types. The earlier build (77 checks) also passed
  `mp_map_rotation_smoke.py` (44 checks). Two things shape the harness. A
  client that was removed or refused waits in a dialog whose loop does not
  run its network client, so each later connection is a fresh process. And
  a server launched with no frag or time limit stays in warmup, which keeps
  the moderation stages still; the shutdown stage sets a frag limit and
  restarts the map to get a match that ends.

## Audit, 22 November 2026

The user asked for the implementation and the functionality around it to be
scrutinised. Two independent reviews (the engine-free core with its file
reader; the wiring into the game and the engine) and a pass of my own found
the following, all fixed in the same change unless marked open below.

**Wrong result, silently**
- The console's lexer damaged what the commands were given: an IPv6 group
  such as `1ef0` lost a character and a different range was banned; a reason
  beginning with a digit became the ban's length; `[address]:port` was never
  seen whole. Fixed at the root: the commands read the line as typed.
- A player named like an address (`192.0.2.7`, `[`) took a ban meant for it.
- `banlist.txt` that could not be read (too large, cut short, UTF-16, a first
  line the sniff did not expect) was taken for an empty or a Quake 4 list and
  written over; more than 512 bans, or 256 other lines, were silently dropped
  on the next save; a full disk could promote a short staged file. Now such a
  file is left as it is, the list in memory stays in force, saving is refused
  until a load works, and a staged file is read back before it is promoted.
- `serverShutdown match` was skipped by every map load that goes round
  `MapRestart` (a limit vote, a ruleset change, a series handing on its map,
  the eight-hour restart). All of them pass one point in `RunFrame` now.

**Evasion**
- Over IPv6 a ban by slot, a mute and the per-address limit keyed on one
  `/128`; a player moves inside their `/64` at will. All three use the `/64`
  (`MPModPlayerPrefixBits`, `AsyncServer_SameSubscriber`).
- Reserved names were passed under Raven's long escapes (`^c900admin`,
  `^idm0`, `^n1`), and with invisible characters and non-breaking spaces. The
  cleaner mirrors `idStr::IsEscape`, keeps colours only, removes icons and
  repeats, keeps only well-formed UTF-8, removes invisible characters and
  reads every kind of space as a space. A caret that starts no colour is
  removed, so a name can no longer swallow the text printed after it.
- A muted player still spoke through `ui_name` and `ui_clan` (shown in the
  score list and every kill message) and through the map queue's
  announcement. The mute holds both, and the announcement goes to the sender
  alone. The clan tag had no rules at all; it follows the name's now.
- The per-address limit's reconnect exemption matched on the port alone, and
  in my first `/64` version on the client-chosen id alone.

**Privilege**
- An admin's ban, or a mute, of a player who shares an address with another
  admin reached that admin. Refused.
- An admin who is watching holds the referee role and through Match Control
  could remove another admin. Only the host does now.
- The host could not see who was an admin, end a login or withdraw the
  password while the server ran. `admin`, `admin revoke`, `admin off`.
- An admin password could be one character. Eight bytes at least.
- `admin logout` took away a referee role earned with the referee's own
  password.

**The log**
- Refused connections were paced by the last address seen, so two banned
  addresses in turn wrote a line a packet. Paced for each ban, with a cap of
  30 lines a minute for the list.
- A referee's removal of a player was logged as a console kick with nobody
  named; a console kick could vanish when the disconnect's own broadcast
  dropped another client and overwrote `net_serverLastDrop`; an engine error
  inside an admin's request left every later line attributed to that admin;
  a locked-out login wrote a line every two seconds for ever.
- A console line that came by rcon was indistinguishable from one typed at
  the console. It carries `rcon`, the sender's address (`net_serverRconFrom`).
- A name or reason with a stray byte, or one cut by the line's length, made
  the line invalid UTF-8. Values are written a character at a time, and a
  byte that is no part of one as U+FFFD.

**Smaller**
- A shutdown set on one listen server ran on the next one in the same process
  (`net_serverRunId`). `banlist reload` did not remove the connected players
  a new ban covered. A list of 64 permanent mutes lost one to any timed mute.
  The chat flood window's stamps outlived a map restart. `admin <password>`
  was refused but kept in the console history (the engine treats that line
  as private now). An admin login lasted one map (see **The grant**).

**Left as they are**
- The chat flood silence belongs to the slot and ends with a reconnect or a
  map load. A reconnect takes longer than the silence.
- A timed ban's expiry trusts the wall clock: a host whose clock jumps ahead
  expires bans early, and a load then deletes them.
- An admin alternating `mute` and `unmute` writes two log lines a second.
  The admin is trusted and named on each line.
- Look-alike letters from other alphabets pass the reserved-name check.
- A first load cannot tell a missing `banlist.txt` from one the system will
  not open; both read as no list. A reload keeps the list in memory.
- On an older engine, or for a line with `$`, the commands still get the
  lexer's tokens.
- A kick, by vote or by hand, lets the player reconnect at once. Whether a
  kick should carry a short ban is a decision for the project, not a defect.

## Not done yet

- Bans by identity (waits for MP-17) and `status` showing an identity.
- A row for the mute vote on the retained Vote page.
- Admins have no menu: the commands are console only.
- The retained Admin page still labels the range column as it did the GUID
  and does not show a ban's reason or expiry.
- Linux, macOS, IPv6 and a real remote address: the smoke ran on Windows over
  IPv4 loopback only, so the `/64` rules, the referee's removal and the map
  queue's muted announcement are covered by native tests and pins, not by a
  run.
