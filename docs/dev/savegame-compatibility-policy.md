# Savegame Compatibility Policy

> **Development status:** This document describes the current source tree and its evidence, not the contents of the latest available 0.13.2 package. The 0.14.0 release was withdrawn. See [release status](../user/release-status.md).

This policy defines which openQ4 single-player saves the current runtime may
load. Reliability mechanisms and implementation detail are documented in
[Savegame Reliability](savegame-reliability.md).

## Current Policy at a Glance

| Save payload | Current decision |
| --- | --- |
| Version 4, exact wire-ABI stamp, build at or above the verified floor, valid integrity/footer | Current writer and reader; persistent Awakening status |
| Version 3, exact wire-ABI stamp, layout build at or above the verified floor, valid integrity/footer | Supported format path; build/source drift is diagnostic only |
| Version 3 stamped build 1 by a listed release (0.10.0–0.13.0) | Read as that release's build (see below) |
| Version 3, layout build below the verified floor (including an unlisted build-1 snapshot) | Rejected before map teardown |
| Version 3, different wire-ABI stamp | Rejected before map teardown |
| Version 2 (0.6.95–0.9.0), build 546 or later, Windows x64 runtime | Read as the layout it shipped (see [Version 2](#version-2)) |
| Version 2 before build 546, or on another platform | Rejected before map teardown |
| Unstamped legacy payload (0.6.92 and earlier) | Accepted only when its marker equals the current build and the runtime stamp is Windows/MSVC x64 little-endian `raw1`; in practice no current build writes one, so every unstamped save is refused |
| Empty, negative-length, or over-512-MiB `.save` | Rejected before header or CRC preflight |
| Payload version newer than 4 | Rejected; no forward-compatibility guessing |
| Multiplayer/dedicated save | Not a supported player-facing feature |

The outer session-header reader accepts outer versions `1834`, `0`, and `1` so
that the payload can be examined. Passing that outer check does not override the
payload policy in this document.

## Version 4 and the Version 3 Decoder

Version 4 is the only format written by current builds. Version 3 retains its
bounded decoder and verified build floor. The complete file must be
no larger than 512 MiB, including its integrity trailer, and the payload wire-ABI
stamp must exactly equal the running build's stamp:

```text
<os>-<compiler-abi>-<architecture>-<endian>-raw1
```

Examples include `windows-msvcabi-x64-le-raw1`,
`linux-itaniumabi-x64-le-raw1`, and `macos-itaniumabi-arm64-le-raw1`.

Version 4 adds two typed floats to both game libraries' animator records: the
base playback multiplier and a separate temporary slowdown multiplier. The SP
AI record additionally stores freeze amount, freeze start time (`-1` means not
solid), hit location, freeze and underlying materials, Retch owner, damage
multipliers and buff material. Native raw structures, the `raw1` stamp, footer
version 1 and integrity version 1 are unchanged. Existing packed flags and typed
fields use the v3 encoding in both v3 and v4 readers.

The v3 decoder consumes none of those new fields. A living AI with the old solid
ice overlay resumes a fresh four-second shatter timer: the original timer was
never recorded. Partial chill thaws and its current animation rates are restored.
Retch ownership and effects are rebuilt after every saved entity has restored.
The v4 reader instead preserves the original timer, layered effects and animation
rates. This is a bounded old-format recovery, not an exported v3 save option.

Within v3, build number, generated source hash, and source-file count are recorded
for diagnosis but do not reject a save. The policy assumes every wire-incompatible
change bumps the gameplay compatibility version. A v3 save from another build is
therefore eligible only when the exact ABI stamp matches and both builds correctly
honored the v3 schema.

Eligibility is not runtime certification. For example, a Linux arm64 v3 save may
be format-eligible on another Linux arm64 `raw1` build, but it is supported for a
release only when that platform has matching candidate runtime evidence.

Released 0.10.0 used version 3 before the two player liquid-state fields were
added, and like every release through 0.13.0 it stamped build 1. Its snapshot
(`19351be3…`, 404 files) is listed with the other build-1 releases (see
[Releases That Stamped Build 1](#releases-that-stamped-build-1)), so it is read as
build 657, and the per-field thresholds give `idPhysics_Player::swimSpeed` and
`idPlayer::nextLiquidSurfaceSoundTime` safe defaults. A second v0.10 release-build defect came
from MSVC identical-code folding: the former pointer-address comparison could
omit the empty `idPhysics` class frame. Current readers use the surrounding sync
sequence to accept that known omission, while current writers derive class-frame
ownership from source declarations so optimization can no longer alter the wire
format.

## Version 2

Version 2 was written by 0.6.95, 0.8.0 and 0.9.0 (builds 545-639). It carries a
source snapshot, sync markers and a footer, but no wire-ABI stamp and no
integrity trailer. It is read again, on the Windows x64 `raw1` path only (the
layout it shipped; a v2 save from another platform cannot identify itself), from
build 546: that build, the 0.6.95 tag (`22c7c52f`), added four fields to every
saved sound channel, so the two pre-546 development builds are refused. Every v2
payload stamped build 1 is a release from 0.6.95 on and is read as build 546.

A serializer comparison of `openQ4-game` from `fef4fcf` (the first v2 writer) to
`550271b` (0.9.0) shows no writer change within v2, and from 0.9.0 to the first
v3 commit only typed replacements behind `version >= 3` readers, plus the liquid
state that v3 introduced: six `idAI` fields and four `idPlayer` fields, which a v2
payload now leaves at their spawned defaults. The engine's GUI and sound writers
are byte-identical across that boundary.

An earlier attempt claimed v2 through an allowlist of `(build, source SHA-256,
source-file count)` tuples; on 2026-08-31 all five testable tuples desynced
after map teardown, and the allowlist was removed. Those failures came from the
liquid fields above and from the GUI problems the later sections describe
(transition interpolators, the HUD revision, non-finite GUI values), not from the
v2 layout itself. Measured 2026-10-16, all into gameplay: five saves written by
0.9.0 rebuilt from its tag and `openQ4-game` `550271b` (snapshot `0c27fa5c…`,
identical to the release's), the same five stamped build 1 as the release
stamped them, and a development build 614 save of the same snapshot. No save from
the 0.6.95 or 0.8.0 binaries was available; their game layout is the same.

## Releases That Stamped Build 1

The build number selects how fields added within a version are read, and the
build floor is judged against it. Releases up to and including 0.13.0 were built
from a shallow checkout, which stamped every save with build 1. Those saves are
recognised by the source snapshot they carry and read as the build their release
tag carries: the engine commit count, which a full checkout stamps and which
0.13.1 (858) and later releases carry themselves. `src/framework/SaveGameReleases.h`
holds the one table that the engine preflight and both game libraries read:

| Release | Source snapshot | Files | Read as build |
| --- | --- | ---: | ---: |
| 0.10.0 | `19351be3…` | 404 | 657 |
| 0.11.0 | `484e95b8…` | 404 | 693 |
| 0.12.0 | `f4e6e8c8…` | 404 | 719 |
| 0.13.0 | `6703f7b6…` | 404 | 853 |

Each snapshot is the stamp generator applied to the release tag and the
`openQ4-game` commit its release workflow packaged; `savegame_v3_contract.py`
checks every listed build against its tag's commit count where the tags are
present. The table is closed: every later release stamps its own build. The
layout build is a reading of the payload, not a new claim: a build-1 payload
whose snapshot is not listed stays at build 1 and is refused by the floor.

A serializer comparison of the release trees shows what this recovers. The
first v3 build (640) and 0.10.0 share one layout; 0.11.0 adds
`idPhysics_Player::swimSpeed`; 0.11.0 and 0.12.0 are identical; 0.13.0 adds
`idPlayer::nextLiquidSurfaceSoundTime`. Read as their builds, the per-field
thresholds below decode every one of them.

## The Verified Build Floor

Within version 3, support is expressed as the oldest build whose layout a
verified decoder covers (`SESSION_OPENQ4_SAVEGAME_MINIMUM_SUPPORTED_BUILD`,
currently 640, where version 3 began). Payloads whose layout build is below it are
refused during preflight.

The floor was 661 until release identification existed. The sample behind it,
recorded as "the released v0.10 payload (build 1)", was a 0.12.0 save (snapshot
`f4e6e8c8…`): read as build 1 it skipped a `swimSpeed` its file contains and
desynced. Read as build 719 it restores. Measured 2026-10-15 on Windows x64, all
into gameplay: the three real 0.12.0 saves on hand, and 0.10.0, 0.11.0 and 0.13.0
equivalents made by re-stamping real saves of identical layout (a 0.12.0 save
re-stamped as 0.11.0; the same with `swimSpeed` removed as 0.10.0; a dev build
845 save, whose snapshot equals 0.13.0's, re-stamped as build 1). No save written
by the 0.10.0, 0.11.0 or 0.13.0 binaries themselves was available.

The [21 October original-writer corpus](savegame-reliability.md#original-release-writers-21-october-2026)
supersedes that evidence-availability limit for Windows x64. Five map-start
saves from each of the published 0.6.95, 0.8.0, 0.10.0 and 0.11.0 binaries pass
current restore/resave/reload. Five completed v0.13.0 staged payloads also pass;
that original release rejects its own build-1 save transaction, and the record
keeps debugger-captured staging distinct from committed saves. This adds
runtime evidence without broadening the ABI or format policy.

Version 2 has its own floor, build 546 (see [Version 2](#version-2)). Lowering
either floor requires a real save in that format that restores cleanly, not a
layout argument alone.

## Fields Added Within A Version

A field added to a saved class without a version bump splits the format in two
even though both halves report the same version. Each such field therefore
carries the build that introduced it, and the reader consults that build rather
than a snapshot identity:

| Field | Added by | Build |
| --- | --- | ---: |
| `idPhysics_Player::swimSpeed` | openQ4-game `2cc5a61`, 2026-08-13 | 661 |
| `idPlayer::nextLiquidSurfaceSoundTime` | openQ4-game `d06a09d`, 2026-08-19 | 721 |
| `idExtrapolate::cacheValid`, inside raw `jointMod_t` records and GUI transition interpolators | `f15a41f03`, 2026-10-14 (v4) | 1582 |

The first two landed six days apart, so a save can legitimately carry the first and
not the second. A single boolean gated both against one hard-coded v0.10 tuple,
which answered "present" for every other v3 payload: every save written between
those builds read a field its file does not contain and desynced the rest of the
restore. Adding a field this way is still discouraged -- a version bump is
clearer -- but when it happens the threshold must be recorded here and in both
game trees.

The third is the same trap one level down: an idlib member added for an
unrelated fix. The animator's joint modifiers and every GUI transition save their
`idInterpolateAccelDecelLinear` as raw bytes, so the extrapolator's new flag grew
each record by a word. Every earlier payload, 0.14.000's included (build 1574),
holds the shorter record; the current reader failed them with a joint-count or
GUI string error after the map had been torn down. Both readers now read a
payload whose layout build predates 1582 through `openQ4SaveGameLegacyInterpolate_t`
(`SaveGameReleases.h`), a mirror of the earlier layout. The engine's GUI reader
learns the payload's layout build from the session, which holds it for exactly
the duration of the game restore, and applies it only to that file. A change to
any type reachable from a raw save record is a format change.

## GUI Content Revisions

A GUI's saved state follows the structure of the `.gui` file that built it:
windows, registers and script parameters are written in parse order with no
counts or lengths, so editing a GUI that saves leaves earlier records in the
previous structure, and reading one against the edited file desyncs. The
restore cannot bound or skip such a record on its own.

Each repo-authored in-game GUI whose edit changed its saved structure keeps the
revision earlier builds saved under `guis/savecompat/`, listed in
`SaveGameReleases.h` with the first build of the current revision and a window
only the current revision has:

| GUI | Current from | Saved definition | Current-revision window |
| --- | ---: | --- | --- |
| `guis/hud.gui` | 1562 | `guis/savecompat/hud_r1.gui` | `hit_indicator_reduced` |
| `guis/hud_strogg.gui` | 1562 | `guis/savecompat/hud_strogg_r1.gui` | `hit_indicator_reduced` |

A record from a payload whose layout build predates the revision, and whose blob
does not name that window, is read through `idUserInterface::ReadFromSaveGameWithLayout`.
The engine parses the saved definition twice in private: one copy consumes the
record with every normal check, the other shows what that definition starts with.
The state dictionary, interaction flags and cursor carry over to the current GUI,
and so does every window value the game had changed from the saved definition
wherever the current GUI has a window and variable of the same name and type: a
boss bar a script made visible, and its health width, survive. Each value carries
its evaluation flag too. A script `set` clears that flag so the variable stops
following its expression, and the saved flag decides, even where the value equals
the definition's: a HUD panel a script hid stays hidden through later GUI updates,
while a variable still following its expression keeps doing so. Running
transitions and timeline progress restart. Both copies are then freed. The window check matters because a
build of uncommitted work can carry the edit under an earlier build number (the
build-1561 saves on hand do). GUI records hold no sync markers, so the next one
bounds the search. The saved definitions are copies of `38bdc6872^` and must not
be edited. A future edit to a GUI that saves adds a row and a saved definition,
or avoids changing the saved structure.

## Non-Finite GUI Values

A GUI variable can hold a non-finite expression result. Since `40922f767`
(2026-09-01) writers save zero for it; earlier ones saved the value itself. The
reader errored on it, which failed every 0.10.0–0.12.0 save with such a value in
the objectives GUI. A non-finite GUI variable component is still a whole float in
step with the stream, so the reader now zeroes it as the writer does. Window
layout fields keep rejecting non-finite values.

## Build Numbers Are Not Unique Across Branches

A build number is the engine commit count, and merged side branches reuse
counts: build 1582 is `f15a41f03` on `main` and also a side-branch commit from
2026-10-08. Every threshold above is a property of `main`, where releases come
from. A development build of a side branch, or of uncommitted work, can stamp a
build whose layout does not match `main`'s at that number; its saves are not
covered except where a record identifies its own layout, as the GUI revision
window does.

## Unstamped Legacy Payloads

Unstamped payloads have no source snapshot, sync sequence, footer, or integrity
trailer. They are eligible only when:

- the first payload integer equals the running `BUILD_NUMBER`; and
- the running wire ABI is exactly `windows-msvcabi-x64-le-raw1`.

This is a narrow recovery path, not a general promise to load retail Quake 4,
arbitrary old openQ4, another operating system, or another architecture. Legacy
payloads receive bounded restore checks where available but cannot gain the v3
whole-file CRC/footer retroactively.

Saves from 0.1.010 to 0.6.92 are unstamped and stay refused. Their game writers
match v2's apart from the stamp, markers and footer, but tried against eight
saves on 2026-10-16 (the release build 1, development builds 246 and 480-491)
they failed after map teardown, and three things keep them out of reach:

- saves from before the 2026-06-20 content relocation were made with a different
  script program, which only a level restart can follow (`idProgram::Restore`);
- the sound world gained `attenuatedVolume` at build 502 (`f0b51cfae`) and the
  channel fields at 546, so the sound record has three layouts in this range; and
- every release in the range stamped build 1, so a release save cannot say which
  of those layouts it holds, and with no sync markers a wrong guess is found only
  after the map is gone.

Starting the saved level afresh with the save's loadout, which the header
already carries, is the realistic remedy for these and is not implemented.

## Backward Compatibility

Backward compatibility means a newer runtime reading an older save. It is
supported only through an explicit decoder or allowlist:

- current v4 and decoded v3 on the exact ABI path at or above the verified build floor;
- version 2 from build 546 on the Windows x64 path;
- build-1 releases 0.10.0 through 0.13.0, read as their release builds;
- fields added within v3 and v4, through their recorded per-field build thresholds
  and legacy empty-physics-frame recognition;
- GUI records written for an earlier revision of their file, through its saved
  definition;
- the narrow same-build unstamped Windows x64 legacy path.

Unsupported saves fail closed before map teardown when total size, header,
version, ABI, allowlist, integrity, footer, or map preflight fails. The 512 MiB
limit applies equally to v4, v3, v2 and unstamped legacy input. No in-place
conversion or repair tool is currently provided. Players should retain a copy of
important slots before upgrading, especially when moving from an unlisted
development build.

Removing an existing approved decoder is a release-policy change and requires an
upgrade note and, where practical, a migration path.

## Forward Compatibility

Forward compatibility means an older runtime reading a save written by a newer
format. It is not promised. Unknown payload, footer, integrity, or raw-layout
versions are rejected instead of being parsed as the nearest known layout.

Version 4 saves cannot be loaded by older v3-only engines. Players who need to
return to an older executable must keep their original v3 slots. The new reader
retains a bounded v3 decoder; the writer always stamps its new layout as v4.

## Platform and Architecture Policy

The `raw1` stamp is deliberately restrictive because native-layout fields remain.
Current policy is:

- same OS ABI, compiler ABI, architecture, endian, and raw revision: format may
  be eligible;
- different operating system, compiler ABI, architecture, endian, or raw
  revision: reject;
- x64 and arm64 saves are not interchangeable;
- Windows, Linux, and macOS saves are not interchangeable;
- compilation on a platform does not prove save/load runtime support there;
- same-platform eligibility does not supersede the project's platform support
  and release evidence gates.

Cross-platform transfer can be considered only after the remaining raw inventory
is normalized, a portable wire revision is defined, fixtures are verified on each
supported ABI, and the policy is deliberately revised. It must not be inferred
from the typed fields already migrated.

## Schema Governance

The payload version is the compatibility boundary. Any change that alters the
ordered byte stream or the interpretation of a valid value must be classified
before merge.

### A payload-version bump is required when

- a field is added, removed, reordered, resized, or changes encoding;
- a list gains or loses an on-wire count or sentinel;
- an object/reference representation changes in a way an existing reader cannot
  preserve;
- native structure layout or raw-write meaning changes;
- SP and MP would otherwise interpret the same version differently; or
- a valid old payload would be consumed at a different offset.

### A payload-version bump is not normally required when

- a write is replaced by typed calls that emit byte-for-byte identical v3 data;
- a reader adds a bound that only rejects values impossible in a valid save;
- diagnostics, source hashing, lookup performance, or transaction handling change
  without changing payload bytes; or
- sidecar validation changes without changing the `.save` payload schema.

Every versioned change must update, in one change set:

1. the engine preflight constants and decoder;
2. both SP and MP GameLib constants, writers, and readers;
3. the wire-ABI/raw revision when native layout changes;
4. footer/integrity versions when those structures change;
5. corruption models, raw-write inventory, and ABI contracts;
6. this policy, reliability documentation, and player-facing upgrade notes.

The generated source hash is evidence and diagnosis for v3, not a substitute for
this versioning decision. The v0.10 decoder above is a narrowly reviewed repair
for a format that had already shipped without a required version bump; it is not
precedent for adding fields within v3.

## Failure and User-Message Policy

Compatibility failures must identify the rejected layer—total size, outer
version, payload version, ABI, v2 snapshot, legacy build, CRC, footer, or
map—without attempting a partial restore. The running map remains active for
failures caught by engine preflight. The fresh-map initialization fallback handles
an explicit `false` return from `InitFromSaveGame`, such as a changed script
program. Deeper recoverable semantic errors now unwind the scoped restore owner
before the session closes the save or releases the world. The session then stops
the partial map and opens the Load Game page with a localized failure notice, or
the classic message dialog when the retained page is unavailable. The log retains
the closest class/field boundary available. This is explicit recovery, without
staged-world rollback or automatic restart of a semantically damaged save.
Fatal/startup errors retain their normal policy. The
[recovery evidence and limits](savegame-reliability.md#recovery-after-semantic-restore-failure)
include CRC-valid damaged saves; arbitrary malformed ownership graphs remain
Q4-05 qualification work.

Unrecognized unstamped/build-1 payloads still fail preflight. A readable header
does not authorize an automatic level restart or checkpoint replacement. Any
future header-only restart must be a distinct player-requested action with its
own save-preservation checks; recognized historical release readers are unchanged.

Save slots may remain visible when their bounded outer header is readable; menu
discovery intentionally does not checksum all payloads. Selecting an incompatible
slot must produce the precise failure and leave the original file untouched.

Release notes must state any compatibility break, supported older decoder, lack
of cross-platform transfer, and any required player action. They must not describe
build success or static ABI checks as runtime save/load validation.
