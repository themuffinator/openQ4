"""The retained multiplayer menus (docs/dev/ui-visual-design.md section 14.18).

Built by tools/ui/build_retained_screens.py, which writes and checks them with
the other retained screens. The Escape card covers the game's multiplayer menu
(mpmain.gui) while a match runs: a card centered over the softened view with a
header, a horizontal strip of eight tabs, the page and a prompt bar. The
Welcome card covers it while the player has not answered the join offer, with
Join, Server, Players and Settings. A page that is not built yet hands off to
its stock page through the mpStockPage verb, and the session keeps each card
opt-in (ui_retainedMultiplayer) while any of its pages does
(RETAINED_MP_ESCAPE_MISSING_PAGES, RETAINED_MP_WELCOME_MISSING_PAGES).

The tab strip cannot fit eight Marine labels in the specified 648 dp card in
any shipped language: with the active tab's flare and shoulder, even the
shortest English set needs about 860 dp. The Escape card therefore grows to
the widest language's fitted strip, never past a 4:3 view's 16 dp margins,
and generation fails when a language would need more (docs/dev/ui/
retained-screens.md, Multiplayer menus).
"""
from __future__ import annotations

import math
import re
from pathlib import Path

import build_retained_screens as b
import hud_svg
from build_retained_screens import (EASE_OUT, FULL, MARKER, OLIVE, ORANGE, RAIL_CARD, SOFT_FOCUS_BLUR,
                                    SOFT_FOCUS_SATURATION, VALUE, Document, absolute, colour, confirmation, group,
                                    keyword, label, length, linear, link, marker_path, number, path, picture, rgb, solid,
                                    stroke, track, transform, typeface, vector)

ROOT = Path(__file__).resolve().parents[2]
STRINGS = ROOT / "content" / "baseoq4" / "pak0" / "strings"
# The maps a game type offers the Vote and Admin pages' map choices: the
# game's own cap (idMultiplayerGame::RETAINED_VOTE_MAPS), so the two agree.
MAP_CATALOG = int(re.search(r"static const int RETAINED_VOTE_MAPS = (\d+);",
                            (ROOT / "src" / "mpgame" / "MultiplayerGame.h").read_text(encoding="utf-8")).group(1))

TAB_GREEN = "#5A652A"          # the active tab's fill (Appendix B.1, ctrls_tab2-4)
BODY = [0, 0, 0, 0.94]         # the card's body (section 6, panel anatomy)
VIEW_43 = 960.0                # the narrowest landscape view the card must clear, in dp
SIDE_MARGIN = 16.0             # the card never comes closer than this to the view's sides
ESCAPE_MIN_W, ESCAPE_H = 648.0, 477.0
HEADER_H = 40.0
CUT_MAJOR, CUT_MINOR = 12.0, 3.0
INSET = 24.0                   # page content inset
TAB_TOP, TAB_RISE = 52.0, 30.0
TAB_FLARE, TAB_SHOULDER, TAB_PAD = 3.0, 18.0, 5.0
TAB_SIZE = 14.0                # Marine label size in dp (decision D7: the spec gives none)
STRIP_EDGE = 12.0              # the strip's distance from the card's sides
KEYCAP_W, KEYCAP_GAP = 20.0, 6.0
PAGE_TOP = TAB_TOP + TAB_RISE + 16.0
PROMPT_H = 22.0
PROMPT_BOTTOM = 16.0
PAGE_FADE_MS = 150
OPEN_MS, RISE_MS, REDUCED_MS, RELEASE_MS = 250, 150, 80, 250
RISE_DP = 12.0
PLACEHOLDER = "#str_230030"    # bound labels' text before their first binding

# The eight Escape tabs in order: (page id, tab label, the stock menu button
# the interim hand-off presses). The session's RETAINED_MP_STOCK_PAGES lists
# the same buttons in the same order.
ESCAPE_TABS = [
    ("team", "#str_231001", "main_b_jointeam"),
    ("players", "#str_231002", "main_b_players"),
    ("vote", "#str_231003", "main_b_vote"),
    ("match", "#str_231004", "main_b_matchcontrol"),
    ("settings", "#str_231005", "main_b_settings"),
    ("voice", "#str_231006", "main_b_voiceconfig"),
    ("server", "#str_231007", "main_b_serverinfo"),
    ("admin", "#str_231008", "main_b_admin"),
]
KEY_PREVIOUS, KEY_NEXT = "#str_231009", "#str_231010"
VERB_TABS = "#str_231011"
HANDOFF_NOTE, HANDOFF_ACTION = "#str_231012", "#str_231013"
KEY_ESCAPE, KEY_ENTER = "#str_107020", "#str_107019"
VERB_RESUME, VERB_SELECT = "#str_200381", "#str_231042"
MAIN_MENU, DISCONNECT, DISCONNECT_BODY = "#str_200188", "#str_200006", "#str_200173"


def table_text(key: str) -> dict:
    """`key`'s text in every shipped language's openQ4 table."""
    texts = {}
    for language in b.LANGUAGES:
        for line in (STRINGS / f"{language}_openq4.lang").read_text(encoding="utf-8").splitlines():
            match = re.match(r'\s*"(#str_\d+)"\s+"(.*)"\s*$', line)
            if match and match.group(1) == key:
                texts[language] = match.group(2)
    missing = sorted(set(b.LANGUAGES) - set(texts))
    if missing:
        raise SystemExit(f"{key} is missing from the {', '.join(missing)} openQ4 tables")
    return texts


def tab_slot(label_width: float) -> float:
    """A tab's slot: the leading flare and padding, the label, the padding and
    the trailing shoulder, so the active silhouette never reaches a neighbour."""
    return TAB_FLARE + TAB_PAD + label_width + TAB_PAD + TAB_SHOULDER


def label_width(key: str, language: str) -> float:
    return b.text_width("marine", table_text(key)[language], TAB_SIZE)


def strip_chrome() -> float:
    """The strip's width beside its tabs: its edges and the two keycaps."""
    return 2 * (STRIP_EDGE + KEYCAP_W + KEYCAP_GAP)


def fitted_card_width(keys: list, prompts: tuple | None = None) -> float:
    """A card's width: the specified 648 dp, or the widest language's tabs
    with the strip's keycaps and edges, or the widest language's prompt bar
    (`prompts`: the back verb and the trailing links) inside the card's
    insets, each with a 2 % margin against layout rounding, rounded up to a
    whole dp. Fails when that needs more than a 4:3 view allows."""
    strips = {language: sum(tab_slot(label_width(key, language)) for key in keys) for language in b.LANGUAGES}
    needed = max(strips.values()) * 1.02 + strip_chrome()
    if prompts is not None:
        # Measured exactly as laid out, with 4 dp to spare. A controller's
        # glyphs (section 13.5) must fit wherever the keycaps do.
        bars = {language: prompt_width(language, *prompts) for language in b.LANGUAGES}
        needed = max(needed, max(bars.values()) + 4 + 2 * INSET)
        glyph_bars = {language: prompt_width(language, *prompts, glyphs=True) for language in b.LANGUAGES}
        if max(glyph_bars.values()) > max(bars.values()):
            raise SystemExit("the card's controller prompt bar is wider than its keycap bar: "
                             + ", ".join(f"{language} {round(width)}" for language, width in glyph_bars.items()))
    limit = VIEW_43 - 2 * SIDE_MARGIN
    if needed > limit:
        raise SystemExit(f"the card needs {needed:.0f} dp, past a 4:3 view's {limit:g} dp; tab strips: "
                         + ", ".join(f"{language} {round(width)}" for language, width in strips.items()))
    return max(ESCAPE_MIN_W, float(int(needed + 0.999)))


def prompt_width(language: str, back_verb: str, links: list, glyphs: bool = False) -> float:
    """The prompt bar's natural width in `language` (prompt_row's layout):
    each keycap padded 7 dp a side, or with `glyphs` each controller button's
    22 dp, with 3 dp between caps and 6 dp after the last, each verb with
    18 dp after it, then the trailing links, 10.5 dp of marker before each
    label and 21 dp between them."""
    def text(key: str) -> str:
        return any_text(key)[language]

    def caps(keys: list) -> float:
        widths = [PROMPT_H if glyphs else 14 + b.text_width("lowpixel", text(key), 13) for key in keys]
        return sum(widths) + 3 * (len(keys) - 1) + 6
    width = 0.0
    for keys, verb in (([KEY_ESCAPE], back_verb), ([KEY_PREVIOUS, KEY_NEXT], VERB_TABS), ([KEY_ENTER], VERB_SELECT)):
        width += caps(keys) + b.text_width("marine", text(verb), 14) + 18
    for index, key in enumerate(links):
        width += 10.5 + b.text_width("marine", text(key), 16) + (21 if index < len(links) - 1 else 0)
    return width


# ------------------------------------------------------------------- the card

def card_frame(width: float, height: float) -> dict:
    """The section 6 card: a black body at 0.94 inside one marine.rail.card
    rail, 12 dp cuts at the top-trailing and bottom-leading corners and 3 dp
    cuts at the other two."""
    def outline(inset: float) -> list:
        d = inset * 0.4142   # a 45-degree corner moves this far along each side per unit of inset
        return [(CUT_MINOR + d, inset), (width - CUT_MAJOR - d, inset), (width - inset, CUT_MAJOR + d),
                (width - inset, height - CUT_MINOR - d), (width - CUT_MINOR - d, height - inset),
                (CUT_MAJOR + d, height - inset), (inset, height - CUT_MAJOR - d), (inset, CUT_MINOR + d)]
    return vector("card-frame", absolute(left=0, top=0, width=width, height=height), [
        path("body", outline(0), fill=solid(BODY)),
        path("rail", outline(0.75), stroke=stroke(solid(rgb(RAIL_CARD)), 1.5)),
    ])


def bound_text(ident: str, doc: Document, state: str, face: str, size: float, tint: list) -> dict:
    node = label(ident, PLACEHOLDER, {"position": keyword("relative"), "display": keyword("block"),
                                      **typeface(face, size, HEADER_H, tint), "white-space": keyword("nowrap")})
    doc.bind(f"{ident}.text", ident, "text", {"state": state})
    return node


def card_header(doc: Document, width: float, title_state: str, trailing: list) -> list:
    """The 40 dp header (sections 6 and 14.18): a white 0.16 wash in the light
    plate's shape fading toward the trailing edge, a 1 dp rule below, the
    8 dp triangle and the title in Marine, and the trailing texts."""
    band_top, band_bottom, cut = 3.0, HEADER_H - 3.0, 19.0
    wash = vector("header-wash", absolute(left=1.5, top=0, width=width - 3, height=HEADER_H), [
        path("wash", [(0, band_top), ({"fraction": 1}, band_top), ({"fraction": 1}, band_bottom), (cut, band_bottom),
                      (0, band_bottom - cut)],
             fill=linear((0, 0), ({"fraction": 1}, 0), [(0, [1, 1, 1, 0.16]), (0.5, [1, 1, 1, 0.16]), (1, [1, 1, 1, 0])])),
    ])
    rule = vector("header-rule", absolute(left=1.5, top=HEADER_H, width=width - 3, height=1), [
        path("rule", [(0, 0.5), ({"fraction": 1}, 0.5)], closed=False, stroke=stroke(solid(rgb(RAIL_CARD, 0.8)), 1)),
    ])
    # One row: the triangle, the title taking whatever the trailing texts
    # leave and clipping there, then the trailing texts at their own widths.
    triangle = vector("header-triangle", {"position": keyword("relative"), "display": keyword("block"), "width": length(8),
                                          "height": length(8), "flex-shrink": number(0), "margin-right": length(-8)},
                      [marker_path("mark", 0, 0, 8, rgb(MARKER))])
    title = label("header-title", PLACEHOLDER, {"position": keyword("relative"), "display": keyword("block"), "flex-grow": number(1),
                  "flex-shrink": number(1), "min-width": length(0), "height": length(HEADER_H), "overflow": keyword("hidden"),
                  **typeface("marine", 20, HEADER_H, [1, 1, 1, 0.9]), "white-space": keyword("nowrap")})
    doc.bind("header-title.text", "header-title", "text", {"state": title_state})
    for item in trailing:
        item["properties"]["flex-shrink"] = number(0)
    row = group("header-row", {**absolute(left=INSET, top=0, right=INSET, height=HEADER_H), "display": keyword("flex"),
                               "flex-direction": keyword("row"), "align-items": keyword("center"), "column-gap": length(16)},
                [triangle, title, *trailing])
    return [wash, rule, row]


def keycap(ident: str, key: str, *, fixed: bool = False, event: str | None = None, doc: Document | None = None) -> dict:
    """A keycap plate with a 45-degree lower-leading cut (section 13.4) and
    its key name in Lowpixel. A `fixed` cap is the strip's 20 dp glyph; the
    prompt bar's caps fit their key names. With `event` it is a button."""
    shape = path("cap", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (6, {"fraction": 1}),
                         (0, {"fraction": 1, "dp": -6})], fill=solid([1, 1, 1, 1]))
    name_props = {"position": keyword("relative"), "display": keyword("block"), **typeface("lowpixel", 13, PROMPT_H, [0.04, 0.05, 0.035, 1]),
                  "white-space": keyword("nowrap")}
    if fixed:
        name_props.update({**absolute(left=0, top=0, width=KEYCAP_W, height=PROMPT_H), "text-align": keyword("center")})
        cap_props = {**absolute(left=0, top=0, width=KEYCAP_W, height=PROMPT_H), "opacity": number(0.85)}
        props = {"position": keyword("absolute"), "width": length(KEYCAP_W), "height": length(PROMPT_H)}
    else:
        cap_props = {"position": keyword("relative"), "display": keyword("block"), "height": length(PROMPT_H),
                     "padding-left": length(7), "padding-right": length(7), "opacity": number(0.85)}
        props = {"position": keyword("relative"), "display": keyword("block"), "height": length(PROMPT_H)}
    name = label(f"{ident}-key", key, name_props)
    if fixed:
        children = [vector(f"{ident}-cap", {**cap_props, "display": keyword("block")}, [shape]), name]
    else:
        children = [vector(f"{ident}-cap", cap_props, [shape], [name])]
    if doc is not None and key in b.FACE_FOR_KEY:
        # Section 13.5: the keycap for the keyboard, otherwise the controller
        # family's button that does the same, at the cap's size.
        keyboard = {"op": "select", "args": [{"op": "==", "args": [{"state": b.PROMPT_GLYPHS}, 0]}, "block", "none"]}
        for node in children:
            doc.bind(f"{node['id']}.display", node["id"], "display", keyboard)
        glyphs = b.device_glyphs(doc, ident, b.FACE_FOR_KEY[key], [1, 1, 1, 1], PROMPT_H, KEYCAP_W if fixed else None)
        pad_props = (absolute(left=0, top=0, width=KEYCAP_W, height=PROMPT_H) if fixed else
                     {"position": keyword("relative"), "height": length(PROMPT_H)})
        children.append(group(f"{ident}-pad", {**pad_props, "display": keyword("none"), "opacity": number(0.85)}, glyphs))
        doc.bind(f"{ident}-pad.display", f"{ident}-pad", "display",
                 {"op": "select", "args": [{"op": "==", "args": [{"state": b.PROMPT_GLYPHS}, 0]}, "none", "block"]})
    if event is None:
        return group(ident, props, children)
    tracks = {"default": 0.85, "hover": 1, "focus": 1, "pressed": 1, "disabled": 0.4}
    ids = doc.states(ident, {state: [(f"{ident}-cap", "opacity", number(value))] +
                             ([(f"{ident}-pad", "opacity", number(value))] if key in b.FACE_FOR_KEY else [])
                             for state, value in tracks.items()})
    return group(ident, props, children, control={"role": "button", "label": key, "event": event, "states": ids})


def tab(doc: Document, index: int, ident: str, key: str, state: str = "card.tab") -> dict:
    """One tab of the strip: its label in flow, padded to hold the leading
    flare and the trailing shoulder, so the tab is as wide as its label in
    the running language plus its share of the strip's remainder. The active tab rises 30 dp with a 3 dp leading
    flare and an 18 dp 45-degree trailing shoulder, filled #5A652A from 1.00
    at the top to 0.60 at the baseline inside a white outline, and opens into
    the wash below: each tab draws the strip's baseline under itself only while
    it is inactive, so nothing is painted over the active tab's foot (an
    opaque eraser there left a seam against the translucent wash). Inactive
    labels sit at 0.55 and rise to 0.85
    on hover; the active label is white, and focus turns a label orange.
    `state` holds the current tab's index."""
    node = f"tab-{ident}"
    h = TAB_RISE
    right = {"fraction": 1}
    shoulder = {"fraction": 1, "dp": -TAB_SHOULDER}
    edge = [(0, h), (TAB_FLARE, h - TAB_FLARE), (TAB_FLARE, 0), (shoulder, 0), (right, TAB_SHOULDER), (right, h)]
    silhouette = vector(f"{node}-active", {**FULL, "display": keyword("none")}, [
        path("fill", edge, fill=linear((0, 0), (0, h), [(0, rgb(TAB_GREEN, 1)), (1, rgb(TAB_GREEN, 0.6))])),
        path("outline", edge, closed=False, stroke=stroke(solid([1, 1, 1, 0.75]), 1.5)),
    ])
    face = {"text-align": keyword("center"), "white-space": keyword("nowrap")}
    # The lit copy lies under the label, so focus's orange still shows on the
    # active tab, and the 0.55 white at rest reads as white over it.
    lit = label(f"{node}-lit", key, {**absolute(left=TAB_FLARE + TAB_PAD, top=0, right=TAB_PAD + TAB_SHOULDER, height=h),
                                     "display": keyword("none"), **typeface("marine", TAB_SIZE, h, [1, 1, 1, 1]), **face})
    text_node = label(f"{node}-label", key, {"position": keyword("relative"), "display": keyword("block"),
                                             **typeface("marine", TAB_SIZE, h, [1, 1, 1, 0.55]), **face})
    ids = doc.states(node, {
        "default": [(f"{node}-label", "color", colour([1, 1, 1, 0.55]))],
        "hover": [(f"{node}-label", "color", colour([1, 1, 1, 0.85]))],
        "focus": [(f"{node}-label", "color", colour(rgb(ORANGE)))],
        "pressed": [(f"{node}-label", "color", colour(rgb(ORANGE)))],
        "disabled": [(f"{node}-label", "color", colour([1, 1, 1, 0.3]))],
    })
    # The strip's baseline under this tab, while it is not the active one.
    rail = vector(f"{node}-rail", {**absolute(left=0, top=h - 1.5, width=0, height=2), "width": length(100, "%"),
                                   "display": keyword("block"), "pointer-events": keyword("none")},
                  [path("rail", [(0, 0.75), ({"fraction": 1}, 0.75)], closed=False, stroke=stroke(solid([1, 1, 1, 0.75]), 1.5))])
    active = {"op": "==", "args": [{"state": state}, index]}
    doc.bind(f"{node}-active.display", f"{node}-active", "display", {"op": "select", "args": [active, "block", "none"]})
    doc.bind(f"{node}-rail.display", f"{node}-rail", "display", {"op": "select", "args": [active, "none", "block"]})
    doc.bind(f"{node}-lit.display", f"{node}-lit", "display", {"op": "select", "args": [active, "block", "none"]})
    return group(node, {"position": keyword("relative"), "display": keyword("block"), "height": length(h),
                        "padding-left": length(TAB_FLARE + TAB_PAD), "padding-right": length(TAB_PAD + TAB_SHOULDER),
                        "flex-grow": number(1)},
                 [rail, silhouette, lit, text_node],
                 control={"role": "button", "label": key, "event": f"tab_{ident}", "states": ids})


def tab_strip(doc: Document, width: float, tabs: list, top: float = TAB_TOP, state: str = "card.tab") -> list:
    """The strip under the header (section 14.18): Q and E keycaps at its
    ends run the previous and next tab; the tabs share the width between
    them; the baseline runs under all of them, broken under the active tab,
    and a #5A652A wash fades below it over one tab height. The strip stands
    `top` dp down its container; `state` holds the current tab's index."""
    inner_left = STRIP_EDGE + KEYCAP_W + KEYCAP_GAP
    inner_width = width - 2 * inner_left
    slots = [tab(doc, index, ident, key, state) for index, (ident, key, _window) in enumerate(tabs)]
    keys_top = top + (TAB_RISE - PROMPT_H) / 2
    previous = keycap("tab-previous", KEY_PREVIOUS, fixed=True, event="onTabPrevious", doc=doc)
    following = keycap("tab-next", KEY_NEXT, fixed=True, event="onTabNext", doc=doc)
    previous["properties"].update(absolute(left=STRIP_EDGE, top=keys_top))
    following["properties"].update(absolute(left=width - STRIP_EDGE - KEYCAP_W, top=keys_top))
    row = group("tabs", {**absolute(left=inner_left, top=top, width=inner_width, height=TAB_RISE),
                         "display": keyword("flex"), "flex-direction": keyword("row"), "align-items": keyword("flex-end")}, slots)
    baseline_y = top + TAB_RISE
    # The baseline under the keycaps; the tabs draw it under themselves.
    baselines = [vector(f"tab-baseline-{side}", absolute(left=left, top=baseline_y - 1.5, width=inner_left - STRIP_EDGE, height=2), [
        path("rail", [(0, 0.75), ({"fraction": 1}, 0.75)], closed=False, stroke=stroke(solid([1, 1, 1, 0.75]), 1.5))])
        for side, left in (("lead", STRIP_EDGE), ("trail", inner_left + inner_width))]
    wash = vector("tab-wash", absolute(left=STRIP_EDGE, top=baseline_y, width=width - 2 * STRIP_EDGE, height=TAB_RISE), [
        path("wash", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
             fill=linear((0, 0), (0, {"fraction": 1}), [(0, rgb(TAB_GREEN, 0.57)), (1, rgb(TAB_GREEN, 0.02))]))])
    return [wash, *baselines, row, previous, following]


def in_game_plate(doc: Document, ident: str, key: str, left: float, top: float, *, width: float = 300,
                  primary: bool = False, action: str | None = None, event: str | None = None) -> dict:
    """An action on an in-game card (section 14.18): the light plate of
    section 6 (b6_light) with its leading, cut and bottom rails, at 0.62 for
    the page's primary action and 0.28 for the others, rising to 1.00 with
    the orange marker and inset rail when focused; Marine 20 dp."""
    rest = 0.62 if primary else 0.28
    band_top, band_bottom, cut = 5.0, 40.0, 20.0
    fill = [(0, rgb(OLIVE, 0.49)), (0.33, rgb(OLIVE, 0.49)), (1, rgb(OLIVE, 0))]
    rail = [(0, rgb(OLIVE)), (0.33, rgb(OLIVE)), (1, rgb(OLIVE, 0))]
    plate = vector(f"{ident}-plate", {**absolute(left=0, top=0, width=width, height=45), "opacity": number(rest)}, [
        path("fill", [(0, band_top), (width, band_top), (width, band_bottom), (cut, band_bottom), (0, band_bottom - cut)],
             fill=linear((0, 0), (width, 0), fill)),
        path("rail", [(0.75, band_top), (0.75, band_bottom - cut), (cut, band_bottom - 0.75), (width, band_bottom - 0.75)],
             closed=False, stroke=stroke(linear((0, 0), (width, 0), rail), 1.5)),
    ])
    inset = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=width, height=45), "opacity": number(0)}, [
        path("inset", [(3.5, band_top + 3), (3.5, band_bottom - cut + 1.5), (cut + 1.5, band_bottom - 3.5), (width - 20, band_bottom - 3.5)],
             closed=False, stroke=stroke(solid(rgb(ORANGE)), 1.2))])
    marker_rest = vector(f"{ident}-marker", {**absolute(left=14, top=14, width=8, height=8), "opacity": number(rest)}, [
        marker_path("mark", 0, 0, 8, rgb(MARKER))])
    marker_hot = vector(f"{ident}-marker-hot", {**absolute(left=14, top=14, width=8, height=8), "opacity": number(0)}, [
        marker_path("mark", 0, 0, 8, rgb(ORANGE))])
    text_node = label(f"{ident}-label", key, {**absolute(left=26, top=0, width=width - 30, height=45),
                      **typeface("marine", 20, 45, [1, 1, 1, 0.8]), "white-space": keyword("nowrap"), "transform": transform()})

    def state(plate_opacity, marker, hot, focus, tint, moved):
        return [(f"{ident}-plate", "opacity", number(plate_opacity)), (f"{ident}-marker", "opacity", number(marker)),
                (f"{ident}-marker-hot", "opacity", number(hot)), (f"{ident}-focus", "opacity", number(focus)),
                (f"{ident}-label", "color", colour(tint)), (f"{ident}-label", "transform", moved)]
    ids = doc.states(ident, {
        "default": state(rest, rest, 0, 0, [1, 1, 1, 0.8], transform()),
        "hover": state(0.8, 0, 1, 0, rgb(ORANGE), transform(sx=1.03, sy=1.03)),
        "focus": state(1, 0, 1, 1, rgb(ORANGE), transform(sx=1.03, sy=1.03)),
        "pressed": state(1, 0, 1, 1, rgb(ORANGE), transform(tx=1, ty=1, sx=1.03, sy=1.03)),
        "disabled": state(rest, rest, 0, 0, [1, 1, 1, 0.8], transform()),
    })
    control = {"role": "button", "label": key, "states": ids}
    if action:
        control["action"] = action
    else:
        control["event"] = event
    return group(ident, absolute(left=left, top=top, width=width, height=45), [plate, inset, marker_rest, marker_hot, text_node],
                 control=control)


def page_group(doc: Document, index: int, ident: str, width: float, height: float, children: list, controls: list) -> dict:
    """A page of the card: shown while it is current or fading out. Its
    controls answer only while it is current, so a page fading out takes no
    input (their disabled visuals equal their rest)."""
    current = {"op": "==", "args": [{"state": "card.tab"}, index]}
    for control in controls:
        doc.bind(f"{control}.enabled", control, "enabled", current)
    shown = {"op": "||", "args": [current, {"op": "==", "args": [{"state": "card.leaving"}, index]}]}
    doc.bind(f"page-{ident}.display", f"page-{ident}", "display", {"op": "select", "args": [shown, "block", "none"]})
    page_height = height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16
    return group(f"page-{ident}", {**absolute(left=INSET, top=PAGE_TOP, width=width - 2 * INSET, height=page_height),
                                   "display": keyword("none"), "opacity": number(1)}, children)


def handoff_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, str]:
    """An interim page: it says the page opens in the classic menu and offers
    that as its primary action, which records the page and runs mpStockPage."""
    event = f"stock_{ident}"
    doc.events[event] = [{"op": "setState", "values": {"card.stock_page": index}}, {"op": "action", "action": "mpStockPage"}]
    primary = f"page-{ident}-open"
    page_width = width - 2 * INSET
    note = label(f"page-{ident}-note", HANDOFF_NOTE, {**absolute(left=0, top=0, width=page_width, height=24),
                 **typeface("lowpixel", 17, 24, [1, 1, 1, 0.8]), "white-space": keyword("nowrap")})
    # The plate fits its label in every language: 26 dp before it, 20 dp of
    # fade after it.
    plate_width = max(300.0, max(b.text_width("marine", text, 20) for text in table_text(HANDOFF_ACTION).values()) + 46)
    if plate_width > page_width:
        raise SystemExit(f"the hand-off action needs {plate_width:.0f} dp, past the page's {page_width:g} dp")
    plate = in_game_plate(doc, primary, HANDOFF_ACTION, 0, 36, width=round(plate_width, 3), primary=True, event=event)
    return page_group(doc, index, ident, width, height, [note, plate], [primary]), primary


# ---------------------------------------------------------------- Team page

TEAM_SLOTS = 3
SLOT_TOP, SLOT_PITCH = 50.0, 62.0
SLOT_W = 640.0                # a slot's plate; its reason or detail runs under the label
ERROR = "#E46D56"             # an unavailable action's reason (section 14.18)
MP_MARINE, MP_STROGG, MP_SPECTATOR, MP_NEUTRAL = "#6AA42B", "#FF7B04", "#999999", "#8B964B"
# Every label a slot can show, in every language: its own strings, and the
# join form with each team's name.
SLOT_LABELS = ("#str_231014", "#str_231016", "#str_231017", "#str_231018", "#str_200195")
JOIN_FORMAT, TEAM_NAMES = "#str_231015", ("#str_200197", "#str_200199")


def any_text(key: str) -> dict:
    """`key`'s text in every shipped language, from any of its tables (the
    English table where a language's own lacks it, as the engine falls back)."""
    import re
    texts = {}
    for language in ("english",) + tuple(b.LANGUAGES):
        for path in sorted(b.STRINGS.glob(f"{language}_*.lang")):
            for line in path.read_text(encoding="utf-8").splitlines():
                match = re.match(r'\s*"(#str_\d+)"\s+"(.*)"\s*$', line)
                if match and match.group(1) == key:
                    texts[language] = match.group(2)
    return {language: texts.get(language, texts["english"]) for language in b.LANGUAGES}


def check_slot_labels() -> None:
    """Fail generation when a slot's label and lock would leave the plate."""
    room = SLOT_W - 26 - 20 - 20       # after the marker, before the lock and the fade
    widest = {}
    for key in SLOT_LABELS:
        for language, text in any_text(key).items():
            widest[(key, language)] = b.text_width("marine", text, 20)
    formats = table_text(JOIN_FORMAT)
    for team in TEAM_NAMES:
        for language, name in any_text(team).items():
            widest[(JOIN_FORMAT + " " + team, language)] = b.text_width("marine", formats[language].replace("%s", name), 20)
    over = {key: round(width) for key, width in widest.items() if width > room}
    if over:
        raise SystemExit(f"Team page labels past their {room:g} dp: {over}")


def padlock(ident: str, tint: list) -> dict:
    """The lock after an unavailable action's label: a shackle over a body."""
    return vector(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(12), "height": length(14),
                          "margin-left": length(8), "flex-shrink": number(0)}, [
        path("shackle", [(3, 7), (3, 3.5), (4.5, 1.5), (7.5, 1.5), (9, 3.5), (9, 7)], closed=False, stroke=stroke(solid(tint), 1.5)),
        path("body", [(1, 6.5), (11, 6.5), (11, 13.5), (1, 13.5)], fill=solid(tint)),
    ])


