# Android setup, backups and logs

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

openQ4 runs Quake 4 on Android phones and tablets, with touch controls,
controllers or a keyboard. This guide covers setting it up, playing with touch
controls, sizing the menus, and backing up your saves.

The Android version is **experimental**. It has been tested on one phone so
far, a Samsung Galaxy S25, and it does not yet draw every advanced effect of
the PC version.

## What you need

- An ARM64 phone or tablet with Android 7.0 (API 24) or newer and OpenGL ES 3.0.
- Your own Quake 4 1.4.2 game files: the `q4base` folder from your PC
  installation. The app does not include them.
- The signed openQ4 APK from the
  [0.14.1 release](https://github.com/themuffinator/openQ4/releases/tag/v0.14.1).
  Android may ask you to allow installing apps from your browser or file
  manager.

The setup screen is available in English, German, Spanish and French.

## Prepare the game files

1. Open **openQ4 setup**, choose a language and select **Continue**.
2. Select **Choose game folder**, then choose the `q4base` folder from your own
   Quake 4 installation. It can be a folder inside Downloads or on a connected
   USB drive.
3. Wait while setup copies and checks the game files. Fix any error about a
   required file before you play; missing optional content or language files
   only give a warning.
4. When the files are ready, select **Single player** or **Multiplayer**.

**Cancel** keeps the files already copied. **Retry last operation** checks
those and starts the unfinished file again. If the source folder disappears,
reconnect it or choose it again. If you copied the files into the app's
game-data folder yourself, use **Check installed files**.

## Play with touch controls

- Drag on the left half of the game view to move; drag on a free area of the
  right half to look.
- Hold the crosshair button to fire. Dragging from that button also turns the
  view. The up and down arrow buttons jump and crouch.
- Tap the double chevron to cycle weapons, or hold it to open the weapon wheel.
  Drag toward a weapon and release to select it.
- The curved arrow reloads and the magnifier zooms, when the current weapon
  can.
- Tap **Pause** in the upper-left corner, or use Android's Back, to open the
  pause menu. During cinematics, Pause skips as the game normally allows.

Using a controller, keyboard or mouse hides the touch controls; touching the
game view brings them back. Multiplayer adds scoreboard and chat buttons at the
top.

### Portrait play

The game plays in landscape unless you choose **Landscape and portrait** on the
Interface page (see [Adjust menu and text size](#adjust-menu-and-text-size)).
Held upright, the game view sits at the top and the band below it becomes the
thumb deck: drag in its left half to move, and find fire in its right half with
the weapon button above it, jump and crouch toward the edge, and zoom and
reload toward the middle. Dragging anywhere else looks. The Remastered HUD
moves health, armour and ammunition above the deck.

Turning the device keeps your game, but releases any buttons you are holding.
On shorter phones some buttons can sit above the deck, and at larger button
sizes the controls reach into the game view; use the layout editor below to
move them.

### Choose a control style

Select **Controls** in Android setup:

- **Default**, **Compact** or **Left-handed**. Left-handed moves the movement
  area to the right and mirrors the buttons. Compact draws smaller buttons that
  are still easy to tap.
- **Button size** and **Look sensitivity**. Drag in the test grid to try the
  look speed; it never sends movement or fire to a running game.
- The page also lists the controllers it finds when it opens.

Select **Apply controls** to save. Your choices take effect the next time the
game starts or you return to it from setup. **Discard changes** keeps your
previous settings, and unsaved choices are kept as a draft if setup is
interrupted.

### Edit the touch layout

Select **Edit layout** on the Controls page, or in the game open
**Settings → Controls** and select **Touch Layout** at the end of the Other
tab. The editor shows your screen with every control and its name:

- Drag a control to move it. It snaps to a grid and keeps clear of the other
  controls, the screen edges and the crosshair; if the spot is taken, it moves
  to the nearest free place.
- Choose a control by tapping it or from the **Control** list, then set its
  **Size** (75–150%) and **Opacity** (10–80%), or **Hide this control**. Pause
  always stays visible, because it also skips cinematics.
- **Orientation** switches between the landscape and portrait layouts; each
  keeps its own. **Touch layout** changes the preset the layout starts from.
- **Reset this control** and **Reset this orientation** go back to the preset.

Select **Apply layout** to save; it takes effect when you return to or start
the game. Edits are kept as a draft if setup is interrupted, and **Discard
changes** keeps the saved layout. If a layout does not fit the game window,
for example in split-screen, the game uses the preset instead.

## Adjust menu and text size

Select **Interface** in Android setup to choose how large the game's menus and
text appear. **Viewing profile** sets the starting sizes:

| Viewing profile | Interface size | Text size |
| --- | --- | --- |
| Automatic | Phone, or Tablet on a large screen | As that profile |
| Desk (monitor) | 100% | 100% |
| Couch (television) | 150% | 125% |
| Handheld | 125% | 100% |
| Phone, Tablet | 100% | Your Android font size, 100–200% |

Choose a percentage under **Interface size** (75–200%) or **Text size**
(100–200%) to override the profile; **Profile default** goes back to it. The
sample heading and text preview the result, and the page shows the sizes the
game will use. The game still fits its menus to the screen, so on a phone a
larger interface size may not make them bigger; text size always applies.

**Screen orientation** chooses **Landscape** (either way up; the default) or
**Landscape and portrait**, which turns the game with the device unless
Android's rotation lock is on. Portrait play is new and still being tested on
phones.

Select **Apply interface** to save. The game uses these sizes and the
orientation the next time it starts or you return to it from setup. Each Apply
takes effect once: if you later change the UI scale or text size inside the
game, returning from setup keeps your in-game changes until you apply the
Interface page again. **Discard changes** keeps your previous settings.

## Leaving the game

Switching to another app pauses single player and first saves it as
**Suspended**. If Android closes openQ4 while it is in the background,
**Continue** on the title screen picks up from that save. It is not made
during cinematics, after death or a failed objective, or when storage is
nearly full. To turn it off, enter `com_suspendSave 0` in the console.

In multiplayer the server drops a player who is away for 40 seconds or more.
Coming back after that reconnects you to the same server straight away; after
a shorter break the game checks the connection and reconnects if the server no
longer answers. If the server has gone, you see the usual connection message.

## Return to setup during play

In the classic menus, open **Settings** and select **Android setup**. In the
new interface, **Settings → Controls → Touch Layout** (on the Other tab) opens
setup at the layout editor. Select **Return to game** at the top of setup to go
back to your game; single player stays paused until you resume it. This works
even before you have chosen a setup language.

Importing, checking files and exporting are unavailable while the game is
running or closing. Quit the game first.

## Back up saved games or export logs

1. Quit the game, then reopen **openQ4 setup**. File operations become
   available once the game has finished closing.
2. Under **Backups and diagnostics**, select **Back up saved games** or
   **Export logs**. These work even if the game files have not passed their
   check.
3. In Android's file picker, choose where to save the suggested ZIP file.
   Choose storage outside the app if the copy must survive removing the app.
4. Wait for the export-complete message. If you chose a cloud service, also
   wait for its upload to finish.

A save backup holds everything in each game's `savegames` folder, including
descriptions and preview pictures. A log export holds each game's `logs`
folder, including setup diagnostics. The ZIP keeps those folders and adds
`openq4-export.tsv`, a list of the files with their sizes and checksums. Your
Quake 4 game files, unpacked data and settings are not included. Logs can
contain file paths and server addresses, so review them before sharing.

Setup builds the ZIP in the app's own storage before copying it to your
destination, so you need free space for roughly the size of the files being
exported, in the app and at the destination; setup tells you the exact amount
if space is short. One export can hold up to 2 GiB and 4,096 files.

If an export is cancelled, interrupted or fails, your saves and logs stay in
the app. The destination may be left with an empty or incomplete ZIP: delete
it in Files and try again with a new destination. After the app restarts, a
retry starts a fresh export and asks for a destination again.

Setup cannot restore a backup ZIP yet. Keep your current installation and its
app data until you have a way to restore on your device and game version.

## Update without removing saves

Install updates over the existing app from the same source, so the signature
matches. Uninstalling the app or clearing its data deletes its saves, so make a
backup before you change installations. Upgrading between releases has not yet
been tested on every device.
