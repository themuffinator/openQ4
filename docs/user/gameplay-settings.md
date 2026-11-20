# Gameplay settings

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

This guide covers how the game plays, looks and sounds: difficulty and Turbo Mode, the HUD and crosshair, multiplayer hit feedback, field of view, subtitles and sound, and a few convenience options. For keys, mouse and controllers, see [Input settings](input-settings.md).

## Where to find these settings

| You want to change | New interface | Classic menus (`ui_retained 0`) |
|---|---|---|
| Difficulty, Turbo Mode | The **New Game** page; during a campaign, the in-game menu's **Restart Level** | The **New Game** page; during a campaign, the in-game menu's **Difficulty** |
| HUD layout, size and opacity | **Settings → Game Options → HUD** | Game Options, **HUD** section |
| Crosshair, hit marker, animated crosshair | **Settings → Game Options → HUD** | Game Options, **Crosshair** section (no animated crosshair) |
| Auto weapon switch, skip cinematics, corpses, enhanced physics, language, console access, pickup and opponent styles | **Settings → Game Options → General** | Game Options, **Gameplay** section |
| Weapon field of view and position, Show Gun, Gun Position | **Settings → Game Options → View Weapon** (**General** in 0.14.1) | Game Options, **View Weapon** section |
| View kick, screen and cinematic shake, reduced flashing, opaque menu backing | **Settings → Game Options → Accessibility** (**General** and **HUD** in 0.14.1) | Game Options, **Gameplay** and **HUD** sections |
| Volume, room reverb, speakers, subtitles | **Settings → Audio** | **Settings → Audio** |

The classic Game Options page has a **Section** chooser at the top that jumps between its groups. Settings you change are saved automatically.

## Difficulty

Pick a difficulty on the **New Game** page when you start a campaign. To change it during a campaign, open the in-game menu and choose **Restart Level** (in the classic menus, **Difficulty**, between **Settings** and **Return to Game**). The difficulty list opens with the difficulty you are playing selected. Pick one and choose **Restart Level**: the level starts again from its beginning at that difficulty, with the weapons, ammunition, health and armour you had when you entered it. **Back** changes nothing.

The level has to restart because a level sets up its enemies, their health and its pickups as it loads, and a saved game keeps the difficulty it was saved at. Changing `g_skill` from the console in the middle of a level only changes what the game reads as it goes, such as the damage you take.

| `g_skill` | Difficulty | Damage you take from enemies, and enemy health |
|---:|---|---|
| `0` | Private | -30% |
| `1` | Corporal (default) | normal |
| `2` | Lieutenant | +40% |
| `3` | General | +80%, and no medkits |
| `4` | Warlord | +180%, and no medkits |

The console command `restartLevel` does the same job: `restartLevel` alone restarts the current level at the current difficulty, and `restartLevel 3` restarts it on General. Your loadout comes from the level's start autosave (the same save the death screen's **Restart** loads), and the restart writes a new one at the new difficulty. Anything you did in the level since it started is lost.

## Turbo mode

Choose **Turbo Mode** on the single-player **New Game** page. It applies when you start the new game; choosing Off starts a normal campaign, even after a turbo game or a multiplayer session.

Turbo makes you move faster and removes weapon reloads, including the shotgun pump after each shot. Shots still use ammunition. Doors, platforms and elevators you operate move faster too. Machinery that a level's own scripts choreograph, such as the lift your squad rides in Hangar Perimeter, keeps its normal speed so those scenes stay in step.

The console setting is `g_turboMode` (`0` off, `1` on). Turning it on during a reload cancels that reload; turning it off restores normal magazines and reloads.

## HUD and crosshair

### Gameplay status bar

The new interface (the default, `ui_retained 1`) draws a centred bar for ammunition, health and armour, with Marine or Strogg artwork to match the campaign. In multiplayer it shows the current weapon's total ammunition in that weapon's colour. When your armour runs out, the other gauges stay where they are.

Choose a layout with **Settings → Game Options → HUD → HUD Layout**:

- **Remastered** (default): the HUD follows your screen. The status bar sits centred on the bottom edge, the radio in the top-right corner and objective notices in the top corners; multiplayer's kill feed, standing, timer and chat keep to the edges.
- **Minimal**: health, armour, ammunition and the crosshair, keeping prompts, chat and votes.
- **Competitive** (multiplayer): a match bar at the top with both sides' scores, the timer and network quality, your team's health and armour at the left edge, and a larger status bar.
- **Classic**: the original Quake 4 positions.

