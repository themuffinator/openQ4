"""Bound keys as glyphs on a menu page (docs/dev/ui-visual-design.md
sections 13.4 and 13.5): a keyboard key is the prompt bar's keycap, its name
in dark Lowpixel on a white plate with the 45-degree cut; the arrow keys draw
their arrow, the mouse its body with the bound button dark, and a controller
button the glyph of the family in use, the prompt bar's shapes in the same
white with dark marks.

The engine resolves what to draw (UserInterfaceRetained.cpp): a key state
holds the key's label and "<state>.glyph" its kind, the contract the HUD's
prompt chips already read (remastered_hud_mp.GLYPH_*); "<state>.arrow" names
an arrow key. A page declares one such state per slot it can show
(idUserInterfaceRetained::SetKeyBindingNames).

A chip is sized in em of its strip, whose font size is the chip's height, so
a page that grows its rows grows the strip's one length and every chip
follows; the art is drawn in fractions of each glyph's box. A page repeats
every glyph in each of its slots, so the art is the document's symbols
(Document.symbols), which each slot's nodes name."""
from __future__ import annotations

from build_retained_screens import (GLYPH_INK, Document, circle_path, face_glyph, group, keyword, label, length, number, path,
                                    point, solid, stroke, vector)
from retained_mp_menus import PLACEHOLDER

BOX = 22.0                    # dp: the box the art is drawn in, a prompt bar glyph's
PLATE = [1, 1, 1, 0.85]       # a prompt bar keycap's white
GAP = 0.3                     # em between chips
GROUP_GAP = 0.35              # em more between the last keyboard or mouse key's slot and the first controller button's
# The glyph kinds (UserInterfaceRetained.cpp PromptGlyph).
KEYCAP, LETTER = 0, 1
CROSS, CIRCLE, SQUARE, TRIANGLE = 2, 3, 4, 5
SOUTH, EAST, WEST, NORTH = 6, 7, 8, 9
BUMPER, TRIGGER = 10, 11
DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT = 12, 13, 14, 15
STICK, MENU, VIEW, HOME, TOUCHPAD, PADDLE, BUTTON = 16, 17, 18, 19, 20, 21, 22
MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE, WHEEL_UP, WHEEL_DOWN, MOUSE_SIDE = 23, 24, 25, 26, 27, 28
# The kinds whose label the engine supplies and one shared label draws.
LABELLED = (LETTER, BUMPER, TRIGGER, STICK, PADDLE)


def em(value: float) -> dict:
    return length(round(value, 4), "em")


def _fractions(node: dict, width: float) -> dict:
    """The node's art, drawn in dp in a `width` x BOX box, in fractions of it."""
    def convert(value, extent):
        return {"fraction": round(value / extent, 5)} if isinstance(value, (int, float)) else value
    for drawn in node["paths"]:
        for command in drawn["commands"]:
            if "points" in command:
                command["points"] = [[convert(at[0], width), convert(at[1], BOX)] for at in command["points"]]
    return node


def _rounded(ident: str, x: float, y: float, w: float, h: float, r: float, *, top_only: bool = False, **paint) -> dict:
    """A rounded rectangle as cubic corners; `top_only` leaves the lower corners square."""
    k = 0.5523 * r
    right, bottom = x + w, y + h
    commands = [("move", [(x, bottom - (0 if top_only else r))]), ("line", [(x, y + r)]),
                ("cubic", [(x, y + r - k), (x + r - k, y), (x + r, y)]), ("line", [(right - r, y)]),
                ("cubic", [(right - r + k, y), (right, y + r - k), (right, y + r)])]
    if top_only:
        commands += [("line", [(right, bottom)]), ("line", [(x, bottom)])]
    else:
        commands += [("line", [(right, bottom - r)]), ("cubic", [(right, bottom - r + k), (right - r + k, bottom), (right - r, bottom)]),
                     ("line", [(x + r, bottom)]), ("cubic", [(x + r - k, bottom), (x, bottom - r + k), (x, bottom - r)])]
    return {"id": ident, "commands": [{"id": f"c{index}", "op": op, "points": [point(px, py) for px, py in points]}
                                      for index, (op, points) in enumerate(commands)] + [{"id": "z", "op": "close"}], **paint}


