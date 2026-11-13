"""The retained buy menu (docs/dev/ui-visual-design.md section 14.18).

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. The buy card covers the game's buy menu
(guis/buymenu.gui) as the Escape card covers its multiplayer menu: the same
card, header, tab strip, prompt bar and motion, at the Escape card's size.

Its four tabs (WEAPONS, MODS, ARMOR & AMMO, TEAM) list the game's catalog
(buyCatalog in src/mpgame/mp/Buying.cpp, read here so the two agree). Each
item is a plate: its HUD icon, its name, a lock where it cannot be bought
now (a check where the player owns it), and its price in the value colour
(the error colour where the player cannot afford it). The focused or
hovered plate shows its detail beside the list: the weapon, the price, what
the item does and, where it cannot be bought now, why; the 36 dp pitch that
fits a page's plates holds no second line for the reason. The game publishes
every item (mp.buy<i>.*) and rows the server's rules do not sell collapse,
so a page lists what this server sells; MODS says why it is empty under
Quake 4's rules. Buying runs mpBuy with the item's catalog index, which the
game checks again; the menu stays open, as the stock one does.
"""
from __future__ import annotations

import re
from pathlib import Path

import build_retained_screens as b
import hud_svg
from build_retained_screens import (EASE_OUT, FULL, MARKER, OLIVE, ORANGE, VALUE, Document, absolute, colour, group,
                                    keyword, label, length, linear, marker_path, number, path, rgb, solid, stroke, track,
                                    transform, typeface, vector)
from retained_mp_menus import (ERROR, INSET, KEY_ENTER, KEY_ESCAPE, KEY_NEXT, KEY_PREVIOUS, PAGE_TOP, PLACEHOLDER, PROMPT_BOTTOM,
                               PROMPT_H, VERB_RESUME, VERB_TABS, any_text, backdrop, bound_text, card_frame, card_header,
                               card_motion, fit_card, keycap, padlock, page_group, tab_strip, table_text)
from retained_mp_summary import card_width

ROOT = Path(__file__).resolve().parents[2]
ESCAPE_H = 477.0

BUY_TABS = [
    ("weapons", "#str_231923", None),
    ("mods", "#str_231924", None),
    ("gear", "#str_231925", None),
    ("team", "#str_231001", None),
]
CATEGORY_PAGE = {"BUY_CATEGORY_WEAPONS": "weapons", "BUY_CATEGORY_MODS": "mods", "BUY_CATEGORY_GEAR": "gear",
                 "BUY_CATEGORY_TEAM": "team"}
VERB_BUY = "#str_231922"
MODS_EMPTY = "#str_231926"
PRICE_HEADING = "#str_231927"

# Each item's icons from the HUD's atlas (section 14.16): its plate's item
# symbol, and the detail's larger drawing: the weapon wheel's drawing of the
# weapon (the weapon a mod fits), else the item's symbol.
ICONS = {
    "weapon_shotgun": ("item_shotgun", "shotgun"),
    "weapon_hyperblaster": ("item_hyperblaster", "hyperblaster"),
    "weapon_grenadelauncher": ("item_grenade", "grenadelauncher"),
    "weapon_nailgun": ("item_nailgun", "nailgun"),
    "weapon_rocketlauncher": ("item_rocket", "rocketlauncher"),
    "weapon_railgun": ("item_railgun", "railgun"),
    "weapon_lightninggun": ("item_lightning", "lightninggun"),
    "weapon_dmg": ("item_darkmatter", "darkmattergun"),
    "weapon_napalmgun": ("simple_item_napalmgun", "napalmgun"),
    "wpmod_hyperblaster_bounce1": ("item_hyperblaster", "hyperblaster"),
    "wpmod_nailgun_power": ("item_nailgun", "nailgun"),
    "wpmod_rocketlauncher_burst": ("item_rocket", "rocketlauncher"),
    "wpmod_lightninggun_chain": ("item_lightning", "lightninggun"),
    "item_armor_small": ("item_armorshard", "item_armorshard"),
    "item_armor_large": ("item_armor", "item_armor"),
    "ammorefill": ("ammo", "ammo"),
    "ammo_regen": ("item_teamammo", "item_teamammo"),
    "health_regen": ("item_teamhealth", "item_teamhealth"),
    "damage_boost": ("item_teamdamage", "item_teamdamage"),
    "fc_armor_regen": ("item_fcarmorregen", "item_fcarmorregen"),
}

