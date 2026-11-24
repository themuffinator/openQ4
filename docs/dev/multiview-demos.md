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

A render demo recording (stream v14) is a container of checked blocks with
keyframes: it plays what is intact of a file that was cut short or damaged, may
pass 2 GiB, takes about 40% of the space the v13 stream did, and seeks from
the last level start or keyframe before its target
([block container](#block-container-keyframes-and-large-files-v14-14-november-2026)).

The format is intentionally new. An openQ4 `.mvd` is not labelled as a legacy
Quake 4 `.netdemo`, because the latter is a packet-oriented client recording
with different compatibility and safety properties. Since MP-22's fourth round
a Quake 4 1.4.2 network demo (`.ndmo85`) is read, checked and written in memory
as MVD records, then played through the same player
([retail network demos](#retail-quake-4-142-network-demos-mp-22-23-november-2026)).

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

1. It enumerates `.mvd`, `.demo`, `.cdemo`, `.netdemo`, `.ndmo85`, `.part`, and
   `.ucmd` files under `demos/`, including package-backed read-only files.
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
  module's protocol state (2.42 accepts recordings made with 2.41 and reads
  their older player, projectile and entity-tag layout; see the
  [protocol record](mp-network-qualification.md#network-protocol-242-15-november-2026));
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
legacy `.netdemo` compatibility. `playNetDemo` and `playNetTimeDemo` also read
a Quake 4 1.4.2 `.ndmo85`.

Relevant MVD cvars are:

- `mvd_scale` and `mvd_paused` hold playback rate and pause state;
- `mvd_seekBudgetMS` bounds replay-seek work per rendered frame;
- `mvd_keyframeInterval` sets the keyframe spacing and `mvd_useKeyframes`
  lets seeks use them; `mvd_markerLeadMS` is the lead-in before a marker;
- `demo_renderSeekBudgetMS` is a cooperative render-demo replay budget checked
  between stream commands. An indivisible renderer command or map load can
  exceed it;
- `demo_recordKeyframeSeconds` (10) spaces a render demo's keyframes and
  `demo_useKeyframes` lets seeks start from them; `demo_recordMinFreeMB` (256)
  and `demo_recordMaxSizeMB` (0, none) end a recording cleanly before the disk
  fills or at a size; `demo_recordThread` packs its blocks on a worker thread
  and `com_compressDemos 0` stores them unpacked;
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
| Native or historical openQ4 render `.demo` | Yes | Yes | Yes: a v14 recording from the last level start or keyframe, then replay; older ones replay from the first frame | Fixed recorded camera | Presentation stream; it has no off-camera game state. A v11 campaign demo spans single-player levels as segments whose starts are markers ([campaign render demos](#campaign-render-demos-v11-7-november-2026)); a v14 one is a container of checked blocks ([block container](#block-container-keyframes-and-large-files-v14-14-november-2026)). |
| Retail Quake 4 render `.demo` | Yes | Yes | Yes, replay based | Fixed recorded camera | Its own reader ([retail render demos](#retail-quake-4-render-demos-mp-22-23-november-2026)); needs the recording's levels and effect declarations. |
| Quake 4 1.4.2 network demo `.ndmo85` | Yes | Yes | Yes, replay based | A client recording: the recorder's view; a server recording: free roam and player follow | Decoded into MVD records in memory; needs `si_fps` 60 and the recording's paks ([retail network demos](#retail-quake-4-142-network-demos-mp-22-23-november-2026)). |
| Retail Quake 4 `.netdemo` before 1.4.2 | No | No | No | Depends on original client/server recording | `NDMO` header shown; other protocols and layouts are not decoded. |
| openQ4 `.cdemo` | Best effort | No | No | Simulation-defined | Unversioned developer artifact; safe only with the matching engine/game build. |
| `.mvd.part`, `.part`, or `.ucmd` | No | No | No | None | Shown as incomplete rather than a healthy recording; `mvdRecover` publishes the complete prefix of a `.mvd.part` as a new demo. |

Render-demo wrapper compatibility includes the current `openQ4 RDEMO` and
historical `OpenQ4 RDEMO` spellings. `Quake4 RDEMO` is ambiguous: early
openQ4 and retail Quake 4 both used it. The first four decoded bytes read as
the int `DS_VERSION` in both, so the decoder looks further: an openQ4 stream
follows with a stream version from 1 to the current one, a retail stream with
its savegame version and then the bytes `DS_SOUND` and `SCMD_STATE`, which
every retail recording starts with. Anything else is refused as unrecognized.
A retail stream plays through its own reader
([retail render demos](#retail-quake-4-render-demos-mp-22-23-november-2026)).

Render demos can pause, change rate, step, and seek by consuming their command
stream. Rewinding a v14 recording opens the file at the last level start or
keyframe before the target ([block container](#block-container-keyframes-and-large-files-v14-14-november-2026));
rewinding an older one restarts it. Either then replays commands with sound
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
  network demo streams; retail render demos play through their own reader
  since 23 November 2026.

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
clearing the flag. Normal spawns keep their raise animation. (13 November:
live respawns and joins also requested catch-up, through a false state hitch
and new weapon entities, so they were caught up as well. Those now raise with
the server's weapon; a seek's catch-up still wins. See the
[network record](mp-network-qualification.md#the-weapon-for-a-new-life-raises-with-the-servers-13-november-2026).) The executable
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

## Client-view recordings (MP-22), 23 November 2026

A player who is not hosting records their own view in the MVD container.
Quake 4's game module still carries Raven's client net-demo state:
`idGameLocal::WriteNetworkInfo` and `ReadNetworkInfo` save and restore a
client's snapshot states, its PVS words, the players, the snapshots not yet
acknowledged (respawning every entity from its stored state) and the
multiplayer start state. The engine records around that.

**Format.** The header requires `MVD_FEATURE_CLIENT_VIEW` (bit 6) with
`RECORD_CRC` and `LENGTH_DELIMITED` and none of the server view's features, so
an openQ4 reader older than this refuses the file at its header; format stays
1.3 and a server recording's header is unchanged. The optional features are
the clean end and user info: no timeline index, markers or keyframes. Every
live record opens with the recording's own clock, `(frame, time)`: real time
since the start added to the client's frame and time when recording began. The
client's own game frame steps back whenever a snapshot resynchronizes it, and
a recording's timeline may only move forward.

| Record | Type | Payload after the clock |
|---|---|---|
| Network state | 3, version 2 | slot (u8); last snapshot's sequence, frame and time; the client's frame and time; the commands in flight from that snapshot's frame (count, then frame, client, length and `WriteUserCmdDelta` bytes each); then the game's client network info |
| Client snapshot | 30, required | the client's frame after reading it, then the whole unreliable snapshot message from the engine header on |
| Applied snapshot | 31, required | the sequence `SERVER_RELIABLE_MESSAGE_APPLYSNAPSHOT` named |
| Game reliable | 32, required | the message as the game reads it, after the engine's identifier (at most 8 KiB) |
| Recorder's command | 33, optional | frame, then `WriteUserCmdDelta` bytes |
| User info | 12 | as in a server recording: slot and dictionary (also an emptied one when a player leaves) |

The end record counts client snapshots as snapshots and applied snapshots and
game reliables as reliables.

**Recording.** `record`, `recordNetDemo` and `recordMVD` record a client view
when no local server is running and the client is in game. The hooks sit
where `idAsyncClient` hands data to the game: the snapshot after it was read,
the applied snapshot after it was applied, a game reliable before the game
reads it, user info after it is set, and the player's own command as it is
generated. A game-init message (a map change) or a disconnect finishes the
file. The game's demo state is never set on the recording client. A client on
a pure server cannot see loose files through the file system, its own demos
included, so a free recording name is also checked in the save folder on disk
(`MVD_PathExists`); before this a second recording under the same name could
have replaced the first.

**Validation.** Before the game reads a byte, `ValidateClientNetworkState`
walks the whole start state: every length, every entity number, and every
stored state's entity definition (the 20-bit spawn id, then the definition
number in `BitsForInteger( entityDefs ) + 1` bits, which must name a
definition this game has, as `ReadNetworkInfo` raises a fatal error
otherwise); the recorder must be among the players; the multiplayer part goes
through the server form's checks. The recorder validates its own start state
before writing it. The library summary and the playback index check every
client record's size and clock, and each view refuses the other's snapshot
and reliable records.

**Playback.** The game runs as a client in `DEMO_PLAYING` with no server
demo, as the recorder's slot (`idAsyncClient::GetLocalClientNum` asks the
player). Records are processed in file order as the playback clock passes
their stamps: snapshots through `ClientReadSnapshot` (then the other clients'
relayed commands into the command table, as the client did), applied
snapshots through `ClientApplySnapshot`, reliables through
`ClientProcessReliableMessage`, the recorder's commands into its table row.
Each frame repeats the client's prediction from the last snapshot's frame to
the recorder's frame at that point (the client frame stored with the snapshot
plus the time since its stamp), duplicating missing commands as
`DuplicateUsercmds` does, at most 64 frames a frame. Seeking replays from the
start; follow, free roam and auto-tracking are refused, and the library and
deck offer neither.

**Keyframes (second round, same day).** A client recording writes a keyframe
after a snapshot every `mvd_keyframeInterval` seconds, in its own optional
record type 34 with a server keyframe's layout: counts, then the map state, the
client network state (as the start record's), and a four-byte version word as
the third block, which a keyframe block may not leave empty. The playback index
notes them; a seek restores the nearest one through the same
`ApplyPlaybackNetworkState` path as the start, which now takes the game clock
from the restored state's client time, and replays the rest.

**Recovery, timedemo and packs.** `mvdRecover` counts a client recording's
snapshots and reliables and copies its keyframes without a directory.
`timeDemo <name>.mvd` (and `timeDemoQuit`) plays any MVD with one simulation
tic a frame, uncapped (`Common_GetRequestedPresentationCap`), and prints
`MVD_TIMEDEMO frames= seconds= fps= slowest= match= complete=`; it times the
same frames every run, so FPS work can compare builds on a recording. Every
recording's metadata now lists its packs (`FS_ListLoadedPacks`:
`<dir>/<pack>:<checksum>`); a content refusal compares them with the loaded
ones and names a changed, missing or extra pack, or says that every pack
matches.

**Autoaction.** `ui_autoAction` (a player) and `si_autoAction` (a server, in
its server info) take the words `demo`, `ss` and `stats`
(`src/game/mp/AutoAction.h`, with `MPBuildMatchBasename`:
`YYYYMMDD-HHMMSS_<mode>_<map>[_<player>]`). The player's side starts on the
local game's live edge (`rvGameState::GameStateChanged`, never during demo
playback) by buffering `record "<name>"`, quoted because the tokenizer splits
the name at its dash; the server's starts in `NewState(GAMEON)` with
`ServerStartMVDRecording` unless the match is managed. Both leave a recording
someone else started alone, a host whose server records records once, and two
seconds into the review take the screenshot, write `stats/<name>.json` and stop
the demo.

**Deck, export and library (third round, same day).** The deck's **Players**
button opens a framed list of up to 16 recorded players (`demo_player<n>_*`
GUI state, `demoFollowRow`), and `demoPlayers` lists them at the console.
`StepFrames` takes a negative count, so **Step back** and `demoStep -1` seek
one tic back through the keyframe before it. `demoExport <demo>.mvd [fps]
[quit]` plays like a timedemo with 1000/fps of the match a frame and buffers a
`screenshot "exports/<demo>/frame%05d.png"` each frame, which runs at the head
of the next frame's command buffer before playback moves on, so each picture is
the frame shown; sound came in the fourth round (below). A client recording now has
markers: the client's game runs `PublishMVDMarkers` (phase, round, pause,
score, flag and powerup changes it can see) and marks each kill from the death
message, and the recorder stamps them on its own clock. Every finished
recording ends with an optional summary record (type 23: the names seen while
recording); the library shows them, or whose view a client recording is, and
`demoFind` searches name, map, mode, server, players and recorder. A recording
made where there is a view takes a 320x240 `demos/<name>_thumb.png` three
seconds in (`screenshot <file> <width> <height>`), which the library shows and
deletes with the demo.

**Sound in exports (fourth round, same day).** `demoExport` writes
`exports/<demo>/<demo>.wav` beside the pictures (`src/sound/SoundCapture.h`).
Before `StartPlayback` loads the map, `Sound_BeginCapture` reads the open
device's `ALC_FREQUENCY` and `ALC_OUTPUT_MODE_SOFT` (mono, stereo, HRTF
stereo, quad, 5.1, 6.1, 7.1), opens the file and restarts the sound system
onto an `ALC_SOFT_loopback` device in that format: 16-bit, at the device's
rate, HRTF and the output limiter as the device had them; a layout the mixer
cannot render falls back to stereo. The map's samples then load straight onto
the loopback context, and nothing reaches the speakers. Each picture the
export buffers calls `Sound_CaptureAdvance` with its step, and that frame's
`idSoundSystemLocal::Render` mixes it after the world and the hardware update
(`MixCapture`): `alcRenderSamplesSOFT` renders `msec * rate / 1000` minus the
frames already written, in slices of at most 100 ms with the playing world and
the voices updated between them so a stream's ~1 s queue refills as in a live
frame, and the sound clock moves on by exactly the slice instead of reading
`Sys_Milliseconds`. `RefreshSoundTime`, the reverb slots' idle clock and the
listener gain (no device pause) follow the export, endpoint monitoring and
device events stay off, and the session does not silence the playing world for
lost focus. The steps themselves are now whole milliseconds that add up to
`n * 1000 / fps` (`DemoExportStepMsec`: 33, 33, 34 at 30 a second): the old
fixed `1000 / fps` step made 30 and 60 a second pictures 1% and 4% shorter
than the match, which a track at the match's own rate would have shown.
`FinishPlayback` and `StopPlayback` call `Sound_EndCapture`, which mixes the
last step no sound frame reached, rewrites the RIFF header (PCM for one or two
channels, `WAVE_FORMAT_EXTENSIBLE` with OpenAL Soft's speaker mask above that),
reports `MVD_EXPORT_SOUND written=1 file=... rate channels frames msec peak rms
complete` and restarts onto the playback device; the sound clock keeps any lead
the export built over the wall clock (`soundClockOffset`), so it never runs
backwards. A track that cannot be made (sound off, no loopback in the runtime,
for example Apple's OpenAL framework, a settings change holding the device, an
unwritable file) leaves the device alone and reports `MVD_EXPORT_SOUND
written=0 reason="..."`; a mixer lost mid-export writes silence for the rest
so the track keeps its length. Stream blocks still decode on job workers, so a
stream that starts in an export starts a frame late, as it does live.
`SoundCaptureTest` checks the step and frame arithmetic, the headers and the
levels; `mp_client_demo.py` pins the wiring, and `mp_client_demo_smoke.py`
exports the client recording with sound on and reads the WAV back: format,
rate and channels as reported, exactly `pictures * 100 ms` at 10 a second, not
silent, and the device opened again. On 23 November it passed on Windows
(OpenGL, a headphone endpoint with automatic HRTF): 330 pictures and a
33.000 s, 48 kHz HRTF stereo track (1,584,000 frames, peak 27,684, every
second audible). The same export with `s_numberOfSpeakers 6` wrote a 5.1
`WAVE_FORMAT_EXTENSIBLE` track (mask 0x60F) of the same length, with sound in
the front, centre and side channels. The render-demo `aviDemo` path, which
wrote no pictures here, now records a video file with its sound through video
capture ([Record Video](../user/video-capture.md)), which shares this
loopback mixer.

**Open.** Recording across a map change, and the prediction's
lead: playback predicts to the recorder's frame as the clock reconstructs it,
which matches the recorded view to within the frame-time jitter of the
original client.

## Retail Quake 4 render demos (MP-22), 23 November 2026

openQ4 plays render demos recorded by retail Quake 4 1.4.2 (`recordDemo`,
`demoShot`) from the library and with `playDemo` and `timeDemo`. The decision
to build readers for both retail formats is the user's, 23 November 2026; the
network demo reader has [its own section](#retail-quake-4-142-network-demos-mp-22-23-november-2026).

### What retail writes

Retail keeps Doom 3's render-demo wrapper (`Quake4 RDEMO`, a compression
scheme, the LZW, LZSS and Huffman compressors openQ4 already has) and its
three top-level tokens, but writes every token and every command as one byte
and lays its records out after its own structures. The layout below was read
from the retail executable's writers (`Quake4.exe` 1.4.3 as Steam ships it;
the Hex-Rays reconstruction misses most of what follows) and checked against
recordings that executable made:

- `DS_VERSION`, then retail's savegame version as an int.
- `DS_SOUND`, a command byte, then the Doom 3 sound commands with three
  changes: a listener placement carries the game time after the listener id,
  the free flag is a byte, and shader parms are the 40-byte struct in openQ4's
  field order. `SCMD_STATE` is retail's sound savegame layout: the listener,
  then each live emitter with its parms, eight channel slots of 125 bytes and
  the shader and sample names playing on them.
- `DS_RENDER`, a command byte (1 to 20). Entity, light and effect records are
  the retail structures with pointers as presence words and names after them.
  Three things in them are easy to get wrong:
  - `idFile::WriteBounds` writes a bounds' maximum before its minimum. Read
    the other way round, every entity has inverted bounds, gets no area
    references and its dynamic model is never drawn.
  - An entity record carries each GUI it has: an int id, the GUI's state
    dictionary, its whole window tree (each `idWindow` writes its geometry,
    fourteen typed window variables, its registers and its children, an
    `idSimpleWindow` twelve variables) and the cursor.
  - An effect record carries `rvBSE`'s live state: the effect, then each
    segment and every live particle, each particle type with its own record.
    Retail writes neither the segment count nor which segments hold particle
    pools; its reader takes both from the effect as it starts it.
  - The map header is four ints (0, then 224 and 216, retail's entity and
    light record sizes, then a word whose meaning is not known), the 256-byte
    map name and the start time.
  - `DC_UPDATE_*` records carry only a handle; the full record follows when
    the definition is next drawn. `DC_DEFINE_MODEL` is never written. Each
    frame has two identical main views and, where the level has one, a
    portal-sky view before them. The stream ends after the last
    `DC_END_FRAME`, without `DS_FINISHED`.

### Playback

`idDemoFile::IsRetailRenderStream()` routes the stream: the session reads byte
tokens, `idRenderWorld::ProcessRetailDemoCommand`
(`src/renderer/RenderWorld_demo_retail.cpp`) the render commands and
`idSoundWorld::ProcessRetailDemoCommand` the sound commands.

- **GUIs.** The window tree is read and checked but not rebuilt: the GUI the
  state dictionary names is loaded and given that state, so a screen shows what
  it showed, drawn by openQ4. A GUI already on the entity with the same file
  is reused across updates.
- **Effects.** The reader asks the effect manager
  (`rvBSEManager::DemoParticleLayout`) to start a scratch copy of the effect as
  retail would, and reads the state segment by segment with the pools that
  copy allocated. The particles are not restored: the effect runs from the
  start time its state records, through the same silent pre-roll openQ4's own
  demos use. A recording of an effect this installation does not declare
  cannot be read past and stops.
- **Timing.** A frame lasts as long as the recorded view times say, so playback
  keeps the recording's pace; the two-tic frame retail assumes is not used.
- **Portal skies** are drawn before the main view, as the game draws them.
- **Controls.** Pause, speed and step work as for openQ4's older render demos;
  seeking replays from the first frame. There is no keyframe or summary, so the
  library shows no length.

Every count, index and length is bounded before use, every float that places
something is checked finite, and a record that does not fit stops playback
with a warning naming it. Nothing in the reader trusts retail's own playback,
which keeps no bounds: retail itself crashed after 130 of the fixture's 136
frames while capturing it with `aviDemo`.

### Validation

- `tools/tests/retail_render_demo.py` decodes an uncompressed retail stream on
  its own, as a check of the layout: it reads the fixture below to its last
  byte. With no effect declarations it finds each effect's segment layout by
  search, accepting one only when the next commands decode.
- `tools/tests/retail_render_demo_contract.py` builds a retail-layout stream
  with every record kind, decodes it, cuts it at every byte and corrupts it
  3,000 times (every outcome a `DemoError`), and pins the engine wiring and the
  library string in all twelve languages. It needs no game files.
- `tools/tests/retail_render_demo_smoke.py` records the fixture with retail
  Quake 4 itself (`.tmp/runtime/retail-audio`, console scripts only, no input
  synthesized), has retail compress it into the other three schemes and
  capture its frames with `aviDemo`, then plays all four copies in the staged
  openQ4, captures openQ4's frames by pausing and stepping, compares them with
  retail's, and plays sixteen cut or corrupted copies in one session.
  Results are recorded in the MP-22 entry of the [roadmap](roadmap.md).

## Retail Quake 4 1.4.2 network demos (MP-22), 23 November 2026

Quake 4 1.4.2 records a network demo (`recordNetDemo`) as the packets one client
received, behind a header holding that client's network state; a server can
also record one for a spectator in slot 32. 1.4.2 named the files by protocol,
`.ndmo85`, and installs two in `q4mp/demos`: `id_perftest.ndmo85`, client 2's
view of a nine-player DM match on q4dm7 (151 s, 2,177 packets), and
`id_serverdemo.ndmo85`, the server's recording of the same match (59 s, 922
packets). They are the fixtures; the retail game never has to run. openQ4 plays
both through the MVD player.

**Container.** Read from `Quake4.exe` 1.4.2 (`idDemo::Play` 0x10097cc0,
`idDemo::ReadHeader` 0x10096d10, `idFileSystemLocal::ReadDemoHeader` 0x100595c0,
`idAsyncClient::ReadDemoHeader` 0x1007fb30); `Quake4Decompiled-main` is an older
2.64 build and differs. Little-endian, in order:

| Part | Layout |
|---|---|
| Header | `NDMO` ident, version 12, protocol `(2 << 16) \| 85`, offset of the command tail |
| Pure paks | `fs_game_base`, `fs_game`, pak count; per pak a name and checksum; the recording OS; 6 x 10 game-pak names and checksums |
| Kind | one byte: 1 for a server demo |
| Client | data checksum, client time, client id, client number (32 for a server demo), prediction, predict time, address (type, 4 bytes, port), server id, challenge, message sequence, snapshot sequence, snapshot frame and time, game id, game frame, time and residual |
| Commands | client demos only: the `[32][256]` command table, each a 29-byte command or a `-1` |
| Channel | incoming and fragment sequences, the pending fragment, the reliable queue (always empty) |
| Spawn data | 32 user-info dictionaries, server info, synced cvars (count, then length-prefixed strings) |
| Game | `idGameLocal::WriteNetworkInfo`: a client demo's 4096 entity states, PVS, players and pending snapshots; a server demo's players; then the multiplayer start state and game state |
| Packets | time, size and bytes, up to a zero time just before the command tail |
| Command tail | the recorder's own commands, ended by `-1` |

`id_perftest.ndmo85` also holds a packet stamped with time 0 mid-stream, where
1.4.2's own player stopped; openQ4 plays on through the rest at the previous
time and ends only at the zero directly before the command tail.

**Reading it** (`RetailNetDemo.cpp`). `ParseContainer` checks every count and
length against the bounds 1.4.2's code was built with before anything is copied
(pak count 128, dictionary keys 4096 and strings 1023, fragment and message
16384, entity states 1 to 512 bytes, 64 pending snapshots, game messages 8192,
packets inside the stream, command frames rising, nothing after the tail); a
failure names the part. `Decode` runs the 1.4.2 channel, which is openQ4's old
2.41 one: sequence, 1,300-byte fragments, the zero-run coding with 3-bit words,
reliable acknowledgement and the receive queue. Reliable ids are 1.4.2's
(PURE, RELOAD, CLIENTINFO, SYNCEDCVARS, DISCONNECT, APPLYSNAPSHOT, GAME,
ENTERGAME and two it adds); the snapshot header is openQ4's without the
command-slack short. A map change, a reload or the recorder's own disconnect
ends the stream there. The events are stamped with the recording's clock:
the server's snapshot frames for a server demo, the recorder's frames,
rebuilt from the packet times and the header's clock, for a client demo.

**Playing it.** `idMultiViewDemo::OpenRetailNetDemo` refuses a demo recorded at
an `si_fps` other than openQ4's 60 or needing a pak this installation lacks,
then writes the events in memory as format 1.3 records, protocol 2.85: a client
demo as a client-view recording (the header's command table becomes the
network state's commands, a snapshot record gets openQ4's slack field), a
server demo as a server-view one with its routed game reliables. Everything
after that is the MVD path, with its validation, deck, library entry, seek by
replay, `timeDemo` and export. Three things differ for 2.85: the commands of
other players after a client snapshot use 1.4.2's codec (2-bit movement
values, no times; `ReadRelayedUsercmd`), a server demo's reliables may route
to slot 32, the demo viewer itself, which the game shows only to the free
camera because each player's own copy shows while following, and the game
state message is shorter.

**The game module** reads 2.85 as `RetailSnapshotFormat()`. 1.4.2's layout is
the 2.41 one (`LegacySnapshotFormat()`: velocity, launch direction, hit
feedback, 32-bit entity tags) without what openQ4 added before that: classic
ruleset fields, weapon mods, the material type of a damage effect, 18
powerups (1.4.2 sent 16 in the start state, the player state and the end
times), vote fields (12 flags in a short), the overtime state and the neutral
CTF flag. Retail's delta reader read a base past its end as -1 for that read
alone, and its writer sent such values as unchanged; `idBitMsgDelta`
`SetRetailBaseReads` does the same for a 2.85 snapshot only. The check tag
after every entity makes any remaining difference visible at the entity it
starts at.

**Decl numbering.** Quake 4 sends decls by index (`ReadDecl`, entity
definitions in `entityDefBits` from the count). openQ4's own decl files sit
among the stock ones, so `BuildDeclRemap` rebuilds the recording's numbering
per type: the decls whose file comes from one of Quake 4's own paks
(`FS_FilePack`: `q4base` or the recording's `fs_game`), in openQ4's parse order
(file order, then line), with a decl an openQ4 override file defined first
placed where the stock file defined it again (`DeclManager_ShadowedSource`).
The demo's pak list cannot decide it: it is the pure list of the paks the map
referenced, and the 1.4.3 Steam update supplies some files from `pak023` to
`pak025`. The game asks through `idNetworkSystem::RetailDemoDeclIndex` and
`RetailDemoDeclCount` (game API 63). The rebuilt tables match what
`Quake4Ded.exe` lists (`listEntityDefs all` and the effect, sound, material and
skin lists): 1,804 entity definitions and 329 skins exactly, and every effect,
sound and material over the range explicit decls cover.
`net_retailDemoDebug` 1 to 3 prints the files, every rebuilt decl and every
entity of every snapshot.

**Validation, 23 November.** `retail_netdemo_contract.py` reads both demos with
an independent Python reader (container, channel, zero-run coding) and finds
the packet, snapshot and reliable counts the engine prints, generates the
damaged copies, and pins the bounds, the gates and the localization.
`mp_retail_netdemo_smoke.py` (Windows, OpenGL, hidden window, console scripts
only) plays the client demo from the library's Play action through seek,
rewind, frame steps and the refused follow to its clean end, the server demo
with follow and the free camera to its end, times the server demo with
`timeDemoQuit`, plays 35 damaged copies (truncations in every part, oversized
counts and lengths, another release's version and protocol, a server demo with
a player as recorder, a bad message size, flipped bits in a packet), each
refused with the reason or stopped with a warning and the game quitting from
its script, and compares the decl numbering with `Quake4Ded.exe`'s. Not run:
Vulkan, Linux, macOS and a person watching.

**Open.** Demos from releases before 1.4.2 (`.netdemo`, protocols 2.64 to 2.71
with other headers and game layouts) are listed and refused; recordings at
another `si_fps` are refused until openQ4 simulates other tick rates; the
recorder's prediction reconstructs its frames from packet times, so a client
demo's view leads its snapshots by the recorded prediction only approximately.

## Campaign render demos (v11), 7 November 2026

Roadmap Q4-09 owns this work. Before v11 a render demo ended at the first map
unload, so it could never span a level transition, a save load or a death
reload. Its frames also included every menu and loading screen, and it kept
only the 2D batch still pending at the end of a frame.

### Stream

v11 adds session records (`DS_SESSION`, `DemoFile.h`). Each is a dictionary;
readers skip unknown record kinds.

```text
DS_VERSION 14 (11 to 13 still play)
DS_SESSION DSC_HEADER   game, campaign, mode, userCmdHz, skill, build
per segment (one level):
  -- v14: both sides reset the hash-string table here, before the record --
  DS_SESSION DSC_SEGMENT        index, frame, map, entityFilter, reason, skill
  -- v11 to v13: both sides reset the hash-string table here, after it --
  DS_SOUND  SCMD_STATE          the complete sound world
  DS_RENDER DC_LOADMAP          the level, then its portal states
  DS_SESSION DSC_SEGMENT_READY
  frames ... DS_SESSION DSC_FRAME_TIME int microseconds (v13), DS_RENDER DC_END_FRAME
  v14, every ten seconds: a keyframe in blocks of its own, then
    DS_SESSION DSC_RESYNC       both sides reset the hash-string table
DS_SESSION DSC_END              frames, segments, durationMS (v13)
raw trailer (outside the stream): payload | int length | uint crc32 | "OQ4DSUM1"
```

Through v13 the stream is one `idCompressor` stream; from v14 it is carried in
checked blocks ([block container](#block-container-keyframes-and-large-files-v14-14-november-2026)).

A segment depends on nothing before it: a fresh string table, the full sound
state, a level load that frees every definition, archive flags reset so every
visible entity, light and effect is written again, in-world GUIs rewritten, the
first frame's 2D written in full and, from v12, no joints predicted from an
earlier record ([joint prediction](#joint-prediction-v12-7-november-2026)). The writer finds hash strings through an
index instead of a linear search, and the per-segment reset keeps each table
far below the 65,536-string bound over a long campaign.

The trailer gives the library and the player the duration (from v13
`durationMS`; earlier, the frame count and tic rate) and every segment's start
frame, reason, map and, from v13, start time without decoding the
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
  (`EndRenderDemoRecordFrame`); `EndFrame` no longer does. Menus, pauses and
  loading screens leave no frames. From v13 every gameplay draw is a frame,
  stamped with its own time ([frame-rate recording](#frame-rate-recording-v13-11-november-2026));
  v11 and v12 held the game to `USERCMD_PER_DEMO_FRAME` (two) tics per frame.
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
- The timeline is the time of the recorded frames (v13: each frame's own
  duration; v11 and v12: two tics a frame at the recording's tic rate), so it
  continues across levels whose game clocks restart. Seeking is the existing
  restart-and-replay path, crossing segments as it goes.
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
of an archived entity, and starts a fresh one at every raw record, the only
point where the recording's history is fresh too. Until 11 November it did
not, so an entity that took a freed handle with the same joint count was
predicted from the one before it and playback stopped on a checksum mismatch;
v12 demos play with the fix as well. A predicted record without a matching history, a
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

### Frame-rate recording (v13), 11 November 2026

**Why recordings were 30 fps.** The render demo inherited Doom 3's fixed
cadence: `USERCMD_PER_DEMO_FRAME` is 2 and the tic rate 60. While `writeDemo`
was set, `idSessionLocal::Frame` waited for `lastGameTic + 2` and then ran
exactly two tics a frame (dropping older ones, and calling a fatal
"numCmdsToRun < fixedTic" if it ever had fewer). The live game itself therefore
ran at 30 Hz for as long as a recording lasted, whatever the frame cap, and each
recorded frame meant 33.3 ms. Playback waited for the same two tics before each
frame. Multiplayer never runs `idSessionLocal::Frame`, so a multiplayer render
demo recorded every presented frame and then played them at 30 Hz: a recording
made at 60 fps played its action at half speed (measured 0.48x), one made at
240 fps at an eighth.

**v13.** Nothing waits for tics any more except fixed-rate capture
(`aviCaptureMode`). Every gameplay draw is a recorded frame, and
`EndRenderDemoRecordFrame` writes a `DSC_FRAME_TIME` record, one int, before
the frame's `DC_END_FRAME`: the microseconds since the previous recorded frame.
That is the high-resolution clock between them when the previous frame was the
draw just before; after anything that left no frame (a menu, the pause screen,
a level load, the segment start) it is one tic, so the timeline never contains
time the recording does not. A stall counts 250 ms at most. The trailer and
`DSC_END` carry `durationMS`, and each trailer segment line appends its start
time (older readers stop at the filter field).

Playback paces every render demo on frame times. A playback clock runs at the
playback rate in real time (`RenderDemoPlaybackElapsedUS`; capture steps
`com_aviDemoTics` tics a frame instead); each update reads every frame the
clock has passed and shows the first one still ahead of it, at most 64 a call.
A pre-v13 frame is two tics, so those demos keep their 30 Hz cadence; a v13 frame
missing its time stops playback with a warning. A step, the end of a seek and
the first frame put the clock in the middle of the frame on screen, and a level
load (`DSC_SEGMENT_READY`) or a pause restarts its real-time anchor, so neither
plays as a jump. The middle, not the start: on a frame boundary every update
would be a coin flip between two frames when the display runs at the
recording's rate. Capture starts half a step before the frame on screen ends,
so each fixed step samples the middle of an interval. Very high speeds on very
high frame rates are limited by the 64 frames an update reads (a 500 fps
recording at 16x on a 60 Hz display plays at about 8x). Because playback no
longer blocks the main loop, the playback overlay runs at the display rate too.
A level that drew no frame before the next began starts at that next frame.

Recording every drawn frame repeats the last tic's joints on frames drawn
between tics, and v12's history would then predict the next tic's motion as
none. From v13 a joint that did not move keeps the step it last moved by (both
ends, by the stream's version).

Validation, staged Windows client, hidden window (its presentation cap is
60 Hz), no input; cases, traces and their scripts under `.tmp/demo-fps/`
(local only):

- Two levels (airdefense1, then `map game/airdefense2`) recorded at the cap:
  the game presented 60.0 Hz with one tic per frame while recording (it ran at
  30 before), 965 frames over 16.5 s, level two one tic after level one's last
  frame, 28.1 MB (1.7 MB/s; v12 at 30 fps was 0.92 MB/s). The same script at
  `com_maxfps 45`: 45.0 Hz, 1.33 tics per frame, 732 frames, 22.6 MB.
- `timeDemoQuit` played every frame of both on GL, the 45 fps one on Vulkan too,
  with every joint record matching its checksum. The v11 and v12 clips (500
  frames each) and the three-level v11 demo (745) still play in full.
- Real-time playback, traced with `com_showDemo 1` (which now prints each
  frame's time and the recorded view's game time): one recorded frame per
  60 Hz update for the 60 fps demo, 1.33 for the 45 fps one, a frame every
  other update for v12; the recorded action ran at 0.95-0.96x, which is real
  time (the game clock adds 16 ms per 16.67 ms tic), at 2x and 0.5x as asked,
  not at all while paused, one frame per `demoStep`, and on from the target
  after `demoSeek` and level markers.
- Multiplayer (listen server, three bots, 6 s): `main` recorded 361 frames it
  called 12 s, whose action played at 0.48x; v13 recorded the same 361 frames
  as 6 s, played at 0.96x, in 6.6 MB instead of 21.4 MB.
- A reused handle (`spawn monster_strogg_marine`, `killmonsters`, spawn again,
  twice, in airdefense1): playback of the v13 recording stopped at frame 353 of
  482 on "entity 82 joints do not match their checksum" before the reader fix,
  and `main`'s own v12 recording and playback stopped at frame 253 of 341. With
  the fix both play every frame.
- Capture (`aviDemo`, `com_aviDemoTics 2`): every frame of the v12 clip (500)
  and of the three-level v11 demo (745) was shown exactly once, across level
  changes; the 60 fps recording gave 496 captured frames for 16.5 s.

### Block container, keyframes and large files (v14), 14 November 2026

**Why.** Through v13 a recording's stream went through one `idCompressor`
(LZW with a 12-bit dictionary) from its first byte to its last, with nothing
between the bytes and the decoder. Four things followed:

- **Size.** A render demo repeats itself one frame back, 50 to 250 KB away,
  which that dictionary cannot hold, and raw floats came out 1:1. A busy level
  at 60 frames a second cost about 1.7 MB a second.
- **Fragility.** Nothing in the file was checked. A recording that was killed
  ended wherever the compressor's buffer happened to be, and one bad byte took
  every byte after it. Only a joint checksum, by accident, ever noticed damage.
- **Seeking.** The one way to an earlier time was to play the file again from
  its first frame, through the load of every level on the way.
- **2 GiB.** `idFile` carries `int` lengths and `long` seeks. A file of 2 GiB
  or more could not be opened at all, which at that rate was twenty minutes.

**Container.** A recording is now the wrapper (`openQ4 RDEMO`, then the scheme
int, `4`) followed by blocks, and the summary trailer after the last one
(`DemoBlockCodec.h`; no engine types, so the whole container is tested without
the engine):

```text
block = header (52 bytes, little endian) | packed payload

 0  u32 magic "OQ4B"          28  u64 startTimeUS  the timeline at the last frame before it
 4  u16 header bytes (52)     36  u64 durationUS   what the frames inside it add
 6  u8  flags                 44  u32 CRC-32 of the packed payload
 7  u8  codec                 48  u32 CRC-32 of the 48 bytes before
 8  u32 raw bytes (<= 1 MiB)
12  u32 packed bytes          flags: RESET 1, SEGMENT_START 2, FRAME_END 4,
16  u32 level segment                STREAM_END 8, UNTIMED 16, KEYFRAME 32
20  u32 startFrame            codec: 0 stored, 1 long-range matches + zlib
24  u32 frames ending inside
```

A block is checked before a byte of it is used: the header's own CRC, known
flags and codec, sizes within the limits, then the payload's CRC, then the
decoder, which accepts only an exact encoding of the block's size. The stream
ends, with the reason and the byte offset, at the first block that fails. A
block closes at the end of the first frame that fills it to 128 KiB, or that
ends half a second after the block before it, so blocks end on frame
boundaries (a frame or a level's first state larger than 1 MiB is split) and a
crash costs about half a second of recording.

Every header also places its block on the timeline. Walking the headers alone
(`idDemoFile::ReadTimeline`, no payload decoded) gives the frames, the
duration, where every level and keyframe begins and whether the recording was
stopped cleanly, so a recording without its summary trailer is described and
seeks like any other. The block flagged `STREAM_END` ends the stream; the
trailer after it is never read as a block.

**Compression.** Two stages (`idDemoBlock::encoder_t`). The long-range stage
indexes one position in eight of the last 2 MiB of the stream by the 16 bytes
that begin there, probes every position of a new block and replaces any run of
24 bytes or more that already occurred with a length and a distance. A record
that differs from the frame before in a few fields lines up with it at one
distance, so a match at the last distance costs one byte to name and may be 8
bytes short, and the bytes between two such matches are stored as the XOR with
the bytes one distance back whenever that leaves more zeros, the same idea as
LZMA's matched literals. What is left goes through the in-tree DEFLATE
(`imagetools/PNG/ZlibStream.h`, level 6). The history runs across blocks and
starts again at a `RESET` block: the file's first, and the first of every
level and keyframe, which therefore decode with nothing before them. A block
that would not shrink is stored.

On the 51.00 MiB stream of the v13 size study (16.5 s, two levels, 60 frames a
second): LZW 26.47 MiB, zlib level 9 as one stream 16.49 MiB, xz level 6
8.35 MiB, the block container 10.68 MiB (4.78:1). It packs at 87 MiB a second
(a block takes 4.6 ms at worst) and unpacks at about 1 GiB a second (1.2 ms at
worst), on one core. The window was measured, not assumed: 1, 2, 4 and 8 MiB
came within 0.3% of each other, and the anchor, match and block sizes within
1%.

**Recording.** The game thread fills blocks (`idDemoBlock::builder_t`) and, as
each closes, hands it to a worker thread that packs it; the game thread writes
the packed blocks to the file in order as they come back and flushes them.
Only the game thread touches the file, and it waits for the worker only when
8 MiB are queued (`demo_recordThread 0` packs on the game thread). The file is
written through the C runtime directly, so no `int` length is involved.
`BeginSegment` closes the open block and resets the hash-string table before
the `DSC_SEGMENT` record, not after it as v13 did, so a level's first block
names every string it uses. `EndFrame` stamps the frame onto the block
timeline.

A recording ends itself, cleanly and with its summary, when a block cannot be
written, when the save path's disk has less than `demo_recordMinFreeMB` free
(256 by default: a campaign must still be able to save) or when the file
reaches `demo_recordMaxSizeMB` (0, no limit). It ends in the draw that found
out, after the frame is whole; a queued `stopRecording` would wait behind any
script that is waiting. `com_compressDemos 0` stores the blocks unpacked, for
tools that read the stream. `demoRecordStatus` adds the file and stream sizes.

**Keyframes.** Every `demo_recordKeyframeSeconds` (10) of recorded time, between
two frames, the recording gives the level a second way in
(`WriteRenderDemoKeyframe`): the records a segment starts with (a `DSC_SEGMENT`
whose reason is `keyframe`, the sound world, the level and its portal states,
`DSC_SEGMENT_READY`) and then every definition playback holds at that point,
in blocks flagged `KEYFRAME` with a hash-string table of their own. The
definitions playback holds are exactly the archived ones, because a definition
that changes after it was written is deleted from the stream as it changes.
The archive flags stay as they are. Only what later records are predicted from
starts again: joints are written in full, the 2D in full, in-world GUIs as
they next draw, and a `DSC_RESYNC` record starts a new string table for the
stream that follows.

Playback in order never sees a keyframe: the block reader decodes its blocks,
since the blocks after it lean on their bytes, and hands none of them out. It
reads `DSC_RESYNC` and goes on with the definitions it has. Playback that was
sent to a keyframe is handed its blocks, takes the level's records exactly as
it takes a level's start, and reads on. The definitions follow the record that
ends the level's load, as they do in a level played from its start: a
keyframe that put them inside the load left the smoke of a level entered from
another one drawn with the default image.

A keyframe is about 100 KB of stream on airdefense1 (34 KB in a corridor) and
takes 0 to 2 ms to write; its first block packs to under 90 KB with no history
and the blocks after it then match against it, so eight keyframes in an
82-second recording cost less than 1% of the file.

**Playback.** A seek opens the file at the last level start or keyframe at or
before its target (`RenderDemoTimelineEntryAt`, `JumpRenderDemoToEntry`): the
frame being read ends, the reader moves to the block, the frame count and the
timeline are set from its header, and replay runs from there under the usual
budget. A seek back replays the seconds since the keyframe; a seek ahead skips
what lies before a later one; neither loads a level it does not land in.
`demo_useKeyframes 0` starts from the level's start instead. A v11 to v13
recording, or a v14 stream without a block timeline, still restarts and
replays.

When the stream stops at a damaged block, everything after it leans on it up
to the next level start or keyframe, which leans on nothing: playback warns,
goes on from there and says which frames were lost
(`ResumeRenderDemoAfterDamage`). A recording that simply ends without its
closing block plays to its last whole frame and reports that it was
interrupted; neither is a malformed-demo stop.

Two things playback now does for any effect it learns of late (at a keyframe,
at a level start recorded mid-level, or when the effect first comes into
view), because a keyframe made the old behaviour visible: the recording stamps
an effect record with the view's time when nobody has updated the effect since
it began, so playback no longer creates a standing column of smoke in the past
and leaps to the present; and playback runs an effect that has been going for
a while through its last 12 seconds, silently, before showing it, so it
appears in full flow and not from its opening burst.

**Large files.** `demoBytes_t` (`DemoFile.cpp`) addresses a loose demo file
through the C runtime's 64-bit seeks alongside the `idFile` the file system
opened, and falls back to the `idFile` for a file inside a pack or preloaded
into memory. The reader, the timeline walk, the trailer and the library's size
all go through it, so the trailer of an older LZW recording past 2 GiB is
found as well.

**`compressDemo <name> [scheme]`** writes `<name>_compressed.demo` in the block
container by default; schemes 0 to 3 remain for the older streams. A block
container is copied block for block, keyframes included, so the copy keeps
its timeline, and at the same level the copy is the same file. An older
recording becomes a block container without a timeline (`UNTIMED`): it gains
the size and the checks and still seeks by replay.

**Validation.** Staged Windows client, hidden window (60 Hz), no input; cases
under `.tmp/demo-revise/` (local only). Another session's client ran alongside
most runs, so the seek times are upper bounds.

- `openq4-demo-block-codec` (native, no engine): header round trip, all 416
  single-bit header flips and 7 well-formed invalid headers refused; round
  trips at every level and block size, noise stored, 7.5 MB past the window;
  10 hostile payloads with a valid zlib wrapper refused (distances before the
  history or beyond the window, lengths past the block, literals left over,
  overlong varints) with no byte written past the block; 600 bit-flip trials on
  a packed block never produced wrong bytes; a three-level recording with nine
  keyframes read back in order, from each level and from each keyframe; 2,264
  truncations each played and described exactly their whole blocks; 400
  single-bit flips each stopped the stream at the block they hit, and the
  stream resumed at each later way in; a block-for-block copy equalled its
  source.
- An 82.3 s recording at 60 frames a second, airdefense1 for 61 s then
  airdefense2 (4,913 frames, two levels, eight keyframes): 281.3 MB of stream
  in a 56.9 MB file, 0.69 MB a second. The same stream as LZW is 140.0 MB
  (1.70 MB a second), 2.46 times the size. `timeDemoQuit` played all 4,913
  frames on OpenGL and on Vulkan.
- The v13 (965 frames), v12 (500), v11 (500) and three-level v11 (745)
  recordings play every frame.
- Seeks on that recording: to 55 s from 3 s 0.55 s (3.3 s by replay on the
  same build without keyframes), back to 25 s 0.49 s, into level two at 75 s
  0.81 s with its load, back into level one 1.1 s. With `demo_useKeyframes 0`
  the same three targets took 2.4, 1.7 and 5.1 s against 0.64, 1.0 and 1.1 s.
- A paused frame reached through a keyframe and the same frame reached by
  replay from the level's start (27 s, 44.5 s after a visit to level two, and
  77 s): the same view, entities, weapon, HUD and smoke columns; 0.05% of the
  pixels differ by more than 24 levels, all in the smoke, whose particles are
  random in any playback.
- A recording killed at 35 s: 830 whole blocks, 2,093 frames, all played, then
  "the recording was interrupted" and a normal exit; the library lists it as
  an interrupted recording of 35 s.
- One flipped bit at 31% of the file: a warning with the byte offset, playback
  resumed at the keyframe at 20 s having lost 83 frames, and reached the end.
  A copy cut in the middle of a block played its 1,005 whole frames.
- `demo_recordMaxSizeMB 6`: the recording ended itself at 6.1 MB with its
  summary, 351 frames, all playable.
- A 2.35 GiB file (level one, 2.3 GiB of stored filler blocks as a level, then
  level two and the trailer): a seek to 70 s opened the file at byte
  2,522,491,343 and played on; a seek to 79 s opened at a keyframe beyond it;
  the library lists it with its duration.
- `compressDemo`: the block copy of the recording is byte-identical; the v13
  LZW recording (28.1 MB) became an 11.4 MB block container that plays its 965
  frames; a v14 stream written as LZW plays its 4,913.
- `com_compressDemos 0` with `demo_recordThread 0`: 384 stored blocks, played.

Not covered: recording on Vulkan, Linux and macOS (the container and the
64-bit file access are portable C++, and the native test runs everywhere, but
no recording was made there); a multiplayer recording; a recording that is
really hours long rather than a synthetic large file; and hearing the result,
since every run was silent.

### Library

A v11 single-player entry shows **Campaign demo**, its first level, the date,
the duration from the trailer and `<campaign> / Levels: N`. The file being
recorded is listed as **Recording in progress** and cannot be played. A demo
from the other campaign is **Other campaign**, with Play disabled and the
reason. A demo without a trailer is **Interrupted recording**, still playable to
its last complete frame; a v14 one also shows its length, which its blocks
give, and has its level markers. `compressDemo` carries the trailer over. The
library measures a file with 64-bit lengths, so a recording past 2 GiB is
listed like any other.

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
block container, the library and the strings; `openq4-demo-block-codec` runs
the container itself. Remaining (Q4-09): Vulkan recording, Linux and macOS, a
session that is really hours long, a multiplayer recording in the block
container, hearing a recording played back, masked HUD groups and local sounds
already playing at a segment start. Animated entities' joints are now most of
what a recording stores; a smaller exact coding of them is the next step in
size.

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
