# Demo Library, Playback, and Multi-View Demos

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

Multi-view demos are server-side multiplayer recordings. One `.mvd` file
contains the full match state across every active multiplayer instance instead
of only the camera seen by the person who started recording, so playback can
switch between active players or use a free camera.

openQ4 also has one **Demos** library for native MVDs, openQ4 render demos,
developer command demos, unsupported retail recordings, and incomplete files.
Choose **Multiplayer > Demos** on the main menu, or enter `demoMenu` in the
console. The
browser can filter **All**, **Multi-view**, **Render**, **Legacy**, and
**Incomplete** entries and shows the detected type, map, date, duration, and
playback status.

Only controls supported by the selected format are enabled. A loose file in
your save-path `demos/` directory can be deleted after confirmation;
package-backed demos are read-only.

## Record your campaign

A single-player recording now follows you through the whole session. Open the
console and enter:

```text
recordDemo my_campaign
```

Enter it at the title screen and the recording starts with the first frame of
the next level you start, continue or load. Enter it during a level and it starts
there.

One file keeps recording through level transitions, save loads, deaths and
reloads, difficulty restarts and chapter starts. Time spent in menus, on the
pause screen or on loading screens is left out. Recording stops when you enter
`stopRecording`, quit to the main menu or quit the game. It also stops when you
join a multiplayer game. `demoRecordStatus` shows whether a recording is
waiting, running or between levels, with its file, length and level count.

Recordings go to `demos/` in your save path, under the active campaign's game
directory. In **Demos** a campaign recording is listed as **Campaign demo**,
with its first level, length and number of levels. A demo recorded in the other
campaign (Quake 4 or The Awakening) says so and cannot be played until you switch
campaigns. A recording cut short by a crash or power loss is marked
**Interrupted recording** and still plays up to its last complete frame.

During playback each new level shows its loading screen, and the overlay names
the level you are watching. **Prev Marker** and **Next Marker**, or
`demoMarker prev` and `demoMarker next`, jump between level starts; Prev goes to
the start of the current level first. Seeking, speed, pause and stepping work
across levels.

A campaign demo stores what you saw and heard, including the HUD and in-world
screens, not the game itself, so the camera cannot leave your view. Grouped HUD
panels play back without their shaped edges. Busy levels use about 1 MB per
second, so a long campaign recording needs several gigabytes of space.

## Record a match

Host a multiplayer server map, then use:

```text
recordMVD match_name
```

Omit the name to use an unused `mvd` filename. Recordings are written under the
current save path in `baseoq4/demos/`.

Stop cleanly with:

```text
stopMVD
```

openQ4 records to a `.mvd.part` file first. A clean stop adds the end marker and
renames it to `.mvd`. If the game or machine stops unexpectedly, the partial
file stays clearly marked and the last good data is not mistaken for a
complete demo.

`mvdStatus` shows a recording in progress: its file, size, elapsed time and
snapshot count. It also shows whether the last recording was saved, or why it
failed and where its partial file is.

### Markers

A recording marks the moments worth finding again: match phase and round
changes, timeouts and pauses, team score changes, every kill, flag pickups,
drops, returns and captures, and powerup pickups. Add your own note while
recording, from the console or over rcon:

```text
mvdMark "Overtime starts"
```

Players who join or rename during a recording keep their names in playback, so
you can follow them by name.

### Recover an interrupted recording

Every record in an MVD carries its own length and checksum, so the complete
part of an interrupted recording can be saved:

```text
mvdRecover match_name.mvd.part
```

openQ4 copies every complete record up to the first damaged or missing one,
rebuilds the timeline and writes the end marker into a new demo named
`match_name_recovered.mvd`, or the next free numbered name. The `.part` file is
never changed and no existing demo is overwritten. The console reports how many
snapshots were kept and why the copy stopped. A recording with no complete
snapshot cannot be recovered. Stop any recording or playback first.

The familiar Quake 4 names `recordNetDemo`, `stopNetDemo`, and `playNetDemo`
are aliases for this system. The files are still `.mvd`; openQ4 does not
pretend they are legacy `.netdemo` files.

## Inspect and play

Validate a recording and print its format, duration, record counts, and clean
end status:

```text
mvdInfo match_name
```

Play it with:

```text
playMVD match_name
```

You can instead select the file in **Demos** and choose **Play**. A compact
overlay shows the current time, duration, speed, and view. Press Escape to open
the interactive playback controls; playback pauses while the menu is open and
resumes on close only if it was running beforehand.

The controls include:

