# Cel shading

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

Cel shading gives Quake 4 a cartoon look: lighting falls into flat bands of tone, and black ink outlines the shapes. It's off by default, so the game looks like stock Quake 4 until you turn it on. It works on OpenGL and Vulkan. There is no menu switch; you set it from the console.

## Turning it on

Open the console and enter:

```cfg
seta r_celShading 1
seta r_celShadingWorld 1
```

Both apply immediately, with no `vid_restart`. Set them back to `0` to return to the stock look exactly.

- `r_celShading` covers characters and objects: players, monsters, movable objects and your first-person weapon.
- `r_celShadingWorld` covers the level itself: walls, floors and other map geometry.

They are separate because many players want one without the other, and because they cost different amounts. Cel-shaded characters in a normally lit level is a popular and cheap combination; the world switch is the one that adds full-screen work every frame.

## Ready-made looks

### Comic book

Heavy ink, few tones.

```cfg
seta r_celShading 1
seta r_celShadingWorld 1
seta r_celShadingSteps 3
seta r_celOutlineWidth 3.0
seta r_celShadingWorldWidth 3.0
seta r_celShadingWorldNormalThreshold 0.5
```

### Cel-shaded characters only

Leaves the level looking like Quake 4 and costs almost nothing extra.

```cfg
seta r_celShading 1
seta r_celShadingWorld 0
seta r_celShadingSteps 4
seta r_celOutlineWidth 2.0
```

### Soft toon

Keeps the outlines and tonal steps but eases the band edges. It's the calmest option in motion.

```cfg
seta r_celShading 1
seta r_celShadingWorld 1
seta r_celShadingSteps 5
seta r_celShadingSoftness 0.5
seta r_celShadingWorldNormalThreshold 0.25
```

### Outlines without banding

Ink only, with normal lighting underneath.

```cfg
seta r_celShading 1
seta r_celShadingWorld 1
seta r_celShadingBands 0
```

## Banded lighting

Each light's contribution is stepped onto a ladder of flat tones instead of a smooth ramp. `r_celShadingSteps` sets how many bands there are: fewer is more graphic, and `2` gives a pure two-tone look. Shadows, falloff and projected light patterns all land on the same bands, and `r_celShadingSpecular` turns shiny highlights into hard-edged patches on the same ladder.

Pure black and full brightness are left alone, so unlit surfaces stay unlit and bright lights keep their punch. Bands follow the brightest colour channel, so a coloured light never changes hue.

**Softness.** At `r_celShadingSoftness 0`, each band edge is a hard step. That's the classic cel look, but the edge can visibly crawl across curved surfaces as the camera moves. Raising softness blends each edge over part of a band: `0.5` blends half a band either side, and `1.0` is nearly smooth shading that still keeps the tonal steps. The bands themselves stay where they are. `0.2` to `0.4` looks clearly banded while removing most of the crawl. It defaults to `0` so existing cel configs keep the look they were tuned for.

## Outlines on characters and weapons

Each cel-shaded character or object gets an outline drawn from an enlarged copy of its own shape. The width is measured in screen pixels, so a line stays the same thickness close up or across the room. `r_celOutlineWidth` sets it.

Your first-person weapon has its own width and opacity (`r_celViewWeaponOutlineWidth`, `r_celViewWeaponOutlineAlpha`), because it fills so much of the screen that a line tuned for a distant monster looks far too heavy on it. `r_celViewWeapon 0` leaves the weapon out entirely.

Set the ink colour as four numbers from 0 to 255 (red, green, blue, opacity). The same colour inks the level's outlines.

```cfg
seta r_celOutlineColor "0 0 0 255"      // black ink
seta r_celOutlineColor "20 10 60 255"   // deep violet ink
seta r_celOutlineColor "0 0 0 160"      // softened black
```

An opacity of `0` switches the ink off completely while leaving the banding on. A value openQ4 can't read falls back to opaque black, so a typo never silently removes the outlines. Outlines are drawn after all lighting, so turning brightness up can't wash them out.

## Outlines on the level

The level has no shapes to enlarge, so openQ4 finds its edges on screen instead. It looks for two kinds:

- **Silhouettes**, where one surface passes in front of another. `r_celShadingWorldDepthThreshold` sets how small a step counts, measured against distance so lines keep the same weight near and far.
- **Creases**, where two faces meet at the same depth, such as a wall meeting a floor. `r_celShadingWorldNormalThreshold` sets how sharp a corner must be. `0` drops creases and keeps silhouettes only, which looks cleaner in detailed rooms.

