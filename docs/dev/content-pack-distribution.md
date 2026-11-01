# Content pack distribution

> **Status:** adopted 1 November 2026 as roadmap items PKG-01 to PKG-06 under
> [desktop platforms and packaging](roadmap.md#desktop-platforms-and-packaging). The
> roadmap holds their priorities and exit conditions; this page keeps the
> measurements, constraints and design behind them. Measurements are from main at
> `f62e521ea` and the published 0.12.0 to 0.13.2 packages.

Every openQ4 release is a full download, and almost all of it is `baseoq4/pak1.pk4`, a
pack that changes in fewer than half of the releases. This page records the
measurements, the constraints in the current engine and packaging, and a strategy that
brings a typical update down from hundreds of megabytes to a few tens.

## The problem in numbers

The 0.13.2 Windows x64 zip is 721.8 MB. Read from its central directory:

| Member | Size in the zip | Share |
| --- | ---: | ---: |
| `baseoq4/pak1.pk4` | 636.6 MB | 88% |
| PDBs (client, dedicated, both game modules) | 49.5 MB | 7% |
| Executables, modules, docs and the rest | 29.9 MB | 4% |
| `baseoq4/pak0.pk4` | 5.8 MB | 1% |

The other platform packages are the same size (640 to 690 MB) because each carries
its own copy of the same pak1. The 0.13.2 packages were downloaded about 2,150 times,
roughly 1.4 TB in all.

On current main pak1 is 797.8 MB and pak0 13.4 MB, so the next full Windows package
will be about 890 MB. pak1 grew by 154 MB in 0.14 when 275 loadscreen tiles were
recaptured; the new tiles compress less well.

How often each pack's sources changed between published tags:

| Interval | pak0 files | pak1 files |
| --- | ---: | ---: |
| 0.6.5 to 0.6.7 | 7 | 0 |
| 0.6.7 to 0.6.8 | 5 | 404 (SP light grids, loadscreens) |
| 0.6.8 to 0.6.9 | 7 | 0 |
| 0.6.9 to 0.6.91, 0.6.92, 0.6.95 | 0 | 0 |
| 0.6.95 to 0.8.0 | 15 | 11 |
| 0.8.0 to 0.9.0 | 2 | 158 (SP light grids removed) |
| 0.9.0 to 0.10.0 | 81 | 0 |
| 0.10.0 to 0.11.0 | 7 | 264 (loadscreens) |
| 0.11.0 to 0.12.0 | 14 | 0 |
| 0.12.0 to 0.13.0 | 22 | 3 (`liquid_lab` map) |
| 0.13.0 to 0.13.1 | 0 | 0 |
| 0.13.1 to 0.13.2 | 9 | 0 |
| 0.13.2 to 0.14.000 (withdrawn) | 137 | 275 (loadscreens) |

pak1 changed in 6 of these 15 intervals; pak0 in 11. 0.13.0, 0.13.1 and 0.13.2 shipped
within seven days with a byte-identical pak1 (CRC `ac5db2ea`, 643,853,621 bytes, the
same in the x64 and arm64 Windows zips). A player who updated from 0.13.0 to 0.13.1
and then to 0.13.2 downloaded 1.4 GB for two updates whose changed files fit in about
80 MB each.

### What pak1 holds

| Content | Files | Packed |
| --- | ---: | ---: |
| Loadscreen tiles, `.tga` (70 sets of five tiles) | 350 | 455.4 MB |
| Loadscreen tiles, DXT1 `.dds` beside each `.tga` | 350 | 74.7 MB |
| Light-grid atlas `.tga` (`env/maps/...`) | 5,870 | 174.2 MB |
| `.lightgridpack` (49 MP maps, `airdefense2`) | 50 | 86.8 MB |
| `.lightgrid` metadata | 52 | 5.7 MB |
| `liquid_lab` map, two `convoy2b` script overrides | 5 | 0.2 MB |

Two of these rows are mostly duplicates:

- **Light-grid atlases.** 170.4 MB of the atlas TGAs belong to the 50 maps that also
  ship a `.lightgridpack`. A valid pack wins at load time and the loose files are
  documented as inspection and fallback material ([light grids](../user/light-grids.md)).
  Only `waste` (3.8 MB) has atlases and no pack.
- **Loadscreens.** Every tile is stored twice, as an uncompressed 32-bit TGA (4 MB raw;
  in a sample of six tiles the alpha byte was constant) and as a DXT1 DDS. The renderer
  prefers a DDS beside its source image when its staleness check allows
  (`R_ResolvePreferredDDSImageSource`, `src/renderer/Image_load.cpp`). Re-encoding the
  TGAs losslessly saves little: dropping the padding byte gave 6% and a horizontal
  prediction filter about 25% on the same sample.

## Constraints in the current tree

- **Digests are compiled in.** `generate_pak_header.py` writes `OPENQ4_PAK0_MD5` and
  `OPENQ4_PAK1_MD5`, and `ValidateOpenQ4Paks` (`src/framework/FileSystem.cpp`) refuses
  to start without both packs matching ([runtime pack verification](runtime-pack-verification.md)).
  The table has exactly two names, as does `IsOpenQ4PurePack`, which keeps both packs
  `PURE_ALWAYS` on pure servers. The dedicated server runs the same check, so a server
  package needs the 530 MB of loadscreens it never draws.
- **Packing is deterministic today.** `openq4_pak.py` sorts entries, stamps them
  1980-01-01 and deflates at level 9, and the identical 0.13.x pak1 shows it held across
  releases and Windows architectures. The Linux and macOS packages were not checked
  (their archives cannot be read by range). Deflate output depends on the zlib build
  behind the packing Python, so a runner-image or Python upgrade can change a pack's
  bytes with no source change. Each platform job also repacks the full 2.8 GB of loose
  pak1 sources.
- **Load order.** `FS_ComparePk4LoadOrder` loads numbered packs by number, and the
  later-loaded pack wins. Wider numerals load first, so `pak10.pk4` loads before
  `pak9.pk4` and loses to it. Every `*.pk4` in `baseoq4/` is mounted, so a retired pack
  left behind by an update is still read.
- **Packaging.** `package_nightly.py` copies `OPENQ4_PACK_NAMES` into every archive;
  the macOS app keeps both packs inside `Contents/Resources/baseoq4/`, so a signed
  bundle could not take a new pack without being replaced; `android_package.py`
  requires `pak0.pk4` and `pak1.pk4`. The Inno installer uses one solid LZMA2 stream
  over a single `[Files]` glob and has no `[InstallDelete]` section. The AppImage is
  built with `appimagetool` and no update information.
- **No updater.** The engine has no HTTP client (`ID_ENABLE_CURL` is 0 and there is no
  curl dependency). The legacy version check consumes the master server's reply but
  only ever opens the compile-time `PROJECT_RELEASES_URL`; network-supplied URLs are
  inert by design. Any update path must keep that rule: what to download, and the
  digest it must have, comes from the binary or a signed manifest, never from a
  datagram.

## Strategy

Six rules, roughly in order of payoff for effort.

1. **Separate packs by how often they change.** Code-coupled content (GUIs, GLSL,
   strings, configs, gameplay scripts) changes with nearly every release and is small;
   it stays in pak0 and ships with the binaries. Baked bulk content (light grids,
   loadscreens) changes when someone deliberately rebakes it; it moves to packs with
   their own lifetimes, split again by audience so one rebake does not invalidate the
   other.
2. **A content pack is immutable and identified by its digest.** Unchanged sources must
   produce the same bytes, and a release that does not touch a pack reuses the
   published file rather than rebuilding it. A player who already has that digest
   never downloads it again, by any channel.
3. **Build each content pack once and ship the same file everywhere.** One job builds
   or fetches it; every platform package, installer and update channel carries those
   exact bytes, and release verification proves it.
4. **Ship what the runtime reads.** Release packs drop fallback copies the engine never
   loads in a correct install; development trees and tools keep them.
5. **Small content fixes go in an overlay, not a rebuild.** Between rebakes, a few
   changed files ship as a small higher-numbered pack that overrides its base. Overlays
   fold into the base at the next deliberate content generation.
6. **Let delivery channels do the diffing.** Once the bytes are stable, zsync,
   installer hash checks and patching storefronts turn stability into small downloads
   with no engine work.

### Target layout

| Pack | Contents | Changes when | Size (est.) | Carried by |
| --- | --- | --- | ---: | --- |
| `pak0.pk4` | Today's core runtime plus the `liquid_lab` map and the two script overrides now in pak1 | Most releases | 14 MB | Every package and update archive |
| `pak1.pk4` | Light grids: `.lightgridpack`, `.lightgrid`, atlases only for maps without a pack | A light-grid rebake | 96 MB | Client packages; standalone content asset |
| `pak2.pk4` | Loadscreens | A levelshot recapture or format change | 75 to 530 MB (see below) | Client packages; standalone content asset |
| `pak3.pk4` to `pak9.pk4` | Overlays of pak1 or pak2 between generations | Content fixes | A few MB | Update archives |

Single-digit numbers keep the overlays above their bases in load order. pak1 and pak2
are presentation data only, so the dedicated server can treat them as optional. That
needs a pure-server rule as well as a validation rule: a client must keep using its
loadscreens and light grids on a server that does not carry them, so client-only packs
should stay out of the server's pure list instead of being `PURE_ALWAYS`.

The engine side is a generalisation of what exists: `generate_pak_header.py` emits a
table of `{name, digest, size, client-only}`, and `ValidateOpenQ4Paks`,
`IsOpenQ4PurePack`, the packager, the archive verifiers and Android packaging read
that table instead of two hard-coded names. The compiled-in digest stays; because an
unchanged pack keeps its digest, consecutive executables accept the same file.

### Loadscreen format decision

The single largest lever, and a quality decision rather than an engineering one:

- **DDS only:** pak2 is 75 MB and the TGAs are dropped (-455 MB). Needs proof that
  every path that shows a loadscreen, at every texture-quality setting and on GL,
  Vulkan and GLES, reads the DDS, and that DXT1 at 1024x1024 is acceptable on large and
  high-DPI displays.
- **Keep a lossless source, re-encoded:** about 25% off the TGAs (roughly 415 MB pak2).
  Little gain for the work.
- **Lossy high-quality source** (for example JPEG at high quality, if the image loader
  supports it on every renderer) beside or instead of the DDS: likely 100 to 150 MB.
  Needs the same reader audit and a visual comparison.

Whichever is chosen, recapturing at higher quality later is a pak2 generation of its
own, announced and shipped once, never folded silently into a code release.

## Expected effect

Estimates for the Windows x64 package; other platforms differ by a few megabytes.

| Download | Next release, unchanged policy | With the update archive | With split, trim and overlays |
| --- | ---: | ---: | ---: |
| First install | 890 MB | 840 MB (PDBs moved to a symbols asset) | 215 MB (DDS-only loadscreens) to 560 MB |
| Update, no content change (9 of the last 15) | 890 MB | 45 MB | 45 MB |
| Update with a content fix | 890 MB | 45 MB plus the whole 798 MB pak1 | 45 MB plus an overlay of a few MB |
| Update after a deliberate rebake | 890 MB | 45 MB plus pak1 | 45 MB plus only the rebaked pack |
| Dedicated server | 890 MB | 840 MB | about 45 MB |

## Work items

The strategy is tracked in the [roadmap](roadmap.md#desktop-platforms-and-packaging),
which states each item's scope and exit condition:

| Item | Priority | Rule it delivers |
| --- | --- | --- |
| PKG-01: update archives and content-pack status | P1 | 2, 3 (no engine change) |
| PKG-02: split pak1 by change rate and audience | P1 | 1, 2, 3 |
| PKG-03: ship only what the runtime reads | P1 | 4, and the loadscreen format decision |
| PKG-04: overlay packs | P2 | 5 |
| PKG-05: delta-aware channels | P2 | 6 |
| PKG-06: in-game updater and content storage | P3 decision | Keeps the "No updater" constraint's download rule |