LIST_W = 520.0                 # the plates' column
COLUMN_GAP = 28.0
ROW_H, ROW_GAP = 32.0, 4.0
ROW_PITCH = ROW_H + ROW_GAP
ICON_X, ICON_H, LABEL_X = 26.0, 20.0, 60.0
PRICE_W = 64.0
DETAIL_ICON_H = 48.0
LABEL_SIZE, PRICE_SIZE = 16.0, 17.0


def catalog() -> list:
    """The game's catalog, in its order: (item, category page, name key)."""
    source = (ROOT / "src" / "mpgame" / "mp" / "Buying.cpp").read_text(encoding="latin-1")
    body = source[source.index("const buyCatalogItem_t buyCatalog[] = {"):]
    body = body[:body.index("};")]
    items = []
    for match in re.finditer(r'\{\s*"(\w+)",\s*(\d+),\s*(BUY_CATEGORY_\w+),\s*(?:true|false),\s*\d+,\s*"(#str_\d+)",\s*(?:"#str_\d+"|NULL)\s*\}', body):
        items.append((match.group(1), CATEGORY_PAGE[match.group(3)], match.group(4)))
    if not items:
        raise SystemExit("could not read buyCatalog from src/mpgame/mp/Buying.cpp")
    missing = [item for item, _page, _name in items if item not in ICONS]
    if missing:
        raise SystemExit(f"buy catalog items without icons: {missing}")
    return items


def page_height(height: float) -> float:
    return height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16


def check_labels(items: list, width: float, height: float) -> None:
    """Fail generation when a plate's name and price would leave the plate,
    the detail's name its two lines or a reason its line, or a page's plates
    the page, in any language."""
    reasons = ("#str_231914", "#str_231915", "#str_231916", "#str_231917", "#str_231918", "#str_231919")
    room = LIST_W - LABEL_X - 20 - PRICE_W - 12
    over = {}
    for _item, _page, name in items:
        for language, text in any_text(name).items():
            if b.text_width("marine", text, LABEL_SIZE) > room:
                over[(name, language)] = round(b.text_width("marine", text, LABEL_SIZE))
    if over:
        raise SystemExit(f"buy plate names past their {room:.0f} dp: {over}")
    detail_w = width - 2 * INSET - LIST_W - COLUMN_GAP
    for name in {name for _item, _page, name in items}:
        for language, text in any_text(name).items():
            if b.text_width("marine", text, 22) > 2 * detail_w * 0.92:
                raise SystemExit(f"{name} needs more than the detail's two lines in {language}")
    for key in reasons:
        for language, text in table_text(key).items():
            if b.text_width("lowpixel", text, 15) > detail_w:
                raise SystemExit(f"{key} past the detail's {detail_w:g} dp in {language}")
    for language, text in table_text(MODS_EMPTY).items():
        if b.text_width("lowpixel", text, 15) > 2 * LIST_W * 0.92:
            raise SystemExit(f"{MODS_EMPTY} needs more than two lines in {language}")
    rows = max(sum(1 for _i, page, _n in items if page == ident) for ident, _key, _w in BUY_TABS)
    if rows * ROW_PITCH - ROW_GAP > page_height(height):
        raise SystemExit(f"{rows} buy plates need {rows * ROW_PITCH - ROW_GAP:g} dp, past the page's {page_height(height):g} dp")


def S(state: str) -> dict:
    return {"state": state}


def sel(condition, yes, no) -> dict:
    return {"op": "select", "args": [condition, yes, no]}