def action_plate(doc: Document, ident: str, key: str, width: float, top: float, event: str, steps: list, *,
                 primary: bool = False, menu_button: bool = False) -> dict:
    """An action the game offers: an in-game plate whose label, availability
    and reason the game publishes (`key`.shown, .available, .label, .reason
    and .detail). An unavailable action stays in place, dimmed, with a lock
    after its label and its reason in the error color on the plate; choosing
    it shakes the plate for 300 ms and pulses the reason, and nothing else
    happens. An available one runs `steps`, which ask the game, and the game
    checks the action again. With `menu_button` the row ends with the
    controller's Menu button, which runs the action too (section 13.5)."""
    for name, kind in (("shown", "boolean"), ("available", "boolean"), ("label", "string"), ("reason", "string"), ("detail", "string")):
        doc.state[f"{key}.{name}"] = {"type": kind, "initial": False if kind == "boolean" else ""}
    plate = in_game_plate(doc, ident, PLACEHOLDER, 0, 0, width=width, primary=primary, event=event)
    # The label leaves the plate's own box for a row with the lock after it.
    label_node = next(child for child in plate["children"] if child["id"] == f"{ident}-label")
    plate["children"].remove(label_node)
    label_node["properties"].update({"position": keyword("relative"), "display": keyword("block"), "flex-shrink": number(1),
                                     "min-width": length(0), "overflow": keyword("hidden")})
    for prop in ("left", "top", "width"):
        label_node["properties"].pop(prop, None)
    doc.bind(f"{ident}-label.text", f"{ident}-label", "text", {"state": f"{key}.label"})
    lock = padlock(f"{ident}-lock", rgb(ERROR))
    unavailable = {"op": "!", "args": [{"state": f"{key}.available"}]}
    doc.bind(f"{ident}-lock.display", f"{ident}-lock", "display", {"op": "select", "args": [unavailable, "block", "none"]})
    row_children = [label_node, lock]
    if menu_button:
        # The glyph keeps the row's end for a controller; the keyboard has no
        # key for it, so it shows none there.
        row_children += [group(f"{ident}-menu-gap", {"position": keyword("relative"), "display": keyword("block"),
                                                     "flex-grow": number(1), "height": length(1)}),
                         group(f"{ident}-menu", {"position": keyword("relative"), "display": keyword("block"), "width": length(PROMPT_H),
                                                 "height": length(PROMPT_H), "flex-shrink": number(0), "margin-left": length(8)},
                               b.device_glyphs(doc, f"{ident}-menu", "menu", [1, 1, 1, 0.85], PROMPT_H))]
    row = group(f"{ident}-row", {**absolute(left=26, top=0, width=width - 46, height=45), "display": keyword("flex"),
                                 "flex-direction": keyword("row"), "align-items": keyword("center"), "pointer-events": keyword("none")},
                row_children)
    plate["children"].append(row)
    # The plate dims with its label; the reason beside it keeps its color.
    plate["properties"]["opacity"] = number(1)
    doc.bind(f"{ident}.opacity", ident, "opacity", {"op": "select", "args": [{"state": f"{key}.available"}, 1, 0.6]})
    side = {**absolute(left=26, top=40, width=width - 26, height=18), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
            "display": keyword("none")}
    reason = label(f"{ident}-reason", PLACEHOLDER, {**side, **typeface("lowpixel", 14, 18, rgb(ERROR)), "opacity": number(1)})
    detail = label(f"{ident}-detail", PLACEHOLDER, {**side, **typeface("lowpixel", 14, 18, [1, 1, 1, 0.7])})
    doc.bind(f"{ident}-reason.text", f"{ident}-reason", "text", {"state": f"{key}.reason"})
    doc.bind(f"{ident}-detail.text", f"{ident}-detail", "text", {"state": f"{key}.detail"})
    doc.bind(f"{ident}-reason.display", f"{ident}-reason", "display", {"op": "select", "args": [unavailable, "block", "none"]})
    doc.bind(f"{ident}-detail.display", f"{ident}-detail", "display",
             {"op": "select", "args": [{"state": f"{key}.available"}, "block", "none"]})
    holder = group(f"{ident}-slot", {**absolute(left=0, top=top, width=width, height=58),
                                     "transform": transform(), "display": keyword("none")}, [plate, reason, detail])
    doc.bind(f"{ident}-slot.display", f"{ident}-slot", "display", {"op": "select", "args": [{"state": f"{key}.shown"}, "block", "none"]})
    shake = f"shake-{ident}"
    doc.timelines.add(shake, 300, [
        track(f"{ident}-slot", "transform", [(0, transform()), (50, transform(tx=-6)), (110, transform(tx=6)), (170, transform(tx=-4)),
                                             (230, transform(tx=3)), (300, transform())]),
        track(f"{ident}-reason", "opacity", [(0, number(1)), (100, number(0.35)), (200, number(1)), (300, number(1))]),
    ])
    doc.events[event] = [
        {"op": "if", "condition": {"state": f"{key}.available"}, "then": steps, "else": [{"op": "playTimeline", "timeline": shake}]},
    ]
    return holder


def slot_plate(doc: Document, index: int, width: float) -> tuple[dict, str]:
    """A Team page action (mp.action<i>.*): an available one records its slot
    and runs mpTeamAction, which the game derives again from the player's
    state."""
    ident = f"team-slot-{index}"
    steps = [{"op": "setState", "values": {"card.team_action": index}}, {"op": "action", "action": "mpTeamAction"}]
    holder = action_plate(doc, ident, f"mp.action{index}", width, SLOT_TOP + SLOT_PITCH * index, f"team_slot_{index}", steps,
                          primary=index == 0)
    return holder, ident


def team_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, str]:
    """The Team page (section 14.18): the player's team in its header band
    with its score and both teams' sizes (spectators and deathmatch their own
    bands), the actions the game offers now, and the last three chat lines."""
    check_slot_labels()
    page_width = width - 2 * INSET
    page_height = height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16
    doc.state.update({
        "card.team_action": {"type": "number", "initial": -1},
        "mp.team.band": {"type": "string", "initial": ""},
        "mp.team.band_color": {"type": "number", "initial": 3},
        "mp.team.band_score": {"type": "string", "initial": ""},
        "mp.team.band_detail": {"type": "string", "initial": ""},
        **{f"mp.chat{line}": {"type": "string", "initial": ""} for line in range(3)},
    })
    band_h, cut = 36.0, 8.0
    bands = []
    for color_index, tint in enumerate((MP_MARINE, MP_STROGG, MP_SPECTATOR, MP_NEUTRAL)):
        # The header band (section 6): an upper-leading cut, top and leading
        # rails, the fill full to half the width and fading toward the end.
        node = f"team-band-{color_index}"
        bands.append(vector(node, {**absolute(left=0, top=0, width=page_width, height=band_h), "display": keyword("none")}, [
            path("fill", [(cut, 0), ({"fraction": 1}, 0), ({"fraction": 1}, band_h), (0, band_h), (0, cut)],
                 fill=linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(tint, 0.49)), (0.5, rgb(tint, 0.49)), (0.75, rgb(tint, 0.25)),
                                                         (0.95, rgb(tint, 0.05)), (1, rgb(tint, 0))])),
            path("rail", [(0.75, band_h), (0.75, cut), (cut, 0.75), ({"fraction": 0.6}, 0.75)], closed=False,
                 stroke=stroke(linear((0, 0), ({"fraction": 0.6}, 0), [(0, rgb(tint)), (0.7, rgb(tint)), (1, rgb(tint, 0))]), 1.5)),
        ]))
        doc.bind(f"{node}.display", node, "display",
                 {"op": "select", "args": [{"op": "==", "args": [{"state": "mp.team.band_color"}, color_index]}, "block", "none"]})
    band_title = label("team-band-title", PLACEHOLDER, {**absolute(left=20, top=0, width=page_width * 0.5, height=band_h),
                       **typeface("marine", 20, band_h, [1, 1, 1, 0.95]), "white-space": keyword("nowrap")})
    band_score = label("team-band-score", PLACEHOLDER, {**absolute(left=page_width * 0.5, top=0, width=page_width * 0.2, height=band_h),
                       **typeface("lowpixel", 20, band_h, rgb(VALUE)), "text-align": keyword("right"), "white-space": keyword("nowrap")})
    band_detail = label("team-band-detail", PLACEHOLDER, {**absolute(left=page_width * 0.72, top=0, width=page_width * 0.26, height=band_h),
                        **typeface("lowpixel", 17, band_h, [1, 1, 1, 0.8]), "white-space": keyword("nowrap")})
    doc.bind("team-band-title.text", "team-band-title", "text", {"state": "mp.team.band"})
    doc.bind("team-band-score.text", "team-band-score", "text", {"state": "mp.team.band_score"})
    doc.bind("team-band-detail.text", "team-band-detail", "text", {"state": "mp.team.band_detail"})
    slots, controls = [], []
    for slot in range(TEAM_SLOTS):
        holder, control = slot_plate(doc, slot, min(page_width, SLOT_W))
        slots.append(holder)
        controls.append(control)
    chat = [label(f"team-chat-{line}", PLACEHOLDER, {**absolute(left=0, top=page_height - 22 * (3 - line), width=page_width, height=20),
                  **typeface("lowpixel", 15, 20, [1, 1, 1, 0.65]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
            for line in range(3)]
    for line in range(3):
        doc.bind(f"team-chat-{line}.text", f"team-chat-{line}", "text", {"state": f"mp.chat{line}"})
    doc.session("mpTeamAction", "mpTeamAction")
    children = [*bands, band_title, band_score, band_detail, *slots, *chat]
    return page_group(doc, index, ident, width, height, children, controls), controls[0]


# ------------------------------------------------------------- Players page

# The lists: the player's own team (or Marines while spectating, or everyone
# outside team modes), the other team, then the spectators, each as many rows
# as the engine's connection table can hold (MAX_CLIENTS is 32). The game fills them in
# this order (RETAINED_PLAYER_LISTS and RETAINED_PLAYER_ROWS in the game) and
# says which band color each list takes: Marine, Strogg, spectators or the
# deathmatch olive.
PLAYER_LISTS = (("a", (0, 1, 3)), ("b", (0, 1)), ("s", (2,)))
PLAYER_ROWS = 32
BAND_TINTS = (MP_MARINE, MP_STROGG, MP_SPECTATOR, MP_NEUTRAL)
LIST_W = 380.0                 # the lists' column; the statistics take the rest
LIST_HEAD_H, LIST_BAND_H, LIST_GAP = 16.0, 22.0, 6.0
ROW_MAX = 24.0                # fixed readable pitch; long rosters scroll (section 14.14)
ROSTER_GUTTER = 36.0          # the shared authored scrollbar's target, outside the rows
ROW_FACE = 14.0
NAME_KEY, SCORE_KEY, PING_KEY = "#str_200242", "#str_200198", "#str_200037"
KILLS_KEY, DEATHS_KEY, ACCURACY_KEY, AWARDS_KEY = "#str_200201", "#str_200202", "#str_200203", "#str_200204"
# The accuracy row in the stock statistics' order: each weapon's icon in its
# color code (Appendix B.4). The game publishes mp.stat.acc<i> for the same
# weapons (RETAINED_ACCURACY_WEAPONS).
WEAPONS = (("weapon_machinegun", "item_machinegun", (1, 1, 0)), ("weapon_shotgun", "item_shotgun", (1, 0.5, 0)),
           ("weapon_hyperblaster", "item_hyperblaster", (0, 0.45, 1)), ("weapon_grenadelauncher", "item_grenade", (0.2, 0.56, 0.07)),
           ("weapon_nailgun", "item_nailgun", (0.6, 0.8, 0.8)), ("weapon_rocketlauncher", "item_rocket", (1, 0.2, 0)),
           ("weapon_railgun", "item_railgun", (0, 1, 0)), ("weapon_lightninggun", "item_lightning", (1, 1, 0.73)),
           ("weapon_dmg", "item_darkmatter", (0.77, 0, 1)), ("weapon_napalmgun", "item_fire", (1, 0.75, 0.25)))
# The awards the stock statistics show, the last three only in flag modes
# (RETAINED_AWARDS in the game); medals stay bitmaps (section 6).
AWARDS = ("excellent", "impressive", "humiliation", "combo_kill", "rampage", "capture", "assist", "defense")
FLAG_AWARDS = 3
MUTE_LABELS, FRIEND_LABELS = ("#str_200250", "#str_200251"), ("#str_200248", "#str_200249")
# The held scoreboard's marks (section 14.14): the skull's slot, the BOT tag
# in the header-band construction and the network-quality bars before ping.
BOT_KEY, BOT_TINT, DARK_TEXT = "#str_231231", "#8B964B", "#10140A"
STATE_W, BARS_GAP, BARS_W, NAME_FLOOR = 18.0, 6.0, 22.0, 150.0
SKULL = ("M4 0a4 4 0 0 1 4 4c0 1.4-.7 2-1.3 2.7v1.3H1.3V6.7C.7 6 0 5.4 0 4a4 4 0 0 1 4-4z"
         "M2.4 3.4h1.2v1.2H2.4zM4.4 3.4h1.2v1.2H4.4z")
MUTE_SELF, FRIEND_SELF = "#str_231031", "#str_231032"
STAT_PLATES_TOP = 192.0


def widest(keys, face: str, size: float) -> float:
    return max(b.text_width(face, text, size) for key in keys for text in any_text(key).values())


def list_columns(width: float) -> dict:
    """The rows' columns: the marker, the speaker and friend symbols, the
    dead mark, the name taking what is left (a bot's BOT tag within it),
    then the score, the network-quality bars and the ping, the score and
    ping at the widest of their heading in any language and their longest
    values."""
    score = max(widest([SCORE_KEY], "lowpixel", 11), b.text_width("lowpixel", "-999", ROW_FACE)) + 2
    ping = max(widest([PING_KEY], "lowpixel", 11), b.text_width("lowpixel", "999", ROW_FACE)) + 2
    columns = {"marker": 12.0, "speaker": 18.0, "friend": 20.0, "state": STATE_W, "score": round(score, 3),
               "bars": BARS_GAP + BARS_W, "ping": round(ping, 3), "gap": 8.0, "end": 4.0}
    columns["name"] = (width - columns["marker"] - columns["speaker"] - columns["friend"] - columns["state"] - score
                       - columns["bars"] - ping - columns["gap"] - columns["end"])
    if columns["name"] < NAME_FLOOR:
        raise SystemExit(f"the player lists leave {columns['name']:.0f} dp for names")
    return columns


def speaker_icon(ident: str) -> dict:
    """The speaker with a wave (section 6, status symbols); muting crosses it
    with a red slash."""
    tint = [1, 1, 1, 0.7]
    slash = vector(f"{ident}-slash", {**absolute(left=0, top=0, width=14, height=14), "display": keyword("none")}, [
        path("slash", [(1.5, 12.5), (12.5, 1.5)], closed=False, stroke=stroke(solid(rgb(ERROR)), 1.6))])
    return vector(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(14), "height": length(14),
                          "margin-right": length(4), "flex-shrink": number(0)}, [
        path("body", [(1.5, 5), (4.5, 5), (8, 2), (8, 12), (4.5, 9), (1.5, 9)], fill=solid(tint)),
        path("wave", [(10, 4), (11.6, 7), (10, 10)], closed=False, stroke=stroke(solid(tint), 1.2)),
    ], [slash])


def friend_icon(ident: str) -> dict:
    """The friend silhouette (section 6, status symbols), 0.125 when the
    player is not a friend."""
    tint = [1, 1, 1, 1]
    head = [(7 + 2.6 * x, 4.6 + 2.6 * y) for x, y in ((0, -1), (0.71, -0.71), (1, 0), (0.71, 0.71), (0, 1), (-0.71, 0.71),
                                                      (-1, 0), (-0.71, -0.71))]
    return vector(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(14), "height": length(14),
                          "margin-right": length(6), "flex-shrink": number(0), "opacity": number(0.125)}, [
        path("head", head, fill=solid(tint)),
        path("shoulders", [(1.5, 13.5), (2.2, 10.4), (4.6, 8.6), (9.4, 8.6), (11.8, 10.4), (12.5, 13.5)], fill=solid(tint)),
    ])


def skull_icon(ident: str) -> dict:
    """The held scoreboard's dead mark (section 14.14): a white skull in its
    own slot, shown by opacity so the slot keeps its room. One node a row:
    each node costs every card's load about 0.07 ms."""
    skull = {"id": "k", "commands": hud_svg.commands(SKULL, (1.5, 0, 0, 1.5, 1, 1.5)), "fillRule": "evenodd",
             "fill": solid([1, 1, 1, 1])}
    return vector(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(14), "height": length(16),
                          "margin-right": length(STATE_W - 14), "flex-shrink": number(0), "opacity": number(0)}, [skull])


def bot_tag(ident: str) -> dict:
    """The BOT tag (section 14.9), as on the held scoreboard: dark Lowpixel
    text on an olive plate at 0.85. One label node, its plate a background,
    so the tag costs a row a single node."""
    return label(ident, BOT_KEY, {"position": keyword("relative"), "display": keyword("none"), "white-space": keyword("nowrap"),
                                  "flex-shrink": number(0), "margin-left": length(6), "padding-left": length(6),
                                  "padding-right": length(6), "background-color": colour(rgb(BOT_TINT, 0.85)),
                                  **typeface("lowpixel", 10.8, 15, rgb(DARK_TEXT)), "letter-spacing": length(0.6)})


def ping_meter(doc: Document, ident: str, key: str) -> dict:
    """The network-quality bars (section 13.13), as on the held scoreboard:
    four bars rising from the text's baseline, lit to the row's step, unlit
    bars at 0.20 of the same color. The game publishes the step (`bars`, 4 at
    60 ms or less down to 1 past 200 ms, 0 for a bot or an empty row, which
    keeps the room, empty, so ping lines up) and its color (`bars_r`, `_g`,
    `_b`: #B5C784 for three or four, #E3AD36 for two, #E46D56 for one), so
    each bar is one rectangle with one short binding: every row exists 96
    times on a card, and four drawn variants or colors chosen here cost each
    card about 0.4 MB more."""
    level = {"state": f"{key}.bars"}
    tint = [{"state": f"{key}.bars_{channel}"} for channel in "rgb"]
    bars = []
    for bar in range(4):
        x, h = 2 + bar * 5, 4.5 + bar * 3.3
        node = f"{ident}-bar{bar}"
        bars.append(group(node, {**absolute(left=x, top=round(15 - h, 3), width=3.3, height=round(h, 3)),
                                 "background-color": colour([1, 1, 1, 0])}))
        alpha = {"op": "select", "args": [{"op": ">", "args": [level, bar]}, 1, {"op": "select", "args": [
            {"op": ">", "args": [level, 0]}, 0.2, 0]}]}
        doc.bind(f"{node}.background-color", node, "background-color", [*tint, alpha])
    return group(f"{ident}-bars", {"position": keyword("relative"), "display": keyword("block"), "width": length(BARS_W),
                                   "height": length(16), "margin-left": length(BARS_GAP), "flex-shrink": number(0)}, bars)


# The faction marks (section 6's emblems, the HUD's `marinelogo` and
# `strogglogo` vectors) for the Marine and Strogg teams, by team index.
FACTION_MARKS = ("marinelogo", "strogglogo")


def faction_mark(ident: str, team: int, left: float, top: float, size: float, props: dict | None = None) -> dict:
    """A team's faction mark, white at 0.95, `size` dp square."""
    return group(ident, {**absolute(left=left, top=top, width=size, height=size), "display": keyword("block"),
                         "pointer-events": keyword("none"), **(props or {})},
                 hud_svg.icon_nodes(FACTION_MARKS[team], f"{ident}-g", 0, 0, size, tint=[1, 1, 1, 0.95]))


def team_band(ident: str, width: float, height: float, tint: str, cut: float = 8.0) -> dict:
    """A header band in a team's color (section 6): an upper-leading cut, top
    and leading rails, the fill full to half the width and fading toward the
    end."""
    return vector(ident, {**absolute(left=0, top=0, width=width, height=height), "display": keyword("none")}, [
        path("fill", [(cut, 0), ({"fraction": 1}, 0), ({"fraction": 1}, height), (0, height), (0, cut)],
             fill=linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(tint, 0.49)), (0.5, rgb(tint, 0.49)), (0.75, rgb(tint, 0.25)),
                                                     (0.95, rgb(tint, 0.05)), (1, rgb(tint, 0))])),
        path("rail", [(0.75, height), (0.75, cut), (cut, 0.75), ({"fraction": 0.6}, 0.75)], closed=False,
             stroke=stroke(linear((0, 0), ({"fraction": 0.6}, 0), [(0, rgb(tint)), (0.7, rgb(tint)), (1, rgb(tint, 0))]), 1.5)),
    ])


def player_action(doc: Document, verb: str) -> None:
    """Capture the selected connection in each queued Session request."""
    doc.actions[verb] = {"operation": "session.menu", "arguments": {
        "command": verb, "client": {"state": "card.client"}, "identity": {"state": "card.identity"}}}


def player_row(doc: Document, prefix: str, kind: str, row: int, width: float, columns: dict,
               interactive: bool = True) -> tuple[dict, str]:
    """One row of a list, the scoreboard's row (section 14.14): a band at
    0.08, 0.29 and the orange marker for the player's own row; the speaker, crossed
    when muted, and the friend symbol; a skull while dead, the name (a bot's
    with its BOT tag), the score, the network-quality bars and the ping, the
    row at 0.50 while its player is dead. The player whose statistics show is
    lit in olive. Choosing a row records its client and runs mpSelectPlayer;
    the game checks the client again."""
    ident = f"{prefix}-{kind}-{row}"
    key = f"mp.players.{kind}{row}"
    doc.state.update({
        f"{key}.client": {"type": "number", "initial": -1}, f"{key}.identity": {"type": "string", "initial": ""},
        f"{key}.name": {"type": "string", "initial": ""},
        f"{key}.score": {"type": "string", "initial": ""}, f"{key}.ping": {"type": "string", "initial": ""},
        f"{key}.friend": {"type": "boolean", "initial": False}, f"{key}.muted": {"type": "boolean", "initial": False},
        f"{key}.local": {"type": "boolean", "initial": False}, f"{key}.bot": {"type": "boolean", "initial": False},
        f"{key}.dead": {"type": "boolean", "initial": False}, f"{key}.bars": {"type": "number", "initial": 0},
        **{f"{key}.bars_{channel}": {"type": "number", "initial": 1} for channel in "rgb"},
    })
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    band = vector(f"{ident}-band", {**absolute(left=0, top=0, width=width, height=length(100, "%")), "opacity": number(0.08)},
                  [path("band", [(0, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}), (0, {"fraction": 1, "dp": -1})],
                        fill=solid([1, 1, 1, 1]))])
    doc.bind(f"{ident}-band.opacity", f"{ident}-band", "opacity", {"op": "select", "args": [{"state": f"{key}.local"}, 0.29, 0.08]})
    chosen = vector(f"{ident}-chosen", {**absolute(left=0, top=0, width=width, height=length(100, "%")), "display": keyword("none")},
                    [path("fill", whole, fill=linear((0, 0), ({"fraction": 1}, 0),
                                                     [(0, rgb(OLIVE, 0.6)), (0.4, rgb(OLIVE, 0.45)), (1, rgb(OLIVE, 0))]))])
    if interactive:
        selected = {"op": "&&", "args": [{"op": ">=", "args": [{"state": f"{key}.client"}, 0]},
                                         {"op": "==", "args": [{"state": f"{key}.client"}, {"state": "mp.stat.client"}]}]}
        doc.bind(f"{ident}-chosen.display", f"{ident}-chosen", "display", {"op": "select", "args": [selected, "block", "none"]})
    focus = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=width, height=length(100, "%")), "opacity": number(0)},
                   [path("inset", [(0.75, 1), (0.75, {"fraction": 1, "dp": -1.5}), (width, {"fraction": 1, "dp": -1.5})], closed=False,
                         stroke=stroke(solid(rgb(ORANGE)), 1.2))])
    # Your row's orange marker, the scoreboard's (section 14.14).
    marker = vector(f"{ident}-marker", {"position": keyword("relative"), "display": keyword("none"), "width": length(8),
                                         "height": length(8), "margin-right": length(4), "flex-shrink": number(0)},
                    [marker_path("mark", 0, 0, 8, rgb(ORANGE))])
    spacer = group(f"{ident}-spacer", {"position": keyword("relative"), "display": keyword("block"), "width": length(columns["marker"]),
                                       "height": length(8), "flex-shrink": number(0)})
    doc.bind(f"{ident}-marker.display", f"{ident}-marker", "display", {"op": "select", "args": [{"state": f"{key}.local"}, "block", "none"]})
    doc.bind(f"{ident}-spacer.display", f"{ident}-spacer", "display", {"op": "select", "args": [{"state": f"{key}.local"}, "none", "block"]})
    speaker = speaker_icon(f"{ident}-speaker")
    doc.bind(f"{ident}-speaker-slash.display", f"{ident}-speaker-slash", "display",
             {"op": "select", "args": [{"state": f"{key}.muted"}, "block", "none"]})
    friend = friend_icon(f"{ident}-friend")
    doc.bind(f"{ident}-friend.opacity", f"{ident}-friend", "opacity", {"op": "select", "args": [{"state": f"{key}.friend"}, 1, 0.125]})
    cell = {"position": keyword("relative"), "display": keyword("block"), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
            "flex-shrink": number(0)}
    name = label(f"{ident}-name", PLACEHOLDER, {**cell, "flex-grow": number(1), "flex-shrink": number(1), "min-width": length(0),
                                               **typeface("lowpixel", ROW_FACE, 16, [1, 1, 1, 0.85])})
    score = label(f"{ident}-score", PLACEHOLDER, {**cell, "width": length(columns["score"]), "text-align": keyword("right"),
                                                 **typeface("lowpixel", ROW_FACE, 16, [1, 1, 1, 0.85])})
    ping = label(f"{ident}-ping", PLACEHOLDER, {**cell, "width": length(columns["ping"]), "margin-left": length(columns["gap"]),
                                               "margin-right": length(columns["end"]), "text-align": keyword("right"),
                                               **typeface("lowpixel", ROW_FACE, 16, [1, 1, 1, 0.6])})
    for part in ("name", "score", "ping"):
        doc.bind(f"{ident}-{part}.text", f"{ident}-{part}", "text", {"state": f"{key}.{part}"})
    skull = skull_icon(f"{ident}-skull")
    doc.bind(f"{ident}-skull.opacity", f"{ident}-skull", "opacity", {"op": "select", "args": [{"state": f"{key}.dead"}, 1, 0]})
    tag = bot_tag(f"{ident}-bot")
    doc.bind(f"{ident}-bot.display", f"{ident}-bot", "display", {"op": "select", "args": [{"state": f"{key}.bot"}, "block", "none"]})
    bars = ping_meter(doc, ident, key)
    content = group(f"{ident}-content", {**absolute(left=2, top=0, width=width - 2, height=length(100, "%")), "display": keyword("flex"),
                                         "flex-direction": keyword("row"), "align-items": keyword("center"), "pointer-events": keyword("none"),
                                         "opacity": number(1)},
                    [marker, spacer, speaker, friend, skull, name, tag, score, bars, ping])
    doc.bind(f"{ident}-content.opacity", f"{ident}-content", "opacity", {"op": "select", "args": [{"state": f"{key}.dead"}, 0.5, 1]})
    props = {"position": keyword("relative"), "display": keyword("none"), "width": length(width), "height": length(ROW_MAX),
             "flex-shrink": number(0)}
    if interactive:
        ids = doc.states(ident, {
            "default": [(f"{ident}-focus", "opacity", number(0)), (f"{ident}-name", "color", colour([1, 1, 1, 0.85]))],
            "hover": [(f"{ident}-focus", "opacity", number(0.5)), (f"{ident}-name", "color", colour([1, 1, 1, 1]))],
            "focus": [(f"{ident}-focus", "opacity", number(1)), (f"{ident}-name", "color", colour(rgb(ORANGE)))],
            "pressed": [(f"{ident}-focus", "opacity", number(1)), (f"{ident}-name", "color", colour(rgb(ORANGE)))],
            "disabled": [(f"{ident}-focus", "opacity", number(0)), (f"{ident}-name", "color", colour([1, 1, 1, 0.85]))],
        })
        event = f"{prefix}_{kind}{row}"
        doc.events[event] = [{"op": "setState", "values": {"card.client": {"state": f"{key}.client"},
                                                          "card.identity": {"state": f"{key}.identity"}}},
                             {"op": "action", "action": "mpSelectPlayer"}]
        node = group(ident, props, [band, chosen, focus, content],
                     control={"role": "button", "label": PLACEHOLDER, "event": event, "states": ids,
                              "context": {"state": f"{key}.identity"}})
    else:
        node = group(ident, props, [band, content])
    doc.bind(f"{ident}.display", ident, "display",
             {"op": "select", "args": [{"op": "<", "args": [row, {"state": f"mp.players.{kind}.count"}]}, "block", "none"]})
    return node, ident


def roster_frame(doc: Document, prefix: str, width: float, height: float, headings: dict, children: list,
                 key: str = "#str_231002") -> tuple[dict, str]:
    """Fixed column headings above a bounded roster viewport. The scrollbar
    reuses the production list artwork and semantic navigation, including
    focus reveal, Home/End, paging and view-state restoration."""
    body_top = LIST_HEAD_H
    body_height = height - body_top
    column = group(f"{prefix}-column", {"position": keyword("relative"), "display": keyword("flex"),
                                      "flex-direction": keyword("column")}, children)
    viewport = group(f"{prefix}-viewport", {**absolute(left=0, top=body_top, width=width - ROSTER_GUTTER, height=body_height),
                                           "overflow": keyword("auto")}, [column])
    bar = f"{prefix}-scroll"
    scrollbar = b.objectives_scrollbar(doc, bar, f"{prefix}-viewport", OLIVE, ORANGE,
                                      width - ROSTER_GUTTER, body_top, body_height, line_step=ROW_MAX, key=key)
    return group(f"{prefix}-lists", absolute(left=0, top=0, width=width, height=height),
                 [headings, viewport, scrollbar]), bar


