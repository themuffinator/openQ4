# Native GUI import checkpoint

Status: Stage 1 translation input, 8 September 2026. All 271 inventoried GUI
resources have native preprocessed token exports and structured import records.
No resource is accepted as a replacement. The [migration manifest](migration-manifest.json)
and [full plan](../plans/idtech5-ui.md) retain the complete migration, runtime,
artwork and editor requirements.

## Pipeline and provenance

1. [Inventory](inventory-report.md) establishes the effective source paths and
   SHA-256 hashes under explicit retail/openQ4 mounts. Its `--export-requests`
   option writes the source/hash request list consumed by the capture harness.
2. `ui_exportLegacy` reads each source through the engine filesystem, snapshots
   its bytes, and preprocesses that exact snapshot with `idParser::LoadMemory`.
   The source name preserves native include lookup. Lexer flags match
   `idUserInterfaceLocal::InitFromFile`; includes, macros, conditional sections
   and string escapes use the engine implementation. Exporting does not create
   windows or execute GUI scripts.
3. Each JSON file contains original root bytes, parser flags, and ordered
   records of token type, subtype, line, lines crossed, active parser source and
   value. Bytes use an explicitly tagged Latin-1 mapping with ASCII JSON
   escapes; this is a lossless byte transport, not a localization encoding choice.
4. [The capture harness](../../../tools/ui/capture_legacy_baseline.py) binds the
   `UI_GUI_EXPORT_BEGIN/END` log interval to every output, rejects preprocessing
   warnings/errors, verifies root source hashes against the inventory, and
   records token counts and output hashes. An exported file alone is insufficient:
   `idParser` can report a nonfatal error before reaching the export command's end.
5. [The structured importer](../../../tools/ui/legacy_syntax.py) verifies each
   token file against capture metadata, then writes ordered window/property/event
   records, expression trees, script branches, dependencies and diagnostics.
   Grammar tables and their hashes come from the local `Window.cpp`,
   `GuiScript.cpp` and `RegExp.cpp` sources.

The root snapshot/hash identifies exactly what was parsed. Include and macro
expansion is captured in the token output; the current parser-source field is
not full macro-definition provenance. Captured root bytes are not an archival
snapshot of every included non-GUI file. Preserve inventory, tokens, logs,
binaries and source revisions together when reproducing an import.

The command limits roots to `guis/`, outputs to `ui-import/*.json`, source size
to 8 MiB, tokens to one million and output to 64 MiB per resource. The harness
limits request batches to 512 entries and 32 KiB of generated console commands.
Generated proprietary source/token/model data stays under `.tmp/` and is never
included in Git or release documentation.

## Structured translation input

The model retains declaration order and duplicate window names, typed register
expressions, string values, per-window relative timelines, named events,
definitions, icons and nested `if`/`else` scripts. Every node refers back to a
token span; diagnostics record token index, active parser source and line.

Expression grouping follows the legacy engine: subtraction and division
associate from the right (`10 - 3 - 1` evaluates as `10 - (3 - 1)`); `&&` and
`||` share a precedence level. Negative terms require a number, and conditional
branches retain the engine's precedence rules. Replacing these with a C-like
expression parser would change existing behavior.

The dependency model records window symbols, GUI dictionary references, CVars,
localization keys, materials/fonts/models/skins/animations, and application
request fragments. Requests such as `set "cmd" ...` and `consolecmd` remain
unexecuted data for subsequent game-bridge resolution.

`complete_token_coverage` means the structured pass completed and consumed every
token. `complete_syntax` additionally requires no reported syntax diagnostic.
Neither establishes native window-variable resolution or behavior parity.
Unknown/custom properties retain their operands with pending resolution;
table/vector indexing, dynamic materials, duplicate-window merge behavior,
localization decoding and application dispatch still need semantic work.
Every output explicitly records `replacement_acceptance: false` and
`binding_resolution: pending`.

## Corpus results

| Measurement | Result |
| --- | ---: |
| Native exports with matching effective source hashes | 271 / 271 |
| Structured passes with complete token coverage | 271 / 271 |
| Resources without a syntax diagnostic | 269 / 271 |
| Window declarations after preprocessing | 15,414 |
| Event declarations after preprocessing | 8,654 |
| Property declarations | 73,957 |
| Script statements, including branches | 31,562 |
| Float/vector definitions | 877 |
| Icon definitions | 11 |
| Custom property declarations awaiting resolution | 18 |
| Unique GUI dictionary references | 1,131 |
| Unique CVar references | 161 |
| Unique localization keys | 1,237 |
| Application GUI-command / console-command occurrences | 1,805 / 82 |

These are declarations/reference occurrences across separately imported
resources, with includes expanded. They are not live instance counts. They
intentionally differ from the earlier unexpanded lexical inventory.

