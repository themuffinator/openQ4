#!/usr/bin/env python3
"""Generate the experimental level editor workspace GUI.

The workspace keeps Quake 4's stock menu art (b6_light buttons, tooltip
frames) and localized #str labels. Panels are containers whose rectangles
come from the docking layout the engine publishes as gui:: state, so the
same windows work in any dock. Run with --check to verify the committed file.
"""
from __future__ import annotations

import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "content/baseoq4/pak0/guis/level_editor_experimental.gui"

ORANGE = "0.890,0.537,0,1"
MUTED = "0.72,0.76,0.63,1"
FONT = 'font "fonts/lowpixel" textspacing -1'


def button(name: str, x: str, y: str, w: str, h: str, label: str, action: str, scale: float = 0.16) -> str:
    """A stock menu button with the main menu's hover transitions."""
    return (
        f'windowDef {name} {{ rect ({x}),({y}),({w}),({h}) noevents "gui::ed_confirm" background "gfx/guis/mainmenu/b6_light" matcolor 0.545,0.588,0.294,0.62\n'
        f' onMouseEnter {{\n'
        f'  if ( "gui::ed_confirm" == 0 ) {{\n'
        f'   stoptransitions "{name}";\n'
        f'   set "{name}::matcolor_w" "1";\n'
        f'   transition "{name}_label::forecolor" "$desktop::white_8" "$desktop::orange" "0";\n'
        f'   transition "{name}_corner::matcolor" "$desktop::orange_4" "$desktop::orange" "0";\n'
        f'   set "cmd" "play main_menu_mouseover";\n'
        f'  }}\n'
        f' }}\n'
        f' onMouseExit {{\n'
        f'  transition "{name}::matcolor_w" "1" "0.62" "300";\n'
        f'  transition "{name}_label::forecolor" "$desktop::orange" "$desktop::white_8" "300";\n'
        f'  transition "{name}_corner::matcolor" "$desktop::orange" "$desktop::orange_4" "300";\n'
        f' }}\n'
        f' onAction {{ set "cmd" "play main_menu_selection ; editorExperimentalAction {action}"; }} }}\n'
        f'windowDef {name}_label {{ rect ({x})+10,({y})+3,({w})-12,({h}) noevents 1 text "{label}" {FONT} textscale {scale:.2f} forecolor 1,1,1,0.8 }}\n'
        f'windowDef {name}_corner {{ rect ({x})+3,({y})+({h})/2-3,(6),(6) noevents 1 background "gfx/guis/mainmenu/corner" matcolor 0.890,0.537,0,0.4 }}\n'
    )


def label(name: str, x: str, y: str, w: str, h: str, text: str, scale: float = 0.15, color: str = MUTED) -> str:
    return f'windowDef {name} {{ rect ({x}),({y}),({w}),({h}) noevents 1 text "{text}" {FONT} textscale {scale:.2f} forecolor {color} }}\n'


def frame(name: str, x: str, y: str, w: str, h: str) -> str:
    return (
        f'windowDef {name}_body {{ rect ({x})+4,({y})+4,({w})-8,({h})-8 noevents 1 background "gfx/guis/mainmenu/screen" matcolor 0.09,0.11,0.07,0.88 }}\n'
        f'windowDef {name}_grad {{ rect ({x})+4,({y})+4,({w})-8,({h})-8 noevents 1 background "gfx/guis/mainmenu/bg_darkgrad" matcolor 0.545,0.588,0.294,0.14 }}\n'
        f'windowDef {name}_top {{ rect ({x}),({y}),({w}),(16) noevents 1 background "gfx/guis/mainmenu/tooltip_edge" matcolor 1,1,1,0.95 }}\n'
        f'windowDef {name}_mid {{ rect ({x}),({y})+16,({w}),({h})-32 noevents 1 background "gfx/guis/mainmenu/tooltip_mid" matcolor 1,1,1,0.95 }}\n'
        f'windowDef {name}_btm {{ rect ({x}),({y})+({h})-16,({w}),(16) noevents 1 background "gfx/guis/mainmenu/tooltip_edge" matscalex -1 matscaley -1 matcolor 1,1,1,0.95 }}\n'
    )


