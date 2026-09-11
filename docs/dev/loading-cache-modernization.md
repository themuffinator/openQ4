# Level-Load Cache Modernization

This document owns the implementation contract and promotion evidence for the
Milestone B loading/cache slice in the
[idTech 5-level modernization roadmap](idtech5-modernization-roadmap.md).
The implementation and local integration gate are complete, but final
committed-package runtime, platform, and performance evidence is **not
complete**. Nothing in this document should be read as a universal measured
speed-up or release-platform qualification: the measurements below are local
development evidence, and the explicitly open promotion gates still apply.

## Compatibility boundary

The installed Quake 4 and mod sources remain authoritative:

- Every replay source is resolved first through the normal VFS search order,
  addon and pure-PK4 rules. A learned manifest never authorizes a file.
- A ready preload may replace only the bytes behind an already authorized file
  handle, and only when generation, semantic type, normalized path,
  uncompressed size, loose-file timestamp, and containing-PK4 checksum still
  match exactly.
- The production worker stage reads complete source bytes and performs the
  `idFile_InZip` PK4 inflation reached by that read. It then validates supported
  RIFF/WAVE, Ogg, PNG, and JPEG framing and exact termination, walks the whole
  buffer for transport and SHA-256 integrity, and seals an immutable typed DTO.
  DDS and formats without a worker framing contract remain deliberately opaque.
- This worker decode is framing and integrity validation, not asset-specific
  model, world, collision, animation, declaration, image, or sound parsing. It
  does not resolve declarations, mutate live managers, or call renderer/audio
  APIs. Existing main-thread owners still perform those operations. If a
  result is absent, late, rejected, malformed, or cancelled, the owner
  continues with the ordinary VFS handle.
- Generated model, world, collision, and animation files are private derived
  data under `fs_savepath`. They do not participate in VFS search or pure-PK4
  negotiation and are always disposable.

This keeps source parsing as the correctness path and makes the entire feature
reversible without converting or modifying retail assets.

## Learned manifest identity

The first successful load learns the source files that were actually opened,
their semantic type and priority, first-use order, use count, options, and
source identity. Successful post-publication source opens continue to be
learned until the generation is closed, so demand-loaded media can be included
on the next visit. The canonical manifest merges duplicate observations and
orders replay deterministically by priority, first use, and stable identity.

A manifest is accepted only when every campaign key matches:

The current `.oqpm` manifest is format/producer version 2; earlier records are
not widened or guessed and therefore miss cleanly.

| Key | Exact input |
|---|---|
| Producer and format | Manifest producer version, format magic/version, end marker, canonical ordering, bounds, and SHA-256 integrity |
| Map | Normalized lowercase slash-separated map key, such as `maps/game/airdefense1` |
| Runtime role | Full 32-byte SHA-256 of exactly `singleplayer`, `multiplayer`, or `dedicated` |
| Entity selection | Full 32-byte SHA-256 of the active `si_entityFilter` value |
| Content/search state | `fs_game`, `fs_game_base`, the exact ordered active search list, and the active pure-PK4 checksum list |
| Load-affecting settings | SHA-256 of the versioned settings string listed below |

For each PK4 search entry, the content key includes its lowercase filename,
checksum, archive length, file count, addon-search flag, and search-order
position. Directory entries include the lowercase game directory and position;
absolute installation roots are intentionally excluded. Loose files are then
validated individually by normalized path, size, and timestamp. The manifest
intentionally does not hash every loose source byte; same-size,
timestamp-preserving edits can therefore evade this preload identity check.
Generated-cache owners that need stronger loose-source identity add a content
CRC/signature to their own key. PK4 members are validated by normalized path,
uncompressed size, and containing-PK4 checksum.

The learned-manifest settings contract is `level-load-settings-v2` followed, in
this exact order, by:

