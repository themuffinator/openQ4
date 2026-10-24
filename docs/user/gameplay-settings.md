# Gameplay Settings and Runtime Toggles

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

This guide covers a small set of gameplay and audio cvars that are useful for testing, accessibility, and personal preference.

The auto-skip cinematics, corpse cleanup, and corpse sink controls are also available in the in-game menu at `Settings -> Game Options`.

## Quick Reference

| Setting | Default | Scope | What it does |
|---|---:|---|---|
| `ui_weaponWheelOffset` | `27` | SP and MP client | Horizontal wheel offset as a percentage of screen width from center, −40 to +40. `0` centers it; `27` preserves the usual right-side position. Also in Game Options > Controller, beside wheel sensitivity. |
| `g_skill` | `1` | Single-player | Difficulty, `0` Private to `4` Warlord. A saved game brings back its own; change it mid-campaign with [Difficulty](#difficulty). |
| `g_autoSkipCinematics` | `0` | SP and MP game code | Automatically skips cinematics as soon as they begin. Disabled by default. |
| `g_corpseRemoveDelaySP` | `0` | Single-player | Controls how long SP corpses remain before disappearing. `0` uses stock timing, `-1` disables corpse removal. |
| `g_corpseRemoveDelayMP` | `0` | Multiplayer | Controls how long MP corpses remain before disappearing. `0` uses stock timing, `-1` disables corpse removal. |
| `g_corpseSink` | `0` | SP and MP game code | Selects corpse sink mode instead of the normal dissolve or burn-away behavior. |
| `g_classicDynamicLights` | `1` | SP and MP game code | Quake II/III style dynamic lights on muzzle flashes, bright projectiles and explosions. On by default. |
| `g_classicDynamicLightScale` | `1` | SP and MP game code | Radius multiplier for the classic dynamic lights, `0.25` to `4`. |
| `s_musicVolume` | `0.5` | Client audio | Controls music volume independently of the main sound mix. |
| `s_dialogueVolume` | `1` | Client audio | Spoken dialogue volume, from `0` (mute) to `1` (full), independently of effects and music. |
| `s_useEAXReverb` | `1` | Client audio | Quake 4's per-area room reverb, as the level designers set it up. See [Room Reverb](#room-reverb). |
| `s_useEAXOcclusion` | `1` | Client audio | Closed doors muffle the sounds behind them. Works while `s_useEAXReverb` is on. |
| `s_numberOfSpeakers` | `0` | Client audio | Speaker layout. `0` follows your output device, `2` forces stereo, `6` forces 5.1. See [Speaker Layout](#speaker-layout). |
| `hud_damageNumbers` | `0` | Multiplayer client | Floating damage numbers over the players you hit. `0` off, `1` opponents only, `2` all damage you deal. |
| `hud_damageNumberStyle` | `1` | Multiplayer client | How damage numbers are coloured. `1` white through red, `2` one colour per damage band, `3` one colour per weapon. |
| `hud_damageNumberScale` | `1` | Multiplayer client | Damage number size multiplier, `0.25` to `4`. |
| `hud_hitMarker` | `1` | SP and MP client | Crosshair hit marker on hits you land. On by default. |
| `hud_hitMarkerScale` | `1` | SP and MP client | Hit marker size multiplier, `0.25` to `4`, on top of `g_crosshairSize`. |
| `hud_crosshairHitFlash` | `0` | SP and MP client | Recolours the crosshair on a hit, as stock Quake 4 does. Implied when `hud_hitMarker` is off. |
| `g_hitFeedback` | `2` | Multiplayer server | Whether the server tells attackers about their hits at all. `0` none, `1` without the amount, `2` with the amount. |

## Cinematics

`g_autoSkipCinematics` is intended for repeat testing runs, speed-focused replays, and development workflows where you do not want to manually skip every scripted sequence.

The in-game menu exposes this as `Settings -> Game Options -> Auto Skip Cinematics`.

Behavior:
- `0`: normal behavior, cinematics play as authored.
- `1`: cinematics are skipped automatically when they start.

Notes:
- The cvar is archived, but it is disabled by default.
- The change affects future cinematics. It does not retroactively alter one that has already finished.

Example:

```cfg
seta g_autoSkipCinematics 1
```

## Turbo Mode

Choose **Turbo Mode** on the single-player New Game page. The choice applies when
you start the new game; selecting Off starts a normal campaign even after a turbo
game or a multiplayer session. Turbo increases movement speeds and removes weapon
reloads, including the shotgun pump after each shot. Shots still use ammunition.
Doors, platforms and elevators you operate move faster too. Machinery that the
level's own scripts choreograph, such as the lift your squad rides in Hangar
Perimeter, keeps its normal speed so those scenes stay in step.

The console setting is `g_turboMode` (`0` off, `1` on). Enabling it during a reload
cancels that reload; disabling it restores normal magazine and reload behavior.

## Difficulty

During a single-player game the in-game menu has a **Difficulty** entry, between **Settings** and **Return to Game**. It opens the difficulty list from New Game with the difficulty you are playing selected. Pick one and choose **Restart Level**: the level starts again from its beginning at that difficulty, with the weapons, ammo, health and armor you had when you entered it. **Back** changes nothing.

The level has to restart because a saved game keeps the difficulty it was saved at, and a level picks its enemies, their health and its pickups as it loads. Changing `g_skill` in the middle of a level only changes what the game reads as it goes, such as the damage you take.

| `g_skill` | Difficulty | Damage you take from enemies, and enemy health |
|---:|---|---|
| `0` | Private | -30% |
| `1` | Corporal | normal |
| `2` | Lieutenant | +40% |
| `3` | General | +80%, and no medkits |
| `4` | Warlord | +180%, and no medkits |

The console command `restartLevel` does the same: `restartLevel` alone restarts the current level at the current difficulty, and `restartLevel 3` restarts it on General. The loadout comes from the level's start autosave, the same save the death screen's **Restart** loads, and the restart writes a new one at the new difficulty. Anything done in the level since it started is lost.

## Corpse Cleanup

openQ4 now exposes separate corpse-removal timing controls for single-player and multiplayer.

### Single-player

`g_corpseRemoveDelaySP` accepts three useful ranges:
- `0`: use stock timing.
- `-1`: never remove corpses automatically.
- `> 0`: override the delay in seconds before corpse removal begins.

Example:

```cfg
seta g_corpseRemoveDelaySP 20
```

### Multiplayer

`g_corpseRemoveDelayMP` behaves the same way, but applies to the multiplayer game module.

Example:

```cfg
seta g_corpseRemoveDelayMP 10
```

Notes:
- These cvars affect corpse cleanup timing. They do not change health, gibbing, or damage behavior.
- New values are most useful for newly created corpses. Existing corpses may already be partway through their current cleanup path.

## Corpse Sinking

`g_corpseSink` switches corpse disappearance to a Quake 3 style sink animation.

Behavior:
- `0`: use the normal stock-style dissolve or burn-away path.
- `1`: sink corpses into the floor before removal while keeping ragdoll active.
- `2`: sink corpses into the floor before removal after stopping ragdoll first.

Notes:
- This cvar is shared by SP and MP.
- The configured SP or MP corpse delay still controls when the sink starts.
- If the relevant corpse-delay cvar is `-1`, corpse removal is disabled and sinking will not start.
- Mode `2` uses the actor's normal corpse physics while the sink runs, so the body no longer keeps simulating as a ragdoll during the sink.

Example:

```cfg
seta g_corpseSink 1
seta g_corpseRemoveDelaySP 15
```

No-ragdoll sink example:

```cfg
seta g_corpseSink 2
seta g_corpseRemoveDelaySP 15
```

## Music Volume

`s_musicVolume` controls the volume of music-tagged sound shaders without changing the rest of the game mix.

Behavior:
- `0`: mute music.
- `0.5`: default music level.
- `1`: full music level.

Notes:
- This is separate from the master sound volume.
- It applies to music shaders authored under the stock Quake 4 music paths, including both `sound/musical/` and `sound/ambience/musical/`.
- The setting is live and can be adjusted while the game is running.

Examples:

```cfg
seta s_musicVolume 0.2
```

```cfg
seta s_musicVolume 0
```

## Dialogue Volume

**Settings > Audio > Dialogue Volume** changes spoken dialogue without lowering
weapons or music. The setting takes effect immediately and is saved for both the
retail campaign and The Awakening. `seta s_dialogueVolume 0.6` sets it to 60% of
the authored level; `0` mutes dialogue. Master volume still affects everything.

NPC lip-sync speech, radio lines and sounds using the game's voice-over asset
paths share this control, including English audio fallback in other text
languages. Multiplayer microphone audio has its own settings. A mod that mixes
speech and music into one recording cannot separate those parts with this slider.

## Room Reverb

Every Quake 4 map tells the game which reverb each of its rooms, corridors and outdoor areas uses: a
tiled bathroom rings, a hangar booms, a canyon sounds open. openQ4 plays these the way the original
game did with EAX sound hardware:

- the reverb follows you from area to area, and the areas next to you keep their own reverb, so a
  gunshot in the hangar next door still sounds like the hangar;
- each nearby area's reverb comes from the doorway that leads to it;
- closed doors muffle the sounds behind them, and their reverb with them;
- radio chatter stays dry and voice-over sits a little further back, as in the original mix.

It needs OpenAL's EFX extension. OpenAL Soft provides it: openQ4 ships it on Windows and macOS, and
Linux distributions install it as their OpenAL. Apple's own OpenAL framework has no EFX, so a build
using it plays without reverb.

Behavior:
- `s_useEAXReverb 1` (default): room reverb on, in every performance preset. `0` turns it off, along with door muffling, as in
  the original game. Changing it from the console takes effect after `s_restart`.
- `s_useEAXOcclusion 1` (default): closed doors muffle sound while reverb is on.
- `s_muteEAXReverb 1`: silences the reverb of the area you are standing in, for comparison.

Notes:
- Maps without a reverb table, including The Awakening's, use the Generic reverb everywhere, as
  the original game does.
- `listReverbs` lists the reverbs, the current map's areas and the reverbs playing around you.
  `s_showReverb 1` prints a line whenever they change.

## Speaker Layout

openQ4 now follows your audio device by default. Headphones and stereo speakers get stereo, with
OpenAL Soft's 3D headphone audio (HRTF) switching on by itself when the system reports headphones,
and a 5.1 or 7.1 system gets all of its channels.

The in-game menu exposes this as `Settings -> Audio -> Surround Speakers`: `Auto`, `Off` (stereo)
or `On` (5.1).

Behavior:
- `s_numberOfSpeakers 0` (default, `Auto`): use the output device's own layout.
- `s_numberOfSpeakers 2` (`Off`): always stereo, as the original game's default.
- `s_numberOfSpeakers 6` (`On`): always 5.1. On a stereo device the system downmixes it, and
  headphone HRTF cannot engage, so pick this only for a 5.1 system that misreports its layout.

Notes:
- Earlier openQ4 builds asked for 5.1 on every device. A saved `6` from those builds moves to `0`
  once. A saved `2` is kept.
- `s_openALHRTF` chooses headphone HRTF: `0` automatic (default), `1` off, `2` on. Earlier builds
  could not turn it off on headphones; `1` now does.
- Unplugging a headset or switching the default device keeps the sound going, including music
  and looping ambience, and keeps your speaker and HRTF choices. Dialogue and other sounds that
  were playing pick up where the scene is when audio returns, instead of being cut off, and a
  line paused in a menu continues from where it stopped.
- Changing `s_numberOfSpeakers`, `s_openALHRTF` or `s_outputLimiter` applies to the open device
  in place: sounds keep playing without reloading, and a line that is playing moves to or from
  the centre speaker at its current point.

## Audio Quality

These are on by default and rarely need changing. Each takes effect after `s_restart`.

- `s_sourceRadiusScale 0.5`: a sound has a size, half of its shader's `minDistance`. Standing next
  to a machine or a fire, it surrounds you instead of jumping from ear to ear as you turn, and
  your own footsteps and weapons play evenly instead of from straight ahead. `0` makes every
  positioned sound a point.
- `s_directStereo 1`: music and other stereo sounds play straight to your speakers or headphones.
  With 3D headphone audio on, they used to pass through it as two virtual speakers, which
  narrowed the stereo and made them quieter.
- `s_resampler auto`: most Quake 4 sounds are recorded at 22 kHz; `auto` upsamples them with a
  sinc filter for cleaner high frequencies. `default` keeps OpenAL's own, and `listResamplers`
  lists the choices.
- `s_outputLimiter 1`: loud scenes compress instead of clipping.
- `s_airAbsorption 1`: distant sounds lose a little treble, by the air absorption each room's
  reverb preset sets.

## Hit Marker

Every hit you land blooms four short angled marks out of your crosshair and fades
them within a fraction of a second. It works in single-player and multiplayer, and
it is on by default: `Settings -> Game Options -> Crosshair -> Hit Marker`.

The marker reads the hit back to you rather than just confirming it:

- A graze or a splash hit is a small, quiet tick.
- A normal hit is the reference pulse.
- A heavy hit - a rail, a rocket, a direct dark matter hit - is wider, brighter,
  and lasts a beat longer.
- A hit that finishes the target flashes white hot and travels furthest.
- Armour absorbing part of the hit lifts the marks towards white.
- Team damage shows amber and self damage shows a muted grey-blue, so a mistake
  never reads like a reward.

Sustained fire re-arms the marker instead of stacking a second one, and a heavier
hit landing behind a lighter one keeps the heavier look for the rest of the pulse.

`hud_hitMarker 0` turns it off. `hud_hitMarkerScale` scales it between `0.25` and
`4`; it already follows `g_crosshairSize`, so the marks frame whatever crosshair
you use.

Stock Quake 4 recolours the crosshair red for a moment when you hit something. The
marker replaces that cue, so the recolour is off while the marker is on. Turning
the marker off brings the recolour back on its own, and
`hud_crosshairHitFlash 1` asks for both at once.

In multiplayer the marker sizes itself from the same server-permitted hit
information as damage numbers, so with `g_hitFeedback 1` or `0` you still get a
marker on every hit - just always the normal one, because the server is not
telling you what the hit was worth.

## Damage Numbers

In multiplayer, openQ4 can float the damage you deal above the player you hit, the
way Quake Live's damage plums do. The numbers are projected from the point of
impact and then drift along a short arc before fading, so they stay readable
during a fight without ever sitting on top of your crosshair.

This is a display choice and it is off by default.

`hud_damageNumbers` decides which of your hits are shown:
- `0`: off.
- `1`: opponents only. This is what Quake Live shows.
- `2`: everything you deal, adding team damage and self damage.

`hud_damageNumberStyle` decides how they are coloured:
- `1`: white for a scratch through red at 100 damage.
- `2`: one colour per damage band - blue up to 25, yellow to 50, orange to 75, red above.
- `3`: one colour per weapon.

`hud_damageNumberScale` scales the text between `0.25` and `4`.

The number is the full worth of the hit, so armour counts towards it rather than
being subtracted from it.

### Server permission

`g_hitFeedback` is a **server** setting and it decides whether attackers are told
about their hits at all:
- `0`: no hit feedback. Damage numbers show nothing regardless of client settings.
- `1`: the hit is announced but the amount is withheld, so damage numbers stay blank.
- `2`: the amount is included. This is the default.

A server that wants to keep exact damage off the table sets `g_hitFeedback 1` or
`0`; no client setting can override it.

Example:

```cfg
seta hud_damageNumbers 1
seta hud_damageNumberStyle 2
seta hud_damageNumberScale 1.25
```

## Classic Dynamic Lights

`g_classicDynamicLights` restores the Quake II / Quake III habit of lighting the world from the
action: muzzle flashes throw a short, wide pool of light, bright projectiles carry their own glow,
and explosions flash the surrounding geometry. It is on by default, and the colors come from the
shipped Quake 4 assets.

`g_classicDynamicLightScale` scales the radius of all of them between `0.25` and `4`. Lower it
before turning the feature off if the effect is stronger than you want.

See [Classic Dynamic Lights](classic-dynamic-lights.md) for the per-weapon colors, the content
override keys, and the `testClassicLight` console command.

## Example Presets

### Fast Testing Setup

```cfg
seta g_autoSkipCinematics 1
seta g_corpseRemoveDelaySP 2
seta g_corpseSink 1
seta s_musicVolume 0.2
```

### Keep Corpses Around

```cfg
seta g_corpseRemoveDelaySP -1
seta g_corpseRemoveDelayMP -1
```

## Troubleshooting

- If `g_autoSkipCinematics 1` appears to do nothing, verify you are testing a real in-game cinematic and not a normal scripted gameplay event.
- If corpses still disappear quickly, check whether you are in SP or MP and set the matching delay cvar for that game module.
- If `g_corpseSink 1` or `g_corpseSink 2` is enabled but corpses never sink, make sure the relevant corpse delay is not set to `-1`.
- If music does not respond to `s_musicVolume`, test on a map or menu that is actively playing music rather than general ambience or voice-over audio.
- If damage numbers never appear, check `g_hitFeedback` on the server: at `0` or `1` the amount never reaches you. They are also multiplayer only, and they only cover damage dealt to players.
- If damage numbers appear for opponents but not team mates, that is `hud_damageNumbers 1` working as intended. Set it to `2` to include team and self damage.

## Dialogue Subtitles

Enable **Dialogue Subtitles** in **Settings > Audio** (scroll down). Subtitles
work in both Classic and modern UI, including The Awakening, and remain visible
when dialogue or master audio is muted. Up to two speakers appear at once;
long lines advance in pages as their audio plays. Pause and save/load keep the
caption at the current point in the line.

| Setting | Default | Range | Effect |
| --- | --- | --- | --- |
| `ui_subtitles` | `0` | `0`–`1` | Enable dialogue subtitles |
| `ui_subtitleScale` | `1` | `1`–`2` | Text size; also respects the interface text scale |
| `ui_subtitleBackground` | `0.6` | `0`–`1` | Black backing opacity |
| `ui_subtitleSpeakers` | `1` | `0`–`1` | Show an available speaker name or the radio label |

All four settings are saved. Text comes from your installed game's lip-sync
declarations, with the normal language fallback. The Steam release installs
these transcripts in English only, and The Awakening's are written in English,
so other menu languages show English captions. Coverage is incomplete: a
missing transcript stays blank, and a scene description is never substituted.
Player voice chat and sound effects are not transcribed. Large captions can
overlap Awakening title cards in portrait windows.

## Gameplay Status Bar

Current source builds default to the modern UI (`ui_retained 1`). Select classic with
`seta ui_retained 0`, or modern with `seta ui_retained 1`.
Saved preferences from earlier versions are preserved. For released-package
defaults, see [Modern and Classic Screens](client-settings.md#modern-and-classic-screens).

The modern UI (`ui_retained 1`) shows a centered ammo, health and armor
bar, with Marine and Strogg artwork matching the campaign. Multiplayer shows
total ammo in the current weapon's color. Armor reaching zero leaves the other
gauges in place.

With the Remastered HUD layout (the default) the whole HUD follows your screen:
the status bar stays centered on the bottom edge, the radio sits in the top-right
corner, objective notices use the top corners, and multiplayer's kill feed,
standing, timer and chat keep to the edges. Vehicles get the same centered status
bar with an Exit prompt naming your jump key (the air-defence cannon keeps its
scope and adds the radio display), the objectives display lists every
objective with the completed ones below, and the scoreboard adds your place, ping
bars and the Tourney bracket. After you die in multiplayer, a Respawn prompt
names your attack key once you can return, with a countdown while overtime
delays your respawn.

**Settings > Game Options > HUD** sets the layout, size, width and safe area,
whether the objectives open while you hold the scores button or with one
press, the HUD's opacity, and whether the damage direction indicator, the
pickup messages and the weapon strip appear (these four apply to the Remastered
and Minimal layouts), and Reduced Flashing, which works in every layout. The
same settings are console variables, saved automatically:

| Setting | Default | Range | Effect |
| --- | --- | --- | --- |
| `ui_hudLayout` | `1` | `0`–`3` | `1` Remastered layout, `2` Minimal (health, armor, ammunition and the crosshair, keeping prompts, chat and votes), `3` Competitive (multiplayer: a match bar at the top with both sides' scores, the timer and network quality, your team's health and armor at the left edge and a larger status bar), `0` Classic (the original Quake 4 positions) |
| `ui_hudWidth` | `1` | `0`–`2` | How far edge HUD items spread on wide screens: `0` 16:9, `1` 21:9, `2` the full width |
| `ui_hudScale` | `1` (`1.25` on Steam Deck and with the [Couch profile](display-settings.md#television-viewing-couch-profile)) | `0.5`–`2` | HUD size, independent of menu size |
| `ui_hudSafeInset` | `0` | `0`–`10` | Distance inward, as a percentage of the viewport |
| `g_objectivesToggle` | `0` | `0`–`1` | Toggle Objectives: `0` shows the objectives while the scores button is held, `1` opens and closes them with each press (the Remastered display then shows a Close prompt) |
| `ui_hudOpacity` | `1` | `0.25`–`1` | HUD Opacity: fades the whole HUD |
| `ui_hudDamageDirection` | `1` | `0`–`1` | Damage Direction: `0` hides the indicator pointing toward damage |
| `ui_hudPickups` | `1` | `0`–`1` | Pickup Messages: `0` hides the item lines |
| `ui_hudWeaponStrip` | `1` | `0`–`1` | Weapon Strip: `0` keeps the strip of weapons hidden when you change weapon |
| `ui_reducedFlashing` | `0` | `0`–`1` | Reduced Flashing: `1` keeps the damage direction indicator and the vehicle hit glow at 40% at most, dims the white flash of teleporters and multiplayer spawns, and drops the red flash when you die |

For example, `ui_hudScale 1.25` enlarges the HUD by 25%. On narrow screens the
status bar's size is limited to keep every gauge visible. `ui_retained 0`
restores the stock interface; the stock HUD also appears automatically if a new
one cannot load.
