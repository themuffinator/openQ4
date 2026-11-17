# Interface atlas source

The interface atlas is the illustrated companion to
[the visual design](../../ui-visual-design.md): every sheet draws a part of the
interface as editable vector art, with the sizes, colours, opacities and
timings the specification records, and lets you hover, focus and replay it.
It draws vector reconstructions only. It reads no game assets, and none of its
parts are extracted retail art.

`atlas.json` lists the page in order. Each entry is a part file or literal text
between parts:

| Folder | Holds |
| --- | --- |
| `shell/` | The page head, shared styles, header, navigation and the closing markup |
| `sheets/` | One file per sheet: its section markup and its own styles |
| `scripts/` | The drawing scripts, in the order the page runs them; `core.js` defines `window.__atlas` and its shared helpers |
| `styles/` | Styles that sit between scripts |
| this folder | Sheets linked from the specification, which keep their paths: `awakening-menu.html`, `medlabs-implant.html`, `audio-waveforms.js`, `weapon-wheel-placement.js` |

Build the page with:

```bash
python tools/ui/update_design_atlas.py
```

It writes `.tmp/atlas/openq4-atlas.html`. `--check` confirms every listed part
exists and every part file is listed; `--compare <file>` reports the first
difference from an earlier build.

## What the sheets draw

Each sheet draws the retained interface (`ui_retained 1`) as its generator in
`tools/ui/` builds it, checked against engine captures made with the
`tools/ui/capture_*.py` harnesses. Where the implementation and the
specification disagree, the sheet draws the implementation and its caption
names the difference. Parts the specification asks for that are not built
are tagged "not built". The refresh of 17 November 2026 found these
differences; they are the specification's or the implementation's to resolve,
not the atlas's:

- Title (13.8, C 1.43): the 0.60 Bayer dither was taken out of `title.rui`
  (e983d4f63); the emblem has a charge, flare and reticle instead of the 9 s glint.
- Settings (13.7): rows are closed outlined plates and nothing marks a
  modified row; dropdowns are as wide as the row, not the value column; the
  header band's cut is 9 u and the tab strip a white 0.75 outline rising 30 dp.
- Tokens: the modal glow is `#454E1B` in the generators, `#46501B` in section 4.
- DISPLAY's countdown is orange Marine with no tick ramp; notices sit 14 dp
  from the top over the band; there is no More action or binding-conflict dialog.
- Loading (14.17): the objectives are one text block, the identity line one
  item, the progress bar's cut is upper-leading, and touch shows the keyboard prompt.
- Initializing (14.17): four clamps on the diagonals, the emblem at y 125, a
  stepped-stroke glow, no rim arcs on the sweep, the status line at 412 u.
- Multiplayer cards (14.18): 873×477 and 907×477 dp to fit the widest
  language; tabs share the strip's width; unavailable actions rest at 0.60; the
  summary's prompt bar always shows keycaps; the buy card is not in the spec.
- HUD (14.7, 14.15): hit markers use `#F22B24` and `#FFE0D1` with team and
  splash tiers and default off; the animated crosshair, on by default, replaces
  the stock hit pop and is not in the specification; `ui_hudLayout` has no
  Touch value; quick chat has no Strogg translation.
- Objectives display: three entries plus COMPLETED are 485 u tall, above the
  480 u view.

Edit a part, not the built page. A new sheet needs its section in `sheets/`,
its script in `scripts/`, entries in `atlas.json` at the right place, and a link
in the navigation in `shell/head.html`. Scripts share one page, so class names
and element ids are global: prefix them per sheet.
