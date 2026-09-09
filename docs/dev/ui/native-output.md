# Native-output OpenGL UI repair

This increment starts from engine `a8bad0f5bf69b08493714ad4faaeae7585d5abf2`
and companion `1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`. The
implementation is qualified by the final integrated production-method checks
and separately bound actual before/after pixel evidence below. This is partial evidence for
[SUR-006](product-requirements.md), not completion of that requirement or M6.

## Behavior

OpenGL's swap-tail resolution filter could resample the completed backbuffer,
including UI, when a below-native scale in modes 1–3 had no qualifying scene
present or temporal owner. UI-only frames could therefore lose edge and text
detail without reducing world rendering cost. The repair makes that late hook
inert and removes its obsolete scene-present marker.

Main-world scaling remains in the existing scene-target sizing and
spatial/temporal resolve before later native UI commands. Supersampling retains
its existing size limits and resolve. If a scene target is unavailable, the
existing direct-render fallback receives no substitute completed-frame filter.
Vulkan and GLES production sources are unchanged.

Static below-native `r_resolutionScaleMode 0` retains its explicit legacy
whole-frame crop. This compatibility mode is outside this native-UI claim.
CRT/color/opaque-alpha/present ordering is unchanged.

## Qualification and limits

The final integrated production-method test executes the real swap, scene-size,
spatial-presentation and sharpening methods against counted graphics doubles:
**1,220 checks pass and seven compiled source mutations are rejected**. It
covers ordinary UI-only output, world sizing/resolve boundaries, mode-0 policy,
dynamic extents, supersampling clamps, failed-present inputs, sharpening failure,
MSAA copying and screenshot routing. Full `RB_STD_DrawView` target setup is
statically guarded and reviewed, not executed by this extraction harness.

The final integrated record is `.tmp/native-ui-output-021km139/result.json`,
SHA-256 `725fb1fdff0e6ddd88f2715785af202bcfa78e0c28f83a5d2169bdb1cd61eb4c`.
It hashes the actual source bytes. The first integrated test attempt is preserved
as `.tmp/ui/native-output-review/regression.log`: its mutation anchor expected LF in CRLF source. The
test's source-extraction normalization was corrected, retaining raw source hashes;
this was a test portability failure, not a production rendering failure.
The earlier isolated 1,220-check/seven-mutation record remains historical at
`.tmp/ui-native-output-worktree/.tmp/native-ui-output-joen2fbt/result.json`.

The final Windows build completed 1,226 targets; adjacent temporal-presentation,
screenshot-readback and HDR checks passed. Its existing Meson warning about the
stored RmlUi wrap hash remains recorded. A separate dependency audit reconstructs
the pinned archive with both authored patches: all 578 source/header/integration
files match after CRLF normalization, with four newline-only differences and no
content differences (`.tmp/ui/native-output-review/rmlui-source-qualified.json`).
Live dependency files and build metadata were not changed. The extraction test itself does not
establish GPU pixels. Separate final Windows SP/OpenGL before/after captures
and their source/package/binary bindings passed. Across the eight ordinary
non-reference UI cases, all 2,764,800 RGB channels per image equal the 100%
reference after the repair (zero changed channels
and maximum channel delta zero). The preserved before captures show
the following differences against their own 100% reference; every corresponding
after case has zero differences:

| Mode | Scale | Before changed RGB channels | Before maximum channel delta |
| --- | --- | --- | --- |
| 1 | 85% | 369,814 | 144 |
| 1 | 50% | 491,698 | 163 |
| 2 | 85% | 414,165 | 138 |
| 2 | 50% | 574,472 | 162 |
| 3 | 85% | 369,814 | 144 |
| 3 | 50% | 491,698 | 163 |

Mode 1 at 125/200% already equaled the reference before the repair and remains
equal afterward. Mode 0 at 50% remains byte-identical before/after, with
2,330,660 differing RGB channels against native and maximum delta 251; that
deliberately preserved crop is exempt from native-output equality.

All 24 engine images were visually reviewed. Each run retained 39 baseline
warnings with no new warning messages or errors. The immutable
`.tmp/ui/native-output-review/capture-evidence.json` binds the actual matrix,
original TGAs, lossless previews, logs, commands, source/package/binary hashes, build/tests and
review; its digest is recorded in the requirement register. Supporting
moving-world captures do not prove world pixel equality or performance.

The fixed probe enters SP/OpenGL gameplay, then uses the normal retained SYSTEM
route on a UI-only frame. At constant 1280x720 output and 125% density, it
compares every RGB channel of the stable, unfocused UI against mode 1 at 100%: mode 1 at
50/85/125/200%, and modes 2/3 at 50/85%. Brightness/gamma, authored state and
geometry are held constant; CRT, temporal and other screen effects are disabled.
Engine `screenshot` TGAs and lossless previews preserve the original failing
baseline and support review of complete text and edge detail. Mode 0 at 50% is
measured separately and exempt from equality. Modes 2/3 above native are not
included in this capture matrix.

The two moving gameplay images per run and recorded scene extents are supporting
observations, not proof of world pixel equality, physical input alignment or
reduced rendering cost. Unfocused static UI does not qualify all focus, hover,
transition, modal, loading, console, HUD, cinematic, subtitle or world-GUI states.

Remaining work includes:

- Actual dynamic-resolution/world-target execution, input-target equality,
  cinematic/subtitle geometry and all UI surfaces at constant native output.
- The legacy crop policy and full renderer/platform/display matrix.
- CRT ordering/parity: GL still filters the completed frame; the audited Vulkan
  path does not implement equivalent CRT behavior.
- The pre-existing GL mode-3 policy mismatch: the world sharpening helper clamps
  the mode to 0–2, selecting mode-2 sharpening for mode 3. This UI-only repair
  does not qualify advertised nearest-neighbor world filtering.
- Optimized CPU/GPU, memory/lifetime and full product qualification under
  [M6 and the final audit](../plans/ui-product-completion.md).

All 227 requirements, 271 pending GUI migrations and seven final gates remain
required. Preserve the earlier [SYSTEM-exit checkpoint](system-exit.md) and its
original evidence unchanged; its render-order finding motivated this repair.