In the Remastered layout, vehicles get the same centred status bar with an Exit prompt naming your jump key (the air-defence cannon keeps its scope and adds the radio display). The objectives display lists every objective, with the completed ones below. The scoreboard adds your place, ping bars and the Tourney bracket. After you die in multiplayer, a Respawn prompt names your attack key once you can return, with a countdown while overtime delays your respawn.

The same page also sets:

- **HUD Size**, independent of the menu size. On narrow screens the status bar is kept small enough for every gauge to fit.
- **HUD Width**: how far edge items spread on wide screens (16:9, 21:9 or Full).
- **Safe Area Inset**: moves the HUD in from the screen edges. On a screen with a camera cutout or system bars, the HUD also keeps clear of them.
- **Toggle Objectives**: Off shows the objectives while you hold the scores button; On opens and closes them with each press.
- **HUD Opacity**, **Damage Direction**, **Pickup Messages** and **Weapon Strip**. These four work in the Remastered and Minimal layouts.
- **Reduced Flashing** (on the **Accessibility** tab; **HUD** in 0.14.1), which works in every layout. It keeps the damage direction indicator and the vehicle hit glow at 40% at most, dims the white flash of teleporters and multiplayer spawns, and drops the red flash when you die.
- **Opaque Menu Backing** (also on **Accessibility**): draws a solid backing behind menu dialogs instead of softening the screen beneath.

The classic interface (`ui_retained 0`) uses the same layout with its original artwork. The status bar is centred on the bottom edge, and the weapon strip shows the weapon's name above it. Up to two pickup lines sit over the gauges, and a repeated item counts up on its line ("Armor Shard ×2"). Vehicles get the centred status bar and an Exit prompt in the bottom-right corner. Multiplayer shows your standing and the gap, or the team panels with your team first, the flags and their carriers, with the timer beneath. Tourney shows its bracket strip with your match highlighted, the kill feed sits in the top-right corner, and chat sits at the bottom left, clear of the status bar. Spectators get a card naming the player they follow; a vote shows as a card at the left edge with the Yes and No keys, the tally and the time left; and while you wait to respawn, a prompt names the control. Layout, HUD Size, HUD Opacity, HUD Width, Safe Area Inset, Pickup Messages, Weapon Strip and Damage Direction all work there; Minimal hides the same extras, and Competitive uses the Remastered positions.

If the new HUD cannot load, the classic HUD appears automatically.

### Crosshair

**Settings → Game Options → HUD** has the crosshair controls:

- **Crosshair**: **Weapon Default** uses each weapon's own crosshair; **Custom** uses the one you pick with **Set Crosshair** on every weapon.
- **Crosshair Size**: Small to Extra Large (`g_crosshairSize` `16` to `48`, default `32`).
- **Crosshair Color**: eight colours, white by default (`g_crosshairColor`).
- **Hit Marker** and **Animated Crosshair**, described next.

### Hit marker

Turn on **Settings → Game Options → HUD → Hit Marker** (`hud_hitMarker 1`) to have every hit you land bloom four short angled marks out of your crosshair and fade them within a fraction of a second. It works in single-player and multiplayer. It is off by default, so you get stock Quake 4's cue instead: the crosshair turns red for a moment when you hit something.

The marker tells you what kind of hit it was:

- A graze or splash hit is a small, quiet tick.
- A normal hit is the reference pulse.
- A heavy hit (a rail, a rocket, a direct dark matter hit) is wider, brighter and lasts a beat longer.
- A hit that finishes the target flashes white-hot and travels furthest.
- Armour absorbing part of the hit lifts the marks towards white.
- Team damage shows amber and self damage a muted grey-blue, so a mistake never looks like a reward.

Sustained fire re-arms the marker instead of stacking a second one, and a heavier hit landing behind a lighter one keeps the heavier look for the rest of the pulse.

The marker follows your crosshair size, and `hud_hitMarkerScale` scales it further. While the marker is on, the red crosshair flash is off; `hud_crosshairHitFlash 1` asks for both.

In multiplayer the marker uses the same hit information the server allows for damage numbers. On a server that withholds the damage amount (`g_hitFeedback 1` or `0`), you still get a marker on every hit, but always the normal one.

### Animated crosshair

