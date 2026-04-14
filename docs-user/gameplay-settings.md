# Gameplay Settings and Runtime Toggles

This guide covers a small set of gameplay and audio cvars that are useful for testing, accessibility, and personal preference.

The corpse cleanup and corpse sink controls are also available in the in-game menu at `Settings -> Game Options`.

## Quick Reference

| Setting | Default | Scope | What it does |
|---|---:|---|---|
| `g_autoSkipCinematics` | `0` | SP and MP game code | Automatically skips cinematics as soon as they begin. Disabled by default. |
| `g_corpseRemoveDelaySP` | `0` | Single-player | Controls how long SP corpses remain before disappearing. `0` uses stock timing, `-1` disables corpse removal. |
| `g_corpseRemoveDelayMP` | `0` | Multiplayer | Controls how long MP corpses remain before disappearing. `0` uses stock timing, `-1` disables corpse removal. |
| `g_corpseSink` | `0` | SP and MP game code | Selects corpse sink mode instead of the normal dissolve or burn-away behavior. |
| `s_musicVolume` | `0.5` | Client audio | Controls music volume independently of the main sound mix. |

## Cinematics

`g_autoSkipCinematics` is intended for repeat testing runs, speed-focused replays, and development workflows where you do not want to manually skip every scripted sequence.

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

## Corpse Cleanup

OpenQ4 now exposes separate corpse-removal timing controls for single-player and multiplayer.

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
