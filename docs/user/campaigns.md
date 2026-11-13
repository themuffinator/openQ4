# Quake 4 and The Awakening campaigns

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

openQ4 plays two story campaigns: the original Quake 4 campaign and **The Awakening**, the recovered Quake 4 expansion. Both run on openQ4's own game code and keep their own saves. This guide covers choosing a campaign, installing The Awakening's files, saves and known issues. For the bot ladder on the multiplayer maps, see [Arena Campaign](arena-campaign.md); for what openQ4 changes inside the expansion, see [The Awakening on openQ4](awakening.md).

## Choose a campaign

1. Open **Single Player → Campaign**. The campaign you are playing is already selected, marked with an orange bar.
2. Choose **Quake 4** or **The Awakening**.
3. Pick **New Game** to set the difficulty and Turbo Mode and start from the first level, or **Chapters** to start from any level you have reached.

Each choice opens its own page, and **Back** climbs one level at a time: to Campaign, then Single Player, then the main menu, where **Continue** and **Load Game** use the selected campaign's saves. Choosing a campaign never overwrites a saved game.

The classic menus (`ui_retained 0`) open a campaign's page as soon as you choose it, with the difficulty, Turbo Mode, **Start Game** and **Chapters** together on one page.

## Chapters

**Chapters** lists every level of the campaign in the order the campaign plays it: 31 for Quake 4 (the storage and processing facilities count twice, once per visit) and 13 for The Awakening. Each shows its loading-screen name and picture.

- The first level is always open. Every other level unlocks once you reach it in the campaign. A level's start autosave already on disk also counts, so progress from before this list existed is kept.
- Unlocks stay when you delete saves, and each campaign keeps its own.
- A chapter starts from its level's start autosave, with the weapons and difficulty you reached it with. If that autosave is gone, the level starts fresh at your current difficulty (in the classic menus, the one chosen on the campaign page), with the level's standard starting weapons where it has them, and writes a new start autosave.
- Levels you have not reached are listed but locked.

## Install The Awakening

openQ4 includes none of The Awakening's files: you supply your own copy of the expansion's content, which remains under its owners' terms. You also still need your own copy of Quake 4 (see [Getting started](getting-started.md)).

