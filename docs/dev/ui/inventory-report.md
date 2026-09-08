# GUI replacement inventory

Date: 8 September 2026. Status: stage 1 partial implementation, not a conversion
or acceptance report. The [visual specification](../ui-visual-design.md) and
[full plan](../plans/idtech5-ui.md) remain the completion requirements.

## Reproduction

From the engine repository root, run:

```powershell
python tools/ui/legacy_inventory.py `
  --mount 'retail=C:\Program Files (x86)\Steam\steamapps\common\Quake 4\q4base' `
  --mount 'openq4-pak0=content/baseoq4/pak0' `
  --mount 'openq4-pak1=content/baseoq4/pak1' `
  --output .tmp/ui/legacy-inventory.json
python tools/tests/ui_legacy_inventory.py
```

The tool is platform-independent Python using the standard library. On other
hosts, supply the actual installed asset directory with the same explicit
mount order. It reads archives in place and writes metadata, never extracted
retail source or images. Full generated output stays under `.tmp/`.

The tracked [migration manifest](migration-manifest.json) is initialized using
the optional `--seed-manifest` argument. That operation refuses to overwrite an
existing manifest so later translation/acceptance evidence cannot be erased.
Each source hash binds evidence to the corresponding effective GUI content.
The optional `--export-requests` output feeds the subsequent
[native preprocessing and structured import](legacy-import.md) pipeline.

## Observed corpus

| Measurement | Result |
| --- | ---: |
| Effective GUI resources, including GUI include files | 271 |
| Lexical window declarations | 14,517 |
| Event declarations | 8,037 |
| Timeline event declarations | 5,497 |
| Named-event declarations | 530 |
| Unique referenced localization keys | 1,237 |
| Distinct artwork/font/model references | 1,222 |
| Missing include/cyclic include errors | 0 |
| Source fragments requiring brace review | 1 |

| Window type | Count |
| --- | ---: |
| `windowDef` | 14,131 |
| `choiceDef` | 152 |
| `editDef` | 86 |
| `listDef` | 66 |
| `bindDef` | 48 |
| `sliderDef` | 32 |
| `renderDef` | 2 |

The two model/render widgets are the player previews in `guis/mainmenu.gui`
and `guis/mpmain.gui`; they need functional rendering, not a static replacement.
The corpus includes 124 monitor GUIs, 59 map GUIs, 23 shared/common GUIs,
12 weapon GUIs, six mover GUIs, six vehicle GUIs and four model GUIs, in
addition to menus, HUDs and other root resources. All remain in migration scope
even when a resource's stock gameplay reachability is not yet established.

## What the tool proves

- Explicit low-to-high mounts; engine-compatible numbered/non-numbered PK4
  ordering; loose overrides; ignored official game-binary archives in q4base.
- Effective source selection, SHA-256 and all shadowed source locations for
  inventoried resources. Same-layer duplicates/case collisions are errors.
- Comment/string-aware lexical locations and declarations, includes and their
  dependency order, state/key references and script command tokens.
- Direct braced material declaration candidates and direct file/image/font
  candidates for GUI artwork references. Duplicate declarations remain
  ambiguous rather than being assigned an invented winner.
- Every migration seed starts pending, with no replacement, bitmap approval,
  behavioral acceptance or visual acceptance. Family hints are routing aids,
  not completed source/design review.

## What remains unproven

This is not an engine parser. It does not expand macros, evaluate conditions or
expressions, prove reachability, resolve every declaration guide, enumerate
all images inside multi-stage material programs or execute GUI scripts.
Lexical counts include include files as sources and are not counts of live
runtime instances. Console strings and dynamically supplied game state need
the semantic translation/behavior analysis stage.

Resource discovery only covers the supplied mounts. Savepath/developer loose
files are intentionally excluded from this canonical-source report, and actual
runtime mount diagnostics must be checked when capturing a baseline. A seed's
source hash must match fresh inventory before future evidence is accepted.