**Settings → Game Options → HUD → Animated Crosshair** (`hud_crosshairAnimated`, on by default) draws each weapon's own Quake 4 crosshair live instead of as a still picture. The designs are the originals, split into the arcs, ticks, bars, darts and brackets they were drawn from, and those pieces follow the weapon, so the crosshair tells you what the gun is doing. Turn it off for the still picture.

Every design:

- **Assembles as the weapon comes up**, so it is whole by the time you can fire. It folds away while a weapon is put down and while the use hand, the talk frame or another context cursor shows. A dot always marks the aim point while it is folded away.
- **Kicks with each shot** and settles before the next one.
- **Stands down while you can't fire.** Reloading dims the design and shows the reload: one pip per round for weapons loaded a round at a time, a sweep otherwise. Out of ammunition, a design with a cross turns it into an X.
- **Takes the hit.** A hit squeezes the design for the length of the pulse, and a kill also sends a thin ring out from its edge.

Each design also has its own behaviour:

| Crosshair | What it shows |
| --- | --- |
| Blaster | The four arcs wind shut into a ring as the blaster charges, and a thin outer ring breathes while a full charge is held. |
| Hyperblaster | The arcs turn with the barrels for as long as they spin. |
| Machinegun | The ticks walk out under sustained fire and back as it stops, and draw in to the ring while you aim down the sights. |
| Shotgun | The ring is thrown wide by the shot and sends a faint echo out to the edge of the pattern; reloading counts the shells back in. |
| Grenade launcher | The quarters break open on the shot, like a breech, and stand apart until the next round is ready. |
| Dark matter gun | The quarters turn and draw tight as the core charges, then spring back to rest when it is ready. |
| Nailgun | The brackets draw together as the drum spins up, and clamp shut with four pips between them on a seeker lock. |
| Rocket launcher | The faint halo sweeps round as the launcher cycles and closes when the next rocket is ready. |
| Railgun | The shot throws the four bars out and the recharge draws them back; they are home, with a brief flash, when the next slug is. |
| Lightning gun | The beam draws the four darts in and sets them crackling. They are the hit marker: every hit drives them home in the hit's colour. |
| Napalm launcher | The shot throws the splatter ring wide and a quarter turn round; it draws back in as the next charge comes up. |
| Gauntlet | Three teeth turn round the disc while the blade spins. |