Two shipped sources require explicit expression review:
`guis/monitors/strogg/core/core4.gui:635` and
`guis/monitors/strogg/hub/hub4.gui:631` contain a bare backslash in a color alpha
expression. Native `ParseTerm` checks a declaration table first, then the
owning window's variables. If neither resolves the term, it emits a deferred
variable operation. `FixupParms` retries the variable lookup once after parsing;
`EvaluateRegisters` writes **zero** when the resulting pointer is null. An
existing variable, a definition encountered before fixup, or a matching table
can change that result. An immediate float lookup also calls `Init`/`Set` with
the token spelling (which is nonnumeric here); late fixup does not reinitialize
the variable. An unqualified name does not inherit a parent's variable.

The importer now records this conditional native fallback beside the original
term, token span and diagnostic. It still refuses unconditional constant
lowering: exported tokens do not observe the live declaration/variable inventory.
The following literal `n` and `matscalex` tokens are separate native custom
properties, not a newline escape to repair. Their original line grouping is
preserved, including the separate `-` property in `core4.gui`.

`tools/tests/ui_legacy_expression.py` compiles the actual native lookup,
expression, fixup and evaluator method bodies against counted surrounding
services. It proves the unresolved-zero path, table precedence, immediate/late
local definitions, parent/qualified lookup, and both trailing-token layouts.
This establishes the native fallback; it does not instantiate these monitors,
observe a live table inventory, qualify later script writes, or accept their
replacement GUIs. Other unusual custom fields remain pending.

The earlier lexical brace observation for `guis/maps/tram1/bridge1.gui` does
not prevent complete preprocessed structure import. Instantiation and behavior
of that GUI still require a gameplay baseline.

## Reproduction and verification

Use a staged client built with the Meson wrapper. From the repository root:

```powershell
python tools/ui/legacy_inventory.py `
  --mount 'retail=C:\Program Files (x86)\Steam\steamapps\common\Quake 4\q4base' `
  --mount 'openq4-pak0=content/baseoq4/pak0' `
  --mount 'openq4-pak1=content/baseoq4/pak1' `
  --output .tmp/ui/legacy-inventory.json `
  --export-requests .tmp/ui/legacy-import-requests.json
python tools/ui/capture_legacy_baseline.py --mode sp --renderer gl --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' --legacy-export-list .tmp/ui/legacy-import-requests.json --output .tmp/ui/import/sp-gl
python tools/ui/capture_legacy_baseline.py --mode mp --renderer vulkan --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' --legacy-export-list .tmp/ui/legacy-import-requests.json --output .tmp/ui/import/mp-vulkan
python tools/ui/legacy_syntax.py .tmp/ui/import/sp-gl --output .tmp/ui/import/syntax-sp
python tools/ui/legacy_syntax.py .tmp/ui/import/mp-vulkan --output .tmp/ui/import/syntax-mp
python tools/tests/ui_legacy_inventory.py
python tools/tests/ui_legacy_import.py
python tools/tests/ui_legacy_expression.py --mutations
```

Both capture and analysis require fresh output directories. The inventory and
structured-import tests are included in commit-validation and push-verification
script smoke jobs. The 14 inventory
tests exercise effective VFS selection and exported request hashes; the 19
import tests cover expression grouping, branches, duplicate declarations,
timelines, dependency data, incomplete structures and source/log validation.

Windows client and dedicated builds and runtime staging passed. Final local
captures are `.tmp/ui/import/sp-gl-corpus2` and
`.tmp/ui/import/mp-vulkan-corpus2`; structured results are `syntax-sp3` and
`syntax-mp3` in the same parent. All 271 token files are byte-identical across
SP/OpenGL and MP/Vulkan, and the structured summaries match. Snapshot parsing
also matches the preceding `LoadFile` export in token type/subtype/value and
line fields; only active root-source path spelling differs.

Both runs entered the correct gameplay map, stayed hidden/windowed with host
mouse/controller input disabled, captured through engine `screenshot`, and
exited cleanly. The reviewed 1280x720 images show the existing SP and MP game/HUD
rendering after export; they qualify gameplay continuity, not replacement art.
SP produced zero warnings/errors. MP produced zero errors and the same 93
warnings (86 unique messages) as the preceding input checkpoint.

| Final evidence | SHA-256 |
| --- | --- |
| Client executable | `165c5eeaf7c966e5958188b99f5cb23aadd17d61119dd2ad76ff6d65b65542a8` |
| SP engine screenshot | `0e8810a8621af6dcfa629f5380eadb9e34b54fd835905f717d8c3094a774a177` |
| MP engine screenshot | `710cf0b7314ba66309c5662cae2850aa5e0520096fab4cd039b0da96b2579dd3` |

The engine checkpoint is based on `9afac07e`; the unchanged companion remains
`300aedd9c56e20666ca7eacc0d71db70502660b9`. Capture metadata records module,
configuration and log hashes. This work adds no external implementation code.

## Remaining work

Resolve native variables and game actions, translate bindings and timelines to
the canonical runtime, implement the remaining widgets, classify materials and
bitmap exceptions, reconstruct vector artwork, and qualify every GUI family in
gameplay. Editor import must expose this source/diagnostic model while retaining
editable canonical layout/vector/motion data. The extensive visual editor,
all-GUI acceptance and broader platform qualification remain open.
