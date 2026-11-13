# Demos and multi-view demos

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

openQ4 records and plays back two main kinds of demo:

- **Campaign demos** record what you see and hear while you play single-player,
  across a whole session of levels, as one file.
- **Multi-view demos** (MVDs, `.mvd` files) record a whole multiplayer match on
  the server. Instead of one player's view, they hold the full match, so when
  you watch you can follow any player or fly a free camera.

Both appear in the **Demos** library, with the controls each format supports.

## Find your demos

Choose **Multiplayer → Demos** on the main menu, or enter `demoMenu` in the
console. The library lists every demo with its type, map, date, length and
whether it can be played. Filter it by **All**, **Multi-view**, **Render**,
**Legacy** and **Incomplete**.

Only the controls the selected demo supports are available. A demo file in
your own `demos/` folder (in your save path) can be deleted after you confirm;
demos inside packages are read-only.

## Watch a demo

Select a demo in **Demos** and choose **Play**, or enter `playMVD match_name` in
the console for a multi-view demo. A small overlay shows the time, length,
speed and view. Press Escape to open the playback controls; playback pauses
while they are open and resumes when you close them, if it was playing before.

The controls include:

- pause and resume, and step one frame;
- skip back or forward 10 or 30 seconds;
- speeds of 0.25x, 0.5x, 1x, 2x and 4x;
- **Follow next** and **Free roam** in multi-view demos;
- **Prev Marker** and **Next Marker**, when the demo has markers;
- **Auto-Track**, which cycles Off, Killer and Action;
- stop and close.

In a multi-view demo, attack also follows the next active player and jump
leaves follow mode. Your normal movement and look controls fly the free camera.

### Jump to markers

A multi-view demo marks the moments worth finding again: match phase and round
changes, timeouts and pauses, team score changes, every kill, flag pickups,
drops, returns and captures, and powerup pickups, plus any notes added while
recording.

`mvdMarkers` lists a demo's markers with their times. `demoMarker next` and
`demoMarker prev` jump to the next or previous one, landing two seconds before
it (`mvd_markerLeadMS` changes the lead-in). Add a kind to skip the others:

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

Use the **Auto-Track** button, or `demoAutoTrack killer`, `demoAutoTrack action`
or `demoAutoTrack off`. Add `marine` or `strogg` to watch one team only.
Choosing a player yourself, or pressing attack or jump, hands the camera back to
you.

### Playback console commands

These work for any demo whose format supports the action:

| Command | Action |
|---|---|
| `demoPause [0\|1]` | Toggle pause, or pause (`1`) or resume (`0`). |
| `demoSpeed <0.05-16>` | Set a playback speed beyond the menu presets. |
| `demoSeek <seconds>` | Go to a time, counted from the start. |
| `demoSkip <seconds>` | Move back or forward, such as `demoSkip -10`. |
| `demoStep` | Pause and advance one frame. |
| `demoFollow [next\|prev\|free\|<slot>\|<name>]` | In a multi-view demo, follow the next or previous player, a player slot or a player by name. |
| `demoFreeRoam` | Leave follow mode and fly freely. |
| `demoMarker next\|prev [kind]` | Jump to the next or previous marker. |
| `demoAutoTrack [off\|killer\|action] [any\|marine\|strogg]` | Set auto-tracking, or cycle it with no arguments. |
| `demoStop` | Stop the demo. |

A name matches whole names first, ignoring colours and case, then the one name
that contains what you typed; the console says when several names match. The
overlay names the player you follow.

The older multi-view-only names still work: `mvdPause`, `mvdSpeed`, `mvdSeek`,
`mvdSkip`, `mvdStep`, `mvdFollow`, `mvdFollowNext`, `mvdFollowPrev`,
`mvdFreeRoam`, `mvdMarker` and `mvdAutoTrack`. `stopMVD` stops multi-view
recording or playback.

## Record your campaign

A single-player recording follows you through the whole session. Open the
console and enter:

```text
recordDemo my_campaign
```

Enter it at the title screen and recording starts with the first frame of the
next level you start, continue or load. Enter it during a level and it starts
there.

