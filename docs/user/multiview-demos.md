# Demos and multi-view demos

> This guide matches openQ4 0.15.0. Anything new in 0.15.0 is marked **New in 0.15.0**. See [release status](release-status.md) for what each download includes.

openQ4 records and plays back two main kinds of demo:

- **Campaign demos** record what you see and hear while you play single-player,
  across a whole session of levels, as one file.
- **Multi-view demos** (MVDs, `.mvd` files) record a whole multiplayer match on
  the server. Instead of one player's view, they hold the full match, so when
  you watch you can follow any player or fly a free camera.
- **Your own view of a match**, recorded on any server you join, is a `.mvd`
  file too (**after 0.15.0**). It holds what you saw, so it plays back from
  your point of view only.
- **Quake 4 1.4.2 network demos** (`.ndmo85` files), recorded by the original
  game, play too (**after 0.15.0**). See
  [Watch a Quake 4 1.4.2 network demo](#watch-a-quake-4-142-network-demo).

Both appear in the **Demos** library, with the controls each format supports.

## Find your demos

Choose **Multiplayer → Demos** on the main menu, or enter `demoMenu` in the
console. The library lists every demo with its type, map, date, length and
whether it can be played. Filter it by **All**, **Multi-view**, **Render**,
**Legacy** and **Incomplete**.

Only the controls the selected demo supports are available. A demo file in
your own `demos/` folder (in your save path) can be deleted after you confirm;
demos inside packages are read-only.

**After 0.15.0:** the details line names the players in a multi-view demo, or
whose view a recording of one player's view shows, and a recording made where
there was a view shows its own picture in place of the map's levelshot (a
320x240 `demos/<name>_thumb.png` taken three seconds in). `demoFind <text>`
lists the demos whose name, map, mode, server, players or recorder contain the
text.

## Watch a demo

Select a demo in **Demos** and choose **Play**, or enter `playMVD match_name` in
the console for a multi-view demo. A small overlay shows the time, length,
speed and view. Press Escape to open the playback controls; playback pauses
while they are open and resumes when you close them, if it was playing before.

The controls include:

- pause and resume, and step one frame;
- skip back or forward 10 or 30 seconds;
- speeds of 0.25x, 0.5x, 1x, 2x and 4x;
- **Follow next** and **Free roam** in multi-view demos, and **Players**, a
  list of everyone recorded to follow by name (**after 0.15.0**);
- **Step back**, a frame at a time (**after 0.15.0**);
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

### Watch a Quake 4 1.4.2 network demo

> **After 0.15.0.** This describes the development build.

The original Quake 4 records multiplayer demos with `recordNetDemo`. Version
1.4.2 names them by network protocol, so its files end in `.ndmo85`, and it
installs two of its own in `q4mp/demos`: `id_perftest.ndmo85` (one player's
view of a match on Over The Edge) and `id_serverdemo.ndmo85` (the server's
recording of the same match).

Copy a `.ndmo85` file into the `demos/` folder in your openQ4 save path. It then
appears in **Demos**, with its map, length and players, and plays with **Play**.
You can also enter `playNetDemo id_perftest.ndmo85` in the console.

- A demo a player recorded shows that player's view, with their HUD and the
  match as they saw it. You can pause, change speed, step frames and seek;
  seeking back replays the demo from the start.
- A demo a server recorded starts on the free camera. Follow any player or fly
  around the match, as in a multi-view demo.
