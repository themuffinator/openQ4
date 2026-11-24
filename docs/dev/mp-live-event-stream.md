# Live match event stream

Roadmap item: [MP-16](roadmap.md). Player guide:
[Feed a broadcast overlay](../user/competitive-matches.md#feed-a-broadcast-overlay).

openQ4 can write the events of a multiplayer match to a file while the match
is played, one JSON object per line, so a broadcast overlay or a statistics
tool can follow it. This page is the reference for that format and for the
code that produces it.

## Turning it on

Set `g_matchEventStream 1`. The cvar is archived, needs no cheats, and may be
set on either end:

- A server (dedicated or listen) writes the match it runs.
- A client writes the match it is shown. A caster who does not host sets it on
  their own client and spectates.

The file is `match-events/live.ndjson` under the game directory of the save
path (`fs_savepath`), for example
`<save path>/baseoq4/match-events/live.ndjson`. It is not written during demo
playback or by a repeater.

## What a reader can rely on

- Every line is one complete JSON object in UTF-8 followed by a single `\n`.
  The game flushes after each line, so a line is either absent or whole.
- A stream starts with a `stream` line that describes the match and everyone
  in it. A reader that starts late reads from the top of the file and has the
  present state when it reaches the end.
- The file is truncated and a new stream begins when a map starts, when the
  process reconnects, and when the cvar is turned on. A follower that sees the
  file shrink starts again from offset zero.
- Player names are untrusted input. Colour escapes are removed, quotes,
  backslashes and control bytes are escaped, and any byte that is not
  well-formed UTF-8 is replaced with U+FFFD. A reader must still treat names as
  text, never as markup.
- The stream holds only what a scoreboard, the flag status and the kill feed
  already show a spectator. It has no positions, health, armour, ammunition or
  item timing, and a client's stream holds nothing its server did not send it.
  A client cannot tell bots from people, so it omits `bot`.

## Envelope

Every line begins with the same four members, in this order:

| Member | Type | Meaning |
| --- | --- | --- |
| `v` | integer | Schema version. This page describes version 1. |
| `seq` | integer | Position in the stream, from 0, rising by one per line. |
| `t` | integer | Game time in milliseconds. Never decreases within a stream. |
| `type` | string | The event type; decides the remaining members. |

A gap in `seq` means lines were lost to a full disk or a line too long to
write; the game never reuses a number.

## Events

Team names are `marine` and `strogg`. A `team` member is `null` outside team
modes. Slots are client numbers, 0 to 31.

| `type` | Members | Written when |
| --- | --- | --- |
| `stream` | `game`, `source` (`server` or `client`), `map`, `gameType`, `phase`, `teamGame`, `teams` (team modes), `players` | A stream opens. |
| `phase` | `from`, `to` | The match phase changes: `inactive`, `warmup`, `countdown`, `live`, `suddenDeath`, `review`, `nextGame`. |
| `player` | `event` (`join`, `leave`, `name`, `spectator`, `team`), `slot`, `name`, `was` (the old name, on `name`), `team`, `spectator`, `score`, `bot` (servers) | Someone joins or leaves, is renamed, starts or stops spectating, or changes team. |
| `score` | `team`, `score`, `was` | A team's score changes. |
| `playerScore` | `slot`, `name`, `score`, `was` | A player's score changes. |
| `flag` | `flag` (`marine`, `strogg`, `neutral`), `state` (`base`, `dropped`, `taken`), `by` (One Flag: the team holding it), `carrier`, `carrierName`, `wasCarrier`, `wasCarrierName` | A flag is taken, dropped, returned or captured. A capture reads `base` with `wasCarrier` set and is followed by a `score` line. |
| `powerup` | `event` (`gain`, `end`), `slot`, `name`, `powerup` | A player picks up, or loses, an item powerup: `quadDamage`, `haste`, `regeneration`, `invisibility`, `ammoRegen`, `guard`, `doubler`, `scout`, `adrenaline`, `armorRegen`. |
| `kill` | `killer`, `killerName`, `victim`, `victimName`, `weapon`, `cause` (`weapon`, `world`, `telefrag`), `suicide`, `teamKill`, `quad` | A player dies. `killer` is `null` for a suicide or the world; `weapon` is the weapon definition without its `weapon_` prefix, or `null`. |
| `standings` | `phase`, `teamGame`, `teams` (team modes), `players` | The match reaches `review`: the final scores. |

`teams` is an array of `{ "team", "score" }`. `players` is an array of
`{ "slot", "name", "team", "spectator", "score", "bot" }`.

A `kill` line is written when the death is scored; the `playerScore` and
`score` lines it causes follow on the next frame.

On a client, `powerup` lines cover the players that client was sent, which is
everyone for a spectator and the visible players otherwise.

## The match log

`g_matchLog 1` (MP-18, off by default) makes a server keep the same events
for good in `logs/match.ndjson` under the save path. It differs from the live
file in three ways:

- **Servers only.** A client keeps no match log.
- **Appended, never truncated.** Where the live file starts again, the match
  log begins a new stream: a `stream` line, with `seq` back at 0 and `t` at
  the new map's clock. A reader splits the file on `stream` lines.
- **Wall-clock time.** Its `stream` lines carry `utc`, the time the stream
  began (`"2026-11-22T10:15:00Z"`), after `gameType`. The live file's do
  not. Add `t` to it for the time of a later line, bearing in mind that game
  time stops during a pause.

Everything else on this page holds for both files. The match log has no
rotation: an operator moves or deletes it.

## Compatibility

New event types and new members may be added without changing `v`; a reader
must ignore what it does not know. Renaming or removing a type or member, or
changing a member's meaning, raises `v`. A reader should skip lines whose `v`
it does not implement.

## Sample overlay

`tools/overlay/match_overlay.py` is a complete reader in the Python standard
library:

```text
python tools/overlay/match_overlay.py --file <live.ndjson>           serve a page on http://127.0.0.1:27980/
python tools/overlay/match_overlay.py --file <live.ndjson> --print   print a feed
python tools/overlay/match_overlay.py --file <live.ndjson> --once    print the state as JSON
```

The page has a transparent background for use as a browser source and reads
`/state.json`, which other tools may read too. The server listens on the
loopback address unless `--host` is given. `MatchState` in that file is the
reducer to copy: it applies one decoded line at a time, ignores unknown types,
counts `seq` gaps and starts over on a `stream` line.

## Implementation

| File | Role |
| --- | --- |
| `src/game/mp/LiveEvents.h` | The state that is compared, the line builder and the diff. No engine dependency. |
| `src/game/mp/LiveEventStream.cpp` | Captures the state from the game, owns the file, declares both cvars and asserts that the header's tables match the game's enums. One class, `mpLiveEventStream`, of two kinds: `LIVE` and `MATCH_LOG`. |
| `src/game/MultiplayerGame.cpp` | Calls `liveEvents.Update()` once a frame on servers and clients, `Kill()` where a death is scored (server) or received (client), and `Close()` on reset and shutdown; and the same on `matchLog`, which acts only on a server with `g_matchLog` set. |

## Checks

`tools/tests/mp_live_event_stream_contract.py` compiles
`tools/tests/native/LiveEventsFixture.cpp` against the production header,
validates a sample stream line by line against the table above, replays it
through the overlay's reducer, file follower, command line and page, and
re-runs the fixture against single-reversal mutations of the header.
`tools/tests/mp_spectator_casting_smoke.py` runs a listen server with bots and
requires the stream a real match writes to pass the same validation.
