# Liquids

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

openQ4 adds real water, slime and lava, the way the older Quake games had them:
you wade, swim and drown, and slime and lava hurt. They work in maps built for
them. The stock Quake 4 maps have none: the water you see there is decoration,
and dangerous pools are a hidden damage trigger over a solid surface.

The first half of this guide covers playing in liquids. The rest is for mappers
and modders who want to add them.

## Playing in liquids

| | Water | Slime | Lava |
| --- | --- | --- | --- |
| Wade and swim | yes | yes | yes |
| Drown when your head is under | yes | yes | yes |
| Damage while you are in it | none | 10 per depth level | 30 per depth level |
| View tint when your head is under | blue | green | orange |
| Bots go through it | yes | never | never |

**Depth** has three levels: feet, waist and head. Slime and lava damage scale
with it, every half second, so ankle-deep lava costs a third of what being under
it does, and being under it kills in about a second.

**Moving.**

- Wading slows you down.
- Jump swims up and crouch swims down, even while you are standing on the
  bottom. With no input you sink.
- Swimming runs at 160 units a second in single-player, the same as running,
  and 200 in multiplayer or once you have been stroggified. Holding crouch to
  dive or swimming straight up does not slow you.
- Push into a ledge while waist- or head-deep to jump out of the pool.
- Landing in a liquid softens a fall: half damage at feet depth, a quarter at
  waist depth, none with your head under.

**Air and drowning.** You have twelve seconds of air with your head under. After
that you take drowning damage that grows each time, up to 15 a hit. Your air
shows on the same HUD readout as Quake 4's vacuum areas. It drains faster
underwater, so a full bar is twelve seconds of swimming but still the usual 30
seconds of vacuum.

**Under the surface.** With your head under, the view takes on the liquid's
colour and gets hazier with distance: colours fade (red first), lights grow
halos, far things blur, the edges of the view soften, the picture wobbles
gently, and you see light patterns from the surface and drifting particles.
Clear water lets you see a long way; lava barely a metre. The change eases in
over a fraction of a second, and your HUD, crosshair and menus are never
tinted. Both OpenGL and Vulkan draw it. If you prefer a plain colour wash, enter
`r_underwater 0` in the console.

**Sound** is muffled with your head under, and sounds from above the surface go
distant and dull. Music, announcer and menu sounds stay clear.

**Weapons** fire through the surface. Shots splash where they cross it and can
still hit anything in the pool. Projectiles trail bubbles instead of smoke while
underwater, and instant-hit weapons leave a bubble trail along the underwater
part of the shot.

**Obituaries.** Drowning, slime and lava each have their own multiplayer death
message and kill-feed icon.

## Try the Liquid Volume Lab

The **Liquid Volume Lab** (`mp/liquid_lab`) is a small multiplayer map that
shows every liquid side by side: deep water, shallow water and wading water on
one side, lava and slime on the other, each in its own basin. Every spawn gets
the full arsenal, so you can also try shots, splashes and trails.

Pick it from the multiplayer map list (Deathmatch, Team Deathmatch or Tourney),
or enter this in the console:

```
spawnServer mp/liquid_lab
```

## Building a liquid volume

Texture a brush with one of the shipped materials and that brush becomes the
liquid:

```
textures/openq4/liquids/water         moving surface with heat-haze distortion
textures/openq4/liquids/water_calm    same volume, still surface, cheaper to draw
textures/openq4/liquids/slime
textures/openq4/liquids/lava
```

- Texture the whole brush, all six sides.
- The material makes the brush non-solid itself. Do not add `solid` afterwards.
- If you were faking lava with a `trigger_hurt` over a decorative brush, delete
  the trigger when you convert it. Otherwise players take damage from both.

## Writing your own liquid material

One of the keywords `water`, `lava` or `slime` makes a material a liquid and
non-solid in one step. A complete example:

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

**`translucent` is required, even for lava and slime.** When dmap compiles the
map, it seals any brush whose sides are all opaque. A sealed brush has no inside,
so players can never enter it and your pool behaves as a solid block.

`materialType` picks the impact effect and sound when a weapon hits the surface.
`water` comes with Quake 4; openQ4 adds `lava` and `slime` in
`materials/types/liquids_openq4.mtt`.

## Adding a liquid without recompiling the map

To drop a box of liquid into a map, including a map you cannot recompile, use
one of these entities:

```
func_liquid_openq4_water
func_liquid_openq4_slime
func_liquid_openq4_lava
```