def edit(name: str, x: str, y: str, w: str, h: str, var: str, maxchars: int, action: str | None) -> str:
    on_enter = f'\n onEnter {{ set "cmd" "editorExperimentalAction {action}"; }}' if action else ''
    return (
        f'editDef {name} {{ rect ({x}),({y}),({w}),({h})\n'
        f' text "gui::{var}" noevents "gui::ed_confirm" maxchars {maxchars} {FONT} textscale .16\n'
        f' forecolor 1,1,1,0.9 backcolor 0.035,0.045,0.025,0.95 bordercolor 0.3,0.34,0.15,0.5 bordersize 1{on_enter} }}\n'
    )


def listdef(name: str, x: str, y: str, w: str, h: str, action: str) -> str:
    return (
        f'listDef {name} {{ rect ({x}),({y}),({w}),({h})\n'
        f' listname {name} noevents "gui::ed_confirm" itemheight 14 scrollbar 1 maxitems 65536\n'
        f' {FONT} textscale .15 forecolor 0.92,0.94,0.88,1 hovercolor 1,0.66,0.18,1\n'
        f' backgroundHover "gfx/guis/mainmenu/bg_hover" backgroundLine "gfx/guis/mainmenu/bg_line"\n'
        f' backgroundFocus "gfx/guis/mainmenu/bg_focus" backgroundGreyed "gfx/guis/mainmenu/bg_line"\n'
        f' onAction {{ set "cmd" "editorExperimentalAction {action}"; }}\n'
        f' onSelChange {{ set "cmd" "editorExperimentalAction {action}"; }}\n'
        f' onEnter {{ set "cmd" "editorExperimentalAction {action}"; }} }}\n'
    )


def toolbar(row: str, y: int, items: list[tuple], start: float = 8, span: float = 616, scale: float = 0.15) -> str:
    """Buttons share the row by weight (an optional fourth tuple member)."""
    out = ''
    weights = [item[3] if len(item) > 3 else 1.0 for item in items]
    unit = (span - 4 * (len(items) - 1)) / sum(weights)
    x = start
    for (name, text, action, *_), weight in zip(items, weights):
        width = unit * weight
        out += button(f'{row}_{name}', f'{x:.1f}', str(y), f'{width:.1f}', '22', text, action, scale)
        x += width + 4
    return out


def panel(name: str, title: str, docks: list[str], body: str) -> str:
    """A docked panel: a title tab with dock controls, and a content container."""
    tab = f'"gui::ed_{name}_tab_x"', f'"gui::ed_{name}_tab_y"', f'"gui::ed_{name}_tab_w"', f'"gui::ed_{name}_tab_h"'
    out = (
        f'windowDef {name}Tab {{ rect ({tab[0]}),({tab[1]}),({tab[2]}),({tab[3]}) visible "gui::ed_{name}_tab_vis"\n'
        f'windowDef {name}Tab_back {{ rect 0,0,({tab[2]})-1,({tab[3]}) noevents 1 background "gfx/guis/mainmenu/b6_light" matcolor 0.545,0.588,0.294,(0.25+0.4*"gui::ed_{name}_active") }}\n'
    )
    controls = [d for d in docks] + ['hidden']
    glyph = {'left': '#str_42939', 'right': '#str_42940', 'bottom': '#str_42995', 'hidden': '#str_42996'}
    # Only the active tab carries the dock controls, so shared docks keep
    # room for their titles.
    # The title spans the tab; the active tab adds its dock buttons on a
    # second line, so titles keep their width however a dock is shared.
    small = 12
    active = f'"gui::ed_{name}_active"'
    out += button(f'{name}Tab_title', '0', '0', f'({tab[2]})-1', '16', title, f'tab_{name}', 0.13)
    for i, dock in enumerate(controls):
        x = f'({tab[2]})-{(len(controls) - i) * (small + 1)}'
        out += (
            f'windowDef {name}Tab_{dock} {{ rect ({x}),(16),({small}),(12) visible {active} noevents "gui::ed_confirm" background "gfx/guis/mainmenu/b6_light" matcolor 0.545,0.588,0.294,0.5\n'
            f' onAction {{ set "cmd" "play main_menu_selection ; editorExperimentalAction dock_{name}_{dock}"; }} }}\n'
            f'windowDef {name}Tab_{dock}_label {{ rect ({x})+3,(14),({small})-2,(13) visible {active} noevents 1 text "{glyph[dock]}" {FONT} textscale .12 forecolor 1,1,1,0.85 }}\n'
        )
    out += '}\n'
    box = f'"gui::ed_{name}_x"', f'"gui::ed_{name}_y"', f'"gui::ed_{name}_w"', f'"gui::ed_{name}_h"'
    out += f'windowDef {name}Panel {{ rect ({box[0]}),({box[1]}),({box[2]}),({box[3]}) visible "gui::ed_{name}_vis"\n'
    out += frame(f'{name}_frame', '0', '0', box[2], box[3])
    out += body.replace('@W', box[2]).replace('@H', box[3])
    out += '}\n'
    return out


