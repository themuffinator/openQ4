# Liquids

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

openQ4 has water, slime and lava that behave the way they do in the older Quake games: you wade,
you swim, you drown, and lava and slime hurt. Retail Quake 4 has none of this — everything that
looks like water in the stock maps is decoration with a `trigger_hurt` floating over it where it is
meant to be dangerous — so liquids are something you author, and this page is how.

## What a liquid does

| | water | slime | lava |
| --- | --- | --- | --- |
| wade and swim | yes | yes | yes |
| drown when submerged | yes | yes | yes |
| damage while in it | no | 10 per water level | 30 per water level |
| screen tint | blue | green | orange |
| muffled audio when submerged | yes | yes | yes |
| bots route through it | yes | never | never |

Water level is the Quake ladder: feet, waist, head. Damage scales with it, so standing ankle-deep
in lava is a third of the punishment of being under it — and being under it kills in about a second.

Movement matches Quake 3. Wading clamps your ground speed, and pushing into a clear-topped ledge
while waist- or head-deep does the water jump that pops you out of the pool. The ledge and clearance
probes are taken at the Quake 4 player's waist and just above their head, and the launch height
scales with player size and gravity, so looking up or down cannot make a valid pool exit fail.
Falling damage is halved at feet depth, quartered at waist depth, and gone entirely once your head
is under.

**Swimming.** Jump swims up, crouch swims down, and they keep working while you are stood on the
bottom of a pool. With no input at all you sink, as you always did in Quake.

Quake 3 swims at 160 units a second — its 320 run speed times a swim scale of 0.5. openQ4 runs at
160, so matching Quake 3 puts swimming at the same number as running here; that is a consequence of
Quake 4's slower footspeed rather than a mistake. Multiplayer and a stroggified player swim faster
still:

| | swim speed |
| --- | --- |
| singleplayer, before stroggification | 160 (`pm_swimSpeed`, Quake 3's figure) |
| multiplayer, or after stroggification | 200 (`pm_swimSpeedFast`) |

Swimming is its own gait: it is not slowed by holding crouch to descend, and not dropped to walk
speed for swimming straight up with no other key held.

## Building a liquid volume

Texture a brush with one of the shipped materials and that brush *is* the liquid:

```
textures/openq4/liquids/water         moving surface with heat-haze distortion
textures/openq4/liquids/water_calm    same volume, still surface, cheaper
textures/openq4/liquids/slime
textures/openq4/liquids/lava
```

Texture the whole brush, all six sides. The material clears the solid flag itself, so you do not
need to also mark it non-solid, and you must not add `solid` afterwards.

If you had been faking lava with a `trigger_hurt` over a decorative brush, delete the trigger when
you convert it. The two damage sources do not know about each other and you will take both.

## Trying the liquid lab

`mp/liquid_lab` is a compact multiplayer showcase built from the same materials. Its north row has
deep swimming water, shallow swimming water, and wading water; the south row has lava and slime.
Every liquid sits in a separate recessed basin, while a sky ceiling, shadow-casting overhead beams,
and submerged lights in the deep pool make the different surfaces easy to inspect. Lava has a
bright animated molten body, heat haze, steam and boiling patches; slime is a vivid cellular green
fluid with its own bubbling surface. Local water, lava and slime loops make each basin audible, with
the water bed placed below the surface so it behaves naturally from either side. Every spawn
receives the complete multiplayer arsenal and ammunition again on each respawn, which makes the map
useful for projectile-entry, impact, trail, and splash testing as well as movement.

Start it from the multiplayer map list or from the console:

```
spawnServer mp/liquid_lab
```

## Writing your own liquid material

The keyword that makes a liquid is `water`, `lava` or `slime`. It sets the content flag and clears
the solid flag in one step. Then:

```
textures/mymap/greenwater
{
	qer_editorimage	textures/mymap/greenwater.tga

	water           // the liquid keyword: water, lava or slime
	liquid          // surface type, so footsteps and impacts sound wet
	translucent     // REQUIRED - see below
	twosided        // so the surface is visible from underneath

	materialType	water   // impact reaction: water, lava or slime

	{
		blend	diffusemap
		map		textures/mymap/greenwater.tga
		translate	time * 0.03 , time * 0.02
	}
}
```

**`translucent` is not optional, even for lava.** dmap seals any brush whose sides are all opaque,
and a sealed volume has no space inside it — the player can never get in, and none of the liquid
behaviour will ever run. Lava and slime look solid but must still be declared translucent or your
pool will be a solid block.

`materialType` picks which impact effect and sound a weapon uses when it hits the surface. `water`
ships with Quake 4; `lava` and `slime` are added by openQ4 in
`materials/types/liquids_openq4.mtt`.

## Under the surface

Putting your head under changes how the world looks and sounds.

What makes water read as a *volume* rather than a colour filter is that everything it does gets
stronger with distance, so the view effect is built around the depth buffer:

- **Absorption.** Light loses red first, then green, exponentially with how far it travelled — the
  single fact that most makes underwater look underwater. Each liquid states what it still lets
  through at its own visibility range, so clear water carries a long way and lava barely a metre.
- **In-scattering.** What the water absorbs it puts back as its own colour, which is why distance
  goes pale and flat rather than simply dark. With absorption, this is the pair that turns a tint
  into a volume.
- **Bloom.** Suspended particulate throws light sideways, so every bright source grows a halo that
  widens with the water between it and you. It is the most recognisable single cue in the set.
- **Scattering blur.** Distance softens while near surfaces stay sharp.
- **Soft focus toward the edges.** A vignette in shape only — it never darkens anything. The centre
  of the view stays readable and the periphery loses its edges, the way a dive mask behaves.
- **Refraction.** Two crossed sine layers at different scales and speeds, so the wobble never reads
  as a repeating pattern.
- **Chromatic aberration** toward the edges, **caustics** from the surface overhead, and **marine
  snow** drifting past.

It eases in and out over about a sixth of a second, so breaking the surface does not cut.

Only the 3D view is affected. The HUD, the crosshair and any menu stay sharp and untinted — the
effect runs inside the world render, not over the finished frame.

**Sound.** Non-global world sounds muffle while your head is under, and anything on the other side
of the surface is occluded on top of that: the world above goes distant and dull the moment you go
under, while music, announcements and interface sounds remain clear. Room reverb is turned well down
for submerged and cross-surface audio so the result sounds enclosed by water rather than like a
large hall. Entry and exit visuals still happen at every crossing, but their shared surface
sound has a 600 ms debounce so a single jump in shallow water cannot play it twice.

**Shots and trails.** A liquid surface is a presentation boundary, not a bulletproof wall.
Projectiles and hitscan shots continue through it, play a splash and an explicit impact sound at the
crossing, and can still hit geometry or actors inside the pool. A moving projectile swaps its smoke
for local bubbles while it is submerged. A hitscan shot draws a one-shot bubble wake over only the
part of the ray inside the volume, even when both its muzzle and eventual hit are in air.

OpenGL and Vulkan both draw the effect. Setting `r_underwater 0` replaces it with a flat colour
wash, so you still know you are under something.

## A liquid volume without a brush

If you just want a box of liquid — or you are adding one to a map you cannot recompile — there is an
entity for it:

```
func_liquid_openq4_water
func_liquid_openq4_slime
func_liquid_openq4_lava
```

Set `size` for the box, which sits on the entity origin, or give explicit `mins`/`maxs`. It works
from the console too, which is the quickest way to see any of this in a stock map:

```bash
spawn func_liquid_openq4_lava size "512 512 256"
```

## Tuning

| cvar | default | what it does |
| --- | --- | --- |
| `pm_waterAir` | 720 | frames underwater before drowning starts — 720 is Quake 3's twelve seconds |
| `g_drownDamageMax` | 15 | ceiling on the rising drown damage |
| `g_liquidDamageInterval` | 500 | milliseconds between lava and slime damage ticks |
| `g_liquidScreenTint` | 1 | overall strength of the underwater view, `0` turns it off |
| `r_underwater` | 1 | the underwater view post-process; `0` falls back to a flat colour wash |
| `r_underwaterWarp` | 1.0 | refraction strength |
| `r_underwaterBlur` | 1.0 | how much distance softens the image |
| `r_underwaterEdgeSoften` | 0.8 | soft focus toward the edges — a focus mask, it never darkens |
| `r_underwaterBloom` | 1.0 | halo around lights |
| `r_underwaterAberration` | 0.35 | channel separation toward the edges |
| `r_underwaterParticles` | 0.25 | marine snow |
| `r_underwaterVisibility` | 1.0 | scales how far you can see through any liquid |
| `r_underwaterCaustics` | 0.06 | strength of the moving light pattern |
| `pm_swimSpeed` | 160 | swim speed, Quake 3's figure |
| `pm_swimSpeedFast` | 200 | swim speed in multiplayer and after stroggification |
| `g_debugLiquid` | 0 | log water level changes, the air reservoir, speed, every damage tick and each wading footstep |
| `g_liquidTestVolume` | "" | dev aid: drop a `water`/`slime`/`lava` volume around the player on spawn |
| `g_liquidTestVolumeSize` | 640 | cube size of that test volume |

Drowning shares the air reservoir and HUD readout with Quake 4's vacuum areas. Underwater the
reservoir drains faster, so a full bar is twelve seconds of swimming but still the full `pm_air`
worth of vacuum.

Drowning, slime and lava each have their own multiplayer death method, localized obituary text and
colored graphical death-feed icon built from stock images. Mods can replace the `dm2`, `dm3` and
`dm4` icon/text entries in `liquid_openq4` without changing game code.

## Retargeting the sounds and effects

Every liquid sound and splash is keyed off a single def, `liquid_openq4` in
`def/liquids_openq4.def`. Override that def in your mod to change the whole set at once. Any key you
leave out is simply silent, so a partial set is fine. The shipped defaults point directly at stock
Quake 4 sounds and effects; the Liquid Volume Lab keeps its custom ambience and hazardous-surface
bubbling scoped to the showcase map.

Keys are `<what>_<liquid>`: `snd_enter_water`, `snd_under_lava`, `fx_splash_slime`,
`fx_impact_water`, `fx_bubbles_water`, `fx_projectile_trail_slime`, and
`fx_trace_trail_water`, plus `snd_drown` and `snd_wade`. Projectile trails are local looping
effects; trace trails receive an end origin and should use a line distribution with
`useEndOrigin linearSpacing`.

A weapon that defines its own collision `fx_impact_water` keeps it. A projectile crossing a volume
uses the shared stock liquid impact and explicit `snd_impact_*` entry so its sound cannot disappear
with an unreliable visual-effect message.

## Monsters and NPCs

Monsters are in the same water you are. Lava and slime burn them at the same rate and with the same
water-level scaling, and anything with its head under drowns after twelve seconds. Breaking the
surface splashes and sounds for them exactly as it does for the player.

Quake 4's invulnerable story marines are unaffected, because they already ignore damage — nothing
special was needed for that.

Two def keys opt a monster out:

```
"canBreatheLiquid"  "1"   // never drowns: anything aquatic, or that does not breathe
"liquidImmune"      "1"   // lava and slime do nothing: anything already made of fire
```

## Bots

Bots swim, surface before they drown, and climb when something is burning them. They will not route
through lava or slime: those cells are refused when the navigation mesh is built, and a bot that is
somehow pushed toward one will refuse the step. Water is left in the graph, because it is crossable.