- `playNetTimeDemo id_perftest.ndmo85`, or `timeDemo id_perftest.ndmo85`, plays
  one as a benchmark (see [Time a demo](#time-a-demo)).

A network demo plays when it was recorded at 60 game frames a second (the
standard setting) and your Quake 4 installation has the game data it was
recorded with; a full installation does. Otherwise the library says why it
cannot play. Demos from Quake 4 releases before 1.4.2 (`.netdemo` files) use an
older layout: the library lists them as **Legacy** and does not play them. A
damaged file is refused, with the reason in the console, and never stops openQ4.

### Playback console commands

These work for any demo whose format supports the action:

| Command | Action |
|---|---|
| `demoPause [0\|1]` | Toggle pause, or pause (`1`) or resume (`0`). |
| `demoSpeed <0.05-16>` | Set a playback speed beyond the menu presets. |
| `demoSeek <seconds>` | Go to a time, counted from the start. |
| `demoSkip <seconds>` | Move back or forward, such as `demoSkip -10`. |
| `demoStep [frames]` | Pause and advance one frame, or that many; a negative number steps back (**after 0.15.0**). |
| `demoFollow [next\|prev\|free\|<slot>\|<name>]` | In a multi-view demo, follow the next or previous player, a player slot or a player by name. |
| `demoFreeRoam` | Leave follow mode and fly freely. |
| `demoMarker next\|prev [kind]` | Jump to the next or previous marker. |
| `demoAutoTrack [off\|killer\|action] [any\|marine\|strogg]` | Set auto-tracking, or cycle it with no arguments. |
| `demoPlayers` | List a multi-view demo's recorded players, to follow with `demoFollow` (**after 0.15.0**). |
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
**Interrupted recording**. It still shows its length, seeks and has its level
markers, and it plays up to the last moment that reached the disk, at most
about half a second before the cut.

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

File size grows with the frame rate. A busy level takes about 0.7 MB per second
at 60 frames per second, roughly 2.5 GB an hour, and a quiet one much less.
That is about 40% of what openQ4 0.14.1 and earlier wrote for the same play.
Files can grow past 2 GB. To record smaller files, lower your frame cap first,
for example `com_maxfps 60`.

A recording never fills your disk. It stops by itself, as a complete demo, when
less than 256 MB is free where your saves are kept, so the game can still
save. `demo_recordMinFreeMB` changes that amount and `demo_recordMaxSizeMB`
stops a recording at a size you choose.

### Shrink a recording from an older version

`compressDemo my_campaign` writes `my_campaign_compressed.demo` in the new,
smaller format; the original is left alone. The copy plays exactly as before,
though only recordings made by this version seek quickly.

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
**After 0.15.0**, `playNetDemo` also plays a Quake 4 1.4.2 `.ndmo85` file.

### Record your view on any server

> **After 0.15.0.** This describes the development build; 0.15.0 records only
> on a server you host.

On a server you joined, enter:

```text
record match_name
```

and later `stopRecord`. Leave out the name to use an unused `mvd` file name.
The file goes to `baseoq4/demos/` in your save path, like a server's
recording, and appears in **Demos**. `recordNetDemo` and `recordMVD` do the
same when you are not hosting; when you host, all three record the whole
match.

The recording holds what your game received from the server and your own
moves, so it plays back exactly what you saw: your view, the players and
effects around you, your HUD and the scoreboard. It cannot follow another
player or fly free, and the **Follow** and **Free roam** controls are not
offered. Pause, speed, stepping and skipping work, and seeking takes about a
second: the recording keeps a keyframe every ten seconds (`mvd_keyframeInterval`).

A recording ends by itself when the map changes or you leave the server.
`mvdStatus` shows a recording in progress (`view=client`); `mvdInfo` describes
a finished one, including whose view it is. `mvdRecover` saves the complete
part of one that was cut short, as for a server's recording.

### Record every match automatically

> **After 0.15.0.**

Set `ui_autoAction` to the things to do for every match you play:

```text
ui_autoAction "demo ss stats"
```

- `demo` records each match from the moment it goes live until the result.
- `ss` takes a screenshot of the final scoreboard.
- `stats` writes the result to `stats/<match>.json`: the map, mode, team
  scores and each player's score, kills, deaths and damage.

The files share one name made of the date and time, the mode, the map and your
name, such as `20261123-161522_dm_q4dm1_player`. A recording you started
yourself is left alone. A server can do the same for every match it runs with
`si_autoAction "demo stats"`, which players see in its server information; see
the [server setup guide](server-setup.md).

### Time a demo

`timeDemo match_name.mvd` plays a multi-view demo, or a recording of your own
view, as a benchmark: each frame moves the match on by one simulation tic and
frames are not capped, so the same file always shows the same frames. The
console reports the frames, seconds, frame rate and the slowest frame;
`timeDemoQuit` quits afterwards. A hidden or minimized window may still be held
to the display's rate.

### Export a demo as pictures

`demoExport match_name.mvd 30` writes a multi-view demo, or a recording of your
own view, as one PNG picture every thirtieth of a second of the match, in
`exports/match_name/` in your save path (`frame00000.png` on); add `quit` to
quit afterwards. Any video editor or encoder turns the pictures into a video.

The sound comes with it: `exports/match_name/match_name.wav` is the match as
you would have heard it, at your sound device's rate and in its speaker layout
(stereo, or 5.1 and so on), and exactly as long as the pictures. Nothing plays
through your speakers while the export runs; your sound device comes back when
it ends. To make a video with `ffmpeg`:

```text
ffmpeg -framerate 30 -i frame%05d.png -i match_name.wav -c:v libx264 -pix_fmt yuv420p -c:a aac match_name.mp4
```

Use the same number for `-framerate` as you gave `demoExport`. The sound track
needs sound to be on (`s_noSound 0`); otherwise, or if your sound library cannot
mix into a file, the console says why and only the pictures are written.

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
| openQ4 `.mvd` of one player's view (after 0.15.0) | Supported | Pause, speed, step, and replay-based seek (replays from the start) | The recorder's view only |
| openQ4 render `.demo`, including campaign demos | Supported | Pause, speed, step, level markers, and fast seeking from the nearest keyframe (older recordings replay from the start) | The recorded view only |
| Retail Quake 4 render `.demo` (after 0.15.0) | Supported | Pause, speed, step, and replay-based seek (replays from the start) | The recorded view only |
| Quake 4 1.4.2 network demo `.ndmo85` (after 0.15.0) | Supported | Pause, speed, step, and replay-based seek (replays from the start) | A player's recording: their view only. A server's recording: free camera and player follow |
| Retail Quake 4 `.netdemo`, before 1.4.2 | Not supported | None | None |
| Command `.cdemo` | Developer use only | Play and stop only | Whatever the recording did |
| `.mvd.part`, `.part`, `.ucmd` | Incomplete and not playable (`mvdRecover` can save an interrupted `.mvd.part`) | None | None |

**Render demos** store the picture, sound and menus you saw, not the whole
match, so they cannot offer a free camera or player following. A recording made
by this version stores a keyframe every ten seconds, so seeking, backwards or
forwards, takes about a second, a little longer when it lands in another level
and that level has to load. A recording from an older version restarts and
fast-forwards to the time you asked for instead. **Seeking...** shows
meanwhile, and starting another seek just changes the target. A render demo recorded in a multiplayer match by an older
openQ4 version plays in slow motion, because those versions recorded every
frame but replayed at 30 frames per second; record it again for real-time
playback.

Render demos from early openQ4 versions still play when openQ4 can verify
them. If one is damaged, playback closes it and returns to the menu with a warning instead of crashing openQ4.

A recording made by this version is checked piece by piece as it plays. If part
of the file is damaged, playback skips ahead to the next keyframe or level,
at most ten seconds later, tells you in the console how much was lost, and
carries on to the end.

**Retail Quake 4 render demos** (after 0.15.0): a `.demo` recorded with the
original game's `recordDemo` plays like any other render demo. Put it in the
`demos/` folder of your save path; the library lists it as **Retail Quake 4
render demo** with its level, and `playDemo name` plays it from the console.
It shows the view, the HUD, the screens in the world, the effects and the sound
the original game recorded, with your openQ4 graphics settings. It needs the
same Quake 4 game files as the recording: a demo of a level or effect your
installation does not have stops with a message. A damaged file stops with a
warning in the console and returns to the menu. Seeking replays the demo from
its start.

**Quake 4 `.netdemo` files from before 1.4.2** use a different format. The
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
| `mvd_snapshotDelay` | `50` | Milliseconds between snapshots: `50` records 20 a second, the normal multiplayer update rate. Lower values record finer detail and bigger files. Recordings made before 17 November 2026 took 15 a second at this setting, so new ones are about a third larger. |
| `mvd_maxSnapshotMB` | `4` | Largest size of one snapshot, in MB, before compression. |
| `mvd_maxSizeMB` | `1024` | Largest file size in MB; `0` for no limit. |
| `mvd_maxDurationMinutes` | `360` | Longest recording, in minutes; `0` for no limit. |
| `mvd_libraryQuotaMB` | `0` | Largest total size of your recorded MVDs, in MB. When it is full, new recordings are refused; existing ones are never deleted. `0` for no limit. |
| `mvd_keyframeInterval` | `10` | Seconds between keyframes, which make seeking fast; `0` records none. |
| `mvd_enforceContent` | `1` | Refuses playback when the demo's game data differs from yours. |
| `mvd_seekBudgetMS` | `12` | The most time per displayed frame spent replaying while seeking a multi-view demo. |
| `demo_renderSeekBudgetMS` | `10` | The same for render demos. |
| `demo_recordKeyframeSeconds` | `10` | Seconds between keyframes in a render demo, which make seeking fast and let playback recover from a damaged file; `0` records none. |
| `demo_recordMinFreeMB` | `256` | A render demo recording stops when less than this many MB are free on the disk your saves are on; `0` never stops for it. |
| `demo_recordMaxSizeMB` | `0` | Largest render demo file in MB; `0` for no limit. |
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

When the game data checksum differs, the console names the packs that differ
from the ones the demo was recorded with, or says that every pack matches and
loose files or a mod changed the game data (**after 0.15.0**; older demos do
not record their packs).

`mvd_enforceContent 0` skips only the game data checksum, for troubleshooting.
It cannot make demos recorded with different game files play correctly.

For how the demo system works internally, see the developers'
[demo playback and MVD architecture](../dev/multiview-demos.md).