**Hits.** With the [hit marker](#hit-marker) on, its marks dock into the clear space of the design in hand, and the design keeps your crosshair colour with only a light cast of the hit's colour. With the marker off, the design takes the whole tint instead. A hit that lands while the design is folded away still draws its marks at full strength round the aim point.

**Vehicles** get the same treatment for your seat's gun. A cannon that recharges between shots shows the recharge as a sweep, a gun with a target closes four corners on its crosshair, and hits you land from a vehicle show on its crosshair too.

Good to know:

- It needs the new interface. The classic interface keeps the still crosshair.
- It follows your crosshair size and colour. Black outlines stay black at every colour, as in the original art.
- With **Custom** chosen, that design is the one animated on every weapon. The ten simple marks of the custom set animate as one piece.
- **Reduced Flashing** softens the hit tint and drops the flashes. Reduced motion (`ui_retainedReducedMotion 1`) fades the design in place instead of flying it in, and stops the turning and trembling.
- The Awakening's space cannon keeps its own lock-on reticule, which already changes with the lock.

### Multiplayer objective markers

Small markers show where the objectives are in CTF and One Flag CTF (including the Arena variants), control points and DeadZone zones, with the straight-line distance in metres as the only text. They are on by default; `hud_objectiveMarkers 0` hides them.

- **Flags:** a solid pennant means the flag is home, a hollow pennant with an upward cue means it is carried, and a lowered pennant with a downward cue means it is dropped. The marker stays at the base.
- **Zones:** an empty core means unclaimed, a filled core in a team's colour means controlled, and two opposing halves mean contested. A small padlock means you need a DeadZone artifact to score.

Markers show through walls, and arrows point to objectives outside your view. They dim near the crosshair and hide while you are dead, zoomed, in a menu or looking at the scoreboard. Spectators following a player see that player's markers; free-camera spectators see none. HUD Size, Safe Area Inset and HUD Opacity apply to them, and they work with both interfaces. In VR they follow your head on the HUD; see [VR display settings](vr.md#display).

### Damage numbers (multiplayer)

In multiplayer, openQ4 can float the damage you deal above the player you hit, like Quake Live's damage numbers. Each number starts at the point of impact and drifts along a short arc before fading, so it never sits on your crosshair. The number is the full worth of the hit: armour counts towards it rather than being taken off.

There is no menu row for these; use the console:

- `hud_damageNumbers`: `0` off, `1` opponents only (the default, as in Quake Live), `2` everything you deal, including team and self damage.
- `hud_damageNumberStyle`: `1` white for a scratch through to red at 100 damage, `2` one colour per damage band (blue up to 25, yellow to 50, orange to 75, red above), `3` one colour per weapon.
- `hud_damageNumberScale`: text size, `0.25` to `4`.

The server decides whether you get any of this. Its `g_hitFeedback` setting is `2` (send the amount) by default; at `1` the hit is announced but the amount is withheld, so numbers stay blank, and at `0` there is no hit feedback at all. No client setting can override it.

```cfg
seta hud_damageNumbers 1
seta hud_damageNumberStyle 2
seta hud_damageNumberScale 1.25
```

## Field of view

There is no field-of-view slider in the menus; set it from the console with `g_fov` (default `90`), for example `g_fov 100`. The number is the horizontal angle on a 4:3 screen. On a wider screen openQ4 keeps the same vertical view and shows more at the sides, so you don't need to raise it for widescreen. Multiplayer accepts `90` to `175`. To make sure your value is applied every time, put `set g_fov 100` in your `autoexec.cfg` (see [Input settings](input-settings.md#config-files)).

The weapon on screen has its own controls on **Settings → Game Options → View Weapon** (**General** in 0.14.1): **Weapon FOV** (`0` follows your field of view), **Weapon FOV Aspect**, and **Weapon X/Y/Z Offset**, described in [Display settings](display-settings.md). **Weapon Kick** (`g_weaponMuzzleKick`) makes the weapon kick back when it fires; turn it off to hold the weapon still. To stop the camera itself jerking when you fire, see [Camera kick and screen shake](#camera-kick-and-screen-shake).

## Dialogue subtitles

Turn on **Settings → Audio → Dialogue Subtitles**, under **Subtitles**. Subtitles work in both interfaces, including The Awakening, and stay visible when dialogue or master volume is muted. Up to two speakers appear at once, and long lines advance in pages as the audio plays. When a line finishes, its last words stay up for half a second more, and a short line stays up for at least a second and a half, so you have time to read it; a line that is cut off clears at once. Pausing, saving and loading keep the caption at the current point in the line. **Subtitle Size**, **Subtitle Background**, **Speaker Names** and **Untranscribed Speech** sit beside it. **Speaker Names** puts the character's name before their line, for example *Medic Anderson: There. He's stabilized for the moment.*, and **Radio** before a radio line whose speaker is unknown. **Not in 0.14.1:** in 0.14.1 Speaker Names showed only the radio label, never a character's name. The emotion cues the game's dialogue text carries for its facial animation show as stage directions, for example *(Furrow) Let's not lose focus here.* **Not in 0.14.1:** 0.14.1 shows them raw, as `{furrow}`.

The text comes from your installed game. Most lines use its lip-sync transcripts. Radio chatter and the other lines that have none show the line as written in the game's script, which its sound files carry; now and then the actor says a shorter or slightly different take. A marine heard over the helmet radio shows the same text as when you hear them face to face. The installed text is English only (the Steam release installs English, and The Awakening's is written in English), so other menu languages show English captions. Screams, laughter, pain sounds and multiplayer announcers have no words to show. With **Untranscribed Speech** on (the default) such a sound, or any other speech without text, shows **[untranscribed speech]**, after the speaker's name when it is known, so you can tell someone is making it; where that does not fit on one line, as in a narrow window at large text sizes, it shortens to **[speech]**. Turn it off to leave those blank. Stage directions in the script, such as *(pain grunt)*, are never shown as captions. Voice chat and sound effects are not transcribed. In portrait windows, cinematic captions sit below the picture, clear of title cards; at the largest text sizes they show one line at a time there, with a second speaker above the picture. A speaker name too wide for half the line is left out.

### Caption packs

A caption pack is a mod that replaces a line's text, for example with a translation or an exact transcript where the script line differs from what is said, or adds text for a line that has none. openQ4 does not ship one and copies no transcripts: its captions come from the game you installed. Install a pack like any other mod, as a PK4 in your game folder or a mod folder. A pack's text takes priority over the installed text for the same line; an entry with no text, or a missing `#str_` key, leaves the installed text in place.

For pack authors: a pack is `lipsync/<name>.lipsync` with one entry per line, named `subtitle/` plus either the sound shader's name or the voice file's path without its extension:

```
lipSync subtitle/vo_1_1_1_140_1 { text "#str_9100001" }
lipSync subtitle/sound/vo/airdefense/1_1_1_150_2 { text "Your own words for this line." }
```

Use the shader name for a sound that has one voice file. A shader that picks one of several files at random needs one entry per file path, because each file is a different line. The text can be literal or a `#str_` key; keys come from the pack's own `strings/<language>_<name>.lang` files, so one pack can carry every language and falls back to English where a language has no entry. These entries are only captions: they do not change lip sync. Publish only text you have the right to publish.

To list the lines without a lip-sync transcript of their own, run `python tools/tests/audio_asset_inventory.py --assets <game folder> --output report.json --pack-template mypack.lipsync` from an openQ4 source checkout. It writes an empty entry for every such line, about 1,400 for Quake 4 and 70 for The Awakening, with no transcript text. Most of them already show a script line or a helmet copy's transcript, which an entry replaces. In the console, `listSubtitles` shows each caption's source as `installed`, `script` (the line as written in the game's script), `authored` (from a pack) or `none`.

## Sound

### Music and dialogue volume

**Settings → Audio** has **Volume** (everything), **Music Volume** and **Dialogue Volume**. Changes apply at once.

- **Music Volume** (`s_musicVolume`, default `0.5`) changes the music without touching the rest of the mix.
- **Dialogue Volume** (`s_dialogueVolume`, default `1`) changes spoken dialogue without lowering weapons or music. It covers character speech, radio lines and other voice-over, including the English audio you hear in other text languages, and it is saved for both the original campaign and The Awakening. Multiplayer voice chat has its own settings. A mod that mixes speech and music into one recording cannot separate them with this slider.

### Room reverb

Every Quake 4 map tells the game which reverb each of its rooms, corridors and outdoor areas uses: a tiled bathroom rings, a hangar booms, a canyon sounds open. openQ4 plays these the way the original game did with EAX sound hardware:

- the reverb follows you from area to area, and nearby areas keep their own, so a gunshot in the hangar next door still sounds like the hangar;
- each nearby area's reverb comes from the doorway that leads to it;
- closed doors muffle the sounds behind them, and their reverb with them;
- radio chatter stays dry and voice-over sits a little further back, as in the original mix.

Room reverb is on in every performance preset. Turn it off with **Settings → Audio → EAX Advanced HD**, which also turns off door muffling, as in the original game. From the console the setting is `s_useEAXReverb`; a console change takes effect after `s_restart`.

Maps without a reverb table, including The Awakening's, use a generic reverb everywhere, as the original game does.

Reverb needs OpenAL Soft, the sound library openQ4 ships on Windows and macOS and that Linux distributions install as their OpenAL. A build that uses Apple's own OpenAL plays without reverb.

### Speakers and headphones

openQ4 follows your audio device by default: headphones and stereo speakers get stereo, and a 5.1 or 7.1 system gets all of its channels. When the system reports headphones, 3D headphone audio (HRTF, which makes sounds seem to come from around you) switches on by itself.

- **Settings → Audio → Surround Speakers**: **Auto** (default) uses the device's own layout, **Off** always plays stereo, as the original game did by default, and **On** always plays 5.1. On a stereo device 5.1 is downmixed and headphone audio cannot engage, so choose **On** only for a 5.1 system that reports the wrong layout.
- **Settings → Audio → Headphone HRTF**: **Auto** (default), **Off** or **On**.

These changes apply straight away: sounds keep playing, and a line that is playing moves to or from the centre speaker where it is. Unplugging a headset or switching the default device keeps the sound going, including music and looping ambience, and keeps your choices. Dialogue that was playing picks up where the scene is when audio returns, and a line paused in a menu continues from where it stopped.

### Audio quality

These are on by default and rarely need changing. Each takes effect after `s_restart`.

- `s_sourceRadiusScale 0.5`: gives each sound a size, so standing next to a machine or a fire surrounds you instead of jumping from ear to ear as you turn, and your own footsteps and weapons play evenly. `0` makes every positioned sound a point.
- `s_directStereo 1`: music and other stereo sounds play straight to your speakers or headphones instead of through 3D headphone audio, which narrowed and quietened them.
- `s_resampler auto`: most Quake 4 sounds are recorded at 22 kHz; `auto` upsamples them with a high-quality filter for cleaner high frequencies. `default` keeps OpenAL's own, and `listResamplers` lists the choices.
- `s_outputLimiter 1`: loud scenes compress instead of clipping.
- `s_airAbsorption 1`: distant sounds lose a little treble, as each room's reverb sets.

## Other options

### Cinematics

**Settings → Game Options → General → Auto Skip Cinematics** (`g_autoSkipCinematics 1`) skips every cinematic as soon as it starts, which is handy for replays and speed runs. It is off by default and affects the next cinematic, not one already playing. **Cinematic Shake** (**Settings → Game Options → Accessibility**; **General** in 0.14.1) scales the extra camera movement added to cinematics (`ui_cinematicShake`, `0` none to `1` full); VR always turns it off.

### Camera kick and screen shake

**Settings → Game Options → Accessibility → View Kick** (**General** in 0.14.1; `g_viewKick`) sets how much the camera jerks when you fire: the kick every single-player weapon gives the view, the lightning gun's shake while it fires, and the kick of vehicle guns. `0` keeps the view still and `1` (the default) is the original kick; the new interface's slider goes in tenths, the classic page offers Off, Low and Full. Your weapons behave exactly as before: shots still go where the crosshair points, and spread, damage and rate of fire do not change. **Weapon Kick** moves only the gun on screen, so turn both off for a completely steady view. Being hit still jolts the view.

**Screen Shake** (`g_screenShake`) on the same page sets the shake from explosions, including your own rockets and grenades, from collapsing machinery and loud sounds, and from riding The Awakening's speeder bike, `0` none to `1` full. Both apply at once. Multiplayer weapons and explosions never kick or shake the view, so these settings change nothing there.

### Weapon switching

**Not in 0.14.1.** **Settings → Game Options → General → Auto Weapon Switch** (`ui_autoSwitch`) decides what happens when you pick up a weapon:

- **Smart** (`2`, the default) switches only when it helps. In multiplayer a pickup takes over from the gauntlet, from the machinegun you spawn with, from a weapon that needs no ammunition such as the Classic ruleset's blaster, or from a weapon that has run dry, and never pulls a real weapon out of your hands mid-fight: running over a shotgun with the railgun up keeps the railgun. In single player you switch to a weapon the first time you get it, as Quake 4 always did.
- **Always** (`1`) switches to every weapon you pick up in multiplayer, and to every new weapon in single player: Quake 4's original behaviour.
- **Off** (`0`) never switches; a weapon handed to you by a character in the campaign is still raised.

Profiles from earlier versions that still had Quake 4's default (`1`) move to Smart once; `0` stays off. If you also set a [weapon preference order](multiplayer-client-options.md#weapon-preference-order), a switch Smart allows still has to go up your order. Buying always switches to what you bought.

**Last weapon** (`G`, or the controller's right bumper) switches back to the weapon you held before, in single player and multiplayer. Rebind it under **Settings → Controls → Weapons → Last Weapon**.

### Corpses

**Settings → Game Options → General** has **Corpse Time** (how long bodies stay) and **Corpse Sink** (how they leave). From the console:

- `g_corpseRemoveDelaySP` (single-player) and `g_corpseRemoveDelayMP` (multiplayer): `0` uses the stock timing, `-1` never removes corpses, and a number above `0` is the delay in seconds before removal starts.
- `g_corpseSink`: `0` uses the normal dissolve or burn-away; `1` sinks corpses into the floor, Quake 3 style, while they stay ragdolls; `2` stops the ragdoll first, then sinks.

The corpse delay still decides when sinking starts, so with a delay of `-1` corpses never sink. These settings change only when bodies disappear, not health, gibbing or damage, and a new value applies best to bodies created after the change.

```cfg
// Keep corpses around in both modes
seta g_corpseRemoveDelaySP -1
seta g_corpseRemoveDelayMP -1
```

### Enhanced physics

**Settings → Game Options → General → Enhanced Physics** (`g_enhancedPhysics 1`) switches corpses, crates, barrels, debris and other loose objects to a newer simulation. Stacks settle and stay put, ragdolls fall on from the motion they had instead of dropping dead on the spot, and limbs no longer stay stretched after a blast. Hits push bodies with the same force as before, and a blast throws corpses, crates and barrels about as far as with the option off. On a slope a loose crate or barrel comes to rest sooner, where the original keeps it sliding. It costs about the same processor time as the original in most scenes and much less where piles of objects used to keep each other awake. In a few scenes it costs somewhat more than the original, most of all on Linux when several ragdolls come to rest at once. The option is off by default until that is settled and it has been reviewed in play.

It is off by default; off runs the original physics exactly. A change takes effect from the next map or loaded save. In multiplayer the server's setting applies to everyone, and a demo plays back with the setting it was recorded with.

### Classic dynamic lights

`g_classicDynamicLights` (on by default) brings back the Quake II and Quake III habit of lighting the world from the action: muzzle flashes throw a short, wide pool of light, bright projectiles carry their own glow, and explosions flash the surrounding walls. The colours come from the shipped Quake 4 assets. `g_classicDynamicLightScale` scales the radius of all of them from `0.25` to `4`; try lowering it before turning the feature off. See [Classic dynamic lights](classic-dynamic-lights.md) for the per-weapon colours.

### Flashlights

**Not in 0.14.1.** Your flashlight stays on your side of the wall. Held hard against a wall, a door or a thin panel, the gun used to reach through it, so the flashlight lit the room behind and left the wall in front of you dark. The marines' flashlight beams now shine from the flashlight under the barrel, widen out to the flashlight's reach as a soft cone, and stop at the first wall or person in the way; the original thin beam passed straight through walls. `g_flashlightBeam 0` brings the original beam back.

## Settings reference

All of these are saved automatically. Type them in the console, or put them in a config file as described in [Input settings](input-settings.md#config-files).

### HUD and crosshair

| Setting | Default | What it does |
|---|---:|---|
| `ui_hudLayout` | `1` | `1` Remastered, `2` Minimal, `3` Competitive, `0` Classic. |
| `ui_hudScale` | `1` (`1.25` on Steam Deck and with the [Couch profile](display-settings.md#television-viewing-couch-profile)) | HUD size, `0.5` to `2`. |
| `ui_hudWidth` | `1` | Spread of edge items: `0` 16:9, `1` 21:9, `2` full width. |
| `ui_hudSafeInset` | `0` | Inset from the screen edges, `0` to `10` percent. |
| `ui_hudOpacity` | `1` | HUD opacity, `0.25` to `1`. |
| `ui_hudDamageDirection` | `1` | `0` hides the damage direction indicator. |
| `ui_hudPickups` | `1` | `0` hides pickup messages. |
| `ui_hudWeaponStrip` | `1` | `0` keeps the weapon strip hidden when you change weapon. |
| `ui_reducedFlashing` | `0` | `1` turns on Reduced Flashing. |
| `g_objectivesToggle` | `0` | `1` opens and closes the objectives with each press of the scores button. |
| `g_crosshairSize` | `32` | Crosshair size: `16`, `24`, `32`, `40` or `48`. |
| `g_crosshairColor` | `1 1 1 1` | Crosshair colour as red, green, blue and opacity. |
| `hud_hitMarker` | `0` | `1` shows the hit marker. |
| `hud_hitMarkerScale` | `1` | Hit marker size, `0.25` to `4`, on top of the crosshair size. |
| `hud_crosshairHitFlash` | `0` | `1` also turns the crosshair red on a hit while the hit marker is on. |
| `hud_crosshairAnimated` | `1` | Animated crosshair (new interface only). |
| `ui_retainedReducedMotion` | `0` | `1` reduces decorative motion in the new interface, including the animated crosshair. |
| `ui_retained` | `1` | `1` new interface, `0` classic interface. |

### Multiplayer feedback

| Setting | Default | What it does |
|---|---:|---|
| `hud_objectiveMarkers` | `1` | Objective markers for CTF bases, control points and DeadZone zones. |
| `hud_damageNumbers` | `1` | Damage numbers: `0` off, `1` opponents only, `2` everything you deal. |
| `hud_damageNumberStyle` | `1` | `1` white to red, `2` colour per damage band, `3` colour per weapon. |
| `hud_damageNumberScale` | `1` | Damage number size, `0.25` to `4`. |
| `g_hitFeedback` | `2` | **Server setting.** `0` no hit feedback, `1` hits without the amount, `2` hits with the amount. |

### Game

| Setting | Default | What it does |
|---|---:|---|
| `g_skill` | `1` | Difficulty, `0` Private to `4` Warlord. A saved game brings back its own. |
| `g_turboMode` | `0` | Turbo Mode (single-player). |
| `ui_autoSwitch` | `2` | Weapon pickups: `0` never switch, `1` always switch (Quake 4), `2` smart. **Not in 0.14.1:** Quake 4's switch was `0` or `1`, default `1`. |
| `g_fov` | `90` | Field of view, measured on a 4:3 screen. |
| `g_autoSkipCinematics` | `0` | `1` skips cinematics as they start. |
| `ui_cinematicShake` | `1` | Extra cinematic camera movement, `0` to `1`. |
| `g_viewKick` | `1` | Camera kick and shake when you fire, `0` to `1`; aim is unchanged. |
| `g_screenShake` | `1` | Screen shake from explosions, machinery and vehicles (single-player), `0` to `1`. |
| `g_corpseRemoveDelaySP` | `0` | Single-player corpse delay in seconds; `0` stock, `-1` never. |
| `g_corpseRemoveDelayMP` | `0` | Multiplayer corpse delay in seconds; `0` stock, `-1` never. |
| `g_corpseSink` | `0` | `0` dissolve, `1` sink as a ragdoll, `2` sink without ragdoll. |
| `g_enhancedPhysics` | `0` | `1` uses the enhanced ragdoll and object physics from the next map; multiplayer follows the server. |
| `g_classicDynamicLights` | `1` | Dynamic lights on muzzle flashes, bright projectiles and explosions. |
| `g_classicDynamicLightScale` | `1` | Radius of those lights, `0.25` to `4`. |
| `g_flashlightBeam` | `1` | Characters' visible flashlight beams: `1` widen to the light's reach and stop at walls, `0` the original thin beam. **Not in 0.14.1.** |

### Sound and subtitles

| Setting | Default | What it does |
|---|---:|---|
| `s_musicVolume` | `0.5` | Music volume, `0` to `1`. |
| `s_dialogueVolume` | `1` | Dialogue volume, `0` (mute) to `1`. |
| `s_useEAXReverb` | `1` | Room reverb and door muffling. Apply a console change with `s_restart`. |
| `s_useEAXOcclusion` | `1` | Closed doors muffle sounds behind them while reverb is on. |
| `s_numberOfSpeakers` | `0` | `0` follow the device, `2` stereo, `6` 5.1. |
| `s_openALHRTF` | `0` | Headphone HRTF: `0` automatic, `1` off, `2` on. |
| `s_sourceRadiusScale` | `0.5` | Sound size; `0` makes every sound a point. |
| `s_directStereo` | `1` | Stereo sounds bypass 3D headphone audio. |
| `s_resampler` | `auto` | Upsampling filter; `listResamplers` lists the choices. |
| `s_outputLimiter` | `1` | Compresses loud scenes instead of clipping. |
| `s_airAbsorption` | `1` | Distant sounds lose a little treble. |
| `ui_subtitles` | `0` | `1` shows dialogue subtitles. |
| `ui_subtitleScale` | `1` | Subtitle text size, `1` to `2`; also follows the interface text size. |
| `ui_subtitleBackground` | `0.6` | Opacity of the black backing, `0` to `1`. |
| `ui_subtitleSpeakers` | `1` | Shows the speaker's name, or the radio label, when known. |
| `ui_subtitleUntranscribed` | `1` | Shows **[untranscribed speech]** for dialogue that has no transcript; `0` leaves it blank. |

## Troubleshooting

- **Damage numbers never appear.** They are multiplayer only and cover damage to players. Check the server's `g_hitFeedback`: at `0` or `1` the amount never reaches you. If they show for opponents but not team mates, that is `hud_damageNumbers 1`; set it to `2`.
- **Corpses still disappear quickly.** Set the delay for the mode you are playing: `g_corpseRemoveDelaySP` for single-player, `g_corpseRemoveDelayMP` for multiplayer.
- **Corpses never sink.** Make sure that mode's corpse delay is not `-1`.
- **Auto Skip Cinematics seems to do nothing.** It skips cinematics only, not scripted moments that play while you keep control.
- **Music Volume seems to do nothing.** Test where music is actually playing; general ambience and voice-over are not music.
- **No room reverb on a Mac.** The build is using Apple's own OpenAL, which has no reverb support; openQ4's macOS packages bundle OpenAL Soft instead.
- **To compare with and without reverb**, `s_muteEAXReverb 1` silences the reverb of the area you are standing in. `listReverbs` lists the reverbs, the current map's areas and those playing around you, and `s_showReverb 1` prints a line whenever they change.