def _art(doc: Document, ident: str, symbol: str, width: float, draw) -> dict:
    """One glyph: a flex item `width` dp wide in the BOX-high box, hidden
    until its kind is the slot's. Its art is the document's `symbol`, which
    `draw` makes the first time a slot asks for it."""
    if symbol not in doc.symbols:
        doc.symbols[symbol] = _fractions({"paths": draw()}, width)["paths"]
    # Its slot never shrinks, so the glyph keeps its width without saying so.
    node = vector(ident, {"display": keyword("none"), "width": em(width / BOX), "height": em(1)}, [])
    del node["paths"]
    node["symbol"] = symbol
    return node


def _show(doc: Document, node: dict, condition: dict, shown: str = "block") -> dict:
    doc.bind(f"{node['id']}.display", node["id"], "display", {"op": "select", "args": [condition, shown, "none"]})
    return node


def _is(state: str, value: int) -> dict:
    return {"op": "==", "args": [{"state": state}, value]}


def _any(state: str, values: tuple) -> dict:
    result = _is(state, values[0])
    for value in values[1:]:
        result = {"op": "||", "args": [result, _is(state, value)]}
    return result


def _text(ident: str, size: float, more: dict | None = None) -> dict:
    """Dark Lowpixel `size` dp tall in a BOX-high line, in em of the strip."""
    return label(ident, PLACEHOLDER, {"position": keyword("relative"), "display": keyword("block"),
                                      "font-family": {"type": "font", "value": "lowpixel"}, "font-size": em(size / BOX),
                                      "line-height": em(BOX / size), "color": {"type": "color", "value": GLYPH_INK},
                                      "white-space": keyword("nowrap"), "text-align": keyword("center"), **(more or {})})


def _cap(doc: Document, ident: str, state: str, shape: str) -> dict:
    """A plate as wide as its label: the keyboard's keycap with its
    45-degree cut ("key"), or a controller's labelled button, a lozenge
    ("button")."""
    whole = {"position": keyword("absolute"), "left": length(0), "top": length(0), "width": length(100, "%"), "height": length(100, "%")}
    full, most = {"fraction": 1}, {"fraction": 1, "dp": -6}
    if shape == "key":
        outline = path("cap", [(0, 0), (full, 0), (full, full), (6, full), (0, most)], fill=solid(PLATE))
    else:
        half = {"fraction": 0.5}
        outline = path("cap", [(6, {"fraction": 0.14}), (most, {"fraction": 0.14}), (full, half), (most, {"fraction": 0.86}),
                               (6, {"fraction": 0.86}), (0, half)], fill=solid(PLATE))
    text = _text(f"{ident}-name", 12)
    doc.bind(f"{ident}-name.text", f"{ident}-name", "text", {"state": state})
    return group(ident, {"position": keyword("relative"), "display": keyword("none"), "height": em(1), "min-width": em(0.3),
                         "padding-left": em(0.35), "padding-right": em(0.35), "flex-shrink": number(0)},
                 [vector(f"{ident}-plate", whole, [outline]), text])


def _arrow(direction: int) -> list:
    """An arrow key's keycap: the plate and the arrow, 1 up, 2 down, 3 left, 4 right."""
    arrow = [(0, -6.2), (5.2, -0.6), (1.9, -0.6), (1.9, 6.2), (-1.9, 6.2), (-1.9, -0.6), (-5.2, -0.6)]
    turned = {1: lambda x, y: (x, y), 2: lambda x, y: (-x, -y), 3: lambda x, y: (y, -x), 4: lambda x, y: (-y, x)}[direction]
    return [path("cap", [(0, 0), (BOX, 0), (BOX, BOX), (6, BOX), (0, BOX - 6)], fill=solid(PLATE)),
            path("arrow", [(11 + dx, 11 + dy) for dx, dy in (turned(x, y) for x, y in arrow)], fill=solid(GLYPH_INK))]