Set `size` for the box, which sits on the entity origin (default
`256 256 128`), or give explicit `mins` and `maxs`. `func_liquid_openq4` with a
`liquid` key of `water`, `slime` or `lava` does the same.

The quickest way to see liquids in a stock map is the console. Load the map with
`devmap` so cheats are on, then:

```
spawn func_liquid_openq4_lava size "512 512 256"
```

## Changing liquid sounds and effects

Every liquid sound and splash comes from one entity definition, `liquid_openq4`
in `def/liquids_openq4.def`. Override it in your mod to change the whole set at
once. A key you leave out is silent, so a partial set is fine. The defaults use
stock Quake 4 sounds and effects; the Liquid Volume Lab's own ambience and
bubbling stay on that map.

Keys are named `<what>_<liquid>`, for example `snd_enter_water`,
`snd_under_lava`, `fx_splash_slime`, `fx_impact_water`, `fx_bubbles_water`,
`fx_projectile_trail_slime` and `fx_trace_trail_water`, plus `snd_drown` and
`snd_wade`.

- Projectile trails are local looping effects.
- Trace trails (for instant-hit shots) receive an end point, so give them a line
  distribution with `useEndOrigin linearSpacing`.
- A weapon that defines its own `fx_impact_water` keeps it. A projectile crossing
  a volume uses the shared liquid impact and its `snd_impact_*` sound.

The multiplayer death messages and kill-feed icons are the `dm2`, `dm3` and
`dm4` entries in `liquid_openq4`; mods can replace them without changing game
code.

## Monsters and bots

Monsters share your liquids. Slime and lava burn them at the same rate, scaled
by depth the same way, and anything with its head under drowns after twelve
seconds. They splash and make sound when they cross the surface, as you do.
Quake 4's invulnerable story marines are unaffected because they already ignore
damage.

Two entity definition keys opt a monster out:

```
"canBreatheLiquid"  "1"   // never drowns: anything aquatic, or that does not breathe
"liquidImmune"      "1"   // slime and lava do nothing: anything already made of fire
```

**Bots** swim, come up for air before they drown, and climb out when something
burns them. They never route through slime or lava: those areas are left out of
their navigation when it is built, and a bot pushed towards one refuses the
step. Water stays in their routes because it is safe to cross.

## Settings reference

All of these are console variables (cvars). The underwater view settings are
saved in your config.

**Underwater view**

| Setting | Default | What it does |
| --- | --- | --- |
| `g_liquidScreenTint` | 1 | Overall strength of the underwater view, 0 to 1; `0` turns it off. |
| `r_underwater` | 1 | The full underwater view; `0` uses a plain colour wash instead. |
| `r_underwaterWarp` | 1.0 | How much the picture wobbles. |
| `r_underwaterBlur` | 1.0 | How much distance blurs. |
| `r_underwaterEdgeSoften` | 0.8 | How much the edges of the view soften. It never darkens them. |
| `r_underwaterBloom` | 1.0 | Halos around lights. |
| `r_underwaterAberration` | 0.35 | Colour fringing towards the edges. |
| `r_underwaterParticles` | 0.25 | Drifting particles. |
| `r_underwaterVisibility` | 1.0 | Scales how far you can see through any liquid. |
| `r_underwaterCaustics` | 0.06 | Strength of the moving light pattern. |

**Gameplay** (in multiplayer the server's values apply)

| Setting | Default | What it does |
| --- | --- | --- |
| `pm_waterAir` | 720 | How long you can stay under before drowning, in 60ths of a second (720 is twelve seconds, as in Quake 3). |
| `g_drownDamageMax` | 15 | The most damage one drowning hit can do. |
| `g_liquidDamageInterval` | 500 | Milliseconds between slime and lava damage hits. |
| `pm_swimSpeed` | 160 | Swim speed in single-player before stroggification (Quake 3's figure). |
| `pm_swimSpeedFast` | 200 | Swim speed in multiplayer and after stroggification. |

## Troubleshooting and diagnostics

| Setting | Default | What it does |
| --- | --- | --- |
| `g_debugLiquid` | 0 | Prints depth changes, your air, speed, every damage hit and projectile surface crossings to the console. It also prints each wading footstep (not in 0.14.1). |
| `g_liquidTestVolume` | "" | Puts a `water`, `slime` or `lava` volume around you when you spawn; empty turns it off. |
| `g_liquidTestVolumeSize` | 640 | Size of that test volume's cube. |

If you cannot get into your pool at all, check that its material has
`translucent`. If players take damage twice, look for a leftover `trigger_hurt`.