def browser() -> str:
    out = ''
    modes = [('entities', '#str_42908'), ('maps', '#str_42909'), ('classes', '#str_42910'),
             ('materials', '#str_42969'), ('models', '#str_42970'), ('sounds', '#str_42971')]
    for i, (name, text) in enumerate(modes):
        # Two per row: asset words are long in several languages.
        col, row = i % 2, i // 2
        out += button(f'browse_{name}', f'4+{col}*((@W)-8)/2', f'{4 + row * 20}', '((@W)-8)/2-2', '18', text, f'browse_{i}', 0.14)
    out += edit('ed_filter', '6', '66', '(@W)-12', '18', 'ed_filter', 120, 'filter')
    out += label('filter_hint', '6', '85', '(@W)-12', '13', '#str_42929', 0.13)
    out += listdef('edBrowser', '6', '100', '(@W)-12', '(@H)-100-24', 'select')
    out += f'windowDef browser_count {{ rect (6),((@H)-18),((@W)-12),(14) noevents 1 text "gui::ed_browserCount" {FONT} textscale .14 forecolor {MUTED} }}\n'
    return out


def inspector() -> str:
    out = f'windowDef selected {{ rect (6),(4),((@W)-12),(14) noevents 1 text "gui::ed_selected" {FONT} textscale .14 forecolor {MUTED} }}\n'
    out += f'windowDef usage {{ rect (6),(18),((@W)-12),(24) noevents 1 text "gui::ed_usage" {FONT} textscale .12 forecolor 0.6,0.64,0.52,1 }}\n'
    out += listdef('edProperties', '6', '44', '(@W)-12', '(@H)-44-136', 'property')
    out += label('key_label', '6', '(@H)-132', '40', '18', '#str_42912', 0.14)
    out += edit('ed_key', '46', '(@H)-132', '(@W)-52', '18', 'ed_key', 4096, 'key')
    out += label('value_label', '6', '(@H)-112', '40', '18', '#str_42913', 0.14)
    out += edit('ed_value', '46', '(@H)-112', '(@W)-52', '18', 'ed_value', 4096, 'apply')
    out += f'windowDef key_type {{ rect (6),((@H)-92),((@W)-12),(22) noevents 1 text "gui::ed_keyType" {FONT} textscale .12 forecolor 0.6,0.64,0.52,1 }}\n'
    out += button('apply', '6', '(@H)-68', '((@W)-16)/2', '20', '#str_42914', 'apply')
    out += button('unset', '10+((@W)-16)/2', '(@H)-68', '((@W)-16)/2', '20', '#str_42915', 'unset')
    out += edit('ed_rename', '6', '(@H)-44', '((@W)-16)*0.55', '18', 'ed_rename', 256, 'rename')
    out += button('rename', '10+((@W)-16)*0.55', '(@H)-45', '((@W)-16)*0.45', '20', '#str_42983', 'rename', 0.14)
    out += f'windowDef refs {{ rect (6),((@H)-22),((@W)-12),(14) noevents 1 text "gui::ed_refs" {FONT} textscale .13 forecolor {MUTED} }}\n'
    return out


