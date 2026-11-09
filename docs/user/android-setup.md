# Android setup, backups and logs

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

The Android app is experimental. It needs your own Quake 4 1.4.2 game data;
retail files are not included in the APK. The current setup screen supports
English, German, Spanish and French. The standalone app includes experimental
touch gameplay controls and also supports controllers and keyboards.

## Prepare the game files

1. Open **openQ4 setup**, choose a language and select **Continue**.
2. Select **Choose game folder**, then choose the `q4base` folder from your own
   Quake 4 installation. A subfolder inside Downloads or a connected USB drive
   can hold the source files.
3. Wait for copying, archive verification and preparation of bundled files.
   Required-file errors must be resolved before play. Missing optional content
   or language media produces a warning.
4. Select **Single player** or **Multiplayer** when the files are ready.

**Cancel** keeps completed archives. **Retry last operation** checks those
copies and starts the unfinished archive again. If the source folder becomes
unavailable, reconnect it or choose it again. If files were copied manually
into the app’s game-data directory, use **Check installed files**.

## Play with touch controls

- Drag on the left half of the game view to move; drag on a free area of the
  right half to look.
- Hold the crosshair button to fire. Dragging from that button also turns the
  view. The up and down arrow buttons jump and crouch.
- Tap the double chevron to cycle weapons, or hold it to open the weapon wheel.
  Drag toward a weapon and release to select it.
- The curved arrow reloads and the magnifier zooms when the current weapon
  supports those actions.
- Tap Pause in the upper-left corner, or use Android Back, to open the pause
  menu. During cinematics, Pause follows the game's skip behavior.

Using a controller, keyboard or mouse hides the gameplay controls; touching
the game view brings them back. Multiplayer adds scoreboard and chat buttons
at the top.

The game plays in landscape unless you choose **Landscape and portrait** on
the Interface page (below). Held upright, the band below the game view becomes
the thumb deck: drag in its left half to move, and the fire button sits in its
right half with the weapon button above it, jump and crouch toward the edge,
and zoom and reload toward the middle. Dragging anywhere else looks. The
Remastered HUD moves its health, armor and ammunition readouts up above the
deck. Turning the
device keeps your game; buttons held at that moment are released. On shorter
phones the upper buttons can sit above the deck, and at larger button sizes
the controls climb into the view; the layout editor (below) can move them.

Select **Controls** in Android setup to choose Default, Compact or Left-handed.
Left-handed moves the movement area to the right and mirrors the buttons.
Compact uses smaller artwork while retaining touch targets of at least 48 dp.
Adjust **Button size** and **Look sensitivity**, then drag in the test grid to
try the look speed. The page also lists controllers detected when it opens.

Select **Apply controls** to save. These choices take effect at the next game
launch or return from setup. **Discard changes** keeps the previously applied
settings. Unapplied choices are retained as a draft if setup is interrupted;
the test grid never sends movement or fire commands to the running game.

### Edit the touch layout

Select **Edit layout** on the Controls page, or in the game open **Settings**,
then **Controls**, and select **Touch layout** at the end of the Other tab.
The editor shows your screen with every control and its name:

- Drag a control to move it. It snaps to an 8 dp grid and keeps clear of the
  other controls, the screen edges and the crosshair; if the spot is taken it
  moves to the nearest free place.
- Choose a control by tapping it or from the **Control** list, then set its
  **Size** (75–150%) and **Opacity** (10–80%), or **Hide this control**. Pause
  always stays visible, because it also skips cinematics.
- **Orientation** switches between the landscape and portrait layouts; each
  keeps its own. **Touch layout** changes the preset the layout starts from.
- **Reset this control** and **Reset this orientation** go back to the preset.

Select **Apply layout** to save it; it takes effect when you return to or
launch the game. Edits are kept as a draft if setup is interrupted, and
**Discard changes** keeps the applied layout. If a layout does not fit the
game window, for example in split-screen, the game shows the preset instead.

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
(100–200%) to replace the profile's value; **Profile default** follows the
profile again. The sample heading and text preview the result, and the page
shows the sizes the game will use. The game still fits its menus to the screen,
so on a phone a larger interface size may not enlarge them; text size
always applies.

**Screen orientation** chooses **Landscape** (either way up; the default) or
**Landscape and portrait**, which turns the game with the device unless
Android's rotation lock is on. Portrait shows the game view across the width
with the touch controls below it. Portrait play is new and still being tested
on phones.

Select **Apply interface** to save. The game uses these sizes and the
orientation at its next launch or return from setup. Each Apply takes effect once: if you later change the
UI scale or text size inside the game, returning from setup keeps
those changes until you apply the Interface page again. **Discard changes**
keeps the previously applied settings.

## Leaving the game

Switching to another app pauses single player and saves it as **Suspended**
first. If Android closes openQ4 while it is in the background, **Continue** on
the title screen picks up from that save. The save is skipped during
cinematics, after death or a failed objective, and when storage is nearly full.
Turn it off with `com_suspendSave 0` in the console.

In multiplayer the server drops a player who is away for 40 seconds or more.
Returning after that reconnects to the same server straight away; after a
shorter break the game checks the connection and reconnects if the server no
longer answers. If the server has gone, you see the usual connection message.

## Return to setup during play

Open the game's **Settings**, then select **Android setup**. Select
**Return to game** at the top of setup to return to the running session.
Single player stays paused until you resume it. This also works before a
setup language has been selected.

Imports, file checks and exports remain unavailable while the game is running
or closing. Quit the game before using those operations.

## Back up saved games or export logs

1. Quit the game, then reopen **openQ4 setup**. File operations become available
   after the game has finished closing.
2. Under **Backups and diagnostics**, select **Back up saved games** or
   **Export logs**. These actions do not require a successful game-data check.
3. In Android’s file picker, choose a destination and save the proposed ZIP
   filename. Choose storage outside the app if the copy must survive removal
   of the app.
4. Wait for the export-complete message. If you selected a cloud provider,
   also wait for that provider’s upload to finish.

A save backup includes the files in each game’s `savegames` folder, including
descriptions and preview images. A log export includes each game’s `logs`
folder, including setup diagnostics. The ZIP preserves those game-relative
folders and contains `openq4-export.tsv`, an inventory with file sizes and
SHA-256 checksums. Retail data, extracted packages and settings are not part of
these exports. Logs can include local file paths and server addresses; review
them before sharing.

Export reads the originals and prepares a temporary ZIP before copying it to
the selected destination. Allow free app storage for approximately the total
size of the selected files plus archive overhead, as well as space at the
destination. The app reports the exact temporary-space requirement if needed.
Each export supports up to 2 GiB of source data and 4,096 entries below the
selected folders, with bounded folder depth and portable filenames.

If export is cancelled, interrupted or fails, the original saves and logs stay
in the app. The destination can contain an empty or incomplete ZIP: remove that
failed copy in Files, then retry with a new destination. After an app restart,
retry starts a fresh export and asks for a destination again.

This screen currently exports backups; it does not import backup ZIPs. Keep
the original installation and its app data until a restore path has been
verified for your intended device and game version.

## Update without removing saves

Install updates over the existing app using the same distributor and signing
identity. Uninstalling the app or clearing its app data removes its private
saves. Make an independent backup before changing installations. Physical
device, provider and release-upgrade qualification remains in progress.
