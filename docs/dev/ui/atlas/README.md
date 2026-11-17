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

Edit a part, not the built page. A new sheet needs its section in `sheets/`,
its script in `scripts/`, entries in `atlas.json` at the right place, and a link
in the navigation in `shell/head.html`. Scripts share one page, so class names
and element ids are global: prefix them per sheet.
