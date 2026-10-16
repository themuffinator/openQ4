# Shared runtime refinements, 14 October 2026

This pass addresses reproduced failures in shared containers, motion sampling,
bit messages and both game modules' tactical search. Existing UI, renderer and
map-compiler work in the checkout is preserved.

## Changes

- `idList::HeapPop` now chooses the higher-priority child at every level. The
  old implementation calculated the correct child and then unconditionally
  replaced it with the left child. Tactical searches therefore could visit
  farther areas before nearer ones.
- List append and insert preserve a source element that aliases their own
  storage before reallocating or shifting it. Bulk list append reserves its
  final capacity once, including self-append, instead of copying the growing
  prefix at every allocation increment.
- Linear interpolation invalidates its cached sample when the start, end,
  duration or start time changes. Extrapolation caches the correct value when
  sampling before its start. A NaN cached time marks the cache stale, so
  negative sample times, including `-1`, are valid. A separate validity flag
  was tried first, but saves store these interpolators as native images, and
  the extra member broke every save holding a joint modifier or a running GUI
  transition; see [Savegame Reliability](savegame-reliability.md#remaining-raw-serialization-raw1).
- Accelerated linear and sine interpolation recompute velocity after endpoint
  changes. Reinitializing with a nonpositive duration clears prior phases and
  velocity; negative acceleration/deceleration durations clamp to zero.
- Bit-message strings support zero-capacity/discard reads without touching the
  destination. Zero-length string limits encode an empty string. Invalid byte
  write lengths are rejected before shifting or copying. Reads after exhaustion
  initialize the requested output, and saved/restored read cursors retain the
  overflow flag.
- Binary delta fields compare against the aligned baseline directly and decode
  into their destination. This removes the fixed 1 KB stack buffer and its
  redundant copies. Existing valid wire encodings and zero-length byte alignment
  are preserved; an explicit replacement can still recover from an exhausted
  baseline, while reusing an incomplete baseline is rejected.
- SP and MP tactical search size visited-area storage to the active AAS, reject
  invalid owner and destination areas, and clear stale results when no search is
  possible. Generation marks avoid clearing every area on each search; heap and
  feature storage are retained across queries. Equal feature weights compare
  equal. The existing distance, visit and result limits remain in place.
- Empty command tokens are ignored at dispatch, avoiding meaningless startup
  errors from empty script commands. Multiplayer smoke tools resolve their
  optional executable name before checking freshness, so a default launch
  neither crashes on `None` nor silently skips the executable check.
  The match-control smoke also clears each prior process log before launch,
  preventing old readiness markers from accepting an incomplete startup when
  reusing a writable test profile.
- Existing UI integration fixtures now expect the current retained-UI default
  and compile the production GUI number-formatting and dictionary-store helpers.
  This repairs three validation failures without changing the existing UI
  implementation. The state-log fixture retains its replaced-string poisoning
  and rejects all three deliberately reintroduced logging regressions.
- API checks now pin the checkout's existing game API version 52, and the zoom
  alignment check inspects the preparation function's body without assuming
  identical SP/MP parameter lists. The presentation fixture includes the HUD
  cache-validity field and verifies that a write invalidates it. Both CI
  workflows now include the previously omitted Vulkan-upload and UI feedback,
  atlas, pause and HUD-state checks, plus the new runtime-freshness check and
  native-test generator syntax checks.

## Regression coverage

Five native suites are registered by `build_native_tests`:

| Suite | Behavioral coverage |
| --- | --- |
| `openq4-idlist` | All permutations of seven heap values, duplicates, aliased string growth/insertion, self-append, bounded assignment count for a 4096-element bulk append |
| `openq4-interpolation` | Same-time mutations, rewinds, negative sample times, retiming, linear/sine endpoint changes and phase resets |
| `openq4-bitmsg` | Empty/discard/truncated strings, invalid lengths, overflow cursor restore, empty-block alignment, binary deltas through 8192 bytes and truncated-baseline recovery/rejection |
| `openq4-aas-tactical-game` | Production SP search ordering, reuse, stale results, area numbers above 32000, invalid graph edges and a subsequent smaller map |
| `openq4-aas-tactical-mpgame` | The same cases compiled from the canonical MP implementation |

The container and motion tests include production headers. Bit-message and
tactical tests compile the selected production declarations/methods verbatim,
with small substitutes for surrounding engine services and graph data. These
are behavioral tests rather than source-token checks.

`tools/tests/mp_runtime_freshness.py` also checks default and custom executable
selection, stale modules, timestamp tolerance and the explicit override on
Windows and Linux. It is registered with the push-validation checks.

## Validation

- The Windows x64 debug build and release-style staging pass for the client,
  dedicated server, both renderers and both game modules.
- All 171 native tests have passing results. The parallel run returned 170
  passes and a 240-second timeout in `openq4-ui-retained-screens`; that suite
  passed alone in 222 seconds with its original timeout. Both logs are retained.
  (The merged retained-page line later split that suite into nine
  `openq4-ui-retained-screens-<group>` suites with their own timeouts.)
- All five GCC/Linux ASan+UBSan suites pass. The final empty-baseline refinement
  also passes the Windows bit-message test and Linux sanitizers. Sanitizers ran
  in Ubuntu under WSL; this is not Linux graphical-gameplay qualification.
- Windowed single-player gameplay passes on `game/storage1` and
  `game/airdefense1` with OpenGL, and `game/airdefense1` with Vulkan. Engine
  captures show the loaded scenes. Saving and loading `game/airdefense1` also
  passes, including a fresh engine capture after restore and normal exit.
- The two-client OpenGL match-control smoke passes on `mp/q4dm1`, including
  admission, panels, repeated pause/resume, the authoritative aborted outcome
  and 16 engine captures. Both processes exit normally. Menu activation takes
  910/789 ms against the unchanged 3000 ms limit. Earlier admission timeouts are
  retained; the completed debug run uses a 180-second startup allowance and a
  480-second overall allowance after repairing stale readiness-log handling.
- All 340 source/contract checks have passing results after targeted repairs
  and reruns. The original run returned 326 passes and 14 failures: stale API
  and function-signature expectations, incomplete fixtures, missing CI entries
  and one native-input source-fingerprint change during development. All 14
  targeted reruns pass; the original failure log is retained. The new runtime-
  freshness check also passes separately on Windows and Linux.
  The settings-route rerun passes 286 checks; the retained adapter passes its
  production case and rejects 17 compiled mutations; state logging passes its
  15 checks and rejects three mutations.

Evidence is retained under `.tmp/refinement-20261014/`, including build, staging,
validation and sanitizer logs. No OS screenshots or mouse/keyboard automation
are used. Runtime checks use the staged `.install/` executables and installed
retail assets, with isolated writable profiles under that evidence directory.
Automatic approval review blocked deletion of the task's disposable compilation
outputs and helper scripts (`blocked by policy`); those files remain in the
same project-local directory alongside the evidence.

## Existing limitations

This work does not close the separate oversized runtime surface limit affecting
`hub2`, or the retained title/pause-menu performance parity work tracked in
[release completion](release-completion.md). No broad frame-rate improvement is
claimed from the focused container and tactical-search optimizations.

The existing build configuration also warns that the configured RmlUi
subproject revision may lag its wrap file, and that four native-input fixtures
access SDL headers through a subproject path. The latter is a Meson layout
warning that may become an error in a future Meson release. Dependency refresh
and that fixture build-layout cleanup remain separate work.

The live multiplayer run also reports lazy precache warnings and a missing
retail image, `models/monsters/burn_misc_sm`. They do not fail this functional
smoke; the asset-reference/precache cleanup remains unqualified by this pass.