1. Put the expansion's **`q4xbase` folder** next to Quake 4's `q4base` folder, keeping its subfolders intact. Loose files and `.pk4` packs (the game's archive files) both work.
2. Start openQ4 and open **Single Player → Campaign**. **The Awakening** becomes selectable once openQ4 finds the content.

Never copy the expansion's files into `q4base` or `baseoq4`: they replace many of Quake 4's own definitions, scripts and menus.

**Keeping the expansion somewhere else.** Point openQ4 at it with a launch option:

```text
+set fs_awakeningpath "D:\Games\Quake 4 Awakening"
```

The folder can contain `q4xbase`, or be the `q4xbase` folder itself. openQ4 remembers the setting; restart the game after changing it. This location is used only for The Awakening: having the expansion installed never changes the Quake 4 campaign.

**If The Awakening stays unavailable,** the Campaign page says whether the content is missing or incomplete. openQ4 enables the campaign only when it finds the expansion's scripts and definitions and all thirteen campaign maps; a folder named `q4xbase` or a `mod.json` file on its own is not enough. Type `campaignList` in the console to see which required files are missing.

openQ4 never loads the expansion's original game DLL, or game modules that older openQ4 versions left in `q4xbase`; both campaigns use openQ4's own single-player module.

## Saves and switching

- Each campaign keeps its settings, saves and caches in its own folder inside openQ4's save folder (`fs_savepath`): `baseoq4` for Quake 4 and `q4xbase` for The Awakening.
- Switching campaigns reloads the game's files, so nothing from The Awakening carries over into Quake 4, Arena or multiplayer. Arena and multiplayer always run on stock Quake 4 content; the expansion's multiplayer changes are not supported.
- When you upgrade openQ4, keep your existing save folders. Saves from older openQ4 versions that this version can load stay with their own campaign. Update openQ4 as a whole package, never just the executable or one game module.

## Known issues

The Awakening is built from the expansion's unfinished alpha. All 13 maps load and play, but complete start-to-finish playthroughs are still being tested. openQ4 finding the files means your installation is complete, not that the alpha's unfinished missions or missing assets are restored: some scripted events, pictures and features were never finished in the content itself. [The Awakening on openQ4](awakening.md) lists what openQ4 fixes and the alpha's [known issues](awakening.md#known-issues-in-the-alpha).

## Mods and The Awakening

> **Not in 0.14.1.** It arrives in the next release. In 0.14.1, choosing a campaign unloads any mod you have loaded, and mods cannot play over The Awakening.

A mod can play over The Awakening as well as over Quake 4. Its files come first, then the expansion's, then openQ4's and Quake 4's, so a mod can replace anything the expansion supplies.

- **From the menus.** Load your mod from **Mods**, then choose **The Awakening** under **Single Player → Campaign**. The mod stays loaded. Choosing **Quake 4** plays the same mod over Quake 4 again, and a mod you load while The Awakening is running plays over The Awakening.
- **From a shortcut or batch file:**

  ```bat
  openQ4-client_x64.exe +set fs_game mymod +set fs_game_base q4xbase +campaignSelect awakening start
  ```

  Leave out `+campaignSelect awakening start` to start at the menu instead of the first level. Add `+set fs_awakeningpath "D:\Games\Quake 4 Awakening"` if the expansion is not next to `q4base`.

- **A mod made for one campaign** can say so with `"campaign"` in its `mod.json`. Players then need only `+set fs_game mymod`:

  ```json
  {
    "name": "My Awakening Mod",
    "version": "1.0",
    "releaseDate": "2026-11-13",
    "website": "https://example.com",
    "author": "You",
    "requiredopenQ4Version": "0.14.1",
    "campaign": "awakening"
  }
  ```

  `"campaign": "quake4"` keeps a mod off The Awakening instead. Without the key, a mod plays over whichever campaign the player picks, and choosing the other campaign in the menu plays it without a mod tied to one campaign. The value must be `awakening` or `quake4`; anything else makes openQ4 ignore the mod and write the reason to the log.

  openQ4 0.14.1 does not know the `campaign` key: it ignores it and runs the mod over Quake 4. If your mod cannot work without the expansion, raise `requiredopenQ4Version` to the first release that reads the key once that release is out, so older versions refuse the mod instead of running it over the wrong campaign.

A mod keeps its settings, saves and demos in its own folder in `fs_savepath` for both campaigns. **Load Game** lists only the saves of the campaign you are playing, and a demo plays only in the campaign it was recorded in. Multiplayer never uses The Awakening: hosting or joining a game removes the expansion and keeps your mod. A mod that ships its own game module replaces openQ4's, including openQ4's Awakening support; content-only mods use openQ4's modules. The expansion's own menus are never used, with or without a mod, but a mod's menus are.

The console variable `fs_awakeningActive` reads `1` while the expansion is loaded, and the log names the game folders it was loaded with.

## Console commands and settings

These console commands follow the same rules as the menus, including the reload when you switch campaigns.

| Command or setting | What it does |
|---|---|
| `campaignSelect quake4`, `campaignSelect awakening` | Opens that campaign's **New Game** page (its campaign page in the classic menus). Add `start` to begin the first level at once, or `chapters` to open its **Chapters** page. |
| `campaignSelect arena` | Opens the Arena browser. |
| `campaignMenu campaigns` | Opens the campaign chooser. `campaignMenu` on its own opens **Single Player**. |
| `campaignChapters` | Lists the current campaign's chapters and which you have reached. |
| `campaignChapter <number>` | Starts a reached chapter, as the **Chapters** list does. |
| `campaignList` | Reports whether The Awakening's content is present and complete, and lists any missing files. |
| `fs_awakeningpath` | Where The Awakening's content lives, if not next to `q4base`. Saved; takes effect after a restart. |
| `ui_chaptersQuake4`, `ui_chaptersAwakening` | Where each campaign's chapter unlocks are saved. You never need to set them by hand. |
| `fs_awakeningActive` | **Not in 0.14.1:** read-only; `1` while The Awakening's files are loaded. |

## Credits

Quake 4: The Awakening was developed by Raven Software and Ritual Entertainment. Justin Marshall recovered and published the expansion. openQ4's support for it is written independently and includes none of its content.
