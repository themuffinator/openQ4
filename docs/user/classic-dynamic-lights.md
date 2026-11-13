# Classic dynamic lights

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

In Quake II and Quake III, the action lit the world: muzzle flashes threw light across the room, rockets and plasma carried their glow down the corridor, and explosions flashed the walls. Quake 4's shipped effects mostly don't: apart from the single-player rocket and grenade trails, player projectiles carry no light, and no explosion in the game lights the room. openQ4 adds that classic layer back, in single-player and multiplayer. It is on by default.

## Turning it down or off

There is no menu switch; use the console.

- `g_classicDynamicLightScale` makes every classic light reach further or less far, from `0.25` to `4`. If the effect is too strong, try `0.5` before turning it off.
- `g_classicDynamicLights 0` turns it off. Live flashes end at once and no new ones start; projectiles already in flight keep their light until they expire.

Both are saved, so your choice survives a restart. `g_projectileLights 0` still hides all projectile lights, classic ones included, and `g_muzzleFlash 0` still hides muzzle flashes.

## What it adds

**Muzzle flashes.** Firing adds a short, wide light at the barrel: about 100 ms long, with half again the reach of the weapon's own flash. It sits on top of the stock flash, so each shot pops harder and briefly lights more of the room. Weapons without a flash light of their own get one.

**Bright projectiles.** Rockets, plasma bolts and dark matter spheres carry a light. This only fills gaps: a projectile that already has its own light keeps it, and the single-player rocket and grenade, whose trails already light their surroundings, are left alone so their glow isn't doubled. Their multiplayer trails have no light, so those get the classic one.

**Explosions.** A detonation flashes the surrounding walls and fades over 120 to 400 ms, depending on what went off. Exploding barrels and other breakables flash too.

Bullets, nails, debris and glass get nothing: they aren't bright, there are far too many of them to light, and Quake II and Quake III didn't light them either.

## Colours

The colours come from Quake 4's own assets, so each light looks like part of the effect it belongs to.

Muzzle flashes use the firing weapon's own `flashColor`, including any colour a mod sets. For the stock weapons:

| Weapon | `flashColor` | Looks |
|---|---|---|
| Blaster, Hyperblaster, Gauntlet | `0.7 0.8 1` | blue-white |
| Machinegun, Shotgun, Nailgun | `1 0.8 0.4` | warm white |
| Rocket Launcher, Grenade Launcher, Napalm Gun, Dark Matter Gun | `0.99 0.84 0.31` | amber |
| Railgun | `0.72 1 0.9` | pale green-white |
| Lightning Gun | `0.5 0.8 1` | pale blue |

Projectile and explosion colours come from lights Raven authored elsewhere in the same assets:

| Light | Colour | Taken from |
|---|---|---|
| Rocket | `0.906 0.518 0.161` | `effects/weapons/rocketlauncher/fly.fx` |
| Grenade | `0.973 0.286 0.086` | `effects/weapons/grenadelauncher/trail.fx` |
| Napalm | `0.922 0.545 0.322` | `effects/weapons/napalmgun/globburn.fx` |
| Dark matter | `0.502 0 1` | `effects/weapons/dmg/core.fx` |
| Energy bolts | `0.7 0.8 1` | the energy weapons' own `flashColor` |
| Most explosions | `1 0.824 0.290` | `effects/monsters/strogg_flyer/bomb_burst.fx` |

Energy bolts, napalm and dark matter flash in their own colours when they detonate.

## For modders: content overrides

An entity definition can override what openQ4 picks for it:

```
"classic_light"         "0"             // never give this one a classic light
"classic_light_color"   "1 0.5 0.2"     // use this colour instead
"classic_light_radius"  "180"           // light radius while in flight, in game units
```

`classic_light_radius` also overrides the rule that skips projectiles whose effect already has a light, so a mod that replaces the single-player rocket trail with an unlit one can ask for the classic light back.

## Cost

Each classic light is a real light in openQ4's renderer, not the cheap tint it was in Quake II. To keep the cost down, they cast no shadows, at most 24 flashes are alive at once (the oldest is reused when the pool is full), and flashes more than about 3000 units from you are skipped.

## Seeing them without firing

With cheats enabled, `testClassicLight` raises the flashes in front of you:

```
testClassicLight                                  // one muzzle flash and one explosion
testClassicLight muzzle weapon_railgun            // that weapon's flash colour
testClassicLight explosion projectile_dmg         // that projectile's explosion flash
```

## Settings reference

| Setting | Default | Range | What it does |
|---|---:|---|---|
| `g_classicDynamicLights` | `1` | `0..1` | Quake II and III style lights on muzzle flashes, bright projectiles and explosions. |
| `g_classicDynamicLightScale` | `1` | `0.25..4` | Reach of those lights. |
| `g_projectileLights` | `1` | `0..1` | Shows lights on projectiles, classic ones included. |
| `g_muzzleFlash` | `1` | `0..1` | Shows muzzle flashes. |