def roster_focus(prefix: str, height: float, tab: str) -> list:
    """Read-only lists focus their bar only when the authored rows overflow.
    A short roster leaves navigation on the tab instead of an inactive bar."""
    total = 0
    for kind, _tints in PLAYER_LISTS:
        count = {"state": f"mp.players.{kind}.count"}
        shown = {"op": ">", "args": [count, 0]} if kind == "s" else {"state": f"mp.players.{kind}.shown"}
        band = {"op": "select", "args": [shown, LIST_BAND_H + (0 if kind == "a" else LIST_GAP), 0]}
        rows = {"op": "*", "args": [count, ROW_MAX]}
        total = {"op": "+", "args": [total, {"op": "+", "args": [band, rows]}]}
    return [{"op": "focus", "control": f"tab-{tab}"},
            {"op": "if", "condition": {"op": ">", "args": [total, height - LIST_HEAD_H]},
             "then": [{"op": "focus", "control": f"{prefix}-scroll"}]}]


def player_lists(doc: Document, prefix: str, width: float, height: float, interactive: bool = True) -> tuple[dict, list, list]:
    """The team lists (section 14.18 with the scoreboard's rows, section
    14.14): headings once above them, then each list's band in its team's
    color with its title and score over its rows; the spectators' band shows
    only while there are spectators. Rows keep their 24 dp pitch in a scrolling
    column under fixed headings. Returns the lists, their controls and the steps that focus the
    player whose statistics show (or the first row). Without `interactive`
    the rows are not controls and the scrollbar takes focus when it can scroll."""
    outer_width = width
    width -= ROSTER_GUTTER
    if interactive:
        doc.state.update({"card.client": {"type": "number", "initial": -1}, "mp.stat.client": {"type": "number", "initial": -1},
                          "card.identity": {"type": "string", "initial": ""}, "mp.stat.identity": {"type": "string", "initial": ""}})
    columns = list_columns(width)
    heading = {**typeface("lowpixel", 11, LIST_HEAD_H, [1, 1, 1, 0.4]), "white-space": keyword("nowrap")}
    name_left = columns["marker"] + columns["speaker"] + columns["friend"] + columns["state"] + 2
    score_right = width - columns["end"] - columns["ping"] - columns["gap"] - columns["bars"]
    headings = group(f"{prefix}-headings", {"position": keyword("relative"), "display": keyword("block"), "width": length(width),
                                            "height": length(LIST_HEAD_H), "flex-shrink": number(0)}, [
        label(f"{prefix}-head-name", NAME_KEY, {**absolute(left=name_left, top=0, width=columns["name"], height=LIST_HEAD_H), **heading}),
        label(f"{prefix}-head-score", SCORE_KEY, {**absolute(left=round(score_right - columns["score"], 3), top=0, width=columns["score"],
                                                            height=LIST_HEAD_H), **heading, "text-align": keyword("right")}),
        label(f"{prefix}-head-ping", PING_KEY, {**absolute(left=round(width - columns["end"] - columns["ping"], 3), top=0,
                                                          width=columns["ping"], height=LIST_HEAD_H), **heading, "text-align": keyword("right")}),
        vector(f"{prefix}-head-rule", absolute(left=0, top=LIST_HEAD_H - 1, width=width, height=1), [
            path("rule", [(0, 0.5), ({"fraction": 1}, 0.5)], closed=False, stroke=stroke(solid([1, 1, 1, 0.1]), 1))]),
    ])
    shown = {kind: ({"state": f"mp.players.{kind}.shown"} if kind != "s" else {"op": ">", "args": [{"state": "mp.players.s.count"}, 0]})
             for kind, _tints in PLAYER_LISTS}
    children, controls = [], []
    for kind, tints in PLAYER_LISTS:
        doc.state.update({
            f"mp.players.{kind}.shown": {"type": "boolean", "initial": False}, f"mp.players.{kind}.title": {"type": "string", "initial": ""},
            f"mp.players.{kind}.color": {"type": "number", "initial": tints[0]}, f"mp.players.{kind}.score": {"type": "string", "initial": ""},
            f"mp.players.{kind}.count": {"type": "number", "initial": 0},
        })
        band = f"{prefix}-{kind}-band"
        variants = []
        for tint in tints:
            node = f"{band}-{tint}"
            variants.append(team_band(node, width, LIST_BAND_H, BAND_TINTS[tint], cut=6))
            doc.bind(f"{node}.display", node, "display",
                     {"op": "select", "args": [{"op": "==", "args": [{"state": f"mp.players.{kind}.color"}, tint]}, "block", "none"]})
        marks = []
        for team in (0, 1):
            if team in tints:
                node = faction_mark(f"{band}-mark{team}", team, 8, (LIST_BAND_H - 14) / 2, 14, {"display": keyword("none")})
                doc.bind(f"{node['id']}.display", node["id"], "display",
                         {"op": "select", "args": [{"op": "==", "args": [{"state": f"mp.players.{kind}.color"}, team]}, "block", "none"]})
                marks.append(node)
        title = label(f"{band}-title", PLACEHOLDER, {**absolute(left=28, top=0, width=width * 0.6 - 12, height=LIST_BAND_H),
                      **typeface("marine", 15, LIST_BAND_H, [1, 1, 1, 0.95]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
        score = label(f"{band}-score", PLACEHOLDER, {**absolute(left=round(score_right - 80, 3), top=0, width=80, height=LIST_BAND_H),
                      **typeface("lowpixel", 16, LIST_BAND_H, rgb(VALUE)), "text-align": keyword("right"), "white-space": keyword("nowrap")})
        doc.bind(f"{band}-title.text", f"{band}-title", "text", {"state": f"mp.players.{kind}.title"})
        doc.bind(f"{band}-score.text", f"{band}-score", "text", {"state": f"mp.players.{kind}.score"})
        holder = group(band, {"position": keyword("relative"), "display": keyword("none"), "width": length(width),
                              "height": length(LIST_BAND_H), "flex-shrink": number(0),
                              "margin-top": length(0 if kind == "a" else LIST_GAP)}, [*variants, *marks, title, score])
        doc.bind(f"{band}.display", band, "display", {"op": "select", "args": [shown[kind], "block", "none"]})
        children.append(holder)
        for row in range(PLAYER_ROWS):
            node, control = player_row(doc, prefix, kind, row, width, columns, interactive)
            children.append(node)
            if interactive:
                controls.append(control)
    lists, bar = roster_frame(doc, prefix, outer_width, height, headings, children)
    controls.append(bar)
    # The first row of the first list with players, unless the player whose
    # statistics show has a row: a later focus step overrides an earlier one.
    focus = [{"op": "focus", "control": f"tab-{prefix}"}]
    if not interactive:
        return lists, controls, roster_focus(prefix, height, prefix)
    for kind, _tints in reversed(PLAYER_LISTS):
        focus.append({"op": "if", "condition": {"op": ">", "args": [{"state": f"mp.players.{kind}.count"}, 0]},
                      "then": [{"op": "focus", "control": f"{prefix}-{kind}-0"}]})
    for kind, _tints in PLAYER_LISTS:
        for row in range(PLAYER_ROWS):
            key = f"mp.players.{kind}{row}"
            focus.append({"op": "if", "condition": {"op": "&&", "args": [
                {"op": "<", "args": [row, {"state": f"mp.players.{kind}.count"}]},
                {"op": "==", "args": [{"state": f"{key}.client"}, {"state": "mp.stat.client"}]}]},
                "then": [{"op": "focus", "control": f"{prefix}-{kind}-{row}"}]})
    player_action(doc, "mpSelectPlayer")
    return lists, controls, focus


def welcome_players_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, list]:
    """The Welcome card's Players page (section 14.18): the team lists with
    the speaker and friend symbols, score and ping, spectators below; its
    scrollbar takes navigation when the roster exceeds the viewport."""
    page_width = width - 2 * INSET
    page_height = height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16
    lists, controls, focus = player_lists(doc, ident, page_width, page_height, interactive=False)
    return page_group(doc, index, ident, width, height, [lists], controls), focus


def check_stat_labels(width: float) -> None:
    """Fail generation when Mute or Friend, in either form, with its lock,
    would leave its plate, or a statistics heading its column."""
    room = width - 26 - 20 - 20
    over = {}
    for key in (*MUTE_LABELS, *FRIEND_LABELS):
        for language, text in any_text(key).items():
            if b.text_width("marine", text, 20) > room:
                over[(key, language)] = round(b.text_width("marine", text, 20))
    for key in (MUTE_SELF, FRIEND_SELF):
        for language, text in any_text(key).items():
            if b.text_width("lowpixel", text, 14) > width - 26:
                over[(key, language)] = round(b.text_width("lowpixel", text, 14))
    for key in (KILLS_KEY, DEATHS_KEY, SCORE_KEY):
        for language, text in any_text(key).items():
            if b.text_width("lowpixel", text, 12) > width / 3 - 8:
                over[(key, language)] = round(b.text_width("lowpixel", text, 12))
    if over:
        raise SystemExit(f"Players page labels past their room: {over}")


def player_stats(doc: Document, left: float, width: float, height: float, plates: bool = True) -> tuple[dict, list]:
    """The selected player's statistics (section 14.18): the name in a band
    in the player's team color; kills, deaths and score; accuracy per weapon,
    each weapon's icon in its color code and dimmed while it has not fired;
    the awards' medals with their counts, the flag awards only in flag modes;
    then, with `plates`, Mute and Friend. Both are unavailable, with a
    reason, on the player's own row."""
    check_stat_labels(width)
    doc.state.update({
        "mp.stat.name": {"type": "string", "initial": ""}, "mp.stat.color": {"type": "number", "initial": 3},
        "mp.stat.kills": {"type": "string", "initial": ""}, "mp.stat.deaths": {"type": "string", "initial": ""},
        "mp.stat.score": {"type": "string", "initial": ""}, "mp.stat.flag_mode": {"type": "boolean", "initial": False},
        **{f"mp.stat.acc{weapon}": {"type": "string", "initial": ""} for weapon in range(len(WEAPONS))},
        **{f"mp.stat.award{award}": {"type": "string", "initial": ""} for award in range(len(AWARDS))},
    })
    band_h = 26.0
    children = []
    for tint, color in enumerate(BAND_TINTS):
        node = f"stat-band-{tint}"
        children.append(team_band(node, width, band_h, color))
        doc.bind(f"{node}.display", node, "display",
                 {"op": "select", "args": [{"op": "==", "args": [{"state": "mp.stat.color"}, tint]}, "block", "none"]})
    children.append(label("stat-name", PLACEHOLDER, {**absolute(left=18, top=0, width=width - 24, height=band_h),
                          **typeface("marine", 18, band_h, [1, 1, 1, 0.95]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}))
    doc.bind("stat-name.text", "stat-name", "text", {"state": "mp.stat.name"})
    heading = {**typeface("lowpixel", 12, 14, HEADING), "white-space": keyword("nowrap")}
    cell = width / 3
    for column, (part, key) in enumerate((("kills", KILLS_KEY), ("deaths", DEATHS_KEY), ("score", SCORE_KEY))):
        x = round(cell * column, 3)
        children.append(label(f"stat-{part}-label", key, {**absolute(left=x, top=32, width=round(cell - 8, 3), height=14), **heading}))
        children.append(label(f"stat-{part}", PLACEHOLDER, {**absolute(left=x, top=46, width=round(cell - 8, 3), height=26),
                              **typeface("lowpixel", 22, 26, rgb(VALUE)), "white-space": keyword("nowrap")}))
        doc.bind(f"stat-{part}.text", f"stat-{part}", "text", {"state": f"mp.stat.{part}"})
    children.append(label("stat-accuracy-label", ACCURACY_KEY, {**absolute(left=0, top=78, width=width, height=14), **heading}))
    columns = 5
    pitch_x = width / columns
    for weapon, (_def, icon, tint) in enumerate(WEAPONS):
        x, y = round(pitch_x * (weapon % columns), 3), 94 + 22 * (weapon // columns)
        quiet = {"op": "||", "args": [{"op": "==", "args": [{"state": f"mp.stat.acc{weapon}"}, "-"]},
                                      {"op": "==", "args": [{"state": f"mp.stat.acc{weapon}"}, ""]}]}
        children.append(picture(f"stat-acc-{weapon}-icon", f"gfx/guis/hud/icons/{icon}",
                                {**absolute(left=x, top=y + 2, width=18, height=18), "image-color": colour([*tint, 1]),
                                 "opacity": number(1)}, fit="contain"))
        doc.bind(f"stat-acc-{weapon}-icon.opacity", f"stat-acc-{weapon}-icon", "opacity", {"op": "select", "args": [quiet, 0.35, 1]})
        children.append(label(f"stat-acc-{weapon}", PLACEHOLDER, {**absolute(left=round(x + 24, 3), top=y, width=round(pitch_x - 26, 3),
                              height=22), **typeface("lowpixel", 15, 22, [1, 1, 1, 0.85]), "white-space": keyword("nowrap")}))
        doc.bind(f"stat-acc-{weapon}.text", f"stat-acc-{weapon}", "text", {"state": f"mp.stat.acc{weapon}"})
    children.append(label("stat-awards-label", AWARDS_KEY, {**absolute(left=0, top=144, width=width, height=14), **heading}))
    pitch_x = width / len(AWARDS)
    for award, name in enumerate(AWARDS):
        x = round(pitch_x * award, 3)
        medal = group(f"stat-award-{award}-cell", {**absolute(left=x, top=160, width=round(pitch_x, 3), height=24),
                                                    "display": keyword("block")}, [
            picture(f"stat-award-{award}-icon", f"gfx/mp/awards/{name}", {**absolute(left=0, top=1, width=22, height=22),
                                                                          "opacity": number(1)}, fit="contain"),
            label(f"stat-award-{award}", PLACEHOLDER, {**absolute(left=25, top=0, width=round(pitch_x - 26, 3), height=24),
                  **typeface("lowpixel", 15, 24, rgb(VALUE)), "white-space": keyword("nowrap")}),
        ])
        children.append(medal)
        doc.bind(f"stat-award-{award}.text", f"stat-award-{award}", "text", {"state": f"mp.stat.award{award}"})
        earned = {"op": "&&", "args": [{"op": "!=", "args": [{"state": f"mp.stat.award{award}"}, "0"]},
                                       {"op": "&&", "args": [{"op": "!=", "args": [{"state": f"mp.stat.award{award}"}, "-"]},
                                                             {"op": "!=", "args": [{"state": f"mp.stat.award{award}"}, ""]}]}]}
        doc.bind(f"stat-award-{award}-icon.opacity", f"stat-award-{award}-icon", "opacity", {"op": "select", "args": [earned, 1, 0.35]})
        if award >= len(AWARDS) - FLAG_AWARDS:
            doc.bind(f"stat-award-{award}-cell.display", f"stat-award-{award}-cell", "display",
                     {"op": "select", "args": [{"state": "mp.stat.flag_mode"}, "block", "none"]})
    if not plates:
        return group("players-stats", absolute(left=left, top=0, width=width, height=height), children), []
    plates = []
    for index, (verb, key) in enumerate((("mpMute", "mp.mute"), ("mpFriend", "mp.friend"))):
        ident = f"players-{key.split('.')[1]}"
        steps = [{"op": "setState", "values": {"card.client": {"state": "mp.stat.client"},
                                              "card.identity": {"state": "mp.stat.identity"}}}, {"op": "action", "action": verb}]
        holder = action_plate(doc, ident, key, width, STAT_PLATES_TOP + SLOT_PITCH * index, f"players_{key.split('.')[1]}", steps)
        holder["children"][0]["control"]["context"] = {"state": "mp.stat.identity"}
        plates.append(holder)
        player_action(doc, verb)
    if STAT_PLATES_TOP + SLOT_PITCH + 58 > height:
        raise SystemExit("the Players page's Mute and Friend leave the page")
    stats = group("players-stats", absolute(left=left, top=0, width=width, height=height), [*children, *plates])
    return stats, ["players-mute", "players-friend"]


def players_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, list]:
    """The Players page (section 14.18): the team lists beside the selected
    player's statistics, then Mute and Friend."""
    page_width = width - 2 * INSET
    page_height = height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16
    lists, rows, focus = player_lists(doc, ident, LIST_W, page_height)
    stats, plates = player_stats(doc, LIST_W + 24, page_width - LIST_W - 24, page_height)
    return page_group(doc, index, ident, width, height, [lists, stats], rows + plates), focus


# ----------------------------------------------------------- Welcome: Join

WELCOME_TABS = [
    ("join", "#str_231033", None),
    ("server", "#str_231007", None),
    ("players", "#str_231002", None),
    ("settings", "#str_231005", "qj_b_settings"),
]
WELCOME_SLOTS = 4                 # RETAINED_WELCOME_SLOTS in the game
LEAVE_SERVER, VERB_SPECTATE = "#str_231039", "#str_200195"
LEADERS_KEY, ARENAS_KEY, PLAYERS_KEY = "#str_231040", "#str_231041", "#str_200038"
TEAM_CARD_TOP, TEAM_CARD_H, TEAM_BAND_H = 56.0, 100.0, 30.0


def team_card(doc: Document, team: int, left: float, width: float) -> tuple[dict, str]:
    """A team card on the Join page (section 14.18), the plate that joins the
    team: the header band in the team's color with its name and score, then
    its player count and, in CTF, its flag's state. Its availability and
    reason come from the game (mp.join<team>.*) as an action plate's do: an
    unavailable card dims with a lock and its reason and shakes when chosen."""
    ident, key = f"join-slot-{team}", f"mp.join{team}"
    for name, kind in (("shown", "boolean"), ("available", "boolean"), ("label", "string"), ("reason", "string"), ("detail", "string")):
        doc.state[f"{key}.{name}"] = {"type": kind, "initial": False if kind == "boolean" else ""}
    tint = BAND_TINTS[team]
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (12, {"fraction": 1}), (0, {"fraction": 1, "dp": -12})]
    body = vector(f"{ident}-body", {**absolute(left=0, top=0, width=width, height=TEAM_CARD_H), "opacity": number(0.62)}, [
        path("fill", whole, fill=linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(OLIVE, 0.4)), (0.6, rgb(OLIVE, 0.25)), (1, rgb(OLIVE, 0.08))])),
        path("rail", [(0.75, 0), (0.75, {"fraction": 1, "dp": -12}), (12, {"fraction": 1, "dp": -0.75}), ({"fraction": 1}, {"fraction": 1, "dp": -0.75})],
             closed=False, stroke=stroke(linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(OLIVE)), (0.6, rgb(OLIVE)), (1, rgb(OLIVE, 0))]), 1.5)),
    ])
    focus = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=width, height=TEAM_CARD_H), "opacity": number(0)}, [
        path("inset", [(3.5, TEAM_BAND_H + 3), (3.5, {"fraction": 1, "dp": -13}), (13.5, {"fraction": 1, "dp": -3.5}),
                       ({"fraction": 1, "dp": -20}, {"fraction": 1, "dp": -3.5})], closed=False, stroke=stroke(solid(rgb(ORANGE)), 1.2))])
    band = team_band(f"{ident}-band", width, TEAM_BAND_H, tint)
    band["properties"]["display"] = keyword("block")
    mark = faction_mark(f"{ident}-mark", team, 12, (TEAM_BAND_H - 20) / 2, 20)
    name = label(f"{ident}-label", PLACEHOLDER, {**absolute(left=40, top=0, width=width * 0.62 - 24, height=TEAM_BAND_H),
                 **typeface("marine", 20, TEAM_BAND_H, [1, 1, 1, 0.95]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    doc.bind(f"{ident}-label.text", f"{ident}-label", "text", {"state": f"{key}.label"})
    score = label(f"{ident}-score", PLACEHOLDER, {**absolute(left=width * 0.62, top=0, right=12, height=TEAM_BAND_H),
                  **typeface("lowpixel", 22, TEAM_BAND_H, rgb(VALUE)), "text-align": keyword("right"), "white-space": keyword("nowrap")})
    doc.bind(f"{ident}-score.text", f"{ident}-score", "text", {"state": f"mp.welcome.team{team}.score"})
    count_label = label(f"{ident}-count-label", PLAYERS_KEY, {**absolute(left=16, top=TEAM_BAND_H + 6, width=width * 0.45, height=20),
                        **typeface("lowpixel", 13, 20, HEADING), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    count = label(f"{ident}-count", PLACEHOLDER, {**absolute(left=width * 0.45 + 16, top=TEAM_BAND_H + 6, width=60, height=20),
                  **typeface("lowpixel", 16, 20, [1, 1, 1, 0.9]), "white-space": keyword("nowrap")})
    doc.bind(f"{ident}-count.text", f"{ident}-count", "text", {"state": f"mp.welcome.team{team}.count"})
    flag = label(f"{ident}-flag", PLACEHOLDER, {**absolute(left=16, top=TEAM_BAND_H + 28, width=width - 32, height=18),
                 **typeface("lowpixel", 14, 18, [1, 1, 1, 0.75]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    doc.bind(f"{ident}-flag.text", f"{ident}-flag", "text", {"state": f"mp.welcome.team{team}.flag"})
    lock = padlock(f"{ident}-lock", rgb(ERROR))
    lock["properties"].update(absolute(left=width - 30, top=TEAM_BAND_H + 9))   # under the band, clear of the score
    unavailable = {"op": "!", "args": [{"state": f"{key}.available"}]}
    doc.bind(f"{ident}-lock.display", f"{ident}-lock", "display", {"op": "select", "args": [unavailable, "block", "none"]})
    reason = label(f"{ident}-reason", PLACEHOLDER, {**absolute(left=16, top=TEAM_CARD_H - 22, width=width - 32, height=18),
                   **typeface("lowpixel", 14, 18, rgb(ERROR)), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                   "display": keyword("none"), "opacity": number(1)})
    doc.bind(f"{ident}-reason.text", f"{ident}-reason", "text", {"state": f"{key}.reason"})
    doc.bind(f"{ident}-reason.display", f"{ident}-reason", "display", {"op": "select", "args": [unavailable, "block", "none"]})
    ids = doc.states(ident, {
        "default": [(f"{ident}-body", "opacity", number(0.62)), (f"{ident}-focus", "opacity", number(0)),
                    (f"{ident}-label", "color", colour([1, 1, 1, 0.95]))],
        "hover": [(f"{ident}-body", "opacity", number(0.85)), (f"{ident}-focus", "opacity", number(0)),
                  (f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "focus": [(f"{ident}-body", "opacity", number(1)), (f"{ident}-focus", "opacity", number(1)),
                  (f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "pressed": [(f"{ident}-body", "opacity", number(1)), (f"{ident}-focus", "opacity", number(1)),
                    (f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "disabled": [(f"{ident}-body", "opacity", number(0.62)), (f"{ident}-focus", "opacity", number(0)),
                     (f"{ident}-label", "color", colour([1, 1, 1, 0.95]))],
    })
    event = f"join_slot_{team}"
    plate = group(ident, {**absolute(left=0, top=0, width=width, height=TEAM_CARD_H), "opacity": number(1)},
                  [body, band, mark, focus, name, score, count_label, count, flag, lock],
                  control={"role": "button", "label": PLACEHOLDER, "event": event, "states": ids})
    doc.bind(f"{ident}.opacity", ident, "opacity", {"op": "select", "args": [{"state": f"{key}.available"}, 1, 0.6]})
    holder = group(f"{ident}-slot", {**absolute(left=left, top=TEAM_CARD_TOP, width=width, height=TEAM_CARD_H),
                                     "transform": transform(), "display": keyword("none")}, [plate, reason])
    doc.bind(f"{ident}-slot.display", f"{ident}-slot", "display", {"op": "select", "args": [{"state": f"{key}.shown"}, "block", "none"]})
    shake = f"shake-{ident}"
    doc.timelines.add(shake, 300, [
        track(f"{ident}-slot", "transform", [(0, transform()), (50, transform(tx=-6)), (110, transform(tx=6)), (170, transform(tx=-4)),
                                             (230, transform(tx=3)), (300, transform())]),
        track(f"{ident}-reason", "opacity", [(0, number(1)), (100, number(0.35)), (200, number(1)), (300, number(1))]),
    ])
    doc.events[event] = [{"op": "if", "condition": {"state": f"{key}.available"},
                          "then": [{"op": "setState", "values": {"card.welcome_action": team}}, {"op": "action", "action": "mpWelcomeAction"}],
                          "else": [{"op": "playTimeline", "timeline": shake}]}]
    return holder, ident


def join_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, list]:
    """The Welcome card's Join page (section 14.18): the mode and map; the
    match state with the time left in the value color, the limit and the
    player count. Team modes then show the two team cards, Auto join
    (focused, naming the team it picks) and Spectate; deathmatch the three
    leaders, Join game and Spectate; Tourney the arenas in play and joining or
    leaving the tournament. The game derives each action again when it is
    chosen."""
    page_width = width - 2 * INSET
    page_height = height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16
    doc.state.update({
        "card.welcome_action": {"type": "number", "initial": -1},
        "mp.welcome.match": {"type": "string", "initial": ""}, "mp.welcome.state": {"type": "string", "initial": ""},
        "mp.welcome.limit": {"type": "string", "initial": ""}, "mp.welcome.team_mode": {"type": "boolean", "initial": False},
        "mp.welcome.arena_count": {"type": "number", "initial": 0},
        **{f"mp.welcome.team{team}.{part}": {"type": "string", "initial": ""} for team in range(2) for part in ("score", "count", "flag")},
        **{f"mp.welcome.leader{rank}.{part}": {"type": "string", "initial": ""} for rank in range(3) for part in ("name", "score")},
        **{f"mp.welcome.arena{arena}": {"type": "string", "initial": ""} for arena in range(4)},
    })
    match = label("join-match", PLACEHOLDER, {**absolute(left=0, top=0, width=page_width, height=26),
                  **typeface("marine", 18, 26, [1, 1, 1, 0.9]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    doc.bind("join-match.text", "join-match", "text", {"state": "mp.welcome.match"})
    line = {"position": keyword("relative"), "display": keyword("block"), "white-space": keyword("nowrap"), "margin-right": length(16),
            "flex-shrink": number(0)}
    facts = []
    for node, state, face, size, tint in (("join-state", "mp.welcome.state", "lowpixel", 15, [1, 1, 1, 0.8]),
                                          ("join-clock", "mp.clock", "lowpixel", 17, rgb(VALUE)),
                                          ("join-limit", "mp.welcome.limit", "lowpixel", 15, [1, 1, 1, 0.7]),
                                          ("join-players", "mp.welcome.players", "lowpixel", 15, [1, 1, 1, 0.7])):
        facts.append(label(node, PLACEHOLDER, {**line, **typeface(face, size, 22, tint)}))
        doc.bind(f"{node}.text", node, "text", {"state": state})
    facts_row = group("join-facts", {**absolute(left=0, top=28, width=page_width, height=22), "display": keyword("flex"),
                                     "flex-direction": keyword("row"), "align-items": keyword("center"), "overflow": keyword("hidden")}, facts)
    # Team modes: the cards side by side, then Auto join and Spectate.
    card_w = (page_width - 12) / 2
    cards = [team_card(doc, team, round(team * (card_w + 12), 3), round(card_w, 3)) for team in range(2)]
    team_steps = lambda slot: [{"op": "setState", "values": {"card.welcome_action": slot}}, {"op": "action", "action": "mpWelcomeAction"}]
    automatic = action_plate(doc, "join-slot-2", "mp.join2", min(page_width, SLOT_W), TEAM_CARD_TOP + TEAM_CARD_H + 10, "join_slot_2",
                             team_steps(2), primary=True)
    spectate = action_plate(doc, "join-slot-3", "mp.join3", min(page_width, SLOT_W), TEAM_CARD_TOP + TEAM_CARD_H + 10 + SLOT_PITCH,
                            "join_slot_3", team_steps(3))
    if TEAM_CARD_TOP + TEAM_CARD_H + 10 + SLOT_PITCH + 58 > page_height:
        raise SystemExit("the Join page's team layout leaves the page")
    team_layout = group("join-teams", {**absolute(left=0, top=0, width=page_width, height=page_height), "display": keyword("none")},
                        [card for card, _ident in cards] + [automatic, spectate])
    doc.bind("join-teams.display", "join-teams", "display", {"op": "select", "args": [{"state": "mp.welcome.team_mode"}, "block", "none"]})
    # Elsewhere: the three leaders (or Tourney's arenas), then Join game (or
    # the tournament) and Spectate, on the same slots.
    heading = {**typeface("lowpixel", 13, 18, HEADING), "white-space": keyword("nowrap")}
    leaders = [label("join-leaders-label", LEADERS_KEY, {**absolute(left=0, top=56, width=page_width, height=18), **heading})]
    for rank in range(3):
        top = 76 + 22 * rank
        leaders.append(label(f"join-leader-{rank}-rank", PLACEHOLDER, {**absolute(left=0, top=top, width=24, height=22),
                             **typeface("lowpixel", 16, 22, rgb(VALUE)), "white-space": keyword("nowrap")}))
        doc.bind(f"join-leader-{rank}-rank.text", f"join-leader-{rank}-rank", "text",
                 {"op": "select", "args": [{"op": "!=", "args": [{"state": f"mp.welcome.leader{rank}.name"}, ""]},
                                           {"op": "numberText", "args": [rank + 1]}, ""]})
        leaders.append(label(f"join-leader-{rank}", PLACEHOLDER, {**absolute(left=28, top=top, width=page_width * 0.6, height=22),
                             **typeface("lowpixel", 16, 22, [1, 1, 1, 0.85]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}))
        doc.bind(f"join-leader-{rank}.text", f"join-leader-{rank}", "text", {"state": f"mp.welcome.leader{rank}.name"})
        leaders.append(label(f"join-leader-{rank}-score", PLACEHOLDER, {**absolute(left=page_width * 0.6 + 32, top=top, width=60, height=22),
                             **typeface("lowpixel", 16, 22, rgb(VALUE)), "text-align": keyword("right"), "white-space": keyword("nowrap")}))
        doc.bind(f"join-leader-{rank}-score.text", f"join-leader-{rank}-score", "text", {"state": f"mp.welcome.leader{rank}.score"})
    leader_group = group("join-leaders", {**absolute(left=0, top=0, width=page_width, height=150), "display": keyword("block")}, leaders)
    arenas = [label("join-arenas-label", ARENAS_KEY, {**absolute(left=0, top=56, width=page_width, height=18), **heading})]
    for arena in range(4):
        node = f"join-arena-{arena}"
        arenas.append(label(node, PLACEHOLDER, {**absolute(left=0, top=76 + 18 * arena, width=page_width, height=18),
                            **typeface("lowpixel", 15, 18, [1, 1, 1, 0.8]), "white-space": keyword("pre"), "overflow": keyword("hidden")}))
        doc.bind(f"{node}.text", node, "text", {"state": f"mp.welcome.arena{arena}"})
    arena_group = group("join-arenas", {**absolute(left=0, top=0, width=page_width, height=150), "display": keyword("none")}, arenas)
    tourney = {"op": ">", "args": [{"state": "mp.welcome.arena_count"}, 0]}
    doc.bind("join-arenas.display", "join-arenas", "display", {"op": "select", "args": [tourney, "block", "none"]})
    doc.bind("join-leaders.display", "join-leaders", "display", {"op": "select", "args": [tourney, "none", "block"]})
    solo = [action_plate(doc, f"join-solo-{slot}", f"mp.join{slot}", min(page_width, SLOT_W), 156 + SLOT_PITCH * slot, f"join_solo_{slot}",
                         team_steps(slot), primary=slot == 0) for slot in range(2)]
    solo_layout = group("join-solo", {**absolute(left=0, top=0, width=page_width, height=page_height), "display": keyword("none")},
                        [leader_group, arena_group, *solo])
    doc.bind("join-solo.display", "join-solo", "display", {"op": "select", "args": [{"state": "mp.welcome.team_mode"}, "none", "block"]})
    doc.session("mpWelcomeAction", "mpWelcomeAction")
    controls = [ident for _card, ident in cards] + ["join-slot-2", "join-slot-3", "join-solo-0", "join-solo-1"]
    focus = [{"op": "if", "condition": {"state": "mp.welcome.team_mode"}, "then": [{"op": "focus", "control": "join-slot-2"}],
              "else": [{"op": "focus", "control": "join-solo-0"}]}]
    return page_group(doc, index, ident, width, height, [match, facts_row, team_layout, solo_layout], controls), focus


# -------------------------------------------------------------- Server page

ROTATION_SLOTS = 16
RULES = ("#str_231023", "#str_231024", "#str_231025", "#str_200038", "#str_231026", "#str_231027", "#str_231028")
HEADING = [0.545, 0.588, 0.294, 1]   # #8B964B, the multiplayer headings' olive


def server_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, str]:
    """The Server page (section 14.18): the name and address, the server's
    message in #FFFF8D, the rules in two columns (mode, map, limits, players,
    friendly fire, balance and next map) and the map rotation, the current map
    in orange. It has no actions; its tab keeps the focus."""
    page_width = width - 2 * INSET
    doc.state.update({
        "mp.server.name": {"type": "string", "initial": ""},
        "mp.server.address": {"type": "string", "initial": ""},
        "mp.server.message": {"type": "string", "initial": ""},
        **{f"mp.rule{rule}": {"type": "string", "initial": ""} for rule in range(len(RULES))},
        **{f"mp.rotation{slot}": {"type": "string", "initial": ""} for slot in range(ROTATION_SLOTS)},
        "mp.rotation_count": {"type": "number", "initial": 0},
        "mp.rotation_current": {"type": "number", "initial": -1},
    })
    name = label("server-name", PLACEHOLDER, {**absolute(left=0, top=0, width=page_width, height=28),
                 **typeface("marine", 20, 28, [1, 1, 1, 0.9]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    address_label = label("server-address-label", "#str_231030", {**absolute(left=0, top=30, width=120, height=20),
                          **typeface("lowpixel", 14, 20, HEADING), "white-space": keyword("nowrap")})
    address = label("server-address", PLACEHOLDER, {**absolute(left=124, top=30, width=page_width - 124, height=20),
                    **typeface("profont", 15, 20, [1, 1, 1, 0.75]), "white-space": keyword("nowrap")})
    message = label("server-message", PLACEHOLDER, {**absolute(left=0, top=56, width=page_width, height=60),
                    **typeface("lowpixel", 17, 20, b.rgb("#FFFF8D")), "white-space": keyword("pre-line"), "overflow": keyword("hidden")})
    for node, state in (("server-name", "mp.server.name"), ("server-address", "mp.server.address"), ("server-message", "mp.server.message")):
        doc.bind(f"{node}.text", node, "text", {"state": state})
    # Two columns of rules, the left one wider for the limits; each column's
    # labels take the width of its longest in any language.
    rules = []
    split = round(page_width * 0.55, 3)
    columns_x = (0.0, split)
    columns_w = (split - 12, page_width - split)
    label_w = [max(b.text_width("lowpixel", text, 14) for key in RULES[start:stop] for text in any_text(key).values()) + 6
               for start, stop in ((0, 4), (4, len(RULES)))]
    for rule, key in enumerate(RULES):
        side = 0 if rule < 4 else 1
        left, top = columns_x[side], 124 + 24 * (rule % 4)
        rules.append(label(f"server-rule-{rule}-label", key, {**absolute(left=left, top=top, width=round(label_w[side], 3), height=22),
                           **typeface("lowpixel", 14, 22, HEADING), "white-space": keyword("nowrap")}))
        value_left = round(left + label_w[side] + 6, 3)
        value = label(f"server-rule-{rule}", PLACEHOLDER, {**absolute(left=value_left, top=top, width=round(columns_w[side] - label_w[side] - 6, 3),
                      height=22), **typeface("lowpixel", 15, 22, [1, 1, 1, 0.85]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
        doc.bind(f"server-rule-{rule}.text", f"server-rule-{rule}", "text", {"state": f"mp.rule{rule}"})
        rules.append(value)
    heading = label("server-rotation-label", "#str_231029", {**absolute(left=0, top=228, width=page_width, height=20),
                    **typeface("lowpixel", 14, 20, HEADING), "white-space": keyword("nowrap")})
    rotation = []
    columns, pitch_x = 4, page_width / 4
    for slot in range(ROTATION_SLOTS):
        node = f"server-rotation-{slot}"
        rotation.append(label(node, PLACEHOLDER, {**absolute(left=pitch_x * (slot % columns), top=250 + 19 * (slot // columns),
                                                            width=pitch_x - 8, height=19),
                              **typeface("lowpixel", 15, 19, [1, 1, 1, 0.7]), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                              "display": keyword("none")}))
        doc.bind(f"{node}.text", node, "text", {"state": f"mp.rotation{slot}"})
        doc.bind(f"{node}.display", node, "display",
                 {"op": "select", "args": [{"op": "<", "args": [slot, {"state": "mp.rotation_count"}]}, "block", "none"]})
        current = {"op": "==", "args": [{"state": "mp.rotation_current"}, slot]}
        hot = rgb(ORANGE)
        doc.bind(f"{node}.color", node, "color", [{"op": "select", "args": [current, hot[channel], rest]}
                                                  for channel, rest in enumerate((1, 1, 1, 0.7))])
    children = [name, address_label, address, message, *rules, heading, *rotation]
    return page_group(doc, index, ident, width, height, children, []), f"tab-{ident}"


# --------------------------------------------------------------- Vote page

# The drafted call's rows in the game's retainedVoteField_t order, which the
# session's RETAINED_MP_VOTE_FIELDS follows with each row's verb: the game's
# key, the verb, the label and the control: a choice of a list the game
# publishes (its option slots), a yes or no, or a limit's slider (minimum,
# maximum, step). A list's rows are mp.vote.<key><row> with their count in
# mp.vote.<key>_count; the kick choice leads with no one (-1).
VOTE_ROWS = (
    ("map", "mpVoteMap", "#str_200209", ("choice", MAP_CATALOG)),
    ("gametype", "mpVoteGameType", "#str_200210", ("choice", 16)),
    ("timelimit", "mpVoteTimeLimit", "#str_200211", ("slider", 0, 60, 1)),
    ("fraglimit", "mpVoteFragLimit", "#str_200060", ("slider", 0, 100, 1)),
    ("capturelimit", "mpVoteCaptureLimit", "#str_200215", ("slider", 1, 50, 1)),
    ("tourneylimit", "mpVoteTourneyLimit", "#str_200214", ("slider", 1, 20, 1)),
    ("controltime", "mpVoteControlTime", "#str_222013", ("slider", 10, 600, 10)),
    ("balance", "mpVoteBalance", "#str_200212", ("toggle",)),
    ("shuffle", "mpVoteShuffle", "#str_201044", ("toggle",)),
    ("restart", "mpVoteRestart", "#str_200208", ("toggle",)),
    ("buying", "mpVoteBuying", "#str_222000", ("toggle",)),
    ("kick", "mpVoteKick", "#str_200207", ("choice", PLAYER_ROWS)),
)
VOTE_RUNNING, VOTE_CALL, VOTE_NONE_RUNNING = "#str_231043", "#str_231044", "#str_231047"
VOTE_LOCKED, VOTE_NO_ONE = "#str_231052", "#str_231053"
VOTE_LINES = 6                     # RETAINED_VOTE_LINES in the game
VOTE_LEFT_W = 360.0                # the running vote's column; the call takes the rest
VOTE_ROW_H, VOTE_OPTION_H, VOTE_VISIBLE_ROWS = 26.0, 22.0, 8
VOTE_HEAD_H = 24.0
LABEL_LINE = 13.0                  # a two-line setting label's line pitch


def row_face(wrap: bool, row_h: float = VOTE_ROW_H) -> dict:
    """A setting row's label: one line at 15 dp, or two at 14 dp."""
    if wrap:
        return {**typeface("lowpixel", 14, LABEL_LINE, [1, 1, 1, 0.85]), "white-space": keyword("normal")}
    return {**typeface("lowpixel", 15, row_h, [1, 1, 1, 0.85]), "white-space": keyword("nowrap")}


def wrapped_width(text: str, size: float = 14, lines_allowed: int = 2) -> float:
    """The narrowest width that holds `text` in `lines_allowed` lines (two by
    default) at `size` dp."""
    words = text.split()
    width = max(b.text_width("lowpixel", word, size) for word in words)
    while True:
        lines, line = 1, ""
        for word in words:
            trial = f"{line} {word}".strip()
            if line and b.text_width("lowpixel", trial, size) > width:
                lines, line = lines + 1, word
            else:
                line = trial
        if lines <= lines_allowed:
            return width
        width += 2


def row_states(doc: Document, ident: str, labelled: bool = True) -> dict:
    """A setting row's feedback: its focus rail and wash come up on hover and
    focus, and its label (a table cell has none) turns orange with focus."""
    def state(rail, wash, tint):
        tracks = [(f"{ident}-focus", "opacity", number(rail)), (f"{ident}-wash", "opacity", number(wash))]
        return tracks + ([(f"{ident}-label", "color", colour(tint))] if labelled else [])
    return doc.states(ident, {
        "default": state(0, 0, [1, 1, 1, 0.85]), "hover": state(0, 0.6, [1, 1, 1, 1]),
        "focus": state(1, 1, rgb(ORANGE)), "pressed": state(1, 1, rgb(ORANGE)), "disabled": state(0, 0, [1, 1, 1, 0.85]),
    })


def choice_popup(ident: str, entries: list, width: float) -> tuple[dict, list]:
    """A choice's list (section 7: it unfolds under its row into a constrained
    scrollable popup with the stock dropdown's black body, its #B2CD43 1 dp
    border at 20 % and 22 dp rows). `entries` hold each row's label (a
    string key, a key with the index into its stock list, or {"state": key}
    for a label the game publishes) and value, and may add whether the
    option can be chosen (False, or an expression): one that cannot still
    shows."""
    options, rows = [], []
    for index, (text, value, *enabled) in enumerate(entries):
        option = f"{ident}-option-{index}"
        band = lambda node, tint: vector(node, {**FULL, "display": keyword("none")}, [
            path("band", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})], fill=solid(tint))])
        selected = band(f"{option}-selected", rgb(OLIVE, 0.35))
        highlight = band(f"{option}-highlight", rgb(ORANGE, 0.3))
        key = text[0] if isinstance(text, tuple) else text
        caption = label(f"{option}-label", key if isinstance(key, str) else PLACEHOLDER,
                        {"position": keyword("relative"), "display": keyword("block"), "padding-left": length(8),
                         **typeface("lowpixel", 15, VOTE_OPTION_H, [1, 1, 1, 0.9]), "white-space": keyword("nowrap"),
                         "overflow": keyword("hidden")})
        rows.append(group(option, {"position": keyword("relative"), "display": keyword("block"), "width": length(100, "%"),
                                   "height": length(VOTE_OPTION_H)}, [selected, highlight, caption]))
        entry = {"id": option, "node": option, "label": key, "value": value,
                 "parts": {"label": f"{option}-label", "selected": f"{option}-selected", "highlight": f"{option}-highlight"}}
        if isinstance(text, tuple):
            entry["labelIndex"] = text[1]
        if enabled:
            entry["enabled"] = enabled[0]
        options.append(entry)
    view_h = min(VOTE_VISIBLE_ROWS, len(entries)) * VOTE_OPTION_H
    # The pop-up's opaque black frame (user rule, 5 November 2026), beneath
    # the body: the fill covers its inner half, so 2 dp of black shows outside
    # the faint border.
    edge = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    frame = vector(f"{ident}-popup-frame", FULL, [
        path("rim", edge, stroke=stroke(solid([0, 0, 0, 1]), 4)),
        path("body", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})], fill=solid([0, 0, 0, 0.96]),
             stroke=stroke(solid(b.rgb("#B2CD43", 0.2)), 1)),
    ])
    content = group(f"{ident}-content", {**absolute(left=0, top=0), "width": length(100, "%")}, rows)
    viewport = group(f"{ident}-viewport", {"position": keyword("relative"), "display": keyword("block"), "width": length(width - 18),
                                           "height": length(view_h), "margin-left": length(2), "margin-top": length(2)}, [content])
    thumb = group(f"{ident}-scroll-thumb", {**absolute(left=0, top=0, width=8, height=24)}, [
        vector(f"{ident}-scroll-ink", FULL, [path("thumb", [(1, 0), (7, 0), (7, {"fraction": 1}), (1, {"fraction": 1})],
                                                  fill=solid(rgb(OLIVE, 0.9)))])])
    track_node = group(f"{ident}-scroll-track", {**absolute(left=width - 12, top=2, width=8, height=view_h)}, [
        vector(f"{ident}-scroll-trough", FULL, [path("trough", [(3, 0), (5, 0), (5, {"fraction": 1}), (3, {"fraction": 1})],
                                                     fill=solid([1, 1, 1, 0.12]))]), thumb])
    popup = group(f"{ident}-popup", {**absolute(left=0, top=VOTE_ROW_H, width=width, height=view_h + 4), "display": keyword("none")},
                  [frame, viewport, track_node])
    return popup, options


def value_row(doc: Document, ident: str, label_key: str | None, kind: str, width: float, value_x: float, *, value: dict, action: str,
              entries: list | None = None, count: dict | None = None, slider: tuple | None = None, allowed: dict | None = None,
              shown: dict | None = None, accessible: str | None = None, wrap: bool = False, row_h: float = VOTE_ROW_H,
              placement: str = "card") -> dict:
    """A setting row (section 7): a light plate with the label 21 dp in and
    the value at the value column in marine.value, the whole row one control
    (a table cell has no label: its row's text names it). A choice unfolds
    its list under the row; a yes or no is the stock check box; a slider runs
    along a tick track with its number after it. `value` reads the setting
    back and `action` takes the new value. With `allowed` the row dims with
    the padlock after its label where it is false; with `shown` it shows only
    where that is true. A `wrap` label takes two lines at 14 dp. `row_h` is
    the row's height, 26 dp unless its page sets rows on a taller pitch;
    `placement` the ancestor a choice's list unfolds within."""
    value_w = width - value_x - 12
    middle = (row_h - VOTE_ROW_H) / 2 if row_h != VOTE_ROW_H else 0
    plate = vector(f"{ident}-plate", FULL, [
        path("plate", [(6, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}), (0, {"fraction": 1, "dp": -1}), (0, 7)],
             fill=solid(rgb(OLIVE, 0.12)), stroke=stroke(solid(rgb(OLIVE, 0.32)), 1)),
    ])
    wash = vector(f"{ident}-wash", {**FULL, "opacity": number(0)}, [
        path("wash", [(6, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}), (0, {"fraction": 1, "dp": -1}), (0, 7)],
             fill=linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(OLIVE, 0.45)), (1, rgb(OLIVE, 0.1))])),
    ])
    rail = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=4, height=row_h), "opacity": number(0)}, [
        path("rail", [(0, 7), (6, 1), (6, row_h - 1), (0, row_h - 1)], fill=solid(rgb(ORANGE)))])
    children = [plate, wash, rail]
    if label_key is not None:
        # The label and, on a locked row, the padlock after it share the label
        # column; a locked row's label gives way to its lock.
        caption = label(f"{ident}-label", label_key, {"position": keyword("relative"), "display": keyword("block"), "flex-shrink": number(1),
                        "min-width": length(0), **row_face(wrap, row_h), "overflow": keyword("hidden")})
        head_children = [caption]
        if allowed is not None:
            lock = padlock(f"{ident}-lock", rgb(ERROR))
            doc.bind(f"{ident}-lock.display", f"{ident}-lock", "display", {"op": "select", "args": [allowed, "none", "block"]})
            head_children.append(lock)
        children.append(group(f"{ident}-head", {**absolute(left=21, top=0, width=value_x - 29, height=row_h), "display": keyword("flex"),
                                                "flex-direction": keyword("row"), "align-items": keyword("center")}, head_children))
    spec = {"role": kind, "label": accessible or label_key, "action": action, "value": value,
            "states": row_states(doc, ident, label_key is not None)}
    if kind == "choice":
        # Lists take the 14 dp face: the stock maps' names run long, in capitals.
        value_node = label(f"{ident}-value", PLACEHOLDER, {**absolute(left=value_x, top=0, width=value_w - 14, height=row_h),
                           **typeface("lowpixel", 14, row_h, rgb(VALUE)), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
        chevron = vector(f"{ident}-chevron", absolute(left=width - 24, top=10 + middle, width=12, height=6), [
            path("chevron", [(0, 0), (6, 6), (12, 0)], closed=False, stroke=stroke(solid(rgb(VALUE)), 1.5))])
        popup, options = choice_popup(ident, entries, value_w + 8)
        popup["properties"]["left"] = length(value_x - 8)
        popup["properties"]["top"] = length(row_h)
        children += [value_node, chevron, popup]
        spec.update({"parts": {"popup": f"{ident}-popup", "viewport": f"{ident}-viewport", "content": f"{ident}-content",
                               "value": f"{ident}-value"},
                     "visibleRows": min(VOTE_VISIBLE_ROWS, len(entries)), "options": options})
        if count is not None:
            spec["optionCount"] = count
        spec.update({"placementBounds": placement, "scrollbar": {"track": f"{ident}-scroll-track", "thumb": f"{ident}-scroll-thumb",
                                                              "lineStep": VOTE_OPTION_H, "minimumThumb": 24}})
    elif kind == "toggle":
        box = vector(f"{ident}-box", absolute(left=value_x, top=6 + middle, width=14, height=14), [
            path("box", [(0.75, 0.75), (13.25, 0.75), (13.25, 13.25), (0.75, 13.25)], stroke=stroke(solid(rgb(VALUE)), 1.5))])
        checked = group(f"{ident}-checked", {**absolute(left=value_x, top=6 + middle, width=14, height=14), "display": keyword("none")}, [
            vector(f"{ident}-mark", FULL, [path("mark", [(3.5, 3.5), (10.5, 3.5), (10.5, 10.5), (3.5, 10.5)], fill=solid(rgb(VALUE)))])])
        children += [box, checked]
        spec["parts"] = {"checked": f"{ident}-checked"}
    else:
        minimum, maximum, step, decimals = slider
        track_w = value_w - 56
        ticks = []
        steps = 20
        for tick in range(steps + 1):
            x = round(track_w * tick / steps, 3)
            ticks.append(path(f"tick{tick}", [(x, 13 - 3 - 5 * tick / steps), (x, 13 + 3 + 5 * tick / steps)], closed=False,
                              stroke=stroke(solid([1, 1, 1, 0.45]), 1)))
        track_node = group(f"{ident}-track", absolute(left=value_x, top=0, width=track_w, height=VOTE_ROW_H), [
            vector(f"{ident}-ramp", FULL, ticks),
            group(f"{ident}-fill", {**absolute(left=0, top=11, width=0, height=4)}, [
                vector(f"{ident}-fill-ink", FULL, [path("fill", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, 4), (0, 4)],
                                                        fill=solid(rgb(VALUE, 0.8)))])]),
            group(f"{ident}-thumb", absolute(left=0, top=5, width=4, height=16), [
                vector(f"{ident}-thumb-ink", FULL, [path("thumb", [(0, 0), (4, 0), (4, 16), (0, 16)], fill=solid(rgb(VALUE)))])]),
        ])
        number_node = label(f"{ident}-value", PLACEHOLDER, {**absolute(left=value_x + track_w + 8, top=0, width=48, height=VOTE_ROW_H),
                            **typeface("lowpixel", 15, VOTE_ROW_H, rgb(VALUE)), "text-align": keyword("right"), "white-space": keyword("nowrap")})
        children += [track_node, number_node]
        spec.update({"minimum": minimum, "maximum": maximum, "step": step, "decimals": decimals, "orientation": "horizontal",
                     "parts": {"track": f"{ident}-track", "fill": f"{ident}-fill", "thumb": f"{ident}-thumb", "value": f"{ident}-value"}})
    row = group(ident, {"position": keyword("relative"), "display": keyword("none" if shown is not None else "block"), "width": length(width),
                        "height": length(row_h), "opacity": number(1), "flex-shrink": number(0)}, children, control=spec)
    if shown is not None:
        doc.bind(f"{ident}.display", ident, "display", {"op": "select", "args": [shown, "block", "none"]})
    if allowed is not None:
        doc.bind(f"{ident}.opacity", ident, "opacity", {"op": "select", "args": [allowed, 1, 0.38]})
    return row


def stack_row(doc: Document, row: dict, width: float, value_x: float, row_h: float) -> None:
    """Section 2's reflow of a setting row (value_row, match_number_row or
    create_server.text_row) in a column that cannot widen: above 100 % text
    the row grows with the text, its label over its value, each line half
    the 100 % row's height times the text size. The label runs across the
    row on the lines the running language needs (one, or two where a long
    translation needs them); the value, the check box, the slider or the
    field starts at the label's edge on the line under it and runs to the
    row's end, a choice's value ending in an ellipsis past it; a field's
    validation line stays under the row. The check box, chevron and slider
    track are furniture and keep their size. A choice's options grow with
    the text. Every binding gives the authored value at 100 %
    (check_text_bindings); generation fails where a label in a shipped
    language needs more than two lines at 200 %."""
    b.text_source(doc)
    TEXT, LARGER = b.TEXT, b.LARGER
    ident = row["id"]
    nodes = {node["id"]: node for node in b.walk_nodes(row)}
    role = row["control"]["role"]

    def larger(node: str, key: str, grown, authored=None) -> None:
        if authored is None:
            authored = nodes[node]["properties"][key]["value"]
        doc.bind(f"text.{node}.{key}", node, key, {"op": "select", "args": [LARGER, grown, authored]})
    half = {"op": "*", "args": [row_h / 2, TEXT]}
    lead = half    # the value's line: under the label's
    if f"{ident}-head" in nodes:
        caption = nodes[f"{ident}-label"]["properties"]
        face, size = caption["font-family"]["value"], caption["font-size"]["value"]
        counts = b.wrapped_lines((row["control"]["label"],), face, size, width - 33)
        for language, found in counts.items():
            if found[2] > 2:
                raise SystemExit(f"{ident}'s label in {language} needs {found[2]} lines of its row at 200 % text")
        if any(found[scale] > 1 for found in counts.values() for scale in (1.5, 2)):
            lines = b.text_buckets({scale: b.per_language(doc, {language: found[scale] for language, found in counts.items()})
                                    for scale in (1.5, 2)})
            lead = {"op": "*", "args": [half, lines]}
            caption_node = nodes[f"{ident}-label"]
            if caption["white-space"]["value"] == "nowrap":
                larger(caption_node["id"], "white-space", "normal")
        larger(f"{ident}-head", "width", width - 33)
        larger(f"{ident}-head", "height", lead)
        if caption["white-space"]["value"] == "nowrap":
            larger(f"{ident}-label", "line-height", row_h / 2)
    # A row that opens a dialog, or an entry dialog's lone field without a
    # label, has no line of its own for the value: it grows with the text.
    alone = role in ("text", "number") and f"{ident}-head" not in nodes
    if alone:
        lead, half = 0, b.text_times(row_h)
    grown = b.text_times(row_h) if role == "button" or alone else b.text_sum(lead, half)

    def middle_of_line(size: float):
        """The top that centres `size` dp in the value's line."""
        return b.text_sum(-size / 2, lead, {"op": "*", "args": [row_h / 4, TEXT]})
    larger(ident, "height", grown)
    rail = nodes[f"{ident}-focus"]
    b.in_fractions(rail, rail["properties"]["width"]["value"], row_h)
    larger(rail["id"], "height", grown)
    if role == "button":
        # A row that opens a dialog grows with the text, its label centred on
        # one line or filling it with the two a long translation needs, and
        # its chevron centred at the row's end.
        caption = nodes[f"{ident}-label"]["properties"]
        counts = b.wrapped_lines((row["control"]["label"],), caption["font-family"]["value"], caption["font-size"]["value"],
                                 caption["width"]["value"])
        for language, found in counts.items():
            if found[2] > 2:
                raise SystemExit(f"{ident}'s label in {language} needs {found[2]} lines of its row at 200 % text")
        if any(found[scale] > 1 for found in counts.values() for scale in (1.5, 2)):
            lines = b.text_buckets({scale: b.per_language(doc, {language: found[scale] for language, found in counts.items()})
                                    for scale in (1.5, 2)})
            larger(f"{ident}-label", "white-space", "normal")
            larger(f"{ident}-label", "line-height", {"op": "/", "args": [row_h, lines]})
        larger(f"{ident}-label", "height", grown)
        larger(f"{ident}-chevron", "top", b.text_sum(-nodes[f"{ident}-chevron"]["properties"]["height"]["value"] / 2, half))
    elif role == "choice":
        value = nodes[f"{ident}-value"]
        larger(value["id"], "left", 21)
        larger(value["id"], "top", lead)
        larger(value["id"], "height", half)
        larger(value["id"], "line-height", row_h / 2)
        larger(value["id"], "width", width - 21 - 26)
        value["properties"]["text-overflow"] = keyword("clip")
        larger(value["id"], "text-overflow", "ellipsis", "clip")
        chevron = nodes[f"{ident}-chevron"]["properties"]
        larger(f"{ident}-chevron", "top", middle_of_line(chevron["height"]["value"]))
        for option in row["control"]["options"]:
            doc.bind(f"text.{option['node']}.height", option["node"], "height", b.text_times(nodes[option["node"]]["properties"]["height"]["value"]))
    elif role == "toggle":
        for part in (f"{ident}-box", f"{ident}-checked"):
            larger(part, "left", 21)
            larger(part, "top", middle_of_line(nodes[part]["properties"]["height"]["value"]))
    elif role == "slider":
        track_node = nodes[f"{ident}-track"]["properties"]
        track_w, track_h = track_node["width"]["value"], track_node["height"]["value"]
        larger(f"{ident}-track", "left", 21)
        larger(f"{ident}-track", "top", middle_of_line(track_h))
        number_node = nodes[f"{ident}-value"]
        larger(number_node["id"], "left", 21 + track_w + 8)
        larger(number_node["id"], "top", lead)
        larger(number_node["id"], "height", half)
        larger(number_node["id"], "line-height", row_h / 2)
        larger(number_node["id"], "width", b.text_times(number_node["properties"]["width"]["value"]))
    else:
        # A number or text field: its viewport and text fill the value's line
        # but a 2 dp margin above and below; the runtime places the caret,
        # selection and composition in it.
        field = f"{ident}-viewport"
        if alone:
            # The field keeps its place and grows in it with the text.
            for key in ("top", "height"):
                doc.bind(f"text.{field}.{key}", field, key, b.text_times(nodes[field]["properties"][key]["value"]))
            doc.bind(f"text.{ident}-text.height", f"{ident}-text", "height", b.text_times(nodes[f"{ident}-text"]["properties"]["height"]["value"]))
        else:
            larger(field, "left", 13)
            larger(field, "width", width - 25)
            larger(field, "top", b.text_sum(2, lead))
            larger(field, "height", b.text_sum(-4, half))
            larger(f"{ident}-text", "height", b.text_sum(-4, half))
            larger(f"{ident}-text", "line-height", b.text_sum(row_h / 2, {"op": "/", "args": [-4, TEXT]}))
    if f"{ident}-validation" in nodes:
        message = nodes[f"{ident}-validation"]["properties"]
        larger(f"{ident}-validation", "top", b.text_sum(1, grown))
        doc.bind(f"text.{ident}-validation.height", f"{ident}-validation", "height", b.text_times(message["height"]["value"]))


def vote_row(doc: Document, key: str, verb: str, label_key: str, control: tuple, width: float, value_x: float) -> dict:
    """One field of the drafted call, a setting row. A list's rows come from
    the game (the kick list leads with no one); the row shows where the
    drafted game type uses its field (mp.vote.<key>.row_shown) and dims with
    the padlock after its label where the server forbids it (row_allowed)."""
    kind = control[0]
    state_key = f"mp.vote.{key}"
    boolean = kind == "toggle"
    doc.state.update({
        f"{state_key}.row_shown": {"type": "boolean", "initial": False},
        f"{state_key}.row_allowed": {"type": "boolean", "initial": True},
        state_key: {"type": "boolean", "initial": False} if boolean else {"type": "number", "initial": -1 if kind == "choice" else 0},
    })
    doc.actions[verb] = {"operation": "session.menuValue", "input": "boolean" if boolean else "number",
                         "arguments": {"command": verb, "value": {"input": "value"}}}
    entries, count, slider = None, None, None
    if kind == "choice":
        slots = control[1]
        for row in range(slots):
            doc.state[f"{state_key}{row}"] = {"type": "string", "initial": ""}
        doc.state[f"{state_key}_count"] = {"type": "number", "initial": 0}
        leading = [(VOTE_NO_ONE, -1)] if key == "kick" else []
        entries = leading + [({"state": f"{state_key}{row}"}, row) for row in range(slots)]
        count = {"state": f"{state_key}_count"}
    elif kind == "slider":
        slider = (*control[1:], 0)
    row = value_row(doc, f"vote-row-{key}", label_key, kind, width, value_x, value={"state": state_key}, action=verb,
                    entries=entries, count=count, slider=slider, allowed={"state": f"{state_key}.row_allowed"},
                    shown={"state": f"{state_key}.row_shown"})
    if key == "kick":
        doc.state["mp.vote.kick_catalog"] = {"type": "string", "initial": ""}
        row["control"]["context"] = {"state": "mp.vote.kick_catalog"}
        doc.actions[verb]["arguments"]["catalog"] = {"state": "mp.vote.kick_catalog"}
    return row


def vote_keycap(ident: str, key_state: str, available: str, doc: Document) -> dict:
    """The key bound to a ballot (F1 and F2 by default; the session names it
    in `key_state`), as the prompt bar's keycaps draw it, after the plate's
    label while the player can vote; the lock takes its place otherwise."""
    shape = path("cap", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (6, {"fraction": 1}),
                         (0, {"fraction": 1, "dp": -6})], fill=solid([1, 1, 1, 1]))
    name = label(f"{ident}-key", PLACEHOLDER, {"position": keyword("relative"), "display": keyword("block"),
                 **typeface("lowpixel", 13, PROMPT_H, [0.04, 0.05, 0.035, 1]), "white-space": keyword("nowrap")})
    doc.bind(f"{ident}-key.text", f"{ident}-key", "text", {"state": key_state})
    cap = vector(f"{ident}-cap", {"position": keyword("relative"), "display": keyword("block"), "height": length(PROMPT_H),
                                  "padding-left": length(7), "padding-right": length(7), "opacity": number(0.85)}, [shape], [name])
    node = group(ident, {"position": keyword("relative"), "display": keyword("none"), "height": length(PROMPT_H),
                         "margin-left": length(10), "flex-shrink": number(0)}, [cap])
    keyed = {"op": "&&", "args": [{"state": f"{key_state}_bound"}, {"state": available}]}
    doc.bind(f"{ident}.display", ident, "display", {"op": "select", "args": [keyed, "block", "none"]})
    return node


def check_vote_labels(width: float, value_x: float) -> None:
    """Fail generation when a row's label would run under its lock, or a
    ballot's label and keycap past its plate, in any language."""
    room = value_x - 29
    over = {}
    for _key, _verb, label_key, _control in VOTE_ROWS:
        for language, text in any_text(label_key).items():
            if b.text_width("lowpixel", text, 15) > room:
                over[(label_key, language)] = round(b.text_width("lowpixel", text, 15))
    # A ballot's label leaves room for its keycap (or, unavailable, its lock);
    # Call Vote's for its lock.
    for key, room in (("#str_200906", VOTE_LEFT_W - 46 - 50), ("#str_200907", VOTE_LEFT_W - 46 - 50),
                      ("#str_200218", min(width, SLOT_W) - 46 - 20)):
        for language, text in any_text(key).items():
            if b.text_width("marine", text, 20) > room:
                over[(key, language)] = round(b.text_width("marine", text, 20))
    # The reasons on the ballots' and Call Vote's plates, the headings, and
    # the running vote's texts with short values in their formats.
    texts = [(key, "lowpixel", 14, VOTE_LEFT_W - 26) for key in ("#str_231048", "#str_231049", "#str_231050")]
    texts += [(key, "lowpixel", 14, min(width, SLOT_W) - 26) for key in ("#str_231050", "#str_108023", "#str_231049", "#str_104273",
                                                                          "#str_231051", "#str_231577", "#str_231578")]
    texts += [(VOTE_RUNNING, "lowpixel", 14, VOTE_LEFT_W), (VOTE_NONE_RUNNING, "lowpixel", 15, VOTE_LEFT_W),
              (VOTE_CALL, "lowpixel", 14, width * 0.5), (VOTE_LOCKED, "lowpixel", 14, width * 0.5)]
    texts += [(key, "lowpixel", 15, VOTE_LEFT_W) for key in ("#str_231045", "#str_231046", "#str_104435", "#str_104422", "#str_104423",
                                                             "#str_122011", "#str_104427", "#str_122009", "#str_110010", "#str_104429",
                                                             "#str_104430", "#str_104431", "#str_104432", "#str_104433", "#str_104434")]
    for key, face, size, room in texts:
        for language, text in any_text(key).items():
            sample = text.replace("%s", "Anderson").replace("%d", "20")
            if b.text_width(face, sample, size) > room:
                over[(key, language)] = round(b.text_width(face, sample, size))
    for name in ("DEATH BEFORE DISHONOR", "THE FRAGGING YARD 1V1", "CAMPGROUNDS REDUX"):
        if b.text_width("lowpixel", name, 14) > width - value_x - 26:
            over[(name, "maps")] = round(b.text_width("lowpixel", name, 14))
    if over:
        raise SystemExit(f"Vote page labels past their room: {over}")


def vote_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, list]:
    """The Vote page (section 14.18): the running vote first, with who called
    it, what it changes, the time left, the yes and no tally, and Yes and No
    with the keys bound to them; then the call-a-vote rows (map, game type,
    the limits the drafted game type uses, balance, shuffle, restart, buying
    and kick) and Call Vote, unavailable with its reason while a vote runs or
    nothing would change. The page opens on Yes while the player can vote,
    and on its first row otherwise."""
    page_width = width - 2 * INSET
    page_height = height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16
    right_x = VOTE_LEFT_W + 24
    right_w = page_width - right_x
    # The value column takes 48 % of the row: the stock maps' names are long,
    # in capitals ("DEATH BEFORE DISHONOR", "THE FRAGGING YARD 1V1").
    value_x = round(right_w * 0.52, 3)
    check_vote_labels(right_w, value_x)
    doc.state.update({
        "mp.vote.running": {"type": "boolean", "initial": False},
        "mp.vote.caller": {"type": "string", "initial": ""},
        "mp.vote.time": {"type": "string", "initial": ""},
        "mp.vote.tally": {"type": "string", "initial": ""},
        "mp.vote.line_count": {"type": "number", "initial": 0},
        **{f"mp.vote.line{line}": {"type": "string", "initial": ""} for line in range(VOTE_LINES)},
        "mp.vote.locked": {"type": "boolean", "initial": False},
        **{f"mp.keys.vote_{ballot}": {"type": "string", "initial": ""} for ballot in ("yes", "no")},
        **{f"mp.keys.vote_{ballot}_bound": {"type": "boolean", "initial": False} for ballot in ("yes", "no")},
    })
    running = {"state": "mp.vote.running"}
    heading = lambda node, key, left, w: label(node, key, {**absolute(left=left, top=0, width=w, height=20),
                                                           **typeface("lowpixel", 14, 20, HEADING), "white-space": keyword("nowrap")})
    # The running vote.
    left = [heading("vote-heading-running", VOTE_RUNNING, 0, VOTE_LEFT_W)]
    none = label("vote-none", VOTE_NONE_RUNNING, {**absolute(left=0, top=VOTE_HEAD_H, width=VOTE_LEFT_W, height=20),
                 **typeface("lowpixel", 15, 20, [1, 1, 1, 0.55]), "white-space": keyword("nowrap"), "display": keyword("block")})
    doc.bind("vote-none.display", "vote-none", "display", {"op": "select", "args": [running, "none", "block"]})
    left.append(none)
    texts = [("vote-caller", "mp.vote.caller", 0, [1, 1, 1, 0.75])]
    texts += [(f"vote-line-{line}", f"mp.vote.line{line}", 4 if line == 0 else 0, [1, 1, 1, 0.92]) for line in range(VOTE_LINES)]
    texts += [("vote-time", "mp.vote.time", 6, rgb(VALUE)), ("vote-tally", "mp.vote.tally", 2, [1, 1, 1, 0.8])]
    column = []
    for node, state, gap, tint in texts:
        column.append(label(node, PLACEHOLDER, {"position": keyword("relative"), "width": length(VOTE_LEFT_W), "height": length(18),
                            "margin-top": length(gap), "flex-shrink": number(0), **typeface("lowpixel", 15, 18, tint),
                            "white-space": keyword("nowrap"), "overflow": keyword("hidden"), "display": keyword("none")}))
        doc.bind(f"{node}.text", node, "text", {"state": state})
        shown = running
        if node.startswith("vote-line-"):
            shown = {"op": "&&", "args": [running, {"op": "<", "args": [int(node.rsplit("-", 1)[1]), {"state": "mp.vote.line_count"}]}]}
        doc.bind(f"{node}.display", node, "display", {"op": "select", "args": [shown, "block", "none"]})
    left.append(group("vote-running", {**absolute(left=0, top=VOTE_HEAD_H, width=VOTE_LEFT_W, height=18 * (VOTE_LINES + 3) + 12),
                                       "display": keyword("flex"), "flex-direction": keyword("column")}, column))
    ballots_top = VOTE_HEAD_H + 22 + 18 * (VOTE_LINES + 3)
    controls = []
    for ballot, verb, offset in (("yes", "mpVoteYes", 0), ("no", "mpVoteNo", SLOT_PITCH)):
        node = f"vote-{ballot}"
        doc.session(verb, verb)
        holder = action_plate(doc, node, f"mp.vote.{ballot}", VOTE_LEFT_W, ballots_top + offset, f"vote_{ballot}",
                              [{"op": "action", "action": verb}], primary=ballot == "yes")
        row = next(child for child in holder["children"][0]["children"] if child["id"] == f"{node}-row")
        row["children"].insert(1, vote_keycap(f"{node}-keycap", f"mp.keys.vote_{ballot}", f"mp.vote.{ballot}.available", doc))
        left.append(holder)
        controls.append(node)
    # The call being drafted.
    right = [heading("vote-heading-call", VOTE_CALL, 0, right_w * 0.5)]
    locked = label("vote-locked", VOTE_LOCKED, {**absolute(left=right_w * 0.5, top=0, width=right_w * 0.5, height=20),
                   **typeface("lowpixel", 14, 20, rgb(ERROR)), "text-align": keyword("right"), "white-space": keyword("nowrap"),
                   "display": keyword("none")})
    doc.bind("vote-locked.display", "vote-locked", "display", {"op": "select", "args": [{"state": "mp.vote.locked"}, "block", "none"]})
    right.append(locked)
    rows = [vote_row(doc, key, verb, label_key, control, right_w, value_x) for key, verb, label_key, control in VOTE_ROWS]
    # At most nine rows show at once (team modes with a buy menu).
    right.append(group("vote-rows", {**absolute(left=0, top=VOTE_HEAD_H, width=right_w, height=9 * VOTE_ROW_H),
                                     "display": keyword("flex"), "flex-direction": keyword("column")}, rows))
    doc.session("mpCallVote", "mpCallVote")
    call_top = page_height - SLOT_PITCH + 4
    right.append(action_plate(doc, "vote-call", "mp.vote.call", min(right_w, SLOT_W), call_top, "vote_call",
                              [{"op": "action", "action": "mpCallVote"}]))
    controls.append("vote-call")
    column = group("vote-call-column", absolute(left=right_x, top=0, width=right_w, height=page_height), right)
    current = {"op": "==", "args": [{"state": "card.tab"}, index]}
    for key, _verb, _label, control in VOTE_ROWS:
        enabled = {"op": "&&", "args": [current, {"state": f"mp.vote.{key}.row_allowed"}]}
        if control[0] == "choice" and key != "kick":
            enabled = {"op": "&&", "args": [enabled, {"op": ">", "args": [{"state": f"mp.vote.{key}_count"}, 0]}]}
        doc.bind(f"vote-row-{key}.enabled", f"vote-row-{key}", "enabled", enabled)
    page = page_group(doc, index, ident, width, height, [*left, column], controls)
    # Yes while the player can vote, else the map row, else Call Vote.
    focus = [{"op": "if", "condition": {"state": "mp.vote.yes.available"}, "then": [{"op": "focus", "control": "vote-yes"}],
              "else": [{"op": "if", "condition": {"op": "&&", "args": [{"state": "mp.vote.map.row_allowed"},
                                                                       {"op": ">", "args": [{"state": "mp.vote.map_count"}, 0]}]},
                        "then": [{"op": "focus", "control": "vote-row-map"}], "else": [{"op": "focus", "control": "vote-call"}]}]}]
    return page, focus


# ------------------------------------------------------ Settings and Voice

# The appearance lists' stock values, which the adapter's settings.player.set
# allowlists (FindPlayerSetting in src/ui/UserInterfaceRetained.cpp) and the
# pages read back as each setting's text.
LEVELS = ("0", "0.35", "0.7", "1")                        # #str_41207: Off, Low, Medium, High
EFFECT_COLORS = ("1 0.12 0.05", "1 0.45 0.05", "1 0.9 0.15", "0.1 0.85 0.25", "0.1 0.9 0.95", "0.3 0.45 1", "1 0.15 0.85", "1 1 1")
BRIGHT_COLORS = ("1 0.05 0.02", "1 0.4 0.02", "1 0.88 0.1", "0.05 1 0.22", "0.05 0.9 1", "0.25 0.4 1", "1 0.1 0.85", "1 1 1")
OUTLINE_WIDTHS = ("1.0", "1.5", "2.0", "3.0", "4.0", "6.0")   # #str_41214
# The stock rail color swatches' colors, in their order (RetainedRailColor in the game).
RAIL_SWATCHES = ((1, 0, 0), (1, 0.501, 0), (1, 1, 0), (0, 1, 0), (0, 1, 1), (0, 0, 1), (1, 0, 0.501))
# The appearance table's rows: label, the enemy and teammate settings, and
# their stock list (#str key, values).
APPEARANCE_ROWS = (
    ("#str_41205", "cl_player_outline_enemy", "cl_player_outline_team", "#str_41207", LEVELS),
    ("#str_41206", "cl_player_rimlight_enemy", "cl_player_rimlight_team", "#str_41207", LEVELS),
    ("#str_41208", "cl_player_visibility_enemy_color", "cl_player_visibility_team_color", "#str_41213", EFFECT_COLORS),
    ("#str_41209", "cl_player_brightskin_enemy", "cl_player_brightskin_team", "#str_41207", LEVELS),
    ("#str_41210", "cl_player_brightskin_enemy_color", "cl_player_brightskin_team_color", "#str_41213", BRIGHT_COLORS),
)
MODEL_SLOTS, MODEL_ROWS = 3, 24       # RETAINED_MODEL_SLOTS and RETAINED_MODEL_ROWS in the game
MODEL_VERBS = ("mpModelSelf", "mpModelEnemy", "mpModelTeam")
CROSSHAIRS = 20                       # the stock picker's crosshairs (mtr_crosshair1-20)
SETTINGS_PLATES = (("mpSettingsControls", "#str_200083"), ("mpSettingsGame", "#str_200084"), ("mpSettingsSystem", "#str_200085"))
CLASSIC_EDIT = "#str_231012"          # the hand-off note: the page opens in the classic menu


def heading_text(ident: str, key: str, left: float, top: float, width: float) -> dict:
    return label(ident, key, {**absolute(left=left, top=top, width=width, height=20), **typeface("lowpixel", 14, 20, HEADING),
                              "white-space": keyword("nowrap"), "overflow": keyword("hidden")})


def label_column(keys) -> float:
    """The label column that holds every language's label in two lines."""
    return math.ceil((max(wrapped_width(text) for key in keys for text in any_text(key).values()) + 29) * 1000) / 1000


def cvar_state(doc: Document, cvar: str, kind: str) -> dict:
    """A setting the page reads back from its CVar, as its text for a list."""
    key = f"cvar.{cvar}"
    initial = {"string": "", "number": 0, "boolean": False}[kind]
    doc.state[key] = {"type": kind, "initial": initial, "cvar": cvar}
    return {"state": key}


def setting_action(doc: Document, cvar: str, kind: str) -> str:
    action = f"set.{cvar}"
    doc.actions[action] = {"operation": "settings.player.set", "input": kind, "arguments": {"cvar": cvar, "value": {"input": "value"}}}
    return action


def model_entries(doc: Document, slot: int) -> tuple[list, dict]:
    """A model list's rows, as the game publishes them (mp.model<slot>.<row>)."""
    for row in range(MODEL_ROWS):
        doc.state[f"mp.model{slot}.{row}"] = {"type": "string", "initial": ""}
    doc.state.update({f"mp.model{slot}": {"type": "number", "initial": -1}, f"mp.model{slot}_count": {"type": "number", "initial": 0},
                      f"mp.model{slot}.row_shown": {"type": "boolean", "initial": False}})
    doc.actions[MODEL_VERBS[slot]] = {"operation": "session.menuValue", "input": "number",
                                      "arguments": {"command": MODEL_VERBS[slot], "value": {"input": "value"}}}
    return [({"state": f"mp.model{slot}.{row}"}, row) for row in range(MODEL_ROWS)], {"state": f"mp.model{slot}_count"}


def row_label(ident: str, key: str, left: float, top: float, width: float) -> dict:
    """A setting row's two-line label, centered in its row."""
    caption = label(f"{ident}-label", key, {"position": keyword("relative"), "display": keyword("block"), "min-width": length(0),
                    **row_face(True), "overflow": keyword("hidden")})
    return group(f"{ident}-head", {**absolute(left=left, top=top, width=width, height=VOTE_ROW_H), "display": keyword("flex"),
                                   "flex-direction": keyword("row"), "align-items": keyword("center")}, [caption])


NAME_BYTES, CLAN_BYTES = 23, 12   # the stock fields' maxchars, RETAINED_MP_NAME_BYTES and _CLAN_BYTES in the session


def rail_row(doc: Document, ident: str, width: float, value_x: float) -> tuple[dict, list]:
    """The rail color: the stock swatches, each a button that asks the game
    for its color; the one the player's tint matches stands taller."""
    doc.state.update({"card.rail": {"type": "number", "initial": -1}, "mp.settings.rail": {"type": "number", "initial": -1}})
    doc.session("mpRail", "mpRail")
    plate = vector(f"{ident}-plate", FULL, [
        path("plate", [(6, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}), (0, {"fraction": 1, "dp": -1}), (0, 7)],
             fill=solid(rgb(OLIVE, 0.12)), stroke=stroke(solid(rgb(OLIVE, 0.32)), 1)),
    ])
    caption = row_label(ident, "#str_200981", 21, 0, value_x - 29)
    swatches, controls = [], []
    for row, tint in enumerate(RAIL_SWATCHES):
        node = f"{ident}-{row}"
        chosen = {"op": "==", "args": [{"state": "mp.settings.rail"}, row]}
        ring = vector(f"{node}-ring", {**absolute(left=0, top=0, width=18, height=22), "opacity": number(0)}, [
            path("ring", [(0.75, 0.75), (17.25, 0.75), (17.25, 21.25), (0.75, 21.25)], stroke=stroke(solid(rgb(ORANGE)), 1.5))])
        chip = vector(f"{node}-chip", {**absolute(left=3, top=6, width=12, height=10), "transform": transform()}, [
            path("chip", [(0, 0), (12, 0), (12, 10), (0, 10)], fill=solid([*tint, 1]))])
        doc.bind(f"{node}-chip.transform", f"{node}-chip", "transform",
                 [0, {"op": "select", "args": [chosen, -3, 0]}, 1, {"op": "select", "args": [chosen, 1.6, 1]}, 0])
        ids = doc.states(node, {
            "default": [(f"{node}-ring", "opacity", number(0))], "hover": [(f"{node}-ring", "opacity", number(0.6))],
            "focus": [(f"{node}-ring", "opacity", number(1))], "pressed": [(f"{node}-ring", "opacity", number(1))],
            "disabled": [(f"{node}-ring", "opacity", number(0))],
        })
        event = f"rail_{row}"
        doc.events[event] = [{"op": "setState", "values": {"card.rail": row}}, {"op": "action", "action": "mpRail"}]
        swatches.append(group(node, absolute(left=value_x + 20 * row, top=2, width=18, height=22), [ring, chip],
                              control={"role": "button", "label": "#str_200981", "event": event, "states": ids}))
        controls.append(node)
    return group(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(width), "height": length(VOTE_ROW_H),
                         "flex-shrink": number(0)}, [plate, caption, *swatches]), controls


def settings_plates(doc: Document, ident: str, left: float, top: float, width: float) -> tuple[list, list]:
    """Controls, Game Options and System: each leaves for the main menu's
    page (decision D13)."""
    plates, controls = [], []
    for index, (verb, key) in enumerate(SETTINGS_PLATES):
        doc.session(verb, verb)
        node = f"{ident}-{verb}"
        plates.append(in_game_plate(doc, node, key, left, top + 50 * index, width=width, action=verb))
        controls.append(node)
    return plates, controls


def player_rows(doc: Document, index: int, width: float, value_x: float, last: tuple) -> tuple[list, list]:
    """The player's rows: name and clan as text fields (the stock fields'
    23 and 12 characters, the clan may be empty) holding the settings as the
    game publishes them, colour codes and all, and Enter setting them
    (mpSettingsName, mpSettingsClan); the model, the rail color swatches,
    then `last` (the handicap on Escape, the crosshair on Welcome)."""
    from retained_create_server import text_row as field_row
    # The plain text the game publishes beside the fields' own.
    doc.state.update({"mp.settings.name": {"type": "string", "initial": ""}, "mp.settings.clan": {"type": "string", "initial": ""}})
    rows = []
    for ident, key, state, verb, size, empty in (("settings-name", "#str_200219", "mp.settings.name_edit", "mpSettingsName", NAME_BYTES, False),
                                                  ("settings-clan", "#str_200220", "mp.settings.clan_edit", "mpSettingsClan", CLAN_BYTES, True)):
        doc.state[state] = {"type": "string", "initial": ""}
        doc.session(verb, verb)
        event = f"{ident.replace('-', '_')}_entered"
        doc.events[event] = [{"op": "action", "action": verb}]
        rows.append(field_row(doc, ident, key, width, value_x, state, event, size, VOTE_ROW_H, allow_empty=empty))
    entries, count = model_entries(doc, 0)
    rows.append(value_row(doc, "settings-model", "#str_200221", "choice", width, value_x, value={"state": "mp.model0"},
                          action=MODEL_VERBS[0], entries=entries, count=count, wrap=True))
    rail, swatches = rail_row(doc, "settings-rail", width, value_x)
    rows.append(rail)
    rows.append(last[0])
    return rows, ["settings-name", "settings-clan", "settings-model", *swatches, last[1]]


def check_settings_labels(width: float, value_x: float, right_w: float, cell_w: float) -> None:
    """Fail generation when a label, a list's value or a plate's label would
    not fit in any language."""
    over = {}
    for key in ("#str_200219", "#str_200220", "#str_200221", "#str_200981", "#str_223001", "#str_200307"):
        for language, text in any_text(key).items():
            if wrapped_width(text) > value_x - 29:
                over[(key, language)] = round(wrapped_width(text))
    for key, items in (("#str_41207", 4), ("#str_41213", 8), ("#str_41214", 6)):
        for language, text in any_text(key).items():
            for item in text.split(";")[:items]:
                if b.text_width("lowpixel", item, 14) > cell_w - 34:
                    over[(key, language, item)] = round(b.text_width("lowpixel", item, 14))
    for _verb, key in SETTINGS_PLATES:
        for language, text in any_text(key).items():
            if b.text_width("marine", text, 20) > width - 30:
                over[(key, language)] = round(b.text_width("marine", text, 20))
    if over:
        raise SystemExit(f"Settings page labels past their room: {over}")


def settings_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, list]:
    """The Settings page (section 14.18): the player's name, clan, model, rail
    color and handicap, then Controls, Game Options and System, which leave
    for the main menu's pages; beside them the appearance of opponents and
    teammates: the model forced on them, their outline, rim light, effect
    color, brightskin and its color, and the outline's width, the teammates'
    column only in team modes."""
    page_width = width - 2 * INSET
    page_height = height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16
    left_w = 380.0
    right_x = left_w + 24
    right_w = page_width - right_x
    value_x = label_column(("#str_200219", "#str_200220", "#str_200221", "#str_200981", "#str_223001"))
    table_x = label_column(("#str_41204", *(row[0] for row in APPEARANCE_ROWS), "#str_41211"))
    cell_w = round((right_w - table_x - 6) / 2, 3)
    check_settings_labels(left_w, value_x, right_w, cell_w)
    handicap = value_row(doc, "settings-handicap", "#str_223001", "slider", left_w, value_x, value=cvar_state(doc, "ui_handicap", "number"),
                         action=setting_action(doc, "ui_handicap", "number"), slider=(1, 100, 1, 0), wrap=True)
    rows, controls = player_rows(doc, index, left_w, value_x, (handicap, "settings-handicap"))
    left = [heading_text("settings-heading-player", "#str_200255", 0, 0, left_w),
            group("settings-player", {**absolute(left=0, top=VOTE_HEAD_H, width=left_w, height=len(rows) * VOTE_ROW_H),
                                      "display": keyword("flex"), "flex-direction": keyword("column")}, rows)]
    plates, plate_controls = settings_plates(doc, "settings", 0, VOTE_HEAD_H + len(rows) * VOTE_ROW_H + 12, left_w)
    left += plates
    controls += plate_controls
    # The appearance table: a row's label, then the opponents' and the
    # teammates' cells, the teammates' only in team modes.
    team_mode = {"state": "mp.model2.row_shown"}
    right = [heading_text("settings-heading-opponents", "#str_42037", right_x + table_x, 0, cell_w),
             heading_text("settings-heading-teammates", "#str_42038", right_x + table_x + cell_w + 6, 0, cell_w)]
    doc.bind("settings-heading-teammates.display", "settings-heading-teammates", "display",
             {"op": "select", "args": [team_mode, "block", "none"]})
    right[1]["properties"]["display"] = keyword("none")
    table_rows = [("#str_41204", None)] + [(row[0], row) for row in APPEARANCE_ROWS]
    for row_index, (label_key, row) in enumerate(table_rows):
        top = VOTE_HEAD_H + row_index * VOTE_ROW_H
        right.append(row_label(f"settings-row-{row_index}", label_key, right_x + 21, top, table_x - 29))
        for side in range(2):
            node = f"settings-row-{row_index}-{'opponents' if side == 0 else 'teammates'}"
            if row is None:
                entries, count = model_entries(doc, 1 + side)
                cell = value_row(doc, node, None, "choice", cell_w, 8, value={"state": f"mp.model{1 + side}"}, action=MODEL_VERBS[1 + side],
                                 entries=entries, count=count, accessible=label_key, shown=team_mode if side == 1 else None)
            else:
                cvar = row[1 + side]
                entries = [((row[3], option), value) for option, value in enumerate(row[4])]
                cell = value_row(doc, node, None, "choice", cell_w, 8, value=cvar_state(doc, cvar, "string"),
                                 action=setting_action(doc, cvar, "string"), entries=entries, accessible=label_key,
                                 shown=team_mode if side == 1 else None)
            cell["properties"].update(absolute(left=right_x + table_x + side * (cell_w + 6), top=top))
            right.append(cell)
            controls.append(node)
    width_top = VOTE_HEAD_H + len(table_rows) * VOTE_ROW_H + 8
    outline_width = value_row(doc, "settings-outline-width", "#str_41211", "choice", right_w, table_x,
                              value=cvar_state(doc, "cl_player_outline_width", "string"),
                              action=setting_action(doc, "cl_player_outline_width", "string"),
                              entries=[(("#str_41214", option), value) for option, value in enumerate(OUTLINE_WIDTHS)], wrap=True)
    outline_width["properties"].update(absolute(left=right_x, top=width_top))
    right.append(outline_width)
    controls.append("settings-outline-width")
    note = label("settings-team-note", "#str_41212", {**absolute(left=right_x, top=width_top + VOTE_ROW_H + 12, width=right_w, height=36),
                 **typeface("lowpixel", 14, 18, [1, 1, 1, 0.6]), "white-space": keyword("normal"), "overflow": keyword("hidden"),
                 "display": keyword("block")})
    doc.bind("settings-team-note.display", "settings-team-note", "display", {"op": "select", "args": [team_mode, "none", "block"]})
    right.append(note)
    page = page_group(doc, index, ident, width, height, [*left, *right], controls)
    return page, "settings-name"


def voice_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, str]:
    """Explicit microphone consent, push-to-talk, receive mix and a local test.
    The status and level come from the running backend, including failures."""
    page_width = width - 2 * INSET
    left_w = 420.0
    right_x = left_w + 24
    right_w = page_width - right_x
    value_x = label_column(("#str_231301", "#str_201017", "#str_201018", "#str_201019", "#str_201030", "#str_201020"))
    consent = cvar_state(doc, "s_voiceChatCapture", "boolean")
    rows = [
        value_row(doc, "voice-capture", "#str_231301", "toggle", left_w, value_x, value=consent,
                  action=setting_action(doc, "s_voiceChatCapture", "boolean"), wrap=True),
        value_row(doc, "voice-send", "#str_201017", "toggle", left_w, value_x, value=cvar_state(doc, "s_voiceChatSend", "boolean"),
                  action=setting_action(doc, "s_voiceChatSend", "boolean"), wrap=True),
        value_row(doc, "voice-receive", "#str_201018", "toggle", left_w, value_x, value=cvar_state(doc, "s_voiceChatReceive", "boolean"),
                  action=setting_action(doc, "s_voiceChatReceive", "boolean"), wrap=True),
        value_row(doc, "voice-volume", "#str_201019", "slider", left_w, value_x, value=cvar_state(doc, "s_voiceVolume", "number"),
                  action=setting_action(doc, "s_voiceVolume", "number"), slider=(0, 1, 0.05, 2), wrap=True),
        value_row(doc, "voice-mic", "#str_201030", "slider", left_w, value_x, value=cvar_state(doc, "s_micInputLevel", "number"),
                  action=setting_action(doc, "s_micInputLevel", "number"), slider=(0, 10, 0.5, 1), wrap=True),
        value_row(doc, "voice-echo", "#str_201020", "toggle", left_w, value_x, value=cvar_state(doc, "s_voiceChatEcho", "boolean"),
                  action=setting_action(doc, "s_voiceChatEcho", "boolean"), wrap=True),
    ]
    left = [heading_text("voice-heading", "#str_231006", 0, 0, left_w),
            group("voice-rows", {**absolute(left=0, top=VOTE_HEAD_H, width=left_w, height=len(rows) * VOTE_ROW_H),
                                 "display": keyword("flex"), "flex-direction": keyword("column")}, rows)]
    controls = ["voice-capture", "voice-send", "voice-receive", "voice-volume", "voice-mic", "voice-echo"]
    # The push-to-talk key, as the session names it, and Controls to rebind it.
    doc.state.update({"mp.keys.voice_chat": {"type": "string", "initial": ""}, "mp.keys.voice_chat_bound": {"type": "boolean", "initial": False}})
    bound = {"state": "mp.keys.voice_chat_bound"}
    key = vote_keycap("voice-key", "mp.keys.voice_chat", "mp.keys.voice_chat_bound", doc)
    key["properties"].update(absolute(left=right_x, top=VOTE_HEAD_H + 2))
    key["properties"]["margin-left"] = length(0)
    unbound = label("voice-unbound", "#str_231055", {**absolute(left=right_x, top=VOTE_HEAD_H, width=right_w, height=VOTE_ROW_H),
                    **typeface("lowpixel", 15, VOTE_ROW_H, [1, 1, 1, 0.6]), "white-space": keyword("nowrap"), "display": keyword("block")})
    doc.bind("voice-unbound.display", "voice-unbound", "display", {"op": "select", "args": [bound, "none", "block"]})
    doc.session("mpSettingsControls", "mpSettingsControls")
    rebind = in_game_plate(doc, "voice-controls", "#str_200083", right_x, VOTE_HEAD_H + VOTE_ROW_H + 6, width=min(right_w, 320),
                           action="mpSettingsControls")
    controls.append("voice-controls")
    # A real peak meter, with no decorative signal while capture is off.
    meter_top = VOTE_HEAD_H + VOTE_ROW_H + 64
    meter_label = label("voice-meter-label", "#str_201027", {**absolute(left=right_x, top=meter_top, width=right_w, height=20),
                        **typeface("lowpixel", 14, 20, HEADING), "white-space": keyword("nowrap")})
    meter = vector("voice-meter", {**absolute(left=right_x, top=meter_top + 22, width=min(right_w, 320), height=10), "opacity": number(0.38)}, [
        path("trough", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, 10), (0, 10)], stroke=stroke(solid(rgb(OLIVE, 0.6)), 1))])
    fill = vector("voice-meter-fill", {**absolute(left=right_x, top=meter_top + 22, width=0, height=10)}, [
        path("level", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, 10), (0, 10)], fill=solid(rgb(ORANGE)))])
    doc.bind("voice-meter-fill.width", "voice-meter-fill", "width",
             {"op": "*", "args": [cvar_state(doc, "s_voiceChatLevel", "number"), min(right_w, 320)]})
    test = value_row(doc, "voice-test", "#str_231307", "toggle", right_w, label_column(("#str_231307",)),
                     value=cvar_state(doc, "s_voiceChatTest", "boolean"),
                     action=setting_action(doc, "s_voiceChatTest", "boolean"), allowed=consent, wrap=True)
    test["properties"].update(absolute(left=right_x, top=meter_top + 44))
    doc.bind("voice-test.enabled", "voice-test", "enabled",
             {"op": "&&", "args": [consent, {"op": "==", "args": [{"state": "card.tab"}, index]}]})
    status = label("voice-status", "#str_231306", {**absolute(left=right_x, top=meter_top + 82, width=right_w, height=70),
                   **typeface("lowpixel", 14, 18, [1, 1, 1, 0.85]), "white-space": keyword("normal")})
    state = cvar_state(doc, "s_voiceChatStatus", "number")
    message = "#str_231306"
    for value, status_key in ((1, "#str_231302"), (2, "#str_231303"), (3, "#str_231304"), (4, "#str_231305")):
        message = {"op": "select", "args": [{"op": "==", "args": [state, value]}, status_key, message]}
    doc.bind("voice-status.text", "voice-status", "text", message)
    right = [heading_text("voice-heading-talk", "#str_231054", right_x, 0, right_w), key, unbound, rebind, meter_label, meter, fill, test, status]
    return page_group(doc, index, ident, width, height, [*left, *right], controls), "voice-capture"


