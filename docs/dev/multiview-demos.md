# Demo Playback and Multi-View Demo Architecture

This document records implementation, format and compatibility contracts.
Outstanding demo work is coordinated only in the [development roadmap](roadmap.md#multiplayer-bots-arena-and-demos),
MP-05; deferred broadcast and ecosystem decisions are OPT-04. Historical delivery
phases below do not establish a separate plan or current qualification status.

## Status

The MVD core and the first unified demo-playback experience are implemented.
An openQ4 multiplayer server can record one full-world stream across every
active multiplayer instance, preserve the audience of reliable and unreliable
events, finalize it as a versioned `.mvd` file, validate it, and play it
through the Quake 4 server-demo free-fly/follow path.

The main-menu **Demos** library discovers native MVDs, openQ4 render demos,
command demos, unsupported legacy recordings, and incomplete recording
artifacts. Playback has one capability-gated control surface for pause/resume,
speed, relative and absolute seeking, stepping, MVD free roam/follow, and
stopping. Formats do not receive controls for state they did not record.

The format is intentionally new. An openQ4 `.mvd` is not labelled as a legacy
Quake 4 `.netdemo`, because the latter is a packet-oriented client recording
with different compatibility and safety properties.

## Competitive precedent

| Generation | Representative implementation | Model | Expectations it established |
|---|---|---|---|
| QuakeWorld | [MVDSV](https://github.com/QW-Group/mvdsv) and [ezQuake multiview](https://ezquake.com/docs/multiview.html) | The server records one stream containing every player POV. Messages carry all/single/multiple/stats routing and the same stream can feed QTV. | One recording for the match, instant POV switching, server-side `record`/`easyrecord`, automatic naming, downloads, autotracking, and broadcast use. |
| Quake II | [Q2PRO MVD](https://github.com/q2pro/q2pro/blob/master/doc/server.asciidoc) | A server-side dummy observer receives full entities, player state, and routed messages. `.mvd2` supports local playback and GTV relay. | Record without occupying a real player, play/seek controls, automatic limits, channel filtering, relay, and record-during-playback workflows. |
| Quake III Arena | [CPMA commands](https://www.playmorepromode.com/guides/cpma-commands), [client settings](https://www.playmorepromode.com/guides/cpma-client-settings), and [server settings](https://www.playmorepromode.com/guides/cpma-server-settings) | `mvd` records all POVs; playback can show all/none, highlight a primary view, and attach stats to that view. | Multi-window viewing, coach/team views, highlighted-player stats, automatic recording/action bits, powerup/killer tracking, and unattended broadcast operation. |
| Quake 4 | [idDevNet server network demos](https://iddevnet.dhewm3.org/quake4/NetworkDemos.html) and [benchmarking notes](https://iddevnet.dhewm3.org/quake4/Benchmarking.html) | The 1.1+ server demo records more than one client and permits free flight or player cycling. Competitive mods such as Q4Max built multipov and broadcast workflows on that base. | Familiar `recordNetDemo`/`playNetDemo`/`stopNetDemo` commands, free flight, cycling players, playback speed, filesystem/content compatibility checks, and competition-friendly automation. |

Across the generations, “multiview” means more than recording the spectator
camera. The durable user contract is:

- one authoritative match file, recorded by the server;
- every relevant player POV and full match state in that file;
- correct private, excluded, team/instance, positional, and global event routing;
- free-fly and low-friction player switching during playback;
- recording that is safe to leave enabled on a dedicated server;
- clear handling of incomplete, corrupt, oversized, and incompatible files;
- automation, seeking, richer view layouts, and live relay as natural follow-ons.

## openQ4 architecture

The Quake 4 game code already contained a dormant server-demo pseudo-client at
slot `MAX_CLIENTS`. Its original path can:

- build a delta snapshot without a player PVS, covering the complete active
  world for multiplayer instance 0;
- queue unreliable messages with either a target client or source PVS areas;
- serialize initial players, game state, and reliable state;
- apply server-demo snapshots to the pseudo-client during playback;
- filter events for the followed POV or current free-fly PVS;
- drive a free camera, switch followed players on attack, and leave follow mode
  on jump.

MVD 1.2 activates and extends that path:

1. `recordMVD` marks the game as server-demo recording and writes map plus
   initial network state.
2. Every game reliable is written once with its signed target, exclusion, or
   multiplayer-instance route. Unreliable target, PVS-area, and instance/PVS
   routes remain inside the game snapshot queue.
3. After authoritative server frames, the game module records periodic delta
   snapshots containing networked entities from every active instance rather
   than silently limiting tournament arenas to instance 0.
4. A clean stop writes an end record, flushes the file, closes it, and renames
   the staged `.mvd.part` file to `.mvd`.
5. `playMVD` loads the multiplayer game module when necessary, restores the
   recorded map and initial state, and advances the pseudo-client from checked
   records.

The session layer supplies the format-neutral library and player:

1. It enumerates `.mvd`, `.demo`, `.cdemo`, `.netdemo`, `.part`, and `.ucmd`
   files under `demos/`, including package-backed read-only files.
2. It uses bounded structural probes for render/command demos and checked MVD
   metadata rather than trusting the extension alone.
3. It assigns explicit capabilities to each entry and exposes only those
   actions in the GUI.
4. It routes native MVD, render-demo, and command-demo playback to their
   existing typed engine entry points.
5. It keeps unsupported retail and incomplete files visible with a useful
   status instead of trying to feed them to the wrong decoder.
6. It deletes only loose files that resolve beneath the save-path `demos/`
   directory; package-backed files remain read-only.

## Format and compatibility contract

The current writer produces `OQ4MVD` format `1.3`. The reader supports the
major-1 compatibility family, including formats `1.0` to `1.2`.

### Header

The header contains:

- an eight-byte magic value and byte-order marker;
- header byte length, allowing later versions to append fields;
- container major and minor versions;
- required and optional feature bitsets;
- Quake 4 network protocol major/minor;
- a game compatibility slot and simulation rate;
- recording start frame/time and snapshot interval;
- declaration/content checksum;
- an optional timeline-index record offset (`0` when absent);
- CRC-32 over the complete header except its CRC field.

A reader accepts the same container major even when the recorded minor is
newer, provided every required feature bit is supported. A different major is
rejected.

The 32-bit game compatibility slot deliberately stays in the same header
position:

- format 1.0 and 1.1 interpret it as the raw `GAME_API_VERSION`; playback uses
  an explicit allowlist of stream-compatible writers (currently APIs 39, 42,
  44, and the current API), rather than treating every older ABI as safe;
- format 1.2 and newer interpret it as a packed MVD game-schema
  `major:minor` (`major << 16 | minor`) and ask the game module whether that
  schema is compatible.

This avoids rewriting the established major-1 header while decoupling future
recording compatibility from unrelated game API changes.

API 39 is explicitly retained because API 40 only appended the versioned MVD
callbacks; APIs 42 and 44 are retained because their later lifecycle and
presentation-interface revisions did not change the version-1 snapshot or
reliable-record grammar. Future legacy API exceptions must be reviewed and
added individually.

### Records

Every record has its own:

- sync marker and extensible header length;
- type, record version, and flags;
- bounded payload length;
- payload CRC-32;
- record-header CRC-32.

The record header is length-delimited before allocation. The current reader
caps a header at 256 bytes and a payload at 16 MiB.

Current record types are metadata, map state, initial network state, routed
reliable message, full-world snapshot, optional timeline index, and clean end.
Map state, network state, reliable messages, and snapshots are marked required.
Format 1.3 adds five optional record types: user-info update (12), keyframe
(13), structured marker (14), marker directory (21) and keyframe directory
(22).

An unknown optional record is skipped using its declared length. An unknown
required record, required record version, flag, or required feature bit stops
playback before its payload reaches game code. Existing record semantics must
never be silently changed; a changed payload gets a new record version.

Reliable and snapshot records have two supported schemas. Version 1 is retained
for format 1.0/1.1 playback. Version 2 is written by format 1.2 and adds the
instance byte required to route private arena traffic without leaking it into
another arena. Metadata, map state, network state, index, and end records remain
at record version 1.

Format 1.2 requires both `FULL_WORLD_INSTANCES` and `INSTANCE_ROUTING` feature
bits. A purported 1.2 file missing either bit is rejected rather than being
accepted as a misleading partial-world recording.

### Format 1.0 to 1.3

The supported minor versions are intentionally incremental:

- A 1.0 recording has no timeline-index record and leaves its header index
  offset at zero. Its reliable and snapshot records use version 1. The current
  reader validates the stream and reconstructs its timeline by scanning checked
  snapshot records.
- A 1.1 recording can append an optional sparse index containing the first
  snapshot, approximately one entry per second, and the final snapshot. Each
  entry stores game time, file offset, and sequence, followed by the indexed
  end time. The header points to that record and the index is validated against
  bounded, strictly increasing times, offsets, and sequences.
- The index record is optional and length-delimited. Readers that do not know
  it can skip it; readers must not make it a prerequisite for decoding the
  unchanged major-1 snapshot stream.
- A 1.2 recording retains that optional index and writes reliable/snapshot
  record version 2. It records every active world instance and carries explicit
  instance routing for reliable and PVS-area unreliable messages.
- A 1.3 recording keeps the 1.2 stream unchanged and adds only optional
  records, advertised by the optional feature bits `USERINFO`, `MARKERS` and
  `KEYFRAMES`. A 1.2 reader skips them by their declared lengths and plays the
  same snapshots and reliables. The game schema stays 1.0: the snapshot and
  reliable grammar did not change, and the keyframe baseline carries its own
  version.

The 1.1/1.2 index is a timeline directory, not a random-access checkpoint. It
provides a bounded, quick browser/startup summary without allocating an entry
for every snapshot. MVD snapshots remain a delta chain. Forward seeking
consumes records without real-time delay. Without keyframes, a backward seek
resets the recorded map and initial network state, rewinds the stream, and
replays from the beginning under the per-frame `mvd_seekBudgetMS` work budget.
This replay-from-start path remains the compatibility fallback for 1.0 to 1.2
recordings and for any keyframe that cannot be restored.

### Keyframes, 1.3

Every `mvd_keyframeInterval` seconds (default 10, `0` records none) the
recorder writes a keyframe record immediately after a snapshot. A keyframe is
independently restorable because it holds three blocks, each length-delimited
and validated before anything is applied:

1. the map state a recording started at that moment would write, with the
   current user information of every slot and passwords removed;
2. the game's network info (`WriteNetworkInfo`): the player list with spawn
   identities, the start state and the game-state message, the same blocks the
   stream's initial network-state record holds;
3. the game's delta baseline (`ServerWriteServerDemoBaseline`, grammar version
   1): the recorder's acknowledged entity states merged with the snapshot just
   written, including stale states of freed entity slots, and that snapshot's
   entity PVS words.

The third block is what makes the jump exact. The recorder encodes the next
snapshot as a delta against precisely that table; a reused entity slot is
encoded against its stale predecessor. Playback installs the table as the
acknowledged state of an empty pending snapshot with the keyframe's sequence
(`ClientReadServerDemoBaseline`), so the next recorded snapshot applies it and
decodes against the same bytes the recorder used. Entities present at the
keyframe are spawned or updated from their states as a snapshot would; absent
ones are staled the same way.

The keyframe also records the snapshot sequence, the reliable count and the
number of records before it, so the end record's counts still validate after a
jump. A trailing keyframe directory between the timeline index and the end
record lists each keyframe's time, file offset and sequence; the indexed
playback path already reads that region for the end record. A recording
without a usable directory collects keyframes while it validates the stream.

`mvdSeek` uses the latest keyframe at or before the target for a backward seek,
and for a forward seek when that keyframe is at least 20 seconds ahead (a map
reload costs more than replaying a shorter gap). It then replays forward from
the keyframe to the exact target. A keyframe whose record, directory entry,
network info or baseline fails validation is refused before the world resets;
a failure after the reset falls back to replay from the start. Either way the
directory is dropped for the rest of the session. `mvd_useKeyframes 0` forces
the replay path for comparison.

A keyframe restores what a recording started at that moment would restore.
Client-side effects, chat already shown and events queued before the keyframe
are not part of it, which matches the start of any recording.

### User-info updates, 1.3

The engine broadcasts user-info changes outside the game's reliable channel, so
earlier recordings stored only the user information present when recording
started. A player who joined or renamed later reached playback without a name
and could not be followed by name or slot. Format 1.3 records each broadcast as
an optional user-info record, applied in stream order during playback.

### Structured markers, 1.3

The multiplayer game publishes markers while the server records, through the
append-only `idNetworkSystem::ServerRecordMVDMarker` (`serverMVDMarker_t`).
Each marker has a kind, an optional player, an optional instance, two values
and up to 120 bytes of printable text:

| Kind | Source | Values |
|---|---|---|
| `phase` | match phase change | previous phase, new phase |
| `round` | round state change | previous state, new state |
| `pause` | timeout or technical pause state change | new pause state, kind and owner side |
| `score` | team score change in team modes | team, new score |
| `kill` | every death | client is the attacker (-1 world or suicide), victim, method of death |
| `flag` | CTF and One Flag flag state change | client is the carrier, flag team, new state |
| `powerup` | a player gains an item powerup | client is the holder, powerup |
| `note` | `mvdMark <text>` from the console or rcon | text |

`idMultiplayerGame::PublishMVDMarkers` compares the match state it observed in
the previous server frame with the current one (`MPMVDMarkerDiff`, a pure
function in `mp/MvdMarkers.h`); kills come from `PlayerDeath`. The first
recorded frame only primes the comparison. The recorder stamps each marker with
the server frame and time, keeps at most 20,000 per recording and writes them
again as a marker directory when it finalizes. `mvdRecover` rebuilds both
directories from the records it salvages.

### Auto-tracking

`demo_autoTrack` chooses whom the playback camera follows: `1` moves to the
killer of the followed player 1.5 seconds after the death, `2` follows the
action (a flag carrier first, then active powerups, kills in the last ten
seconds, the score leader and living players). Action mode holds each choice
for three seconds and switches only for a clear lead. `demo_autoTrackTeam`
limits either mode to one team. Choosing a camera by hand, or attack or jump in
the playback view, turns auto-tracking off. The logic lives in
`mp/DemoAutoTrack.h`; the engine's follow-restoration loop adopts a camera the
game selected itself.

A seek replays records without running game frames, so client-only debris
spawned during it (gibs, brass and BSE debris, all `rvClientMoveable`) never
moves or reaches its fade-out before the seek lands. It used to stay frozen
where it spawned: clusters of gibs inside a body, often at the death camera,
which was the distorted foreground debris of the Red Rover seek finding. The
engine now publishes the read-only `mvd_seeking` while a seek replays, and a
client moveable spawned then removes itself on the first event service. The
presentation frame that ends the seek runs that service before anything is
drawn. After a seek, debris from deaths just before the target is therefore
missing rather than frozen. `tools/tests/mvd_seek_debris_contract.py` guards
the flag and the hook with five failing controls; the
[24 October probe](#red-rover-seek-debris-24-october-2026) measured it.

The index remains optional even for a clean 1.2 recording. In particular, a
recording stopped before its first scheduled snapshot has no index, may report
zero snapshots, and is still a valid completed stream when its initialization
records and end counts agree. End-record validation counts the entire stream,
including initialization records, so no-index compatibility does not weaken
the clean-end check.

### Network and content compatibility

Container compatibility does not imply snapshot compatibility. Playback also
requires:

- the same Quake 4 network protocol major;
- read-only acceptance of the recorded protocol minor by
  `idGame::IsDemoProtocolCompatible` while inspecting the library, followed by
  `idGame::ValidateDemoProtocol` when playback starts and commits the game
  module's protocol state;
- the legacy game API match for format 1.0/1.1, or acceptance of the packed
  game schema by `idGame::IsMVDSchemaCompatible` for format 1.2+;
- the same simulation/user-command rate;
- the recorded declaration checksum by default.

`mvd_enforceContent 0` is an explicit diagnostic override for the last check.
It is not a promise that mismatched assets or declarations can be replayed
correctly.

Before native game parsers receive data, openQ4 validates the MVD
network-state structure, routed reliable-message envelope, snapshot queue
lengths, entity/client ranges, ordering, and record timeline. The matching
game-module readers also reject malformed nested messages and invalid
multiplayer indices. This is part of the format boundary: checksums prove that
bytes are unchanged, while structural validation proves they are safe and
coherent enough to decode.

The network-state start state is game-owned: each client record carries a
`POWERUP_MAX`-bit powerup mask. That width grew from 16 to 18 bits when The
Awakening's powerups were added (27 September 2026, without a schema bump), and
the engine validator, which assumed 16, refused every later recording with two
or more players ("invalid MVD multiplayer start-state client 4": client 1
misread two bits early). The engine now accepts the width that parses as
exactly the recorded player list (`MVD_StartStateMatchesPlayers`), and
`idMultiplayerGame::ClientReadStartState` probes 18 then 16 bits before reading
(`MP_StartStatePowerupBits`), so both generations of recordings play.
`tools/tests/mvd_start_state_contract.py` executes both production checks for
one to six players at each width, plus malformed start states, with
single-reversal controls.

Recorded map-state cvars are also treated as untrusted input. Only names
currently registered with `CVAR_NETWORKSYNC` may be restored; an arbitrary
recorded dictionary entry cannot set a local renderer, filesystem, console, or
developer cvar.

### Failure and privacy behavior

- New recordings use `.mvd.part`. Only a stream with a valid end record is
  atomically renamed to `.mvd`.
- A failed or crashed recording remains visibly partial and is never presented
  as complete.
- CRC failure or truncation stops at the damaged record.
- Snapshot, record, file-size, duration, and path limits bound resource use.
- Playback accepts at most 256 records while locating map/network
  initialization and processes at most 4096 stream records in one displayed
  frame. The separate seek time budget still limits replay work across frames.
- User-provided recording names are reduced to a safe basename and never
  overwrite an existing recording.
- Server and user state required for playback is recorded, but password keys
  are removed from server info. Rcon credentials are not part of the stream.

## Current operator interface

Select **Demos** on the main menu or run `demoMenu`. The browser offers
**All**, **Multi-view**, **Render**, **Legacy**, and **Incomplete** filters,
with name, detected type, map, date, duration, status, and selected-file
details. Enter or **Play** starts a supported entry. **Delete** is offered only
for a loose save-path file and always asks for confirmation.

During supported playback a compact status overlay shows the name, time,
duration, rate, and active view. Escape opens the interactive controls and
pauses playback. Closing the controls resumes only if the demo was running
before the menu opened.

The GUI provides:

- pause/resume and one-frame step;
- relative skips of -30, -10, +10, and +30 seconds;
- 0.25x, 0.5x, 1x, 2x, and 4x rate presets;
- MVD-only free roam and follow-next;
- previous and next marker, when the demo has markers;
- an auto-track button that cycles off, killer and action;
- stop and close.

The format-neutral console commands are:

| Command | Purpose |
|---|---|
| `demoMenu` | Open the library, or playback controls when a demo is active. |
| `demoPause [0\|1]` | Toggle pause, or set it explicitly. |
| `demoSpeed <0.05-16>` | Set playback rate. |
| `demoSeek <seconds>` | Seek to an absolute time from the demo start. |
| `demoSkip <seconds>` | Seek by a signed relative interval. |
| `demoStep` | Pause and advance one frame. |
| `demoFollow [next\|prev\|free\|<slot>\|<name>]` | Follow the next or previous eligible player, a recorded client slot, or a player by name when the active backend is MVD. No argument keeps "next". |
| `demoFreeRoam` | Enter free camera when the active backend is MVD. |
| `demoStop` | Stop the active demo. |

The MVD-specific interface remains available:

- `recordMVD [name]`, `stopMVD`, `playMVD <name>`, `mvdInfo <name>` and
  `mvdRecover <name.mvd.part>`;
- `mvdPause [0|1]`, `mvdSeek <seconds>`, `mvdSkip <seconds>`, and
  `mvdSpeed <0.05-16>`;
- `mvdStep [frames]`, `mvdFollow [next|prev|free|<slot>|<name>]`,
  `mvdFollowNext`, `mvdFollowPrev` and `mvdFreeRoam`;
- `mvdStatus`, which also reports an active recording
  (`MVD_RECORDING path= staged= bytes= elapsed= snapshots= reliable= records=`)
  and the last recording result (`MVD_LAST_RESULT state= reason= final= partial=`);
- `mvdMark <text>` adds an operator note to the active recording;
  `mvdMarkers [kind]` lists the playing demo's markers (`MVD_MARKER index= time=
  kind= client= value0= value1= text=`), and `mvdMarker next|prev [kind]` (also
  `demoMarker`) seeks to the next or previous marker, `mvd_markerLeadMS`
  (default 2000) before it;
- `mvdAutoTrack [off|killer|action] [any|marine|strogg]` (also
  `demoAutoTrack`, which cycles the mode without arguments);
- `mvdLibrary`, which reports the writable library's recording count, bytes,
  configured quota and `automaticDeletion=0`.

`DEMO_FOLLOW_PREV` walks the same eligibility test as `DEMO_FOLLOW_NEXT`
backwards (`idGameLocal::SetDemoFollowClient`). A direct slot or name first
requires the slot to hold a recorded player (`GetUserInfo`); the game's existing
camera revalidation still releases a target the recording does not allow. Name
lookup is an exact colour/case-insensitive match, else the single containing
name; several containing names are reported as ambiguous. The overlay names the
followed player and falls back to its slot number.

`mvdRecover` salvages an interrupted recording. It requires an idle recorder,
copies the source header and every complete, checksum-valid record up to the
first truncated or corrupt one into a staged stream, discards any stale index or
end record from a failed finalize, rebuilds the timeline index exactly as
`CaptureServerFrame` would, appends the end record `StopRecording` would have
written at that point and publishes `<stem>_recovered.mvd` (or the next free
numbered name). The source is never modified, no existing demo is overwritten,
a stream without a complete snapshot is refused, and a failed recovery removes
its staged output. `tools/tests/mvd_recovery_contract.py` executes the
production reader, writer and `Recover` at every cut point of a synthetic
recording plus corrupt, stale-index and damaged-header cases, with
single-reversal controls.

Attack still switches to the next active player and jump leaves follow mode
for free flight. Quake 4-compatible aliases are `recordNetDemo`,
`stopNetDemo`, and `playNetDemo`. They create or read `.mvd`; they do not claim
legacy `.netdemo` compatibility.

Relevant MVD cvars are:

- `mvd_scale` and `mvd_paused` hold playback rate and pause state;
- `mvd_seekBudgetMS` bounds replay-seek work per rendered frame;
- `mvd_keyframeInterval` sets the keyframe spacing and `mvd_useKeyframes`
  lets seeks use them; `mvd_markerLeadMS` is the lead-in before a marker;
- `demo_renderSeekBudgetMS` is a cooperative render-demo replay budget checked
  between stream commands. An indivisible renderer command or map load can
  exceed it;
- `mvd_snapshotDelay` defaults to 50 ms;
- `mvd_maxSnapshotMB`, `mvd_maxSizeMB`, and
  `mvd_maxDurationMinutes` bound recording resources;
- `mvd_libraryQuotaMB` optionally bounds new recordings against existing writable
  library storage; `0` (the default) disables this extra limit;
- `mvd_enforceContent` controls the declaration-checksum gate.

### Library retention and quota, 28 October 2026

Recordings are retained until an operator explicitly archives or deletes them.
This preserves competitive-series journal links. There is no age-based or
oldest-first pruning, and standard builds do not download recordings or assets
from servers. Use a separately administered file transfer for sharing demos;
`mvdLibrary` and recording status are available through the console/rcon.

With `mvd_libraryQuotaMB` enabled, admission counts `.mvd` and `.mvd.part` files
recursively under this profile's writable `demos/`, including nested competition
recordings, malformed files and interrupted streams. Read-only search paths do
not consume this writable-library budget. An unreadable, linked or excessively
large directory tree refuses admission instead of silently undercounting it.
The recorder freezes the remaining budget when it starts, reserves index/end
overhead and refuses a record before it would cross that budget. A quota stop
retains its complete prefix as a recoverable `.mvd.part`; `mvdStatus` reports the
failure and path. Existing files are never removed to make room.

This is a recording-admission policy for one recorder per writable profile,
not a filesystem-wide reservation: external writers and files added after
admission are not tracked. `mvdRecover` is an explicit salvage operation and
can create a second copy beyond the quota; archive space first if needed.
Changing the CVar during a recording applies to the next recording.

Native fixtures cover nested/partial/Unicode names, linked roots, exact footer
boundaries and overflow. Production recording failure cases cover quota stops
with the per-file cap disabled. The staged GL smoke in
`.tmp/mp-completion-20261028/mvd-quota/` preserves a synthetic 2,031,616-byte
occupancy file, refuses a 1 MiB quota without opening a recording, then admits
64 KiB under a 2 MiB quota. The stopped library contains 2,096,741 bytes, below
its 2,097,152-byte limit. Recovery retains the source and produces a playable
demo; its engine render-target capture was inspected. The report hashes the
runtime before and after and records exit 0. Repeat with
`tools/tests/mp_mvd_quota_smoke.py --basepath <retail-root> --output-dir <new-dir>`.

## Playback capability matrix

| Library entry | Play | Pause/rate | Seek/step | Camera | Notes |
|---|---:|---:|---:|---|---|
| Native openQ4 `.mvd` 1.3 | Yes | Yes | Yes, through keyframes, then replay | Full-world free roam, player follow, markers and auto-tracking | Markers, keyframes and user-info updates are optional records. |
| Native openQ4 `.mvd` 1.0-1.2 | Yes | Yes | Yes, replay based | Full-world free roam, player follow and auto-tracking | 1.0/1.1 retain legacy instance-0 capture semantics; protocol/content checks still apply. |
| Native or historical openQ4 render `.demo` | Yes | Yes | Yes, replay based | Fixed recorded camera | Presentation stream; it has no off-camera game state. A v11 campaign demo spans single-player levels as segments whose starts are markers ([campaign render demos](#campaign-render-demos-v11-7-november-2026)). |
| Retail Quake 4 render `.demo` | No | No | No | Recorded camera only in the original engine | Detected as legacy; the byte-oriented retail stream needs a separate decoder. |
| Retail Quake 4 `.netdemo` | No | No | No | Depends on original client/server recording | `NDMO` metadata is shown, but retail packet protocols are not decoded. |
| openQ4 `.cdemo` | Best effort | No | No | Simulation-defined | Unversioned developer artifact; safe only with the matching engine/game build. |
| `.mvd.part`, `.part`, or `.ucmd` | No | No | No | None | Shown as incomplete rather than a healthy recording; `mvdRecover` publishes the complete prefix of a `.mvd.part` as a new demo. |

Render-demo wrapper compatibility includes the current `openQ4 RDEMO` and
historical `OpenQ4 RDEMO` spellings. `Quake4 RDEMO` is ambiguous: early
openQ4 and retail Quake 4 both used it. The decoder accepts it only when the
decoded first token proves it is the 32-bit openQ4 stream; a retail
byte-oriented stream is reported as unsupported.

Render demos can pause, change rate, step, and seek by consuming their command
stream. Rewinding restarts that render demo and replays commands with sound
muted until the target. Replay is advanced incrementally on the main thread
under the cooperative `demo_renderSeekBudgetMS` budget; the restart is deferred
so the seeking state can be presented first, although an indivisible map load
or renderer command can still take longer than the budget. Repeated seek
requests retarget the active operation, and mute state is restored on
completion, cancellation, failure, or stop. It cannot manufacture free-roam
or player-follow data.

Legacy render-demo decoding is best effort but no longer trusts the old stream.
Top-level tokens, hash-string tables, renderer payloads, indices, and sound
commands use exact checked reads and bounded values. A truncated or malformed
renderer or sound command closes the demo and returns to the menu with a
warning instead of escalating corrupt playback input into a fatal engine
error. Savegame sound-state failures retain their existing fatal policy; only
the demo-input path is made recoverable.

Command demos are intentionally not presented as portable recordings. Their
raw, unversioned command log and consistency checks make them sensitive to
engine/game changes; the unified UI exposes only play and stop. The browser
performs a bounded, nonfatal header and command-frame preflight before enabling
Play, and playback repeats that validation before and after map load.

## Historical delivery scope

This phase record is retained for provenance. MP-05 in the roadmap owns remaining
recording, recovery and playback work; OPT-04 owns the deferred broadcast scope.
Use the current status and dated evidence to distinguish implemented features
from unfinished work instead of treating every historical bullet as outstanding.

### Phase 1: reliable core — complete

- Versioned, extensible, checksummed container.
- Server-side full-world capture across every active instance and reliable/
  unreliable audience routing.
- Staged finalization and bounded reads/writes.
- Map/initial-state restoration and pseudo-client playback.
- MVD 1.0/1.1 playback plus the 1.2 instance-aware writer, optional timeline
  index, and bounded replay-from-start seeking.
- Free-fly/follow controls, speed, pause, step, and info/validation command.
- Dedicated/client builds, gameplay round trip, corruption test, and source
  contract test.

### Phase 2: tournament operations

- `mvd_autoRecord` policies for warmup, countdown, match start/end, map change,
  and dedicated-server restart.
- Collision-resistant names built from UTC time, event, game type, map, round,
  and sanitized player/team names.
- Server-side list, retention, quota, and download policy.
- Structured markers for match transitions, timeouts, scores, and admin notes.
- A recovery command that scans a `.part`, verifies complete records, writes a
  recovery end marker to a new file, and never modifies the source. Implemented
  23 October 2026 as `mvdRecover` (see the operator interface above).
- Console/Rcon status reporting and explicit reason codes for automatic stops.
  `mvdStatus` now reports the active recording and the last result's state,
  reason and paths; automatic-stop reason codes were already in the result ABI.

### Phase 3: competitive playback UX — foundation complete

- Unified demo browser with format/status probing, filters, details, safe
  deletion, and explicit incomplete/read-only handling.
- Capability-gated playback overlay for MVD and render demos.
- Pause, slow/fast playback, single-step, relative skip, absolute seek, and
  bounded restart/replay rewind.
- Explicit MVD follow-next and free-roam actions alongside attack/jump.
- Historical openQ4 render-wrapper recognition and clear rejection of retail
  render and network demo streams.

Competitive playback work delivered since: previous, slot and name follow
(23 October 2026), and structured markers, killer/action/team auto-tracking
and independently restorable keyframes (MVD 1.3, 2 November 2026). Remaining
historical scope, deferred by the 28 October scope decision or OPT-04:

- a named player list in the playback controls;
- CPMA-style highlighted primary view and multi-window `viewall` layouts with
  deterministic audio ownership;
- reverse-scrub presentation, timedemo/AVI integration, and generated
  thumbnails.

### Phase 4: broadcast and ecosystem

- A separately versioned live transport for Q4TV/GTV-style repeaters. The disk
  container remains the archive boundary; relay framing, authentication,
  backpressure, and reconnect semantics are not smuggled into the MVD 1.x
  container.
- Delayed spectator feeds, multiple downstream viewers, and record-while-relay.
- Optional chunk compression as a new feature/record version with strict
  decompressed-size limits.
- Metadata export for tournament tools without requiring full game playback.

## Red Rover seek debris, 24 October 2026

The original Red Rover recording behind the seek/debris finding no longer
exists, so `.tmp/mp-round3/rr_seek_probe.py` makes a new one. A listen server
hosts Red Rover on q4dm1 with five bots; the host plays without god mode, so it
dies and gibs too. The recording is 95.5 s (363 KB) and has two live rounds.
Playback follows the host. The probe compares continuous playback, paused at
four checkpoints, with the same times reached by seeking. `listClientEntities`
lists the client-side entities at each stop; it now names each entry's real
class and origin (it used to print `rvClientEntity` for everything and
dereferenced empty slots).

- Before the fix, the seek frames matched continuous play except where a death
  fell shortly before the target. There the seek left nine `rvClientMoveable`
  gib chunks per gibbed player in a cluster about 20 units across, where
  continuous play had already thrown them and faded them out. In the first run,
  a rewind to the first checkpoint followed by a jump to the third showed one
  such cluster at the followed player's death camera: a large, distorted gib
  filling the foreground, the shape of the original finding.
- Fixed-time grid, 14 forward seeks every 6 s from 12 to 90 s, on the same
  recording. Without the removal hook, six seeks left 9 to 18 frozen client
  moveables, still there at the next seek because their fade-out never fired.
  With it, no seek left any. Reports: `.tmp/mp-round3/runtime/rr1/report.json`
  (`grid_before`, `grid_after`).

Still open: after a paused seek the followed player's first-person weapon model
is not drawn until playback runs. In the first run, both seek frames at the
first checkpoint also lacked one nearby player that continuous play showed.
The rewind frame also lacked the map's light-shaft effects, because no frames
had run since the map reload.

## Paused seek weapon catch-up, 28 October 2026

The retained 24 October Red Rover recording reproduces the missing first-person
weapon at living-player checkpoints. A render definition alone was insufficient
evidence: it existed, but the weapon was still raising below the camera. The
snapshot reader had requested catch-up; `idPlayer::UpdateWeapon` recreated the
weapon and cleared that request before `Weapon_Combat` could honour it.

Initialization now calls the existing `NetCatchup` state when requested, before
clearing the flag. Normal spawns keep their raise animation. The executable
`mvd_seek_weapon_state.py` runs the production initialization block, verifies
once-only catch-up and absent/dead-player admission, and rejects the old code.

`mp_mvd_seek_smoke.py` checks paused forward/backward seeks at 12, 26, 46, 66,
86, 12 and 46 seconds in the same recording. It checks exact playback time,
unchanged pause/follow state, ready visible weapons for living loaded players,
engine screenshots and absence of frozen client moveables. The OpenGL runs
pass under `.tmp/mp-completion-20261028/mvd-catchup-gl2/` and
`mvd-final-gl/`; their reports bind the tested binaries and recording. The
inspected 46-second OpenGL capture shows the machinegun again. Earlier
`mvd-baseline/` captures preserve the missing-weapon control; its original model
presence check alone did not detect the missing visual, so it is not a pass for
this corrected gate. The directories `mvd-catchup-vk/` and `mvd-final-vk/` do
not qualify Vulkan: the harness incorrectly passed `vk`, which selected OpenGL.
The corrected probe passes `vulkan` and verifies renderer activation/gameplay
in the engine log, as well as rejecting runtime mutation during a run.
All seven corrected Vulkan checkpoints pass under
`.tmp/mp-completion-20261028/mvd-qualified-vulkan/`, including the actual API
reported at each capture. The inspected 46-second engine capture shows the
machinegun and clean scene. An intermediate `mvd-final-vulkan/` run retains a
test failure: it required a shadow-map log line even when shadow maps were
disabled; the renderer itself activated and captured correctly.

This resolves the paused-seek weapon finding. Independent keyframes, transient
effects reconstruction, structured markers and the other MP-05 scope
remain separate acceptance gates.

## MVD 1.3 qualification, 2 November 2026

Windows x64, OpenGL, debugoptimized build of the MVD 1.3 change, original
Quake 4 assets. Runs are under `.tmp/wt-mp05-tools/` (`mvd13_probe.py`,
`ded_record.py`, `old_reader.py`); every launch is hidden and driven by console
scripts, with no operating-system input.

Recordings:

- `run1`: listen server, CTF on q4ctf1, five skill-3 bots, 105.6 s, two
  `mvdMark` notes. 564 KB, ten keyframes, 14 markers (9 kills, 3 powerups,
  2 notes).
- `run2`: the same plus a bot that joins 30 s into the recording. One
  user-info record; 12 markers; ten keyframes.
- `ded1`: dedicated server, Team DM on q4dm1, five bots and a late joiner,
  cheats off. 76.7 s, 20 markers, seven keyframes, one user-info record.

Results:

| Check | Result |
|---|---|
| Keyframe seeks against full replay (`run1`, seven paused targets: 24, 47.5, 71, 96, back to 38 and 12, forward to 88 s) | Four keyframe restores, no fallbacks. At every target the match state, every player's team, health, score and spectating flag, and the 881-885 entity numbers and definitions match the replay-from-start run exactly. Living players' poses match exactly. |
| The two pose differences | Both are one player who had died two seconds earlier (health -13 and -7). A corpse's ragdoll runs on the client, and its pose also differs between full replay and a fresh forward replay of the same target, so it is not keyframe state. |
| Markers (`run1`) | `mvdMarker next` six times walks markers 0-5, each landing exactly 2,000 ms before its marker; `prev` returns to marker 4; `next note` reaches the first note. |
| Auto-tracking (`run1`, `run2`) | Action mode follows recent killers and powerup holders and holds each choice at least three seconds. In killer mode the late joiner killed the followed player at 47.7 s and the camera moved to them 1.5-2 s later. A manual `mvdFollow next` turns it off. |
| Late joiner (`run2`) | `mvdFollow Cortez` follows slot 6 by name after a replay seek to 70 s and after a keyframe seek to 45 s. |
| Older reader (`run2` played by the 1.2 main runtime) | `mvdInfo` validates format 1.3 and skips 25 optional records; playback seeks to 70 s and back to 20 s without errors, and cannot follow the late joiner ("unavailable"), the defect the user-info record fixes. Its content checksum differed from this build, so this diagnostic ran with `mvd_enforceContent 0`. |
| Dedicated recording (`ded1`) | Pause, 2x speed, keyframe seeks to 75, 25 and 52 s, follow by name of the late joiner and action auto-tracking all work. |

The dedicated run found a defect older than 1.3. Quake 4 treats every plain
cvar as a cheat, and playback restores the recording server's network-synced
cvars, including `net_allowCheats 0` from an ordinary server. `mvd_paused`,
`mvd_scale` and the timeline's `demo_uiSeek` then refused every change, so a
demo recorded on a server without cheats could not be paused or slowed, and
`mvdPause 1` printed "resumed". Every earlier qualification used listen servers
with cheats enabled. The viewer-side playback cvars are now `CVAR_NOCHEAT`;
none of them changes what was recorded.

Not established: phase, round, pause, score and flag markers at runtime (the
scripted capture stayed in warmup); those kinds come from the same per-frame
diff as the runtime powerup markers and are covered by the native fixture.
Vulkan, Linux and macOS playback of 1.3 recordings were not run.

Executable checks: `mvd_navigation_contract.py` runs marker, keyframe and
directory parsing, baseline validation, marker stepping, the marker diff and
auto-tracking with 15 single-reversal controls; `mvd_recovery_contract.py`
now also requires recovery to rebuild both directories from the surviving
records (2,365 cut points, six controls).

## Campaign render demos (v11), 7 November 2026

Roadmap Q4-09 owns this work. Before v11 a render demo ended at the first map
unload, so it could never span a level transition, a save load or a death
reload. Its frames also included every menu and loading screen, and it kept
only the 2D batch still pending at the end of a frame.

### Stream

v11 adds session records (`DS_SESSION`, `DemoFile.h`). Each is a dictionary;
readers skip unknown record kinds.

```text
DS_VERSION 12 (11 before joint prediction; both play)
DS_SESSION DSC_HEADER   game, campaign, mode, userCmdHz, skill, build
per segment (one level):
  DS_SESSION DSC_SEGMENT        index, frame, map, entityFilter, reason, skill
  -- both sides reset the hash-string table here --
  DS_SOUND  SCMD_STATE          the complete sound world
  DS_RENDER DC_LOADMAP          the level, then its portal states
  DS_SESSION DSC_SEGMENT_READY
  frames ... DS_RENDER DC_END_FRAME
DS_SESSION DSC_END              frames, segments
raw trailer (outside the compressed stream): payload | int length | uint crc32 | "OQ4DSUM1"
```

A segment depends on nothing before it: a fresh string table, the full sound
state, a level load that frees every definition, archive flags reset so every
visible entity, light and effect is written again, in-world GUIs rewritten, the
first frame's 2D written in full and, from v12, no joints predicted from an
earlier record ([joint prediction](#joint-prediction-v12-7-november-2026)). The writer finds hash strings through an
index instead of a linear search, and the per-segment reset keeps each table
far below the 65,536-string bound over a long campaign.

The trailer gives the library and the player the duration (frame count and
tic rate) and every segment's start frame, reason and map without decoding the
stream. Its payload is a dictionary under a CRC; a recording that was killed has
no trailer and is described as interrupted. `DSC_END` stops the reader before
the trailer's bytes.

`DC_GUI_SURFACE` (entity handle, GUI slot, 2D model) carries an entity's
in-world GUI as it drew, written only when its checksum changes.
`DC_GUI_MODEL` is now written surface by surface: a surface identical to the
same surface of the previous frame costs one byte. A changed one stores plain
vertices in 25 bytes rather than 68, and indices in two bytes. Readers before
v11 keep the old decoders.

### Recording

`recordDemo [name]` in a level opens the file (version, then header) and
records that level as the first segment. At the title or between levels it
**arms** the recording: the file opens, header first, with the next
single-player level. The states are armed, recording and suspended. `writeDemo` is
set only while recording, so the renderer and sound system write nothing while
a level loads or before the first level.

- `UnloadMap` suspends a single-player recording before teardown, so a level
  change, save load, death (`died`), restart or chapter start never ends it.
  A multiplayer recording keeps its one-map scope and stops there.
- `ExecuteMapChange` names the next segment's reason (start, level, load or
  restart) and, once the level is spawned, `ContinueRenderDemoAfterMapChange`
  begins the segment. A multiplayer join, MVD playback or demo playback ends
  the recording instead.
- `Stop()` (quit to menu, disconnect) ends a suspended recording; an armed one
  survives it, since a new game from the title may reload the game module first.
  Shutdown ends any recording. An armed recording opens its file with its first
  level, so one cancelled or quit before any level leaves no file.
- Only the gameplay branch of `idSessionLocal::Draw` ends a recorded frame
  (`WriteDemoEndFrame`); `EndFrame` no longer does. Menus, pauses and loading
  screens leave no frames, and the frame count equals the recorded game time:
  a recording runs exactly `USERCMD_PER_DEMO_FRAME` tics per frame. A long
  sound load (`timeHitch`) no longer skips tics under that fixed-tic check,
  which would have stopped the engine.
- `demoRecordStatus` prints
  `RENDER_DEMO_RECORDING state= path= frames= seconds= segments= map= elapsed=`.

### What the 2D record holds

The renderer flushes its 2D batch whenever a retained root begins, a render
target changes or a scene is captured. While a demo records,
`idGuiModel::EmitFullScreen` appends each batch flushed to the screen to a
frame model that `WriteDemoPics` writes. Batches drawn into a retained
composition layer (`_retainedLayerImageN`) are kept as that layer's content,
which resets with the layer's clear. A composite of the layer records as its
content, each vertex colour scaled by the composite's opacity, so grouped HUD
panels survive. Left out of the recording:

- post-process sorts, which compose the scene itself: the game presents its
  off-screen scene target with `postprocess/nopostprocess`, and playback renders
  the recorded scene directly;
- retained materials whose images exist only in the recording process: the
  coverage atlas, masks, generated pixels, blurred backdrops, model previews,
  the pause scene and layer-sampling linear masks.

While a demo records, the retained host stops using the coverage atlas, cached
layers, cached masked sources and frame replay, so the HUD reaches the
recording as vector geometry. The visible frame is the same.
Masks are not replayed: a masked HUD group (the marine HUD's slanted plates)
plays back as its unmasked content.

### Playback

- A segment resets the reader's string table and checks that its frame number
  matches the frames played. A new level shows the loading screen with the level's
  name (not while seeking, timing or capturing), stops the outgoing sounds and
  opens a media load (`declManager`, `renderSystem`, `soundSystem`, BSE) that
  `DSC_SEGMENT_READY` closes, purging what the previous level used and this one
  does not, in `ExecuteMapChange`'s order.
- The timeline counts frames at the recording's tic rate, so it continues across
  levels whose game clocks restart. Seeking is the existing restart-and-replay
  path, crossing segments as it goes.
- Level starts are markers: `demoMarker next|prev` and the playback deck's
  marker buttons. Previous returns to the current level's start unless it is
  within two seconds of it. The overlay names the level (`Level 2/3: <name>`).
- `StartPlayingRenderDemo` reads the header first and refuses a demo recorded
  in another game directory before stopping anything.
- In-world GUIs draw the recorded geometry in place of the empty GUI playback
  gives the entity.
- Sound commands may name emitter 0, the sound world's local emitter (music,
  narration, interface sounds). The reader resolved demo emitters through the
  savegame lookup, which rejects 0, so any recording that started a local sound
  stopped playing at that command; `DemoEmitterForIndex` now resolves it. A local
  sound already playing when a segment begins is still not restored: the sound
  state, shared with savegames, never stores that slot.

### Joint prediction (v12), 7 November 2026

Measured first, on a scripted recording of 16.7 seconds (airdefense1 for 8
seconds, then a change to airdefense2), uncompressed bytes by record: animated
entities' joints 28.9%, the 2D model 28.5%, sound commands 21.3%, effects 13.0%,
the other entity fields 5.9%, in-world GUIs 1.9%, the rest under 1%. The demo
file's LZW (a 12-bit dictionary) cannot compress raw joint floats at all: the
10.3 MB of joints in that recording still took 10.5 MB, half of the 20.4 MB
file. Each frame about fifteen animated entities rewrite every joint, and 78% of
those joints move from one frame to the next.

v12 writes an entity's joints (`WriteDemoJoints` / `ReadDemoJoints` in
`RenderWorld_demo.cpp`) as:

```text
byte mode
  0 raw         numJoints * 12 little-endian IEEE words (v11's layout)
  1 predicted   int payloadBytes
                a bit per joint, set when it moved since the handle's last record
                for each moved joint, 12 LEB128 varints of the zigzagged residual
                uint FNV-1a checksum of the decoded words
```

The prediction is the word last recorded for that handle, extended linearly by
the step from the record before it when both had this joint count. It works on
the words as 32-bit integers, never as floats, so every platform decodes the
recorded bits exactly, scale, signed zeros and NaNs included. Recording and
playback keep the same history per entity handle and both drop it at every
`DC_LOADMAP`, a segment start or map load. A recording also drops a handle's
history when the entity is freed and writes its next joints raw. Playback keeps
it across `DC_DELETE_ENTITYDEF`, which a recording writes before every rewrite
of an archived entity. A predicted record without a matching history, a
malformed residual, trailing bytes or a checksum mismatch stops playback with a
warning instead of showing a wrong pose.

The encoding was chosen offline from that recording's joints, compressed by an
exact model of the demo LZW: XOR against the previous record took 6.6 MB, XOR
with the bytes regrouped by significance 6.3 MB, first-order integer residuals
5.9 MB, linear residuals as varints with the moved-joint mask 5.4 MB. Rice coding
packs the same residuals into 4.6 MB, but the LZW expands near-random bytes by a
quarter, to 5.9 MB; varints keep a structure it can use. Quaternions were not
used: they are not exact, and Quake 4 joint matrices can carry scale.

Result on the same scripted clip recorded again (two segments, 500 frames, the
second level starting at frame 260 in both): 20.4 MB in v11, 15.4 MB in v12
(-24.5%), about 1.22 MB/s down to 0.92 MB/s. The 2D model, sound commands and
effects are now the larger share.

Validation, staged Windows client, hidden window, no input:

- `timeDemoQuit` played all 500 frames of the v12 clip on GL and on Vulkan, with
  every predicted record matching its checksum. The v11 recording of the same
  clip and the earlier three-level v11 demo (745 frames) still play in full.
- Paused at 6 s (level one) and 12 s (level two), the v11 and v12 recordings
  played in the same savepath show the same view and the same pistol and hand
  pose; what differs is smoke, fire and light flicker between two live
  recordings. Compare in one savepath: a savepath whose first run detected the
  balanced preset drew airdefense1's sky black for both demos.
- In an uncompressed v12 demo, one flipped residual bit stopped playback at that
  frame with "joints do not match their checksum", and a bad mode byte with
  "unknown joint record"; the client exited normally both times.

### Library

A v11 single-player entry shows **Campaign demo**, its first level, the date,
the duration from the trailer and `<campaign> / Levels: N`. The file being
recorded is listed as **Recording in progress** and cannot be played. A demo
from the other campaign is **Other campaign**, with Play disabled and the
reason. A demo without a trailer is **Interrupted recording**, still playable to
its last complete frame. `compressDemo` carries the trailer over.

### Validation, 7 November 2026

Staged Windows GL client, stock content, hidden window, no input; cases and
captures under `.tmp/campaign-demos/` (local only):

- Armed at the title: airdefense1 (start), `map game/airdefense2` (level) and
  `loadGame` back to airdefense1 (load): 3 segments, 745 frames, 33.5 MB.
  `timeDemoQuit` played all 745 frames through the three segments with the media
  purge reporting 41 and 9 models at the later segments.
- Real-time playback: `demoMarker next` twice reached each segment start; two
  `prev` presses during a seek stepped back two levels; `demoSeek 12` crossed
  back into the second level; the library showed the entry as playable with
  capabilities 159.
- Death through the restart menu (`kill`, then the attack usercmd from
  `openq4_testInput`): the recording suspended with no frames while the menu
  stood, resumed on `loadGame` as a load segment, and `disconnect` finalized it;
  all 319 frames played.
- Killed mid-recording: no trailer, listed as interrupted and playable; 181
  frames played before the reader stopped safely at the cut.
- A demo whose header names `q4xbase`: **Incompatible / Other campaign**, Play
  disabled, and `playDemo` refused without leaving the menu.
- HUD and screens: playback frames matched live captures for health, the weapon
  icon, crosshair ring, small icons, the in-world "transfer successful" terminal
  and the (unmasked) plates.
- Vulkan (`r_renderApi vulkan`) played the GL recordings identically: all 745
  frames of the three-level demo, and the same HUD capture.
- The Awakening (`fs_game q4xbase`, its content hard-linked from the user profile
  into the case's): armed at the title, `m01_stranarus_trench1` then a change to
  `m01_stranarus_trench2`, 2 segments and 406 frames. It first stopped playing at
  a local-sound command (the emitter-0 defect above); after the fix the library
  showed **Campaign demo**, Stranarus Trench, 00:13 and "Quake 4: The Awakening /
  Levels: 2", and all 406 frames played. Copied into base Quake 4 it was listed as
  **Other campaign** with Play disabled.
- Size: about 0.7 MB/s in a quiet corridor and 1.4 MB/s on airdefense1, where
  about fifteen animated entities rewrite their joints every frame. A changed
  2D surface is typically 1 of 10 per frame. v12's joint prediction takes a
  quarter off ([joint prediction](#joint-prediction-v12-7-november-2026)).

`campaign_demo_contract.py` pins the format, both ends, the joint coding, the
library and the strings. Remaining (Q4-09): Vulkan recording, Linux and macOS, a
long session with measured seek times and size, the 2D model, sound and effect
records that now dominate the size, masked HUD groups and local sounds already
playing at a segment start.

## Update policy

Future changes follow these rules:

1. Additive metadata or records that old readers may ignore use optional
   records and increment the container minor when useful for discovery.
2. A new required capability gets a required feature bit and a new record
   version. Readers lacking it fail before playback.
3. Breaking header, ordering, timing, or baseline semantics require a new
   container major. The old decoder remains available for the supported
   compatibility window.
4. Game-level MVD changes increment the packed MVD schema and teach
   `IsMVDSchemaCompatible` exactly which older schemas remain safe. They do not
   inherit compatibility merely because the wider game API happens to match.
5. Snapshot protocol changes increment the network protocol and teach the pure
   `IsDemoProtocolCompatible` query plus playback's state-committing
   `ValidateDemoProtocol` exactly which older minors remain safe.
6. Each supported major keeps golden complete, zero-snapshot/no-index,
   truncated, CRC-corrupt, unknown-optional, unknown-required, older-minor, and
   newer-minor fixtures. Major-1 fixtures cover both record-v1 legacy routing
   and record-v2 instance routing.
7. Release qualification includes dedicated recording, listen-server
   recording, joins/leaves, bots, every game type, targeted/excluded/PVS
   and instance-routed effects, map-stop finalization, playback POV switching,
   and cross-platform reads on Windows, Linux, and macOS.