- pause/resume and single-frame step;
- -30, -10, +10, and +30 second skips;
- 0.25x, 0.5x, 1x, 2x, and 4x speed presets;
- **Follow next** and **Free roam** for MVDs;
- **Prev Marker** and **Next Marker**, when the demo has markers;
- **Auto-Track**, which cycles Off, Killer and Action;
- stop and close.

While watching an MVD, attack also follows the next active player and jump
leaves follow mode. Normal movement and look controls move the free camera.

### Jump to markers

`mvdMarkers` lists a demo's markers with their times. `demoMarker next` and
`demoMarker prev` jump to the next or previous one, landing two seconds before
it (`mvd_markerLeadMS` changes the lead-in). Add a kind to skip the rest:

```text
demoMarker next kill
demoMarker prev note
```

The kinds are `phase`, `round`, `pause`, `score`, `kill`, `flag`, `powerup` and
`note`.

### Let the camera follow the action

Auto-tracking picks the player to follow for you:

- **Killer** moves to whoever killed the player you are watching, a moment after
  the kill so you see it happen.
- **Action** follows the most interesting player: a flag carrier first, then a
  player with powerups, a recent run of kills or the lead. It holds each choice
  for a few seconds, so the camera does not flick between players.

Use the **Auto-Track** button, or `demoAutoTrack killer`, `demoAutoTrack
action` or `demoAutoTrack off`. Add `marine` or `strogg` to watch one team only.
Choosing a player yourself, or pressing attack or jump, hands the camera back to
you.

### Playback console commands

These commands work through the common playback controller when the active
format supports the action:

| Command | Action |
|---|---|
| `demoPause [0\|1]` | Toggle pause, or pause/resume explicitly. |
| `demoSpeed <0.05-16>` | Set a playback speed beyond the menu presets if needed. |
| `demoSeek <seconds>` | Go to an absolute time from the start. |
| `demoSkip <seconds>` | Move by a signed interval, such as `demoSkip -10`. |
| `demoStep` | Pause and advance one frame. |
| `demoFollow [next\|prev\|free\|<slot>\|<name>]` | Follow the next or previous player, a client slot, or a player by name in an MVD. |
| `demoFreeRoam` | Leave MVD follow mode for free flight. |
| `demoMarker next\|prev [kind]` | Jump to the next or previous MVD marker. |
| `demoAutoTrack [off\|killer\|action] [any\|marine\|strogg]` | Set auto-tracking, or cycle it with no arguments. |
| `demoStop` | Stop the active demo. |

A name matches whole names first, ignoring colors and case, then the one name
that contains what you typed; the console says when several names match. The
overlay names the player you follow.

MVD-only equivalents remain available: `mvdPause`, `mvdSpeed`, `mvdSeek`,
`mvdSkip`, `mvdStep`, `mvdFollow`, `mvdFollowNext`, `mvdFollowPrev`,
`mvdFreeRoam`, `mvdMarker` and `mvdAutoTrack`. Use `stopMVD` to stop MVD
recording or playback.

## What each format can do

| Format | Playback | Seeking and speed | Camera |
|---|---|---|---|
| openQ4 MVD `.mvd` 1.3 | Supported | Pause, speed, step, and seeking from the nearest keyframe | Full-world free roam, player follow, markers and auto-tracking |
| openQ4 MVD `.mvd` 1.0-1.2 | Supported, including compatible API-39, API-42, and API-44 1.0/1.1 recordings | Pause, speed, step, and replay-based seek | Full-world free roam, player follow and auto-tracking |
| openQ4 render `.demo`, including campaign demos | Supported | Pause, speed, step, replay-based seek and level markers | Fixed recorded camera |
| Retail Quake 4 render `.demo` | Not supported yet | None | Not available |
| Retail Quake 4 `.netdemo` | Not supported yet | None | Not available |
| Command `.cdemo` | Developer best effort | Play/stop only | Determined by the recorded simulation |
| `.mvd.part`, `.part`, `.ucmd` | Incomplete; not playable (`mvdRecover` saves an interrupted `.mvd.part`) | None | None |

A render demo stores the recorded renderer, sound, and GUI presentation, not a
complete multiplayer world. It therefore cannot provide genuine free roam or
player following. Rewinding restarts it and silently replays its commands to
the requested time in cooperative per-frame batches. The seeking overlay is
shown before restart work begins; a single indivisible map load or renderer
command can still take longer than the configured budget. Starting another
seek simply retargets that replay.