One file keeps recording through level changes, loading saves, deaths and
reloads, difficulty restarts and chapter starts. Time in menus, on the pause
screen or on loading screens is left out. Recording stops when you enter
`stopRecording`, quit to the main menu, quit the game or join a multiplayer
game. `demoRecordStatus` shows whether a recording is waiting, running or
between levels, with its file, length and number of levels.

Recordings go to `demos/` in your save path, under the active campaign's game
folder. In **Demos** a campaign recording is listed as **Campaign demo**, with
its first level, length and number of levels. A demo recorded in the other
campaign (Quake 4 or The Awakening) says so and plays only after you switch
campaigns. A recording cut short by a crash or power loss is marked
**Interrupted recording** and still plays up to its last complete frame.

During playback each new level shows its loading screen, and the overlay names
the level you are watching. **Prev Marker** and **Next Marker**, or
`demoMarker prev` and `demoMarker next`, jump between level starts; Prev goes to
the start of the current level first. Seeking, speed, pause and stepping work
across levels.

A campaign demo stores what you saw and heard, including the HUD and in-world
screens, not the game itself, so the camera cannot leave your view. Grouped HUD
panels play back without their shaped edges.

Recording does not change how the game runs. It keeps every frame you see, at
your own frame rate, and plays back at that rate on a display fast enough to
show it. Demos from older openQ4 versions, which held the game to 30 frames per
second while recording, still play at 30.

File size grows with the frame rate. A busy level takes about 1.7 MB per second
at 60 frames per second, so a long campaign recording needs several gigabytes.
To record smaller files, lower your frame cap first, for example
`com_maxfps 60`.

## Record a match

Multi-view demos are recorded by the server, so you need to be hosting: start a
multiplayer server, then enter in its console:

```text
recordMVD match_name
```

Leave out the name to use an unused `mvd` file name. Recordings are saved in
`baseoq4/demos/` in your save path. Stop cleanly with:

```text
stopMVD
```

openQ4 records to a `.mvd.part` file first. A clean stop finishes the file and
renames it to `.mvd`, so a recording cut short by a crash is never mistaken for
a complete demo.

`mvdStatus` shows a recording in progress: its file, size, length and number of
snapshots (a snapshot is one update of the whole match state). It also shows
whether the last recording was saved, or why it failed and where its partial
file is.

The familiar Quake 4 commands `recordNetDemo`, `stopNetDemo` and `playNetDemo`
use this system too. Their files are `.mvd`, not the old `.netdemo` format.

### Add markers and notes