def welcome_settings_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, str]:
    """The Welcome card's Settings page (section 14.18): name and clan, the
    model, the rail color swatches and the crosshair, a slider through the
    stock picker's crosshairs (0 for each weapon's own) with the chosen one
    beside it; then Controls, Game Options and System, which stand for All
    settings."""
    page_width = width - 2 * INSET
    left_w = 400.0
    value_x = label_column(("#str_200219", "#str_200220", "#str_200221", "#str_200981", "#str_200307"))
    check_settings_labels(left_w, value_x, page_width - left_w, 200)
    doc.state.update({"mp.crosshair": {"type": "number", "initial": 0}, "mp.crosshair_image": {"type": "string", "initial": ""}})
    doc.actions["mpCrosshair"] = {"operation": "session.menuValue", "input": "number",
                                  "arguments": {"command": "mpCrosshair", "value": {"input": "value"}}}
    crosshair = value_row(doc, "settings-crosshair", "#str_200307", "slider", left_w, value_x, value={"state": "mp.crosshair"},
                          action="mpCrosshair", slider=(0, CROSSHAIRS, 1, 0), wrap=True)
    rows, controls = player_rows(doc, index, left_w, value_x, (crosshair, "settings-crosshair"))
    preview_top = VOTE_HEAD_H + 4 * VOTE_ROW_H
    custom = {"op": ">", "args": [{"state": "mp.crosshair"}, 0]}
    preview = picture("settings-crosshair-image", "", {**absolute(left=left_w + 16, top=preview_top - 4, width=32, height=32),
                      "display": keyword("none")}, fit="contain")
    doc.bind("settings-crosshair-image.image", "settings-crosshair-image", "image", {"state": "mp.crosshair_image"})
    doc.bind("settings-crosshair-image.display", "settings-crosshair-image", "display", {"op": "select", "args": [custom, "block", "none"]})
    by_weapon = label("settings-crosshair-weapon", "#str_231058", {**absolute(left=left_w + 16, top=preview_top, width=page_width - left_w - 16,
                      height=VOTE_ROW_H), **typeface("lowpixel", 15, VOTE_ROW_H, [1, 1, 1, 0.6]), "white-space": keyword("nowrap"),
                      "display": keyword("block")})
    doc.bind("settings-crosshair-weapon.display", "settings-crosshair-weapon", "display", {"op": "select", "args": [custom, "none", "block"]})
    plates, plate_controls = settings_plates(doc, "settings", 0, VOTE_HEAD_H + len(rows) * VOTE_ROW_H + 12, left_w)
    children = [heading_text("settings-heading-player", "#str_200255", 0, 0, left_w),
                group("settings-player", {**absolute(left=0, top=VOTE_HEAD_H, width=left_w, height=len(rows) * VOTE_ROW_H),
                                          "display": keyword("flex"), "flex-direction": keyword("column")}, rows),
                preview, by_weapon, *plates]
    return page_group(doc, index, ident, width, height, children, controls + plate_controls), "settings-name"