def tools() -> str:
    out = ''
    rows = [('class', '#str_42916', 'ed_class', 'create', 2), ('name', '#str_42917', 'ed_name', 'create', 22)]
    for name, text, var, action, y in rows:
        out += label(f'{name}_label', '6', str(y), '46', '18', text, 0.14)
        out += edit(f'ed_{name}', '54', str(y), '(@W)-60', '18', var, 256, action)
    out += button('create', '6', '42', '((@W)-16)/2', '18', '#str_42918', 'create')
    out += button('delete', '10+((@W)-16)/2', '42', '((@W)-16)/2', '18', '#str_42919', 'delete')
    out += label('material_label', '6', '64', '46', '18', '#str_42985', 0.14)
    out += edit('ed_material', '54', '64', '(@W)-60', '18', 'ed_material', 256, 'material')
    out += button('apply_material', '6', '84', '(@W)-12', '18', '#str_42986', 'material')
    out += label('layer_label', '6', '106', '46', '18', '#str_42974', 0.14)
    out += edit('ed_layer', '54', '106', '(@W)-60', '18', 'ed_layer', 64, 'layer')
    out += button('set_layer', '6', '126', '(@W)-12', '18', '#str_42975', 'layer')
    out += listdef('edLayers', '6', '148', '(@W)-12', '(@H)-148-80', 'layerSelect')
    # Toggles for the selected layer row (or the layer field).
    for i, (name, text) in enumerate([('layerVisibility', '#str_43135'), ('layerLocking', '#str_43136')]):
        out += button(f'{name}', f'6+{i}*((@W)-12)/2', '(@H)-76', '((@W)-12)/2-2', '18', text, name, 0.13)
    out += label('prefab_label', '6', '(@H)-54', '46', '18', '#str_42980', 0.14)
    out += edit('ed_prefab', '54', '(@H)-54', '(@W)-60', '18', 'ed_prefab', 120, 'prefabSave')
    out += button('prefab_save', '6', '(@H)-32', '((@W)-16)/2', '18', '#str_42981', 'prefabSave', 0.14)
    out += button('prefab_insert', '10+((@W)-16)/2', '(@H)-32', '((@W)-16)/2', '18', '#str_42982', 'prefabInsert', 0.14)
    return out


def problems() -> str:
    out = f'windowDef build_status {{ rect (6),(4),(((@W)-12)*0.6),(14) noevents 1 text "gui::ed_buildStatus" {FONT} textscale .14 forecolor {ORANGE} }}\n'
    out += f'windowDef problem_count {{ rect (6+((@W)-12)*0.6),(4),(((@W)-12)*0.4),(14) noevents 1 text "gui::ed_problemCount" {FONT} textscale .14 forecolor {MUTED} }}\n'
    out += 'windowDef build_bar { rect (6),(19),(("gui::ed_buildProgress")*((@W)-12)),(3) noevents 1 visible "gui::ed_building" backcolor 0.890,0.537,0,0.9 }\n'
    out += listdef('edProblems', '6', '24', '(@W)-12', '(@H)-28', 'problem')
    return out


