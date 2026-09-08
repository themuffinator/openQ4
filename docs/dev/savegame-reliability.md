# Savegame Reliability

This document describes the current openQ4 save/load reliability model. Format
eligibility is defined separately in the
[savegame compatibility policy](savegame-compatibility-policy.md).

## Scope

Player-facing save/load is single-player only. The session refuses saves during
network play, dedicated servers cannot save, and loading switches to `game_sp`
before opening the slot. The SP and MP GameLib trees retain matching serializers
so shared gameplay code stays aligned, but that does not make multiplayer or
dedicated-server save/load a supported feature.

A save slot is a three-file set under `savegames/`:

- `<slot>.save`: outer session header plus the positional GameLib payload;
- `<slot>.txt`: bounded menu description metadata;
- `<slot>.tga`: an optional validated preview image.

The `.save` payload remains positional. Reliability comes from explicit schema
markers, bounded reads, reference validation, a whole-file integrity check, and a
recoverable commit protocol; it does not come from guessing how an incompatible
payload was intended to be laid out.

## Version 3 Layout and Integrity

Current writers emit gameplay compatibility version 3. After the outer session
header, the GameLib writes:

1. `OQ4S`, payload version 3, build number, generated source hash, source-file
   count, and the wire-ABI stamp;
2. the positional object and gameplay state, with `OQ4Y` class-boundary sequence
   markers;
3. an `OQ4F` footer containing its version, exact footer offset, saved object
   count, and sync-marker count;
4. an engine-appended 16-byte `OQ4I` integrity trailer containing its version,
   the protected length, and a CRC-32.

The CRC covers bytes `0..protectedLength-1`: the complete `.save` file from its
outer header through the GameLib footer. Only the integrity trailer itself is
excluded. Validation requires the protected length to equal the trailer offset,
the footer to occupy the end of the protected region, the footer offset to match
its physical position, and the CRC to match. This rejects truncation, inserted or
trailing bytes, and accidental modification anywhere in the protected save.

The complete `.save`, including the 16-byte v3 integrity trailer, is limited to
512 MiB. A staged writer must fit the trailer within that limit before it starts
the CRC scan or append. Load and recovery validation reject an empty, negative,
or oversized reported file length before reading its header or scanning its
payload. The subtraction-based writer check keeps its arithmetic within the
legacy signed `int` file-length interface.

CRC-32 is an accidental-corruption check, not authentication. It does not make a
save trustworthy when an attacker can rewrite both the payload and checksum, and
it provides no confidentiality. The description and preview sidecars are not
covered by the `.save` CRC; they are validated and committed separately as part
of the same slot transaction.

The current v3 reader requires a matching wire ABI and a build number of at least
661. Build/source differences above that floor are diagnostic, but two player
fields still depend on historical build thresholds: `swimSpeed` at 661 and
`nextLiquidSurfaceSoundTime` at 721. Payload-version discipline remains necessary;
the build count is not a substitute for an explicit future schema revision.

Release compilation must use complete engine Git history. A shallow checkout
incorrectly produces build 1, causing the engine to reject its own staged saves
and causing field-presence checks to describe the wrong layout. Starting with
0.13.1, stable version generation rejects incomplete history. Lowering the restore
floor alone would not repair these incorrectly stamped payloads. Older released
build-1 saves are not supported by the current reader; previous snapshot-specific
compatibility claims were withdrawn when actual restores failed.

Older releases could also serialize a different class-frame sequence depending on
link optimization. The old dispatcher compared member-function addresses, and
MSVC identical-code folding made the empty `idPhysics` and `idClass` methods
appear identical in optimized builds. Current restore code recognizes the
missing empty physics frame from the next sync-marker sequence. Current writers
record whether each class declares its own `Save` and `Restore` methods, making
the emitted frame sequence deterministic across debug and optimized link
profiles.

## Save Transaction and Crash Recovery

The old slot is not replaced while a new payload is still being constructed.
Saving uses same-directory `.tmp` staging paths and `.prev` backups:

1. Recover or roll back any interrupted earlier transaction for the slot.
2. Write and sync the description, then validate its bounded fields and slot
   identity.