def buy_plate(doc: Document, index: int, item: str, offset, width: float, detail_w: float, page_h: float) -> tuple[dict, str]:
    """One item's plate (section 14.18's in-game ladder at the list's 32 dp
    pitch): the light plate at 0.28 rising to 1.00 with the orange marker and
    inset rail when hot, the item's icon, its name in Marine, a lock and the
    reason in the error colour where it cannot be bought now (a check and the
    reason where the player owns it), and its price in the value colour.
    Choosing an unavailable one shakes it for 300 ms; an available one buys.
    Hot, it shows its detail in the column beside the list. `offset` places
    it under the shown plates before it."""
    key = f"mp.buy{index}"
    for name, kind in (("shown", "boolean"), ("available", "boolean"), ("owned", "boolean"), ("dear", "boolean"), ("label", "string"),
                       ("price", "string"), ("reason", "string"), ("detail", "string")):
        doc.state[f"{key}.{name}"] = {"type": kind, "initial": False if kind == "boolean" else ""}
    ident = f"buy-{index}"
    rest = 0.28
    band_top, band_bottom, cut = 3.0, ROW_H - 3.0, 12.0
    fill = [(0, rgb(OLIVE, 0.49)), (0.33, rgb(OLIVE, 0.49)), (1, rgb(OLIVE, 0))]
    rail = [(0, rgb(OLIVE)), (0.33, rgb(OLIVE)), (1, rgb(OLIVE, 0))]
    plate = vector(f"{ident}-plate", {**absolute(left=0, top=0, width=width, height=ROW_H), "opacity": number(rest)}, [
        path("fill", [(0, band_top), (width, band_top), (width, band_bottom), (cut, band_bottom), (0, band_bottom - cut)],
             fill=linear((0, 0), (width, 0), fill)),
        path("rail", [(0.75, band_top), (0.75, band_bottom - cut), (cut, band_bottom - 0.75), (width, band_bottom - 0.75)],
             closed=False, stroke=stroke(linear((0, 0), (width, 0), rail), 1.5)),
    ])
    inset = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=width, height=ROW_H), "opacity": number(0)}, [
        path("inset", [(3.5, band_top + 3), (3.5, band_bottom - cut + 1.5), (cut + 1.5, band_bottom - 3.5), (width - 20, band_bottom - 3.5)],
             closed=False, stroke=stroke(solid(rgb(ORANGE)), 1.2))])
    marker_rest = vector(f"{ident}-marker", {**absolute(left=10, top=9, width=7, height=7), "opacity": number(rest)},
                         [marker_path("mark", 0, 0, 7, rgb(MARKER))])
    marker_hot = vector(f"{ident}-marker-hot", {**absolute(left=10, top=9, width=7, height=7), "opacity": number(0)},
                        [marker_path("mark", 0, 0, 7, rgb(ORANGE))])
    small, large = ICONS[item]
    icon = group(f"{ident}-icon", {**absolute(left=ICON_X, top=(ROW_H - ICON_H) / 2, width=ICON_H * 2, height=ICON_H),
                                   "pointer-events": keyword("none"), "opacity": number(1)},
                 hud_svg.icon_nodes(small, f"{ident}-icon-g", 0, 0, ICON_H))
    name = label(f"{ident}-label", PLACEHOLDER, {"position": keyword("relative"), "display": keyword("block"), "flex-shrink": number(1),
                 "min-width": length(0), "overflow": keyword("hidden"), **typeface("marine", LABEL_SIZE, ROW_H, [1, 1, 1, 0.8]),
                 "white-space": keyword("nowrap"), "transform": transform()})
    doc.bind(f"{ident}-label.text", f"{ident}-label", "text", S(f"{key}.label"))
    lock = padlock(f"{ident}-lock", rgb(ERROR))
    check = vector(f"{ident}-check", {"position": keyword("relative"), "display": keyword("none"), "width": length(13),
                                      "height": length(14), "margin-left": length(8), "flex-shrink": number(0)}, [
        path("tick", [(1.5, 7.5), (5, 11), (11.5, 3)], closed=False, stroke=stroke(solid(rgb(VALUE, 0.85)), 1.8))])
    locked = {"op": "&&", "args": [{"op": "!", "args": [S(f"{key}.available")]}, {"op": "!", "args": [S(f"{key}.owned")]}]}
    doc.bind(f"{ident}-lock.display", f"{ident}-lock", "display", sel(locked, "block", "none"))
    doc.bind(f"{ident}-check.display", f"{ident}-check", "display", sel(S(f"{key}.owned"), "block", "none"))
    gap = group(f"{ident}-gap", {"position": keyword("relative"), "display": keyword("block"), "flex-grow": number(1), "height": length(1)})
    # The price in the value colour, or the error colour where it is out of
    # reach; one copy shows.
    price_props = {"position": keyword("relative"), "flex-shrink": number(0), "width": length(PRICE_W), "text-align": keyword("right"),
                   "white-space": keyword("nowrap")}
    price = label(f"{ident}-price", PLACEHOLDER, {**price_props, "display": keyword("block"),
                  **typeface("lowpixel", PRICE_SIZE, ROW_H, rgb(VALUE))})
    dear = label(f"{ident}-price-dear", PLACEHOLDER, {**price_props, "display": keyword("none"),
                 **typeface("lowpixel", PRICE_SIZE, ROW_H, rgb(ERROR))})
    for node in (f"{ident}-price", f"{ident}-price-dear"):
        doc.bind(f"{node}.text", node, "text", S(f"{key}.price"))
    doc.bind(f"{ident}-price.display", f"{ident}-price", "display", sel(S(f"{key}.dear"), "none", "block"))
    doc.bind(f"{ident}-price-dear.display", f"{ident}-price-dear", "display", sel(S(f"{key}.dear"), "block", "none"))
    row = group(f"{ident}-row", {**absolute(left=LABEL_X, top=0, width=width - LABEL_X - 12, height=ROW_H), "display": keyword("flex"),
                                 "flex-direction": keyword("row"), "align-items": keyword("center"), "pointer-events": keyword("none"),
                                 "opacity": number(1)},
                [name, lock, check, gap, price, dear])

    # The detail beside the list, shown while the plate is hot: the drawing,
    # the name, the price, why it cannot be had now (or that it is owned),
    # then what the item does.
    detail_left = width + COLUMN_GAP
    drawing_w = hud_svg.symbol_width(large, DETAIL_ICON_H)
    drawing = group(f"{ident}-drawing", absolute(left=0, top=0, width=drawing_w, height=DETAIL_ICON_H),
                    hud_svg.icon_nodes(large, f"{ident}-drawing-g", 0, 0, DETAIL_ICON_H))
    title = label(f"{ident}-detail-name", PLACEHOLDER, {**absolute(left=0, top=DETAIL_ICON_H + 12, width=detail_w, height=56),
                  **typeface("marine", 22, 28, [1, 1, 1, 0.9]), "white-space": keyword("normal"), "overflow": keyword("hidden")})
    doc.bind(f"{ident}-detail-name.text", f"{ident}-detail-name", "text", S(f"{key}.label"))
    heading = label(f"{ident}-detail-heading", PRICE_HEADING, {**absolute(left=0, top=DETAIL_ICON_H + 74, width=detail_w, height=16),
                    **typeface("lowpixel", 13, 16, [1, 1, 1, 0.45]), "white-space": keyword("nowrap")})
    cost = label(f"{ident}-detail-price", PLACEHOLDER, {**absolute(left=0, top=DETAIL_ICON_H + 90, width=detail_w, height=28),
                 **typeface("marine", 22, 28, rgb(VALUE)), "white-space": keyword("nowrap")})
    doc.bind(f"{ident}-detail-price.text", f"{ident}-detail-price", "text", S(f"{key}.price"))
    what = label(f"{ident}-detail-text", PLACEHOLDER, {**absolute(left=0, top=DETAIL_ICON_H + 150, width=detail_w, height=60),
                 **typeface("lowpixel", 15, 20, [1, 1, 1, 0.8])})
    doc.bind(f"{ident}-detail-text.text", f"{ident}-detail-text", "text", S(f"{key}.detail"))
    why_props = {**absolute(left=0, top=DETAIL_ICON_H + 122, width=detail_w, height=20), "white-space": keyword("nowrap"),
                 "display": keyword("none")}
    why = label(f"{ident}-detail-reason", PLACEHOLDER, {**why_props, **typeface("lowpixel", 15, 20, rgb(ERROR)), "opacity": number(1)})
    owned = label(f"{ident}-detail-owned", PLACEHOLDER, {**why_props, **typeface("lowpixel", 15, 20, rgb(VALUE, 0.85))})
    for node in (f"{ident}-detail-reason", f"{ident}-detail-owned"):
        doc.bind(f"{node}.text", node, "text", S(f"{key}.reason"))
    doc.bind(f"{ident}-detail-reason.display", f"{ident}-detail-reason", "display", sel(locked, "block", "none"))
    doc.bind(f"{ident}-detail-owned.display", f"{ident}-detail-owned", "display", sel(S(f"{key}.owned"), "block", "none"))
    detail = group(f"{ident}-detail", {**absolute(left=detail_left, top=0, width=detail_w, height=page_h), "opacity": number(0),
                                       "pointer-events": keyword("none")}, [drawing, title, heading, cost, what, why, owned])
    # Its plate stands under the shown plates before it; the detail keeps the
    # page's top.
    if offset != 0:
        doc.bind(f"{ident}-detail.top", f"{ident}-detail", "top", {"op": "-", "args": [0, offset]})

    def state(plate_opacity, marker, hot, focus, tint, moved, shown_detail):
        return [(f"{ident}-plate", "opacity", number(plate_opacity)), (f"{ident}-marker", "opacity", number(marker)),
                (f"{ident}-marker-hot", "opacity", number(hot)), (f"{ident}-focus", "opacity", number(focus)),
                (f"{ident}-label", "color", colour(tint)), (f"{ident}-label", "transform", moved),
                (f"{ident}-detail", "opacity", number(shown_detail))]
    label_w = width - LABEL_X - 12
    ids = doc.states(ident, {
        "default": state(rest, rest, 0, 0, [1, 1, 1, 0.8], transform(), 0),
        "hover": state(0.8, 0, 1, 0, rgb(ORANGE), b.pop(label_w), 1),
        "focus": state(1, 0, 1, 1, rgb(ORANGE), b.pop(label_w), 1),
        "pressed": state(1, 0, 1, 1, rgb(ORANGE), b.pop(label_w, 1, 1), 1),
        "disabled": state(rest, rest, 0, 0, [1, 1, 1, 0.8], transform(), 0),
    })
    event = f"buy_{index}"
    action = f"buy{index}"
    doc.actions[action] = {"operation": "session.menu", "arguments": {"command": "mpBuy", "item": index}}
    control = {"role": "button", "label": PLACEHOLDER, "event": event, "states": ids}
    holder = group(ident, {**absolute(left=0, top=0, width=width, height=ROW_H), "display": keyword("none"), "transform": transform()},
                   [plate, inset, marker_rest, marker_hot, icon, row, detail], control=control)
    doc.bind(f"{ident}.display", ident, "display", sel(S(f"{key}.shown"), "block", "none"))
    if offset != 0:
        doc.bind(f"{ident}.top", ident, "top", offset)
    # An unavailable plate dims with its name; the reason keeps its colour.
    doc.bind(f"{ident}-row.opacity", f"{ident}-row", "opacity", sel(S(f"{key}.available"), 1, 0.6))
    doc.bind(f"{ident}-icon.opacity", f"{ident}-icon", "opacity", sel(S(f"{key}.available"), 1, 0.45))
    shake = f"shake-{ident}"
    doc.timelines.add(shake, 300, [
        track(ident, "transform", [(0, transform()), (50, transform(tx=-6)), (110, transform(tx=6)), (170, transform(tx=-4)),
                                   (230, transform(tx=3)), (300, transform())]),
        track(f"{ident}-detail-reason", "opacity", [(0, number(1)), (100, number(0.35)), (200, number(1)), (300, number(1))]),
    ])
    doc.events[event] = [{"op": "if", "condition": S(f"{key}.available"), "then": [{"op": "action", "action": action}],
                          "else": [{"op": "playTimeline", "timeline": shake}]}]
    return holder, ident