def views() -> str:
    v = '"gui::ed_view_x"', '"gui::ed_view_y"', '"gui::ed_view_w"', '"gui::ed_view_h"'
    out = frame('view_frame', v[0], v[1], v[2], v[3])
    out += f'editorViewDef authoringView {{ rect ({v[0]})+6,({v[1]})+6,({v[2]})-12,({v[3]})-12 viewmode -1 visible "gui::ed_single" noevents "gui::ed_confirm" }}\n'
    for i, (col, row) in enumerate([(0, 0), (1, 0), (0, 1), (1, 1)]):
        out += (f'editorViewDef quadView{i} {{ rect ({v[0]})+6+{col}*(({v[2]})-12)/2,({v[1]})+6+{row}*(({v[3]})-12)/2,'
                f'(({v[2]})-12)/2-2,(({v[3]})-12)/2-2 viewmode {i} visible "gui::ed_quad" noevents "gui::ed_confirm" }}\n')
    for name, prefix, pane in [('leftSplitter', 'ed_lsplit', 1), ('rightSplitter', 'ed_rsplit', 2), ('bottomSplitter', 'ed_bsplit', 3)]:
        art = 'background "gfx/guis/mainmenu/vert_line" matcolor 0.545,0.588,0.294,0.2' if pane != 3 else 'backcolor 0.545,0.588,0.294,0.12'
        out += (f'editorViewDef {name} {{ rect ("gui::{prefix}_x"),("gui::{prefix}_y"),("gui::{prefix}_w"),("gui::{prefix}_h") splitter {pane} '
                f'visible "gui::{prefix}_vis" noevents "gui::ed_confirm" {art} }}\n')
    return out


def confirm() -> str:
    out = 'windowDef confirmPanel { rect 0,0,640,480 modal 1 visible "gui::ed_confirm" backcolor 0,0,0,0.65\n'
    out += frame('confirm_frame', '144', '181', '352', '116')
    out += label('confirm_title', '161', '204', '314', '31', '#str_42934', 0.23, '1,1,1,0.9')
    for name, x, text, action in [('confirm_discard', '162', '#str_42935', 'confirm'), ('confirm_cancel', '324', '#str_42936', 'cancel')]:
        out += (f'windowDef {name} {{ rect ({x}),(253),(144),(28)  background "gfx/guis/mainmenu/b6_light" matcolor 0.545,0.588,0.294,0.62\n'
                f' onAction {{ set "cmd" "play main_menu_selection ; editorExperimentalAction {action}"; }} }}\n'
                f'windowDef {name}_label {{ rect ({x})+12,(253+4),(144-14),(28) noevents 1 text "{text}" {FONT} textscale .20 forecolor 1,1,1,0.8 }}\n')
    out += '}\n'
    return out