3. Write and sync the `.save` header and GameLib payload.
4. Calculate and append the v3 integrity trailer, sync it, reopen the staged
   `.save`, and validate the complete header, compatibility block, CRC, and
   footer.
5. Validate and sync the preview. If the slot must have no preview, stage a
   synced `OQ4D` preview-deletion marker instead of leaving the old preview
   ambiguous.
6. Back up existing final files to `.prev`, commit description and preview state,
   and rename the payload last. The final payload rename is the commit point.
7. Remove backups; if a deletion marker was committed, remove it so the final
   slot has no `.tga`.

Failure before the commit point removes staging files and restores backups.
Recovery runs before both save and load. A valid final payload with no payload
temporary proves that the payload commit occurred, so recovery finishes cleanup.
Otherwise it restores `.prev` files, removes components created by the interrupted
attempt, and clears temporary files. The preview-deletion marker lets recovery
distinguish “the new slot intentionally has no preview” from “preview capture did
not finish,” preventing a stale image from being resurrected.

This is a practical same-filesystem recovery protocol. File contents are synced
with `fflush` plus `_commit` on Windows or `fsync` on POSIX before promotion.
Rename/removal directory metadata and the parent directory are not separately
synced, and rollback after a rename failure is best effort. The code therefore
does not claim database-style ACID behavior or a fully power-loss-atomic/durable
three-file commit across every filesystem.

The GameLib serializer closes successful writes explicitly. Its destructor does
not attempt to finish serialization: a gameplay error may unwind the save only
after map shutdown has begun, when the registered object pointers are no longer
safe to visit. This keeps the original diagnostic intact and prevents a second
access violation from masking it. `Close()` is also harmless when called again
after a completed write.

## Load Preflight Before Map Teardown

Loading first recovers an interrupted slot, then reads into temporary state. Before
`ExecuteMapChange` tears down the running map, the engine validates:

- the supported outer game name and outer header version;
- bounded map/entity-filter strings and persistent-player dictionaries;
- the v3 CRC/trailer/footer, or the exact approved older-payload policy;
- the payload version and wire ABI;
- a non-empty, normalized, safe map path; and
- that the referenced map/entity-filter combination is available.

Only after those checks pass are persistent-player data, savegame state, and the
target map committed to the session. A truncated save, failed CRC, unsupported
version/ABI, unsafe path, or missing map therefore leaves the current map intact.

This preflight does not simulate every later object restore. Class construction,
object-specific semantic validation, and cross-object attachment still occur as
the new map initializes. Those checks now fail close to the offending field, but
there is not yet a general staged-world rollback after `RestoreObjects` begins.
That remaining boundary must not be described as full preflight of every gameplay
semantic.

The load menu deliberately performs only bounded outer-header discovery so it
does not checksum every save on each refresh. A header-valid slot can remain
visible even if payload preflight later rejects it; selection then fails before
map teardown with the compatibility or integrity diagnostic.

## Bounded Data and Typed References

The restore path treats saved counts, lengths, enums, and references as untrusted.
Important top-level limits include:

| Layer | Current bound |
| --- | --- |
| Save-slot basename | 96 bytes after scrubbing |
| Complete `.save` file | 512 MiB, including the v3 integrity trailer |
| Description file | 8 KiB |
| Preview file | 64 MiB; TGA type 2/10, 1–8,192 px per axis, 24/32 bpp |
| Session and GameLib dictionaries | 16,384 key/value pairs |
| GameLib object table | `MAX_GENTITIES + MAX_CENTITIES + 4096` objects |
| Sound restore | 8,192 emitters and 8,192 total channels |
| GUI state | 16,384 entries, 64 KiB per string, 16 MiB aggregate |
| GUI tree reference traversal | 1,048,576 descendants, depth 1,024 |
| GUI transitions per window | 4,096 |

SP and MP also enforce domain-specific caps for interpreter stacks, entity/client
references, inventory/objective lists, AI actions and joints, physics contacts,
fracture shards, vehicle parts and occupants, GUI events, target histories, and
other variable-length collections. The exact cap inventory is locked by
`tools/tests/savegame_corruption_contract.py`; most subsystem caps are between
32 and 4,096 entries according to the domain.