# ---------------------------------------------------------------- Match page

# Match Control's six sections, the stock page's own names, form the page's
# inner strip (decision D3): the triggers page through them, Left and Right
# move along the strip and accept chooses.
MATCH_SECTIONS = ("status", "teams", "proposals", "rules", "series", "evidence")
MATCH_SECTION_KEYS = ("#str_41701", "#str_41702", "#str_41703", "#str_41704", "#str_41705", "#str_41706")
MATCH_BUILT = MATCH_SECTIONS            # every section is built
# The card's Match Control actions by index (card.match_op): the session's
# RETAINED_MP_MATCH_TOKENS lists the same stock tokens in the same order.
MATCH_TOKENS = ("refresh", "ready_toggle", "team_ready_toggle", "arm_force_ready", "timeout", "tech_pause", "resume",
                "arm_forfeit", "arm_abort", "referee_logout", "action_side_a", "action_side_b",
                "follow_prev", "follow_next", "follow_free", "confirm", "cancel_confirm",
                "team_join_marine", "team_join_strogg", "team_spectate", "queue_join", "queue_defer", "queue_leave",
                "roster_accept", "roster_leave", "roster_invite", "arm_roster_remove", "arm_roster_substitute", "role_assign",
                "team_lock_toggle", "broadcaster_set", "arm_participant_remove", "series_contestant_bind",
                "proposal_create", "proposal_yes", "proposal_no", "proposal_abstain", "proposal_cancel",
                "rules_select_profile", "rules_stage_field", "arm_rules_commit", "rules_discard",
                "series_stage", "arm_series_start", "arm_series_cancel", "arm_series_advance",
                "arm_veto_ban", "arm_veto_pick", "arm_veto_decider", "arm_veto_side_marine", "arm_veto_side_strogg")