```text
com_binaryRead
r_renderer
r_actualRenderer
r_mergeModelSurfaces
r_slopVertex
r_slopTexCoord
r_slopNormal
r_useNewSkinning
r_useFastSkinning
r_forceConvertMD5R
r_convertMD5toMD5R
r_convertStaticToMD5R
r_convertProcToMD5R
r_pbrMaterials
image_downSize
image_downSizeLimit
image_downSizeSpecular
image_downSizeSpecularLimit
image_downSizeBump
image_downSizeBumpLimit
image_ignoreHighQuality
image_picmip
image_picmipFilter
image_picmipMinSize
image_usePrecompressedTextures
s_useCompression
sys_lang
```

Display-only settings are not part of this key. Generated-cache owners add
their own parser/settings signatures for representation-specific choices such
as surface merging, silhouette remapping, slop values, MD5R source/conversion
mode, deferred tangents, and collision source/geometry identity.

## Bounded replay pipeline

Replay is generation-scoped and follows this ownership sequence:

1. The main thread validates a matching manifest, applies entry/file/aggregate
   budgets, resolves each candidate through the ordinary VFS, and checks its
   current source identity before admitting it.
2. High-priority job-list items read independently opened handles in bounded
   chunks. For PK4 members, the handle inflates the member during this worker
   read. Jobs poll cooperative cancellation between chunks.
3. A complete read enters the worker framing/integrity stage. Supported
   container formats must consume their exact framed extent; every result must
   report the whole decoded byte count, match the independent read transport
   checksum, and produce nonzero SHA-256 integrity. Only then is a sealed,
   immutable DTO published with generation, type, normalized path,
   authoritative source identity, frame kind/count, payload extent, and shared
   backing bytes. Partial reads, malformed/trailing frames, allocation failures,
   cancellation, and identity mismatches never publish.
4. A later ordinary source open performs VFS and pure authorization again. If
   the exact immutable result is already ready, the filesystem closes the
   physical/PK4 handle and returns a read-only view carrying the authoritative
   name, full path, timestamp, and container checksum. It never waits for an
   unfinished item at this point.
5. The normal owner performs its format-specific parse/decode, validation,
   adoption, and renderer/audio upload work on its established main-owner path.
   Replay results remain alive through renderer, sound, declaration, BSE, and UI
   `EndLevelLoad` processing, then the session releases the generation.
6. Map failure, unload, filesystem restart, module reload, and shutdown cancel
   and join the generation before dependent state is released.

If the job service is disabled, deterministic, saturated, or unavailable, the
admitted bounded batch executes synchronously. Sources outside the budget and
all unsuccessful results continue through ordinary VFS reads.

Default replay budgets are:

| Control | Default | Allowed range | Meaning |
|---|---:|---:|---|
| `com_levelLoadPreloadMaxEntries` | `64` | 1-64 | Independently opened source files admitted per map generation; the hard cap prevents PK4 stream-handle exhaustion |
| `com_levelLoadPreloadMaxFileMB` | `128` | 1-512 MiB | Maximum uncompressed size of one source |
| `com_levelLoadPreloadMaxStagingMB` | `384` | 8-2048 MiB | Aggregate admitted staging memory |
| `com_levelLoadPreloadMaxDecodeMB` | `384` | 8-2048 MiB | Aggregate admitted immutable decoded-result residency |
| `com_levelLoadPreloadReadChunkKB` | `256` | 16-4096 KiB | Cancellation and read-accounting chunk; implementation additionally caps it at 4 MiB |
| `com_levelLoadPreloadDecodeChunkKB` | `256` | 16-4096 KiB | Cancellation and integrity-walk chunk; implementation additionally caps it at 4 MiB |

The framing stage does not transcode or duplicate the complete source buffer:
on success the DTO takes shared ownership of the staging allocation and that
allocation moves from staging accounting to decoded-result accounting. Normal
owner parsing can allocate its established live representation in addition to
the retained source buffer; those owner allocations are outside these two
pipeline budgets. The per-file and aggregate caps bound replay work, but they
are not a claim that total level-load memory is limited to 384 MiB.

The portable scheduler's independent list/job/dependency and queue caps remain
in force; see the [portable job-system contract](parallel-job-system.md).

Replay priority is learned and deterministically bounded at generation start.
There is no portal-visibility-driven live reprioritization, arbitrary background
asset streaming, or worker-side owner parsing/upload in this milestone.

