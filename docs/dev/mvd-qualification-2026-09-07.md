# MVD qualification (2026-09-07)

The named replay-camera and recording-ownership scenarios below pass. These
checks extend the [competitive match audit](competitive-match-reference-audit.md)
and do not establish tournament certification or physical menu navigation.

## Paused cameras across a restart

A seek that stopped between a round reset and the next active player snapshot
lost the selected camera. Continuous playback could lose it at the same round
transition. The engine now retains the chosen player until the game can present
an eligible offline POV again. Choosing Free Camera
explicitly cancels restoration. The game still applies its offline-demo and
spectator eligibility checks before presenting the view.

Restoring the slot exposed a second problem: the inactive snapshot left the
player's eye height at zero, and a paused seek ran only one of the prediction
frames that normally blend crouch height. The camera therefore remained 8.84
units above the floor instead of 68. Camera selection/restoration now snaps to
the recorded stance; ordinary movement retains the existing smoothing. The
same stance calculation handles standing, crouching, death, spectators and
vehicles, without changing the live camera-authorization boundary.
An already valid camera is not reset on every continuous-playback frame, so
ordinary replay crouch-height smoothing is preserved as well.

All report names in the following table are relative to
`.tmp/mp-gametypes-round/` and contain a `report.json` file. The source is the
25.696-second recording linked by the map report in
`series-bo1-clean-final2/host/baseoq4/match-results/` (match session
`16625671859972499265`, series `3584208895820049897`). Playback starts through
the selected Demos library entry, follows both humans, enters free camera,
refollows, seeks from 3 to 10 to 12 seconds, rewinds, steps and consumes the
clean end record.

| Report | Result and evidence |
| --- | --- |
| `mvd-seek-gap-before-fix` | Expected regression failure on runtime v40: the camera is lost after the intermediate restart seek and on rewind. The recording itself has a clean end and the engine exits 0. |
| `mvd-seek-gap-after-fix` | Pass on v41, which changes only the engine executable from v40. The original selected player returns at the next active snapshot and survives rewind. |
| `mvd-seek-gap-explicit-free` | Pass on v41: explicitly leaving follow at the restart gap retains free camera through later seeks and rewind. |
| `mvd-eye-height-before-fix` | Expected regression failure on v43: following is correct, but both the queried eye and actual player render view are only 8.84 units above the origin at 12 seconds. |
| `mvd-eye-height-after-fix` | Pass on v44, which replaces v43's game MP module. Standing eye and render height are immediately 68 at the initial, forward-seek and rewind checkpoints. |
| `mvd-eye-height-staged-package` | Pass on the complete v45 runtime, matching all nine staged package files. It retains the same 68-unit eye/render-height checks and eight fresh engine screenshots, with exit 0. |

Each restart-gap run retains eight engine-written screenshots. Original TGA
comparison confirms the visible floor-level camera before the eye-height fix
and the correct elevated view afterward. PNG copies are for visual review;
the engine render target is the capture source.

`mp_mvd_smoke.py --standing-camera-height 68` makes the standing idle-player
pose check explicit. It is optional because arbitrary recordings can include
crouching, death or motion. `--boundary-seconds` exercises an intermediate
restart gap, and `--cancel-boundary-follow` checks an intentional free-camera
choice. The separate startup budget allows cold map loading without relaxing
the later seek/control deadlines.

The production `mp_match_demo_follow_contract.py` passes on Windows and Linux.
It exercises the offline authorization boundary, cycling across instances,
round-viewer recreation, immediate stance restoration, preserved live crouch
smoothing and rejection of live-server camera mutation. Engine MVD lifecycle,
server API and Demos browser/control/localization contracts also pass.
The full companion competitive contract runner passes all 54 checks in
`.tmp/mp-mvd-final-competitive-contracts.log`.

## Continuous rounds and current recordings

`rr-mvd-continuous-comparison` reproduces the continuous-playback camera loss
on v45. `rr-mvd-continuous-camera-fixed` passes on v46, which changes only
v45's engine executable: the player remains selected across multiple Red Rover
rounds, and all initial/forward/rewind eye and render heights are 68. These
causal runs use an older development recording with rejected competitive match
views; they establish camera behavior, not compatibility of its old match UI.

`rr-managed-mvd-current` records a fresh eight-round managed Red Rover match
with two humans on pinned v47. Seven remote round resets retain match state,
both peers reach the accepted 8-0 review and exit 0, and session
`13797616128935558160` publishes a lossless 32-event report with a committed
MVD. The copied runtime and codec manifests identify its recording package.

`rr-current-mvd-seek` passes that current-format recording on the final v48
package, including library selection/Play, both POVs, free camera, seek from
30 to 70 seconds, rewind, step and clean end. It retains seven engine captures,
checks all three standing eye/render heights and reports zero rejected match
views. The source uses the current game-state layout even though its recording
package and final replay package are recorded separately.

`rr-current-mvd-continuous` also passes on v48: normal playback crosses the
intervening rounds without losing the selected player, then rewinds and reaches
a clean end. It retains seven captures, 68-unit eye/render heights and zero
rejected match views. `mvd-persistent-explicit-free` passes the earlier BO1's
intermediate restart-gap check on v48 with eight captures: deliberately choosing
Free Camera stays free through forward seek and rewind. Both engines exit 0.

The fixture's `--play-to-later` option compares continuous playback with forward
seeking. Rejected competitive match views now fail qualification by default;
`--allow-development-view-mismatch` is restricted to explicit camera diagnosis
of old development files, with the rejection count retained in the report.