Reference validation includes:

- object indices must be in range; every nonzero reference must resolve, and the
  restored runtime type must derive from the type expected by the receiving
  field;
- registered objects serialize by stable index. A transient runtime pointer
  outside the saved object graph is diagnosed in developer output and written
  as the null index, matching the retail serializer; this includes area-location
  references outside the registry in some stock-map states. Event targets
  receive the same restore-time type validation as other nonzero references;
- class names, object counts, sync sequences, script state, enum values, and
  variable-size structures are checked before use;
- a serialized GUI must load before its positional state is consumed;
- GUI state rejects duplicate/empty keys, embedded NULs, invalid boolean bytes,
  oversized strings, and aggregate-budget violations;
- parsed GUI registers, scripts, window names/IDs/structural flags, named events,
  child ownership, and transition target/property offsets must match the saved
  schema;
- focus, capture, and hover references must identify valid windows. V3 retains
  the four-byte field while using a tagged descendant ordinal for nested desktop
  focus/capture; approved v2 saves with ambiguous parent-local IDs are repaired
  only when a unique serialized `WIN_FOCUS` or `WIN_CAPTURE` flag identifies the
  intended descendant.

Save construction uses a pointer hash for object discovery and reference lookup,
preserving on-disk indices while avoiding repeated full object-list scans.

## Remaining Raw Serialization (`raw1`)

Many high-risk scalars, flags, vectors, references, and arrays now use explicit
typed fields, including v3 branches that retain raw v2 readers where required.
The payload is not yet architecture-neutral. Its ABI stamp ends in `raw1` because
native-layout blocks remain.

The GameLib corruption contract locks the following direct raw-write inventory in
each of the SP and MP source trees (22 calls per tree):

| Relative source | Raw writes |
| --- | ---: |
| `BrittleFracture.cpp` | 1 |
| `SecurityCamera.cpp` | 1 |
| `ai/Monster_ConvoyHover.cpp` | 2 |
| `anim/Anim_Blend.cpp` | 5 |
| `physics/Clip.cpp` | 1 |
| `physics/Physics_AF.cpp` | 2 |
| `script/Script_Interpreter.cpp` | 1 |
| `script/Script_Program.cpp` | 1 |
| `vehicle/Vehicle.cpp` | 1 |
| `vehicle/VehicleDriver.cpp` | 2 |
| `vehicle/VehicleParts.cpp` | 1 |
| `vehicle/VehiclePosition.cpp` | 5 |

Engine-owned GUI/window and interpolation aggregates also retain fixed native
layout in parts of the positional payload. The table above is the exact locked
GameLib *direct-call* inventory, not a claim that these are the only native-layout
bytes reachable through all helper functions.

Consequences:

- v3 currently requires an exact OS ABI, compiler ABI, architecture, endian, and
  raw-layout revision match;
- a Windows/MSVC x64 little-endian save is not eligible on Linux x64, macOS
  arm64, Windows arm64, or another stamp;
- fixed-width typed migrations improve safety but do not make the whole payload
  portable until the remaining inventory is removed and the ABI policy changes.

## Schema-Bump Rules

The v3 reader accepts different supported build/source snapshots when the
payload version and wire ABI match. Therefore a source edit that changes bytes or
their meaning must not rely on the source hash to reject old saves.

Use these rules for future changes:

1. Bump the gameplay compatibility version when adding, removing, reordering,
   resizing, or reinterpreting a serialized field, changing list/reference
   semantics, or otherwise making a valid current-version payload unsafe to read
   in either direction.
2. Update engine preflight plus both SP and MP GameLib readers/writers atomically.
   Preserve an explicit older-version decoder only when it is bounded, tested,
   and intentional; never probe or guess between layouts after restore starts.
3. Keep unsupported v2 payloads rejected before map teardown. Any future older
   format support needs an explicit decoder and successful real-save evidence;
   matching a build number or source tuple is insufficient.
4. Treat any raw-write inventory drift as a review failure. A byte-for-byte typed
   replacement may retain `raw1`; an ABI/layout change requires a new raw-layout
   suffix and normally a new payload version.