def buy_page(doc: Document, index: int, ident: str, items: list, width: float, height: float) -> tuple[dict, list]:
    """A tab's page: its plates in the catalog's order, each under the shown
    ones before it, the separator before the detail column, and for MODS the
    reason it is empty under Quake 4's rules. Focus goes to its first shown
    plate."""
    page_w = width - 2 * INSET
    page_h = page_height(height)
    detail_w = page_w - LIST_W - COLUMN_GAP
    rows, controls, before = [], [], []
    for catalog_index, (item, page, _name) in enumerate(items):
        if page != ident:
            continue
        offset = 0
        for j in before:
            term = sel(S(f"mp.buy{j}.shown"), ROW_PITCH, 0)
            offset = term if offset == 0 else {"op": "+", "args": [offset, term]}
        holder, control = buy_plate(doc, catalog_index, item, offset, LIST_W, detail_w, page_h)
        rows.append(holder)
        controls.append(control)
        before.append(catalog_index)
    separator = vector(f"page-{ident}-rule", absolute(left=LIST_W + COLUMN_GAP / 2, top=0, width=1, height=page_h), [
        path("rule", [(0.5, 0), (0.5, page_h)], closed=False, stroke=stroke(solid(rgb(OLIVE, 0.32)), 1))])
    children = [separator, *rows]
    count = f"mp.buy.{ident}_count"
    doc.state[count] = {"type": "number", "initial": 0}
    if ident == "mods":
        empty = label("page-mods-empty", MODS_EMPTY, {**absolute(left=0, top=0, width=LIST_W, height=48),
                      **typeface("lowpixel", 15, 22, [1, 1, 1, 0.75]), "display": keyword("none")})
        doc.bind("page-mods-empty.display", "page-mods-empty", "display", sel({"op": "==", "args": [S(count), 0]}, "block", "none"))
        children.append(empty)
    focus: list = []
    for catalog_index, control in reversed(list(zip(before, controls))):
        focus = [{"op": "if", "condition": S(f"mp.buy{catalog_index}.shown"), "then": [{"op": "focus", "control": control}],
                  **({"else": focus} if focus else {})}]
    return page_group(doc, index, ident, width, height, children, controls), focus


