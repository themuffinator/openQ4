# Savegame Reliability

> **Development status:** This document describes the current source tree and its evidence, not the contents of the latest available 0.13.2 package. The 0.14.0 release was withdrawn. See [release status](../user/release-status.md).

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

## Version 4 Layout and Integrity

Current writers emit gameplay compatibility version 4. After the outer session
header, the GameLib writes:

1. `OQ4S`, payload version 4, build number, generated source hash, source-file
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

The complete `.save`, including the 16-byte integrity trailer, is limited to
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

The v4 reader and retained v3 decoder require a matching wire ABI and a layout
build of at least 640, where v3 began. The layout build is the build a payload
carries, or for a release that stamped build 1, that release's build
(`src/framework/SaveGameReleases.h`). Build/source differences above the floor are
diagnostic, but fields added within a version still depend on historical build
thresholds: `swimSpeed` at 661, `nextLiquidSurfaceSoundTime` at 721, and the
interpolator records that grew at 1582. Payload-version discipline remains
necessary; the build count is not a substitute for an explicit future schema
revision.

Version 4 appends typed animator playback multipliers in both libraries and
Awakening freeze/buff state to SP AI records. It changes no raw-write structure,
footer or integrity-trailer encoding. The raw-write inventory below therefore
remains unchanged. The v3 decoder skips these additions and recovers solid ice
with a fresh four-second timer, thaws partial chill, and reconstructs Retch buffs
after the complete entity graph restores. The
[compatibility policy](savegame-compatibility-policy.md#version-4-and-the-version-3-decoder)
defines these fields and the upgrade boundary.

Release compilation must use complete engine Git history. A shallow checkout
incorrectly produces build 1, causing the engine to reject its own staged saves
and causing field-presence checks to describe the wrong layout. Starting with
0.13.1, stable version generation rejects incomplete history. Lowering the restore
floor alone would not repair these incorrectly stamped payloads: the released
build-1 saves of 0.10.0 through 0.13.0 are instead recognised by their source
snapshot and read as their release builds, which restores them (see
[Releases That Stamped Build 1](savegame-compatibility-policy.md#releases-that-stamped-build-1)).
The earlier snapshot-specific claims failed because they described the wrong
layouts, not because the payloads were unreadable.

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

### Restore identity and partial registration

The master entity/client tables read each pointer and spawn ID into temporary
values before publishing them. Duplicate registrations and the reserved
`ENTITYNUM_NONE` slot are rejected while the first registration stays intact.
An interrupted entry cannot leave a published pointer with an unread spawn ID.
Entity restore then validates its serialized number against that registration
before assigning it. Invalid or mismatched numbers leave the live object's
identity unchanged, so teardown can still unregister the correct slot.
Client-effect constructors may have registered before `LoadMap` clears those
tables. Those old numbers are replaceable; duplicate detection checks whether
the same pointer was already published in the saved table being read.

These checks close specific cleanup hazards, not general restore rollback.
Factory-object ownership and the explicit player recovery flow are addressed
separately below; Q4-05 still requires deeper corrupted-graph qualification.
The existing early rejection policy and supported v2/v3/v4 formats are unchanged.

Evidence recorded 18 October 2026:

- The original production read fails the compiled negative control in
  `.tmp/stage-two/save-identity-repro.log`: rejecting `INT_MIN` had already
  replaced the live object's identity. SHA-256:
  `1125c857ae3b1100d001fa97ec3e8746806bc60fe85c6b82588a27f111d08afa`.
- `tools/tests/savegame_restore_identity.py` compiles the production SP/MP
  identity reads and master-table helper. Windows and Linux pass
  bounds, mismatches, valid reads, duplicate/reserved registrations, interrupted
  entries and constructor-assigned numbers. Both final logs have SHA-256
  `3751319f76c8ceaf966673950f89ab354d034aa44df9d2d1e74c76813b7c9594`.
  Meson runs both compiled targets; they and the imported game regression suite
  pass in `.tmp/stage-two/save-identity-native-r2.log` (SHA-256
  `76b31b6d19f2f29044d6757b91aed7a5016fb75dbb255733baeba21e4128d52b`).
- `.tmp/stage-two/save-identity-source.json` binds the final source files to
  base `aa5ebbe6c1196343e000d613528c167e3ae88835`, SHA-256
  `164972a7a8d0dcf7c8475023d52d9a2f82b45ed2e5cf81883280e9c3e48dee91`.
- `.tmp/s2save-identity-r2/stock_asset_baseline_report.json` passes all four
  roles: Storage1 save/load during the pod sequence, SP demo playback, Q4DM1
  listen-server and pure client gameplay. Its exact binaries, unchanged retail
  PK4 inventory, engine logs and captures are hashed in the report, SHA-256
  `3b413ab940a4b6d1c596d0032865af522da94470c9ee3c3dfcee32b4602d14e4`.
  The helper `.tmp/stage-two/run_save_identity_baseline.py` adds a hidden
  window, disabled joystick/TTY input and hidden console to the baseline's
  windowed 1280x720 OpenGL launch. Before/after engine screenshots show the
  restored pod/world state. This is valid-save regression evidence; the malformed
  identity cases above are compiled reader tests, not a corrupted gameplay-save
  corpus or complete campaign qualification.
- The earlier `.tmp/s2save-identity/stock_asset_baseline_report.json` retains
  the failed candidate which rejected client-effect constructor registrations,
  SHA-256 `5182ff526067015d8fd882905e4858bf2fa1aae231b2dcc5da49b3fbf87e37f8`.
  Its normal-save failure prompted the constructor case above. The earlier
  209-test native pass alone did not prove valid gameplay restore.

### Ownership during an interrupted restore

`idRestoreGame` now owns its factory-created objects until the entire game
initialization succeeds. A temporary pointer hash observes `idClass` destruction
on the restoring thread. Deleting an attached object or a registered map entity
therefore clears its restore-list entry before another cleanup pass can visit it.
Success explicitly releases ownership to the game without deleting objects.

Map shutdown first clears registered entities, then deletes remaining restore
objects while script, renderer and collision dependencies still exist. Scope
unwinding also shuts down an unfinished restore. An early script incompatibility
still takes the existing fresh-map/header-loadout fallback. The ownership change
does not retain the world that was torn down; the subsequent recovery UI is
described below.

The compiled fixture executes the production ownership methods, deletion loop
and base destructor for both libraries. Six cases each run 1,000 cycles:
independent objects, parent/child destruction, deletion by another owner,
successful transfer, map rollback and exception unwinding. Nested owner scopes
are also checked. Windows and Linux pass. Removing the destruction
notification makes both cascade and external-deletion controls fail. The original
unsafe cleanup is retained in `.tmp/stage-two/save-cleanup-repro.log`, SHA-256
`fd18f0c5a304086b93824843a5389eb40d1a1735c5f9b32bc6a825565a387397`;
the missing-notification controls are in
`.tmp/stage-two/save-cleanup-mutations.log`, SHA-256
`1a5ba250ee00c0cac68263fb86bb9f0b94c4b6798de19df96137b84cea803edd`.

`tools/tests/savegame_restore_cleanup_runtime.py` exercises the staged Windows
package with a current Storage1 save and a synthetic copy whose script checksum
differs, with the outer CRC recomputed. The first probe in
`.tmp/s2save-cleanup/report.json` (SHA-256
`66f0a8f4c4ffb94b520a5e12597d94d8054aa5d0fcc8b8cbbaa09291f0c58490`)
loads the valid save, rejects the synthetic copy after creating 2,962 objects
(111 client effects), enters the existing fresh-map fallback, then loads the
valid save again. Both save hashes remain unchanged. Three engine screenshots
show the pod/world state; the fallback capture is during its initial fade.
This 18.67-second hidden-window OpenGL probe uses disabled input and sound.
It is not a deep malformed-object graph, arbitrary ownership-cycle, historical
save, audio-device or full-campaign qualification. Existing non-precache and
Storage1 `bridge_gui` warnings remain in its engine log.

The reusable probe's final run passes in 16.52 seconds at
`.tmp/s2save-cleanup-final/report.json`, SHA-256
`7758a68a68a905446b05fc11d1c9404db670bc3e830da5aaf82a1a781d601d25`.
The final native suite passes **221/221** in
`.tmp/stage-two/save-cleanup-native-final.log`, SHA-256
`a01850466b105391dbcdafbd4ff0cb2e1e9cb604f1ec4f985e655d1587952394`.
The preceding run's 220/221 result is retained: its match-shutdown fixture
needed a declaration for the new restore hook; the actual shutdown method and
existing match-evidence assertions remain tested. The complete source/log binding
against `4561169c`, with all 40 retail PK4 hashes rechecked, is
`.tmp/stage-two/save-cleanup-evidence.json`, SHA-256
`470259d315e52d5377715a98babffe087460c8cf5119eaa89bfe4133a3d847db`.

### Recovery after semantic restore failure

During the engine's scoped save load, an ordinary recoverable error now throws
before the generic error handler stops the session or closes the save. This lets
the GameLib restore owner unwind its partial map while the file, script program
and render world still exist. The session then disarms the restore context,
closes the file, stops the failed map and reports the failure in a fresh menu.
Startup, recursive and explicitly fatal error policies are unchanged. Handled
restore errors do not accumulate toward the generic rapid-error fatal threshold
or replace the OS clipboard.

The retained Load Game page selects the failed save and displays a localized
notice offering another save or a new game. The classic menu, and a retained
page that cannot open, use the existing nonblocking message dialog. The menu host
must refresh its retained home before choosing the route; otherwise a load
failure can incorrectly choose the classic dialog for one frame. All twelve
language tables include the notice and dialog title. Recovery returns failure to
the caller, so the normal successful-load path cannot close the new menu.

This is explicit recovery, not staged-world rollback. The previous map is already
gone, and no partial state is offered as playable. Preflight failures continue to
preserve the running map. The existing script-checksum mismatch path still starts
the saved level fresh. Unrecognized unstamped/build-1 formats continue to fail
preflight: their readable header alone does not justify silently restarting a
level or replacing a checkpoint. A separate player-requested header-only restart
would need its own UI and save-preservation qualification; unknown payloads are
never guessed at.

Client-entity allocation also now checks table capacity before reading the next
entry. Both a full-table hint and a scan across a full table previously accessed
one element past the array. The bounded refusal retains the existing warning and
deferred removal behavior. The compiled SP/MP regression runs 1,000 fill/refuse/
free/reuse cycles against checked storage; the pre-fix implementation fails it.

Validation on 19 October 2026 uses the complete production Common error handler,
load transaction and recovery UI method with counted services on Windows and
Linux. It checks ownership unwinding before session teardown, scope
reset, successful load, 1,000 repeated failures, a subsequent successful load,
bounded error messages, ordinary/fatal behavior and retained/classic routing.
Removing the scoped error policy reproduces premature session teardown. This
fixture tests control flow; actual object destruction is covered by the lifetime
fixture and runtime probe, not by its service doubles.

`tools/tests/savegame_restore_recovery_runtime.py` derives five fixtures from a
current Windows v4 Storage1 save: an out-of-range object index, duplicate entity
and client registrations, a wrong client-object type and an invalid free-entity
index. Each changes one integer and recomputes the outer CRC, reaching semantic
validation after teardown. Each run loads the healthy save, rejects all five
copies, captures every recovery screen through the engine, then loads the healthy
save again. All six file hashes must remain unchanged. The probe requires the
correct recovery UI after every failure, not merely a clean process exit.

The first candidate's `.tmp/s2save-recovery-retained-gl/report.json` proves the
rejections and healthy reload but does **not** qualify retained UI routing: only
two of five failures reached that page, with three classic dialogs. Its original
pass field predates the strengthened UI assertion. The failure is preserved, and
the corrected source explicitly refreshes the home before opening the load page.

The final four runtime combinations all pass, totaling twenty semantic failures
and eight successful restores. Engine screenshots show readable retained notices
and classic dialogs on both backends, followed by the restored pod/world scene.
These hidden-window 1280x720 probes disable mouse, joystick and sound; their
elapsed times are workload records, not performance comparisons.

| UI / renderer | Report under `.tmp/` | SHA-256 |
| --- | --- | --- |
| Retained / OpenGL | `s2save-recovery-retained-gl-r2/report.json` | `5da98342841f57de620b6bbbc2f6e3a156ca9b7533598ce904e1cab8bac1aeff` |
| Classic / OpenGL | `s2save-recovery-classic-gl/report.json` | `a78a29d2c206732f8f44545ff47fc6a4c14c1c4f40c284a3b6f0e3a296728dd0` |
| Retained / Vulkan | `s2save-recovery-retained-vk/report.json` | `af5fa9b39eb47a0200c50e1e6c30cbef6f046a65bc8de823fea55cb978a81b04` |
| Classic / Vulkan | `s2save-recovery-classic-vk/report.json` | `b0ddbff704fc9ef24296ccd28ae55d5fa89f320e32613acb09e0bb86ca038f00` |

The full Meson suite passes **224/224** in
`.tmp/stage-two/save-recovery-native-suite.log`, SHA-256
`72849a579f96700c675afafd2eff19ad3eb7558396acacff447b26ab0575614d`.
The source, package and log binding against `11204fab`, including a fresh check
of all forty retail PK4 hashes, is `.tmp/stage-two/save-recovery-evidence.json`,
SHA-256 `756665a955927be7fbc85ebf18dee823040aa9332294550ee4881bc3d916bacb`.
It retains the earlier UI-routing failure, Windows/Linux controls, the client
capacity reproduction and exact engine/module/content hashes for every run.

This evidence does not cover arbitrary malformed ownership cycles, every
constructor/restore field, historical formats, physical input/audio, all campaign
states or other runtime ABIs. Those limits remain open under Q4-05/Q4-07. Existing
non-precache warnings and the Storage1 `bridge_gui` script warnings remain
separate findings.

### Renderer ownership during interrupted field reads

Saved model and light handles describe whether a definition existed in the saved
world. They are never ownership of a definition in the current renderer. Entity,
actor, projectile, exploding-barrel, client-model, client-moveable, vehicle-light,
weapon-light and Strogg-hover restores now keep those integers in local variables
until the renderer returns a new handle. Both game libraries retain the original
read order, presence test and wire representation.

Previously a short handle read, a later field error or a renderer acquisition
failure could leave a saved index in the live ownership field. Cleanup would
then free an unrelated or invalid definition. This is distinct from reference
graph cleanup: even an otherwise independent object could cause it.

`tools/tests/savegame_restore_render_handles.py` reproduces the old behavior in
all nine paths in both libraries. Four smaller Restore methods are compiled
whole; the other five contribute their exact handle read/acquisition blocks,
with unrelated fields explicitly omitted. Counted renderer services reject
foreign and duplicate releases. The corrected methods pass on Windows and
Linux for absent definitions, zero/ordinary/maximum saved indices and a
failure at every read/acquisition boundary, including a read that writes before
reporting failure. For each library the positive-handle cases cover 61 distinct
boundaries. This does not claim to test all intervening fields of the larger
Restore methods or the renderer's internal allocation failure behavior.

The runtime probe's `--cases render-handles` mode decodes a bounded current v4
entity record through its render data and empty GUI slots. It changes the saved
model handle to `INT_MAX` and corrupts the immediately following `ReadRefSound`
sync marker, retaining a valid outer CRC. This reaches a failure after the saved
handle was consumed and before any new render definition can be acquired.

Both staged Windows runs pass on 19 October 2026: OpenGL in 19.23 seconds and
Vulkan in 25.03 seconds. Each captures the expected recovery page and the healthy
save's restored pod/world scene, with both save hashes unchanged. These are
hidden-window, input-disabled, silent correctness probes, not timing comparisons.
The parsed entity is `tallElevatorClip` (slot 33); its saved model handle is at
offset 377026 and the rejected sound sync is at 377030. Reports:

- `.tmp/s2save-render-handles-gl/report.json`, SHA-256
  `265b12c1c3e27f7979b229461d5dfbd55493d21c8097155e4940490607c8e890`.
- `.tmp/s2save-render-handles-vk/report.json`, SHA-256
  `0734b42019cb34e9a395947adbf0a7c84ab76eb16f1702d6cf179d6534cf7148`.

The complete native suite passes **242/242**, including eighteen registered
renderer-handle cases, in `.tmp/stage-two/save-render-handle-native-suite.log`,
SHA-256 `f5a91b427b8fa62642224afe25732ba39c2deb37ef2eb688f18c48d5e5a15660`.
The source, runtime, native reproduction and asset binding against `c28d5842` is
`.tmp/stage-two/save-render-handle-evidence.json`, SHA-256
`c1f3dfe209a9d7f75f2905d7db4b82e99c47f1ba7d1009f41d2d58b2c7d7575a`.
All forty retail PK4 hashes were rechecked. Other field/ownership graphs and
historical/platform save qualification remain outside this repair's evidence.

### Binding graphs during failed restores

Entity restore reads bind parents, team roots and team successors into temporary
references. It rejects a new edge that closes a cycle before publishing the
links, because later class restore methods may already traverse them. Once all
objects and the footer have been read, a separate pass checks root consistency,
membership and the contiguous parent-before-descendant order maintained by
`JoinTeam`, before visual updates run. Forward references remain supported and
the serialized representation is unchanged.

When a restore fails, its active ownership scopes clear every surviving entity's
binding and team links before map teardown deletes entities. Incomplete saved
backlinks therefore never reach ordinary `RemoveBinds`, `Unbind` or `QuitTeam`
traversal. The same preparation precedes direct restore-object deletion. An
already committed load has no active restore owner, so ordinary map shutdown
retains its normal team teardown behavior.

The native fixture compiles the production link reader, validator, cleanup
preparation and three teardown methods. Checked links expose loops, null links
and references to deleted entities. It covers interrupted reads, self and
multi-entity cycles, missing roots/membership, inconsistent roots, invalid binding
order and valid nested, sibling and unbound teams. Valid teams are read in reverse
order to exercise forward references, then destroyed in both orders. Its reduced
eight-entity capacity tests the same bounded algorithms; it is not a full map or
all-class destructor simulation.

The runtime fixture parses a current Storage1 entity through the complete base
record, including reference sound and static physics. CRC-valid copies introduce
a partial team followed by a PVS failure, a self bind, a self successor, a missing
root and a root without a tail. The previous staged build (`09491d7c`) loads the
healthy control, then stalls at map cleanup for the first damaged copy until the
50-second harness limit. `.tmp/s2save-bindings-before/report.json` retains that
failure and the exact previous binary hashes; only the initial gameplay capture
was produced, with no recovery page or final healthy reload.

The corrected Windows runs pass all five rejections and both healthy restores:
OpenGL in 34.39 seconds and Vulkan in 41.23 seconds. Each produces seven engine
captures, including five load pages with the failure notice and the final
restored pod/world scene. All six save files remain byte-identical and all forty
retail PK4 hashes were rechecked. These hidden, input-disabled correctness probes
are not timing comparisons. Reports and SHA-256:

- Previous failure: `.tmp/s2save-bindings-before/report.json`,
  `1a43a02188c4542be61dfa17dc765d1dc7d1f5a62dc6bdde563f38ad7c8a211d`.
- OpenGL: `.tmp/s2save-bindings-gl/report.json`,
  `1e34ddf56722f1d20143eda1be066317c0a286c138604c9533895d6d8a898c0b`.
- Vulkan: `.tmp/s2save-bindings-vk/report.json`,
  `4f78c59dd548f48316abcaa709643866996e16b86a66a60788d0bc430114c227`.

The native binding fixture passes for both libraries with Clang on Windows and
Linux. The full Windows suite passes **244/244**, including the two registered
binding tests, in `.tmp/stage-two/save-bindings-native-suite.log`, SHA-256
`ef4fffb35d9c19f8d0093a2614678239715bd5894d2240b7659bdb9a647bdb4d`.
The source/runtime/asset binding against `09491d7c` is
`.tmp/stage-two/save-bindings-evidence.json`, SHA-256
`9c8b8555f86dc9423414804125900945fde507dd58bedd3746770bdbb8e61025`.

This repair covers entity binding/team graphs. Other owning references,
constructor failures and historical/platform save qualification remain separate
Q4-05/Q4-07 work.

### Typed spawn references during restore

Ordinary object references already check their declared class. The saved spawn
IDs used by `idEntityPtr<T>` and `rvClientEntityPtr<T>` now perform an equivalent
check when the referenced generation is live. A temporary pointer record holds
the read until validation succeeds, so an interrupted integer read cannot change
the current reference. Correct subclasses, const-qualified pointers, empty slots
and expired generations retain their existing behavior; client references also
retain their existing unsigned generation interpretation. The save representation
and normal gameplay pointer lookup are unchanged.

The production-template fixture compiles both pointer readers, their validity
checks, entity lookup and spawn-ID packing in each game module. Its checked
eight-slot tables cover ordinary and client type mismatches, inherited types,
null/expired references, signed boundary values and a short read that writes
before throwing. Restoring the old unchecked readers makes the separate entity,
client and both interrupted-read controls fail. This is pointer-reader evidence,
not a simulation of every class that stores one of these references.

The current-v4 runtime fixture decodes the game header through the AI lists,
timers and subsequent dictionary boundary. It changes only `lastAIAlertActor`
at offset 131097 to the live spawn ID 12321 of `tallElevatorClip` (entity 33,
`idStaticEntity`), retaining a valid outer CRC. The previous staged build
(`3bd58aa0`) accepts that incompatible reference and produces no recovery page;
`.tmp/s2save-spawn-refs-before/report.json` records the failed rejection check.
That run exits normally; it does not establish a runtime crash.

The corrected native fixture passes 85 checks per game module with Windows
Clang 20.1.7 and Ubuntu Clang 18.1.3. All four controls fail with the old reader
restored in each module. The full Windows suite passes **246/246**, recorded in
`.tmp/stage-two/save-spawn-refs-native-suite.log`, SHA-256
`ca0bdaa30179aaec5f11a10791ea688a57802f7f1cd18e8272320d8ee9bbb817`.

Both corrected staged runtime runs reject the wrong actor class, display the
retained recovery page and reload the healthy save: OpenGL in 19.53 seconds and
Vulkan in 18.94 seconds. The recovery and final gameplay captures were inspected;
both save hashes and all forty retail PK4 hashes remain unchanged. These are hidden-window,
input-disabled correctness runs, not performance or physical-device evidence.
Reports and SHA-256:

- Previous acceptance failure: `.tmp/s2save-spawn-refs-before/report.json`,
  `09792dd85f65f5c9c2e5fbb04110c415e0394dfe17b1b23d033e94e6e284e5e3`.
- OpenGL: `.tmp/s2save-spawn-refs-gl/report.json`,
  `ccb7bbe96ca8fc3aa286446244f7f3d8029eb2df56ebb3f837edf815be7568df`.
- Vulkan: `.tmp/s2save-spawn-refs-vk/report.json`,
  `dfdb5dcf7ad360431b58c4ca1c9b89091ae9c622cd30a30af4e7bcdaeb569458`.

The source/runtime/native/asset binding against `3bd58aa0` is
`.tmp/stage-two/save-spawn-refs-evidence.json`, SHA-256
`dd77220c06c40e7a183e4c0faa8d4999bb51c50bdb660ecdcfac1eede39cb110`.

### Mover activation chains during restore

Door/platform activation chains are separate from entity physics teams. Their
saved master and successor references now stay temporary until bounded cycle
checks pass. Complete-graph validation checks each activation root and member
after all fields are restored; standalone roots and forward references remain
valid. The entity validation and failed-restore preparation hooks dispatch to the
mover overrides, which retain the base physics-team checks and clear activation
links before destruction. The save representation is unchanged.

The native mover fixture compiles the actual destructor body with checked links
and a stub physics setter, plus the production reader, validator and cleanup
dispatch. It reproduces two cyclic traversals and two references to deleted
movers with the old behavior. The corrected code covers both interrupted reads,
five cyclic inputs, five inconsistent completed graphs, reverse-order restore,
both deletion orders and standalone roots. The reduced eight-entity fixture does
not simulate the entire door or platform destructor.

The runtime fixture decodes the entity and mover prefixes of two independent
Storage1 doors, through the following physics sync boundary:
`door_typical_139` (entity 454, object 346) and `door_typical_97` (entity 495,
object 384). CRC-valid copies introduce a self successor, a later buddy-count
error after installing a partial relationship, a missing root, an unreachable
member and mutually cyclic masters. The partial case points the later-deleted
door at the earlier-deleted one, exercising the teardown ordering hazard.

Windows Clang 20.1.7 and Ubuntu Clang 18.1.3 pass the SP/MP native fixtures;
the complete Windows suite passes 248/248. The final staged Windows SP runs on
OpenGL and Vulkan each reject all five damaged copies, display the retained load
page, and restore the healthy checkpoint before and after the failures. Seven
engine captures per backend and unchanged hashes for all six save files bind
each run. The reviewed recovery and final gameplay captures show the expected
load page and Storage1 pod/world. These hidden, input-disabled runs make no
physical input or performance claim. All 40 retail PK4 hashes were reverified.

| Evidence | SHA-256 |
| --- | --- |
| `.tmp/stage-two/save-movers-evidence.json` | `16998397bdf7e92928ca3bab11fdd8be9d41604f64434a957d5e0fc59f8fff7e` |
| `.tmp/stage-two/save-movers-native-suite.log` | `247f48676761809f9593b34ce88dff52ef5845da0c229c28d606cdaa2bc6f266` |
| `.tmp/s2save-movers-gl/report.json` | `4810002a51bd3f0ca2f1ae8f18882eab8903caee667d6c46f92c53b086b360f1` |
| `.tmp/s2save-movers-vk/report.json` | `428759af77779cdc216cfc1369421b8e7eb0c750d3bcb438158ba3041547ba9a` |

The evidence record binds the changed sources, compiler/native logs, staged
runtime hashes and original asset manifest to base revision `6a0eef17`.
The native reproduction is the before-fix control; no old-runtime mover result
is claimed. An early runtime invocation was refused by the stage freshness
guard while installation was still running; both accepted runs started after
staging completed. That harness diagnostic remains in
`.tmp/stage-two/save-movers-gl.log`.

Other owning references and historical/platform save qualification remain
separate Q4-05/Q4-07 work.

### Throwing object factories

The existing scoped cleanup also recovers when a registered factory throws after
earlier objects have been constructed. The runtime fixture decodes the complete
class table and replaces `rvClientEffect` with the equally sized abstract
`rvClientEntity` at object indices 2852, 2907 and 2962 of the 2962-object Storage1
save. These are the first, middle and last client-effect entries near the end of
the table, not early/middle/late positions across the entire object list. The
abstract factory reports its existing error before any field restore begins;
the file length and outer CRC remain valid.

On source `41001b14`, Windows SP OpenGL and Vulkan each pass all three failure
cases, show the retained recovery page and restore a healthy save before and
after them. Each run exits cleanly, retains five engine captures and leaves all
four saves unchanged. Reviewed recovery and final gameplay captures show the
expected page and pod/world. This extends the unchanged 248-test native baseline
with real factory-exception coverage; it does not simulate allocation exhaustion
or qualify every constructor's partial initialization. The input-disabled,
hidden-window runs make no physical-input or performance claim.

| Evidence | SHA-256 |
| --- | --- |
| `.tmp/stage-two/save-factories-evidence.json` | `45bb765ed3e5530cb0d50b353e255efc5cb660fed18e03a8b3f27f0db654314c` |
| `.tmp/s2save-factories-gl/report.json` | `b4a7530f739b982a5f27bb4248ebebfb5fdf1d8e8dacce93d11d8fe8f4b15a26` |
| `.tmp/s2save-factories-vk/report.json` | `480dac47851b6e0ff3401f360c8896783047bcf95e6af73c6bb7e6fda4a3f005` |

### Script-thread ownership during restore

Animated entities and the frame-command runner delete their owned script threads.
A saved alias between these owners previously permitted a second delete; a
partially restored entity could also outlive its thread in the temporary object
table. Both game modules now claim owning references before publishing them,
reject duplicate claims, and detach restored owners before failed-map cleanup.
The registry then releases each surviving thread. An unrelated frame-command
thread is preserved by this detachment.

After all fields and the footer are read, owned threads must retain their manual
deletion policy. Each interpreter also validates its saved thread against the
owner installed by its constructor. The claim flag is transient: the save layout
and healthy thread execution remain unchanged.

`tools/tests/savegame_restore_threads.py` compiles the production ownership
methods, interpreter identity reader, animated destructor body and cleanup
dispatch. Its checked replacement for the destructor's thread delete diagnoses
a second delete before invoking undefined C++; it does not execute interpreters,
physics or combat-model teardown. The before-fix control fails three cleanup
cases per module. The corrected Windows Clang 20.1.7 and Ubuntu Clang 18.1.3 runs
pass duplicate/interrupted/wrong-type/null references, deletion policies,
interpreter identities and five cleanup arrangements. Removing each of the five
guards independently makes the corresponding SP/MP fixture fail. The complete
Windows suite passes 250/250.

The runtime parser reconciles all 14 thread identities and all 18 animated records
with the Storage1 class table. The six CRC-valid copies introduce two animated
owners for thread 2839, an animated owner sharing frame thread 2838, a field error
after installing an owner, another/null interpreter owner, and an automatic
deletion policy on the frame thread. The selected animated records (objects 8
and 15) originally have null owners; these are synthetic failure fixtures, not
claims of a broken authored Storage1 relationship.

Windows SP OpenGL and Vulkan each reject all six copies, show the retained load
page and restore the healthy checkpoint before and after them. Eight engine
captures per backend, unchanged hashes for all seven test saves, and an unchanged
original checkpoint bind the runs. Reviewed recovery and final gameplay captures
show the load page and Storage1 pod/world. The hidden, input-disabled tests make
no physical-input or performance claim; MP coverage is the native fixture, since
multiplayer save/load is unsupported.

| Evidence | SHA-256 |
| --- | --- |
| `.tmp/stage-two/save-threads-evidence.json` | `760233dd15b8c057c7f3e6d8ef30d91c1479e8abe0df17e0bb40ef6eaacd3992` |
| `.tmp/stage-two/save-threads-suite.log` | `8235bfff9327c7b0139839e41e55984d788fedf8ec170ff84d36ff55f6249888` |
| `.tmp/s2save-threads-gl/report.json` | `ed3d882e70863df54e0bba3fa30cbbddd4e8eac5a634a91e7b203616937795ff` |
| `.tmp/s2save-threads-vk/report.json` | `01dfe9ac719208021f18ec34eb63fd71dd12f31de16b3b83b194c6ad21ee997e` |

The record binds changed sources, compiler/native logs, staged package identities
and the existing 40-PK4 asset manifest to base revision `00e6649f`. Other owning
graphs, partial constructors and historical/platform qualification remain open
under Q4-05/Q4-07.

### Standalone object-class admission

The weapon-owner investigation found that `rvWeapon::Restore` assumes an
initialized owner, weapon definition and view/world models. It is not part of
the ordinary save table: `GameLocal::SaveGame` registers entities, script threads
and client entities only. View weapons are entities and remain supported; the
player reconstructs its separate weapon implementation. Physics is serialized
as embedded state. The SDK 1.4.2 and tagged 0.14.000 writers use the same three
families, and all eleven inspected v2/v3/v4 historical tables conform.

Both readers now reject registered classes outside those families while
validating the complete table, before calling any factory. A null-owner check
late in weapon restoration would miss its earlier initialization assumptions.
This admission check leaves valid save representations and embedded readers
unchanged; it is not evidence of a normal weapon save crashing.

`savegame_restore_classes.py` compiles production table creation, type-range
checks and local exception cleanup with an observed fixture registry. The old
reader fails twelve early/late table cases per module. Windows/Linux SP/MP pass
the corrected cases, supported base/derived families, empty/count checks and
null/throwing factories. The full Windows suite passes **252/252**.

Current Windows GL/Vulkan each reject three CRC-valid class substitutions
(`rvWeapon`, `idPhysics_Player`, `idClass`), show recovery and load the healthy
Storage1 save before and after. Five engine captures and four unchanged saves
per backend support the result. Separate GL restores pass for existing Storage1
saves from rebuilt 0.9/0.14 tags and the original 0.12 checkpoint. These preserve
the historical readers; they do not fill Q4-07's missing original-binary corpus.

`.tmp/stage-two/save-classes-evidence.json`, SHA-256
`b8c483c43d2a6465aa4b7d8dd8ccb5f9d2dd2abf3497a66b9aba9c8196f03b1e`,
binds base `da36f3e60`, changed sources, build/stage/native logs, package hashes,
the historical inventory and all three runtime reports. Input was disabled and
captures came from the engine. No physical-input, performance or cross-platform
runtime acceptance is inferred. Other owning graphs and partial-constructor
qualification remain under Q4-05.

### Serialized GUI admission and optional menu fallback

The S25 restore investigation exposed a second, independent issue: the GUI
manager treated every failed GUI creation during a loading session as a missing
serialized GUI. Save loading also prepares optional retained menus before it
reads game state. A failed optional menu therefore aborted an otherwise valid
save instead of leaving the stock screen available. The observed pause-menu
name was not present in the S25 save.

The missing-GUI check now belongs to `idRestoreGame::ReadUserInterface` in both
game libraries. It rejects a nonempty saved GUI reference immediately after
creation fails, before consuming its unframed positional state. The GUI manager
returns failure normally so optional menu callers can fall back. Save bytes and
historical GUI readers are unchanged.

`tools/tests/savegame_restore_gui.py` compiles the production SP/MP readers with
observed GUI/file stand-ins. The old readers fail the missing-definition check;
the corrected readers pass missing, empty, unique/shared, healthy, malformed
payload and historical-reader cases on Windows and Linux. The actual manager
lifecycle fixture also reproduces the old session-wide rejection and passes
optional legacy/retained failures afterward, including dedicated builds. Both
platform builds and all **259 Windows native tests** pass.

Windows OpenGL and Vulkan each load the healthy Storage1 save with a malformed
optional pause document, reject a CRC-valid missing serialized GUI, show the
retained recovery page and reload the healthy save. The isolated test mod gives
the fault document precedence without changing the staged package. Three engine
captures per backend show recovery and the restored opening scene; test-save
and original-source hashes remain unchanged. Input and sound were disabled.

`.tmp/stage-two/save-gui-boundary-evidence.json`, SHA-256
`0ae778b7ec1610750bc120b208e72c7d1da7b1c805095c2155e0cf890fb341d4`,
binds source, build, native, runtime and verified Android debug-APK identities.
It retains the initial fixture-length error, ineffective lower-priority override
and missing test-mod manifest as harness failures. The corrected APK has not yet
been installed or tested physically because ADB is disconnected. Broader
owning-graph, historical-corpus and Android gates remain open.

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

The attached `anderson.save` has the **0.10.0 writer fingerprint**, despite the
report naming 0.12.0 as the runtime being used. Reinspection on 21 October finds
payload v3/build 1, source snapshot
`19351be39d2d4077a74294c0442707ef9565fc7a2fa9af9b81e05fc9aca8b220`,
404 files: exactly the independently qualified published 0.10.0 writer below.
Release identification correctly selects layout build 657, including the omitted
physics frame and the two player fields absent from that layout. The earlier
claim here that the attachment was written by 0.12.0 was incorrect.

The [reporter confirmed loading](https://github.com/themuffinator/openQ4/issues/123#issuecomment-5434563455),
then [clarified that the GUI was stock](https://github.com/themuffinator/openQ4/issues/123#issuecomment-5434577318).
The [closure record](https://github.com/themuffinator/openQ4/issues/123#issuecomment-5544391639)
withdraws the assumption that a special HiDef GUI set was missing. Neither a
copied stock game DLL nor a GUI mismatch establishes a modified GUI asset.
Later model/cinematic failures were tracked separately under #151.

Current candidate retests preserve two distinct failures. With the supplied
rebuilt map archive, restoration stops at missing `_prelight_light_170`; that
model is absent from the supplied `.proc` but present in the retail `.proc`.
With the original retail map, restoration passes that boundary and reaches
`mon4.gui`, where saved `topstuff` child ID 2 is encountered while the parser
expects `template/squares`, child ID 6. The diagnostic now names both windows
and the parsed parent instead of attributing the parsed ID to the saved name. Both
return to the load menu with the original save unchanged; the harness later
times out because its success-only map script cannot run. The GUI identity
mismatch remains under investigation, without assuming modified reporter assets.
These are not current successful restores of the attached slot.

A diagnostic control removes only the decorative `template/squares` leaf from
an isolated copy of retail `mon4.gui`. The exact original save then restores,
saves a new slot and reloads that slot in active gameplay; both engine captures
were inspected. The original file remains byte-identical. Fresh saves from the
published 0.6.95, 0.8.0, 0.10.0 and 0.11.0 binaries all contain this window,
whereas the attachment does not. This isolates the required graph difference;
it does not establish why the reporter's saved graph differs, qualify their
original GUI bytes, or justify a general reader fallback. No replacement GUI
is shipped and the production structural checks remain intact.

The first control's startup fallback quit command ran ahead of its queued
map-check script, so its original report remains failed. The corrected runner
uses only the after-map script and bounded process timeout. Its successful
restore/resave/reload is in
`.tmp/stage-two/issue123-stock-map-omitted-square-control-r2/`;
`.tmp/stage-two/issue123-control-evidence.json` (SHA-256
`c32e8e46ee6b6f021d818d692224cce6ae25be0bd8374b825175589e22784e04`)
binds original-writer name offsets, the test-only GUI, logs, captures and runtime.

Evidence: `.tmp/stage-two/issue123-current-r1/`,
`.tmp/stage-two/issue123-stock-map-r1/` and
`.tmp/stage-two/issue123-fixtures/`. The downloaded save ZIP is SHA-256
`9309249d7943762fe0804145400c919a9959e9b962c7e6f2462d438719d767e0`;
its unmodified save is
`afb39c3dde68fa267b9720bbdfd6b8287a3fdcf8f317605237ef0df4fae6e58e`.
Attachment URLs, map/model identities, runtime hashes and the reporter's
clarification are retained beside the logs.

An independent current-source Windows x64 run created a fresh save on
`game/airdefense2`, reloaded it through `Game Map Init SaveGame`, captured an
engine-render-target screenshot in active gameplay, and exited normally. This
proves the deterministic current writer/reader round trip; it does not qualify
the attached historical slot or explain the current GUI identity mismatch.

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

The old allowlist-based v2 claim above was withdrawn on 2026-08-31 and replaced
on 2026-10-16 by a v2 reader (see the
[compatibility policy](savegame-compatibility-policy.md#version-2)). The 0.13.1
Windows candidate passes fresh manual save, quicksave, restore, and
save-after-restore in stock `game/airdefense1`.

Old-save corpus, 2026-10-15, Windows x64 debugoptimized candidate built from
`d08bf5488` plus the release, interpolator, GUI-revision and non-finite GUI value
readers. Each save was copied into an isolated save path and loaded with
`+loadGame`; "loaded" means the restore finished and two seconds of play ran
without an error:

| Saves | Result |
| --- | --- |
| Withdrawn 0.14.000, written by the rebuilt historical release (`1621aa215`) on Air Defense 1, Hangar 1, Storage 1, Tram 1B, Process 1 | 5 of 5 loaded (the unpatched build failed them) |
| 0.12.0 release saves (`%LOCALAPPDATA%`, 2026-08-26) | 3 of 3 loaded |
| 0.10.0, 0.11.0 and 0.13.0 equivalents (re-stamped real saves of identical layout) | 5 of 5 loaded |
| Development builds 691–1669, base game, including walker, tank and vehicle maps | 21 of 21 loaded |
| Development builds 1119–1561, The Awakening | 12 of 12 loaded |
| 0.9.0 (v2), written by the rebuilt release (`7a1d0c54e` with `openQ4-game` `550271b`) on the same five maps, as built (build 621) and stamped build 1 like the release | 10 of 10 loaded (2026-10-16) |
| Version 2 development build 614 (0.9.0's snapshot) | Loaded (2026-10-16) |
| Version 2 development build 544 (before the 546 sound fields) | Refused before map teardown |
| Unstamped (release build 1, builds 246 and 480-491) | Refused before map teardown; tried, they fail after teardown (see the policy) |

The Load Game page was driven the same way with engine commands
(`openq4_retainedGui focus` and `menu accept`, no device input): it listed every
save in a mixed corpus, refused an unstamped one with "This save is damaged or
comes from an incompatible version.", and loaded the 0.12.0 checkpoint into
Air Defense Bunker with the HUD restored through its earlier definition.

The harness and fixtures live under `.tmp/savecompat/` (`load_saves.py`,
`menu_check.py`, `restamp_save.py`, `make_save.py`). The in-repo
`tools/tests/savegame_restore_matrix.py` restores a set of saves with a staged
client, optionally restamped to other build numbers, never deletes anything and
refuses inputs inside its output directory; `--self-test` runs in CI.
`tools/tests/savegame_gui_layout_contract.py` pins every serialized openQ4 GUI and
the `guis/savecompat/` revisions, so editing one fails until it is classified.

GUI-revision window state, Linux x64, 2026-10-18. A v3 save from build 1109 on
`game/mcc_1`, whose map script sends `hideHud` (`set "p_health::visible" "0"` and
likewise for armor, overriding their expressions), was restored, armor was then
changed and 300 frames run, with `hudGet` reading the live HUD before and after
(`savegame_restore_matrix.py --after-load ... --expect-log ...`). Copying values
alone left both panels visible (`= 1`) from the first update, because their
evaluation flags stayed set and `p_armor`'s saved `0` equals its definition's.
Carrying the flags keeps both at `0` throughout. The same build keeps
`Bossbar::visible = 1` after 300 frames on the `game/waste` boss-fight save, and
restores the 0.14.000 build-1574 and v0.13.2 build-878 saves. At the earlier review, no save written by the
0.6.95, 0.8.0, 0.10.0, 0.11.0 or 0.13.0 binaries themselves was available, and
Linux and macOS old-save restores are untested.

### Original release writers, 21 October 2026

The published Windows x64 ZIPs for 0.6.95, 0.8.0, 0.10.0, 0.11.0 and 0.13.0
were downloaded and checked against their GitHub asset SHA-256 digests. Each
original executable, matching game module and packaged data was used unchanged
with retail Steam assets. Saves cover Air Defense 1, Hangar 1, Storage 1,
Tram 1B and Process 1, after 2.5 seconds of active simulation at map start.

| Original writer | Payload | Completed original save transactions | Current restore/resave/reload |
| --- | --- | --- | --- |
| 0.6.95 | v2/build 1, `4a868438…`, 218 files | 5/5 | 5/5 |
| 0.8.0 | v2/build 1, `914dd145…`, 218 files | 5/5 | 5/5 |
| 0.10.0 | v3/build 1, `19351be3…`, 404 files | 5/5 | 5/5 |
| 0.11.0 | v3/build 1, `484e95b8…`, 404 files | 5/5 | 5/5 |
| 0.13.0 | v3/build 1, `6703f7b6…`, 404 files | 0/5; original validator rejects build 1 | 5/5 from completed staged payloads |

The 0.13.0 binary writes and checksums its payload, then rejects its own build
number as below 661 and removes the staged files. The five ordinary failed
transactions remain recorded. A debugger breakpoint at
`Session_ValidateStagedSaveGameHeader` subsequently paused the unchanged original
executable after the checksum was flushed. The complete temporary payload was
copied, its length/CRC verified, and execution resumed through the original
rejection. Those five unmodified payloads pass current restore, resave and
reload. They are genuine original-writer **staged payloads**, not evidence that
0.13.0 successfully committed those slots. No binary, build stamp or validator
was patched, and no historical executable was rebuilt.

All 25 current restores and all 25 subsequent reloads enter the expected map,
preserve the expected entity filter and run active simulation; all 25 current
resaves succeed. The original input bytes remain unchanged. Engine capture
sheets were inspected for all five releases, including the Storage 1 opening
and Tram 1B vehicle views. The initial 0.6.95 checker wrongly required
`entityFilter=first` on every map; its reports are retained with independent
assessments against the actual map-specific expected filter and unchanged
artifacts. No favorable result was substituted for a failed game run.

The aggregate `.tmp/stage-two/historical-corpus-evidence.json`, SHA-256
`d60b0e69ee47e34b5bb06790576249e6b1225b43d66e100572717d0ceadb243a`,
binds all 75 selected runs, five failed original v0.13.0 transactions, original
ZIP/file identities, save stamps, captures and the exact candidate runtimes.
Reader runs span two recorded Windows builds; the source snapshot and each
binary hash are retained per run. This closes the missing original-binary
map-start coverage for this ABI, with the v0.13 staging limitation explicit.
It does not close Q4-07's current #123 fixture, other target ABIs, arbitrary
campaign states or Q4-02's progression checks.

Historical Linux x64/Wayland save/load evidence predates v3 and does not prove
the current candidate on physical Linux hardware. ARM64 contract/build results
are not ARM64 runtime evidence, and no current v3 macOS runtime result is claimed.
Cross-platform compilation must not be presented as cross-platform save transfer
or runtime signoff.