## Generated-cache contract

Framework-owned `.oqgc` files use a versioned, bounded, little-endian envelope
with kind, parser version, authoritative source identity, content and settings
signatures, decoded length, payload SHA-256, whole-record integrity, end marker,
and trailing-data rejection. The runtime currently writes and accepts the
uncompressed envelope codec; the enum reservation for deflate is not a claim
that generated payload compression is enabled.

The framework opens and identifies the authoritative source before deriving or
opening a cache filename. An envelope or payload failure removes that exact
private file, records a miss/corruption when reporting is enabled, and lets the
owner parse the source. Writes use a unique staging file, flush it, then
atomically promote it over the final path.

### Render models

The model cache supports the exact static, MD5, and MD5R owner payloads selected
by their loaders. Each decoder validates bounded counts, indices, strings,
finite numeric data, surfaces, geometry, joints/weights, and type-specific
state into a detached model, rejects trailing bytes, and swaps state only after
the complete payload is valid. Unsupported model states remain on their source
loader and are not silently flattened.

### Render worlds

The world cache covers the finalized classic `.proc` CPU representation:
inline static and shadow model payloads, per-area model identity and `procSky`
state, inter-area portal windings and fade/cull data, area BSP nodes, map name,
source timestamp, and map CRC. The reader applies strict per-field and aggregate
caps, finite-coordinate and graph/index/area validation, and builds all models,
portals, and nodes off to the side before publishing and running the normal
world finalization path.

Packed MD5RProc worlds can share buffers across models. The world writer refuses
that representation rather than losing the sharing contract. A selected
MD5RProc companion, or classic proc converted through `r_convertProcToMD5R`,
therefore uses the authoritative source path with no world-cache write.

### Collision models

The collision payload stores pointer-free model vertices, edges, polygons,
brushes, BSP nodes, references, material names, source slots, and geometry CRC.
It bounds all counts and aggregate allocations, validates contents, finite
planes/bounds, indices, graph topology, reference coverage, proc-clip ordering,
and current material declarations, then constructs a detached model set. The
live collision table changes only after every staged model is adoptable.

Authored `.cmc`/`.cm` sources are preferred by the normal binary/text selection.
Map-derived collision caches retain the `.map` source identity and include the
exact `.proc` path, length, timestamp/container checksum, and loose-file CRC in
their owner settings. Any failure returns to the existing authored or
map/`.proc` build path.

### MD5 animations

SP and MP share the separate version-3 `.banim` contract owned by
`openQ4-game`. Its source identity follows the parser's exact selection: with
`com_binaryRead 1`, the compiled `<requested-name>c` source wins when present;
otherwise the requested `.md5anim` source is used. The record stores that
selected normalized path, length, timestamp, containing-PK4 checksum, and a
content CRC for loose sources. A PK4 member relies on its containing archive
checksum rather than rereading the member for an additional source CRC.

Before parsing any record field, the reader bounds the whole file and checks a
stored record length plus CRC-32 trailer. It then validates the selected source
identity, bounded frame/joint/component counts, frame-rate and duration
arithmetic, hierarchy and component ranges, finite floats, ordered bounds,
near-unit base-frame quaternions, end marker, and exact stream consumption into
private lists. Only then does it replace the live animation. Logical cache paths
are slash/lowercase normalized and reject rooted/drive, dot, empty, control,
unsafe, trailing-dot/space, and Windows device-name segments. Invalid files are
removed and the selected parser source is used. Writes use a `.tmp` file,
`Sync()`, and atomic promotion.

The v3 reader first holds the bounded on-disk record so it can verify the whole
record CRC before trusting lengths, then copies the verified record into a
read-only memory file for field parsing. Its 528 MiB record cap therefore makes
the temporary copy finite but potentially material; it is separate from the
framework preload staging/decode budgets.

## Private paths

All paths are below the active game directory under `fs_savepath` (normally
`<fs_savepath>/baseoq4/`):