MATCH_STATUS_LINES = 6                  # RETAINED_MATCH_STATUS_LINES in the game
# The Status section's actions by the game's operation name (mp.match.op.*),
# with the token each sends, its accessible name and, for those Match Control
# confirms first, the stock confirmation's text.
MATCH_STATUS_ACTIONS = (("ready", "ready_toggle", "#str_41713", None),
                        ("team_ready", "team_ready_toggle", "#str_41715", None),
                        ("timeout", "timeout", "#str_41716", None),
                        ("tech_pause", "tech_pause", "#str_41717", None),
                        ("resume", "resume", "#str_41718", None),
                        ("force_ready", "arm_force_ready", "#str_41719", "#str_41900"),
                        ("forfeit", "arm_forfeit", "#str_41783", "#str_41798"),
                        ("abort", "arm_abort", "#str_41784", "#str_41799"))
# The Teams section's: the player's own team and queue place first, then the
# roster actions on the row chosen, as the stock page orders them.
MATCH_TEAM_ACTIONS = (("join_marine", "team_join_marine", "#str_41723", None),
                      ("join_strogg", "team_join_strogg", "#str_41724", None),
                      ("spectate", "team_spectate", "#str_41725", None),
                      ("queue_join", "queue_join", "#str_41726", None),
                      ("queue_defer", "queue_defer", "#str_41728", None),
                      ("queue_leave", "queue_leave", "#str_41727", None),
                      ("roster_accept", "roster_accept", "#str_41729", None),
                      ("roster_leave", "roster_leave", "#str_42343", None),
                      ("roster_invite", "roster_invite", "#str_41730", None),
                      ("roster_remove", "arm_roster_remove", "#str_41731", "#str_41902"),
                      ("roster_substitute", "arm_roster_substitute", "#str_41732", "#str_41903"),
                      ("role_assign", "role_assign", "#str_41733", None),
                      ("team_lock", "team_lock_toggle", "#str_41734", None),
                      ("broadcaster", "broadcaster_set", "#str_41795", None),
                      ("participant_remove", "arm_participant_remove", "#str_41908", "#str_42349"),
                      ("contestant_bind", "series_contestant_bind", "#str_41909", None))
# The Proposals section's: Create proposal, then the ballots and the
# cancellation; and the Rules section's, Commit rules asking first.
MATCH_PROPOSAL_ACTIONS = (("proposal_create", "proposal_create", "#str_41740", None),
                          ("proposal_yes", "proposal_yes", "#str_41741", None),
                          ("proposal_no", "proposal_no", "#str_41742", None),
                          ("proposal_abstain", "proposal_abstain", "#str_41743", None),
                          ("proposal_cancel", "proposal_cancel", "#str_41744", None))
MATCH_RULE_ACTIONS = (("rules_select_profile", "rules_select_profile", "#str_41747", None),
                      ("rules_stage", "rules_stage_field", "#str_41748", None),
                      ("rules_commit", "arm_rules_commit", "#str_41749", "#str_41901"),
                      ("rules_discard", "rules_discard", "#str_41750", None))
# The Series section's: staging, starting, cancelling and advancing the
# series, then the veto choices; all but staging ask first.
MATCH_SERIES_ACTIONS = (("series_stage", "series_stage", "#str_41754", None),
                        ("series_start", "arm_series_start", "#str_41755", "#str_41905"),
                        ("series_cancel", "arm_series_cancel", "#str_41756", "#str_41904"),
                        ("series_advance", "arm_series_advance", "#str_41757", "#str_41906"),
                        ("veto_ban", "arm_veto_ban", "#str_41758", "#str_41907"),
                        ("veto_pick", "arm_veto_pick", "#str_41759", "#str_41907"),
                        ("veto_decider", "arm_veto_decider", "#str_41760", "#str_41907"),
                        ("veto_side_marine", "arm_veto_side_marine", "#str_41761", "#str_41907"),
                        ("veto_side_strogg", "arm_veto_side_strogg", "#str_41762", "#str_41907"))
MATCH_ACTIONS = MATCH_STATUS_ACTIONS + MATCH_TEAM_ACTIONS + MATCH_PROPOSAL_ACTIONS + MATCH_RULE_ACTIONS + MATCH_SERIES_ACTIONS
# Every label a section's action row can show: Set ready reads Set not ready
# once ready, Lock team Unlock team once locked, and Grant broadcaster Revoke
# broadcaster for a broadcaster.
MATCH_STATUS_LABELS = ("#str_41713", "#str_41714", "#str_41715", "#str_41716", "#str_41717", "#str_41718", "#str_41719",
                       "#str_41783", "#str_41784")
MATCH_TEAM_LABELS = tuple(key for _name, _token, key, _body in MATCH_TEAM_ACTIONS) + ("#str_41735", "#str_41796")
MATCH_PROPOSAL_LABELS = tuple(key for _name, _token, key, _body in MATCH_PROPOSAL_ACTIONS)
MATCH_RULE_LABELS = tuple(key for _name, _token, key, _body in MATCH_RULE_ACTIONS)
MATCH_SERIES_LABELS = tuple(key for _name, _token, key, _body in MATCH_SERIES_ACTIONS)
# The Match Control lists the card can select a row of, by the card's index
# (card.match_list): the session's RETAINED_MP_MATCH_LISTS in the same order.
MATCH_LISTS = ("team", "replacement", "proposal", "profile", "rule", "series_map")
# Each list's rows: the game's RETAINED_MATCH_*_ROWS, read from its header,
# which says which lists hold every row Match Control can and which end with
# the way to the stock page past a full server's rows.
MATCH_ROW_COUNTS = {name: int(value) for name, value in re.findall(
    r"static const int RETAINED_MATCH_(\w+)_ROWS = (\d+);",
    (ROOT / "src" / "mpgame" / "MultiplayerGame.h").read_text(encoding="utf-8"))}
MATCH_TEAM_ROWS, MATCH_REPLACEMENT_ROWS = MATCH_ROW_COUNTS["TEAM"], MATCH_ROW_COUNTS["REPLACEMENT"]
MATCH_LIST_ROW_H = 22.0
MATCH_TEAMS_W = 450.0                   # the Teams section's lists; the role and the actions take the rest
MATCH_TEAMS_HEAD, MATCH_REPLACEMENTS_HEAD, MATCH_ROLE = "#str_41736", "#str_41785", "#str_41787"
# The roles an invitation or an assignment gives: the protocol's roster
# roles, 1 to 4 (the stock choice's values).
MATCH_ROLES = (("#str_42470", 1), ("#str_42471", 2), ("#str_42472", 3), ("#str_42473", 4))
# Proposals: the proposal a ballot or a cancellation goes to (the session's
# RETAINED_MP_MATCH_SCOPES by index), under the stock page's own names for
# the two, and the ballot each proposal the player may make takes.
MATCH_PROPOSAL_ROWS = MATCH_ROW_COUNTS["PROPOSAL"]
MATCH_PROPOSALS_W = 430.0               # the running proposals and the ballots; the proposals to make take the rest
MATCH_BALLOT_TARGET = "#str_41793"
MATCH_SCOPES = (("#str_41738", 0), ("#str_41739", 1))
MATCH_PROPOSAL_SCOPE_KEYS = ("#str_42490", "#str_42491")
# Rules: the profiles and the rule fields (RETAINED_MATCH_PROFILE_ROWS and
# _RULE_ROWS in the game), each field's type, and the value to stage, a whole
# number up to the largest a rule takes (the session's
# RETAINED_MP_MATCH_RULE_VALUE_MAX).
MATCH_PROFILE_ROWS, MATCH_RULE_ROWS = MATCH_ROW_COUNTS["PROFILE"], MATCH_ROW_COUNTS["RULE"]
MATCH_RULES_W = 430.0                   # the rules' lists; the value, the staged changes and the actions take the rest
MATCH_PROFILE_HEAD, MATCH_RULES_HEAD, MATCH_VALUE, MATCH_STAGED_HEAD = "#str_41788", "#str_41745", "#str_41789", "#str_41746"
MATCH_RULE_VALUE_MAX, MATCH_RULE_VALUE_MESSAGE = 10000, "#str_231059"
# Series and Evidence: the map pool, the veto and map history and the recent
# evidence (RETAINED_MATCH_SERIES_MAP_ROWS, _HISTORY_ROWS and _EVIDENCE_ROWS
# in the game; the last two only to read), the format a staged series takes
# (the session's RETAINED_MP_MATCH_SERIES_PROFILES by index, under the
# projection's own names), and the texts the rows' columns hold.
MATCH_SERIES_MAP_ROWS, MATCH_HISTORY_ROWS, MATCH_EVIDENCE_ROWS = (MATCH_ROW_COUNTS["SERIES_MAP"], MATCH_ROW_COUNTS["HISTORY"],
                                                                 MATCH_ROW_COUNTS["EVIDENCE"])
MATCH_SERIES_W = 450.0                  # the Series and Evidence lists; the format and the actions take the rest
MATCH_MAP_POOL_HEAD, MATCH_HISTORY_HEAD, MATCH_EVIDENCE_HEAD, MATCH_RECENT_HEAD = "#str_41752", "#str_41753", "#str_41763", "#str_41766"
MATCH_SERIES_FORMAT, MATCH_REFRESH = "#str_41788", "#str_41771"
MATCH_SERIES_PROFILES = (("#str_42710", 0), ("#str_42711", 1), ("#str_42712", 2))
MATCH_SIDE_KEYS = ("#str_42630", "#str_42631", "#str_42632", "#str_42633")
MATCH_DISPOSITION_KEYS = ("#str_42530", "#str_42531", "#str_42532")
MATCH_HISTORY_KEYS = ("#str_42520", "#str_42521", "#str_42522", "#str_42523", "#str_42540", "#str_42541", "#str_42542", "#str_42543")
MATCH_CONFIRM_TITLE = "#str_41797"
MATCH_WAITING, MATCH_REFEREE, MATCH_SIGN_IN, MATCH_SIGN_OUT = "#str_41774", "#str_41779", "#str_41781", "#str_41782"
REFEREE_BYTES = 64                 # RETAINED_MP_CREDENTIAL_BYTES in the session
MATCH_FOLLOW = (("previous", "follow_prev", "#str_42885"), ("next", "follow_next", "#str_42886"), ("free", "follow_free", "#str_42887"))
MATCH_STRIP_H, MATCH_STRIP_GAP = 26.0, 10.0
MATCH_LEFT_W = 320.0                    # the status column; the actions take the rest
MATCH_ROW_PITCH = 32.0
# Every reason an action row can show: the protocol's reasons (the projection
# names each by its own key or by the same key through the server's
# localization id), the wait for a view and the unknown value.
MATCH_REASON_KEYS = ("#str_41774", "#str_42301") + tuple(f"#str_{key}" for key in range(42360, 42392))


def match_op(token: str) -> list:
    """The steps that ask the game for a Match Control action by its token."""
    return [{"op": "setState", "values": {"card.match_op": MATCH_TOKENS.index(token)}}, {"op": "action", "action": "mpMatch"}]


def match_modal_id(name: str) -> str:
    return "match" + "".join(part.title() for part in name.split("_")) + "Modal"


def match_modals(doc: Document) -> list:
    """The stock confirmation for each action Match Control confirms first.
    The action arms it in the game and shows the modal; Yes confirms it and
    No, or back, cancels it, each hiding the modal again."""
    modals = []
    for name, _token, _accessible, body in MATCH_ACTIONS:
        if body is None:
            continue
        modal = match_modal_id(name)
        doc.events[f"{modal}Yes"] = [*match_op("confirm"), {"op": "call", "event": f"{modal}Hide"}]
        doc.events[f"{modal}No"] = [*match_op("cancel_confirm"), {"op": "call", "event": f"{modal}Hide"}]
        modals.append(confirmation(doc, modal, MATCH_CONFIRM_TITLE, body, None, yes_event=f"{modal}Yes", no_event=f"{modal}No"))
    return modals


def check_match_labels(width: float, keys: tuple, lines: int = 2) -> float:
    """Where the action rows' reason column starts: after a label column that
    holds every label in `keys` in two lines at 14 dp, in every language, and
    its padlock. The reason column must hold every reason a row can show in
    `lines` lines, in every language."""
    need = max(wrapped_width(text) for key in keys for text in table_text(key).values())
    value_x = math.ceil(21 + need + 20 + 8)
    reason = max(wrapped_width(text, 14, lines) for key in MATCH_REASON_KEYS for text in table_text(key).values())
    if width - value_x - 8 < reason:
        raise SystemExit(f"the Match actions' labels need {need:.0f} dp, leaving their reasons {width - value_x - 8:.0f} dp of the "
                         f"{reason:.0f} dp the longest needs in {lines} lines")
    return value_x


def match_action_steps(name: str, token: str, body: str | None) -> list:
    """What choosing an available action does: ask the game for its token
    and, where Match Control confirms first, show the stock confirmation."""
    return match_op(token) + ([{"op": "call", "event": f"{match_modal_id(name)}Show"}] if body else [])


def match_action_row(doc: Document, name: str, accessible: str, width: float, value_x: float, top: float | None,
                     steps: list, lines: int = 2) -> tuple[dict, str]:
    """A Match Control action as a row (section 7): the game publishes its
    label, availability and reason (mp.match.op.<name>.*). The label and, on
    an unavailable action, the padlock fill the label column and the reason
    the rest in the error color; choosing an unavailable action shakes the row
    and pulses its reason, and asks nothing. An available one runs `steps`,
    and the game checks the action again. Without `top` the row flows in its
    column at the rows' pitch, and a hidden row takes no room. A row whose
    reason column is narrow takes `lines` lines of reason."""
    key = f"mp.match.op.{name}"
    height = VOTE_ROW_H if lines <= 2 else lines * LABEL_LINE + 4
    for field, kind in (("shown", "boolean"), ("available", "boolean"), ("label", "string"), ("reason", "string"), ("detail", "string")):
        doc.state[f"{key}.{field}"] = {"type": kind, "initial": False if kind == "boolean" else ""}
    ident, event = f"match-{name.replace('_', '-')}", f"match_{name}"
    plate = vector(f"{ident}-plate", FULL, [
        path("plate", [(6, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}), (0, {"fraction": 1, "dp": -1}), (0, 7)],
             fill=solid(rgb(OLIVE, 0.12)), stroke=stroke(solid(rgb(OLIVE, 0.32)), 1)),
    ])
    wash = vector(f"{ident}-wash", {**FULL, "opacity": number(0)}, [
        path("wash", [(6, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}), (0, {"fraction": 1, "dp": -1}), (0, 7)],
             fill=linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(OLIVE, 0.45)), (1, rgb(OLIVE, 0.1))])),
    ])
    rail = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=4, height=height), "opacity": number(0)}, [
        path("rail", [(0, 7), (6, 1), (6, height - 1), (0, height - 1)], fill=solid(rgb(ORANGE)))])
    caption = label(f"{ident}-label", PLACEHOLDER, {"position": keyword("relative"), "display": keyword("block"), "flex-shrink": number(1),
                    "min-width": length(0), **row_face(True), "overflow": keyword("hidden")})
    doc.bind(f"{ident}-label.text", f"{ident}-label", "text", {"state": f"{key}.label"})
    lock = padlock(f"{ident}-lock", rgb(ERROR))
    unavailable = {"op": "!", "args": [{"state": f"{key}.available"}]}
    doc.bind(f"{ident}-lock.display", f"{ident}-lock", "display", {"op": "select", "args": [unavailable, "block", "none"]})
    head = group(f"{ident}-head", {**absolute(left=21, top=0, width=value_x - 29, height=height), "display": keyword("flex"),
                                   "flex-direction": keyword("row"), "align-items": keyword("center")}, [caption, lock])
    reason = label(f"{ident}-reason", PLACEHOLDER, {**absolute(left=value_x, top=(height - lines * LABEL_LINE) / 2 if lines > 2 else 0,
                                                              width=width - value_x - 8, height=height if lines <= 2 else lines * LABEL_LINE),
                   **typeface("lowpixel", 14, LABEL_LINE, rgb(ERROR)), "white-space": keyword("normal"), "overflow": keyword("hidden"),
                   "display": keyword("none"), "opacity": number(1)})
    doc.bind(f"{ident}-reason.text", f"{ident}-reason", "text", {"state": f"{key}.reason"})
    doc.bind(f"{ident}-reason.display", f"{ident}-reason", "display", {"op": "select", "args": [unavailable, "block", "none"]})
    spec = {"role": "button", "label": accessible, "event": event, "states": row_states(doc, ident)}
    place = absolute(left=0, top=top, width=width, height=height) if top is not None else {
        "position": keyword("relative"), "width": length(width), "height": length(height),
        "margin-bottom": length(MATCH_ROW_PITCH - VOTE_ROW_H), "flex-shrink": number(0)}
    row = group(ident, {**place, "display": keyword("none"), "opacity": number(1), "transform": transform()},
                [plate, wash, rail, head, reason], control=spec)
    doc.bind(f"{ident}.display", ident, "display", {"op": "select", "args": [{"state": f"{key}.shown"}, "block", "none"]})
    doc.bind(f"{ident}.opacity", ident, "opacity", {"op": "select", "args": [{"state": f"{key}.available"}, 1, 0.6]})
    shake = f"shake-{ident}"
    doc.timelines.add(shake, 300, [
        track(ident, "transform", [(0, transform()), (50, transform(tx=-6)), (110, transform(tx=6)), (170, transform(tx=-4)),
                                   (230, transform(tx=3)), (300, transform())]),
        track(f"{ident}-reason", "opacity", [(0, number(1)), (100, number(0.35)), (200, number(1)), (300, number(1))]),
    ])
    doc.events[event] = [
        {"op": "if", "condition": {"state": f"{key}.available"}, "then": steps, "else": [{"op": "playTimeline", "timeline": shake}]},
    ]
    return row, ident


# The triggers' glyphs at the trailing end of an inner strip (section 13.5):
# the triggers step its sections, and the strip shows which buttons do, for
# a controller only, since no key steps them.
TRIGGER_W, TRIGGER_GAP = 22.0, 4.0


def section_triggers(doc: Document, ident: str, keys: tuple, page_width: float) -> dict:
    """The left and right triggers' glyphs at the strip's trailing end, a
    legend shown only while prompt.glyphs names a controller family. The
    strip's sections (`keys`, each in a 10 dp padded slot 4 dp apart) must
    end before them in every shipped language; generation fails otherwise."""
    room = page_width - 2 * TRIGGER_W - TRIGGER_GAP - 8
    for language in b.LANGUAGES:
        used = sum(20 + 4 + b.text_width("lowpixel", b.engine_text(key)[language], 16) for key in keys)
        if used > room:
            raise SystemExit(f"{ident}'s sections in {language} take {used:.0f} dp, past the triggers at {room:.0f} dp")
    glyphs = [group(f"{ident}-{side}", {"position": keyword("relative"), "display": keyword("block"), "width": length(TRIGGER_W),
                                       "height": length(PROMPT_H), **({"margin-left": length(TRIGGER_GAP)} if side == "rt" else {})},
                    b.device_glyphs(doc, f"{ident}-{side}", position, [1, 1, 1, 0.85], PROMPT_H, TRIGGER_W))
              for side, position in (("lt", "left-trigger"), ("rt", "right-trigger"))]
    return group(ident, {**absolute(right=0, top=(MATCH_STRIP_H - PROMPT_H) / 2, height=PROMPT_H), "display": keyword("flex"),
                         "flex-direction": keyword("row"), "pointer-events": keyword("none")}, glyphs)


