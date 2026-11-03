# Quake 4 and The Awakening campaigns

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

Open **Single Player → Campaign** to choose Quake 4 or The Awakening; the
campaign you are playing is chosen already, marked with an orange bar. Then
pick **New Game** to set the difficulty and Turbo Mode and start, or
**Chapters** to start from a level you have reached. Each opens its own page,
and Back climbs one level at a time: to Campaign, then Single Player, then the
main menu, where Continue and Load Game use that campaign's saves. Choosing a
campaign does not overwrite a saved game.

The classic menus (`ui_retained 0`) open a campaign's page as soon as you
choose it, with the difficulty, Turbo Mode, **Start Game** and **Chapters**
together on that page.

## Chapters

**Chapters** lists every level of the campaign in the order the campaign plays
it: 31 for Quake 4 (the storage and processing facilities count twice, once
per visit) and 13 for The Awakening. Each shows its loading-screen name and
levelshot.

- The first level is always open. Every other level unlocks once you reach it
  in the campaign. Starting a level records the unlock, and so does a level's
  start autosave already on disk, so progress made before this list existed
  counts.
- Unlocks stay when you delete saves. Each campaign keeps its own, in its
  configuration (`ui_chaptersQuake4` and `ui_chaptersAwakening`).
- A chapter starts from its level's start autosave, with the arsenal and
  difficulty you reached it with. If that autosave is gone, the level starts
  fresh at your current difficulty (in the classic menus, the one chosen on
  the campaign page), with the map's standard test arsenal where it has one,
  and writes a new start autosave.
- Levels you have not reached are listed but locked.

The Awakening option becomes available when openQ4 finds its campaign content.
An absent or partial installation leaves the option unavailable and explains
why. Both campaigns use openQ4's normal `baseoq4` SP module; the original leaked
DLLs and older openQ4 `q4xbase` modules are ignored.

## Install your content

Quake 4 still requires your legitimate retail `q4base` PK4s. Expansion assets
are not included in openQ4 and remain under their owners' terms.

Place the expansion's **`q4xbase` directory** alongside the retail installation's
`q4base` directory, keeping its subdirectories intact. Loose content and PK4s
are supported. Do not copy expansion files into `q4base` or `baseoq4`, because
they override many retail declarations, scripts and interface assets.

If you keep the expansion elsewhere, launch with an explicit content location:

```text
+set fs_awakeningpath "E:\Games\Quake_4_Alpha-main"
```

The directory can contain `q4xbase`, or be the `q4xbase` directory itself. The
setting is archived. Restart the game after changing it. Discovery checks the
defining scripts/declarations and all thirteen compiled campaign maps across
the configured content roots; a directory name or `mod.json` alone is insufficient.
`campaignList` reports missing required files when investigating a partial install.

The optional content location is used only by The Awakening. Merely installing
or discovering the expansion does not add its files to the active retail game.

## Saves and switching

Quake 4 keeps configuration, saves and caches under `fs_savepath/baseoq4`;
The Awakening uses `fs_savepath/q4xbase`. Supported openQ4 saves created before
the source consolidation retain their campaign namespace and serialization.
Keep your existing directories when upgrading, and replace the engine and both
`baseoq4` game modules together.

Changing campaigns restarts the filesystem so declaration/script overrides
cannot survive into the other campaign. Arena and multiplayer use `baseoq4`
and unload expansion content before they start. Awakening multiplayer changes
are excluded from this integration.

The expansion is unfinished alpha content. Finding the required files confirms
a structurally usable installation, not completed missions or restored missing
assets. Some authored script calls, imagery and unfinished features remain
content limitations, recorded in the [support audit](../dev/q4x-awakening-support-audit.md).
[The Awakening on openQ4](awakening.md) lists what openQ4 changes and fixes in
the campaign, and the alpha's known content issues.

## Console use

`campaignSelect quake4` and `campaignSelect awakening` open the corresponding
new-campaign menu after any required restart. Add `start` to begin the fixed
first map directly. `campaignSelect arena` opens the Arena browser on stock
content. These commands follow the same content/module rules as menu actions.
`campaignMenu campaigns` opens the campaign chooser. `campaignChapters` lists the
active campaign's chapters and which are reached; `campaignChapter <number>`
starts a reached one as the Chapters list does.

## Credits

Quake 4: The Awakening was developed by Raven Software and Ritual
Entertainment. Justin Marshall recovered and published the expansion. openQ4's
support for it is written independently and includes none of its content.
