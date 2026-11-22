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
- The file is rewritten whole on each change. A crash during the write can
  lose the list; a temporary file and a rename are not done yet.

### Text mute

- `mute`/`unmute` by slot or name. A muted player's chat and voice-menu lines
  stop in `ServerProcessReliableMessage` (with a notice at most every ten
  seconds) and again in `ProcessChatMessage`, which also covers a bot's lines.
- The mute is also stored by address (64 entries, in memory), so a reconnect
  does not lift it. It ends when it expires, when it is lifted or when the
  server process stops.
- Microphone voice is not touched; players mute voices for themselves.

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
`unmute`, `kick`, `flood`, `refused`. Fields as they apply: `time` (UTC),
`event`, `by` (`console`, `admin`, `vote`, `menu`, `server`), `slot`, `name`,
`address`, `range`, `reason`, `seconds`, `expires`. A value too long for the
1,024-byte line is cut on a whole escape and the line stays valid JSON.

## Commands

All are game commands that refuse to run anywhere but on the server (its
console, a listen host, or rcon): `ban`, `unban`, `banlist [reload]`, `mute`,
`unmute`, `status`. `removeClientFromBanList` is kept as another name for
`unban`.

**Words the console cuts apart.** `idCmdArgs` tokenizes with the script lexer,
so `192.0.2.0/24` arrives as `192.0.2.0` and `/24`, `10m` as `10` and `m`, and
an IPv6 address in pieces at its colons; rcon then forwards the pieces joined
by spaces, so the original spacing is gone by the time the server sees them.
`MPModJoinAddressArgs` and `MPModJoinDurationArgs` (in the core, natively
tested) put those words back: a `/prefix` argument joins the address before
it, a number joins a one-letter unit after it, and an IPv6 address is taken
from `[` to `]`. An IPv6 address without brackets is refused instead of being
guessed at, because a hex word of the reason could otherwise be read as part
of the address.

## Tests

- `tools/tests/mp_moderation.py`: compiles and runs
  `tools/tests/native/MpModerationTest.cpp` against the core, then pins the
  call sites, each command and its server-only guard, the file reader, the
  engine limit, the text in twelve languages and the guide. No assets.
- `tools/tests/network_security.py`: pins the caps and policies.
- `tools/tests/mp_moderation_smoke.py`: a pure dedicated server, one bot and
  four hidden clients in turn over loopback (the player, one from the banned
  address, one after the ban is lifted, one sharing the address). Needs a
  staged runtime and retail assets, so CI compiles it and does not run it.
  22 November 2026, Windows x64, debugoptimized build of this change over
  `590f2a20e`: 49 checks pass and every process exits cleanly. A client that
  was removed or refused waits in a dialog whose loop does not run its
  network client, which is why each later connection is a fresh process.

## Not done yet

- Bans by identity (waits for MP-17) and `status` showing an identity.
- A `mute` vote (MP-12 reserved the word).
- The engine's own `kick <slot>` is not in the admin log; `gameKick`, the
  Admin page, votes, bans and idle drops are.
- An NDJSON match log for casual servers.
- An admin role between referee and rcon.
- Graceful shutdown (announce, finish the match, stop).
- An atomic write of `banlist.txt`.
- The retained Admin page still labels the range column as it did the GUID
  and does not show a ban's reason or expiry.
- Linux, macOS and a real remote address: the smoke ran on Windows over
  loopback only.