def match_tab(doc: Document, position: int, section: str, key: str) -> tuple[dict, str]:
    """One of the inner strip's sections: its name, orange with focus, over a
    bar while it is the section shown."""
    ident = f"match-tab-{section}"
    selected = {"op": "==", "args": [{"state": "card.match_section"}, position]}
    caption = label(f"{ident}-label", key, {"position": keyword("relative"), "display": keyword("block"),
                    **typeface("lowpixel", 16, MATCH_STRIP_H, [1, 1, 1, 0.7]), "white-space": keyword("nowrap")})
    bar = vector(f"{ident}-bar", {**absolute(left=0, top=MATCH_STRIP_H - 3, width=0, height=3), "width": length(100, "%"),
                                  "display": keyword("none")},
                 [path("bar", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, 3), (0, 3)], fill=solid(rgb(ORANGE)))])
    doc.bind(f"{ident}-bar.display", f"{ident}-bar", "display", {"op": "select", "args": [selected, "block", "none"]})
    states = doc.states(ident, {
        "default": [(f"{ident}-label", "color", colour([1, 1, 1, 0.7]))],
        "hover": [(f"{ident}-label", "color", colour([1, 1, 1, 1]))],
        "focus": [(f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "pressed": [(f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "disabled": [(f"{ident}-label", "color", colour([1, 1, 1, 0.7]))],
    })
    event = f"match_section_{section}"
    doc.events[event] = [{"op": "setState", "values": {"card.match_section": position}}, {"op": "focus", "control": ident}]
    tab_node = group(ident, {"position": keyword("relative"), "display": keyword("block"), "height": length(MATCH_STRIP_H),
                             "padding-left": length(10), "padding-right": length(10), "margin-right": length(4), "flex-shrink": number(0)},
                     [caption, bar], control={"role": "button", "label": key, "event": event, "states": states})
    return tab_node, ident


def match_side_chip(doc: Document, ident: str, side: int, left: float, top: float, width: float) -> dict:
    """One side of the action target as a chip: its name, underlined while it
    is the side chosen, dimmed where the player may not choose it. Choosing
    it asks the game (action_side_a or _b)."""
    caption = label(f"{ident}-label", PLACEHOLDER, {**absolute(left=10, top=0, width=width - 16, height=24),
                    **typeface("lowpixel", 15, 24, [1, 1, 1, 0.8]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    doc.bind(f"{ident}-label.text", f"{ident}-label", "text", {"state": f"mp.match.side{side}.label"})
    mark = vector(f"{ident}-mark", {**absolute(left=0, top=21, width=width, height=3), "display": keyword("none")},
                  [path("mark", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, 3), (0, 3)], fill=solid(rgb(VALUE)))])
    doc.bind(f"{ident}-mark.display", f"{ident}-mark", "display",
             {"op": "select", "args": [{"state": f"mp.match.side{side}.selected"}, "block", "none"]})
    states = doc.states(ident, {
        "default": [(f"{ident}-label", "color", colour([1, 1, 1, 0.8]))], "hover": [(f"{ident}-label", "color", colour([1, 1, 1, 1]))],
        "focus": [(f"{ident}-label", "color", colour(rgb(ORANGE)))], "pressed": [(f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "disabled": [(f"{ident}-label", "color", colour([1, 1, 1, 0.8]))],
    })
    event = f"match_side_{side}"
    doc.events[event] = [{"op": "if", "condition": {"state": f"mp.match.side{side}.available"},
                          "then": match_op("action_side_a" if side == 0 else "action_side_b")}]
    node = group(ident, {**absolute(left=left, top=top, width=width, height=24), "opacity": number(1)}, [caption, mark],
                 control={"role": "button", "label": "#str_41720" if side == 0 else "#str_41721", "event": event, "states": states})
    doc.bind(f"{ident}.opacity", ident, "opacity", {"op": "select", "args": [{"state": f"mp.match.side{side}.available"}, 1, 0.4]})
    return node


def match_list_row(doc: Document, key: str, row: int, width: float, columns: tuple, fields: tuple | None = None) -> tuple[dict, str]:
    """A row of one of Match Control's lists, the scoreboard's row (section
    14.14) at 22 dp: its columns as the game publishes them
    (mp.match.<key><row>.c<n>; `fields` the ones it shows, all by default,
    and `columns` each fixed column's width after the first, which takes the
    rest), the row chosen lit in olive and the focus rail at its leading
    edge. The team list tints a side's own row in its team's color. Choosing
    a row names it by its list's and its own index (card.match_list,
    card.match_row) and runs mpMatchSelect; the game checks the row against
    its model again. A list the stock page only shows (not in MATCH_LISTS)
    takes focus, so it scrolls, and chooses nothing."""
    selectable = key in MATCH_LISTS
    ident, state = f"match-{key.replace('_', '-')}-{row}", f"mp.match.{key}{row}"
    fields = fields or tuple(range(1 + len(columns)))
    # Every column the game publishes is declared, shown or not.
    for field in range(1 + max(fields)):
        doc.state[f"{state}.c{field}"] = {"type": "string", "initial": ""}
    band = group(f"{ident}-band", {**FULL, "background-color": colour([1, 1, 1, 0.06])})
    if key == "team":
        doc.state.update({f"{state}.kind": {"type": "number", "initial": -1}, f"{state}.side": {"type": "number", "initial": -1}})
        side_row = {"op": "==", "args": [{"state": f"{state}.kind"}, 0]}

        def tint(component: int) -> dict:
            marine, strogg, other = rgb(MP_MARINE)[component], rgb(MP_STROGG)[component], rgb(MP_NEUTRAL)[component]
            return {"op": "select", "args": [side_row, {"op": "select", "args": [
                {"op": "==", "args": [{"state": f"{state}.side"}, 0]}, marine,
                {"op": "select", "args": [{"op": "==", "args": [{"state": f"{state}.side"}, 1]}, strogg, other]}]}, 1]}
        doc.bind(f"{ident}-band.background-color", f"{ident}-band", "background-color",
                 [tint(0), tint(1), tint(2), {"op": "select", "args": [side_row, 0.35, 0.06]}])
    chosen = group(f"{ident}-chosen", {**FULL, "display": keyword("none"), "background-color": colour(rgb(OLIVE, 0.45))})
    if selectable:
        doc.bind(f"{ident}-chosen.display", f"{ident}-chosen", "display",
                 {"op": "select", "args": [{"op": "==", "args": [{"state": f"mp.match.{key}.selected"}, row]}, "block", "none"]})
    rail = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=4, height=MATCH_LIST_ROW_H), "opacity": number(0)},
                  [path("rail", [(0, 0), (3, 0), (3, MATCH_LIST_ROW_H), (0, MATCH_LIST_ROW_H)], fill=solid(rgb(ORANGE)))])
    cell = {"position": keyword("relative"), "display": keyword("block"), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
            "flex-shrink": number(0), **typeface("lowpixel", 14, MATCH_LIST_ROW_H, [1, 1, 1, 0.85])}
    lead = f"{ident}-c{fields[0]}"
    cells = [label(lead, PLACEHOLDER, {**cell, "flex-grow": number(1), "flex-shrink": number(1), "min-width": length(0)})]
    for field, column_width in zip(fields[1:], columns):
        cells.append(label(f"{ident}-c{field}", PLACEHOLDER, {**cell, "width": length(column_width), "margin-left": length(6),
                                                               **typeface("lowpixel", 14, MATCH_LIST_ROW_H, [1, 1, 1, 0.6])}))
    for field in fields:
        doc.bind(f"{ident}-c{field}.text", f"{ident}-c{field}", "text", {"state": f"{state}.c{field}"})
    content = group(f"{ident}-content", {**absolute(left=10, top=0, width=width - 14, height=MATCH_LIST_ROW_H), "display": keyword("flex"),
                                         "flex-direction": keyword("row"), "align-items": keyword("center"), "pointer-events": keyword("none")},
                    cells)
    states = doc.states(ident, {
        "default": [(f"{ident}-focus", "opacity", number(0)), (lead, "color", colour([1, 1, 1, 0.85]))],
        "hover": [(f"{ident}-focus", "opacity", number(0.5)), (lead, "color", colour([1, 1, 1, 1]))],
        "focus": [(f"{ident}-focus", "opacity", number(1)), (lead, "color", colour(rgb(ORANGE)))],
        "pressed": [(f"{ident}-focus", "opacity", number(1)), (lead, "color", colour(rgb(ORANGE)))],
        "disabled": [(f"{ident}-focus", "opacity", number(0)), (lead, "color", colour([1, 1, 1, 0.85]))],
    })
    event = f"match_{key}_{row}"
    doc.events[event] = [{"op": "setState", "values": {"card.match_list": MATCH_LISTS.index(key), "card.match_row": row}},
                         {"op": "action", "action": "mpMatchSelect"}] if selectable else []
    node = group(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(width),
                         "height": length(MATCH_LIST_ROW_H), "margin-bottom": length(1), "flex-shrink": number(0)},
                 [band, chosen, rail, content] if selectable else [band, rail, content],
                 control={"role": "button", "label": PLACEHOLDER, "event": event, "states": states})
    doc.bind(f"{ident}.display", ident, "display",
             {"op": "select", "args": [{"op": "<", "args": [row, {"state": f"mp.match.{key}.count"}]}, "block", "none"]})
    return node, ident


def match_list(doc: Document, key: str, rows: int, left: float, top: float, width: float, height: float,
               columns: tuple, fields: tuple | None = None) -> tuple[dict, list]:
    """One of Match Control's lists as a scrolling column of rows (focus
    scrolls a row into view, as the wheel does). Past the card's rows the
    list ends with a link to the stock page, which shows them all."""
    doc.state.update({f"mp.match.{key}.count": {"type": "number", "initial": 0},
                      f"mp.match.{key}.more": {"type": "boolean", "initial": False},
                      f"mp.match.{key}.selected": {"type": "number", "initial": -1}})
    children, controls = [], []
    for row in range(rows):
        node, control = match_list_row(doc, key, row, width, columns, fields)
        children.append(node)
        controls.append(control)
    more = link(doc, f"match-{key.replace('_', '-')}-more", HANDOFF_ACTION, event="classic_match")
    more["properties"]["display"] = keyword("none")
    more["properties"]["margin-top"] = length(4)
    doc.bind(f"{more['id']}.display", more["id"], "display", {"op": "select", "args": [{"state": f"mp.match.{key}.more"}, "block", "none"]})
    children.append(more)
    controls.append(more["id"])
    viewport = group(f"match-{key.replace('_', '-')}-list", {**absolute(left=left, top=top, width=width, height=height),
                                                             "overflow": keyword("auto")}, children)
    return viewport, controls


def match_teams_section(doc: Document, width: float, height: float) -> tuple[list, list]:
    """The Teams section (the stock page's Teams): the team and roster rows,
    each side's own row in its team's color, and the participants a
    substitution can bring in, beside the role an invitation or an assignment
    gives (or, where a Duel side can be bound, the two sides) and the team
    actions: joining a team or spectating, the queue, the roster invitation,
    leaving the roster, and a captain's or referee's actions on the row
    chosen. Remove, Substitute and Expel ask first, as the stock page does.
    Choosing a row tells the game, which checks every action against it; the
    latest result shows under the lists."""
    doc.state.update({"card.match_list": {"type": "number", "initial": -1}, "card.match_row": {"type": "number", "initial": -1},
                      "mp.match.role": {"type": "number", "initial": 1}})
    doc.session("mpMatchSelect", "mpMatchSelect")
    doc.actions["mpMatchRole"] = {"operation": "session.menuValue", "input": "number",
                                  "arguments": {"command": "mpMatchRole", "value": {"input": "value"}}}
    head = {**typeface("lowpixel", 14, 18, rgb(OLIVE)), "white-space": keyword("nowrap")}
    list_w = MATCH_TEAMS_W
    team_h = 6 * (MATCH_LIST_ROW_H + 1)
    result_h = 36.0
    # The side and the state or role columns hold their longest values in
    # every language; the name takes the rest.
    team_list, team_controls = match_list(doc, "team", MATCH_TEAM_ROWS, 0, 20, list_w, team_h, (130.0, 150.0))
    replacement_top = 20 + team_h + 8
    replacement_list, replacement_controls = match_list(doc, "replacement", MATCH_REPLACEMENT_ROWS, 0, replacement_top + 20, list_w,
                                                        height - replacement_top - 20 - result_h - 6, ())
    # The latest result, as the stock page keeps it under every section, so an
    # action's outcome shows where it was taken.
    result = label("match-teams-result", PLACEHOLDER, {**absolute(left=0, top=height - result_h, width=list_w, height=result_h),
                   **typeface("lowpixel", 15, 17, [1, 1, 1, 0.85]), "white-space": keyword("normal"), "overflow": keyword("hidden")})
    doc.bind("match-teams-result.text", "match-teams-result", "text", {"state": "mp.match.result"})
    children = [label("match-teams-head", MATCH_TEAMS_HEAD, {**absolute(left=0, top=0, width=list_w, height=18), **head}), team_list,
                label("match-replacements-head", MATCH_REPLACEMENTS_HEAD,
                      {**absolute(left=0, top=replacement_top, width=list_w, height=18), **head}), replacement_list, result]
    controls = team_controls + replacement_controls
    # The role, or where a Duel side can be bound, the side.
    right_x = list_w + 16
    right_w = width - right_x
    # The column is narrow: each reason takes up to three lines.
    value_x = check_match_labels(right_w, MATCH_TEAM_LABELS, 3)
    binding = {"state": "mp.match.op.contestant_bind.shown"}
    role = value_row(doc, "match-role", MATCH_ROLE, "choice", right_w, value_x, value={"state": "mp.match.role"}, action="mpMatchRole",
                     entries=list(MATCH_ROLES), shown={"op": "!", "args": [binding]})
    children.append(group("match-role-slot", absolute(left=right_x, top=0, width=right_w, height=VOTE_ROW_H), [role]))
    controls.append(role["id"])
    chips = group("match-bind-side", {**absolute(left=right_x, top=0, width=right_w, height=VOTE_ROW_H), "display": keyword("none")},
                  [match_side_chip(doc, f"match-bind-side-{side}", side, side * right_w / 2, 0, right_w / 2 - 12) for side in range(2)])
    doc.bind("match-bind-side.display", "match-bind-side", "display", {"op": "select", "args": [binding, "block", "none"]})
    children.append(chips)
    controls += ["match-bind-side-0", "match-bind-side-1"]
    # The actions, in a column that scrolls as focus moves down it.
    rows = []
    for name, token, accessible, body in MATCH_TEAM_ACTIONS:
        row, row_id = match_action_row(doc, name, accessible, right_w, value_x, None, match_action_steps(name, token, body), 3)
        rows.append(row)
        controls.append(row_id)
    actions_top = VOTE_ROW_H + 8
    children.append(group("match-team-actions", {**absolute(left=right_x, top=actions_top, width=right_w, height=height - actions_top),
                                                 "overflow": keyword("auto")}, rows))
    return children, controls


def match_text(doc: Document, ident: str, head: str | None, state: str, left: float, top: float, width: float, lines: int) -> list:
    """A heading in olive, where there is one, over a text the game publishes,
    wrapping to `lines` lines."""
    nodes = []
    if head is not None:
        nodes.append(label(f"{ident}-head", head, {**absolute(left=left, top=top, width=width, height=18),
                           **typeface("lowpixel", 14, 18, rgb(OLIVE)), "white-space": keyword("nowrap")}))
        top += 20
    nodes.append(label(ident, PLACEHOLDER, {**absolute(left=left, top=top, width=width, height=lines * 17),
                       **typeface("lowpixel", 15, 17, [1, 1, 1, 0.85]), "white-space": keyword("normal"), "overflow": keyword("hidden")}))
    doc.bind(f"{ident}.text", ident, "text", {"state": state})
    return nodes


def match_number_row(doc: Document, ident: str, label_key: str, width: float, value_x: float, *, value: dict, action: str,
                     maximum: int, message: str, minimum: int = 0, row_h: float = VOTE_ROW_H) -> dict:
    """A whole-number field as a setting row (section 7, with the SYSTEM page's
    precise field): the label in its column, then the value, which the player
    edits from `minimum` (0) to `maximum`; outside that the field says so in `message`
    under itself and asks nothing. Its value goes to `action`, and the game
    checks it again."""
    shape = [(6, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}), (0, {"fraction": 1, "dp": -1}), (0, 7)]
    plate = vector(f"{ident}-plate", FULL, [path("plate", shape, fill=solid(rgb(OLIVE, 0.12)), stroke=stroke(solid(rgb(OLIVE, 0.32)), 1))])
    wash = vector(f"{ident}-wash", {**FULL, "opacity": number(0)}, [
        path("wash", shape, fill=linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(OLIVE, 0.45)), (1, rgb(OLIVE, 0.1))]))])
    rail = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=4, height=row_h), "opacity": number(0)}, [
        path("rail", [(0, 7), (6, 1), (6, row_h - 1), (0, row_h - 1)], fill=solid(rgb(ORANGE)))])
    caption = label(f"{ident}-label", label_key, {"position": keyword("relative"), "display": keyword("block"), "flex-shrink": number(1),
                    "min-width": length(0), **row_face(True), "overflow": keyword("hidden")})
    head = group(f"{ident}-head", {**absolute(left=21, top=0, width=value_x - 29, height=row_h), "display": keyword("flex"),
                                   "flex-direction": keyword("row"), "align-items": keyword("center")}, [caption])
    field_w = width - value_x - 12
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    # The runtime places, sizes and shows the selection, caret and composition,
    # and blinks the caret, from these typed bases.
    ink = lambda name, tint, w, h: vector(name, {**absolute(left=0, top=0, width=w, height=h), "display": keyword("block"),
                                                 "opacity": number(1), "pointer-events": keyword("none")},
                                          [path("ink", whole, fill=solid(tint))])
    field_plate = vector(f"{ident}-field", {**FULL, "pointer-events": keyword("none")},
                         [path("field", whole, fill=solid([0, 0, 0, 0.6]), stroke=stroke(solid(rgb(OLIVE, 0.45)), 1))])
    text = label(f"{ident}-text", PLACEHOLDER, {**absolute(left=6, top=0, width=4096, height=VOTE_ROW_H - 4),
                 **typeface("lowpixel", 15, VOTE_ROW_H - 4, rgb(VALUE)), "white-space": keyword("pre"), "text-align": keyword("left")})
    viewport = group(f"{ident}-viewport", {**absolute(left=value_x - 8, top=2 + ((row_h - VOTE_ROW_H) / 2 if row_h != VOTE_ROW_H else 0), width=field_w,
                                                      height=VOTE_ROW_H - 4),
                                           "overflow": keyword("hidden")},
                     [field_plate, ink(f"{ident}-selection", rgb(ORANGE, 0.4), 0, VOTE_ROW_H - 4), text,
                      ink(f"{ident}-caret", [1, 1, 1, 1], 1, VOTE_ROW_H - 4), ink(f"{ident}-composition", rgb(ORANGE), 0, 1)])
    # The message spans the row under the field, and must fit it in every language.
    need = max(b.text_width("lowpixel", text, 13) for text in table_text(message).values())
    if need > width - 33:
        raise SystemExit(f"the number field's message needs {need:.0f} dp, past its row's {width - 33:.0f} dp")
    validation = label(f"{ident}-validation", message, {**absolute(left=21, top=row_h + 1, width=width - 33, height=16),
                       **typeface("lowpixel", 13, 16, rgb(ERROR)), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                       "display": keyword("none")})
    parts = {part: f"{ident}-{part}" for part in ("viewport", "text", "selection", "caret", "composition", "validation")}
    spec = {"role": "number", "label": label_key, "action": action, "value": value, "minimum": minimum, "maximum": maximum, "integer": True,
            "exponent": False, "maxBytes": 8, "parts": parts, "states": row_states(doc, ident)}
    return group(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(width),
                         "height": length(row_h), "opacity": number(1), "flex-shrink": number(0)},
                 [plate, wash, rail, head, viewport, validation], control=spec)


def match_section_result(doc: Document, section: str, left: float, top: float, width: float) -> dict:
    """The latest result, which the stock page keeps under every section, so an
    action's outcome shows where it was taken."""
    ident = f"match-{section}-result"
    node = label(ident, PLACEHOLDER, {**absolute(left=left, top=top, width=width, height=36),
                 **typeface("lowpixel", 15, 17, [1, 1, 1, 0.85]), "white-space": keyword("normal"), "overflow": keyword("hidden")})
    doc.bind(f"{ident}.text", ident, "text", {"state": "mp.match.result"})
    return node


def match_column_actions(doc: Document, actions: tuple, left: float, top: float, width: float, value_x: float) -> tuple[list, list]:
    """Actions stacked at the rows' pitch in a column of their own."""
    rows, controls = [], []
    for row_index, (name, token, accessible, body) in enumerate(actions):
        row, row_id = match_action_row(doc, name, accessible, width, value_x, row_index * MATCH_ROW_PITCH,
                                       match_action_steps(name, token, body))
        rows.append(row)
        controls.append(row_id)
    ident = "match-" + actions[0][0].replace("_", "-") + "-column"
    return [group(ident, absolute(left=left, top=top, width=width, height=len(actions) * MATCH_ROW_PITCH), rows)], controls


def match_proposals_section(doc: Document, width: float, height: float) -> tuple[list, list]:
    """The Proposals section (the stock page's Proposals): the running global
    and team proposals, the one a ballot or a cancellation goes to and the
    ballots, beside the proposals the player may make, Create proposal and the
    latest result. Each action says why it is unavailable."""
    doc.state.update({"mp.match.proposal.global": {"type": "string", "initial": ""},
                      "mp.match.proposal.side": {"type": "string", "initial": ""},
                      "mp.match.scope": {"type": "number", "initial": 0}})
    doc.actions["mpMatchScope"] = {"operation": "session.menuValue", "input": "number",
                                   "arguments": {"command": "mpMatchScope", "value": {"input": "value"}}}
    left_w = MATCH_PROPOSALS_W
    right_x = left_w + 16
    right_w = width - right_x
    value_x = check_match_labels(left_w, MATCH_PROPOSAL_LABELS + (MATCH_BALLOT_TARGET,))
    children = [*match_text(doc, "match-proposal-global", MATCH_SCOPES[0][0], "mp.match.proposal.global", 0, 0, left_w, 2),
                *match_text(doc, "match-proposal-side", MATCH_SCOPES[1][0], "mp.match.proposal.side", 0, 60, left_w, 2)]
    target = value_row(doc, "match-scope", MATCH_BALLOT_TARGET, "choice", left_w, value_x, value={"state": "mp.match.scope"},
                       action="mpMatchScope", entries=list(MATCH_SCOPES), wrap=True)
    children.append(group("match-scope-slot", absolute(left=0, top=122, width=left_w, height=VOTE_ROW_H), [target]))
    controls = [target["id"]]
    ballots, ballot_controls = match_column_actions(doc, MATCH_PROPOSAL_ACTIONS[1:], 0, 156, left_w, value_x)
    children += ballots
    controls += ballot_controls
    # The proposals the player may make: each operation and its ballot.
    scope_w = math.ceil(max(b.text_width("lowpixel", text, 14) for key in MATCH_PROPOSAL_SCOPE_KEYS for text in table_text(key).values()))
    proposals, proposal_controls = match_list(doc, "proposal", MATCH_PROPOSAL_ROWS, right_x, 0, right_w,
                                              MATCH_PROPOSAL_ROWS * (MATCH_LIST_ROW_H + 1), (float(scope_w),))
    children.append(proposals)
    controls += proposal_controls
    create, create_controls = match_column_actions(doc, MATCH_PROPOSAL_ACTIONS[:1], right_x, MATCH_PROPOSAL_ROWS * (MATCH_LIST_ROW_H + 1) + 8,
                                                   right_w, check_match_labels(right_w, MATCH_PROPOSAL_LABELS[:1]))
    children += create
    controls += create_controls
    children.append(match_section_result(doc, "proposals", right_x, height - 36, right_w))
    return children, controls


def match_rules_section(doc: Document, width: float, height: float) -> tuple[list, list]:
    """The Rules section (the stock page's Rules): the committed rules, the
    profiles and the rule fields, beside the value to stage for the field
    chosen, the staged changes, the rule actions and the latest result;
    Commit rules asks first. The value is a number field whose value waits on
    the game's menu, as the stock field's does."""
    doc.state.update({"mp.match.rules.summary": {"type": "string", "initial": ""},
                      "mp.match.rules.staged": {"type": "string", "initial": ""},
                      "mp.match.rule_value": {"type": "number", "initial": 0}})
    doc.actions["mpMatchRuleValue"] = {"operation": "session.menuValue", "input": "number",
                                       "arguments": {"command": "mpMatchRuleValue", "value": {"input": "value"}}}
    head = {**typeface("lowpixel", 14, 18, rgb(OLIVE)), "white-space": keyword("nowrap")}
    list_w = MATCH_RULES_W
    right_x = list_w + 16
    right_w = width - right_x
    children = match_text(doc, "match-rules-summary", None, "mp.match.rules.summary", 0, 0, list_w, 2)
    profiles, profile_controls = match_list(doc, "profile", MATCH_PROFILE_ROWS, 0, 60, list_w, 58, ())
    rule_top = 146
    # Each field's name and value (a staged one after its arrow); the type the
    # projection writes between them is left out, for the names' room.
    value_w = math.ceil(b.text_width("lowpixel", f"{MATCH_RULE_VALUE_MAX} -> {MATCH_RULE_VALUE_MAX}", 14)) + 2
    rules, rule_controls = match_list(doc, "rule", MATCH_RULE_ROWS, 0, rule_top, list_w, height - rule_top, (float(value_w),), (0, 2))
    children += [label("match-profiles-head", MATCH_PROFILE_HEAD, {**absolute(left=0, top=40, width=list_w, height=18), **head}), profiles,
                 label("match-rules-head", MATCH_RULES_HEAD, {**absolute(left=0, top=rule_top - 20, width=list_w, height=18), **head}), rules]
    controls = profile_controls + rule_controls
    value_x = check_match_labels(right_w, MATCH_RULE_LABELS + (MATCH_VALUE,))
    field = match_number_row(doc, "match-rule-value", MATCH_VALUE, right_w, value_x, value={"state": "mp.match.rule_value"},
                             action="mpMatchRuleValue", maximum=MATCH_RULE_VALUE_MAX, message=MATCH_RULE_VALUE_MESSAGE)
    children.append(group("match-rule-value-slot", absolute(left=right_x, top=0, width=right_w, height=VOTE_ROW_H), [field]))
    controls.append(field["id"])
    children += match_text(doc, "match-rules-staged", MATCH_STAGED_HEAD, "mp.match.rules.staged", right_x, 50, right_w, 2)
    actions, action_controls = match_column_actions(doc, MATCH_RULE_ACTIONS, right_x, 112, right_w, value_x)
    children += actions
    controls += action_controls
    children.append(match_section_result(doc, "rules", right_x, height - 36, right_w))
    return children, controls


def match_series_section(doc: Document, width: float, height: float) -> tuple[list, list]:
    """The Series section (the stock page's Series): the series and its state,
    the map pool and the veto and map history, beside the format a staged
    series takes and the series and veto actions, each saying why it is
    unavailable, in a column that scrolls; Start series, Cancel series,
    Advance map and every veto choice ask first, as the stock page does. A
    map is chosen as a row is on Teams."""
    doc.state.update({"mp.match.series.summary": {"type": "string", "initial": ""},
                      "mp.match.series_profile": {"type": "number", "initial": 0}})
    doc.actions["mpMatchSeriesProfile"] = {"operation": "session.menuValue", "input": "number",
                                           "arguments": {"command": "mpMatchSeriesProfile", "value": {"input": "value"}}}
    head = {**typeface("lowpixel", 14, 18, rgb(OLIVE)), "white-space": keyword("nowrap")}
    list_w = MATCH_SERIES_W
    right_x = list_w + 16
    right_w = width - right_x
    widest = lambda keys: math.ceil(max(b.text_width("lowpixel", text, 14) for key in keys for text in table_text(key).values())) + 2
    side_w = widest(MATCH_SIDE_KEYS)
    # Each map's name, disposition and the side that chose it; each veto's or
    # map's name, action or outcome, and side or score (the stock page's
    # fourth columns, the starting and winning sides, are left out for the
    # names' room; the series' summary names the current map's side).
    children = match_text(doc, "match-series-summary", None, "mp.match.series.summary", 0, 0, list_w, 2)
    maps, map_controls = match_list(doc, "series_map", MATCH_SERIES_MAP_ROWS, 0, 60, list_w, 4 * (MATCH_LIST_ROW_H + 1),
                                    (float(widest(MATCH_DISPOSITION_KEYS)), float(side_w)), (0, 1, 2))
    history_top = 60 + 4 * (MATCH_LIST_ROW_H + 1) + 26
    history, history_controls = match_list(doc, "series_history", MATCH_HISTORY_ROWS, 0, history_top, list_w,
                                           height - history_top - 42, (float(widest(MATCH_HISTORY_KEYS)), float(side_w)), (0, 1, 2))
    children += [label("match-map-pool-head", MATCH_MAP_POOL_HEAD, {**absolute(left=0, top=40, width=list_w, height=18), **head}), maps,
                 label("match-history-head", MATCH_HISTORY_HEAD, {**absolute(left=0, top=history_top - 20, width=list_w, height=18), **head}),
                 history, match_section_result(doc, "series", 0, height - 36, list_w)]
    controls = map_controls + history_controls
    # The column is narrow: each reason takes up to three lines.
    value_x = check_match_labels(right_w, MATCH_SERIES_LABELS + (MATCH_SERIES_FORMAT,), 3)
    format_row = value_row(doc, "match-series-format", MATCH_SERIES_FORMAT, "choice", right_w, value_x,
                           value={"state": "mp.match.series_profile"}, action="mpMatchSeriesProfile", entries=list(MATCH_SERIES_PROFILES),
                           wrap=True)
    children.append(group("match-series-format-slot", absolute(left=right_x, top=0, width=right_w, height=VOTE_ROW_H), [format_row]))
    controls.append(format_row["id"])
    rows = []
    for name, token, accessible, body in MATCH_SERIES_ACTIONS:
        row, row_id = match_action_row(doc, name, accessible, right_w, value_x, None, match_action_steps(name, token, body), 3)
        rows.append(row)
        controls.append(row_id)
    actions_top = VOTE_ROW_H + 8
    children.append(group("match-series-actions", {**absolute(left=right_x, top=actions_top, width=right_w, height=height - actions_top),
                                                   "overflow": keyword("auto")}, rows))
    return children, controls


def match_evidence_section(doc: Document, width: float, height: float) -> tuple[list, list]:
    """The Evidence section (the stock page's Evidence, read-only): the
    evidence's state (recording, the match report, the multi-view demo and
    the events recorded) and the most recent evidence, with Refresh, which
    asks the game to project its view again."""
    doc.state["mp.match.evidence.summary"] = {"type": "string", "initial": ""}
    head = {**typeface("lowpixel", 14, 18, rgb(OLIVE)), "white-space": keyword("nowrap")}
    list_w = MATCH_SERIES_W
    children = match_text(doc, "match-evidence-summary", MATCH_EVIDENCE_HEAD, "mp.match.evidence.summary", 0, 0, list_w, 3)
    recent, recent_controls = match_list(doc, "evidence", MATCH_EVIDENCE_ROWS, 0, 99, list_w, MATCH_EVIDENCE_ROWS * (MATCH_LIST_ROW_H + 1), ())
    children += [label("match-recent-head", MATCH_RECENT_HEAD, {**absolute(left=0, top=79, width=list_w, height=18), **head}), recent]
    doc.events["match_evidence_refresh"] = match_op("refresh")
    refresh = link(doc, "match-evidence-refresh", MATCH_REFRESH, event="match_evidence_refresh")
    children.append(group("match-evidence-refresh-slot", absolute(left=list_w + 16, top=0, width=width - list_w - 16, height=24), [refresh]))
    return children, recent_controls + [refresh["id"]]