def build() -> str:
    out = '// Experimental level editor workspace; stock Quake 4 artwork only.\n'
    out += '// Generated by tools/ui/build_level_editor_gui.py; edit the generator.\n'
    out += ('windowDef Desktop {\n rect 0,0,640,480\n menugui 1\n backcolor 0.025,0.032,0.02,1\n'
            ' definevec4 white_8 1,1,1,0.8\n definevec4 orange 0.890,0.537,0,1\n definevec4 orange_4 0.890,0.537,0,0.4\n'
            ' onESC {\n  if ( "gui::ed_confirm" == 1 ) { set "cmd" "editorExperimentalAction cancel"; }\n'
            '  else { set "cmd" "editorExperimentalAction hide"; }\n }\n'
            ' onNamedEvent workspaceConfirm { setFocus "confirm_cancel"; }\n'
            ' onNamedEvent workspaceResume { setFocus "edBrowser"; }\n')
    out += label('heading', '8', '3', '150', '22', '#str_42900', 0.27, ORANGE)
    out += label('experimental', '160', '7', '100', '16', '#str_42946', 0.16, ORANGE)
    out += f'windowDef document {{ rect (264),(7),(268),(16) noevents 1 text "gui::ed_title" {FONT} textscale .16 forecolor 0.72,0.76,0.63,1 }}\n'
    out += f'windowDef dirty {{ rect (536),(7),(96),(16) noevents 1 text "gui::ed_dirty" {FONT} textscale .16 forecolor {ORANGE} }}\n'
    out += toolbar('file', 26, [('new', '#str_42901', 'new'), ('open', '#str_42902', 'open'), ('save', '#str_42903', 'save', 1.15),
                               ('copy', '#str_42907', 'copy', 1.45), ('undo', '#str_42904', 'undo', 1.2), ('redo', '#str_42905', 'redo', 1.2),
                               ('build', '#str_42947', 'build'), ('play', '#str_42948', 'play'), ('stop', '#str_42949', 'stop'),
                               ('hide', '#str_42906', 'hide', 1.8)])
    out += label('path_label', '8', '53', '36', '18', '#str_42933', 0.15)
    out += edit('ed_path', '46', '52', '484', '20', 'ed_path', 220, 'open')
    out += button('close_map', '536', '51', '88', '22', '#str_42937', 'close', 0.15)
    out += toolbar('view', 76, [('view3d', '#str_42950', 'mode_0'), ('top', '#str_42951', 'mode_1'), ('front', '#str_42952', 'mode_2'),
                               ('side', '#str_42953', 'mode_3'), ('quad', '#str_42955', 'quad'), ('gridDown', '#str_42956', 'gridDown'),
                               ('gridUp', '#str_42957', 'gridUp'), ('validate', '#str_42959', 'validate'), ('frame', '#str_42930', 'frame'),
                               ('panels', '#str_42960', 'panels')])
    out += f'windowDef lock_state {{ rect (8),(100),(80),(22) noevents "gui::ed_confirm" background "gfx/guis/mainmenu/b6_light" matcolor 0.545,0.588,0.294,(0.3+0.5*"gui::ed_lock")\n onAction {{ set "cmd" "play main_menu_selection ; editorExperimentalAction lock"; }} }}\n'
    out += f'windowDef lock_state_label {{ rect (16),(103),(70),(22) noevents 1 text "gui::ed_lockText" {FONT} textscale .14 forecolor 1,1,1,0.8 }}\n'
    # Weighted by the longest translations (see the fit phase of
    # tools/tests/level_editor_authoring.py).
    out += toolbar('edit', 100, [('brush', '#str_42961', 'brush', 0.85), ('patch', '#str_42962', 'patch', 0.85),
                                ('duplicate', '#str_42963', 'duplicate', 1.15), ('remove', '#str_42919', 'remove'),
                                ('group', '#str_42964', 'group'), ('ungroup', '#str_42965', 'ungroup', 1.2),
                                ('rotateCcw', '#str_42966', 'rotateCcw', 1.2), ('rotateCw', '#str_42967', 'rotateCw', 1.2),
                                ('snap', '#str_42968', 'snap')], start=92, span=532, scale=0.14)
    out += views()
    out += panel('browser', '#str_43133', ['left', 'right'], browser())
    out += panel('inspector', '#str_42911', ['left', 'right'], inspector())
    out += panel('tools', '#str_42972', ['left', 'right'], tools())
    out += panel('problems', '#str_42973', ['left', 'right', 'bottom'], problems())
    out += f'windowDef message {{ rect (8),(441),(624),(16) noevents 1 text "gui::ed_message" {FONT} textscale .15 forecolor {ORANGE} }}\n'
    out += f'windowDef hint {{ rect (8),(459),(624),(16) noevents 1 text "gui::ed_hint" {FONT} textscale .13 forecolor {MUTED} }}\n'
    out += confirm()
    out += '}\n'
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='fail if the committed GUI differs')
    args = parser.parse_args()
    text = build()
    if args.check:
        current = OUTPUT.read_text(encoding='utf-8').replace('\r\n', '\n') if OUTPUT.exists() else ''
        if current != text:
            print(f'{OUTPUT.relative_to(ROOT)} is out of date; run tools/ui/build_level_editor_gui.py')
            return 1
        print('level editor GUI is current')
        return 0
    OUTPUT.write_text(text, encoding='utf-8', newline='\n')
    print(f'wrote {OUTPUT.relative_to(ROOT)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