Retail Quake 4 `.demo` and `.netdemo` files use different stream/protocol
formats. The library identifies them as legacy and leaves **Play** disabled
instead of risking an incorrect decoder. Early openQ4 render demos remain
supported when their decoded stream can be verified, including historical
`OpenQ4 RDEMO` and openQ4-authored `Quake4 RDEMO` wrappers.

Supported historical openQ4 render demos are decoded defensively. If a
renderer or sound command, hash string, index, or top-level token is truncated
or malformed, playback closes the file and returns to the menu with a warning
instead of terminating openQ4.

`.cdemo` files are unversioned developer command logs whose consistency
depends on the matching engine and game build. They are not portable match
archives. openQ4 structurally preflights them before enabling Play and rejects
truncated or malformed files without entering their recorded map.

## Recording limits

These archived settings make unattended server recording safer:

| Setting | Default | Purpose |
|---|---:|---|
| `mvd_snapshotDelay` | `50` | Milliseconds between full-world snapshots. |
| `mvd_maxSnapshotMB` | `4` | Maximum uncompressed size of one snapshot. |
| `mvd_maxSizeMB` | `1024` | Maximum file size in MiB; `0` disables the limit. |
| `mvd_maxDurationMinutes` | `360` | Maximum recording duration; `0` disables the limit. |
| `mvd_enforceContent` | `1` | Refuse playback when declarations/content differ. |
| `mvd_seekBudgetMS` | `12` | Maximum replay-seek work per displayed frame. |
| `mvd_keyframeInterval` | `10` | Seconds between keyframes that let seeks skip most of the replay; `0` records none. |
| `demo_renderSeekBudgetMS` | `10` | Cooperative render-demo replay budget checked between stream commands. |

Lowering the snapshot delay increases temporal detail and file size. The
50 ms default matches the normal multiplayer snapshot interval.

## MVD versions, seeking, and damaged files

The MVD container has its own version, required-feature negotiation, per-record
versions, bounded lengths, and checksums. A newer openQ4 can remain compatible
with older recordings when their game schema and snapshot protocol are still
supported.

MVD 1.0 contains the checked match stream without a timeline index. MVD 1.1
adds an optional sparse timeline index containing the first snapshot,
approximately one entry per second, and the final snapshot while leaving the
required record layouts unchanged. MVD 1.2 records networked entities from every
active multiplayer instance and preserves instance-only reliable and positional
effects. Its reliable and snapshot records have a new version, while the reader
retains the version-1 decoder used by 1.0 and 1.1.
MVD 1.3 is the current writer: it adds markers, keyframes and name changes as
optional records, so an older openQ4 that reads 1.2 still plays a 1.3 demo,
without those extras.

The current reader accepts all four versions and reconstructs a timeline by
scanning checked snapshots when the index is absent. A valid 1.1 or 1.2 index
also lets the browser obtain duration and count information without first
scanning the whole match. A clean recording that ends before its first
scheduled snapshot legitimately has no index and a zero snapshot count; it
remains playable when its initialization and end marker are otherwise valid.

Snapshots are stored as changes from the previous one, so a seek has to rebuild
the world. An MVD 1.3 demo stores a keyframe every ten seconds: a complete copy
of the match state the next snapshot builds on. Rewinding, or jumping well
ahead, reloads the map, restores the nearest keyframe before the target and
replays at most the last ten seconds, so a rewind late in a long match no
longer replays everything before it. A
keyframe that fails its checks is never used; openQ4 falls back to replaying
from the start, which is also how MVD 1.0 to 1.2 demos seek. The overlay shows
**Seeking...** while that work is in progress.

Formats 1.0 and 1.1 keep their original exact game-API compatibility check.
Format 1.2 uses a separately versioned MVD game schema, allowing future game
updates to retain demo compatibility without pretending every game API change
is interchangeable. Required feature bits ensure a 1.2 file cannot claim
full-world instance support while omitting the data needed to provide it.

Playback stops safely when:

- the container major is unsupported;
- a required future feature or record is unknown;
- the network snapshot protocol is incompatible;
- the legacy game API or current MVD game schema is incompatible;
- the simulation rate differs;
- the content/declaration checksum differs;
- map state attempts to restore a cvar that is not network-synchronized;
- a record is truncated or fails its checksum.

Playback also limits how many records may be consumed while finding the
initial map state and in any one displayed frame. These internal ceilings keep
a valid long match smooth while preventing a damaged stream from causing an
unbounded startup or frame stall.

`mvd_enforceContent 0` bypasses only the content checksum for diagnostics. It
cannot make genuinely incompatible assets or declarations replay correctly.

For the implementation and future tournament/broadcast roadmap, see the
[demo playback and MVD architecture](../dev/multiview-demos.md).