def match_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, list]:
    """The Match page (section 14.18): Match Control's six sections as a second,
    smaller strip. Status shows the match's phase and state lines, the side an
    action applies to where the player chooses one, the referee's sign-in, a
    spectator's camera, the last result, and the readiness, pause and match
    actions, each saying why it is unavailable; Force ready, Forfeit and Abort
    ask first, as the stock page does. Teams, Proposals, Rules, Series and
    Evidence follow it (match_teams_section and the builders after it). The
    referee signs in with the password in a masked field; the rows past the
    card's still open the stock page (classic_match). The page asks the game to
    project its view again as it opens; the game mirrors Match Control's own
    states into mp.match.*."""
    doc.session("mpMatch", "mpMatch")
    doc.state.update({
        "card.match_section": {"type": "number", "initial": 0},
        "card.match_op": {"type": "number", "initial": -1},
        "mp.match.available": {"type": "boolean", "initial": False},
        "mp.match.phase": {"type": "string", "initial": ""},
        "mp.match.result": {"type": "string", "initial": ""},
        "mp.match.follow": {"type": "boolean", "initial": False},
        "mp.match.side.shown": {"type": "boolean", "initial": False},
        "mp.match.side.label": {"type": "string", "initial": ""},
        **{f"mp.match.status{line}": {"type": "string", "initial": ""} for line in range(MATCH_STATUS_LINES)},
        **{f"mp.match.side{side}.{field}": {"type": kind, "initial": False if kind == "boolean" else ""}
           for side in range(2) for field, kind in (("label", "string"), ("available", "boolean"), ("selected", "boolean"))},
    })
    # What the page cannot edit yet (the referee credential, a text field) and
    # the rows past the card's open the stock page.
    doc.events["classic_match"] = [{"op": "setState", "values": {"card.stock_page": index}}, {"op": "action", "action": "mpStockPage"}]
    page_width = width - 2 * INSET
    page_height = height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16
    controls: list = []
    # The inner strip.
    tabs = []
    for position, (section, key) in enumerate(zip(MATCH_SECTIONS, MATCH_SECTION_KEYS)):
        tab_node, tab_id = match_tab(doc, position, section, key)
        tabs.append(tab_node)
        controls.append(tab_id)
    strip = group("match-strip", {**absolute(left=0, top=0, width=page_width, height=MATCH_STRIP_H), "display": keyword("flex"),
                                  "flex-direction": keyword("row"), "align-items": keyword("center")},
                  [*tabs, section_triggers(doc, "match-triggers", MATCH_SECTION_KEYS, page_width)])
    rule = vector("match-strip-rule", absolute(left=0, top=MATCH_STRIP_H, width=page_width, height=1), [
        path("rule", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, 1), (0, 1)], fill=solid(rgb(OLIVE, 0.5)))])
    section_top = MATCH_STRIP_H + MATCH_STRIP_GAP
    section_height = page_height - section_top
    for name, step in (("onSectionPrevious", -1), ("onSectionNext", 1)):
        steps: list = []
        for position in reversed(range(len(MATCH_SECTIONS))):
            target = MATCH_SECTIONS[(position + step) % len(MATCH_SECTIONS)]
            steps = [{"op": "if", "condition": {"op": "==", "args": [{"state": "card.match_section"}, position]},
                      "then": [{"op": "call", "event": f"match_section_{target}"}], **({"else": steps} if steps else {})}]
        doc.events[name] = [{"op": "if", "condition": {"op": "==", "args": [{"state": "card.tab"}, index]}, "then": steps}]

    def section_group(section: str, position: int, children: list) -> dict:
        node = group(f"match-section-{section}", {**absolute(left=0, top=section_top, width=page_width, height=section_height),
                                                  "display": keyword("none")}, children)
        doc.bind(f"match-section-{section}.display", f"match-section-{section}", "display",
                 {"op": "select", "args": [{"op": "==", "args": [{"state": "card.match_section"}, position]}, "block", "none"]})
        return node

    # Status: the match's state in the leading column, its actions beside it.
    left_w = MATCH_LEFT_W
    right_x = left_w + 16
    right_w = page_width - right_x
    value_x = check_match_labels(right_w, MATCH_STATUS_LABELS)
    # The leading column flows: the phase, the state lines that are set (each
    # wraps), then the action target, the referee and a spectator's camera.
    flow = {"position": keyword("relative"), "display": keyword("block")}
    column = [label("match-phase", PLACEHOLDER, {**flow, "height": length(22), "margin-bottom": length(6),
                                                  **typeface("marine", 17, 22, [1, 1, 1, 0.9]), "white-space": keyword("nowrap"),
                                                  "overflow": keyword("hidden")})]
    doc.bind("match-phase.text", "match-phase", "text", {"state": "mp.match.phase"})
    for line in range(MATCH_STATUS_LINES):
        node_id = f"match-status-{line}"
        column.append(label(node_id, PLACEHOLDER, {**flow, **typeface("lowpixel", 15, 17, [1, 1, 1, 0.8]), "white-space": keyword("normal")}))
        doc.bind(f"{node_id}.text", node_id, "text", {"state": f"mp.match.status{line}"})
        doc.bind(f"{node_id}.display", node_id, "display",
                 {"op": "select", "args": [{"op": "!=", "args": [{"state": f"mp.match.status{line}"}, ""]}, "block", "none"]})
    # The side an action applies to, where the player chooses one.
    side_children = [label("match-side-label", PLACEHOLDER, {**absolute(left=0, top=0, width=left_w, height=18),
                                                             **typeface("lowpixel", 14, 18, rgb(OLIVE)), "white-space": keyword("nowrap")})]
    doc.bind("match-side-label.text", "match-side-label", "text", {"state": "mp.match.side.label"})
    for side in range(2):
        side_children.append(match_side_chip(doc, f"match-side-{side}", side, side * left_w / 2, 20, left_w / 2 - 12))
        controls.append(f"match-side-{side}")
    side_row = group("match-side", {**flow, "height": length(46), "margin-top": length(10), "display": keyword("none")}, side_children)
    doc.bind("match-side.display", "match-side", "display", {"op": "select", "args": [{"state": "mp.match.side.shown"}, "block", "none"]})
    column.append(side_row)
    # The referee: while the player may sign in, a masked field takes the
    # referee password and Enter signs in with it (mpRefereeLogin, which hands
    # it to the game's menu as the stock field did); Sign out asks the game.
    # Either says why it is unavailable.
    from retained_create_server import text_row as field_row
    login, logout = "mp.match.op.referee_login", "mp.match.op.referee_logout"
    for op_key in (login, logout):
        for field, kind in (("shown", "boolean"), ("available", "boolean"), ("label", "string"), ("reason", "string"), ("detail", "string")):
            doc.state[f"{op_key}.{field}"] = {"type": kind, "initial": False if kind == "boolean" else ""}
    doc.session("mpRefereeLogin", "mpRefereeLogin")
    doc.state["card.referee_credential"] = {"type": "string", "initial": ""}
    doc.events["match_referee_entered"] = [{"op": "if", "condition": {"state": f"{login}.available"},
                                            "then": [{"op": "action", "action": "mpRefereeLogin"}]}]
    doc.events["match_referee_logout"] = [{"op": "if", "condition": {"state": f"{logout}.available"}, "then": match_op("referee_logout")}]
    sign_in = field_row(doc, "match-referee-login", MATCH_SIGN_IN, left_w - 114, 8, "card.referee_credential", "match_referee_entered",
                        REFEREE_BYTES, 26, allow_empty=True, masked=True, caption=False)
    sign_out = link(doc, "match-referee-logout", MATCH_SIGN_OUT, event="match_referee_logout")
    for node, op_key in ((sign_in, login), (sign_out, logout)):
        node["properties"]["display"] = keyword("none")
        doc.bind(f"{node['id']}.display", node["id"], "display", {"op": "select", "args": [{"state": f"{op_key}.shown"}, "block", "none"]})
        controls.append(node["id"])
    referee_reason = label("match-referee-reason", PLACEHOLDER, {**flow, **typeface("lowpixel", 14, 16, rgb(ERROR)),
                                                                 "white-space": keyword("normal")})
    shown_reason = {"op": "select", "args": [{"state": f"{login}.shown"},
                                             {"op": "select", "args": [{"state": f"{login}.available"}, "", {"state": f"{login}.reason"}]},
                                             {"op": "select", "args": [{"state": f"{logout}.available"}, "", {"state": f"{logout}.reason"}]}]}
    doc.bind("match-referee-reason.text", "match-referee-reason", "text", shown_reason)
    referee = group("match-referee", {**flow, "margin-top": length(8)}, [
        group("match-referee-head", {**flow, "height": length(26), "display": keyword("flex"), "flex-direction": keyword("row"),
                                     "align-items": keyword("center")}, [
            label("match-referee-label", MATCH_REFEREE, {"position": keyword("relative"), "display": keyword("block"),
                  "width": length(110), "flex-shrink": number(0), **typeface("lowpixel", 15, 22, rgb(OLIVE)), "white-space": keyword("nowrap")}),
            sign_in, sign_out]),
        referee_reason,
    ])
    column.append(referee)
    # A spectator steps the camera through the players it may follow.
    follow_links = []
    for name, token, key in MATCH_FOLLOW:
        event = f"match_follow_{name}"
        doc.events[event] = match_op(token)
        node = link(doc, f"match-follow-{name}", key, event=event)
        follow_links.append(node)
        controls.append(node["id"])
    follow = group("match-follow", {**flow, "height": length(22), "margin-top": length(8), "display": keyword("none"),
                                    "flex-direction": keyword("row"), "align-items": keyword("center")}, follow_links)
    doc.bind("match-follow.display", "match-follow", "display", {"op": "select", "args": [{"state": "mp.match.follow"}, "flex", "none"]})
    column.append(follow)
    # The actions, each a row.
    rows = []
    for row_index, (name, token, accessible, body) in enumerate(MATCH_STATUS_ACTIONS):
        row, row_id = match_action_row(doc, name, accessible, right_w, value_x, row_index * MATCH_ROW_PITCH,
                                       match_action_steps(name, token, body))
        rows.append(row)
        controls.append(row_id)
    stacked = len(MATCH_STATUS_ACTIONS) * MATCH_ROW_PITCH
    actions = group("match-actions", absolute(left=right_x, top=0, width=right_w, height=stacked), rows)
    result = label("match-result", PLACEHOLDER, {**absolute(left=right_x, top=stacked + 4, width=right_w, height=section_height - stacked - 4),
                   **typeface("lowpixel", 15, 17, [1, 1, 1, 0.85]), "white-space": keyword("normal"), "overflow": keyword("hidden")})
    doc.bind("match-result.text", "match-result", "text", {"state": "mp.match.result"})
    live = group("match-status-live", {**absolute(left=0, top=0, width=page_width, height=section_height), "display": keyword("none")},
                 [group("match-status-column", {**absolute(left=0, top=0, width=left_w, height=section_height), "overflow": keyword("hidden")},
                        column), actions, result])
    doc.bind("match-status-live.display", "match-status-live", "display",
             {"op": "select", "args": [{"state": "mp.match.available"}, "block", "none"]})
    waiting = label("match-waiting", MATCH_WAITING, {**absolute(left=0, top=0, width=page_width, height=24),
                    **typeface("lowpixel", 17, 24, [1, 1, 1, 0.8]), "white-space": keyword("nowrap"), "display": keyword("none")})
    doc.bind("match-waiting.display", "match-waiting", "display", {"op": "select", "args": [{"state": "mp.match.available"}, "none", "block"]})
    sections = [section_group("status", 0, [waiting, live])]
    # Teams, Proposals and Rules, each waiting for a view as Status does.
    for section, builder in (("teams", match_teams_section), ("proposals", match_proposals_section), ("rules", match_rules_section),
                             ("series", match_series_section), ("evidence", match_evidence_section)):
        section_children, section_controls = builder(doc, page_width, section_height)
        live = group(f"match-{section}-live", {**absolute(left=0, top=0, width=page_width, height=section_height),
                                               "display": keyword("none")}, section_children)
        doc.bind(f"match-{section}-live.display", f"match-{section}-live", "display",
                 {"op": "select", "args": [{"state": "mp.match.available"}, "block", "none"]})
        waiting = label(f"match-{section}-waiting", MATCH_WAITING, {**absolute(left=0, top=0, width=page_width, height=24),
                        **typeface("lowpixel", 17, 24, [1, 1, 1, 0.8]), "white-space": keyword("nowrap"), "display": keyword("none")})
        doc.bind(f"match-{section}-waiting.display", f"match-{section}-waiting", "display",
                 {"op": "select", "args": [{"state": "mp.match.available"}, "none", "block"]})
        sections.append(section_group(section, MATCH_SECTIONS.index(section), [waiting, live]))
        controls += section_controls
    # Opening the page asks the game to project its view again, then focuses
    # the section shown.
    focus_steps: list = []
    for position in reversed(range(len(MATCH_SECTIONS))):
        focus_steps = [{"op": "if", "condition": {"op": "==", "args": [{"state": "card.match_section"}, position]},
                        "then": [{"op": "focus", "control": f"match-tab-{MATCH_SECTIONS[position]}"}],
                        **({"else": focus_steps} if focus_steps else {})}]
    return page_group(doc, index, ident, width, height, [strip, rule, *sections], controls), [*match_op("refresh"), *focus_steps]


PAGE_BUILDERS = {"team": team_page, "players": players_page, "vote": vote_page, "match": match_page, "settings": settings_page,
                 "voice": voice_page, "server": server_page}


def fit_card(result: dict, width: float) -> dict:
    """A card in a view narrower than itself (5:4, portrait): the canvas
    shrinks so the card and its side margins fit the view's width; it stays
    centred. Wider views are unchanged."""
    result["canvas"]["minWidth"] = float(int(width + 2 * SIDE_MARGIN + 0.999))
    return result


def prompt_row(doc: Document, width: float, height: float, back_verb: str, trailing: list) -> dict:
    """The prompt bar inside the card (section 14.18): Resume or Spectate on
    the back key, Tabs on Q and E, Select on Enter, then the trailing
    actions, the destructive one last. Each key shows the active device's
    glyph (section 13.5): its keycap, or the controller's east button,
    shoulders and south button."""
    def prompt(ident: str, keys: list, verb: str) -> list:
        caps = [keycap(f"prompt-{ident}-{n}", key, doc=doc) for n, key in enumerate(keys)]
        for cap in caps:
            cap["properties"]["margin-right"] = length(3 if cap is not caps[-1] else 6)
        return caps + [label(f"prompt-{ident}-verb", verb, {"position": keyword("relative"), "display": keyword("block"),
                       "margin-right": length(18), **typeface("marine", 14, PROMPT_H, [1, 1, 1, 0.8]),
                       "white-space": keyword("nowrap")})]
    items = (prompt("back", [KEY_ESCAPE], back_verb) + prompt("tabs", [KEY_PREVIOUS, KEY_NEXT], VERB_TABS) +
             prompt("select", [KEY_ENTER], VERB_SELECT))
    spacer = group("prompt-spacer", {"position": keyword("relative"), "display": keyword("block"), "flex-grow": number(1),
                                     "height": length(PROMPT_H)})
    # Prompts never shrink: the card is fitted to the bar, and a bar that did
    # not fit would show past the card instead of squeezing its labels.
    for item in items + trailing:
        item["properties"]["flex-shrink"] = number(0)
    return group("prompts", {**absolute(left=INSET, top=height - PROMPT_BOTTOM - PROMPT_H, width=width - 2 * INSET, height=PROMPT_H),
                             "display": keyword("flex"), "flex-direction": keyword("row"), "align-items": keyword("center")},
                 items + [spacer] + trailing)


def backdrop(doc: Document) -> list:
    """The view behind the card, softened and never dimmed (section 14.18 with
    modal.softfocus's values, decision D1); with the opaque-backing option,
    or where the renderer cannot soften it, a centred darkening scrim with a
    vignette stands in (section 13.6)."""
    doc.state["soft_focus"] = {"type": "boolean", "initial": False, "cvar": "ui_retainedSoftFocus"}
    softened = group("scene-softfocus", {**FULL, "display": keyword("none"), "backdrop-blur": length(SOFT_FOCUS_BLUR),
                                         "backdrop-saturate": number(SOFT_FOCUS_SATURATION)})
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    scrim = vector("scrim", {**FULL, "display": keyword("block"), "opacity": number(1)}, [
        path("dim", whole, fill=solid([0, 0, 0, 0.45])),
        path("vignette", whole, fill=linear((0, 0), (0, {"fraction": 1}),
                                            [(0, [0, 0, 0, 0.6]), (0.3, [0, 0, 0, 0]), (0.7, [0, 0, 0, 0]), (1, [0, 0, 0, 0.6])])),
    ])
    doc.bind("scene-softfocus.display", "scene-softfocus", "display", {"op": "select", "args": [{"state": "soft_focus"}, "block", "none"]})
    doc.bind("scrim.display", "scrim", "display", {"op": "select", "args": [{"state": "soft_focus"}, "none", "block"]})
    return [softened, scrim]


def card_motion(doc: Document, tabs: list, focus: list) -> None:
    """Section 14.18 motion and the tab programs.

    open      the softening ramps in over 250 ms while the card rises 12 dp and
              fades in over 150 ms (ease-out); under reduced motion no rise,
              and the softening and fade take 80 ms. The current page's
              primary action takes focus.
    release   every way out closes the card at once; the softening releases
              over 250 ms while the session keeps drawing the closed card.
    tab_<id>  switches to a tab at once and cross-fades the page over 150 ms,
              then focuses its primary action; onTabPrevious and onTabNext
              wrap around the strip.

    `focus` holds each page's steps that focus its primary action.
    """
    blur, tint = length(SOFT_FOCUS_BLUR), number(SOFT_FOCUS_SATURATION)
    none_blur, none_tint = length(0), number(1)

    def opening(ident: str, ramp: float, fade: float, rise: float, essential: bool | None = None) -> None:
        # Each opening restarts from nothing at 1 ms: a play retargets only its
        # first key from the current value, and the document outlives each open.
        doc.timelines.add(ident, max(ramp, fade), essential=essential, tracks=[
            track("scene-softfocus", "backdrop-blur", [(0, none_blur), (1, none_blur), (ramp, blur)]),
            track("scene-softfocus", "backdrop-saturate", [(0, none_tint), (1, none_tint), (ramp, tint)]),
            track("scrim", "opacity", [(0, number(0)), (1, number(0)), (ramp, number(1))]),
            track("card", "opacity", [(0, number(0)), (1, number(0), EASE_OUT), (fade, number(1))]),
            track("card", "transform", [(0, transform(ty=rise)), (1, transform(ty=rise), EASE_OUT), (fade, transform())]),
        ])
    opening("open", OPEN_MS, RISE_MS, RISE_DP)
    # Reduced motion's own opening plays as authored: the runtime would
    # otherwise jump the softening to its end instead of ramping it in 80 ms.
    opening("open-reduced", REDUCED_MS, REDUCED_MS, 0, essential=True)
    doc.timelines.add("release", RELEASE_MS, [
        track("scene-softfocus", "backdrop-blur", [(0, blur), (RELEASE_MS, none_blur)]),
        track("scene-softfocus", "backdrop-saturate", [(0, tint), (RELEASE_MS, none_tint)]),
        track("scrim", "opacity", [(0, number(1)), (RELEASE_MS, number(0))]),
    ])
    doc.timelines.add("unrelease", OPEN_MS, [
        track("scene-softfocus", "backdrop-blur", [(0, none_blur), (1, none_blur), (OPEN_MS, blur)]),
        track("scene-softfocus", "backdrop-saturate", [(0, none_tint), (1, none_tint), (OPEN_MS, tint)]),
        track("scrim", "opacity", [(0, number(0)), (1, number(0)), (OPEN_MS, number(1))]),
        track("card", "opacity", [(0, number(1)), (OPEN_MS, number(1))]),
    ])
    count = len(tabs)
    stop_voice = []
    if any(ident == "voice" for ident, _key, _window in tabs):
        doc.actions["voiceTestStop"] = {"operation": "settings.player.set",
            "arguments": {"cvar": "s_voiceChatTest", "value": False}}
        stop_voice = [{"op": "if", "condition": {"state": "cvar.s_voiceChatTest"},
                       "then": [{"op": "action", "action": "voiceTestStop"}]}]

    def focus_current() -> dict:
        steps: list = []
        for index in reversed(range(count)):
            steps = [{"op": "if", "condition": {"op": "==", "args": [{"state": "card.tab"}, index]},
                      "then": focus[index], **({"else": steps} if steps else {})}]
        return steps[0]
    for index, (ident, _key, _window) in enumerate(tabs):
        page = f"page-{ident}"
        # The page arriving fades in from nothing; every other page fades from
        # where it stands to nothing, which leaves hidden pages at 0.
        doc.timelines.add(f"show-{ident}", PAGE_FADE_MS, [
            track(page, "opacity", [(0, number(0)), (1, number(0)), (PAGE_FADE_MS, number(1))]),
            *[track(f"page-{other}", "opacity", [(0, number(1)), (PAGE_FADE_MS, number(0))])
              for other, _k, _w in tabs if other != ident],
        ], complete="pageShown")
        doc.events[f"tab_{ident}"] = [
            {"op": "if", "condition": {"op": "!=", "args": [{"state": "card.tab"}, index]},
             "then": [*stop_voice, {"op": "setState", "values": {"card.leaving": {"state": "card.tab"}, "card.tab": index}},
                      {"op": "playTimeline", "timeline": f"show-{ident}"}, *focus[index]]},
        ]
    doc.events["pageShown"] = [{"op": "setState", "values": {"card.leaving": -1}}]
    for name, step in (("onTabPrevious", -1), ("onTabNext", 1)):
        steps: list = []
        for index in reversed(range(count)):
            target = tabs[(index + step) % count][0]
            steps = [{"op": "if", "condition": {"op": "==", "args": [{"state": "card.tab"}, index]},
                      "then": [{"op": "call", "event": f"tab_{target}"}], **({"else": steps} if steps else {})}]
        doc.events[name] = steps
    doc.events["open"] = [
        *stop_voice,
        {"op": "setState", "values": {"card.released": False, "card.leaving": -1}},
        {"op": "if", "condition": {"state": "reduced_motion"},
         "then": [{"op": "playTimeline", "timeline": "open-reduced"}], "else": [{"op": "playTimeline", "timeline": "open"}]},
        focus_current(),
    ]
    doc.events["release"] = [*stop_voice, {"op": "setState", "values": {"card.released": True}}, {"op": "playTimeline", "timeline": "release"}]
    doc.events["onActivate"] = [{"op": "if", "condition": {"state": "card.released"},
                                 "then": [{"op": "setState", "values": {"card.released": False}},
                                          {"op": "playTimeline", "timeline": "unrelease"}]}]
    doc.events["onBack"] = [{"op": "action", "action": "mpClose"}]
    # The Menu button (section 13.5) presses the Admin page's Apply while its
    # Server section has changes to apply; otherwise it closes the card, as
    # Back does. Cards without an Admin page leave the button Back.
    admin = next((index for index, (ident, _key, _window) in enumerate(tabs) if ident == "admin"), None)
    if admin is not None:
        from retained_mp_admin import APPLY, SECTIONS
        doc.events["onMenuButton"] = [{"op": "if", "condition": {"op": "&&", "args": [
            {"op": "==", "args": [{"state": "card.tab"}, admin]},
            {"op": "&&", "args": [{"op": "==", "args": [{"state": "card.admin_section"}, SECTIONS.index("server")]},
                                  {"state": f"mp.admin.{APPLY[0]}.available"}]}]},
            "then": [{"op": "call", "event": "admin_apply"}], "else": [{"op": "action", "action": "mpClose"}]}]


WELCOME_BUILDERS = {"join": join_page, "server": server_page, "players": welcome_players_page, "settings": welcome_settings_page}


def welcome_document() -> dict:
    """The Welcome card (section 14.18), which opens while the player has not
    answered the join offer, Spectate leading its prompt bar: Esc closes it
    and leaves the player spectating, and the menu key opens it again until
    the player joins or spectates. Its Settings tab hands off to the join
    panel's settings for now. It takes the Escape card's specified 648 x 477 dp
    rather than its own 570 x 402: the Players and Server pages it shares need
    the room."""
    doc = Document("openq4.mp_welcome")
    width = fitted_card_width([key for _, key, _ in WELCOME_TABS], (VERB_SPECTATE, [MAIN_MENU, LEAVE_SERVER]))
    height = ESCAPE_H
    doc.state.update({
        "card.tab": {"type": "number", "initial": 0},
        "card.leaving": {"type": "number", "initial": -1},
        "card.stock_page": {"type": "number", "initial": -1},
        "card.released": {"type": "boolean", "initial": False},
        "reduced_motion": {"type": "boolean", "initial": False, "cvar": "ui_retainedReducedMotion"},
        "mp.welcome.title": {"type": "string", "initial": ""},
        "mp.welcome.players": {"type": "string", "initial": ""},
        "mp.clock": {"type": "string", "initial": ""},
    })
    for verb in ("mpClose", "mpMainMenu", "mpDisconnect", "mpStockPage"):
        doc.session(verb, verb)
    trailing = [bound_text("header-players", doc, "mp.welcome.players", "lowpixel", 15, [1, 1, 1, 0.8])]
    pages, focus = [], []
    for index, (ident, _key, _window) in enumerate(WELCOME_TABS):
        page, primary = WELCOME_BUILDERS.get(ident, lambda d, i, n, w, h: handoff_page(d, i, n, w, h))(doc, index, ident, width, height)
        pages.append(page)
        focus.append([{"op": "focus", "control": primary}] if isinstance(primary, str) else primary)
    actions = [link(doc, "prompt-mainmenu", MAIN_MENU, action="mpMainMenu"),
               link(doc, "prompt-leave", LEAVE_SERVER, event="leaveModalShow")]
    actions[-1]["properties"]["margin-right"] = length(0)
    card = group("card", {"position": keyword("absolute"), "left": length(50, "%"), "top": length(50, "%"),
                          "width": length(width), "height": length(height), "margin-left": length(-width / 2),
                          "margin-top": length(-height / 2), "opacity": number(0), "transform": transform()},
                 [card_frame(width, height), *card_header(doc, width, "mp.welcome.title", trailing), *tab_strip(doc, width, WELCOME_TABS),
                  *pages, prompt_row(doc, width, height, VERB_SPECTATE, actions)])
    leave = confirmation(doc, "leaveModal", LEAVE_SERVER, DISCONNECT_BODY, "mpDisconnect")
    chrome = group("chrome", {**FULL, "display": keyword("block")}, [card, leave])
    doc.bind("chrome.display", "chrome", "display", {"op": "select", "args": [{"state": "card.released"}, "none", "block"]})
    root = group("screen", {**FULL, "font-family": b.font("marine"), "font-size": length(16), "color": colour([1, 1, 1, 0.8])},
                 [*backdrop(doc), chrome])
    card_motion(doc, WELCOME_TABS, focus)
    return fit_card(doc.build(root), width)


def escape_document() -> dict:
    """The Escape card (section 14.18), opened with the menu key during a
    match, Resume leading its prompt bar. Every page is built: Team, Players,
    Vote, Match, Settings, Voice, Server and Admin (retained_mp_admin)."""
    doc = Document("openq4.mp_escape")
    width, height = fitted_card_width([key for _, key, _ in ESCAPE_TABS], (VERB_RESUME, [MAIN_MENU, DISCONNECT])), ESCAPE_H
    doc.state.update({
        "card.tab": {"type": "number", "initial": 0},
        "card.leaving": {"type": "number", "initial": -1},
        "card.stock_page": {"type": "number", "initial": -1},
        "card.released": {"type": "boolean", "initial": False},
        "reduced_motion": {"type": "boolean", "initial": False, "cvar": "ui_retainedReducedMotion"},
        "mp.title": {"type": "string", "initial": ""},
        "mp.mode": {"type": "string", "initial": ""},
        "mp.clock": {"type": "string", "initial": ""},
        "mp.score": {"type": "string", "initial": ""},
    })
    for verb in ("mpClose", "mpMainMenu", "mpDisconnect", "mpStockPage"):
        doc.session(verb, verb)
    trailing = [bound_text("header-mode", doc, "mp.mode", "lowpixel", 15, [1, 1, 1, 0.7]),
                bound_text("header-clock", doc, "mp.clock", "lowpixel", 17, rgb(VALUE)),
                bound_text("header-score", doc, "mp.score", "lowpixel", 15, [1, 1, 1, 0.85])]
    from retained_mp_admin import admin_modals, admin_page
    builders = {**PAGE_BUILDERS, "admin": admin_page}
    pages, focus = [], []
    for index, (ident, _key, _window) in enumerate(ESCAPE_TABS):
        page, primary = builders.get(ident, lambda d, i, n, w, h: handoff_page(d, i, n, w, h))(doc, index, ident, width, height)
        pages.append(page)
        # A page names its primary action, or gives the steps that find it.
        focus.append([{"op": "focus", "control": primary}] if isinstance(primary, str) else primary)
    actions = [link(doc, "prompt-mainmenu", MAIN_MENU, action="mpMainMenu"),
               link(doc, "prompt-disconnect", DISCONNECT, event="disconnectModalShow")]
    actions[-1]["properties"]["margin-right"] = length(0)
    card = group("card", {"position": keyword("absolute"), "left": length(50, "%"), "top": length(50, "%"),
                          "width": length(width), "height": length(height), "margin-left": length(-width / 2),
                          "margin-top": length(-height / 2), "opacity": number(0), "transform": transform()},
                 [card_frame(width, height), *card_header(doc, width, "mp.title", trailing), *tab_strip(doc, width, ESCAPE_TABS),
                  *pages, prompt_row(doc, width, height, VERB_RESUME, actions)])
    disconnect = confirmation(doc, "disconnectModal", DISCONNECT, DISCONNECT_BODY, "mpDisconnect")
    # Every way out hides the card and its modals at once; the backdrop
    # releases behind them.
    chrome = group("chrome", {**FULL, "display": keyword("block")}, [card, disconnect, *match_modals(doc), *admin_modals(doc)])
    doc.bind("chrome.display", "chrome", "display", {"op": "select", "args": [{"state": "card.released"}, "none", "block"]})
    root = group("screen", {**FULL, "font-family": b.font("marine"), "font-size": length(16), "color": colour([1, 1, 1, 0.8])},
                 [*backdrop(doc), chrome])
    card_motion(doc, ESCAPE_TABS, focus)
    return fit_card(doc.build(root), width)