## Recording ownership and limits

`mp_recording_ownership_smoke.py` runs a real two-human Team DM BO1 through
veto, map entry, countdown, gameplay, forfeit, review and series completion.
Each passing case below retains eight engine captures and exits both peers
with code 0. The final accepted score is 1-0, with a single matching result in
an ordered journal without dropped events.

| Report under `.tmp/mp-gametypes-round/` | Proven behavior |
| --- | --- |
| `mvd-operator-stop-before-result-matched` | The operator starts the recording before match countdown and stops it during gameplay. The map and final series both link the same committed manual recording. Runtime v42; series `9938172325585392080`. |
| `mvd-operator-stop-after-result` | The operator's stream keeps growing through map review. The sealed map report omits the unfinished MVD; an explicit stop before series sealing allows the final series to link the committed file. Runtime v40; series `14687989254439840105`. |
| `mvd-operator-open-at-seal` | The stream keeps growing after the series completes. The immutable series correctly retains a pending `.mvd.part` entry, reason 5. A later explicit stop commits the MVD without changing the sealed series JSON hash. Runtime v40; series `7046198148500317594`. |
| `mvd-recording-start-failure` | A file occupying the fixture's `demos` directory prevents automatic recording from opening. The match still completes and publishes valid result JSON; one `mvdStart` failure records reason 259, with no playable MVD link. The blocker remains unchanged. Runtime v40; series `17006646913452047716`. |
| `mvd-automatic-duration-limit` | A one-minute recording limit stops exactly one automatic stream cleanly while the match remains live. Subsequent forfeit and series completion publish the correct map, series and MVD artifacts. Runtime v42; series `9193654515498853895`. |
| `mvd-automatic-size-limit` | The configured 1 MiB cap stops automatic recording while gameplay remains live. A 1,048,549-byte `.mvd.part` is retained, the journal records one `mvdStop` failure (reason 262), and neither map nor series advertises a playable recording. Both peers complete the series and exit 0. Runtime v45; series `4093041005818189957`. |

These are actual engine recording tests. They complement the earlier blocked
final-destination and recovery checks in the main audit.

`mvd_recording_failure_contract.py` additionally executes the production record
writer, cap calculation, terminal-result capture, close/sync and promotion
control flow against deterministic file faults. It covers short record headers
and payloads, invalid inputs, the exact cap boundary, index/end-record failures,
failed synchronization/promotion and duplicate or reentrant stops. A failed
recording retains only its partial path; no failure may publish it as playable
or close/promote it twice. Windows and Linux ASan/UBSan checks pass. A mutated
writer that swallows the synchronization failure is rejected. The contract is
included in both CI workflows and the validation runner.

The native fault tests stub the filesystem, index builder, checksums and reset
dependencies. They do not simulate actual disk exhaustion, power loss or every
process-interruption boundary, and they complement the runtime tests rather
than replacing them.

## Build and evidence identity

The final complete Windows build and staging pass in
`.tmp/mp-mvd-final-replay-build.log` and
`.tmp/mp-mvd-final-replay-stage.log`. Staging uses the project wrapper with
`install -C builddir --no-rebuild --skip-subprojects`; automatic closure of
running applications is disabled. No import libraries are present in the
staged package root or `baseoq4` directory.

`.tmp/mp-runtime-qualification-v48/manifest.json` and its five-entry codec
manifest identify the copied runtime and production decoder. All nine runtime
files match `.install`; the comparison is retained in
`.tmp/mp-mvd-final-replay-staged-hashes.json`. These hashes identify the tested
working-tree build, not a clean committed release:

- Client: `e6f280772db1215a0fcea384a380160933c203ce4174eedd1ebd95ad3c86835d`.
- MP game module: `5ad9c1864782d8835e4c6687b40ef721d982581cf9997ea00db18f6a9798e841`.

The earlier v45 build/staging evidence remains in
`.tmp/mp-mvd-final-camera-build.log`, `.tmp/mp-mvd-final-camera-stage.log` and
`.tmp/mp-mvd-final-camera-staged-hashes.json`; it predates the continuous-round
camera extension.

All launches are windowed with hidden test windows, disabled mouse/joystick
devices and explicit MP auto-join. Native console and GUI action handlers
drive the fixtures; no operating-system input injection or screen capture is
used.

## Other findings and remaining scope

`rr-mvd-camera-restoration` passes the older 128.912-second Red Rover recording
through library Play, both player cameras, free camera, forward seek, rewind,
step and clean end on v44. All three measured host views have 68-unit eye
height. A distorted foreground debris effect remains visible at the
70-second round-complete checkpoint; this replay-control pass does not qualify
that rendering issue as fixed. Continuous playback at the same location on
v46 does not show the obstruction. Disabling effects/material deformations
does not remove the seek artifact, and servicing posted events during snapshot
reads did not correct it; that candidate change was reverted. The fresh
current-format recording's selected view is clear at its own 70-second
checkpoint, but uses a different spawn position and does not close the older
replay's transient-geometry finding.

An earlier `mvd-operator-stop-before-result` run using v41 failed during client
admission, before any recording operation, with a null collision-model fatal
error. The complete matched v42 package passed the same recording scenario
and the duration-limit scenario. The isolated admission failure's cause is
not established; v41 remains an offline causal comparison, not the matched
live qualification package.

Existing stock-asset/AAS and non-precache warnings remain distinguishable
from these regressions. Physical keyboard/controller navigation, remaining
filesystem failure boundaries and the broader competitive release checks in
the main audit remain open. Live Q4TV/repeater transport is not shipped.
