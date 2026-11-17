# DDS texture replacements

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

openQ4 can load texture packs made of DDS files (a compressed image format that
graphics cards read directly) in place of Quake 4's own textures, without
changing the original game files. It reads packs laid out the dhewm3 way, in a
`dds/` folder, and packs that put the DDS files next to the original textures,
as some Quake 4 packs do. It also reuses the precomputed textures that ship
inside Quake 4 itself.

The first part of this guide is for players installing a pack; the rest is for
pack authors.

## Installing a texture pack

1. Put the pack in openQ4's `baseoq4` folder: either its loose `dds/` folder,
   or the pack's PK4 file (a Quake 4 package, which is a renamed zip file).
2. Start openQ4 and load a map. Replacements are on by default, so there is
   nothing to switch on.

Keep packs in `baseoq4`, not in Quake 4's `q4base` folder: `q4base` also works,
but a Steam file verification may delete them there.

To check that the pack is being used, see [Troubleshooting](#troubleshooting).

## The replacement setting

`image_usePrecompressedTextures` is a console variable (cvar) with three values.
Set it, then load a map or run `reloadImages all`:

- `1` (default) uses any supported replacement (DXT1, DXT5, RXGB and BC7) and
  also reuses the precomputed textures in Quake 4's own PK4s. **This is the
  right setting for a BC7 pack.** The performance presets keep it at 1.
- `2` uses only BC7 replacements, and ignores DXT and RXGB files, including the
  ones in Quake 4's PK4s. Use it only if you want everything else to come from
  the uncompressed originals.
- `0` turns off all DDS replacements, including your own packs.

**BC7 needs graphics support.** Run `gfxInfo` and check the
`Texture compression: S3TC/DXT=1 BC7/BPTC=1` line. Without BC7 support, BC7
files still load, uncompressed, so they use more video memory. DXT and RXGB
files are decoded by the processor when the graphics card cannot take them
directly. **Not in 0.14.1:** BC7 files are decoded the same way; 0.14.1 skipped
them on graphics without BC7 support.

> **macOS:** with OpenGL, a Mac has no BC7 support. Apple's OpenGL stops below
> the version that added it, so `gfxInfo` reports `BC7/BPTC=0` on every Mac,
> Apple Silicon and Intel alike, and every BC7 file loads uncompressed. No
> setting or driver update changes that. With the experimental Vulkan renderer,
> `gfxInfo` shows whether the Mac's graphics support it. Pack authors: ship DXT
> or RXGB files alongside BC7 if Mac players matter to you.

## Making a pack

### Where files belong

Mirror the original texture's path under `dds/` and change the extension:

```text
Original texture:  textures/stone/wall01.tga
Replacement:       baseoq4/dds/textures/stone/wall01.dds
```

A `.dds` placed beside the original path instead, such as
`baseoq4/textures/stone/wall01.dds`, also works. If both exist, the `dds/` copy
wins.

**Not in 0.14.1:** uncompressed replacements and new textures can also be PNG
files. When a material names `wall01.tga` and there is no `.tga` or `.jpg`,
openQ4 loads `wall01.png`, and a material may name a `.png` directly. Every
kind of PNG works: greyscale, colour and palette images at any bit depth,
interlaced files, and transparency. 16-bit images load at 8 bits per channel.

### Bump maps and other image programs

Quake 4 materials rarely name a bump texture directly. They usually build it
with an image *program*:

```text
bumpmap addnormals( textures/stone/wall01_local, heightmap( textures/stone/wall01_h, 4 ) )
```

You can replace either end:

- **Replace the finished result.** Name the file after the whole program, using
  dhewm3's naming:

  ```text
  heightmap(models/monsters/foo_local, 4)
      -> dds/heightmap/models/monsters/foo_local 4.dds
  ```

  Pack tools should keep that naming for combined programs such as
  `heightmap`, `addnormals`, `add` and `scale`.
- **Replace an input.** Ship `dds/textures/stone/wall01_local.dds` (or the
  `.dds` beside the original) and openQ4 recomputes the program's result from
  it at load. This is the simplest option and what a pack made for Quake 4
  normally does. **Not in 0.14.1:** Quake 4 ships a precomputed result for
  nearly every stock bump map, and 0.14.1 kept using it even after a pack
  replaced the inputs. openQ4 now sets Quake 4's result aside as soon as any
  input comes from outside Quake 4's and openQ4's own packs, and stores the
  rebuilt bump map compressed (DXT5, the layout Quake 4's own use), so a 2K
  bump map costs about 5 MB of video memory rather than 21 MB. BC7 inputs are
  decoded too; 0.14.1 skipped them.

Rebuilding takes longer than loading a finished file, so the first load of a
map after installing a pack is slower. openQ4 keeps the results in its image
cache, and later loads read them from there.

**`downsize()` textures. Not in 0.14.1.** Quake 4 wraps most character, weapon
and monster textures in `downsize( <texture>, 1 )`. A replacement named after
the texture itself, such as `models/characters/marine/helmet_d.dds` or
`dds/models/characters/marine/helmet_d.dds`, now covers it, and it loads
compressed. 0.14.1 showed Quake 4's own half-size version instead.

If you are not sure what an image is called, turn on
`image_showPrecompressedTextures` (see [Troubleshooting](#troubleshooting)):
openQ4 prints every path it looked for.

### Search order

For each image, openQ4 tries these in order and uses the first usable file:

1. `dds/<image program name>.dds`, the dhewm3 naming
2. `dds/<texture path>.dds`, for plain textures
3. `<texture path>.dds`, beside the original `.tga`, `.jpg` or `.png`, as Quake 4 packs do
4. for `downsize( <texture>, N )`, steps 2 and 3 for that texture (**Not in 0.14.1**)
5. `progimg/...`, the precomputed results inside Quake 4's own PK4s, unless an
   input of the program was replaced (**Not in 0.14.1**)

Quake 4's own precomputed files come last, so a replacement named in steps 1-4
always wins, and so does a replaced input. openQ4 finds them
automatically, including nested programs and environment cube faces under
`gfx/env/`; you don't need to extract, rename or regenerate them.

### BC7 files

openQ4 accepts single 2D BC7 textures with either:

- a DX10 header with `DXGI_FORMAT_BC7_UNORM` or `DXGI_FORMAT_BC7_UNORM_SRGB`; or
- a common `BC7` FourCC code.

Texture arrays, cube maps, volume textures, invalid sizes and files with
missing mipmap levels are rejected safely. Your own mipmaps are used as they
are. When texture size is reduced, openQ4 skips the largest levels, and images
that never use mipmaps upload only the full-size level.

### Normal maps

Write BC7 normal maps the ordinary way: X, Y and Z in red, green and blue, with
alpha unused. That is the default in `texconv`, NVTT, Compressonator and
`bc7enc`, and it is what openQ4 expects.

> **Packs encoded with `bc7enc -r2a` need re-encoding.** That option moves the
> channels into the DXT5 normal-map layout. openQ4 reads BC7 normal maps as
> plain RGB, so such a pack shows flat lighting; encode it again without
> `-r2a`. Only DXT5 and true `RXGB` files use the swapped layout.

### Stale files

openQ4 ignores a loose replacement that is older than its original texture, so
an out-of-date DDS cannot hide a newer edit. Rebuild or re-save a DDS after
changing its source image. Quake 4's own precomputed files get a few seconds'
grace for timestamp rounding.

Files inside a PK4 have no timestamp, so they count as older than any loose
file. A packed replacement is never stale against an original that is also
packed, but a loose copy of the original texture, such as an extracted `.tga`
left in `q4base`, `baseoq4` or the mod's folder, makes it look out of date.
openQ4 then loads the loose original, uncompressed, which can multiply a map's
texture memory and hide the pack. With `image_showPrecompressedTextures 1` the
log names each one as `Ignoring stale DDS replacement`. Move loose texture
folders out of the game's folders when you install a packed texture pack.

## Troubleshooting

Enter these in the console to check a pack:

```text
image_usePrecompressedTextures 1
image_showPrecompressedTextures 1
reloadImages all
```

The log then shows the DDS file chosen for each replacement. For every image
where nothing was chosen, it lists each path it tried and why that path was
rejected, which is the quickest way to find out what a file should be called:

```text
no DDS replacement for 'textures/stone/wall01':
   dds/textures/stone/wall01.dds       [dds/ (dhewm3 naming)] no such file
   textures/stone/wall01.dds           [alongside the original asset (Quake 4 style)] no such file
```

- `listImages` marks which loaded images are BC7.
- `imageDDSSelfTest` checks openQ4's built-in DDS naming and BC7 header
  handling, if you suspect the engine rather than your pack.
- If nothing is replaced at all, check `gfxInfo` for BC7 support and that
  `image_usePrecompressedTextures` is not `0`.
- If a map takes minutes to load, or Vulkan warns that textures are being
  reduced to fit video memory, the pack is probably loading uncompressed:
  `image_usePrecompressedTextures` is `0`, or the pack has no DDS files.
  Uncompressed textures need four or more times the memory. See
  [when textures don't fit](display-settings.md#when-textures-dont-fit-in-video-memory).
- If menus show raw `#str_` codes while the pack is loaded, the mod's folder
  holds a copy of openQ4's own files from an older build. The usual culprit is
  openQ4's `pak0.pk4`, `pak1.pk4` or `pak2.pk4` copied in next to the pack;
  less often, one of the pack's own PK4s carries a `strings/*_openq4.lang`
  file. Delete those copies. openQ4's packs belong only in `baseoq4/`, and a
  texture pack doesn't need any of openQ4's files. Releases after 0.14.1 skip
  a copy of openQ4's `pak0.pk4` found in a mod's folder and read a mod's
  `*_openq4.lang` on top of openQ4's own table, so newer text still shows.