Markers are added automatically (see [Jump to markers](#jump-to-markers)). Add
your own note while recording, from the console or over remote console:

```text
mvdMark "Overtime starts"
```

Players who join or rename during a recording keep their names in playback, so
you can follow them by name.

### Recover an interrupted recording

Every part of an MVD carries its own length and checksum, so the complete part
of an interrupted recording can be saved. Stop any recording or playback first,
then enter:

```text
mvdRecover match_name.mvd.part
```

openQ4 copies everything up to the first damaged or missing piece and writes it
as a finished demo named `match_name_recovered.mvd`, or the next free numbered
name. The `.part` file is never changed and no existing demo is overwritten. The
console reports how many snapshots were kept and why the copy stopped. A
recording with no complete snapshot cannot be recovered.

## What each format can do

| Format | Playback | Seeking and speed | Camera |
|---|---|---|---|
| openQ4 MVD `.mvd` 1.3 | Supported | Pause, speed, step, and fast seeking from the nearest keyframe | Free camera over the whole match, player follow, markers and auto-tracking |
| openQ4 MVD `.mvd` 1.0–1.2 | Supported, from game versions openQ4 still recognises | Pause, speed, step, and replay-based seek (replays from the start) | Free camera over the whole match, player follow and auto-tracking |
| openQ4 render `.demo`, including campaign demos | Supported | Pause, speed, step, replay-based seek and level markers | The recorded view only |
| Retail Quake 4 render `.demo` | Not supported yet | None | None |
| Retail Quake 4 `.netdemo` | Not supported yet | None | None |
| Command `.cdemo` | Developer use only | Play and stop only | Whatever the recording did |
| `.mvd.part`, `.part`, `.ucmd` | Incomplete and not playable (`mvdRecover` can save an interrupted `.mvd.part`) | None | None |

**Render demos** store the picture, sound and menus you saw, not the whole
match, so they cannot offer a free camera or player following. Rewinding one
restarts it and fast-forwards to the time you asked for, showing **Seeking...**
meanwhile; a map load can still cause a short pause. Starting another seek just
changes the target. A render demo recorded in a multiplayer match by an older
openQ4 version plays in slow motion, because those versions recorded every
frame but replayed at 30 frames per second; record it again for real-time
playback.

Render demos from early openQ4 versions still play when openQ4 can verify
them. If one is damaged, playback closes it and returns to the menu with a warning instead of crashing openQ4.

**Retail Quake 4** `.demo` and `.netdemo` files use different formats. The
library marks them as **Legacy** and keeps **Play** unavailable rather than
risk playing them wrongly.

**Command demos** (`.cdemo`) are developer logs that replay correctly only with
the exact openQ4 version that made them, so they are not a way to share
matches. openQ4 checks each one before enabling **Play** and rejects a damaged
file without loading its map.

## Recording limits

These settings protect a server that records unattended. They are saved in
your configuration.

| Setting | Default | What it does |
|---|---:|---|
| `mvd_snapshotDelay` | `50` | Milliseconds between snapshots. Lower values record finer detail and bigger files; the default matches the normal multiplayer update rate. |
| `mvd_maxSnapshotMB` | `4` | Largest size of one snapshot, in MB, before compression. |
| `mvd_maxSizeMB` | `1024` | Largest file size in MB; `0` for no limit. |
| `mvd_maxDurationMinutes` | `360` | Longest recording, in minutes; `0` for no limit. |
| `mvd_libraryQuotaMB` | `0` | Largest total size of your recorded MVDs, in MB. When it is full, new recordings are refused; existing ones are never deleted. `0` for no limit. |
| `mvd_keyframeInterval` | `10` | Seconds between keyframes, which make seeking fast; `0` records none. |
| `mvd_enforceContent` | `1` | Refuses playback when the demo's game data differs from yours. |
| `mvd_seekBudgetMS` | `12` | The most time per displayed frame spent replaying while seeking a multi-view demo. |
| `demo_renderSeekBudgetMS` | `10` | The same for render demos. |
| `mvd_markerLeadMS` | `2000` | How many milliseconds before a marker a jump lands. |

## Versions, seeking and damaged files

New recordings use MVD 1.3. openQ4 still plays MVD 1.0, 1.1 and 1.2 demos.

- **MVD 1.3** adds markers, keyframes and player name changes as optional
  extras, so an older openQ4 that reads 1.2 still plays a 1.3 demo, without
  them.
- **MVD 1.2 and later** record every active multiplayer instance, such as each
  Tourney arena. From 1.2 on, a demo records its own version of the game data it contains, so it keeps playing after
  openQ4 updates that do not change that data.
- **MVD 1.1 and later** can keep a timeline, so the library shows a demo's
  length without reading the whole file. When a demo has none, as in 1.0,
  openQ4 builds one by reading the file.
- **MVD 1.0 and 1.1** play when they come from a game version openQ4 still
  recognises.

A recording stopped cleanly before its first snapshot still plays, as long as
the rest of the file is valid.

**Seeking.** Snapshots are stored as changes from the one before, so seeking
has to rebuild the match. An MVD 1.3 demo stores a keyframe, a complete copy of
the match state, every ten seconds. Rewinding, or jumping well ahead, reloads
the map, restores the nearest keyframe before the target and replays at most
the last ten seconds. A keyframe that fails its checks is never used; openQ4
replays from the start instead, which is also how MVD 1.0 to 1.2 demos seek.
The overlay shows **Seeking...** while it works.

**Playback stops safely** when:

- the demo is from a newer openQ4 and needs something this version does not
  have;
- its network or game data version is incompatible;
- it was recorded at a different simulation rate (game updates per second);
- its game data checksum differs from yours;
- it tries to change a setting that is not network-synchronized (one the server
  never shares with players);
- part of it is cut short or fails its checksum.

openQ4 also limits how much of a demo it reads at once, so a long match plays
smoothly and a damaged file cannot freeze the game.

`mvd_enforceContent 0` skips only the game data checksum, for troubleshooting.
It cannot make demos recorded with different game files play correctly.

For how the demo system works internally, see the developers'
[demo playback and MVD architecture](../dev/multiview-demos.md).