MOUSE_W = 16.0


def _mouse(kind: int) -> list:
    """The mouse from above, the bound button dark: its left, right or
    middle button, the wheel with an arrow for a turn up or down, or a side
    button's nub at its flank."""
    left, top, width, height, split, middle = 2.0, 1.0, 12.0, 20.0, 9.6, 8.0
    ink = solid(GLYPH_INK)
    seam = stroke(ink, 1.1, minimum=1)
    paths = [_rounded("body", left, top, width, height, 5.6, fill=solid(PLATE))]
    if kind == MOUSE_LEFT:
        paths.append({"id": "button", "fill": ink, "commands": [
            {"id": "m", "op": "move", "points": [point(middle - 0.8, top + 1.4)]},
            {"id": "a", "op": "cubic", "points": [point(left + 3.6, top + 1.4), point(left + 1.4, top + 3.4), point(left + 1.4, top + 6.0)]},
            {"id": "b", "op": "line", "points": [point(left + 1.4, split - 1.0)]},
            {"id": "c", "op": "line", "points": [point(middle - 0.8, split - 1.0)]}, {"id": "z", "op": "close"}]})
    elif kind == MOUSE_RIGHT:
        right = left + width
        paths.append({"id": "button", "fill": ink, "commands": [
            {"id": "m", "op": "move", "points": [point(middle + 0.8, top + 1.4)]},
            {"id": "a", "op": "cubic", "points": [point(right - 3.6, top + 1.4), point(right - 1.4, top + 3.4), point(right - 1.4, top + 6.0)]},
            {"id": "b", "op": "line", "points": [point(right - 1.4, split - 1.0)]},
            {"id": "c", "op": "line", "points": [point(middle + 0.8, split - 1.0)]}, {"id": "z", "op": "close"}]})
    else:
        paths.append(path("seam", [(middle, top + 1.4), (middle, top + 2.6)], closed=False, stroke=seam))
    paths.append(path("split", [(left + 1.2, split), (left + width - 1.2, split)], closed=False, stroke=seam))
    if kind in (MOUSE_LEFT, MOUSE_RIGHT):
        paths.append(path("seam", [(middle, top + 1.4), (middle, split)], closed=False, stroke=seam))
    if kind in (MOUSE_MIDDLE, WHEEL_UP, WHEEL_DOWN):
        paths.append(_rounded("wheel", middle - 1.5, top + 2.8, 3.0, 5.6, 1.4, fill=ink))
    if kind == WHEEL_UP:
        paths.append(path("turn", [(middle, split + 2.2), (middle + 3.2, split + 6.2), (middle - 3.2, split + 6.2)], fill=ink))
    elif kind == WHEEL_DOWN:
        paths.append(path("turn", [(middle, split + 7.0), (middle + 3.2, split + 3.0), (middle - 3.2, split + 3.0)], fill=ink))
    elif kind == MOUSE_SIDE:
        paths.append(_rounded("nub", 0.0, split - 3.2, 2.6, 6.4, 1.2, fill=solid([1, 1, 1, 1])))
    return paths