| Data | Private path |
|---|---|
| Learned manifests | `generated/manifests/<sha256>.oqpm` |
| Render-model payloads | `generated/models/<sha256>.oqgc` |
| Render-world payloads | `generated/worlds/<sha256>.oqgc` |
| Collision-model payloads | `generated/collision/<sha256>.oqgc` |
| Animation payloads | `generated/animations/<normalized-source>.banim` |

The opaque names are derived from validated keys, not asset names. Do not ship
these files in PK4s or treat them as authored content.

## Controls and rollback

The experiment is now protected by a default-off archived master control. The
individual controls retain their prior values so an explicit opt-in needs only
one additional switch:

| Control | Default | Effect |
|---|---:|---|
| `com_levelLoadModernization` | `0` | Master admission gate for every framework and SP/MP animation cache/preload path |
| `com_levelLoadCache` | `1` | Enables learned manifests and generated model/world/collision cache reads |
| `com_levelLoadCacheWrite` | `1` | Enables atomic manifest and generated-cache writes |
| `com_levelLoadPreload` | `1` | Replays a matching manifest through the bounded read/framing pipeline |
| `com_levelLoadCacheReport` | `0` | Prints per-generation replay, read/decode, cancellation, memory, and generated-cache counters |

The six replay-budget controls are listed above. Animation cache reads and
writes additionally require `g_useGeneratedAnimCache 1` and
`g_writeGeneratedAnimCache 1` in both game modules. Scheduler behavior is
controlled by the startup settings documented in
[parallel-job-system.md](parallel-job-system.md).

This default was corrected on 2026-08-20 after stock MP evidence showed the
experimental path dominating a 32.3-second map load (16.5 seconds in game init
and 15.2 seconds in media finish), with repeated generated-cache publication.
The retained Milestone B cold/warm campaign proved boundedness and reuse, but
did not justify changing the player baseline: 69.3 seconds cold and 53.3 seconds
warm remained materially slower than the expected classic experience.

A controlled 2026-08-20 Vulkan `mp/q4dm9` pair used the same staged runtime,
retail base path, bordered 1280x720 mode, and isolated save roots. The new
default-off run completed the client map load in 9,449 ms (`gameInit=3,107`,
`mediaFinish=6,000`) with no generated-cache writes. Explicitly setting
`com_levelLoadModernization 1` took 21,935 ms (`gameInit=5,093`,
`mediaFinish=16,224`) and published 35 framework cache records on the client;
the paired server published 103. The opt-in cold path was therefore 132% slower
than the classic default in this case. Artifacts are retained under
`.tmp/regression-q4dm9-fixed/` and `.tmp/regression-q4dm9-cache-on/`.

The master rollback is immediate and takes precedence over older archived
individual settings:

- Keep `com_levelLoadModernization 0` for ordinary play and baseline or
  benchmark runs. No framework or animation cache is read or written.
- Set `com_levelLoadModernization 1` only for focused cache evaluation; the
  individual rollback choices below then apply.

- Set `com_levelLoadPreload 0` before loading a map to disable learned source
  preparation while retaining validated generated model/world/collision
  payloads.
- Set `com_levelLoadCacheWrite 0` to keep valid existing framework caches
  read-only and stop new framework derived data from being published.
- Set `com_levelLoadCache 0` before loading a map to use ordinary source paths
  for manifests and framework model/world/collision caches.
- Set both animation controls to `0` to force `.md5anim` source parsing without
  reading or writing `.banim` files.
- With openQ4 closed, delete any or all of the `generated/` subdirectories above
  for a clean rebuild. Never delete the original retail/mod source files.

## Evidence status

The implementation contract above is source-backed. Focused native format and
pipeline tests, Windows SP/MP and engine integration builds, workflow-contract
checks, static macOS/ARM64 policy checks, and the local Windows runtime campaigns
below pass within their stated scope. Those checks close the local
implementation gate, not the release gate. The openQ4 tree was not committed
when the evidence was captured; the companion content matches local commit
`c32a3858b30139ddb850c3cec4f1274cd93bb98c`, which was not yet published. The
staged development package is not a clean release candidate, and there has
been no Linux/macOS runtime qualification. Only the measurements explicitly
recorded below may be inferred.

