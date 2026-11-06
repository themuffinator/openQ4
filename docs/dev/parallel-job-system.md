# Portable Job System

openQ4 now owns a portable bounded job service for coarse engine work. It is
available in client, tool, and dedicated builds without changing Quake 4 asset,
save, demo, or network formats. Its first production consumer is the learned
level-load read/decode pipeline: workers read independently opened,
identity-checked source handles, perform the PK4 inflation reached by those
reads, validate supported source framing plus whole-buffer integrity, and
publish a sealed immutable DTO. Resource owners still perform asset-specific
parse/decode, validation, adoption, and upload on their established main path;
no live renderer, audio, archive, or game state is mutated by those jobs.

The architecture was informed by id Software's GPLv3
[Doom 3 BFG `idParallelJobList`](https://github.com/id-Software/DOOM-3-BFG/blob/1caba1979589971b5ed44e315d9ead30b278d8b4/neo/idlib/ParallelJobList.h).
The implementation is original openQ4 code. It deliberately replaces BFG's
platform-specific worker assumptions and spinning list wait with portable C++
threads and condition variables, explicit saturation, and cooperative
cancellation. Both projects use GPLv3-compatible licensing; no BFG source file
was copied into this subsystem, so the audited BFG-lineage source-file count is
unchanged.

## Runtime policy and lifecycle

`idCommon` initializes the engine-owned service before commands and higher-level
runtime systems can submit work. Shutdown stops admission, requests cancellation,
waits for running callers and workers, and joins every worker before network,
session, renderer, or other subsystem teardown begins.

The startup-only controls are:

- `jobs_enable 1` enables threaded scheduling. Setting it to `0` never discards
  work; accepted lists execute inline in deterministic insertion order.
- `jobs_deterministic 1` selects the same synchronous implementation for tests
  and debugging. Dedicated builds default this setting to `1`, so they create no
  background job workers unless explicitly configured otherwise; threaded
  client execution requires `jobs_enable 1` with `jobs_deterministic 0`.
- `jobs_numThreads 0` reserves one hardware thread when possible and clamps the
  worker count to 1-32. An explicit value selects that bounded count.
- `jobs_queueCapacity 64` bounds admitted lists and accepts values from 1-1024.

Synchronous work participates in the same active-list and quiescence contract as
threaded work. A concurrent `CancelAll`, `WaitAll`, or shutdown therefore sees
and joins an inline list running on another submitting thread. Concurrent inline
submissions sleep until their submit sequence reaches the front rather than
spinning.

### Level-load read/decode consumer

The level-load manager validates an exact learned manifest and applies its own
entry, per-file, staging/decode aggregate-byte, and read/decode chunk budgets
before creating one high-priority list. Every candidate file is opened through
ordinary VFS and pure-PK4 policy on the main thread, and its current path, size,
loose timestamp, and PK4 checksum must match before admission. Workers perform
bounded reads, PK4 inflation, typed framing validation for supported RIFF/WAVE,
Ogg, PNG, and JPEG sources, and whole-buffer transport/SHA-256 integrity walks with
cooperative cancellation. DDS and other owner-specific formats remain opaque.
Complete results become sealed immutable DTOs backed by shared byte arrays;
partial, malformed, late, failed, or cancelled results are never published.

When an owner later opens a resource, the normal VFS lookup runs first. Only an
already-ready exact-identity result may replace the bytes behind that authorized
handle, without waiting. Image, sound, model, world, collision, declaration,
GUI, effect, skin, animation, and raw-file owners therefore keep their ordinary
asset-specific parse/decode/adopt/upload behavior and source fallback. Map
failure, unload, filesystem restart, module reload, and shutdown cancel and join
the generation before dependent state is released.

`com_levelLoadModernization 0` disables the complete experimental consumer;
when explicitly enabled, `com_levelLoadPreload 0` disables preload without
disabling validated generated model/world/collision payloads. If the scheduler is deterministic,
disabled, saturated, or unavailable, the admitted bounded batch runs inline;
unadmitted sources continue through ordinary VFS reads. The exact campaign key,
budgets, controls, rollback, and still-pending promotion evidence are documented
in [Level-Load Cache Modernization](loading-cache-modernization.md).

## Submission contract

Each `idJobList` declares fixed job and dependency capacities when it is created.
The implementation also fails closed above 1,048,576 jobs or 256 dependencies
per list. Combined with the configured active-list bound, this places a finite
upper limit on admitted work. Scheduler list and worker storage is reserved
before workers start; admission, dependency refresh, cancellation, and teardown
do not grow scheduler-owned containers. Job functions receive their payload,
worker index, and an `idJobCancellationToken`; payload ownership and lifetime
remain the caller's responsibility through the list's terminal state.

Admission returns an explicit `idJobSubmitResult`. In particular,
`QUEUE_FULL` leaves the list in its building state so the owner can wait for its
own safe join point and retry. Work is never silently dropped or placed in an
unbounded overflow queue. Callers must handle every non-success result; the
service does not guess whether blocking, inline fallback, or cancellation is
safe for a consumer.

Dependencies are deliberately chronological: a list may depend only on a list
already submitted to the same service. Self-dependencies, duplicate edges,
foreign-service lists, unsubmitted dependencies, and reverse edges into an
already-submitted list fail closed. This makes a dependency cycle
unrepresentable. A dependent list sleeps until all prerequisites complete and
is cancelled if a prerequisite fails or is cancelled.

Lists have low, normal, or high priority. Higher priority wins initially, while
every skipped runnable list gains a scheduling age. At the starvation threshold,
aged lists enter a primary oldest-age selection class which ignores base
priority; equal ages use the monotonic submit sequence. A continuously runnable
low-priority list therefore runs after a bounded number of dispatches even when
nine or more earlier high-priority lists remain runnable. Selection is
deterministic even though completion timing with multiple workers is necessarily
concurrent.

Cancellation is cooperative. Jobs that have not started are accounted for and
never invoked; running jobs must poll `context.IsCancellationRequested()` and
leave their own data in a safe state. List waits, worker waits, dependency waits,
and shutdown waits all use condition variables. There is no spin or sleep-poll
loop in the scheduler.

## Diagnostics and validation

`jobsStats` prints current admission, queue high-water, running/sleeping worker,
completion, rejection, cancellation, failure, wake, priority-promotion,
execution-time, and wait-time counters. Per-list status and timing counters are
also available through the C++ API.

`jobsSelfTest` exercises engine-service execution, dependency ordering, batch
work, and counters in either threaded or synchronous mode. Automation should
require the exact versioned success prefix:

```text
jobsSelfTest PASS v1
```

Engine shutdown emits this exact clean-quiescence marker only after all accepted
work has reached a terminal state and every worker has joined:

```text
jobsShutdown PASS v1 initialized=0 queued=0 running=0
```

The focused native test covers deterministic inline execution, worker
parallelism, fixed capacities, retryable saturation, dependency and failure
propagation, self/cycle rejection, cooperative cancellation, low-priority aging,
threaded shutdown, and cancellation/join of an inline job submitted from another
thread. `tools/tests/parallel_job_system.py` keeps the production lifecycle,
public contract, build registration, and documentation wired into the default
validation suite.

## Consumer boundary and promotion evidence

Production consumers must have immutable inputs, caller-owned result storage,
and an explicit main-thread join point. The learned preload consumer meets that
contract with a worker-produced framing/integrity DTO whose generation, semantic
type, authoritative source identity, frame metadata, transport checksum,
SHA-256 integrity, and backing bytes are sealed before publication. This is a
real decode stage, but it deliberately stops before asset-specific owner parsing
or transformation. Future image/audio/model decode, transcode, upload, or
generated-cache preparation jobs require their own detached result and
main-owner adoption contract. Archive mutation, live game-state mutation, and
renderer API calls from arbitrary workers remain out of bounds.

The current light-grid CPU integration lives inside a dynamically loaded renderer
module. That module does not import the engine's `jobSystem` through
`RenderModuleAPI`, so wiring it directly would create an unresolved/duplicate
service boundary and unclear shutdown ownership. Light-grid work should adopt
the service only after a narrow renderer job import or engine-owned work packet
contract is designed; it remains outside this consumer migration.

The learned level-load read/framing pipeline is the first production migration.
`jobs_enable 1` and `jobs_enable 0` must still produce identical owner-visible
results even though one may stage reads concurrently and the other runs the
same bounded work inline. The 2026-08-19 current-build validation closed the
earlier local Milestone A lifecycle gate:
jobs-on and jobs-off `game/storage1` produced identical engine TGA bytes and
matching game state; both OpenGL modes and jobs-on Vulkan completed the repeated
`game/mcc_2` -> `game/storage1` -> `game/storage2` -> `game/storage1` ->
`game/tram1` campaign with clean shutdown markers; and five dedicated-server
runs each exited normally with exactly one synchronous self-test and one clean
shutdown marker.

Those runs used an immutable development runtime built from an uncommitted
source tree. Release promotion still requires the same evidence to be retained
from clean source and a freshly staged final package. Multiplayer evidence must
retain `ui_autoJoin 1` unless the join flow itself is under test. Milestone B's
new consumer parity, cancellation, corruption, memory, and timing evidence is
also still pending in
[loading-cache-modernization.md](loading-cache-modernization.md); the integrated
consumer is not itself a final performance or platform claim.

## Map-cycle and shutdown evidence (6 November 2026)

CORE-01 reran the map-cycle checks on the Windows x64 debugoptimized build with
a harness that records the process's private bytes after each load settles and
then quits. Each tour ran from a fresh save path against the installed retail
packages.

| Tour | Loads | Exit | Peak private MiB by lap | Jobs shutdown |
|---|---:|---|---|---|
| Dedicated, `mp/q4dm1`-`q4dm8` x3, `jobs_enable 1` | 24/24 | 0, 0.42 s after the last command | 391 / 427 / 427 | PASS |
| Dedicated, same, `jobs_enable 0` | 24/24 | 0, 0.41 s | 392 / 426 / 427 | PASS |
| SP client, airdefense1/airdefense2/hangar1/hangar2 x3 | 12/12 | 0, 2.3 s | 3,386 / 3,439 / 3,582 | PASS |
| SP client, airdefense1/hangar2 x6 | 12/12 | 0 | airdefense1 3,381-3,514; last four 3,462-3,489 | PASS |

The first lap loads each map's assets once; afterwards memory stays in a fixed
band. On the dedicated server it moves between about 370 and 427 MiB rather than
rising, with the same pattern whether jobs run threaded or inline. The client
needed six laps to show it: `airdefense1` rose for two visits and then held
within 27 MiB, and `hangar2` within 20 MiB. No run logged an error, and every
run's last line was `jobsShutdown PASS v1 initialized=0 queued=0 running=0`.
The SP client ran with a hidden window and OpenGL. These are local development
runs, not release packages, and no other platform was toured.

## Final-package qualification (6 November 2026)

CORE-01 repeated the checks on release-style packages built from clean source
at `00f4b4003` the way `manual-release.yml` builds them (`debugoptimized`,
`b_ndebug=true`, forced fallback dependencies, stable track, version 0.14.1)
and packed by `package_release.py`. Each package's `VERSION.txt` records that
commit with `openq4_dirty=false`. They were extracted and run in place, against
the installed retail packages, which stayed read-only.

| Package | SHA-256 |
|---|---|
| `openq4-0.14.1-windows-x64.zip` | `5dc827ebbd58b86f5711f9081b58af47536aec793dcc239c2f4cf8ef60e04db5` |
| `openq4-0.14.1-linux-x64.tar.xz` | `f7b5d6e16194f61c4c3c3df80444d45f74a4f12d8a7549a20ec7adb3744be933` |

Windows x64 ran on the development machine; Linux x64 ran on Ubuntu under WSL,
the client under Xvfb with Mesa llvmpipe. Memory is private bytes on Windows
and anonymous resident plus swapped memory on Linux, at each load's settle.

| Check | Windows x64 | Linux x64 |
|---|---|---|
| Dedicated `q4dm1`-`q4dm8` x3, `jobs_enable 1` | 24/24, lap peaks 389/423/421 MiB, exit 0.42 s after the last command | 24/24, 118/120/120 MiB, exit 0.2 s |
| Same, `jobs_enable 0` | 24/24, 390/423/424 MiB | 24/24, 118/120/120 MiB |
| Dedicated starts and quits | 5/5 exit 0, one synchronous self-test and one shutdown pass each; 5 packaged smokes pass | 5/5, the same; packaged smoke passes |
| SP campaign `mcc_2`-`storage1`-`storage2`-`storage1`-`tram1` | OpenGL jobs on and off and Vulkan jobs on: 5/5 loads each | Jobs on and off: 5/5 each |
| SP `airdefense1`/`hangar2` tour | 6 laps, peaks 3,338-3,449 MiB, last four laps within 30 MiB | 8 laps, 1,815-1,917 MiB (see below) |
| Jobs on/off parity, `game/storage1` | Four runs (on, off, both repeated): identical view, identical 2,837-line entity list | — |

Every run exited 0 with no logged error, and every run's last job line was
`jobsShutdown PASS v1 initialized=0 queued=0 running=0`. Every client log
records the effects system starting (`BSE Init`) and no dedicated log does:
the dedicated server kept its effects runtime disabled. The parity screenshots
are not byte-identical, but two runs with the same setting differ as much as jobs on
and off do (mean difference 3.2-4.5 against 5.9). The differences lie in the
animated smoke over the drop zone; the lift and player model match.

The Linux tours found a memory defect the Windows runs could not show. With
jobs on, every `airdefense1`/`hangar2` load added about 60 MiB: 2,370 MiB on
the first lap, 3,125 MiB by the eighth and still rising, against a flat
1,800-1,894 MiB with `jobs_enable 0`. Job workers allocate from their own glibc
heap arenas and the main thread frees that memory, so glibc kept the pages. The
engine now hands free heap memory back to the system after each level load
(`Sys_ReleaseFreeHeapMemory`, `malloc_trim` on glibc, nothing on Windows,
whose heap does this itself). The release costs 21-31 ms per load and is
reported by `com_showLevelLoadTimes`; with it, the same eight-lap tour peaks at
1,815-1,917 MiB, level with jobs off, and load times are unchanged.

Renderer budgets ran on the Windows package with the required benchmark
profile:

| Backend | Result |
|---|---|
| OpenGL | 9/9 required cases within budget. `storage2` and `medlabs` exceeded it while other clients shared the machine (CPU p95 28 and 58 ms) and pass on a quiet rerun (CPU p95 3.8 and 4.1 ms). |
| Vulkan | 9/9 required cases within budget. |

The optimized package exposed a timing assumption in `sp-storage1-second`:
720 settling frames at a 240 fps cap are only 3 s of game time, so both
backends sampled the starting lift still rising. The scene contract now also
waits 6 s of real time (`stock-entry-view-v2`), and the case passes on both
backends.

The [retail baseline](stock-asset-baseline.md) ran the Windows package against a
view of the 40 retail PK4s alone, bound to a manifest approved in a separate dry
run. SP capture on `storage1`, save and load, demo playback and the `q4dm1`
listen server and client all pass, within their per-map budgets on a quiet
machine; the engine screenshots show the expected scenes. A first attempt on
the shared machine failed only the server's p99 (154 ms): its
p50 stayed at 6-8 ms while isolated frames reached 150 ms, and the rerun's p99
was 9.8 ms.

macOS is a preview platform without maintainer hardware or retail assets on
continuous integration; its evidence comes from reporters through the macOS
evidence gate in [release completion](release-completion.md), not from CORE-01.

## Background asset work profile (6 November 2026)

CORE-03 profiled what level-load work is left on the main thread before moving
more of it to workers. A hidden-window SP load of `game/airdefense1` took
15.9 s: render world 0.3, game initialization 6.3, player spawn 0.5, media
finish 3.2 and settle 5.6. Image work in media finish was 1.6 s across 1,835
files: timestamp probes 181 ms, generated-image reads 61 ms, decode 721 ms and
GL upload 311 ms. One 32 MiB environment cube map was 236 ms of the decode.
The DDS prefetch already moves the reads of 1,750 of those files to workers.

Moving the remaining decode would save at most about 0.7 s, under 5% of the
load, and would need image programs and their buffers to cross threads; it is
not justified on this evidence. The profile did find work that was wasted
outright: dedicated servers read every prefetched image and discarded it. Only
a running renderer now prefetches (see
[Air Defense 1 evidence](airdefense1-optimization-evidence.md)).