def key_slot(doc: Document, ident: str, state: str) -> dict:
    """One keyboard or mouse key: `state` is its key state, with
    `state`.glyph and `state`.arrow beside it."""
    doc.state[state] = {"type": "string", "initial": ""}
    doc.state[f"{state}.glyph"] = {"type": "number", "initial": 0}
    doc.state[f"{state}.arrow"] = {"type": "number", "initial": 0}
    kind, arrow = f"{state}.glyph", f"{state}.arrow"
    cap = _show(doc, _cap(doc, f"{ident}-cap", state, "key"), {"op": "&&", "args": [_is(kind, KEYCAP), _is(arrow, 0)]})
    arrows = [_show(doc, _art(doc, f"{ident}-arrow-{direction}", f"key-arrow-{direction}", BOX, lambda: _arrow(direction)),
                    {"op": "&&", "args": [_is(kind, KEYCAP), _is(arrow, direction)]}) for direction in (1, 2, 3, 4)]
    mice = [_show(doc, _art(doc, f"{ident}-mouse-{value}", f"key-mouse-{value}", MOUSE_W, lambda: _mouse(value)), _is(kind, value))
            for value in (MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE, WHEEL_UP, WHEEL_DOWN)]
    # A side button's number stands in the body under the split.
    side = _show(doc, _art(doc, f"{ident}-mouse-{MOUSE_SIDE}", f"key-mouse-{MOUSE_SIDE}", MOUSE_W, lambda: _mouse(MOUSE_SIDE)),
                 _is(kind, MOUSE_SIDE))
    number_text = _text(f"{ident}-side", 9, {"position": keyword("absolute"), "left": length(12.5, "%"), "top": length(46, "%"),
                                              "width": length(75, "%"), "line-height": em(1.25)})
    doc.bind(f"{ident}-side.text", f"{ident}-side", "text", {"state": state})
    side["properties"]["position"] = keyword("relative")
    side["children"] = [number_text]
    return group(ident, {"position": keyword("relative"), "display": keyword("none"), "flex-direction": keyword("row"),
                         "align-items": keyword("center"), "height": em(1), "margin-right": em(GAP), "flex-shrink": number(0)},
                 [cap, *arrows, *mice, side])


def _pad_arts() -> dict:
    """Each controller glyph's width and art by kind."""
    ink, plate = solid(GLYPH_INK), solid(PLATE)
    line = lambda width: stroke(ink, width, minimum=1)
    disc = lambda: circle_path("disc", 11, 11, 10.6, fill=plate)
    arts = {LETTER: (BOX, [disc()])}
    for kind, family, position in ((CROSS, "playstation", "south"), (CIRCLE, "playstation", "east"), (SQUARE, "playstation", "west"),
                                   (TRIANGLE, "playstation", "north"), (SOUTH, "generic", "south"), (EAST, "generic", "east"),
                                   (WEST, "generic", "west"), (NORTH, "generic", "north")):
        arts[kind] = (BOX, face_glyph("face", family, position, PLATE)["paths"])
    arts[BUMPER] = (32.0, [_rounded("plate", 1, 4.5, 30, 13, 4, top_only=True, fill=plate)])
    arts[TRIGGER] = (24.0, [_rounded("plate", 2, 2, 20, 18, 7, top_only=True, fill=plate)])
    arm, reach = 3.6, 10.2
    plus = [(11 - arm, 11 - reach), (11 + arm, 11 - reach), (11 + arm, 11 - arm), (11 + reach, 11 - arm), (11 + reach, 11 + arm),
            (11 + arm, 11 + arm), (11 + arm, 11 + reach), (11 - arm, 11 + reach), (11 - arm, 11 + arm), (11 - reach, 11 + arm),
            (11 - reach, 11 - arm), (11 - arm, 11 - arm)]
    inset = 1.3
    lit = {DPAD_UP: (11 - arm + inset, 11 - reach + inset, 2 * (arm - inset), reach - arm),
           DPAD_DOWN: (11 - arm + inset, 11 + arm - inset, 2 * (arm - inset), reach - arm),
           DPAD_LEFT: (11 - reach + inset, 11 - arm + inset, reach - arm, 2 * (arm - inset)),
           DPAD_RIGHT: (11 + arm - inset, 11 - arm + inset, reach - arm, 2 * (arm - inset))}
    for kind, (x, y, w, h) in lit.items():
        arts[kind] = (BOX, [path("cross", plus, fill=plate), path("arm", [(x, y), (x + w, y), (x + w, y + h), (x, y + h)], fill=ink)])
    arts[STICK] = (BOX, [circle_path("rim", 11, 11, 10.4, line=stroke(plate, 1.1, minimum=1)), circle_path("cap", 11, 11, 8.2, fill=plate)])
    arts[MENU] = (BOX, [disc()] + [path(f"bar-{index}", [(6.5, 11 + dy), (15.5, 11 + dy)], closed=False, stroke=line(1.6))
                                   for index, dy in enumerate((-3.2, 0.0, 3.2))])
    arts[VIEW] = (BOX, [disc(), path("back", [(6.5, 12.6), (6.5, 6.5), (12.6, 6.5), (12.6, 9.4)], closed=False, stroke=line(1.4)),
                        path("back-foot", [(6.5, 12.6), (9.4, 12.6)], closed=False, stroke=line(1.4)),
                        path("front", [(9.4, 9.4), (15.5, 9.4), (15.5, 15.5), (9.4, 15.5)], stroke=line(1.4))])
    arts[HOME] = (BOX, [disc(), path("roof", [(5.8, 11.4), (11, 6.2), (16.2, 11.4)], closed=False, stroke=line(1.6)),
                        path("walls", [(7.6, 10.2), (7.6, 15.6), (14.4, 15.6), (14.4, 10.2)], closed=False, stroke=line(1.6))])
    arts[TOUCHPAD] = (32.0, [_rounded("plate", 1, 3.5, 30, 15, 3.5, fill=plate), circle_path("dot", 16, 11, 1.7, fill=ink)])
    arts[PADDLE] = (26.0, [_rounded("plate", 1, 3, 24, 16, 5, fill=plate)])
    return arts