5. Bump the outer `SAVEGAME_VERSION` only when the outer session header changes.
   Footer or integrity-trailer layout changes require their own version update and
   a gameplay-version decision.
6. Update corruption models, ABI contracts, compatibility policy, and release
   notes with the code change.

Parser tightening that rejects values impossible in a valid existing save can
remain within a version. A new writer representation is not merely parser
tightening and requires an explicit compatibility analysis.

## Performance Characteristics

- CRC calculation is linear in `.save` size and uses a fixed 64 KiB buffer.
- A successful save performs one sequential scan to calculate the CRC and another
  during staged revalidation. Load preflight performs one checksum scan.
- Menu refresh does not checksum every payload.
- Object reference lookup uses a hash index rather than repeated linear scans.
- Count and byte caps bound individual allocations and loops. GUI descendant
  validation is bounded and follows stable child IDs.

The 512 MiB total-file cap bounds the worst-case checksum work and stays well
inside the signed `int` length/offset range used by the legacy file interfaces.
CRC work remains linear below that ceiling: a maximum-size successful v3 save may
still scan almost 512 MiB twice, and load preflight may scan it once.

The extra sequential scans, file syncs, and renames are deliberate reliability
costs. They should be measured on representative large SP saves before changing
the transaction or integrity model.

## Issue #84

The `idMoveState::Restore: invalid path length` report in issue #84 was caused by
uninitialized move-state fields in the affected GameLib, not proof by itself that
the restore cursor had drifted. Current code initializes `pathLen`, `pathArea`,
`pathTime`, and the path entries, then serializes only bounded active points.

An affected old save may already contain indeterminate data. CRC cannot repair
that data, and the restore path intentionally rejects it rather than inventing an
AI path.

## Issue #123

The attached v0.10 save in issue #123 exposed both historical v3 differences
above. Reconstructing its source snapshot reproduced the embedded SHA-256
exactly. With the targeted decoder, the original file passes the omitted physics
frame and both four-byte player-field boundaries, including the reported
`ReadTrace` offset, before reaching later state owned by the reporter's
unprovided HiDef mod GUI/assets.

An independent current-source Windows x64 run created a fresh save on
`game/airdefense2`, reloaded it through `Game Map Init SaveGame`, captured an
engine-render-target screenshot in active gameplay, and exited normally. This
proves the deterministic current writer/reader round trip; completing the
third-party-mod fixture still requires reporter-side validation with the exact
mod installation.

## Validation Evidence and Limits

The focused regression set is:

```text
python tools/tests/savegame_v3_contract.py
python tools/tests/savegame_pointer_width_safety.py
python tools/tests/savegame_corruption_contract.py
```

All three savegame contracts pass for the current tree. They cover the v3
integrity/transaction model, the exact v0.10 compatibility tuple and decoder,
source-declared class-frame ownership, total-file boundary arithmetic,
pointer-width safety, bounded and typed restore rules, raw-write inventory, and
source parity; static contracts are not runtime proof. Windows x64 client UI
translation units and both SP/MP GameLibs compile/link, while companion checks
cover the ARM64 ABI source contract and typed restored-object references.

Historical Windows x64 candidate runs on `game/airdefense1` recorded a fresh v3 save
and `Game Map Init SaveGame`, rejection of a CRC-modified copy before map
teardown, rollback/finalization recovery including preview-marker cleanup, restore
of a then-allowlisted build-614 v2 fixture, wrong-ABI rejection, and successful v3
restore when only build/source diagnostics differ. A netplay save is rejected,
and dedicated builds expose neither save nor load commands. These runs used
windowed launches and engine-render-target screenshots where visual evidence was
captured.

The old v2 compatibility claim above has since been withdrawn. The 0.13.1 Windows
candidate passes fresh manual save, quicksave, restore, and save-after-restore in
stock `game/airdefense1`; incorrectly stamped build-1 saves remain rejected.

Historical Linux x64/Wayland save/load evidence predates v3 and does not prove
the current candidate on physical Linux hardware. ARM64 contract/build results
are not ARM64 runtime evidence, and no current v3 macOS runtime result is claimed.
Cross-platform compilation must not be presented as cross-platform save transfer
or runtime signoff.