Edges against the sky always count as silhouettes, so the horizon is always inked. Characters never get a second line from this pass on top of their own outline.

Character outlines fade into fog along with the character. Level outlines are added after fog and bloom, so they stay crisp, but a far wall in heavy fog still gets a full-strength line. Lower `r_celShadingWorldAlpha` to balance them.

## Requirements and limits

- **On OpenGL, banding needs GLSL shaders.** Without them, and on a few surfaces drawn by the older shader path, lighting stays smooth while the outlines still draw. The level's outlines need GLSL and don't draw without it.
- Without GLSL, character outlines use a simpler enlargement that gets the width roughly right but less exactly towards the edges of a large model. They also draw without masking if there is no stencil buffer.
- Cut-out cards such as grates and fences are outlined around their whole rectangle, not their visible shape. The level's outline pass draws them correctly.
- The level's outlines draw only in the main view: not in mirrors, portal skies or other in-world screens. `r_skipPostProcess 1` turns them off with the other full-screen effects. Banding and character outlines are unaffected by either.

## Troubleshooting

**Nothing changed after `r_celShading 1`.** It affects characters and objects, so look at one, or at your weapon. A room with nothing but level geometry needs `r_celShadingWorld 1` too.

**Banding but no outlines.** Check `r_celOutline`, `r_celOutlineAlpha` and the last number of `r_celOutlineColor`: any of them at zero switches the outlines off.

**Characters have outlines but the level doesn't.** Set `r_celShadingWorldDebug 1`. It shows the detected edges over a black background, which is also the best way to tune the two thresholds, and prints the reason whenever the level outline pass doesn't run, for example `cel world outline skipped: not the main scene view (subview, portal sky, or 2D pass)`. It resets when you restart the game.

**Band edges shimmer or crawl when you move.** Raise `r_celShadingSoftness`, starting at `0.3`.

**Distant walls look scribbled on.** Raise `r_celShadingWorldDepthThreshold`, or lower `r_celShadingWorldNormalThreshold` to drop creases.

**The weapon's outline is too heavy.** Lower `r_celViewWeaponOutlineWidth`, or set `r_celViewWeapon 0`.

**Banding looks flat and grey.** Lower `r_celShadingSteps`.

## Settings reference

| Setting | Default | Range | What it does |
|---|---|---|---|
| `r_celShading` | `0` | 0/1 | Cel shading for characters, objects and your weapon. |
| `r_celShadingWorld` | `0` | 0/1 | Cel shading and outlines for the level itself. |
| `r_celShadingBands` | `1` | 0/1 | Steps lighting into bands. `0` keeps smooth lighting with outlines only. |
| `r_celShadingSteps` | `4` | 2-8 | Number of bands. Lower is more graphic. |
| `r_celShadingSoftness` | `0` | 0.0-1.0 | How much of a band each edge blends across. |
| `r_celShadingSpecular` | `1` | 0/1 | Turns shiny highlights into hard-edged patches. |
| `r_celViewWeapon` | `1` | 0/1 | Includes your first-person weapon and arms. |
| `r_celOutline` | `1` | 0/1 | Outlines around characters and objects. |
| `r_celOutlineWidth` | `2.0` | 0.5-8.0 | Character outline width, in screen pixels. |
| `r_celOutlineAlpha` | `1.0` | 0.0-1.0 | Character outline opacity. |
| `r_celOutlineColor` | `0 0 0 255` | 0-255 each | Ink colour as `"r g b a"`, shared by character and level outlines. |
| `r_celViewWeaponOutlineWidth` | `1.0` | 0.5-8.0 | Weapon outline width. |
| `r_celViewWeaponOutlineAlpha` | `1.0` | 0.0-1.0 | Weapon outline opacity. |
| `r_celShadingWorldWidth` | `2.0` | 1.0-8.0 | Level outline thickness, in screen pixels. |
| `r_celShadingWorldAlpha` | `1.0` | 0.0-1.0 | Level outline opacity. |
| `r_celShadingWorldDepthThreshold` | `0.0015` | 0.0001-0.02 | Silhouette sensitivity. Lower catches smaller depth steps. |
| `r_celShadingWorldNormalThreshold` | `0.4` | 0.0-1.0 | Crease sensitivity. `0` draws silhouettes only. |
| `r_celShadingWorldDebug` | `0` | 0/1 | Shows the detected level edges and reports why the pass is skipped. Not saved. |