def prompt_bar(doc: Document, width: float, height: float) -> dict:
    """The prompt bar (section 14.18): Resume on the back key, Tabs on Q and E
    and Buy on Enter, each showing the active device's glyph (section 13.5)."""
    def prompt(ident: str, keys: list, verb: str) -> list:
        caps = [keycap(f"prompt-{ident}-{n}", key, doc=doc) for n, key in enumerate(keys)]
        for cap in caps:
            cap["properties"]["margin-right"] = length(3 if cap is not caps[-1] else 6)
        return caps + [label(f"prompt-{ident}-verb", verb, {"position": keyword("relative"), "display": keyword("block"),
                       "margin-right": length(18), **typeface("marine", 14, PROMPT_H, [1, 1, 1, 0.8]),
                       "white-space": keyword("nowrap")})]
    items = (prompt("back", [KEY_ESCAPE], VERB_RESUME) + prompt("tabs", [KEY_PREVIOUS, KEY_NEXT], VERB_TABS) +
             prompt("buy", [KEY_ENTER], VERB_BUY))
    for item in items:
        item["properties"]["flex-shrink"] = number(0)
    return group("prompts", {**absolute(left=INSET, top=height - PROMPT_BOTTOM - PROMPT_H, width=width - 2 * INSET, height=PROMPT_H),
                             "display": keyword("flex"), "flex-direction": keyword("row"), "align-items": keyword("center")}, items)