The 17 unresolved artwork references after lexical name disambiguation include
colored weapon/teammate symbols and console/test-loading imagery. They need
engine-generated/alias, source-presence or unused-resource investigation before
being called missing assets. No artwork has yet been classified as an approved
bitmap exception; simple furniture remains mandatory vector reconstruction.

`guis/maps/tram1/bridge1.gui` has an extra lexical closing brace. Its behavior
must be recorded before translation. Subsequent native preprocessing and
structured import consume its full token stream successfully. This lexical
observation is not evidence of a runtime failure or permission to drop it.

## Validation

`tools/tests/ui_legacy_inventory.py` exercises real temporary ZIP/directory
mounts, package priority, loose overrides, case collisions, ignored game
archives, comment/string adversaries, source locations, relative/root include
resolution, cycles, missing includes, malformed tokens, and material/file/
runtime-binding distinctions. Window names such as `online`, `one`, `font`
and `background` cannot masquerade as events or asset properties.

Before baseline launches, the Windows menu-cursor path was found to synchronize
the host pointer even with `in_mouse 0`. A guard now prevents routing from
disabled/hidden windows and prevents synchronization while unfocused. Native
tests execute the production functions in both Windows and POSIX branches over
32 state combinations plus an absent window. Full game captures require the
newly built guarded executable; an older staged binary is not sufficient.

## Initial gameplay reference captures

The MSVC-aware wrapper compiled the current client and staged it using
`install -C builddir --no-rebuild --skip-subprojects`. Client SHA-256:
`8e2f74e071877b849c1dde39e5885ee6839d623b796e6a958029836a4079c2b7`.

`tools/ui/capture_legacy_baseline.py` derives its mode-specific map arguments
from the SP airdefense1 / MP q4dm1 VS Code tasks, overrides the display with a
1280x720 hidden window, disables mouse/controller input, uses a new isolated
savepath and invokes the registered `screenshot` command three seconds after
active gameplay drawing begins, followed by `quit`. Each evidence directory
records exact arguments, binary hashes, log hash and screenshot hash. Hidden
window captures are limited to 60 Hz by the existing Windows presentation cap;
they do not establish high-refresh motion quality.

```powershell
python tools/ui/capture_legacy_baseline.py --mode sp --renderer gl --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' --output .tmp/ui/baseline/sp-gl-01
python tools/ui/capture_legacy_baseline.py --mode mp --renderer vulkan --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' --output .tmp/ui/baseline/mp-vulkan-01
```

Use new output directory names on repetition; existing evidence is not overwritten.

| Capture | Visual observation | Engine outcome |
| --- | --- | --- |
| `sp-gl-01` | Marine health/weapon HUD, crosshair and incoming-transmission overlay in airdefense1 gameplay | Exit 0; screenshot marker; no WARNING/ERROR records |
| `mp-vulkan-01` | MP health/ammo HUD, rank/timer/warmup overlays and crosshair after explicit automatic join | Exit 0; screenshot marker; 93 WARNING records, no ERROR records |

TGA SHA-256 values:

- SP: `1237198293f61707df86b6a8b950542d73e65250d86bd9319356a06a4184897b`
- MP: `76ae602768191be32f84817c593738fcdedbd1ee2e515ceef482dd998d697670`

Both engine images were decoded and visually inspected. PNG copies preserve
the captured pixels for convenient review; they are not OS captures. These are
usable existing-UI references, not acceptance evidence for the replacement.
The MP warnings include non-precached napalm GUI materials/tables and remain
open for the migration/qualification work; capture success is not a clean-log
claim. Logs/captures reside under each evidence directory's `save/baseoq4/`.

Remaining family gameplay captures, complete property/command classification,
vector/bitmap decisions and full conversion are outstanding. Stage 1 is not
complete merely because this inventory and two captures succeeded.