### Required run identity

Record these fields for every retained campaign:

| Field | Value |
|---|---|
| openQ4 commit | _Pending_: evidence was captured from the uncommitted final-development tree |
| `openQ4-game` commit | Local `c32a3858b30139ddb850c3cec4f1274cd93bb98c`; remote publication pending |
| Build/package identity and hashes | Windows x64 staged development package; client, dedicated, OpenGL/Vulkan modules, and SP/MP GameLibs built and staged successfully. Release-candidate hashes remain pending. |
| OS, architecture, compiler, and storage | Windows x64 production build with MSVC; native format/pipeline tests also pass strict Clang C++20. Storage-device identity was not retained. |
| Renderer/backend, SP/MP/dedicated role, and map sequence | Direct windowed OpenGL 1280x720 SP `game/storage1` campaigns; two dedicated `q4dm1` loads separated by disconnect; staged stock SP load/save/reload/demo and pure auto-joined MP server/client campaign. Vulkan was built, not runtime-qualified here. |
| Retail/mod PK4 and overlay identity | Stock campaign recorded 40 retail PK4s and zero loose retail files beneath the staged openQ4 overlays. Exact package/hash binding remains part of clean-candidate promotion. |
| Cache, preload, job, binary-read, conversion, and renderer settings | Cache/preload defaults for cold/warm/corruption runs; `jobs_enable 0` for synchronous fallback; framework cache/preload and animation reads/writes disabled for rollback. Remaining exact settings are retained in the logs/reports. |
| Log/report/artifact paths | `.tmp/milestone-b-final-evidence-v2/savepaths/sp/baseoq4/logs/`; `.tmp/milestone-b-dedicated-final/baseoq4/logs/milestone_b_dedicated.log`; `.tmp/milestone-b-stock-20260820-final4/` |

### Functional and failure evidence

| Gate | Required retained evidence | Status |
|---|---|---|
| Cold learn then exact warm replay | First load writes a canonical manifest; next identical load reports a manifest match and successful source acquisitions | **Local development pass:** cold learned 5,425 entries; exact warm matched, replayed/decoded 64 entries, and recorded 68 acquisitions with no decode failure/cancellation |
| Search/PK4/source invalidation | Ordered search, pure list, PK4 checksum, loose timestamp/size, map, mode, entity filter, and settings changes each prevent stale reuse | **Partial:** exact-match replay passed, and the second dedicated load correctly missed after the exact state was deliberately changed. A retained runtime matrix for every key remains pending. |
| Corruption fallback | Truncated, bit-flipped, oversized, wrong-kind/version, wrong-owner, and trailing-byte manifest/model/world/collision/animation cases are removed or ignored and load from source | **Local focused pass:** a corrupt world cache fell back and rewrote cleanly; a corrupt animation-v3 record was rejected and atomically rebuilt; focused malformed-input tests pass. Broader owner/runtime corruption coverage remains pending. |
| Transactional publication | Failed model/world/collision/animation payloads leave no partial live owner state | **Local focused pass:** the world and animation corruption runs reached clean map completion; the rebuilt animation matched its pre-corruption SHA-256 and left no `.tmp`. Model/collision runtime fault injection remains pending. |
| Job parity | Jobs threaded, deterministic, and disabled produce matching gameplay state and engine-rendered evidence | **Partial:** the jobs-disabled warm run completed the same map through `syncFallback=1` with 64 replayed/decoded entries, 68 acquisitions, 188 generated hits, and no failure. Final jobs-on/off engine-image parity from a clean candidate remains pending. |
| Cancellation and teardown | Repeated map changes, load failure, module/filesystem restart, and shutdown cancel/join without stale publication or leaked handles | **Partial:** native read/decode cancellation tests pass, and both dedicated generations closed and joined across disconnect. Broader runtime load-failure, module/filesystem-restart, and in-flight cancellation campaigns remain pending. |
| Role/backend coverage | Required SP, pure auto-joined MP, dedicated, OpenGL, and Vulkan campaigns complete from the final package | **Partial:** Windows OpenGL SP, pure auto-joined MP, and dedicated lifecycles passed functionally; Vulkan and all roles built. No Vulkan runtime or Linux/macOS runtime qualification is claimed. |
| Retail asset coverage | Representative loose and PK4 sources plus classic proc, MD5RProc fallback, model, collision, image, sound, GUI, decl, effect, and animation opens are exercised | **Local PK4 pass; broader matrix pending:** the staged stock campaign used 40 retail PK4s and zero loose retail files and completed its SP/MP functional artifacts. Explicit loose-source and full representation coverage remains pending. |