def buy_document() -> dict:
    """The buy card, opened by the buy menu's key (buyMenu) where the server
    sells; Esc returns to the game, as the stock buy menu's Close does."""
    doc = Document("openq4.mp_buy")
    width, height = card_width(), ESCAPE_H
    items = catalog()
    check_labels(items, width, height)
    doc.state.update({
        "card.tab": {"type": "number", "initial": 0},
        "card.leaving": {"type": "number", "initial": -1},
        "card.released": {"type": "boolean", "initial": False},
        "reduced_motion": {"type": "boolean", "initial": False, "cvar": "ui_retainedReducedMotion"},
        "mp.buy.title": {"type": "string", "initial": ""},
        "mp.buy.rules": {"type": "string", "initial": ""},
        "mp.buy.credits": {"type": "string", "initial": ""},
    })
    doc.session("mpClose", "mpClose")
    trailing = [bound_text("header-rules", doc, "mp.buy.rules", "lowpixel", 15, [1, 1, 1, 0.6]),
                bound_text("header-credits", doc, "mp.buy.credits", "lowpixel", 17, rgb(VALUE))]
    pages, focus = [], []
    for index, (ident, _key, _window) in enumerate(BUY_TABS):
        page, steps = buy_page(doc, index, ident, items, width, height)
        pages.append(page)
        focus.append(steps)
    card = group("card", {"position": keyword("absolute"), "left": length(50, "%"), "top": length(50, "%"),
                          "width": length(width), "height": length(height), "margin-left": length(-width / 2),
                          "margin-top": length(-height / 2), "opacity": number(0), "transform": transform()},
                 [card_frame(width, height), *card_header(doc, width, "mp.buy.title", trailing), *tab_strip(doc, width, BUY_TABS),
                  *pages, prompt_bar(doc, width, height)])
    chrome = group("chrome", {**FULL, "display": keyword("block")}, [card])
    doc.bind("chrome.display", "chrome", "display", sel(S("card.released"), "none", "block"))
    root = group("screen", {**FULL, "font-family": b.font("marine"), "font-size": length(16), "color": colour([1, 1, 1, 0.8])},
                 [*backdrop(doc), chrome])
    card_motion(doc, BUY_TABS, focus)
    return fit_card(doc.build(root), width)
