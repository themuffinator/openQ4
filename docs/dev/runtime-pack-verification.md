# Runtime Pack Verification

openQ4 ships its own runtime content in the packs listed in
`tools/build/content_packs.json`:

| Pack | Contents | Audience |
| --- | --- | --- |
| `baseoq4/pak0.pk4` | Core runtime: GLSL programs, fonts, strings, menus, configs, gameplay scripts, the `liquid_lab` map | Every client and server |
| `baseoq4/pak1.pk4` | Light grids: `.lightgridpack`, `.lightgrid` and atlas images | Clients only |
| `baseoq4/pak2.pk4` | Loadscreens | Clients only |

The build packs each one deterministically and compiles a table of their
names, MD5 digests, sizes and audiences into the client and the dedicated
server (`OPENQ4_PACK_TABLE_ENTRIES` and the `OPENQ4_PAKn_*` macros in the
generated `openq4_paks_generated.h`). At every filesystem startup
`idFileSystemLocal::ValidateOpenQ4Paks` (`src/framework/FileSystem.cpp`) proves
every pack in that table against its digest. The check runs when `fs_game` or
`fs_game_base` is `baseoq4`, or when a mod runs over the openQ4 runtime underlay
(`UsesOpenQ4RuntimeUnderlay`); a mod that names another base directory checks
no openQ4 packs, as before.

## When a pack is missing or does not match

A missing or modified **core pack** stops startup at once, in English, because
pak0 holds the string tables:

```text
openQ4 runtime content packs in 'baseoq4' are missing or modified.
```

followed by the reason (`missing baseoq4/pak0.pk4, core runtime content
(expected checksum ...)`, `checksum mismatch for baseoq4/pak0.pk4, core runtime
content (expected ..., got ... from ...)` or `could not read ... for checksum
validation`) and the version and release page to reinstall from.

A missing or modified **client-only pack** is reported once the string tables
have loaded (`FS_RaiseDeferredContentPackFailure`, called by `InitGame` after the
startup language load), in the player's language: which pack is wrong, what it
holds, and that it comes with the full package or on its own from the release
page under the same name (`#str_231700` to `#str_231707` in every
`*_openq4.lang`). The English detail lines follow for support. Nothing between
filesystem startup and the language load reads those packs. A later filesystem
restart reports such a failure at once.

The **dedicated server** treats client-only packs as optional: a server without
pak1 and pak2 logs `Client-only pk4 baseoq4/pak1.pk4 (light grids) is not
installed; this server does not need it` and starts, and a stale copy is only a
warning.

## Pure servers

`IsOpenQ4PurePack` keeps the core pack `PURE_ALWAYS`. Client-only packs are
`PURE_NEVER` (`IsOpenQ4ClientOnlyPack` in `GetPackStatus`): they never enter a
pure list, so a server that does not carry them does not hide them, and a
client keeps its own loadscreens and light grids on a pure server.
`tools/tests/content_pack_split_smoke.py` hosts `mp/q4dm1` on a dedicated
server that has only pak0 and joins it with a client that has all three packs;
the client's `path` shows pak0 on the pure list and pak1 and pak2 off it, and
the map is lit from its packed light grid.
`tools/tests/content_pack_status_smoke.py` checks the localized report for a
missing loadscreen pack (German), a stale light-grid pack (French) and the
English report for a missing core pack.

## Why it was slow

Before the split (PKG-02) pak1 was about 800 MB. Hashing it ran at MD5 speed,
about 600 MB/s on one core, so it cost about 1.3 s of every launch with a warm
disk cache (more from a cold cache, more again in a debug build) before anything
else could start. A filesystem restart paid it again. The light-grid and
loadscreen packs together are still about 800 MB, so the record below matters
as much as before.

## The verified-pack record

A pack that passes is recorded in `generated/packs/verified.txt` below the
active game directory under `fs_cachepath` (which is `fs_savepath` unless a
host sets it). A later startup that finds the same identity for the same
path, recorded with the digest this executable expects, does not read the pack
again. The identity is:

| Field | Windows | Linux, macOS |
| --- | --- | --- |
| size | `GetFileInformationByHandle` | `fstat` `st_size` |
| last write | `FileBasicInfo` `LastWriteTime` | `st_mtim` (`st_mtimespec` on macOS) |
| last change | `FileBasicInfo` `ChangeTime` | `st_ctim` (`st_ctimespec` on macOS) |
| volume | volume serial number | not used |
| file id | file index | inode |
| zip directory | `pack_t::checksum` (MD4 over the central directory's CRCs) and entry count, both already computed when the pack was loaded | same |

The device number is left out on Linux and macOS because btrfs subvolumes and
network mounts receive a new one at each mount; the path and inode already
identify the file. The stamp is read through the same narrow-path `fopen` that
loaded the pack, so it describes the file the engine actually uses.

The record is a header line followed by up to 16 lines, newest first:

```text
openQ4 verified runtime packs 1
<md5> <size> <write> <change> <volume> <file id> <zip checksum> <zip entries> <path>
```

All numbers are lower-case hexadecimal and the path runs to the end of the
line. The 16 entries let builddir, the staged `.install` package and test
runtimes share one save path. A pass is recorded only when the pack's identity
was the same before and after it was hashed. The record is written to a staged
file named after the process and promoted with `PromoteFile`, so concurrent
startups never read half a record. A malformed or foreign record, a record
from another format version, an entry recorded with another build's digest and
a pack whose identity cannot be read all fall back to hashing. Only a hash can
reject a pack, so every refusal still comes from the real digest.

## What still fails

- **A missing pack** fails before the record is consulted.
- **An edited pack** fails. Editing a file always moves its change time (on
  POSIX `st_ctime` cannot be set from user space; on Windows `SetFileTime`, used
  by tools that restore a write time, moves `ChangeTime` too), so the pack is
  hashed and its digest no longer matches. Rewriting it with an archive tool
  also changes its size, write time, file id and central-directory CRCs.
- **A replaced or copied pack** (another build's pack, a partial copy) has a
  new file id and change time and is hashed.
- **A touched or re-staged pack** is hashed once and recorded again if it still
  matches. `meson install` copies the packs anew, so the first launch after a
  stage hashes them.

The record is a cache with the trust model of `make` and `git`, not a
signature. Someone who rewrites a pack and then forges every part of its
identity, including the Windows change time through the native API, can get
past it; they could equally rewrite the record or the executable. The check
exists to catch stale, partial, mixed or hand-edited installs, and it still
does.

## Controls and log lines

- `fs_cacheVerifiedPaks 1` (default, startup-only) uses the record.
  `+set fs_cacheVerifiedPaks 0` hashes both packs at every startup and neither
  reads nor writes the record. Deleting `generated/packs/verified.txt` forces one
  full check at the next startup.
- The log says which path each pack took:
  - `Verified pk4 <path>: unchanged since its checksum was recorded`
  - `Verified pk4 <path>: hashed in <n> ms`
  - `Could not record the verified pk4 checksums in <path>; the packs will be hashed again at the next startup`
    when the cache path is read-only or full. Startup continues; it only loses
    the shortcut.

The dedicated server runs the same code.

## Evidence

Measured on 2026-10-09 on the Windows x64 development machine (NVMe SSD,
other sessions' work in the background). Both builds came from the debug
`builddir` at 69cabb80 plus the same working tree, one without the record
and one with it, and ran from `.tmp/runtime/baseline` and
`.tmp/runtime/candidate` with identical modules and packs (pak1 one file
hard-linked into both). Each launch was a hidden 1280x720 OpenGL client with a
warm disk cache and no input, timed to its first title frame.

Interleaved A/B, one warm-up per side on a fresh save path, then five ABBA
pairs:

| | Hashing every launch | With the record |
| --- | ---: | ---: |
| First title frame, median | 15.03 s | 11.89 s |
| First title frame, each run | 16.53, 14.59, 14.22, 16.36, 15.03 s | 11.89, 12.45, 12.15, 11.81, 11.49 s |
| Pack checks (pak1 loaded to checks done), median | 1,860 ms | 4 ms |
| Decl init, median | 1,616 ms | 1,112 ms |
| Renderer init to BSE, median | 2,748 ms | 2,186 ms |

The title came 3.5 s sooner per pair (median; 2.1 to 4.6 s, every pair
faster). The checks account for 1.86 s of it. Decl and renderer init also
ran about 0.5 s faster each without the 800 MB read, most likely because the
read had pushed other files out of the cache on a machine short of memory
(only `FileSystem.cpp` differs between the builds); every later phase matched
within 50 ms. The candidate's warm-up, with no record yet, hashed pak0 in 22 ms and
pak1 in 1,801 ms and wrote the record, the cost every launch paid before. The
baseline's own warm-up, the first launch to read pak1 through its new link,
spent 32.4 s in the checks; its later launches settled at 1.8 to 2.0 s. The
debug build hashes about a third slower than an optimized one: the engine's
`MD5_FileChecksum` compiled with MSVC `/O2` took 1,339 ms over pak1 (fastest of
five runs), so an optimized build saves about 1.3 s plus the cache effect.

Functional runs on the candidate runtime (19 of 19 checks passed; fatal
paths ran under cdb, which ended the process at `Sys_Error` so no window
appeared):

- A fresh save path hashes both packs and writes a record of two entries; the
  next launch reads neither pack.
- `+set fs_cacheVerifiedPaks 0` hashes both packs and leaves the record
  untouched.
- Touching pak0 sends it alone back to hashing, records it again, and the
  launch after that trusts it.
- pak0 with one byte of member data flipped in place, its size and write time
  put back, fails with "openQ4 runtime content packs in 'baseoq4' are missing
  or modified." and `checksum mismatch for baseoq4/pak0.pk4`, twice in a row,
  while pak1 still comes from the record.
- A missing pak1 fails with `missing baseoq4/pak1.pk4 (expected checksum
  ...)` although the record lists it.
- A record of garbage is ignored, both packs are hashed, and the record is
  rewritten.
- The dedicated server hashes and records on its first start and reads
  neither pack on its second.

On NTFS the change time moves both when a file is written and when its write
time is set back afterwards (an in-place edit followed by a PowerShell
`LastWriteTime` restore left `ChangeTime` later than both), which is why the
edited pack above was caught.

## Tests

`tools/tests/openq4_pure_pack.py` pins the identity fields, the record location
and publication, and the order of the checks. It also compiles the real record
helpers and `ValidateOpenQ4Paks` against a scripted disk and checks:

- the first startup hashes both packs and records them, and the next one reads
  neither;
- each identity field, changed alone, sends that pack back to hashing, and an
  edited pack fails with both digests named, even with its write time put back;
- a pack recorded with another build's digest is hashed, and the record cannot
  vouch for a digest the build does not expect;
- `fs_cacheVerifiedPaks 0`, a pack without a readable identity, a pack that
  changes while it is hashed, a missing pack, an unreadable pack, a read-only
  cache path and the mod underlay rules;
- record parsing: round trips, other versions, CRLF lines, truncated lines,
  malformed fields, 64-bit overflow and the 16-entry bound.