PAD_ARTS = _pad_arts()


def pad_slot(doc: Document, ident: str, state: str) -> dict:
    """One controller button: `state` is its key state, with `state`.glyph
    beside it."""
    doc.state[state] = {"type": "string", "initial": ""}
    doc.state[f"{state}.glyph"] = {"type": "number", "initial": 0}
    kind = f"{state}.glyph"
    arts = [_show(doc, _art(doc, f"{ident}-kind-{value}", f"key-pad-{value}", width, lambda: paths), _is(kind, value))
            for value, (width, paths) in PAD_ARTS.items()]
    cap = _show(doc, _cap(doc, f"{ident}-cap", state, "button"), _is(kind, BUTTON))
    # The engine's label for the family in use (A, LB, RT, L3, P1) over its glyph.
    name = _text(f"{ident}-name", 10.5, {"position": keyword("absolute"), "left": length(0), "top": length(0), "width": length(100, "%"),
                                          "display": keyword("none")})
    doc.bind(f"{ident}-name.text", f"{ident}-name", "text", {"state": state})
    _show(doc, name, _any(kind, LABELLED))
    return group(ident, {"position": keyword("relative"), "display": keyword("none"), "flex-direction": keyword("row"),
                         "align-items": keyword("center"), "height": em(1), "margin-right": em(GAP), "flex-shrink": number(0)},
                 [*arts, cap, name])


def bound_keys(doc: Document, ident: str, state: str, keys: int = 2, pads: int = 2) -> list:
    """The chips of the action whose binding state is `state`: `keys`
    keyboard or mouse keys, then `pads` controller buttons, each shown while
    the action has that many (`state`.keyCount, `state`.padCount)."""
    doc.state[f"{state}.keyCount"] = {"type": "number", "initial": 0}
    doc.state[f"{state}.padCount"] = {"type": "number", "initial": 0}
    chips = []
    for family, count, build in (("key", keys, key_slot), ("pad", pads, pad_slot)):
        for slot in range(count):
            chip = build(doc, f"{ident}-{family}{slot}", f"{state}.{family}{slot}")
            _show(doc, chip, {"op": ">", "args": [{"state": f"{state}.{family}Count"}, slot]}, "flex")
            chips.append(chip)
    # The controller's buttons stand a little apart from the keyboard's keys.
    if keys and pads:
        chips[keys - 1]["properties"]["margin-right"] = em(GAP + GROUP_GAP)
    return chips