### Performance and resource evidence

The direct comparison used windowed OpenGL at 1280x720 on `game/storage1` with
the same stock retail-PK4 set. Milliseconds are engine-reported map-phase times;
process peaks are observed working-set peaks, not pipeline allocation totals.

| Measurement | Cold learn/write | Exact warm cache/replay | Artifact |
|---|---:|---:|---|
| Total map-load time | 69,285 ms | 53,299 ms (23.1% lower in this local comparison) | Local SP log root below |
| Render-world phase | 7,882 ms | 3,014 ms | Local SP log root below |
| Game initialization phase | 25,736 ms | 15,538 ms | Local SP log root below |
| Player-spawn phase | 1,395 ms | 636 ms | Local SP log root below |
| Cache join | 111 ms | 115 ms | Local SP log root below |
| Media finish | 34,034 ms | 33,857 ms | Local SP log root below |
| Settle | 126 ms | 130 ms | Local SP log root below |
| Process peak | 2,075,062,272 B | 2,147,344,384 B | Observed Windows process peak |
| Manifest/replay/acquisition | learned 5,425; match 0; replay 0 | match 1; replay 64; acquired 68 | `com_levelLoadCacheReport 1` |
| Read/inflate and framing/integrity | no replay | 77,318,587 B; decoded 64; failed 0; cancelled 0 | `com_levelLoadCacheReport 1` |
| Peak pipeline residency | no replay | staging 76,932,512 B; decoded 77,318,587 B (both below 384 MiB defaults) | `com_levelLoadCacheReport 1` |
| Generated-cache counters | hits 0; misses 188; writes 188; corrupt 0 | hits 188; misses 0; writes 0; corrupt 0 | `com_levelLoadCacheReport 1` |

The local SP artifacts are under
`.tmp/milestone-b-final-evidence-v2/savepaths/sp/baseoq4/logs/`.
These timings describe this one development machine and map. In particular, the
warm process peak was higher than the cold peak even though both pipeline peaks
were inside their configured bounds; the 23.1% total-time difference is not a
cross-platform or release-candidate guarantee.

### Focused fallback and rollback runs

| Run | Result | Observed process peak |
|---|---|---:|
| Jobs disabled | 55,183 ms total; manifest match 1, replay/decoded 64, acquired 68, generated hits 188, no read/decode failures, `syncFallback=1`, clean completion | 2,149,097,472 B |
| Corrupt world cache | 55,144 ms total; generated hits 187, miss 1, write 1, corrupt 1; source fallback/rewrite and clean map marker | 2,148,675,584 B |
| Corrupt animation v3 | The marine-idle `.banim` was opened, rejected, reparsed from source, and atomically rewritten. Its recorded SHA-256 (prefix `CE5F99E0`) exactly matched the backup, and no `.tmp` remained. | Not separately recorded |
| Full rollback | Framework cache/preload plus animation cache read/write disabled; join 0; no generated model/world/collision/animation reference; clean completion in 29,969 ms | 2,062,823,424 B |

The animation corruption record is
`.tmp/milestone-b-final-evidence-v2/savepaths/sp/baseoq4/logs/milestone_b_final_v2_anim_corrupt.log`.
The rollback time is recorded as a correctness result, not compared as a speed
claim, because the run intentionally changed cache/write behavior.

The resulting private `generated/` tree contained 987 files and 180,618,336 B:

| Kind | Files | Bytes |
|---|---:|---:|
| Animations | 707 | 12,859,371 |
| Collision | 1 | 25,914,726 |
| Images | 91 | 76,535,744 |
| Manifests | 1 | 923,296 |
| Models | 186 | 22,846,157 |
| Worlds | 1 | 41,539,042 |

### Dedicated-server development run

`.tmp/milestone-b-dedicated-final/baseoq4/logs/milestone_b_dedicated.log`
records exit code 0 after 20.6 seconds with a 720,044,032 B observed process
peak. The actual `game_mp` module was selected, and two complete `q4dm1` loads
separated by `disconnect` both reached ready state. Generations 1 and 2 closed
and joined; the first load recorded 93 generated misses/writes and the second a
generated hit. No renderer or OpenGL markers appeared. The second in-process
manifest deliberately missed under changed exact state, so this run is not
dedicated manifest-replay evidence.

### Staged stock compatibility campaign

The final-development staged campaign at
`.tmp/milestone-b-stock-20260820-final4/` completed all functional SP
load/save/reload/demo and pure MP server/client lifecycle checks with
`si_pure 1`, `net_serverAllowServerMod 0`, and `ui_autoJoin 1`. It retained the
expected artifacts and engine screenshots against 40 retail PK4s and zero loose
retail files. Local visual review found coherent SP world/save views, active
`q4dm1` MP HUD/player views, and no cache or rendering corruption.

The report's overall status is nevertheless **FAIL**, solely because sustained
MP CPU timing exceeded the unchanged fixed budget: server p95/p99 were
22.548/29.739 ms and client p95/p99 were 24.406/28.778 ms. GPU budgets passed,
as did SP CPU/GPU budgets. The harness now waits up to 120 seconds for the cold
server and samples the settled client for 10 seconds, without changing the
budget. The earlier `.tmp/milestone-b-stock-20260820-2/` campaign passed every
role and budget, but its hashes predate final fixes and it is historical only,
not final-package promotion evidence.

### Build and platform qualification

- Windows x64 client, dedicated, OpenGL, Vulkan, SP, and MP build/link/stage
  checks pass.
- Native manifest-format and pipeline suites pass with MSVC and strict Clang
  C++20; the broad no-build/no-runtime validation set passes 132/132.
- The provenance audit remains 595 Doom 3 and 37 Doom 3 BFG header-family files.
- Linux ARM64 and macOS source/static/policy checks pass only. WSL runtime was
  unavailable with `E_ACCESSDENIED`, Docker was unavailable, and no Apple
  hardware runtime was available; none of those platforms is runtime-qualified
  by this evidence.

Do not mark Milestone B release promotion complete until the source pair is
committed, the package and hashes are bound to those commits, the fixed MP CPU
budget passes (or is changed through a separately reviewed contract update),
and the required release-platform and broader cancellation/failure campaigns
are retained and reviewed. The roadmap's implemented/local-integration status
does not waive this evidence gate.

## Source locations

- [Cache/manifest format](https://github.com/themuffinator/openQ4/blob/master/src/framework/LevelLoadCacheFormat.h)
- [Cache manager](https://github.com/themuffinator/openQ4/blob/master/src/framework/LevelLoadCacheManager.cpp)
- [Bounded read/framing pipeline](https://github.com/themuffinator/openQ4/blob/master/src/framework/LevelLoadPipeline.cpp)
- [Filesystem authorization/substitution](https://github.com/themuffinator/openQ4/blob/master/src/framework/FileSystem.cpp)
- [Session campaign key](https://github.com/themuffinator/openQ4/blob/master/src/framework/Session.cpp)
- [Render-model payloads](https://github.com/themuffinator/openQ4/blob/master/src/renderer/Model.cpp)
- [Render-world payload](https://github.com/themuffinator/openQ4/blob/master/src/renderer/RenderWorld_load.cpp)
- [Collision-model payload](https://github.com/themuffinator/openQ4/blob/master/src/cm/CollisionModel_files.cpp)
- [Player controls and cleanup](../user/level-load-cache.md)
- [Source provenance](source-provenance.md)
